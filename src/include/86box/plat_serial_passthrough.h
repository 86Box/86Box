/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for platform specific serial to host passthrough
 *
 *
 *
 * Authors:	Andreas J. Reichel <webmaster@6th-dimension.com>
 *
 *		Copyright 2021		Andreas J. Reichel
 */
#ifndef PLAT_SERIAL_PASSTHROUGH_H
#define PLAT_SERIAL_PASSTHROUGH_H

#if (defined(__unix__) || defined(__APPLE__)) && !defined(__linux__)

#include <86box/plat_serpt_unix.h>

#elif defined(_WIN32)

#include <86box/plat_serpt_win32.h>

#endif

#ifdef __cplusplus
extern "C" {
#endif

extern void plat_serpt_write(void *p, uint8_t data);

#ifdef __cplusplus
extern }
#endif


#endif
