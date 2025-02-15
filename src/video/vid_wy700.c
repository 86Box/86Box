/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Wyse-700 emulation.
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
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/plat_unused.h>

#define WY700_XSIZE 1280
#define WY700_YSIZE 800

void updatewindowsize(int x, int y);

/* The Wyse 700 is an unusual video card. Though it has an MC6845 CRTC, this
 * is not exposed directly to the host PC. Instead, the CRTC is controlled by
 * an MC68705P3 microcontroller.
 *
 * Rather than emulate the real CRTC, I'm writing this as more or less a
 * fixed-frequency card with a 1280x800 display, and scaling its selection
 * of modes to that window.
 *
 * By default, the card responds to both the CGA and MDA I/O and memory
 * ranges. Either range can be disabled by means of jumpers; this allows
 * the Wy700 to coexist with a CGA or MDA.
 *
 * wy700->wy700_mode indicates which of the supported video modes is in use:
 *
 * 0x00:   40x 25   text     (CGA compatible)        [32x32 character cell]
 * 0x02:   80x 25   text     (CGA / MDA compatible)  [16x32 character cell]
 * 0x04:  320x200x4 graphics (CGA compatible)
 * 0x06:  640x200x2 graphics (CGA compatible)
 * 0x80:  640x400x2 graphics
 * 0x90:  320x400x4 graphics
 * 0xA0: 1280x400x2 graphics
 * 0xB0:  640x400x4 graphics
 * 0xC0: 1280x800x2 graphics (interleaved)
 * 0xD0:  640x800x4 graphics (interleaved)
 * In hi-res graphics modes, bit 3 of the mode byte is the enable flag.
 *
 */

/* What works (or appears to) :
 * MDA/CGA 80x25 text mode
 * CGA 40x25 text mode
 * CGA 640x200 graphics mode
 * CGA 320x200 graphics mode
 * Hi-res graphics modes
 * Font selection
 * Display enable / disable
 *   -- via Wy700 mode register      (in hi-res modes)
 *   -- via Wy700 command register   (in text & CGA modes)
 *   -- via CGA/MDA control register (in text & CGA modes)
 *
 * What doesn't work, is untested or not well understood:
 * - Cursor detach (commands 4 and 5)
 */

/* The microcontroller sets up the real CRTC with one of five fixed mode
 * definitions. As written, this is a fairly simplistic emulation that
 * doesn't attempt to closely follow the actual working of the CRTC; but I've
 * included the definitions here for information. */

static uint8_t mode_1280x800[] = {
    0x31, /* Horizontal total */
    0x28, /* Horizontal displayed */
    0x29, /* Horizontal sync position */
    0x06, /* Horizontal sync width */
    0x1b, /* Vertical total */
    0x00, /* Vertical total adjust */
    0x19, /* Vertical displayed */
    0x1a, /* Vsync position */
    0x03, /* Interlace and skew */
    0x0f, /* Maximum raster address */
};

static uint8_t mode_1280x400[] = {
    0x31, /* Horizontal total */
    0x28, /* Horizontal displayed */
    0x29, /* Horizontal sync position */
    0x06, /* Horizontal sync width */
    0x1b, /* Vertical total */
    0x00, /* Vertical total adjust */
    0x19, /* Vertical displayed */
    0x1a, /* Vsync position */
    0x01, /* Interlace and skew */
    0x0f, /* Maximum raster address */
};

static uint8_t mode_640x400[] = {
    0x18, /* Horizontal total */
    0x14, /* Horizontal displayed */
    0x14, /* Horizontal sync position */
    0x03, /* Horizontal sync width */
    0x1b, /* Vertical total */
    0x00, /* Vertical total adjust */
    0x19, /* Vertical displayed */
    0x1a, /* Vsync position */
    0x01, /* Interlace and skew */
    0x0f, /* Maximum raster address */
};

static uint8_t mode_640x200[] = {
    0x18, /* Horizontal total */
    0x14, /* Horizontal displayed */
    0x14, /* Horizontal sync position */
    0xff, /* Horizontal sync width */
    0x37, /* Vertical total */
    0x00, /* Vertical total adjust */
    0x32, /* Vertical displayed */
    0x34, /* Vsync position */
    0x03, /* Interlace and skew */
    0x07, /* Maximum raster address */
};

