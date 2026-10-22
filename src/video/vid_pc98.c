/*
 * NEC PC-9801 graphics front end: dual uPD7220, GRCG, EGC, planar VRAM and
 * character-generator window.
 *
 * On the VX the EGC sits between both bus masters (CPU and graphics GDC)
 * and planar VRAM. Its compatibility mode supplies the GRCG functions.
 */
#include <86box/vid_pc98.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t
tvram_gdc_read(void *opaque, uint32_t addr)
{
    pc98_video_t *v = (pc98_video_t *) opaque;
    return v->tvram[addr & 0x1fff];
}

static void
tvram_gdc_write(void *opaque, uint32_t addr, uint16_t data)
{
    pc98_video_t *v         = (pc98_video_t *) opaque;
    v->tvram[addr & 0x1fff] = data;
}

static unsigned
gdc_plane(uint32_t addr)
{
    /* PC-98 GDC linear GVRAM order is E, B, R, G.  CPU aperture order is
     * B/R/G at A8000/B0000/B8000 with E at E0000. */
    static const uint8_t order[4] = { PC98_PLANE_E, PC98_PLANE_B, PC98_PLANE_R, PC98_PLANE_G };
    return order[(addr >> 14) & 3];
}

static void charger_write(pc98_video_t *v, unsigned page, unsigned plane,
                          uint16_t off, uint16_t data, uint16_t mem_mask,
                          bool gdc);

static uint16_t
gvram_gdc_read(void *opaque, uint32_t addr)
{
    pc98_video_t *v    = (pc98_video_t *) opaque;
    unsigned      page = (addr >> 16) & 1;
    /* With the charger active, the GDC's read/modify/write reads see zero.
     * This prevents its preliminary read from feeding the CPU shifter or
     * contaminating the pixel mask it subsequently presents to the EGC.
     * PC-9801 Programmer's Bible, p.464 (GDC drawing through EGC). */
    if (v->grcg.mode & 0x80)
        return 0;
    return v->gvram[page][gdc_plane(addr)][addr & 0x3fff];
}

static void
gvram_gdc_write(void *opaque, uint32_t addr, uint16_t data)
{
    pc98_video_t *v    = (pc98_video_t *) opaque;
    unsigned      page = (addr >> 16) & 1;
    charger_write(v, page, gdc_plane(addr), (uint16_t) (addr & 0x3fff),
                  data, 0xffff, true);
}

static uint8_t
bitrev8(uint8_t x)
{
    x = (uint8_t) (((x & 0x55) << 1) | ((x >> 1) & 0x55));
    x = (uint8_t) (((x & 0x33) << 2) | ((x >> 2) & 0x33));
    x = (uint8_t) ((x << 4) | (x >> 4));
    return x;
}

static uint16_t
egc_bus_mask(uint16_t x)
{
    /* EGC bit numbering is reversed within each byte on the x86 bus. */
    return (uint16_t) (bitrev8((uint8_t) x) | ((uint16_t) bitrev8((uint8_t) (x >> 8)) << 8));
}

static uint16_t
direct_word(const pc98_video_t *v, unsigned page, unsigned plane, uint16_t off)
{
    return v->gvram[page & 1][plane & 3][off & 0x3fff];
}

static void
direct_word_write(pc98_video_t *v, unsigned page, unsigned plane, uint16_t off,
                  uint16_t data, uint16_t mem_mask)
{
    uint16_t *p = &v->gvram[page & 1][plane & 3][off & 0x3fff];
    *p          = (uint16_t) ((*p & ~mem_mask) | (data & mem_mask));
}

static uint16_t
color_pattern(const pc98_video_t *v, unsigned plane)
{
    switch ((v->egc.regs[1] >> 13) & 3) {
        case 1:
            return v->egc.bg[plane & 3];
        case 2:
            return v->egc.fg[plane & 3];
        default:
            return v->egc.pat[plane & 3];
    }
}

uint16_t
pc98_egc_rop(uint8_t truth, uint16_t src, uint16_t pat, uint16_t dst)
{
    /* NEC's truth-table address order is S,D,P.  Build the result in parallel
     * over all sixteen pixel bits instead of looping pixel-by-pixel. */
    uint16_t ns  = (uint16_t) ~src;
    uint16_t np  = (uint16_t) ~pat;
    uint16_t nd  = (uint16_t) ~dst;
    uint16_t out = 0;

    if (truth & 0x01)
        out |= (uint16_t) (ns & nd & np); /* 000 */
    if (truth & 0x02)
        out |= (uint16_t) (ns & nd & pat); /* 001 */
    if (truth & 0x04)
        out |= (uint16_t) (ns & dst & np); /* 010 */
    if (truth & 0x08)
        out |= (uint16_t) (ns & dst & pat); /*011 */
    if (truth & 0x10)
        out |= (uint16_t) (src & nd & np); /* 100 */
    if (truth & 0x20)
        out |= (uint16_t) (src & nd & pat); /*101 */
    if (truth & 0x40)
        out |= (uint16_t) (src & dst & np); /*110 */
    if (truth & 0x80)
        out |= (uint16_t) (src & dst & pat); /*111 */
    return out;
}

