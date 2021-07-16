/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		3DFX Voodoo emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2008-2020 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_dither.h>
#include <86box/vid_voodoo_regs.h>
#include <86box/vid_voodoo_render.h>
#include <86box/vid_voodoo_texture.h>


typedef struct voodoo_state_t
{
        int xstart, xend, xdir;
        uint32_t base_r, base_g, base_b, base_a, base_z;
        struct
        {
                int64_t base_s, base_t, base_w;
                int lod;
        } tmu[2];
        int64_t base_w;
        int lod;
        int lod_min[2], lod_max[2];
        int dx1, dx2;
        int y, yend, ydir;
        int32_t dxAB, dxAC, dxBC;
        int tex_b[2], tex_g[2], tex_r[2], tex_a[2];
        int tex_s, tex_t;
        int clamp_s[2], clamp_t[2];

        int32_t vertexAx, vertexAy, vertexBx, vertexBy, vertexCx, vertexCy;

        uint32_t *tex[2][LOD_MAX+1];
        int tformat;

        int *tex_w_mask[2];
        int *tex_h_mask[2];
        int *tex_shift[2];
        int *tex_lod[2];

        uint16_t *fb_mem, *aux_mem;

        int32_t ib, ig, ir, ia;
        int32_t z;

        int32_t new_depth;

        int64_t tmu0_s, tmu0_t;
        int64_t tmu0_w;
        int64_t tmu1_s, tmu1_t;
        int64_t tmu1_w;
        int64_t w;

        int pixel_count, texel_count;
        int x, x2, x_tiled;

        uint32_t w_depth;

        float log_temp;
        uint32_t ebp_store;
        uint32_t texBaseAddr;

        int lod_frac[2];
} voodoo_state_t;

#ifdef ENABLE_VOODOO_RENDER_LOG
int voodoo_render_do_log = ENABLE_VOODOO_RENDER_LOG;

