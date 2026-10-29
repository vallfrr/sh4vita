"""Extracts and disassembles the D3D8 shaders embedded in the exe (tables passed to CreateVertexShader /
CreatePixelShader, cf. FUN_004050d0 and FUN_00404ac0). Used to size and test the GLSL translator.

Usage : python recomp/d3dshaders.py [--dump]
"""
import collections, pathlib, struct, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from pe import PE

ROOT = pathlib.Path(__file__).resolve().parent.parent
# (start of the table = 1st ppuVar3 - 8, exclusive end of ppuVar3) according to FUN_004050d0
VS_TABLES = [(0x5d5930, 0x5d5970), (0x5eafd8, 0x5eb768), (0x5ebbf8, 0x5ebc38), (0x5f0648, 0x5f0788),
             (0x5f5418, 0x5f5558), (0x5ff0a8, 0x5ff1e8), (0x5fa1e8, 0x5fa328), (0x5ff234, 0x5ff235)]
PS_FUNCS = [0x5d52a0, 0x5d5310, 0x5d5380, 0x5d5438, 0x5d5538, 0x5d5590, 0x5d55e8]

OPS = {0: "nop", 1: "mov", 2: "add", 3: "sub", 4: "mad", 5: "mul", 6: "rcp", 7: "rsq", 8: "dp3", 9: "dp4",
       10: "min", 11: "max", 12: "slt", 13: "sge", 14: "exp", 15: "log", 16: "lit", 17: "dst", 18: "lrp",
       19: "frc", 20: "m4x4", 21: "m4x3", 22: "m3x4", 23: "m3x3", 24: "m3x2", 64: "texcoord", 65: "texkill",
       66: "tex", 67: "texbem", 68: "texbeml", 69: "texreg2ar", 70: "texreg2gb", 71: "texm3x2pad",
       72: "texm3x2tex", 73: "texm3x3pad", 74: "texm3x3tex", 75: "texm3x3diff", 76: "texm3x3spec",
       77: "texm3x3vspec", 78: "expp", 79: "logp", 80: "cnd", 81: "def", 82: "texreg2rgb", 83: "texdp3tex",
       84: "texm3x2depth", 85: "texdp3", 86: "texm3x3", 87: "texdepth", 88: "cmp", 89: "bem",
       0xFFFE: "comment", 0xFFFF: "end"}
REGTYPE = {0: "r", 1: "v", 2: "c", 3: "t", 4: "rasout", 5: "attrout", 6: "texcrdout"}  # vs: 3=a0 ; ps: 3=t


def read_tokens(pe, addr, limit=4096):
    toks = []
    for i in range(limit):
        t = pe.u32(addr + 4 * i)
        toks.append(t)
        if t == 0x0000FFFF:
            return toks
    raise ValueError(f"no shader end at 0x{addr:x}")


def read_decl(pe, addr):
    toks = []
    for i in range(256):
        t = pe.u32(addr + 4 * i)
        toks.append(t)
        if t == 0xFFFFFFFF:
            return toks
    raise ValueError("declaration without end")


def disasm(toks):
    """List of (opcode, name, [operand tokens]); ignores comments."""
    ver, out, i = toks[0], [], 1
    while i < len(toks):
        t = toks[i]
        op = t & 0xFFFF
        if op == 0xFFFF:
            break
        if op == 0xFFFE:
            i += 1 + ((t >> 16) & 0x7FFF)
            continue
        j = i + 1
        while j < len(toks) and toks[j] & 0x80000000:
            j += 1
        out.append((op, OPS.get(op, f"op{op}"), toks[i + 1:j], t))
        i = j
    return ver, out


def main():
    pe = PE(ROOT / "game" / "sh4.exe")
    vs, ps = [], []
    for lo, hi in VS_TABLES:
        p = lo
        while p < hi:
            decl, func = pe.u32(p - 4), pe.u32(p)
            vs.append((p - 8, decl, func))
            p += 16
    for f in PS_FUNCS:
        ps.append(f)
    opc, regs, vers, declt = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()
    funcs = sorted({f for _, _, f in vs if f})
    for f in funcs:
        ver, ins = disasm(read_tokens(pe, f))
        vers[f"vs {ver >> 8 & 0xff}.{ver & 0xff}"] += 1
        for op, name, args, _ in ins:
            opc["vs." + name] += 1
            for a in args:
                rt = (a >> 28) & 7
                regs[f"vs:{'a0' if rt == 3 else REGTYPE.get(rt)}"] += 1
    for _, decl, _ in vs:
        if decl:
            for t in read_decl(pe, decl):
                declt[t >> 29] += 1
    for f in ps:
        ver, ins = disasm(read_tokens(pe, f))
        vers[f"ps {ver >> 8 & 0xff}.{ver & 0xff}"] += 1
        for op, name, args, t in ins:
            mod = (t >> 30) & 1
            opc["ps." + name + ("+" if mod else "")] += 1
    print(f"{len(vs)} VS entries ({len(funcs)} distinct functions, {sum(1 for _, _, f in vs if not f)} without function = FFP+decl), {len(ps)} PS")
    print("versions :", dict(vers))
    print("opcodes :", dict(sorted(opc.items())))
    print("registres VS :", dict(regs))
    print("declaration tokens (type):", dict(declt))
    if "--dump" in sys.argv:
        for f in funcs[:3] + ps[:2]:
            ver, ins = disasm(read_tokens(pe, f))
            print(f"--- 0x{f:x} version {ver:#x}")
            for op, name, args, _ in ins:
                print(f"   {name:8} " + " ".join(f"{a:08x}" for a in args))


if __name__ == "__main__":
    main()
