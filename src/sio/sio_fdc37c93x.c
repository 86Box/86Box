/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SMC FDC37C93x Super I/O Chips.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/machine.h>
#include <86box/nvr.h>
#include <86box/apm.h>
#include <86box/access_bus.h>
#include <86box/acpi.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/video.h>
#include <86box/sio.h>
#include "cpu.h"

typedef struct fdc37c93x_t {
    uint8_t       chip_id;
    uint8_t       is_apm;
    uint8_t       is_compaq;
    uint8_t       has_nvr;
    uint8_t       max_ld;
    uint8_t       tries;
    uint8_t       port_370;
    uint8_t       gpio_reg;
    uint8_t       gpio_regs[256];
    uint8_t       gpio_pulldn[8];
    uint8_t       auxio_reg;
    uint8_t       regs[48];
    uint8_t       alt_regs[3][8];
    uint8_t       ld_regs[11][256];
    uint16_t      kbc_type;
    uint16_t      superio_base;
    uint16_t      fdc_base;
    uint16_t      lpt_base;
    uint16_t      nvr_pri_base;
    uint16_t      nvr_sec_base;
    uint16_t      kbc_base;
    uint16_t      gpio_base; /* Set to EA */
    uint16_t      auxio_base;
    uint16_t      uart_base[2];
    int           locked;
    int           cur_reg;
    fdc_t        *fdc;
    access_bus_t *access_bus;
    nvr_t        *nvr;
    acpi_t       *acpi;
    void         *kbc;
    serial_t     *uart[2];
    lpt_t        *lpt;
} fdc37c93x_t;

static void    fdc37c93x_write(uint16_t port, uint8_t val, void *priv);
static uint8_t fdc37c93x_read(uint16_t port, void *priv);

static uint8_t gp_func_regs[8][8] = { { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },      /* GP00-GP07 */
                                      { 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7 },      /* GP10-GP17 */
                                      { 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef },      /* GP20-GP27 */
                                      { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },      /* GP30-GP37 */
                                      { 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7 },      /* GP40-GP47 */
                                      { 0xc8, 0xc9, 0xff, 0xcb, 0xcc, 0xff, 0xff, 0xff },      /* GP50-GP57 */
                                      { 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7 },      /* GP60-GP67 */
                                      { 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf } };    /* GP70-GP77 */

static uint16_t
make_port_superio(const fdc37c93x_t *dev)
{
    const uint16_t r0 = dev->regs[0x26];
    const uint16_t r1 = dev->regs[0x27];

    const uint16_t p = (r1 << 8) + r0;

    return p;
}

static uint16_t
make_port(const fdc37c93x_t *dev, const uint8_t ld)
{
    const uint16_t r0 = dev->ld_regs[ld][0x60];
    const uint16_t r1 = dev->ld_regs[ld][0x61];

    const uint16_t p = (r0 << 8) + r1;

    return p;
}

static uint16_t
make_port_sec(const fdc37c93x_t *dev, const uint8_t ld)
{
    const uint16_t r0 = dev->ld_regs[ld][0x62];
    const uint16_t r1 = dev->ld_regs[ld][0x63];

    const uint16_t p = (r0 << 8) + r1;

    return p;
}

static uint8_t
fdc37c93x_auxio_read(UNUSED(uint16_t port), void *priv)
{
    const fdc37c93x_t *dev = (fdc37c93x_t *) priv;

    return dev->auxio_reg;
}

static void
fdc37c93x_auxio_write(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;

    dev->auxio_reg = val;
}

static __inline uint8_t
fdc37c93x_do_read_gp(fdc37c93x_t *dev, int reg, int bit)
{
    /* Update bit 2 on the Acer V35N according to the selected graphics card type. */
    if ((reg == 2) && !strncmp(machine_get_internal_name(), "acer", 4))
        dev->gpio_pulldn[reg] = (dev->gpio_pulldn[reg] & 0xfb) | (video_is_mda() ? 0x00 : 0x04);

    return dev->gpio_regs[reg] & dev->gpio_pulldn[reg] & (1 << bit);
}

static __inline uint8_t
fdc37c93x_do_read_alt(const fdc37c93x_t *dev, int alt, int reg, int bit)
{
    return dev->alt_regs[alt][reg] & (1 << bit);
}

static uint8_t
fdc37c93x_read_gp(const fdc37c93x_t *dev, int reg, int bit)
{
    uint8_t gp_reg      = gp_func_regs[reg][bit];
    uint8_t gp_func_reg = dev->ld_regs[0x08][gp_reg];
    uint8_t gp_func;
    uint8_t ret         = 1 << bit;

    if (gp_func_reg & 0x01)  switch (reg) {
        default:
            /* Do nothing, this GP does not exist. */
            break;
        case 1:
            switch (bit) {
               default:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x00)
                        ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                    else
                        ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                    break;
                case 1:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (!(gp_func & 0x01)) {
                        if (gp_func & 0x02)
                            ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                        else
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                    }
                    break;
                case 3:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x01)
                        /* TODO: Write to power LED if it's ever implemented. */
                        ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                    else
                        ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                    break;
               case 6:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                        case 1 ... 3:
                            ret = fdc37c93x_do_read_alt(dev, gp_func - 1, reg, bit);
                            break;
                    }
                    break;
            }
            break;
        case 2:
            switch (bit) {
                default:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x00)
                        ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                    else
                        ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                    break;
                case 0:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                        case 1:
                            ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                            break;
                        case 2:
                            ret = kbc_at_read_p(dev->kbc, 2, 0x01) ? (1 << bit) : 0x00;
                            break;
                    }
                    break;
                case 1: case 2:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                        case 1: case 2:
                            ret = fdc37c93x_do_read_alt(dev, gp_func - 1, reg, bit);
                            break;
                    }
                    break;
                case 5:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x00)
                        ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                    else
                        ret = kbc_at_read_p(dev->kbc, 2, 0x02) ? (1 << bit) : 0x00;
                    break;
                case 6: case 7:
                    /* Do nothing, these bits do not exist. */
                    break;
            }
            break;
        case 4:
            gp_func = (gp_func_reg >> 3) & 0x03;
            switch (bit) {
                default:
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                            break;
                        case 1:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                    }
                    break;
                case 0: case 1:
                    switch (gp_func) {
                        case 0:
                            ret = fdc_get_media_id(dev->fdc, bit ^ 1) ? (1 << bit) : 0x00;
                            break;
                        case 1:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                    }
                    break;
                case 6:
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                            break;
                        case 1:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                        case 2:
                            /* TODO: Write to power LED if it's ever implemented. */
                            ret = fdc37c93x_do_read_alt(dev, 1, reg, bit);
                            break;
                        case 3:
                            ret = fdc37c93x_do_read_alt(dev, 2, reg, bit);
                            break;
                    }
                    break;
                case 7:
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                            break;
                        case 1:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                        case 2:
                            ret = fdc37c93x_do_read_alt(dev, 1, reg, bit);
                            break;
                    }
                    break;
            }
            break;
        case 5:
            gp_func = (gp_func_reg >> 3) & 0x03;
            switch (bit) {
                default:
                    break;
                case 0: case 3: case 4:
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                            break;
                        case 1:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                    }
                    break;
                case 1:
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                            break;
                        case 1:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                    }
                    break;
            }
            break;
        case 6:
            gp_func = (gp_func_reg >> 3) & 0x03;
            switch (bit) {
                default:
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                            break;
                        case 1:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                        case 2:
                            ret = kbc_at_read_p(dev->kbc, 1, 1 << bit);
                            break;
                    }
                    break;
                case 0:
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                            break;
                        case 1:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                        case 2:
                            /* TODO: Write to power LED if it's ever implemented. */
                            ret = fdc37c93x_do_read_alt(dev, 1, reg, bit);
                            break;
                    }
                    break;
                case 1:
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                            break;
                        case 1:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                        case 2:
                            ret = fdc37c93x_do_read_alt(dev, 1, reg, bit);
                            break;
                    }
                    break;
            }
            break;
        case 7:
            gp_func = (gp_func_reg >> 3) & 0x03;
            switch (bit) {
                default:
                    switch (gp_func) {
                        case 0:
                            ret = fdc37c93x_do_read_alt(dev, 0, reg, bit);
                            break;
                        case 1:
                            ret = fdc37c93x_do_read_gp((fdc37c93x_t *) dev, reg, bit);
                            break;
                    }
                    break;
            }
            break;
    }

    if (gp_func_reg & 0x02)
        ret ^= (1 << bit);

    return ret;
}

