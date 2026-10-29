// IDirect3D8: single adapter, "generic DX8 card" capabilities, CreateDevice.
#include "d3d8.h"
#include "plat.h"
#include <stdlib.h>

typedef struct { ComObj com; } D3D;
#define M(name) static void d3d_##name(CPU *c)

M(QueryInterface) { STDRET(3, 0x80004002u); }
M(AddRef) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o ? ++o->refs : 1); }
M(Release) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o ? --o->refs : 0); }
M(RegisterSoftwareDevice) { STDRET(2, D3DERR_INVALIDCALL); }
M(GetAdapterCount) { STDRET(1, 1); }
M(GetAdapterIdentifier) {
    HOSTFN;
    uint32_t p = ARG(4);
    memset(mb + p, 0, 1068); // sizeof(D3DADAPTER_IDENTIFIER8)
    strcpy(GSTR(p), "sh4vita.dll");
    strcpy(GSTR(p + 512), "sh4vita OpenGL");
    W32(p + 1024 + 8, 0x10de); // VendorId (NVIDIA)
    W32(p + 1024 + 12, 0x0250);
    STDRET(4, HR_OK);
}
static const uint32_t modes[][2] = {{640, 480}, {800, 600}, {960, 544}, {1024, 768}};
M(GetAdapterModeCount) { STDRET(2, 4); }
M(EnumAdapterModes) {
    HOSTFN;
    uint32_t i = ARG(3), p = ARG(4);
    if (i >= 4) STDRET(4, D3DERR_INVALIDCALL);
    W32(p, modes[i][0]); W32(p + 4, modes[i][1]); W32(p + 8, 60); W32(p + 12, FMT_X8R8G8B8);
    STDRET(4, HR_OK);
}
M(GetAdapterDisplayMode) {
    HOSTFN;
    uint32_t p = ARG(3);
    W32(p, 1024); W32(p + 4, 768); W32(p + 8, 60); W32(p + 12, FMT_X8R8G8B8);
    STDRET(3, HR_OK);
}
M(CheckDeviceType) { STDRET(6, HR_OK); }
M(CheckDeviceFormat) {
    HOSTFN;
    uint32_t usage = ARG(5), fmt = ARG(7);
    rt_log("[d3d8] CheckDeviceFormat usage=%x fmt=%08x\n", usage, fmt);
    STDRET(7, fmt == FMT_P8 ? D3DERR_NOTAVAILABLE : HR_OK);
}
M(CheckDeviceMultiSampleType) { HOSTFN; STDRET(5, ARG(5) == 0 ? HR_OK : D3DERR_NOTAVAILABLE); }
M(CheckDepthStencilMatch) { STDRET(6, HR_OK); }

void d3d_fill_caps(uint32_t p) {
    HOSTFN;
    memset(mb + p, 0, 212);
    W32(p + 0, 1);            // D3DDEVTYPE_HAL
    W32(p + 8, 0x20000);      // Caps: READ_SCANLINE
    W32(p + 12, 0x20000);     // Caps2: FULLSCREENGAMMA
    W32(p + 20, 0x80000003u); // PresentationIntervals: IMMEDIATE|ONE|DEFAULT
    W32(p + 28, 0x001bfef0u); // DevCaps : HWTRANSFORMANDLIGHT, DRAWPRIMITIVES2EX, HWRASTERIZATION...
    W32(p + 32, 0x00000ff2u); // PrimitiveMiscCaps : masques, cull CW/CCW, COLORWRITEENABLE, BLENDOP
    W32(p + 36, 0x00f7e1b1u); // RasterCaps : DITHER, ZTEST, FOGVERTEX, FOGTABLE, MIPMAPLODBIAS, ZFOG, WFOG...
    W32(p + 40, 0xff); W32(p + 44, 0x1fff); W32(p + 48, 0x1fff); W32(p + 52, 0xff);
    W32(p + 56, 0x000242a8u); // ShadeCaps
    W32(p + 60, 0x0000c5c7u); // TextureCaps: PERSPECTIVE, ALPHA, MIPMAP, CUBEMAP, PROJECTED... (not POW2)
    W32(p + 64, 0x07070707u); W32(p + 68, 0x07070707u); W32(p + 72, 0x07070707u);
    W32(p + 76, 0x3f);        // TextureAddressCaps
    W32(p + 80, 0x3f);
    W32(p + 84, 0x1f);
    W32(p + 88, 2048); W32(p + 92, 2048); W32(p + 96, 256);
    W32(p + 100, 8192); W32(p + 104, 2048); W32(p + 108, 8);
    W32(p + 112, rt_u32f(1e10f));
    W32(p + 116, rt_u32f(-8192.f)); W32(p + 120, rt_u32f(-8192.f));
    W32(p + 124, rt_u32f(8192.f)); W32(p + 128, rt_u32f(8192.f));
    W32(p + 136, 0xff);       // StencilCaps
    W32(p + 140, 0x00080008u); // FVFCaps: 8 texture coordinates
    W32(p + 144, 0x03ffffffu); // TextureOpCaps : toutes
    W32(p + 148, 8); W32(p + 152, 4);
    W32(p + 156, 0x3b);       // VertexProcessingCaps
    W32(p + 160, 8); W32(p + 164, 6); W32(p + 168, 4); W32(p + 172, 0);
    W32(p + 176, rt_u32f(64.f));
    W32(p + 180, 0xfffff); W32(p + 184, 0xffff); W32(p + 188, 16); W32(p + 192, 255); // 16-bit indices: GXM limit
    W32(p + 196, 0xfffe0101u); W32(p + 200, 96);
    W32(p + 204, 0xffff0104u); W32(p + 208, rt_u32f(8.f));
}
M(GetDeviceCaps) { HOSTFN; d3d_fill_caps(ARG(4)); STDRET(4, HR_OK); }
M(GetAdapterMonitor) { STDRET(2, 1); }

uint32_t device_create(uint32_t pp);
M(CreateDevice) {
    HOSTFN;
    uint32_t pp = ARG(6), out = ARG(7);
    rt_log("[d3d8] CreateDevice %ux%u fmt=%u windowed=%u depth=%u/%u flags=%x\n", R32(pp), R32(pp + 4),
           R32(pp + 8), R32(pp + 28), R32(pp + 32), R32(pp + 36), ARG(5));
    uint32_t dev = device_create(pp);
    W32(out, dev);
    STDRET(7, dev ? HR_OK : HR_FAIL);
}

static const gfn d3d_vt[] = {d3d_QueryInterface, d3d_AddRef, d3d_Release, d3d_RegisterSoftwareDevice,
    d3d_GetAdapterCount, d3d_GetAdapterIdentifier, d3d_GetAdapterModeCount, d3d_EnumAdapterModes,
    d3d_GetAdapterDisplayMode, d3d_CheckDeviceType, d3d_CheckDeviceFormat, d3d_CheckDeviceMultiSampleType,
    d3d_CheckDepthStencilMatch, d3d_GetDeviceCaps, d3d_GetAdapterMonitor, d3d_CreateDevice};

void *g_d3d_obj;   // last IDirect3D8 created (IDirect3DDevice8::GetDirect3D)
void imp_D3D8_Direct3DCreate8(CPU *c) {
    D3D *d = calloc(1, sizeof *d);
    uint32_t g = com_wrap(&d->com, K_D3D, d3d_vt, 16, "IDirect3D8");
    g_d3d_obj = d;
    rt_log("[d3d8] Direct3DCreate8 -> %08x\n", g);
    STDRET(1, g);
}