static uint8_t mode_80x24[] = {
    0x31, /* Horizontal total */
    0x28, /* Horizontal displayed */
    0x2A, /* Horizontal sync position */
    0xff, /* Horizontal sync width */
    0x1b, /* Vertical total */
    0x00, /* Vertical total adjust */
    0x19, /* Vertical displayed */
    0x1a, /* Vsync position */
    0x01, /* Interlace and skew */
    0x0f, /* Maximum raster address */
};

static uint8_t mode_40x24[] = {
    0x18, /* Horizontal total */
    0x14, /* Horizontal displayed */
    0x15, /* Horizontal sync position */
    0xff, /* Horizontal sync width */
    0x1b, /* Vertical total */
    0x00, /* Vertical total adjust */
    0x19, /* Vertical displayed */
    0x1a, /* Vsync position */
    0x01, /* Interlace and skew */
    0x0f, /* Maximum raster address */
};

/* Font ROM: Two fonts, each containing 256 characters, 16x16 pixels */
extern uint8_t fontdatw[512][32];

typedef struct wy700_t {
    mem_mapping_t mapping;

    /* The microcontroller works by watching four ports:
     * 0x3D8 / 0x3B8 (mode control register)
     * 0x3DD         (top scanline address)
     * 0x3DF         (Wy700 control register)
     * CRTC reg 14   (cursor location high)
     *
     * It will do nothing until one of these registers is touched. When
     * one is, it then reconfigures the internal 6845 based on what it
     * sees.
     */
    uint8_t last_03D8; /* Copies of values written to the listed */
    uint8_t last_03DD; /* I/O ports */
    uint8_t last_03DF;
    uint8_t last_crtc_0E;

    uint8_t cga_crtc[32];   /* The 'CRTC' as the host PC sees it */
    uint8_t real_crtc[32];  /* The internal CRTC as the microcontroller */
                            /* sees it */
    int      cga_crtcreg;   /* Current CRTC register */
    uint16_t wy700_base;    /* Framebuffer base address (native modes) */
    uint8_t  wy700_control; /* Native control / command register */
    uint8_t  wy700_mode;    /* Current mode (see list at top of file) */
    uint8_t  cga_ctrl;      /* Emulated MDA/CGA control register */
    uint8_t  cga_colour;    /* Emulated CGA colour register (ignored) */

    uint8_t mda_stat; /* MDA status (IN 0x3BA) */
    uint8_t cga_stat; /* CGA status (IN 0x3DA) */

    int font;    /* Current font, 0 or 1 */
    int enabled; /* Display enabled, 0 or 1 */
    int detach;  /* Detach cursor, 0 or 1 */

    uint64_t   dispontime, dispofftime;
    pc_timer_t timer;

    int linepos, displine;
    int vc;
    int dispon, blink;
    int vsynctime;

    uint8_t *vram;
} wy700_t;

/* Mapping of attributes to colours, in CGA emulation... */
static int cgacols[256][2][2];
/* ... and MDA emulation. */
static int mdacols[256][2][2];

void    wy700_recalctimings(wy700_t *wy700);
void    wy700_write(uint32_t addr, uint8_t val, void *priv);
uint8_t wy700_read(uint32_t addr, void *priv);
void    wy700_checkchanges(wy700_t *wy700);

static video_timings_t timing_wy700 = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

void
wy700_out(uint16_t addr, uint8_t val, void *priv)
{
    wy700_t *wy700 = (wy700_t *) priv;
    switch (addr) {
        /* These three registers are only mapped in the 3Dx range,
         * not the 3Bx range. */
        case 0x3DD: /* Base address (low) */
            wy700->wy700_base &= 0xFF00;
            wy700->wy700_base |= val;
            wy700_checkchanges(wy700);
            break;

        case 0x3DE: /* Base address (high) */
            wy700->wy700_base &= 0xFF;
            wy700->wy700_base |= ((uint16_t) val) << 8;
            wy700_checkchanges(wy700);
            break;

        case 0x3DF: /* Command / control register */
            wy700->wy700_control = val;
            wy700_checkchanges(wy700);
            break;

        /* Emulated CRTC, register select */
        case 0x3b0:
        case 0x3b2:
        case 0x3b4:
        case 0x3b6:
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            wy700->cga_crtcreg = val & 31;
            break;

        /* Emulated CRTC, value */
        case 0x3b1:
        case 0x3b3:
        case 0x3b5:
        case 0x3b7:
        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            wy700->cga_crtc[wy700->cga_crtcreg] = val;

            wy700_checkchanges(wy700);
            wy700_recalctimings(wy700);
            return;

        /* Emulated MDA / CGA control register */
        case 0x3b8:
        case 0x3D8:
            wy700->cga_ctrl = val;
            wy700_checkchanges(wy700);
            return;
        /* Emulated CGA colour register */
        case 0x3D9:
            wy700->cga_colour = val;
            return;

        default:
            break;
    }
}

