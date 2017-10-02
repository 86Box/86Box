/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Common code to handle all sorts of hard disk images.
 *
 * Version:	@(#)hdd.c	1.0.2	2017/09/30
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../ibm.h"
#include "../cpu/cpu.h"
#include "../device.h"
#include "../machine/machine.h"
#include "hdd.h"


hard_disk_t	hdd[HDD_NUM];


int
hdd_init(void)
{
    /* Clear all global data. */
    memset(hdd, 0x00, sizeof(hdd));

    return(0);
}
