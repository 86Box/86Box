/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          IBM PC Convertible LCD controller and optional CRT adapter.
 *
 * The LCD is not a CGA with a monochrome palette: it has independent font
 * SRAM, two-panel scanning, readable registers and a diagnostic clock.
 * Reference: IBM 5140 Technical Reference, volume 1, 2-45..2-62, 3-53..3-68;
 * the 09/13/85 ROM's LCDCTL_TST, LCDINIT and video module.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_cga_comp.h>
#include <86box/vid_ibm5140.h>

#define LCD_WIDTH       640
#define LCD_HEIGHT      200
#define LCD_VRAM_SIZE   0x4000
#define LCD_FONT_SIZE   0x2000
#define LCD_NORMAL      0x04
#define LCD_DECODE      0x08
#define LCD_FONT_ACCESS 0x10
#define LCD_SYNCS       0x20
#define LCD_POWER       0x40
#define LCD_SENSE       0x80

/* Four serial data pins carry a top-panel byte and a bottom-panel byte
 * in four nibbles per character time. Each nibble takes three diagnostic
 * clocks. The ROM's row stride is 03c0 clocks, not a CRTC horizontal total.
 * A half-line frame pulse follows the 100 paired rows. The eleven-clock
 * fetch/serializer latency and panel ordering are reconstructed from the
 * two ROM pixel tests; they are not an independently measured gate netlist.
 */
#define LCD_LINE_CLOCKS  (80 * 12)
#define LCD_DATA_CLOCKS  (100 * LCD_LINE_CLOCKS)
#define LCD_FRAME_CLOCKS (LCD_DATA_CLOCKS + LCD_LINE_CLOCKS / 2)
#define LCD_FETCH_CLOCKS 11
#define LCD_FRAME_USEC   20000.0
/* Normal status is 50 Hz (Technical Reference 2-62). Its duty cycle is not
 * specified: 2 ms of blanking is inferred from the ROM's bounded low interval
 * and is consistent with the panel transfer-time range. The diagnostic
 * half-line pulse does not establish the normal-mode status pulse width. */
#define LCD_SYNC_USEC    2000.0
#define LCD_ACTIVE_USEC  (LCD_FRAME_USEC - LCD_SYNC_USEC)

/* Liquid-crystal optical persistence. The panel is driven at 50 Hz, so one
 * presented frame is one 20 ms response step. A 100 ms exponential time
 * constant in each direction is the starting approximation: the frame-by-frame
 * LGR measurement in IBM5140_REF/HANDBOOK_5140/LCD_APPEARANCE.md supports about
 * 106 ms for darkening on one reflective panel. Lightening and both later
 * panels remain unmeasured tuning parameters, not IBM specifications. */
#define LCD_TAU_TO_ONE_MS  100.0
#define LCD_TAU_TO_ZERO_MS 100.0
#define LCD_LEVELS         64

struct ibm5140_video_t {
    mem_mapping_t mapping;
    pc_timer_t    timer;
    mem_mapping_t substitution_mapping;
    cga_t        *crt;
    uint8_t       vram[LCD_VRAM_SIZE];
    uint8_t       font[LCD_FONT_SIZE];
    uint8_t       regs[32];
    uint8_t       control_index;
    uint8_t       function;
    uint8_t       diagnostic;
    uint8_t       display_index;
    uint8_t       mode;
    uint8_t       status_toggle;
    uint8_t       normal_sync;
    uint8_t       monitor;
    uint32_t      diagnostic_clock;
    uint32_t      blink;
    /* Per-pixel quantized optical level (0..LCD_LEVELS-1); one state per pel,
     * so interrupted transitions and temporal PWM keep their history. */
    uint8_t       level[LCD_WIDTH * LCD_HEIGHT];
    uint8_t       next_level[2][LCD_LEVELS];
    uint32_t      level_color[LCD_LEVELS];
};

