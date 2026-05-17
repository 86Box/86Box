/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ALi M5113 Super I/O Chip.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2026 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#ifdef ENABLE_ALI5113_LOG
#include "cpu.h"
#endif
#include <86box/sio.h>
#include <86box/plat_unused.h>

typedef struct ali5113_t {
    uint8_t   regs[256];
    int       locked;
    int       cur_reg;
    fdc_t    *fdc;
    serial_t *uart[3];
    lpt_t    *lpt;
} ali5113_t;

static void    ali5113_write(uint16_t port, uint8_t val, void *priv);
static uint8_t ali5113_read(uint16_t port, void *priv);

#ifdef ENABLE_ALI5113_LOG
int ali5113_do_log = ENABLE_ALI5113_LOG;

static void
ali5113_log(const char *fmt, ...)
{
    va_list ap;

    if (ali5113_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ali5113_log(fmt, ...)
#endif

static void
ali5113_fdc_handler(ali5113_t *dev)
{
    uint8_t enable = dev->regs[0x66] & 0x01;

    fdc_remove(dev->fdc);

    if (enable)
        fdc_set_base(dev->fdc, 0x03f0);
}

/*
   AAh bit 0: 1 = enable, 0 = disable.
   BBh bit 3, 2: 01 = 3BCh, 10 = 378H, 11 = 278h.
   BBh: 48: normal, 08: bi-directional, 18: EPP, 28: ECP.
 */
static void
ali5113_lpt_handler(ali5113_t *dev)
{
    uint8_t  enable = dev->regs[0xaa] & 0x01;
    uint8_t  irq    = (dev->regs[0xcc] & 0x80) ? 5 : 7;
    uint16_t port   = 0x0000;
    uint8_t  irq_readout[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x08,
                                 0x00, 0x10, 0x18, 0x20, 0x00, 0x28, 0x30, 0x00 };

    lpt_port_remove(dev->lpt);

    switch (dev->regs[0xbb] & 0x0c) {
        default:
        case 0x04:
            port = 0x03bc;
            break;
        case 0x08:
            port = 0x0378;
            break;
        case 0x0c:
            port = 0x0278;
            break;
    }

    lpt_set_epp(dev->lpt, (dev->regs[0xbb] & 0x10));
    lpt_set_ecp(dev->lpt, (dev->regs[0xbb] & 0x20));
    lpt_set_ext(dev->lpt, !(dev->regs[0xbb] & 0x40));

    if (enable)
        lpt_port_setup(dev->lpt, port);

    lpt_port_irq(dev->lpt, irq);

    lpt_set_cnfgb_readout(dev->lpt, ((irq > 15) ? 0x00 : irq_readout[irq]) |
                          ((jumpered_internal_ecp_dma >= 4) ? 0x00 : jumpered_internal_ecp_dma));
}

static void
ali5113_serial_handler(ali5113_t *dev, int uart)
{
    uint8_t  enable = 0;
    uint8_t  irq    = 4;
    uint16_t port   = 0x0000;

    if (uart == 0) {
        enable = dev->regs[0x88] & 0x01;
        port   = (dev->regs[0x77] & 0x10) ? 0x03e8 : 0x03f8;
    } else if (uart == 1) {
        enable = dev->regs[0x99] & 0x01;
        port   = (dev->regs[0x77] & 0x02) ? 0x02e8 : 0x02f8;

        irq    = 3;
    }

    serial_remove(dev->uart[uart]);

    if (enable)
        serial_setup(dev->uart[uart], port, irq);
}

static void
ali5113_reset(void *priv)
{
    ali5113_t *dev = (ali5113_t *) priv;

    memset(dev->regs, 0x00, 256);

    dev->regs[0x66] = 0x01;
    dev->regs[0x77] = 0x10;
    dev->regs[0x88] = 0x01;
    dev->regs[0x99] = 0x01;
    dev->regs[0xaa] = 0x01;
    dev->regs[0xbb] = 0x48;
    dev->regs[0xcc] = 0x00;

    ali5113_lpt_handler(dev);
    ali5113_serial_handler(dev, 0);
    ali5113_serial_handler(dev, 1);
    ali5113_serial_handler(dev, 2);

    fdc_reset(dev->fdc);
    ali5113_fdc_handler(dev);

    dev->locked = 0;
}

static void
ali5113_write(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    ali5113_t *dev    = (ali5113_t *) priv;
    uint8_t    valxor;

    switch (dev->locked) {
        default:
            ali5113_log("[%04X:%08X] [%04X] ALi M5113: Write %02X in invalid phase %i\n",
                        CS, cpu_state.pc, port, val, dev->locked);
            dev->locked = 0;
            break;
        case 0:
            if (val == 0x51) {
                dev->locked++;
                ali5113_log("[%04X:%08X] [%04X] ALi M5113: Write %02X and advance to phase 1\n",
                            CS, cpu_state.pc, port, val);
            } else {
                dev->locked = 0;
                ali5113_log("[%04X:%08X] [%04X] ALi M5113: Write %02X and do nothing\n",
                            CS, cpu_state.pc, port, val);
            }
            break;
        case 1:
            if (val == 0x29) {
                dev->locked++;
                dev->cur_reg = 0x93;
                fdc_3f1_enable(dev->fdc, 0);
                ali5113_log("[%04X:%08X] [%04X] ALi M5113: Write %02X and enter configuration\n",
                            CS, cpu_state.pc, port, val);
            } else {
                ali5113_log("[%04X:%08X] [%04X] ALi M5113: Write %02X and return to phase 0\n",
                            CS, cpu_state.pc, port, val);
            }
            break;
        case 2:
            dev->cur_reg = val;
            dev->locked++;
            ali5113_log("[%04X:%08X] [%04X] ALi M5113: Write %02X to index\n",
                        CS, cpu_state.pc, port, val);
            break;
        case 3:
            valxor = dev->regs[dev->cur_reg] ^ val;
            dev->regs[dev->cur_reg] = val;

            switch (dev->cur_reg) {
                default:
                    break;
                /*
                   3E8: 77h bit 4 set, 88h bit 1 set;
                   3F8: 77h bit 4 clear, 88h bit 1 set;
                   Disabled: 88h bit 1 clear.
                 */
                case 0x66:
                    if (valxor & 0x01)
                        ali5113_fdc_handler(dev);
                    break;
                case 0x77:
                    if (valxor & 0x10)
                        ali5113_serial_handler(dev, 0);
                    if (valxor & 0x02)
                        ali5113_serial_handler(dev, 1);
                    break;
                case 0x88:
                    if (valxor & 0x01)
                        ali5113_serial_handler(dev, 0);
                    break;
                case 0x99:
                    if (valxor & 0x01)
                        ali5113_serial_handler(dev, 1);
                    break;
                case 0xaa:
                    if (valxor & 0x01)
                        ali5113_lpt_handler(dev);
                    break;
                case 0xbb:
                    if (valxor & 0x7c)
                        ali5113_lpt_handler(dev);
                    break;
                case 0xcc:
                    if (valxor & 0x80)
                        ali5113_lpt_handler(dev);
                    break;
            }

            dev->locked = 0;
            ali5113_log("[%04X:%08X] [%04X] ALi M5113: Write %02X to register %02X\n",
                        CS, cpu_state.pc, port, val, dev->cur_reg);
            break;
    }
}

static uint8_t
ali5113_read(uint16_t port, void *priv)
{
    ali5113_t *dev = (ali5113_t *) priv;
    uint8_t    ret;

    switch (dev->locked) {
        default:
            ret = 0xff;
            ali5113_log("[%04X:%08X] [%04X] Read %02X from nothing\n",
                        CS, cpu_state.pc, port, ret);
            break;
        case 2:
            ret = dev->cur_reg;
            ali5113_log("[%04X:%08X] [%04X] Read %02X from index\n",
                        CS, cpu_state.pc, port, ret);
            break;
        case 3:
            ret = dev->regs[dev->cur_reg];
            ali5113_log("[%04X:%08X] [%04X] Read %02X from register %02X\n",
                        CS, cpu_state.pc, port, ret, dev->cur_reg);
            dev->locked = 0;
            break;
    }

    return ret;
}

static void
ali5113_close(void *priv)
{
    ali5113_t *dev = (ali5113_t *) priv;

    free(dev);
}

static void *
ali5113_init(const device_t *info)
{
    ali5113_t *dev = (ali5113_t *) calloc(1, sizeof(ali5113_t));

    dev->fdc = device_add(&fdc_at_ali_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->lpt     = device_add_inst(&lpt_port_device, 1);

    ali5113_reset(dev);

    /* The ICOP-6021 BIOS seems to also support a second M5113 on port 0398h. */
    io_sethandler(0x03f1, 0x0001,
                  ali5113_read, NULL, NULL,
                  ali5113_write, NULL, NULL, dev);

    device_add_params(&kbc_at_device, (void *) KBC_VEN_ALI);

    return dev;
}

const device_t ali5113_device = {
    .name          = "ALi M5113 Super I/O",
    .internal_name = "ali5113",
    .flags         = 0,
    .local         = 0x40,
    .init          = ali5113_init,
    .close         = ali5113_close,
    .reset         = ali5113_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
