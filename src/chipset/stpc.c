/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the STMicroelectronics STPC series of SoCs.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2020 RichardG.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/device.h>
#include <86box/port_92.h>
#include <86box/usb.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/serial.h>
#include <86box/lpt.h>
#include <86box/chipset.h>

#define STPC_CONSUMER2 0x104a020b
#define STPC_ATLAS     0x104a0210
#define STPC_ELITE     0x104a021a
#define STPC_CLIENT    0x100e55cc

typedef struct stpc_t {
    uint32_t local;

    /* Main registers (port 22h/23h) */
    uint8_t reg_offset;
    uint8_t regs[256];

    /* Host bus interface */
    uint16_t host_base;
    uint8_t  host_offset;
    uint8_t  host_regs[256];

    /* Local bus */
    uint16_t localbus_base;
    uint8_t  localbus_offset;
    uint8_t  localbus_regs[256];

    /* PCI devices */
    uint8_t     pci_conf[4][256];
    smram_t    *smram;
    usb_t      *usb;
    int         ide_slot;
    int         usb_slot;
    sff8038i_t *bm[2];

    /* Miscellaneous */
    usb_params_t usb_params;
} stpc_t;

typedef struct stpc_serial_t {
    serial_t *uart[2];
} stpc_serial_t;

typedef struct stpc_lpt_t {
    uint8_t unlocked;
    uint8_t offset;
    uint8_t reg1;
    uint8_t reg4;
} stpc_lpt_t;

#ifdef ENABLE_STPC_LOG
int stpc_do_log = ENABLE_STPC_LOG;

