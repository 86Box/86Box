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
    const char_device_t *chardev;
} char_devices[] = {
    // clang-format off
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

const char_device_t *
char_get(const char *internal_name)
{
    for (int i = 0; char_devices[i].chardev; i++) {
        if (!strcmp(internal_name, char_devices[i].chardev->device.internal_name))
            return char_devices[i].chardev;
    }

    char_log("Char: Could not find device \"%s\"\n", internal_name);
    return NULL;
}

void
char_init(char_port_t *port, const char *init_string, int instance)
{
    if (!init_string)
        return;

    char *buf = strdup(init_string);
    char *strtok_save;
    const char_device_t *chardev = char_get(strtok_r(buf, ":", &strtok_save));
    if (!chardev)
        goto end;
    char_log("Char: Initializing device \"%s\"\n", chardev->device.internal_name);

    port->config = ini_new();
    const char *key;
    int i = 0;
    while ((key = strtok_r(NULL, ":", &strtok_save))) {
        const char *val = strchr(key, '=');
        if (val) {
            *((char *) val++) = '\0';
        } else {
            val = key;
            key = (chardev->device.config && (chardev->device.config[i].type != CONFIG_END)) ? chardev->device.config[i].name : "";
        }
        if (chardev->device.config && (chardev->device.config[i].type != CONFIG_END))
            i++;
        char_log("Char: Setting option %s = %s\n", key, val);
        ini_set_string(port->config, "", key, val);
    }

    memcpy(&port->chardev, chardev, sizeof(char_device_t));
    port->local = port->chardev.device.local;
    port->chardev.device.local = 0;

    port->priv = device_add_inst_params(&port->chardev.device, instance, port);

end:
    free(buf);
}
