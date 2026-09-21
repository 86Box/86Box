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
#include <86box/timer.h>
#include <86box/vid_pc98.h>
#include <86box/video.h>

#define PC98_IPL_PATH       "roms/machines/pc9801/ipl.rom"
#define PC98_ITF_PATH       "roms/machines/pc9801/itf.rom"
#define PC98_BIOS_PATH      "roms/machines/pc9801/bios.rom"
#define PC98_FONT_PATH      "roms/machines/pc9801/font.rom"
#define PC98_KANJI_X01_PATH "roms/machines/pc9801/24256c-x01.bin"
#define PC98_KANJI_X02_PATH "roms/machines/pc9801/24256c-x02.bin"
#define PC98_KANJI_X03_PATH "roms/machines/pc9801/24256c-x03.bin"
#define PC98_KANJI_X04_PATH "roms/machines/pc9801/24256c-x04.bin"
#define PC98_VX_YLL01_PATH  "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll01.bin"
#define PC98_VX_YLL02_PATH  "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll02.bin"
#define PC98_VX_YLL03_PATH  "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll03.bin"
#define PC98_VX_YLL04_PATH  "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll04.bin"

typedef struct pc98_86box_t pc98_86box_t;

typedef struct pc98_plane_map_t {
    pc98_86box_t *dev;
    uint8_t plane;
} pc98_plane_map_t;

struct pc98_86box_t {
    pc98_video_t core;
    pc_timer_t scan_timer;
    uint32_t frame_no;
    uint8_t in_vsync;
    uint8_t need_resize;
    uint8_t display_on;   /* last presented state of video FF 7 */
    uint8_t logged_state;
    uint8_t *ipl;
    uint8_t ipl_bank;
    mem_mapping_t ipl_low_map;
    mem_mapping_t tvram_map;
    mem_mapping_t cg_map;
    mem_mapping_t gvram_map[4];
    mem_mapping_t ipl_high_map;
    pc98_plane_map_t plane_map[4];
};

static uint8_t tvram_rb(uint32_t addr, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *)priv;
    uint32_t off = (addr - 0xa0000u) & 0x3fffu;

    /* A3FE2-A3FFF is the firmware memory-switch window.  It occupies the
     * low byte of each word; the high byte remains the ordinary nonexistent
     * attribute lane. */
    if (off >= 0x3fe2u) {
        if (off & 1u) return 0xff;
        return d->core.memsw[(off >> 1) & 0x0fu];
    }

    /* Attribute RAM has no odd byte lane on real PC-98 hardware. */
    if (off >= 0x2000u && (off & 1u)) return 0xff;

    uint16_t w = d->core.tvram[(off >> 1) & 0x1fffu];
    return (off & 1u) ? (uint8_t)(w >> 8) : (uint8_t)w;
}

static uint16_t tvram_rw(uint32_t addr, void *priv)
{
    uint16_t lo = tvram_rb(addr, priv);
    uint16_t hi = tvram_rb(addr + 1u, priv);
    return (uint16_t)(lo | (hi << 8));
}

static void tvram_wb(uint32_t addr, uint8_t val, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *)priv;
    uint32_t off = (addr - 0xa0000u) & 0x3fffu;
    if (off >= 0x3fe2u) {
        if (!(off & 1u) && d->core.video_ff[6])
            d->core.memsw[(off >> 1) & 0x0fu] = val;
        return;
    }
    if (off >= 0x2000u && (off & 1u)) return;

    uint16_t *p = &d->core.tvram[(off >> 1) & 0x1fffu];
    if (off & 1u) *p = (uint16_t)((*p & 0x00ffu) | ((uint16_t)val << 8));
    else          *p = (uint16_t)((*p & 0xff00u) | val);
}

static void tvram_ww(uint32_t addr, uint16_t val, void *priv)
{
    tvram_wb(addr, (uint8_t)val, priv);
    tvram_wb(addr + 1u, (uint8_t)(val >> 8), priv);
}

/*
 * A4000h character-generator window.  Scanline comes from address bits
 * 4:1.  Ordinary Kanji use address bit 0 as the left/right byte lane; the
 * 56xx/57xx programmable area instead uses the A5h L/R latch.  Keep this
 * distinction in the shared core decoder so the port and MMIO paths cannot
 * silently grow different address maps again.
 */