static void
stpc_log(const char *fmt, ...)
{
    va_list ap;

    if (stpc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define stpc_log(fmt, ...)
#endif

static void
stpc_recalcmapping(stpc_t *dev)
{
    uint8_t  reg, bitpair;
    uint32_t base, size;
    int      state;

    shadowbios       = 0;
    shadowbios_write = 0;

    for (reg = 0; reg <= 3; reg++) {
        for (bitpair = 0; bitpair <= ((reg == 3) ? 0 : 3); bitpair++) {
            if (reg == 3) {
                size = 0x10000;
                base = 0xf0000;
            } else {
                size = 0x4000;
                base = 0xc0000 + (size * ((reg * 4) + bitpair));
            }
            stpc_log("STPC: Shadowing for %05X-%05X (reg %02X bp %d wmask %02X rmask %02X) =", base, base + size - 1, 0x25 + reg, bitpair, 1 << (bitpair * 2), 1 << ((bitpair * 2) + 1));

            state = 0;
            if (dev->regs[0x25 + reg] & (1 << (bitpair * 2))) {
                stpc_log(" w on");
                state |= MEM_WRITE_INTERNAL;
                if (base >= 0xe0000)
                    shadowbios_write |= 1;
            } else {
                stpc_log(" w off");
                state |= MEM_WRITE_EXTANY;
            }
            if (dev->regs[0x25 + reg] & (1 << ((bitpair * 2) + 1))) {
                stpc_log("; r on\n");
                state |= MEM_READ_INTERNAL;
                if (base >= 0xe0000)
                    shadowbios |= 1;
            } else {
                stpc_log("; r off\n");
                state |= MEM_READ_EXTANY;
            }

            mem_set_mem_state(base, size, state);
        }
    }

    flushmmucache_nopc();
}

static void
stpc_host_write(uint16_t addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: host_write(%04X, %02X)\n", addr, val);

    if (addr == dev->host_base)
        dev->host_offset = val;
    else if (addr == (dev->host_base + 4))
        dev->host_regs[dev->host_offset] = val;
}

static uint8_t
stpc_host_read(uint16_t addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (addr == dev->host_base)
        ret = dev->host_offset;
    else if (addr == (dev->host_base + 4))
        ret = dev->host_regs[dev->host_offset];
    else
        ret = 0xff;

    stpc_log("STPC: host_read(%04X) = %02X\n", addr, ret);
    return ret;
}

static void
stpc_localbus_write(uint16_t addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: localbus_write(%04X, %02X)\n", addr, val);

    if (addr == dev->localbus_base)
        dev->localbus_offset = val;
    else if (addr == (dev->localbus_base + 4))
        dev->localbus_regs[addr] = val;
}

static uint8_t
stpc_localbus_read(uint16_t addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (addr == dev->localbus_base)
        ret = dev->localbus_offset;
    else if (addr == (dev->localbus_base + 4))
        ret = dev->localbus_regs[dev->localbus_offset];
    else
        ret = 0xff;

    stpc_log("STPC: localbus_read(%04X) = %02X\n", addr, ret);
    return ret;
}

static void
stpc_nb_write(int func, int addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: nb_write(%d, %02X, %02X)\n", func, addr, val);

    if (func > 0)
        return;

    switch (addr) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0e:
        case 0x51:
        case 0x53:
        case 0x54:
            return;

        case 0x05:
            val &= 0x01;
            break;

        case 0x50:
            val &= 0x1f;
            break;

        case 0x52:
            val &= 0x70;
            break;
    }

    dev->pci_conf[0][addr] = val;
}

static uint8_t
stpc_nb_read(int func, int addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (func > 0)
        ret = 0xff;
    else
        ret = dev->pci_conf[0][addr];

    stpc_log("STPC: nb_read(%d, %02X) = %02X\n", func, addr, ret);
    return ret;
}

static void
stpc_ide_handlers(stpc_t *dev, int bus)
{
    uint16_t main, side;

    if (bus & 0x01) {
        ide_pri_disable();

        if (dev->pci_conf[2][0x09] & 0x01) {
            main = (dev->pci_conf[2][0x11] << 8) | (dev->pci_conf[2][0x10] & 0xf8);
            side = ((dev->pci_conf[2][0x15] << 8) | (dev->pci_conf[2][0x14] & 0xfc)) + 2;
        } else {
            main = 0x1f0;
            side = 0x3f6;
        }

        ide_set_base(0, main);
        ide_set_side(0, side);

        stpc_log("STPC: IDE primary main %04X side %04X enable ", main, side);
        if ((dev->pci_conf[2][0x04] & 0x01) && !(dev->pci_conf[2][0x48] & 0x04)) {
            stpc_log("1\n");
            ide_pri_enable();
        } else {
            stpc_log("0\n");
        }
    }

    if (bus & 0x02) {
        ide_sec_disable();

        if (dev->pci_conf[2][0x09] & 0x04) {
            main = (dev->pci_conf[2][0x19] << 8) | (dev->pci_conf[2][0x18] & 0xf8);
            side = ((dev->pci_conf[2][0x1d] << 8) | (dev->pci_conf[2][0x1c] & 0xfc)) + 2;
        } else {
            main = 0x170;
            side = 0x376;
        }

        ide_set_base(1, main);
        ide_set_side(1, side);

        stpc_log("STPC: IDE secondary main %04X side %04X enable ", main, side);
        if ((dev->pci_conf[2][0x04] & 0x01) && !(dev->pci_conf[2][0x48] & 0x08)) {
            stpc_log("1\n");
            ide_sec_enable();
        } else {
            stpc_log("0\n");
        }
    }
}

static void
stpc_ide_bm_handlers(stpc_t *dev)
{
    uint16_t base = (dev->pci_conf[2][0x20] & 0xf0) | (dev->pci_conf[2][0x21] << 8);

    sff_bus_master_handler(dev->bm[0], dev->pci_conf[2][0x04] & 1, base);
    sff_bus_master_handler(dev->bm[1], dev->pci_conf[2][0x04] & 1, base + 8);
}

static void
stpc_ide_write(int func, int addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: ide_write(%d, %02X, %02X)\n", func, addr, val);

    if (func > 0)
        return;

    switch (addr) {
        case 0x04:
            dev->pci_conf[2][addr] = (dev->pci_conf[2][addr] & 0xbe) | (val & 0x41);
            stpc_ide_handlers(dev, 0x03);
            stpc_ide_bm_handlers(dev);
            break;

        case 0x05:
            dev->pci_conf[2][addr] = val & 0x01;
            break;

        case 0x07:
            dev->pci_conf[2][addr] &= ~(val & 0x70);
            break;

        case 0x09:
            dev->pci_conf[2][addr] = (dev->pci_conf[2][addr] & 0x8a) | (val & 0x05);
            stpc_ide_handlers(dev, 0x03);
            break;

        case 0x10:
            dev->pci_conf[2][addr] = (val & 0xf8) | 1;
            stpc_ide_handlers(dev, 0x01);
            break;
        case 0x11:
            dev->pci_conf[2][addr] = val;
            stpc_ide_handlers(dev, 0x01);
            break;

        case 0x14:
            dev->pci_conf[2][addr] = (val & 0xfc) | 1;
            stpc_ide_handlers(dev, 0x01);
            break;
        case 0x15:
            dev->pci_conf[2][addr] = val;
            stpc_ide_handlers(dev, 0x01);
            break;

        case 0x18:
            dev->pci_conf[2][addr] = (val & 0xf8) | 1;
            stpc_ide_handlers(dev, 0x02);
            break;
        case 0x19:
            dev->pci_conf[2][addr] = val;
            stpc_ide_handlers(dev, 0x02);
            break;

        case 0x1c:
            dev->pci_conf[2][addr] = (val & 0xfc) | 1;
            stpc_ide_handlers(dev, 0x02);
            break;
        case 0x1d:
            dev->pci_conf[2][addr] = val;
            stpc_ide_handlers(dev, 0x02);
            break;

        case 0x20:
            dev->pci_conf[2][0x20] = (val & 0xf0) | 1;
            stpc_ide_bm_handlers(dev);
            break;
        case 0x21:
            dev->pci_conf[2][0x21] = val;
            stpc_ide_bm_handlers(dev);
            break;

        case 0x3c:
            dev->pci_conf[2][addr] = val;
            break;

        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x47:
            dev->pci_conf[2][addr] = val;
            break;

        case 0x48:
            dev->pci_conf[2][addr] = (val & 0x8c) & ~(val & 0x03);
            stpc_ide_handlers(dev, 0x03);
            if (val & 0x02) {
                sff_bus_master_set_irq(0x01, dev->bm[0]);
                sff_bus_master_set_irq(0x01, dev->bm[1]);
            }
            if (val & 0x01) {
                sff_bus_master_set_irq(0x00, dev->bm[0]);
                sff_bus_master_set_irq(0x00, dev->bm[1]);
            }
            break;
    }
}

static uint8_t
stpc_ide_read(int func, int addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (func > 0)
        ret = 0xff;
    else {
        ret = dev->pci_conf[2][addr];
        if (addr == 0x48) {
            ret &= 0xfc;
            ret |= !!(dev->bm[0]->status & 0x04);
            ret |= (!!(dev->bm[1]->status & 0x04)) << 1;
        }
    }

    stpc_log("STPC: ide_read(%d, %02X) = %02X\n", func, addr, ret);
    return ret;
}

static void
stpc_isab_write(int func, int addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    if ((func == 1) && (dev->local != STPC_ATLAS)) {
        stpc_ide_write(0, addr, val, priv);
        return;
    }

    stpc_log("STPC: isab_write(%d, %02X, %02X)\n", func, addr, val);

    if (func > 0)
        return;

    switch (addr) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0e:
            return;

        case 0x05:
            val &= 0x01;
            break;
    }

    dev->pci_conf[1][addr] = val;
}

