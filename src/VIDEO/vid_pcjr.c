#include <stdlib.h>
#include <math.h>
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../pic.h"
#include "../timer.h"
#include "../device.h"
#include "video.h"
#include "vid_cga_comp.h"
#include "vid_pcjr.h"


#define PCJR_RGB 0
#define PCJR_COMPOSITE 1


typedef struct pcjr_t
{
        mem_mapping_t mapping;
        
        uint8_t crtc[32];
        int crtcreg;
        
        int      array_index;
        uint8_t  array[32];
        int      array_ff;
        int      memctrl;
        uint8_t  stat;
        int addr_mode;
        
        uint8_t *vram, *b8000;

        int linepos, displine;
        int sc, vc;
        int dispon;
        int con, coff, cursoron, blink;
        int vsynctime, vadj;
        uint16_t ma, maback;
        
        int dispontime, dispofftime, vidtime;
        int firstline, lastline;
        
        int composite;
} pcjr_t;

static uint8_t crtcmask[32] = 
{
        0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void pcjr_recalcaddress(pcjr_t *pcjr);
void pcjr_recalctimings(pcjr_t *pcjr);
        
void pcjr_out(uint16_t addr, uint8_t val, void *p)
{
        pcjr_t *pcjr = (pcjr_t *)p;
        uint8_t old;
        switch (addr)
        {
                case 0x3d4:
                pcjr->crtcreg = val & 0x1f;
                return;
                case 0x3d5:
                old = pcjr->crtc[pcjr->crtcreg];
                pcjr->crtc[pcjr->crtcreg] = val & crtcmask[pcjr->crtcreg];
                if (old != val)
                {
                        if (pcjr->crtcreg < 0xe || pcjr->crtcreg > 0x10)
                        {
                                fullchange = changeframecount;
                                pcjr_recalctimings(pcjr);
                        }
                }
                return;
                case 0x3da:
                if (!pcjr->array_ff)
                        pcjr->array_index = val & 0x1f;
                else
                {
                        if (pcjr->array_index & 0x10)
                                val &= 0x0f;
                        pcjr->array[pcjr->array_index & 0x1f] = val;
                        if (!(pcjr->array_index & 0x1f))
                                update_cga16_color(val);
                }
                pcjr->array_ff = !pcjr->array_ff;
                break;
                case 0x3df:
                pcjr->memctrl = val;
                pcjr->addr_mode = val >> 6;
                pcjr_recalcaddress(pcjr);
                break;
        }
}

uint8_t pcjr_in(uint16_t addr, void *p)
{
        pcjr_t *pcjr = (pcjr_t *)p;
        switch (addr)
        {
                case 0x3d4:
                return pcjr->crtcreg;
                case 0x3d5:
                return pcjr->crtc[pcjr->crtcreg];
                case 0x3da:
                pcjr->array_ff = 0;
                pcjr->stat ^= 0x10;
                return pcjr->stat;
        }
        return 0xFF;
}

void pcjr_recalcaddress(pcjr_t *pcjr)
{
        if ((pcjr->memctrl & 0xc0) == 0xc0)
        {
                pcjr->vram  = &ram[(pcjr->memctrl & 0x06) << 14];
                pcjr->b8000 = &ram[(pcjr->memctrl & 0x30) << 11];
        }
        else
        {
                pcjr->vram  = &ram[(pcjr->memctrl & 0x07) << 14];
                pcjr->b8000 = &ram[(pcjr->memctrl & 0x38) << 11];
        }
}

void pcjr_write(uint32_t addr, uint8_t val, void *p)
{
        pcjr_t *pcjr = (pcjr_t *)p;
        if (pcjr->memctrl == -1) 
                return;
                
        egawrites++;
        pcjr->b8000[addr & 0x3fff] = val;
}

uint8_t pcjr_read(uint32_t addr, void *p)
{
        pcjr_t *pcjr = (pcjr_t *)p;
        if (pcjr->memctrl == -1) 
                return 0xff;
                
        egareads++;
        return pcjr->b8000[addr & 0x3fff];
}

void pcjr_recalctimings(pcjr_t *pcjr)
{
	double _dispontime, _dispofftime, disptime;
        if (pcjr->array[0] & 1)
        {
                disptime = pcjr->crtc[0] + 1;
                _dispontime = pcjr->crtc[1];
        }
        else
        {
                disptime = (pcjr->crtc[0] + 1) << 1;
                _dispontime = pcjr->crtc[1] << 1;
        }
        _dispofftime = disptime - _dispontime;
        _dispontime  *= CGACONST;
        _dispofftime *= CGACONST;
	pcjr->dispontime  = (int)(_dispontime  * (1 << TIMER_SHIFT));
	pcjr->dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT));
}


