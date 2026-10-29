# tga2png.py in.tga out.png [gain]: uncompressed 32-bit TGA (bottom-up unless bit 0x20) -> brightened PNG
import sys, zlib, struct
d = open(sys.argv[1], 'rb').read(); w = d[12] | d[13] << 8; h = d[14] | d[15] << 8; top = d[17] & 0x20; g = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
px = d[18:]; rows = []
for y in range(h):
    sy = y if top else h - 1 - y; r = bytearray(); o = sy * w * 4
    for x in range(w):
        b, gg, rr = px[o+4*x], px[o+4*x+1], px[o+4*x+2]
        r += bytes([min(255, int(rr*g)), min(255, int(gg*g)), min(255, int(b*g))])
    rows.append(b'\x00' + bytes(r))
def ch(t, dd): c = struct.pack('>I', len(dd)) + t + dd; return c + struct.pack('>I', zlib.crc32(t+dd) & 0xffffffff)
open(sys.argv[2], 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(b''.join(rows), 1)) + ch(b'IEND', b''))