static uint8_t
stpc_isab_read(int func, int addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if ((func == 1) && (dev->local != STPC_ATLAS))
        ret = stpc_ide_read(0, addr, priv);
    else if (func > 0)
        ret = 0xff;
    else
        ret = dev->pci_conf[1][addr];

    stpc_log("STPC: isab_read(%d, %02X) = %02X\n", func, addr, ret);
    return ret;
}

static void
stpc_usb_write(int func, int addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: usb_write(%d, %02X, %02X)\n", func, addr, val);

    if (func > 0)
        return;

    switch (addr) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0e:
        case 0x10:
            return;

        case 0x05:
            val &= 0x01;
            break;

        case 0x11:
            dev->pci_conf[3][addr] = val & 0xf0;
            ohci_update_mem_mapping(dev->usb, dev->pci_conf[3][0x11], dev->pci_conf[3][0x12], dev->pci_conf[3][0x13], 1);
            break;

        case 0x12:
        case 0x13:
            dev->pci_conf[3][addr] = val;
            ohci_update_mem_mapping(dev->usb, dev->pci_conf[3][0x11], dev->pci_conf[3][0x12], dev->pci_conf[3][0x13], 1);
            break;
    }

    dev->pci_conf[3][addr] = val;
}

static uint8_t
stpc_usb_read(int func, int addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (func > 0)
        ret = 0xff;
    else
        ret = dev->pci_conf[3][addr];

    stpc_log("STPC: usb_read(%d, %02X) = %02X\n", func, addr, ret);
    return ret;
}

