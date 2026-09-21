/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM PS/2 Model 25 MCGA video subsystem.
 *
 * The Model 25 MCGA does not implement the VGA register interface. Its two
 * proprietary gate arrays provide CGA-compatible text and graphics modes
 * plus 640x480 monochrome and 320x200 256-colour modes. Register and storage
 * behaviour follows the IBM Personal System/2 Model 25 Technical Reference,
 * first edition (June 1987) and the IBM 7690 Clinical Workstation Technical
 * Reference.
 *
 * This virtual device driver offers both a CRT and an LCD mode; the CRT is
 * modelled over the PS/2 Model 25/30 with an analog VGA style monitor
 * attachde, and the IBM 7690 with an LCD attached.
 *
 * Authors: Josh Rodd, <josh@rodd.us>
 *
 *          Copyright (C) 2026 Simplebooks Foundation
 *          Copyright (C) 2026 Josh Rodd
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/plat_unused.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/video.h>
#include <86box/vid_mcga.h>
#include "cpu.h"
#include "808x_marty_86box.h"


#define MCGA_VRAM_SIZE      0x10000
#define MCGA_FONT_RAM_SIZE  0x02000
#define MCGA_CRTC_REGS      0x15

/*
 * The 7690's panel has a fixed 640x480 raster. Everything a mode does not
 * cover is panel background; 400-line modes are centered vertically.
 *
 * Presentation uses off/on endpoints plus their RGB midpoint for two-bit
 * graphics. This intentionally departs from the reference's C1-only rule.
 * Each pel approaches its target with a quantized exponential response of
 * about 100 ms, using RGB interpolation between the selected endpoints.
 */
#define MCGA_LCD_WIDTH      640
#define MCGA_LCD_HEIGHT     480
#define MCGA_LCD_PICTURE    400
#define MCGA_LCD_LEVELS     65 /* Includes an exact halfway response state. */
#define MCGA_LCD_TAU_MS     100.0

/*
 * Panel colour sets, taken from the IBM 7690 reference's LCD appearance
 * presets. Pixel 0 (background) first, pixel 1 (foreground) second.
 */
static const uint8_t mcga_lcd_sets[2][2][3] = {
    { { 0x29, 0x2e, 0x47 }, { 0xb5, 0xc8, 0xc4 } }, /* dark field,    pale marks */
    { { 0xe8, 0xdb, 0x9b }, { 0x55, 0x4b, 0x4a } }  /* warm yellow,   dark marks */
};

/* The panel's memory is the Convertible's slower LCD timing, not the CRT's. */
static const video_timings_t mcga_lcd_timings = {
    .type    = VIDEO_ISA,
    .write_b = 4,
    .write_w = 8,
    .write_l = 16,
    .read_b  = 4,
    .read_w  = 8,
    .read_l  = 16
};

#define MCGA_MODE_TEXT      0
#define MCGA_MODE_CGA4      4
#define MCGA_MODE_CGA6      6
#define MCGA_MODE_11        11
#define MCGA_MODE_13        13

#define MCGA_CGA_BLINK      0x20
#define MCGA_CGA_HIRES      0x10
#define MCGA_CGA_ENABLE     0x08
#define MCGA_CGA_BW         0x04
#define MCGA_CGA_GRAPHICS   0x02
#define MCGA_CGA_80COL      0x01

typedef struct mcga_t {
    uint8_t *vram;
    uint8_t *font_ram;

    uint8_t crtc[MCGA_CRTC_REGS];
    uint8_t crtcreg;
    uint8_t cga_mode;
    uint8_t border;
    uint8_t ext_mode;

    uint8_t dac[256][3];
    uint8_t dac_mask;
    uint8_t dac_read_addr;
    uint8_t dac_write_addr;
    uint8_t dac_read_pos;
    uint8_t dac_write_pos;
    uint8_t dac_last_read;
    uint32_t palette[256];

    uint8_t enabled;
    uint8_t linepos;
    uint8_t font_pending;
    uint8_t irq_latch;
    uint8_t lcd;
    uint8_t lcd_set;
    uint8_t lcd_index;
    uint8_t lcd_regs[16];
    uint8_t lcd_next[3][MCGA_LCD_LEVELS];
    int     lcd_top;

    uint32_t lcd_color[MCGA_LCD_LEVELS];
    uint8_t *lcd_level;
    uint32_t *lcd_picture;
    uint8_t (*lcd_ram)[0x8000];

    int displine;
    int blink;

    uint64_t disp_on_time;
    uint64_t disp_off_time;

    pc_timer_t   timer;
    mem_mapping_t mapping;
} mcga_t;

/*
 * Where a picture row is drawn. The panel keeps the picture in its own
 * buffer, so that composing the raster cannot disturb what the mode is
 * drawing, and the picture row still indexes the mode's raster, which is
 * what the renderers use it for.
 */
#define MCGA_PICTURE_ROW(dev, y) ((y) + (dev)->lcd_top)
#define MCGA_LINE(dev, y) ((dev)->lcd ? \
    ((dev)->lcd_picture + ((size_t) MCGA_PICTURE_ROW(dev, y) * MCGA_LCD_WIDTH)) : \
    buffer32->line[y])

static const video_timings_t mcga_timings = {
    .type    = VIDEO_ISA,
    .write_b = 8,
    .write_w = 16,
    .write_l = 32,
    .read_b  = 8,
    .read_w  = 16,
    .read_l  = 32
};

static const uint8_t mcga_default_palette[16][3] = {
    { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x2a },
    { 0x00, 0x2a, 0x00 }, { 0x00, 0x2a, 0x2a },
    { 0x2a, 0x00, 0x00 }, { 0x2a, 0x00, 0x2a },
    { 0x2a, 0x15, 0x00 }, { 0x2a, 0x2a, 0x2a },
    { 0x15, 0x15, 0x15 }, { 0x15, 0x15, 0x3f },
    { 0x15, 0x3f, 0x15 }, { 0x15, 0x3f, 0x3f },
    { 0x3f, 0x15, 0x15 }, { 0x3f, 0x15, 0x3f },
    { 0x3f, 0x3f, 0x15 }, { 0x3f, 0x3f, 0x3f }
};

