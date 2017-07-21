/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		SVGA renderers.
 *
 * Version:	@(#)vid_svga_render.c	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#include <stdio.h>
#include "../ibm.h"
#include "../mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"


int invert_display = 0;
int video_grayscale = 0;
int video_graytype = 0;

uint32_t shade[5][256] =
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

uint32_t svga_color_transform(uint32_t color)
{
	uint32_t temp = 0;
	if (video_grayscale != 0)
	{
		if (video_graytype)
		{
			if (video_graytype == 1)
				temp = ((54 * ((color & 0xff0000) >> 16)) + (183 * ((color & 0xff00) >> 8)) + (18 * (color & 0xff))) / 255;
			else
				temp = (((color & 0xff0000) >> 16) + ((color & 0xff00) >> 8) + (color & 0xff)) / 3;
		}
		else
			temp = ((76 * ((color & 0xff0000) >> 16)) + (150 * ((color & 0xff00) >> 8)) + (29 * (color & 0xff))) / 255;
		switch (video_grayscale)
		{
			case 2:
			case 3:
			case 4:
				color = shade[video_grayscale][temp];
				break;
			default:
				color = temp;
				color |= temp << 8;
				color |= temp << 16;
				break;
		}
	}
	if (invert_display)
	{
		temp = (0xff - (color & 0xff));
		temp |= (0xff00 - (color & 0xff00));
		temp |= (0xff0000 - (color & 0xff0000));
	}
	else
		return color;
	return temp;
}

int svga_display_line(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	unsigned int dl = svga->displine;
	if (svga->crtc[9] & 0x1f)
	{
		dl -= (svga->crtc[8] & 0x1f);
	}
	dl += y_add;
	dl &= 0x7ff;
	return dl;
}

void svga_render_blank(svga_t *svga)
{
#if 0
        int x, xx;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);
	uint32_t *p;
        
        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

	if (dl >= 2046)
	{
		return;
	}
        
        for (x = 0; x < svga->hdisp; x++)
        {
                switch (svga->seqregs[1] & 9)
                {
                        case 0:
                        for (xx = 0; xx < 9; xx++)
			{
				p = ((uint32_t *)buffer32->line[dl]);
				if (&(p[(x * 9) + xx + 32 + x_add]) != NULL)
				{
					p[(x * 9) + xx + 32 + x_add] = svga_color_transform(0);
				}
			}
                        break;

                        case 1:
                        for (xx = 0; xx < 8; xx++)
			{
				p = ((uint32_t *)buffer32->line[dl]);
				if (&(p[(x * 8) + xx + 32 + x_add]) != NULL)
				{
					p[(x * 8) + xx + 32 + x_add] = svga_color_transform(0);
				}
			}
                        break;

                        case 8:
                        for (xx = 0; xx < 18; xx++)
			{
				p = ((uint32_t *)buffer32->line[dl]);
				if (&(p[(x * 18) + xx + 32 + x_add]) != NULL)
				{
					p[(x * 18) + xx + 32 + x_add] = svga_color_transform(0);
				}
			}
                        break;

                        case 9:
                        for (xx = 0; xx < 16; xx++)
			{
				p = ((uint32_t *)buffer32->line[dl]);
				if (&(p[(x * 16) + xx + 32 + x_add]) != NULL)
				{
					p[(x * 16) + xx + 32 + x_add] = svga_color_transform(0);
				}
			}
                        break;
                }
        }
#endif
        int x, xx;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
        
        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        for (x = 0; x < svga->hdisp; x++)
        {
                switch (svga->seqregs[1] & 9)
                {
                        case 0:
                        for (xx = 0; xx < 9; xx++) ((uint32_t *)buffer32->line[svga->displine + y_add])[(x * 9) + xx + 32 + x_add] = svga_color_transform(0);
                        break;
                        case 1:
                        for (xx = 0; xx < 8; xx++) ((uint32_t *)buffer32->line[svga->displine + y_add])[(x * 8) + xx + 32 + x_add] = svga_color_transform(0);
                        break;
                        case 8:
                        for (xx = 0; xx < 18; xx++) ((uint32_t *)buffer32->line[svga->displine + y_add])[(x * 18) + xx + 32 + x_add] = svga_color_transform(0);
                        break;
                        case 9:
                        for (xx = 0; xx < 16; xx++) ((uint32_t *)buffer32->line[svga->displine + y_add])[(x * 16) + xx + 32 + x_add] = svga_color_transform(0);
                        break;
                }
        }
}

