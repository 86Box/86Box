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
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2008-2019 Sarah Walker.
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/sound.h>


#define DEVICE_MAX	256			/* max # of devices */


static device_t		*devices[DEVICE_MAX];
static void		*device_priv[DEVICE_MAX];
static device_context_t	device_current, device_prev;


#ifdef ENABLE_DEVICE_LOG
int device_do_log = ENABLE_DEVICE_LOG;


static void
device_log(const char *fmt, ...)
{
    va_list ap;

    if (device_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define device_log(fmt, ...)
#endif


/* Initialize the module for use. */
void
device_init(void)
{
    memset(devices, 0x00, sizeof(devices));
}


void
device_set_context(device_context_t *c, const device_t *d, int inst)
{
    memset(c, 0, sizeof(device_context_t));
    c->dev = d;
    if (inst)
    	sprintf(c->name, "%s #%i", d->name, inst);
    else
    	sprintf(c->name, "%s", d->name);
}


static void
device_context_common(const device_t *d, int inst)
{
    memcpy(&device_prev, &device_current, sizeof(device_context_t));
    device_set_context(&device_current, d, inst);
}


void
device_context(const device_t *d)
{
    device_context_common(d, 0);
}


void
device_context_inst(const device_t *d, int inst)
{
    device_context_common(d, inst);
}


void
device_context_restore(void)
{
    memcpy(&device_current, &device_prev, sizeof(device_context_t));
}


static void *
device_add_common(const device_t *d, const device_t *cd, void *p, int inst)
{
    void *priv = NULL;
    int c;

    for (c = 0; c < 256; c++) {
	if (!inst && (devices[c] == (device_t *) d)) {
		device_log("DEVICE: device already exists!\n");
		return (NULL);
	}
	if (devices[c] == NULL) break;
    }
    if (c >= DEVICE_MAX)
	fatal("DEVICE: too many devices\n");

    /* Do this so that a chained device_add will not identify the same ID
       its master device is already trying to assign. */
    devices[c] = (device_t *)d;

    if (p == NULL) {
	memcpy(&device_prev, &device_current, sizeof(device_context_t));
	device_set_context(&device_current, cd, inst);

	if (d->init != NULL) {
		priv = d->init(d);
		if (priv == NULL) {
			if (d->name)
				device_log("DEVICE: device '%s' init failed\n", d->name);
			else
				device_log("DEVICE: device init failed\n");

			devices[c] = NULL;
			device_priv[c] = NULL;

			return(NULL);
		}
	}

	if (d->name)
		device_log("DEVICE: device '%s' init successful\n", d->name);
	else
		device_log("DEVICE: device init successful\n");

	memcpy(&device_current, &device_prev, sizeof(device_context_t));
	device_priv[c] = priv;
    } else
	device_priv[c] = p;

    return(priv);
}


void *
device_add(const device_t *d)
{
    return device_add_common(d, d, NULL, 0);
}


/* For devices that do not have an init function (internal video etc.) */
void
device_add_ex(const device_t *d, void *priv)
{
    device_add_common(d, d, priv, 0);
}


void *
device_add_inst(const device_t *d, int inst)
{
    return device_add_common(d, d, NULL, inst);
}


/* For devices that do not have an init function (internal video etc.) */
void
device_add_inst_ex(const device_t *d, void *priv, int inst)
{
    device_add_common(d, d, priv, inst);
}


/* These four are to add a device with another device's context - will be
   used to add machines' internal devices. */
void *
device_cadd(const device_t *d, const device_t *cd)
{
    return device_add_common(d, cd, NULL, 0);
}


/* For devices that do not have an init function (internal video etc.) */
void
device_cadd_ex(const device_t *d, const device_t *cd, void *priv)
{
    device_add_common(d, cd, priv, 0);
}


void *
device_cadd_inst(const device_t *d, const device_t *cd, int inst)
{
    return device_add_common(d, cd, NULL, inst);
}


/* For devices that do not have an init function (internal video etc.) */
void
device_cadd_inst_ex(const device_t *d, const device_t *cd, void *priv, int inst)
{
    device_add_common(d, cd, priv, inst);
}


void
device_close_all(void)
{
    int c;

    for (c = (DEVICE_MAX - 1); c >= 0; c--) {
	if (devices[c] != NULL) {
		if (devices[c]->name)
			device_log("Closing device: \"%s\"...\n", devices[c]->name);
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

    for (c = 0; c < DEVICE_MAX; c++) {
	if (devices[c] != NULL) {
		if (devices[c]->reset != NULL)
			devices[c]->reset(device_priv[c]);
	}
    }
}


/* Reset all attached PCI devices - needed for PCI turbo reset control. */
void
device_reset_all_pci(void)
{
    int c;

    for (c=0; c<DEVICE_MAX; c++) {
	if (devices[c] != NULL) {
		if ((devices[c]->reset != NULL) && (devices[c]->flags & DEVICE_PCI))
			devices[c]->reset(device_priv[c]);
	}
    }
}


void *
device_get_priv(const device_t *d)
{
    int c;

    for (c = 0; c < DEVICE_MAX; c++) {
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

    for (c = 0; c < DEVICE_MAX; c++) {
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

    for (c = 0; c < DEVICE_MAX; c++) {
	if (devices[c] != NULL) {
		if (devices[c]->force_redraw != NULL)
                                devices[c]->force_redraw(device_priv[c]);
	}
    }
}


const char *
device_get_config_string(const char *s)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_string((char *) device_current.name, (char *) s, (char *) c->default_string));

	c++;
    }

    return(NULL);
}


int
device_get_config_int(const char *s)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_int((char *) device_current.name, (char *) s, c->default_int));

	c++;
    }

    return(0);
}


int
device_get_config_int_ex(const char *s, int def)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_int((char *) device_current.name, (char *) s, def));

	c++;
    }

    return(def);
}


int
device_get_config_hex16(const char *s)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_hex16((char *) device_current.name, (char *) s, c->default_int));

	c++;
    }

    return(0);
}


int
device_get_config_hex20(const char *s)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_hex20((char *) device_current.name, (char *) s, c->default_int));

	c++;
    }

    return(0);
}


int
device_get_config_mac(const char *s, int def)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name))
		return(config_get_mac((char *) device_current.name, (char *) s, def));

	c++;
    }

    return(def);
}


void
device_set_config_int(const char *s, int val)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name)) {
 		config_set_int((char *) device_current.name, (char *) s, val);
		break;
	}

	c++;
    }
}


void
device_set_config_hex16(const char *s, int val)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name)) {
		config_set_hex16((char *) device_current.name, (char *) s, val);
		break;
	}

	c++;
    }
}


void
device_set_config_hex20(const char *s, int val)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name)) {
		config_set_hex20((char *) device_current.name, (char *) s, val);
		break;
	}

	c++;
    }
}


void
device_set_config_mac(const char *s, int val)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
	if (! strcmp(s, c->name)) {
		config_set_mac((char *) device_current.name, (char *) s, val);
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
