/*
 * 86Box integration for the PC-9801 video core.
 *
 * This device maps the standard low-resolution PC-98 video apertures and I/O
 * ports, and loads the PC-9801VX banked IPL plus a 0x46800-byte font
 * dump.  The VX exposes a 96 KiB CPU window into a 192 KiB IPL region: bank 0
 * contains the ITF reset firmware, bank 1 the main BIOS.  The PC-9801VX
 * motherboard integration lives in m_pc98.c; this
 * file owns the display-side memory map, firmware mappings and presentation.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/rom.h>
#include <86box/pc98_rom.h>
#include <86box/pc98_bios_patch.h>
#include <86box/timer.h>
#include <86box/vid_pc98.h>
#include <86box/video.h>
#include <86box/ui.h>
#include "cpu.h"

#define PC98_FONT_PATH            "roms/machines/pc9801/font.rom"
#define PC98_KANJI_X01_PATH       "roms/machines/pc9801/24256c-x01.bin"
#define PC98_KANJI_X02_PATH       "roms/machines/pc9801/24256c-x02.bin"
#define PC98_KANJI_X03_PATH       "roms/machines/pc9801/24256c-x03.bin"
#define PC98_KANJI_X04_PATH       "roms/machines/pc9801/24256c-x04.bin"

#define PC98_DEFAULT_ACTIVE_START 25u
#define PC98_DEFAULT_ACTIVE_END   425u
#define PC98_DEFAULT_TOTAL_LINES  440u
#define PC98_DEFAULT_VSYNC_LINES  8u
/* NEC Technical Data Book, hardware (1993), pp. 349/351: 21.0526 MHz
 * video clock, 30.4 us active and 40.28 us total. Keep the published clock
 * as an integer; text 2xWCLK is dot/4, graphics is dot/8 or dot/4. */
#define PC98_DOT_CLOCK_HZ 21052600u

typedef struct pc98_86box_t pc98_86box_t;

typedef struct pc98_plane_map_t {
    pc98_86box_t *dev;
    uint8_t       plane;
} pc98_plane_map_t;

struct pc98_86box_t {
    pc98_video_t     core;
    pc_timer_t       scan_timer;
    uint32_t         frame_no;
    uint16_t         raster_line;
    pc_timer_t       gdc_timer;
    uint64_t         gdc_last_tsc;
    uint64_t         gdc_timer_usec;
    uint128_t        gdc_clock_remainder[2];
    uint128_t        gdc_clock_denominator[2];
    uint32_t         raster_clock_remainder;
    uint8_t          in_vsync;
    uint8_t          vrtc_armed;
    uint8_t          raster_phase;
    uint8_t          need_resize;
    uint8_t          display_on; /* last presented state of video FF 7 */
    uint8_t          logged_state;
    uint8_t         *ipl;
    uint8_t         *firmware_banks;
    pc98_model_t     model;
    uint8_t          ipl_bank;
    uint8_t          flash_bank;
    mem_mapping_t    ipl_low_map;
    mem_mapping_t    tvram_map;
    mem_mapping_t    cg_map;
    mem_mapping_t    gvram_map[4];
    mem_mapping_t    ipl_high_map;
    mem_mapping_t    ipl_reset_map;
    pc98_plane_map_t plane_map[4];
};

static void pc98_gdc_sync(pc98_86box_t *d);
static void pc98_gdc_schedule(pc98_86box_t *d);
static uint8_t
tvram_read_lane(uint32_t addr, pc98_86box_t *d)
{
    uint32_t off = (addr - 0xa0000u) & 0x3fffu;

    /* A3FE2-A3FFF is the firmware memory-switch window.  It occupies the
     * low byte of each word; the high byte remains the ordinary nonexistent
     * attribute lane. */
    if (off >= 0x3fe2u) {
        if (off & 1u)
            return 0xff;
        return d->core.memsw[(off >> 1) & 0x0fu];
    }

    /* Attribute RAM has no odd byte lane on real PC-98 hardware. */
    if (off >= 0x2000u && (off & 1u))
        return 0xff;

    uint16_t w = d->core.tvram[(off >> 1) & 0x1fffu];
    return (off & 1u) ? (uint8_t) (w >> 8) : (uint8_t) w;
}

static uint8_t
tvram_rb(uint32_t addr, void *priv)
{
    pc98_86box_t *d = priv;
    pc98_gdc_sync(d);
    uint8_t value = tvram_read_lane(addr, d);
    return value;
}

static uint16_t
tvram_rw(uint32_t addr, void *priv)
{
    pc98_86box_t *d = priv;
    if (addr & 1u) {
        uint8_t lo = tvram_rb(addr, priv);
        return (uint16_t) (lo | ((uint16_t) mem_readb_phys(addr + 1u) << 8));
    }
    pc98_gdc_sync(d);
    uint16_t value = tvram_read_lane(addr, d) | ((uint16_t) tvram_read_lane(addr + 1u, d) << 8);
    return value;
}

static void
tvram_write_lane(uint32_t addr, uint8_t val, pc98_86box_t *d)
{
    uint32_t off = (addr - 0xa0000u) & 0x3fffu;
    if (off >= 0x3fe2u) {
        if (!(off & 1u) && d->core.video_ff[6])
            d->core.memsw[(off >> 1) & 0x0fu] = val;
        return;
    }
    if (off >= 0x2000u && (off & 1u))
        return;

    uint16_t *p = &d->core.tvram[(off >> 1) & 0x1fffu];
    if (off & 1u)
        *p = (uint16_t) ((*p & 0x00ffu) | ((uint16_t) val << 8));
    else
        *p = (uint16_t) ((*p & 0xff00u) | val);
}

static void
tvram_wb(uint32_t addr, uint8_t val, void *priv)
{
    pc98_86box_t *d = priv;
    pc98_gdc_sync(d);
    tvram_write_lane(addr, val, d);
}

static void
tvram_ww(uint32_t addr, uint16_t val, void *priv)
{
    pc98_86box_t *d = priv;
    if (addr & 1u) {
        tvram_wb(addr, (uint8_t) val, priv);
        mem_writeb_phys(addr + 1u, (uint8_t) (val >> 8));
        return;
    }
    pc98_gdc_sync(d);
    tvram_write_lane(addr, (uint8_t) val, d);
    tvram_write_lane(addr + 1u, (uint8_t) (val >> 8), d);
}

/*
 * A4000h character-generator window.  Scanline comes from address bits
 * 4:1.  Ordinary Kanji use address bit 0 as the left/right byte lane; the
 * 56xx/57xx programmable area instead uses the A5h L/R latch.  Keep this
 * distinction in the shared core decoder so the port and MMIO paths cannot
 * silently grow different address maps again.
 */
static uint32_t
cg_window_offset(const pc98_video_t *v, uint32_t mmio_off,
                 int *writable)
{
    return pc98_cg_window_offset(v, mmio_off, writable);
}

