/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Vérité pixel engine, ported from rust-dos. */

#include "internal.h"

typedef struct {
    pixel_param even, odd;
} param_pair;
typedef struct {
    param_pair pair;
    bool       shadow;
} first_pair;
typedef struct {
    int32_t channel[4];
} color;

static const first_pair pair1[] = {
    { { P_X, P_NONE },    false },
    { { P_Q, P_NONE },    true  },
    { { P_X, P_Y },       false },
    { { P_Q, P_Y },       true  },
    { { P_X, P_U },       false },
    { { P_Q, P_U },       true  },
    { { P_X, P_R },       false },
    { { P_Q, P_R },       true  },
    { { P_X, P_A },       false },
    { { P_Q, P_A },       true  },
    { { P_Z, P_X },       false },
    { { P_NONE, P_NONE }, true  },
    { { P_X, P_F },       false },
    { { P_Q, P_F },       true  },
    { { P_Y, P_U },       true  },
    { { P_Q, P_U },       true  },
};

static const param_pair pair2[] = {
    { P_X,    P_V    },
    { P_Y,    P_V    },
    { P_R,    P_V    },
    { P_NONE, P_V    },
    { P_I,    P_V    },
    { P_Z,    P_V    },
    { P_A,    P_V    },
    { P_Z,    P_U    },
    { P_G,    P_B    },
    { P_Z,    P_A    },
    { P_Z,    P_NONE },
    { P_NONE, P_F    },
    { P_Z,    P_F    },
    { P_F,    P_V    },
    { P_R,    P_F    },
    { P_R,    P_A    },
    { P_R,    P_NONE },
    { P_Z,    P_R    },
    { P_NONE, P_A    },
    { P_F,    P_A    },
    { P_I,    P_NONE },
    { P_Z,    P_I    },
    { P_I,    P_A    },
    { P_I,    P_F    },
};

static const param_pair pair3[] = {
    { P_G,    P_B    },
    { P_Z,    P_A    },
    { P_Z,    P_NONE },
    { P_NONE, P_F    },
    { P_Z,    P_F    },
    { P_NONE, P_NONE },
    { P_R,    P_F    },
    { P_R,    P_A    },
    { P_R,    P_NONE },
    { P_Z,    P_R    },
    { P_NONE, P_A    },
    { P_F,    P_A    },
    { P_I,    P_NONE },
    { P_Z,    P_I    },
    { P_I,    P_A    },
    { P_I,    P_F    },
};

enum {
    DST_ZBUF_WRITE    = 1 << 16,
    DST_YUV2RGB       = 1 << 18,
    DST_BLEND         = 1 << 19,
    DST_DITHER        = 1 << 20,
    DST_FOG           = 1 << 21,
    DST_NO_PAD        = 1 << 22,
    DST_READ_DISABLE  = 1 << 23,
    DST_BGR           = 1 << 24,
    DST_TRANSP_REJECT = 1 << 25,
};

enum {
    SRC_FILTER           = 1 << 7,
    SRC_NO_PAD           = 1 << 9,
    SRC_SWAP_UV          = 1 << 10,
    SRC_CHROMA           = 1 << 11,
    SRC_Z_SCISSOR        = 1 << 12,
    SRC_BGR              = 1 << 13,
    SRC_CHROMA_BLACK_YUV = 1 << 14,
    SRC_U_CLAMP          = 1 << 15,
    SRC_V_CLAMP          = 1 << 16,
};

void
pixel_reset(pixel_engine *pe)
{
    *pe = (pixel_engine) {
        .src_mask     = 0x07FF07FF,
        .dst_mode     = 0xC,
        .dst_fmt      = 4,
        .pmask        = UINT32_MAX,
        .scissor_x    = 0x800,
        .scissor_y    = 0x800,
        .fg           = 0xFF000000,
        .yuv_contrast = 41,
        .u_mask       = 0x7FF,
        .v_mask       = 0x7FF,
        .q            = 1 << 24,
        .a            = 255 << 16,
        .f            = 255 << 16,
    };
}

static int32_t
clamp(int32_t value, int32_t max)
{
    return value < 0 ? 0 : value > max ? max
                                       : value;
}

static int32_t
sat(int32_t value)
{
    return clamp(value >> 16, 255);
}

static unsigned
fmt_bytes(unsigned fmt)
{
    if (fmt >= 1 && fmt <= 3)
        return 1;
    if (fmt >= 8 && fmt <= 10)
        return 0;
    return fmt == 12 || fmt == 13 ? 4 : 2;
}

