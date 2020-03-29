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
 * Version:	@(#)spd.h	1.0.0	2020/03/24
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#ifndef EMU_SPD_H
# define EMU_SPD_H


#define SPD_BASE_ADDR		0x50

#define SPD_TYPE_EDO		0x02
#define SPD_TYPE_SDRAM		0x04

#define SPD_MIN_SIZE_SDRAM	8

#define SPD_SDR_SIGNAL_LVTTL	0x01

#define SPD_SDR_REFRESH_NORMAL	0x00
#define SPD_SDR_REFRESH_SELF	0x80

#define SPD_SDR_BURST_PAGE	0x80

#define SPD_SDR_ATTR_BUFFERED	0x01
#define SPD_SDR_ATTR_REGISTERED	0x02

#define SPD_SDR_ATTR_EARLY_RAS	0x01
#define SPD_SDR_ATTR_AUTO_PC	0x02
#define SPD_SDR_ATTR_PC_ALL	0x04
#define SPD_SDR_ATTR_W1R_BURST	0x08
#define SPD_SDR_ATTR_VCC_LOW_5	0x10
#define SPD_SDR_ATTR_VCC_HI_5	0x20


typedef struct _spd_sdram_ {
    uint8_t	bytes_used, spd_size, mem_type,
    		row_bits, col_bits, rows,
    		data_width_lsb, data_width_msb,
    		signal_level, tclk, tac,
    		config, refresh_rate,
    		sdram_width, ecc_width,
    		tccd, burst, banks, cas, cs, we,
    		mod_attr, dev_attr,
    		tclk2, tac2, tclk3, tac3,
    		trp, trrd, trcd, tras,
    		bank_density,
    		ca_setup, ca_hold, data_setup, data_hold,
    		reserved[26],
    		spd_rev, checksum,
    		mfg_jedec[8], mfg_loc;
    char	part_no[18];
    uint8_t	rev_code[2],
    		mfg_year, mfg_week, serial[4], mfg_specific[27],
    		freq, features,
    		other_data[128];
} spd_sdram_t;


extern void spd_register(uint8_t ram_type, uint8_t slot_mask, uint16_t max_module_size);


#endif	/*EMU_SPD_H*/
