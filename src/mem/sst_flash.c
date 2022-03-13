/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of an SST flash chip.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Melissa Goad, <mszoopers@protonmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2020 Melissa Goad.
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
#include <86box/m_xt_xi8088.h>


typedef struct sst_t
{
    uint8_t		manufacturer, id, has_bbp, is_39,
			page_bytes, sdp, bbp_first_8k, bbp_last_8k;

    int			command_state, id_mode,
			dirty;

    uint32_t		size, mask,
			page_mask, page_base,
			last_addr;

    uint8_t		page_buffer[128],
			page_dirty[128];
    uint8_t		*array;

    mem_mapping_t	mapping[8], mapping_h[8];

    pc_timer_t		page_write_timer;
} sst_t;


static char	flash_path[1024];


#define SST_CHIP_ERASE    	0x10	/* Both 29 and 39, 6th cycle */
#define SST_SDP_DISABLE		0x20	/* Only 29, Software data protect disable and write - treat as write */
#define SST_SECTOR_ERASE  	0x30	/* Only 39, 6th cycle */
#define W_BOOT_BLOCK_PROT	0x40	/* Only W29C020 */
#define SST_SET_ID_MODE_ALT	0x60	/* Only 29, 6th cycle */
#define SST_ERASE         	0x80	/* Both 29 and 39 */
					/* With data 60h on 6th cycle, it's alt. ID */
#define SST_SET_ID_MODE   	0x90	/* Both 29 and 39 */
#define SST_BYTE_PROGRAM  	0xa0	/* Both 29 and 39 */
#define SST_CLEAR_ID_MODE 	0xf0	/* Both 29 and 39 */
					/* 1st cycle variant only on 39 */

#define SST			0xbf	/* SST Manufacturer's ID */
#define SST29EE010		0x0700
#define SST29LE_VE010		0x0800
#define SST29EE020		0x1000
#define SST29LE_VE020		0x1200
#define SST39SF512		0xb400
#define SST39SF010		0xb500
#define SST39SF020		0xb600
#define SST39SF040		0xb700

#define WINBOND			0xda	/* Winbond Manufacturer's ID */
#define W29C020			0x4500

#define SIZE_512K		0x010000
#define SIZE_1M			0x020000
#define SIZE_2M			0x040000
#define SIZE_4M			0x080000


static void
sst_sector_erase(sst_t *dev, uint32_t addr)
{
    uint32_t base = addr & (dev->mask & ~0xfff);

    if ((base < 0x2000) && (dev->bbp_first_8k & 0x01))
	return;
    else if ((base >= (dev->size - 0x2000)) && (dev->bbp_last_8k & 0x01))
	return;

    memset(&dev->array[base], 0xff, 4096);
    dev->dirty = 1;
}


static void
sst_new_command(sst_t *dev, uint32_t addr, uint8_t val)
{
    uint32_t base = 0x00000, size = dev->size;

    if (dev->command_state == 5)  switch (val) {
	case SST_CHIP_ERASE:
		if (dev->bbp_first_8k & 0x01) {
			base += 0x2000;
			size -= 0x2000;
		}

		if (dev->bbp_last_8k & 0x01)
			size -= 0x2000;

		memset(&(dev->array[base]), 0xff, size);
		dev->command_state = 0;
		break;

	case SST_SDP_DISABLE:
		if (!dev->is_39)
			dev->sdp = 0;
		dev->command_state = 0;
		break;

	case SST_SECTOR_ERASE:
		if (dev->is_39)
			sst_sector_erase(dev, addr);
		dev->command_state = 0;
		break;

	case SST_SET_ID_MODE_ALT:
		dev->id_mode = 1;
		dev->command_state = 0;
		break;

	default:
		dev->command_state = 0;
		break;
    } else  switch (val) {
	case SST_ERASE:
		dev->command_state = 3;
		break;

	case SST_SET_ID_MODE:
		dev->id_mode = 1;
		dev->command_state = 0;
		break;

	case SST_BYTE_PROGRAM:
		if (!dev->is_39) {
			dev->sdp = 1;
			memset(dev->page_buffer, 0xff, 128);
			memset(dev->page_dirty, 0x00, 128);
			dev->page_bytes = 0;
			dev->last_addr = 0xffffffff;
			timer_on_auto(&dev->page_write_timer, 210.0);
		}
		dev->command_state = 6;
		break;

	case W_BOOT_BLOCK_PROT:
		dev->command_state = dev->has_bbp ? 8 : 0;
		break;

	case SST_CLEAR_ID_MODE:
		dev->id_mode = 0;
		dev->command_state = 0;
		break;

	default:
		dev->command_state = 0;
		break;
    }
}


