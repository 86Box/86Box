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
#include <stdbool.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/usb.h>
#include <86box/dma.h>

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
    OHCI_HcRevision         = 0x00 /* 0x00 */,
    OHCI_HcControl          = 0x01 /* 0x04 */,
    OHCI_HcCommandStatus    = 0x02 /* 0x08 */,
    OHCI_HcInterruptStatus  = 0x03 /* 0x0c */,
    OHCI_HcInterruptEnable  = 0x04 /* 0x10 */,
    OHCI_HcInterruptDisable = 0x05 /* 0x14 */,
    OHCI_HcHCCA             = 0x06 /* 0x18 */,
    OHCI_HcPeriodCurrentED  = 0x07 /* 0x1c */,
    OHCI_HcControlHeadED    = 0x08 /* 0x20 */,
    OHCI_HcControlCurrentED = 0x09 /* 0x24 */,
    OHCI_HcBulkHeadED       = 0x0a /* 0x28 */,
    OHCI_HcBulkCurrentED    = 0x0b /* 0x2c */,
    OHCI_HcDoneHead         = 0x0c /* 0x30 */,
    OHCI_HcFmInterval       = 0x0d /* 0x34 */,
    OHCI_HcFmRemaining      = 0x0e /* 0x38 */,
    OHCI_HcFmNumber         = 0x0f /* 0x3c */,
    OHCI_HcPeriodicStart    = 0x10 /* 0x40 */,
    OHCI_HcLSThreshold      = 0x11 /* 0x44 */,
    OHCI_HcRhDescriptorA    = 0x12 /* 0x48 */,
    OHCI_HcRhDescriptorB    = 0x13 /* 0x4c */,
    OHCI_HcRhStatus         = 0x14 /* 0x50 */,
    OHCI_HcRhPortStatus1    = 0x15 /* 0x54 */,
    OHCI_HcRhPortStatus2    = 0x16 /* 0x58 */,
    OHCI_HcRhPortStatus3    = 0x17 /* 0x5c */
};

enum
{
    OHCI_aHcRevision         = 0x00,
    OHCI_aHcControl          = 0x04,
    OHCI_aHcCommandStatus    = 0x08,
    OHCI_aHcInterruptStatus  = 0x0c,
    OHCI_aHcInterruptEnable  = 0x10,
    OHCI_aHcInterruptDisable = 0x14,
    OHCI_aHcHCCA             = 0x18,
    OHCI_aHcPeriodCurrentED  = 0x1c,
    OHCI_aHcControlHeadED    = 0x20,
    OHCI_aHcControlCurrentED = 0x24,
    OHCI_aHcBulkHeadED       = 0x28,
    OHCI_aHcBulkCurrentED    = 0x2c,
    OHCI_aHcDoneHead         = 0x30,
    OHCI_aHcFmInterval       = 0x34,
    OHCI_aHcFmRemaining      = 0x38,
    OHCI_aHcFmNumber         = 0x3c,
    OHCI_aHcPeriodicStart    = 0x40,
    OHCI_aHcLSThreshold      = 0x44,
    OHCI_aHcRhDescriptorA    = 0x48,
    OHCI_aHcRhDescriptorB    = 0x4c,
    OHCI_aHcRhStatus         = 0x50,
    OHCI_aHcRhPortStatus1    = 0x54,
    OHCI_aHcRhPortStatus2    = 0x58,
    OHCI_aHcRhPortStatus3    = 0x5c
};

/* OHCI HcInterruptEnable/Disable bits */
enum
{
    OHCI_HcInterruptEnable_SO = 1 << 0,
    OHCI_HcInterruptEnable_WDH = 1 << 1,
    OHCI_HcInterruptEnable_SF = 1 << 2,
    OHCI_HcInterruptEnable_RD = 1 << 3,
    OHCI_HcInterruptEnable_UE = 1 << 4,
    OHCI_HcInterruptEnable_HNO = 1 << 5,
    OHCI_HcInterruptEnable_RHSC = 1 << 6,
};

/* OHCI HcControl bits */
enum
{
    OHCI_HcControl_ControlBulkServiceRatio = 1 << 0,
    OHCI_HcControl_PeriodicListEnable = 1 << 1,
    OHCI_HcControl_IsochronousEnable = 1 << 2,
    OHCI_HcControl_ControlListEnable = 1 << 3,
    OHCI_HcControl_BulkListEnable = 1 << 4
};