static void
stpc_remap_host(stpc_t *dev, uint16_t host_base)
{
    stpc_log("STPC: Remapping host bus from %04X to %04X\n", dev->host_base, host_base);

    io_removehandler(dev->host_base, 5,
                     stpc_host_read, NULL, NULL, stpc_host_write, NULL, NULL, dev);
    if (host_base) {
        io_sethandler(host_base, 5,
                      stpc_host_read, NULL, NULL, stpc_host_write, NULL, NULL, dev);
    }
    dev->host_base = host_base;
}

static void
stpc_remap_localbus(stpc_t *dev, uint16_t localbus_base)
{
    stpc_log("STPC: Remapping local bus from %04X to %04X\n", dev->localbus_base, localbus_base);

    io_removehandler(dev->localbus_base, 5,
                     stpc_localbus_read, NULL, NULL, stpc_localbus_write, NULL, NULL, dev);
    if (localbus_base) {
        io_sethandler(localbus_base, 5,
                      stpc_localbus_read, NULL, NULL, stpc_localbus_write, NULL, NULL, dev);
    }
    dev->localbus_base = localbus_base;
}

static uint8_t
stpc_serial_handlers(uint8_t val)
{
    stpc_serial_t *dev = device_get_priv(&stpc_serial_device);
    if (!dev) {
        stpc_log("STPC: Not remapping UARTs, disabled by strap (raw %02X)\n", val);
        return 0;
    }

    uint16_t uart0_io = 0x3f8, uart1_io = 0x3f8;
    uint8_t  uart0_irq = 4, uart1_irq = 3;

    if (val & 0x10)
        uart1_io &= 0xfeff;
    if (val & 0x20)
        uart1_io &= 0xffef;
    if (val & 0x40)
        uart0_io &= 0xfeff;
    if (val & 0x80)
        uart0_io &= 0xffef;

    if (uart0_io == uart1_io) {
        /* Apply defaults if both UARTs are set to the same address. */
        stpc_log("STPC: Both UARTs set to %02X, resetting to defaults\n", uart0_io);
        uart0_io = 0x3f8;
        uart1_io = 0x2f8;
    }

    if (!(uart0_io & 0x100)) {
        /* The address for UART0 establishes the IRQs for both ports. */
        uart0_irq = 3;
        uart1_irq = 4;
    }

    stpc_log("STPC: Remapping UART0 to %04X %d and UART1 to %04X %d (raw %02X)\n", uart0_io, uart0_irq, uart1_io, uart1_irq, val);

    serial_remove(dev->uart[0]);
    serial_setup(dev->uart[0], uart0_io, uart0_irq);
    serial_remove(dev->uart[1]);
    serial_setup(dev->uart[1], uart1_io, uart1_irq);

    return 1;
}

