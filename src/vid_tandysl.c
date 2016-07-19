#include <stdlib.h>
#include <math.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "timer.h"
#include "video.h"
#include "vid_tandysl.h"

typedef struct tandysl_t
{
        mem_mapping_t mapping;
        mem_mapping_t ram_mapping;
        mem_mapping_t vram_mapping;
        
        uint8_t crtc[32];
        int crtcreg;
        
        int      array_index;
        uint8_t  array[32];
        int      memctrl;//=-1;
        uint32_t base;
        uint8_t  mode, col;
        uint8_t  stat;
        
        uint8_t *vram, *b8000;
        uint32_t b8000_limit;
        uint8_t planar_ctrl;

        int linepos, displine;
        int sc, vc;
        int dispon;
        int con, coff, cursoron, blink;
        int vsynctime, vadj;
        uint16_t ma, maback;
        
        int dispontime, dispofftime;
	int64_t vidtime;
        int firstline, lastline;
} tandysl_t;

static uint8_t crtcmask[32] = 
{
        0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void tandysl_recalcaddress(tandysl_t *tandy);
static void tandysl_recalctimings(tandysl_t *tandy);
static void tandysl_recalcmapping(tandysl_t *tandy);
static uint8_t tandysl_in(uint16_t addr, void *p);
static void tandysl_ram_write(uint32_t addr, uint8_t val, void *p);
static void tandysl_write(uint32_t addr, uint8_t val, void *p);

static void tandysl_out(uint16_t addr, uint8_t val, void *p)
{
        tandysl_t *tandy = (tandysl_t *)p;
        uint8_t old;
//        pclog("TandySL OUT %04X %02X\n",addr,val);
        switch (addr)
        {
                case 0x3d4:
                tandy->crtcreg = val & 0x1f;
                return;
                case 0x3d5:
//                pclog("Write CRTC R%02x %02x  ",tandy->crtcreg, val);
                old = tandy->crtc[tandy->crtcreg];
                tandy->crtc[tandy->crtcreg] = val & crtcmask[tandy->crtcreg];
//                pclog("now %02x\n", tandy->crtc[tandy->crtcreg]);
                if (old != val)
                {
                        if (tandy->crtcreg < 0xe || tandy->crtcreg > 0x10)
                        {
                                fullchange = changeframecount;
                                tandysl_recalctimings(tandy);
                        }
                }
                return;
                case 0x3d8:
                tandy->mode = val;
                return;
                case 0x3d9:
                tandy->col = val;
                return;
                case 0x3da:
                tandy->array_index = val & 0x1f;
                break;
                case 0x3de:
                if (tandy->array_index & 16) 
                        val &= 0xf;
                tandy->array[tandy->array_index & 0x1f] = val;
                if ((tandy->array_index & 0x1f) == 5)
                {
                        tandysl_recalcmapping(tandy);
                        tandysl_recalcaddress(tandy);
                }
                break;
                case 0x3df:
                tandy->memctrl = val;
//                pclog("tandy 3df write %02x\n", val);
                tandysl_recalcaddress(tandy);
                break;
                case 0x65:
                if (val == 8) /*Hack*/
                        return;
                tandy->planar_ctrl = val;
                tandysl_recalcmapping(tandy);
                break;
                case 0xffe8:
                if ((val & 0xe) == 0xe)
                        mem_mapping_disable(&tandy->ram_mapping);
                else
                        mem_mapping_set_addr(&tandy->ram_mapping, ((val >> 1) & 7) * 128 * 1024, 0x20000);
                tandysl_recalcaddress(tandy);
                break;
        }
}

static uint8_t tandysl_in(uint16_t addr, void *p)
{
        tandysl_t *tandy = (tandysl_t *)p;
//        if (addr!=0x3DA) pclog("Tandy IN %04X\n",addr);
        switch (addr)
        {
                case 0x3d4:
                return tandy->crtcreg;
                case 0x3d5:
                return tandy->crtc[tandy->crtcreg];
                case 0x3da:
                return tandy->stat;
        }
//        pclog("Bad Tandy IN %04x\n", addr);
        return 0xFF;
}

static void tandysl_recalcaddress(tandysl_t *tandy)
{
        tandy->b8000_limit = 0x8000;
        if (tandy->array[5] & 1)
        {
                tandy->vram  = &ram[((tandy->memctrl & 0x04) << 14) + tandy->base];
                tandy->b8000 = &ram[((tandy->memctrl & 0x20) << 11) + tandy->base];
        }
        else if ((tandy->memctrl & 0xc0) == 0xc0)
        {
                tandy->vram  = &ram[((tandy->memctrl & 0x06) << 14) + tandy->base];
                tandy->b8000 = &ram[((tandy->memctrl & 0x30) << 11) + tandy->base];
//                printf("VRAM at %05X B8000 at %05X\n",((tandy->memctrl&0x6)<<14)+tandy->base,((tandy->memctrl&0x30)<<11)+tandy->base);
        }
        else
        {
                tandy->vram  = &ram[((tandy->memctrl & 0x07) << 14) + tandy->base];
                tandy->b8000 = &ram[((tandy->memctrl & 0x38) << 11) + tandy->base];
//                printf("VRAM at %05X B8000 at %05X\n",((tandy->memctrl&0x7)<<14)+tandy->base,((tandy->memctrl&0x38)<<11)+tandy->base);
                if ((tandy->memctrl & 0x38) == 0x38)
                        tandy->b8000_limit = 0x4000;
        }
}

static void tandysl_recalcmapping(tandysl_t *tandy)
{
        mem_mapping_disable(&tandy->mapping);
        io_removehandler(0x03d0, 0x0010, tandysl_in, NULL, NULL, tandysl_out, NULL, NULL, tandy);
        if (tandy->planar_ctrl & 4)
        {
//                pclog("Enable VRAM mapping\n");
                mem_mapping_enable(&tandy->mapping);
                if (tandy->array[5] & 1)
                {
//                        pclog("Tandy mapping at A0000 %p %p\n", tandy_ram_write, tandy_write);
                        mem_mapping_set_addr(&tandy->mapping, 0xa0000, 0x10000);
                }
                else
                {
//                        pclog("Tandy mapping at B8000\n");
                        mem_mapping_set_addr(&tandy->mapping, 0xb8000, 0x8000);
                }
//                mem_mapping_enable(&tandy->vram_mapping);
                io_sethandler(0x03d0, 0x0010, tandysl_in, NULL, NULL, tandysl_out, NULL, NULL, tandy);
        }
        else
        {
//                pclog("Disable VRAM mapping\n");
                mem_mapping_disable(&tandy->mapping);
//                mem_mapping_disable(&tandy->vram_mapping);
                io_removehandler(0x03d0, 0x0010, tandysl_in, NULL, NULL, tandysl_out, NULL, NULL, tandy);
        }
}
static void tandysl_ram_write(uint32_t addr, uint8_t val, void *p)
{
        tandysl_t *tandy = (tandysl_t *)p;
//        pclog("Tandy RAM write %05X %02X %04X:%04X %08x\n",addr,val,CS,pc, tandy->base);
        ram[tandy->base + (addr & 0x1ffff)] = val;
}

static uint8_t tandysl_ram_read(uint32_t addr, void *p)
{
        tandysl_t *tandy = (tandysl_t *)p;
//        if (!nopageerrors) pclog("Tandy RAM read %05X %02X %04X:%04X\n",addr,ram[tandy->base + (addr & 0x1ffff)],CS,pc);
        return ram[tandy->base + (addr & 0x1ffff)];
}

static void tandysl_write(uint32_t addr, uint8_t val, void *p)
{
        tandysl_t *tandy = (tandysl_t *)p;
        if (tandy->memctrl == -1) 
                return;
                
        egawrites++;
//        pclog("Tandy VRAM write %05X %02X %04X:%04X  %02x %x\n",addr,val,CS,pc,tandy->array[5], (uintptr_t)&tandy->b8000[addr & 0xffff] - (uintptr_t)ram);
        if (tandy->array[5] & 1)
                tandy->b8000[addr & 0xffff] = val;
        else
        {
                if ((addr & 0x7fff) >= tandy->b8000_limit)
                        return;
                tandy->b8000[addr & 0x7fff] = val;
        }
}

static uint8_t tandysl_read(uint32_t addr, void *p)
{
        tandysl_t *tandy = (tandysl_t *)p;
        if (tandy->memctrl == -1) 
                return 0xff;
                
        egareads++;
//        if (!nopageerrors) pclog("Tandy VRAM read  %05X %02X %04X:%04X\n",addr,tandy->b8000[addr&0x7FFF],CS,pc);
        if (tandy->array[5] & 1)
                return tandy->b8000[addr & 0xffff];
        if ((addr & 0x7fff) >= tandy->b8000_limit)
                return 0xff;
        return tandy->b8000[addr & 0x7fff];
}

static void tandysl_recalctimings(tandysl_t *tandy)
{
	double _dispontime, _dispofftime, disptime;
        if (tandy->mode & 1)
        {
                disptime = tandy->crtc[0] + 1;
                _dispontime = tandy->crtc[1];
        }
        else
        {
                disptime = (tandy->crtc[0] + 1) << 1;
                _dispontime = tandy->crtc[1] << 1;
        }
        _dispofftime = disptime - _dispontime;
        _dispontime  *= CGACONST;
        _dispofftime *= CGACONST;
	tandy->dispontime  = (int)(_dispontime  * (1 << TIMER_SHIFT));
	tandy->dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT));
}


