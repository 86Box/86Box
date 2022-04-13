/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Plantronics ColorPlus emulation.
 *
 *
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
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/lpt.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_colorplus.h>
#include <86box/vid_cga_comp.h>


/* Bits in the colorplus control register: */
#define COLORPLUS_PLANE_SWAP	0x40	/* Swap planes at 0000h and 4000h */
#define COLORPLUS_640x200_MODE	0x20	/* 640x200x4 mode active */
#define COLORPLUS_320x200_MODE	0x10	/* 320x200x16 mode active */
#define COLORPLUS_EITHER_MODE   0x30	/* Either mode active */

/* Bits in the CGA graphics mode register */
#define CGA_GRAPHICS_MODE       0x02	/* CGA graphics mode selected? */

#define CGA_RGB 0
#define CGA_COMPOSITE 1

#define COMPOSITE_OLD 0
#define COMPOSITE_NEW 1


video_timings_t timing_colorplus = {VIDEO_ISA, 8, 16, 32,   8, 16, 32};


void cga_recalctimings(cga_t *cga);

void colorplus_out(uint16_t addr, uint8_t val, void *p)
{
        colorplus_t *colorplus = (colorplus_t *)p;

        if (addr == 0x3DD)
        {
                colorplus->control = val & 0x70;
	}
        else
        {
                cga_out(addr, val, &colorplus->cga);
	}
}

uint8_t colorplus_in(uint16_t addr, void *p)
{
        colorplus_t *colorplus = (colorplus_t *)p;

        return cga_in(addr, &colorplus->cga);
}

void colorplus_write(uint32_t addr, uint8_t val, void *p)
{
        colorplus_t *colorplus = (colorplus_t *)p;

	if ((colorplus->control & COLORPLUS_PLANE_SWAP) &&
	    (colorplus->control & COLORPLUS_EITHER_MODE) &&
	    (colorplus->cga.cgamode & CGA_GRAPHICS_MODE))
	{
		addr ^= 0x4000;
	}
	else if (!(colorplus->control & COLORPLUS_EITHER_MODE))
	{
		addr &= 0x3FFF;
	}
        colorplus->cga.vram[addr & 0x7fff] = val;
        if (colorplus->cga.snow_enabled)
        {
				int offset = ((timer_get_remaining_u64(&colorplus->cga.timer) / CGACONST) * 2) & 0xfc;
				colorplus->cga.charbuffer[offset] = colorplus->cga.vram[addr & 0x7fff];
				colorplus->cga.charbuffer[offset | 1] = colorplus->cga.vram[addr & 0x7fff];
        }
        cycles -= 4;
}

uint8_t colorplus_read(uint32_t addr, void *p)
{
        colorplus_t *colorplus = (colorplus_t *)p;

	if ((colorplus->control & COLORPLUS_PLANE_SWAP) &&
	    (colorplus->control & COLORPLUS_EITHER_MODE) &&
	    (colorplus->cga.cgamode & CGA_GRAPHICS_MODE))
	{
		addr ^= 0x4000;
	}
	else if (!(colorplus->control & COLORPLUS_EITHER_MODE))
	{
		addr &= 0x3FFF;
	}
        cycles -= 4;
        if (colorplus->cga.snow_enabled)
        {
				int offset = ((timer_get_remaining_u64(&colorplus->cga.timer) / CGACONST) * 2) & 0xfc;
				colorplus->cga.charbuffer[offset] = colorplus->cga.vram[addr & 0x7fff];
				colorplus->cga.charbuffer[offset | 1] = colorplus->cga.vram[addr & 0x7fff];
        }
        return colorplus->cga.vram[addr & 0x7fff];
}

void colorplus_recalctimings(colorplus_t *colorplus)
{
	cga_recalctimings(&colorplus->cga);
}

