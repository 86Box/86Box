/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Sigma Color 400 emulation.
 *
 *
 *
 * Authors: John Elliott,
 *
 *          Copyright 2018 John Elliott.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/plat_unused.h>

#define ROM_SIGMA_FONT "roms/video/sigma/sigma400_font.rom"
#define ROM_SIGMA_BIOS "roms/video/sigma/sigma400_bios.rom"

/* The Sigma Designs Color 400 is a video card from 1985, presumably intended
 * as an EGA competitor.
 *
 * The hardware seems to have gone through various iterations; I've seen
 * pictures of full-length and half-length versions.
 * TH99 describes the jumpers / switches:
 *                    <http://arvutimuuseum.ee/th99/v/S-T/52579.htm>
 *
 * The card is CGA-compatible at BIOS level, but to improve compatibility
 * attempts to write to the CGA I/O ports at 0x3D0-0x3DF trigger an NMI. The
 * card's BIOS handles the NMI and translates the CGA writes into commands
 * to its own hardware at 0x2D0-0x2DF. (DIP switches on the card allow the
 * base address to be changed, but since the BIOS dump I have doesn't support
 * this I haven't emulated it. Likewise the BIOS ROM address can be changed,
 * but I'm going with the default of 0xC0000).
 *
 * The BIOS still functions if the NMI system isn't operational. There
 * doesn't seem to be a jumper or DIP switch to lock it out, but at startup
 * the BIOS tests for its presence and configures itself to work or not
 * as required. I've therefore added a configuration option to handle this.
 *
 * The card's real CRTC at 0x2D0/0x2D1 appears to be a 6845. One oddity is
 * that all its horizontal counts are halved compared to what a real CGA
 * uses; 40-column modes have a width of 20, and 80-column modes have a
 * width of 40. This means that the CRTC cursor position (registers 14/15) can
 * only address even-numbered columns, so the top bit of the control
 * register at 0x2D9 is used to adjust the position.
 *
 * Apart from the CRTC, registers are:
 *
 * 0x2D8: Mode register. Values written by the card BIOS are:
 *         Text 40x25:       0xA8
 *         Text 80x25:       0xB9
 *         Text 80x30:       0xB9
 *         Text 80x50:       0x79
 *         Graphics 320x200: 0x0F
 *         Graphics 640x200: 0x1F
 *         Graphics 640x400: 0x7F
 *
 * I have assumed this is a bitmap with the following meaning: */
#define MODE_80COLS   0x01 /* For text modes, 80 columns across */
#define MODE_GRAPHICS 0x02 /* Graphics mode */
#define MODE_NOBLINK  0x04 /* Disable blink? */
#define MODE_ENABLE   0x08 /* Enable display */
#define MODE_HRGFX    0x10 /* For graphics modes, 640 pixels across */
#define MODE_640x400  0x40 /* 400-line graphics mode */
#define MODE_FONT16   0x80 /* Use 16-pixel high font */
/*
 * 0x2D9: Control register, with the following bits: */
#define CTL_CURSOR     0x80 /* Low bit of cursor position */
#define CTL_NMI        0x20 /* Writes to 0x3D0-0x3DF trigger NMI */
#define CTL_CLEAR_LPEN 0x08 /* Strobe 0 to clear lightpen latch */
#define CTL_SET_LPEN   0x04 /* Strobe 0 to set lightpen latch */
#define CTL_PALETTE    0x01 /* 0x2DE writes to palette (1) or plane (0) */
/*
 * The card BIOS seems to support two variants of the hardware: One where
 * bits 2 and 3 are normally 1 and are set to 0 to set/clear the latch, and
 * one where they are normally 0 and are set to 1. Behaviour is selected by
 * whether the byte at C000:17FFh is greater than 2Fh.
 *
 * 0x2DA: Status register.
 */
