/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		AV9194 clock generator emulation.
 *
 *		Used by the S3 86c801 (V7-Mirage) card.
 *
 * Version:	@(#)vid_av9194.c	1.0.1	2019/01/12
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "device.h"
#include "mem.h"
#include "timer.h"
#include "video.h"
#include "vid_svga.h"


float
av9194_getclock(int clock, void *p)
{
    float ret = 0.0;
    
    switch (clock & 0x0f)
    {
	case 0:
		ret = 25175000.0;
		break;
	case 1:
		ret = 28322000.0;
		break;
	case 2:
		ret = 40000000.0;
		break;
	case 4:
		ret = 50000000.0;
		break;
	case 5:
		ret = 77000000.0;
		break;
	case 6:
		ret = 36000000.0;
		break;
	case 7:
		ret = 44900000.0;
		break;
	case 8:
		ret = 130000000.0;
		break;
	case 9:
		ret = 120000000.0;
		break;
	case 0xa:
		ret = 80000000.0;
		break;
	case 0xb:
		ret = 31500000.0;
		break;
	case 0xc:
		ret = 110000000.0;
		break;
	case 0xd:
		ret = 65000000.0;
		break;
	case 0xe:
		ret = 75000000.0;
		break;
	case 0xf:
		ret = 94500000.0;
		break;
    }
    
    return ret;
}


static void *
av9194_init(const device_t *info)
{
    /* Return something non-NULL. */
    return (void *) &av9194_device;
}


const device_t av9194_device =
{
        "AV9194 Clock Generator",
        0, 0,
        av9194_init, NULL,
	NULL, NULL, NULL, NULL
};

