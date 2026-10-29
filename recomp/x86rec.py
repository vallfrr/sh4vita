"""Static recompiler x86 (PE32) -> C for SILENT HILL 4 PC.

Inputs: function list (functions.json from re/ghidra/ExportFunctions.java: functions and thunks;
per-function instruction addresses, from Ghidra)
and the bytes of game/sh4.exe, decoded with Capstone.
Output: port/gen/ (never versioned: derived from the game binary).

Model: one C function per guest function, `void F_xxxxxxxx(CPU *c)`. Registers
are local variables, reloaded/saved around calls; flags are locals (the
compiler eliminates dead computations); CALL pushes the real return address,
RET pops and returns in C. x87 in CPU (stack of 8 doubles).
"""
import json, os, pathlib, re, struct, sys, collections

import capstone
from capstone import x86 as X

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from pe import PE

ROOT = pathlib.Path(__file__).resolve().parent.parent
GEN = ROOT / "port" / "gen"

REG32 = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
SUBREG = {}
for r in REG32:
    SUBREG[r] = (r, 0, 4)
    SUBREG[r[1:]] = (r, 0, 2)
for r in ["eax", "ecx", "edx", "ebx"]:
    SUBREG[r[1] + "l"] = (r, 0, 1)
    SUBREG[r[1] + "h"] = (r, 8, 1)

MASK = {1: "0xffu", 2: "0xffffu", 4: "0xffffffffu"}
SX = {1: "(int8_t)", 2: "(int16_t)", 4: "(int32_t)"}
SPILL = "c->eax=eax;c->ecx=ecx;c->edx=edx;c->ebx=ebx;c->esp=esp;c->ebp=ebp;c->esi=esi;c->edi=edi;"
RELOAD = "eax=c->eax;ecx=c->ecx;edx=c->edx;ebx=c->ebx;esp=c->esp;ebp=c->ebp;esi=c->esi;edi=c->edi;"

CC = {"e": "e", "z": "e", "ne": "ne", "nz": "ne", "b": "b", "c": "b", "nae": "b", "ae": "ae", "nb": "ae",
      "nc": "ae", "be": "be", "na": "be", "a": "a", "nbe": "a", "l": "l", "nge": "l", "ge": "ge", "nl": "ge",
      "le": "le", "ng": "le", "g": "g", "nle": "g", "s": "s", "ns": "ns", "o": "o", "no": "no", "p": "p",
      "pe": "p", "np": "np", "po": "np"}
FLAGCOND = {"e": "zf", "ne": "!zf", "b": "cf", "ae": "!cf", "be": "(cf|zf)", "a": "!(cf|zf)",
            "l": "(sf!=of)", "ge": "(sf==of)", "le": "(zf||sf!=of)", "g": "(!zf&&sf==of)",
            "s": "sf", "ns": "!sf", "o": "of", "no": "!of", "p": "pf", "np": "!pf"}
NOFLAGS = {"mov", "movzx", "movsx", "lea", "push", "pop", "xchg", "not", "cdq", "nop", "wait", "leave",
           "cld", "std", "pushal", "popal", "pushfd", "jmp", "cpuid"}
FCONST = {0xE8: "1.0", 0xE9: "3.321928094887362348", 0xEA: "1.442695040888963407", 0xEB: "3.141592653589793239",
          0xEC: "0.301029995663981195", 0xED: "0.693147180559945309", 0xEE: "0.0"}


def h(v):
    return f"0x{v & 0xffffffff:x}u"


class Recompiler:
    def __init__(self):
        self.pe = PE(ROOT / "game" / "sh4.exe")
        self.iat = self.pe.imports()
        self.funcs, self.thunks, self.names = {}, {}, {}
        fj = ROOT / "re" / "out" / "functions.json"
        if fj.exists():   # produced by re/ghidra/ExportFunctions.java (headless Ghidra, no other tool needed)
            for p in json.loads(fj.read_text(encoding="utf-8-sig")):
                a = int(p["a"], 16)
                self.names[a] = p["name"]
                if p["thunk"]:
                    self.thunks[a] = p["target"]
                else:
                    self.funcs[a] = sorted(p["insns"])
        else:             # legacy: REA export (procs.json + one .asm listing per function)
            procs = json.loads((ROOT / "re" / "out" / "procs.json").read_text(encoding="utf-8-sig"))["normalized_result"]
            for p in procs:
                if p["procedure"]["external"]:
                    continue
                a = int(p["address"], 16)
                self.names[a] = p["value"]
                if p["procedure"]["thunk"]:
                    self.thunks[a] = p["procedure"]["thunk_target"]
                else:
                    self.funcs[a] = None
            for a in self.funcs:
                lines = (ROOT / "re" / "out" / "asm" / f"0x{a:x}.asm").read_text(encoding="utf-8").splitlines()
                self.funcs[a] = sorted(int(l.split(":")[0], 16) for l in lines if l.startswith("0x"))
        self.entries = set(self.funcs) | set(self.thunks)
        text = next(s for s in self.pe.sections if s[0] == ".text")
        self.text = (text[1], text[1] + text[2])
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self.report = collections.Counter()
        self.report_sites = collections.defaultdict(list)
        self.x87net = {}   # entry -> net effect (pushes - pops) of the function on the x87 stack, if known and non-zero
        self.discover_pointer_targets()

    BAD = {"in", "out", "insb", "insd", "outsb", "outsd", "cli", "sti", "hlt", "int", "into", "les", "lds", "retf",
           "iretd", "salc", "arpl", "bound", "lcall", "ljmp", "aaa", "aas", "daa", "das", "aam", "aad", "lock",
           "enter", "lahf", "loop", "loopne", "loope", "xlatb"}

    def descend(self, entry, seeds, strict=False):
        """Recursive descent from `seeds` (Ghidra body or entry alone): fallthrough, direct
        branches and jump tables; stops at other function entries (tail call).
        Returns (valid?, {address: instruction}, {jmp address: targets}). strict: rejects
        as soon as an instruction is undecodable or impossible in this game (false function)."""
        lo, hi = self.text
        decoded, switches, work = {}, {}, sorted(seeds, reverse=True)
        while work:
            a = work.pop()
            if a in decoded:
                continue
            ins = self.decode(a)
            bad = ins is None or ins.mnemonic.split()[0] in self.BAD or \
                (ins.mnemonic == "push" and ins.op_str in ("ss", "ds", "es", "cs"))
            if bad and strict:
                return False, decoded, switches
            if ins is None:
                continue
            decoded[a] = ins
            base = ins.mnemonic.split()[-1]
            succ = []
            if base not in TERMINAL:
                succ.append(a + ins.size)
            if ins.mnemonic.startswith("j") and ins.operands and ins.operands[0].type == X.X86_OP_IMM:
                succ.append(ins.operands[0].imm & 0xffffffff)
            tbl = self.jump_table(ins, decoded)
            if tbl:
                switches[a] = tbl
                succ.extend(tbl)
            for s in succ:
                if s not in decoded and (s == entry or s not in self.entries) and lo <= s < hi:
                    work.append(s)
        return True, decoded, switches

    def jump_table(self, ins, decoded):
        """Targets of a `jmp [idx*4 + table]`: read from the exe as long as they stay in .text near
        the jump; bounded by the preceding `cmp idx, N ; ja/jae` when found."""
        if ins.mnemonic != "jmp" or not ins.operands or ins.operands[0].type != X.X86_OP_MEM:
            return None
        m = ins.operands[0].mem
        if m.base or not m.index or m.scale != 4 or not m.disp:
            return None
        lo, hi = self.text
        limit = 1024
        idx = ins.reg_name(m.index)
        for back in range(2, 16):
            p = decoded.get(ins.address - back)
            if p and p.mnemonic in ("ja", "jae"):
                for back2 in range(2, 12):
                    q = decoded.get(p.address - back2)
                    if q and q.mnemonic == "cmp" and len(q.operands) == 2 and q.operands[1].type == X.X86_OP_IMM \
                            and q.operands[0].type == X.X86_OP_REG and q.reg_name(q.operands[0].reg) == idx:
                        limit = (q.operands[1].imm & 0xffffffff) + (1 if p.mnemonic == "ja" else 0)
                        break
                break
        tgts = []
        for k in range(min(limit, 1024)):
            t = self.pe.u32((m.disp + 4 * k) & 0xffffffff)
            if t is None or not (lo <= t < hi) or abs(t - ins.address) > 0x10000:
                break
            tgts.append(t)
        return sorted(set(tgts)) or None

    def scan_code(self, entry, strict=True):
        """(valid?, constants pointing into .text, direct call targets) of the code reached."""
        lo, hi = self.text
        ok, decoded, _ = self.descend(entry, self.funcs.get(entry) or [entry], strict)
        imms, calls = set(), set()
        for ins in decoded.values():
            base = ins.mnemonic.split()[-1]
            for op in ins.operands:
                if op.type == X.X86_OP_IMM and lo <= (op.imm & 0xffffffff) < hi:
                    if base == "call":
                        calls.add(op.imm & 0xffffffff)
                    elif not base.startswith("j"):
                        imms.add(op.imm & 0xffffffff)
        return ok, imms, calls

    def discover_pointer_targets(self):
        """Functions reached only through a pointer (CRT init tables, callbacks, vtables,
        atexit stubs): Ghidra does not always list them. Candidates: 32-bit values of the data sections,
        constants and call targets of the code, pointing into .text outside the Ghidra
        bodies. Iterated until stable; a candidate whose decoding hits an impossible
        instruction (in/out/cli/les/retf...) is rejected as data."""
        covered = set()
        for addrs in self.funcs.values():
            covered.update(addrs)
        lo, hi = self.text
        cands = set()
        for name, va, vsz, raw, rsz, _ in self.pe.sections:
            if name in (".rdata", ".data", ".data1"):
                blob = self.pe.data[raw:raw + rsz]
                for off in range(0, len(blob) - 3, 4):
                    v = struct.unpack_from("<I", blob, off)[0]
                    if lo <= v < hi:
                        cands.add(v)
        for f in list(self.funcs):
            _, imms, calls = self.scan_code(f, strict=False)
            cands |= imms | calls
        found, rejected = [], set()
        while True:
            new = sorted(v for v in cands if v not in self.entries and v not in covered and v not in rejected
                         and self.pe.read(v, 1) not in (b"\xcc", b"\x00"))
            if not new:
                break
            for v in new:
                ok, imms, calls = self.scan_code(v)
                if not ok:
                    rejected.add(v)
                    continue
                self.funcs[v] = [v]
                self.names[v] = f"PTR_FUN_{v:08x}"
                self.entries.add(v)
                found.append(v)
                cands |= imms | calls
        self.report["functions discovered by pointer"] = len(found)
        self.report["candidates rejected (data)"] = len(rejected)
        self.report_sites["functions discovered by pointer"] = [f"0x{v:x}" for v in found[:20]]

    def imp_name(self, slot):
        dll, fn = self.iat[slot].split("!")
        fn = "ord" + fn[1:] if fn.startswith("#") else fn
        return f"imp_{dll.split('.')[0]}_{re.sub(r'[^A-Za-z0-9_]', '_', fn)}"

    def decode(self, a):
        b = self.pe.read(a, 16)
        return next(self.md.disasm(b, a), None) if b else None

    def note(self, kind, where):
        self.report[kind] += 1
        if len(self.report_sites[kind]) < 20:
            self.report_sites[kind].append(f"0x{where:x}")


