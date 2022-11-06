/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Tseng Labs ET3000.
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

#define BIOS_ROM_PATH "roms/video/et3000/Tseng ET3000AX ISA VGA-VGA ULTRA.bin"

typedef struct {
    const char *name;
    int         type;

    svga_t svga;

    rom_t bios_rom;

    uint8_t banking;
} et3000_t;

static video_timings_t timing_et3000_isa = { VIDEO_ISA, 3, 3, 6, 5, 5, 10 };

static uint8_t et3000_in(uint16_t addr, void *priv);
static void    et3000_out(uint16_t addr, uint8_t val, void *priv);

static uint8_t
et3000_in(uint16_t addr, void *priv)
{
    et3000_t *dev  = (et3000_t *) priv;
    svga_t   *svga = &dev->svga;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3cd: /*Banking*/
            return dev->banking;

        case 0x3d4:
            return svga->crtcreg;

        case 0x3d5:
            return svga->crtc[svga->crtcreg];
    }

    return svga_in(addr, svga);
}

static void
et3000_out(uint16_t addr, uint8_t val, void *priv)
{
    et3000_t *dev  = (et3000_t *) priv;
    svga_t   *svga = &dev->svga;
    uint8_t   old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c0:
        case 0x3c1:
            if (svga->attrff && (svga->attraddr == 0x16)) {
                svga->attrregs[0x16] = val;
                svga->chain4 &= ~0x10;
                if (svga->gdcreg[5] & 0x40)
                    svga->chain4 |= (svga->attrregs[0x16] & 0x10);
                svga_recalctimings(svga);
            }
            break;

        case 0x3c5:
            if (svga->seqaddr == 4) {
                svga->seqregs[4] = val;

                svga->chain2_write = !(val & 4);
                svga->chain4       = (svga->chain4 & ~8) | (val & 8);
                svga->fast         = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) && svga->chain4 && !(svga->adv_flags & FLAG_ADDR_BY8);
                return;
            }
            break;

        case 0x3cf:
            if ((svga->gdcaddr & 15) == 5) {
                svga->chain4 &= ~0x10;
                if (val & 0x40)
                    svga->chain4 |= (svga->attrregs[0x16] & 0x10);
            }
            break;

        case 0x3cd: /*Banking*/
            dev->banking = val;
            if (!(svga->crtc[0x23] & 0x80) && !(svga->gdcreg[6] & 0x08)) {
                switch ((val >> 6) & 3) {
                    case 0: /*128K segments*/
                        svga->write_bank = (val & 7) << 17;
                        svga->read_bank  = ((val >> 3) & 7) << 17;
                        break;
                    case 1: /*64K segments*/
                        svga->write_bank = (val & 7) << 16;
                        svga->read_bank  = ((val >> 3) & 7) << 16;
                        break;
                }
            }
            return;

        case 0x3d4:
            svga->crtcreg = val & 0x3f;
            return;

        case 0x3d5:
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;

            if (old != val) {
                if (svga->crtcreg < 0x0e || svga->crtcreg > 0x10) {
                    svga->fullchange = changeframecount;
                    svga_recalctimings(svga);
                }
            }
            break;
    }

    svga_out(addr, val, svga);
}

static void
et3000_recalctimings(svga_t *svga)
{
    svga->ma_latch |= (svga->crtc[0x23] & 2) << 15;
    if (svga->crtc[0x25] & 1)
        svga->vblankstart |= 0x400;
    if (svga->crtc[0x25] & 2)
        svga->vtotal |= 0x400;
    if (svga->crtc[0x25] & 4)
        svga->dispend |= 0x400;
    if (svga->crtc[0x25] & 8)
        svga->vsyncstart |= 0x400;
    if (svga->crtc[0x25] & 0x10)
        svga->split |= 0x400;

    svga->interlace = !!(svga->crtc[0x25] & 0x80);

    if (svga->attrregs[0x16] & 0x10) {
        svga->ma_latch <<= (1 << 0);
        svga->rowoffset <<= (1 << 0);
        switch (svga->gdcreg[5] & 0x60) {
            case 0x00:
                svga->render = svga_render_4bpp_highres;
                svga->hdisp *= 2;
                break;
            case 0x20:
                svga->render = svga_render_2bpp_highres;
                break;
            case 0x40:
            case 0x60:
                svga->render = svga_render_8bpp_highres;
                break;
        }
    }

    /* pclog("HDISP = %i, HTOTAL = %i, ROWOFFSET = %i, INTERLACE = %i\n",
          svga->hdisp, svga->htotal, svga->rowoffset, svga->interlace); */

    switch (((svga->miscout >> 2) & 3) | ((svga->crtc[0x24] << 1) & 4)) {
        case 0:
        case 1:
            break;
        case 3:
            svga->clock = (cpuclock * (double) (1ull << 32)) / 40000000.0;
            break;
        case 5:
            svga->clock = (cpuclock * (double) (1ull << 32)) / 65000000.0;
            break;
        default:
            svga->clock = (cpuclock * (double) (1ull << 32)) / 36000000.0;
            break;
    }
}

static void *
et3000_init(const device_t *info)
{
    const char *fn;
    et3000_t   *dev;

    dev = (et3000_t *) malloc(sizeof(et3000_t));
    memset(dev, 0x00, sizeof(et3000_t));
    dev->name = info->name;
    dev->type = info->local;
    fn        = BIOS_ROM_PATH;

    switch (dev->type) {
        case 0: /* ISA ET3000AX */
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et3000_isa);
            svga_init(info, &dev->svga, dev, device_get_config_int("memory") << 10,
                      et3000_recalctimings, et3000_in, et3000_out,
                      NULL, NULL);
            io_sethandler(0x03c0, 32,
                          et3000_in, NULL, NULL, et3000_out, NULL, NULL, dev);
            break;
    }

    rom_init(&dev->bios_rom, (char *) fn,
             0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    dev->svga.bpp     = 8;
    dev->svga.miscout = 1;

    dev->svga.packed_chain4 = 1;

    return (dev);
}

static void
et3000_close(void *priv)
{
    et3000_t *dev = (et3000_t *) priv;

    svga_close(&dev->svga);

    free(dev);
}

static void
et3000_speed_changed(void *priv)
{
    et3000_t *dev = (et3000_t *) priv;

    svga_recalctimings(&dev->svga);
}

static void
et3000_force_redraw(void *priv)
{
    et3000_t *dev = (et3000_t *) priv;

    dev->svga.fullchange = changeframecount;
}

static int
et3000_available(void)
{
    return rom_present(BIOS_ROM_PATH);
}

static const device_config_t et3000_config[] = {
    { .name        = "memory",
     .description = "Memory size",
     .type        = CONFIG_SELECTION,
     .default_int = 512,
     .selection   = {
          { .description = "256 KB",
              .value       = 256 },
          { .description = "512 KB",
              .value       = 512 },
          { .description = "1 MB",
              .value       = 1024 },
          { .description = "" } } },
    { .type = CONFIG_END }
};

const device_t et3000_isa_device = {
    .name          = "Tseng Labs ET3000AX (ISA)",
    .internal_name = "et3000ax",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = et3000_init,
    .close         = et3000_close,
    .reset         = NULL,
    { .available = et3000_available },
    .speed_changed = et3000_speed_changed,
    .force_redraw  = et3000_force_redraw,
    .config        = et3000_config
};
