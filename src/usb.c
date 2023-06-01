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
#include <assert.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/fifo8.h>
#include <86box/usb.h>
#include <86box/dma.h>

//#define ENABLE_USB_LOG 1
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

usb_t* usb_device_inst = NULL;

static void
usb_interrupt_ohci(usb_t *dev, uint32_t level)
{
    pclog("OHCI: Interrupt level %d ", level);
    if (dev->ohci_mmio[OHCI_HcControl].b[1] & 1) {
        pclog("(SMI)\n");
        if (dev->usb_params && dev->usb_params->smi_handle && !dev->usb_params->smi_handle(dev, dev->usb_params->parent_priv))
            return;

        if (level)
            smi_raise();
    } else if (dev->usb_params != NULL) {
        pclog("(normal)\n");
        if ((dev->usb_params->parent_priv != NULL) && (dev->usb_params->update_interrupt != NULL))
            dev->usb_params->update_interrupt(dev, dev->usb_params->parent_priv);
    }
}

static uint8_t
uhci_reg_read(uint16_t addr, void *p)
{
    usb_t   *dev = (usb_t *) p;
    uint8_t  ret;
    uint8_t *regs = dev->uhci_io;

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
    uint32_t Control;
    uint32_t CBP;
    uint32_t NextTD;
    uint32_t BE;
} usb_td_t;

/* Endpoint descriptors */
typedef struct
{
    uint32_t Control;
    uint32_t TailP;
    uint32_t HeadP;
    uint32_t NextED;
} usb_ed_t;

#define ENDPOINT_DESC_LIMIT 32

static uint16_t
ohci_get_remaining_frame_time(usb_t* usb)
{
    uint32_t remaining = timer_get_remaining_us(&usb->ohci_frame_timer);

    if ((usb->ohci_mmio[OHCI_HcControl].l & 0xc0) != 0x80) {
        return 0;
    }

    if (remaining > 1000)
        remaining = 1000;

    return (uint16_t)(remaining * 12.);
}

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
        case OHCI_aHcInterruptDisable:
        case OHCI_aHcInterruptDisable + 1:
        case OHCI_aHcInterruptDisable + 2:
        case OHCI_aHcInterruptDisable + 3:
            ret = dev->ohci_mmio[OHCI_HcInterruptEnable].b[addr & 3];
            break;
        case OHCI_aHcFmRemaining + 3:
        case OHCI_aHcFmRemaining + 2:
        case OHCI_aHcFmRemaining + 1:
        case OHCI_aHcFmRemaining:
        {
            uint32_t remaining = ohci_get_remaining_frame_time(dev);
            ret = ((remaining >> ((addr - OHCI_aHcFmRemaining) * 8)) & 0xFF);
            if (addr == (OHCI_aHcFmRemaining + 3)) {
                ret |= !!(dev->ohci_mmio[OHCI_HcFmInterval].l & 0x80000000) << 7;
                update_tsc();
            }
            break;
        }
        case OHCI_aHcControl + 3:
        {
            update_tsc();
            break;
        }
        case OHCI_aHcHCCA:
        {
            ret = 0x00;
            break;
        }
        default:
            {
                if (addr >= OHCI_aHcRhPortStatus1 && (addr & 0x3) == 1) {
                    ret |= 0x1;
                }
                break;
            }
    }

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
    //pclog("dev->ohci_mmio[OHCI_HcInterruptStatus].l = 0x%08X, dev->ohci_mmio[OHCI_HcInterruptEnable].l = 0x%08X\n", dev->ohci_mmio[OHCI_HcInterruptStatus].l, dev->ohci_mmio[OHCI_HcInterruptEnable].l);

    if (!(dev->ohci_mmio[OHCI_HcInterruptEnable].l & (1 << 31))) {
        level = 0;
    }

    //pclog("level = %d\n", level);

    if (level != dev->irq_level) {
        dev->irq_level = level;
        usb_interrupt_ohci(dev, level);
    }
}

void
ohci_set_interrupt(usb_t *dev, uint8_t bit)
{    
    dev->ohci_mmio[OHCI_HcInterruptStatus].b[0] |= bit;
    //pclog("OHCI: Interrupt bit 0x%X\n", bit);

    /* TODO: Does setting UnrecoverableError also assert PERR# on any emulated USB chipsets? */

    ohci_update_irq(dev);
}

/* Next two functions ported over from QEMU. */
static int ohci_copy_td_input(usb_t* dev, usb_td_t *td,
                        uint8_t *buf, int len)
{
    uint32_t ptr;
    uint32_t n;

    ptr = td->CBP;
    n = 0x1000 - (ptr & 0xfff);
    if (n > len) {
        n = len;
    }
    dma_bm_write(ptr, buf, n, 1);
    if (n == len) {
        return 0;
    }
    ptr = td->BE & ~0xfffu;
    buf += n;
    dma_bm_write(ptr, buf, len - n, 1);
    return 0;
}

static int ohci_copy_td_output(usb_t* dev, usb_td_t *td,
                        uint8_t *buf, int len)
{
    uint32_t ptr;
    uint32_t n;

    ptr = td->CBP;
    n = 0x1000 - (ptr & 0xfff);
    if (n > len) {
        n = len;
    }
    dma_bm_read(ptr, buf, n, 1);
    if (n == len) {
        return 0;
    }
    ptr = td->BE & ~0xfffu;
    buf += n;
    dma_bm_read(ptr, buf, len - n, 1);
    return 0;
}