TERMINAL = {"ret", "jmp", "int3", "hlt", "ud2"}

# Functions replaced by hand-written native C (port/src/rt/natives.c): unconditional
# SIMD, or later hot functions optimised for the Vita.
# The game CRT's __ftol (edx:eax = truncation of ST(0), pops): inlined in static x87 mode, without flushing the stack.
FTOL = 0x575e30

def put(path, text):
    # does not rewrite an identical file: keeps the date, make only recompiles what changes
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return
    path.write_text(text, encoding="utf-8")


# Wrappers: the recompiled body becomes F_xxx_orig, F_xxx calls the native function which may call it back (console settings, measurements).
WRAPS = {
    0x408960: "nat_savedispatch", # propagate failed read/modify/write saves to the console options layer
    0x528a50: "nat_titlechoice", # opt-in deterministic title destination for menu qualification
    0x408030: "nat_savewrite", # staged save transport; original signature/MD5 retained, diagnostic fallback available
    0x409530: "nat_sound_request", # opt-in resident sound archive usage audit
    0x409a30: "nat_sound_request3d",
    0x551f80: "nat_arena_init",  # opt-in smaller linear arena; original path is the default
    0x552000: "nat_arena_alloc", # opt-in high-water trace of the game's 64 MiB linear arena
    0x4203a0: "nat_collision_segments", # opt-in transactional segment/polygon filtering; original callbacks retained
    0x41f570: "nat_collision_polygons", # opt-in direct sphere/polygon lists, shared native geometry service
    0x40a880: "nat_audio_adpcm", # opt-in bit-exact 13-byte audio block decoding
    0x415ef0: "nat_model_buffer_lock", # audited read-only UV scope; other buffer writes invalidate native metadata
    0x415ee0: "nat_model_buffer_unlock",
    0x553c70: "nat_cinematic_frame", # opt-in laboratory common-event cinematic scheduler
    0x41d670: "nat_collision_sphere",  # opt-in native sphere candidate traversal/filtering
    0x561a20: "nat_collision_set",  # clear the BSP root when a room has no collision resource
    0x573a20: "nat_raytri",         # scalar ray/triangle math, same float spills and comparison order
    0x433f00: "nat_lightglow",   # light-halo grid (17x12 cells x lights, ~4 ms/frame in the hospital)
    0x50c2a0: "nat_optdlg",      # opens a Yes/No dialog (id): the options one ("change the options?") opens on Yes
    0x526be0: "nat_optframe",    # options menu state machine (once per frame while open): flags "options open" (dinput: L/R = switch page)
    0x523e40: "nat_optbegin",    # snapshot the port's camera setting alongside the game's options
    0x526810: "nat_optsave",     # persist sensitivity only after accepting the options
    0x524980: "nat_camslider",   # removed vibration row becomes camera sensitivity
    0x413c60: "nat_camvalue",
    0x525450: "nat_camhelp",
    0x518830: "nat_vita_bindings",  # integrated original binding editor; Vita layout and capture scope
    0x5176a0: "nat_vita_bindings_draw", # fixed Vita diagram inside the original options lifecycle
    0x517e60: "nat_vita_bindings_input", # cancel only; no binding editor input
    0x517c30: "nat_vita_bindings_title", # replace PC title and page count
    0x563d20: "nat_controls_font_quad", # scoped font geometry scaling; untouched UVs and other menus
    0x559a10: "nat_controls_font_batch", # retain the game's font atlas for a native-resolution menu pass
    0x526490: "nat_controls_options_page", # draw L/R only on frames with original page arrows
    0x50c440: "nat_opttext",     # localized strings encoded in the game's font
    0x5254f0: "nat_optrow",      # reuse the game's volume gauge for the new row
    0x523aa0: "nat_optchanged",  # remap per-row highlights after compacting the pages
    0x523d20: "nat_optanychanged", # compare the complete options + private sensitivity
    0x554350: "nat_curaxis",      # analog axis of the on-screen cursor: flags "cursor in use" (dinput puts the left stick on X)
    0x406f50: "nat_axischange",   # release old direction before a direct analog sign reversal
    0x52d710: "nat_camera_probe", # diagnostic wrapper, original behavior by default
    0x52e0b0: "nat_camera_input", # scale the game's first-person look input after digital conversion
    0x54bae0: "nat_window_camera", # mark the original exterior-window camera update
    0x54cc10: "nat_window_close",  # restore normal stick mapping as soon as the view closes
}

OVERRIDES = {
    0x404a00: "nat_memcpy_mmx",   # the game's MMX/movntq memcpy (cdecl dst, src, n)
    0x5771a6: "nat_rand",          # CRT _rand: reads the TLS slot directly (avoids __getptd = 3 host calls)
}


def optimize_audio_packets(code):
    """Batch a prefix, leaving the final original iteration to restore its ABI."""
    marker = "L_0040ab20:;\n"
    assert code.count(marker) == 1
    return code.replace(marker, """  extern int rt_audio_packets_enabled(CPU *);
  const int audio_packets_enabled=rt_audio_packets_enabled(c);
""" + marker + """  if(audio_packets_enabled) {
    extern uint32_t rt_adpcm_packet_prefix(CPU *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
    uint32_t skipped=rt_adpcm_packet_prefix(c,esi,ebp,esp,ebx,edi);
    if(skipped) { ebx+=skipped; edi+=(skipped/26u)*(R32(esi) ? 64u : 32u); }
  }
""", 1)


def optimize_model_bbox(code):
    """Replace the hot x87 extrema scan in the game's model renderer.

    Each vertex has a 64-byte stride and the two compared fields are floats at
    +0x18/+0x1c.  Keep the original 32-bit words when updating an extremum so
    NaNs and signed zero retain the game's exact stored representation.
    """
    begin, end = "L_004290c0:;\n", "L_0042911e:;\n"
    assert code.count(begin) == code.count(end) == 1
    start = code.index(begin)
    stop = code.index(end, start)
    native = """L_004290c0:;
  {
    uint32_t src = ecx;
    uint32_t count = edx;
    for (uint32_t i = 0; i < count; ++i, src += 0x40u) {
      uint32_t xword = R32(src + 0x18u), yword = R32(src + 0x1cu);
      float x = rt_f32(xword), y = rt_f32(yword);
      if (x > rt_f32(R32(esp + 0x34u))) W32(esp + 0x34u, xword);
      if (y > rt_f32(R32(esp + 0x1cu))) W32(esp + 0x1cu, yword);
      if (x < rt_f32(R32(esp + 0x30u))) W32(esp + 0x30u, xword);
      if (y < rt_f32(R32(esp + 0x28u))) W32(esp + 0x28u, yword);
    }
    ecx = src;
    edi = 0;
    W32(esp + 0x20u, ecx);
  }
"""
    return code[:start] + native + code[stop:]