static uint8_t
cg_rb(uint32_t addr, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;
    pc98_video_t *v = &d->core;
    if (!v->font_rom)
        return 0xff;

    const uint32_t off = (addr - 0xa4000u) & 0xfffu;
    const uint32_t idx = cg_window_offset(v, off, NULL);

    if (idx >= PC98_FONT_ADDRESS_SIZE)
        return 0xffu;
    /* An ANK selection indexes the compact chargen banks, which are shorter
     * than the address space. */
    if (!(v->font_addr & 0x00ffu) && idx >= v->font_dump_size)
        return 0xffu;

    return v->font_rom[idx];
}

static uint16_t
cg_rw(uint32_t addr, void *priv)
{
    return (uint16_t) (cg_rb(addr, priv) | ((uint16_t) cg_rb(addr + 1u, priv) << 8));
}

static void
cg_wb(uint32_t addr, uint8_t val, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;
    pc98_video_t *v = &d->core;
    if (!v->font_rom)
        return;

    const uint32_t off      = (addr - 0xa4000u) & 0xfffu;
    int            writable = 0;
    const uint32_t idx      = cg_window_offset(v, off, &writable);

    /* Only the 56xx/57xx programmable area is RAM. */
    if (writable && idx < PC98_FONT_ADDRESS_SIZE)
        v->font_rom[idx] = val;
}

static void
cg_ww(uint32_t addr, uint16_t val, void *priv)
{
    /* Model the word access as the two CPU byte-lane cycles the hardware
     * sees.  For ordinary Kanji they select opposite halves; for 56xx/57xx
     * PCG both lanes use the A5h L/R latch and therefore target the same
     * selected half. */
    cg_wb(addr, (uint8_t) val, priv);
    cg_wb(addr + 1u, (uint8_t) (val >> 8), priv);
}

static uint8_t
gvram_rb(uint32_t addr, void *priv)
{
    pc98_plane_map_t *m = (pc98_plane_map_t *) priv;
    pc98_gdc_sync(m->dev);
    uint16_t off   = (uint16_t) (addr & 0x7fffu);
    uint8_t  value = pc98_gvram_read8(&m->dev->core, m->plane, off);
    return value;
}

static uint16_t
gvram_rw(uint32_t addr, void *priv)
{
    pc98_plane_map_t *m = (pc98_plane_map_t *) priv;
    if (addr & 1) {
        /* The VX has a 16-bit bus: an unaligned word is two byte cycles.
         * Dispatch the second cycle through its actual aperture, including
         * accesses crossing a plane or ROM boundary. */
        uint8_t lo = gvram_rb(addr, priv);
        uint8_t hi = mem_readb_phys(addr + 1u);
        return (uint16_t) (lo | ((uint16_t) hi << 8));
    }
    pc98_gdc_sync(m->dev);
    uint16_t off   = (uint16_t) ((addr & 0x7fffu) >> 1);
    uint16_t value = pc98_gvram_read16(&m->dev->core, m->plane, off, 0xffff);
    return value;
}

static void
gvram_wb(uint32_t addr, uint8_t val, void *priv)
{
    pc98_plane_map_t *m = (pc98_plane_map_t *) priv;
    pc98_gdc_sync(m->dev);
    uint16_t off = (uint16_t) (addr & 0x7fffu);
    pc98_gvram_write8(&m->dev->core, m->plane, off, val);
}

static void
gvram_ww(uint32_t addr, uint16_t val, void *priv)
{
    pc98_plane_map_t *m = (pc98_plane_map_t *) priv;
    if (addr & 1) {
        gvram_wb(addr, (uint8_t) val, priv);
        mem_writeb_phys(addr + 1u, (uint8_t) (val >> 8));
        return;
    }
    pc98_gdc_sync(m->dev);
    uint16_t off = (uint16_t) ((addr & 0x7fffu) >> 1);
    pc98_gvram_write16(&m->dev->core, m->plane, off, val, 0xffff);
}

static uint8_t
pc98_inb(uint16_t port, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;
    pc98_gdc_sync(d);
    uint8_t value = pc98_video_in8(&d->core, port);
    pc98_gdc_schedule(d);
    return value;
}

static void
pc98_outb(uint16_t port, uint8_t val, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;
    pc98_gdc_sync(d);
    pc98_video_out8(&d->core, port, val);
    pc98_gdc_schedule(d);
}

static uint16_t
pc98_inw(uint16_t port, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;
    pc98_gdc_sync(d);
    uint16_t value = pc98_video_in16(&d->core, port);
    pc98_gdc_schedule(d);
    return value;
}

static void
pc98_outw(uint16_t port, uint16_t val, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;
    pc98_gdc_sync(d);
    pc98_video_out16(&d->core, port, val);
    pc98_gdc_schedule(d);
}

static uint8_t
ipl_rb(uint32_t addr, void *priv)
{
    pc98_86box_t *d   = (pc98_86box_t *) priv;
    uint32_t      off = pc98_ipl_window_offset_for_model(addr, d->model);
    if (!d->ipl || off >= PC98_IPL_WINDOW_SIZE)
        return 0xff;
    if (d->firmware_banks && d->flash_bank < 8 && off >= 0x10000u)
        return d->firmware_banks[d->flash_bank * PC98_BX4_BANK_SIZE + off - 0x10000u];
    return d->ipl[(uint32_t) d->ipl_bank * PC98_IPL_WINDOW_SIZE + off];
}

static uint16_t
ipl_rw(uint32_t addr, void *priv)
{
    return (uint16_t) (ipl_rb(addr, priv) | ((uint16_t) ipl_rb(addr + 1u, priv) << 8));
}

static uint32_t
ipl_rl(uint32_t addr, void *priv)
{
    return (uint32_t) ipl_rw(addr, priv) | ((uint32_t) ipl_rw(addr + 2u, priv) << 16);
}

static void
ipl_bank_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;
    (void) port;
    if (((val & 0xf0u) == 0x00u || (val & 0xf0u) == 0x10u) && ((val & 0xedu) == 0x00u)) {
        uint8_t bank = (val >> 1) & 1u;
        if (bank != d->ipl_bank)
            pclog("[PC98] IPL bank %u -> %u (43D=%02X)\n", d->ipl_bank, bank, val);
        d->ipl_bank   = bank;
        d->flash_bank = 0xffu;
    }
}

static void
pc98_flash_bank_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_86box_t *d = priv;
    (void) port;
    /* getitf98's documented E0,E2,...EE logical bank selection at F8000h. */
    if ((val & 0xf1u) == 0xe0u)
        d->flash_bank = (val >> 1) & 7u;
}

