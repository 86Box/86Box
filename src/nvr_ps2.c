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
 *
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
#include <86box/86box.h>
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/nvr_ps2.h>
#include <86box/rom.h>


typedef struct {
    int		addr;

    uint8_t	ram[8192];

    char	*fn;
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
    int c;

    nvr = (ps2_nvr_t *)malloc(sizeof(ps2_nvr_t));
    memset(nvr, 0x00, sizeof(ps2_nvr_t));

    /* Set up the NVR file's name. */
    c = strlen(machine_get_internal_name()) + 9;
    nvr->fn = (char *)malloc(c + 1);
    sprintf(nvr->fn, "%s_sec.nvr", machine_get_internal_name());

    io_sethandler(0x0074, 3,
		  ps2_nvr_read,NULL,NULL, ps2_nvr_write,NULL,NULL, nvr);

    f = nvr_fopen(nvr->fn, "rb");

    memset(nvr->ram, 0xff, 8192);
    if (f != NULL) {
	if (fread(nvr->ram, 1, 8192, f) != 8192)
		fatal("ps2_nvr_init(): Error reading EEPROM data\n");
	fclose(f);
    }

    return(nvr);
}


static void
ps2_nvr_close(void *priv)
{
    ps2_nvr_t *nvr = (ps2_nvr_t *)priv;
    FILE *f = NULL;

    f = nvr_fopen(nvr->fn, "wb");

    if (f != NULL) {
	(void)fwrite(nvr->ram, 8192, 1, f);
	fclose(f);
    }

    free(nvr);
}


const device_t ps2_nvr_device = {
    .name = "PS/2 Secondary NVRAM",
    .internal_name = "ps2_nvr",
    .flags = 0,
    .local = 0,
    .init = ps2_nvr_init,
    .close = ps2_nvr_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