static void
stpc_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: reg_write(%04X, %02X)\n", addr, val);

    if (addr == 0x22) {
        dev->reg_offset = val;
    } else {
        stpc_log("STPC: regs[%02X] = %02X\n", dev->reg_offset, val);

        switch (dev->reg_offset) {
            case 0x12:
                if (dev->regs[0x10] == 0x07)
                    stpc_remap_host(dev, (dev->host_base & 0xff00) | val);
                else if (dev->regs[0x10] == 0x06)
                    stpc_remap_localbus(dev, (dev->localbus_base & 0xff00) | val);
                break;

            case 0x13:
                if (dev->regs[0x10] == 0x07)
                    stpc_remap_host(dev, (dev->host_base & 0x00ff) | (val << 8));
                else if (dev->regs[0x10] == 0x06)
                    stpc_remap_localbus(dev, (dev->localbus_base & 0x00ff) | (val << 8));
                break;

            case 0x21:
                val &= 0xfe;
                break;

            case 0x22:
                val &= 0x7f;
                break;

            case 0x25:
            case 0x26:
            case 0x27:
            case 0x28:
                if (dev->reg_offset == 0x28) {
                    val &= 0xe3;
                    smram_state_change(dev->smram, 0, !!(val & 0x80));
                }
                dev->regs[dev->reg_offset] = val;
                stpc_recalcmapping(dev);
                break;

            case 0x29:
                val &= 0x0f;
                break;

            case 0x36:
                val &= 0x3f;
                break;

            case 0x52:
            case 0x53:
            case 0x54:
            case 0x55:
                stpc_log("STPC: Set IRQ routing: INT %c -> %d\n", 0x41 + ((dev->reg_offset - 2) & 0x03), (val & 0x80) ? (val & 0xf) : -1);
                val &= 0x8f;
                pci_set_irq_routing(PCI_INTA + ((dev->reg_offset - 2) & 0x03), (val & 0x80) ? (val & 0xf) : PCI_IRQ_DISABLED);
                break;

            case 0x56:
            case 0x57:
                pic_elcr_write(dev->reg_offset, val, (dev->reg_offset & 1) ? &pic2 : &pic);
                if (dev->reg_offset == 0x57)
                    refresh_at_enable = (val & 0x01);
                break;

            case 0x59:
                val &= 0xf1;
                stpc_serial_handlers(val);
                break;
        }

        dev->regs[dev->reg_offset] = val;
    }
}

static uint8_t
stpc_reg_read(uint16_t addr, void *priv)
{
    stpc_t *dev = (stpc_t *) priv;
    uint8_t ret;

    if (addr == 0x22)
        ret = dev->reg_offset;
    else if (dev->reg_offset >= 0xc0)
        return 0xff; /* let the CPU code handle Cyrix CPU registers */
    else if ((dev->reg_offset == 0x56) || (dev->reg_offset == 0x57)) {
        /* ELCR registers. */
        ret = pic_elcr_read(dev->reg_offset, (dev->reg_offset & 1) ? &pic2 : &pic);
        if (dev->reg_offset == 0x57)
            ret |= (dev->regs[dev->reg_offset] & 0x01);
    } else
        ret = dev->regs[dev->reg_offset];

    stpc_log("STPC: reg_read(%04X) = %02X\n", dev->reg_offset, ret);
    return ret;
}

static void
stpc_reset(void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: reset()\n");

    memset(dev->regs, 0, sizeof(dev->regs));
    dev->regs[0x7b] = 0xff;
    if (device_get_priv(&stpc_lpt_device))
        dev->regs[0x4c] |= 0x80; /* LPT strap */
    if (stpc_serial_handlers(0x00))
        dev->regs[0x4c] |= 0x03; /* UART straps */
}