static int
load_font_from_rom_path(pc98_video_t *v)
{
    uint8_t                 *dump     = (uint8_t *) malloc(PC98_FONT_DUMP_SIZE);
    uint8_t                 *raw      = (uint8_t *) malloc(PC98_RAW_KANJI_SIZE);
    uint8_t                 *part     = (uint8_t *) malloc(0x8000u);
    static const char *const paths[4] = {
        PC98_KANJI_X01_PATH, PC98_KANJI_X02_PATH,
        PC98_KANJI_X03_PATH, PC98_KANJI_X04_PATH
    };
    int ok = 0;

    if (!dump || !raw || !part)
        goto done;
    if (!pc98_rom_read_exact(PC98_FONT_PATH, dump, PC98_FONT_DUMP_SIZE))
        goto done;

    /* Reproduce MAME's ROM_LOAD16_BYTE + ROM_CONTINUE layout.  X01/X02
     * supply even/odd bytes at 00000h and 20000h; X03/X04 do the same at
     * 40000h and 60000h. */
    memset(raw, 0xff, PC98_RAW_KANJI_SIZE);
    for (unsigned rom = 0; rom < 4u; rom++) {
        const size_t bank = (rom >> 1) ? 0x40000u : 0u;
        const size_t lane = rom & 1u;
        if (!pc98_rom_read_exact(paths[rom], part, 0x8000u))
            goto done;
        for (unsigned half = 0; half < 2u; half++)
            for (unsigned i = 0; i < 0x4000u; i++)
                raw[bank + half * 0x20000u + lane + i * 2u] = part[half * 0x4000u + i];
    }

    ok = pc98_video_install_font_set(v, dump, PC98_FONT_DUMP_SIZE,
                                     raw, PC98_RAW_KANJI_SIZE)
        == 0;
done:
    free(part);
    free(raw);
    free(dump);
    return ok;
}

static const video_timings_t pc98_timings = {
    .type    = VIDEO_ISA,
    .write_b = 4,
    .write_w = 8,
    .write_l = 16,
    .read_b  = 4,
    .read_w  = 8,
    .read_l  = 16
};

static uint32_t
pc98_rgb(unsigned pen)
{
    unsigned bright = (pen & 8u) ? 0x55u : 0u;
    unsigned b      = (pen & 1u) ? 0xaau + bright : bright;
    unsigned r      = (pen & 2u) ? 0xaau + bright : bright;
    unsigned g      = (pen & 4u) ? 0xaau + bright : bright;
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;
    return makecol((uint8_t) r, (uint8_t) g, (uint8_t) b);
}

static uint32_t
pc98_graphics_rgb(const pc98_video_t *v, unsigned pen)
{
    if (v->ex_video_ff[0]) {
        const uint8_t *rgb = v->analog_palette[pen & 0x0fu];
        return makecol((uint8_t) (rgb[0] * 0x11u),
                       (uint8_t) (rgb[1] * 0x11u),
                       (uint8_t) (rgb[2] * 0x11u));
    }

    return pc98_rgb(pen);
}

/* NP2/W's text palette is not the 0xAA digital graphics palette.
 * Its pal_maketext() uses the eight degpal1[] entries, and degpal1[] is
 * generated with each asserted B/R/G bit at full 0xFF intensity.  Keep this
 * separate from pc98_rgb(): the latter is still used by the bitmap layer. */
static uint32_t
pc98_text_rgb(unsigned color)
{
    const uint8_t b = (color & 1u) ? 0xffu : 0u;
    const uint8_t r = (color & 2u) ? 0xffu : 0u;
    const uint8_t g = (color & 4u) ? 0xffu : 0u;
    return makecol(r, g, b);
}

static uint32_t
pc98_font_offset(const pc98_video_t *v, uint16_t code,
                 unsigned line, unsigned right, unsigned lr)
{
    if (code & 0xff00u)
        return ((((uint32_t) code & 0x7f7fu) << 5) + ((line & 15u) << 1) + (right & 1u)) & (PC98_FONT_ADDRESS_SIZE - 1u);

    /* FONTSEL=0 uses the 8x8 SBCS block at 0000h.  With LR>=16 PC-98
     * doubles its scanlines vertically.  FONTSEL=1 uses the 8x16 block. */
    if (v->video_ff[3])
        return (0x800u + (uint32_t) (code & 0xffu) * 16u + (line & 15u)) & (PC98_FONT_ADDRESS_SIZE - 1u);
    return ((uint32_t) (code & 0xffu) * 8u + ((lr >= 16u) ? ((line >> 1) & 7u) : (line & 7u))) & (PC98_FONT_ADDRESS_SIZE - 1u);
}

static uint8_t
pc98_semigraphics_bits(uint8_t tile, unsigned line, unsigned char_size)
{
    uint8_t        bits = 0;
    const unsigned tall = (char_size == 16u);

    for (unsigned sx = 0; sx < 8u; sx++) {
        unsigned gfx_bit = sx & 4u;
        gfx_bit += (line & (2u << tall)) >> (1u + tall);
        gfx_bit += (line & (4u << tall)) >> (1u + tall);
        if ((tile >> gfx_bit) & 1u)
            bits |= (uint8_t) (0x80u >> sx);
    }
    return bits;
}

static int
pc98_cursor_phase(const pc98_86box_t *d, const upd7220_t *g)
{
    if (!g->dc)
        return 0;
    if (g->sc)
        return 1;

    /* uPD7220 CCHAR defines cursor on-time and off-time as 2*BR frames.
     * BR=0 is treated as the shortest useful rate rather than divide-by-zero. */
    unsigned br   = g->br ? g->br : 1u;
    unsigned span = 2u * br;
    return ((d->frame_no / span) & 1u) == 0u;
}

static int
pc98_attribute_blink_off(const pc98_86box_t *d, const upd7220_t *g)
{
    unsigned br     = g->br ? g->br : 1u;
    unsigned period = 8u * br;
    unsigned phase  = d->frame_no % period;

    /* Attribute blink is half the cursor rate with a 3/4-on, 1/4-off duty. */
    return phase >= 6u * br;
}

static void pc98_render_graphics_text_area(pc98_86box_t              *d,
                                           const upd7220_t           *g,
                                           const upd7220_partition_t *area,
                                           unsigned                   back_y,
                                           unsigned                   zoom,
                                           unsigned                   lines,
                                           unsigned                   first,
                                           unsigned                   last);

