/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the generic device interface to handle
 *		all devices attached to the emulator.
 *
 * Version:	@(#)device.c	1.0.5	2018/03/18
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 *		Copyright 2016-2018 Miran Grca.
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
#include <wchar.h>
#include "86box.h"
#include "cpu/cpu.h"
#include "config.h"
#include "device.h"
#include "machine/machine.h"
#include "sound/sound.h"


#define DEVICE_MAX	256			/* max # of devices */


static void	*device_priv[DEVICE_MAX];
static device_t	*devices[DEVICE_MAX];
static device_t	*device_current;


void
device_init(void)
{
    memset(devices, 0x00, sizeof(devices));
}


void *
device_add(const device_t *d)
{
    void *priv = NULL;
    int c;

    for (c=0; c<256; c++) {
	if (devices[c] == (device_t *)d) {
		pclog("DEVICE: device already exists!\n");
		return(NULL);
	}
	if (devices[c] == NULL) break;
    }
    if (c >= DEVICE_MAX)
	fatal("DEVICE: too many devices\n");

    device_current = (device_t *)d;

    if (d->init != NULL) {
	priv = d->init(d);
	if (priv == NULL) {
		if (d->name)
			pclog("DEVICE: device '%s' init failed\n", d->name);
		  else
			pclog("DEVICE: device init failed\n");
		return(NULL);
	}
    }

    devices[c] = (device_t *)d;
    device_priv[c] = priv;

    return(priv);
}


/* For devices that do not have an init function (internal video etc.) */
void
device_add_ex(const device_t *d, void *priv)
{
    int c;

    for (c=0; c<256; c++) {
	if (devices[c] == (device_t *)d) {
		fatal("device_add: device already exists!\n");
		break;
	}
	if (devices[c] == NULL) break;
    }
    if (c >= DEVICE_MAX)
	fatal("device_add: too many devices\n");

    device_current = (device_t *)d;

    devices[c] = (device_t *)d;
    device_priv[c] = priv;
}


void
device_close_all(void)
{
    int c;

    for (c=0; c<DEVICE_MAX; c++) {
	if (devices[c] != NULL) {
		if (devices[c]->close != NULL)
			devices[c]->close(device_priv[c]);
		devices[c] = device_priv[c] = NULL;
	}
    }
}


void
device_reset_all(void)
{
    int c;

    for (c=0; c<DEVICE_MAX; c++) {
	if (devices[c] != NULL) {
		if (devices[c]->reset != NULL)
			devices[c]->reset(device_priv[c]);
	}
    }
}


void *
device_get_priv(const device_t *d)
{
    int c;

    for (c=0; c<DEVICE_MAX; c++) {
	if (devices[c] != NULL) {
		if (devices[c] == d)
			return(device_priv[c]);
	}
    }

    return(NULL);
}


int
device_available(const device_t *d)
{
#ifdef RELEASE_BUILD
    if (d->flags & DEVICE_NOT_WORKING) return(0);
#endif
    if (d->available != NULL)
	return(d->available());

    return(1);
}


void
device_speed_changed(void)
{
    int c;

    for (c=0; c<DEVICE_MAX; c++) {
	if (devices[c] != NULL) {
		if (devices[c]->speed_changed != NULL)
			devices[c]->speed_changed(device_priv[c]);
	}
    }

    sound_speed_changed();
}


void
device_force_redraw(void)
{
    int c;

    for (c=0; c<DEVICE_MAX; c++) {
	if (devices[c] != NULL) {
		if (devices[c]->force_redraw != NULL)
                                devices[c]->force_redraw(device_priv[c]);
	}
    }
}


void
device_add_status_info(char *s, int max_len)
{
    int c;

    for (c=0; c<DEVICE_MAX; c++) {
	if (devices[c] != NULL) {
		if (devices[c]->add_status_info != NULL)
			devices[c]->add_status_info(s, max_len, device_priv[c]);
	}
    }
}


char *
device_get_config_string(char *s)
{
    const device_config_t *c = device_current->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_string((char *)device_current->name, s, (char *)c->default_string));

	c++;
    }

    return(NULL);
}


int
device_get_config_int(char *s)
{
    const device_config_t *c = device_current->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_int((char *)device_current->name, s, c->default_int));

	c++;
    }

    return(0);
}


int
device_get_config_int_ex(char *s, int default_int)
{
    const device_config_t *c = device_current->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_int((char *)device_current->name, s, default_int));

	c++;
    }

    return(default_int);
}


int
device_get_config_hex16(char *s)
{
    const device_config_t *c = device_current->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_hex16((char *)device_current->name, s, c->default_int));

	c++;
    }

    return(0);
}


int
device_get_config_hex20(char *s)
{
    const device_config_t *c = device_current->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_hex20((char *)device_current->name, s, c->default_int));

	c++;
    }

    return(0);
}


int
device_get_config_mac(char *s, int default_int)
{
    const device_config_t *c = device_current->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_mac((char *)device_current->name, s, default_int));

	c++;
    }

    return(default_int);
}


void
device_set_config_int(char *s, int val)
{
    const device_config_t *c = device_current->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name)) {
 		config_set_int((char *)device_current->name, s, val);
		break;
	}

	c++;
    }
}


void
device_set_config_hex16(char *s, int val)
{
    const device_config_t *c = device_current->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name)) {
		config_set_hex16((char *)device_current->name, s, val);
		break;
	}

	c++;
    }
}


void
device_set_config_hex20(char *s, int val)
{
    const device_config_t *c = device_current->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name)) {
		config_set_hex20((char *)device_current->name, s, val);
		break;
	}

	c++;
    }
}


void
device_set_config_mac(char *s, int val)
{
    const device_config_t *c = device_current->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name)) {
		config_set_mac((char *)device_current->name, s, val);
		break;
	}

	c++;
    }
}


int
device_is_valid(const device_t *device, int mflags)
{
    if (device == NULL) return(1);

    if ((device->flags & DEVICE_AT) && !(mflags & MACHINE_AT)) return(0);

    if ((device->flags & DEVICE_CBUS) && !(mflags & MACHINE_CBUS)) return(0);

    if ((device->flags & DEVICE_ISA) && !(mflags & MACHINE_ISA)) return(0);

    if ((device->flags & DEVICE_MCA) && !(mflags & MACHINE_MCA)) return(0);

    if ((device->flags & DEVICE_EISA) && !(mflags & MACHINE_EISA)) return(0);

    if ((device->flags & DEVICE_VLB) && !(mflags & MACHINE_VLB)) return(0);

    if ((device->flags & DEVICE_PCI) && !(mflags & MACHINE_PCI)) return(0);

    if ((device->flags & DEVICE_PS2) && !(mflags & MACHINE_HDC_PS2)) return(0);
    if ((device->flags & DEVICE_AGP) && !(mflags & MACHINE_AGP)) return(0);

    return(1);
}


int
machine_get_config_int(char *s)
{
    const device_t *d = machine_getdevice(machine);
    const device_config_t *c;

    if (d == NULL) return(0);

    c = d->config;
    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_int((char *)d->name, s, c->default_int));

	c++;
    }

    return(0);
}


char *
machine_get_config_string(char *s)
{
    const device_t *d = machine_getdevice(machine);
    const device_config_t *c;

    if (d == NULL) return(0);

    c = d->config;
    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_string((char *)d->name, s, (char *)c->default_string));

	c++;
    }

    return(NULL);
}