static void
stpc_setup(stpc_t *dev)
{
    stpc_log("STPC: setup()\n");

    /* Main register interface */
    io_sethandler(0x22, 2,
                  stpc_reg_read, NULL, NULL, stpc_reg_write, NULL, NULL, dev);

    /* Northbridge */
    if (dev->local & STPC_CLIENT) {
        dev->pci_conf[0][0x00] = 0x0e;
        dev->pci_conf[0][0x01] = 0x10;
        dev->pci_conf[0][0x02] = 0x64;
        dev->pci_conf[0][0x03] = 0x05;
    } else {
        dev->pci_conf[0][0x00] = 0x4a;
        dev->pci_conf[0][0x01] = 0x10;
        dev->pci_conf[0][0x02] = 0x0a;
        dev->pci_conf[0][0x03] = 0x02;
    }

    dev->pci_conf[0][0x04] = 0x07;

    dev->pci_conf[0][0x06] = 0x80;
    dev->pci_conf[0][0x07] = 0x02;

    dev->pci_conf[0][0x0b] = 0x06;

    /* ISA Bridge */
    dev->pci_conf[1][0x00] = dev->local >> 16;
    dev->pci_conf[1][0x01] = dev->local >> 24;
    dev->pci_conf[1][0x02] = dev->local;
    dev->pci_conf[1][0x03] = dev->local >> 8;

    dev->pci_conf[1][0x04] = 0x0f;

    dev->pci_conf[1][0x06] = 0x80;
    dev->pci_conf[1][0x07] = 0x02;

    dev->pci_conf[1][0x0a] = 0x01;
    dev->pci_conf[1][0x0b] = 0x06;

    /* NOTE: This is an erratum in the STPC Atlas programming manual, the programming manuals for the other
             STPC chipsets say 0x80, which is indeed multi-function (as the STPC Atlas programming manual
             indicates as well), and Windows 2000 also issues a 0x7B STOP error if it is 0x40. */
    dev->pci_conf[1][0x0e] = /*0x40*/ 0x80;

    /* IDE */
    dev->pci_conf[2][0x00] = dev->local >> 16;
    dev->pci_conf[2][0x01] = dev->local >> 24;

    if (dev->local == STPC_ATLAS) {
        dev->pci_conf[2][0x02] = 0x28;
        dev->pci_conf[2][0x03] = 0x02;
    } else {
        dev->pci_conf[2][0x02] = dev->pci_conf[1][0x02];
        dev->pci_conf[2][0x03] = dev->pci_conf[1][0x03];
    }

    dev->pci_conf[2][0x06] = 0x80;
    dev->pci_conf[2][0x07] = 0x02;

    dev->pci_conf[2][0x09] = 0x8a;
    dev->pci_conf[2][0x0a] = 0x01;
    dev->pci_conf[2][0x0b] = 0x01;

    /* NOTE: This is an erratum in the STPC Atlas programming manual, the programming manuals for the other
             STPC chipsets say 0x80, which is indeed multi-function (as the STPC Atlas programming manual
             indicates as well), and Windows 2000 also issues a 0x7B STOP error if it is 0x40. */
    dev->pci_conf[2][0x0e] = /*0x40*/ 0x80;

    dev->pci_conf[2][0x10] = 0x01;
    dev->pci_conf[2][0x14] = 0x01;
    dev->pci_conf[2][0x18] = 0x01;
    dev->pci_conf[2][0x1c] = 0x01;
    dev->pci_conf[2][0x20] = 0x01;

    dev->pci_conf[2][0x40] = 0x60;
    dev->pci_conf[2][0x41] = 0x97;
    dev->pci_conf[2][0x42] = 0x60;
    dev->pci_conf[2][0x43] = 0x97;
    dev->pci_conf[2][0x44] = 0x60;
    dev->pci_conf[2][0x45] = 0x97;
    dev->pci_conf[2][0x46] = 0x60;
    dev->pci_conf[2][0x47] = 0x97;

    /* USB */
    if (dev->usb) {
        dev->pci_conf[3][0x00] = dev->local >> 16;
        dev->pci_conf[3][0x01] = dev->local >> 24;
        dev->pci_conf[3][0x02] = 0x30;
        dev->pci_conf[3][0x03] = 0x02;

        dev->pci_conf[3][0x06] = 0x80;
        dev->pci_conf[3][0x07] = 0x02;

        dev->pci_conf[3][0x09] = 0x10;
        dev->pci_conf[3][0x0a] = 0x03;
        dev->pci_conf[3][0x0b] = 0x0c;

        /* NOTE: This is an erratum in the STPC Atlas programming manual, the programming manuals for the other
                 STPC chipsets say 0x80, which is indeed multi-function (as the STPC Atlas programming manual
                 indicates as well), and Windows 2000 also issues a 0x7B STOP error if it is 0x40. */
        dev->pci_conf[3][0x0e] = /*0x40*/ 0x80;
    }

    /* PCI setup */
    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
}