#define OHCI_TD_DIR(val) ((val >> 19) & 3)
#define OHCI_ED_DIR(val) ((val >> 11) & 3)

uint8_t
ohci_service_transfer_desc(usb_t* dev, usb_ed_t* endpoint_desc)
{
    uint32_t td_addr = endpoint_desc->HeadP & ~0xf;
    usb_td_t td;
    uint8_t dir;
    uint8_t pid_token = 255;
    uint32_t len = 0;
    uint32_t pktlen = 0;
    uint32_t actual_length = 0;
    uint32_t i = 0;
    uint8_t device_result = 0;
    usb_device_t* target = NULL;

    dma_bm_read(td_addr, (uint8_t*)&td, sizeof(usb_td_t), 4);
    pclog("td.Control (b) = 0x%08X\n", td.Control);

    switch (dir = OHCI_ED_DIR(endpoint_desc->Control)) {
        case 1:
        case 2:
            break;
        default:
            dir = OHCI_TD_DIR(td.Control);
            break;
    }

    switch (dir) {
        case 0: /* Setup */
            pid_token = USB_PID_SETUP;
            break;
        case 1: /* OUT */
            pid_token = USB_PID_OUT;
            break;
        case 2: /* IN */
            pid_token = USB_PID_IN;
            break;
        default:
            return 1;
    }

    if (td.CBP && td.BE) {
        if ((td.CBP & 0xfffff000) != (td.BE & 0xfffff000)) {
            len = (td.BE & 0xfff) + 0x1001 - (td.CBP & 0xfff);
        } else {
            if (td.CBP > td.BE) {
                ohci_set_interrupt(dev, OHCI_HcInterruptEnable_UE);
                return 1;
            }

            len = (td.BE - td.CBP) + 1;
        }
        if (len > sizeof(dev->ohci_usb_buf)) {
            len = sizeof(dev->ohci_usb_buf);
        }

        pktlen = len;
        if (len && pid_token != USB_PID_IN) {
            pktlen = (endpoint_desc->Control >> 16) & 0xFFF;
            if (pktlen > len) {
                pktlen = len;
            }
            ohci_copy_td_output(dev, &td, dev->ohci_usb_buf, pktlen);
        }
    }

    for (i = 0; i < 2; i++) {
        if (!dev->ohci_devices[i])
            continue;


        if (dev->ohci_devices[i]->address != (endpoint_desc->Control & 0x7f))
            continue;
        
        target = dev->ohci_devices[i];
        break;
    }

    if (!target)
        return 1;

    actual_length = pktlen;

    device_result = target->device_process(target->priv, dev->ohci_usb_buf, &actual_length, pid_token, (endpoint_desc->Control & 0x780) >> 7, !(endpoint_desc->Control & (1 << 18)));

    pclog("Device result: 0x%X\n", device_result);

    if (device_result == USB_ERROR_NO_ERROR) {
        if (pid_token == USB_PID_IN) {
            ohci_copy_td_input(dev, &td, dev->ohci_usb_buf, actual_length);
        } else {
            actual_length = pktlen;
        }
    }

    if ((actual_length == pktlen) || (pid_token == USB_PID_IN && (td.Control & (1 << 18)) && device_result == USB_ERROR_NO_ERROR)) {
        if (len == actual_length) {
            td.CBP = 0;
        } else {
            if ((td.CBP & 0xfff) + actual_length > 0xfff) {
                td.CBP = (td.BE & ~0xfff) + ((td.CBP + actual_length) & 0xfff);
            } else {
                td.CBP += actual_length;
            }
        }

        td.Control |= (1 << 25); /* dataToggle[1] */
        td.Control ^= (1 << 24); /* dataToggle[0] */
        td.Control &= ~0xFC000000; /* Set both ErrorCount and ConditionCode to 0. */

        if (pid_token != USB_PID_IN && len != actual_length) {
            goto exit_no_retire;
        }

        endpoint_desc->HeadP &= ~0x2;
        if (td.Control & (1 << 24)) {
            endpoint_desc->HeadP |= 0x2;
        }
    } else {
        if (actual_length != 0xFFFFFFFF && actual_length >= 0) {
            td.Control &= ~0xF0000000;
            td.Control |= (0x9 << 28);
        } else {
            switch (device_result) {
                case USB_ERROR_NAK:
                    return 1;
                case USB_ERROR_STALL:
                    td.Control |= (0x4 << 28); /* STALL PID returned */
                    break;
                case USB_ERROR_OVERRUN:
                    td.Control |= (0x8 << 28); /* Data overrun from endpoint. */
                    break;
                case USB_ERROR_UNDERRUN:
                    td.Control |= (0x9 << 28); /* Data underrun from endpoint. */
                    break;
            }
            dev->ohci_interrupt_counter = 0;
        }

        endpoint_desc->HeadP |= 0x1;
    }

    endpoint_desc->HeadP &= 0xf;
    endpoint_desc->HeadP |= td.NextTD & ~0xf;
    td.NextTD = dev->ohci_mmio[OHCI_HcDoneHead].l;
    dev->ohci_mmio[OHCI_HcDoneHead].l = td_addr;
    i = (td.Control >> 21) & 7;
    if (i < dev->ohci_interrupt_counter) {
        dev->ohci_interrupt_counter = i;
    }
exit_no_retire:
    dma_bm_write(td_addr, (uint8_t*)&td, sizeof(usb_td_t), 4);
    pclog("td.Control (a) = 0x%08X\n", td.Control);
    return (td.Control & 0xF0000000);
}

