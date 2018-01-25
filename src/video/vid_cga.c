/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the old and new IBM CGA graphics cards.
 *
 * Version:	@(#)vid_cga.c	1.0.13	2018/01/25
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
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "video.h"
#include "vid_cga.h"
#include "vid_cga_comp.h"


#define CGA_RGB 0
#define CGA_COMPOSITE 1

#define COMPOSITE_OLD 0
#define COMPOSITE_NEW 1

static uint8_t crtcmask[32] = 
{
        0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void cga_recalctimings(cga_t *cga);

void cga_out(uint16_t addr, uint8_t val, void *p)
{
        cga_t *cga = (cga_t *)p;
        uint8_t old;
        switch (addr)
        {
                case 0x3D4:
                cga->crtcreg = val & 31;
                return;
                case 0x3D5:
                old = cga->crtc[cga->crtcreg];
                cga->crtc[cga->crtcreg] = val & crtcmask[cga->crtcreg];
                if (old != val)
                {
                        if (cga->crtcreg < 0xe || cga->crtcreg > 0x10)
                        {
                                fullchange = changeframecount;
                                cga_recalctimings(cga);
                        }
                }
                return;
                case 0x3D8:
                if (((cga->cgamode ^ val) & 5) != 0)
                {
                        cga->cgamode = val;
                        update_cga16_color(cga->cgamode);
                }

		if ((cga->cgamode ^ val) & 1)
		{
			cga_palette = (cga->rgb_type << 1);
			cgapal_rebuild();
		}

                cga->cgamode = val;
                return;
                case 0x3D9:
                cga->cgacol = val;
                return;
        }
}

uint8_t cga_in(uint16_t addr, void *p)
{
        cga_t *cga = (cga_t *)p;
        switch (addr)
        {
                case 0x3D4:
                return cga->crtcreg;
                case 0x3D5:
                return cga->crtc[cga->crtcreg];
                case 0x3DA:
                return cga->cgastat;
        }
        return 0xFF;
}

void cga_write(uint32_t addr, uint8_t val, void *p)
{
        cga_t *cga = (cga_t *)p;

        cga->vram[addr & 0x3fff] = val;
        if (cga->snow_enabled)
        {
                cga->charbuffer[ ((int)(((cga->dispontime - cga->vidtime) * 2) / CGACONST)) & 0xfc] = val;
                cga->charbuffer[(((int)(((cga->dispontime - cga->vidtime) * 2) / CGACONST)) & 0xfc) | 1] = val;
        }
        egawrites++;
        cycles -= 4;
}

uint8_t cga_read(uint32_t addr, void *p)
{
        cga_t *cga = (cga_t *)p;
        cycles -= 4;        
        if (cga->snow_enabled)
        {
                cga->charbuffer[ ((int)(((cga->dispontime - cga->vidtime) * 2) / CGACONST)) & 0xfc] = cga->vram[addr & 0x3fff];
                cga->charbuffer[(((int)(((cga->dispontime - cga->vidtime) * 2) / CGACONST)) & 0xfc) | 1] = cga->vram[addr & 0x3fff];
        }
        egareads++;
        return cga->vram[addr & 0x3fff];
}

void cga_recalctimings(cga_t *cga)
{
        double disptime;
	double _dispontime, _dispofftime;
        /* pclog("Recalc - %i %i %i\n", cga->crtc[0], cga->crtc[1], cga->cgamode & 1); */
        if (cga->cgamode & 1)
        {
                disptime = (double) (cga->crtc[0] + 1);
                _dispontime = (double) cga->crtc[1];
        }
        else
        {
                disptime = (double) ((cga->crtc[0] + 1) << 1);
                _dispontime = (double) (cga->crtc[1] << 1);
        }
        _dispofftime = disptime - _dispontime;
        _dispontime = _dispontime * CGACONST;
        _dispofftime = _dispofftime * CGACONST;
	cga->dispontime = (int64_t)(_dispontime * (1LL << TIMER_SHIFT));
	cga->dispofftime = (int64_t)(_dispofftime * (1LL << TIMER_SHIFT));
}

void cga_poll(void *p)
{
        cga_t *cga = (cga_t *)p;
        uint16_t ca = (cga->crtc[15] | (cga->crtc[14] << 8)) & 0x3fff;
        int drawcursor;
        int x, c;
        int oldvc;
        uint8_t chr, attr;
        uint16_t dat;
        int cols[4];
        int col;
        int oldsc;

        if (!cga->linepos)
        {
                cga->vidtime += cga->dispofftime;
                cga->cgastat |= 1;
                cga->linepos = 1;
                oldsc = cga->sc;
                if ((cga->crtc[8] & 3) == 3) 
                   cga->sc = ((cga->sc << 1) + cga->oddeven) & 7;
                if (cga->cgadispon)
                {
                        if (cga->displine < cga->firstline)
                        {
                                cga->firstline = cga->displine;
                                video_wait_for_buffer();
                        }
                        cga->lastline = cga->displine;
                        for (c = 0; c < 8; c++)
                        {
                                if ((cga->cgamode & 0x12) == 0x12)
                                {
                                        buffer->line[cga->displine][c] = 0;
                                        if (cga->cgamode & 1) buffer->line[cga->displine][c + (cga->crtc[1] << 3) + 8] = 0;
                                        else                  buffer->line[cga->displine][c + (cga->crtc[1] << 4) + 8] = 0;
                                }
                                else
                                {
                                        buffer->line[cga->displine][c] = (cga->cgacol & 15) + 16;
                                        if (cga->cgamode & 1) buffer->line[cga->displine][c + (cga->crtc[1] << 3) + 8] = (cga->cgacol & 15) + 16;
                                        else                  buffer->line[cga->displine][c + (cga->crtc[1] << 4) + 8] = (cga->cgacol & 15) + 16;
                                }
                        }
                        if (cga->cgamode & 1)
                        {
                                for (x = 0; x < cga->crtc[1]; x++)
                                {
                                        chr = cga->charbuffer[x << 1];
                                        attr = cga->charbuffer[(x << 1) + 1];
                                        drawcursor = ((cga->ma == ca) && cga->con && cga->cursoron);
                                        if (cga->cgamode & 0x20)
                                        {
                                                cols[1] = (attr & 15) + 16;
                                                cols[0] = ((attr >> 4) & 7) + 16;
                                                if ((cga->cgablink & 8) && (attr & 0x80) && !cga->drawcursor) 
                                                        cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = (attr & 15) + 16;
                                                cols[0] = (attr >> 4) + 16;
                                        }
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[cga->displine][(x << 3) + c + 8] = cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[cga->displine][(x << 3) + c + 8] = cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                        cga->ma++;
                                }
                        }
                        else if (!(cga->cgamode & 2))
                        {
                                for (x = 0; x < cga->crtc[1]; x++)
                                {
                                        chr  = cga->vram[((cga->ma << 1) & 0x3fff)];
                                        attr = cga->vram[(((cga->ma << 1) + 1) & 0x3fff)];
                                        drawcursor = ((cga->ma == ca) && cga->con && cga->cursoron);
                                        if (cga->cgamode & 0x20)
                                        {
                                                cols[1] = (attr & 15) + 16;
                                                cols[0] = ((attr >> 4) & 7) + 16;
                                                if ((cga->cgablink & 8) && (attr & 0x80)) cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = (attr & 15) + 16;
                                                cols[0] = (attr >> 4) + 16;
                                        }
                                        cga->ma++;
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[cga->displine][(x << 4)+(c << 1) + 8] = buffer->line[cga->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[cga->displine][(x << 4) + (c << 1) + 8] = buffer->line[cga->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                }
                        }
                        else if (!(cga->cgamode & 16))
                        {
                                cols[0] = (cga->cgacol & 15) | 16;
                                col = (cga->cgacol & 16) ? 24 : 16;
                                if (cga->cgamode & 4)
                                {
                                        cols[1] = col | 3;
                                        cols[2] = col | 4;
                                        cols[3] = col | 7;
                                }
                                else if (cga->cgacol & 32)
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
                                for (x = 0; x < cga->crtc[1]; x++)
                                {
                                        dat = (cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000)] << 8) | cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000) + 1];
                                        cga->ma++;
                                        for (c = 0; c < 8; c++)
                                        {
                                                buffer->line[cga->displine][(x << 4) + (c << 1) + 8] =
                                                  buffer->line[cga->displine][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
                                                dat <<= 2;
                                        }
                                }
                        }
                        else
                        {
                                cols[0] = 0; cols[1] = (cga->cgacol & 15) + 16;
                                for (x = 0; x < cga->crtc[1]; x++)
                                {
                                        dat = (cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000)] << 8) | cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000) + 1];
                                        cga->ma++;
                                        for (c = 0; c < 16; c++)
                                        {
                                                buffer->line[cga->displine][(x << 4) + c + 8] = cols[dat >> 15];
                                                dat <<= 1;
                                        }
                                }
                        }
                }
                else
                {
                        cols[0] = ((cga->cgamode & 0x12) == 0x12) ? 0 : (cga->cgacol & 15) + 16;
                        if (cga->cgamode & 1) hline(buffer, 0, cga->displine, (cga->crtc[1] << 3) + 16, cols[0]);
                        else                  hline(buffer, 0, cga->displine, (cga->crtc[1] << 4) + 16, cols[0]);
                }

                if (cga->cgamode & 1) x = (cga->crtc[1] << 3) + 16;
                else                  x = (cga->crtc[1] << 4) + 16;

                if (cga->composite)
                {
			for (c = 0; c < x; c++)
				buffer32->line[cga->displine][c] = buffer->line[cga->displine][c] & 0xf;

			Composite_Process(cga->cgamode, 0, x >> 2, buffer32->line[cga->displine]);
                }

                cga->sc = oldsc;
                if (cga->vc == cga->crtc[7] && !cga->sc)
                   cga->cgastat |= 8;
                cga->displine++;
                if (cga->displine >= 360) 
                        cga->displine = 0;
        }
        else
        {
                cga->vidtime += cga->dispontime;
                cga->linepos = 0;
                if (cga->vsynctime)
                {
                        cga->vsynctime--;
                        if (!cga->vsynctime)
                           cga->cgastat &= ~8;
                }
                if (cga->sc == (cga->crtc[11] & 31) || ((cga->crtc[8] & 3) == 3 && cga->sc == ((cga->crtc[11] & 31) >> 1))) 
                { 
                        cga->con = 0; 
                        cga->coff = 1; 
                }
                if ((cga->crtc[8] & 3) == 3 && cga->sc == (cga->crtc[9] >> 1))
                   cga->maback = cga->ma;
                if (cga->vadj)
                {
                        cga->sc++;
                        cga->sc &= 31;
                        cga->ma = cga->maback;
                        cga->vadj--;
                        if (!cga->vadj)
                        {
                                cga->cgadispon = 1;
                                cga->ma = cga->maback = (cga->crtc[13] | (cga->crtc[12] << 8)) & 0x3fff;
                                cga->sc = 0;
                        }
                }
                else if (cga->sc == cga->crtc[9])
                {
                        cga->maback = cga->ma;
                        cga->sc = 0;
                        oldvc = cga->vc;
                        cga->vc++;
                        cga->vc &= 127;

                        if (cga->vc == cga->crtc[6]) 
                                cga->cgadispon = 0;

                        if (oldvc == cga->crtc[4])
                        {
                                cga->vc = 0;
                                cga->vadj = cga->crtc[5];
                                if (!cga->vadj) cga->cgadispon = 1;
                                if (!cga->vadj) cga->ma = cga->maback = (cga->crtc[13] | (cga->crtc[12] << 8)) & 0x3fff;
                                if ((cga->crtc[10] & 0x60) == 0x20) cga->cursoron = 0;
                                else                                cga->cursoron = cga->cgablink & 8;
                        }

                        if (cga->vc == cga->crtc[7])
                        {
                                cga->cgadispon = 0;
                                cga->displine = 0;
                                cga->vsynctime = 16;
                                if (cga->crtc[7])
                                {
                                        if (cga->cgamode & 1) x = (cga->crtc[1] << 3) + 16;
                                        else                  x = (cga->crtc[1] << 4) + 16;
                                        cga->lastline++;
                                        if ((x != xsize) || ((cga->lastline - cga->firstline) != ysize) || video_force_resize_get())
                                        {
                                                xsize = x;
                                                ysize = cga->lastline - cga->firstline;
                                                if (xsize < 64) xsize = 656;
                                                if (ysize < 32) ysize = 200;
                                                set_screen_size(xsize, (ysize << 1) + 16);

						if (video_force_resize_get())
							video_force_resize_set(0);
                                        }
                                        
                                        if (cga->composite) 
                                           video_blit_memtoscreen(0, cga->firstline - 4, 0, (cga->lastline - cga->firstline) + 8, xsize, (cga->lastline - cga->firstline) + 8);
                                        else          
                                           video_blit_memtoscreen_8(0, cga->firstline - 4, 0, (cga->lastline - cga->firstline) + 8, xsize, (cga->lastline - cga->firstline) + 8);
                                        frames++;

                                        video_res_x = xsize - 16;
                                        video_res_y = ysize;
                                        if (cga->cgamode & 1)
                                        {
                                                video_res_x /= 8;
                                                video_res_y /= cga->crtc[9] + 1;
                                                video_bpp = 0;
                                        }
                                        else if (!(cga->cgamode & 2))
                                        {
                                                video_res_x /= 16;
                                                video_res_y /= cga->crtc[9] + 1;
                                                video_bpp = 0;
                                        }
                                        else if (!(cga->cgamode & 16))
                                        {
                                                video_res_x /= 2;
                                                video_bpp = 2;
                                        }
                                        else
                                        {
                                                video_bpp = 1;
                                        }
                                }
                                cga->firstline = 1000;
                                cga->lastline = 0;
                                cga->cgablink++;
                                cga->oddeven ^= 1;
                        }
                }
                else
                {
                        cga->sc++;
                        cga->sc &= 31;
                        cga->ma = cga->maback;
                }
                if (cga->cgadispon)
                        cga->cgastat &= ~1;
                if ((cga->sc == (cga->crtc[10] & 31) || ((cga->crtc[8] & 3) == 3 && cga->sc == ((cga->crtc[10] & 31) >> 1)))) 
                        cga->con = 1;
                if (cga->cgadispon && (cga->cgamode & 1))
                {
                        for (x = 0; x < (cga->crtc[1] << 1); x++)
                            cga->charbuffer[x] = cga->vram[(((cga->ma << 1) + x) & 0x3fff)];
                }
        }
}