static void
stpc_usb_update_interrupt(usb_t* usb, void* priv)
{
    stpc_t *dev = (stpc_t *) priv;

    if (usb->irq_level)
        pci_set_irq(dev->usb_slot, PCI_INTA);
    else
        pci_clear_irq(dev->usb_slot, PCI_INTA);
}

static void
stpc_close(void *priv)
{
    stpc_t *dev = (stpc_t *) priv;

    stpc_log("STPC: close()\n");

    smram_del(dev->smram);

    free(dev);
}

static void *
stpc_init(const device_t *info)
{
    stpc_log("STPC: init()\n");

    stpc_t *dev = (stpc_t *) malloc(sizeof(stpc_t));
    memset(dev, 0, sizeof(stpc_t));

    dev->local = info->local;

    pci_add_card(PCI_ADD_NORTHBRIDGE, stpc_nb_read, stpc_nb_write, dev);
    dev->ide_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, stpc_isab_read, stpc_isab_write, dev);
    if (dev->local == STPC_ATLAS) {
        dev->usb_params.smi_handle       = NULL;
        dev->usb_params.update_interrupt = stpc_usb_update_interrupt;
        dev->usb_params.parent_priv      = dev;

        dev->ide_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, stpc_ide_read, stpc_ide_write, dev);
        dev->usb      = device_add_parameters(&usb_device, &dev->usb_params);
        dev->usb_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, stpc_usb_read, stpc_usb_write, dev);
    }

    dev->bm[0] = device_add_inst(&sff8038i_device, 1);
    dev->bm[1] = device_add_inst(&sff8038i_device, 2);

    sff_set_irq_mode(dev->bm[0], 0, 0);
    sff_set_irq_mode(dev->bm[0], 1, 0);

    sff_set_irq_mode(dev->bm[1], 0, 0);
    sff_set_irq_mode(dev->bm[1], 1, 0);

    stpc_setup(dev);
    stpc_reset(dev);

    dev->smram = smram_add();

    smram_enable(dev->smram, 0x000a0000, 0x000a0000, 0x00020000, 0, 1);

    device_add(&port_92_pci_device);

    pic_elcr_io_handler(0);
    refresh_at_enable = 0;

    return dev;
}

static void
stpc_serial_close(void *priv)
{
    stpc_serial_t *dev = (stpc_serial_t *) priv;

    stpc_log("STPC: serial_close()\n");

    free(dev);
}

static void *
stpc_serial_init(const device_t *info)
{
    stpc_log("STPC: serial_init()\n");

    stpc_serial_t *dev = (stpc_serial_t *) malloc(sizeof(stpc_serial_t));
    memset(dev, 0, sizeof(stpc_serial_t));

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    /* Initialization is performed by stpc_reset */

    return dev;
}

static void
stpc_lpt_handlers(stpc_lpt_t *dev, uint8_t val)
{
    uint8_t old_addr = (dev->reg1 & 0x03), new_addr = (val & 0x03);

    switch (old_addr) {
        case 0x1:
            lpt3_remove();
            break;

        case 0x2:
            lpt1_remove();
            break;

        case 0x3:
            lpt2_remove();
            break;
    }

    switch (new_addr) {
        case 0x1:
            stpc_log("STPC: Remapping parallel port to LPT3\n");
            lpt3_init(0x3bc);
            break;

        case 0x2:
            stpc_log("STPC: Remapping parallel port to LPT1\n");
            lpt1_init(0x378);
            break;

        case 0x3:
            stpc_log("STPC: Remapping parallel port to LPT2\n");
            lpt2_init(0x278);
            break;

        default:
            stpc_log("STPC: Disabling parallel port\n");
            break;
    }

    dev->reg1 = (val & 0x08);
    dev->reg1 |= new_addr;
    dev->reg1 |= 0x84; /* reserved bits that default to 1; hardwired? */
}

