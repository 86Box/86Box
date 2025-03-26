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
 *          Copyright 2021-2025 Jasmine Iwanek.
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
#include <86box/plat.h>
#include <86box/rom.h>
#include <86box/sound.h>
#include <86box/ui.h>

#define DEVICE_MAX 256 /* max # of devices */

static device_t        *devices[DEVICE_MAX];
static void            *device_priv[DEVICE_MAX];
static device_context_t device_current;
static device_context_t device_prev;
static void            *device_common_priv;

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
device_set_context(device_context_t *ctx, const device_t *dev, int inst)
{
    memset(ctx, 0, sizeof(device_context_t));
    ctx->dev      = dev;
    ctx->instance = inst;
    if (inst) {
        sprintf(ctx->name, "%s #%i", dev->name, inst);

        /* If a numbered section is not present, but a non-numbered of the same name
           is, rename the non-numbered section to numbered. */
        const void *sec        = config_find_section(ctx->name);
        void *      single_sec = config_find_section((char *) dev->name);
        if ((sec == NULL) && (single_sec != NULL))
            config_rename_section(single_sec, ctx->name);
    } else if (!strcmp(dev->name, "PS/2 Mouse")) {
        sprintf(ctx->name, "%s", dev->name);

        /* Migrate the old "Standard PS/2 Mouse" section */
        const void *sec        = config_find_section(ctx->name);
        void *      old_sec    = config_find_section("Standard PS/2 Mouse");
        if ((sec == NULL) && (old_sec != NULL))
            config_rename_section(old_sec, ctx->name);
    } else if (!strcmp(dev->name, "Microsoft RAMCard")) {
        sprintf(ctx->name, "%s", dev->name);

        /* Migrate the old "Microsoft RAMCard for IBM PC" section */
        const void *sec        = config_find_section(ctx->name);
        void *      old_sec    = config_find_section("Microsoft RAMCard for IBM PC");
        if ((sec == NULL) && (old_sec != NULL))
            config_rename_section(old_sec, ctx->name);
    } else
        sprintf(ctx->name, "%s", dev->name);
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
device_add_common(const device_t *dev, void *p, void *params, int inst)
{
    device_t *init_dev = NULL;
    void     *priv     = NULL;
    int16_t   c;

    /*
       IMPORTANT: This is needed to gracefully handle machine
                  device addition if the relevant device is NULL.
     */
    if (dev == NULL)
        return NULL;

    if (!device_available(dev)) {
        wchar_t temp[512] = { 0 };
        swprintf(temp, sizeof_w(temp),
                 plat_get_string(STRING_HW_NOT_AVAILABLE_DEVICE),
                 dev->name);
        ui_msgbox_header(MBX_INFO,
                         plat_get_string(STRING_HW_NOT_AVAILABLE_TITLE),
                         temp);
        return ((void *) dev->name);
    }

    if (params != NULL) {
        init_dev = calloc(1, sizeof(device_t));
        memcpy(init_dev, dev, sizeof(device_t));
        init_dev->local |= (uintptr_t) params;
    } else
        init_dev = (device_t *) dev;

    for (c = 0; c < DEVICE_MAX; c++) {
        if (!inst && (devices[c] == dev)) {
            device_log("DEVICE: device already exists!\n");
            return (NULL);
        }
        if (devices[c] == NULL)
            break;
    }
    if (c >= DEVICE_MAX) {
        fatal("Attempting to initialize more than the maximum "
              "limit of %i devices\n", DEVICE_MAX);
        return NULL;
    }

    /* Do this so that a chained device_add will not identify the same ID
       its master device is already trying to assign. */
    devices[c] = (device_t *) dev;
    if (!strcmp(dev->name, "None") || !strcmp(dev->name, "Internal"))
        fatal("Attempting to add dummy device of type: %s\n", dev->name);

    if (p == NULL) {
        memcpy(&device_prev, &device_current, sizeof(device_context_t));
        device_set_context(&device_current, dev, inst);

        if (dev->init != NULL) {
            /* Give it our temporary device in case we have dynamically changed info->local. */
            priv = dev->init(init_dev);

            if (priv == NULL) {
#ifdef ENABLE_DEVICE_LOG
                if (dev->name)
                    device_log("DEVICE: device '%s' init failed\n", dev->name);
                else
                    device_log("DEVICE: device init failed\n");
#endif

                devices[c]     = NULL;
                device_priv[c] = NULL;

                if ((init_dev != NULL) && (init_dev != (device_t *) dev))
                    free(init_dev);

                return (NULL);
            }
        }

#ifdef ENABLE_DEVICE_LOG
        if (dev->name)
            device_log("DEVICE: device '%s' init successful\n", dev->name);
        else
            device_log("DEVICE: device init successful\n");
#endif

        memcpy(&device_current, &device_prev, sizeof(device_context_t));
        device_priv[c] = priv;
    } else
        device_priv[c] = p;

    if (init_dev != dev)
        free(init_dev);

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
    return device_add_common(dev, NULL, NULL, 0);
}

void *
device_add_linked(const device_t *dev, void *priv)
{
    void *ret;

    device_common_priv = priv;
    ret = device_add_common(dev, NULL, NULL, 0);
    device_common_priv = NULL;
    return ret;
}

void *
device_add_params(const device_t *dev, void *params)
{
    return device_add_common(dev, NULL, params, 0);
}

/* For devices that do not have an init function (internal video etc.) */
void
device_add_ex(const device_t *dev, void *priv)
{
    device_add_common(dev, priv, NULL, 0);
}

void
device_add_ex_params(const device_t *dev, void *priv, void *params)
{
    device_add_common(dev, priv, params, 0);
}

void *
device_add_inst(const device_t *dev, int inst)
{
    return device_add_common(dev, NULL, NULL, inst);
}

void *
device_add_inst_params(const device_t *dev, int inst, void *params)
{
    return device_add_common(dev, NULL, params, inst);
}

/* For devices that do not have an init function (internal video etc.) */
void
device_add_inst_ex(const device_t *dev, void *priv, int inst)
{
    device_add_common(dev, priv, NULL, inst);
}

void
device_add_inst_ex_params(const device_t *dev, void *priv, int inst, void *params)
{
    device_add_common(dev, priv, params, inst);
}

void *
device_get_common_priv(void)
{
    return device_common_priv;
}

void
device_close_all(void)
{
    for (int16_t c = (DEVICE_MAX - 1); c >= 0; c--) {
        if (devices[c] != NULL) {
#ifdef ENABLE_DEVICE_LOG
            if (devices[c]->name)
                device_log("Closing device: \"%s\"...\n", devices[c]->name);
#endif
            if (devices[c]->close != NULL)
                devices[c]->close(device_priv[c]);
            devices[c]     = NULL;
            device_priv[c] = NULL;
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

#ifdef UNCOMMENT_LATER
    /* TODO: Actually convert the LPT devices to device_t's. */
    if ((match_flags == DEVICE_ALL) || (match_flags == DEVICE_PCI))
        lpt_reset();
#endif
}

void *
device_find_first_priv(uint32_t match_flags)
{
    void *ret = NULL;

    for (uint16_t c = 0; c < DEVICE_MAX; c++) {
        if (devices[c] != NULL) {
            if ((device_priv[c] != NULL) && (devices[c]->flags & match_flags)) {
                ret = device_priv[c];
                break;
            }
        }
    }

    return ret;
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
    if (dev != NULL) {
        const device_config_t *config = dev->config;
        if (config != NULL) {
            while (config->type != CONFIG_END) {
                if (config->type == CONFIG_BIOS) {
                    int roms_present = 0;
                    const device_config_bios_t *bios = (const device_config_bios_t *) config->bios;

                    /* Go through the ROM's in the device configuration. */
                    while ((bios != NULL) &&
                           (bios->name != NULL) &&
                           (bios->internal_name != NULL) &&
                           (bios->files_no != 0)) {
                        int i = 0;
                        for (uint8_t bf = 0; bf < bios->files_no; bf++)
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

uint8_t
device_get_bios_type(const device_t *dev, const char *internal_name)
{
    if (dev != NULL) {
        const device_config_t *config = dev->config;
        if (config != NULL) {
            while (config->type != CONFIG_END) {
                if (config->type == CONFIG_BIOS) {
                    const device_config_bios_t *bios = (const device_config_bios_t *) config->bios;
                    while ((bios != NULL) &&
                           (bios->name != NULL) &&
                           (bios->internal_name != NULL) &&
                           (bios->files_no != 0)) {
                        if (!strcmp(internal_name, bios->internal_name))
                            return bios->bios_type;
                        bios++;
                    }
                }
                config++;
            }
        }
    }

    return 0;
}

uint8_t
device_get_bios_num_files(const device_t *dev, const char *internal_name)
{
    if (dev != NULL) {
        const device_config_t *config = dev->config;
        if (config != NULL) {
            while (config->type != CONFIG_END) {
                if (config->type == CONFIG_BIOS) {
                    const device_config_bios_t *bios = (const device_config_bios_t *) config->bios;
                    while ((bios != NULL) &&
                           (bios->name != NULL) &&
                           (bios->internal_name != NULL) &&
                           (bios->files_no != 0)) {
                        if (!strcmp(internal_name, bios->internal_name))
                            return bios->files_no;
                        bios++;
                    }
                }
                config++;
            }
        }
    }

    return 0;
}

uint32_t
device_get_bios_local(const device_t *dev, const char *internal_name)
{
    if (dev != NULL) {
        const device_config_t *config = dev->config;
        if (config != NULL) {
            while (config->type != CONFIG_END) {
                if (config->type == CONFIG_BIOS) {
                    const device_config_bios_t *bios = (const device_config_bios_t *) config->bios;
                    while ((bios != NULL) &&
                           (bios->name != NULL) &&
                           (bios->internal_name != NULL) &&
                           (bios->files_no != 0)) {
                        printf("Internal name was: %s", internal_name);
                        if (!strcmp(internal_name, bios->internal_name))
                            return bios->local;
                        bios++;
                    }
                }
                config++;
            }
        }
    }

    return 0;
}

uint32_t
device_get_bios_file_size(const device_t *dev, const char *internal_name)
{
    if (dev != NULL) {
        const device_config_t *config = dev->config;
        if (config != NULL) {
            while (config->type != CONFIG_END) {
                if (config->type == CONFIG_BIOS) {
                    const device_config_bios_t *bios = (const device_config_bios_t *) config->bios;

                    /* Go through the ROM's in the device configuration. */
                    while ((bios != NULL) &&
                           (bios->name != NULL) &&
                           (bios->internal_name != NULL) &&
                           (bios->files_no != 0)) {
                        if (!strcmp(internal_name, bios->internal_name))
                            return bios->size;
                        bios++;
                    }
                }
                config++;
            }
        }
    }

    return 0;
}

const char *
device_get_bios_file(const device_t *dev, const char *internal_name, int file_no)
{
    if (dev != NULL) {
        const device_config_t *config = dev->config;
        if (config != NULL) {
            while (config->type != CONFIG_END) {
                if (config->type == CONFIG_BIOS) {
                    const device_config_bios_t *bios = (const device_config_bios_t *) config->bios;

                    /* Go through the ROM's in the device configuration. */
                    while ((bios != NULL) &&
                           (bios->name != NULL) &&
                           (bios->internal_name != NULL) &&
                           (bios->files_no != 0)) {
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

    while (config->type != CONFIG_END) {
        c++;
        config++;
    }

    return (c > 0) ? 1 : 0;
}

void
device_get_name(const device_t *dev, int bus, char *name)
{
    const char *sbus = NULL;
    const char *fbus;
    char       *tname;
    char        pbus[16] = { 0 };

    if (dev == NULL)
        return;

    name[0] = 0x00;

    if (bus) {
        if ((dev->flags & (DEVICE_SIDECAR | DEVICE_ISA)) ==
            (DEVICE_SIDECAR | DEVICE_ISA))
            sbus = "ISA/Sidecar";
        else if (dev->flags & DEVICE_SIDECAR)
            sbus = "Sidecar";
        else if (dev->flags & DEVICE_XT_KBC)
            sbus = "XT KBC";
        else if (dev->flags & DEVICE_ISA16)
            sbus = "ISA16";
        else if (dev->flags & DEVICE_AT_KBC)
            sbus = "AT KBC";
        else if (dev->flags & DEVICE_PS2_KBC)
            sbus = "PS/2 KBC";
        else if (dev->flags & DEVICE_ISA)
            sbus = "ISA";
        else if (dev->flags & DEVICE_CBUS)
            sbus = "C-BUS";
        else if (dev->flags & DEVICE_PCMCIA)
            sbus = "PCMCIA";
        else if (dev->flags & DEVICE_MCA)
            sbus = "MCA";
        else if (dev->flags & DEVICE_MCA32)
            sbus = "MCA32";
        else if (dev->flags & DEVICE_HIL)
            sbus = "HP HIL";
        else if (dev->flags & DEVICE_EISA)
            sbus = "EISA";
        else if (dev->flags & DEVICE_AT32)
            sbus = "AT/32";
        else if (dev->flags & DEVICE_OLB)
            sbus = "OLB";
        else if (dev->flags & DEVICE_VLB)
            sbus = "VLB";
        else if (dev->flags & DEVICE_PCI)
            sbus = "PCI";
        else if (dev->flags & DEVICE_CARDBUS)
            sbus = "CardBus";
        else if (dev->flags & DEVICE_USB)
            sbus = "USB";
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
            tname = (char *) calloc(1, strlen(dev->name) + 1);

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
            else if ((fbus == NULL) || (*(fbus - 1) == 'o') || (*(fbus - 1) == '-') || (*(fbus - 2) == 'r'))
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

int
device_get_instance(void)
{
    return device_current.instance;
}

const char *
device_get_config_string(const char *str)
{
    const char *ret = "";

    if (device_current.dev != NULL) {
        const device_config_t *cfg = device_current.dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name)) {
                const char *s = (config_get_string((char *) device_current.name,
                                 (char *) str, (char *) cfg->default_string));
                ret = (s == NULL) ? "" : s;
                break;
            }

            cfg++;
        }
    }

    return ret;
}

int
device_get_config_int(const char *str)
{
    if (device_current.dev != NULL) {
        const device_config_t *cfg = device_current.dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name))
                return (config_get_int((char *) device_current.name, (char *) str, cfg->default_int));

            cfg++;
        }
    }

    return 0;
}

int
device_get_config_int_ex(const char *str, int def)
{
    if (device_current.dev != NULL) {
        const device_config_t *cfg = device_current.dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name))
                return (config_get_int((char *) device_current.name, (char *) str, def));

            cfg++;
        }
    }

    return def;
}

int
device_get_config_hex16(const char *str)
{
    if (device_current.dev != NULL) {
        const device_config_t *cfg = device_current.dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name))
                return (config_get_hex16((char *) device_current.name, (char *) str, cfg->default_int));

            cfg++;
        }
    }

    return 0;
}

int
device_get_config_hex20(const char *str)
{
    if (device_current.dev != NULL) {
        const device_config_t *cfg = device_current.dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name))
                return (config_get_hex20((char *) device_current.name, (char *) str, cfg->default_int));

            cfg++;
        }
    }

    return 0;
}

int
device_get_config_mac(const char *str, int def)
{
    if (device_current.dev != NULL) {
        const device_config_t *cfg = device_current.dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name))
                return (config_get_mac((char *) device_current.name, (char *) str, def));

            cfg++;
        }
    }

    return def;
}

