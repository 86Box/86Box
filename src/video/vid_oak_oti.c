/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Oak OTI037C/67/077 emulation.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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
#include <86box/plat_unused.h>

#define BIOS_037C_PATH        "roms/video/oti/bios.bin"
#define BIOS_067_AMA932J_PATH "roms/machines/ama932j/OTI067.BIN"
#define BIOS_067_M300_08_PATH "roms/machines/m30008/EVC_BIOS.ROM"
#define BIOS_067_M300_15_PATH "roms/machines/m30015/EVC_BIOS.ROM"
#define BIOS_077_PATH         "roms/video/oti/oti077.vbi"
#define BIOS_077_ACER100T_PATH "roms/machines/acer100t/oti077_acer100t.BIN"


enum {
    OTI_037C        = 0,
    OTI_067         = 2,
    OTI_067_AMA932J = 3,
    OTI_067_M300    = 4,
    OTI_077         = 5,
    OTI_077_ACER100T = 6
};

typedef struct {
    svga_t svga;

    rom_t bios_rom;

    int     index;
    uint8_t regs[32];

    uint8_t chip_id;
    uint8_t pos;
    uint8_t enable_register;
    uint8_t dipswitch_val;

    uint32_t vram_size;
    uint32_t vram_mask;
} oti_t;

static video_timings_t timing_oti = { .type = VIDEO_ISA, .write_b = 6, .write_w = 8, .write_l = 16, .read_b = 6, .read_w = 8, .read_l = 16 };