static void
pc98_render_graphics(pc98_86box_t *d, unsigned first, unsigned last)
{
    pc98_video_t    *v            = &d->core;
    const upd7220_t *g            = &v->graph_gdc;
    const unsigned   lr           = g->lr ? g->lr : 1u;
    const unsigned   zoom         = (unsigned) g->disp + 1u;
    const unsigned   interlace    = ((g->mode & 0x09u) == 0x09u) ? 1u : 0u;
    const unsigned   display_mode = g->mode & 0x22u;
    const int        mixed_mode   = (display_mode == 0x00u);
    const int        bitmap_mode  = (display_mode == 0x02u);
    const unsigned   active_lines = g->al ? ((unsigned) g->al << interlace) : 400u;
    unsigned         back_y       = 0;

    if (!g->de || (!mixed_mode && !bitmap_mode))
        return;

    /* Each of the four GDC areas has its own start address, length, memory
     * increment and pitch mode. In mixed mode, IM selects bitmap areas and
     * bitmap memory advances once per scanline. */
    for (unsigned part = 0; part < 4u && back_y < last; part++) {
        upd7220_partition_t area;
        upd7220_get_graphics_partition(g, part, &area);

        const int area_bitmap = bitmap_mode || area.im;
        /* The GDC exposes only three bitmap areas in mixed mode.  Area four
         * may still be text there; the area limit applies when IM selects
         * bitmap data, matching the controller's update path. */
        if (mixed_mode && area.im && part >= 3u)
            break;

        unsigned lines;
        if (area_bitmap) {
            lines = area.len ? area.len : 0x400u;
            if (interlace)
                lines <<= 1;
        } else {
            /* In mixed mode, non-bitmap partitions are text from this GDC.
             * Their zero LEN field is empty (the 1024-line rule is bitmap-only). */
            lines = area.len ? ((area.len + lr - 1u) / lr) * lr : 0u;
            pc98_render_graphics_text_area(d, g, &area, back_y, zoom, lines,
                                           first, last);
            back_y += lines * zoom;
            continue;
        }

        const unsigned pitch_shift = bitmap_mode ? area.im : 1u;
        unsigned       addr_pitch  = g->pitch >> pitch_shift;
        if (!addr_pitch)
            addr_pitch = 40u;
        unsigned display_pitch = g->pitch >> pitch_shift;
        if (!display_pitch)
            display_pitch = 40u;
        unsigned width_words = g->aw ? (g->aw >> pitch_shift) : 40u;
        if (width_words > 40u)
            width_words = 40u;

        unsigned start = first > back_y ? (first - back_y) / zoom : 0u;
        for (unsigned y = start; y < lines && back_y + y * zoom < last; y++) {
            const unsigned y_divisor = mixed_mode ? 1u : lr;
            const uint32_t line_addr = (area.sad & 0x3ffffu) + (y / y_divisor) * addr_pitch;
            for (unsigned z = 0; z < zoom; z++) {
                /* The 640x400 buffer starts at the active picture origin;
                 * VBP belongs to raster timing, not a row offset in this buffer. */
                const unsigned dst_y = back_y + y * zoom + z;
                if (dst_y < first || dst_y >= last || dst_y >= 400u || dst_y >= active_lines)
                    continue;

                for (unsigned w = 0; w < width_words; w++) {
                    const uint32_t wa = (line_addr + (area.wd + 1u) * (w % display_pitch)) & 0x3fffu;
                    const uint16_t pb = v->gvram[v->display_page & 1][PC98_PLANE_B][wa];
                    const uint16_t pr = v->gvram[v->display_page & 1][PC98_PLANE_R][wa];
                    const uint16_t pg = v->gvram[v->display_page & 1][PC98_PLANE_G][wa];
                    const uint16_t pi = v->gvram[v->display_page & 1][PC98_PLANE_E][wa];
                    for (unsigned bit = 0; bit < 16; bit++) {
                        const unsigned x = w * 16u + bit;
                        if (x >= 640u)
                            break;
                        unsigned pen = ((pb >> bit) & 1u) | (((pr >> bit) & 1u) << 1) | (((pg >> bit) & 1u) << 2);
                        if (v->ex_video_ff[0])
                            pen |= ((pi >> bit) & 1u) << 3;
                        buffer32->line[dst_y][x] = pc98_graphics_rgb(v, pen);
                    }
                }
            }
        }

        back_y += lines * zoom;
    }
}

static void
pc98_render_text_scanline(pc98_86box_t *d, const upd7220_t *g,
                          uint32_t base,
                          unsigned pitch, unsigned widthmul,
                          unsigned lr, unsigned char_size,
                          unsigned sy, unsigned y,
                          int cursor_on, int blink_off)
{
    pc98_video_t *v              = &d->core;
    unsigned      last_underline = 0;
    uint16_t      last_pcg       = 0xffffu;

    for (unsigned col = 0; col < pitch;) {
        const uint32_t ta       = (base + col * widthmul) & 0x0fffu;
        uint16_t       tw       = v->tvram[ta];
        uint8_t        lo       = (uint8_t) tw;
        uint8_t        hi       = (uint8_t) (tw >> 8);
        uint16_t       code     = lo;
        unsigned       halves   = 1;
        unsigned       pcg_half = 0;

        if (hi) {
            code = (uint16_t) (((uint16_t) (lo & 0x7fu) << 8) | (hi & 0x7fu));
            /* 56xx/57xx programmable gaiji are stored as 16-pixel strips,
             * but software commonly writes the same code into two adjacent
             * text cells with LR clear in both.  The second occurrence is
             * the right half.  Treating every 56xx/57xx cell as left-half
             * only is what made symbols such as the return-key glyph show
             * the first 8 pixels twice.  This matches the real-PC-98/MAME
             * pairing rule while keeping the attribute address per cell. */
            if ((code & 0x7e00u) == 0x5600u) {
                if (last_pcg == code) {
                    pcg_half = 1;
                    last_pcg = 0xffffu;
                } else {
                    pcg_half = 0;
                    last_pcg = code;
                }
                halves = 1;
            } else if ((code & 0x7c00u) == 0x0800u) {
                halves   = 1;
                last_pcg = 0xffffu;
            } else {
                halves   = 2;
                last_pcg = 0xffffu;
            }
        } else {
            last_pcg = 0xffffu;
        }

        for (unsigned half = 0; half < halves && col + half < pitch; half++) {
            uint32_t attr_addr    = ((ta + half) & 0x0fffu) | 0x1000u;
            uint8_t  attr         = (uint8_t) v->tvram[attr_addr];
            unsigned color        = (attr >> 5) & 7u;
            unsigned reverse      = !!(attr & 0x04u);
            unsigned underline    = !!(attr & 0x08u);
            unsigned hidden       = !(attr & 0x01u);
            unsigned blink        = !!(attr & 0x02u) && blink_off;
            unsigned vline        = !v->video_ff[0] && !!(attr & 0x10u);
            unsigned semigraphics = v->video_ff[0] && !!(attr & 0x10u) && !hi;
            unsigned cursor_here  = cursor_on && (((g->ead & 0x0fffu) == ta) || (halves == 2u && ((g->ead + 1u) & 0x0fffu) == ta));

            uint8_t bits = 0;

            if (!hidden) {
                if (hi) {
                    if (sy < 16u) {
                        const unsigned glyph_half = ((code & 0x7e00u) == 0x5600u) ? pcg_half : half;
                        bits                      = v->font_rom[pc98_font_offset(v, code, sy, glyph_half, lr)];
                    }
                } else if (semigraphics) {
                    bits = pc98_semigraphics_bits(lo, sy, char_size);
                } else if (sy < char_size) {
                    bits = v->font_rom[pc98_font_offset(v, code, sy, 0, lr)];
                }
            }

            if (sy == lr - 1u) {
                if (underline)
                    bits = 0x0fu;
                if (last_underline)
                    bits |= 0xf0u;
            }
            if (vline)
                bits |= 0x08u;

            if (cursor_here && sy >= g->ctop && sy <= g->cbot)
                bits ^= 0xffu;
            if (blink)
                bits = 0;
            if (reverse)
                bits ^= 0xffu;

            for (unsigned sx = 0; sx < 8u; sx++) {
                if (!(bits & (0x80u >> sx)))
                    continue;
                const unsigned x = ((col + half) * 8u + sx) * widthmul;
                if (x >= 640u)
                    continue;
                buffer32->line[y][x] = pc98_text_rgb(color);
                if (widthmul == 2u && x + 1u < 640u)
                    buffer32->line[y][x + 1u] = pc98_text_rgb(color);
            }
            last_underline = underline;
        }
        col += halves;
    }
}