static __inline void
fdc37c93x_do_write_gp(fdc37c93x_t *dev, int reg, int bit, int set)
{
    dev->gpio_regs[reg] = (dev->gpio_regs[reg] & ~(1 << bit)) |
                          (set << bit);
}

static __inline void
fdc37c93x_do_write_alt(fdc37c93x_t *dev, int alt, int reg, int bit, int set)
{
    dev->alt_regs[alt][reg] = (dev->alt_regs[alt][reg] & ~(1 << bit)) |
                              (set << bit);
}

static void
fdc37c93x_write_gp(fdc37c93x_t *dev, int reg, int bit, int set)
{
    uint8_t gp_func_reg = dev->ld_regs[0x08][gp_func_regs[reg][bit]];
    uint8_t gp_func;

    if (gp_func_reg & 0x02)
        set = !set;

    if (!(gp_func_reg & 0x01))  switch (reg) {
        default:
            /* Do nothing, this GP does not exist. */
            break;
        case 1:
            switch (bit) {
               default:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x00)
                        fdc37c93x_do_write_gp(dev, reg, bit, set);
                    else
                        fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                    break;
                case 1:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (!(gp_func & 0x01)) {
                        if (gp_func & 0x02) {
                            set ? picint(1 << 13) : picintc(1 << 13);
                            fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                        } else
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                    }
                    break;
                case 3:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x01)
                        /* TODO: Write to power LED if it's ever implemented. */
                        fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                    else
                        fdc37c93x_do_write_gp(dev, reg, bit, set);
                    break;
               case 6:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                        case 1 ... 3:
                            fdc37c93x_do_write_alt(dev, gp_func - 1, reg, bit, set);
                            break;
                    }
                    break;
            }
            break;
        case 2:
            switch (bit) {
                default:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x00)
                        fdc37c93x_do_write_gp(dev, reg, bit, set);
                    else
                        fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                    break;
                case 0:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                        case 1:
                            fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                            break;
                        case 2:
                            kbc_at_write_p(dev->kbc, 2, 0xfe, set);
                            break;
                    }
                    break;
                case 1: case 2:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                        case 1: case 2:
                            fdc37c93x_do_write_alt(dev, gp_func - 1, reg, bit, set);
                            break;
                    }
                    break;
                case 5:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x00)
                        fdc37c93x_do_write_gp(dev, reg, bit, set);
                    else
                        kbc_at_write_p(dev->kbc, 2, 0xfd, set << 1);
                    break;
                case 6: case 7:
                    /* Do nothing, these bits do not exist. */
                    break;
            }
            break;
        case 4:
            gp_func = (gp_func_reg >> 3) & 0x03;
            switch (bit) {
                default:
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                            break;
                        case 1:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                    }
                    break;
                case 0: case 1:
                    switch (gp_func) {
                        case 0:
                            fdc_set_media_id(dev->fdc, bit ^ 1, set);
                            break;
                        case 1:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                    }
                    break;
                case 6:
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                            break;
                        case 1:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                        case 2:
                            /* TODO: Write to power LED if it's ever implemented. */
                            fdc37c93x_do_write_alt(dev, 1, reg, bit, set);
                            break;
                        case 3:
                            fdc37c93x_do_write_alt(dev, 2, reg, bit, set);
                            break;
                    }
                    break;
                case 7:
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                            break;
                        case 1:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                        case 2:
                            fdc37c93x_do_write_alt(dev, 1, reg, bit, set);
                            if (!set)
                                smi_raise();
                            break;
                    }
                    break;
            }
            break;
        case 5:
            gp_func = (gp_func_reg >> 3) & 0x03;
            switch (bit) {
                default:
                    break;
                case 0: case 3: case 4:
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                            break;
                        case 1:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                    }
                    break;
                case 1:
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                            if (set)
                                plat_power_off();
                            break;
                        case 1:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                    }
                    break;
            }
            break;
        case 6:
            gp_func = (gp_func_reg >> 3) & 0x03;
            switch (bit) {
                default:
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                            break;
                        case 1:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                        case 2:
                            kbc_at_write_p(dev->kbc, 1, ~(1 << bit), set << bit);
                            break;
                    }
                    break;
                case 0:
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                            break;
                        case 1:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                        case 2:
                            /* TODO: Write to power LED if it's ever implemented. */
                            fdc37c93x_do_write_alt(dev, 1, reg, bit, set);
                            break;
                    }
                    break;
                case 1:
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                            break;
                        case 1:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                        case 2:
                            fdc37c93x_do_write_alt(dev, 1, reg, bit, set);
                            break;
                    }
                    break;
            }
            break;
        case 7:
            gp_func = (gp_func_reg >> 3) & 0x03;
            switch (bit) {
                default:
                    switch (gp_func) {
                        case 0:
                            fdc37c93x_do_write_alt(dev, 0, reg, bit, set);
                            break;
                        case 1:
                            fdc37c93x_do_write_gp(dev, reg, bit, set);
                            break;
                    }
                    break;
            }
            break;
    }
}

static uint8_t
fdc37c93x_gpio_read(uint16_t port, void *priv)
{
    const fdc37c93x_t *dev = (fdc37c93x_t *) priv;
    uint8_t            ret = 0xff;

    if (dev->locked) { 
        if (dev->is_compaq)
            ret = fdc37c93x_read(port & 0x0001, priv);
    } else if (port & 0x0001)  switch (dev->gpio_reg) {
        case 0x01: case 0x02:
            ret = 0x00;
            for (uint8_t i = 0; i < 8; i++)
                ret |= fdc37c93x_read_gp(dev, dev->gpio_reg, i);
            break;
        case 0x03:
            ret = dev->ld_regs[0x08][0xf4];
            break;
        case 0x04 ... 0x07:
            if (dev->chip_id >= FDC37C93X_FR) {
                ret = 0x00;
                for (uint8_t i = 0; i < 8; i++)
                    ret |= fdc37c93x_read_gp(dev, dev->gpio_reg, i);
            }
            break;
        case 0x08 ... 0x0f:
            if (dev->chip_id >= FDC37C93X_FR)
                ret = dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08];
            break;
    } else
        ret = dev->gpio_reg;

    return ret;
}