uint8_t
ohci_service_endpoint_desc(usb_t* dev, uint32_t head)
{
    usb_ed_t endpoint_desc;
    uint8_t active = 0;
    uint32_t next = 0;
    uint32_t limit_counter = 0;
    
    if (head == 0)
        return 0;

    for (uint32_t cur = head; cur && limit_counter++ < ENDPOINT_DESC_LIMIT; cur = next) {
        dma_bm_read(cur, (uint8_t*)&endpoint_desc, sizeof(usb_ed_t), 4);

        next = endpoint_desc.NextED & ~0xFu;

        //pclog("endpoint_desc.Control = 0x%X\n", endpoint_desc.Control);

        if ((endpoint_desc.Control & (1 << 14)) || (endpoint_desc.HeadP & (1 << 0)))
            //pclog("Skipping endpoint 0x%X to 0x%X\n", cur, next);
            continue;

        if (endpoint_desc.Control & 0x8000) {
            fatal("OHCI: Isochronous transfers not implemented!\n");
        }

        active = 1;

        while ((endpoint_desc.HeadP & ~0xFu) != endpoint_desc.TailP) {
            if (ohci_service_transfer_desc(dev, &endpoint_desc))
                break;
        }

        dma_bm_write(cur, (uint8_t*)&endpoint_desc, sizeof(usb_ed_t), 4);
    }

    return active;
}

void
ohci_end_of_frame(usb_t* dev)
{
    usb_hcca_t hcca;
    if (dev->ohci_mmio[OHCI_HcHCCA].l == 0x00) {
        ohci_set_interrupt(dev, OHCI_HcInterruptEnable_UE);
        return;
    }
    dma_bm_read(dev->ohci_mmio[OHCI_HcHCCA].l & ~0xFF, (uint8_t*)&hcca, sizeof(usb_hcca_t), 4);

//    pclog("dev->ohci_mmio[OHCI_HcControl].l = 0x%08X\n", dev->ohci_mmio[OHCI_HcControl].l);

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

    dma_bm_write(dev->ohci_mmio[OHCI_HcHCCA].l & ~0xFF, (uint8_t*)&hcca, sizeof(usb_hcca_t), 4);
}

void
ohci_start_of_frame(usb_t* dev)
{
    ohci_set_interrupt(dev, OHCI_HcInterruptEnable_SF);
    //pclog("OHCI: Start of frame 0x%X\n", dev->ohci_mmio[OHCI_HcFmNumber].w[0]);
    timer_on_auto(&dev->ohci_frame_timer, 999.);
}

void
ohci_frame_boundary(void* priv)
{
    usb_t *dev = (usb_t *) priv;
    ohci_end_of_frame(dev);
    ohci_start_of_frame(dev);
}

void
ohci_port_reset_callback(void* priv)
{
    usb_t *dev = (usb_t *) priv;

    dev->ohci_mmio[OHCI_HcRhPortStatus1].b[0] &= ~0x10;
    dev->ohci_mmio[OHCI_HcRhPortStatus1].b[2] |= 0x10;
    ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RHSC);
}

void
ohci_port_reset_callback_2(void* priv)
{
    usb_t *dev = (usb_t *) priv;

    dev->ohci_mmio[OHCI_HcRhPortStatus2].b[0] &= ~0x10;
    dev->ohci_mmio[OHCI_HcRhPortStatus2].b[2] |= 0x10;
    ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RHSC);
}

static void
ohci_soft_reset(usb_t* dev)
{
    uint32_t old_HcControl = (dev->ohci_mmio[OHCI_HcControl].l & 0x100) | 0xc0;
    dev->ohci_mmio[OHCI_HcRevision].b[0] = 0x10;
    dev->ohci_mmio[OHCI_HcRevision].b[1] = 0x01;
    dev->ohci_mmio[OHCI_HcFmInterval].l = 0x27782edf; /* FrameInterval = 11999, FSLargestDataPacket = 10104 */
    dev->ohci_mmio[OHCI_HcFmNumber].l = 0x0;
    dev->ohci_mmio[OHCI_HcLSThreshold].l = 0x628;
    dev->ohci_mmio[OHCI_HcInterruptStatus].l = 0;
    dev->ohci_mmio[OHCI_HcInterruptEnable].l = (1 << 31);
    dev->ohci_mmio[OHCI_HcInterruptDisable].l = 0;
    dev->ohci_mmio[OHCI_HcControl].l = old_HcControl;
    dev->ohci_mmio[OHCI_HcBulkHeadED].l = dev->ohci_mmio[OHCI_HcBulkCurrentED].l = 0;
    dev->ohci_mmio[OHCI_HcControlHeadED].l = dev->ohci_mmio[OHCI_HcControlCurrentED].l = 0;
    dev->ohci_mmio[OHCI_HcPeriodCurrentED].l = 0;
    dev->ohci_mmio[OHCI_HcHCCA].l = 0;
    dev->ohci_mmio[OHCI_HcBulkHeadED].l = dev->ohci_mmio[OHCI_HcDoneHead].l = 0;
    dev->ohci_mmio[OHCI_HcCommandStatus].l = 0x00;
    dev->ohci_interrupt_counter = 7;
    timer_disable(&dev->ohci_frame_timer);
    ohci_update_irq(dev);
}

