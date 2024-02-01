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
    atomic_bool   engine_active;
    atomic_bool   quit;
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
        uint32_t x, y;
        int x_dir, y_dir;

        uint8_t bytes_per_pixel;
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

    void* i2c_ddc, *ddc;

    uint8_t st01;
} chips_69000_t;

static video_timings_t timing_sis = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 4, .read_b = 20, .read_w = 20, .read_l = 35 };

uint8_t chips_69000_readb_linear(uint32_t addr, void *p);
uint16_t chips_69000_readw_linear(uint32_t addr, void *p);
uint32_t chips_69000_readl_linear(uint32_t addr, void *p);
void chips_69000_writeb_linear(uint32_t addr, uint8_t val, void *p);
void chips_69000_writew_linear(uint32_t addr, uint16_t val, void *p);
void chips_69000_writel_linear(uint32_t addr, uint32_t val, void *p);

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
            /* Report no presence of flat panel module. */
            return 0;
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
            *dst = 0xFF;
            break;
    }
}

void
chips_69000_do_rop_24bpp(uint32_t *dst, uint32_t src, uint8_t rop)
{
    uint32_t orig_dst = *dst & 0xFF000000;
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
    *dst &= 0xFFFFFF;
    *dst |= orig_dst;
}

void
chips_69000_do_rop_8bpp_patterned(uint8_t *dst, uint8_t src, uint8_t nonpattern_src, uint8_t rop)
{
    if ((rop & 0xF) == ((rop >> 4) & 0xF)) {
        return chips_69000_do_rop_8bpp(dst, nonpattern_src, rop);
    }

    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x05:
            *dst = ~(*dst) & ~src;
            break;
        case 0x0A:
            *dst &= ~src;
            break;
        case 0x0F:
            *dst = ~src;
            break;
        case 0x50:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x5A:
            *dst ^= src;
            break;
        case 0x5F:
            *dst = ~src | ~(*dst);
            break;
        case 0xB8:
            *dst = (((src ^ *dst) & nonpattern_src) ^ src);
            break;
        case 0xA0:
            *dst &= src;
            break;
        case 0xA5:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xAF:
            *dst |= ~src;
            break;
        case 0xF0:
            *dst = src;
            break;
        case 0xF5:
            *dst = src | ~(*dst);
            break;
        case 0xFA:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
}

void
chips_69000_do_rop_16bpp_patterned(uint16_t *dst, uint16_t src, uint8_t nonpattern_src, uint8_t rop)
{
    if ((rop & 0xF) == ((rop >> 4) & 0xF)) {
        return chips_69000_do_rop_16bpp(dst, nonpattern_src, rop);
    }

    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x05:
            *dst = ~(*dst) & ~src;
            break;
        case 0x0A:
            *dst &= ~src;
            break;
        case 0x0F:
            *dst = ~src;
            break;
        case 0x50:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x5A:
            *dst ^= src;
            break;
        case 0x5F:
            *dst = ~src | ~(*dst);
            break;
        case 0xB8:
            *dst = (((src ^ *dst) & nonpattern_src) ^ src);
            break;
        case 0xA0:
            *dst &= src;
            break;
        case 0xA5:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xAF:
            *dst |= ~src;
            break;
        case 0xF0:
            *dst = src;
            break;
        case 0xF5:
            *dst = src | ~(*dst);
            break;
        case 0xFA:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
}

