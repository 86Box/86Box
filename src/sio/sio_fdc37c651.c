/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 * Emulation of the SMC FDC37C651 Super I/O
 *
 * Authors:	Tiseno100
 * Copyright 2020 Tiseno100
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>

#ifdef ENABLE_FDC37C651_LOG
int fdc37c651_do_log = ENABLE_FDC37C651_LOG;
static void
fdc37c651_log(const char *fmt, ...)
{
    va_list ap;

    if (fdc37c651_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define fdc37c651_log(fmt, ...)
#endif

typedef struct
{
    uint8_t configuration_select, regs[3];
    uint16_t com3, com4;

    fdc_t *fdc_controller;
    serial_t *uart[2];

} fdc37c651_t;

static void
fdc37c651_write(uint16_t addr, uint8_t val, void *priv)
{
    fdc37c651_t *dev = (fdc37c651_t *)priv;

    switch (addr)
    {
    case 0x3f0:
        dev->configuration_select = val;
        break;
    case 0x3f1:
        switch (dev->configuration_select)
        {
        case 0: /* CR0 */
            dev->regs[dev->configuration_select] = val;
            ide_pri_disable();
            fdc_remove(dev->fdc_controller);

            if (val & 1) /* Enable IDE */
                ide_pri_enable();

            if (val & 0x10) /* Enable FDC */
                fdc_set_base(dev->fdc_controller, 0x3f0);

            break;

        case 1: /* CR1 */
            dev->regs[dev->configuration_select] = val;
            lpt1_remove();

            if ((val & 3) != 0) /* Program LPT if not Disabled */
                lpt1_init((val & 2) ? ((val & 1) ? 0x278 : 0x378) : 0x3f8);

            switch ((val >> 4) & 3) /* COM3 & 4 Select*/
            {
            case 0:
                dev->com3 = 0x338;
                dev->com4 = 0x238;
                break;
            case 1:
                dev->com3 = 0x3e8;
                dev->com4 = 0x2e8;
                break;
            case 2:
                dev->com3 = 0x2e8;
                dev->com4 = 0x2e0;
                break;
            case 3:
                dev->com3 = 0x220;
                dev->com4 = 0x228;
                break;
            }

            break;

        case 2: /* CR2 */
            dev->regs[dev->configuration_select] = val;
            serial_remove(dev->uart[0]);
            serial_remove(dev->uart[1]);

            if (val & 4)
                serial_setup(dev->uart[0], (val & 2) ? ((val & 1) ? dev->com4 : dev->com3) : ((val & 1) ? 0x2f8 : 0x3f8), 4);

            if (val & 0x40)
                serial_setup(dev->uart[1], (val & 0x20) ? ((val & 0x10) ? dev->com4 : dev->com3) : ((val & 0x10) ? 0x2f8 : 0x3f8), 3);

            break;
        }
        break;
    }
}

static uint8_t
fdc37c651_read(uint16_t addr, void *priv)
{
    fdc37c651_t *dev = (fdc37c651_t *)priv;

    return dev->regs[dev->configuration_select];
}

static void
fdc37c651_close(void *priv)
{
    fdc37c651_t *dev = (fdc37c651_t *)priv;
    free(dev);
}

static void *
fdc37c651_init(const device_t *info)
{
    fdc37c651_t *dev = (fdc37c651_t *)malloc(sizeof(fdc37c651_t));
    memset(dev, 0, sizeof(fdc37c651_t));

    dev->fdc_controller = device_add(&fdc_at_smc_device);
    dev->uart[0] = device_add_inst(&ns16450_device, 1);
    dev->uart[1] = device_add_inst(&ns16450_device, 2);
    device_add(&ide_isa_device);

    /* Program Defaults */
    dev->regs[0] = 0x3f;
    dev->regs[1] = 0x9f;
    dev->regs[2] = 0xdc;
    ide_pri_disable();
    fdc_remove(dev->fdc_controller);
    lpt1_remove();
    serial_remove(dev->uart[0]);
    serial_remove(dev->uart[1]);

    ide_pri_enable();
    fdc_set_base(dev->fdc_controller, 0x3f0);
    lpt1_init(0x278);
    serial_setup(dev->uart[0], 0x2f8, 4);
    serial_setup(dev->uart[1], 0x3f8, 3);

    io_sethandler(0x03f0, 2, fdc37c651_read, NULL, NULL, fdc37c651_write, NULL, NULL, dev);

    return dev;
}

const device_t fdc37c651_device = {
    "SMC FDC37C651",
    0,
    0,
    fdc37c651_init,
    fdc37c651_close,
    NULL,
    {NULL},
    NULL,
    NULL,
    NULL};
