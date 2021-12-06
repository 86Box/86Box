/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of nVidia's RIVA TNT graphics card.
 *		Special thanks to Marcelina Kościelnicka, without whom this
 *		would not have been possible.
 *
 * Version:	@(#)vid_rivatnt.c	1.0.0	2019/09/13
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Melissa Goad
 *
 *		Copyright 2020 Miran Grca.
 *		Copyright 2020 Melissa Goad.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include "../cpu/cpu.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

#define BIOS_RIVATNT_PATH		"roms/video/nvidia/NV4_diamond_revB.rom"

#define RIVATNT_VENDOR_ID 0x10de
#define RIVATNT_DEVICE_ID 0x0020

typedef struct rivatnt_t
{
	mem_mapping_t	mmio_mapping;
	mem_mapping_t 	linear_mapping;

	svga_t		svga;

	rom_t		bios_rom;

	uint32_t		vram_size, vram_mask,
			mmio_base, lfb_base;

	uint8_t		read_bank, write_bank;

	uint8_t		pci_regs[256];
	uint8_t		int_line;

	int			card;

	struct
	{
		uint8_t rma_access_reg[4];
		uint8_t rma_mode;
		uint32_t rma_dst_addr;
		uint32_t rma_data;
	} rma;

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
		uint32_t cache_error;

		struct
		{
			uint32_t push_enabled, pull_enabled;
			uint32_t status0, status1;
			uint32_t put, get;
		} caches[2];
	} pfifo;

	struct
	{
		uint32_t intr, intr_en;

		uint64_t time;
		uint32_t alarm;

		uint16_t clock_mul, clock_div;
	} ptimer;

	struct
	{
		uint16_t width;
		int bpp;

		uint32_t config_0;
	} pfb;
	
	struct
	{
		uint32_t intr, intr_en;
	} pcrtc;

	struct
	{
		uint32_t fifo_enable;
	} pgraph;
	

	struct
	{
		uint32_t nvpll, mpll, vpll;
	} pramdac;

	uint32_t ramin[0x100000/4];

	pc_timer_t nvtimer;
	pc_timer_t mtimer;

	double nvtime;
	double mtime;

	void *i2c, *ddc;
} rivatnt_t;

static video_timings_t timing_rivatnt		= {VIDEO_PCI, 2,  2,  1,  20, 20, 21};

static uint8_t rivatnt_in(uint16_t addr, void *p);
static void rivatnt_out(uint16_t addr, uint8_t val, void *p);

static uint8_t 
rivatnt_pci_read(int func, int addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	// svga_t *svga = &rivatnt->svga;

    //pclog("RIVA TNT PCI read %02x\n", addr);

	switch (addr) {
	case 0x00: return 0xde; /*nVidia*/
	case 0x01: return 0x10;

	case 0x02: return 0x20;
	case 0x03: return 0x00;
	
	case 0x04: return rivatnt->pci_regs[0x04] & 0x37; /*Respond to IO and memory accesses*/
	case 0x05: return rivatnt->pci_regs[0x05] & 0x03;

	case 0x06: return 0x18;
	case 0x07: return 0x02;

	case 0x08: return 0x00; /*Revision ID*/
	case 0x09: return 0x00; /*Programming interface*/

	case 0x0a: return 0x00; /*Supports VGA interface*/
	case 0x0b: return 0x03;

	case 0x13: return rivatnt->mmio_base >> 24;

	case 0x17: return rivatnt->lfb_base >> 24;

	case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		return rivatnt->pci_regs[addr];

	case 0x30: return (rivatnt->pci_regs[0x30] & 0x01); /*BIOS ROM address*/
	case 0x31: return 0x00;
	case 0x32: return rivatnt->pci_regs[0x32];
	case 0x33: return rivatnt->pci_regs[0x33];

	case 0x3c: return rivatnt->int_line;
	case 0x3d: return PCI_INTA;

	case 0x3e: return 0x03;
	case 0x3f: return 0x01;
	}

	return 0x00;
}


