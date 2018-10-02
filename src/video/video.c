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
 * Version:	@(#)video.c	1.0.27	2018/10/02
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
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../config.h"
#include "../timer.h"
#include "../plat.h"
#include "video.h"
#include "vid_svga.h"


bitmap_t	*screen = NULL,
		*buffer = NULL,
		*buffer32 = NULL;
uint8_t		fontdat[2048][8];		/* IBM CGA font */
uint8_t		fontdatm[2048][16];		/* IBM MDA font */
uint8_t		fontdatw[512][32];		/* Wyse700 font */
uint8_t		fontdat8x12[256][16];		/* MDSI Genius font */
dbcs_font_t	*fontdatksc5601 = NULL;		/* Korean KSC-5601 font */
dbcs_font_t	*fontdatksc5601_user = NULL;	/* Korean KSC-5601 user defined font */
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
static int	video_force_resize;
int		invert_display = 0;
int		video_grayscale = 0;
int		video_graytype = 0;
static int	vid_type;
static const video_timings_t	*vid_timings;


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


const uint32_t shade[5][256] =
{
	{0},	// RGB Color (unused)
	{0},	// RGB Grayscale (unused)
	{		// Amber monitor
		0x000000, 0x060000, 0x090000, 0x0d0000, 0x100000, 0x120100, 0x150100, 0x170100, 0x1a0100, 0x1c0100, 0x1e0200, 0x210200, 0x230200, 0x250300, 0x270300, 0x290300,
		0x2b0400, 0x2d0400, 0x2f0400, 0x300500, 0x320500, 0x340500, 0x360600, 0x380600, 0x390700, 0x3b0700, 0x3d0700, 0x3f0800, 0x400800, 0x420900, 0x440900, 0x450a00,
		0x470a00, 0x480b00, 0x4a0b00, 0x4c0c00, 0x4d0c00, 0x4f0d00, 0x500d00, 0x520e00, 0x530e00, 0x550f00, 0x560f00, 0x581000, 0x591000, 0x5b1100, 0x5c1200, 0x5e1200,
		0x5f1300, 0x601300, 0x621400, 0x631500, 0x651500, 0x661600, 0x671600, 0x691700, 0x6a1800, 0x6c1800, 0x6d1900, 0x6e1a00, 0x701a00, 0x711b00, 0x721c00, 0x741c00,
		0x751d00, 0x761e00, 0x781e00, 0x791f00, 0x7a2000, 0x7c2000, 0x7d2100, 0x7e2200, 0x7f2300, 0x812300, 0x822400, 0x832500, 0x842600, 0x862600, 0x872700, 0x882800,
		0x8a2900, 0x8b2900, 0x8c2a00, 0x8d2b00, 0x8e2c00, 0x902c00, 0x912d00, 0x922e00, 0x932f00, 0x953000, 0x963000, 0x973100, 0x983200, 0x993300, 0x9b3400, 0x9c3400,
		0x9d3500, 0x9e3600, 0x9f3700, 0xa03800, 0xa23900, 0xa33a00, 0xa43a00, 0xa53b00, 0xa63c00, 0xa73d00, 0xa93e00, 0xaa3f00, 0xab4000, 0xac4000, 0xad4100, 0xae4200,
		0xaf4300, 0xb14400, 0xb24500, 0xb34600, 0xb44700, 0xb54800, 0xb64900, 0xb74a00, 0xb94a00, 0xba4b00, 0xbb4c00, 0xbc4d00, 0xbd4e00, 0xbe4f00, 0xbf5000, 0xc05100,
		0xc15200, 0xc25300, 0xc45400, 0xc55500, 0xc65600, 0xc75700, 0xc85800, 0xc95900, 0xca5a00, 0xcb5b00, 0xcc5c00, 0xcd5d00, 0xce5e00, 0xcf5f00, 0xd06000, 0xd26101,
		0xd36201, 0xd46301, 0xd56401, 0xd66501, 0xd76601, 0xd86701, 0xd96801, 0xda6901, 0xdb6a01, 0xdc6b01, 0xdd6c01, 0xde6d01, 0xdf6e01, 0xe06f01, 0xe17001, 0xe27201,
		0xe37301, 0xe47401, 0xe57501, 0xe67602, 0xe77702, 0xe87802, 0xe97902, 0xeb7a02, 0xec7b02, 0xed7c02, 0xee7e02, 0xef7f02, 0xf08002, 0xf18103, 0xf28203, 0xf38303,
		0xf48403, 0xf58503, 0xf68703, 0xf78803, 0xf88903, 0xf98a04, 0xfa8b04, 0xfb8c04, 0xfc8d04, 0xfd8f04, 0xfe9005, 0xff9105, 0xff9205, 0xff9305, 0xff9405, 0xff9606,
		0xff9706, 0xff9806, 0xff9906, 0xff9a07, 0xff9b07, 0xff9d07, 0xff9e08, 0xff9f08, 0xffa008, 0xffa109, 0xffa309, 0xffa409, 0xffa50a, 0xffa60a, 0xffa80a, 0xffa90b,
		0xffaa0b, 0xffab0c, 0xffac0c, 0xffae0d, 0xffaf0d, 0xffb00e, 0xffb10e, 0xffb30f, 0xffb40f, 0xffb510, 0xffb610, 0xffb811, 0xffb912, 0xffba12, 0xffbb13, 0xffbd14,
		0xffbe14, 0xffbf15, 0xffc016, 0xffc217, 0xffc317, 0xffc418, 0xffc619, 0xffc71a, 0xffc81b, 0xffca1c, 0xffcb1d, 0xffcc1e, 0xffcd1f, 0xffcf20, 0xffd021, 0xffd122,
		0xffd323, 0xffd424, 0xffd526, 0xffd727, 0xffd828, 0xffd92a, 0xffdb2b, 0xffdc2c, 0xffdd2e, 0xffdf2f, 0xffe031, 0xffe133, 0xffe334, 0xffe436, 0xffe538, 0xffe739
	},
	{		// Green monitor
		0x000000, 0x000400, 0x000700, 0x000900, 0x000b00, 0x000d00, 0x000f00, 0x001100, 0x001300, 0x001500, 0x001600, 0x001800, 0x001a00, 0x001b00, 0x001d00, 0x001e00,
		0x002000, 0x002100, 0x002300, 0x002400, 0x002601, 0x002701, 0x002901, 0x002a01, 0x002b01, 0x002d01, 0x002e01, 0x002f01, 0x003101, 0x003201, 0x003301, 0x003401,
		0x003601, 0x003702, 0x003802, 0x003902, 0x003b02, 0x003c02, 0x003d02, 0x003e02, 0x004002, 0x004102, 0x004203, 0x004303, 0x004403, 0x004503, 0x004703, 0x004803,
		0x004903, 0x004a03, 0x004b04, 0x004c04, 0x004d04, 0x004e04, 0x005004, 0x005104, 0x005205, 0x005305, 0x005405, 0x005505, 0x005605, 0x005705, 0x005806, 0x005906,
		0x005a06, 0x005b06, 0x005d06, 0x005e07, 0x005f07, 0x006007, 0x006107, 0x006207, 0x006308, 0x006408, 0x006508, 0x006608, 0x006708, 0x006809, 0x006909, 0x006a09,
		0x006b09, 0x016c0a, 0x016d0a, 0x016e0a, 0x016f0a, 0x01700b, 0x01710b, 0x01720b, 0x01730b, 0x01740c, 0x01750c, 0x01760c, 0x01770c, 0x01780d, 0x01790d, 0x017a0d,
		0x017b0d, 0x017b0e, 0x017c0e, 0x017d0e, 0x017e0f, 0x017f0f, 0x01800f, 0x018110, 0x028210, 0x028310, 0x028410, 0x028511, 0x028611, 0x028711, 0x028812, 0x028912,
		0x028a12, 0x028a13, 0x028b13, 0x028c13, 0x028d14, 0x028e14, 0x038f14, 0x039015, 0x039115, 0x039215, 0x039316, 0x039416, 0x039417, 0x039517, 0x039617, 0x039718,
		0x049818, 0x049918, 0x049a19, 0x049b19, 0x049c19, 0x049c1a, 0x049d1a, 0x049e1b, 0x059f1b, 0x05a01b, 0x05a11c, 0x05a21c, 0x05a31c, 0x05a31d, 0x05a41d, 0x06a51e,
		0x06a61e, 0x06a71f, 0x06a81f, 0x06a920, 0x06aa20, 0x07aa21, 0x07ab21, 0x07ac21, 0x07ad22, 0x07ae22, 0x08af23, 0x08b023, 0x08b024, 0x08b124, 0x08b225, 0x09b325,
		0x09b426, 0x09b526, 0x09b527, 0x0ab627, 0x0ab728, 0x0ab828, 0x0ab929, 0x0bba29, 0x0bba2a, 0x0bbb2a, 0x0bbc2b, 0x0cbd2b, 0x0cbe2c, 0x0cbf2c, 0x0dbf2d, 0x0dc02d,
		0x0dc12e, 0x0ec22e, 0x0ec32f, 0x0ec42f, 0x0fc430, 0x0fc530, 0x0fc631, 0x10c731, 0x10c832, 0x10c932, 0x11c933, 0x11ca33, 0x11cb34, 0x12cc35, 0x12cd35, 0x12cd36,
		0x13ce36, 0x13cf37, 0x13d037, 0x14d138, 0x14d139, 0x14d239, 0x15d33a, 0x15d43a, 0x16d43b, 0x16d53b, 0x17d63c, 0x17d73d, 0x17d83d, 0x18d83e, 0x18d93e, 0x19da3f,
		0x19db40, 0x1adc40, 0x1adc41, 0x1bdd41, 0x1bde42, 0x1cdf43, 0x1ce043, 0x1de044, 0x1ee145, 0x1ee245, 0x1fe346, 0x1fe446, 0x20e447, 0x20e548, 0x21e648, 0x22e749,
		0x22e74a, 0x23e84a, 0x23e94b, 0x24ea4c, 0x25ea4c, 0x25eb4d, 0x26ec4e, 0x27ed4e, 0x27ee4f, 0x28ee50, 0x29ef50, 0x29f051, 0x2af152, 0x2bf153, 0x2cf253, 0x2cf354,
		0x2df455, 0x2ef455, 0x2ff556, 0x2ff657, 0x30f758, 0x31f758, 0x32f859, 0x32f95a, 0x33fa5a, 0x34fa5b, 0x35fb5c, 0x36fc5d, 0x37fd5d, 0x38fd5e, 0x38fe5f, 0x39ff60
	},
	{		// White monitor
		0x000000, 0x010102, 0x020203, 0x020304, 0x030406, 0x040507, 0x050608, 0x060709, 0x07080a, 0x08090c, 0x080a0d, 0x090b0e, 0x0a0c0f, 0x0b0d10, 0x0c0e11, 0x0d0f12,
		0x0e1013, 0x0f1115, 0x101216, 0x111317, 0x121418, 0x121519, 0x13161a, 0x14171b, 0x15181c, 0x16191d, 0x171a1e, 0x181b1f, 0x191c20, 0x1a1d21, 0x1b1e22, 0x1c1f23,
		0x1d2024, 0x1e2125, 0x1f2226, 0x202327, 0x212428, 0x222529, 0x22262b, 0x23272c, 0x24282d, 0x25292e, 0x262a2f, 0x272b30, 0x282c30, 0x292d31, 0x2a2e32, 0x2b2f33,
		0x2c3034, 0x2d3035, 0x2e3136, 0x2f3237, 0x303338, 0x313439, 0x32353a, 0x33363b, 0x34373c, 0x35383d, 0x36393e, 0x373a3f, 0x383b40, 0x393c41, 0x3a3d42, 0x3b3e43,
		0x3c3f44, 0x3d4045, 0x3e4146, 0x3f4247, 0x404348, 0x414449, 0x42454a, 0x43464b, 0x44474c, 0x45484d, 0x46494d, 0x474a4e, 0x484b4f, 0x484c50, 0x494d51, 0x4a4e52,
		0x4b4f53, 0x4c5054, 0x4d5155, 0x4e5256, 0x4f5357, 0x505458, 0x515559, 0x52565a, 0x53575b, 0x54585b, 0x55595c, 0x565a5d, 0x575b5e, 0x585c5f, 0x595d60, 0x5a5e61,
		0x5b5f62, 0x5c6063, 0x5d6164, 0x5e6265, 0x5f6366, 0x606466, 0x616567, 0x626668, 0x636769, 0x64686a, 0x65696b, 0x666a6c, 0x676b6d, 0x686c6e, 0x696d6f, 0x6a6e70,
		0x6b6f70, 0x6c7071, 0x6d7172, 0x6f7273, 0x707374, 0x707475, 0x717576, 0x727677, 0x747778, 0x757879, 0x767979, 0x777a7a, 0x787b7b, 0x797c7c, 0x7a7d7d, 0x7b7e7e,
		0x7c7f7f, 0x7d8080, 0x7e8181, 0x7f8281, 0x808382, 0x818483, 0x828584, 0x838685, 0x848786, 0x858887, 0x868988, 0x878a89, 0x888b89, 0x898c8a, 0x8a8d8b, 0x8b8e8c,
		0x8c8f8d, 0x8d8f8e, 0x8e908f, 0x8f9190, 0x909290, 0x919391, 0x929492, 0x939593, 0x949694, 0x959795, 0x969896, 0x979997, 0x989a98, 0x999b98, 0x9a9c99, 0x9b9d9a,
		0x9c9e9b, 0x9d9f9c, 0x9ea09d, 0x9fa19e, 0xa0a29f, 0xa1a39f, 0xa2a4a0, 0xa3a5a1, 0xa4a6a2, 0xa6a7a3, 0xa7a8a4, 0xa8a9a5, 0xa9aaa5, 0xaaaba6, 0xabaca7, 0xacada8,
		0xadaea9, 0xaeafaa, 0xafb0ab, 0xb0b1ac, 0xb1b2ac, 0xb2b3ad, 0xb3b4ae, 0xb4b5af, 0xb5b6b0, 0xb6b7b1, 0xb7b8b2, 0xb8b9b2, 0xb9bab3, 0xbabbb4, 0xbbbcb5, 0xbcbdb6,
		0xbdbeb7, 0xbebfb8, 0xbfc0b8, 0xc0c1b9, 0xc1c2ba, 0xc2c3bb, 0xc3c4bc, 0xc5c5bd, 0xc6c6be, 0xc7c7be, 0xc8c8bf, 0xc9c9c0, 0xcacac1, 0xcbcbc2, 0xccccc3, 0xcdcdc3,
		0xcecec4, 0xcfcfc5, 0xd0d0c6, 0xd1d1c7, 0xd2d2c8, 0xd3d3c9, 0xd4d4c9, 0xd5d5ca, 0xd6d6cb, 0xd7d7cc, 0xd8d8cd, 0xd9d9ce, 0xdadacf, 0xdbdbcf, 0xdcdcd0, 0xdeddd1,
		0xdfded2, 0xe0dfd3, 0xe1e0d4, 0xe2e1d4, 0xe3e2d5, 0xe4e3d6, 0xe5e4d7, 0xe6e5d8, 0xe7e6d9, 0xe8e7d9, 0xe9e8da, 0xeae9db, 0xebeadc, 0xecebdd, 0xedecde, 0xeeeddf,
		0xefeedf, 0xf0efe0, 0xf1f0e1, 0xf2f1e2, 0xf3f2e3, 0xf4f3e3, 0xf6f3e4, 0xf7f4e5, 0xf8f5e6, 0xf9f6e7, 0xfaf7e8, 0xfbf8e9, 0xfcf9e9, 0xfdfaea, 0xfefbeb, 0xfffcec
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


void
video_inform(int type, const video_timings_t *ptr)
{
    vid_type = type;
    vid_timings = ptr;
}


int
video_get_type(void)
{
    return vid_type;
}


void
video_update_timing(void)
{
    if (!vid_timings)
	return;

    if (vid_timings->type == VIDEO_ISA) {
	video_timing_read_b = ISA_CYCLES(vid_timings->read_b);
	video_timing_read_w = ISA_CYCLES(vid_timings->read_w);
	video_timing_read_l = ISA_CYCLES(vid_timings->read_l);
	video_timing_write_b = ISA_CYCLES(vid_timings->write_b);
	video_timing_write_w = ISA_CYCLES(vid_timings->write_w);
	video_timing_write_l = ISA_CYCLES(vid_timings->write_l);
    } else {
	video_timing_read_b = (int)(bus_timing * vid_timings->read_b);
	video_timing_read_w = (int)(bus_timing * vid_timings->read_w);
	video_timing_read_l = (int)(bus_timing * vid_timings->read_l);
	video_timing_write_b = (int)(bus_timing * vid_timings->write_b);
	video_timing_write_w = (int)(bus_timing * vid_timings->write_w);
	video_timing_write_l = (int)(bus_timing * vid_timings->write_l);
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

    if (fontdatksc5601) {
	free(fontdatksc5601);
	fontdatksc5601 = NULL;
    }

    if (fontdatksc5601_user) {
	free(fontdatksc5601_user);
	fontdatksc5601_user = NULL;
    }
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
    if (f == NULL)
	return;

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

	case 6: /* Korean KSC-5601 */
		if (!fontdatksc5601)
			fontdatksc5601 = malloc(16384 * sizeof(dbcs_font_t));

		if (!fontdatksc5601_user)
			fontdatksc5601_user = malloc(192 * sizeof(dbcs_font_t));

		for (c = 0; c < 16384; c++)
		{
			for (d = 0; d < 32; d++)
				fontdatksc5601[c].chr[d]=getc(f);
		}
		break;
    }

    (void)fclose(f);
}


uint32_t
video_color_transform(uint32_t color)
{
    uint8_t *clr8 = (uint8_t *) &color;
    /* if (!video_grayscale && !invert_display)
	return color; */
    if (video_grayscale) {
	if (video_graytype) {
		if (video_graytype == 1)
			color = ((54 * (uint32_t)clr8[2]) + (183 * (uint32_t)clr8[1]) + (18 * (uint32_t)clr8[0])) / 255;
		else
			color = ((uint32_t)clr8[2] + (uint32_t)clr8[1] + (uint32_t)clr8[0]) / 3;
	} else
		color = ((76 * (uint32_t)clr8[2]) + (150 * (uint32_t)clr8[1]) + (29 * (uint32_t)clr8[0])) / 255;
	switch (video_grayscale) {
		case 2: case 3: case 4:
			color = (uint32_t) shade[video_grayscale][color];
			break;
		default:
			clr8[3] = 0;
			clr8[0] = color;
			clr8[1] = clr8[2] = clr8[0];
			break;
	}
    }
    if (invert_display)
	color ^= 0x00ffffff;
    return color;
}

void
video_transform_copy(uint32_t *dst, uint32_t *src, int len)
{
    int i;

    for (i = 0; i < len; i++) {
	*dst = video_color_transform(*src);
	dst++;
	src++;
    }
}
