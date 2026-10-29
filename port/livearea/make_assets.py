"""Generates the legacy placeholder LiveArea icon0.png / bg.png / startup.png.

Running this script overwrites the current, supplied LiveArea artwork.

8-bit palette PNGs only (standard library): the Vita rejects full-colour PNGs inside a .vpk
(install error 0x8010113D).
"""
import pathlib, struct, zlib

OUT = pathlib.Path(__file__).resolve().parent
SIGNATURE = bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
FILTER_NONE = bytes([0])
GLYPH = {  # police 5x7
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
}


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def png(path, w, h, px):
    palette, rows = {}, []
    for y in range(h):
        row = bytearray(FILTER_NONE)
        for x in range(w):
            row.append(palette.setdefault(px(x, y), len(palette)))
        rows.append(bytes(row))
    assert len(palette) <= 256, len(palette)
    plte = b"".join(bytes(c) for c in palette)
    path.write_bytes(
        SIGNATURE
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
        + chunk(b"PLTE", plte)
        + chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
        + chunk(b"IEND", b"")
    )


def bg(w, h):
    def f(x, y):
        dx, dy = (x - w / 2) / (w / 2), (y - h / 2) / (h / 2)
        v = round(max(0.0, 1.0 - (dx * dx + dy * dy) * 0.9) * 120) / 120
        return (int(18 + 70 * v), int(10 + 14 * v), int(10 + 12 * v))
    return f


def with_text(base, w, h, text, scale):
    cw = 6 * scale * len(text) - scale
    x0, y0 = (w - cw) // 2, (h - 7 * scale) // 2

    def f(x, y):
        gx, gy = x - x0, y - y0
        if 0 <= gy < 7 * scale and 0 <= gx < cw:
            i, off = divmod(gx // scale, 6)
            if off < 5 and GLYPH[text[i]][gy // scale][off] == "1":
                return (225, 215, 200)
        return base(x, y)
    return f


png(OUT / "icon0.png", 128, 128, with_text(bg(128, 128), 128, 128, "SH4", 5))
png(OUT / "bg.png", 840, 500, bg(840, 500))
png(OUT / "startup.png", 280, 158, with_text(bg(280, 158), 280, 158, "SH4", 10))
print("ok")
