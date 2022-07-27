/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 450KX Mars Chipset.
 *
 * Authors:	Tiseno100,
 *
 *		Copyright 2021 Tiseno100.
 */

/*
Note: i450KX PB manages PCI memory access with MC manages DRAM memory access.
Due to 86Box limitations we can't manage them seperately thus it is dev branch till then.

i450GX is way more popular of an option but needs more stuff.
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
#include <86box/pci.h>
#include <86box/smram.h>
#include <86box/spd.h>
#include <86box/chipset.h>


#ifdef ENABLE_450KX_LOG
int i450kx_do_log = ENABLE_450KX_LOG;
static void
i450kx_log(const char *fmt, ...)
{
    va_list ap;

    if (i450kx_do_log)
    {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define i450kx_log(fmt, ...)
#endif


/* TODO: Finish the bus index stuff. */
typedef struct i450kx_t {
    smram_t *	smram[2];

    uint8_t	pb_pci_conf[256], mc_pci_conf[256];
    uint8_t	mem_state[2][256];

    uint8_t	bus_index;
} i450kx_t;


static void
i450kx_map(i450kx_t *dev, int bus, uint32_t addr, uint32_t size, int state)
{
    uint32_t base = addr >> 12;
    int states[4] = { MEM_READ_EXTANY | MEM_WRITE_EXTANY, MEM_READ_INTERNAL | MEM_WRITE_EXTANY,
		      MEM_READ_EXTANY | MEM_WRITE_INTERNAL, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL };

    state &= 3;
    if (dev->mem_state[bus][base] != state) {
	if (bus)
		mem_set_mem_state_bus_both(addr, size, states[state]);
	else
		mem_set_mem_state_cpu_both(addr, size, states[state]);
	dev->mem_state[bus][base] = state;
	flushmmucache_nopc();
    }
}


static void
i450kx_smram_recalc(i450kx_t *dev, int bus)
{
    uint8_t *regs = bus ? dev->pb_pci_conf : dev->mc_pci_conf;
    uint32_t addr, size;

    smram_disable(dev->smram[bus]);

    addr = ((uint32_t) regs[0xb8] << 16) | ((uint32_t) regs[0xb9] << 24);
    size = (((uint32_t) ((regs[0xbb] >> 4) & 0x0f)) << 16) + 0x00010000;

    if ((addr != 0x00000000) && !!(regs[0x57] & 0x08)) {
	if (bus)
		smram_enable_ex(dev->smram[bus], addr, addr, size, 0, !!(regs[0x57] & 8), 0, 1);
	else
		smram_enable_ex(dev->smram[bus], addr, addr, size, !!(regs[0x57] & 8), 0, 1, 0);
    }

    flushmmucache();
}


static void
i450kx_vid_buf_recalc(i450kx_t *dev, int bus)
{
    uint8_t *regs = bus ? dev->pb_pci_conf : dev->mc_pci_conf;

    // int state = (regs[0x58] & 0x02) ? (MEM_READ_EXTANY | MEM_WRITE_EXTANY) : (MEM_READ_DISABLED | MEM_WRITE_DISABLED);
    int state = (regs[0x58] & 0x02) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    if (bus)
	mem_set_mem_state_bus_both(0x000a0000, 0x00020000, state);
    else
	mem_set_mem_state_cpu_both(0x000a0000, 0x00020000, state);

    flushmmucache_nopc();
}


