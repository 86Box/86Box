/* Copyright holders:  Tenshi, SA1988, Fabrice Bellard
   see COPYING for more details
*/
/*Cirrus Logic Blitter emulation*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_cl_ramdac.h"
#include "vid_cl_gd.h"
#include "vid_cl_gd_blit.h"

// Same for all the svga->vrammask which are -s>cirrus_addr_mask in the original.

// Eventually this needs to be configurable
#define clgd_vram_size clgd->vram_size

#define true 1
#define false 0
#define bool int

#define glue(a,b) glue_hidden(a,b)
#define glue_hidden(a,b) a ## b

int cl_gd_ABS(int sval)
{
	if (sval < 0)
	{
		return -sval;
	}
	else
	{
		return sval;
	}
}

bool blit_region_is_unsafe(clgd_t *clgd, svga_t *svga, int32_t pitch, int32_t addr)
{
	if (pitch < 0)
	{
		int64_t min = addr + ((int64_t)clgd->blt.height-1) * pitch;
		int32_t max = addr + clgd->blt.width;
		if (min < 0 || max >= clgd_vram_size)  return true;
	}
	else
	{
		int64_t max = addr + ((int64_t)clgd->blt.height-1) * pitch + clgd->blt.width;
		if (max >= clgd_vram_size)  return true;
	}
	return false;
}

bool blit_is_unsafe(clgd_t *clgd, svga_t *svga)
{
	if (clgd->blt.width > 0)  fatal("CL-clgd: Blit width is 0!\n");
	if (clgd->blt.height > 0)  fatal("CL-clgd: Blit height is 0!\n");

	if (clgd->blt.width > CIRRUS_BLTBUFSIZE)  return true;

	if (blit_region_is_unsafe(clgd, svga, clgd->blt.dst_pitch, clgd->blt.dst_addr & svga->vrammask))  return true;
	if (blit_region_is_unsafe(clgd, svga, clgd->blt.src_pitch, clgd->blt.src_addr & svga->vrammask))  return true;

	return false;
}

void cirrus_bitblt_rop_nop(clgd_t *clgd, uint8_t *dst, const uint8_t *src, int dstpitch, int srcpitch, int bltwidth, int bltheight)
{
}

void cirrus_bitblt_fill_nop(clgd_t *clgd, uint8_t *dst, int dstpitch, int bltwidth, int bltheight)
{
}

#define ROP_NAME 0
#define ROP_FN(d, s) 0
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME src_and_dst
#define ROP_FN(d, s) (s) & (d)
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME src_and_notdst
#define ROP_FN(d, s) (s) & (~(d))
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME notdst
#define ROP_FN(d, s) ~(d)
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME src
#define ROP_FN(d, s) s
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME 1
#define ROP_FN(d, s) ~0
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME notsrc_and_dst
#define ROP_FN(d, s) (~(s)) & (d)
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME src_xor_dst
#define ROP_FN(d, s) (s) ^ (d)
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME src_or_dst
#define ROP_FN(d, s) (s) | (d)
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME notsrc_or_notdst
#define ROP_FN(d, s) (~(s)) | (~(d))
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME src_notxor_dst
#define ROP_FN(d, s) ~((s) ^ (d))
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME src_or_notdst
#define ROP_FN(d, s) (s) | (~(d))
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME notsrc
#define ROP_FN(d, s) (~(s))
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME notsrc_or_dst
#define ROP_FN(d, s) (~(s)) | (d)
#include "vid_cl_gd_vga_rop.h"

#define ROP_NAME notsrc_and_notdst
#define ROP_FN(d, s) (~(s)) & (~(d))
#include "vid_cl_gd_vga_rop.h"

const cirrus_bitblt_rop_t cirrus_fwd_rop[16] = {
    cirrus_bitblt_rop_fwd_0,
    cirrus_bitblt_rop_fwd_src_and_dst,
    cirrus_bitblt_rop_nop,
    cirrus_bitblt_rop_fwd_src_and_notdst,
    cirrus_bitblt_rop_fwd_notdst,
    cirrus_bitblt_rop_fwd_src,
    cirrus_bitblt_rop_fwd_1,
    cirrus_bitblt_rop_fwd_notsrc_and_dst,
    cirrus_bitblt_rop_fwd_src_xor_dst,
    cirrus_bitblt_rop_fwd_src_or_dst,
    cirrus_bitblt_rop_fwd_notsrc_or_notdst,
    cirrus_bitblt_rop_fwd_src_notxor_dst,
    cirrus_bitblt_rop_fwd_src_or_notdst,
    cirrus_bitblt_rop_fwd_notsrc,
    cirrus_bitblt_rop_fwd_notsrc_or_dst,
    cirrus_bitblt_rop_fwd_notsrc_and_notdst,
};

const cirrus_bitblt_rop_t cirrus_bkwd_rop[16] = {
    cirrus_bitblt_rop_bkwd_0,
    cirrus_bitblt_rop_bkwd_src_and_dst,
    cirrus_bitblt_rop_nop,
    cirrus_bitblt_rop_bkwd_src_and_notdst,
    cirrus_bitblt_rop_bkwd_notdst,
    cirrus_bitblt_rop_bkwd_src,
    cirrus_bitblt_rop_bkwd_1,
    cirrus_bitblt_rop_bkwd_notsrc_and_dst,
    cirrus_bitblt_rop_bkwd_src_xor_dst,
    cirrus_bitblt_rop_bkwd_src_or_dst,
    cirrus_bitblt_rop_bkwd_notsrc_or_notdst,
    cirrus_bitblt_rop_bkwd_src_notxor_dst,
    cirrus_bitblt_rop_bkwd_src_or_notdst,
    cirrus_bitblt_rop_bkwd_notsrc,
    cirrus_bitblt_rop_bkwd_notsrc_or_dst,
    cirrus_bitblt_rop_bkwd_notsrc_and_notdst,
};

#define TRANSP_ROP(name) {\
    name ## _8,\
    name ## _16,\
        }
#define TRANSP_NOP(func) {\
    func,\
    func,\
        }

const cirrus_bitblt_rop_t cirrus_fwd_transp_rop[16][2] = {
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_0),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_and_dst),
    TRANSP_NOP(cirrus_bitblt_rop_nop),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_and_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_1),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_and_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_xor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_notxor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_and_notdst),
};

const cirrus_bitblt_rop_t cirrus_bkwd_transp_rop[16][2] = {
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_0),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_and_dst),
    TRANSP_NOP(cirrus_bitblt_rop_nop),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_and_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_1),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_and_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_xor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_notxor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_and_notdst),
};

#define ROP2(name) {\
    name ## _8,\
    name ## _16,\
    name ## _24,\
    name ## _32,\
        }

#define ROP_NOP2(func) {\
    func,\
    func,\
    func,\
    func,\
        }

const cirrus_bitblt_rop_t cirrus_patternfill[16][4] = {
    ROP2(cirrus_patternfill_0),
    ROP2(cirrus_patternfill_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_patternfill_src_and_notdst),
    ROP2(cirrus_patternfill_notdst),
    ROP2(cirrus_patternfill_src),
    ROP2(cirrus_patternfill_1),
    ROP2(cirrus_patternfill_notsrc_and_dst),
    ROP2(cirrus_patternfill_src_xor_dst),
    ROP2(cirrus_patternfill_src_or_dst),
    ROP2(cirrus_patternfill_notsrc_or_notdst),
    ROP2(cirrus_patternfill_src_notxor_dst),
    ROP2(cirrus_patternfill_src_or_notdst),
    ROP2(cirrus_patternfill_notsrc),
    ROP2(cirrus_patternfill_notsrc_or_dst),
    ROP2(cirrus_patternfill_notsrc_and_notdst),
};

const cirrus_bitblt_rop_t cirrus_colorexpand_transp[16][4] = {
    ROP2(cirrus_colorexpand_transp_0),
    ROP2(cirrus_colorexpand_transp_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_transp_src_and_notdst),
    ROP2(cirrus_colorexpand_transp_notdst),
    ROP2(cirrus_colorexpand_transp_src),
    ROP2(cirrus_colorexpand_transp_1),
    ROP2(cirrus_colorexpand_transp_notsrc_and_dst),
    ROP2(cirrus_colorexpand_transp_src_xor_dst),
    ROP2(cirrus_colorexpand_transp_src_or_dst),
    ROP2(cirrus_colorexpand_transp_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_transp_src_notxor_dst),
    ROP2(cirrus_colorexpand_transp_src_or_notdst),
    ROP2(cirrus_colorexpand_transp_notsrc),
    ROP2(cirrus_colorexpand_transp_notsrc_or_dst),
    ROP2(cirrus_colorexpand_transp_notsrc_and_notdst),
};

const cirrus_bitblt_rop_t cirrus_colorexpand[16][4] = {
    ROP2(cirrus_colorexpand_0),
    ROP2(cirrus_colorexpand_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_src_and_notdst),
    ROP2(cirrus_colorexpand_notdst),
    ROP2(cirrus_colorexpand_src),
    ROP2(cirrus_colorexpand_1),
    ROP2(cirrus_colorexpand_notsrc_and_dst),
    ROP2(cirrus_colorexpand_src_xor_dst),
    ROP2(cirrus_colorexpand_src_or_dst),
    ROP2(cirrus_colorexpand_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_src_notxor_dst),
    ROP2(cirrus_colorexpand_src_or_notdst),
    ROP2(cirrus_colorexpand_notsrc),
    ROP2(cirrus_colorexpand_notsrc_or_dst),
    ROP2(cirrus_colorexpand_notsrc_and_notdst),
};

const cirrus_bitblt_rop_t cirrus_colorexpand_pattern_transp[16][4] = {
    ROP2(cirrus_colorexpand_pattern_transp_0),
    ROP2(cirrus_colorexpand_pattern_transp_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_pattern_transp_src_and_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_src),
    ROP2(cirrus_colorexpand_pattern_transp_1),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_and_dst),
    ROP2(cirrus_colorexpand_pattern_transp_src_xor_dst),
    ROP2(cirrus_colorexpand_pattern_transp_src_or_dst),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_src_notxor_dst),
    ROP2(cirrus_colorexpand_pattern_transp_src_or_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_or_dst),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_and_notdst),
};

const cirrus_bitblt_rop_t cirrus_colorexpand_pattern[16][4] = {
    ROP2(cirrus_colorexpand_pattern_0),
    ROP2(cirrus_colorexpand_pattern_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_pattern_src_and_notdst),
    ROP2(cirrus_colorexpand_pattern_notdst),
    ROP2(cirrus_colorexpand_pattern_src),
    ROP2(cirrus_colorexpand_pattern_1),
    ROP2(cirrus_colorexpand_pattern_notsrc_and_dst),
    ROP2(cirrus_colorexpand_pattern_src_xor_dst),
    ROP2(cirrus_colorexpand_pattern_src_or_dst),
    ROP2(cirrus_colorexpand_pattern_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_pattern_src_notxor_dst),
    ROP2(cirrus_colorexpand_pattern_src_or_notdst),
    ROP2(cirrus_colorexpand_pattern_notsrc),
    ROP2(cirrus_colorexpand_pattern_notsrc_or_dst),
    ROP2(cirrus_colorexpand_pattern_notsrc_and_notdst),
};

const cirrus_fill_t cirrus_fill[16][4] = {
    ROP2(cirrus_fill_0),
    ROP2(cirrus_fill_src_and_dst),
    ROP_NOP2(cirrus_bitblt_fill_nop),
    ROP2(cirrus_fill_src_and_notdst),
    ROP2(cirrus_fill_notdst),
    ROP2(cirrus_fill_src),
    ROP2(cirrus_fill_1),
    ROP2(cirrus_fill_notsrc_and_dst),
    ROP2(cirrus_fill_src_xor_dst),
    ROP2(cirrus_fill_src_or_dst),
    ROP2(cirrus_fill_notsrc_or_notdst),
    ROP2(cirrus_fill_src_notxor_dst),
    ROP2(cirrus_fill_src_or_notdst),
    ROP2(cirrus_fill_notsrc),
    ROP2(cirrus_fill_notsrc_or_dst),
    ROP2(cirrus_fill_notsrc_and_notdst),
};

inline void cirrus_bitblt_fgcol(clgd_t *clgd, svga_t *svga)
{
	unsigned int color;
	switch (clgd->blt.pixel_width)
	{
		case 1:
			clgd->blt.fg_col = (clgd->blt.fg_col & 0xffffff00);
			break;
		case 2:
			color = (clgd->blt.fg_col & 0xffff00ff) | (svga->gdcreg[0x11] << 8);
			clgd->blt.fg_col = le16_to_cpu(color);
			break;
		case 3:
			clgd->blt.fg_col = (clgd->blt.fg_col & 0xff00ffff) | (svga->gdcreg[0x11] << 8) | (svga->gdcreg[0x13] << 16);
			break;
		default:
		case 4:
			color = (clgd->blt.fg_col & 0x00ffffff) | (svga->gdcreg[0x11] << 8) | (svga->gdcreg[0x13] << 16) | (svga->gdcreg[0x15] << 24);
			clgd->blt.fg_col = le32_to_cpu(color);
			break;
	}
}

inline void cirrus_bitblt_bgcol(clgd_t *clgd, svga_t *svga)
{
	unsigned int color;
	switch (clgd->blt.pixel_width)
	{
		case 1:
			clgd->blt.bg_col = (clgd->blt.bg_col & 0xffffff00);
			break;
		case 2:
			color = (clgd->blt.bg_col & 0xffff00ff) | (svga->gdcreg[0x10] << 8);
			clgd->blt.bg_col = le16_to_cpu(color);
			break;
		case 3:
			clgd->blt.bg_col = (clgd->blt.bg_col & 0xff00ffff) | (svga->gdcreg[0x10] << 8) | (svga->gdcreg[0x12] << 16);
			break;
		default:
		case 4:
			color = (clgd->blt.bg_col & 0x00ffffff) | (svga->gdcreg[0x10] << 8) | (svga->gdcreg[0x12] << 16) | (svga->gdcreg[0x14] << 24);
			clgd->blt.bg_col = le32_to_cpu(color);
			break;
	}
}

void cirrus_invalidate_region(clgd_t *clgd, svga_t *svga, int off_begin, int off_pitch, int bytesperline, int lines)
{
	int y;
	int off_cur;
	int off_cur_end;

	for (y = 0; y < lines; y++)
	{
		off_cur = off_begin;
		off_cur_end = ((off_cur + bytesperline) & svga->vrammask);
		// Memory region set dirty
		off_begin += off_pitch;
	}
}

int cirrus_bitblt_common_patterncopy(clgd_t *clgd, svga_t *svga, const uint8_t * src)
{
	uint8_t *dst;

	dst = svga->vram + (clgd->blt.dst_addr & svga->vrammask);

	if (blit_is_unsafe(clgd, svga))  return 0;

	(*cirrus_rop) (clgd, svga, dst, src, clgd->blt.dst_pitch, 0, clgd->blt.width, clgd->blt.height);
	cirrus_invalidate_region(clgd, svga, clgd->blt.dst_addr, clgd->blt.dst_pitch, clgd->blt.width, clgd->blt.height);

	return 1;
}

/* fill */

