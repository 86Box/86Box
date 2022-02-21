/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the Toshiba T1000 and T1200 portables.
 *
 *		The T1000 is the T3100e's little brother -- a real laptop
 *		with a rechargeable battery.
 *
 *		Features: 80C88 at 4.77MHz
 *		- 512k system RAM
 *		- 640x200 monochrome LCD
 *		- 82-key keyboard
 *		- Real-time clock. Not the normal 146818, but a TC8521,
 *		   which is a 4-bit chip.
 *		- A ROM drive (128k, 256k or 512k) which acts as a mini
 *		  hard drive and contains a copy of DOS 2.11.
 *		- 160 bytes of non-volatile RAM for the CONFIG.SYS used
 *		  when booting from the ROM drive. Possibly physically
 *		  located in the keyboard controller RAM.
 *
 *		An optional memory expansion board can be fitted. This adds
 *		768k of RAM, which can be used for up to three purposes:
 *		> Conventional memory -- 128k between 512k and 640k
 *		> HardRAM -- a battery-backed RAM drive.
 *		> EMS
 *
 *		This means that there are up to three different
 *		implementations of non-volatile RAM in the same computer
 *		(52 nibbles in the TC8521, 160 bytes of CONFIG.SYS, and
 *		up to 768k of HardRAM).
 *
 *		The T1200 is a slightly upgraded version with a turbo mode
 *		(double CPU clock, 9.54MHz) and an optional hard drive.
 *		The interface for this is proprietary both at the physical
 *		and programming level.
 *
 *		01F2h: If hard drive is present, low 4 bits are 0Ch [20Mb]
 *			or 0Dh [10Mb].
 *
 *		The hard drive is a 20MB (615/2/26) RLL 3.5" drive.
 *
 *		The TC8521 is a 4-bit RTC, so each memory location can only
 *		hold a single BCD digit. Hence everything has 'ones' and
 *		'tens' digits.
 *
 * NOTE:	Still need to figure out a way to load/save ConfigSys and
 *		HardRAM stuff. Needs to be linked in to the NVR code.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2018,2019 Fred N. van Kempen.
 *		Copyright 2018,2019 Miran Grca.
 *		Copyright 2018,2019 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <time.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/mem.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/gameport.h>
#include <86box/video.h>
#include <86box/plat.h>
#include <86box/machine.h>
#include <86box/m_xt_t1000.h>


#define T1000_ROMSIZE	(512*1024UL)	/* Maximum ROM drive size is 512k */


enum TC8521_ADDR {
    /* Page 0 registers */
    TC8521_SECOND1 = 0,
    TC8521_SECOND10,
    TC8521_MINUTE1,
    TC8521_MINUTE10,
    TC8521_HOUR1,
    TC8521_HOUR10,
    TC8521_WEEKDAY,
    TC8521_DAY1,
    TC8521_DAY10,
    TC8521_MONTH1,
    TC8521_MONTH10,
    TC8521_YEAR1,
    TC8521_YEAR10,
    TC8521_PAGE,	/* PAGE register */
    TC8521_TEST,	/* TEST register */
    TC8521_RESET,	/* RESET register */

    /* Page 1 registers */
    TC8521_24HR = 0x1A,
    TC8521_LEAPYEAR = 0x1B
};


typedef struct {
    /* ROM drive */
    uint8_t	*romdrive;
    uint8_t	rom_ctl;
    uint32_t	rom_offset;
    mem_mapping_t rom_mapping;

    /* CONFIG.SYS drive. */
    uint8_t	t1000_nvram[160];
    uint8_t	t1200_nvram[2048];

    /* System control registers */
    uint8_t	sys_ctl[16];
    uint8_t	syskeys;
    uint8_t	turbo;

    /* NVRAM control */
    uint8_t	nvr_c0;
    uint8_t	nvr_tick;
    int		nvr_addr;
    uint8_t	nvr_active;
    mem_mapping_t	nvr_mapping;	/* T1200 NVRAM mapping */

    /* EMS data */
    uint8_t	ems_reg[4];
    mem_mapping_t mapping[4];
    uint32_t	page_exec[4];
    uint8_t	ems_port_index;
    uint16_t	ems_port;
    uint8_t	is_640k;
    uint32_t	ems_base;
    int32_t	ems_pages;

    fdc_t	 *fdc;

    nvr_t	nvr;
    int		is_t1200;
} t1000_t;