static uint32_t
line_stride(uint32_t code)
{
    static const uint32_t high[] = { 0, 16, 32, 64, 128, 1024, 2048, 4096 };
    static const uint32_t low[]  = { 0, 256, 512, 1024, 4, 768, 576, 1152 };
    return high[(code >> 4) & 7] + low[code & 7];
}

static uint32_t
read_pixel(verite_memory mem, uint32_t addr, unsigned bytes)
{
    if (bytes == 1)
        return mem_read8(mem, addr);
    if (bytes == 2)
        return mem_read16(mem, addr);
    return mem_read32(mem, addr);
}

static void
write_pixel(verite_memory mem, uint32_t addr, unsigned bytes, uint32_t v)
{
    if (bytes == 1)
        mem_write8(mem, addr, v);
    else if (bytes == 2)
        mem_write16(mem, addr, v);
    else
        mem_write32(mem, addr, v);
}

static uint32_t
rop(uint32_t code, uint32_t s, uint32_t d)
{
    return (~s & ~d & (0U - (code & 1))) | (~s & d & (0U - ((code >> 1) & 1))) | (s & ~d & (0U - ((code >> 2) & 1))) | (s & d & (0U - ((code >> 3) & 1)));
}

static bool
rop_reads(uint32_t code)
{
    return ((code >> 1) ^ code) & 5;
}

static int32_t
widen(uint32_t value, unsigned bits, bool no_pad)
{
    unsigned max = (1U << bits) - 1;
    value &= max;
    return no_pad ? value << (8 - bits) : (value * 255 + max / 2) / max;
}

static color
argb(uint32_t word)
{
    return (color) {
        { (word >> 16) & 255, (word >> 8) & 255, word & 255, word >> 24 }
    };
}

static color
decode(unsigned fmt, uint32_t raw, bool bgr, bool no_pad)
{
    color c;
    switch (fmt) {
        case 1:
            c = (color) {
                { widen(raw >> 5, 3, no_pad), widen(raw >> 2, 3, no_pad), widen(raw, 2, no_pad), 255 }
            };
            break;
        case 2:
            {
                int32_t i = raw & 255;
                c         = (color) {
                            { i, i, i, 255 }
                };
                break;
            }
        case 3:
            c = (color) {
                { 255, 255, 255, raw & 255 }
            };
            break;
        case 5:
            c = (color) {
                { widen(raw >> 8, 4, no_pad), widen(raw >> 4, 4, no_pad), widen(raw, 4, no_pad), widen(raw >> 12, 4, no_pad) }
            };
            break;
        case 6:
            c = (color) {
                { widen(raw >> 10, 5, no_pad), widen(raw >> 5, 5, no_pad), widen(raw, 5, no_pad), raw & 0x8000 ? 255 : 0 }
            };
            break;
        case 12:
            c = argb(raw);
            break;
        default:
            c = (color) {
                { widen(raw >> 11, 5, no_pad), widen(raw >> 5, 6, no_pad), widen(raw, 5, no_pad), 255 }
            };
            break;
    }
    if (bgr) {
        int32_t r    = c.channel[0];
        c.channel[0] = c.channel[2];
        c.channel[2] = r;
    }
    return c;
}

static uint32_t
quantize(int32_t value, unsigned n, unsigned dither)
{
    unsigned v = clamp(value, 255) + ((dither << 4) >> n);
    return (v < 255 ? v : 255) >> (8 - n);
}

static uint32_t
encode(unsigned fmt, color c, unsigned dither, bool bgr)
{
    int32_t r = c.channel[bgr ? 2 : 0], g = c.channel[1], b = c.channel[bgr ? 0 : 2], a = c.channel[3];
    switch (fmt) {
        case 1:
            return (quantize(r, 3, dither) << 5) | (quantize(g, 3, dither) << 2) | quantize(b, 2, dither);
        case 2:
            return quantize(r, 8, dither);
        case 3:
            return quantize(a, 8, dither);
        case 5:
            return (quantize(a, 4, dither) << 12) | (quantize(r, 4, dither) << 8) | (quantize(g, 4, dither) << 4) | quantize(b, 4, dither);
        case 6:
            return (quantize(a, 1, dither) << 15) | (quantize(r, 5, dither) << 10) | (quantize(g, 5, dither) << 5) | quantize(b, 5, dither);
        case 12:
            return (quantize(a, 8, dither) << 24) | (quantize(r, 8, dither) << 16) | (quantize(g, 8, dither) << 8) | quantize(b, 8, dither);
        default:
            return (quantize(r, 5, dither) << 11) | (quantize(g, 6, dither) << 5) | quantize(b, 5, dither);
    }
}