static void
fdc37c93x_gpio_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;

    if (dev->locked) { 
        if (dev->is_compaq)
            fdc37c93x_write(port & 0x0001, val, priv);
    } else if (port & 0x0001)  switch (dev->gpio_reg) {
        case 0x01: case 0x02:
            for (uint8_t i = 0; i < 8; i++)
                fdc37c93x_write_gp(dev, dev->gpio_reg, i, val & (1 << i));
            break;
        case 0x03:
            if (dev->chip_id >= FDC37C93X_FR)
                dev->ld_regs[0x08][0xf4] = val & 0xef;
            else
                dev->ld_regs[0x08][0xf4] = val & 0x0f;
            break;
        case 0x04 ... 0x07:
            if (dev->chip_id >= FDC37C93X_FR)
                for (uint8_t i = 0; i < 8; i++)
                    fdc37c93x_write_gp(dev, dev->gpio_reg, i, val & (1 << i));
            break;
        case 0x08: case 0x0a:
        case 0x0c: case 0x0e:
            if (dev->chip_id >= FDC37C93X_FR)
                dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08] = val;
            break;
        case 0x09:
            if (dev->chip_id >= FDC37C93X_FR)
                dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08] = val & 0xd3;
            break;
        case 0x0b:
            if (dev->chip_id >= FDC37C93X_FR)
                dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08] = val & 0x17;
            break;
        case 0x0d:
            if (dev->chip_id == FDC37C93X_APM)
                dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08] = val;
            else if (dev->chip_id == FDC37C93X_FR)
                dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08] = val & 0xbf;
            break;
        case 0x0f:
            if (dev->chip_id == FDC37C93X_APM)
                dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08] = val & 0x7f;
            else if (dev->chip_id == FDC37C93X_FR)
                dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08] = val & 0x3f;
            break;
    } else
        dev->gpio_reg = val;
}

static void
fdc37c93x_superio_handler(fdc37c93x_t *dev)
{
    if (!dev->is_compaq) {
        if (dev->superio_base != 0x0000)
            io_removehandler(dev->superio_base, 0x0002,
                             fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL, dev);
        dev->superio_base = make_port_superio(dev);
        if (dev->superio_base != 0x0000)
            io_sethandler(dev->superio_base, 0x0002,
                          fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL, dev);
    }
}

static void
fdc37c93x_fdc_handler(fdc37c93x_t *dev)
{
    const uint8_t  global_enable = !!(dev->regs[0x22] & (1 << 0));
    const uint8_t  local_enable  = !!dev->ld_regs[0][0x30];
    const uint16_t old_base      = dev->fdc_base;

    dev->fdc_base = 0x0000;

    if (global_enable && local_enable)
        dev->fdc_base = make_port(dev, 0) & 0xfff8;

    if (dev->fdc_base != old_base) {
        if ((old_base >= 0x0100) && (old_base <= 0x0ff8))
            fdc_remove(dev->fdc);

        if ((dev->fdc_base >= 0x0100) && (dev->fdc_base <= 0x0ff8))
            fdc_set_base(dev->fdc, dev->fdc_base);
    }
}

static void
fdc37c93x_lpt_handler(fdc37c93x_t *dev)
{
    uint16_t ld_port       = 0x0000;
    uint16_t mask          = 0xfffc;
    uint8_t  global_enable   = !!(dev->regs[0x22] & (1 << 3));
    uint8_t  local_enable    = !!dev->ld_regs[3][0x30];
    uint8_t  lpt_irq         = dev->ld_regs[3][0x70];
    uint8_t  lpt_dma         = dev->ld_regs[3][0x74];
    uint8_t  lpt_mode        = dev->ld_regs[3][0xf0] & 0x07;
    uint8_t  irq_readout[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x08,
                                 0x00, 0x10, 0x18, 0x20, 0x00, 0x00, 0x28, 0x30 };

    if (lpt_irq > 15)
        lpt_irq = 0xff;

    if (lpt_dma >= 4)
        lpt_dma = 0xff;

    lpt_port_remove(dev->lpt);
    lpt_set_fifo_threshold(dev->lpt, (dev->ld_regs[3][0xf0] & 0x78) >> 3);
    switch (lpt_mode) {
        default:
        case 0x04:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x00:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 1);
            break;
        case 0x01: case 0x05:
            mask = 0xfff8;
            lpt_set_epp(dev->lpt, 1);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x02:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 1);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x03: case 0x07:
            mask = 0xfff8;
            lpt_set_epp(dev->lpt, 1);
            lpt_set_ecp(dev->lpt, 1);
            lpt_set_ext(dev->lpt, 0);
            break;
    }
    if (global_enable && local_enable) {
        ld_port = (make_port(dev, 3) & 0xfffc) & mask;
        if ((ld_port >= 0x0100) && (ld_port <= (0x0ffc & mask)))
            lpt_port_setup(dev->lpt, ld_port);
    }
    lpt_port_irq(dev->lpt, lpt_irq);
    lpt_port_dma(dev->lpt, lpt_dma);

    lpt_set_cnfgb_readout(dev->lpt, ((lpt_irq > 15) ? 0x00 : irq_readout[lpt_irq]) |
                                    ((lpt_dma >= 4) ? 0x00 : lpt_dma));
}

static void
fdc37c93x_serial_handler(fdc37c93x_t *dev, const int uart)
{
    const uint8_t  uart_no       = 4 + uart;
    const uint8_t  global_enable = !!(dev->regs[0x22] & (1 << uart_no));
    const uint8_t  local_enable  = !!dev->ld_regs[uart_no][0x30];
    const uint16_t old_base      = dev->uart_base[uart];
    double         clock_src     = 24000000.0 / 13.0;

    dev->uart_base[uart] = 0x0000;

    if (global_enable && local_enable)
        dev->uart_base[uart] = make_port(dev, uart_no) & 0xfff8;

    if (dev->uart_base[uart] != old_base) {
        if ((old_base >= 0x0100) && (old_base <= 0x0ff8))
            serial_remove(dev->uart[uart]);

        if ((dev->uart_base[uart] >= 0x0100) && (dev->uart_base[uart] <= 0x0ff8))
            serial_setup(dev->uart[uart], dev->uart_base[uart], dev->ld_regs[uart_no][0x70]);
    }

    switch (dev->ld_regs[uart_no][0xf0] & 0x03) {
        case 0x00:
            clock_src = 24000000.0 / 13.0;
            break;
        case 0x01:
            clock_src = 24000000.0 / 12.0;
            break;
        case 0x02:
            clock_src = 24000000.0 / 1.0;
            break;
        case 0x03:
            clock_src = 24000000.0 / 1.625;
            break;

        default:
            break;
    }

    serial_set_clock_src(dev->uart[uart], clock_src);

    /*
       TODO: If UART 2's own IRQ pin is also enabled when shared,
             it should also be asserted.
     */
    if ((dev->chip_id >= FDC37C93X_FR) && (dev->ld_regs[4][0xf0] & 0x80)) {
        serial_irq(dev->uart[0], dev->ld_regs[4][0x70]);
        serial_irq(dev->uart[1], dev->ld_regs[4][0x70]);
    } else
        serial_irq(dev->uart[uart], dev->ld_regs[uart_no][0x70]);
}

static void
fdc37c93x_nvr_pri_handler(const fdc37c93x_t *dev)
{
    uint8_t  local_enable = !!dev->ld_regs[6][0x30];

    if (dev->chip_id != 0x02)
        local_enable &= ((dev->ld_regs[6][0xf0] & 0x90) != 0x80);

    if (dev->has_nvr) {
        nvr_at_handler(0, 0x70, dev->nvr);
        if (local_enable)
            nvr_at_handler(1, 0x70, dev->nvr);
    }
}

