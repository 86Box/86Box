/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SMC FDC37C932FR and FDC37C935 Super
 *          I/O Chips.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/pci.h>
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
#include <86box/sio.h>
#include <86box/plat_unused.h>

typedef struct fdc37c93x_t {
    uint8_t       chip_id;
    uint8_t       is_apm;
    uint8_t       is_compaq;
    uint8_t       has_nvr;
    uint8_t       tries;
    uint8_t       port_370;
    uint8_t       gpio_regs[2];
    uint8_t       auxio_reg;
    uint8_t       regs[48];
    uint8_t       ld_regs[11][256];
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
} fdc37c93x_t;

static void    fdc37c93x_write(uint16_t port, uint8_t val, void *priv);
static uint8_t fdc37c93x_read(uint16_t port, void *priv);

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

static uint8_t
fdc37c93x_gpio_read(uint16_t port, void *priv)
{
    const fdc37c93x_t *dev = (fdc37c93x_t *) priv;
    uint8_t            ret = 0xff;

    if (strcmp(machine_get_internal_name(), "vectra54"))
        ret = dev->gpio_regs[port & 1];

    return ret;
}

static void
fdc37c93x_gpio_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;

    if (!(port & 1))
        dev->gpio_regs[0] = (dev->gpio_regs[0] & 0xfc) | (val & 0x03);
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
    const uint8_t  global_enable = !!(dev->regs[0x22] & (1 << 3));
    const uint8_t  local_enable  = !!dev->ld_regs[3][0x30];
    uint8_t        lpt_irq       = dev->ld_regs[3][0x70];
    const uint16_t old_base      = dev->lpt_base;

    if (lpt_irq > 15)
        lpt_irq = 0xff;

    dev->lpt_base = 0x0000;

    if (global_enable && local_enable)
        dev->lpt_base = make_port(dev, 3) & 0xfffc;

    if (dev->lpt_base != old_base) {
        if ((old_base >= 0x0100) && (old_base <= 0x0ffc))
            lpt1_remove();

        if ((dev->lpt_base >= 0x0100) && (dev->lpt_base <= 0x0ffc))
            lpt1_setup(dev->lpt_base);
    }

    lpt1_irq(lpt_irq);
}

static void
fdc37c93x_serial_handler(fdc37c93x_t *dev, const int uart)
{
    const uint8_t  uart_no       = 4 + uart;
    const uint8_t  global_enable = !!(dev->regs[0x22] & (1 << uart_no));
    const uint8_t  local_enable  = !!dev->ld_regs[uart_no][0x30];
    const uint16_t old_base      = dev->uart_base[uart];

    dev->uart_base[uart] = 0x0000;

    if (global_enable && local_enable)
        dev->uart_base[uart] = make_port(dev, uart_no) & 0xfff8;

    if (dev->uart_base[uart] != old_base) {
        if ((old_base >= 0x0100) && (old_base <= 0x0ff8))
            serial_remove(dev->uart[uart]);

        if ((dev->uart_base[uart] >= 0x0100) && (dev->uart_base[uart] <= 0x0ff8))
            serial_setup(dev->uart[uart], dev->uart_base[uart], dev->ld_regs[uart_no][0x70]);
    }

    serial_irq(dev->uart[uart], dev->ld_regs[uart_no][0x70]);
}

