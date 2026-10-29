"""Compare the opt-in native palette against the supported original x86 loop.

Requires a local GOG v5 game/sh4.exe, Unicorn and a native shared-library C
compiler. No game-derived bytes are written to the repository.
"""
import argparse
import ctypes as C
import hashlib
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "recomp"))
from pe import PE
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_EBX, UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI,
    UC_X86_REG_EDI, UC_X86_REG_EIP)


class CPU(C.Structure):
    _fields_ = [(s, C.c_uint32) for s in
        ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "df",
         "eflags_ext", "fs_base")] + [("st", C.c_double * 8)] + [
         (s, C.c_uint32) for s in ("ftop", "fsw", "fcw")]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cc", default="gcc")
    ap.add_argument("--cases", type=int, default=1000)
    args = ap.parse_args()
    game = ROOT / "game/sh4.exe"
    assert hashlib.sha256(game.read_bytes()).hexdigest() == \
        "cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0"
    src = (ROOT / "port/src/d3d8/device.c").read_text()
    begin = src.index("M(SetVertexShaderConstant) {")
    end = src.index("M(GetVertexShaderConstant) {", begin)
    head = r'''
#include "x86rt.h"
#include <stdlib.h>
uint8_t *g_mem;
typedef struct { struct { uint32_t guest; } com; float vsc[96][4]; } Device;
static Device device;
static Device *g_dev = &device;
unsigned records;
#define HOSTFN uint8_t *const mb = g_mem
#define DEV Device *d = g_dev
#define ARG(n) R32(c->esp + 4u * (n))
#define STDRET(n,v) do { c->eax=(v); c->esp+=4u*((n)+1); return; } while(0)
#define M(name) static void dev_##name(CPU *c)
#define MAX_VSC 96
#define HR_OK 0u
static void dev_SetVertexShaderConstant(CPU *c);
gfn rt_lookup(uint32_t a) { return a == 0x18000 ? dev_SetVertexShaderConstant : NULL; }
int prep_set(void *dst,const void *src,size_t n) {
    if (!memcmp(dst,src,n)) return 0;
    memcpy(dst,src,n); ++records; return 1;
}
float *test_vsc(void) { device.com.guest=0x10000; return device.vsc[0]; }
void test_enable(void) {
#ifdef _WIN32
    _putenv("SH4_NATIVE_PALETTE=1");
#else
    setenv("SH4_NATIVE_PALETTE", "1", 1);
#endif
}
'''
    # Windows exports are explicit; ELF exports ordinary non-static functions.
    exports = '\n' + ('__declspec(dllexport) ' if os.name == 'nt' else '')
    head = head.replace('float *test_vsc', exports + 'float *test_vsc')
    body = src[begin:end].replace('int d3d_model_palette',
        ('__declspec(dllexport) ' if os.name == 'nt' else '') + 'int d3d_model_palette')
    with tempfile.TemporaryDirectory(prefix="sh4-palette-", ignore_cleanup_errors=True) as tmp:
        cfile = Path(tmp) / "palette.c"
        library = Path(tmp) / ("palette.dll" if os.name == "nt" else "palette.so")
        cfile.write_text(head + body)
        subprocess.run([args.cc, "-std=gnu11", "-O2", "-shared", "-fPIC",
            *(["-Wl,--export-all-symbols"] if os.name == "nt" else []),
            "-I" + str(ROOT / "port/src/rt"), str(cfile), "-o", str(library)], check=True)
        os.environ["SH4_NATIVE_PALETTE"] = "1"
        lib = C.CDLL(str(library))
        lib.test_enable()
        mem = C.create_string_buffer(32 << 20)
        C.c_void_p.in_dll(lib, "g_mem").value = C.addressof(mem)
        lib.test_vsc.restype = C.POINTER(C.c_ubyte)
        vsc = lib.test_vsc()
        lib.d3d_model_palette.argtypes = [C.POINTER(CPU)]
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(0, 32 << 20)
        pe = PE(game)
        uc.mem_write(0x428e64, pe.read(0x428e64, 0x428e9d - 0x428e64))
        registers = [UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
            UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI]
        expected = bytearray(96 * 16)
        calls = [0]

        def call_method(u, address, size, data):
            if address != 0x18000:
                return
            sp = u.reg_read(UC_X86_REG_ESP)
            ret, dev, reg, source, count = struct.unpack('<5I', bytes(u.mem_read(sp, 20)))
            assert dev == 0x10000 and count == 3
            expected[reg * 16:(reg + count) * 16] = u.mem_read(source, count * 16)
            u.reg_write(UC_X86_REG_EAX, 0)
            u.reg_write(UC_X86_REG_ESP, sp + 20)
            u.reg_write(UC_X86_REG_EIP, ret)
            calls[0] += 1

        uc.hook_add(UC_HOOK_CODE, call_method)
        rnd = random.Random(0x428e64)
        def write(address, value):
            uc.mem_write(address, value)
            C.memmove(C.addressof(mem) + address, value, len(value))

        write(0x10000, struct.pack('<I', 0x11000))
        write(0x6e7d94, struct.pack('<I', 0x10000))
        write(0x1113c, struct.pack('<I', 0x18000))
        native_records = original_calls = 0
        for case in range(args.cases):
            # All empty/full, each individual slot/gap, then arbitrary masks.
            if case == 0: mask = 0
            elif case == 1: mask = (1 << 20) - 1
            elif case < 22: mask = 1 << (case - 2)
            elif case < 42: mask = ((1 << 20) - 1) ^ (1 << (case - 22))
            else: mask = rnd.getrandbits(20)
            indices = [rnd.randrange(32) if mask >> i & 1 else 0xffffffff for i in range(20)]
            write(0x12000, struct.pack('<20I', *indices))
            write(0xf629c0, rnd.randbytes(32 * 64))
            initial = rnd.randbytes(96 * 16)
            C.memmove(vsc, initial, len(initial))
            for repeat in range(2):
                # Second upload must emit no native record when values are unchanged.
                expected[:] = C.string_at(vsc, len(initial))
                stack = bytearray(rnd.randbytes(256))
                struct.pack_into('<I', stack, 0x90, 0x12000)
                write(0x20080, bytes(stack))
                regs = [rnd.getrandbits(32) for _ in range(8)]
                regs[4], regs[7] = 0x20100, 34
                cpu = CPU()
                for (name, _), value in zip(CPU._fields_[:8], regs): setattr(cpu, name, value)
                for reg, value in zip(registers, regs): uc.reg_write(reg, value)
                calls[0] = 0
                uc.emu_start(0x428e64, 0x428e9d, count=2000)
                assert uc.reg_read(UC_X86_REG_EIP) == 0x428e9d
                C.c_uint.in_dll(lib, 'records').value = 0
                assert lib.d3d_model_palette(C.byref(cpu)) == 1
                assert [getattr(cpu, x[0]) for x in CPU._fields_[:8]] == \
                    [uc.reg_read(reg) for reg in registers], (case, repeat, 'registers')
                assert C.string_at(C.addressof(mem) + 0x20080, 256) == \
                    bytes(uc.mem_read(0x20080, 256)), (case, repeat, 'guest stack')
                assert C.string_at(vsc, len(expected)) == expected, (case, repeat, 'constants')
                count = C.c_uint.in_dll(lib, 'records').value
                assert count <= 1 and (repeat == 0 or count == 0)
                native_records += count
                original_calls += calls[0]
        # An unknown/replaced vtable method must preserve the original path.
        write(0x1113c, struct.pack('<I', 0x18004))
        saved = bytes(cpu)
        assert lib.d3d_model_palette(C.byref(cpu)) == 0 and bytes(cpu) == saved
        print(f'{args.cases * 2} original-x86/native comparisons passed: registers, stack, '
              f'constant bits, empty slots, unchanged repeat and method fallback; '
              f'{original_calls} original calls -> {native_records} native records.')
        if os.name == 'nt':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)


if __name__ == '__main__':
    main()
