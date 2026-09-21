/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the NatSemi PC87307 Super I/O chip.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020-2025 Miran Grca.
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
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/sio.h>
#include <86box/random.h>
#include <86box/plat_fallthrough.h>
#include "cpu.h"

typedef struct pc87307_t {
    uint8_t   id;
    uint8_t   baddr;
    uint8_t   pm_idx;
    uint8_t   regs[48];
    uint8_t   ld_regs[256][256];
    uint8_t   pcregs[16];
    uint8_t   gpio[2][8];
    uint8_t   pm[8];
    uint16_t  superio_base;
    uint16_t  gpio_base;
    uint16_t  gpio_base2;
    uint16_t  pm_base;
    int       cur_reg;
    void     *kbc;
    fdc_t    *fdc;
    serial_t *uart[2];
    lpt_t    *lpt;
} pc87307_t;

enum {
    LD_KBD   = 0,
    LD_MOUSE,
    LD_RTC,
    LD_FDC,
    LD_LPT,
    LD_UART2,
    LD_UART1,
    LD_GPIO,
    LD_PM
} pc87307_ld_t;

#define LD_MIN LD_KBD
#define LD_MAX LD_PM

static void    fdc_handler(pc87307_t *dev);
static void    lpt_handler(pc87307_t *dev);
static void    serial_handler(pc87307_t *dev, int uart);
static void    kbc_handler(pc87307_t *dev);
static void    pc87307_write(uint16_t port, uint8_t val, void *priv);
static uint8_t pc87307_read(uint16_t port, void *priv);

#ifdef ENABLE_PC87307_LOG
int pc87307_do_log = ENABLE_PC87307_LOG;

