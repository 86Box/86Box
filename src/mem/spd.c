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
#include <86box/i2c.h>
#include <86box/spd.h>
#include <86box/version.h>
#include <86box/machine.h>


#define SPD_ROLLUP(x)	((x) >= 16 ? ((x) - 15) : (x))


int		spd_present = 0;
spd_t		*spd_modules[SPD_MAX_SLOTS];

static const device_t spd_device;


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


static void
spd_close(void *priv)
{
    spd_log("SPD: close()\n");

    for (uint8_t i = 0; i < SPD_MAX_SLOTS; i++) {
	if (spd_modules[i])
		i2c_eeprom_close(spd_modules[i]->eeprom);
    }

    spd_present = 0;
}


static void *
spd_init(const device_t *info)
{
    spd_log("SPD: init()\n");

    for (uint8_t i = 0; i < SPD_MAX_SLOTS; i++) {
	if (spd_modules[i])
		spd_modules[i]->eeprom = i2c_eeprom_init(i2c_smbus, SPD_BASE_ADDR + i, spd_modules[i]->data, sizeof(spd_modules[i]->data), 0);
    }

    spd_present = 1;

    return &spd_modules;
}


int
comp_ui16_rev(const void *elem1, const void *elem2)
{
    uint16_t a = *((uint16_t *) elem1);
    uint16_t b = *((uint16_t *) elem2);
    return ((a > b) ? -1 : ((a < b) ? 1 : 0));
}


void
spd_populate(uint16_t *rows, uint8_t slot_count, uint16_t total_size, uint16_t min_module_size, uint16_t max_module_size, uint8_t enable_asym)
{
    uint8_t row, next_empty_row, split, i;
    uint16_t asym;

    /* Populate rows with modules in power-of-2 capacities. */
    memset(rows, 0, SPD_MAX_SLOTS << 1);
    for (row = 0; row < slot_count && total_size; row++) {
	/* populate slot */
	rows[row] = 1 << log2i(MIN(total_size, max_module_size));
	if (total_size >= rows[row]) {
		spd_log("SPD: Initial row %d = %d MB\n", row, rows[row]);
		total_size -= rows[row];
	} else {
		rows[row] = 0;
		break;
	}
    }

    /* Did we populate all the RAM? */
    if (total_size) {
	/* Work backwards to add the missing RAM as asymmetric modules if possible. */
	if (enable_asym) {
		row = slot_count - 1;
		do {
			asym = (1 << log2i(MIN(total_size, rows[row])));
			if (rows[row] + asym <= max_module_size) {
				rows[row] += asym;
				total_size -= asym;
			}
		} while ((row-- > 0) && total_size);
	}

	if (total_size) /* still not enough */
		spd_log("SPD: Not enough RAM slots (%d) to cover memory (%d MB short)\n", slot_count, total_size);
    }

    /* Populate empty rows by splitting modules... */
    split = (total_size == 0); /* ...if possible. */
    while (split) {
	/* Look for a module to split. */
	split = 0;
	for (row = 0; row < slot_count; row++) {
		if ((rows[row] < (min_module_size << 1)) || (rows[row] != (1 << log2i(rows[row]))))
			continue; /* no module here, module is too small to be split, or asymmetric module */

		/* Find next empty row. */
		next_empty_row = 0;
		for (i = row + 1; i < slot_count && !next_empty_row; i++) {
			if (!rows[i])
				next_empty_row = i;
		}
		if (!next_empty_row)
			break; /* no empty rows left */

		/* Split the module into its own row and the next empty row. */
		spd_log("SPD: splitting row %d (%d MB) into %d and %d (%d MB each)\n", row, rows[row], row, next_empty_row, rows[row] >> 1);
		rows[row] = rows[next_empty_row] = rows[row] >> 1;
		split = 1;
		break;
	}

	/* Sort rows by descending capacity if any were split. */
	if (split)
		qsort(rows, slot_count, sizeof(uint16_t), comp_ui16_rev);
    }
}


static int
spd_write_part_no(char *part_no, char *type, uint16_t size)
{
    char size_unit;

    if (size >= 1024) {
	size_unit = 'G';
	size >>= 10;
    } else {
	size_unit = 'M';
    }

    return sprintf(part_no, EMU_NAME "-%s-%03d%c", type, size, size_unit);
}