static void
fdc37c93x_nvr_sec_handler(fdc37c93x_t *dev)
{
    uint8_t        local_enable = !!dev->ld_regs[6][0x30];
    const uint16_t old_base     = dev->nvr_sec_base;

    local_enable &= (((dev->ld_regs[6][0xf0] & 0xe0) == 0x80) ||
                     ((dev->ld_regs[6][0xf0] & 0xe0) == 0xe0));

    dev->nvr_sec_base = 0x0000;

    if (local_enable)
        dev->nvr_sec_base = make_port_sec(dev, 6) & 0xfffe;

    if (dev->nvr_sec_base != old_base) {
        if (dev->has_nvr && (old_base > 0x0000) && (old_base <= 0x0ffe))
            nvr_at_sec_handler(0, dev->nvr_sec_base, dev->nvr);

        /* Datasheet erratum: First it says minimum address is 0x0100, but later implies that it's 0x0000
                              and that default is 0x0070, same as (unrelocatable) primary NVR. */
        if (dev->has_nvr && (dev->nvr_sec_base > 0x0000) && (dev->nvr_sec_base <= 0x0ffe))
            nvr_at_sec_handler(1, dev->nvr_sec_base, dev->nvr);
    }
}

static void
fdc37c93x_kbc_handler(fdc37c93x_t *dev)
{
    const uint8_t  local_enable = !!dev->ld_regs[7][0x30];
    const uint16_t old_base = dev->kbc_base;

    dev->kbc_base = local_enable ? 0x0060 : 0x0000;

    if (dev->kbc_base != old_base)
        kbc_at_handler(local_enable, dev->kbc_base, dev->kbc);

    kbc_at_set_irq(0, dev->ld_regs[7][0x70], dev->kbc);
    kbc_at_set_irq(1, dev->ld_regs[7][0x72], dev->kbc);
}

static void
fdc37c93x_auxio_handler(fdc37c93x_t *dev)
{
    const uint8_t local_enable = !!dev->ld_regs[8][0x30];
    const uint16_t old_base    = dev->auxio_base;

    if (local_enable)
        dev->auxio_base = make_port(dev, 8);
    else
        dev->auxio_base = 0x0000;

    if (dev->auxio_base != old_base) {
        if ((old_base >= 0x0100) && (old_base <= 0x0fff))
            io_removehandler(old_base, 0x0001,
                         fdc37c93x_auxio_read, NULL, NULL, fdc37c93x_auxio_write, NULL, NULL, dev);

        if ((dev->auxio_base >= 0x0100) && (dev->auxio_base <= 0x0fff))
            io_sethandler(dev->auxio_base, 0x0001,
                          fdc37c93x_auxio_read, NULL, NULL, fdc37c93x_auxio_write, NULL, NULL, dev);
    }
}

static void
fdc37c93x_gpio_handler(fdc37c93x_t *dev)
{
    const uint8_t local_enable = !!(dev->regs[0x03] & 0x80) ||
                                   (dev->is_compaq && dev->locked);
    const uint16_t old_base    = dev->gpio_base;

    dev->gpio_base = 0x0000;

    if (local_enable)  switch (dev->regs[0x03] & 0x03) {
        default:
            break;
        case 0:
            dev->gpio_base = 0x00e0;
            break;
        case 1:
            dev->gpio_base = 0x00e2;
            break;
        case 2:
            dev->gpio_base = 0x00e4;
            break;
        case 3:
            dev->gpio_base = 0x00ea; /* Default */
            break;
    }

    if (dev->gpio_base != old_base) {
        if (old_base != 0x0000)
            io_removehandler(old_base, 0x0002,
                         fdc37c93x_gpio_read, NULL, NULL, fdc37c93x_gpio_write, NULL, NULL, dev);

        if (dev->gpio_base > 0x0000)
            io_sethandler(dev->gpio_base, 0x0002,
                          fdc37c93x_gpio_read, NULL, NULL, fdc37c93x_gpio_write, NULL, NULL, dev);
    }
}

static void
fdc37c93x_access_bus_handler(fdc37c93x_t *dev)
{
    const uint8_t  global_enable = !!(dev->regs[0x22] & (1 << 6));
    const uint8_t  local_enable  = !!dev->ld_regs[9][0x30];
    const uint16_t ld_port = dev->access_bus->base = make_port(dev, 9);

    access_bus_handler(dev->access_bus, global_enable && local_enable, ld_port);
}

static void
fdc37c93x_acpi_handler(fdc37c93x_t *dev)
{
    uint16_t      ld_port;
    const uint8_t local_enable = !!dev->ld_regs[0x0a][0x30];
    const uint8_t sci_irq      = dev->ld_regs[0x0a][0x70];

    acpi_update_io_mapping(dev->acpi, 0x0000, local_enable);
    if (local_enable) {
        ld_port = make_port(dev, 0x0a) & 0xFFF0;
        if ((ld_port >= 0x0100) && (ld_port <= 0x0FF0))
            acpi_update_io_mapping(dev->acpi, ld_port, local_enable);
    }

    acpi_update_aux_io_mapping(dev->acpi, 0x0000, local_enable);
    if (local_enable) {
        ld_port = make_port_sec(dev, 0x0a) & 0xFFF8;
        if ((ld_port >= 0x0100) && (ld_port <= 0x0FF8))
            acpi_update_aux_io_mapping(dev->acpi, ld_port, local_enable);
    }

    acpi_set_irq_line(dev->acpi, sci_irq);
}

static void
fdc37c93x_state_change(fdc37c93x_t *dev, const uint8_t locked)
{
    dev->locked = locked;
    fdc_3f1_enable(dev->fdc, !locked);
    fdc37c93x_gpio_handler(dev);
}

