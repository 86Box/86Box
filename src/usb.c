/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Universal Serial Bus emulation (currently dummy UHCI and
 *		OHCI).
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020 Miran Grca.
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
#define usb_log(fmt, ...)
#endif


static uint8_t
uhci_reg_read(uint16_t addr, void *p)
{
    uint8_t ret = 0xff;

    switch (addr & 0x1f) {
	case 0x10: case 0x11: case 0x12: case 0x13:
		/* Port status */
                ret = 0x00;
		break;
    }

    return ret;
}


static void
uhci_reg_write(uint16_t addr, uint8_t val, void *p)
{
}


void
uhci_update_io_mapping(usb_t *dev, uint8_t base_l, uint8_t base_h, int enable)
{
    if (dev->uhci_enable && (dev->uhci_io_base != 0x0000))
	io_removehandler(dev->uhci_io_base, 0x20, uhci_reg_read, NULL, NULL, uhci_reg_write, NULL, NULL, dev);

    dev->uhci_io_base = base_l | (base_h << 8);
    dev->uhci_enable = enable;

    if (dev->uhci_enable && (dev->uhci_io_base != 0x0000))
	io_sethandler(dev->uhci_io_base, 0x20, uhci_reg_read, NULL, NULL, uhci_reg_write, NULL, NULL, dev);
}


static uint8_t
ohci_mmio_read(uint32_t addr, void *p)
{
    usb_t *dev = (usb_t *) p;
    uint8_t ret = 0x00;

    addr &= 0x00000fff;

    ret = dev->ohci_mmio[addr];

    return ret;
}


static void
ohci_mmio_write(uint32_t addr, uint8_t val, void *p)
{
    usb_t *dev = (usb_t *) p;

    addr &= 0x00000fff;

    switch (addr) {
    	case 0x08: /* HCCOMMANDSTATUS */
    		/* bit HostControllerReset must be cleared for the controller to be seen as initialized */
    		val &= ~0x01;

    		/* bit OwnershipChangeRequest triggers an ownership change (SMM <-> OS) */
    		if (val & 0x0f) {
    			dev->ohci_mmio[0x0f] = 0x40;
    			dev->ohci_mmio[0x05] &= ~(dev->ohci_mmio[0x05] & 0x01);
    		}
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
    dev->ohci_enable = enable;

    if (dev->ohci_enable && (dev->ohci_mem_base != 0x00000000))
    	mem_mapping_set_addr(&dev->ohci_mmio_mapping, dev->ohci_mem_base, 0x1000);
}


static void
usb_close(void *priv)
{
    usb_t *dev = (usb_t *) priv;

    free(dev);
}


static void *
usb_init(const device_t *info)
{
    usb_t *dev;

    dev = (usb_t *)malloc(sizeof(usb_t));
    if (dev == NULL) return(NULL);
    memset(dev, 0x00, sizeof(usb_t));

    memset(dev->ohci_mmio, 0x00, 4096);
    dev->ohci_mmio[0x00] = 0x10;
    dev->ohci_mmio[0x01] = 0x01;
    dev->ohci_mmio[0x48] = 0x02;

    mem_mapping_add(&dev->ohci_mmio_mapping, 0, 0,
		    ohci_mmio_read,  NULL, NULL,
		    ohci_mmio_write, NULL, NULL,
		    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->ohci_mmio_mapping);

    return dev;
}


const device_t usb_device =
{
    "Universal Serial Bus",
    DEVICE_PCI,
    0,
    usb_init, 
    usb_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
