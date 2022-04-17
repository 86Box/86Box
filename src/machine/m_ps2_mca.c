/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of MCA-based PS/2 machines.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2008-2019 Sarah Walker.
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
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/nvr.h>
#include <86box/nvr_ps2.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/mouse.h>
#include <86box/port_6x.h>
#include <86box/port_92.h>
#include <86box/serial.h>
#include <86box/video.h>
#include <86box/machine.h>


static struct
{
        uint8_t adapter_setup;
        uint8_t option[4];
        uint8_t pos_vga;
        uint8_t setup;
        uint8_t sys_ctrl_port_a;
        uint8_t subaddr_lo, subaddr_hi;

        uint8_t memory_bank[8];

        uint8_t io_id;
	uint16_t planar_id;

        mem_mapping_t split_mapping;
        mem_mapping_t expansion_mapping;
	mem_mapping_t cache_mapping;

        uint8_t (*planar_read)(uint16_t port);
        void (*planar_write)(uint16_t port, uint8_t val);

        uint8_t mem_regs[3];

        uint32_t split_addr, split_size;
	uint32_t split_phys;

        uint8_t mem_pos_regs[8];
        uint8_t mem_2mb_pos_regs[8];

	int pending_cache_miss;

	serial_t *uart;
} ps2;

/*The model 70 type 3/4 BIOS performs cache testing. Since 86Box doesn't have any
  proper cache emulation, it's faked a bit here.

  Port E2 is used for cache diagnostics. Bit 7 seems to be set on a cache miss,
  toggling bit 2 seems to clear this. The BIOS performs at least the following
  tests :

  - Disable RAM, access low 64kb (386) / 8kb (486), execute code from cache to
    access low memory and verify that there are no cache misses.
  - Write to low memory using DMA, read low memory and verify that all accesses
    cause cache misses.
  - Read low memory, verify that first access is cache miss. Read again and
    verify that second access is cache hit.

  These tests are also performed on the 486 model 70, despite there being no
  external cache on this system. Port E2 seems to control the internal cache on
  these systems. Presumably this port is connected to KEN#/FLUSH# on the 486.
  This behaviour is required to pass the timer interrupt test on the 486 version
  - the BIOS uses a fixed length loop that will terminate too early on a 486/25
  if it executes from internal cache.

  To handle this, 86Box uses some basic heuristics :
  - If cache is enabled but RAM is disabled, accesses to low memory go directly
    to cache memory.
  - Reads to cache addresses not 'valid' will set the cache miss flag, and mark
    that line as valid.
  - Cache flushes will clear the valid array.
  - DMA via the undocumented PS/2 command 0xb will clear the valid array.
  - Disabling the cache will clear the valid array.
  - Disabling the cache will also mark shadowed ROM areas as using ROM timings.
    This works around the timing loop mentioned above.
*/

static uint8_t ps2_cache[65536];
static int ps2_cache_valid[65536/8];


#ifdef ENABLE_PS2_MCA_LOG
int ps2_mca_do_log = ENABLE_PS2_MCA_LOG;