static uint16_t
egc_shift(pc98_egc_t *e, unsigned plane, uint16_t val)
{
    unsigned src_off = e->regs[6] & 0x0f;
    unsigned dst_off = (e->regs[6] >> 4) & 0x0f;
    uint16_t out;

    if (src_off == dst_off) {
        out                = (uint16_t) (val | e->leftover[plane]);
        e->leftover[plane] = 0;
        return out;
    }

    if (e->regs[6] & 0x1000) { /* descending/rightward stream */
        if (dst_off > src_off) {
            unsigned n         = dst_off - src_off;
            out                = (uint16_t) ((val >> n) | e->leftover[plane]);
            e->leftover[plane] = (uint16_t) (val << (16 - n));
        } else {
            unsigned n         = src_off - dst_off;
            out                = (uint16_t) ((val >> (16 - n)) | e->leftover[plane]);
            e->leftover[plane] = (uint16_t) (val << n);
        }
    } else { /* ascending/leftward stream */
        if (dst_off > src_off) {
            unsigned n         = dst_off - src_off;
            out                = (uint16_t) ((val << n) | e->leftover[plane]);
            e->leftover[plane] = (uint16_t) (val >> (16 - n));
        } else {
            unsigned n         = src_off - dst_off;
            out                = (uint16_t) ((val << (16 - n)) | e->leftover[plane]);
            e->leftover[plane] = (uint16_t) (val >> n);
        }
    }
    return out;
}

static void
egc_restart(pc98_egc_t *e)
{
    e->count  = (int32_t) (e->regs[7] & 0x0fff) + 1;
    e->first  = 1;
    e->start  = 0;
    e->loaded = 0;
}

static void
egc_reset(pc98_egc_t *e)
{
    memset(e, 0, sizeof(*e));
    e->regs[0] = 0xfff0; /* access */
    e->regs[1] = 0x00ff; /* fg/bg mode */
    e->regs[4] = 0xffff; /* logical mask register */
    e->regs[7] = 0x000f; /* 16 bits */
    e->mask    = 0xffff;
    egc_restart(e);
}

static void
egc_reg_write(pc98_video_t *v, unsigned reg, uint16_t data, uint16_t mem_mask)
{
    pc98_egc_t *e = &v->egc;
    reg &= 7;

    /* VX registers are decoded only in active extended mode. The 6Ah
     * register-write unlock controls mode changes, not these data ports. */
    if (!v->egc_enabled || !(v->grcg.mode & 0x80))
        return;

    /* The mask and color ports have opposite write gates. See NEC VX
     * supplement p.43 and the TSG EGC hardware notes pp.23-25. */
    if (reg == 4 && (e->regs[1] & 0x6000))
        return;
    if ((reg == 3 || reg == 5) && !(e->regs[1] & 0x6000))
        return;

    e->regs[reg] = (uint16_t) ((e->regs[reg] & ~mem_mask) | (data & mem_mask));

    switch (reg) {
        case 3:
        case 5:
            for (unsigned i = 0; i < 4; i++) {
                uint16_t color = (e->regs[reg] & (1u << i)) ? 0xffff : 0;
                if (reg == 3) {
                    e->fg[i]        = color;
                    v->grcg.tile[i] = (uint8_t) color;
                } else {
                    e->bg[i] = color;
                }
            }
            break;
        case 4:
            e->mask = egc_bus_mask(e->regs[4]);
            break;
        case 6:
        case 7:
            /* Shift/length programming starts a new stream. Changing the ROP
             * does not discard a source already read into the shifter. */
            memset(e->leftover, 0, sizeof(e->leftover));
            egc_restart(e);
            break;
        default:
            break;
    }
}

static void
egc_load_pattern(pc98_egc_t *e, const uint16_t data[4], uint16_t mask,
                 bool write)
{
    unsigned lead = (e->regs[1] >> 8) & 3;
    for (unsigned i = 0; i < 4; i++) {
        /* Lead-plane selection is a read-bus choice. PLOAD_W snapshots the
         * destination bank before the write, even with compare read disabled.
         * EGC v1.3 B06/W10/W11 distinguish this from selected-plane PLOAD_R. */
        if ((e->regs[0] & (1u << i)) || (!write && !(e->regs[2] & 0x2000) && i != lead))
            continue;
        e->pat[i] = (uint16_t) ((e->pat[i] & ~mask) | (data[i] & mask));
    }
}

