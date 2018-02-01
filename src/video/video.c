/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Main video-rendering module.
 *
 *		Video timing settings -
 *
 *		8-bit - 1mb/sec
 *		B = 8 ISA clocks
 *		W = 16 ISA clocks
 *		L = 32 ISA clocks
 *
 *		Slow 16-bit - 2mb/sec
 *		B = 6 ISA clocks
 *		W = 8 ISA clocks
 *		L = 16 ISA clocks
 *
 *		Fast 16-bit - 4mb/sec
 *		B = 3 ISA clocks
 *		W = 3 ISA clocks
 *		L = 6 ISA clocks
 *
 *		Slow VLB/PCI - 8mb/sec (ish)
 *		B = 4 bus clocks
 *		W = 8 bus clocks
 *		L = 16 bus clocks
 *
 *		Mid VLB/PCI -
 *		B = 4 bus clocks
 *		W = 5 bus clocks
 *		L = 10 bus clocks
 *
 *		Fast VLB/PCI -
 *		B = 3 bus clocks
 *		W = 3 bus clocks
 *		L = 4 bus clocks
 *
 * Version:	@(#)video.c	1.0.14	2018/02/01
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../machine/machine.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../config.h"
#include "../timer.h"
#include "../plat.h"
#include "video.h"
#include "vid_svga.h"


enum {
    VIDEO_ISA = 0,
    VIDEO_BUS
};


bitmap_t	*screen = NULL,
		*buffer = NULL,
		*buffer32 = NULL;
uint8_t		fontdat[2048][8];		/* IBM CGA font */
uint8_t		fontdatm[2048][16];		/* IBM MDA font */
uint8_t		fontdatw[512][32];		/* Wyse700 font */
uint8_t		fontdat8x12[256][16];		/* MDSI Genius font */
uint32_t	pal_lookup[256];
int		xsize = 1,
		ysize = 1;
int		cga_palette = 0;
uint32_t	*video_6to8 = NULL,
		*video_15to32 = NULL,
		*video_16to32 = NULL;
int		egareads = 0,
		egawrites = 0,
		changeframecount = 2;
uint8_t		rotatevga[8][256];
int		frames = 0;
int		fullchange = 0;
uint8_t		edatlookup[4][4];
int		overscan_x = 0,
		overscan_y = 0;
int		video_timing_read_b = 0,
		video_timing_read_w = 0,
		video_timing_read_l = 0;
int		video_timing_write_b = 0,
		video_timing_write_w = 0,
		video_timing_write_l = 0;
int		video_res_x = 0,
		video_res_y = 0,
		video_bpp = 0;
int		video_timing[6][4] = {
    { VIDEO_ISA, 8, 16, 32	},
    { VIDEO_ISA, 6,  8, 16	},
    { VIDEO_ISA, 3,  3,  6	},
    { VIDEO_BUS, 4,  8, 16	},
    { VIDEO_BUS, 4,  5, 10	},
    { VIDEO_BUS, 3,  3,  4	}
};
static int	video_force_resize;
PALETTE		cgapal = {
    {0,0,0},    {0,42,0},   {42,0,0},   {42,21,0},
    {0,0,0},    {0,42,42},  {42,0,42},  {42,42,42},
    {0,0,0},    {21,63,21}, {63,21,21},  {63,63,21},
    {0,0,0},    {21,63,63}, {63,21,63}, {63,63,63},

    {0,0,0},    {0,0,42},   {0,42,0},   {0,42,42},
    {42,0,0},   {42,0,42},  {42,21,00}, {42,42,42},
    {21,21,21}, {21,21,63}, {21,63,21}, {21,63,63},
    {63,21,21}, {63,21,63}, {63,63,21}, {63,63,63},

    {0,0,0},    {0,21,0},   {0,0,42},   {0,42,42},
    {42,0,21},  {21,10,21}, {42,0,42},  {42,0,63},
    {21,21,21}, {21,63,21}, {42,21,42}, {21,63,63},
    {63,0,0},   {42,42,0},  {63,21,42}, {41,41,41},
        
    {0,0,0},   {0,42,42},   {42,0,0},   {42,42,42},
    {0,0,0},   {0,42,42},   {42,0,0},   {42,42,42},
    {0,0,0},   {0,63,63},   {63,0,0},   {63,63,63},
    {0,0,0},   {0,63,63},   {63,0,0},   {63,63,63},
};
PALETTE		cgapal_mono[6] = {
    {	/* 0 - green, 4-color-optimized contrast. */
	{0x00,0x00,0x00},{0x00,0x0d,0x03},{0x01,0x17,0x05},
	{0x01,0x1a,0x06},{0x02,0x28,0x09},{0x02,0x2c,0x0a},
	{0x03,0x39,0x0d},{0x03,0x3c,0x0e},{0x00,0x07,0x01},
	{0x01,0x13,0x04},{0x01,0x1f,0x07},{0x01,0x23,0x08},
	{0x02,0x31,0x0b},{0x02,0x35,0x0c},{0x05,0x3f,0x11},{0x0d,0x3f,0x17},
    },
    {	/* 1 - green, 16-color-optimized contrast. */
	{0x00,0x00,0x00},{0x00,0x0d,0x03},{0x01,0x15,0x05},
	{0x01,0x17,0x05},{0x01,0x21,0x08},{0x01,0x24,0x08},
	{0x02,0x2e,0x0b},{0x02,0x31,0x0b},{0x01,0x22,0x08},
	{0x02,0x28,0x09},{0x02,0x30,0x0b},{0x02,0x32,0x0c},
	{0x03,0x39,0x0d},{0x03,0x3b,0x0e},{0x09,0x3f,0x14},{0x0d,0x3f,0x17},
    },
    {	/* 2 - amber, 4-color-optimized contrast. */
	{0x00,0x00,0x00},{0x15,0x05,0x00},{0x20,0x0b,0x00},
	{0x24,0x0d,0x00},{0x33,0x18,0x00},{0x37,0x1b,0x00},
	{0x3f,0x26,0x01},{0x3f,0x2b,0x06},{0x0b,0x02,0x00},
	{0x1b,0x08,0x00},{0x29,0x11,0x00},{0x2e,0x14,0x00},
	{0x3b,0x1e,0x00},{0x3e,0x21,0x00},{0x3f,0x32,0x0a},{0x3f,0x38,0x0d},
    },
    {	/* 3 - amber, 16-color-optimized contrast. */
	{0x00,0x00,0x00},{0x15,0x05,0x00},{0x1e,0x09,0x00},
	{0x21,0x0b,0x00},{0x2b,0x12,0x00},{0x2f,0x15,0x00},
	{0x38,0x1c,0x00},{0x3b,0x1e,0x00},{0x2c,0x13,0x00},
	{0x32,0x17,0x00},{0x3a,0x1e,0x00},{0x3c,0x1f,0x00},
	{0x3f,0x27,0x01},{0x3f,0x2a,0x04},{0x3f,0x36,0x0c},{0x3f,0x38,0x0d},
    },
    {	/* 4 - grey, 4-color-optimized contrast. */
	{0x00,0x00,0x00},{0x0e,0x0f,0x10},{0x15,0x17,0x18},
	{0x18,0x1a,0x1b},{0x24,0x25,0x25},{0x27,0x28,0x28},
	{0x33,0x34,0x32},{0x37,0x38,0x35},{0x09,0x0a,0x0b},
	{0x11,0x12,0x13},{0x1c,0x1e,0x1e},{0x20,0x22,0x22},
	{0x2c,0x2d,0x2c},{0x2f,0x30,0x2f},{0x3c,0x3c,0x38},{0x3f,0x3f,0x3b},
    },
    {	/* 5 - grey, 16-color-optimized contrast. */
	{0x00,0x00,0x00},{0x0e,0x0f,0x10},{0x13,0x14,0x15},
	{0x15,0x17,0x18},{0x1e,0x20,0x20},{0x20,0x22,0x22},
	{0x29,0x2a,0x2a},{0x2c,0x2d,0x2c},{0x1f,0x21,0x21},
	{0x23,0x25,0x25},{0x2b,0x2c,0x2b},{0x2d,0x2e,0x2d},
	{0x34,0x35,0x33},{0x37,0x37,0x34},{0x3e,0x3e,0x3a},{0x3f,0x3f,0x3b},
    }
};


static struct {
    int		x, y, y1, y2, w, h;
    int		busy;
    int		buffer_in_use;

    thread_t	*blit_thread;
    event_t	*wake_blit_thread;
    event_t	*blit_complete;
    event_t	*buffer_not_in_use;
}		blit_data;


static void (*blit_func)(int x, int y, int y1, int y2, int w, int h);


static
void blit_thread(void *param)
{
    while (1) {
	thread_wait_event(blit_data.wake_blit_thread, -1);
	thread_reset_event(blit_data.wake_blit_thread);

	if (blit_func)
		blit_func(blit_data.x, blit_data.y,
			  blit_data.y1, blit_data.y2,
			  blit_data.w, blit_data.h);

	blit_data.busy = 0;
	thread_set_event(blit_data.blit_complete);
    }
}


void
video_setblit(void(*blit)(int,int,int,int,int,int))
{
    blit_func = blit;
}


void
video_blit_complete(void)
{
    blit_data.buffer_in_use = 0;

    thread_set_event(blit_data.buffer_not_in_use);
}


void
video_wait_for_blit(void)
{
    while (blit_data.busy)
	thread_wait_event(blit_data.blit_complete, -1);
    thread_reset_event(blit_data.blit_complete);
}


void
video_wait_for_buffer(void)
{
    while (blit_data.buffer_in_use)
	thread_wait_event(blit_data.buffer_not_in_use, -1);
    thread_reset_event(blit_data.buffer_not_in_use);
}


void
video_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
    if (h <= 0) return;

    video_wait_for_blit();

    blit_data.busy = 1;
    blit_data.buffer_in_use = 1;
    blit_data.x = x;
    blit_data.y = y;
    blit_data.y1 = y1;
    blit_data.y2 = y2;
    blit_data.w = w;
    blit_data.h = h;

    thread_set_event(blit_data.wake_blit_thread);
}


