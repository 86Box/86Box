/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the VIA Apollo series of chips.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Melissa Goad, <mszoopers@protonmail.com>
 *		Tiseno100,
 *
 *		Copyright 2020 Miran Grca.
 *		Copyright 2020 Melissa Goad.
 *		Copyright 2020 Tiseno100.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/pci.h>
#include <86box/keyboard.h>
#include <86box/chipset.h>
#include <86box/spd.h>


typedef struct via_apollo_t
{
    uint16_t	id;
    uint8_t	pci_conf[256];

    smram_t	*smram;
} via_apollo_t;


static void
apollo_map(uint32_t addr, uint32_t size, int state)
{
    switch (state & 3) {
	case 0:
		mem_set_mem_state_both(addr, size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
		break;
	case 1:
		mem_set_mem_state_both(addr, size, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
		break;
	case 2:
		mem_set_mem_state_both(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
		break;
	case 3:
		mem_set_mem_state_both(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		break;
    }

    flushmmucache_nopc();
}


static void
apollo_smram_map(via_apollo_t *dev, int smm, uint32_t host_base, uint32_t size, int is_smram)
{
    if (((is_smram & 0x03) == 0x01) || ((is_smram & 0x03) == 0x02))
	smram_enable(dev->smram, host_base, 0x000a0000, size, 0, 1);

    mem_set_mem_state_smram_ex(smm, host_base, size, is_smram & 0x03);
    flushmmucache();
}


static void
via_apollo_setup(via_apollo_t *dev)
{
    /* Host Bridge */
    dev->pci_conf[0x00] = 0x06; /*VIA*/
    dev->pci_conf[0x01] = 0x11;
    dev->pci_conf[0x02] = dev->id & 0xff;
    dev->pci_conf[0x03] = (dev->id >> 8);

    dev->pci_conf[0x04] = 6;
    dev->pci_conf[0x05] = 0;

    dev->pci_conf[0x06] = 0x90;
    dev->pci_conf[0x07] = 0x02;

    if (dev->id == 0x0597)
	dev->pci_conf[0x08] = 1;	/* Production Silicon ("Revision B") */
    dev->pci_conf[0x09] = 0;
    dev->pci_conf[0x0a] = 0;
    dev->pci_conf[0x0b] = 6;
    dev->pci_conf[0x0c] = 0;
    dev->pci_conf[0x0d] = 0;
    dev->pci_conf[0x0e] = 0;
    dev->pci_conf[0x0f] = 0;
    dev->pci_conf[0x10] = 0x08;
    dev->pci_conf[0x34] = 0xa0;

    if (dev->id == 0x0691) {
 	dev->pci_conf[0x56] = 0x01;
	dev->pci_conf[0x57] = 0x01;
    }
    dev->pci_conf[0x5a] = 0x01;
    dev->pci_conf[0x5b] = 0x01;
    dev->pci_conf[0x5c] = 0x01;
    dev->pci_conf[0x5d] = 0x01;
    dev->pci_conf[0x5e] = 0x01;
    dev->pci_conf[0x5f] = 0x01;

    dev->pci_conf[0x64] = 0xec;
    dev->pci_conf[0x65] = 0xec;
    dev->pci_conf[0x66] = 0xec;
    if (dev->id == 0x0691)
	dev->pci_conf[0x67] = 0xec;	/* DRAM Timing for Banks 6,7. */
    dev->pci_conf[0x6b] = 0x01;

    dev->pci_conf[0xa0] = 0x02;
    dev->pci_conf[0xa2] = 0x10;
    dev->pci_conf[0xa4] = 0x03;
    dev->pci_conf[0xa5] = 0x02;
    dev->pci_conf[0xa7] = 0x07;
}


static void
via_apollo_host_bridge_write(int func, int addr, uint8_t val, void *priv)
{
    via_apollo_t *dev = (via_apollo_t *) priv;

    if (func)
	return;

    /*Read-only addresses*/
    if ((addr < 4) || ((addr >= 5) && (addr < 7)) || ((addr >= 8) && (addr < 0xd)) ||
	((addr >= 0xe) && (addr < 0x12)) || ((addr >= 0x14) && (addr < 0x50)) ||
	(addr == 0x69) || ((addr >= 0x79) && (addr < 0x7e)) ||
	((addr >= 0x81) && (addr < 0x84)) || ((addr >= 0x85) && (addr < 0x88)) ||
	((addr >= 0x8c) && (addr < 0xa8)) || ((addr >= 0xaa) && (addr < 0xac)) ||
	((addr >= 0xad) && (addr < 0xf0)) || ((addr >= 0xf8) && (addr < 0xfc)) ||
	(addr == 0xfd))
	return;
    if (((addr == 0x78) || (addr >= 0xad)) && (dev->id == 0x0597))
	return;
    if (((addr == 0x67) || ((addr >= 0xf0) && (addr < 0xfc))) && (dev->id != 0x0691))
	return;

    switch(addr) {
	case 0x04:
		dev->pci_conf[0x04] = (dev->pci_conf[0x04] & ~0x40) | (val & 0x40);
		break;
	case 0x07:
		dev->pci_conf[0x07] &= ~(val & 0xb0);
		break;
	case 0x0d:
		dev->pci_conf[0x0d] = (dev->pci_conf[0x0d] & ~0x07) | (val & 0x07);
		dev->pci_conf[0x75] = (dev->pci_conf[0x75] & ~0x30) | ((val & 0x06) << 3);
		break;

	case 0x12:	/* Graphics Aperture Base */
		dev->pci_conf[0x12] = (val & 0xf0);
		break;
	case 0x13:	/* Graphics Aperture Base */
		dev->pci_conf[0x13] = val;
		break;

	case 0x50:	/* Cache Control 1 */
		if (dev->id == 0x0691)
			dev->pci_conf[0x50] = val;
		else
			dev->pci_conf[0x50] = (dev->pci_conf[0x50] & ~0xf8) | (val & 0xf8);
		break;
	case 0x51:	/* Cache Control 2 */
		if (dev->id == 0x0691)
			dev->pci_conf[0x51] = val;
		else
			dev->pci_conf[0x51] = (dev->pci_conf[0x51] & ~0xeb) | (val & 0xeb);
		break;
	case 0x52:	/* Non_Cacheable Control */
		if (dev->id == 0x0691)
			dev->pci_conf[0x52] = (dev->pci_conf[0x52] & ~0x9f) | (val & 0x9f);
		else
			dev->pci_conf[0x52] = (dev->pci_conf[0x52] & ~0xf5) | (val & 0xf5);
		break;
	case 0x53:	/* System Performance Control */
		if (dev->id == 0x0691)
			dev->pci_conf[0x53] = val;
		else
			dev->pci_conf[0x53] = (dev->pci_conf[0x53] & ~0xf0) | (val & 0xf0);
		break;

	case 0x56: case 0x57: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: /* DRAM Row Ending Address */
		if (dev->id >= 0x0691)
			spd_write_drbs(dev->pci_conf, 0x5a, 0x56, 8);
		else if (addr >= 0x5a)
			spd_write_drbs(dev->pci_conf, 0x5a, 0x5f, 8);
		break;

	case 0x58:
		if (dev->id == 0x0597)
			dev->pci_conf[0x58] = (dev->pci_conf[0x58] & ~0xee) | (val & 0xee);
		else
			dev->pci_conf[0x58] = val;
		break;
	case 0x59:
		if (dev->id == 0x0691)
			dev->pci_conf[0x59] = val;
		else
			dev->pci_conf[0x59] = (dev->pci_conf[0x59] & ~0xf0) | (val & 0xf0);
		break;

	case 0x61:	/* Shadow RAM Control 1 */
		if ((dev->pci_conf[0x61] ^ val) & 0x03)
			apollo_map(0xc0000, 0x04000, val & 0x03);
		if ((dev->pci_conf[0x61] ^ val) & 0x0c)
			apollo_map(0xc4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->pci_conf[0x61] ^ val) & 0x30)
			apollo_map(0xc8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->pci_conf[0x61] ^ val) & 0xc0)
			apollo_map(0xcc000, 0x04000, (val & 0xc0) >> 6);
		dev->pci_conf[0x61] = val;
		break;
	case 0x62:	/* Shadow RAM Control 2 */
		if ((dev->pci_conf[0x62] ^ val) & 0x03)
			apollo_map(0xd0000, 0x04000, val & 0x03);
		if ((dev->pci_conf[0x62] ^ val) & 0x0c)
			apollo_map(0xd4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->pci_conf[0x62] ^ val) & 0x30)
			apollo_map(0xd8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->pci_conf[0x62] ^ val) & 0xc0)
			apollo_map(0xdc000, 0x04000, (val & 0xc0) >> 6);
		dev->pci_conf[0x62] = val;
		break;
	case 0x63:	/* Shadow RAM Control 3 */
		if ((dev->pci_conf[0x63] ^ val) & 0x30) {
			apollo_map(0xf0000, 0x10000, (val & 0x30) >> 4);
			shadowbios = (((val & 0x30) >> 4) & 0x02);
		}
		if ((dev->pci_conf[0x63] ^ val) & 0xc0)
			apollo_map(0xe0000, 0x10000, (val & 0xc0) >> 6);
		dev->pci_conf[0x63] = val;
		smram_disable_all();
		if (dev->id == 0x0691) switch (val & 0x03) {
			case 0x00:
			default:
				apollo_smram_map(dev, 1, 0x000a0000, 0x00020000, 1);	/* SMM: Code DRAM, Data DRAM */
				apollo_smram_map(dev, 0, 0x000a0000, 0x00020000, 0);	/* Non-SMM: Code PCI, Data PCI */
				break;
			case 0x01:
				apollo_smram_map(dev, 1, 0x000a0000, 0x00020000, 1);	/* SMM: Code DRAM, Data DRAM */
				apollo_smram_map(dev, 0, 0x000a0000, 0x00020000, 1);	/* Non-SMM: Code DRAM, Data DRAM */
				break;
			case 0x02:
				apollo_smram_map(dev, 1, 0x000a0000, 0x00020000, 3);	/* SMM: Code Invalid, Data Invalid */
				apollo_smram_map(dev, 0, 0x000a0000, 0x00020000, 2);	/* Non-SMM: Code DRAM, Data PCI */
				break;
			case 0x03:
				apollo_smram_map(dev, 1, 0x000a0000, 0x00020000, 1);	/* SMM: Code DRAM, Data DRAM */
				apollo_smram_map(dev, 0, 0x000a0000, 0x00020000, 3);	/* Non-SMM: Code Invalid, Data Invalid */
				break;
		} else  switch (val & 0x03) {
			case 0x00:
			default:
				/* Disable SMI Address Redirection (default) */
				apollo_smram_map(dev, 1, 0x000a0000, 0x00020000, 0);
				if (dev->id == 0x0597)
					apollo_smram_map(dev, 1, 0x00030000, 0x00020000, 1);
				apollo_smram_map(dev, 0, 0x000a0000, 0x00020000, 0);
				break;
			case 0x01:
				/* Allow access to DRAM Axxxx-Bxxxx for both normal and SMI cycles */
				apollo_smram_map(dev, 1, 0x000a0000, 0x00020000, 1);
				if (dev->id == 0x0597)
					apollo_smram_map(dev, 1, 0x00030000, 0x00020000, 1);
				apollo_smram_map(dev, 0, 0x000a0000, 0x00020000, 1);
				break;
			case 0x02:
				/* Reserved */
				apollo_smram_map(dev, 1, 0x000a0000, 0x00020000, 3);
				if (dev->id == 0x0597) {
					/* SMI 3xxxx-4xxxx redirect to Axxxx-Bxxxx. */
					apollo_smram_map(dev, 1, 0x00030000, 0x00020000, 1);
				}
				apollo_smram_map(dev, 0, 0x000a0000, 0x00020000, 3);
				break;
			case 0x03:
				/* Allow SMI Axxxx-Bxxxx DRAM access */
				apollo_smram_map(dev, 1, 0x000a0000, 0x00020000, 1);
				if (dev->id == 0x0597)
					apollo_smram_map(dev, 1, 0x00030000, 0x00020000, 1);
				apollo_smram_map(dev, 0, 0x000a0000, 0x00020000, 0);
				break;
		}
		break;
	case 0x68:
		if (dev->id == 0x0597)
			dev->pci_conf[0x68] = (dev->pci_conf[0x6b] & ~0xfe) | (val & 0xfe);
		else if (dev->id == 0x0598)
			dev->pci_conf[0x68] = val;
		else
			dev->pci_conf[0x68] = (dev->pci_conf[0x6b] & ~0xfd) | (val & 0xfd);
		break;
	case 0x6b:
		if (dev->id == 0x0691)
			dev->pci_conf[0x6b] = (dev->pci_conf[0x6b] & ~0xcf) | (val & 0xcf);
		else
			dev->pci_conf[0x6b] = (dev->pci_conf[0x6b] & ~0xc1) | (val & 0xc1);
		break;
	case 0x6c:
		if (dev->id == 0x0597)
			dev->pci_conf[0x6c] = (dev->pci_conf[0x6c] & ~0x1f) | (val & 0x1f);
		else if (dev->id == 0x0598)
			dev->pci_conf[0x6c] = (dev->pci_conf[0x6c] & ~0x7f) | (val & 0x7f);
		else
			dev->pci_conf[0x6c] = val;
		break;
	case 0x6d:
		if (dev->id == 0x0597)
			dev->pci_conf[0x6d] = (dev->pci_conf[0x6d] & ~0x0f) | (val & 0x0f);
		else if (dev->id == 0x0598)
			dev->pci_conf[0x6d] = (dev->pci_conf[0x6d] & ~0x7f) | (val & 0x7f);
		else
			dev->pci_conf[0x6d] = val;
		break;
	case 0x6e:
		dev->pci_conf[0x6e] = (dev->pci_conf[0x6e] & ~0xb7) | (val & 0xb7);
		break;

	case 0x70:
		if (dev->id == 0x0597)
			dev->pci_conf[0x70] = (dev->pci_conf[0x70] & ~0xf1) | (val & 0xf1);
		else
			dev->pci_conf[0x70] = val;
		break;
	case 0x74:
		dev->pci_conf[0x74] = (dev->pci_conf[0x74] & ~0xc0) | (val & 0xc0);
		break;
	case 0x75:
		dev->pci_conf[0x75] = (dev->pci_conf[0x75] & ~0xcf) | (val & 0xcf);
		break;
	case 0x76:
		dev->pci_conf[0x76] = (dev->pci_conf[0x76] & ~0xf0) | (val & 0xf0);
		break;
	case 0x77:
		dev->pci_conf[0x77] = (dev->pci_conf[0x77] & ~0xc0) | (val & 0xc0);
		break;
	case 0x7e:
		dev->pci_conf[0x7e] = (dev->pci_conf[0x7e] & ~0x3f) | (val & 0x3f);
		break;

	case 0x80:
		dev->pci_conf[0x80] = (dev->pci_conf[0x80] & ~0x8f) | (val & 0x8f);
		break;
	case 0x84:
		/* The datasheet first mentions 7-0 but then says 3-0 are reserved -
		   - minimum of 16 MB for the graphics aperture? */
		dev->pci_conf[0x84] = (dev->pci_conf[0x84] & ~0xf0) | (val & 0xf0);
		break;
	case 0x88:
		dev->pci_conf[0x88] = (dev->pci_conf[0x88] & ~0x07) | (val & 0x07);
		break;
	case 0x89:
		dev->pci_conf[0x89] = (dev->pci_conf[0x89] & ~0xf0) | (val & 0xf0);
		break;

	case 0xa8:
		dev->pci_conf[0xa8] = (dev->pci_conf[0xa8] & ~0x03) | (val & 0x03);
		break;
	case 0xa9:
		dev->pci_conf[0xa9] = (dev->pci_conf[0xa9] & ~0x03) | (val & 0x03);
		break;
	case 0xac:
		dev->pci_conf[0xac] = (dev->pci_conf[0xac] & ~0x0f) | (val & 0x0f);
		break;
	case 0xfc:
		if (dev->id > 0x0597)
			dev->pci_conf[0xfc] = (dev->pci_conf[0xfc] & ~0x01) | (val & 0x01);
		break;

	default:
		dev->pci_conf[addr] = val;
		break;
    }
}