static void
ohci_rhport_reset(usb_t* dev)
{
    dev->ohci_mmio[OHCI_HcRhPortStatus1].l = dev->ohci_mmio[OHCI_HcRhPortStatus2].l = 0;
    
    dev->ohci_mmio[OHCI_HcRhDescriptorA].b[0] = 0x02;
    dev->ohci_mmio[OHCI_HcRhDescriptorA].b[1] = 0x02;
    dev->ohci_mmio[OHCI_HcRhDescriptorB].l = 0x00;
    timer_disable(&dev->ohci_frame_timer);
    for (int i = 0; i < 2; i++) {
        if (dev->ohci_devices[i]) {
            usb_device_t* usbdev = dev->ohci_devices[i];
            usb_detach_device(dev, i | (USB_BUS_OHCI << 16));
            usb_attach_device(dev, usbdev, USB_BUS_OHCI);
            dev->ohci_devices[i]->device_reset(dev->ohci_devices[i]->priv);
        }
    }
}

#define OHCI_PORT_CCS         (1 << 0)
#define OHCI_PORT_PES         (1 << 1)
#define OHCI_PORT_PSS         (1 << 2)
#define OHCI_PORT_POCI        (1 << 3)
#define OHCI_PORT_PRS         (1 << 4)
#define OHCI_PORT_PPS         (1 << 8)
#define OHCI_PORT_LSDA        (1 << 9)
#define OHCI_PORT_CSC         (1 << 16)
#define OHCI_PORT_PESC        (1 << 17)
#define OHCI_PORT_PSSC        (1 << 18)
#define OHCI_PORT_OCIC        (1 << 19)
#define OHCI_PORT_PRSC        (1 << 20)
#define OHCI_PORT_WTC         (OHCI_PORT_CSC | OHCI_PORT_PESC | \
                               OHCI_PORT_PSSC | OHCI_PORT_OCIC | \
                               OHCI_PORT_PRSC)

static void ohci_port_power(usb_t *dev, int i, int p)
{
    if (p) {
        dev->ohci_rhports[i].l |= OHCI_PORT_PPS;
    } else {
        dev->ohci_rhports[i].l &= ~(OHCI_PORT_PPS | OHCI_PORT_CCS |
                                  OHCI_PORT_PSS | OHCI_PORT_PRS);
    }
}

/*
 * Sets a flag in a port status reg but only set it if the port is connected.
 * If not set ConnectStatusChange flag. If flag is enabled return 1.
 */
static int ohci_port_set_if_connected(usb_t *dev, int i, uint32_t val)
{
    int ret = 1;

    /* writing a 0 has no effect */
    if (val == 0) {
        return 0;
    }
    /* If CurrentConnectStatus is cleared we set ConnectStatusChange */
    if (!(dev->ohci_rhports[i].l & OHCI_PORT_CCS)) {
        dev->ohci_rhports[i].l |= OHCI_PORT_CSC;
        #if 0
        if (ohci->rhstatus & OHCI_RHS_DRWE) {
            /* CSC is a wakeup event */
            if (ohci_resume(ohci)) {
                ohci_set_interrupt(ohci, OHCI_INTR_RD);
            }
        }
        #endif
        return 0;
    }

    if (dev->ohci_rhports[i].l & val) {
        ret = 0;
    }
    /* set the bit */
    dev->ohci_rhports[i].l |= val;

    return ret;
}


static void ohci_port_set_status(usb_t *dev, int portnum, uint32_t val)
{
    uint32_t old_state;
    ohci_mmio_t *port;

    port = &dev->ohci_rhports[portnum];
    old_state = port->l;

    /* Write to clear CSC, PESC, PSSC, OCIC, PRSC */
    if (val & OHCI_PORT_WTC) {
        port->l &= ~(val & OHCI_PORT_WTC);
    }
    if (val & OHCI_PORT_CCS) {
        port->l &= ~OHCI_PORT_PES;
    }
    ohci_port_set_if_connected(dev, portnum, val & OHCI_PORT_PES);

    if (ohci_port_set_if_connected(dev, portnum, val & OHCI_PORT_PSS)) {
    }

    if (ohci_port_set_if_connected(dev, portnum, val & OHCI_PORT_PRS)) {
        dev->ohci_devices[portnum]->device_reset(dev->ohci_devices[portnum]->priv);
        port->l &= ~OHCI_PORT_PRS;
        /* ??? Should this also set OHCI_PORT_PESC. */
        port->l |= OHCI_PORT_PES | OHCI_PORT_PRSC;
    }

    /* Invert order here to ensure in ambiguous case, device is powered up. */
    if (val & OHCI_PORT_LSDA) {
        ohci_port_power(dev, portnum, 0);
    }
    if (val & OHCI_PORT_PPS) {
        ohci_port_power(dev, portnum, 1);
    }
    if (old_state != port->l) {
        ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RHSC);
    }
}

#define OHCI_RHS_LPS          (1U << 0)
#define OHCI_RHS_OCI          (1U << 1)
#define OHCI_RHS_DRWE         (1U << 15)
#define OHCI_RHS_LPSC         (1U << 16)
#define OHCI_RHS_OCIC         (1U << 17)
#define OHCI_RHS_CRWE         (1U << 31)