static void
usb_interrupt_ohci(usb_t *dev, uint32_t level)
{
    if (dev->ohci_mmio[OHCI_HcControl].b[1] & 1) {
        if (dev->usb_params && dev->usb_params->smi_handle && !dev->usb_params->smi_handle(dev, dev->usb_params->parent_priv))
            return;

        if (level)
            smi_raise();
    } else if (dev->usb_params != NULL) {
        if ((dev->usb_params->parent_priv != NULL) && (dev->usb_params->update_interrupt != NULL))
            dev->usb_params->update_interrupt(dev, dev->usb_params->parent_priv);
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

typedef struct
{
    uint32_t HccaInterrruptTable[32];
    uint16_t HccaFrameNumber;
    uint16_t HccaPad1;
    uint32_t HccaDoneHead;
} usb_hcca_t;

/* Transfer descriptors */
typedef struct
{
    union
    {
        uint32_t Control;
        struct
        {
            uint32_t Reserved : 18;
            uint8_t bufferRounding : 1;
            uint8_t Direction : 2;
            uint8_t DelayInterrupt : 3;
            uint8_t DataToggle : 2;
            uint8_t ErrorCount : 2;
            uint8_t ConditionCode : 4;
        } flags;
    };
    uint32_t CBP;
    uint32_t NextTD;
    uint32_t BE;
} usb_td_t;

/* Endpoint descriptors */
typedef struct
{
    union
    {
        uint32_t Control;
        struct
        {
            uint8_t FunctionAddress : 7;
            uint8_t EndpointNumber : 4;
            uint8_t Direction : 2;
            bool Speed : 1;
            bool Skip : 1;
            bool Format : 1;
            uint16_t MaximumPacketSize : 11;
            uint8_t Reserved : 5;
        } flags;
    };
    uint32_t TailP;
    union
    {
        uint32_t HeadP;
        struct
        {
            bool Halted : 1;
            bool toggleCarry : 1;
        } flags_2;
    };
    uint32_t NextED;
} usb_ed_t;

#define ENDPOINT_DESC_LIMIT 32

static uint8_t
ohci_mmio_read(uint32_t addr, void *p)
{
    usb_t  *dev = (usb_t *) p;
    uint8_t ret = 0x00;
#ifdef ENABLE_USB_LOG
    uint32_t old_addr = addr;
#endif

    addr &= 0x00000fff;

    ret = dev->ohci_mmio[addr >> 2].b[addr & 3];

    switch (addr) {
        case 0x101:
            ret = (ret & 0xfe) | (!!mem_a20_key);
            break;
        case OHCI_aHcRhPortStatus1 + 1:
        case OHCI_aHcRhPortStatus2 + 1:
        case OHCI_aHcRhPortStatus3 + 1:
            ret |= 0x1;
            break;
        case OHCI_aHcInterruptDisable:
        case OHCI_aHcInterruptDisable + 1:
        case OHCI_aHcInterruptDisable + 2:
        case OHCI_aHcInterruptDisable + 3:
            ret = dev->ohci_mmio[OHCI_HcInterruptEnable].b[addr & 3];
        default:
            break;
    }

    if (addr == 0x101)
        ret = (ret & 0xfe) | (!!mem_a20_key);

#ifdef ENABLE_USB_LOG
    usb_log("[R] %08X = %04X\n", old_addr, ret);
#endif

    return ret;
}

static uint16_t
ohci_mmio_readw(uint32_t addr, void *p)
{
    return ohci_mmio_read(addr, p) | (ohci_mmio_read(addr + 1, p) << 8);
}

static uint32_t
ohci_mmio_readl(uint32_t addr, void *p)
{
    return ohci_mmio_readw(addr, p) | (ohci_mmio_readw(addr + 2, p) << 16);
}

static void
ohci_update_irq(usb_t *dev)
{
    uint32_t level = !!(dev->ohci_mmio[OHCI_HcInterruptStatus].l & dev->ohci_mmio[OHCI_HcInterruptEnable].l);

    if (level != dev->irq_level) {
        dev->irq_level = level;
        usb_interrupt_ohci(dev, level);
    }
}

void
ohci_set_interrupt(usb_t *dev, uint8_t bit)
{
    if (!(dev->ohci_mmio[OHCI_HcInterruptEnable].b[3] & 0x80))
        return;
    
    if (!(dev->ohci_mmio[OHCI_HcInterruptEnable].b[0] & bit))
        return;

    if (dev->ohci_mmio[OHCI_HcInterruptDisable].b[0] & bit)
        return;

    dev->ohci_mmio[OHCI_HcInterruptStatus].b[0] |= bit;

    ohci_update_irq(dev);
}

uint8_t
ohci_service_endpoint_desc(usb_t* dev, uint32_t head)
{
    usb_ed_t endpoint_desc;

    return 0;
}

void
ohci_end_of_frame(usb_t* dev)
{
    usb_hcca_t hcca;
    /* TODO: Put endpoint and transfer descriptor processing here. */
    dma_bm_read(dev->ohci_mmio[OHCI_HcHCCA].l, (uint8_t*)&hcca, sizeof(usb_hcca_t), 4);

    if (dev->ohci_mmio[OHCI_HcControl].l & OHCI_HcControl_PeriodicListEnable) {
        ohci_service_endpoint_desc(dev, hcca.HccaInterrruptTable[dev->ohci_mmio[OHCI_HcFmNumber].l & 0x1f]);
    }

    if ((dev->ohci_mmio[OHCI_HcControl].l & OHCI_HcControl_ControlListEnable)
        && (dev->ohci_mmio[OHCI_HcCommandStatus].l & 0x2)) {
        uint8_t result = ohci_service_endpoint_desc(dev, dev->ohci_mmio[OHCI_HcControlHeadED].l);
        if (!result) {
            dev->ohci_mmio[OHCI_HcControlHeadED].l = 0;
            dev->ohci_mmio[OHCI_HcCommandStatus].l &= ~0x2;
        }
    }

    if ((dev->ohci_mmio[OHCI_HcControl].l & OHCI_HcControl_BulkListEnable)
        && (dev->ohci_mmio[OHCI_HcCommandStatus].l & 0x4)) {
        uint8_t result = ohci_service_endpoint_desc(dev, dev->ohci_mmio[OHCI_HcBulkHeadED].l);
        if (!result) {
            dev->ohci_mmio[OHCI_HcBulkHeadED].l = 0;
            dev->ohci_mmio[OHCI_HcCommandStatus].l &= ~0x4;
        }
    }

    if (dev->ohci_interrupt_counter == 0 && !(dev->ohci_mmio[OHCI_HcInterruptStatus].l & OHCI_HcInterruptEnable_WDH)) {
        if (dev->ohci_mmio[OHCI_HcDoneHead].l == 0) {
            fatal("OHCI: HcDoneHead is still NULL!");
        }

        if (dev->ohci_mmio[OHCI_HcInterruptStatus].l & dev->ohci_mmio[OHCI_HcInterruptEnable].l) {
            dev->ohci_mmio[OHCI_HcDoneHead].l |= 1;
        }

        hcca.HccaDoneHead = dev->ohci_mmio[OHCI_HcDoneHead].l;
        dev->ohci_mmio[OHCI_HcDoneHead].l = 0;
        dev->ohci_interrupt_counter = 7;
        ohci_set_interrupt(dev, OHCI_HcInterruptEnable_WDH);
    }

    if (dev->ohci_interrupt_counter != 0 && dev->ohci_interrupt_counter != 7) {
        dev->ohci_interrupt_counter--;
    }

    dev->ohci_mmio[OHCI_HcFmNumber].w[0]++;
    hcca.HccaFrameNumber = dev->ohci_mmio[OHCI_HcFmNumber].w[0];

    dma_bm_write(dev->ohci_mmio[OHCI_HcHCCA].l, (uint8_t*)&hcca, sizeof(usb_hcca_t), 4);
}

void
ohci_start_of_frame(usb_t* dev)
{
    ohci_set_interrupt(dev, OHCI_HcInterruptEnable_SO);
}

void
ohci_update_frame_counter(void* priv)
{
    usb_t *dev = (usb_t *) priv;

    dev->ohci_mmio[OHCI_HcFmRemaining].w[0] &= 0x3fff;
    if (dev->ohci_mmio[OHCI_HcFmRemaining].w[0] == 0) {
        ohci_end_of_frame(dev);
        dev->ohci_mmio[OHCI_HcFmRemaining].w[0] = dev->ohci_mmio[OHCI_HcFmInterval].w[0] & 0x3fff;
        dev->ohci_mmio[OHCI_HcFmRemaining].l &= ~(1 << 31);
        dev->ohci_mmio[OHCI_HcFmRemaining].l |= dev->ohci_mmio[OHCI_HcFmInterval].l & (1 << 31);
        ohci_start_of_frame(dev);
        timer_on_auto(&dev->ohci_frame_timer, 1. / 12.);
        return;
    }
    dev->ohci_mmio[OHCI_HcFmRemaining].w[0]--;
    timer_on_auto(&dev->ohci_frame_timer, 1. / 12.);
}

void
ohci_port_reset_callback(void* priv)
{
    usb_t *dev = (usb_t *) priv;

    dev->ohci_mmio[OHCI_HcRhPortStatus1].b[0] &= ~0x10;
    dev->ohci_mmio[OHCI_HcRhPortStatus1].b[2] |= 0x10;
}

void
ohci_port_reset_callback_2(void* priv)
{
    usb_t *dev = (usb_t *) priv;

    dev->ohci_mmio[OHCI_HcRhPortStatus2].b[0] &= ~0x10;
    dev->ohci_mmio[OHCI_HcRhPortStatus2].b[2] |= 0x10;
}

static void
ohci_mmio_write(uint32_t addr, uint8_t val, void *p)
{
    usb_t  *dev = (usb_t *) p;
    uint8_t old;

#ifdef ENABLE_USB_LOG
    usb_log("[W] %08X = %04X\n", addr, val);
#endif

    addr &= 0x00000fff;

    switch (addr) {
        case OHCI_aHcControl:
            if ((val & 0xc0) == 0x00) {
                /* UsbReset */
                dev->ohci_mmio[OHCI_HcRhPortStatus1].b[2] = dev->ohci_mmio[OHCI_HcRhPortStatus2].b[2] = 0x16;
            }
            break;
        case OHCI_aHcCommandStatus:
            /* bit OwnershipChangeRequest triggers an ownership change (SMM <-> OS) */
            if (val & 0x08) {
                dev->ohci_mmio[OHCI_HcInterruptStatus].b[3] = 0x40;
                if ((dev->ohci_mmio[OHCI_HcInterruptEnable].b[3] & 0xc0) == 0xc0)
                    smi_raise();
            }

            /* bit HostControllerReset must be cleared for the controller to be seen as initialized */
            if (val & 0x01) {
                memset(dev->ohci_mmio, 0x00, 4096);
                dev->ohci_mmio[OHCI_HcRevision].b[0] = 0x10;
                dev->ohci_mmio[OHCI_HcRevision].b[1] = 0x01;
                dev->ohci_mmio[OHCI_HcRhDescriptorA].b[0] = 0x02;
                val &= ~0x01;
            }
            break;
        case OHCI_aHcHCCA:
            return;
        case OHCI_aHcInterruptEnable:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0x7f);
            dev->ohci_mmio[OHCI_HcInterruptDisable].b[0] &= ~(val & 0x7f);
            ohci_update_irq(dev);
            return;
        case OHCI_aHcInterruptEnable + 1:
        case OHCI_aHcInterruptEnable + 2:
            return;
        case OHCI_aHcInterruptEnable + 3:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0xc0);
            dev->ohci_mmio[OHCI_HcInterruptDisable].b[3] &= ~(val & 0xc0);
            ohci_update_irq(dev);
            return;
        case OHCI_aHcInterruptDisable:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0x7f);
            dev->ohci_mmio[OHCI_HcInterruptEnable].b[0] &= ~(val & 0x7f);
            ohci_update_irq(dev);
            return;
        case OHCI_aHcInterruptDisable + 1:
        case OHCI_aHcInterruptDisable + 2:
            return;
        case OHCI_aHcInterruptDisable + 3:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0xc0);
            dev->ohci_mmio[OHCI_HcInterruptEnable].b[3] &= ~(val & 0xc0);
            ohci_update_irq(dev);
            return;
        case OHCI_aHcInterruptStatus:
            dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~(val & 0x7f);
            return;
        case OHCI_aHcInterruptStatus + 1:
        case OHCI_aHcInterruptStatus + 2:
            return;
        case OHCI_aHcInterruptStatus + 3:
            dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~(val & 0x40);
            return;
        case OHCI_aHcFmRemaining + 3:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0x80);
            return;
        case OHCI_aHcFmRemaining + 1:
        case OHCI_aHcPeriodicStart + 1:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0x3f);
            return;
        case OHCI_aHcLSThreshold + 1:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0x0f);
            return;
        case OHCI_aHcFmRemaining + 2:
        case OHCI_aHcFmNumber + 2:
        case OHCI_aHcFmNumber + 3:
        case OHCI_aHcPeriodicStart + 2:
        case OHCI_aHcPeriodicStart + 3:
        case OHCI_aHcLSThreshold + 2:
        case OHCI_aHcLSThreshold + 3:
        case OHCI_aHcRhDescriptorA:
        case OHCI_aHcRhDescriptorA + 2:
            return;
        case OHCI_aHcRhDescriptorA + 1:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0x1b);
            if (val & 0x02) {
                dev->ohci_mmio[OHCI_HcRhPortStatus1].b[1] |= 0x01;
                dev->ohci_mmio[OHCI_HcRhPortStatus2].b[1] |= 0x01;
            }
            return;
        case OHCI_aHcRhDescriptorA + 3:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0x03);
            return;
        case OHCI_aHcRhDescriptorB:
        case OHCI_aHcRhDescriptorB + 2:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0x06);
            if ((addr == OHCI_HcRhDescriptorB) && !(val & 0x04)) {
                if (!(dev->ohci_mmio[OHCI_HcRhPortStatus2].b[0] & 0x01))
                    dev->ohci_mmio[OHCI_HcRhPortStatus2].b[2] |= 0x01;
                dev->ohci_mmio[OHCI_HcRhPortStatus2].b[0] |= 0x01;
            }
            if ((addr == OHCI_HcRhDescriptorB) && !(val & 0x02)) {
                if (!(dev->ohci_mmio[OHCI_HcRhPortStatus1].b[0] & 0x01))
                    dev->ohci_mmio[OHCI_HcRhPortStatus1].b[2] |= 0x01;
                dev->ohci_mmio[OHCI_HcRhPortStatus1].b[0] |= 0x01;
            }
            return;
        case OHCI_aHcRhDescriptorB + 1:
        case OHCI_aHcRhDescriptorB + 3:
            return;
        case OHCI_aHcRhStatus:
            if (val & 0x01) {
                if ((dev->ohci_mmio[OHCI_HcRhDescriptorA].b[1] & 0x03) == 0x00) {
                    dev->ohci_mmio[OHCI_HcRhPortStatus1].b[1] &= ~0x01;
                    dev->ohci_mmio[OHCI_HcRhPortStatus1].b[0] &= ~0x17;
                    dev->ohci_mmio[OHCI_HcRhPortStatus1].b[2] &= ~0x17;
                    dev->ohci_mmio[OHCI_HcRhPortStatus2].b[1] &= ~0x01;
                    dev->ohci_mmio[OHCI_HcRhPortStatus2].b[0] &= ~0x17;
                    dev->ohci_mmio[OHCI_HcRhPortStatus2].b[2] &= ~0x17;
                } else if ((dev->ohci_mmio[OHCI_HcRhDescriptorA].b[1] & 0x03) == 0x01) {
                    if (!(dev->ohci_mmio[OHCI_HcRhDescriptorB].b[2] & 0x02)) {
                        dev->ohci_mmio[OHCI_HcRhPortStatus1].b[1] &= ~0x01;
                        dev->ohci_mmio[OHCI_HcRhPortStatus1].b[0] &= ~0x17;
                        dev->ohci_mmio[OHCI_HcRhPortStatus1].b[2] &= ~0x17;
                    }
                    if (!(dev->ohci_mmio[OHCI_HcRhDescriptorB].b[2] & 0x04)) {
                        dev->ohci_mmio[OHCI_HcRhPortStatus2].b[1] &= ~0x01;
                        dev->ohci_mmio[OHCI_HcRhPortStatus2].b[0] &= ~0x17;
                        dev->ohci_mmio[OHCI_HcRhPortStatus2].b[2] &= ~0x17;
                    }
                }
            }
            return;
        case OHCI_aHcRhStatus + 1:
            if (val & 0x80)
                dev->ohci_mmio[addr >> 2].b[addr & 3] |= 0x80;
            return;
        case OHCI_aHcRhStatus + 2:
            dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~(val & 0x02);
            if (val & 0x01) {
                if ((dev->ohci_mmio[OHCI_HcRhDescriptorA].b[1] & 0x03) == 0x00) {
                    dev->ohci_mmio[OHCI_HcRhPortStatus1].b[1] |= 0x01;
                    dev->ohci_mmio[OHCI_HcRhPortStatus2].b[1] |= 0x01;
                } else if ((dev->ohci_mmio[OHCI_HcRhDescriptorA].b[1] & 0x03) == 0x01) {
                    if (!(dev->ohci_mmio[OHCI_HcRhDescriptorB].b[2] & 0x02))
                        dev->ohci_mmio[OHCI_HcRhPortStatus1].b[1] |= 0x01;
                    if (!(dev->ohci_mmio[OHCI_HcRhDescriptorB].b[2] & 0x04))
                        dev->ohci_mmio[OHCI_HcRhPortStatus2].b[1] |= 0x01;
                }
            }
            return;
        case OHCI_aHcRhStatus + 3:
            if (val & 0x80)
                dev->ohci_mmio[OHCI_HcRhStatus].b[1] &= ~0x80;
            return;
        case OHCI_aHcRhPortStatus1:
        case OHCI_aHcRhPortStatus2:
            old = dev->ohci_mmio[addr >> 2].b[addr & 3];

            if (val & 0x10) {
                if (old & 0x01) {
                    dev->ohci_mmio[addr >> 2].b[addr & 3] |= 0x10;
                    timer_on_auto(&dev->ohci_port_reset_timer[(addr - OHCI_aHcRhPortStatus1) / 4], 10000.);
                } else
                    dev->ohci_mmio[(addr + 2) >> 2].b[(addr + 2) & 3] |= 0x01;
            }
            if (val & 0x08)
                dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~0x04;
            if (val & 0x04)
                dev->ohci_mmio[addr >> 2].b[addr & 3] |= 0x04;
            if (val & 0x02) {
                if (old & 0x01)
                    dev->ohci_mmio[addr >> 2].b[addr & 3] |= 0x02;
                else
                    dev->ohci_mmio[(addr + 2) >> 2].b[(addr + 2) & 3] |= 0x01;
            }
            if (val & 0x01) {
                if (old & 0x01)
                    dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~0x02;
                else
                    dev->ohci_mmio[(addr + 2) >> 2].b[(addr + 2) & 3] |= 0x01;
            }

            if (!(dev->ohci_mmio[addr >> 2].b[addr & 3] & 0x04) && (old & 0x04))
                dev->ohci_mmio[(addr + 2) >> 2].b[(addr + 2) & 3] |= 0x04;
            /* if (!(dev->ohci_mmio[addr >> 2].b[addr & 3] & 0x02))
                    dev->ohci_mmio[(addr + 2) >> 2].b[(addr + 2) & 3] |= 0x02; */
            return;
        case OHCI_aHcRhPortStatus1 + 1:
            if ((val & 0x02) && ((dev->ohci_mmio[OHCI_HcRhDescriptorA].b[1] & 0x03) == 0x00) && (dev->ohci_mmio[OHCI_HcRhDescriptorB].b[2] & 0x02)) {
                dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~0x01;
                dev->ohci_mmio[OHCI_HcRhPortStatus1].b[0] &= ~0x17;
                dev->ohci_mmio[OHCI_HcRhPortStatus1].b[2] &= ~0x17;
            }
            if ((val & 0x01) && ((dev->ohci_mmio[OHCI_HcRhDescriptorA].b[1] & 0x03) == 0x00) && (dev->ohci_mmio[OHCI_HcRhDescriptorB].b[2] & 0x02)) {
                dev->ohci_mmio[addr >> 2].b[addr & 3] |= 0x01;
                dev->ohci_mmio[OHCI_HcRhPortStatus2].b[0] &= ~0x17;
                dev->ohci_mmio[OHCI_HcRhPortStatus2].b[2] &= ~0x17;
            }
            return;
        case OHCI_aHcRhPortStatus2 + 1:
            if ((val & 0x02) && ((dev->ohci_mmio[OHCI_HcRhDescriptorA].b[1] & 0x03) == 0x00) && (dev->ohci_mmio[OHCI_HcRhDescriptorB].b[2] & 0x04))
                dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~0x01;
            if ((val & 0x01) && ((dev->ohci_mmio[OHCI_HcRhDescriptorA].b[1] & 0x03) == 0x00) && (dev->ohci_mmio[OHCI_HcRhDescriptorB].b[2] & 0x04))
                dev->ohci_mmio[addr >> 2].b[addr & 3] |= 0x01;
            return;
        case OHCI_aHcRhPortStatus1 + 2:
        case OHCI_aHcRhPortStatus2 + 2:
            dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~(val & 0x1f);
            return;
        case OHCI_aHcRhPortStatus1 + 3:
        case OHCI_aHcRhPortStatus2 + 3:
            return;
        case OHCI_aHcDoneHead:
        case OHCI_aHcBulkCurrentED:
        case OHCI_aHcBulkHeadED:
        case OHCI_aHcControlCurrentED:
        case OHCI_aHcControlHeadED:
        case OHCI_aHcPeriodCurrentED:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0xf0);
            return;
    }

    dev->ohci_mmio[addr >> 2].b[addr & 3] = val;
}