static void
ps2_mca_log(const char *fmt, ...)
{
    va_list ap;

    if (ps2_mca_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ps2_mca_log(fmt, ...)
#endif


static uint8_t ps2_read_cache_ram(uint32_t addr, void *priv)
{
        ps2_mca_log("ps2_read_cache_ram: addr=%08x %i %04x:%04x\n", addr, ps2_cache_valid[addr >> 3], CS,cpu_state.pc);
        if (!ps2_cache_valid[addr >> 3])
        {
                ps2_cache_valid[addr >> 3] = 1;
                ps2.mem_regs[2] |= 0x80;
        }
        else
                ps2.pending_cache_miss = 0;

        return ps2_cache[addr];
}
static uint16_t ps2_read_cache_ramw(uint32_t addr, void *priv)
{
        ps2_mca_log("ps2_read_cache_ramw: addr=%08x %i %04x:%04x\n", addr, ps2_cache_valid[addr >> 3], CS,cpu_state.pc);
        if (!ps2_cache_valid[addr >> 3])
        {
                ps2_cache_valid[addr >> 3] = 1;
                ps2.mem_regs[2] |= 0x80;
        }
        else
                ps2.pending_cache_miss = 0;

        return *(uint16_t *)&ps2_cache[addr];
}
static uint32_t ps2_read_cache_raml(uint32_t addr, void *priv)
{
        ps2_mca_log("ps2_read_cache_raml: addr=%08x %i %04x:%04x\n", addr, ps2_cache_valid[addr >> 3], CS,cpu_state.pc);
        if (!ps2_cache_valid[addr >> 3])
        {
                ps2_cache_valid[addr >> 3] = 1;
                ps2.mem_regs[2] |= 0x80;
        }
        else
                ps2.pending_cache_miss = 0;

        return *(uint32_t *)&ps2_cache[addr];
}
static void ps2_write_cache_ram(uint32_t addr, uint8_t val, void *priv)
{
        ps2_mca_log("ps2_write_cache_ram: addr=%08x val=%02x %04x:%04x %i\n", addr, val, CS,cpu_state.pc);
        ps2_cache[addr] = val;
}

void ps2_cache_clean(void)
{
        memset(ps2_cache_valid, 0, sizeof(ps2_cache_valid));
}

static uint8_t ps2_read_split_ram(uint32_t addr, void *priv)
{
        addr = (addr % (ps2.split_size << 10)) + ps2.split_phys;
        return mem_read_ram(addr, priv);
}
static uint16_t ps2_read_split_ramw(uint32_t addr, void *priv)
{
        addr = (addr % (ps2.split_size << 10)) + ps2.split_phys;
        return mem_read_ramw(addr, priv);
}
static uint32_t ps2_read_split_raml(uint32_t addr, void *priv)
{
        addr = (addr % (ps2.split_size << 10)) + ps2.split_phys;
        return mem_read_raml(addr, priv);
}
static void ps2_write_split_ram(uint32_t addr, uint8_t val, void *priv)
{
        addr = (addr % (ps2.split_size << 10)) + ps2.split_phys;
        mem_write_ram(addr, val, priv);
}
static void ps2_write_split_ramw(uint32_t addr, uint16_t val, void *priv)
{
        addr = (addr % (ps2.split_size << 10)) + ps2.split_phys;
        mem_write_ramw(addr, val, priv);
}
static void ps2_write_split_raml(uint32_t addr, uint32_t val, void *priv)
{
        addr = (addr % (ps2.split_size << 10)) + ps2.split_phys;
        mem_write_raml(addr, val, priv);
}


#define PS2_SETUP_IO  0x80
#define PS2_SETUP_VGA 0x20

#define PS2_ADAPTER_SETUP 0x08

static uint8_t model_50_read(uint16_t port)
{
        switch (port)
        {
                case 0x100:
                return 0xff;
                case 0x101:
                return 0xfb;
                case 0x102:
                return ps2.option[0];
                case 0x103:
                return ps2.option[1];
                case 0x104:
                return ps2.option[2];
                case 0x105:
                return ps2.option[3];
                case 0x106:
                return ps2.subaddr_lo;
                case 0x107:
                return ps2.subaddr_hi;
        }
        return 0xff;
}

static uint8_t model_55sx_read(uint16_t port)
{
        switch (port)
        {
                case 0x100:
                return 0xff;
                case 0x101:
                return 0xfb;
                case 0x102:
                return ps2.option[0];
                case 0x103:
                return ps2.option[1];
                case 0x104:
                return ps2.memory_bank[ps2.option[3] & 7];
                case 0x105:
                return ps2.option[3];
                case 0x106:
                return ps2.subaddr_lo;
                case 0x107:
                return ps2.subaddr_hi;
        }
        return 0xff;
}

static uint8_t model_70_type3_read(uint16_t port)
{
        switch (port)
        {
                case 0x100:
                return ps2.planar_id & 0xff;
                case 0x101:
                return ps2.planar_id >> 8;
                case 0x102:
                return ps2.option[0];
                case 0x103:
                return ps2.option[1];
                case 0x104:
                return ps2.option[2];
                case 0x105:
                return ps2.option[3];
                case 0x106:
                return ps2.subaddr_lo;
                case 0x107:
                return ps2.subaddr_hi;
        }
        return 0xff;
}

static uint8_t model_80_read(uint16_t port)
{
        switch (port)
        {
                case 0x100:
                return 0xff;
                case 0x101:
                return 0xfd;
                case 0x102:
                return ps2.option[0];
                case 0x103:
                return ps2.option[1];
                case 0x104:
                return ps2.option[2];
                case 0x105:
                return ps2.option[3];
                case 0x106:
                return ps2.subaddr_lo;
                case 0x107:
                return ps2.subaddr_hi;
        }
        return 0xff;
}

static void model_50_write(uint16_t port, uint8_t val)
{
        switch (port)
        {
                case 0x100:
                ps2.io_id = val;
                break;
                case 0x101:
                break;
                case 0x102:
                lpt1_remove();
                serial_remove(ps2.uart);
                if (val & 0x04)
                {
                        if (val & 0x08)
                                serial_setup(ps2.uart, COM1_ADDR, COM1_IRQ);
                        else
                                serial_setup(ps2.uart, COM2_ADDR, COM2_IRQ);
                }
                if (val & 0x10)
                {
                        switch ((val >> 5) & 3)
                        {
                                case 0:
                                lpt1_init(LPT_MDA_ADDR);
                                break;
                                case 1:
                                lpt1_init(LPT1_ADDR);
                                break;
                                case 2:
                                lpt1_init(LPT2_ADDR);
                                break;
                        }
                }
                ps2.option[0] = val;
                break;
                case 0x103:
                ps2.option[1] = val;
                break;
                case 0x104:
                ps2.option[2] = val;
                break;
                case 0x105:
                ps2.option[3] = val;
                break;
                case 0x106:
                ps2.subaddr_lo = val;
                break;
                case 0x107:
                ps2.subaddr_hi = val;
                break;
        }
}


static void model_55sx_mem_recalc(void)
{
	int i, j, state;
#ifdef ENABLE_PS2_MCA_LOG
	int enabled_mem = 0;
#endif
	int base = 0, remap_size = (ps2.option[3] & 0x10) ? 384 : 256;
	int bit_mask = 0x00, max_rows = 4;
	int bank_to_rows[16] = { 4, 2, 1, 0, 0, 2, 1, 0, 0, 0, 0, 0, 0, 2, 1, 0 };

	ps2_mca_log("%02X %02X\n", ps2.option[1], ps2.option[3]);

	mem_remap_top(remap_size);
	mem_set_mem_state(0x00000000, (mem_size + 384) * 1024, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
	mem_set_mem_state(0x000e0000, 0x00020000, MEM_READ_EXTANY | MEM_WRITE_DISABLED);

	for (i = 0; i < 2; i++)
	{
		max_rows = bank_to_rows[(ps2.memory_bank[i] >> 4) & 0x0f];

		if (max_rows == 0)
			continue;

		for (j = 0; j < max_rows; j++)
		{
			if (ps2.memory_bank[i] & (1 << j)) {
				ps2_mca_log("Set memory at %06X-%06X to internal\n", (base * 1024), (base * 1024) + (((base > 0) ? 1024 : 640) * 1024) - 1);
				mem_set_mem_state(base * 1024, ((base > 0) ? 1024 : 640) * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
#ifdef ENABLE_PS2_MCA_LOG
				enabled_mem += 1024;
#endif
				bit_mask |= (1 << (j + (i << 2)));
			}
			base += 1024;
		}
	}

#ifdef ENABLE_PS2_MCA_LOG
	ps2_mca_log("Enabled memory: %i kB (%02X)\n", enabled_mem, bit_mask);
#endif

	if (ps2.option[3] & 0x10)
	{
		/* Enable ROM. */
		ps2_mca_log("Enable ROM\n");
		state = MEM_READ_EXTANY;
	}
	else
	{
		/* Disable ROM. */
		if ((ps2.option[1] & 1) && !(ps2.option[3] & 0x20) && (bit_mask & 0x01))
		{
			/* Disable RAM between 640 kB and 1 MB. */
			ps2_mca_log("Disable ROM, enable RAM\n");
			state = MEM_READ_INTERNAL;
		}
		else
		{
			ps2_mca_log("Disable ROM, disable RAM\n");
			state = MEM_READ_DISABLED;
		}
	}

	/* Write always disabled. */
	state |= MEM_WRITE_DISABLED;

	mem_set_mem_state(0xe0000, 0x20000, state);

	/* if (!(ps2.option[3] & 0x08))
	{
		ps2_mca_log("Memory not yet configured\n");
		return;
	} */

	ps2_mca_log("Enable shadow mapping at %06X-%06X\n", (mem_size * 1024), (mem_size * 1024) + (remap_size * 1024) - 1);

	if ((ps2.option[1] & 1) && !(ps2.option[3] & 0x20) && (bit_mask & 0x01)) {
		ps2_mca_log("Set memory at %06X-%06X to internal\n", (mem_size * 1024), (mem_size * 1024) + (remap_size * 1024) - 1);
		mem_set_mem_state(mem_size * 1024, remap_size * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	}

	flushmmucache_nopc();
}


static void model_55sx_write(uint16_t port, uint8_t val)
{
        switch (port)
        {
                case 0x100:
                ps2.io_id = val;
                break;
                case 0x101:
                break;
                case 0x102:
                lpt1_remove();
                serial_remove(ps2.uart);
                if (val & 0x04)
                {
                        if (val & 0x08)
                                serial_setup(ps2.uart, COM1_ADDR, COM1_IRQ);
                        else
                                serial_setup(ps2.uart, COM2_ADDR, COM2_IRQ);
                }
                if (val & 0x10)
                {
                        switch ((val >> 5) & 3)
                        {
                                case 0:
                                lpt1_init(LPT_MDA_ADDR);
                                break;
                                case 1:
                                lpt1_init(LPT1_ADDR);
                                break;
                                case 2:
                                lpt1_init(LPT2_ADDR);
                                break;
                        }
                }
                ps2.option[0] = val;
                break;
                case 0x103:
                ps2_mca_log("Write POS1: %02X\n", val);
                ps2.option[1] = val;
		model_55sx_mem_recalc();
		break;
                case 0x104:
                ps2.memory_bank[ps2.option[3] & 7] &= ~0xf;
                ps2.memory_bank[ps2.option[3] & 7] |= (val & 0xf);
                ps2_mca_log("Write memory bank %i: %02X\n", ps2.option[3] & 7, val);
		model_55sx_mem_recalc();
                break;
                case 0x105:
                ps2_mca_log("Write POS3: %02X\n", val);
                ps2.option[3] = val;
		model_55sx_mem_recalc();
                break;
                case 0x106:
                ps2.subaddr_lo = val;
                break;
                case 0x107:
                ps2.subaddr_hi = val;
                break;
        }
}

static void model_70_type3_write(uint16_t port, uint8_t val)
{
        switch (port)
        {
                case 0x100:
                break;
                case 0x101:
                break;
                case 0x102:
                lpt1_remove();
                serial_remove(ps2.uart);
                if (val & 0x04)
                {
                        if (val & 0x08)
                                serial_setup(ps2.uart, COM1_ADDR, COM1_IRQ);
                        else
                                serial_setup(ps2.uart, COM2_ADDR, COM2_IRQ);
                }
                if (val & 0x10)
                {
                        switch ((val >> 5) & 3)
                        {
                                case 0:
                                lpt1_init(LPT_MDA_ADDR);
                                break;
                                case 1:
                                lpt1_init(LPT1_ADDR);
                                break;
                                case 2:
                                lpt1_init(LPT2_ADDR);
                                break;
                        }
                }
                ps2.option[0] = val;
                break;
                case 0x103:
				if (ps2.planar_id == 0xfff9)
					ps2.option[1] = (ps2.option[1] & 0x0f) | (val & 0xf0);
                break;
                case 0x104:
				if (ps2.planar_id == 0xfff9)
					ps2.option[2] = val;
                break;
                case 0x105:
                ps2.option[3] = val;
                break;
                case 0x106:
                ps2.subaddr_lo = val;
                break;
                case 0x107:
                ps2.subaddr_hi = val;
                break;
        }
}


static void model_80_write(uint16_t port, uint8_t val)
{
        switch (port)
        {
                case 0x100:
                break;
                case 0x101:
                break;
                case 0x102:
                lpt1_remove();
                serial_remove(ps2.uart);
                if (val & 0x04)
                {
                        if (val & 0x08)
                                serial_setup(ps2.uart, COM1_ADDR, COM1_IRQ);
                        else
                                serial_setup(ps2.uart, COM2_ADDR, COM2_IRQ);
                }
                if (val & 0x10)
                {
                        switch ((val >> 5) & 3)
                        {
                                case 0:
                                lpt1_init(LPT_MDA_ADDR);
                                break;
                                case 1:
                                lpt1_init(LPT1_ADDR);
                                break;
                                case 2:
                                lpt1_init(LPT2_ADDR);
                                break;
                        }
                }
                ps2.option[0] = val;
                break;
                case 0x103:
                ps2.option[1] = (ps2.option[1] & 0x0f) | (val & 0xf0);
                break;
                case 0x104:
                ps2.option[2] = val;
                break;
                case 0x105:
                ps2.option[3] = val;
                break;
                case 0x106:
                ps2.subaddr_lo = val;
                break;
                case 0x107:
                ps2.subaddr_hi = val;
                break;
        }
}

uint8_t ps2_mca_read(uint16_t port, void *p)
{
        uint8_t temp;

        switch (port)
        {
                case 0x91:
                // fatal("Read 91 setup=%02x adapter=%02x\n", ps2.setup, ps2.adapter_setup);
                if (!(ps2.setup & PS2_SETUP_IO))
                        temp = 0x00;
                else if (!(ps2.setup & PS2_SETUP_VGA))
                        temp = 0x00;
                else if (ps2.adapter_setup & PS2_ADAPTER_SETUP)
                        temp = 0x00;
                else
                        temp = !mca_feedb();
		temp |= 0xfe;
		break;
                case 0x94:
                temp = ps2.setup;
                break;
                case 0x96:
                temp = ps2.adapter_setup | 0x70;
                break;
                case 0x100:
                if (!(ps2.setup & PS2_SETUP_IO))
                        temp = ps2.planar_read(port);
                else if (!(ps2.setup & PS2_SETUP_VGA))
                        temp = 0xfd;
                else if (ps2.adapter_setup & PS2_ADAPTER_SETUP)
                        temp = mca_read(port);
                else
                        temp = 0xff;
                break;
                case 0x101:
                if (!(ps2.setup & PS2_SETUP_IO))
                        temp = ps2.planar_read(port);
                else if (!(ps2.setup & PS2_SETUP_VGA))
                        temp = 0xef;
                else if (ps2.adapter_setup & PS2_ADAPTER_SETUP)
                        temp = mca_read(port);
                else
                        temp = 0xff;
                break;
                case 0x102:
                if (!(ps2.setup & PS2_SETUP_IO))
                        temp = ps2.planar_read(port);
                else if (!(ps2.setup & PS2_SETUP_VGA))
                        temp = ps2.pos_vga;
				else if (ps2.adapter_setup & PS2_ADAPTER_SETUP)
                        temp = mca_read(port);
                else
                        temp = 0xff;
                break;
                case 0x103:
                if (!(ps2.setup & PS2_SETUP_IO))
                        temp = ps2.planar_read(port);
                else if ((ps2.setup & PS2_SETUP_VGA) && (ps2.adapter_setup & PS2_ADAPTER_SETUP))
                        temp = mca_read(port);
                else
                        temp = 0xff;
                break;
                case 0x104:
                if (!(ps2.setup & PS2_SETUP_IO))
                        temp = ps2.planar_read(port);
                else if ((ps2.setup & PS2_SETUP_VGA) && (ps2.adapter_setup & PS2_ADAPTER_SETUP))
                        temp = mca_read(port);
                else
                        temp = 0xff;
                break;
                case 0x105:
                if (!(ps2.setup & PS2_SETUP_IO))
                        temp = ps2.planar_read(port);
                else if ((ps2.setup & PS2_SETUP_VGA) && (ps2.adapter_setup & PS2_ADAPTER_SETUP))
                        temp = mca_read(port);
                else
                        temp = 0xff;
                break;
                case 0x106:
                if (!(ps2.setup & PS2_SETUP_IO))
                        temp = ps2.planar_read(port);
                else if ((ps2.setup & PS2_SETUP_VGA) && (ps2.adapter_setup & PS2_ADAPTER_SETUP))
                        temp = mca_read(port);
                else
                        temp = 0xff;
                break;
                case 0x107:
                if (!(ps2.setup & PS2_SETUP_IO))
                        temp = ps2.planar_read(port);
                else if ((ps2.setup & PS2_SETUP_VGA) && (ps2.adapter_setup & PS2_ADAPTER_SETUP))
                        temp = mca_read(port);
                else
                        temp = 0xff;
                break;

                default:
                temp = 0xff;
                break;
        }

        ps2_mca_log("ps2_read: port=%04x temp=%02x\n", port, temp);

       return temp;
}

static void ps2_mca_write(uint16_t port, uint8_t val, void *p)
{
        ps2_mca_log("ps2_write: port=%04x val=%02x %04x:%04x\n", port, val, CS,cpu_state.pc);

        switch (port)
        {
                case 0x94:
                ps2.setup = val;
                break;
                case 0x96:
                if ((val & 0x80) && !(ps2.adapter_setup & 0x80))
                        mca_reset();
                ps2.adapter_setup = val;
                mca_set_index(val & 7);
                break;
                case 0x100:
                if (!(ps2.setup & PS2_SETUP_IO))
                        ps2.planar_write(port, val);
                else if ((ps2.setup & PS2_SETUP_VGA) && (ps2.adapter_setup & PS2_ADAPTER_SETUP))
                        mca_write(port, val);
                break;
                case 0x101:
                if (!(ps2.setup & PS2_SETUP_IO))
                        ps2.planar_write(port, val);
                else if ((ps2.setup & PS2_SETUP_VGA) && (ps2.setup & PS2_SETUP_VGA) && (ps2.adapter_setup & PS2_ADAPTER_SETUP))
                        mca_write(port, val);
                break;
                case 0x102:
                if (!(ps2.setup & PS2_SETUP_IO))
                        ps2.planar_write(port, val);
                else if (!(ps2.setup & PS2_SETUP_VGA))
                        ps2.pos_vga = val;
                else if (ps2.adapter_setup & PS2_ADAPTER_SETUP)
                        mca_write(port, val);
                break;
                case 0x103:
                if (!(ps2.setup & PS2_SETUP_IO))
                        ps2.planar_write(port, val);
                else if (ps2.adapter_setup & PS2_ADAPTER_SETUP)
                        mca_write(port, val);
                break;
                case 0x104:
                if (!(ps2.setup & PS2_SETUP_IO))
                        ps2.planar_write(port, val);
                else if (ps2.adapter_setup & PS2_ADAPTER_SETUP)
                        mca_write(port, val);
                break;
                case 0x105:
                if (!(ps2.setup & PS2_SETUP_IO))
                        ps2.planar_write(port, val);
                else if (ps2.adapter_setup & PS2_ADAPTER_SETUP)
                        mca_write(port, val);
                break;
                case 0x106:
                if (!(ps2.setup & PS2_SETUP_IO))
                        ps2.planar_write(port, val);
                else if (ps2.adapter_setup & PS2_ADAPTER_SETUP)
                        mca_write(port, val);
                break;
                case 0x107:
                if (!(ps2.setup & PS2_SETUP_IO))
                        ps2.planar_write(port, val);
                else if (ps2.adapter_setup & PS2_ADAPTER_SETUP)
                        mca_write(port, val);
                break;
        }
}

static void ps2_mca_board_common_init()
{
        io_sethandler(0x0091, 0x0001, ps2_mca_read, NULL, NULL, ps2_mca_write, NULL, NULL, NULL);
        io_sethandler(0x0094, 0x0001, ps2_mca_read, NULL, NULL, ps2_mca_write, NULL, NULL, NULL);
        io_sethandler(0x0096, 0x0001, ps2_mca_read, NULL, NULL, ps2_mca_write, NULL, NULL, NULL);
        io_sethandler(0x0100, 0x0008, ps2_mca_read, NULL, NULL, ps2_mca_write, NULL, NULL, NULL);

	device_add(&port_6x_ps2_device);
	device_add(&port_92_device);

        ps2.setup = 0xff;

        lpt1_init(LPT_MDA_ADDR);
}

static uint8_t ps2_mem_expansion_read(int port, void *p)
{
        return ps2.mem_pos_regs[port & 7];
}

static void ps2_mem_expansion_write(int port, uint8_t val, void *p)
{
        if (port < 0x102 || port == 0x104)
                return;

        ps2.mem_pos_regs[port & 7] = val;

        if (ps2.mem_pos_regs[2] & 1)
                mem_mapping_enable(&ps2.expansion_mapping);
        else
                mem_mapping_disable(&ps2.expansion_mapping);
}

static uint8_t ps2_mem_expansion_feedb(void *p)
{
	return (ps2.mem_pos_regs[2] & 1);
}

static void ps2_mca_mem_fffc_init(int start_mb)
{
	uint32_t planar_size, expansion_start;

	planar_size = (start_mb - 1) << 20;
	expansion_start = start_mb << 20;

	mem_mapping_set_addr(&ram_high_mapping, 0x100000, planar_size);

	ps2.mem_pos_regs[0] = 0xff;
	ps2.mem_pos_regs[1] = 0xfc;

	switch ((mem_size / 1024) - start_mb)
	{
		case 1:
			ps2.mem_pos_regs[4] = 0xfc;	/* 11 11 11 00 = 0 0 0 1 */
			break;
		case 2:
			ps2.mem_pos_regs[4] = 0xfe;	/* 11 11 11 10 = 0 0 0 2 */
			break;
		case 3:
			ps2.mem_pos_regs[4] = 0xf2;	/* 11 11 00 10 = 0 0 1 2 */
			break;
		case 4:
			ps2.mem_pos_regs[4] = 0xfa;	/* 11 11 10 10 = 0 0 2 2 */
			break;
		case 5:
			ps2.mem_pos_regs[4] = 0xca;	/* 11 00 10 10 = 0 1 2 2 */
			break;
		case 6:
			ps2.mem_pos_regs[4] = 0xea;	/* 11 10 10 10 = 0 2 2 2 */
			break;
		case 7:
			ps2.mem_pos_regs[4] = 0x2a;	/* 00 10 10 10 = 1 2 2 2 */
			break;
		case 8:
			ps2.mem_pos_regs[4] = 0xaa;	/* 10 10 10 10 = 2 2 2 2 */
			break;
	}

	mca_add(ps2_mem_expansion_read, ps2_mem_expansion_write, ps2_mem_expansion_feedb, NULL, NULL);
	mem_mapping_add(&ps2.expansion_mapping,
			expansion_start,
			(mem_size - (start_mb << 10)) << 10,
			mem_read_ram,
			mem_read_ramw,
			mem_read_raml,
			mem_write_ram,
			mem_write_ramw,
			mem_write_raml,
			&ram[expansion_start],
			MEM_MAPPING_INTERNAL,
			NULL);
	mem_mapping_disable(&ps2.expansion_mapping);
}

static void ps2_mca_mem_d071_init(int start_mb)
{
	uint32_t planar_size, expansion_start;

	planar_size = (start_mb - 1) << 20;
	expansion_start = start_mb << 20;

	mem_mapping_set_addr(&ram_high_mapping, 0x100000, planar_size);

	ps2.mem_pos_regs[0] = 0xd0;
	ps2.mem_pos_regs[1] = 0x71;
	ps2.mem_pos_regs[4] = (mem_size / 1024) - start_mb;

	mca_add(ps2_mem_expansion_read, ps2_mem_expansion_write, ps2_mem_expansion_feedb, NULL, NULL);
	mem_mapping_add(&ps2.expansion_mapping,
			expansion_start,
			(mem_size - (start_mb << 10)) << 10,
			mem_read_ram,
			mem_read_ramw,
			mem_read_raml,
			mem_write_ram,
			mem_write_ramw,
			mem_write_raml,
			&ram[expansion_start],
			MEM_MAPPING_INTERNAL,
			NULL);
	mem_mapping_disable(&ps2.expansion_mapping);
}


static void ps2_mca_board_model_50_init()
{
        ps2_mca_board_common_init();

        mem_remap_top(384);
        mca_init(4);
	device_add(&keyboard_ps2_mca_2_device);

        ps2.planar_read = model_50_read;
        ps2.planar_write = model_50_write;

        if (mem_size > 2048)
        {
                /* Only 2 MB supported on planar, create a memory expansion card for the rest */
		ps2_mca_mem_fffc_init(2);
        }

	if (gfxcard == VID_INTERNAL)
		device_add(&ps1vga_mca_device);
}

static void ps2_mca_board_model_55sx_init()
{
        ps2_mca_board_common_init();

	ps2.option[1] = 0x00;
	ps2.option[2] = 0x00;
        ps2.option[3] = 0x10;

        memset(ps2.memory_bank, 0xf0, 8);
        switch (mem_size/1024)
        {
                case 1:
                ps2.memory_bank[0] = 0x61;
                break;
                case 2:
                ps2.memory_bank[0] = 0x51;
                break;
                case 3:
                ps2.memory_bank[0] = 0x51;
                ps2.memory_bank[1] = 0x61;
                break;
                case 4:
                ps2.memory_bank[0] = 0x51;
                ps2.memory_bank[1] = 0x51;
                break;
                case 5:
                ps2.memory_bank[0] = 0x01;
                ps2.memory_bank[1] = 0x61;
                break;
                case 6:
                ps2.memory_bank[0] = 0x01;
                ps2.memory_bank[1] = 0x51;
                break;
                case 7: /*Not supported*/
                ps2.memory_bank[0] = 0x01;
                ps2.memory_bank[1] = 0x51;
                break;
                case 8:
                ps2.memory_bank[0] = 0x01;
                ps2.memory_bank[1] = 0x01;
                break;
        }

        mca_init(4);
	device_add(&keyboard_ps2_mca_device);

        ps2.planar_read = model_55sx_read;
        ps2.planar_write = model_55sx_write;

	if (gfxcard == VID_INTERNAL)
		device_add(&ps1vga_mca_device);

	model_55sx_mem_recalc();
}

static void mem_encoding_update(void)
{
	mem_mapping_disable(&ps2.split_mapping);

	if (ps2.split_size > 0)
		mem_set_mem_state(ps2.split_addr, ps2.split_size << 10, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
	if (((mem_size << 10) - (1 << 20)) > 0)
		mem_set_mem_state(1 << 20, (mem_size << 10) - (1 << 20), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

        ps2.split_addr = ((uint32_t) (ps2.mem_regs[0] & 0xf)) << 20;
		if (!ps2.split_addr)
			ps2.split_addr = 1 << 20;

        if (ps2.mem_regs[1] & 2) {
                mem_set_mem_state(0xe0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
		ps2_mca_log("PS/2 Model 80-111: ROM space enabled\n");
        } else {
                mem_set_mem_state(0xe0000, 0x20000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
		ps2_mca_log("PS/2 Model 80-111: ROM space disabled\n");
	}

	if (ps2.mem_regs[1] & 4) {
		mem_mapping_set_addr(&ram_low_mapping, 0x00000, 0x80000);
		ps2_mca_log("PS/2 Model 80-111: 00080000- 0009FFFF disabled\n");
	} else {
		mem_mapping_set_addr(&ram_low_mapping, 0x00000, 0xa0000);
		ps2_mca_log("PS/2 Model 80-111: 00080000- 0009FFFF enabled\n");
	}

        if (!(ps2.mem_regs[1] & 8))
        {
		if (ps2.mem_regs[1] & 4) {
			ps2.split_size = 384;
			ps2.split_phys = 0x80000;
		} else {
			ps2.split_size = 256;
			ps2.split_phys = 0xa0000;
		}

		mem_set_mem_state(ps2.split_addr, ps2.split_size << 10, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		mem_mapping_set_exec(&ps2.split_mapping, &ram[ps2.split_phys]);
		mem_mapping_set_addr(&ps2.split_mapping, ps2.split_addr, ps2.split_size << 10);

		ps2_mca_log("PS/2 Model 80-111: Split memory block enabled at %08X\n", ps2.split_addr);
        } else {
		ps2.split_size = 0;
		ps2_mca_log("PS/2 Model 80-111: Split memory block disabled\n");
	}

	flushmmucache_nopc();
}

static uint8_t mem_encoding_read(uint16_t addr, void *p)
{
        switch (addr)
        {
                case 0xe0:
                return ps2.mem_regs[0];
                case 0xe1:
                return ps2.mem_regs[1];
        }
        return 0xff;
}
static void mem_encoding_write(uint16_t addr, uint8_t val, void *p)
{
        switch (addr)
        {
                case 0xe0:
                ps2.mem_regs[0] = val;
                break;
                case 0xe1:
                ps2.mem_regs[1] = val;
                break;
        }
        mem_encoding_update();
}

static uint8_t mem_encoding_read_cached(uint16_t addr, void *p)
{
        switch (addr)
        {
                case 0xe0:
                return ps2.mem_regs[0];
                case 0xe1:
                return ps2.mem_regs[1];
                case 0xe2:
                return ps2.mem_regs[2];
        }
        return 0xff;
}

static void mem_encoding_write_cached(uint16_t addr, uint8_t val, void *p)
{
        uint8_t old;

        switch (addr)
        {
		case 0xe0:
		ps2.mem_regs[0] = val;
		break;
		case 0xe1:
		ps2.mem_regs[1] = val;
                break;
                case 0xe2:
                old = ps2.mem_regs[2];
                ps2.mem_regs[2] = (ps2.mem_regs[2] & 0x80) | (val & ~0x88);
                if (val & 2)
                {
                        ps2_mca_log("Clear latch - %i\n", ps2.pending_cache_miss);
                        if (ps2.pending_cache_miss)
                                ps2.mem_regs[2] |=  0x80;
                        else
                                ps2.mem_regs[2] &= ~0x80;
                        ps2.pending_cache_miss = 0;
                }

                if ((val & 0x21) == 0x20 && (old & 0x21) != 0x20)
                        ps2.pending_cache_miss = 1;
                if ((val & 0x21) == 0x01 && (old & 0x21) != 0x01)
                        ps2_cache_clean();
#if 1
 // FIXME: Look into this!!!
                if (val & 0x01)
                        ram_mid_mapping.flags |= MEM_MAPPING_ROM_WS;
                else
                        ram_mid_mapping.flags &= ~MEM_MAPPING_ROM_WS;
#endif
                break;
        }
        ps2_mca_log("mem_encoding_write: addr=%02x val=%02x %04x:%04x  %02x %02x\n", addr, val, CS,cpu_state.pc, ps2.mem_regs[1],ps2.mem_regs[2]);
        mem_encoding_update();
        if ((ps2.mem_regs[1] & 0x10) && (ps2.mem_regs[2] & 0x21) == 0x20)
        {
                mem_mapping_disable(&ram_low_mapping);
                mem_mapping_enable(&ps2.cache_mapping);
                flushmmucache();
        }
        else
        {
                mem_mapping_disable(&ps2.cache_mapping);
                mem_mapping_enable(&ram_low_mapping);
                flushmmucache();
        }
}

static void ps2_mca_board_model_70_type34_init(int is_type4, int slots)
{
        ps2_mca_board_common_init();

        ps2.split_addr = mem_size * 1024;
        mca_init(slots);
	device_add(&keyboard_ps2_mca_device);

        ps2.planar_read = model_70_type3_read;
        ps2.planar_write = model_70_type3_write;

        device_add(&ps2_nvr_device);

        io_sethandler(0x00e0, 0x0003, mem_encoding_read_cached, NULL, NULL, mem_encoding_write_cached, NULL, NULL, NULL);

        ps2.mem_regs[1] = 2;

        switch (mem_size/1024)
        {
                case 2:
                ps2.option[1] = 0xa6;
                ps2.option[2] = 0x01;
                break;
                case 4:
                ps2.option[1] = 0xaa;
                ps2.option[2] = 0x01;
                break;
                case 6:
                ps2.option[1] = 0xca;
                ps2.option[2] = 0x01;
                break;
                case 8:
                default:
                ps2.option[1] = 0xca;
                ps2.option[2] = 0x02;
                break;
        }

        if (is_type4)
                ps2.option[2] |= 0x04; /*486 CPU*/

        mem_mapping_add(&ps2.split_mapping,
                    (mem_size+256) * 1024,
                    256*1024,
                    ps2_read_split_ram,
                    ps2_read_split_ramw,
                    ps2_read_split_raml,
                    ps2_write_split_ram,
                    ps2_write_split_ramw,
                    ps2_write_split_raml,
                    &ram[0xa0000],
                    MEM_MAPPING_INTERNAL,
                    NULL);
        mem_mapping_disable(&ps2.split_mapping);

        mem_mapping_add(&ps2.cache_mapping,
                    0,
                    (is_type4) ? (8 * 1024) : (64 * 1024),
                    ps2_read_cache_ram,
                    ps2_read_cache_ramw,
                    ps2_read_cache_raml,
                    ps2_write_cache_ram,
                    NULL,
                    NULL,
                    ps2_cache,
                    MEM_MAPPING_INTERNAL,
                    NULL);
        mem_mapping_disable(&ps2.cache_mapping);

		if (ps2.planar_id == 0xfff9) {
			if (mem_size > 4096)
			{
				/* Only 4 MB supported on planar, create a memory expansion card for the rest */
				if (mem_size > 12288) {
					ps2_mca_mem_d071_init(4);
				} else {
					ps2_mca_mem_fffc_init(4);
				}
			}
		} else {
			if (mem_size > 8192)
			{
				/* Only 8 MB supported on planar, create a memory expansion card for the rest */
				if (mem_size > 16384)
					ps2_mca_mem_d071_init(8);
				else {
					ps2_mca_mem_fffc_init(8);
				}
			}
		}

	if (gfxcard == VID_INTERNAL)
		device_add(&ps1vga_mca_device);
}

static void ps2_mca_board_model_80_type2_init(int is486)
{
        ps2_mca_board_common_init();

        ps2.split_addr = mem_size * 1024;
        mca_init(8);
	device_add(&keyboard_ps2_mca_device);

        ps2.planar_read = model_80_read;
        ps2.planar_write = model_80_write;

        device_add(&ps2_nvr_device);

        io_sethandler(0x00e0, 0x0002, mem_encoding_read, NULL, NULL, mem_encoding_write, NULL, NULL, NULL);

        ps2.mem_regs[1] = 2;

	/* Note by Kotori: I rewrote this because the original code was using
	   Model 80 Type 1-style 1 MB memory card settings, which are *NOT*
	   supported by Model 80 Type 2. */
        switch (mem_size/1024)
        {
                case 1:
                ps2.option[1] = 0x0e;	/* 11 10 = 0 2 */
		ps2.mem_regs[1] = 0xd2;	/* 01 = 1 (first) */
		ps2.mem_regs[0] = 0xf0;	/* 11 = invalid */
                break;
                case 2:
                ps2.option[1] = 0x0e;	/* 11 10 = 0 2 */
		ps2.mem_regs[1] = 0xc2;	/* 00 = 2 */
		ps2.mem_regs[0] = 0xf0;	/* 11 = invalid */
                break;
                case 3:
                ps2.option[1] = 0x0a;	/* 10 10 = 2 2 */
		ps2.mem_regs[1] = 0xc2;	/* 00 = 2 */
		ps2.mem_regs[0] = 0xd0;	/* 01 = 1 (first) */
                break;
                case 4:
                default:
                ps2.option[1] = 0x0a;	/* 10 10 = 2 2 */
		ps2.mem_regs[1] = 0xc2;	/* 00 = 2 */
		ps2.mem_regs[0] = 0xc0;	/* 00 = 2 */
                break;
        }

	ps2.mem_regs[0] |= ((mem_size/1024) & 0x0f);

        mem_mapping_add(&ps2.split_mapping,
                    (mem_size+256) * 1024,
                    256*1024,
                    ps2_read_split_ram,
                    ps2_read_split_ramw,
                    ps2_read_split_raml,
                    ps2_write_split_ram,
                    ps2_write_split_ramw,
                    ps2_write_split_raml,
                    &ram[0xa0000],
                    MEM_MAPPING_INTERNAL,
                    NULL);
        mem_mapping_disable(&ps2.split_mapping);

        if ((mem_size > 4096) && !is486)
        {
			/* Only 4 MB supported on planar, create a memory expansion card for the rest */
			if (mem_size > 12288)
				ps2_mca_mem_d071_init(4);
			else {
				ps2_mca_mem_fffc_init(4);
			}
        }

	if (gfxcard == VID_INTERNAL)
		device_add(&ps1vga_mca_device);

	ps2.split_size = 0;
}


static void
machine_ps2_common_init(const machine_t *model)
{
        machine_common_init(model);

        if (fdc_type == FDC_INTERNAL)
	device_add(&fdc_at_device);

        dma16_init();
        ps2_dma_init();
        device_add(&ps_no_nmi_nvr_device);
        pic2_init();

        pit_ps2_init();

	nmi_mask = 0x80;

	ps2.uart = device_add_inst(&ns16550_device, 1);
}


int
machine_ps2_model_50_init(const machine_t *model)
{
	int ret;

	ret = bios_load_interleaved("roms/machines/ibmps2_m50/90x7420.zm13",
				    "roms/machines/ibmps2_m50/90x7429.zm18",
				    0x000f0000, 131072, 0);
	ret &= bios_load_aux_interleaved("roms/machines/ibmps2_m50/90x7423.zm14",
					 "roms/machines/ibmps2_m50/90x7426.zm16",
					 0x000e0000, 65536, 0);

	if (bios_only || !ret)
		return ret;

        machine_ps2_common_init(model);

        ps2_mca_board_model_50_init();

	return ret;
}


int
machine_ps2_model_55sx_init(const machine_t *model)
{
	int ret;

	ret = bios_load_interleaved("roms/machines/ibmps2_m55sx/33f8146.zm41",
				    "roms/machines/ibmps2_m55sx/33f8145.zm40",
				    0x000e0000, 131072, 0);

	if (bios_only || !ret)
		return ret;

        machine_ps2_common_init(model);

        ps2_mca_board_model_55sx_init();

	return ret;
}


int
machine_ps2_model_70_type3_init(const machine_t *model)
{
	int ret;

	ret = bios_load_interleaved("roms/machines/ibmps2_m70_type3/70-a_even.bin",
				    "roms/machines/ibmps2_m70_type3/70-a_odd.bin",
				    0x000e0000, 131072, 0);

	if (bios_only || !ret)
		return ret;

        machine_ps2_common_init(model);

		ps2.planar_id = 0xf9ff;

        ps2_mca_board_model_70_type34_init(0, 4);

	return ret;
}


int
machine_ps2_model_80_init(const machine_t *model)
{
	int ret;

	ret = bios_load_interleaved("roms/machines/ibmps2_m80/15f6637.bin",
				    "roms/machines/ibmps2_m80/15f6639.bin",
				    0x000e0000, 131072, 0);

	if (bios_only || !ret)
		return ret;

        machine_ps2_common_init(model);

        ps2_mca_board_model_80_type2_init(0);

	return ret;
}

int
machine_ps2_model_80_axx_init(const machine_t *model)
{
	int ret;

	ret = bios_load_interleaved("roms/machines/ibmps2_m80/64f4356.bin",
				    "roms/machines/ibmps2_m80/64f4355.bin",
				    0x000e0000, 131072, 0);

	if (bios_only || !ret)
		return ret;

        machine_ps2_common_init(model);

		ps2.planar_id = 0xfff9;

        ps2_mca_board_model_70_type34_init(0, 8);

	return ret;
}
