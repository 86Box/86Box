#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../io.h"
#include "../keyboard.h"
#include "../lpt.h"
#include "../mem.h"
#include "../nmi.h"
#include "../nvr.h"
#include "../nvr_tc8521.h"
#include "../pit.h"
#include "../plat.h"
#include "../rom.h"
#include "../cpu/cpu.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../game/gameport.h"
#include "../video/vid_t1000.h"
#include "machine.h"

/* The T1000 is the T3100e's little brother -- a real laptop with a 
 * rechargeable battery. 
 *
 * Features: 80C88 at 4.77MHz
 *           512k system RAM
 *           640x200 monochrome LCD
 *           82-key keyboard 
 *           Real-time clock. Not the normal 146818, but a TC8521, which 
 *                is a 4-bit chip.
 *           A ROM drive (128k, 256k or 512k) which acts as a mini hard 
 *                drive and contains a copy of DOS 2.11. 
 *           160 bytes of non-volatile RAM for the CONFIG.SYS used when 
 *                booting from the ROM drive. Possibly physically located
 *                in the keyboard controller RAM.
 *
 * An optional memory expansion board can be fitted. This adds 768k of RAM,
 * which can be used for up to three purposes:
 *	> Conventional memory -- 128k between 512k and 640k
 *	> HardRAM -- a battery-backed RAM drive.
 *      > EMS
 *
 * This means that there are up to three different implementations of 
 * non-volatile RAM in the same computer (52 nibbles in the TC8521, 160 
 * bytes of CONFIG.SYS, and up to 768k of HardRAM).
 *
 * The T1200 is a slightly upgraded version with a turbo mode (double CPU 
 * clock, 9.54MHz) and an optional hard drive. The interface for this is
 * proprietary both at the physical and programming level.
 *
 *  01F2h: If hard drive is present, low 4 bits are 0Ch [20Mb] or 0Dh [10Mb]. 
 *
 */

void common_init();

uint8_t config_sys[160];

static struct t1000_system
{
	/* ROM drive */
	uint8_t *romdrive;
	uint8_t rom_ctl;
	uint32_t rom_offset;
	mem_mapping_t rom_mapping;

	/* System control registers */
	uint8_t sys_ctl[16];
	uint8_t syskeys;
	uint8_t turbo;

	/* NVRAM control */
	uint8_t nvr_c0;	
	uint8_t nvr_tick;	
	int     nvr_addr;
	uint8_t nvr_active;

	/* EMS data */
	uint8_t ems_reg[4];
	mem_mapping_t mapping[4];
	uint32_t page_exec[4];
	uint8_t  ems_port_index;
	uint16_t ems_port;
	uint8_t  is_640k;	
	uint32_t ems_base;
	int32_t  ems_pages;

	fdc_t	 *fdc;
} t1000;

#define T1000_ROMSIZE (512 * 1024)	/* Maximum ROM drive size is 512k */

/* The CONFIG.SYS storage (160 bytes) and battery-backed EMS (all RAM 
 * above 512k) are implemented as NVR devices */

void t1000_configsys_loadnvr()
{
	FILE *f;

	memset(config_sys, 0x1A, sizeof(config_sys));
	f = plat_fopen(nvr_path(L"t1000_config.nvr"), L"rb");
	if (f)
	{
		fread(config_sys, sizeof(config_sys), 1, f);
		fclose(f);
	}
}

/* All RAM beyond 512k is non-volatile */
void t1000_emsboard_loadnvr()
{
	FILE *f;

	if (mem_size > 512)
	{
		f = plat_fopen(nvr_path(L"t1000_ems.nvr"), L"rb");
		if (f)
		{
			fread(&ram[512 * 1024], 
				1024, (mem_size - 512), f);
			fclose(f);
		}
	}
}

void t1000_configsys_savenvr()
{
	FILE *f;

	f = plat_fopen(nvr_path(L"t1000_config.nvr"), L"wb");
	if (f)
	{
		fwrite(config_sys, sizeof(config_sys), 1, f);
		fclose(f);
	}
}


