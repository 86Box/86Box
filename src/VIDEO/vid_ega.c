/*EGA emulation*/
#include <stdlib.h>
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "video.h"
#include "vid_ega.h"


extern uint8_t edatlookup[4][4];

static uint8_t ega_rotate[8][256];

static uint32_t pallook16[256], pallook64[256];

/*3C2 controls default mode on EGA. On VGA, it determines monitor type (mono or colour)*/
int egaswitchread,egaswitches=9; /*7=CGA mode (200 lines), 9=EGA mode (350 lines), 8=EGA mode (200 lines)*/

static int old_overscan_color = 0;

int update_overscan = 0;

void ega_out(uint16_t addr, uint8_t val, void *p)
{
        ega_t *ega = (ega_t *)p;
        int c;
        uint8_t o, old;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(ega->miscout & 1)) 
                addr ^= 0x60;
        
        switch (addr)
        {
                case 0x3c0:
		case 0x3c1:
                if (!ega->attrff)
                   ega->attraddr = val & 31;
                else
                {
                        ega->attrregs[ega->attraddr & 31] = val;
                        if (ega->attraddr < 16) 
                                fullchange = changeframecount;
                        if (ega->attraddr == 0x10 || ega->attraddr == 0x14 || ega->attraddr < 0x10)
                        {
                                for (c = 0; c < 16; c++)
                                {
                                        if (ega->attrregs[0x10] & 0x80) ega->egapal[c] = (ega->attrregs[c] &  0xf) | ((ega->attrregs[0x14] & 0xf) << 4);
                                        else                            ega->egapal[c] = (ega->attrregs[c] & 0x3f) | ((ega->attrregs[0x14] & 0xc) << 4);
                                }
                        }
                }
                ega->attrff ^= 1;
                break;
                case 0x3c2:
                egaswitchread = val & 0xc;
                ega->vres = !(val & 0x80);
                ega->pallook = ega->vres ? pallook16 : pallook64;
                ega->vidclock = val & 4; /*printf("3C2 write %02X\n",val);*/
                ega->miscout=val;
                break;
                case 0x3c4: 
                ega->seqaddr = val; 
                break;
                case 0x3c5:
                o = ega->seqregs[ega->seqaddr & 0xf];
                ega->seqregs[ega->seqaddr & 0xf] = val;
                if (o != val && (ega->seqaddr & 0xf) == 1) 
                        ega_recalctimings(ega);
                switch (ega->seqaddr & 0xf)
                {
                        case 1: 
                        if (ega->scrblank && !(val & 0x20)) 
                                fullchange = 3; 
                        ega->scrblank = (ega->scrblank & ~0x20) | (val & 0x20); 
                        break;
                        case 2: 
                        ega->writemask = val & 0xf; 
                        break;
                        case 3:
                        ega->charsetb = (((val >> 2) & 3) * 0x10000) + 2;
                        ega->charseta = ((val & 3)        * 0x10000) + 2;
                        break;
                        case 4:
                        ega->chain2_write = !(val & 4);
                        break;
                }
                break;
                case 0x3ce: 
                ega->gdcaddr = val; 
                break;
                case 0x3cf:
                ega->gdcreg[ega->gdcaddr & 15] = val;
                switch (ega->gdcaddr & 15)
                {
                        case 2: 
                        ega->colourcompare = val; 
                        break;
                        case 4: 
                        ega->readplane = val & 3; 
                        break;
                        case 5: 
                        ega->writemode = val & 3;
                        ega->readmode = val & 8; 
                        ega->chain2_read = val & 0x10;
                        break;
                        case 6:
                        switch (val & 0xc)
                        {
                                case 0x0: /*128k at A0000*/
                                mem_mapping_set_addr(&ega->mapping, 0xa0000, 0x20000);
                                break;
                                case 0x4: /*64k at A0000*/
                                mem_mapping_set_addr(&ega->mapping, 0xa0000, 0x10000);
                                break;
                                case 0x8: /*32k at B0000*/
                                mem_mapping_set_addr(&ega->mapping, 0xb0000, 0x08000);
                                break;
                                case 0xC: /*32k at B8000*/
                                mem_mapping_set_addr(&ega->mapping, 0xb8000, 0x08000);
                                break;
                        }
                        break;
                        case 7: 
                        ega->colournocare = val; 
                        break;
                }
                break;
		case 0x3d0:
                case 0x3d4:
                ega->crtcreg = val & 31;
                return;
		case 0x3d1:
                case 0x3d5:
                if (ega->crtcreg <= 7 && ega->crtc[0x11] & 0x80) return;
                old = ega->crtc[ega->crtcreg];
                ega->crtc[ega->crtcreg] = val;
                if (old != val)
                {
                        if (ega->crtcreg < 0xe || ega->crtcreg > 0x10)
                        {
                                fullchange = changeframecount;
                                ega_recalctimings(ega);
                        }
                }
                break;
        }
}

/*
 * Get the input status register 0
 *
 * Note by Tohka: Code from PCE.
 */