/* Healthy-panel sRGB endpoints proposed in LCD_APPEARANCE.md, indexed by the
 * machine's display selection. Off/background first, on/foreground second.
 * The backlit panel's dark field and pale marks make it the effective reverse
 * of the reflective panels; that falls out of the table rather than a flag. */
static const uint8_t lcd_presets[3][2][3] = {
    { { 0x92, 0x9c, 0x91 }, { 0x51, 0x55, 0x60 } }, /* 1: original reflective */
    { { 0xa3, 0xad, 0x98 }, { 0x49, 0x4b, 0x65 } }, /* 2: enhanced reflective */
    { { 0x53, 0x57, 0xa6 }, { 0xb5, 0xce, 0xcb } }  /* 0: backlit             */
};

static double
lcd_srgb_to_linear(double channel)
{
    return (channel <= 0.04045) ? (channel / 12.92) : pow((channel + 0.055) / 1.055, 2.4);
}

static double
lcd_linear_to_srgb(double channel)
{
    return (channel <= 0.0031308) ? (channel * 12.92) : (1.055 * pow(channel, 1.0 / 2.4) - 0.055);
}

/* Precompute the presentation colours (linear-light interpolation between the
 * panel endpoints, quantized back to sRGB) and the two response tables. Each
 * response entry maps the current level to the next one under a constant
 * electrical target, so an interrupted transition resumes from wherever the
 * liquid crystal actually sits. */
static void
lcd_build_response(ibm5140_video_t *video, int display)
{
    const double tau[2]  = { LCD_TAU_TO_ZERO_MS, LCD_TAU_TO_ONE_MS };
    const double decay   = exp(-(LCD_FRAME_USEC / 1000.0) / tau[0]);
    const double rise    = 1.0 - exp(-(LCD_FRAME_USEC / 1000.0) / tau[1]);
    const int    preset  = (display == 1) ? 0 : ((display == 2) ? 1 : 2);
    const int    last    = LCD_LEVELS - 1;
    double       linear_off[3], linear_on[3];

    for (unsigned channel = 0; channel < 3; channel++) {
        linear_off[channel] = lcd_srgb_to_linear(lcd_presets[preset][0][channel] / 255.0);
        linear_on[channel]  = lcd_srgb_to_linear(lcd_presets[preset][1][channel] / 255.0);
    }

    for (int level = 0; level <= last; level++) {
        const double fraction = (double) level / last;
        uint32_t     color    = 0;
        for (unsigned channel = 0; channel < 3; channel++) {
            const double linear = linear_off[channel] + (fraction * (linear_on[channel] - linear_off[channel]));
            const double srgb   = lcd_linear_to_srgb(linear) * 255.0;
            const long   value  = lround(srgb);
            color |= ((uint32_t) (value < 0 ? 0 : (value > 255 ? 255 : value))) << (16 - (8 * channel));
        }
        video->level_color[level] = color;
        /* Quantizing the state makes the rounded map stall one level short of
         * an endpoint, so force progress toward the target while it is short. */
        uint8_t next_off = (uint8_t) lround(level * decay);
        uint8_t next_on  = (uint8_t) lround(level + ((last - level) * rise));
        if ((next_off == level) && (level != 0))
            next_off--;
        if ((next_on == level) && (level != last))
            next_on++;
        video->next_level[0][level] = next_off;
        video->next_level[1][level] = next_on;
    }
}

static const video_timings_t lcd_timings = {
    .type = VIDEO_ISA, .write_b = 4, .write_w = 8, .write_l = 16,
    .read_b = 4, .read_w = 8, .read_l = 16
};
static const video_timings_t crt_timings = {
    .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32,
    .read_b = 8, .read_w = 16, .read_l = 32
};

static unsigned
lcd_start(const ibm5140_video_t *video)
{
    return ((video->regs[0x0c] << 8) | video->regs[0x0d]) & 0x1fff;
}

static unsigned
lcd_text_scale(const ibm5140_video_t *video)
{
    return ((video->function & 2) && !(video->mode & 1)) ? 2 : 1;
}