static int
mcga_mode(const mcga_t *dev)
{
    if ((dev->crtc[0x10] & 0x01) && (dev->ext_mode & 0x04))
        return MCGA_MODE_13;
    if (dev->crtc[0x10] & 0x02)
        return MCGA_MODE_11;
    if (dev->cga_mode & MCGA_CGA_HIRES)
        return MCGA_MODE_CGA6;
    if (dev->cga_mode & MCGA_CGA_GRAPHICS)
        return MCGA_MODE_CGA4;
    return MCGA_MODE_TEXT;
}

static int
mcga_is_double_width(const mcga_t *dev, UNUSED(int always))
{
    const int mode = mcga_mode(dev);

    if ((mode == MCGA_MODE_CGA6) || (mode == MCGA_MODE_11) ||
        ((mode == MCGA_MODE_TEXT) && (dev->cga_mode & MCGA_CGA_80COL)))
        return 0;

    return 1;
}

void
mcga_recalctimings(mcga_t *dev)
{
    double disptime;
    double _dispontime;
    double _dispofftime;
    double mcga_const   = (double) ((dev->crtc[0x10] & 0x10) ? VGACONST1 : CGACONST);

    disptime      = (double) ((dev->crtc[0x00] + 2) << 4);
    _dispontime   = (double) ((dev->crtc[0x01] + 1) << 4);
    if (_dispontime >= disptime)
        _dispontime = disptime - 2;

    _dispofftime       = disptime - _dispontime;
    _dispontime        = _dispontime * mcga_const;
    _dispofftime       = _dispofftime * mcga_const;
    dev->disp_on_time  = (uint64_t) (int64_t) (_dispontime);
    dev->disp_off_time = (uint64_t) (int64_t) (_dispofftime);
}

static int
mcga_width(const mcga_t *dev)
{
#ifdef NO_STRETCH
    if (mcga_is_double_width(dev, 0))
        return (dev->crtc[0x01] + 1) * 8;

    return (dev->crtc[0x01] + 1) * 16;
#else
    return (dev->crtc[0x01] + 1) * 16;
#endif
}

static int
mcga_height(const mcga_t *dev)
{
    return ((((dev->crtc[0x10]) & 0x40) ? 0x000 : 0x100) | dev->crtc[0x06]) + 1;
}

/*
 * The picture height the panel letterboxes. Every mode the formatter
 * produces is a 400-line picture - 200-line modes are doubled and a text
 * screen is 25 sixteen-line rows - except mode 11H, which is the panel's own
 * 480-line raster. The CRTC's own figure is not used: it carries a few lines
 * of vertical retrace with it.
 */
static int
mcga_panel_content(const mcga_t *dev)
{
    return (mcga_mode(dev) == MCGA_MODE_11) ? mcga_height(dev) : MCGA_LCD_PICTURE;
}

/* First panel line the mode's picture is drawn on; 0 when it fills the panel. */
static int
mcga_panel_top(const mcga_t *dev)
{
    const int content = mcga_panel_content(dev);

    if (!dev->lcd || (content >= MCGA_LCD_HEIGHT))
        return 0;

    return (MCGA_LCD_HEIGHT - content) / 2;
}

static void
ibm7690_lcd_reset(mcga_t *dev)
{
    /* Master/system reset preserves the two position registers, index 0Ch,
       and image SRAM (Technical Reference, 1-67). */
    memset(&dev->lcd_regs[0x02], 0, 0x0a);
    memset(&dev->lcd_regs[0x0d], 0, 0x03);
}

static unsigned
ibm7690_lcd_address(const mcga_t *dev)
{
    /* Provisional 15-bit SRAM decode: eight row and seven column bits.
       The diagnostic establishes distinct rows 01h..80h and columns
       00h..40h, but not the wiring outside that subset. In particular,
       column 40h must not alias the zero-column disturbance writes. */
    return ((unsigned) dev->lcd_regs[0x05] << 7) | (dev->lcd_regs[0x04] & 0x7f);
}

/*
 * LCD controller index 0Bh: the diagnostic synchronises on this one and counts
 * line-phase edges over a frame, expecting 390..410 of them, so the bits come
 * from the panel's own raster rather than from a stored value. The reference
 * infers bit 7 as line/display phase and bit 1 as vertical phase; the polarity
 * is not documented, so the active phases are reported as ones.
 */
static uint8_t
ibm7690_lcd_status(const mcga_t *dev)
{
    uint8_t ret = 0x00;

    /* One horizontal blanking pulse per line. */
    if (dev->linepos == 0)
        ret |= 0x80;

    /* Vertical blanking bounds the active-line count. The diagnostic waits
       for clear->set twice, then counts horizontal pulses while clear. */
    if (dev->displine >= mcga_height(dev))
        ret |= 0x02;

    return ret;
}

/* The LCD controller's index/data ports are independent of the tablet. */
static uint8_t
ibm7690_lcd_read(uint16_t addr, void *priv)
{
    mcga_t *dev = (mcga_t *) priv;

    if ((addr & 1) == 0)
        return dev->lcd_index;
    if (dev->lcd_index >= sizeof(dev->lcd_regs))
        return 0xff;
    if ((dev->lcd_index == 0x06) || (dev->lcd_index == 0x07))
        return dev->lcd_ram[dev->lcd_index - 0x06][ibm7690_lcd_address(dev)];
    if ((dev->lcd_index == 0x0b) && (dev->lcd_regs[0x0e] & 0x01))
        return ibm7690_lcd_status(dev);
    return dev->lcd_regs[dev->lcd_index];
}

static void
ibm7690_lcd_write(uint16_t addr, uint8_t val, void *priv)
{
    mcga_t *dev = (mcga_t *) priv;

    if ((addr & 1) == 0) {
        dev->lcd_index = val;
    } else if (dev->lcd_index == 0xff) {
        ibm7690_lcd_reset(dev);
    } else if (dev->lcd_index < sizeof(dev->lcd_regs)) {
        if ((dev->lcd_index == 0x06) || (dev->lcd_index == 0x07))
            dev->lcd_ram[dev->lcd_index - 0x06][ibm7690_lcd_address(dev)] = val;
        else
            dev->lcd_regs[dev->lcd_index] = val;
    }
}

