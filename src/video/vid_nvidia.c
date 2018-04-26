/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		nVidia RIVA 128 emulation.
 *
 * Version:	@(#)vid_nv_riva128.c	1.0.6	2018/04/26
 *
 * Author:	Melissa Goad
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2015-2018 Melissa Goad.
 *		Copyright 2015-2018 Miran Grca.
 */
 
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../machine/machine.h"
#include "../io.h"
#include "../mem.h"
#include "../pci.h"
#include "../pic.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "../plat.h"
#include "video.h"
#include "vid_nv_riva128.h"
#include "vid_svga.h"
#include "vid_svga_render.h"

typedef struct riva128_t
{
	mem_mapping_t   linear_mapping;
	mem_mapping_t     mmio_mapping;

	rom_t bios_rom;

	svga_t svga;

	uint8_t card_id;
	int pci_card;
	int is_nv3t;

	uint16_t vendor_id;
	uint16_t device_id;

	uint32_t linear_base, linear_size;

	uint16_t rma_addr;

	uint8_t pci_regs[256];

	int memory_size;

	uint8_t ext_regs_locked;

	uint8_t read_bank;
	uint8_t write_bank;

	struct
	{
		uint32_t intr;
		uint32_t intr_en;
		uint32_t intr_line;
		uint32_t enable;
	} pmc;

	struct
	{
		uint32_t intr;
		uint32_t intr_en;
	} pbus;

	struct
	{
		uint32_t cache_error;
		uint32_t intr;
		uint32_t intr_en;

		uint32_t ramht;
		uint32_t ramht_addr;
		uint32_t ramht_size;

		uint32_t ramfc;
		uint32_t ramfc_addr;

		uint32_t ramro;
		uint32_t ramro_addr;
		uint32_t ramro_size;

		uint16_t chan_mode;
		uint16_t chan_dma;
		uint16_t chan_size; //0 = 1024, 1 = 512

		uint32_t runout_put, runout_get;

		struct
		{
			uint32_t dmaput;
			uint32_t dmaget;
		} channels[16];

		struct
		{
			int chanid;
			int push_enabled;
			int runout;
			uint32_t get, put;
			uint32_t ctx;
		} caches[2];

		struct
		{
			int subchan;
			uint16_t method;
			uint32_t param;
		} cache0, cache1[64];
	} pfifo;

	struct
	{
		uint32_t addr;
		uint32_t data;
		uint8_t access_reg[4];
		uint8_t mode;
	} rma;

	struct
	{
		uint32_t intr, intr_en;

		uint64_t time;
		uint32_t alarm;

		uint16_t clock_mul, clock_div;
	} ptimer;

	struct
	{
		int width;
		int bpp;
		uint32_t config_0;
	} pfb;

	struct
	{
		uint32_t boot_0;
	} pextdev;

	struct
	{
		int pgraph_speedhack;

		uint32_t obj_handle[8];
		uint16_t obj_class[8];

		uint32_t debug[5];

		uint32_t intr;
		uint32_t intr_en;

		uint32_t invalid;
		uint32_t invalid_en;

		uint32_t ctx_switch[5];
		uint32_t ctx_control;
		uint32_t ctx_user;
		uint32_t ctx_cache[8][5];

		uint32_t fifo_enable;

		uint32_t fifo_st2_addr;
		uint32_t fifo_st2_data;

		uint32_t uclip_xmin, uclip_ymin, uclip_xmax, uclip_ymax;
		uint32_t oclip_xmin, oclip_ymin, oclip_xmax, oclip_ymax;

		uint32_t src_canvas_min, src_canvas_max;
		uint32_t dst_canvas_min, dst_canvas_max;

		uint8_t rop;

		uint32_t chroma;

		uint32_t beta;

		uint32_t notify;

		//NV3
		uint32_t surf_offset[4];
		uint32_t surf_pitch[4];

		uint32_t cliprect_min[2];
		uint32_t cliprect_max[2];
		uint32_t cliprect_ctrl;

		uint32_t instance;

		uint32_t dma_intr, dma_intr_en;

		uint32_t status;
	} pgraph;

	struct
	{
		uint32_t nvpll;
		uint32_t nv_m,nv_n,nv_p;

		uint32_t mpll;
		uint32_t m_m,m_n,m_p;

		uint32_t vpll;
		uint32_t v_m,v_n,v_p;

		uint32_t pll_ctrl;

		uint32_t gen_ctrl;
	} pramdac;

	uint32_t channels[16][8][0x2000];

	struct
	{
		int scl;
		int sda;
	} i2c;

	int64_t mtime, mfreq;
} riva128_t;

uint8_t riva128_rma_in(uint16_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	svga_t* svga = &riva128->svga;
	uint8_t ret = 0;

	addr &= 0xff;

	//pclog("RIVA 128 RMA read %04X %04X:%08X\n", addr, CS, cpu_state.pc);

	switch(addr)
	{
	case 0x00:
		ret = 0x65;
		break;
	case 0x01:
		ret = 0xd0;
		break;
	case 0x02:
		ret = 0x16;
		break;
	case 0x03:
		ret = 0x2b;
		break;
	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
		if(riva128->rma.addr < 0x1000000) /*ret = riva128_mmio_read((riva128->rma.addr + (addr & 3)) & 0xffffff, riva128);*/pclog("RIVA 128 MMIO write %08x %08x\n", riva128->rma.addr & 0xffffff, riva128->rma.data);
		else ret = svga_read_linear((riva128->rma.addr - 0x1000000), svga);
		break;
	}

	return ret;
}

void riva128_rma_out(uint16_t addr, uint8_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	svga_t* svga = &riva128->svga;

	addr &= 0xff;

	//pclog("RIVA 128 RMA write %04X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);

	switch(addr)
	{
	case 0x04:
		riva128->rma.addr &= ~0xff;
		riva128->rma.addr |= val;
		break;
	case 0x05:
		riva128->rma.addr &= ~0xff00;
		riva128->rma.addr |= (val << 8);
		break;
	case 0x06:
		riva128->rma.addr &= ~0xff0000;
		riva128->rma.addr |= (val << 16);
		break;
	case 0x07:
		riva128->rma.addr &= ~0xff000000;
		riva128->rma.addr |= (val << 24);
		break;
	case 0x08:
	case 0x0c:
	case 0x10:
	case 0x14:
		riva128->rma.data &= ~0xff;
		riva128->rma.data |= val;
		break;
	case 0x09:
	case 0x0d:
	case 0x11:
	case 0x15:
		riva128->rma.data &= ~0xff00;
		riva128->rma.data |= (val << 8);
		break;
	case 0x0a:
	case 0x0e:
	case 0x12:
	case 0x16:
		riva128->rma.data &= ~0xff0000;
		riva128->rma.data |= (val << 16);
		break;
	case 0x0b:
	case 0x0f:
	case 0x13:
	case 0x17:
		riva128->rma.data &= ~0xff000000;
		riva128->rma.data |= (val << 24);
		if(riva128->rma.addr < 0x1000000) /*riva128_mmio_write_l(riva128->rma.addr & 0xffffff, riva128->rma.data, riva128);*/pclog("RIVA 128 MMIO write %08x %08x\n", riva128->rma.addr & 0xffffff, riva128->rma.data);
		else svga_writel_linear((riva128->rma.addr - 0x1000000), riva128->rma.data, svga);
		break;
	}

	if(addr & 0x10) riva128->rma.addr+=4;
}

uint8_t riva128_in(uint16_t addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	svga_t* svga = &riva128->svga;
	uint8_t ret = 0;

	if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
		addr ^= 0x60;

	//        if (addr != 0x3da) pclog("S3 in %04X %04X:%08X  ", addr, CS, cpu_state.pc);
	switch (addr)
	{
	case 0x3D4:
		ret = svga->crtcreg;
		break;
	case 0x3D5:
		switch(svga->crtcreg)
		{
		case 0x28:
			ret = svga->crtc[0x28] & 0x3f;
			break;
		case 0x34:
			ret = svga->displine & 0xff;
			break;
		case 0x35:
			ret = (svga->displine >> 8) & 7;
			break;
		case 0x3e:
			//DDC status register
			ret = (riva128->i2c.sda << 3) | (riva128->i2c.scl << 2);
			break;
		default:
			ret = svga->crtc[svga->crtcreg];
			break;
		}
		//if(svga->crtcreg > 0x18)
		//  pclog("RIVA 128 Extended CRTC read %02X %04X:%08X\n", svga->crtcreg, CS, cpu_state.pc);
		break;
	default:
		ret = svga_in(addr, svga);
		break;
	}
	//        if (addr != 0x3da) pclog("%02X\n", ret);
	return ret;
}

void riva128_out(uint16_t addr, uint8_t val, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	svga_t *svga = &riva128->svga;

	uint8_t old;

	if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
		addr ^= 0x60;

	switch(addr)
	{
	case 0x3D4:
		svga->crtcreg = val;
		return;
	case 0x3D5:
		if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
			return;
		if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
			val = (svga->crtc[7] & ~0x10) | (val & 0x10);
		old = svga->crtc[svga->crtcreg];
		svga->crtc[svga->crtcreg] = val;
		switch(svga->crtcreg)
		{
		case 0x1e:
			riva128->read_bank = val;
			if (svga->chain4) svga->read_bank = riva128->read_bank << 15;
			else              svga->read_bank = riva128->read_bank << 13;
			break;
		case 0x1d:
			riva128->write_bank = val;
			if (svga->chain4) svga->write_bank = riva128->write_bank << 15;
			else              svga->write_bank = riva128->write_bank << 13;
			break;
		case 0x19:
        case 0x1a:
		case 0x25:
		case 0x28:
		case 0x2d:
			svga_recalctimings(svga);
			break;
		case 0x38:
			riva128->rma.mode = val & 0xf;
			break;
		case 0x3f:
			riva128->i2c.sda = (val >> 4) & 1;
			riva128->i2c.scl = (val >> 5) & 1;
			break;
		}
		//if(svga->crtcreg > 0x18)
		//  pclog("RIVA 128 Extended CRTC write %02X %02x %04X:%08X\n", svga->crtcreg, val, CS, cpu_state.pc);
		if (old != val)
		{
			if (svga->crtcreg < 0xE || svga->crtcreg > 0x10)
			{
				svga->fullchange = changeframecount;
				svga_recalctimings(svga);
			}
		}
		return;
	}

	svga_out(addr, val, svga);
}

uint8_t riva128_pci_read(int func, int addr, void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	uint8_t ret = 0;
	//pclog("RIVA 128 PCI read %02X %04X:%08X\n", addr, CS, cpu_state.pc);
	switch (addr)
	{
	case 0x00:
		ret = riva128->vendor_id & 0xff;
		break;
	case 0x01:
		ret = riva128->vendor_id >> 8;
		break;

	case 0x02:
		ret = riva128->device_id & 0xff;
		break;
	case 0x03:
		ret = riva128->device_id >> 8;
		break;

	case 0x04:
		ret = riva128->pci_regs[0x04] & 0x37;
		break;
	case 0x05:
		ret = riva128->pci_regs[0x05] & 0x01;
		break;

	case 0x06:
		ret = 0x20;
		break;
	case 0x07:
		ret = riva128->pci_regs[0x07] & 0x73;
		break;

	case 0x08:
		ret = 0x00;
		break; /*Revision ID*/
	case 0x09:
		ret = 0;
		break; /*Programming interface*/

	case 0x0a:
		ret = 0x00;
		break; /*Supports VGA interface*/
	case 0x0b:
		ret = 0x03; /*output = 3; */break;

	case 0x0e:
		ret = 0x00;
		break; /*Header type*/

	case 0x13:
	case 0x17:
		ret = riva128->pci_regs[addr];
		break;

	case 0x2c:
	case 0x2d:
	case 0x2e:
	case 0x2f:
		ret = riva128->pci_regs[addr];
		//if(CS == 0x0028) output = 3;
		break;

	case 0x30:
		return riva128->pci_regs[0x30] & 0x01; /*BIOS ROM address*/
	case 0x31:
		return 0x00;
	case 0x32:
		return riva128->pci_regs[0x32];
	case 0x33:
		return riva128->pci_regs[0x33];

	case 0x34:
		ret = 0x00;
		break;

	case 0x3c:
		ret = riva128->pci_regs[0x3c];
		break;

	case 0x3d:
		ret = 0x01;
		break; /*INTA*/

	case 0x3e:
		ret = 0x03;
		break;
	case 0x3f:
		ret = 0x01;
		break;

	}
	//        pclog("%02X\n", ret);
	return ret;
}

void riva128_reenable_svga_mappings(svga_t *svga)
{
	switch (svga->gdcreg[6] & 0xc) /*Banked framebuffer*/
	{
	case 0x0: /*128k at A0000*/
		mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
		svga->banked_mask = 0xffff;
		break;
	case 0x4: /*64k at A0000*/
		mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
		svga->banked_mask = 0xffff;
		break;
	case 0x8: /*32k at B0000*/
		mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
		svga->banked_mask = 0x7fff;
		break;
	case 0xC: /*32k at B8000*/
		mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
		svga->banked_mask = 0x7fff;
		break;
	}
}

void riva128_pci_write(int func, int addr, uint8_t val, void *p)
{
	//pclog("RIVA 128 PCI write %02X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);
	riva128_t *riva128 = (riva128_t *)p;
	svga_t* svga = &riva128->svga;
	switch (addr)
	{
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x3d:
	case 0x3e:
	case 0x3f:
		return;

	case PCI_REG_COMMAND:
		riva128->pci_regs[PCI_REG_COMMAND] = val & 0x27;
		mem_mapping_disable(&svga->mapping);
		mem_mapping_disable(&riva128->mmio_mapping);
		mem_mapping_disable(&riva128->linear_mapping);
		if (val & PCI_COMMAND_IO)
		{
			io_removehandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);
			io_sethandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);
		}
		else io_removehandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);
		if (val & PCI_COMMAND_MEM)
		{
			uint32_t mmio_addr = riva128->pci_regs[0x13] << 24;
			uint32_t linear_addr = riva128->pci_regs[0x17] << 24;
			if (!mmio_addr && !linear_addr)
			{
				riva128_reenable_svga_mappings(svga);
			}
			if (mmio_addr)
			{
				mem_mapping_set_addr(&riva128->mmio_mapping, mmio_addr, 0x1000000);
			}
			if (linear_addr)
			{
				mem_mapping_set_addr(&riva128->linear_mapping, linear_addr, 0x1000000);
			}
		}
		return;

	case 0x05:
		riva128->pci_regs[0x05] = val & 0x01;
		return;

	case 0x07:
		riva128->pci_regs[0x07] = (riva128->pci_regs[0x07] & 0x8f) | (val & 0x70);
		return;

	case 0x13:
	{
		uint32_t mmio_addr;
		riva128->pci_regs[addr] = val;
		mmio_addr = riva128->pci_regs[0x13] << 24;
		mem_mapping_disable(&riva128->mmio_mapping);
		if (mmio_addr)
		{
			mem_mapping_set_addr(&riva128->mmio_mapping, mmio_addr, 0x1000000);
		}
		return;
	}

	case 0x17:
	{
		uint32_t linear_addr;
		riva128->pci_regs[addr] = val;
		linear_addr = riva128->pci_regs[0x17] << 24;
		mem_mapping_disable(&riva128->linear_mapping);
		if (linear_addr)
		{
			mem_mapping_set_addr(&riva128->linear_mapping, linear_addr, 0x1000000);
		}
		return;
	}

	case 0x30:
	case 0x32:
	case 0x33:
		riva128->pci_regs[addr] = val;
		mem_mapping_disable(&riva128->bios_rom.mapping);
		if (riva128->pci_regs[0x30] & 0x01)
		{
			uint32_t addr = (riva128->pci_regs[0x32] << 16) | (riva128->pci_regs[0x33] << 24);
			//                        pclog("RIVA 128 bios_rom enabled at %08x\n", addr);
			mem_mapping_set_addr(&riva128->bios_rom.mapping, addr, 0x8000);
		}
		return;

	case 0x3c:
		riva128->pci_regs[0x3c] = val & 0x0f;
		return;

	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
		riva128->pci_regs[addr - 0x14] = val; //0x40-0x43 are ways to write to 0x2c-0x2f
		return;
	}
}