uint8_t
wy700_in(uint16_t addr, void *priv)
{
    const wy700_t *wy700 = (wy700_t *) priv;

    switch (addr) {
        case 0x3b0:
        case 0x3b2:
        case 0x3b4:
        case 0x3b6:
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            return wy700->cga_crtcreg;
        case 0x3b1:
        case 0x3b3:
        case 0x3b5:
        case 0x3b7:
        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            return wy700->cga_crtc[wy700->cga_crtcreg];
        case 0x3b8:
        case 0x3d8:
            return wy700->cga_ctrl;
        case 0x3d9:
            return wy700->cga_colour;
        case 0x3ba:
            return wy700->mda_stat;
        case 0x3da:
            return wy700->cga_stat;

        default:
            break;
    }
    return 0xff;
}

/* Check if any of the four key registers has changed. If so, check for a
 * mode change or cursor size change */
void
wy700_checkchanges(wy700_t *wy700)
{
    uint8_t curstart;
    uint8_t curend;

    if (wy700->last_03D8 == wy700->cga_ctrl && wy700->last_03DD == (wy700->wy700_base & 0xFF) && wy700->last_03DF == wy700->wy700_control && wy700->last_crtc_0E == wy700->cga_crtc[0x0E]) {
        return; /* Nothing changed */
    }
    /* Check for control register changes */
    if (wy700->last_03DF != wy700->wy700_control) {
        wy700->last_03DF = wy700->wy700_control;

        /* Values 1-7 are commands. */
        switch (wy700->wy700_control) {
            case 1: /* Reset */
                wy700->font    = 0;
                wy700->enabled = 1;
                wy700->detach  = 0;
                break;

            case 2: /* Font 1 */
                wy700->font = 0;
                break;

            case 3: /* Font 2 */
                wy700->font = 1;
                break;

                /* Even with the microprogram from an original card, I can't really work out
                 * what commands 4 and 5 (which I've called 'cursor detach' / 'cursor attach')
                 * do. Command 4 sets a flag in microcontroller RAM, and command 5 clears
                 * it. When the flag is set, the real cursor doesn't track the cursor in the
                 * emulated CRTC, and its blink rate increases. Possibly it's a self-test
                 * function of some kind.
                 *
                 * The card documentation doesn't cover these commands.
                 */

            case 4: /* Detach cursor */
                wy700->detach = 1;
                break;

            case 5: /* Attach cursor */
                wy700->detach = 0;
                break;

            case 6: /* Disable display */
                wy700->enabled = 0;
                break;

            case 7: /* Enable display */
                wy700->enabled = 1;
                break;

            default:
                break;
        }
        /* A control write with the top bit set selects graphics mode */
        if (wy700->wy700_control & 0x80) {
            /* Select hi-res graphics mode; map framebuffer at A0000 */
            mem_mapping_set_addr(&wy700->mapping, 0xa0000, 0x20000);
            wy700->wy700_mode = wy700->wy700_control;

            /* Select appropriate preset timings */
            if (wy700->wy700_mode & 0x40) {
                memcpy(wy700->real_crtc, mode_1280x800,
                       sizeof(mode_1280x800));
            } else if (wy700->wy700_mode & 0x20) {
                memcpy(wy700->real_crtc, mode_1280x400,
                       sizeof(mode_1280x400));
            } else {
                memcpy(wy700->real_crtc, mode_640x400,
                       sizeof(mode_640x400));
            }
        }
    }
    /* An attempt to program the CGA / MDA selects low-res mode */
    else if (wy700->last_03D8 != wy700->cga_ctrl) {
        wy700->last_03D8 = wy700->cga_ctrl;
        /* Set lo-res text or graphics mode.
         * (Strictly speaking, when not in hi-res mode the card
         *  should be mapped at B0000-B3FFF and B8000-BBFFF, leaving
         * a 16k hole between the two ranges) */
        mem_mapping_set_addr(&wy700->mapping, 0xb0000, 0x0C000);
        if (wy700->cga_ctrl & 2) /* Graphics mode */
        {
            wy700->wy700_mode = (wy700->cga_ctrl & 0x10) ? 6 : 4;
            memcpy(wy700->real_crtc, mode_640x200,
                   sizeof(mode_640x200));
        } else if (wy700->cga_ctrl & 1) /* Text mode 80x24 */
        {
            wy700->wy700_mode = 2;
            memcpy(wy700->real_crtc, mode_80x24, sizeof(mode_80x24));
        } else /* Text mode 40x24 */
        {
            wy700->wy700_mode = 0;
            memcpy(wy700->real_crtc, mode_40x24, sizeof(mode_40x24));
        }
    }
    /* Convert the cursor sizes from the ones used by the CGA or MDA
     * to native */

    if (wy700->cga_crtc[9] == 13) /* MDA scaling */
    {
        curstart             = wy700->cga_crtc[10] & 0x1F;
        wy700->real_crtc[10] = ((curstart + 5) >> 3) + curstart;
        if (wy700->real_crtc[10] > 31)
            wy700->real_crtc[10] = 31;
        /* And bring 'cursor disabled' flag across */
        if ((wy700->cga_crtc[10] & 0x60) == 0x20) {
            wy700->real_crtc[10] |= 0x20;
        }
        curend               = wy700->cga_crtc[11] & 0x1F;
        wy700->real_crtc[11] = ((curend + 5) >> 3) + curend;
        if (wy700->real_crtc[11] > 31)
            wy700->real_crtc[11] = 31;
    } else /* CGA scaling */
    {
        curstart             = wy700->cga_crtc[10] & 0x1F;
        wy700->real_crtc[10] = curstart << 1;
        if (wy700->real_crtc[10] > 31)
            wy700->real_crtc[10] = 31;
        /* And bring 'cursor disabled' flag across */
        if ((wy700->cga_crtc[10] & 0x60) == 0x20) {
            wy700->real_crtc[10] |= 0x20;
        }
        curend               = wy700->cga_crtc[11] & 0x1F;
        wy700->real_crtc[11] = curend << 1;
        if (wy700->real_crtc[11] > 31)
            wy700->real_crtc[11] = 31;
    }
}