static void
fdc37c93x_nvr_pri_handler(const fdc37c93x_t *dev)
{
    uint8_t  local_enable = !!dev->ld_regs[6][0x30];

    if (dev->chip_id != 0x02)
        local_enable &= ((dev->ld_regs[6][0xf0] & 0x90) != 0x80);

    nvr_at_handler(0, 0x70, dev->nvr);
    if (local_enable)
        nvr_at_handler(1, 0x70, dev->nvr);
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
        if ((old_base > 0x0000) && (old_base <= 0x0ffe))
            nvr_at_sec_handler(0, dev->nvr_sec_base, dev->nvr);

        /* Datasheet erratum: First it says minimum address is 0x0100, but later implies that it's 0x0000
                              and that default is 0x0070, same as (unrelocatable) primary NVR. */
        if ((dev->nvr_sec_base > 0x0000) && (dev->nvr_sec_base <= 0x0ffe))
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
        kbc_at_handler(local_enable, dev->kbc);
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
    const uint8_t local_enable = !dev->locked && !!(dev->regs[0x03] & 0x80);
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
    uint8_t      index  = (port & 1) ? 0 : 1;
    uint8_t      valxor;

    /* Compaq Presario 4500: Unlock at FB, Register at EA, Data at EB, Lock at F9. */
    if (port == 0xea)
        index = 1;
    else if (port == 0xeb)
        index = 0;

    if (port == 0xfb) {
        fdc37c93x_state_change(dev, 1);
        dev->tries = 0;
        return;
    } else if (port == 0xf9) {
        fdc37c93x_state_change(dev, 0);
        return;
    } else if (index) {
        if ((!dev->is_compaq) && (val == 0x55) && !dev->locked) {
            if (dev->tries) {
                fdc37c93x_state_change(dev, 1);
                dev->tries = 0;
            } else
                dev->tries++;
        } else if (dev->locked) {
            if ((!dev->is_compaq) && (val == 0xaa)) {
                fdc37c93x_state_change(dev, 0);
                return;
            }
            dev->cur_reg = val;
        } else if ((!dev->is_compaq) && dev->tries)
            dev->tries = 0;
        return;
    } else {
        if (dev->locked) {
            if (dev->cur_reg < 48) {
                valxor = val ^ dev->regs[dev->cur_reg];
                if ((val == 0x20) || (val == 0x21))
                    return;
                dev->regs[dev->cur_reg] = val;
            } else {
                uint8_t keep = 0x00;

                valxor = val ^ dev->ld_regs[dev->regs[7]][dev->cur_reg];
                if (((dev->cur_reg & 0xF0) == 0x70) && (dev->regs[7] < 4))
                    return;
                /* Block writes to some logical devices. */
                if (dev->regs[7] > 0x0a)
                    return;
                else
                    switch (dev->regs[7]) {
                        // case 0x01:
                        // case 0x02:
                            // return;
                        case 0x06:
                            if (!dev->has_nvr)
                                return;
                            /* Bits 0 to 3 of logical device 6 (RTC) register F0h must stay set
                               once they are set. */
                            else if (dev->cur_reg == 0xf0)
                                keep = dev->ld_regs[dev->regs[7]][dev->cur_reg] & 0x0f;
                            break;
                        case 0x09:
                            /* If we're on the FDC37C935, return as this is not a valid
                               logical device there. */
                            if (!dev->is_apm && (dev->chip_id == 0x02))
                                return;
                            break;
                        case 0x0a:
                            /* If we're not on the FDC37C931APM, return as this is not a
                               valid logical device there. */
                            if (!dev->is_apm)
                                return;
                            break;

                        default:
                            break;
                    }
                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val | keep;
            }
        } else
            return;
    }

    if (dev->cur_reg < 48) {
        switch (dev->cur_reg) {
            case 0x02:
                if (val == 0x02)
                    fdc37c93x_state_change(dev, 0);
                break;
            case 0x03:
                dev->regs[0x03] &= 0x83;
                break;
            case 0x22:
                if (valxor & 0x01)
                    fdc37c93x_fdc_handler(dev);
                if (valxor & 0x08)
                    fdc37c93x_lpt_handler(dev);
                if (valxor & 0x10)
                    fdc37c93x_serial_handler(dev, 0);
                if (valxor & 0x20)
                    fdc37c93x_serial_handler(dev, 1);
                if ((valxor & 0x40) && (dev->chip_id != 0x02))
                    fdc37c93x_access_bus_handler(dev);
                break;

            case 0x27:
                if (dev->chip_id != 0x02)
                    fdc37c93x_superio_handler(dev);
                break;

            default:
                break;
        }

        return;
    }

    switch (dev->regs[7]) {
        case 0:
            /* FDD */
            switch (dev->cur_reg) {
                case 0x30:
                case 0x60:
                case 0x61:
                    if ((dev->cur_reg == 0x30) && (val & 0x01))
                        dev->regs[0x22] |= 0x01;
                    if (valxor)
                        fdc37c93x_fdc_handler(dev);
                    break;
                case 0xF0:
                    if (valxor & 0x01)
                        fdc_update_enh_mode(dev->fdc, val & 0x01);
                    if (valxor & 0x10)
                        fdc_set_swap(dev->fdc, (val & 0x10) >> 4);
                    break;
                case 0xF1:
                    if (valxor & 0xC)
                        fdc_update_densel_force(dev->fdc, (val & 0xc) >> 2);
                    break;
                case 0xF2:
                    if (valxor & 0xC0)
                        fdc_update_rwc(dev->fdc, 3, (val & 0xc0) >> 6);
                    if (valxor & 0x30)
                        fdc_update_rwc(dev->fdc, 2, (val & 0x30) >> 4);
                    if (valxor & 0x0C)
                        fdc_update_rwc(dev->fdc, 1, (val & 0x0c) >> 2);
                    if (valxor & 0x03)
                        fdc_update_rwc(dev->fdc, 0, (val & 0x03));
                    break;
                case 0xF4:
                    if (valxor & 0x18)
                        fdc_update_drvrate(dev->fdc, 0, (val & 0x18) >> 3);
                    break;
                case 0xF5:
                    if (valxor & 0x18)
                        fdc_update_drvrate(dev->fdc, 1, (val & 0x18) >> 3);
                    break;
                case 0xF6:
                    if (valxor & 0x18)
                        fdc_update_drvrate(dev->fdc, 2, (val & 0x18) >> 3);
                    break;
                case 0xF7:
                    if (valxor & 0x18)
                        fdc_update_drvrate(dev->fdc, 3, (val & 0x18) >> 3);
                    break;

                default:
                    break;
            }
            break;
        case 3:
            /* Parallel port */
            switch (dev->cur_reg) {
                case 0x30:
                case 0x60:
                case 0x61:
                case 0x70:
                    if ((dev->cur_reg == 0x30) && (val & 0x01))
                        dev->regs[0x22] |= 0x08;
                    if (valxor)
                        fdc37c93x_lpt_handler(dev);
                    break;

                default:
                    break;
            }
            break;
        case 4:
            /* Serial port 1 */
            switch (dev->cur_reg) {
                case 0x30:
                case 0x60:
                case 0x61:
                case 0x70:
                    if ((dev->cur_reg == 0x30) && (val & 0x01))
                        dev->regs[0x22] |= 0x10;
                    if (valxor)
                        fdc37c93x_serial_handler(dev, 0);
                    break;

                default:
                    break;
            }
            break;
        case 5:
            /* Serial port 2 */
            switch (dev->cur_reg) {
                case 0x30:
                case 0x60:
                case 0x61:
                case 0x70:
                    if ((dev->cur_reg == 0x30) && (val & 0x01))
                        dev->regs[0x22] |= 0x20;
                    if (valxor)
                        fdc37c93x_serial_handler(dev, 1);
                    break;

                default:
                    break;
            }
            break;
        case 6:
            /* RTC/NVR */
            if (!dev->has_nvr)
                return;
            switch (dev->cur_reg) {
                case 0x30:
                    if (valxor) {
                        fdc37c93x_nvr_pri_handler(dev);
                        if (dev->chip_id != 0x02)
                            fdc37c93x_nvr_sec_handler(dev);
                    }
                    break;
                case 0x62:
                case 0x63:
                    if ((dev->chip_id != 0x02) && valxor)
                        fdc37c93x_nvr_sec_handler(dev);
                    break;
                case 0xf0:
                    if (valxor) {
                        nvr_lock_set(0x80, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x01), dev->nvr);
                        nvr_lock_set(0xa0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x02), dev->nvr);
                        nvr_lock_set(0xc0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x04), dev->nvr);
                        nvr_lock_set(0xe0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x08), dev->nvr);
                        if ((dev->chip_id == 0x02) && (dev->ld_regs[6][dev->cur_reg] & 0x80))
                            nvr_bank_set(0, 1, dev->nvr);
                        else if ((dev->chip_id != 0x02) && (dev->ld_regs[6][dev->cur_reg] & 0x80))
                            switch ((dev->ld_regs[6][dev->cur_reg] >> 4) & 0x07) {
                                default:
                                case 0x00:
                                    nvr_bank_set(0, 0xff, dev->nvr);
                                    nvr_bank_set(1, 1, dev->nvr);
                                    break;
                                case 0x01:
                                    nvr_bank_set(0, 0, dev->nvr);
                                    nvr_bank_set(1, 1, dev->nvr);
                                    break;
                                case 0x02:
                                case 0x04:
                                    nvr_bank_set(0, 0xff, dev->nvr);
                                    nvr_bank_set(1, 0xff, dev->nvr);
                                    break;
                                case 0x03:
                                case 0x05:
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
                        else {
                            nvr_bank_set(0, 0, dev->nvr);
                            if (dev->chip_id != 0x02)
                                nvr_bank_set(1, 0xff, dev->nvr);
                        }

                        fdc37c93x_nvr_pri_handler(dev);
                        if (dev->chip_id != 0x02)
                            fdc37c93x_nvr_sec_handler(dev);
                    }
                    break;

                default:
                    break;
            }
            break;
        case 7:
            /* Keyboard */
            switch (dev->cur_reg) {
                case 0x30:
                    if (valxor)
                        fdc37c93x_kbc_handler(dev);
                    break;

                default:
                    break;
            }
            break;
        case 8:
            /* Auxiliary I/O */
            switch (dev->cur_reg) {
                case 0x30:
                case 0x60:
                case 0x61:
                case 0x70:
                    if (valxor)
                        fdc37c93x_auxio_handler(dev);
                    break;

                default:
                    break;
            }
            break;
        case 9:
            /* Access bus (FDC37C932FR and FDC37C931APM only) */
            switch (dev->cur_reg) {
                case 0x30:
                case 0x60:
                case 0x61:
                case 0x70:
                    if ((dev->cur_reg == 0x30) && (val & 0x01))
                        dev->regs[0x22] |= 0x40;
                    if (valxor)
                        fdc37c93x_access_bus_handler(dev);
                    break;

                default:
                    break;
            }
            break;
        case 10:
            /* Access bus (FDC37C931APM only) */
            switch (dev->cur_reg) {
                case 0x30:
                case 0x60:
                case 0x61:
                case 0x62:
                case 0x63:
                case 0x70:
                    if (valxor)
                        fdc37c93x_acpi_handler(dev);
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
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
            } else {
                if ((dev->regs[7] == 0) && (dev->cur_reg == 0xF2)) {
                    ret = (fdc_get_rwc(dev->fdc, 0) | (fdc_get_rwc(dev->fdc, 1) << 2) | (fdc_get_rwc(dev->fdc, 2) << 4) | (fdc_get_rwc(dev->fdc, 3) << 6));
                } else
                    ret = dev->ld_regs[dev->regs[7]][dev->cur_reg];
            }
        }
    }

    return ret;
}

