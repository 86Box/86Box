/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the IBM RGB 528 true colour RAMDAC.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/plat_unused.h>

typedef union ibm_rgb528_pixel8_t {
    uint8_t pixel;
    struct {
        uint8_t b : 2;
        uint8_t g : 3;
        uint8_t r : 2;
    };
} ibm_rgb528_pixel8_t;

typedef union ibm_rgb528_pixel16_t {
    uint16_t pixel;
    struct {
        uint16_t b_ : 5;
        uint16_t g_ : 6;
        uint16_t r_ : 5;
    };
    struct {
        uint16_t b : 5;
        uint16_t g : 5;
        uint16_t r : 5;
        uint16_t c : 1;
    };
} ibm_rgb528_pixel16_t;

typedef union ibm_rgb528_pixel32_t {
    uint32_t pixel;
    struct {
        uint8_t b;
        uint8_t g;
        uint8_t r;
        uint8_t a;
    };
} ibm_rgb528_pixel32_t;

typedef struct ibm_rgb528_ramdac_t {
    int                  type;
    PALETTE              extpal;
    uint32_t             extpallook[256];
    uint8_t              indexed_data[2048];
    uint8_t              cursor32_data[256];
    uint8_t              cursor64_data[1024];
    uint8_t              palettes[3][256];
    ibm_rgb528_pixel32_t extra_pal[4];
    int16_t              hwc_y;
    int16_t              hwc_x;
    uint16_t             index;
    uint16_t             smlc_part;
    uint8_t              cmd_r0;
    uint8_t              cmd_r1;
    uint8_t              cmd_r2;
    uint8_t              cmd_r3;
    uint8_t              cmd_r4;
    uint8_t              status;
    uint8_t              indx_cntl;
    uint8_t              cursor_array;
    uint8_t              cursor_hotspot_x;
    uint8_t              cursor_hotspot_y;
    uint8_t              misc_clock;
    uint8_t              pix_f_ref_div;
    uint8_t              pix_f[16];
    uint8_t              pix_n[8];
    uint8_t              pix_m[8];
    float                ref_clock;
} ibm_rgb528_ramdac_t;

void
ibm_rgb528_render_4bpp(svga_t *svga)
{
    uint32_t                  *p;
    ibm_rgb528_pixel32_t       dat_out;
    uint8_t                    dat;
    uint32_t                   dat32     = 0x00000000;
    uint64_t                   dat64     = 0x0000000000000000ULL;
    uint64_t                   dat642    = 0x0000000000000000ULL;
    const ibm_rgb528_ramdac_t *ramdac    = (ibm_rgb528_ramdac_t *) svga->ramdac;
    uint8_t                    b8_dcol   = (ramdac->indexed_data[0x0c] & 0xc0) >> 6;
    uint8_t                    partition = (ramdac->indexed_data[0x07] & 0x0f) << 4;
    uint8_t                    swap_word = ramdac->indexed_data[0x72] & 0x10;
    uint8_t                    swap_nib  = ramdac->indexed_data[0x72] & 0x21;
    uint8_t                    vram_size = ramdac->indexed_data[0x70] & 0x03;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->changedvram[(svga->memaddr >> 12) + 2] || svga->fullchange) {
        p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        for (int x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
            if (vram_size == 3) {
                if (!(x & 31)) {
                    dat64  = *(uint64_t *) (&svga->vram[svga->memaddr]);
                    dat642 = *(uint64_t *) (&svga->vram[svga->memaddr + 8]);
                    if (swap_word) {
                        dat64  = (dat64 << 32ULL) | (dat64 >> 32ULL);
                        dat642 = (dat642 << 32ULL) | (dat642 >> 32ULL);
                    }
                }
                if (swap_nib)
                    dat = (((x & 16) ? dat642 : dat64) >> ((x & 15) << 2)) & 0xf;
                else
                    dat = (((x & 16) ? dat642 : dat64) >> (((x & 15) << 2) ^ 4)) & 0xf;
            } else if (vram_size == 1) {
                if (!(x & 15)) {
                    dat64 = *(uint64_t *) (&svga->vram[svga->memaddr]);
                    if (swap_word)
                        dat64 = (dat64 << 32ULL) | (dat64 >> 32ULL);
                }
                if (swap_nib)
                    dat = (dat64 >> ((x & 15) << 2)) & 0xf;
                else
                    dat = (dat64 >> (((x & 15) << 2) ^ 4)) & 0xf;
            } else {
                if (!(x & 7))
                    dat32 = *(uint32_t *) (&svga->vram[svga->memaddr]);
                if (swap_nib)
                    dat = (dat32 >> ((x & 7) << 2)) & 0xf;
                else
                    dat = (dat32 >> (((x & 7) << 2) ^ 4)) & 0xf;
            }
            if (b8_dcol == 0x00) {
                dat_out.a = 0x00;
                dat_out.r = ramdac->palettes[0][partition | dat];
                dat_out.g = ramdac->palettes[1][partition | dat];
                dat_out.b = ramdac->palettes[2][partition | dat];
            } else
                dat_out.pixel = video_8togs[dat];
            if (svga->lowres) {
                p[x << 1] = p[(x << 1) + 1] = dat_out.pixel & 0xffffff;
            } else
                p[x] = dat_out.pixel & 0xffffff;

            if ((vram_size == 3) && ((x & 31) == 31))
                svga->memaddr = (svga->memaddr + 16) & svga->vram_display_mask;
            if ((vram_size == 1) && ((x & 15) == 15))
                svga->memaddr = (svga->memaddr + 8) & svga->vram_display_mask;
            else if ((!vram_size) && ((x & 7) == 7))
                svga->memaddr = (svga->memaddr + 4) & svga->vram_display_mask;
        }
    }
}