void riva128_recalctimings(svga_t *svga)
{
	riva128_t *riva128 = (riva128_t *)svga->p;

	svga->ma_latch += (svga->crtc[0x19] & 0x1f) << 16;
	svga->rowoffset += (svga->crtc[0x19] & 0xe0) << 3;
	if (svga->crtc[0x25] & 0x01) svga->vtotal      += 0x400;
	if (svga->crtc[0x25] & 0x02) svga->dispend     += 0x400;
	if (svga->crtc[0x25] & 0x04) svga->vblankstart += 0x400;
	if (svga->crtc[0x25] & 0x08) svga->vsyncstart  += 0x400;
	if (svga->crtc[0x25] & 0x10) svga->htotal      += 0x100;
	if (svga->crtc[0x2d] & 0x01) svga->hdisp       += 0x100;
	//The effects of the large screen bit seem to just be doubling the row offset.
	//However, these large modes still don't work. Possibly core SVGA bug? It does report 640x2 res after all.
	//if (!(svga->crtc[0x1a] & 0x04)) svga->rowoffset <<= 1;
	switch(svga->crtc[0x28] & 3)
	{
	case 1:
		svga->bpp = 8;
		svga->lowres = 0;
		svga->render = svga_render_8bpp_highres;
		break;
	case 2:
		svga->bpp = 16;
		svga->lowres = 0;
		svga->render = svga_render_16bpp_highres;
		break;
	case 3:
		svga->bpp = 32;
		svga->lowres = 0;
		svga->render = svga_render_32bpp_highres;
		break;
	}

	/*if((svga->crtc[0x28] & 3) != 0)
	{
	  if(svga->crtc[0x1a] & 2) svga_set_ramdac_type(svga, RAMDAC_6BIT);
	  else svga_set_ramdac_type(svga, RAMDAC_8BIT);
	}
	else svga_set_ramdac_type(svga, RAMDAC_6BIT);*/

	double freq;

	if (((svga->miscout >> 2) & 2) == 2)
	{
		freq = 13500000.0;

		if(riva128->pramdac.v_m == 0) riva128->pramdac.v_m = 1;
		else
		{
			freq = (freq * riva128->pramdac.v_n) / (1 << riva128->pramdac.v_p) / riva128->pramdac.v_m;
			//pclog("RIVA 128 Pixel clock is %f Hz\n", freq);
		}

		svga->clock = cpuclock / freq;
	}

	if(riva128->card_id == 0x03)
	{
		freq = 13500000.0;

		if(riva128->pramdac.m_m == 0) riva128->pramdac.m_m = 1;
		else
		{
			freq = (freq * riva128->pramdac.m_n) / (1 << riva128->pramdac.m_p) / riva128->pramdac.m_m;
			//pclog("RIVA 128 Memory clock is %f Hz\n", freq);
		}

		riva128->mfreq = freq;
		riva128->mtime = (int64_t)((TIMER_USEC * 100000000.0) / riva128->mfreq);
	}
}


