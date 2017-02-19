/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
/*MDA emulation*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "timer.h"
#include "video.h"
#include "vid_mda.h"
#ifndef __unix
#include "win-cgapal.h"
#endif

typedef struct mda_t
{
        mem_mapping_t mapping;
        
        uint8_t crtc[32];
        int crtcreg;
        
        uint8_t ctrl, stat;
        
        int dispontime, dispofftime;
        int vidtime;
        
        int firstline, lastline;

        int linepos, displine;
        int vc, sc;
        uint16_t ma, maback;
        int con, coff, cursoron;
        int dispon, blink;
        int vsynctime, vadj;

        uint8_t *vram;
} mda_t;

static int mdacols[256][2][2];

void mda_recalctimings(mda_t *mda);

void mda_out(uint16_t addr, uint8_t val, void *p)
{
        mda_t *mda = (mda_t *)p;
        switch (addr)
        {
                case 0x3b0: case 0x3b2: case 0x3b4: case 0x3b6:
                mda->crtcreg = val & 31;
                return;
                case 0x3b1: case 0x3b3: case 0x3b5: case 0x3b7:
                mda->crtc[mda->crtcreg] = val;
                if (mda->crtc[10] == 6 && mda->crtc[11] == 7) /*Fix for Generic Turbo XT BIOS, which sets up cursor registers wrong*/
                {
                        mda->crtc[10] = 0xb;
                        mda->crtc[11] = 0xc;
                }
                mda_recalctimings(mda);
                return;
                case 0x3b8:
                mda->ctrl = val;
                return;
        }
}

uint8_t mda_in(uint16_t addr, void *p)
{
        mda_t *mda = (mda_t *)p;
        switch (addr)
        {
                case 0x3b0: case 0x3b2: case 0x3b4: case 0x3b6:
                return mda->crtcreg;
                case 0x3b1: case 0x3b3: case 0x3b5: case 0x3b7:
                return mda->crtc[mda->crtcreg];
                case 0x3ba:
                return mda->stat | 0xF0;
        }
        return 0xff;
}

void mda_write(uint32_t addr, uint8_t val, void *p)
{
        mda_t *mda = (mda_t *)p;
        egawrites++;
	/* Horrible hack, I know, but it's the only way to fix the 440FX BIOS filling the VRAM with garbage until Tom fixes the memory emulation. */
	if ((cs == 0xE0000) && (cpu_state.pc == 0xBF2F) && (romset == ROM_440FX))  return;
	if ((cs == 0xE0000) && (cpu_state.pc == 0xBF77) && (romset == ROM_440FX))  return;
        mda->vram[addr & 0xfff] = val;
}

uint8_t mda_read(uint32_t addr, void *p)
{
        mda_t *mda = (mda_t *)p;
        egareads++;
        return mda->vram[addr & 0xfff];
}

void mda_recalctimings(mda_t *mda)
{
	double _dispontime, _dispofftime, disptime;
        disptime = mda->crtc[0] + 1;
        _dispontime = mda->crtc[1];
        _dispofftime = disptime - _dispontime;
        _dispontime *= MDACONST;
        _dispofftime *= MDACONST;
	mda->dispontime = (int)(_dispontime * (1 << TIMER_SHIFT));
	mda->dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT));
}