void
wy700_write(uint32_t addr, uint8_t val, void *priv)
{
    wy700_t *wy700 = (wy700_t *) priv;

    if (wy700->wy700_mode & 0x80) /* High-res mode. */
    {
        addr &= 0xFFFF;
        /* In 800-line modes, bit 1 of the control register sets the high bit of the
         * write address. */
        if ((wy700->wy700_mode & 0x42) == 0x42) {
            addr |= 0x10000;
        }
        wy700->vram[addr] = val;
    } else {
        wy700->vram[addr & 0x3fff] = val;
    }
}

uint8_t
wy700_read(uint32_t addr, void *priv)
{
    const wy700_t *wy700 = (wy700_t *) priv;

    if (wy700->wy700_mode & 0x80) { /* High-res mode. */
        addr &= 0xFFFF;
        /* In 800-line modes, bit 0 of the control register sets the high bit of the
         * read address. */
        if ((wy700->wy700_mode & 0x41) == 0x41) {
            addr |= 0x10000;
        }
        return wy700->vram[addr];
    } else {
        return wy700->vram[addr & 0x3fff];
    }
}

void
wy700_recalctimings(wy700_t *wy700)
{
    double disptime;
    double _dispontime;
    double _dispofftime;

    disptime     = wy700->real_crtc[0] + 1;
    _dispontime  = wy700->real_crtc[1];
    _dispofftime = disptime - _dispontime;
    _dispontime *= MDACONST;
    _dispofftime *= MDACONST;
    wy700->dispontime  = (uint64_t) (_dispontime);
    wy700->dispofftime = (uint64_t) (_dispofftime);
}

