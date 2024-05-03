/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the NCR 53c400 series of SCSI Host Adapters
 *          made by NCR. These controllers were designed for the ISA and MCA bus.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2017-2018 Sarah Walker.
 *          Copyright 2017-2018 Fred N. van Kempen.
 *          Copyright 2017-2024 TheCollector1995.
 */

#ifndef SCSI_NCR53C400_H
#define SCSI_NCR53C400_H

typedef struct ncr53c400_t {
    rom_t         bios_rom;
    mem_mapping_t mapping;
    ncr_t   ncr;
    uint8_t buffer[512];
    uint8_t int_ram[0x40];
    uint8_t ext_ram[0x600];

    uint32_t rom_addr;
    uint16_t base;

    int8_t  type;
    uint8_t block_count;
    uint8_t status_ctrl;

    int simple_ctrl;

    int block_count_loaded;

    int buffer_pos;
    int buffer_host_pos;

    int     busy;
    uint8_t pos_regs[8];

    pc_timer_t timer;
} ncr53c400_t;

#define CTRL_DATA_DIR           0x40
#define STATUS_BUFFER_NOT_READY 0x04
#define STATUS_5380_ACCESSIBLE  0x80

extern void    ncr53c400_simple_write(uint8_t val, void *priv);
extern void    ncr53c400_write(uint32_t addr, uint8_t val, void *priv);
extern uint8_t ncr53c400_simple_read(void *priv);
extern uint8_t ncr53c400_read(uint32_t addr, void *priv);
extern void    ncr53c400_callback(void *priv);

#endif /*SCSI_NCR53C400_H*/
