/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CH9204 clock generator emulation.
 *
 *
 * Authors: TheCollector1995.
 *
 *          Copyright 2026 TheCollector1995.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>

typedef struct ch9204_t {
    float freq[16];
} ch9204_t;

#ifdef ENABLE_CH9204_LOG
int ch9204_do_log = ENABLE_CH9201_LOG;

static void
ch9204_log(const char *fmt, ...)
{
    va_list ap;

    if (ch9204_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ch9204_log(fmt, ...)
#endif

float
ch9204_getclock(int clock, void *priv)
{
    const ch9204_t *ch9204 = (ch9204_t *) priv;

    if (clock > 15)
        clock = 15;

    return ch9204->freq[clock];
}

static void *
ch9204_init(const device_t *info)
{
    ch9204_t *ch9204 = (ch9204_t *) calloc(1, sizeof(ch9204_t));

    ch9204->freq[0x00] =  25175000.0;
    ch9204->freq[0x01] =  28322000.0;
    ch9204->freq[0x02] =  32514000.0;
    ch9204->freq[0x03] =  36000000.0;
    ch9204->freq[0x04] =  40000000.0;
    ch9204->freq[0x05] =  44900000.0;
    ch9204->freq[0x06] =  50350000.0;
    ch9204->freq[0x07] =  65000000.0;
    ch9204->freq[0x08] =  78000000.0;
    ch9204->freq[0x09] =  56644000.0;
    ch9204->freq[0x0a] =  63000000.0;
    ch9204->freq[0x0b] =  75000000.0;
    ch9204->freq[0x0c] =  80000000.0;
    ch9204->freq[0x0d] =  89800000.0;
    ch9204->freq[0x0e] = 100700000.0;
    ch9204->freq[0x0f] =  31500000.0;

    return ch9204;
}

static void
ch9204_close(void *priv)
{
    ch9204_t *ch9204 = (ch9204_t *) priv;

    if (ch9204)
        free(ch9204);
}

const device_t ch9204_device = {
    .name          = "CH9204 Clock Generator",
    .internal_name = "ch9204",
    .flags         = 0,
    .local         = 0,
    .init          = ch9204_init,
    .close         = ch9204_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
