/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Realtek RTG series of VGA ISA chips.
 *
 *
 *
 * Authors: TheCollector1995, <mariogplayer90@gmail.com>
 *
 *          Copyright 2021 TheCollector1995.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

#define RTG_3105_BIOS_ROM_PATH "roms/video/rtg/RTG3105I.VBI"
#define RTG_3106_BIOS_ROM_PATH "roms/video/rtg/realtekrtg3106.BIN"

typedef struct {
    const char *name;
    int         type;

    svga_t svga;

    rom_t bios_rom;

    uint8_t bank3d6,
        bank3d7;

    uint32_t vram_size,
        vram_mask;
} rtg_t;

static video_timings_t timing_rtg_isa = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l = 6, .read_b = 5, .read_w = 5, .read_l = 10 };

static void
rtg_recalcbanking(rtg_t *dev)
{
    svga_t *svga = &dev->svga;

    svga->write_bank = (dev->bank3d7 & 0x0f) * 65536;

    if (svga->gdcreg[0x0f] & 4)
        svga->read_bank = (dev->bank3d6 & 0x0f) * 65536;
    else
        svga->read_bank = svga->write_bank;
}

static uint8_t
rtg_in(uint16_t addr, void *priv)
{
    rtg_t  *dev  = (rtg_t *) priv;
    svga_t *svga = &dev->svga;
    uint8_t ret  = 0;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3ce:
            return svga->gdcaddr;

        case 0x3cf:
            if (svga->gdcaddr == 0x0c)
                return svga->gdcreg[0x0c] | 4;
            else if ((svga->gdcaddr > 8) && (svga->gdcaddr != 0x0c))
                return svga->gdcreg[svga->gdcaddr];
            break;

        case 0x3d4:
            return svga->crtcreg;

        case 0x3d5:
            if (svga->crtcreg == 0x1a)
                return dev->type << 6;
            if (svga->crtcreg == 0x1e) {
                ret = svga->crtc[0x1e];
                ret &= ~3;
                if (dev->vram_size == 1024)
                    ret = 2;
                else if (dev->vram_size == 512)
                    ret = 1;
                else
                    ret = 0;
                return ret;
            }
            return svga->crtc[svga->crtcreg];

        case 0x3d6:
            return dev->bank3d6;

        case 0x3d7:
            return dev->bank3d7;

        default:
            break;
    }

    return svga_in(addr, svga);
}

static void
rtg_out(uint16_t addr, uint8_t val, void *priv)
{
    rtg_t  *dev  = (rtg_t *) priv;
    svga_t *svga = &dev->svga;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3ce:
            svga->gdcaddr = val;
            return;

        case 0x3cf:
            if (svga->gdcaddr > 8) {
                svga->gdcreg[svga->gdcaddr] = val;

                switch (svga->gdcaddr) {
                    case 0x0b:
                    case 0x0c:
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                        break;

                    case 0x0f:
                        rtg_recalcbanking(dev);
                        return;

                    default:
                        break;
                }
            }
            break;

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

            if (svga->crtc[0x1e] & 0x80) {
                switch (svga->crtcreg) {
                    case 0x19:
                        svga->vram_display_mask = (val & 0x20) ? dev->vram_mask : 0x3ffff;
                        svga->fullchange        = changeframecount;
                        svga_recalctimings(svga);
                        break;

                    default:
                        break;
                }
            }

            if (old != val) {
                if (svga->crtcreg < 0xe || svga->crtcreg > 0x10) {
                    if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                        svga->fullchange = 3;
                        svga->ma_latch   = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            break;

        case 0x3d6:
            dev->bank3d6 = val;
            rtg_recalcbanking(dev);
            return;

        case 0x3d7:
            dev->bank3d7 = val;
            rtg_recalcbanking(dev);
            return;

        default:
            break;
    }

    svga_out(addr, val, svga);
}

static void
rtg_recalctimings(svga_t *svga)
{
    const rtg_t  *dev  = (rtg_t *) svga->priv;

    svga->ma_latch |= ((svga->crtc[0x19] & 0x10) << 16) | ((svga->crtc[0x19] & 0x40) << 17);

    svga->interlace = (svga->crtc[0x19] & 1);

    /*Clock table not available, currently a guesswork*/
    switch (((svga->miscout >> 2) & 3) | ((svga->gdcreg[0x0c] & 0x20) >> 3)) {
        case 0:
        case 1:
            break;
        case 2:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 36000000.0;
            break;
        case 3:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 65100000.0;
            break;
        case 4:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 44900000.0;
            break;
        case 5:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 50000000.0;
            break;
        case 6:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 80000000.0;
            break;
        case 7:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 75000000.0;
            break;

        default:
            break;
    }

    switch (svga->gdcreg[0x0c] & 3) {
        case 1:
            svga->clock /= 1.5;
            break;
        case 2:
            svga->clock /= 2;
            break;
        case 3:
            svga->clock /= 4;
            break;

        default:
            break;
    }

    if (!svga->scrblank && (svga->crtc[0x17] & 0x80) && svga->attr_palette_enable) {
        if ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1)) {
            switch (svga->gdcreg[5] & 0x60) {
                case 0x00:
                    if (svga->seqregs[1] & 8) /*Low res (320)*/
                        svga->render = svga_render_4bpp_lowres;
                    else {
                        if (svga->hdisp == 1280)
                            svga->rowoffset >>= 1;

                        svga->render = svga_render_4bpp_highres;
                    }
                    break;
                case 0x20:                    /*4 colours*/
                    if (svga->seqregs[1] & 8) /*Low res (320)*/
                        svga->render = svga_render_2bpp_lowres;
                    else
                        svga->render = svga_render_2bpp_highres;
                    break;
                case 0x40:
                case 0x60:
                    if (svga->crtc[0x19] & 2) {
                        if (svga->hdisp == 1280)
                            svga->hdisp >>= 1;
                        else if (dev->type == 2)
                            svga->rowoffset <<= 1;

                        svga->render = svga_render_8bpp_highres;
                    } else
                        svga->render = svga_render_8bpp_lowres;
                    break;

                default:
                    break;
            }
        }
    }
}

