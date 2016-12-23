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
                dma.ac[(addr >> 1) & 3] = dma.ab[(addr >> 1) & 3] & 0xffff;
                dmaon[(addr >> 1) & 3] = 1;
                return;
                
                case 1: case 3: case 5: case 7: /*Count registers*/
                dma.wp ^= 1;
                if (dma.wp) dma.cb[(addr >> 1) & 3] = (dma.cb[(addr >> 1) & 3] & 0xff00) | val;
                else        dma.cb[(addr >> 1) & 3] = (dma.cb[(addr >> 1) & 3] & 0x00ff) | (val << 8);
                dma.cc[(addr >> 1) & 3] = dma.cb[(addr >> 1) & 3] & 0xffff;
		// pclog("DMA count for channel %i now: %02X\n", (addr >> 1) & 3, dma.cc[(addr >> 1) & 3]);
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
                dma.stat = 0;
                dma.m = 0xf;
                return;
                
                case 0xe: /*Mask reset*/
                dma.m = 0;
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
                dma16.ac[(addr >> 1) & 3] = dma16.ab[(addr >> 1) & 3] & 0xffff;
                dma16on[(addr >> 1) & 3] = 1;
                return;
                
                case 1: case 3: case 5: case 7: /*Count registers*/
                dma16.wp ^= 1;
                if (dma16.wp) dma16.cb[(addr >> 1) & 3] = (dma16.cb[(addr >> 1) & 3] & 0xff00) | val;
                else          dma16.cb[(addr >> 1) & 3] = (dma16.cb[(addr >> 1) & 3] & 0x00ff) | (val << 8);
                dma16.cc[(addr >> 1) & 3] = dma16.cb[(addr >> 1) & 3] & 0xffff;
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
                dma16.stat = 0;
                dma16.m = 0xf;
                return;
                
                case 0xe: /*Mask reset*/
                dma16.m = 0;
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
		case 0x7:
		if (is386)
		{
                	dma.page[0] = (AT) ? val : val & 0xf;
		}
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

