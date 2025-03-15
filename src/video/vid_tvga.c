/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Trident TVGA (8900D) emulation.
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
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

#define TVGA8900B_ID              0x03
#define TVGA9000B_ID              0x23
#define TVGA8900CLD_ID            0x33

#define ROM_TVGA_8900B            "roms/video/tvga/tvga8900b.vbi"
#define ROM_TVGA_8900CLD          "roms/video/tvga/trident.bin"
#define ROM_TVGA_8900DR           "roms/video/tvga/8900DR.VBI"
#define ROM_TVGA_9000B            "roms/video/tvga/tvga9000b.bin"
#define ROM_TVGA_9000B_NEC_SV9000 "roms/video/tvga/SV9000.VBI"

typedef struct tvga_t {
    mem_mapping_t linear_mapping;
    mem_mapping_t accel_mapping;

    svga_t svga;

    rom_t   bios_rom;
    uint8_t card_id;

    uint8_t tvga_3d8, tvga_3d9;
    int     oldmode;
    uint8_t oldctrl1;
    uint8_t oldctrl2, newctrl2;

    int      vram_size;
    uint32_t vram_mask;
} tvga_t;

video_timings_t timing_tvga8900 = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l = 6, .read_b = 8, .read_w = 8, .read_l = 12 };
video_timings_t timing_tvga8900dr = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l = 6, .read_b = 5, .read_w = 5, .read_l = 10 };
video_timings_t timing_tvga9000 = { .type = VIDEO_ISA, .write_b = 7, .write_w = 7, .write_l = 12, .read_b = 7, .read_w = 7, .read_l = 12 };

static uint8_t crtc_mask[0x40] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x7f, 0xff, 0x3f, 0x7f, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xef,
    0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
    0x7f, 0x00, 0x00, 0x2f, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void tvga_recalcbanking(tvga_t *tvga);
void
tvga_out(uint16_t addr, uint8_t val, void *priv)
{
    tvga_t *tvga = (tvga_t *) priv;
    svga_t *svga = &tvga->svga;

    uint8_t old;

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3C5:
            switch (svga->seqaddr & 0xf) {
                case 0xB:
                    tvga->oldmode = 1;
                    break;
                case 0xC:
                    if (svga->seqregs[0xe] & 0x80)
                        svga->seqregs[0xc] = val;
                    break;
                case 0xd:
                    if (tvga->oldmode)
                        tvga->oldctrl2 = val;
                    else {
                        tvga->newctrl2 = val;
                        svga_recalctimings(svga);
                    }
                    break;
                case 0xE:
                    if (tvga->oldmode)
                        tvga->oldctrl1 = val;
                    else {
                        svga->seqregs[0xe] = val ^ 2;
                        tvga->tvga_3d8     = svga->seqregs[0xe] & 0xf;
                        tvga_recalcbanking(tvga);
                    }
                    return;

                default:
                    break;
            }
            break;

        case 0x3C6:
        case 0x3C7:
        case 0x3C8:
        case 0x3C9:
            if (tvga->card_id != TVGA9000B_ID) {
                tkd8001_ramdac_out(addr, val, svga->ramdac, svga);
                return;
            }
            break;

        case 0x3CF:
            switch (svga->gdcaddr & 15) {
                case 0x6:
                    old = svga->gdcreg[6];
                    svga_out(addr, val, svga);
                    if ((old & 0xc) != 0 && (val & 0xc) == 0) {
                        /*override mask - TVGA supports linear 128k at A0000*/
                        svga->banked_mask = 0x1ffff;
                    }
                    return;
                case 0xE:
                    svga->gdcreg[0xe] = val ^ 2;
                    tvga->tvga_3d9    = svga->gdcreg[0xe] & 0xf;
                    tvga_recalcbanking(tvga);
                    break;
                case 0xF:
                    svga->gdcreg[0xf] = val;
                    tvga_recalcbanking(tvga);
                    break;

                default:
                    break;
            }
            break;
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old = svga->crtc[svga->crtcreg];
            val &= crtc_mask[svga->crtcreg];
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
            switch (svga->crtcreg) {
                case 0x1e:
                    svga->vram_display_mask = (val & 0x80) ? tvga->vram_mask : 0x3ffff;
                    break;

                default:
                    break;
            }
            return;
        case 0x3D8:
            if (svga->gdcreg[0xf] & 4) {
                tvga->tvga_3d8 = val;
                tvga_recalcbanking(tvga);
            }
            return;
        case 0x3D9:
            if (svga->gdcreg[0xf] & 4) {
                tvga->tvga_3d9 = val;
                tvga_recalcbanking(tvga);
            }
            return;
        case 0x3DB:
            if (tvga->card_id != TVGA9000B_ID) {
                /*3db appears to be a 4 bit clock select register on 8900D*/
                svga->miscout  = (svga->miscout & ~0x0c) | ((val & 3) << 2);
                tvga->newctrl2 = (tvga->newctrl2 & ~0x01) | ((val & 4) >> 2);
                tvga->oldctrl1 = (tvga->oldctrl1 & ~0x10) | ((val & 8) << 1);
                svga_recalctimings(svga);
            }
            break;

        default:
            break;
    }
    svga_out(addr, val, svga);
}