static uint16_t
egc_read_vram(pc98_video_t *v, unsigned page, uint16_t off, uint16_t mem_mask)
{
    pc98_egc_t *e = &v->egc;
    uint16_t    plane_data[4];

    for (unsigned i = 0; i < 4; i++)
        plane_data[i] = direct_word(v, page, i, off);

    /* Original NEC EGCs return the previous CPU VRAM write in CPU-source
     * mode, rather than performing a VRAM read/compare (TSG notes p.30).
     * In particular, this read must not clock VRAM data into the shifter. */
    if (e->regs[2] & 0x0400)
        return (uint16_t) (e->cpu_data & mem_mask);

    if ((e->regs[2] & 0x0300) == 0x0100) {
        egc_load_pattern(e, plane_data, mem_mask, false);
    }

    if (e->first && !e->start && !e->loaded) {
        unsigned dst_off = (e->regs[6] >> 4) & 0x0f;
        unsigned src_off = e->regs[6] & 0x0f;
        if (dst_off >= src_off)
            e->start = 1;
        e->loaded = 1;
    } else {
        e->start = 1;
    }

    for (unsigned i = 0; i < 4; i++)
        e->src[i] = egc_shift(e, i, plane_data[i]);

    uint16_t ret;
    if (e->regs[2] & 0x2000) {
        /* Compare against the physical foreground latches, independently
         * of the choice of P input. Inhibited planes do not participate. */
        ret = 0xffff;
        for (unsigned i = 0; i < 4; i++) {
            if (!(e->regs[0] & (1u << i)))
                ret &= (uint16_t) ~(e->fg[i] ^ plane_data[i]);
        }
    } else {
        /* Lead-plane readback is the unshifted bus data. The shifted
         * four-plane source is held separately for the next write. */
        ret = plane_data[(e->regs[1] >> 8) & 3];
    }
    return (uint16_t) (ret & mem_mask);
}

static uint16_t
mask_first_bits(uint16_t mask, bool forward, unsigned dst_off)
{
    if (!dst_off)
        return mask;
    if (forward)
        return (uint16_t) (mask & (uint16_t) ~((1u << dst_off) - 1u));
    return (uint16_t) (mask & (uint16_t) ((1u << (16 - dst_off)) - 1u));
}

static uint16_t
end_mask_bits(bool forward, unsigned count)
{
    if (count == 0)
        return 0;
    if (count >= 16)
        return 0xffff;
    if (forward)
        return (uint16_t) ((1u << count) - 1u);
    return (uint16_t) ~((1u << (16 - count)) - 1u);
}

static void
egc_write_vram(pc98_video_t *v, unsigned page, uint16_t off,
               uint16_t data, uint16_t mem_mask, bool gdc)
{
    pc98_egc_t *e       = &v->egc;
    uint16_t    mask    = (uint16_t) (e->mask & mem_mask);
    bool        forward = !(e->regs[6] & 0x1000);
    unsigned    dst_off = (e->regs[6] >> 4) & 0x0f;
    unsigned    src_off = e->regs[6] & 0x0f;

    if ((e->regs[2] & 0x0300) == 0x0200) {
        uint16_t before[4];
        for (unsigned i = 0; i < 4; i++)
            before[i] = direct_word(v, page, i, off);
        egc_load_pattern(e, before, mem_mask, true);
    } else if (!gdc && (e->regs[2] & 0x0700) == 0x0500) {
        /* PS=source and SD=CPU loads the CPU data on a write, including
         * the dummy write which primes a shifted transfer. */
        uint16_t cpu[4] = { data, data, data, data };
        egc_load_pattern(e, cpu, mem_mask, false);
    }

    /* If source starts later than destination, the first host write primes
     * the shifter instead of producing destination bits. */
    if (!gdc && !e->start && src_off > dst_off) {
        if (e->regs[2] & 0x0400) {
            (void) egc_shift(e, 0, data);
            e->leftover[1] = e->leftover[2] = e->leftover[3] = e->leftover[0];
        }
        e->start = 1;
        return;
    }

    if (!gdc && e->first) {
        mask     = mask_first_bits(mask, forward, dst_off);
        e->start = 1;
    }

    unsigned transfer_bits = (mem_mask == 0xffff) ? 16u : 8u;
    if (!gdc && e->count < (int32_t) transfer_bits) {
        uint16_t endm = end_mask_bits(forward, (unsigned) (e->count > 0 ? e->count : 0));
        if (e->first && dst_off) {
            if (forward)
                endm = (uint16_t) (endm << dst_off);
            else
                endm = (uint16_t) (endm >> dst_off);
        }
        mask &= endm;
    }

    unsigned output_mode = (e->regs[2] >> 11) & 3;
    if (output_mode == 3)
        return; /* reserved */

    /* Clock all four shifters, including inhibited write planes. The access
     * mask controls the final stores, not the state of the source stream. */
    if (!gdc && (e->regs[2] & 0x0400)) {
        for (unsigned i = 0; i < 4; i++)
            e->src[i] = egc_shift(e, i, data);
    }

    for (unsigned i = 0; i < 4; i++) {
        if (e->regs[0] & (1u << i))
            continue; /* 1 = plane inhibited */

        uint16_t dst = direct_word(v, page, i, off);
        uint16_t src = e->src[i];
        uint16_t pat = color_pattern(v, i);
        uint16_t out;

        if (gdc && (e->regs[2] & 0x0400))
            src = data;

        switch (output_mode) {
            case 0:
                out = data;
                break;
            case 1:
                out = pc98_egc_rop((uint8_t) e->regs[2], src, pat, dst);
                break;
            case 2:
                out = pat;
                break;
            default:
                out = dst;
                break;
        }

        direct_word_write(v, page, i, off, out, mask);
    }

    /* Source/destination bit addresses are explicitly invalid for GDC
     * accesses (NEC VX supplement p.46). Do not consume the CPU stream. */
    if (gdc)
        return;

    int consumed;
    if (mem_mask != 0xffff)
        consumed = e->first ? 8 - (int) (dst_off & 7) : 8;
    else
        consumed = e->first ? 16 - (int) dst_off : 16;
    e->count -= consumed;
    e->first = 0;

    if (e->count <= 0) {
        /* The length setting is retained for the next raster. First/last
         * masks and the dummy-write stage delimit the new line. */
        egc_restart(e);
    }
}

