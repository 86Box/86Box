/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Chips & Technologies F82C710 Universal
 *          Peripheral Controller (UPC).
 *
 * Relevant literature:
 *
 *          [1] Chips and Technologies, Inc.,
 *              82710 Univeral Peripheral Controller, Data Sheet,
 *              PRELIMINARY, August 1990.
 *              <http://66.113.161.23/~mR_Slug/pub/datasheets/chipsets/CandT/82C710.pdf>
 *
 * Authors: Eluan Costa Miranda, <eluancm@gmail.com>
 *          Lubomir Rintel, <lkundrak@v3.sk>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020-2025 Eluan Costa Miranda.
 *          Copyright 2021-2025 Lubomir Rintel.
 *          Copyright 2025 Miran Grca.
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
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/gameport.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/machine.h>
#include <86box/mouse.h>
#include <86box/plat_fallthrough.h>
#include <86box/sio.h>
#include "cpu.h"

typedef struct upc_t {
    int      configuration_state;    /* State of algorithm to enter the
                                        configuration mode. */
    int      configuration_mode;
    uint8_t  next_value;
    uint16_t cri_addr;               /* CRI = Configuration Index Register,
                                              addr is even. */
    uint16_t cap_addr;               /* CAP = Configuration Access Port, addr is
                                              odd and is cri_addr + 1. */
    uint8_t  cri;                    /* Currently indexed register. */
    uint8_t  last_write;
    uint16_t mouse_base;

    /* These regs are not affected by reset */
    uint8_t   regs[15];              /* There are 16 indexes, but there is no
                                        need to store the last one which is:
                                        R = cri_addr / 4, W = exit config mode. */
    int       serial_irq;
    int       lpt_irq;
    int       xta;
    fdc_t    *fdc;
    void     *gameport;
    void     *mouse;
    void     *hdc_xta;
    serial_t *uart;
    lpt_t    *lpt;
} upc_t;

#ifdef ENABLE_F82C710_LOG
int f82c710_do_log = ENABLE_F82C710_LOG;

