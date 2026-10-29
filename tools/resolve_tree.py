# usage: resolve_tree.py log nm.txt: replaces "T +off" in [tree]/[prof] lines with the name F_xxxxxxxx (nm of the same build's exe)
import sys
sym = {}
for l in open(sys.argv[2]):
    p = l.split()
    if len(p) == 3 and p[2].startswith('F_'): sym[p[2]] = int(p[0], 16)
ref = sym['F_00554100']
by = {a - ref: n for n, a in sym.items()}
for l in open(sys.argv[1], encoding='utf-8', errors='replace'):
    if (l.startswith('[tree]') or l.startswith('[prof]')) and ' T ' in l:
        h, o = l.rstrip().rsplit(' T ', 1)
        print(h, by.get(int(o), '?' + o))