/* Draw a single line of the screen in either text mode */
void
wy700_textline(wy700_t *wy700)
{
    int            w  = (wy700->wy700_mode == 0) ? 40 : 80;
    int            cw = (wy700->wy700_mode == 0) ? 32 : 16;
    uint8_t        chr;
    uint8_t        attr;
    uint8_t        bitmap[2];
    const uint8_t *fontbase = &fontdatw[0][0];
    int            blink;
    int            c;
    int            drawcursor;
    int            cursorline;
    int            mda = 0;
    uint16_t       addr;
    uint8_t        sc;
    uint16_t       ma = (wy700->cga_crtc[13] | (wy700->cga_crtc[12] << 8)) & 0x3fff;
    uint16_t       ca = (wy700->cga_crtc[15] | (wy700->cga_crtc[14] << 8)) & 0x3fff;

    /* The fake CRTC character height register selects whether MDA or CGA
     * attributes are used */
    if (wy700->cga_crtc[9] == 0 || wy700->cga_crtc[9] == 13) {
        mda = 1;
    }

    if (wy700->font) {
        fontbase += 256 * 32;
    }
    addr = ((ma & ~1) + (wy700->displine >> 5) * w) * 2;
    sc   = (wy700->displine >> 1) & 15;

    ma += ((wy700->displine >> 5) * w);

    if ((wy700->real_crtc[10] & 0x60) == 0x20) {
        cursorline = 0;
    } else {
        cursorline = ((wy700->real_crtc[10] & 0x1F) <= sc) && ((wy700->real_crtc[11] & 0x1F) >= sc);
    }

    for (int x = 0; x < w; x++) {
        chr        = wy700->vram[(addr + 2 * x) & 0x3FFF];
        attr       = wy700->vram[(addr + 2 * x + 1) & 0x3FFF];
        drawcursor = ((ma == ca) && cursorline && wy700->enabled && (wy700->cga_ctrl & 8) && (wy700->blink & 16));
        blink      = ((wy700->blink & 16) && (wy700->cga_ctrl & 0x20) && (attr & 0x80) && !drawcursor);

        if (wy700->cga_ctrl & 0x20)
            attr &= 0x7F;
        /* MDA underline */
        if (sc == 14 && mda && ((attr & 7) == 1)) {
            for (c = 0; c < cw; c++)
                buffer32->line[wy700->displine][(x * cw) + c] = mdacols[attr][blink][1];
        } else /* Draw 16 pixels of character */
        {
            bitmap[0] = fontbase[chr * 32 + 2 * sc];
            bitmap[1] = fontbase[chr * 32 + 2 * sc + 1];
            for (c = 0; c < 16; c++) {
                int col;
                if (c < 8)
                    col = (mda ? mdacols : cgacols)[attr][blink][(bitmap[0] & (1 << (c ^ 7))) ? 1 : 0];
                else
                    col = (mda ? mdacols : cgacols)[attr][blink][(bitmap[1] & (1 << ((c & 7) ^ 7))) ? 1 : 0];
                if (!(wy700->enabled) || !(wy700->cga_ctrl & 8))
                    col = mdacols[0][0][0];
                if (w == 40) {
                    buffer32->line[wy700->displine][(x * cw) + 2 * c]     = col;
                    buffer32->line[wy700->displine][(x * cw) + 2 * c + 1] = col;
                } else
                    buffer32->line[wy700->displine][(x * cw) + c] = col;
            }

            if (drawcursor) {
                for (c = 0; c < cw; c++)
                    buffer32->line[wy700->displine][(x * cw) + c] ^= (mda ? mdacols : cgacols)[attr][0][1];
            }
            ++ma;
        }
    }
}

/* Draw a line in either of the CGA graphics modes (320x200 or 640x200) */
void
wy700_cgaline(wy700_t *wy700)
{
    int      c;
    uint32_t dat;
    uint8_t  ink = 0;
    uint16_t addr;

    uint16_t ma = (wy700->cga_crtc[13] | (wy700->cga_crtc[12] << 8)) & 0x3fff;
    addr        = ((wy700->displine >> 2) & 1) * 0x2000 + (wy700->displine >> 3) * 80 + ((ma & ~1) << 1);

    /* The fixed mode setting here programs the real CRTC with a screen
     * width to 20, so draw in 20 fixed chunks of 4 bytes each */
    for (uint8_t x = 0; x < 20; x++) {
        dat = ((wy700->vram[addr & 0x3FFF] << 24) | (wy700->vram[(addr + 1) & 0x3FFF] << 16) | (wy700->vram[(addr + 2) & 0x3FFF] << 8) | (wy700->vram[(addr + 3) & 0x3FFF]));
        addr += 4;

        if (wy700->wy700_mode == 6) {
            for (c = 0; c < 32; c++) {
                ink = (dat & 0x80000000) ? 16 + 15 : 16 + 0;
                if (!(wy700->enabled) || !(wy700->cga_ctrl & 8))
                    ink = 16;
                buffer32->line[wy700->displine][x * 64 + 2 * c] = buffer32->line[wy700->displine][x * 64 + 2 * c + 1] = ink;
                dat                                                                                                   = dat << 1;
            }
        } else {
            for (c = 0; c < 16; c++) {
                switch ((dat >> 30) & 3) {
                    case 0:
                        ink = 16 + 0;
                        break;
                    case 1:
                        ink = 16 + 8;
                        break;
                    case 2:
                        ink = 16 + 7;
                        break;
                    case 3:
                        ink = 16 + 15;
                        break;

                    default:
                        break;
                }
                if (!(wy700->enabled) || !(wy700->cga_ctrl & 8))
                    ink = 16;
                buffer32->line[wy700->displine][x * 64 + 4 * c] = buffer32->line[wy700->displine][x * 64 + 4 * c + 1] = buffer32->line[wy700->displine][x * 64 + 4 * c + 2] = buffer32->line[wy700->displine][x * 64 + 4 * c + 3] = ink;
                dat                                                                                                                                                                                                               = dat << 2;
            }
        }
    }
}