static void
ohci_mmio_writew(uint32_t addr, uint16_t val, void *p)
{
    ohci_mmio_write(addr, val & 0xff, p);
    ohci_mmio_write(addr + 1, val >> 8, p);
}

static void
ohci_mmio_writel(uint32_t addr, uint32_t val, void *p)
{
    ohci_mmio_writew(addr, val & 0xffff, p);
    ohci_mmio_writew(addr + 2, val >> 16, p);
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

    usb_log("ohci_update_mem_mapping(): OHCI %sabled at %08X\n", dev->ohci_enable ? "en" : "dis", dev->ohci_mem_base);
}

uint8_t
usb_attach_device(usb_t *dev, usb_device_t* device, uint8_t bus_type)
{
    return 255;
}

void
usb_detach_device(usb_t *dev, uint8_t port, uint8_t bus_type)
{
    /* Unused. */
}

static void
usb_reset(void *priv)
{
    usb_t *dev = (usb_t *) priv;

    memset(dev->uhci_io, 0x00, sizeof(dev->uhci_io));
    dev->uhci_io[0x0c] = 0x40;
    dev->uhci_io[0x10] = dev->uhci_io[0x12] = 0x80;

    memset(dev->ohci_mmio, 0x00, sizeof(dev->ohci_mmio));
    dev->ohci_mmio[OHCI_HcRevision].b[0] = 0x10;
    dev->ohci_mmio[OHCI_HcRevision].b[1] = 0x01;
    dev->ohci_mmio[OHCI_HcRhDescriptorA].b[0] = 0x02;
    dev->ohci_mmio[OHCI_HcRhDescriptorA].b[1] = 0x02;

    io_removehandler(dev->uhci_io_base, 0x20, uhci_reg_read, NULL, NULL, uhci_reg_write, uhci_reg_writew, NULL, dev);
    dev->uhci_enable = 0;

    mem_mapping_disable(&dev->ohci_mmio_mapping);
    dev->ohci_enable = 0;

    usb_log("usb_reset(): OHCI %sabled at %08X\n", dev->ohci_enable ? "en" : "dis", dev->ohci_mem_base);
    usb_log("usb_reset(): map = %08X\n", &dev->ohci_mmio_mapping);
}

static void
usb_close(void *priv)
{
    usb_t *dev = (usb_t *) priv;

    free(dev);
}

static void *
usb_init_ext(const device_t *info, void *params)
{
    usb_t *dev;

    dev = (usb_t *) malloc(sizeof(usb_t));
    if (dev == NULL)
        return (NULL);
    memset(dev, 0x00, sizeof(usb_t));

    dev->usb_params = (usb_params_t *) params;

    mem_mapping_add(&dev->ohci_mmio_mapping, 0, 0x1000,
                    ohci_mmio_read, ohci_mmio_readw, ohci_mmio_readl,
                    ohci_mmio_write, ohci_mmio_writew, ohci_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    mem_mapping_disable(&dev->ohci_mmio_mapping);

    timer_add(&dev->ohci_frame_timer, ohci_update_frame_counter, dev, 0); /* Unused for now, to be used for frame counting. */
    timer_add(&dev->ohci_port_reset_timer[0], ohci_port_reset_callback, dev, 0);
    timer_add(&dev->ohci_port_reset_timer[1], ohci_port_reset_callback_2, dev, 0);

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
