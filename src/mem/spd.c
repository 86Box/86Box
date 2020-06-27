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
 *
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
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/smbus.h>
#include <86box/spd.h>
#include <86box/version.h>
#include <86box/machine.h>


#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define SPD_ROLLUP(x)	((x) >= 16 ? ((x) - 15) : (x))


int		spd_present = 0;
spd_t		*spd_devices[SPD_MAX_SLOTS];
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
    spd_log("SPD: read(%02X, %02X) = %02X\n", addr, cmd, ret);
    return ret;
}


uint16_t
spd_read_word_cmd(uint8_t addr, uint8_t cmd, void *priv)
{
    return (spd_read_byte_cmd(addr, cmd + 1, priv) << 8) | spd_read_byte_cmd(addr, cmd, priv);
}


uint8_t
spd_read_block_cmd(uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv)
{
    uint8_t read = 0;
    for (uint8_t i = cmd; i < len && i < SPD_DATA_SIZE; i++) {
    	data[read++] = spd_read_byte_cmd(addr, i, priv);
    }
    return read;
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
    			spd_read_byte, spd_read_byte_cmd, spd_read_word_cmd, spd_read_block_cmd,
    			spd_write_byte, NULL, NULL, NULL,
    			dev);

    spd_present = 0;

    free(dev);
}


static void *
spd_init(const device_t *info)
{
    spd_t *dev = spd_devices[info->local];

    spd_log("SPD: initializing slot %d (SMBus %02Xh)\n", dev->slot, SPD_BASE_ADDR + dev->slot);

    smbus_sethandler(SPD_BASE_ADDR + dev->slot, 1,
    		     spd_read_byte, spd_read_byte_cmd, spd_read_word_cmd, spd_read_block_cmd,
    		     spd_write_byte, NULL, NULL, NULL,
    		     dev);

    spd_present = 1;

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
    uint16_t a = *((uint16_t *) elem1);
    uint16_t b = *((uint16_t *) elem2);
    return ((a > b) ? -1 : ((a < b) ? 1 : 0));
}


void
spd_populate(uint16_t *vslots, uint8_t slot_count, uint16_t total_size, uint16_t min_module_size, uint16_t max_module_size, uint8_t enable_asym)
{
    uint8_t vslot, next_empty_vslot, split, i;
    uint16_t asym;

    /* populate vslots with modules in power-of-2 capacities */
    memset(vslots, 0x00, SPD_MAX_SLOTS << 1);
    for (vslot = 0; vslot < slot_count && total_size; vslot++) {
    	/* populate slot */
    	vslots[vslot] = (1 << log2_ui16(MIN(total_size, max_module_size)));
    	if (total_size >= vslots[vslot]) {
    		spd_log("SPD: initial vslot %d = %d MB\n", vslot, vslots[vslot]);
    		total_size -= vslots[vslot];
    	} else {
    		vslots[vslot] = 0;
    		break;
    	}
    }

    /* did we populate all the RAM? */
    if (total_size) {
    	/* work backwards to add the missing RAM as asymmetric modules if possible */
    	if (enable_asym) {
    		vslot = slot_count - 1;
    		do {
    			asym = (1 << log2_ui16(MIN(total_size, vslots[vslot])));
    			if (vslots[vslot] + asym <= max_module_size) {
    				vslots[vslot] += asym;
    				total_size -= asym;
    			}
    		} while ((vslot-- > 0) && total_size);
    	}

    	if (total_size) /* still not enough */
    		spd_log("SPD: not enough RAM slots (%d) to cover memory (%d MB short)\n", slot_count, total_size);
    }

    /* populate empty vslots by splitting modules... */
    split = (total_size == 0); /* ...if possible */
    while (split) {
    	/* look for a module to split */
    	split = 0;
    	for (vslot = 0; vslot < slot_count; vslot++) {
    		if ((vslots[vslot] < (min_module_size << 1)) || (vslots[vslot] != (1 << log2_ui16(vslots[vslot]))))
    			continue; /* no module here, module is too small to be split, or asymmetric module */

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
    		break;
    	}

    	/* sort vslots by descending capacity if any were split */
    	if (split)
    		qsort(vslots, slot_count, sizeof(uint16_t), comp_ui16_rev);
    }
}