static void
field_set(uint32_t *reg, uint32_t mask, uint32_t value)
{
    *reg = (*reg & ~mask) | (value & mask);
}

void
pixel_set(pixel_engine *pe, pixel_param param, uint32_t value)
{
    int32_t v = (int32_t) value;
    switch (param) {
        case P_NONE:
            break;
        case P_XSHADOW:
            pe->x_shadow = v;
            break;
        case P_X:
            pe->x = v;
            break;
        case P_Y:
            pe->y = v;
            break;
        case P_Z:
            pe->z     = v;
            pe->z_ovf = (value & 0xf8000000) && (pe->src_mode & SRC_Z_SCISSOR);
            break;
        case P_U:
        case P_V:
            if ((param == P_U) != !!(pe->src_mode & SRC_SWAP_UV)) {
                pe->u      = v;
                pe->u_mask = pe->src_mask & 0x7ff;
            } else {
                pe->v      = v;
                pe->v_mask = (pe->src_mask >> 16) & 0x7ff;
            }
            break;
        case P_Q:
            pe->q = value;
            break;
        case P_R:
            pe->r = v;
            field_set(&pe->fg, 0xff0000, (uint32_t) sat(v) << 16);
            break;
        case P_G:
            pe->g = v;
            field_set(&pe->fg, 0xff00, (uint32_t) sat(v) << 8);
            break;
        case P_B:
            pe->b = v;
            field_set(&pe->fg, 0xff, sat(v));
            break;
        case P_A:
            pe->a = v;
            field_set(&pe->fg, 0xff000000, (uint32_t) sat(v) << 24);
            break;
        case P_F:
            pe->f = v;
            break;
        case P_I:
            {
                uint32_t gray = sat(v);
                pe->r = pe->g = pe->b = v;
                field_set(&pe->fg, 0xffffff, gray * 0x010101);
                break;
            }
        case P_CNT:
            pe->cnt = value >> 16;
            break;
    }
}

static void
set_fg(pixel_engine *pe, uint32_t value, bool alpha)
{
    field_set(&pe->fg, alpha ? UINT32_MAX : 0xffffff, value);
    color c = argb(pe->fg);
    pe->r   = c.channel[0] << 16;
    pe->g   = c.channel[1] << 16;
    pe->b   = c.channel[2] << 16;
    pe->a   = c.channel[3] << 16;
}

uint32_t
pixel_state(const pixel_engine *pe, uint8_t index)
{
    switch (index) {
        case 0:
            return pe->src_base;
        case 1:
            return pe->src_mode;
        case 2:
            return pe->src_mask;
        case 3:
            return pe->stride;
        case 4:
            return pe->dst_base;
        case 5:
            return pe->dst_mode;
        case 6:
            return pe->dst_fmt;
        case 7:
            return pe->pmask;
        case 8:
            return pe->pattern;
        case 12:
            return pe->pat_mode;
        case 13:
            return pe->pat_offset;
        case 14:
            return pe->scissor_x;
        case 15:
            return pe->scissor_y;
        case 16:
            return pe->z_base;
        case 17:
            return pe->dither_offset;
        case 19:
            return pe->fg;
        case 20:
            return pe->bg;
        case 21:
            return pe->fog_color;
        case 22:
            return pe->dst_color;
        case 23:
            return pe->chroma_color;
        case 24:
            return pe->yuv_contrast;
        case 26:
            return pe->chroma_mask;
        case 28:
            return ((uint32_t) pe->pick_valid << 16) | ((uint32_t) pe->pick_hit << 17);
        case 29:
            return pe->alpha_thres;
        case 30:
            return pe->spec_color;
        case 112:
            return pe->x;
        case 113:
            return pe->y;
        case 114:
            return ((uint32_t) pe->z_ovf << 27) | ((uint32_t) pe->z & 0x07ffffff);
        case 117:
            return (uint32_t) sat(pe->f) << 16;
        case 119:
            return (uint32_t) pe->u & 0x07ffff00;
        case 121:
            return (uint32_t) pe->v & 0x07ffff00;
        case 123:
            return (uint32_t) pe->u_dir << 31;
        case 125:
            return (uint32_t) pe->v_dir << 31;
        case 131:
            return pe->drawctl;
        case 132:
            return ((uint32_t) pe->x_shadow & 0xffff0000) | pe->x_dir;
        case 133:
            return pe->cnt << 16;
        default:
            return index >= 32 && index <= 39 ? pe->tex4pal[index - 32] : 0;
    }
}