void mda_poll(void *p)
{
        mda_t *mda = (mda_t *)p;
        uint16_t ca = (mda->crtc[15] | (mda->crtc[14] << 8)) & 0x3fff;
        int drawcursor;
        int x, c;
        int oldvc;
        uint8_t chr, attr;
        int cols[4];
        int oldsc;
        int blink;
        if (!mda->linepos)
        {
                mda->vidtime += mda->dispofftime;
                mda->stat |= 1;
                mda->linepos = 1;
                oldsc = mda->sc;
                if ((mda->crtc[8] & 3) == 3) 
                        mda->sc = (mda->sc << 1) & 7;
                if (mda->dispon)
                {
                        if (mda->displine < mda->firstline)
                        {
                                mda->firstline = mda->displine;
                        }
                        mda->lastline = mda->displine;
                        cols[0] = 0;
                        cols[1] = 7;
                        for (x = 0; x < mda->crtc[1]; x++)
                        {
                                chr  = mda->vram[(mda->ma << 1) & 0x3fff];
                                attr = mda->vram[((mda->ma << 1) + 1) & 0x3fff];
                                drawcursor = ((mda->ma == ca) && mda->con && mda->cursoron);
                                blink = ((mda->blink & 16) && (mda->ctrl & 0x20) && (attr & 0x80) && !drawcursor);
                                if (mda->sc == 12 && ((attr & 7) == 1))
                                {
                                        for (c = 0; c < 9; c++)
                                            buffer->line[mda->displine][(x * 9) + c] = mdacols[attr][blink][1];
                                }
                                else
                                {
                                        for (c = 0; c < 8; c++)
                                            buffer->line[mda->displine][(x * 9) + c] = mdacols[attr][blink][(fontdatm[chr][mda->sc] & (1 << (c ^ 7))) ? 1 : 0];
                                        if ((chr & ~0x1f) == 0xc0) buffer->line[mda->displine][(x * 9) + 8] = mdacols[attr][blink][fontdatm[chr][mda->sc] & 1];
                                        else                       buffer->line[mda->displine][(x * 9) + 8] = mdacols[attr][blink][0];
                                }
                                mda->ma++;
                                if (drawcursor)
                                {
                                        for (c = 0; c < 9; c++)
                                            buffer->line[mda->displine][(x * 9) + c] ^= mdacols[attr][0][1];
                                }
                        }
                }
                mda->sc = oldsc;
                if (mda->vc == mda->crtc[7] && !mda->sc)
                {
                        mda->stat |= 8;
//                        printf("VSYNC on %i %i\n",vc,sc);
                }
                mda->displine++;
                if (mda->displine >= 500) 
                        mda->displine=0;
        }
        else
        {
                mda->vidtime += mda->dispontime;
                if (mda->dispon) mda->stat&=~1;
                mda->linepos=0;
                if (mda->vsynctime)
                {
                        mda->vsynctime--;
                        if (!mda->vsynctime)
                        {
                                mda->stat&=~8;
//                                printf("VSYNC off %i %i\n",vc,sc);
                        }
                }
                if (mda->sc == (mda->crtc[11] & 31) || ((mda->crtc[8] & 3) == 3 && mda->sc == ((mda->crtc[11] & 31) >> 1))) 
                { 
                        mda->con = 0; 
                        mda->coff = 1; 
                }
                if (mda->vadj)
                {
                        mda->sc++;
                        mda->sc &= 31;
                        mda->ma = mda->maback;
                        mda->vadj--;
                        if (!mda->vadj)
                        {
                                mda->dispon = 1;
                                mda->ma = mda->maback = (mda->crtc[13] | (mda->crtc[12] << 8)) & 0x3fff;
                                mda->sc = 0;
                        }
                }
                else if (mda->sc == mda->crtc[9] || ((mda->crtc[8] & 3) == 3 && mda->sc == (mda->crtc[9] >> 1)))
                {
                        mda->maback = mda->ma;
                        mda->sc = 0;
                        oldvc = mda->vc;
                        mda->vc++;
                        mda->vc &= 127;
                        if (mda->vc == mda->crtc[6]) 
                                mda->dispon=0;
                        if (oldvc == mda->crtc[4])
                        {
//                                printf("Display over at %i\n",displine);
                                mda->vc = 0;
                                mda->vadj = mda->crtc[5];
                                if (!mda->vadj) mda->dispon = 1;
                                if (!mda->vadj) mda->ma = mda->maback = (mda->crtc[13] | (mda->crtc[12] << 8)) & 0x3fff;
                                if ((mda->crtc[10] & 0x60) == 0x20) mda->cursoron = 0;
                                else                                mda->cursoron = mda->blink & 16;
                        }
                        if (mda->vc == mda->crtc[7])
                        {
                                mda->dispon = 0;
                                mda->displine = 0;
                                mda->vsynctime = 16;
                                if (mda->crtc[7])
                                {
//                                        printf("Lastline %i Firstline %i  %i\n",lastline,firstline,lastline-firstline);
                                        x = mda->crtc[1] * 9;
                                        mda->lastline++;
                                        if (x != xsize || (mda->lastline - mda->firstline) != ysize)
                                        {
                                                xsize = x;
                                                ysize = mda->lastline - mda->firstline;
//                                                printf("Resize to %i,%i - R1 %i\n",xsize,ysize,crtcm[1]);
                                                if (xsize < 64) xsize = 656;
                                                if (ysize < 32) ysize = 200;
                                                updatewindowsize(xsize, ysize);
                                        }
                                        video_blit_memtoscreen_8(0, mda->firstline, xsize, mda->lastline - mda->firstline);
                                        frames++;
                                        video_res_x = mda->crtc[1];
                                        video_res_y = mda->crtc[6];
                                        video_bpp = 0;
                                }
                                mda->firstline = 1000;
                                mda->lastline = 0;
                                mda->blink++;
                        }
                }
                else
                {
                        mda->sc++;
                        mda->sc &= 31;
                        mda->ma = mda->maback;
                }
                if ((mda->sc == (mda->crtc[10] & 31) || ((mda->crtc[8] & 3) == 3 && mda->sc == ((mda->crtc[10] & 31) >> 1))))
                {
                        mda->con = 1;
//                        printf("Cursor on - %02X %02X %02X\n",crtcm[8],crtcm[10],crtcm[11]);
                }
        }
}

