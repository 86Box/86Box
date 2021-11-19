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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <86box/plat.h>
#include <86box/plat_serial_passthrough.h>


void plat_serpt_write(void *p, uint8_t data)
{
	/* TODO: write to host */
}