void
pc98_video_init(pc98_video_t *v)
{
    memset(v, 0, sizeof(*v));
    /* PC-98 memory switches are battery-backed firmware settings exposed in
     * the tail of TVRAM.  Use the documented NEC defaults rather than an
     * all-ones power-on image; the IPL reads these very early during POST. */
    {
        static const uint8_t defaults[16] = {
            0xe1, 0x48, 0xe1, 0x05, 0xe1, 0x04, 0xe1, 0x00,
            0xe1, 0x01, 0xe1, 0x00, 0xe1, 0x00, 0xe1, 0x00
        };
        memcpy(v->memsw, defaults, sizeof(defaults));
    }
    upd7220_init(&v->text_gdc, v, tvram_gdc_read, tvram_gdc_write);
    upd7220_init(&v->graph_gdc, v, gvram_gdc_read, gvram_gdc_write);
    pc98_video_reset(v);
}

void
pc98_video_reset(pc98_video_t *v)
{
    memset(v->gvram, 0, sizeof(v->gvram));
    memset(v->tvram, 0, sizeof(v->tvram));
    memset(&v->grcg, 0, sizeof(v->grcg));
    egc_reset(&v->egc);
    v->egc_enabled = 0;
    v->cpu_page = v->display_page = 0;
    v->font_addr                  = 0;
    v->font_line                  = 0;
    v->font_lr                    = 0;
    v->kac                        = 0;
    memset(v->palette, 0, sizeof(v->palette));
    v->palette_index = 0;
    memset(v->analog_palette, 0, sizeof(v->analog_palette));
    memset(v->txt_scroll, 0, sizeof(v->txt_scroll));
    memset(v->video_ff, 0, sizeof(v->video_ff));
    memset(v->ex_video_ff, 0, sizeof(v->ex_video_ff));
    v->gfx_ff = 0;
    upd7220_reset(&v->text_gdc);
    upd7220_reset(&v->graph_gdc);
}

void
pc98_video_destroy(pc98_video_t *v)
{
    if (v->owns_ipl)
        free(v->ipl_rom);
    if (v->owns_font)
        free(v->font_rom);
    v->ipl_rom = v->font_rom = NULL;
    v->ipl_size = v->font_dump_size = 0;
    v->owns_ipl = v->owns_font = 0;
}

static int
read_exact_file(const char *path, uint8_t *dst, size_t expected)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0 || (size_t) size != expected) {
        fclose(f);
        return -2;
    }
    rewind(f);
    size_t got = fread(dst, 1, expected, f);
    fclose(f);
    return got == expected ? 0 : -3;
}

int
pc98_video_load_ipl(pc98_video_t *v, const char *path)
{
    uint8_t *buf = (uint8_t *) malloc(PC98_IPL_REGION_SIZE);
    if (!buf)
        return -4;
    int r = read_exact_file(path, buf, PC98_IPL_REGION_SIZE);
    if (r) {
        free(buf);
        return r;
    }
    if (v->owns_ipl)
        free(v->ipl_rom);
    v->ipl_rom  = buf;
    v->ipl_size = PC98_IPL_REGION_SIZE;
    v->owns_ipl = 1;
    return 0;
}

static void
pc98_font_copy_strip(uint8_t *dst, const uint8_t *dump,
                     unsigned dst_tile, unsigned src_tile,
                     int fill)
{
    /* The 0x46800-byte NEC/T98 FONT.ROM used by the PC-98 MAME sets is not
     * arranged in the same order as the GDC's 16-bit character code.  Treat
     * the file as a sequence of 32-byte tiles (16 left bytes followed by 16
     * right bytes), then place 0x20-tile strips into the native 1 MiB CG
     * address image.  This is the same logical reconstruction used by MAME's
     * init_pc9801_kanji(), but done directly without an intermediate 512 KiB
     * buffer. */
    for (unsigned n = 0; n < 0x20u; n++) {
        size_t d = ((size_t) (dst_tile + n) << 5);
        if (!fill) {
            memset(dst + d, 0, 0x20u);
            continue;
        }

        size_t s = ((size_t) (src_tile + n) << 5);
        for (unsigned line = 0; line < 16u; line++) {
            uint8_t left  = 0;
            uint8_t right = 0;
            if (s + line < PC98_FONT_DUMP_SIZE)
                left = dump[s + line];
            if (s + 16u + line < PC98_FONT_DUMP_SIZE)
                right = dump[s + 16u + line];
            dst[d + line * 2u]      = left;
            dst[d + line * 2u + 1u] = right;
        }
    }
}

