// Optional scheduler accounting, independent of wall time spent waiting for
// another stage of the renderer. No kernel plugin or sampling thread needed.
#include "rt.h"
#include <stdlib.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

typedef struct {
    int id;
    char label[24];
    uint64_t clock;
    unsigned preempt,release;
} CpuTime;
static CpuTime threads[32];
static int enabled=-1;
static double clocks_per_us;

static int cpu_enabled(void) {
    return __atomic_load_n(&enabled,__ATOMIC_ACQUIRE)>0;
}

int rt_cpu_profile_thread(const char *label) {
    int mode=__atomic_load_n(&enabled,__ATOMIC_ACQUIRE);
    if (mode<0) return 0; // the environment file has not been read yet
    if (!mode) return 1;
    int id=sceKernelGetThreadId();
    for (unsigned i=0;i<32;i++) if (__atomic_load_n(&threads[i].id,__ATOMIC_ACQUIRE)==id) return 1;
    for (unsigned i=0;i<32;i++) {
        int expected=0;
        if (!__atomic_compare_exchange_n(&threads[i].id,&expected,-1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        snprintf(threads[i].label,sizeof threads[i].label,"%s",label);
        threads[i].clock=0;threads[i].preempt=threads[i].release=0;
        __atomic_store_n(&threads[i].id,id,__ATOMIC_RELEASE);
        return 1;
    }
    return 1;
}

void rt_cpu_profile_init(void) {
    const char *setting=getenv("SH4_CPU_TIME");
    __atomic_store_n(&enabled,setting && atoi(setting)!=0,__ATOMIC_RELEASE);
    if (!cpu_enabled()) return;
    SceKernelThreadInfo a={.size=sizeof a},b={.size=sizeof b};
    int id=sceKernelGetThreadId();
    if (sceKernelGetThreadInfo(id,&a)<0) return;
    uint64_t begin=sceKernelGetSystemTimeWide(),end;
    do { end=sceKernelGetSystemTimeWide(); } while (end-begin<20000);
    // Force a scheduling boundary in case the run counter is updated lazily.
    sceKernelDelayThread(1000);
    if (sceKernelGetThreadInfo(id,&b)<0 || b.runClocks<=a.runClocks) return;
    clocks_per_us=(double)(b.runClocks-a.runClocks)/(double)(end-begin);
    rt_log("[cpu-calibration] busy_wall_us=%llu run_clocks=%llu clocks_per_us=%.6f arm_mhz=%d estimated=1\n",
           (unsigned long long)(end-begin),(unsigned long long)(b.runClocks-a.runClocks),clocks_per_us,scePowerGetArmClockFrequency());
    rt_cpu_profile_thread("game");
}

void rt_cpu_profile_report(double frames) {
    if (!cpu_enabled() || frames<=0) return;
    extern unsigned g_frame;
    static uint64_t previous;
    uint64_t now=sceKernelGetSystemTimeWide(),wall=previous ? now-previous : 0;
    previous=now;
    for (unsigned i=0;i<32;i++) {
        int id=__atomic_load_n(&threads[i].id,__ATOMIC_ACQUIRE);
        if (id<=0) continue;
        SceKernelThreadInfo info={.size=sizeof info};
        int error=sceKernelGetThreadInfo(id,&info);
        if (error<0) {
            rt_log("[cpu-thread] event=retired id=%x name=%s error=%x\n",id,threads[i].label,(unsigned)error);
            __atomic_store_n(&threads[i].id,0,__ATOMIC_RELEASE);continue;
        }
        if (wall && threads[i].clock && info.runClocks>=threads[i].clock) {
            uint64_t delta=info.runClocks-threads[i].clock;
            double ms=clocks_per_us ? (double)delta/clocks_per_us/1000.0/frames : -1;
            rt_log("[cpu-thread] frame=%u wall_us=%llu frames=%.0f name=%s id=%x core=%d affinity=%x run_clocks=%llu cpu_ms_est=%.3f preempt=%u release=%u\n",
                   g_frame,(unsigned long long)wall,frames,threads[i].label,id,info.lastExecutedCpuId,info.currentCpuAffinityMask,
                   (unsigned long long)delta,ms,info.threadPreemptCount-threads[i].preempt,info.threadReleaseCount-threads[i].release);
        }
        threads[i].clock=info.runClocks;threads[i].preempt=info.threadPreemptCount;threads[i].release=info.threadReleaseCount;
    }
}
#else
void rt_cpu_profile_init(void) {}
int rt_cpu_profile_thread(const char *label) { (void)label;return 1; }
void rt_cpu_profile_report(double frames) { (void)frames; }
#endif
