/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the NEC Mate NX MA30D/23D Unknown Readout.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020-2023 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/plat_unused.h>

static uint8_t
nec_mate_unk_read(UNUSED(uint16_t addr), UNUSED(void *priv))
{
    /* Expected by this NEC machine.

       It writes something on ports 3D6C, 3D6D, and 3D6E, then expects to read
       2Ah from port 3D6D. Then it repeats this with ports 6A, 6B, and 6C.
     */
    return 0x2a;
}

static void
nec_mate_unk_close(void *priv)
{
    uint8_t *dev = (uint8_t *) priv;

    free(dev);
}

static void *
nec_mate_unk_init(UNUSED(const device_t *info))
{
    /* We have to return something non-NULL. */
    uint8_t *dev = (uint8_t *) calloc(1, sizeof(uint8_t));

    io_sethandler(0x006b, 0x0001, nec_mate_unk_read, NULL, NULL, NULL, NULL, NULL, NULL);
    io_sethandler(0x3d6d, 0x0001, nec_mate_unk_read, NULL, NULL, NULL, NULL, NULL, NULL);

    return dev;
}

const device_t nec_mate_unk_device = {
    .name          = "NEC Mate NX MA30D/23D Unknown Readout",
    .internal_name = "nec_mate_unk",
    .flags         = 0,
    .local         = 0,
    .init          = nec_mate_unk_init,
    .close         = nec_mate_unk_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
