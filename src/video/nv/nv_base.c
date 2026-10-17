/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Base file for emulation of NVidia video cards.
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */

// Common NV1/3/4... init
#define HAVE_STDARG_H // wtf is this crap
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <86box/86box.h>
#ifndef RELEASE_BUILD
#include <86box/device.h>
#endif
#include <86box/log.h>

// Common logging
/* Off unless built with ENABLE_NV_LOG: the driver touches MMIO constantly. */
#ifdef ENABLE_NV_LOG
int nv_do_log = ENABLE_NV_LOG;
#else
int nv_do_log = 0;
#endif

// A bit of kludge so that in the future we can abstract this function acorss multiple generations of Nvidia GPUs
void* nv_log_device;
bool nv_log_full = false;

void nv_log_set_device(void* device)
{
    // in case the cyclical logger doesn't show you the full context of what went on, you can enable this debug feature
    #ifndef RELEASE_BUILD
    if (device 
    && device_get_config_int("nv_debug_fulllog"))
    {
        nv_log_full = true; 
    }
    #endif

    nv_log_device = device;
}

void nv_log_internal(const char* fmt, va_list arg)
{
    if (!nv_log_device)
        return;

    // If our debug config option is configured, full log. Otherwise log with cyclical detection.
    if (nv_log_full)   
        log_out(nv_log_device, fmt, arg);
    else
        log_out_cyclic(nv_log_device, fmt, arg);
    
}

void nv_log(const char *fmt, ...)
{
    va_list arg; 

    if (!nv_do_log)
        return; 

    va_start(arg, fmt);
    nv_log_internal(fmt, arg);
    va_end(arg);
}

void nv_log_verbose_only(const char *fmt, ...)
{
    //#ifdef ENABLE_NV_LOG_ULTRA
    va_list arg; 

    if (!nv_do_log)
        return; 

    va_start(arg, fmt);
    nv_log_internal(fmt, arg);
    va_end(arg);
    //#endif
}
/*
#else
void nv_log(const char *fmt, ...)
{
    
}

void nv_log_verbose_only(const char *fmt, ...)
{
    
}

void nv_log_set_device(void* device)
{

}
#endif*/

/* Something the driver did that we do not handle yet (an unimplemented method,
   an odd format...). warning() would open a message box every time, and a
   driver repeats these constantly, so each distinct message goes to the log
   once instead. */
#define NV_WARNING_SEEN_MAX 256
static uint32_t nv_warning_seen[NV_WARNING_SEEN_MAX];
static int      nv_warning_seen_count;

void nv_warning(const char *fmt, ...)
{
    char     buf[512];
    va_list  arg;
    uint32_t hash = 2166136261u;

    va_start(arg, fmt);
    vsnprintf(buf, sizeof(buf), fmt, arg);
    va_end(arg);

    for (const char *p = buf; *p; p++)
        hash = (hash ^ (uint8_t) *p) * 16777619u;

    for (int i = 0; i < nv_warning_seen_count; i++) {
        if (nv_warning_seen[i] == hash)
            return;
    }
    if (nv_warning_seen_count < NV_WARNING_SEEN_MAX)
        nv_warning_seen[nv_warning_seen_count++] = hash;

    always_log("NV: %s%s", buf, (buf[0] && buf[strlen(buf) - 1] == '\n') ? "" : "\n");
}