static void
pc87307_log(const char *fmt, ...)
{
    va_list ap;

    if (pc87307_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pc87307_log(fmt, ...)
#endif

static void
pc87307_gpio_write(uint16_t port, uint8_t val, void *priv)
{
    pc87307_t *dev  = (pc87307_t *) priv;
    uint8_t    bank = !!(dev->regs[0x22] & 0x80);

    /* Bit 7 of SCNF2 = bank. */
    pc87307_log("[%04X:%08X] [W] (%04X) Bank %i = %02X\n",
                CS, cpu_state.pc, port, bank, val);

    dev->gpio[bank][port & 0x0007] = val;

    if (bank == 0) {
        machine_handle_gpio(1, ((dev->gpio[0][5] & dev->gpio[0][4]) << 8) | (dev->gpio[0][0] & dev->gpio[0][1]));
    }
}

uint8_t
pc87307_gpio_read(uint16_t port, void *priv)
{
    const pc87307_t *dev  = (pc87307_t *) priv;
    uint8_t          pins = 0xff;
    uint8_t          bank = !!(dev->regs[0x22] & 0x80);
    uint8_t          ret  = dev->gpio[bank][port & 0x0007];

    switch (port & 0x0007) {
        case 0x0000:
            if (bank == 0) {
                uint8_t mask = dev->gpio[0][1];
                pins = machine_handle_gpio(0, 0xFFFF) & 0xFF;
                ret  = (ret & mask) | (pins & ~mask);
            }
            break;
        case 0x0004:
            if (bank == 0) {
                uint8_t mask = dev->gpio[0][5];
                pins = (machine_handle_gpio(0, 0xFFFF) >> 8) & 0xFF;
                ret  = (ret & mask) | (pins & ~mask);
            } else
                ret = 0xff;
            break;

        default:
            if (bank == 1)
                ret = 0xff;
            break;
    }

    /* Bit 7 of SCNF2 = bank. */
    pc87307_log("[%04X:%08X] [R] (%04X) Bank %i = %02X\n",
                CS, cpu_state.pc, port, bank, ret);

    return ret;
}

static void
pc87307_gpio_remove(pc87307_t *dev)
{
    if (dev->gpio_base != 0xffff) {
        io_removehandler(dev->gpio_base, 0x0008,
                         pc87307_gpio_read, NULL, NULL, pc87307_gpio_write, NULL, NULL, dev);
        dev->gpio_base = 0xffff;
    }

    if (dev->gpio_base2 != 0xffff) {
        io_removehandler(dev->gpio_base2, 0x0008,
                         pc87307_gpio_read, NULL, NULL, pc87307_gpio_write, NULL, NULL, dev);
        dev->gpio_base2 = 0xffff;
    }
}

static void
pc87307_gpio_init(pc87307_t *dev, int bank, uint16_t addr)
{
    uint16_t *bank_base = bank ? &(dev->gpio_base2) : &(dev->gpio_base);

    *bank_base = addr;

    io_sethandler(*bank_base, 0x0008,
                  pc87307_gpio_read, NULL, NULL, pc87307_gpio_write, NULL, NULL, dev);
}

static void
pc87307_pm_write(uint16_t port, uint8_t val, void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    if (port & 1)
        dev->pm[dev->pm_idx] = val;
    else {
        dev->pm_idx = val & 0x07;

        switch (dev->pm_idx) {
            case 0x00:
                fdc_handler(dev);
                lpt_handler(dev);
                serial_handler(dev, 1);
                serial_handler(dev, 0);
                break;

            default:
                break;
        }
    }
}

uint8_t
pc87307_pm_read(uint16_t port, void *priv)
{
    const pc87307_t *dev = (pc87307_t *) priv;

    if (port & 1)
        return dev->pm[dev->pm_idx];
    else
        return dev->pm_idx;
}

static void
pc87307_pm_remove(pc87307_t *dev)
{
    if (dev->pm_base != 0xffff) {
        io_removehandler(dev->pm_base, 0x0008,
                         pc87307_pm_read, NULL, NULL, pc87307_pm_write, NULL, NULL, dev);
        dev->pm_base = 0xffff;
    }
}

static void
pc87307_pm_init(pc87307_t *dev, uint16_t addr)
{
    dev->pm_base = addr;

    io_sethandler(dev->pm_base, 0x0008,
                  pc87307_pm_read, NULL, NULL, pc87307_pm_write, NULL, NULL, dev);
}

static void
kbc_handler(pc87307_t *dev)
{
    uint8_t  active   = (dev->ld_regs[LD_KBD][0x00] & 0x01) &&
                        (dev->pm[0x00] & 0x01);
    uint8_t  active_2 = dev->ld_regs[LD_MOUSE][0x00] & 0x01;
    uint8_t  irq      = (dev->ld_regs[LD_KBD][0x40] & 0x0f);
    uint8_t  irq_2    = (dev->ld_regs[LD_MOUSE][0x40] & 0x0f);
    uint16_t addr     = (dev->ld_regs[LD_KBD][0x30] << 8) |
                        dev->ld_regs[LD_KBD][0x31];
    uint16_t addr_2   = (dev->ld_regs[LD_KBD][0x32] << 8) |
                        dev->ld_regs[LD_KBD][0x33];

    pc87307_log("%02X, %02X, %02X, %02X, %04X, %04X\n",
                active, active_2, irq, irq_2, addr, addr_2);

    if (addr <= 0xfff8) {
        pc87307_log("Enabling KBC #1 on %04X...\n", addr);
        kbc_at_port_handler(0, active, addr,   dev->kbc);
    }

    if (addr_2 <= 0xfff8) {
        pc87307_log("Enabling KBC #2 on %04X...\n", addr_2);
        kbc_at_port_handler(1, active, addr_2, dev->kbc);
    }

    kbc_at_set_irq(0, active               ? irq   : 0xffff, dev->kbc);
    kbc_at_set_irq(1, (active && active_2) ? irq_2 : 0xffff, dev->kbc);
}

static void
fdc_handler(pc87307_t *dev)
{
    fdc_remove(dev->fdc);

    uint8_t  active = (dev->ld_regs[LD_FDC][0x00] & 0x01) &&
                      (dev->pm[0x00] & 0x08);
    uint8_t  irq    = (dev->ld_regs[LD_FDC][0x40] & 0x0f);
    uint8_t  dma    = (dev->ld_regs[LD_FDC][0x44] & 0x0f);
    uint16_t addr   = ((dev->ld_regs[LD_FDC][0x30] << 8) |
                      dev->ld_regs[LD_FDC][0x31]) & 0xfff8;

    if (active && (addr <= 0xfff8)) {
        pc87307_log("Enabling FDC on %04X, IRQ %i...\n", addr, irq);
        fdc_set_base(dev->fdc, addr);
        fdc_set_irq(dev->fdc, irq);
        fdc_set_dma_ch(dev->fdc, dma);
    }
}

static void
lpt_handler(pc87307_t *dev)
{
    uint8_t  active = (dev->ld_regs[LD_LPT][0x00] & 0x01) &&
                      (dev->pm[0x00] & 0x10);
    uint8_t  irq    = (dev->ld_regs[LD_LPT][0x40] & 0x0f);
    uint8_t  dma    = (dev->ld_regs[LD_LPT][0x44] & 0x0f);
    uint16_t addr   = (dev->ld_regs[LD_LPT][0x30] << 8) |
                      dev->ld_regs[LD_LPT][0x31];
    uint8_t  mode   = (dev->ld_regs[LD_LPT][0xf0] >> 7);
    uint16_t mask   = 0xfffc;

    if (irq > 15)
        irq = 0xff;

    if (dma >= 4)
        dma = 0xff;

    lpt_port_remove(dev->lpt);

    switch (mode) {
        default:
        case 0x00:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x01:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 1);
            break;
        case 0x02: case 0x03:
            mask = 0xfff8;
            lpt_set_epp(dev->lpt, 1);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x04:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 1);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x07:
            mask = 0xfff8;
            lpt_set_epp(dev->lpt, 1);
            lpt_set_ecp(dev->lpt, 1);
            lpt_set_ext(dev->lpt, 0);
            break;
    }

    lpt_set_cfg_regs_enabled(dev->lpt, !!(dev->ld_regs[LD_LPT][0xf0] & 0x10));

    if (active && (addr <= (0xfffc & mask))) {
        pc87307_log("Enabling LPT1 on %04X...\n", addr);
        lpt_port_setup(dev->lpt, addr & mask);
    } else
        lpt_port_setup(dev->lpt, 0xffff);

    lpt_port_irq(dev->lpt, irq);
    lpt_port_dma(dev->lpt, dma);
}

