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
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat_unused.h>

typedef struct ics2595_t {
    int oldfs3;
    int oldfs2;
    int dat;
    int pos;
    int state;

    uint64_t last_write;

    double clocks[16];
    double mclocks[4];
    double output_clock;
} ics2595_t;

enum {
    ICS2595_IDLE = 0,
    ICS2595_WRITE,
    ICS2595_READ
};

static int ics2595_div[4] = { 8, 4, 2, 1 };

/* Tmax is 4096 reference-divider periods (data sheet, "Programming Mode
   Selection"): 13.16 ms with the divider of 46 and 14.318 MHz. */
#define ICS2595_TMAX_US 13159

void
ics2595_write(void *priv, int strobe, int dat)
{
    ics2595_t *ics2595 = (ics2595_t *) priv;
    int        d;
    int        n;
    int        l;
    double     freq;

    if (strobe) {
        /* 2 * Tmax with no write clears the shift register and ends any
           sequence, so a stray clock from a plain clock select does not
           start a word that swallows the next one. */
        if ((tsc - ics2595->last_write) > ((2 * ICS2595_TMAX_US * TIMER_USEC) >> 32))
            ics2595->state = ICS2595_IDLE;
        ics2595->last_write = tsc;

        if ((dat & 8) && !ics2595->oldfs3) { /*Data clock*/
            switch (ics2595->state) {
                case ICS2595_IDLE:
                    /* A word opens with its START bit, a 0 (ICS2595 data sheet,
                       table 1), which is bit 0 of the word. ATI's BIOS clocks a 1
                       ahead of it (mach64 ISA BIOS, C000:5733) and X.org does not;
                       real ATI18818s take both. */
                    if (!(dat & 4)) {
                        ics2595->dat   = ics2595->dat >> 1; /* the start bit, bit 0 */
                        ics2595->pos   = 1;
                        ics2595->state = ICS2595_WRITE;
                    }
                    break;
                case ICS2595_WRITE:
                    ics2595->dat = (ics2595->dat >> 1);
                    if (dat & 4)
                        ics2595->dat |= (1 << 19);
                    ics2595->pos++;
                    if (ics2595->pos == 20) {
                        /* START, R/W, L0-L4, N0-N7, EXTFREQ, D0-D1, two STOP bits
                           (data sheet, table 1). A read (R/W = 1) leaves the table
                           alone and shifts the location out on FS0 over the next
                           11 clocks (table 2). */
                        l = (ics2595->dat >> 2) & 0x1f;
                        n = ((ics2595->dat >> 7) & 255) + 257;
                        d = ics2595_div[(ics2595->dat >> 16) & 3];

                        if (ics2595->dat & 2) {
                            ics2595->state = ICS2595_READ;
                            ics2595->pos   = 0;
                            break;
                        }

                        /* EXTFREQ routes the EXTFREQ pin to the output in place of
                           the PLL; what the mach64 boards drive it with is not
                           known, so such a location gives no clock. */
                        if (ics2595->dat & (1 << 15))
                            freq = 0.0;
                        else
                            freq = (14318181.8 * ((double) n / 46.0)) / (double) d;

                        /* L4 = 1 addresses the MCLK table, of which 10000-10011
                           are listed (table 3). */
                        if (l < 16)
                            ics2595->clocks[l] = freq;
                        else if (l < 20)
                            ics2595->mclocks[l - 16] = freq;
                        ics2595->state = ICS2595_IDLE;
                    }
                    break;
                case ICS2595_READ:
                    if (++ics2595->pos == 11)
                        ics2595->state = ICS2595_IDLE;
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
/* The power-up table of the ICS2595-02, the pattern with the reference
   divider of 46 that the mach64 BIOSes give for their ATI18818 (data sheet,
   frequency table). Location 6 is the EXTFREQ pin, whose source on the
   boards is not known; the MCLK table has only two entries. The BIOS
   reprograms all 16 VCLKs at POST. */
static const double ics2595_ati_table[16] = {
    100.27, 125.90, 93.06, 36.27, 50.76, 57.03, 0.00, 45.28,
    135.99, 32.20, 110.51, 80.21, 40.11, 45.28, 75.51, 65.49
};

static const double ics2595_ati_mclk_table[2] = { 40.42, 45.59 };

static void *
ics2595_init(UNUSED(const device_t *info))
{
    ics2595_t *ics2595 = (ics2595_t *) calloc(1, sizeof(ics2595_t));

    for (int c = 0; c < 16; c++)
        ics2595->clocks[c] = ics2595_ati_table[c] * 1000000.0;
    for (int c = 0; c < 2; c++)
        ics2595->mclocks[c] = ics2595_ati_mclk_table[c] * 1000000.0;
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

/* Entry n of the table: a VGA mode's clock select lines pick one directly. */
double
ics2595_getclock_entry(void *priv, int n)
{
    const ics2595_t *ics2595 = (ics2595_t *) priv;

    return ics2595->clocks[n & 15];
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
