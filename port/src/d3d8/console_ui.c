// Native menus use the existing D3D prep queue. No GL context or shader is
// managed here, and every modified device field is restored before returning.
#include "console_ui.h"
#include "prep.h"
#include <math.h>
#include <stdlib.h>

typedef struct { float x,y,z,rhw; uint32_t color; float u,v; } UiVertex;
static Texture *atlas;
static uint32_t vertices;
static Device saved;
static int active;
static int attempted;
static Texture *native_target;
static Surface *frame_rt,*frame_ds;
static unsigned frame_vp[6];
static int native_frame;
static UiVertex glyph_batch[64*6];
static unsigned glyph_count;
static Texture *glyph_texture;
static void flush_glyphs(void);
extern uint64_t g_mem_tex;

static int load(void) {
    if (atlas) return 1;
    if (attempted) return 0;
    attempted=1;
#ifdef __vita__
    const char *path="app0:ui/controls.tga";
#else
    const char *path="../../port/assets/controls/controls.tga";
    const char *override=getenv("SH4_UI_ASSETS"); if (override) path=override;
#endif
    FILE *f=fopen(path,"rb"); unsigned char h[18];
    if (!f) { rt_log("[controls] cannot open %s\n",path); return 0; }
    int valid=fread(h,1,18,f)==18 && !h[0] && !h[1] && h[2]==2 &&
        h[12]==0 && h[13]==4 && h[14]==0 && h[15]==2 && h[16]==32 && h[17]==0x28;
    if (!valid) { fclose(f); rt_log("[controls] invalid atlas header\n"); return 0; }
    uint32_t mem=gheap_alloc(1024*512*4), vb=gheap_alloc(sizeof glyph_batch);
    if (!mem || !vb) { if(mem)gheap_free(mem);if(vb)gheap_free(vb);fclose(f);return 0; }
    int ok=fread(g_mem+mem,1,1024*512*4,f)==1024*512*4 && fgetc(f)==EOF && !ferror(f);
    fclose(f);
    if (!ok) { gheap_free(mem);gheap_free(vb);rt_log("[controls] incomplete atlas\n");return 0; }
    // This texture is private to the host UI: no guest COM wrappers needed.
    // Allocate all host ownership nodes with checks before publishing it.
    Texture*t=calloc(1,sizeof*t);Surface*s=calloc(1,sizeof*s);Surface**lv=calloc(1,sizeof*lv);
    if(!t||!s||!lv){free(t);free(s);free(lv);gheap_free(mem);gheap_free(vb);return 0;}
    t->com.kind=K_TEXTURE;t->com.refs=1;t->w=1024;t->h=512;t->levels=1;t->fmt=FMT_A8R8G8B8;t->pool=1;
    t->lv=lv;lv[0]=s;s->tex=t;s->w=1024;s->h=512;s->fmt=FMT_A8R8G8B8;s->mem=mem;
    atlas=t;atlas->dirty=1;g_mem_tex+=1024*512*4;vertices=vb;
    rt_log("[controls] atlas loaded (2048 KiB); released on exit\n");return 1;
}