void
device_set_config_int(const char *str, int val)
{
    if (device_current.dev != NULL) {
        const device_config_t *cfg = device_current.dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name)) {
                config_set_int((char *) device_current.name, (char *) str, val);
                break;
            }

            cfg++;
        }
    }
}

void
device_set_config_hex16(const char *str, int val)
{
    if (device_current.dev != NULL) {
        const device_config_t *cfg = device_current.dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name)) {
                config_set_hex16((char *) device_current.name, (char *) str, val);
                break;
            }

            cfg++;
        }
    }
}

void
device_set_config_hex20(const char *str, int val)
{
    if (device_current.dev != NULL) {
        const device_config_t *cfg = device_current.dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name)) {
                config_set_hex20((char *) device_current.name, (char *) str, val);
                break;
            }

        cfg++;
        }
    }
}

void
device_set_config_mac(const char *str, int val)
{
    if (device_current.dev != NULL) {
        const device_config_t *cfg = device_current.dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name)) {
                config_set_mac((char *) device_current.name, (char *) str, val);
                break;
            }

            cfg++;
        }
    }
}

int
device_is_valid(const device_t *device, int mch)
{
    int ret = 1;

    if ((device != NULL) && ((device->flags & DEVICE_BUS) != 0))
        ret = machine_has_bus(mch, device->flags & DEVICE_BUS);

    return ret;
}

int
machine_get_config_int(char *str)
{
    const device_t *dev = machine_get_device(machine);

    if (dev != NULL) {
        const device_config_t *cfg = dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name))
                return (config_get_int((char *) dev->name, str, cfg->default_int));

            cfg++;
        }
    }
    return 0;
}

const char *
machine_get_config_string(char *str)
{
    const device_t *dev = machine_get_device(machine);
    const char     *ret = "";

    if (dev != NULL) {
        const device_config_t *cfg = dev->config;

        while ((cfg != NULL) && (cfg->type != CONFIG_END)) {
            if (!strcmp(str, cfg->name)) {
                const char *s = config_get_string((char *) dev->name, str,
                                                  (char *) cfg->default_string);
                ret = (s == NULL) ? "" : s;
                break;
            }

            cfg++;
        }
    }

    return ret;
}

const device_t *
device_context_get_device(void)
{
    return device_current.dev;
}

const device_t device_none = {
    .name          = "None",
    .internal_name = "none",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t device_internal = {
    .name          = "Internal",
    .internal_name = "internal",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
