/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the UMC 8886xx PCI to ISA Bridge .
 *
 *		Note: This chipset has no datasheet, everything were done via
 *		reverse engineering the BIOS of various machines using it.
 *
 * Authors:	Tiseno100,
 *
 *		Copyright 2021 Tiseno100.
 */

/* UMC 8886 Configuration Registers

   TODO:
   - More Appropriate Bitmasking(If it's even possible)

   Warning: Register documentation may be inaccurate!

   UMC 8886xx:
   (F: Has No Internal IDE / AF or BF: Has Internal IDE)

   Function 0 Register 43:
   Bits 7-4 PCI IRQ for INTB
   Bits 3-0 PCI IRQ for INTA

   Function 0 Register 44:
   Bits 7-4 PCI IRQ for INTD
   Bits 3-0 PCI IRQ for INTC

   Function 0 Register 46:
   Bit 7: Replace SMI request for non-SMM CPU's (1: IRQ15/0: IRQ10)

   Function 0 Register 51:
   Bit 2: VGA Power Down (0: Standard/1: VESA DPMS)

   Function 0 Register 56:
   Bit 1-0 ISA Bus Speed
       0 0 PCICLK/3
       0 1 PCICLK/4
       1 0 PCICLK/2

   Function 0 Register A4:
   Bit 0: Host to PCI Clock (1: 1 by 1/0: 1 by half)

   Function 1 Register 4:
   Bit 0: Enable Internal IDE
*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>

#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/pci.h>

#include <86box/chipset.h>


#ifdef ENABLE_UMC_8886_LOG
int umc_8886_do_log = ENABLE_UMC_8886_LOG;
static void
umc_8886_log(const char *fmt, ...)
{
    va_list ap;

    if (umc_8886_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define umc_8886_log(fmt, ...)
#endif


/* PCI IRQ Flags */
#define INTA (PCI_INTA + (2 * !(addr & 1)))
#define INTB (PCI_INTB + (2 * !(addr & 1)))
#define IRQRECALCA (((val & 0xf0) != 0) ? ((val & 0xf0) >> 4) : PCI_IRQ_DISABLED)
#define IRQRECALCB (((val & 0x0f) != 0) ? (val & 0x0f) : PCI_IRQ_DISABLED)

/* Disable Internal IDE Flag needed for the BF Southbridge variant */
#define HAS_IDE dev->has_ide

/* Southbridge Revision */
#define SB_ID dev->sb_id


typedef struct umc_8886_t
{
    uint8_t pci_conf_sb[2][256]; /* PCI Registers */
    uint16_t sb_id;              /* Southbridge Revision */
    int has_ide;                 /* Check if Southbridge Revision is AF or F */
} umc_8886_t;


static void
umc_8886_ide_handler(int status)
{
    ide_pri_disable();
    ide_sec_disable();

    if (status) {
        ide_pri_enable();
        ide_sec_enable();
    }
}


static void
um8886_write(int func, int addr, uint8_t val, void *priv)
{
    umc_8886_t *dev = (umc_8886_t *)priv;
    umc_8886_log("UM8886: dev->regs[%02x] = %02x (%02x)\n", addr, val, func);

    /* We don't know the RW status of registers but Phoenix writes on some RO registers too*/
    if (addr > 3)  switch (func) {
	case 0: /* Southbridge */
		switch (addr) {
			case 0x43:
			case 0x44:
				dev->pci_conf_sb[func][addr] = val;
				pci_set_irq_routing(INTA, IRQRECALCA);
				pci_set_irq_routing(INTB, IRQRECALCB);
				break;

			case 0x46:
				dev->pci_conf_sb[func][addr] = val & 0xaf;
				break;

			case 0x47:
				dev->pci_conf_sb[func][addr] = val & 0x4f;
				break;

			case 0x56:
				dev->pci_conf_sb[func][addr] = val;
				switch (val & 2) {
					case 0:
						cpu_set_isa_pci_div(3);
						break;
					case 1:
						cpu_set_isa_pci_div(4);
						break;
					case 2:
						cpu_set_isa_pci_div(2);
						break;
				}
				break;

			case 0x57:
				dev->pci_conf_sb[func][addr] = val & 0x38;
				break;

			case 0x71:
				dev->pci_conf_sb[func][addr] = val & 1;
				break;

			case 0x90:
				dev->pci_conf_sb[func][addr] = val & 2;
				break;

			case 0x92:
				dev->pci_conf_sb[func][addr] = val & 0x1f;
				break;

			case 0xa0:
				dev->pci_conf_sb[func][addr] = val & 0xfc;
				break;

			case 0xa4:
				dev->pci_conf_sb[func][addr] = val & 0x89;
				cpu_set_pci_speed(cpu_busspeed / ((val & 1) ? 1 : 2));
				break;

			default:
				dev->pci_conf_sb[func][addr] = val;
				break;
		}
		break;
	case 1:	/* IDE Controller */
		dev->pci_conf_sb[func][addr] = val;
		if ((addr == 4) && HAS_IDE)
			umc_8886_ide_handler(val & 1);
		break;
    }
}


static uint8_t
um8886_read(int func, int addr, void *priv)
{
    umc_8886_t *dev = (umc_8886_t *)priv;
    return dev->pci_conf_sb[func][addr];
}


static void
umc_8886_reset(void *priv)
{
    umc_8886_t *dev = (umc_8886_t *)priv;

    /* Defaults */
    dev->pci_conf_sb[0][0] = 0x60; /* UMC */
    dev->pci_conf_sb[0][1] = 0x10;

    dev->pci_conf_sb[0][2] = (SB_ID & 0xff); /* 8886xx */
    dev->pci_conf_sb[0][3] = ((SB_ID >> 8) & 0xff);

    dev->pci_conf_sb[0][8] = 1;

    dev->pci_conf_sb[0][0x09] = 0x00;
    dev->pci_conf_sb[0][0x0a] = 0x01;
    dev->pci_conf_sb[0][0x0b] = 0x06;

    for (int i = 1; i < 5; i++) /* Disable all IRQ interrupts */
	pci_set_irq_routing(i, PCI_IRQ_DISABLED);

    if (HAS_IDE) {
	dev->pci_conf_sb[1][4] = 1; /* Start with Internal IDE Enabled */
	umc_8886_ide_handler(1);
    }
}


static void
umc_8886_close(void *priv)
{
    umc_8886_t *dev = (umc_8886_t *)priv;

    free(dev);
}


static void *
umc_8886_init(const device_t *info)
{
    umc_8886_t *dev = (umc_8886_t *)malloc(sizeof(umc_8886_t));
    memset(dev, 0, sizeof(umc_8886_t));

    dev->has_ide = (info->local == 0x886a);
    pci_add_card(PCI_ADD_SOUTHBRIDGE, um8886_read, um8886_write, dev); /* Device 12: UMC 8886xx */

    /* Add IDE if UM8886AF variant */
    if (HAS_IDE)
        device_add(&ide_pci_2ch_device);

    /* Get the Southbridge Revision */
    SB_ID = info->local;

    umc_8886_reset(dev);

    return dev;
}


const device_t umc_8886f_device = {
    "UMC 8886F",
    DEVICE_PCI,
    0x8886,
    umc_8886_init,
    umc_8886_close,
    umc_8886_reset,
    { NULL },
    NULL,
    NULL,
    NULL
};

const device_t umc_8886af_device = {
    "UMC 8886AF",
    DEVICE_PCI,
    0x886a,
    umc_8886_init,
    umc_8886_close,
    umc_8886_reset,
    { NULL },
    NULL,
    NULL,
    NULL
};
