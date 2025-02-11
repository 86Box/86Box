/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Mach32-compatible ATI 68875 RAMDAC and clones.
 *
 *
 *
 * Authors: TheCollector1995.
 *
 *          Copyright 2022-2023 TheCollector1995.
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
#include <86box/vid_svga_render.h>
#include <86box/plat_unused.h>

typedef struct ati68875_ramdac_t {
    uint8_t gen_cntl;
    uint8_t in_clk_sel;
    uint8_t out_clk_sel;
    uint8_t mux_cntl;
    uint8_t palette_page_sel;
    uint8_t test_reg;
} ati68875_ramdac_t;

void
ati68875_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, void *priv, svga_t *svga)
{
    ati68875_ramdac_t *ramdac = (ati68875_ramdac_t *) priv;
    uint8_t rs = (addr & 0x03);

    rs |= (!!rs2 << 2);
    rs |= (!!rs3 << 3);

    switch (rs) {
        case 0x00: /* Palette Write Index Register (RS value = 0000) */
        case 0x01: /* Palette Data Register (RS value = 0001) */
        case 0x02: /* Pixel Read Mask Register (RS value = 0010) */
        case 0x03:
            svga_out(addr, val, svga);
            break;
        case 0x08: /* General Control Register (RS value = 1000) */
            ramdac->gen_cntl = val;
            break;
        case 0x09: /* Input Clock Selection Register (RS value = 1001) */
            ramdac->in_clk_sel = val;
            break;
        case 0x0a: /* Output Clock Selection Register (RS value = 1010) */
            ramdac->out_clk_sel = val;
            break;
        case 0x0b: /* MUX Control Register (RS value = 1011) */
            ramdac->mux_cntl = val;
            break;
        case 0x0c: /* Palette Page Register (RS value = 1100) */
            ramdac->palette_page_sel = val;
            break;
        case 0x0e: /* Test Register (RS value = 1110) */
            ramdac->test_reg = val;
            break;
        case 0x0f: /* Reset State (RS value = 1111) */
            ramdac->mux_cntl = 0x2d;
            break;

        default:
            break;
    }

    return;
}

uint8_t
ati68875_ramdac_in(uint16_t addr, int rs2, int rs3, void *priv, svga_t *svga)
{
    const ati68875_ramdac_t *ramdac = (ati68875_ramdac_t *) priv;
    uint8_t                  rs = (addr & 0x03);
    uint8_t                  temp = 0;

    rs |= (!!rs2 << 2);
    rs |= (!!rs3 << 3);

    switch (rs) {
        case 0x00: /* Palette Write Index Register (RS value = 0000) */
        case 0x01: /* Palette Data Register (RS value = 0001) */
        case 0x02: /* Pixel Read Mask Register (RS value = 0010) */
        case 0x03:
            temp = svga_in(addr, svga);
            break;
        case 0x08: /* General Control Register (RS value = 1000) */
            temp = ramdac->gen_cntl;
            break;
        case 0x09: /* Input Clock Selection Register (RS value = 1001) */
            temp = ramdac->in_clk_sel;
            break;
        case 0x0a: /* Output Clock Selection Register (RS value = 1010) */
            temp = ramdac->out_clk_sel;
            break;
        case 0x0b: /* MUX Control Register (RS value = 1011) */
            temp = ramdac->mux_cntl;
            break;
        case 0x0c: /* Palette Page Register (RS value = 1100) */
            temp = ramdac->palette_page_sel;
            break;
        case 0x0e: /* Test Register (RS value = 1110) */
            switch (ramdac->test_reg & 0x07) {
                case 0x03:
                    temp = 0x75;
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    return temp;
}

static void *
ati68875_ramdac_init(UNUSED(const device_t *info))
{
    ati68875_ramdac_t *ramdac = (ati68875_ramdac_t *) malloc(sizeof(ati68875_ramdac_t));
    memset(ramdac, 0, sizeof(ati68875_ramdac_t));

    ramdac->mux_cntl = 0x2d;

    return ramdac;
}

static void
ati68875_ramdac_close(void *priv)
{
    ati68875_ramdac_t *ramdac = (ati68875_ramdac_t *) priv;

    if (ramdac)
        free(ramdac);
}

const device_t ati68875_ramdac_device = {
    .name          = "ATI 68875 RAMDAC",
    .internal_name = "ati68875_ramdac",
    .flags         = 0,
    .local         = 0,
    .init          = ati68875_ramdac_init,
    .close         = ati68875_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
