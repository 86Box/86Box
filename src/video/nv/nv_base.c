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
 *
 *          Copyright 2024-2025 starfrost
 */

// Common NV1/3/4... init
#define HAVE_STDARG_H // wtf is this crap
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <86box/86box.h>
#ifndef RELEASE_BUILD
#include <86box/device.h>
#endif
#include <86box/log.h>


// Common logging
#ifdef ENABLE_NV_LOG
int nv_do_log = ENABLE_NV_LOG;

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
    #ifdef ENABLE_NV_LOG_ULTRA
    if (!nv_do_log)
        return; 

    va_start(arg, fmt);
    nv_log_internal(fmt, arg);
    va_end(arg);
    #endif
}

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
#endif