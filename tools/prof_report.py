# usage: prof_report.py prof.txt syms.txt  (syms = nm -n | grep ' [tT] ')
import sys, bisect, collections
syms = []
for l in open(sys.argv[2]):
    a, _, n = l.split()[:3]; syms.append((int(a, 16), n))
addrs = [a for a, _ in syms]
base = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0
c = collections.Counter(); tot = 0
for l in open(sys.argv[1]):
    r = int(l, 16) - base; i = bisect.bisect_right(addrs, r) - 1
    c[syms[i][1] if i >= 0 else '?'] += 1; tot += 1
for n, k in c.most_common(int(sys.argv[4]) if len(sys.argv) > 4 else 40): print(f"{100*k/tot:5.1f}% {k:5d} {n}")
