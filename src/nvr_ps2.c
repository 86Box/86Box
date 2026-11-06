/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handling of the PS/2 series CMOS devices.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2017-2018 Fred N. van Kempen.
 *          Copyright 2008-2018 Sarah Walker.
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
#include "cpu.h"

typedef struct ps2_nvr_t {
    int addr;
    int loaded;
    int can_save;

    uint8_t *ram;
    int      size;

    char *fn;
} ps2_nvr_t;

static uint8_t
ps2_nvr_read(uint16_t port, void *priv)
{
    const ps2_nvr_t *nvr = (ps2_nvr_t *) priv;
    uint8_t          ret = 0xff;

    switch (port) {
        case 0x74:
            ret = nvr->addr & 0xff;
            break;

        case 0x75:
            ret = nvr->addr >> 8;
            break;

        case 0x76:
            ret = nvr->ram[nvr->addr & (nvr->size - 1)];
            break;

        default:
            break;
    }

    return ret;
}

static void
ps2_nvr_write(uint16_t port, uint8_t val, void *priv)
{
    ps2_nvr_t *nvr = (ps2_nvr_t *) priv;

    switch (port) {
        case 0x74:
            nvr->addr = (nvr->addr & 0x1f00) | val;
            break;

        case 0x75:
            nvr->addr = (nvr->addr & 0xff) | ((val & 0x1f) << 8);
            break;

        case 0x76:
            nvr->ram[nvr->addr & (nvr->size - 1)] = val;
            break;

        default:
            break;
    }
}

static void *
ps2_nvr_init(const device_t *info)
{
    ps2_nvr_t *nvr;
    FILE      *fp = NULL;
    int        c;

    nvr = (ps2_nvr_t *) calloc(1, sizeof(ps2_nvr_t));

    if (info->local)
        nvr->size = 2048;
    else
        nvr->size = 8192;

    /* Set up the NVR file's name. */
    c       = strlen(machine_get_nvr_name()) + 9;
    nvr->fn = (char *) calloc(1, c + 1);
    sprintf(nvr->fn, "%s_sec.nvr", machine_get_nvr_name());

    io_sethandler(0x0074, 3,
                  ps2_nvr_read, NULL, NULL, ps2_nvr_write, NULL, NULL, nvr);

    fp = nvr_fopen(nvr->fn, "rb");

    nvr->ram = (uint8_t *) calloc(1, nvr->size);
    memset(nvr->ram, 0xff, nvr->size);
    nvr->can_save = 1;
    if (fp != NULL) {
        nvr->loaded = 1;
        /* A file shorter than the device means the NVR used to be smaller;
           keep the 0xFF-filled tail rather than refusing to start. */
        if (cpu_s != NULL)
            (void) fread(nvr->ram, 1, nvr->size, fp);
        else
            /* Nothing was loaded, so RAM does not describe this file's content. */
            nvr->can_save = 0;
        fclose(fp);
    }

    return nvr;
}

int
ps2_nvr_is_new(void *priv)
{
    const ps2_nvr_t *nvr = (const ps2_nvr_t *) priv;

    return !nvr->loaded;
}

void
ps2_nvr_set_byte(void *priv, uint16_t addr, uint8_t val)
{
    ps2_nvr_t *nvr = (ps2_nvr_t *) priv;

    if (addr < nvr->size)
        nvr->ram[addr] = val;
}

static void
ps2_nvr_close(void *priv)
{
    ps2_nvr_t *nvr = (ps2_nvr_t *) priv;
    FILE      *fp  = NULL;

    /* Never write back an image that was not loaded from this file. */
    if (nvr->can_save) {
        /* Truncating keeps the file's size equal to the device's. */
        fp = nvr_fopen(nvr->fn, "wb");

        if (fp != NULL) {
            (void) fwrite(nvr->ram, nvr->size, 1, fp);
            fclose(fp);
        }
    }

    if (nvr->ram != NULL)
        free(nvr->ram);

    free(nvr);
}

const device_t ps2_nvr_2kb_device = {
    .name          = "IBM PS/2 2KB Secondary NVRAM",
    .internal_name = "ps2_nvr_2kb",
    .flags         = 0,
    .local         = 1,
    .init          = ps2_nvr_init,
    .close         = ps2_nvr_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ps2_nvr_8kb_device = {
    .name          = "IBM PS/2 8KB Secondary NVRAM",
    .internal_name = "ps2_nvr_8kb",
    .flags         = 0,
    .local         = 0,
    .init          = ps2_nvr_init,
    .close         = ps2_nvr_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