static uint32_t cg_window_offset(const pc98_video_t *v, uint32_t mmio_off,
                                 int *writable)
{
    return pc98_cg_window_offset(v, mmio_off, writable);
}

static uint8_t cg_rb(uint32_t addr, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *)priv;
    pc98_video_t *v = &d->core;
    if (!v->font_rom) return 0xff;

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

static uint16_t cg_rw(uint32_t addr, void *priv)
{
    return (uint16_t)(cg_rb(addr, priv) | ((uint16_t)cg_rb(addr + 1u, priv) << 8));
}

static void cg_wb(uint32_t addr, uint8_t val, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *)priv;
    pc98_video_t *v = &d->core;
    if (!v->font_rom) return;

    const uint32_t off = (addr - 0xa4000u) & 0xfffu;
    int            writable = 0;
    const uint32_t idx = cg_window_offset(v, off, &writable);

    /* Only the 56xx/57xx programmable area is RAM. */
    if (writable && idx < PC98_FONT_ADDRESS_SIZE)
        v->font_rom[idx] = val;
}

static void cg_ww(uint32_t addr, uint16_t val, void *priv)
{
    /* Model the word access as the two CPU byte-lane cycles the hardware
     * sees.  For ordinary Kanji they select opposite halves; for 56xx/57xx
     * PCG both lanes use the A5h L/R latch and therefore target the same
     * selected half. */
    cg_wb(addr, (uint8_t)val, priv);
    cg_wb(addr + 1u, (uint8_t)(val >> 8), priv);
}

static uint8_t gvram_rb(uint32_t addr, void *priv)
{
    pc98_plane_map_t *m = (pc98_plane_map_t *)priv;
    uint16_t off = (uint16_t)(addr & 0x7fffu);
    return pc98_gvram_read8(&m->dev->core, m->plane, off);
}

static uint16_t gvram_rw(uint32_t addr, void *priv)
{
    pc98_plane_map_t *m = (pc98_plane_map_t *)priv;
    uint16_t off = (uint16_t)((addr & 0x7fffu) >> 1);
    return pc98_gvram_read16(&m->dev->core, m->plane, off, 0xffff);
}

static void gvram_wb(uint32_t addr, uint8_t val, void *priv)
{
    pc98_plane_map_t *m = (pc98_plane_map_t *)priv;
    pc98_gvram_write8(&m->dev->core, m->plane, (uint16_t)(addr & 0x7fffu), val);
}

static void gvram_ww(uint32_t addr, uint16_t val, void *priv)
{
    pc98_plane_map_t *m = (pc98_plane_map_t *)priv;
    uint16_t off = (uint16_t)((addr & 0x7fffu) >> 1);
    pc98_gvram_write16(&m->dev->core, m->plane, off, val, 0xffff);
}

static uint8_t pc98_inb(uint16_t port, void *priv)
{
    return pc98_video_in8(&((pc98_86box_t *)priv)->core, port);
}

static void pc98_outb(uint16_t port, uint8_t val, void *priv)
{
    pc98_video_out8(&((pc98_86box_t *)priv)->core, port, val);
}

static uint32_t ipl_window_offset(uint32_t addr)
{
    if (addr >= 0x00fe8000u)
        return addr - 0x00fe8000u;
    return addr - 0x000e8000u;
}

static uint8_t ipl_rb(uint32_t addr, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *)priv;
    uint32_t off = ipl_window_offset(addr);
    if (!d->ipl || off >= PC98_IPL_WINDOW_SIZE)
        return 0xff;
    return d->ipl[(uint32_t)d->ipl_bank * PC98_IPL_WINDOW_SIZE + off];
}

static uint16_t ipl_rw(uint32_t addr, void *priv)
{
    return (uint16_t)(ipl_rb(addr, priv) | ((uint16_t)ipl_rb(addr + 1u, priv) << 8));
}

static uint32_t ipl_rl(uint32_t addr, void *priv)
{
    return (uint32_t)ipl_rw(addr, priv) | ((uint32_t)ipl_rw(addr + 2u, priv) << 16);
}