static void
sst_page_write(void *priv)
{
    sst_t *dev = (sst_t *) priv;
    int i;

    if (dev->last_addr != 0xffffffff) {
	dev->page_base = dev->last_addr & dev->page_mask;
	for (i = 0; i < 128; i++) {
		if (dev->page_dirty[i]) {
			if (((dev->page_base + i) < 0x2000) && (dev->bbp_first_8k & 0x01))
				continue;
			else if (((dev->page_base + i) >= (dev->size - 0x2000)) && (dev->bbp_last_8k & 0x01))
				continue;

			dev->array[dev->page_base + i] = dev->page_buffer[i];
			dev->dirty |= 1;
		}
	}
    }
    dev->page_bytes = 0;
    dev->command_state = 0;
    timer_disable(&dev->page_write_timer);
}


static uint8_t
sst_read_id(uint32_t addr, void *p)
{
    sst_t *dev = (sst_t *) p;
    uint8_t ret = 0x00;

    if ((addr & 0xffff) == 0)
	ret = dev->manufacturer;
    else if ((addr & 0xffff) == 1)
	ret = dev->id;
#ifdef UNKNOWN_FLASH
    else if ((addr & 0xffff) == 0x100)
	ret = 0x1c;
    else if ((addr & 0xffff) == 0x101)
	ret = 0x92;
#endif
    else if (dev->has_bbp) {
	if (addr == 0x00002)
		ret = dev->bbp_first_8k;
	else if (addr == 0x3fff2)
		ret = dev->bbp_last_8k;
    }

    return ret;
}


static void
sst_buf_write(sst_t *dev, uint32_t addr, uint8_t val)
{
    dev->page_buffer[addr & 0x0000007f] = val;
    dev->page_dirty[addr & 0x0000007f] = 1;
    dev->page_bytes++;
    dev->last_addr = addr;
    if (dev->page_bytes >= 128) {
	sst_page_write(dev);
    } else
	timer_on_auto(&dev->page_write_timer, 210.0);
}


static void
sst_write(uint32_t addr, uint8_t val, void *p)
{
    sst_t *dev = (sst_t *) p;

    switch (dev->command_state) {
	case 0:
	case 3:
		/* 1st and 4th Bus Write Cycle */
		if ((val == 0xf0) && dev->is_39 && (dev->command_state == 0)) {
			if (dev->id_mode)
				dev->id_mode = 0;
			dev->command_state = 0;
		} else if (((addr & 0x7fff) == 0x5555) && (val == 0xaa))
			dev->command_state++;
		else {
			if (!dev->is_39 && !dev->sdp && (dev->command_state == 0)) {
				/* 29 series, software data protection off, start loading the page. */
				memset(dev->page_buffer, 0xff, 128);
				memset(dev->page_dirty, 0x00, 128);
				dev->page_bytes = 0;
				dev->command_state = 7;
				sst_buf_write(dev, addr, val);
			} else
				dev->command_state = 0;
		}
		break;
	case 1:
	case 4:
		/* 2nd and 5th Bus Write Cycle */
		if (((addr & 0x7fff) == 0x2aaa) && (val == 0x55))
			dev->command_state++;
		else
			dev->command_state = 0;
		break;
	case 2:
	case 5:
		/* 3rd and 6th Bus Write Cycle */
		if ((dev->command_state == 5) && (val == SST_SECTOR_ERASE)) {
			/* Sector erase - can be on any address. */
			sst_new_command(dev, addr, val);
		} else if ((addr & 0x7fff) == 0x5555)
			sst_new_command(dev, addr, val);
		else
			dev->command_state = 0;
		break;
	case 6:
		/* Page Load Cycle (29) / Data Write Cycle (39SF) */
		if (dev->is_39) {
			dev->command_state = 0;

			dev->array[addr & dev->mask] = val;
			dev->dirty = 1;
		} else {
			dev->command_state++;
			sst_buf_write(dev, addr, val);
		}
		break;
	case 7:
		if (!dev->is_39)
			sst_buf_write(dev, addr, val);
		break;
	case 8:
		if ((addr == 0x00000) && (val == 0x00))
			dev->bbp_first_8k = 0xff;
		else if ((addr == 0x3ffff) && (val == 0xff))
			dev->bbp_last_8k = 0xff;
		dev->command_state = 0;
		break;
    }
}


static uint8_t
sst_read(uint32_t addr, void *p)
{
    sst_t *dev = (sst_t *) p;
    uint8_t ret = 0xff;

    addr &= 0x000fffff;

    if (dev->id_mode)
	ret = sst_read_id(addr, p);
    else {
	if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
		ret = dev->array[addr - biosaddr];
    }

    return ret;
}


static uint16_t
sst_readw(uint32_t addr, void *p)
{
    sst_t *dev = (sst_t *) p;
    uint16_t ret = 0xffff;

    addr &= 0x000fffff;

    if (dev->id_mode)
	ret = sst_read(addr, p) | (sst_read(addr + 1, p) << 8);
    else {
	if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
		ret = *(uint16_t *)&dev->array[addr - biosaddr];
    }

    return ret;
}


static uint32_t
sst_readl(uint32_t addr, void *p)
{
    sst_t *dev = (sst_t *) p;
    uint32_t ret = 0xffffffff;

    addr &= 0x000fffff;

    if (dev->id_mode)
	ret = sst_readw(addr, p) | (sst_readw(addr + 2, p) << 16);
    else {
	if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
		ret = *(uint32_t *)&dev->array[addr - biosaddr];
    }

    return ret;
}