def optimize_model_uv(code):
    """Replace the second 64-byte vertex pass with equivalent native C."""
    begin, end = "L_00429158:;\n", "L_0042927d:;\n"
    assert code.count(begin) == code.count(end) == 1
    start = code.index(begin)
    stop = code.index(end, start)
    original = code[start + len(begin):stop]
    native = (ROOT / "recomp" / "model_uv_native.inc").read_text(encoding="utf-8")
    assert native.startswith(begin) and native.endswith("\n")
    # Retain an in-game original-instruction oracle. Its first load happens
    # before cleaning memcpy's arguments; this caught a native stack-slot error.
    gate = (begin + "  { extern int rt_original_model_uv(void);\n"
            "    if (rt_original_model_uv()) goto L_original_model_uv; }\n")
    replacement = (gate + native[len(begin):] + "  goto L_0042927d;\n"
                   "L_original_model_uv:;\n" + original)
    return code[:start] + replacement + code[stop:]


def optimize_model_palette(code):
    """Opt-in native matrix-palette upload; retain the original loop fallback."""
    begin = "L_00428e64:;\n"
    end = "  // 0x428e9d: mov eax, dword ptr [esi + 0x1c]\n"
    assert code.count(begin) == code.count(end) == 1
    hook = ("  { extern int d3d_model_palette(CPU *);\n"
            f"    {SPILL}\n"
            "    if (d3d_model_palette(c)) {\n"
            f"      {RELOAD}\n"
            "      cf=sf=of=0; zf=pf=1; fa=fb=94; fr=0;\n"
            "      goto L_native_palette_done;\n"
            "    }\n  }\n")
    return code.replace(begin, hook + begin, 1).replace(end, "L_native_palette_done:;\n" + end, 1)


def optimize_model_vertices(code):
    """Isolated native passes; retained inline implementations remain fallback."""
    for begin, end, func, flags in [
        ("L_004290c0:;\n", "L_0042911e:;\n", "d3d_model_bounds", ""),
        ("L_00429158:;\n", "L_0042927d:;\n", "d3d_model_uv",
         "cf=of=sf=0; zf=pf=1; fa=fb=eax; fr=0;"),
    ]:
        assert code.count(begin) == code.count(end) == 1
        target = end.split(":")[0]
        hook = (f"  {{ extern int {func}(CPU *);\n    {SPILL}\n"
                f"    if ({func}(c)) {{\n      {RELOAD}\n"
                f"      {flags}\n      goto {target};\n    }}\n  }}\n")
        code = code.replace(begin, begin + hook, 1)
    return code


X87_STATIC = os.environ.get("X87_STATIC", "1") != "0"
X87_BAD = ("rt_unimpl", "c->ftop", "rt_fsave", "rt_frstor")
X87_TOK = re.compile(r"ST\((\d+)\)|FPUSH\(|FPOP\(\)|rt_fnstsw\(c\)")
X87_WR = re.compile(r"\s*=(?!=)")


class X87Fail(Exception):
    pass


def xlab(e):
    """Name of static stack element e (0 = top at entry, <0 = pushed since)."""
    return f"x{e}" if e >= 0 else f"y{-e}"