static int read_rom_exact(const char *path, uint8_t *dst, size_t bytes)
{
    FILE *fp = rom_fopen(path, "rb");
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0 || ftell(fp) != (long)bytes ||
        fseek(fp, 0, SEEK_SET) != 0 || fread(dst, 1, bytes, fp) != bytes) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static int rom_has_exact_size(const char *path, size_t bytes)
{
    FILE *fp = rom_fopen(path, "rb");
    long size;
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 0; }
    size = ftell(fp);
    fclose(fp);
    return size == (long)bytes;
}

/* Build MAME/real-hardware normalized VX IPL layout.
 *
 *   region 00000-0ffff : erased
 *   region 10000-17fff : ITF (reset bank)
 *   region 18000-2ffff : main BIOS bank
 *
 * Accepted inputs, in preference order:
 *   1) a ready-made 0x30000 ipl.rom;
 *   2) 0x8000 itf.rom + 0x18000 bios.rom;
 *   3) the four physical 27C256 VX CPU-board dumps used by MAME.
 */
static uint8_t *load_vx_ipl(void)
{
    uint8_t *ipl = (uint8_t *)malloc(PC98_IPL_REGION_SIZE);
    if (!ipl) return NULL;
    memset(ipl, 0xff, PC98_IPL_REGION_SIZE);

    if (read_rom_exact(PC98_IPL_PATH, ipl, PC98_IPL_REGION_SIZE))
        return ipl;

    if (rom_has_exact_size(PC98_ITF_PATH, 0x8000u) &&
        rom_has_exact_size(PC98_BIOS_PATH, PC98_IPL_WINDOW_SIZE)) {
        if (!read_rom_exact(PC98_ITF_PATH, ipl + 0x10000u, 0x8000u) ||
            !read_rom_exact(PC98_BIOS_PATH, ipl + 0x18000u, PC98_IPL_WINDOW_SIZE)) {
            free(ipl);
            return NULL;
        }
        return ipl;
    }

    if (rom_has_exact_size(PC98_VX_YLL01_PATH, 0x8000u) &&
        rom_has_exact_size(PC98_VX_YLL02_PATH, 0x8000u) &&
        rom_has_exact_size(PC98_VX_YLL03_PATH, 0x8000u) &&
        rom_has_exact_size(PC98_VX_YLL04_PATH, 0x8000u)) {
        uint8_t *biosrom = (uint8_t *)malloc(0x20000u);
        uint8_t *tmp = (uint8_t *)malloc(0x8000u);
        if (!biosrom || !tmp) { free(tmp); free(biosrom); free(ipl); return NULL; }
        memset(biosrom, 0xff, 0x20000u);
        const char *paths[4] = {
            PC98_VX_YLL01_PATH, PC98_VX_YLL03_PATH,
            PC98_VX_YLL02_PATH, PC98_VX_YLL04_PATH
        };
        const unsigned bases[4] = { 0x00000u, 0x00001u, 0x10000u, 0x10001u };
        for (unsigned r = 0; r < 4; r++) {
            if (!read_rom_exact(paths[r], tmp, 0x8000u)) {
                free(tmp); free(biosrom); free(ipl); return NULL;
            }
            for (unsigned i = 0; i < 0x8000u; i++)
                biosrom[bases[r] + i * 2u] = tmp[i];
        }
        memcpy(ipl + 0x10000u, biosrom + 0x18000u, 0x8000u);
        memcpy(ipl + 0x18000u, biosrom + 0x08000u, 0x8000u);
        memcpy(ipl + 0x20000u, biosrom + 0x00000u, 0x8000u);
        memcpy(ipl + 0x28000u, biosrom + 0x10000u, 0x8000u);
        free(tmp);
        free(biosrom);
        return ipl;
    }

    free(ipl);
    return NULL;
}

static void ipl_bank_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *)priv;
    (void)port;
    if (((val & 0xf0u) == 0x00u || (val & 0xf0u) == 0x10u) &&
        ((val & 0xedu) == 0x00u)) {
        uint8_t bank = (val >> 1) & 1u;
        if (bank != d->ipl_bank)
            pclog("[PC98] IPL bank %u -> %u (43D=%02X)\n", d->ipl_bank, bank, val);
        d->ipl_bank = bank;
    }
}