void pcjr_poll(void *p)
{
        pcjr_t *pcjr = (pcjr_t *)p;
        uint16_t ca = (pcjr->crtc[15] | (pcjr->crtc[14] << 8)) & 0x3fff;
        int drawcursor;
        int x, c;
        int oldvc;
        uint8_t chr, attr;
        uint16_t dat;
        int cols[4];
        int oldsc;
        if (!pcjr->linepos)
        {
                pcjr->vidtime += pcjr->dispofftime;
                pcjr->stat &= ~1;
                pcjr->linepos = 1;
                oldsc = pcjr->sc;
                if ((pcjr->crtc[8] & 3) == 3) 
                        pcjr->sc = (pcjr->sc << 1) & 7;
                if (pcjr->dispon)
                {
                        uint16_t offset = 0;
                        uint16_t mask = 0x1fff;
                        
                        if (pcjr->displine < pcjr->firstline)
                        {
                                pcjr->firstline = pcjr->displine;
                                video_wait_for_buffer();
                        }
                        pcjr->lastline = pcjr->displine;
                        cols[0] = (pcjr->array[2] & 0xf) + 16;
                        for (c = 0; c < 8; c++)
                        {
                                buffer->line[pcjr->displine][c] = cols[0];
                                if (pcjr->array[0] & 1) buffer->line[pcjr->displine][c + (pcjr->crtc[1] << 3) + 8] = cols[0];
                                else                    buffer->line[pcjr->displine][c + (pcjr->crtc[1] << 4) + 8] = cols[0];
                        }
                        
                        switch (pcjr->addr_mode)
                        {
                                case 0: /*Alpha*/
                                offset = 0;
                                mask = 0x3fff;
                                break;
                                case 1: /*Low resolution graphics*/
                                offset = (pcjr->sc & 1) * 0x2000;
                                break;
                                case 3: /*High resolution graphics*/
                                offset = (pcjr->sc & 3) * 0x2000;
                                break;
                        }
                        switch ((pcjr->array[0] & 0x13) | ((pcjr->array[3] & 0x08) << 5))
                        {
                                case 0x13: /*320x200x16*/
                                for (x = 0; x < pcjr->crtc[1]; x++)
                                {
                                        dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) | 
                                               pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                                        pcjr->ma++;
                                        buffer->line[pcjr->displine][(x << 3) + 8]  = 
                                        buffer->line[pcjr->displine][(x << 3) + 9]  = pcjr->array[((dat >> 12) & pcjr->array[1]) + 16] + 16;
                                        buffer->line[pcjr->displine][(x << 3) + 10] = 
                                        buffer->line[pcjr->displine][(x << 3) + 11] = pcjr->array[((dat >>  8) & pcjr->array[1]) + 16] + 16;
                                        buffer->line[pcjr->displine][(x << 3) + 12] = 
                                        buffer->line[pcjr->displine][(x << 3) + 13] = pcjr->array[((dat >>  4) & pcjr->array[1]) + 16] + 16;
                                        buffer->line[pcjr->displine][(x << 3) + 14] = 
                                        buffer->line[pcjr->displine][(x << 3) + 15] = pcjr->array[(dat         & pcjr->array[1]) + 16] + 16;
                                }
                                break;
                                case 0x12: /*160x200x16*/
                                for (x = 0; x < pcjr->crtc[1]; x++)
                                {
                                        dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) | 
                                               pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                                        pcjr->ma++;
                                        buffer->line[pcjr->displine][(x << 4) + 8]  = 
                                        buffer->line[pcjr->displine][(x << 4) + 9]  = 
                                        buffer->line[pcjr->displine][(x << 4) + 10] =
                                        buffer->line[pcjr->displine][(x << 4) + 11] = pcjr->array[((dat >> 12) & pcjr->array[1]) + 16] + 16;
                                        buffer->line[pcjr->displine][(x << 4) + 12] = 
                                        buffer->line[pcjr->displine][(x << 4) + 13] =
                                        buffer->line[pcjr->displine][(x << 4) + 14] =
                                        buffer->line[pcjr->displine][(x << 4) + 15] = pcjr->array[((dat >>  8) & pcjr->array[1]) + 16] + 16;
                                        buffer->line[pcjr->displine][(x << 4) + 16] = 
                                        buffer->line[pcjr->displine][(x << 4) + 17] =
                                        buffer->line[pcjr->displine][(x << 4) + 18] =
                                        buffer->line[pcjr->displine][(x << 4) + 19] = pcjr->array[((dat >>  4) & pcjr->array[1]) + 16] + 16;
                                        buffer->line[pcjr->displine][(x << 4) + 20] = 
                                        buffer->line[pcjr->displine][(x << 4) + 21] =
                                        buffer->line[pcjr->displine][(x << 4) + 22] =
                                        buffer->line[pcjr->displine][(x << 4) + 23] = pcjr->array[(dat         & pcjr->array[1]) + 16] + 16;
                                }
                                break;
                                case 0x03: /*640x200x4*/
                                for (x = 0; x < pcjr->crtc[1]; x++)
                                {
                                        dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) |
                                               pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                                        pcjr->ma++;
                                        for (c = 0; c < 8; c++)
                                        {
                                                chr  =  (dat >>  7) & 1;
                                                chr |= ((dat >> 14) & 2);
                                                buffer->line[pcjr->displine][(x << 3) + 8 + c] = pcjr->array[(chr & pcjr->array[1]) + 16] + 16;
                                                dat <<= 1;
                                        }
                                }
                                break;
                                case 0x01: /*80 column text*/
                                for (x = 0; x < pcjr->crtc[1]; x++)
                                {
                                        chr  = pcjr->vram[((pcjr->ma << 1) & mask) + offset];
                                        attr = pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                                        drawcursor = ((pcjr->ma == ca) && pcjr->con && pcjr->cursoron);
                                        if (pcjr->array[3] & 4)
                                        {
                                                cols[1] = pcjr->array[ ((attr & 15)      & pcjr->array[1]) + 16] + 16;
                                                cols[0] = pcjr->array[(((attr >> 4) & 7) & pcjr->array[1]) + 16] + 16;
                                                if ((pcjr->blink & 16) && (attr & 0x80) && !drawcursor) 
                                                        cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = pcjr->array[((attr & 15) & pcjr->array[1]) + 16] + 16;
                                                cols[0] = pcjr->array[((attr >> 4) & pcjr->array[1]) + 16] + 16;
                                        }
                                        if (pcjr->sc & 8)
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[pcjr->displine][(x << 3) + c + 8] = cols[0];
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[pcjr->displine][(x << 3) + c + 8] = cols[(fontdat[chr][pcjr->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[pcjr->displine][(x << 3) + c + 8] ^= 15;
                                        }
                                        pcjr->ma++;
                                }
                                break;
                                case 0x00: /*40 column text*/
                                for (x = 0; x < pcjr->crtc[1]; x++)
                                {
                                        chr  = pcjr->vram[((pcjr->ma << 1) & mask) + offset];
                                        attr = pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                                        drawcursor = ((pcjr->ma == ca) && pcjr->con && pcjr->cursoron);
                                        if (pcjr->array[3] & 4)
                                        {
                                                cols[1] = pcjr->array[ ((attr & 15)      & pcjr->array[1]) + 16] + 16;
                                                cols[0] = pcjr->array[(((attr >> 4) & 7) & pcjr->array[1]) + 16] + 16;
                                                if ((pcjr->blink & 16) && (attr & 0x80) && !drawcursor) 
                                                        cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = pcjr->array[((attr & 15) & pcjr->array[1]) + 16] + 16;
                                                cols[0] = pcjr->array[((attr >> 4) & pcjr->array[1]) + 16] + 16;
                                        }
                                        pcjr->ma++;
                                        if (pcjr->sc & 8)
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[pcjr->displine][(x << 4) + (c << 1) + 8] = 
                                                    buffer->line[pcjr->displine][(x << 4) + (c << 1) + 1 + 8] = cols[0];
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[pcjr->displine][(x << 4) + (c << 1) + 8] = 
                                                    buffer->line[pcjr->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][pcjr->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 16; c++)
                                                    buffer->line[pcjr->displine][(x << 4) + c + 8] ^= 15;
                                        }
                                }
                                break;
                                case 0x02: /*320x200x4*/
                                cols[0] = pcjr->array[0 + 16] + 16;
                                cols[1] = pcjr->array[1 + 16] + 16;
                                cols[2] = pcjr->array[2 + 16] + 16;
                                cols[3] = pcjr->array[3 + 16] + 16;
                                for (x = 0; x < pcjr->crtc[1]; x++)
                                {
                                        dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) | 
                                               pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                                        pcjr->ma++;
                                        for (c = 0; c < 8; c++)
                                        {
                                                buffer->line[pcjr->displine][(x << 4) + (c << 1) + 8] =
                                                buffer->line[pcjr->displine][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
                                                dat <<= 2;
                                        }
                                }
                                break;
                                case 0x102: /*640x200x2*/
                                cols[0] = pcjr->array[0 + 16] + 16;
                                cols[1] = pcjr->array[1 + 16] + 16;
                                for (x = 0; x < pcjr->crtc[1]; x++)
                                {
                                        dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) |
                                               pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                                        pcjr->ma++;
                                        for (c = 0; c < 16; c++)
                                        {
                                                buffer->line[pcjr->displine][(x << 4) + c + 8] = cols[dat >> 15];
                                                dat <<= 1;
                                        }
                                }
                                break;
                        }
                }
                else
                {
                        if (pcjr->array[3] & 4)
                        {
                                if (pcjr->array[0] & 1) hline(buffer, 0, pcjr->displine, (pcjr->crtc[1] << 3) + 16, (pcjr->array[2] & 0xf) + 16);
                                else                 hline(buffer, 0, pcjr->displine, (pcjr->crtc[1] << 4) + 16, (pcjr->array[2] & 0xf) + 16);
                        }
                        else
                        {
                                cols[0] = pcjr->array[0 + 16] + 16;
                                if (pcjr->array[0] & 1) hline(buffer, 0, pcjr->displine, (pcjr->crtc[1] << 3) + 16, cols[0]);
                                else                 hline(buffer, 0, pcjr->displine, (pcjr->crtc[1] << 4) + 16, cols[0]);
                        }
                }
                if (pcjr->array[0] & 1) x = (pcjr->crtc[1] << 3) + 16;
                else                    x = (pcjr->crtc[1] << 4) + 16;
                if (pcjr->composite)
                {
			for (c = 0; c < x; c++)
				buffer32->line[pcjr->displine][c] = buffer->line[pcjr->displine][c] & 0xf;

			Composite_Process(pcjr->array[0], 0, x >> 2, buffer32->line[pcjr->displine]);
                }
                pcjr->sc = oldsc;
                if (pcjr->vc == pcjr->crtc[7] && !pcjr->sc)
                {
                        pcjr->stat |= 8;
                }
                pcjr->displine++;
                if (pcjr->displine >= 360) 
                        pcjr->displine = 0;
        }
        else
        {
                pcjr->vidtime += pcjr->dispontime;
                if (pcjr->dispon) 
                        pcjr->stat |= 1;
                pcjr->linepos = 0;
                if (pcjr->vsynctime)
                {
                        pcjr->vsynctime--;
                        if (!pcjr->vsynctime)
                        {
                                pcjr->stat &= ~8;
                        }
                }
                if (pcjr->sc == (pcjr->crtc[11] & 31) || ((pcjr->crtc[8] & 3) == 3 && pcjr->sc == ((pcjr->crtc[11] & 31) >> 1))) 
                { 
                        pcjr->con = 0; 
                        pcjr->coff = 1; 
                }
                if (pcjr->vadj)
                {
                        pcjr->sc++;
                        pcjr->sc &= 31;
                        pcjr->ma = pcjr->maback;
                        pcjr->vadj--;
                        if (!pcjr->vadj)
                        {
                                pcjr->dispon = 1;
                                pcjr->ma = pcjr->maback = (pcjr->crtc[13] | (pcjr->crtc[12] << 8)) & 0x3fff;
                                pcjr->sc = 0;
                        }
                }
                else if (pcjr->sc == pcjr->crtc[9] || ((pcjr->crtc[8] & 3) == 3 && pcjr->sc == (pcjr->crtc[9] >> 1)))
                {
                        pcjr->maback = pcjr->ma;
                        pcjr->sc = 0;
                        oldvc = pcjr->vc;
                        pcjr->vc++;
                        pcjr->vc &= 127;
                        if (pcjr->vc == pcjr->crtc[6]) 
                                pcjr->dispon = 0;
                        if (oldvc == pcjr->crtc[4])
                        {
                                pcjr->vc = 0;
                                pcjr->vadj = pcjr->crtc[5];
                                if (!pcjr->vadj) 
                                        pcjr->dispon = 1;
                                if (!pcjr->vadj) 
                                        pcjr->ma = pcjr->maback = (pcjr->crtc[13] | (pcjr->crtc[12] << 8)) & 0x3fff;
                                if ((pcjr->crtc[10] & 0x60) == 0x20) pcjr->cursoron = 0;
                                else                                  pcjr->cursoron = pcjr->blink & 16;
                        }
                        if (pcjr->vc == pcjr->crtc[7])
                        {
                                pcjr->dispon = 0;
                                pcjr->displine = 0;
                                pcjr->vsynctime = 16;
                                picint(1 << 5);
                                if (pcjr->crtc[7])
                                {
                                        if (pcjr->array[0] & 1) x = (pcjr->crtc[1] << 3) + 16;
                                        else                    x = (pcjr->crtc[1] << 4) + 16;
                                        pcjr->lastline++;
                                        if (x != xsize || (pcjr->lastline - pcjr->firstline) != ysize)
                                        {
                                                xsize = x;
                                                ysize = pcjr->lastline - pcjr->firstline;
                                                if (xsize < 64) xsize = 656;
                                                if (ysize < 32) ysize = 200;
                                                updatewindowsize(xsize, (ysize << 1) + 16);
                                        }

                                        if (pcjr->composite) 
                                           video_blit_memtoscreen(0, pcjr->firstline-4, 0, (pcjr->lastline - pcjr->firstline) + 8, xsize, (pcjr->lastline - pcjr->firstline) + 8);
                                        else          
                                           video_blit_memtoscreen_8(0, pcjr->firstline-4, xsize, (pcjr->lastline - pcjr->firstline) + 8);

                                        frames++;
                                        video_res_x = xsize - 16;
                                        video_res_y = ysize;
                                }
                                pcjr->firstline = 1000;
                                pcjr->lastline = 0;
                                pcjr->blink++;
                        }
                }
                else
                {
                        pcjr->sc++;
                        pcjr->sc &= 31;
                        pcjr->ma = pcjr->maback;
                }
                if ((pcjr->sc == (pcjr->crtc[10] & 31) || ((pcjr->crtc[8] & 3) == 3 && pcjr->sc == ((pcjr->crtc[10] & 31) >> 1)))) 
                        pcjr->con = 1;
        }
}

