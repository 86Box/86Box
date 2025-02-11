/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI 28800 emulation (VGA Charger and Korean VGA)
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          greatpsycho,
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2018 greatpsycho.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
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

#define VGAWONDERXL               1
#define VGAWONDERXLPLUS           2
#ifdef USE_XL24
#    define VGAWONDERXL24         3
#endif /* USE_XL24 */

#define BIOS_ATIKOR_PATH         "roms/video/ati28800/atikorvga.bin"
#define BIOS_ATIKOR_4620P_PATH_L "roms/machines/spc4620p/31005h.u8"
#define BIOS_ATIKOR_4620P_PATH_H "roms/machines/spc4620p/31005h.u10"
#define BIOS_ATIKOR_6033P_PATH   "roms/machines/spc6033p/phoenix.BIN"
#define FONT_ATIKOR_PATH         "roms/video/ati28800/ati_ksc5601.rom"
#define FONT_ATIKOR_4620P_PATH   "roms/machines/spc4620p/svb6120a_font.rom"
#define FONT_ATIKOR_6033P_PATH   "roms/machines/spc6033p/svb6120a_font.rom"

#define BIOS_VGAXL_EVEN_PATH     "roms/video/ati28800/xleven.bin"
#define BIOS_VGAXL_ODD_PATH      "roms/video/ati28800/xlodd.bin"

#ifdef USE_XL24
#    define BIOS_XL24_EVEN_PATH "roms/video/ati28800/112-14318-102.bin"
#    define BIOS_XL24_ODD_PATH  "roms/video/ati28800/112-14319-102.bin"
#endif /* USE_XL24 */

#define BIOS_ROM_PATH            "roms/video/ati28800/bios.bin"
#define BIOS_VGAXL_ROM_PATH      "roms/video/ati28800/ATI_VGAWonder_XL.bin"
#define BIOS_VGAXL_PLUS_ROM_PATH "roms/video/ati28800/VGAWonder1024D_XL_Plus_VGABIOS_U19.BIN"

typedef struct ati28800_t {
    svga_t       svga;
    ati_eeprom_t eeprom;

    rom_t bios_rom;

    uint8_t  regs[256];
    int      index;
    uint16_t vtotal;

    uint32_t memory;
    uint8_t  id;

    uint8_t  port_03dd_val;
    uint16_t get_korean_font_kind;
    int      in_get_korean_font_kind_set;
    int      get_korean_font_enabled;
    int      get_korean_font_index;
    uint16_t get_korean_font_base;
    int      ksc5601_mode_enabled;

    int type, type_korean;
} ati28800_t;

static video_timings_t timing_ati28800     = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l = 6, .read_b = 5, .read_w = 5, .read_l = 10 };
static video_timings_t timing_ati28800_spc = { .type = VIDEO_ISA, .write_b = 2, .write_w = 2, .write_l = 4, .read_b = 4, .read_w = 4, .read_l = 8 };

#ifdef ENABLE_ATI28800_LOG
int ati28800_do_log = ENABLE_ATI28800_LOG;