void colorplus_poll(void *p)
{
        colorplus_t *colorplus = (colorplus_t *)p;
        int x, c;
        int oldvc;
        uint16_t dat0, dat1;
        int cols[4];
        int col;
        int oldsc;
	static const int cols16[16] = { 0x10,0x12,0x14,0x16,
					0x18,0x1A,0x1C,0x1E,
					0x11,0x13,0x15,0x17,
					0x19,0x1B,0x1D,0x1F };
	uint8_t *plane0 = colorplus->cga.vram;
	uint8_t *plane1 = colorplus->cga.vram + 0x4000;

	/* If one of the extra modes is not selected, drop down to the CGA
	 * drawing code. */
	if (!((colorplus->control & COLORPLUS_EITHER_MODE) &&
	      (colorplus->cga.cgamode & CGA_GRAPHICS_MODE)))
	{
		cga_poll(&colorplus->cga);
		return;
	}

        if (!colorplus->cga.linepos)
        {
                timer_advance_u64(&colorplus->cga.timer, colorplus->cga.dispofftime);
                colorplus->cga.cgastat |= 1;
                colorplus->cga.linepos = 1;
                oldsc = colorplus->cga.sc;
                if ((colorplus->cga.crtc[8] & 3) == 3)
                   colorplus->cga.sc = ((colorplus->cga.sc << 1) + colorplus->cga.oddeven) & 7;
                if (colorplus->cga.cgadispon)
                {
                        if (colorplus->cga.displine < colorplus->cga.firstline)
                        {
                                colorplus->cga.firstline = colorplus->cga.displine;
                                video_wait_for_buffer();
                        }
                        colorplus->cga.lastline = colorplus->cga.displine;
			/* Left / right border */
                        for (c = 0; c < 8; c++)
                        {
                                buffer32->line[colorplus->cga.displine][c] =
                                buffer32->line[colorplus->cga.displine][c + (colorplus->cga.crtc[1] << 4) + 8] =
					(colorplus->cga.cgacol & 15) + 16;
                        }
			if (colorplus->control & COLORPLUS_320x200_MODE)
			{
                                for (x = 0; x < colorplus->cga.crtc[1]; x++)
                                {
                                        dat0 = (plane0[((colorplus->cga.ma << 1) & 0x1fff) + ((colorplus->cga.sc & 1) * 0x2000)] << 8) |
                                                plane0[((colorplus->cga.ma << 1) & 0x1fff) + ((colorplus->cga.sc & 1) * 0x2000) + 1];
                                        dat1 = (plane1[((colorplus->cga.ma << 1) & 0x1fff) + ((colorplus->cga.sc & 1) * 0x2000)] << 8) |
                                                plane1[((colorplus->cga.ma << 1) & 0x1fff) + ((colorplus->cga.sc & 1) * 0x2000) + 1];
                                        colorplus->cga.ma++;
                                        for (c = 0; c < 8; c++)
                                        {
                                                buffer32->line[colorplus->cga.displine][(x << 4) + (c << 1) + 8] =
                                                buffer32->line[colorplus->cga.displine][(x << 4) + (c << 1) + 1 + 8] =
                                                	cols16[(dat0 >> 14) | ((dat1 >> 14) << 2)];
                                                dat0 <<= 2;
                                                dat1 <<= 2;
                                        }
                                }
			}
			else if (colorplus->control & COLORPLUS_640x200_MODE)
                        {
                                cols[0] = (colorplus->cga.cgacol & 15) | 16;
                                col = (colorplus->cga.cgacol & 16) ? 24 : 16;
                                if (colorplus->cga.cgamode & 4)
                                {
                                        cols[1] = col | 3;
                                        cols[2] = col | 4;
                                        cols[3] = col | 7;
                                }
                                else if (colorplus->cga.cgacol & 32)
                                {
                                        cols[1] = col | 3;
                                        cols[2] = col | 5;
                                        cols[3] = col | 7;
                                }
                                else
                                {
                                        cols[1] = col | 2;
                                        cols[2] = col | 4;
                                        cols[3] = col | 6;
                                }
                                for (x = 0; x < colorplus->cga.crtc[1]; x++)
                                {
                                        dat0 = (plane0[((colorplus->cga.ma << 1) & 0x1fff) + ((colorplus->cga.sc & 1) * 0x2000)] << 8) |
                                                plane0[((colorplus->cga.ma << 1) & 0x1fff) + ((colorplus->cga.sc & 1) * 0x2000) + 1];
                                        dat1 = (plane1[((colorplus->cga.ma << 1) & 0x1fff) + ((colorplus->cga.sc & 1) * 0x2000)] << 8) |
                                                plane1[((colorplus->cga.ma << 1) & 0x1fff) + ((colorplus->cga.sc & 1) * 0x2000) + 1];
                                        colorplus->cga.ma++;
                                        for (c = 0; c < 16; c++)
                                        {
                                                buffer32->line[colorplus->cga.displine][(x << 4) + c + 8] =
                                                	cols[(dat0 >> 15) | ((dat1 >> 15) << 1)];
                                                dat0 <<= 1;
                                                dat1 <<= 1;
                                        }
                                }
                        }
                }
                else	/* Top / bottom border */
                {
                        cols[0] = (colorplus->cga.cgacol & 15) + 16;
                        hline(buffer32, 0, colorplus->cga.displine, (colorplus->cga.crtc[1] << 4) + 16, cols[0]);
                }

                x = (colorplus->cga.crtc[1] << 4) + 16;

                if (colorplus->cga.composite)
			Composite_Process(colorplus->cga.cgamode, 0, x >> 2, buffer32->line[colorplus->cga.displine]);

                colorplus->cga.sc = oldsc;
                if (colorplus->cga.vc == colorplus->cga.crtc[7] && !colorplus->cga.sc)
                   colorplus->cga.cgastat |= 8;
                colorplus->cga.displine++;
                if (colorplus->cga.displine >= 360)
                        colorplus->cga.displine = 0;
        }
        else
        {
                timer_advance_u64(&colorplus->cga.timer, colorplus->cga.dispontime);
                colorplus->cga.linepos = 0;
                if (colorplus->cga.vsynctime)
                {
                        colorplus->cga.vsynctime--;
                        if (!colorplus->cga.vsynctime)
                           colorplus->cga.cgastat &= ~8;
                }
                if (colorplus->cga.sc == (colorplus->cga.crtc[11] & 31) || ((colorplus->cga.crtc[8] & 3) == 3 && colorplus->cga.sc == ((colorplus->cga.crtc[11] & 31) >> 1)))
                {
                        colorplus->cga.con = 0;
                        colorplus->cga.coff = 1;
                }
                if ((colorplus->cga.crtc[8] & 3) == 3 && colorplus->cga.sc == (colorplus->cga.crtc[9] >> 1))
                   colorplus->cga.maback = colorplus->cga.ma;
                if (colorplus->cga.vadj)
                {
                        colorplus->cga.sc++;
                        colorplus->cga.sc &= 31;
                        colorplus->cga.ma = colorplus->cga.maback;
                        colorplus->cga.vadj--;
                        if (!colorplus->cga.vadj)
                        {
                                colorplus->cga.cgadispon = 1;
                                colorplus->cga.ma = colorplus->cga.maback = (colorplus->cga.crtc[13] | (colorplus->cga.crtc[12] << 8)) & 0x3fff;
                                colorplus->cga.sc = 0;
                        }
                }
                else if (colorplus->cga.sc == colorplus->cga.crtc[9])
                {
                        colorplus->cga.maback = colorplus->cga.ma;
                        colorplus->cga.sc = 0;
                        oldvc = colorplus->cga.vc;
                        colorplus->cga.vc++;
                        colorplus->cga.vc &= 127;

                        if (colorplus->cga.vc == colorplus->cga.crtc[6])
                                colorplus->cga.cgadispon = 0;

                        if (oldvc == colorplus->cga.crtc[4])
                        {
                                colorplus->cga.vc = 0;
                                colorplus->cga.vadj = colorplus->cga.crtc[5];
                                if (!colorplus->cga.vadj) colorplus->cga.cgadispon = 1;
                                if (!colorplus->cga.vadj) colorplus->cga.ma = colorplus->cga.maback = (colorplus->cga.crtc[13] | (colorplus->cga.crtc[12] << 8)) & 0x3fff;
                                if ((colorplus->cga.crtc[10] & 0x60) == 0x20) colorplus->cga.cursoron = 0;
                                else                                colorplus->cga.cursoron = colorplus->cga.cgablink & 8;
                        }

                        if (colorplus->cga.vc == colorplus->cga.crtc[7])
                        {
                                colorplus->cga.cgadispon = 0;
                                colorplus->cga.displine = 0;
                                colorplus->cga.vsynctime = 16;
                                if (colorplus->cga.crtc[7])
                                {
                                        if (colorplus->cga.cgamode & 1) x = (colorplus->cga.crtc[1] << 3) + 16;
                                        else                  x = (colorplus->cga.crtc[1] << 4) + 16;
                                        colorplus->cga.lastline++;
                                        if (x != xsize || (colorplus->cga.lastline - colorplus->cga.firstline) != ysize)
                                        {
                                                xsize = x;
                                                ysize = colorplus->cga.lastline - colorplus->cga.firstline;
                                                if (xsize < 64) xsize = 656;
                                                if (ysize < 32) ysize = 200;
                                                set_screen_size(xsize, (ysize << 1) + 16);
                                        }

                                        if (colorplus->cga.composite)
                                           video_blit_memtoscreen(0, colorplus->cga.firstline - 4, xsize, (colorplus->cga.lastline - colorplus->cga.firstline) + 8);
                                        else
                                           video_blit_memtoscreen_8(0, colorplus->cga.firstline - 4, xsize, (colorplus->cga.lastline - colorplus->cga.firstline) + 8);
                                        frames++;

                                        video_res_x = xsize - 16;
                                        video_res_y = ysize;
                                        if (colorplus->cga.cgamode & 1)
                                        {
                                                video_res_x /= 8;
                                                video_res_y /= colorplus->cga.crtc[9] + 1;
                                                video_bpp = 0;
                                        }
                                        else if (!(colorplus->cga.cgamode & 2))
                                        {
                                                video_res_x /= 16;
                                                video_res_y /= colorplus->cga.crtc[9] + 1;
                                                video_bpp = 0;
                                        }
                                        else if (!(colorplus->cga.cgamode & 16))
                                        {
                                                video_res_x /= 2;
                                                video_bpp = 2;
                                        }
                                        else
                                        {
                                                video_bpp = 1;
                                        }
                                }
                                colorplus->cga.firstline = 1000;
                                colorplus->cga.lastline = 0;
                                colorplus->cga.cgablink++;
                                colorplus->cga.oddeven ^= 1;
                        }
                }
                else
                {
                        colorplus->cga.sc++;
                        colorplus->cga.sc &= 31;
                        colorplus->cga.ma = colorplus->cga.maback;
                }
                if (colorplus->cga.cgadispon)
                        colorplus->cga.cgastat &= ~1;
                if ((colorplus->cga.sc == (colorplus->cga.crtc[10] & 31) || ((colorplus->cga.crtc[8] & 3) == 3 && colorplus->cga.sc == ((colorplus->cga.crtc[10] & 31) >> 1))))
                        colorplus->cga.con = 1;
                if (colorplus->cga.cgadispon && (colorplus->cga.cgamode & 1))
                {
                        for (x = 0; x < (colorplus->cga.crtc[1] << 1); x++)
                            colorplus->cga.charbuffer[x] = colorplus->cga.vram[(((colorplus->cga.ma << 1) + x) & 0x3fff)];
                }
        }
}