class Func:
    quiet = False
    net = None
    call_net = 0     # net effect on the x87 stack of the direct call being translated
    static = False   # static x87 stack (see plan_x87); false = dynamic ST()/FPUSH()/FPOP() through c->ftop
    cur_d = 0

    def __init__(self, rec, entry):
        self.rec, self.entry = rec, entry
        # Ghidra body completed by recursive descent: Ghidra cuts the body after
        # a call assumed not to return, and `9B xx` (wait+op) is a single instruction for
        # it but two for Capstone. Code shared with another function is duplicated;
        # we stop at function entries (tail call).
        ghidra = set(rec.funcs[entry])
        _, decoded, self.switches = rec.descend(entry, ghidra)
        extra = set(decoded) - ghidra
        if extra:
            rec.note("instructions added by descent", min(extra))
            rec.report["  (total instructions added)"] += len(extra) - 1
        self.insns = [decoded[a] for a in sorted(decoded)]
        self.addrs = set(decoded)
        self.labels = set()
        self.out = []
        self.flagsrc = None

    # ---------- operands ----------
    def reg(self, ins, regid):
        return ins.reg_name(regid)

    def rd_reg(self, name):
        r, sh, sz = SUBREG[name]
        if sz == 4:
            return r
        if sz == 2:
            return f"({r}&0xffffu)"
        return f"({r}&0xffu)" if sh == 0 else f"(({r}>>8)&0xffu)"

    def wr_reg(self, name, v):
        r, sh, sz = SUBREG[name]
        if sz == 4:
            return f"{r}=(uint32_t)({v});"
        if sz == 2:
            return f"{r}=({r}&0xffff0000u)|((uint32_t)({v})&0xffffu);"
        if sh == 0:
            return f"{r}=({r}&0xffffff00u)|((uint32_t)({v})&0xffu);"
        return f"{r}=({r}&0xffff00ffu)|(((uint32_t)({v})&0xffu)<<8);"

    def addr(self, ins, op, seg=True):
        m = op.mem
        parts = []
        if m.base:
            parts.append(self.rd_reg(self.reg(ins, m.base)))
        if m.index:
            idx = self.rd_reg(self.reg(ins, m.index))
            parts.append(idx if m.scale == 1 else f"{idx}*{m.scale}u")
        if m.disp or not parts:
            parts.append(h(m.disp))
        e = "(uint32_t)(" + "+".join(parts) + ")"
        if seg and m.segment and self.reg(ins, m.segment) == "fs":
            e = f"(c->fs_base+{e})"
        return e

    def rd(self, ins, op, size=None):
        size = size or op.size
        if op.type == X.X86_OP_REG:
            return self.rd_reg(self.reg(ins, op.reg))
        if op.type == X.X86_OP_IMM:
            return h(op.imm & {1: 0xff, 2: 0xffff, 4: 0xffffffff}[size])
        a = self.addr(ins, op)
        return {1: f"R8({a})", 2: f"R16({a})", 4: f"R32({a})"}[size]

    def wr(self, ins, op, v, size=None):
        size = size or op.size
        if op.type == X.X86_OP_REG:
            return self.wr_reg(self.reg(ins, op.reg), v)
        a = self.addr(ins, op)
        return {1: f"W8({a},{v});", 2: f"W16({a},{v});", 4: f"W32({a},{v});"}[size]

    # ---------- emission ----------
    def emit(self, s):
        if self.static:
            # Static x87 stack: flush of live elements before any call/return, reload afterwards (tokens
            # resolved at the end of the function, when the range of elements used is known).
            if SPILL in s:
                s = s.replace(SPILL, SPILL + f"@F({self.cur_d})@")
            if RELOAD in s:
                s = s.replace(RELOAD, RELOAD + f"@R({self.cur_d + self.call_net})@")
        self.out.append("  " + s)

    def is_simd(self, ins):
        for op in ins.operands:
            if op.type == X.X86_OP_REG and self.reg(ins, op.reg)[:2] in ("mm", "xm"):
                return True
        return ins.mnemonic in {"femms", "emms", "pfmul", "pfadd", "pfsub", "pfsubr", "pfacc", "pfrcp", "pfrcpit1",
                                "pfrcpit2", "pfrsqrt", "pfrsqit1", "pfcmpge", "pfcmpgt", "pfcmpeq", "pfmax",
                                "pfmin", "pfnacc", "pfpnacc", "pi2fd", "pf2id", "pswapd", "movntq", "stmxcsr",
                                "ldmxcsr", "prefetch", "prefetchnta", "prefetcht0"}

    def cond(self, cc):
        k = self.flagsrc
        if k:
            kind, size = k
            bits = size * 8
            msb = f"((fr>>{bits - 1})&1u)"
            A, B = "fa", "fb"
            SA, SB = f"{SX[size]}fa", f"{SX[size]}fb"
            if kind == "cmp":
                d = {"e": f"{A}=={B}", "ne": f"{A}!={B}", "b": f"{A}<{B}", "ae": f"{A}>={B}", "be": f"{A}<={B}",
                     "a": f"{A}>{B}", "l": f"{SA}<{SB}", "ge": f"{SA}>={SB}", "le": f"{SA}<={SB}",
                     "g": f"{SA}>{SB}", "s": msb, "ns": f"!{msb}"}
            else:
                d = {"e": "fr==0", "ne": "fr!=0", "s": msb, "ns": f"!{msb}", "l": msb, "ge": f"!{msb}",
                     "le": f"(fr==0||{msb})", "g": f"(fr!=0&&!{msb})", "b": "0", "ae": "1", "be": "fr==0",
                     "a": "fr!=0"}
            if cc in d:
                return f"({d[cc]})"
        return FLAGCOND[cc]

    def flags_szp(self, r, size):
        return f"zf=({r}==0);sf=({r}>>{size * 8 - 1})&1u;pf=PF({r});"

    # ---------- pass 1: labels ----------
    def scan(self):
        ins_list = self.insns
        for idx, ins in enumerate(ins_list):
            mn = ins.mnemonic
            if mn.startswith("j") and ins.operands and ins.operands[0].type == X.X86_OP_IMM:
                t = ins.operands[0].imm & 0xffffffff
                if t in self.addrs:
                    self.labels.add(t)
            if ins.address in self.switches:
                self.switches[ins.address] = [t for t in self.switches[ins.address] if t in self.addrs]
                self.labels.update(self.switches[ins.address])
            nxt = ins.address + ins.size
            if idx + 1 < len(ins_list) and ins_list[idx + 1].address != nxt and nxt in self.addrs:
                self.labels.add(nxt)

    # ---------- static x87 stack ----------
    # Each function that uses the x87 gets a static "labelling" of its stack: element e is x_e (e >= 0:
    # elements already present at entry, 0 = top) or y_(-e) (pushed since entry); ST(i) at depth d
    # (pushes - pops) is element i - d, a local double variable (hence a VFP register). The physical base `ft0`
    # is a variable: element e lives in c->st[(ft0+e)&7]. Before every call/return/external jump the modified elements
    # are written to c->st and c->ftop is updated; after every call ftop is re-read (the callee may have pushed
    # or popped: the labelling stays valid, only the base changes) and the elements are reloaded. If the depth is
    # not the same on all paths (or if the function uses fsave/fninit...), fall back to dynamic mode.
    def x87_text(self, ins):
        """C lines of the dynamic mode for an x87 instruction (no side effect on the report)."""
        n0, note = len(self.out), self.rec.note
        self.rec.note = lambda *a, **k: None
        try:
            self.x87(ins)
        finally:
            self.rec.note = note
        lines = self.out[n0:]
        del self.out[n0:]
        return lines

    def is_x87(self, ins):
        return ins.opcode[0] in range(0xD8, 0xE0) and not self.is_simd(ins)

    def cfg_succ(self, ins):
        base = ins.mnemonic.split()[-1]
        s = []
        if base not in TERMINAL:
            s.append(ins.address + ins.size)
        if ins.mnemonic.startswith("j") and ins.operands and ins.operands[0].type == X.X86_OP_IMM:
            s.append(ins.operands[0].imm & 0xffffffff)
        s.extend(self.switches.get(ins.address, ()))
        return [t for t in s if t in self.addrs]

    def why(self, r):
        if not self.quiet:
            self.rec.report["x87 dyn: " + r] += 1
        return False

    def plan_x87(self, quiet=False):
        """Computes the stack depth at the entry of each instruction (self.depth) and the function's net effect
        (self.net, None if undetermined); returns True if the function uses the x87 with a consistent depth
        (static mode), False = dynamic mode. Direct calls have the net effect measured in the previous round
        (rec.x87net, 0 by default): a bad estimate never breaks execution (the base ft0 is re-read
        from c->ftop after the call), it only makes consistency fail at joins."""
        self.quiet, self.net, self.depth = quiet, None, {}
        dx, has = {}, False
        for ins in self.insns:
            if ins.mnemonic.split()[-1] == "call" and ins.operands and ins.operands[0].type == X.X86_OP_IMM:
                t = ins.operands[0].imm & 0xffffffff
                if self.rec.names.get(t) == "__setjmp3":
                    return self.why("setjmp")
                if self.rec.x87net.get(t):
                    dx[ins.address] = self.rec.x87net[t]
                continue
            if not self.is_x87(ins):
                continue
            try:
                t = "\n".join(self.x87_text(ins))
            except (KeyError, StopIteration, IndexError):
                return self.why("x87_text exc")
            if any(k in t for k in X87_BAD):
                return self.why("X87_BAD")
            for m in re.finditer(r"FPUSH\(", t):
                if "ST(" in t[m.end():t.find(";", m.end())]:
                    return self.why("FPUSH(ST)")
            has = True
            dx[ins.address] = t.count("FPUSH(") - t.count("FPOP()")
        if not dx:
            return self.why("no x87")
        byaddr = {i.address: i for i in self.insns}
        d_in, work, exits = {self.entry: 0}, [self.entry], set()
        while work:
            a = work.pop()
            ins = byaddr.get(a)
            if ins is None:
                continue
            d = d_in[a] + dx.get(a, 0)
            if abs(d) > 8:
                return self.why("|d|>7")
            base = ins.mnemonic.split()[-1]
            if base == "ret":
                exits.add(d)
            elif base == "jmp" or ins.mnemonic.startswith("j"):
                if ins.operands and ins.operands[0].type == X.X86_OP_IMM:
                    t = ins.operands[0].imm & 0xffffffff
                    if t not in self.addrs:
                        exits.add(d + self.rec.x87net.get(t, 0))
            for t in self.cfg_succ(ins):
                if t not in d_in:
                    d_in[t] = d
                    work.append(t)
                elif d_in[t] != d:
                    self.rec.note("x87 dyn: merge@", t)
                    self.rec.note("x87 dyn: mergefn@", self.entry)
                    return self.why("merge mismatch")
        if len(exits) == 1:
            self.net = next(iter(exits))
        self.depth = d_in
        return has

    def sx(self, line):
        """Rewrites ST()/FPUSH()/FPOP() of an x87 line into static elements; updates the current depth."""
        st = [self.cur_d]

        def rep(m):
            t = m.group(0)
            if t.startswith("ST("):
                e = int(m.group(1)) - st[0]
                if X87_WR.match(line, m.end()):
                    self.xw.add(e)
                return self.xuse(e)
            if t == "FPUSH(":
                st[0] += 1
                self.xw.add(-st[0])
                return f"{self.xuse(-st[0])}=("
            if t == "FPOP()":
                st[0] -= 1
                return "(void)0"
            return f"rt_fnstsw_t(c,ft0-({st[0]}))"
        out = X87_TOK.sub(rep, line)
        self.cur_d = st[0]
        return out

    def xuse(self, e):
        if not -8 <= e < 8:
            raise X87Fail()
        self.xa.add(e)
        return xlab(e)

    def x87_flush(self, d):
        s = [f"c->ftop=(ft0-({d}))&7u;"]
        for e in range(-d, self.xm):
            if e in self.xw:
                s.append(f"c->st[(ft0+({e}))&7u]={xlab(e)};")
        return "".join(s)

    def x87_reload(self, d):
        s = [f"ft0=(c->ftop+({d}))&7u;"]
        for e in range(-d, self.xm):
            s.append(f"{self.xuse(e)}=c->st[(ft0+({e}))&7u];")
        return "".join(s)

    # ---------- passe 2 ----------
    def gen(self):
        self.scan()
        self.static = False
        if X87_STATIC and self.plan_x87():
            try:
                return self.gen_body(True)
            except X87Fail:
                pass
            self.rec.report["static x87 abandoned (label out of range)"] += 1
        elif X87_STATIC and any(self.is_x87(i) for i in self.insns):
            self.rec.report["x87 dynamique (profondeur variable ou fsave/fninit...)"] += 1
        return self.gen_body(False)

    def gen_body(self, static):
        self.static = static
        self.cur_d = 0
        self.flagsrc = None
        self.xa, self.xw = set(), set()
        name = f"F_{self.entry:08x}"
        self.out = [f"void {name}(CPU *c) {{",
                    "  uint8_t *const mb = g_mem;",
                    "  uint32_t eax=c->eax,ecx=c->ecx,edx=c->edx,ebx=c->ebx,esp=c->esp,ebp=c->ebp,esi=c->esi,edi=c->edi;",
                    "  uint32_t cf=0,zf=0,sf=0,of=0,pf=0,fa=0,fb=0,fr=0;",
                    "  (void)cf;(void)zf;(void)sf;(void)of;(void)pf;(void)fa;(void)fb;(void)fr;(void)mb;"]
        if self.insns and self.insns[0].address != self.entry:
            # code placed before the entry (shared block or Ghidra chunk): start at the entry
            self.labels.add(self.entry)
            self.out.append(f"  goto L_{self.entry:08x};")
        prev_falls, prev_next = False, None
        for ins in self.insns:
            if prev_falls and prev_next != ins.address:
                self.fallthrough(prev_next)
            if ins.address in self.labels:
                self.out.append(f"L_{ins.address:08x}:;")
                self.flagsrc = None
            self.out.append(f"  // {ins.address:#x}: {ins.mnemonic} {ins.op_str}")
            if self.static:
                self.cur_d = self.depth.get(ins.address, 0)
            try:
                prev_falls = self.insn(ins)
            except (KeyError, StopIteration, IndexError) as e:
                # unlikely decoding (often a false function discovered through a pointer)
                self.rec.note(f"intraduisible:{ins.mnemonic}", ins.address)
                self.emit(f"{SPILL} rt_unimpl(c, {h(ins.address)}, \"{ins.mnemonic} (intraduisible)\");")
                prev_falls = False
            prev_next = ins.address + ins.size
        if prev_falls:
            self.fallthrough(prev_next)
        self.out.append("}")
        if static:
            self.xm = max([e + 1 for e in self.xa if e >= 0], default=0)
            body = "\n".join(self.out)
            body = re.sub(r"@F\((-?\d+)\)@", lambda m: self.x87_flush(int(m.group(1))), body)
            body = re.sub(r"@R\((-?\d+)\)@", lambda m: self.x87_reload(int(m.group(1))), body)
            labs = sorted(self.xa | set(range(self.xm)))
            decl = ",".join(f"{xlab(e)}=0.0" for e in labs)
            load = "".join(f"{xlab(e)}=c->st[(ft0+{e})&7u];" for e in range(self.xm))
            head, rest = body.split("\n", 1)
            body = f"{head}\n  uint32_t ft0=c->ftop; double {decl}; {load}\n{rest}"
            self.rec.report["x87 statique"] += 1
            return body
        return "\n".join(self.out)

    def fallthrough(self, a):
        if a in self.addrs:
            self.emit(f"goto L_{a:08x};")
        else:
            self.tail(a)

    def tail(self, t):
        """Jump to t outside the function: tail call."""
        if t in self.rec.entries:
            self.emit(f"{SPILL} F_{t:08x}(c); return;")
        else:
            self.rec.note("jump out of function to non-entry", t)
            self.emit(f"{SPILL} rt_bad_jump(c, {h(self.entry)}, {h(t)});")

    def call_target(self, ins, op):
        """Returns ('direct', 'F_x'/'imp_x') or ('indirect', expr)."""
        if op.type == X.X86_OP_IMM:
            t = op.imm & 0xffffffff
            if t in self.rec.entries:
                return "direct", f"F_{t:08x}"
            self.rec.note("call to non-entry", ins.address)
            return "indirect", h(t)
        if op.type == X.X86_OP_MEM and not op.mem.base and not op.mem.index and op.mem.segment == 0:
            slot = op.mem.disp & 0xffffffff
            if slot in self.rec.iat:
                return "direct", self.rec.imp_name(slot)
        return "indirect", self.rd(ins, op, 4)

    # ---------- one instruction; returns True if it can continue in sequence ----------
    def insn(self, ins):
        mn, ops, nxt = ins.mnemonic, ins.operands, ins.address + ins.size
        op0 = ops[0] if ops else None
        op1 = ops[1] if len(ops) > 1 else None
        E = self.emit
        base = mn.split()[-1]
        if self.is_x87(ins):
            n0 = len(self.out)
            self.x87(ins)
            if self.static:
                lines = self.out[n0:]
                del self.out[n0:]
                for l in lines:
                    self.out.append(self.sx(l))
            return True
        if self.is_simd(ins) or base in {"movss", "mulps", "addps", "subps", "mulss", "shufps", "movaps", "movups",
                                          "mulsd", "addsd", "subsd", "divsd", "mulpd", "addpd", "movapd", "andpd",
                                          "andps", "xorps", "xorpd", "orps", "orpd", "andnps", "movlpd", "movhps",
                                          "movlps", "unpcklpd", "unpcklps", "cvtps2pi", "cvtpi2ps", "cvtss2si",
                                          "cvtsi2ss", "cvtdq2pd", "cvtsd2si", "rcpps", "cmpneqps", "cmpltpd",
                                          "ucomisd", "pshufd"}:
            if ins.opcode[0] not in (0xA4, 0xA5, 0xA6, 0xA7):
                self.rec.note(f"simd:{base}", ins.address)
                E(f"{SPILL} rt_unimpl(c, {h(ins.address)}, \"{mn}\");")
                return True
        if ins.opcode[0] in (0xA4, 0xA5, 0xA6, 0xA7, 0xAA, 0xAB, 0xAE, 0xAF) and len(ins.opcode) >= 1 \
                and ins.opcode[1] == 0:
            self.string(ins)
            return True

        if base in NOFLAGS or base.startswith("set") or base.startswith("j"):
            pass
        else:
            self.flagsrc = None

        if base == "mov":
            E(self.wr(ins, op0, self.rd(ins, op1)))
        elif base in ("movzx", "movsx"):
            v = self.rd(ins, op1)
            if base == "movsx":
                v = f"(uint32_t)(int32_t){SX[op1.size]}{v}"
            E(self.wr(ins, op0, v))
        elif base == "lea":
            E(self.wr(ins, op0, self.addr(ins, op1, seg=False)))
        elif base == "push":
            if op0.size == 2:
                E(f"{{ uint32_t v={self.rd(ins, op0)}; esp-=2; W16(esp,v); }}")
            else:
                E(f"{{ uint32_t v={self.rd(ins, op0, 4)}; esp-=4; W32(esp,v); }}")
        elif base == "pop":
            if op0.size == 2:
                E(f"{{ uint32_t v=R16(esp); esp+=2; {self.wr(ins, op0, 'v')} }}")
            else:
                E(f"{{ uint32_t v=R32(esp); esp+=4; {self.wr(ins, op0, 'v')} }}")
        elif base == "xchg":
            E(f"{{ uint32_t t0={self.rd(ins, op0)}, t1={self.rd(ins, op1)}; {self.wr(ins, op0, 't1')} "
              f"{self.wr(ins, op1, 't0')} }}")
        elif base in ("add", "adc", "sub", "sbb", "cmp"):
            self.arith(ins, base, op0, op1)
        elif base in ("and", "or", "xor", "test"):
            s = op0.size
            o = {"and": "&", "test": "&", "or": "|", "xor": "^"}[base]
            w = "" if base == "test" else self.wr(ins, op0, "r")
            E(f"{{ uint32_t r={self.rd(ins, op0)}{o}{self.rd(ins, op1, s)}; fr=r; cf=0; of=0; "
              f"{self.flags_szp('r', s)} {w} }}")
            self.flagsrc = ("test", s)
        elif base in ("inc", "dec"):
            s = op0.size
            sign = 1 << (s * 8 - 1)
            if base == "inc":
                E(f"{{ uint32_t r=({self.rd(ins, op0)}+1u)&{MASK[s]}; of=(r=={h(sign)}); {self.flags_szp('r', s)} "
                  f"{self.wr(ins, op0, 'r')} }}")
            else:
                E(f"{{ uint32_t a={self.rd(ins, op0)}; uint32_t r=(a-1u)&{MASK[s]}; of=(a=={h(sign)}); "
                  f"{self.flags_szp('r', s)} {self.wr(ins, op0, 'r')} }}")
        elif base == "neg":
            s = op0.size
            E(f"{{ uint32_t a={self.rd(ins, op0)}; uint32_t r=(0u-a)&{MASK[s]}; cf=(a!=0); "
              f"of=(a=={h(1 << (s * 8 - 1))}); {self.flags_szp('r', s)} {self.wr(ins, op0, 'r')} }}")
        elif base == "not":
            E(self.wr(ins, op0, f"~{self.rd(ins, op0)}"))
        elif base in ("shl", "sal", "shr", "sar"):
            self.shift(ins, base, op0, op1)
        elif base in ("shld", "shrd"):
            cnt = self.rd(ins, ops[2], 1) if ops[2].type == X.X86_OP_REG else h(ops[2].imm & 31)
            d, s_ = self.rd(ins, op0), self.rd(ins, op1)
            if base == "shld":
                body = f"r=(d<<n)|(s>>(32u-n)); cf=(d>>(32u-n))&1u;"
            else:
                body = f"r=(d>>n)|(s<<(32u-n)); cf=(d>>(n-1u))&1u;"
            E(f"{{ uint32_t n=({cnt})&31u, d={d}, s={s_}, r; if (n) {{ {body} of=((r^d)>>31)&1u; "
              f"{self.flags_szp('r', 4)} {self.wr(ins, op0, 'r')} }} }}")
        elif base == "rcr":
            s = op0.size
            n = op1.imm if op1.type == X.X86_OP_IMM else None
            if n != 1:
                self.rec.note("rcr count!=1", ins.address)
                E(f"{SPILL} rt_unimpl(c, {h(ins.address)}, \"rcr n\");")
            else:
                E(f"{{ uint32_t a={self.rd(ins, op0)}; uint32_t r=(a>>1)|(cf<<{s * 8 - 1}); of=((a>>{s * 8 - 1})&1u)^cf; "
                  f"cf=a&1u; {self.wr(ins, op0, 'r')} }}")
        elif base == "imul":
            self.imul(ins, ops)
        elif base == "mul":
            self.mul(ins, op0)
        elif base in ("div", "idiv"):
            self.div(ins, base, op0)
        elif base == "cdq":
            E("edx=(uint32_t)((int32_t)eax>>31);")
        elif base == "cwde":
            E("eax=(uint32_t)(int32_t)(int16_t)eax;")
        elif base.startswith("set"):
            E(self.wr(ins, op0, f"({self.cond(CC[base[3:]])}?1u:0u)"))
        elif base in ("bt", "bts"):
            self.bt(ins, base, op0, op1)
        elif base == "leave":
            E("esp=ebp; ebp=R32(esp); esp+=4;")
        elif base == "cld":
            E("c->df=0;")
        elif base == "std":
            E("c->df=1;")
        elif base in ("nop", "wait"):
            pass
        elif base in ("int3", "hlt", "ud2"):
            E(f"{SPILL} rt_unimpl(c, {h(ins.address)}, \"{mn}\");")
            return False
        elif base == "cpuid":
            E("rt_cpuid(&eax,&ebx,&ecx,&edx);")
        elif base == "pushal":
            E("{ uint32_t o=esp; esp-=32; W32(esp+28,eax);W32(esp+24,ecx);W32(esp+20,edx);W32(esp+16,ebx);"
              "W32(esp+12,o);W32(esp+8,ebp);W32(esp+4,esi);W32(esp,edi); }")
        elif base == "popal":
            E("edi=R32(esp);esi=R32(esp+4);ebp=R32(esp+8);ebx=R32(esp+16);edx=R32(esp+20);ecx=R32(esp+24);"
              "eax=R32(esp+28); esp+=32;")
        elif base == "pushfd":
            E("esp-=4; W32(esp, rt_flags_pack(c,cf,pf,zf,sf,of));")
        elif base == "popfd":
            E("{ uint32_t f=R32(esp); esp+=4; cf=f&1u; pf=(f>>2)&1u; zf=(f>>6)&1u; sf=(f>>7)&1u; of=(f>>11)&1u; "
              "c->df=(f>>10)&1u; c->eflags_ext=f&0x00240000u; }")
        elif base == "sahf":
            E("{ uint32_t f=(eax>>8)&0xffu; cf=f&1u; pf=(f>>2)&1u; zf=(f>>6)&1u; sf=(f>>7)&1u; }")
        elif base == "jmp":
            return self.jmp(ins, op0)
        elif base == "jecxz":
            t = op0.imm & 0xffffffff
            if t in self.addrs:
                E(f"if (ecx==0) goto L_{t:08x};")
            else:
                E("if (ecx==0) {")
                self.tail(t)
                E("}")
        elif base.startswith("j"):
            cc = CC[base[1:]]
            t = op0.imm & 0xffffffff
            if t in self.addrs:
                E(f"if ({self.cond(cc)}) goto L_{t:08x};")
            else:
                E(f"if ({self.cond(cc)}) {{")
                self.tail(t)
                E("}")
        elif base == "call":
            kind, tgt = self.call_target(ins, op0)
            if kind == "direct" and op0.type == X.X86_OP_IMM and self.rec.names.get(op0.imm & 0xffffffff) == "__setjmp3":
                # host setjmp armed on the return address (cf. rt_setjmp_slot / rt_call)
                E(f"esp-=4; W32(esp,{h(nxt)}); {SPILL} {tgt}(c); {RELOAD}")
                E(f"if (setjmp(*rt_setjmp_slot(c, {h(nxt)}))) {{ {RELOAD} }}")
            elif kind == "direct" and self.static and op0.type == X.X86_OP_IMM and op0.imm & 0xffffffff == FTOL:
                E(self.sx("{ int64_t r_=rt_ftol(ST(0)); eax=(uint32_t)r_; edx=(uint32_t)((uint64_t)r_>>32); } FPOP();"))
            elif kind == "direct" and self.entry == 0x4287f0 and tgt == "F_00404a00":
                # This hot model loop calls the native MMX-copy replacement thousands of times per frame.
                # Its replacement only copies guest bytes and pops RET; keep the return-address store and stack effect.
                E(f"esp-=4; W32(esp,{h(nxt)}); {{ uint32_t d_=R32(esp+4u), s_=R32(esp+8u); "
                  "int32_t n_=(int32_t)R32(esp+12u); if (n_>0) memmove(mb+d_, mb+s_, (size_t)n_); } esp+=4;")
            elif kind == "direct":
                self.call_net = self.rec.x87net.get(op0.imm & 0xffffffff, 0) if op0.type == X.X86_OP_IMM else 0
                E(f"esp-=4; W32(esp,{h(nxt)}); {SPILL} {tgt}(c); {RELOAD}")
                self.call_net = 0
            else:
                E(f"{{ uint32_t t={tgt}; esp-=4; W32(esp,{h(nxt)}); {SPILL} rt_call(c,t); {RELOAD} }}")
            self.flagsrc = None
        elif base == "ret":
            n = (op0.imm if op0 else 0) + 4
            E(f"esp+={n}u; {SPILL} return;")
            return False
        else:
            self.rec.note(f"unhandled:{mn}", ins.address)
            E(f"{SPILL} rt_unimpl(c, {h(ins.address)}, \"{mn}\");")
        return True

    def arith(self, ins, base, op0, op1):
        s = op0.size
        bits = s * 8
        a, b = self.rd(ins, op0), self.rd(ins, op1, s)
        carry = "+cf" if base == "adc" else ("+cf" if base == "sbb" else "")
        if base in ("add", "adc"):
            calc = f"uint64_t t=(uint64_t)a+b{carry}; uint32_t r=(uint32_t)t&{MASK[s]}; cf=(uint32_t)(t>>{bits})&1u; " \
                   f"of=(((a^r)&(b^r))>>{bits - 1})&1u;"
        else:
            calc = f"uint64_t t=(uint64_t)a-b-{'cf' if base == 'sbb' else '0'}; uint32_t r=(uint32_t)t&{MASK[s]}; " \
                   f"cf=(uint32_t)(t>>{bits})&1u; of=(((a^b)&(a^r))>>{bits - 1})&1u;"
        w = "" if base == "cmp" else self.wr(ins, op0, "r")
        self.emit(f"{{ uint32_t a={a}, b={b}; {calc} {self.flags_szp('r', s)} fa=a; fb=b; fr=r; {w} }}")
        self.flagsrc = ("cmp", s) if base in ("cmp", "sub") else None

    def shift(self, ins, base, op0, op1):
        s = op0.size
        bits = s * 8
        if op1.type == X.X86_OP_IMM:
            n = op1.imm & 31
            if n == 0:
                return
            cnt, guard = h(n), ""
        else:
            cnt, guard = f"({self.rd(ins, op1)}&31u)", "if (n) "
        a = self.rd(ins, op0)
        if base in ("shl", "sal"):
            body = f"r=(a<<n)&{MASK[s]}; cf=(a>>({bits}u-n))&1u; of=((r>>{bits - 1})&1u)^cf;"
        elif base == "shr":
            body = f"r=a>>n; cf=(a>>(n-1u))&1u; of=(a>>{bits - 1})&1u;"
        else:
            body = f"r=(uint32_t)((int32_t){SX[s]}a>>n)&{MASK[s]}; cf=(uint32_t)((int32_t){SX[s]}a>>(n-1u))&1u; of=0;"
        self.emit(f"{{ uint32_t n={cnt}, a={a}, r; {guard}{{ {body} {self.flags_szp('r', s)} "
                  f"{self.wr(ins, op0, 'r')} }} }}")

    def imul(self, ins, ops):
        E = self.emit
        if len(ops) == 1:
            s = ops[0].size
            v = self.rd(ins, ops[0])
            if s == 4:
                E(f"{{ int64_t p=(int64_t)(int32_t)eax*(int32_t){v}; eax=(uint32_t)p; edx=(uint32_t)((uint64_t)p>>32); "
                  f"cf=of=(p!=(int32_t)eax); }}")
            elif s == 2:
                E(f"{{ int32_t p=(int32_t)(int16_t)eax*(int16_t){v}; {self.wr_reg('ax', 'p')} {self.wr_reg('dx', 'p>>16')} "
                  f"cf=of=(p!=(int16_t)p); }}")
            else:
                E(f"{{ int32_t p=(int32_t)(int8_t)eax*(int8_t){v}; {self.wr_reg('ax', 'p')} cf=of=(p!=(int8_t)p); }}")
            return
        d = ops[0]
        a = self.rd(ins, ops[1] if len(ops) == 3 else ops[0])
        b = self.rd(ins, ops[2] if len(ops) == 3 else ops[1], d.size)
        t = SX[d.size]
        E(f"{{ int64_t p=(int64_t){t}{a}*{t}{b}; uint32_t r=(uint32_t)p&{MASK[d.size]}; cf=of=(p!=(int64_t){t}r); "
          f"{self.wr(ins, d, 'r')} }}")

    def mul(self, ins, op0):
        s = op0.size
        v = self.rd(ins, op0)
        if s == 4:
            self.emit(f"{{ uint64_t p=(uint64_t)eax*{v}; eax=(uint32_t)p; edx=(uint32_t)(p>>32); cf=of=(edx!=0); }}")
        elif s == 2:
            self.emit(f"{{ uint32_t p=(eax&0xffffu)*{v}; {self.wr_reg('ax', 'p')} {self.wr_reg('dx', 'p>>16')} cf=of=(p>>16)!=0; }}")
        else:
            self.emit(f"{{ uint32_t p=(eax&0xffu)*{v}; {self.wr_reg('ax', 'p')} cf=of=(p>>8)!=0; }}")

    def div(self, ins, base, op0):
        s = op0.size
        v = self.rd(ins, op0)
        z = f"if (!d) {{ {SPILL} rt_div0(c, {h(ins.address)}); }}"
        if base == "div":
            if s == 4:
                self.emit(f"{{ uint32_t d={v}; {z} uint64_t n=((uint64_t)edx<<32)|eax; eax=(uint32_t)(n/d); edx=(uint32_t)(n%d); }}")
            elif s == 2:
                self.emit(f"{{ uint32_t d={v}; {z} uint32_t n=((edx&0xffffu)<<16)|(eax&0xffffu); "
                          f"{self.wr_reg('ax', 'n/d')} {self.wr_reg('dx', 'n%d')} }}")
            else:
                self.emit(f"{{ uint32_t d={v}; {z} uint32_t n=eax&0xffffu; {self.wr_reg('al', 'n/d')} {self.wr_reg('ah', 'n%d')} }}")
        else:
            if s == 4:
                self.emit(f"{{ int32_t d=(int32_t){v}; {z} int64_t n=(int64_t)(((uint64_t)edx<<32)|eax); "
                          f"eax=(uint32_t)(int32_t)(n/d); edx=(uint32_t)(int32_t)(n%d); }}")
            elif s == 2:
                self.emit(f"{{ int32_t d=(int16_t){v}; {z} int32_t n=(int32_t)(((edx&0xffffu)<<16)|(eax&0xffffu)); "
                          f"{self.wr_reg('ax', 'n/d')} {self.wr_reg('dx', 'n%d')} }}")
            else:
                self.emit(f"{{ int32_t d=(int8_t){v}; {z} int32_t n=(int16_t)(eax&0xffffu); "
                          f"{self.wr_reg('al', 'n/d')} {self.wr_reg('ah', 'n%d')} }}")

    def bt(self, ins, base, op0, op1):
        if op0.type == X.X86_OP_MEM and op1.type == X.X86_OP_REG:
            a = self.addr(ins, op0)
            o = self.rd(ins, op1)
            ea = f"({a}+(uint32_t)(((int32_t){o}>>5)*4))"
            setb = f"W32(ea, R32(ea)|(1u<<b));" if base == "bts" else ""
            self.emit(f"{{ uint32_t ea={ea}, b={o}&31u; cf=(R32(ea)>>b)&1u; {setb} }}")
        else:
            bit = f"({self.rd(ins, op1)}&31u)"
            setb = self.wr(ins, op0, f"{self.rd(ins, op0)}|(1u<<b)") if base == "bts" else ""
            self.emit(f"{{ uint32_t b={bit}; cf=({self.rd(ins, op0)}>>b)&1u; {setb} }}")

    def jmp(self, ins, op0):
        E = self.emit
        if op0.type == X.X86_OP_IMM:
            t = op0.imm & 0xffffffff
            if t in self.addrs:
                E(f"goto L_{t:08x};")
            else:
                self.tail(t)
            return False
        if ins.address in self.switches:
            cases = " ".join(f"case {h(t)}: goto L_{t:08x};" for t in self.switches[ins.address])
            E(f"{{ uint32_t t={self.rd(ins, op0, 4)}; switch (t) {{ {cases} default: {SPILL} rt_call(c,t); return; }} }}")
            return False
        kind, tgt = self.call_target(ins, op0)
        if kind == "direct":
            E(f"{SPILL} {tgt}(c); return;")
        else:
            E(f"{{ uint32_t t={tgt}; {SPILL} rt_call(c,t); return; }}")
        return False

    def string(self, ins):
        op = ins.opcode[0]
        rep = ins.prefix[0]
        s = 1 if op in (0xA4, 0xA6, 0xAA, 0xAE) else (2 if ins.prefix[2] == 0x66 else 4)
        R, W = {1: ("R8", "W8"), 2: ("R16", "W16"), 4: ("R32", "W32")}[s]
        acc = {1: "(eax&0xffu)", 2: "(eax&0xffffu)", 4: "eax"}[s]
        st = f"int32_t st=c->df?-{s}:{s};"
        E = self.emit
        self.flagsrc = None
        if op in (0xA4, 0xA5):
            one = f"{W}(edi,{R}(esi)); esi+=st; edi+=st;"
            if rep:
                E(f"{{ {st} if (!c->df && (uint32_t)(edi-esi) >= ecx*{s}u) {{ memmove(mb+edi, mb+esi, ecx*{s}u); "
                  f"esi+=ecx*{s}u; edi+=ecx*{s}u; ecx=0; }} else for (; ecx; ecx--) {{ {one} }} }}")
            else:
                E(f"{{ {st} {one} }}")
        elif op in (0xAA, 0xAB):
            one = f"{W}(edi,{acc}); edi+=st;"
            if rep:
                fast = f"if (!c->df && {s}==1) {{ memset(mb+edi, eax&0xff, ecx); edi+=ecx; ecx=0; }} else " if s == 1 else ""
                E(f"{{ {st} {fast}for (; ecx; ecx--) {{ {one} }} }}")
            else:
                E(f"{{ {st} {one} }}")
        else:
            bits = s * 8
            if op in (0xA6, 0xA7):
                ab = f"uint32_t a={R}(esi), b={R}(edi); esi+=st; edi+=st;"
            else:
                ab = f"uint32_t a={acc}, b={R}(edi); edi+=st;"
            fl = f"uint64_t t=(uint64_t)a-b; uint32_t r=(uint32_t)t&{MASK[s]}; cf=(uint32_t)(t>>{bits})&1u; " \
                 f"of=(((a^b)&(a^r))>>{bits - 1})&1u; {self.flags_szp('r', s)}"
            if rep:
                stop = "if (!zf) break;" if rep == 0xF3 else "if (zf) break;"
                E(f"{{ {st} while (ecx) {{ {ab} ecx--; {fl} {stop} }} }}")
            else:
                E(f"{{ {st} {ab} {fl} }}")

    def x87(self, ins):
        E = self.emit
        op, modrm = ins.opcode[0], ins.modrm
        mod, reg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
        ari = {0: "ST(0)+v", 1: "ST(0)*v", 4: "ST(0)-v", 5: "v-ST(0)", 6: "ST(0)/v", 7: "v/ST(0)"}
        if mod != 3:
            mem = next(o for o in ins.operands if o.type == X.X86_OP_MEM)
            A = self.addr(ins, mem)
            if op in (0xD8, 0xDC, 0xDA, 0xDE):
                v = {0xD8: f"(double)rt_f32(R32({A}))", 0xDC: f"rt_f64(R64({A}))",
                     0xDA: f"(double)(int32_t)R32({A})", 0xDE: f"(double)(int16_t)R16({A})"}[op]
                if reg in ari:
                    E(f"{{ double v={v}; ST(0)={ari[reg]}; }}")
                else:
                    E(f"{{ double v={v}; rt_fcom(c, ST(0), v); {'FPOP();' if reg == 3 else ''} }}")
                return
            tbl = {
                (0xD9, 0): f"FPUSH((double)rt_f32(R32({A})));",
                (0xD9, 2): f"W32({A}, rt_u32f((float)ST(0)));",
                (0xD9, 3): f"W32({A}, rt_u32f((float)ST(0))); FPOP();",
                (0xD9, 5): f"c->fcw=R16({A});",
                (0xD9, 7): f"W16({A}, c->fcw);",
                (0xDB, 0): f"FPUSH((double)(int32_t)R32({A}));",
                (0xDB, 2): f"W32({A}, (uint32_t)rt_fist32(c, ST(0)));",
                (0xDB, 3): f"W32({A}, (uint32_t)rt_fist32(c, ST(0))); FPOP();",
                (0xDB, 5): f"FPUSH(rt_f80_load(mb+{A}));",
                (0xDB, 7): f"rt_f80_store(mb+{A}, ST(0)); FPOP();",
                (0xDD, 0): f"FPUSH(rt_f64(R64({A})));",
                (0xDD, 2): f"W64({A}, rt_u64f(ST(0)));",
                (0xDD, 3): f"W64({A}, rt_u64f(ST(0))); FPOP();",
                (0xDD, 4): f"rt_frstor(c, {A});",
                (0xDD, 6): f"rt_fsave(c, {A});",
                (0xDD, 7): f"W16({A}, rt_fnstsw(c));",
                (0xDF, 0): f"FPUSH((double)(int16_t)R16({A}));",
                (0xDF, 2): f"W16({A}, (uint16_t)rt_fist16(c, ST(0)));",
                (0xDF, 3): f"W16({A}, (uint16_t)rt_fist16(c, ST(0))); FPOP();",
                (0xDF, 5): f"FPUSH((double)(int64_t)R64({A}));",
                (0xDF, 7): f"W64({A}, (uint64_t)rt_fist64(c, ST(0))); FPOP();",
            }
            if (op, reg) in tbl:
                E(tbl[(op, reg)])
            else:
                self.rec.note(f"x87 mem {op:x}/{reg}", ins.address)
                E(f"{SPILL} rt_unimpl(c, {h(ins.address)}, \"{ins.mnemonic}\");")
            return
        i = rm
        if op == 0xD8:
            if reg in ari:
                E(f"{{ double v=ST({i}); ST(0)={ari[reg]}; }}")
            else:
                E(f"rt_fcom(c, ST(0), ST({i})); {'FPOP();' if reg == 3 else ''}")
            return
        if op in (0xDC, 0xDE):
            pop = "FPOP();" if op == 0xDE else ""
            if op == 0xDE and modrm == 0xD9:
                E("rt_fcom(c, ST(0), ST(1)); FPOP(); FPOP();")
                return
            res = {0: f"ST({i})+ST(0)", 1: f"ST({i})*ST(0)", 4: f"ST(0)-ST({i})", 5: f"ST({i})-ST(0)",
                   6: f"ST(0)/ST({i})", 7: f"ST({i})/ST(0)"}
            if reg in res:
                E(f"ST({i})={res[reg]}; {pop}")
            else:
                E(f"rt_fcom(c, ST(0), ST({i})); {'FPOP();' if reg == 3 or op == 0xDE else ''}")
            return
        if op == 0xD9:
            if reg == 0:
                E(f"{{ double v=ST({i}); FPUSH(v); }}")
            elif reg == 1:
                E(f"{{ double v=ST(0); ST(0)=ST({i}); ST({i})=v; }}")
            elif modrm in FCONST:
                E(f"FPUSH({FCONST[modrm]});")
            else:
                m = {0xD0: "", 0xE0: "ST(0)=-ST(0);", 0xE1: "ST(0)=fabs(ST(0));", 0xE4: "rt_fcom(c, ST(0), 0.0);",
                     0xF0: "ST(0)=exp2(ST(0))-1.0;", 0xF1: "ST(1)=ST(1)*log2(ST(0)); FPOP();",
                     0xF2: "ST(0)=tan(ST(0)); FPUSH(1.0); c->fsw&=~FSW_C2;",
                     0xF3: "ST(1)=atan2(ST(1),ST(0)); FPOP();", 0xF5: "ST(0)=remainder(ST(0),ST(1)); c->fsw&=~FSW_C2;",
                     0xF6: "c->ftop=(c->ftop-1)&7;", 0xF7: "c->ftop=(c->ftop+1)&7;",
                     0xF8: "ST(0)=fmod(ST(0),ST(1)); c->fsw&=~FSW_C2;", 0xF9: "ST(1)=ST(1)*log2(ST(0)+1.0); FPOP();",
                     0xFA: "ST(0)=sqrt(ST(0));", 0xFB: "{ double v=ST(0); ST(0)=sin(v); FPUSH(cos(v)); c->fsw&=~FSW_C2; }",
                     0xFC: "ST(0)=rt_fround(c, ST(0));", 0xFD: "ST(0)=ldexp(ST(0),(int)trunc(ST(1)));",
                     0xFE: "ST(0)=sin(ST(0)); c->fsw&=~FSW_C2;", 0xFF: "ST(0)=cos(ST(0)); c->fsw&=~FSW_C2;"}
                if modrm in m:
                    E(m[modrm])
                else:
                    self.rec.note(f"x87 d9 {modrm:x}", ins.address)
                    E(f"{SPILL} rt_unimpl(c, {h(ins.address)}, \"{ins.mnemonic}\");")
            return
        if op == 0xDD:
            m = {0: "", 2: f"ST({i})=ST(0);", 3: f"ST({i})=ST(0); FPOP();", 4: f"rt_fcom(c, ST(0), ST({i}));",
                 5: f"rt_fcom(c, ST(0), ST({i})); FPOP();"}
            E(m.get(reg, f"{SPILL} rt_unimpl(c, {h(ins.address)}, \"{ins.mnemonic}\");"))
            return
        if op == 0xDA and modrm == 0xE9:
            E("rt_fcom(c, ST(0), ST(1)); FPOP(); FPOP();")
            return
        if op == 0xDB and modrm in (0xE2, 0xE3):
            E("c->fsw&=0x7f00u;" if modrm == 0xE2 else "c->fsw=0; c->ftop=0; c->fcw=0x37f;")
            return
        if op == 0xDF and modrm == 0xE0:
            E(self.wr_reg("ax", "rt_fnstsw(c)"))
            return
        if op == 0xDF and reg == 0:
            E("FPOP();")
            return
        self.rec.note(f"x87 reg {op:x} {modrm:x}", ins.address)
        E(f"{SPILL} rt_unimpl(c, {h(ins.address)}, \"{ins.mnemonic}\");")


