/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ALi M1429 & M1429G chipset.
 *
 *      Note: This chipset has no datasheet, everything were done via
 *      reverse engineering the BIOS of various machines using it.
 *
 *      Authors: Tiseno100
 *
 *		Copyright 2021 Tiseno100
 *
 */

/*
    ALi M1429/M1429G Configuration Registers

    Notes: Incorporated sometimes with a M1435 PCI-to-VLB Bridge
           M1429G is just a 1429 with Green Functionality
           SMM in it's entirety needs more research

    Warning: Register documentation may be inaccurate!

    Register 03h: Write C5h to unlock the configuration registers

    Register 10h & 11h: DRAM Bank Configuration

    Register 12h:
    Bit 2: Memory Remapping Enable (128KB)

    Register 13h:
    Bit 7: Shadow RAM Enable for F8000-FFFFF
    Bit 6: Shadow RAM Enable for F0000-F7FFF
    Bit 5: Shadow RAM Enable for E8000-FFFFF
    Bit 4: Shadow RAM Enable for E0000-F7FFF
    Bit 3: Shadow RAM Enable for D8000-FFFFF
    Bit 2: Shadow RAM Enable for D0000-F7FFF
    Bit 1: Shadow RAM Enable for C8000-FFFFF
    Bit 0: Shadow RAM Enable for C0000-F7FFF

    Register 14h:
    Bit 1: Shadow RAM Write for Enabled Segments
    Bit 0: Shadow RAM Read for Enabled Segments

    Register 18h:
    Bit 6-5-4 (Cache Size)
        0 0 0 32KB
	0 0 1 128KB
	0 1 0 256KB
	0 1 1 512KB
	1 0 0 64KB
	1 0 1 256KB
	1 1 0 512KB
	1 1 1 1MB

    Bit 1: L2 Cache Enable

    Register 20h:
    Bits 2-1-0: Bus Clock Speed
         0 0 0: 7.1519Mhz (ATCLK2)
	 0 0 1: CLK2IN/4
	 0 1 0: CLK2IN/5
	 0 1 1: CLK2IN/6
	 1 0 0: CLK2IN/8
	 1 0 1: CLK2IN/10
	 1 1 0: CLK2IN/12

*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>

#include <86box/mem.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

#define GREEN dev->is_g /* Is G Variant */
#define UNLOCKED !dev->cfg_locked /* Is Unlocked (Write C5h on register 03h) */
#define ENABLE_ALI1429_LOG 1
#ifdef ENABLE_ALI1429_LOG
int ali1429_do_log = ENABLE_ALI1429_LOG;
static void
ali1429_log(const char *fmt, ...)
{
	va_list ap;

	if (ali1429_do_log)
	{
		va_start(ap, fmt);
		pclog_ex(fmt, ap);
		va_end(ap);
	}
}
#else
#define ali1429_log(fmt, ...)
#endif

typedef struct
{
	uint8_t index,
	regs[90],	/* Proper Configuration Registers */
	reg_57h;	/* M1429G Configuration Register 57h */

	int cfg_locked; /* Configuration Lock */
	int is_g;	/* Is G Variant */
} ali1429_t;

static void ali1429_defaults(ali1429_t *dev)
{
/* M1429 Defaults */
dev->regs[0x10] = 0xf0;
dev->regs[0x11] = 0xff;
dev->regs[0x12] = 0x10;
dev->regs[0x14] = 0x48;
dev->regs[0x15] = 0x40;
dev->regs[0x17] = 0x7a;
dev->regs[0x1a] = 0x80;
dev->regs[0x22] = 0x80;
dev->regs[0x23] = 0x57;
dev->regs[0x25] = 0xc0;
dev->regs[0x27] = 0x30;

/* M1429G Default Registers */
if(GREEN)
{
dev->regs[0x31] = 0x88;
dev->regs[0x32] = 0xc0;
dev->regs[0x38] = 0xe5;
dev->regs[0x40] = 0xe3;
dev->regs[0x41] = 2;
dev->regs[0x45] = 0x80;
}
}