void
ibm_rgb528_render_8bpp(svga_t *svga)
{
    uint32_t                  *p;
    ibm_rgb528_pixel32_t       dat_out;
    uint8_t                    dat;
    uint32_t                   dat32     = 0x00000000;
    uint64_t                   dat64     = 0x0000000000000000ULL;
    uint64_t                   dat642    = 0x0000000000000000ULL;
    const ibm_rgb528_ramdac_t *ramdac    = (ibm_rgb528_ramdac_t *) svga->ramdac;
    uint8_t                    b8_dcol   = (ramdac->indexed_data[0x0c] & 0xc0) >> 6;
    uint8_t                    swap_word = ramdac->indexed_data[0x72] & 0x10;
    uint8_t                    vram_size = ramdac->indexed_data[0x70] & 0x03;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->changedvram[(svga->memaddr >> 12) + 2] || svga->fullchange) {
        p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        for (int x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
            if (vram_size == 3) {
                if (!(x & 15)) {
                    dat64  = *(uint64_t *) (&svga->vram[svga->memaddr]);
                    dat642 = *(uint64_t *) (&svga->vram[svga->memaddr + 8]);
                    if (swap_word) {
                        dat64  = (dat64 << 32ULL) | (dat64 >> 32ULL);
                        dat642 = (dat642 << 32ULL) | (dat642 >> 32ULL);
                    }
                }
                dat = (((x & 8) ? dat642 : dat64) >> ((x & 7) << 3)) & 0xff;
            } else if (vram_size == 1) {
                if (!(x & 7)) {
                    dat64 = *(uint64_t *) (&svga->vram[svga->memaddr]);
                    if (swap_word)
                        dat64 = (dat64 << 32ULL) | (dat64 >> 32ULL);
                }
                dat = (dat64 >> ((x & 7) << 3)) & 0xff;
            } else {
                if (!(x & 3))
                    dat32 = *(uint32_t *) (&svga->vram[svga->memaddr]);
                dat = (dat32 >> ((x & 3) << 3)) & 0xff;
            }
            if (b8_dcol == 0x00) {
                dat_out.a = 0x00;
                dat_out.r = ramdac->palettes[0][dat];
                dat_out.g = ramdac->palettes[1][dat];
                dat_out.b = ramdac->palettes[2][dat];
            } else
                dat_out.pixel = video_8togs[dat];
            if (svga->lowres) {
                p[x << 1] = p[(x << 1) + 1] = dat_out.pixel & 0xffffff;
            } else
                p[x] = dat_out.pixel & 0xffffff;

            if ((vram_size == 3) && ((x & 15) == 15))
                svga->memaddr = (svga->memaddr + 16) & svga->vram_display_mask;
            else if ((vram_size == 1) && ((x & 7) == 7))
                svga->memaddr = (svga->memaddr + 8) & svga->vram_display_mask;
            else if ((!vram_size) && ((x & 3) == 3))
                svga->memaddr = (svga->memaddr + 4) & svga->vram_display_mask;
        }
    }
}

