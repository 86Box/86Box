/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/i2c.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>
#include <86box/vid_vga.h>

#include "verite/core.h"

#define VERITE_ROM "roms/video/verite/phx49.rom"
#define ROM_SIZE   0x8000
#define VRAM_SIZE  0x400000
#define BAR_SIZE   0x1000000
#define TICK_US    50
#define MODE_VESA            0x01
#define MODE_VGA             0x02
#define CRTC_CTL_DDC_DRIVE   0x20
#define CRTC_CTL_DDC_SCL     0x40
#define CRTC_CTL_DDC_SDA     0x80
#define CRTC_CTL_VSYNC       0x400
#define CRTC_CTL_VIDEO       0x1000
#define CRTC_CTL_STEREO      0x2000
#define CRTC_CTL_DISPLAYED   0x4000
#define CRTC_CTL_BGR         0x8000
#define CRTC_CTL_EVEN_FRAME  0x10000
#define CRTC_CTL_LINE_DOUBLE 0x20000
#define CRTC_CTL_SWITCHED    0x40000
#define CRTC_CTL_FRAME_FLAGS (CRTC_CTL_DISPLAYED | CRTC_CTL_EVEN_FRAME | CRTC_CTL_SWITCHED)
#define CRTC_TEST_VBLANK     0x40000
#define CRTC_TEST_NOT_VBLANK 0x10000
#define DRAM_SLOW_PRECHARGE  0x0010
#define DRAM_REFRESH_VBLANK  0x2000
#define DRAM_REFRESH_DISABLE 0x8000
#define DRAM_REFRESH_COUNT   0xff0000
/* Approximate EDO refresh costs in the 25 MHz RISC clock domain. */
#define DRAM_REFRESH_UNIT_NS 320
#define DRAM_REFRESH_CYCLES  2
#define PLL_VGA_CLOCK        0x40000
#define PLL_WORD_MASK        0xfffff
#define PLL_REF_HZ           14318180.0

enum {
    CRTC_TEST   = 0x80,
    CRTC_CTL    = 0x84,
    CRTC_HORZ   = 0x88,
    CRTC_VERT   = 0x8c,
    FRAME_BASE_B = 0x90,
    FRAME_BASE_A = 0x94,
    CRTC_OFFSET = 0x98,
    CRTC_STATUS = 0x9c,
    DRAM_CTL    = 0xa0,
    RAMDAC      = 0xb0,
    PLL_DEVICE  = 0xc0
};

typedef struct {
    vga_t         vga;
    rom_t         bios;
    mem_mapping_t linear;
    mem_mapping_t banked;
    pc_timer_t    timer;
    verite_core  *core;
    void         *ramdac;
    void         *ddc_bus;
    void         *ddc_monitor;
    uint8_t       ddc1_edid[128];
    unsigned      ddc1_bit;
    int           ddc2_active;
    uint8_t       regs[256];
    uint8_t       pci_slot, irq_state, irq_line, irq_asserted, command;
    uint8_t       bar_hi, io_hi, io_installed, endian, pci_control, mode, dac_command0;
    uint32_t      rom_bar, bank, frame_base_b, frame[2], pitch[2];
    unsigned      scanout_offset;
    int           native_bpp;
    uint64_t      time_ns;
    uint64_t      dram_last_refresh_ns;
    unsigned      dram_refresh_pending;
    uint32_t      frame_flags;
    uint32_t      pll_shift;
    double        pixel_clock_hz;
} verite_t;

static const uint16_t dac_ports[] = { 0x3c8, 0x3c9, 0x3c6, 0x3c7 };

static const video_timings_t verite_timing = { VIDEO_PCI, 2, 2, 1, 20, 20, 21 };

static uint32_t
verite_reg(const verite_t *v, uint8_t reg)
{
    return v->regs[reg] | ((uint32_t) v->regs[reg + 1] << 8) | ((uint32_t) v->regs[reg + 2] << 16) | ((uint32_t) v->regs[reg + 3] << 24);
}