static void
serial_handler(pc87307_t *dev, int uart)
{
    serial_remove(dev->uart[uart]);

    uint8_t  active = (dev->ld_regs[LD_UART1 - uart][0x00] & 0x01) &&
                      (dev->pm[0x00] & (1 << (6 - uart)));
    uint8_t  irq    = (dev->ld_regs[LD_UART1 - uart][0x40] & 0x0f);
    uint16_t addr   = (dev->ld_regs[LD_UART1 - uart][0x30] << 8) |
                      dev->ld_regs[LD_UART1 - uart][0x31];

    if (active && (addr <= 0xfff8)) {
        pc87307_log("Enabling COM%i on %04X...\n", uart + 1, addr);
        serial_setup(dev->uart[uart], addr, irq);
    } else
        serial_setup(dev->uart[uart], 0x0000, irq);
}

static void
gpio_handler(pc87307_t *dev)
{
    pc87307_gpio_remove(dev);

    uint8_t  active = (dev->ld_regs[LD_GPIO][0x00] & 0x01);
    uint16_t addr   = (dev->ld_regs[LD_GPIO][0x30] << 8) |
                      dev->ld_regs[LD_GPIO][0x31];
    uint16_t addr_2 = (dev->ld_regs[LD_GPIO][0x32] << 8) |
                      dev->ld_regs[LD_GPIO][0x33];

    if (active) {
        pc87307_log("Enabling GPIO #1 on %04X...\n", addr);
        pc87307_gpio_init(dev, 0, addr);
        pc87307_log("Enabling GPIO #2 on %04X...\n", addr_2);
        pc87307_gpio_init(dev, 1, addr_2);
    }
}

static void
pm_handler(pc87307_t *dev)
{
    pc87307_pm_remove(dev);

    uint8_t  active = (dev->ld_regs[LD_PM][0x00] & 0x01);
    uint16_t addr   = (dev->ld_regs[LD_PM][0x30] << 8) |
                      dev->ld_regs[LD_PM][0x31];

    if (active) {
        pc87307_log("Enabling power management on %04X...\n", addr);
        pc87307_pm_init(dev, addr);
    }
}

static void
superio_handler(pc87307_t *dev)
{
    if (dev->superio_base != 0x0000)
        io_removehandler(dev->superio_base, 0x0002,
                         pc87307_read, NULL, NULL,
                         pc87307_write, NULL, NULL, dev);

    switch (dev->regs[0x22] & 0x03) {
        default:
            dev->superio_base = 0x0000;
            break;        case 0x02:
            dev->superio_base = 0x015c;
            break;
        case 0x03:
            dev->superio_base = 0x002e;
            break;
    }

    if (dev->superio_base != 0x0000) {
        pc87307_log("Enabling Super I/O on %04X...\n", dev->superio_base);
        io_sethandler(dev->superio_base, 0x0002,
                      pc87307_read, NULL, NULL,
                      pc87307_write, NULL, NULL, dev);
    }
}