int console_ui_begin(void) {
    if (!g_dev || active || !load()) return 0;
    Device *d=g_dev;
    extern int g_vita_bindings_open;
    if(g_vita_bindings_open&&!native_frame) {
        if(!native_target) {
            Texture*t=calloc(1,sizeof*t);Surface*s=calloc(1,sizeof*s);Surface**lv=calloc(1,sizeof*lv);
            if(!t||!s||!lv){free(t);free(s);free(lv);return 0;}
            t->com.kind=K_TEXTURE;t->com.refs=1;t->w=960;t->h=544;t->levels=1;t->usage=1;t->fmt=FMT_A8R8G8B8;t->dirty=1;
            t->lv=lv;lv[0]=s;s->tex=t;s->w=960;s->h=544;s->usage=1;s->fmt=FMT_A8R8G8B8;
            native_target=t;
        }
        frame_rt=d->rt;frame_ds=d->ds;memcpy(frame_vp,&d->vp,sizeof frame_vp);
        Surface*target=native_target->lv[0],*depth=NULL;
        prep_set(&d->rt,&target,sizeof target);prep_set(&d->ds,&depth,sizeof depth);
        __typeof__(d->vp) vp={0,0,960,544,0,1};prep_set(&d->vp,&vp,sizeof vp);prep_bind();native_frame=1;
    }
    saved=*d; active=1;
    uint32_t rs[256]={0},tss[MAX_STAGES][32]={{0}};
    rs[19]=5;rs[20]=6;rs[22]=1;rs[27]=1;rs[168]=15;rs[171]=1;
    tss[0][1]=4;tss[0][2]=2;tss[0][3]=0;tss[0][4]=4;tss[0][5]=2;tss[0][6]=0;
    tss[0][13]=tss[0][14]=3;tss[0][16]=tss[0][17]=2;
    for(unsigned i=1;i<MAX_STAGES;i++)tss[i][1]=1;
    prep_set(d->rs,rs,sizeof rs);prep_set(d->tss,tss,sizeof tss);
    PSET(vs,0x144);PSET(ps,0);
    return 1;
}

static void quad(float x,float y,float w,float h,float u,float v,float uw,float vh,uint32_t color,int solid) {
    if(!active)return;
    flush_glyphs();
    Device *d=g_dev;
    // Use the current target/viewport, just as the surrounding game UI does.
    float sx=d->vp.w/960.0f,sy=d->vp.h/544.0f;
    x=d->vp.x+x*sx-.5f;y=d->vp.y+y*sy-.5f;w*=sx;h*=sy;
    UiVertex a={x,y,0,1,color,u,v},b={x+w,y,0,1,color,u+uw,v},
        e={x+w,y+h,0,1,color,u+uw,v+vh},f={x,y+h,0,1,color,u,v+vh};
    UiVertex q[]={a,b,e,a,e,f};
    memcpy(g_mem+vertices,q,sizeof q);
    Texture *tex=solid?NULL:atlas;
    prep_set(&d->tex[0],&tex,sizeof tex);
    uint32_t op=solid?2:4,arg=solid?0:2;
    prep_set(&d->tss[0][1],&op,4);prep_set(&d->tss[0][2],&arg,4);
    prep_set(&d->tss[0][4],&op,4);prep_set(&d->tss[0][5],&arg,4);
    prep_draw(d,4,0,6,2,NULL,0,0,g_mem+vertices,sizeof(UiVertex));
    // The queue copies this small UP draw before the next quad rewrites it.
}

