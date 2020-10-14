/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the VIA VT82C49X chipset.
 *
 *
 *
 * Authors:	Tiseno100,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020 Tiseno100.
 *		Copyright 2020 Miran Grca.
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
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/pic.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

typedef struct
{
    uint8_t	has_ide, index,
		regs[256];

    smram_t	*smram_smm, *smram_low,
		*smram_high;
} vt82c49x_t;

#ifdef ENABLE_VT82C49X_LOG
int vt82c49x_do_log = ENABLE_VT82C49X_LOG;
static void
vt82c49x_log(const char *fmt, ...)
{
    va_list ap;

    if (vt82c49x_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define vt82c49x_log(fmt, ...)
#endif


static void
vt82c49x_recalc(vt82c49x_t *dev)
{
    int i, state;
    int relocate;
    int wr_c0, wr_c8, wr_e8, wr_e0;
    int rr_c0, rr_c8, rr_e8, rr_e0;
    int wp_c0, wp_e0, wp_e8, wp_f;
    uint32_t base;

    /* Register 33h */
    wr_c8 = (dev->regs[0x33] & 0x80) ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;
    wr_c0 = (dev->regs[0x33] & 0x40) ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;
    wr_e8 = (dev->regs[0x33] & 0x20) ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;
    wr_e0 = (dev->regs[0x33] & 0x10) ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;
    rr_c8 = (dev->regs[0x33] & 0x80) ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
    rr_c0 = (dev->regs[0x33] & 0x40) ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
    rr_e8 = (dev->regs[0x33] & 0x20) ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
    rr_e0 = (dev->regs[0x33] & 0x10) ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
    relocate = (dev->regs[0x33] >> 2) & 0x03;

    /* Register 40h */
    wp_c0 = (dev->regs[0x40] & 0x80) ? wr_c0 : MEM_WRITE_INTERNAL;
    wp_f = (dev->regs[0x40] & 0x40) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;
    wp_e8 = (dev->regs[0x40] & 0x20) ? wr_e8 : MEM_WRITE_INTERNAL;
    wp_e0 = (dev->regs[0x40] & 0x20) ? wr_e0 : MEM_WRITE_INTERNAL;

    /* Registers 30h-32h */
    if (relocate >= 2) {
	mem_set_mem_state_both(0xc8000, 0x8000, wr_c8 | rr_c8);
	mem_set_mem_state_both(0xc0000, 0x8000, wr_c0 | rr_c0);

	mem_set_mem_state_both(0xd0000, 0x10000, MEM_WRITE_EXTERNAL | MEM_READ_EXTERNAL);
    } else  for (i = 0; i < 8; i += 2) {
	base = 0xc0000 + (i << 13);
	if (base >= 0xc8000) {
		state = (dev->regs[0x30] & i) ? MEM_WRITE_INTERNAL : wr_c8;
		state |= (dev->regs[0x30] & (i + 1)) ? MEM_READ_INTERNAL : rr_c8;
	} else {
		state = (dev->regs[0x30] & i) ? wp_c0 : wr_c0;
		state |= (dev->regs[0x30] & (i + 1)) ? MEM_READ_INTERNAL : rr_c0;
	}
	mem_set_mem_state_both(base, 0x4000, state);

	base = 0xd0000 + (i << 13);
	state = (dev->regs[0x31] & i) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTERNAL;
	state |= (dev->regs[0x31] & (i + 1)) ? MEM_READ_INTERNAL : MEM_READ_EXTERNAL;
	mem_set_mem_state_both(base, 0x4000, state);
    }

    state = (dev->regs[0x32] & 0x10) ? wp_f : MEM_WRITE_EXTANY;
    state |= (dev->regs[0x32] & 0x20) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
    shadowbios_write = (dev->regs[0x32] & 0x10) ? ((wp_f == MEM_WRITE_INTERNAL) ? 1 : 0) : 0;
    shadowbios = (dev->regs[0x32] & 0x20) ? 1 : 0;
    mem_set_mem_state_both(0xf0000, 0x10000, state);

    if (relocate == 3) {
	mem_set_mem_state_both(0xe8000, 0x8000, wr_e8 | rr_e8);
	mem_set_mem_state_both(0xe0000, 0x8000, wr_e0 | rr_e0);
    } else {
	state = (dev->regs[0x32] & 0x40) ? wp_e8 : wr_e8;
	state |= (dev->regs[0x32] & 0x80) ? MEM_READ_INTERNAL : rr_e8;
	shadowbios_write |= (dev->regs[0x32] & 0x40) ? ((wp_e8 == MEM_WRITE_INTERNAL) ? 1 : 0) : 0;
	shadowbios |= (dev->regs[0x32] & 0x80) ? 1 : 0;
	mem_set_mem_state_both(0xe8000, 0x8000, state);

	state = (dev->regs[0x32] & 0x40) ? wp_e0 : wr_e0;
	state |= (dev->regs[0x32] & 0x80) ? MEM_READ_INTERNAL : rr_e0;
	shadowbios_write |= (dev->regs[0x32] & 0x40) ? ((wp_e0 == MEM_WRITE_INTERNAL) ? 1 : 0) : 0;
	shadowbios |= (dev->regs[0x32] & 0x80) ? 1 : 0;
	mem_set_mem_state_both(0xe0000, 0x8000, state);
    }

    switch (relocate) {
	case 0x00:
	default:
		mem_remap_top(0);
		break;
	case 0x02:
		mem_remap_top(256);
		break;
	case 0x03:
		mem_remap_top(384);
		break;
    }
}


static void
vt82c49x_write(uint16_t addr, uint8_t val, void *priv)
{
    vt82c49x_t *dev = (vt82c49x_t *) priv;
    uint8_t valxor;

    switch (addr) {
	case 0xa8:
		dev->index = val;
		break;

	case 0xa9:
		valxor = (val ^ dev->regs[dev->index]);
		if (dev->index == 0x55)
			dev->regs[dev->index] &= ~val;
		else
			dev->regs[dev->index] = val;

		vt82c49x_log("dev->regs[0x%02x] = %02x\n", dev->index, val);

		switch(dev->index) {
			/* Wait States */
			case 0x03:
				cpu_update_waitstates();
				break;

			/* Shadow RAM and top of RAM relocation */
			case 0x30:
			case 0x31:
			case 0x32:
			case 0x33:
			case 0x40:
				vt82c49x_recalc(dev);
				break;

			/* External Cache Enable(Based on the 486-VC-HD BIOS) */
			case 0x50:
				cpu_cache_ext_enabled = (val & 0x84);
				break;

			/* Software SMI */
			case 0x54:
				if ((dev->regs[0x5b] & 0x80) && (valxor & 0x01) && (val & 0x01)) {
					if (dev->regs[0x5b] & 0x20)
						smi_line = 1;
					else
						picint(1 << 15);
					dev->regs[0x55] = 0x01;
				}
				break;

			/* SMRAM */
			case 0x5b:
				smram_disable_all();

				if (val & 0x80) {
					smram_enable(dev->smram_smm, (val & 0x40) ? 0x00060000 : 0x00030000, 0x000a0000, 0x00020000,
						     0, (val & 0x10));
					smram_enable(dev->smram_high, 0x000a0000, 0x000a0000, 0x00020000,
						     (val & 0x08), (val & 0x08));
					smram_enable(dev->smram_low, 0x00030000, 0x000a0000, 0x00020000,
						     (val & 0x02), 0);
				}
				break;

			/* Edge/Level IRQ Control */
			case 0x62: case 0x63:
				if (dev->index == 0x63)
					pic_elcr_write(dev->index, val & 0xde, NULL);
				else {
					pic_elcr_write(dev->index, val & 0xf8, NULL);
					pic_elcr_set_enabled(val & 0x01);
				}
				break;

			/* Local Bus IDE Controller */
			case 0x71:
				if (dev->has_ide) {
					ide_pri_disable();
					ide_set_base(0, (val & 0x40) ? 0x170 : 0x1f0);
					ide_set_side(0, (val & 0x40) ? 0x376 : 0x3f6);
					if (val & 0x01)
						ide_pri_enable();
				}
				break;
		}
		break;
    }
}


static uint8_t
vt82c49x_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    vt82c49x_t *dev = (vt82c49x_t *) priv;

    switch (addr) {
	case 0xa9:
		if (dev->index == 0x63)
			ret = pic_elcr_read(dev->index, NULL) | (dev->regs[dev->index] & 0x01);
		else if (dev->index == 0x62)
			ret = pic_elcr_read(dev->index, NULL) | (dev->regs[dev->index] & 0x07);
		else
			ret = dev->regs[dev->index];
		break;
    }

    return ret;
}


static void
vt82c49x_close(void *priv)
{
    vt82c49x_t *dev = (vt82c49x_t *) priv;

    smram_del(dev->smram_high);
    smram_del(dev->smram_low);
    smram_del(dev->smram_smm);

    free(dev);
}


static void *
vt82c49x_init(const device_t *info)
{
    vt82c49x_t *dev = (vt82c49x_t *) malloc(sizeof(vt82c49x_t));
    memset(dev, 0x00, sizeof(vt82c49x_t));

    dev->smram_smm = smram_add();
    dev->smram_low = smram_add();
    dev->smram_high = smram_add();

    dev->has_ide = info->local & 1;
    if (dev->has_ide)
	device_add(&ide_vlb_device);

    device_add(&port_92_device);

    io_sethandler(0x0a8, 0x0002, vt82c49x_read, NULL, NULL, vt82c49x_write, NULL, NULL, dev);

    pic_elcr_io_handler(0);
    pic_elcr_set_enabled(1);

    vt82c49x_recalc(dev);
    
    return dev;
}


const device_t via_vt82c49x_device = {
    "VIA VT82C49X",
    0,
    0,
    vt82c49x_init, vt82c49x_close, NULL,
    NULL, NULL, NULL,
    NULL
};


const device_t via_vt82c49x_ide_device = {
    "VIA VT82C49X (With IDE)",
    0,
    1,
    vt82c49x_init, vt82c49x_close, NULL,
    NULL, NULL, NULL,
    NULL
};