static uint8_t ega_get_input_status_0(ega_t *ega)
{
	unsigned bit;
	uint8_t status0 = 0;

	bit = (egaswitchread >> 2) & 3;

	if (egaswitches & (0x08 >> bit)) {
		status0 |= 0x10;
	}
	else {
		status0 &= ~0x10;
	}

	return status0;
}

uint8_t ega_in(uint16_t addr, void *p)
{
        ega_t *ega = (ega_t *)p;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(ega->miscout & 1)) 
                addr ^= 0x60;

        switch (addr)
        {
                case 0x3c0: 
                return ega->attraddr;
                case 0x3c1: 
                return ega->attrregs[ega->attraddr];
                case 0x3c2:
		return ega_get_input_status_0(ega);
                break;
                case 0x3c4: 
                return ega->seqaddr;
                case 0x3c5:
                return ega->seqregs[ega->seqaddr & 0xf];
		case 0x3c8:
		return 2;
                case 0x3cc: 
                return ega->miscout;
                case 0x3ce: 
                return ega->gdcaddr;
                case 0x3cf:
                return ega->gdcreg[ega->gdcaddr & 0xf];
		case 0x3d0:
                case 0x3d4:
                return ega->crtcreg;
		case 0x3d1:
                case 0x3d5:
                return ega->crtc[ega->crtcreg];
                case 0x3da:
                ega->attrff = 0;
                ega->stat ^= 0x30; /*Fools IBM EGA video BIOS self-test*/
                return ega->stat;
        }
        return 0xff;
}

void ega_recalctimings(ega_t *ega)
{
	double _dispontime, _dispofftime, disptime;
        double crtcconst;

        ega->vtotal = ega->crtc[6];
        ega->dispend = ega->crtc[0x12];
        ega->vsyncstart = ega->crtc[0x10];
        ega->split = ega->crtc[0x18];

        if (ega->crtc[7] & 1)  ega->vtotal |= 0x100;
        if (ega->crtc[7] & 32) ega->vtotal |= 0x200;
        ega->vtotal++;

        if (ega->crtc[7] & 2)  ega->dispend |= 0x100;
        if (ega->crtc[7] & 64) ega->dispend |= 0x200;
        ega->dispend++;

        if (ega->crtc[7] & 4)   ega->vsyncstart |= 0x100;
        if (ega->crtc[7] & 128) ega->vsyncstart |= 0x200;
        ega->vsyncstart++;

        if (ega->crtc[7] & 0x10) ega->split |= 0x100;
        if (ega->crtc[9] & 0x40) ega->split |= 0x200;
        ega->split+=2;

        ega->hdisp = ega->crtc[1];
        ega->hdisp++;

        ega->rowoffset = ega->crtc[0x13];

        if (ega->vidclock) crtcconst = (ega->seqregs[1] & 1) ? MDACONST : (MDACONST * (9.0 / 8.0));
        else               crtcconst = (ega->seqregs[1] & 1) ? CGACONST : (CGACONST * (9.0 / 8.0));

        disptime = ega->crtc[0] + 2;
        _dispontime = ega->crtc[1] + 1;

        if (ega->seqregs[1] & 8) 
        { 
                disptime*=2; 
                _dispontime*=2; 
        }
        _dispofftime = disptime - _dispontime;
        _dispontime  *= crtcconst;
        _dispofftime *= crtcconst;

	ega->dispontime  = (int)(_dispontime  * (1 << TIMER_SHIFT));
	ega->dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT));
}

