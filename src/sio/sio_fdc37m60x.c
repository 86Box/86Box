/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 * Emulation of the SMSC FDC37M60x Super I/O
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
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>

#define SIO_INDEX_PORT dev->sio_index_port
#define INDEX          dev->index

/* Current Logical Device Number */
#define CURRENT_LOGICAL_DEVICE dev->regs[0x07]

/* Global Device Configuration */
#define ENABLED(ld)      dev->device_regs[ld][0x30]
#define BASE_ADDRESS(ld) ((dev->device_regs[ld][0x60] << 8) | (dev->device_regs[ld][0x61]))
#define IRQ(ld)          dev->device_regs[ld][0x70]
#define DMA(ld)          dev->device_regs[ld][0x74]

/* Miscellaneous Chip Functionality */
#define SOFT_RESET    (val & 0x01)
#define POWER_CONTROL dev->regs[0x22]

#ifdef ENABLE_FDC37M60X_LOG
int fdc37m60x_do_log = ENABLE_FDC37M60X_LOG;

static void
fdc37m60x_log(const char *fmt, ...)
{
    va_list ap;

    if (fdc37m60x_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define fdc37m60x_log(fmt, ...)
#endif

typedef struct
{
    uint8_t  index, regs[256], device_regs[10][256], cfg_lock, ide_function;
    uint16_t sio_index_port;

    fdc_t    *fdc;
    serial_t *uart[2];

} fdc37m60x_t;

static void fdc37m60x_fdc_handler(fdc37m60x_t *dev);
static void fdc37m60x_uart_handler(uint8_t num, fdc37m60x_t *dev);
static void fdc37m60x_lpt_handler(fdc37m60x_t *dev);
static void fdc37m60x_logical_device_handler(fdc37m60x_t *dev);
static void fdc37m60x_reset(void *priv);

static void
fdc37m60x_write(uint16_t addr, uint8_t val, void *priv)
{
    fdc37m60x_t *dev = (fdc37m60x_t *) priv;

    if (addr & 1) {
        if (!dev->cfg_lock) {
            switch (INDEX) {
                /* Global Configuration */
                case 0x02:
                    dev->regs[INDEX] = val;
                    if (SOFT_RESET)
                        fdc37m60x_reset(dev);
                    break;

                case 0x07:
                    CURRENT_LOGICAL_DEVICE = val;
                    break;

                case 0x22:
                    POWER_CONTROL = val & 0x3f;
                    break;

                case 0x23:
                    dev->regs[INDEX] = val & 0x3f;
                    break;

                case 0x24:
                    dev->regs[INDEX] = val & 0x4e;
                    break;

                case 0x2b:
                case 0x2c:
                case 0x2d:
                case 0x2e:
                case 0x2f:
                    dev->regs[INDEX] = val;
                    break;

                /* Device Configuration */
                case 0x30:
                case 0x60:
                case 0x61:
                case 0x70:
                case 0x74:
                case 0xf0:
                case 0xf1:
                case 0xf2:
                case 0xf3:
                case 0xf4:
                case 0xf5:
                case 0xf6:
                case 0xf7:
                    if (CURRENT_LOGICAL_DEVICE <= 0x81) /* Avoid Overflow */
                        dev->device_regs[CURRENT_LOGICAL_DEVICE][INDEX] = (INDEX == 0x30) ? (val & 1) : val;
                    fdc37m60x_logical_device_handler(dev);
                    break;
            }
        }
    } else {
        /* Enter/Escape Configuration Mode */
        if (val == 0x55)
            dev->cfg_lock = 0;
        else if (!dev->cfg_lock && (val == 0xaa))
            dev->cfg_lock = 1;
        else if (!dev->cfg_lock)
            INDEX = val;
    }
}

static uint8_t
fdc37m60x_read(uint16_t addr, void *priv)
{
    fdc37m60x_t *dev = (fdc37m60x_t *) priv;
    uint8_t      ret = 0xff;

    if (addr & 1)
        ret = (INDEX >= 0x30) ? dev->device_regs[CURRENT_LOGICAL_DEVICE][INDEX] : dev->regs[INDEX];

    return ret;
}

static void
fdc37m60x_fdc_handler(fdc37m60x_t *dev)
{
    fdc_remove(dev->fdc);

    if (ENABLED(0) || (POWER_CONTROL & 0x01)) {
        fdc_set_base(dev->fdc, BASE_ADDRESS(0));
        fdc_set_irq(dev->fdc, IRQ(0) & 0xf);
        fdc_set_dma_ch(dev->fdc, DMA(0) & 0x07);
        fdc37m60x_log("SMC60x-FDC: BASE %04x IRQ %d DMA %d\n", BASE_ADDRESS(0), IRQ(0) & 0xf, DMA(0) & 0x07);
    }

    fdc_update_enh_mode(dev->fdc, dev->device_regs[0][0xf0] & 0x01);

    fdc_update_densel_force(dev->fdc, (dev->device_regs[0][0xf1] & 0xc) >> 2);

    fdc_update_rwc(dev->fdc, 3, (dev->device_regs[0][0xf2] & 0xc0) >> 6);
    fdc_update_rwc(dev->fdc, 2, (dev->device_regs[0][0xf2] & 0x30) >> 4);
    fdc_update_rwc(dev->fdc, 1, (dev->device_regs[0][0xf2] & 0x0c) >> 2);
    fdc_update_rwc(dev->fdc, 0, (dev->device_regs[0][0xf2] & 0x03));

    fdc_update_drvrate(dev->fdc, 0, (dev->device_regs[0][0xf4] & 0x18) >> 3);
    fdc_update_drvrate(dev->fdc, 1, (dev->device_regs[0][0xf5] & 0x18) >> 3);
    fdc_update_drvrate(dev->fdc, 2, (dev->device_regs[0][0xf6] & 0x18) >> 3);
    fdc_update_drvrate(dev->fdc, 3, (dev->device_regs[0][0xf7] & 0x18) >> 3);
}

static void
fdc37m60x_uart_handler(uint8_t num, fdc37m60x_t *dev)
{
    serial_remove(dev->uart[num & 1]);

    if (ENABLED(4 + (num & 1)) || (POWER_CONTROL & (1 << (4 + (num & 1))))) {
        serial_setup(dev->uart[num & 1], BASE_ADDRESS(4 + (num & 1)), IRQ(4 + (num & 1)) & 0xf);
        fdc37m60x_log("SMC60x-UART%d: BASE %04x IRQ %d\n", num & 1, BASE_ADDRESS(4 + (num & 1)), IRQ(4 + (num & 1)) & 0xf);
    }
}

void
fdc37m60x_lpt_handler(fdc37m60x_t *dev)
{
    lpt1_remove();

    if (ENABLED(3) || (POWER_CONTROL & 0x08)) {
        lpt1_init(BASE_ADDRESS(3));
        lpt1_irq(IRQ(3) & 0xf);
        fdc37m60x_log("SMC60x-LPT: BASE %04x IRQ %d\n", BASE_ADDRESS(3), IRQ(3) & 0xf);
    }
}

void
fdc37m60x_logical_device_handler(fdc37m60x_t *dev)
{
    /* Register 07h:
        Device 0: FDC
        Device 3: LPT
        Device 4: UART1
        Device 5: UART2
     */

    switch (CURRENT_LOGICAL_DEVICE) {
        case 0x00:
            fdc37m60x_fdc_handler(dev);
            break;

        case 0x03:
            fdc37m60x_lpt_handler(dev);
            break;

        case 0x04:
            fdc37m60x_uart_handler(0, dev);
            break;

        case 0x05:
            fdc37m60x_uart_handler(1, dev);
            break;
    }
}

static void
fdc37m60x_reset(void *priv)
{
    fdc37m60x_t *dev = (fdc37m60x_t *) priv;
    uint8_t      i;

    memset(dev->regs, 0, sizeof(dev->regs));
    for (i = 0; i < 10; i++)
        memset(dev->device_regs[i], 0, sizeof(dev->device_regs[i]));

    dev->regs[0x20] = 0x47;
    dev->regs[0x24] = 0x04;
    dev->regs[0x26] = SIO_INDEX_PORT & 0xf;
    dev->regs[0x27] = (SIO_INDEX_PORT >> 4) & 0xf;

    /* FDC Registers */
    dev->device_regs[0][0x60] = 0x03; /* Base Address */
    dev->device_regs[0][0x61] = 0xf0;
    dev->device_regs[0][0x70] = 0x06;
    dev->device_regs[0][0x74] = 0x02;
    dev->device_regs[0][0xf0] = 0x0e;
    dev->device_regs[0][0xf2] = 0xff;

    /* LPT Port */
    dev->device_regs[3][0x74] = 0x04;
    dev->device_regs[3][0xf0] = 0x3c;

    /* UART1 */
    dev->device_regs[4][0x74] = 0x04;
    dev->device_regs[4][0xf1] = 0x02;
    dev->device_regs[4][0xf2] = 0x03;

    /* AUX */
    dev->device_regs[8][0xc0] = 0x06;
    dev->device_regs[8][0xc1] = 0x03;

    fdc37m60x_fdc_handler(dev);
    fdc37m60x_uart_handler(0, dev);
    fdc37m60x_uart_handler(1, dev);
    fdc37m60x_lpt_handler(dev);
}

static void
fdc37m60x_close(void *priv)
{
    fdc37m60x_t *dev = (fdc37m60x_t *) priv;

    free(dev);
}

static void *
fdc37m60x_init(const device_t *info)
{
    fdc37m60x_t *dev = (fdc37m60x_t *) malloc(sizeof(fdc37m60x_t));
    memset(dev, 0, sizeof(fdc37m60x_t));
    SIO_INDEX_PORT = info->local;

    dev->fdc     = device_add(&fdc_at_smc_device);
    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    io_sethandler(SIO_INDEX_PORT, 0x0002, fdc37m60x_read, NULL, NULL, fdc37m60x_write, NULL, NULL, dev);

    fdc37m60x_reset(dev);

    return dev;
}

const device_t fdc37m60x_device = {
    .name          = "SMSC FDC37M60X",
    .internal_name = "fdc37m60x",
    .flags         = 0,
    .local         = FDC_PRIMARY_ADDR,
    .init          = fdc37m60x_init,
    .close         = fdc37m60x_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37m60x_370_device = {
    .name          = "SMSC FDC37M60X with 10K Pull Up Resistor",
    .internal_name = "fdc37m60x_370",
    .flags         = 0,
    .local         = FDC_SECONDARY_ADDR,
    .init          = fdc37m60x_init,
    .close         = fdc37m60x_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