uint8_t
tvga_in(uint16_t addr, void *priv)
{
    tvga_t *tvga = (tvga_t *) priv;
    svga_t *svga = &tvga->svga;

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3C5:
            if ((svga->seqaddr & 0xf) == 0xb) {
                tvga->oldmode = 0;
                return tvga->card_id; /*Must be at least a TVGA8900*/
            }
            if ((svga->seqaddr & 0xf) == 0xd) {
                if (tvga->oldmode)
                    return tvga->oldctrl2;
                return tvga->newctrl2;
            }
            if ((svga->seqaddr & 0xf) == 0xe) {
                if (tvga->oldmode)
                    return tvga->oldctrl1;
            }
            break;
        case 0x3C6:
        case 0x3C7:
        case 0x3C8:
        case 0x3C9:
            if (tvga->card_id != TVGA9000B_ID) {
                return tkd8001_ramdac_in(addr, svga->ramdac, svga);
            }
            break;
        case 0x3D4:
            return svga->crtcreg;
        case 0x3D5:
            if (svga->crtcreg > 0x18 && svga->crtcreg < 0x1e)
                return 0xff;
            return svga->crtc[svga->crtcreg];
        case 0x3d8:
            return tvga->tvga_3d8;
        case 0x3d9:
            return tvga->tvga_3d9;

        default:
            break;
    }
    return svga_in(addr, svga);
}

static void
tvga_recalcbanking(tvga_t *tvga)
{
    svga_t *svga = &tvga->svga;

    svga->write_bank = (tvga->tvga_3d8 & 0x1f) * 65536;

    if (svga->gdcreg[0xf] & 1)
        svga->read_bank = (tvga->tvga_3d9 & 0x1f) * 65536;
    else
        svga->read_bank = svga->write_bank;
}

