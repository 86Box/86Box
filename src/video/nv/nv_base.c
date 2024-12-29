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
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <86box/86box.h>
#include <86box/nv/vid_nv.h>


// Common logging
#ifdef ENABLE_NV_LOG
int nv_do_log = ENABLE_NV_LOG;

void nv_log(const char *fmt, ...)
{
    va_list ap;

    if (nv_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
void
nv_log(const char *fmt, ...)
{

}
#endif