static void
verite_render(svga_t *svga)
{
    const verite_t *v = svga->priv;
    int             y = svga->displine + svga->y_add;
    if (y < 0 || y >= 2048 || !svga->monitor->target_buffer)
        return;

    uint32_t       ctl   = verite_reg(v, CRTC_CTL);
    /* Stereoscopic scanout uses A on even frames and B on odd frames. */
    unsigned       eye   = (ctl & CRTC_CTL_STEREO) && !(v->frame_flags & CRTC_CTL_EVEN_FRAME);
    unsigned       row   = svga->displine >> !!(ctl & CRTC_CTL_LINE_DOUBLE);
    uint32_t      *pixel = &svga->monitor->target_buffer->line[y][svga->x_add];
    uint32_t       addr  = (v->frame[eye] + v->pitch[eye] * row) & (VRAM_SIZE - 1);
    const uint8_t *ram   = svga->vram;
    int            width = svga->hdisp / svga->dots_per_clock;

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    const uint32_t *colors = v->native_bpp == 15 ? video_15to32 : video_16to32;
    if ((v->native_bpp == 15 || v->native_bpp == 16) && !(addr & 3)
        && !(width & 1) && 2U * width <= VRAM_SIZE - addr) {
        /* VRAM stores the two 16-bit pixels in each dword in reverse order. */
        for (int x = 0; x < width; x += 2) {
            const uint8_t *pair = ram + addr + 2 * x;
            pixel[x]     = colors[pair[2] | (pair[3] << 8)];
            pixel[x + 1] = colors[pair[0] | (pair[1] << 8)];
        }
    } else {
        for (int x = 0; x < width; x++) {
            uint32_t a = addr & (VRAM_SIZE - 1);
            switch (v->native_bpp) {
                case 8:
                    pixel[x] = svga->map8[ram[a ^ 3] & svga->dac_mask];
                    addr++;
                    break;
                case 15:
                case 16:
                    {
                        uint16_t color = ram[a ^ 2] | ((uint16_t) ram[((a + 1) & (VRAM_SIZE - 1)) ^ 2] << 8);
                        pixel[x]       = colors[color];
                        addr += 2;
                        break;
                    }
                default:
                    pixel[x] = ((uint32_t) ram[(a + 2) & (VRAM_SIZE - 1)] << 16) | ((uint32_t) ram[(a + 1) & (VRAM_SIZE - 1)] << 8) | ram[a];
                    addr += 4;
                    break;
            }
        }
    }

    if (v->native_bpp > 8) {
        if (ctl & CRTC_CTL_BGR) {
            for (int x = 0; x < width; x++)
                pixel[x] = makecol32(getcolb(pixel[x]), getcolg(pixel[x]), getcolr(pixel[x]));
        }
        if (svga->lut_map && v->native_bpp < 32) {
            /* The Bt485 routes the 5/6-bit components to either end of
               the palette address, filling the unused bits with zero. */
            unsigned green_shift = v->native_bpp == 16 ? 2 : 3;
            int contiguous = bt48x_clut_contiguous(v->ramdac);
            unsigned rb_address_shift = contiguous ? 0 : 3;
            unsigned g_address_shift  = contiguous ? 0 : green_shift;
            for (int x = 0; x < width; x++) {
                unsigned r = (getcolr(pixel[x]) >> 3) << rb_address_shift;
                unsigned g = (getcolg(pixel[x]) >> green_shift) << g_address_shift;
                unsigned b = (getcolb(pixel[x]) >> 3) << rb_address_shift;
                pixel[x] = svga_lookup_lut_ram(svga, makecol32(r, g, b));
            }
        } else if (svga->lut_map) {
            for (int x = 0; x < width; x++)
                pixel[x] = svga_lookup_lut_ram(svga, pixel[x]);
        }
    }

    if (svga->dots_per_clock == 2) {
        /* Expand from right to left so the source pixels are still available. */
        for (int x = width - 1; x >= 0; x--)
            pixel[2 * x] = pixel[2 * x + 1] = pixel[x];
    }
}

static void
verite_update_pitch(verite_t *v, unsigned width, unsigned bpp, uint32_t ctl)
{
    unsigned bytes = width * ((bpp + 7) / 8);
    unsigned fifo  = (ctl & 0x10) ? 128 : 64;

    for (unsigned eye = 0; eye < 2; eye++) {
        unsigned pitch = (bytes - !(v->frame[eye] & 7)) / fifo * fifo + v->scanout_offset;
        v->pitch[eye]  = MAX(pitch, bytes);
    }
}

