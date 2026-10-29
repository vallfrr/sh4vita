# bmp2png.py in.bmp out.png [scale] : BMP 32/24 bits -> PNG, stdlib seul.
import struct, sys, zlib
b = open(sys.argv[1], 'rb').read()
off = struct.unpack('<I', b[10:14])[0]
w, h, _, bpp = struct.unpack('<iihh', b[18:30])
bp = bpp // 8
st = (w * bp + 3) & ~3
top_down = h < 0
h = abs(h)
k = int(sys.argv[3]) if len(sys.argv) > 3 else 1
rows = []
for y in range(0, h, k):
    r = b[off + (y if top_down else h - 1 - y) * st:][:w * bp]
    row = bytearray()
    for x in range(0, w, k):
        row += bytes((r[x * bp + 2], r[x * bp + 1], r[x * bp]))
    rows.append(b'\0' + bytes(row))
raw = b''.join(rows)
def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
open(sys.argv[2], 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', len(range(0, w, k)), len(rows), 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