void
ibm_rgb528_render_15_16bpp(svga_t *svga)
{
    uint32_t                  *p;
    ibm_rgb528_pixel16_t      *dat_ex;
    ibm_rgb528_pixel32_t       dat_out;
    uint16_t                   dat;
    uint32_t                   dat32     = 0x00000000;
    uint64_t                   dat64     = 0x0000000000000000ULL;
    uint64_t                   dat642    = 0x0000000000000000ULL;
    const ibm_rgb528_ramdac_t *ramdac    = (ibm_rgb528_ramdac_t *) svga->ramdac;
    uint8_t                    b16_dcol  = (ramdac->indexed_data[0x0c] & 0xc0) >> 6;
    uint8_t                    by16_pol  = ramdac->indexed_data[0x0c] & 0x20;
    uint8_t                    b555_565  = ramdac->indexed_data[0x0c] & 0x02;
    uint8_t                    bspr_cnt  = ramdac->indexed_data[0x0c] & 0x01;
    uint8_t                    partition = (ramdac->indexed_data[0x07] & 0x0e) << 4;
    uint8_t                    b6bit_lin = ramdac->indexed_data[0x07] & 0x80;
    uint8_t                    swaprb    = ramdac->indexed_data[0x72] & 0x80;
    uint8_t                    swap_word = ramdac->indexed_data[0x72] & 0x10;
    uint8_t                    vram_size = ramdac->indexed_data[0x70] & 0x01;
    uint8_t                    temp;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (b555_565 && (b16_dcol != 0x01))
        partition &= 0xc0;

    if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->changedvram[(svga->memaddr >> 12) + 2] || svga->fullchange) {
        p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        for (int x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
            if (vram_size == 2) {
                if (!(x & 7)) {
                    dat64  = *(uint64_t *) (&svga->vram[svga->memaddr]);
                    dat642 = *(uint64_t *) (&svga->vram[svga->memaddr + 8]);
                    if (swap_word) {
                        dat64  = (dat64 << 32ULL) | (dat64 >> 32ULL);
                        dat642 = (dat64 << 32ULL) | (dat642 >> 32ULL);
                    }
                }
                dat = (((x & 4) ? dat642 : dat64) >> ((x & 3) << 4)) & 0xffff;
            } else if (vram_size == 1) {
                if (!(x & 3)) {
                    dat64 = *(uint64_t *) (&svga->vram[svga->memaddr]);
                    if (swap_word)
                        dat64 = (dat64 << 32ULL) | (dat64 >> 32ULL);
                }
                dat = (dat64 >> ((x & 3) << 4)) & 0xffff;
            } else {
                if (!(x & 1))
                    dat32 = *(uint32_t *) (&svga->vram[svga->memaddr]);
                dat = (dat32 >> ((x & 1) << 4)) & 0xffff;
            }
            dat_ex = (ibm_rgb528_pixel16_t *) &dat;
            if (b555_565 && (b16_dcol != 0x01)) {
                if (swaprb) {
                    temp       = dat_ex->r_;
                    dat_ex->r_ = dat_ex->b_;
                    dat_ex->b_ = temp;
                }
                if (b16_dcol == 0x00) {
                    dat_out.a = 0x00;
                    if (bspr_cnt) {
                        dat_out.r = ramdac->palettes[0][partition | dat_ex->r_];
                        dat_out.g = ramdac->palettes[1][partition | dat_ex->g_];
                        dat_out.b = ramdac->palettes[2][partition | dat_ex->b_];
                    } else {
                        dat_out.r = ramdac->palettes[0][dat_ex->r_ << 3];
                        dat_out.g = ramdac->palettes[1][dat_ex->g_ << 2];
                        dat_out.b = ramdac->palettes[2][dat_ex->b_ << 3];
                    }
                    if ((svga->ramdac_type != RAMDAC_8BIT) && !b6bit_lin) {
                        dat_out.r |= ((dat_out.r & 0xc0) >> 6);
                        dat_out.g |= ((dat_out.g & 0xc0) >> 6);
                        dat_out.b |= ((dat_out.b & 0xc0) >> 6);
                    }
                } else
                    dat_out.pixel = video_16to32[dat_ex->pixel];
            } else {
                if (swaprb) {
                    temp      = dat_ex->r;
                    dat_ex->r = dat_ex->b;
                    dat_ex->b = temp;
                }
                if (by16_pol)
                    dat ^= 0x8000;
                if ((b16_dcol == 0x00) || ((b16_dcol == 0x01) && !(dat & 0x8000))) {
                    dat_out.a = 0x00;
                    if (bspr_cnt) {
                        dat_out.r = ramdac->palettes[0][partition | dat_ex->r];
                        dat_out.g = ramdac->palettes[1][partition | dat_ex->g];
                        dat_out.b = ramdac->palettes[2][partition | dat_ex->b];
                    } else {
                        dat_out.r = ramdac->palettes[0][dat_ex->r << 3];
                        dat_out.g = ramdac->palettes[1][dat_ex->g << 3];
                        dat_out.b = ramdac->palettes[2][dat_ex->b << 3];
                    }
                    if ((svga->ramdac_type != RAMDAC_8BIT) && !b6bit_lin) {
                        dat_out.r |= ((dat_out.r & 0xc0) >> 6);
                        dat_out.g |= ((dat_out.g & 0xc0) >> 6);
                        dat_out.b |= ((dat_out.b & 0xc0) >> 6);
                    }
                } else
                    dat_out.pixel = video_15to32[dat_ex->pixel & 0x7fff];
            }
            if (svga->lowres) {
                p[x << 1] = p[(x << 1) + 1] = dat_out.pixel & 0xffffff;
            } else
                p[x] = dat_out.pixel & 0xffffff;

            if ((vram_size == 3) && ((x & 7) == 7))
                svga->memaddr = (svga->memaddr + 16) & svga->vram_display_mask;
            else if ((vram_size == 1) && ((x & 3) == 3))
                svga->memaddr = (svga->memaddr + 8) & svga->vram_display_mask;
            else if (!vram_size && ((x & 1) == 1))
                svga->memaddr = (svga->memaddr + 4) & svga->vram_display_mask;
        }
    }
}

