/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          C&T 69000 emulation.
 *
 *
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2023-2024 Cacodemon345
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <time.h>
#include <stdatomic.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/pci.h>
#include <86box/thread.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/plat_unused.h>
#include <86box/bswap.h>
#include <assert.h>

#pragma pack(push, 1)
typedef struct chips_69000_bitblt_t
{
    /* BR00 - Source and Destination Span Register. */
    uint16_t source_span;
    uint16_t destination_span;

    /* BR01 - Pattern/Source Expansion Background Color & Transparency Key Register. */
    uint32_t pattern_source_key_bg;

    /* BR02 - Pattern/Source Expansion Foreground Color Register. */
    uint32_t pattern_source_key_fg;

    /* BR03 - Monochrome Source Control Register. */
    uint8_t monochrome_source_left_clip;
    uint8_t monochrome_source_right_clip;
    uint8_t monochrome_source_initial_discard;
    uint8_t monochrome_source_alignment : 3;
    uint8_t monochrome_source_expansion_color_reg_select : 1;
    uint8_t dummy_8 : 4;

    /* BR04 - BitBLT Control Register. */
    uint32_t bitblt_control;

    /* BR05 - Pattern Address Register. */
    uint32_t pat_addr;

    /* BR06 - Source Address Register. */
    uint32_t source_addr;

    /* BR07 - Destination Address Register. */
    uint32_t destination_addr;

    /* BR08 - Destination Width & Height Register. */
    uint16_t destination_width;
    uint16_t destination_height;

    /* BR09 - Source Expansion Background Color & Transparency Key Register. */
    uint32_t source_key_bg;

    /* BR0A - Source Expansion Foreground Color Register. */
    uint32_t source_key_fg;
} chips_69000_bitblt_t;
#pragma pack(pop)

typedef struct chips_69000_t {
    svga_t        svga;
    uint8_t       pci_conf_status;
    uint8_t       slot, irq_state;
    uint8_t       pci_line_interrupt;
    uint8_t       pci_rom_enable;
    uint8_t       read_write_bank;
    bool          engine_active;
    bool          quit;
    thread_t     *accel_thread;
    event_t      *fifo_event, *fifo_data_event;
    pc_timer_t    decrement_timer;
    uint16_t      rom_addr;
    mem_mapping_t linear_mapping;
    uint8_t       on_board;

    rgb_t cursor_palette[8];
    uint32_t cursor_pallook[8];

    uint8_t mm_regs[256], mm_index;
    uint8_t flat_panel_regs[256], flat_panel_index;
    uint8_t ext_regs[256], ext_index;

    union {
        uint32_t mem_regs[4];
        uint16_t mem_regs_w[4 * 2];
        uint8_t mem_regs_b[4 * 4];
    };
    union {
        uint32_t bitblt_regs[11];
        uint16_t bitblt_regs_w[11 * 2];
        uint8_t bitblt_regs_b[11 * 4];
        struct chips_69000_bitblt_t bitblt;
    };

    struct
    {
        struct chips_69000_bitblt_t bitblt;

        uint32_t actual_source_height;
        uint32_t actual_destination_height;

        uint32_t actual_destination_width;

        uint32_t count_x, count_y;
        int x, y;
        int x_dir, y_dir;

        uint8_t bytes_per_pixel;

        /* Byte counter for BitBLT port writes. */
        uint8_t bytes_written;
        uint8_t bytes_skip;
        uint32_t mono_bytes_pitch;
        uint8_t mono_bits_skip_left;
        uint32_t bytes_counter;
        uint32_t bytes_in_line_written;
        uint8_t bytes_port[256];
    } bitblt_running;

    union {
        uint16_t subsys_vid;
        uint8_t subsys_vid_b[2];
    };

    union {
        uint16_t subsys_pid;
        uint8_t subsys_pid_b[2];
    };

    rom_t bios_rom;

    void *i2c, *ddc;

    uint8_t st01;
} chips_69000_t;

static chips_69000_t *reset_state = NULL;

/* TODO: Probe timings on real hardware. */
static video_timings_t timing_chips = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 10, .read_w = 10, .read_l = 10 };

uint8_t chips_69000_readb_linear(uint32_t addr, void *priv);
uint16_t chips_69000_readw_linear(uint32_t addr, void *priv);
uint32_t chips_69000_readl_linear(uint32_t addr, void *priv);
void chips_69000_writeb_linear(uint32_t addr, uint8_t val, void *priv);
void chips_69000_writew_linear(uint32_t addr, uint16_t val, void *priv);
void chips_69000_writel_linear(uint32_t addr, uint32_t val, void *priv);

/* Multimedia handling. */
uint8_t
chips_69000_read_multimedia(chips_69000_t* chips)
{
    switch (chips->mm_index) {
        case 0:
            /* Report no playback/capture capability. */
            return 0;
        default:
            return chips->mm_regs[chips->mm_index];
    }
    return chips->mm_regs[chips->mm_index];
}

/* Multimedia (write) handling. */
void
chips_69000_write_multimedia(chips_69000_t* chips, uint8_t val)
{
    switch (chips->mm_index) {
        case 0:
            return;
        default:
            chips->mm_regs[chips->mm_index] = val;
            break;
    }
    chips->mm_regs[chips->mm_index] = val;
}

/* Flat panel handling. */
uint8_t
chips_69000_read_flat_panel(chips_69000_t* chips)
{
    switch (chips->flat_panel_index) {
        case 0:
            return 1;
        default:
            return chips->flat_panel_regs[chips->flat_panel_index];
    }
    return chips->flat_panel_regs[chips->flat_panel_index];
}

/* Flat panel (write) handling. */
void
chips_69000_write_flat_panel(chips_69000_t* chips, uint8_t val)
{
    switch (chips->flat_panel_index) {
        case 0:
            return;
        case 1:
        case 0x20 ... 0x33:
        case 0x35:
        case 0x36:
            chips->flat_panel_regs[chips->flat_panel_index] = val;
            svga_recalctimings(&chips->svga);
            return;
        default:
            chips->flat_panel_regs[chips->flat_panel_index] = val;
            break;
    }
    chips->flat_panel_regs[chips->flat_panel_index] = val;
}

void
chips_69000_interrupt(chips_69000_t* chips)
{
    pci_irq(chips->slot, PCI_INTA, 0, !!((chips->mem_regs[0] & chips->mem_regs[1]) & 0x80004040), &chips->irq_state);
}

void
chips_69000_bitblt_interrupt(chips_69000_t* chips)
{
    chips->engine_active = 0;
    chips->mem_regs[1] |= 1 << 31;

    chips_69000_interrupt(chips);
}

void
chips_69000_do_rop_8bpp(uint8_t *dst, uint8_t src, uint8_t rop)
{
    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x11:
            *dst = ~(*dst) & ~src;
            break;
        case 0x22:
            *dst &= ~src;
            break;
        case 0x33:
            *dst = ~src;
            break;
        case 0x44:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x66:
            *dst ^= src;
            break;
        case 0x77:
            *dst = ~src | ~(*dst);
            break;
        case 0x88:
            *dst &= src;
            break;
        case 0x99:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xBB:
            *dst |= ~src;
            break;
        case 0xCC:
            *dst = src;
            break;
        case 0xDD:
            *dst = src | ~(*dst);
            break;
        case 0xEE:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
}

void
chips_69000_do_rop_16bpp(uint16_t *dst, uint16_t src, uint8_t rop)
{
    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x11:
            *dst = ~(*dst) & ~src;
            break;
        case 0x22:
            *dst &= ~src;
            break;
        case 0x33:
            *dst = ~src;
            break;
        case 0x44:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x66:
            *dst ^= src;
            break;
        case 0x77:
            *dst = ~src | ~(*dst);
            break;
        case 0x88:
            *dst &= src;
            break;
        case 0x99:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xBB:
            *dst |= ~src;
            break;
        case 0xCC:
            *dst = src;
            break;
        case 0xDD:
            *dst = src | ~(*dst);
            break;
        case 0xEE:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFFFF;
            break;
    }
}

void
chips_69000_do_rop_24bpp(uint32_t *dst, uint32_t src, uint8_t rop)
{
    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x11:
            *dst = ~(*dst) & ~src;
            break;
        case 0x22:
            *dst &= ~src;
            break;
        case 0x33:
            *dst = ~src;
            break;
        case 0x44:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x66:
            *dst ^= src;
            break;
        case 0x77:
            *dst = ~src | ~(*dst);
            break;
        case 0x88:
            *dst &= src;
            break;
        case 0x99:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xBB:
            *dst |= ~src;
            break;
        case 0xCC:
            *dst = src;
            break;
        case 0xDD:
            *dst = src | ~(*dst);
            break;
        case 0xEE:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFFFFFF;
            break;
    }
}

void
chips_69000_do_rop_8bpp_patterned(uint8_t *dst, uint8_t pattern, uint8_t src, uint8_t rop)
{
    if ((rop & 0xF) == ((rop >> 4) & 0xF)) {
        return chips_69000_do_rop_8bpp(dst, src, rop);
    }

    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x05:
            *dst = ~(*dst) & ~pattern;
            break;
        case 0x0A:
            *dst &= ~pattern;
            break;
        case 0x0F:
            *dst = ~pattern;
            break;
        case 0x1A:
            *dst = pattern ^ (*dst | (pattern & src));
            break;
        case 0x2A:
            *dst = *dst & (~(src & pattern));
            break;
        case 0x3A:
            *dst = src ^ (pattern | (*dst ^ src));
            break;
        case 0x4A:
            *dst = *dst ^ (pattern & (src | *dst));
            break;
        case 0x50:
            *dst = pattern & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x5A:
            *dst ^= pattern;
            break;
        case 0x5F:
            *dst = ~pattern | ~(*dst);
            break;
        case 0x6A:
            *dst = *dst ^ (pattern & src);
            break;
        case 0x7A:
            *dst = *dst ^ (pattern & (src | (~*dst)));
            break;
        case 0x8A:
            *dst = *dst & (src | (~pattern));
            break;
        case 0x9A:
            *dst = *dst ^ (pattern & (~src));
            break;
        case 0xB8:
            *dst = (((pattern ^ *dst) & src) ^ pattern);
            break;
        case 0xA0:
            *dst &= pattern;
            break;
        case 0xA5:
            *dst ^= ~pattern;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xAC:
            *dst = src ^ (pattern & (*dst ^ src));
            break;
        case 0xAF:
            *dst |= ~pattern;
            break;
        case 0xBA:
            *dst |= (pattern & ~src);
            break;
        case 0xCA:
            *dst ^= (pattern & (src ^ *dst));
            break;
        case 0xE2:
            *dst ^= (src & (pattern ^ *dst));
            break;
        case 0xDA:
            *dst ^= pattern & (~(src & *dst));
            break;
        case 0xEA:
            *dst |= pattern & src;
            break;
        case 0xF0:
            *dst = pattern;
            break;
        case 0xF5:
            *dst = pattern | ~(*dst);
            break;
        case 0xFA:
            *dst |= pattern;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
        default:
            pclog("Unknown ROP 0x%X\n", rop);
            break;
    }
}

