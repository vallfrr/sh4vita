# bright.py in.png out.png [gain]: brightens a dark Vita screenshot (RGBA PNG written by shot2png) for reading
import sys, zlib, struct
def rd(p):
    d = open(p, 'rb').read(); pos = 8; idat = b''
    while pos < len(d):
        l, = struct.unpack('>I', d[pos:pos+4]); t = d[pos+4:pos+8]
        if t == b'IHDR': w, h = struct.unpack('>II', d[pos+8:pos+16]); ct = d[pos+8+9]
        if t == b'IDAT': idat += d[pos+8:pos+8+l]
        pos += 12 + l
    raw = zlib.decompress(idat); bpp = 4 if ct == 6 else 3; st = w * bpp; out = []; prev = bytearray(st); i = 0
    for y in range(h):
        f = raw[i]; i += 1; row = bytearray(raw[i:i+st]); i += st
        for x in range(st):
            a = row[x-bpp] if x >= bpp else 0; b = prev[x]; c = prev[x-bpp] if x >= bpp else 0
            if f == 1: row[x] = (row[x]+a) & 255
            elif f == 2: row[x] = (row[x]+b) & 255
            elif f == 3: row[x] = (row[x]+((a+b) >> 1)) & 255
            elif f == 4:
                p = a+b-c; pa = abs(p-a); pb = abs(p-b); pc = abs(p-c)
                row[x] = (row[x]+(a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        out.append(row); prev = row
    return w, h, bpp, out
w, h, bpp, rows = rd(sys.argv[1]); g = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
lut = bytes(min(255, int(255 * ((i/255.0) ** (1/ (1+ (g-1)*0.5))) * 1.0)) for i in range(256))
out = []
for r in rows:
    o = bytearray(r)
    for x in range(len(o)):
        if bpp == 4 and x % 4 == 3: continue
        o[x] = lut[o[x]]
    out.append(b'\x00' + bytes(o))
def ch(t, d): c = struct.pack('>I', len(d)) + t + d; return c + struct.pack('>I', zlib.crc32(t+d) & 0xffffffff)
open(sys.argv[2], 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6 if bpp == 4 else 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(b''.join(out), 1)) + ch(b'IEND', b''))
