/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the STPC series of SoCs.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/port_92.h>
#include <86box/usb.h>
#include <86box/chipset.h>


#define STPC_NB_CLIENT		0x01
#define STPC_ISAB_CLIENT	0x02
#define STPC_ISAB_CONSUMER2	0x04
#define STPC_IDE_ATLAS		0x08
#define STPC_USB		0x10


typedef struct stpc_t
{
    uint32_t	local;

    /* Main registers (port 22h/23h) */
    uint8_t	reg_offset;
    uint8_t	regs[256];

    /* Host bus interface */
    uint16_t	host_base;
    uint8_t	host_offset;
    uint8_t	host_regs[256];

    /* Local bus */
    uint16_t	localbus_base;
    uint8_t	localbus_offset;
    uint8_t	localbus_regs[256];

    /* PCI devices */
    uint8_t	pci_conf[4][256];
    usb_t	*usb;
} stpc_t;


#define ENABLE_STPC_LOG 1
#ifdef ENABLE_STPC_LOG
int stpc_do_log = ENABLE_STPC_LOG;


static void
stpc_log(const char *fmt, ...)
{
    va_list ap;

    if (stpc_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define stpc_log(fmt, ...)
#endif


static void
stpc_recalcmapping(stpc_t *dev)
{
    uint8_t reg, bitpair;
    uint32_t base, size;
    int state;

    shadowbios = 0;
    shadowbios_write = 0;

    for (reg = 0; reg <= 3; reg++) {
	for (bitpair = 0; bitpair <= (reg == 3 ? 0 : 3); bitpair++) {
		if (reg == 3) {
			size = 0x10000;
			base = 0xf0000;
		} else {
			size = 0x4000;
			base = 0xc0000 + (size * ((reg * 4) + bitpair));
		}
		stpc_log("STPC: Shadowing for %05x-%05x (reg %02x bp %d wmask %02x rmask %02x) =", base, base + size - 1, 0x25 + reg, bitpair, 1 << (bitpair * 2), 1 << ((bitpair * 2) + 1));

		state = 0;
		if (dev->regs[0x25 + reg] & (1 << (bitpair * 2))) {
			stpc_log(" w on");
			state |= MEM_WRITE_INTERNAL;
			if (base >= 0xe0000)
				shadowbios_write |= 1;
		} else {
			stpc_log(" w off");
			state |= MEM_WRITE_EXTANY;
		}
		if (dev->regs[0x25 + reg] & (1 << ((bitpair * 2) + 1))) {
			stpc_log("; r on\n");
			state |= MEM_READ_INTERNAL;
			if (base >= 0xe0000)
				shadowbios |= 1;
		} else {
			stpc_log("; r off\n");
			state |= MEM_READ_EXTANY;
		}

		mem_set_mem_state(base, size, state);
	}
    }

    flushmmucache();
}


static void
stpc_smram_map(int smm, uint32_t addr, uint32_t size, int is_smram)
{
    mem_set_mem_state_smram(smm, addr, size, is_smram);
}


static void
stpc_host_write(uint16_t addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: host_write(%04x, %02x)\n", addr, val);

    if (addr == dev->host_base)
	dev->host_offset = val;
    else if (addr == dev->host_base + 4)
	dev->host_regs[dev->host_offset] = val;
}


static uint8_t
stpc_host_read(uint16_t addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (addr == dev->host_base)
	ret = dev->host_offset;
    else if (addr == dev->host_base + 4)
	ret = dev->host_regs[dev->host_offset];
    else
	ret = 0xff;

    stpc_log("STPC: host_read(%04x) = %02x\n", addr, ret);
    return ret;
}


static void
stpc_localbus_write(uint16_t addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: localbus_write(%04x, %02x)\n", addr, val);

    if (addr == dev->localbus_base)
	dev->localbus_offset = val;
    else if (addr == dev->localbus_base + 4)
	dev->localbus_regs[addr] = val;
}


static uint8_t
stpc_localbus_read(uint16_t addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (addr == dev->localbus_base)
	ret = dev->localbus_offset;
    else if (addr == dev->localbus_base + 4)
	ret = dev->localbus_regs[dev->localbus_offset];
    else
	ret = 0xff;

    stpc_log("STPC: localbus_read(%04x) = %02x\n", addr, ret);
    return ret;
}


static void
stpc_nb_write(int func, int addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: nb_write(%d, %02x, %02x)\n", func, addr, val);

    if (func > 0)
	return;

    switch (addr) {
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x04: case 0x06: case 0x07: case 0x08:
	case 0x09: case 0x0a: case 0x0b: case 0x0e:
	case 0x51: case 0x53: case 0x54:
		return;

	case 0x05:
		val &= 0x01;
		break;

	case 0x50:
		val &= 0x1f;
		break;

	case 0x52:
		val &= 0x70;
		break;
    }

    dev->pci_conf[0][addr] = val;
}


static uint8_t
stpc_nb_read(int func, int addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (func > 0)
	ret = 0xff;
    else
	ret = dev->pci_conf[0][addr];

    stpc_log("STPC: nb_read(%d, %02x) = %02x\n", func, addr, ret);
    return ret;
}


static void
stpc_ide_write(int func, int addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: ide_write(%d, %02x, %02x)\n", func, addr, val);

    if (func > 0)
	return;

    switch (addr) {
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x04: case 0x06: case 0x07: case 0x08:
	case 0x09: case 0x0a: case 0x0b: case 0x0e:
		return;

	case 0x05:
		val &= 0x01;
		break;
    }

    dev->pci_conf[2][addr] = val;
}


static uint8_t
stpc_ide_read(int func, int addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (func > 0)
    	ret = 0xff;
    else
    	ret = dev->pci_conf[2][addr];

    stpc_log("STPC: ide_read(%d, %02x) = %02x\n", func, addr, ret);
    return ret;
}


static void
stpc_isab_write(int func, int addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    if (func == 1 && !(dev->local & STPC_IDE_ATLAS)) {
    	stpc_ide_write(0, addr, val, priv);
    	return;
    }

    stpc_log("STPC: isab_write(%d, %02x, %02x)\n", func, addr, val);

    if (func > 0)
	return;

    switch (addr) {
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x04: case 0x06: case 0x07: case 0x08:
	case 0x09: case 0x0a: case 0x0b: case 0x0e:
		return;

	case 0x05:
		val &= 0x01;
		break;
    }

    dev->pci_conf[1][addr] = val;
}


static uint8_t
stpc_isab_read(int func, int addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (func == 1 && !(dev->local & STPC_IDE_ATLAS))
    	return stpc_ide_read(0, addr, priv);
    else if (func > 0)
    	ret = 0xff;
    else
    	ret = dev->pci_conf[1][addr];

    stpc_log("STPC: isab_read(%d, %02x) = %02x\n", func, addr, ret);
    return ret;
}


static void
stpc_usb_write(int func, int addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: usb_write(%d, %02x, %02x)\n", func, addr, val);

    if (func > 0)
	return;

    switch (addr) {
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x04: case 0x06: case 0x07: case 0x08:
	case 0x09: case 0x0a: case 0x0b: case 0x0e:
	case 0x10:
		return;

	case 0x05:
		val &= 0x01;
		break;

	case 0x11:
		dev->pci_conf[3][addr] = val & 0xf0;
		ohci_update_mem_mapping(dev->usb, dev->pci_conf[3][0x11], dev->pci_conf[3][0x12], dev->pci_conf[3][0x13], 1);
		break;

	case 0x12: case 0x13:
		dev->pci_conf[3][addr] = val;
		ohci_update_mem_mapping(dev->usb, dev->pci_conf[3][0x11], dev->pci_conf[3][0x12], dev->pci_conf[3][0x13], 1);
		break;
    }

    dev->pci_conf[3][addr] = val;
}


static uint8_t
stpc_usb_read(int func, int addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (func > 0)
    	ret = 0xff;
    else
    	ret = dev->pci_conf[3][addr];

    stpc_log("STPC: usb_read(%d, %02x) = %02x\n", func, addr, ret);
    return ret;
}


static void
stpc_remap_host(stpc_t *dev, uint16_t host_base)
{
    stpc_log("STPC: Remapping host bus from %04x to %04x\n", dev->host_base, host_base);

    io_removehandler(dev->host_base, 5,
		     stpc_host_read, NULL, NULL, stpc_host_write, NULL, NULL, dev);
    if (host_base) {
	io_sethandler(host_base, 5,
		      stpc_host_read, NULL, NULL, stpc_host_write, NULL, NULL, dev);
    }
    dev->host_base = host_base;
}


static void
stpc_remap_localbus(stpc_t *dev, uint16_t localbus_base)
{
    stpc_log("STPC: Remapping local bus from %04x to %04x\n", dev->localbus_base, localbus_base);

    io_removehandler(dev->localbus_base, 5,
		     stpc_localbus_read, NULL, NULL, stpc_localbus_write, NULL, NULL, dev);
    if (localbus_base) {
	io_sethandler(localbus_base, 5,
		      stpc_localbus_read, NULL, NULL, stpc_localbus_write, NULL, NULL, dev);
    }
    dev->localbus_base = localbus_base;
}


static void
stpc_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: reg_write(%04x, %02x)\n", addr, val);

    if (addr == 0x22) {
	dev->reg_offset = val;
    } else {
	stpc_log("STPC: regs[%02x] = %02x\n", dev->reg_offset, val);

	switch (dev->reg_offset) {
		case 0x12:
			if (dev->regs[0x10] == 0x07)
				stpc_remap_host(dev, (dev->host_base & 0xff00) | val);
			else if (dev->regs[0x10] == 0x06)
				stpc_remap_localbus(dev, (dev->localbus_base & 0xff00) | val);
			break;

		case 0x13:
			if (dev->regs[0x10] == 0x07)
				stpc_remap_host(dev, (dev->host_base & 0x00ff) | (val << 8));
			else if (dev->regs[0x10] == 0x06)
				stpc_remap_localbus(dev, (dev->localbus_base & 0x00ff) | (val << 8));
			break;

		case 0x21:
			val &= 0xfe;
			break;

		case 0x22:
			val &= 0x7f;
			break;

		case 0x25: case 0x26: case 0x27: case 0x28:
			if (dev->reg_offset == 0x28) {
				val &= 0xe3;
				stpc_smram_map(0, smram[0].host_base, smram[0].size, !!(val & 0x80));
			}
			dev->regs[dev->reg_offset] = val;
			stpc_recalcmapping(dev);
			break;

		case 0x29:
			val &= 0x0f;
			break;

		case 0x36:
			val &= 0x3f;
			break;
	}

	dev->regs[dev->reg_offset] = val;
    }
}


static uint8_t
stpc_reg_read(uint16_t addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (addr == 0x22)
	ret = dev->reg_offset;
    else if (dev->reg_offset >= 0xc0)
    	return 0xff; /* Cyrix CPU registers: let the CPU code handle those */
    else
	ret = dev->regs[dev->reg_offset];

    stpc_log("STPC: reg_read(%04x) = %02x\n", addr, ret);
    return ret;
}


static void
stpc_reset(void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: reset()\n");

    memset(dev->regs, 0, sizeof(dev->regs));
    dev->regs[0x7b] = 0xff;

    io_removehandler(0x22, 2,
		     stpc_reg_read, NULL, NULL, stpc_reg_write, NULL, NULL, dev);
    io_sethandler(0x22, 2,
		  stpc_reg_read, NULL, NULL, stpc_reg_write, NULL, NULL, dev);
}


static void
stpc_setup(stpc_t *dev)
{
    stpc_log("STPC: setup()\n");

    uint32_t local = dev->local;
    memset(dev, 0, sizeof(stpc_t));
    dev->local = local;

    /* Northbridge */
    dev->pci_conf[0][0x00] = 0x4a;
    dev->pci_conf[0][0x01] = 0x10;
    if (dev->local & STPC_NB_CLIENT) {
    	dev->pci_conf[0][0x02] = 0x64;
    	dev->pci_conf[0][0x03] = 0x05;
    } else {
    	dev->pci_conf[0][0x02] = 0x0a;
    	dev->pci_conf[0][0x03] = 0x02;
    }

    dev->pci_conf[0][0x04] = 0x07;

    dev->pci_conf[0][0x06] = 0x80;
    dev->pci_conf[0][0x07] = 0x02;

    dev->pci_conf[0][0x0b] = 0x06;

    /* ISA Bridge */
    dev->pci_conf[1][0x00] = 0x4a;
    dev->pci_conf[1][0x01] = 0x10;
    if (dev->local & STPC_ISAB_CLIENT) {
    	dev->pci_conf[1][0x02] = 0xcc;
    	dev->pci_conf[1][0x03] = 0x55;
    } else if (dev->local & STPC_ISAB_CONSUMER2) {
    	dev->pci_conf[1][0x02] = 0x0b;
    	dev->pci_conf[1][0x03] = 0x02;
    } else {
    	dev->pci_conf[1][0x02] = 0x10;
    	dev->pci_conf[1][0x03] = 0x02;
    }

    dev->pci_conf[1][0x04] = 0x0f;

    dev->pci_conf[1][0x06] = 0x80;
    dev->pci_conf[1][0x07] = 0x02;

    dev->pci_conf[1][0x0a] = 0x01;
    dev->pci_conf[1][0x0b] = 0x06;

    dev->pci_conf[1][0x0e] = 0x40;

    /* IDE */
    dev->pci_conf[2][0x00] = 0x4a;
    dev->pci_conf[2][0x01] = 0x10;
    if (dev->local & STPC_IDE_ATLAS) {
    	dev->pci_conf[2][0x02] = 0x28;
    	dev->pci_conf[2][0x03] = 0x02;
    } else {
    	dev->pci_conf[2][0x02] = dev->pci_conf[1][0x02];
    	dev->pci_conf[2][0x03] = dev->pci_conf[1][0x03];
    }

    dev->pci_conf[2][0x06] = 0x80;
    dev->pci_conf[2][0x07] = 0x02;

    dev->pci_conf[2][0x09] = 0x8a;
    dev->pci_conf[2][0x0a] = 0x01;
    dev->pci_conf[2][0x0b] = 0x01;

    dev->pci_conf[2][0x0e] = 0x40;

    dev->pci_conf[2][0x10] = 0x01;
    dev->pci_conf[2][0x14] = 0x01;
    dev->pci_conf[2][0x18] = 0x01;
    dev->pci_conf[2][0x1c] = 0x01;

    dev->pci_conf[2][0x40] = 0x60;
    dev->pci_conf[2][0x41] = 0x97;
    dev->pci_conf[2][0x42] = 0x60;
    dev->pci_conf[2][0x43] = 0x97;
    dev->pci_conf[2][0x44] = 0x60;
    dev->pci_conf[2][0x45] = 0x97;
    dev->pci_conf[2][0x46] = 0x60;
    dev->pci_conf[2][0x47] = 0x97;

    /* USB */
    if (dev->local & STPC_USB) {
    	dev->pci_conf[3][0x00] = 0x4a;
    	dev->pci_conf[3][0x01] = 0x10;
    	dev->pci_conf[3][0x02] = 0x30;
    	dev->pci_conf[3][0x03] = 0x02;
	
    	dev->pci_conf[3][0x06] = 0x80;
    	dev->pci_conf[3][0x07] = 0x02;

    	dev->pci_conf[3][0x09] = 0x10;
    	dev->pci_conf[3][0x0a] = 0x03;
    	dev->pci_conf[3][0x0b] = 0x0c;

    	dev->pci_conf[3][0x0e] = 0x40;
    }
}


static void
stpc_close(void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: close()\n");

    free(dev);
}


static void *
stpc_init(const device_t *info)
{
    stpc_log("STPC: init()\n");

    stpc_t *dev = (stpc_t *) malloc(sizeof(stpc_t));
    dev->local = info->local;

    pci_add_card(0x0B, stpc_nb_read, stpc_nb_write, dev);
    pci_add_card(0x0C, stpc_isab_read, stpc_isab_write, dev);
    if (dev->local & STPC_IDE_ATLAS)
    	pci_add_card(0x0D, stpc_ide_read, stpc_ide_write, dev);
    if (dev->local & STPC_USB) {
    	dev->usb = device_add(&usb_device);
    	pci_add_card(0x0E, stpc_usb_read, stpc_usb_write, dev);
    }

    stpc_setup(dev);
    stpc_reset(dev);

    smram[0].host_base = 0x000a0000;
    smram[0].ram_base = 0x000a0000;
    smram[0].size = 0x00020000;

    mem_mapping_set_addr(&ram_smram_mapping[0], smram[0].host_base, smram[0].size);
    mem_mapping_set_exec(&ram_smram_mapping[0], ram + smram[0].ram_base);

    stpc_smram_map(0, smram[0].host_base, smram[0].size, 0);
    stpc_smram_map(1, smram[0].host_base, smram[0].size, 1);

    device_add(&port_92_pci_device);

    return dev;
}


const device_t stpc_client_device =
{
    "STPC Client",
    DEVICE_PCI,
    STPC_NB_CLIENT | STPC_ISAB_CLIENT,
    stpc_init, 
    stpc_close, 
    stpc_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t stpc_consumer2_device =
{
    "STPC Consumer-II",
    DEVICE_PCI,
    STPC_ISAB_CONSUMER2,
    stpc_init, 
    stpc_close, 
    stpc_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t stpc_elite_device =
{
    "STPC Elite",
    DEVICE_PCI,
    0,
    stpc_init, 
    stpc_close, 
    stpc_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t stpc_atlas_device =
{
    "STPC Atlas",
    DEVICE_PCI,
    STPC_IDE_ATLAS | STPC_USB,
    stpc_init, 
    stpc_close, 
    stpc_reset,
    NULL,
    NULL,
    NULL,
    NULL
};