static int
mcga_total_lines(const mcga_t *dev)
{
    /*
       The calculation gives me a total of 449 lines for 70 Hz.
       Total + adjust configured by the BIOS gives me 445 lines.
       This indicates that 4 lines need to be added.
     */
    return ((((dev->crtc[0x10]) & 0x40) ? 0x000 : 0x100) | dev->crtc[0x04]) +
        dev->crtc[0x05] + 4;
}

static int
mcga_vsync_start(const mcga_t *dev)
{
    /* Actually, + 1 is correct once you take vertical total adjust into account. */
    return ((((dev->crtc[0x10]) & 0x40) ? 0x000 : 0x100) | dev->crtc[0x07]) + 1;
}

static int
mcga_vsync_width(const mcga_t *dev)
{
    const int width = (dev->crtc[0x03] >> 4) & 0x0f;

    return width ? width : 16;
}

/* RGB interpolation makes the halfway state the arithmetic mean of the
   off/on pixels, rounded to the nearest channel value. */
static void
mcga_lcd_build_colors(mcga_t *dev)
{
    const int last = MCGA_LCD_LEVELS - 1;

    for (int level = 0; level <= last; level++) {
        uint32_t col = 0;

        for (unsigned channel = 0; channel < 3; channel++) {
            const unsigned value =
                (mcga_lcd_sets[dev->lcd_set][0][channel] * (last - level) +
                 mcga_lcd_sets[dev->lcd_set][1][channel] * level + last / 2) / last;

            col |= value << (16 - (8 * channel));
        }

        dev->lcd_color[level] = col | 0xff000000;
    }
}

/* Advance toward off, midpoint or on. Force progress when quantization
   would otherwise stall short of the target. */
static void
mcga_lcd_build_response(mcga_t *dev, double step_usec)
{
    const int    last  = MCGA_LCD_LEVELS - 1;
    const double step  = step_usec / 1000.0;
    const double decay = exp(-step / MCGA_LCD_TAU_MS);

    for (unsigned target = 0; target < 3; target++) {
        const int goal = target * (last / 2);

        for (int level = 0; level <= last; level++) {
            int next = (int) lround(goal + ((level - goal) * decay));

            if ((next == level) && (level != goal))
                next += (level < goal) ? 1 : -1;
            dev->lcd_next[target][level] = (uint8_t) next;
        }
    }
}

/*
 * CRT palette entries follow the DAC. The LCD text and two-bit
 * graphics conversions bypass this palette; the other graphics paths retain
 * their provisional binary conversion here.
 */
static void
mcga_rebuild_color(mcga_t *dev, uint8_t index)
{
    const uint32_t col = makecol32(video_6to8[dev->dac[index][0] & 0x3f],
                                   video_6to8[dev->dac[index][1] & 0x3f],
                                   video_6to8[dev->dac[index][2] & 0x3f]);

    if (!dev->lcd) {
        dev->palette[index] = col;
        return;
    }

    dev->palette[index] = (getcolr(col) | getcolg(col) | getcolb(col)) ?
                              dev->lcd_color[MCGA_LCD_LEVELS - 1] : dev->lcd_color[0];
}

static void
mcga_load_font(mcga_t *dev)
{
    const unsigned block = (dev->crtc[0x13] >> 4) & 0x03;
    const unsigned page  = (dev->crtc[0x12] >> 5) & 0x01;
    const unsigned count = (unsigned) dev->crtc[0x14] + 1;

    /*
     * Each font block is arranged as sixteen 512-byte scan-line planes.
     * An entry is a character number followed by that scan line's bitmap;
     * the character number is repeated in every plane.
     */
    for (unsigned entry = 0; entry < count; entry++) {
        const unsigned source = (block * 0x2000) + (entry * 2);
        const unsigned chr    = dev->vram[source];

        for (unsigned line = 0; line < 16; line++)
            dev->font_ram[(page * 0x1000) + (chr * 16) + line] =
                dev->vram[source + (line * 0x200) + 1];
    }

    dev->crtc[0x12] &= ~0x80;
    dev->font_pending = 0;
}

static uint32_t
mcga_translate_address(const mcga_t *dev, uint32_t addr)
{
    const int mode = mcga_mode(dev);

    if (mode == MCGA_MODE_TEXT) {
        if ((addr >= 0xa0000) && (addr < 0xb0000))
            return addr - 0xa0000;
        if ((addr >= 0xb8000) && (addr < 0xc0000))
            return 0x8000 + (addr - 0xb8000);
    } else if ((mode == MCGA_MODE_CGA4) || (mode == MCGA_MODE_CGA6)) {
        if ((addr >= 0xb8000) && (addr < 0xc0000))
            return 0x8000 + (addr - 0xb8000);
    } else if ((addr >= 0xa0000) && (addr < 0xb0000))
        return addr - 0xa0000;

    return UINT32_MAX;
}

static uint8_t
mcga_mem_read(uint32_t addr, void *priv)
{
    const mcga_t *dev = (mcga_t *) priv;
    const uint32_t offset = mcga_translate_address(dev, addr);

    return (offset < MCGA_VRAM_SIZE) ? dev->vram[offset] : 0xff;
}

static uint16_t
mcga_mem_readw(uint32_t addr, void *priv)
{
    return mcga_mem_read(addr, priv) | (mcga_mem_read(addr + 1, priv) << 8);
}

static uint32_t
mcga_mem_readl(uint32_t addr, void *priv)
{
    return mcga_mem_readw(addr, priv) | ((uint32_t) mcga_mem_readw(addr + 2, priv) << 16);
}

static void
mcga_mem_write(uint32_t addr, uint8_t val, void *priv)
{
    mcga_t *dev = (mcga_t *) priv;
    const uint32_t offset = mcga_translate_address(dev, addr);

    if (offset < MCGA_VRAM_SIZE)
        dev->vram[offset] = val;
}

static void
mcga_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    mcga_mem_write(addr, val, priv);
    mcga_mem_write(addr + 1, val >> 8, priv);
}