#define STATUS_CURSOR 0x80 /* Last value written to bit 7 of 0x2D9 */
#define STATUS_NMI    0x20 /* Last value written to bit 5 of 0x2D9 */
#define STATUS_RETR_V 0x10 /* Vertical retrace */
#define STATUS_LPEN_T 0x04 /* Lightpen switch is off */
#define STATUS_LPEN_A 0x02 /* Edge from lightpen has set trigger */
#define STATUS_RETR_H 0x01 /* Horizontal retrace */
/*
 * 0x2DB: On read: Byte written to the card that triggered NMI
 * 0x2DB: On write: Resets the 'card raised NMI' flag.
 * 0x2DC: On read: Bit 7 set if the card raised NMI. If so, bits 0-3
 *                 give the low 4 bits of the I/O port address.
 * 0x2DC: On write: Resets the NMI.
 * 0x2DD: Memory paging. The memory from 0xC1800 to 0xC1FFF can be either:
 *
 *  > ROM: A 128 character 8x16 font for use in graphics modes
 *  > RAM: Use by the video BIOS to hold its settings.
 *
 * Reading port 2DD switches to ROM. Bit 7 of the value read gives the
 * previous paging state: bit 7 set if ROM was paged, clear if RAM was
 * paged.
 *
 * Writing port 2DD switches to RAM.
 *
 * 0x2DE: Meaning depends on bottom bit of value written to port 0x2D9.
 *        Bit 0 set: Write to palette. High 4 bits of value = register,
 *                   low 4 bits = RGBI values (active low)
 *        Bit 0 clear: Write to plane select. Low 2 bits of value select
 *                    plane 0-3
 */

typedef struct sigma_t {
    mem_mapping_t mapping, bios_ram;
    rom_t         bios_rom;

    uint8_t crtc[32]; /* CRTC: Real values */

    uint8_t lastport;   /* Last I/O port written */
    uint8_t lastwrite;  /* Value written to that port */
    uint8_t sigma_ctl;  /* Controls register:
                         * Bit 7 is low bit of cursor position
                         * Bit 5 set if writes to CGA ports trigger NMI
                         * Bit 3 clears lightpen latch
                         * Bit 2 sets lightpen latch
                         * Bit 1 controls meaning of port 2DE
                         */
    uint8_t enable_nmi; /* Enable the NMI mechanism for CGA emulation?*/
    uint8_t rom_paged;  /* Is ROM paged in at 0xC1800? */

    uint8_t crtc_value; /* Value to return from a CRTC register read */

    uint8_t sigmastat, /* Status register [0x2DA] */
        fake_stat;     /* see sigma_in() for comment */

    uint8_t sigmamode; /* Mode control register [0x2D8] */

    uint16_t ma, maback;

    int crtcreg; /* CRTC: Real selected register */

    int linepos, displine;
    int sc, vc;
    int cgadispon;
    int con, coff, cursoron, cgablink;
    int vsynctime, vadj;
    int oddeven;

    uint64_t dispontime, dispofftime;

    int firstline, lastline;

    int drawcursor;

    pc_timer_t timer;

    uint8_t *vram;
    uint8_t  bram[2048];

    uint8_t palette[16];

    int plane;
    int revision;
    int fullchange;
} sigma_t;

#define COMPOSITE_OLD 0
#define COMPOSITE_NEW 1

static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static video_timings_t timing_sigma = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

static void sigma_recalctimings(sigma_t *cga);

static void
sigma_out(uint16_t addr, uint8_t val, void *priv)
{
    sigma_t *sigma = (sigma_t *) priv;
    uint8_t  old;

    if (addr >= 0x3D0 && addr < 0x3E0) {
        sigma->lastport  = addr & 0x0F;
        sigma->lastwrite = val;
        /* If set to NMI on video I/O... */
        if (sigma->enable_nmi && (sigma->sigma_ctl & CTL_NMI)) {
            sigma->lastport |= 0x80; /* Card raised NMI */
            nmi_raise();
        }
        /* For CRTC emulation, the card BIOS sets the value to be
         * read from port 0x3D1 like this */
        if (addr == 0x3D1)
            sigma->crtc_value = val;
    } else
        switch (addr) {
            case 0x2D0:
            case 0x2D2:
            case 0x2D4:
            case 0x2D6:
                sigma->crtcreg = val & 31;
                return;
            case 0x2D1:
            case 0x2D3:
            case 0x2D5:
            case 0x2D7:
                old                         = sigma->crtc[sigma->crtcreg];
                sigma->crtc[sigma->crtcreg] = val & crtcmask[sigma->crtcreg];
                if (old != val) {
                    if (sigma->crtcreg < 0xe || sigma->crtcreg > 0x10) {
                        sigma->fullchange = changeframecount;
                        sigma_recalctimings(sigma);
                    }
                }
                return;

            case 0x2D8:
                sigma->sigmamode = val;
                return;
            case 0x2D9:
                sigma->sigma_ctl = val;
                return;
            case 0x2DB:
                sigma->lastport &= 0x7F;
                return;
            case 0x2DC: /* Reset NMI */
                nmi = 0;
                sigma->lastport &= 0x7F;
                return;
            case 0x2DD: /* Page in RAM at 0xC1800 */
                if (sigma->rom_paged != 0)
                    flushmmucache_nopc();
                sigma->rom_paged = 0x00;
                return;

            case 0x2DE:
                if (sigma->sigma_ctl & CTL_PALETTE)
                    sigma->palette[val >> 4] = (val & 0x0F) ^ 0x0F;
                else
                    sigma->plane = val & 3;
                return;

            default:
                break;
        }
}

