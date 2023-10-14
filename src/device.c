/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the generic device interface to handle
 *          all devices attached to the emulator.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2021      Andreas J. Reichel.
 *          Copyright 2021-2022 Jasmine Iwanek.
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
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/sound.h>

#define DEVICE_MAX 256 /* max # of devices */

static device_t        *devices[DEVICE_MAX];
static void            *device_priv[DEVICE_MAX];
static device_context_t device_current;
static device_context_t device_prev;

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
#    define device_log(fmt, ...)
#endif

/* Initialize the module for use. */
void
device_init(void)
{
    memset(devices, 0x00, sizeof(devices));
}

void
device_set_context(device_context_t *c, const device_t *dev, int inst)
{
    const void *sec;
    void       *single_sec;

    memset(c, 0, sizeof(device_context_t));
    c->dev      = dev;
    c->instance = inst;
    if (inst) {
        sprintf(c->name, "%s #%i", dev->name, inst);

        /* If this is the first instance and a numbered section is not present, but a non-numbered
           section of the same name is, rename the non-numbered section to numbered. */
        if (inst == 1) {
            sec        = config_find_section(c->name);
            single_sec = config_find_section((char *) dev->name);
            if ((sec == NULL) && (single_sec != NULL))
                config_rename_section(single_sec, c->name);
        }
    } else
        sprintf(c->name, "%s", dev->name);
}

static void
device_context_common(const device_t *dev, int inst)
{
    memcpy(&device_prev, &device_current, sizeof(device_context_t));
    device_set_context(&device_current, dev, inst);
}

void
device_context(const device_t *dev)
{
    device_context_common(dev, 0);
}

void
device_context_inst(const device_t *dev, int inst)
{
    device_context_common(dev, inst);
}

void
device_context_restore(void)
{
    memcpy(&device_current, &device_prev, sizeof(device_context_t));
}

static void *
device_add_common(const device_t *dev, const device_t *cd, void *p, void *params, int inst)
{
    void *priv = NULL;
    int   c;

    for (c = 0; c < 256; c++) {
        if (!inst && (devices[c] == dev)) {
            device_log("DEVICE: device already exists!\n");
            return (NULL);
        }
        if (devices[c] == NULL)
            break;
    }
    if ((c >= DEVICE_MAX) || (c >= 256)) {
        fatal("DEVICE: too many devices\n");
        return NULL;
    }

    /* Do this so that a chained device_add will not identify the same ID
       its master device is already trying to assign. */
    devices[c] = (device_t *) dev;
    if (!strcmp(dev->name, "None") || !strcmp(dev->name, "Internal")) {
        uint32_t *p = NULL;
        *p = 5; /* Crash deliberately. */
    }

    if (p == NULL) {
        memcpy(&device_prev, &device_current, sizeof(device_context_t));
        device_set_context(&device_current, cd, inst);

        if (dev->init != NULL) {
            priv = (dev->flags & DEVICE_EXTPARAMS) ? dev->init_ext(dev, params) : dev->init(dev);
            if (priv == NULL) {
                if (dev->name)
                    device_log("DEVICE: device '%s' init failed\n", dev->name);
                else
                    device_log("DEVICE: device init failed\n");

                devices[c]     = NULL;
                device_priv[c] = NULL;

                return (NULL);
            }
        }

        if (dev->name)
            device_log("DEVICE: device '%s' init successful\n", dev->name);
        else
            device_log("DEVICE: device init successful\n");

        memcpy(&device_current, &device_prev, sizeof(device_context_t));
        device_priv[c] = priv;
    } else
        device_priv[c] = p;

    return priv;
}

const char *
device_get_internal_name(const device_t *dev)
{
    if (dev == NULL)
        return "";

    return dev->internal_name;
}

void *
device_add(const device_t *dev)
{
    return device_add_common(dev, dev, NULL, NULL, 0);
}

void *
device_add_parameters(const device_t *dev, void *params)
{
    return device_add_common(dev, dev, NULL, params, 0);
}

/* For devices that do not have an init function (internal video etc.) */
void
device_add_ex(const device_t *dev, void *priv)
{
    device_add_common(dev, dev, priv, NULL, 0);
}

void
device_add_ex_parameters(const device_t *dev, void *priv, void *params)
{
    device_add_common(dev, dev, priv, params, 0);
}

void *
device_add_inst(const device_t *dev, int inst)
{
    return device_add_common(dev, dev, NULL, NULL, inst);
}

void *
device_add_inst_parameters(const device_t *dev, int inst, void *params)
{
    return device_add_common(dev, dev, NULL, params, inst);
}