static void
stpc_lpt_write(uint16_t addr, uint8_t val, void *priv)
{
    stpc_lpt_t *dev = (stpc_lpt_t *) priv;

    if (dev->unlocked < 2) {
        /* Cheat a little bit: in reality, any write to any
           I/O port is supposed to reset the unlock counter. */
        if ((addr == 0x3f0) && (val == 0x55))
            dev->unlocked++;
        else
            dev->unlocked = 0;
    } else if (addr == 0x3f0) {
        if (val == 0xaa)
            dev->unlocked = 0;
        else
            dev->offset = val;
    } else if (dev->offset == 1) {
        /* dev->reg1 is set by stpc_lpt_handlers */
        stpc_lpt_handlers(dev, val);
    } else if (dev->offset == 4) {
        dev->reg4 = (val & 0x03);
    }
}

static void
stpc_lpt_reset(void *priv)
{
    stpc_lpt_t *dev = (stpc_lpt_t *) priv;

    stpc_log("STPC: lpt_reset()\n");

    dev->unlocked = 0;
    dev->offset   = 0x00;
    dev->reg1     = 0x9f;
    dev->reg4     = 0x00;
    stpc_lpt_handlers(dev, dev->reg1);
}

static void
stpc_lpt_close(void *priv)
{
    stpc_lpt_t *dev = (stpc_lpt_t *) priv;

    stpc_log("STPC: lpt_close()\n");

    free(dev);
}

static void *
stpc_lpt_init(const device_t *info)
{
    stpc_log("STPC: lpt_init()\n");

    stpc_lpt_t *dev = (stpc_lpt_t *) malloc(sizeof(stpc_lpt_t));
    memset(dev, 0, sizeof(stpc_lpt_t));

    stpc_lpt_reset(dev);

    io_sethandler(0x3f0, 2,
                  NULL, NULL, NULL, stpc_lpt_write, NULL, NULL, dev);

    return dev;
}

/* STPC SoCs */
const device_t stpc_client_device = {
    .name          = "STPC Client",
    .internal_name = "stpc_client",
    .flags         = DEVICE_PCI,
    .local         = STPC_CLIENT,
    .init          = stpc_init,
    .close         = stpc_close,
    .reset         = stpc_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t stpc_consumer2_device = {
    .name          = "STPC Consumer-II",
    .internal_name = "stpc_consumer2",
    .flags         = DEVICE_PCI,
    .local         = STPC_CONSUMER2,
    .init          = stpc_init,
    .close         = stpc_close,
    .reset         = stpc_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t stpc_elite_device = {
    .name          = "STPC Elite",
    .internal_name = "stpc_elite",
    .flags         = DEVICE_PCI,
    .local         = STPC_ELITE,
    .init          = stpc_init,
    .close         = stpc_close,
    .reset         = stpc_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t stpc_atlas_device = {
    .name          = "STPC Atlas",
    .internal_name = "stpc_atlas",
    .flags         = DEVICE_PCI,
    .local         = STPC_ATLAS,
    .init          = stpc_init,
    .close         = stpc_close,
    .reset         = stpc_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* Auxiliary devices */
const device_t stpc_serial_device = {
    .name          = "STPC Serial UARTs",
    .internal_name = "stpc_serial",
    .flags         = 0,
    .local         = 0,
    .init          = stpc_serial_init,
    .close         = stpc_serial_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t stpc_lpt_device = {
    .name          = "STPC Parallel Port",
    .internal_name = "stpc_lpt",
    .flags         = 0,
    .local         = 0,
    .init          = stpc_lpt_init,
    .close         = stpc_lpt_close,
    .reset         = stpc_lpt_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
