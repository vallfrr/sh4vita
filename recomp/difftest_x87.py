"""Differential test of the x87 FPU: each x87 instruction form of the game is executed in Unicorn
(stack prepared by 6 FLDs, then FNSTSW + 8 FSTPs to read it back) and via the generated C (x86rec), on random
values. Compares ST(0..depth), C0/C2/C3, TOP, AX (fnstsw ax) and the memory written.
Relative tolerance 1e-9 (double-precision emulation instead of 80 bits).

Usage : python recomp/difftest_x87.py [cas_par_forme=16] [--static]
--static: the form is translated as an [instruction ; ret] function in static stack mode (x86rec.Func.gen), the 6-value
stack being the caller's (input elements); non-eligible forms (fninit, fsave...) are skipped.
"""
import collections, math, os, pathlib, random, struct, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from x86rec import Recompiler, Func, X
import difftest as D
import unicorn
from unicorn import x86_const as U
import capstone

ROOT = D.ROOT
OUT = ROOT / "build" / "difftest"
CODE = 0x00080000
VALS = 0x000a0000  # 6 input doubles
DUMP = 0x000a1000  # fnstsw + 8 output doubles
K = 6


def fval(rng):
    kind = rng.randrange(6)
    if kind == 0:
        return float(rng.randrange(-1000, 1000))
    if kind == 1:
        return rng.uniform(-1, 1)
    if kind == 2:
        return rng.uniform(0.001, 10)
    if kind == 3:
        return rng.uniform(-1e6, 1e6)
    if kind == 4:
        return rng.choice([0.0, 1.0, -1.0, 0.5, 2.0, 3.14159])
    return rng.uniform(-100, 100)


def asm(md_bytes):
    return md_bytes


def build_code(ins):
    """fninit ; fld qword [VALS+8*(K-1-i)] x K ; <instr> ; fnstsw [DUMP] ; fstp qword [DUMP+8+8i] x 8"""
    b = bytearray(b"\xdb\xe3")  # fninit
    for i in range(K):
        a = VALS + 8 * (K - 1 - i)
        b += b"\xdd\x05" + struct.pack("<I", a)  # fld qword [a]
    start = CODE + len(b)
    b += bytes(ins.bytes)
    after = CODE + len(b)
    b += b"\xdd\x3d" + struct.pack("<I", DUMP)  # fnstsw [DUMP]
    for i in range(8):
        b += b"\xdd\x1d" + struct.pack("<I", DUMP + 8 + 8 * i)  # fstp qword [..]
    return bytes(b), start, after


