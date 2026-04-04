/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Main module for character devices.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2025-2026 RichardG.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/ini.h>
#include <86box/char.h>

static const struct {
    const device_t *dev;
} char_devices[] = {
    // clang-format off
    { &loopback_com_device },
    { &loopback_lpt_device },
    { &hostfile_device },
    { &hostser_device },
    { &stdio_device },
    { 0 }
    // clang-format on
};

#define ENABLE_CHAR_LOG 1
#ifdef ENABLE_CHAR_LOG
int char_do_log = ENABLE_CHAR_LOG;

static void
char_log(const char *fmt, ...)
{
    va_list ap;

    if (char_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define char_log(fmt, ...)
#endif

static char_port_t *active_port = NULL;

static const device_t *
char_get(const char *internal_name)
{
    for (int i = 0; char_devices[i].dev; i++) {
        if (!strcmp(internal_name, char_devices[i].dev->internal_name))
            return char_devices[i].dev;
    }

    char_log("Char: Could not find device \"%s\"\n", internal_name);
    return NULL;
}

void
char_init(char_port_t *port, const char *init_string, int instance)
{
    if (!init_string)
        return;

    /* Find the device. */
    char *buf = strdup(init_string);
    char *strtok_save;
    const device_t *dev = char_get(strtok_r(buf, ":", &strtok_save));
    if (!dev)
        goto end;
    char_log("Char: Initializing device \"%s\"\n", dev->internal_name);

    /* Parse device configuration. */
    port->config = ini_new();
    const char *key;
    int i = 0;
    while ((key = strtok_r(NULL, ":", &strtok_save))) {
        const char *val = strchr(key, '=');
        if (val) {
            *((char *) val++) = '\0';
        } else {
            val = key;
            key = (dev->config && (dev->config[i].type != CONFIG_END)) ? dev->config[i].name : "";
        }
        if (dev->config && (dev->config[i].type != CONFIG_END))
            i++;
        char_log("Char: Setting option %s = %s\n", key, val);
        ini_set_string(port->config, "", key, val);
    }

    /* Add the device. */
    active_port = port;
    port->chardev.priv = device_add_inst(dev, instance);
    active_port = NULL;

end:
    free(buf);
}

void *
char_attach(uint32_t flags,
            ssize_t  (*read)(uint8_t *buf, ssize_t len, void *priv),
            ssize_t  (*write)(uint8_t *buf, ssize_t len, void *priv),
            uint32_t (*status)(void *priv),
            void     (*control)(uint32_t flags, void *priv),
            void     (*port_config)(void *priv),
            void     *priv)
{
    if (!active_port)
        fatal("Char: No active port\n");

    char_device_t *chardev = &active_port->chardev;
    chardev->flags         = flags;
    chardev->read          = read;
    chardev->write         = write;
    chardev->status        = status;
    chardev->control       = control;
    chardev->port_config   = port_config;
    chardev->priv          = priv;

    return active_port;
}