void colorplus_init(colorplus_t *colorplus)
{
	cga_init(&colorplus->cga);
}

void *colorplus_standalone_init(const device_t *info)
{
        int display_type;

        colorplus_t *colorplus = malloc(sizeof(colorplus_t));
        memset(colorplus, 0, sizeof(colorplus_t));

	video_inform(VIDEO_FLAG_TYPE_CGA, &timing_colorplus);

	/* Copied from the CGA init. Ideally this would be done by
	 * calling a helper function rather than duplicating code */
        display_type = device_get_config_int("display_type");
        colorplus->cga.composite = (display_type != CGA_RGB);
        colorplus->cga.revision = device_get_config_int("composite_type");
        colorplus->cga.snow_enabled = device_get_config_int("snow_enabled");

        colorplus->cga.vram = malloc(0x8000);

	cga_comp_init(colorplus->cga.revision);
        timer_add(&colorplus->cga.timer, colorplus_poll, colorplus, 1);
        mem_mapping_add(&colorplus->cga.mapping, 0xb8000, 0x08000, colorplus_read, NULL, NULL, colorplus_write, NULL, NULL,  NULL, MEM_MAPPING_EXTERNAL, colorplus);
        io_sethandler(0x03d0, 0x0010, colorplus_in, NULL, NULL, colorplus_out, NULL, NULL, colorplus);

	lpt3_init(0x3BC);

        return colorplus;
}