void svga_render_text_40(svga_t *svga)
{     
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        if (svga->fullchange)
        {
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[32 + x_add];
                int x, xx;
                int drawcursor;
                uint8_t chr, attr, dat;
                uint32_t charaddr;
                int fg, bg;
                int xinc = (svga->seqregs[1] & 1) ? 16 : 18;
                
                for (x = 0; x < svga->hdisp; x += xinc)
                {
                        drawcursor = ((svga->ma == svga->ca) && svga->con && svga->cursoron);
			chr = svga->vram[svga->ma << 1];
			attr = svga->vram[(svga->ma << 1) + 1];
                        if (attr & 8) charaddr = svga->charsetb + (chr * 128);
                        else          charaddr = svga->charseta + (chr * 128);

                        if (drawcursor) 
                        { 
                                bg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask]; 
                                fg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask]; 
                        }
                        else
                        {
                                fg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                                bg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
                                if (attr & 0x80 && svga->attrregs[0x10] & 8)
                                {
                                        bg = svga->pallook[svga->egapal[(attr >> 4) & 7] & svga->dac_mask];
                                        if (svga->blink & 16) 
                                                fg = bg;
                                }
                        }

                        dat = svga->vram[charaddr + (svga->sc << 2)];
                        if (svga->seqregs[1] & 1) 
                        { 
                                for (xx = 0; xx < 16; xx += 2) 
                                        p[xx] = p[xx + 1] = (dat & (0x80 >> (xx >> 1))) ? svga_color_transform(fg) : svga_color_transform(bg);
                        }
                        else
                        {
                                for (xx = 0; xx < 16; xx += 2)
                                        p[xx] = p[xx + 1] = (dat & (0x80 >> (xx >> 1))) ? svga_color_transform(fg) : svga_color_transform(bg);
                                if ((chr & ~0x1F) != 0xC0 || !(svga->attrregs[0x10] & 4)) 
                                        p[16] = p[17] = svga_color_transform(bg);
                                else                  
                                        p[16] = p[17] = (dat & 1) ? svga_color_transform(fg) : svga_color_transform(bg);
                        }
                        svga->ma += 4; 
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += xinc;
                }
        }
}

#if 0
void svga_render_text_40_12(svga_t *svga)
{     
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = (enable_overscan) ? 12 : 0;
	int dl = svga_display_line(svga);

        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        if (svga->fullchange)
        {
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[32 + x_add];
                int x, xx;
                int drawcursor;
                uint8_t chr, attr;
		uint16_t dat;
                uint32_t charaddr;
                int fg, bg;
                int xinc = 24;
                
                for (x = 0; x < svga->hdisp; x += xinc)
                {
                        drawcursor = ((svga->ma == svga->ca) && svga->con && svga->cursoron);
			chr = svga->vram[svga->ma << 1];
			attr = svga->vram[(svga->ma << 1) + 1];
                        if (attr & 8) charaddr = svga->charsetb + (chr * 128);
                        else          charaddr = svga->charseta + (chr * 128);

                        if (drawcursor) 
                        { 
                                bg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask]; 
                                fg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask]; 
                        }
                        else
                        {
                                fg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                                bg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
                                if (attr & 0x80 && svga->attrregs[0x10] & 8)
                                {
                                        bg = svga->pallook[svga->egapal[(attr >> 4) & 7] & svga->dac_mask];
                                        if (svga->blink & 16) 
                                                fg = bg;
                                }
                        }

                        dat = *(uint16_t *) &(svga->vram[charaddr + (svga->sc << 2) - 1]);
			for (xx = 0; xx < 24; xx += 2) 
				p[xx] = p[xx + 1] = (dat & (0x800 >> (xx >> 1))) ? svga_color_transform(fg) : svga_color_transform(bg);
                        svga->ma += 4; 
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += xinc;
                }
        }
}
#endif