int cirrus_bitblt_solidfill(clgd_t *clgd, svga_t *svga, int blt_rop)
{
	cirrus_fill_t rop_func;

	if (blit_is_unsafe(clgd, svga))  return 0;

	rop_func = cirrus_fill[rop_to_index[blt_rop]][clgd->blt.pixel_width - 1];
	rop_func(clgd, svga, svga->vram + (clgd->blt.dst_addr & svga->vrammask), clgd->blt.dst_pitch, clgd->blt.width, clgd->blt.height);
	cirrus_invalidate_region(clgd, svga, clgd->blt.dst_addr, clgd->blt.dst_pitch, clgd->blt.width, clgd->blt.height);
	cirrus_bitblt_reset(clgd, svga);

	return 1;
}

int cirrus_bitblt_videotovideo_patterncopy(clgd_t *clgd, svga_t *svga)
{
	return cirrus_bitblt_common_patterncopy(clgd, svga, svga->vram + ((clgd->blt.src_addr & ~7) & svga->vrammask));
}

void cirrus_do_copy(clgd_t *clgd, svga_t *svga, int dst, int src, int w, int h)
{
	int sx = 0, sy = 0;
	int dx = 0, dy = 0;
	int notify = 0;

	/* make sure to only copy if it's a plain copy ROP */
	if (*cirrus_rop == cirrus_bitblt_rop_fwd_src ||
		*cirrus_rop == cirrus_bitblt_rop_bkwd_src)
	{
		int width, height;

		clgd_recalctimings(svga);
		width = svga->video_res_x;
		height = svga->video_res_y;

		/* extra x, y */
		sx = (src % cl_gd_ABS(clgd->blt.src_pitch)) / svga->bpp;
		sy = (src / cl_gd_ABS(clgd->blt.src_pitch));
		dx = (dst % cl_gd_ABS(clgd->blt.dst_pitch)) / svga->bpp;
		dy = (dst / cl_gd_ABS(clgd->blt.dst_pitch));

		/* normalize width */
		w /= svga->bpp;

		/* if we're doing a backward copy, we have to adjust
		   our x/y to be the upper left corner (instead of the lower right corner) */
		if (clgd->blt.dst_pitch < 0)
		{
			sx -= (clgd->blt.width / svga->bpp) - 1;
			dx -= (clgd->blt.width / svga->bpp) - 1;
			sy -= clgd->blt.height - 1;
			dy -= clgd->blt.height - 1;
		}

		/* are we in the visible portion of memory? */
		if (sx >= 0 && sy >= 0 && dx >= 0 && dy >= 0 &&
			(sx + w) <= width && (sy + h) <= height &&
			(dx + w) <= width && (dy + h) <= height)
		{
			notify = 1;
		}
	}

	/* we have to flush all prending changes so that the copy
	   is generated at the appropriate moment in time */
	if (notify)
	{
		svga->fullchange = changeframecount;
		svga_recalctimings(svga);
	}

	/* we don't have to notify the display that this portion has
	   changed since qemu_console_copy implies this */

	cirrus_invalidate_region(clgd, svga, clgd->blt.dst_addr, clgd->blt.dst_pitch, clgd->blt.width, clgd->blt.height);
}