static void
oti_out(uint16_t addr, uint8_t val, void *priv)
{
    oti_t  *oti  = (oti_t *) priv;
    svga_t *svga = &oti->svga;
    uint8_t old;
    uint8_t idx;
    uint8_t enable;

    if (!oti->chip_id && !(oti->enable_register & 1) && (addr != 0x3C3))
        return;

    if ((((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && addr < 0x3de) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3C3:
            if (!oti->chip_id) {
                oti->enable_register = val & 1;
                return;
            }
            svga_out(addr, val, svga);
            return;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            if (oti->chip_id == OTI_077 || oti->chip_id == OTI_077_ACER100T)
                sc1148x_ramdac_out(addr, 0, val, svga->ramdac, svga);
            else
                svga_out(addr, val, svga);
            return;

        case 0x3D4:
            if (oti->chip_id)
                svga->crtcreg = val & 0x3f;
            else
                svga->crtcreg = val; /* FIXME: The BIOS wants to set the test bit? */
            return;

        case 0x3D5:
            if (oti->chip_id && (svga->crtcreg & 0x20))
                return;
            idx = svga->crtcreg;
            if (!oti->chip_id)
                idx &= 0x1f;
            if ((idx < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((idx == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old             = svga->crtc[idx];
            svga->crtc[idx] = val;
            if (old != val) {
                if ((idx < 0x0e) || (idx > 0x10)) {
                    if (idx == 0x0c || idx == 0x0d) {
                        svga->fullchange = 3;
                        svga->ma_latch   = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            break;

        case 0x3DE:
            if (oti->chip_id)
                oti->index = val & 0x1f;
            else
                oti->index = val;
            return;

        case 0x3DF:
            idx = oti->index;
            if (!oti->chip_id)
                idx &= 0x1f;
            oti->regs[idx] = val;
            switch (idx) {
                case 0xD:
                    if (oti->chip_id == OTI_067) {
                        svga->vram_display_mask = (val & 0x0c) ? oti->vram_mask : 0x3ffff;
                        if (!(val & 0x80))
                            svga->vram_display_mask = 0x3ffff;

                        if ((val & 0x80) && oti->vram_size == 256)
                            mem_mapping_disable(&svga->mapping);
                        else
                            mem_mapping_enable(&svga->mapping);
                    } else if (oti->chip_id == OTI_077 || oti->chip_id == OTI_077_ACER100T) {
                        svga->vram_display_mask = (val & 0x0c) ? oti->vram_mask : 0x3ffff;

                        switch ((val & 0xc0) >> 6) {
                            default:
                            case 0x00: /* 256 kB of memory */
                                enable = (oti->vram_size >= 256);
                                if (val & 0x0c)
                                    svga->vram_display_mask = MIN(oti->vram_mask, 0x3ffff);
                                break;
                            case 0x01: /* 1 MB of memory */
                            case 0x03:
                                enable = (oti->vram_size >= 1024);
                                if (val & 0x0c)
                                    svga->vram_display_mask = MIN(oti->vram_mask, 0xfffff);
                                break;
                            case 0x02: /* 512 kB of memory */
                                enable = (oti->vram_size >= 512);
                                if (val & 0x0c)
                                    svga->vram_display_mask = MIN(oti->vram_mask, 0x7ffff);
                                break;
                        }

                        if (enable)
                            mem_mapping_enable(&svga->mapping);
                        else
                            mem_mapping_disable(&svga->mapping);
                    } else {
                        if (val & 0x80)
                            mem_mapping_disable(&svga->mapping);
                        else
                            mem_mapping_enable(&svga->mapping);
                    }
                    break;

                case 0x11:
                    svga->read_bank  = (val & 0xf) * 65536;
                    svga->write_bank = (val >> 4) * 65536;
                    break;

                default:
                    break;
            }
            return;

        default:
            break;
    }

    svga_out(addr, val, svga);
}

static uint8_t
oti_in(uint16_t addr, void *priv)
{
    oti_t  *oti  = (oti_t *) priv;
    svga_t *svga = &oti->svga;
    uint8_t idx;
    uint8_t temp;

    if (!oti->chip_id && !(oti->enable_register & 1) && (addr != 0x3C3))
        return 0xff;

    if ((((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && addr < 0x3de) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3C2:
            if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x50)
                temp = 0;
            else
                temp = 0x10;
            break;

        case 0x3C3:
            if (oti->chip_id)
                temp = svga_in(addr, svga);
            else
                temp = oti->enable_register;
            break;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            if (oti->chip_id == OTI_077 || oti->chip_id == OTI_077_ACER100T)
                return sc1148x_ramdac_in(addr, 0, svga->ramdac, svga);
            return svga_in(addr, svga);

        case 0x3CF:
            return svga->gdcreg[svga->gdcaddr & 0xf];

        case 0x3D4:
            temp = svga->crtcreg;
            break;

        case 0x3D5:
            if (oti->chip_id) {
                if (svga->crtcreg & 0x20)
                    temp = 0xff;
                else
                    temp = svga->crtc[svga->crtcreg];
            } else
                temp = svga->crtc[svga->crtcreg & 0x1f];
            break;

        case 0x3DA:
            if (oti->chip_id) {
                temp = svga_in(addr, svga);
                break;
            }

            svga->attrff = 0;
            /*The OTI-037C BIOS waits for bits 0 and 3 in 0x3da to go low, then reads 0x3da again
              and expects the diagnostic bits to equal the current border colour. As I understand
              it, the 0x3da active enable status does not include the border time, so this may be
              an area where OTI-037C is not entirely VGA compatible.*/
            svga->cgastat &= ~0x30;
            /* copy color diagnostic info from the overscan color register */
            switch (svga->attrregs[0x12] & 0x30) {
                case 0x00: /* P0 and P2 */
                    if (svga->attrregs[0x11] & 0x01)
                        svga->cgastat |= 0x10;
                    if (svga->attrregs[0x11] & 0x04)
                        svga->cgastat |= 0x20;
                    break;
                case 0x10: /* P4 and P5 */
                    if (svga->attrregs[0x11] & 0x10)
                        svga->cgastat |= 0x10;
                    if (svga->attrregs[0x11] & 0x20)
                        svga->cgastat |= 0x20;
                    break;
                case 0x20: /* P1 and P3 */
                    if (svga->attrregs[0x11] & 0x02)
                        svga->cgastat |= 0x10;
                    if (svga->attrregs[0x11] & 0x08)
                        svga->cgastat |= 0x20;
                    break;
                case 0x30: /* P6 and P7 */
                    if (svga->attrregs[0x11] & 0x40)
                        svga->cgastat |= 0x10;
                    if (svga->attrregs[0x11] & 0x80)
                        svga->cgastat |= 0x20;
                    break;

                default:
                    break;
            }
            temp = svga->cgastat;
            break;

        case 0x3DE:
            temp = oti->index;
            if (oti->chip_id)
                temp |= (oti->chip_id << 5);
            break;

        case 0x3DF:
            idx = oti->index;
            if (!oti->chip_id)
                idx &= 0x1f;
            if (idx == 0x10)
                temp = oti->dipswitch_val;
            else
                temp = oti->regs[idx];
            break;

        default:
            temp = svga_in(addr, svga);
            break;
    }

    return temp;
}

static void
oti_pos_out(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    oti_t *oti = (oti_t *) priv;

    if ((val ^ oti->pos) & 8) {
        if (val & 8)
            io_sethandler(0x03c0, 32, oti_in, NULL, NULL,
                          oti_out, NULL, NULL, oti);
        else
            io_removehandler(0x03c0, 32, oti_in, NULL, NULL,
                             oti_out, NULL, NULL, oti);
    }

    oti->pos = val;
}

static uint8_t
oti_pos_in(UNUSED(uint16_t addr), void *priv)
{
    const oti_t *oti = (oti_t *) priv;

    return (oti->pos);
}

static float
oti_getclock(int clock)
{
    float ret = 0.0;

    switch (clock) {
        default:
        case 0:
            ret = 25175000.0;
            break;
        case 1:
            ret = 28322000.0;
            break;
        case 4:
            ret = 14318000.0;
            break;
        case 5:
            ret = 16257000.0;
            break;
        case 7:
            ret = 35500000.0;
            break;
    }

    return ret;
}

static void
oti_recalctimings(svga_t *svga)
{
    const oti_t *oti     = (oti_t *) svga->priv;
    int          clk_sel = ((svga->miscout >> 2) & 3) | ((oti->regs[0x0d] & 0x20) >> 3);

    svga->clock = (cpuclock * (double) (1ULL << 32)) / oti_getclock(clk_sel);

    if (oti->chip_id > 0) {
        if (oti->regs[0x14] & 0x08)
            svga->ma_latch |= 0x10000;
        if (oti->regs[0x16] & 0x08)
            svga->ma_latch |= 0x20000;

        if (oti->regs[0x14] & 0x01)
            svga->vtotal += 0x400;
        if (oti->regs[0x14] & 0x02)
            svga->dispend += 0x400;
        if (oti->regs[0x14] & 0x04)
            svga->vsyncstart += 0x400;

        svga->interlace = oti->regs[0x14] & 0x80;
    }

    if ((oti->regs[0x0d] & 0x0c) && !(oti->regs[0x0d] & 0x10))
        svga->rowoffset <<= 1;

    if (svga->bpp == 16) {
        svga->render = svga_render_16bpp_highres;
        svga->hdisp >>= 1;
    } else if (svga->bpp == 15) {
        svga->render = svga_render_15bpp_highres;
        svga->hdisp >>= 1;
    }
}

static void *
oti_init(const device_t *info)
{
    oti_t      *oti   = malloc(sizeof(oti_t));
    const char *romfn = NULL;

    memset(oti, 0x00, sizeof(oti_t));
    oti->chip_id = info->local;

    oti->dipswitch_val = 0x18;

    switch (oti->chip_id) {
        case OTI_037C:
            romfn          = BIOS_037C_PATH;
            oti->vram_size = 256;
            oti->regs[0]   = 0x08; /* FIXME: The BIOS wants to read this at index 0? This index is undocumented. */
#if 0
            io_sethandler(0x03c0, 32,
                          oti_in, NULL, NULL, oti_out, NULL, NULL, oti);
#endif
            break;

        case OTI_067_AMA932J:
            romfn          = BIOS_067_AMA932J_PATH;
            oti->chip_id   = 2;
            oti->vram_size = device_get_config_int("memory");
            oti->dipswitch_val |= 0x20;
            oti->pos = 0x08; /* Tell the BIOS the I/O ports are already enabled to avoid a double I/O handler mess. */
            io_sethandler(0x46e8, 1, oti_pos_in, NULL, NULL, oti_pos_out, NULL, NULL, oti);
            break;

        case OTI_067_M300:
            if (rom_present(BIOS_067_M300_15_PATH))
                romfn = BIOS_067_M300_15_PATH;
            else
                romfn = BIOS_067_M300_08_PATH;
            oti->vram_size = device_get_config_int("memory");
            oti->pos       = 0x08; /* Tell the BIOS the I/O ports are already enabled to avoid a double I/O handler mess. */
            io_sethandler(0x46e8, 1, oti_pos_in, NULL, NULL, oti_pos_out, NULL, NULL, oti);
            break;

        case OTI_067:
        case OTI_077:
            romfn          = BIOS_077_PATH;
            oti->vram_size = device_get_config_int("memory");
            oti->pos       = 0x08; /* Tell the BIOS the I/O ports are already enabled to avoid a double I/O handler mess. */
            io_sethandler(0x46e8, 1, oti_pos_in, NULL, NULL, oti_pos_out, NULL, NULL, oti);
            break;
        case OTI_077_ACER100T:
            romfn          = BIOS_077_ACER100T_PATH;
            oti->vram_size = device_get_config_int("memory");
            oti->pos = 0x08; /* Tell the BIOS the I/O ports are already enabled to avoid a double I/O handler mess. */
            io_sethandler(0x46e8, 1, oti_pos_in, NULL, NULL, oti_pos_out, NULL, NULL, oti);
            break;

        default:
            break;
    }

    if (romfn != NULL) {
        rom_init(&oti->bios_rom, romfn,
                 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    }

    oti->vram_mask = (oti->vram_size << 10) - 1;

    if (oti->chip_id == OTI_077_ACER100T){
        /* josephillips: Required to show all BIOS 
        information on Acer 100T only
        */
        video_inform(0x1,&timing_oti);   
    }else{
         video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_oti);
    }

    svga_init(info, &oti->svga, oti, oti->vram_size << 10,
              oti_recalctimings, oti_in, oti_out, NULL, NULL);

    if (oti->chip_id == OTI_077 || oti->chip_id == OTI_077_ACER100T)
        oti->svga.ramdac = device_add(&sc11487_ramdac_device); /*Actually a 82c487, probably a clone.*/

    io_sethandler(0x03c0, 32,
                  oti_in, NULL, NULL, oti_out, NULL, NULL, oti);

    oti->svga.miscout       = 1;
    oti->svga.packed_chain4 = 1;
    
    return oti;
}

static void
oti_close(void *priv)
{
    oti_t *oti = (oti_t *) priv;

    svga_close(&oti->svga);

    free(oti);
}

static void
oti_speed_changed(void *priv)
{
    oti_t *oti = (oti_t *) priv;

    svga_recalctimings(&oti->svga);
}

static void
oti_force_redraw(void *priv)
{
    oti_t *oti = (oti_t *) priv;

    oti->svga.fullchange = changeframecount;
}

static int
oti037c_available(void)
{
    return (rom_present(BIOS_037C_PATH));
}

static int
oti067_ama932j_available(void)
{
    return (rom_present(BIOS_067_AMA932J_PATH));
}

static int
oti077_acer100t_available(void)
{
    return (rom_present(BIOS_077_ACER100T_PATH));
}

static int
oti067_077_available(void)
{
    return (rom_present(BIOS_077_PATH));
}

static int
oti067_m300_available(void)
{
    if (rom_present(BIOS_067_M300_15_PATH))
        return (rom_present(BIOS_067_M300_15_PATH));
    else
        return (rom_present(BIOS_067_M300_08_PATH));
}

// clang-format off
static const device_config_t oti067_config[] = {
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
};

static const device_config_t oti067_ama932j_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 256,
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
};

static const device_config_t oti077_acer100t_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 512,
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
};

static const device_config_t oti077_config[] = {
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
};
// clang-format on

const device_t oti037c_device = {
    .name          = "Oak OTI-037C",
    .internal_name = "oti037c",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = oti_init,
    .close         = oti_close,
    .reset         = NULL,
    .available     = oti037c_available,
    .speed_changed = oti_speed_changed,
    .force_redraw  = oti_force_redraw,
    .config        = NULL
};

const device_t oti067_device = {
    .name          = "Oak OTI-067",
    .internal_name = "oti067",
    .flags         = DEVICE_ISA,
    .local         = 2,
    .init          = oti_init,
    .close         = oti_close,
    .reset         = NULL,
    .available     = oti067_077_available,
    .speed_changed = oti_speed_changed,
    .force_redraw  = oti_force_redraw,
    .config        = oti067_config
};

const device_t oti067_m300_device = {
    .name          = "Oak OTI-067 (Olivetti M300-08/15)",
    .internal_name = "oti067_m300",
    .flags         = DEVICE_ISA,
    .local         = 4,
    .init          = oti_init,
    .close         = oti_close,
    .reset         = NULL,
    .available     = oti067_m300_available,
    .speed_changed = oti_speed_changed,
    .force_redraw  = oti_force_redraw,
    .config        = oti067_config
};

const device_t oti067_ama932j_device = {
    .name          = "Oak OTI-067 (AMA-932J)",
    .internal_name = "oti067_ama932j",
    .flags         = DEVICE_ISA,
    .local         = 3,
    .init          = oti_init,
    .close         = oti_close,
    .reset         = NULL,
    .available     = oti067_ama932j_available,
    .speed_changed = oti_speed_changed,
    .force_redraw  = oti_force_redraw,
    .config        = oti067_ama932j_config
};

const device_t oti077_acer100t_device = {
    .name          = "Oak OTI-077 (Acer 100T)",
    .internal_name = "oti077_acer100t",
    .flags         = DEVICE_ISA,
    .local         = 6,
    .init          = oti_init,
    .close         = oti_close,
    .reset         = NULL,
    .available     = oti077_acer100t_available,
    .speed_changed = oti_speed_changed,
    .force_redraw  = oti_force_redraw,
    .config        = oti077_acer100t_config
};


const device_t oti077_device = {
    .name          = "Oak OTI-077",
    .internal_name = "oti077",
    .flags         = DEVICE_ISA,
    .local         = 5,
    .init          = oti_init,
    .close         = oti_close,
    .reset         = NULL,
    .available     = oti067_077_available,
    .speed_changed = oti_speed_changed,
    .force_redraw  = oti_force_redraw,
    .config        = oti077_config
};