void cga_init(cga_t *cga)
{
        cga->composite = 0;
}

void *cga_standalone_init(device_t *info)
{
        int display_type;
        cga_t *cga = malloc(sizeof(cga_t));
        memset(cga, 0, sizeof(cga_t));

        display_type = device_get_config_int("display_type");
        cga->composite = (display_type != CGA_RGB);
        cga->revision = device_get_config_int("composite_type");
        cga->snow_enabled = device_get_config_int("snow_enabled");

        cga->vram = malloc(0x4000);

	cga_comp_init(cga->revision);
        timer_add(cga_poll, &cga->vidtime, TIMER_ALWAYS_ENABLED, cga);
        mem_mapping_add(&cga->mapping, 0xb8000, 0x08000, cga_read, NULL, NULL, cga_write, NULL, NULL,  NULL, MEM_MAPPING_EXTERNAL, cga);
        io_sethandler(0x03d0, 0x0010, cga_in, NULL, NULL, cga_out, NULL, NULL, cga);

        overscan_x = overscan_y = 16;

        cga->rgb_type = device_get_config_int("rgb_type");
	cga_palette = (cga->rgb_type << 1);
	cgapal_rebuild();

        return cga;
}

void cga_close(void *p)
{
        cga_t *cga = (cga_t *)p;

        free(cga->vram);
        free(cga);
}

