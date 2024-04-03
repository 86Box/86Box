/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Tseng Labs ET4000.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          GreatPsycho, <greatpsycho@yahoo.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2017-2018 Fred N. van Kempen.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2008-2018 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
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
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

#define ET4000_TYPE_TC6058AF 0 /* ISA ET4000AX (TC6058AF) */
#define ET4000_TYPE_ISA      1 /* ISA ET4000AX */
#define ET4000_TYPE_MCA      2 /* MCA ET4000AX */
#define ET4000_TYPE_KOREAN   3 /* Korean ET4000 */
#define ET4000_TYPE_TRIGEM   4 /* Trigem 286M ET4000 */
#define ET4000_TYPE_KASAN    5 /* Kasan ET4000 */

#define BIOS_ROM_PATH          "roms/video/et4000/ET4000.BIN"
#define V8_06_BIOS_ROM_PATH    "roms/video/et4000/ET4000_V8_06.BIN"
#define TC6058AF_BIOS_ROM_PATH "roms/video/et4000/Tseng_Labs_VGA-4000_BIOS_V1.1.bin"
#define V1_21_BIOS_ROM_PATH    "roms/video/et4000/Tseng_Labs_VGA-4000_BIOS_V1.21.bin"
#define KOREAN_BIOS_ROM_PATH   "roms/video/et4000/tgkorvga.bin"
#define KOREAN_FONT_ROM_PATH   "roms/video/et4000/tg_ksc5601.rom"
#define KASAN_BIOS_ROM_PATH    "roms/video/et4000/et4000_kasan16.bin"
#define KASAN_FONT_ROM_PATH    "roms/video/et4000/kasan_ksc5601.rom"

typedef struct {
    const char *name;
    int         type;

    svga_t svga;

    uint8_t pos_regs[8];

    rom_t bios_rom;

    uint8_t  banking;
    uint32_t vram_size,
        vram_mask;

    uint8_t  port_22cb_val;
    uint8_t  port_32cb_val;
    int      get_korean_font_enabled;
    int      get_korean_font_index;
    uint16_t get_korean_font_base;

    uint8_t  kasan_cfg_index;
    uint8_t  kasan_cfg_regs[16];
    uint16_t kasan_access_addr;
    uint8_t  kasan_font_data[4];
} et4000_t;

static const uint8_t crtc_mask[0x40] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0x0f, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static video_timings_t timing_et4000_isa = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l = 6, .read_b = 5, .read_w = 5, .read_l = 10 };
static video_timings_t timing_et4000_mca = { .type = VIDEO_MCA, .write_b = 4, .write_w = 5, .write_l = 10, .read_b = 5, .read_w = 5, .read_l = 10 };

static void    et4000_kasan_out(uint16_t addr, uint8_t val, void *priv);
static uint8_t et4000_kasan_in(uint16_t addr, void *priv);

static uint8_t
et4000_in(uint16_t addr, void *priv)
{
    et4000_t *dev  = (et4000_t *) priv;
    svga_t   *svga = &dev->svga;
    uint8_t   ret;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c2:
            if (dev->type == ET4000_TYPE_MCA) {
                if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x4e)
                    return 0;
                else
                    return 0x10;
            }
            break;

        case 0x3c5:
            if ((svga->seqaddr & 0xf) == 7)
                return svga->seqregs[svga->seqaddr & 0xf] | 4;
            break;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            if (dev->type >= ET4000_TYPE_ISA)
                return sc1502x_ramdac_in(addr, svga->ramdac, svga);

        case 0x3cd: /*Banking*/
            return dev->banking;

        case 0x3d4:
            return svga->crtcreg;

        case 0x3d5:
            return svga->crtc[svga->crtcreg];

        case 0x3da:
            svga->attrff = 0;

            if (svga->cgastat & 0x01)
                svga->cgastat &= ~0x30;
            else
                svga->cgastat ^= 0x30;

            ret = svga->cgastat;

            if ((svga->fcr & 0x08) && svga->dispon)
                ret |= 0x08;

            if (ret & 0x08)
                ret &= 0x7f;
            else
                ret |= 0x80;

            return ret;

        default:
            break;
    }

    return svga_in(addr, svga);
}

