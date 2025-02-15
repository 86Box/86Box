/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI 18800 emulation (VGA Edge-16)
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
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
#include <86box/vid_ati_eeprom.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

#define BIOS_ROM_PATH_WONDER         "roms/video/ati18800/VGA_Wonder_V3-1.02.bin"
#define BIOS_ROM_PATH_VGA88          "roms/video/ati18800/vga88.bin"
#define BIOS_ROM_PATH_EDGE16         "roms/video/ati18800/vgaedge16.vbi"

enum {
    ATI18800_WONDER = 0,
    ATI18800_VGA88,
    ATI18800_EDGE16
};

typedef struct ati18800_t {
    svga_t       svga;
    ati_eeprom_t eeprom;

    rom_t bios_rom;

    uint8_t regs[256];
    int     index;
    int     type;
    uint32_t memory;
} ati18800_t;

static video_timings_t timing_ati18800 = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

static void
ati18800_out(uint16_t addr, uint8_t val, void *priv)
{
    ati18800_t *ati18800 = (ati18800_t *) priv;
    svga_t     *svga     = &ati18800->svga;
    uint8_t     old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x1ce:
            ati18800->index = val;
            break;
        case 0x1cf:
            old                             = ati18800->regs[ati18800->index];
            ati18800->regs[ati18800->index] = val;
            switch (ati18800->index) {
                case 0xb0:
                    if ((old ^ val) & 6)
                        svga_recalctimings(svga);
                    break;
                case 0xb2:
                case 0xbe:
                    if (ati18800->regs[0xbe] & 8) { /*Read/write bank mode*/
                        svga->read_bank  = ((ati18800->regs[0xb2] & 0xe0) >> 5) * 0x10000;
                        svga->write_bank = ((ati18800->regs[0xb2] & 0x0e) >> 1) * 0x10000;
                    } else /*Single bank mode*/
                        svga->read_bank = svga->write_bank = ((ati18800->regs[0xb2] & 0x0e) >> 1) * 0x10000;
                    break;
                case 0xb3:
                    ati_eeprom_write(&ati18800->eeprom, val & 8, val & 2, val & 1);
                    break;

                default:
                    break;
            }
            break;

        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80) && !(ati18800->regs[0xb4] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80) && !(ati18800->regs[0xb4] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;
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

        default:
            break;
    }
    svga_out(addr, val, svga);
}

static uint8_t
ati18800_in(uint16_t addr, void *priv)
{
    ati18800_t *ati18800 = (ati18800_t *) priv;
    svga_t     *svga     = &ati18800->svga;
    uint8_t     temp     = 0xff;

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x1ce:
            temp = ati18800->index;
            break;
        case 0x1cf:
            switch (ati18800->index) {
                case 0xb7:
                    temp = ati18800->regs[ati18800->index] & ~8;
                    if (ati_eeprom_read(&ati18800->eeprom))
                        temp |= 8;
                    break;
                default:
                    temp = ati18800->regs[ati18800->index];
                    break;
            }
            break;

        case 0x3D4:
            temp = svga->crtcreg;
            break;
        case 0x3D5:
            temp = svga->crtc[svga->crtcreg];
            break;
        default:
            temp = svga_in(addr, svga);
            break;
    }
    return temp;
}