static void
ati28800_log(const char *fmt, ...)
{
    va_list ap;

    if (ati28800_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ati28800_log(fmt, ...)
#endif

static void ati28800_recalctimings(svga_t *svga);

static void
ati28800_out(uint16_t addr, uint8_t val, void *priv)
{
    ati28800_t *ati28800 = (ati28800_t *) priv;
    svga_t     *svga     = &ati28800->svga;
    uint8_t     old;

    ati28800_log("ati28800_out : %04X %02X\n", addr, val);

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x1ce:
            ati28800->index = val;
            break;
        case 0x1cf:
            old                             = ati28800->regs[ati28800->index];
            ati28800->regs[ati28800->index] = val;
            ati28800_log("ATI 28800 write reg=0x%02X, val=0x%02X\n", ati28800->index, val);
            switch (ati28800->index) {
                case 0xa3:
                    if ((old ^ val) & 0x10)
                        svga_recalctimings(svga);
                    break;
                case 0xa7:
                    if ((old ^ val) & 0x80)
                        svga_recalctimings(svga);
                    break;
                case 0xad:
                    if ((old ^ val) & 0x0c)
                        svga_recalctimings(svga);
                    break;
                case 0xb0:
                    if ((old ^ val) & 0x60)
                        svga_recalctimings(svga);
                    break;
                case 0xb2:
                case 0xbe:
                    if (ati28800->regs[0xbe] & 0x08) { /* Read/write bank mode */
                        svga->read_bank  = (((ati28800->regs[0xb2] & 0x01) << 3) | ((ati28800->regs[0xb2] & 0xe0) >> 5)) * 0x10000;
                        svga->write_bank = ((ati28800->regs[0xb2] & 0x1e) >> 1) * 0x10000;
                    } else { /* Single bank mode */
                        svga->read_bank  = ((ati28800->regs[0xb2] & 0x1e) >> 1) * 0x10000;
                        svga->write_bank = ((ati28800->regs[0xb2] & 0x1e) >> 1) * 0x10000;
                    }
                    if (ati28800->index == 0xbe) {
                        if ((old ^ val) & 0x10)
                            svga_recalctimings(svga);
                    }
                    break;
                case 0xb3:
                    ati_eeprom_write(&ati28800->eeprom, val & 8, val & 2, val & 1);
                    break;
                case 0xb6:
                    if ((old ^ val) & 0x10)
                        svga_recalctimings(svga);
                    break;
                case 0xb8:
                    if ((old ^ val) & 0x40)
                        svga_recalctimings(svga);
                    break;
                case 0xb9:
                    if ((old ^ val) & 2)
                        svga_recalctimings(svga);
                    break;

                default:
                    break;
            }
            break;

        case 0x3C6:
        case 0x3C7:
        case 0x3C8:
        case 0x3C9:
            if ((ati28800->type == VGAWONDERXL) || (ati28800->type == VGAWONDERXLPLUS))
                sc1148x_ramdac_out(addr, 0, val, svga->ramdac, svga);
            else
                svga_out(addr, val, svga);
            return;

        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
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

static void
ati28800k_out(uint16_t addr, uint8_t val, void *priv)
{
    ati28800_t *ati28800 = (ati28800_t *) priv;
    svga_t     *svga     = &ati28800->svga;
    uint16_t    oldaddr  = addr;

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x1CF:
            if (ati28800->index == 0xBF && ((ati28800->regs[0xBF] ^ val) & 0x20)) {
                ati28800->ksc5601_mode_enabled = val & 0x20;
                svga_recalctimings(svga);
            }
            ati28800_out(oldaddr, val, priv);
            break;
        case 0x3DD:
            ati28800->port_03dd_val = val;
            if (val == 1)
                ati28800->get_korean_font_enabled = 0;
            if (ati28800->in_get_korean_font_kind_set) {
                ati28800->get_korean_font_kind        = (val << 8) | (ati28800->get_korean_font_kind & 0xFF);
                ati28800->get_korean_font_enabled     = 1;
                ati28800->get_korean_font_index       = 0;
                ati28800->in_get_korean_font_kind_set = 0;
            }
            break;
        case 0x3DE:
            ati28800->in_get_korean_font_kind_set = 0;
            if (ati28800->get_korean_font_enabled) {
                if ((ati28800->get_korean_font_base & 0x7F) > 0x20 && (ati28800->get_korean_font_base & 0x7F) < 0x7F) {
                    fontdatksc5601_user[(ati28800->get_korean_font_kind & 4) * 24 + (ati28800->get_korean_font_base & 0x7F) - 0x20].chr[ati28800->get_korean_font_index] = val;
                }
                ati28800->get_korean_font_index++;
                ati28800->get_korean_font_index &= 0x1F;
            } else {
                switch (ati28800->port_03dd_val) {
                    case 0x10:
                        ati28800->get_korean_font_base = ((val & 0x7F) << 7) | (ati28800->get_korean_font_base & 0x7F);
                        break;
                    case 8:
                        ati28800->get_korean_font_base = (ati28800->get_korean_font_base & 0x3F80) | (val & 0x7F);
                        break;
                    case 1:
                        ati28800->get_korean_font_kind = (ati28800->get_korean_font_kind & 0xFF00) | val;
                        if (val & 2)
                            ati28800->in_get_korean_font_kind_set = 1;
                        break;

                    default:
                        break;
                }
                break;
            }
            break;
        default:
            ati28800_out(oldaddr, val, priv);
            break;
    }
}

static uint8_t
ati28800_in(uint16_t addr, void *priv)
{
    ati28800_t *ati28800 = (ati28800_t *) priv;
    svga_t     *svga     = &ati28800->svga;
    uint8_t     temp;

    if (addr != 0x3da)
        ati28800_log("ati28800_in : %04X ", addr);

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x1ce:
            temp = ati28800->index;
            break;
        case 0x1cf:
            switch (ati28800->index) {
                case 0xaa:
                    temp = ati28800->id;
                    break;
                case 0xb0:
                    temp = ati28800->regs[0xb0] | 0x80;
                    if (ati28800->memory == 1024) {
                        temp &= ~0x10;
                        temp |= 0x08;
                    } else if (ati28800->memory == 512) {
                        temp |= 0x10;
                        temp &= ~0x08;
                    } else {
                        temp &= ~0x18;
                    }
                    break;
                case 0xb7:
                    temp = ati28800->regs[0xb7] & ~8;
                    if (ati_eeprom_read(&ati28800->eeprom))
                        temp |= 8;
                    break;

                default:
                    temp = ati28800->regs[ati28800->index];
                    break;
            }
            break;

        case 0x3c2:
            if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x50)
                temp = 0;
            else
                temp = 0x10;
            break;

        case 0x3C6:
        case 0x3C7:
        case 0x3C8:
        case 0x3C9:
            if ((ati28800->type == VGAWONDERXL) || (ati28800->type == VGAWONDERXLPLUS))
                return sc1148x_ramdac_in(addr, 0, svga->ramdac, svga);
            return svga_in(addr, svga);

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
    if (addr != 0x3da)
        ati28800_log("%02X\n", temp);
    return temp;
}

static uint8_t
ati28800k_in(uint16_t addr, void *priv)
{
    ati28800_t   *ati28800 = (ati28800_t *) priv;
    const svga_t *svga     = &ati28800->svga;
    uint16_t      oldaddr  = addr;
    uint8_t       temp     = 0xFF;

    if (addr != 0x3da)
        ati28800_log("ati28800k_in : %04X ", addr);

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3DE:
            if (ati28800->get_korean_font_enabled) {
                switch (ati28800->get_korean_font_kind >> 8) {
                    case 4: /* ROM font */
                        temp = fontdatksc5601[ati28800->get_korean_font_base].chr[ati28800->get_korean_font_index++];
                        break;
                    case 2: /* User defined font */
                        if ((ati28800->get_korean_font_base & 0x7F) > 0x20 && (ati28800->get_korean_font_base & 0x7F) < 0x7F) {
                            temp = fontdatksc5601_user[(ati28800->get_korean_font_kind & 4) * 24 + (ati28800->get_korean_font_base & 0x7F) - 0x20].chr[ati28800->get_korean_font_index];
                        } else
                            temp = 0xFF;
                        ati28800->get_korean_font_index++;
                        break;
                    default:
                        break;
                }
                ati28800->get_korean_font_index &= 0x1F;
            }
            break;
        default:
            temp = ati28800_in(oldaddr, priv);
            break;
    }
    if (addr != 0x3da)
        ati28800_log("%02X\n", temp);
    return temp;
}