static void
pc87307_write(uint16_t port, uint8_t val, void *priv)
{
    pc87307_t *dev   = (pc87307_t *) priv;
    uint8_t    ld    = dev->regs[0x07];
    uint8_t    reg   = dev->cur_reg - 0x30;
    uint8_t    index = (port & 1) ? 0 : 1;
    uint8_t    old   = dev->regs[dev->cur_reg];

    if (index) {
        dev->cur_reg = val;
        return;
    } else {
#ifdef ENABLE_PC87307_LOG
        if (dev->cur_reg >= 0x30)
            pc87307_log("[%04X:%08X] [W] (%04X) %02X:%02X = %02X\n",
                        CS, cpu_state.pc, port, ld, dev->cur_reg, val);
        else
            pc87307_log("[%04X:%08X] [W] (%04X)    %02X = %02X\n",
                        CS, cpu_state.pc, port,     dev->cur_reg, val);
#endif
        switch (dev->cur_reg) {
            case 0x00:
            case 0x02: case 0x03:
            case 0x06: case 0x07:
                dev->regs[dev->cur_reg] = val;
                break;
            case 0x21:
                dev->regs[dev->cur_reg] = val;
                fdc_toggle_flag(dev->fdc, FDC_FLAG_PS2_MCA, !(val & 0x04));
                break;
            case 0x22:
                dev->regs[dev->cur_reg] = val;
                superio_handler(dev);
                break;
            case 0x23:
                dev->regs[dev->cur_reg] = (old & 0xf0) | (val & 0x0f);
                break;
            case 0x24:
                dev->pcregs[dev->regs[0x23]] = val;
                break;
            default:
                if (dev->cur_reg >= 0x30)
                    old = dev->ld_regs[ld][reg];
                break;
        }
    }

    switch (dev->cur_reg) {
        case 0x30:
            switch (ld) {
                default:
                    break;
                case LD_KBD: case LD_MOUSE:
                    dev->ld_regs[ld][reg] = val;
                    kbc_handler(dev);
                    break;
                case LD_RTC:
                    dev->ld_regs[ld][reg] = val;
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    fdc_handler(dev);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = val;
                    lpt_handler(dev);
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 1);
                    break;
                case LD_UART1:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 0);
                    break;
                case LD_GPIO:
                    dev->ld_regs[ld][reg] = val;
                    gpio_handler(dev);
                    break;
                case LD_PM:
                    dev->ld_regs[ld][reg] = val;
                    pm_handler(dev);
                    break;
            }
            break;
        /* I/O Range Check. */
        case 0x31:
            switch (ld) {
                default:
                    break;
                case LD_MIN ... LD_MAX:
                    if (ld != LD_MOUSE)
                        dev->ld_regs[ld][reg] = val;
                    break;
            }
            break;
        /* Base Address 0 MSB. */
        case 0x60:
            switch (ld) {
                default:
                    break;
                case LD_KBD:
                    dev->ld_regs[ld][reg] = val;
                    kbc_handler(dev);
                    break;
                case LD_RTC:
                    dev->ld_regs[ld][reg] = val;
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    fdc_handler(dev);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = (old & 0xfc) | (val & 0x03);
                    lpt_handler(dev);
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 1);
                    break;
                case LD_UART1:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 0);
                    break;
                case LD_GPIO:
                    dev->ld_regs[ld][reg] = val;
                    gpio_handler(dev);
                    break;
                case LD_PM:
                    dev->ld_regs[ld][reg] = val;
                    pm_handler(dev);
                    break;
            }
            break;
        /* Base Address 0 LSB. */
        case 0x61:
            switch (ld) {
                default:
                    break;
                case LD_KBD:
                    dev->ld_regs[ld][reg] = (old & 0x04) | (val & 0xfb);
                    kbc_handler(dev);
                    break;
                case LD_RTC:
                    dev->ld_regs[ld][reg] = (old & 0x01) | (val & 0xfe);
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = (old & 0x07) | (val & 0xf8);
                    fdc_handler(dev);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = (old & 0x03) | (val & 0xfc);
                    lpt_handler(dev);
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = (old & 0x07) | (val & 0xf8);
                    serial_handler(dev, 1);
                    break;
                case LD_UART1:
                    dev->ld_regs[ld][reg] = (old & 0x07) | (val & 0xf8);
                    serial_handler(dev, 0);
                    break;
                case LD_GPIO:
                    dev->ld_regs[ld][reg] = (old & 0x07) | (val & 0xf8);
                    gpio_handler(dev);
                    break;
                case LD_PM:
                    dev->ld_regs[ld][reg] = (old & 0x01) | (val & 0xfe);
                    pm_handler(dev);
                    break;
            }
            break;
        /* Base Address 1 MSB (undocumented for Logical Device 7). */
        case 0x62:
            switch (ld) {
                default:
                    break;
                case LD_KBD:
                    dev->ld_regs[ld][reg] = val;
                    kbc_handler(dev);
                    break;
                case LD_GPIO:
                    dev->ld_regs[ld][reg] = val;
                    gpio_handler(dev);
                    break;
            }
            break;
        /* Base Address 1 LSB (undocumented for Logical Device 7). */
        case 0x63:
            switch (ld) {
                default:
                    break;
                case LD_KBD:
                    dev->ld_regs[ld][reg] = (old & 0x04) | (val & 0xfb);
                    kbc_handler(dev);
                    break;
                case LD_GPIO:
                    dev->ld_regs[ld][reg] = (old & 0x01) | (val & 0xfe);
                    gpio_handler(dev);
                    break;
            }
            break;
        /* Interrupt Select. */
        case 0x70:
            switch (ld) {
                default:
                    break;
                case LD_KBD: case LD_MOUSE:
                    dev->ld_regs[ld][reg] = val;
                    kbc_handler(dev);
                    break;
                case LD_RTC:
                    dev->ld_regs[ld][reg] = val;
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    fdc_handler(dev);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = val;
                    lpt_handler(dev);
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 1);
                    break;
                case LD_UART1:
                    dev->ld_regs[ld][reg] = val;
                    serial_handler(dev, 0);
                    break;
            }
            break;
        /* Interrupt Type. */
        case 0x71:
            switch (ld) {
                default:
                    break;
                case LD_MIN ... LD_MAX:
                    if ((ld == LD_KBD) || (ld == LD_MOUSE))
                        dev->ld_regs[ld][reg] = (old & 0xfc) | (val & 0x03);
                    else
                        dev->ld_regs[ld][reg] = (old & 0xfd) | (val & 0x02);
                    break;
            }
            break;
        /* DMA Channel Select 0. */
        case 0x74:
            switch (ld) {
                default:
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    fdc_handler(dev);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = val;
                    lpt_handler(dev);
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = val;
                    break;
            }
            break;
        /* DMA Channel Select 1. */
        case 0x75:
            switch (ld) {
                default:
                    break;
                case LD_UART2:
                    dev->ld_regs[ld][reg] = val;
                    break;
            }
            break;
        /* Configuration Register 0. */
        case 0xf0:
            switch (ld) {
                default:
                    break;
                case LD_KBD:
                    dev->ld_regs[ld][reg] = val;
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    fdc_update_densel_polarity(dev->fdc, (val & 0x20) ? 1 : 0);
                    fdc_update_enh_mode(dev->fdc, (val & 0x40) ? 1 : 0);
                    break;
                case LD_LPT:
                    dev->ld_regs[ld][reg] = val;
                    lpt_handler(dev);
                    break;
                case LD_UART2: case LD_UART1:
                    dev->ld_regs[ld][reg] = val;
                    break;
            }
            break;
        /* Configuration Register 1. */
        case 0xf1:
            switch (ld) {
                default:
                    break;
                case LD_FDC:
                    dev->ld_regs[ld][reg] = val;
                    break;
            }
            break;

        default:
            break;
    }
}

