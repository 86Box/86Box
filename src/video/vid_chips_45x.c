/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Chips & Technologies 82C450/82C451/82C452 (EVGA/SVGA) emulation.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2023-2026 Cacodemon345.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
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
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/plat_unused.h>
#include "cpu.h"

#define BIOS_82C450_PATH "roms/video/chips/ct82c450.bin"
#define BIOS_82C451_PATH "roms/video/chips/evga16.vbi"
#define BIOS_82C452_PATH "roms/video/chips/wvga16s.bin"

enum {
    CHIPS_82C450 = 0,
    CHIPS_82C451,
    CHIPS_82C452
};

typedef struct chips_45x_t {
    svga_t svga;

    rom_t bios_rom;

    int      type;
    uint32_t vram_size;

    uint8_t xr_index;
    uint8_t xr[0x80];

    uint8_t setup_ctrl; /* 46E8h */
    uint8_t global_enable; /* 102h */
    uint8_t ext_enable; /* 103h */

    uint32_t bank[2];
    int      ext_map;
    int      dual_map;

    /* CGA / Hercules emulation (XR14). */
    uint8_t cga_mode; /* 3D8h */
    uint8_t herc_mode; /* 3B8h */
    uint8_t herc_config; /* 3BFh */
} chips_45x_t;

enum {
    EMU_VGA = 0,
    EMU_CGA,
    EMU_HERC,
    EMU_EGA
};

/* CGA IRGB colors as EGA/VGA attribute palette values. */
static const uint8_t cga_to_ega[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
};

static video_timings_t timing_chips_45x = { .type = VIDEO_ISA, .write_b = 6, .write_w = 8, .write_l = 16, .read_b = 6, .read_w = 8, .read_l = 16 };

#ifdef ENABLE_CHIPS_45X_LOG
int chips_45x_do_log = ENABLE_CHIPS_45X_LOG;

