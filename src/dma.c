/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"

#include "disc.h"
#include "dma.h"
#include "fdc.h"
#include "io.h"
#include "mem.h"
#include "video.h"

static uint8_t dmaregs[16];
static int dmaon[4];
static uint8_t dma16regs[16];
static int dma16on[4];
static uint8_t dmapages[16];

void dma_reset()
{
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
}

uint8_t dma_read(uint16_t addr, void *priv)
{
        uint8_t temp;
//        printf("Read DMA %04X %04X:%04X %i %02X\n",addr,CS,pc, pic_intpending, pic.pend);
        switch (addr & 0xf)
        {
                case 0: case 2: case 4: case 6: /*Address registers*/
                dma.wp ^= 1;
                if (dma.wp) 
                        return dma.ac[(addr >> 1) & 3] & 0xff;
                return dma.ac[(addr >> 1) & 3] >> 8;
                
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
//        printf("Bad DMA read %04X %04X:%04X\n",addr,CS,pc);
        return dmaregs[addr & 0xf];
}

void dma_write(uint16_t addr, uint8_t val, void *priv)
{
//        printf("Write DMA %04X %02X %04X:%04X\n",addr,val,CS,pc);
        dmaregs[addr & 0xf] = val;
        switch (addr & 0xf)
        {
                case 0: case 2: case 4: case 6: /*Address registers*/
                dma.wp ^= 1;
                if (dma.wp) dma.ab[(addr >> 1) & 3] = (dma.ab[(addr >> 1) & 3] & 0xff00) | val;
                else        dma.ab[(addr >> 1) & 3] = (dma.ab[(addr >> 1) & 3] & 0x00ff) | (val << 8);
                dma.ac[(addr >> 1) & 3] = dma.ab[(addr >> 1) & 3];
                dmaon[(addr >> 1) & 3] = 1;
                return;
                
                case 1: case 3: case 5: case 7: /*Count registers*/
                dma.wp ^= 1;
                if (dma.wp) dma.cb[(addr >> 1) & 3] = (dma.cb[(addr >> 1) & 3] & 0xff00) | val;
                else        dma.cb[(addr >> 1) & 3] = (dma.cb[(addr >> 1) & 3] & 0x00ff) | (val << 8);
                dma.cc[(addr >> 1) & 3] = dma.cb[(addr >> 1) & 3];
                dmaon[(addr >> 1) & 3] = 1;
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

uint8_t dma16_read(uint16_t addr, void *priv)
{
        uint8_t temp;
//        printf("Read DMA %04X %04X:%04X\n",addr,cs>>4,pc);
        addr >>= 1;
        switch (addr & 0xf)
        {
                case 0: case 2: case 4: case 6: /*Address registers*/
                dma16.wp ^= 1;
                if (dma16.wp) 
                        return dma16.ac[(addr >> 1) & 3] & 0xff;
                return dma16.ac[(addr >> 1) & 3] >> 8;
                
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
//        printf("Write dma16 %04X %02X %04X:%04X\n",addr,val,CS,pc);
        addr >>= 1;
        dma16regs[addr & 0xf] = val;
        switch (addr & 0xf)
        {
                case 0: case 2: case 4: case 6: /*Address registers*/
                dma16.wp ^= 1;
                if (dma16.wp) dma16.ab[(addr >> 1) & 3] = (dma16.ab[(addr >> 1) & 3] & 0xff00) | val;
                else          dma16.ab[(addr >> 1) & 3] = (dma16.ab[(addr >> 1) & 3] & 0x00ff) | (val << 8);
                dma16.ac[(addr >> 1) & 3] = dma16.ab[(addr >> 1) & 3];
                dma16on[(addr >> 1) & 3] = 1;
                return;
                
                case 1: case 3: case 5: case 7: /*Count registers*/
                dma16.wp ^= 1;
                if (dma16.wp) dma16.cb[(addr >> 1) & 3] = (dma16.cb[(addr >> 1) & 3] & 0xff00) | val;
                else          dma16.cb[(addr >> 1) & 3] = (dma16.cb[(addr >> 1) & 3] & 0x00ff) | (val << 8);
                dma16.cc[(addr >> 1) & 3] = dma16.cb[(addr >> 1) & 3];
                dma16on[(addr >> 1) & 3] = 1;
                return;
                
                case 8: /*Control register*/
                return;
                
                case 0xa: /*Mask*/
                if (val & 4) dma16.m |=  (1 << (val & 3));
                else         dma16.m &= ~(1 << (val & 3));
                return;
                
                case 0xb: /*Mode*/
                dma16.mode[val & 3] = val;
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
                break;
                case 2:
                dma.page[3] = (AT) ? val : val & 0xf;
                break;
                case 3:
                dma.page[1] = (AT) ? val : val & 0xf;
                break;
                case 0x9:
                dma16.page[2] = val;
                break;
                case 0xa:
                dma16.page[3] = val;
                break;
                case 0xb:
                dma16.page[1] = val;
                break;
        }
}

uint8_t dma_page_read(uint16_t addr, void *priv)
{
        return dmapages[addr & 0xf];
}

void dma_init()
{
        io_sethandler(0x0000, 0x0010, dma_read,      NULL, NULL, dma_write,      NULL, NULL,  NULL);
        io_sethandler(0x0080, 0x0008, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
}

void dma16_init()
{
        io_sethandler(0x00C0, 0x0020, dma16_read,    NULL, NULL, dma16_write,    NULL, NULL,  NULL);
        io_sethandler(0x0088, 0x0008, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
}


uint8_t _dma_read(uint32_t addr)
{
        return mem_readb_phys(addr);
}

uint16_t _dma_readw(uint32_t addr)
{
        return mem_readw_phys(addr);
}

void _dma_write(uint32_t addr, uint8_t val)
{
        mem_writeb_phys(addr, val);
        mem_invalidate_range(addr, addr);
}

void _dma_writew(uint32_t addr, uint16_t val)
{
        mem_writew_phys(addr, val);
        mem_invalidate_range(addr, addr + 1);
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

                temp = _dma_read(dma.ac[channel] + (dma.page[channel] << 16)); //ram[(dma.ac[2]+(dma.page[2]<<16))&rammask];

                if (dma.mode[channel] & 0x20) dma.ac[channel]--;
                else                          dma.ac[channel]++;
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

#if 0
                temp =  _dma_read((dma16.ac[channel] << 1) + ((dma16.page[channel] & ~1) << 16)) |
                       (_dma_read((dma16.ac[channel] << 1) + ((dma16.page[channel] & ~1) << 16) + 1) << 8);
#endif

		temp = _dma_readw((dma16.ac[channel] << 1) + ((dma16.page[channel] & ~1) << 16));

                if (dma16.mode[channel] & 0x20) dma16.ac[channel]--;
                else                            dma16.ac[channel]++;
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

void dma_channel_dump()
{
	int i = 0;
	FILE *f;
	f = fopen("dma.dmp", "wb");
	for (i = 0; i < (21 * 512); i++)
	{
		fputc(mem_readb_phys((dma.page[2] << 16) + dma16.ac[2] + i), f);
	}
	fclose(f);
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

                _dma_write(dma.ac[channel] + (dma.page[channel] << 16), val);

                if (dma.mode[channel]&0x20) dma.ac[channel]--;
                else                        dma.ac[channel]++;
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

#if 0
                _dma_write((dma16.ac[channel] << 1) + ((dma16.page[channel] & ~1) << 16),     val);
                _dma_write((dma16.ac[channel] << 1) + ((dma16.page[channel] & ~1) << 16) + 1, val >> 8);                
#endif

		_dma_writew((dma16.ac[channel] << 1) + ((dma16.page[channel] & ~1) << 16),     val);

                if (dma16.mode[channel]&0x20) dma16.ac[channel]--;
                else                          dma16.ac[channel]++;
                dma16.cc[channel]--;
                if (dma16.cc[channel] < 0)
                {
                        if (dma16.mode[channel] & 0x10) /*Auto-init*/
                        {
                                dma16.cc[channel] = dma16.cb[channel] + 1;
                                dma16.ac[channel] = dma16.ab[channel];
                        }
                                dma16.m |= (1 << channel);
                        dma16.stat |= (1 << channel);
                }

                if (dma.m & (1 << channel))
                        return DMA_OVER;
        }
        return 0;
}

size_t PageLengthReadWrite(uint32_t Address, size_t TotalSize)
{
	size_t l;
	uint32_t Page;

	Page = Address & 4095;
	l = (Page + 4096) - Address;
	if (l > TotalSize)
		l = TotalSize;

	return l;
}