static void
verite_recalctimings(svga_t *svga)
{
    verite_t *v   = svga->priv;
    uint32_t  ctl = verite_reg(v, CRTC_CTL);
    int       native = (ctl & CRTC_CTL_VIDEO) && !(v->mode & MODE_VGA);

    svga_set_ramdac_type(svga, native && (v->dac_command0 & 2) ? RAMDAC_8BIT : RAMDAC_6BIT);
    svga->dac_hwcursor_draw = native ? bt48x_hwcursor_draw : NULL;
    svga->lut_map           = 0;
    svga->hoverride         = 0;
    if (!native)
        return;

    int bpp;
    switch (ctl & 15) {
        case 1:
        case 2:
            bpp = 8;
            break;
        case 4:
            bpp = 16;
            break;
        case 6:
            bpp = 15;
            break;
        case 12:
            bpp = 32;
            break;
        default:
            return;
    }

    uint32_t horz  = verite_reg(v, CRTC_HORZ);
    uint32_t vert  = verite_reg(v, CRTC_VERT);
    int      width = ((horz & 0xff) + 1) * 8;
    int      total = width + 8 * (3 + ((horz >> 9) & 63) + ((horz >> 16) & 31) + ((horz >> 21) & 7));
    int      lines = (vert & 0x7ff) + 1;
    int      front = ((vert >> 20) & 63) + 1;
    int      sync  = ((vert >> 17) & 7) + 1;
    int      back  = ((vert >> 11) & 63) + 1;

    verite_update_pitch(v, width, bpp, ctl);

    v->native_bpp                   = bpp;
    svga->lut_map                  = bpp > 8 && !bt48x_clut_bypass(v->ramdac);
    svga->render                   = verite_render;
    svga->hoverride                = 1;
    /* Low-resolution scanout repeats rows and needs matching horizontal
       expansion in the host buffer. Keep CRTC timing and pitch in source units. */
    svga->dots_per_clock           = (ctl & CRTC_CTL_LINE_DOUBLE) && width <= 1024 ? 2 : 1;
    svga->hdisp                   = width * svga->dots_per_clock;
    svga->hdisp_time              = width;
    svga->htotal                   = total;
    svga->hblankstart              = width - 1;
    svga->hblank_end_val           = total - 1;
    svga->hblank_end_mask          = 0xfff;
    svga->char_width               = 1;
    svga->dispend = svga->vblankstart = lines;
    svga->vsyncstart                  = lines + front;
    svga->vtotal                      = lines + front + sync + back;
    /* Without a programmed PLL clock, use the VGA clock selected above. */
    if (v->pixel_clock_hz)
        svga->clock = (cpuclock * (double) (1ULL << 32)) / v->pixel_clock_hz;
    if (bt48x_clock_doubled(v->ramdac))
        svga->clock *= 0.5;
    /* Cancel the VGA sequencer's dot clock divide when using native timings. */
    if (svga->seqregs[1] & 8)
        svga->clock *= 0.5;
    /* Native vertical timing counts field lines. The SVGA poller weaves them. */
    svga->interlace                = bt48x_interlaced(v->ramdac);
    svga->linedbl = svga->rowcount = 0;
    svga->split                   = 0x1000;
    svga->monitor->mon_overscan_x = svga->monitor->mon_overscan_y = 0;
}

static void
verite_redraw(void *priv)
{
    verite_t *v            = priv;
    v->vga.svga.fullchange = changeframecount;
}

static uint32_t
verite_crtc_status(verite_t *v)
{
    svga_t *svga   = &v->vga.svga;
    int     native = svga->render == verite_render;
    int     line   = svga->vc;
    int     phase, end;

    if (line < svga->dispend) {
        phase = 0;
        end   = svga->dispend;
    } else if (line < svga->vsyncstart) {
        phase = 1;
        end   = svga->vsyncstart;
    } else {
        int sync_lines = native ? ((verite_reg(v, CRTC_VERT) >> 17) & 7) + 1
                                : ((svga->crtc[0x11] - svga->vsyncstart - 1) & 15) + 1;
        end   = svga->vsyncstart + sync_lines;
        phase = 3;
        if (line >= end) {
            phase = 2;
            end   = svga->vtotal;
        }
    }

    unsigned horizontal = !!svga->linepos;
    uint64_t remaining  = timer_get_remaining_u64(&svga->timer);
    uint64_t duration   = horizontal ? svga->dispofftime : svga->dispontime;
    unsigned clocks     = horizontal ? MAX(0, svga->htotal - svga->hdisp_time) : svga->hdisp_time;
    if (native)
        clocks /= 8;
    if (duration) {
        remaining = MIN(remaining, duration);
        clocks = (unsigned) (((uint128_t) remaining * clocks + duration - 1) / duration);
    }
    if (horizontal && native) {
        uint32_t horz      = verite_reg(v, CRTC_HORZ);
        unsigned back      = ((horz >> 9) & 63) + 1;
        unsigned sync      = ((horz >> 16) & 31) + 1;
        if (clocks <= back)
            horizontal = 2;
        else if (clocks <= back + sync)
            horizontal = 3;
        if (horizontal == 1)
            clocks -= back + sync;
        else if (horizontal == 3)
            clocks -= back;
    }

    /* Phase and clocks remaining in each direction. */
    unsigned lines_left = end > line ? end - line : 0;
    return ((uint32_t) phase << 22) | (MIN(lines_left, 0x7ffU) << 11)
           | (horizontal << 9) | MIN(clocks, 0xffU);
}

