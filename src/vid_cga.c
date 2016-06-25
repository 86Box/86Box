/*CGA emulation*/
#include <stdlib.h>
#include <math.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "timer.h"
#include "video.h"
#include "vid_cga.h"
#include "vid_cga_comp.h"

static int i_filt[8],q_filt[8];

static uint8_t tarray[65536];

int cga_brown;
int cga_color_burst;

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
//        pclog("CGA_OUT %04X %02X\n", addr, val);
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
                if (((cga->cgamode ^ val) & 5) != 0) {
                        cga->cgamode = val;
                        update_cga16_color(cga);
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
//        pclog("CGA_IN %04X\n", addr);
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
//        pclog("CGA_WRITE %04X %02X\n", addr, val);
	/* Horrible hack, I know, but it's the only way to fix the 440FX BIOS filling the VRAM with garbage until Tom fixes the memory emulation. */
	if ((cs == 0xE0000) && (cpu_state.pc == 0xBF2F) && (romset == ROM_440FX))  { egawrites++; return; }
	if ((cs == 0xE0000) && (cpu_state.pc == 0xBF77) && (romset == ROM_440FX))  { egawrites++; return; }
        cga->vram[addr & 0x3fff] = val;
        cga->charbuffer[ ((int)(((cga->dispontime - cga->vidtime) * 2) / CGACONST)) & 0xfc] = val;
        cga->charbuffer[(((int)(((cga->dispontime - cga->vidtime) * 2) / CGACONST)) & 0xfc) | 1] = val;
        egawrites++;
        cycles -= 4;
}

uint8_t cga_read(uint32_t addr, void *p)
{
        cga_t *cga = (cga_t *)p;
        cycles -= 4;        
        cga->charbuffer[ ((int)(((cga->dispontime - cga->vidtime) * 2) / CGACONST)) & 0xfc] = cga->vram[addr & 0x3fff];
        cga->charbuffer[(((int)(((cga->dispontime - cga->vidtime) * 2) / CGACONST)) & 0xfc) | 1] = cga->vram[addr & 0x3fff];
        egareads++;
//        pclog("CGA_READ %04X\n", addr);
        return cga->vram[addr & 0x3fff];
}

void cga_recalctimings(cga_t *cga)
{
        double disptime;
	double _dispontime, _dispofftime;
        pclog("Recalc - %i %i %i\n", cga->crtc[0], cga->crtc[1], cga->cgamode & 1);
        if (cga->cgamode & 1)
        {
                disptime = cga->crtc[0] + 1;
                _dispontime = cga->crtc[1];
        }
        else
        {
                disptime = (cga->crtc[0] + 1) << 1;
                _dispontime = cga->crtc[1] << 1;
        }
        _dispofftime = disptime - _dispontime;
//        printf("%i %f %f %f  %i %i\n",cgamode&1,disptime,dispontime,dispofftime,crtc[0],crtc[1]);
        _dispontime *= CGACONST;
        _dispofftime *= CGACONST;
//        printf("Timings - on %f off %f frame %f second %f\n",dispontime,dispofftime,(dispontime+dispofftime)*262.0,(dispontime+dispofftime)*262.0*59.92);
	cga->dispontime = (int)(_dispontime * (1 << TIMER_SHIFT));
	cga->dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT));
}

static int ntsc_col[8][8]=
{
        {0,0,0,0,0,0,0,0}, /*Black*/
        {0,0,1,1,1,1,0,0}, /*Blue*/
        {1,0,0,0,0,1,1,1}, /*Green*/
        {0,0,0,0,1,1,1,1}, /*Cyan*/
        {1,1,1,1,0,0,0,0}, /*Red*/
        {0,1,1,1,1,0,0,0}, /*Magenta*/
        {1,1,0,0,0,0,1,1}, /*Yellow*/
        {1,1,1,1,1,1,1,1}  /*White*/
};

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
        int y_buf[8] = {0, 0, 0, 0, 0, 0, 0, 0}, y_val, y_tot;
        int i_buf[8] = {0, 0, 0, 0, 0, 0, 0, 0}, i_val, i_tot;
        int q_buf[8] = {0, 0, 0, 0, 0, 0, 0, 0}, q_val, q_tot;
        int r, g, b;
	uint8_t *tline;
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
//                                printf("Firstline %i\n",firstline);
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
                                                    buffer->line[cga->displine][(x << 3) + c + 8] = cols[(fontdat[chr][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[cga->displine][(x << 3) + c + 8] = cols[(fontdat[chr][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
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
                                                    buffer->line[cga->displine][(x << 4)+(c << 1) + 8] = buffer->line[cga->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[cga->displine][(x << 4) + (c << 1) + 8] = buffer->line[cga->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
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

                if (cga_comp)
                {
					tline = (uint8_t *) buffer32->line[cga->displine];

					for (c = 0; c < x; c++)
					{
						tarray[c] = buffer->line[cga->displine][c] & 0xf;
					}

					Composite_Process(cga, 0, x >> 2, tarray);

					for (c = 0; c < x; c++)
					{
						((uint32_t *) tline)[c] = ((uint32_t *) tarray)[c];
					}
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
                                // cga->vsynctime = (cga->crtc[3] >> 4) + 1;
				cga->vsynctime = 16;
                                if (cga->crtc[7])
                                {
                                        if (cga->cgamode & 1) x = (cga->crtc[1] << 3) + 16;
                                        else                  x = (cga->crtc[1] << 4) + 16;
                                        cga->lastline++;
                                        if (x != xsize || (cga->lastline - cga->firstline) != ysize)
                                        {
                                                xsize = x;
                                                ysize = cga->lastline - cga->firstline;
                                                if (xsize < 64) xsize = 656;
                                                if (ysize < 32) ysize = 200;
                                                updatewindowsize(xsize, (ysize << 1) + 16);
                                        }
                                        
startblit();
                                        if (cga_comp) 
                                           video_blit_memtoscreen(0, cga->firstline - 4, 0, (cga->lastline - cga->firstline) + 8, xsize, (cga->lastline - cga->firstline) + 8);
                                        else          
                                           video_blit_memtoscreen_8(0, cga->firstline - 4, xsize, (cga->lastline - cga->firstline) + 8);
                                        frames++;
endblit();
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
                if (cga->cgadispon) cga->cgastat &= ~1;
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
}

void *cga_standalone_init()
{
        int c;
        int cga_tint = -2;
        cga_t *cga = malloc(sizeof(cga_t));
        memset(cga, 0, sizeof(cga_t));

        cga->vram = malloc(0x4000);
                
		cga_comp_init(cga);
        timer_add(cga_poll, &cga->vidtime, TIMER_ALWAYS_ENABLED, cga);
        mem_mapping_add(&cga->mapping, 0xb8000, 0x08000, cga_read, NULL, NULL, cga_write, NULL, NULL,  NULL, 0, cga);
        io_sethandler(0x03d0, 0x0010, cga_in, NULL, NULL, cga_out, NULL, NULL, cga);
		
		for (c = 0; c < 8192; c++)
		{
			((uint64_t *) tarray)[c] = 0;
		}

		overscan_x = overscan_y = 16;
		
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

device_t cga_device =
{
        "CGA (Old)",
        0,
        cga_standalone_init,
        cga_close,
        NULL,
        cga_speed_changed,
        NULL,
        NULL
};

device_t cga_new_device =
{
        "CGA (New)",
        0,
        cga_standalone_init,
        cga_close,
        NULL,
        cga_speed_changed,
        NULL,
        NULL
};