void
spd_register(uint8_t ram_type, uint8_t slot_mask, uint16_t max_module_size)
{
    uint8_t slot, slot_count, vslot, i;
    uint16_t min_module_size, vslots[SPD_MAX_SLOTS], asym;
    device_t *info;
    spd_edo_t *edo_data;
    spd_sdram_t *sdram_data;

    /* determine the minimum module size for this RAM type */
    switch (ram_type) {
    	case SPD_TYPE_FPM:
    	case SPD_TYPE_EDO:
    		min_module_size = SPD_MIN_SIZE_EDO;
    		break;

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

    /* populate vslots  */
    spd_populate(vslots, slot_count, (mem_size >> 10), min_module_size, max_module_size, 1);

    /* register SPD devices and populate their data according to the vslots */
    vslot = 0;
    for (slot = 0; slot < SPD_MAX_SLOTS && vslots[vslot]; slot++) {
    	if (!(slot_mask & (1 << slot)))
    		continue; /* slot disabled */

    	info = (device_t *) malloc(sizeof(device_t));
    	memset(info, 0, sizeof(device_t));
    	info->name = "Serial Presence Detect ROM";
    	info->local = slot;
    	info->init = spd_init;
    	info->close = spd_close;

    	spd_devices[slot] = (spd_t *) malloc(sizeof(spd_t));
    	memset(spd_devices[slot], 0, sizeof(spd_t));
    	spd_devices[slot]->info = info;
    	spd_devices[slot]->slot = slot;
    	spd_devices[slot]->size = vslots[vslot];

    	/* determine the second row size, from which the first row size can be obtained */
    	asym = (vslots[vslot] - (1 << log2_ui16(vslots[vslot]))); /* separate the powers of 2 */
    	if (!asym) /* is the module asymmetric? */
    		asym = (vslots[vslot] >> 1); /* symmetric, therefore divide by 2 */

    	spd_devices[slot]->row1 = (vslots[vslot] - asym);
    	spd_devices[slot]->row2 = asym;

    	spd_log("SPD: registering slot %d = vslot %d = %d MB (%d/%d)\n", slot, vslot, vslots[vslot], spd_devices[slot]->row1, spd_devices[slot]->row2);

    	switch (ram_type) {
    		case SPD_TYPE_FPM:
    		case SPD_TYPE_EDO:
    			edo_data = (spd_edo_t *) &spd_data[slot];
    			memset(edo_data, 0, sizeof(spd_edo_t));

    			/* EDO SPD is specified by JEDEC and present in some modules, but
    			   most utilities cannot interpret it correctly. SIV32 at least gets
    			   the module capacities right, so it was used as a reference here. */
    			edo_data->bytes_used = 0x80;
    			edo_data->spd_size = 0x08;
    			edo_data->mem_type = ram_type;
    			edo_data->row_bits = SPD_ROLLUP(7 + log2_ui16(spd_devices[slot]->row1)); /* first row */
    			edo_data->col_bits = 9;
    			if (spd_devices[slot]->row1 != spd_devices[slot]->row2) { /* the upper 4 bits of row_bits/col_bits should be 0 on a symmetric module */
    				edo_data->row_bits |= (SPD_ROLLUP(7 + log2_ui16(spd_devices[slot]->row2)) << 4); /* second row, if different from first */
    				edo_data->col_bits |= (9 << 4); /* same as first row, but just in case */
    			}
    			edo_data->banks = 2;
    			edo_data->data_width_lsb = 64;
    			edo_data->signal_level = SPD_SIGNAL_LVTTL;
    			edo_data->trac = 50;
    			edo_data->tcac = 13;
    			edo_data->refresh_rate = SPD_REFRESH_NORMAL;
    			edo_data->dram_width = 8;

    			edo_data->spd_rev = 0x12;
    			sprintf(edo_data->part_no, EMU_NAME "-%s-%03dM", (ram_type == SPD_TYPE_FPM) ? "FPM" : "EDO", vslots[vslot]);
    			for (i = strlen(edo_data->part_no); i < sizeof(edo_data->part_no); i++)
    				edo_data->part_no[i] = ' '; /* part number should be space-padded */
    			edo_data->rev_code[0] = EMU_VERSION_MAJ;
    			edo_data->rev_code[1] = (((EMU_VERSION_MIN / 10) << 4) | (EMU_VERSION_MIN % 10));
    			edo_data->mfg_year = 20;
    			edo_data->mfg_week = 17;

    			for (i = 0; i < 63; i++)
    				edo_data->checksum += spd_data[slot][i];
    			for (i = 0; i < 129; i++)
    				edo_data->checksum2 += spd_data[slot][i];
    			break;

    		case SPD_TYPE_SDRAM:
    			sdram_data = (spd_sdram_t *) &spd_data[slot];
    			memset(sdram_data, 0, sizeof(spd_sdram_t));

    			sdram_data->bytes_used = 0x80;
    			sdram_data->spd_size = 0x08;
    			sdram_data->mem_type = ram_type;
    			sdram_data->row_bits = SPD_ROLLUP(6 + log2_ui16(spd_devices[slot]->row1)); /* first row */
    			sdram_data->col_bits = 9;
    			if (spd_devices[slot]->row1 != spd_devices[slot]->row2) { /* the upper 4 bits of row_bits/col_bits should be 0 on a symmetric module */
    				sdram_data->row_bits |= (SPD_ROLLUP(6 + log2_ui16(spd_devices[slot]->row2)) << 4); /* second row, if different from first */
    				sdram_data->col_bits |= (9 << 4); /* same as first row, but just in case */
    			}
    			sdram_data->rows = 2;
    			sdram_data->data_width_lsb = 64;
    			sdram_data->signal_level = SPD_SIGNAL_LVTTL;
    			sdram_data->tclk = 0x75; /* 7.5 ns = 133.3 MHz */
    			sdram_data->tac = 0x10;
    			sdram_data->refresh_rate = SPD_SDR_REFRESH_SELF | SPD_REFRESH_NORMAL;
    			sdram_data->sdram_width = 8;
    			sdram_data->tccd = 1;
    			sdram_data->burst = SPD_SDR_BURST_PAGE | 1 | 2 | 4 | 8;
    			sdram_data->banks = 4;
    			sdram_data->cas = 0x1c; /* CAS 5/4/3 supported */
    			sdram_data->cslat = sdram_data->we = 0x7f;
    			sdram_data->dev_attr = SPD_SDR_ATTR_EARLY_RAS | SPD_SDR_ATTR_AUTO_PC | SPD_SDR_ATTR_PC_ALL | SPD_SDR_ATTR_W1R_BURST;
    			sdram_data->tclk2 = 0xA0; /* 10 ns = 100 MHz */
    			sdram_data->tclk3 = 0xF0; /* 15 ns = 66.7 MHz */
    			sdram_data->tac2 = sdram_data->tac3 = 0x10;
    			sdram_data->trp = sdram_data->trrd = sdram_data->trcd = sdram_data->tras = 1;
    			if (spd_devices[slot]->row1 != spd_devices[slot]->row2) {
    				/* Utilities interpret bank_density a bit differently on asymmetric modules. */
    				sdram_data->bank_density  = (1 << (log2_ui16(spd_devices[slot]->row1 >> 1) - 2)); /* first row */
    				sdram_data->bank_density |= (1 << (log2_ui16(spd_devices[slot]->row2 >> 1) - 2)); /* second row */
    			} else {
    				sdram_data->bank_density  = (1 << (log2_ui16(spd_devices[slot]->row1 >> 1) - 1)); /* symmetric module = only one bit is set */
    			}
    			sdram_data->ca_setup = sdram_data->data_setup = 0x15;
    			sdram_data->ca_hold = sdram_data->data_hold = 0x08;

    			sdram_data->spd_rev = 0x12;
    			sprintf(sdram_data->part_no, EMU_NAME "-SDR-%03dM", vslots[vslot]);
    			for (i = strlen(sdram_data->part_no); i < sizeof(sdram_data->part_no); i++)
    				sdram_data->part_no[i] = ' '; /* part number should be space-padded */
    			sdram_data->rev_code[0] = EMU_VERSION_MAJ;
    			sdram_data->rev_code[1] = (((EMU_VERSION_MIN / 10) << 4) | (EMU_VERSION_MIN % 10));
    			sdram_data->mfg_year = 20;
    			sdram_data->mfg_week = 13;

    			sdram_data->freq = 100;
    			sdram_data->features = 0xFF;

    			for (i = 0; i < 63; i++)
    				sdram_data->checksum += spd_data[slot][i];
    			for (i = 0; i < 129; i++)
    				sdram_data->checksum2 += spd_data[slot][i];
    			break;
    	}

    	device_add(info);
    	vslot++;
    }
}


void
spd_write_drbs(uint8_t *regs, uint8_t reg_min, uint8_t reg_max, uint8_t drb_unit)
{
    uint8_t row, dimm, drb, apollo = 0;
    uint16_t size, vslots[SPD_MAX_SLOTS];

    /* Special case for VIA Apollo Pro family, which jumps from 5F to 56. */
    if (reg_max < reg_min) {
    	apollo = reg_max;
    	reg_max = reg_min + 7;
    }

    /* No SPD: split SIMMs into pairs as if they were "DIMM"s. */
    if (!spd_present) {
    	dimm = ((reg_max - reg_min) + 1) >> 1; /* amount of "DIMM"s, also used to determine the maximum "DIMM" size */
    	spd_populate(vslots, dimm, (mem_size >> 10), drb_unit, 1 << (log2_ui16(machines[machine].max_ram / dimm)), 0);
    }

    /* Write DRBs for each row. */
    spd_log("Writing DRBs... regs=[%02X:%02X] unit=%d\n", reg_min, reg_max, drb_unit);
    for (row = 0; row <= (reg_max - reg_min); row++) {
    	dimm = (row >> 1);
    	size = 0;

    	if (spd_present) {
    		/* SPD enabled: use SPD info for this slot, if present. */
    		if (spd_devices[dimm]) {
    			if (spd_devices[dimm]->row1 < drb_unit) /* hack within a hack: turn a double-sided DIMM that is too small into a single-sided one */
    				size = ((row & 1) ? 0 : drb_unit);
    			else
    				size = ((row & 1) ? spd_devices[dimm]->row2 : spd_devices[dimm]->row1);
    		}
    	} else {
    		/* No SPD: use the values calculated above. */
    		size = (vslots[dimm] >> 1);
    	}

    	/* Determine the DRB register to write. */
    	drb = reg_min + row;
    	if ((apollo) && ((drb & 0xf) < 0xa))
    		drb = apollo + (drb & 0xf);

    	/* Write DRB register, adding the previous DRB's value. */
    	if (row == 0)
    		regs[drb] = 0;
    	else if ((apollo) && (drb == apollo))
    		regs[drb] = regs[drb | 0xf]; /* 5F comes before 56 */
    	else
    		regs[drb] = regs[drb - 1];
    	if (size)
    		regs[drb] += (size / drb_unit); /* this will intentionally overflow on 440GX with 2 GB */
    	spd_log("DRB[%d] = %d MB (%02Xh raw)\n", row, size, regs[drb]);
    }
}