static void
ati28800_recalctimings(svga_t *svga)
{
    ati28800_t       *ati28800 = (ati28800_t *) svga->priv;
    int               clock_sel;

    if (ati28800->regs[0xad] & 0x08)
        svga->hblankstart    = ((ati28800->regs[0x0d] >> 2) << 8) + svga->crtc[2];

    clock_sel = ((svga->miscout >> 2) & 3) | ((ati28800->regs[0xbe] & 0x10) >> 1) |
                ((ati28800->regs[0xb9] & 2) << 1);

    if (ati28800->regs[0xa3] & 0x10)
        svga->ma_latch |= 0x10000;

    if (ati28800->regs[0xb0] & 0x40)
        svga->ma_latch |= 0x20000;

    if (ati28800->regs[0xb8] & 0x40)
        svga->clock *= 2;

    if (ati28800->regs[0xa7] & 0x80)
        svga->clock *= 3;

    if ((ati28800->regs[0xb6] & 0x18) >= 0x10) {
        svga->hdisp <<= 1;
        svga->htotal <<= 1;
        svga->rowoffset <<= 1;
        svga->dots_per_clock <<= 1;
        svga->gdcreg[5] &= ~0x40;
    }

    if (ati28800->regs[0xb0] & 0x20) {
        svga->gdcreg[5] |= 0x40;
        if ((ati28800->regs[0xb6] & 0x18) >= 0x10)
            svga->packed_4bpp = 1;
        else
            svga->packed_4bpp = 0;
    } else
        svga->packed_4bpp = 0;

    if ((ati28800->regs[0xb6] & 0x18) == 8) {
        svga->hdisp <<= 1;
        svga->htotal <<= 1;
        svga->dots_per_clock <<= 1;
        svga->ati_4color = 1;
    } else
        svga->ati_4color = 0;

    if (!svga->scrblank && (svga->crtc[0x17] & 0x80) && svga->attr_palette_enable) {
         if ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1)) {
            svga->clock = (cpuclock * (double) (1ULL << 32)) / svga->getclock(clock_sel, svga->clock_gen);
            ati28800_log("SEQREG1 bit 3=%x. gdcreg5 bits 5-6=%02x, 4bit pel=%02x, "
                         "planar 16color=%02x, apa mode=%02x, attregs10 bit 7=%02x.\n",
                         svga->seqregs[1] & 8, svga->gdcreg[5] & 0x60,
                         ati28800->regs[0xb3] & 0x40, ati28800->regs[0xac] & 0x40,
                         ati28800->regs[0xb6] & 0x18, ati28800->svga.attrregs[0x10] & 0x80);
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
                        case 15:
                            if (svga->lowres)
                                svga->render = svga_render_15bpp_lowres;
                            else {
                                svga->render = svga_render_15bpp_highres;
                                svga->hdisp >>= 1;
                                svga->dots_per_clock >>= 1;
                                svga->rowoffset <<= 1;
                                svga->ma_latch <<= 1;
                            }
                            break;
                        default:
                            break;
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

static void
ati28800k_recalctimings(svga_t *svga)
{
    const ati28800_t *ati28800 = (ati28800_t *) svga->priv;

    ati28800_recalctimings(svga);

    if (svga->render == svga_render_text_80 && ati28800->ksc5601_mode_enabled)
        svga->render = svga_render_text_80_ksc5601;
}

void *
ati28800k_init(const device_t *info)
{
    ati28800_t *ati28800 = (ati28800_t *) malloc(sizeof(ati28800_t));
    memset(ati28800, 0, sizeof(ati28800_t));

    ati28800->type_korean = info->local;

    if (ati28800->type_korean == 0) {
        ati28800->memory = device_get_config_int("memory");
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ati28800);
    } else {
        ati28800->memory = 512;
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ati28800_spc);
    }

    ati28800->port_03dd_val               = 0;
    ati28800->get_korean_font_base        = 0;
    ati28800->get_korean_font_index       = 0;
    ati28800->get_korean_font_enabled     = 0;
    ati28800->get_korean_font_kind        = 0;
    ati28800->in_get_korean_font_kind_set = 0;
    ati28800->ksc5601_mode_enabled        = 0;

    switch (ati28800->type_korean) {
        default:
        case 0:
            rom_init(&ati28800->bios_rom, BIOS_ATIKOR_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            loadfont(FONT_ATIKOR_PATH, 6);
            break;
        case 1:
            rom_init_interleaved(&ati28800->bios_rom, BIOS_ATIKOR_4620P_PATH_L, BIOS_ATIKOR_4620P_PATH_H, 0xc0000,
                                 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            loadfont(FONT_ATIKOR_4620P_PATH, 6);
            break;
        case 2:
            rom_init(&ati28800->bios_rom, BIOS_ATIKOR_6033P_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            loadfont(FONT_ATIKOR_6033P_PATH, 6);
            break;
    }

    svga_init(info, &ati28800->svga, ati28800, ati28800->memory << 10, /*Memory size, default 512KB*/
              ati28800k_recalctimings,
              ati28800k_in, ati28800k_out,
              NULL,
              NULL);
    ati28800->svga.clock_gen = device_add(&ati18810_device);
    ati28800->svga.getclock  = ics2494_getclock;

    io_sethandler(0x01ce, 0x0002, ati28800k_in, NULL, NULL, ati28800k_out, NULL, NULL, ati28800);
    io_sethandler(0x03c0, 0x0020, ati28800k_in, NULL, NULL, ati28800k_out, NULL, NULL, ati28800);

    ati28800->svga.miscout                   = 1;
    ati28800->svga.bpp                       = 8;
    ati28800->svga.packed_chain4             = 1;
    ati28800->svga.ksc5601_sbyte_mask        = 0;
    ati28800->svga.ksc5601_udc_area_msb[0]   = 0xC9;
    ati28800->svga.ksc5601_udc_area_msb[1]   = 0xFE;
    ati28800->svga.ksc5601_swap_mode         = 0;
    ati28800->svga.ksc5601_english_font_type = 0;

    ati_eeprom_load(&ati28800->eeprom, "atikorvga.nvr", 0);

    return ati28800;
}

static void *
ati28800_init(const device_t *info)
{
    ati28800_t *ati28800;
    ati28800 = malloc(sizeof(ati28800_t));
    memset(ati28800, 0x00, sizeof(ati28800_t));

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ati28800);

    ati28800->memory = device_get_config_int("memory");

    ati28800->type = info->local;

    switch (ati28800->type) {
        case VGAWONDERXL:
            ati28800->id = 5;
            rom_init(&ati28800->bios_rom,
                     BIOS_VGAXL_ROM_PATH,
                     0xc0000, 0x8000, 0x7fff,
                     0, MEM_MAPPING_EXTERNAL);
            ati28800->svga.ramdac = device_add(&sc11486_ramdac_device);
            break;

        case VGAWONDERXLPLUS:
            ati28800->id = 6;
            rom_init(&ati28800->bios_rom,
                     BIOS_VGAXL_PLUS_ROM_PATH,
                     0xc0000, 0x8000, 0x7fff,
                     0, MEM_MAPPING_EXTERNAL);
            ati28800->svga.ramdac = device_add(&sc11483_ramdac_device);
            ati28800->memory = 1024;
            break;

#ifdef USE_XL24
        case VGAWONDERXL24:
            ati28800->id = 6;
            rom_init_interleaved(&ati28800->bios_rom,
                                 BIOS_XL24_EVEN_PATH,
                                 BIOS_XL24_ODD_PATH,
                                 0xc0000, 0x10000, 0xffff,
                                 0, MEM_MAPPING_EXTERNAL);
            break;
#endif /* USE_XL24 */

        default:
            ati28800->id = 5;
            rom_init(&ati28800->bios_rom,
                     BIOS_ROM_PATH,
                     0xc0000, 0x8000, 0x7fff,
                     0, MEM_MAPPING_EXTERNAL);
            break;
    }

    svga_init(info, &ati28800->svga, ati28800, ati28800->memory << 10, /*default: 512kb*/
              ati28800_recalctimings,
              ati28800_in, ati28800_out,
              NULL,
              NULL);
    ati28800->svga.clock_gen = device_add(&ati18810_device);
    ati28800->svga.getclock  = ics2494_getclock;

    io_sethandler(0x01ce, 2,
                  ati28800_in, NULL, NULL,
                  ati28800_out, NULL, NULL, ati28800);
    io_sethandler(0x03c0, 32,
                  ati28800_in, NULL, NULL,
                  ati28800_out, NULL, NULL, ati28800);

    ati28800->svga.miscout       = 1;
    ati28800->svga.bpp           = 8;
    ati28800->svga.packed_chain4 = 1;

    switch (ati28800->type) {
        case VGAWONDERXL:
            ati_eeprom_load(&ati28800->eeprom, "ati28800xl.nvr", 0);
            break;

        case VGAWONDERXLPLUS:
            ati_eeprom_load(&ati28800->eeprom, "ati28800_wonder1024d_xl_plus.nvr", 0);
            break;

#ifdef USE_XL24
        case VGAWONDERXL24:
            ati_eeprom_load(&ati28800->eeprom, "ati28800xl24.nvr", 0);
            break;
#endif /* USE_XL24 */

        default:
            ati_eeprom_load(&ati28800->eeprom, "ati28800.nvr", 0);
            break;
    }

    return ati28800;
}

static int
ati28800_available(void)
{
    return (rom_present(BIOS_ROM_PATH));
}

static int
ati28800k_available(void)
{
    return (rom_present(BIOS_ATIKOR_PATH) && rom_present(FONT_ATIKOR_PATH));
}

static int
compaq_ati28800_available(void)
{
    return (rom_present(BIOS_VGAXL_ROM_PATH));
}

static int
ati28800_wonder1024d_xl_plus_available(void)
{
    return (rom_present(BIOS_VGAXL_PLUS_ROM_PATH));
}

#ifdef USE_XL24
static int
ati28800_wonderxl24_available(void)
{
    return (rom_present(BIOS_XL24_EVEN_PATH) && rom_present(BIOS_XL24_ODD_PATH));
}
#endif /* USE_XL24 */

static void
ati28800_close(void *priv)
{
    ati28800_t *ati28800 = (ati28800_t *) priv;

    svga_close(&ati28800->svga);

    free(ati28800);
}

static void
ati28800_speed_changed(void *priv)
{
    ati28800_t *ati28800 = (ati28800_t *) priv;

    svga_recalctimings(&ati28800->svga);
}

static void
ati28800_force_redraw(void *priv)
{
    ati28800_t *ati28800 = (ati28800_t *) priv;

    ati28800->svga.fullchange = changeframecount;
}

// clang-format off
static const device_config_t ati28800_config[] = {
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

#ifdef USE_XL24
static const device_config_t ati28800_wonderxl_config[] = {
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
#endif /* USE_XL24 */
// clang-format on

const device_t ati28800_device = {
    .name          = "ATI 28800-5 (ATI VGA Charger)",
    .internal_name = "ati28800",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = ati28800_init,
    .close         = ati28800_close,
    .reset         = NULL,
    .available     = ati28800_available,
    .speed_changed = ati28800_speed_changed,
    .force_redraw  = ati28800_force_redraw,
    .config        = ati28800_config
};

const device_t ati28800k_device = {
    .name          = "ATI Korean VGA",
    .internal_name = "ati28800k",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = ati28800k_init,
    .close         = ati28800_close,
    .reset         = NULL,
    .available     = ati28800k_available,
    .speed_changed = ati28800_speed_changed,
    .force_redraw  = ati28800_force_redraw,
    .config        = ati28800_config
};

const device_t ati28800k_spc4620p_device = {
    .name          = "ATI Korean VGA On-Board SPC-4620P",
    .internal_name = "ati28800k_spc4620p",
    .flags         = DEVICE_ISA,
    .local         = 1,
    .init          = ati28800k_init,
    .close         = ati28800_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = ati28800_speed_changed,
    .force_redraw  = ati28800_force_redraw,
    .config        = NULL
};

const device_t ati28800k_spc6033p_device = {
    .name          = "ATI Korean VGA On-Board SPC-6033P",
    .internal_name = "ati28800k_spc6033p",
    .flags         = DEVICE_ISA,
    .local         = 2,
    .init          = ati28800k_init,
    .close         = ati28800_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = ati28800_speed_changed,
    .force_redraw  = ati28800_force_redraw,
    .config        = NULL
};

const device_t compaq_ati28800_device = {
    .name          = "ATI 28800-5 (ATI VGA Wonder XL)",
    .internal_name = "compaq_ati28800",
    .flags         = DEVICE_ISA,
    .local         = VGAWONDERXL,
    .init          = ati28800_init,
    .close         = ati28800_close,
    .reset         = NULL,
    .available     = compaq_ati28800_available,
    .speed_changed = ati28800_speed_changed,
    .force_redraw  = ati28800_force_redraw,
    .config        = ati28800_config
};

const device_t ati28800_wonder1024d_xl_plus_device = {
    .name          = "ATI 28800-6 (ATI VGA Wonder 1024D XL Plus)",
    .internal_name = "ati28800_wonder1024d_xl_plus",
    .flags         = DEVICE_ISA,
    .local         = VGAWONDERXLPLUS,
    .init          = ati28800_init,
    .close         = ati28800_close,
    .reset         = NULL,
    .available     = ati28800_wonder1024d_xl_plus_available,
    .speed_changed = ati28800_speed_changed,
    .force_redraw  = ati28800_force_redraw,
    .config        = NULL
};

#ifdef USE_XL24
const device_t ati28800_wonderxl24_device = {
    .name          = "ATI-28800 (VGA Wonder XL24)",
    .internal_name = "ati28800w",
    .flags         = DEVICE_ISA,
    .local         = VGAWONDERXL24,
    .init          = ati28800_init,
    .close         = ati28800_close,
    .reset         = NULL,
    .available     = ati28800_wonderxl24_available,
    .speed_changed = ati28800_speed_changed,
    .force_redraw  = ati28800_force_redraw,
    .config        = ati28800_wonderxl_config
};
#endif /* USE_XL24 */