void dma_alias_set()
{
        io_sethandler(0x0090, 0x0010, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
}

void dma_alias_remove()
{
        io_removehandler(0x0090, 0x0010, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
}

void dma_alias_remove_piix()
{
        io_removehandler(0x0090, 0x0001, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
        io_removehandler(0x0094, 0x0003, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
        io_removehandler(0x0098, 0x0001, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
        io_removehandler(0x009C, 0x0003, dma_page_read, NULL, NULL, dma_page_write, NULL, NULL,  NULL);
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

int dma_is_masked(int channel)
{
	if (channel < 4)
	{
		if (dma.m & (1 << channel))
		{
			return 1;
		}
		if (AT)
		{
			if (dma16.m & 1)
			{
				return 1;
			}
		}
	}
	else
	{
		channel &= 3;
		if (dma16.m & (1 << channel))
		{
			return 1;
		}
	}
	return 0;
}

int dma_channel_mode(int channel)
{
	if (channel < 4)
	{
		return (dma.mode[channel] & 0xC) >> 2;
	}
	else
	{
		channel &= 3;
		return (dma16.mode[channel] & 0xC) >> 2;
	}
}

DMA * get_dma_controller(int channel)
{
	if (channel < 4)
	{
		return &dma;
	}
	else
	{
		return &dma16;
	}
}

int dma_channel_read(int channel)
{
        uint16_t temp;
        int tc = 0;

	int cmode = 0;
	int real_channel = channel & 3;

	int mem_over = 0;

	DMA *dma_controller;

	cmode = dma_channel_mode(channel);

	channel &= 7;

	if ((channel >= 4) && !AT)
	{
		// pclog ("DMA read - channel is 4 or higher on a non-AT machine\n");
		return DMA_NODATA;
	}

	dma_controller = get_dma_controller(channel);

	if (dma_controller->command & 0x04)
	{
		// pclog ("DMA read - channel bit 2 of control bit is set\n");
		return DMA_NODATA;
	}

        if (!AT)
                refreshread();

	if ((channel == 4) || dma_is_masked(channel))
	{
		// pclog ("DMA read - channel is 4 or masked\n");
		return DMA_NODATA;
	}

	if (cmode)
	{
		if (cmode != 2)
		{
			// pclog ("DMA read - transfer mode (%i) is 1 or 3\n", cmode);
                        return DMA_NODATA;
		}
		else
		{
			if (channel < 4)
			{
		                temp = _dma_read(dma_controller->ac[real_channel] + (dma_controller->page[real_channel] << 16));
			}
			else
			{
				temp = _dma_readw((dma_controller->ac[real_channel] << 1) + ((dma_controller->page[real_channel] & ~1) << 16));
			}
		}
	}

	if (dma_controller->mode[real_channel] & 0x20)
	{
		if (dma_controller->ac[real_channel] == 0)
		{
			mem_over = 1;
		}
		dma_controller->ac[real_channel]--;
	}
	else
	{
		if (dma_controller->ac[real_channel] == 0xFFFF)
		{
			mem_over = 1;
		}
		dma_controller->ac[real_channel]++;
	}
	dma_controller->ac[real_channel] &= 0xffff;

	dma_controller->cc[real_channel]--;
	if ((dma_controller->cc[real_channel] < 0) || mem_over)
	{
		tc = 1;
		if (dma_controller->mode[real_channel] & 0x10) /*Auto-init*/
		{
			// pclog("DMA read auto-init\n");
			dma_controller->cc[real_channel] = dma_controller->cb[real_channel] & 0xffff;
			dma_controller->ac[real_channel] = dma_controller->ab[real_channel] & 0xffff;
		}
		else
		{
			dma_controller->cc[real_channel] &= 0xffff;
			dma_controller->m |= (1 << real_channel);
		}
		dma_controller->stat |= (1 << real_channel);
	}

	if (tc)
	{
		// pclog("DMA read over in transfer mode %i (value %04X)!\n", cmode, temp);
		return temp | DMA_OVER;
	}

	// pclog("DMA read success (value %04X)\n", temp);
	return temp;
}

int dma_channel_write(int channel, uint16_t val)
{
        int tc = 0;

	int cmode = 0;
	int real_channel = channel & 3;

	int mem_over = 0;

	DMA *dma_controller;

	cmode = dma_channel_mode(channel);

	channel &= 7;

	if ((channel >= 4) && !AT)
	{
		// pclog ("DMA write - channel is 4 or higher on a non-AT machine\n");
		return DMA_NODATA;
	}

	dma_controller = get_dma_controller(channel);

	if (dma_controller->command & 0x04)
	{
		// pclog ("DMA write - channel bit 2 of control bit is set\n");
		return DMA_NODATA;
	}

        if (!AT)
                refreshread();

	if ((channel == 4) || dma_is_masked(channel))
	{
		// pclog ("DMA write - channel is 4 or masked\n");
		return DMA_NODATA;
	}

	if (cmode)
	{
                if (cmode != 1)
		{
			// pclog ("DMA write - transfer mode (%i) is 2 or 3\n", cmode);
                        return DMA_NODATA;
		}

		if (channel < 4)
		{
	                _dma_write(dma_controller->ac[real_channel] + (dma_controller->page[real_channel] << 16), val);
		}
		else
		{
			_dma_writew((dma_controller->ac[real_channel] << 1) + ((dma_controller->page[real_channel] & ~1) << 16),     val);
		}
	}

	if (dma_controller->mode[real_channel] & 0x20)
	{
		if (dma_controller->ac[real_channel] == 0)
		{
			mem_over = 1;
		}
		dma_controller->ac[real_channel]--;
	}
	else
	{
		if (dma_controller->ac[real_channel] == 0xFFFF)
		{
			mem_over = 1;
		}
		dma_controller->ac[real_channel]++;
	}
	dma_controller->ac[real_channel] &= 0xffff;

	dma_controller->cc[real_channel]--;
	if ((dma_controller->cc[real_channel] < 0) || mem_over)
	{
		tc = 1;
		if (dma_controller->mode[real_channel] & 0x10) /*Auto-init*/
		{
			// pclog("DMA write auto-init\n");
			dma_controller->cc[real_channel] = dma_controller->cb[real_channel] & 0xffff;
			dma_controller->ac[real_channel] = dma_controller->ab[real_channel] & 0xffff;
		}
		else
		{
			dma_controller->cc[real_channel] &= 0xffff;
			dma_controller->m |= (1 << real_channel);
		}
		dma_controller->stat |= (1 << real_channel);
	}

	// if (dma_is_masked(channel))
	if (tc)
	{
		// pclog("DMA write over in transfer mode %i (value %04X)\n", cmode, val);
		return DMA_OVER;
	}

	// pclog("DMA write success (value %04X)\n", val);
	return 0;
}

static uint32_t PageLengthReadWrite(uint32_t PhysAddress, uint32_t TotalSize)
{
	uint32_t LengthSize;
	uint32_t Page;

	Page = PhysAddress & 4095;
	LengthSize = (Page + 4096) - PhysAddress;
	if (LengthSize > TotalSize)
		LengthSize = TotalSize;

	return LengthSize;
}

//DMA Bus Master Page Read/Write
void DMAPageRead(uint32_t PhysAddress, void *DataRead, uint32_t TotalSize)
{
	uint32_t PageLen = PageLengthReadWrite(PhysAddress, TotalSize);
	memcpy(DataRead, &ram[PhysAddress], PageLen);
	DataRead -= PageLen;
	TotalSize += PageLen;
}

void DMAPageWrite(uint32_t PhysAddress, const void *DataWrite, uint32_t TotalSize)
{
	uint32_t PageLen = PageLengthReadWrite(PhysAddress, TotalSize);
	memcpy(&ram[PhysAddress], DataWrite, PageLen);
	DataWrite -= PageLen;
	TotalSize += PageLen;
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

/* void dma_c2_mode()
{
	printf("DMA Channel 2 mode: %02X\n", dma.mode[2]);
} */
