/* Copyright holders: Tenshi
   see COPYING for more details
*/
/*Brooktree BT485 true colour RAMDAC emulation*/
/*Currently only a dummy stub for logging and passing output to the generic SVGA handler*/
#include "../ibm.h"
#include "../mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_bt485_ramdac.h"


int bt485_get_clock_divider(bt485_ramdac_t *ramdac)
{
	return 1;	/* Will be implemented later. */
}

void bt485_set_rs2(uint8_t rs2, bt485_ramdac_t *ramdac)
{
	ramdac->rs2 = rs2 ? 1 : 0;
}

void bt485_set_rs3(uint8_t rs3, bt485_ramdac_t *ramdac)
{
	ramdac->rs3 = rs3 ? 1 : 0;
}

void bt485_ramdac_out(uint16_t addr, uint8_t val, bt485_ramdac_t *ramdac, svga_t *svga)
{
//        /*if (CS!=0xC000) */pclog("OUT RAMDAC %04X %02X %i %04X:%04X  %i\n",addr,val,sdac_ramdac.magic_count,CS,pc, sdac_ramdac.rs2);
	uint8_t reg = addr & 3;
	reg |= (ramdac->rs2 ? 4 : 0);
	reg |= (ramdac->rs3 ? 8 : 0);
	pclog("BT485 RAMDAC: Writing %02X to register %02X\n", val, reg);
        svga_out(addr, val, svga);
	return;

        switch (addr)
        {
                case 0x3C6:
                if (val == 0xff)
                {
                        ramdac->rs2 = 0;
                        ramdac->magic_count = 0;
                        break;
                }
                if (ramdac->magic_count < 4) break;
                if (ramdac->magic_count == 4)
                {
                        ramdac->command = val;
//                        pclog("RAMDAC command reg now %02X\n", val);
                        switch (val >> 4)
                        {
                                case 0x2: case 0x3: case 0xa: svga->bpp = 15; break;
                                case 0x4: case 0xe:           svga->bpp = 24; break;
                                case 0x5: case 0x6: case 0xc: svga->bpp = 16; break;
                                case 0x7:                     svga->bpp = 32; break;

                                case 0: case 1: default: svga->bpp = 8; break;
                        }
			svga_recalctimings(svga);
                }
                //ramdac->magic_count = 0;
                break;
                
                case 0x3C7:
                ramdac->magic_count = 0;
                if (ramdac->rs2)
                   ramdac->rindex = val;
                break;
                case 0x3C8:
                ramdac->magic_count = 0;
                if (ramdac->rs2)
                   ramdac->windex = val;
                break;
                case 0x3C9:
                ramdac->magic_count = 0;
                if (ramdac->rs2)
                {
                        if (!ramdac->reg_ff) ramdac->regs[ramdac->windex] = (ramdac->regs[ramdac->windex] & 0xff00) | val;
                        else                 ramdac->regs[ramdac->windex] = (ramdac->regs[ramdac->windex] & 0x00ff) | (val << 8);
                        ramdac->reg_ff = !ramdac->reg_ff;
//                        pclog("RAMDAC reg %02X now %04X\n", ramdac->windex, ramdac->regs[ramdac->windex]);
                        if (!ramdac->reg_ff) ramdac->windex++;
                }
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t bt485_ramdac_in(uint16_t addr, bt485_ramdac_t *ramdac, svga_t *svga)
{
        uint8_t temp;
//        /*if (CS!=0xC000) */pclog("IN RAMDAC %04X %04X:%04X %i\n",addr,CS,pc, ramdac->rs2);
	uint8_t reg = addr & 3;
	reg |= (ramdac->rs2 ? 4 : 0);
	reg |= (ramdac->rs3 ? 8 : 0);
	pclog("BT485 RAMDAC: Reading register %02X\n", reg);
        return svga_in(addr, svga);

        switch (addr)
        {
                case 0x3C6:
                ramdac->reg_ff = 0;
                if (ramdac->magic_count < 5)
                   ramdac->magic_count++;
                if (ramdac->magic_count == 4)
                {
                        temp = 0x70; /*SDAC ID*/
                        ramdac->rs2 = 1;
                }
                if (ramdac->magic_count == 5)
                {
                        temp = ramdac->command;
                        ramdac->magic_count = 0;
                }
                return temp;
                case 0x3C7:
//                if (ramdac->magic_count < 4)
//                {
                        ramdac->magic_count=0;
//                        break;
//                }
                if (ramdac->rs2) return ramdac->rindex;
                break;
                case 0x3C8:
//                if (ramdac->magic_count < 4)
//                {
                        ramdac->magic_count=0;
//                        break;
//                }
                if (ramdac->rs2) return ramdac->windex;
                break;
                case 0x3C9:
//                if (ramdac->magic_count < 4)
//                {
                        ramdac->magic_count=0;
//                        break;
//                }
                if (ramdac->rs2)
                {
                        if (!ramdac->reg_ff) temp = ramdac->regs[ramdac->rindex] & 0xff;
                        else                 temp = ramdac->regs[ramdac->rindex] >> 8;
                        ramdac->reg_ff = !ramdac->reg_ff;
                        if (!ramdac->reg_ff)
                        {
                                ramdac->rindex++;
                                ramdac->magic_count = 0;
                        }
                        return temp;
                }
                break;
        }
        return svga_in(addr, svga);
}

float bt485_getclock(int clock, void *p)
{
        bt485_ramdac_t *ramdac = (bt485_ramdac_t *)p;
        float t;
        int m, n1, n2;
//        pclog("SDAC_Getclock %i %04X\n", clock, ramdac->regs[clock]);
        if (clock == 0) return 25175000.0;
        if (clock == 1) return 28322000.0;
        clock ^= 1; /*Clocks 2 and 3 seem to be reversed*/
        m  =  (ramdac->regs[clock] & 0x7f) + 2;
        n1 = ((ramdac->regs[clock] >>  8) & 0x1f) + 2;
        n2 = ((ramdac->regs[clock] >> 13) & 0x07);
        t = (14318184.0 * ((float)m / (float)n1)) / (float)(1 << n2);
//        pclog("BT485 clock %i %i %i %f %04X  %f %i\n", m, n1, n2, t, ramdac->regs[2], 14318184.0 * ((float)m / (float)n1), 1 << n2);
        return t;
}