def emit_static(rec, ins, n):
    f = Func.__new__(Func)
    ret = next(rec.md.disasm(bytes([0xc3]), ins.address + ins.size))
    f.rec, f.entry, f.insns, f.addrs = rec, ins.address, [ins, ret], {ins.address, ret.address}
    f.labels, f.switches, f.out, f.flagsrc = set(), {}, [], None
    try:
        code = f.gen()
    except Exception:
        return None
    if not f.static:
        return None
    code = code.replace(f"void F_{ins.address:08x}(CPU *c)", f"static void F_{n}(CPU *c)")
    return code + "\n" + f"static void T{n}(CPU *c, uint32_t *fl) {{ (void)fl; F_{n}(c); }}" + "\n"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    static = "--static" in sys.argv
    per = int(args[0]) if args else 16
    rng = random.Random(99)
    rec = Recompiler()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    shapes = collections.defaultdict(list)
    for entry, addrs in rec.funcs.items():
        for a in addrs:
            ins = rec.decode(a)
            if ins and ins.opcode[0] in range(0xD8, 0xE0):
                k = (ins.opcode[0], ins.modrm >> 3 & 7 if ins.modrm >> 6 != 3 else ins.modrm, ins.operands[0].size if ins.operands else 0)
                if len(shapes[k]) < 2:
                    shapes[k].append(ins)
    tests, cases, refs = [], [], []
    for k, insns in shapes.items():
        for orig in insns:
            # The instruction is relocated: any memory operand -> base ebx in the scratch area.
            code = (emit_static if static else D.emit_test)(rec, orig, len(tests))
            if not code:
                continue
            tid = len(tests)
            tests.append((code, orig))
            for _ in range(per):
                vals = [fval(rng) for _ in range(K)]
                cs = D.make_case(orig, rng)
                if not cs:
                    continue
                regs, flags, mem = cs
                ref = run(orig, regs, mem, vals)
                if ref:
                    cases.append((tid, regs, mem, vals))
                    refs.append(ref)
    src = ['#include "x86rt.h"'] + [c for c, _ in tests]
    src.append("static void (*const TESTS[])(CPU *, uint32_t *) = {" + ",".join(f"T{i}" for i in range(len(tests))) + "};")
    src.append(HARNESS)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "x87.c").write_text("\n".join(src), encoding="utf-8")
    with open(OUT / "x87cases.bin", "wb") as f:
        for tid, regs, mem, vals in cases:
            f.write(struct.pack("<9I", tid, *regs) + struct.pack("<6d", *vals) + struct.pack("<I", len(mem)))
            for a, b in mem:
                f.write(struct.pack("<II", a, len(b)) + b)
    exe = OUT / "x87.exe"
    r = subprocess.run(["J:/tools/mingw64/bin/gcc.exe", "-std=gnu11", "-O1", "-w", f"-I{ROOT / 'port/src/rt'}", f"-I{D.SDL}/include", f"-I{D.SDL}/include/SDL2",
                        str(OUT / "x87.c"), str(ROOT / "port/src/rt/x86rt.c"), "-o", str(exe), f"-L{D.SDL}/lib", "-lSDL2", "-lm"],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[:3000])
        return
    subprocess.run([str(exe), str(OUT / "x87cases.bin"), str(OUT / "x87out.bin")], check=True,
                   env=dict(os.environ, PATH=f"{D.SDL}/bin;J:/tools/mingw64/bin;" + os.environ["PATH"]))
    data = (OUT / "x87out.bin").read_bytes()
    pos, fails, ex = 0, collections.Counter(), {}
    for (tid, regs, mem, vals), (usw, ust, ueax, umem) in zip(cases, refs):
        sw, eax = struct.unpack_from("<II", data, pos); pos += 8
        st = struct.unpack_from("<8d", data, pos); pos += 64
        cm = []
        for a, b in mem:
            cm.append(data[pos:pos + len(b)]); pos += len(b)
        ins = tests[tid][1]
        why = []
        top_u, top_c = (usw >> 11) & 7, (sw >> 11) & 7
        if top_u != top_c:
            why.append(f"TOP {top_c}!={top_u}")
        if (usw ^ sw) & 0x4500:
            why.append(f"C0/C2/C3 {sw & 0x4500:#x}!={usw & 0x4500:#x}")
        depth = 8 - top_u if top_u else 8
        for i in range(min(depth, 8)):
            a, b = st[i], ust[i]
            if math.isnan(a) and math.isnan(b):
                continue
            if not (a == b or (math.isfinite(a) and math.isfinite(b) and abs(a - b) <= 1e-9 * max(1.0, abs(a), abs(b)))):
                why.append(f"ST{i} {a!r}!={b!r}")
                break
        if (eax ^ ueax) & 0xffff and "ax" in ins.op_str:
            why.append(f"ax {eax & 0xffff:#x}!={ueax & 0xffff:#x}")
        for (a, b), x, y in zip(mem, cm, umem):
            if x != y:
                # floats written: tolerance
                ok = len(x) == len(y) and all(
                    abs(p - q) <= 1e-6 * max(1.0, abs(p), abs(q)) or (math.isnan(p) and math.isnan(q))
                    for p, q in zip(struct.unpack(f"<{len(x)//4}f", x), struct.unpack(f"<{len(y)//4}f", y))
                ) if "dword" in ins.op_str and ins.mnemonic in ("fst", "fstp") else False
                if not ok:
                    why.append("memory")
                break
        if why:
            key = f"{ins.mnemonic} {ins.op_str}"
            fails[key] += 1
            ex.setdefault(key, (ins, why, vals))
    print(f"{len(shapes)} x87 shapes, {len(tests)} tests, {len(cases)} cases, {sum(fails.values())} failures")
    for k, n in fails.most_common(40):
        ins, why, vals = ex[k]
        print(f"  {n:4}  {k:40} {ins.bytes.hex():14} {'; '.join(why)[:140]}  inputs {[round(v, 3) for v in vals[:3]]}")