void
video_blit_memtoscreen_8(int x, int y, int y1, int y2, int w, int h)
{
    int yy, xx;

    if (h <= 0) return;

    for (yy = 0; yy < h; yy++)
    {
	if ((y + yy) >= 0 && (y + yy) < buffer->h)
	{
		for (xx = 0; xx < w; xx++)
			*(uint32_t *) &(buffer32->line[y + yy][(x + xx) << 2]) = pal_lookup[buffer->line[y + yy][x + xx]];
	}
    }

    video_blit_memtoscreen(x, y, y1, y2, w, h);
}


void
cgapal_rebuild(void)
{
    int c;

    /* We cannot do this (yet) if we have not been enabled yet. */
    if (video_6to8 == NULL) return;

    for (c=0; c<256; c++) {
	pal_lookup[c] = makecol(video_6to8[cgapal[c].r],
			        video_6to8[cgapal[c].g],
			        video_6to8[cgapal[c].b]);
    }

    if ((cga_palette > 1) && (cga_palette < 8)) {
	if (vid_cga_contrast != 0) {
		for (c=0; c<16; c++) {
			pal_lookup[c] = makecol(video_6to8[cgapal_mono[cga_palette - 2][c].r],
						video_6to8[cgapal_mono[cga_palette - 2][c].g],
						video_6to8[cgapal_mono[cga_palette - 2][c].b]);
			pal_lookup[c+16] = makecol(video_6to8[cgapal_mono[cga_palette - 2][c].r],
						   video_6to8[cgapal_mono[cga_palette - 2][c].g],
						   video_6to8[cgapal_mono[cga_palette - 2][c].b]);
			pal_lookup[c+32] = makecol(video_6to8[cgapal_mono[cga_palette - 2][c].r],
						   video_6to8[cgapal_mono[cga_palette - 2][c].g],
						   video_6to8[cgapal_mono[cga_palette - 2][c].b]);
			pal_lookup[c+48] = makecol(video_6to8[cgapal_mono[cga_palette - 2][c].r],
						   video_6to8[cgapal_mono[cga_palette - 2][c].g],
						   video_6to8[cgapal_mono[cga_palette - 2][c].b]);
		}
	} else {
		for (c=0; c<16; c++) {
			pal_lookup[c] = makecol(video_6to8[cgapal_mono[cga_palette - 1][c].r],
						video_6to8[cgapal_mono[cga_palette - 1][c].g],
						video_6to8[cgapal_mono[cga_palette - 1][c].b]);
			pal_lookup[c+16] = makecol(video_6to8[cgapal_mono[cga_palette - 1][c].r],
						   video_6to8[cgapal_mono[cga_palette - 1][c].g],
						   video_6to8[cgapal_mono[cga_palette - 1][c].b]);
			pal_lookup[c+32] = makecol(video_6to8[cgapal_mono[cga_palette - 1][c].r],
						   video_6to8[cgapal_mono[cga_palette - 1][c].g],
						   video_6to8[cgapal_mono[cga_palette - 1][c].b]);
			pal_lookup[c+48] = makecol(video_6to8[cgapal_mono[cga_palette - 1][c].r],
						   video_6to8[cgapal_mono[cga_palette - 1][c].g],
						   video_6to8[cgapal_mono[cga_palette - 1][c].b]);
		}
	}
    }

    if (cga_palette == 8)
	pal_lookup[0x16] = makecol(video_6to8[42],video_6to8[42],video_6to8[0]);
}