static int
verite_ddc_data(verite_t *v)
{
    uint8_t ctl = v->regs[CRTC_CTL];
    if (!(ctl & CRTC_CTL_DDC_DRIVE))
        return i2c_gpio_get_sda(v->ddc_bus);
    if (v->ddc2_active)
        return !!(ctl & CRTC_CTL_DDC_SDA);

    /* DDC1 sends the base EDID MSB first, with a zero after each byte. */
    unsigned bit = v->ddc1_bit % 9;
    return bit < 8 && (v->ddc1_edid[v->ddc1_bit / 9] & (0x80 >> bit));
}

static void
verite_ddc_drive(verite_t *v, uint8_t previous)
{
    uint8_t ctl = v->regs[CRTC_CTL];
    if ((previous ^ ctl) & CRTC_CTL_DDC_SCL)
        v->ddc2_active = 1;
    /* The direction bit releases SDA only. The BIOS still clocks SCL
       while reading data from the monitor. */
    i2c_gpio_set(v->ddc_bus, !!(ctl & CRTC_CTL_DDC_SCL),
                 !(ctl & CRTC_CTL_DDC_DRIVE) || !!(ctl & CRTC_CTL_DDC_SDA));
}

static void
verite_latch_pixel_clock(verite_t *v)
{
    uint32_t word = v->pll_shift;
    unsigned m    = ((word >> 10) & 0xff) + 2;
    unsigned n    = (word & 0xff) + 2;
    unsigned p    = (word >> 8) & 3;
    double   hz   = (word & PLL_VGA_CLOCK) ? 0.0 : 2.0 * PLL_REF_HZ * m / (n << p);

    if (hz != v->pixel_clock_hz) {
        v->pixel_clock_hz = hz;
        svga_recalctimings(&v->vga.svga);
    }
}

static uint32_t
verite_ext_read(void *priv, uint8_t reg, uint8_t len)
{
    verite_t *v      = priv;
    /* Reading the PLL port transfers the serial word to the clock generator. */
    if (reg <= PLL_DEVICE && reg + len > PLL_DEVICE)
        verite_latch_pixel_clock(v);
    svga_t   *svga   = &v->vga.svga;
    uint32_t  status = (reg < CRTC_STATUS + 4 && reg + len > CRTC_STATUS) ? verite_crtc_status(v) : 0;
    uint32_t  test   = (verite_reg(v, CRTC_TEST) & ~(CRTC_TEST_VBLANK | CRTC_TEST_NOT_VBLANK))
                       | (svga->vc >= svga->vblankstart ? CRTC_TEST_VBLANK : CRTC_TEST_NOT_VBLANK);

    uint32_t value = 0;
    for (unsigned i = 0; i < len; i++) {
        uint8_t r    = reg + i;
        uint8_t byte = v->regs[r];
        if (r >= RAMDAC && r < RAMDAC + 16)
            byte = bt48x_ramdac_in(dac_ports[r & 3], r & 4, r & 8, v->ramdac, svga);
        else if (r >= CRTC_STATUS && r < CRTC_STATUS + 4)
            byte = status >> (8 * (r - CRTC_STATUS));
        else if (r >= CRTC_TEST && r < CRTC_TEST + 4)
            byte = test >> (8 * (r - CRTC_TEST));
        else if (r >= FRAME_BASE_B && r < FRAME_BASE_A)
            byte = v->frame_base_b >> (8 * (r - FRAME_BASE_B));
        else if (r == CRTC_CTL)
            byte = (byte & ~CRTC_CTL_DDC_SDA) | (verite_ddc_data(v) ? CRTC_CTL_DDC_SDA : 0);
        else if (r > CRTC_CTL && r < CRTC_CTL + 4) {
            unsigned shift = 8 * (r - CRTC_CTL);
            byte = (byte & ~(CRTC_CTL_FRAME_FLAGS >> shift)) | (v->frame_flags >> shift);
        }
        value |= (uint32_t) byte << (8 * i);
    }
    return value;
}

static void
verite_dac_write(verite_t *v, uint16_t port, int rs2, int rs3, uint8_t value)
{
    svga_t *svga     = &v->vga.svga;
    uint8_t gdc_mode = svga->gdcreg[5];
    int     dac_type = svga->ramdac_type;
    int     command0 = port == 0x3c6 && rs2 && !rs3;

    if (command0)
        v->dac_command0 = value;
    bt48x_ramdac_out(port, rs2, rs3, value, v->ramdac, svga);
    if (command0)
        svga->ramdac_type = dac_type;

    /* Native pixel formats come from CRTC_CTL. Keep the VGA mode registers
       independent of changes to the RAMDAC's native pixel format. */
    if (command0 || svga->bpp != 8 || svga->gdcreg[5] != gdc_mode) {
        svga->bpp       = 8;
        svga->gdcreg[5] = gdc_mode;
        svga_recalctimings(svga);
    }
}

