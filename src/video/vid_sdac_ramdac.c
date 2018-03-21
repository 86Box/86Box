/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		87C716 'SDAC' true colour RAMDAC emulation.
 *
 * Version:	@(#)vid_sdac_ramdac.c	1.0.3	2018/03/21
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
#include <wchar.h>
#include "../86box.h"
#include "../mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_sdac_ramdac.h"

static void sdac_control_write(sdac_ramdac_t *ramdac, svga_t *svga, uint8_t val)
{
        ramdac->command = val;
        switch (val >> 4)
        {
                case 0x2: case 0x3: case 0xa: svga->bpp = 15; break;
                case 0x4: case 0xe:           svga->bpp = 24; break;
                case 0x5: case 0x6: case 0xc: svga->bpp = 16; break;
                case 0x7:                     svga->bpp = 32; break;

                case 0: case 1: default: svga->bpp = 8; break;
        }
}

static void sdac_reg_write(sdac_ramdac_t *ramdac, int reg, uint8_t val)
{
        if ((reg >= 2 && reg <= 7) || (reg == 0xa) || (reg == 0xe))
        {
                if (!ramdac->reg_ff)
                        ramdac->regs[reg] = (ramdac->regs[reg] & 0xff00) | val;
                else
                        ramdac->regs[reg] = (ramdac->regs[reg] & 0x00ff) | (val << 8);
        }
        ramdac->reg_ff = !ramdac->reg_ff;
        if (!ramdac->reg_ff)
                ramdac->windex++;
}

static uint8_t sdac_reg_read(sdac_ramdac_t *ramdac, int reg)
{
        uint8_t temp;
        
        if (!ramdac->reg_ff)
                temp = ramdac->regs[reg] & 0xff;
        else
                temp = ramdac->regs[reg] >> 8;
        ramdac->reg_ff = !ramdac->reg_ff;
        if (!ramdac->reg_ff)
                ramdac->rindex++;

        return temp;
}

void sdac_ramdac_out(uint16_t addr, uint8_t val, sdac_ramdac_t *ramdac, svga_t *svga)
{
        switch (addr)
        {               
                case 2:
                if (ramdac->magic_count == 4)
                        sdac_control_write(ramdac, svga, val);
                ramdac->magic_count = 0;
                break;
                
                case 3:
                ramdac->magic_count = 0;
                break;
                case 0:
                ramdac->magic_count = 0;
                break;
                case 1:
                ramdac->magic_count = 0;
                break;

                case 4:
                ramdac->windex = val;
                ramdac->reg_ff = 0;
                break;
                case 5:
                sdac_reg_write(ramdac, ramdac->windex & 0xff, val);
                break;
                case 6:
                sdac_control_write(ramdac, svga, val);
                break;
                case 7:
                ramdac->rindex = val;
                ramdac->reg_ff = 0;
                break;
        }
        if (!(addr & 4))
        {
                if (addr < 2)
                        svga_out(addr + 0x3c8, val, svga);
                else
                        svga_out(addr + 0x3c4, val, svga);
        }
}

uint8_t sdac_ramdac_in(uint16_t addr, sdac_ramdac_t *ramdac, svga_t *svga)
{
        uint8_t temp;
        switch (addr)
        {
                case 2:
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
                case 3:
                ramdac->magic_count=0;
                break;
                case 0:
                ramdac->magic_count=0;
                break;
                case 1:
                ramdac->magic_count=0;
                break;

                case 4:
                return ramdac->windex;
                case 5:
                return sdac_reg_read(ramdac, ramdac->rindex & 0xff);
                case 6:
                return ramdac->command;
                case 7:
                return ramdac->rindex;
        }
        if (!(addr & 4))
        {
                if (addr < 2)
                        return svga_in(addr + 0x3c8, svga);
                else
                        return svga_in(addr + 0x3c4, svga);
        }
        return 0xff;
}

float sdac_getclock(int clock, void *p)
{
        sdac_ramdac_t *ramdac = (sdac_ramdac_t *)p;
        float t;
        int m, n1, n2;
        if (clock == 0) return 25175000.0;
        if (clock == 1) return 28322000.0;
        clock ^= 1; /*Clocks 2 and 3 seem to be reversed*/
        m  =  (ramdac->regs[clock] & 0x7f) + 2;
        n1 = ((ramdac->regs[clock] >>  8) & 0x1f) + 2;
        n2 = ((ramdac->regs[clock] >> 13) & 0x07);
        t = (14318184.0 * ((float)m / (float)n1)) / (float)(1 << n2);
        return t;
}

void sdac_init(sdac_ramdac_t *ramdac)
{
        ramdac->regs[0] = 0x6128;
        ramdac->regs[1] = 0x623d;
}
