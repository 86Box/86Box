/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ICD2047 clock generator emulation.
 *
 *          Used by the V7 chips.
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

typedef struct icd2047_t {
    float freq[32];
} icd2047_t;

#ifdef ENABLE_ICD2047_LOG
int icd2047_do_log = ENABLE_ICD2047_LOG;

static void
icd2047_log(const char *fmt, ...)
{
    va_list ap;

    if (icd2047_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define icd2047_log(fmt, ...)
#endif

float
icd2047_getclock(int clock, void *priv)
{
    const icd2047_t *icd2047 = (icd2047_t *) priv;

    if (clock > 31)
        clock = 31;

    return icd2047->freq[clock];
}

static void *
icd2047_init(const device_t *info)
{
    icd2047_t *icd2047 = (icd2047_t *) malloc(sizeof(icd2047_t));
    memset(icd2047, 0, sizeof(icd2047_t));

    switch (info->local) {
        case 20:
            /* ICD2047-20 for Headland series */
            icd2047->freq[0x00] = 25175000.0;
            icd2047->freq[0x01] = 28322000.0;
            icd2047->freq[0x02] = 40000000.0;
            icd2047->freq[0x03] = 32500000.0;
            icd2047->freq[0x04] = 50350000.0;
            icd2047->freq[0x05] = 65000000.0;
            icd2047->freq[0x06] = 38000000.0;
            icd2047->freq[0x07] = 44900000.0;
            icd2047->freq[0x08] = 25175000.0;
            icd2047->freq[0x09] = 28322000.0;
            icd2047->freq[0x0a] = 80000000.0;
            icd2047->freq[0x0b] = 32500000.0;
            icd2047->freq[0x0c] = 50350000.0;
            icd2047->freq[0x0d] = 65000000.0;
            icd2047->freq[0x0e] = 76000000.0;
            icd2047->freq[0x0f] = 44900000.0;
            icd2047->freq[0x10] = 25175000.0;
            icd2047->freq[0x11] = 44900000.0;
            icd2047->freq[0x12] = 28322000.0;
            icd2047->freq[0x13] = 38000000.0;
            icd2047->freq[0x14] = 40000000.0;
            icd2047->freq[0x15] = 46000000.0;
            icd2047->freq[0x16] = 48000000.0;
            icd2047->freq[0x17] = 60000000.0;
            icd2047->freq[0x18] = 65000000.0;
            icd2047->freq[0x19] = 72000000.0;
            icd2047->freq[0x1a] = 74000000.0;
            icd2047->freq[0x1b] = 76000000.0;
            icd2047->freq[0x1c] = 78000000.0;
            icd2047->freq[0x1d] = 80000000.0;
            icd2047->freq[0x1e] = 100000000.0;
            icd2047->freq[0x1f] = 110000000.0;
            break;

        default:
            break;
    }

    return icd2047;
}

static void
icd2047_close(void *priv)
{
    icd2047_t *icd2047 = (icd2047_t *) priv;

    if (icd2047)
        free(icd2047);
}

const device_t icd2047_20_device = {
    .name          = "ICD2047-20 Clock Generator",
    .internal_name = "icd2047_20",
    .flags         = 0,
    .local         = 20,
    .init          = icd2047_init,
    .close         = icd2047_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