void
ibm_rgb528_render_24bpp(svga_t *svga)
{
    uint32_t                  *p;
    ibm_rgb528_pixel32_t      *dat_ex;
    uint32_t                   dat;
    uint64_t                   dat64[6];
    uint8_t                   *dat8      = (uint8_t *) dat64;
    const ibm_rgb528_ramdac_t *ramdac    = (ibm_rgb528_ramdac_t *) svga->ramdac;
    uint8_t                    b24_dcol  = ramdac->indexed_data[0x0d] & 0x01;
    uint8_t                    swaprb    = ramdac->indexed_data[0x72] & 0x80;
    uint8_t                    swap_word = ramdac->indexed_data[0x72] & 0x10;
    uint8_t                    vram_size = ramdac->indexed_data[0x70] & 0x01;
    uint8_t                    b6bit_lin = ramdac->indexed_data[0x07] & 0x80;
    uint8_t                    temp;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->changedvram[(svga->memaddr >> 12) + 2] || svga->fullchange) {
        p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        for (int x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
            dat_ex = (ibm_rgb528_pixel32_t *) &dat;
            if (vram_size == 3) {
                if ((x & 15) == 0) {
                    dat64[0] = *(uint64_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
                    dat64[1] = *(uint64_t *) (&svga->vram[(svga->memaddr + 8) & svga->vram_display_mask]);
                    dat64[2] = *(uint64_t *) (&svga->vram[(svga->memaddr + 16) & svga->vram_display_mask]);
                    dat64[3] = *(uint64_t *) (&svga->vram[(svga->memaddr + 24) & svga->vram_display_mask]);
                    dat64[4] = *(uint64_t *) (&svga->vram[(svga->memaddr + 32) & svga->vram_display_mask]);
                    dat64[5] = *(uint64_t *) (&svga->vram[(svga->memaddr + 40) & svga->vram_display_mask]);
                    if (swap_word) {
                        dat64[0] = (dat64[0] << 32ULL) | (dat64[0] >> 32ULL);
                        dat64[1] = (dat64[1] << 32ULL) | (dat64[1] >> 32ULL);
                        dat64[2] = (dat64[2] << 32ULL) | (dat64[2] >> 32ULL);
                        dat64[3] = (dat64[3] << 32ULL) | (dat64[3] >> 32ULL);
                        dat64[4] = (dat64[4] << 32ULL) | (dat64[4] >> 32ULL);
                        dat64[5] = (dat64[5] << 32ULL) | (dat64[5] >> 32ULL);
                    }
                }
                dat_ex = (ibm_rgb528_pixel32_t *) &(dat8[(x & 15) * 3]);
            } else if (vram_size == 1) {
                if ((x & 7) == 0) {
                    dat64[0] = *(uint64_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
                    dat64[1] = *(uint64_t *) (&svga->vram[(svga->memaddr + 8) & svga->vram_display_mask]);
                    dat64[2] = *(uint64_t *) (&svga->vram[(svga->memaddr + 16) & svga->vram_display_mask]);
                    if (swap_word) {
                        dat64[0] = (dat64[0] << 32ULL) | (dat64[0] >> 32ULL);
                        dat64[1] = (dat64[1] << 32ULL) | (dat64[1] >> 32ULL);
                        dat64[2] = (dat64[2] << 32ULL) | (dat64[2] >> 32ULL);
                    }
                }
                dat_ex = (ibm_rgb528_pixel32_t *) &(dat8[(x & 7) * 3]);
            } else
                dat = 0x00000000;
            if (swaprb) {
                temp      = dat_ex->r;
                dat_ex->r = dat_ex->b;
                dat_ex->b = temp;
            }
            if (b24_dcol == 0x00) {
                dat_ex->a = 0x00;
                dat_ex->r = ramdac->palettes[0][dat_ex->r];
                dat_ex->g = ramdac->palettes[1][dat_ex->g];
                dat_ex->g = ramdac->palettes[2][dat_ex->b];
                if ((svga->ramdac_type != RAMDAC_8BIT) && !b6bit_lin) {
                    dat_ex->r |= ((dat_ex->r & 0xc0) >> 6);
                    dat_ex->g |= ((dat_ex->g & 0xc0) >> 6);
                    dat_ex->b |= ((dat_ex->b & 0xc0) >> 6);
                }
            }
            if (svga->lowres) {
                p[x << 1] = p[(x << 1) + 1] = dat_ex->pixel & 0xffffff;
            } else
                p[x] = dat_ex->pixel & 0xffffff;

            if ((vram_size == 3) && ((x & 15) == 15))
                svga->memaddr = (svga->memaddr + 48) & svga->vram_display_mask;
            else if ((vram_size == 1) && ((x & 7) == 7))
                svga->memaddr = (svga->memaddr + 24) & svga->vram_display_mask;
        }
    }
}

void
ibm_rgb528_render_32bpp(svga_t *svga)
{
    uint32_t                  *p;
    ibm_rgb528_pixel32_t      *dat_ex;
    uint32_t                   dat       = 0x00000000;
    uint64_t                   dat64     = 0x0000000000000000ULL;
    uint64_t                   dat642    = 0x0000000000000000ULL;
    const ibm_rgb528_ramdac_t *ramdac    = (ibm_rgb528_ramdac_t *) svga->ramdac;
    uint8_t                    b32_dcol  = ramdac->indexed_data[0x0e] & 0x03;
    uint8_t                    by32_pol  = ramdac->indexed_data[0x0e] & 0x04;
    uint8_t                    swaprb    = ramdac->indexed_data[0x72] & 0x80;
    uint8_t                    swap_word = ramdac->indexed_data[0x72] & 0x10;
    uint8_t                    vram_size = ramdac->indexed_data[0x70] & 0x01;
    uint8_t                    b6bit_lin = ramdac->indexed_data[0x07] & 0x80;
    uint8_t                    temp;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->changedvram[(svga->memaddr >> 12) + 2] || svga->fullchange) {
        p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        for (int x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
            if (vram_size == 3) {
                if (!(x & 3)) {
                    dat64  = *(uint64_t *) (&svga->vram[svga->memaddr]);
                    dat642 = *(uint64_t *) (&svga->vram[svga->memaddr + 8]);
                    if (swap_word) {
                        dat64  = (dat64 << 32ULL) | (dat64 >> 32ULL);
                        dat642 = (dat642 << 32ULL) | (dat642 >> 32ULL);
                    }
                }
                dat = (((x & 2) ? dat642 : dat64) >> ((x & 1ULL) << 5ULL)) & 0xffffffff;
            } else if (vram_size == 1) {
                if (!(x & 1)) {
                    dat64 = *(uint64_t *) (&svga->vram[svga->memaddr]);
                    if (swap_word)
                        dat64 = (dat64 << 32ULL) | (dat64 >> 32ULL);
                }
                dat = (dat64 >> ((x & 1ULL) << 5ULL)) & 0xffffffff;
            } else
                dat = *(uint32_t *) (&svga->vram[svga->memaddr]);
            dat_ex = (ibm_rgb528_pixel32_t *) &dat;
            if (swaprb) {
                temp      = dat_ex->r;
                dat_ex->r = dat_ex->b;
                dat_ex->b = temp;
            }
            if ((b32_dcol < 0x03) && by32_pol)
                dat ^= 0x01000000;
            if ((b32_dcol == 0x00) || ((b32_dcol == 0x01) && !(dat & 0x01000000))) {
                dat_ex->a = 0x00;
                dat_ex->r = ramdac->palettes[0][dat_ex->r];
                dat_ex->g = ramdac->palettes[1][dat_ex->g];
                dat_ex->g = ramdac->palettes[2][dat_ex->b];
                if ((svga->ramdac_type != RAMDAC_8BIT) && !b6bit_lin) {
                    dat_ex->r |= ((dat_ex->r & 0xc0) >> 6);
                    dat_ex->g |= ((dat_ex->g & 0xc0) >> 6);
                    dat_ex->b |= ((dat_ex->b & 0xc0) >> 6);
                }
            }
            if (svga->lowres) {
                p[x << 1] = p[(x << 1) + 1] = dat_ex->pixel & 0xffffff;
            } else
                p[x] = dat_ex->pixel & 0xffffff;

            if ((vram_size == 3) && ((x & 3) == 3))
                svga->memaddr = (svga->memaddr + 16) & svga->vram_display_mask;
            else if ((vram_size == 1) && ((x & 1) == 1))
                svga->memaddr = (svga->memaddr + 8) & svga->vram_display_mask;
            else if (!vram_size)
                svga->memaddr = (svga->memaddr + 4) & svga->vram_display_mask;
        }
    }
}

