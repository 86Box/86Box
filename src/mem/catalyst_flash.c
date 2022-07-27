/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 1 Mbit and 2 Mbit, 8-bit and
 *		16-bit flash devices.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/plat.h>


#define FLAG_WORD	4
#define FLAG_BXB	2
#define FLAG_INV_A16	1


enum
{
    BLOCK_MAIN1,
    BLOCK_MAIN2,
    BLOCK_DATA1,
    BLOCK_DATA2,
    BLOCK_BOOT,
    BLOCKS_NUM
};

enum
{
    CMD_SET_READ = 0x00,
    CMD_READ_SIGNATURE = 0x90,
    CMD_ERASE = 0x20,
    CMD_ERASE_CONFIRM = 0x20,
    CMD_ERASE_VERIFY = 0xA0,
    CMD_PROGRAM = 0x40,
    CMD_PROGRAM_VERIFY = 0xC0,
    CMD_RESET = 0xFF
};


typedef struct flash_t
{
    uint8_t		command, pad,
			pad0, pad1,
			*array;

    mem_mapping_t	mapping, mapping_h[2];
} flash_t;


static char	flash_path[1024];


static uint8_t
flash_read(uint32_t addr, void *p)
{
    flash_t *dev = (flash_t *) p;
    uint8_t ret = 0xff;

    addr &= biosmask;

    switch (dev->command) {
	case CMD_ERASE_VERIFY:
	case CMD_PROGRAM_VERIFY:
	case CMD_RESET:
	case CMD_SET_READ:
		ret = dev->array[addr];
		break;

	case CMD_READ_SIGNATURE:
		if (addr == 0x00000)
			ret = 0x31;	/* CATALYST */
		else if (addr == 0x00001)
			ret = 0xB4;	/* 28F010 */
		break;
    }

    return ret;
}


static uint16_t
flash_readw(uint32_t addr, void *p)
{
    flash_t *dev = (flash_t *)p;
    uint16_t *q;

    addr &= biosmask;

    q = (uint16_t *)&(dev->array[addr]);

    return *q;
}


static uint32_t
flash_readl(uint32_t addr, void *p)
{
    flash_t *dev = (flash_t *)p;
    uint32_t *q;

    addr &= biosmask;

    q = (uint32_t *)&(dev->array[addr]);

    return *q;
}


static void
flash_write(uint32_t addr, uint8_t val, void *p)
{
    flash_t *dev = (flash_t *) p;

    addr &= biosmask;

    switch (dev->command) {
	case CMD_ERASE:
		if (val == CMD_ERASE_CONFIRM)
			memset(dev->array, 0xff, biosmask + 1);
		break;

	case CMD_PROGRAM:
		dev->array[addr] = val;
		break;

	default:
		dev->command = val;
		break;
    }
}


static void
flash_writew(uint32_t addr, uint16_t val, void *p)
{
}


static void
flash_writel(uint32_t addr, uint32_t val, void *p)
{
}


static void
catalyst_flash_add_mappings(flash_t *dev)
{
    memcpy(dev->array, rom, biosmask + 1);

    mem_mapping_add(&dev->mapping, 0xe0000, 0x20000,
		    flash_read, flash_readw, flash_readl,
		    flash_write, flash_writew, flash_writel,
		    dev->array, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM|MEM_MAPPING_ROMCS, (void *) dev);

    mem_mapping_add(&(dev->mapping_h[0]), 0xfffc0000, 0x20000,
		    flash_read, flash_readw, flash_readl,
		    flash_write, flash_writew, flash_writel,
		    dev->array, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM|MEM_MAPPING_ROMCS, (void *) dev);
    mem_mapping_add(&(dev->mapping_h[1]), 0xfffe0000, 0x20000,
		    flash_read, flash_readw, flash_readl,
		    flash_write, flash_writew, flash_writel,
		    dev->array, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM|MEM_MAPPING_ROMCS, (void *) dev);
}


static void
catalyst_flash_reset(void *priv)
{
    flash_t *dev = (flash_t *) priv;

    dev->command = CMD_RESET;
}


static void *
catalyst_flash_init(const device_t *info)
{
    FILE *f;
    flash_t *dev;

    dev = malloc(sizeof(flash_t));
    memset(dev, 0, sizeof(flash_t));

    sprintf(flash_path, "%s.bin", machine_get_internal_name_ex(machine));

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    dev->array = (uint8_t *) malloc(0x20000);
    memset(dev->array, 0xff, 0x20000);

    catalyst_flash_add_mappings(dev);

    dev->command = CMD_RESET;

    f = nvr_fopen(flash_path, "rb");
    if (f) {
	fread(dev->array, 0x20000, 1, f);
	fclose(f);
    }

    return dev;
}


static void
catalyst_flash_close(void *p)
{
    FILE *f;
    flash_t *dev = (flash_t *)p;

    f = nvr_fopen(flash_path, "wb");
    fwrite(dev->array, 0x20000, 1, f);
    fclose(f);

    free(dev->array);
    dev->array = NULL;

    free(dev);
}


const device_t catalyst_flash_device = {
    .name = "Catalyst 28F010-D Flash BIOS",
    .internal_name = "catalyst_flash",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = catalyst_flash_init,
    .close = catalyst_flash_close,
    .reset = catalyst_flash_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
