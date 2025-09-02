/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          STG1702 true colour RAMDAC emulation.
 *
 *
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
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/plat_unused.h>

typedef struct stg_ramdac_t {
    int     magic_count, index;
    uint8_t regs[256];
    uint8_t command;
} stg_ramdac_t;

static int stg_state_read[2][8] = {
    {1,  2, 3, 4, 0, 0, 0, 0},
    { 1, 2, 3, 4, 5, 6, 7, 7}
};
static int stg_state_write[8] = { 0, 0, 0, 0, 0, 6, 7, 7 };

void
stg_ramdac_set_bpp(svga_t *svga, stg_ramdac_t *ramdac)
{
    if (ramdac->command & 0x8) {
        switch (ramdac->regs[3]) {
            default:
            case 0:
            case 5:
            case 7:
                svga->bpp = 8;
                break;
            case 1:
            case 2:
            case 8:
                svga->bpp = 15;
                break;
            case 3:
            case 6:
                svga->bpp = 16;
                break;
            case 4:
            case 9:
                svga->bpp = 24;
                break;
        }
    } else {
        switch (ramdac->command >> 5) {
            default:
            case 0:
                svga->bpp = 8;
                break;
            case 5:
                svga->bpp = 15;
                break;
            case 6:
                svga->bpp = 16;
                break;
            case 7:
                svga->bpp = 24;
                break;
        }
    }

    svga_recalctimings(svga);
}

void
stg_ramdac_out(uint16_t addr, uint8_t val, void *priv, svga_t *svga)
{
    stg_ramdac_t *ramdac = (stg_ramdac_t *) priv;
    int           didwrite;
    int           old;

    switch (addr) {
        case 0x3c6:
            switch (ramdac->magic_count) {
                /* 0 = PEL mask register */
                case 0:
                case 1:
                case 2:
                case 3:
                    break;
                case 4: /* REG06 */
                    old             = ramdac->command;
                    ramdac->command = val;
                    if ((old ^ val) & 8)
                        stg_ramdac_set_bpp(svga, ramdac);
                    else {
                        if ((old ^ val) & 0xE0)
                            stg_ramdac_set_bpp(svga, ramdac);
                    }
                    break;
                case 5:
                    ramdac->index = (ramdac->index & 0xff00) | val;
                    break;
                case 6:
                    ramdac->index = (ramdac->index & 0xff) | (val << 8);
                    break;
                case 7:
                    if (ramdac->index < 0x100)
                        ramdac->regs[ramdac->index] = val;
                    if ((ramdac->index == 3) && (ramdac->command & 8))
                        stg_ramdac_set_bpp(svga, ramdac);
                    ramdac->index++;
                    break;

                default:
                    break;
            }
            didwrite            = (ramdac->magic_count >= 4);
            ramdac->magic_count = stg_state_write[ramdac->magic_count & 7];
            if (didwrite)
                return;
            break;
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            ramdac->magic_count = 0;
            break;

        default:
            break;
    }

    svga_out(addr, val, svga);
}

uint8_t
stg_ramdac_in(uint16_t addr, void *priv, svga_t *svga)
{
    stg_ramdac_t *ramdac = (stg_ramdac_t *) priv;
    uint8_t       temp   = 0xff;

    switch (addr) {
        case 0x3c6:
            switch (ramdac->magic_count) {
                case 0:
                case 1:
                case 2:
                case 3:
                    temp = 0xff;
                    break;
                case 4:
                    temp = ramdac->command;
                    break;
                case 5:
                    temp = ramdac->index & 0xff;
                    break;
                case 6:
                    temp = ramdac->index >> 8;
                    break;
                case 7:
                    switch (ramdac->index) {
                        case 0:
                            temp = 0x44;
                            break;
                        case 1:
                            temp = 0x03;
                            break;
                        case 7:
                            temp = 0x88;
                            break;
                        default:
                            if (ramdac->index < 0x100)
                                temp = ramdac->regs[ramdac->index];
                            else
                                temp = 0xff;
                            break;
                    }
                    ramdac->index++;
                    break;

                default:
                    break;
            }
            ramdac->magic_count = stg_state_read[(ramdac->command & 0x10) ? 1 : 0][ramdac->magic_count & 7];
            return temp;
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            ramdac->magic_count = 0;
            break;

        default:
            break;
    }

    return svga_in(addr, svga);
}

float
stg_getclock(int clock, void *priv)
{
    stg_ramdac_t   *ramdac = (stg_ramdac_t *) priv;
    float           t;
    int             m;
    int             n;
    int             n2;
    const uint16_t *c;

    if (clock == 0)
        return 25175000.0;
    if (clock == 1)
        return 28322000.0;

    clock ^= 1; /*Clocks 2 and 3 seem to be reversed*/
    c  = (uint16_t *) &ramdac->regs[0x20 + (clock << 1)];
    m  = (*c & 0xff) + 2;        /* B+2 */
    n  = ((*c >> 8) & 0x1f) + 2; /* N1+2 */
    n2 = ((*c >> 13) & 0x07);    /* D */
    n2 = (1 << n2);
    t  = (14318184.0f * (float) m) / (float) (n * n2);

    return t;
}

static void *
stg_ramdac_init(UNUSED(const device_t *info))
{
    stg_ramdac_t *ramdac = (stg_ramdac_t *) malloc(sizeof(stg_ramdac_t));
    memset(ramdac, 0, sizeof(stg_ramdac_t));

    return ramdac;
}

static void
stg_ramdac_close(void *priv)
{
    stg_ramdac_t *ramdac = (stg_ramdac_t *) priv;

    if (ramdac)
        free(ramdac);
}

const device_t stg_ramdac_device = {
    .name          = "SGS-Thompson STG170x RAMDAC",
    .internal_name = "stg_ramdac",
    .flags         = 0,
    .local         = 0,
    .init          = stg_ramdac_init,
    .close         = stg_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
