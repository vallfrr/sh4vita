// Optional development tour. No guest writes or checks when SH4_AUDIT is absent.
#pragma once
#include "rt.h"
extern int g_audit_enabled;
extern int g_audit_frames;
extern int g_audit_checks;
void rt_audit_init(void);
void rt_audit_input(CPU *c, uint32_t keys, uint32_t size);
unsigned rt_audit_motion(void); // explicit tour motion flags for the controller path
void rt_audit_frame(unsigned frame);
void rt_audit_watchdog(void);
int rt_audit_capture(unsigned frame, char *path, size_t size);
void rt_audit_request_capture(unsigned index, unsigned angle);
