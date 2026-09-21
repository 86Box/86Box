/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the CRC code.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#ifndef EMU_CRC_H
#define EMU_CRC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef union {
    uint16_t word;
    uint8_t  bytes[2];
} crc_t;

extern void crc16_setup(uint16_t *crc_table, uint16_t poly);
extern void crc16_calc(uint16_t *crc_table, uint8_t byte, crc_t *crc_var);

#ifdef __cplusplus
}
#endif

#endif /*EMU_CRC_H*/