def optimize_sound_archive_copies(code):
    """Opt-in reader delivery for forward bulk copies; stock loops are fallback."""
    code = code.replace('uint8_t *const mb = g_mem;', '''uint8_t *const mb = g_mem;
  extern int g_sound_transfer_on;
  extern int rt_sound_archive_transfer(CPU *,uint32_t,uint32_t,uint32_t);''', 1)
    for width in (1, 4):
        before = f'memmove(mb+edi, mb+esi, ecx*{width}u);'
        after = f'if (!__atomic_load_n(&g_sound_transfer_on,__ATOMIC_RELAXED) || !rt_sound_archive_transfer(c,edi,esi,ecx*{width}u)) ' + before
        if code.count(before) != 4:
            raise RuntimeError(f'GOG sound stream copies changed: width {width}')
        code = code.replace(before, after)
    # Preserve writes, but route scalar source reads in these same functions.
    code = re.sub(r'\bR(8|16|32)\(', lambda m: f'rt_sound_r{m.group(1)}(mb,c,', code)
    return '#include "sound_archive_access.h"\n' + code


def main():
    rec = Recompiler()
    GEN.mkdir(parents=True, exist_ok=True)
    header = ['#include "x86rt.h"', '#include "x86_funcs.h"', ""]
    protos, files, cur, cur_n, fileno = [], [], [], 0, 0
    order = sorted(rec.funcs)
    # Net effect of each function on the x87 stack (fixed point: a caller depends on its callees).
    for _ in range(6):
        net = {}
        for a in order:
            if a not in OVERRIDES:
                f = Func(rec, a)
                f.scan()
                f.plan_x87(quiet=True)
                if f.net:
                    net[a] = f.net
        if net == rec.x87net:
            break
        rec.x87net = net
    print(f"x87: {len(rec.x87net)} functions with non-zero net effect", file=sys.stderr)
    for k, a in enumerate(order):
        if a in OVERRIDES:
            code = f"void {OVERRIDES[a]}(CPU *c);\nvoid F_{a:08x}(CPU *c) {{ {OVERRIDES[a]}(c); }}"
        else:
            code = Func(rec, a).gen()
            if a in (0x408d50, 0x409530, 0x409a30):
                code = optimize_sound_archive_copies(code)
            if a == 0x40a960:
                code = optimize_audio_packets(code)
            if a == 0x4287f0:
                code = optimize_model_bbox(code)
                code = optimize_model_uv(code)
                code = optimize_model_palette(code)
                code = optimize_model_vertices(code)
            if a in WRAPS:
                code = code.replace(f"void F_{a:08x}(CPU *c) {{", f"void F_{a:08x}_orig(CPU *c) {{", 1)
                code = (f"void {WRAPS[a]}(CPU *c);\nvoid F_{a:08x}_orig(CPU *c);\n"
                        f"void F_{a:08x}(CPU *c) {{ {WRAPS[a]}(c); }}\n") + code
        cur.append(code)
        cur_n += len(rec.funcs[a])
        protos.append(f"void F_{a:08x}(CPU *c);")
        if cur_n >= 2500 or k == len(order) - 1:
            put(GEN / f"x86_f{fileno:03d}.c", "\n".join(header) + "\n" + "\n\n".join(cur) + "\n")
            fileno, cur, cur_n = fileno + 1, [], 0
    # thunks
    th = ['#include "x86rt.h"', '#include "x86_funcs.h"', ""]
    for a, t in sorted(rec.thunks.items()):
        protos.append(f"void F_{a:08x}(CPU *c);")
        if t.startswith("EXTERNAL"):
            # import thunk: JMP [IAT] -> host function
            ins = rec.decode(a)
            slot = ins.operands[0].mem.disp & 0xffffffff
            th.append(f"void F_{a:08x}(CPU *c) {{ {rec.imp_name(slot)}(c); }}")
        else:
            tt = int(t, 16)
            if tt not in rec.entries:
                rec.note("thunk to non-entry", a)
                th.append(f"void F_{a:08x}(CPU *c) {{ rt_bad_jump(c, {h(a)}, {h(tt)}); }}")
            else:
                th.append(f"void F_{a:08x}(CPU *c) {{ F_{tt:08x}(c); }}")
    put(GEN / "x86_thunks.c", "\n".join(th) + "\n")
    imps = sorted({rec.imp_name(s) for s in rec.iat})
    protos += [f"void {n}(CPU *c);" for n in imps]
    put(GEN / "x86_funcs.h", "#pragma once\n#include \"x86rt.h\"\n" + "\n".join(protos) + "\n")
    # sorted dispatch table + IAT table
    disp = ['#include "x86_funcs.h"', "const struct { uint32_t a; gfn f; } x86_functab[] = {"]
    disp += [f"  {{{h(a)}, F_{a:08x}}}," for a in sorted(rec.entries)]
    disp += ["};", f"const unsigned x86_functab_n = {len(rec.entries)};",
             "const struct { uint32_t slot; gfn f; const char *name; } x86_iat[] = {"]
    disp += [f"  {{{h(s)}, {rec.imp_name(s)}, \"{rec.iat[s]}\"}}," for s in sorted(rec.iat)]
    disp += ["};", f"const unsigned x86_iat_n = {len(rec.iat)};",
             f"const uint32_t x86_entry = {h(rec.pe.entry)};"]
    put(GEN / "x86_dispatch.c", "\n".join(disp) + "\n")
    print(f"{len(order)} functions, {len(rec.thunks)} thunks, {fileno} files, {len(imps)} imports")
    for k, n in rec.report.most_common():
        print(f"  {n:6}  {k}   ex: {' '.join(rec.report_sites[k][:6])}")


if __name__ == "__main__":
    main()
