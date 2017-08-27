/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel DMA controllers.
 *
 * Version:	@(#)dma.c	1.0.2	2017/08/23
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#include "ibm.h"
#include "cpu/x86.h"
#include "mem.h"
#include "io.h"
#include "dma.h"


static uint8_t dmaregs[16];
static uint8_t dma16regs[16];
static uint8_t dmapages[16];

DMA dma, dma16;


void dma_reset(void)
{
#if 1
        int c;
        dma.wp = 0;
        for (c = 0; c < 16; c++) 
                dmaregs[c] = 0;
        for (c = 0; c < 4; c++)
        {
                dma.mode[c] = 0;
                dma.ac[c] = 0;
                dma.cc[c] = 0;
                dma.ab[c] = 0;
                dma.cb[c] = 0;
        }
        dma.m = 0;
        
        dma16.wp = 0;
        for (c = 0; c < 16; c++) 
                dma16regs[c] = 0;
        for (c = 0; c < 4; c++)
        {
                dma16.mode[c] = 0;
                dma16.ac[c] = 0;
                dma16.cc[c] = 0;
                dma16.ab[c] = 0;
                dma16.cb[c] = 0;
        }
        dma16.m = 0;
#else
	memset(dmaregs, 0, 16);
	memset(dma16regs, 0, 16);
	memset(dmapages, 0, 16);
	memset(&dma, 0, sizeof(DMA));
	memset(&dma16, 0, sizeof(DMA));
#endif
}

uint8_t dma_read(uint16_t addr, void *priv)
{
        uint8_t temp;
        switch (addr & 0xf)
        {
                case 0: case 2: case 4: case 6: /*Address registers*/
                dma.wp ^= 1;
                if (dma.wp) 
                        return dma.ac[(addr >> 1) & 3] & 0xff;
                return (dma.ac[(addr >> 1) & 3] >> 8) & 0xff;
                
                case 1: case 3: case 5: case 7: /*Count registers*/
                dma.wp ^= 1;
                if (dma.wp) temp = dma.cc[(addr >> 1) & 3] & 0xff;
                else        temp = dma.cc[(addr >> 1) & 3] >> 8;
                return temp;
                
                case 8: /*Status register*/
                temp = dma.stat;
                dma.stat = 0;
                return temp;
                
                case 0xd:
                return 0;
        }
        return dmaregs[addr & 0xf];
}

void dma_write(uint16_t addr, uint8_t val, void *priv)
{
        dmaregs[addr & 0xf] = val;
        switch (addr & 0xf)
        {
                case 0: case 2: case 4: case 6: /*Address registers*/
                dma.wp ^= 1;
                if (dma.wp) dma.ab[(addr >> 1) & 3] = (dma.ab[(addr >> 1) & 3] & 0xffff00) | val;
                else        dma.ab[(addr >> 1) & 3] = (dma.ab[(addr >> 1) & 3] & 0xff00ff) | (val << 8);
                dma.ac[(addr >> 1) & 3] = dma.ab[(addr >> 1) & 3];
                return;
                
                case 1: case 3: case 5: case 7: /*Count registers*/
                dma.wp ^= 1;
                if (dma.wp) dma.cb[(addr >> 1) & 3] = (dma.cb[(addr >> 1) & 3] & 0xff00) | val;
                else        dma.cb[(addr >> 1) & 3] = (dma.cb[(addr >> 1) & 3] & 0x00ff) | (val << 8);
                dma.cc[(addr >> 1) & 3] = dma.cb[(addr >> 1) & 3];
                return;
                
                case 8: /*Control register*/
                dma.command = val;
                return;
                
                case 0xa: /*Mask*/
                if (val & 4) dma.m |=  (1 << (val & 3));
                else         dma.m &= ~(1 << (val & 3));
                return;
                
                case 0xb: /*Mode*/
                dma.mode[val & 3] = val;
                if (dma.is_ps2)
                {
                        dma.ps2_mode[val & 3] &= ~0x1c;
                        if (val & 0x20)
                                dma.ps2_mode[val & 3] |= 0x10;
                        if ((val & 0xc) == 8)
                                dma.ps2_mode[val & 3] |= 4;
                        else if ((val & 0xc) == 4)
                                dma.ps2_mode[val & 3] |= 0xc;
                }
                return;
                
                case 0xc: /*Clear FF*/
                dma.wp = 0;
                return;
                
                case 0xd: /*Master clear*/
                dma.wp = 0;
                dma.m = 0xf;
                return;
                
                case 0xf: /*Mask write*/
                dma.m = val & 0xf;
                return;
        }
}

static uint8_t dma_ps2_read(uint16_t addr, void *priv)
{
        uint8_t temp = 0xff;
        
        switch (addr)
        {
                case 0x1a:
                switch (dma.xfr_command)
                {
                        case 2: /*Address*/
                        case 3:
                        switch (dma.byte_ptr)
                        {
                                case 0:
                                temp = (dma.xfr_channel & 4) ? (dma16.ac[dma.xfr_channel & 3] & 0xff) : (dma.ac[dma.xfr_channel] & 0xff);
                                dma.byte_ptr = 1;
                                break;
                                case 1:
                                temp = (dma.xfr_channel & 4) ? (dma16.ac[dma.xfr_channel & 3] >> 8) : (dma.ac[dma.xfr_channel] >> 8);
                                dma.byte_ptr = 2;
                                break;
                                case 2:
                                temp = (dma.xfr_channel & 4) ? (dma16.ac[dma.xfr_channel & 3] >> 16) : (dma.ac[dma.xfr_channel] >> 16);
                                dma.byte_ptr = 0;
                                break;
                        }
                        break;
                        case 4: /*Count*/
                        case 5:
                        if (dma.byte_ptr)
                                temp = (dma.xfr_channel & 4) ? (dma16.cc[dma.xfr_channel & 3] >> 8) : (dma.cc[dma.xfr_channel] >> 8);
                        else
                                temp = (dma.xfr_channel & 4) ? (dma16.cc[dma.xfr_channel & 3] & 0xff) : (dma.cc[dma.xfr_channel] & 0xff);
                        dma.byte_ptr = (dma.byte_ptr + 1) & 1;
                        break;
                        case 7: /*Mode*/
                        temp = (dma.xfr_channel & 4) ? dma16.ps2_mode[dma.xfr_channel & 3] : dma.ps2_mode[dma.xfr_channel];
                        break;
                        case 8: /*Arbitration Level*/
                        temp = (dma.xfr_channel & 4) ? dma16.arb_level[dma.xfr_channel & 3] : dma.arb_level[dma.xfr_channel];
                        break;
                        
                        default:
                        fatal("Bad XFR Read command %i channel %i\n", dma.xfr_command, dma.xfr_channel);
                }
                break;
        }

        return temp;
}

static void dma_ps2_write(uint16_t addr, uint8_t val, void *priv)
{
        uint8_t mode;

        switch (addr)
        {
                case 0x18:
                dma.xfr_channel = val & 0x7;
                dma.xfr_command = val >> 4;
                dma.byte_ptr = 0;
                switch (dma.xfr_command)
                {
                        case 9: /*Set DMA mask*/
                        if (dma.xfr_channel & 4)
                                dma16.m |= (1 << (dma.xfr_channel & 3));
                        else
                                dma.m |= (1 << dma.xfr_channel);
                        break;
                        case 0xa: /*Reset DMA mask*/
                        if (dma.xfr_channel & 4)
                                dma16.m &= ~(1 << (dma.xfr_channel & 3));
                        else
                                dma.m &= ~(1 << dma.xfr_channel);
                        break;
                }                        
                break;
                case 0x1a:
                switch (dma.xfr_command)
                {
                        case 2: /*Address*/
                        switch (dma.byte_ptr)
                        {
                                case 0:
                                if (dma.xfr_channel & 4)
                                        dma16.ac[dma.xfr_channel & 3] = (dma16.ac[dma.xfr_channel & 3] & 0xffff00) | val;
                                else
                                        dma.ac[dma.xfr_channel] = (dma.ac[dma.xfr_channel] & 0xffff00) | val;
                                dma.byte_ptr = 1;
                                break;
                                case 1:
                                if (dma.xfr_channel & 4)
                                        dma16.ac[dma.xfr_channel & 3] = (dma16.ac[dma.xfr_channel & 3] & 0xff00ff) | (val << 8);
                                else
                                        dma.ac[dma.xfr_channel] = (dma.ac[dma.xfr_channel] & 0xff00ff) | (val << 8);
                                dma.byte_ptr = 2;
                                break;
                                case 2:
                                if (dma.xfr_channel & 4)
                                        dma16.ac[dma.xfr_channel & 3] = (dma16.ac[dma.xfr_channel & 3] & 0x00ffff) | (val << 16);
                                else
                                        dma.ac[dma.xfr_channel] = (dma.ac[dma.xfr_channel] & 0x00ffff) | (val << 16);
                                dma.byte_ptr = 0;
                                break;
                        }
                        if (dma.xfr_channel & 4)
                                dma16.ab[dma.xfr_channel & 3] = dma16.ac[dma.xfr_channel & 3];
                        else
                                dma.ab[dma.xfr_channel] = dma.ac[dma.xfr_channel];
                        break;

                        case 4: /*Count*/
                        if (dma.byte_ptr)
                        {
                                if (dma.xfr_channel & 4)
                                        dma16.cc[dma.xfr_channel & 3] = (dma16.cc[dma.xfr_channel & 3] & 0xff) | (val << 8);
                                else
                                        dma.cc[dma.xfr_channel] = (dma.cc[dma.xfr_channel] & 0xff) | (val << 8);
                        }
                        else
                        {
                                if (dma.xfr_channel & 4)
                                        dma16.cc[dma.xfr_channel & 3] = (dma16.cc[dma.xfr_channel & 3] & 0xff00) | val;
                                else
                                        dma.cc[dma.xfr_channel] = (dma.cc[dma.xfr_channel] & 0xff00) | val;
                        }
                        dma.byte_ptr = (dma.byte_ptr + 1) & 1;
                        if (dma.xfr_channel & 4)
                                dma16.cb[dma.xfr_channel & 3] = dma16.cc[dma.xfr_channel & 3];
                        else
                                dma.cb[dma.xfr_channel] = dma.cc[dma.xfr_channel];
                        break;

                        case 7: /*Mode register*/
                        mode = 0;
                        if (val & 0x10)
                                mode |= 0x20;
                        if ((val & 0xc) == 4)
                                mode |= 8;
                        else if ((val & 0xc) == 0xc)
                                mode |= 4;
                        if ((val & 0x40) && !(dma.xfr_channel & 4))
                                fatal("16-bit DMA on 8-bit channel\n");
                        if (!(val & 0x40) && (dma.xfr_channel & 4))
                                fatal("8-bit DMA on 16-bit channel\n");
                        if (dma.xfr_channel & 4)
                        {
                                dma16.mode[dma.xfr_channel & 3] = (dma16.mode[dma.xfr_channel & 3] & ~0x2c) | mode;
                                dma16.ps2_mode[dma.xfr_channel & 3] = val;
                        }
                        else
                        {
                                dma.mode[dma.xfr_channel] = (dma.mode[dma.xfr_channel] & ~0x2c) | mode;
                                dma.ps2_mode[dma.xfr_channel] = val;
                        }
                        break;

                        case 8: /*Arbitration Level*/
                        if (dma.xfr_channel & 4)
                                dma16.arb_level[dma.xfr_channel & 3] = val;
                        else
                                dma.arb_level[dma.xfr_channel] = val;
                        break;

                        default:
                        fatal("Bad XFR command %i channel %i val %02x\n", dma.xfr_command, dma.xfr_channel, val);
                }
                break;
        }
}

uint8_t dma16_read(uint16_t addr, void *priv)
{
        uint8_t temp;
        addr >>= 1;
        switch (addr & 0xf)
        {
                case 0: case 2: case 4: case 6: /*Address registers*/
                dma16.wp ^= 1;
                if (dma.is_ps2)
                {
                        if (dma16.wp) 
                                return dma16.ac[(addr >> 1) & 3] & 0xff;
                        return (dma16.ac[(addr >> 1) & 3] >> 8) & 0xff;
                }
                if (dma16.wp) 
                        return (dma16.ac[(addr >> 1) & 3] >> 1) & 0xff;
                return (dma16.ac[(addr >> 1) & 3] >> 9) & 0xff;
                
                case 1: case 3: case 5: case 7: /*Count registers*/
                dma16.wp ^= 1;
                if (dma16.wp) temp = dma16.cc[(addr >> 1) & 3] & 0xff;
                else          temp = dma16.cc[(addr >> 1) & 3] >> 8;
                return temp;
                
                case 8: /*Status register*/
                temp = dma16.stat;
                dma16.stat = 0;
                return temp;
        }
        return dma16regs[addr & 0xf];
}

void dma16_write(uint16_t addr, uint8_t val, void *priv)
{
        addr >>= 1;
        dma16regs[addr & 0xf] = val;
        switch (addr & 0xf)
        {
                case 0: case 2: case 4: case 6: /*Address registers*/
                dma16.wp ^= 1;
                if (dma.is_ps2)
                {
                        if (dma16.wp) dma16.ab[(addr >> 1) & 3] = (dma16.ab[(addr >> 1) & 3] & 0xffff00) | val;
                        else          dma16.ab[(addr >> 1) & 3] = (dma16.ab[(addr >> 1) & 3] & 0xff00ff) | (val << 8);
                }
                else
                {
                        if (dma16.wp) dma16.ab[(addr >> 1) & 3] = (dma16.ab[(addr >> 1) & 3] & 0xfffe00) | (val << 1);
                        else          dma16.ab[(addr >> 1) & 3] = (dma16.ab[(addr >> 1) & 3] & 0xfe01ff) | (val << 9);
                }
                dma16.ac[(addr >> 1) & 3] = dma16.ab[(addr >> 1) & 3];
                return;
                
                case 1: case 3: case 5: case 7: /*Count registers*/
                dma16.wp ^= 1;
                if (dma16.wp) dma16.cb[(addr >> 1) & 3] = (dma16.cb[(addr >> 1) & 3] & 0xff00) | val;
                else          dma16.cb[(addr >> 1) & 3] = (dma16.cb[(addr >> 1) & 3] & 0x00ff) | (val << 8);
                dma16.cc[(addr >> 1) & 3] = dma16.cb[(addr >> 1) & 3];
                return;
                
                case 8: /*Control register*/
                return;
                
                case 0xa: /*Mask*/
                if (val & 4) dma16.m |=  (1 << (val & 3));
                else         dma16.m &= ~(1 << (val & 3));
                return;
                
                case 0xb: /*Mode*/
                dma16.mode[val & 3] = val;
                if (dma.is_ps2)
                {
                        dma16.ps2_mode[val & 3] &= ~0x1c;
                        if (val & 0x20)
                                dma16.ps2_mode[val & 3] |= 0x10;
                        if ((val & 0xc) == 8)
                                dma16.ps2_mode[val & 3] |= 4;
                        else if ((val & 0xc) == 4)
                                dma16.ps2_mode[val & 3] |= 0xc;
                }
                return;
                
                case 0xc: /*Clear FF*/
                dma16.wp = 0;
                return;
                
                case 0xd: /*Master clear*/
                dma16.wp = 0;
                dma16.m = 0xf;
                return;
                
                case 0xf: /*Mask write*/
                dma16.m = val&0xf;
                return;
        }
}


void dma_page_write(uint16_t addr, uint8_t val, void *priv)
{
        dmapages[addr & 0xf] = val;
        switch (addr & 0xf)
        {
                case 1:
                dma.page[2] = (AT) ? val : val & 0xf;
                dma.ab[2] = (dma.ab[2] & 0xffff) | (dma.page[2] << 16);
                dma.ac[2] = (dma.ac[2] & 0xffff) | (dma.page[2] << 16);
                break;
                case 2:
                dma.page[3] = (AT) ? val : val & 0xf;
                dma.ab[3] = (dma.ab[3] & 0xffff) | (dma.page[3] << 16);
                dma.ac[3] = (dma.ac[3] & 0xffff) | (dma.page[3] << 16);
                break;
                case 3:
                dma.page[1] = (AT) ? val : val & 0xf;
                dma.ab[1] = (dma.ab[1] & 0xffff) | (dma.page[1] << 16);
                dma.ac[1] = (dma.ac[1] & 0xffff) | (dma.page[1] << 16);
                break;
                case 7:
                dma.page[0] = (AT) ? val : val & 0xf;
                dma.ab[0] = (dma.ab[0] & 0xffff) | (dma.page[0] << 16);
                dma.ac[0] = (dma.ac[0] & 0xffff) | (dma.page[0] << 16);
                break;
                case 0x9:
                dma16.page[2] = val & 0xfe;
                dma16.ab[2] = (dma16.ab[2] & 0x1ffff) | (dma16.page[2] << 16);
                dma16.ac[2] = (dma16.ac[2] & 0x1ffff) | (dma16.page[2] << 16);
                break;
                case 0xa:
                dma16.page[3] = val & 0xfe;
                dma16.ab[3] = (dma16.ab[3] & 0x1ffff) | (dma16.page[3] << 16);
                dma16.ac[3] = (dma16.ac[3] & 0x1ffff) | (dma16.page[3] << 16);
                break;
                case 0xb:
                dma16.page[1] = val & 0xfe;
                dma16.ab[1] = (dma16.ab[1] & 0x1ffff) | (dma16.page[1] << 16);
                dma16.ac[1] = (dma16.ac[1] & 0x1ffff) | (dma16.page[1] << 16);
                break;
                case 0xf:
                dma16.page[0] = val & 0xfe;
                dma16.ab[0] = (dma16.ab[0] & 0x1ffff) | (dma16.page[0] << 16);
                dma16.ac[0] = (dma16.ac[0] & 0x1ffff) | (dma16.page[0] << 16);
                break;
        }
}

uint8_t dma_page_read(uint16_t addr, void *priv)
{
        return dmapages[addr & 0xf];
}

void dma_init(void)
{
        io_sethandler(0x0000, 0x0010, dma_read,      NULL, NULL, dma_write,      NULL, NULL,  NULL);
        io_sethandler(0x0080, 0x0008, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
        dma.is_ps2 = 0;
}

void dma16_init(void)
{
        io_sethandler(0x00C0, 0x0020, dma16_read,    NULL, NULL, dma16_write,    NULL, NULL,  NULL);
        io_sethandler(0x0088, 0x0008, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
}

void dma_alias_set(void)
{
        io_sethandler(0x0090, 0x0010, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
}

void dma_alias_remove(void)
{
        io_removehandler(0x0090, 0x0010, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
}

void dma_alias_remove_piix(void)
{
        io_removehandler(0x0090, 0x0001, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
        io_removehandler(0x0094, 0x0003, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
        io_removehandler(0x0098, 0x0001, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
        io_removehandler(0x009C, 0x0003, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
}

void ps2_dma_init(void)
{
        io_sethandler(0x0018, 0x0001, dma_ps2_read,  NULL, NULL, dma_ps2_write,  NULL, NULL,  NULL);
        io_sethandler(0x001a, 0x0001, dma_ps2_read,  NULL, NULL, dma_ps2_write,  NULL, NULL,  NULL);
        dma.is_ps2 = 1;
}


uint8_t _dma_read(uint32_t addr)
{
        uint8_t temp = mem_readb_phys(addr);
        return temp;
}

void _dma_write(uint32_t addr, uint8_t val)
{
        mem_writeb_phys(addr, val);
        mem_invalidate_range(addr, addr);
}

int dma_channel_read(int channel)
{
        uint16_t temp;
        int tc = 0;
        
	if (dma.command & 0x04)
		return DMA_NODATA;
		
        if (!AT)
                refreshread();
        
        if (channel < 4)
        {
		if (dma.m & (1 << channel))
                        return DMA_NODATA;
                if ((dma.mode[channel] & 0xC) != 8)
                        return DMA_NODATA;

                temp = _dma_read(dma.ac[channel]);

                if (dma.mode[channel] & 0x20)
                {
                        if (dma.is_ps2)
                                dma.ac[channel]--;
                        else
                                dma.ac[channel] = (dma.ac[channel] & 0xff0000) | ((dma.ac[channel] - 1) & 0xffff);
                }
                else
                {
                        if (dma.is_ps2)
                                dma.ac[channel]++;
                        else
                                dma.ac[channel] = (dma.ac[channel] & 0xff0000) | ((dma.ac[channel] + 1) & 0xffff);
                }
                dma.cc[channel]--;
                if (dma.cc[channel] < 0)
                {
                        tc = 1;
                        if (dma.mode[channel] & 0x10) /*Auto-init*/
                        {
                                dma.cc[channel] = dma.cb[channel];
                                dma.ac[channel] = dma.ab[channel];
                        }
                        else
                                dma.m |= (1 << channel);
                        dma.stat |= (1 << channel);
                }

                if (tc)
                        return temp | DMA_OVER;
                return temp;
        }
        else
        {
                channel &= 3;
                if (dma16.m & (1 << channel))
                        return DMA_NODATA;
                if ((dma16.mode[channel] & 0xC) != 8)
                        return DMA_NODATA;

                temp =  _dma_read(dma16.ac[channel]) |
                       (_dma_read(dma16.ac[channel] + 1) << 8);

                if (dma16.mode[channel] & 0x20)
                {
                        if (dma.is_ps2)
                                dma16.ac[channel] -= 2;
                        else
                                dma16.ac[channel] = (dma16.ac[channel] & 0xfe0000) | ((dma16.ac[channel] - 2) & 0x1ffff);
                }
                else
                {
                        if (dma.is_ps2)
                                dma16.ac[channel] += 2;
                        else
                                dma16.ac[channel] = (dma16.ac[channel] & 0xfe0000) | ((dma16.ac[channel] + 2) & 0x1ffff);
                }

                dma16.cc[channel]--;
                if (dma16.cc[channel] < 0)
                {
                        tc = 1;
                        if (dma16.mode[channel] & 0x10) /*Auto-init*/
                        {
                                dma16.cc[channel] = dma16.cb[channel];
                                dma16.ac[channel] = dma16.ab[channel];
                        }
                        else
                                dma16.m |= (1 << channel);
                        dma16.stat |= (1 << channel);
                }

                if (tc)
                        return temp | DMA_OVER;
                return temp;
        }
}

int dma_channel_write(int channel, uint16_t val)
{
	if (dma.command & 0x04)
		return DMA_NODATA;

        if (!AT)
                refreshread();

        if (channel < 4)
        {
                if (dma.m & (1 << channel))
                        return DMA_NODATA;
                if ((dma.mode[channel] & 0xC) != 4)
                        return DMA_NODATA;

                _dma_write(dma.ac[channel], val);

                if (dma.mode[channel] & 0x20)
                {
                        if (dma.is_ps2)
                                dma.ac[channel]--;
                        else
                                dma.ac[channel] = (dma.ac[channel] & 0xff0000) | ((dma.ac[channel] - 1) & 0xffff);
                }
                else
                {
                        if (dma.is_ps2)
                                dma.ac[channel]++;
                        else
                                dma.ac[channel] = (dma.ac[channel] & 0xff0000) | ((dma.ac[channel] + 1) & 0xffff);
                }

                dma.cc[channel]--;
                if (dma.cc[channel] < 0)
                {
                        if (dma.mode[channel] & 0x10) /*Auto-init*/
                        {
                                dma.cc[channel] = dma.cb[channel];
                                dma.ac[channel] = dma.ab[channel];
                        }
                        else
                                dma.m |= (1 << channel);
                        dma.stat |= (1 << channel);
                }

                if (dma.m & (1 << channel))
                        return DMA_OVER;
        }
        else
        {
                channel &= 3;
                if (dma16.m & (1 << channel))
                        return DMA_NODATA;
                if ((dma16.mode[channel] & 0xC) != 4)
                        return DMA_NODATA;

                _dma_write(dma16.ac[channel],     val);
                _dma_write(dma16.ac[channel] + 1, val >> 8);                

                if (dma16.mode[channel] & 0x20)
                {
                        if (dma.is_ps2)
                                dma16.ac[channel] -= 2;
                        else
                                dma16.ac[channel] = (dma16.ac[channel] & 0xfe0000) | ((dma16.ac[channel] - 2) & 0x1ffff);
                }
                else
                {
                        if (dma.is_ps2)
                                dma16.ac[channel] += 2;
                        else
                                dma16.ac[channel] = (dma16.ac[channel] & 0xfe0000) | ((dma16.ac[channel] + 2) & 0x1ffff);
                }

                dma16.cc[channel]--;
                if (dma16.cc[channel] < 0)
                {
                        if (dma16.mode[channel] & 0x10) /*Auto-init*/
                        {
                                dma16.cc[channel] = dma16.cb[channel] + 1;
                                dma16.ac[channel] = dma16.ab[channel];
                        }
			else
                                dma16.m |= (1 << channel);
                        dma16.stat |= (1 << channel);
                }

                if (dma16.m & (1 << channel))
                        return DMA_OVER;
        }
        return 0;
}

int dma_mode(int channel)
{
	if (channel < 4)
	{
		return dma.mode[channel];
	}
	else
	{
		return dma16.mode[channel & 3];
	}
}

/* DMA Bus Master Page Read/Write */
void DMAPageRead(uint32_t PhysAddress, char *DataRead, uint32_t TotalSize)
{
	memcpy(DataRead, &ram[PhysAddress], TotalSize);
}

void DMAPageWrite(uint32_t PhysAddress, const char *DataWrite, uint32_t TotalSize)
{
	mem_invalidate_range(PhysAddress, PhysAddress + TotalSize - 1);
	memcpy(&ram[PhysAddress], DataWrite, TotalSize);
}
