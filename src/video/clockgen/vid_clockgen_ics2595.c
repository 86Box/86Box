/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ICS2595 clock chip emulation.  Used by ATI Mach64.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat_unused.h>

typedef struct ics2595_t {
    int oldfs3;
    int oldfs2;
    int dat;
    int pos;
    int state;

    double clocks[16];
    double output_clock;
} ics2595_t;

enum {
    ICS2595_IDLE = 0,
    ICS2595_WRITE,
    ICS2595_READ
};

static int ics2595_div[4] = { 8, 4, 2, 1 };

void
ics2595_write(void *priv, int strobe, int dat)
{
    ics2595_t *ics2595 = (ics2595_t *) priv;
    int        d;
    int        n;
    int        l;

    if (strobe) {
        if ((dat & 8) && !ics2595->oldfs3) { /*Data clock*/
            switch (ics2595->state) {
                case ICS2595_IDLE:
                    ics2595->state = (dat & 4) ? ICS2595_WRITE : ICS2595_IDLE;
                    ics2595->pos   = 0;
                    break;
                case ICS2595_WRITE:
                    ics2595->dat = (ics2595->dat >> 1);
                    if (dat & 4)
                        ics2595->dat |= (1 << 19);
                    ics2595->pos++;
                    if (ics2595->pos == 20) {
                        l = (ics2595->dat >> 2) & 0xf;
                        n = ((ics2595->dat >> 7) & 255) + 257;
                        d = ics2595_div[(ics2595->dat >> 16) & 3];

                        ics2595->clocks[l] = (14318181.8 * ((double) n / 46.0)) / (double) d;
                        ics2595->state     = ICS2595_IDLE;
                    }
                    break;

                default:
                    break;
            }
        }

        ics2595->oldfs2 = dat & 4;
        ics2595->oldfs3 = dat & 8;
    }

    ics2595->output_clock = ics2595->clocks[dat];
}

/* The ATI18818 frequency table for the ATI68860 DAC, PCLK_TABLE 2, in MHz
   (mach64 BIOS Kit BIO-888GX0-02, D-3): the entries a mach64 BIOS loads,
   so an entry is a usable dot clock before anything reprograms it. */
static const double ics2595_ati_table[16] = {
    25.18, 28.32, 31.50, 36.00, 40.00, 44.90, 49.50, 50.00,
    100.00, 110.00, 126.00, 135.00, 78.25, 80.00, 75.00, 65.00
};

static void *
ics2595_init(UNUSED(const device_t *info))
{
    ics2595_t *ics2595 = (ics2595_t *) calloc(1, sizeof(ics2595_t));

    for (int c = 0; c < 16; c++)
        ics2595->clocks[c] = ics2595_ati_table[c] * 1000000.0;
    ics2595->output_clock = ics2595->clocks[0];

    return ics2595;
}

static void
ics2595_close(void *priv)
{
    ics2595_t *ics2595 = (ics2595_t *) priv;

    if (ics2595)
        free(ics2595);
}

double
ics2595_getclock(void *priv)
{
    const ics2595_t *ics2595 = (ics2595_t *) priv;

    return ics2595->output_clock;
}

void
ics2595_setclock(void *priv, double clock)
{
    ics2595_t *ics2595 = (ics2595_t *) priv;

    ics2595->output_clock = clock;
}

const device_t ics2595_device = {
    .name          = "ICS2595 clock chip",
    .internal_name = "ics2595",
    .flags         = 0,
    .local         = 0,
    .init          = ics2595_init,
    .close         = ics2595_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