void t1000_emsboard_savenvr()
{
	FILE *f;

	if (mem_size > 512)
	{
		f = plat_fopen(nvr_path(L"t1000_ems.nvr"), L"wb");
		if (f)
		{
			fwrite(&ram[512 * 1024], 
				1024, (mem_size - 512), f);
			fclose(f);
		}
	}
}



/* Given an EMS page ID, return its physical address in RAM. */
uint32_t ems_execaddr(struct t1000_system *sys, int pg, uint16_t val)
{
	if (!(val & 0x80)) return 0;	/* Bit 7 reset => not mapped */
	if (!sys->ems_pages) return 0;	/* No EMS available: all used by 
					 * HardRAM or conventional RAM */

	val &= 0x7F;

/*	pclog("Select EMS page: %d of %d\n", val, sys->ems_pages); */
	if (val < sys->ems_pages)
	{
/* EMS is any memory above 512k, with ems_base giving the start address */
		return (512 * 1024) + (sys->ems_base * 0x10000) + (0x4000 * val);
	}
	return 0;
}


static uint8_t ems_in(uint16_t addr, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;

/*	pclog("ems_in(%04x)=%02x\n", addr, sys->ems_reg[(addr >> 14) & 3]); */
	return sys->ems_reg[(addr >> 14) & 3];
}

static void ems_out(uint16_t addr, uint8_t val, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;
	int pg = (addr >> 14) & 3;

/*	pclog("ems_out(%04x, %02x) pg=%d\n", addr, val, pg); */
	sys->ems_reg[pg] = val;
	sys->page_exec[pg] = ems_execaddr(sys, pg, val);
	if (sys->page_exec[pg]) /* Page present */
	{
		mem_mapping_enable(&sys->mapping[pg]);
		mem_mapping_set_exec(&sys->mapping[pg], ram + sys->page_exec[pg]);
	}
	else
	{
		mem_mapping_disable(&sys->mapping[pg]);
	}
}


/* Hardram size is in 64k units */
static void ems_set_hardram(struct t1000_system *sys, uint8_t val)
{
	int n;

	val &= 0x1F;	/* Mask off pageframe address */
	if (val && mem_size > 512)
	{
		sys->ems_base = val;
	} 
	else
	{
		sys->ems_base = 0;
	}
/*	pclog("EMS base set to %02x\n", val); */
	sys->ems_pages = 48 - 4 * sys->ems_base;
	if (sys->ems_pages < 0) sys->ems_pages = 0;
	/* Recalculate EMS mappings */
	for (n = 0; n < 4; n++)
	{
		ems_out(n << 14, sys->ems_reg[n], sys);
	}
}


static void ems_set_640k(struct t1000_system *sys, uint8_t val)
{
	if (val && mem_size >= 640)
	{
		mem_mapping_set_addr(&ram_low_mapping, 0, 640 * 1024);
		sys->is_640k = 1;
	} 
	else
	{
		mem_mapping_set_addr(&ram_low_mapping, 0, 512 * 1024);
		sys->is_640k = 0;
	}
}

static void ems_set_port(struct t1000_system *sys, uint8_t val)
{
	int n;

/*	pclog("ems_set_port(%d)", val & 0x0F); */
	if (sys->ems_port)
	{
		for (n = 0; n <= 0xC000; n += 0x4000)
		{
			io_removehandler(sys->ems_port + n, 0x01, 
				ems_in, NULL, NULL, ems_out, NULL, NULL, sys);
		}
		sys->ems_port = 0;
	}
	val &= 0x0F;	
	sys->ems_port_index = val;
	if (val == 7)	/* No EMS */
	{
		sys->ems_port = 0;
	}
	else
	{
		sys->ems_port = 0x208 | (val << 4);
		for (n = 0; n <= 0xC000; n += 0x4000)
		{
			io_sethandler(sys->ems_port + n, 0x01, 
				ems_in, NULL, NULL, ems_out, NULL, NULL, sys);
		}
		sys->ems_port = 0;
	}
/*	pclog(" -> %04x\n", sys->ems_port); */
}

static int addr_to_page(uint32_t addr)
{
	return (addr - 0xD0000) / 0x4000;
}