/* Draw a line in the medium-resolution graphics modes (640x400 or 320x400) */
void
wy700_medresline(wy700_t *wy700)
{
    int      c;
    uint32_t dat;
    uint8_t  ink = 0;
    uint32_t addr;

    addr = (wy700->displine >> 1) * 80 + 4 * wy700->wy700_base;

    for (uint8_t x = 0; x < 20; x++) {
        dat = ((wy700->vram[addr & 0x1FFFF] << 24) | (wy700->vram[(addr + 1) & 0x1FFFF] << 16) | (wy700->vram[(addr + 2) & 0x1FFFF] << 8) | (wy700->vram[(addr + 3) & 0x1FFFF]));
        addr += 4;

        if (wy700->wy700_mode & 0x10) {
            for (c = 0; c < 16; c++) {
                switch ((dat >> 30) & 3) {
                    case 0:
                        ink = 16 + 0;
                        break;
                    case 1:
                        ink = 16 + 8;
                        break;
                    case 2:
                        ink = 16 + 7;
                        break;
                    case 3:
                        ink = 16 + 15;
                        break;

                    default:
                        break;
                }
                /* Display disabled? */
                if (!(wy700->wy700_mode & 8))
                    ink = 16;
                buffer32->line[wy700->displine][x * 64 + 4 * c] = buffer32->line[wy700->displine][x * 64 + 4 * c + 1] = buffer32->line[wy700->displine][x * 64 + 4 * c + 2] = buffer32->line[wy700->displine][x * 64 + 4 * c + 3] = ink;
                dat                                                                                                                                                                                                               = dat << 2;
            }
        } else {
            for (c = 0; c < 32; c++) {
                ink = (dat & 0x80000000) ? 16 + 15 : 16 + 0;
                /* Display disabled? */
                if (!(wy700->wy700_mode & 8))
                    ink = 16;
                buffer32->line[wy700->displine][x * 64 + 2 * c] = buffer32->line[wy700->displine][x * 64 + 2 * c + 1] = ink;
                dat                                                                                                   = dat << 1;
            }
        }
    }
}

/* Draw a line in one of the high-resolution modes */
void
wy700_hiresline(wy700_t *wy700)
{
    int      c;
    uint32_t dat;
    uint8_t  ink = 0;
    uint32_t addr;

    addr = (wy700->displine >> 1) * 160 + 4 * wy700->wy700_base;

    if (wy700->wy700_mode & 0x40) /* 800-line interleaved modes */
    {
        if (wy700->displine & 1)
            addr += 0x10000;
    }
    for (uint8_t x = 0; x < 40; x++) {
        dat = ((wy700->vram[addr & 0x1FFFF] << 24) | (wy700->vram[(addr + 1) & 0x1FFFF] << 16) | (wy700->vram[(addr + 2) & 0x1FFFF] << 8) | (wy700->vram[(addr + 3) & 0x1FFFF]));
        addr += 4;

        if (wy700->wy700_mode & 0x10) {
            for (c = 0; c < 16; c++) {
                switch ((dat >> 30) & 3) {
                    case 0:
                        ink = 16 + 0;
                        break;
                    case 1:
                        ink = 16 + 8;
                        break;
                    case 2:
                        ink = 16 + 7;
                        break;
                    case 3:
                        ink = 16 + 15;
                        break;

                    default:
                        break;
                }
                /* Display disabled? */
                if (!(wy700->wy700_mode & 8))
                    ink = 16;
                buffer32->line[wy700->displine][x * 32 + 2 * c] = buffer32->line[wy700->displine][x * 32 + 2 * c + 1] = ink;
                dat                                                                                                   = dat << 2;
            }
        } else {
            for (c = 0; c < 32; c++) {
                ink = (dat & 0x80000000) ? 16 + 15 : 16 + 0;
                /* Display disabled? */
                if (!(wy700->wy700_mode & 8))
                    ink = 16;
                buffer32->line[wy700->displine][x * 32 + c] = ink;
                dat                                         = dat << 1;
            }
        }
    }
}

