/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the VLSI VL82c480 chipset.
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2020 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

typedef struct {
        int cfg_index;
        uint8_t cfg_regs[256];
} vl82c480_t;

#define CFG_ID     0x00
#define CFG_AAXS   0x0d
#define CFG_BAXS   0x0e
#define CFG_CAXS   0x0f
#define CFG_DAXS   0x10
#define CFG_EAXS   0x11
#define CFG_FAXS   0x12

#define ID_VL82C480 0x90

static void 
shadow_control(uint32_t addr, uint32_t size, int state)
{
/*      pclog("shadow_control: addr=%08x size=%04x state=%i\n", addr, size, state); */
	switch (state) {
		case 0:
			mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
			break;
		case 1:
			mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
			break;
		case 2:
			mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
			break;
		case 3:
			mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
			break;
	}
	flushmmucache_nopc();
}

static void
vl82c480_write(uint16_t addr, uint8_t val, void *p)
{
	vl82c480_t *dev = (vl82c480_t *)p;
	
	switch (addr) {
		case 0xec:
			dev->cfg_index = val;
			break;

		case 0xed:
			if (dev->cfg_index >= 0x01 && dev->cfg_index <= 0x24) {
				dev->cfg_regs[dev->cfg_index] = val;
                        switch (dev->cfg_index) {
				case CFG_AAXS:
					shadow_control(0xa0000, 0x4000, val & 3);
					shadow_control(0xa4000, 0x4000, (val >> 2) & 3);
					shadow_control(0xa8000, 0x4000, (val >> 4) & 3);
					shadow_control(0xac000, 0x4000, (val >> 6) & 3);
					break;
				case CFG_BAXS:
					shadow_control(0xb0000, 0x4000, val & 3);
					shadow_control(0xb4000, 0x4000, (val >> 2) & 3);
					shadow_control(0xb8000, 0x4000, (val >> 4) & 3);
					shadow_control(0xbc000, 0x4000, (val >> 6) & 3);
					break;
				case CFG_CAXS:
					shadow_control(0xc0000, 0x4000, val & 3);
					shadow_control(0xc4000, 0x4000, (val >> 2) & 3);
					shadow_control(0xc8000, 0x4000, (val >> 4) & 3);
					shadow_control(0xcc000, 0x4000, (val >> 6) & 3);
					break;
				case CFG_DAXS:
					shadow_control(0xd0000, 0x4000, val & 3);
					shadow_control(0xd4000, 0x4000, (val >> 2) & 3);
					shadow_control(0xd8000, 0x4000, (val >> 4) & 3);
					shadow_control(0xdc000, 0x4000, (val >> 6) & 3);
					break;
				case CFG_EAXS:
					shadow_control(0xe0000, 0x4000, val & 3);
					shadow_control(0xe4000, 0x4000, (val >> 2) & 3);
					shadow_control(0xe8000, 0x4000, (val >> 4) & 3);
					shadow_control(0xec000, 0x4000, (val >> 6) & 3);
					break;
				case CFG_FAXS:
					shadow_control(0xf0000, 0x4000, val & 3);
					shadow_control(0xf4000, 0x4000, (val >> 2) & 3);
					shadow_control(0xf8000, 0x4000, (val >> 4) & 3);
					shadow_control(0xfc000, 0x4000, (val >> 6) & 3);
					break;
			}
		}
		break;

		case 0xee:
			if (mem_a20_alt)
				outb(0x92, inb(0x92) & ~2);
			break;
        }
}

static uint8_t 
vl82c480_read(uint16_t addr, void *p)
{
	vl82c480_t *dev = (vl82c480_t *)p;
	uint8_t ret = 0xff;

	switch (addr) {
		case 0xec:
			ret = dev->cfg_index;
			break;
		
		case 0xed:
			ret = dev->cfg_regs[dev->cfg_index];
			break;
		
		case 0xee:
			if (!mem_a20_alt)
				outb(0x92, inb(0x92) | 2);
			break;
                
                case 0xef:
			softresetx86();
			cpu_set_edx();
			break;
	}

	return ret;
}

static void
vl82c480_close(void *p)
{
	vl82c480_t *dev = (vl82c480_t *)p;

	free(dev);
}


static void *
vl82c480_init(const device_t *info)
{
	vl82c480_t *dev = (vl82c480_t *)malloc(sizeof(vl82c480_t));
	memset(dev, 0, sizeof(vl82c480_t));
	
	dev->cfg_regs[CFG_ID] = ID_VL82C480;
	
	io_sethandler(0x00ec, 0x0004,  vl82c480_read, NULL, NULL, vl82c480_write, NULL, NULL,  dev);

	device_add(&port_92_device);

	return dev;
}

const device_t vl82c480_device = {
    "VLSI VL82c480",
    0,
    0,
    vl82c480_init, vl82c480_close, NULL,
    NULL, NULL, NULL,
    NULL
};