static void
pc98_render_graphics_text_area(pc98_86box_t              *d,
                               const upd7220_t           *g,
                               const upd7220_partition_t *area,
                               unsigned                   back_y,
                               unsigned                   zoom,
                               unsigned                   lines,
                               unsigned                   first,
                               unsigned                   last)
{
    pc98_video_t *v = &d->core;
    unsigned      pitch, lr, widthmul, char_size;
    const int     cursor_on = pc98_cursor_phase(d, g);
    const int     blink_off = pc98_attribute_blink_off(d, g);

    if (!g->de || !v->font_rom || !lines)
        return;

    pitch = g->pitch ? g->pitch : 80u;
    if (pitch > 80u)
        pitch = 80u;
    lr = g->lr ? g->lr : 1u;
    if (lr > 32u)
        lr = 32u;
    widthmul  = v->video_ff[2] ? 2u : 1u;
    char_size = v->video_ff[3] ? 16u : 8u;
    if (!v->video_ff[3] && lr >= 16u)
        char_size = 16u;

    const unsigned scroll       = v->txt_scroll[3] & 0x0fu;
    const unsigned scroll_start = (32u - v->txt_scroll[4]) & 0x1fu;
    unsigned       scroll_rows  = (unsigned) v->txt_scroll[5] + 1u;
    if (scroll_rows > 32u)
        scroll_rows = 32u;

    unsigned start = first > back_y ? (first - back_y) / zoom / lr : 0u;
    for (unsigned row = start; row * lr < area->len && back_y + row * lr * zoom < last; row++) {
        const unsigned display_row = (back_y / lr) + row;
        const int      in_scroll   = scroll && display_row >= scroll_start && display_row - scroll_start < scroll_rows;

        for (unsigned sy = 0; sy < lr; sy++) {
            const unsigned source_offset = in_scroll ? sy + scroll : sy;
            const unsigned source_row    = row + source_offset / lr;
            const unsigned source_sy     = source_offset % lr;
            const uint32_t source_base   = (area->sad + source_row * pitch) & 0x0fffu;

            for (unsigned z = 0; z < zoom; z++) {
                const unsigned y = back_y + (row * lr + sy) * zoom + z;
                if (y < first || y >= last || y >= 400u)
                    continue;

                pc98_render_text_scanline(d, g, source_base, pitch, widthmul,
                                          lr, char_size, source_sy, y,
                                          cursor_on, blink_off);
            }
        }
    }
}

static void
pc98_render_text(pc98_86box_t *d, const upd7220_t *g,
                 unsigned first, unsigned last)
{
    pc98_video_t *v = &d->core;
    unsigned      pitch, lr, widthmul, char_size;
    const int     cursor_on = pc98_cursor_phase(d, g);
    const int     blink_off = pc98_attribute_blink_off(d, g);
    unsigned      back_y    = 0;

    if (!g->de || !v->font_rom)
        return;
    pitch = g->pitch ? g->pitch : 80u;
    if (pitch > 80u)
        pitch = 80u;
    lr = g->lr ? g->lr : 16u;
    if (lr > 32u)
        lr = 32u;
    widthmul  = v->video_ff[2] ? 2u : 1u;
    char_size = v->video_ff[3] ? 16u : 8u;
    if (!v->video_ff[3] && lr >= 16u)
        char_size = 16u;

    /* 76h is a four-bit scanline offset (0..15); 78h is the two's-complement
     * top text row and 7Ah stores the scroll-window height minus one. */
    const unsigned scroll       = v->txt_scroll[3] & 0x0fu;
    const unsigned scroll_start = (32u - v->txt_scroll[4]) & 0x1fu;
    unsigned       scroll_rows  = (unsigned) v->txt_scroll[5] + 1u;
    if (scroll_rows > 32u)
        scroll_rows = 32u;

    for (unsigned part = 0; part < 4u && back_y < last; part++) {
        upd7220_partition_t area;
        upd7220_get_text_partition(g, part, &area);
        /* PRAM LEN counts raster lines. Loading a new SAD does not restart
         * the character-row clock. Keep its partial-row phase, including
         * the smooth-scroll carry, across area boundaries. */
        unsigned start = first > back_y ? first - back_y : 0u;
        for (unsigned line = start; line < area.len && back_y + line < last; line++) {
            const unsigned y           = back_y + line;
            const unsigned display_row = y / lr;
            const int      in_scroll   = scroll && display_row >= scroll_start && display_row - scroll_start < scroll_rows;
            const unsigned source_line = (back_y % lr) + line + (in_scroll ? scroll : 0u);
            const uint32_t source_base = (area.sad + (source_line / lr) * pitch) & 0x0fffu;

            pc98_render_text_scanline(d, g, source_base, pitch, widthmul,
                                      lr, char_size, source_line % lr, y,
                                      cursor_on, blink_off);
        }
        /* A zero-length text area is empty and introduces no gap. */
        back_y += area.len;
    }
}

static void
pc98_render_lines(pc98_86box_t *d, unsigned first, unsigned last)
{
    if (!buffer32 || first >= 400u)
        return;
    if (last > 400u)
        last = 400u;
    for (unsigned y = first; y < last; y++)
        memset(buffer32->line[y], 0, 640u * sizeof(uint32_t));

    if (d->core.video_ff[7]) {
        const unsigned graph_mode = d->core.graph_gdc.mode & 0x22u;
        if (graph_mode == 0x20u)
            pc98_render_text(d, &d->core.graph_gdc, first, last);
        else
            pc98_render_graphics(d, first, last);
        pc98_render_text(d, &d->core.text_gdc, first, last);
    }
}

/* The device that currently owns the display.
 *
 * pc98_init() can run more than once for a single emulator session - 86Box
 * initialises the machine again after its first pass - and each run registers
 * a frame timer that lives *inside* the device allocation.  A device that is
 * no longer current must not present: otherwise two timers fight over the
 * frame buffer, and the stale one, whose guest never runs, has display enable
 * clear and so blanks the screen every time it fires.  That is what left the
 * display dead until the first machine reset, which is where 86Box purges the
 * timer list.
 *
 * The check is a pointer comparison only.  A stale device may already have
 * been freed, so the callback must not touch it - and returning without
 * re-arming is what stops the timer for good. */
static pc98_86box_t *pc98_active = NULL;

static int pc98_present_frame(pc98_86box_t *d);

static unsigned
pc98_gdc_clock_divisor(const pc98_86box_t *d, unsigned index)
{
    /* Text always uses the fast clock; graphics uses the physical SW2-8
     * selection reported at PPI port 31h bit 7, not BIOS bookkeeping RAM. */
    return !index || d->core.graph_clock_fast ? 4u : 8u;
}