static bool
inside(const pixel_engine *pe, int32_t x, int32_t y)
{
    return x >= (int32_t) ((pe->scissor_x >> 16) & 0xfff) && x < (int32_t) (pe->scissor_x & 0xfff) && x < 2048 && y >= (int32_t) ((pe->scissor_y >> 16) & 0xfff) && y < (int32_t) (pe->scissor_y & 0xfff) && y < 2048;
}

/* Returns false for a transparent pattern pixel, advancing PatOffset either way. */
static bool
pattern_pixel(pixel_engine *pe, bool *background)
{
    *background = false;
    if (!(pe->pat_mode & 0x10000))
        return true;
    *background    = !(pe->pattern & (UINT32_C(1) << (31 - (pe->pat_offset & 31))));
    pe->pat_offset = (pe->pat_offset & 31) != (pe->pat_mode & 31) ? (pe->pat_offset + 1) & 31 : 0;
    return !*background || (pe->pat_mode & 0x20000);
}

static unsigned
dst_bytes(const pixel_engine *pe)
{
    unsigned bytes = fmt_bytes(pe->dst_fmt & 15);
    return bytes ? bytes : 1;
}

static uint32_t
dst_at(const pixel_engine *pe, int32_t x, int32_t y)
{
    return pe->dst_base + line_stride(pe->stride >> 8) * (uint32_t) y + (uint32_t) x * dst_bytes(pe);
}

static unsigned
src_fmt(const pixel_engine *pe)
{
    unsigned fmt = pe->src_mode & 15;
    return fmt ? fmt : pe->dst_fmt & 15;
}

static bool
keyed(const pixel_engine *pe, uint32_t raw, unsigned bytes)
{
    if (bytes == 1)
        raw *= 0x01010101;
    else if (bytes == 2)
        raw *= 0x00010001;
    return (pe->src_mode & SRC_CHROMA) && ((raw ^ pe->chroma_color) & pe->chroma_mask) == 0;
}

static color
key_color(const pixel_engine *pe)
{
    return pe->src_mode & SRC_CHROMA_BLACK_YUV ? (color) {
        { 128, 16, 128, 0 }
    }
                                               : (color) { 0 };
}

static color
texel(const pixel_engine *pe, int32_t x, int32_t y, verite_memory mem)
{
    uint32_t tx  = pe->src_mode & SRC_U_CLAMP ? (uint32_t) clamp(x, pe->u_mask) : (uint32_t) x & pe->u_mask;
    uint32_t ty  = pe->src_mode & SRC_V_CLAMP ? (uint32_t) clamp(y, pe->v_mask) : (uint32_t) y & pe->v_mask;
    unsigned fmt = src_fmt(pe);
    uint32_t row = pe->src_base + line_stride(pe->stride) * ty;
    bool     bgr = !!(pe->src_mode & SRC_BGR), no_pad = !!(pe->src_mode & SRC_NO_PAD);
    if (fmt >= 8 && fmt <= 10) {
        uint32_t byte  = mem_read8(mem, row + tx / 2);
        unsigned index = tx & 1 ? byte & 15 : byte >> 4;
        uint32_t entry = (pe->tex4pal[index / 2] >> (index & 1 ? 0 : 16)) & 0xffff;
        return decode(fmt - 4, entry, bgr, no_pad);
    }
    if (fmt == 13) {
        uint32_t word = mem_read32(mem, row + tx / 2 * 4);
        if (keyed(pe, word, 4))
            return key_color(pe);
        return (color) {
            { (word >> (bgr ? 0 : 16)) & 255, (word >> (tx & 1 ? 8 : 24)) & 255,
             (word >> (bgr ? 16 : 0)) & 255, 255 }
        };
    }
    unsigned bytes = fmt_bytes(fmt);
    uint32_t raw   = read_pixel(mem, row + tx * bytes, bytes);
    return keyed(pe, raw, bytes) ? key_color(pe) : decode(fmt, raw, bgr, no_pad);
}