static unsigned
lcd_columns(const ibm5140_video_t *video)
{
    unsigned count = video->regs[(video->function & 2) ? 0x15 : 0x01];
    return count / lcd_text_scale(video);
}

static unsigned
lcd_char_height(const ibm5140_video_t *video)
{
    return (video->regs[(video->function & 2) ? 0x09 : 0x16] & 31) + 1;
}

static int
lcd_graphics(const ibm5140_video_t *video)
{
    return (video->function & 2) && (video->mode & 2);
}

/* Return the displayed, binary eight-pel character row. Font access by
 * the CPU never changes the display's regeneration/font fetch sources. */
static uint8_t
lcd_character(const ibm5140_video_t *video, unsigned column, unsigned y)
{
    const unsigned scale = lcd_text_scale(video);
    const unsigned columns = lcd_columns(video);
    const unsigned height = lcd_char_height(video);
    unsigned address = lcd_start(video);
    unsigned scan = y;

    if (column >= columns || y >= (unsigned) video->regs[6] * height)
        return 0;

    if ((video->function & 1) && y >= LCD_HEIGHT / 2) {
        /* R25/R26 address the character row crossing the panel split;
         * R27 supplies the scan within that row (03c0:04 at 80x25). */
        address += ((video->regs[0x19] << 8) | video->regs[0x1a]) / scale;
        scan = y - LCD_HEIGHT / 2 + (video->regs[0x1b] & 31);
    }
    address = (address + (scan / height) * columns + column) & 0x1fff;
    scan %= height;

    const uint8_t character = video->vram[address * 2];
    const uint8_t attribute = video->vram[address * 2 + 1];
    const uint8_t enables = video->regs[0x14];
    const unsigned intensity = ((enables >> 3) & 1) | ((enables >> 6) & 2);
    const unsigned alternate = (attribute & 8) && intensity == 3;
    /* The BIOS uses R18=0. Bit 0 selects the base font; arbitrary R18
     * encodings beyond the two populated banks are not documented. */
    const unsigned font_base = ((video->regs[0x12] ^ alternate) & 1) << 12;
    uint8_t glyph = (scan < 8) ? video->font[font_base + character * 8 + scan] : 0;
    const unsigned foreground = attribute & enables & 7;
    const unsigned background = (attribute >> 4) & (enables >> 4) & 7;
    const int underline = (!(video->function & 2) && foreground == 1 && !background) ||
                          ((attribute & 8) && intensity == 1);

    if (underline && scan == 7)
        glyph = 0xff;
    if ((video->mode & 0x20) && (attribute & 0x80) && (video->blink & 16))
        glyph = 0;

    uint8_t pixels;
    if (foreground == background)
        pixels = foreground ? 0xff : 0;
    else
        pixels = background ? (uint8_t) ~glyph : glyph;
    if ((attribute & 8) && intensity == 2)
        pixels ^= 0xff;

    const unsigned cursor = ((video->regs[0x0e] << 8) | video->regs[0x0f]) & 0x1fff;
    const unsigned cursor_reg = (video->function & 2) ? 0x0a : 0x17;
    const unsigned cursor_start = video->regs[cursor_reg];
    const unsigned cursor_end = video->regs[cursor_reg + 1] & 31;
    const unsigned cursor_phase = (cursor_start & 0x60) == 0x60 ? 32 : 16;
    if (address == cursor && (cursor_start & 0x60) != 0x20 &&
        !(video->blink & cursor_phase) && scan >= (cursor_start & 31) && scan <= cursor_end)
        pixels ^= 0xff;
    return pixels;
}

/* All graphics modes consume the same 640 binary pels. A 320-mode value
 * 01 is white/black and 10 is black/white, never a uniform gray pixel.
 * For 40-column text the glyph pels are doubled; this is a CGA-compatible
 * placement inference, since the panel manual does not specify placement. */
