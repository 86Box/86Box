/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of SPD (Serial Presence Detect) devices.
 *
 * Version:	@(#)spd.c	1.0.0	2020/03/24
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "device.h"
#include "smbus.h"
#include "spd.h"


#define SPD_MAX_SLOTS	8
#define SPD_DATA_SIZE	256

#define MIN(a, b) ((a) < (b) ? (a) : (b))


typedef struct _spd_ {
    uint8_t	slot;
    uint8_t	addr_register;
} spd_t;


device_t	*spd_devices[SPD_MAX_SLOTS];
uint8_t		spd_data[SPD_MAX_SLOTS][SPD_DATA_SIZE];


static uint8_t	spd_read_byte(uint8_t addr, void *priv);
static uint8_t	spd_read_byte_cmd(uint8_t addr, uint8_t cmd, void *priv);
static void	spd_write_byte(uint8_t addr, uint8_t val, void *priv);


#ifdef ENABLE_SPD_LOG
int spd_do_log = ENABLE_SPD_LOG;


static void
spd_log(const char *fmt, ...)
{
    va_list ap;

    if (spd_do_log) {
    	va_start(ap, fmt);
    	pclog_ex(fmt, ap);
    	va_end(ap);
    }
}
#else
#define spd_log(fmt, ...)
#endif


uint8_t
spd_read_byte(uint8_t addr, void *priv)
{
    spd_t *dev = (spd_t *) priv;
    return spd_read_byte_cmd(addr, dev->addr_register, priv);
}


uint8_t
spd_read_byte_cmd(uint8_t addr, uint8_t cmd, void *priv)
{
    spd_t *dev = (spd_t *) priv;
    uint8_t ret = *(spd_data[dev->slot] + cmd);
    spd_log("SPD: read(%02x, %02x) = %02x\n", addr, cmd, ret);
    return ret;
}


void
spd_write_byte(uint8_t addr, uint8_t val, void *priv)
{
    spd_t *dev = (spd_t *) priv;
    dev->addr_register = val;
}


static void
spd_close(void *priv)
{
    spd_t *dev = (spd_t *) priv;

    spd_log("SPD: closing slot %d (SMBus %02Xh)\n", dev->slot, SPD_BASE_ADDR + dev->slot);

    smbus_removehandler(SPD_BASE_ADDR + dev->slot, 1,
    			spd_read_byte, spd_read_byte_cmd, NULL, NULL,
    			spd_write_byte, NULL, NULL, NULL,
    			dev);

    free(dev);
}


static void *
spd_init(const device_t *info)
{
    spd_t *dev = (spd_t *) malloc(sizeof(spd_t));
    memset(dev, 0, sizeof(spd_t));

    dev->slot = info->local;

    spd_log("SPD: initializing slot %d (SMBus %02Xh)\n", dev->slot, SPD_BASE_ADDR + dev->slot);

    smbus_sethandler(SPD_BASE_ADDR + dev->slot, 1,
    			spd_read_byte, spd_read_byte_cmd, NULL, NULL,
    			spd_write_byte, NULL, NULL, NULL,
    			dev);

    return dev;
}


uint8_t
log2_ui16(uint16_t i)
{
    uint8_t ret = 0;
    while ((i >>= 1))
    	ret++;
    return ret;
}


int
comp_ui16_rev(const void *elem1, const void *elem2)
{
    uint16_t a = *((uint16_t *)elem1);
    uint16_t b = *((uint16_t *)elem2);
    return ((a > b) ? -1 : ((a < b) ? 1 : 0));
}


void
spd_register(uint8_t ram_type, uint8_t slot_mask, uint16_t max_module_size)
{
    uint8_t slot, slot_count, vslot, next_empty_vslot, i, split;
    uint16_t min_module_size, total_size, vslots[SPD_MAX_SLOTS];
    spd_sdram_t *sdram_data;

    /* determine the minimum module size for this RAM type */
    switch (ram_type) {
    	case SPD_TYPE_SDRAM:
    		min_module_size = SPD_MIN_SIZE_SDRAM;
    		break;

	default:
    		spd_log("SPD: unknown RAM type 0x%02X\n", ram_type);
    		return;
    }

    /* count how many (real) slots are enabled */
    slot_count = 0;
    for (slot = 0; slot < SPD_MAX_SLOTS; slot++) {
    	vslots[slot] = 0;
    	if (slot_mask & (1 << slot)) {
    		slot_count++;
    	}
    }

    /* populate vslots with modules in power-of-2 capacities */
    total_size = (mem_size >> 10);
    for (vslot = 0; vslot < slot_count && total_size; vslot++) {
    	/* populate slot */
    	vslots[vslot] = (1 << log2_ui16(MIN(total_size, max_module_size)));
    	if (total_size >= vslots[vslot]) {
    		spd_log("SPD: vslot %d = %d MB\n", vslot, vslots[vslot]);
    		total_size -= vslots[vslot];
    	} else {
    		break;
    	}
    }

    if (total_size > 0) /* did we populate everything? */
    	spd_log("SPD: not enough RAM slots (%d) to cover memory (%d MB short)\n", slot_count, total_size);

    /* populate empty vslots by splitting modules while possible */
    split = 1;
    while (split) {
    	/* look for a module to split */
    	split = 0;
    	for (vslot = 0; vslot < slot_count; vslot++) {
    		if (vslots[vslot] < (min_module_size << 1))
    			continue; /* no module here or module is too small to be split */

    		/* find next empty vslot */
    		next_empty_vslot = 0;
    		for (i = vslot + 1; i < slot_count && !next_empty_vslot; i++) {
    			if (!vslots[i])
    				next_empty_vslot = i;
    		}
    		if (!next_empty_vslot)
    			break; /* no empty vslots left */

    		/* split the module into its own vslot and the next empty vslot */
    		spd_log("SPD: splitting vslot %d (%d MB) into %d and %d (%d MB each)\n", vslot, vslots[vslot], vslot, next_empty_vslot, (vslots[vslot] >> 1));
    		vslots[vslot] = vslots[next_empty_vslot] = (vslots[vslot] >> 1);
    		split = 1;
    	}

    	/* re-sort vslots by descending capacity if any modules were split */
    	if (split)
    		qsort(vslots, slot_count, sizeof(uint16_t), comp_ui16_rev);
    }

    /* register SPD devices and populate their data according to the vslots */
    vslot = 0;
    for (slot = 0; slot < SPD_MAX_SLOTS && vslots[vslot]; slot++) {
    	if (!(slot_mask & (1 << slot)))
    		continue; /* slot disabled */

    	spd_log("SPD: registering slot %d = vslot %d = %d MB\n", slot, vslot, vslots[vslot]);

    	spd_devices[slot] = (device_t *)malloc(sizeof(device_t));
    	memset(spd_devices[slot], 0, sizeof(device_t));
    	spd_devices[slot]->name = "Serial Presence Detect ROM";
    	spd_devices[slot]->local = slot;
    	spd_devices[slot]->init = spd_init;
    	spd_devices[slot]->close = spd_close;

    	switch (ram_type) {
    		case SPD_TYPE_SDRAM:
    			sdram_data = (spd_sdram_t *)&spd_data[slot];
    			memset(sdram_data, 0, sizeof(spd_sdram_t));

    			sdram_data->bytes_used = 0x80;
    			sdram_data->spd_size = 0x08;
    			sdram_data->mem_type = ram_type;
    			sdram_data->row_bits = 5 + log2_ui16(vslots[vslot]);
    			sdram_data->col_bits = 9;
    			sdram_data->rows = 2;
    			sdram_data->data_width_lsb = 64;
    			sdram_data->data_width_msb = 0;
    			sdram_data->signal_level = SPD_SDR_SIGNAL_LVTTL;
    			sdram_data->tclk = sdram_data->tac = 0x10;
    			sdram_data->config = 0;
    			sdram_data->refresh_rate = SPD_SDR_REFRESH_SELF | SPD_SDR_REFRESH_NORMAL;
    			sdram_data->sdram_width = 8;
    			sdram_data->tccd = 1;
    			sdram_data->burst = SPD_SDR_BURST_PAGE | 1 | 2 | 4 | 8;
    			sdram_data->banks = 4;
    			sdram_data->cas = sdram_data->cs = sdram_data->we = 0x7F;
    			sdram_data->dev_attr = SPD_SDR_ATTR_EARLY_RAS | SPD_SDR_ATTR_AUTO_PC | SPD_SDR_ATTR_PC_ALL | SPD_SDR_ATTR_W1R_BURST;
    			sdram_data->tclk2 = sdram_data->tac2 = 0x10;
    			sdram_data->trp = sdram_data->trrd = sdram_data->trcd = sdram_data->tras = 1;
    			sdram_data->bank_density = 1 << (log2_ui16(vslots[vslot] >> 1) - 2);
    			sdram_data->ca_setup = sdram_data->data_setup = 0x15;
    			sdram_data->ca_hold = sdram_data->data_hold = 0x08;
    			sdram_data->spd_rev = 0x12;
    			sprintf(sdram_data->part_no, "86Box-SDR-%03dM", vslots[vslot]);
    			sdram_data->mfg_year = 0x20;
    			sdram_data->mfg_week = 0x13;
    			sdram_data->freq = 100;
    			sdram_data->features = 0xFF;

    			for (i = 0; i < 63; i++)
    				sdram_data->checksum += spd_data[slot][i];
    			break;
    	}

    	device_add(spd_devices[slot]);
    	vslot++;
    }
}