static color
sample(const pixel_engine *pe, int32_t u, int32_t v, verite_memory mem)
{
    int32_t tx = u >> 16, ty = v >> 16;
    if (!(pe->src_mode & SRC_FILTER))
        return texel(pe, tx, ty, mem);
    int32_t fu = (u >> 8) & 255, fv = (v >> 8) & 255;
    int32_t weights[] = { (256 - fu) * (256 - fv), fu * (256 - fv), (256 - fu) * fv, fu * fv };
    color   sum       = { 0 };
    for (int tap = 0; tap < 4; tap++) {
        color c = texel(pe, tx + (tap & 1), ty + (tap >> 1), mem);
        for (unsigned k = 0; k < 4; k++)
            sum.channel[k] += c.channel[k] * weights[tap];
    }
    for (unsigned k = 0; k < 4; k++)
        sum.channel[k] = (sum.channel[k] + 32768) >> 16;
    return sum;
}

static int32_t
factor(uint32_t func, bool source, unsigned k, color s, color d)
{
    int32_t saturation = k == 3 ? 255 : clamp(s.channel[3], 255 - d.channel[3]);
    switch (func) {
        case 0:
            return source ? d.channel[k] : s.channel[k];
        case 1:
            return 255 - (source ? d.channel[k] : s.channel[k]);
        case 2:
            return s.channel[3];
        case 3:
            return 255 - s.channel[3];
        case 4:
            return d.channel[3];
        case 5:
            return 255 - d.channel[3];
        case 6:
            return 0;
        case 8:
            return saturation;
        case 9:
            return 255 - saturation;
        case 10:
            return source ? s.channel[k] : d.channel[k];
        case 11:
            return 255 - (source ? s.channel[k] : d.channel[k]);
        default:
            return 255;
    }
}

static color
blend(const pixel_engine *pe, color s, color d)
{
    unsigned sf = (pe->dst_mode >> 4) & 15, df = (pe->dst_mode >> 8) & 15;
    color    out;
    for (unsigned k = 0; k < 4; k++) {
        int32_t a      = sf == 15 ? s.channel[k] - 128 : (s.channel[k] * factor(sf, true, k, s, d) + 127) / 255;
        int32_t b      = df == 15 ? d.channel[k] - 128 : (d.channel[k] * factor(df, false, k, s, d) + 127) / 255;
        out.channel[k] = clamp(a + b, 255);
    }
    return out;
}

static color
yuv_to_rgb(const pixel_engine *pe, color c)
{
    int32_t cr = c.channel[0] - 128, cb = c.channel[2] - 128;
    int32_t black = (pe->fog_color >> 8) & 255;
    int32_t sy    = ((int32_t) (pe->yuv_contrast & 255) + 256) * (c.channel[1] - black) + 128;
    /* Euclidean and truncating division agree after clamping negative channels. */
    return (color) {
        { clamp((sy + 407 * cr) / 255, 255),
         clamp((sy - 207 * cr - 100 * cb) / 255, 255),
         clamp((sy + 515 * cb) / 255, 255), c.channel[3] }
    };
}

