# drawlist.py log.txt [first_draw_index] : one line per [draw] of a trace (frame-split on ps=1 blits)
import re, sys
L = open(sys.argv[1], encoding='utf8', errors='replace').read().split('\n')
i = 0
for l in L:
    m = re.match(r'\[draw\] prim=(\d+) n=(\d+) first=\d+ idx=(\d) vs=(\w+) ps=(\w+) tex0=(\S+?)\((\S+) fmt (\w+)\) zen=(\d) blend=(\d)\((\d),(\d)\) atest=(\d).*? fog=(\d) .*?vp=(\S+) (\S+) rt=(\S+)', l)
    if m:
        g = m.groups(); print(i, 'p%s n=%s vs=%s ps=%s tex=%s %s zen=%s bl=%s(%s,%s) vp=%s %s rt=%s' % (g[0], g[1], g[3], g[4], g[5], g[6], g[8], g[9], g[10], g[11], g[14], g[15], g[16])); i += 1