static uint16_t
pc98_raw_kanji_tile(uint16_t tile)
{
    /* MAME bitswap<16>(tile,15,14,13,12,11,7,6,5,10,9,8,4,3,2,1,0). */
    return (uint16_t) ((tile & 0xf81fu) | ((tile & 0x00e0u) << 3) | ((tile & 0x0700u) >> 3));
}

int
pc98_video_install_font_set(pc98_video_t *v, const uint8_t *dump, size_t size,
                            const uint8_t *raw_kanji, size_t raw_size)
{
    if (!dump || size != PC98_FONT_DUMP_SIZE)
        return -2;
    if (raw_kanji && raw_size != PC98_RAW_KANJI_SIZE)
        return -2;

    uint8_t *buf = (uint8_t *) malloc(PC98_FONT_ADDRESS_SIZE);
    if (!buf)
        return -4;

    /* This exact 0x46800 FONT.ROM (including the common SHA1
     * 554b87377d176830d21bd03964dc71f8e98676b1 dump) is a compact chargen
     * image.  The first 0x1800 bytes are the 8x8 and 8x16 ANK banks and can
     * be consumed directly by our SBCS renderer.  The 16x16 portion is NOT a
     * simple rectangular JIS table: symbols and standard kanji are stored in
     * several strips.  Expanding it as low=01h..5Ch/high=20h..7Fh leaves the
     * actual TVRAM codes (for example xx20h..xx7Fh) at 0xff, which is exactly
     * why every Japanese character rendered as a solid 16x16 rectangle.
     *
     * Reconstruct the native code-indexed 1 MiB image using the strip map
     * used for the real PC-9801 chargen dump. */
    memset(buf, 0xff, PC98_FONT_ADDRESS_SIZE);

    /* FONT.ROM is only the chargen/overlay ROM.  The base JIS CG rows,
     * including symbols, hiragana and katakana, live in four separate
     * 24256C ROMs.  Convert their address-line permutation first, exactly as
     * init_pc9801_kanji() does, then apply the compact FONT.ROM strips below. */
    if (raw_kanji) {
        for (uint32_t tile = 0; tile < PC98_RAW_KANJI_SIZE / 0x20u; tile++) {
            const size_t src = (size_t) pc98_raw_kanji_tile((uint16_t) tile) << 5;
            memcpy(buf + ((size_t) tile << 5), raw_kanji + src, 0x20u);
        }
    }
    memcpy(buf, dump, 0x1800u);

#define COPY_STRIP(d, s) pc98_font_copy_strip(buf, dump, (d), (s), 1)
#define ZERO_STRIP(d)    pc98_font_copy_strip(buf, dump, (d), 0, 0)

    ZERO_STRIP(0x0800);
    ZERO_STRIP(0x0820);
    ZERO_STRIP(0x0840);
    ZERO_STRIP(0x0860);
    ZERO_STRIP(0x0900);
    COPY_STRIP(0x0920, 0x03c0);
    COPY_STRIP(0x0940, 0x03e0);
    COPY_STRIP(0x0960, 0x0400);
    ZERO_STRIP(0x0a00);
    COPY_STRIP(0x0a20, 0x0420);
    COPY_STRIP(0x0a40, 0x0440);
    COPY_STRIP(0x0a60, 0x0460);
    ZERO_STRIP(0x0b00);
    COPY_STRIP(0x0b20, 0x0480);
    COPY_STRIP(0x0b40, 0x04a0);
    COPY_STRIP(0x0b60, 0x04c0);
    ZERO_STRIP(0x0c00);
    COPY_STRIP(0x0c20, 0x04e0);
    COPY_STRIP(0x0c40, 0x0500);
    COPY_STRIP(0x0c60, 0x0520);
    ZERO_STRIP(0x0d00);
    COPY_STRIP(0x0d20, 0x0540);
    COPY_STRIP(0x0d40, 0x0560);
    COPY_STRIP(0x0d60, 0x0580);
    ZERO_STRIP(0x0e00);
    ZERO_STRIP(0x0e20);
    ZERO_STRIP(0x0e40);
    ZERO_STRIP(0x0e60);
    ZERO_STRIP(0x0f00);
    ZERO_STRIP(0x0f20);
    ZERO_STRIP(0x0f40);
    ZERO_STRIP(0x0f60);

    for (unsigned dst_row = 0x1000u, src_row = 0x0660u;
         dst_row < 0x8000u;
         dst_row += 0x0100u, src_row += 0x0060u) {
        ZERO_STRIP(dst_row);
        COPY_STRIP(dst_row + 0x20u, src_row + 0x00u);
        COPY_STRIP(dst_row + 0x40u, src_row + 0x20u);
        COPY_STRIP(dst_row + 0x60u, src_row + 0x40u);
    }

#undef COPY_STRIP
#undef ZERO_STRIP

    if (v->owns_font)
        free(v->font_rom);
    v->font_rom       = buf;
    v->font_dump_size = PC98_FONT_DUMP_SIZE;
    v->owns_font      = 1;
    return 0;
}

int
pc98_video_install_font_dump(pc98_video_t *v, const uint8_t *dump, size_t size)
{
    return pc98_video_install_font_set(v, dump, size, NULL, 0);
}