static void
verite_ext_write(void *priv, uint8_t reg, uint32_t value, uint8_t len)
{
    verite_t *v = priv;
    uint8_t   previous_ctl = v->regs[CRTC_CTL];
    uint32_t  previous_dram = verite_reg(v, DRAM_CTL);
    for (unsigned i = 0; i < len; i++) {
        uint8_t r    = reg + i;
        uint8_t byte = value >> (8 * i);
        if (r >= RAMDAC && r < RAMDAC + 16) {
            verite_dac_write(v, dac_ports[r & 3], r & 4, r & 8, byte);
        } else {
            v->regs[r] = byte;
            /* The guest clocks one bit per write, most significant bit first. */
            if (r == PLL_DEVICE)
                v->pll_shift = ((v->pll_shift << 1) | (byte & 1)) & PLL_WORD_MASK;
        }
    }
    if (reg <= CRTC_CTL && reg + len > CRTC_CTL)
        verite_ddc_drive(v, previous_ctl);
    if (reg < DRAM_CTL + 4 && reg + len > DRAM_CTL) {
        uint32_t dram = verite_reg(v, DRAM_CTL);
        if ((dram ^ previous_dram) & (DRAM_REFRESH_DISABLE | DRAM_REFRESH_COUNT)) {
            v->dram_last_refresh_ns = v->time_ns;
            v->dram_refresh_pending = 0;
        }
        verite_core_set_precharge(v->core, !!(dram & DRAM_SLOW_PRECHARGE));
    }
    if (reg < FRAME_BASE_A + 4 && reg + len > FRAME_BASE_A) {
        /* Writing A commits the pending B address as part of the same pair. */
        v->frame_base_b = verite_reg(v, FRAME_BASE_B);
        v->frame_flags &= ~(CRTC_CTL_DISPLAYED | CRTC_CTL_SWITCHED);
    }
    if (reg < CRTC_STATUS && reg + len > CRTC_CTL) {
        svga_recalctimings(&v->vga.svga);
        verite_redraw(v);
    }
}

static uint32_t
verite_dma_read(void *priv, uint32_t address)
{
    const verite_t *v = priv;
    uint8_t         data[4];
    if (!(v->command & 4))
        return UINT32_MAX;
    dma_bm_read(address, data, sizeof(data), 4);
    return data[0] | ((uint32_t) data[1] << 8) | ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);
}

static const verite_board board = { verite_ext_read, verite_ext_write, verite_dma_read };

static void
verite_update_bank(verite_t *v)
{
    uint32_t bank = verite_core_bank(v->core) & 0xffe000;
    int      vga  = v->mode & MODE_VGA;
    v->bank       = bank;
    if ((v->command & 2) && vga)
        mem_mapping_enable(&v->vga.svga.mapping);
    else
        mem_mapping_disable(&v->vga.svga.mapping);
    if ((v->command & 2) && !vga && (v->mode & MODE_VESA))
        mem_mapping_enable(&v->banked);
    else
        mem_mapping_disable(&v->banked);
}

static void
verite_settle(verite_t *v)
{
    static const uint8_t endian_xor[] = { 0, 3, 1, 2 };
    uint32_t             status       = verite_core_status(v->core);
    v->endian                         = endian_xor[(status >> 8) & 3];
    uint8_t mode                      = verite_core_mode(v->core);
    if ((v->mode ^ mode) & (MODE_VESA | MODE_VGA)) {
        v->mode = mode;
        verite_update_bank(v);
        svga_recalctimings(&v->vga.svga);
        verite_redraw(v);
    }
    if (status & 2)
        verite_redraw(v);
    uint8_t asserted = status & 1;
    if (asserted != v->irq_asserted) {
        v->irq_asserted = asserted;
        pci_irq(v->pci_slot, PCI_INTA, 0, asserted, &v->irq_state);
    }
}

static void
verite_run(verite_t *v)
{
    verite_core_run(v->core, v->time_ns, v->vga.svga.vram, VRAM_SIZE, &board, v);
    verite_settle(v);
}

