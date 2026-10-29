# sheet.py out.png cols gain in1.png in2.png ... : contact sheet (each image 1/3 size, brightened)
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
cols = int(sys.argv[2]); gain = float(sys.argv[3]); files = sys.argv[4:]
S = 3; W, H = 960 // S, 544 // S; n = len(files); rn = (n + cols - 1) // cols
lut = bytes(min(255, int(255 * (i / 255.0) ** (1 / gain))) for i in range(256))
canvas = [bytearray(cols * W * 3) for _ in range(rn * H)]
for k, f in enumerate(files):
    w, h, bpp, r = rd(f); cx, cy = k % cols, k // cols
    for y in range(H):
        s = r[min(h-1, y * h // H)]; o = canvas[cy * H + y]
        for x in range(W):
            q = (x * w // W) * bpp; o[(cx * W + x) * 3: (cx * W + x) * 3 + 3] = bytes([lut[s[q]], lut[s[q+1]], lut[s[q+2]]])
raw = b''.join(b'\x00' + bytes(r) for r in canvas)
def ch(t, d): c = struct.pack('>I', len(d)) + t + d; return c + struct.pack('>I', zlib.crc32(t+d) & 0xffffffff)
open(sys.argv[1], 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', cols * W, rn * H, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 1)) + ch(b'IEND', b''))
