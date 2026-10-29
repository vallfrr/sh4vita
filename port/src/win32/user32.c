// Emulated USER32: a single window (SDL), minimal message queue.
#include "win32.h"
#include "plat.h"

#define U(name) void imp_USER32_##name(CPU *c)
#define HWND_MAIN    0x00010010u
#define HWND_DESKTOP 0x00010014u

static uint32_t g_wndproc;
static int g_quit_posted;

U(RegisterClassExA) { HOSTFN; g_wndproc = R32(ARG(1) + 8); STDRET(1, 0xc001); }
U(UnregisterClassA) { STDRET(2, 1); }
U(CreateWindowExA) {
    HOSTFN;
    rt_log("CreateWindowExA(\"%s\", %ux%u)\n", GSTR(ARG(3)), ARG(7), ARG(8));
    rt_apply_res();
    STDRET(12, HWND_MAIN);
}
U(DestroyWindow) { STDRET(1, 1); }
U(ShowWindow) { plat_show_window(); STDRET(2, 1); }
U(UpdateWindow) { STDRET(1, 1); }
U(GetWindowPlacement) {
    HOSTFN;
    uint32_t p = ARG(2);
    memset(mb + p, 0, 44);
    W32(p, 44);
    W32(p + 8, 1); // SW_SHOWNORMAL
    STDRET(2, 1);
}
U(GetForegroundWindow) { STDRET(0, HWND_MAIN); }
U(GetDesktopWindow) { STDRET(0, HWND_DESKTOP); }
U(LoadIconA) { STDRET(2, 1); }
U(RegisterWindowMessageA) { static uint32_t n = 0xc100; STDRET(1, n++); }
U(SystemParametersInfoA) {
    HOSTFN;
    if (ARG(3) && ARG(1) == 0x10) W32(ARG(3), 0); // SPI_GETSCREENSAVEACTIVE
    STDRET(4, 1);
}
U(DefWindowProcA) { STDRET(4, 0); }
U(MessageBoxA) {
    HOSTFN;
    rt_log("MessageBoxA(\"%s\", \"%s\")\n", GSTR(ARG(3)) ? GSTR(ARG(3)) : "", GSTR(ARG(2)) ? GSTR(ARG(2)) : "");
    int button = plat_message_box(GSTR(ARG(3)), GSTR(ARG(2)), ARG(4));
    if (!button) w32_set_last_error(c, 31); // ERROR_GEN_FAILURE; no implicit answer
    STDRET(4, button);
}
U(PeekMessageA) {
    HOSTFN;
    uint32_t msg = ARG(1);
    if (plat_pump_events() || g_quit_posted) {
        memset(mb + msg, 0, 28);
        W32(msg + 4, 0x12); // WM_QUIT
        STDRET(5, 1);
    }
    STDRET(5, 0);
}
U(TranslateMessage) { STDRET(1, 0); }
U(DispatchMessageA) { STDRET(1, 0); }
U(PostQuitMessage) { g_quit_posted = 1; STDRET(1, 0); }
U(PostMessageA) {
    HOSTFN;
    if (ARG(2) == 0x10 || ARG(2) == 0x12) g_quit_posted = 1; // WM_CLOSE / WM_QUIT
    STDRET(4, 1);
}
U(PostThreadMessageA) { STDRET(4, 1); }
U(GetQueueStatus) { STDRET(1, 0); }
U(MsgWaitForMultipleObjects) {
    HOSTFN;
    uint32_t n = ARG(1), hs[64];
    for (uint32_t i = 0; i < n && i < 64; i++) hs[i] = R32(ARG(2) + 4 * i);
    extern uint64_t g_t_wait;
    uint64_t t0 = SDL_GetPerformanceCounter();
    uint32_t r = w32_wait(n, hs, ARG(3) != 0, ARG(4));
    if (c == g_main_cpu) g_t_wait += SDL_GetPerformanceCounter() - t0;
    STDRET(5, r);
}
U(GetCursorPos) { HOSTFN; W32(ARG(1), 0); W32(ARG(1) + 4, 0); STDRET(1, 1); }
U(SetCursorPos) { STDRET(2, 1); }
U(ClientToScreen) { STDRET(2, 1); }
U(GetClientRect) {
    HOSTFN;
    uint32_t r = ARG(2);
    W32(r, 0); W32(r + 4, 0); W32(r + 8, plat_width()); W32(r + 12, plat_height());
    STDRET(2, 1);
}
