"""Characterize the sphere candidate chain against the original GOG v5 x86.

This is an oracle for a future native collision module, not a performance test.
It compiles local generated references in a temporary directory and compares
ordered candidate IDs, query writes and callback state to original machine code
and to the experimental native candidate service.
Requires the supported game/sh4.exe, regenerated port/gen, Unicorn and GCC.
No game-derived source or test data is committed.
"""
import argparse
import ctypes as C
import hashlib
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "recomp"))
from pe import PE
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX,
    UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESP, UC_X86_REG_EBP,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_FPCW,
    UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

FUNCTIONS = (0x41d670, 0x420c10, 0x41f560, 0x41f570, 0x41ac30, 0x420a70)
SIZE = 32 << 20
TREE, QUERY, SP, STOP = 0x100000, 0x130000, 0x2fff00, 0x18000


class CPU(C.Structure):
    _fields_ = [(s, C.c_uint32) for s in
        ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "df",
         "eflags_ext", "fs_base")] + [("st", C.c_double * 8)] + [
         (s, C.c_uint32) for s in ("ftop", "fsw", "fcw")]


def references():
    found = {}
    for path in sorted((ROOT / "port/gen").glob("x86_f*.c")):
        text = path.read_text(encoding="utf-8")
        for address in FUNCTIONS:
            if address in found:
                continue
            match = re.search(r"^void F_%08x_orig\(CPU \*c\) \{" % address,text,re.M)
            if not match:
                match = re.search(r"^void F_%08x\(CPU \*c\) \{" % address,text,re.M)
            if not match:
                continue
            end = text.find("\nvoid F_", match.end())
            found[address] = text[match.start():end if end >= 0 else len(text)].replace(
                "void F_%08x_orig(" % address,"void F_%08x(" % address,1)
    if set(found) != set(FUNCTIONS):
        raise RuntimeError("Regenerate port/gen: missing collision references")
    head = '#include "x86rt.h"\n#include "collision_native.h"\n#include <stdlib.h>\nuint8_t *g_mem;\n'
    head += "\n".join("void F_%08x(CPU *);" % a for a in FUNCTIONS)
    head += '''
void rt_call(CPU *c, uint32_t fn) {
    if (fn != 0x420a70) abort();
    F_00420a70(c);
}
int test_native(CPU *c) {
    uint8_t *const mb=g_mem;
    uint32_t query=R32(c->esp+8);
    Sh4CollisionView v={.data=g_mem,.size=32u<<20,.root=R32(c->esp+4),.query_address=query};
    Sh4CollisionQuery q={.count=R32(0xed7938)};
    memcpy(q.sphere,g_mem+query,20);
    memcpy(q.work,g_mem+0xed7460,sizeof q.work);
    memcpy(q.polygon,g_mem+0xed7538,sizeof q.polygon);
    if(!sh4_collision_sphere(&v,&q)) return 0;
    memcpy(g_mem+query,q.sphere,20);
    memcpy(g_mem+0xed7460,q.work,sizeof q.work);
    memcpy(g_mem+0xed7538,q.polygon,sizeof q.polygon);
    W32(0xed7938,q.count);
    c->esp+=4;
    return 1;
}
'''
    return head + "\n".join(found[a] for a in FUNCTIONS)