void ega_poll(void *p)
{
        ega_t *ega = (ega_t *)p;
        uint8_t chr, dat, attr;
        uint32_t charaddr;
        int x, xx;
        uint32_t fg, bg;
        int offset;
        uint8_t edat[4];
        int drawcursor = 0;
	int y_add = enable_overscan ? 14 : 0;
	int x_add = enable_overscan ? 8 : 0;
	int y_add_ex = enable_overscan ? 28 : 0;
	int x_add_ex = enable_overscan ? 16 : 0;
	uint32_t *q, *r, i, j;
	int wx = 640, wy = 350;

        if (!ega->linepos)
        {
                ega->vidtime += ega->dispofftime;

                ega->stat |= 1;
                ega->linepos = 1;

                if (ega->dispon)
                {
                        if (ega->firstline == 2000) 
                        {
                                ega->firstline = ega->displine;
                                video_wait_for_buffer();
                        }

                        if (ega->scrblank)
                        {
                                for (x = 0; x < ega->hdisp; x++)
                                {
                                        switch (ega->seqregs[1] & 9)
                                        {
                                                case 0:
                                                for (xx = 0; xx < 9; xx++)  ((uint32_t *)buffer32->line[ega->displine + y_add])[(x * 9) + xx + 32 + x_add] = 0;
                                                break;
                                                case 1:
                                                for (xx = 0; xx < 8; xx++)  ((uint32_t *)buffer32->line[ega->displine + y_add])[(x * 8) + xx + 32 + x_add] = 0;
                                                break;
                                                case 8:
                                                for (xx = 0; xx < 18; xx++) ((uint32_t *)buffer32->line[ega->displine + y_add])[(x * 18) + xx + 32 + x_add] = 0;
                                                break;
                                                case 9:
                                                for (xx = 0; xx < 16; xx++) ((uint32_t *)buffer32->line[ega->displine + y_add])[(x * 16) + xx + 32 + x_add] = 0;
                                                break;
                                        }
                                }
                        }
                        else if (!(ega->gdcreg[6] & 1))
                        {
                                if (fullchange)
                                {
                                        for (x = 0; x < ega->hdisp; x++)
                                        {
                                                drawcursor = ((ega->ma == ega->ca) && ega->con && ega->cursoron);
                                                chr  = ega->vram[(ega->ma << 1) & ega->vrammask];
                                                attr = ega->vram[((ega->ma << 1) + 1) & ega->vrammask];

                                                if (attr & 8) charaddr = ega->charsetb + (chr * 128);
                                                else          charaddr = ega->charseta + (chr * 128);

                                                if (drawcursor) 
                                                { 
                                                        bg = ega->pallook[ega->egapal[attr & 15]]; 
                                                        fg = ega->pallook[ega->egapal[attr >> 4]]; 
                                                }
                                                else
                                                {
                                                        fg = ega->pallook[ega->egapal[attr & 15]];
                                                        bg = ega->pallook[ega->egapal[attr >> 4]];
                                                        if (attr & 0x80 && ega->attrregs[0x10] & 8)
                                                        {
                                                                bg = ega->pallook[ega->egapal[(attr >> 4) & 7]];
                                                                if (ega->blink & 16) 
                                                                        fg = bg;
                                                        }
                                                }

                                                dat = ega->vram[charaddr + (ega->sc << 2)];
                                                if (ega->seqregs[1] & 8)
                                                {
                                                        if (ega->seqregs[1] & 1) 
                                                        { 
                                                                for (xx = 0; xx < 8; xx++) 
                                                               	        ((uint32_t *)buffer32->line[ega->displine + y_add])[(((x << 4) + 32 + (xx << 1)) & 2047) + x_add] =
                                                       	                ((uint32_t *)buffer32->line[ega->displine + y_add])[(((x << 4) + 33 + (xx << 1)) & 2047) + x_add] = (dat & (0x80 >> xx)) ? fg : bg; 
                                                        }
                                                        else
                                                        {
                       	                                        for (xx = 0; xx < 8; xx++) 
               	                                                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(((x * 18) + 32 + (xx << 1)) & 2047) + x_add] = 
       	                                                                ((uint32_t *)buffer32->line[ega->displine + y_add])[(((x * 18) + 33 + (xx << 1)) & 2047) + x_add] = (dat & (0x80 >> xx)) ? fg : bg;
                                                                if ((chr & ~0x1f) != 0xc0 || !(ega->attrregs[0x10] & 4)) 
               	                                                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(((x * 18) + 32 + 16) & 2047) + x_add] = 
									((uint32_t *)buffer32->line[ega->displine + y_add])[(((x * 18) + 32 + 17) & 2047) + x_add] = bg;
                                                                else                  
               	                                                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(((x * 18) + 32 + 16) & 2047) + x_add] = 
       	                                                                ((uint32_t *)buffer32->line[ega->displine + y_add])[(((x * 18) + 32 + 17) & 2047) + x_add] = (dat & 1) ? fg : bg;
                                                        }
                                                }
                                                else
                                                {
                                                        if (ega->seqregs[1] & 1) 
                                                        { 
                                       	                        for (xx = 0; xx < 8; xx++) 
                               	                                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(((x << 3) + 32 + xx) & 2047) + x_add] = (dat & (0x80 >> xx)) ? fg : bg; 
                                                        }
                                                        else
                                                        {
                                                                for (xx = 0; xx < 8; xx++) 
                                                               	        ((uint32_t *)buffer32->line[ega->displine + y_add])[(((x * 9) + 32 + xx) & 2047) + x_add] = (dat & (0x80 >> xx)) ? fg : bg;
                                                       	        if ((chr & ~0x1f) != 0xc0 || !(ega->attrregs[0x10] & 4)) 
                                               	                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(((x * 9) + 32 + 8) & 2047) + x_add] = bg;
                                       	                        else                  
                               	                                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(((x * 9) + 32 + 8) & 2047) + x_add] = (dat & 1) ? fg : bg;
                                                        }
                                                }
                                                ega->ma += 4; 
                                                ega->ma &= ega->vrammask;
                                        }
                                }
                        }
                        else
                        {
                                switch (ega->gdcreg[5] & 0x20)
                                {
                                        case 0x00:
                                        if (ega->seqregs[1] & 8)
                                        {
                                                offset = ((8 - ega->scrollcache) << 1) + 16;
                                                for (x = 0; x <= ega->hdisp; x++)
                                                {
                                                        if (ega->sc & 1 && !(ega->crtc[0x17] & 1))
                                                        {
                                                                edat[0] = ega->vram[ega->ma | 0x8000];
                                                                edat[1] = ega->vram[ega->ma | 0x8001];
                                                                edat[2] = ega->vram[ega->ma | 0x8002];
                                                                edat[3] = ega->vram[ega->ma | 0x8003];
                                                        }
                                                        else
                                                        {
                                                                edat[0] = ega->vram[ega->ma];
                                                                edat[1] = ega->vram[ega->ma | 0x1];
                                                                edat[2] = ega->vram[ega->ma | 0x2];
                                                                edat[3] = ega->vram[ega->ma | 0x3];
                                                        }
                                                        ega->ma += 4; 
                                                        ega->ma &= ega->vrammask;

                                       	                dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
                               	                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 14 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 15 + offset + x_add] = ega->pallook[ega->egapal[(dat & 0xf) & ega->attrregs[0x12]]];
                       	                                ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 12 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 13 + offset + x_add] = ega->pallook[ega->egapal[(dat >> 4)  & ega->attrregs[0x12]]];
               	                                        dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
       	                                                ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 10 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 11 + offset + x_add] = ega->pallook[ega->egapal[(dat & 0xf) & ega->attrregs[0x12]]];
                                                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  8 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  9 + offset + x_add] = ega->pallook[ega->egapal[(dat >> 4)  & ega->attrregs[0x12]]];
                                       	                dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
                               	                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  6 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  7 + offset + x_add] = ega->pallook[ega->egapal[(dat & 0xf) & ega->attrregs[0x12]]];
                       	                                ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  4 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  5 + offset + x_add] = ega->pallook[ega->egapal[(dat >> 4)  & ega->attrregs[0x12]]];
               	                                        dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
       	                                                ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  2 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  3 + offset + x_add] = ega->pallook[ega->egapal[(dat & 0xf) & ega->attrregs[0x12]]];
                                                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +      offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  1 + offset + x_add] = ega->pallook[ega->egapal[(dat >> 4)  & ega->attrregs[0x12]]];
                                                }
                                        }
                                        else
                                        {
                                                offset = (8 - ega->scrollcache) + 24;
                                                for (x = 0; x <= ega->hdisp; x++)
                                                {
                                                        if (ega->sc & 1 && !(ega->crtc[0x17] & 1))
                                                        {
                                                                edat[0] = ega->vram[ega->ma | 0x8000];
                                                                edat[1] = ega->vram[ega->ma | 0x8001];
                                                                edat[2] = ega->vram[ega->ma | 0x8002];
                                                                edat[3] = ega->vram[ega->ma | 0x8003];
                                                        }
                                                        else
                                                        {
                                                                edat[0] = ega->vram[ega->ma];
                                                                edat[1] = ega->vram[ega->ma | 0x1];
                                                                edat[2] = ega->vram[ega->ma | 0x2];
                                                                edat[3] = ega->vram[ega->ma | 0x3];
                                                        }
                                                        ega->ma += 4; 
                                                        ega->ma &= ega->vrammask;

                                                        dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
                                       	                ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 3) + 7 + offset + x_add] = ega->pallook[ega->egapal[(dat & 0xf) & ega->attrregs[0x12]]];
                               	                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 3) + 6 + offset + x_add] = ega->pallook[ega->egapal[(dat >> 4)  & ega->attrregs[0x12]]];
                       	                                dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
               	                                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 3) + 5 + offset + x_add] = ega->pallook[ega->egapal[(dat & 0xf) & ega->attrregs[0x12]]];
       	                                                ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 3) + 4 + offset + x_add] = ega->pallook[ega->egapal[(dat >> 4)  & ega->attrregs[0x12]]];
                                                        dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
                                                       	((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 3) + 3 + offset + x_add] = ega->pallook[ega->egapal[(dat & 0xf) & ega->attrregs[0x12]]];
                                               	        ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 3) + 2 + offset + x_add] = ega->pallook[ega->egapal[(dat >> 4)  & ega->attrregs[0x12]]];
                                       	                dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
                               	                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 3) + 1 + offset + x_add] = ega->pallook[ega->egapal[(dat & 0xf) & ega->attrregs[0x12]]];
                       	                                ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 3) +     offset + x_add] = ega->pallook[ega->egapal[(dat >> 4)  & ega->attrregs[0x12]]];
                                                }
                                        }
                                        break;
                                        case 0x20:
                                        offset = ((8 - ega->scrollcache) << 1) + 16;
                                        for (x = 0; x <= ega->hdisp; x++)
                                        {
                                                if (ega->sc & 1 && !(ega->crtc[0x17] & 1))
                                                {
                                                        edat[0] = ega->vram[(ega->ma << 1) + 0x8000];
                                                        edat[1] = ega->vram[(ega->ma << 1) + 0x8001];
                                                }
                                                else
                                                {
                                                        edat[0] = ega->vram[(ega->ma << 1)];
                                                        edat[1] = ega->vram[(ega->ma << 1) + 1];
                                                }
                                                ega->ma += 4; 
                                                ega->ma &= ega->vrammask;

                                                ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 14 + offset + x_add]=  ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 15 + offset + x_add] = ega->pallook[ega->egapal[edat[1] & 3]];
                                               	((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 12 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 13 + offset + x_add] = ega->pallook[ega->egapal[(edat[1] >> 2) & 3]];
                                       	        ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 10 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) + 11 + offset + x_add] = ega->pallook[ega->egapal[(edat[1] >> 4) & 3]];
                               	                ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  8 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  9 + offset + x_add] = ega->pallook[ega->egapal[(edat[1] >> 6) & 3]];
                       	                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  6 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  7 + offset + x_add] = ega->pallook[ega->egapal[(edat[0] >> 0) & 3]];
               	                                ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  4 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  5 + offset + x_add] = ega->pallook[ega->egapal[(edat[0] >> 2) & 3]];
       	                                        ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  2 + offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  3 + offset + x_add] = ega->pallook[ega->egapal[(edat[0] >> 4) & 3]];
                                                ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +      offset + x_add] = ((uint32_t *)buffer32->line[ega->displine + y_add])[(x << 4) +  1 + offset + x_add] = ega->pallook[ega->egapal[(edat[0] >> 6) & 3]];
                                        }
                                        break;
                                }
                        }
                        if (ega->lastline < ega->displine) 
                                ega->lastline = ega->displine;
                }

                ega->displine++;
                if ((ega->stat & 8) && ((ega->displine & 15) == (ega->crtc[0x11] & 15)) && ega->vslines)
                        ega->stat &= ~8;
                ega->vslines++;
                if (ega->displine > 500) 
                        ega->displine = 0;
        }
        else
        {
                ega->vidtime += ega->dispontime;
                if (ega->dispon) 
                        ega->stat &= ~1;
                ega->linepos = 0;
                if (ega->sc == (ega->crtc[11] & 31)) 
                   ega->con = 0; 
                if (ega->dispon)
                {
                        if (ega->sc == (ega->crtc[9] & 31))
                        {
                                ega->sc = 0;

                                ega->maback += (ega->rowoffset << 3);
                                ega->maback &= ega->vrammask;
                                ega->ma = ega->maback;
                        }
                        else
                        {
                                ega->sc++;
                                ega->sc &= 31;
                                ega->ma = ega->maback;
                        }
                }
                ega->vc++;
                ega->vc &= 1023;
                if (ega->vc == ega->split)
                {
                        ega->ma = ega->maback = 0;
                        if (ega->attrregs[0x10] & 0x20)
                                ega->scrollcache = 0;
                }
                if (ega->vc == ega->dispend)
                {
                        ega->dispon=0;
                        if (ega->crtc[10] & 0x20) ega->cursoron = 0;
                        else                      ega->cursoron = ega->blink & 16;
                        if (!(ega->gdcreg[6] & 1) && !(ega->blink & 15)) 
                                fullchange = 2;
                        ega->blink++;

                        if (fullchange) 
                                fullchange--;
                }
                if (ega->vc == ega->vsyncstart)
                {
                        ega->dispon = 0;
                        ega->stat |= 8;
                        if (ega->seqregs[1] & 8) x = ega->hdisp * ((ega->seqregs[1] & 1) ? 8 : 9) * 2;
                        else                     x = ega->hdisp * ((ega->seqregs[1] & 1) ? 8 : 9);
                        if ((x != xsize || (ega->lastline - ega->firstline) != ysize) || update_overscan)
                        {
                                xsize = x;
                                ysize = ega->lastline - ega->firstline;
                                if (xsize < 64) xsize = 640;
                                if (ysize < 32) ysize = 200;
				y_add = enable_overscan ? 14 : 0;
				x_add = enable_overscan ? 8 : 0;
				y_add_ex = enable_overscan ? 28 : 0;
				x_add_ex = enable_overscan ? 16 : 0;

				if ((xsize > 2032) || (ysize > 2032))
				{
					x_add = x_add_ex = 0;
					y_add = y_add_ex = 0;
					suppress_overscan = 1;
				}
				else
				{
					suppress_overscan = 0;
				}

                                if (ega->vres)
                                        updatewindowsize(xsize + x_add_ex, (ysize << 1) + y_add_ex);
                                else
                                        updatewindowsize(xsize + x_add_ex, ysize + y_add_ex);
                        }

			if (enable_overscan)
			{
				if ((x >= 160) && ((ega->lastline - ega->firstline) >= 120))
				{
					for (i  = 0; i < y_add; i++)
					{
						q = &((uint32_t *)buffer32->line[i])[32];
						r = &((uint32_t *)buffer32->line[ysize + y_add + i])[32];

						for (j = 0; j < (xsize + x_add_ex); j++)
						{
							q[j] = ega->pallook[ega->attrregs[0x11]];
							r[j] = ega->pallook[ega->attrregs[0x11]];
						}
					}

					for (i = y_add; i < (ysize + y_add); i ++)
					{
						q = &((uint32_t *)buffer32->line[i])[32];

						for (j = 0; j < x_add; j++)
						{
							q[j] = ega->pallook[ega->attrregs[0x11]];
							q[xsize + x_add + j] = ega->pallook[ega->attrregs[0x11]];
						}
					}
				}
			}
         
                        video_blit_memtoscreen(32, 0, ega->firstline, ega->lastline + y_add_ex, xsize + x_add_ex, ega->lastline - ega->firstline + y_add_ex);

                        frames++;
                        
                        ega->video_res_x = wx;
                        ega->video_res_y = wy + 1;
                        if (!(ega->gdcreg[6] & 1)) /*Text mode*/
                        {
                                ega->video_res_x /= (ega->seqregs[1] & 1) ? 8 : 9;
                                ega->video_res_y /= (ega->crtc[9] & 31) + 1;
                                ega->video_bpp = 0;
                        }
                        else
                        {
                                if (ega->crtc[9] & 0x80)
                                   ega->video_res_y /= 2;
                                if (!(ega->crtc[0x17] & 1))
                                   ega->video_res_y *= 2;
                                ega->video_res_y /= (ega->crtc[9] & 31) + 1;                                   
                                if (ega->seqregs[1] & 8)
                                   ega->video_res_x /= 2;
                                ega->video_bpp = (ega->gdcreg[5] & 0x20) ? 2 : 4;
                        }

                        readflash=0;
                        ega->firstline = 2000;
                        ega->lastline = 0;

                        ega->maback = ega->ma = (ega->crtc[0xc] << 8)| ega->crtc[0xd];
                        ega->ca = (ega->crtc[0xe] << 8) | ega->crtc[0xf];
                        ega->ma <<= 2;
                        ega->maback <<= 2;
                        ega->ca <<= 2;
                        changeframecount = 2;
                        ega->vslines = 0;
                }
                if (ega->vc == ega->vtotal)
                {
                        ega->vc = 0;
                        ega->sc = 0;
                        ega->dispon = 1;
                        ega->displine = 0;
                        ega->scrollcache = ega->attrregs[0x13] & 7;
                }
                if (ega->sc == (ega->crtc[10] & 31)) 
                        ega->con = 1;
        }
}