void svga_render_text_80(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        if (svga->fullchange)
        {
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[32 + x_add];
                int x, xx;
                int drawcursor;
                uint8_t chr, attr, dat;
                uint32_t charaddr;
                int fg, bg;
                int xinc = (svga->seqregs[1] & 1) ? 8 : 9;

                for (x = 0; x < svga->hdisp; x += xinc)
                {
                        drawcursor = ((svga->ma == svga->ca) && svga->con && svga->cursoron);
			chr = svga->vram[svga->ma << 1];
			attr = svga->vram[(svga->ma << 1) + 1];
                        if (attr & 8) charaddr = svga->charsetb + (chr * 128);
                        else          charaddr = svga->charseta + (chr * 128);

                        if (drawcursor) 
                        { 
                                bg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask]; 
                                fg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask]; 
                        }
                        else
                        {
                                fg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                                bg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
                                if (attr & 0x80 && svga->attrregs[0x10] & 8)
                                {
                                        bg = svga->pallook[svga->egapal[(attr >> 4) & 7] & svga->dac_mask];
                                        if (svga->blink & 16) 
                                                fg = bg;
                                }
                        }

                        dat = svga->vram[charaddr + (svga->sc << 2)];
                        if (svga->seqregs[1] & 1) 
                        { 
                                for (xx = 0; xx < 8; xx++) 
                                        p[xx] = (dat & (0x80 >> xx)) ? svga_color_transform(fg) : svga_color_transform(bg);
                        }
                        else
                        {
                                for (xx = 0; xx < 8; xx++) 
                                        p[xx] = (dat & (0x80 >> xx)) ? svga_color_transform(fg) : svga_color_transform(bg);
                                if ((chr & ~0x1F) != 0xC0 || !(svga->attrregs[0x10] & 4)) 
                                        p[8] = svga_color_transform(bg);
                                else                  
                                        p[8] = (dat & 1) ? svga_color_transform(fg) : svga_color_transform(bg);
                        }
                        svga->ma += 4; 
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += xinc;
                }
        }
}

#if 0
void svga_render_text_80_12(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = (enable_overscan) ? 12 : 0;
	int dl = svga_display_line(svga);

        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        if (svga->fullchange)
        {
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[32 + x_add];
                int x, xx;
                int drawcursor;
                uint8_t chr, attr;
		uint16_t dat;
                uint32_t charaddr;
                int fg, bg;
                int xinc = 12;

                for (x = 0; x < svga->hdisp; x += xinc)
                {
                        drawcursor = ((svga->ma == (svga->ca + 8)) && svga->con && svga->cursoron);
			chr = svga->vram[svga->ma << 1];
			attr = svga->vram[(svga->ma << 1) + 1];
                        if (attr & 8) charaddr = svga->charsetb + (chr * 128);
                        else          charaddr = svga->charseta + (chr * 128);

                        if (drawcursor) 
                        { 
                                bg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask]; 
                                fg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask]; 
                        }
                        else
                        {
                                fg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                                bg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
                                if (attr & 0x80 && svga->attrregs[0x10] & 8)
                                {
                                        bg = svga->pallook[svga->egapal[(attr >> 4) & 7] & svga->dac_mask];
                                        if (svga->blink & 16) 
                                                fg = bg;
                                }
                        }

                        dat = svga->vram[charaddr + (svga->sc << 2)] << 4;
                        dat |= ((svga->vram[charaddr + (svga->sc << 2) + 1]) >> 4);
			for (xx = 0; xx < 12; xx++) 
				p[xx] = (dat & (0x800 >> xx)) ? svga_color_transform(fg) : svga_color_transform(bg);
                        svga->ma += 4; 
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += xinc;
                }
        }
}
#endif