static void
fdc37c93x_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c93x_t *dev    = (fdc37c93x_t *) priv;
    uint8_t      index  = !(port & 1);
    uint8_t      valxor;

    if (port == 0x00fb) {
        fdc37c93x_state_change(dev, 1);
        dev->tries = 0;
    } else if (port == 0x00f9)
        fdc37c93x_state_change(dev, 0);
    else if (index) {
        if ((!dev->is_compaq) && (val == 0x55) && !dev->locked) {
            if (dev->tries) {
                fdc37c93x_state_change(dev, 1);
                dev->tries = 0;
            } else
                dev->tries++;
        } else if (dev->locked) {
            if ((!dev->is_compaq) && (val == 0xaa))
                fdc37c93x_state_change(dev, 0);
            else
                dev->cur_reg = val;
        } else if ((!dev->is_compaq) && dev->tries)
            dev->tries = 0;
    } else if (dev->locked) {
        if (dev->cur_reg < 0x30) {
            valxor = val ^ dev->regs[dev->cur_reg];

            switch (dev->cur_reg) {
                case 0x02:
                    dev->regs[dev->cur_reg] = val;
                    if (val == 0x02)
                        fdc37c93x_state_change(dev, 0);
                    break;
                case 0x03:
                    dev->regs[dev->cur_reg] = val & 0x83;
                    fdc37c93x_gpio_handler(dev);
                    break;
                case 0x07: case 0x26:
                case 0x2e ... 0x2f:
                    dev->regs[dev->cur_reg] = val;
                    break;
                case 0x22:
                    if (dev->chip_id >= FDC37C93X_FR)
                        dev->regs[dev->cur_reg] = val & 0x7f;
                    else
                        dev->regs[dev->cur_reg] = val & 0x6f;

                    if (valxor & 0x01)
                        fdc37c93x_fdc_handler(dev);
                    if (valxor & 0x08)
                        fdc37c93x_lpt_handler(dev);
                    if (valxor & 0x10)
                        fdc37c93x_serial_handler(dev, 0);
                    if (valxor & 0x20)
                        fdc37c93x_serial_handler(dev, 1);
                    if ((dev->chip_id >= FDC37C93X_FR) && (valxor & 0x40))
                        fdc37c93x_access_bus_handler(dev);
                    break;
                case 0x23:
                    if (dev->chip_id >= FDC37C93X_FR)
                        dev->regs[dev->cur_reg] = val & 0x7f;
                    else
                        dev->regs[dev->cur_reg] = val & 0x6f;
                    break;
                case 0x24:
                    if (dev->chip_id >= FDC37C93X_FR)
                        dev->regs[dev->cur_reg] = val & 0xcf;
                    else
                        dev->regs[dev->cur_reg] = val & 0xcc;

                    if ((dev->chip_id >= FDC37C93X_FR) && (valxor & 0x01)) {
                        serial_set_clock_src(dev->uart[0], (val & 0x01) ?
                                             48000000.0 : 24000000.0);
                        serial_set_clock_src(dev->uart[1], (val & 0x01) ?
                                             48000000.0 : 24000000.0);
                    }
                    break;
                case 0x27:
                    if (dev->chip_id >= FDC37C93X_FR) {
                        dev->regs[dev->cur_reg] = val;

                        fdc37c93x_superio_handler(dev);
                    }
                    break;
                case 0x28:
                    if (dev->chip_id >= FDC37C93X_FR)
                        dev->regs[dev->cur_reg] = val & 0x1f;
                     break;

                default:
                    break;
            }
        } else {
            valxor = val ^ dev->ld_regs[dev->regs[7]][dev->cur_reg];

            if ((dev->regs[7] <= dev->max_ld) && ((dev->regs[7] != 0x08) ||
                (dev->cur_reg < 0xb0) || (dev->cur_reg > 0xdf) ||
                (dev->chip_id >= FDC37C93X_FR)))  switch (dev->regs[7]) {
                case 0x00:    /* FDD */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                        case 0x74:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x01;
                            if (valxor)
                                fdc37c93x_fdc_handler(dev);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x1f;

                            if (valxor & 0x01)
                                fdc_update_enh_mode(dev->fdc, val & 0x01);
                            if (valxor & 0x0c) {
                                fdc_clear_flags(dev->fdc, FDC_FLAG_PS2 | FDC_FLAG_PS2_MCA);
                                switch (val & 0x0c) {
                                    case 0x00:
                                        fdc_set_flags(dev->fdc, FDC_FLAG_PS2);
                                        break;
                                    case 0x04:
                                        fdc_set_flags(dev->fdc, FDC_FLAG_PS2_MCA);
                                        break;
                                }
                            }
                            if (valxor & 0x10)
                                fdc_set_swap(dev->fdc, (val & 0x10) >> 4);
                            break;
                        case 0xf1:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xfc;

                            if (valxor & 0x0c)
                                fdc_update_densel_force(dev->fdc, (val & 0xc) >> 2);
                            break;
                        case 0xf2:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor & 0xc0)
                                fdc_update_rwc(dev->fdc, 3, (val & 0xc0) >> 6);
                            if (valxor & 0x30)
                                fdc_update_rwc(dev->fdc, 2, (val & 0x30) >> 4);
                            if (valxor & 0x0c)
                                fdc_update_rwc(dev->fdc, 1, (val & 0x0c) >> 2);
                            if (valxor & 0x03)
                                fdc_update_rwc(dev->fdc, 0, (val & 0x03));
                            break;
                        case 0xf4 ... 0xf7:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x5b;

                            if (valxor & 0x18)
                                fdc_update_drvrate(dev->fdc, dev->cur_reg - 0xf4,
                                                   (val & 0x18) >> 3);
                            break;
                    }
                    break;
                case 0x01:    /* IDE1 */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x70:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x02;
                            break;
                        case 0xf0: case 0xf1:
                            if (dev->chip_id >= FDC37C93X_FR)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x1f;
                            else if (dev->cur_reg == 0xf0)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                    }
                    break;
                case 0x02:    /* IDE2 */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x70:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x04;
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x01;
                            break;
                    }
                    break;
                case 0x03:    /* Parallel Port */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                        case 0x74:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x08;
                            if (valxor)
                                fdc37c93x_lpt_handler(dev);
                            break;
                        /*
                           Bits 2:0: Mode:
                               - 000: Bi-directional (SPP);
                               - 001: EPP-1.9 and SPP;
                               - 010: ECP;
                               - 011: ECP and EPP-1.9;
                               - 101: EPP-1.7 and SPP;
                               - 110: ECP and EPP-1.7.
                           Bits 6:3: ECP FIFO Threshold.
                         */
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            if (valxor & 0x7f)
                                fdc37c93x_lpt_handler(dev);
                            break;
                        case 0xf1:
                            if (dev->chip_id >= FDC37C93X_FR)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;
                            break;
                    }
                    break;
                case 0x04:    /* Serial port 1 */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x10;
                            if (valxor)
                                fdc37c93x_serial_handler(dev, 0);
                            break;
                        case 0xf0:
                            if (dev->chip_id >= FDC37C93X_FR) {
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x83;
                            } else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;

                            if (valxor & 0x83) {
                                fdc37c93x_serial_handler(dev, 0);
                                fdc37c93x_serial_handler(dev, 1);
                            }
                            break;
                    }
                    break;
                case 0x05:    /* Serial port 2 */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x70:
                        case 0x74:
                            if (((dev->cur_reg != 0x62) && (dev->cur_reg != 0x63)) ||
                                (dev->chip_id == FDC37C93X_FR)) {
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                                if ((dev->cur_reg == 0x30) && (val & 0x01))
                                    dev->regs[0x22] |= 0x20;
                                if (valxor)
                                    fdc37c93x_serial_handler(dev, 1);
                            }
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;

                            if (valxor & 0x03) {
                                fdc37c93x_serial_handler(dev, 0);
                                fdc37c93x_serial_handler(dev, 1);
                            }
                            break;
                        case 0xf1:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x7f;
                            break;
                        case 0xf2:
                            if (dev->chip_id >= FDC37C93X_FR)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                    }
                    break;
                case 0x06:    /* RTC */
                    switch (dev->cur_reg) {
                        case 0x30:
                        // case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x70:
                            if (((dev->cur_reg != 0x62) && (dev->cur_reg != 0x63)) ||
                                (dev->chip_id >= FDC37C93X_FR)) {
                               dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                               if (valxor) {
                                    fdc37c93x_nvr_pri_handler(dev);

                                    if (dev->chip_id >= FDC37C93X_FR)
                                        fdc37c93x_nvr_sec_handler(dev);
                                }
                            }
                            break;
                        case 0xf0:
                            if (dev->chip_id >= FDC37C93X_FR)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x8f;

                            if (dev->has_nvr && valxor) {
                                nvr_lock_set(0x80, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x01), dev->nvr);
                                nvr_lock_set(0xa0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x02), dev->nvr);
                                nvr_lock_set(0xc0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x04), dev->nvr);
                                nvr_lock_set(0xe0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x08), dev->nvr);
                                if (dev->ld_regs[6][dev->cur_reg] & 0x80) {
                                    if (dev->chip_id == FDC37C93X_NORMAL)
                                        nvr_bank_set(0, 1, dev->nvr);
                                    else  switch ((dev->ld_regs[6][dev->cur_reg] >> 4) & 0x07) {
                                        case 0x00:
                                        default:
                                            nvr_bank_set(0, 0xff, dev->nvr);
                                            nvr_bank_set(1, 1, dev->nvr);
                                            break;
                                        case 0x01:
                                            nvr_bank_set(0, 0, dev->nvr);
                                            nvr_bank_set(1, 1, dev->nvr);
                                            break;
                                        case 0x02: case 0x04:
                                            nvr_bank_set(0, 0xff, dev->nvr);
                                            nvr_bank_set(1, 0xff, dev->nvr);
                                            break;
                                        case 0x03: case 0x05:
                                            nvr_bank_set(0, 0, dev->nvr);
                                            nvr_bank_set(1, 0xff, dev->nvr);
                                            break;
                                        case 0x06:
                                            nvr_bank_set(0, 0xff, dev->nvr);
                                            nvr_bank_set(1, 2, dev->nvr);
                                            break;
                                        case 0x07:
                                            nvr_bank_set(0, 0, dev->nvr);
                                            nvr_bank_set(1, 2, dev->nvr);
                                            break;
                                    }
                                } else {
                                    nvr_bank_set(0, 0, dev->nvr);
                                    if (dev->chip_id >= FDC37C93X_FR)
                                        nvr_bank_set(1, 0xff, dev->nvr);
                                }

                                fdc37c93x_nvr_pri_handler(dev);
                                if (dev->chip_id >= FDC37C93X_FR)
                                   fdc37c93x_nvr_sec_handler(dev);
                            }
                            break;
                        case 0xf1:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x1f;
                            break;
                        case 0xf2: case 0xf3:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                        case 0xf4:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x83;
                            break;
                    }
                    break;
                case 0x07:    /* Keyboard */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x70: case 0x72:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor)
                                fdc37c93x_kbc_handler(dev);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x87;
                            break;
                    }
                    break;
                case 0x08:    /* Aux. I/O */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x70:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor)
                                fdc37c93x_auxio_handler(dev);
                            break;
                        case 0xb0: case 0xb2:
                        case 0xb4: case 0xb6:
                        case 0xe0: case 0xe1:
                        case 0xe9: case 0xf2:
                        case 0xf3:
                        case 0xc0 ... 0xc9:
                        case 0xcb ... 0xcc:
                        case 0xd0 ... 0xdf:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                        case 0xb1:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xd3;
                            break;
                        case 0xb3:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x17;
                            break;
                        case 0xb5:
                            if (dev->chip_id == FDC37C93X_APM)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xbf;
                            break;
                        case 0xb7:
                            if (dev->chip_id == FDC37C93X_APM)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x7f;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x3f;
                            break;
                        case 0xb8:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x3f;
                            break;
                        case 0x18:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x18;
                            break;
                        case 0xe2 ... 0xe5:
                        case 0xe7:
                        case 0xeb ... 0xed:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x0f;
                            break;
                        case 0xe6: case 0xe8:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x1f;
                            break;
                        case 0xea:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x9f;
                            break;
                        case 0xef:
                            if (dev->chip_id >= FDC37C93X_FR)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xf8;
                            break;
                        case 0xf1:
                            if (dev->chip_id >= FDC37C93X_FR)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x83;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;
                            break;
                        case 0xf4:
                            if (dev->chip_id >= FDC37C93X_FR)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xef;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x0f;
                            break;
                        case 0xf6:
                            if (dev->chip_id >= FDC37C93X_FR)
                                for (uint8_t i = 0; i < 8; i++)
                                    fdc37c93x_write_gp(dev, 1, i, val & (1 << i));
                            break;
                        case 0xf7:
                            if (dev->chip_id >= FDC37C93X_FR)
                                for (uint8_t i = 0; i < 8; i++)
                                    fdc37c93x_write_gp(dev, 2, i, val & (1 << i));
                            break;
                        case 0xf8:
                            if (dev->chip_id >= FDC37C93X_FR)
                                for (uint8_t i = 0; i < 8; i++)
                                    fdc37c93x_write_gp(dev, 4, i, val & (1 << i));
                            break;
                        case 0xf9:
                            if (dev->chip_id >= FDC37C93X_FR)
                                for (uint8_t i = 0; i < 8; i++)
                                    fdc37c93x_write_gp(dev, 5, i, val & (1 << i));
                            break;
                        case 0xfa:
                            if (dev->chip_id >= FDC37C93X_FR)
                                for (uint8_t i = 0; i < 8; i++)
                                    fdc37c93x_write_gp(dev, 6, i, val & (1 << i));
                            break;
                        case 0xfb:
                            if (dev->chip_id >= FDC37C93X_FR)
                                for (uint8_t i = 0; i < 8; i++)
                                    fdc37c93x_write_gp(dev, 7, i, val & (1 << i));
                            break;
                    }
                    break;
                case 0x09:    /* Access.Bus */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x40;
                            if (valxor)
                                fdc37c93x_access_bus_handler(dev);
                            break;
                    }
                    break;
                case 0x0a:    /* ACPI */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x70:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor)
                                fdc37c93x_acpi_handler(dev);
                            break;
                    }
                    break;
            }
        }
    }
}