void console_ui_sprite_color(int id,float x,float y,float w,float h,uint32_t color) {
    if(id==UI_VITA)quad(x,y,w,h,0,0,1,465.4545f/512,color,0);
    else if(id>=0 && id<=UI_DPAD)quad(x,y,w,h,(id*64+8)/1024.0f,464/512.0f,48/1024.0f,48/512.0f,color,0);
}
void console_ui_sprite(int id,float x,float y,float w,float h) { console_ui_sprite_color(id,x,y,w,h,0xffffffff); }
void console_ui_fill(float x,float y,float w,float h,uint32_t color) { quad(x,y,w,h,0,0,0,0,color,1); }
void console_ui_glyph(Texture*t,const float p[4],const float uv[4],uint32_t top,uint32_t bottom) {
    if(!active||!t)return;
    if(glyph_count&&(glyph_texture!=t||glyph_count==64))flush_glyphs();
    glyph_texture=t;
    Device*d=g_dev;float sx=d->vp.w/960.0f,sy=d->vp.h/544.0f;
    float x0=p[0]*sx-.5f,y0=p[1]*sy-.5f,x1=p[2]*sx-.5f,y1=p[3]*sy-.5f;
    float u0=uv[0]/t->w,v0=uv[1]/t->h,u1=uv[2]/t->w,v1=uv[3]/t->h;
    UiVertex a={x0,y0,0,1,top,u0,v0},b={x1,y0,0,1,top,u1,v0},e={x1,y1,0,1,bottom,u1,v1},f={x0,y1,0,1,bottom,u0,v1};
    UiVertex q[]={a,b,e,a,e,f};memcpy(glyph_batch+glyph_count*6,q,sizeof q);glyph_count++;
}
static void flush_glyphs(void) {
    if(!glyph_count)return;
    Device*d=g_dev;Texture*t=glyph_texture;
    memcpy(g_mem+vertices,glyph_batch,glyph_count*6*sizeof(UiVertex));
    prep_set(&d->tex[0],&t,sizeof t);uint32_t op=4,arg=2;
    prep_set(&d->tss[0][1],&op,4);prep_set(&d->tss[0][2],&arg,4);
    prep_set(&d->tss[0][4],&op,4);prep_set(&d->tss[0][5],&arg,4);
    prep_draw(d,4,0,glyph_count*6,glyph_count*2,NULL,0,0,g_mem+vertices,sizeof(UiVertex));
    glyph_count=0;glyph_texture=NULL;
}
void console_ui_line(float x1,float y1,float x2,float y2,uint32_t color) {
    // Thin diagonal quads; reuse the exact same textured-vertex shader layout.
    if(!active)return;
    flush_glyphs();
    Device*d=g_dev;float dx=x2-x1,dy=y2-y1,len=sqrtf(dx*dx+dy*dy);if(len<.01f)return;
    float nx=-dy/len*.80f,ny=dx/len*.80f,sx=d->vp.w/960.0f,sy=d->vp.h/544.0f;
    UiVertex a={(x1+nx)*sx+d->vp.x-.5f,(y1+ny)*sy+d->vp.y-.5f,0,1,color,0,0},
        b={(x2+nx)*sx+d->vp.x-.5f,(y2+ny)*sy+d->vp.y-.5f,0,1,color,0,0},
        e={(x2-nx)*sx+d->vp.x-.5f,(y2-ny)*sy+d->vp.y-.5f,0,1,color,0,0},
        f={(x1-nx)*sx+d->vp.x-.5f,(y1-ny)*sy+d->vp.y-.5f,0,1,color,0,0};
    UiVertex q[]={a,b,e,a,e,f};memcpy(g_mem+vertices,q,sizeof q);
    Texture*t=NULL;uint32_t op=2,arg=0;
    prep_set(&d->tex[0],&t,sizeof t);prep_set(&d->tss[0][1],&op,4);prep_set(&d->tss[0][2],&arg,4);
    prep_set(&d->tss[0][4],&op,4);prep_set(&d->tss[0][5],&arg,4);
    prep_draw(d,4,0,6,2,NULL,0,0,g_mem+vertices,sizeof(UiVertex));
}
void console_ui_end(void) {
    if(!active)return;
    flush_glyphs();
    Device*d=g_dev;
    prep_set(d->rs,saved.rs,sizeof d->rs);prep_set(d->tss,saved.tss,sizeof d->tss);
    prep_set(&d->tex[0],&saved.tex[0],sizeof d->tex[0]);PSET(vs,saved.vs);PSET(ps,saved.ps);active=0;
}
void console_ui_release(void) {
    console_ui_end();
    console_ui_finish_frame();
    if(g_dev){Surface*none=NULL;prep_set(&g_dev->ui_display,&none,sizeof none);}
    if(native_target){d3d_release(&native_target->com);native_target=NULL;}
    if(atlas){d3d_release(&atlas->com);atlas=NULL;}
    if(vertices){gheap_free(vertices);vertices=0;}
    attempted=0;
}
void console_ui_font_scale(float*x,float*y) {
    *x=native_frame&&frame_vp[2]?960.0f/frame_vp[2]:1;
    *y=native_frame&&frame_vp[3]?544.0f/frame_vp[3]:1;
}
void console_ui_finish_frame(void) {
    if(!g_dev)return;
    Device*d=g_dev;Surface*display=NULL;
    if(native_frame) {
        display=native_target->lv[0];
        prep_set(&d->rt,&frame_rt,sizeof frame_rt);prep_set(&d->ds,&frame_ds,sizeof frame_ds);
        prep_set(&d->vp,frame_vp,sizeof frame_vp);prep_bind();native_frame=0;
    }
    prep_set(&d->ui_display,&display,sizeof display);
}
