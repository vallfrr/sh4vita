"""Exercises the actual portable runtime loader, including corrupt prepared data."""
import ctypes
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def checksum(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return value


def sealed(data):
    data = bytearray(data)
    struct.pack_into('<Q', data, 56, checksum(data[64:]))
    return data


def synthetic():
    payload = bytearray()
    for i in range(210):
        source = b'void main() {}\n\0'
        payload += struct.pack('<6I', 0x500000 + 4*i, 0x600000, 2, 28, 4, len(source)) + source
        payload += b'\0' * (-len(payload) % 4)
    header = b'SH4VSB1\0' + struct.pack('<4I', 0x10001, 210, len(payload), 0)
    header += bytes.fromhex('cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0')
    return sealed(header + bytes(8) + payload)


class Entry(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in ('func', 'decl', 'bgra', 'stride', 'nc')] + [('source', ctypes.c_char_p)]


class Bank(ctypes.Structure):
    _fields_ = [('storage', ctypes.c_void_p), ('count', ctypes.c_uint), ('checksum', ctypes.c_uint64), ('entries', ctypes.POINTER(Entry))]


with tempfile.TemporaryDirectory(prefix='sh4-bank-') as tmp:
    tmp = Path(tmp)
    library = tmp / ('bank.dll' if os.name == 'nt' else 'bank.so')
    subprocess.run([os.environ.get('CC', 'gcc'), '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(ROOT / 'port/src/d3d8/shader_bank.c'), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.shader_bank_load.argtypes = [ctypes.c_char_p, ctypes.POINTER(Bank)]
    lib.shader_bank_load.restype = ctypes.c_int
    lib.shader_bank_free.argtypes = [ctypes.POINTER(Bank)]

    def load(data, expected):
        path = tmp / 'shaders.bin'
        path.write_bytes(data)
        bank = Bank()
        result = lib.shader_bank_load(os.fsencode(path), ctypes.byref(bank))
        assert (result == 0) == expected, result
        if expected:
            assert bank.count == 210 and bank.entries[0].source
        else:
            assert not bank.storage and not bank.entries and not bank.count
        lib.shader_bank_free(ctypes.byref(bank))

    original = synthetic()
    load(original, True)
    for size in (0, 1, 63, 64, 88, len(original)-1):
        load(original[:size], False)
    for offset in (0, 8, 12, 16, 20, 24, 56, 100):
        bad = bytearray(original)
        bad[offset] ^= 0xff
        load(bad, False)
    for offset, value in ((64, 0), (68, 0), (72, 1 << 16), (76, 3), (80, 97), (84, 0xffffffff)):
        bad = bytearray(original)
        struct.pack_into('<I', bad, offset, value)
        load(sealed(bad), False)
    bad = bytearray(original)
    bad[88 + len(b'void main() {}\n')] = 1
    load(sealed(bad), False)
    if os.environ.get('SH4PREP_TEST_BANK'):
        load(Path(os.environ['SH4PREP_TEST_BANK']).read_bytes(), True)
    print('Prepared shader bank: actual loader accepts valid banks; rejects truncated, corrupt and invalid records.')
    if os.name == 'nt':
        import _ctypes
        _ctypes.FreeLibrary(lib._handle)