static void
mcga_mem_writel(uint32_t addr, uint32_t val, void *priv)
{
    mcga_mem_writew(addr, val, priv);
    mcga_mem_writew(addr + 2, val >> 16, priv);
}

static uint8_t
mcga_mode_control_read(const mcga_t *dev)
{
    const int mode = mcga_mode(dev);
    uint8_t ret = 0x10;

    if (dev->cga_mode & MCGA_CGA_80COL)
        ret |= 0x80;
    if ((mode == MCGA_MODE_CGA6) || (mode == MCGA_MODE_11) ||
        ((mode == MCGA_MODE_TEXT) && (dev->cga_mode & MCGA_CGA_80COL)))
        ret |= 0x20;
    if (mode == MCGA_MODE_TEXT)
        ret |= 0x08;
    if ((mode == MCGA_MODE_CGA4) || (mode == MCGA_MODE_CGA6) || (mode == MCGA_MODE_13))
        ret |= 0x04;
    if (mode == MCGA_MODE_11)
        ret |= 0x02;
    if (mode == MCGA_MODE_13)
        ret |= 0x01;

    return ret;
}

static uint8_t
mcga_io_read(uint16_t addr, void *priv)
{
    mcga_t *dev = (mcga_t *) priv;
    uint8_t ret;

    switch (addr) {
        case 0x03c6:
            ret = dev->dac_mask;
            break;

        case 0x03c7:
            ret = dev->dac_last_read ? 0x03 : 0x00;
            break;

        case 0x03c8:
            ret = dev->dac_write_addr;
            break;

        case 0x03c9:
            ret = dev->dac[dev->dac_read_addr][dev->dac_read_pos] & 0x3f;
            dev->dac_read_pos++;
            if (dev->dac_read_pos == 3) {
                dev->dac_read_pos = 0;
                dev->dac_read_addr++;
            }
            break;

        case 0x03d4:
            ret = dev->crtcreg;
            break;

        case 0x03d5:
            if (dev->crtcreg == 0x10)
                ret = mcga_mode_control_read(dev);
            else if (dev->crtcreg == 0x11)
                ret = (dev->crtc[0x11] & 0xbf) | (dev->irq_latch ? 0x40 : 0x00);
            else if ((dev->crtcreg == 0x12) && (dev->crtc[0x11] & 0x80))
                ret = 0x02; /* Analog color display. */
            else if (dev->crtcreg < MCGA_CRTC_REGS)
                ret = dev->crtc[dev->crtcreg];
            else
                ret = 0xff;
            break;

        case 0x03d8:
            ret = dev->cga_mode;
            break;

        case 0x03d9:
            ret = dev->border;
            break;

        case 0x03da:
            ret = 0x00;

            if ((dev->displine >= mcga_vsync_start(dev)) &&
                (dev->displine < (mcga_vsync_start(dev) + mcga_vsync_width(dev))))
                ret |= 0x08;
            if (!dev->linepos || (dev->displine >= mcga_height(dev)))
                ret |= 0x01;
            break;

        case 0x03dd:
            ret = dev->ext_mode & 0x84;
            break;

        default:
            ret = 0xff;
            break;
    }

    return ret;
}

static void
mcga_io_write(uint16_t addr, uint8_t val, void *priv)
{
    mcga_t *dev = (mcga_t *) priv;

    switch (addr) {
        case 0x03c6:
            /*
             * Type 8525 latches this register, but IBM documents that PEL
             * mask operations are not supported; do not apply it to pixels.
             */
            dev->dac_mask = val;
            break;

        case 0x03c7:
            dev->dac_read_addr = val;
            dev->dac_read_pos  = 0;
            dev->dac_last_read = 1;
            break;

        case 0x03c8:
            dev->dac_write_addr = val;
            dev->dac_write_pos  = 0;
            dev->dac_last_read  = 0;
            break;

        case 0x03c9:
            dev->dac[dev->dac_write_addr][dev->dac_write_pos] = val & 0x3f;
            dev->dac_write_pos++;
            if (dev->dac_write_pos == 3) {
                mcga_rebuild_color(dev, dev->dac_write_addr);
                dev->dac_write_pos = 0;
                dev->dac_write_addr++;
            }
            break;

        case 0x03d4:
            dev->crtcreg = val & 0x3f;
            break;

        case 0x03d5:
            if (dev->crtcreg >= MCGA_CRTC_REGS)
                break;
            if ((dev->crtcreg <= 0x09) && (dev->crtc[0x10] & 0x80))
                break;

            switch (dev->crtcreg) {
                case 0x09:
                    dev->crtc[0x09] = val & 0x0f;
                    break;

                case 0x0a:
                    dev->crtc[0x0a] = val & 0x2f;
                    break;

                case 0x0b:
                    dev->crtc[0x0b] = val & 0x0f;
                    break;

                case 0x0e:
                    dev->crtc[0x0e] = val & 0x0f;
                    break;

                case 0x10:
                    dev->crtc[0x10] = val & 0x9b;
                    break;

                case 0x11:
                    dev->crtc[0x11] = val & 0xb0;
                    if (!(val & 0x10)) {
                        dev->irq_latch = 0;
                        picintc(1 << 2);
                    } else if (val & 0x20)
                        picintc(1 << 2);
                    else if (dev->enabled && dev->irq_latch)
                        picint(1 << 2);
                    break;

                case 0x12:
                    ;
                    const uint8_t old = dev->crtc[0x12];
                    dev->crtc[0x12] = val & 0xf7;
                    if (!(val & 0x80))
                        dev->font_pending = 0;
                    else if (!(old & 0x80)) {
                        if (val & 0x40)
                            mcga_load_font(dev);
                        else
                            dev->font_pending = 1;
                    }
                    break;

                case 0x13:
                    /*
                     * All eight register latches are readable/writable.  The
                     * documented font-table pointers use only bits 5-4
                     * (00h, 10h, 20h, or 30h); mcga_load_font() masks those
                     * bits when turning the latched value into an address.
                     */
                    dev->crtc[0x13] = val;
                    break;

                default:
                    dev->crtc[dev->crtcreg] = val;
                    break;
            }

            mcga_recalctimings(dev);
            break;

        case 0x03d8:
            dev->cga_mode = val & 0x3f;
            mcga_recalctimings(dev);
            break;

        case 0x03d9:
            dev->border = val & 0x3f;
            break;

        case 0x03dd:
            dev->ext_mode = val & 0x84;
            mcga_recalctimings(dev);
            break;

        default:
            break;
    }
}