void *mda_init()
{
        int c;
        mda_t *mda = malloc(sizeof(mda_t));
        memset(mda, 0, sizeof(mda_t));

        mda->vram = malloc(0x1000);

        timer_add(mda_poll, &mda->vidtime, TIMER_ALWAYS_ENABLED, mda);
        mem_mapping_add(&mda->mapping, 0xb0000, 0x08000, mda_read, NULL, NULL, mda_write, NULL, NULL,  NULL, 0, mda);
        io_sethandler(0x03b0, 0x0010, mda_in, NULL, NULL, mda_out, NULL, NULL, mda);

        for (c = 0; c < 256; c++)
        {
                mdacols[c][0][0] = mdacols[c][1][0] = mdacols[c][1][1] = 16;
                if (c & 8) mdacols[c][0][1] = 15 + 16;
                else       mdacols[c][0][1] =  7 + 16;
        }
        mdacols[0x70][0][1] = 16;
        mdacols[0x70][0][0] = mdacols[0x70][1][0] = mdacols[0x70][1][1] = 16 + 15;
        mdacols[0xF0][0][1] = 16;
        mdacols[0xF0][0][0] = mdacols[0xF0][1][0] = mdacols[0xF0][1][1] = 16 + 15;
        mdacols[0x78][0][1] = 16 + 7;
        mdacols[0x78][0][0] = mdacols[0x78][1][0] = mdacols[0x78][1][1] = 16 + 15;
        mdacols[0xF8][0][1] = 16 + 7;
        mdacols[0xF8][0][0] = mdacols[0xF8][1][0] = mdacols[0xF8][1][1] = 16 + 15;
        mdacols[0x00][0][1] = mdacols[0x00][1][1] = 16;
        mdacols[0x08][0][1] = mdacols[0x08][1][1] = 16;
        mdacols[0x80][0][1] = mdacols[0x80][1][1] = 16;
        mdacols[0x88][0][1] = mdacols[0x88][1][1] = 16;

	overscan_x = overscan_y = 0;

#ifndef __unix
        cga_palette = device_get_config_int("rgb_type") << 1;
	if (cga_palette > 6)
	{
		cga_palette = 0;
	}
	cgapal_rebuild();
#endif

        return mda;
}

void mda_close(void *p)
{
        mda_t *mda = (mda_t *)p;

        free(mda->vram);
        free(mda);
}

void mda_speed_changed(void *p)
{
        mda_t *mda = (mda_t *)p;
        
        mda_recalctimings(mda);
}

#ifndef __unix
static device_config_t mda_config[] =
{
        {
                .name = "rgb_type",
                .description = "Display type",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "Default",
                                .value = 0
                        },
                        {
                                .description = "Green",
                                .value = 1
                        },
                        {
                                .description = "Amber",
                                .value = 2
                        },
                        {
                                .description = "Gray",
                                .value = 3
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0
        },
        {
                .type = -1
        }
};
#endif

device_t mda_device =
{
        "MDA",
        0,
        mda_init,
        mda_close,
        NULL,
        mda_speed_changed,
        NULL,
#ifdef __unix
        NULL
#else
        NULL,
	mda_config
#endif
};