void colorplus_close(void *p)
{
        colorplus_t *colorplus = (colorplus_t *)p;

        free(colorplus->cga.vram);
        free(colorplus);
}

void colorplus_speed_changed(void *p)
{
        colorplus_t *colorplus = (colorplus_t *)p;

        cga_recalctimings(&colorplus->cga);
}

static const device_config_t colorplus_config[] = {
// clang-format off
    {
        .name = "display_type",
        .description = "Display type",
        .type = CONFIG_SELECTION,
        .default_int = CGA_RGB,
        .selection = {
            {
                .description = "RGB",
                .value = CGA_RGB
            },
            {
                .description = "Composite",
                .value = CGA_COMPOSITE
            },
            {
                .description = ""
            }
        }
    },
    {
        .name = "composite_type",
        .description = "Composite type",
        .type = CONFIG_SELECTION,
        .default_int = COMPOSITE_OLD,
        .selection = {
            {
                .description = "Old",
                .value = COMPOSITE_OLD
            },
            {
                .description = "New",
                .value = COMPOSITE_NEW
            },
            {
                .description = ""
            }
        }
    },
    {
        .name = "snow_enabled",
        .description = "Snow emulation",
        .type = CONFIG_BINARY,
        .default_int = 1
    },
    {
        .type = CONFIG_END
    }
// clang-format on
};

const device_t colorplus_device =
{
    .name = "Colorplus",
    .internal_name = "plantronics",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = colorplus_standalone_init,
    .close = colorplus_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = colorplus_speed_changed,
    .force_redraw = NULL,
    .config = colorplus_config
};