static uint8_t
pc87307_read(uint16_t port, void *priv)
{
    const pc87307_t *dev   = (pc87307_t *) priv;
    uint8_t          ld    = dev->regs[0x07];
    uint8_t          reg   = dev->cur_reg - 0x30;
    uint8_t          index = (port & 1) ? 0 : 1;
    uint8_t          ret   = 0xff;

    if (index)
        ret = dev->cur_reg;
    else {
        if (dev->cur_reg >= 0x30)
            ret = dev->ld_regs[ld][reg];
        else if (dev->cur_reg == 0x24)
            ret = dev->pcregs[dev->regs[0x23]];
        /* Write-only registers. */
        else if ((dev->cur_reg == 0x00) ||
                 (dev->cur_reg == 0x02) || (dev->cur_reg == 0x03))
            ret = 0x00;
        else
            ret = dev->regs[dev->cur_reg];
#ifdef EANBLE_PC87307_LOG
        if (dev->cur_reg >= 0x30)
            pc87307_log("[%04X:%08X] [R] (%04X) %02X:%02X = %02X\n",
                        CS, cpu_state.pc, port, ld, dev->cur_reg, ret);
        else
            pc87307_log("[%04X:%08X] [R] (%04X)    %02X = %02X\n",
                        CS, cpu_state.pc, port,     dev->cur_reg, ret);
#endif
    }

    return ret;
}