void cga_speed_changed(void *p)
{
        cga_t *cga = (cga_t *)p;

        cga_recalctimings(cga);
}

device_config_t cga_config[] =
{
        {
                "display_type", "Display type", CONFIG_SELECTION, "", CGA_RGB,
                {
                        {
                                "RGB", CGA_RGB
                        },
                        {
                                "Composite", CGA_COMPOSITE
                        },
                        {
                                ""
                        }
                }
        },
        {
                "composite_type", "Composite type", CONFIG_SELECTION, "", COMPOSITE_OLD,
                {
                        {
                                "Old", COMPOSITE_OLD
                        },
                        {
                                "New", COMPOSITE_NEW
                        },
                        {
                                ""
                        }
                }
        },
        {
                "rgb_type", "RGB type", CONFIG_SELECTION, "", 0,
                {
                        {
                                "Color", 0
                        },
                        {
                                "Green Monochrome", 1
                        },
                        {
                                "Amber Monochrome", 2
                        },
                        {
                                "Gray Monochrome", 3
                        },
                        {
                                "Color (no brown)", 4
                        },
                        {
                                ""
                        }
                }
        },
        {
                "snow_enabled", "Snow emulation", CONFIG_BINARY, "", 1
        },
        {
                "", "", -1
        }
};

device_t cga_device =
{
        "CGA",
        DEVICE_ISA, 0,
        cga_standalone_init,
        cga_close,
	NULL,
        NULL,
        cga_speed_changed,
        NULL,
        NULL,
        cga_config
};