/* Read RAM in the EMS page frame */
static uint8_t ems_read_ram(uint32_t addr, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;
	int pg = addr_to_page(addr);

	if (pg < 0) return 0xFF;
	addr = sys->page_exec[pg] + (addr & 0x3FFF);
	return ram[addr];	
}




static uint16_t ems_read_ramw(uint32_t addr, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;
	int pg = addr_to_page(addr);

	if (pg < 0) return 0xFF;
	/* pclog("ems_read_ramw addr=%05x ", addr); */
	addr = sys->page_exec[pg] + (addr & 0x3FFF);
	/* pclog("-> %06x val=%04x\n", addr, *(uint16_t *)&ram[addr]);	 */
	return *(uint16_t *)&ram[addr];	
}


static uint32_t ems_read_raml(uint32_t addr, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;
	int pg = addr_to_page(addr);

	if (pg < 0) return 0xFF;
	addr = sys->page_exec[pg] + (addr & 0x3FFF);
	return *(uint32_t *)&ram[addr];	
}

/* Write RAM in the EMS page frame */
static void ems_write_ram(uint32_t addr, uint8_t val, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;
	int pg = addr_to_page(addr);

	if (pg < 0) return;
	addr = sys->page_exec[pg] + (addr & 0x3FFF);
	if (ram[addr] != val) nvr_dosave = 1;
	ram[addr] = val;	
}


static void ems_write_ramw(uint32_t addr, uint16_t val, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;
	int pg = addr_to_page(addr);

	if (pg < 0) return;
	/* pclog("ems_write_ramw addr=%05x ", addr); */
	addr = sys->page_exec[pg] + (addr & 0x3FFF);
	/* pclog("-> %06x val=%04x\n", addr, val); */

	if (*(uint16_t *)&ram[addr] != val) nvr_dosave = 1;	
	*(uint16_t *)&ram[addr] = val;	
}


static void ems_write_raml(uint32_t addr, uint32_t val, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;
	int pg = addr_to_page(addr);

	if (pg < 0) return;
	addr = sys->page_exec[pg] + (addr & 0x3FFF);
	if (*(uint32_t *)&ram[addr] != val) nvr_dosave = 1;	
	*(uint32_t *)&ram[addr] = val;	
}





void t1000_syskey(uint8_t andmask, uint8_t ormask, uint8_t xormask)
{
	t1000.syskeys &= ~andmask;
	t1000.syskeys |= ormask;
	t1000.syskeys ^= xormask;
}



static uint8_t read_t1000_ctl(uint16_t addr, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;

	switch (addr & 0x0F)
	{
		case 1:    return sys->syskeys; 
/* Detect EMS board */
		case 0x0F: switch (sys->sys_ctl[0x0E])
			   {
				case 0x50: if (mem_size <= 512) 
						return 0xFF;
					   return 0x90 | sys->ems_port_index;
/* 0x60 is the page frame address: (0xD000 - 0xC400) / 0x20 */
				case 0x51: return sys->ems_base | 0x60;
				case 0x52: return sys->is_640k ? 0x80 : 0;
			   }
			   return 0xFF;
		default: return sys->sys_ctl[addr & 0x0F]; 
	}
}

static void t1200_turbo_set(uint8_t value)
{
	if (value == t1000.turbo)
	{
		return;
	}
	t1000.turbo = value;
	if (!value)
	{
		cpu_dynamic_switch(0);
	}
	else
	{
		cpu_dynamic_switch(cpu);
	}
}

