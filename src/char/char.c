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
 *          Copyright 2025 RichardG.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/char.h>

static const struct {
    const char_device_t *chardev;
} char_devices[] = {
    // clang-format off
    { &hostser_device },
    { 0 }
    // clang-format on
};

char_port_t *char_port = NULL;

const char_device_t *
char_get(const char *internal_name)
{
    for (int i = 0; char_devices[i].chardev; i++) {
        if (!strcmp(internal_name, char_devices[i].chardev->device.internal_name))
            return char_devices[i].chardev;
    }

    return NULL;
}

void
char_init(const char_device_t *chardev, int config_instance)
{
    // TODO
}