/* Set root hub status */
static void ohci_set_hub_status(usb_t *dev, uint32_t val)
{
    uint32_t old_state;

    old_state = dev->ohci_mmio[OHCI_HcRhStatus].l;

    /* write 1 to clear OCIC */
    if (val & OHCI_RHS_OCIC) {
        dev->ohci_mmio[OHCI_HcRhStatus].l &= ~OHCI_RHS_OCIC;
    }
    if (val & OHCI_RHS_LPS) {
        int i;

        for (i = 0; i < 2; i++) {
            ohci_port_power(dev, i, 0);
        }
    }

    if (val & OHCI_RHS_LPSC) {
        int i;

        for (i = 0; i < 2; i++) {
            ohci_port_power(dev, i, 1);
        }
    }

    if (val & OHCI_RHS_DRWE) {
        dev->ohci_mmio[OHCI_HcRhStatus].l |= OHCI_RHS_DRWE;
    }
    if (val & OHCI_RHS_CRWE) {
        dev->ohci_mmio[OHCI_HcRhStatus].l &= ~OHCI_RHS_DRWE;
    }
    if (old_state != dev->ohci_mmio[OHCI_HcRhStatus].l) {
        ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RHSC);
    }
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

    //pclog("dev->ohci_mmio[0x%X].b[%d] = 0x%X\n", addr >> 2, addr & 3, val);

    switch (addr) {
        case OHCI_aHcControl:
            old = dev->ohci_mmio[OHCI_HcControl].b[0];
#ifdef ENABLE_USB_LOG
            usb_log("OHCI: OHCI state 0x%X\n", (val & 0xc0));
#endif
            if ((old & 0xc0) == (val & 0xc0))
                return;
            if ((val & 0xc0) == 0x00) {
                /* UsbReset */
                ohci_rhport_reset(dev);
            } else if ((val & 0xc0) == 0x80) {
                dev->ohci_mmio[OHCI_HcFmRemaining].w[0] = 0;
                timer_on_auto(&dev->ohci_frame_timer, 1000.);
            }
            if ((val & 0xc0) == 0xc0) {
                //dev->ohci_mmio[OHCI_HcInterruptStatus].l &= ~OHCI_HcInterruptEnable_SF;
                //timer_disable(&dev->ohci_frame_timer);
            }
            break;
        case OHCI_aHcCommandStatus:
            /* bit OwnershipChangeRequest triggers an ownership change (SMM <-> OS) */
            if (val & 0x08) {
                dev->ohci_mmio[OHCI_HcInterruptStatus].b[3] = 0x40;
                if ((dev->ohci_mmio[OHCI_HcInterruptEnable].b[3] & 0x40) == 0x40) {
                    smi_raise();
                }
            }

            /* bit HostControllerReset must be cleared for the controller to be seen as initialized */
            if (val & 0x01) {
                ohci_soft_reset(dev);

                val &= ~0x01;
            }
            break;
        case OHCI_aHcHCCA:
            return;
        case OHCI_aHcInterruptEnable:
            dev->ohci_mmio[addr >> 2].b[addr & 3] |= (val & 0x7f);
            ohci_update_irq(dev);
            return;
        case OHCI_aHcInterruptEnable + 1:
        case OHCI_aHcInterruptEnable + 2:
            return;
        case OHCI_aHcInterruptEnable + 3:
            dev->ohci_mmio[addr >> 2].b[addr & 3] |= (val & 0xc0);
            ohci_update_irq(dev);
            return;
        case OHCI_aHcInterruptDisable:
            dev->ohci_mmio[OHCI_HcInterruptEnable].b[addr & 3] &= ~(val & 0x7f);
            ohci_update_irq(dev);
            return;
        case OHCI_aHcInterruptDisable + 1:
        case OHCI_aHcInterruptDisable + 2:
            return;
        case OHCI_aHcInterruptDisable + 3:
            dev->ohci_mmio[OHCI_HcInterruptEnable].b[addr & 3] &= ~(val & 0xc0);
            ohci_update_irq(dev);
            return;
        case OHCI_aHcInterruptStatus:
            dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~(val & 0x7f);
            ohci_update_irq(dev);
            return;
        case OHCI_aHcInterruptStatus + 1:
        case OHCI_aHcInterruptStatus + 2:
            return;
        case OHCI_aHcInterruptStatus + 3:
            dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~(val & 0x40);
            ohci_update_irq(dev);
            return;
        case OHCI_aHcFmRemaining + 3:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0x80);
            return;
        case OHCI_aHcFmRemaining + 1:
            return;
        case OHCI_aHcPeriodicStart + 1:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0x3f);
            return;
        case OHCI_aHcLSThreshold + 1:
            dev->ohci_mmio[addr >> 2].b[addr & 3] = (val & 0x0f);
            return;
        case OHCI_aHcFmRemaining + 2:
        case OHCI_aHcFmNumber:
        case OHCI_aHcFmNumber + 1:
        case OHCI_aHcFmNumber + 2:
        case OHCI_aHcFmNumber + 3:
        case OHCI_aHcPeriodicStart + 2:
        case OHCI_aHcPeriodicStart + 3:
        case OHCI_aHcLSThreshold + 2:
        case OHCI_aHcLSThreshold + 3:
            return;
        case OHCI_aHcRhDescriptorB ... OHCI_aHcRhDescriptorB + 3:
            return;
