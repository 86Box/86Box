/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Handling of the PS/2 series CMOS devices.
 *
 * Version:	@(#)nvr_ps2.c	1.0.5	2018/03/18
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 *		Copyright 2008-2018 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "86box.h"
#include "cpu/cpu.h"
#include "machine/machine.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "nvr.h"
#include "nvr_ps2.h"
#include "rom.h"


typedef struct {
    int		addr;

    uint8_t	ram[8192];
} ps2_nvr_t;


static uint8_t
ps2_nvr_read(uint16_t port, void *priv)
{
    ps2_nvr_t *nvr = (ps2_nvr_t *)priv;
    uint8_t ret = 0xff;

    switch (port) {
	case 0x74:
		ret = nvr->addr & 0xff;
		break;

	case 0x75:
		ret = nvr->addr >> 8;
		break;

	case 0x76:
		ret = nvr->ram[nvr->addr];
		break;
    }

    return(ret);
}


static void
ps2_nvr_write(uint16_t port, uint8_t val, void *priv)
{
    ps2_nvr_t *nvr = (ps2_nvr_t *)priv;

    switch (port) {
	case 0x74:
		nvr->addr = (nvr->addr & 0x1f00) | val;
		break;

	case 0x75:
		nvr->addr = (nvr->addr & 0xff) | ((val & 0x1f) << 8);
		break;

	case 0x76:
		nvr->ram[nvr->addr] = val;
		break;
    }
}


static void *
ps2_nvr_init(const device_t *info)
{
    ps2_nvr_t *nvr;
    FILE *f = NULL;

    nvr = (ps2_nvr_t *)malloc(sizeof(ps2_nvr_t));
    memset(nvr, 0x00, sizeof(ps2_nvr_t));
	
    io_sethandler(0x0074, 3,
		  ps2_nvr_read,NULL,NULL, ps2_nvr_write,NULL,NULL, nvr);

    switch (romset) {
	case ROM_IBMPS2_M80:
		f = nvr_fopen(L"ibmps2_m80_sec.nvr", L"rb");
		break;
    }

    memset(nvr->ram, 0xff, 8192);
    if (f != NULL) {
	(void)fread(nvr->ram, 8192, 1, f);
	fclose(f);
    }

    return(nvr);
}


static void
ps2_nvr_close(void *priv)
{
    ps2_nvr_t *nvr = (ps2_nvr_t *)priv;
    FILE *f = NULL;

    switch (romset) {
	case ROM_IBMPS2_M70_TYPE3:
		f = nvr_fopen(L"ibmps2_m70_type3_sec.nvr", L"rb");
		break;
	case ROM_IBMPS2_M70_TYPE4:
		f = nvr_fopen(L"ibmps2_m70_type4_sec.nvr", L"rb");
		break;
	case ROM_IBMPS2_M80:
		f = nvr_fopen(L"ibmps2_m80_sec.nvr", L"wb");
		break;
    }

    if (f != NULL) {
	(void)fwrite(nvr->ram, 8192, 1, f);
	fclose(f);
    }

    free(nvr->ram);

    free(nvr);
}


const device_t ps2_nvr_device = {
    "PS/2 Secondary NVRAM",
    0, 0,
    ps2_nvr_init, ps2_nvr_close, NULL,
    NULL, NULL, NULL,
    NULL
};