static uint8_t
mcga_cga4_color(const mcga_t *dev, uint8_t pixel)
{
    if (!pixel)
        return dev->border & 0x0f;

    uint8_t color = pixel << 1;
    if (dev->border & 0x20)
        color++;
    if (dev->border & 0x10)
        color += 8;
    return color;
}

static void
mcga_render_text(mcga_t *dev, int y)
{
#ifdef FIXED_DIMENSIONS
    const int cols       = (dev->cga_mode & MCGA_CGA_80COL) ? 80 : 40;
    const int glyph_line = y & 0x0f;
    const int row        = y >> 4;
#else
    int cols             = mcga_width(dev);
    if (mcga_is_double_width(dev, 0))
        cols /= 16;
    else
        cols /= 8;

    const int height = (dev->crtc[0x09] + 1) << 1;

    const int glyph_line = y % height;
    const int row        = y / height;
#endif
    const uint16_t start = ((dev->crtc[0x0c] << 8) | dev->crtc[0x0d]) & 0x3fff;
    const uint16_t cursor = ((dev->crtc[0x0e] << 8) | dev->crtc[0x0f]) & 0x3fff;

    for (int column = 0; column < cols; column++) {
        const uint16_t cell = (start + (row * cols) + column) & 0x3fff;
        const uint8_t chr   = dev->vram[0x8000 + ((cell << 1) & 0x7fff)];
        const uint8_t attr  = dev->vram[0x8000 + (((cell << 1) + 1) & 0x7fff)];
        const unsigned page = (dev->crtc[0x12] & 0x10) ?
                              ((attr >> 3) & 1) : ((dev->crtc[0x12] >> 5) & 1);
        uint8_t fg = (dev->crtc[0x12] & 0x10) ? (attr & 0x07) : (attr & 0x0f);
        uint8_t bg = (dev->cga_mode & MCGA_CGA_BLINK) ?
                     ((attr >> 4) & 0x07) : ((attr >> 4) & 0x0f);
        uint8_t bits = dev->font_ram[(page * 0x1000) + (chr * 16) + glyph_line];

        if ((dev->cga_mode & MCGA_CGA_BLINK) && (attr & 0x80) && (dev->blink & 0x10))
            fg = bg;

        /* The LCD takes digital RGBI before the DAC: odd text colors are
           white, even colors black (Technical Reference, 1-41). */
        const uint32_t fg_color = dev->lcd ?
            dev->lcd_color[(fg & 1) * (MCGA_LCD_LEVELS - 1)] : dev->palette[fg];
        const uint32_t bg_color = dev->lcd ?
            dev->lcd_color[(bg & 1) * (MCGA_LCD_LEVELS - 1)] : dev->palette[bg];

        /*
         * Cursor scan-line values are 0 through 7.  The Model 25
         * formatter double-scans the cursor over its 16-line text box.
         */
        const int cursor_line = glyph_line >> 1;
        const int cursor_on = !(dev->crtc[0x0a] & 0x20) &&
                              (cell == cursor) &&
                              (cursor_line >= (dev->crtc[0x0a] & 0x0f)) &&
                              (cursor_line <= (dev->crtc[0x0b] & 0x0f)) &&
                              (dev->blink & 0x08);
        if (cursor_on)
            bits = 0xff;

        /* Double-width. */
        if (mcga_is_double_width(dev, 0))
            for (int bit = 0; bit < 8; bit++)
                MCGA_LINE(dev, y)[(column * 16) + (bit << 1)] =
                MCGA_LINE(dev, y)[(column * 16) + (bit << 1) + 1] =
                    (bits & (0x80 >> bit)) ? fg_color : bg_color;
        else
            for (int bit = 0; bit < 8; bit++)
                MCGA_LINE(dev, y)[(column * 8) + bit] =
                    (bits & (0x80 >> bit)) ? fg_color : bg_color;
    }
}

static void
mcga_render_cga4(mcga_t *dev, int y)
{
    const int source_y = y >> 1;
    const uint16_t start = ((dev->crtc[0x0c] << 8) | dev->crtc[0x0d]) << 1;
    const uint32_t row = start + ((source_y & 1) * 0x2000) +
                         ((source_y >> 1) * 80);
    uint32_t colors[4];

    /* Assumed LCD mode-4/5 mapping: 00 off, 01/10 midpoint, 11 on.
       Independent of the CGA palette and DAC; overrides the reference's
       C1-only rule rather than changing the CRT color interpretation. */
    for (unsigned pixel = 0; pixel < 4; pixel++)
        colors[pixel] = dev->lcd ?
            dev->lcd_color[((pixel & 1) + (pixel >> 1)) * ((MCGA_LCD_LEVELS - 1) / 2)] :
            dev->palette[mcga_cga4_color(dev, pixel)];

    /* Double-width. */
    for (int byte = 0; byte < 80; byte++) {
        uint8_t data = dev->vram[0x8000 + ((row + byte) & 0x7fff)];
        if (mcga_is_double_width(dev, 0))
            for (int pixel = 0; pixel < 8; pixel += 2) {
                const uint32_t color = colors[data >> 6];
                MCGA_LINE(dev, y)[(byte * 8) + pixel] =
                MCGA_LINE(dev, y)[(byte * 8) + pixel + 1] =
                    color;
                data <<= 2;
            }
        else
            for (int pixel = 0; pixel < 4; pixel++) {
                const uint32_t color = colors[data >> 6];
                MCGA_LINE(dev, y)[(byte * 4) + pixel] =
                    color;
                data <<= 2;
            }
    }
}