int
pc98_video_load_font(pc98_video_t *v, const char *path)
{
    uint8_t *dump = (uint8_t *) malloc(PC98_FONT_DUMP_SIZE);
    if (!dump)
        return -4;

    int r = read_exact_file(path, dump, PC98_FONT_DUMP_SIZE);
    if (!r)
        r = pc98_video_install_font_dump(v, dump, PC98_FONT_DUMP_SIZE);
    free(dump);
    return r;
}

/*
 * One decoder for both CG windows.
 *
 * The character generator is reachable two ways, and the VX firmware uses
 * both: the A4000h memory window, where the scanline comes from the address
 * bus, and the port pair A5h/A9h, where it comes from the A5h latch.  They
 * must resolve a given latched code to the same byte, because the CG BIOS
 * at FEB73h/FEC51h writes a character through one and reads it back through
 * the other.
 *
 * `line` is the doubled scanline (0, 2, ... 30) as latched from A5h bits
 * 3:0, and `lr` the left/right half from A5h bit 5.  The selection rules,
 * in the order the hardware applies them:
 *
 *   low code byte == 0   an ANK (single-byte) character, whose code is the
 *                        HIGH byte; 8x8 or 8x16 according to FONTSEL.  ROM.
 *   code & 7c00 == 0800  the 8x16 single-byte area.  ROM.
 *   code 56xx / 57xx     the programmable character area.  RAM.
 *   anything else        ordinary kanji.  ROM.
 */
uint32_t
pc98_cg_offset(const pc98_video_t *v, uint32_t line, uint32_t lr,
               int *writable)
{
    const uint32_t line_index = (line >> 1) & 0x0fu;

    if (writable)
        *writable = 0;

    /* ANK: the code sits in the high byte and the bank depends on FONTSEL. */
    if (!(v->font_addr & 0x00ffu)) {
        const unsigned char_size = v->video_ff[3] ? 1u : 0u;
        const uint32_t idx       = ((uint32_t) (v->font_addr >> 8) * (8u << char_size)) + (char_size * 0x800u) + line_index;
        return idx & (PC98_FONT_ADDRESS_SIZE - 1u);
    }

    uint32_t off = (uint32_t) (v->font_addr & 0x7f7f) << 5;
    off |= line & 0x1eu;

    if ((v->font_addr & 0x7c00u) == 0x0800u)
        return off & (PC98_FONT_ADDRESS_SIZE - 1u);

    off |= lr & 1u;

    if ((v->font_addr & 0xff00u) == 0x5600u || (v->font_addr & 0xff00u) == 0x5700u) {
        off |= (uint32_t) (!v->kac) << 12;
        if (writable)
            *writable = 1;
    }

    return off & (PC98_FONT_ADDRESS_SIZE - 1u);
}

uint32_t
pc98_font_window_offset(const pc98_video_t *v)
{
    return pc98_cg_offset(v, v->font_line, v->font_lr, NULL);
}

/*
 * Decode an access through the A4000h character-generator memory window.
 *
 * For ordinary two-byte Kanji, the CPU byte lane selects the left/right
 * half: even address = one byte, odd address = the other.  The 56xx/57xx
 * programmable-character area is special: A5h bit 5 supplies L/R, so both
 * byte lanes resolve through that latch.  ANK and the 8x16 single-byte area
 * ignore the LR argument in pc98_cg_offset(), so passing the bus lane there
 * is harmless and keeps all address decoding in one place.
 */
uint32_t
pc98_cg_window_offset(const pc98_video_t *v, uint32_t window_off,
                      int *writable)
{
    const uint16_t hi = (uint16_t) (v->font_addr & 0xff00u);
    const uint32_t lr = (hi == 0x5600u || hi == 0x5700u)
        ? (uint32_t) v->font_lr
        : (window_off & 1u);

    return pc98_cg_offset(v, window_off & 0x1eu, lr, writable);
}

uint16_t
pc98_gvram_read16(pc98_video_t *v, unsigned plane, uint16_t off, uint16_t mem_mask)
{
    uint16_t native_mask = egc_bus_mask(mem_mask);
    uint16_t native;

    if (!(v->grcg.mode & 0x80))
        native = (uint16_t) (direct_word(v, v->cpu_page, plane, off) & native_mask);
    else if (v->egc_enabled)
        native = egc_read_vram(v, v->cpu_page, off, native_mask);
    else if (v->grcg.mode & 0x40)
        native = 0; /* NEC prohibits RMW reads; zero is an emulation policy. */
    else {
        uint16_t mismatch = 0;
        for (unsigned i = 0; i < 4; i++) {
            if (!(v->grcg.mode & (1u << i))) {
                uint16_t tile = (uint16_t) (v->grcg.tile[i] | ((uint16_t) v->grcg.tile[i] << 8));
                mismatch |= (uint16_t) (direct_word(v, v->cpu_page, i, off) ^ tile);
            }
        }
        native = (uint16_t) (~mismatch & native_mask);
    }

    /* The CPU aperture reverses bit numbering within each byte relative to
     * the GDC-native VRAM representation. */
    return (uint16_t) (egc_bus_mask(native) & mem_mask);
}