/* For devices that do not have an init function (internal video etc.) */
void
device_add_inst_ex(const device_t *dev, void *priv, int inst)
{
    device_add_common(dev, dev, priv, NULL, inst);
}

void
device_add_inst_ex_parameters(const device_t *dev, void *priv, int inst, void *params)
{
    device_add_common(dev, dev, priv, params, inst);
}

/* These eight are to add a device with another device's context - will be
   used to add machines' internal devices. */
void *
device_cadd(const device_t *dev, const device_t *cd)
{
    return device_add_common(dev, cd, NULL, NULL, 0);
}

void *
device_cadd_parameters(const device_t *dev, const device_t *cd, void *params)
{
    return device_add_common(dev, cd, NULL, params, 0);
}

/* For devices that do not have an init function (internal video etc.) */
void
device_cadd_ex(const device_t *dev, const device_t *cd, void *priv)
{
    device_add_common(dev, cd, priv, NULL, 0);
}

void
device_cadd_ex_parameters(const device_t *dev, const device_t *cd, void *priv, void *params)
{
    device_add_common(dev, cd, priv, params, 0);
}

void *
device_cadd_inst(const device_t *dev, const device_t *cd, int inst)
{
    return device_add_common(dev, cd, NULL, NULL, inst);
}

void *
device_cadd_inst_parameters(const device_t *dev, const device_t *cd, int inst, void *params)
{
    return device_add_common(dev, cd, NULL, params, inst);
}

/* For devices that do not have an init function (internal video etc.) */
void
device_cadd_inst_ex(const device_t *dev, const device_t *cd, void *priv, int inst)
{
    device_add_common(dev, cd, priv, NULL, inst);
}

void
device_cadd_inst_ex_parameters(const device_t *dev, const device_t *cd, void *priv, int inst, void *params)
{
    device_add_common(dev, cd, priv, params, inst);
}