static void *
rtg_init(const device_t *info)
{
    const char *fn;
    rtg_t      *dev;

    dev = (rtg_t *) malloc(sizeof(rtg_t));
    memset(dev, 0x00, sizeof(rtg_t));
    dev->name = info->name;
    dev->type = info->local;
    fn = NULL;

    switch (dev->type) {
        case 1: /* ISA RTG3105 */
            fn        = RTG_3105_BIOS_ROM_PATH;
            dev->vram_size = device_get_config_int("memory");
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_rtg_isa);
            svga_init(info, &dev->svga, dev, dev->vram_size << 10,
                      rtg_recalctimings, rtg_in, rtg_out,
                      NULL, NULL);
            io_sethandler(0x03c0, 32,
                          rtg_in, NULL, NULL, rtg_out, NULL, NULL, dev);
            break;
        case 2: /* ISA RTG3106 */
            fn        = RTG_3106_BIOS_ROM_PATH;
            dev->vram_size = device_get_config_int("memory");
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_rtg_isa);
            svga_init(info, &dev->svga, dev, dev->vram_size << 10,
                      rtg_recalctimings, rtg_in, rtg_out,
                      NULL, NULL);
            io_sethandler(0x03c0, 32,
                          rtg_in, NULL, NULL, rtg_out, NULL, NULL, dev);
            break;

        default:
            break;
    }

    dev->svga.bpp     = 8;
    dev->svga.miscout = 1;

    dev->vram_mask = dev->vram_size - 1;

    rom_init(&dev->bios_rom, fn,
             0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return dev;
}

static void
rtg_close(void *priv)
{
    rtg_t *dev = (rtg_t *) priv;

    svga_close(&dev->svga);

    free(dev);
}

static void
rtg_speed_changed(void *priv)
{
    rtg_t *dev = (rtg_t *) priv;

    svga_recalctimings(&dev->svga);
}

static void
rtg_force_redraw(void *priv)
{
    rtg_t *dev = (rtg_t *) priv;

    dev->svga.fullchange = changeframecount;
}

static int
rtg3105_available(void)
{
    return rom_present(RTG_3105_BIOS_ROM_PATH);
}

static int
rtg3106_available(void)
{
    return rom_present(RTG_3106_BIOS_ROM_PATH);
}

static const device_config_t rtg3105_config[] = {
  // clang-format off
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 512,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "256 KB", .value = 256 },
            { .description = "512 KB", .value = 512 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t rtg3106_config[] = {
  // clang-format off
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1024,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "256 KB", .value =  256 },
            { .description = "512 KB", .value =  512 },
            { .description = "1 MB",   .value = 1024 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t realtek_rtg3105_device = {
    .name          = "Realtek RTG3105 (ISA)",
    .internal_name = "rtg3105",
    .flags         = DEVICE_ISA,
    .local         = 1,
    .init          = rtg_init,
    .close         = rtg_close,
    .reset         = NULL,
    .available     = rtg3105_available,
    .speed_changed = rtg_speed_changed,
    .force_redraw  = rtg_force_redraw,
    .config        = rtg3105_config
};

const device_t realtek_rtg3106_device = {
    .name          = "Realtek RTG3106 (ISA)",
    .internal_name = "rtg3106",
    .flags         = DEVICE_ISA,
    .local         = 2,
    .init          = rtg_init,
    .close         = rtg_close,
    .reset         = NULL,
    .available     = rtg3106_available,
    .speed_changed = rtg_speed_changed,
    .force_redraw  = rtg_force_redraw,
    .config        = rtg3106_config
};