#if 0
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
#endif
#if 0
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
                    dev->ohci_mmio[addr >> 2].b[(addr + 2) & 3] |= 0x10;
                    if (!(dev->ohci_mmio[addr >> 2].b[addr & 3] & 0x02)) {
                        dev->ohci_mmio[addr >> 2].b[(addr + 2) & 3] |= 0x02;
                    }
                    dev->ohci_mmio[addr >> 2].b[addr & 3] |= 0x02;
                    //timer_on_auto(&dev->ohci_port_reset_timer[(addr - OHCI_aHcRhPortStatus1) / 4], 10000.);
                    if (dev->ohci_devices[(addr - OHCI_aHcRhPortStatus1) >> 2])
                        dev->ohci_devices[(addr - OHCI_aHcRhPortStatus1) >> 2]->device_reset(dev->ohci_devices[(addr - OHCI_aHcRhPortStatus1) >> 2]->priv);
                } else
                    dev->ohci_mmio[(addr + 2) >> 2].b[(addr + 2) & 3] |= 0x01;
            }
            if (val & 0x08)
                dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~0x04;
            if (val & 0x04) {
                if (old & 0x01)
                    dev->ohci_mmio[addr >> 2].b[addr & 3] |= 0x04;
                else
                    dev->ohci_mmio[(addr + 2) >> 2].b[(addr + 2) & 3] |= 0x01;
            }
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
            if (old != dev->ohci_mmio[addr >> 2].b[addr & 3]) {
                ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RHSC);
            }
            return;
        case OHCI_aHcRhPortStatus1 + 1:
            if ((val & 0x02)) {
                dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~0x01;
            }
            if ((val & 0x01)) {
                dev->ohci_mmio[addr >> 2].b[addr & 3] |= 0x01;
            }
            if (!(dev->ohci_mmio[addr >> 2].b[addr & 3] & 0x01)) {
                dev->ohci_mmio[OHCI_HcRhPortStatus1].b[0] &= ~0x17;
                dev->ohci_mmio[OHCI_HcRhPortStatus1].b[2] &= ~0x17;
                if (!(dev->ohci_mmio[OHCI_HcRhPortStatus1].b[0] & 0x1) && dev->ohci_devices[0]) {
                    dev->ohci_mmio[OHCI_HcRhPortStatus1].b[0] |= 0x1;
                    dev->ohci_mmio[OHCI_HcRhPortStatus1].b[2] |= 0x1;
                    ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RHSC);
                    return;
                }
            }
            return;
        case OHCI_aHcRhPortStatus2 + 1:
            if ((val & 0x02)) {
                dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~0x01;
                dev->ohci_mmio[OHCI_HcRhPortStatus2].b[0] &= ~0x17;
                dev->ohci_mmio[OHCI_HcRhPortStatus2].b[2] &= ~0x17;
            }
            if ((val & 0x01)) {
                dev->ohci_mmio[addr >> 2].b[addr & 3] |= 0x01;
                if (!(dev->ohci_mmio[OHCI_HcRhPortStatus2].b[0] & 0x1) && dev->ohci_devices[1]) {
                    dev->ohci_mmio[OHCI_HcRhPortStatus2].b[0] |= 0x1;
                    dev->ohci_mmio[OHCI_HcRhPortStatus2].b[2] |= 0x1;
                    ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RHSC);
                    return;
                }
            }
            if (old != dev->ohci_mmio[addr >> 2].b[addr & 3]) {
                ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RHSC);
            }
            return;
        case OHCI_aHcRhPortStatus1 + 2:
        case OHCI_aHcRhPortStatus2 + 2:
            dev->ohci_mmio[addr >> 2].b[addr & 3] &= ~(val & 0x1f);
            if (old != dev->ohci_mmio[addr >> 2].b[addr & 3]) {
                ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RHSC);
            }
            return;
        case OHCI_aHcRhPortStatus1 + 3:
        case OHCI_aHcRhPortStatus2 + 3:
            return;
#endif
        case OHCI_aHcRhStatus ... OHCI_aHcRhStatus + 3:
            ohci_set_hub_status(dev, val << (8 * (addr & 3)));
            return;
        case OHCI_aHcRhPortStatus1 ... OHCI_aHcRhPortStatus1 + 3:
            ohci_port_set_status(dev, 0, val << (8 * (addr & 3)));
            return;
        case OHCI_aHcRhPortStatus2 ... OHCI_aHcRhPortStatus2 + 3:
            ohci_port_set_status(dev, 1, val << (8 * (addr & 3)));
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

uint16_t
usb_attach_device(usb_t *dev, usb_device_t* device, uint8_t bus_type)
{
    if (!usb_device_inst)
        return (uint16_t)-1;
    switch (bus_type) {
        case USB_BUS_OHCI:
            {
                for (int i = 0; i < 2; i++) {
                    if (!dev->ohci_devices[i]) {
                        uint32_t old = dev->ohci_mmio[OHCI_HcRhPortStatus1 + (i)].l;
                        dev->ohci_devices[i] = device;
                        dev->ohci_mmio[OHCI_HcRhPortStatus1 + (i)].b[0] |= 0x1;
                        dev->ohci_mmio[OHCI_HcRhPortStatus1 + (i)].b[2] |= 0x1;
                        if ((dev->ohci_mmio[OHCI_HcControl].b[0] & 0xc0) == 0xc0) {
                            ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RD);
                        }
                        if (old != dev->ohci_mmio[OHCI_HcRhPortStatus1 + (i)].l) {
                            ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RHSC);
                        }
                        return i | (bus_type << 8);
                    }
                }
            }
            break;
    }
    return (uint16_t)-1;
}

