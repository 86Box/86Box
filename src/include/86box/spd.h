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

#ifndef EMU_SPD_H
#define EMU_SPD_H

#define SPD_BASE_ADDR           0x50
#define SPD_MAX_SLOTS           8
#define SPD_DATA_SIZE           256

#define SPD_TYPE_FPM            0x01
#define SPD_TYPE_EDO            0x02
#define SPD_TYPE_SDRAM          0x04

#define SPD_MIN_SIZE_EDO        8
#define SPD_MIN_SIZE_SDRAM      8

#define SPD_SIGNAL_LVTTL        0x01

#define SPD_REFRESH_NORMAL      0x00
#define SPD_SDR_REFRESH_SELF    0x80

#define SPD_SDR_BURST_PAGE      0x80

#define SPD_SDR_ATTR_BUFFERED   0x01
#define SPD_SDR_ATTR_REGISTERED 0x02

#define SPD_SDR_ATTR_EARLY_RAS  0x01
#define SPD_SDR_ATTR_AUTO_PC    0x02
#define SPD_SDR_ATTR_PC_ALL     0x04
#define SPD_SDR_ATTR_W1R_BURST  0x08
#define SPD_SDR_ATTR_VCC_LOW_5  0x10
#define SPD_SDR_ATTR_VCC_HI_5   0x20

typedef struct {
    uint8_t bytes_used, spd_size, mem_type,
        row_bits, col_bits, banks,
        data_width_lsb, data_width_msb,
        signal_level, trac, tcac,
        config, refresh_rate,
        dram_width, ecc_width,
        reserved[47],
        spd_rev, checksum,
        mfg_jedec[8], mfg_loc;
    char    part_no[18];
    uint8_t rev_code[2],
        mfg_year, mfg_week, serial[4], mfg_specific[27],
        vendor_specific[2],
        other_data[127],
        checksum2;
} spd_edo_t;

typedef struct {
    uint8_t bytes_used, spd_size, mem_type,
        row_bits, col_bits, rows,
        data_width_lsb, data_width_msb,
        signal_level, tclk, tac,
        config, refresh_rate,
        sdram_width, ecc_width,
        tccd, burst, banks, cas, cslat, we,
        mod_attr, dev_attr,
        tclk2, tac2, tclk3, tac3,
        trp, trrd, trcd, tras,
        bank_density,
        ca_setup, ca_hold, data_setup, data_hold,
        reserved[26],
        spd_rev, checksum,
        mfg_jedec[8], mfg_loc;
    char    part_no[18];
    uint8_t rev_code[2],
        mfg_year, mfg_week, serial[4], mfg_specific[27],
        freq, features,
        other_data[127],
        checksum2;
} spd_sdram_t;

typedef struct {
    uint8_t  slot;
    uint16_t size;
    uint16_t row1;
    uint16_t row2;

    union {
        uint8_t     data[SPD_DATA_SIZE];
        spd_edo_t   edo_data;
        spd_sdram_t sdram_data;
    };
    void *eeprom;
} spd_t;

extern void spd_register(uint8_t ram_type, uint8_t slot_mask, uint16_t max_module_size);
extern void spd_write_drbs(uint8_t *regs, uint8_t reg_min, uint8_t reg_max, uint8_t drb_unit);
extern void spd_write_drbs_with_ext(uint8_t *regs, uint8_t reg_min, uint8_t reg_max, uint8_t drb_unit);
extern void spd_write_drbs_interleaved(uint8_t *regs, uint8_t reg_min, uint8_t reg_max, uint8_t drb_unit);
extern void spd_write_drbs_ali1621(uint8_t *regs, uint8_t reg_min, uint8_t reg_max);

#endif /*EMU_SPD_H*/
