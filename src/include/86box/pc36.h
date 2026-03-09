/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Wangtek PC-36 tape controller.
 *
 *
 * Authors: 
 *
 *          Copyright 2025 seal331.
 */
#ifndef PC36_H
#define PC36_H

#define PC36_SEL_QIC_11_FORMAT 0x26
#define PC36_SEL_QIC_24_FORMAT 0x27

#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */
extern const device_t wangtek_qic_device;

/* Functions. */

#ifdef __cplusplus
}
#endif

#endif /*PC36_H*/
