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
#ifndef SCSI_T128_H
#define SCSI_T128_H

typedef struct t128_t {
    ncr_t         ncr;
    rom_t         bios_rom;
    mem_mapping_t mapping;

    uint8_t  ctrl;
    uint8_t  status;
    uint8_t  buffer[512];
    uint8_t  ext_ram[0x80];
    uint32_t block_count;

    int block_loaded;
    int pos;
    int host_pos;

    uint32_t rom_addr;

    int     bios_enabled;
    uint8_t pos_regs[8];
    int     type;

    pc_timer_t timer;
} t128_t;

extern void    t128_write(uint32_t addr, uint8_t val, void *priv);
extern uint8_t t128_read(uint32_t addr, void *priv);

extern void    t128_callback(void *priv);

#endif /*SCSI_T128_H*/
