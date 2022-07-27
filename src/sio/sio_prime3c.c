/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 * Emulation of the LG Prime3C Super I/O
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

#ifdef ENABLE_PRIME3C_LOG
int prime3c_do_log = ENABLE_PRIME3C_LOG;
static void
prime3c_log(const char *fmt, ...)
{
    va_list ap;

    if (prime3c_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define prime3c_log(fmt, ...)
#endif

/* Function Select(Note on prime3c_enable) */
#define FUNCTION_SELECT dev->regs[0xc2]

/* Base Address Registers */
#define FDC_BASE_ADDRESS dev->regs[0xc3]
#define IDE_BASE_ADDRESS dev->regs[0xc4]
#define IDE_SIDE_ADDRESS dev->regs[0xc5]
#define LPT_BASE_ADDRESS dev->regs[0xc6]
#define UART1_BASE_ADDRESS dev->regs[0xc7]
#define UART2_BASE_ADDRESS dev->regs[0xc8]

/* FDC/LPT Configuration */
#define FDC_LPT_DMA dev->regs[0xc9]
#define FDC_LPT_IRQ dev->regs[0xca]

/* UART 1/2 Configuration */
#define UART_IRQ dev->regs[0xcb]

/* Miscellaneous Configuration*/
#define FDC_SWAP (dev->regs[0xd6] & 0x01)

/* IDE functionality(Note on Init) */
#define HAS_IDE_FUNCTIONALITY dev->ide_function

typedef struct
{
    uint8_t index, regs[256], cfg_lock, ide_function;

    fdc_t *fdc_controller;
    serial_t *uart[2];

} prime3c_t;

void prime3c_fdc_handler(prime3c_t *dev);
void prime3c_uart_handler(uint8_t num, prime3c_t *dev);
void prime3c_lpt_handler(prime3c_t *dev);
void prime3c_ide_handler(prime3c_t *dev);
void prime3c_enable(prime3c_t *dev);

static void
prime3c_write(uint16_t addr, uint8_t val, void *priv)
{
    prime3c_t *dev = (prime3c_t *)priv;

    switch (addr)
    {
    case 0x398:
        dev->index = val;

        /* Enter/Escape Configuration Mode */
        if (val == 0x33)
            dev->cfg_lock = 0;
        else if (val == 0x55)
            dev->cfg_lock = 1;
        break;

    case 0x399:
        if (!dev->cfg_lock)
        {
            switch (dev->index)
            {
            case 0xc2:
                FUNCTION_SELECT = val & 0xbf;
                prime3c_enable(dev);
                break;

            case 0xc3:
                FDC_BASE_ADDRESS = val & 0xfc;
                prime3c_fdc_handler(dev);
                break;

            case 0xc4:
                IDE_BASE_ADDRESS = val & 0xfc;
                if (HAS_IDE_FUNCTIONALITY)
                    prime3c_ide_handler(dev);
                break;

            case 0xc5:
                IDE_SIDE_ADDRESS = (val & 0xfc) | 0x02;
                if (HAS_IDE_FUNCTIONALITY)
                    prime3c_ide_handler(dev);
                break;

            case 0xc6:
                LPT_BASE_ADDRESS = val;
                break;

            case 0xc7:
                UART1_BASE_ADDRESS = val & 0xfe;
                prime3c_uart_handler(0, dev);
                break;

            case 0xc8:
                UART2_BASE_ADDRESS = val & 0xfe;
                prime3c_uart_handler(1, dev);
                break;

            case 0xc9:
                FDC_LPT_DMA = val;
                prime3c_fdc_handler(dev);
                break;

            case 0xca:
                FDC_LPT_IRQ = val;
                prime3c_fdc_handler(dev);
                prime3c_lpt_handler(dev);
                break;

            case 0xcb:
                UART_IRQ = val;
                prime3c_uart_handler(0, dev);
                prime3c_uart_handler(1, dev);
                break;

            case 0xcd:
            case 0xce:
                dev->regs[dev->index] = val;
                break;

            case 0xcf:
                dev->regs[dev->index] = val & 0x3f;
                break;

            case 0xd0:
                dev->regs[dev->index] = val & 0xfc;
                break;

            case 0xd1:
                dev->regs[dev->index] = val & 0x3f;
                break;

            case 0xd3:
                dev->regs[dev->index] = val & 0x7c;
                break;

            case 0xd5:
            case 0xd6:
            case 0xd7:
            case 0xd8:
                dev->regs[dev->index] = val;
                break;
            }
        }
        break;
    }
}

static uint8_t
prime3c_read(uint16_t addr, void *priv)
{
    prime3c_t *dev = (prime3c_t *)priv;

    return dev->regs[dev->index];
}

void prime3c_fdc_handler(prime3c_t *dev)
{
    fdc_remove(dev->fdc_controller);
    if (FUNCTION_SELECT & 0x10)
    {
        fdc_set_base(dev->fdc_controller, FDC_BASE_ADDRESS << 2);
        fdc_set_irq(dev->fdc_controller, (FDC_LPT_IRQ >> 4) & 0xf);
        fdc_set_dma_ch(dev->fdc_controller, (FDC_LPT_DMA >> 4) & 0xf);
        fdc_set_swap(dev->fdc_controller, FDC_SWAP);
        prime3c_log("Prime3C-FDC: BASE %04x IRQ %01x DMA %01x\n", FDC_BASE_ADDRESS << 2, (FDC_LPT_IRQ >> 4) & 0xf, (FDC_LPT_DMA >> 4) & 0xf);
    }
}

void prime3c_uart_handler(uint8_t num, prime3c_t *dev)
{
    serial_remove(dev->uart[num & 1]);
    if (FUNCTION_SELECT & (!(num & 1) ? 0x04 : 0x08))
    {
        serial_setup(dev->uart[num & 1], (!(num & 1) ? UART1_BASE_ADDRESS : UART2_BASE_ADDRESS) << 2, (UART_IRQ >> (!(num & 1) ? 4 : 0)) & 0xf);
        prime3c_log("Prime3C-UART%01x: BASE %04x IRQ %01x\n", num & 1, (!(num & 1) ? UART1_BASE_ADDRESS : UART2_BASE_ADDRESS) << 2, (UART_IRQ >> (!(num & 1) ? 4 : 0)) & 0xf);
    }
}

void prime3c_lpt_handler(prime3c_t *dev)
{
    lpt1_remove();
    if (!(FUNCTION_SELECT & 0x03))
    {

        lpt1_init(LPT_BASE_ADDRESS << 2);
        lpt1_irq(FDC_LPT_IRQ & 0xf);
        prime3c_log("Prime3C-LPT: BASE %04x IRQ %02x\n", LPT_BASE_ADDRESS << 2, FDC_LPT_IRQ & 0xf);
    }
}

void prime3c_ide_handler(prime3c_t *dev)
{
    ide_pri_disable();
    if (FUNCTION_SELECT & 0x20)
    {
        ide_set_base(0, IDE_BASE_ADDRESS << 2);
        ide_set_side(0, IDE_SIDE_ADDRESS << 2);
        ide_pri_enable();
        prime3c_log("Prime3C-IDE: BASE %04x SIDE %04x\n", IDE_BASE_ADDRESS << 2, IDE_SIDE_ADDRESS << 2);
    }
}

void prime3c_enable(prime3c_t *dev)
{
/*
Simulate a device enable/disable scenario

Register C2: Function Select
Bit 7: Gameport
Bit 6: Reserved
Bit 5: IDE
Bit 4: FDC
Bit 3: UART 2
Bit 2: UART 1
Bit 1/0: PIO (0/0 Unidirectional , 0/1 ECP, 1/0 EPP, 1/1 Disabled)

Note: 86Box LPT is simplistic and can't do ECP or EPP.
*/

!(FUNCTION_SELECT & 0x03) ? prime3c_lpt_handler(dev) : lpt1_remove();
(FUNCTION_SELECT & 0x04) ? prime3c_uart_handler(0, dev) : serial_remove(dev->uart[0]);
(FUNCTION_SELECT & 0x08) ? prime3c_uart_handler(1, dev) : serial_remove(dev->uart[1]);
(FUNCTION_SELECT & 0x10) ? prime3c_fdc_handler(dev) : fdc_remove(dev->fdc_controller);
if (HAS_IDE_FUNCTIONALITY)
    (FUNCTION_SELECT & 0x20) ? prime3c_ide_handler(dev) : ide_pri_disable();
}

static void
prime3c_close(void *priv)
{
    prime3c_t *dev = (prime3c_t *)priv;

    free(dev);
}

static void *
prime3c_init(const device_t *info)
{
    prime3c_t *dev = (prime3c_t *)malloc(sizeof(prime3c_t));
    memset(dev, 0, sizeof(prime3c_t));

    /* Avoid conflicting with machines that make no use of the Prime3C Internal IDE */
    HAS_IDE_FUNCTIONALITY = info->local;

    dev->regs[0xc0] = 0x3c;
    dev->regs[0xc2] = 0x03;
    dev->regs[0xc3] = 0x3c;
    dev->regs[0xc4] = 0x3c;
    dev->regs[0xc5] = 0x3d;
    dev->regs[0xd5] = 0x3c;

    dev->fdc_controller = device_add(&fdc_at_device);
    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);
    if (HAS_IDE_FUNCTIONALITY)
        device_add(&ide_isa_device);

    prime3c_fdc_handler(dev);
    prime3c_uart_handler(0, dev);
    prime3c_uart_handler(1, dev);
    prime3c_lpt_handler(dev);
    if (HAS_IDE_FUNCTIONALITY)
        prime3c_ide_handler(dev);

    io_sethandler(0x0398, 0x0002, prime3c_read, NULL, NULL, prime3c_write, NULL, NULL, dev);

    return dev;
}

const device_t prime3c_device = {
    .name = "Goldstar Prime3C",
    .internal_name = "prime3c",
    .flags = 0,
    .local = 0,
    .init = prime3c_init,
    .close = prime3c_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t prime3c_ide_device = {
    .name = "Goldstar Prime3C with IDE functionality",
    .internal_name = "prime3c_ide",
    .flags = 0,
    .local = 1,
    .init = prime3c_init,
    .close = prime3c_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