void
pixel_spr(pixel_engine *pe, uint32_t index, uint32_t value, verite_memory mem)
{
    static const uint8_t source_bits[] = { 7, 9, 10, 11, 12, 13 };
    switch (index) {
        case 0:
            pe->src_base = value;
            break;
        case 1:
            pe->src_mode = value;
            break;
        case 2:
            pe->src_mask = value;
            break;
        case 3:
            pe->stride = value;
            break;
        case 4:
            pe->dst_base = value;
            break;
        case 5:
            pe->dst_mode = value;
            break;
        case 6:
            pe->dst_fmt = value;
            break;
        case 7:
            pe->pmask = value;
            break;
        case 8:
            pe->pattern    = value;
            pe->pat_offset = 0;
            break;
        case 10:
            pe->pattern = 0;
            for (unsigned byte = 0; byte < 4; byte++)
                pe->pattern = (pe->pattern << 8) | mem_read8(mem, value + byte);
            pe->pat_offset = 0;
            break;
        case 12:
            pe->pat_mode = value;
            break;
        case 13:
            pe->pat_offset = value & 31;
            break;
        case 14:
            pe->scissor_x = value;
            break;
        case 15:
            pe->scissor_y = value;
            break;
        case 16:
            pe->z_base = value;
            break;
        case 17:
            pe->dither_offset = value;
            break;
        case 19:
            set_fg(pe, value, true);
            break;
        case 20:
            pe->bg = value;
            break;
        case 21:
            pe->fog_color = value;
            break;
        case 22:
            pe->dst_color = value;
            break;
        case 23:
            pe->chroma_color = value;
            break;
        case 24:
            pe->yuv_contrast = value;
            break;
        case 26:
            pe->chroma_mask = value;
            break;
        case 27:
            set_fg(pe, value, false);
            break;
        case 28:
            pe->pick_valid = !!(value & 0x10000);
            pe->pick_hit   = !!(value & 0x20000);
            break;
        case 29:
            pe->alpha_thres = value;
            break;
        case 30:
            pe->spec_color = value;
            break;
        case 48:
            field_set(&pe->src_mode, 0xf, value);
            break;
        case 49:
            field_set(&pe->src_mode, 0x70, value);
            break;
        case 50:
        case 51:
        case 52:
        case 53:
        case 54:
        case 55:
            field_set(&pe->src_mode, 1U << source_bits[index - 50], value);
            break;
        case 56:
            field_set(&pe->src_mask, 0x7ff, value);
            break;
        case 57:
            field_set(&pe->src_mask, 0x7ff0000, value);
            break;
        case 58:
        case 59:
        case 60:
            field_set(&pe->stride, 0x7fU << (8 * (index - 58)), value);
            break;
        case 61:
        case 62:
        case 63:
            field_set(&pe->src_mode, 1U << (index - 61 + 14), value);
            break;
        case 64:
            field_set(&pe->dst_mode, 0xf, value);
            break;
        case 65:
            field_set(&pe->dst_mode, 0xf0, value);
            break;
        case 66:
            field_set(&pe->dst_mode, 0xf00, value);
            break;
        case 67:
            field_set(&pe->dst_mode, 0x7000, value);
            break;
        case 68:
            field_set(&pe->dst_mode, DST_ZBUF_WRITE, value);
            break;
        case 69:
        case 70:
        case 71:
        case 72:
        case 73:
        case 74:
        case 75:
        case 76:
            field_set(&pe->dst_mode, 1U << (index - 69 + 18), value);
            break;
        case 80:
            field_set(&pe->pat_mode, 0x1f, value);
            break;
        case 81:
            field_set(&pe->pat_mode, 1U << 16, value);
            break;
        case 82:
            field_set(&pe->pat_mode, 1U << 17, value);
            break;
        case 83:
            field_set(&pe->src_mode, 1U << 17, value);
            break;
        default:
            if (index >= 32 && index <= 39)
                pe->tex4pal[index - 32] = value;
            break;
    }
}

static uint32_t
pixel_mask(unsigned bytes)
{
    return bytes == 4 ? UINT32_MAX : (1U << (8 * bytes)) - 1;
}

static void
pixel_parallel(verite_core *c, int32_t x, int32_t y, int32_t sx, int32_t sy, verite_memory mem)
{
    pixel_engine *pe = &c->pe;
    bool          background;
    if (!pattern_pixel(pe, &background) || !inside(pe, x, y))
        return;
    unsigned bytes = dst_bytes(pe);
    uint32_t addr  = dst_at(pe, x, y);
    uint32_t mask  = pixel_mask(bytes);
    uint32_t source;
    if (!((pe->src_mode >> 4) & 7) || background) {
        /* A colour word packs one pixel for each position within the word. */
        uint32_t word  = background ? pe->bg : pe->fg;
        unsigned shift = 8 * (4 - bytes - (addr & 3 & ~(bytes - 1)));
        source         = (word >> shift) & mask;
    } else {
        unsigned n = fmt_bytes(src_fmt(pe));
        if (!n)
            n = 1;
        uint32_t row = pe->src_base + line_stride(pe->stride) * ((uint32_t) sy & pe->v_mask);
        source       = read_pixel(mem, row + ((uint32_t) sx & pe->u_mask) * n, n);
        if (keyed(pe, source, n))
            return;
    }
    unsigned code   = pe->dst_mode & 15;
    uint32_t pmask  = pe->pmask & mask;
    uint32_t old    = rop_reads(code) || pmask != mask ? read_pixel(mem, addr, bytes) : 0;
    uint32_t result = (rop(code, source, old) & pmask) | (old & ~pmask);
    write_pixel(mem, addr, bytes, result & mask);
    pe->pick_hit = c->drew = true;
}