void
device_close_all(void)
{
    for (int16_t c = (DEVICE_MAX - 1); c >= 0; c--) {
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
device_reset_all(uint32_t match_flags)
{
    for (uint16_t c = 0; c < DEVICE_MAX; c++) {
        if (devices[c] != NULL) {
            if ((devices[c]->reset != NULL) && (devices[c]->flags & match_flags))
                devices[c]->reset(device_priv[c]);
        }
    }
}

void *
device_get_priv(const device_t *dev)
{
    for (uint16_t c = 0; c < DEVICE_MAX; c++) {
        if (devices[c] != NULL) {
            if (devices[c] == dev)
                return (device_priv[c]);
        }
    }

    return (NULL);
}

int
device_available(const device_t *dev)
{
    const device_config_t      *config = NULL;
    const device_config_bios_t *bios   = NULL;
    int                         roms_present = 0;
    int                         i = 0;

    if (dev != NULL) {
        config = dev->config;
        if (config != NULL) {
            while (config->type != -1) {
                if (config->type == CONFIG_BIOS) {
                    bios = (const device_config_bios_t *) config->bios;

                    /* Go through the ROM's in the device configuration. */
                    while (bios->files_no != 0) {
                        i = 0;
                        for (int bf = 0; bf < bios->files_no; bf++)
                            i += !!rom_present(bios->files[bf]);
                        if (i == bios->files_no)
                            roms_present++;
                        bios++;
                    }

                    return (roms_present ? -1 : 0);
                }
                config++;
            }
        }

        /* No CONFIG_BIOS field present, use the classic available(). */
        if (dev->available != NULL)
            return (dev->available());
        else
            return 1;
    }

    /* A NULL device is never available. */
    return 0;
}

const char *
device_get_bios_file(const device_t *dev, const char *internal_name, int file_no)
{
    const device_config_t      *config = NULL;
    const device_config_bios_t *bios   = NULL;

    if (dev != NULL) {
        config = dev->config;
        if (config != NULL) {
            while (config->type != -1) {
                if (config->type == CONFIG_BIOS) {
                    bios = config->bios;

                    /* Go through the ROM's in the device configuration. */
                    while (bios->files_no != 0) {
                        if (!strcmp(internal_name, bios->internal_name)) {
                            if (file_no < bios->files_no)
                                return bios->files[file_no];
                            else
                                return NULL;
                        }
                        bios++;
                    }
                }
                config++;
            }
        }
    }

    /* A NULL device is never available. */
    return (NULL);
}

int
device_has_config(const device_t *dev)
{
    int                    c = 0;
    const device_config_t *config;

    if (dev == NULL)
        return 0;

    if (dev->config == NULL)
        return 0;

    config = dev->config;

    while (config->type != -1) {
        if (config->type != CONFIG_MAC)
            c++;
        config++;
    }

    return (c > 0) ? 1 : 0;
}

int
device_poll(const device_t *dev)
{
    for (uint16_t c = 0; c < DEVICE_MAX; c++) {
        if (devices[c] != NULL) {
            if (devices[c] == dev) {
                if (devices[c]->poll)
                    return (devices[c]->poll(device_priv[c]));
            }
        }
    }

    return 0;
}

void
device_get_name(const device_t *dev, int bus, char *name)
{
    const char *sbus = NULL;
    const char *fbus;
    char       *tname;
    char        pbus[8] = { 0 };

    if (dev == NULL)
        return;

    name[0] = 0x00;

    if (bus) {
        if (dev->flags & DEVICE_ISA)
            sbus = (dev->flags & DEVICE_AT) ? "ISA16" : "ISA";
        else if (dev->flags & DEVICE_CBUS)
            sbus = "C-BUS";
        else if (dev->flags & DEVICE_MCA)
            sbus = "MCA";
        else if (dev->flags & DEVICE_EISA)
            sbus = "EISA";
        else if (dev->flags & DEVICE_VLB)
            sbus = "VLB";
        else if (dev->flags & DEVICE_PCI)
            sbus = "PCI";
        else if (dev->flags & DEVICE_AGP)
            sbus = "AGP";
        else if (dev->flags & DEVICE_AC97)
            sbus = "AMR";
        else if (dev->flags & DEVICE_COM)
            sbus = "COM";
        else if (dev->flags & DEVICE_LPT)
            sbus = "LPT";

        if (sbus != NULL) {
            /* First concatenate [<Bus>] before the device's name. */
            strcat(name, "[");
            strcat(name, sbus);
            strcat(name, "] ");

            /* Then change string from ISA16 to ISA if applicable. */
            if (!strcmp(sbus, "ISA16"))
                sbus = "ISA";
            else if (!strcmp(sbus, "COM") || !strcmp(sbus, "LPT")) {
                sbus = NULL;
                strcat(name, dev->name);
                return;
            }

            /* Generate the bus string with parentheses. */
            strcat(pbus, "(");
            strcat(pbus, sbus);
            strcat(pbus, ")");

            /* Allocate the temporary device name string and set it to all zeroes. */
            tname = (char *) malloc(strlen(dev->name) + 1);
            memset(tname, 0x00, strlen(dev->name) + 1);

            /* First strip the bus string with parentheses. */
            fbus = strstr(dev->name, pbus);
            if (fbus == dev->name)
                strcat(tname, dev->name + strlen(pbus) + 1);
            else if (fbus == NULL)
                strcat(tname, dev->name);
            else {
                strncat(tname, dev->name, fbus - dev->name - 1);
                strcat(tname, fbus + strlen(pbus));
            }

            /* Then also strip the bus string with parentheses. */
            fbus = strstr(tname, sbus);
            if (fbus == tname)
                strcat(name, tname + strlen(sbus) + 1);
            /* Special case to not strip the "oPCI" from "Ensoniq AudioPCI" or
               the "-ISA" from "AMD PCnet-ISA". */
            else if ((fbus == NULL) || (*(fbus - 1) == 'o') || (*(fbus - 1) == '-'))
                strcat(name, tname);
            else {
                strncat(name, tname, fbus - tname - 1);
                strcat(name, fbus + strlen(sbus));
            }

            /* Free the temporary device name string. */
            free(tname);
            tname = NULL;
        } else
            strcat(name, dev->name);
    } else
        strcat(name, dev->name);
}

void
device_speed_changed(void)
{
    for (uint16_t c = 0; c < DEVICE_MAX; c++) {
        if (devices[c] != NULL) {
            device_log("DEVICE: device '%s' speed changed\n", devices[c]->name);

            if (devices[c]->speed_changed != NULL)
                devices[c]->speed_changed(device_priv[c]);
        }
    }

    sound_speed_changed();
}

void
device_force_redraw(void)
{
    for (uint16_t c = 0; c < DEVICE_MAX; c++) {
        if (devices[c] != NULL) {
            if (devices[c]->force_redraw != NULL)
                devices[c]->force_redraw(device_priv[c]);
        }
    }
}

const int
device_get_instance(void)
{
    return device_current.instance;
}

const char *
device_get_config_string(const char *s)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
        if (!strcmp(s, c->name))
            return (config_get_string((char *) device_current.name, (char *) s, (char *) c->default_string));

        c++;
    }

    return (NULL);
}

int
device_get_config_int(const char *s)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
        if (!strcmp(s, c->name))
            return (config_get_int((char *) device_current.name, (char *) s, c->default_int));

        c++;
    }

    return 0;
}

