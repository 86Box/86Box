/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ICS90C64A clock generator emulation.
 *
 *          Used by the PVGA chips.
 *
 * Authors: TheCollector1995.
 *
 *          Copyright 2025 TheCollector1995.
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

typedef struct ics90c64a_t {
    float freq[17];
} ics90c64a_t;

#ifdef ENABLE_ICS90C64A_LOG
int ics90c64a_do_log = ENABLE_ICS90C64A_LOG;

static void
ics90c64a_log(const char *fmt, ...)
{
    va_list ap;

    if (ics90c64a_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ics90c64a_log(fmt, ...)
#endif

float
ics90c64a_vclk_getclock(int clock, void *priv)
{
    const ics90c64a_t *ics90c64a = (ics90c64a_t *) priv;

    if (clock > 16)
        clock = 16;

    return ics90c64a->freq[clock];
}

static void *
ics90c64a_init(const device_t *info)
{
    ics90c64a_t *ics90c64a = (ics90c64a_t *) calloc(1, sizeof(ics90c64a_t));

    switch (info->local) {
        case 903:
            /* ICS90C64A-903 for PVGA chip series, also per debian svgatext mode textconfig */
            ics90c64a->freq[0x00] = 30000000.0;
            ics90c64a->freq[0x01] = 77250000.0;
            ics90c64a->freq[0x02] = 14318000.0;
            ics90c64a->freq[0x03] = 80000000.0;
            ics90c64a->freq[0x04] = 31500000.0;
            ics90c64a->freq[0x05] = 36000000.0;
            ics90c64a->freq[0x06] = 75000000.0;
            ics90c64a->freq[0x07] = 50000000.0;
            ics90c64a->freq[0x08] = 40000000.0;
            ics90c64a->freq[0x09] = 50000000.0;
            ics90c64a->freq[0x0a] = 32000000.0;
            ics90c64a->freq[0x0b] = 44900000.0;
            ics90c64a->freq[0x0c] = 25175000.0;
            ics90c64a->freq[0x0d] = 28322000.0;
            ics90c64a->freq[0x0e] = 65000000.0;
            ics90c64a->freq[0x0f] = 36000000.0;
            break;

        default:
            break;
    }

    return ics90c64a;
}

static void
ics90c64a_close(void *priv)
{
    ics90c64a_t *ics90c64a = (ics90c64a_t *) priv;

    if (ics90c64a)
        free(ics90c64a);
}

const device_t ics90c64a_903_device = {
    .name          = "ICS90C64A-903 Clock Generator",
    .internal_name = "ics90c64a_903",
    .flags         = 0,
    .local         = 903,
    .init          = ics90c64a_init,
    .close         = ics90c64a_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