static video_timings_t timing_dram     = {VIDEO_BUS, 0,0,0, 0,0,0}; /*No additional waitstates*/
static video_timings_t timing_pc1512   = {VIDEO_BUS, 0,0,0, 0,0,0}; /*PC1512 video code handles waitstates itself*/
static video_timings_t timing_pc1640   = {VIDEO_ISA, 8,16,32, 8,16,32};
static video_timings_t timing_pc200    = {VIDEO_ISA, 8,16,32, 8,16,32};
static video_timings_t timing_m24      = {VIDEO_ISA, 8,16,32, 8,16,32};
static video_timings_t timing_pvga1a   = {VIDEO_ISA, 6, 8,16, 6, 8,16};
static video_timings_t timing_wd90c11  = {VIDEO_ISA, 3, 3, 6, 5, 5,10};
static video_timings_t timing_vga      = {VIDEO_ISA, 8,16,32, 8,16,32};
static video_timings_t timing_ps1_svga = {VIDEO_ISA, 6, 8,16, 6, 8,16};
static video_timings_t timing_t3100e   = {VIDEO_ISA, 8,16,32, 8,16,32};

void
video_update_timing(void)
{
    video_timings_t *timing;
    int new_gfxcard;

    if (video_speed == -1) {
	new_gfxcard = 0;

	switch(romset) {
		case ROM_IBMPCJR:
		case ROM_TANDY:
		case ROM_TANDY1000HX:
		case ROM_TANDY1000SL2:
			timing = &timing_dram;
			break;
		case ROM_PC1512:
			timing = &timing_pc1512;
			break;
		case ROM_PC1640:
			timing = &timing_pc1640;
			break;
		case ROM_PC200:
			timing = &timing_pc200;
			break;
		case ROM_OLIM24:
			timing = &timing_m24;
			break;
		case ROM_PC2086:
		case ROM_PC3086:
			timing = &timing_pvga1a;
			break;
		case ROM_MEGAPC:
		case ROM_MEGAPCDX:
			timing = &timing_wd90c11;
			break;
		case ROM_IBMPS1_2011:
		case ROM_IBMPS2_M30_286:
		case ROM_IBMPS2_M50:
		case ROM_IBMPS2_M55SX:
		case ROM_IBMPS2_M80:
			timing = &timing_vga;
			break;
		case ROM_IBMPS1_2121:
		case ROM_IBMPS1_2133:
			timing = &timing_ps1_svga;
			break;
		case ROM_T3100E:
			timing = &timing_t3100e;
			break;
		default:
			new_gfxcard = video_old_to_new(gfxcard);
			timing = video_card_gettiming(new_gfxcard);
			break;
	}

	if (timing->type == VIDEO_ISA) {
		video_timing_read_b = ISA_CYCLES(timing->read_b);
		video_timing_read_w = ISA_CYCLES(timing->read_w);
		video_timing_read_l = ISA_CYCLES(timing->read_l);
		video_timing_write_b = ISA_CYCLES(timing->write_b);
		video_timing_write_w = ISA_CYCLES(timing->write_w);
		video_timing_write_l = ISA_CYCLES(timing->write_l);
	} else {
		video_timing_read_b = (int)(bus_timing * timing->read_b);
		video_timing_read_w = (int)(bus_timing * timing->read_w);
		video_timing_read_l = (int)(bus_timing * timing->read_l);
		video_timing_write_b = (int)(bus_timing * timing->write_b);
		video_timing_write_w = (int)(bus_timing * timing->write_w);
		video_timing_write_l = (int)(bus_timing * timing->write_l);
	}
    } else  {
	if (video_timing[video_speed][0] == VIDEO_ISA) {
		video_timing_read_b = ISA_CYCLES(video_timing[video_speed][1]);
		video_timing_read_w = ISA_CYCLES(video_timing[video_speed][2]);
		video_timing_read_l = ISA_CYCLES(video_timing[video_speed][3]);
		video_timing_write_b = ISA_CYCLES(video_timing[video_speed][1]);
		video_timing_write_w = ISA_CYCLES(video_timing[video_speed][2]);
		video_timing_write_l = ISA_CYCLES(video_timing[video_speed][3]);
	} else {
		video_timing_read_b = (int)(bus_timing * video_timing[video_speed][1]);
		video_timing_read_w = (int)(bus_timing * video_timing[video_speed][2]);
		video_timing_read_l = (int)(bus_timing * video_timing[video_speed][3]);
		video_timing_write_b = (int)(bus_timing * video_timing[video_speed][1]);
		video_timing_write_w = (int)(bus_timing * video_timing[video_speed][2]);
		video_timing_write_l = (int)(bus_timing * video_timing[video_speed][3]);
	}
    }

    if (cpu_16bitbus) {
	video_timing_read_l = video_timing_read_w * 2;
	video_timing_write_l = video_timing_write_w * 2;
    }
}


