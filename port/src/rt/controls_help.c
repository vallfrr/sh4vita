// Fixed Vita reference page in the original options lifecycle, not a remapper.
#include "rt.h"
#include "d3d8/console_ui.h"
#include <stdlib.h>
int g_vita_bindings_open;
typedef struct { float x,y,sx,sy; unsigned frame,valid; } GlyphPlacement;
static GlyphPlacement placements[800];
extern unsigned g_frame;
extern void F_00563d20_orig(CPU*),F_00559a10_orig(CPU*);
typedef struct {float p[4],uv[4];uint32_t top,bottom;} MenuGlyph;
static MenuGlyph glyphs[1600];static unsigned glyph_n,layout_frame;
static Texture*font_texture;
static unsigned page_frame;
extern void F_00526490_orig(CPU*);
void nat_controls_options_page(CPU*c) {
    page_frame=g_frame;
    F_00526490_orig(c);
}
static unsigned menu_fade(void) {
    HOSTFN;
    // F565ca0 publishes the original menu fade, 0=visible, 128=black.
    // Reuse it after the original menu update; never advance a second timer.
    int value=(int32_t)R32(0x137f98c);
    return value<=0?0:value>=128?255:(unsigned)value*2;
}
static uint32_t glyph_color(uint32_t c){uint32_t r=0;for(unsigned i=0;i<4;i++){unsigned v=((c>>(i*8))&255)*2;if(v>255)v=255;r|=v<<(i*8);}return r;}
void nat_controls_font_quad(CPU*c) {
    HOSTFN;uint32_t caller=R32(c->esp),base=0x125724c;
    if(g_vita_bindings_open&&caller>=0x559a10&&caller<0x559d90&&c->esi>=base&&(c->esi-base)%20==0&&(c->esi-base)/20<800&&glyph_n<1600){
        GlyphPlacement*p=&placements[(c->esi-base)/20];
        if(p->valid&&p->frame==g_frame){
            MenuGlyph*q=&glyphs[glyph_n++];
            for(unsigned i=0;i<4;i++){q->p[i]=((rt_f32(ARG(i+1))-(i&1?34.285714f:40.0f))*(i&1?p->sy:p->sx)+(i&1?p->y:p->x))*(i&1?544.0f/480:960.0f/640);q->uv[i]=rt_f32(ARG(i+5));}
            q->top=glyph_color(ARG(9));q->bottom=glyph_color(ARG(10));
        }
    }
    // Let the original font pipeline prepare its cache, buffers and textures.
    F_00563d20_orig(c);
}
void nat_controls_font_batch(CPU*c) {
    unsigned before=glyph_n;F_00559a10_orig(c);
    if(glyph_n!=before&&g_dev&&g_dev->tex[0]&&g_dev->tex[0]!=font_texture){
        if(font_texture)d3d_release(&font_texture->com);
        font_texture=g_dev->tex[0];d3d_addref(font_texture);
    }
}
static int enabled(void) {
    const char*v=getenv("SH4_CONSOLE_BINDINGS");if(v)return !strcmp(v,"1");
#ifdef __vita__
    return 1;
#else
    return 0;
#endif
}
extern void F_00518830_orig(CPU*),F_005176a0_orig(CPU*),F_00517e60_orig(CPU*),F_00517c30_orig(CPU*);
void nat_vita_bindings(CPU*c) {
    int was=g_vita_bindings_open;g_vita_bindings_open=enabled();
    if(g_vita_bindings_open&&!was)rt_log("[controls] reference page enter\n");
    F_00518830_orig(c);
    if(c->eax&&g_vita_bindings_open){g_vita_bindings_open=0;rt_log("[controls] reference page leave\n");}
}
void nat_vita_bindings_input(CPU*c) {
    if(!g_vita_bindings_open){F_00517e60_orig(c);return;}
    uint32_t exit=rt_guest_call(c,0x4f84a0,2,0,1);
    if(exit)rt_guest_call(c,0x56cc40,1,0x9c43);
    STDRET(0,exit!=0); // original Cancel edge; no capture/default/preset/save path
}
void nat_vita_bindings_title(CPU*c) {
    if(g_vita_bindings_open){STDRET(0,0);}F_00517c30_orig(c);
}