static void ali1429_shadow(ali1429_t *dev)
{
	uint16_t can_write = (dev->regs[0x14] & 2) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
	uint16_t can_read = (dev->regs[0x14] & 1) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;

	for (int i = 0; i < 8; i++)
	{
		if (dev->regs[0x13] & (1 << i))
			mem_set_mem_state_both(0xc0000 + (i << 15), 0x8000, can_read | can_write);
		else
			mem_set_mem_state_both(0xc0000 + (i << 15), 0x8000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
	}

	flushmmucache_nopc();
}

static void
ali1429_write(uint16_t addr, uint8_t val, void *priv)
{
	ali1429_t *dev = (ali1429_t *)priv;

	switch (addr)
	{
	case 0x22:
		dev->index = val;
		break;

	case 0x23:
		if (dev->index == 0x03)
			dev->cfg_locked = !(val == 0xc5);
		else if(dev->index <= 0x57)
			ali1429_log("M1429%s: dev->regs[%02x] = %02x POST: %02x\n", GREEN ? "G" : "", dev->index, val, inb(0x80));

		if (!dev->cfg_locked)
		{
			/* Common M1429 Registers */
			switch (dev->index)
			{
				case 0x10:
				case 0x11:
					dev->regs[dev->index] = val;
					break;

				case 0x12:
					dev->regs[dev->index] = val;

					if(val & 4)
					mem_remap_top(128);
					else
					mem_remap_top(0);

					break;

				case 0x13:
				case 0x14:
					dev->regs[dev->index] = val;
					ali1429_shadow(dev);
					break;

				case 0x15:
				case 0x16:
				case 0x17:
					dev->regs[dev->index] = val;
					break;

				case 0x18:
					dev->regs[dev->index] = (val & 0x8f) | 0x20;
					cpu_cache_ext_enabled = !!(val & 2);
					cpu_update_waitstates();
					break;

				case 0x19:
				case 0x1a:
				case 0x1e:
					dev->regs[dev->index] = val;
					break;

				case 0x20:
					dev->regs[dev->index] = val;

					switch(val & 7)
					{
						case 0:
						case 7: /* Illegal */
						cpu_set_isa_speed(7159091);
						break;

						case 1:
						cpu_set_isa_speed(cpu_busspeed / 4);
						break;

						case 2:
						cpu_set_isa_speed(cpu_busspeed / 5);
						break;

						case 3:
						cpu_set_isa_speed(cpu_busspeed / 6);
						break;

						case 4:
						cpu_set_isa_speed(cpu_busspeed / 8);
						break;

						case 5:
						cpu_set_isa_speed(cpu_busspeed / 10);
						break;

						case 6:
						cpu_set_isa_speed(cpu_busspeed / 12);
						break;
					}
					break;

				case 0x21:
				case 0x22:
				case 0x23:
				case 0x24:
				case 0x25:
				case 0x26:
				case 0x27:
					dev->regs[dev->index] = val;
					break;
			}

			/* M1429G Only Registers */
			if (GREEN)
			{
				switch (dev->index)
				{
					case 0x30:
					case 0x31:
					case 0x32:
					case 0x33:
					case 0x34:
					case 0x35:
					case 0x36:
					case 0x37:
					case 0x38:
					case 0x39:
					case 0x3a:
					case 0x3b:
					case 0x3c:
					case 0x3e:
					case 0x3d:
					case 0x3f:
					case 0x40:
					case 0x41:
					case 0x43:
					case 0x45:
					case 0x4a:
						dev->regs[dev->index] = val;
						break;

					case 0x57:
						dev->reg_57h = val;
						break;
				}
			}
		}
		break;
	}
}

static uint8_t
ali1429_read(uint16_t addr, void *priv)
{
	ali1429_t *dev = (ali1429_t *)priv;

	if ((addr == 0x23) && (dev->index >= 0x10) && (dev->index <= 0x4a))
		return dev->regs[dev->index];
	else if ((addr == 0x23) && (dev->index == 0x57))
		return dev->reg_57h;
	else if ((addr == 0x23) && (dev->index >= 0xc0) && cpu_iscyrix)
		return 0xff;
	else if (addr == 0x22)
		return dev->index;
	else
		return 0;
}

static void
ali1429_close(void *priv)
{
	ali1429_t *dev = (ali1429_t *)priv;
	free(dev);
}

static void *
ali1429_init(const device_t *info)
{
	ali1429_t *dev = (ali1429_t *)malloc(sizeof(ali1429_t));
	memset(dev, 0, sizeof(ali1429_t));

	dev->cfg_locked = 1;
	GREEN = info->local;

	io_sethandler(0x0022, 0x0002, ali1429_read, NULL, NULL, ali1429_write, NULL, NULL, dev);

	device_add(&port_92_device);

	ali1429_defaults(dev);

	return dev;
}

const device_t ali1429_device = {
    "ALi M1429",
    0,
    0,
    ali1429_init, ali1429_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};

const device_t ali1429g_device = {
    "ALi M1429G",
    0,
    1,
    ali1429_init, ali1429_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