void
chips_69000_do_rop_16bpp_patterned(uint16_t *dst, uint16_t pattern, uint16_t src, uint8_t rop)
{
    if ((rop & 0xF) == ((rop >> 4) & 0xF)) {
        return chips_69000_do_rop_16bpp(dst, src, rop);
    }

    switch (rop) {
        default:
            pclog("Unknown ROP 0x%X\n", rop);
            break;
        case 0x00:
            *dst = 0;
            break;
        case 0x05:
            *dst = ~(*dst) & ~pattern;
            break;
        case 0x0A:
            *dst &= ~pattern;
            break;
        case 0x0F:
            *dst = ~pattern;
            break;
        case 0x1A:
            *dst = pattern ^ (*dst | (pattern & src));
            break;
        case 0x2A:
            *dst = *dst & (~(src & pattern));
            break;
        case 0x3A:
            *dst = src ^ (pattern | (*dst ^ src));
            break;
        case 0x4A:
            *dst = *dst ^ (pattern & (src | *dst));
            break;
        case 0x50:
            *dst = pattern & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x5A:
            *dst ^= pattern;
            break;
        case 0x5F:
            *dst = ~pattern | ~(*dst);
            break;
        case 0x6A:
            *dst = *dst ^ (pattern & src);
            break;
        case 0x7A:
            *dst = *dst ^ (pattern & (src | (~*dst)));
            break;
        case 0x8A:
            *dst = *dst & (src | (~pattern));
            break;
        case 0x9A:
            *dst = *dst ^ (pattern & (~src));
            break;
        case 0xB8:
            *dst = (((pattern ^ *dst) & src) ^ pattern);
            break;
        case 0xA0:
            *dst &= pattern;
            break;
        case 0xA5:
            *dst ^= ~pattern;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xAC:
            *dst = src ^ (pattern & (*dst ^ src));
            break;
        case 0xAF:
            *dst |= ~pattern;
            break;
        case 0xBA:
            *dst |= (pattern & ~src);
            break;
        case 0xCA:
            *dst ^= (pattern & (src ^ *dst));
            break;
        case 0xE2:
            *dst ^= (src & (pattern ^ *dst));
            break;
        case 0xDA:
            *dst ^= pattern & (~(src & *dst));
            break;
        case 0xEA:
            *dst |= pattern & src;
            break;
        case 0xF0:
            *dst = pattern;
            break;
        case 0xF5:
            *dst = pattern | ~(*dst);
            break;
        case 0xFA:
            *dst |= pattern;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
}

void
chips_69000_do_rop_24bpp_patterned(uint32_t *dst, uint32_t pattern, uint32_t src, uint8_t rop)
{
    uint32_t orig_dst = *dst & 0xFF000000;

    if ((rop & 0xF) == ((rop >> 4) & 0xF)) {
        return chips_69000_do_rop_24bpp(dst, src, rop);
    }

    switch (rop) {
        default:
            pclog("Unknown ROP 0x%X\n", rop);
            break;
        case 0x00:
            *dst = 0;
            break;
        case 0x05:
            *dst = ~(*dst) & ~pattern;
            break;
        case 0x0A:
            *dst &= ~pattern;
            break;
        case 0x0F:
            *dst = ~pattern;
            break;
        case 0x1A:
            *dst = pattern ^ (*dst | (pattern & src));
            break;
        case 0x2A:
            *dst = *dst & (~(src & pattern));
            break;
        case 0x3A:
            *dst = src ^ (pattern | (*dst ^ src));
            break;
        case 0x4A:
            *dst = *dst ^ (pattern & (src | *dst));
            break;
        case 0x50:
            *dst = pattern & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x5A:
            *dst ^= pattern;
            break;
        case 0x5F:
            *dst = ~pattern | ~(*dst);
            break;
        case 0x6A:
            *dst = *dst ^ (pattern & src);
            break;
        case 0x7A:
            *dst = *dst ^ (pattern & (src | (~*dst)));
            break;
        case 0x8A:
            *dst = *dst & (src | (~pattern));
            break;
        case 0x9A:
            *dst = *dst ^ (pattern & (~src));
            break;
        case 0xB8:
            *dst = (((pattern ^ *dst) & src) ^ pattern);
            break;
        case 0xA0:
            *dst &= pattern;
            break;
        case 0xA5:
            *dst ^= ~pattern;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xAC:
            *dst = src ^ (pattern & (*dst ^ src));
            break;
        case 0xAF:
            *dst |= ~pattern;
            break;
        case 0xBA:
            *dst |= (pattern & ~src);
            break;
        case 0xCA:
            *dst ^= (pattern & (src ^ *dst));
            break;
        case 0xDA:
            *dst ^= pattern & (~(src & *dst));
            break;
        case 0xE2:
            *dst ^= (src & (pattern ^ *dst));
            break;
        case 0xEA:
            *dst |= pattern & src;
            break;
        case 0xF0:
            *dst = pattern;
            break;
        case 0xF5:
            *dst = pattern | ~(*dst);
            break;
        case 0xFA:
            *dst |= pattern;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
    *dst &= 0xFFFFFF;
    *dst |= orig_dst;
}

void
chips_69000_recalctimings(svga_t *svga)
{
    chips_69000_t *chips = (chips_69000_t *) svga->priv;

    svga->clock = (cpuclock * (double) (1ULL << 32)) / svga->getclock((svga->miscout >> 2) & 3, svga->priv);

    if (chips->ext_regs[0x81] & 0x10) {
        svga->htotal -= 5;
    }

    if (((chips->ext_regs[0x61] & 0x8) && !(chips->ext_regs[0x61] & 0x4))
        || ((chips->ext_regs[0x61] & 0x2) && !(chips->ext_regs[0x61] & 0x1))) {
        svga->dpms = 1;
    } else
        svga->dpms = 0;

    if (chips->ext_regs[0x09] & 0x1) {
        svga->vtotal -= 2;
        svga->vtotal &= 0xFF;
        svga->vtotal |= (svga->crtc[0x30] & 0xF) << 8;
        svga->vtotal += 2;

        svga->dispend--;
        svga->dispend &= 0xFF;
        svga->dispend |= (svga->crtc[0x31] & 0xF) << 8;
        svga->dispend++;

        svga->vsyncstart--;
        svga->vsyncstart &= 0xFF;
        svga->vsyncstart |= (svga->crtc[0x32] & 0xF) << 8;
        svga->vsyncstart++;

        svga->vblankstart--;
        svga->vblankstart &= 0xFF;
        svga->vblankstart |= (svga->crtc[0x33] & 0xF) << 8;
        svga->vblankstart++;

        if (!(chips->ext_regs[0x81] & 0x10))
            svga->htotal -= 5;

        svga->htotal |= (svga->crtc[0x38] & 0x1) << 8;

        if (!(chips->ext_regs[0x81] & 0x10))
            svga->htotal += 5;

        svga->hblank_end_val = ((svga->crtc[3] & 0x1f) | ((svga->crtc[5] & 0x80) ? 0x20 : 0x00)) | (svga->crtc[0x3c] & 0b11000000);
        svga->hblank_end_mask = 0xff;

        svga->ma_latch |= (svga->crtc[0x40] & 0xF) << 16;
        svga->rowoffset |= (svga->crtc[0x41] & 0xF) << 8;

        svga->interlace = !!(svga->crtc[0x70] & 0x80);

        if (svga->hdisp == 1280 && svga->dispend == 1024) {
            svga->interlace = 0;
        }

        switch (chips->ext_regs[0x81] & 0xF) {
            default:
                svga->bpp = 8;
                break;
            case 0b0010:
                svga->bpp = 8;
                svga->render = svga_render_8bpp_highres;
                break;

            case 0b0100:
                svga->bpp = 15;
                svga->render = svga_render_15bpp_highres;
                break;
            case 0b0101:
                svga->bpp = 16;
                svga->render = svga_render_16bpp_highres;
                break;
            case 0b0110:
                svga->bpp = 24;
                svga->render = svga_render_24bpp_highres;
                break;
            case 0b0111:
                svga->bpp = 32;
                svga->render = svga_render_32bpp_highres;
                break;
        }
#if 1
        if (chips->flat_panel_regs[0x01] & 0x2) {
            /* TODO: Fix horizontal parameter calculations. */
            if (svga->hdisp > (((chips->flat_panel_regs[0x20] | ((chips->flat_panel_regs[0x25] & 0xF) << 8)) + 1) << 3)) {
                svga->hdisp = ((chips->flat_panel_regs[0x20] | ((chips->flat_panel_regs[0x25] & 0xF) << 8)) + 1) << 3;
                //svga->htotal = ((chips->flat_panel_regs[0x23] | ((chips->flat_panel_regs[0x26] & 0xF) << 8)) + 5) << 3;
                //svga->hblank_end_val = svga->htotal - 1;
                svga->hoverride = 1;
            } else
                svga->hoverride = 0;

            if (svga->dispend > (((chips->flat_panel_regs[0x30] | ((chips->flat_panel_regs[0x35] & 0xF) << 8)) + 1))) {
                svga->dispend = svga->vsyncstart = svga->vblankstart = ((chips->flat_panel_regs[0x30] | ((chips->flat_panel_regs[0x35] & 0xF) << 8)) + 1);
            }
            //svga->hdisp = ((chips->flat_panel_regs[0x20] | ((chips->flat_panel_regs[0x25] & 0xF) << 8)) + 1) << 3;
            //svga->htotal = ((chips->flat_panel_regs[0x23] | ((chips->flat_panel_regs[0x26] & 0xF) << 8)) + 5) << 3;
            //svga->hblank_end_val = svga->htotal - 1;
            //svga->dispend = svga->vsyncstart = svga->vblankstart = ((chips->flat_panel_regs[0x30] | ((chips->flat_panel_regs[0x35] & 0xF) << 8)) + 1);
            //svga->vsyncstart = ((chips->flat_panel_regs[0x31] | ((chips->flat_panel_regs[0x35] & 0xF0) << 4)) + 1);
            //svga->vtotal = ((chips->flat_panel_regs[0x33] | ((chips->flat_panel_regs[0x36] & 0xF) << 8)) + 2);
            svga->clock = (cpuclock * (double) (1ULL << 32)) / svga->getclock((chips->flat_panel_regs[0x03] >> 2) & 3, svga->priv);
        } else {
            svga->hoverride = 0;
        }
#endif
    } else {
        svga->bpp = 8;
        svga->hoverride = 0;
    }
}

void
chips_69000_decrement_timer(void* p)
{
    chips_69000_t *chips = (chips_69000_t*)p;

    chips->ext_regs[0xD2]--;
    timer_on_auto(&chips->decrement_timer, 1000000. / 2000.);
}

void
chips_69000_recalc_banking(chips_69000_t *chips)
{
    svga_t* svga = &chips->svga;
    chips->svga.read_bank = chips->svga.write_bank = 0;

    svga->chain2_write = !(svga->seqregs[0x4] & 4);
    svga->chain4       = (svga->seqregs[0x4] & 8) || (chips->ext_regs[0xA] & 0x4);
    svga->fast         = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) && ((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only) && !(svga->adv_flags & FLAG_ADDR_BY8);

    if (chips->ext_regs[0xA] & 1) {
        chips->svga.read_bank = chips->svga.write_bank = 0x10000 * (chips->ext_regs[0xE] & 0x7f);
    }

    /*if (chips->ext_regs[0x40] & 2) {
        svga->decode_mask = (1 << 18) - 1;
    } else {
        svga->decode_mask = (1 << 21) - 1;
    }*/
}

void
chips_69000_process_pixel(chips_69000_t* chips, uint32_t pixel)
{
    uint32_t pattern_fg = chips->bitblt_running.bitblt.pattern_source_key_fg;
    uint32_t pattern_bg = chips->bitblt_running.bitblt.pattern_source_key_bg;
    uint8_t pattern_data = 0;
    uint32_t pattern_pixel = 0;
    uint32_t dest_pixel = 0;
    uint32_t dest_addr = chips->bitblt_running.bitblt.destination_addr + (chips->bitblt_running.y * chips->bitblt_running.bitblt.destination_span) + (chips->bitblt_running.x * chips->bitblt_running.bytes_per_pixel);
    uint8_t vert_pat_alignment = (chips->bitblt_running.bitblt.bitblt_control >> 20) & 7;
    uint8_t orig_dest_addr_bit = chips->bitblt_running.bitblt.destination_addr & 1;

    switch (chips->bitblt_running.bytes_per_pixel) {
        case 1: /* 8 bits-per-pixel. */
            {
                dest_pixel = chips_69000_readb_linear(dest_addr, chips);
                break;
            }
        case 2: /* 16 bits-per-pixel. */
            {
                dest_pixel = chips_69000_readb_linear(dest_addr, chips);
                dest_pixel |= chips_69000_readb_linear(dest_addr + 1, chips) << 8;
                break;
            }
        case 3: /* 24 bits-per-pixel. */
            {
                dest_pixel = chips_69000_readb_linear(dest_addr, chips);
                dest_pixel |= chips_69000_readb_linear(dest_addr + 1, chips) << 8;
                dest_pixel |= chips_69000_readb_linear(dest_addr + 2, chips) << 16;
                break;
            }
    }

    if (chips->bitblt_running.bytes_per_pixel == 2) {
        chips->bitblt_running.bitblt.destination_addr >>= 1;
    }
    if (chips->bitblt_running.bitblt.bitblt_control & (1 << 18)) {
        uint8_t is_true = 0;
        if (chips->bitblt_running.bitblt.bitblt_control & (1 << 19))
            pattern_data = 0;
        else
            pattern_data = chips_69000_readb_linear(chips->bitblt_running.bitblt.pat_addr + ((vert_pat_alignment + (chips->bitblt_running.y & 7)) & 7), chips);

        is_true = !!(pattern_data & (1 << (7 - ((chips->bitblt_running.bitblt.destination_addr + chips->bitblt_running.x) & 7))));

        if (!is_true && (chips->bitblt_running.bitblt.bitblt_control & (1 << 17))) {
            if (chips->bitblt_running.bytes_per_pixel == 2) {
                chips->bitblt_running.bitblt.destination_addr <<= 1;
                chips->bitblt_running.bitblt.destination_addr |= orig_dest_addr_bit;
            }
            return;
        }

        pattern_pixel = is_true ? pattern_fg : pattern_bg;

        pattern_pixel &= (1 << (8 * (chips->bitblt_running.bytes_per_pixel))) - 1;
    } else {
        if (chips->bitblt_running.bytes_per_pixel == 1) {
            pattern_pixel = chips_69000_readb_linear(chips->bitblt_running.bitblt.pat_addr
                                                        + 8 * ((vert_pat_alignment + chips->bitblt_running.y) & 7)
                                                        + (((chips->bitblt_running.bitblt.destination_addr & 7) + chips->bitblt_running.x) & 7), chips);
        }
        if (chips->bitblt_running.bytes_per_pixel == 2) {
            pattern_pixel = chips_69000_readb_linear(chips->bitblt_running.bitblt.pat_addr
                                                        + (2 * 8 * ((vert_pat_alignment + chips->bitblt_running.y) & 7))
                                                        + (2 * (((chips->bitblt_running.bitblt.destination_addr & 7) + chips->bitblt_running.x) & 7)), chips);

            pattern_pixel |= chips_69000_readb_linear(chips->bitblt_running.bitblt.pat_addr
                                                        + (2 * 8 * ((vert_pat_alignment + chips->bitblt_running.y) & 7))
                                                        + (2 * (((chips->bitblt_running.bitblt.destination_addr & 7) + chips->bitblt_running.x) & 7)) + 1, chips) << 8;
        }
        if (chips->bitblt_running.bytes_per_pixel == 3) {
            pattern_pixel = chips_69000_readb_linear(chips->bitblt_running.bitblt.pat_addr
                                                        + (4 * 8 * ((vert_pat_alignment + chips->bitblt_running.y) & 7))
                                                        + (3 * (((chips->bitblt_running.bitblt.destination_addr & 7) + chips->bitblt_running.x) & 7)), chips);

            pattern_pixel |= chips_69000_readb_linear(chips->bitblt_running.bitblt.pat_addr
                                                        + (4 * 8 * ((vert_pat_alignment + chips->bitblt_running.y) & 7))
                                                        + (3 * (((chips->bitblt_running.bitblt.destination_addr & 7) + chips->bitblt_running.x) & 7)) + 1, chips) << 8;

            pattern_pixel |= chips_69000_readb_linear(chips->bitblt_running.bitblt.pat_addr
                                                        + (4 * 8 * ((vert_pat_alignment + chips->bitblt_running.y) & 7))
                                                        + (3 * (((chips->bitblt_running.bitblt.destination_addr & 7) + chips->bitblt_running.x) & 7)) + 2, chips) << 16;
        }
    }
    if (chips->bitblt_running.bytes_per_pixel == 2) {
        chips->bitblt_running.bitblt.destination_addr <<= 1;
        chips->bitblt_running.bitblt.destination_addr |= orig_dest_addr_bit;
    }

    if (chips->bitblt_running.bitblt.bitblt_control & (1 << 14)) {
        switch ((chips->bitblt_running.bitblt.bitblt_control >> 15) & 3) {
            case 1:
            case 3:
            {
                uint32_t color_key = (chips->bitblt_running.bitblt.monochrome_source_expansion_color_reg_select)
                ? chips->bitblt_running.bitblt.source_key_bg
                : chips->bitblt_running.bitblt.pattern_source_key_bg;
                color_key &= (1 << (8 * (chips->bitblt_running.bytes_per_pixel))) - 1;

                if (!!(color_key == dest_pixel) == !!(chips->bitblt_running.bitblt.bitblt_control & (1 << 16))) {
                    return;
                }

                break;
            }
        }
    }

    switch (chips->bitblt_running.bytes_per_pixel) {
        case 1: /* 8 bits-per-pixel. */
            {
                chips_69000_do_rop_8bpp_patterned((uint8_t*)&dest_pixel, pattern_pixel, pixel, chips->bitblt_running.bitblt.bitblt_control & 0xFF);
                break;
            }
        case 2: /* 16 bits-per-pixel. */
            {
                chips_69000_do_rop_16bpp_patterned((uint16_t*)&dest_pixel, pattern_pixel, pixel, chips->bitblt_running.bitblt.bitblt_control & 0xFF);
                break;
            }
        case 3: /* 24 bits-per-pixel. */
            {
                chips_69000_do_rop_24bpp_patterned((uint32_t*)&dest_pixel, pattern_pixel, pixel, chips->bitblt_running.bitblt.bitblt_control & 0xFF);
                break;
            }
    }

    if (chips->bitblt_running.bitblt.bitblt_control & (1 << 14)) {
        switch ((chips->bitblt_running.bitblt.bitblt_control >> 15) & 3) {
            case 0:
            case 2:
            {
                uint32_t color_key = (chips->bitblt_running.bitblt.monochrome_source_expansion_color_reg_select)
                ? chips->bitblt_running.bitblt.source_key_bg
                : chips->bitblt_running.bitblt.pattern_source_key_bg;
                color_key &= (1 << (8 * (chips->bitblt_running.bytes_per_pixel))) - 1;
                dest_pixel &= (1 << (8 * (chips->bitblt_running.bytes_per_pixel))) - 1;

                if (!!(color_key == dest_pixel) == !!(chips->bitblt_running.bitblt.bitblt_control & (1 << 16))) {
                    return;
                }

                break;
            }
        }
    }

    switch (chips->bitblt_running.bytes_per_pixel) {
        case 1: /* 8 bits-per-pixel. */
            {
                chips_69000_writeb_linear(dest_addr, dest_pixel & 0xFF, chips);
                break;
            }
        case 2: /* 16 bits-per-pixel. */
            {
                chips_69000_writeb_linear(dest_addr, dest_pixel & 0xFF, chips);
                chips_69000_writeb_linear(dest_addr + 1, (dest_pixel >> 8) & 0xFF, chips);
                break;
            }
        case 3: /* 24 bits-per-pixel. */
            {
                chips_69000_writeb_linear(dest_addr, dest_pixel & 0xFF, chips);
                chips_69000_writeb_linear(dest_addr + 1, (dest_pixel >> 8) & 0xFF, chips);
                chips_69000_writeb_linear(dest_addr + 2, (dest_pixel >> 16) & 0xFF, chips);
                break;
            }
    }
}

void
chips_69000_process_mono_bit(chips_69000_t* chips, uint8_t val)
{
    uint32_t pixel = 0x0;
    uint8_t is_true = !!val;
    uint32_t source_fg = chips->bitblt_running.bitblt.pattern_source_key_fg;
    uint32_t source_bg = chips->bitblt_running.bitblt.pattern_source_key_bg;

    if (!chips->engine_active)
        return;

    if (chips->bitblt_running.bitblt.monochrome_source_expansion_color_reg_select) {
        source_fg = chips->bitblt_running.bitblt.source_key_fg;
        source_bg = chips->bitblt_running.bitblt.source_key_bg;
    }

    if (chips->bitblt_running.bitblt.monochrome_source_initial_discard) {
        chips->bitblt_running.bitblt.monochrome_source_initial_discard--;
        return;
    }

    if (chips->bitblt_running.mono_bits_skip_left) {
        chips->bitblt_running.mono_bits_skip_left--;
        return;
    }

    if (!is_true && (chips->bitblt_running.bitblt.bitblt_control & (1 << 13))) {
        goto advance;
    }
    pixel = is_true ? source_fg : source_bg;
    pixel &= (1 << (8 * (chips->bitblt_running.bytes_per_pixel))) - 1;

    chips_69000_process_pixel(chips, pixel);

advance:
    chips->bitblt_running.x += chips->bitblt_running.x_dir;
    chips->bitblt_running.count_x += 1;
    if (chips->bitblt_running.count_x >= chips->bitblt_running.actual_destination_width) {
        chips->bitblt_running.count_y += 1;
        chips->bitblt_running.y += chips->bitblt_running.y_dir * 1;
        chips->bitblt_running.count_x = 0;
        chips->bitblt_running.x = 0;
        chips->bitblt_running.mono_bits_skip_left = chips->bitblt_running.bitblt.monochrome_source_left_clip;
        if (chips->bitblt_running.count_y >= (chips->bitblt_running.actual_destination_height))
            chips_69000_bitblt_interrupt(chips);
    }
}

void chips_69000_bitblt_write(chips_69000_t* chips, uint8_t data);

void
chips_69000_setup_bitblt(chips_69000_t* chips)
{
    chips->engine_active = 1;

    memset(&chips->bitblt_running, 0, sizeof(chips->bitblt_running));
    chips->bitblt_running.bitblt = chips->bitblt;
    chips->bitblt_running.actual_source_height = chips->bitblt.destination_height;
    chips->bitblt_running.actual_destination_height = chips->bitblt.destination_height;
    chips->bitblt_running.count_x = chips->bitblt_running.count_y = 0;
    chips->bitblt_running.bytes_written = 0;
    chips->bitblt_running.bytes_counter = 0;
    chips->bitblt_running.bytes_in_line_written = 0;
    chips->bitblt_running.bytes_skip = 0;
    chips->bitblt_running.mono_bytes_pitch = 0;
    chips->bitblt_running.mono_bits_skip_left = 0;
    int orig_cycles = cycles;

    if (chips->bitblt.bitblt_control & (1 << 23)) {
        chips->bitblt_running.bytes_per_pixel = 1 + ((chips->bitblt.bitblt_control >> 24) & 3);
    } else {
        chips->bitblt_running.bytes_per_pixel = 1 + ((chips->ext_regs[0x20] >> 4) & 3);
    }

    chips->bitblt_running.actual_destination_width = chips->bitblt_running.bitblt.destination_width / chips->bitblt_running.bytes_per_pixel;

    chips->bitblt_running.x = 0;
    chips->bitblt_running.y = 0;

    switch ((chips->bitblt_running.bitblt.bitblt_control >> 8) & 3) {
        case 0:
            chips->bitblt_running.x_dir = 1;
            chips->bitblt_running.y_dir = 1;
            break;
        case 1:
            chips->bitblt_running.x_dir = -1;
            chips->bitblt_running.y_dir = 1;
            if (!(chips->bitblt_running.bitblt.bitblt_control & (1 << 10)))
                chips->bitblt_running.bitblt.source_addr -= (chips->bitblt_running.bytes_per_pixel - 1);
            chips->bitblt_running.bitblt.destination_addr -= (chips->bitblt_running.bytes_per_pixel - 1);
            break;
        case 2:
            chips->bitblt_running.x_dir = 1;
            chips->bitblt_running.y_dir = -1;
            break;
        case 3:
            chips->bitblt_running.x_dir = -1;
            chips->bitblt_running.y_dir = -1;
            if (!(chips->bitblt_running.bitblt.bitblt_control & (1 << 10)))
                chips->bitblt_running.bitblt.source_addr -= (chips->bitblt_running.bytes_per_pixel - 1);
            chips->bitblt_running.bitblt.destination_addr -= (chips->bitblt_running.bytes_per_pixel - 1);
            break;
    }

    /* Drawing is pointless if monochrome pattern is enabled, monochrome write-masking is enabled and solid pattern is enabled. */
    if ((chips->bitblt_running.bitblt.bitblt_control & (1 << 17))
    && (chips->bitblt_running.bitblt.bitblt_control & (1 << 18))
    && (chips->bitblt_running.bitblt.bitblt_control & (1 << 19))) {
        chips_69000_bitblt_interrupt(chips);
        return;
    }

#if 0
    if (chips->bitblt_running.bitblt.bitblt_control & (1 << 12)) {
        pclog("C&T: Monochrome blit (monochrome_source_alignment = %d, "
        "monochrome left clip = %d, "
        "monochrome right clip = %d, "
        "monochrome initial discard = %d, "
        "destination_width = %d, destination_height = %d)\n", chips->bitblt_running.bitblt.monochrome_source_alignment,
        chips->bitblt_running.bitblt.monochrome_source_left_clip,
        chips->bitblt_running.bitblt.monochrome_source_right_clip,
        chips->bitblt_running.bitblt.monochrome_source_initial_discard,
        chips->bitblt_running.bitblt.destination_width,
        chips->bitblt_running.bitblt.destination_height);
    }
#endif

    if (chips->bitblt_running.bitblt.bitblt_control & (1 << 10)) {
        chips->bitblt_running.bitblt.source_addr &= 7;
        if (!(chips->bitblt_running.bitblt.bitblt_control & (1 << 12))) {
            /* Yes, the NT 4.0 and Linux drivers will send this many amount of bytes to the video adapter on quadword-boundary-crossing image blits.
               This weird calculation is intended and deliberate.
            */
            if ((chips->bitblt_running.bitblt.source_addr + (chips->bitblt_running.bitblt.destination_width)) > ((chips->bitblt_running.bitblt.destination_width + 7) & ~7))
                chips->bitblt_running.bytes_skip = 8 + (((chips->bitblt_running.bitblt.destination_width + 7) & ~7) - chips->bitblt_running.bitblt.destination_width);
        } else {
            chips->bitblt_running.mono_bits_skip_left = chips->bitblt_running.bitblt.monochrome_source_left_clip;

            if (chips->bitblt_running.bitblt.monochrome_source_alignment == 5)
                chips->bitblt_running.bitblt.monochrome_source_alignment = 0;

            if (chips->bitblt_running.bitblt.monochrome_source_alignment == 0) {
                chips->bitblt_running.mono_bytes_pitch = ((chips->bitblt_running.actual_destination_width + chips->bitblt_running.bitblt.monochrome_source_left_clip + 63) & ~63) / 8;
            }
        }

        return;
    }

    if (chips->bitblt_running.bitblt.bitblt_control & (1 << 12)) {
        uint32_t source_addr = chips->bitblt_running.bitblt.source_addr;

        while (chips->engine_active) {
            switch (chips->bitblt_running.bitblt.monochrome_source_alignment) {
                case 0: /* Source-span aligned. */
                    {
                        /* Note: This value means quadword-alignment when BitBLT port is the source. */
                        /* TODO: This is handled purely on a best case basis. */
                        uint32_t orig_count_y = chips->bitblt_running.count_y;
                        uint32_t orig_source_addr = chips->bitblt_running.bitblt.source_addr;
                        while (orig_count_y == chips->bitblt_running.count_y) {
                            int i = 0;
                            uint8_t data = chips_69000_readb_linear(orig_source_addr, chips);
                            orig_source_addr++;
                            for (i = 0; i < 8; i++) {
                                chips_69000_process_mono_bit(chips, !!(data & (1 << (7 - i))));
                                if (orig_count_y != chips->bitblt_running.count_y) {
                                    break;
                                }
                            }
                            if ((source_addr + chips->bitblt_running.bitblt.source_span) == orig_source_addr)
                                break;
                        }

                        source_addr = chips->bitblt_running.bitblt.source_addr + chips->bitblt_running.bitblt.source_span;
                        chips->bitblt_running.bitblt.source_addr = source_addr;
                        break;
                    }
                case 1: /* Bit-aligned */
                case 2: /* Byte-aligned */
                    {
                        uint32_t data = chips_69000_readb_linear(source_addr, chips);
                        chips_69000_bitblt_write(chips, data & 0xFF);
                        source_addr += 1;
                        break;
                    }
                case 3: /* Word-aligned*/
                    {
                        uint32_t data = chips_69000_readw_linear(source_addr, chips);
                        chips_69000_bitblt_write(chips, data & 0xFF);
                        chips_69000_bitblt_write(chips, (data >> 8) & 0xFF);
                        source_addr += 2;
                        break;
                    }
                case 4: /* Doubleword-aligned*/
                    {
                        uint32_t data = chips_69000_readl_linear(source_addr, chips);
                        chips_69000_bitblt_write(chips, data & 0xFF);
                        chips_69000_bitblt_write(chips, (data >> 8) & 0xFF);
                        chips_69000_bitblt_write(chips, (data >> 16) & 0xFF);
                        chips_69000_bitblt_write(chips, (data >> 24) & 0xFF);
                        source_addr += 4;
                        break;
                    }
                case 5: /* Quadword-aligned*/
                    {
                        uint64_t data = (uint64_t)chips_69000_readl_linear(source_addr, chips) | ((uint64_t)chips_69000_readl_linear(source_addr + 4, chips) << 32ull);
                        chips_69000_bitblt_write(chips, data & 0xFF);
                        chips_69000_bitblt_write(chips, (data >> 8) & 0xFF);
                        chips_69000_bitblt_write(chips, (data >> 16) & 0xFF);
                        chips_69000_bitblt_write(chips, (data >> 24) & 0xFF);
                        chips_69000_bitblt_write(chips, (data >> 32ull) & 0xFF);
                        chips_69000_bitblt_write(chips, (data >> 40ull) & 0xFF);
                        chips_69000_bitblt_write(chips, (data >> 48ull) & 0xFF);
                        chips_69000_bitblt_write(chips, (data >> 56ull) & 0xFF);
                        source_addr += 8;
                        break;
                    }
            }
        }
        return;
    }

    do {
        do {
            uint32_t pixel = 0;
            uint32_t source_addr = chips->bitblt_running.bitblt.source_addr + (chips->bitblt_running.y * chips->bitblt.source_span) + (chips->bitblt_running.x * chips->bitblt_running.bytes_per_pixel);

            switch (chips->bitblt_running.bytes_per_pixel) {
                case 1: /* 8 bits-per-pixel. */
                    {
                        pixel = chips_69000_readb_linear(source_addr, chips);
                        break;
                    }
                case 2: /* 16 bits-per-pixel. */
                    {
                        pixel = chips_69000_readb_linear(source_addr, chips);
                        pixel |= chips_69000_readb_linear(source_addr + 1, chips) << 8;
                        break;
                    }
                case 3: /* 24 bits-per-pixel. */
                    {
                        pixel = chips_69000_readb_linear(source_addr, chips);
                        pixel |= chips_69000_readb_linear(source_addr + 1, chips) << 8;
                        pixel |= chips_69000_readb_linear(source_addr + 2, chips) << 16;
                        break;
                    }
            }

            chips_69000_process_pixel(chips, pixel);

            chips->bitblt_running.x += chips->bitblt_running.x_dir;
        } while ((++chips->bitblt_running.count_x) < chips->bitblt_running.actual_destination_width);

        chips->bitblt_running.y += chips->bitblt_running.y_dir;
        chips->bitblt_running.count_x = 0;
        chips->bitblt_running.x = 0;
    } while ((++chips->bitblt_running.count_y) < chips->bitblt_running.actual_destination_height);
    cycles = orig_cycles;
    chips_69000_bitblt_interrupt(chips);
}

void
chips_69000_bitblt_write(chips_69000_t* chips, uint8_t data) {

    if (!chips->engine_active) {
        return;
    }

    if (chips->bitblt_running.bitblt.bitblt_control & (1 << 12)) {
        int orig_cycles = cycles;
        chips->bitblt_running.bytes_port[chips->bitblt_running.bytes_written++] = data;
        if (chips->bitblt_running.bitblt.monochrome_source_alignment == 1) {
            uint8_t val = chips->bitblt_running.bytes_port[0];
            int i = 0;
            chips->bitblt_running.bytes_written = 0;
            for (i = 0; i < 8; i++) {
                chips_69000_process_mono_bit(chips, !!(val & (1 << (7 - i))));
            }
        } else if (chips->bitblt_running.bitblt.monochrome_source_alignment == 0 && chips->bitblt_running.mono_bytes_pitch && chips->bitblt_running.mono_bytes_pitch == chips->bitblt_running.bytes_written) {
            int orig_count_y = chips->bitblt_running.count_y;
            int i = 0, j = 0;
            chips->bitblt_running.bytes_written = 0;

            for (j = 0; j < chips->bitblt_running.mono_bytes_pitch; j++) {
                for (i = 0; i < 8; i++) {
                    chips_69000_process_mono_bit(chips, !!(chips->bitblt_running.bytes_port[j] & (1 << (7 - i))));
                    if (orig_count_y != chips->bitblt_running.count_y) {
                        cycles = orig_cycles;
                        return;
                    }
                }
            }
        }
        else if ((chips->bitblt_running.bitblt.monochrome_source_alignment == 0 && !chips->bitblt_running.mono_bytes_pitch)
        || chips->bitblt_running.bitblt.monochrome_source_alignment == 2) {
            int orig_count_y = chips->bitblt_running.count_y;
            int i = 0;
            uint8_t val = chips->bitblt_running.bytes_port[0];
            chips->bitblt_running.bytes_written = 0;

            for (i = 0; i < 8; i++) {
                chips_69000_process_mono_bit(chips, !!(val & (1 << (7 - i))));
                if (orig_count_y != chips->bitblt_running.count_y && chips->bitblt_running.bitblt.monochrome_source_alignment != 1) {
                    cycles = orig_cycles;
                    return;
                }
            }
        } else if (chips->bitblt_running.bitblt.monochrome_source_alignment == 3
        && chips->bitblt_running.bytes_written == 2) {
            int orig_count_y = chips->bitblt_running.count_y;
            int i = 0;
            uint16_t val = (chips->bitblt_running.bytes_port[1]) | (chips->bitblt_running.bytes_port[0] << 8);
            chips->bitblt_running.bytes_written = 0;

            for (i = 0; i < 16; i++) {
                chips_69000_process_mono_bit(chips, !!(val & (1 << (15 - i))));
                if (orig_count_y != chips->bitblt_running.count_y) {
                    cycles = orig_cycles;
                    return;
                }
            }
        } else if (chips->bitblt_running.bitblt.monochrome_source_alignment == 4
        && chips->bitblt_running.bytes_written == 4) {
            int orig_count_y = chips->bitblt_running.count_y;
            int i = 0;
            uint32_t val = chips->bitblt_running.bytes_port[3] | (chips->bitblt_running.bytes_port[2] << 8) | (chips->bitblt_running.bytes_port[1] << 16) | (chips->bitblt_running.bytes_port[0] << 24);
            chips->bitblt_running.bytes_written = 0;

            for (i = 0; i < 32; i++) {
                chips_69000_process_mono_bit(chips, !!(val & (1 << (31 - i))));
                if (orig_count_y != chips->bitblt_running.count_y) {
                    cycles = orig_cycles;
                    return;
                }
            }
        } else if (chips->bitblt_running.bitblt.monochrome_source_alignment == 5 && chips->bitblt_running.bytes_written == 8) {
            int orig_count_y = chips->bitblt_running.count_y;
            int i = 0;
            uint64_t val = 0;

            val |= chips->bitblt_running.bytes_port[7];
            val |= chips->bitblt_running.bytes_port[6] << 8;
            val |= chips->bitblt_running.bytes_port[5] << 16;
            val |= chips->bitblt_running.bytes_port[4] << 24;
            val |= (uint64_t)chips->bitblt_running.bytes_port[3] << 32ULL;
            val |= (uint64_t)chips->bitblt_running.bytes_port[2] << 40ULL;
            val |= (uint64_t)chips->bitblt_running.bytes_port[1] << 48ULL;
            val |= (uint64_t)chips->bitblt_running.bytes_port[0] << 56ULL;

            chips->bitblt_running.bytes_written = 0;

            for (i = 0; i < 64; i++) {
                chips_69000_process_mono_bit(chips, !!(val & (1 << (63 - i))));
                if (orig_count_y != chips->bitblt_running.count_y) {
                    cycles = orig_cycles;
                    return;
                }
            }
        }
        cycles = orig_cycles;
        return;
    }

    chips->bitblt_running.bytes_counter++;
    if (chips->bitblt_running.bytes_counter <= (chips->bitblt_running.bitblt.source_addr)) {
        return;
    }
    chips->bitblt_running.bytes_port[chips->bitblt_running.bytes_written++] = data;
    if (chips->bitblt_running.bytes_written == chips->bitblt_running.bytes_per_pixel) {
        int orig_cycles = cycles;
        uint32_t source_pixel = chips->bitblt_running.bytes_port[0];
        chips->bitblt_running.bytes_written = 0;
        if (chips->bitblt_running.bytes_per_pixel >= 2)
            source_pixel |= (chips->bitblt_running.bytes_port[1] << 8);
        if (chips->bitblt_running.bytes_per_pixel >= 3)
            source_pixel |= (chips->bitblt_running.bytes_port[2] << 16);

        chips->bitblt_running.bytes_in_line_written += chips->bitblt_running.bytes_per_pixel;

        chips_69000_process_pixel(chips, source_pixel);
        cycles = orig_cycles;
        chips->bitblt_running.x += chips->bitblt_running.x_dir;

        if (chips->bitblt_running.bytes_in_line_written >= chips->bitblt_running.bitblt.destination_width) {
            if (chips->bitblt_running.bytes_skip) {
                chips->bitblt_running.bitblt.source_addr = chips->bitblt_running.bytes_skip;
            }
            else if (chips->bitblt_running.bitblt.destination_width & 7)
                chips->bitblt_running.bitblt.source_addr = 8 - ((chips->bitblt_running.bitblt.destination_width) & 7);
            else
                chips->bitblt_running.bitblt.source_addr = 0;

            chips->bitblt_running.y += chips->bitblt_running.y_dir;
            chips->bitblt_running.count_y++;
            chips->bitblt_running.bytes_counter = 0;
            chips->bitblt_running.bytes_in_line_written = 0;

            chips->bitblt_running.count_x = 0;
            chips->bitblt_running.x = 0;

            if (chips->bitblt_running.count_y >= chips->bitblt_running.actual_destination_height) {
                chips_69000_bitblt_interrupt(chips);
                return;
            }
        }
    }
}

uint8_t
chips_69000_read_ext_reg(chips_69000_t* chips)
{
    uint8_t index = chips->ext_index;
    uint8_t val = chips->ext_regs[index];
    switch (index) {
        case 0x00:
            val = 0x2C;
            break;
        case 0x01:
            val = 0x10;
            break;
        case 0x02:
            val = 0xC0;
            break;
        case 0x03:
            val = 0x00;
            break;
        case 0x04:
            val = 0x62;
            break;
        case 0x05:
            val = 0x00;
            break;
        case 0x06:
            val = chips->linear_mapping.base >> 24;
            break;
        case 0x08:
            val = 0x02;
            break;
        case 0x0A:
            val = chips->ext_regs[index] & 0x37;
            break;
        case 0x20:
            val &= ~1;
            val |= !!chips->engine_active;
            /* TODO: Handle BitBLT reset, if required. */
            break;
        case 0x63:
            {
                val = chips->ext_regs[index];
                if (!(chips->ext_regs[0x62] & 0x8))
                    val = (val & ~8) | (i2c_gpio_get_scl(chips->i2c) << 3);

                if (!(chips->ext_regs[0x62] & 0x4))
                    val = (val & ~4) | (i2c_gpio_get_sda(chips->i2c) << 2);

                break;
            }
        case 0x70:
            val = 0x3;
            break;
        case 0x71:
            val = 0b01101000;
            break;
        case 0xD0:
            val |= 1;
            break;
    }
    return val;
}

void
chips_69000_write_ext_reg(chips_69000_t* chips, uint8_t val)
{
    switch (chips->ext_index) {
        case 0xA:
            chips->ext_regs[chips->ext_index] = val & 0x37;
            chips_69000_recalc_banking(chips);
            break;
        case 0xB:
            chips->ext_regs[chips->ext_index] = val & 0xD;
            break;
        case 0xE:
            chips->ext_regs[chips->ext_index] = val & 0x7f;
            chips_69000_recalc_banking(chips);
            break;
        case 0x9:
            chips->ext_regs[chips->ext_index] = val & 0x3;
            svga_recalctimings(&chips->svga);
            break;
        case 0x40:
            chips->ext_regs[chips->ext_index] = val & 0x3;
            chips_69000_recalc_banking(chips);
            svga_recalctimings(&chips->svga);
            break;
        case 0x60:
            chips->ext_regs[chips->ext_index] = val & 0x43;
            break;
        case 0x20:
            chips->ext_regs[chips->ext_index] = val & 0x3f;
            break;
        case 0x61:
            chips->ext_regs[chips->ext_index] = val & 0x7f;
            svga_recalctimings(&chips->svga);
            break;
        case 0x62:
            chips->ext_regs[chips->ext_index] = val & 0x9C;
            break;
        case 0x63:
            {
                uint8_t scl = 0, sda = 0;
                if (chips->ext_regs[0x62] & 0x8)
                    scl = !!(val & 8);
                else
                    scl = i2c_gpio_get_scl(chips->i2c);

                if (chips->ext_regs[0x62] & 0x4)
                    sda = !!(val & 4);
                else
                    scl = i2c_gpio_get_sda(chips->i2c);

                i2c_gpio_set(chips->i2c, scl, sda);

                chips->ext_regs[chips->ext_index] = val & 0x9F;
                break;
            }
        case 0x67:
            chips->ext_regs[chips->ext_index] = val & 0x2;
            break;
        case 0x80:
            chips->ext_regs[chips->ext_index] = val & 0xBF;
            svga_set_ramdac_type(&chips->svga, (val & 0x80) ? RAMDAC_8BIT : RAMDAC_6BIT);
            break;
        case 0x81:
            chips->ext_regs[chips->ext_index] = val & 0x1f;
            svga_recalctimings(&chips->svga);
            break;
        case 0x82:
            chips->ext_regs[chips->ext_index] = val & 0xf;
            chips->svga.lut_map = !!(val & 0x8);
            break;
        case 0xA0:
            chips->ext_regs[chips->ext_index] = val;
            chips->svga.hwcursor.ena = ((val & 7) == 0b101) || ((val & 7) == 0b1);
            chips->svga.hwcursor.cur_xsize = chips->svga.hwcursor.cur_ysize = ((val & 7) == 0b1) ? 32 : 64;
            break;
        case 0xA2:
            chips->ext_regs[chips->ext_index] = val;
            chips->svga.hwcursor.addr = (val << 8) | ((chips->ext_regs[0xA3] & 0x3F) << 16);
            break;
        case 0xA3:
            chips->ext_regs[chips->ext_index] = val;
            chips->svga.hwcursor.addr = ((chips->ext_regs[0xA2]) << 8) | ((val & 0x3F) << 16);
            break;
        case 0xA4:
            chips->ext_regs[chips->ext_index] = val;
            chips->svga.hwcursor.x = val | (chips->ext_regs[0xA5] & 7) << 8;
            if (chips->ext_regs[0xA5] & 0x80)
                chips->svga.hwcursor.x = -chips->svga.hwcursor.x;
            break;
        case 0xA5:
            chips->ext_regs[chips->ext_index] = val;
            chips->svga.hwcursor.x = chips->ext_regs[0xA4] | (val & 7) << 8;
            if (chips->ext_regs[0xA5] & 0x80)
                chips->svga.hwcursor.x = -chips->svga.hwcursor.x;
            break;
        case 0xA6:
            chips->ext_regs[chips->ext_index] = val;
            chips->svga.hwcursor.y = val | (chips->ext_regs[0xA7] & 7) << 8;
            if (chips->ext_regs[0xA7] & 0x80) {
                chips->svga.hwcursor.y = -chips->svga.hwcursor.y;
            }
            break;
        case 0xA7:
            chips->ext_regs[chips->ext_index] = val;
            chips->svga.hwcursor.y = chips->ext_regs[0xA6] | (val & 7) << 8;
            if (chips->ext_regs[0xA7] & 0x80) {
                chips->svga.hwcursor.y = -chips->svga.hwcursor.y;
            }
            break;
        case 0xC8:
        case 0xC9:
        case 0xCB:
            chips->ext_regs[chips->ext_index] = val;
            svga_recalctimings(&chips->svga);
            break;
        case 0xD2:
            break;
        default:
            chips->ext_regs[chips->ext_index] = val;
            break;
    }
}

void
chips_69000_out(uint16_t addr, uint8_t val, void *priv)
{
    chips_69000_t  *chips  = (chips_69000_t *) priv;
    svga_t *svga = &chips->svga;
    uint8_t old, index;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c0:
            if (!(chips->ext_regs[0x09] & 0x02))
                break;
            svga->attraddr = val & 31;
            if ((val & 0x20) != svga->attr_palette_enable) {
                svga->fullchange          = 3;
                svga->attr_palette_enable = val & 0x20;
                svga_recalctimings(svga);
            }
            return;
        case 0x3c1:
            if ((chips->ext_regs[0x09] & 0x02))
            {
                svga->attrff = 1;
                svga_out(addr, val, svga);
                svga->attrff = 0;
                return;
            }
            break;
        case 0x3c9:
            if (!(chips->ext_regs[0x80] & 0x01))
                break;
            if (svga->adv_flags & FLAG_RAMDAC_SHIFT)
                val <<= 2;
            svga->fullchange = svga->monitor->mon_changeframecount;
            switch (svga->dac_pos) {
                case 0:
                    svga->dac_r = val;
                    svga->dac_pos++;
                    break;
                case 1:
                    svga->dac_g = val;
                    svga->dac_pos++;
                    break;
                case 2:
                    index                 = svga->dac_addr & 7;
                    chips->cursor_palette[index].r = svga->dac_r;
                    chips->cursor_palette[index].g = svga->dac_g;
                    chips->cursor_palette[index].b = val;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        chips->cursor_pallook[index] = makecol32(chips->cursor_palette[index].r, chips->cursor_palette[index].g, chips->cursor_palette[index].b);
                    else
                        chips->cursor_pallook[index] = makecol32(video_6to8[chips->cursor_palette[index].r & 0x3f], video_6to8[chips->cursor_palette[index].g & 0x3f], video_6to8[chips->cursor_palette[index].b & 0x3f]);
                    svga->dac_pos  = 0;
                    svga->dac_addr = (svga->dac_addr + 1) & 255;
                    break;
            }
            return;
        case 0x3c5:
            svga_out(addr, val, svga);
            chips_69000_recalc_banking(chips);
            return;
        case 0x3D0:
            chips->flat_panel_index = val;
            return;
        case 0x3D1:
            return chips_69000_write_flat_panel(chips, val);
        case 0x3D2:
            chips->mm_index = val;
            return;
        case 0x3D3:
            return chips_69000_write_multimedia(chips, val);
        case 0x3D4:
            svga->crtcreg = val & 0xff;
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
        case 0x3B6:
        case 0x3D6:
            chips->ext_index = val;
            return;
        case 0x3B7:
        case 0x3D7:
            return chips_69000_write_ext_reg(chips, val);

    }
    svga_out(addr, val, svga);
}

uint8_t
chips_69000_in(uint16_t addr, void *priv)
{
    chips_69000_t  *chips  = (chips_69000_t *) priv;
    svga_t *svga = &chips->svga;
    uint8_t temp = 0, index;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3C5:
            return svga->seqregs[svga->seqaddr];
        case 0x3c9:
            if (!(chips->ext_regs[0x80] & 0x01)) {
                temp = svga_in(addr, svga);
                break;
            }
            index = (svga->dac_addr - 1) & 7;
            switch (svga->dac_pos) {
                case 0:
                    svga->dac_pos++;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        temp = chips->cursor_palette[index].r;
                    else
                        temp = chips->cursor_palette[index].r & 0x3f;
                    break;
                case 1:
                    svga->dac_pos++;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        temp = chips->cursor_palette[index].g;
                    else
                        temp = chips->cursor_palette[index].g & 0x3f;
                    break;
                case 2:
                    svga->dac_pos  = 0;
                    svga->dac_addr = (svga->dac_addr + 1) & 255;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        temp = chips->cursor_palette[index].b;
                    else
                        temp = chips->cursor_palette[index].b & 0x3f;
                    break;
            }
            if (svga->adv_flags & FLAG_RAMDAC_SHIFT)
                temp >>= 2;
            break;
        case 0x3D0:
            return chips->flat_panel_index;
        case 0x3D1:
            return chips_69000_read_flat_panel(chips);
        case 0x3D2:
            return chips->mm_index;
        case 0x3D3:
            return chips_69000_read_multimedia(chips);
        case 0x3D4:
            temp = svga->crtcreg;
            break;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                temp = 0xff;
            else
                temp = svga->crtc[svga->crtcreg];
            break;
        case 0x3B6:
        case 0x3D6:
            temp = chips->ext_index;
            break;
        case 0x3B7:
        case 0x3D7:
            temp = chips_69000_read_ext_reg(chips);
            break;
        default:
            temp = svga_in(addr, svga);
            break;
    }
    return temp;
}