/* drawp stops at a word boundary. rdrawp continues until Cnt is exhausted. */
void
pixel_draw_parallel(verite_core *c, uint8_t op, uint8_t d, uint8_t s2, uint8_t s1, verite_memory mem)
{
    uint32_t      e2        = risc_src(c, s2 & ~1);
    uint32_t      o2        = risc_src(c, s2 | 1);
    uint32_t      e1        = risc_src(c, s1 & ~1);
    uint32_t      o1        = risc_src(c, s1 | 1);
    pixel_engine *pe        = &c->pe;
    bool          resetting = !!(op & 4), repeated = !!(op & 1);
    if (resetting)
        pe->y = (int32_t) o2;
    else
        pixel_set(pe, P_U, o2);

    unsigned cnt  = pe->cnt & 0xffff;
    int32_t  x0   = pe->x_shadow;
    uint32_t step = pe->x_dir ? 0U - 0x10000 : 0x10000;
    unsigned n    = cnt;
    if (!repeated && !resetting) {
        uint32_t addr  = dst_at(pe, pe->x_shadow >> 16, pe->y >> 16);
        unsigned bytes = dst_bytes(pe);
        unsigned left  = pe->x_dir ? (addr & 3) / bytes + 1 : (4 - (addr & 3)) / bytes;
        if (left < n)
            n = left;
    }
    for (unsigned i = 0; i < n; i++) {
        pixel_parallel(c, pe->x_shadow >> 16, pe->y >> 16, pe->u >> 16, pe->v >> 16, mem);
        pe->x_shadow = (int32_t) ((uint32_t) pe->x_shadow + step);
        pe->u        = (int32_t) ((uint32_t) pe->u + step);
    }
    pe->cnt = resetting ? cnt : cnt - n;
    if (resetting)
        pe->x_shadow = x0;
    if (resetting || (!repeated && cnt)) {
        risc_dst(c, d & ~1, e2 + e1);
        risc_dst(c, d | 1, o2 + o1);
    }
}

static bool
depth_pass(unsigned mode, uint32_t z, verite_memory mem, uint32_t addr)
{
    if (!mode)
        return true;
    if (mode == 7)
        return false;
    uint32_t old = mem_read16(mem, addr);
    switch (mode) {
        case 1:
            return z < old;
        case 2:
            return z == old;
        case 3:
            return z <= old;
        case 4:
            return z > old;
        case 5:
            return z != old;
        default:
            return z >= old;
    }
}

static void
pixel_full(verite_core *core, int32_t x, int32_t y, bool perspective, verite_memory mem)
{
    static const unsigned dither_matrix[4][4] = {
        { 0,  12, 3,  15 },
        { 7,  11, 4,  8  },
        { 13, 1,  14, 2  },
        { 10, 6,  9,  5  }
    };
    pixel_engine *pe = &core->pe;
    bool          background;
    if (!pattern_pixel(pe, &background) || !inside(pe, x, y) || (pe->z_ovf && (pe->src_mode & SRC_Z_SCISSOR)))
        return;
    color    c    = background ? argb(pe->bg) : (color) {
        { sat(pe->r), sat(pe->g), sat(pe->b), sat(pe->a) }
    };
    unsigned func = (pe->src_mode >> 4) & 7;
    if (func) {
        int32_t u = pe->u, v = pe->v;
        if (perspective && pe->q) {
            u = (int32_t) (((int64_t) u * (INT64_C(1) << 24)) / pe->q);
            v = (int32_t) (((int64_t) v * (INT64_C(1) << 24)) / pe->q);
        }
        color t = sample(pe, u, v, mem);
        if (func == 2) {
            for (unsigned k = 0; k < 3; k++)
                c.channel[k] = (c.channel[k] * (255 - t.channel[3]) + t.channel[k] * t.channel[3] + 127) / 255;
        } else if (func == 3) {
            for (unsigned k = 0; k < 4; k++)
                c.channel[k] = (c.channel[k] * t.channel[k] + 127) / 255;
        } else
            c = t;
    }
    if ((pe->dst_mode & DST_TRANSP_REJECT) && c.channel[3] <= (int32_t) (pe->alpha_thres & 255))
        return;
    if (pe->dst_mode & DST_FOG) {
        int32_t f   = sat(pe->f);
        color   fog = argb(pe->fog_color);
        for (unsigned k = 0; k < 3; k++)
            c.channel[k] = f * (c.channel[k] - fog.channel[k]) / 255 + fog.channel[k];
    }
    uint32_t z_addr = pe->z_base + line_stride(pe->stride >> 16) * (uint32_t) y + 2 * (uint32_t) x;
    uint32_t z      = (uint32_t) (pe->z >> 11) & 0xffff;
    if (!depth_pass((pe->dst_mode >> 12) & 7, z, mem, z_addr))
        return;

    unsigned fmt = pe->dst_fmt & 15, bytes = dst_bytes(pe);
    uint32_t addr = dst_at(pe, x, y);
    uint32_t mask = pixel_mask(bytes), pmask = pe->pmask & mask;
    unsigned code          = pe->dst_mode & 15;
    bool     blending      = !!(pe->dst_mode & DST_BLEND);
    bool     read_disabled = !!(pe->dst_mode & DST_READ_DISABLE);
    bool     bgr           = !!(pe->dst_mode & DST_BGR);
    uint32_t old           = (blending || rop_reads(code) || pmask != mask) && !read_disabled ? read_pixel(mem, addr, bytes) : 0;
    if (blending) {
        color d = read_disabled ? argb(pe->dst_color) : decode(fmt, old, bgr, !!(pe->dst_mode & DST_NO_PAD));
        c       = blend(pe, c, d);
    } else if (pe->dst_mode & DST_YUV2RGB)
        c = yuv_to_rgb(pe, c);

    unsigned dither = 0;
    if (pe->dst_mode & DST_DITHER) {
        unsigned dx = ((uint32_t) x + (pe->dither_offset & 3)) & 3;
        unsigned dy = ((uint32_t) y + ((pe->dither_offset >> 4) & 3)) & 3;
        dither      = dither_matrix[dy][dx];
    }
    uint32_t encoded = encode(fmt, c, dither, bgr);
    uint32_t out     = (rop(code, encoded, old) & pmask) | (old & ~pmask);
    write_pixel(mem, addr, bytes, out & mask);
    if (pe->dst_mode & DST_ZBUF_WRITE)
        mem_write16(mem, z_addr, z);
    pe->pick_hit = core->drew = true;
}