static uint8_t
fdc37c93x_read(uint16_t port, void *priv)
{
    fdc37c93x_t *dev   = (fdc37c93x_t *) priv;
    uint8_t      index = (port & 1) ? 0 : 1;
    uint8_t      ret   = 0xff;

    /* Compaq Presario 4500: Unlock at FB, Register at EA, Data at EB, Lock at F9. */
    if ((port == 0xea) || (port == 0xf9) || (port == 0xfb))
        index = 1;
    else if (port == 0xeb)
        index = 0;

    if (dev->locked) {
        if (index)
            ret = dev->cur_reg;
        else {
            if (dev->cur_reg < 0x30) {
                if (dev->cur_reg == 0x20)
                    ret = dev->chip_id;
                else
                    ret = dev->regs[dev->cur_reg];
            } else if (dev->regs[7] <= dev->max_ld) {
                if ((dev->regs[7] == 0x00) && (dev->cur_reg == 0xf2))
                    ret = (fdc_get_rwc(dev->fdc, 0) | (fdc_get_rwc(dev->fdc, 1) << 2) |
                          (fdc_get_rwc(dev->fdc, 2) << 4) | (fdc_get_rwc(dev->fdc, 3) << 6));
                else if ((dev->regs[7] == 0x08) && (dev->cur_reg >= 0xf6) &&
                         (dev->cur_reg <= 0xfb) &&
                         (dev->chip_id >= FDC37C93X_FR))  switch (dev->cur_reg) {
                    case 0xf6:
                        if (dev->chip_id >= FDC37C93X_FR) {
                            ret = 0x00;
                            for (uint8_t i = 0; i < 8; i++)
                                ret |= fdc37c93x_read_gp(dev, 1, i);
                        }
                        break;
                    case 0xf7:
                        if (dev->chip_id >= FDC37C93X_FR) {
                            ret = 0x00;
                            for (uint8_t i = 0; i < 8; i++)
                                ret |= fdc37c93x_read_gp(dev, 2, i);
                        }
                        break;
                    case 0xf8:
                        if (dev->chip_id >= FDC37C93X_FR) {
                            ret = 0x00;
                            for (uint8_t i = 0; i < 8; i++)
                                ret |= fdc37c93x_read_gp(dev, 4, i);
                        }
                        break;
                    case 0xf9:
                        if (dev->chip_id >= FDC37C93X_FR) {
                            ret = 0x00;
                            for (uint8_t i = 0; i < 8; i++)
                                ret |= fdc37c93x_read_gp(dev, 5, i);
                        }
                        break;
                    case 0xfa:
                        if (dev->chip_id >= FDC37C93X_FR) {
                            ret = 0x00;
                            for (uint8_t i = 0; i < 8; i++)
                                ret |= fdc37c93x_read_gp(dev, 6, i);
                        }
                        break;
                    case 0xfb:
                        if (dev->chip_id >= FDC37C93X_FR) {
                            ret = 0x00;
                            for (uint8_t i = 0; i < 8; i++)
                                ret |= fdc37c93x_read_gp(dev, 7, i);
                        }
                        break;
                } else if ((dev->regs[7] != 0x06) || (dev->cur_reg != 0xf3))
                    ret = dev->ld_regs[dev->regs[7]][dev->cur_reg];
            }
        }
    }

    return ret;
}