static uint8_t
sigma_in(uint16_t addr, void *priv)
{
    uint8_t  result = 0xFF;
    sigma_t *sigma  = (sigma_t *) priv;

    switch (addr) {
        case 0x2D0:
        case 0x2D2:
        case 0x2D4:
        case 0x2D6:
            result = sigma->crtcreg;
            break;
        case 0x2D1:
        case 0x2D3:
        case 0x2D5:
        case 0x2D7:
            result = sigma->crtc[sigma->crtcreg & 0x1F];
            break;
        case 0x2DA:
            result = (sigma->sigma_ctl & 0xE0) | (sigma->sigmastat & 0x1F);
            break;
        case 0x2DB:
            result = sigma->lastwrite; /* Value that triggered NMI */
            break;
        case 0x2DC:
            result = sigma->lastport; /* Port that triggered NMI */
            break;
        case 0x2DD: /* Page in ROM at 0xC1800 */
            result = (sigma->rom_paged ? 0x80 : 0);
            if (sigma->rom_paged != 0x80)
                flushmmucache_nopc();
            sigma->rom_paged = 0x80;
            break;
        case 0x3D1:
        case 0x3D3:
        case 0x3D5:
        case 0x3D7:
            result = sigma->crtc_value;
            break;
        /* For CGA compatibility we have to return something palatable on this port.
           On a real card this functionality can be turned on or off with SW1/6 */
        case 0x3DA:
            if (sigma->sigmamode & MODE_ENABLE) {
                result = sigma->sigmastat & 0x07;
                if (sigma->sigmastat & STATUS_RETR_V)
                    result |= 0x08;
            } else {
                /*
                 * The card is not running yet, and someone
                 * (probably the system BIOS) is trying to
                 * read our status in CGA mode.
                 *
                 * One of the systems that do this, is the
                 * DTK XT (PIM-10TB-Z board) with ERSO 2.42
                 * BIOS. If this test fails (i.e. it doesnt
                 * see valid HS and VS bits alternate) it
                 * will generate lots of annoying beeps..
                 *
                 * So, the trick here is to just send it
                 * some alternating bits, making it think
                 * the CGA circuitry is operational.
                 */
                sigma->fake_stat ^= (0x08 | 0x01);
                result = sigma->fake_stat;
            }
            break;

        default:
            break;
    }

    return result;
}

static void
sigma_write(uint32_t addr, uint8_t val, void *priv)
{
    sigma_t *sigma = (sigma_t *) priv;

    sigma->vram[sigma->plane * 0x8000 + (addr & 0x7fff)] = val;
    cycles -= 4;
}

static uint8_t
sigma_read(uint32_t addr, void *priv)
{
    const sigma_t *sigma = (sigma_t *) priv;

    cycles -= 4;

    return sigma->vram[sigma->plane * 0x8000 + (addr & 0x7fff)];
}

static void
sigma_bwrite(uint32_t addr, uint8_t val, void *priv)
{
    sigma_t *sigma = (sigma_t *) priv;

    addr &= 0x3FFF;
    if ((addr < 0x1800) || sigma->rom_paged || (addr >= 0x2000))
        ;
    else
        sigma->bram[addr & 0x7FF] = val;
}