void
pc87307_reset(void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    memset(dev->regs, 0x00, 0x30);
    for (uint16_t i = 0; i < 256; i++)
        memset(dev->ld_regs[i], 0x00, 0xd0);
    memset(dev->pcregs, 0x00, 0x10);
    memset(dev->gpio, 0xff, 0x08);
    memset(dev->pm, 0x00, 0x08);

    dev->regs[0x20] = dev->id;
    dev->regs[0x21] = 0x04;
    dev->regs[0x22] = dev->baddr;

    dev->ld_regs[LD_KBD  ][0x00] = 0x01;
    dev->ld_regs[LD_KBD  ][0x31] = 0x60;
    dev->ld_regs[LD_KBD  ][0x33] = 0x64;
    dev->ld_regs[LD_KBD  ][0x40] = 0x01;
    dev->ld_regs[LD_KBD  ][0x41] = 0x02;
    dev->ld_regs[LD_KBD  ][0x44] = 0x04;
    dev->ld_regs[LD_KBD  ][0x45] = 0x04;
    dev->ld_regs[LD_KBD  ][0xc0] = 0x40;

    dev->ld_regs[LD_MOUSE][0x40] = 0x0c;
    dev->ld_regs[LD_MOUSE][0x41] = 0x02;
    dev->ld_regs[LD_MOUSE][0x44] = 0x04;
    dev->ld_regs[LD_MOUSE][0x45] = 0x04;

    dev->ld_regs[LD_RTC  ][0x00] = 0x01;
    dev->ld_regs[LD_RTC  ][0x31] = 0x70;
    dev->ld_regs[LD_RTC  ][0x40] = 0x08;
    dev->ld_regs[LD_RTC  ][0x44] = 0x04;
    dev->ld_regs[LD_RTC  ][0x45] = 0x04;

    dev->ld_regs[LD_FDC  ][0x01] = 0x01;
    dev->ld_regs[LD_FDC  ][0x30] = 0x03;
    dev->ld_regs[LD_FDC  ][0x31] = 0xf0;
    dev->ld_regs[LD_FDC  ][0x32] = 0x03;
    dev->ld_regs[LD_FDC  ][0x33] = 0xf7;
    dev->ld_regs[LD_FDC  ][0x40] = 0x06;
    dev->ld_regs[LD_FDC  ][0x41] = 0x03;
    dev->ld_regs[LD_FDC  ][0x44] = 0x02;
    dev->ld_regs[LD_FDC  ][0x45] = 0x04;
    dev->ld_regs[LD_FDC  ][0xc0] = 0x02;

    dev->ld_regs[LD_LPT  ][0x30] = 0x02;
    dev->ld_regs[LD_LPT  ][0x31] = 0x78;
    dev->ld_regs[LD_LPT  ][0x40] = 0x07;
    dev->ld_regs[LD_LPT  ][0x44] = 0x04;
    dev->ld_regs[LD_LPT  ][0x45] = 0x04;
    dev->ld_regs[LD_LPT  ][0xc0] = 0xf2;

    dev->ld_regs[LD_UART2][0x30] = 0x02;
    dev->ld_regs[LD_UART2][0x31] = 0xf8;
    dev->ld_regs[LD_UART2][0x40] = 0x03;
    dev->ld_regs[LD_UART2][0x41] = 0x03;
    dev->ld_regs[LD_UART2][0x44] = 0x04;
    dev->ld_regs[LD_UART2][0x45] = 0x04;
    dev->ld_regs[LD_UART2][0xc0] = 0x02;

    dev->ld_regs[LD_UART1][0x30] = 0x03;
    dev->ld_regs[LD_UART1][0x31] = 0xf8;
    dev->ld_regs[LD_UART1][0x40] = 0x04;
    dev->ld_regs[LD_UART1][0x41] = 0x03;
    dev->ld_regs[LD_UART1][0x44] = 0x04;
    dev->ld_regs[LD_UART1][0x45] = 0x04;
    dev->ld_regs[LD_UART1][0xc0] = 0x02;

    dev->ld_regs[LD_GPIO ][0x44] = 0x04;
    dev->ld_regs[LD_GPIO ][0x45] = 0x04;

    dev->ld_regs[LD_PM   ][0x44] = 0x04;
    dev->ld_regs[LD_PM   ][0x45] = 0x04;

    dev->gpio[0][0] = 0xff;
    dev->gpio[0][1] = 0x00;
    dev->gpio[0][2] = 0x00;
    dev->gpio[0][3] = 0xff;
    dev->gpio[0][4] = 0xff;
    dev->gpio[0][5] = 0x00;
    dev->gpio[0][6] = 0x00;
    dev->gpio[0][7] = 0xff;
    dev->gpio[1][1] = 0x00;

    dev->pm[0] = 0xff;
    dev->pm[1] = 0xff;
    dev->pm[4] = 0x0e;
    dev->pm[7] = 0x01;

    dev->gpio_base = dev->pm_base = 0xffff;

    /*
        0 = 360 rpm @ 500 kbps for 3.5"
        1 = Default, 300 rpm @ 500, 300, 250, 1000 kbps for 3.5"
    */
    fdc_toggle_flag(dev->fdc, FDC_FLAG_PS2_MCA, 0);
    fdc_reset(dev->fdc);

    kbc_handler(dev);
    fdc_handler(dev);
    lpt_handler(dev);
    serial_handler(dev, 0);
    serial_handler(dev, 1);
    gpio_handler(dev);
    pm_handler(dev);
    superio_handler(dev);
}