static void tandysl_poll(void *p)
{
        tandysl_t *tandy = (tandysl_t *)p;
        uint16_t ca = (tandy->crtc[15] | (tandy->crtc[14] << 8)) & 0x3fff;
        int drawcursor;
        int x, c;
        int oldvc;
        uint8_t chr, attr;
        uint16_t dat;
        int cols[4];
        int col;
        int oldsc;

        if (!tandy->linepos)
        {
//                pclog("tandy_poll vc=%i sc=%i dispon=%i\n", tandy->vc, tandy->sc, tandy->dispon);
//                cgapal[0]=tandy->col&15;
//                printf("Firstline %i Lastline %i tandy->displine %i\n",firstline,lastline,tandy->displine);
                tandy->vidtime += tandy->dispofftime;
                tandy->stat |= 1;
                tandy->linepos = 1;
                oldsc = tandy->sc;
                if ((tandy->crtc[8] & 3) == 3) 
                        tandy->sc = (tandy->sc << 1) & 7;
                if (tandy->dispon)
                {
                        if (tandy->displine < tandy->firstline)
                        {
                                tandy->firstline = tandy->displine;
//                                printf("Firstline %i\n",firstline);
                        }
                        tandy->lastline = tandy->displine;
                        cols[0] = (tandy->array[2] & 0xf) + 16;
                        for (c = 0; c < 8; c++)
                        {
                                if (tandy->array[3] & 4)
                                {
                                        buffer->line[tandy->displine][c] = cols[0];
                                        if (tandy->mode & 1) buffer->line[tandy->displine][c + (tandy->crtc[1] << 3) + 8] = cols[0];
                                        else                 buffer->line[tandy->displine][c + (tandy->crtc[1] << 4) + 8] = cols[0];
                                }
                                else if ((tandy->mode & 0x12) == 0x12)
                                {
                                        buffer->line[tandy->displine][c] = 0;
                                        if (tandy->mode & 1) buffer->line[tandy->displine][c + (tandy->crtc[1] << 3) + 8] = 0;
                                        else                 buffer->line[tandy->displine][c + (tandy->crtc[1] << 4) + 8] = 0;
                                }
                                else
                                {
                                        buffer->line[tandy->displine][c] = (tandy->col & 15) + 16;
                                        if (tandy->mode & 1) buffer->line[tandy->displine][c + (tandy->crtc[1] << 3) + 8] = (tandy->col & 15) + 16;
                                        else                 buffer->line[tandy->displine][c + (tandy->crtc[1] << 4) + 8] = (tandy->col & 15) + 16;
                                }
                        }
                        if (tandy->array[5] & 1) /*640x200x16*/
                        {
                                for (x = 0; x < tandy->crtc[1]*2; x++)
                                {
                                        dat = (tandy->vram[(tandy->ma << 1) & 0xffff] << 8) | 
                                               tandy->vram[((tandy->ma << 1) + 1) & 0xffff];
                                        tandy->ma++;
                                        buffer->line[tandy->displine][(x << 2) + 8]  = tandy->array[((dat >> 12) & 0xf)/*tandy->array[1])*/ + 16] + 16;
                                        buffer->line[tandy->displine][(x << 2) + 9]  = tandy->array[((dat >>  8) & 0xf)/*tandy->array[1])*/ + 16] + 16;
                                        buffer->line[tandy->displine][(x << 2) + 10] = tandy->array[((dat >>  4) & 0xf)/*tandy->array[1])*/ + 16] + 16;
                                        buffer->line[tandy->displine][(x << 2) + 11] = tandy->array[(dat         & 0xf)/*tandy->array[1])*/ + 16] + 16;
                                }
                        }
                        else if ((tandy->array[3] & 0x10) && (tandy->mode & 1)) /*320x200x16*/
                        {
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        dat = (tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 3) * 0x2000)] << 8) | 
                                               tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 3) * 0x2000) + 1];
                                        tandy->ma++;
                                        buffer->line[tandy->displine][(x << 3) + 8]  = 
                                        buffer->line[tandy->displine][(x << 3) + 9]  = tandy->array[((dat >> 12) & tandy->array[1]) + 16] + 16;
                                        buffer->line[tandy->displine][(x << 3) + 10] = 
                                        buffer->line[tandy->displine][(x << 3) + 11] = tandy->array[((dat >>  8) & tandy->array[1]) + 16] + 16;
                                        buffer->line[tandy->displine][(x << 3) + 12] = 
                                        buffer->line[tandy->displine][(x << 3) + 13] = tandy->array[((dat >>  4) & tandy->array[1]) + 16] + 16;
                                        buffer->line[tandy->displine][(x << 3) + 14] = 
                                        buffer->line[tandy->displine][(x << 3) + 15] = tandy->array[(dat         & tandy->array[1]) + 16] + 16;
                                }
                        }
                        else if (tandy->array[3] & 0x10) /*160x200x16*/
                        {
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        dat = (tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 1) * 0x2000)] << 8) | 
                                               tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 1) * 0x2000) + 1];
                                        tandy->ma++;
                                        buffer->line[tandy->displine][(x << 4) + 8]  = 
                                        buffer->line[tandy->displine][(x << 4) + 9]  = 
                                        buffer->line[tandy->displine][(x << 4) + 10] =
                                        buffer->line[tandy->displine][(x << 4) + 11] = tandy->array[((dat >> 12) & tandy->array[1]) + 16] + 16;
                                        buffer->line[tandy->displine][(x << 4) + 12] = 
                                        buffer->line[tandy->displine][(x << 4) + 13] =
                                        buffer->line[tandy->displine][(x << 4) + 14] =
                                        buffer->line[tandy->displine][(x << 4) + 15] = tandy->array[((dat >>  8) & tandy->array[1]) + 16] + 16;
                                        buffer->line[tandy->displine][(x << 4) + 16] = 
                                        buffer->line[tandy->displine][(x << 4) + 17] =
                                        buffer->line[tandy->displine][(x << 4) + 18] =
                                        buffer->line[tandy->displine][(x << 4) + 19] = tandy->array[((dat >>  4) & tandy->array[1]) + 16] + 16;
                                        buffer->line[tandy->displine][(x << 4) + 20] = 
                                        buffer->line[tandy->displine][(x << 4) + 21] =
                                        buffer->line[tandy->displine][(x << 4) + 22] =
                                        buffer->line[tandy->displine][(x << 4) + 23] = tandy->array[(dat         & tandy->array[1]) + 16] + 16;
                                }
                        }
                        else if (tandy->array[3] & 0x08) /*640x200x4 - this implementation is a complete guess!*/
                        {
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        dat = (tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 3) * 0x2000)] << 8) |
                                               tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 3) * 0x2000) + 1];
                                        tandy->ma++;
                                        for (c = 0; c < 8; c++)
                                        {
                                                chr  =  (dat >>  7) & 1;
                                                chr |= ((dat >> 14) & 2);
                                                buffer->line[tandy->displine][(x << 3) + 8 + c] = tandy->array[(chr & tandy->array[1]) + 16] + 16;
                                                dat <<= 1;
                                        }
                                }
                        }
                        else if (tandy->mode & 1)
                        {
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        chr  = tandy->vram[ (tandy->ma << 1)      & 0x3fff];
                                        attr = tandy->vram[((tandy->ma << 1) + 1) & 0x3fff];
                                        drawcursor = ((tandy->ma == ca) && tandy->con && tandy->cursoron);
                                        if (tandy->mode & 0x20)
                                        {
                                                cols[1] = tandy->array[ ((attr & 15)      & tandy->array[1]) + 16] + 16;
                                                cols[0] = tandy->array[(((attr >> 4) & 7) & tandy->array[1]) + 16] + 16;
                                                if ((tandy->blink & 16) && (attr & 0x80) && !drawcursor) 
                                                        cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = tandy->array[((attr & 15) & tandy->array[1]) + 16] + 16;
                                                cols[0] = tandy->array[((attr >> 4) & tandy->array[1]) + 16] + 16;
                                        }
                                        if (tandy->sc & 8)
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[tandy->displine][(x << 3) + c + 8] = cols[0];
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[tandy->displine][(x << 3) + c + 8] = cols[(fontdat[chr][tandy->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                                        }
//                                        if (!((ma^(crtc[15]|(crtc[14]<<8)))&0x3FFF)) printf("Cursor match! %04X\n",ma);
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[tandy->displine][(x << 3) + c + 8] ^= 15;
                                        }
                                        tandy->ma++;
                                }
                        }
                        else if (!(tandy->mode & 2))
                        {
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        chr  = tandy->vram[ (tandy->ma << 1)      & 0x3fff];
                                        attr = tandy->vram[((tandy->ma << 1) + 1) & 0x3fff];
                                        drawcursor = ((tandy->ma == ca) && tandy->con && tandy->cursoron);
                                        if (tandy->mode & 0x20)
                                        {
                                                cols[1] = tandy->array[ ((attr & 15)      & tandy->array[1]) + 16] + 16;
                                                cols[0] = tandy->array[(((attr >> 4) & 7) & tandy->array[1]) + 16] + 16;
                                                if ((tandy->blink & 16) && (attr & 0x80) && !drawcursor) 
                                                        cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = tandy->array[((attr & 15) & tandy->array[1]) + 16] + 16;
                                                cols[0] = tandy->array[((attr >> 4) & tandy->array[1]) + 16] + 16;
                                        }
                                        tandy->ma++;
                                        if (tandy->sc & 8)
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[tandy->displine][(x << 4) + (c << 1) + 8] = 
                                                    buffer->line[tandy->displine][(x << 4) + (c << 1) + 1 + 8] = cols[0];
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[tandy->displine][(x << 4) + (c << 1) + 8] = 
                                                    buffer->line[tandy->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][tandy->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 16; c++)
                                                    buffer->line[tandy->displine][(x << 4) + c + 8] ^= 15;
                                        }
                                }
                        }
                        else if (!(tandy->mode& 16))
                        {
                                cols[0] = (tandy->col & 15) | 16;
                                col = (tandy->col & 16) ? 24 : 16;
                                if (tandy->mode & 4)
                                {
                                        cols[1] = col | 3;
                                        cols[2] = col | 4;
                                        cols[3] = col | 7;
                                }
                                else if (tandy->col & 32)
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
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        dat = (tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 1) * 0x2000)] << 8) | 
                                               tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 1) * 0x2000) + 1];
                                        tandy->ma++;
                                        for (c = 0; c < 8; c++)
                                        {
                                                buffer->line[tandy->displine][(x << 4) + (c << 1) + 8] =
                                                buffer->line[tandy->displine][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
                                                dat <<= 2;
                                        }
                                }
                        }
                        else
                        {
                                cols[0] = 0; 
                                cols[1] = tandy->array[(tandy->col & tandy->array[1]) + 16] + 16;
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        dat = (tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 1) * 0x2000)] << 8) |
                                               tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 1) * 0x2000) + 1];
                                        tandy->ma++;
                                        for (c = 0; c < 16; c++)
                                        {
                                                buffer->line[tandy->displine][(x << 4) + c + 8] = cols[dat >> 15];
                                                dat <<= 1;
                                        }
                                }
                        }
                }
                else
                {
                        if (tandy->array[3] & 4)
                        {
                                if (tandy->mode & 1) hline(buffer, 0, tandy->displine, (tandy->crtc[1] << 3) + 16, (tandy->array[2] & 0xf) + 16);
                                else                 hline(buffer, 0, tandy->displine, (tandy->crtc[1] << 4) + 16, (tandy->array[2] & 0xf) + 16);
                        }
                        else
                        {
                                cols[0] = ((tandy->mode & 0x12) == 0x12) ? 0 : (tandy->col & 0xf) + 16;
                                if (tandy->mode & 1) hline(buffer, 0, tandy->displine, (tandy->crtc[1] << 3) + 16, cols[0]);
                                else                 hline(buffer, 0, tandy->displine, (tandy->crtc[1] << 4) + 16, cols[0]);
                        }
                }
                if (tandy->mode & 1) x = (tandy->crtc[1] << 3) + 16;
                else                 x = (tandy->crtc[1] << 4) + 16;
                tandy->sc = oldsc;
                if (tandy->vc == tandy->crtc[7] && !tandy->sc)
                {
                        tandy->stat |= 8;
//                        printf("VSYNC on %i %i\n",vc,sc);
                }
                tandy->displine++;
                if (tandy->displine >= 360) 
                        tandy->displine = 0;
        }
        else
        {
                tandy->vidtime += tandy->dispontime;
                if (tandy->dispon) 
                        tandy->stat &= ~1;
                tandy->linepos = 0;
                if (tandy->vsynctime)
                {
                        tandy->vsynctime--;
                        if (!tandy->vsynctime)
                        {
                                tandy->stat &= ~8;
//                                printf("VSYNC off %i %i\n",vc,sc);
                        }
                }
                if (tandy->sc == (tandy->crtc[11] & 31) || ((tandy->crtc[8] & 3) == 3 && tandy->sc == ((tandy->crtc[11] & 31) >> 1))) 
                { 
                        tandy->con = 0; 
                        tandy->coff = 1; 
                }
                if (tandy->vadj)
                {
                        tandy->sc++;
                        tandy->sc &= 31;
                        tandy->ma = tandy->maback;
                        tandy->vadj--;
                        if (!tandy->vadj)
                        {
                                tandy->dispon = 1;
                                if (tandy->array[5] & 1)
                                        tandy->ma = tandy->maback = tandy->crtc[13] | (tandy->crtc[12] << 8);
                                else
                                        tandy->ma = tandy->maback = (tandy->crtc[13] | (tandy->crtc[12] << 8)) & 0x3fff;
                                tandy->sc = 0;
//                                printf("Display on!\n");
                        }
                }
                else if (tandy->sc == tandy->crtc[9] || ((tandy->crtc[8] & 3) == 3 && tandy->sc == (tandy->crtc[9] >> 1)))
                {
                        tandy->maback = tandy->ma;
//                        con=0;
//                        coff=0;
                        tandy->sc = 0;
                        oldvc = tandy->vc;
                        tandy->vc++;
                        tandy->vc &= 255;
//                        printf("VC %i %i %i %i  %i\n",vc,crtc[4],crtc[6],crtc[7],tandy->dispon);
                        if (tandy->vc == tandy->crtc[6]) 
                        {
//                                pclog("Display off\n");
                                tandy->dispon = 0;
                        }
                        if (oldvc == tandy->crtc[4])
                        {
//                                pclog("Display over\n");
//                                printf("Display over at %i\n",tandy->displine);
                                tandy->vc = 0;
                                tandy->vadj = tandy->crtc[5];
                                if (!tandy->vadj) 
                                        tandy->dispon = 1;
                                if (!tandy->vadj) 
                                {
                                        if (tandy->array[5] & 1)
                                                tandy->ma = tandy->maback = tandy->crtc[13] | (tandy->crtc[12] << 8);
                                        else
                                                tandy->ma = tandy->maback = (tandy->crtc[13] | (tandy->crtc[12] << 8)) & 0x3fff;
                                }
                                if ((tandy->crtc[10] & 0x60) == 0x20) tandy->cursoron = 0;
                                else                                  tandy->cursoron = tandy->blink & 16;
//                                printf("CRTC10 %02X %i\n",crtc[10],cursoron);
                        }
                        if (tandy->vc == tandy->crtc[7])
                        {
                                tandy->dispon = 0;
                                tandy->displine = 0;
                                tandy->vsynctime = 16;//(crtc[3]>>4)+1;
//                                printf("tandy->vsynctime %i %02X\n",tandy->vsynctime,crtc[3]);
//                                tandy->stat|=8;
                                if (tandy->crtc[7])
                                {
//                                        printf("Lastline %i Firstline %i  %i   %i %i\n",lastline,firstline,lastline-firstline,crtc[1],xsize);
                                        if (tandy->mode & 1) x = (tandy->crtc[1] << 3) + 16;
                                        else                 x = (tandy->crtc[1] << 4) + 16;
                                        tandy->lastline++;
                                        if (x != xsize || (tandy->lastline - tandy->firstline) != ysize)
                                        {
                                                xsize = x;
                                                ysize = tandy->lastline - tandy->firstline;
//                                                printf("Resize to %i,%i - R1 %i\n",xsize,ysize,crtc[1]);
                                                if (xsize < 64) xsize = 656;
                                                if (ysize < 32) ysize = 200;
                                                updatewindowsize(xsize, (ysize << 1) + 16);
                                        }
//                                        printf("Blit %i %i\n",firstline,lastline);
//printf("Xsize is %i\n",xsize);
                                startblit();
                                        video_blit_memtoscreen_8(0, tandy->firstline-4, xsize, (tandy->lastline - tandy->firstline) + 8);
                                endblit();
                                        frames++;
                                        video_res_x = xsize - 16;
                                        video_res_y = ysize;
                                        if ((tandy->array[3] & 0x10) && (tandy->mode & 1)) /*320x200x16*/
                                        {
                                                video_res_x /= 2;
                                                video_bpp = 4;
                                        }
                                        else if (tandy->array[3] & 0x10) /*160x200x16*/
                                        {
                                                video_res_x /= 4;
                                                video_bpp = 4;
                                        }
                                        else if (tandy->array[3] & 0x08) /*640x200x4 - this implementation is a complete guess!*/
                                           video_bpp = 2;
                                        else if (tandy->mode & 1)
                                        {
                                                video_res_x /= 8;
                                                video_res_y /= tandy->crtc[9] + 1;
                                                video_bpp = 0;
                                        }
                                        else if (!(tandy->mode & 2))
                                        {
                                                video_res_x /= 16;
                                                video_res_y /= tandy->crtc[9] + 1;
                                                video_bpp = 0;
                                        }
                                        else if (!(tandy->mode & 16))
                                        {
                                                video_res_x /= 2;
                                                video_bpp = 2;
                                        }
                                        else
                                           video_bpp = 1;                                                
                                }
                                tandy->firstline = 1000;
                                tandy->lastline = 0;
                                tandy->blink++;
                        }
                }
                else
                {
                        tandy->sc++;
                        tandy->sc &= 31;
                        tandy->ma = tandy->maback;
                }
                if ((tandy->sc == (tandy->crtc[10] & 31) || ((tandy->crtc[8] & 3) == 3 && tandy->sc == ((tandy->crtc[10] & 31) >> 1)))) 
                        tandy->con = 1;
        }
}

