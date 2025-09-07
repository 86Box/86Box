/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Universal Serial Bus emulation (currently dummy UHCI and
 *          OHCI).
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/usb.h>
#include "cpu.h"
#include <86box/plat_unused.h>

#ifdef ENABLE_USB_LOG
int usb_do_log = ENABLE_USB_LOG;

static void
usb_log(const char *fmt, ...)
{
    va_list ap;

    if (usb_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define usb_log(fmt, ...)
#endif

static uint8_t
uhci_reg_read(uint16_t addr, void *priv)
{
    const usb_t   *dev = (usb_t *) priv;
    uint8_t        ret;
    const uint8_t *regs = dev->uhci_io;

    addr &= 0x0000001f;

    ret = regs[addr];

    return ret;
}

static void
uhci_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    usb_t   *dev  = (usb_t *) priv;
    uint8_t *regs = dev->uhci_io;

    addr &= 0x0000001f;

    switch (addr) {
        case 0x02:
            regs[0x02] &= ~(val & 0x3f);
            break;
        case 0x04:
            regs[0x04] = (val & 0x0f);
            break;
        case 0x09:
            regs[0x09] = (val & 0xf0);
            break;
        case 0x0a:
        case 0x0b:
            regs[addr] = val;
            break;
        case 0x0c:
            regs[0x0c] = (val & 0x7f);
            break;

        default:
            break;
    }
}

static void
uhci_reg_writew(uint16_t addr, uint16_t val, void *priv)
{
    usb_t    *dev  = (usb_t *) priv;
    uint16_t *regs = (uint16_t *) dev->uhci_io;

    addr &= 0x0000001f;

    switch (addr) {
        case 0x00:
            if ((val & 0x0001) && !(regs[0x00] & 0x0001))
                regs[0x01] &= ~0x20;
            else if (!(val & 0x0001))
                regs[0x01] |= 0x20;
            regs[0x00] = (val & 0x00ff);
            break;
        case 0x06:
            regs[0x03] = (val & 0x07ff);
            break;
        case 0x10:
        case 0x12:
            regs[addr >> 1] = ((regs[addr >> 1] & 0xedbb) | (val & 0x1244)) & ~(val & 0x080a);
            break;
        default:
            uhci_reg_write(addr, val & 0xff, priv);
            uhci_reg_write(addr + 1, (val >> 8) & 0xff, priv);
            break;
    }
}

void
uhci_update_io_mapping(usb_t *dev, uint8_t base_l, uint8_t base_h, int enable)
{
    if (dev->uhci_enable && (dev->uhci_io_base != 0x0000))
        io_removehandler(dev->uhci_io_base, 0x20, uhci_reg_read, NULL, NULL, uhci_reg_write, uhci_reg_writew, NULL, dev);

    dev->uhci_io_base = base_l | (base_h << 8);
    dev->uhci_enable  = enable;

    if (dev->uhci_enable && (dev->uhci_io_base != 0x0000))
        io_sethandler(dev->uhci_io_base, 0x20, uhci_reg_read, NULL, NULL, uhci_reg_write, uhci_reg_writew, NULL, dev);
}

static uint8_t
ohci_mmio_read(uint32_t addr, void *priv)
{
    const usb_t  *dev = (usb_t *) priv;
    uint8_t       ret = 0x00;

    addr &= 0x00000fff;

    ret = dev->ohci_mmio[addr];

    if (addr == 0x101)
        ret = (ret & 0xfe) | (!!mem_a20_key);

    return ret;
}

static void
ohci_mmio_write(uint32_t addr, uint8_t val, void *priv)
{
    usb_t  *dev = (usb_t *) priv;
    uint8_t old;

    addr &= 0x00000fff;

    switch (addr) {
        case 0x04:
            if ((val & 0xc0) == 0x00) {
                /* UsbReset */
                dev->ohci_mmio[0x56] = dev->ohci_mmio[0x5a] = 0x16;
            }
            break;
        case 0x08: /* HCCOMMANDSTATUS */
            /* bit OwnershipChangeRequest triggers an ownership change (SMM <-> OS) */
            if (val & 0x08) {
                dev->ohci_mmio[0x0f] = 0x40;
                if ((dev->ohci_mmio[0x13] & 0xc0) == 0xc0)
                    smi_raise();
            }

            /* bit HostControllerReset must be cleared for the controller to be seen as initialized */
            if (val & 0x01) {
                memset(dev->ohci_mmio, 0x00, 4096);
                dev->ohci_mmio[0x00] = 0x10;
                dev->ohci_mmio[0x01] = 0x01;
                dev->ohci_mmio[0x48] = 0x02;
                val &= ~0x01;
            }
            break;
        case 0x0c:
            dev->ohci_mmio[addr] &= ~(val & 0x7f);
            return;
        case 0x0d:
        case 0x0e:
            return;
        case 0x0f:
            dev->ohci_mmio[addr] &= ~(val & 0x40);
            return;
        case 0x3b:
            dev->ohci_mmio[addr] = (val & 0x80);
            return;
        case 0x39:
        case 0x41:
            dev->ohci_mmio[addr] = (val & 0x3f);
            return;
        case 0x45:
            dev->ohci_mmio[addr] = (val & 0x0f);
            return;
        case 0x3a:
        case 0x3e:
        case 0x3f:
        case 0x42:
        case 0x43:
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x4a:
            return;
        case 0x49:
            dev->ohci_mmio[addr] = (val & 0x1b);
            if (val & 0x02) {
                dev->ohci_mmio[0x55] |= 0x01;
                dev->ohci_mmio[0x59] |= 0x01;
            }
            return;
        case 0x4b:
            dev->ohci_mmio[addr] = (val & 0x03);
            return;
        case 0x4c:
        case 0x4e:
            dev->ohci_mmio[addr] = (val & 0x06);
            if ((addr == 0x4c) && !(val & 0x04)) {
                if (!(dev->ohci_mmio[0x58] & 0x01))
                    dev->ohci_mmio[0x5a] |= 0x01;
                dev->ohci_mmio[0x58] |= 0x01;
            }
            if ((addr == 0x4c) && !(val & 0x02)) {
                if (!(dev->ohci_mmio[0x54] & 0x01))
                    dev->ohci_mmio[0x56] |= 0x01;
                dev->ohci_mmio[0x54] |= 0x01;
            }
            return;
        case 0x4d:
        case 0x4f:
            return;
        case 0x50:
            if (val & 0x01) {
                if ((dev->ohci_mmio[0x49] & 0x03) == 0x00) {
                    dev->ohci_mmio[0x55] &= ~0x01;
                    dev->ohci_mmio[0x54] &= ~0x17;
                    dev->ohci_mmio[0x56] &= ~0x17;
                    dev->ohci_mmio[0x59] &= ~0x01;
                    dev->ohci_mmio[0x58] &= ~0x17;
                    dev->ohci_mmio[0x5a] &= ~0x17;
                } else if ((dev->ohci_mmio[0x49] & 0x03) == 0x01) {
                    if (!(dev->ohci_mmio[0x4e] & 0x02)) {
                        dev->ohci_mmio[0x55] &= ~0x01;
                        dev->ohci_mmio[0x54] &= ~0x17;
                        dev->ohci_mmio[0x56] &= ~0x17;
                    }
                    if (!(dev->ohci_mmio[0x4e] & 0x04)) {
                        dev->ohci_mmio[0x59] &= ~0x01;
                        dev->ohci_mmio[0x58] &= ~0x17;
                        dev->ohci_mmio[0x5a] &= ~0x17;
                    }
                }
            }
            return;
        case 0x51:
            if (val & 0x80)
                dev->ohci_mmio[addr] |= 0x80;
            return;
        case 0x52:
            dev->ohci_mmio[addr] &= ~(val & 0x02);
            if (val & 0x01) {
                if ((dev->ohci_mmio[0x49] & 0x03) == 0x00) {
                    dev->ohci_mmio[0x55] |= 0x01;
                    dev->ohci_mmio[0x59] |= 0x01;
                } else if ((dev->ohci_mmio[0x49] & 0x03) == 0x01) {
                    if (!(dev->ohci_mmio[0x4e] & 0x02))
                        dev->ohci_mmio[0x55] |= 0x01;
                    if (!(dev->ohci_mmio[0x4e] & 0x04))
                        dev->ohci_mmio[0x59] |= 0x01;
                }
            }
            return;
        case 0x53:
            if (val & 0x80)
                dev->ohci_mmio[0x51] &= ~0x80;
            return;
        case 0x54:
        case 0x58:
            old = dev->ohci_mmio[addr];

            if (val & 0x10) {
                if (old & 0x01) {
                    dev->ohci_mmio[addr] |= 0x10;
                    /* TODO: The clear should be on a 10 ms timer. */
                    dev->ohci_mmio[addr] &= ~0x10;
                    dev->ohci_mmio[addr + 2] |= 0x10;
                } else
                    dev->ohci_mmio[addr + 2] |= 0x01;
            }
            if (val & 0x08)
                dev->ohci_mmio[addr] &= ~0x04;
            if (val & 0x04)
                dev->ohci_mmio[addr] |= 0x04;
            if (val & 0x02) {
                if (old & 0x01)
                    dev->ohci_mmio[addr] |= 0x02;
                else
                    dev->ohci_mmio[addr + 2] |= 0x01;
            }
            if (val & 0x01) {
                if (old & 0x01)
                    dev->ohci_mmio[addr] &= ~0x02;
                else
                    dev->ohci_mmio[addr + 2] |= 0x01;
            }

            if (!(dev->ohci_mmio[addr] & 0x04) && (old & 0x04))
                dev->ohci_mmio[addr + 2] |= 0x04;
#if 0
            if (!(dev->ohci_mmio[addr] & 0x02))
                 dev->ohci_mmio[addr + 2] |= 0x02;
#endif
            return;
        case 0x55:
            if ((val & 0x02) && ((dev->ohci_mmio[0x49] & 0x03) == 0x00) && (dev->ohci_mmio[0x4e] & 0x02)) {
                dev->ohci_mmio[addr] &= ~0x01;
                dev->ohci_mmio[0x54] &= ~0x17;
                dev->ohci_mmio[0x56] &= ~0x17;
            }
            if ((val & 0x01) && ((dev->ohci_mmio[0x49] & 0x03) == 0x00) && (dev->ohci_mmio[0x4e] & 0x02)) {
                dev->ohci_mmio[addr] |= 0x01;
                dev->ohci_mmio[0x58] &= ~0x17;
                dev->ohci_mmio[0x5a] &= ~0x17;
            }
            return;
        case 0x59:
            if ((val & 0x02) && ((dev->ohci_mmio[0x49] & 0x03) == 0x00) && (dev->ohci_mmio[0x4e] & 0x04))
                dev->ohci_mmio[addr] &= ~0x01;
            if ((val & 0x01) && ((dev->ohci_mmio[0x49] & 0x03) == 0x00) && (dev->ohci_mmio[0x4e] & 0x04))
                dev->ohci_mmio[addr] |= 0x01;
            return;
        case 0x56:
        case 0x5a:
            dev->ohci_mmio[addr] &= ~(val & 0x1f);
            return;
        case 0x57:
        case 0x5b:
            return;

        default:
            break;
    }

    dev->ohci_mmio[addr] = val;
}

void
ohci_update_mem_mapping(usb_t *dev, uint8_t base1, uint8_t base2, uint8_t base3, int enable)
{
    if (dev->ohci_enable && (dev->ohci_mem_base != 0x00000000))
        mem_mapping_disable(&dev->ohci_mmio_mapping);

    dev->ohci_mem_base = ((base1 << 8) | (base2 << 16) | (base3 << 24)) & 0xfffff000;
    dev->ohci_enable   = enable;

    if (dev->ohci_enable && (dev->ohci_mem_base != 0x00000000))
        mem_mapping_set_addr(&dev->ohci_mmio_mapping, dev->ohci_mem_base, 0x1000);
}

static void
usb_reset(void *priv)
{
    usb_t *dev = (usb_t *) priv;

    memset(dev->uhci_io, 0x00, 128);
    dev->uhci_io[0x0c] = 0x40;
    dev->uhci_io[0x10] = dev->uhci_io[0x12] = 0x80;

    memset(dev->ohci_mmio, 0x00, 4096);
    dev->ohci_mmio[0x00] = 0x10;
    dev->ohci_mmio[0x01] = 0x01;
    dev->ohci_mmio[0x48] = 0x02;

    io_removehandler(dev->uhci_io_base, 0x20, uhci_reg_read, NULL, NULL, uhci_reg_write, uhci_reg_writew, NULL, dev);
    dev->uhci_enable = 0;

    mem_mapping_disable(&dev->ohci_mmio_mapping);
    dev->ohci_enable = 0;
}

static void
usb_close(void *priv)
{
    usb_t *dev = (usb_t *) priv;

    free(dev);
}

static void *
usb_init(UNUSED(const device_t *info))
{
    usb_t *dev;

    dev = (usb_t *) malloc(sizeof(usb_t));
    if (dev == NULL)
        return (NULL);
    memset(dev, 0x00, sizeof(usb_t));

    memset(dev->uhci_io, 0x00, 128);
    dev->uhci_io[0x0c] = 0x40;
    dev->uhci_io[0x10] = dev->uhci_io[0x12] = 0x80;

    memset(dev->ohci_mmio, 0x00, 4096);
    dev->ohci_mmio[0x00] = 0x10;
    dev->ohci_mmio[0x01] = 0x01;
    dev->ohci_mmio[0x48] = 0x02;

    mem_mapping_add(&dev->ohci_mmio_mapping, 0, 0,
                    ohci_mmio_read, NULL, NULL,
                    ohci_mmio_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    usb_reset(dev);

    return dev;
}

const device_t usb_device = {
    .name          = "Universal Serial Bus",
    .internal_name = "usb",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = usb_init,
    .close         = usb_close,
    .reset         = usb_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