static void
fdc37c93x_reset(fdc37c93x_t *dev)
{
    memset(dev->regs, 0x00, sizeof(dev->regs));

    dev->regs[0x03] = 0x03;
    dev->regs[0x20] = dev->chip_id;
    dev->regs[0x21] = 0x01;
    dev->regs[0x22] = 0x39;
    dev->regs[0x24] = 0x04;
    dev->regs[0x26] = dev->port_370 ? 0x70 : 0xf0;
    dev->regs[0x27] = 0x03;

    memset(dev->ld_regs, 0x00, sizeof(dev->ld_regs));

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

    /* Logical device 8: Auxiliary I/O */
    dev->ld_regs[0x08][0x30] = 0x00;
    dev->ld_regs[0x08][0x60] = 0x00;
    dev->ld_regs[0x08][0x61] = 0x00;

    /* Logical device 9: ACCESS.bus */
    dev->ld_regs[0x09][0x30] = 0x00;
    dev->ld_regs[0x09][0x60] = 0x00;
    dev->ld_regs[0x09][0x61] = 0x00;

    /* Logical device A: ACPI */
    dev->ld_regs[0x0a][0x30] = 0x00;
    dev->ld_regs[0x0a][0x60] = 0x00;
    dev->ld_regs[0x0a][0x61] = 0x00;

    fdc37c93x_gpio_handler(dev);
    fdc37c93x_lpt_handler(dev);
    fdc37c93x_serial_handler(dev, 0);
    fdc37c93x_serial_handler(dev, 1);
    fdc37c93x_auxio_handler(dev);
    if (dev->is_apm || (dev->chip_id == 0x03))
        fdc37c93x_access_bus_handler(dev);
    if (dev->is_apm)
        fdc37c93x_acpi_handler(dev);

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

    dev->chip_id   = info->local & 0xff;
    dev->is_apm    = (info->local >> 8) & 0x01;
    dev->is_compaq = (info->local >> 8) & 0x02;
    dev->has_nvr   = !((info->local >> 8) & 0x04);
    dev->port_370  = ((info->local >> 8) & 0x08);

    dev->gpio_regs[0] = 0xff;
#if 0
    dev->gpio_regs[1] = (info->local == 0x0030) ? 0xff : 0xfd;
#endif
    dev->gpio_regs[1] = (dev->chip_id == 0x30) ? 0xff : 0xfd;

    if (dev->has_nvr) {
        dev->nvr = device_add(&at_nvr_device);

        nvr_bank_set(0, 0, dev->nvr);
        nvr_bank_set(1, 0xff, dev->nvr);
    }

    if (dev->is_apm || (dev->chip_id == 0x03))
        dev->access_bus = device_add(&access_bus_device);

    if (dev->is_apm)
        dev->acpi = device_add(&acpi_smc_device);

    if (dev->is_compaq) {
        io_sethandler(0x0ea, 0x0002,
                      fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL, dev);
        io_sethandler(0x0f9, 0x0001,
                      fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL, dev);
        io_sethandler(0x0fb, 0x0001,
                      fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL, dev);
    }

    dev->kbc = device_add(&keyboard_ps2_ami_pci_device);

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

const device_t fdc37c931apm_device = {
    .name          = "SMC FDC37C931APM Super I/O",
    .internal_name = "fdc37c931apm",
    .flags         = 0,
    .local         = 0x130, /* Share the same ID with the 932QF. */
    .init          = fdc37c93x_init,
    .close         = fdc37c93x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c931apm_compaq_device = {
    .name          = "SMC FDC37C931APM Super I/O (Compaq Presario 4500)",
    .internal_name = "fdc37c931apm_compaq",
    .flags         = 0,
    .local         = 0x330, /* Share the same ID with the 932QF. */
    .init          = fdc37c93x_init,
    .close         = fdc37c93x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c932_device = {
    .name          = "SMC FDC37C932 Super I/O",
    .internal_name = "fdc37c932",
    .flags         = 0,
    .local         = 0x02,
    .init          = fdc37c93x_init,
    .close         = fdc37c93x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c932fr_device = {
    .name          = "SMC FDC37C932FR Super I/O",
    .internal_name = "fdc37c932fr",
    .flags         = 0,
    .local         = 0x03,
    .init          = fdc37c93x_init,
    .close         = fdc37c93x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c932qf_device = {
    .name          = "SMC FDC37C932QF Super I/O",
    .internal_name = "fdc37c932qf",
    .flags         = 0,
    .local         = 0x30,
    .init          = fdc37c93x_init,
    .close         = fdc37c93x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c935_device = {
    .name          = "SMC FDC37C935 Super I/O",
    .internal_name = "fdc37c935",
    .flags         = 0,
    .local         = 0x02,
    .init          = fdc37c93x_init,
    .close         = fdc37c93x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c935_370_device = {
    .name          = "SMC FDC37C935 Super I/O (Port 370h)",
    .internal_name = "fdc37c935_370",
    .flags         = 0,
    .local         = 0x802,
    .init          = fdc37c93x_init,
    .close         = fdc37c93x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c935_no_nvr_device = {
    .name          = "SMC FDC37C935 Super I/O",
    .internal_name = "fdc37c935",
    .flags         = 0,
    .local         = 0x402,
    .init          = fdc37c93x_init,
    .close         = fdc37c93x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