enum { VIEW,HELP,TITLE,RECENTER,AIM,SELECT_ITEM,DPAD,MOVE,LEFT_STICK,ITEMS,
       USE_CARRY,MAP,RUN,ACTION,CAMERA,RIGHT_STICK,ALBUM,PAUSE,INFO,EVADE,BACK,TEXT_COUNT };
static const char *const texts[5][TEXT_COUNT]={
 {"View controls","View the PS Vita button layout.","Controls","Recenter camera","Aim","Item","D-pad","Move","Left stick","Use /","select","Map / memo","Run / cancel","Action / examine","Camera","Right stick","Album","Pause","Item info","Dodge","Back"},
 {"Voir les commandes","Consultez les touches de la PS Vita.","Commandes","Recentrer caméra","Viser","Objet","Croix directionnelle","Déplacement","Stick gauche","Utiliser /","sélectionner","Plan / mémo","Courir / annuler","Action / examiner","Caméra","Stick droit","Album","Pause","Info objet","Esquiver","Retour"},
 {"Steuerung ansehen","Tastenbelegung der PS Vita ansehen.","Steuerung","Kamera zentrieren","Zielen","Objekt wählen","Steuerkreuz","Bewegen","Linker Stick","Benutzen /","auswaehlen","Karte / Notizen","Rennen / zurück","Aktion / prüfen","Kamera","Rechter Stick","Album","Pause","Objektinfo","Ausweichen","Zurück"},
 {"Vedi comandi","Consulta i comandi della PS Vita.","Comandi","Centra visuale","Mira","Scegli oggetto","Croce direzionale","Movimento","Stick sinistro","Usa /","seleziona","Mappa / memo","Corri / annulla","Azione / esamina","Visuale","Stick destro","Album","Pausa","Info oggetto","Schiva","Indietro"},
 {"Ver controles","Consulta los controles de PS Vita.","Controles","Centrar cámara","Apuntar","Elegir objeto","Cruceta","Moverse","Stick izquierdo","Usar /","seleccionar","Mapa / notas","Correr / volver","Acción / examinar","Cámara","Stick derecho","Álbum","Pausa","Info objeto","Esquivar","Volver"}
};
static uint32_t cached[5][TEXT_COUNT];
static uint32_t string(unsigned lang,unsigned key) {
    unsigned l=lang>=1&&lang<=5?lang-1:0;if(key>=TEXT_COUNT)key=TITLE;
    if(!cached[l][key]) {
        const unsigned char*s=(const unsigned char*)texts[l][key];
        uint32_t p=gheap_alloc((uint32_t)strlen((const char*)s)+4),at=p;
        if(!p)rt_fatal("guest memory exhausted while creating controls text");
        HOSTFN;W16(at,0x8000);at+=2;
        while(*s){unsigned ch=*s++,glyph;
            if(ch==0xc3&&*s)glyph=0xc0-*s++; // UTF-8 Latin-1 -> 0x100 - CP1252
            else glyph=ch==' '?0:0x100-ch;
            W8(at++,glyph);
        }W16(at,0xffff);cached[l][key]=p;
    }return cached[l][key];
}
uint32_t rt_controls_string(unsigned lang,int id){return string(lang,id==0x7002?VIEW:id==0x7003?HELP:TITLE);}
void nat_controls_hint(CPU*c){STDRET(0,0x7003);}
void rt_controls_patch_labels(void) {
    if(!enabled())return;
    HOSTFN;
    W32(0x654d88+0x14,0x7002);W32(0x654d88+0x18,rt_host_addr(nat_controls_hint,"Vita controls hint"));
}
static void label(CPU*c,unsigned key,int x,int y,int color,int small) {
    HOSTFN;
    rt_guest_call(c,0x556a40,1,0);rt_guest_call(c,0x556a50,1,color);
    unsigned before=R32(0x1261bf4);
    rt_guest_call(c,0x559e70,3,string(R8(0x1091ae1),key),32,32);
    unsigned after=R32(0x1261bf4);
    float sx=small?.50f:.85f, target=x*640.0f/960;
    if(key==RECENTER||key==SELECT_ITEM||key==MOVE||key==ALBUM) {
        float right=40;
        for(unsigned i=before;i<after&&i<800;i++) {
            uint32_t p=0x1257248+20*i;
            float edge=(int16_t)R16(p)+(int16_t)R16(p+4);if(edge>right)right=edge;
        }
        target=(key==ALBUM?552:206)*640.0f/960-(right-40)*sx;
    }
    for(unsigned i=before;i<after&&i<800;i++)placements[i]=(GlyphPlacement){target,y*480.0f/544,sx,small?.80f:1.10f,g_frame,1};
}
static void line(float a,float b,float c,float d,float e,float f) {
    console_ui_line(a,b,c,d,0xffa5a5a5);console_ui_line(c,d,e,f,0xffa5a5a5);
}
void nat_vita_bindings_draw(CPU*c) {
    if(!g_vita_bindings_open){F_005176a0_orig(c);return;}
    HOSTFN;
    glyph_n=0;layout_frame=g_frame;
    if(font_texture){d3d_release(&font_texture->com);font_texture=NULL;}
    // Real game font, with all shared presentation state restored afterwards.
    uint8_t font_state[0x28];memcpy(font_state,mb+0x1261c00,sizeof font_state);
    rt_guest_call(c,0x556ae0,1,0x80);
    label(c,TITLE,40,25,0,0);label(c,RECENTER,40,92,0,1);label(c,AIM,770,92,0,1);
    label(c,SELECT_ITEM,40,211,0,1);
    label(c,MOVE,40,341,0,1);
    label(c,ITEMS,770,146,0,1);label(c,USE_CARRY,770,170,0,1);
    label(c,MAP,770,200,0,1);label(c,RUN,770,244,0,1);label(c,ACTION,770,288,0,1);
    label(c,CAMERA,770,351,0,1);
    label(c,ALBUM,400,397,0,1);label(c,PAUSE,743,397,0,1);
    label(c,INFO,116,471,0,1);label(c,EVADE,432,471,0,1);label(c,BACK,842,473,0,1);
    memcpy(mb+0x1261c00,font_state,sizeof font_state);STDRET(0,0);
}
void rt_controls_present(void) {
    if(g_vita_bindings_open&&layout_frame+1==g_frame) {
    if(console_ui_begin()) {
        console_ui_fill(0,0,960,544,0xff000000);console_ui_sprite(UI_VITA,220,148,520,236.36f);
        line(214,118,270,118,310,161);line(758,118,697,118,650,161);
        line(214,232,248,232,290,230);line(214,361,246,361,303,290);
        line(758,167,726,167,673,211);line(758,218,725,218,651,231);
        line(758,262,729,262,695,231);line(758,306,725,306,673,251);
        line(758,374,728,374,660,290);line(652,331,619,414,565,414);line(678,331,704,414,734,414);
        console_ui_line(40,463,920,463,0xff333333);
        console_ui_sprite(UI_L,37,475,28,28);console_ui_sprite(UI_SQUARE,80,475,28,28);
        console_ui_sprite(UI_R,352,475,28,28);console_ui_sprite(UI_CIRCLE,396,475,28,28);
        console_ui_line(67,489,76,489,0xffeeeeee);console_ui_line(71.5f,484.5f,71.5f,493.5f,0xffeeeeee);
        console_ui_line(382,489,391,489,0xffeeeeee);console_ui_line(386.5f,484.5f,386.5f,493.5f,0xffeeeeee);
        console_ui_sprite(UI_CIRCLE,808,477,28,28);
        for(unsigned i=0;i<glyph_n;i++)console_ui_glyph(font_texture,glyphs[i].p,glyphs[i].uv,glyphs[i].top,glyphs[i].bottom);
        unsigned fade=menu_fade();
        if(fade)console_ui_fill(0,0,960,544,fade<<24);
        console_ui_end();
    }
    }
    if(font_texture){d3d_release(&font_texture->com);font_texture=NULL;}
}
void rt_controls_page_badges(void) {
    if(!enabled()||g_vita_bindings_open||page_frame!=g_frame)return;
    HOSTFN;
    // F526490 moves both stock triangles by this signed 512-wide menu offset.
    // Use the same value in the same frame, including interrupted transitions.
    float slide=(int32_t)R32(0x109222c)*(960.0f/512.0f);
    if(console_ui_begin()) {
        // Align with the existing game page arrows; do not draw a second pair.
        uint32_t color=((255-menu_fade())<<24)|0xffffff;
        console_ui_sprite_color(UI_L,95+slide,47,38,38,color);console_ui_sprite_color(UI_R,827+slide,47,38,38,color);
        console_ui_end();
    }
}