static void
ibm_rgb528_set_bpp(ibm_rgb528_ramdac_t *ramdac, svga_t *svga)
{
    uint8_t b16_dcol = (ramdac->indexed_data[0x0c] & 0xc0) >> 6;
    uint8_t b555_565 = ramdac->indexed_data[0x0c] & 0x02;

    if (ramdac->indexed_data[0x071] & 0x01)
        switch (ramdac->indexed_data[0x00a] & 0x07) {
            case 0x02:
                svga->bpp = 4;
                break;
            case 0x03:
            default:
                svga->bpp = 8;
                break;
            case 0x04:
                if (b555_565 && (b16_dcol != 0x01))
                    svga->bpp = 16;
                else
                    svga->bpp = 15;
                break;
            case 0x05:
                svga->bpp = 24;
                break;
            case 0x06:
                svga->bpp = 32;
                break;
        }
    else
        svga->bpp = 8;

    svga_recalctimings(svga);
}

void
ibm_rgb528_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga)
{
    ibm_rgb528_ramdac_t *ramdac = (ibm_rgb528_ramdac_t *) priv;
    uint16_t             index;
    uint8_t              rs        = (addr & 0x03);
    uint16_t             da_mask   = 0x03ff;
    uint8_t              updt_cntl = (ramdac->indexed_data[0x30] & 0x08);
    rs |= (!!rs2 << 2);

    switch (rs) {
        case 0x00: /* Palette Write Index Register (RS value = 0000) */
        case 0x03:
            svga->dac_pos    = 0;
            svga->dac_status = addr & 0x03;
            svga->dac_addr   = val;
            if (svga->dac_status)
                svga->dac_addr = (svga->dac_addr + 1) & da_mask;
            break;
        case 0x01: /* Palette Data Register (RS value = 0001) */
            index = svga->dac_addr & 255;
            if (svga->ramdac_type == RAMDAC_8BIT)
                ramdac->palettes[svga->dac_pos][index] = val;
            else
                ramdac->palettes[svga->dac_pos][index] = (val & 0x3f) << 2;
            svga_out(addr, val, svga);
            break;
        case 0x02: /* Pixel Read Mask Register (RS value = 0010) */
            svga_out(addr, val, svga);
            break;
        case 0x04:
            ramdac->index = (ramdac->index & 0x0700) | val;
            if ((ramdac->index >= 0x0100) && (ramdac->index <= 0x04ff))
                ramdac->cursor_array = 1;
            break;
        case 0x05:
            ramdac->index = (ramdac->index & 0x00ff) | ((val & 0x07) << 0x08);
            if ((ramdac->index >= 0x0100) && (ramdac->index <= 0x04ff))
                ramdac->cursor_array = 1;
            break;
        case 0x06:
            if ((ramdac->index < 0x0100) || (ramdac->index > 0x04ff) || ramdac->cursor_array)
                ramdac->indexed_data[ramdac->index] = val;

            switch (ramdac->index) {
                case 0x00a:
                case 0x00c:
                    ibm_rgb528_set_bpp(ramdac, svga);
                    break;
                case 0x014:
                    ramdac->pix_f_ref_div = val;
                    break;
                case 0x020:
                case 0x022:
                case 0x024:
                case 0x026:
                case 0x028:
                case 0x02a:
                case 0x02c:
                case 0x02e:
                    switch (ramdac->indexed_data[0x0010] & 0x03) {
                        case 0x00:
                            ramdac->pix_f[(ramdac->index - 0x0020)] = val;
                            break;
                        case 0x01:
                            ramdac->pix_m[(ramdac->index - 0x0020) >> 1] = val;
                            break;
                        case 0x02:
                            ramdac->pix_f[(ramdac->index - 0x0020)] = val;
                            break;
                        case 0x03:
                            ramdac->pix_m[(ramdac->index - 0x0020) >> 1] = val;
                            break;
                        default:
                            break;
                    }
                    break;
                case 0x021:
                case 0x023:
                case 0x025:
                case 0x027:
                case 0x029:
                case 0x02b:
                case 0x02d:
                case 0x02f:
                    switch (ramdac->indexed_data[0x0010] & 0x03) {
                        case 0x00:
                            ramdac->pix_f[(ramdac->index - 0x0020)] = val;
                            break;
                        case 0x01:
                            ramdac->pix_n[(ramdac->index - 0x0020) >> 1] = val;
                            break;
                        case 0x02:
                            ramdac->pix_f[(ramdac->index - 0x0020)] = val;
                            break;
                        case 0x03:
                            ramdac->pix_n[(ramdac->index - 0x0020) >> 1] = val;
                            break;
                        default:
                            break;
                    }
                    break;

                case 0x030:
                    switch (val & 0xc0) {
                        case 0x00:
                            ramdac->smlc_part = 0x0100;
                            break;
                        case 0x40:
                            ramdac->smlc_part = 0x0200;
                            break;
                        case 0x80:
                            ramdac->smlc_part = 0x0300;
                            break;
                        case 0xc0:
                            ramdac->smlc_part = 0x0400;
                            break;

                        default:
                            break;
                    }
                    svga->dac_hwcursor.addr      = ramdac->smlc_part;
                    svga->dac_hwcursor.cur_xsize = svga->dac_hwcursor.cur_ysize = (val & 0x04) ? 64 : 32;
                    svga->dac_hwcursor.ena                                      = ((val & 0x03) != 0x00);
                    break;
                case 0x031:
                    if (!updt_cntl)
                        break;
                    ramdac->hwc_x        = (ramdac->hwc_x & 0xff00) | val;
                    svga->dac_hwcursor.x = ((int) ramdac->hwc_x) - ramdac->cursor_hotspot_x;
                    break;
                case 0x032:
                    /* Sign-extend the sign bit (7) to the remaining bits (6-4). */
                    val &= 0x8f;
                    if (val & 0x80)
                        val |= 0x70;
                    ramdac->indexed_data[ramdac->index] = val;
                    if (!updt_cntl)
                        break;
                    ramdac->hwc_x        = (ramdac->hwc_x & 0x00ff) | (val << 8);
                    svga->dac_hwcursor.x = ((int) ramdac->hwc_x) - ramdac->cursor_hotspot_x;
                    break;
                case 0x033:
                    if (!updt_cntl)
                        break;
                    ramdac->hwc_y        = (ramdac->hwc_y & 0xff00) | val;
                    svga->dac_hwcursor.y = ((int) ramdac->hwc_y) - ramdac->cursor_hotspot_y;
                    break;
                case 0x034:
                    /* Sign-extend the sign bit (7) to the remaining bits (6-4). */
                    val &= 0x8f;
                    if (val & 0x80)
                        val |= 0x70;
                    ramdac->indexed_data[ramdac->index] = val;
                    if (updt_cntl) {
                        ramdac->hwc_y        = (ramdac->hwc_y & 0x00ff) | (val << 8);
                        svga->dac_hwcursor.y = ((int) ramdac->hwc_y) - ramdac->cursor_hotspot_y;
                    } else {
                        ramdac->hwc_x = ramdac->indexed_data[0x031];
                        ramdac->hwc_x |= (ramdac->indexed_data[0x032] << 8);
                        ramdac->hwc_y = ramdac->indexed_data[0x033];
                        ramdac->hwc_y |= (val << 8);
                        svga->dac_hwcursor.x = ((int) ramdac->hwc_x) - ramdac->cursor_hotspot_x;
                        svga->dac_hwcursor.y = ((int) ramdac->hwc_y) - ramdac->cursor_hotspot_y;
                    }
                    break;
                case 0x035:
                    if (svga->dac_hwcursor.cur_xsize == 64)
                        ramdac->cursor_hotspot_x = (val & 0x3f);
                    else
                        ramdac->cursor_hotspot_x = (val & 0x1f);
                    svga->dac_hwcursor.x = ((int) ramdac->hwc_x) - ramdac->cursor_hotspot_x;
                    break;
                case 0x036:
                    if (svga->dac_hwcursor.cur_xsize == 64)
                        ramdac->cursor_hotspot_y = (val & 0x3f);
                    else
                        ramdac->cursor_hotspot_y = (val & 0x1f);
                    svga->dac_hwcursor.y = ((int) ramdac->hwc_y) - ramdac->cursor_hotspot_y;
                    break;
                case 0x040:
                case 0x043:
                case 0x046:
                    ramdac->extra_pal[(ramdac->index - 0x40) / 3].r = val;
                    break;
                case 0x041:
                case 0x044:
                case 0x047:
                    ramdac->extra_pal[(ramdac->index - 0x41) / 3].g = val;
                    break;
                case 0x042:
                case 0x045:
                case 0x048:
                    ramdac->extra_pal[(ramdac->index - 0x42) / 3].b = val;
                    break;
                case 0x060:
                    ramdac->extra_pal[3].r = val;
                    break;
                case 0x061:
                    ramdac->extra_pal[3].g = val;
                    break;
                case 0x062:
                    ramdac->extra_pal[3].b = val;
                    break;
                case 0x071:
                    svga->ramdac_type = (val & 0x04) ? RAMDAC_8BIT : RAMDAC_6BIT;
                    ibm_rgb528_set_bpp(ramdac, svga);
                    break;
                default:
                    break;
            }
            if (ramdac->indx_cntl) {
                if (ramdac->index == 0x00ff)
                    ramdac->cursor_array = 0;
                ramdac->index++;
            }
            break;
        case 0x07:
            ramdac->indx_cntl = val & 0x01;
            break;

        default:
            break;
    }

    return;
}