static uint8_t
lcd_byte(const ibm5140_video_t *video, unsigned column, unsigned y)
{
    if (!(video->mode & 8) || !(video->function & LCD_SYNCS) ||
        column >= video->regs[0x15] || y >= LCD_HEIGHT)
        return 0;

    if (lcd_graphics(video)) {
        const unsigned height = lcd_char_height(video);
        unsigned row = y;
        unsigned address = lcd_start(video) * 2;
        if (y >= (unsigned) video->regs[6] * height)
            return 0;
        if ((video->function & 1) && y >= LCD_HEIGHT / 2) {
            address += (video->regs[0x1c] << 8) | video->regs[0x1d];
            row -= LCD_HEIGHT / 2;
        }
        address = ((address + (row / height) * video->regs[0x15] + column) & 0x1fff) |
                  ((row & 1) << 13);
        return video->vram[address];
    }

    const unsigned scale = lcd_text_scale(video);
    const uint8_t glyph = lcd_character(video, column / scale, y);
    if (scale == 1)
        return glyph;
    static const uint8_t doubled[16] = {
        0x00, 0x03, 0x0c, 0x0f, 0x30, 0x33, 0x3c, 0x3f,
        0xc0, 0xc3, 0xcc, 0xcf, 0xf0, 0xf3, 0xfc, 0xff
    };
    return doubled[(column & 1) ? (glyph & 15) : (glyph >> 4)];
}

static uint32_t
lcd_normal_clock(ibm5140_video_t *video)
{
    const double remaining = (double) timer_get_remaining_u64(&video->timer) / (double) TIMER_USEC;
    const double duration = video->normal_sync ? LCD_SYNC_USEC : LCD_ACTIVE_USEC;
    const double elapsed = remaining < duration ? duration - remaining : 0;
    unsigned position = (unsigned) (elapsed * LCD_FRAME_CLOCKS / LCD_FRAME_USEC);
    if (video->normal_sync)
        position += LCD_DATA_CLOCKS;
    if (position >= LCD_FRAME_CLOCKS)
        position = LCD_FRAME_CLOCKS - 1;
    return position;
}

static uint8_t
lcd_feedback(ibm5140_video_t *video)
{
    const unsigned clock = (video->function & LCD_NORMAL) ? lcd_normal_clock(video) : video->diagnostic_clock;
    if (clock >= LCD_DATA_CLOCKS || !(video->function & LCD_SYNCS))
        return 0;
    const unsigned line = clock / LCD_LINE_CLOCKS;
    const unsigned nibble = (clock % LCD_LINE_CLOCKS) / 3;
    const unsigned y = line + ((nibble & 2) ? LCD_HEIGHT / 2 : 0);
    const uint8_t data = lcd_byte(video, nibble / 4, y);
    return (nibble & 1) ? (data & 15) : (data >> 4);
}

static void
lcd_present(ibm5140_video_t *video)
{
    if (video->crt)
        return;
    monitor_t *monitor = &monitors[video->monitor];
    const int enabled = (video->function & (LCD_POWER | LCD_SYNCS)) == (LCD_POWER | LCD_SYNCS) &&
                        (video->mode & 8);
    const uint8_t (*const next_level)[LCD_LEVELS] = video->next_level;
    const uint32_t *const level_color             = video->level_color;
    uint8_t              *level                   = video->level;
    video_wait_for_buffer_monitor(video->monitor);
    for (unsigned y = 0; y < LCD_HEIGHT; y++) {
        uint32_t *line = monitor->target_buffer->line[y];
        for (unsigned column = 0; column < LCD_WIDTH / 8; column++) {
            const uint8_t data = enabled ? lcd_byte(video, column, y) : 0;
            for (unsigned bit = 0; bit < 8; bit++) {
                const uint8_t next = next_level[(data & (0x80 >> bit)) != 0][*level];
                *level++           = next;
                line[column * 8 + bit] = level_color[next];
            }
        }
    }
    if (monitor->mon_xsize != LCD_WIDTH || monitor->mon_ysize != LCD_HEIGHT ||
        video_force_resize_get_monitor(video->monitor)) {
        monitor->mon_xsize = LCD_WIDTH;
        monitor->mon_ysize = LCD_HEIGHT;
        set_screen_size_monitor(LCD_WIDTH, LCD_HEIGHT, video->monitor);
        video_force_resize_set_monitor(0, video->monitor);
    }
    video_blit_memtoscreen_monitor(0, 0, LCD_WIDTH, LCD_HEIGHT, video->monitor);
    monitor->mon_res_x = LCD_WIDTH;
    monitor->mon_res_y = LCD_HEIGHT;
    monitor->mon_bpp = 1;
    frames++;
}

