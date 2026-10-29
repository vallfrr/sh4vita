// Opt-in laboratory playback through the GOG's common cinematic event path.
// This bypasses narrative triggers; it is not a substitute for a story replay.
#include "audit.h"
#include <SDL.h>
#include <stdlib.h>

extern unsigned g_frame;
extern void F_00553c70_orig(CPU *c);
static const char *schedule;
static unsigned initialized, last_frame=~0u, requested, ident, sequence;
static unsigned started, started_ticks, waiting_since, stable, phase, last_state=~0u, shot;
static unsigned shot_every=120;
static uint32_t object, work;

static void next_request(void) {
    if (!schedule || !*schedule) { requested=ident=0; return; }
    char *end;
    unsigned long frame=strtoul(schedule,&end,10);
    if (end==schedule || *end!=':' || !frame || frame>1000000)
        rt_fatal("invalid SH4_DEMO frame");
    schedule=end+1;
    unsigned long id=strtoul(schedule,&end,10);
    if (end==schedule || (*end && *end!=',') || !id || id>32767)
        rt_fatal("invalid SH4_DEMO ID");
    requested=(unsigned)frame; ident=(unsigned)id;
    waiting_since=requested>g_frame ? requested : g_frame;
    schedule=*end ? end+1 : end;
    stable=0;
}

static uint32_t common_driver(unsigned id, unsigned *stage) {
    HOSTFN;
    *stage=0;
    for (unsigned i=0;i<512;i++) {
        uint32_t p=0x650640+i*6;
        if (!R16(p)) break;
        if (R16(p)==id) { *stage=R8(p+4); break; }
    }
    if (!*stage || *stage>10) return 0;
    // Only drivers with the audited common init/exec pair. Custom event code
    // needs its own setup and must never be guessed from an ID alone.
    for (uint32_t p=0x5b7000;p+44<=0x6a3000;p+=4) {
        if (R32(p+8)!=0x4cd170 || R32(p+12)!=0x4cd1b0 || R32(p+28)!=id) continue;
        uint32_t files=R32(p+24);
        if (!files || files<0x5b7000 || files>0x6a3000-4) continue;
        for (unsigned n=0;n<64 && files+n*4<=0x6a3000-4;n++) {
            uint32_t f=R32(files+n*4);
            if (f==0x0fffffff) return p;
            if ((f&0xf000f000)!=0xf000f000) break;
        }
    }
    return 0;
}

static void pump(CPU *c) {
    HOSTFN;
    if (!initialized) {
        initialized=1;
        schedule=getenv("SH4_DEMO");
        if (schedule && *schedule) {
            if (!g_audit_enabled) rt_fatal("SH4_DEMO requires SH4_AUDIT=1");
            const char *e=getenv("SH4_DEMO_SHOT_EVERY");
            if (e) {
                char *end;unsigned long v=strtoul(e,&end,10);
                if (!*e || *end || (v && v<30) || v>3000) rt_fatal("invalid SH4_DEMO_SHOT_EVERY");
                shot_every=(unsigned)v;
            }
            next_request();
            rt_log("[cinematic] event=config mode=common-event narrative=unverified\n");
        }
    }
    if (!requested || last_frame==g_frame) return;
    last_frame=g_frame;
    if (!phase) {
        if (g_frame<requested) return;
        int ready=R32(0xfd5a90)>=1 && R32(0xfd5a90)<=11 &&
            !R32(0xfd5aa0) && !R32(0xfd5aa4) && !R32(0xfd5c20) && !R32(0xfd5c28) &&
            (!R32(0x10774b8) || R8(0x10775ca) || R32(0x10774b4)>=4);
        stable=ready ? stable+1 : 0;
        if (!stable && (g_frame-requested)%300==0)
            rt_log("[cinematic] event=waiting id=%u frame=%u world=%u load=%u,%u pause=%u event=%u demo=%u state=%u clean=%u\n",
                   ident,g_frame,R32(0xfd5a90),R32(0xfd5aa0),R32(0xfd5aa4),R32(0xfd5c20),R32(0xfd5c28),
                   R32(0x10774b8),R32(0x10774b4),R8(0x10775ca));
        if (g_frame-waiting_since>1800) rt_fatal("cinematic %u: scene never ready",ident);
        if (stable<30) return;
        unsigned stage;
        uint32_t driver=common_driver(ident,&stage);
        if (!driver || stage!=R32(0xfd5a90)) {
            rt_log("[cinematic] event=refused id=%u frame=%u reason=%s stage=%u world=%u\n",
                   ident,g_frame,driver?"stage-mismatch":"custom-or-absent-driver",stage,R32(0xfd5a90));
            next_request(); return;
        }
        object=gheap_calloc(0x440); work=gheap_calloc(0xa0);
        if (!object || !work) rt_fatal("cinematic laboratory allocation failed");
        W32(object+0x420,work); W32(work,driver);
        memcpy(mb+work+4,mb+driver,44);
        rt_guest_call(c,0x4ccbf0,0); // stop normal control/HUD while resources load
        rt_guest_call(c,0x565ca0,1,0x80);
        started=g_frame; started_ticks=SDL_GetTicks(); phase=1; shot=0; sequence++;
        rt_log("[cinematic] event=begin index=%u id=%u frame=%u world=%u scene=%u driver=%x files=%x\n",
               sequence,ident,g_frame,R32(0xfd5a90),R32(0xfd5a94),driver,R32(driver+24));
    }
    if (g_frame-started>18000 || SDL_GetTicks()-started_ticks>600000)
        rt_fatal("cinematic %u: laboratory timeout phase=%u",ident,phase);
    if (phase==1) {
        // The real event's bounded file list and asynchronous preload state
        // machine, rather than starting a demo with missing actor textures.
        if (rt_guest_call(c,0x4cfbb0,1,object)) {
            rt_guest_call(c,0x4cd170,1,object);
            phase=2; last_state=~0u;
            rt_log("[cinematic] event=preloaded id=%u frame=%u load_ms=%u\n",ident,g_frame,SDL_GetTicks()-started_ticks);
        }
        return;
    }
    unsigned state=R32(0x10774b4);
    if (state!=last_state) {
        rt_log("[cinematic] event=state id=%u frame=%u state=%u actors=%u\n",ident,g_frame,state,R32(0x10774a8));
        last_state=state;
    }
    if (shot_every && state==3 && (shot==0 || (g_frame-started)%shot_every==0))
        rt_audit_request_capture(10000+sequence,shot++);
    if (rt_guest_call(c,0x4cd1b0,1,object)) {
        rt_guest_call(c,0x4cd2e0,0);
        rt_guest_call(c,0x4cfd10,1,object);
        rt_log("[cinematic] event=complete index=%u id=%u frame=%u elapsed_frames=%u elapsed_ms=%u shots=%u\n",
               sequence,ident,g_frame,g_frame-started,SDL_GetTicks()-started_ticks,shot);
        rt_log_flush();
        gheap_free(work);gheap_free(object);work=object=0;phase=0;
        next_request();
    }
}

void nat_cinematic_frame(CPU *c) {
    F_00553c70_orig(c);
    // No scheduling, allocation or guest writes in ordinary play. Only the
    // first getenv and this cached flag are paid when the laboratory is off.
    static int enabled=-1;
    if (enabled<0) { const char *e=getenv("SH4_DEMO");enabled=e && *e; }
    if (!enabled || c!=g_main_cpu) return;
    CPU saved=*c;
    pump(c);
    *c=saved;
}