void svga_render_2bpp_lowres(svga_t *svga)
{
        int changed_offset;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);
        
        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                changed_offset = (svga->ma << 1) >> 12;
        else
                changed_offset = ((svga->ma << 1) + 0x8000) >> 12;
                
        if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset + 1] || svga->fullchange)
        {
                int x;
                int offset = ((8 - svga->scrollcache) << 1) + 16;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
                       
                for (x = 0; x <= svga->hdisp; x += 16)
                {
                        uint8_t dat[2];
                        
                        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                        {
                                dat[0] = svga->vram[(svga->ma << 1) + 0x8000];
                                dat[1] = svga->vram[(svga->ma << 1) + 0x8001];
                        }
                        else
                        {
                                dat[0] = svga->vram[(svga->ma << 1)];
                                dat[1] = svga->vram[(svga->ma << 1) + 1];
                        }
                        svga->ma += 4; 
                        svga->ma = svga_mask_addr(svga->ma, svga);

                        p[0]  = p[1]  = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 6) & 3] & svga->dac_mask]);
                        p[2]  = p[3]  = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 4) & 3] & svga->dac_mask]);
                        p[4]  = p[5]  = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 2) & 3] & svga->dac_mask]);
                        p[6]  = p[7]  = svga_color_transform(svga->pallook[svga->egapal[dat[0] & 3] & svga->dac_mask]);
                        p[8]  = p[9]  = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 6) & 3] & svga->dac_mask]);
                        p[10] = p[11] = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 4) & 3] & svga->dac_mask]);
                        p[12] = p[13] = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 2) & 3] & svga->dac_mask]);
                        p[14] = p[15] = svga_color_transform(svga->pallook[svga->egapal[dat[1] & 3] & svga->dac_mask]);

                        p += 16;
                }
        }
}

void svga_render_2bpp_highres(svga_t *svga)
{
        int changed_offset;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                changed_offset = ((svga->ma << 1) | 0x8000) >> 12;
        else
                changed_offset = (svga->ma << 1) >> 12;
                                
        if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset + 1] || svga->fullchange)
        {
                int x;
                int offset = (8 - svga->scrollcache) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
                       
                for (x = 0; x <= svga->hdisp; x += 8)
                {
                        uint8_t dat[2];
                        
                        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                        {
                                dat[0] = svga->vram[(svga->ma << 1) + 0x8000];
                                dat[1] = svga->vram[(svga->ma << 1) + 0x8001];
                        }
                        else
                        {
                                dat[0] = svga->vram[(svga->ma << 1)];
                                dat[1] = svga->vram[(svga->ma << 1) + 1];
                        }
                        svga->ma += 4; 
                        svga->ma = svga_mask_addr(svga->ma, svga);

                        p[0] = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 6) & 3] & svga->dac_mask]);
                        p[1] = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 4) & 3] & svga->dac_mask]);
                        p[2] = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 2) & 3] & svga->dac_mask]);
                        p[3] = svga_color_transform(svga->pallook[svga->egapal[dat[0] & 3] & svga->dac_mask]);
                        p[4] = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 6) & 3] & svga->dac_mask]);
                        p[5] = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 4) & 3] & svga->dac_mask]);
                        p[6] = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 2) & 3] & svga->dac_mask]);
                        p[7] = svga_color_transform(svga->pallook[svga->egapal[dat[1] & 3] & svga->dac_mask]);
                        
                        p += 8;
                }
        }
}

void svga_render_4bpp_lowres(svga_t *svga)
{
	int x;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);
	int offset;
	uint32_t *p;
	uint32_t *r, *q;
	uint8_t edat[4];
	uint8_t dat;

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange)
        {
                offset = ((8 - svga->scrollcache) << 1) + 16;
                p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x += 16)
                {
			r = (uint32_t *)(&edat[0]);
			q = (uint32_t *)(&svga->vram[svga->ma]);
			*r = *q;
                        svga->ma += 4; 
                        svga->ma = svga_mask_addr(svga->ma, svga);

                        dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
                        p[0]  = p[1]  = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[2]  = p[3]  = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
                        p[4]  = p[5]  = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[6]  = p[7]  = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
                        p[8]  = p[9]  = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[10] = p[11] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
                        p[12] = p[13] = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[14] = p[15] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                                                
                        p += 16;
                }
        }
}

