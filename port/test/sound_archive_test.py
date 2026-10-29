"""Compare the range reader against the player's unmodified sound.pac."""
from pathlib import Path
import argparse,ctypes,random,struct,subprocess,tempfile

root=Path(__file__).resolve().parents[2]
class Entry(ctypes.Structure):
    _fields_=[('offset',ctypes.c_uint32),('bytes',ctypes.c_uint32),('opaque',ctypes.c_uint32)]
class Bank(ctypes.Structure):
    _fields_=[('file_bytes',ctypes.c_uint32),('entries',Entry*355)]
class Cache(ctypes.Structure):
    _fields_=[('bank',ctypes.POINTER(Bank)),('storage',ctypes.c_void_p),('block',ctypes.c_uint32*8),('stamp',ctypes.c_uint32*8),('clock',ctypes.c_uint32),('valid',ctypes.c_uint32)]
Read=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--archive',type=Path,default=root/'build/pc/game/sound/sound.pac')
    args=ap.parse_args();original=args.archive.read_bytes();active=bytearray(original)
    failed_offset=None;calls=0
    @Read
    def read(ctx,offset,dst,size):
        nonlocal calls
        calls+=1
        if offset==failed_offset:
            # Backends may partially fill a block before reporting an I/O error.
            ctypes.memmove(dst,b'\xa7'*min(size,16),min(size,16));return 0
        if offset+size>len(active):return 0
        ctypes.memmove(dst,bytes(active[offset:offset+size]),size);return 1
    with tempfile.TemporaryDirectory(prefix='sh4-sound-archive-') as tmp:
        dll=Path(tmp)/'archive.dll'
        subprocess.run(['gcc','-shared','-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'port/src/rt/sound_archive.c'),str(root/'port/src/rt/sound_archive_cache.c'),'-o',str(dll)],check=True)
        lib=ctypes.CDLL(str(dll))
        lib.sh4_sound_archive_open.argtypes=[ctypes.POINTER(Bank),Read,ctypes.c_void_p,ctypes.c_uint32]
        lib.sh4_sound_archive_read.argtypes=[ctypes.POINTER(Bank),Read,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32]
        lib.sh4_sound_cache_init.argtypes=[ctypes.POINTER(Cache),ctypes.POINTER(Bank),ctypes.c_void_p,ctypes.c_uint32]
        lib.sh4_sound_cache_read.argtypes=[ctypes.POINTER(Cache),Read,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32]
        bank=Bank();assert lib.sh4_sound_archive_open(ctypes.byref(bank),read,None,len(original))
        storage=ctypes.create_string_buffer(8*65536+32);ctypes.memset(storage,0xa5,len(storage));cache=Cache()
        assert not lib.sh4_sound_cache_init(ctypes.byref(cache),ctypes.byref(bank),ctypes.addressof(storage)+16,8*65536-1)
        assert lib.sh4_sound_cache_init(ctypes.byref(cache),ctypes.byref(bank),ctypes.addressof(storage)+16,8*65536)
        snapshot=bytes(bank);padded=original+b'\0'*((-len(original))%4096);checked=0
        def compare(offset,size):
            nonlocal checked
            out=ctypes.create_string_buffer(size+32);ctypes.memset(out,0xa5,size+32)
            assert lib.sh4_sound_archive_read(ctypes.byref(bank),read,None,offset,ctypes.addressof(out)+16,size)
            assert out.raw[:16]==b'\xa5'*16 and out.raw[-16:]==b'\xa5'*16
            assert out.raw[16:16+size]==padded[offset:offset+size]
            ctypes.memset(out,0xa5,size+32)
            assert lib.sh4_sound_cache_read(ctypes.byref(cache),read,None,offset,ctypes.addressof(out)+16,size)
            assert out.raw[:16]==b'\xa5'*16 and out.raw[-16:]==b'\xa5'*16
            assert out.raw[16:16+size]==padded[offset:offset+size]
            assert storage.raw[:16]==b'\xa5'*16 and storage.raw[-16:]==b'\xa5'*16
            checked+=1
        for i,e in enumerate(bank.entries):
            assert (e.offset,e.bytes,e.opaque)==struct.unpack_from('<III',original,8+12*i)
            compare(e.offset,e.bytes)
            # The original resident mapping allows reads across entry boundaries.
            compare(e.offset+e.bytes-16,min(512,len(padded)-(e.offset+e.bytes-16)))
            p=e.offset+12
            while p+8<=e.offset+e.bytes:
                tag=original[p:p+4];size=struct.unpack_from('<I',original,p+4)[0]
                if tag==b'data':compare(p+8,size);break
                p+=8+size+(size&1)
            else:raise AssertionError(f'entry {i} has no data chunk')
        rng=random.Random(0x408ac0)
        for _ in range(10000):
            offset=rng.randrange(len(padded)+1);size=rng.randrange(min(4096,len(padded)-offset)+1);compare(offset,size)
        compare(len(original),len(padded)-len(original));compare(len(padded),0)
        bad=ctypes.create_string_buffer(b'guard')
        for off,size in [(len(padded)+1,0),(len(padded),1),(0xffffffff,2),(1,0xffffffff)]:
            assert not lib.sh4_sound_archive_read(ctypes.byref(bank),read,None,off,bad,size)
            assert bad.value==b'guard'
        # Malformed and truncated directories must not change the bank object.
        mutations=[(0,b'BAD!'),(4,struct.pack('<I',354)),(8,struct.pack('<I',0xffffffff)),(12,struct.pack('<I',0xffffffff))]
        for pos,value in mutations:
            active[:]=original;active[pos:pos+4]=value
            assert not lib.sh4_sound_archive_open(ctypes.byref(bank),read,None,len(original));assert bytes(bank)==snapshot
        active[:]=original
        for size in [0,4267,len(original)-1,0xffffffff]:
            assert not lib.sh4_sound_archive_open(ctypes.byref(bank),read,None,size);assert bytes(bank)==snapshot
        for failed_offset in [0,8]:
            assert not lib.sh4_sound_archive_open(ctypes.byref(bank),read,None,len(original));assert bytes(bank)==snapshot
        failed_offset=bank.entries[0].offset
        assert not lib.sh4_sound_archive_read(ctypes.byref(bank),read,None,failed_offset,bad,4)
        # Repeated cache hits must not touch the backing file, even on clock wrap.
        failed_offset=None;compare(100,100);before=calls
        for _ in range(100):assert lib.sh4_sound_cache_read(ctypes.byref(cache),read,None,100,bad,4)
        assert calls==before
        cache.clock=0xffffffff;compare(100,100);compare(65536,100)
        # A failed miss is invalidated, so retry must invoke the source again.
        assert lib.sh4_sound_cache_init(ctypes.byref(cache),ctypes.byref(bank),ctypes.addressof(storage)+16,8*65536)
        failed_offset=0;assert not lib.sh4_sound_cache_read(ctypes.byref(cache),read,None,100,bad,4)
        assert cache.valid==0;failed_offset=None;before=calls
        assert lib.sh4_sound_cache_read(ctypes.byref(cache),read,None,100,bad,4) and calls==before+1
        assert bad.raw[:4]==original[100:104]
        print(f'{checked} exact ranges passed with/without 512 KiB cache; all 355 entries, cross-entry PCM, zero tail, malformed input, cache hits and I/O retry covered')
        import _ctypes,os
        if os.name=='nt':_ctypes.FreeLibrary(lib._handle)
        else:_ctypes.dlclose(lib._handle)

if __name__=='__main__':main()
