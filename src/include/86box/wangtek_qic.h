/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Wangtek PC-36/PC-02 controllers.
 *
 *
 * Authors:
 *
 *          Copyright 2025 seal331.
 */
#ifndef WANGTEK_QIC_H
#define WANGTEK_QIC_H

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

/* Wangtek-specific extensions (PC-36) */
#define WANGTEK_QIC_SEL_QIC_11_FORMAT 0x26
#define WANGTEK_QIC_SEL_QIC_24_FORMAT 0x27

#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */
extern const device_t wangtek_qic_device;

/* Functions. */

#ifdef __cplusplus
}
#endif

#endif /*WANGTEK_QIC_H*/