static void *pcjr_video_init()
{
        int display_type;
        pcjr_t *pcjr = malloc(sizeof(pcjr_t));
        memset(pcjr, 0, sizeof(pcjr_t));

        display_type = model_get_config_int("display_type");
        pcjr->composite = (display_type != PCJR_RGB);

        pcjr->memctrl = -1;
        
        timer_add(pcjr_poll, &pcjr->vidtime, TIMER_ALWAYS_ENABLED, pcjr);
        mem_mapping_add(&pcjr->mapping, 0xb8000, 0x08000, pcjr_read, NULL, NULL, pcjr_write, NULL, NULL,  NULL, 0, pcjr);
        io_sethandler(0x03d0, 0x0010, pcjr_in, NULL, NULL, pcjr_out, NULL, NULL, pcjr);
        return pcjr;
}

static void pcjr_video_close(void *p)
{
        pcjr_t *pcjr = (pcjr_t *)p;

        free(pcjr);
}

static void pcjr_speed_changed(void *p)
{
        pcjr_t *pcjr = (pcjr_t *)p;
        
        pcjr_recalctimings(pcjr);
}

device_t pcjr_video_device =
{
        "IBM PCjr (video)",
        0,
        pcjr_video_init,
        pcjr_video_close,
        NULL,
        pcjr_speed_changed,
        NULL,
        NULL
};

static device_config_t pcjr_config[] =
{
        {
                "display_type", "Display type", CONFIG_SELECTION, "", PCJR_RGB,
                {
                        {
                                "RGB", PCJR_RGB
                        },
                        {
                                "Composite", PCJR_COMPOSITE
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

/*This isn't really a device as such - more of a convenient way to hook in the
  config information*/
device_t pcjr_device =
{
        "IBM PCjr",
        0,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        pcjr_config
};
