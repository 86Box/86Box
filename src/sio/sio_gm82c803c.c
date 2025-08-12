/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the GoldStar GM82C803C Super I/O Chip.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
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
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>

typedef struct gm82c803c_t {
    uint8_t   tries;
    uint8_t   has_ide;
    uint8_t   dma_map[4];
    uint8_t   irq_map[10];
    uint8_t   regs[256];
    int       cur_reg;
    fdc_t    *fdc;
    serial_t *uart[2];
    lpt_t    *lpt;
} gm82c803c_t;

#ifdef ENABLE_GM82C803C_LOG
int gm82c803c_do_log = ENABLE_GM82C803C_LOG;

static void
gm82c803c_log(const char *fmt, ...)
{
    va_list ap;

    if (gm82c803c_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define gm82c803c_log(fmt, ...)
#endif

static void
ide_handler(gm82c803c_t *dev)
{
    uint16_t ide_port     = 0x0000;

    if (dev->has_ide > 0) {
        int ide_id = dev->has_ide - 1;

        ide_handlers(ide_id, 0);

        ide_port     = (dev->regs[0xc4] << 2) & 0xfff0;
        ide_set_base_addr(ide_id, 0, ide_port);

        ide_port     = ((dev->regs[0xc5] << 2) & 0xfff0) | 0x0006;
        ide_set_base_addr(ide_id, 1, ide_port);

        if (dev->regs[0xc2] & 0x20)
            ide_handlers(ide_id, 1);
    }
}

static void
fdc_handler(gm82c803c_t *dev)
{
    uint16_t fdc_port     = 0x0000;
    uint8_t  fdc_irq      = 6;
    uint8_t  fdc_dma      = 2;

    fdc_port     = (dev->regs[0xc3] << 2) & 0xfff0;

    fdc_irq      = dev->irq_map[dev->regs[0xca] >> 4];
    fdc_dma      = dev->dma_map[(dev->regs[0xc9] >> 4) & 0x03];

    fdc_set_irq(dev->fdc, fdc_irq);
    fdc_set_dma_ch(dev->fdc, fdc_dma);

    fdc_remove(dev->fdc);
    if (dev->regs[0xc2] & 0x10)
        fdc_set_base(dev->fdc, fdc_port);
}

static void
set_serial_addr(gm82c803c_t *dev, int port)
{
    uint16_t serial_port = 0x0000;
    uint8_t  serial_irq  = COM1_IRQ;
    double   clock_src   = 24000000.0 / 13.0;

    if (dev->regs[0xce] & (1 << (6 + port)))
        clock_src = 24000000.0 / 3.0;
    else if (dev->regs[0xd1] & (1 << port))
        clock_src = 24000000.0 / 12.0;

    serial_remove(dev->uart[port]);
    if (dev->regs[0xc2] & (0x04 << port)) {
        serial_port = (dev->regs[0xc7 + port] << 2) & 0xfff8;
        serial_irq  = dev->irq_map[dev->regs[0xcb] >> ((port ^ 1) * 0xff)];

        serial_setup(dev->uart[port], serial_port, serial_irq);
    }

    serial_set_clock_src(dev->uart[port], clock_src);
}

static void
lpt_handler(gm82c803c_t *dev)
{
    uint16_t lpt_port     = 0x0000;
    uint16_t mask         = 0xfffc;
    uint8_t  local_enable = 1;
    uint8_t  lpt_irq      = LPT1_IRQ;
    uint8_t  lpt_dma      = 3;
    uint8_t  lpt_mode     = (dev->regs[0xc2] & 0x03);

    switch (lpt_mode) {
        default:
            local_enable = 0;
            break;
        case 0x00:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 1);
            break;
        case 0x01:
            lpt_set_epp(dev->lpt, !!(dev->regs[0xd0] & 0x20));
            lpt_set_ecp(dev->lpt, 1);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x02:
            mask = 0xfff8;
            lpt_set_epp(dev->lpt, 1);
            lpt_set_ecp(dev->lpt, !!(dev->regs[0xd0] & 0x20));
            lpt_set_ext(dev->lpt, 0);
            break;
    }

    lpt_port     = ((dev->regs[0xc6] << 2) & 0xfffc) & mask;

    lpt_irq      = dev->irq_map[dev->regs[0xca] & 0x0f];
    lpt_dma      = dev->dma_map[dev->regs[0xc9] & 0x03];

    if (lpt_irq > 15)
        lpt_irq = 0xff;

    lpt_port_remove(dev->lpt);
    lpt_set_fifo_threshold(dev->lpt, dev->regs[0x0a] & 0x0f);

    if (local_enable && (lpt_port >= 0x0100) && (lpt_port <= (0x0ffc & mask)))
        lpt_port_setup(dev->lpt, lpt_port);

    lpt_port_irq(dev->lpt, lpt_irq);
    lpt_port_dma(dev->lpt, lpt_dma);
}

static void
gm82c803c_write(uint16_t port, uint8_t val, void *priv)
{
    gm82c803c_t *dev    = (gm82c803c_t *) priv;
    uint8_t      valxor = 0;

    if (dev->tries == 2) {
        if (port == 0x0398) {
            if (val == 0xcc)
                dev->tries = 0;
            else
                dev->cur_reg = val;
        } else {
            if ((dev->cur_reg < 0xc2) || (dev->cur_reg > 0xd8))
                return;

            valxor                  = val ^ dev->regs[dev->cur_reg];
            dev->regs[dev->cur_reg] = val;

            switch (dev->cur_reg) {
                default:
                    break;
                case 0xc2:
                    if (valxor & 0x20)
                        ide_handler(dev);
                    if (valxor & 0x10)
                        fdc_handler(dev);
                    if (valxor & 0x08)
                        set_serial_addr(dev, 1);
                    if (valxor & 0x04)
                        set_serial_addr(dev, 0);
                    if (valxor & 0x03)
                        lpt_handler(dev);
                    break;
                case 0xc3:
                    if (valxor)
                        fdc_handler(dev);
                    break;
                case 0xc4: case 0xc5:
                    if (valxor)
                        ide_handler(dev);
                    break;
                case 0xc6:
                    if (valxor)
                        lpt_handler(dev);
                    break;
                case 0xc7:
                    if (valxor)
                        set_serial_addr(dev, 0);
                case 0xc8:
                    if (valxor)
                        set_serial_addr(dev, 1);
                case 0xc9:
                    if (valxor & 0xf0)
                        fdc_handler(dev);
                    if (valxor & 0x0f)
                        lpt_handler(dev);
                    break;
                case 0xca:
                    if (valxor & 0xf0)
                        fdc_handler(dev);
                    if (valxor & 0x0f)
                        lpt_handler(dev);
                    break;
                case 0xcb:
                    if (valxor & 0xf0)
                        set_serial_addr(dev, 0);
                    if (valxor & 0x0f)
                        set_serial_addr(dev, 1);
                    break;
                case 0xce:
                    if (valxor & 0x80)
                        set_serial_addr(dev, 1);
                    if (valxor & 0x40)
                        set_serial_addr(dev, 0);
                    break;
                case 0xd0:
                    if (valxor & 0x20)
                        lpt_handler(dev);
                    break;
                case 0xd1:
                    if (valxor & 0x02)
                        set_serial_addr(dev, 1);
                    if (valxor & 0x01)
                        set_serial_addr(dev, 0);
                    break;

                    if (valxor & 0x20)
                        ide_handler(dev);
                    if (valxor & 0x08)
                        set_serial_addr(dev, 1);
                    if (valxor & 0x04)
                        set_serial_addr(dev, 0);
                    break;
            }
        }
    } else if ((port == 0x0398) && (val == 0x33))
        dev->tries++;
}

static uint8_t
gm82c803c_read(uint16_t port, void *priv)
{
    const gm82c803c_t *dev = (gm82c803c_t *) priv;
    uint8_t            ret = 0xff;

    if (dev->tries == 2) {
        if ((port == 0x0399) && (dev->cur_reg >= 0xa0) && (dev->cur_reg <= 0xa1))
            ret = dev->regs[dev->cur_reg];
    }

    return ret;
}

static void
gm82c803c_reset(gm82c803c_t *dev)
{
    serial_remove(dev->uart[0]);
    serial_setup(dev->uart[0], COM1_ADDR, COM1_IRQ);

    serial_remove(dev->uart[1]);
    serial_setup(dev->uart[1], COM2_ADDR, COM2_IRQ);

    lpt_port_remove(dev->lpt);
    lpt_port_setup(dev->lpt, LPT1_ADDR);

    fdc_reset(dev->fdc);
    fdc_remove(dev->fdc);

    dev->tries = 0;
    memset(dev->regs, 0, 256);

    dev->regs[0xc0] = 0x3c;
    dev->regs[0xc2] = 0x03;
    dev->regs[0xc3] = 0x3c;
    dev->regs[0xc4] = 0x3c;
    dev->regs[0xc5] = 0x3d;
    dev->regs[0xd5] = 0x3c;

    set_serial_addr(dev, 0);
    set_serial_addr(dev, 1);

    lpt_handler(dev);

    fdc_handler(dev);

    if (dev->has_ide)
        ide_handler(dev);
}

static void
gm82c803c_close(void *priv)
{
    gm82c803c_t *dev = (gm82c803c_t *) priv;

    free(dev);
}

static void *
gm82c803c_init(const device_t *info)
{
    gm82c803c_t *dev = (gm82c803c_t *) calloc(1, sizeof(gm82c803c_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->has_ide = (info->local >> 8) & 0xff;

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->lpt = device_add_inst(&lpt_port_device, 1);
    lpt_set_cnfgb_readout(dev->lpt, 0x00);

    io_sethandler(0x0398, 0x0002,
                  gm82c803c_read, NULL, NULL, gm82c803c_write, NULL, NULL, dev);

    dev->dma_map[0] = 4;
    for (int i = 1; i < 4; i++)
        dev->dma_map[i] = i;

    memset(dev->irq_map, 0xff, 16);
    dev->irq_map[0]  = 0xff;
    for (int i = 1; i < 7; i++)
         dev->irq_map[i] = i;
    dev->irq_map[1]  = 5;
    dev->irq_map[5]  = 7;
    dev->irq_map[7]  = 0xff;    /* Reserved. */
    dev->irq_map[8]  = 10;

    gm82c803c_reset(dev);

    return dev;
}

const device_t gm82c803c_device = {
    .name          = "Goldstar GM82C803C",
    .internal_name = "gm82c803c",
    .flags         = 0,
    .local         = 0,
    .init          = gm82c803c_init,
    .close         = gm82c803c_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