static uint8_t
chips_69000_pci_read(UNUSED(int func), int addr, void *priv)
{
    chips_69000_t *chips = (chips_69000_t *) priv;

    {
        switch (addr) {
            case 0x00:
                return 0x2C;
            case 0x01:
                return 0x10;
            case 0x02:
                return 0xC0;
            case 0x03:
                return 0x00;
            case 0x04:
                return (chips->pci_conf_status & 0b11100011) | 0x80;
            case 0x06:
                return 0x80;
            case 0x07:
                return 0x02;
            case 0x08:
            case 0x09:
            case 0x0a:
                return 0x00;
            case 0x0b:
                return 0x03;
            case 0x13:
                return chips->linear_mapping.base >> 24;
            case 0x30:
                return chips->pci_rom_enable & 0x1;
            case 0x31:
                return 0x0;
            case 0x32:
                return chips->rom_addr & 0xFF;
            case 0x33:
                return (chips->rom_addr & 0xFF00) >> 8;
            case 0x3c:
                return chips->pci_line_interrupt;
            case 0x3d:
                return 0x01;
            case 0x2C:
            case 0x2D:
            case 0x6C:
            case 0x6D:
                return (chips->subsys_vid >> ((addr & 1) * 8)) & 0xFF;
            case 0x2E:
            case 0x2F:
            case 0x6E:
            case 0x6F:
                return (chips->subsys_pid >> ((addr & 1) * 8)) & 0xFF;
            default:
                return 0x00;
        }
    }
}

