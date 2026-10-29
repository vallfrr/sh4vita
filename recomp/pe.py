"""Minimal PE32 reader: sections, bytes by virtual address, import table (IAT)."""
import struct


class PE:
    def __init__(self, path):
        self.data = d = open(path, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        opt = pe + 24
        assert struct.unpack_from("<H", d, opt)[0] == 0x10B, "PE32 expected"
        self.image_base = struct.unpack_from("<I", d, opt + 28)[0]
        self.entry = self.image_base + struct.unpack_from("<I", d, opt + 16)[0]
        self.image_size = struct.unpack_from("<I", d, opt + 56)[0]
        self.import_rva = struct.unpack_from("<I", d, opt + 104)[0]
        self.sections = []
        for i in range(nsec):
            name, vsz, va, rsz, raw = struct.unpack_from("<8sIIII", d, opt + optsz + i * 40)
            chars = struct.unpack_from("<I", d, opt + optsz + i * 40 + 36)[0]
            self.sections.append((name.rstrip(b"\0").decode(), self.image_base + va, vsz, raw, rsz, chars))

    def off(self, va):
        for _, sva, vsz, raw, rsz, _ in self.sections:
            if sva <= va < sva + max(vsz, rsz):
                return None if va - sva >= rsz else raw + va - sva
        return None

    def read(self, va, n):
        o = self.off(va)
        return None if o is None else self.data[o:o + n]

    def u32(self, va):
        b = self.read(va, 4)
        return None if b is None or len(b) < 4 else struct.unpack("<I", b)[0]

    def cstr(self, va):
        o = self.off(va)
        return self.data[o:self.data.index(b"\0", o)].decode("latin1")

    def imports(self):
        """{IAT address: 'DLL!name'}; ordinals are written 'DLL!#n'."""
        out, p = {}, self.image_base + self.import_rva
        while True:
            ilt, _, _, name, iat = struct.unpack("<5I", self.read(p, 20))
            if name == 0:
                return out
            dll = self.cstr(self.image_base + name).upper()
            q, slot = self.image_base + (ilt or iat), self.image_base + iat
            while (e := self.u32(q)):
                fn = f"#{e & 0xFFFF}" if e & 0x80000000 else self.cstr(self.image_base + e + 2)
                out[slot] = f"{dll}!{fn}"
                q += 4
                slot += 4
            p += 20


def demo(path):
    pe = PE(path)
    assert pe.image_base == 0x400000
    imps = pe.imports()
    assert "D3D8.DLL!Direct3DCreate8" in imps.values()
    return pe, imps


if __name__ == "__main__":
    import sys
    pe, imps = demo(sys.argv[1])
    print(f"entry={pe.entry:#x} image_size={pe.image_size:#x} imports={len(imps)}")
    for s in pe.sections:
        print(f"  {s[0]:8} va={s[1]:#x} vsz={s[2]:#x} raw={s[3]:#x} rsz={s[4]:#x} chars={s[5]:#x}")