static void 
rivatnt_recalc_mapping(rivatnt_t *rivatnt)
{
	svga_t *svga = &rivatnt->svga;
		
	if (!(rivatnt->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
	//pclog("PCI mem off\n");
		mem_mapping_disable(&svga->mapping);
		mem_mapping_disable(&rivatnt->mmio_mapping);
		mem_mapping_disable(&rivatnt->linear_mapping);
	return;
	}

	//pclog("PCI mem on\n");
	//pclog("rivatnt->mmio_base = %08X\n", rivatnt->mmio_base);
	if (rivatnt->mmio_base)
		mem_mapping_set_addr(&rivatnt->mmio_mapping, rivatnt->mmio_base, 0x1000000);
	else
		mem_mapping_disable(&rivatnt->mmio_mapping);

	//pclog("rivatnt->lfb_base = %08X\n", rivatnt->lfb_base);
	if (rivatnt->lfb_base) {
	mem_mapping_set_addr(&rivatnt->linear_mapping, rivatnt->lfb_base, 0x1000000);
	} else {
		mem_mapping_disable(&rivatnt->linear_mapping);
	}

	switch (svga->gdcreg[6] & 0x0c) {
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


static void 
rivatnt_pci_write(int func, int addr, uint8_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

    //pclog("RIVA TNT PCI write %02x %02x\n", addr, val);

	switch (addr) {
	case PCI_REG_COMMAND:
		rivatnt->pci_regs[PCI_REG_COMMAND] = val & 0x37;
		io_removehandler(0x03c0, 0x0020, rivatnt_in, NULL, NULL, rivatnt_out, NULL, NULL, rivatnt);
		if (val & PCI_COMMAND_IO)
			io_sethandler(0x03c0, 0x0020, rivatnt_in, NULL, NULL, rivatnt_out, NULL, NULL, rivatnt);
		rivatnt_recalc_mapping(rivatnt);
		break;

	case 0x05:
		rivatnt->pci_regs[0x05] = val & 0x01;
		break;

	case 0x13:
		rivatnt->mmio_base = val << 24;
		rivatnt_recalc_mapping(rivatnt);
		break;

	case 0x17: 
		rivatnt->lfb_base = val << 24;
		rivatnt_recalc_mapping(rivatnt);
		break;

	case 0x30: case 0x32: case 0x33:
		rivatnt->pci_regs[addr] = val;
		if (rivatnt->pci_regs[0x30] & 0x01) {
			uint32_t addr = (rivatnt->pci_regs[0x32] << 16) | (rivatnt->pci_regs[0x33] << 24);
			mem_mapping_set_addr(&rivatnt->bios_rom.mapping, addr, 0x10000);
		} else
			mem_mapping_disable(&rivatnt->bios_rom.mapping);
		break;

	case 0x3c:
		rivatnt->int_line = val;
		break;

	case 0x40: case 0x41: case 0x42: case 0x43:
		/* 0x40-0x43 are ways to write to 0x2c-0x2f */
		rivatnt->pci_regs[0x2c + (addr & 0x03)] = val;
		break;
	}
}

uint32_t
rivatnt_pmc_recompute_intr(void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	uint32_t intr = 0;
	if(rivatnt->pfifo.intr & rivatnt->pfifo.intr_en) intr |= (1 << 8);
	if(rivatnt->ptimer.intr & rivatnt->ptimer.intr_en) intr |= (1 << 20);
	if(rivatnt->pcrtc.intr & rivatnt->pcrtc.intr_en) intr |= (1 << 24);
	if(rivatnt->pmc.intr & (1u << 31)) intr |= (1u << 31);
	
	if((intr & 0x7fffffff) && (rivatnt->pmc.intr_en & 1)) pci_set_irq(rivatnt->card, PCI_INTA);
	else if((intr & (1 << 31)) && (rivatnt->pmc.intr_en & 2)) pci_set_irq(rivatnt->card, PCI_INTA);
	//else pci_clear_irq(rivatnt->card, PCI_INTA);
	return intr;
}

uint32_t
rivatnt_pmc_read(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	switch(addr)
	{
	case 0x000000:
		return 0x20104010; //ID register.
	case 0x000100:
		return rivatnt_pmc_recompute_intr(rivatnt);
	case 0x000140:
		return rivatnt->pmc.intr_en;
	case 0x000200:
		return rivatnt->pmc.enable;
	}
	return 0;
}

void
rivatnt_pmc_write(uint32_t addr, uint32_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	switch(addr)
	{
	case 0x000100:
		rivatnt->pmc.intr = val & (1u << 31);
		rivatnt_pmc_recompute_intr(rivatnt);
		break;
	case 0x000140:
		rivatnt->pmc.intr_en = val & 3;
		rivatnt_pmc_recompute_intr(rivatnt);
		break;
	case 0x000200:
		rivatnt->pmc.enable = val;
		break;
	}
}

uint32_t
rivatnt_pfifo_read(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	switch(addr)
	{
	case 0x002080:
		return rivatnt->pfifo.cache_error;
	case 0x002100:
		return rivatnt->pfifo.intr;
	case 0x002140:
		return rivatnt->pfifo.intr_en;
	}
	return 0;
}

void
rivatnt_pfifo_write(uint32_t addr, uint32_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	switch(addr)
	{
	case 0x002100:
	{
		uint32_t tmp = rivatnt->pfifo.intr & ~val;
		rivatnt->pfifo.intr = tmp;
		pci_clear_irq(rivatnt->card, PCI_INTA);
		if(!(rivatnt->pfifo.intr & 1)) rivatnt->pfifo.cache_error = 0;
		break;
	}
	case 0x002140:
		rivatnt->pfifo.intr_en = val & 0x11111;
		rivatnt_pmc_recompute_intr(rivatnt);
		break;
	case 0x003000:
		rivatnt->pfifo.caches[0].push_enabled = val & 1;
		break;
	case 0x003010:
		rivatnt->pfifo.caches[0].put = val;
		break;
	case 0x003050:
		rivatnt->pfifo.caches[0].pull_enabled = val & 1;
		break;
	case 0x003070:
		rivatnt->pfifo.caches[0].get = val;
		break;
	case 0x003200:
		rivatnt->pfifo.caches[1].push_enabled = val & 1;
		break;
	case 0x003210:
		rivatnt->pfifo.caches[1].put = val;
		break;
	case 0x003250:
		rivatnt->pfifo.caches[1].pull_enabled = val & 1;
		break;
	case 0x003270:
		rivatnt->pfifo.caches[1].get = val;
		break;
	}
}

void
rivatnt_ptimer_interrupt(int num, void *p)
{
	//nv_riva_log("RIVA TNT PTIMER interrupt #%d fired!\n", num);
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	rivatnt->ptimer.intr |= (1 << num);

	rivatnt_pmc_recompute_intr(rivatnt);
}

uint32_t
rivatnt_ptimer_read(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	switch(addr)
	{
	case 0x009100:
		return rivatnt->ptimer.intr;
	case 0x009140:
		return rivatnt->ptimer.intr_en;
	case 0x009200:
		return rivatnt->ptimer.clock_div;
	case 0x009210:
		return rivatnt->ptimer.clock_mul;
	case 0x009400:
		return rivatnt->ptimer.time & 0xffffffffULL;
	case 0x009410:
		return rivatnt->ptimer.time >> 32;
	case 0x009420:
		return rivatnt->ptimer.alarm;
	}
	return 0;
}

void
rivatnt_ptimer_write(uint32_t addr, uint32_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	switch(addr)
	{
	case 0x009100:
		rivatnt->ptimer.intr &= ~val;
		rivatnt_pmc_recompute_intr(rivatnt);
		break;
	case 0x009140:
		rivatnt->ptimer.intr_en = val & 1;
		rivatnt_pmc_recompute_intr(rivatnt);
		break;
	case 0x009200:
		if(!(uint16_t)val) val = 1;
		rivatnt->ptimer.clock_div = (uint16_t)val;
		break;
	case 0x009210:
		rivatnt->ptimer.clock_mul = (uint16_t)val;
		break;
	case 0x009400:
		rivatnt->ptimer.time &= 0x0fffffff00000000ULL;
		rivatnt->ptimer.time |= val & 0xffffffe0;
		break;
	case 0x009410:
		rivatnt->ptimer.time &= 0xffffffe0;
		rivatnt->ptimer.time |= (uint64_t)(val & 0x0fffffff) << 32;
		break;
	case 0x009420:
		rivatnt->ptimer.alarm = val & 0xffffffe0;
		//HACK to make wfw3.11 not take forever to start
		if(val == 0xffffffff)
		{
			rivatnt_ptimer_interrupt(0, rivatnt);
		}
		break;
	}
}

uint32_t
rivatnt_pfb_read(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	switch(addr)
	{
		case 0x100000:
			switch(rivatnt->vram_size)
			{
				case 4 << 20: return 0x15;
				case 8 << 20: return 0x16;
				case 16 << 20: return 0x17;
			}
			break;
	}

	return 0;
}

uint32_t
rivatnt_pextdev_read(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	switch(addr)
	{
		case 0x101000:
			return 0x0000019e;
	}

	return 0;
}

uint32_t
rivatnt_pcrtc_read(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	switch(addr)
	{
		case 0x600100:
			return rivatnt->pcrtc.intr;
		case 0x600140:
			return rivatnt->pcrtc.intr_en;
	}
	return 0;
}

void
rivatnt_pcrtc_write(uint32_t addr, uint32_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	switch(addr)
	{
		case 0x600100:
			rivatnt->pcrtc.intr &= ~val;
			rivatnt_pmc_recompute_intr(rivatnt);
			break;
		case 0x600140:
			rivatnt->pcrtc.intr_en = val & 1;
			rivatnt_pmc_recompute_intr(rivatnt);
			break;
	}
}

uint32_t
rivatnt_pramdac_read(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	switch(addr)
	{
		case 0x680500:
			return rivatnt->pramdac.nvpll;
		case 0x680504:
			return rivatnt->pramdac.mpll;
		case 0x680508:
			return rivatnt->pramdac.vpll;
	}
	return 0;
}

void
rivatnt_pramdac_write(uint32_t addr, uint32_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	switch(addr)
	{
		case 0x680500:
			rivatnt->pramdac.nvpll = val;
			break;
		case 0x680504:
			rivatnt->pramdac.mpll = val;
			break;
		case 0x680508:
			rivatnt->pramdac.vpll = val;
			break;
	}
	svga_recalctimings(&rivatnt->svga);
}

void
rivatnt_ptimer_tick(void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	//pclog("[RIVA TNT] PTIMER tick! mul %04x div %04x\n", rivatnt->ptimer.clock_mul, rivatnt->ptimer.clock_div);

	double time = ((double)rivatnt->ptimer.clock_mul * 10.0) / (double)rivatnt->ptimer.clock_div; //Multiply by 10 to avoid timer system limitations.
	uint32_t tmp;
	int alarm_check;

	//if(cs == 0x0008 && !rivatnt->pgraph.beta) nv_riva_log("RIVA TNT PTIMER time elapsed %f alarm %08x, time_low %08x\n", time, rivatnt->ptimer.alarm, rivatnt->ptimer.time & 0xffffffff);

	tmp = rivatnt->ptimer.time;
	rivatnt->ptimer.time += (uint64_t)time;

	alarm_check = (uint32_t)(rivatnt->ptimer.time - rivatnt->ptimer.alarm) & 0x80000000;

	//alarm_check = ((uint32_t)rivatnt->ptimer.time >= (uint32_t)rivatnt->ptimer.alarm);

	//pclog("[RIVA TNT] Timer %08x %016llx %08x %d\n", rivatnt->ptimer.alarm, rivatnt->ptimer.time, tmp, alarm_check);

	if(alarm_check)
	{
		pclog("[RIVA TNT] PTIMER ALARM interrupt fired!\n");
		rivatnt_ptimer_interrupt(0, rivatnt);
	}
}

void
rivatnt_nvclk_poll(void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	rivatnt_ptimer_tick(rivatnt);
	timer_on_auto(&rivatnt->nvtimer, rivatnt->nvtime);
}

void
rivatnt_mclk_poll(void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	timer_on_auto(&rivatnt->mtimer, rivatnt->mtime);
}

uint32_t
rivatnt_mmio_read_l(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	addr &= 0xffffff;

	uint32_t ret = 0;

	switch(addr) {
	case 0x6013b4: case 0x6013b5:
	case 0x6013d4: case 0x6013d5:
	case 0x6013da:
	case 0x0c03c2: case 0x0c03c3: case 0x0c03c4: case 0x0c03c5: case 0x0c03cc:
	case 0x6813c6: case 0x6813c7: case 0x6813c8: case 0x6813c9: case 0x6813ca: case 0x6813cb:
		ret = (rivatnt_in((addr+0) & 0x3ff,p) << 0) | (rivatnt_in((addr+1) & 0x3ff,p) << 8) | (rivatnt_in((addr+2) & 0x3ff,p) << 16) | (rivatnt_in((addr+3) & 0x3ff,p) << 24);
		break;
	}

	addr &= 0xfffffc;

	if ((addr >= 0x000000) && (addr <= 0x000fff)) ret = rivatnt_pmc_read(addr, rivatnt);
	if ((addr >= 0x002000) && (addr <= 0x003fff)) ret = rivatnt_pfifo_read(addr, rivatnt);
	if ((addr >= 0x009000) && (addr <= 0x009fff)) ret = rivatnt_ptimer_read(addr, rivatnt);
	if ((addr >= 0x100000) && (addr <= 0x100fff)) ret = rivatnt_pfb_read(addr, rivatnt);
	if ((addr >= 0x101000) && (addr <= 0x101fff)) ret = rivatnt_pextdev_read(addr, rivatnt);
	if ((addr >= 0x600000) && (addr <= 0x600fff)) ret = rivatnt_pcrtc_read(addr, rivatnt);
	if ((addr >= 0x680000) && (addr <= 0x680fff)) ret = rivatnt_pramdac_read(addr, rivatnt);
	if ((addr >= 0x700000) && (addr <= 0x7fffff)) ret = rivatnt->ramin[(addr & 0xfffff) >> 2];
	if ((addr >= 0x300000) && (addr <= 0x30ffff)) ret = ((uint32_t *) rivatnt->bios_rom.rom)[(addr & rivatnt->bios_rom.mask) >> 2];

	if ((addr >= 0x1800) && (addr <= 0x18ff))
		ret = (rivatnt_pci_read(0,(addr+0) & 0xff,p) << 0) | (rivatnt_pci_read(0,(addr+1) & 0xff,p) << 8) | (rivatnt_pci_read(0,(addr+2) & 0xff,p) << 16) | (rivatnt_pci_read(0,(addr+3) & 0xff,p) << 24);

	if(addr != 0x9400) pclog("[RIVA TNT] MMIO read %08x returns value %08x\n", addr, ret);

	return ret;
}


uint8_t
rivatnt_mmio_read(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	addr &= 0xffffff;

	if ((addr >= 0x300000) && (addr <= 0x30ffff)) return rivatnt->bios_rom.rom[addr & rivatnt->bios_rom.mask];

	if ((addr >= 0x1800) && (addr <= 0x18ff))
	return rivatnt_pci_read(0,addr & 0xff,p);

	switch(addr) {
	case 0x6013b4: case 0x6013b5:
	case 0x6013d4: case 0x6013d5:
	case 0x6013da:
	case 0x0c03c2: case 0x0c03c3: case 0x0c03c4: case 0x0c03c5: case 0x0c03cc:
	case 0x6813c6: case 0x6813c7: case 0x6813c8: case 0x6813c9: case 0x6813ca: case 0x6813cb:
		return rivatnt_in(addr & 0x3ff,p);
		break;
	}

	return (rivatnt_mmio_read_l(addr & 0xffffff, rivatnt) >> ((addr & 3) << 3)) & 0xff;
}


uint16_t
rivatnt_mmio_read_w(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	addr &= 0xffffff;

	if ((addr >= 0x300000) && (addr <= 0x30ffff)) return ((uint16_t *) rivatnt->bios_rom.rom)[(addr & rivatnt->bios_rom.mask) >> 1];

	if ((addr >= 0x1800) && (addr <= 0x18ff))
	return (rivatnt_pci_read(0,(addr+0) & 0xff,p) << 0) | (rivatnt_pci_read(0,(addr+1) & 0xff,p) << 8);

	switch(addr) {
	case 0x6013b4: case 0x6013b5:
	case 0x6013d4: case 0x6013d5:
	case 0x6013da:
	case 0x0c03c2: case 0x0c03c3: case 0x0c03c4: case 0x0c03c5: case 0x0c03cc:
	case 0x6813c6: case 0x6813c7: case 0x6813c8: case 0x6813c9: case 0x6813ca: case 0x6813cb:
		return (rivatnt_in((addr+0) & 0x3ff,p) << 0) | (rivatnt_in((addr+1) & 0x3ff,p) << 8);
		break;
	}

   return (rivatnt_mmio_read_l(addr & 0xffffff, rivatnt) >> ((addr & 3) << 3)) & 0xffff;
}


void
rivatnt_mmio_write_l(uint32_t addr, uint32_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	addr &= 0xffffff;

	pclog("[RIVA TNT] MMIO write %08x %08x\n", addr, val);

	if ((addr >= 0x1800) && (addr <= 0x18ff)) {
	rivatnt_pci_write(0, addr & 0xff, val & 0xff, p);
	rivatnt_pci_write(0, (addr+1) & 0xff, (val>>8) & 0xff, p);
	rivatnt_pci_write(0, (addr+2) & 0xff, (val>>16) & 0xff, p);
	rivatnt_pci_write(0, (addr+3) & 0xff, (val>>24) & 0xff, p);
	return;
	}

	if((addr >= 0x000000) && (addr <= 0x000fff)) rivatnt_pmc_write(addr, val, rivatnt);
	if((addr >= 0x002000) && (addr <= 0x003fff)) rivatnt_pfifo_write(addr, val, rivatnt);
	if((addr >= 0x009000) && (addr <= 0x009fff)) rivatnt_ptimer_write(addr, val, rivatnt);
	if((addr >= 0x600000) && (addr <= 0x600fff)) rivatnt_pcrtc_write(addr, val, rivatnt);
	if((addr >= 0x680000) && (addr <= 0x680fff)) rivatnt_pramdac_write(addr, val, rivatnt);
	if((addr >= 0x700000) && (addr <= 0x7fffff)) rivatnt->ramin[(addr & 0xfffff) >> 2] = val;

	switch(addr) {
	case 0x6013b4: case 0x6013b5:
	case 0x6013d4: case 0x6013d5:
	case 0x6013da:
	case 0x0c03c2: case 0x0c03c3: case 0x0c03c4: case 0x0c03c5: case 0x0c03cc:
	case 0x6813c6: case 0x6813c7: case 0x6813c8: case 0x6813c9: case 0x6813ca: case 0x6813cb:
		rivatnt_out(addr & 0xfff, val & 0xff, p);
		rivatnt_out((addr+1) & 0xfff, (val>>8) & 0xff, p);
		rivatnt_out((addr+2) & 0xfff, (val>>16) & 0xff, p);
		rivatnt_out((addr+3) & 0xfff, (val>>24) & 0xff, p);
		break;
	}
}


void
rivatnt_mmio_write(uint32_t addr, uint8_t val, void *p)
{
	uint32_t tmp;

	addr &= 0xffffff;

	switch(addr) {
	case 0x6013b4: case 0x6013b5:
	case 0x6013d4: case 0x6013d5:
	case 0x6013da:
	case 0x0c03c2: case 0x0c03c3: case 0x0c03c4: case 0x0c03c5: case 0x0c03cc:
	case 0x6813c6: case 0x6813c7: case 0x6813c8: case 0x6813c9: case 0x6813ca: case 0x6813cb:
		rivatnt_out(addr & 0xfff, val & 0xff, p);
		return;
	}

	tmp = rivatnt_mmio_read_l(addr,p);
	tmp &= ~(0xff << ((addr & 3) << 3));
	tmp |= val << ((addr & 3) << 3);
	rivatnt_mmio_write_l(addr, tmp, p);
	if ((addr >= 0x1800) && (addr <= 0x18ff)) rivatnt_pci_write(0, addr & 0xff, val, p);
}


void
rivatnt_mmio_write_w(uint32_t addr, uint16_t val, void *p)
{
	uint32_t tmp;

	if ((addr >= 0x1800) && (addr <= 0x18ff)) {
	rivatnt_pci_write(0, addr & 0xff, val & 0xff, p);
	rivatnt_pci_write(0, (addr+1) & 0xff, (val>>8) & 0xff, p);
	return;
	}

	addr &= 0xffffff;
	tmp = rivatnt_mmio_read_l(addr,p);
	tmp &= ~(0xffff << ((addr & 3) << 3));
	tmp |= val << ((addr & 3) << 3);
	rivatnt_mmio_write_l(addr, tmp, p);
}

uint8_t
rivatnt_rma_in(uint16_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	svga_t *svga = &rivatnt->svga;
	uint8_t ret = 0;

	addr &= 0xff;

	// nv_riva_log("RIVA TNT RMA read %04X %04X:%08X\n", addr, CS, cpu_state.pc);

	switch(addr) {
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
		if (rivatnt->rma.rma_dst_addr < 0x1000000)
			ret = rivatnt_mmio_read((rivatnt->rma.rma_dst_addr + (addr & 3)) & 0xffffff, rivatnt);
		else
			ret = svga_read_linear((rivatnt->rma.rma_dst_addr - 0x1000000) & 0xffffff, svga);
		break;
	}

	return ret;
}


void
rivatnt_rma_out(uint16_t addr, uint8_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	svga_t* svga = &rivatnt->svga;

	addr &= 0xff;

	// nv_riva_log("RIVA TNT RMA write %04X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);

	switch(addr) {
	case 0x04:
		rivatnt->rma.rma_dst_addr &= ~0xff;
		rivatnt->rma.rma_dst_addr |= val;
		break;
	case 0x05:
		rivatnt->rma.rma_dst_addr &= ~0xff00;
		rivatnt->rma.rma_dst_addr |= (val << 8);
		break;
	case 0x06:
		rivatnt->rma.rma_dst_addr &= ~0xff0000;
		rivatnt->rma.rma_dst_addr |= (val << 16);
		break;
	case 0x07:
		rivatnt->rma.rma_dst_addr &= ~0xff000000;
		rivatnt->rma.rma_dst_addr |= (val << 24);
		break;
	case 0x08:
	case 0x0c:
	case 0x10:
	case 0x14:
		rivatnt->rma.rma_data &= ~0xff;
		rivatnt->rma.rma_data |= val;
		break;
	case 0x09:
	case 0x0d:
	case 0x11:
	case 0x15:
		rivatnt->rma.rma_data &= ~0xff00;
		rivatnt->rma.rma_data |= (val << 8);
		break;
	case 0x0a:
	case 0x0e:
	case 0x12:
	case 0x16:
		rivatnt->rma.rma_data &= ~0xff0000;
		rivatnt->rma.rma_data |= (val << 16);
		break;
	case 0x0b:
	case 0x0f:
	case 0x13:
	case 0x17:
		rivatnt->rma.rma_data &= ~0xff000000;
		rivatnt->rma.rma_data |= (val << 24);
		if (rivatnt->rma.rma_dst_addr < 0x1000000)
			rivatnt_mmio_write_l(rivatnt->rma.rma_dst_addr & 0xffffff, rivatnt->rma.rma_data, rivatnt);
		else
			svga_writel_linear((rivatnt->rma.rma_dst_addr - 0x1000000) & 0xffffff, rivatnt->rma.rma_data, svga);
		break;
	}

	if (addr & 0x10)
	rivatnt->rma.rma_dst_addr+=4;
}


static void
rivatnt_out(uint16_t addr, uint8_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	svga_t *svga = &rivatnt->svga;
	uint8_t old;

	if ((addr >= 0x3d0) && (addr <= 0x3d3)) {
	rivatnt->rma.rma_access_reg[addr & 3] = val;
	if(!(rivatnt->rma.rma_mode & 1))
		return;
	rivatnt_rma_out(((rivatnt->rma.rma_mode & 0xe) << 1) + (addr & 3), rivatnt->rma.rma_access_reg[addr & 3], rivatnt);
	}

	if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
	addr ^= 0x60;

	switch (addr) {
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
		if(svga->seqregs[0x06] == 0x57)
		{
			switch(svga->crtcreg) {
				case 0x1e:
					rivatnt->read_bank = val;
					if (svga->chain4) svga->read_bank = rivatnt->read_bank << 15;
					else              svga->read_bank = rivatnt->read_bank << 13;
					break;
				case 0x1d:
					rivatnt->write_bank = val;
					if (svga->chain4) svga->write_bank = rivatnt->write_bank << 15;
					else              svga->write_bank = rivatnt->write_bank << 13;
					break;
				case 0x19: case 0x1a: case 0x25: case 0x28:
				case 0x2d:
					svga_recalctimings(svga);
					break;
				case 0x38:
					rivatnt->rma.rma_mode = val & 0xf;
					break;
				case 0x3f:
					i2c_gpio_set(rivatnt->i2c, !!(val & 0x20), !!(val & 0x10));
					break;
			}
		}
		//if (svga->crtcreg > 0x18)
			// pclog("RIVA TNT Extended CRTC write %02X %02x\n", svga->crtcreg, val);
		if (old != val) {
			if ((svga->crtcreg < 0xe) || (svga->crtcreg > 0x10)) {
				svga->fullchange = changeframecount;
				svga_recalctimings(svga);
			}
		}
		break;
	}

	svga_out(addr, val, svga);
}


static uint8_t
rivatnt_in(uint16_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	svga_t *svga = &rivatnt->svga;
	uint8_t temp;

	if ((addr >= 0x3d0) && (addr <= 0x3d3)) {
	if (!(rivatnt->rma.rma_mode & 1))
		return 0x00;
	return rivatnt_rma_in(((rivatnt->rma.rma_mode & 0xe) << 1) + (addr & 3), rivatnt);
	}

	if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) addr ^= 0x60;

	switch (addr) {
	case 0x3D4:
		temp = svga->crtcreg;
		break;
	case 0x3D5:
		switch(svga->crtcreg) {
			case 0x3e:
					/* DDC status register */
				temp = (i2c_gpio_get_sda(rivatnt->i2c) << 3) | (i2c_gpio_get_scl(rivatnt->i2c) << 2);
				break;
			default:
				temp = svga->crtc[svga->crtcreg];
				break;
		}
		break;
	default:
		temp = svga_in(addr, svga);
		break;
	}

	return temp;
}

static void
rivatnt_recalctimings(svga_t *svga)
{
	rivatnt_t *rivatnt = (rivatnt_t *)svga->p;

	svga->ma_latch += (svga->crtc[0x19] & 0x1f) << 16;
	svga->rowoffset += (svga->crtc[0x19] & 0xe0) << 3;
	if (svga->crtc[0x25] & 0x01) svga->vtotal      += 0x400;
	if (svga->crtc[0x25] & 0x02) svga->dispend     += 0x400;
	if (svga->crtc[0x25] & 0x04) svga->vblankstart += 0x400;
	if (svga->crtc[0x25] & 0x08) svga->vsyncstart  += 0x400;
	if (svga->crtc[0x25] & 0x10) svga->htotal      += 0x100;
	if (svga->crtc[0x2d] & 0x01) svga->hdisp       += 0x100;	
	/* The effects of the large screen bit seem to just be doubling the row offset.
	   However, these large modes still don't work. Possibly core SVGA bug? It does report 640x2 res after all. */

	switch(svga->crtc[0x28] & 3) {
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

	double freq = 13500000.0;
	int m_m = rivatnt->pramdac.mpll & 0xff;
	int m_n = (rivatnt->pramdac.mpll >> 8) & 0xff;
	int m_p = (rivatnt->pramdac.mpll >> 16) & 7;

	if(m_n == 0) m_n = 1;
	if(m_m == 0) m_m = 1;

	freq = (freq * m_n) / (m_m << m_p);
	rivatnt->mtime = 10000000.0 / freq; //Multiply period by 10 to work around timer system limitations.
	timer_on_auto(&rivatnt->mtimer, rivatnt->mtime);

	freq = 13500000;
	int nv_m = rivatnt->pramdac.nvpll & 0xff;
	int nv_n = (rivatnt->pramdac.nvpll >> 8) & 0xff;
	int nv_p = (rivatnt->pramdac.nvpll >> 16) & 7;

	if(nv_n == 0) nv_n = 1;
	if(nv_m == 0) nv_m = 1;

	freq = (freq * nv_n) / (nv_m << nv_p);
	rivatnt->nvtime = 10000000.0 / freq; //Multiply period by 10 to work around timer system limitations.
	timer_on_auto(&rivatnt->nvtimer, rivatnt->nvtime);

	freq = 13500000;
	int v_m = rivatnt->pramdac.vpll & 0xff;
	int v_n = (rivatnt->pramdac.vpll >> 8) & 0xff;
	int v_p = (rivatnt->pramdac.vpll >> 16) & 7;

	if(v_n == 0) v_n = 1;
	if(v_m == 0) v_m = 1;

	freq = (freq * v_n) / (v_m << v_p);
	svga->clock = (cpuclock * (double)(1ull << 32)) / freq;
}

void
rivatnt_vblank_start(svga_t *svga)
{
	rivatnt_t *rivatnt = (rivatnt_t *)svga->p;

	rivatnt->pcrtc.intr |= 1;

	rivatnt_pmc_recompute_intr(rivatnt);
}

static void
*rivatnt_init(const device_t *info)
{
	rivatnt_t *rivatnt = malloc(sizeof(rivatnt_t));
	svga_t *svga;
	char *romfn = BIOS_RIVATNT_PATH;
	memset(rivatnt, 0, sizeof(rivatnt_t));
	svga = &rivatnt->svga;

	rivatnt->vram_size = device_get_config_int("memory") << 20;
	rivatnt->vram_mask = rivatnt->vram_size - 1;

	svga_init(info, &rivatnt->svga, rivatnt, rivatnt->vram_size,
		  rivatnt_recalctimings, rivatnt_in, rivatnt_out,
		  NULL, NULL);

	svga->decode_mask = rivatnt->vram_mask;

	rom_init(&rivatnt->bios_rom, romfn, 0xc0000, 0x10000, 0xffff, 0, MEM_MAPPING_EXTERNAL);
	mem_mapping_disable(&rivatnt->bios_rom.mapping);

	mem_mapping_add(&rivatnt->mmio_mapping, 0, 0, rivatnt_mmio_read, rivatnt_mmio_read_w, rivatnt_mmio_read_l, rivatnt_mmio_write, rivatnt_mmio_write_w, rivatnt_mmio_write_l,  NULL, MEM_MAPPING_EXTERNAL, rivatnt);
	mem_mapping_disable(&rivatnt->mmio_mapping);
	mem_mapping_add(&rivatnt->linear_mapping, 0, 0, svga_read_linear, svga_readw_linear, svga_readl_linear, svga_write_linear, svga_writew_linear, svga_writel_linear,  NULL, MEM_MAPPING_EXTERNAL, &rivatnt->svga);
	mem_mapping_disable(&rivatnt->linear_mapping);

	svga->vblank_start = rivatnt_vblank_start;

	rivatnt->card = pci_add_card(PCI_ADD_VIDEO, rivatnt_pci_read, rivatnt_pci_write, rivatnt);

	rivatnt->pci_regs[0x04] = 0x07;
	rivatnt->pci_regs[0x05] = 0x00;
	rivatnt->pci_regs[0x07] = 0x02;

	rivatnt->pci_regs[0x30] = 0x00;
	rivatnt->pci_regs[0x32] = 0x0c;
	rivatnt->pci_regs[0x33] = 0x00;

	rivatnt->pmc.intr_en = 1;

	//Default values for the RAMDAC PLLs
	rivatnt->pramdac.mpll = 0x03c20d;
	rivatnt->pramdac.nvpll = 0x03c20d;
	rivatnt->pramdac.vpll = 0x03c20d;

	timer_add(&rivatnt->nvtimer, rivatnt_nvclk_poll, rivatnt, 0);
	timer_add(&rivatnt->mtimer, rivatnt_mclk_poll, rivatnt, 0);

	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_rivatnt);

	rivatnt->i2c = i2c_gpio_init("ddc_rivatnt");
	rivatnt->ddc = ddc_init(i2c_gpio_get_bus(rivatnt->i2c));

	return rivatnt;
}


static int
rivatnt_available(void)
{
	return rom_present(BIOS_RIVATNT_PATH);
}


void
rivatnt_close(void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	
	svga_close(&rivatnt->svga);

	free(rivatnt->ramin);
	
	free(rivatnt);
}


void
rivatnt_speed_changed(void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	
	svga_recalctimings(&rivatnt->svga);
}


void
rivatnt_force_redraw(void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;

	rivatnt->svga.fullchange = changeframecount;
}


static const device_config_t rivatnt_config[] =
{
		{
				.name = "memory",
				.description = "Memory size",
				.type = CONFIG_SELECTION,
				.selection =
				{
						{
								.description = "4 MB",
								.value = 4
						},
						{
								.description = "8 MB",
								.value = 8
						},
                        {
								.description = "16 MB",
								.value = 16
						},
						{
								.description = ""
						}
				},
				.default_int = 16
		},
		{
				.type = -1
		}
};

const device_t rivatnt_pci_device =
{
	"nVidia RIVA TNT (PCI)",
	DEVICE_PCI,
	RIVATNT_DEVICE_ID,
	rivatnt_init,
	rivatnt_close, 
	NULL,
	{ rivatnt_available },
	rivatnt_speed_changed,
	rivatnt_force_redraw,
	rivatnt_config
};