void
tvga_recalctimings(svga_t *svga)
{
    const tvga_t *tvga = (tvga_t *) svga->priv;
    int           clksel;
    int           high_res_256 = 0;

    if (!svga->rowoffset)
        svga->rowoffset = 0x100; /*This is the only sensible way I can see this being handled,
                                   given that TVGA8900D has no overflow bits.
                                   Some sort of overflow is required for 320x200x24 and 1024x768x16*/
    if (svga->crtc[0x29] & 0x10)
        svga->rowoffset += 0x100;

    if (svga->bpp == 24)
        svga->hdisp = (svga->crtc[1] + 1) * 8;

    if ((svga->crtc[0x1e] & 0xA0) == 0xA0)
        svga->ma_latch |= 0x10000;
    if ((svga->crtc[0x27] & 0x01) == 0x01)
        svga->ma_latch |= 0x20000;
    if ((svga->crtc[0x27] & 0x02) == 0x02)
        svga->ma_latch |= 0x40000;

    if (tvga->oldctrl2 & 0x10) {
        svga->rowoffset <<= 1;
        svga->ma_latch <<= 1;
    }

    if (svga->gdcreg[0xf] & 0x08) {
        svga->htotal *= 2;
        svga->hdisp *= 2;
        svga->hdisp_time *= 2;
    }

    svga->interlace = (svga->crtc[0x1e] & 4);

    if (svga->interlace)
        svga->rowoffset >>= 1;

    if (tvga->card_id == TVGA8900CLD_ID)
        clksel = ((svga->miscout >> 2) & 3) | ((tvga->newctrl2 & 0x01) << 2) | ((tvga->oldctrl1 & 0x10) >> 1);
    else
        clksel = ((svga->miscout >> 2) & 3) | ((tvga->newctrl2 & 0x01) << 2) | ((tvga->newctrl2 & 0x40) >> 3);

    switch (clksel) {
        case 0x2:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 44900000.0;
            break;
        case 0x3:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 36000000.0;
            break;
        case 0x4:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 57272000.0;
            break;
        case 0x5:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 65000000.0;
            break;
        case 0x6:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 50350000.0;
            break;
        case 0x7:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 40000000.0;
            break;
        case 0x8:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 88000000.0;
            break;
        case 0x9:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 98000000.0;
            break;
        case 0xa:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 118800000.0;
            break;
        case 0xb:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 108000000.0;
            break;
        case 0xc:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 72000000.0;
            break;
        case 0xd:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 77000000.0;
            break;
        case 0xe:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 80000000.0;
            break;
        case 0xf:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 75000000.0;
            break;

        default:
            break;
    }

    if (tvga->card_id != TVGA8900CLD_ID) {
        /*TVGA9000 doesn't seem to have support for a 'high res' 256 colour mode
          (without the VGA pixel doubling). Instead it implements these modes by
          doubling the horizontal pixel count and pixel clock. Hence we use a
          basic heuristic to detect this*/
        if (svga->interlace)
            high_res_256 = (svga->htotal * 8) > (svga->vtotal * 4);
        else
            high_res_256 = (svga->htotal * 8) > (svga->vtotal * 2);
    }

    if ((tvga->oldctrl2 & 0x10) || high_res_256) {
        if (high_res_256)
            svga->hdisp /= 2;
        switch (svga->bpp) {
            case 8:
                svga->render = svga_render_8bpp_highres;
                break;
            case 15:
                svga->render = svga_render_15bpp_highres;
                svga->hdisp /= 2;
                break;
            case 16:
                svga->render = svga_render_16bpp_highres;
                svga->hdisp /= 2;
                break;
            case 24:
                svga->render = svga_render_24bpp_highres;
                svga->hdisp /= 3;
                break;

            default:
                break;
        }
        svga->lowres = 0;
    }
}

static void *
tvga_init(const device_t *info)
{
    const char *bios_fn;
    tvga_t     *tvga = malloc(sizeof(tvga_t));
    memset(tvga, 0, sizeof(tvga_t));

    tvga->card_id = info->local & 0xFF;

    if (tvga->card_id == TVGA9000B_ID) {
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_tvga9000);
        tvga->vram_size = 512 << 10;
    } else {
        if (info->local & 0x0100)
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_tvga8900dr);
        else
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_tvga8900);
        tvga->vram_size = device_get_config_int("memory") << 10;
    }

    tvga->vram_mask = tvga->vram_size - 1;

    switch (tvga->card_id) {
        case TVGA8900B_ID:
            bios_fn = ROM_TVGA_8900B;
            break;
        case TVGA8900CLD_ID:
            if (info->local & 0x0100)
                bios_fn = ROM_TVGA_8900DR;
            else
                bios_fn = ROM_TVGA_8900CLD;
            break;
        case TVGA9000B_ID:
            bios_fn = (info->local & 0x100) ? ROM_TVGA_9000B_NEC_SV9000 : ROM_TVGA_9000B;
            break;
        default:
            free(tvga);
            return NULL;
    }

    rom_init(&tvga->bios_rom, bios_fn, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    svga_init(info, &tvga->svga, tvga, tvga->vram_size,
              tvga_recalctimings,
              tvga_in, tvga_out,
              NULL,
              NULL);

    if (tvga->card_id != TVGA9000B_ID)
        tvga->svga.ramdac = device_add(&tkd8001_ramdac_device);

    io_sethandler(0x03c0, 0x0020, tvga_in, NULL, NULL, tvga_out, NULL, NULL, tvga);

    return tvga;
}

