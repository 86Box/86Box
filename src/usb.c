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
#include <86box/timer.h>
#include <86box/usb.h>

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

/* OHCI registers */
enum
{
    OHCI_HcRevision = 0x00,
    OHCI_HcControl = 0x04,
    OHCI_HcCommandStatus = 0x08,
    OHCI_HcInterruptStatus = 0x0C,
    OHCI_HcInterruptEnable = 0x10,
    OHCI_HcInterruptDisable = 0x14,
    OHCI_HcHCCA = 0x18,
    OHCI_HcPeriodCurrentED = 0x1C,
    OHCI_HcControlHeadED = 0x20,
    OHCI_HcControlCurrentED = 0x24,
    OHCI_HcBulkHeadED = 0x28,
    OHCI_HcBulkCurrentED = 0x2C,
    OHCI_HcDoneHead = 0x30,
    OHCI_HcFMInterval = 0x34,
    OHCI_HcFmRemaining = 0x38,
    OHCI_HcFmNumber = 0x3C,
    OHCI_HcPeriodicStart = 0x40,
    OHCI_HcLSThreshold = 0x44,
    OHCI_HcRhDescriptorA = 0x48,
    OHCI_HcRhDescriptorB = 0x4C,
    OHCI_HcRhStatus = 0x50,
    OHCI_HcRhPortStatus1 = 0x54,
    OHCI_HcRhPortStatus2 = 0x58,
    OHCI_HcRhPortStatus3 = 0x5C
};

static void
usb_interrupt_ohci(usb_t* usb)
{
    if (usb->ohci_mmio[OHCI_HcControl + 1] & 1) {
        smi_raise();
    }
    else if (usb->usb_params != NULL) {
        if (usb->usb_params->parent_priv != NULL && usb->usb_params->raise_interrupt != NULL) {
            usb->usb_params->raise_interrupt(usb, usb->usb_params->parent_priv);
        }
    }
}

static uint8_t
uhci_reg_read(uint16_t addr, void *p)
{
    usb_t  *dev = (usb_t *) p;
    uint8_t ret, *regs = dev->uhci_io;

    addr &= 0x0000001f;

    ret = regs[addr];

    return ret;
}

static void
uhci_reg_write(uint16_t addr, uint8_t val, void *p)
{
    usb_t   *dev  = (usb_t *) p;
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
    }
}