void *riva128_init(const device_t *info)
{
	riva128_t *riva128 = malloc(sizeof(riva128_t));
	memset(riva128, 0, sizeof(riva128_t));

	riva128->card_id = 0x03;
	riva128->is_nv3t = 0;

	riva128->vendor_id = 0x12d2;
	riva128->device_id = 0x0018;

	riva128->memory_size = device_get_config_int("memory");

	svga_init(&riva128->svga, riva128, riva128->memory_size << 20,
	          riva128_recalctimings,
	          riva128_in, riva128_out,
	          NULL, NULL);

	riva128->svga.decode_mask = (riva128->memory_size << 20) - 1;

	rom_init(&riva128->bios_rom, L"roms/video/nv_riva128/Diamond_V330_rev-e.vbi", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
	if (PCI)
		mem_mapping_disable(&riva128->bios_rom.mapping);

	/*mem_mapping_add(&riva128->mmio_mapping,     0, 0,
	                riva128_mmio_read,
	                riva128_mmio_read_w,
	                riva128_mmio_read_l,
	                riva128_mmio_write,
	                riva128_mmio_write_w,
	                riva128_mmio_write_l,
	                NULL,
	                0,
	                riva128);*/

	mem_mapping_add(&riva128->linear_mapping,   0, 0,
	                svga_read_linear,
	                svga_readw_linear,
	                svga_readl_linear,
	                svga_write_linear,
	                svga_writew_linear,
	                svga_writel_linear,
	                NULL,
	                0,
	                &riva128->svga);

	io_sethandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);

	// riva128->pci_regs[4] = 3;
	riva128->pci_regs[4] = 7;
	riva128->pci_regs[5] = 0;
	riva128->pci_regs[6] = 0;
	riva128->pci_regs[7] = 2;

	riva128->pci_regs[0x2c] = 0xd2;
	riva128->pci_regs[0x2d] = 0x12;
	riva128->pci_regs[0x2e] = 0x00;
	riva128->pci_regs[0x2f] = 0x03;

	riva128->pci_regs[0x30] = 0x00;
	riva128->pci_regs[0x32] = 0x0c;
	riva128->pci_regs[0x33] = 0x00;

	riva128->pmc.intr = 0;
	riva128->pbus.intr = 0;
	riva128->pfifo.intr = 0;
	riva128->pgraph.intr = 0;
	riva128->ptimer.intr = 0;

	riva128->pci_card = pci_add_card(PCI_ADD_VIDEO, riva128_pci_read, riva128_pci_write, riva128);

	riva128->ptimer.clock_mul = 1;
	riva128->ptimer.clock_div = 1;

	//default values so that the emulator can boot. These'll be overwritten by the video BIOS anyway.
	riva128->pramdac.m_m = 0x03;
	riva128->pramdac.m_n = 0xc2;
	riva128->pramdac.m_p = 0x0d;

	//timer_add(riva128_mclk_poll, &riva128->mtime, &timer_one, riva128);

	return riva128;
}