static void
mcga_render_cga6(mcga_t *dev, int y)
{
    const int source_y = y >> 1;
    const uint16_t start = ((dev->crtc[0x0c] << 8) | dev->crtc[0x0d]) << 1;
    const uint32_t row = start + ((source_y & 1) * 0x2000) +
                         ((source_y >> 1) * 80);
    const uint8_t fg = (dev->cga_mode & MCGA_CGA_BW) ? 0x07 : (dev->border & 0x0f);

    for (int byte = 0; byte < 80; byte++) {
        const uint8_t data = dev->vram[0x8000 + ((row + byte) & 0x7fff)];
        for (int pixel = 0; pixel < 8; pixel++)
            MCGA_LINE(dev, y)[(byte * 8) + pixel] =
                dev->palette[(data & (0x80 >> pixel)) ? fg : 0];
    }
}

static void
mcga_render_mode11(mcga_t *dev, int y)
{
    const uint16_t start = ((dev->crtc[0x0c] << 8) | dev->crtc[0x0d]) << 1;
    const uint32_t row = start + (y * 80);
    const uint8_t fg = (dev->cga_mode & MCGA_CGA_BW) ? 0x07 : (dev->border & 0x0f);

    for (int byte = 0; byte < 80; byte++) {
        const uint8_t data = dev->vram[(row + byte) & 0xffff];
        for (int pixel = 0; pixel < 8; pixel++)
            MCGA_LINE(dev, y)[(byte * 8) + pixel] =
                dev->palette[(data & (0x80 >> pixel)) ? fg : 0];
    }
}

static void
mcga_render_mode13(mcga_t *dev, int y)
{
    const int source_y = y >> 1;
    const uint16_t start = ((dev->crtc[0x0c] << 8) | dev->crtc[0x0d]) << 1;
    const uint32_t row = start + (source_y * 320);

    /* Double-width. */
    if (mcga_is_double_width(dev, 0))
        for (int x = 0; x < 640; x += 2)
            MCGA_LINE(dev, y)[x] = MCGA_LINE(dev, y)[x + 1] =
                dev->palette[dev->vram[(row + (x >> 1)) & 0xffff]];
    else
        for (int x = 0; x < 320; x++)
            MCGA_LINE(dev, y)[x] = dev->palette[dev->vram[(row + (x >> 1)) & 0xffff]];
}

static void
mcga_render_line(mcga_t *dev, int y)
{
    const int width = mcga_width(dev);

    if (!(dev->cga_mode & MCGA_CGA_ENABLE)) {
        for (int x = 0; x < width; x++)
            MCGA_LINE(dev, y)[x] = dev->palette[0];
        return;
    }

    switch (mcga_mode(dev)) {
        case MCGA_MODE_CGA4:
            mcga_render_cga4(dev, y);
            break;

        case MCGA_MODE_CGA6:
            mcga_render_cga6(dev, y);
            break;

        case MCGA_MODE_11:
            mcga_render_mode11(dev, y);
            break;

        case MCGA_MODE_13:
            mcga_render_mode13(dev, y);
            break;

        default:
            mcga_render_text(dev, y);
            break;
    }
}

/* Fill the whole picture buffer with a colour. */
static void
mcga_lcd_fill(mcga_t *dev, uint32_t colour)
{
    uint32_t *picture = dev->lcd_picture;

    for (size_t i = 0; i < ((size_t) MCGA_LCD_WIDTH * MCGA_LCD_HEIGHT); i++)
        picture[i] = colour;
}

/*
 * A mode change moves where the picture sits in the raster. Everything the
 * picture does not cover is panel background, so put the background back
 * where the previous mode drew.
 */
static void
mcga_lcd_set_top(mcga_t *dev)
{
    const int top    = mcga_panel_top(dev);
    const int bottom = top + mcga_panel_content(dev);

    if (top == dev->lcd_top)
        return;

    dev->lcd_top = top;

    for (int y = 0; y < MCGA_LCD_HEIGHT; y++) {
        if ((y >= top) && (y < bottom))
            continue;

        uint32_t *line = dev->lcd_picture + ((size_t) y * MCGA_LCD_WIDTH);

        for (int x = 0; x < MCGA_LCD_WIDTH; x++)
            line[x] = dev->lcd_color[0];
    }
}

/*
 * How long one presented frame lasts. The reference gives no panel scan rate
 * - the interface's LP period has no documented unit or equation - so the
 * optics advance with the MCGA's own frame, which is the timing the guest
 * programs and the only one this machine documents.
 */
static double
mcga_lcd_step(const mcga_t *dev)
{
    return (double) ((dev->disp_on_time + dev->disp_off_time) *
                     (uint64_t) mcga_total_lines(dev)) / (double) TIMER_USEC;
}

/*
 * One panel frame: walk the whole 640x480 raster, step each pel's optical
 * level toward what the picture says, and compose the result. That raster is
 * all the renderer ever sees of this machine's video.
 */
static void
mcga_lcd_present(mcga_t *dev)
{
    const uint32_t off = dev->lcd_color[0];
    const uint32_t on  = dev->lcd_color[MCGA_LCD_LEVELS - 1];

    mcga_lcd_build_response(dev, mcga_lcd_step(dev));

    video_wait_for_buffer();

    for (int y = 0; y < MCGA_LCD_HEIGHT; y++) {
        const uint32_t *picture = dev->lcd_picture + ((size_t) y * MCGA_LCD_WIDTH);
        uint8_t        *level   = &dev->lcd_level[(size_t) y * MCGA_LCD_WIDTH];
        uint32_t       *line    = buffer32->line[y];

        for (int x = 0; x < MCGA_LCD_WIDTH; x++) {
            const unsigned target = (picture[x] != off) + (picture[x] == on);
            const uint8_t next = dev->lcd_next[target][level[x]];

            level[x] = next;
            line[x]  = dev->lcd_color[next];
        }
    }

    if ((xsize != MCGA_LCD_WIDTH) || (ysize != MCGA_LCD_HEIGHT) || video_force_resize_get()) {
        xsize = MCGA_LCD_WIDTH;
        ysize = MCGA_LCD_HEIGHT;
        set_screen_size(MCGA_LCD_WIDTH, MCGA_LCD_HEIGHT);
        video_force_resize_set(0);
    }

    video_blit_memtoscreen(0, 0, MCGA_LCD_WIDTH, MCGA_LCD_HEIGHT);
    frames++;

    /* The panel is a 640x480 display in every mode. */
    video_res_x = MCGA_LCD_WIDTH;
    video_res_y = MCGA_LCD_HEIGHT;
    video_bpp   = (mcga_mode(dev) == MCGA_MODE_CGA4) ? 2 : 1;
}

