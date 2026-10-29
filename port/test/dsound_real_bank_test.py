"""Mix every PCM effect from the user's bank with current and previous mixers.

Host differential check only; it is not Vita playback or streaming-audio proof.
"""
from pathlib import Path
import subprocess,tempfile

root=Path(__file__).resolve().parents[2]
# Reuse the differential fixture's extraction/stubs without running its main.
fixture=root/'port/test/dsound_mix_scratch_test.py'
prefix=fixture.read_text().split('\ntest=r',1)[0]
context={'__file__':str(fixture)}
exec(compile(prefix,str(fixture),'exec'),context)
head=context['head'].replace('memory[1<<16]','memory[32<<20]')
head=head.replace('for(int i=0;i<n*2;i++)p[i]+=(i%17-8)*0.001f;','(void)p;(void)n;')
code=head+context['helpers']
for name,text in [('original_mix',context['old']),('candidate_mix',context['s'])]:
    code+=context['mixer'](text).replace('static void mix(',f'static void {name}(')
code+=r'''
static uint32_t le(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
 size_t length=fread(memory,1,sizeof memory,f);assert(!ferror(f)&&feof(f));fclose(f);
 assert(length>4268&&!memcmp(memory,"SDPA",4)&&le(memory+4)==355);
 uint64_t calls=0,bytes=0;unsigned crossing=0;uint8_t a[4096],b[4096];
 for(unsigned id=0;id<355;id++){
  uint32_t off=le(memory+8+12*id),n=le(memory+12+12*id);assert(off<=length&&n<=length-off);
  const uint8_t *p=memory+off;assert(n>=12&&!memcmp(p,"RIFF",4)&&!memcmp(p+8,"WAVE",4));
  uint32_t start=0,size=0;unsigned format=0,channels=0,rate=0,align=0,bits=0;
  for(uint32_t cursor=12;cursor+8<=n;){
   uint32_t chunk=le(p+cursor+4);
   /* Some original WAV data declarations cross the SDPA entry boundary.
      Preserve the immutable resident-bank layout and its zero-filled final page.
      Report this explicitly rather than repairing source data in the test. */
   assert(chunk<=((length+4095u)&~4095u)-off-cursor-8);
   if(!memcmp(p+cursor,"data",4)){start=off+cursor+8;size=chunk;if(chunk>n-cursor-8)crossing++;break;}
   assert(chunk<=n-cursor-8);
   if(!memcmp(p+cursor,"fmt ",4)){assert(chunk>=16);const uint8_t *v=p+cursor+8;format=v[0]|v[1]<<8;channels=v[2]|v[3]<<8;rate=le(v+4);align=v[12]|v[13]<<8;bits=v[14]|v[15]<<8;}
   cursor+=8+chunk+(chunk&1);
  }
  assert(format==1&&channels==1&&rate==22050&&align==2&&bits==16&&start&&size%2==0);
  DSBuf oldbuf={0},newbuf={0};oldbuf.mem=start;oldbuf.size=size;oldbuf.ch=1;oldbuf.rate=rate;oldbuf.bits=bits;oldbuf.align=align;oldbuf.playing=1;newbuf=oldbuf;
  unsigned limit=0;
  while(oldbuf.playing||newbuf.playing){
   assert(++limit<100000);bufs=&oldbuf;original_mix(NULL,a,sizeof a);bufs=&newbuf;candidate_mix(NULL,b,sizeof b);
   assert(!memcmp(a,b,sizeof a));assert(!memcmp(&oldbuf,&newbuf,sizeof oldbuf));calls++;bytes+=sizeof a;
  }
 }
 printf("355 declared PCM payloads mixed; %llu callback pairs / %llu output bytes identical; %u original cross-entry data declarations retained within bank; host only, no Vita playback claim\n",(unsigned long long)calls,(unsigned long long)bytes,crossing);
}
'''
with tempfile.TemporaryDirectory() as temp:
    path=Path(temp);(path/'test.c').write_text(code);exe=path/'test.exe'
    subprocess.run([str(root.parent/'tools/mingw64/bin/gcc.exe'),'-std=c11','-O2','-D__vita__',str(path/'test.c'),'-o',str(exe),'-lm'],check=True)
    subprocess.run([str(exe),str(root/'build/pc/game/sound/sound.pac')],check=True)
