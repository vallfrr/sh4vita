"""Exercise actual Vita/Win32 message functions against a simulated SDK.

The system compositor and real pad still need hardware qualification. Character
encoding is checked independently against Python's Windows-1252 decoder.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
backend = (root / 'port/src/vita/dialog.c').read_text(encoding='utf-8')
backend = backend[backend.index('int plat_message_box('):]
frontend = (root / 'port/src/win32/user32.c').read_text(encoding='utf-8')
frontend = frontend[frontend.index('U(MessageBoxA) {'):frontend.index('U(PeekMessageA)')]
head = r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cp1252.h"
typedef struct { unsigned args[5],result,error; } CPU;
static unsigned char memory[128];
#define U(name) void imp_##name(CPU *c)
#define HOSTFN (void)0
#define ARG(n) c->args[n]
static const char *guest_string(unsigned a) { return a ? (const char *)memory+a : NULL; }
#define GSTR(a) guest_string(a)
#define STDRET(n,v) do{c->result=(v);return;}while(0)
#define SCE_MSG_DIALOG_BUTTON_TYPE_OK 0
#define SCE_MSG_DIALOG_MODE_USER_MSG 1
#define SCE_COMMON_DIALOG_STATUS_RUNNING 1
#define SCE_COMMON_DIALOG_RESULT_OK 0
#define SCE_MSG_DIALOG_BUTTON_ID_OK 1
#define GL_TRUE 1
typedef struct { int buttonType;const char *msg; } SceMsgDialogUserMessageParam;
typedef struct { int mode;SceMsgDialogUserMessageParam *userMsgParam; } SceMsgDialogParam;
typedef struct { int result,buttonId; } SceMsgDialogResult;
static int init_error,read_error,term_error,button=1,result,syncs,swaps,closes,terms,test;
static char captured[6144];
static void rt_log(const char *fmt,...) { (void)fmt; }
static void w32_set_last_error(CPU *c,unsigned error) { c->error=error; }
static char *test_env(const char *name) { assert(!strcmp(name,"SH4_MESSAGE_TEST"));return test?"1":NULL; }
static void q_sync(const char *why) { assert(!strcmp(why,"message-box"));syncs++; }
static void sceMsgDialogParamInit(SceMsgDialogParam *p) { memset(p,0,sizeof *p); }
static int sceMsgDialogInit(SceMsgDialogParam *p) {
 assert(p->mode==1 && !p->userMsgParam->buttonType);
 strcpy(captured,p->userMsgParam->msg);return init_error?-1:0;
}
static int sceMsgDialogGetStatus(void) { return (test?!closes:swaps<3)?1:2; }
static void vglSwapBuffers(int common) { assert(common==1);swaps++;assert(swaps<=120); }
static int sceMsgDialogClose(void) { closes++;return 0; }
static int sceMsgDialogGetResult(SceMsgDialogResult *r) { r->result=result;r->buttonId=button;return read_error?-1:0; }
static int sceMsgDialogTerm(void) { terms++;return term_error?-1:0; }
#define getenv test_env
'''
tail = r'''
#undef getenv
static void reset(void) {
 init_error=read_error=term_error=result=swaps=syncs=closes=terms=test=0;button=1;
}
int main(int argc,char **argv) {
 assert(argc==2);CPU c={0};strcpy((char *)memory+1,"SH4");strcpy((char *)memory+40,"Erreur m\xe9moire");
 c.args[2]=40;c.args[3]=1;reset();imp_MessageBoxA(&c);
 assert(c.result==1 && swaps==3 && syncs==1 && terms==1);
 assert(!strcmp(captured,"SH4\n\nErreur m\xc3\xa9moire"));
 reset();init_error=1;imp_MessageBoxA(&c);assert(!c.result && c.error==31 && !swaps && !terms);
 reset();read_error=1;imp_MessageBoxA(&c);assert(!c.result && terms==1);
 reset();term_error=1;imp_MessageBoxA(&c);assert(!c.result);
 reset();result=1;imp_MessageBoxA(&c);assert(!c.result);
 reset();button=0;imp_MessageBoxA(&c);assert(!c.result);
 reset();c.args[4]=4;imp_MessageBoxA(&c);assert(!c.result && !syncs && !terms);
 reset();c.args[4]=0;test=1;imp_MessageBoxA(&c);assert(swaps==120 && closes==1 && terms==1);
 reset();assert(plat_message_box(NULL,NULL,0)==1);
 unsigned char all[256];for(unsigned i=0;i<255;i++)all[i]=(unsigned char)(i+1);all[255]=0;
 FILE *f=fopen(argv[1],"wb");assert(f);
 for(unsigned cap=0;cap<=800;cap++) {
  unsigned char out[810];memset(out,0xa5,sizeof out);
  size_t n=rt_cp1252_utf8((char *)out+1,cap,(char *)all);
  assert(!n || n<cap);assert(out[0]==0xa5 && out[cap+1]==0xa5);
  if(cap)assert(out[n+1]==0);
  uint32_t count=(uint32_t)n;fwrite(&count,4,1,f);fwrite(out+1,1,n,f);
 }
 fclose(f);puts("message box: OK, failed/aborted/unsupported dialogs, diagnostic close and UTF-8 bounds passed");
}
'''
compiler = os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
with tempfile.TemporaryDirectory(prefix='sh4-message-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(head + backend + frontend + tail, encoding='utf-8')
    subprocess.run([compiler, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'port/src/rt'), str(path / 'test.c'),
                    '-o', str(path / 'test.exe')], check=True)
    subprocess.run([str(path / 'test.exe'), str(path / 'encoding.bin')], check=True)
    data = (path / 'encoding.bin').read_bytes()
    full = bytes(range(1, 256)).decode('cp1252', errors='replace')
    offset = 0
    for capacity in range(801):
        count = int.from_bytes(data[offset:offset + 4], 'little')
        offset += 4
        text = data[offset:offset + count].decode('utf-8', errors='strict')
        offset += count
        expected = ''
        for ch in full:
            if len((expected + ch).encode('utf-8')) >= capacity:
                break
            expected += ch
        assert text == expected, capacity
    assert offset == len(data)
    print('All 255 non-NUL CP1252 bytes and 801 clipping capacities match Python Unicode conversion.')