static void *tandysl_init()
{
        int c;
        tandysl_t *tandy = malloc(sizeof(tandysl_t));
        memset(tandy, 0, sizeof(tandysl_t));

        tandy->memctrl = -1;
        tandy->base = (mem_size - 128) * 1024;
        tandy->b8000_limit = 0x8000;
        tandy->planar_ctrl = 4;
        
        timer_add(tandysl_poll, &tandy->vidtime, TIMER_ALWAYS_ENABLED, tandy);
        mem_mapping_add(&tandy->mapping, 0xb8000, 0x08000, tandysl_read, NULL, NULL, tandysl_write, NULL, NULL,  NULL, 0, tandy);
        mem_mapping_add(&tandy->ram_mapping, 0x80000, 0x20000, tandysl_ram_read, NULL, NULL, tandysl_ram_write, NULL, NULL,  NULL, 0, tandy);
        /*Base 128k mapping is controlled via port 0xffe8, so we remove it from the main mapping*/
        mem_mapping_set_addr(&ram_low_mapping, 0, (mem_size - 128) * 1024);
        io_sethandler(0x03d0, 0x0010, tandysl_in, NULL, NULL, tandysl_out, NULL, NULL, tandy);
        io_sethandler(0xffe8, 0x0001, tandysl_in, NULL, NULL, tandysl_out, NULL, NULL, tandy);
        io_sethandler(0x0065, 0x0001, tandysl_in, NULL, NULL, tandysl_out, NULL, NULL, tandy);
	overscan_x = overscan_y = 16;
        return tandy;
}

static void tandysl_close(void *p)
{
        tandysl_t *tandy = (tandysl_t *)p;

        free(tandy);
}

static void tandysl_speed_changed(void *p)
{
        tandysl_t *tandy = (tandysl_t *)p;
        
        tandysl_recalctimings(tandy);
}

device_t tandysl_device =
{
        "Tandy 1000SL (video)",
        0,
        tandysl_init,
        tandysl_close,
        NULL,
        tandysl_speed_changed,
        NULL,
        NULL
};