static uint8_t
via_apollo_read(int func, int addr, void *priv)
{
    via_apollo_t *dev = (via_apollo_t *) priv;
    uint8_t ret = 0xff;

    switch(func) {
        case 0:
		ret = dev->pci_conf[addr];
		break;
    }

    return ret;
}


static void
via_apollo_write(int func, int addr, uint8_t val, void *priv)
{
    switch(func) {
	case 0:
		via_apollo_host_bridge_write(func, addr, val, priv);
		break;
    }
}


static void
via_apollo_reset(void *priv)
{
    via_apollo_write(0, 0x61, 0x00, priv);
    via_apollo_write(0, 0x62, 0x00, priv);
    via_apollo_write(0, 0x63, 0x00, priv);
}


static void *
via_apollo_init(const device_t *info)
{
    via_apollo_t *dev = (via_apollo_t *) malloc(sizeof(via_apollo_t));
    memset(dev, 0, sizeof(via_apollo_t));

    dev->smram = smram_add();
    apollo_smram_map(dev, 1, 0x000a0000, 0x00020000, 1);	/* SMM: Code DRAM, Data DRAM */

    pci_add_card(PCI_ADD_NORTHBRIDGE, via_apollo_read, via_apollo_write, dev);

    dev->id = info->local;

    switch (dev->id) {
	case 0x0597:
		device_add(&via_vp3_agp_device);
		break;

	case 0x0598:
		device_add(&via_mvp3_agp_device);
		break;

	case 0x0691:
		device_add(&via_apro_agp_device);
		break;
    }

    via_apollo_setup(dev);
    via_apollo_reset(dev);

    return dev;
}


static void
via_apollo_close(void *priv)
{
    via_apollo_t *dev = (via_apollo_t *) priv;

    smram_del(dev->smram);

    free(dev);
}


const device_t via_vp3_device =
{
    "VIA Apollo VP3",
    DEVICE_PCI,
    0x0597,	/*VT82C597*/
    via_apollo_init, 
    via_apollo_close, 
    via_apollo_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t via_mvp3_device =
{
    "VIA Apollo MVP3",
    DEVICE_PCI,
    0x0598,	/*VT82C598MVP*/
    via_apollo_init, 
    via_apollo_close, 
    via_apollo_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t via_apro_device = {
    "VIA Apollo Pro",
    DEVICE_PCI,
    0x0691,	/*VT82C691*/
    via_apollo_init,
    via_apollo_close,
    via_apollo_reset,
    NULL,
    NULL,
    NULL,
    NULL
};
