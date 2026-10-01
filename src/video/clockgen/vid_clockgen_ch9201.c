/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ICS1394/CH9201 clock generator emulation.
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

typedef struct ch9201_t {
    float freq[32];
} ch9201_t;

#ifdef ENABLE_CH9201_LOG
int ch9201_do_log = ENABLE_CH9201_LOG;

static void
ch9201_log(const char *fmt, ...)
{
    va_list ap;

    if (ch9201_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ch9201_log(fmt, ...)
#endif

float
ch9201_getclock(int clock, void *priv)
{
    const ch9201_t *ch9201 = (ch9201_t *) priv;

    if (clock > 31)
        clock = 31;

    return ch9201->freq[clock];
}

static void *
ch9201_init(const device_t *info)
{
    ch9201_t *ch9201 = (ch9201_t *) calloc(1, sizeof(ch9201_t));

    ch9201->freq[0x00] = 25175000.0;
    ch9201->freq[0x01] = 28322000.0;
    ch9201->freq[0x02] = 40000000.0;
    ch9201->freq[0x03] = 32514000.0;
    ch9201->freq[0x04] = 50350000.0;
    ch9201->freq[0x05] = 65000000.0;
    ch9201->freq[0x06] = 38000000.0;
    ch9201->freq[0x07] = 44900000.0;
    ch9201->freq[0x08] = 25175000.0;
    ch9201->freq[0x09] = 44900000.0;
    ch9201->freq[0x0a] = 50350000.0;
    ch9201->freq[0x0b] = 65000000.0;
    ch9201->freq[0x0c] = 40000000.0;
    ch9201->freq[0x0d] = 14318184.0;
    ch9201->freq[0x0e] = 50350000.0;
    ch9201->freq[0x0f] = 80000000.0;
    ch9201->freq[0x10] = 25175000.0;
    ch9201->freq[0x11] = 28322000.0;
    ch9201->freq[0x12] = 32514000.0;
    ch9201->freq[0x13] = 36000000.0;
    ch9201->freq[0x14] = 40000000.0;
    ch9201->freq[0x15] = 44900000.0;
    ch9201->freq[0x16] = 50350000.0;
    ch9201->freq[0x17] = 65000000.0;
    ch9201->freq[0x18] = 25175000.0;
    ch9201->freq[0x19] = 28322000.0;
    ch9201->freq[0x1a] = 32514000.0;
    ch9201->freq[0x1b] = 36000000.0;
    ch9201->freq[0x1c] = 40000000.0;
    ch9201->freq[0x1d] = 44900000.0;
    ch9201->freq[0x1e] = 50350000.0;
    ch9201->freq[0x1f] = 62000000.0;

    return ch9201;
}

static void
ch9201_close(void *priv)
{
    ch9201_t *ch9201 = (ch9201_t *) priv;

    if (ch9201)
        free(ch9201);
}

const device_t ch9201_device = {
    .name          = "CH9201 Clock Generator",
    .internal_name = "ch9201",
    .flags         = 0,
    .local         = 0,
    .init          = ch9201_init,
    .close         = ch9201_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