static uint64_t
pc98_gdc_deadline(const pc98_86box_t *d, unsigned index,
                  uint32_t clocks)
{
    if (!clocks)
        return UINT64_MAX;
    uint128_t clock_denominator = (uint128_t) TIMER_USEC * (1000000u * pc98_gdc_clock_divisor(d, index));
    uint128_t numerator         = clocks * clock_denominator - d->gdc_clock_remainder[index];
    uint128_t denominator       = (uint128_t) PC98_DOT_CLOCK_HZ << 32;
    uint64_t  cpu_clocks        = (uint64_t) ((numerator + denominator - 1) / denominator);
    return cpu_clocks ? cpu_clocks : 1;
}

static void
pc98_gdc_schedule(pc98_86box_t *d)
{
    uint32_t text  = upd7220_next_event(&d->core.text_gdc);
    uint32_t graph = upd7220_next_event(&d->core.graph_gdc);
    timer_disable(&d->gdc_timer);
    if ((!text && !graph) || !TIMER_USEC)
        return;

    /* Round the deadline up to a CPU cycle, since the timer dispatcher uses
     * integer TSC timestamps. Preserve fractional GDC clocks between events. */
    uint64_t text_deadline  = pc98_gdc_deadline(d, 0, text);
    uint64_t graph_deadline = pc98_gdc_deadline(d, 1, graph);
    uint64_t cpu_clocks     = text_deadline < graph_deadline ? text_deadline : graph_deadline;
    timer_set_delay_u64(&d->gdc_timer, cpu_clocks << 32);
}

static void
pc98_gdc_sync(pc98_86box_t *d)
{
    uint64_t elapsed = tsc >= d->gdc_last_tsc ? tsc - d->gdc_last_tsc : 0;
    d->gdc_last_tsc  = tsc;
    for (unsigned index = 0; index < 2; index++) {
        upd7220_t *g           = index ? &d->core.graph_gdc : &d->core.text_gdc;
        uint128_t  denominator = (uint128_t) TIMER_USEC * (1000000u * pc98_gdc_clock_divisor(d, index));
        uint128_t  previous    = d->gdc_clock_denominator[index];
        if (!previous)
            previous = (uint128_t) d->gdc_timer_usec * (1000000u * pc98_gdc_clock_divisor(d, index));
        if (previous) {
            uint128_t numerator           = ((uint128_t) elapsed << 32) * PC98_DOT_CLOCK_HZ + d->gdc_clock_remainder[index];
            uint64_t  clocks              = (uint64_t) (numerator / previous);
            d->gdc_clock_remainder[index] = numerator % previous;
            while (clocks) {
                uint32_t step = clocks > UINT32_MAX ? UINT32_MAX : (uint32_t) clocks;
                upd7220_advance(g, step);
                clocks -= step;
            }
            /* Preserve subclock phase when the CPU speed or graphics divider
             * changes. Normal execution carries the exact division remainder;
             * polling frequency therefore cannot accumulate clock drift. */
            if (previous != denominator) {
                uint128_t fraction            = (d->gdc_clock_remainder[index] << 32) / previous;
                d->gdc_clock_remainder[index] = (fraction * denominator) >> 32;
            }
        }
        d->gdc_clock_denominator[index] = denominator;
    }
    d->gdc_timer_usec = TIMER_USEC;
    pc98_gdc_schedule(d);
}

static void
pc98_gdc_tick(void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;
    if (d == pc98_active)
        pc98_gdc_sync(d);
}

static void
pc98_speed_changed(void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;
    pc98_gdc_sync(d);
}

static void
pc98_begin_vsync(pc98_86box_t *d)
{
    d->in_vsync = 1;
    upd7220_set_retrace(&d->core.text_gdc, 0, 1);
    upd7220_set_retrace(&d->core.graph_gdc, 0, 1);
    /* CRTV fires once after power-on and then once per reset write at 64h.
     * EOI alone cannot request another frame interrupt. */
    if (d->vrtc_armed) {
        d->vrtc_armed = 0;
        picint(1u << 2);
    }
}

void
pc9801_video_vrtc_reset(void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;
    if (!d)
        return;
    picintc(1u << 2);
    d->vrtc_armed = 1;
}

static void
pc98_raster_limits(const pc98_86box_t *d, uint16_t *total_lines,
                   uint16_t *active_start, uint16_t *active_end,
                   uint16_t *vsync_start)
{
    const upd7220_t *g = &d->core.text_gdc;
    if (!g->al || !g->vs)
        g = &d->core.graph_gdc;
    unsigned total  = (unsigned) g->vs + g->vbp + g->al + g->vfp;
    unsigned active = (unsigned) g->vbp + g->al;
    unsigned top    = g->vbp;

    if (!g->al || !g->vs || total <= active || total > 1024u) {
        total        = PC98_DEFAULT_TOTAL_LINES;
        active       = PC98_DEFAULT_ACTIVE_END;
        top          = PC98_DEFAULT_ACTIVE_START;
        *vsync_start = (uint16_t) (total - PC98_DEFAULT_VSYNC_LINES);
    } else {
        *vsync_start = (uint16_t) (total - g->vs);
    }

    *total_lines  = (uint16_t) total;
    *active_start = (uint16_t) top;
    *active_end   = (uint16_t) active;
}

/* Each phase is an integer number of master GDC display words (two clocks).
 * Prefer the text master; use graphics when only the slave is programmed.
 * The reset fallback is the NEC 24 kHz, 80-word, 440-line display. */
static uint64_t
pc98_raster_phase_delay(pc98_86box_t *d, unsigned phase)
{
    const upd7220_t *g       = &d->core.text_gdc;
    unsigned         divisor = 4;
    if (!g->aw || !g->hfp || !g->hs || !g->hbp) {
        g       = &d->core.graph_gdc;
        divisor = pc98_gdc_clock_divisor(d, 1);
    }
    unsigned words;
    if (!g->aw || !g->hfp || !g->hs || !g->hbp) {
        static const unsigned defaults[] = { 80, 8, 8, 10 };
        words                            = defaults[phase - 1];
        divisor                          = 4;
    } else {
        words = phase == 1 ? g->aw : phase == 2 ? g->hfp
            : phase == 3                        ? g->hs
                                                : g->hbp;
    }
    uint128_t numerator       = (uint128_t) TIMER_USEC * words * 2u * (1000000u * divisor) + d->raster_clock_remainder;
    uint64_t  delay           = (uint64_t) (numerator / PC98_DOT_CLOCK_HZ);
    d->raster_clock_remainder = (uint32_t) (numerator % PC98_DOT_CLOCK_HZ);
    return delay ? delay : 1;
}

/* Start the free-running raster close to its first vertical retrace. A short
 * initial wait lets VX firmware observe VRTC without delaying boot by a full
 * frame. Phases: 0=first VRTC, 1=AW, 2=HFP, 3=HS, 4=HBP. */