static void
charger_write(pc98_video_t *v, unsigned page, unsigned plane,
              uint16_t off, uint16_t data, uint16_t mem_mask,
              bool gdc)
{
    if (!gdc)
        v->egc.cpu_data = (uint16_t) (data & mem_mask);

    if (!(v->grcg.mode & 0x80)) {
        direct_word_write(v, page, plane, off, data, mem_mask);
        return;
    }

    if (v->egc_enabled) {
        egc_write_vram(v, page, off, data, mem_mask, gdc);
        return;
    }

    /* GRCG RMW uses the same mask latch as extended EGC mode. A byte
     * access clears the other lane rather than merging its previous mask.
     * TSG hardware experiments, pp.28-29. */
    if (v->grcg.mode & 0x40) {
        v->egc.mask    = (uint16_t) (data & mem_mask);
        v->egc.regs[4] = egc_bus_mask(v->egc.mask);
    }

    for (unsigned i = 0; i < 4; i++) {
        if (v->grcg.mode & (1u << i))
            continue;
        uint16_t tile = (uint16_t) (v->grcg.tile[i] | ((uint16_t) v->grcg.tile[i] << 8));
        if (v->grcg.mode & 0x40) {
            uint16_t old       = direct_word(v, page, i, off);
            uint16_t effective = (uint16_t) (data & mem_mask);
            uint16_t out       = (uint16_t) ((old & ~effective) | (tile & effective));
            direct_word_write(v, page, i, off, out, mem_mask);
        } else {
            direct_word_write(v, page, i, off, tile, mem_mask);
        }
    }
}

void
pc98_gvram_write16(pc98_video_t *v, unsigned plane, uint16_t off,
                   uint16_t data, uint16_t mem_mask)
{
    /* The GDC and CPU see different bit numbering, but share the charger
     * and its latches. Page selection is independent for the two masters. */
    charger_write(v, v->cpu_page, plane, off, egc_bus_mask(data),
                  egc_bus_mask(mem_mask), false);
}

uint8_t
pc98_gvram_read8(pc98_video_t *v, unsigned plane, uint16_t byte_off)
{
    uint16_t off  = (uint16_t) (byte_off >> 1);
    uint16_t mask = (byte_off & 1) ? 0xff00 : 0x00ff;
    uint16_t w    = pc98_gvram_read16(v, plane, off, mask);
    return (byte_off & 1) ? (uint8_t) (w >> 8) : (uint8_t) w;
}

void
pc98_gvram_write8(pc98_video_t *v, unsigned plane, uint16_t byte_off, uint8_t data)
{
    uint16_t off = (uint16_t) (byte_off >> 1);
    if (byte_off & 1)
        pc98_gvram_write16(v, plane, off, (uint16_t) data << 8, 0xff00);
    else
        pc98_gvram_write16(v, plane, off, data, 0x00ff);
}

static void
video_ff_write(pc98_video_t *v, uint8_t data)
{
    unsigned reg = (data & 0x0e) >> 1;
    switch (reg) {
        case 1:
            v->gfx_ff = 1;
            break;
        case 4:
            if (v->gfx_ff) {
                v->video_ff[reg] = data & 1;
                v->gfx_ff        = 0;
            }
            break;
        default:
            v->video_ff[reg] = data & 1;
            break;
    }
    v->kac = v->video_ff[5];
}

static void
ex_video_ff_write(pc98_video_t *v, uint8_t data)
{
    if ((data & 0xf0) != 0)
        return;
    unsigned reg = (data & 0x0e) >> 1;
    if (reg == 2 && !v->ex_video_ff[3])
        return;
    if (reg < 4)
        v->ex_video_ff[reg] = data & 1;
    v->egc_enabled = v->ex_video_ff[2];
}

uint8_t
pc98_video_in8(pc98_video_t *v, uint16_t port)
{
    switch (port) {
        case 0x0060:
            return upd7220_read(&v->text_gdc, 0);
        case 0x0062:
            return upd7220_read(&v->text_gdc, 1);
        case 0x0070:
            return v->txt_scroll[0];
        case 0x0072:
            return v->txt_scroll[1];
        case 0x0074:
            return v->txt_scroll[2];
        case 0x0076:
            return v->txt_scroll[3];
        case 0x0078:
            return v->txt_scroll[4];
        case 0x007a:
            return v->txt_scroll[5];
        case 0x00a0:
            return upd7220_read(&v->graph_gdc, 0);
        case 0x00a2:
            return upd7220_read(&v->graph_gdc, 1);
        case 0x00a4:
            return v->display_page & 1;
        case 0x00a6:
            return v->cpu_page & 1;
        case 0x00a8:
            return v->ex_video_ff[0] ? v->palette_index : v->palette[0];
        case 0x00aa:
            return v->ex_video_ff[0] ? v->analog_palette[v->palette_index][1] : v->palette[1];
        case 0x00ac:
            return v->ex_video_ff[0] ? v->analog_palette[v->palette_index][0] : v->palette[2];
        case 0x00ae:
            return v->ex_video_ff[0] ? v->analog_palette[v->palette_index][2] : v->palette[3];
        case 0x00a9:
            if (v->font_rom)
                return v->font_rom[pc98_font_window_offset(v)];
            return 0xff;
        default:
            /* VX EGC ports are write-only. Readback exists on later H98/GS
             * hardware, not on the machine this device emulates. */
            return 0xff;
    }
}