static void
chips_69000_pci_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    chips_69000_t *chips = (chips_69000_t *) priv;

    {
        switch (addr) {
            case 0x04:
                {
                    chips->pci_conf_status = val;
                    io_removehandler(0x03c0, 0x0020, chips_69000_in, NULL, NULL, chips_69000_out, NULL, NULL, chips);
                    mem_mapping_disable(&chips->linear_mapping);
                    mem_mapping_disable(&chips->svga.mapping);
                    if (chips->pci_conf_status & PCI_COMMAND_IO) {
                        io_sethandler(0x03c0, 0x0020, chips_69000_in, NULL, NULL, chips_69000_out, NULL, NULL, chips);
                    }
                    if (chips->pci_conf_status & PCI_COMMAND_MEM) {
                        mem_mapping_enable(&chips->svga.mapping);
                        if (chips->linear_mapping.base)
                            mem_mapping_set_addr(&chips->linear_mapping, chips->linear_mapping.base, (1 << 24));
                    }
                    break;
                }
            case 0x13:
                {
                    chips->linear_mapping.base = val << 24;
                    if (chips->linear_mapping.base)
                        mem_mapping_set_addr(&chips->linear_mapping, chips->linear_mapping.base, (1 << 24));
                    break;
                }
            case 0x3c:
                chips->pci_line_interrupt = val;
                break;
            case 0x30:
                if (chips->on_board) break;
                chips->pci_rom_enable = val & 0x1;
                mem_mapping_disable(&chips->bios_rom.mapping);
                if (chips->pci_rom_enable & 1) {
                    mem_mapping_set_addr(&chips->bios_rom.mapping, chips->rom_addr << 16, 0x10000);
                }
                break;
            case 0x32:
                if (chips->on_board) break;
                chips->rom_addr &= ~0xFF;
                chips->rom_addr |= val & 0xFC;
                if (chips->pci_rom_enable & 1) {
                    mem_mapping_set_addr(&chips->bios_rom.mapping, chips->rom_addr << 16, 0x10000);
                }
                break;
            case 0x33:
                if (chips->on_board) break;
                chips->rom_addr &= ~0xFF00;
                chips->rom_addr |= (val << 8);
                if (chips->pci_rom_enable & 1) {
                    mem_mapping_set_addr(&chips->bios_rom.mapping, chips->rom_addr << 16, 0x10000);
                }
                break;
            case 0x6C:
            case 0x6D:
                chips->subsys_vid_b[addr & 1] = val;
                break;
            case 0x6E:
            case 0x6F:
                chips->subsys_pid_b[addr & 1] = val;
                break;
        }
    }
}