static void
pc98_arm_raster(pc98_86box_t *d, unsigned first_vsync_us)
{
    d->in_vsync    = 0;
    d->raster_line = 0;
    memset(d->gdc_clock_remainder, 0, sizeof(d->gdc_clock_remainder));
    for (unsigned i = 0; i < 2; i++)
        d->gdc_clock_denominator[i] = (uint128_t) TIMER_USEC * (1000000u * pc98_gdc_clock_divisor(d, i));
    d->raster_clock_remainder = 0;
    d->gdc_last_tsc           = tsc;
    d->gdc_timer_usec         = TIMER_USEC;
    timer_disable(&d->gdc_timer);
    d->raster_phase = 0;
    upd7220_enable_bus_timing(&d->core.text_gdc, 1);
    upd7220_enable_bus_timing(&d->core.graph_gdc, 1);
    upd7220_set_bus_position(&d->core.text_gdc, 0, 0);
    upd7220_set_bus_position(&d->core.graph_gdc, 0, 0);
    upd7220_set_retrace(&d->core.text_gdc, 0, 0);
    upd7220_set_retrace(&d->core.graph_gdc, 0, 0);
    timer_set_delay_u64(&d->scan_timer, TIMER_USEC * (uint64_t) first_vsync_us);
}

static void
pc98_scan_tick(void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;

    if (d != pc98_active)
        return;

    pc98_gdc_sync(d);

    if (d->raster_phase == 0) {
        uint16_t total_lines;
        uint16_t active_start;
        uint16_t active_end;
        uint16_t vsync_start;
        pc98_raster_limits(d, &total_lines, &active_start, &active_end, &vsync_start);
        (void) total_lines;
        (void) active_end;
        d->raster_line = vsync_start;
        upd7220_set_bus_position(&d->core.text_gdc, 0, vsync_start);
        upd7220_set_bus_position(&d->core.graph_gdc, 0, vsync_start);
        pc98_begin_vsync(d);
        d->raster_phase = 1;
        timer_advance_u64(&d->scan_timer, pc98_raster_phase_delay(d, 1));
        return;
    }

    if (d->raster_phase == 1) {
        /* HBLANK is status bit 6 on both GDCs. Games use this short pulse to
         * pace raster effects and line-at-a-time animation. */
        d->raster_phase = 2;
        upd7220_set_retrace(&d->core.text_gdc, 1, d->in_vsync);
        upd7220_set_retrace(&d->core.graph_gdc, 1, d->in_vsync);
        timer_advance_u64(&d->scan_timer, pc98_raster_phase_delay(d, 2));
        return;
    }

    if (d->raster_phase < 4) {
        ++d->raster_phase;
        timer_advance_u64(&d->scan_timer,
                          pc98_raster_phase_delay(d, d->raster_phase));
        return;
    }

    /* End HBLANK and advance one scanline. Command/memory execution has its
     * own clock and no longer advances in coarse raster-sized batches. */
    d->raster_phase = 1;
    upd7220_set_retrace(&d->core.text_gdc, 0, d->in_vsync);
    upd7220_set_retrace(&d->core.graph_gdc, 0, d->in_vsync);

    uint16_t total_lines;
    uint16_t active_start;
    uint16_t active_end;
    uint16_t vsync_start;
    pc98_raster_limits(d, &total_lines, &active_start, &active_end, &vsync_start);

    if (++d->raster_line >= total_lines) {
        d->raster_line = 0;
        d->in_vsync    = 0;
        upd7220_set_retrace(&d->core.text_gdc, 0, 0);
        upd7220_set_retrace(&d->core.graph_gdc, 0, 0);
    } else {
        if (d->raster_line == active_end)
            pc98_present_frame(d);
        if (d->raster_line == vsync_start)
            pc98_begin_vsync(d);
    }

    /* Sample display memory as the beam enters each active line. Writes to
     * an already scanned row must not redraw that row at frame end. */
    if (d->raster_line >= active_start && d->raster_line < active_end && buffer32) {
        unsigned interlace = !!(d->core.graph_gdc.mode & 0x08u);
        unsigned y         = (d->raster_line - active_start) << interlace;
        if (!y) {
            video_wait_for_buffer();
            unsigned height = (active_end - active_start) << interlace;
            for (unsigned row = height; row < 400u; row++)
                memset(buffer32->line[row], 0, 640u * sizeof(uint32_t));
        }
        pc98_render_lines(d, y, y + (1u << interlace));
    }

    timer_advance_u64(&d->scan_timer, pc98_raster_phase_delay(d, 1));
}

/* Present the completed raster. Returns 0 if there is nowhere to draw. */
static int
pc98_present_frame(pc98_86box_t *d)
{
    if (!buffer32)
        return 0;

#ifndef RELEASE_BUILD
    if (d->core.video_ff[7] != d->display_on || !d->logged_state) {
        d->display_on   = d->core.video_ff[7];
        d->logged_state = 1;
        pclog("[PC98] display %s at frame %u (text GDC de=%u, graphics de=%u)\n",
              d->display_on ? "enabled" : "disabled", d->frame_no,
              d->core.text_gdc.de, d->core.graph_gdc.de);
    }
#endif

    /* The geometry must be re-established after every init and reset: xsize
     * and ysize are globals that survive a machine re-init, so testing them
     * alone can decide there is nothing to do while the renderer still has no
     * size for this device. */
    if (d->need_resize || xsize != 640 || ysize != 400 || video_force_resize_get()) {
        xsize = 640;
        ysize = 400;
        set_screen_size(xsize, ysize);
        video_force_resize_set(0);
        d->need_resize = 0;
    }
    video_blit_memtoscreen(0, 0, 640, 400);
    frames++;
    video_res_x = 640;
    video_res_y = 400;
    video_bpp   = 4;
    d->frame_no++;
    return 1;
}

int
pc9801_video_firmware_available_for_model(pc98_model_t model)
{
    int have_ipl = pc98_ipl_available_for_model(model);
    return have_ipl && pc98_rom_has_size(PC98_FONT_PATH, PC98_FONT_DUMP_SIZE) && pc98_rom_has_size(PC98_KANJI_X01_PATH, 0x8000u) && pc98_rom_has_size(PC98_KANJI_X02_PATH, 0x8000u) && pc98_rom_has_size(PC98_KANJI_X03_PATH, 0x8000u) && pc98_rom_has_size(PC98_KANJI_X04_PATH, 0x8000u);
}

int
pc9801_video_firmware_available(void)
{
    return pc9801_video_firmware_available_for_model(PC98_MODEL_VX);
}

static int
pc98_available(void)
{
    return pc9801_video_firmware_available();
}

static int
pc98_bx4_available(void)
{
    return pc9801_video_firmware_available_for_model(PC98_MODEL_BX4);
}