static void
pc87307_close(void *priv)
{
    pc87307_t *dev = (pc87307_t *) priv;

    free(dev);
}

static void *
pc87307_init(const device_t *info)
{
    pc87307_t *dev = (pc87307_t *) calloc(1, sizeof(pc87307_t));

    dev->id = info->local & 0xff;

    dev->fdc = device_add(&fdc_at_nsc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->lpt     = device_add_inst(&lpt_port_device, 1);
    lpt_set_cnfga_readout(dev->lpt, 0x14);

    switch (info->local & PCX730X_KBC) {
        case PCX730X_AMI:
        default:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_AMI | 0x00003500));
            break;
        /* Optiplex! */
        case PCX730X_PHOENIX_42:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_PHOENIX | 0x00013700));
            break;
        case PCX730X_PHOENIX_42I:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_PHOENIX | 0x00041600));
            break;
    }

    if (info->local & PCX730X_15C)
        dev->baddr = 0x02;
    else
        dev->baddr = 0x03;

    pc87307_reset(dev);

    return dev;
}

const device_t pc87307_device = {
    .name          = "National Semiconductor PC87307 Super I/O",
    .internal_name = "pc87307",
    .flags         = 0,
    .local         = 0x1c0,
    .init          = pc87307_init,
    .close         = pc87307_close,
    .reset         = pc87307_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc87307_15c_device = {
    .name          = "National Semiconductor PC87307 Super I/O (Port 15Ch)",
    .internal_name = "pc87307_15c",
    .flags         = 0,
    .local         = 0x2c0,
    .init          = pc87307_init,
    .close         = pc87307_close,
    .reset         = pc87307_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc87307_both_device = {
    .name          = "National Semiconductor PC87307 Super I/O (Ports 2Eh and 15Ch)",
    .internal_name = "pc87307_both",
    .flags         = 0,
    .local         = 0x3c0,
    .init          = pc87307_init,
    .close         = pc87307_close,
    .reset         = pc87307_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc97307_device = {
    .name          = "National Semiconductor PC97307 Super I/O",
    .internal_name = "pc97307",
    .flags         = 0,
    .local         = 0x1cf,
    .init          = pc87307_init,
    .close         = pc87307_close,
    .reset         = pc87307_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
