"""The startup shortcut must not conceal missing or changed compiled stages."""
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


class Ready(ctypes.Structure):
    _fields_ = [('count', ctypes.c_uint), ('overflow', ctypes.c_int),
                ('paths', (ctypes.c_char * 256) * 512)]


with tempfile.TemporaryDirectory(prefix='sh4-ready-') as folder:
    folder = Path(folder)
    library = folder / ('ready.dll' if os.name == 'nt' else 'ready.so')
    subprocess.run([os.environ.get('CC', 'gcc'), '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(ROOT/'port/src/d3d8/shader_ready.c'), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    validator_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_char_p)
    lib.shader_ready_add.argtypes = [ctypes.POINTER(Ready), ctypes.c_char_p]
    lib.shader_ready_save.argtypes = [ctypes.c_char_p, ctypes.c_uint64, ctypes.c_uint64, ctypes.POINTER(Ready)]
    lib.shader_ready_check.argtypes = [ctypes.c_char_p, ctypes.c_uint64, ctypes.c_uint64, validator_type]
    validated = []

    @validator_type
    def validate(name):
        path = Path(os.fsdecode(name))
        validated.append(path)
        return int(path.is_file() and path.read_bytes() == b'valid compiled stage')

    marker = folder/'ready.bin'
    marker_path = os.fsencode(marker)
    ready = Ready()
    for i in range(3):
        stage = folder/f'stage{i}.bin'
        stage.write_bytes(b'valid compiled stage')
        lib.shader_ready_add(ctypes.byref(ready), os.fsencode(stage))
        lib.shader_ready_add(ctypes.byref(ready), os.fsencode(stage))
    assert ready.count == 3
    assert lib.shader_ready_save(marker_path, 0x1234, 0x5678, ctypes.byref(ready)) == 1
    original = marker.read_bytes()
    assert lib.shader_ready_check(marker_path, 0x1234, 0x5678, validate) == 1
    assert len(validated) == 3
    assert lib.shader_ready_check(marker_path, 1, 0x5678, validate) == 0
    assert lib.shader_ready_check(marker_path, 0x1234, 1, validate) == 0
    (folder/'stage1.bin').unlink()
    assert lib.shader_ready_check(marker_path, 0x1234, 0x5678, validate) == 0
    (folder/'stage1.bin').write_bytes(b'corrupted stage')
    assert lib.shader_ready_check(marker_path, 0x1234, 0x5678, validate) == 0
    (folder/'stage1.bin').write_bytes(b'valid compiled stage')
    for length in (0, 8, 39, 40, len(original)-1):
        marker.write_bytes(original[:length])
        assert lib.shader_ready_check(marker_path, 0x1234, 0x5678, validate) == 0
    for offset in (0, 8, 16, 24, 28, 32, 50):
        changed = bytearray(original)
        changed[offset] ^= 0xff
        marker.write_bytes(changed)
        assert lib.shader_ready_check(marker_path, 0x1234, 0x5678, validate) == 0
    lib.shader_ready_add(ctypes.byref(ready), b'x'*256)
    assert ready.overflow and not lib.shader_ready_save(marker_path, 0x1234, 0x5678, ctypes.byref(ready))
    print('Actual readiness code: all stages validated; missing/corrupt stage, changed configuration, invalid manifest and oversized path reject shortcut.')
    if os.name == 'nt':
        import _ctypes
        _ctypes.FreeLibrary(lib._handle)
