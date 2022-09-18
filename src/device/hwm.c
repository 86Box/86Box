/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Common functions for hardware monitoring chips.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/hwm.h>

/* Refer to specific hardware monitor implementations for the meaning of hwm_values. */
hwm_values_t hwm_values;

uint16_t
hwm_get_vcore()
{
    /* Determine Vcore for the active CPU. */
    return cpu_s->voltage;
}