void svga_render_4bpp_highres(svga_t *svga)
{
        int changed_offset;
	int changed_offset2;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);
	int x;
	int offset;
	uint32_t *p;
	uint8_t edat[4];
	uint8_t dat;
	uint32_t *r, *q;
        
        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                changed_offset = svga_mask_addr(svga->ma | 0x8000, svga) >> 12;
        else
                changed_offset = svga->ma >> 12;
                
        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                changed_offset2 = svga_mask_addr((svga->ma | 0x8000) + 4096, svga) >> 12;
        else
                changed_offset2 = svga_mask_addr(svga->ma + 4096, svga) >> 12;
                
        if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset2] || svga->fullchange)
        {
                offset = (8 - svga->scrollcache) + 24;
                p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
        
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
                
                for (x = 0; x <= svga->hdisp; x += 8)
                {
			r = (uint32_t *)(&edat[0]);
                        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
				q = (uint32_t *)(&svga->vram[svga_mask_addr(svga->ma | 0x8000, svga)]);
                        else
				q = (uint32_t *)(&svga->vram[svga->ma]);
			*r = *q;
                        svga->ma += 4;
                        svga->ma = svga_mask_addr(svga->ma, svga);

                        dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
                        p[0] = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[1] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
                        p[2] = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[3] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
                        p[4] = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[5] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
                        p[6] = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[7] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        
                        p += 8;
                }
        }
}

void svga_render_8bpp_lowres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - (svga->scrollcache & 6)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
                                                                
                for (x = 0; x <= svga->hdisp; x += 8)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga->ma]);
                        
                        p[0] = p[1] = svga_color_transform(svga->pallook[dat & svga->dac_mask]);
                        p[2] = p[3] = svga_color_transform(svga->pallook[(dat >> 8) & svga->dac_mask]);
                        p[4] = p[5] = svga_color_transform(svga->pallook[(dat >> 16) & svga->dac_mask]);
                        p[6] = p[7] = svga_color_transform(svga->pallook[(dat >> 24) & svga->dac_mask]);
                        
                        svga->ma += 4;
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += 8;
                }
        }
}

void svga_render_8bpp_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];

                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
                                                                
                for (x = 0; x <= svga->hdisp; x += 8)
                {
                        uint32_t dat;
                        dat = *(uint32_t *)(&svga->vram[svga->ma]);
                        p[0] = svga_color_transform(svga->pallook[dat & svga->dac_mask]);
                        p[1] = svga_color_transform(svga->pallook[(dat >> 8) & svga->dac_mask]);
                        p[2] = svga_color_transform(svga->pallook[(dat >> 16) & svga->dac_mask]);
                        p[3] = svga_color_transform(svga->pallook[(dat >> 24) & svga->dac_mask]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + 4, svga)]);
                        p[4] = svga_color_transform(svga->pallook[dat & svga->dac_mask]);
                        p[5] = svga_color_transform(svga->pallook[(dat >> 8) & svga->dac_mask]);
                        p[6] = svga_color_transform(svga->pallook[(dat >> 16) & svga->dac_mask]);
                        p[7] = svga_color_transform(svga->pallook[(dat >> 24) & svga->dac_mask]);
                        
                        svga->ma += 8;
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += 8;
                }
        }
}