void
chips_69000_do_rop_24bpp_patterned(uint32_t *dst, uint32_t src, uint8_t nonpattern_src, uint8_t rop)
{
    uint32_t orig_dst = *dst & 0xFF000000;

    if ((rop & 0xF) == ((rop >> 4) & 0xF)) {
        return chips_69000_do_rop_24bpp(dst, nonpattern_src, rop);
    }

    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x05:
            *dst = ~(*dst) & ~src;
            break;
        case 0x0A:
            *dst &= ~src;
            break;
        case 0x0F:
            *dst = ~src;
            break;
        case 0x50:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x5A:
            *dst ^= src;
            break;
        case 0x5F:
            *dst = ~src | ~(*dst);
            break;
        case 0xB8:
            *dst = (((src ^ *dst) & nonpattern_src) ^ src);
            break;
        case 0xA0:
            *dst &= src;
            break;
        case 0xA5:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xAF:
            *dst |= ~src;
            break;
        case 0xF0:
            *dst = src;
            break;
        case 0xF5:
            *dst = src | ~(*dst);
            break;
        case 0xFA:
            *dst |= src;
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

    if (chips->ext_regs[0x81] & 0x10) {
        svga->htotal -= 5;
    }

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
        svga->hblank_end_len = 0x100;

        svga->ma_latch |= (svga->crtc[0x40] & 0xF) << 16;
        svga->rowoffset |= (svga->crtc[0x41] & 0xF) << 8;

        svga->interlace = !!(svga->crtc[0x70] & 0x80);

        switch (chips->ext_regs[0x81] & 0xF) {
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
    uint8_t pattern_data_8bpp[8][8];
    uint16_t pattern_data_16bpp[8][8];
    uint32_t pattern_pixel = 0;
    uint32_t dest_pixel = 0;
    uint32_t dest_addr = chips->bitblt_running.bitblt.destination_addr + (chips->bitblt_running.y * chips->bitblt.destination_span) + (chips->bitblt_running.x * chips->bitblt_running.bytes_per_pixel);
    uint8_t vert_pat_alignment = (chips->bitblt_running.bitblt.bitblt_control >> 20) & 7;

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

    /* TODO: Find out from where it actually pulls the exact pattern x and y values. */
    if (chips->bitblt_running.bitblt.bitblt_control & (1 << 18)) {
        uint8_t is_true = 0;
        if (chips->bitblt_running.bitblt.bitblt_control & (1 << 19))
            pattern_data = 0;
        else
            pattern_data = chips_69000_readb_linear(chips->bitblt_running.bitblt.pat_addr + (vert_pat_alignment + chips->bitblt_running.count_y), chips);

        is_true = !!(pattern_data & (1 << (((chips->bitblt_running.bitblt.destination_addr & 7) + chips->bitblt_running.count_x) & 7)));

        if (!is_true && (chips->bitblt_running.bitblt.bitblt_control & (1 << 17))) {
            return;
        }

        pattern_pixel = is_true ? pattern_fg : pattern_bg;

        pattern_pixel &= (1 << (8 * (chips->bitblt_running.bytes_per_pixel))) - 1;
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
chips_69000_setup_bitblt(chips_69000_t* chips)
{
    chips->engine_active = 1;
    chips->bitblt_running.bitblt = chips->bitblt;
    chips->bitblt_running.actual_source_height = chips->bitblt.destination_height;
    chips->bitblt_running.actual_destination_height = chips->bitblt.destination_height;
    chips->bitblt_running.count_x = chips->bitblt_running.count_y = 0;

    if (chips->bitblt.bitblt_control & (1 << 23)) {
        chips->bitblt_running.bytes_per_pixel = 1 + ((chips->bitblt.bitblt_control >> 24) & 3);
    } else {
        chips->bitblt_running.bytes_per_pixel = 1 + ((chips->ext_regs[0x20] >> 4) & 3);
    }

    chips->bitblt_running.actual_destination_width = chips->bitblt_running.bitblt.destination_width / chips->bitblt_running.bytes_per_pixel;

    switch ((chips->bitblt.bitblt_control >> 8) & 3) {
        case 0:
            chips->bitblt_running.x = 0;
            chips->bitblt_running.y = 0;
            chips->bitblt_running.x_dir = 1;
            chips->bitblt_running.y_dir = 1;
            break;
        case 1:
            chips->bitblt_running.x = chips->bitblt_running.actual_destination_width - 1;
            chips->bitblt_running.y = 0;
            chips->bitblt_running.x_dir = -1;
            chips->bitblt_running.y_dir = 1;
            break;
        case 2:
            chips->bitblt_running.x = 0;
            chips->bitblt_running.y = chips->bitblt_running.bitblt.destination_height - 1;
            chips->bitblt_running.x_dir = 1;
            chips->bitblt_running.y_dir = -1;
            break;
        case 3:
            chips->bitblt_running.x = chips->bitblt_running.actual_destination_width - 1;
            chips->bitblt_running.y = chips->bitblt_running.bitblt.destination_height - 1;
            chips->bitblt_running.x_dir = -1;
            chips->bitblt_running.y_dir = -1;
            break;
    }

    if (chips->bitblt_running.bitblt.bitblt_control & (1 << 10)) {
        return;
    }

    /* Drawing is pointless if monochrome pattern is enabled, monochrome write-masking is enabled and solid pattern is enabled. */
    if ((chips->bitblt_running.bitblt.bitblt_control & (1 << 17))
        && (chips->bitblt_running.bitblt.bitblt_control & (1 << 18))
        && (chips->bitblt_running.bitblt.bitblt_control & (1 << 19))) {
            chips_69000_bitblt_interrupt(chips);
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
    } while ((++chips->bitblt_running.count_y) < chips->bitblt_running.actual_destination_height);
    
    chips_69000_bitblt_interrupt(chips);
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
                    val = (val & ~8) | (i2c_gpio_get_scl(chips->i2c_ddc) << 3);
                
                if (!(chips->ext_regs[0x62] & 0x4))
                    val = (val & ~4) | (i2c_gpio_get_sda(chips->i2c_ddc) << 2);

                break;
            }
        case 0x70:
            val = 0x3;
            break;
        case 0x71:
            val = 0x0;
            break;
    }
    // if (chips->ext_index != 0x4E && chips->ext_index != 0x4F
    // && (chips->ext_index < 0xE0 || chips->ext_index > 0xEB))
    //     pclog("C&T: Read ext reg 0x%02X, ret = 0x%02X\n", index, val);
    return val;
}

void
chips_69000_write_ext_reg(chips_69000_t* chips, uint8_t val)
{
    // if (chips->ext_index != 0x4E && chips->ext_index != 0x4F
    // && (chips->ext_index < 0xE0 || chips->ext_index > 0xEB))
    //     pclog("C&T: Write ext reg 0x%02X, ret = 0x%02X\n", chips->ext_index, val);
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
                    scl = i2c_gpio_get_scl(chips->i2c_ddc);
                
                if (chips->ext_regs[0x62] & 0x4)
                    sda = !!(val & 4);
                else
                    scl = i2c_gpio_get_sda(chips->i2c_ddc);
                
                i2c_gpio_set(chips->i2c_ddc, scl, sda);

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
        case 0xD2:
            break;
        default:
            chips->ext_regs[chips->ext_index] = val;
            break;
    }
}