static void *
pc98_init(const device_t *info)
{
    pc98_86box_t *d = (pc98_86box_t *) calloc(1, sizeof(*d));
    if (!d)
        return NULL;

    pc98_video_init(&d->core);

    if (!load_font_from_rom_path(&d->core)) {
        pc98_video_destroy(&d->core);
        free(d);
        return NULL;
    }

    d->model = (pc98_model_t) info->local;
    d->ipl   = pc98_ipl_load_for_model(d->model);
    if (d->ipl && d->model == PC98_MODEL_BX4) {
        d->firmware_banks = pc98_bx4_banks_load();
        if (!d->firmware_banks) {
            free(d->ipl);
            d->ipl = NULL;
        }
    }
    if (!d->ipl) {
        pc98_video_destroy(&d->core);
        free(d);
        return NULL;
    }
    if (d->model == PC98_MODEL_VX && machine_get_config_int("skip_timer_checks") && pc98_bios_patch_vx_timer(d->ipl + 0x10000u, 0x8000u) == PC98_BIOS_PATCH_UNSUPPORTED) {
        ui_msgbox_header(MBX_ERROR, "PC-9801VX BIOS timer patch",
                         "The loaded VX BIOS revision is not supported by the timer patch. "
                         "Disable Skip BIOS timer checks to start with the original firmware.");
        free(d->ipl);
        pc98_video_destroy(&d->core);
        free(d);
        return NULL;
    }
    d->ipl_bank   = 0; /* hardware reset enters the ITF bank */
    d->flash_bank = 0xffu;
#ifndef RELEASE_BUILD
    pclog("[PC98] IPL reset vector bytes:");
    for (unsigned i = 0; i < 16; i++)
        pclog(" %02X", d->ipl[0x17ff0u + i]);
    pclog("\n");
#endif
    d->core.ipl_rom  = d->ipl;
    d->core.ipl_size = PC98_IPL_REGION_SIZE;
    d->core.owns_ipl = 0;

    /* VX presents a 0x18000 banked IPL window at E8000-FFFFF and mirrors the
     * same window at FE8000-FFFFFF for the 80286 reset vector. */
    mem_mapping_add(&d->ipl_low_map, 0x000e8000, PC98_IPL_WINDOW_SIZE,
                    ipl_rb, ipl_rw, ipl_rl, NULL, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM, d);
    mem_mapping_add(&d->ipl_high_map, 0x00fe8000, PC98_IPL_WINDOW_SIZE,
                    ipl_rb, ipl_rw, ipl_rl, NULL, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM, d);
    if (d->model == PC98_MODEL_BX4)
        mem_mapping_add(&d->ipl_reset_map, 0xfffe8000u, PC98_IPL_WINDOW_SIZE,
                        ipl_rb, ipl_rw, ipl_rl, NULL, NULL, NULL,
                        NULL, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM, d);

    mem_mapping_add(&d->tvram_map, 0x000a0000, 0x4000,
                    tvram_rb, tvram_rw, NULL, tvram_wb, tvram_ww, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, d);
    mem_mapping_add(&d->cg_map, 0x000a4000, 0x1000,
                    cg_rb, cg_rw, NULL, cg_wb, cg_ww, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, d);

    static const uint32_t base[4] = { 0x000a8000, 0x000b0000, 0x000b8000, 0x000e0000 };
    for (unsigned i = 0; i < 4; i++) {
        d->plane_map[i].dev   = d;
        d->plane_map[i].plane = (uint8_t) i;
        mem_mapping_add(&d->gvram_map[i], base[i], 0x8000,
                        gvram_rb, gvram_rw, NULL, gvram_wb, gvram_ww, NULL,
                        NULL, MEM_MAPPING_EXTERNAL, &d->plane_map[i]);
    }

    /* Install only real video ports.  In particular the odd 71/73/75/77
     * ports are the motherboard PIT and must remain available. */
    {
        static const uint16_t vp[] = {
            0x0060, 0x0062, 0x0068, 0x006a,
            0x0070, 0x0072, 0x0074, 0x0076, 0x0078, 0x007a, 0x007c, 0x007e,
            0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a8, 0x00a9, 0x00aa, 0x00ac, 0x00ae
        };
        for (unsigned i = 0; i < sizeof(vp) / sizeof(vp[0]); i++)
            io_sethandler(vp[i], 1, pc98_inb, NULL, NULL, pc98_outb, NULL, NULL, d);
        io_sethandler(0x04a0, 0x10, pc98_inb, pc98_inw, NULL, pc98_outb, pc98_outw, NULL, d);
        io_sethandler(0x043d, 1, NULL, NULL, NULL, ipl_bank_out, NULL, NULL, d);
        if (d->model == PC98_MODEL_BX4)
            io_sethandler(0x043f, 1, NULL, NULL, NULL, pc98_flash_bank_out, NULL, NULL, d);
    }

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &pc98_timings);
    d->need_resize = 1;
    pc98_active    = d; /* this device now owns the display; see pc98_scan_tick */
    d->vrtc_armed  = 1; /* The hardware produces one initial CRTV request. */
    timer_add(&d->scan_timer, pc98_scan_tick, d, 0);
    timer_add(&d->gdc_timer, pc98_gdc_tick, d, 0);
    pc98_arm_raster(d, 1000u);

    /* Deliberately no frame is presented here.  Composing one would mean
     * blitting from inside machine initialisation, before the renderer is
     * guaranteed to have a target buffer or a running blit thread - a null
     * dereference in the first case and a wait that never completes in the
     * second.  need_resize makes the first scheduled frame establish the
     * geometry instead, which is a millisecond away. */
    return d;
}

static void
pc98_reset(void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;

    d->ipl_bank     = 0;
    d->flash_bank   = 0xffu;
    d->need_resize  = 1;
    d->logged_state = 0;
    pc98_video_reset(&d->core);
    pc9801_video_vrtc_reset(d);
    pc98_arm_raster(d, 1000u);
    video_force_resize_set(1);
}

static void
pc98_force_redraw(void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;
    d->need_resize  = 1;
}

static void
pc98_close(void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *) priv;

    /* The frame timer lives inside this allocation and is linked into 86Box's
     * timer list, so it has to be taken out before the memory goes away. */
    timer_disable(&d->scan_timer);
    timer_disable(&d->gdc_timer);
    if (pc98_active == d)
        pc98_active = NULL;

    free(d->ipl);
    free(d->firmware_banks);
    d->ipl          = NULL;
    d->core.ipl_rom = NULL;
    pc98_video_destroy(&d->core);
    free(d);
}

pc98_video_t *
pc9801_video_get_core(void *priv)
{
    return priv ? &((pc98_86box_t *) priv)->core : NULL;
}

pc98_model_t
pc9801_video_get_model(void *priv)
{
    return priv ? ((pc98_86box_t *) priv)->model : PC98_MODEL_VX;
}

const device_t pc9801_video_device = {
    .name          = "NEC PC-9801 dual GDC + GRCG/EGC",
    .internal_name = "pc9801_video",
    .flags         = DEVICE_ONBOARD,
    .local         = 0,
    .init          = pc98_init,
    .close         = pc98_close,
    .reset         = pc98_reset,
    .available     = pc98_available,
    .speed_changed = pc98_speed_changed,
    .force_redraw  = pc98_force_redraw,
    .config        = NULL
};

const device_t pc9801bx4_video_device = {
    .name          = "NEC PC-9801BX4 legacy graphics (experimental)",
    .internal_name = "pc9801bx4_video",
    .flags         = DEVICE_ONBOARD,
    .local         = PC98_MODEL_BX4,
    .init          = pc98_init,
    .close         = pc98_close,
    .reset         = pc98_reset,
    .available     = pc98_bx4_available,
    .speed_changed = pc98_speed_changed,
    .force_redraw  = pc98_force_redraw
};
