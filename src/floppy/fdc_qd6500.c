/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Addition of the 86box AT FDC as the multifunction QDI Vision QD6500 VLB floppy disk controller.
 *
 * Authors: mw308
 *
 *          Copyright 2026 mw308
 *
 * The QD6500A multifunction card provides a conventional AT-compatible
 * floppy interface through its UMC multifunction logic.  The currently
 * established hardware behaviour is represented by the common PC/AT FDC
 * core (primary ports 3F0h-3F7h, IRQ 6, DMA 2).
 *
 * JP16-specific decode behaviour is not modelled. Passive captures from
 * a QD6500A CARD V3.011/1993 were identical with JP16 open and closed,
 * so more investigation is needed.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/fdc.h>

static void *
fdc_qd6500_init(const device_t *info)
{
    (void) info;

    /*
     * Selecting this card as the floppy controller represents the real
     * card's FDC being enabled, so the real enable/disable jumper option
     * is not required.
     *
     * This adds the standard AT FDC already in 86box.
     */
    device_add(&fdc_at_device);

    return NULL;
}

const device_t fdc_qd6500_device = {
    .name          = "QDI QD6500A",
    .internal_name = "fdc_qd6500a",
    .flags         = DEVICE_VLB,
    .local         = 0,
    .init          = fdc_qd6500_init,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