static void
verite_dram_refresh(verite_t *v)
{
    uint32_t dram     = verite_reg(v, DRAM_CTL);
    unsigned interval = ((dram & DRAM_REFRESH_COUNT) >> 16) * DRAM_REFRESH_UNIT_NS;
    if (!interval || (dram & DRAM_REFRESH_DISABLE))
        return;

    unsigned refreshes = (v->time_ns - v->dram_last_refresh_ns) / interval;
    v->dram_last_refresh_ns += (uint64_t) refreshes * interval;
    v->dram_refresh_pending += refreshes;
    if ((dram & DRAM_REFRESH_VBLANK) && v->vga.svga.dispon)
        return;

    verite_core_refresh(v->core, v->dram_refresh_pending * (DRAM_REFRESH_CYCLES + !!(dram & DRAM_SLOW_PRECHARGE)));
    v->dram_refresh_pending = 0;
}

static void
verite_timer(void *priv)
{
    verite_t *v = priv;
    timer_advance_u64(&v->timer, TICK_US * TIMER_USEC);
    v->time_ns += TICK_US * 1000;
    verite_dram_refresh(v);
    verite_run(v);
}

static void
verite_vsync(svga_t *svga)
{
    verite_t *v   = svga->priv;
    uint32_t  ctl = verite_reg(v, CRTC_CTL);

    /* Address registers take effect at retrace, even with RISC retrace disabled. */
    v->frame[0]       = verite_reg(v, FRAME_BASE_A) & 0xffffff;
    v->frame[1]       = v->frame_base_b & 0xffffff;
    v->scanout_offset = verite_reg(v, CRTC_OFFSET) & 0xffff;
    if (svga->render == verite_render)
        verite_update_pitch(v, svga->hdisp / svga->dots_per_clock, v->native_bpp, ctl);

    v->frame_flags = (v->frame_flags ^ CRTC_CTL_EVEN_FRAME) | CRTC_CTL_DISPLAYED | CRTC_CTL_SWITCHED;
    if (ctl & CRTC_CTL_VSYNC) {
        v->ddc1_bit = (v->ddc1_bit + 1) % (sizeof(v->ddc1_edid) * 9);
        verite_core_retrace(v->core);
        verite_settle(v);
    }
}

static uint32_t
verite_port_read(uint16_t port, uint8_t len, verite_t *v)
{
    uint8_t reg = port & 0xff;
    verite_run(v);
    if (reg >= 0x80)
        return verite_ext_read(v, reg, len);
    uint32_t value = verite_core_read(v->core, reg, len, v->vga.svga.vram, VRAM_SIZE);
    verite_settle(v);
    return value;
}

static void
verite_port_write(uint16_t port, uint32_t value, uint8_t len, verite_t *v)
{
    uint8_t reg = port & 0xff;
    verite_run(v);
    if (reg >= 0x80)
        verite_ext_write(v, reg, value, len);
    else {
        verite_core_write(v->core, reg, value, len, v->vga.svga.vram, VRAM_SIZE, &board, v);
        if (reg < 0x10)
            verite_run(v);
        if ((reg <= 0x48 && reg + len > 0x48) || (reg < 0x78 && reg + len > 0x72))
            verite_update_bank(v);
        verite_settle(v);
    }
}

static uint8_t
verite_in(uint16_t port, void *priv)
{
    return verite_port_read(port, 1, priv);
}
static uint16_t
verite_inw(uint16_t port, void *priv)
{
    return verite_port_read(port, 2, priv);
}
static uint32_t
verite_inl(uint16_t port, void *priv)
{
    return verite_port_read(port, 4, priv);
}
static void
verite_out(uint16_t port, uint8_t val, void *priv)
{
    verite_port_write(port, val, 1, priv);
}
static void
verite_outw(uint16_t port, uint16_t val, void *priv)
{
    verite_port_write(port, val, 2, priv);
}
static void
verite_outl(uint16_t port, uint32_t val, void *priv)
{
    verite_port_write(port, val, 4, priv);
}

static uint8_t
verite_vga_in(uint16_t port, void *priv)
{
    verite_t *v = priv;
    if (port >= 0x3c6 && port <= 0x3c9)
        return bt48x_ramdac_in(port, 0, 0, v->ramdac, &v->vga.svga);
    return vga_in(port, &v->vga);
}

static void
verite_vga_out(uint16_t port, uint8_t value, void *priv)
{
    verite_t *v = priv;
    if (port >= 0x3c6 && port <= 0x3c9)
        verite_dac_write(v, port, 0, 0, value);
    else
        vga_out(port, value, &v->vga);
    if (port == 0x3cf && v->vga.svga.gdcaddr == 6)
        verite_update_bank(v);
}

static uint8_t
verite_vram_read(verite_t *v, uint32_t addr)
{
    return v->vga.svga.vram[(addr ^ v->endian) & (VRAM_SIZE - 1)];
}