static void
pb_write(int func, int addr, uint8_t val, void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;

    // pclog("i450KX-PB: [W] dev->pb_pci_conf[%02X] = %02X POST: %02X\n", addr, val, inb(0x80));
    i450kx_log("i450KX-PB: [W] dev->pb_pci_conf[%02X] = %02X POST: %02X\n", addr, val, inb(0x80));

    if (func == 0)  switch (addr) {
	case 0x04:
		dev->pb_pci_conf[addr] = (dev->pb_pci_conf[addr] & 0x04) | (val & 0x53);
		break;
	case 0x05:
		dev->pb_pci_conf[addr] = val & 0x01;
		break;

	case 0x07:
		dev->pb_pci_conf[addr] &= ~(val & 0xf9);
		break;

	case 0x0d:
		dev->pb_pci_conf[addr] = val;
		break;

	case 0x0f:
		dev->pb_pci_conf[addr] = val & 0xcf;
		break;

	case 0x40: case 0x41:
		dev->pb_pci_conf[addr] = val;
		break;
	case 0x43:
		dev->pb_pci_conf[addr] = val & 0x80;
		break;

	case 0x48:
		dev->pb_pci_conf[addr] = val & 0x06;
		break;

	case 0x4a: case 0x4b:
		dev->pb_pci_conf[addr] = val;
		// if (addr == 0x4a)
			// pci_remap_bus(dev->bus_index, val);
		break;

	case 0x4c:
		dev->pb_pci_conf[addr] = (dev->pb_pci_conf[addr] & 0x01) | (val & 0xd8);
		break;

	case 0x51:
		dev->pb_pci_conf[addr] = val;
		break;

	case 0x53:
		dev->pb_pci_conf[addr] = val & 0x02;
		break;

	case 0x54:
		dev->pb_pci_conf[addr] = val & 0x7b;
		break;
	case 0x55:
		dev->pb_pci_conf[addr] = val & 0x03;
		break;

	case 0x57:
		dev->pb_pci_conf[addr] = val & 0x08;
		i450kx_smram_recalc(dev, 1);
		break;

	case 0x58:
		dev->pb_pci_conf[addr] = val & 0x02;
		i450kx_vid_buf_recalc(dev, 1);
		break;

	case 0x59:	/* PAM0 */
		if ((dev->pb_pci_conf[0x59] ^ val) & 0x0f)
			i450kx_map(dev, 1, 0x80000, 0x20000, val & 0x0f);
		if ((dev->pb_pci_conf[0x59] ^ val) & 0xf0) {
			i450kx_map(dev, 1, 0xf0000, 0x10000, val >> 4);
			shadowbios = (val & 0x10);
		}
		dev->pb_pci_conf[0x59] = val & 0x33;
		break;
	case 0x5a:	/* PAM1 */
		if ((dev->pb_pci_conf[0x5a] ^ val) & 0x0f)
			i450kx_map(dev, 1, 0xc0000, 0x04000, val & 0xf);
		if ((dev->pb_pci_conf[0x5a] ^ val) & 0xf0)
			i450kx_map(dev, 1, 0xc4000, 0x04000, val >> 4);
		dev->pb_pci_conf[0x5a] = val & 0x33;
		break;
	case 0x5b:	/*PAM2 */
		if ((dev->pb_pci_conf[0x5b] ^ val) & 0x0f)
			i450kx_map(dev, 1, 0xc8000, 0x04000, val & 0xf);
		if ((dev->pb_pci_conf[0x5b] ^ val) & 0xf0)
			i450kx_map(dev, 1, 0xcc000, 0x04000, val >> 4);
		dev->pb_pci_conf[0x5b] = val & 0x33;
		break;
	case 0x5c:	/*PAM3 */
		if ((dev->pb_pci_conf[0x5c] ^ val) & 0x0f)
			i450kx_map(dev, 1, 0xd0000, 0x04000, val & 0xf);
		if ((dev->pb_pci_conf[0x5c] ^ val) & 0xf0)
			i450kx_map(dev, 1, 0xd4000, 0x04000, val >> 4);
		dev->pb_pci_conf[0x5c] = val & 0x33;
		break;
	case 0x5d:	/* PAM4 */
		if ((dev->pb_pci_conf[0x5d] ^ val) & 0x0f)
			i450kx_map(dev, 1, 0xd8000, 0x04000, val & 0xf);
		if ((dev->pb_pci_conf[0x5d] ^ val) & 0xf0)
			i450kx_map(dev, 1, 0xdc000, 0x04000, val >> 4);
		dev->pb_pci_conf[0x5d] = val & 0x33;
		break;
	case 0x5e:	/* PAM5 */
		if ((dev->pb_pci_conf[0x5e] ^ val) & 0x0f)
			i450kx_map(dev, 1, 0xe0000, 0x04000, val & 0xf);
		if ((dev->pb_pci_conf[0x5e] ^ val) & 0xf0)
			i450kx_map(dev, 1, 0xe4000, 0x04000, val >> 4);
		dev->pb_pci_conf[0x5e] = val & 0x33;
		break;
	case 0x5f:	/* PAM6 */
		if ((dev->pb_pci_conf[0x5f] ^ val) & 0x0f)
			i450kx_map(dev, 1, 0xe8000, 0x04000, val & 0xf);
		if ((dev->pb_pci_conf[0x5f] ^ val) & 0xf0)
			i450kx_map(dev, 1, 0xec000, 0x04000, val >> 4);
		dev->pb_pci_conf[0x5f] = val & 0x33;
		break;

	case 0x70:
		dev->pb_pci_conf[addr] = val & 0xf8;
		break;

	case 0x71:
		dev->pb_pci_conf[addr] = val & 0x71;
		break;

	case 0x78:
		dev->pb_pci_conf[addr] = val & 0xf0;
		break;
	case 0x79:
		dev->pb_pci_conf[addr] = val & 0xfc;
		break;
	case 0x7a:
		dev->pb_pci_conf[addr] = val;
		break;
	case 0x7b:
		dev->pb_pci_conf[addr] = val & 0x0f;
		break;

	case 0x7c:
		dev->pb_pci_conf[addr] = val & 0x9f;
		break;
	case 0x7d:
		dev->pb_pci_conf[addr] = val & 0x1a;
		break;
	case 0x7e:
		dev->pb_pci_conf[addr] = val & 0xf0;
		break;
	case 0x7f:
		dev->pb_pci_conf[addr] = val;
		break;

	case 0x88: case 0x89:
		dev->pb_pci_conf[addr] = val;
		break;
	case 0x8b:
		dev->pb_pci_conf[addr] = val & 0x80;
		break;
	case 0x8c: case 0x8d:
		dev->pb_pci_conf[addr] = val;
		break;

	case 0x9c:
		dev->pb_pci_conf[addr] = val & 0x01;
		break;

	case 0xa4:
		dev->pb_pci_conf[addr] = val & 0xf8;
		break;
	case 0xa5: case 0xa6:
		dev->pb_pci_conf[addr] = val;
		break;
	case 0xa7:
		dev->pb_pci_conf[addr] = val & 0x0f;
		break;

	case 0xb0:
		dev->pb_pci_conf[addr] = val & 0xe0;
		break;
	case 0xb1:
		dev->pb_pci_conf[addr] = val & /*0x1a*/ 0x1f;
		break;

	case 0xb4:
		dev->pb_pci_conf[addr] = val & 0xe0;
		break;
	case 0xb5:
		dev->pb_pci_conf[addr] = val & 0x1f;
		break;

	case 0xb8: case 0xb9:
		dev->pb_pci_conf[addr] = val;
		i450kx_smram_recalc(dev, 1);
		break;
	case 0xbb:
		dev->pb_pci_conf[addr] = val & 0xf0;
		i450kx_smram_recalc(dev, 1);
		break;

	case 0xbc:
		dev->pb_pci_conf[addr] = val & 0x11;
		break;

	case 0xc0:
		dev->pb_pci_conf[addr] = val & 0xdf;
		break;
	case 0xc1:
		dev->pb_pci_conf[addr] = val & 0x3f;
		break;

	case 0xc4:
		dev->pb_pci_conf[addr] &= ~(val & 0x0f);
		break;
	case 0xc5:
		dev->pb_pci_conf[addr] &= ~(val & 0x0a);
		break;
	case 0xc6:
		dev->pb_pci_conf[addr] &= ~(val & 0x1f);
		break;

	case 0xc8:
		dev->pb_pci_conf[addr] = val & 0x1f;
		break;

	case 0xca:
	case 0xcb:
		dev->pb_pci_conf[addr] = val;
		break;
    }
}


