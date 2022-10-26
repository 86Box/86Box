/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Epson Equity LT portable computer emulation.
 *
 * Author:	Lubomir Rintel, <lkundrak@v3.sk>
 *
 *		Copyright 2022 Lubomir Rintel.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS	FOR A PARTICULAR  PURPOSE. See	the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */

// clang-format off
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/nvr.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
// clang-format on

static void
elt_vid_off_poll(void *p)
{
    cga_t  *cga   = p;
    uint8_t hdisp = cga->crtc[1];

    /* Don't display anything.
     * TODO: Do something less stupid to emulate backlight off. */
    cga->crtc[1] = 0;
    cga_poll(cga);
    cga->crtc[1] = hdisp;
}

static void
sysstat_out(uint16_t port, uint8_t val, void *p)
{
    cga_t *cga = p;

    switch (val) {
        case 0:
            break;
        case 1:
            /* Backlight off. */
            if (cga)
                timer_set_callback(&cga->timer, elt_vid_off_poll);
            break;
        case 2:
            /* Backlight on. */
            if (cga)
                timer_set_callback(&cga->timer, cga_poll);
            break;
        default:
            pclog("Unknown sysstat command: 0x%02x\n", val);
    }
}

static uint8_t
sysstat_in(uint16_t port, void *p)
{
    cga_t  *cga = p;
    uint8_t ret = 0x0a; /* No idea what these bits are */

    /* External CRT. We don't emulate the LCD/CRT switching, let's just
     * frivolously use this bit to indicate we're using the LCD if the
     * user didn't override the video card for now. */
    if (cga == NULL)
        ret |= 0x40;

    return ret;
}

static void
elt_vid_out(uint16_t addr, uint8_t val, void *p)
{
    cga_t *cga = p;

    /* The Equity LT chipset's CRTC contains more registers than the
     * regular CGA. The BIOS writes one of them, register 36 (0x24).
     * Nothing is known about the number or function of those registers,
     * let's just ignore them so that we don't clobber the CGA register.
     * Also, the BIOS writes that register via the 3D0h/3D1h alias
     * instead of the usual 3D4h/3D5h, possibly to keep the wraparound
     * behavior on the usual addresses (just an assumption, not
     * verified). */
    switch (addr) {
        case 0x3d0:
            cga->crtcreg = val;
            return;
        case 0x3d1:
            if (cga->crtcreg >= 32)
                return;
            /* FALLTHROUGH */
        default:
            cga->crtcreg &= 31;
            cga_out(addr, val, p);
    }
}

static uint8_t
elt_vid_in(uint16_t addr, void *p)
{
    cga_t *cga = p;

    /* Just make sure we don't ever let regular CGA code run with crtcreg
     * pointing out of crtcregs[] bounds. */
    cga->crtcreg &= 31;
    return cga_in(addr, p);
}

static void
load_font_rom(uint32_t font_data)
{
    int c, d;
    for (c = 0; c < 256; c++)
        for (d = 0; d < 8; d++)
            fontdat[c][d] = mem_readb_phys(font_data++);
}

int
machine_elt_init(const machine_t *model)
{
    cga_t *cga = NULL;
    int    ret;

    ret = bios_load_interleavedr("roms/machines/elt/HLO-B2.rom",
                                 "roms/machines/elt/HLO-A2.rom",
                                 0x000fc000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    /* The machine doesn't have any separate font ROM chip. The text mode
     * font is likely a mask ROM in the chipset. video_reset() will try
     * to load a MDA font, but let's have a reasonable fall back if it's
     * not available. Read in the graphical mode font from the BIOS ROM
     * image. */
    load_font_rom(0xffa6e);

    machine_common_init(model);

    nmi_init();

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    if (fdc_type == FDC_INTERNAL)
        device_add(&fdc_xt_device);

    if (gfxcard == VID_INTERNAL) {
        cga = device_add(&cga_device);
        io_removehandler(0x03d0, 0x0010, cga_in, NULL, NULL, cga_out, NULL, NULL, cga);
        io_sethandler(0x03d0, 0x0010, elt_vid_in, NULL, NULL, elt_vid_out, NULL, NULL, cga);
    }

    /* Keyboard goes after the video, because on XT compatibles it's dealt
     * with by the same PPI as the config switches and we need them to
     * indicate the correct display type */
    device_add(&keyboard_xt_device);

    device_add(&elt_nvr_device);

    io_sethandler(0x11b8, 1, sysstat_in, NULL, NULL, sysstat_out, NULL, NULL, cga);

    return ret;
}
