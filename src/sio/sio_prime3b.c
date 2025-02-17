/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Goldstar Prime3B Super I/O
 *
 *
 *
 * Authors: Tiseno100
 *
 *          Copyright 2021 Tiseno100
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
#include <86box/plat_unused.h>

#define FSR                   dev->regs[0xa0]
#define ASR                   dev->regs[0xa1]
#define PDR                   dev->regs[0xa2]
#define HAS_IDE_FUNCTIONALITY dev->ide_function

#ifdef ENABLE_PRIME3B_LOG
int prime3b_do_log = ENABLE_PRIME3B_LOG;

static void
prime3b_log(const char *fmt, ...)
{
    va_list ap;

    if (prime3b_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define prime3b_log(fmt, ...)
#endif

typedef struct prime3b_t {
    uint8_t  index;
    uint8_t  regs[256];
    uint8_t  cfg_lock;
    uint8_t  ide_function;
    uint16_t com3_addr;
    uint16_t com4_addr;

    fdc_t    *fdc_controller;
    serial_t *uart[2];

} prime3b_t;

void prime3b_fdc_handler(prime3b_t *dev);
void prime3b_uart_handler(uint8_t num, prime3b_t *dev);
void prime3b_lpt_handler(prime3b_t *dev);
void prime3b_ide_handler(prime3b_t *dev);
void prime3b_enable(prime3b_t *dev);
void prime3b_powerdown(prime3b_t *dev);

static void
prime3b_write(uint16_t addr, uint8_t val, void *priv)
{
    prime3b_t *dev = (prime3b_t *) priv;

    if (addr == 0x398) {
        dev->index = val;

        /* Enter/Escape Configuration Mode */
        if (val == 0x33)
            dev->cfg_lock = 0;
        else if (val == 0xcc)
            dev->cfg_lock = 1;
    } else if ((addr == 0x399) && !dev->cfg_lock) {
        switch (dev->index) {
            case 0xa0: /* Function Selection Register (FSR) */
                FSR = val;
                prime3b_enable(dev);
                break;
            case 0xa1: /* Address Selection Register (ASR) */
                ASR = val;
                prime3b_enable(dev);
                break;
            case 0xa2: /* Power Down Register (PDR) */
                dev->regs[0xa2] = val;
                break;
            case 0xa3: /* Test Mode Register (TMR) */
                dev->regs[0xa3] = val;
                break;
            case 0xa4: /* Miscellaneous Function Register */
                dev->regs[0xa4] = val;
                switch ((dev->regs[0xa4] >> 6) & 3) {
                    case 0:
                        dev->com3_addr = COM3_ADDR;
                        dev->com4_addr = COM4_ADDR;
                        break;
                    case 1:
                        dev->com3_addr = 0x338;
                        dev->com4_addr = 0x238;
                        break;
                    case 2:
                        dev->com3_addr = COM4_ADDR;
                        dev->com4_addr = 0x2e0;
                        break;
                    case 3:
                        dev->com3_addr = 0x220;
                        dev->com4_addr = 0x228;
                        break;

                    default:
                        break;
                }
                break;
            case 0xa5: /* ECP Register */
                dev->regs[0xa5] = val;
                break;

            default:
                break;
        }
    }
}

static uint8_t
prime3b_read(UNUSED(uint16_t addr), void *priv)
{
    const prime3b_t *dev = (prime3b_t *) priv;

    return dev->regs[dev->index];
}

void
prime3b_fdc_handler(prime3b_t *dev)
{
    uint16_t fdc_base = !(ASR & 0x40) ? FDC_PRIMARY_ADDR : FDC_SECONDARY_ADDR;
    fdc_remove(dev->fdc_controller);
    fdc_set_base(dev->fdc_controller, fdc_base);
    prime3b_log("Prime3B-FDC: Enabled with base %03x\n", fdc_base);
}

void
prime3b_uart_handler(uint8_t num, prime3b_t *dev)
{
    uint16_t uart_base;
    if ((ASR >> (3 + 2 * num)) & 1)
        uart_base = !((ASR >> (2 + 2 * num)) & 1) ? dev->com3_addr : dev->com4_addr;
    else
        uart_base = !((ASR >> (2 + 2 * num)) & 1) ? COM1_ADDR : COM2_ADDR;

    serial_remove(dev->uart[num]);
    serial_setup(dev->uart[num], uart_base, 4 - num);
    prime3b_log("Prime3B-UART%d: Enabled with base %03x\n", num, uart_base);
}

void
prime3b_lpt_handler(prime3b_t *dev)
{
    uint16_t lpt_base = (ASR & 2) ? LPT_MDA_ADDR : (!(ASR & 1) ? LPT1_ADDR : LPT2_ADDR);
    lpt1_remove();
    lpt1_setup(lpt_base);
    lpt1_irq(LPT1_IRQ);
    prime3b_log("Prime3B-LPT: Enabled with base %03x\n", lpt_base);
}

void
prime3b_ide_handler(prime3b_t *dev)
{
    ide_pri_disable();
    uint16_t ide_base = !(ASR & 0x80) ? 0x1f0 : 0x170;
    uint16_t ide_side = ide_base + 0x206;
    ide_set_base(0, ide_base);
    ide_set_side(0, ide_side);
    prime3b_log("Prime3B-IDE: Enabled with base %03x and side %03x\n", ide_base, ide_side);
}

void
prime3b_enable(prime3b_t *dev)
{
    /*
        Simulate a device enable/disable scenario

            Register A0: Function Selection Register (FSR)
            Bit 7: Gameport
            Bit 6: 4 FDD Enable
            Bit 5: IDE
            Bit 4: FDC
            Bit 3: UART 2
            Bit 2: UART 1
            Bit 1/0: PIO (0/0 Bidirectional , 0/1 ECP, 1/0 EPP, 1/1 Disabled)

            Note: 86Box LPT is simplistic and can't do ECP or EPP.
    */

    !(FSR & 3) ? prime3b_lpt_handler(dev) : lpt1_remove();
    (FSR & 4) ? prime3b_uart_handler(0, dev) : serial_remove(dev->uart[0]);
    (FSR & 8) ? prime3b_uart_handler(1, dev) : serial_remove(dev->uart[1]);
    (FSR & 0x10) ? prime3b_fdc_handler(dev) : fdc_remove(dev->fdc_controller);
    if (HAS_IDE_FUNCTIONALITY)
        (FSR & 0x20) ? prime3b_ide_handler(dev) : ide_pri_disable();
}

void
prime3b_powerdown(prime3b_t *dev)
{
    /* Note: It can be done more efficiently for sure */
    uint8_t old_base = PDR;

    if (PDR & 1)
        PDR |= 0x1e;

    if (PDR & 0x40)
        io_removehandler(0x0398, 0x0002, prime3b_read, NULL, NULL, prime3b_write, NULL, NULL, dev);

    if (PDR & 2)
        fdc_remove(dev->fdc_controller);

    if (PDR & 4)
        serial_remove(dev->uart[0]);

    if (PDR & 8)
        serial_remove(dev->uart[1]);

    if (PDR & 0x10)
        lpt1_remove();

    if (PDR & 1)
        PDR = old_base;
}

static void
prime3b_close(void *priv)
{
    prime3b_t *dev = (prime3b_t *) priv;

    free(dev);
}

static void *
prime3b_init(const device_t *info)
{
    prime3b_t *dev = (prime3b_t *) calloc(1, sizeof(prime3b_t));

    /* Avoid conflicting with machines that make no use of the Prime3B Internal IDE */
    HAS_IDE_FUNCTIONALITY = info->local;

    dev->regs[0xa0] = 3;

    dev->fdc_controller = device_add(&fdc_at_device);
    dev->uart[0]        = device_add_inst(&ns16550_device, 1);
    dev->uart[1]        = device_add_inst(&ns16550_device, 2);
    if (HAS_IDE_FUNCTIONALITY)
        device_add(&ide_isa_device);

    dev->com3_addr = COM3_ADDR;
    dev->com4_addr = COM4_ADDR;
    fdc_reset(dev->fdc_controller);

    prime3b_enable(dev);

    io_sethandler(0x0398, 0x0002, prime3b_read, NULL, NULL, prime3b_write, NULL, NULL, dev);

    return dev;
}

const device_t prime3b_device = {
    .name          = "Goldstar Prime3B",
    .internal_name = "prime3b",
    .flags         = 0,
    .local         = 0,
    .init          = prime3b_init,
    .close         = prime3b_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t prime3b_ide_device = {
    .name          = "Goldstar Prime3B with IDE functionality",
    .internal_name = "prime3b_ide",
    .flags         = 0,
    .local         = 1,
    .init          = prime3b_init,
    .close         = prime3b_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