uint8_t
chips_69000_readb_mmio(uint32_t addr, chips_69000_t* chips)
{
    addr &= 0xFFF;
    switch (addr & 0xFFF) {
        case 0x00 ... 0x2B:
            if (addr == 0x13) {
                return (chips->bitblt_regs_b[addr & 0xFF] & 0x7F) | (chips->engine_active ? 0x80 : 0x00);
            }
            return chips->bitblt_regs_b[addr & 0xFF];
        case 0x3b:
            return (chips->engine_active ? 0x80 : 0x00);
        case 0x38:
            return 0x7F;
        case 0x600 ... 0x60F:
            return chips->mem_regs_b[addr & 0xF];
        case 0x768:
            return chips_69000_in(0x3b4, chips);
        case 0x769:
            return chips_69000_in(0x3b5, chips);
        case 0x774:
            return chips_69000_in(0x3ba, chips);
        case 0x780:
            return chips_69000_in(0x3c0, chips);
        case 0x781:
            return chips_69000_in(0x3c1, chips);
        case 0x784:
            return chips_69000_in(0x3c2, chips);
        case 0x788:
            return chips_69000_in(0x3c4, chips);
        case 0x789:
            return chips_69000_in(0x3c5, chips);
        case 0x78C:
            return chips_69000_in(0x3c6, chips);
        case 0x78D:
            return chips_69000_in(0x3c7, chips);
        case 0x790:
            return chips_69000_in(0x3c8, chips);
        case 0x791:
            return chips_69000_in(0x3c9, chips);
        case 0x794:
            return chips_69000_in(0x3ca, chips);
        case 0x798:
            return chips_69000_in(0x3cc, chips);
        case 0x79C:
            return chips_69000_in(0x3ce, chips);
        case 0x79D:
            return chips_69000_in(0x3cf, chips);
        case 0x7A0:
            return chips_69000_in(0x3d0, chips);
        case 0x7A1:
            return chips_69000_in(0x3d1, chips);
        case 0x7A4:
            return chips_69000_in(0x3d2, chips);
        case 0x7A5:
            return chips_69000_in(0x3d3, chips);
        case 0x7A8:
            return chips_69000_in(0x3d4, chips);
        case 0x7A9:
            return chips_69000_in(0x3d5, chips);
        case 0x7AC:
            return chips_69000_in(0x3d6, chips);
        case 0x7AD:
            return chips_69000_in(0x3d7, chips);
        case 0x7B4:
            return chips_69000_in(0x3da, chips);
    }
    return 0x00;
}