void svga_render_15bpp_lowres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - (svga->scrollcache & 6)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];

                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x += 16)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + x, svga)]);
                        p[x]     = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 1] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 2] = svga_color_transform(video_15to32[dat >> 16]);
                        p[x + 3] = svga_color_transform(video_15to32[dat >> 16]);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + x + 4, svga)]);
                        p[x + 4] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 5] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 6] = svga_color_transform(video_15to32[dat >> 16]);
                        p[x + 7] = svga_color_transform(video_15to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + x + 8, svga)]);
                        p[x + 8] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 9] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 10] = svga_color_transform(video_15to32[dat >> 16]);
                        p[x + 11] = svga_color_transform(video_15to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + x + 12, svga)]);
                        p[x + 12] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 13] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 14] = svga_color_transform(video_15to32[dat >> 16]);
                        p[x + 15] = svga_color_transform(video_15to32[dat >> 16]);
                }
                svga->ma += x << 1; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_15bpp_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];

                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x += 8)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1), svga)]);
                        p[x]     = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 1] = svga_color_transform(video_15to32[dat >> 16]);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 4, svga)]);
                        p[x + 2] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 3] = svga_color_transform(video_15to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 8, svga)]);
                        p[x + 4] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 5] = svga_color_transform(video_15to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 12, svga)]);
                        p[x + 6] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 7] = svga_color_transform(video_15to32[dat >> 16]);
                }
                svga->ma += x << 1; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_16bpp_lowres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - (svga->scrollcache & 6)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];

                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x += 16)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + x, svga)]);
                        p[x]     = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 1] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 2] = svga_color_transform(video_16to32[dat >> 16]);
                        p[x + 3] = svga_color_transform(video_16to32[dat >> 16]);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + x + 4, svga)]);
                        p[x + 4] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 5] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 6] = svga_color_transform(video_16to32[dat >> 16]);
                        p[x + 7] = svga_color_transform(video_16to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + x + 8, svga)]);
                        p[x + 8] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 9] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 10] = svga_color_transform(video_16to32[dat >> 16]);
                        p[x + 11] = svga_color_transform(video_16to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + x + 12, svga)]);
                        p[x + 12] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 13] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 14] = svga_color_transform(video_16to32[dat >> 16]);
                        p[x + 15] = svga_color_transform(video_16to32[dat >> 16]);
                }
                svga->ma += x << 1; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_16bpp_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];

                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x += 8)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1), svga)]);
                        p[x]     = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 1] = svga_color_transform(video_16to32[dat >> 16]);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 4, svga)]);
                        p[x + 2] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 3] = svga_color_transform(video_16to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 8, svga)]);
                        p[x + 4] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 5] = svga_color_transform(video_16to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 12, svga)]);
                        p[x + 6] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 7] = svga_color_transform(video_16to32[dat >> 16]);
                }
                svga->ma += x << 1; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_24bpp_lowres(svga_t *svga)
{
        int x, offset;
        uint32_t fg;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);
        
        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                offset = (8 - (svga->scrollcache & 6)) + 24;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        fg = svga->vram[svga->ma] | (svga->vram[svga_mask_addr(svga->ma + 1, svga)] << 8) | (svga->vram[svga_mask_addr(svga->ma + 2, svga)] << 16);
                        svga->ma += 3; 
                        svga->ma = svga_mask_addr(svga->ma, svga);
                        ((uint32_t *)buffer32->line[dl])[(x << 1) + offset + x_add] = ((uint32_t *)buffer32->line[dl])[(x << 1) + 1 + offset + x_add] = svga_color_transform(fg);
                }
        }
}

void svga_render_24bpp_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x += 4)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga->ma]);
                        p[x] = svga_color_transform(dat & 0xffffff);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + 3, svga)]);
                        p[x + 1] = svga_color_transform(dat & 0xffffff);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + 6, svga)]);
                        p[x + 2] = svga_color_transform(dat & 0xffffff);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + 9, svga)]);
                        p[x + 3] = svga_color_transform(dat & 0xffffff);
                        
                        svga->ma += 12;
	                svga->ma = svga_mask_addr(svga->ma, svga);
                }
        }
}

void svga_render_32bpp_lowres(svga_t *svga)
{
        int x, offset;
        uint32_t fg;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);
        
        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                offset = (8 - (svga->scrollcache & 6)) + 24;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        fg = svga->vram[svga->ma] | (svga->vram[svga_mask_addr(svga->ma + 1, svga)] << 8) | (svga->vram[svga_mask_addr(svga->ma + 2, svga)] << 16);
                        svga->ma += 4; 
                        svga->ma = svga_mask_addr(svga->ma, svga);
                        ((uint32_t *)buffer32->line[dl])[(x << 1) + offset + x_add] = ((uint32_t *)buffer32->line[dl])[(x << 1) + 1 + offset + x_add] = svga_color_transform(fg);
                }
        }
}

/*72%
  91%*/
void svga_render_32bpp_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] ||  svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 2, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 2), svga)]);
                        p[x] = svga_color_transform(dat & 0xffffff);
                }
                svga->ma += 4; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_ABGR8888_lowres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] ||  svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 2, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 2), svga)]);
                        p[x << 1] = p[(x << 1) + 1] = svga_color_transform(((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16));
                }
                svga->ma += 4; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_ABGR8888_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] ||  svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 2, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 2), svga)]);
                        p[x] = svga_color_transform(((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16));
                }
                svga->ma += 4; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_RGBA8888_lowres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] ||  svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 2, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 2), svga)]);
                        p[x << 1] = p[(x << 1) + 1] = svga_color_transform(dat >> 8);
                }
                svga->ma += 4; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_RGBA8888_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] ||  svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 2, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 2), svga)]);
                        p[x] = svga_color_transform(dat >> 8);
                }
                svga->ma += 4; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}
