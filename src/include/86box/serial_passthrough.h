/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the Serial Passthrough Virtual Device
 *
 *
 *
 * Author:	Andreas J. Reichel, <webmaster@6th-dimension.com>
 *
 *		Copyright 2021          Andreas J. Reichel.
 */

#ifndef SERIAL_PASSTHROUGH_H
#define SERIAL_PASSTHROUGH_H


#include <stdint.h>
#include <stdbool.h>

void serial_passthrough_init(void);
uint8_t serial_passthrough_create(uint8_t com_port);

#endif