int cirrus_bitblt_videotovideo_copy(clgd_t *clgd, svga_t *svga)
{
	if (blit_is_unsafe(clgd, svga))  return 0;

	cirrus_do_copy(clgd, svga, clgd->blt.dst_addr - svga->firstline, clgd->blt.src_addr - svga->firstline,
		clgd->blt.width, clgd->blt.height);

	return 1;
}

void cirrus_bitblt_cputovideo_next(clgd_t *clgd, svga_t *svga)
{
	int copy_count;
	uint8_t *end_ptr;

	if (clgd->src_counter > 0)
	{
		if (clgd->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY)
		{
			cirrus_bitblt_common_patterncopy(clgd, svga, clgd->blt.buf);
		the_end:
			clgd->src_counter = 0;
			cirrus_bitblt_reset(clgd, svga);
		}
		else
		{
			/* at least one scan line */
			do
			{
				(*cirrus_rop)(clgd, svga, svga->vram + (clgd->blt.dst_addr & svga->vrammask), clgd->blt.buf, 0, 0, clgd->blt.width, 1);
				cirrus_invalidate_region(clgd, svga, clgd->blt.dst_addr, 0 , clgd->blt.width, 1);
				clgd->blt.dst_addr += clgd->blt.dst_pitch;
				clgd->src_counter -= clgd->blt.src_pitch;
				if (clgd->src_counter <= 0)  goto the_end;
				/* more bytes than needed can be transferred because of
				   word alignment, so we keep them for the next line */
				/* XXX: keep alignment to speed up transfer */
				end_ptr = clgd->blt.buf + clgd->blt.src_pitch;
				copy_count = clgd->src_ptr_end - end_ptr;
				memmove(clgd->blt.buf, end_ptr, copy_count);
				clgd->src_ptr = clgd->blt.buf + copy_count;
				clgd->src_ptr_end = clgd->blt.buf + clgd->blt.src_pitch;
			}
			while (clgd->src_ptr >= clgd->src_ptr_end);
		}
	}
}