void
pc98_video_out8(pc98_video_t *v, uint16_t port, uint8_t data)
{
    switch (port) {
        case 0x0060:
            upd7220_write(&v->text_gdc, 0, data);
            return;
        case 0x0062:
            upd7220_write(&v->text_gdc, 1, data);
            return;
        case 0x0068:
            video_ff_write(v, data);
            return;
        case 0x006a:
            ex_video_ff_write(v, data);
            return;
        case 0x0070:
            v->txt_scroll[0] = data;
            return;
        case 0x0072:
            v->txt_scroll[1] = data;
            return;
        case 0x0074:
            v->txt_scroll[2] = data;
            return;
        case 0x0076:
            v->txt_scroll[3] = data;
            return;
        case 0x0078:
            v->txt_scroll[4] = data;
            return;
        case 0x007a:
            v->txt_scroll[5] = data;
            return;
        case 0x007c:
            v->grcg.mode       = data;
            v->grcg.tile_index = 0;
            return;
        case 0x007e:
            {
                unsigned i      = v->grcg.tile_index & 3;
                uint8_t  tile   = bitrev8(data);
                v->grcg.tile[i] = tile;
                v->egc.fg[i]    = (uint16_t) (tile | ((uint16_t) tile << 8));
            }
            v->grcg.tile_index = (uint8_t) ((v->grcg.tile_index + 1) & 3);
            return;
        case 0x00a0:
            upd7220_write(&v->graph_gdc, 0, data);
            return;
        case 0x00a2:
            upd7220_write(&v->graph_gdc, 1, data);
            return;
        /* NEC CG code latch: A1h is the low JIS byte, A3h is the normalized
         * high byte (JIS high - 20h).  The VX BIOS writes A3h first and A1h
         * second after normalising the JIS code. */
        case 0x00a1:
            v->font_addr = (uint16_t) ((v->font_addr & 0xff00) | data);
            return;
        case 0x00a3:
            v->font_addr = (uint16_t) ((v->font_addr & 0x00ff) | ((uint16_t) data << 8));
            return;
        case 0x00a5:
            v->font_line = (uint8_t) ((data & 0x0f) << 1);
            v->font_lr   = (uint8_t) (((data >> 5) & 1) ^ 1);
            return;
        case 0x00a4:
            v->display_page = data & 1;
            return;
        case 0x00a6:
            v->cpu_page = data & 1;
            return;
        case 0x00a8:
            if (v->ex_video_ff[0])
                v->palette_index = data & 0x0f;
            else
                v->palette[0] = data;
            return;
        case 0x00aa:
            if (v->ex_video_ff[0])
                v->analog_palette[v->palette_index][1] = data & 0x0f;
            else
                v->palette[1] = data;
            return;
        case 0x00ac:
            if (v->ex_video_ff[0])
                v->analog_palette[v->palette_index][0] = data & 0x0f;
            else
                v->palette[2] = data;
            return;
        case 0x00ae:
            if (v->ex_video_ff[0])
                v->analog_palette[v->palette_index][2] = data & 0x0f;
            else
                v->palette[3] = data;
            return;
        case 0x00a9:
            if (v->font_rom) {
                int            writable = 0;
                const uint32_t off      = pc98_cg_offset(v, v->font_line, v->font_lr,
                                                         &writable);
                /* Only the 56xx/57xx programmable area is RAM; everything else
                 * the window can select is character ROM. */
                if (writable)
                    v->font_rom[off] = data;
            }
            return;
        default:
            if (port >= 0x04a0 && port <= 0x04af) {
                /* Tolerate byte programming for existing guests. Real VX EGC
                 * software must use even-port word OUTs; this fallback does not
                 * emulate the bus hang from unsupported accesses. */
                unsigned reg  = (port - 0x04a0) >> 1;
                uint16_t mask = (port & 1) ? 0xff00 : 0x00ff;
                uint16_t word = (port & 1) ? (uint16_t) data << 8 : data;
                egc_reg_write(v, reg, word, mask);
            }
            return;
    }
}

uint16_t
pc98_video_in16(pc98_video_t *v, uint16_t port)
{
    return (uint16_t) (pc98_video_in8(v, port) | ((uint16_t) pc98_video_in8(v, (uint16_t) (port + 1)) << 8));
}

void
pc98_video_out16(pc98_video_t *v, uint16_t port, uint16_t data)
{
    if (port >= 0x04a0 && port <= 0x04ae && !(port & 1)) {
        egc_reg_write(v, (port - 0x04a0) >> 1, data, 0xffff);
        return;
    }
    pc98_video_out8(v, port, (uint8_t) data);
    pc98_video_out8(v, (uint16_t) (port + 1), (uint8_t) (data >> 8));
}
