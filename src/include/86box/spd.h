/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of SPD (Serial Presence Detect) devices.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2020 RichardG.
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

typedef struct spd_edo_t {
    uint8_t bytes_used;
    uint8_t spd_size;
    uint8_t mem_type;
    uint8_t row_bits;
    uint8_t col_bits;
    uint8_t banks;
    uint8_t data_width_lsb;
    uint8_t data_width_msb;
    uint8_t signal_level;
    uint8_t trac;
    uint8_t tcac;
    uint8_t config;
    uint8_t refresh_rate;
    uint8_t dram_width;
    uint8_t ecc_width;
    uint8_t reserved[47];
    uint8_t spd_rev;
    uint8_t checksum;
    uint8_t mfg_jedec[8];
    uint8_t mfg_loc;
    char    part_no[18];
    uint8_t rev_code[2];
    uint8_t mfg_year;
    uint8_t mfg_week;
    uint8_t serial[4];
    uint8_t mfg_specific[27];
    uint8_t vendor_specific[2];
    uint8_t other_data[127];
    uint8_t checksum2;
} spd_edo_t;

typedef struct spd_sdram_t {
    uint8_t bytes_used;
    uint8_t spd_size;
    uint8_t mem_type;
    uint8_t row_bits;
    uint8_t col_bits;
    uint8_t rows;
    uint8_t data_width_lsb;
    uint8_t data_width_msb;
    uint8_t signal_level;
    uint8_t tclk;
    uint8_t tac;
    uint8_t config;
    uint8_t refresh_rate;
    uint8_t sdram_width;
    uint8_t ecc_width;
    uint8_t tccd;
    uint8_t burst;
    uint8_t banks;
    uint8_t cas;
    uint8_t cslat;
    uint8_t we;
    uint8_t mod_attr;
    uint8_t dev_attr;
    uint8_t tclk2;
    uint8_t tac2;
    uint8_t tclk3;
    uint8_t tac3;
    uint8_t trp;
    uint8_t trrd;
    uint8_t trcd;
    uint8_t tras;
    uint8_t bank_density;
    uint8_t ca_setup;
    uint8_t ca_hold;
    uint8_t data_setup;
    uint8_t data_hold;
    uint8_t reserved[26];
    uint8_t spd_rev, checksum;
    uint8_t mfg_jedec[8];
    uint8_t mfg_loc;
    char    part_no[18];
    uint8_t rev_code[2];
    uint8_t mfg_year;
    uint8_t mfg_week;
    uint8_t serial[4];
    uint8_t mfg_specific[27];
    uint8_t freq;
    uint8_t features;
    uint8_t other_data[127];
    uint8_t checksum2;
} spd_sdram_t;

typedef struct spd_t {
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
