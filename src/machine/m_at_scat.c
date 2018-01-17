/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Chips&Technology's SCAT (82C235) chipset.
 *
 *		Re-worked version based on the 82C235 datasheet and errata.
 *
 * Version:	@(#)m_at_scat.c	1.0.8	2018/01/16
 *
 * Authors:	Original by GreatPsycho for PCem.
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../cpu/cpu.h"
#include "../cpu/x86.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../io.h"
#include "../mem.h"
#include "machine.h"


#define SCAT_DEBUG		1

#define SCAT_DMA_WS_CTL		0x01
#define SCAT_VERSION		0x40
#define SCAT_CLOCK_CTL		0x41
#define SCAT_PERIPH_CTL		0x44
#define SCAT_MISC_STATUS	0x45
#define SCAT_POWER_MGMT		0x46
#define SCAT_ROM_ENABLE		0x48
#define SCAT_RAM_WR_PROTECT	0x49
#define SCAT_SHADOW_RAM_EN_1	0x4a
#define SCAT_SHADOW_RAM_EN_2	0x4b
#define SCAT_SHADOW_RAM_EN_3	0x4c
#define SCAT_DRAM_CONFIG	0x4d
#define SCAT_EXT_BOUNDARY	0x4e
#define SCAT_EMS_CTL		0x4f
#define SCAT_SYS_CTL		0x7f		/* port 92 */


typedef struct {
    uint8_t regs_2x8;
    uint8_t regs_2x9;
} ems_t;


static uint8_t		scat_regs[128];
static int		scat_index;
static uint32_t		scat_xms_bound;
static uint8_t		scat_ems_reg = 0;
static ems_t		scat_ems[32];			/* EMS page regs */
static mem_mapping_t	scat_mapping[32];		/* EMS pages */
static mem_mapping_t	scat_top_mapping[24];		/* top 384K mapping */
static mem_mapping_t	scat_A000_mapping;		/* A000-C000 mapping */
static mem_mapping_t	scat_shadowram_mapping[6];	/* BIOS shadowing */
static mem_mapping_t	scat_high_mapping[16];		/* >1M mapping */


static void
shadow_state_update(void)
{
    int i, val;

    /* TODO - ROMCS enable features should be implemented later. */
    for (i=0; i<24; i++) {
	val = ((scat_regs[SCAT_SHADOW_RAM_EN_1 + (i >> 3)] >> (i & 7)) & 1) ? MEM_READ_INTERNAL : MEM_READ_EXTERNAL;
	if (i < 8) {
		val |= ((scat_regs[SCAT_SHADOW_RAM_EN_1 + (i >> 3)] >> (i & 7)) & 1) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTERNAL;
	} else {
		if ((scat_regs[SCAT_RAM_WR_PROTECT] >> ((i - 8) >> 1)) & 1)
			val |= MEM_WRITE_DISABLED;
		  else
			val |= ((scat_regs[SCAT_SHADOW_RAM_EN_1 + (i >> 3)] >> (i & 7)) & 1) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTERNAL;
	}
	mem_set_mem_state((i + 40) << 14, 0x4000, val);
    }

    flushmmucache();
}


static void
set_xms_bound(uint8_t val)
{
    uint32_t max_xms, max_mem;
    int i;

    max_mem = (mem_size << 10);
    max_xms = (mem_size >= 16384) ? 0xFC0000 : max_mem;
    pclog("SCAT: set_xms_bound(%02x): max_mem=%d, max_xms=%d\n",
					val, max_mem, max_xms);

    switch (val & 0x0f) {
	case 1:
		scat_xms_bound = 0x100000;
		break;

	case 2:
		scat_xms_bound = 0x140000;
		break;

	case 3:
		scat_xms_bound = 0x180000;
		break;

	case 4:
		scat_xms_bound = 0x200000;
		break;

	case 5:
		scat_xms_bound = 0x300000;
		break;

	case 6:
		scat_xms_bound = 0x400000;
		break;

	case 7:
		scat_xms_bound = 0x600000;
		break;

	case 8:
		scat_xms_bound = 0x800000;
		break;

	case 9:
		scat_xms_bound = 0xA00000;
		break;

	case 10:
		scat_xms_bound = 0xC00000;
		break;

	case 11:
		scat_xms_bound = 0xE00000;
		break;

	default:
		scat_xms_bound = max_xms;
		break;
   }

   if ((val & 0x40) == 0 && (scat_regs[SCAT_DRAM_CONFIG] & 0x0f) == 3) {
	if (val != 1) {
		if (mem_size > 1024)
			mem_mapping_disable(&ram_high_mapping);
		for (i=0; i<6; i++)
			mem_mapping_enable(&scat_shadowram_mapping[i]);
		if ((val & 0x0f) == 0)
			scat_xms_bound = 0x160000;
	} else {
		for (i=0; i<6; i++)
			mem_mapping_disable(&scat_shadowram_mapping[i]);
		if (mem_size > 1024)
			mem_mapping_enable(&ram_high_mapping);
	}
	pclog("SCAT: set XMS bound(%02X) = %06X (%dK for EMS)\n",
		val, scat_xms_bound, (0x160000-scat_xms_bound)>>10);

	if (scat_xms_bound > 0x100000)
		mem_set_mem_state(0x100000,
				  scat_xms_bound - 0x100000,
				  MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	if (scat_xms_bound < 0x160000)
		mem_set_mem_state(scat_xms_bound,
				  0x160000 - scat_xms_bound,
				  MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
    } else {
	for (i=0; i<6; i++)
		mem_mapping_disable(&scat_shadowram_mapping[i]);
	if (mem_size > 1024)
		mem_mapping_enable(&ram_high_mapping);

	if (scat_xms_bound > max_xms)
		scat_xms_bound = max_xms;
	pclog("SCAT: set XMS bound(%02X) = %06X (%dK for EMS)\n",
		val, scat_xms_bound, (max_mem-scat_xms_bound)>>10);

	if (scat_xms_bound > 0x100000)
		mem_set_mem_state(0x100000,
				  scat_xms_bound-0x100000,
				  MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	if (scat_xms_bound < max_mem)
		mem_set_mem_state(scat_xms_bound,
				  (mem_size<<10)-scat_xms_bound,
				  MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
    }
}


static uint32_t
get_addr(uint32_t addr, ems_t *p)
{
    if (p && (scat_regs[SCAT_EMS_CTL] & 0x80) && (p->regs_2x9 & 0x80)) {
	addr = (addr & 0x3fff) | (((p->regs_2x9 & 3)<<8) | p->regs_2x8)<<14;
    }

    if (mem_size < 2048 &&
	((scat_regs[SCAT_DRAM_CONFIG] & 0x0F) > 7 ||
	 (scat_regs[SCAT_EXT_BOUNDARY] & 0x40) != 0))
	addr = (addr & ~0x780000) | ((addr & 0x600000) >> 2);
      else
    if ((scat_regs[SCAT_DRAM_CONFIG] & 0x0F) < 8 &&
	(scat_regs[SCAT_EXT_BOUNDARY] & 0x40) == 0) {
	addr &= ~0x600000;
	if (mem_size > 2048 || (mem_size == 2048 &&
	    (scat_regs[SCAT_DRAM_CONFIG] & 0x0F) < 6))
		addr |= (addr & 0x180000) << 2;
    }

    if ((scat_regs[SCAT_EXT_BOUNDARY] & 0x40) == 0 &&
	(scat_regs[SCAT_DRAM_CONFIG] & 0x0F) == 3 &&
	(addr & ~0x600000) >= 0x100000 && (addr & ~0x600000) < 0x160000)
	addr ^= mem_size < 2048 ? 0x1F0000 : 0x670000;

    return(addr);
}


static void
memmap_state_update(void)
{
    uint32_t addr;
    int i;

    for (i=16; i<24; i++) {
	addr = get_addr(0x40000 + (i<<14), NULL);
	mem_mapping_set_exec(&scat_top_mapping[i],
			     addr < (mem_size<<10) ? ram+addr : NULL);
    }

    addr = get_addr(0xA0000, NULL);
    mem_mapping_set_exec(&scat_A000_mapping,
			 addr < (mem_size<<10) ? ram+addr : NULL);

    for (i=0; i<6; i++) {
	addr = get_addr(0x100000 + (i<<16), NULL);
	mem_mapping_set_exec(&scat_shadowram_mapping[i],
			     addr < (mem_size<<10) ? ram+addr : NULL);
    }

    flushmmucache();
}


static void
ems_state(int state)
{
    uint32_t base_addr, virt_addr;
    int i;

    for (i=0; i<32; i++) {
	base_addr = (i + 16) << 14;

	if (i >= 24)
		base_addr += 0x30000;

	if (state && (scat_ems[i].regs_2x9 & 0x80)) {
		virt_addr = get_addr(base_addr, &scat_ems[i]);
		if (i < 24)
			mem_mapping_disable(&scat_top_mapping[i]);
		mem_mapping_enable(&scat_mapping[i]);
		if (virt_addr < (mem_size<<10))
			mem_mapping_set_exec(&scat_mapping[i], ram+virt_addr);
		  else
			mem_mapping_set_exec(&scat_mapping[i], NULL);
	} else {
		mem_mapping_set_exec(&scat_mapping[i], ram+base_addr);
		mem_mapping_disable(&scat_mapping[i]);
		if (i < 24)
			mem_mapping_enable(&scat_top_mapping[i]);
	}
    }
}


/* Read a byte from a LIM/EMS page. */
static uint8_t
ems_pgrd(uint32_t vaddr, void *priv)
{
    ems_t *ems = (ems_t *)priv;
    uint32_t addr;
    uint8_t val = 0xff;

    addr = get_addr(vaddr, ems);
    if (addr < (mem_size << 10))
	val = mem_read_ram(addr, priv);
#if SCAT_DEBUG > 1
    pclog("SCAT: ems_pgrd(%06x->%06x) = %02x\n", vaddr, addr, val);
#endif

    return(val);
}


/* Write a byte to a LIM/EMS page. */
static void
ems_pgwr(uint32_t vaddr, uint8_t val, void *priv)
{
    ems_t *ems = (ems_t *)priv;
    uint32_t addr;

    addr = get_addr(vaddr, ems);
#if SCAT_DEBUG > 1
    pclog("SCAT: ems_pgwr(%06x->%06x, %02x)\n", vaddr, addr, val);
#endif
    if (addr < (mem_size << 10))
	mem_write_ram(addr, val, priv);
}


/* Read from a LIM/EMS control register. */
static uint8_t
ems_read(uint16_t port, void *priv)
{
    uint8_t val = 0xff;
    uint8_t idx;

    switch (port) {
	case 0x208:
	case 0x218:
		if ((scat_regs[SCAT_EMS_CTL] & 0x41) == (0x40 | ((port & 0x10) >> 4))) {
			idx = scat_ems_reg & 0x1f;
			val = scat_ems[idx].regs_2x8;
		}
		break;

	case 0x209:
	case 0x219:
		if ((scat_regs[SCAT_EMS_CTL] & 0x41) == (0x40 | ((port & 0x10) >> 4))) {
			idx = scat_ems_reg & 0x1f;
			val = scat_ems[idx].regs_2x9;
		}
		break;

	case 0x20A:
	case 0x21A:
		if ((scat_regs[SCAT_EMS_CTL] & 0x41) == (0x40 | ((port & 0x10) >> 4))) {
			val = scat_ems_reg;
		}
		break;
    }

#if SCAT_DEBUG > 2
    pclog("SCAT: ems_read(%04x) = %02x\n", port, val);
#endif
    return(val);
}


/* Write to one of the LIM/EMS control registers. */
static void
ems_write(uint16_t port, uint8_t val, void *priv)
{
    uint32_t base_addr, virt_addr;
    uint8_t idx;
 
#if SCAT_DEBUG > 1
    pclog("SCAT: ems_write(%04x, %02x)\n", port, val);
#endif
    switch(port) {
	case 0x208:
	case 0x218:
		if ((scat_regs[SCAT_EMS_CTL] & 0x41) == (0x40 | ((port & 0x10) >> 4))) {
			idx = scat_ems_reg & 0x1f;
			scat_ems[idx].regs_2x8 = val;
			base_addr = (idx + 16) << 14;
			if (idx >= 24)
				base_addr += 0x30000;

			if ((scat_regs[SCAT_EMS_CTL] & 0x80) && (scat_ems[idx].regs_2x9 & 0x80)) {
				virt_addr = get_addr(base_addr, &scat_ems[idx]);
				if (virt_addr < (mem_size << 10))
					mem_mapping_set_exec(&scat_mapping[idx], ram + virt_addr);
				  else
					mem_mapping_set_exec(&scat_mapping[idx], NULL);
				flushmmucache();
			}
		}
		break;

	case 0x209:
	case 0x219:
		if ((scat_regs[SCAT_EMS_CTL] & 0x41) == (0x40 | ((port & 0x10) >> 4))) {
			idx = scat_ems_reg & 0x1f;
			scat_ems[idx].regs_2x9 = val;
			base_addr = (idx + 16) << 14;
			if (idx >= 24)
				base_addr += 0x30000;

			if (scat_regs[SCAT_EMS_CTL] & 0x80) {
				if (val & 0x80) {
					virt_addr = get_addr(base_addr, &scat_ems[idx]);
					if (idx < 24)
						mem_mapping_disable(&scat_top_mapping[idx]);
					if (virt_addr < (mem_size << 10))
						mem_mapping_set_exec(&scat_mapping[idx], ram+virt_addr);
					  else
						mem_mapping_set_exec(&scat_mapping[idx], NULL);
					mem_mapping_enable(&scat_mapping[idx]);
				} else {
					mem_mapping_set_exec(&scat_mapping[idx], ram + base_addr);
					mem_mapping_disable(&scat_mapping[idx]);
					if (idx < 24)
						mem_mapping_enable(&scat_top_mapping[idx]);
				}
				flushmmucache();
			}

			if (scat_ems_reg & 0x80) {
				scat_ems_reg = (scat_ems_reg & 0xe0) | ((scat_ems_reg + 1) & 0x1f);
			}
		}
		break;

	case 0x20A:
	case 0x21A:
		if ((scat_regs[SCAT_EMS_CTL] & 0x41) == (0x40 | ((port & 0x10) >> 4))) {
			scat_ems_reg = val;
		}
		break;
    }
}


static uint8_t
scat_read(uint16_t port, void *priv)
{
    uint8_t val = 0xff;

    switch (port) {
	case 0x23:
		switch (scat_index) {
			case SCAT_MISC_STATUS:
				val = (scat_regs[scat_index] & 0xbf) | ((mem_a20_key & 2) << 5);
				break;

			case SCAT_DRAM_CONFIG:
				val = (scat_regs[scat_index] & 0x8f) | (cpu_waitstates == 1 ? 0 : 0x10);
				break;

			case SCAT_SYS_CTL:
				val = port_92_read(0x0092, priv);
				break;

			default:
				val = scat_regs[scat_index];
				break;
		}
		break;

	case 0x92:
		val = scat_regs[SCAT_SYS_CTL];
		break;

    }

#if SCAT_DEBUG > 2
    pclog("SCAT: read(%04x) = %02x\n", port, val);
#endif
    return(val);
}


/* Write to one of the Internal Control Registers. */
static void
ics_write(uint8_t idx, uint8_t val)
{
    uint8_t reg_valid = 0;
    uint8_t shadow_update = 0;
    uint8_t map_update = 0;

#if SCAT_DEBUG > 1
    pclog("SCAT: icr_write(%02x, %02x)\n", idx, val);
#endif
    switch (idx) {
	case SCAT_CLOCK_CTL:
	case SCAT_PERIPH_CTL:
		reg_valid = 1;
		break;

	case SCAT_EMS_CTL:
		if (val & 0x40) {
			if (val & 1) {
				io_sethandler(0x0218, 3,
					      ems_read, NULL, NULL,
					      ems_write, NULL, NULL, NULL);
				io_removehandler(0x0208, 3,
						 ems_read, NULL, NULL,
						 ems_write, NULL, NULL, NULL);
			} else {
				io_sethandler(0x0208, 3,
					      ems_read, NULL, NULL,
					      ems_write, NULL, NULL, NULL);
				io_removehandler(0x0218, 3,
						 ems_read, NULL, NULL,
						 ems_write, NULL, NULL, NULL);
			}
		} else {
			io_removehandler(0x0208, 3,
					 ems_read, NULL, NULL,
					 ems_write, NULL, NULL, NULL);
			io_removehandler(0x0218, 3,
					 ems_read, NULL, NULL,
					 ems_write, NULL, NULL, NULL);
		}
		ems_state(val & 0x80);
		reg_valid = 1;
		break;

	case SCAT_POWER_MGMT:
		val &= 0x40;
		reg_valid = 1;
		break;

	case SCAT_DRAM_CONFIG:
		if ((scat_regs[SCAT_EXT_BOUNDARY] & 0x40) == 0) {
			if ((val & 0x0f) == 3) {
				if (mem_size > 1024)
					mem_mapping_disable(&ram_high_mapping);
				for (idx=0; idx<6; idx++)
					mem_mapping_enable(&scat_shadowram_mapping[idx]);
			} else {
				for (idx=0; idx<6; idx++)
					mem_mapping_disable(&scat_shadowram_mapping[idx]);
				if (mem_size > 1024)
					mem_mapping_enable(&ram_high_mapping);
			}
		} else {
			for (idx=0; idx<6; idx++)
				mem_mapping_disable(&scat_shadowram_mapping[idx]);
			if (mem_size > 1024)
				mem_mapping_enable(&ram_high_mapping);
		}
		map_update = 1;

		cpu_waitstates = (val & 0x70) == 0 ? 1 : 2;
		cpu_update_waitstates();

		reg_valid = 1;
		break;

	case SCAT_EXT_BOUNDARY:
		set_xms_bound(val & 0x4f);
		mem_set_mem_state(0x40000, 0x60000,
				  (val & 0x20) ? MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL : MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		if ((val ^ scat_regs[SCAT_EXT_BOUNDARY]) & 0x40)
						map_update = 1;
		reg_valid = 1;
		break;

	case SCAT_ROM_ENABLE:
	case SCAT_RAM_WR_PROTECT:
	case SCAT_SHADOW_RAM_EN_1:
	case SCAT_SHADOW_RAM_EN_2:
	case SCAT_SHADOW_RAM_EN_3:
		reg_valid = 1;
		shadow_update = 1;
		break;

	case SCAT_SYS_CTL:
		port_92_write(0x0092, val, NULL);
		break;

	default:
		break;
    }

    if (reg_valid)
	scat_regs[scat_index] = val;
#ifndef RELEASE_BUILD
      else
	pclog("SCAT: attemped write to register %02X at %04X:%04X\n",
					idx, val, CS, cpu_state.pc);
#endif

    if (shadow_update)
	shadow_state_update();

    if (map_update)
	memmap_state_update();
}


static void
scat_write(uint16_t port, uint8_t val, void *priv)
{
#if SCAT_DEBUG > 2
    pclog("SCAT: write(%04x, %02x)\n", port, val);
#endif
    switch (port) {
	case 0x22:
		scat_index = val;
		break;

	case 0x23:
		ics_write(scat_index, val);
		break;
    }
}


static void
scat_init(void)
{
    int i;

#if SCAT_DEBUG
    pclog("SCAT: initializing..\n");
#endif
    io_sethandler(0x0022, 2,
		  scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);

    port_92_reset();

    port_92_add();

    for (i=0; i<128; i++)
	scat_regs[i] = 0xff;
    scat_regs[SCAT_DMA_WS_CTL] = 0x00;
    scat_regs[SCAT_VERSION] = 0x0a;
    scat_regs[SCAT_CLOCK_CTL] = 0x02;
    scat_regs[SCAT_PERIPH_CTL] = 0x80;
    scat_regs[SCAT_MISC_STATUS] = 0x37;
    scat_regs[SCAT_POWER_MGMT] = 0x00;
    scat_regs[SCAT_ROM_ENABLE] = 0xC0;
    scat_regs[SCAT_RAM_WR_PROTECT] = 0x00;
    scat_regs[SCAT_SHADOW_RAM_EN_1] = 0x00;
    scat_regs[SCAT_SHADOW_RAM_EN_2] = 0x00;
    scat_regs[SCAT_SHADOW_RAM_EN_3] = 0x00;
    scat_regs[SCAT_DRAM_CONFIG] = cpu_waitstates == 1 ? 0x00 : 0x10;
    scat_regs[SCAT_EXT_BOUNDARY] = 0x00;
    scat_regs[SCAT_EMS_CTL] = 0x00;
    scat_regs[SCAT_SYS_CTL] = 0x00;

    /* Limit RAM size to 16MB. */
    mem_mapping_set_addr(&ram_low_mapping, 0x000000, 0x40000);

    /*
     * Configure the DRAM controller.
     *
     * Since SCAT allows mixing of the various-sized DRAM chips,
     * memory configuration is not simple.  The SCAT datasheet
     * tells us in table 6-6 how to set up the four banks:
     *
     * MEM SIZE	  MIX_EN   RAMCFG  Ext RAM  Comments
     *   0   K	    0	    0x00	    No RAM installed
     *   512 K      0       0x01    0  K
     *   640 K      0       0x02    0  K
     *   1   M      0	    0x03    384K
     *   1   M      0	    0x04    0  K
     *   1.5 M      0	    0x05    512K
     *   2   M      0	    0x06    1  M    4x512K
     *   2   M      0	    0x08    1  M    1x2M
     *   2.5 M      1	    0x01    1.5M    2M+512K
     *   3   M      1	    0x04    2  M    1x2M
     *   4   M      0	    0x09    3  M    2x2M
     *   4.5 M      1	    0x02    3.5M    2x2M+512K
     *   5   M      1	    0x05    4  M    2x2M+2x512K
     *   6   M      0	    0x0a    5  M    3x2M
     *   6.5 M      1	    0x03    5.5M    3x2M+512K
     *   8   M      0	    0x0b    7  M    4x2M
     */
    pclog("SCAT: mem_size=%d\n", mem_size);

    /* Create the 32 EMS page frame mappings for 256-640K. */
    for (i=0; i<24; i++) {
	mem_mapping_add(&scat_top_mapping[i],
			0x40000 + (i<<14), 0x4000,
			ems_pgrd, NULL, NULL,
			ems_pgwr, NULL, NULL,
			mem_size > 256+(i<<4) ? ram+0x40000+(i<<14) : NULL,
			MEM_MAPPING_INTERNAL, NULL);
	mem_mapping_enable(&scat_top_mapping[i]);
    }

    /* Re-map the 128K at A0000 (video BIOS) to above 16MB+top. */
    mem_mapping_add(&scat_A000_mapping,
		    0xA0000, 0x20000,
		    ems_pgrd, NULL, NULL,
		    ems_pgwr, NULL, NULL,
		    ram+0xA0000,
		    MEM_MAPPING_INTERNAL, NULL);
    mem_mapping_disable(&scat_A000_mapping);

    /* Create 32 page frames for EMS, each 16K. */
    for (i=0; i<32; i++) {
	scat_ems[i].regs_2x8 = 0xff;
	scat_ems[i].regs_2x9 = 0x03;
	mem_mapping_add(&scat_mapping[i],
			(i + (i >= 24 ? 28 : 16)) << 14, 0x04000,
			ems_pgrd, NULL, NULL,
			ems_pgwr, NULL, NULL,
			ram + ((i + (i >= 24 ? 28 : 16)) << 14),
			0, &scat_ems[i]);
	mem_mapping_disable(&scat_mapping[i]);
    }

    for (i=4; i<10; i++) isram[i] = 0;

    /* Re-map the BIOS ROM (C0000-FFFFF) area. */
    for (i=12; i<16; i++) {
	mem_mapping_add(&scat_high_mapping[i],
			(i<<14) + 0xFC0000, 0x04000,
			mem_read_bios, mem_read_biosw, mem_read_biosl,
			mem_write_null, mem_write_nullw, mem_write_nulll,
			rom+(i<<14),
			0, NULL);
    }

    for (i=0; i<6; i++) {
	mem_mapping_add(&scat_shadowram_mapping[i],
			0x100000 + (i<<16), 0x10000,
			ems_pgrd, NULL, NULL,
			ems_pgwr, NULL, NULL,
			mem_size >= 1024 ? ram+get_addr(0x100000+(i<<16), NULL) : NULL,
			MEM_MAPPING_INTERNAL, NULL);
    }

    set_xms_bound(0);
    shadow_state_update();
}


void
machine_at_scat_init(machine_t *model)
{
    machine_at_init(model);
    device_add(&fdc_at_device);

    scat_init();
}