static void
voodoo_render_log(const char *fmt, ...)
{
    va_list ap;

    if (voodoo_render_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define voodoo_render_log(fmt, ...)
#endif


static uint8_t logtable[256] =
{
        0x00,0x01,0x02,0x04,0x05,0x07,0x08,0x09,0x0b,0x0c,0x0e,0x0f,0x10,0x12,0x13,0x15,
        0x16,0x17,0x19,0x1a,0x1b,0x1d,0x1e,0x1f,0x21,0x22,0x23,0x25,0x26,0x27,0x28,0x2a,
        0x2b,0x2c,0x2e,0x2f,0x30,0x31,0x33,0x34,0x35,0x36,0x38,0x39,0x3a,0x3b,0x3d,0x3e,
        0x3f,0x40,0x41,0x43,0x44,0x45,0x46,0x47,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x50,0x51,
        0x52,0x53,0x54,0x55,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x60,0x61,0x62,0x63,
        0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6c,0x6d,0x6e,0x6f,0x70,0x71,0x72,0x73,0x74,
        0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,0x80,0x81,0x83,0x84,0x85,
        0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8c,0x8d,0x8e,0x8f,0x90,0x91,0x92,0x93,0x94,
        0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,0xa0,0xa1,0xa2,0xa2,0xa3,
        0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xad,0xae,0xaf,0xb0,0xb1,0xb2,
        0xb3,0xb4,0xb5,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbc,0xbd,0xbe,0xbf,0xc0,
        0xc1,0xc2,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xcd,
        0xce,0xcf,0xd0,0xd1,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd6,0xd7,0xd8,0xd9,0xda,0xda,
        0xdb,0xdc,0xdd,0xde,0xde,0xdf,0xe0,0xe1,0xe1,0xe2,0xe3,0xe4,0xe5,0xe5,0xe6,0xe7,
        0xe8,0xe8,0xe9,0xea,0xeb,0xeb,0xec,0xed,0xee,0xef,0xef,0xf0,0xf1,0xf2,0xf2,0xf3,
        0xf4,0xf5,0xf5,0xf6,0xf7,0xf7,0xf8,0xf9,0xfa,0xfa,0xfb,0xfc,0xfd,0xfd,0xfe,0xff
};

static __inline int fastlog(uint64_t val)
{
        uint64_t oldval = val;
        int exp = 63;
        int frac;

        if (!val || val & (1ULL << 63))
                return 0x80000000;

        if (!(val & 0xffffffff00000000))
        {
                exp -= 32;
                val <<= 32;
        }
        if (!(val & 0xffff000000000000))
        {
                exp -= 16;
                val <<= 16;
        }
        if (!(val & 0xff00000000000000))
        {
                exp -= 8;
                val <<= 8;
        }
        if (!(val & 0xf000000000000000))
        {
                exp -= 4;
                val <<= 4;
        }
        if (!(val & 0xc000000000000000))
        {
                exp -= 2;
                val <<= 2;
        }
        if (!(val & 0x8000000000000000))
        {
                exp -= 1;
                val <<= 1;
        }

        if (exp >= 8)
                frac = (oldval >> (exp - 8)) & 0xff;
        else
                frac = (oldval << (8 - exp)) & 0xff;

        return (exp << 8) | logtable[frac];
}

static inline int voodoo_fls(uint16_t val)
{
        int num = 0;

//voodoo_render_log("fls(%04x) = ", val);
        if (!(val & 0xff00))
        {
                num += 8;
                val <<= 8;
        }
        if (!(val & 0xf000))
        {
                num += 4;
                val <<= 4;
        }
        if (!(val & 0xc000))
        {
                num += 2;
                val <<= 2;
        }
        if (!(val & 0x8000))
        {
                num += 1;
                val <<= 1;
        }
//voodoo_render_log("%i %04x\n", num, val);
        return num;
}

typedef struct voodoo_texture_state_t
{
        int s, t;
        int w_mask, h_mask;
        int tex_shift;
} voodoo_texture_state_t;

static inline void tex_read(voodoo_state_t *state, voodoo_texture_state_t *texture_state, int tmu)
{
        uint32_t dat;

        if (texture_state->s & ~texture_state->w_mask)
        {
                if (state->clamp_s[tmu])
                {
                        if (texture_state->s < 0)
                                texture_state->s = 0;
                        if (texture_state->s > texture_state->w_mask)
                                texture_state->s = texture_state->w_mask;
                }
                else
                        texture_state->s &= texture_state->w_mask;
        }
        if (texture_state->t & ~texture_state->h_mask)
        {
                if (state->clamp_t[tmu])
                {
                        if (texture_state->t < 0)
                                texture_state->t = 0;
                        if (texture_state->t > texture_state->h_mask)
                                texture_state->t = texture_state->h_mask;
                }
                else
                        texture_state->t &= texture_state->h_mask;
        }

        dat = state->tex[tmu][state->lod][texture_state->s + (texture_state->t << texture_state->tex_shift)];

        state->tex_b[tmu] = dat & 0xff;
        state->tex_g[tmu] = (dat >> 8) & 0xff;
        state->tex_r[tmu] = (dat >> 16) & 0xff;
        state->tex_a[tmu] = (dat >> 24) & 0xff;
}

#define LOW4(x)  ((x & 0x0f) | ((x & 0x0f) << 4))
#define HIGH4(x) ((x & 0xf0) | ((x & 0xf0) >> 4))

static inline void tex_read_4(voodoo_state_t *state, voodoo_texture_state_t *texture_state, int s, int t, int *d, int tmu, int x)
{
        rgba_u dat[4];

        if (((s | (s + 1)) & ~texture_state->w_mask) || ((t | (t + 1)) & ~texture_state->h_mask))
        {
                int c;
                for (c = 0; c < 4; c++)
                {
                        int _s = s + (c & 1);
                        int _t = t + ((c & 2) >> 1);

                        if (_s & ~texture_state->w_mask)
                        {
                                if (state->clamp_s[tmu])
                                {
                                        if (_s < 0)
                                                _s = 0;
                                        if (_s > texture_state->w_mask)
                                                _s = texture_state->w_mask;
                                }
                                else
                                        _s &= texture_state->w_mask;
                        }
                        if (_t & ~texture_state->h_mask)
                        {
                                if (state->clamp_t[tmu])
                                {
                                        if (_t < 0)
                                                _t = 0;
                                        if (_t > texture_state->h_mask)
                                                _t = texture_state->h_mask;
                                }
                                else
                                        _t &= texture_state->h_mask;
                        }
                        dat[c].u = state->tex[tmu][state->lod][_s + (_t << texture_state->tex_shift)];
                }
        }
        else
        {
                dat[0].u = state->tex[tmu][state->lod][s +     (t << texture_state->tex_shift)];
                dat[1].u = state->tex[tmu][state->lod][s + 1 + (t << texture_state->tex_shift)];
                dat[2].u = state->tex[tmu][state->lod][s +     ((t + 1) << texture_state->tex_shift)];
                dat[3].u = state->tex[tmu][state->lod][s + 1 + ((t + 1) << texture_state->tex_shift)];
        }

        state->tex_r[tmu] = (dat[0].rgba.r * d[0] + dat[1].rgba.r * d[1] + dat[2].rgba.r * d[2] + dat[3].rgba.r * d[3]) >> 8;
        state->tex_g[tmu] = (dat[0].rgba.g * d[0] + dat[1].rgba.g * d[1] + dat[2].rgba.g * d[2] + dat[3].rgba.g * d[3]) >> 8;
        state->tex_b[tmu] = (dat[0].rgba.b * d[0] + dat[1].rgba.b * d[1] + dat[2].rgba.b * d[2] + dat[3].rgba.b * d[3]) >> 8;
        state->tex_a[tmu] = (dat[0].rgba.a * d[0] + dat[1].rgba.a * d[1] + dat[2].rgba.a * d[2] + dat[3].rgba.a * d[3]) >> 8;
}

static inline void voodoo_get_texture(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int tmu, int x)
{
        voodoo_texture_state_t texture_state;
        int d[4];
        int s, t;
        int tex_lod = state->tex_lod[tmu][state->lod];

        texture_state.w_mask = state->tex_w_mask[tmu][state->lod];
        texture_state.h_mask = state->tex_h_mask[tmu][state->lod];
        texture_state.tex_shift = 8 - tex_lod;

        if (params->tLOD[tmu] & LOD_TMIRROR_S)
        {
                if (state->tex_s & 0x1000)
                        state->tex_s = ~state->tex_s;
        }
        if (params->tLOD[tmu] & LOD_TMIRROR_T)
        {
                if (state->tex_t & 0x1000)
                        state->tex_t = ~state->tex_t;
        }

        if (voodoo->bilinear_enabled && params->textureMode[tmu] & 6)
        {
                int _ds, dt;

                state->tex_s -= 1 << (3+tex_lod);
                state->tex_t -= 1 << (3+tex_lod);

                s = state->tex_s >> tex_lod;
                t = state->tex_t >> tex_lod;

                _ds = s & 0xf;
                dt = t & 0xf;

                s >>= 4;
                t >>= 4;
//if (x == 80)
//if (voodoo_output)
//        voodoo_render_log("s=%08x t=%08x _ds=%02x _dt=%02x\n", s, t, _ds, dt);
                d[0] = (16 - _ds) * (16 - dt);
                d[1] =  _ds * (16 - dt);
                d[2] = (16 - _ds) * dt;
                d[3] = _ds * dt;

//                texture_state.s = s;
//                texture_state.t = t;
                tex_read_4(state, &texture_state, s, t, d, tmu, x);


/*                state->tex_r = (tex_samples[0].rgba.r * d[0] + tex_samples[1].rgba.r * d[1] + tex_samples[2].rgba.r * d[2] + tex_samples[3].rgba.r * d[3]) >> 8;
                state->tex_g = (tex_samples[0].rgba.g * d[0] + tex_samples[1].rgba.g * d[1] + tex_samples[2].rgba.g * d[2] + tex_samples[3].rgba.g * d[3]) >> 8;
                state->tex_b = (tex_samples[0].rgba.b * d[0] + tex_samples[1].rgba.b * d[1] + tex_samples[2].rgba.b * d[2] + tex_samples[3].rgba.b * d[3]) >> 8;
                state->tex_a = (tex_samples[0].rgba.a * d[0] + tex_samples[1].rgba.a * d[1] + tex_samples[2].rgba.a * d[2] + tex_samples[3].rgba.a * d[3]) >> 8;*/
/*                state->tex_r = tex_samples[0].r;
                state->tex_g = tex_samples[0].g;
                state->tex_b = tex_samples[0].b;
                state->tex_a = tex_samples[0].a;*/
        }
        else
        {
        //        rgba_t tex_samples;
        //        voodoo_texture_state_t texture_state;
//                int s = state->tex_s >> (18+state->lod);
//                int t = state->tex_t >> (18+state->lod);
        //        int s, t;

//                state->tex_s -= 1 << (17+state->lod);
//                state->tex_t -= 1 << (17+state->lod);

                s = state->tex_s >> (4+tex_lod);
                t = state->tex_t >> (4+tex_lod);

                texture_state.s = s;
                texture_state.t = t;
                tex_read(state, &texture_state, tmu);

/*                state->tex_r = tex_samples[0].rgba.r;
                state->tex_g = tex_samples[0].rgba.g;
                state->tex_b = tex_samples[0].rgba.b;
                state->tex_a = tex_samples[0].rgba.a;*/
        }
}

static inline void voodoo_tmu_fetch(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int tmu, int x)
{
        if (params->textureMode[tmu] & 1)
        {
                int64_t _w = 0;

                if (tmu)
                {
                        if (state->tmu1_w)
                                _w = (int64_t)((1ULL << 48) / state->tmu1_w);
                        state->tex_s = (int32_t)(((((state->tmu1_s + (1 << 13)) >> 14) * _w) + (1 << 29))  >> 30);
                        state->tex_t = (int32_t)(((((state->tmu1_t + (1 << 13))  >> 14)  * _w) + (1 << 29))  >> 30);
                }
                else
                {
                        if (state->tmu0_w)
                                _w = (int64_t)((1ULL << 48) / state->tmu0_w);
                        state->tex_s = (int32_t)(((((state->tmu0_s + (1 << 13))  >> 14) * _w) + (1 << 29)) >> 30);
                        state->tex_t = (int32_t)(((((state->tmu0_t + (1 << 13))  >> 14)  * _w) + (1 << 29))  >> 30);
                }

                state->lod = state->tmu[tmu].lod + (fastlog(_w) - (19 << 8));
        }
        else
        {
                if (tmu)
                {
                        state->tex_s = (int32_t)(state->tmu1_s >> (14+14));
                        state->tex_t = (int32_t)(state->tmu1_t >> (14+14));
                }
                else
                {
                        state->tex_s = (int32_t)(state->tmu0_s >> (14+14));
                        state->tex_t = (int32_t)(state->tmu0_t >> (14+14));
                }
                state->lod = state->tmu[tmu].lod;
        }

        if (state->lod < state->lod_min[tmu])
                state->lod = state->lod_min[tmu];
        else if (state->lod > state->lod_max[tmu])
                state->lod = state->lod_max[tmu];
        state->lod_frac[tmu] = state->lod & 0xff;
        state->lod >>= 8;

        voodoo_get_texture(voodoo, params, state, tmu, x);
}


/*Perform texture fetch and blending for both TMUs*/
static inline void voodoo_tmu_fetch_and_blend(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int x)
{
        int r,g,b,a;
        int c_reverse, a_reverse;
//        int c_reverse1, a_reverse1;
        int factor_r = 0, factor_g = 0, factor_b = 0, factor_a = 0;

        voodoo_tmu_fetch(voodoo, params, state, 1, x);

        if ((params->textureMode[1] & TEXTUREMODE_TRILINEAR) && (state->lod & 1))
        {
                c_reverse = tc_reverse_blend;
                a_reverse = tca_reverse_blend;
        }
        else
        {
                c_reverse = !tc_reverse_blend;
                a_reverse = !tca_reverse_blend;
        }
/*        c_reverse1 = c_reverse;
        a_reverse1 = a_reverse;*/
        if (tc_sub_clocal_1)
        {
                switch (tc_mselect_1)
                {
                        case TC_MSELECT_ZERO:
                        factor_r = factor_g = factor_b = 0;
                        break;
                        case TC_MSELECT_CLOCAL:
                        factor_r = state->tex_r[1];
                        factor_g = state->tex_g[1];
                        factor_b = state->tex_b[1];
                        break;
                        case TC_MSELECT_AOTHER:
                        factor_r = factor_g = factor_b = 0;
                        break;
                        case TC_MSELECT_ALOCAL:
                        factor_r = factor_g = factor_b = state->tex_a[1];
                        break;
                        case TC_MSELECT_DETAIL:
                        factor_r = (params->detail_bias[1] - state->lod) << params->detail_scale[1];
                        if (factor_r > params->detail_max[1])
                                factor_r = params->detail_max[1];
                        factor_g = factor_b = factor_r;
                        break;
                        case TC_MSELECT_LOD_FRAC:
                        factor_r = factor_g = factor_b = state->lod_frac[1];
                        break;
                }
                if (!c_reverse)
                {
                        r = (-state->tex_r[1] * (factor_r + 1)) >> 8;
                        g = (-state->tex_g[1] * (factor_g + 1)) >> 8;
                        b = (-state->tex_b[1] * (factor_b + 1)) >> 8;
                }
                else
                {
                        r = (-state->tex_r[1] * ((factor_r^0xff) + 1)) >> 8;
                        g = (-state->tex_g[1] * ((factor_g^0xff) + 1)) >> 8;
                        b = (-state->tex_b[1] * ((factor_b^0xff) + 1)) >> 8;
                }
                if (tc_add_clocal_1)
                {
                        r += state->tex_r[1];
                        g += state->tex_g[1];
                        b += state->tex_b[1];
                }
                else if (tc_add_alocal_1)
                {
                        r += state->tex_a[1];
                        g += state->tex_a[1];
                        b += state->tex_a[1];
                }
                state->tex_r[1] = CLAMP(r);
                state->tex_g[1] = CLAMP(g);
                state->tex_b[1] = CLAMP(b);
        }
        if (tca_sub_clocal_1)
        {
                switch (tca_mselect_1)
                {
                        case TCA_MSELECT_ZERO:
                        factor_a = 0;
                        break;
                        case TCA_MSELECT_CLOCAL:
                        factor_a = state->tex_a[1];
                        break;
                        case TCA_MSELECT_AOTHER:
                        factor_a = 0;
                        break;
                        case TCA_MSELECT_ALOCAL:
                        factor_a = state->tex_a[1];
                        break;
                        case TCA_MSELECT_DETAIL:
                        factor_a = (params->detail_bias[1] - state->lod) << params->detail_scale[1];
                        if (factor_a > params->detail_max[1])
                                factor_a = params->detail_max[1];
                        break;
                        case TCA_MSELECT_LOD_FRAC:
                        factor_a = state->lod_frac[1];
                        break;
                }
                if (!a_reverse)
                        a = (-state->tex_a[1] * ((factor_a ^ 0xff) + 1)) >> 8;
                else
                        a = (-state->tex_a[1] * (factor_a + 1)) >> 8;
                if (tca_add_clocal_1 || tca_add_alocal_1)
                        a += state->tex_a[1];
                state->tex_a[1] = CLAMP(a);
        }


        voodoo_tmu_fetch(voodoo, params, state, 0, x);

        if ((params->textureMode[0] & TEXTUREMODE_TRILINEAR) && (state->lod & 1))
        {
                c_reverse = tc_reverse_blend;
                a_reverse = tca_reverse_blend;
        }
        else
        {
                c_reverse = !tc_reverse_blend;
                a_reverse = !tca_reverse_blend;
        }

        if (!tc_zero_other)
        {
                r = state->tex_r[1];
                g = state->tex_g[1];
                b = state->tex_b[1];
        }
        else
                r = g = b = 0;
        if (tc_sub_clocal)
        {
                r -= state->tex_r[0];
                g -= state->tex_g[0];
                b -= state->tex_b[0];
        }
        switch (tc_mselect)
        {
                case TC_MSELECT_ZERO:
                factor_r = factor_g = factor_b = 0;
                break;
                case TC_MSELECT_CLOCAL:
                factor_r = state->tex_r[0];
                factor_g = state->tex_g[0];
                factor_b = state->tex_b[0];
                break;
                case TC_MSELECT_AOTHER:
                factor_r = factor_g = factor_b = state->tex_a[1];
                break;
                case TC_MSELECT_ALOCAL:
                factor_r = factor_g = factor_b = state->tex_a[0];
                break;
                case TC_MSELECT_DETAIL:
                factor_r = (params->detail_bias[0] - state->lod) << params->detail_scale[0];
                if (factor_r > params->detail_max[0])
                        factor_r = params->detail_max[0];
                factor_g = factor_b = factor_r;
                break;
                case TC_MSELECT_LOD_FRAC:
                factor_r = factor_g = factor_b = state->lod_frac[0];
                break;
        }
        if (!c_reverse)
        {
                r = (r * (factor_r + 1)) >> 8;
                g = (g * (factor_g + 1)) >> 8;
                b = (b * (factor_b + 1)) >> 8;
        }
        else
        {
                r = (r * ((factor_r^0xff) + 1)) >> 8;
                g = (g * ((factor_g^0xff) + 1)) >> 8;
                b = (b * ((factor_b^0xff) + 1)) >> 8;
        }
        if (tc_add_clocal)
        {
                r += state->tex_r[0];
                g += state->tex_g[0];
                b += state->tex_b[0];
        }
        else if (tc_add_alocal)
        {
                r += state->tex_a[0];
                g += state->tex_a[0];
                b += state->tex_a[0];
        }

        if (!tca_zero_other)
                a = state->tex_a[1];
        else
                a = 0;
        if (tca_sub_clocal)
                a -= state->tex_a[0];
        switch (tca_mselect)
        {
                case TCA_MSELECT_ZERO:
                factor_a = 0;
                break;
                case TCA_MSELECT_CLOCAL:
                factor_a = state->tex_a[0];
                break;
                case TCA_MSELECT_AOTHER:
                factor_a = state->tex_a[1];
                break;
                case TCA_MSELECT_ALOCAL:
                factor_a = state->tex_a[0];
                break;
                case TCA_MSELECT_DETAIL:
                factor_a = (params->detail_bias[0] - state->lod) << params->detail_scale[0];
                if (factor_a > params->detail_max[0])
                        factor_a = params->detail_max[0];
                break;
                case TCA_MSELECT_LOD_FRAC:
                factor_a = state->lod_frac[0];
                break;
        }
        if (a_reverse)
                a = (a * ((factor_a ^ 0xff) + 1)) >> 8;
        else
                a = (a * (factor_a + 1)) >> 8;
        if (tca_add_clocal || tca_add_alocal)
                a += state->tex_a[0];


        state->tex_r[0] = CLAMP(r);
        state->tex_g[0] = CLAMP(g);
        state->tex_b[0] = CLAMP(b);
        state->tex_a[0] = CLAMP(a);

        if (tc_invert_output)
        {
                state->tex_r[0] ^= 0xff;
                state->tex_g[0] ^= 0xff;
                state->tex_b[0] ^= 0xff;
        }
        if (tca_invert_output)
                state->tex_a[0] ^= 0xff;
}

#if (defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86) && !(defined __amd64__ || defined _M_X64)
#include <86box/vid_voodoo_codegen_x86.h>
#elif (defined __amd64__ || defined _M_X64)
#include <86box/vid_voodoo_codegen_x86-64.h>
#else
int voodoo_recomp = 0;
#endif

static void voodoo_half_triangle(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int ystart, int yend, int odd_even)
{
/*        int rgb_sel                 = params->fbzColorPath & 3;
        int a_sel                   = (params->fbzColorPath >> 2) & 3;
        int cc_localselect          = params->fbzColorPath & (1 << 4);
        int cca_localselect         = (params->fbzColorPath >> 5) & 3;
        int cc_localselect_override = params->fbzColorPath & (1 << 7);
        int cc_zero_other           = params->fbzColorPath & (1 << 8);
        int cc_sub_clocal           = params->fbzColorPath & (1 << 9);
        int cc_mselect              = (params->fbzColorPath >> 10) & 7;
        int cc_reverse_blend        = params->fbzColorPath & (1 << 13);
        int cc_add                  = (params->fbzColorPath >> 14) & 3;
        int cc_add_alocal           = params->fbzColorPath & (1 << 15);
        int cc_invert_output        = params->fbzColorPath & (1 << 16);
        int cca_zero_other          = params->fbzColorPath & (1 << 17);
        int cca_sub_clocal          = params->fbzColorPath & (1 << 18);
        int cca_mselect             = (params->fbzColorPath >> 19) & 7;
        int cca_reverse_blend       = params->fbzColorPath & (1 << 22);
        int cca_add                 = (params->fbzColorPath >> 23) & 3;
        int cca_invert_output       = params->fbzColorPath & (1 << 25);
        int src_afunc = (params->alphaMode >> 8) & 0xf;
        int dest_afunc = (params->alphaMode >> 12) & 0xf;
        int alpha_func = (params->alphaMode >> 1) & 7;
        int a_ref = params->alphaMode >> 24;
        int depth_op = (params->fbzMode >> 5) & 7;
        int dither = params->fbzMode & FBZ_DITHER;*/
        int texels;
        int c;
#ifndef NO_CODEGEN
        uint8_t (*voodoo_draw)(voodoo_state_t *state, voodoo_params_t *params, int x, int real_y);
#endif
        int y_diff = SLI_ENABLED ? 2 : 1;
		int y_origin = (voodoo->type >= VOODOO_BANSHEE) ? voodoo->y_origin_swap : (voodoo->v_disp-1);

        if ((params->textureMode[0] & TEXTUREMODE_MASK) == TEXTUREMODE_PASSTHROUGH ||
            (params->textureMode[0] & TEXTUREMODE_LOCAL_MASK) == TEXTUREMODE_LOCAL)
                texels = 1;
        else
                texels = 2;

        state->clamp_s[0] = params->textureMode[0] & TEXTUREMODE_TCLAMPS;
        state->clamp_t[0] = params->textureMode[0] & TEXTUREMODE_TCLAMPT;
        state->clamp_s[1] = params->textureMode[1] & TEXTUREMODE_TCLAMPS;
        state->clamp_t[1] = params->textureMode[1] & TEXTUREMODE_TCLAMPT;
//        int last_x;
//        voodoo_render_log("voodoo_triangle : bottom-half %X %X %X %X %X %i  %i %i %i\n", xstart, xend, dx1, dx2, dx2 * 36, xdir,  y, yend, ydir);

        for (c = 0; c <= LOD_MAX; c++)
        {
                state->tex[0][c] = &voodoo->texture_cache[0][params->tex_entry[0]].data[texture_offset[c]];
                state->tex[1][c] = &voodoo->texture_cache[1][params->tex_entry[1]].data[texture_offset[c]];
        }

        state->tformat = params->tformat[0];

        state->tex_w_mask[0] = params->tex_w_mask[0];
        state->tex_h_mask[0] = params->tex_h_mask[0];
        state->tex_shift[0] = params->tex_shift[0];
        state->tex_lod[0] = params->tex_lod[0];
        state->tex_w_mask[1] = params->tex_w_mask[1];
        state->tex_h_mask[1] = params->tex_h_mask[1];
        state->tex_shift[1] = params->tex_shift[1];
        state->tex_lod[1] = params->tex_lod[1];

        if ((params->fbzMode & 1) && (ystart < params->clipLowY))
        {
                int dy = params->clipLowY - ystart;

                state->base_r += params->dRdY*dy;
                state->base_g += params->dGdY*dy;
                state->base_b += params->dBdY*dy;
                state->base_a += params->dAdY*dy;
                state->base_z += params->dZdY*dy;
                state->tmu[0].base_s += params->tmu[0].dSdY*dy;
                state->tmu[0].base_t += params->tmu[0].dTdY*dy;
                state->tmu[0].base_w += params->tmu[0].dWdY*dy;
                state->tmu[1].base_s += params->tmu[1].dSdY*dy;
                state->tmu[1].base_t += params->tmu[1].dTdY*dy;
                state->tmu[1].base_w += params->tmu[1].dWdY*dy;
                state->base_w += params->dWdY*dy;
                state->xstart += state->dx1*dy;
                state->xend   += state->dx2*dy;

                ystart = params->clipLowY;
        }

        if ((params->fbzMode & 1) && (yend >= params->clipHighY))
                yend = params->clipHighY;

        state->y = ystart;
//        yend--;

        if (SLI_ENABLED)
        {
                int test_y;

                if (params->fbzMode & (1 << 17))
                        test_y = y_origin - state->y;
                else
                        test_y = state->y;

                if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (test_y & 1)) ||
                    ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(test_y & 1)))
                {
                        state->y++;

                        state->base_r += params->dRdY;
                        state->base_g += params->dGdY;
                        state->base_b += params->dBdY;
                        state->base_a += params->dAdY;
                        state->base_z += params->dZdY;
                        state->tmu[0].base_s += params->tmu[0].dSdY;
                        state->tmu[0].base_t += params->tmu[0].dTdY;
                        state->tmu[0].base_w += params->tmu[0].dWdY;
                        state->tmu[1].base_s += params->tmu[1].dSdY;
                        state->tmu[1].base_t += params->tmu[1].dTdY;
                        state->tmu[1].base_w += params->tmu[1].dWdY;
                        state->base_w += params->dWdY;
                        state->xstart += state->dx1;
                        state->xend += state->dx2;
                }
        }