int
calc_6to8(int c)
{
    int ic, i8;
    double d8;

    ic = c;
    if (ic == 64)
	ic = 63;
      else
	ic &= 0x3f;
    d8 = (ic / 63.0) * 255.0;
    i8 = (int) d8;

    return(i8 & 0xff);
}


int
calc_15to32(int c)
{
    int b, g, r;
    double db, dg, dr;

    b = (c & 31);
    g = ((c >> 5) & 31);
    r = ((c >> 10) & 31);
    db = (((double) b) / 31.0) * 255.0;
    dg = (((double) g) / 31.0) * 255.0;
    dr = (((double) r) / 31.0) * 255.0;
    b = (int) db;
    g = ((int) dg) << 8;
    r = ((int) dr) << 16;

    return(b | g | r);
}


int
calc_16to32(int c)
{
    int b, g, r;
    double db, dg, dr;

    b = (c & 31);
    g = ((c >> 5) & 63);
    r = ((c >> 11) & 31);
    db = (((double) b) / 31.0) * 255.0;
    dg = (((double) g) / 63.0) * 255.0;
    dr = (((double) r) / 31.0) * 255.0;
    b = (int) db;
    g = ((int) dg) << 8;
    r = ((int) dr) << 16;

    return(b | g | r);
}


void
hline(bitmap_t *b, int x1, int y, int x2, uint32_t col)
{
    if (y < 0 || y >= buffer->h)
	   return;

    if (b == buffer)
	memset(&b->line[y][x1], col, x2 - x1);
      else
	memset(&((uint32_t *)b->line[y])[x1], col, (x2 - x1) * 4);
}


