/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Novell NetWare 2.x Key Card, which
 *          was used for anti-piracy protection.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2024 Cacodemon345.
 */
#ifndef NOVELL_KEYCARD_H
#define NOVELL_KEYCARD_H

/* I/O port range used. */
#define NOVELL_KEYCARD_ADDR    0x23a
#define NOVELL_KEYCARD_ADDRLEN 6

#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */
extern const device_t novell_keycard_device;

/* Functions. */

#ifdef __cplusplus
}
#endif

#endif /* NOVELL_KEYCARD_H */
