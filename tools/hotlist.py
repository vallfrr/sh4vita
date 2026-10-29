# usage: hotlist.py nm.txt min_calls out.txt hot1.txt [hot2.txt ...]
#   nm.txt   = `nm sh4.exe` of the instrumented PC build (lines "addr t F_xxxxxxxx")
#   hotN.txt = SH4_HOTDUMP of a `SH4_PROFILE=a,b` run (lines "offset_vs_F_00554100 entries")
# writes out.txt = C header `gen/x86_hot.h`: __attribute__((hot)) prototypes of the F_xxxxxxxx called at least min_calls times (sum of the windows).
# CMake includes it (-include) in the generated code: GCC places these functions in .text.hot, contiguous (I-cache/TLB locality).
import sys, collections
sym = {}
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) == 3 and p[2].startswith('F_'): sym[p[2]] = int(p[0], 16)
ref = sym['F_00554100']
byaddr = {a - ref: n for n, a in sym.items()}
tot = collections.Counter()
for f in sys.argv[4:]:
    for l in open(f):
        o, n = l.split(); tot[int(o)] += int(n)
hot = sorted(byaddr[o] for o, n in tot.items() if n >= int(sys.argv[2]) and o in byaddr)
miss = sum(1 for o in tot if o not in byaddr)
open(sys.argv[3], 'w').write('#pragma once\n#include "x86rt.h"\n' + ''.join(f'void {n}(CPU *c) __attribute__((hot));\n' for n in hot))
print(f"{len(hot)} hot functions out of {len(sym)} ({miss} addresses without symbol), total calls {sum(tot.values())}")