#ifndef NO_CODEGEN
        if (voodoo->use_recompiler)
                voodoo_draw = voodoo_get_block(voodoo, params, state, odd_even);
        else
                voodoo_draw = NULL;
#endif

        voodoo_render_log("dxAB=%08x dxBC=%08x dxAC=%08x\n", state->dxAB, state->dxBC, state->dxAC);
//        voodoo_render_log("Start %i %i\n", ystart, voodoo->fbzMode & (1 << 17));

        for (; state->y < yend; state->y += y_diff)
        {
                int x, x2;
                int real_y = (state->y << 4) + 8;
                int start_x;
                int dx;
                uint16_t *fb_mem, *aux_mem;

                state->ir = state->base_r;
                state->ig = state->base_g;
                state->ib = state->base_b;
                state->ia = state->base_a;
                state->z = state->base_z;
                state->tmu0_s = state->tmu[0].base_s;
                state->tmu0_t = state->tmu[0].base_t;
                state->tmu0_w = state->tmu[0].base_w;
                state->tmu1_s = state->tmu[1].base_s;
                state->tmu1_t = state->tmu[1].base_t;
                state->tmu1_w = state->tmu[1].base_w;
                state->w = state->base_w;

                x = (state->vertexAx << 12) + ((state->dxAC * (real_y - state->vertexAy)) >> 4);

                if (real_y < state->vertexBy)
                        x2 = (state->vertexAx << 12) + ((state->dxAB * (real_y - state->vertexAy)) >> 4);
                else
                        x2 = (state->vertexBx << 12) + ((state->dxBC * (real_y - state->vertexBy)) >> 4);

                if (params->fbzMode & (1 << 17))
                        real_y = y_origin - (real_y >> 4);
                else
                        real_y >>= 4;

                if (SLI_ENABLED)
                {
                        if (((real_y >> 1) & voodoo->odd_even_mask) != odd_even)
                                goto next_line;
                }
                else
                {
                        if ((real_y & voodoo->odd_even_mask) != odd_even)
                                goto next_line;
                }

                start_x = x;

                if (state->xdir > 0)
                        x2 -= (1 << 16);
                else
                        x -= (1 << 16);
                dx = ((x + 0x7000) >> 16) - (((state->vertexAx << 12) + 0x7000) >> 16);
                x = (x + 0x7000) >> 16;
                x2 = (x2 + 0x7000) >> 16;

                voodoo_render_log("%03i:%03i : Ax=%08x start_x=%08x  dSdX=%016llx dx=%08x  s=%08x -> ", x, state->y, state->vertexAx << 8, start_x, params->tmu[0].dTdX, dx, state->tmu0_t);

                state->ir += (params->dRdX * dx);
                state->ig += (params->dGdX * dx);
                state->ib += (params->dBdX * dx);
                state->ia += (params->dAdX * dx);
                state->z += (params->dZdX * dx);
                state->tmu0_s += (params->tmu[0].dSdX * dx);
                state->tmu0_t += (params->tmu[0].dTdX * dx);
                state->tmu0_w += (params->tmu[0].dWdX * dx);
                state->tmu1_s += (params->tmu[1].dSdX * dx);
                state->tmu1_t += (params->tmu[1].dTdX * dx);
                state->tmu1_w += (params->tmu[1].dWdX * dx);
                state->w += (params->dWdX * dx);

                voodoo_render_log("%08llx %lli %lli\n", state->tmu0_t, state->tmu0_t >> (18+state->lod), (state->tmu0_t + (1 << (17+state->lod))) >> (18+state->lod));

                if (params->fbzMode & 1)
                {
                        if (state->xdir > 0)
                        {
                                if (x < params->clipLeft)
                                {
                                        int dx = params->clipLeft - x;

                                        state->ir += params->dRdX*dx;
                                        state->ig += params->dGdX*dx;
                                        state->ib += params->dBdX*dx;
                                        state->ia += params->dAdX*dx;
                                        state->z += params->dZdX*dx;
                                        state->tmu0_s += params->tmu[0].dSdX*dx;
                                        state->tmu0_t += params->tmu[0].dTdX*dx;
                                        state->tmu0_w += params->tmu[0].dWdX*dx;
                                        state->tmu1_s += params->tmu[1].dSdX*dx;
                                        state->tmu1_t += params->tmu[1].dTdX*dx;
                                        state->tmu1_w += params->tmu[1].dWdX*dx;
                                        state->w += params->dWdX*dx;

                                        x = params->clipLeft;
                                }
                                if (x2 >= params->clipRight)
                                        x2 = params->clipRight-1;
                        }
                        else
                        {
                                if (x >= params->clipRight)
                                {
                                        int dx = (params->clipRight-1) - x;

                                        state->ir += params->dRdX*dx;
                                        state->ig += params->dGdX*dx;
                                        state->ib += params->dBdX*dx;
                                        state->ia += params->dAdX*dx;
                                        state->z += params->dZdX*dx;
                                        state->tmu0_s += params->tmu[0].dSdX*dx;
                                        state->tmu0_t += params->tmu[0].dTdX*dx;
                                        state->tmu0_w += params->tmu[0].dWdX*dx;
                                        state->tmu1_s += params->tmu[1].dSdX*dx;
                                        state->tmu1_t += params->tmu[1].dTdX*dx;
                                        state->tmu1_w += params->tmu[1].dWdX*dx;
                                        state->w += params->dWdX*dx;

                                        x = params->clipRight-1;
                                }
                                if (x2 < params->clipLeft)
                                        x2 = params->clipLeft;
                        }
                }

                if (x2 < x && state->xdir > 0)
                        goto next_line;
                if (x2 > x && state->xdir < 0)
                        goto next_line;

                if (SLI_ENABLED)
                {
                        state->fb_mem = fb_mem = (uint16_t *)&voodoo->fb_mem[params->draw_offset + ((real_y >> 1) * params->row_width)];
                        state->aux_mem = aux_mem = (uint16_t *)&voodoo->fb_mem[(params->aux_offset + ((real_y >> 1) * params->row_width)) & voodoo->fb_mask];
                }
                else
                {
                        if (params->col_tiled)
                                state->fb_mem = fb_mem = (uint16_t *)&voodoo->fb_mem[params->draw_offset + (real_y >> 5) * params->row_width + (real_y & 31) * 128];
                        else
                                state->fb_mem = fb_mem = (uint16_t *)&voodoo->fb_mem[params->draw_offset + (real_y * params->row_width)];
                        if (params->aux_tiled)
                                state->aux_mem = aux_mem = (uint16_t *)&voodoo->fb_mem[(params->aux_offset + (real_y >> 5) * params->aux_row_width + (real_y & 31) * 128) & voodoo->fb_mask];
                        else
                                state->aux_mem = aux_mem = (uint16_t *)&voodoo->fb_mem[(params->aux_offset + (real_y * params->row_width)) & voodoo->fb_mask];
                }

                voodoo_render_log("%03i: x=%08x x2=%08x xstart=%08x xend=%08x dx=%08x\n", state->y, x, x2, state->xstart, state->xend, dx);

                state->pixel_count = 0;
                state->texel_count = 0;
                state->x = x;
                state->x2 = x2;
#ifndef NO_CODEGEN
                if (voodoo->use_recompiler)
                {
                        voodoo_draw(state, params, x, real_y);
                }
                else
#endif
                do
                {
                        int x_tiled = (x & 63) | ((x >> 6) * 128*32/2);
                        start_x = x;
                        state->x = x;
                        voodoo->pixel_count[odd_even]++;
                        voodoo->texel_count[odd_even] += texels;
                        voodoo->fbiPixelsIn++;

                        voodoo_render_log("  X=%03i T=%08x\n", x, state->tmu0_t);
//                        if (voodoo->fbzMode & FBZ_RGB_WMASK)
                        {
                                int update = 1;
                                uint8_t cother_r = 0, cother_g = 0, cother_b = 0, aother;
                                uint8_t clocal_r, clocal_g, clocal_b, alocal;
                                int src_r = 0, src_g = 0, src_b = 0, src_a = 0;
                                int msel_r, msel_g, msel_b, msel_a;
                                uint8_t dest_r, dest_g, dest_b, dest_a;
                                uint16_t dat;
                                int sel;
                                int32_t new_depth, w_depth;

                                if (state->w & 0xffff00000000)
                                        w_depth = 0;
                                else if (!(state->w & 0xffff0000))
                                        w_depth = 0xf001;
                                else
                                {
                                        int exp = voodoo_fls((uint16_t)((uint32_t)state->w >> 16));
                                        int mant = ((~(uint32_t)state->w >> (19 - exp))) & 0xfff;
                                        w_depth = (exp << 12) + mant + 1;
                                        if (w_depth > 0xffff)
                                                w_depth = 0xffff;
                                }

//                                w_depth = CLAMP16(w_depth);

                                if (params->fbzMode & FBZ_W_BUFFER)
                                        new_depth = w_depth;
                                else
                                        new_depth = CLAMP16(state->z >> 12);

                                if (params->fbzMode & FBZ_DEPTH_BIAS)
                                        new_depth = CLAMP16(new_depth + (int16_t)params->zaColor);

                                if (params->fbzMode & FBZ_DEPTH_ENABLE)
                                {
                                        uint16_t old_depth = voodoo->params.aux_tiled ? aux_mem[x_tiled] : aux_mem[x];

                                        DEPTH_TEST((params->fbzMode & FBZ_DEPTH_SOURCE) ? (params->zaColor & 0xffff) : new_depth);
                                }

                                dat = voodoo->params.col_tiled ? fb_mem[x_tiled] : fb_mem[x];
                                dest_r = (dat >> 8) & 0xf8;
                                dest_g = (dat >> 3) & 0xfc;
                                dest_b = (dat << 3) & 0xf8;
                                dest_r |= (dest_r >> 5);
                                dest_g |= (dest_g >> 6);
                                dest_b |= (dest_b >> 5);
                                dest_a = 0xff;

                                if (params->fbzColorPath & FBZCP_TEXTURE_ENABLED)
                                {
                                        if ((params->textureMode[0] & TEXTUREMODE_LOCAL_MASK) == TEXTUREMODE_LOCAL || !voodoo->dual_tmus)
                                        {
                                                /*TMU0 only sampling local colour or only one TMU, only sample TMU0*/
                                                voodoo_tmu_fetch(voodoo, params, state, 0, x);
                                        }
                                        else if ((params->textureMode[0] & TEXTUREMODE_MASK) == TEXTUREMODE_PASSTHROUGH)
                                        {
                                                /*TMU0 in pass-through mode, only sample TMU1*/
                                                voodoo_tmu_fetch(voodoo, params, state, 1, x);

                                                state->tex_r[0] = state->tex_r[1];
                                                state->tex_g[0] = state->tex_g[1];
                                                state->tex_b[0] = state->tex_b[1];
                                                state->tex_a[0] = state->tex_a[1];
                                        }
                                        else
                                        {
                                                voodoo_tmu_fetch_and_blend(voodoo, params, state, x);
                                        }

                                        if ((params->fbzMode & FBZ_CHROMAKEY) &&
                                                state->tex_r[0] == params->chromaKey_r &&
                                                state->tex_g[0] == params->chromaKey_g &&
                                                state->tex_b[0] == params->chromaKey_b)
                                        {
                                                voodoo->fbiChromaFail++;
                                                goto skip_pixel;
                                        }
                                }

                                if (voodoo->trexInit1[0] & (1 << 18))
                                {
                                        state->tex_r[0] = state->tex_g[0] = 0;
                                        state->tex_b[0] = voodoo->tmuConfig;
                                }

                                if (cc_localselect_override)
                                        sel = (state->tex_a[0] & 0x80) ? 1 : 0;
                                else
                                        sel = cc_localselect;

                                if (sel)
                                {
                                        clocal_r = (params->color0 >> 16) & 0xff;
                                        clocal_g = (params->color0 >> 8)  & 0xff;
                                        clocal_b =  params->color0        & 0xff;
                                }
                                else
                                {
                                        clocal_r = CLAMP(state->ir >> 12);
                                        clocal_g = CLAMP(state->ig >> 12);
                                        clocal_b = CLAMP(state->ib >> 12);
                                }

                                switch (_rgb_sel)
                                {
                                        case CC_LOCALSELECT_ITER_RGB: /*Iterated RGB*/
                                        cother_r = CLAMP(state->ir >> 12);
                                        cother_g = CLAMP(state->ig >> 12);
                                        cother_b = CLAMP(state->ib >> 12);
                                        break;

                                        case CC_LOCALSELECT_TEX: /*TREX Color Output*/
                                        cother_r = state->tex_r[0];
                                        cother_g = state->tex_g[0];
                                        cother_b = state->tex_b[0];
                                        break;

                                        case CC_LOCALSELECT_COLOR1: /*Color1 RGB*/
                                        cother_r = (params->color1 >> 16) & 0xff;
                                        cother_g = (params->color1 >> 8)  & 0xff;
                                        cother_b =  params->color1        & 0xff;
                                        break;

                                        case CC_LOCALSELECT_LFB: /*Linear Frame Buffer*/
                                        cother_r = src_r;
                                        cother_g = src_g;
                                        cother_b = src_b;
                                        break;
                                }

                                switch (cca_localselect)
                                {
                                        case CCA_LOCALSELECT_ITER_A:
                                        alocal = CLAMP(state->ia >> 12);
                                        break;

                                        case CCA_LOCALSELECT_COLOR0:
                                        alocal = (params->color0 >> 24) & 0xff;
                                        break;

                                        case CCA_LOCALSELECT_ITER_Z:
                                        alocal = CLAMP(state->z >> 20);
                                        break;

                                        default:
                                        fatal("Bad cca_localselect %i\n", cca_localselect);
                                        alocal = 0xff;
                                        break;
                                }

                                switch (a_sel)
                                {
                                        case A_SEL_ITER_A:
                                        aother = CLAMP(state->ia >> 12);
                                        break;
                                        case A_SEL_TEX:
                                        aother = state->tex_a[0];
                                        break;
                                        case A_SEL_COLOR1:
                                        aother = (params->color1 >> 24) & 0xff;
                                        break;
                                        default:
                                        fatal("Bad a_sel %i\n", a_sel);
                                        aother = 0;
                                        break;
                                }

                                if (cc_zero_other)
                                {
                                        src_r = 0;
                                        src_g = 0;
                                        src_b = 0;
                                }
                                else
                                {
                                        src_r = cother_r;
                                        src_g = cother_g;
                                        src_b = cother_b;
                                }

                                if (cca_zero_other)
                                        src_a = 0;
                                else
                                        src_a = aother;

                                if (cc_sub_clocal)
                                {
                                        src_r -= clocal_r;
                                        src_g -= clocal_g;
                                        src_b -= clocal_b;
                                }

                                if (cca_sub_clocal)
                                        src_a -= alocal;

                                switch (cc_mselect)
                                {
                                        case CC_MSELECT_ZERO:
                                        msel_r = 0;
                                        msel_g = 0;
                                        msel_b = 0;
                                        break;
                                        case CC_MSELECT_CLOCAL:
                                        msel_r = clocal_r;
                                        msel_g = clocal_g;
                                        msel_b = clocal_b;
                                        break;
                                        case CC_MSELECT_AOTHER:
                                        msel_r = aother;
                                        msel_g = aother;
                                        msel_b = aother;
                                        break;
                                        case CC_MSELECT_ALOCAL:
                                        msel_r = alocal;
                                        msel_g = alocal;
                                        msel_b = alocal;
                                        break;
                                        case CC_MSELECT_TEX:
                                        msel_r = state->tex_a[0];
                                        msel_g = state->tex_a[0];
                                        msel_b = state->tex_a[0];
                                        break;
                                        case CC_MSELECT_TEXRGB:
                                        msel_r = state->tex_r[0];
                                        msel_g = state->tex_g[0];
                                        msel_b = state->tex_b[0];
                                        break;

                                        default:
                                                fatal("Bad cc_mselect %i\n", cc_mselect);
                                        msel_r = 0;
                                        msel_g = 0;
                                        msel_b = 0;
                                        break;
                                }

                                switch (cca_mselect)
                                {
                                        case CCA_MSELECT_ZERO:
                                        msel_a = 0;
                                        break;
                                        case CCA_MSELECT_ALOCAL:
                                        msel_a = alocal;
                                        break;
                                        case CCA_MSELECT_AOTHER:
                                        msel_a = aother;
                                        break;
                                        case CCA_MSELECT_ALOCAL2:
                                        msel_a = alocal;
                                        break;
                                        case CCA_MSELECT_TEX:
                                        msel_a = state->tex_a[0];
                                        break;

                                        default:
                                                fatal("Bad cca_mselect %i\n", cca_mselect);
                                        msel_a = 0;
                                        break;
                                }

                                if (!cc_reverse_blend)
                                {
                                        msel_r ^= 0xff;
                                        msel_g ^= 0xff;
                                        msel_b ^= 0xff;
                                }
                                msel_r++;
                                msel_g++;
                                msel_b++;

                                if (!cca_reverse_blend)
                                        msel_a ^= 0xff;
                                msel_a++;

                                src_r = (src_r * msel_r) >> 8;
                                src_g = (src_g * msel_g) >> 8;
                                src_b = (src_b * msel_b) >> 8;
                                src_a = (src_a * msel_a) >> 8;

                                switch (cc_add)
                                {
                                        case CC_ADD_CLOCAL:
                                        src_r += clocal_r;
                                        src_g += clocal_g;
                                        src_b += clocal_b;
                                        break;
                                        case CC_ADD_ALOCAL:
                                        src_r += alocal;
                                        src_g += alocal;
                                        src_b += alocal;
                                        break;
                                        case 0:
                                        break;
                                        default:
                                        fatal("Bad cc_add %i\n", cc_add);
                                }

                                if (cca_add)
                                        src_a += alocal;

                                src_r = CLAMP(src_r);
                                src_g = CLAMP(src_g);
                                src_b = CLAMP(src_b);
                                src_a = CLAMP(src_a);

                                if (cc_invert_output)
                                {
                                        src_r ^= 0xff;
                                        src_g ^= 0xff;
                                        src_b ^= 0xff;
                                }
                                if (cca_invert_output)
                                        src_a ^= 0xff;

                                if (params->fogMode & FOG_ENABLE)
                                        APPLY_FOG(src_r, src_g, src_b, state->z, state->ia, state->w);

                                if (params->alphaMode & 1)
                                        ALPHA_TEST(src_a);

                                if (params->alphaMode & (1 << 4)) {
                                        if (dithersub && !dither2x2 && voodoo->dithersub_enabled)
                                        {
                                       	        dest_r = dithersub_rb[dest_r][real_y & 3][x & 3];
                                       	        dest_g = dithersub_g [dest_g][real_y & 3][x & 3];
                                       	        dest_b = dithersub_rb[dest_b][real_y & 3][x & 3];
                                        }
                                        if (dithersub && dither2x2 && voodoo->dithersub_enabled)
                                        {
                                       	        dest_r = dithersub_rb2x2[dest_r][real_y & 1][x & 1];
                                                dest_g = dithersub_g2x2 [dest_g][real_y & 1][x & 1];
                                                dest_b = dithersub_rb2x2[dest_b][real_y & 1][x & 1];
                                        }
                                        ALPHA_BLEND(src_r, src_g, src_b, src_a);
                                }

                                if (update)
                                {
                                        if (dither)
                                        {
                                                if (dither2x2)
                                                {
                                                        src_r = dither_rb2x2[src_r][real_y & 1][x & 1];
                                                        src_g =  dither_g2x2[src_g][real_y & 1][x & 1];
                                                        src_b = dither_rb2x2[src_b][real_y & 1][x & 1];
                                                }
                                                else
                                                {
                                                        src_r = dither_rb[src_r][real_y & 3][x & 3];
                                                        src_g =  dither_g[src_g][real_y & 3][x & 3];
                                                        src_b = dither_rb[src_b][real_y & 3][x & 3];
                                                }
                                        }
                                        else
                                        {
                                                src_r >>= 3;
                                                src_g >>= 2;
                                                src_b >>= 3;
                                        }

                                        if (params->fbzMode & FBZ_RGB_WMASK)
                                        {
                                                if (voodoo->params.col_tiled)
                                                        fb_mem[x_tiled] = src_b | (src_g << 5) | (src_r << 11);
                                                else
                                                        fb_mem[x] = src_b | (src_g << 5) | (src_r << 11);
                                        }
                                        if ((params->fbzMode & (FBZ_DEPTH_WMASK | FBZ_DEPTH_ENABLE)) == (FBZ_DEPTH_WMASK | FBZ_DEPTH_ENABLE))
                                        {
                                                if (voodoo->params.aux_tiled)
                                                        aux_mem[x_tiled] = new_depth;
                                                else
                                                        aux_mem[x] = new_depth;
                                        }
                                }
                        }
                        voodoo->fbiPixelsOut++;
skip_pixel:
                        if (state->xdir > 0)
                        {
                                state->ir += params->dRdX;
                                state->ig += params->dGdX;
                                state->ib += params->dBdX;
                                state->ia += params->dAdX;
                                state->z += params->dZdX;
                                state->tmu0_s += params->tmu[0].dSdX;
                                state->tmu0_t += params->tmu[0].dTdX;
                                state->tmu0_w += params->tmu[0].dWdX;
                                state->tmu1_s += params->tmu[1].dSdX;
                                state->tmu1_t += params->tmu[1].dTdX;
                                state->tmu1_w += params->tmu[1].dWdX;
                                state->w += params->dWdX;
                        }
                        else
                        {
                                state->ir -= params->dRdX;
                                state->ig -= params->dGdX;
                                state->ib -= params->dBdX;
                                state->ia -= params->dAdX;
                                state->z -= params->dZdX;
                                state->tmu0_s -= params->tmu[0].dSdX;
                                state->tmu0_t -= params->tmu[0].dTdX;
                                state->tmu0_w -= params->tmu[0].dWdX;
                                state->tmu1_s -= params->tmu[1].dSdX;
                                state->tmu1_t -= params->tmu[1].dTdX;
                                state->tmu1_w -= params->tmu[1].dWdX;
                                state->w -= params->dWdX;
                        }

                        x += state->xdir;
                } while (start_x != x2);

                voodoo->pixel_count[odd_even] += state->pixel_count;
                voodoo->texel_count[odd_even] += state->texel_count;
                voodoo->fbiPixelsIn += state->pixel_count;

                if (voodoo->params.draw_offset == voodoo->params.front_offset && (real_y >> 1) < 2048)
                        voodoo->dirty_line[real_y >> 1] = 1;

next_line:
                if (SLI_ENABLED)
                {
                        state->base_r += params->dRdY;
                        state->base_g += params->dGdY;
                        state->base_b += params->dBdY;
                        state->base_a += params->dAdY;
                        state->base_z += params->dZdY;
                        state->tmu[0].base_s += params->tmu[0].dSdY;
                        state->tmu[0].base_t += params->tmu[0].dTdY;
                        state->tmu[0].base_w += params->tmu[0].dWdY;
                        state->tmu[1].base_s += params->tmu[1].dSdY;
                        state->tmu[1].base_t += params->tmu[1].dTdY;
                        state->tmu[1].base_w += params->tmu[1].dWdY;
                        state->base_w += params->dWdY;
                        state->xstart += state->dx1;
                        state->xend += state->dx2;
                }
                state->base_r += params->dRdY;
                state->base_g += params->dGdY;
                state->base_b += params->dBdY;
                state->base_a += params->dAdY;
                state->base_z += params->dZdY;
                state->tmu[0].base_s += params->tmu[0].dSdY;
                state->tmu[0].base_t += params->tmu[0].dTdY;
                state->tmu[0].base_w += params->tmu[0].dWdY;
                state->tmu[1].base_s += params->tmu[1].dSdY;
                state->tmu[1].base_t += params->tmu[1].dTdY;
                state->tmu[1].base_w += params->tmu[1].dWdY;
                state->base_w += params->dWdY;
                state->xstart += state->dx1;
                state->xend += state->dx2;
        }

        voodoo->texture_cache[0][params->tex_entry[0]].refcount_r[odd_even]++;
        voodoo->texture_cache[1][params->tex_entry[1]].refcount_r[odd_even]++;
}

