"""Lists the DLLs and functions imported by a 32-bit PE (standard library only)."""
import struct, sys

def imports(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24
    assert struct.unpack_from("<H", d, opt)[0] == 0x10B, "PE32 expected"
    imp_rva = struct.unpack_from("<I", d, opt + 104)[0]
    secs = [struct.unpack_from("<8sIIII", d, opt + optsz + i * 40) for i in range(nsec)]

    def off(rva):
        for _, vsz, va, rsz, raw in secs:
            if va <= rva < va + max(vsz, rsz):
                return rva - va + raw
        raise ValueError(hex(rva))

    def cstr(o):
        return d[o:d.index(b"\0", o)].decode("latin1")

    out, p = {}, off(imp_rva)
    while True:
        ilt, _, _, name, iat = struct.unpack_from("<5I", d, p)
        if name == 0:
            return out
        q, funcs = off(ilt or iat), []
        while (e := struct.unpack_from("<I", d, q)[0]):
            funcs.append(f"#{e & 0xFFFF}" if e & 0x80000000 else cstr(off(e) + 2))
            q += 4
        out[cstr(off(name))] = funcs
        p += 20

if __name__ == "__main__":
    for dll, fs in imports(sys.argv[1]).items():
        print(f"{dll} ({len(fs)}): {', '.join(fs)}")