void
wy700_poll(void *priv)
{
    wy700_t *wy700 = (wy700_t *) priv;
    int      mode;

    if (!wy700->linepos) {
        timer_advance_u64(&wy700->timer, wy700->dispofftime);
        wy700->cga_stat |= 1;
        wy700->mda_stat |= 1;
        wy700->linepos = 1;
        if (wy700->dispon) {
            if (wy700->displine == 0) {
                video_wait_for_buffer();
            }

            if (wy700->wy700_mode & 0x80)
                mode = wy700->wy700_mode & 0xF0;
            else
                mode = wy700->wy700_mode & 0x0F;

            switch (mode) {
                default:
                case 0x00:
                case 0x02:
                    wy700_textline(wy700);
                    break;
                case 0x04:
                case 0x06:
                    wy700_cgaline(wy700);
                    break;
                case 0x80:
                case 0x90:
                    wy700_medresline(wy700);
                    break;
                case 0xA0:
                case 0xB0:
                case 0xC0:
                case 0xD0:
                case 0xE0:
                case 0xF0:
                    wy700_hiresline(wy700);
                    break;
            }
        }
        video_process_8(WY700_XSIZE, wy700->displine);
        wy700->displine++;
        /* Hardcode a fixed refresh rate and VSYNC timing */
        if (wy700->displine == 800) /* Start of VSYNC */
        {
            wy700->cga_stat |= 8;
            wy700->dispon = 0;
        }
        if (wy700->displine == 832) /* End of VSYNC */
        {
            wy700->displine = 0;
            wy700->cga_stat &= ~8;
            wy700->dispon = 1;
        }
    } else {
        if (wy700->dispon) {
            wy700->cga_stat &= ~1;
            wy700->mda_stat &= ~1;
        }
        timer_advance_u64(&wy700->timer, wy700->dispontime);
        wy700->linepos = 0;

        if (wy700->displine == 800) {
            /* Hardcode 1280x800 window size */
            if ((WY700_XSIZE != xsize) || (WY700_YSIZE != ysize) || video_force_resize_get()) {
                xsize = WY700_XSIZE;
                ysize = WY700_YSIZE;
                if (xsize < 64)
                    xsize = 656;
                if (ysize < 32)
                    ysize = 200;
                set_screen_size(xsize, ysize);

                if (video_force_resize_get())
                    video_force_resize_set(0);
            }
            video_blit_memtoscreen(0, 0, xsize, ysize);

            frames++;
            /* Fixed 1280x800 resolution */
            video_res_x = WY700_XSIZE;
            video_res_y = WY700_YSIZE;
            if (wy700->wy700_mode & 0x80)
                mode = wy700->wy700_mode & 0xF0;
            else
                mode = wy700->wy700_mode & 0x0F;
            switch (mode) {
                case 0x00:
                case 0x02:
                    video_bpp = 0;
                    break;
                case 0x04:
                case 0x90:
                case 0xB0:
                case 0xD0:
                case 0xF0:
                    video_bpp = 2;
                    break;
                default:
                    video_bpp = 1;
                    break;
            }
            wy700->blink++;
        }
    }
}