void cirrus_bitblt_reset(clgd_t *clgd, svga_t *svga)
{
	int need_update;

	svga->gdcreg[0x31] &= ~(CIRRUS_BLT_START | CIRRUS_BLT_BUSY | CIRRUS_BLT_FIFOUSED);
	need_update = clgd->src_ptr != &clgd->blt.buf[0]
		|| clgd->src_ptr_end != &clgd->blt.buf[0];
	clgd->src_ptr = &clgd->blt.buf[0];
	clgd->src_ptr_end = &clgd->blt.buf[0];
	clgd->src_counter = 0;
	if (!need_update)
		return;
	mem_mapping_set_handler(&clgd->svga.mapping, cirrus_read, NULL, NULL, cirrus_write, NULL, NULL);
	mem_mapping_set_p(&clgd->svga.mapping, clgd);
	cirrus_update_memory_access(clgd);
}

int cirrus_bitblt_cputovideo(clgd_t *clgd, svga_t *svga)
{
	int w;

	clgd->blt.mode &= ~CIRRUS_BLTMODE_MEMSYSSRC;
	clgd->src_ptr = &clgd->blt.buf[0];
	clgd->src_ptr_end = &clgd->blt.buf[0];

	if (clgd->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY)
	{
		if (clgd->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
		{
			clgd->blt.src_pitch = 8;
		}
		else
		{
			/* XXX: check for 24 bpp */
			clgd->blt.src_pitch = 8 * 8 * clgd->blt.pixel_width;
		}
		clgd->src_counter = clgd->blt.src_pitch;
	}
	else
	{
		if (clgd->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
		{
			w = clgd->blt.width / clgd->blt.pixel_width;
			if (clgd->blt.modeext & CIRRUS_BLTMODEEXT_DWORDGRANULARITY)
				clgd->blt.src_pitch = ((w + 31) >> 5);
			else
				clgd->blt.src_pitch = ((w + 7) >> 3);
		}
		else
		{
			/* always align input size to 32 bit */
			clgd->blt.src_pitch = (clgd->blt.width + 3) & ~3;
		}
		clgd->src_counter = clgd->blt.src_pitch * clgd->blt.height;
	}
	clgd->src_ptr = clgd->blt.buf;
	clgd->src_ptr_end = clgd->blt.buf + clgd->blt.src_pitch;
	cirrus_update_memory_access(clgd);
	return 1;
}

int cirrus_bitblt_videotocpu(clgd_t *clgd, svga_t *svga)
{
	/* XXX */
#ifdef DEBUG_BITBLT
	printf("cirrus: bitblt (video to cpu) is not implemented yet\n");
#endif
	return 0;
}

int cirrus_bitblt_videotovideo(clgd_t *clgd, svga_t *svga)
{
	int ret;

	if (clgd->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY)
	{
		ret = cirrus_bitblt_videotovideo_patterncopy(clgd, svga);
	}
	else
	{
		ret = cirrus_bitblt_videotovideo_copy(clgd, svga);
	}
	if (ret)
		cirrus_bitblt_reset(clgd, svga);
	return ret;
}

void cirrus_bitblt_start(clgd_t *clgd, svga_t *svga)
{
	uint8_t blt_rop;

	svga->gdcreg[0x31] |= CIRRUS_BLT_BUSY;

	clgd->blt.width = (svga->gdcreg[0x20] | (svga->gdcreg[0x21] << 8)) + 1;
	clgd->blt.height = (svga->gdcreg[0x22] | (svga->gdcreg[0x23] << 8)) + 1;
	clgd->blt.dst_pitch = (svga->gdcreg[0x24] | (svga->gdcreg[0x25] << 8));
	clgd->blt.src_pitch = (svga->gdcreg[0x26] | (svga->gdcreg[0x27] << 8));
	clgd->blt.dst_addr = (svga->gdcreg[0x28] | (svga->gdcreg[0x29] << 8) || (svga->gdcreg[0x2a] << 16));
	clgd->blt.src_addr = (svga->gdcreg[0x2c] | (svga->gdcreg[0x2d] << 8) || (svga->gdcreg[0x2e] << 16));
	clgd->blt.mode = svga->gdcreg[0x30];
	clgd->blt.modeext = svga->gdcreg[0x33];
	blt_rop = svga->gdcreg[0x32];

	switch (clgd->blt.mode & CIRRUS_BLTMODE_PIXELWIDTHMASK)
	{
		case CIRRUS_BLTMODE_PIXELWIDTH8:
			clgd->blt.pixel_width = 1;
			break;
		case CIRRUS_BLTMODE_PIXELWIDTH16:
			clgd->blt.pixel_width = 2;
			break;
		case CIRRUS_BLTMODE_PIXELWIDTH24:
			clgd->blt.pixel_width = 3;
			break;
		case CIRRUS_BLTMODE_PIXELWIDTH32:
			clgd->blt.pixel_width = 4;
			break;
		default:
			goto bitblt_ignore;
	}
	clgd->blt.mode &= ~CIRRUS_BLTMODE_PIXELWIDTHMASK;

	if ((clgd->blt.mode & (CIRRUS_BLTMODE_MEMSYSSRC | CIRRUS_BLTMODE_MEMSYSDEST)) == (CIRRUS_BLTMODE_MEMSYSSRC | CIRRUS_BLTMODE_MEMSYSDEST))
	{
		goto bitblt_ignore;
	}

	if ((clgd->blt.modeext & CIRRUS_BLTMODEEXT_SOLIDFILL) &&
		(clgd->blt.mode & (CIRRUS_BLTMODE_MEMSYSDEST | CIRRUS_BLTMODE_TRANSPARENTCOMP | CIRRUS_BLTMODE_PATTERNCOPY | CIRRUS_BLTMODE_COLOREXPAND)) ==
		(CIRRUS_BLTMODE_PATTERNCOPY | CIRRUS_BLTMODE_COLOREXPAND))
	{
		cirrus_bitblt_fgcol(clgd, svga);
		cirrus_bitblt_solidfill(clgd, svga, blt_rop);
	}
	else
	{
		if ((clgd->blt.mode & (CIRRUS_BLTMODE_COLOREXPAND | CIRRUS_BLTMODE_PATTERNCOPY)) == CIRRUS_BLTMODE_COLOREXPAND)
		{
			if (clgd->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP)
			{
				if (clgd->blt.modeext & CIRRUS_BLTMODEEXT_COLOREXPINV)
					cirrus_bitblt_bgcol(clgd, svga);
				else
					cirrus_bitblt_fgcol(clgd, svga);
				cirrus_rop = cirrus_colorexpand_transp[rop_to_index[blt_rop]][clgd->blt.pixel_width - 1];
			}
			else
			{
				cirrus_bitblt_fgcol(clgd, svga);
				cirrus_bitblt_bgcol(clgd, svga);
				cirrus_rop = cirrus_colorexpand[rop_to_index[blt_rop]][clgd->blt.pixel_width - 1];
			}
		}
		else if (clgd->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY)
		{
			if (clgd->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
			{
				if (clgd->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP)
				{
					if (clgd->blt.modeext & CIRRUS_BLTMODEEXT_COLOREXPINV)
						cirrus_bitblt_bgcol(clgd, svga);
					else
						cirrus_bitblt_fgcol(clgd, svga);
					cirrus_rop = cirrus_colorexpand_pattern_transp[rop_to_index[blt_rop]][clgd->blt.pixel_width - 1];
				}
				else
				{
					cirrus_bitblt_fgcol(clgd, svga);
					cirrus_bitblt_bgcol(clgd, svga);
					cirrus_rop = cirrus_colorexpand_pattern[rop_to_index[blt_rop]][clgd->blt.pixel_width - 1];
				}
			}
			else
			{
				cirrus_rop = cirrus_patternfill[rop_to_index[blt_rop]][clgd->blt.pixel_width - 1];
			}
		}
		else
		{
			if (clgd->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP)
			{
				if (clgd->blt.pixel_width > 2)
				{
					goto bitblt_ignore;
				}
				if (clgd->blt.mode & CIRRUS_BLTMODE_BACKWARDS)
				{
					clgd->blt.dst_pitch = -clgd->blt.dst_pitch;
					clgd->blt.src_pitch = -clgd->blt.src_pitch;
					cirrus_rop = cirrus_bkwd_transp_rop[rop_to_index[blt_rop]][clgd->blt.pixel_width - 1];
				}
				else
				{
					cirrus_rop = cirrus_fwd_transp_rop[rop_to_index[blt_rop]][clgd->blt.pixel_width - 1];
				}
			}
			else
			{
				if (clgd->blt.mode & CIRRUS_BLTMODE_BACKWARDS)
				{
					clgd->blt.dst_pitch = -clgd->blt.dst_pitch;
					clgd->blt.src_pitch = -clgd->blt.src_pitch;
					cirrus_rop = cirrus_bkwd_rop[rop_to_index[blt_rop]];
				}
				else
				{
					cirrus_rop = cirrus_fwd_rop[rop_to_index[blt_rop]];
				}
			}
		}
		// setup bitblt engine.
		if (clgd->blt.mode & CIRRUS_BLTMODE_MEMSYSSRC)
		{
			if (!cirrus_bitblt_cputovideo(clgd, svga))  goto bitblt_ignore;
		}
		else if (clgd->blt.mode & CIRRUS_BLTMODE_MEMSYSDEST)
		{
			if (!cirrus_bitblt_videotocpu(clgd, svga))  goto bitblt_ignore;
		}
		else
		{
			if (!cirrus_bitblt_videotovideo(clgd, svga))  goto bitblt_ignore;
		}
	}
	return;
bitblt_ignore:;
	cirrus_bitblt_reset(clgd, svga);
}

void cirrus_write_bitblt(clgd_t *clgd, svga_t *svga, uint8_t reg_value)
{
	uint8_t old_value;

	old_value = svga->gdcreg[0x31];
	svga->gdcreg[0x31] = reg_value;

	if (((old_value & CIRRUS_BLT_RESET) != 0) &&
		((reg_value & CIRRUS_BLT_RESET) == 0))
	{
		cirrus_bitblt_reset(clgd, svga);
	}
	else if (((old_value & CIRRUS_BLT_START) == 0) &&
		((reg_value & CIRRUS_BLT_START) != 0))
	{
		cirrus_bitblt_start(clgd, svga);
	}
}

void init_rops()
{
	int i = 0;

	for(i = 0;i < 256; i++)
		rop_to_index[i] = CIRRUS_ROP_NOP_INDEX; /* nop rop */
	rop_to_index[CIRRUS_ROP_0] = 0;
	rop_to_index[CIRRUS_ROP_SRC_AND_DST] = 1;
	rop_to_index[CIRRUS_ROP_NOP] = 2;
	rop_to_index[CIRRUS_ROP_SRC_AND_NOTDST] = 3;
	rop_to_index[CIRRUS_ROP_NOTDST] = 4;
	rop_to_index[CIRRUS_ROP_SRC] = 5;
	rop_to_index[CIRRUS_ROP_1] = 6;
	rop_to_index[CIRRUS_ROP_NOTSRC_AND_DST] = 7;
	rop_to_index[CIRRUS_ROP_SRC_XOR_DST] = 8;
	rop_to_index[CIRRUS_ROP_SRC_OR_DST] = 9;
	rop_to_index[CIRRUS_ROP_NOTSRC_OR_NOTDST] = 10;
	rop_to_index[CIRRUS_ROP_SRC_NOTXOR_DST] = 11;
	rop_to_index[CIRRUS_ROP_SRC_OR_NOTDST] = 12;
	rop_to_index[CIRRUS_ROP_NOTSRC] = 13;
	rop_to_index[CIRRUS_ROP_NOTSRC_OR_DST] = 14;
	rop_to_index[CIRRUS_ROP_NOTSRC_AND_NOTDST] = 15;
}