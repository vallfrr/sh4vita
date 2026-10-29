# perftab.py log [from to]: ms/frame table per [perf] window of the Vita log, with presents; stdlib only.
import re, sys
lo, hi = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (0, 10**9)
t = open(sys.argv[1], encoding='utf-8', errors='replace').read().replace('Ã©', 'e')
cur = {}
for l in t.splitlines():
    m = re.match(r'\[perf\] ([\d.]+) ms/frame; frame body ([\d.-]+) ms of which game code ([\d.-]+) ms and GL ([\d.-]+)', l)
    if m: cur = dict(ms=m[1], body=m[2], code=m[3], gl=m[4]); continue
    m = re.match(r'\[perf\] GL/frame: prog\+unif ([\d.]+) tex ([\d.]+) idx ([\d.]+) attr ([\d.]+) .*?draw ([\d.]+)', l)
    if m: cur.update(pu=m[1], idx=m[3], draw=m[5]); continue
    m = re.match(r'\[q\] render active ([\d.]+) ms/frame .*?\| (\d+) recs', l)
    if m: cur.update(rend=m[1], rec=m[2]); continue
    m = re.match(r'\[stats\] presents=(\d+) draws=(\d+)', l)
    if m and cur:
        p = int(m[1])
        if lo <= p <= hi: print(f"{p:6d} {cur.get('ms','?'):>6} body {cur.get('body','?'):>6} code {cur.get('code','?'):>6} gl {cur.get('gl','?'):>6} idx {cur.get('idx','?'):>5} pu {cur.get('pu','?'):>5} draw {cur.get('draw','?'):>5} rec {cur.get('rec','?'):>5} rend {cur.get('rend','?'):>5}")
        cur = {}
