"""Extract local generated reference functions for differential testing (output stays in build/)."""
import argparse
from pathlib import Path

FUNCTIONS = (0x573a20, 0x401ec0, 0x401570, 0x41abc0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--gen", type=Path, default=Path("port/gen"))
    ap.add_argument("--out", type=Path, default=Path("build/room-audit/raytri_reference.c"))
    args = ap.parse_args()
    remaining = set(FUNCTIONS)
    output = ['#include "x86rt.h"\n']
    for path in args.gen.glob("x86_f*.c"):
        source = path.read_text(encoding="utf-8")
        for address in sorted(remaining.copy()):
            name = f"F_{address:08x}"
            start = source.find(f"void {name}_orig(") if address == 0x573a20 else -1
            if start < 0:
                start = source.find(f"void {name}(")
            if start < 0:
                continue
            end = source.find("\nvoid F_", start + 1)
            body = source[start:end if end >= 0 else len(source)]
            if address == 0x573a20 and "_orig(" not in body.splitlines()[0]:
                body = body.replace(f"void {name}(", f"void {name}_orig(", 1)
            output.append(body)
            remaining.remove(address)
    if remaining:
        raise RuntimeError(f"Missing generated functions: {remaining}")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(output), encoding="utf-8")
    print(args.out)


if __name__ == "__main__":
    main()