uint16_t
chips_69000_readw_mmio(uint32_t addr, chips_69000_t* chips)
{
    addr &= 0xFFF;
    switch (addr & 0xFFF) {
        default:
            return chips_69000_readb_mmio(addr, chips) | (chips_69000_readb_mmio(addr + 1, chips) << 8);
    }
    return 0xFFFF;
}

uint32_t
chips_69000_readl_mmio(uint32_t addr, chips_69000_t* chips)
{
    addr &= 0xFFF;
    switch (addr & 0xFFF) {
        default:
            return chips_69000_readw_mmio(addr, chips) | (chips_69000_readw_mmio(addr + 2, chips) << 16);
    }
    return 0xFFFFFFFF;
}

void
chips_69000_writeb_mmio(uint32_t addr, uint8_t val, chips_69000_t* chips)
{
    //pclog("C&T Write 0x%X, val = 0x%02X\n", addr, val);
    if (addr & 0x10000) {
        chips_69000_bitblt_write(chips, val);
        return;
    }
    addr &= 0xFFF;
    switch (addr & 0xFFF) {
        case 0x00 ... 0x2B:
            if (addr <= 0x3) {
                //pclog("[%04X:%08X] C&T Write span 0x%X, val = 0x%02X\n", CS, cpu_state.pc, addr, val);
            }
            chips->bitblt_regs_b[addr & 0xFF] = val;
            if ((addr & 0xFFF) == 0x023 && chips->bitblt_regs[0x8] != 0) {
                chips_69000_setup_bitblt(chips);
            }
            break;
        default:
            pclog("C&T Write (unknown) 0x%X, val = 0x%02X\n", addr, val);
            break;
        case 0x600 ... 0x60F:
            switch (addr & 0xFFF)
            {
                case 0x600 ... 0x603:
                {
                    chips->mem_regs_b[addr & 0xF] = val;
                    chips->mem_regs[(addr >> 2) & 0x3] &= 0x80004040;
                    if (addr == 0x605 || addr == 0x607)
                        chips_69000_interrupt(chips);
                    break;
                }

                case 0x604 ... 0x607:
                {
                    chips->mem_regs_b[addr & 0xF] &= ~val;
                    chips->mem_regs[(addr >> 2) & 0x3] &= 0x80004040;
                    if (addr == 0x605 || addr == 0x607)
                        chips_69000_interrupt(chips);
                    break;
                }

                case 0x60c ... 0x60f:
                {
                    chips->mem_regs_b[addr & 0xF] = val;
                    break;
                }

            }
            chips->mem_regs_b[addr & 0xF] = val;
            break;
        case 0x768:
            chips_69000_out(0x3b4, val, chips); break;
        case 0x769:
            chips_69000_out(0x3b5, val, chips); break;
        case 0x774:
            chips_69000_out(0x3ba, val, chips); break;
        case 0x780:
            chips_69000_out(0x3c0, val, chips); break;
        case 0x781:
            chips_69000_out(0x3c1, val, chips); break;
        case 0x784:
            chips_69000_out(0x3c2, val, chips); break;
        case 0x788:
            chips_69000_out(0x3c4, val, chips); break;
        case 0x789:
            chips_69000_out(0x3c5, val, chips); break;
        case 0x78C:
            chips_69000_out(0x3c6, val, chips); break;
        case 0x78D:
            chips_69000_out(0x3c7, val, chips); break;
        case 0x790:
            chips_69000_out(0x3c8, val, chips); break;
        case 0x791:
            chips_69000_out(0x3c9, val, chips); break;
        case 0x794:
            chips_69000_out(0x3ca, val, chips); break;
        case 0x798:
            chips_69000_out(0x3cc, val, chips); break;
        case 0x79C:
            chips_69000_out(0x3ce, val, chips); break;
        case 0x79D:
            chips_69000_out(0x3cf, val, chips); break;
        case 0x7A0:
            chips_69000_out(0x3d0, val, chips); break;
        case 0x7A1:
            chips_69000_out(0x3d1, val, chips); break;
        case 0x7A4:
            chips_69000_out(0x3d2, val, chips); break;
        case 0x7A5:
            chips_69000_out(0x3d3, val, chips); break;
        case 0x7A8:
            chips_69000_out(0x3d4, val, chips); break;
        case 0x7A9:
            chips_69000_out(0x3d5, val, chips); break;
        case 0x7AC:
            chips_69000_out(0x3d6, val, chips); break;
        case 0x7AD:
            chips_69000_out(0x3d7, val, chips); break;
        case 0x7B4:
            chips_69000_out(0x3da, val, chips); break;
    }
}