static void
fdc37c93x_reset(void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;

    memset(dev->regs, 0x00, sizeof(dev->regs));

    dev->regs[0x03] = 0x03;
    dev->regs[0x20] = dev->chip_id;
    dev->regs[0x21] = 0x01;
    dev->regs[0x22] = 0x39;
    dev->regs[0x24] = 0x04;
    if (dev->chip_id >= FDC37C93X_FR)
        dev->regs[0x26] = dev->port_370 ? 0x70 : 0xf0;
    dev->regs[0x27] = 0x03;

    for (uint8_t i = 0; i <= 0x0a; i++)
        memset(dev->ld_regs[i], 0x00, 256);

    /* Logical device 0: FDD */
    dev->ld_regs[0x00][0x30] = 0x00;
    dev->ld_regs[0x00][0x60] = 0x03;
    dev->ld_regs[0x00][0x61] = 0xf0;
    dev->ld_regs[0x00][0x70] = 0x06;
    dev->ld_regs[0x00][0x74] = 0x02;
    dev->ld_regs[0x00][0xf0] = 0x0e;
    dev->ld_regs[0x00][0xf2] = 0xff;

    /* Logical device 1: IDE1 */
    dev->ld_regs[0x01][0x30] = 0x00;
    dev->ld_regs[0x01][0x60] = 0x01;
    dev->ld_regs[0x01][0x61] = 0xf0;
    dev->ld_regs[0x01][0x62] = 0x03;
    dev->ld_regs[0x01][0x63] = 0xf6;
    dev->ld_regs[0x01][0x70] = 0x0e;
    if (dev->chip_id >= FDC37C93X_FR)
        dev->ld_regs[0x01][0xf0] = 0x0c;

    /* Logical device 2: IDE2 */
    dev->ld_regs[0x02][0x30] = 0x00;
    dev->ld_regs[0x02][0x60] = 0x01;
    dev->ld_regs[0x02][0x61] = 0x70;
    dev->ld_regs[0x02][0x62] = 0x03;
    dev->ld_regs[0x02][0x63] = 0x76;
    dev->ld_regs[0x02][0x70] = 0x0f;

    /* Logical device 3: Parallel Port */
    dev->ld_regs[0x03][0x30] = 0x00;
    dev->ld_regs[0x03][0x60] = 0x03;
    dev->ld_regs[0x03][0x61] = 0x78;
    dev->ld_regs[0x03][0x70] = 0x07;
    dev->ld_regs[0x03][0x74] = 0x04;
    dev->ld_regs[0x03][0xf0] = 0x3c;

    /* Logical device 4: Serial Port 1 */
    dev->ld_regs[0x04][0x30] = 0x00;
    dev->ld_regs[0x04][0x60] = 0x03;
    dev->ld_regs[0x04][0x61] = 0xf8;
    dev->ld_regs[0x04][0x70] = 0x04;
    dev->ld_regs[0x04][0xf0] = 0x03;
    serial_irq(dev->uart[0], dev->ld_regs[4][0x70]);

    /* Logical device 5: Serial Port 2 */
    dev->ld_regs[0x05][0x30] = 0x00;
    dev->ld_regs[0x05][0x60] = 0x02;
    dev->ld_regs[0x05][0x61] = 0xf8;
    dev->ld_regs[0x05][0x70] = 0x03;
    dev->ld_regs[0x05][0x74] = 0x04;
    dev->ld_regs[0x05][0xf1] = 0x02;
    if (dev->chip_id >= FDC37C93X_FR)
        dev->ld_regs[0x05][0xf2] = 0x03;
    serial_irq(dev->uart[1], dev->ld_regs[5][0x70]);

    /* Logical device 6: RTC */
    dev->ld_regs[0x06][0x30] = 0x00;
    dev->ld_regs[0x06][0x63] = (dev->has_nvr) ? 0x70 : 0x00;
    dev->ld_regs[0x06][0xf0] = 0x00;
    dev->ld_regs[0x06][0xf4] = 0x03;

    /* Logical device 7: Keyboard */
    dev->ld_regs[0x07][0x30] = 0x00;
    dev->ld_regs[0x07][0x61] = 0x60;
    dev->ld_regs[0x07][0x70] = 0x01;
    dev->ld_regs[0x07][0x72] = 0x0c;

    /* Logical device 8: Auxiliary I/O */
    dev->ld_regs[0x08][0x30] = 0x00;
    dev->ld_regs[0x08][0x60] = 0x00;
    dev->ld_regs[0x08][0x61] = 0x00;
    if (dev->chip_id >= FDC37C93X_FR) {
        dev->ld_regs[0x08][0xb1] = 0x80;
        dev->ld_regs[0x08][0xc0] = 0x01;
        dev->ld_regs[0x08][0xc1] = 0x01;
        dev->ld_regs[0x08][0xc5] = 0x01;
        dev->ld_regs[0x08][0xc6] = 0x01;
        dev->ld_regs[0x08][0xc7] = 0x01;
        dev->ld_regs[0x08][0xc8] = 0x01;
        dev->ld_regs[0x08][0xc9] = 0x80;
        dev->ld_regs[0x08][0xcb] = 0x01;
        dev->ld_regs[0x08][0xcc] = 0x01;
        memset(&(dev->ld_regs[0x08][0xd0]), 0x01, 16);
    }
    memset(&(dev->ld_regs[0x08][0xe0]), 0x01, 14);

    /* Logical device 9: ACCESS.bus */
    if (dev->chip_id >= FDC37C93X_FR) {
        dev->ld_regs[0x09][0x30] = 0x00;
        dev->ld_regs[0x09][0x60] = 0x00;
        dev->ld_regs[0x09][0x61] = 0x00;
    }

    /* Logical device A: ACPI */
    if (dev->chip_id == FDC37C93X_APM) {
        dev->ld_regs[0x0a][0x30] = 0x00;
        dev->ld_regs[0x0a][0x60] = 0x00;
        dev->ld_regs[0x0a][0x61] = 0x00;
    }

    fdc37c93x_gpio_handler(dev);
    fdc37c93x_lpt_handler(dev);
    fdc37c93x_serial_handler(dev, 0);
    fdc37c93x_serial_handler(dev, 1);
    fdc37c93x_auxio_handler(dev);
    if (dev->is_apm || (dev->chip_id == 0x03))
        fdc37c93x_access_bus_handler(dev);
    if (dev->is_apm)
        fdc37c93x_acpi_handler(dev);

    fdc_clear_flags(dev->fdc, FDC_FLAG_PS2 | FDC_FLAG_PS2_MCA);
    fdc_reset(dev->fdc);

    fdc37c93x_fdc_handler(dev);

    if (dev->has_nvr) {
        fdc37c93x_nvr_pri_handler(dev);
        fdc37c93x_nvr_sec_handler(dev);
        nvr_bank_set(0, 0, dev->nvr);
        nvr_bank_set(1, 0xff, dev->nvr);

        nvr_lock_set(0x80, 0x20, 0, dev->nvr);
        nvr_lock_set(0xa0, 0x20, 0, dev->nvr);
        nvr_lock_set(0xc0, 0x20, 0, dev->nvr);
        nvr_lock_set(0xe0, 0x20, 0, dev->nvr);
    }

    fdc37c93x_kbc_handler(dev);

    if (dev->chip_id != 0x02)
        fdc37c93x_superio_handler(dev);

    memset(dev->gpio_regs,   0xff, 256);
    memset(dev->gpio_pulldn, 0xff, 8);

    /* Acer V62X requires bit 0 to be clear to not be stuck in "clear password" mode. */
    if ((machines[machine].init == machine_at_vectra54_init) || (machines[machine].init == machine_at_vectra500mt_init)) {
        dev->gpio_pulldn[1] = 0x40;

        /*
           HP Vectra VL/5 Series 4 GPIO
           (TODO: Find how multipliers > 3.0 are defined):

           Bit 6: 1 = can boot, 0 = no;
           Bit 7, 1 = multiplier (00 = 2.5, 01 = 2.0,
                                  10 = 3.0, 11 = 1.5);
           Bit 5, 4 = bus speed (00 = 50 MHz, 01 = 66 MHz,
                                 10 = 60 MHz, 11 = ????):
           Bit 7, 5, 4, 1: 0000 = 125 MHz, 0010 = 166 MHz,
                           0100 = 150 MHz, 0110 = ??? MHz;
                           0001 = 100 MHz, 0011 = 133 MHz,
                           0101 = 120 MHz, 0111 = ??? MHz;
                           1000 = 150 MHz, 1010 = 200 MHz,
                           1100 = 180 MHz, 1110 = ??? MHz;
                           1001 =  75 MHz, 1011 = 100 MHz,
                           1101 =  90 MHz, 1111 = ??? MHz
         */
        if (cpu_busspeed <= 40000000)
            dev->gpio_pulldn[1] |= 0x30;
        else if ((cpu_busspeed > 40000000) && (cpu_busspeed <= 50000000))
            dev->gpio_pulldn[1] |= 0x00;
        else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
            dev->gpio_pulldn[1] |= 0x20;
        else if (cpu_busspeed > 60000000)
            dev->gpio_pulldn[1] |= 0x10;

        if (cpu_dmulti <= 1.5)
            dev->gpio_pulldn[1] |= 0x82;
        else if ((cpu_dmulti > 1.5) && (cpu_dmulti <= 2.0))
            dev->gpio_pulldn[1] |= 0x02;
        else if ((cpu_dmulti > 2.0) && (cpu_dmulti <= 2.5))
            dev->gpio_pulldn[1] |= 0x00;
        else if (cpu_dmulti > 2.5)
            dev->gpio_pulldn[1] |= 0x80;
    } else if (machines[machine].init == machine_at_acerv62x_init)
        dev->gpio_pulldn[1] = 0xfe;
    else
        dev->gpio_pulldn[1] = (dev->chip_id == 0x30) ? 0xff : 0xfd;

    if (!strncmp(machine_get_internal_name(), "acer", 4))
        /* Bit 2 on the Acer V35N is the text/graphics toggle, bits 1 and 3 = ????. */
        dev->gpio_pulldn[2] = 0x10;

    dev->locked = 0;
}