void *
wy700_init(UNUSED(const device_t *info))
{
    int      c;
    wy700_t *wy700 = malloc(sizeof(wy700_t));

    memset(wy700, 0, sizeof(wy700_t));
    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_wy700);

    /* 128k video RAM */
    wy700->vram = malloc(0x20000);

    loadfont("roms/video/wyse700/wy700.rom", 3);

    timer_add(&wy700->timer, wy700_poll, wy700, 1);

    /* Occupy memory between 0xB0000 and 0xBFFFF (moves to 0xA0000 in
     * high-resolution modes)  */
    mem_mapping_add(&wy700->mapping, 0xb0000, 0x10000, wy700_read, NULL, NULL, wy700_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, wy700);
    /* Respond to both MDA and CGA I/O ports */
    io_sethandler(0x03b0, 0x000C, wy700_in, NULL, NULL, wy700_out, NULL, NULL, wy700);
    io_sethandler(0x03d0, 0x0010, wy700_in, NULL, NULL, wy700_out, NULL, NULL, wy700);

    /* Set up the emulated attributes.
     * CGA is done in four groups: 00-0F, 10-7F, 80-8F, 90-FF */
    for (c = 0; c < 0x10; c++) {
        cgacols[c][0][0] = cgacols[c][1][0] = cgacols[c][1][1] = 16;
        if (c & 8)
            cgacols[c][0][1] = 15 + 16;
        else
            cgacols[c][0][1] = 7 + 16;
    }
    for (c = 0x10; c < 0x80; c++) {
        cgacols[c][0][0] = cgacols[c][1][0] = cgacols[c][1][1] = 16 + 7;
        if (c & 8)
            cgacols[c][0][1] = 15 + 16;
        else
            cgacols[c][0][1] = 0 + 16;

        if ((c & 0x0F) == 8)
            cgacols[c][0][1] = 8 + 16;
    }
    /* With special cases for 00, 11, 22, ... 77 */
    cgacols[0x00][0][1] = cgacols[0x00][1][1] = 16;
    for (c = 0x11; c <= 0x77; c += 0x11) {
        cgacols[c][0][1] = cgacols[c][1][1] = 16 + 7;
    }
    for (c = 0x80; c < 0x90; c++) {
        cgacols[c][0][0] = 16 + 8;
        if (c & 8)
            cgacols[c][0][1] = 15 + 16;
        else
            cgacols[c][0][1] = 7 + 16;
        cgacols[c][1][0] = cgacols[c][1][1] = cgacols[c - 0x80][0][0];
    }
    for (c = 0x90; c < 0x100; c++) {
        cgacols[c][0][0] = 16 + 15;
        if (c & 8)
            cgacols[c][0][1] = 8 + 16;
        else
            cgacols[c][0][1] = 7 + 16;
        if ((c & 0x0F) == 0)
            cgacols[c][0][1] = 16;
        cgacols[c][1][0] = cgacols[c][1][1] = cgacols[c - 0x80][0][0];
    }
    /* Also special cases for 99, AA, ..., FF */
    for (c = 0x99; c <= 0xFF; c += 0x11) {
        cgacols[c][0][1] = 16 + 15;
    }
    /* Special cases for 08, 80 and 88 */
    cgacols[0x08][0][1] = 16 + 8;
    cgacols[0x80][0][1] = 16;
    cgacols[0x88][0][1] = 16 + 8;

    /* MDA attributes */
    for (c = 0; c < 256; c++) {
        mdacols[c][0][0] = mdacols[c][1][0] = mdacols[c][1][1] = 16;
        if (c & 8)
            mdacols[c][0][1] = 15 + 16;
        else
            mdacols[c][0][1] = 7 + 16;
    }
    mdacols[0x70][0][1] = 16;
    mdacols[0x70][0][0] = mdacols[0x70][1][0] = mdacols[0x70][1][1] = 16 + 15;
    mdacols[0xF0][0][1]                                             = 16;
    mdacols[0xF0][0][0] = mdacols[0xF0][1][0] = mdacols[0xF0][1][1] = 16 + 15;
    mdacols[0x78][0][1]                                             = 16 + 7;
    mdacols[0x78][0][0] = mdacols[0x78][1][0] = mdacols[0x78][1][1] = 16 + 15;
    mdacols[0xF8][0][1]                                             = 16 + 7;
    mdacols[0xF8][0][0] = mdacols[0xF8][1][0] = mdacols[0xF8][1][1] = 16 + 15;
    mdacols[0x00][0][1] = mdacols[0x00][1][1] = 16;
    mdacols[0x08][0][1] = mdacols[0x08][1][1] = 16;
    mdacols[0x80][0][1] = mdacols[0x80][1][1] = 16;
    mdacols[0x88][0][1] = mdacols[0x88][1][1] = 16;

    /* Start off in 80x25 text mode */
    wy700->cga_stat   = 0xF4;
    wy700->wy700_mode = 2;
    wy700->enabled    = 1;
    memcpy(wy700->real_crtc, mode_80x24, sizeof(mode_80x24));
    return wy700;
}

void
wy700_close(void *priv)
{
    wy700_t *wy700 = (wy700_t *) priv;

    free(wy700->vram);
    free(wy700);
}

void
wy700_speed_changed(void *priv)
{
    wy700_t *wy700 = (wy700_t *) priv;

    wy700_recalctimings(wy700);
}

const device_t wy700_device = {
    .name          = "Wyse 700",
    .internal_name = "wy700",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = wy700_init,
    .close         = wy700_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = wy700_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