uint8_t
ibm_rgb528_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga)
{
    ibm_rgb528_ramdac_t *ramdac   = (ibm_rgb528_ramdac_t *) priv;
    uint8_t              temp     = 0xff;
    uint8_t              rs       = (addr & 0x03);
    uint8_t              loc_read = (ramdac->indexed_data[0x30] & 0x10);
    rs |= (!!rs2 << 2);

    switch (rs) {
        case 0x00: /* Palette Write Index Register (RS value = 0000) */
        case 0x01: /* Palette Data Register (RS value = 0001) */
        case 0x02: /* Pixel Read Mask Register (RS value = 0010) */
            temp = svga_in(addr, svga);
            break;
        case 0x03: /* Palette Read Index Register (RS value = 0011) */
            temp = svga->dac_addr & 0xff;
            if (ramdac->indexed_data[0x070] & 0x20)
                temp = (temp & 0xfc) | svga->dac_status;
            break;
        case 0x04:
            temp = ramdac->index & 0xff;
            break;
        case 0x05:
            temp = ramdac->index >> 8;
            break;
        case 0x06:
            temp = ramdac->indexed_data[ramdac->index];
            switch (ramdac->index) {
                case 0x0000: /* Revision */
                    temp = 0xe0;
                    break;
                case 0x0001: /* ID */
                    temp = 0x02;
                    break;
                case 0x0031:
                    if (loc_read)
                        temp = ramdac->hwc_x & 0xff;
                    break;
                case 0x0032:
                    if (loc_read)
                        temp = ramdac->hwc_x >> 8;
                    break;
                case 0x0033:
                    if (loc_read)
                        temp = ramdac->hwc_y & 0xff;
                    break;
                case 0x0034:
                    if (loc_read)
                        temp = ramdac->hwc_y >> 8;
                    break;
                default:
                    temp = ramdac->indexed_data[ramdac->index];
                    break;
            }
            if (ramdac->indx_cntl) {
                if (ramdac->index == 0x00ff)
                    ramdac->cursor_array = 0;
                ramdac->index++;
            }
            break;
        case 0x07:
            temp = ramdac->indx_cntl;
            break;

        default:
            break;
    }

    return temp;
}