static void
lcd_poll(void *priv)
{
    ibm5140_video_t *video = priv;
    video->normal_sync ^= 1;
    timer_advance_u64(&video->timer, (uint64_t) ((video->normal_sync ? LCD_SYNC_USEC : LCD_ACTIVE_USEC) * TIMER_USEC));
    if (!video->normal_sync) {
        if ((video->function & (LCD_NORMAL | LCD_SYNCS)) == (LCD_NORMAL | LCD_SYNCS))
            video->blink++;
        lcd_present(video);
    }
}

static void
lcd_mapping(ibm5140_video_t *video)
{
    const uint32_t address = (video->function & 2) ? 0xb8000 : 0xb0000;
    mem_mapping_set_addr(&video->mapping, address, LCD_VRAM_SIZE);
    /* The attached CRT owns B8000; firmware configures the LCD as mono.
     * Do not let a conflicting LCD selection shadow the external SRAM. */
    if ((video->function & LCD_DECODE) && !(video->crt && (video->function & 2)))
        mem_mapping_enable(&video->mapping);
    else
        mem_mapping_disable(&video->mapping);
}

static uint8_t
lcd_read(uint32_t address, void *priv)
{
    const ibm5140_video_t *video = priv;
    address &= LCD_VRAM_SIZE - 1;
    if (video->function & LCD_FONT_ACCESS)
        return address < LCD_FONT_SIZE ? video->font[address] : 0xff;
    return video->vram[address];
}

static void
lcd_write(uint32_t address, uint8_t value, void *priv)
{
    ibm5140_video_t *video = priv;
    address &= LCD_VRAM_SIZE - 1;
    if (video->function & LCD_FONT_ACCESS) {
        /* The documented/tested font view is 8K. Undocumented upper
         * aperture decoding is left open, not mirrored into the fonts. */
        if (address < LCD_FONT_SIZE)
            video->font[address] = value;
    } else
        video->vram[address] = value;
}

static uint8_t
lcd_substitution_read(uint32_t address, void *priv)
{
    const ibm5140_video_t *video = priv;
    return video->vram[address & (LCD_VRAM_SIZE - 1)];
}

static void
lcd_substitution_write(uint32_t address, uint8_t value, void *priv)
{
    ibm5140_video_t *video = priv;
    video->vram[address & (LCD_VRAM_SIZE - 1)] = value;
}

void
ibm5140_video_substitute(ibm5140_video_t *video, int enabled)
{
    if (enabled)
        mem_mapping_enable(&video->substitution_mapping);
    else
        mem_mapping_disable(&video->substitution_mapping);
}

static uint8_t
crt_read(uint32_t address, void *priv)
{
    const ibm5140_video_t *video = priv;
    return video->crt->vram[address & (LCD_VRAM_SIZE - 1)];
}

static void
crt_write(uint32_t address, uint8_t value, void *priv)
{
    ibm5140_video_t *video = priv;
    video->crt->vram[address & (LCD_VRAM_SIZE - 1)] = value;
}