static void
chips_45x_log(const char *fmt, ...)
{
    va_list ap;

    if (chips_45x_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define chips_45x_log(fmt, ...)
#endif

static int
chips_45x_vga_enabled(const chips_45x_t *chips)
{
    return (chips->setup_ctrl & 0x08) && (chips->global_enable & 0x01);
}

static void
chips_45x_set_seq(svga_t *svga, uint8_t idx, uint8_t val)
{
    const uint8_t addr = svga->seqaddr;

    if (svga->seqregs[idx] != val) {
        svga_out(0x3c4, idx, svga);
        svga_out(0x3c5, val, svga);
    }
    svga->seqaddr = addr;
}

static void
chips_45x_set_gdc(svga_t *svga, uint8_t idx, uint8_t val)
{
    const uint8_t addr = svga->gdcaddr;

    if (svga->gdcreg[idx] != val) {
        svga_out(0x3ce, idx, svga);
        svga_out(0x3cf, val, svga);
    }
    svga->gdcaddr = addr;
}

static void
chips_45x_set_attr(svga_t *svga, uint8_t idx, uint8_t val)
{
    const int     ff   = svga->attrff;
    const uint8_t addr = svga->attraddr;

    if (svga->attrregs[idx] != val) {
        svga->attrff = 0;
        svga_out(0x3c0, idx | svga->attr_palette_enable, svga);
        svga_out(0x3c0, val, svga);
    }
    svga->attrff   = ff;
    svga->attraddr = addr;
}

static int
chips_45x_emu_mode(const chips_45x_t *chips)
{
    const uint8_t mode = chips->xr[0x14] & 0x03;

    /* 82C450: VGA, CGA, MDA/Hercules, EGA. 82C451/452: VGA/EGA, CGA, MDA, MDA/Hercules. */
    switch (mode) {
        case 1:
            return EMU_CGA;
        case 2:
            return EMU_HERC;
        case 3:
            return (chips->type == CHIPS_82C450) ? EMU_EGA : EMU_HERC;
        default:
            return EMU_VGA;
    }
}

/*
   In CGA and Hercules emulation the chip follows the emulated mode register on
   its own (the VGA registers are normally locked through XR15), switching
   between text and graphics and selecting the CGA palette.
*/
static void
chips_45x_emu_apply(chips_45x_t *chips)
{
    svga_t       *svga = &chips->svga;
    const int     emu  = chips_45x_emu_mode(chips);
    uint8_t       gr5  = svga->gdcreg[5];
    uint8_t       gr6  = svga->gdcreg[6];
    uint8_t       sr1  = svga->seqregs[1];
    uint8_t       ar10 = svga->attrregs[0x10];
    uint8_t       cr17 = svga->crtc[0x17];
    uint8_t       mode;
    uint8_t       color;
    int           graphics;

    if (emu == EMU_CGA) {
        mode     = chips->cga_mode;
        color    = chips->xr[0x7e];
        graphics = !!(mode & 0x02);
    } else if (emu == EMU_HERC) {
        mode     = chips->herc_mode;
        color    = 0;
        graphics = (mode & 0x02) && (chips->herc_config & 0x01);
    } else
        return;

    if (graphics) {
        gr6 |= 0x01;
        ar10 = (ar10 & ~0x08) | 0x01;
        cr17 &= (emu == EMU_HERC) ? ~0x03 : ~0x01;
        gr5 &= ~0x60;
        if (emu == EMU_CGA) {
            /* Plane 2 holds the font in emulation, keep it out of the pixels. */
            if (mode & 0x10) {
                /* 640x200 two colors. */
                sr1 &= ~0x08;
                chips_45x_set_attr(svga, 0x12, 0x01);
                chips_45x_set_attr(svga, 0x01, cga_to_ega[color & 0x0f]);
            } else {
                /* 320x200 four colors. */
                static const uint8_t pal[2][3] = { { 2, 4, 6 }, { 3, 5, 7 } };
                const uint8_t        inten     = (color & 0x10) ? 0x08 : 0x00;

                gr5 |= 0x20;
                sr1 |= 0x08;
                chips_45x_set_attr(svga, 0x12, 0x03);
                chips_45x_set_attr(svga, 0x00, cga_to_ega[color & 0x0f]);
                for (int i = 0; i < 3; i++)
                    chips_45x_set_attr(svga, 0x01 + i, cga_to_ega[pal[!!(color & 0x20)][i] | inten]);
            }
        }
    } else {
        gr6 &= ~0x01;
        ar10 = (ar10 & ~0x09) | ((mode & 0x20) ? 0x08 : 0x00);
        cr17 |= (emu == EMU_HERC) ? 0x03 : 0x01;
        gr5 &= ~0x60;
        chips_45x_set_attr(svga, 0x12, 0x0f);
        if (emu == EMU_CGA) {
            sr1 = (sr1 & ~0x08) | ((mode & 0x01) ? 0x00 : 0x08);
            chips_45x_set_attr(svga, 0x11, cga_to_ega[color & 0x0f]);
        }
    }

    /* Video enable. */
    sr1 = (sr1 & ~0x20) | ((mode & 0x08) ? 0x00 : 0x20);

    chips_45x_set_gdc(svga, 5, gr5);
    chips_45x_set_gdc(svga, 6, gr6);
    chips_45x_set_seq(svga, 1, sr1);
    chips_45x_set_attr(svga, 0x10, ar10);
    svga->crtc[0x17] = cr17;

    svga->fullchange = changeframecount;
    svga_recalctimings(svga);
}

static void
chips_45x_update_mapping(chips_45x_t *chips)
{
    svga_t  *svga   = &chips->svga;
    int      packed = 0;

    chips->ext_map  = 0;
    chips->dual_map = 0;
    chips->bank[0] = chips->bank[1] = 0;

    /* Banks are kept in display memory (plane) address units; chips_45x_decode()
       scales them to CPU bytes when the sequencer is in chain-4 mode. */
    if (chips->type == CHIPS_82C451) {
        /* Quad mode: packed 256-color pixels, 64K pages selected by XR0B. */
        packed = !!(chips->xr[0x04] & 0x04);
        if (packed) {
            chips->ext_map = 1;
            chips->bank[0] = (chips->xr[0x0b] & 0x03) << 14;
        }
    } else {
        packed = !!(chips->xr[0x0b] & 0x04);
        if (chips->xr[0x0b] & 0x01) {
            chips->ext_map = 1;
            if (chips->type == CHIPS_82C452) {
                chips->bank[0] = (chips->xr[0x10] & 0x3f) << 12;
                chips->bank[1] = (chips->xr[0x11] & 0x3f) << 12;
            } else {
                chips->bank[0] = chips->xr[0x10] << 10;
                chips->bank[1] = chips->xr[0x11] << 10;
            }

            if ((chips->xr[0x0b] & 0x02) && (((svga->gdcreg[6] >> 2) & 3) <= 1))
                chips->dual_map = 1;
        }
    }

    if (svga->packed_chain4 != packed) {
        svga->packed_chain4 = packed;
        svga->fast          = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) &&
                              ((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only);
        svga->fullchange    = changeframecount;
        svga_recalctimings(svga);
    }
}

static void
chips_45x_update_enable(chips_45x_t *chips)
{
    if (chips_45x_vga_enabled(chips))
        mem_mapping_enable(&chips->svga.mapping);
    else
        mem_mapping_disable(&chips->svga.mapping);
}

static uint32_t
chips_45x_decode(chips_45x_t *chips, uint32_t addr)
{
    const svga_t *svga = &chips->svga;
    uint32_t      half;
    const int     shift = svga->chain4 ? 2 : 0;

    addr &= 0x1ffff;

    switch ((svga->gdcreg[6] >> 2) & 3) {
        case 0:
            half = 0x10000;
            break;
        case 1:
            if (addr >= 0x10000)
                return 0xffffffff;
            half = 0x8000;
            break;
        case 2:
            addr -= 0x10000;
            if (addr >= 0x8000)
                return 0xffffffff;
            half = 0;
            break;
        default:
            addr -= 0x18000;
            if (addr >= 0x8000)
                return 0xffffffff;
            half = 0;
            break;
    }

    if (chips->dual_map && half && (addr >= half))
        addr = (addr - half) + (chips->bank[1] << shift);
    else
        addr += chips->bank[0] << shift;

    /* Display memory addresses wrap around at the installed size; drivers rely on
       this when copying bottom-up, where a map base goes "negative". */
    return addr & (svga->vram_mask >> (2 - shift));
}

static void
chips_45x_write(uint32_t addr, uint8_t val, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    if (!chips->ext_map) {
        svga_write(addr, val, &chips->svga);
        return;
    }

    addr = chips_45x_decode(chips, addr);
    if (addr != 0xffffffff)
        svga_write_linear(addr, val, &chips->svga);
}

static void
chips_45x_writew(uint32_t addr, uint16_t val, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    if (!chips->ext_map) {
        svga_writew(addr, val, &chips->svga);
        return;
    }

    addr = chips_45x_decode(chips, addr);
    if (addr != 0xffffffff)
        svga_writew_linear(addr, val, &chips->svga);
}

static void
chips_45x_writel(uint32_t addr, uint32_t val, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    if (!chips->ext_map) {
        svga_writel(addr, val, &chips->svga);
        return;
    }

    addr = chips_45x_decode(chips, addr);
    if (addr != 0xffffffff)
        svga_writel_linear(addr, val, &chips->svga);
}

static uint8_t
chips_45x_read(uint32_t addr, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    if (!chips->ext_map)
        return svga_read(addr, &chips->svga);

    addr = chips_45x_decode(chips, addr);
    if (addr == 0xffffffff)
        return 0xff;

    return svga_read_linear(addr, &chips->svga);
}

static uint16_t
chips_45x_readw(uint32_t addr, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    if (!chips->ext_map)
        return svga_readw(addr, &chips->svga);

    addr = chips_45x_decode(chips, addr);
    if (addr == 0xffffffff)
        return 0xffff;

    return svga_readw_linear(addr, &chips->svga);
}

static uint32_t
chips_45x_readl(uint32_t addr, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    if (!chips->ext_map)
        return svga_readl(addr, &chips->svga);

    addr = chips_45x_decode(chips, addr);
    if (addr == 0xffffffff)
        return 0xffffffff;

    return svga_readl_linear(addr, &chips->svga);
}

static uint8_t
chips_45x_xr_read(chips_45x_t *chips)
{
    const svga_t *svga = &chips->svga;
    uint8_t       ret;

    switch (chips->xr_index) {
        case 0x00:
            switch (chips->type) {
                default:
                case CHIPS_82C450:
                    ret = 0x40;
                    break;
                case CHIPS_82C451:
                    ret = 0x04;
                    break;
                case CHIPS_82C452:
                    ret = 0x14;
                    break;
            }
            break;
        case 0x01:
            /* Configuration straps / DIP switches: ISA, ROM decode enabled. */
            ret = (chips->type == CHIPS_82C450) ? 0xff : 0x7f;
            break;
        case 0x02:
            ret = (chips->xr[0x02] & 0x7f) | (svga->attrff ? 0x80 : 0x00);
            break;
        case 0x14:
            ret = (chips->xr[0x14] & ~0x0c) | ((chips->herc_config & 0x03) << 2);
            break;
        default:
            ret = chips->xr[chips->xr_index];
            break;
    }

    return ret;
}

static void
chips_45x_xr_write(chips_45x_t *chips, uint8_t val)
{
    svga_t *svga = &chips->svga;
    uint8_t idx  = chips->xr_index;
    uint8_t old  = chips->xr[idx];

    switch (idx) {
        case 0x00:
        case 0x01:
            return;
        case 0x04:
            if (chips->type == CHIPS_82C450)
                val &= 0xe5;
            else if (chips->type == CHIPS_82C451)
                val &= 0x04;
            else
                val &= 0x07;
            break;
        case 0x0b:
            val &= (chips->type == CHIPS_82C451) ? 0x03 : 0x07;
            break;
        case 0x0c:
            val &= (chips->type == CHIPS_82C452) ? 0x03 : 0x01;
            break;
        case 0x10:
        case 0x11:
            if (chips->type == CHIPS_82C452)
                val &= 0x3f;
            break;
        default:
            break;
    }

    chips->xr[idx] = val;
    chips_45x_log("C&T 45x: XR%02X = %02X\n", idx, val);

    switch (idx) {
        case 0x04:
            if (chips->type == CHIPS_82C451)
                chips_45x_update_mapping(chips);
            else
                svga->vram_display_mask = (val & 0x04) ? svga->vram_mask : MIN(svga->vram_mask, 0x3ffff);
            if (old != val)
                svga_recalctimings(svga);
            break;
        case 0x0b:
        case 0x10:
        case 0x11:
            chips_45x_update_mapping(chips);
            break;
        case 0x14:
            if ((old ^ val) & 0x03)
                chips_45x_emu_apply(chips);
            break;
        case 0x7e:
            if (old != val)
                chips_45x_emu_apply(chips);
            break;
        case 0x05:
        case 0x0c:
        case 0x18 ... 0x1e:
        case 0x28:
            if (old != val) {
                svga->fullchange = changeframecount;
                svga_recalctimings(svga);
            }
            break;
        default:
            break;
    }
}

static int
chips_45x_xr_port(const chips_45x_t *chips, uint16_t addr)
{
    if (!(chips->ext_enable & 0x80))
        return 0;

    /* 103h bit 6 moves the extension registers from 3D6h/3D7h to 3B6h/3B7h. */
    if (chips->ext_enable & 0x40)
        return (addr & 0xfffe) == 0x3b6;

    return (addr & 0xfffe) == 0x3d6;
}

/* Bits of a CRTC register that XR15 (and CR11 bit 7) leave writable. */
static uint8_t
chips_45x_crtc_wmask(const chips_45x_t *chips, uint8_t idx)
{
    const uint8_t wp = chips->xr[0x15];
    const int     g0 = (chips->svga.crtc[0x11] & 0x80) || (wp & 0x40);
    const int     g2 = !!(wp & 0x02);
    const int     g3 = !!(wp & 0x04);
    const int     g4 = !!(wp & 0x08);
    uint8_t       mask = 0xff;

    switch (idx) {
        case 0x00 ... 0x06:
            if (g0)
                mask = 0x00;
            break;
        case 0x07:
            if (g0)
                mask &= 0x10;
            if (g3)
                mask &= ~0x10;
            break;
        case 0x09:
            if (g2)
                mask &= ~0x1f;
            if (g4)
                mask &= ~0xe0;
            break;
        case 0x0a:
        case 0x0b:
            if (g2)
                mask = 0x00;
            break;
        case 0x08:
        case 0x13:
        case 0x14:
        case 0x18:
            if (g3)
                mask = 0x00;
            break;
        case 0x10:
        case 0x12:
        case 0x15:
        case 0x16:
            if (g4)
                mask = 0x00;
            break;
        case 0x11:
            if (g3)
                mask &= ~0x30;
            if (g4)
                mask &= ~0x4f;
            break;
        case 0x17:
            if (g3)
                mask &= ~0xfb;
            if (g4)
                mask &= ~0x04;
            break;
        default:
            break;
    }

    return mask;
}

static void
chips_45x_out(uint16_t addr, uint8_t val, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;
    svga_t      *svga  = &chips->svga;
    uint8_t      old;
    uint8_t      wmask;

    if (!chips_45x_vga_enabled(chips))
        return;

    if (chips_45x_xr_port(chips, addr)) {
        if (addr & 1)
            chips_45x_xr_write(chips, val);
        else
            chips->xr_index = val & 0x7f;
        return;
    }

    if ((addr >= 0x3b6) && (addr <= 0x3d7) && ((addr & 0xfffe) == 0x3b6 || (addr & 0xfffe) == 0x3d6))
        return;

    switch (addr) {
        case 0x3b8:
            chips->herc_mode = val;
            if (chips_45x_emu_mode(chips) == EMU_HERC)
                chips_45x_emu_apply(chips);
            return;
        case 0x3bf:
            chips->herc_config = val & 0x03;
            if (chips_45x_emu_mode(chips) == EMU_HERC)
                chips_45x_emu_apply(chips);
            return;
        case 0x3d8:
            chips->cga_mode = val;
            if (chips_45x_emu_mode(chips) == EMU_CGA)
                chips_45x_emu_apply(chips);
            return;
        case 0x3d9:
            chips->xr[0x7e] = val & 0x3f;
            if (chips_45x_emu_mode(chips) == EMU_CGA)
                chips_45x_emu_apply(chips);
            return;
        default:
            break;
    }

    if ((((addr & 0xfff0) == 0x3d0) || ((addr & 0xfff0) == 0x3b0)) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c0:
        case 0x3c1:
            if (chips->xr[0x15] & 0x01)
                return;
            break;

        case 0x3c2:
            if (chips->xr[0x15] & 0x10)
                return;
            svga_out(addr, val, svga);
            svga_recalctimings(svga);
            return;

        case 0x3c5:
            if (chips->xr[0x15] & 0x01)
                return;
            svga_out(addr, val, svga);
            if (svga->seqaddr == 4)
                chips_45x_update_mapping(chips);
            return;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            if (chips->xr[0x15] & 0x20)
                return;
            break;

        case 0x3cf:
            if (chips->xr[0x15] & 0x01)
                return;
            svga_out(addr, val, svga);
            if ((svga->gdcaddr & 0x0f) == 6)
                chips_45x_update_mapping(chips);
            return;

        case 0x3da:
            if (chips->xr[0x15] & 0x10)
                return;
            svga->fcr = val;
            svga_recalctimings(svga);
            return;

        case 0x3d4:
            svga->crtcreg = val & 0x3f;
            return;

        case 0x3d5:
            if (svga->crtcreg & 0x20)
                return;
            old                       = svga->crtc[svga->crtcreg];
            wmask                     = chips_45x_crtc_wmask(chips, svga->crtcreg);
            svga->crtc[svga->crtcreg] = (old & ~wmask) | (val & wmask);
            val                       = svga->crtc[svga->crtcreg];
            if (old != val) {
                if ((svga->crtcreg < 0x0e) || (svga->crtcreg > 0x10)) {
                    if (((svga->crtcreg == 0x0c) || (svga->crtcreg == 0x0d)) && !(svga->adv_flags & FLAG_NO_SHIFT3)) {
                        svga->fullchange    = 3;
                        svga->memaddr_latch = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            return;

        default:
            break;
    }

    svga_out(addr, val, svga);
}

static uint8_t
chips_45x_in(uint16_t addr, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;
    svga_t      *svga  = &chips->svga;
    uint8_t      ret;
    uint8_t      sw;

    if (!chips_45x_vga_enabled(chips))
        return 0xff;

    if (chips_45x_xr_port(chips, addr)) {
        if (addr & 1)
            return chips_45x_xr_read(chips);
        return chips->xr_index;
    }

    if ((addr >= 0x3b6) && (addr <= 0x3d7) && ((addr & 0xfffe) == 0x3b6 || (addr & 0xfffe) == 0x3d6))
        return 0xff;

    if ((((addr & 0xfff0) == 0x3d0) || ((addr & 0xfff0) == 0x3b0)) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c2:
            ret = svga_in(addr, svga);
            if (chips->xr[0x1f] & 0x80) {
                /* Virtual switch readback in place of the SENSE pin. */
                sw  = 3 - ((svga->miscout >> 2) & 3);
                ret = (ret & ~0x10) | (((chips->xr[0x1f] >> sw) & 1) << 4);
            }
            break;

        case 0x3da:
            ret = svga_in(addr, svga);
            /* XR14 bit 6: vertical sync status on bit 7, as on an MDA/Hercules. */
            if (chips->xr[0x14] & 0x40)
                ret = (ret & 0x7f) | ((ret & 0x08) ? 0x00 : 0x80);
            break;

        case 0x3d4:
            ret = svga->crtcreg;
            break;

        case 0x3d5:
            if (svga->crtcreg & 0x20)
                ret = 0xff;
            else
                ret = svga->crtc[svga->crtcreg];
            break;

        default:
            ret = svga_in(addr, svga);
            break;
    }

    return ret;
}

static void
chips_45x_dac_out(uint16_t addr, uint8_t val, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    if (chips->xr[0x02] & 0x40)
        chips_45x_out((addr & 0x000f) | 0x03c0, val, priv);
}

static uint8_t
chips_45x_dac_in(uint16_t addr, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    if (chips->xr[0x02] & 0x40)
        return chips_45x_in((addr & 0x000f) | 0x03c0, priv);

    return 0xff;
}

static uint8_t
chips_45x_setup_in(uint16_t addr, void *priv)
{
    const chips_45x_t *chips = (chips_45x_t *) priv;
    uint8_t            ret   = 0xff;

    if (!(chips->setup_ctrl & 0x10))
        return ret;

    switch (addr) {
        case 0x100:
        case 0x101:
            ret = 0xff;
            break;
        case 0x102:
            ret = chips->global_enable;
            break;
        case 0x103:
            ret = chips->ext_enable;
            break;
        case 0x104:
            ret = 0xa5;
            break;
        default:
            break;
    }

    return ret;
}

static void
chips_45x_setup_out(uint16_t addr, uint8_t val, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    if (!(chips->setup_ctrl & 0x10))
        return;

    switch (addr) {
        case 0x102:
            chips->global_enable = val & 0x01;
            chips_45x_update_enable(chips);
            break;
        case 0x103:
            chips->ext_enable = val & 0xdf;
            break;
        default:
            break;
    }
}

static void
chips_45x_46e8_out(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    /* XR70 bit 7 makes 46E8h writes ineffective on the 82C450. */
    if ((chips->type == CHIPS_82C450) && (chips->xr[0x70] & 0x80))
        return;

    chips->setup_ctrl = val & 0x18;
    chips_45x_update_enable(chips);
}

static double
chips_45x_getclock(const chips_45x_t *chips)
{
    const svga_t *svga = &chips->svga;
    double        freq;

    switch ((svga->miscout >> 2) & 3) {
        case 0:
            freq = 25175000.0;
            break;
        case 1:
            freq = 28322000.0;
            break;
        case 2:
            if (chips->type == CHIPS_82C451)
                freq = 40000000.0;
            else {
                /* External clock selected by the Feature Control outputs. */
                switch (svga->fcr & 3) {
                    default:
                    case 0:
                        freq = 40000000.0;
                        break;
                    case 1:
                        freq = 50350000.0;
                        break;
                    case 2:
                        freq = 65000000.0;
                        break;
                    case 3:
                        freq = 44900000.0;
                        break;
                }
            }
            break;
        default:
            if (chips->type == CHIPS_82C452)
                freq = 40000000.0;
            else if (chips->type == CHIPS_82C451)
                freq = 28322000.0;
            else
                freq = 44900000.0;
            break;
    }

    if (chips->type == CHIPS_82C452) {
        const uint8_t xr05 = chips->xr[0x05];
        const int     clk  = (svga->miscout >> 2) & 3;

        if (xr05 & 0x01)
            freq = 40000000.0;
        /* CLK2 is always divided, CLK0/CLK1 only when their bit is set. */
        if ((clk == 2) || ((clk == 0) && (xr05 & 0x40)) || ((clk == 1) && (xr05 & 0x20)))
            freq /= (double) (((xr05 >> 3) & 3) + 1);
    }

    return freq;
}

/*
   Emulated CGA 640x200 and Hercules graphics: the even/odd byte pair (planes 0
   and 1) at each display address is shifted out as 16 one-bit pixels.
*/
static void
chips_45x_render_1bpp(svga_t *svga)
{
    const chips_45x_t *chips = (chips_45x_t *) svga->priv;
    uint32_t          *p;
    uint32_t           addr;
    uint32_t           fg;
    uint32_t           bg;
    uint8_t            dat;

    if (((svga->displine + svga->y_add) < 0) ||
        (svga->monitor->target_buffer == NULL) ||
        (svga->monitor->target_buffer->line[svga->displine + svga->y_add] == NULL))
        return;

    addr = svga->remap_func(svga, svga->memaddr) & svga->vram_display_mask;
    if (!(svga->changedvram[addr >> 12] || svga->changedvram[(addr >> 12) + 1] || svga->fullchange))
        return;

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    p  = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];
    bg = svga->pallook[svga->egapal[0] & svga->dac_mask];
    /* Hercules pixels use the normal-intensity text color. */
    fg = svga->pallook[svga->egapal[(chips_45x_emu_mode(chips) == EMU_HERC) ? 7 : 1] & svga->dac_mask];

    for (int x = 0; x < svga->hdisp; x += 16) {
        addr = svga->remap_func(svga, svga->memaddr) & svga->vram_display_mask;

        for (int b = 0; b < 2; b++) {
            dat = svga->vram[addr | b];
            for (int i = 0; i < 8; i++)
                *p++ = (dat & (0x80 >> i)) ? fg : bg;
        }

        svga->memaddr += 4;
    }
}

static void
chips_45x_recalctimings(svga_t *svga)
{
    const chips_45x_t *chips = (chips_45x_t *) svga->priv;
    const int          emu   = chips_45x_emu_mode(chips);
    int                graphics;
    int                alt;

    svga->clock = (cpuclock * (double) (1ULL << 32)) / chips_45x_getclock(chips);

    if (chips->type == CHIPS_82C452)
        svga->memaddr_latch |= (chips->xr[0x0c] & 0x03) << 16;
    else if (chips->type == CHIPS_82C450)
        svga->memaddr_latch |= (chips->xr[0x0c] & 0x01) << 16;

    graphics = (svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1);

    if (((emu == EMU_CGA) && graphics && (chips->cga_mode & 0x10)) || ((emu == EMU_HERC) && graphics)) {
        if (emu == EMU_CGA)
            svga->dots_per_clock = 16;
        svga->render = chips_45x_render_1bpp;
    }

    /* Hercules page 1 starts 32K into the emulated buffer. */
    if ((emu == EMU_HERC) && (chips->herc_mode & 0x80) && (chips->herc_config & 0x02))
        svga->memaddr_latch += 0x4000;

    /* The alternate horizontal registers replace the CRTC ones in the emulated
       low resolution CGA modes and in Hercules graphics. */
    alt = ((emu == EMU_CGA) && ((svga->seqregs[1] & 0x08) || (graphics && (chips->cga_mode & 0x10)))) ||
          ((emu == EMU_HERC) && graphics);
    if (alt) {
        svga->hdisp          = (chips->xr[0x18] + 1) * svga->dots_per_clock;
        svga->hdisp_time     = chips->xr[0x18] + 1;
        svga->htotal         = chips->xr[0x1b] + 5;
        svga->hblankstart    = chips->xr[0x1c];
        svga->hblank_end_val = (chips->xr[0x1d] & 0x1f) | ((chips->xr[0x1a] & 0x80) ? 0x20 : 0x00);

        /* XR0D bit 1 adds a half step to the alternate offset (e.g. the 45-word
           Hercules row), so advance rows in display memory units directly. */
        svga->adv_flags |= FLAG_NO_SHIFT3;
        svga->rowoffset = ((chips->xr[0x1e] << 1) | ((chips->xr[0x0d] >> 1) & 1)) << 2;
        svga->memaddr_latch <<= 2;
    } else
        svga->adv_flags &= ~FLAG_NO_SHIFT3;

    svga->interlace = graphics && (chips->xr[0x28] & 0x20);

    /*
       The extended 256-color modes keep the VGA timing of two dot clocks per
       pixel but run a byte-mode CRTC over packed memory; render them at their
       real width.
    */
    if (svga->packed_chain4 && (svga->render == svga_render_8bpp_lowres)) {
        svga->render = svga_render_8bpp_highres;
        svga->hdisp >>= 1;
        svga->dots_per_clock >>= 1;
    }
}

static void *
chips_45x_init(const device_t *info)
{
    chips_45x_t *chips = calloc(1, sizeof(chips_45x_t));
    const char  *romfn;

    chips->type = info->local;

    switch (chips->type) {
        default:
        case CHIPS_82C450:
            romfn            = BIOS_82C450_PATH;
            chips->vram_size = device_get_config_int("memory");
            break;
        case CHIPS_82C451:
            romfn            = BIOS_82C451_PATH;
            chips->vram_size = 256;
            break;
        case CHIPS_82C452:
            romfn            = BIOS_82C452_PATH;
            chips->vram_size = device_get_config_int("memory");
            break;
    }

    rom_init(&chips->bios_rom, romfn, 0xc0000, 0x8000, 0x7fff, 0x0000, MEM_MAPPING_EXTERNAL);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_chips_45x);

    svga_init(info, &chips->svga, chips, chips->vram_size << 10,
              chips_45x_recalctimings,
              chips_45x_in, chips_45x_out,
              NULL,
              NULL);

    mem_mapping_set_handler(&chips->svga.mapping,
                            chips_45x_read, chips_45x_readw, chips_45x_readl,
                            chips_45x_write, chips_45x_writew, chips_45x_writel);
    mem_mapping_set_p(&chips->svga.mapping, chips);

    io_sethandler(0x03b0, 0x0030, chips_45x_in, NULL, NULL, chips_45x_out, NULL, NULL, chips);
    io_sethandler(0x0100, 0x0005, chips_45x_setup_in, NULL, NULL, chips_45x_setup_out, NULL, NULL, chips);
    io_sethandler(0x46e8, 0x0001, NULL, NULL, NULL, chips_45x_46e8_out, NULL, NULL, chips);
    if (chips->type != CHIPS_82C450)
        io_sethandler(0x83c6, 0x0004, chips_45x_dac_in, NULL, NULL, chips_45x_dac_out, NULL, NULL, chips);

    chips->setup_ctrl    = 0x08;
    chips->global_enable = 0x01;

    chips->xr[0x06] = 0x4a;
    chips->svga.vram_display_mask = MIN(chips->svga.vram_mask, 0x3ffff);

    chips->svga.miscout = 1;

    return chips;
}

static int
chips_82c450_available(void)
{
    return rom_present(BIOS_82C450_PATH);
}

static int
chips_82c451_available(void)
{
    return rom_present(BIOS_82C451_PATH);
}

static int
chips_82c452_available(void)
{
    return rom_present(BIOS_82C452_PATH);
}

static void
chips_45x_close(void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    svga_close(&chips->svga);

    free(chips);
}

static void
chips_45x_speed_changed(void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    svga_recalctimings(&chips->svga);
}

static void
chips_45x_force_redraw(void *priv)
{
    chips_45x_t *chips = (chips_45x_t *) priv;

    chips->svga.fullchange = changeframecount;
}

// clang-format off
static const device_config_t chips_82c450_config[] = {
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

static const device_config_t chips_82c452_config[] = {
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
// clang-format on

const device_t chips_82c450_device = {
    .name          = "Chips & Technologies 82C450",
    .internal_name = "chips_82c450",
    .flags         = DEVICE_ISA,
    .local         = CHIPS_82C450,
    .init          = chips_45x_init,
    .close         = chips_45x_close,
    .reset         = NULL,
    .available     = chips_82c450_available,
    .speed_changed = chips_45x_speed_changed,
    .force_redraw  = chips_45x_force_redraw,
    .config        = chips_82c450_config
};

const device_t chips_82c451_device = {
    .name          = "Chips & Technologies 82C451",
    .internal_name = "chips_82c451",
    .flags         = DEVICE_ISA,
    .local         = CHIPS_82C451,
    .init          = chips_45x_init,
    .close         = chips_45x_close,
    .reset         = NULL,
    .available     = chips_82c451_available,
    .speed_changed = chips_45x_speed_changed,
    .force_redraw  = chips_45x_force_redraw,
    .config        = NULL
};

const device_t chips_82c452_device = {
    .name          = "Chips & Technologies 82C452",
    .internal_name = "chips_82c452",
    .flags         = DEVICE_ISA,
    .local         = CHIPS_82C452,
    .init          = chips_45x_init,
    .close         = chips_45x_close,
    .reset         = NULL,
    .available     = chips_82c452_available,
    .speed_changed = chips_45x_speed_changed,
    .force_redraw  = chips_45x_force_redraw,
    .config        = chips_82c452_config
};
