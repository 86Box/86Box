/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ICS2494 clock generator emulation.
 *
 *		Used by the AMI S3 924.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020 Miran Grca.
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

typedef struct ics2494_t {
    float freq[16];
} ics2494_t;

#ifdef ENABLE_ICS2494_LOG
int ics2494_do_log = ENABLE_ICS2494_LOG;

static void
ics2494_log(const char *fmt, ...)
{
    va_list ap;

    if (ics2494_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ics2494_log(fmt, ...)
#endif

float
ics2494_getclock(int clock, void *p)
{
    ics2494_t *ics2494 = (ics2494_t *) p;

    if (clock > 16)
        clock = 16;

    return ics2494->freq[clock];
}

static void *
ics2494_init(const device_t *info)
{
    ics2494_t *ics2494 = (ics2494_t *) malloc(sizeof(ics2494_t));
    memset(ics2494, 0, sizeof(ics2494_t));

    switch (info->local) {
        case 305:
            /* ICS2494A(N)-205 for S3 86C924 */
            ics2494->freq[0x0] = 25175000.0;
            ics2494->freq[0x1] = 28322000.0;
            ics2494->freq[0x2] = 40000000.0;
            ics2494->freq[0x3] = 0.0;
            ics2494->freq[0x4] = 50000000.0;
            ics2494->freq[0x5] = 77000000.0;
            ics2494->freq[0x6] = 36000000.0;
            ics2494->freq[0x7] = 44889000.0;
            ics2494->freq[0x8] = 130000000.0;
            ics2494->freq[0x9] = 120000000.0;
            ics2494->freq[0xa] = 80000000.0;
            ics2494->freq[0xb] = 31500000.0;
            ics2494->freq[0xc] = 110000000.0;
            ics2494->freq[0xd] = 65000000.0;
            ics2494->freq[0xe] = 75000000.0;
            ics2494->freq[0xf] = 94500000.0;
            break;
    }

    return ics2494;
}

static void
ics2494_close(void *priv)
{
    ics2494_t *ics2494 = (ics2494_t *) priv;

    if (ics2494)
        free(ics2494);
}

const device_t ics2494an_305_device = {
    .name          = "ICS2494AN-305 Clock Generator",
    .internal_name = "ics2494an_305",
    .flags         = 0,
    .local         = 305,
    .init          = ics2494_init,
    .close         = ics2494_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