void
chips_69000_writew_mmio(uint32_t addr, uint16_t val, chips_69000_t* chips)
{
    if (addr & 0x10000) {
        if ((chips->bitblt_running.bitblt.bitblt_control & (1 << 12))) {
            //pclog("BitBLT mono 0x%04X\n", val);
        }
        chips_69000_bitblt_write(chips, val & 0xFF);
        chips_69000_bitblt_write(chips, (val >> 8) & 0xFF);
        return;
    }
    addr &= 0xFFF;
    switch (addr & 0xFFF) {
        default:
            chips_69000_writeb_mmio(addr, val, chips);
            chips_69000_writeb_mmio(addr + 1, val >> 8, chips);
            break;
    }
}

void
chips_69000_writel_mmio(uint32_t addr, uint32_t val, chips_69000_t* chips)
{
    if (addr & 0x10000) {
        if ((chips->bitblt_running.bitblt.bitblt_control & (1 << 12))) {
            //pclog("BitBLT mono 0x%08X\n", val);
        }
        chips_69000_bitblt_write(chips, val & 0xFF);
        chips_69000_bitblt_write(chips, (val >> 8) & 0xFF);
        chips_69000_bitblt_write(chips, (val >> 16) & 0xFF);
        chips_69000_bitblt_write(chips, (val >> 24) & 0xFF);
        return;
    }
    addr &= 0xFFF;
    switch (addr & 0xFFF) {
        default:
            chips_69000_writew_mmio(addr, val, chips);
            chips_69000_writew_mmio(addr + 2, val >> 16, chips);
            break;
    }
}

uint8_t
chips_69000_readb_linear(uint32_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x400000)
        return chips_69000_readb_mmio(addr, chips);

    return svga_readb_linear(addr & 0x1FFFFF, priv);
}

uint16_t
chips_69000_readw_linear(uint32_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x800000) {
        if (addr & 0x400000)
            return bswap16(chips_69000_readw_mmio(addr, chips));

        return bswap16(svga_readw_linear(addr & 0x1FFFFF, priv));
    }

    if (addr & 0x400000)
        return chips_69000_readw_mmio(addr, chips);

    return svga_readw_linear(addr & 0x1FFFFF, priv);
}

uint32_t
chips_69000_readl_linear(uint32_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x800000) {
        if (addr & 0x400000)
            return bswap32(chips_69000_readl_mmio(addr, chips));

        return bswap32(svga_readl_linear(addr & 0x1FFFFF, priv));
    }

    if (addr & 0x400000)
        return chips_69000_readl_mmio(addr, chips);

    return svga_readl_linear(addr & 0x1FFFFF, priv);
}

void
chips_69000_writeb_linear(uint32_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x400000)
        return chips_69000_writeb_mmio(addr, val, chips);

    svga_writeb_linear(addr & 0x1FFFFF, val, priv);
}

void
chips_69000_writew_linear(uint32_t addr, uint16_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x800000)
        val = bswap16(val);

    if (addr & 0x400000)
        return chips_69000_writew_mmio(addr, val, chips);

    svga_writew_linear(addr & 0x1FFFFF, val, priv);
}

void
chips_69000_writel_linear(uint32_t addr, uint32_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x800000)
        val = bswap32(val);

    if (addr & 0x400000)
        return chips_69000_writel_mmio(addr, val, chips);

    svga_writel_linear(addr & 0x1FFFFF, val, priv);
}

void
chips_69000_vblank_start(svga_t *svga)
{
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;
    chips->mem_regs[1] |= 1 << 14;
    chips->svga.crtc[0x40] &= ~0x80;

    chips_69000_interrupt(chips);
}