void voodoo_triangle(voodoo_t *voodoo, voodoo_params_t *params, int odd_even)
{
        voodoo_state_t state;
        int vertexAy_adjusted;
        int vertexCy_adjusted;
        int dx, dy;

        uint64_t tempdx, tempdy;
        uint64_t tempLOD;
        int LOD;
        int lodbias;

        voodoo->tri_count++;

        dx = 8 - (params->vertexAx & 0xf);
        if ((params->vertexAx & 0xf) > 8)
                dx += 16;
        dy = 8 - (params->vertexAy & 0xf);
        if ((params->vertexAy & 0xf) > 8)
                dy += 16;

/*        voodoo_render_log("voodoo_triangle %i %i %i : vA %f, %f  vB %f, %f  vC %f, %f f %i,%i %08x %08x %08x,%08x tex=%i,%i fogMode=%08x\n", odd_even, voodoo->params_read_idx[odd_even], voodoo->params_read_idx[odd_even] & PARAM_MASK, (float)params->vertexAx / 16.0, (float)params->vertexAy / 16.0,
                                                                     (float)params->vertexBx / 16.0, (float)params->vertexBy / 16.0,
                                                                     (float)params->vertexCx / 16.0, (float)params->vertexCy / 16.0,
                                                                     (params->fbzColorPath & FBZCP_TEXTURE_ENABLED) ? params->tformat[0] : 0,
                                                                     (params->fbzColorPath & FBZCP_TEXTURE_ENABLED) ? params->tformat[1] : 0, params->fbzColorPath, params->alphaMode, params->textureMode[0],params->textureMode[1], params->tex_entry[0],params->tex_entry[1], params->fogMode);*/

        state.base_r = params->startR;
        state.base_g = params->startG;
        state.base_b = params->startB;
        state.base_a = params->startA;
        state.base_z = params->startZ;
        state.tmu[0].base_s = params->tmu[0].startS;
        state.tmu[0].base_t = params->tmu[0].startT;
        state.tmu[0].base_w = params->tmu[0].startW;
        state.tmu[1].base_s = params->tmu[1].startS;
        state.tmu[1].base_t = params->tmu[1].startT;
        state.tmu[1].base_w = params->tmu[1].startW;
        state.base_w = params->startW;

        if (params->fbzColorPath & FBZ_PARAM_ADJUST)
        {
                state.base_r += (dx*params->dRdX + dy*params->dRdY) >> 4;
                state.base_g += (dx*params->dGdX + dy*params->dGdY) >> 4;
                state.base_b += (dx*params->dBdX + dy*params->dBdY) >> 4;
                state.base_a += (dx*params->dAdX + dy*params->dAdY) >> 4;
                state.base_z += (dx*params->dZdX + dy*params->dZdY) >> 4;
                state.tmu[0].base_s += (dx*params->tmu[0].dSdX + dy*params->tmu[0].dSdY) >> 4;
                state.tmu[0].base_t += (dx*params->tmu[0].dTdX + dy*params->tmu[0].dTdY) >> 4;
                state.tmu[0].base_w += (dx*params->tmu[0].dWdX + dy*params->tmu[0].dWdY) >> 4;
                state.tmu[1].base_s += (dx*params->tmu[1].dSdX + dy*params->tmu[1].dSdY) >> 4;
                state.tmu[1].base_t += (dx*params->tmu[1].dTdX + dy*params->tmu[1].dTdY) >> 4;
                state.tmu[1].base_w += (dx*params->tmu[1].dWdX + dy*params->tmu[1].dWdY) >> 4;
                state.base_w += (dx*params->dWdX + dy*params->dWdY) >> 4;
        }

        tris++;

        state.vertexAy = params->vertexAy & ~0xffff0000;
        if (state.vertexAy & 0x8000)
                state.vertexAy |= 0xffff0000;
        state.vertexBy = params->vertexBy & ~0xffff0000;
        if (state.vertexBy & 0x8000)
                state.vertexBy |= 0xffff0000;
        state.vertexCy = params->vertexCy & ~0xffff0000;
        if (state.vertexCy & 0x8000)
                state.vertexCy |= 0xffff0000;

        state.vertexAx = params->vertexAx & ~0xffff0000;
        if (state.vertexAx & 0x8000)
                state.vertexAx |= 0xffff0000;
        state.vertexBx = params->vertexBx & ~0xffff0000;
        if (state.vertexBx & 0x8000)
                state.vertexBx |= 0xffff0000;
        state.vertexCx = params->vertexCx & ~0xffff0000;
        if (state.vertexCx & 0x8000)
                state.vertexCx |= 0xffff0000;

        vertexAy_adjusted = (state.vertexAy+7) >> 4;
        vertexCy_adjusted = (state.vertexCy+7) >> 4;

        if (state.vertexBy - state.vertexAy)
                state.dxAB = (int)((((int64_t)state.vertexBx << 12) - ((int64_t)state.vertexAx << 12)) << 4) / (int)(state.vertexBy - state.vertexAy);
        else
                state.dxAB = 0;
        if (state.vertexCy - state.vertexAy)
                state.dxAC = (int)((((int64_t)state.vertexCx << 12) - ((int64_t)state.vertexAx << 12)) << 4) / (int)(state.vertexCy - state.vertexAy);
        else
                state.dxAC = 0;
        if (state.vertexCy - state.vertexBy)
                state.dxBC = (int)((((int64_t)state.vertexCx << 12) - ((int64_t)state.vertexBx << 12)) << 4) / (int)(state.vertexCy - state.vertexBy);
        else
                state.dxBC = 0;

        state.lod_min[0] = (params->tLOD[0] & 0x3f) << 6;
        state.lod_max[0] = ((params->tLOD[0] >> 6) & 0x3f) << 6;
        if (state.lod_max[0] > 0x800)
                state.lod_max[0] = 0x800;
        state.lod_min[1] = (params->tLOD[1] & 0x3f) << 6;
        state.lod_max[1] = ((params->tLOD[1] >> 6) & 0x3f) << 6;
        if (state.lod_max[1] > 0x800)
                state.lod_max[1] = 0x800;

        state.xstart = state.xend = state.vertexAx << 8;
        state.xdir = params->sign ? -1 : 1;

        state.y = (state.vertexAy + 8) >> 4;
        state.ydir = 1;


        tempdx = (params->tmu[0].dSdX >> 14) * (params->tmu[0].dSdX >> 14) + (params->tmu[0].dTdX >> 14) * (params->tmu[0].dTdX >> 14);
        tempdy = (params->tmu[0].dSdY >> 14) * (params->tmu[0].dSdY >> 14) + (params->tmu[0].dTdY >> 14) * (params->tmu[0].dTdY >> 14);

        if (tempdx > tempdy)
                tempLOD = tempdx;
        else
                tempLOD = tempdy;

        LOD = (int)(log2((double)tempLOD / (double)(1ULL << 36)) * 256);
        LOD >>= 2;

        lodbias = (params->tLOD[0] >> 12) & 0x3f;
        if (lodbias & 0x20)
                lodbias |= ~0x3f;
        state.tmu[0].lod = LOD + (lodbias << 6);


        tempdx = (params->tmu[1].dSdX >> 14) * (params->tmu[1].dSdX >> 14) + (params->tmu[1].dTdX >> 14) * (params->tmu[1].dTdX >> 14);
        tempdy = (params->tmu[1].dSdY >> 14) * (params->tmu[1].dSdY >> 14) + (params->tmu[1].dTdY >> 14) * (params->tmu[1].dTdY >> 14);

        if (tempdx > tempdy)
                tempLOD = tempdx;
        else
                tempLOD = tempdy;

        LOD = (int)(log2((double)tempLOD / (double)(1ULL << 36)) * 256);
        LOD >>= 2;

        lodbias = (params->tLOD[1] >> 12) & 0x3f;
        if (lodbias & 0x20)
                lodbias |= ~0x3f;
        state.tmu[1].lod = LOD + (lodbias << 6);


        voodoo_half_triangle(voodoo, params, &state, vertexAy_adjusted, vertexCy_adjusted, odd_even);
}