void
blit(bitmap_t *src, bitmap_t *dst, int x1, int y1, int x2, int y2, int xs, int ys)
{
}


void
stretch_blit(bitmap_t *src, bitmap_t *dst, int x1, int y1, int xs1, int ys1, int x2, int y2, int xs2, int ys2)
{
}


void
rectfill(bitmap_t *b, int x1, int y1, int x2, int y2, uint32_t col)
{
}


void
set_palette(PALETTE p)
{
}


void
destroy_bitmap(bitmap_t *b)
{
    if (b->dat != NULL)
	free(b->dat);
    free(b);
}


bitmap_t *
create_bitmap(int x, int y)
{
    bitmap_t *b = malloc(sizeof(bitmap_t) + (y * sizeof(uint8_t *)));
    int c;

    b->dat = malloc(x * y * 4);
    for (c = 0; c < y; c++)
	b->line[c] = b->dat + (c * x * 4);
    b->w = x;
    b->h = y;

    return(b);
}


void
video_init(void)
{
    int c, d, e;

    /* Account for overscan. */
    buffer32 = create_bitmap(2048, 2048);

    buffer = create_bitmap(2048, 2048);
    for (c = 0; c < 64; c++) {
	cgapal[c + 64].r = (((c & 4) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
	cgapal[c + 64].g = (((c & 2) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
	cgapal[c + 64].b = (((c & 1) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
	if ((c & 0x17) == 6) 
		cgapal[c + 64].g >>= 1;
    }
    for (c = 0; c < 64; c++) {
	cgapal[c + 128].r = (((c & 4) ? 2 : 0) | ((c & 0x20) ? 1 : 0)) * 21;
	cgapal[c + 128].g = (((c & 2) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
	cgapal[c + 128].b = (((c & 1) ? 2 : 0) | ((c & 0x08) ? 1 : 0)) * 21;
    }

    for (c = 0; c < 256; c++) {
	e = c;
	for (d = 0; d < 8; d++) {
		rotatevga[d][c] = e;
		e = (e >> 1) | ((e & 1) ? 0x80 : 0);
	}
    }
    for (c = 0; c < 4; c++) {
	for (d = 0; d < 4; d++) {
		edatlookup[c][d] = 0;
		if (c & 1) edatlookup[c][d] |= 1;
		if (d & 1) edatlookup[c][d] |= 2;
		if (c & 2) edatlookup[c][d] |= 0x10;
		if (d & 2) edatlookup[c][d] |= 0x20;
	}
    }

    video_6to8 = malloc(4 * 256);
    for (c = 0; c < 256; c++)
	video_6to8[c] = calc_6to8(c);
    video_15to32 = malloc(4 * 65536);
#if 0
    for (c = 0; c < 65536; c++)
	video_15to32[c] = ((c & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 10) & 31) << 19);
#endif
    for (c = 0; c < 65536; c++)
	video_15to32[c] = calc_15to32(c);

    video_16to32 = malloc(4 * 65536);
#if 0
    for (c = 0; c < 65536; c++)
	video_16to32[c] = ((c & 31) << 3) | (((c >> 5) & 63) << 10) | (((c >> 11) & 31) << 19);
#endif
    for (c = 0; c < 65536; c++)
	video_16to32[c] = calc_16to32(c);

    blit_data.wake_blit_thread = thread_create_event();
    blit_data.blit_complete = thread_create_event();
    blit_data.buffer_not_in_use = thread_create_event();
    blit_data.blit_thread = thread_create(blit_thread, NULL);
}


void
video_close(void)
{
    thread_kill(blit_data.blit_thread);
    thread_destroy_event(blit_data.buffer_not_in_use);
    thread_destroy_event(blit_data.blit_complete);
    thread_destroy_event(blit_data.wake_blit_thread);

    free(video_6to8);
    free(video_15to32);
    free(video_16to32);

    destroy_bitmap(buffer);
    destroy_bitmap(buffer32);
}


uint8_t
video_force_resize_get(void)
{
    return video_force_resize;
}


void
video_force_resize_set(uint8_t res)
{
    video_force_resize = res;
}


void
loadfont(wchar_t *s, int format)
{
    FILE *f;
    int c,d;

    f = rom_fopen(s, L"rb");
    if (f == NULL) {
	pclog("VIDEO: cannot load font '%ls', fmt=%d\n", s, format);
	return;
    }

    switch (format) {
	case 0:		/* MDA */
		for (c=0; c<256; c++)
			for (d=0; d<8; d++)
				fontdatm[c][d] = fgetc(f);
		for (c=0; c<256; c++)
			for (d=0; d<8; d++)
				fontdatm[c][d+8] = fgetc(f);
		(void)fseek(f, 4096+2048, SEEK_SET);
		for (c=0; c<256; c++)
			for (d=0; d<8; d++)
				fontdat[c][d] = fgetc(f);
		break;

	case 1:		/* PC200 */
		for (c=0; c<256; c++)
			for (d=0; d<8; d++)
				fontdatm[c][d] = fgetc(f);
		for (c=0; c<256; c++)
		       	for (d=0; d<8; d++)
				fontdatm[c][d+8] = fgetc(f);
		(void)fseek(f, 4096, SEEK_SET);
		for (c=0; c<256; c++) {
			for (d=0; d<8; d++)
				fontdat[c][d] = fgetc(f);
			for (d=0; d<8; d++) (void)fgetc(f);		
		}
		break;

	default:
	case 2:		/* CGA */
		for (c=0; c<256; c++)
		       	for (d=0; d<8; d++)
				fontdat[c][d] = fgetc(f);
		break;

	case 3:		/* Wyse 700 */
		for (c=0; c<512; c++)
			for (d=0; d<32; d++)
				fontdatw[c][d] = fgetc(f);
		break;

	case 4:		/* MDSI Genius */
		for (c=0; c<256; c++)
			for (d=0; d<16; d++)
				fontdat8x12[c][d] = fgetc(f);
		break;

	case 5: /* Toshiba 3100e */
		for (d = 0; d < 2048; d += 512)	/* Four languages... */
		{
	                for (c = d; c < d+256; c++)
                	{
                       		fread(&fontdatm[c][8], 1, 8, f);
                	}
                	for (c = d+256; c < d+512; c++)
                	{
                        	fread(&fontdatm[c][8], 1, 8, f);
                	}
	                for (c = d; c < d+256; c++)
                	{
                        	fread(&fontdatm[c][0], 1, 8, f);
                	}
                	for (c = d+256; c < d+512; c++)
                	{
                        	fread(&fontdatm[c][0], 1, 8, f);
                	}
			fseek(f, 4096, SEEK_CUR);	/* Skip blank section */
	                for (c = d; c < d+256; c++)
                	{
                       		fread(&fontdat[c][0], 1, 8, f);
                	}
                	for (c = d+256; c < d+512; c++)
                	{
                        	fread(&fontdat[c][0], 1, 8, f);
                	}
		}
                break;
    }

    (void)fclose(f);
}