static int
tvga8900b_available(void)
{
    return rom_present(ROM_TVGA_8900B);
}

static int
tvga8900d_available(void)
{
    return rom_present(ROM_TVGA_8900CLD);
}

static int
tvga8900dr_available(void)
{
    return rom_present(ROM_TVGA_8900DR);
}

static int
tvga9000b_available(void)
{
    return rom_present(ROM_TVGA_9000B);
}

static int
tvga9000b_nec_sv9000_available(void)
{
    return rom_present(ROM_TVGA_9000B_NEC_SV9000);
}

void
tvga_close(void *priv)
{
    tvga_t *tvga = (tvga_t *) priv;

    svga_close(&tvga->svga);

    free(tvga);
}

void
tvga_speed_changed(void *priv)
{
    tvga_t *tvga = (tvga_t *) priv;

    svga_recalctimings(&tvga->svga);
}

void
tvga_force_redraw(void *priv)
{
    tvga_t *tvga = (tvga_t *) priv;

    tvga->svga.fullchange = changeframecount;
}

static const device_config_t tvga_config[] = {
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
            /*Chip supports 2mb, but drivers are buggy*/
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format off
};

const device_t tvga8900b_device = {
    .name          = "Trident TVGA 8900B",
    .internal_name = "tvga8900b",
    .flags         = DEVICE_ISA,
    .local         = TVGA8900B_ID,
    .init          = tvga_init,
    .close         = tvga_close,
    .reset         = NULL,
    .available     = tvga8900b_available,
    .speed_changed = tvga_speed_changed,
    .force_redraw  = tvga_force_redraw,
    .config        = tvga_config
};

const device_t tvga8900d_device = {
    .name          = "Trident TVGA 8900D",
    .internal_name = "tvga8900d",
    .flags         = DEVICE_ISA,
    .local         = TVGA8900CLD_ID,
    .init          = tvga_init,
    .close         = tvga_close,
    .reset         = NULL,
    .available     = tvga8900d_available,
    .speed_changed = tvga_speed_changed,
    .force_redraw  = tvga_force_redraw,
    .config        = tvga_config
};

const device_t tvga8900dr_device = {
    .name          = "Trident TVGA 8900D-R",
    .internal_name = "tvga8900dr",
    .flags         = DEVICE_ISA,
    .local         = TVGA8900CLD_ID | 0x0100,
    .init          = tvga_init,
    .close         = tvga_close,
    .reset         = NULL,
    .available     = tvga8900dr_available,
    .speed_changed = tvga_speed_changed,
    .force_redraw  = tvga_force_redraw,
    .config        = tvga_config
};

const device_t tvga9000b_device = {
    .name          = "Trident TVGA 9000B",
    .internal_name = "tvga9000b",
    .flags         = DEVICE_ISA,
    .local         = TVGA9000B_ID,
    .init          = tvga_init,
    .close         = tvga_close,
    .reset         = NULL,
    .available     = tvga9000b_available,
    .speed_changed = tvga_speed_changed,
    .force_redraw  = tvga_force_redraw,
    .config        = NULL
};

const device_t nec_sv9000_device = {
    .name          = "NEC SV9000 (Trident TVGA 9000B)",
    .internal_name = "nec_sv9000",
    .flags         = DEVICE_ISA,
    .local         = TVGA9000B_ID | 0x100,
    .init          = tvga_init,
    .close         = tvga_close,
    .reset         = NULL,
    .available     = tvga9000b_nec_sv9000_available,
    .speed_changed = tvga_speed_changed,
    .force_redraw  = tvga_force_redraw,
    .config        = NULL
};