static uint8_t
pb_read(int func, int addr, void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;
    uint8_t ret = 0xff;

    if (func == 0)
	ret = dev->pb_pci_conf[addr];

    // pclog("i450KX-PB: [R] dev->pb_pci_conf[%02X] = %02X POST: %02X\n", addr, ret, inb(0x80));

    return ret;
}


/* A way to use spd_write_drbs_interlaved() and convert the output to what we need. */
static void
mc_fill_drbs(i450kx_t *dev)
{
    int i;

    spd_write_drbs_interleaved(dev->mc_pci_conf, 0x60, 0x6f, 4);
    for (i = 0x60; i <= 0x6f; i++) {
	if (i & 0x01)
		dev->mc_pci_conf[i] = 0x00;
	else
		dev->mc_pci_conf[i] &= 0x7f;
    }
}


static void
mc_write(int func, int addr, uint8_t val, void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;

    // pclog("i450KX-MC: [W] dev->mc_pci_conf[%02X] = %02X POST: %02X\n", addr, val, inb(0x80));
    i450kx_log("i450KX-MC: [W] dev->mc_pci_conf[%02X] = %02X POST: %02X\n", addr, val, inb(0x80));

    if (func == 0)  switch (addr) {
	case 0x4c:
		dev->mc_pci_conf[addr] = val & 0xdf;
		break;
	case 0x4d:
		dev->mc_pci_conf[addr] = val & 0xff;
		break;

	case 0x57:
		dev->mc_pci_conf[addr] = val & 0x08;
		i450kx_smram_recalc(dev, 0);
		break;

	case 0x58:
		dev->mc_pci_conf[addr] = val & 0x02;
		i450kx_vid_buf_recalc(dev, 0);
		break;

	case 0x59:	/* PAM0 */
		if ((dev->mc_pci_conf[0x59] ^ val) & 0x0f)
			i450kx_map(dev, 0, 0x80000, 0x20000, val & 0x0f);
		if ((dev->mc_pci_conf[0x59] ^ val) & 0xf0) {
			i450kx_map(dev, 0, 0xf0000, 0x10000, val >> 4);
			shadowbios = (val & 0x10);
		}
		dev->mc_pci_conf[0x59] = val & 0x33;
		break;
	case 0x5a:	/* PAM1 */
		if ((dev->mc_pci_conf[0x5a] ^ val) & 0x0f)
			i450kx_map(dev, 0, 0xc0000, 0x04000, val & 0xf);
		if ((dev->mc_pci_conf[0x5a] ^ val) & 0xf0)
			i450kx_map(dev, 0, 0xc4000, 0x04000, val >> 4);
		dev->mc_pci_conf[0x5a] = val & 0x33;
		break;
	case 0x5b:	/*PAM2 */
		if ((dev->mc_pci_conf[0x5b] ^ val) & 0x0f)
			i450kx_map(dev, 0, 0xc8000, 0x04000, val & 0xf);
		if ((dev->mc_pci_conf[0x5b] ^ val) & 0xf0)
			i450kx_map(dev, 0, 0xcc000, 0x04000, val >> 4);
		dev->mc_pci_conf[0x5b] = val & 0x33;
		break;
	case 0x5c:	/*PAM3 */
		if ((dev->mc_pci_conf[0x5c] ^ val) & 0x0f)
			i450kx_map(dev, 0, 0xd0000, 0x04000, val & 0xf);
		if ((dev->mc_pci_conf[0x5c] ^ val) & 0xf0)
			i450kx_map(dev, 0, 0xd4000, 0x04000, val >> 4);
		dev->mc_pci_conf[0x5c] = val & 0x33;
		break;
	case 0x5d:	/* PAM4 */
		if ((dev->mc_pci_conf[0x5d] ^ val) & 0x0f)
			i450kx_map(dev, 0, 0xd8000, 0x04000, val & 0xf);
		if ((dev->mc_pci_conf[0x5d] ^ val) & 0xf0)
			i450kx_map(dev, 0, 0xdc000, 0x04000, val >> 4);
		dev->mc_pci_conf[0x5d] = val & 0x33;
		break;
	case 0x5e:	/* PAM5 */
		if ((dev->mc_pci_conf[0x5e] ^ val) & 0x0f)
			i450kx_map(dev, 0, 0xe0000, 0x04000, val & 0xf);
		if ((dev->mc_pci_conf[0x5e] ^ val) & 0xf0)
			i450kx_map(dev, 0, 0xe4000, 0x04000, val >> 4);
		dev->mc_pci_conf[0x5e] = val & 0x33;
		break;
	case 0x5f:	/* PAM6 */
		if ((dev->mc_pci_conf[0x5f] ^ val) & 0x0f)
			i450kx_map(dev, 0, 0xe8000, 0x04000, val & 0xf);
		if ((dev->mc_pci_conf[0x5f] ^ val) & 0xf0)
			i450kx_map(dev, 0, 0xec000, 0x04000, val >> 4);
		dev->mc_pci_conf[0x5f] = val & 0x33;
		break;

	case 0x60 ... 0x6f:
		dev->mc_pci_conf[addr] = ((addr & 0x0f) & 0x01) ? 0x00 : (val & 0x7f);
		mc_fill_drbs(dev);
		break;

	case 0x74 ... 0x77:
		dev->mc_pci_conf[addr] = val;
		break;

	case 0x78:
		dev->mc_pci_conf[addr] = val & 0xf0;
		break;
	case 0x79:
		dev->mc_pci_conf[addr] = val & 0xfe;
		break;
	case 0x7a:
		dev->mc_pci_conf[addr] = val;
		break;
	case 0x7b:
		dev->mc_pci_conf[addr] = val & 0x0f;
		break;

	case 0x7c:
		dev->mc_pci_conf[addr] = val & 0x1f;
		break;
	case 0x7d:
		dev->mc_pci_conf[addr] = val & 0x0c;
		break;
	case 0x7e:
		dev->mc_pci_conf[addr] = val & 0xf0;
		break;
	case 0x7f:
		dev->mc_pci_conf[addr] = val;
		break;

	case 0x88: case 0x89:
		dev->mc_pci_conf[addr] = val;
		break;
	case 0x8b:
		dev->mc_pci_conf[addr] = val & 0x80;
		break;

	case 0x8c: case 0x8d:
		dev->mc_pci_conf[addr] = val;
		break;

	case 0xa4:
		dev->mc_pci_conf[addr] = val & 0x01;
		break;
	case 0xa5:
		dev->pb_pci_conf[addr] = val & 0xf0;
		break;
	case 0xa6:
		dev->mc_pci_conf[addr] = val;
		break;
	case 0xa7:
		dev->mc_pci_conf[addr] = val & 0x0f;
		break;

	case 0xa8:
		dev->mc_pci_conf[addr] = val & 0xfe;
		break;
	case 0xa9 ... 0xab:
		dev->mc_pci_conf[addr] = val;
		break;

	case 0xac ... 0xae:
		dev->mc_pci_conf[addr] = val;
		break;
	case 0xaf:
		dev->mc_pci_conf[addr] = val & 0x7f;
		break;

	case 0xb8: case 0xb9:
		dev->mc_pci_conf[addr] = val;
		i450kx_smram_recalc(dev, 0);
		break;
	case 0xbb:
		dev->mc_pci_conf[addr] = val & 0xf0;
		i450kx_smram_recalc(dev, 0);
		break;

	case 0xbc:
		dev->mc_pci_conf[addr] = val & 0x01;
		break;

	case 0xc0:
		dev->mc_pci_conf[addr] = val & 0x07;
		break;

	case 0xc2:
		dev->mc_pci_conf[addr] &= ~(val & 0x03);
		break;

	case 0xc4:
		dev->mc_pci_conf[addr] = val & 0xbf;
		break;
	case 0xc5:
		dev->mc_pci_conf[addr] = val & 0x03;
		break;

	case 0xc6:
		dev->mc_pci_conf[addr] &= ~(val & 0x19);
		break;

	case 0xc8:
		dev->mc_pci_conf[addr] = val & 0x1f;
		break;
	case 0xca: case 0xcb:
		dev->mc_pci_conf[addr] = val;
		break;
    }
}


