/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		PC1512 CGA emulation
 *
 *		The PC1512 extends CGA with a bit-planar 640x200x16 mode.
 *		Most CRTC registers are fixed.
 *
 *		The Technical Reference Manual lists the video waitstate
 *		time as between 12 and 46 cycles. We currently always use
 *		the lower number.
 *
 * Version:	@(#)vid_pc1512.c	1.0.1	2017/10/16
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../timer.h"
#include "../device.h"
#include "video.h"
#include "vid_pc1512.h"


typedef struct pc1512_t
{
        mem_mapping_t mapping;
        
        uint8_t crtc[32];
        int crtcreg;

        uint8_t cgacol, cgamode, stat;
        
        uint8_t plane_write, plane_read, border;

        int linepos, displine;
        int sc, vc;
        int cgadispon;
        int con, coff, cursoron, cgablink;
        int64_t vsynctime;
	int vadj;
        uint16_t ma, maback;
        int dispon;
        int blink;
        
        int64_t dispontime, dispofftime;
	int64_t vidtime;
        int firstline, lastline;
        
        uint8_t *vram;
} pc1512_t;

static uint8_t crtcmask[32] = 
{
        0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void pc1512_recalctimings(pc1512_t *pc1512);

static void pc1512_out(uint16_t addr, uint8_t val, void *p)
{
        pc1512_t *pc1512 = (pc1512_t *)p;
        uint8_t old;
        switch (addr)
        {
                case 0x3d4:
                pc1512->crtcreg = val & 31;
                return;
                case 0x3d5:
                old = pc1512->crtc[pc1512->crtcreg];
                pc1512->crtc[pc1512->crtcreg] = val & crtcmask[pc1512->crtcreg];
                if (old != val)
                {
                        if (pc1512->crtcreg < 0xe || pc1512->crtcreg > 0x10)
                        {
                                fullchange = changeframecount;
                                pc1512_recalctimings(pc1512);
                        }
                }
                return;
                case 0x3d8:
                if ((val & 0x12) == 0x12 && (pc1512->cgamode & 0x12) != 0x12)
                {
                        pc1512->plane_write = 0xf;
                        pc1512->plane_read  = 0;
                }
                pc1512->cgamode = val;
                return;
                case 0x3d9:
                pc1512->cgacol = val;
                return;
                case 0x3dd:
                pc1512->plane_write = val;
                return;
                case 0x3de:
                pc1512->plane_read = val & 3;
                return;
                case 0x3df:
                pc1512->border = val;
                return;
        }
}

static uint8_t pc1512_in(uint16_t addr, void *p)
{
        pc1512_t *pc1512 = (pc1512_t *)p;
        switch (addr)
        {
                case 0x3d4:
                return pc1512->crtcreg;
                case 0x3d5:
                return pc1512->crtc[pc1512->crtcreg];
                case 0x3da:
                return pc1512->stat;
        }
        return 0xff;
}

static void pc1512_write(uint32_t addr, uint8_t val, void *p)
{
        pc1512_t *pc1512 = (pc1512_t *)p;

        egawrites++;
        cycles -= 12;
        addr &= 0x3fff;

        if ((pc1512->cgamode & 0x12) == 0x12)
        {
                if (pc1512->plane_write & 1) pc1512->vram[addr]          = val;
                if (pc1512->plane_write & 2) pc1512->vram[addr | 0x4000] = val;
                if (pc1512->plane_write & 4) pc1512->vram[addr | 0x8000] = val;
                if (pc1512->plane_write & 8) pc1512->vram[addr | 0xc000] = val;
        }
        else
           pc1512->vram[addr] = val;
}

static uint8_t pc1512_read(uint32_t addr, void *p)
{
        pc1512_t *pc1512 = (pc1512_t *)p;

        egareads++;
        cycles -= 12;
        addr &= 0x3fff;

        if ((pc1512->cgamode & 0x12) == 0x12)
           return pc1512->vram[addr | (pc1512->plane_read << 14)];
        return pc1512->vram[addr];
}


static void pc1512_recalctimings(pc1512_t *pc1512)
{
	double _dispontime, _dispofftime, disptime;
        disptime = 128; /*Fixed on PC1512*/
        _dispontime = 80;
        _dispofftime = disptime - _dispontime;
        _dispontime  *= CGACONST;
        _dispofftime *= CGACONST;
	pc1512->dispontime  = (int64_t)(_dispontime * (1 << TIMER_SHIFT));
	pc1512->dispofftime = (int64_t)(_dispofftime * (1 << TIMER_SHIFT));
}

static void pc1512_poll(void *p)
{
        pc1512_t *pc1512 = (pc1512_t *)p;
        uint16_t ca = (pc1512->crtc[15] | (pc1512->crtc[14] << 8)) & 0x3fff;
        int drawcursor;
        int x, c;
        uint8_t chr, attr;
        uint16_t dat, dat2, dat3, dat4;
        int cols[4];
        int col;
        int oldsc;

        if (!pc1512->linepos)
        {
                pc1512->vidtime += pc1512->dispofftime;
                pc1512->stat |= 1;
                pc1512->linepos = 1;
                oldsc = pc1512->sc;
                if (pc1512->dispon)
                {
                        if (pc1512->displine < pc1512->firstline) 
                        {
                                pc1512->firstline = pc1512->displine;
                                video_wait_for_buffer();
                        }
                        pc1512->lastline = pc1512->displine;
                        for (c = 0; c < 8; c++)
                        {
                                if ((pc1512->cgamode & 0x12) == 0x12)
                                {
                                        buffer->line[pc1512->displine][c] = (pc1512->border & 15) + 16;
                                        if (pc1512->cgamode & 1) buffer->line[pc1512->displine][c + (pc1512->crtc[1] << 3) + 8] = 0;
                                        else                     buffer->line[pc1512->displine][c + (pc1512->crtc[1] << 4) + 8] = 0;
                                }
                                else
                                {
                                        buffer->line[pc1512->displine][c] = (pc1512->cgacol & 15) + 16;
                                        if (pc1512->cgamode & 1) buffer->line[pc1512->displine][c + (pc1512->crtc[1] << 3) + 8] = (pc1512->cgacol & 15) + 16;
                                        else                     buffer->line[pc1512->displine][c + (pc1512->crtc[1] << 4) + 8] = (pc1512->cgacol & 15) + 16;
                                }
                        }
                        if (pc1512->cgamode & 1)
                        {
                                for (x = 0; x < 80; x++)
                                {
                                        chr  = pc1512->vram[ ((pc1512->ma << 1)      & 0x3fff)];
                                        attr = pc1512->vram[(((pc1512->ma << 1) + 1) & 0x3fff)];
                                        drawcursor = ((pc1512->ma == ca) && pc1512->con && pc1512->cursoron);
                                        if (pc1512->cgamode & 0x20)
                                        {
                                                cols[1] = (attr & 15) + 16;
                                                cols[0] = ((attr >> 4) & 7) + 16;
                                                if ((pc1512->blink & 16) && (attr & 0x80) && !drawcursor) 
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
                                                    buffer->line[pc1512->displine][(x << 3) + c + 8] = cols[(fontdat[chr][pc1512->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[pc1512->displine][(x << 3) + c + 8] = cols[(fontdat[chr][pc1512->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                        pc1512->ma++;
                                }
                        }
                        else if (!(pc1512->cgamode & 2))
                        {
                                for (x = 0; x < 40; x++)
                                {
                                        chr  = pc1512->vram[ ((pc1512->ma << 1)      & 0x3fff)];
                                        attr = pc1512->vram[(((pc1512->ma << 1) + 1) & 0x3fff)];
                                        drawcursor = ((pc1512->ma == ca) && pc1512->con && pc1512->cursoron);
                                        if (pc1512->cgamode & 0x20)
                                        {
                                                cols[1] = (attr & 15) + 16;
                                                cols[0] = ((attr >> 4) & 7) + 16;
                                                if ((pc1512->blink & 16) && (attr & 0x80)) 
                                                        cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = (attr & 15) + 16;
                                                cols[0] = (attr >> 4) + 16;
                                        }
                                        pc1512->ma++;
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[pc1512->displine][(x << 4) + (c << 1) + 8] = 
                                                    buffer->line[pc1512->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][pc1512->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[pc1512->displine][(x << 4) + (c << 1) + 8] = 
                                                    buffer->line[pc1512->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][pc1512->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                }
                        }
                        else if (!(pc1512->cgamode&16))
                        {
                                cols[0] = (pc1512->cgacol & 15) | 16;
                                col = (pc1512->cgacol & 16) ? 24 : 16;
                                if (pc1512->cgamode & 4)
                                {
                                        cols[1] = col | 3;
                                        cols[2] = col | 4;
                                        cols[3] = col | 7;
                                }
                                else if (pc1512->cgacol & 32)
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
                                for (x = 0; x < 40; x++)
                                {
                                        dat = (pc1512->vram[((pc1512->ma << 1) & 0x1fff) + ((pc1512->sc & 1) * 0x2000)] << 8) | pc1512->vram[((pc1512->ma << 1) & 0x1fff) + ((pc1512->sc & 1) * 0x2000) + 1];
                                        pc1512->ma++;
                                        for (c = 0; c < 8; c++)
                                        {
                                                buffer->line[pc1512->displine][(x << 4) + (c << 1) + 8] =
                                                buffer->line[pc1512->displine][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
                                                dat <<= 2;
                                        }
                                }
                        }
                        else
                        {
                                for (x = 0; x < 40; x++)
                                {
                                        ca = ((pc1512->ma << 1) & 0x1fff) + ((pc1512->sc & 1) * 0x2000);
                                        dat  = (pc1512->vram[ca]          << 8) | pc1512->vram[ca + 1];
                                        dat2 = (pc1512->vram[ca + 0x4000] << 8) | pc1512->vram[ca + 0x4001];
                                        dat3 = (pc1512->vram[ca + 0x8000] << 8) | pc1512->vram[ca + 0x8001];
                                        dat4 = (pc1512->vram[ca + 0xc000] << 8) | pc1512->vram[ca + 0xc001];

                                        pc1512->ma++;
                                        for (c = 0; c < 16; c++)
                                        {
                                                buffer->line[pc1512->displine][(x << 4) + c + 8] = (((dat >> 15) | ((dat2 >> 15) << 1) | ((dat3 >> 15) << 2) | ((dat4 >> 15) << 3)) & (pc1512->cgacol & 15)) + 16;
                                                dat  <<= 1;
                                                dat2 <<= 1;
                                                dat3 <<= 1;
                                                dat4 <<= 1;
                                        }
                                }
                        }
                }
                else
                {
                        cols[0] = ((pc1512->cgamode & 0x12) == 0x12) ? 0 : (pc1512->cgacol & 15) + 16;
                        if (pc1512->cgamode & 1) hline(buffer, 0, pc1512->displine, (pc1512->crtc[1] << 3) + 16, cols[0]);
                        else                     hline(buffer, 0, pc1512->displine, (pc1512->crtc[1] << 4) + 16, cols[0]);
                }

                pc1512->sc = oldsc;
                if (pc1512->vsynctime)
                   pc1512->stat |= 8;
                pc1512->displine++;
                if (pc1512->displine >= 360) 
                        pc1512->displine = 0;
        }
        else
        {
                pc1512->vidtime += pc1512->dispontime;
                if ((pc1512->lastline - pc1512->firstline) == 199) 
                        pc1512->dispon = 0; /*Amstrad PC1512 always displays 200 lines, regardless of CRTC settings*/
                if (pc1512->dispon) 
                        pc1512->stat &= ~1;
                pc1512->linepos = 0;
                if (pc1512->vsynctime)
                {
                        pc1512->vsynctime--;
                        if (!pc1512->vsynctime)
                           pc1512->stat &= ~8;
                }
                if (pc1512->sc == (pc1512->crtc[11] & 31)) 
                { 
                        pc1512->con = 0; 
                        pc1512->coff = 1; 
                }
                if (pc1512->vadj)
                {
                        pc1512->sc++;
                        pc1512->sc &= 31;
                        pc1512->ma = pc1512->maback;
                        pc1512->vadj--;
                        if (!pc1512->vadj)
                        {
                                pc1512->dispon = 1;
                                pc1512->ma = pc1512->maback = (pc1512->crtc[13] | (pc1512->crtc[12] << 8)) & 0x3fff;
                                pc1512->sc = 0;
                        }
                }
                else if (pc1512->sc == pc1512->crtc[9])
                {
                        pc1512->maback = pc1512->ma;
                        pc1512->sc = 0;
                        pc1512->vc++;
                        pc1512->vc &= 127;

                        if (pc1512->displine == 32)
                        {
                                pc1512->vc = 0;
                                pc1512->vadj = 6;
                                if ((pc1512->crtc[10] & 0x60) == 0x20) pc1512->cursoron = 0;
                                else                                   pc1512->cursoron = pc1512->blink & 16;
                        }

                        if (pc1512->displine >= 262)
                        {
                                pc1512->dispon = 0;
                                pc1512->displine = 0;
                                pc1512->vsynctime = 46;

                                if (pc1512->cgamode&1) x = (pc1512->crtc[1] << 3) + 16;
                                else                   x = (pc1512->crtc[1] << 4) + 16;
                                x = 640 + 16;
                                pc1512->lastline++;
                                
                                if (x != xsize || (pc1512->lastline - pc1512->firstline) != ysize)
                                {
                                        xsize = x;
                                        ysize = pc1512->lastline - pc1512->firstline;
                                        if (xsize < 64) xsize = 656;
                                        if (ysize < 32) ysize = 200;
                                        updatewindowsize(xsize, (ysize << 1) + 16);
                                }

                                video_blit_memtoscreen_8(0, pc1512->firstline - 4, xsize, (pc1512->lastline - pc1512->firstline) + 8);

                                video_res_x = xsize - 16;
                                video_res_y = ysize;
                                if (pc1512->cgamode & 1)
                                {
                                        video_res_x /= 8;
                                        video_res_y /= pc1512->crtc[9] + 1;
                                        video_bpp = 0;
                                }
                                else if (!(pc1512->cgamode & 2))
                                {
                                        video_res_x /= 16;
                                        video_res_y /= pc1512->crtc[9] + 1;
                                        video_bpp = 0;
                                }
                                else if (!(pc1512->cgamode & 16))
                                {
                                        video_res_x /= 2;
                                        video_bpp = 2;
                                }
                                else
                                {
                                        video_bpp = 4;
                                }

                                pc1512->firstline = 1000;
                                pc1512->lastline = 0;
                                pc1512->blink++;
                        }
                }
                else
                {
                        pc1512->sc++;
                        pc1512->sc &= 31;
                        pc1512->ma = pc1512->maback;
                }
                if (pc1512->sc == (pc1512->crtc[10] & 31)) 
                        pc1512->con = 1;
        }
}


static void *pc1512_init(device_t *info)
{
        pc1512_t *pc1512 = malloc(sizeof(pc1512_t));
        memset(pc1512, 0, sizeof(pc1512_t));

        pc1512->vram = malloc(0x10000);
        
        pc1512->cgacol = 7;
        pc1512->cgamode = 0x12;
                
        timer_add(pc1512_poll, &pc1512->vidtime, TIMER_ALWAYS_ENABLED, pc1512);
        mem_mapping_add(&pc1512->mapping, 0xb8000, 0x08000, pc1512_read, NULL, NULL, pc1512_write, NULL, NULL,  NULL, 0, pc1512);
        io_sethandler(0x03d0, 0x0010, pc1512_in, NULL, NULL, pc1512_out, NULL, NULL, pc1512);
	overscan_x = overscan_y = 16;
        return pc1512;
}

static void pc1512_close(void *p)
{
        pc1512_t *pc1512 = (pc1512_t *)p;

        free(pc1512->vram);
        free(pc1512);
}

static void pc1512_speed_changed(void *p)
{
        pc1512_t *pc1512 = (pc1512_t *)p;
        
        pc1512_recalctimings(pc1512);
}

device_t pc1512_device =
{
        "Amstrad PC1512 (video)",
        0, 0,
        pc1512_init,
        pc1512_close,
	NULL,
        NULL,
        pc1512_speed_changed,
        NULL,
        NULL
};