static void
ati18800_recalctimings(svga_t *svga)
{
    const ati18800_t *ati18800 = (ati18800_t *) svga->priv;
    int               clock_sel;

    clock_sel = ((svga->miscout >> 2) & 3) | ((ati18800->regs[0xbe] & 0x10) >> 1) | ((ati18800->regs[0xb9] & 2) << 1);

    if (ati18800->regs[0xb6] & 0x10) {
        svga->hdisp <<= 1;
        svga->htotal <<= 1;
        svga->rowoffset <<= 1;
        svga->gdcreg[5] &= ~0x40;
    }

    if (ati18800->regs[0xb0] & 6) {
        svga->gdcreg[5] |= 0x40;
        if ((ati18800->regs[0xb6] & 0x18) >= 0x10)
            svga->packed_4bpp = 1;
        else
            svga->packed_4bpp = 0;
    } else
        svga->packed_4bpp = 0;

    if ((ati18800->regs[0xb6] & 0x18) == 8) {
        svga->hdisp <<= 1;
        svga->htotal <<= 1;
        svga->ati_4color = 1;
    } else
        svga->ati_4color = 0;


    if (!svga->scrblank && (svga->crtc[0x17] & 0x80) && svga->attr_palette_enable) {
         if ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1)) {
            svga->clock = (cpuclock * (double) (1ULL << 32)) / svga->getclock(clock_sel, svga->clock_gen);
            switch (svga->gdcreg[5] & 0x60) {
                case 0x00:
                    if (svga->seqregs[1] & 8) /*Low res (320)*/
                        svga->render = svga_render_4bpp_lowres;
                    else
                        svga->render = svga_render_4bpp_highres;
                    break;
                case 0x20:                    /*4 colours*/
                    if (svga->seqregs[1] & 8) /*Low res (320)*/
                        svga->render = svga_render_2bpp_lowres;
                    else
                        svga->render = svga_render_2bpp_highres;
                    break;
                case 0x40:
                case 0x60: /*256+ colours*/
                    switch (svga->bpp) {
                        default:
                        case 8:
                            svga->map8 = svga->pallook;
                            if (svga->lowres)
                                svga->render = svga_render_8bpp_lowres;
                            else {
                                svga->render = svga_render_8bpp_highres;
                                if (!svga->packed_4bpp) {
                                    svga->ma_latch <<= 1;
                                    svga->rowoffset <<= 1;
                                }
                            }
                            break;
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

static void *
ati18800_init(const device_t *info)
{
    ati18800_t *ati18800 = malloc(sizeof(ati18800_t));
    memset(ati18800, 0, sizeof(ati18800_t));

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ati18800);

    ati18800->type = info->local;

    switch (info->local) {
        default:
        case ATI18800_WONDER:
            rom_init(&ati18800->bios_rom, BIOS_ROM_PATH_WONDER, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            ati18800->memory = device_get_config_int("memory");
            break;
        case ATI18800_VGA88:
            rom_init(&ati18800->bios_rom, BIOS_ROM_PATH_VGA88, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            ati18800->memory = 256;
            break;
        case ATI18800_EDGE16:
            rom_init(&ati18800->bios_rom, BIOS_ROM_PATH_EDGE16, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            ati18800->memory = 512;
            break;
    }

    svga_init(info, &ati18800->svga, ati18800, ati18800->memory << 10,
              ati18800_recalctimings,
              ati18800_in, ati18800_out,
              NULL,
              NULL);
    ati18800->svga.clock_gen = device_add(&ati18810_device);
    ati18800->svga.getclock  = ics2494_getclock;

    io_sethandler(0x01ce, 0x0002, ati18800_in, NULL, NULL, ati18800_out, NULL, NULL, ati18800);
    io_sethandler(0x03c0, 0x0020, ati18800_in, NULL, NULL, ati18800_out, NULL, NULL, ati18800);

    ati18800->svga.miscout = 1;
    ati18800->svga.bpp = 8;

    ati_eeprom_load(&ati18800->eeprom, "ati18800.nvr", 0);

    return ati18800;
}

static int
ati18800_wonder_available(void)
{
    return rom_present(BIOS_ROM_PATH_WONDER);
}

static int
ati18800_vga88_available(void)
{
    return rom_present(BIOS_ROM_PATH_VGA88);
}

static int
ati18800_available(void)
{
    return rom_present(BIOS_ROM_PATH_EDGE16);
}

static void
ati18800_close(void *priv)
{
    ati18800_t *ati18800 = (ati18800_t *) priv;

    svga_close(&ati18800->svga);

    free(ati18800);
}

static void
ati18800_speed_changed(void *priv)
{
    ati18800_t *ati18800 = (ati18800_t *) priv;

    svga_recalctimings(&ati18800->svga);
}

static void
ati18800_force_redraw(void *priv)
{
    ati18800_t *ati18800 = (ati18800_t *) priv;

    ati18800->svga.fullchange = changeframecount;
}

static const device_config_t ati18800_wonder_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 512,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection = {
            { .description = "256 KB", .value = 256 },
            { .description = "512 KB", .value = 512 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t ati18800_wonder_device = {
    .name          = "ATI-18800",
    .internal_name = "ati18800w",
    .flags         = DEVICE_ISA,
    .local         = ATI18800_WONDER,
    .init          = ati18800_init,
    .close         = ati18800_close,
    .reset         = NULL,
    .available     = ati18800_wonder_available,
    .speed_changed = ati18800_speed_changed,
    .force_redraw  = ati18800_force_redraw,
    .config        = ati18800_wonder_config
};

const device_t ati18800_vga88_device = {
    .name          = "ATI 18800-1",
    .internal_name = "ati18800v",
    .flags         = DEVICE_ISA,
    .local         = ATI18800_VGA88,
    .init          = ati18800_init,
    .close         = ati18800_close,
    .reset         = NULL,
    .available     = ati18800_vga88_available,
    .speed_changed = ati18800_speed_changed,
    .force_redraw  = ati18800_force_redraw,
    .config        = NULL
};

const device_t ati18800_device = {
    .name          = "ATI VGA Edge 16",
    .internal_name = "ati18800",
    .flags         = DEVICE_ISA,
    .local         = ATI18800_EDGE16,
    .init          = ati18800_init,
    .close         = ati18800_close,
    .reset         = NULL,
    .available     = ati18800_available,
    .speed_changed = ati18800_speed_changed,
    .force_redraw  = ati18800_force_redraw,
    .config        = NULL
};