int
device_get_config_int_ex(const char *s, int def)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
        if (!strcmp(s, c->name))
            return (config_get_int((char *) device_current.name, (char *) s, def));

        c++;
    }

    return def;
}

int
device_get_config_hex16(const char *s)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
        if (!strcmp(s, c->name))
            return (config_get_hex16((char *) device_current.name, (char *) s, c->default_int));

        c++;
    }

    return 0;
}

int
device_get_config_hex20(const char *s)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
        if (!strcmp(s, c->name))
            return (config_get_hex20((char *) device_current.name, (char *) s, c->default_int));

        c++;
    }

    return 0;
}

int
device_get_config_mac(const char *s, int def)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
        if (!strcmp(s, c->name))
            return (config_get_mac((char *) device_current.name, (char *) s, def));

        c++;
    }

    return def;
}

void
device_set_config_int(const char *s, int val)
{
    const device_config_t *c = device_current.dev->config;

    while (c && c->type != -1) {
        if (!strcmp(s, c->name)) {
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
        if (!strcmp(s, c->name)) {
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
        if (!strcmp(s, c->name)) {
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
        if (!strcmp(s, c->name)) {
            config_set_mac((char *) device_current.name, (char *) s, val);
            break;
        }

        c++;
    }
}

int
device_is_valid(const device_t *device, int m)
{
    if (device == NULL)
        return 1;

    if ((device->flags & DEVICE_PCJR) && !machine_has_bus(m, MACHINE_BUS_PCJR))
        return 0;

    if ((device->flags & DEVICE_XTKBC) && machine_has_bus(m, MACHINE_BUS_ISA16) && !machine_has_bus(m, MACHINE_BUS_DM_KBC))
        return 0;

    if ((device->flags & DEVICE_AT) && !machine_has_bus(m, MACHINE_BUS_ISA16))
        return 0;

    if ((device->flags & DEVICE_ATKBC) && !machine_has_bus(m, MACHINE_BUS_ISA16) && !machine_has_bus(m, MACHINE_BUS_DM_KBC))
        return 0;

    if ((device->flags & DEVICE_ISA) && !machine_has_bus(m, MACHINE_BUS_ISA))
        return 0;

    if ((device->flags & DEVICE_CBUS) && !machine_has_bus(m, MACHINE_BUS_CBUS))
        return 0;

    if ((device->flags & DEVICE_PCMCIA) && !machine_has_bus(m, MACHINE_BUS_PCMCIA))
        return 0;

    if ((device->flags & DEVICE_MCA) && !machine_has_bus(m, MACHINE_BUS_MCA))
        return 0;

    if ((device->flags & DEVICE_HIL) && !machine_has_bus(m, MACHINE_BUS_HIL))
        return 0;

    if ((device->flags & DEVICE_EISA) && !machine_has_bus(m, MACHINE_BUS_EISA))
        return 0;

    if ((device->flags & DEVICE_OLB) && !machine_has_bus(m, MACHINE_BUS_OLB))
        return 0;

    if ((device->flags & DEVICE_VLB) && !machine_has_bus(m, MACHINE_BUS_VLB))
        return 0;

    if ((device->flags & DEVICE_PCI) && !machine_has_bus(m, MACHINE_BUS_PCI))
        return 0;

    if ((device->flags & DEVICE_CARDBUS) && !machine_has_bus(m, MACHINE_BUS_CARDBUS))
        return 0;

    if ((device->flags & DEVICE_USB) && !machine_has_bus(m, MACHINE_BUS_USB))
        return 0;

    if ((device->flags & DEVICE_AGP) && !machine_has_bus(m, MACHINE_BUS_AGP))
        return 0;

    if ((device->flags & DEVICE_PS2) && !machine_has_bus(m, MACHINE_BUS_PS2_PORTS))
        return 0;

    if ((device->flags & DEVICE_AC97) && !machine_has_bus(m, MACHINE_BUS_AC97))
        return 0;

    return 1;
}

int
machine_get_config_int(char *s)
{
    const device_t        *d = machine_get_device(machine);
    const device_config_t *c;

    if (d == NULL)
        return 0;

    c = d->config;
    while (c && c->type != -1) {
        if (!strcmp(s, c->name))
            return (config_get_int((char *) d->name, s, c->default_int));

        c++;
    }

    return 0;
}

char *
machine_get_config_string(char *s)
{
    const device_t        *d = machine_get_device(machine);
    const device_config_t *c;

    if (d == NULL)
        return 0;

    c = d->config;
    while (c && c->type != -1) {
        if (!strcmp(s, c->name))
            return (config_get_string((char *) d->name, s, (char *) c->default_string));

        c++;
    }

    return NULL;
}