/* Parameterized draws take the group of values before the group of deltas. */
void
pixel_draw_param(verite_core *c, uint8_t op, uint8_t d, uint8_t s2, uint8_t s1, verite_memory mem)
{
    pixel_engine *pe  = &c->pe;
    uint32_t      ctl = pe->drawctl;
    unsigned      count;
    switch (op & 0xfe) {
        case 0xf0:
            count = 2;
            break;
        case 0xf2:
            count = 4;
            break;
        case 0xf8:
            count = 6;
            break;
        case 0xfa:
            count = 8;
            break;
        default:
            count = 10;
            break;
    }
    first_pair  p1 = pair1[ctl & 15];
    unsigned    i2 = (ctl >> 4) & 31;
    param_pair  p2 = i2 <= 7 ? pair3[i2] : pair2[i2 - 8];
    param_pair  p3 = pair3[(ctl >> 9) & 15], p4 = pair3[(ctl >> 13) & 7];
    pixel_param params[] = { p1.pair.even, p1.pair.odd, p2.even, p2.odd, p3.even, p3.odd, p4.even, p4.odd, P_G, P_B };
    unsigned    align    = 2;
    while (align < count)
        align *= 2;
    d &= ~(align - 1);
    s2 &= ~(align - 1);
    s1 &= ~(align - 1);
    uint32_t values[10], deltas[10];
    for (unsigned i = 0; i < count; i++)
        values[i] = risc_src(c, s2 + i);
    for (unsigned i = 0; i < count; i++)
        deltas[i] = risc_src(c, s1 + i);
    int cnt = (int16_t) pe->cnt;
    if (cnt <= 0)
        return;

    bool perspective = false, shadow = p1.shadow;
    for (unsigned i = 0; i < count; i++) {
        if (params[i] == P_Q)
            perspective = true;
        if (params[i] == P_X)
            shadow = false;
    }
    uint32_t step     = pe->x_dir ? 0U - 0x10000 : 0x10000;
    bool     repeated = !!(op & 1);
    int      n        = repeated ? cnt : 1;
    for (int i = 0; i < n; i++) {
        uint32_t next_q = pe->q;
        for (unsigned k = 0; k < count; k++) {
            if (params[k] == P_Q)
                next_q = values[k];
            else
                pixel_set(pe, params[k], values[k]);
        }
        int32_t x = (shadow ? pe->x_shadow : pe->x) >> 16;
        pixel_full(c, x, pe->y >> 16, perspective, mem);
        pe->q = next_q;
        if (shadow)
            pe->x_shadow = (int32_t) ((uint32_t) pe->x_shadow + step);
        if (repeated && i + 1 < n)
            for (unsigned k = 0; k < count; k++)
                values[k] += deltas[k];
    }
    pe->cnt = repeated ? 0 : cnt - 1;
    if (!repeated)
        for (unsigned k = 0; k < count; k++)
            risc_dst(c, d + k, values[k] + deltas[k]);
}