static void
verite_vram_write(verite_t *v, uint32_t addr, uint8_t value)
{
    addr                                = (addr ^ v->endian) & (VRAM_SIZE - 1);
    v->vga.svga.vram[addr]              = value;
    v->vga.svga.changedvram[addr >> 12] = changeframecount;
}

static uint8_t
linear_read(uint32_t addr, void *priv)
{
    return verite_vram_read(priv, addr & (BAR_SIZE - 1));
}
static void
linear_write(uint32_t addr, uint8_t val, void *priv)
{
    verite_vram_write(priv, addr & (BAR_SIZE - 1), val);
}

static uint8_t
banked_read(uint32_t addr, void *priv)
{
    verite_t *v = priv;
    return verite_vram_read(v, v->bank + (addr & 0xffff));
}

static void
banked_write(uint32_t addr, uint8_t val, void *priv)
{
    verite_t *v = priv;
    verite_vram_write(v, v->bank + (addr & 0xffff), val);
}

static void
verite_decode(verite_t *v, int enable)
{
    if (v->io_installed) {
        io_removehandler((uint16_t) v->io_installed << 8, 0x100,
                         verite_in, verite_inw, verite_inl, verite_out, verite_outw, verite_outl, v);
        v->io_installed = 0;
    }
    io_removehandler(0x3a0, 0x20, verite_vga_in, NULL, NULL, verite_vga_out, NULL, NULL, v);
    io_removehandler(0x3c0, 0x20, verite_vga_in, NULL, NULL, verite_vga_out, NULL, NULL, v);
    if (!enable)
        return;
    v->vga.svga.vga_enabled = !!(v->command & 1);
    if (v->command & 1) {
        io_sethandler(0x3c0, 0x20, verite_vga_in, NULL, NULL, verite_vga_out, NULL, NULL, v);
        if (!(v->vga.svga.miscout & 1))
            io_sethandler(0x3a0, 0x20, verite_vga_in, NULL, NULL, verite_vga_out, NULL, NULL, v);
        if (v->io_hi) {
            v->io_installed = v->io_hi;
            io_sethandler((uint16_t) v->io_hi << 8, 0x100,
                          verite_in, verite_inw, verite_inl, verite_out, verite_outw, verite_outl, v);
        }
    }
    if ((v->command & 2) && v->bar_hi)
        mem_mapping_set_addr(&v->linear, (uint32_t) v->bar_hi << 24, BAR_SIZE);
    else
        mem_mapping_disable(&v->linear);
    verite_update_bank(v);

    if (v->rom_bar & 1)
        mem_mapping_set_addr(&v->bios.mapping, v->rom_bar & ~(ROM_SIZE - 1), ROM_SIZE);
    else
        mem_mapping_disable(&v->bios.mapping);
}

static uint8_t
verite_pci_read(int func, int addr, int len, void *priv)
{
    const verite_t *v = priv;
    (void) len;
    if (func)
        return 0xff;
    switch (addr) {
        case 0x00:
            return 0x63;
        case 0x01:
            return 0x11;
        case 0x02:
            return 0x01;
        case 0x04:
            return v->command;
        case 0x07:
            return 2;
        case 0x08:
            return 2;
        case 0x0b:
            return 3;
        case 0x13:
            return v->bar_hi;
        case 0x14:
            return 1;
        case 0x15:
            return v->io_hi;
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
            return v->rom_bar >> ((addr - 0x30) * 8);
        case 0x3c:
            return v->irq_line;
        case 0x3d:
            return PCI_INTA;
        case 0x50:
            return v->pci_control;
        default:
            return 0;
    }
}

static void
verite_pci_write(int func, int addr, int len, uint8_t value, void *priv)
{
    verite_t *v = priv;
    (void) len;
    if (func)
        return;
    switch (addr) {
        case 0x04:
            v->command = value & 7;
            break;
        case 0x13:
            v->bar_hi = value;
            break;
        case 0x15:
            v->io_hi = value;
            break;
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
            {
                unsigned shift = (addr - 0x30) * 8;
                v->rom_bar     = ((v->rom_bar & ~(0xffU << shift)) | ((uint32_t) value << shift)) & 0xffff8001;
                break;
            }
        case 0x3c:
            v->irq_line = value;
            return;
        case 0x50:
            v->pci_control = value;
            return;
        default:
            return;
    }
    verite_decode(v, 1);
}

static int
verite_available(void)
{
    return rom_present(VERITE_ROM);
}

static void
verite_speed_changed(void *priv)
{
    verite_t *v = priv;
    svga_recalctimings(&v->vga.svga);
}