static void
mcga_present(mcga_t *dev)
{
    if (dev->lcd) {
        if (dev->enabled) {
            mcga_lcd_set_top(dev);
            mcga_lcd_present(dev);
        }
        return;
    }

    const int width  = mcga_width(dev);
    const int height = mcga_height(dev);

    if ((xsize != width) || (ysize != height) || video_force_resize_get()) {
        xsize = width;
        ysize = height;
        set_screen_size(width, height);
        video_force_resize_set(0);
    }

    video_blit_memtoscreen(0, 0, width, height);
    frames++;

    video_res_x = width;
    video_res_y = (mcga_mode(dev) == MCGA_MODE_11) ? 480 :
                  ((mcga_mode(dev) == MCGA_MODE_TEXT) ? 25 : 200);
    video_bpp = (mcga_mode(dev) == MCGA_MODE_TEXT) ? 0 :
                ((mcga_mode(dev) == MCGA_MODE_CGA4) ? 2 :
                 ((mcga_mode(dev) == MCGA_MODE_13) ? 8 : 1));
    if (mcga_mode(dev) == MCGA_MODE_TEXT)
        video_res_x = (dev->cga_mode & MCGA_CGA_80COL) ? 80 : 40;
}

static void
mcga_poll(void *priv)
{
    mcga_t *dev = (mcga_t *) priv;

    if (!dev->linepos) {
        timer_advance_u64(&dev->timer, dev->disp_on_time);
        dev->linepos = 1;

        if (dev->enabled) {
            const int y = dev->displine - dev->lcd_top;

            if (dev->displine == 0) {
                /* The panel composes from its own picture buffer, so the
                   mode's rendering never touches the frame it presents. */
                if (!dev->lcd)
                    video_wait_for_buffer();
            }

            /* Only the picture is rendered here; everything the picture does
               not cover stays at the panel's background level. */
            const int content = dev->lcd ? mcga_panel_content(dev) : mcga_height(dev);

            if ((y >= 0) && (y < content) &&
                ((y + dev->lcd_top) < MCGA_LCD_HEIGHT)) {
                mcga_render_line(dev, y);
                video_lightpen_check_trigger_strobe(0, y, 0, 0, 1. / (VGACONST1 / (cpuclock * (double) (1ULL << 32))), monitor_index_global);
            }
        }
        return;
    }

    video_lightpen_hsync();
    timer_advance_u64(&dev->timer, dev->disp_off_time);
    dev->linepos = 0;
    dev->displine++;

    if (dev->displine == mcga_vsync_start(dev)) {
        video_lightpen_vsync();
        if (dev->font_pending)
            mcga_load_font(dev);

        if (dev->crtc[0x11] & 0x10) {
            dev->irq_latch = 1;
            if (!(dev->crtc[0x11] & 0x20))
                picint(1 << 2);
        } else
            dev->irq_latch = 0;

        if (dev->enabled)
            mcga_present(dev);
        dev->blink++;
    }

    if (dev->displine >= mcga_total_lines(dev))
        dev->displine = 0;
}

void
mcga_set_enabled(void *priv, int enabled)
{
    mcga_t *dev = (mcga_t *) priv;

    enabled = !!enabled;
    if (dev->enabled == enabled)
        return;

    dev->enabled = enabled;
    if (enabled) {
        mem_mapping_enable(&dev->mapping);
        io_sethandler(0x03c6, 4,
                      mcga_io_read, NULL, NULL,
                      mcga_io_write, NULL, NULL, dev);
        io_sethandler(0x03d0, 16,
                      mcga_io_read, NULL, NULL,
                      mcga_io_write, NULL, NULL, dev);
        if (dev->irq_latch && !(dev->crtc[0x11] & 0x20) &&
            (dev->crtc[0x11] & 0x10))
            picint(1 << 2);
    } else {
        mem_mapping_disable(&dev->mapping);
        io_removehandler(0x03c6, 4,
                         mcga_io_read, NULL, NULL,
                         mcga_io_write, NULL, NULL, dev);
        io_removehandler(0x03d0, 16,
                         mcga_io_read, NULL, NULL,
                         mcga_io_write, NULL, NULL, dev);
        picintc(1 << 2);
    }
}

static void
mcga_reset(void *priv)
{
    mcga_t *dev = (mcga_t *) priv;

    memset(dev->crtc, 0x00, sizeof(dev->crtc));
    dev->crtc[0x00] = 0x30;
    dev->crtc[0x01] = 0x27;
    dev->crtc[0x02] = 0x2a;
    dev->crtc[0x03] = 0x26;
    dev->crtc[0x04] = 0x80;
    dev->crtc[0x06] = 0x8f;
    dev->crtc[0x07] = 0x98;
    dev->crtc[0x09] = 0x07;
    dev->crtc[0x0a] = 0x06;
    dev->crtc[0x0b] = 0x07;
    dev->crtc[0x10] = 0x18;
    dev->crtc[0x11] = 0x30;
    dev->crtc[0x12] = 0x46;
    dev->crtc[0x14] = 0xff;

    dev->crtcreg      = 0;
    dev->cga_mode     = 0x29;
    dev->border       = 0x30;
    dev->ext_mode     = 0x00;
    dev->dac_mask     = 0xff;
    dev->dac_read_addr = 0;
    dev->dac_write_addr = 0;
    dev->dac_read_pos = 0;
    dev->dac_write_pos = 0;
    dev->dac_last_read = 0;
    dev->linepos       = 0;
    dev->displine      = 0;
    dev->blink         = 0;
    dev->font_pending  = 0;
    dev->irq_latch     = 0;
    picintc(1 << 2);

    memset(dev->vram, 0x00, MCGA_VRAM_SIZE);
    memset(dev->font_ram, 0x00, MCGA_FONT_RAM_SIZE);
    memset(dev->dac, 0x00, sizeof(dev->dac));
    for (unsigned i = 0; i < 16; i++)
        memcpy(dev->dac[i], mcga_default_palette[i], 3);
    for (unsigned i = 0; i < 256; i++)
        mcga_rebuild_color(dev, i);

    if (dev->lcd) {
        dev->lcd_index = 0x00;
        ibm7690_lcd_reset(dev);
        memset(dev->lcd_level, 0, (size_t) MCGA_LCD_WIDTH * MCGA_LCD_HEIGHT);
        mcga_lcd_fill(dev, dev->lcd_color[0]);
        dev->lcd_top = -1;
        mcga_lcd_set_top(dev);
    }
}

