/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ICS1494 clock generator emulation.
 *
 *          Used by the V7 and PVGA chips.
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

typedef struct ics1494_t {
    float freq[32];
} ics1494_t;

#ifdef ENABLE_ICS1494_LOG
int ics1494_do_log = ENABLE_ICS1494_LOG;

static void
ics1494_log(const char *fmt, ...)
{
    va_list ap;

    if (ics1494_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ics1494_log(fmt, ...)
#endif

float
ics1494_getclock(int clock, void *priv)
{
    const ics1494_t *ics1494 = (ics1494_t *) priv;

    if (clock > 31)
        clock = 31;

    return ics1494->freq[clock];
}

static void *
ics1494_init(const device_t *info)
{
    ics1494_t *ics1494 = (ics1494_t *) malloc(sizeof(ics1494_t));
    memset(ics1494, 0, sizeof(ics1494_t));

    switch (info->local) {
        case 540:
            /* ICS1494(M)-540 for Radius series */
            ics1494->freq[0x00] = 57283000.0;
            ics1494->freq[0x01] = 12273000.0;
            ics1494->freq[0x02] = 14500000.0;
            ics1494->freq[0x03] = 15667000.0;
            ics1494->freq[0x04] = 112000000.0;
            ics1494->freq[0x05] = 126000000.0;
            ics1494->freq[0x06] = 30240000.0;
            ics1494->freq[0x07] = 91200000.0;
            ics1494->freq[0x08] = 120000000.0;
            ics1494->freq[0x09] = 48000000.0;
            ics1494->freq[0x0a] = 50675000.0;
            ics1494->freq[0x0b] = 55300000.0;
            ics1494->freq[0x0c] = 64000000.0;
            ics1494->freq[0x0d] = 68750000.0;
            ics1494->freq[0x0e] = 88500000.0;
            ics1494->freq[0x0f] = 51270000.0;
            ics1494->freq[0x10] = 100000000.0;
            ics1494->freq[0x11] = 95200000.0;
            ics1494->freq[0x12] = 55000000.0;
            ics1494->freq[0x13] = 60000000.0;
            ics1494->freq[0x14] = 63000000.0;
            ics1494->freq[0x15] = 99522000.0;
            ics1494->freq[0x16] = 130000000.0;
            ics1494->freq[0x17] = 80000000.0;
            ics1494->freq[0x18] = 25175000.0;
            ics1494->freq[0x19] = 28322000.0;
            ics1494->freq[0x1a] = 48000000.0;
            ics1494->freq[0x1b] = 76800000.0;
            ics1494->freq[0x1c] = 38400000.0;
            ics1494->freq[0x1d] = 43200000.0;
            ics1494->freq[0x1e] = 61440000.0;
            ics1494->freq[0x1f] = 0.0;
            break;

        case 541:
            /* ICS1494(M)-540 for Radius HT209 */
            ics1494->freq[0x00] = 25175000.0;
            ics1494->freq[0x01] = 28322000.0;
            ics1494->freq[0x02] = 61440000.0; /*FCLK*/
            ics1494->freq[0x03] = 74000000.0; /*XRESM*/
            ics1494->freq[0x04] = 50350000.0;
            ics1494->freq[0x05] = 65000000.0;
            ics1494->freq[0x06] = 37575000.0; /*FCLK*/
            ics1494->freq[0x07] = 40000000.0;
            break;

        default:
            break;
    }

    return ics1494;
}

static void
ics1494_close(void *priv)
{
    ics1494_t *ics1494 = (ics1494_t *) priv;

    if (ics1494)
        free(ics1494);
}

const device_t ics1494m_540_device = {
    .name          = "ICS2494M-540 Clock Generator",
    .internal_name = "ics1494m_540",
    .flags         = 0,
    .local         = 540,
    .init          = ics1494_init,
    .close         = ics1494_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ics1494m_540_radius_ht209_device = {
    .name          = "ICS2494M-540 (Radius HT209) Clock Generator",
    .internal_name = "ics1494m_540_radius_ht209",
    .flags         = 0,
    .local         = 541,
    .init          = ics1494_init,
    .close         = ics1494_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