static void
f82c710_log(const char *fmt, ...)
{
    va_list ap;

    if (f82c710_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define f82c710_log(fmt, ...)
#endif

static void
serial_handler(upc_t *dev)
{
    uint16_t com_addr = 0x0000;

    serial_remove(dev->uart);

    if (dev->regs[0x00] & 0x04) {
        com_addr = dev->regs[0x04] << 2;

        serial_setup(dev->uart, com_addr, dev->serial_irq);
    }
}

static void
lpt_handler(upc_t *dev)
{
    uint16_t lpt_addr = 0x0000;

    lpt_port_remove(dev->lpt);

    if (dev->regs[0x00] & 0x08) {
        lpt_addr = dev->regs[0x06] << 2;

        lpt_port_setup(dev->lpt, lpt_addr);
        lpt_port_irq(dev->lpt, dev->lpt_irq);

        lpt_set_ext(dev->lpt, !!(dev->regs[0x01] & 0x40));
    }
}

static void
ide_handler(upc_t *dev)
{
    if (dev->xta) {
        if (dev->hdc_xta != NULL)
            xta_handler(dev->hdc_xta, 0);
    } else
        ide_pri_disable();

    if (dev->regs[0x0c] & 0x80) {
        if (dev->regs[0x0c] & 0x40) {
            if (dev->xta && (dev->hdc_xta != NULL))
                xta_handler(dev->hdc_xta, 1);
        } else {
            if (!dev->xta)
                ide_pri_enable();
        }
    }
}

static void
fdc_handler(upc_t *dev)
{
    fdc_remove(dev->fdc);

    if (dev->regs[0x0c] & 0x20)
        fdc_set_base(dev->fdc, FDC_PRIMARY_ADDR);

    fdc_set_power_down(dev->fdc, !!(dev->regs[0x0c] & 0x10));
}

static void
mouse_handler(upc_t *dev)
{
    if (dev->mouse_base != 0x0000)
        mouse_upc_handler(0, dev->mouse_base, dev->mouse);

    dev->mouse_base = dev->regs[0x0d] << 2;

    if (dev->mouse_base != 0x0000)
        mouse_upc_handler(1, dev->mouse_base, dev->mouse);
}

static void
f82c710_update_ports(upc_t *dev)
{
    serial_handler(dev);
    lpt_handler(dev);
    ide_handler(dev);
    fdc_handler(dev);
}

static uint8_t
f82c710_config_read(uint16_t port, void *priv)
{
    const upc_t  *dev  = (upc_t *) priv;
    uint8_t       temp = 0xff;

    if (dev->configuration_mode) {
        if (port == dev->cri_addr) {
            temp = dev->cri;
        } else if (port == dev->cap_addr) {
            if (dev->cri == 0x0f)
                temp = dev->cri_addr >> 2;
            else if (dev->cri < 0x0f)
                temp = dev->regs[dev->cri];
        }
    }

    return temp;
}

static void
f82c710_config_write(uint16_t port, uint8_t val, void *priv)
{
    upc_t * dev                       = (upc_t *) priv;
    uint8_t valxor                    = 0x00;
    int     configuration_state_event = 0;

    switch (port) {
        default:
            break;
        case 0x2fa:
            if (dev->configuration_state == 0) {
                configuration_state_event = 1;
                dev->next_value = 0xff - val;
            } else if (dev->configuration_state == 4) {
                uint8_t addr_verify = dev->cri_addr >> 2;
                addr_verify += val;
                if (addr_verify == 0xff) {
                    dev->configuration_mode = 1;
                    /* TODO: is the value of cri reset here or when exiting configuration mode? */
                    io_sethandler(dev->cri_addr, 0x0002,
                                  f82c710_config_read, NULL, NULL,
                                  f82c710_config_write, NULL, NULL, dev);
                } else
                    dev->configuration_mode = 0;
            }
            break;
        case 0x3fa:
            if ((dev->configuration_state == 1) && (val == dev->next_value))
                configuration_state_event = 1;
            else if ((dev->configuration_state == 2) && (val == 0x36))
                configuration_state_event = 1;
            else if (dev->configuration_state == 3) {
                dev->cri_addr = val << 2;
                dev->cap_addr = dev->cri_addr + 1;
                configuration_state_event = 1;
            }
            break;
    }

    if (dev->configuration_mode) {
        if (port == dev->cri_addr)
            dev->cri = val & 0xf;
        else if (port == dev->cap_addr) {
            valxor = (dev->regs[dev->cri] ^ val);
            switch (dev->cri) {
                case 0x00:
                    dev->regs[dev->cri] = (dev->regs[dev->cri] & 0x10) | (val & 0xef);
                    if (valxor & 0x08)
                        lpt_handler(dev);
                    if (valxor & 0x04)
                        serial_handler(dev);
                    break;
                case 0x01:
                    dev->regs[dev->cri] = (dev->regs[dev->cri] & 0x07) | (val & 0xf8);
                    if (valxor & 0x40)
                        serial_handler(dev);
                    break;
                case 0x02:
                    dev->regs[dev->cri] = (dev->regs[dev->cri] & 0x08) | (val & 0xf0);
                    break;
                case 0x03:
                case 0x07: case 0x08:
                    /* TODO: Reserved - is it actually writable? */
                    fallthrough;
                case 0x09: case 0x0a:
                case 0x0b:
                    dev->regs[dev->cri] = val;
                    break;
                case 0x04:
                    dev->regs[dev->cri] = (dev->regs[dev->cri] & 0x01) | (val & 0xfe);
                    if (valxor & 0xfe)
                        serial_handler(dev);
                    break;
                case 0x06:
                    dev->regs[dev->cri] = val;
                    if (valxor)
                        lpt_handler(dev);
                    break;
                case 0x0c:
                    dev->regs[dev->cri] = val;
                    if (valxor & 0xc0)
                        ide_handler(dev);
                    if (valxor & 0x30)
                        fdc_handler(dev);
                    break;
                case 0x0d:
                    dev->regs[dev->cri] = val;
                    if (valxor)
                        mouse_handler(dev);
                    break;
                case 0x0e:
                    dev->regs[dev->cri] = (dev->regs[dev->cri] & 0x20) | (val & 0xdf);
                    if (valxor)
                        mouse_handler(dev);
                    break;
                case 0x0f:
                    dev->configuration_mode = 0;
                    io_removehandler(dev->cri_addr, 0x0002,
                                     f82c710_config_read, NULL, NULL,
                                     f82c710_config_write, NULL, NULL, dev);
                    break;
            }
        }
    }

    /* TODO: is the state only reset when accessing 0x2fa and 0x3fa wrongly? */
    if (((port == 0x2fa) || (port == 0x3fa)) && configuration_state_event)
        dev->configuration_state++;
    else
        dev->configuration_state = 0;
}

static void
f82c710_reset(void *priv)
{
    upc_t *dev = (upc_t *) priv;

    dev->configuration_state = 0;
    dev->configuration_mode  = 0;

    /* Set power-on defaults. */
    dev->regs[0x00] = 0x0c;
    dev->regs[0x01] = 0x00;
    dev->regs[0x02] = 0x00;
    dev->regs[0x03] = 0x00;
    dev->regs[0x04] = 0xfe;
    dev->regs[0x05] = 0x00;
    dev->regs[0x06] = 0x9e;
    dev->regs[0x07] = 0x00;
    dev->regs[0x08] = 0x00;
    dev->regs[0x09] = 0xb0;
    dev->regs[0x0a] = 0x00;
    dev->regs[0x0b] = 0x00;
    dev->regs[0x0c] = 0xa0;
    dev->regs[0x0d] = 0x00;
    dev->regs[0x0e] = 0x00;

    f82c710_update_ports(dev);
}

static void
f82c710_close(void *priv)
{
    upc_t *dev = (upc_t *) priv;

    free(dev);
}

static void *
f82c710_init(const device_t *info)
{
    upc_t *dev = (upc_t *) calloc(1, sizeof(upc_t));

    if (machines[machine].init == machine_xt_pc5086_init)
        dev->fdc        = device_add(&fdc_at_actlow_device);
    else
        dev->fdc        = device_add(&fdc_at_device);

    dev->uart       = device_add_inst(&ns16450_device, 1);
    dev->lpt        = device_add_inst(&lpt_port_device, 1);

    dev->mouse      = device_add_params(&mouse_upc_device, (void *) (uintptr_t) (is286 ? 12 : 2));

    dev->serial_irq = device_get_config_int("serial_irq");
    dev->lpt_irq    = device_get_config_int("lpt_irq");

    io_sethandler(0x02fa, 0x0001, NULL, NULL, NULL, f82c710_config_write, NULL, NULL, dev);
    io_sethandler(0x03fa, 0x0001, NULL, NULL, NULL, f82c710_config_write, NULL, NULL, dev);

    f82c710_reset(dev);

    return dev;
}

static void *
f82c710_pc5086_init(const device_t *info)
{
    upc_t *dev = f82c710_init(info);

    int hdc_present = device_get_config_int("hdc_present");

    if (hdc_present)
       dev->hdc_xta = device_add(&xta_st50x_pc5086_device);

    dev->xta = 1;

    f82c710_reset(dev);

    return dev;
}

static const device_config_t f82c710_config[] = {
    {
        .name           = "serial_irq",
        .description    = "Serial port IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 4,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 4",  .value =  4 },
            { .description = "IRQ 3",  .value =  3 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "lpt_irq",
        .description    = "Parallel port IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 7,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 7",  .value =  7 },
            { .description = "IRQ 5",  .value =  5 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t f82c710_pc5086_config[] = {
    {
        .name           = "serial_irq",
        .description    = "Serial port IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 4,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 4",  .value =  4 },
            { .description = "IRQ 3",  .value =  3 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "lpt_irq",
        .description    = "Parallel port IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 7,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 7",  .value =  7 },
            { .description = "IRQ 5",  .value =  5 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "hdc_present",
        .description    = "Hard disk",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t f82c710_device = {
    .name          = "F82C710 UPC Super I/O",
    .internal_name = "f82c710",
    .flags         = 0,
    .local         = 0,
    .init          = f82c710_init,
    .close         = f82c710_close,
    .reset         = f82c710_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = f82c710_config
};

const device_t f82c710_pc5086_device = {
    .name          = "F82C710 UPC Super I/O (PC5086)",
    .internal_name = "f82c710_pc5086",
    .flags         = 0,
    .local         = 0,
    .init          = f82c710_pc5086_init,
    .close         = f82c710_close,
    .reset         = f82c710_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = f82c710_pc5086_config
};