def run(ins, regs, mem, vals):
    code, start, after = build_code(ins)
    uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    pages = {CODE & ~0xfff, VALS & ~0xfff, DUMP & ~0xfff}
    for a, b in mem:
        for p in range(a & ~0xfff, (a + len(b) + 0xfff) & ~0xfff, 0x1000):
            pages.add(p)
    for p in sorted(pages):
        try:
            uc.mem_map(p, 0x1000)
        except unicorn.UcError:
            pass
    # the relocated code keeps the same operands: make sure the instruction fits in a block
    uc.mem_write(CODE, code)
    uc.mem_write(VALS, struct.pack(f"<{K}d", *vals))
    for a, b in mem:
        uc.mem_write(a, b)
    for r, v in zip(D.UREG, regs):
        uc.reg_write(r, v)
    try:
        uc.emu_start(CODE, CODE + len(code))
    except unicorn.UcError:
        return None
    sw = struct.unpack("<H", uc.mem_read(DUMP, 2))[0]
    st = struct.unpack("<8d", uc.mem_read(DUMP + 8, 64))
    return sw, st, uc.reg_read(U.UC_X86_REG_EAX), [bytes(uc.mem_read(a, len(b))) for a, b in mem]


HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
extern uint32_t g_mem_size;
const struct { uint32_t a; gfn f; } x86_functab[1]; const unsigned x86_functab_n = 0;
const struct { uint32_t slot; gfn f; const char *name; } x86_iat[1]; const unsigned x86_iat_n = 0;
const uint32_t x86_entry = 0;
void gheap_init(uint32_t b, uint32_t s) { (void)b; (void)s; }
uint32_t gheap_alloc(uint32_t s) { (void)s; return 0; } uint32_t gheap_calloc(uint32_t s) { (void)s; return 0; }
uint32_t gheap_alloc_aligned(uint32_t s, uint32_t a) { (void)s; (void)a; return 0; }
void rt_platform_fatal(const char *m) { fprintf(stderr, "%s\n", m); }
int main(int argc, char **argv) {
  g_mem_size = 0x14000000u; g_mem = calloc(1, g_mem_size);
  FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
  uint32_t hdr[9]; double v[6]; uint32_t nb;
  while (fread(hdr, 4, 9, in) == 9) {
    fread(v, 8, 6, in); fread(&nb, 4, 1, in);
    uint32_t addr[16], len[16];
    for (uint32_t i = 0; i < nb; i++) { fread(&addr[i], 4, 1, in); fread(&len[i], 4, 1, in); fread(g_mem + addr[i], 1, len[i], in); }
    CPU c = {0}; c.fcw = 0x37f; c.ftop = 0;
    memcpy(&c.eax, hdr + 1, 32);
    for (int i = 0; i < 6; i++) { c.ftop = (c.ftop - 1) & 7; c.st[c.ftop] = v[5 - i]; }
    uint32_t fl[5] = {0};
    TESTS[hdr[0]](&c, fl);
    uint32_t sw = rt_fnstsw(&c);
    fwrite(&sw, 4, 1, out); fwrite(&c.eax, 4, 1, out);
    double st[8];
    for (int i = 0; i < 8; i++) st[i] = c.st[(c.ftop + i) & 7];
    fwrite(st, 8, 8, out);
    for (uint32_t i = 0; i < nb; i++) fwrite(g_mem + addr[i], 1, len[i], out);
  }
  fclose(out);
  return 0;
}
"""

if __name__ == "__main__":
    main()