def scene(rnd, case):
    """Build a packed original-format tree; the first node's payload is visited first."""
    data = bytearray(0x20000)
    cursor = 0x4000
    nodes = [0, 0x100, 0x200, 0x300, 0x400, 0x500, 0x600]
    edges = {0: nodes[1:]}
    if case % 3 == 2:
        edges = {0: nodes[1:4], 0x100: nodes[4:6], 0x200: nodes[6:]}
    # Periodic saturation cases exercise the original 256-candidate cap.
    counts = [260, 0, 0, 0, 0, 0, 0] if case % 16 == 0 else [
        rnd.randrange(0, 7) for _ in nodes]
    for i, offset in enumerate(nodes):
        children = edges.get(offset, [])
        if case % 2:
            children = list(reversed(children))
        limits = [-64., -64., -64., 64., 64., 64.]
        if i and case % 4 == 0 and i % 2:
            limits = [100., 100., 100., 101., 101., 101.]
        # The original tests radius + upper - center, then center - (lower - radius).
        struct.pack_into("<3fi3fi", data, offset, *limits[3:],
                         len(children), *limits[:3], 0)
        struct.pack_into("<2I", data, offset + 0x20, counts[i], cursor - offset - 0x20)
        if children:
            struct.pack_into("<%dI" % len(children), data, offset + 0x28,
                             *(child - offset for child in children))
        for j in range(counts[i]):
            x, y = (0., 0.) if case % 16 == 0 else (
                rnd.randrange(-8, 9) * .5, rnd.randrange(-8, 9) * .5)
            z = 0. if case % 16 == 0 else rnd.randrange(-4, 5) * .5
            vertices = [(x, y, z), (x + 8., y, z), (x, y + 8., z)]
            physical = 4 if (case + i + j) % 3 == 0 else 3
            if physical == 4:
                vertices = [(x, y, z), (x + 8., y, z),
                            (x + 8., y + 8., z), (x, y + 8., z)]
            if case % 29 == 0 and j % 2:
                vertices = [vertices[0]] * physical  # degenerate plane/edges
            elif case % 5 == 1:
                vertices = [(x,y,z+.11*x-.07*y) for x,y,z in vertices]
            elif case % 5 == 2:
                vertices = [tuple(rnd.uniform(-10,10) for _ in range(3)) for _ in vertices]
            for k, v in enumerate(vertices):
                struct.pack_into("<3fI", data, cursor + k * 16, *v, 0)
            # A flagged quad tests only three vertices but advances four records.
            data[cursor + 12] = physical
            data[cursor + 13] = 1 if physical == 4 and (case + j) % 2 else 0
            cursor += physical * 16
    if case % 16 == 0:
        sphere = (2., 2., .25, -2., 1.)
    else:
        sphere = (rnd.randrange(-20, 21) * .25,
                  rnd.randrange(-20, 21) * .25,
                  rnd.randrange(-12, 13) * .25, -2.,
                  rnd.choice([0., .25, .5, 1., 2., 4.]))
    return bytes(data), struct.pack("<5f", *sphere)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cc", default="gcc")
    ap.add_argument("--cases", type=int, default=1000)
    args = ap.parse_args()
    if args.cases < 1:
        ap.error("--cases must be positive")
    game = ROOT / "game/sh4.exe"
    if hashlib.sha256(game.read_bytes()).hexdigest() != \
            "cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0":
        raise RuntimeError("Only the supported GOG v5 executable is an oracle")
    pe = PE(game)
    with tempfile.TemporaryDirectory(prefix="sh4-collision-", ignore_cleanup_errors=True) as tmp:
        source = Path(tmp) / "oracle.c"
        source.write_text(references(), encoding="utf-8")
        library = Path(tmp) / ("oracle.dll" if os.name == "nt" else "oracle.so")
        subprocess.run([args.cc, "-std=gnu11", "-O2", "-ffp-contract=off",
            "-shared", "-fPIC", *(["-Wl,--export-all-symbols"] if os.name == "nt" else []),
            "-I" + str(ROOT / "port/src/rt"), str(source),
            str(ROOT / "port/src/rt/collision_native.c"), "-o", str(library), "-lm"], check=True)
        lib = C.CDLL(str(library))
        memory = C.create_string_buffer(SIZE)
        C.c_void_p.in_dll(lib, "g_mem").value = C.addressof(memory)
        lib.F_0041d670.argtypes = [C.POINTER(CPU)]
        lib.test_native.argtypes = [C.POINTER(CPU)]
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(0, SIZE)

        def write(address, data):
            uc.mem_write(address, data)
            C.memmove(C.addressof(memory) + address, data, len(data))

        for _, va, _, raw, rawsize, _ in pe.sections:
            if rawsize:
                write(va, pe.data[raw:raw + rawsize])
        regs = [UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
                UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI]
        rnd = random.Random(0x41d670)
        total = saturated = written = 0
        for case in range(args.cases):
            tree, sphere = scene(rnd, case)
            write(TREE, tree)
            write(QUERY, sphere)
            write(SP - 0x10000, bytes(0x10100))
            initial_global=bytearray(rnd.randbytes(0x600))
            struct.pack_into('<I',initial_global,0x538,[0,1,255,256][case%4] if case%23==0 else 0)
            write(0xed7400,bytes(initial_global))
            write(SP, struct.pack("<3I", STOP, TREE, QUERY))
            cpu = CPU(esp=SP, fcw=0x37f)
            for reg, (name, _) in zip(regs, CPU._fields_[:8]):
                uc.reg_write(reg, getattr(cpu, name))
            uc.reg_write(UC_X86_REG_FPCW, 0x37f)
            uc.reg_write(UC_X86_REG_FPSW, 0)
            uc.reg_write(UC_X86_REG_FPTAG, 0xffff)
            uc.emu_start(0x41d670, STOP, count=5000000)
            assert uc.reg_read(UC_X86_REG_EIP) == STOP, (case, "x86 timeout")
            lib.F_0041d670(C.byref(cpu))
            assert cpu.esp == SP + 4, (case, "cdecl return")
            expected_count = struct.unpack("<I", uc.mem_read(0xed7938, 4))[0]
            actual_count = struct.unpack("<I", C.string_at(C.addressof(memory) + 0xed7938, 4))[0]
            assert actual_count == expected_count <= 256, (case, "count", expected_count, actual_count)
            regions=[(0xed7538,1028),(QUERY,20),(0xed7460,168)]
            for label in ['generated','native']:
                if label=='native':
                    C.memmove(C.addressof(memory)+0xed7400,bytes(initial_global),0x600)
                    C.memmove(C.addressof(memory)+QUERY,sphere,20)
                    cpu=CPU(esp=SP,fcw=0x37f)
                    assert lib.test_native(C.byref(cpu))==1,(case,'native refusal')
                for address,size in regions:
                    a=bytes(uc.mem_read(address,size))
                    b=C.string_at(C.addressof(memory)+address,size)
                    if a!=b:
                        off=next(i for i in range(size) if a[i]!=b[i])
                        raise AssertionError((case,label,hex(address+off),
                                              a[max(0,off-4):off+8].hex(),b[max(0,off-4):off+8].hex()))
            # The query is mutable, but the tree/polygons are input-only here.
            assert bytes(uc.mem_read(TREE, len(tree))) == tree, (case, "x86 tree writes")
            assert C.string_at(C.addressof(memory) + TREE, len(tree)) == tree, (case, "C tree writes")
            total += actual_count
            saturated += actual_count == 256
            written += C.string_at(C.addressof(memory) + QUERY, 20) != sphere
        assert total and saturated and written, ("corpus failed to exercise hit/cap/query writes",
                                                  total, saturated, written)
        print(f"{args.cases} sphere/tree queries: {total} ordered candidates, "
              f"{saturated} saturated lists, {written} query writes; "
              "original x86, generated chain and native service agree")
        # Unsupported views must refuse transactionally, so the production bridge
        # can call the original without repairing partially changed guest state.
        faults=['range','cycle','children','payload','vertices','nan','extreme']
        for fault in faults:
            bad=bytearray(tree)
            struct.pack_into('<3fi3fi',bad,0,64.,64.,64.,0,-64.,-64.,-64.,0)
            struct.pack_into('<2I',bad,32,0,0x4000-32)
            root=TREE
            query_data=struct.pack('<5f',2.,2.,.25,-2.,1.)
            if fault=='range': root=SIZE-4
            elif fault=='cycle': struct.pack_into('<I',bad,12,1);struct.pack_into('<I',bad,40,0)
            elif fault=='children': struct.pack_into('<I',bad,12,65537)
            elif fault=='payload': struct.pack_into('<2I',bad,32,1,0x70000000)
            elif fault=='vertices': struct.pack_into('<I',bad,32,1);bad[0x4000+12]=1
            elif fault=='nan': query_data=struct.pack('<I4f',0x7fc00001,2.,.25,-2.,1.)
            elif fault=='extreme': query_data=struct.pack('<5f',1e20,2.,.25,-2.,1.)
            C.memmove(C.addressof(memory)+TREE,bytes(bad),len(bad))
            C.memmove(C.addressof(memory)+QUERY,query_data,20)
            initial_global=bytearray(rnd.randbytes(0x600))
            struct.pack_into('<I',initial_global,0x538,0)
            C.memmove(C.addressof(memory)+0xed7400,bytes(initial_global),0x600)
            C.memmove(C.addressof(memory)+SP,struct.pack('<3I',STOP,root,QUERY),12)
            cpu=CPU(esp=SP,fcw=0x37f)
            assert lib.test_native(C.byref(cpu))==0,(fault,'accepted invalid view')
            assert cpu.esp==SP,(fault,'changed CPU on refusal')
            for address,data in [(TREE,bytes(bad)),(QUERY,query_data),(0xed7400,bytes(initial_global))]:
                assert C.string_at(C.addressof(memory)+address,len(data))==data,(fault,'partial guest write')
        print(f'{len(faults)} invalid/unsupported views refused without guest writes')


if __name__ == "__main__":
    main()
