"""Failure reporting and cross-frame options acceptance use production wrappers."""
from pathlib import Path
import os, shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
head=(root/'port/test/options_test.c').read_text(encoding='utf-8').split('int main(')[0]
head=head.replace('unsigned rt_save_failure_serial(void) { return 0; }','')
head=head.replace('uint32_t rt_save_error_code(void) { return 0; }','')
head=head.replace('void rt_save_report_error(unsigned language, uint32_t error) { assert(!"unexpected save error"); }','')
head=head.replace('void rt_log(const char *fmt, ...) { (void)fmt; }','void rt_log(const char *fmt, ...) { (void)fmt; errno=EACCES; }')
head=head.replace('void F_00526810_orig(CPU *c) { c->eax = 1; c->esp += 4; }',r'''
static int dispatch_mode, options_mode;
void nat_savedispatch(CPU *c);
void w32_set_last_error(CPU *c,uint32_t error) { uint8_t *mb=g_mem;W32(c->fs_base+0x34,error); }
void F_00408960_orig(CPU *c) {
 if(dispatch_mode==1) { rt_save_failed(c,112);c->eax=0; }
 else c->eax=dispatch_mode==2?0:1;
 c->esp+=4;
}
void F_00526810_orig(CPU *c) {
 if(options_mode==1) { nat_savedispatch(c);c->eax=0; }
 else { c->eax=1;c->esp+=4; }
}
''')
head='#include <errno.h>\n#include "input_gate.h"\n'+head
tail=r'''
static int stages, stage_fail, messages;
static char last_message[512];
int rt_staged_file_write(const char *path,const void *data,size_t length) {
 (void)path;(void)data;assert(length==2);++stages;
 if(stage_fail){errno=ENOSPC;return 0;}return 1;
}
int plat_message_box(const char *title,const char *text,unsigned flags) {
 assert(!strcmp(title,"Silent Hill 4") && flags==0x10);++messages;
 snprintf(last_message,sizeof last_message,"%s",text);return 1;
}
int main(void) {
 g_mem=calloc(1,32u<<20);assert(g_mem);uint8_t *mb=g_mem;cpu.fs_base=0x10000;
 W8(0x1091ad0+0x11,2);
 // A failure inside the writer must be recorded once, without altering ABI.
 unsigned before=rt_save_failure_serial();dispatch_mode=1;call(nat_savedispatch);
 assert(!cpu.eax && rt_save_failure_serial()==before+1 && rt_save_error_code()==112);
 // A failed read/modify step before the writer still reports failure.
 dispatch_mode=2;W32(cpu.fs_base+0x34,5);call(nat_savedispatch);
 assert(!cpu.eax && rt_save_failure_serial()==before+2 && rt_save_error_code()==5);
 dispatch_mode=0;call(nat_savedispatch);assert(cpu.eax==1 && rt_save_failure_serial()==before+2);
 // GOG completes the dialog one frame after its ignored write result.
 call(nat_optbegin);change(8,1);dispatch_mode=1;options_mode=1;
 call(nat_optsave);assert(messages==1 && !stages && strstr(last_message,"espace"));
 options_mode=0;call(nat_optsave);assert(messages==1 && !stages);rt_camera_end();value(3);
 // A later successful acceptance clears the failure latch and persists camera.
 call(nat_optbegin);change(8,1);call(nat_optsave);assert(stages==1 && messages==1);
 rt_camera_end();value(4);
 // Private camera-file failure preserves the prior value and errno before logging.
 call(nat_optbegin);change(8,1);stage_fail=1;call(nat_optsave);
 assert(messages==2 && strstr(last_message,"espace"));call(nat_optsave);
 assert(stages==2 && messages==2);rt_camera_end();value(4);
 for(unsigned language=0;language<=6;language++)rt_save_report_error(language,29);
 assert(g_input_neutral_generation==(unsigned)messages);
 free(g_mem);puts("save errors: inner/outer failures, unchanged ABI, cross-frame latch, retry, private-config failure, localized dialogs and post-modal neutralization passed");
}
'''
with tempfile.TemporaryDirectory(prefix='sh4-save-ui-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(head+tail,encoding='utf-8')
    cc=os.environ.get('CC') or shutil.which('gcc') or 'J:/tools/mingw64/bin/gcc.exe'
    subprocess.run([cc,'-std=gnu11','-O2','-I'+str(root/'port/src/rt'),'-I'+str(root/'port/src'),
                    '-I'+str(root.parent/'tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32/include/SDL2'),
                    str(p/'test.c'),str(root/'port/src/rt/options.c'),str(root/'port/src/rt/save_error.c'),
                    str(root/'port/src/rt/input_gate.c'),'-lm','-o',str(p/'test.exe')],check=True)
    subprocess.run([str(p/'test.exe')],check=True)