static void
fdc37c93x_close(void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;

    free(dev);
}

static void *
fdc37c93x_init(const device_t *info)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) calloc(1, sizeof(fdc37c93x_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0]   = device_add_inst(&ns16550_device, 1);
    dev->uart[1]   = device_add_inst(&ns16550_device, 2);

    dev->lpt       = device_add_inst(&lpt_port_device, 1);

    dev->chip_id   = info->local & FDC37C93X_CHIP_ID;
    dev->kbc_type  = info->local & FDC37XXXX_KBC;

    dev->is_apm    = (dev->chip_id == FDC37C93X_APM);
    dev->is_compaq = (dev->kbc_type == FDC37XXX1);

    dev->has_nvr   = !(info->local & FDC37C93X_NO_NVR);
    dev->port_370  = !!(info->local & FDC37XXXX_370);

    if (dev->has_nvr) {
        dev->nvr = device_add_params(&nvr_at_device, (void *) (uintptr_t) NVR_AT_ZERO_DEFAULT);

        nvr_bank_set(0, 0, dev->nvr);
        nvr_bank_set(1, 0xff, dev->nvr);
    }

    dev->max_ld = 8;

    if (dev->chip_id >= FDC37C93X_FR) {
        dev->access_bus = device_add(&access_bus_device);
        dev->max_ld++;
    }

    if (dev->chip_id == FDC37C93X_APM) {
        dev->acpi = device_add(&acpi_smc_device);
        dev->max_ld++;
    }

    if (dev->is_compaq) {
        io_sethandler(0x0f9, 0x0001,
                      fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL, dev);
        io_sethandler(0x0fb, 0x0001,
                      fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL, dev);
    }

    switch (dev->kbc_type) {
        case FDC37XXX1:
            dev->kbc = device_add_params(&kbc_at_device, (void *) KBC_VEN_COMPAQ);
            break;
        case FDC37XXX2:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_AMI | 0x00003500));
            break;
        case FDC37XXX3:
        default:
            dev->kbc = device_add(&kbc_at_device);
            break;
        case FDC37XXX5:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_PHOENIX | 0x00013800));
            break;
        case FDC37XXX7:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_PHOENIX | 0x00041600));
            break;
    }

    /* Set the defaults here so the ports can be removed by fdc37c93x_reset(). */
    dev->fdc_base     = 0x03f0;
    dev->lpt_base     = 0x0378;
    dev->uart_base[0] = 0x03f8;
    dev->uart_base[1] = 0x02f8;
    dev->nvr_pri_base = 0x0070;
    dev->nvr_sec_base = 0x0070;
    dev->kbc_base     = 0x0060;
    dev->gpio_base    = 0x00ea;

    fdc37c93x_reset(dev);

    if (dev->chip_id == 0x02) {
        io_sethandler(0x03f0, 0x0002,
                      fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL, dev);
        io_sethandler(0x0370, 0x0002,
                      fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL, dev);
    }

    return dev;
}

const device_t fdc37c93x_device = {
    .name          = "SMC FDC37C93x Super I/O",
    .internal_name = "fdc37c93x",
    .flags         = 0,
    .local         = 0,
    .init          = fdc37c93x_init,
    .close         = fdc37c93x_close,
    .reset         = fdc37c93x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