void
usb_detach_device(usb_t *dev, uint16_t port)
{
    switch (port >> 8) {
        case USB_BUS_OHCI:
            {
                port &= 0xFF;
                if (port > 2)
                    return;
                if (dev->ohci_devices[port]) {
                    uint32_t old = dev->ohci_mmio[OHCI_HcRhPortStatus1 + (port)].l;
                    dev->ohci_devices[port] = NULL;
                    if (dev->ohci_mmio[OHCI_HcRhPortStatus1 + (port)].b[0] & 0x1) {
                        dev->ohci_mmio[OHCI_HcRhPortStatus1 + (port)].b[0] &= ~0x1;
                        dev->ohci_mmio[OHCI_HcRhPortStatus1 + (port)].b[2] |= 0x1;
                    }
                    if (dev->ohci_mmio[OHCI_HcRhPortStatus1 + (port)].b[0] & 0x2) {
                        dev->ohci_mmio[OHCI_HcRhPortStatus1 + (port)].b[0] &= ~0x2;
                        dev->ohci_mmio[OHCI_HcRhPortStatus1 + (port)].b[2] |= 0x2;
                    }
                    if (old != dev->ohci_mmio[OHCI_HcRhPortStatus1 + (port)].l)
                        ohci_set_interrupt(dev, OHCI_HcInterruptEnable_RHSC);
                    return;
                }

            }
            break;
    }
    return;
}

uint8_t
usb_parse_control_endpoint(usb_device_t* usb_device, uint8_t* data, uint32_t *len, uint8_t pid_token, uint8_t endpoint, uint8_t underrun_not_allowed)
{
    usb_desc_setup_t* setup_packet = (usb_desc_setup_t*)data;
    uint8_t ret = USB_ERROR_STALL;
    
    if (endpoint != 0)
        return USB_ERROR_STALL;

    if (!usb_device->fifo.data) {
        fifo8_create(&usb_device->fifo, 4096);
    }
    //pclog("Control endpoint of device 0x%08X: Transfer (PID = 0x%X, len = %d)\n", usb_device, pid_token, *len);
    switch (pid_token) {
        case USB_PID_SETUP:
        {
            if (*len != 8) {
                return *len > 8 ? USB_ERROR_OVERRUN : USB_ERROR_UNDERRUN;
            }
            usb_device->setup_desc = *setup_packet;
            fifo8_reset(&usb_device->fifo);
            //pclog("bmRequestType = 0x%X, \n", usb_device->setup_desc.bmRequestType);
            //pclog("bRequest = 0x%X, \n", usb_device->setup_desc.bRequest);
            //pclog("wIndex = 0x%X, \n", usb_device->setup_desc.wIndex);
            //pclog("wLength = 0x%X, \n", usb_device->setup_desc.wLength);
            //pclog("wValue = 0x%X\n", usb_device->setup_desc.wValue);
            switch (setup_packet->bmRequestType & 0x1f) {
                case USB_SETUP_TYPE_INTERFACE:
                {
                    if (setup_packet->bmRequestType & 0x80) {
                        switch (setup_packet->bRequest) {
                            case USB_SETUP_GET_STATUS: {
                                ret = 0;
                                fifo8_push(&usb_device->fifo, 0x00);
                                fifo8_push(&usb_device->fifo, 0x00);
                                break;
                            }
                            case USB_SETUP_GET_INTERFACE: {
                                ret = 0;
                                fifo8_push(&usb_device->fifo, usb_device->interface_altsetting[setup_packet->wIndex]);
                                break;
                            }
                        }
                    } else {
                        switch (setup_packet->bRequest) {
                            case USB_SETUP_SET_FEATURE:
                            case USB_SETUP_CLEAR_FEATURE: {
                                ret = 0;
                                break;
                            }
                            case USB_SETUP_SET_INTERFACE: {
                                ret = 0;
                                usb_device->interface_altsetting[setup_packet->wIndex] = setup_packet->wValue;
                                break;
                            }
                        }
                    }
                    break;
                }
                case USB_SETUP_TYPE_ENDPOINT:
                    /* Consider it all handled. */
                    if (setup_packet->bRequest == USB_SETUP_GET_STATUS && (setup_packet->bmRequestType & 0x80)) {
                        fifo8_push(&usb_device->fifo, 0x00);
                        fifo8_push(&usb_device->fifo, 0x00);
                    }
                    return 0;
                case USB_SETUP_TYPE_DEVICE:
                    {
                        if (setup_packet->bmRequestType & 0x80) {
                            switch (setup_packet->bRequest) {
                                case USB_SETUP_GET_STATUS: {
                                    fifo8_push_all(&usb_device->fifo, (uint8_t*)&usb_device->status_bits, sizeof(uint16_t));
                                    ret = 0;
                                    break;
                                }
                                case USB_SETUP_GET_CONFIGURATION: {
                                    fifo8_push(&usb_device->fifo, usb_device->current_configuration);
                                    ret = 0;
                                    break;
                                }
                                case USB_SETUP_GET_DESCRIPTOR: {
                                    switch (setup_packet->wValue >> 8) {
                                        case 0x01: /* Device descriptor */
                                        {
                                            fifo8_push_all(&usb_device->fifo, (uint8_t*)&usb_device->device_desc, sizeof(usb_device->device_desc));
                                            ret = 0;
                                            break;
                                        }
                                        case 0x02: /* Configuration descriptor (with all associated descriptors) */
                                        {
                                            int i = 0;
                                            ret = 0;
                                            fifo8_push_all(&usb_device->fifo, (uint8_t*)&usb_device->conf_desc_items.conf_desc, usb_device->conf_desc_items.conf_desc.base.bLength);
                                            for (i = 0; i < 16; i++) {
                                                if (usb_device->conf_desc_items.other_descs[i] == NULL)
                                                    break;

                                                if (usb_device->conf_desc_items.other_descs[i]->bDescriptorType == 0xFF) {
                                                    fifo8_push_all(&usb_device->fifo, ((usb_desc_ptr_t*)usb_device->conf_desc_items.other_descs[i])->ptr, usb_device->conf_desc_items.other_descs[i]->bLength);
                                                }
                                                else
                                                    fifo8_push_all(&usb_device->fifo, (uint8_t*)usb_device->conf_desc_items.other_descs[i], usb_device->conf_desc_items.other_descs[i]->bLength);
                                            }
                                            break;
                                        }
                                        case 0x03: /* String descriptor */
                                        {
                                            ret = 0;
                                            if (!usb_device->string_desc[setup_packet->wValue & 0xff]) {
                                                return USB_ERROR_STALL;
                                            }
                                            fifo8_push_all(&usb_device->fifo, (uint8_t*)usb_device->string_desc[setup_packet->wValue & 0xff], usb_device->string_desc[setup_packet->wValue & 0xff]->base.bLength);
                                            break;
                                        }
                                        default:
                                            return USB_ERROR_STALL;
                                    }
                                    break;
                                }
                            }
                        } else {
                            switch (setup_packet->bRequest) {
                                case USB_SETUP_SET_FEATURE: {
                                    usb_device->status_bits |= (setup_packet->wValue & 0x1);
                                    ret = 0;
                                    break;
                                }
                                case USB_SETUP_CLEAR_FEATURE: {
                                    usb_device->status_bits &= ~(setup_packet->wValue & 0x1);
                                    ret = 0;
                                    break;
                                }
                                case USB_SETUP_SET_ADDRESS: {
                                    usb_device->address = setup_packet->wValue & 0xFF;
                                    ret = 0;
                                    break;
                                }
                                case USB_SETUP_SET_CONFIGURATION: {
                                    usb_device->current_configuration = setup_packet->wValue & 0xFF;
                                    ret = 0;
                                    break;
                                }
                            }
                        }
                        break;
                    }
            }
            break;
        }
        case USB_PID_IN: {
            const uint8_t* buf = NULL;
            uint32_t used = 0;
            if (!(usb_device->setup_desc.bmRequestType & 0x80)) {
                if (usb_device->setup_desc.wLength && fifo8_num_used(&usb_device->fifo) >= usb_device->setup_desc.wLength) {
                    uint32_t len = 8;
                    return usb_device->device_process(usb_device->priv, (uint8_t*)&usb_device->setup_desc, &len, USB_PID_SETUP, 0, underrun_not_allowed);
                }
                else if (*len == 0)
                    return USB_ERROR_NO_ERROR;
                else
                    return USB_ERROR_STALL;
            }
            if (fifo8_num_used(&usb_device->fifo) == 0 && *len != 0)
                return USB_ERROR_STALL;
            if (fifo8_num_used(&usb_device->fifo) == 0 && *len == 0)
                return USB_ERROR_NO_ERROR;
            buf = fifo8_pop_buf(&usb_device->fifo, *len, len);
            memcpy(data, buf, *len);
            ret = 0;
            break;
        }
        case USB_PID_OUT: {
            if (!(usb_device->setup_desc.bmRequestType & 0x80)) {
                if (*len)
                    fifo8_push_all(&usb_device->fifo, data, *len);
                return 0;
            }
            if (usb_device->setup_desc.bmRequestType & 0x80)
                return USB_ERROR_STALL;
            return 0;
        }
    }
    return ret;
}