void
chips_69000_out(uint16_t addr, uint8_t val, void *p)
{
    chips_69000_t  *chips  = (chips_69000_t *) p;
    svga_t *svga = &chips->svga;
    uint8_t old, index;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    // if (!(addr == 0x3CC || addr == 0x3C9)) pclog("SiS SVGA out: 0x%X, 0x%X\n", addr, val);
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
#if 1
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
#endif
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
        case 0x3D6:
            chips->ext_index = val;
            return;
        case 0x3D7:
            return chips_69000_write_ext_reg(chips, val);
        
    }
    svga_out(addr, val, svga);
}

uint8_t
chips_69000_in(uint16_t addr, void *p)
{
    chips_69000_t  *chips  = (chips_69000_t *) p;
    svga_t *svga = &chips->svga;
    uint8_t temp = 0, index;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    // if (!(addr == 0x3CC || addr == 0x3C9)) pclog("SiS SVGA in: 0x%X\n", addr);
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
        case 0x3D6:
            temp = chips->ext_index;
            break;
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
chips_69000_pci_read(int func, int addr, void *p)
{
    chips_69000_t *chips = (chips_69000_t *) p;

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
                return chips->pci_conf_status;
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
chips_69000_pci_write(int func, int addr, uint8_t val, void *p)
{
    chips_69000_t *chips = (chips_69000_t *) p;

    {
        switch (addr) {
            case 0x04:
                {
                    chips->pci_conf_status = val;
                    io_removehandler(0x03c0, 0x0020, chips_69000_in, NULL, NULL, chips_69000_out, NULL, NULL, chips);
                    mem_mapping_disable(&chips->bios_rom.mapping);
                    mem_mapping_disable(&chips->linear_mapping);
                    mem_mapping_disable(&chips->svga.mapping);
                    if (chips->pci_conf_status & PCI_COMMAND_IO) {
                        io_sethandler(0x03c0, 0x0020, chips_69000_in, NULL, NULL, chips_69000_out, NULL, NULL, chips);
                    }
                    if (chips->pci_conf_status & PCI_COMMAND_MEM) {
                        if (!chips->on_board) mem_mapping_enable(&chips->bios_rom.mapping);
                        mem_mapping_enable(&chips->svga.mapping);
                        if (chips->linear_mapping.base)
                            mem_mapping_enable(&chips->linear_mapping);
                    }
                    break;
                }
            case 0x13:
                {
                    if (!(chips->pci_conf_status & PCI_COMMAND_MEM)) {
                        chips->linear_mapping.base = val << 24;
                        break;
                    }
                    mem_mapping_set_addr(&chips->linear_mapping, val << 24, (1 << 24));
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
                    mem_mapping_set_addr(&chips->bios_rom.mapping, chips->rom_addr << 16, 0x40000);
                }
                break;
            case 0x32:
                if (chips->on_board) break;
                chips->rom_addr &= ~0xFF;
                chips->rom_addr |= val & 0xFC;
                if (chips->pci_rom_enable & 1) {
                    mem_mapping_set_addr(&chips->bios_rom.mapping, chips->rom_addr << 16, 0x40000);
                }
                break;
            case 0x33:
                if (chips->on_board) break;
                chips->rom_addr &= ~0xFF00;
                chips->rom_addr |= (val << 8);
                if (chips->pci_rom_enable & 1) {
                    mem_mapping_set_addr(&chips->bios_rom.mapping, chips->rom_addr << 16, 0x40000);
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
    //pclog("C&T Read 0x%X\n", addr);
    addr &= 0xFFF;
    switch (addr & 0xFFF) {
        case 0x00 ... 0x28:
            if (addr == 0x10) {
                return (chips->bitblt_regs_b[addr & 0xFF] & 0x7F) | (chips->engine_active ? 0x80 : 0x00);
            }
            return chips->bitblt_regs_b[addr & 0xFF];
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
    return 0xFF;
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
    addr &= 0xFFF;
    switch (addr & 0xFFF) {
        case 0x00 ... 0x28:
            chips->bitblt_regs_b[addr & 0xFF] = val;
            if ((addr & 0xFFF) == 0x023 && chips->bitblt_regs[0x8] != 0) {
                uint8_t cntr = 0;
                pclog("BitBLT/Draw operation %hd\n", (uint8_t)cntr++);
                chips_69000_setup_bitblt(chips);
            }
            break;
        case 0x600 ... 0x60F:
            switch (addr & 0xFFF)
            {
                case 0x600 ... 0x603:
                {
                    chips->mem_regs_b[addr & 0xF] = val;
                    chips->mem_regs[addr >> 2] &= 0x80004040;
                    if (addr == 0x605 || addr == 0x607)
                        chips_69000_interrupt(chips);
                    break;
                }

                case 0x604 ... 0x607:
                {
                    chips->mem_regs_b[addr & 0xF] &= ~val;
                    chips->mem_regs[addr >> 2] &= 0x80004040;
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
    addr &= 0xFFF;
    switch (addr & 0xFFF) {
        default:
            chips_69000_writew_mmio(addr, val, chips);
            chips_69000_writew_mmio(addr + 2, val >> 16, chips);
            break;
    }
}

uint8_t
chips_69000_readb_linear(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *) p;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x400000)
        return chips_69000_readb_mmio(addr, chips);

    return svga_readb_linear(addr & 0x1FFFFF, p);
}

uint16_t
chips_69000_readw_linear(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *) p;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x400000)
        return chips_69000_readw_mmio(addr, chips);

    return svga_readw_linear(addr & 0x1FFFFF, p);
}

uint32_t
chips_69000_readl_linear(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *) p;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x400000)
        return chips_69000_readl_mmio(addr, chips);

    return svga_readl_linear(addr & 0x1FFFFF, p);
}

void
chips_69000_writeb_linear(uint32_t addr, uint8_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x400000)
        return chips_69000_writeb_mmio(addr, val, chips);

    svga_writeb_linear(addr & 0x1FFFFF, val, p);
}

void
chips_69000_writew_linear(uint32_t addr, uint16_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x400000)
        return chips_69000_writew_mmio(addr, val, chips);

    svga_writew_linear(addr & 0x1FFFFF, val, p);
}

void
chips_69000_writel_linear(uint32_t addr, uint32_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;

    if (addr & 0x400000)
        return chips_69000_writel_mmio(addr, val, chips);

    svga_writel_linear(addr & 0x1FFFFF, val, p);
}

void
chips_69000_vblank_start(svga_t *svga)
{
    chips_69000_t  *chips  = (chips_69000_t *) svga->priv;
    chips->mem_regs[1] |= 1 << 14;
    
    chips_69000_interrupt(chips);
}

static void *
chips_69000_init(const device_t *info)
{
    chips_69000_t *chips = calloc(1, sizeof(chips_69000_t));

    /* Appears to have an odd VBIOS size. */
    if (!info->local) {
        rom_init(&chips->bios_rom, "roms/video/chips/69000.ROM", 0xc0000, 0x40000, 0x3ffff, 0x0000, MEM_MAPPING_EXTERNAL);
        mem_mapping_disable(&chips->bios_rom.mapping);
    }

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_sis);

    svga_init(info, &chips->svga, chips, 1 << 21, /*2048kb*/
              chips_69000_recalctimings,
              chips_69000_in, chips_69000_out,
              NULL,
              NULL);

    io_sethandler(0x03c0, 0x0020, chips_69000_in, NULL, NULL, chips_69000_out, NULL, NULL, chips);

    pci_add_card(PCI_ADD_VIDEO, chips_69000_pci_read, chips_69000_pci_write, chips, &chips->slot);

    chips->svga.bpp              = 8;
    chips->svga.miscout          = 1;
    chips->svga.recalctimings_ex = chips_69000_recalctimings;
    chips->svga.vblank_start     = chips_69000_vblank_start;

    mem_mapping_add(&chips->linear_mapping, 0, 0, chips_69000_readb_linear, chips_69000_readw_linear, chips_69000_readl_linear, chips_69000_writeb_linear, chips_69000_writew_linear, chips_69000_writel_linear, NULL, MEM_MAPPING_EXTERNAL, chips);

    chips->quit            = 0;
    chips->engine_active   = 0;
    chips->on_board        = !!(info->local);
    
    chips->svga.packed_chain4 = 1;

    timer_add(&chips->decrement_timer, chips_69000_decrement_timer, chips, 0);
    timer_on_auto(&chips->decrement_timer, 1000000. / 2000.);

    chips->i2c_ddc = i2c_gpio_init("c&t_69000_mga");
    chips->ddc     = ddc_init(i2c_gpio_get_bus(chips->i2c_ddc));
    
    chips->flat_panel_regs[0x01] = 1;

    sizeof(struct chips_69000_bitblt_t);

    return chips;
}