static uint8_t
et4000k_in(uint16_t addr, void *priv)
{
    et4000_t *dev = (et4000_t *) priv;
    uint8_t   val = 0xff;

    switch (addr) {
        case 0x22cb:
            return dev->port_22cb_val;

        case 0x22cf:
            val = 0;
            switch (dev->get_korean_font_enabled) {
                case 3:
                    if ((dev->port_32cb_val & 0x30) == 0x30) {
                        val = fontdatksc5601[dev->get_korean_font_base].chr[dev->get_korean_font_index++];
                        dev->get_korean_font_index &= 0x1f;
                    } else if ((dev->port_32cb_val & 0x30) == 0x20 && (dev->get_korean_font_base & 0x7f) > 0x20 && (dev->get_korean_font_base & 0x7f) < 0x7f) {
                        switch (dev->get_korean_font_base & 0x3f80) {
                            case 0x2480:
                                if (dev->get_korean_font_index < 16)
                                    val = fontdatksc5601_user[(dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index];
                                else if (dev->get_korean_font_index >= 24 && dev->get_korean_font_index < 40)
                                    val = fontdatksc5601_user[(dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index - 8];
                                break;

                            case 0x3f00:
                                if (dev->get_korean_font_index < 16)
                                    val = fontdatksc5601_user[96 + (dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index];
                                else if (dev->get_korean_font_index >= 24 && dev->get_korean_font_index < 40)
                                    val = fontdatksc5601_user[96 + (dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index - 8];
                                break;

                            default:
                                break;
                        }
                        dev->get_korean_font_index++;
                        dev->get_korean_font_index %= 72;
                    }
                    break;

                case 4:
                    val = 0x0f;
                    break;

                default:
                    break;
            }
            return val;

        case 0x32cb:
            return dev->port_32cb_val;

        default:
            return et4000_in(addr, priv);
    }
}

static void
et4000_out(uint16_t addr, uint8_t val, void *priv)
{
    et4000_t *dev  = (et4000_t *) priv;
    svga_t   *svga = &dev->svga;
    uint8_t   old;
    uint8_t   pal4to16[16] = { 0, 7, 0x38, 0x3f, 0, 3, 4, 0x3f, 0, 2, 4, 0x3e, 0, 3, 5, 0x3f };

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c0:
        case 0x3c1:
            if (!svga->attrff) {
                svga->attraddr = val & 0x1f;
                if ((val & 0x20) != svga->attr_palette_enable) {
                    svga->fullchange          = 3;
                    svga->attr_palette_enable = val & 0x20;
                    svga_recalctimings(svga);
                }
            } else {
                if ((svga->attraddr == 0x13) && (svga->attrregs[0x13] != val))
                    svga->fullchange = svga->monitor->mon_changeframecount;
                old                  = svga->attrregs[svga->attraddr & 0x1f];
                svga->attrregs[svga->attraddr & 0x1f] = val;
                if (svga->attraddr < 0x10)
                    svga->fullchange = svga->monitor->mon_changeframecount;

                if ((svga->attraddr == 0x10) || (svga->attraddr == 0x14) || (svga->attraddr < 0x10)) {
                    for (int c = 0; c < 0x10; c++) {
                        if (svga->attrregs[0x10] & 0x80)
                            svga->egapal[c] = (svga->attrregs[c] & 0xf) | ((svga->attrregs[0x14] & 0xf) << 4);
                        else if (svga->ati_4color)
                            svga->egapal[c] = pal4to16[(c & 0x03) | ((val >> 2) & 0xc)];
                        else
                            svga->egapal[c] = (svga->attrregs[c] & 0x3f) | ((svga->attrregs[0x14] & 0xc) << 4);
                    }
                    svga->fullchange = svga->monitor->mon_changeframecount;
                }
                /* Recalculate timings on change of attribute register 0x11
                   (overscan border color) too. */
                if (svga->attraddr == 0x10) {
                    svga->chain4 &= ~0x02;
                    if ((val & 0x40) && (svga->attrregs[0x10] & 0x40))
                        svga->chain4 |= (svga->seqregs[0x0e] & 0x02);
                    if (old != val)
                        svga_recalctimings(svga);
                } else if (svga->attraddr == 0x11) {
                    svga->overscan_color = svga->pallook[svga->attrregs[0x11]];
                    if (old != val)
                        svga_recalctimings(svga);
                } else if (svga->attraddr == 0x12) {
                    if ((val & 0xf) != svga->plane_mask)
                        svga->fullchange = svga->monitor->mon_changeframecount;
                    svga->plane_mask = val & 0xf;
                }
            }
            svga->attrff ^= 1;
            return;

        case 0x3c5:
            if (svga->seqaddr == 4) {
                svga->seqregs[4] = val;

                svga->chain2_write = !(val & 4);
                svga->chain4       = (svga->chain4 & ~8) | (val & 8);
                svga->fast         = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) && svga->chain4 && !(svga->adv_flags & FLAG_ADDR_BY8);
                return;
            } else if (svga->seqaddr == 0x0e) {
                svga->seqregs[0x0e] = val;
                svga->chain4 &= ~0x02;
                if ((svga->gdcreg[5] & 0x40) && svga->lowres)
                    svga->chain4 |= (svga->seqregs[0x0e] & 0x02);
                svga_recalctimings(svga);
                return;
            }
            break;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            if (dev->type >= ET4000_TYPE_ISA) {
                sc1502x_ramdac_out(addr, val, svga->ramdac, svga);
                return;
            }
            break;

        case 0x3cd: /*Banking*/
            if (!(svga->crtc[0x36] & 0x10) && !(svga->gdcreg[6] & 0x08)) {
                svga->write_bank = (val & 0xf) * 0x10000;
                svga->read_bank  = ((val >> 4) & 0xf) * 0x10000;
            }
            dev->banking = val;
            return;

        case 0x3cf:
            if ((svga->gdcaddr & 15) == 5) {
                svga->chain4 &= ~0x02;
                if ((val & 0x40) && svga->lowres)
                    svga->chain4 |= (svga->seqregs[0x0e] & 0x02);
            } else if ((svga->gdcaddr & 15) == 6) {
                if (!(svga->crtc[0x36] & 0x10) && !(val & 0x08)) {
                    svga->write_bank = (dev->banking & 0x0f) * 0x10000;
                    svga->read_bank  = ((dev->banking >> 4) & 0x0f) * 0x10000;
                } else
                    svga->write_bank = svga->read_bank = 0;

                old = svga->gdcreg[6];
                svga_out(addr, val, svga);
                if ((old & 0xc) != 0 && (val & 0xc) == 0) {
                    /*override mask - ET4000 supports linear 128k at A0000*/
                    svga->banked_mask = 0x1ffff;
                }
                return;
            }
            break;

        case 0x3d4:
            svga->crtcreg = val & 0x3f;
            return;

        case 0x3d5:
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 0x35) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old = svga->crtc[svga->crtcreg];
            val &= crtc_mask[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;

            if (svga->crtcreg == 0x36) {
                if (!(val & 0x10) && !(svga->gdcreg[6] & 0x08)) {
                    svga->write_bank = (dev->banking & 0x0f) * 0x10000;
                    svga->read_bank  = ((dev->banking >> 4) & 0x0f) * 0x10000;
                } else
                    svga->write_bank = svga->read_bank = 0;
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

        default:
            break;
    }

    svga_out(addr, val, svga);
}

static void
et4000k_out(uint16_t addr, uint8_t val, void *priv)
{
    et4000_t *dev = (et4000_t *) priv;

    switch (addr) {
        case 0x22cb:
            dev->port_22cb_val           = (dev->port_22cb_val & 0xf0) | (val & 0x0f);
            dev->get_korean_font_enabled = val & 7;
            if (dev->get_korean_font_enabled == 3)
                dev->get_korean_font_index = 0;
            break;

        case 0x22cf:
            switch (dev->get_korean_font_enabled) {
                case 1:
                    dev->get_korean_font_base = ((val & 0x7f) << 7) | (dev->get_korean_font_base & 0x7f);
                    break;

                case 2:
                    dev->get_korean_font_base = (dev->get_korean_font_base & 0x3f80) | (val & 0x7f) | (((val ^ 0x80) & 0x80) << 8);
                    break;

                case 3:
                    if ((dev->port_32cb_val & 0x30) == 0x20 && (dev->get_korean_font_base & 0x7f) > 0x20 && (dev->get_korean_font_base & 0x7f) < 0x7f) {
                        switch (dev->get_korean_font_base & 0x3f80) {
                            case 0x2480:
                                if (dev->get_korean_font_index < 16)
                                    fontdatksc5601_user[(dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index] = val;
                                else if (dev->get_korean_font_index >= 24 && dev->get_korean_font_index < 40)
                                    fontdatksc5601_user[(dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index - 8] = val;
                                break;

                            case 0x3f00:
                                if (dev->get_korean_font_index < 16)
                                    fontdatksc5601_user[96 + (dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index] = val;
                                else if (dev->get_korean_font_index >= 24 && dev->get_korean_font_index < 40)
                                    fontdatksc5601_user[96 + (dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index - 8] = val;
                                break;

                            default:
                                break;
                        }
                        dev->get_korean_font_index++;
                    }
                    break;

                default:
                    break;
            }
            break;

        case 0x32cb:
            dev->port_32cb_val = val;
            svga_recalctimings(&dev->svga);
            break;

        default:
            et4000_out(addr, val, priv);
            break;
    }
}

static uint8_t
et4000_kasan_in(uint16_t addr, void *priv)
{
    const et4000_t *et4000 = (et4000_t *) priv;
    uint8_t         val    = 0xFF;

    if (addr == 0x258) {
        val = et4000->kasan_cfg_index;
    } else if (addr == 0x259) {
        if (et4000->kasan_cfg_index >= 0xF0) {
            val = et4000->kasan_cfg_regs[et4000->kasan_cfg_index - 0xF0];
            if (et4000->kasan_cfg_index == 0xF4 && et4000->kasan_cfg_regs[0] & 0x20)
                val |= 0x80;
        }
    } else if (addr >= et4000->kasan_access_addr && addr < et4000->kasan_access_addr + 8) {
        switch (addr - ((et4000->kasan_cfg_regs[2] << 8) | (et4000->kasan_cfg_regs[1]))) {
            case 2:
                val = 0;
                break;
            case 5:
                if (((et4000->get_korean_font_base >> 7) & 0x7F) == (et4000->svga.ksc5601_udc_area_msb[0] & 0x7F) && (et4000->svga.ksc5601_udc_area_msb[0] & 0x80))
                    val = fontdatksc5601_user[(et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index];
                else if (((et4000->get_korean_font_base >> 7) & 0x7F) == (et4000->svga.ksc5601_udc_area_msb[1] & 0x7F) && (et4000->svga.ksc5601_udc_area_msb[1] & 0x80))
                    val = fontdatksc5601_user[96 + (et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index];
                else
                    val = fontdatksc5601[et4000->get_korean_font_base].chr[et4000->get_korean_font_index];
                break;
            default:
                break;
        }
    } else
        val = et4000_in(addr, priv);

    return val;
}

static void
et4000_kasan_out(uint16_t addr, uint8_t val, void *priv)
{
    et4000_t *et4000 = (et4000_t *) priv;

    if (addr == 0x258) {
        et4000->kasan_cfg_index = val;
    } else if (addr == 0x259) {
        if (et4000->kasan_cfg_index >= 0xF0) {
            switch (et4000->kasan_cfg_index - 0xF0) {
                case 0:
                    if (et4000->kasan_cfg_regs[4] & 8)
                        val = (val & 0xFC) | (et4000->kasan_cfg_regs[0] & 3);
                    et4000->kasan_cfg_regs[0] = val;
                    svga_recalctimings(&et4000->svga);
                    break;
                case 1:
                case 2:
                    if ((et4000->kasan_cfg_index - 0xF0) <= 16)
                        et4000->kasan_cfg_regs[et4000->kasan_cfg_index - 0xF0] = val;
                    io_removehandler(et4000->kasan_access_addr, 0x0008, et4000_kasan_in, NULL, NULL, et4000_kasan_out, NULL, NULL, et4000);
                    et4000->kasan_access_addr = (et4000->kasan_cfg_regs[2] << 8) | et4000->kasan_cfg_regs[1];
                    io_sethandler(et4000->kasan_access_addr, 0x0008, et4000_kasan_in, NULL, NULL, et4000_kasan_out, NULL, NULL, et4000);
                    break;
                case 4:
                    if (et4000->kasan_cfg_regs[0] & 0x20)
                        val |= 0x80;
                    et4000->svga.ksc5601_swap_mode = (val & 4) >> 2;
                    et4000->kasan_cfg_regs[4]      = val;
                    svga_recalctimings(&et4000->svga);
                    break;
                case 5:
                    et4000->kasan_cfg_regs[5]              = val;
                    et4000->svga.ksc5601_english_font_type = 0x100 | val;
                    fallthrough;
                case 6:
                case 7:
                    et4000->svga.ksc5601_udc_area_msb[et4000->kasan_cfg_index - 0xF6] = val;
                default:
                    et4000->kasan_cfg_regs[et4000->kasan_cfg_index - 0xF0] = val;
                    svga_recalctimings(&et4000->svga);
                    break;
            }
        }
    } else if (addr >= et4000->kasan_access_addr && addr < et4000->kasan_access_addr + 8) {
        switch (addr - ((et4000->kasan_cfg_regs[2] << 8) | (et4000->kasan_cfg_regs[1]))) {
            case 0:
                if (et4000->kasan_cfg_regs[0] & 2) {
                    et4000->get_korean_font_index = ((val & 1) << 4) | ((val & 0x1E) >> 1);
                    et4000->get_korean_font_base  = (et4000->get_korean_font_base & ~7) | (val >> 5);
                }
                break;
            case 1:
                if (et4000->kasan_cfg_regs[0] & 2)
                    et4000->get_korean_font_base = (et4000->get_korean_font_base & ~0x7F8) | (val << 3);
                break;
            case 2:
                if (et4000->kasan_cfg_regs[0] & 2)
                    et4000->get_korean_font_base = (et4000->get_korean_font_base & ~0x7F800) | ((val & 7) << 11);
                break;
            case 3:
            case 4:
            case 5:
                if (et4000->kasan_cfg_regs[0] & 1) {
                    if ((addr - (((et4000->kasan_cfg_regs[2] << 8) | (et4000->kasan_cfg_regs[1])) + 3)) <= 4)
                        et4000->kasan_font_data[addr - (((et4000->kasan_cfg_regs[2] << 8) | (et4000->kasan_cfg_regs[1])) + 3)] = val;
                }
                break;
            case 6:
                if ((et4000->kasan_cfg_regs[0] & 1) && (et4000->kasan_font_data[3] & !(val & 0x80)) && (et4000->get_korean_font_base & 0x7F) >= 0x20 && (et4000->get_korean_font_base & 0x7F) < 0x7F) {
                    if (((et4000->get_korean_font_base >> 7) & 0x7F) == (et4000->svga.ksc5601_udc_area_msb[0] & 0x7F) && (et4000->svga.ksc5601_udc_area_msb[0] & 0x80))
                        fontdatksc5601_user[(et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index] = et4000->kasan_font_data[2];
                    else if (((et4000->get_korean_font_base >> 7) & 0x7F) == (et4000->svga.ksc5601_udc_area_msb[1] & 0x7F) && (et4000->svga.ksc5601_udc_area_msb[1] & 0x80))
                        fontdatksc5601_user[96 + (et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index] = et4000->kasan_font_data[2];
                }
                et4000->kasan_font_data[3] = val;
                break;
            default:
                break;
        }
    } else
        et4000_out(addr, val, priv);
}

uint32_t
get_et4000_addr(uint32_t addr, void *priv)
{
    const svga_t *svga = (svga_t *) priv;
    uint32_t      nbank;

    switch (svga->crtc[0x37] & 0x0B) {
        case 0x00:
        case 0x01:
            nbank = 0;
            addr &= 0xFFFF;
            break;
        case 0x02:
            nbank = (addr & 1) << 1;
            addr  = (addr >> 1) & 0xFFFF;
            break;
        case 0x03:
            nbank = addr & 3;
            addr  = (addr >> 2) & 0xFFFF;
            break;
        case 0x08:
        case 0x09:
            nbank = 0;
            addr &= 0x3FFFF;
            break;
        case 0x0A:
            nbank = (addr & 1) << 1;
            addr  = (addr >> 1) & 0x3FFFF;
            break;
        case 0x0B:
            nbank = addr & 3;
            addr  = (addr >> 2) & 0x3FFFF;
            break;
        default:
            nbank = 0;
            break;
    }

    if (svga->vram_max >= 1024 * 1024) {
        addr = (addr << 2) | (nbank & 3);
        if ((svga->crtc[0x37] & 3) == 2)
            addr >>= 1;
        else if ((svga->crtc[0x37] & 3) < 2)
            addr >>= 2;
    } else if (svga->vram_max >= 512 * 1024) {
        addr = (addr << 1) | ((nbank & 2) >> 1) | ((nbank & 1) << 19);
        if ((svga->crtc[0x37] & 3) < 2)
            addr >>= 1;
    } else if (svga->vram_max >= 256 * 1024)
        addr = addr | (nbank << 18);
    else if (svga->vram_max > 128 * 1024) {
        addr = (addr << 1) | ((nbank & 2) >> 1) | ((nbank & 1) << 17);
        if ((svga->crtc[0x37] & 3) < 2)
            addr >>= 1;
    } else
        addr = addr | (nbank << 16);

    return addr;
}

static void
et4000_recalctimings(svga_t *svga)
{
    const et4000_t *dev = (et4000_t *) svga->priv;

    svga->ma_latch |= (svga->crtc[0x33] & 3) << 16;

    svga->hblankstart = (((svga->crtc[0x3f] & 0x4) >> 2) << 8) + svga->crtc[2];

    svga->ps_bit_bug = (dev->type == ET4000_TYPE_TC6058AF) && svga->lowres && ((svga->gdcreg[5] & 0x60) >= 0x40);

    if (svga->crtc[0x35] & 1)
        svga->vblankstart |= 0x400;
    if (svga->crtc[0x35] & 2)
        svga->vtotal |= 0x400;
    if (svga->crtc[0x35] & 4)
        svga->dispend |= 0x400;
    if (svga->crtc[0x35] & 8)
        svga->vsyncstart |= 0x400;
    if (svga->crtc[0x35] & 0x10)
        svga->split |= 0x400;
    if (!svga->rowoffset && !svga->ps_bit_bug)
        svga->rowoffset = 0x100;
    if (svga->crtc[0x3f] & 1)
        svga->htotal |= 0x100;
    if (svga->attrregs[0x16] & 0x20) {
        svga->hdisp <<= 1;
        svga->dots_per_clock <<= 1;
    }

    switch (((svga->miscout >> 2) & 3) | ((svga->crtc[0x34] << 1) & 4)) {
        case 0:
        case 1:
            break;
        case 3:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 40000000.0;
            break;
        case 5:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 65000000.0;
            break;
        default:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 36000000.0;
            break;
    }

    switch (svga->bpp) {
        case 15:
        case 16:
            svga->hdisp /= 2;
            svga->dots_per_clock /= 2;
            break;

        case 24:
            svga->hdisp /= 3;
            svga->dots_per_clock /= 3;
            break;

        default:
            break;
    }

    if (dev->type == ET4000_TYPE_KOREAN || dev->type == ET4000_TYPE_TRIGEM || dev->type == ET4000_TYPE_KASAN) {
        if ((svga->render == svga_render_text_80) && ((svga->crtc[0x37] & 0x0A) == 0x0A)) {
            if (dev->port_32cb_val & 0x80) {
                svga->ma_latch -= 2;
                svga->ca_adj = -2;
            }
            if ((dev->port_32cb_val & 0xB4) == ((svga->crtc[0x37] & 3) == 2 ? 0xB4 : 0xB0)) {
                svga->render = svga_render_text_80_ksc5601;
            }
        }
    }

    if ((svga->bpp == 8) && ((svga->gdcreg[5] & 0x60) >= 0x40)) {
        svga->map8 = svga->pallook;
        if (svga->lowres)
            svga->render = svga_render_8bpp_lowres;
        else
            svga->render = svga_render_8bpp_highres;
    }

    if ((svga->seqregs[0x0e] & 0x02) && ((svga->gdcreg[5] & 0x60) >= 0x40) && svga->lowres) {
        svga->ma_latch <<= 1;
        svga->rowoffset <<= 1;
        svga->render = svga_render_8bpp_highres;
    }
}

static void
et4000_kasan_recalctimings(svga_t *svga)
{
    const et4000_t *et4000 = (et4000_t *) svga->priv;

    et4000_recalctimings(svga);

    if (svga->render == svga_render_text_80 && (et4000->kasan_cfg_regs[0] & 8)) {
        svga->hdisp             += svga->dots_per_clock;
        svga->ma_latch          -= 4;
        svga->ca_adj             = (et4000->kasan_cfg_regs[0] >> 6) - 3;
        svga->ksc5601_sbyte_mask = (et4000->kasan_cfg_regs[0] & 4) << 5;
        if ((et4000->kasan_cfg_regs[0] & 0x23) == 0x20 && (et4000->kasan_cfg_regs[4] & 0x80) && ((svga->crtc[0x37] & 0x0B) == 0x0A))
            svga->render = svga_render_text_80_ksc5601;
    }
}

static uint8_t
et4000_mca_read(int port, void *priv)
{
    const et4000_t *et4000 = (et4000_t *) priv;

    return (et4000->pos_regs[port & 7]);
}

static void
et4000_mca_write(int port, uint8_t val, void *priv)
{
    et4000_t *et4000 = (et4000_t *) priv;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    /* Save the MCA register value. */
    et4000->pos_regs[port & 7] = val;
}

static uint8_t
et4000_mca_feedb(UNUSED(void *priv))
{
    return 1;
}

static void *
et4000_init(const device_t *info)
{
    const char *bios_ver = NULL;
    const char *fn;
    et4000_t   *dev;
    int         i;

    dev = (et4000_t *) malloc(sizeof(et4000_t));
    memset(dev, 0x00, sizeof(et4000_t));
    dev->name = info->name;
    dev->type = info->local;
    fn        = BIOS_ROM_PATH;

    switch (dev->type) {
        case ET4000_TYPE_TC6058AF: /* ISA ET4000AX (TC6058AF) */
        case ET4000_TYPE_ISA: /* ISA ET4000AX */
            dev->vram_size = device_get_config_int("memory") << 10;
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et4000_isa);
            svga_init(info, &dev->svga, dev, dev->vram_size,
                      et4000_recalctimings, et4000_in, et4000_out,
                      NULL, NULL);
            io_sethandler(0x03c0, 32,
                          et4000_in, NULL, NULL, et4000_out, NULL, NULL, dev);
            bios_ver      = (char *) device_get_config_bios("bios_ver");
            fn            = (char *) device_get_bios_file(info, bios_ver, 0);
            break;

        case ET4000_TYPE_MCA: /* MCA ET4000AX */
            dev->vram_size = 1024 << 10;
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et4000_mca);
            svga_init(info, &dev->svga, dev, dev->vram_size,
                      et4000_recalctimings, et4000_in, et4000_out,
                      NULL, NULL);
            io_sethandler(0x03c0, 32,
                          et4000_in, NULL, NULL, et4000_out, NULL, NULL, dev);
            dev->pos_regs[0] = 0xf2; /* ET4000 MCA board ID */
            dev->pos_regs[1] = 0x80;
            mca_add(et4000_mca_read, et4000_mca_write, et4000_mca_feedb, NULL, dev);
            break;

        case ET4000_TYPE_KOREAN: /* Korean ET4000 */
        case ET4000_TYPE_TRIGEM: /* Trigem 286M ET4000 */
            dev->vram_size                      = device_get_config_int("memory") << 10;
            dev->port_22cb_val                  = 0x60;
            dev->port_32cb_val                  = 0;
            dev->svga.ksc5601_sbyte_mask        = 0x80;
            dev->svga.ksc5601_udc_area_msb[0]   = 0xC9;
            dev->svga.ksc5601_udc_area_msb[1]   = 0xFE;
            dev->svga.ksc5601_swap_mode         = 0;
            dev->svga.ksc5601_english_font_type = 0;
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et4000_isa);
            svga_init(info, &dev->svga, dev, dev->vram_size,
                      et4000_recalctimings, et4000k_in, et4000k_out,
                      NULL, NULL);
            io_sethandler(0x03c0, 32,
                          et4000k_in, NULL, NULL, et4000k_out, NULL, NULL, dev);
            io_sethandler(0x22cb, 1,
                          et4000k_in, NULL, NULL, et4000k_out, NULL, NULL, dev);
            io_sethandler(0x22cf, 1,
                          et4000k_in, NULL, NULL, et4000k_out, NULL, NULL, dev);
            io_sethandler(0x32cb, 1,
                          et4000k_in, NULL, NULL, et4000k_out, NULL, NULL, dev);
            loadfont(KOREAN_FONT_ROM_PATH, 6);
            fn = KOREAN_BIOS_ROM_PATH;
            break;

        case ET4000_TYPE_KASAN: /* Kasan ET4000 */
            dev->vram_size                      = device_get_config_int("memory") << 10;
            dev->svga.ksc5601_sbyte_mask        = 0;
            dev->svga.ksc5601_udc_area_msb[0]   = 0xC9;
            dev->svga.ksc5601_udc_area_msb[1]   = 0xFE;
            dev->svga.ksc5601_swap_mode         = 0;
            dev->svga.ksc5601_english_font_type = 0x1FF;
            dev->kasan_cfg_index                = 0;
            for (i = 0; i < 16; i++)
                dev->kasan_cfg_regs[i] = 0;
            for (i = 0; i < 4; i++)
                dev->kasan_font_data[i] = 0;
            dev->kasan_cfg_regs[1] = 0x50;
            dev->kasan_cfg_regs[2] = 2;
            dev->kasan_cfg_regs[3] = 6;
            dev->kasan_cfg_regs[4] = 0x78;
            dev->kasan_cfg_regs[5] = 0xFF;
            dev->kasan_cfg_regs[6] = 0xC9;
            dev->kasan_cfg_regs[7] = 0xFE;
            dev->kasan_access_addr = 0x250;
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et4000_isa);
            svga_init(info, &dev->svga, dev, dev->vram_size,
                      et4000_kasan_recalctimings, et4000_in, et4000_out,
                      NULL, NULL);
            io_sethandler(0x03c0, 32,
                          et4000k_in, NULL, NULL, et4000k_out, NULL, NULL, dev);
            io_sethandler(0x0250, 8,
                          et4000_kasan_in, NULL, NULL, et4000_kasan_out, NULL, NULL, dev);
            io_sethandler(0x0258, 2,
                          et4000_kasan_in, NULL, NULL, et4000_kasan_out, NULL, NULL, dev);
            loadfont(KASAN_FONT_ROM_PATH, 6);
            fn = KASAN_BIOS_ROM_PATH;
            break;

        default:
            break;
    }

    if (dev->type >= ET4000_TYPE_ISA)
        dev->svga.ramdac = device_add(&sc1502x_ramdac_device);

    dev->vram_mask = dev->vram_size - 1;

    rom_init(&dev->bios_rom, fn,
             0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    dev->svga.translate_address = get_et4000_addr;

    dev->svga.packed_chain4 = 1;

    return dev;
}

static void
et4000_close(void *priv)
{
    et4000_t *dev = (et4000_t *) priv;

    svga_close(&dev->svga);

    free(dev);
}

static void
et4000_speed_changed(void *priv)
{
    et4000_t *dev = (et4000_t *) priv;

    svga_recalctimings(&dev->svga);
}

static void
et4000_force_redraw(void *priv)
{
    et4000_t *dev = (et4000_t *) priv;

    dev->svga.fullchange = changeframecount;
}

static int
et4000_available(void)
{
    return rom_present(BIOS_ROM_PATH);
}

static int
et4000k_available(void)
{
    return rom_present(KOREAN_BIOS_ROM_PATH) && rom_present(KOREAN_FONT_ROM_PATH);
}

static int
et4000_kasan_available(void)
{
    return rom_present(KASAN_BIOS_ROM_PATH) && rom_present(KASAN_FONT_ROM_PATH);
}

static const device_config_t et4000_tc6058af_config[] = {
  // clang-format off
    {
        .name = "memory",
        .description = "Memory size",
        .type = CONFIG_SELECTION,
        .default_int = 512,
        .selection = {
            {
                .description = "256 KB",
                .value = 256
            },
            {
                .description = "512 KB",
                .value = 512
            },
            {
                .description = "1 MB",
                .value = 1024
            },
            {
                .description = ""
            }
        }
    },
    {
        .name = "bios_ver",
        .description = "BIOS Version",
        .type = CONFIG_BIOS,
        .default_string = "v1_10",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 }, /*W1*/
        .bios = {
            { .name = "Version 1.10", .internal_name = "v1_10", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 32768, .files = { TC6058AF_BIOS_ROM_PATH, "" } },
            { .name = "Version 1.21", .internal_name = "v1_21", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 32768, .files = { V1_21_BIOS_ROM_PATH, "" } },
            { .files_no = 0 }
        },
    },
    {
        .type = CONFIG_END
    }
// clang-format on
};

static const device_config_t et4000_bios_config[] = {
  // clang-format off
    {
        .name = "memory",
        .description = "Memory size",
        .type = CONFIG_SELECTION,
        .default_int = 1024,
        .selection = {
            {
                .description = "256 KB",
                .value = 256
            },
            {
                .description = "512 KB",
                .value = 512
            },
            {
                .description = "1 MB",
                .value = 1024
            },
            {
                .description = ""
            }
        }
    },
    {
        .name = "bios_ver",
        .description = "BIOS Version",
        .type = CONFIG_BIOS,
        .default_string = "v8_01",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 }, /*W1*/
        .bios = {
            { .name = "Version 8.01", .internal_name = "v8_01", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 32768, .files = { BIOS_ROM_PATH, "" } },
            { .name = "Version 8.06", .internal_name = "v8_06", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 32768, .files = { V8_06_BIOS_ROM_PATH, "" } },
            { .files_no = 0 }
        },
    },
    {
        .type = CONFIG_END
    }
  // clang-format on
};

static const device_config_t et4000_config[] = {
  // clang-format off
    {
        .name = "memory",
        .description = "Memory size",
        .type = CONFIG_SELECTION,
        .default_int = 1024,
        .selection = {
            {
                .description = "256 KB",
                .value = 256
            },
            {
                .description = "512 KB",
                .value = 512
            },
            {
                .description = "1 MB",
                .value = 1024
            },
            {
                .description = ""
            }
        }
    },
    {
        .type = CONFIG_END
    }
  // clang-format on
};

const device_t et4000_tc6058af_isa_device = {
    .name          = "Tseng Labs ET4000AX (TC6058AF) (ISA)",
    .internal_name = "et4000ax_tc6058af",
    .flags         = DEVICE_ISA,
    .local         = ET4000_TYPE_TC6058AF,
    .init          = et4000_init,
    .close         = et4000_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = et4000_speed_changed,
    .force_redraw  = et4000_force_redraw,
    .config        = et4000_tc6058af_config
};

const device_t et4000_isa_device = {
    .name          = "Tseng Labs ET4000AX (ISA)",
    .internal_name = "et4000ax",
    .flags         = DEVICE_ISA,
    .local         = ET4000_TYPE_ISA,
    .init          = et4000_init,
    .close         = et4000_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = et4000_speed_changed,
    .force_redraw  = et4000_force_redraw,
    .config        = et4000_bios_config
};

const device_t et4000_mca_device = {
    .name          = "Tseng Labs ET4000AX (MCA)",
    .internal_name = "et4000mca",
    .flags         = DEVICE_MCA,
    .local         = ET4000_TYPE_MCA,
    .init          = et4000_init,
    .close         = et4000_close,
    .reset         = NULL,
    { .available = et4000_available },
    .speed_changed = et4000_speed_changed,
    .force_redraw  = et4000_force_redraw,
    .config        = et4000_config
};

const device_t et4000k_isa_device = {
    .name          = "Trigem Korean VGA (Tseng Labs ET4000AX Korean)",
    .internal_name = "tgkorvga",
    .flags         = DEVICE_ISA,
    .local         = ET4000_TYPE_KOREAN,
    .init          = et4000_init,
    .close         = et4000_close,
    .reset         = NULL,
    { .available = et4000k_available },
    .speed_changed = et4000_speed_changed,
    .force_redraw  = et4000_force_redraw,
    .config        = et4000_config
};

const device_t et4000k_tg286_isa_device = {
    .name          = "Trigem Korean VGA (Trigem 286M)",
    .internal_name = "et4000k_tg286_isa",
    .flags         = DEVICE_ISA,
    .local         = ET4000_TYPE_TRIGEM,
    .init          = et4000_init,
    .close         = et4000_close,
    .reset         = NULL,
    { .available = et4000k_available },
    .speed_changed = et4000_speed_changed,
    .force_redraw  = et4000_force_redraw,
    .config        = et4000_config
};

const device_t et4000_kasan_isa_device = {
    .name          = "Kasan Hangulmadang-16 VGA (Tseng Labs ET4000AX Korean)",
    .internal_name = "kasan16vga",
    .flags         = DEVICE_ISA,
    .local         = ET4000_TYPE_KASAN,
    .init          = et4000_init,
    .close         = et4000_close,
    .reset         = NULL,
    { .available = et4000_kasan_available },
    .speed_changed = et4000_speed_changed,
    .force_redraw  = et4000_force_redraw,
    .config        = et4000_config
};