static t1000_t	t1000;


#ifdef ENABLE_T1000_LOG
int t1000_do_log = ENABLE_T1000_LOG;


static void
t1000_log(const char *fmt, ...)
{
   va_list ap;

   if (t1000_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define t1000_log(fmt, ...)
#endif


/* Set the chip time. */
static void
tc8521_time_set(uint8_t *regs, struct tm *tm)
{
    regs[TC8521_SECOND1] = (tm->tm_sec % 10);
    regs[TC8521_SECOND10] = (tm->tm_sec / 10);
    regs[TC8521_MINUTE1] = (tm->tm_min % 10);
    regs[TC8521_MINUTE10] = (tm->tm_min / 10);
    if (regs[TC8521_24HR] & 0x01) {
	regs[TC8521_HOUR1] = (tm->tm_hour % 10);
	regs[TC8521_HOUR10] = (tm->tm_hour / 10);
    } else {
	regs[TC8521_HOUR1] = ((tm->tm_hour % 12) % 10);
	regs[TC8521_HOUR10] = (((tm->tm_hour % 12) / 10) |
			       ((tm->tm_hour >= 12) ? 2 : 0));
    }
    regs[TC8521_WEEKDAY] = tm->tm_wday;
    regs[TC8521_DAY1] = (tm->tm_mday % 10);
    regs[TC8521_DAY10] = (tm->tm_mday / 10);
    regs[TC8521_MONTH1] = ((tm->tm_mon + 1) % 10);
    regs[TC8521_MONTH10] = ((tm->tm_mon + 1) / 10);
    regs[TC8521_YEAR1] = ((tm->tm_year - 80) % 10);
    regs[TC8521_YEAR10] = (((tm->tm_year - 80) % 100) / 10);
}


/* Get the chip time. */
#define nibbles(a)	(regs[(a##1)] + 10 * regs[(a##10)])
static void
tc8521_time_get(uint8_t *regs, struct tm *tm)
{
    tm->tm_sec = nibbles(TC8521_SECOND);
    tm->tm_min = nibbles(TC8521_MINUTE);
    if (regs[TC8521_24HR] & 0x01)
	tm->tm_hour = nibbles(TC8521_HOUR);
      else
	tm->tm_hour = ((nibbles(TC8521_HOUR) % 12) +
		      (regs[TC8521_HOUR10] & 0x02) ? 12 : 0);
    tm->tm_wday = regs[TC8521_WEEKDAY];
    tm->tm_mday = nibbles(TC8521_DAY);
    tm->tm_mon = (nibbles(TC8521_MONTH) - 1);
    tm->tm_year = (nibbles(TC8521_YEAR) + 1980);
}


/* This is called every second through the NVR/RTC hook. */
static void
tc8521_tick(nvr_t *nvr)
{
    t1000_log("TC8521: ping\n");
}


static void
tc8521_start(nvr_t *nvr)
{
    struct tm tm;

    /* Initialize the internal and chip times. */
    if (time_sync & TIME_SYNC_ENABLED) {
	/* Use the internal clock's time. */
	nvr_time_get(&tm);
	tc8521_time_set(nvr->regs, &tm);
    } else {
	/* Set the internal clock from the chip time. */
	tc8521_time_get(nvr->regs, &tm);
	nvr_time_set(&tm);
    }

#if 0
    /* Start the RTC - BIOS will do this. */
    nvr->regs[TC8521_PAGE] |= 0x80;
#endif
}


/* Write to one of the chip registers. */
static void
tc8521_write(uint16_t addr, uint8_t val, void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;
    uint8_t page;

    /* Get to the correct register page. */
    addr &= 0x0f;
    page = nvr->regs[0x0d] & 0x03;
    if (addr < 0x0d)
	addr += (16 * page);

    if (addr >= 0x10 && nvr->regs[addr] != val)
	nvr_dosave = 1;

    /* Store the new value. */
    nvr->regs[addr] = val;
}


/* Read from one of the chip registers. */
static uint8_t
tc8521_read(uint16_t addr, void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;
    uint8_t page;

    /* Get to the correct register page. */
    addr &= 0x0f;
    page = nvr->regs[0x0d] & 0x03;
    if (addr < 0x0d)
	addr += (16 * page);

    /* Grab and return the desired value. */
    return(nvr->regs[addr]);
}


/* Reset the 8521 to a default state. */
static void
tc8521_reset(nvr_t *nvr)
{
    /* Clear the NVRAM. */
    memset(nvr->regs, 0xff, nvr->size);

    /* Reset the RTC registers. */
    memset(nvr->regs, 0x00, 16);
    nvr->regs[TC8521_WEEKDAY] = 0x01;
    nvr->regs[TC8521_DAY1] = 0x01;
    nvr->regs[TC8521_MONTH1] = 0x01;
}


static void
tc8521_init(nvr_t *nvr, int size)
{
    /* This is machine specific. */
    nvr->size = size;
    nvr->irq = -1;

    /* Set up any local handlers here. */
    nvr->reset = tc8521_reset;
    nvr->start = tc8521_start;
    nvr->tick = tc8521_tick;

    /* Initialize the actual NVR. */
    nvr_init(nvr);

    io_sethandler(0x02c0, 16,
		  tc8521_read,NULL,NULL, tc8521_write,NULL,NULL, nvr);

}


/* Given an EMS page ID, return its physical address in RAM. */
static uint32_t
ems_execaddr(t1000_t *sys, int pg, uint16_t val)
{
    if (!(val & 0x80)) return(0);	/* Bit 7 reset => not mapped */
    if (!sys->ems_pages) return(0);	/* No EMS available: all used by
					 * HardRAM or conventional RAM */
    val &= 0x7f;

#if 0
    t1000_log("Select EMS page: %d of %d\n", val, sys->ems_pages);
#endif
    if (val < sys->ems_pages) {
	/* EMS is any memory above 512k,
	   with ems_base giving the start address */
	return((512 * 1024) + (sys->ems_base * 0x10000) + (0x4000 * val));
    }

    return(0);
}


static uint8_t
ems_in(uint16_t addr, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;

#if 0
    t1000_log("ems_in(%04x)=%02x\n", addr, sys->ems_reg[(addr >> 14) & 3]);
#endif
    return(sys->ems_reg[(addr >> 14) & 3]);
}


static void
ems_out(uint16_t addr, uint8_t val, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;
    int pg = (addr >> 14) & 3;

#if 0
    t1000_log("ems_out(%04x, %02x) pg=%d\n", addr, val, pg);
#endif
    sys->ems_reg[pg] = val;
    sys->page_exec[pg] = ems_execaddr(sys, pg, val);
    if (sys->page_exec[pg]) {
	/* Page present */
	mem_mapping_enable(&sys->mapping[pg]);
	mem_mapping_set_exec(&sys->mapping[pg], ram + sys->page_exec[pg]);
    } else {
	mem_mapping_disable(&sys->mapping[pg]);
    }
}


/* Hardram size is in 64k units */
static void
ems_set_hardram(t1000_t *sys, uint8_t val)
{
    int n;

    val &= 0x1f;	/* Mask off pageframe address */
    if (val && mem_size > 512)
	sys->ems_base = val;
      else
	sys->ems_base = 0;

#if 0
    t1000_log("EMS base set to %02x\n", val);
#endif
    sys->ems_pages = ((mem_size - 512) / 16) - 4 * sys->ems_base;
    if (sys->ems_pages < 0) sys->ems_pages = 0;

    /* Recalculate EMS mappings */
    for (n = 0; n < 4; n++)
	ems_out(n << 14, sys->ems_reg[n], sys);
}


static void
ems_set_640k(t1000_t *sys, uint8_t val)
{
    if (val && mem_size >= 640) {
	mem_mapping_set_addr(&ram_low_mapping, 0, 640 * 1024);
	sys->is_640k = 1;
    } else {
	mem_mapping_set_addr(&ram_low_mapping, 0, 512 * 1024);
	sys->is_640k = 0;
    }
}


static void
ems_set_port(t1000_t *sys, uint8_t val)
{
    int n;

#if 0
    t1000_log("ems_set_port(%d)", val & 0x0f);
#endif
    if (sys->ems_port) {
	for (n = 0; n <= 0xc000; n += 0x4000) {
		io_removehandler(sys->ems_port+n, 1,
				 ems_in,NULL,NULL, ems_out,NULL,NULL, sys);
	}
	sys->ems_port = 0;
    }

    val &= 0x0f;
    sys->ems_port_index = val;
    if (val == 7) {
	/* No EMS */
	sys->ems_port = 0;
    } else {
	sys->ems_port = 0x208 | (val << 4);
	for (n = 0; n <= 0xc000; n += 0x4000) {
		io_sethandler(sys->ems_port+n, 1,
			      ems_in,NULL,NULL, ems_out,NULL,NULL, sys);
	}
	sys->ems_port = 0;
    }

#if 0
    t1000_log(" -> %04x\n", sys->ems_port);
#endif
}


static int
addr_to_page(uint32_t addr)
{
    return((addr - 0xd0000) / 0x4000);
}


/* Read RAM in the EMS page frame. */
static uint8_t
ems_read_ram(uint32_t addr, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;
    int pg = addr_to_page(addr);

    if (pg < 0) return(0xff);
    addr = sys->page_exec[pg] + (addr & 0x3fff);

    return(ram[addr]);
}


static uint16_t
ems_read_ramw(uint32_t addr, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;
    int pg = addr_to_page(addr);

    if (pg < 0) return(0xff);

#if 0
    t1000_log("ems_read_ramw addr=%05x ", addr);
#endif
    addr = sys->page_exec[pg] + (addr & 0x3FFF);

#if 0
    t1000_log("-> %06x val=%04x\n", addr, *(uint16_t *)&ram[addr]);
#endif

    return(*(uint16_t *)&ram[addr]);
}


static uint32_t
ems_read_raml(uint32_t addr, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;
    int pg = addr_to_page(addr);

    if (pg < 0) return(0xff);
    addr = sys->page_exec[pg] + (addr & 0x3fff);

    return(*(uint32_t *)&ram[addr]);
}


/* Write RAM in the EMS page frame. */
static void
ems_write_ram(uint32_t addr, uint8_t val, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;
    int pg = addr_to_page(addr);

    if (pg < 0) return;

    addr = sys->page_exec[pg] + (addr & 0x3fff);
    if (ram[addr] != val) nvr_dosave = 1;

    ram[addr] = val;
}


static void
ems_write_ramw(uint32_t addr, uint16_t val, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;
    int pg = addr_to_page(addr);

    if (pg < 0) return;

#if 0
    t1000_log("ems_write_ramw addr=%05x ", addr);
#endif
    addr = sys->page_exec[pg] + (addr & 0x3fff);

#if 0
    t1000_log("-> %06x val=%04x\n", addr, val);
#endif

    if (*(uint16_t *)&ram[addr] != val) nvr_dosave = 1;

    *(uint16_t *)&ram[addr] = val;
}


static void
ems_write_raml(uint32_t addr, uint32_t val, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;
    int pg = addr_to_page(addr);

    if (pg < 0) return;

    addr = sys->page_exec[pg] + (addr & 0x3fff);
    if (*(uint32_t *)&ram[addr] != val) nvr_dosave = 1;

    *(uint32_t *)&ram[addr] = val;
}


static uint8_t
read_ctl(uint16_t addr, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;
    uint8_t ret = 0xff;

    switch (addr & 0x0f) {
	case 1:
		ret = sys->syskeys;
		break;

	case 0x0f:	/* Detect EMS board */
		switch (sys->sys_ctl[0x0e]) {
			case 0x50:
				if (mem_size > 512)
					ret = (0x90 | sys->ems_port_index);
				break;

			case 0x51:
				/* 0x60 is the page frame address:
				   (0xd000 - 0xc400) / 0x20 */
				ret = (sys->ems_base | 0x60);
				break;

			case 0x52:
				ret = (sys->is_640k ? 0x80 : 0);
				break;
		}
		break;

	default:
		ret = (sys->sys_ctl[addr & 0x0f]);
    }

    return(ret);
}


static void
t1200_turbo_set(uint8_t value)
{
    if (value == t1000.turbo) return;

    t1000.turbo = value;
    if (! value)
	cpu_dynamic_switch(0);
      else
	cpu_dynamic_switch(cpu);
}


static void
write_ctl(uint16_t addr, uint8_t val, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;

    sys->sys_ctl[addr & 0x0f] = val;
    switch (addr & 0x0f) {
	case 4:		/* Video control */
		if (sys->sys_ctl[3] == 0x5A) {
			t1000_video_options_set((val & 0x20) ? 1 : 0);
			t1000_display_set((val & 0x40) ? 0 : 1);
			if (sys->is_t1200)
				t1200_turbo_set((val & 0x80) ? 1 : 0);
		}
		break;

	/* It looks as if the T1200, like the T3100, can disable
	 * its builtin video chipset if it detects the presence of
	 * another video card. */
	case 6: if (sys->is_t1200)
		{
			t1000_video_enable(val & 0x01 ? 0 : 1);
		}
		break;

	case 0x0f:	/* EMS control */
		switch (sys->sys_ctl[0x0e]) {
			case 0x50:
				ems_set_port(sys, val);
				break;

			case 0x51:
				ems_set_hardram(sys, val);
				break;

			case 0x52:
				ems_set_640k(sys, val);
				break;
		}
		break;
    }
}


/* Ports 0xC0 to 0xC3 appear to have two purposes:
 *
 * > Access to the 160 bytes of non-volatile RAM containing CONFIG.SYS
 * > Reading the floppy changeline. I don't know why the Toshiba doesn't
 *   use the normal port 0x3F7 for this, but it doesn't.
 *
 */
static uint8_t
t1000_read_nvram(uint16_t addr, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;
    uint8_t tmp = 0xff;

    switch (addr) {
	case 0xc2: /* Read next byte from NVRAM */
		if (sys->nvr_addr >= 0 && sys->nvr_addr < 160)
			tmp = sys->t1000_nvram[sys->nvr_addr];
		sys->nvr_addr++;
		break;

	case 0xc3: /* Read floppy changeline and NVRAM ready state */
		tmp = fdc_read(0x03f7, t1000.fdc);

		tmp = (tmp & 0x80) >> 3;	/* Bit 4 is changeline */
		tmp |= (sys->nvr_active & 0xc0);/* Bits 6,7 are r/w mode */
		tmp |= 0x2e;			/* Bits 5,3,2,1 always 1 */
		tmp |= (sys->nvr_active & 0x40) >> 6;	/* Ready state */
		break;
    }

    return(tmp);
}


static void
t1000_write_nvram(uint16_t addr, uint8_t val, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;

    /*
     * On the real T1000, port 0xC1 is only usable as the high byte
     * of a 16-bit write to port 0xC0, with 0x5A in the low byte.
     */
    switch (addr) {
	case 0xc0:
		sys->nvr_c0 = val;
		break;

	case 0xc1:	/* Write next byte to NVRAM */
		if (sys->nvr_addr >= 0 && sys->nvr_addr < 160) {
			if (sys->t1000_nvram[sys->nvr_addr] != val)
				nvr_dosave = 1;
			sys->t1000_nvram[sys->nvr_addr] = val;
		}
		sys->nvr_addr++;
		break;

	case 0xc2:
		break;

	case 0xc3:
		/*
		 * At start of NVRAM read / write, 0x80 is written to
		 * port 0xC3. This seems to reset the NVRAM address
		 * counter. A single byte is then written (0xff for
		 * write, 0x00 for read) which appears to be ignored.
		 * Simulate that by starting the address counter off
		 * at -1.
		 */
		sys->nvr_active = val;
		if (val == 0x80) sys->nvr_addr = -1;
		break;
    }
}


static
uint8_t read_t1200_nvram(uint32_t addr, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;

    return sys->t1200_nvram[addr & 0x7FF];
}


static void write_t1200_nvram(uint32_t addr, uint8_t value, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;

    if (sys->t1200_nvram[addr & 0x7FF] != value)
	nvr_dosave = 1;

    sys->t1200_nvram[addr & 0x7FF] = value;
}


/* Port 0xC8 controls the ROM drive */
static uint8_t
t1000_read_rom_ctl(uint16_t addr, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;

    return(sys->rom_ctl);
}


static void
t1000_write_rom_ctl(uint16_t addr, uint8_t val, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;

    sys->rom_ctl = val;
    if (sys->romdrive && (val & 0x80)) {
	/* Enable */
	sys->rom_offset = ((val & 0x7f) * 0x10000) % T1000_ROMSIZE;
	mem_mapping_set_addr(&sys->rom_mapping, 0xa0000, 0x10000);
	mem_mapping_set_exec(&sys->rom_mapping, sys->romdrive + sys->rom_offset);
	mem_mapping_enable(&sys->rom_mapping);
    } else {
	mem_mapping_disable(&sys->rom_mapping);
    }
}


/* Read the ROM drive */
static uint8_t
t1000_read_rom(uint32_t addr, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;

    if (! sys->romdrive) return(0xff);

    return(sys->romdrive[sys->rom_offset + (addr & 0xffff)]);
}


static uint16_t
t1000_read_romw(uint32_t addr, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;

    if (! sys->romdrive) return(0xffff);

    return(*(uint16_t *)(&sys->romdrive[sys->rom_offset + (addr & 0xffff)]));
}


static uint32_t
t1000_read_roml(uint32_t addr, void *priv)
{
    t1000_t *sys = (t1000_t *)priv;

    if (! sys->romdrive) return(0xffffffff);

    return(*(uint32_t *)(&sys->romdrive[sys->rom_offset + (addr & 0xffff)]));
}


const device_t *
t1000_get_device(void)
{
    return(&t1000_video_device);
}


int
machine_xt_t1000_init(const machine_t *model)
{
    FILE *f;
    int pg;

    int ret;

    ret = bios_load_linear("roms/machines/t1000/t1000.rom",
			   0x000f8000, 32768, 0);

    if (bios_only || !ret)
	return ret;

    memset(&t1000, 0x00, sizeof(t1000));
    t1000.is_t1200 = 0;
    t1000.turbo = 0xff;
    t1000.ems_port_index = 7;	/* EMS disabled */

    /* Load the T1000 CGA Font ROM. */
    loadfont("roms/machines/t1000/t1000font.bin", 2);

    /*
     * The ROM drive is optional.
     *
     * If the file is missing, continue to boot; the BIOS will
     * complain 'No ROM drive' but boot normally from floppy.
     */
    f = rom_fopen("roms/machines/t1000/t1000dos.rom", "rb");
    if (f != NULL) {
	t1000.romdrive = malloc(T1000_ROMSIZE);
	if (t1000.romdrive) {
		memset(t1000.romdrive, 0xff, T1000_ROMSIZE);
		if (fread(t1000.romdrive, 1, T1000_ROMSIZE, f) != T1000_ROMSIZE)
			fatal("machine_xt_t1000_init(): Error reading DOS ROM data\n");
	}
	fclose(f);
    }
    mem_mapping_add(&t1000.rom_mapping, 0xa0000, 0x10000,
		    t1000_read_rom,t1000_read_romw,t1000_read_roml,
		    NULL,NULL,NULL, NULL, MEM_MAPPING_EXTERNAL, &t1000);
    mem_mapping_disable(&t1000.rom_mapping);

    /* Map the EMS page frame */
    for (pg = 0; pg < 4; pg++) {
	mem_mapping_add(&t1000.mapping[pg], 0xd0000 + (0x4000 * pg), 16384,
			ems_read_ram,ems_read_ramw,ems_read_raml,
			ems_write_ram,ems_write_ramw,ems_write_raml,
			NULL, MEM_MAPPING_EXTERNAL, &t1000);

	/* Start them all off disabled */
	mem_mapping_disable(&t1000.mapping[pg]);
    }

    /* Non-volatile RAM for CONFIG.SYS */
    io_sethandler(0xc0, 4,
		  t1000_read_nvram,NULL,NULL,
		  t1000_write_nvram,NULL,NULL, &t1000);

    /* ROM drive */
    io_sethandler(0xc8, 1,
		  t1000_read_rom_ctl,NULL,NULL,
		  t1000_write_rom_ctl,NULL,NULL, &t1000);

    /* System control functions, and add-on memory board */
    io_sethandler(0xe0, 16,
		  read_ctl,NULL,NULL, write_ctl,NULL,NULL, &t1000);

    machine_common_init(model);

    pit_ctr_set_out_func(&pit->counters[1], pit_refresh_timer_xt);
    device_add(&keyboard_xt_device);
    t1000.fdc = device_add(&fdc_xt_device);
    nmi_init();

    tc8521_init(&t1000.nvr, model->nvrmask + 1);

    t1000_nvr_load();
    nvr_set_ven_save(t1000_nvr_save);

    if (gfxcard == VID_INTERNAL)
		device_add(&t1000_video_device);

    return ret;
}


const device_t *
t1200_get_device(void)
{
    return(&t1200_video_device);
}


int
machine_xt_t1200_init(const machine_t *model)
{
    int pg;

    int ret;

    ret = bios_load_linear("roms/machines/t1200/t1200_019e.ic15.bin",
			   0x000f8000, 32768, 0);

    if (bios_only || !ret)
	return ret;

    memset(&t1000, 0x00, sizeof(t1000));
    t1000.is_t1200 = 1;
    t1000.ems_port_index = 7;	/* EMS disabled */

    /* Load the T1000 CGA Font ROM. */
    loadfont("roms/machines/t1000/t1000font.bin", 2);

    /* Map the EMS page frame */
    for (pg = 0; pg < 4; pg++) {
	mem_mapping_add(&t1000.mapping[pg],
			0xd0000 + (0x4000 * pg), 16384,
			ems_read_ram,ems_read_ramw,ems_read_raml,
			ems_write_ram,ems_write_ramw,ems_write_raml,
			NULL, MEM_MAPPING_EXTERNAL, &t1000);

	/* Start them all off disabled */
	mem_mapping_disable(&t1000.mapping[pg]);
    }

    /* System control functions, and add-on memory board */
    io_sethandler(0xe0, 16,
		  read_ctl,NULL,NULL, write_ctl,NULL,NULL, &t1000);

    machine_common_init(model);

    mem_mapping_add(&t1000.nvr_mapping,
		    0x000f0000, 2048,
		    read_t1200_nvram, NULL, NULL,
		    write_t1200_nvram, NULL, NULL,
		    NULL, MEM_MAPPING_EXTERNAL, &t1000);

    pit_ctr_set_out_func(&pit->counters[1], pit_refresh_timer_xt);
    device_add(&keyboard_xt_device);
    t1000.fdc = device_add(&fdc_xt_t1x00_device);
    nmi_init();

    tc8521_init(&t1000.nvr, model->nvrmask + 1);

    t1200_nvr_load();
    nvr_set_ven_save(t1200_nvr_save);

    if (gfxcard == VID_INTERNAL)
	device_add(&t1200_video_device);

    return ret;
}


void
t1000_syskey(uint8_t andmask, uint8_t ormask, uint8_t xormask)
{
    t1000.syskeys &= ~andmask;
    t1000.syskeys |= ormask;
    t1000.syskeys ^= xormask;
}


static void
t1000_configsys_load(void)
{
    FILE *f;
    int size;

    memset(t1000.t1000_nvram, 0x1a, sizeof(t1000.t1000_nvram));
    f = plat_fopen(nvr_path("t1000_config.nvr"), "rb");
    if (f != NULL) {
	size = sizeof(t1000.t1000_nvram);
	if (fread(t1000.t1000_nvram, 1, size, f) != size)
		fatal("t1000_configsys_load(): Error reading data\n");
	fclose(f);
    }
}


static void
t1000_configsys_save(void)
{
    FILE *f;
    int size;

    f = plat_fopen(nvr_path("t1000_config.nvr"), "wb");
    if (f != NULL) {
	size = sizeof(t1000.t1000_nvram);
	if (fwrite(t1000.t1000_nvram, 1, size, f) != size)
		fatal("t1000_configsys_save(): Error writing data\n");
	fclose(f);
    }
}


static void
t1200_state_load(void)
{
    FILE *f;
    int size;

    memset(t1000.t1200_nvram, 0, sizeof(t1000.t1200_nvram));
    f = plat_fopen(nvr_path("t1200_state.nvr"), "rb");
    if (f != NULL) {
	size = sizeof(t1000.t1200_nvram);
	if (fread(t1000.t1200_nvram, 1, size, f) != size)
		fatal("t1200_state_load(): Error reading data\n");
	fclose(f);
    }
}


static void
t1200_state_save(void)
{
    FILE *f;
    int size;

    f = plat_fopen(nvr_path("t1200_state.nvr"), "wb");
    if (f != NULL) {
	size = sizeof(t1000.t1200_nvram);
	if (fwrite(t1000.t1200_nvram, 1, size, f) != size)
		fatal("t1200_state_save(): Error writing data\n");
	fclose(f);
    }
}


/* All RAM beyond 512k is non-volatile */
static void
t1000_emsboard_load(void)
{
    FILE *f;

    if (mem_size > 512) {
	f = plat_fopen(nvr_path("t1000_ems.nvr"), "rb");
	if (f != NULL) {
		fread(&ram[512 * 1024], 1024, (mem_size - 512), f);
		fclose(f);
	}
    }
}


static void
t1000_emsboard_save(void)
{
    FILE *f;

    if (mem_size > 512) {
	f = plat_fopen(nvr_path("t1000_ems.nvr"), "wb");
	if (f != NULL) {
		fwrite(&ram[512 * 1024], 1024, (mem_size - 512), f);
		fclose(f);
	}
    }
}


void
t1000_nvr_load(void)
{
    t1000_emsboard_load();
    t1000_configsys_load();
}


void
t1000_nvr_save(void)
{
    t1000_emsboard_save();
    t1000_configsys_save();
}


void
t1200_nvr_load(void)
{
    t1000_emsboard_load();
    t1200_state_load();
}


void
t1200_nvr_save(void)
{
    t1000_emsboard_save();
    t1200_state_save();
}
