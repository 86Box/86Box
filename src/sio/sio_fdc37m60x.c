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
#define INDEX dev->index

/* Current Logical Device Number */
#define CURRENT_LOGICAL_DEVICE dev->regs[0x07]

/* Global Device Configuration */
#define ENABLED dev->device_regs[CURRENT_LOGICAL_DEVICE][0x30]
#define BASE_ADDRESS ((dev->device_regs[CURRENT_LOGICAL_DEVICE][0x60] << 8) | (dev->device_regs[CURRENT_LOGICAL_DEVICE][0x61]))
#define IRQ dev->device_regs[CURRENT_LOGICAL_DEVICE][0x70]
#define DMA dev->device_regs[CURRENT_LOGICAL_DEVICE][0x74]

/* Miscellaneous Chip Functionality */
#define SOFT_RESET (val & 0x01)
#define POWER_CONTROL dev->regs[0x22]

#ifdef ENABLE_FDC37M60X_LOG
int fdc37m60x_do_log = ENABLE_FDC37M60X_LOG;
static void
fdc37m60x_log(const char *fmt, ...)
{
    va_list ap;

    if (fdc37m60x_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define fdc37m60x_log(fmt, ...)
#endif

typedef struct
{
    uint8_t index, regs[256], device_regs[10][256], cfg_lock, ide_function;
    uint16_t sio_index_port;

    fdc_t *fdc_controller;
    serial_t *uart[2];

} fdc37m60x_t;

void fdc37m60x_fdc_handler(fdc37m60x_t *dev);
void fdc37m60x_uart_handler(uint8_t num, fdc37m60x_t *dev);
void fdc37m60x_lpt_handler(fdc37m60x_t *dev);
void fdc37m60x_logical_device_handler(fdc37m60x_t *dev);
static void fdc37m60x_reset(void *priv);

static void
fdc37m60x_write(uint16_t addr, uint8_t val, void *priv)
{
    fdc37m60x_t *dev = (fdc37m60x_t *)priv;

    switch (addr)
    {
    case 0x3f0:
    case 0x370:
        INDEX = val;

        /* Enter/Escape Configuration Mode */
        if (val == 0x55)
            dev->cfg_lock = 0;
        else if (val == 0xaa)
            dev->cfg_lock = 1;
        break;

    case 0x3f1:
    case 0x371:
        if (!dev->cfg_lock)
        {
            switch (INDEX)
            {
            /* Global Configuration */
            case 0x02:
                dev->regs[INDEX] = val;
                if (SOFT_RESET)
                    fdc37m60x_reset(dev);
                break;

            case 0x07:
                CURRENT_LOGICAL_DEVICE = (val & 0x0f);
                break;

            case 0x22:
                POWER_CONTROL = val & 0x3f;
                break;

            case 0x23:
                dev->regs[INDEX] = val & 0x3f;
                break;

            case 0x24:
                dev->regs[INDEX] = val & 0xce;
                break;
            
            /* Device Configuration */
            case 0x30:
            case 0x60:
            case 0x61:
            case 0x70:
            case 0x74:
            if(CURRENT_LOGICAL_DEVICE <= 0x81) /* Avoid Overflow */
            dev->device_regs[CURRENT_LOGICAL_DEVICE][INDEX] = (INDEX == 0x30) ? (val & 1) : val;
            fdc37m60x_logical_device_handler(dev);
            break;
            }
        }
        break;
    }
}

static uint8_t
fdc37m60x_read(uint16_t addr, void *priv)
{
    fdc37m60x_t *dev = (fdc37m60x_t *)priv;

    return (INDEX >= 0x30) ? dev->device_regs[CURRENT_LOGICAL_DEVICE][INDEX] : dev->regs[INDEX];
}

void fdc37m60x_fdc_handler(fdc37m60x_t *dev)
{
    fdc_remove(dev->fdc_controller);
    if(ENABLED || (POWER_CONTROL & 0x01))
    {
        fdc_set_base(dev->fdc_controller, BASE_ADDRESS);
        fdc_set_irq(dev->fdc_controller, IRQ & 0xf);
        fdc_set_dma_ch(dev->fdc_controller, DMA & 0x07);
        fdc37m60x_log("SMC60x-FDC: BASE %04x IRQ %d DMA %d\n", BASE_ADDRESS, IRQ & 0xf, DMA & 0x07);
    }
}

void fdc37m60x_uart_handler(uint8_t num, fdc37m60x_t *dev)
{
    serial_remove(dev->uart[num & 1]);
    if(!(num & 1) ? (ENABLED || (POWER_CONTROL & 0x10)) : (ENABLED || (POWER_CONTROL & 0x20)))
    {
        serial_setup(dev->uart[num & 1], BASE_ADDRESS, IRQ & 0xf);
        fdc37m60x_log("SMC60x-UART%d: BASE %04x IRQ %d\n", num & 1, BASE_ADDRESS, IRQ & 0xf);
    }
}

void fdc37m60x_lpt_handler(fdc37m60x_t *dev)
{
    lpt1_remove();
    if(ENABLED || (POWER_CONTROL & 0x80))
    {
    lpt1_init(BASE_ADDRESS);
    lpt1_irq(IRQ & 0xf);
    fdc37m60x_log("SMC60x-LPT: BASE %04x IRQ %d\n", BASE_ADDRESS, IRQ & 0xf);
    }
}

void fdc37m60x_logical_device_handler(fdc37m60x_t *dev)
{
/*
Register 07h:
Device 0: FDC
Device 3: LPT
Device 4: UART1
Device 5: UART2
*/
    switch (CURRENT_LOGICAL_DEVICE)
    {
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
    fdc37m60x_t *dev = (fdc37m60x_t *)priv;

    CURRENT_LOGICAL_DEVICE = 0x00;
    dev->regs[0x22] = 0x00;
    dev->regs[0x26] = SIO_INDEX_PORT & 0xf;
    dev->regs[0x27] = (SIO_INDEX_PORT >> 4) & 0xf;

    /* FDC Registers */
    dev->device_regs[0][0x30] = 0x00;
    dev->device_regs[0][0x60] = 0x03; /* Base Address */
    dev->device_regs[0][0x61] = 0xf0;

    dev->device_regs[0][0x70] = 0x06;
    dev->device_regs[0][0x74] = 0x02;

    /* LPT Port */
    dev->device_regs[3][0x30] = 0x00;
    dev->device_regs[3][0x60] = 0x00; /* Base Address */
    dev->device_regs[3][0x61] = 0x00;

    dev->device_regs[3][0x64] = 0x04;

    /* UART1 */
    dev->device_regs[4][0x30] = 0x00;
    dev->device_regs[4][0x60] = 0x00; /* Base Address */
    dev->device_regs[4][0x61] = 0x00;

    dev->device_regs[4][0x70] = 0x00;

    /* UART2 */
    dev->device_regs[5][0x30] = 0x00;
    dev->device_regs[5][0x60] = 0x00; /* Base Address */
    dev->device_regs[5][0x61] = 0x00;

    dev->device_regs[5][0x70] = 0x00;

    /* AUX */
    dev->device_regs[8][0x30] = 0x00;

    fdc37m60x_fdc_handler(dev);
    fdc37m60x_uart_handler(0, dev);
    fdc37m60x_uart_handler(1, dev);
    fdc37m60x_lpt_handler(dev);
}

static void
fdc37m60x_close(void *priv)
{
    fdc37m60x_t *dev = (fdc37m60x_t *)priv;

    free(dev);
}

static void *
fdc37m60x_init(const device_t *info)
{
    fdc37m60x_t *dev = (fdc37m60x_t *)malloc(sizeof(fdc37m60x_t));
    memset(dev, 0, sizeof(fdc37m60x_t));
    SIO_INDEX_PORT = info->local;

    dev->regs[0x20] = 0x47;
    dev->regs[0x24] = 0x04;
    dev->device_regs[0][0xf0] = 0x0e;
    dev->device_regs[0][0xf2] = 0xff;
    dev->device_regs[3][0xf0] = 0x3c;
    dev->device_regs[4][0xf1] = 0x02;
    dev->device_regs[4][0xf2] = 0x03;
    dev->device_regs[8][0xc0] = 0x06;
    dev->device_regs[8][0xc1] = 0x03;

    dev->fdc_controller = device_add(&fdc_at_smc_device);
    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    io_sethandler(SIO_INDEX_PORT, 0x0002, fdc37m60x_read, NULL, NULL, fdc37m60x_write, NULL, NULL, dev);

    return dev;
}

const device_t fdc37m60x_device = {
    "SMSC FDC37M60X",
    0,
    0x03f0,
    fdc37m60x_init,
    fdc37m60x_close,
    NULL,
    {NULL},
    NULL,
    NULL,
    NULL};

const device_t fdc37m60x_370_device = {
    "SMSC FDC37M60X with 10K Pull Up Resistor",
    0,
    0x0370,
    fdc37m60x_init,
    fdc37m60x_close,
    NULL,
    {NULL},
    NULL,
    NULL,
    NULL};