static int load_font_from_rom_path(pc98_video_t *v)
{
    uint8_t *dump = (uint8_t *)malloc(PC98_FONT_DUMP_SIZE);
    uint8_t *raw = (uint8_t *)malloc(PC98_RAW_KANJI_SIZE);
    uint8_t *part = (uint8_t *)malloc(0x8000u);
    static const char *const paths[4] = {
        PC98_KANJI_X01_PATH, PC98_KANJI_X02_PATH,
        PC98_KANJI_X03_PATH, PC98_KANJI_X04_PATH
    };
    int ok = 0;

    if (!dump || !raw || !part)
        goto done;
    if (!read_rom_exact(PC98_FONT_PATH, dump, PC98_FONT_DUMP_SIZE))
        goto done;

    /* Reproduce MAME's ROM_LOAD16_BYTE + ROM_CONTINUE layout.  X01/X02
     * supply even/odd bytes at 00000h and 20000h; X03/X04 do the same at
     * 40000h and 60000h. */
    memset(raw, 0xff, PC98_RAW_KANJI_SIZE);
    for (unsigned rom = 0; rom < 4u; rom++) {
        const size_t bank = (rom >> 1) ? 0x40000u : 0u;
        const size_t lane = rom & 1u;
        if (!read_rom_exact(paths[rom], part, 0x8000u))
            goto done;
        for (unsigned half = 0; half < 2u; half++)
            for (unsigned i = 0; i < 0x4000u; i++)
                raw[bank + half * 0x20000u + lane + i * 2u] =
                    part[half * 0x4000u + i];
    }

    ok = pc98_video_install_font_set(v, dump, PC98_FONT_DUMP_SIZE,
                                      raw, PC98_RAW_KANJI_SIZE) == 0;
done:
    free(part);
    free(raw);
    free(dump);
    return ok;
}


static const video_timings_t pc98_timings = {
    .type = VIDEO_ISA,
    .write_b = 4, .write_w = 8, .write_l = 16,
    .read_b  = 4, .read_w  = 8, .read_l  = 16
};