static void render_thread(void *param, int odd_even)
{
        voodoo_t *voodoo = (voodoo_t *)param;

        while (voodoo->render_thread_run[odd_even])
        {
                thread_set_event(voodoo->render_not_full_event[odd_even]);
                thread_wait_event(voodoo->wake_render_thread[odd_even], -1);
                thread_reset_event(voodoo->wake_render_thread[odd_even]);
                voodoo->render_voodoo_busy[odd_even] = 1;

                while (!PARAM_EMPTY(odd_even))
                {
                        uint64_t start_time = plat_timer_read();
                        uint64_t end_time;
                        voodoo_params_t *params = &voodoo->params_buffer[voodoo->params_read_idx[odd_even] & PARAM_MASK];

                        voodoo_triangle(voodoo, params, odd_even);

                        voodoo->params_read_idx[odd_even]++;

                        if (PARAM_ENTRIES(odd_even) > (PARAM_SIZE - 10))
                                thread_set_event(voodoo->render_not_full_event[odd_even]);

                        end_time = plat_timer_read();
                        voodoo->render_time[odd_even] += end_time - start_time;
                }

                voodoo->render_voodoo_busy[odd_even] = 0;
        }
}

void voodoo_render_thread_1(void *param)
{
        render_thread(param, 0);
}
void voodoo_render_thread_2(void *param)
{
        render_thread(param, 1);
}
void voodoo_render_thread_3(void *param)
{
        render_thread(param, 2);
}
void voodoo_render_thread_4(void *param)
{
        render_thread(param, 3);
}