static uint8_t
lcd_in(uint16_t port, void *priv)
{
    ibm5140_video_t *video = priv;
    if (port == 0x74)
        return video->control_index;
    if (port == 0x75) {
        if (video->control_index == 0)
            /* External-only profiles have the detachable LCD removed. Keeping
             * its sense asserted makes POST select the hidden LCD over CRT. */
            return video->function | (video->crt ? 0 : LCD_SENSE);
        if (video->control_index == 1)
            return (video->diagnostic & 0xf0) | lcd_feedback(video);
        return 0xff;
    }
    if (video->crt && (port & 0xfff0) == 0x3d0) {
        /* The Convertible adapter exposes only the cursor address
         * registers for readback; unlike the LCD it is not all R/W. */
        switch (port) {
            case 0x3d1:
            case 0x3d5:
                return (video->crt->crtcreg == 0x0e || video->crt->crtcreg == 0x0f) ?
                       video->crt->crtc[video->crt->crtcreg] : 0xff;
            case 0x3da:
                return video->crt->cgastat & 9;
            default:
                return 0xff;
        }
    }
    const unsigned base = (video->function & 2) ? 0x3d0 : 0x3b0;
    if (!(video->function & LCD_DECODE) || (port & 0xfff0) != base)
        return 0xff;
    if ((port & 15) < 8)
        return (port & 1) ? video->regs[video->display_index] : video->display_index;
    if ((port & 15) == 8)
        return video->mode;
    if ((port & 15) == 10) {
        video->status_toggle ^= 1;
        const int sync = (video->function & LCD_NORMAL) ? video->normal_sync :
                         video->diagnostic_clock >= LCD_DATA_CLOCKS;
        return video->status_toggle | ((sync && (video->function & LCD_SYNCS)) ? 8 : 0);
    }
    return 0xff;
}

static void
lcd_out(uint16_t port, uint8_t value, void *priv)
{
    ibm5140_video_t *video = priv;
    if (port == 0x74) {
        video->control_index = value;
        return;
    }
    if (port == 0x75) {
        if (video->control_index == 0) {
            const uint8_t old = video->function;
            video->function = value & 0x7f;
            if ((old & LCD_NORMAL) && !(value & LCD_NORMAL)) {
                video->diagnostic_clock = LCD_FRAME_CLOCKS - LCD_FETCH_CLOCKS;
                video->diagnostic = 0;
            }
            if ((old ^ value) & (2 | LCD_DECODE | LCD_FONT_ACCESS))
                lcd_mapping(video);
        } else if (video->control_index == 1) {
            /* One master clock per high-to-low diagnostic clock edge.
             * Polling data alone cannot advance the diagnostic raster. */
            if (!(video->function & LCD_NORMAL) && (video->function & LCD_SYNCS) &&
                (video->diagnostic & 1) && !(value & 1)) {
                if (++video->diagnostic_clock == LCD_FRAME_CLOCKS) {
                    video->diagnostic_clock = 0;
                    video->blink++;
                }
            }
            video->diagnostic = value;
        }
        return;
    }
    if (video->crt && (port & 0xfff0) == 0x3d0) {
        switch (port) {
            case 0x3d0:
            case 0x3d1:
            case 0x3d4:
            case 0x3d5:
            case 0x3d8:
            case 0x3d9:
                cga_out(port, value, video->crt);
                break;
            default:
                break;
        }
        return;
    }
    const unsigned base = (video->function & 2) ? 0x3d0 : 0x3b0;
    if (!(video->function & LCD_DECODE) || (port & 0xfff0) != base)
        return;
    if ((port & 15) < 8) {
        if (port & 1)
            video->regs[video->display_index] = value;
        else
            video->display_index = value & 31;
    } else if ((port & 15) == 8)
        video->mode = value;
    /* 3x9 and 3xB..F are reserved; legacy color writes do not recolor LCD pels. */
}