void
spd_register(uint8_t ram_type, uint8_t slot_mask, uint16_t max_module_size)
{
    uint8_t slot, slot_count, row, i;
    uint16_t min_module_size, rows[SPD_MAX_SLOTS], asym;
    spd_edo_t *edo_data;
    spd_sdram_t *sdram_data;

    /* Determine the minimum module size for this RAM type. */
    switch (ram_type) {
	case SPD_TYPE_FPM:
	case SPD_TYPE_EDO:
		min_module_size = SPD_MIN_SIZE_EDO;
		break;

	case SPD_TYPE_SDRAM:
		min_module_size = SPD_MIN_SIZE_SDRAM;
		break;

	default:
		spd_log("SPD: unknown RAM type %02X\n", ram_type);
		return;
    }

    /* Count how many slots are enabled. */
    slot_count = 0;
    for (slot = 0; slot < SPD_MAX_SLOTS; slot++) {
	rows[slot] = 0;
	if (slot_mask & (1 << slot))
		slot_count++;
    }

    /* Populate rows. */
    spd_populate(rows, slot_count, (mem_size >> 10), min_module_size, max_module_size, 1);

    /* Register SPD devices and populate their data according to the rows. */
    row = 0;
    for (slot = 0; (slot < SPD_MAX_SLOTS) && rows[row]; slot++) {
	if (!(slot_mask & (1 << slot)))
		continue; /* slot disabled */

	spd_modules[slot] = (spd_t *) malloc(sizeof(spd_t));
	memset(spd_modules[slot], 0, sizeof(spd_t));
	spd_modules[slot]->slot = slot;
	spd_modules[slot]->size = rows[row];

	/* Determine the second row size, from which the first row size can be obtained. */
	asym = rows[row] - (1 << log2i(rows[row])); /* separate the powers of 2 */
	if (!asym) /* is the module asymmetric? */
		asym = rows[row] >> 1; /* symmetric, therefore divide by 2 */

	spd_modules[slot]->row1 = rows[row] - asym;
	spd_modules[slot]->row2 = asym;

	spd_log("SPD: Registering slot %d = row %d = %d MB (%d/%d)\n", slot, row, rows[row], spd_modules[slot]->row1, spd_modules[slot]->row2);

	switch (ram_type) {
		case SPD_TYPE_FPM:
		case SPD_TYPE_EDO:
			edo_data = &spd_modules[slot]->edo_data;

			/* EDO SPD is specified by JEDEC and present in some modules, but
			   most utilities cannot interpret it correctly. SIV32 at least gets
			   the module capacities right, so it was used as a reference here. */
			edo_data->bytes_used = 0x80;
			edo_data->spd_size = 0x08;
			edo_data->mem_type = ram_type;
			edo_data->row_bits = SPD_ROLLUP(7 + log2i(spd_modules[slot]->row1)); /* first row */
			edo_data->col_bits = 9;
			if (spd_modules[slot]->row1 != spd_modules[slot]->row2) { /* the upper 4 bits of row_bits/col_bits should be 0 on a symmetric module */
				edo_data->row_bits |= SPD_ROLLUP(7 + log2i(spd_modules[slot]->row2)) << 4; /* second row, if different from first */
				edo_data->col_bits |= 9 << 4; /* same as first row, but just in case */
			}
			edo_data->banks = 2;
			edo_data->data_width_lsb = 64;
			edo_data->signal_level = SPD_SIGNAL_LVTTL;
			edo_data->trac = 50;
			edo_data->tcac = 13;
			edo_data->refresh_rate = SPD_REFRESH_NORMAL;
			edo_data->dram_width = 8;

			edo_data->spd_rev = 0x12;
			for (i = spd_write_part_no(edo_data->part_no, (ram_type == SPD_TYPE_FPM) ? "FPM" : "EDO", rows[row]);
			     i < sizeof(edo_data->part_no); i++)
				edo_data->part_no[i] = ' '; /* part number should be space-padded */
			edo_data->rev_code[0] = BCD8(EMU_VERSION_MAJ);
			edo_data->rev_code[1] = BCD8(EMU_VERSION_MIN);
			edo_data->mfg_year = 20;
			edo_data->mfg_week = 17;

			for (i = 0; i < 63; i++)
				edo_data->checksum += spd_modules[slot]->data[i];
			for (i = 0; i < 129; i++)
				edo_data->checksum2 += spd_modules[slot]->data[i];
			break;

		case SPD_TYPE_SDRAM:
			sdram_data = &spd_modules[slot]->sdram_data;

			sdram_data->bytes_used = 0x80;
			sdram_data->spd_size = 0x08;
			sdram_data->mem_type = ram_type;
			sdram_data->row_bits = SPD_ROLLUP(6 + log2i(spd_modules[slot]->row1)); /* first row */
			sdram_data->col_bits = 9;
			if (spd_modules[slot]->row1 != spd_modules[slot]->row2) { /* the upper 4 bits of row_bits/col_bits should be 0 on a symmetric module */
				sdram_data->row_bits |= SPD_ROLLUP(6 + log2i(spd_modules[slot]->row2)) << 4; /* second row, if different from first */
				sdram_data->col_bits |= 9 << 4; /* same as first row, but just in case */
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
			if (spd_modules[slot]->row1 != spd_modules[slot]->row2) {
				/* Utilities interpret bank_density a bit differently on asymmetric modules. */
				sdram_data->bank_density  = 1 << (log2i(spd_modules[slot]->row1 >> 1) - 2); /* first row */
				sdram_data->bank_density |= 1 << (log2i(spd_modules[slot]->row2 >> 1) - 2); /* second row */
			} else {
				sdram_data->bank_density  = 1 << (log2i(spd_modules[slot]->row1 >> 1) - 1); /* symmetric module = only one bit is set */
			}
			sdram_data->ca_setup = sdram_data->data_setup = 0x15;
			sdram_data->ca_hold = sdram_data->data_hold = 0x08;

			sdram_data->spd_rev = 0x12;
			for (i = spd_write_part_no(sdram_data->part_no, "SDR", rows[row]);
			     i < sizeof(sdram_data->part_no); i++)
				sdram_data->part_no[i] = ' '; /* part number should be space-padded */
			sdram_data->rev_code[0] = BCD8(EMU_VERSION_MAJ);
			sdram_data->rev_code[1] = BCD8(EMU_VERSION_MIN);
			sdram_data->mfg_year = 20;
			sdram_data->mfg_week = 13;

			sdram_data->freq = 100;
			sdram_data->features = 0xFF;

			for (i = 0; i < 63; i++)
				sdram_data->checksum += spd_modules[slot]->data[i];
			for (i = 0; i < 129; i++)
				sdram_data->checksum2 += spd_modules[slot]->data[i];
			break;
	}

	row++;
    }

    device_add(&spd_device);
}


void
spd_write_drbs(uint8_t *regs, uint8_t reg_min, uint8_t reg_max, uint8_t drb_unit)
{
    uint8_t row, dimm, drb, apollo = 0;
    uint16_t size, rows[SPD_MAX_SLOTS];

    /* Special case for VIA Apollo Pro family, which jumps from 5F to 56. */
    if (reg_max < reg_min) {
	apollo = reg_max;
	reg_max = reg_min + 7;
    }

    /* No SPD: split SIMMs into pairs as if they were "DIMM"s. */
    if (!spd_present) {
	dimm = ((reg_max - reg_min) + 1) >> 1; /* amount of "DIMM"s, also used to determine the maximum "DIMM" size */
	spd_populate(rows, dimm, mem_size >> 10, drb_unit, 1 << (log2i((machines[machine].max_ram >> 10) / dimm)), 0);
    }

    /* Write DRBs for each row. */
    spd_log("SPD: Writing DRBs... regs=[%02X:%02X] unit=%d\n", reg_min, reg_max, drb_unit);
    for (row = 0; row <= (reg_max - reg_min); row++) {
	dimm = (row >> 1);
	size = 0;

	if (spd_present) {
		/* SPD enabled: use SPD info for this slot, if present. */
		if (spd_modules[dimm]) {
			if (spd_modules[dimm]->row1 < drb_unit) /* hack within a hack: turn a double-sided DIMM that is too small into a single-sided one */
				size = (row & 1) ? 0 : drb_unit;
			else
				size = (row & 1) ? spd_modules[dimm]->row2 : spd_modules[dimm]->row1;
		}
	} else {
		/* No SPD: use the values calculated above. */
		size = (rows[dimm] >> 1);
	}

	/* Determine the DRB register to write. */
	drb = reg_min + row;
	if (apollo && ((drb & 0xf) < 0xa))
		drb = apollo + (drb & 0xf);

	/* Write DRB register, adding the previous DRB's value. */
	if (row == 0)
		regs[drb] = 0;
	else if ((apollo) && (drb == apollo))
		regs[drb] = regs[drb | 0xf]; /* 5F comes before 56 */
	else
		regs[drb] = regs[drb - 1];
	if (size)
		regs[drb] += size / drb_unit; /* this will intentionally overflow on 440GX with 2 GB */
	spd_log("SPD: DRB[%d] = %d MB (%02Xh raw)\n", row, size, regs[drb]);
    }
}


/* Needed for 430LX. */
void
spd_write_drbs_with_ext(uint8_t *regs, uint8_t reg_min, uint8_t reg_max, uint8_t drb_unit)
{
    uint8_t row, dimm, drb;
    uint16_t size, row_val = 0, rows[SPD_MAX_SLOTS];
    int shift;

    /* No SPD: split SIMMs into pairs as if they were "DIMM"s. */
    if (!spd_present) {
	dimm = ((reg_max - reg_min) + 1) >> 1; /* amount of "DIMM"s, also used to determine the maximum "DIMM" size */
	spd_populate(rows, dimm, mem_size >> 10, drb_unit, 1 << (log2i((machines[machine].max_ram >> 10) / dimm)), 0);
    }

    /* Write DRBs for each row. */
    spd_log("SPD: Writing DRBs... regs=[%02X:%02X] unit=%d\n", reg_min, reg_max, drb_unit);
    for (row = 0; row <= (reg_max - reg_min); row++) {
	dimm = (row >> 1);
	size = 0;

	if (spd_present) {
		/* SPD enabled: use SPD info for this slot, if present. */
		if (spd_modules[dimm]) {
			if (spd_modules[dimm]->row1 < drb_unit) /* hack within a hack: turn a double-sided DIMM that is too small into a single-sided one */
				size = (row & 1) ? 0 : drb_unit;
			else
				size = (row & 1) ? spd_modules[dimm]->row2 : spd_modules[dimm]->row1;
		}
	} else {
		/* No SPD: use the values calculated above. */
		size = (rows[dimm] >> 1);
	}

	/* Determine the DRB register to write. */
	drb = reg_min + row;

	/* Write DRB register, adding the previous DRB's value. */
	if (row == 0)
		row_val = 0;
	if (size)
		row_val += size / drb_unit; /* this will intentionally overflow on 440GX with 2 GB */
	regs[drb] = row_val & 0xff;
	drb = reg_min + 8 + (row >> 1);
	shift = (row & 0x01) << 3;
	regs[drb] = (((row_val & 0xfff) >> 8) << shift);
	spd_log("SPD: DRB[%d] = %d MB (%02Xh raw)\n", row, size, regs[drb]);
    }
}


/* Used by ALi M1531 and M1541/2. */
void
spd_write_drbs_interleaved(uint8_t *regs, uint8_t reg_min, uint8_t reg_max, uint8_t drb_unit)
{
    uint8_t row, dimm;
    uint8_t drb;
    uint16_t size, size_acc = 0;
    uint16_t rows[SPD_MAX_SLOTS];

    /* No SPD: split SIMMs into pairs as if they were "DIMM"s. */
    if (!spd_present) {
	dimm = ((reg_max - reg_min) + 1) >> 2; /* amount of "DIMM"s, also used to determine the maximum "DIMM" size */
	spd_populate(rows, dimm, mem_size >> 10, drb_unit, 1 << (log2i((machines[machine].max_ram >> 10) / dimm)), 0);
    }

    /* Write DRBs for each row. */
    spd_log("SPD: Writing DRBs... regs=[%02X:%02X] unit=%d\n", reg_min, reg_max, drb_unit);
    for (row = 0; row <= (reg_max - reg_min); row += 2) {
	dimm = (row >> 2);
	size = 0;

	if (spd_present) {
		/* SPD enabled: use SPD info for this slot, if present. */
		if (spd_modules[dimm]) {
			if (spd_modules[dimm]->row1 < drb_unit) /* hack within a hack: turn a double-sided DIMM that is too small into a single-sided one */
				size = ((row >> 1) & 1) ? 0 : drb_unit;
			else
				size = ((row >> 1) & 1) ? spd_modules[dimm]->row2 : spd_modules[dimm]->row1;
		}
	} else {
		/* No SPD: use the values calculated above. */
		size = (rows[dimm] >> 1);
	}

	/* Determine the DRB register to write. */
	drb = reg_min + row;

	/* Calculate previous and new size. */
	if (row == 0)
		size_acc = 0;
	else
		size_acc += (size / drb_unit);

	/* Write DRB register, adding the previous DRB's value. */
	regs[drb] = size_acc & 0xff;
	regs[drb + 1] = (regs[drb + 1] & 0xf0) | ((size_acc >> 8) & 0x0f);

	spd_log("SPD: DRB[%d] = %d MB (%02Xh raw)\n", row >> 1, size, regs[drb]);
    }
}


/* This is needed because the ALi M1621 does this stuff completely differently,
   as it has DRAM bank registers instead of DRAM row boundary registers. */
void
spd_write_drbs_ali1621(uint8_t *regs, uint8_t reg_min, uint8_t reg_max)
{
    uint8_t dimm, drb;
    uint16_t size;
    uint16_t rows[SPD_MAX_SLOTS];

    /* No SPD: split SIMMs into pairs as if they were "DIMM"s. */
    if (!spd_present) {
	dimm = ((reg_max - reg_min) + 1) >> 2; /* amount of "DIMM"s, also used to determine the maximum "DIMM" size */
	spd_populate(rows, dimm, mem_size >> 10, 4, 1 << (log2i((machines[machine].max_ram >> 10) / dimm)), 0);
    }

    /* Write DRBs for each row. */
    spd_log("SPD: Writing DRBs... regs=[%02X:%02X] unit=%d\n", reg_min, reg_max, drb_unit);
    for (dimm = 0; dimm <= ((reg_max - reg_min) >> 2); dimm++) {
	size = 0;
	drb = reg_min + (dimm << 2);

	regs[drb] = 0xff;
	regs[drb + 1] = 0xff;
	regs[drb + 2] = 0x00;
	regs[drb + 3] = 0xf0;

	if (spd_modules[dimm] == NULL)
		continue;

	if (spd_present) {
		/* SPD enabled: use SPD info for this slot, if present. */
		size = (spd_modules[dimm]->row1 + spd_modules[dimm]->row2) >> 1;
	} else {
		/* No SPD: use the values calculated above. */
		size = (rows[dimm] >> 1);
	}

	if (spd_modules[dimm]->row1)
		regs[drb + 3] |= 0x06;

	switch (size) {
		case 4:
		default:
			regs[drb + 2] = 0x00;
			break;
		case 8:
			regs[drb + 2] = 0x10;
			break;
		case 16:
			regs[drb + 2] = 0x20;
			break;
		case 32:
			regs[drb + 2] = 0x30;
			break;
		case 64:
			regs[drb + 2] = 0x40;
			break;
		case 128:
			regs[drb + 2] = 0x50;
			break;
		case 256:
			regs[drb + 2] = 0x60;
			break;
	}

	if (spd_modules[dimm]->row2) {
		regs[drb + 3] |= 0x01;
		regs[drb + 2] |= 0x80;
	}

	spd_log("SPD: DIMM %i: %02X %02X %02X %02X\n", regs[drb], regs[drb + 1], regs[drb + 2], regs[drb + 3]);
    }
}


static const device_t spd_device = {
    .name = "Serial Presence Detect ROMs",
    .internal_name = "spd",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = spd_init,
    .close = spd_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