static void
verite_reset(void *priv)
{
    verite_t *v = priv;
    verite_core_reset(v->core);
    memset(v->regs, 0, sizeof(v->regs));
    v->command = 3;
    v->bar_hi = v->io_hi = v->irq_line = v->pci_control = 0;
    v->rom_bar                                          = 0xc0001;
    v->frame_flags                                      = 0;
    v->frame_base_b                                     = 0;
    v->frame[0] = v->frame[1]                            = 0;
    v->pitch[0] = v->pitch[1]                            = 0;
    v->scanout_offset                                   = 0;
    v->pll_shift                                        = PLL_VGA_CLOCK;
    v->pixel_clock_hz                                   = 0.0;
    v->dram_last_refresh_ns                             = v->time_ns;
    v->dram_refresh_pending                             = 0;
    v->ddc1_bit                                         = 0;
    v->ddc2_active                                      = 0;
    /* Terminate any transaction carried over from before the reset. */
    i2c_gpio_set(v->ddc_bus, 0, 0);
    i2c_gpio_set(v->ddc_bus, 1, 0);
    i2c_gpio_set(v->ddc_bus, 1, 1);
    v->mode                                             = verite_core_mode(v->core);
    verite_decode(v, 1);
    verite_settle(v);
    verite_speed_changed(v);
}

static void *
verite_init(const device_t *info)
{
    verite_t *v = calloc(1, sizeof(*v));
    if (!v)
        return NULL;
    v->core = verite_core_new();
    if (!v->core) {
        free(v);
        return NULL;
    }
#ifdef VERITE_JIT_X64
    verite_core_set_jit(v->core, device_get_config_int("recompiler"));
#endif
    if (rom_init(&v->bios, VERITE_ROM, 0xc0000, ROM_SIZE, ROM_SIZE - 1, 0, MEM_MAPPING_EXTERNAL)) {
        verite_core_free(v->core);
        free(v);
        return NULL;
    }

    v->ramdac = device_add(&bt485_ramdac_device);
    v->ddc_bus = i2c_gpio_init("Vérité DDC");
    v->ddc_monitor = ddc_init(i2c_gpio_get_bus(v->ddc_bus));
    const uint8_t *edid = ddc_get_edid(v->ddc_monitor);
    if (edid) {
        memcpy(v->ddc1_edid, edid, sizeof(v->ddc1_edid));
        /* DDC1 can only carry one block. Remove the extension count. */
        v->ddc1_edid[127] += v->ddc1_edid[126];
        v->ddc1_edid[126] = 0;
    }
    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &verite_timing);
    svga_init(info, &v->vga.svga, v, VRAM_SIZE, verite_recalctimings,
              verite_vga_in, verite_vga_out, NULL, NULL);
    v->vga.svga.ramdac            = v->ramdac;
    v->vga.svga.vsync_callback    = verite_vsync;
    mem_mapping_add(&v->linear, 0, 0, linear_read, NULL, NULL, linear_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, v);
    mem_mapping_add(&v->banked, 0xa0000, 0x10000, banked_read, NULL, NULL, banked_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, v);
    pci_add_card(PCI_ADD_NORMAL, verite_pci_read, verite_pci_write, v, &v->pci_slot);
    verite_reset(v);
    timer_add(&v->timer, verite_timer, v, 0);
    timer_set_delay_u64(&v->timer, TICK_US * TIMER_USEC);
    return v;
}

static void
verite_close(void *priv)
{
    verite_t *v = priv;
    timer_disable(&v->timer);
    timer_disable(&v->vga.svga.timer);
    mem_mapping_disable(&v->vga.svga.mapping);
    verite_decode(v, 0);
    mem_mapping_disable(&v->linear);
    mem_mapping_disable(&v->banked);
    mem_mapping_disable(&v->bios.mapping);
    svga_close(&v->vga.svga);
    ddc_close(v->ddc_monitor);
    i2c_gpio_close(v->ddc_bus);
    verite_core_free(v->core);
    free(v->bios.rom);
    free(v);
}

#ifdef VERITE_JIT_X64
static const device_config_t verite_config[] = {
    {
        .name        = "recompiler",
        .description = "Dynamic recompiler",
        .type        = CONFIG_BINARY,
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
#endif

const device_t verite_v1000_device = {
    .name          = "Rendition Vérité V1000",
    .internal_name = "verite_v1000",
    .flags         = DEVICE_PCI,
    .init          = verite_init,
    .close         = verite_close,
    .reset         = verite_reset,
    .available     = verite_available,
    .speed_changed = verite_speed_changed,
    .force_redraw  = verite_redraw,
#ifdef VERITE_JIT_X64
    .config        = verite_config
#endif
};