void
ibm_rgb528_recalctimings(void *priv, svga_t *svga)
{
    const ibm_rgb528_ramdac_t *ramdac = (ibm_rgb528_ramdac_t *) priv;

    svga->interlace = !!(ramdac->indexed_data[0x071] & 0x20);
    //pclog("MiscClockControl idx002=%02x, SystemClockControl idx008=%02x, Misc2 idx071=%02x, Misc1 idx070=%02x, Misc4 idx073=%02x.\n",
    //      ramdac->indexed_data[0x002], ramdac->indexed_data[0x008], ramdac->indexed_data[0x071], ramdac->indexed_data[0x070], ramdac->indexed_data[0x073]);

    if (ramdac->indexed_data[0x071] & 0x01) {
        if ((ramdac->indexed_data[0x070] & 0x03) == 0x03) {
            switch ((ramdac->indexed_data[0x002] & 0x0e) >> 1) {
                case 0x00:
                default:
                    svga->clock_multiplier = 0;
                    break;
                case 0x01:
                    svga->clock_multiplier = 1;
                    break;
                case 0x02:
                    svga->clock_multiplier = 2;
                    break;
                case 0x03:
                    svga->clock_multiplier = 3;
                    break;
                case 0x04:
                    svga->clock_multiplier = 4;
                    break;
            }
        } else if ((ramdac->indexed_data[0x070] & 0x03) == 0x01) {
            switch ((ramdac->indexed_data[0x002] & 0x0e) >> 1) {
                case 0x00:
                default:
                    svga->clock_multiplier = 1;
                    svga->clock *= 2.0;
                    break;
                case 0x01:
                    svga->clock_multiplier = 1;
                    break;
                case 0x02:
                    svga->clock_multiplier = 2;
                    break;
                case 0x03:
                    svga->clock_multiplier = 3;
                    break;
                case 0x04:
                    svga->clock_multiplier = 4;
                    break;
            }
        }
    }

    if (svga->scrblank || !svga->attr_palette_enable) {
        if ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1)) {
            if (((svga->gdcreg[5] & 0x60) == 0x40) || ((svga->gdcreg[5] & 0x60) == 0x60)) {
                if (ramdac->indexed_data[0x071] & 0x01) {
                    switch (svga->bpp) {
                        case 4:
                            svga->render = ibm_rgb528_render_4bpp;
                            break;
                        case 8:
                            svga->render = ibm_rgb528_render_8bpp;
                            break;
                        case 15:
                        case 16:
                            svga->render = ibm_rgb528_render_15_16bpp;
                            break;
                        case 24:
                            svga->render = ibm_rgb528_render_24bpp;
                            break;
                        case 32:
                            svga->render = ibm_rgb528_render_32bpp;
                            break;

                        default:
                            break;
                    }
                }
            }
        }
    }
}

float
ibm_rgb528_getclock(int clock, void *priv)
{
    const ibm_rgb528_ramdac_t *ramdac = (ibm_rgb528_ramdac_t *) priv;
    int                     pll_vco_div_cnt;
    int                     pll_df;
    int                     pll_ref_div_cnt;
    int                     ddot_divs[8]    = { 1, 2, 4, 8, 16, 1, 1, 1 };
    int                     ddot_div        = ddot_divs[(ramdac->indexed_data[0x0002] >> 1) & 0x07];
    float                   f_pll;

    if (clock == 0)
        return 25175000.0f;
    if (clock == 1)
        return 28322000.0f;

    switch (ramdac->indexed_data[0x0010] & 0x03) {
        case 0x00:
        default:
            pll_vco_div_cnt = ramdac->pix_f[clock & 0x03] & 0x3f;
            pll_df = 8 >> (ramdac->pix_f[clock & 0x03] >> 6);
            pll_ref_div_cnt = ramdac->pix_f_ref_div & 0x1f;
            break;
        case 0x01:
            pll_vco_div_cnt = ramdac->pix_m[clock & 0x03] & 0x3f;
            pll_df = 8 >> (ramdac->pix_m[clock & 0x03] >> 6);
            pll_ref_div_cnt = ramdac->pix_n[clock & 0x03] & 0x1f;
            break;
        case 0x02:
            pll_vco_div_cnt = ramdac->pix_f[ramdac->indexed_data[0x0011] & 0x0f] & 0x3f;
            pll_df = 8 >> (ramdac->pix_f[ramdac->indexed_data[0x0011] & 0x0f] >> 6);
            pll_ref_div_cnt = ramdac->pix_f_ref_div & 0x1f;
            break;
        case 0x03:
            pll_vco_div_cnt = ramdac->pix_m[ramdac->indexed_data[0x0011] & 0x0f] & 0x3f;
            pll_df = 8 >> (ramdac->pix_m[ramdac->indexed_data[0x0011] & 0x0f] >> 6);
            pll_ref_div_cnt = ramdac->pix_n[ramdac->indexed_data[0x0011] & 0x0f] & 0x1f;
            break;
    }
    f_pll = ramdac->ref_clock * (float) (pll_vco_div_cnt + 65) / (float) (pll_ref_div_cnt * pll_df);
    f_pll /= (float) ddot_div;

    //pclog("PIXCTRL1=%02x, clock=%d, m=%d, df=%d, n=%d, ctrl2=%02x, miscclock=%02x, sysclock=%02x, f_pll=%f.\n",
    //      ramdac->indexed_data[0x010], clock, pll_vco_div_cnt, pll_df, pll_ref_div_cnt, ramdac->indexed_data[0x011], ramdac->indexed_data[0x002], ramdac->indexed_data[0x008], f_pll);
    return f_pll;
}