static void
uhci_reg_writew(uint16_t addr, uint16_t val, void *p)
{
    usb_t    *dev  = (usb_t *) p;
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
            uhci_reg_write(addr, val & 0xff, p);
            uhci_reg_write(addr + 1, (val >> 8) & 0xff, p);
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
ohci_mmio_read(uint32_t addr, void *p)
{
    usb_t  *dev = (usb_t *) p;
    uint8_t ret = 0x00;

    addr &= 0x00000fff;

    ret = dev->ohci_mmio[addr];

    if (addr == 0x101)
        ret = (ret & 0xfe) | (!!mem_a20_key);

    return ret;
}

void
ohci_update_frame_counter(void* priv)
{
    usb_t *dev = (usb_t *) priv;
}

void
ohci_poll_interrupt_descriptors(void* priv)
{
    usb_t *dev = (usb_t *) priv;

    /* TODO: Actually poll the interrupt descriptors. */

    dev->ohci_interrupt_counter++;
    timer_on_auto(&dev->ohci_interrupt_desc_poll_timer, 1000.);
}

void
ohci_port_reset_callback(void* priv)
{
    usb_t *dev = (usb_t *) priv;

    dev->ohci_mmio[OHCI_HcRhPortStatus1] &= ~0x10;
}

void
ohci_port_reset_callback_2(void* priv)
{
    usb_t *dev = (usb_t *) priv;

    dev->ohci_mmio[OHCI_HcRhPortStatus2] &= ~0x10;
}

static void
ohci_mmio_write(uint32_t addr, uint8_t val, void *p)
{
    usb_t  *dev = (usb_t *) p;
    uint8_t old;

    addr &= 0x00000fff;

    switch (addr) {
        case OHCI_HcControl:
            if ((val & 0xc0) == 0x00) {
                /* UsbReset */
                dev->ohci_mmio[OHCI_HcRhPortStatus1 + 2] = dev->ohci_mmio[OHCI_HcRhPortStatus2 + 2] = 0x16;
            }
            break;
        case OHCI_HcCommandStatus:
            /* bit OwnershipChangeRequest triggers an ownership change (SMM <-> OS) */
            if (val & 0x08) {
                dev->ohci_mmio[OHCI_HcInterruptStatus + 3] = 0x40;
                if ((dev->ohci_mmio[OHCI_HcInterruptEnable + 3] & 0xc0) == 0xc0)
                    smi_raise();
            }

            /* bit HostControllerReset must be cleared for the controller to be seen as initialized */
            if (val & 0x01) {
                memset(dev->ohci_mmio, 0x00, 4096);
                dev->ohci_mmio[OHCI_HcRevision] = 0x10;
                dev->ohci_mmio[OHCI_HcRevision + 1] = 0x01;
                dev->ohci_mmio[OHCI_HcRhDescriptorA] = 0x02;
                val &= ~0x01;
            }
            break;
        case OHCI_HcHCCA:
            return;
        case OHCI_HcInterruptStatus:
            dev->ohci_mmio[addr] &= ~(val & 0x7f);
            return;
        case OHCI_HcInterruptStatus + 1:
        case OHCI_HcInterruptStatus + 2:
            return;
        case OHCI_HcInterruptStatus + 3:
            dev->ohci_mmio[addr] &= ~(val & 0x40);
            return;
        case OHCI_HcFmRemaining + 3:
            dev->ohci_mmio[addr] = (val & 0x80);
            return;
        case OHCI_HcFmRemaining + 1:
        case OHCI_HcPeriodicStart + 1:
            dev->ohci_mmio[addr] = (val & 0x3f);
            return;
        case OHCI_HcLSThreshold + 1:
            dev->ohci_mmio[addr] = (val & 0x0f);
            return;
        case OHCI_HcFmRemaining + 2:
        case OHCI_HcFmNumber + 2:
        case OHCI_HcFmNumber + 3:
        case OHCI_HcPeriodicStart + 2:
        case OHCI_HcPeriodicStart + 3:
        case OHCI_HcLSThreshold + 2:
        case OHCI_HcLSThreshold + 3:
        case OHCI_HcRhDescriptorA:
        case OHCI_HcRhDescriptorA + 2:
            return;
        case OHCI_HcRhDescriptorA + 1:
            dev->ohci_mmio[addr] = (val & 0x1b);
            if (val & 0x02) {
                dev->ohci_mmio[OHCI_HcRhPortStatus1 + 1] |= 0x01;
                dev->ohci_mmio[OHCI_HcRhPortStatus2 + 1] |= 0x01;
            }
            return;
        case OHCI_HcRhDescriptorA + 3:
            dev->ohci_mmio[addr] = (val & 0x03);
            return;
        case OHCI_HcRhDescriptorB:
        case OHCI_HcRhDescriptorB + 2:
            dev->ohci_mmio[addr] = (val & 0x06);
            if ((addr == OHCI_HcRhDescriptorB) && !(val & 0x04)) {
                if (!(dev->ohci_mmio[OHCI_HcRhPortStatus2] & 0x01))
                    dev->ohci_mmio[OHCI_HcRhPortStatus2 + 2] |= 0x01;
                dev->ohci_mmio[OHCI_HcRhPortStatus2] |= 0x01;
            }
            if ((addr == OHCI_HcRhDescriptorB) && !(val & 0x02)) {
                if (!(dev->ohci_mmio[OHCI_HcRhPortStatus1] & 0x01))
                    dev->ohci_mmio[OHCI_HcRhPortStatus1 + 2] |= 0x01;
                dev->ohci_mmio[OHCI_HcRhPortStatus1] |= 0x01;
            }
            return;
        case OHCI_HcRhDescriptorB + 1:
        case OHCI_HcRhDescriptorB + 3:
            return;
        case OHCI_HcRhStatus:
            if (val & 0x01) {
                if ((dev->ohci_mmio[OHCI_HcRhDescriptorA + 1] & 0x03) == 0x00) {
                    dev->ohci_mmio[OHCI_HcRhPortStatus1 + 1] &= ~0x01;
                    dev->ohci_mmio[OHCI_HcRhPortStatus1] &= ~0x17;
                    dev->ohci_mmio[OHCI_HcRhPortStatus1 + 2] &= ~0x17;
                    dev->ohci_mmio[OHCI_HcRhPortStatus2 + 1] &= ~0x01;
                    dev->ohci_mmio[OHCI_HcRhPortStatus2] &= ~0x17;
                    dev->ohci_mmio[OHCI_HcRhPortStatus2 + 2] &= ~0x17;
                } else if ((dev->ohci_mmio[OHCI_HcRhDescriptorA + 1] & 0x03) == 0x01) {
                    if (!(dev->ohci_mmio[OHCI_HcRhDescriptorB + 2] & 0x02)) {
                        dev->ohci_mmio[OHCI_HcRhPortStatus1 + 1] &= ~0x01;
                        dev->ohci_mmio[OHCI_HcRhPortStatus1] &= ~0x17;
                        dev->ohci_mmio[OHCI_HcRhPortStatus1 + 2] &= ~0x17;
                    }
                    if (!(dev->ohci_mmio[OHCI_HcRhDescriptorB + 2] & 0x04)) {
                        dev->ohci_mmio[OHCI_HcRhPortStatus2 + 1] &= ~0x01;
                        dev->ohci_mmio[OHCI_HcRhPortStatus2] &= ~0x17;
                        dev->ohci_mmio[OHCI_HcRhPortStatus2 + 2] &= ~0x17;
                    }
                }
            }
            return;
        case OHCI_HcRhStatus + 1:
            if (val & 0x80)
                dev->ohci_mmio[addr] |= 0x80;
            return;
        case OHCI_HcRhStatus + 2:
            dev->ohci_mmio[addr] &= ~(val & 0x02);
            if (val & 0x01) {
                if ((dev->ohci_mmio[OHCI_HcRhDescriptorA + 1] & 0x03) == 0x00) {
                    dev->ohci_mmio[OHCI_HcRhPortStatus1 + 1] |= 0x01;
                    dev->ohci_mmio[OHCI_HcRhPortStatus2 + 1] |= 0x01;
                } else if ((dev->ohci_mmio[OHCI_HcRhDescriptorA + 1] & 0x03) == 0x01) {
                    if (!(dev->ohci_mmio[OHCI_HcRhDescriptorB + 2] & 0x02))
                        dev->ohci_mmio[OHCI_HcRhPortStatus1 + 1] |= 0x01;
                    if (!(dev->ohci_mmio[OHCI_HcRhDescriptorB + 2] & 0x04))
                        dev->ohci_mmio[OHCI_HcRhPortStatus2 + 1] |= 0x01;
                }
            }
            return;
        case OHCI_HcRhStatus + 3:
            if (val & 0x80)
                dev->ohci_mmio[OHCI_HcRhStatus + 1] &= ~0x80;
            return;
        case OHCI_HcRhPortStatus1:
        case OHCI_HcRhPortStatus2:
            old = dev->ohci_mmio[addr];

            if (val & 0x10) {
                if (old & 0x01) {
                    dev->ohci_mmio[addr] |= 0x10;
                    timer_on_auto(&dev->ohci_port_reset_timer[(addr - OHCI_HcRhPortStatus1) / 4], 10000.);
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
            /* if (!(dev->ohci_mmio[addr] & 0x02))
                    dev->ohci_mmio[addr + 2] |= 0x02; */
            return;
        case OHCI_HcRhPortStatus1 + 1:
            if ((val & 0x02) && ((dev->ohci_mmio[OHCI_HcRhDescriptorA + 1] & 0x03) == 0x00) && (dev->ohci_mmio[OHCI_HcRhDescriptorB + 2] & 0x02)) {
                dev->ohci_mmio[addr] &= ~0x01;
                dev->ohci_mmio[OHCI_HcRhPortStatus1] &= ~0x17;
                dev->ohci_mmio[OHCI_HcRhPortStatus1 + 2] &= ~0x17;
            }
            if ((val & 0x01) && ((dev->ohci_mmio[OHCI_HcRhDescriptorA + 1] & 0x03) == 0x00) && (dev->ohci_mmio[OHCI_HcRhDescriptorB + 2] & 0x02)) {
                dev->ohci_mmio[addr] |= 0x01;
                dev->ohci_mmio[OHCI_HcRhPortStatus2] &= ~0x17;
                dev->ohci_mmio[OHCI_HcRhPortStatus2 + 2] &= ~0x17;
            }
            return;
        case OHCI_HcRhPortStatus2 + 1:
            if ((val & 0x02) && ((dev->ohci_mmio[OHCI_HcRhDescriptorA + 1] & 0x03) == 0x00) && (dev->ohci_mmio[OHCI_HcRhDescriptorB + 2] & 0x04))
                dev->ohci_mmio[addr] &= ~0x01;
            if ((val & 0x01) && ((dev->ohci_mmio[OHCI_HcRhDescriptorA + 1] & 0x03) == 0x00) && (dev->ohci_mmio[OHCI_HcRhDescriptorB + 2] & 0x04))
                dev->ohci_mmio[addr] |= 0x01;
            return;
        case OHCI_HcRhPortStatus1 + 2:
        case OHCI_HcRhPortStatus2 + 2:
            dev->ohci_mmio[addr] &= ~(val & 0x1f);
            return;
        case OHCI_HcRhPortStatus1 + 3:
        case OHCI_HcRhPortStatus2 + 3:
            return;
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
    dev->ohci_mmio[OHCI_HcRevision] = 0x10;
    dev->ohci_mmio[OHCI_HcRevision + 1] = 0x01;
    dev->ohci_mmio[OHCI_HcRhDescriptorA] = 0x02;

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
usb_init_ext(const device_t *info, void* params)
{
    usb_t *dev;

    dev = (usb_t *) malloc(sizeof(usb_t));
    if (dev == NULL)
        return (NULL);
    memset(dev, 0x00, sizeof(usb_t));

    dev->usb_params = (usb_params_t*)params;

    mem_mapping_add(&dev->ohci_mmio_mapping, 0, 0,
                    ohci_mmio_read, NULL, NULL,
                    ohci_mmio_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    timer_add(&dev->ohci_frame_timer, ohci_update_frame_counter, dev, 0); /* Unused for now, to be used for frame counting. */
    timer_add(&dev->ohci_port_reset_timer[0], ohci_port_reset_callback, dev, 0);
    timer_add(&dev->ohci_port_reset_timer[1], ohci_port_reset_callback_2, dev, 0);
    timer_add(&dev->ohci_interrupt_desc_poll_timer, ohci_poll_interrupt_descriptors, dev, 0);
    usb_reset(dev);

    return dev;
}

const device_t usb_device = {
    .name          = "Universal Serial Bus",
    .internal_name = "usb",
    .flags         = DEVICE_PCI | DEVICE_EXTPARAMS,
    .local         = 0,
    .init_ext      = usb_init_ext,
    .close         = usb_close,
    .reset         = usb_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