static uint8_t
sigma_bread(uint32_t addr, void *priv)
{
    const sigma_t *sigma = (sigma_t *) priv;
    uint8_t        result;

    addr &= 0x3FFF;
    if (addr >= 0x2000)
        return 0xFF;
    if (addr < 0x1800 || sigma->rom_paged)
        result = sigma->bios_rom.rom[addr & 0x1FFF];
    else
        result = sigma->bram[addr & 0x7FF];

    return result;
}

static void
sigma_recalctimings(sigma_t *sigma)
{
    double disptime;
    double _dispontime;
    double _dispofftime;

    if (sigma->sigmamode & MODE_80COLS) {
        disptime    = (sigma->crtc[0] + 1) << 1;
        _dispontime = (sigma->crtc[1]) << 1;
    } else {
        disptime    = (sigma->crtc[0] + 1) << 2;
        _dispontime = sigma->crtc[1] << 2;
    }

    _dispofftime = disptime - _dispontime;
    _dispontime *= CGACONST;
    _dispofftime *= CGACONST;
    sigma->dispontime  = (uint64_t) (_dispontime);
    sigma->dispofftime = (uint64_t) (_dispofftime);
}

/* Render a line in 80-column text mode */
static void
sigma_text80(sigma_t *sigma)
{
    uint8_t        chr;
    uint8_t        attr;
    uint16_t       ca = (sigma->crtc[15] | (sigma->crtc[14] << 8));
    uint16_t       ma = ((sigma->ma & 0x3FFF) << 1);
    int            drawcursor;
    uint32_t       cols[4];
    const uint8_t *vram = sigma->vram + (ma << 1);

    ca = ca << 1;
    if (sigma->sigma_ctl & CTL_CURSOR)
        ++ca;
    ca &= 0x3fff;

    /* The Sigma 400 seems to use screen widths stated in words
       (40 for 80-column, 20 for 40-column) */
    for (uint32_t x = 0; x < (sigma->crtc[1] << 1); x++) {
        chr        = vram[x << 1];
        attr       = vram[(x << 1) + 1];
        drawcursor = ((ma == ca) && sigma->con && sigma->cursoron);

        if (!(sigma->sigmamode & MODE_NOBLINK)) {
            cols[1] = (attr & 15) | 16;
            cols[0] = ((attr >> 4) & 7) | 16;
            if ((sigma->cgablink & 8) && (attr & 0x80) && !sigma->drawcursor)
                cols[1] = cols[0];
        } else { /* No blink */
            cols[1] = (attr & 15) | 16;
            cols[0] = (attr >> 4) | 16;
        }

        if (drawcursor) {
            for (uint8_t c = 0; c < 8; c++) {
                if (sigma->sigmamode & MODE_FONT16)
                    buffer32->line[sigma->displine][(x << 3) + c + 8] = cols[(fontdatm[chr][sigma->sc & 15] & (1 << (c ^ 7))) ? 1 : 0] ^ 0xf;
                else
                    buffer32->line[sigma->displine][(x << 3) + c + 8] = cols[(fontdat[chr][sigma->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 0xf;
            }
        } else {
            for (uint8_t c = 0; c < 8; c++) {
                if (sigma->sigmamode & MODE_FONT16)
                    buffer32->line[sigma->displine][(x << 3) + c + 8] = cols[(fontdatm[chr][sigma->sc & 15] & (1 << (c ^ 7))) ? 1 : 0];
                else
                    buffer32->line[sigma->displine][(x << 3) + c + 8] = cols[(fontdat[chr][sigma->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
            }
        }
        ++ma;
    }

    sigma->ma += sigma->crtc[1];
}

/* Render a line in 40-column text mode */
static void
sigma_text40(sigma_t *sigma)
{
    uint8_t        chr;
    uint8_t        attr;
    uint16_t       ca = (sigma->crtc[15] | (sigma->crtc[14] << 8));
    uint16_t       ma = ((sigma->ma & 0x3FFF) << 1);
    int            drawcursor;
    uint32_t       cols[4];
    const uint8_t *vram = sigma->vram + ((ma << 1) & 0x3FFF);

    ca = ca << 1;
    if (sigma->sigma_ctl & CTL_CURSOR)
        ++ca;
    ca &= 0x3fff;

    /* The Sigma 400 seems to use screen widths stated in words
       (40 for 80-column, 20 for 40-column) */
    for (uint32_t x = 0; x < (sigma->crtc[1] << 1); x++) {
        chr        = vram[x << 1];
        attr       = vram[(x << 1) + 1];
        drawcursor = ((ma == ca) && sigma->con && sigma->cursoron);

        if (!(sigma->sigmamode & MODE_NOBLINK)) {
            cols[1] = (attr & 15) | 16;
            cols[0] = ((attr >> 4) & 7) | 16;
            if ((sigma->cgablink & 8) && (attr & 0x80) && !sigma->drawcursor)
                cols[1] = cols[0];
        } else { /* No blink */
            cols[1] = (attr & 15) | 16;
            cols[0] = (attr >> 4) | 16;
        }

        if (drawcursor) {
            for (uint8_t c = 0; c < 8; c++) {
                buffer32->line[sigma->displine][(x << 4) + 2 * c + 8] = buffer32->line[sigma->displine][(x << 4) + 2 * c + 9] = cols[(fontdatm[chr][sigma->sc & 15] & (1 << (c ^ 7))) ? 1 : 0] ^ 0xf;
            }
        } else {
            for (uint8_t c = 0; c < 8; c++) {
                buffer32->line[sigma->displine][(x << 4) + 2 * c + 8] = buffer32->line[sigma->displine][(x << 4) + 2 * c + 9] = cols[(fontdatm[chr][sigma->sc & 15] & (1 << (c ^ 7))) ? 1 : 0];
            }
        }
        ma++;
    }

    sigma->ma += sigma->crtc[1];
}

/* Draw a line in the 640x400 graphics mode */
static void
sigma_gfx400(sigma_t *sigma)
{
    const uint8_t *vram = &sigma->vram[((sigma->ma << 1) & 0x1FFF) + (sigma->sc & 3) * 0x2000];
    uint8_t        plane[4];
    uint8_t        col;

    for (uint32_t x = 0; x < (sigma->crtc[1] << 1); x++) {
        plane[0] = vram[x];
        plane[1] = vram[0x8000 + x];
        plane[2] = vram[0x10000 + x];
        plane[3] = vram[0x18000 + x];

        for (uint8_t c = 0, mask = 0x80; c < 8; c++, mask >>= 1) {
            col = ((plane[3] & mask) ? 8 : 0) | ((plane[2] & mask) ? 4 : 0) | ((plane[1] & mask) ? 2 : 0) | ((plane[0] & mask) ? 1 : 0);
            col |= 16;
            buffer32->line[sigma->displine][(x << 3) + c + 8] = col;
        }
        if (x & 1)
            ++sigma->ma;
    }
}

/* Draw a line in the 640x200 graphics mode.
 * This is actually a 640x200x16 mode; on startup, the BIOS selects plane 2,
 * blanks the other planes, and sets palette ink 4 to white. Pixels plotted
 * in plane 2 come out in white, others black; but by programming the palette
 * and plane registers manually you can get the full resolution. */
static void
sigma_gfx200(sigma_t *sigma)
{
    const uint8_t *vram = &sigma->vram[((sigma->ma << 1) & 0x1FFF) + (sigma->sc & 2) * 0x1000];
    uint8_t        plane[4];
    uint8_t        col;

    for (uint32_t x = 0; x < (sigma->crtc[1] << 1); x++) {
        plane[0] = vram[x];
        plane[1] = vram[0x8000 + x];
        plane[2] = vram[0x10000 + x];
        plane[3] = vram[0x18000 + x];

        for (uint8_t c = 0, mask = 0x80; c < 8; c++, mask >>= 1) {
            col = ((plane[3] & mask) ? 8 : 0) | ((plane[2] & mask) ? 4 : 0) | ((plane[1] & mask) ? 2 : 0) | ((plane[0] & mask) ? 1 : 0);
            col |= 16;
            buffer32->line[sigma->displine][(x << 3) + c + 8] = col;
        }

        if (x & 1)
            ++sigma->ma;
    }
}

/* Draw a line in the 320x200 graphics mode */
static void
sigma_gfx4col(sigma_t *sigma)
{
    const uint8_t *vram = &sigma->vram[((sigma->ma << 1) & 0x1FFF) + (sigma->sc & 2) * 0x1000];
    uint8_t        plane[4];
    uint8_t        mask;
    uint8_t        col;

    for (uint32_t x = 0; x < (sigma->crtc[1] << 1); x++) {
        plane[0] = vram[x];
        plane[1] = vram[0x8000 + x];
        plane[2] = vram[0x10000 + x];
        plane[3] = vram[0x18000 + x];

        mask = 0x80;
        for (uint8_t c = 0; c < 4; c++) {
            col  = ((plane[3] & mask) ? 2 : 0) | ((plane[2] & mask) ? 1 : 0);
            mask = mask >> 1;
            col |= ((plane[3] & mask) ? 8 : 0) | ((plane[2] & mask) ? 4 : 0);
            col |= 16;
            mask = mask >> 1;

            buffer32->line[sigma->displine][(x << 3) + (c << 1) + 8] = buffer32->line[sigma->displine][(x << 3) + (c << 1) + 9] = col;
        }

        if (x & 1)
            ++sigma->ma;
    }
}

static void
sigma_poll(void *priv)
{
    sigma_t *sigma = (sigma_t *) priv;
    int      x;
    int      c;
    int      oldvc;
    uint32_t cols[4];
    int      oldsc;

    if (!sigma->linepos) {
        timer_advance_u64(&sigma->timer, sigma->dispofftime);
        sigma->sigmastat |= STATUS_RETR_H;
        sigma->linepos = 1;
        oldsc          = sigma->sc;
        if ((sigma->crtc[8] & 3) == 3)
            sigma->sc = ((sigma->sc << 1) + sigma->oddeven) & 7;
        if (sigma->cgadispon) {
            if (sigma->displine < sigma->firstline) {
                sigma->firstline = sigma->displine;
                video_wait_for_buffer();
            }
            sigma->lastline = sigma->displine;

            cols[0] = 16;
            /* Left overscan */
            for (c = 0; c < 8; c++) {
                buffer32->line[sigma->displine][c] = cols[0];
                if (sigma->sigmamode & MODE_80COLS)
                    buffer32->line[sigma->displine][c + (sigma->crtc[1] << 4) + 8] = cols[0];
                else
                    buffer32->line[sigma->displine][c + (sigma->crtc[1] << 5) + 8] = cols[0];
            }
            if (sigma->sigmamode & MODE_GRAPHICS) {
                if (sigma->sigmamode & MODE_640x400)
                    sigma_gfx400(sigma);
                else if (sigma->sigmamode & MODE_HRGFX)
                    sigma_gfx200(sigma);
                else
                    sigma_gfx4col(sigma);
            } else { /* Text modes */
                if (sigma->sigmamode & MODE_80COLS)
                    sigma_text80(sigma);
                else
                    sigma_text40(sigma);
            }
        } else {
            cols[0] = 16;
            if (sigma->sigmamode & MODE_80COLS)
                hline(buffer32, 0, sigma->displine, (sigma->crtc[1] << 4) + 16, cols[0]);
            else
                hline(buffer32, 0, sigma->displine, (sigma->crtc[1] << 5) + 16, cols[0]);
        }

        if (sigma->sigmamode & MODE_80COLS)
            x = (sigma->crtc[1] << 4) + 16;
        else
            x = (sigma->crtc[1] << 5) + 16;

        for (c = 0; c < x; c++)
            buffer32->line[sigma->displine][c] = sigma->palette[buffer32->line[sigma->displine][c] & 0xf] | 16;

        video_process_8(x, sigma->displine);

        sigma->sc = oldsc;
        if (sigma->vc == sigma->crtc[7] && !sigma->sc)
            sigma->sigmastat |= STATUS_RETR_V;
        sigma->displine++;
        if (sigma->displine >= 560)
            sigma->displine = 0;
    } else {
        timer_advance_u64(&sigma->timer, sigma->dispontime);
        sigma->linepos = 0;
        if (sigma->vsynctime) {
            sigma->vsynctime--;
            if (!sigma->vsynctime)
                sigma->sigmastat &= ~STATUS_RETR_V;
        }
        if (sigma->sc == (sigma->crtc[11] & 31) || ((sigma->crtc[8] & 3) == 3 && sigma->sc == ((sigma->crtc[11] & 31) >> 1))) {
            sigma->con  = 0;
            sigma->coff = 1;
        }
        if ((sigma->crtc[8] & 3) == 3 && sigma->sc == (sigma->crtc[9] >> 1))
            sigma->maback = sigma->ma;
        if (sigma->vadj) {
            sigma->sc++;
            sigma->sc &= 31;
            sigma->ma = sigma->maback;
            sigma->vadj--;
            if (!sigma->vadj) {
                sigma->cgadispon = 1;
                sigma->ma = sigma->maback = (sigma->crtc[13] | (sigma->crtc[12] << 8)) & 0x3fff;
                sigma->sc                 = 0;
            }
        } else if (sigma->sc == sigma->crtc[9]) {
            sigma->maback = sigma->ma;
            sigma->sc     = 0;
            oldvc         = sigma->vc;
            sigma->vc++;
            sigma->vc &= 127;

            if (sigma->vc == sigma->crtc[6])
                sigma->cgadispon = 0;

            if (oldvc == sigma->crtc[4]) {
                sigma->vc   = 0;
                sigma->vadj = sigma->crtc[5];
                if (!sigma->vadj)
                    sigma->cgadispon = 1;
                if (!sigma->vadj)
                    sigma->ma = sigma->maback = (sigma->crtc[13] | (sigma->crtc[12] << 8)) & 0x3fff;
                if ((sigma->crtc[10] & 0x60) == 0x20)
                    sigma->cursoron = 0;
                else
                    sigma->cursoron = sigma->cgablink & 8;
            }

            if (sigma->vc == sigma->crtc[7]) {
                sigma->cgadispon = 0;
                sigma->displine  = 0;
                sigma->vsynctime = 16;
                if (sigma->crtc[7]) {
                    if (sigma->sigmamode & MODE_80COLS)
                        x = (sigma->crtc[1] << 4) + 16;
                    else
                        x = (sigma->crtc[1] << 5) + 16;
                    sigma->lastline++;
                    if ((x != xsize) || ((sigma->lastline - sigma->firstline) != ysize) || video_force_resize_get()) {
                        xsize = x;
                        ysize = sigma->lastline - sigma->firstline;
                        if (xsize < 64)
                            xsize = 656;
                        if (ysize < 32)
                            ysize = 200;
                        if (ysize <= 250)
                            set_screen_size(xsize, (ysize << 1) + 16);
                        else
                            set_screen_size(xsize, ysize + 8);

                        if (video_force_resize_get())
                            video_force_resize_set(0);
                    }

                    video_blit_memtoscreen(0, sigma->firstline - 4, xsize, (sigma->lastline - sigma->firstline) + 8);
                    frames++;

                    video_res_x = xsize - 16;
                    video_res_y = ysize;
                    if (sigma->sigmamode & MODE_GRAPHICS) {
                        if (sigma->sigmamode & (MODE_HRGFX | MODE_640x400))
                            video_bpp = 1;
                        else {
                            video_res_x /= 2;
                            video_bpp = 2;
                        }
                    } else if (sigma->sigmamode & MODE_80COLS) {
                        /* 80-column text */
                        video_res_x /= 8;
                        video_res_y /= sigma->crtc[9] + 1;
                        video_bpp = 0;
                    } else {
                        /* 40-column text */
                        video_res_x /= 16;
                        video_res_y /= sigma->crtc[9] + 1;
                        video_bpp = 0;
                    }
                }
                sigma->firstline = 1000;
                sigma->lastline  = 0;
                sigma->cgablink++;
                sigma->oddeven ^= 1;
            }
        } else {
            sigma->sc++;
            sigma->sc &= 31;
            sigma->ma = sigma->maback;
        }
        if (sigma->cgadispon)
            sigma->sigmastat &= ~STATUS_RETR_H;
        if (sigma->sc == (sigma->crtc[10] & 31) || ((sigma->crtc[8] & 3) == 3 && sigma->sc == ((sigma->crtc[10] & 31) >> 1)))
            sigma->con = 1;
    }
}

static void *
sigma_init(UNUSED(const device_t *info))
{
    int      bios_addr;
    sigma_t *sigma = malloc(sizeof(sigma_t));

    memset(sigma, 0, sizeof(sigma_t));

    bios_addr = device_get_config_hex20("bios_addr");

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_sigma);

    sigma->enable_nmi = device_get_config_int("enable_nmi");

    loadfont(ROM_SIGMA_FONT, 7);
    rom_init(&sigma->bios_rom, ROM_SIGMA_BIOS, bios_addr, 0x2000,
             0x1FFF, 0, MEM_MAPPING_EXTERNAL);
    /* The BIOS ROM is overlaid by RAM, so remove its default mapping
       and access it through sigma_bread() / sigma_bwrite() below */
    mem_mapping_disable(&sigma->bios_rom.mapping);
    memcpy(sigma->bram, &sigma->bios_rom.rom[0x1800], 0x800);

    sigma->vram = malloc(0x8000 * 4);

    timer_add(&sigma->timer, sigma_poll, sigma, 1);
    mem_mapping_add(&sigma->mapping, 0xb8000, 0x08000,
                    sigma_read, NULL, NULL,
                    sigma_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, sigma);
    mem_mapping_add(&sigma->bios_ram, bios_addr, 0x2000,
                    sigma_bread, NULL, NULL,
                    sigma_bwrite, NULL, NULL,
                    sigma->bios_rom.rom, MEM_MAPPING_EXTERNAL, sigma);
    io_sethandler(0x03d0, 0x0010,
                  sigma_in, NULL, NULL,
                  sigma_out, NULL, NULL, sigma);
    io_sethandler(0x02d0, 0x0010,
                  sigma_in, NULL, NULL,
                  sigma_out, NULL, NULL, sigma);

    /* Start with ROM paged in, BIOS RAM paged out */
    sigma->rom_paged = 0x80;

    cga_palette = device_get_config_int("rgb_type") << 1;
    if (cga_palette > 6)
        cga_palette = 0;
    cgapal_rebuild();

    if (sigma->enable_nmi)
        sigma->sigmastat = STATUS_LPEN_T;

    return sigma;
}

static int
sigma_available(void)
{
    return (rom_present(ROM_SIGMA_FONT) && rom_present(ROM_SIGMA_BIOS));
}

static void
sigma_close(void *priv)
{
    sigma_t *sigma = (sigma_t *) priv;

    free(sigma->vram);
    free(sigma);
}

void
sigma_speed_changed(void *priv)
{
    sigma_t *sigma = (sigma_t *) priv;

    sigma_recalctimings(sigma);
}

device_config_t sigma_config[] = {
  // clang-format off
    {
        .name           = "rgb_type",
        .description    = "RGB type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Color",            .value = 0 },
            { .description = "Green Monochrome", .value = 1 },
            { .description = "Amber Monochrome", .value = 2 },
            { .description = "Gray Monochrome",  .value = 3 },
            { .description = "Color (no brown)", .value = 4 },
            { .description = ""                             }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "enable_nmi",
        .description    = "Enable NMI for CGA emulation",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xc0000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "C000H", .value = 0xc0000 },
            { .description = "C800H", .value = 0xc8000 },
            { .description = "CC00H", .value = 0xcc000 },
            { .description = "D000H", .value = 0xd0000 },
            { .description = "D400H", .value = 0xd4000 },
            { .description = "D800H", .value = 0xd8000 },
            { .description = "DC00H", .value = 0xdc000 },
            { .description = "E000H", .value = 0xe0000 },
            { .description = "E400H", .value = 0xe4000 },
            { .description = "E800H", .value = 0xe8000 },
            { .description = "EC00H", .value = 0xec000 },
            { .description = ""                        }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t sigma_device = {
    .name          = "Sigma Color 400",
    .internal_name = "sigma400",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = sigma_init,
    .close         = sigma_close,
    .reset         = NULL,
    .available     = sigma_available,
    .speed_changed = sigma_speed_changed,
    .force_redraw  = NULL,
    .config        = sigma_config
};