void voodoo_queue_triangle(voodoo_t *voodoo, voodoo_params_t *params)
{
        voodoo_params_t *params_new = &voodoo->params_buffer[voodoo->params_write_idx & PARAM_MASK];

        while (PARAM_FULL(0) || (voodoo->render_threads >= 2 && PARAM_FULL(1)) ||
                (voodoo->render_threads == 4 && (PARAM_FULL(2) || PARAM_FULL(3))))
        {
                thread_reset_event(voodoo->render_not_full_event[0]);
                if (voodoo->render_threads >= 2)
                        thread_reset_event(voodoo->render_not_full_event[1]);
                if (voodoo->render_threads == 4)
                {
                        thread_reset_event(voodoo->render_not_full_event[2]);
                        thread_reset_event(voodoo->render_not_full_event[3]);
                }
                if (PARAM_FULL(0))
                        thread_wait_event(voodoo->render_not_full_event[0], -1); /*Wait for room in ringbuffer*/
                if (voodoo->render_threads >= 2 && PARAM_FULL(1))
                        thread_wait_event(voodoo->render_not_full_event[1], -1); /*Wait for room in ringbuffer*/
                if (voodoo->render_threads == 4 && PARAM_FULL(2))
                        thread_wait_event(voodoo->render_not_full_event[2], -1); /*Wait for room in ringbuffer*/
                if (voodoo->render_threads == 4 && PARAM_FULL(3))
                        thread_wait_event(voodoo->render_not_full_event[3], -1); /*Wait for room in ringbuffer*/
        }

        voodoo_use_texture(voodoo, params, 0);
        if (voodoo->dual_tmus)
                voodoo_use_texture(voodoo, params, 1);

        memcpy(params_new, params, sizeof(voodoo_params_t));

        voodoo->params_write_idx++;

        if (PARAM_ENTRIES(0) < 4 || (voodoo->render_threads >= 2 && PARAM_ENTRIES(1) < 4) ||
                        (voodoo->render_threads == 4 && (PARAM_ENTRIES(2) < 4 || PARAM_ENTRIES(3) < 4)))
                voodoo_wake_render_thread(voodoo);
}