static void write_t1000_ctl(uint16_t addr, uint8_t val, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;

	sys->sys_ctl[addr & 0x0F] = val;
	switch (addr & 0x0F)
	{
		/* Video control */
		case 4: if (sys->sys_ctl[3] == 0x5A) 
			{
				t1000_video_options_set((val & 0x20) ? 1 : 0);
				t1000_display_set((val & 0x40) ? 0 : 1);
				if (romset == ROM_T1200)
				{
					t1200_turbo_set((val & 0x80) ? 1 : 0);
				}
			}
			break;
		/* EMS control*/
		case 0x0F:	
			switch (sys->sys_ctl[0x0E])
			{
				case 0x50: ems_set_port(sys, val); break;
				case 0x51: ems_set_hardram(sys, val); break;
				case 0x52: ems_set_640k(sys, val); break;
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
static uint8_t read_t1000_nvram(uint16_t addr, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;
	uint8_t tmp;

	switch (addr)
	{
		case 0xC2: /* Read next byte from NVRAM */
		tmp = 0xFF;
		if (sys->nvr_addr >= 0 && sys->nvr_addr < 160)
		tmp = config_sys[sys->nvr_addr];
		++sys->nvr_addr;
		return tmp;

		case 0xC3: /* Read floppy changeline and NVRAM ready state */
		tmp = fdc_read(0x3F7, t1000.fdc);

		tmp = (tmp & 0x80) >> 3;	/* Bit 4 is changeline */
		tmp |= (sys->nvr_active & 0xC0);/* Bits 6,7 are r/w mode */
		tmp |= 0x2E;			/* Bits 5,3,2,1 always 1 */
		tmp |= (sys->nvr_active & 0x40) >> 6;	/* Ready state */
		return tmp;
	}
	return 0xFF;
}

static void write_t1000_nvram(uint16_t addr, uint8_t val, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;

	switch (addr)
	{
/* On the real T1000, port 0xC1 is only usable as the high byte of a 16-bit 
 * write to port 0xC0, with 0x5A in the low byte. */
		case 0xC0: sys->nvr_c0 = val; 
			   break;
/* Write next byte to NVRAM */
		case 0xC1: if (sys->nvr_addr >= 0 && sys->nvr_addr < 160)
			   {
				if (config_sys[sys->nvr_addr] != val) 
					nvr_dosave = 1;
			   	config_sys[sys->nvr_addr] = val;
			   }
			   ++sys->nvr_addr;
			   break;
		case 0xC2: break;
/* At start of NVRAM read / write, 0x80 is written to port 0xC3. This seems
 * to reset the NVRAM address counter. A single byte is then written (0xFF
 * for write, 0x00 for read) which appears to be ignored. Simulate that by
 * starting the address counter off at -1 */
		case 0xC3: sys->nvr_active = val; 
			   if (val == 0x80) sys->nvr_addr = -1; 
			   break;
	}
}

/* Port 0xC8 controls the ROM drive */
static uint8_t read_t1000_rom_ctl(uint16_t addr, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;

	return sys->rom_ctl;
}

static void write_t1000_rom_ctl(uint16_t addr, uint8_t val, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;

	sys->rom_ctl = val;
	if (sys->romdrive && (val & 0x80))	/* Enable */
	{
		sys->rom_offset = ((val & 0x7F) * 0x10000) % T1000_ROMSIZE;
		mem_mapping_set_addr(&sys->rom_mapping, 0xA0000, 0x10000);
		mem_mapping_set_exec(&sys->rom_mapping, sys->romdrive +
					sys->rom_offset);
		mem_mapping_enable(&sys->rom_mapping);	
	}
	else
	{
		mem_mapping_disable(&sys->rom_mapping);	
	}
}


/* Read the ROM drive */
static uint8_t t1000_read_rom(uint32_t addr, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;

	if (!sys->romdrive) return 0xFF;
	return sys->romdrive[sys->rom_offset + (addr & 0xFFFF)];
}

static uint16_t t1000_read_romw(uint32_t addr, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;

	if (!sys->romdrive) return 0xFFFF;
	return *(uint16_t *)(&sys->romdrive[sys->rom_offset + (addr & 0xFFFF)]);
}


static uint32_t t1000_read_roml(uint32_t addr, void *priv)
{
	struct t1000_system *sys = (struct t1000_system *)priv;

	if (!sys->romdrive) return 0xFFFFFFFF;
	return *(uint32_t *)(&sys->romdrive[sys->rom_offset + (addr & 0xFFFF)]);
}


device_t *
t1000_get_device(void)
{
    return &t1000_device;
}


void machine_xt_t1000_init(machine_t *model)
{
	FILE *f;
	int pg;

	memset(&t1000, 0, sizeof(t1000));
	t1000.turbo = 0xff;
	t1000.ems_port_index = 7;	/* EMS disabled */
/* The ROM drive is optional. If the file is missing, continue to boot; the
 * BIOS will complain 'No ROM drive' but boot normally from floppy. */

	f = rom_fopen(L"roms/machines/t1000/t1000dos.rom", L"rb");
	if (f)
	{
		t1000.romdrive = malloc(T1000_ROMSIZE);
		if (t1000.romdrive)
		{
			memset(t1000.romdrive, 0xFF, T1000_ROMSIZE);
			fread(t1000.romdrive, T1000_ROMSIZE, 1, f);
		}
		fclose(f);
	}
	mem_mapping_add(&t1000.rom_mapping, 0xA0000, 0x10000,
			t1000_read_rom, t1000_read_romw, t1000_read_roml,
			NULL, NULL, NULL, NULL, MEM_MAPPING_INTERNAL, &t1000);
	mem_mapping_disable(&t1000.rom_mapping);

	/* Map the EMS page frame */
	for (pg = 0; pg < 4; pg++)
	{
		mem_mapping_add(&t1000.mapping[pg], 
			0xD0000 + (0x4000 * pg), 16384, 
			ems_read_ram,  ems_read_ramw,  ems_read_raml,
			ems_write_ram, ems_write_ramw, ems_write_raml,
			NULL, MEM_MAPPING_EXTERNAL, 
			&t1000);
		/* Start them all off disabled */
		mem_mapping_disable(&t1000.mapping[pg]);
	}
	
	/* Non-volatile RAM for CONFIG.SYS */
	io_sethandler(0xC0, 4, read_t1000_nvram, NULL, NULL,
			write_t1000_nvram, NULL, NULL, &t1000);
	/* ROM drive */
	io_sethandler(0xC8, 1, read_t1000_rom_ctl, NULL, NULL,
			write_t1000_rom_ctl, NULL, NULL, &t1000);
	/* System control functions, and add-on memory board */
	io_sethandler(0xE0, 0x10, read_t1000_ctl, NULL, NULL,
			write_t1000_ctl, NULL, NULL, &t1000);

	machine_common_init(model);
	pit_set_out_func(&pit, 1, pit_refresh_timer_xt);
	device_add(&keyboard_xt_device);
	t1000.fdc = device_add(&fdc_xt_device);
	nmi_init();
	nvr_tc8521_init();
/* No gameport, and no provision to fit one 	device_add(&gameport_device); */
}


device_t *
t1200_get_device(void)
{
    return &t1200_device;
}


void machine_xt_t1200_init(machine_t *model)
{
	int pg;

	memset(&t1000, 0, sizeof(t1000));
	t1000.ems_port_index = 7;	/* EMS disabled */

	mem_mapping_add(&t1000.rom_mapping, 0xA0000, 0x10000,
			t1000_read_rom, t1000_read_romw, t1000_read_roml,
			NULL, NULL, NULL, NULL, MEM_MAPPING_INTERNAL, &t1000);
	mem_mapping_disable(&t1000.rom_mapping);

	/* Map the EMS page frame */
	for (pg = 0; pg < 4; pg++)
	{
		mem_mapping_add(&t1000.mapping[pg], 
			0xD0000 + (0x4000 * pg), 16384, 
			ems_read_ram,  ems_read_ramw,  ems_read_raml,
			ems_write_ram, ems_write_ramw, ems_write_raml,
			NULL, MEM_MAPPING_EXTERNAL, 
			&t1000);
		/* Start them all off disabled */
		mem_mapping_disable(&t1000.mapping[pg]);
	}
	
	/* System control functions, and add-on memory board */
	io_sethandler(0xE0, 0x10, read_t1000_ctl, NULL, NULL,
			write_t1000_ctl, NULL, NULL, &t1000);

	machine_common_init(model);
	pit_set_out_func(&pit, 1, pit_refresh_timer_xt);
	device_add(&keyboard_xt_device);
	t1000.fdc = device_add(&fdc_xt_device);
	nmi_init();
	nvr_tc8521_init();
/* No gameport, and no provision to fit one 	device_add(&gameport_device); */
}