static int
chips_69000_available(void)
{
    return rom_present("roms/video/chips/69000.ROM");
}

void
chips_69000_close(void *p)
{
    chips_69000_t *chips = (chips_69000_t *) p;

    chips->quit = 1;
//    thread_set_event(chips->fifo_event);
 //   thread_wait(chips->accel_thread);
    ddc_close(chips->ddc);
    i2c_gpio_close(chips->i2c_ddc);
    svga_close(&chips->svga);

    free(chips);
}

void
chips_69000_speed_changed(void *p)
{
    chips_69000_t *chips = (chips_69000_t *) p;

    svga_recalctimings(&chips->svga);
}

void
chips_69000_force_redraw(void *p)
{
    chips_69000_t *chips = (chips_69000_t *) p;

    chips->svga.fullchange = changeframecount;
}

const device_t chips_69000_device = {
    .name          = "Chips & Technologies 69000",
    .internal_name = "c&t_69000",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = chips_69000_init,
    .close         = chips_69000_close,
    .reset         = NULL,
    { .available = chips_69000_available },
    .speed_changed = chips_69000_speed_changed,
    .force_redraw  = chips_69000_force_redraw,
    .config        = NULL
};

const device_t chips_69000_onboard_device = {
    .name          = "Chips & Technologies 69000 (onboard)",
    .internal_name = "c&t_69000_onboard",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = chips_69000_init,
    .close         = chips_69000_close,
    .reset         = NULL,
    { .available = chips_69000_available },
    .speed_changed = chips_69000_speed_changed,
    .force_redraw  = chips_69000_force_redraw,
    .config        = NULL
};
