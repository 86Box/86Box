/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Debug device for assisting in unit testing.
 *          See doc/specifications/86box-unit-tester.md for more info.
 *          If modifying the protocol, you MUST modify the specification
 *          and increment the version number.
 *
 * Authors: GreaseMonkey, <thematrixeatsyou+86b@gmail.com>
 *
 *          Copyright 2024 GreaseMonkey.
 */
#ifndef UNITTESTER_H
#define UNITTESTER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */
extern const device_t unittester_device;

/* Functions. */

#ifdef __cplusplus
}
#endif

#endif /*UNITTESTER_H*/