static void
chips_69000_hwcursor_draw_64x64(svga_t *svga, int displine)
{
    chips_69000_t    *chips  = (chips_69000_t *) svga->priv;
    uint64_t          dat[2];
    int               offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;

    if (svga->interlace && svga->hwcursor_oddeven)
        svga->hwcursor_latch.addr += 16;

    dat[1] = bswap64(*(uint64_t *) (&svga->vram[svga->hwcursor_latch.addr]));
    dat[0] = bswap64(*(uint64_t *) (&svga->vram[svga->hwcursor_latch.addr + 8]));
    svga->hwcursor_latch.addr += 16;

    for (uint8_t x = 0; x < 64; x++) {
        if (!(dat[1] & (1ULL << 63)))
            svga->monitor->target_buffer->line[displine][(offset + svga->x_add) & 2047] = (dat[0] & (1ULL << 63)) ? svga_lookup_lut_ram(svga, chips->cursor_pallook[5]) : svga_lookup_lut_ram(svga, chips->cursor_pallook[4]);
        else if (dat[0] & (1ULL << 63))
            svga->monitor->target_buffer->line[displine][(offset + svga->x_add) & 2047] ^= 0xffffff;

        offset++;
        dat[0] <<= 1;
        dat[1] <<= 1;
    }

    if (svga->interlace && !svga->hwcursor_oddeven)
        svga->hwcursor_latch.addr += 16;
}

static void
chips_69000_hwcursor_draw(svga_t *svga, int displine)
{
    chips_69000_t    *chips  = (chips_69000_t *) svga->priv;
    uint64_t          dat[2];
    int               offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;

    if ((chips->ext_regs[0xa0] & 7) == 0b101)
        return chips_69000_hwcursor_draw_64x64(svga, displine);

    if (svga->interlace) {
        dat[1] = bswap64(*(uint64_t *) (&svga->vram[svga->hwcursor_latch.addr]));
        dat[0] = bswap64(*(uint64_t *) (&svga->vram[svga->hwcursor_latch.addr + 8]));
        svga->hwcursor_latch.addr += 16;
        if (svga->hwcursor_oddeven) {
            dat[1] <<= 32ULL;
            dat[0] <<= 32ULL;
        }
        for (uint8_t x = 0; x < 32; x++) {
            if (!(dat[1] & (1ULL << 63)))
                svga->monitor->target_buffer->line[displine & 2047][(offset + svga->x_add) & 2047] = (dat[0] & (1ULL << 63)) ? svga_lookup_lut_ram(svga, chips->cursor_pallook[5]) : svga_lookup_lut_ram(svga, chips->cursor_pallook[4]);
            else if (dat[0] & (1ULL << 63))
                svga->monitor->target_buffer->line[displine & 2047][(offset + svga->x_add) & 2047] ^= 0xffffff;

            offset++;
            dat[0] <<= 1;
            dat[1] <<= 1;
        }
        return;
    }

    if ((svga->hwcursor_on & 1)) {
        dat[1] = bswap64(*(uint64_t *) (&svga->vram[svga->hwcursor_latch.addr - 16]));
        dat[0] = bswap64(*(uint64_t *) (&svga->vram[(svga->hwcursor_latch.addr - 16) + 8]));
        dat[1] <<= 32ULL;
        dat[0] <<= 32ULL;
    }
    else {
        dat[1] = bswap64(*(uint64_t *) (&svga->vram[svga->hwcursor_latch.addr]));
        dat[0] = bswap64(*(uint64_t *) (&svga->vram[svga->hwcursor_latch.addr + 8]));
        svga->hwcursor_latch.addr += 16;
    }

    for (uint8_t x = 0; x < 32; x++) {
        if (!(dat[1] & (1ULL << 63)))
            svga->monitor->target_buffer->line[displine & 2047][(offset + svga->x_add) & 2047] = (dat[0] & (1ULL << 63)) ? svga_lookup_lut_ram(svga, chips->cursor_pallook[5]) : svga_lookup_lut_ram(svga, chips->cursor_pallook[4]);
        else if (dat[0] & (1ULL << 63))
            svga->monitor->target_buffer->line[displine & 2047][(offset + svga->x_add) & 2047] ^= 0xffffff;

        offset++;
        dat[0] <<= 1;
        dat[1] <<= 1;
    }
}

static float
chips_69000_getclock(int clock, void *priv)
{
    const chips_69000_t *chips = (chips_69000_t *) priv;

    if (clock == 0)
        return 25175000.0;
    if (clock == 1)
        return 28322000.0;

    int m  = chips->ext_regs[0xc8];
    int n  = chips->ext_regs[0xc9];
    int pl = ((chips->ext_regs[0xcb] >> 4) & 7);

    float fvco = 14318181.0 * ((float)(m + 2) / (float)(n + 2));
    if (chips->ext_regs[0xcb] & 4)
        fvco *= 4.0;
    float fo   = fvco / (float)(1 << pl);

    return fo;
}

uint32_t
chips_69000_conv_16to32(svga_t* svga, uint16_t color, uint8_t bpp)
{
    uint32_t ret = 0x00000000;

    if (svga->lut_map) {
        if (bpp == 15) {
            uint8_t b = getcolr(svga->pallook[(color & 0x1f) << 3]);
            uint8_t g = getcolg(svga->pallook[(color & 0x3e0) >> 2]);
            uint8_t r = getcolb(svga->pallook[(color & 0x7c00) >> 7]);
            ret = (video_15to32[color] & 0xFF000000) | makecol(r, g, b);
        } else {
            uint8_t b = getcolr(svga->pallook[(color & 0x1f) << 3]);
            uint8_t g = getcolg(svga->pallook[(color & 0x7e0) >> 3]);
            uint8_t r = getcolb(svga->pallook[(color & 0xf800) >> 8]);
            ret = (video_16to32[color] & 0xFF000000) | makecol(r, g, b);
        }
    } else
        ret = (bpp == 15) ? video_15to32[color] : video_16to32[color];

    return ret;
}

static int
chips_69000_line_compare(svga_t* svga)
{
    const chips_69000_t *chips = (chips_69000_t *) svga->priv;
    if (chips->ext_regs[0x81] & 0xF) {
        return 0;
    }

    return 1;
}

static void
chips_69000_disable_handlers(chips_69000_t *chips)
{
    io_removehandler(0x03c0, 0x0020, chips_69000_in, NULL, NULL, chips_69000_out, NULL, NULL, chips);

    mem_mapping_disable(&chips->linear_mapping);
    mem_mapping_disable(&chips->svga.mapping);
    if (!chips->on_board)
        mem_mapping_disable(&chips->bios_rom.mapping);

    /* Save all the mappings and the timers because they are part of linked lists. */
    reset_state->linear_mapping   = chips->linear_mapping;
    reset_state->svga.mapping     = chips->svga.mapping;
    reset_state->bios_rom.mapping = chips->bios_rom.mapping;

    reset_state->decrement_timer  = chips->decrement_timer;
    reset_state->svga.timer       = chips->svga.timer;
    reset_state->svga.timer8514   = chips->svga.timer8514;
}

static void
chips_69000_reset(void *priv)
{
    chips_69000_t *chips = (chips_69000_t *) priv;

    if (reset_state != NULL) {
        chips_69000_disable_handlers(chips);
        reset_state->slot = chips->slot;

        *chips = *reset_state;
    }
}

static void *
chips_69000_init(const device_t *info)
{
    chips_69000_t *chips = calloc(1, sizeof(chips_69000_t));
    reset_state = calloc(1, sizeof(chips_69000_t));

    /* Appears to have an odd VBIOS size. */
    if (!info->local) {
        rom_init(&chips->bios_rom, "roms/video/chips/69000.ROM", 0xc0000, 0x10000, 0xffff, 0x0000, MEM_MAPPING_EXTERNAL);
        mem_mapping_disable(&chips->bios_rom.mapping);
    }

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_chips);

    svga_init(info, &chips->svga, chips, 1 << 21, /*2048kb*/
              chips_69000_recalctimings,
              chips_69000_in, chips_69000_out,
              chips_69000_hwcursor_draw,
              NULL);

    io_sethandler(0x03c0, 0x0020, chips_69000_in, NULL, NULL, chips_69000_out, NULL, NULL, chips);

    pci_add_card(info->local ? PCI_ADD_VIDEO : PCI_ADD_NORMAL, chips_69000_pci_read, chips_69000_pci_write, chips, &chips->slot);

    chips->svga.bpp              = 8;
    chips->svga.miscout          = 1;
    chips->svga.vblank_start     = chips_69000_vblank_start;
    chips->svga.getclock         = chips_69000_getclock;
    chips->svga.conv_16to32      = chips_69000_conv_16to32;
    chips->svga.line_compare     = chips_69000_line_compare;

    mem_mapping_add(&chips->linear_mapping, 0, 0, chips_69000_readb_linear, chips_69000_readw_linear, chips_69000_readl_linear, chips_69000_writeb_linear, chips_69000_writew_linear, chips_69000_writel_linear, NULL, MEM_MAPPING_EXTERNAL, chips);

    chips->quit            = 0;
    chips->engine_active   = 0;
    chips->on_board        = !!(info->local);

    chips->svga.packed_chain4 = 1;

    timer_add(&chips->decrement_timer, chips_69000_decrement_timer, chips, 0);
    timer_on_auto(&chips->decrement_timer, 1000000. / 2000.);

    chips->i2c = i2c_gpio_init("ddc_chips_69000");
    chips->ddc = ddc_init(i2c_gpio_get_bus(chips->i2c));

    chips->flat_panel_regs[0x01] = 1;

    *reset_state = *chips;

    return chips;
}

static int
chips_69000_available(void)
{
    return rom_present("roms/video/chips/69000.ROM");
}

void
chips_69000_close(void *priv)
{
    chips_69000_t *chips = (chips_69000_t *) priv;

    chips->quit = 1;
//    thread_set_event(chips->fifo_event);
 //   thread_wait(chips->accel_thread);
    ddc_close(chips->ddc);
    i2c_gpio_close(chips->i2c);
    svga_close(&chips->svga);

    free(reset_state);
    reset_state = NULL;

    free(chips);
}

void
chips_69000_speed_changed(void *priv)
{
    chips_69000_t *chips = (chips_69000_t *) priv;

    svga_recalctimings(&chips->svga);
}

void
chips_69000_force_redraw(void *priv)
{
    chips_69000_t *chips = (chips_69000_t *) priv;

    chips->svga.fullchange = chips->svga.monitor->mon_changeframecount;
}

const device_t chips_69000_device = {
    .name          = "Chips & Technologies B69000",
    .internal_name = "chips_69000",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = chips_69000_init,
    .close         = chips_69000_close,
    .reset         = chips_69000_reset,
    .available     = chips_69000_available,
    .speed_changed = chips_69000_speed_changed,
    .force_redraw  = chips_69000_force_redraw,
    .config        = NULL
};

const device_t chips_69000_onboard_device = {
    .name          = "Chips & Technologies B69000 On-Board",
    .internal_name = "chips_69000_onboard",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = chips_69000_init,
    .close         = chips_69000_close,
    .reset         = chips_69000_reset,
    .available     = chips_69000_available,
    .speed_changed = chips_69000_speed_changed,
    .force_redraw  = chips_69000_force_redraw,
    .config        = NULL
};