static uint32_t pc98_rgb(unsigned pen)
{
    unsigned bright = (pen & 8u) ? 0x55u : 0u;
    unsigned b = (pen & 1u) ? 0xaau + bright : bright;
    unsigned r = (pen & 2u) ? 0xaau + bright : bright;
    unsigned g = (pen & 4u) ? 0xaau + bright : bright;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return makecol((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

/* NP2/W's text palette is not the 0xAA digital graphics palette.
 * Its pal_maketext() uses the eight degpal1[] entries, and degpal1[] is
 * generated with each asserted B/R/G bit at full 0xFF intensity.  Keep this
 * separate from pc98_rgb(): the latter is still used by the bitmap layer. */
static uint32_t pc98_text_rgb(unsigned color)
{
    const uint8_t b = (color & 1u) ? 0xffu : 0u;
    const uint8_t r = (color & 2u) ? 0xffu : 0u;
    const uint8_t g = (color & 4u) ? 0xffu : 0u;
    return makecol(r, g, b);
}

static uint32_t pc98_font_offset(const pc98_video_t *v, uint16_t code,
                                  unsigned line, unsigned right, unsigned lr)
{
    if (code & 0xff00u)
        return ((((uint32_t)code & 0x7f7fu) << 5) +
                ((line & 15u) << 1) + (right & 1u)) &
               (PC98_FONT_ADDRESS_SIZE - 1u);

    /* FONTSEL=0 uses the 8x8 SBCS block at 0000h.  With LR>=16 PC-98
     * doubles its scanlines vertically.  FONTSEL=1 uses the 8x16 block. */
    if (v->video_ff[3])
        return (0x800u + (uint32_t)(code & 0xffu) * 16u + (line & 15u)) &
               (PC98_FONT_ADDRESS_SIZE - 1u);
    return ((uint32_t)(code & 0xffu) * 8u +
            ((lr >= 16u) ? ((line >> 1) & 7u) : (line & 7u))) &
           (PC98_FONT_ADDRESS_SIZE - 1u);
}

static uint8_t pc98_semigraphics_bits(uint8_t tile, unsigned line, unsigned char_size)
{
    uint8_t bits = 0;
    const unsigned tall = (char_size == 16u);

    for (unsigned sx = 0; sx < 8u; sx++) {
        unsigned gfx_bit = sx & 4u;
        gfx_bit += (line & (2u << tall)) >> (1u + tall);
        gfx_bit += (line & (4u << tall)) >> (1u + tall);
        if ((tile >> gfx_bit) & 1u)
            bits |= (uint8_t)(0x80u >> sx);
    }
    return bits;
}

static int pc98_cursor_phase(const pc98_86box_t *d)
{
    const upd7220_t *g = &d->core.text_gdc;
    if (!g->dc)
        return 0;
    if (g->sc)
        return 1;

    /* uPD7220 CCHAR defines cursor on-time and off-time as 2*BR frames.
     * BR=0 is treated as the shortest useful rate rather than divide-by-zero. */
    unsigned br = g->br ? g->br : 1u;
    unsigned span = 2u * br;
    return ((d->frame_no / span) & 1u) == 0u;
}

static int pc98_attribute_blink_off(const pc98_86box_t *d)
{
    unsigned br = d->core.text_gdc.br ? d->core.text_gdc.br : 1u;
    unsigned period = 8u * br;
    unsigned phase = d->frame_no % period;

    /* Attribute blink is half the cursor rate with a 3/4-on, 1/4-off duty. */
    return phase >= 6u * br;
}

static void pc98_render_graphics(pc98_86box_t *d)
{
    pc98_video_t *v = &d->core;
    upd7220_partition_t area;
    unsigned pitch;
    unsigned lines;

    if (!v->graph_gdc.de) return;
    upd7220_get_graphics_partition(&v->graph_gdc, 0, &area);
    pitch = v->graph_gdc.pitch ? v->graph_gdc.pitch : 40u;
    lines = area.len ? area.len : 400u;
    if (lines > 400u) lines = 400u;

    for (unsigned y = 0; y < lines; y++) {
        uint32_t wa = (area.sad + y * pitch) & 0x3fffu;
        for (unsigned w = 0; w < pitch && w < 40u; w++, wa = (wa + 1u) & 0x3fffu) {
            uint16_t pb = v->gvram[v->display_page & 1][PC98_PLANE_B][wa];
            uint16_t pr = v->gvram[v->display_page & 1][PC98_PLANE_R][wa];
            uint16_t pg = v->gvram[v->display_page & 1][PC98_PLANE_G][wa];
            uint16_t pi = v->gvram[v->display_page & 1][PC98_PLANE_E][wa];
            for (unsigned bit = 0; bit < 16; bit++) {
                unsigned x = w * 16u + bit;
                unsigned pen = ((pb >> bit) & 1u) |
                               (((pr >> bit) & 1u) << 1) |
                               (((pg >> bit) & 1u) << 2) |
                               (((pi >> bit) & 1u) << 3);
                buffer32->line[y][x] = pc98_rgb(pen);
            }
        }
    }
}

static void pc98_render_text(pc98_86box_t *d)
{
    pc98_video_t *v = &d->core;
    upd7220_partition_t area;
    unsigned pitch, lr, rows, widthmul;
    unsigned char_size;
    int cursor_on;
    int blink_off;

    if (!v->text_gdc.de || !v->font_rom) return;
    upd7220_get_text_partition(&v->text_gdc, 0, &area);
    pitch = v->text_gdc.pitch ? v->text_gdc.pitch : 80u;
    if (pitch > 80u) pitch = 80u;
    lr = v->text_gdc.lr ? v->text_gdc.lr : 16u;
    if (lr > 32u) lr = 32u;
    rows = area.len ? (area.len / lr) : (400u / lr);
    if (!rows || rows > 50u) rows = 400u / lr;
    widthmul = v->video_ff[2] ? 2u : 1u;
    char_size = v->video_ff[3] ? 16u : 8u;
    if (!v->video_ff[3] && lr >= 16u)
        char_size = 16u;
    cursor_on = pc98_cursor_phase(d);
    blink_off = pc98_attribute_blink_off(d);

    for (unsigned row = 0; row < rows; row++) {
        uint32_t base = (area.sad + row * pitch) & 0x0fffu;
        unsigned last_underline = 0;

        for (unsigned col = 0; col < pitch; ) {
            uint32_t ta = (base + col * widthmul) & 0x0fffu;
            uint16_t tw = v->tvram[ta];
            uint8_t lo = (uint8_t)tw;
            uint8_t hi = (uint8_t)(tw >> 8);
            uint16_t code = lo;
            unsigned halves = 1;

            if (hi) {
                code = (uint16_t)(((uint16_t)(lo & 0x7fu) << 8) | (hi & 0x7fu));
                /* 56xx/57xx are programmable gaiji strips and 08xx selects
                 * the single-cell 8x16 area.  Normal Kanji occupy two cells. */
                if ((code & 0x7e00u) == 0x5600u || (code & 0x7c00u) == 0x0800u)
                    halves = 1;
                else
                    halves = 2;
            }

            for (unsigned half = 0; half < halves && col + half < pitch; half++) {
                uint32_t attr_addr = ((ta + half) & 0x0fffu) | 0x1000u;
                uint8_t attr = (uint8_t)v->tvram[attr_addr];
                unsigned color = (attr >> 5) & 7u;
                unsigned reverse = !!(attr & 0x04u);
                unsigned underline = !!(attr & 0x08u);
                unsigned hidden = !(attr & 0x01u);
                unsigned blink = !!(attr & 0x02u) && blink_off;
                unsigned vline = !v->video_ff[0] && !!(attr & 0x10u);
                unsigned semigraphics = v->video_ff[0] && !!(attr & 0x10u) && !hi;
                unsigned cursor_here = cursor_on &&
                    (((v->text_gdc.ead & 0x0fffu) == ta) ||
                     (halves == 2u && ((v->text_gdc.ead + 1u) & 0x0fffu) == ta));

                for (unsigned sy = 0; sy < lr && row * lr + sy < 400u; sy++) {
                    uint8_t bits = 0;

                    if (!hidden) {
                        if (hi) {
                            if (sy < 16u)
                                bits = v->font_rom[pc98_font_offset(v, code, sy, half, lr)];
                        } else if (semigraphics) {
                            bits = pc98_semigraphics_bits(lo, sy, char_size);
                        } else if (sy < char_size) {
                            bits = v->font_rom[pc98_font_offset(v, code, sy, 0, lr)];
                        }
                    }

                    if (sy == lr - 1u) {
                        if (underline) bits = 0x0fu;
                        if (last_underline) bits |= 0xf0u;
                    }
                    if (vline)
                        bits |= 0x08u;

                    if (cursor_here && sy >= v->text_gdc.ctop && sy <= v->text_gdc.cbot)
                        bits ^= 0xffu;
                    if (blink)
                        bits = 0;
                    if (reverse)
                        bits ^= 0xffu;

                    for (unsigned sx = 0; sx < 8u; sx++) {
                        if (!(bits & (0x80u >> sx))) continue;
                        unsigned x = ((col + half) * 8u + sx) * widthmul;
                        unsigned y = row * lr + sy;
                        if (x >= 640u) continue;
                        buffer32->line[y][x] = pc98_text_rgb(color);
                        if (widthmul == 2u && x + 1u < 640u)
                            buffer32->line[y][x + 1u] = pc98_text_rgb(color);
                    }
                }
                last_underline = underline;
            }
            col += halves;
        }
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

/* Put the raster into active display and schedule the next VRTC edge.
 *
 * The raster is independent of the CPU reset state, and on real hardware it is
 * already free-running when the CPU leaves reset.  A freshly created device
 * should therefore not sit a whole 16.7 ms frame away from its first VRTC: the
 * VX firmware polls GDC retrace while bringing the CRT path up, and a
 * full-frame cold-start delay can make that wait time out. */
static void pc98_arm_raster(pc98_86box_t *d, unsigned first_vsync_us)
{
    d->in_vsync = 0;
    upd7220_set_retrace(&d->core.text_gdc, 0, 0);
    upd7220_set_retrace(&d->core.graph_gdc, 0, 0);
    timer_set_delay_u64(&d->scan_timer, TIMER_USEC * (uint64_t)first_vsync_us);
}

static void pc98_scan_tick(void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *)priv;

    if (d != pc98_active)
        return;

    /* VRTC must remain asserted for guest-visible time.  The previous code
     * raised and lowered VSYNC in the same callback, making status-bit polling
     * loops unable to ever observe a vertical retrace. */
    if (d->in_vsync) {
        upd7220_set_retrace(&d->core.text_gdc, 0, 0);
        upd7220_set_retrace(&d->core.graph_gdc, 0, 0);
        d->in_vsync = 0;
        timer_advance_u64(&d->scan_timer, TIMER_USEC * 15667u);
        return;
    }

    if (!pc98_present_frame(d)) {
        /* The renderer has no buffer yet.  Re-arm and try again next frame
         * rather than writing through a null pointer. */
        timer_advance_u64(&d->scan_timer, TIMER_USEC * 16667u);
        return;
    }

    upd7220_advance(&d->core.text_gdc, 100000u);
    upd7220_advance(&d->core.graph_gdc, 100000u);

    upd7220_set_retrace(&d->core.text_gdc, 0, 1);
    upd7220_set_retrace(&d->core.graph_gdc, 0, 1);
    d->in_vsync = 1;
    picint(1u << 2); /* VRTC interrupt; motherboard port 64h clears it. */

    /* Hold VSYNC high for about 1 ms, then spend the rest of the ~60 Hz frame
     * in active display.  This is deliberately coarse but guest-observable. */
    timer_advance_u64(&d->scan_timer, TIMER_USEC * 1000u);
}

/* Compose and present one frame.  Returns 0 if there is nowhere to draw. */
static int pc98_present_frame(pc98_86box_t *d)
{
    if (!buffer32)
        return 0;

    for (unsigned y = 0; y < 400u; y++)
        for (unsigned x = 0; x < 640u; x++)
            buffer32->line[y][x] = 0;

    /* PC-98 enables both bitmap and text layers through video FF 7, set by
     * "out 68h,0Fh" and cleared by 0Eh. */
    if (d->core.video_ff[7]) {
        pc98_render_graphics(d);
        pc98_render_text(d);
    }

    if (d->core.video_ff[7] != d->display_on || !d->logged_state) {
        d->display_on = d->core.video_ff[7];
        d->logged_state = 1;
        pclog("[PC98] display %s at frame %u (text GDC de=%u, graphics de=%u)\n",
              d->display_on ? "enabled" : "disabled", d->frame_no,
              d->core.text_gdc.de, d->core.graph_gdc.de);
    }

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
    video_bpp = 4;
    d->frame_no++;
    return 1;
}

int pc9801_video_firmware_available(void)
{
    int have_ipl = rom_has_exact_size(PC98_IPL_PATH, PC98_IPL_REGION_SIZE) ||
        (rom_has_exact_size(PC98_ITF_PATH, 0x8000u) &&
         rom_has_exact_size(PC98_BIOS_PATH, PC98_IPL_WINDOW_SIZE)) ||
        (rom_has_exact_size(PC98_VX_YLL01_PATH, 0x8000u) &&
         rom_has_exact_size(PC98_VX_YLL02_PATH, 0x8000u) &&
         rom_has_exact_size(PC98_VX_YLL03_PATH, 0x8000u) &&
         rom_has_exact_size(PC98_VX_YLL04_PATH, 0x8000u));
    return have_ipl &&
        rom_has_exact_size(PC98_FONT_PATH, PC98_FONT_DUMP_SIZE) &&
        rom_has_exact_size(PC98_KANJI_X01_PATH, 0x8000u) &&
        rom_has_exact_size(PC98_KANJI_X02_PATH, 0x8000u) &&
        rom_has_exact_size(PC98_KANJI_X03_PATH, 0x8000u) &&
        rom_has_exact_size(PC98_KANJI_X04_PATH, 0x8000u);
}

static int pc98_available(void)
{
    return pc9801_video_firmware_available();
}

static void *pc98_init(const device_t *info)
{
    (void)info;
    pc98_86box_t *d = (pc98_86box_t *)calloc(1, sizeof(*d));
    if (!d) return NULL;

    pc98_video_init(&d->core);

    if (!load_font_from_rom_path(&d->core)) {
        pc98_video_destroy(&d->core);
        free(d);
        return NULL;
    }

    d->ipl = load_vx_ipl();
    if (!d->ipl) {
        pc98_video_destroy(&d->core);
        free(d);
        return NULL;
    }
    d->ipl_bank = 0; /* hardware reset enters the ITF bank */
    pclog("[PC98] IPL reset vector bytes:");
    for (unsigned i = 0; i < 16; i++)
        pclog(" %02X", d->ipl[0x17ff0u + i]);
    pclog("\n");
    d->core.ipl_rom = d->ipl;
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

    mem_mapping_add(&d->tvram_map, 0x000a0000, 0x4000,
                    tvram_rb, tvram_rw, NULL, tvram_wb, tvram_ww, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, d);
    mem_mapping_add(&d->cg_map, 0x000a4000, 0x1000,
                    cg_rb, cg_rw, NULL, cg_wb, cg_ww, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, d);

    static const uint32_t base[4] = { 0x000a8000, 0x000b0000, 0x000b8000, 0x000e0000 };
    for (unsigned i = 0; i < 4; i++) {
        d->plane_map[i].dev = d;
        d->plane_map[i].plane = (uint8_t)i;
        mem_mapping_add(&d->gvram_map[i], base[i], 0x8000,
                        gvram_rb, gvram_rw, NULL, gvram_wb, gvram_ww, NULL,
                        NULL, MEM_MAPPING_EXTERNAL, &d->plane_map[i]);
    }

    /* Install only real video ports.  In particular the odd 71/73/75/77
     * ports are the motherboard PIT and must remain available. */
    { static const uint16_t vp[] = {
        0x0060,0x0062,0x0068,0x006a,
        0x0070,0x0072,0x0074,0x0076,0x0078,0x007a,0x007c,0x007e,
        0x00a0,0x00a1,0x00a2,0x00a3,0x00a4,0x00a5,0x00a6,0x00a8,0x00a9,0x00aa,0x00ac,0x00ae
    };
      for (unsigned i=0;i<sizeof(vp)/sizeof(vp[0]);i++)
          io_sethandler(vp[i],1,pc98_inb,NULL,NULL,pc98_outb,NULL,NULL,d);
      for (uint16_t p=0x04a0;p<=0x04af;p++)
          io_sethandler(p,1,pc98_inb,NULL,NULL,pc98_outb,NULL,NULL,d);
      io_sethandler(0x043d,1,NULL,NULL,NULL,ipl_bank_out,NULL,NULL,d);
    }

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &pc98_timings);
    d->need_resize = 1;
    pc98_active = d; /* this device now owns the display; see pc98_scan_tick */
    timer_add(&d->scan_timer, pc98_scan_tick, d, 0);
    pc98_arm_raster(d, 1000u);

    /* Deliberately no frame is presented here.  Composing one would mean
     * blitting from inside machine initialisation, before the renderer is
     * guaranteed to have a target buffer or a running blit thread - a null
     * dereference in the first case and a wait that never completes in the
     * second.  need_resize makes the first scheduled frame establish the
     * geometry instead, which is a millisecond away. */
    return d;
}

static void pc98_reset(void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *)priv;

    d->ipl_bank = 0;
    d->need_resize = 1;
    d->logged_state = 0;
    pc98_video_reset(&d->core);
    picintc(1u << 2); /* drop any VRTC request raised before the reset */
    pc98_arm_raster(d, 1000u);
    video_force_resize_set(1);
}

static void pc98_force_redraw(void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *)priv;
    d->need_resize = 1;
}

static void pc98_close(void *priv)
{
    pc98_86box_t *d = (pc98_86box_t *)priv;

    /* The frame timer lives inside this allocation and is linked into 86Box's
     * timer list, so it has to be taken out before the memory goes away. */
    timer_disable(&d->scan_timer);
    if (pc98_active == d)
        pc98_active = NULL;

    free(d->ipl);
    d->ipl = NULL;
    d->core.ipl_rom = NULL;
    pc98_video_destroy(&d->core);
    free(d);
}

pc98_video_t *pc9801_video_get_core(void *priv)
{
    return priv ? &((pc98_86box_t *)priv)->core : NULL;
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
    .speed_changed = NULL,
    .force_redraw  = pc98_force_redraw,
    .config        = NULL
};