void
ibm5140_video_reset(ibm5140_video_t *video)
{
    memset(video->regs, 0, sizeof(video->regs));
    video->control_index = 0;
    video->function = 0;
    video->diagnostic = 0;
    video->display_index = 0;
    video->mode = 0;
    video->status_toggle = 0;
    video->normal_sync = 1;
    video->diagnostic_clock = LCD_FRAME_CLOCKS - LCD_FETCH_CLOCKS;
    video->blink = 0;
    memset(video->level, 0, sizeof(video->level));
    ibm5140_video_substitute(video, 0);
    lcd_mapping(video);
    timer_set_delay_u64(&video->timer, (uint64_t) (LCD_SYNC_USEC * TIMER_USEC));

    if (video->crt) {
        cga_t *crt = video->crt;
        memset(crt->crtc, 0, sizeof(crt->crtc));
        memset(crt->charbuffer, 0, sizeof(crt->charbuffer));
        crt->crtcreg = 0;
        crt->cgastat = 0;
        crt->cgamode = 0;
        crt->cgacol = 0;
        crt->lp_strobe = 0;
        crt->linepos = crt->displine = crt->scanline = crt->vc = 0;
        crt->cgadispon = crt->cursorvisible = crt->cursoron = crt->cgablink = 0;
        crt->vsynctime = crt->vadj = 0;
        crt->memaddr = crt->memaddr_backup = 0;
        crt->oddeven = 0;
        crt->firstline = 1000;
        crt->lastline = crt->drawcursor = 0;
        crt->lp_latch_found = false;
        crt->fullchange = monitors[video->monitor].mon_changeframecount;
        cga_recalctimings(crt);
        update_cga16_color(0, 0);
        timer_set_delay_u64(&crt->timer, crt->dispofftime);
    }
    video_force_resize_set_monitor(1, video->monitor);
}

static void
lcd_reset(void *priv)
{
    ibm5140_video_reset(priv);
}

static void
lcd_close(void *priv)
{
    ibm5140_video_t *video = priv;
    timer_disable(&video->timer);
    io_removehandler(0x74, 2, lcd_in, NULL, NULL, lcd_out, NULL, NULL, video);
    io_removehandler(0x3b0, 16, lcd_in, NULL, NULL, lcd_out, NULL, NULL, video);
    io_removehandler(0x3d0, 16, lcd_in, NULL, NULL, lcd_out, NULL, NULL, video);
    mem_mapping_disable(&video->mapping);
    mem_mapping_disable(&video->substitution_mapping);
    /* The separately registered CGA lifecycle owns/free its CRT SRAM. */
    free(video);
}

static const device_t lcd_device = {
    .name = "IBM PC Convertible LCD controller",
    .internal_name = "ibm5140_lcd",
    .flags = DEVICE_ISA,
    .close = lcd_close,
    .reset = lcd_reset
};