static uint8_t
mc_read(int func, int addr, void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;
    uint8_t ret = 0xff;

    if (func == 0)
	ret = dev->mc_pci_conf[addr];

    // pclog("i450KX-MC: [R] dev->mc_pci_conf[%02X] = %02X POST: %02X\n", addr, ret, inb(0x80));

    return ret;
}


static void
i450kx_reset(void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;
    uint32_t i;

    // pclog("i450KX: i450kx_reset()\n");

    /* Defaults PB */
    dev->pb_pci_conf[0x00] = 0x86;
    dev->pb_pci_conf[0x01] = 0x80;
    dev->pb_pci_conf[0x02] = 0xc4;
    dev->pb_pci_conf[0x03] = 0x84;
    dev->pb_pci_conf[0x04] = 0x07;
    dev->pb_pci_conf[0x05] = 0x00;
    dev->pb_pci_conf[0x06] = 0x40;
    dev->pb_pci_conf[0x07] = 0x02;
    dev->pb_pci_conf[0x08] = 0x02;
    dev->pb_pci_conf[0x09] = 0x00;
    dev->pb_pci_conf[0x0a] = 0x00;
    dev->pb_pci_conf[0x0b] = 0x06;
    dev->pb_pci_conf[0x0c] = 0x08;
    dev->pb_pci_conf[0x0d] = 0x20;
    dev->pb_pci_conf[0x0e] = 0x00;
    dev->pb_pci_conf[0x0f] = 0x00;
    dev->pb_pci_conf[0x40] = 0x00;
    dev->pb_pci_conf[0x41] = 0x00;
    dev->pb_pci_conf[0x42] = 0x00;
    dev->pb_pci_conf[0x43] = 0x00;
    dev->pb_pci_conf[0x48] = 0x06;
    dev->pb_pci_conf[0x49] = 0x19;
    dev->pb_pci_conf[0x4a] = 0x00;
    dev->pb_pci_conf[0x4b] = 0x00;
    dev->pb_pci_conf[0x4c] = 0x19;
    dev->pb_pci_conf[0x51] = 0x80;
    dev->pb_pci_conf[0x53] = 0x00;
    dev->pb_pci_conf[0x54] = 0x00;
    dev->pb_pci_conf[0x55] = 0x00;
    dev->pb_pci_conf[0x57] = 0x00;
    dev->pb_pci_conf[0x58] = 0x02;
    dev->pb_pci_conf[0x70] = 0x00;
    dev->pb_pci_conf[0x71] = 0x00;
    dev->pb_pci_conf[0x78] = 0x00;
    dev->pb_pci_conf[0x79] = 0x00;
    dev->pb_pci_conf[0x7a] = 0x00;
    dev->pb_pci_conf[0x7b] = 0x00;
    dev->pb_pci_conf[0x7c] = 0x00;
    dev->pb_pci_conf[0x7d] = 0x00;
    dev->pb_pci_conf[0x7e] = 0x00;
    dev->pb_pci_conf[0x7f] = 0x00;
    dev->pb_pci_conf[0x88] = 0x00;
    dev->pb_pci_conf[0x89] = 0x00;
    dev->pb_pci_conf[0x8a] = 0x00;
    dev->pb_pci_conf[0x8b] = 0x00;
    dev->pb_pci_conf[0x8c] = 0x00;
    dev->pb_pci_conf[0x8d] = 0x00;
    dev->pb_pci_conf[0x8e] = 0x00;
    dev->pb_pci_conf[0x8f] = 0x00;
    dev->pb_pci_conf[0x9c] = 0x00;
    dev->pb_pci_conf[0xa4] = 0x01;
    dev->pb_pci_conf[0xa5] = 0xc0;
    dev->pb_pci_conf[0xa6] = 0xfe;
    dev->pb_pci_conf[0xa7] = 0x00;
    /* Note: Do NOT reset these two registers on programmed (TRC) hard reset! */
    // dev->pb_pci_conf[0xb0] = 0x00;
    // dev->pb_pci_conf[0xb1] = 0x00;
    dev->pb_pci_conf[0xb4] = 0x00;
    dev->pb_pci_conf[0xb5] = 0x00;
    dev->pb_pci_conf[0xb8] = 0x05;
    dev->pb_pci_conf[0xb9] = 0x00;
    dev->pb_pci_conf[0xba] = 0x00;
    dev->pb_pci_conf[0xbb] = 0x00;
    dev->pb_pci_conf[0xbc] = 0x01;
    dev->pb_pci_conf[0xc0] = 0x02;
    dev->pb_pci_conf[0xc1] = 0x00;
    dev->pb_pci_conf[0xc2] = 0x00;
    dev->pb_pci_conf[0xc3] = 0x00;
    dev->pb_pci_conf[0xc4] = 0x00;
    dev->pb_pci_conf[0xc5] = 0x00;
    dev->pb_pci_conf[0xc6] = 0x00;
    dev->pb_pci_conf[0xc7] = 0x00;
    dev->pb_pci_conf[0xc8] = 0x03;
    dev->pb_pci_conf[0xc9] = 0x00;
    dev->pb_pci_conf[0xca] = 0x00;
    dev->pb_pci_conf[0xcb] = 0x00;

    // pci_remap_bus(dev->bus_index, 0x00);
    i450kx_smram_recalc(dev, 1);
    i450kx_vid_buf_recalc(dev, 1);
    pb_write(0, 0x59, 0x30, dev);
    for (i = 0x5a; i <= 0x5f; i++)
	pb_write(0, i, 0x33, dev);

    /* Defaults MC */
    dev->mc_pci_conf[0x00] = 0x86;
    dev->mc_pci_conf[0x01] = 0x80;
    dev->mc_pci_conf[0x02] = 0xc5;
    dev->mc_pci_conf[0x03] = 0x84;
    dev->mc_pci_conf[0x04] = 0x00;
    dev->mc_pci_conf[0x05] = 0x00;
    dev->mc_pci_conf[0x06] = 0x80;
    dev->mc_pci_conf[0x07] = 0x00;
    dev->mc_pci_conf[0x08] = 0x04;
    dev->mc_pci_conf[0x09] = 0x00;
    dev->mc_pci_conf[0x0a] = 0x00;
    dev->mc_pci_conf[0x0b] = 0x05;
    dev->mc_pci_conf[0x49] = 0x14;
    dev->mc_pci_conf[0x4c] = 0x0b;
    dev->mc_pci_conf[0x4d] = 0x08;
    dev->mc_pci_conf[0x4e] = 0x00;
    dev->mc_pci_conf[0x4f] = 0x00;
    dev->mc_pci_conf[0x57] = 0x00;
    dev->mc_pci_conf[0x58] = 0x00;
    dev->mc_pci_conf[0x74] = 0x00;
    dev->mc_pci_conf[0x75] = 0x00;
    dev->mc_pci_conf[0x76] = 0x00;
    dev->mc_pci_conf[0x77] = 0x00;
    dev->mc_pci_conf[0x78] = 0x10;
    dev->mc_pci_conf[0x79] = 0x00;
    dev->mc_pci_conf[0x7a] = 0x00;
    dev->mc_pci_conf[0x7b] = 0x00;
    dev->mc_pci_conf[0x7c] = 0x00;
    dev->mc_pci_conf[0x7d] = 0x00;
    dev->mc_pci_conf[0x7e] = 0x10;
    dev->mc_pci_conf[0x7f] = 0x00;
    dev->mc_pci_conf[0x88] = 0x00;
    dev->mc_pci_conf[0x89] = 0x00;
    dev->mc_pci_conf[0x8a] = 0x00;
    dev->mc_pci_conf[0x8b] = 0x00;
    dev->mc_pci_conf[0x8c] = 0x00;
    dev->mc_pci_conf[0x8d] = 0x00;
    dev->mc_pci_conf[0x8e] = 0x00;
    dev->mc_pci_conf[0x8f] = 0x00;
    dev->mc_pci_conf[0xa4] = 0x01;
    dev->mc_pci_conf[0xa5] = 0xc0;
    dev->mc_pci_conf[0xa6] = 0xfe;
    dev->mc_pci_conf[0xa7] = 0x00;
    dev->mc_pci_conf[0xa8] = 0x00;
    dev->mc_pci_conf[0xa9] = 0x00;
    dev->mc_pci_conf[0xaa] = 0x00;
    dev->mc_pci_conf[0xab] = 0x00;
    dev->mc_pci_conf[0xac] = 0x16;
    dev->mc_pci_conf[0xad] = 0x35;
    dev->mc_pci_conf[0xae] = 0xdf;
    dev->mc_pci_conf[0xaf] = 0x30;
    dev->mc_pci_conf[0xb8] = 0x0a;
    dev->mc_pci_conf[0xb9] = 0x00;
    dev->mc_pci_conf[0xba] = 0x00;
    dev->mc_pci_conf[0xbb] = 0x00;
    dev->mc_pci_conf[0xbc] = 0x01;
    dev->mc_pci_conf[0xc0] = 0x00;
    dev->mc_pci_conf[0xc1] = 0x00;
    dev->mc_pci_conf[0xc2] = 0x00;
    dev->mc_pci_conf[0xc3] = 0x00;
    dev->mc_pci_conf[0xc4] = 0x00;
    dev->mc_pci_conf[0xc5] = 0x00;
    dev->mc_pci_conf[0xc6] = 0x00;
    dev->mc_pci_conf[0xc7] = 0x00;

    i450kx_smram_recalc(dev, 0);
    i450kx_vid_buf_recalc(dev, 0);
    mc_write(0, 0x59, 0x03, dev);
    for (i = 0x5a; i <= 0x5f; i++)
	mc_write(0, i, 0x00, dev);
    for (i = 0x60; i <= 0x6f; i++)
	dev->mc_pci_conf[i] = 0x01;
}


static void
i450kx_close(void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;

    smram_del(dev->smram[1]);
    smram_del(dev->smram[0]);
    free(dev);
}


static void *
i450kx_init(const device_t *info)
{
    i450kx_t *dev = (i450kx_t *)malloc(sizeof(i450kx_t));
    memset(dev, 0, sizeof(i450kx_t));
    pci_add_card(PCI_ADD_NORTHBRIDGE, pb_read, pb_write, dev);	/* Device 19h: Intel 450KX PCI Bridge PB */
    pci_add_card(PCI_ADD_AGPBRIDGE, mc_read, mc_write, dev);	/* Device 14h: Intel 450KX Memory Controller MC */

    dev->smram[0] = smram_add();
    dev->smram[1] = smram_add();

    cpu_cache_int_enabled = 1;
    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();

    i450kx_reset(dev);

    return dev;
}

const device_t i450kx_device = {
    .name = "Intel 450KX (Mars)",
    .internal_name = "i450kx",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = i450kx_init,
    .close = i450kx_close,
    .reset = i450kx_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
