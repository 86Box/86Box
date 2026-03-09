/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Generic QIC-02 interface.
 *
 *
 * Authors:
 *
 *          Copyright 2025 seal331.
 */
#ifndef QIC_GENERIC_H
#define QIC_GENERIC_H

/* QIC-02 command list. */
#define QIC_SEL_DRIVE_0 0x01
#define QIC_SEL_DRIVE_1 0x02
#define QIC_SEL_DRIVE_2 0x04
#define QIC_REW_TO_BOT 0x21
#define QIC_ERASE_TAPE 0x22
#define QIC_INIT_TAPE 0x24
#define QIC_WRITE_DATA 0x40
#define QIC_WRITE_FILE_MARK 0x60
#define QIC_READ_DATA 0x80
#define QIC_READ_FILE_MARK 0xa0
#define QIC_READ_STATUS 0xc0

#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */

/* Functions. */
extern void wangtek_qic_sel_drive_0();
extern void wangtek_qic_sel_drive_1();
extern void wangtek_qic_sel_drive_2();
extern void wangtek_qic_rew_to_bot();
extern void wangtek_qic_erase_tape();
extern void wangtek_qic_init_tape();
extern void wangtek_qic_write_data();
extern void wangtek_qic_write_file_mark();
extern void wangtek_qic_read_data();
extern void wangtek_qic_read_file_mark();
extern void wangtek_qic_read_status();

#ifdef __cplusplus
}
#endif

#endif /*QIC_GENERIC_H*/