static void
sst_add_mappings(sst_t *dev)
{
    int i = 0, count;
    uint32_t base, fbase;
    uint32_t root_base;

    count = dev->size >> 16;
    root_base = 0x100000 - dev->size;

    for (i = 0; i < count; i++) {
	base = root_base + (i << 16);
	fbase = base & biosmask;

	memcpy(&dev->array[fbase], &rom[base & biosmask], 0x10000);

	if (base >= 0xe0000) {
		mem_mapping_add(&(dev->mapping[i]), base, 0x10000,
				sst_read, sst_readw, sst_readl,
				sst_write, NULL, NULL,
				dev->array + fbase, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM|MEM_MAPPING_ROMCS, (void *) dev);
	}
	mem_mapping_add(&(dev->mapping_h[i]), (base | (cpu_16bitbus ? 0xf00000 : 0xfff00000)), 0x10000,
			sst_read, sst_readw, sst_readl,
			sst_write, NULL, NULL,
			dev->array + fbase, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM|MEM_MAPPING_ROMCS, (void *) dev);
    }
}


static void *
sst_init(const device_t *info)
{
    FILE *f;
    sst_t *dev = malloc(sizeof(sst_t));
    memset(dev, 0, sizeof(sst_t));

    sprintf(flash_path, "%s.bin", machine_get_internal_name_ex(machine));

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    dev->array = (uint8_t *) malloc(biosmask + 1);
    memset(dev->array, 0xff, biosmask + 1);

    dev->manufacturer = info->local & 0xff;
    dev->id = (info->local >> 8) & 0xff;
    dev->has_bbp = (dev->manufacturer == WINBOND) && ((info->local & 0xff00) >= W29C020);
    dev->is_39 = (dev->manufacturer == SST) && ((info->local & 0xff00) >= SST39SF512);

    dev->size = info->local & 0xffff0000;
    if ((dev->size == 0x20000) && (strstr(machine_get_internal_name_ex(machine), "xi8088")) && !xi8088_bios_128kb())
	dev->size = 0x10000;

    dev->mask = dev->size - 1;
    dev->page_mask = dev->mask & 0xffffff80;	/* Filter out A0-A6. */
    dev->sdp = 1;
    dev->bbp_first_8k = dev->bbp_last_8k = 0xfe;

    sst_add_mappings(dev);

    f = nvr_fopen(flash_path, "rb");
    if (f) {
	if (fread(&(dev->array[0x00000]), 1, dev->size, f) != dev->size)
		pclog("Less than %i bytes read from the SST Flash ROM file\n", dev->size);
	fclose(f);
    } else
	dev->dirty = 1;		/* It is by definition dirty on creation. */

    if (!dev->is_39)
	timer_add(&dev->page_write_timer, sst_page_write, dev, 0);

    return dev;
}


static void
sst_close(void *p)
{
    FILE *f;
    sst_t *dev = (sst_t *)p;

    if (dev->dirty) {
	f = nvr_fopen(flash_path, "wb");
	if (f != NULL) {
		fwrite(&(dev->array[0x00000]), dev->size, 1, f);
		fclose(f);
	}
    }

    free(dev->array);
    dev->array = NULL;

    free(dev);
}

const device_t sst_flash_29ee010_device = {
    .name = "SST 29EE010 Flash BIOS",
    .internal_name = "sst_flash_29ee010",
    .flags = 0,
    .local = SST | SST29EE010 | SIZE_1M,
    .init = sst_init,
    .close = sst_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t sst_flash_29ee020_device = {
    .name = "SST 29EE020 Flash BIOS",
    .internal_name = "sst_flash_29ee020",
    .flags = 0,
    .local = SST | SST29EE020 | SIZE_2M,
    .init = sst_init,
    .close = sst_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t winbond_flash_w29c020_device = {
    .name = "Winbond W29C020 Flash BIOS",
    .internal_name = "winbond_flash_w29c020",
    .flags = 0,
    .local = WINBOND | W29C020 | SIZE_2M,
    .init = sst_init,
    .close = sst_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t sst_flash_39sf010_device = {
    .name = "SST 39SF010 Flash BIOS",
    .internal_name = "sst_flash_39sf010",
    .flags = 0,
    .local = SST | SST39SF010 | SIZE_1M,
    .init = sst_init,
    .close = sst_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t sst_flash_39sf020_device = {
    .name = "SST 39SF020 Flash BIOS",
    .internal_name = "sst_flash_39sf020",
    .flags = 0,
    .local = SST | SST39SF020 | SIZE_2M,
    .init = sst_init,
    .close = sst_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t sst_flash_39sf040_device = {
    .name = "SST 39SF040 Flash BIOS",
    .internal_name = "sst_flash_39sf040",
    .flags = 0,
    .local = SST | SST39SF040 | SIZE_4M,
    .init = sst_init,
    .close = sst_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