void riva128_close(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;
	FILE *f = fopen("vram.dmp", "wb");
	fwrite(riva128->svga.vram, 4 << 20, 1, f);
	fclose(f);

	svga_close(&riva128->svga);

	free(riva128);
}

int riva128_available(void)
{
	return rom_present(L"roms/video/nv_riva128/Diamond_V330_rev-e.vbi");
}

void riva128_speed_changed(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	svga_recalctimings(&riva128->svga);
}

void riva128_force_redraw(void *p)
{
	riva128_t *riva128 = (riva128_t *)p;

	riva128->svga.fullchange = changeframecount;
}

const device_config_t riva128_config[] =
{
	{
		"memory", "Memory size", CONFIG_SELECTION, "", 4,
		{
			{
				"1 MB", 1
			},
			{
				"2 MB", 2
			},
			{
				"4 MB", 4
			},
			{
				""
			}
		},
	},
	{
		"", "", -1
	}
};

#if 0
const device_config_t riva128zx_config[] =
{
	{
		.name = "memory",
		.description = "Memory size",
		.type = CONFIG_SELECTION,
		.selection =
		{
			{
				.description = "1 MB",
				.value = 1
			},
			{
				.description = "2 MB",
				.value = 2
			},
			{
				.description = "4 MB",
				.value = 4
			},
			{
				.description = "8 MB",
				.value = 8
			},
			{
				.description = ""
			}
		},
		.default_int = 4
	},
	{
		.type = -1
	}
};
#endif

const device_t riva128_device =
{
	"nVidia RIVA 128",
	DEVICE_PCI,
	0,
	riva128_init,
	riva128_close,
	NULL,
	riva128_available,
	riva128_speed_changed,
	riva128_force_redraw,
	riva128_config
};