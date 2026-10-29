"""Validate and package compiled Vita GPU programs; never package game assets.

Generated caches and manifests must stay outside Git. Use `prepare` with a local
format-1 export, then pass the resulting directory as SH4_SHADER_CACHE_DIR.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

EXE_SHA256 = 'cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
CONFIG = dict(optimization=3, fastmath=1, fastprecision=0, fastint=1)
ABI_HEADER = Path(__file__).resolve().parents[1]/'port/src/d3d8/shader_cache_version.h'


def abi():
    return re.search(r'^#define SH4_SHADER_CACHE_ABI "([a-z0-9]+)"$', ABI_HEADER.read_text(encoding='utf-8'), re.M)[1]


def checksum(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return value


def shader(data):
    if len(data) < 36:
        raise ValueError('Truncated serialized GPU stage')
    matrices = struct.unpack_from('<I', data)[0]
    at = 4 + matrices * 4
    if at > len(data)-32 or data[at:at+4] != b'GXP\0':
        raise ValueError('Invalid GPU program or incompatible stage metadata format')


def program(data):
    if len(data) <= 8 or len(data) > 2*1024*1024-24:
        raise ValueError('Invalid complete-program size')
    vertex = struct.unpack_from('<I', data)[0]
    if not 0 < vertex < len(data)-8:
        raise ValueError('Invalid vertex payload length')
    fragment = struct.unpack_from('<I', data, 4+vertex)[0]
    if fragment <= 0 or 8+vertex+fragment != len(data):
        raise ValueError('Invalid fragment payload length')
    shader(data[4:4+vertex])
    shader(data[8+vertex:])


def unwrap(data):
    if len(data) < 24:
        raise ValueError('Truncated program envelope')
    magic, version, length, expected = struct.unpack_from('<8sIIQ', data)
    if magic != b'SH4GXP1\0' or version != 1 or length != len(data)-24 or checksum(data[24:]) != expected:
        raise ValueError('Invalid program envelope/version/checksum')
    return data[24:]


def validate(root, format_=2):
    root = Path(root).resolve()
    manifest = json.loads((root/'manifest.json').read_text(encoding='utf-8'))
    if (manifest.get('format') != format_ or manifest.get('cache_abi') != abi() or
            manifest.get('exe_sha256') != EXE_SHA256 or manifest.get('compiler') != CONFIG or
            manifest.get('stage_backend_revision') != '000a0001'):
        raise ValueError('Cache manifest does not match this renderer, compiler configuration or supported executable')
    paths = set()
    for record in manifest['files']:
        name = record['path']
        match = re.fullmatch(r'programs/([0-9a-f]{16})\.bin|stages/([vf])/([0-9A-F]{2})/cg2-([0-9a-f]{16})\.gxp', name)
        if not match or name in paths:
            raise ValueError('Duplicate or non-cache path: '+name)
        path = (root/name).resolve()
        path.relative_to(root)
        data = path.read_bytes()
        if len(data) != record['bytes'] or hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError('Cache file changed: '+name)
        if name.startswith('programs/'):
            if record['kind'] != 'program':
                raise ValueError('Wrong program kind')
            program(data if format_ == 1 else unwrap(data))
        else:
            if record['kind'] != 'stage' or len(data) < 24 or len(data) >= 1024*1024:
                raise ValueError('Invalid stage size/kind')
            magic, length, key, _ = struct.unpack_from('<IIQQ', data)
            if (magic != 0x32434753 or length != len(data)-24 or key != int(match[4],16) or
                    match[3] != match[4][:2].upper()):
                raise ValueError('Wrong stage key/length: '+name)
            shader(data[24:])
        paths.add(name)
    if not paths or not any(p.startswith('programs/') for p in paths):
        raise ValueError('Empty program bundle')
    actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    if actual != paths | {'manifest.json'}:
        raise ValueError('Bundle contains unlisted files')
    return manifest


def prepare(source, destination):
    source, destination = Path(source), Path(destination)
    manifest = validate(source, 1)
    destination.mkdir(parents=True, exist_ok=False)
    records = []
    for record in manifest['files']:
        data = (source/record['path']).read_bytes()
        if record['kind'] == 'program':
            data = struct.pack('<8sIIQ', b'SH4GXP1\0', 1, len(data), checksum(data)) + data
        target = destination/record['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        records.append(dict(record, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    manifest = dict(manifest, format=2, files=records)
    manifest['bundle_id'] = hashlib.sha256(json.dumps(records,sort_keys=True).encode('utf-8')).hexdigest()
    (destination/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    validate(destination)
    return manifest


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='command',required=True)
    p = sub.add_parser('prepare');p.add_argument('source',type=Path);p.add_argument('destination',type=Path)
    p = sub.add_parser('validate');p.add_argument('directory',type=Path);p.add_argument('--cmake-list',type=Path)
    args = ap.parse_args()
    if args.command == 'prepare':
        manifest = prepare(args.source,args.destination)
    else:
        manifest = validate(args.directory)
        if args.cmake_list:
            names = ['manifest.json'] + sorted(r['path'] for r in manifest['files'])
            args.cmake_list.write_text('set(SH4_BUNDLED_CACHE_FILES\n'+''.join('  "'+p+'"\n' for p in names)+')\n',encoding='utf-8')
    print(f'Validated {len(manifest["files"])} compiled GPU cache files; ABI {manifest["cache_abi"]}.')


if __name__ == '__main__':
    main()