void
ibm_rgb528_hwcursor_draw(svga_t *svga, int displine)
{
    uint8_t                    dat;
    uint8_t                    four_pixels = 0x00;
    int                        pitch;
    int                        x_pos;
    int                        y_pos;
    int                        offset = svga->dac_hwcursor_latch.x - svga->dac_hwcursor_latch.xoff;
    uint32_t                  *p;
    const ibm_rgb528_ramdac_t *ramdac      = (ibm_rgb528_ramdac_t *) svga->ramdac;
    uint8_t                    pix_ordr    = ramdac->indexed_data[0x30] & 0x20;
    uint8_t                    cursor_mode = ramdac->indexed_data[0x30] & 0x03;

    /* The planes come in one part, and each plane is 2bpp,
       so a 32x32 cursor has 8 bytes per line, and a 64x64
       cursor has 16 bytes per line. */
    pitch = (svga->dac_hwcursor_latch.cur_xsize >> 2); /* Bytes per line. */

    if ((ramdac->indexed_data[0x071] & 0x20) && svga->dac_hwcursor_oddeven)
        svga->dac_hwcursor_latch.addr += pitch;

    y_pos = displine;
    x_pos = offset + svga->x_add;
    p     = buffer32->line[y_pos];

    for (int x = 0; x < svga->dac_hwcursor_latch.cur_xsize; x++) {
        if (!(x & 3))
            four_pixels = ramdac->indexed_data[svga->dac_hwcursor_latch.addr];

        if (pix_ordr)
            dat = (four_pixels >> (((3 - x) & 3) << 1)) & 0x03;
        else
            dat = (four_pixels >> ((x & 3) << 1)) & 0x03;

        x_pos = offset + svga->x_add + x;

        switch (cursor_mode) {
            case 0x01:
                switch (dat) {
                    case 0x01:
                        /* Cursor Color 1 */
                        p[x_pos] = ramdac->extra_pal[0].pixel;
                        break;
                    case 0x02:
                        /* Cursor Color 2 */
                        p[x_pos] = ramdac->extra_pal[1].pixel;
                        break;
                    case 0x03:
                        /* Cursor Color 3 */
                        p[x_pos] = ramdac->extra_pal[2].pixel;
                        break;

                    default:
                        break;
                }
                break;
            case 0x02:
                switch (dat) {
                    case 0x00:
                        /* Cursor Color 1 */
                        p[x_pos] = ramdac->extra_pal[0].pixel;
                        break;
                    case 0x01:
                        /* Cursor Color 2 */
                        p[x_pos] = ramdac->extra_pal[1].pixel;
                        break;
                    case 0x03:
                        /* Complement */
                        p[x_pos] ^= 0xffffff;
                        break;

                    default:
                        break;
                }
                break;
            case 0x03:
                switch (dat) {
                    case 0x02:
                        /* Cursor Color 1 */
                        p[x_pos] = ramdac->extra_pal[0].pixel;
                        break;
                    case 0x03:
                        /* Cursor Color 2 */
                        p[x_pos] = ramdac->extra_pal[1].pixel;
                        break;

                    default:
                        break;
                }
                break;

            default:
                break;
        }

        if ((x & 3) == 3)
            svga->dac_hwcursor_latch.addr++;
    }

    if ((ramdac->indexed_data[0x071] & 0x20) && !svga->dac_hwcursor_oddeven)
        svga->dac_hwcursor_latch.addr += pitch;
}

void
ibm_rgb528_ramdac_set_ref_clock(void *priv, svga_t *svga, float ref_clock)
{
    ibm_rgb528_ramdac_t *ramdac = (ibm_rgb528_ramdac_t *) priv;

    if (ramdac)
        ramdac->ref_clock = ref_clock;

    svga_recalctimings(svga);
}

void *
ibm_rgb528_ramdac_init(UNUSED(const device_t *info))
{
    ibm_rgb528_ramdac_t *ramdac = (ibm_rgb528_ramdac_t *) malloc(sizeof(ibm_rgb528_ramdac_t));
    memset(ramdac, 0, sizeof(ibm_rgb528_ramdac_t));

    ramdac->smlc_part            = 0x0100;
    ramdac->ref_clock            = 14318184.0f;
    ramdac->pix_f_ref_div        = 0x07; /*Per datasheet regarding the reference clock value.*/

    ramdac->indexed_data[0x0008] = 0x0001;
    ramdac->indexed_data[0x0015] = 0x0008;
    ramdac->indexed_data[0x0016] = 0x0041;

    return ramdac;
}

static void
ibm_rgb528_ramdac_close(void *priv)
{
    ibm_rgb528_ramdac_t *ramdac = (ibm_rgb528_ramdac_t *) priv;

    if (ramdac)
        free(ramdac);
}

const device_t ibm_rgb528_ramdac_device = {
    .name          = "IBM RGB528 RAMDAC",
    .internal_name = "ibm_rgb528_ramdac",
    .flags         = 0,
    .local         = 0,
    .init          = ibm_rgb528_ramdac_init,
    .close         = ibm_rgb528_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