void ega_write(uint32_t addr, uint8_t val, void *p)
{
        ega_t *ega = (ega_t *)p;
        uint8_t vala, valb, valc, vald;
        int writemask2 = ega->writemask;

        egawrites++;
        cycles -= video_timing_b;
        cycles_lost += video_timing_b;
        
        if (addr >= 0xB0000) addr &= 0x7fff;
        else                 addr &= 0xffff;

        if (ega->chain2_write)
        {
                writemask2 &= ~0xa;
                if (addr & 1)
                        writemask2 <<= 1;
                addr &= ~1;
        }

        addr <<= 2;

        if (!(ega->gdcreg[6] & 1)) 
                fullchange = 2;

        switch (ega->writemode)
        {
                case 1:
                if (writemask2 & 1) ega->vram[addr]       = ega->la;
                if (writemask2 & 2) ega->vram[addr | 0x1] = ega->lb;
                if (writemask2 & 4) ega->vram[addr | 0x2] = ega->lc;
                if (writemask2 & 8) ega->vram[addr | 0x3] = ega->ld;
                break;
                case 0:
                if (ega->gdcreg[3] & 7) 
                        val = ega_rotate[ega->gdcreg[3] & 7][val];
                        
                if (ega->gdcreg[8] == 0xff && !(ega->gdcreg[3] & 0x18) && !ega->gdcreg[1])
                {
                        if (writemask2 & 1) ega->vram[addr]       = val;
                        if (writemask2 & 2) ega->vram[addr | 0x1] = val;
                        if (writemask2 & 4) ega->vram[addr | 0x2] = val;
                        if (writemask2 & 8) ega->vram[addr | 0x3] = val;
                }
                else
                {
                        if (ega->gdcreg[1] & 1) vala = (ega->gdcreg[0] & 1) ? 0xff : 0;
                        else                    vala = val;
                        if (ega->gdcreg[1] & 2) valb = (ega->gdcreg[0] & 2) ? 0xff : 0;
                        else                    valb = val;
                        if (ega->gdcreg[1] & 4) valc = (ega->gdcreg[0] & 4) ? 0xff : 0;
                        else                    valc = val;
                        if (ega->gdcreg[1] & 8) vald = (ega->gdcreg[0] & 8) ? 0xff : 0;
                        else                    vald = val;
                        switch (ega->gdcreg[3] & 0x18)
                        {
                                case 0: /*Set*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) | (ega->la & ~ega->gdcreg[8]);
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | (ega->lb & ~ega->gdcreg[8]);
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | (ega->lc & ~ega->gdcreg[8]);
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | (ega->ld & ~ega->gdcreg[8]);
                                break;
                                case 8: /*AND*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala | ~ega->gdcreg[8]) & ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb | ~ega->gdcreg[8]) & ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc | ~ega->gdcreg[8]) & ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald | ~ega->gdcreg[8]) & ega->ld;
                                break;
                                case 0x10: /*OR*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) | ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | ega->ld;
                                break;
                                case 0x18: /*XOR*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) ^ ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) ^ ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) ^ ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) ^ ega->ld;
                                break;
                        }
                }
                break;
                case 2:
                if (!(ega->gdcreg[3] & 0x18) && !ega->gdcreg[1])
                {
                        if (writemask2 & 1) ega->vram[addr]       = (((val & 1) ? 0xff : 0) & ega->gdcreg[8]) | (ega->la & ~ega->gdcreg[8]);
                        if (writemask2 & 2) ega->vram[addr | 0x1] = (((val & 2) ? 0xff : 0) & ega->gdcreg[8]) | (ega->lb & ~ega->gdcreg[8]);
                        if (writemask2 & 4) ega->vram[addr | 0x2] = (((val & 4) ? 0xff : 0) & ega->gdcreg[8]) | (ega->lc & ~ega->gdcreg[8]);
                        if (writemask2 & 8) ega->vram[addr | 0x3] = (((val & 8) ? 0xff : 0) & ega->gdcreg[8]) | (ega->ld & ~ega->gdcreg[8]);
                }
                else
                {
                        vala = ((val & 1) ? 0xff : 0);
                        valb = ((val & 2) ? 0xff : 0);
                        valc = ((val & 4) ? 0xff : 0);
                        vald = ((val & 8) ? 0xff : 0);
                        switch (ega->gdcreg[3] & 0x18)
                        {
                                case 0: /*Set*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) | (ega->la & ~ega->gdcreg[8]);
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | (ega->lb & ~ega->gdcreg[8]);
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | (ega->lc & ~ega->gdcreg[8]);
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | (ega->ld & ~ega->gdcreg[8]);
                                break;
                                case 8: /*AND*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala | ~ega->gdcreg[8]) & ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb | ~ega->gdcreg[8]) & ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc | ~ega->gdcreg[8]) & ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald | ~ega->gdcreg[8]) & ega->ld;
                                break;
                                case 0x10: /*OR*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) | ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | ega->ld;
                                break;
                                case 0x18: /*XOR*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) ^ ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) ^ ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) ^ ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) ^ ega->ld;
                                break;
                        }
                }
                break;
        }
}

uint8_t ega_read(uint32_t addr, void *p)
{
        ega_t *ega = (ega_t *)p;
        uint8_t temp, temp2, temp3, temp4;
        int readplane = ega->readplane;
        
        egareads++;
        cycles -= video_timing_b;
        cycles_lost += video_timing_b;
        if (addr >= 0xb0000) addr &= 0x7fff;
        else                 addr &= 0xffff;

        if (ega->chain2_read)
        {
                readplane = (readplane & 2) | (addr & 1);
                addr &= ~1;
        }

        addr <<= 2;

        ega->la = ega->vram[addr];
        ega->lb = ega->vram[addr | 0x1];
        ega->lc = ega->vram[addr | 0x2];
        ega->ld = ega->vram[addr | 0x3];
        if (ega->readmode)
        {
                temp   = (ega->colournocare & 1)  ? 0xff : 0;
                temp  &= ega->la;
                temp  ^= (ega->colourcompare & 1) ? 0xff : 0;
                temp2  = (ega->colournocare & 2)  ? 0xff : 0;
                temp2 &= ega->lb;
                temp2 ^= (ega->colourcompare & 2) ? 0xff : 0;
                temp3  = (ega->colournocare & 4)  ? 0xff : 0;
                temp3 &= ega->lc;
                temp3 ^= (ega->colourcompare & 4) ? 0xff : 0;
                temp4  = (ega->colournocare & 8)  ? 0xff : 0;
                temp4 &= ega->ld;
                temp4 ^= (ega->colourcompare & 8) ? 0xff : 0;
                return ~(temp | temp2 | temp3 | temp4);
        }
        return ega->vram[addr | readplane];
}

void ega_init(ega_t *ega)
{
        int c, d, e;
        
        ega->vram = malloc(0x40000);
        ega->vrammask = 0x3ffff;
        
        for (c = 0; c < 256; c++)
        {
                e = c;
                for (d = 0; d < 8; d++)
                {
                        ega_rotate[d][c] = e;
                        e = (e >> 1) | ((e & 1) ? 0x80 : 0);
                }
        }

        for (c = 0; c < 4; c++)
        {
                for (d = 0; d < 4; d++)
                {
                        edatlookup[c][d] = 0;
                        if (c & 1) edatlookup[c][d] |= 1;
                        if (d & 1) edatlookup[c][d] |= 2;
                        if (c & 2) edatlookup[c][d] |= 0x10;
                        if (d & 2) edatlookup[c][d] |= 0x20;
                }
        }

        for (c = 0; c < 256; c++)
        {
                pallook64[c]  = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
                pallook64[c] += makecol32(((c >> 5) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 3) & 1) * 0x55);
                pallook16[c]  = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
                pallook16[c] += makecol32(((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55);
                if ((c & 0x17) == 6) 
                        pallook16[c] = makecol32(0xaa, 0x55, 0);
        }
        ega->pallook = pallook16;
	old_overscan_color = 0;
}

void ega_common_defaults(ega_t *ega)
{
	ega->miscout |= 0x22;
	ega->enablevram = 1;
	ega->oddeven_page = 0;

	ega->seqregs[4] |= 2;
	ega->extvram = 1;

	update_overscan = 0;
}

void *ega_standalone_init()
{
        ega_t *ega = malloc(sizeof(ega_t));
        memset(ega, 0, sizeof(ega_t));
        
	overscan_x = 16;
	overscan_y = 28;

        rom_init(&ega->bios_rom, L"roms/ibm_6277356_ega_card_u44_27128.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

        if (ega->bios_rom.rom[0x3ffe] == 0xaa && ega->bios_rom.rom[0x3fff] == 0x55)
        {
                int c;

                for (c = 0; c < 0x2000; c++)
                {
                        uint8_t temp = ega->bios_rom.rom[c];
                        ega->bios_rom.rom[c] = ega->bios_rom.rom[0x3fff - c];
                        ega->bios_rom.rom[0x3fff - c] = temp;
                }
        }

        ega->crtc[0] = 63;
        ega->dispontime = 1000 * (1 << TIMER_SHIFT);
        ega->dispofftime = 1000 * (1 << TIMER_SHIFT);
	ega->dispontime <<= 1;
	ega->dispofftime <<= 1;

        ega_init(ega);        

	ega_common_defaults(ega);

        mem_mapping_add(&ega->mapping, 0xa0000, 0x20000, ega_read, NULL, NULL, ega_write, NULL, NULL, NULL, 0, ega);
        timer_add(ega_poll, &ega->vidtime, TIMER_ALWAYS_ENABLED, ega);
        io_sethandler(0x03c0, 0x0020, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
        return ega;
}

void *cpqega_standalone_init()
{
        ega_t *ega = malloc(sizeof(ega_t));
        memset(ega, 0, sizeof(ega_t));
        
	overscan_x = 16;
	overscan_y = 28;

        rom_init(&ega->bios_rom, L"roms/108281-001.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

        if (ega->bios_rom.rom[0x3ffe] == 0xaa && ega->bios_rom.rom[0x3fff] == 0x55)
        {
                int c;

                for (c = 0; c < 0x2000; c++)
                {
                        uint8_t temp = ega->bios_rom.rom[c];
                        ega->bios_rom.rom[c] = ega->bios_rom.rom[0x3fff - c];
                        ega->bios_rom.rom[0x3fff - c] = temp;
                }
        }

        ega->crtc[0] = 63;
        ega->dispontime = 1000 * (1 << TIMER_SHIFT);
        ega->dispofftime = 1000 * (1 << TIMER_SHIFT);

        ega_init(ega);        

	ega_common_defaults(ega);

        mem_mapping_add(&ega->mapping, 0xa0000, 0x20000, ega_read, NULL, NULL, ega_write, NULL, NULL, NULL, 0, ega);
        timer_add(ega_poll, &ega->vidtime, TIMER_ALWAYS_ENABLED, ega);
        io_sethandler(0x03c0, 0x0020, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
        return ega;
}

void *sega_standalone_init()
{
        ega_t *ega = malloc(sizeof(ega_t));
        memset(ega, 0, sizeof(ega_t));
        
	overscan_x = 16;
	overscan_y = 28;

        rom_init(&ega->bios_rom, L"roms/lega.vbi", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

        if (ega->bios_rom.rom[0x3ffe] == 0xaa && ega->bios_rom.rom[0x3fff] == 0x55)
        {
                int c;

                for (c = 0; c < 0x2000; c++)
                {
                        uint8_t temp = ega->bios_rom.rom[c];
                        ega->bios_rom.rom[c] = ega->bios_rom.rom[0x3fff - c];
                        ega->bios_rom.rom[0x3fff - c] = temp;
                }
        }

        ega->crtc[0] = 63;
        ega->dispontime = 1000 * (1 << TIMER_SHIFT);
        ega->dispofftime = 1000 * (1 << TIMER_SHIFT);

        ega_init(ega);        

	ega_common_defaults(ega);

        mem_mapping_add(&ega->mapping, 0xa0000, 0x20000, ega_read, NULL, NULL, ega_write, NULL, NULL, NULL, 0, ega);
        timer_add(ega_poll, &ega->vidtime, TIMER_ALWAYS_ENABLED, ega);
        io_sethandler(0x03c0, 0x0020, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
        return ega;
}

static int ega_standalone_available()
{
        return rom_present(L"roms/ibm_6277356_ega_card_u44_27128.bin");
}

static int cpqega_standalone_available()
{
        return rom_present(L"roms/108281-001.bin");
}

static int sega_standalone_available()
{
        return rom_present(L"roms/lega.vbi");
}

void ega_close(void *p)
{
        ega_t *ega = (ega_t *)p;

        free(ega->vram);
        free(ega);
}

void ega_speed_changed(void *p)
{
        ega_t *ega = (ega_t *)p;
        
        ega_recalctimings(ega);
}

device_t ega_device =
{
        "EGA",
        0,
        ega_standalone_init,
        ega_close,
        ega_standalone_available,
        ega_speed_changed,
        NULL,
        NULL
};

device_t cpqega_device =
{
        "Compaq EGA",
        0,
        cpqega_standalone_init,
        ega_close,
        cpqega_standalone_available,
        ega_speed_changed,
        NULL,
        NULL
};

device_t sega_device =
{
        "SuperEGA",
        0,
        sega_standalone_init,
        ega_close,
        sega_standalone_available,
        ega_speed_changed,
        NULL,
        NULL
};