static void *
mcga_init(UNUSED(const device_t *info))
{
    mcga_t *dev = (mcga_t *) calloc(1, sizeof(mcga_t));

    dev->vram     = (uint8_t *) calloc(1, MCGA_VRAM_SIZE);
    dev->font_ram = (uint8_t *) calloc(1, MCGA_FONT_RAM_SIZE);

    /*
     * The formatter always uses a 25.175 MHz oscillator.  Low-resolution
     * modes divide the dot clock by two but retain the same 31.5 kHz line
     * rate.
     */
    dev->disp_on_time  = (uint64_t) (25.4220457 * (double) TIMER_USEC);
    dev->disp_off_time = (uint64_t) (6.3555114 * (double) TIMER_USEC);

    timer_add(&dev->timer, mcga_poll, dev, 1);
    mem_mapping_add(&dev->mapping, 0xa0000, 0x20000,
                    mcga_mem_read, mcga_mem_readw, mcga_mem_readl,
                    mcga_mem_write, mcga_mem_writew, mcga_mem_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &mcga_timings);
    mcga_reset(dev);

    dev->enabled = 0;
    mcga_set_enabled(dev, 1);

    overscan_x = overscan_y = 0;

    return dev;
}

static void
mcga_close(void *priv)
{
    mcga_t *dev = (mcga_t *) priv;

    if (dev->enabled)
        mcga_set_enabled(dev, 0);
    timer_disable(&dev->timer);
    if (dev->lcd)
        io_removehandler(0xF304, 2,
                         ibm7690_lcd_read, NULL, NULL,
                         ibm7690_lcd_write, NULL, NULL, dev);
    free(dev->lcd_ram);
    free(dev->lcd_picture);
    free(dev->lcd_level);
    free(dev->font_ram);
    free(dev->vram);
    free(dev);
}

const device_t mcga_device = {
    .name          = "IBM PS/2 Model 25 MCGA",
    .internal_name = "ibm_ps2_m25_mcga",
    .flags         = DEVICE_ISA | DEVICE_ONBOARD,
    .local         = 0,
    .init          = mcga_init,
    .close         = mcga_close,
    .reset         = mcga_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * The 7690's internal video is the same MCGA subsystem as the other 8086
 * PS/2 machines and runs the same code; it is a separate device so that the
 * machine can name the panel it is wired to instead of a monitor and offer
 * the panel's colour sets. The machine init maps the D4000 firmware segment.
 */
static const device_config_t ibm7690_video_config[] = {
    // clang-format off
    {
        .name           = "display",
        .description    = "Panel",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Dark background, bright text",      .value = 0 },
            { .description = "Bright background, dark text",     .value = 1 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

static void *
ibm7690_video_init(const device_t *info)
{
    mcga_t *dev = (mcga_t *) mcga_init(info);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &mcga_lcd_timings);
    mcga_set_lcd(dev, device_get_config_int("display"));

    return dev;
}

const device_t ibm7690_video_device = {
    .name          = "7690 LCD panel",
    .internal_name = "ibm7690_video",
    .flags         = DEVICE_ISA | DEVICE_ONBOARD,
    .local         = 0,
    .init          = ibm7690_video_init,
    .close         = mcga_close,
    .reset         = mcga_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibm7690_video_config
};

void
mcga_set_lcd(void *priv, int colour_set)
{
    mcga_t *dev = (mcga_t *) priv;

    if (dev == NULL)
        return;

    dev->lcd     = 1;
    dev->lcd_set = (colour_set > 0) ? 1 : 0;

    dev->lcd_level   = (uint8_t *) calloc(1, (size_t) MCGA_LCD_WIDTH * MCGA_LCD_HEIGHT);
    dev->lcd_picture = (uint32_t *) calloc(1, sizeof(uint32_t) * (size_t) MCGA_LCD_WIDTH * MCGA_LCD_HEIGHT);
    dev->lcd_ram     = (uint8_t (*)[0x8000]) calloc(2, sizeof(*dev->lcd_ram));
    if ((dev->lcd_level == NULL) || (dev->lcd_picture == NULL) || (dev->lcd_ram == NULL))
        fatal("mcga_set_lcd(): Unable to allocate the panel's state\n");

    mcga_lcd_build_colors(dev);
    mcga_lcd_build_response(dev, mcga_lcd_step(dev));
    mcga_lcd_fill(dev, dev->lcd_color[0]);

    dev->lcd_top = -1;
    mcga_lcd_set_top(dev);

    io_sethandler(0xF304, 2,
                  ibm7690_lcd_read, NULL, NULL,
                  ibm7690_lcd_write, NULL, NULL, dev);

    for (unsigned index = 0; index < 256; index++)
        mcga_rebuild_color(dev, (uint8_t) index);
}

void
mcga_get_lcd_geometry(void *priv, int *top, int *content)
{
    const mcga_t *dev = (const mcga_t *) priv;

    *top     = ((dev != NULL) && dev->lcd) ? mcga_panel_top(dev) : 0;
    *content = ((dev != NULL) && dev->lcd) ? mcga_panel_content(dev) : MCGA_LCD_HEIGHT;
}

int
mcga_get_lcd_power(void *priv)
{
    const mcga_t *dev = (const mcga_t *) priv;

    return (dev != NULL) && dev->lcd && (dev->lcd_regs[0x0d] & 0x01);
}