static void
usb_reset(void *priv)
{
    usb_t *dev = (usb_t *) priv;

    memset(dev->uhci_io, 0x00, sizeof(dev->uhci_io));
    dev->uhci_io[0x0c] = 0x40;
    dev->uhci_io[0x10] = dev->uhci_io[0x12] = 0x80;

    ohci_soft_reset(dev);
    dev->ohci_mmio[OHCI_HcControl].l = 0x00;
    ohci_rhport_reset(dev);

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

    usb_device_inst = NULL;
    free(dev);
}

static void *
usb_init_ext(const device_t *info, void *params)
{
    usb_t *dev;

    dev = (usb_t *) calloc(1, sizeof(usb_t));
    if (dev == NULL)
        return (NULL);

    dev->usb_params = (usb_params_t *) params;

    mem_mapping_add(&dev->ohci_mmio_mapping, 0, 0x1000,
                    ohci_mmio_read, ohci_mmio_readw, ohci_mmio_readl,
                    ohci_mmio_write, ohci_mmio_writew, ohci_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    mem_mapping_disable(&dev->ohci_mmio_mapping);

    timer_add(&dev->ohci_frame_timer, ohci_frame_boundary, dev, 0);
    timer_add(&dev->ohci_port_reset_timer[0], ohci_port_reset_callback, dev, 0);
    timer_add(&dev->ohci_port_reset_timer[1], ohci_port_reset_callback_2, dev, 0);

    dev->ohci_rhports = &dev->ohci_mmio[OHCI_HcRhPortStatus1];

    usb_reset(dev);

    usb_device_inst = dev;

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
