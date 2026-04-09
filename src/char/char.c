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

static const device_t char_none_device = {
    .name          = "None",
    .internal_name = "none",
    .flags         = DEVICE_COM | DEVICE_LPT,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static const struct {
    const device_t *device;
} char_devices[] = {
    // clang-format off
    { &char_none_device },
    { &char_serial_passthrough_com_device },
    { &char_pipe_com_device },
    { &char_file_com_device },
    { &char_stdio_com_device },
    { &char_loopback_com_device },
    { &char_loopback_lpt_device },
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

const device_t *
char_get_device(const int id)
{
    return char_devices[id].device;
}

const int
char_get_from_internal_name(const char *internal_name, int bus)
{
    for (int i = 0; char_devices[i].device; i++) {
        if ((char_devices[i].device->flags & bus) && !strcmp(internal_name, char_devices[i].device->internal_name))
            return i;
    }

    char_log("Char: Could not find device \"%s\"\n", internal_name);
    return 0;
}

void *
char_init(char_port_t *port, const device_t *device, int instance)
{
    if (!port || !device)
        return NULL;

    active_port = port;
    void *priv = device_add_inst(device, instance);
    active_port = NULL;
    return priv;
}

void *
char_attach(uint32_t flags,
            size_t   (*read)(uint8_t *buf, size_t len, void *priv),
            size_t   (*write)(uint8_t *buf, size_t len, void *priv),
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