ibm5140_video_t *
ibm5140_video_create(int display, int composite)
{
    ibm5140_video_t *video = calloc(1, sizeof(*video));
    if (!video)
        fatal("Unable to allocate IBM 5140 LCD controller\n");
    video->monitor = 0;
    video->normal_sync = 1;
    video->diagnostic_clock = LCD_FRAME_CLOCKS - LCD_FETCH_CLOCKS;
    device_add_ex(&lcd_device, video);
    mem_mapping_add(&video->mapping, 0xb0000, LCD_VRAM_SIZE,
                    lcd_read, NULL, NULL, lcd_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, video);
    mem_mapping_disable(&video->mapping);
    /* Added after conventional RAM so this INTERNAL mapping wins while
     * enabled. V2 p2-9 names MEM_SUBMODE; POST 04DE tests 16K at segment 0. */
    mem_mapping_add(&video->substitution_mapping, 0, LCD_VRAM_SIZE,
                    lcd_substitution_read, NULL, NULL, lcd_substitution_write,
                    NULL, NULL, video->vram, MEM_MAPPING_INTERNAL, video);
    mem_mapping_disable(&video->substitution_mapping);
    io_sethandler(0x74, 2, lcd_in, NULL, NULL, lcd_out, NULL, NULL, video);
    io_sethandler(0x3b0, 16, lcd_in, NULL, NULL, lcd_out, NULL, NULL, video);
    io_sethandler(0x3d0, 16, lcd_in, NULL, NULL, lcd_out, NULL, NULL, video);
    timer_add(&video->timer, lcd_poll, video, 1);

    monitor_t *monitor = &monitors[video->monitor];
    if (display >= 3 && display <= 5) {
        cga_t *crt = calloc(1, sizeof(*crt));
        if (!crt)
            fatal("Unable to allocate IBM 5140 CRT adapter\n");
        crt->vram = calloc(1, LCD_VRAM_SIZE);
        if (!crt->vram)
            fatal("Unable to allocate IBM 5140 CRT SRAM\n");
        video->crt = crt;
        crt->monitor_used = video->monitor;
        crt->composite = display == 5;
        crt->revision = (composite >= 0 && composite <= 2) ? composite : 1;
        crt->rgb_type = display == 3 ? 3 : 5;
        crt->firstline = 1000;
        /* PCjr's existing converter is the old-CGA filter with a black
         * boundary, not revision=2 coerced to the new-CGA boolean. */
        cga_comp_init(crt->revision == CGA_COMPOSITE_PCJR ? 0 : crt->revision);
        cga_recalctimings(crt);
        video_load_font(FONT_IBM_MDA_437_PATH, FONT_FORMAT_MDA, LOAD_FONT_NO_OFFSET);
        *monitor->mon_cga_palette = crt->composite ? 0 : crt->rgb_type * 2;
        cgapal_rebuild_monitor(video->monitor);
        monitor->mon_overscan_x = monitor->mon_overscan_y = 16;
        monitor->mon_composite = !!crt->composite;
        video_inform_monitor(VIDEO_FLAG_TYPE_CGA, &crt_timings, video->monitor);
        mem_mapping_add(&crt->mapping, 0xb8000, LCD_VRAM_SIZE,
                        crt_read, NULL, NULL, crt_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, video);
        timer_add(&crt->timer, cga_poll, crt, 1);
        /* Reuse CGA scan timing, rendering and lifecycle without installing
         * a second set of generic CGA I/O/memory handlers. */
        device_add_ex(&cga_device, crt);
    } else {
        monitor->mon_overscan_x = monitor->mon_overscan_y = 0;
        monitor->mon_composite = 0;
        video_inform_monitor(VIDEO_FLAG_TYPE_CGA, &lcd_timings, video->monitor);
        lcd_build_response(video, display);
    }
    return video;
}

/* Only powered SRAM is retained. The BIOS saves its controller state in
 * font SRAM and restores it after the board's power-on reset. */
int
ibm5140_video_save(ibm5140_video_t *video, FILE *file)
{
    if (!video || !file)
        return 0;
    const uint8_t crt_present = video->crt != NULL;
    if (fwrite(&crt_present, sizeof(crt_present), 1, file) != 1 ||
        fwrite(video->vram, sizeof(video->vram), 1, file) != 1 ||
        fwrite(video->font, sizeof(video->font), 1, file) != 1)
        return 0;
    return !video->crt || fwrite(video->crt->vram, LCD_VRAM_SIZE, 1, file) == 1;
}

int
ibm5140_video_load(ibm5140_video_t *video, FILE *file)
{
    if (!video || !file)
        return 0;
    uint8_t crt_present;
    if (fread(&crt_present, sizeof(crt_present), 1, file) != 1 ||
        crt_present != (video->crt != NULL))
        return 0;
    const size_t size = LCD_VRAM_SIZE + LCD_FONT_SIZE + (crt_present ? LCD_VRAM_SIZE : 0);
    uint8_t *sram = malloc(size);
    if (!sram)
        return 0;
    /* Do not mutate hardware on a truncated/failed retention read. */
    if (fread(sram, size, 1, file) != 1) {
        free(sram);
        return 0;
    }
    memcpy(video->vram, sram, LCD_VRAM_SIZE);
    memcpy(video->font, sram + LCD_VRAM_SIZE, LCD_FONT_SIZE);
    if (video->crt)
        memcpy(video->crt->vram, sram + LCD_VRAM_SIZE + LCD_FONT_SIZE, LCD_VRAM_SIZE);
    free(sram);
    return 1;
}
