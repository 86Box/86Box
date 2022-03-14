/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the VIA PIPC southbridges.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Melissa Goad, <mszoopers@protonmail.com>
 *		RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2020 Melissa Goad.
 *		Copyright 2020-2021 RichardG.
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
#include <86box/scsi_device.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/apm.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/acpi.h>
#include <86box/ddma.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/port_92.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/usb.h>
#include <86box/machine.h>
#include <86box/smbus.h>
#include <86box/chipset.h>
#include <86box/sio.h>
#include <86box/hwm.h>
#include <86box/gameport.h>
#include <86box/sound.h>
#include <86box/snd_ac97.h>
#include <86box/snd_sb.h>
#include <86box/nmi.h>

/* Most revision numbers (PCI-ISA bridge or otherwise) were lifted from PCI device
   listings on forums, as VIA's datasheets are not very helpful regarding those. */
#define VIA_PIPC_586A	0x05862500
#define VIA_PIPC_586B	0x05864700
#define VIA_PIPC_596A	0x05960900
#define VIA_PIPC_596B	0x05962300
#define VIA_PIPC_686A	0x06861400
#define VIA_PIPC_686B	0x06864000
#define VIA_PIPC_8231	0x82311000


enum {
    TRAP_DRQ = 0,
    TRAP_PIRQ,
    TRAP_PIDE_MAIN,
    TRAP_PIDE_SIDE,
    TRAP_SIDE_MAIN,
    TRAP_SIDE_SIDE,
    TRAP_FLP_MAIN,
    TRAP_FLP_SIDE,
    TRAP_COM1,
    TRAP_COM3,
    TRAP_COM2,
    TRAP_COM4,
    TRAP_LPT1,
    TRAP_LPT2,
    TRAP_VGA,
    TRAP_KBC,
    TRAP_AUD_MIDI_0,
    TRAP_AUD_MIDI_1,
    TRAP_AUD_MIDI_2,
    TRAP_AUD_MIDI_3,
    TRAP_AUD_SB_0,
    TRAP_AUD_SB_1,
    TRAP_AUD_SB_2,
    TRAP_AUD_SB_3,
    TRAP_AUD_GAME,
    TRAP_AUD_WSS_0,
    TRAP_AUD_WSS_1,
    TRAP_AUD_WSS_2,
    TRAP_AUD_WSS_3,
    TRAP_GR0,
    TRAP_GR1,
    TRAP_GR2,
    TRAP_GR3,
    TRAP_MAX
};

typedef struct {
    struct _pipc_ *dev;
    void	*trap;
    uint32_t	*sts_reg, *en_reg, mask;
} pipc_io_trap_t;

typedef struct _pipc_ {
    uint32_t	local;
    uint8_t	max_func, max_pcs;

    uint8_t	pci_isa_regs[256],
		ide_regs[256],
		usb_regs[2][256],
		power_regs[256],
		ac97_regs[2][256], fmnmi_regs[4];

    sff8038i_t	*bm[2];
    nvr_t	*nvr;
    int		nvr_enabled, slot;
    ddma_t	*ddma;
    smbus_piix4_t *smbus;
    usb_t	*usb[2];

    acpi_t	*acpi;
    pipc_io_trap_t io_traps[TRAP_MAX];

    void	*gameport, *ac97, *sio, *hwm;
    sb_t	*sb;
    uint16_t	midigame_base, sb_base, fmnmi_base;
} pipc_t;


#ifdef ENABLE_PIPC_LOG
int pipc_do_log = ENABLE_PIPC_LOG;


static void
pipc_log(const char *fmt, ...)
{
    va_list ap;

    if (pipc_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define pipc_log(fmt, ...)
#endif


static void	pipc_sgd_handlers(pipc_t *dev, uint8_t modem);
static void	pipc_codec_handlers(pipc_t *dev, uint8_t modem);
static void	pipc_sb_handlers(pipc_t *dev, uint8_t modem);
static uint8_t	pipc_read(int func, int addr, void *priv);
static void	pipc_write(int func, int addr, uint8_t val, void *priv);


static void
pipc_io_trap_pact(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv)
{
    pipc_io_trap_t *trap = (pipc_io_trap_t *) priv;

    if (*(trap->en_reg) & trap->mask) {
	*(trap->sts_reg) |= trap->mask;
	trap->dev->acpi->regs.glbsts |= 0x0001;
	if (trap->dev->acpi->regs.glben & 0x0001)
		acpi_raise_smi(trap->dev->acpi, 1);
    }
}


static void
pipc_io_trap_glb(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv)
{
    pipc_io_trap_t *trap = (pipc_io_trap_t *) priv;

    if (*(trap->en_reg) & trap->mask) {
	*(trap->sts_reg) |= trap->mask;
	if (trap->dev->local >= VIA_PIPC_686A) {
		if (write)
			trap->dev->acpi->regs.extsmi_val |= 0x1000;
		else
			trap->dev->acpi->regs.extsmi_val &= ~0x1000;
	}
	acpi_raise_smi(trap->dev->acpi, 1);
    }
}


static void
pipc_reset_hard(void *priv)
{
    int i;

    pipc_log("PIPC: reset_hard()\n");

    pipc_t *dev = (pipc_t *) priv;
    uint16_t old_base = (dev->ide_regs[0x20] & 0xf0) | (dev->ide_regs[0x21] << 8);

    sff_bus_master_reset(dev->bm[0], old_base);
    sff_bus_master_reset(dev->bm[1], old_base + 8);

    memset(dev->pci_isa_regs, 0, 256);
    memset(dev->ide_regs, 0, 256);
    memset(dev->usb_regs, 0, 512);
    memset(dev->power_regs, 0, 256);
    memset(dev->ac97_regs, 0, 512);

    /* PCI-ISA bridge registers. */
    dev->pci_isa_regs[0x00] = 0x06; dev->pci_isa_regs[0x01] = 0x11;
    dev->pci_isa_regs[0x02] = dev->local >> 16;
    dev->pci_isa_regs[0x03] = dev->local >> 24;
    dev->pci_isa_regs[0x04] = (dev->local <= VIA_PIPC_586B) ? 0x0f : 0x87;
    dev->pci_isa_regs[0x07] = 0x02;
    dev->pci_isa_regs[0x08] = dev->local >> 8;
    dev->pci_isa_regs[0x0a] = 0x01;
    dev->pci_isa_regs[0x0b] = 0x06;
    dev->pci_isa_regs[0x0e] = 0x80;

    dev->pci_isa_regs[0x48] = 0x01;
    dev->pci_isa_regs[0x4a] = 0x04;
    dev->pci_isa_regs[0x4f] = 0x03;

    dev->pci_isa_regs[0x50] = (dev->local >= VIA_PIPC_686A) ? 0x0e : 0x24; /* 686A/B default value does not line up with default bits */
    dev->pci_isa_regs[0x59] = 0x04;
    if (dev->local >= VIA_PIPC_686A)
	dev->pci_isa_regs[0x5a] = dev->pci_isa_regs[0x5f] = 0x04;

    dma_e = 0x00;
    for (i = 0; i < 8; i++) {
	dma[i].ab &= 0xffff000f;
	dma[i].ac &= 0xffff000f;
    }

    pic_set_shadow(0);

    dev->max_pcs = (dev->local >= VIA_PIPC_686A) ? 3 : 1;

    /* IDE registers. */
    dev->max_func++;
    dev->ide_regs[0x00] = 0x06; dev->ide_regs[0x01] = 0x11;
    dev->ide_regs[0x02] = 0x71; dev->ide_regs[0x03] = 0x05;
    dev->ide_regs[0x04] = 0x80;
    dev->ide_regs[0x06] = (dev->local == VIA_PIPC_686A) ? 0x90 : 0x80; dev->ide_regs[0x07] = 0x02;
    dev->ide_regs[0x08] = (dev->local == VIA_PIPC_596B) ? 0x10 : 0x06; /* only 596B has rev 0x10? */
    dev->ide_regs[0x09] = 0x85;
    dev->ide_regs[0x0a] = 0x01;
    dev->ide_regs[0x0b] = 0x01;

    dev->ide_regs[0x10] = 0xf1; dev->ide_regs[0x11] = 0x01;
    dev->ide_regs[0x14] = 0xf5; dev->ide_regs[0x15] = 0x03;
    dev->ide_regs[0x18] = 0x71; dev->ide_regs[0x19] = 0x01;
    dev->ide_regs[0x1c] = 0x75; dev->ide_regs[0x1d] = 0x03;
    dev->ide_regs[0x20] = 0x01; dev->ide_regs[0x21] = 0xcc;
    if (dev->local >= VIA_PIPC_686A)
	dev->ide_regs[0x34] = 0xc0;
    dev->ide_regs[0x3c] = 0x0e;

    if (dev->local <= VIA_PIPC_586B)
	dev->ide_regs[0x40] = 0x04;
    dev->ide_regs[0x41] = (dev->local == VIA_PIPC_686B) ? 0x06 : 0x02;
    dev->ide_regs[0x42] = 0x09;
    dev->ide_regs[0x43] = (dev->local >= VIA_PIPC_686A) ? 0x0a : 0x3a;
    dev->ide_regs[0x44] = 0x68;
    if (dev->local == VIA_PIPC_686B)
	dev->ide_regs[0x45] = 0x20;
    else if (dev->local >= VIA_PIPC_8231)
	dev->ide_regs[0x45] = 0x03;
    dev->ide_regs[0x46] = 0xc0;
    dev->ide_regs[0x48] = 0xa8; dev->ide_regs[0x49] = 0xa8;
    dev->ide_regs[0x4a] = 0xa8; dev->ide_regs[0x4b] = 0xa8;
    dev->ide_regs[0x4c] = 0xff;
    if (dev->local != VIA_PIPC_686B)
	dev->ide_regs[0x4e] = dev->ide_regs[0x4f] = 0xff;
    dev->ide_regs[0x50] = dev->ide_regs[0x51] = dev->ide_regs[0x52] = dev->ide_regs[0x53] = ((dev->local == VIA_PIPC_686A) || (dev->local == VIA_PIPC_686B)) ? 0x07 : 0x03;
    if (dev->local >= VIA_PIPC_596A)
	dev->ide_regs[0x54] = ((dev->local == VIA_PIPC_686A) || (dev->local == VIA_PIPC_686B)) ? 0x04 : 0x06;

    dev->ide_regs[0x61] = 0x02;
    dev->ide_regs[0x69] = 0x02;

    if (dev->local >= VIA_PIPC_686A) {
	dev->ide_regs[0xc0] = 0x01;
	dev->ide_regs[0xc2] = 0x02;
    }

    /* USB registers. */
    for (i = 0; i <= (dev->local >= VIA_PIPC_686A); i++) {
	dev->max_func++;
	dev->usb_regs[i][0x00] = 0x06; dev->usb_regs[i][0x01] = 0x11;
	dev->usb_regs[i][0x02] = 0x38; dev->usb_regs[i][0x03] = 0x30;
	dev->usb_regs[i][0x04] = 0x00; dev->usb_regs[i][0x05] = 0x00;
	dev->usb_regs[i][0x06] = 0x00; dev->usb_regs[i][0x07] = 0x02;
	switch (dev->local) {
		case VIA_PIPC_586A:
		case VIA_PIPC_586B:
		case VIA_PIPC_596A:
			dev->usb_regs[i][0x08] = 0x02;
			break;

		case VIA_PIPC_596B:
			dev->usb_regs[i][0x08] = 0x08;
			break;

		case VIA_PIPC_686A:
			dev->usb_regs[i][0x08] = 0x06;
			break;

		case VIA_PIPC_686B:
			dev->usb_regs[i][0x08] = 0x1a;
			break;

		case VIA_PIPC_8231:
			dev->usb_regs[i][0x08] = 0x1e;
			break;
	}

	dev->usb_regs[i][0x0a] = 0x03;
	dev->usb_regs[i][0x0b] = 0x0c;
	dev->usb_regs[i][0x0d] = 0x16;
	dev->usb_regs[i][0x20] = 0x01;
	dev->usb_regs[i][0x21] = 0x03;
	if (dev->local == VIA_PIPC_686B)
		dev->usb_regs[i][0x34] = 0x80;
	dev->usb_regs[i][0x3d] = 0x04;

	dev->usb_regs[i][0x60] = 0x10;
	if (dev->local >= VIA_PIPC_686A) {
		dev->usb_regs[i][0x80] = 0x01;
		dev->usb_regs[i][0x82] = 0x02;
	}
	dev->usb_regs[i][0xc1] = 0x20;
    }

    /* Power management registers. */
    if (dev->acpi) {
	dev->max_func++;
	dev->power_regs[0x00] = 0x06; dev->power_regs[0x01] = 0x11;
	if (dev->local >= VIA_PIPC_8231) {
		/* The VT8231 preliminary datasheet lists *two* inaccurate
		   device IDs (3068 and 3057). Real dumps have 8235. */
		dev->power_regs[0x02] = 0x35; dev->power_regs[0x03] = 0x82;
	} else {
		if (dev->local <= VIA_PIPC_586B)
			dev->power_regs[0x02] = 0x40;
		else if (dev->local <= VIA_PIPC_596B)
			dev->power_regs[0x02] = 0x50;
		else
			dev->power_regs[0x02] = 0x57;
		dev->power_regs[0x03] = 0x30;
	}
	dev->power_regs[0x04] = 0x00; dev->power_regs[0x05] = 0x00;
	dev->power_regs[0x06] = (dev->local == VIA_PIPC_686B) ? 0x90 : 0x80; dev->power_regs[0x07] = 0x02;
	switch (dev->local) {
		case VIA_PIPC_586B:
		case VIA_PIPC_686A:
		case VIA_PIPC_8231:
			dev->power_regs[0x08] = 0x10;
			break;

		case VIA_PIPC_596A:
			dev->power_regs[0x08] = 0x20;
			break;

		case VIA_PIPC_596B:
			dev->power_regs[0x08] = 0x30;
			break;

		case VIA_PIPC_686B:
			dev->power_regs[0x08] = 0x40;
			break;
	}
	if (dev->local == VIA_PIPC_686B)
		dev->power_regs[0x34] = 0x68;
	dev->power_regs[0x40] = 0x20;

	dev->power_regs[0x42] = 0x50;
	dev->power_regs[0x48] = 0x01;

	if (dev->local == VIA_PIPC_686B) {
		dev->power_regs[0x68] = 0x01;
		dev->power_regs[0x6a] = 0x02;
	}

	if (dev->local >= VIA_PIPC_686A)
		dev->power_regs[0x70] = 0x01;

	if (dev->local == VIA_PIPC_596A)
		dev->power_regs[0x80] = 0x01;
	else if (dev->local >= VIA_PIPC_596B)
		dev->power_regs[0x90] = 0x01;

	/* Set up PCS I/O traps. */
	pipc_io_trap_t *trap;
	for (i = 0; i <= dev->max_pcs; i++) {
		trap = &dev->io_traps[TRAP_GR0 + i];
		trap->dev = dev;
		trap->trap = io_trap_add(pipc_io_trap_glb, trap);
		if (i & 2) {
			trap->sts_reg = (uint32_t *) &dev->acpi->regs.extiotrapsts;
			trap->en_reg = (uint32_t *) &dev->acpi->regs.extiotrapen;
			trap->mask = 0x01 << (i & 1);
		} else {
			trap->sts_reg = &dev->acpi->regs.glbsts;
			trap->en_reg = &dev->acpi->regs.glben;
			trap->mask = 0x4000 << i;
		}
	}
    }

    /* AC97/MC97 registers. */
    if (dev->local >= VIA_PIPC_686A) {
	for (i = 0; i <= 1; i++) {
		dev->max_func++;
		dev->ac97_regs[i][0x00] = 0x06; dev->ac97_regs[i][0x01] = 0x11;
		dev->ac97_regs[i][0x02] = 0x58 + (0x10 * i); dev->ac97_regs[i][0x03] = 0x30;
		dev->ac97_regs[i][0x06] = 0x10 * (1 - i); dev->ac97_regs[i][0x07] = 0x02;
		switch (dev->local) {
			case VIA_PIPC_686A:
				dev->ac97_regs[i][0x08] = (i == 0) ? 0x12 : 0x01;
				break;

			case VIA_PIPC_686B:
				dev->ac97_regs[i][0x08] = (i == 0) ? 0x50 : 0x30;
				break;

			case VIA_PIPC_8231:
				dev->ac97_regs[i][0x08] = (i == 0) ? 0x40 : 0x20;
				break;
		}

		if (i == 0) {
			dev->ac97_regs[i][0x0a] = 0x01;
			dev->ac97_regs[i][0x0b] = 0x04;
		} else {
			dev->ac97_regs[i][0x0a] = 0x80;
			dev->ac97_regs[i][0x0b] = 0x07;
		}

		dev->ac97_regs[i][0x10] = 0x01;
		if (i == 0) {
			dev->ac97_regs[i][0x14] = 0x01;
			dev->ac97_regs[i][0x18] = 0x01;
		}
		dev->ac97_regs[i][0x1c] = 0x01;

		dev->ac97_regs[i][0x3d] = 0x03;

		if (i == 0)
			dev->ac97_regs[i][0x40] = 0x01;

		dev->ac97_regs[i][0x43] = 0x1c;
		dev->ac97_regs[i][0x48] = 0x01;
		dev->ac97_regs[i][0x4b] = 0x02;

		pipc_sgd_handlers(dev, i);
		pipc_codec_handlers(dev, i);
		pipc_sb_handlers(dev, i);
	}
    }

    if (dev->gameport)
	gameport_remap(dev->gameport, 0x200);

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    if (dev->local <= VIA_PIPC_586B) {
	pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
	pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
	if (dev->local == VIA_PIPC_586B)
		pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);
    }

    ide_pri_disable();
    ide_sec_disable();

    nvr_via_wp_set(0x00, 0x32, dev->nvr);
    nvr_via_wp_set(0x00, 0x0d, dev->nvr);
}


static void
pipc_ide_handlers(pipc_t *dev)
{
    uint16_t main, side;

    ide_pri_disable();
    ide_sec_disable();

    if (dev->ide_regs[0x09] & 0x01) {
	main = (dev->ide_regs[0x11] << 8) | (dev->ide_regs[0x10] & 0xf8);
	side = ((dev->ide_regs[0x15] << 8) | (dev->ide_regs[0x14] & 0xfc)) + 2;
    } else {
	main = 0x1f0;
	side = 0x3f6;
    }
    ide_set_base(0, main);
    ide_set_side(0, side);

    if (dev->ide_regs[0x09] & 0x04) {
	main = (dev->ide_regs[0x19] << 8) | (dev->ide_regs[0x18] & 0xf8);
	side = ((dev->ide_regs[0x1d] << 8) | (dev->ide_regs[0x1c] & 0xfc)) + 2;
    } else {
	main = 0x170;
	side = 0x376;
    }
    ide_set_base(1, main);
    ide_set_side(1, side);

    if (dev->ide_regs[0x04] & PCI_COMMAND_IO) {
	if (dev->ide_regs[0x40] & 0x02)
		ide_pri_enable();
	if (dev->ide_regs[0x40] & 0x01)
		ide_sec_enable();
    }
}


static void
pipc_ide_irqs(pipc_t *dev)
{
    int irq_mode[2] = { 0, 0 };

    if (dev->ide_regs[0x09] & 0x01)
	irq_mode[0] = (dev->ide_regs[0x3d] & 0x01);

    if (dev->ide_regs[0x09] & 0x04)
	irq_mode[1] = (dev->ide_regs[0x3d] & 0x01);

    sff_set_irq_mode(dev->bm[0], 0, irq_mode[0]);
    sff_set_irq_mode(dev->bm[0], 1, irq_mode[1]);

    sff_set_irq_mode(dev->bm[1], 0, irq_mode[0]);
    sff_set_irq_mode(dev->bm[1], 1, irq_mode[1]);
}


static void
pipc_bus_master_handlers(pipc_t *dev)
{
    uint16_t base = (dev->ide_regs[0x20] & 0xf0) | (dev->ide_regs[0x21] << 8);

    sff_bus_master_handler(dev->bm[0], (dev->ide_regs[0x04] & 1), base);
    sff_bus_master_handler(dev->bm[1], (dev->ide_regs[0x04] & 1), base + 8);
}


static void
pipc_pcs_update(pipc_t *dev)
{
    uint8_t i, io_base_reg, io_mask_reg, io_mask_shift, enable;
    uint16_t io_base, io_mask;

    for (i = 0; i <= dev->max_pcs; i++) {
	if (i & 2) {
		io_base_reg = 0x8c;
		io_mask_reg = 0x8a;
	} else {
		io_base_reg = 0x78;
		io_mask_reg = 0x80;
	}
	io_base_reg |= (i & 1) << 1;
	io_mask_shift = (i & 1) << 2;

	if (dev->local <= VIA_PIPC_596B)
		enable = dev->pci_isa_regs[0x76] & (0x10 << i);
	else
		enable = dev->pci_isa_regs[0x8b] & (0x01 << i);

	io_base = dev->pci_isa_regs[io_base_reg] | (dev->pci_isa_regs[io_base_reg | 1] << 8);
	io_mask = (dev->pci_isa_regs[io_mask_reg] >> io_mask_shift) & 0x000f;

	pipc_log("PIPC: Mapping PCS%d to %04X-%04X (enable %d)\n", i, io_base, io_base + io_mask, enable);
	io_trap_remap(dev->io_traps[TRAP_GR0 + i].trap, enable, io_base & ~io_mask, io_mask + 1);
    }
}


static void
pipc_trap_update_paden(pipc_t *dev, uint8_t trap_id,
		       uint32_t paden_mask, uint8_t enable,
		       uint16_t addr, uint16_t size)
{
    pipc_io_trap_t *trap = &dev->io_traps[trap_id];
    enable = (dev->acpi->regs.paden & paden_mask) && enable;

    /* Set up Primary Activity Detect I/O traps dynamically. */
    if (enable && !trap->trap) {
	trap->dev = dev;
	trap->trap = io_trap_add(pipc_io_trap_pact, trap);
	trap->sts_reg = &dev->acpi->regs.padsts;
	trap->en_reg = &dev->acpi->regs.paden;
	trap->mask = paden_mask;
    }

    /* Remap I/O trap. */
    io_trap_remap(trap->trap, enable, addr, size);
}


static void
pipc_trap_update_586(void *priv)
{
    pipc_t *dev = (pipc_t *) priv;

    /* TRAP_DRQ (00000001) and TRAP_PIRQ (00000002) not implemented. */

    pipc_trap_update_paden(dev, TRAP_PIDE_MAIN, 0x00000008, 1, 0x1f0, 8);
    pipc_trap_update_paden(dev, TRAP_SIDE_MAIN, 0x00000008, 1, 0x170, 8);
    pipc_trap_update_paden(dev, TRAP_FLP_MAIN, 0x00000008, 1, 0x3f5, 1);

    pipc_trap_update_paden(dev, TRAP_VGA, 0x00000010, 1, 0x3b0, 48);
    /* [A0000:BFFFF] memory trap not implemented. */

    pipc_trap_update_paden(dev, TRAP_LPT1, 0x00000020, 1, 0x378, 8);
    pipc_trap_update_paden(dev, TRAP_LPT2, 0x00000020, 1, 0x278, 8);

    pipc_trap_update_paden(dev, TRAP_COM1, 0x00000040, 1, 0x3f8, 8);
    pipc_trap_update_paden(dev, TRAP_COM2, 0x00000040, 1, 0x2f8, 8);
    pipc_trap_update_paden(dev, TRAP_COM3, 0x00000040, 1, 0x3e8, 8);
    pipc_trap_update_paden(dev, TRAP_COM4, 0x00000040, 1, 0x2e8, 8);

    pipc_trap_update_paden(dev, TRAP_KBC, 0x00000080, 1, 0x60, 1);
}


static void
pipc_trap_update_596(void *priv)
{
    pipc_t *dev = (pipc_t *) priv;
    int i;

    /* TRAP_DRQ (00000001) and TRAP_PIRQ (00000002) not implemented. */

    pipc_trap_update_paden(dev, TRAP_PIDE_MAIN, 0x00000004, 1, 0x1f0, 8);
    pipc_trap_update_paden(dev, TRAP_PIDE_SIDE, 0x00000004, 1, 0x3f6, 1);

    pipc_trap_update_paden(dev, TRAP_SIDE_MAIN, 0x00000008, 1, 0x170, 8);
    pipc_trap_update_paden(dev, TRAP_SIDE_SIDE, 0x00000008, 1, 0x376, 1);

    pipc_trap_update_paden(dev, TRAP_FLP_MAIN, 0x00000010, 1, 0x3f0, 6);
    pipc_trap_update_paden(dev, TRAP_FLP_SIDE, 0x00000010, 1, 0x3f7, 1);

    pipc_trap_update_paden(dev, TRAP_COM1, 0x00000020, 1, 0x3f8, 8);
    pipc_trap_update_paden(dev, TRAP_COM3, 0x00000020, 1, 0x3e8, 8);

    pipc_trap_update_paden(dev, TRAP_COM2, 0x00000040, 1, 0x2f8, 8);
    pipc_trap_update_paden(dev, TRAP_COM4, 0x00000040, 1, 0x2e8, 8);

    pipc_trap_update_paden(dev, TRAP_LPT1, 0x00000080, 1, 0x378, 8);
    pipc_trap_update_paden(dev, TRAP_LPT2, 0x00000080, 1, 0x278, 8);

    pipc_trap_update_paden(dev, TRAP_VGA, 0x00000100, 1, 0x3b0, 48);
    /* [A0000:BFFFF] memory trap not implemented. */

    pipc_trap_update_paden(dev, TRAP_KBC, 0x00000200, 1, 0x60, 1);

    /* The following traps are poorly documented and assumed to operate on all ranges allowed
       by the Positive Decoding Control registers. I couldn't probe this behavior on hardware.
       It's better to be safe and cover all of them than to assume Intel-like behavior (one range). */

    for (i = 0; i < 3; i++) {
	pipc_trap_update_paden(dev, TRAP_AUD_MIDI_0 + i,
			       0x00000400, (dev->local <= VIA_PIPC_596B) || (dev->power_regs[0x40] & 0x01),
			       0x300 + (0x10 * i), 4);

	pipc_trap_update_paden(dev, TRAP_AUD_SB_0 + i,
			       0x00000400, (dev->local <= VIA_PIPC_596B) || (dev->power_regs[0x40] & 0x02),
			       0x220 + (0x20 * i), 20);
    }

    pipc_trap_update_paden(dev, TRAP_AUD_GAME, 0x00000400, (dev->local <= VIA_PIPC_596B) || (dev->power_regs[0x40] & 0x04), 0x200, 8);

    pipc_trap_update_paden(dev, TRAP_AUD_WSS_0, 0x00000400, (dev->local <= VIA_PIPC_596B) || (dev->power_regs[0x40] & 0x08), 0x530, 8);
    pipc_trap_update_paden(dev, TRAP_AUD_WSS_1, 0x00000400, (dev->local <= VIA_PIPC_596B) || (dev->power_regs[0x40] & 0x08), 0x604, 8);
    pipc_trap_update_paden(dev, TRAP_AUD_WSS_2, 0x00000400, (dev->local <= VIA_PIPC_596B) || (dev->power_regs[0x40] & 0x08), 0xe80, 8);
    pipc_trap_update_paden(dev, TRAP_AUD_WSS_3, 0x00000400, (dev->local <= VIA_PIPC_596B) || (dev->power_regs[0x40] & 0x08), 0xf40, 8);
}


static void
pipc_sgd_handlers(pipc_t *dev, uint8_t modem)
{
    if (!dev->ac97)
	return;

    if (modem)
	ac97_via_remap_modem_sgd(dev->ac97, dev->ac97_regs[1][0x11] << 8, dev->ac97_regs[1][0x04] & PCI_COMMAND_IO);
    else
	ac97_via_remap_audio_sgd(dev->ac97, dev->ac97_regs[0][0x11] << 8, dev->ac97_regs[0][0x04] & PCI_COMMAND_IO);
}


static void
pipc_codec_handlers(pipc_t *dev, uint8_t modem)
{
    if (!dev->ac97)
	return;

    if (modem)
	ac97_via_remap_modem_codec(dev->ac97, dev->ac97_regs[1][0x1d] << 8, dev->ac97_regs[1][0x04] & PCI_COMMAND_IO);
    else
	ac97_via_remap_audio_codec(dev->ac97, dev->ac97_regs[0][0x1d] << 8, dev->ac97_regs[0][0x04] & PCI_COMMAND_IO);
}


static uint8_t
pipc_fmnmi_read(uint16_t addr, void *priv)
{
    pipc_t *dev = (pipc_t *) priv;
    uint8_t ret = dev->fmnmi_regs[addr & 0x03];

    pipc_log("PIPC: fmnmi_read(%02X) = %02X\n", addr & 0x03, ret);

#ifdef VIA_PIPC_FM_EMULATION
    /* Clear NMI/SMI if enabled. */
    if  (dev->ac97_regs[0][0x48] & 0x01) {
	if (dev->ac97_regs[0][0x48] & 0x04)
		smi_line = 0;
	else
		nmi = 0;
    }
#endif

    return ret;
}


static void
pipc_fmnmi_handlers(pipc_t *dev, uint8_t modem)
{
    if (!dev->ac97 || modem)
	return;

    if (dev->fmnmi_base)
	io_removehandler(dev->fmnmi_base, 4, pipc_fmnmi_read, NULL, NULL, NULL, NULL, NULL, dev);

    dev->fmnmi_base = (dev->ac97_regs[0][0x15] << 8) | (dev->ac97_regs[0][0x14] & 0xfc);

    if (dev->fmnmi_base && (dev->ac97_regs[0][0x04] & PCI_COMMAND_IO))
	io_sethandler(dev->fmnmi_base, 4, pipc_fmnmi_read, NULL, NULL, NULL, NULL, NULL, dev);
}


static uint8_t
pipc_fm_read(uint16_t addr, void *priv)
{
#ifdef VIA_PIPC_FM_EMULATION
    uint8_t ret = 0x00;
#else
    pipc_t *dev = (pipc_t *) priv;
    uint8_t ret = opl3_read(addr, &dev->sb->opl);
#endif

    pipc_log("PIPC: fm_read(%02X) = %02X\n", addr & 0x03, ret);

    return ret;
}


static void
pipc_fm_write(uint16_t addr, uint8_t val, void *priv)
{
    pipc_t *dev = (pipc_t *) priv;

    pipc_log("PIPC: fm_write(%02X, %02X)\n", addr & 0x03, val);

#ifdef VIA_PIPC_FM_EMULATION
    /* Real 686B only updates the bank ID register when writing to the
       index port, and only fires NMI/SMI when writing to the data port. */
    if (!(addr & 0x01)) {
	dev->fmnmi_regs[0x00] = (addr & 0x02) ? 0x02 : 0x01;
	dev->fmnmi_regs[0x01] = val;
    } else {
	dev->fmnmi_regs[0x02] = val;

	/* Fire NMI/SMI if enabled. */
	if (dev->ac97_regs[0][0x48] & 0x01) {
		if (dev->ac97_regs[0][0x48] & 0x04)
			smi_line = 1;
		else
			nmi = 1;
	}
    }
#else
    opl3_write(addr, val, &dev->sb->opl);
#endif
}


static void
pipc_sb_handlers(pipc_t *dev, uint8_t modem)
{
    if (!dev->ac97 || modem)
	return;

    sb_dsp_setaddr(&dev->sb->dsp, 0);
    if (dev->sb_base) {
	io_removehandler(dev->sb_base,     4, opl3_read, NULL, NULL, opl3_write, NULL, NULL, &dev->sb->opl);
	io_removehandler(dev->sb_base + 8, 2, opl3_read, NULL, NULL, opl3_write, NULL, NULL, &dev->sb->opl);
	io_removehandler(dev->sb_base + 4, 2, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, dev->sb);
    }

    mpu401_change_addr(dev->sb->mpu, 0);
    mpu401_setirq(dev->sb->mpu, 0);

    io_removehandler(0x388, 4, opl3_read, NULL, NULL, opl3_write, NULL, NULL, &dev->sb->opl);

    if (dev->ac97_regs[0][0x42] & 0x01) {
	dev->sb_base = 0x220 + (0x20 * (dev->ac97_regs[0][0x43] & 0x03));
	sb_dsp_setaddr(&dev->sb->dsp, dev->sb_base);
	if (dev->ac97_regs[0][0x42] & 0x04) {
		io_sethandler(dev->sb_base,     4, opl3_read, NULL, NULL, opl3_write, NULL, NULL, &dev->sb->opl);
		io_sethandler(dev->sb_base + 8, 2, opl3_read, NULL, NULL, opl3_write, NULL, NULL, &dev->sb->opl);
	}
	io_sethandler(dev->sb_base + 4, 2, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, dev->sb);

	uint8_t irq = 5 + (2 * ((dev->ac97_regs[0][0x43] >> 6) & 0x03));
	sb_dsp_setirq(&dev->sb->dsp, (irq == 11) ? 10 : irq);

	sb_dsp_setdma8(&dev->sb->dsp, (dev->ac97_regs[0][0x43] >> 4) & 0x03);

	/* Set up CD audio filter. This might not actually work if VIAUDIO writes to CD volume through AC97. */
	sound_set_cd_audio_filter(sbpro_filter_cd_audio, dev->sb);
    }

    if (dev->ac97_regs[0][0x42] & 0x02) {
	/* BAR 2 is a mess. The MPU and game port remapping registers that VIA claims to be there don't
	   seem to actually exist on a real 686B. Remapping the MPU to BAR 2 itself does work, though. */
	if (dev->ac97_regs[0][0x42] & 0x80)
		mpu401_change_addr(dev->sb->mpu, (dev->ac97_regs[0][0x19] << 8) | (dev->ac97_regs[0][0x18] & 0xfc));
	else
		mpu401_change_addr(dev->sb->mpu, 0x300 | ((dev->ac97_regs[0][0x43] << 2) & 0x30));

        if (!(dev->ac97_regs[0][0x42] & 0x40))
		mpu401_setirq(dev->sb->mpu, dev->sb->dsp.sb_irqnum);
    }

    if (dev->ac97_regs[0][0x42] & 0x04) {
	io_sethandler(0x388, 4, pipc_fm_read, NULL, NULL, pipc_fm_write, NULL, NULL, dev);
    }
}


static uint8_t
pipc_read(int func, int addr, void *priv)
{
    pipc_t *dev = (pipc_t *) priv;
    uint8_t ret = 0xff;
    int c;
    uint8_t pm_func = dev->usb[1] ? 4 : 3;

    if (func > dev->max_func)
	return ret;
    else if (func == 0) { /* PCI-ISA bridge */
	if ((addr >= 0x60) && (addr <= 0x6f)) { /* DMA shadow registers */
		c = (addr & 0x0e) >> 1;
		if (addr & 0x01)
			ret = (dma[c].ab & 0x0000ff00) >> 8;
		else {
			ret = (dma[c].ab & 0x000000f0);
			ret |= (!!(dma_e & (1 << c)) << 3);
		}
	} else
		ret = dev->pci_isa_regs[addr];
    }
    else if ((func == 1) && !(dev->pci_isa_regs[0x48] & 0x02)) { /* IDE */
	ret = dev->ide_regs[addr];
	if ((addr >= 0x50) && (addr <= 0x53)) { /* UDMA timing registers */
		/* Set or clear bit 5 according to UDMA mode. Documentation is unclear, but a real
		   686B does set bit 5 when UDMA is enabled through the method specified in bit 7. */
		c = 0x53 - addr;
		if (ret & 0x80) /* bit 7 set = use bit 6 */
			c = ret & 0x40;
		else if (ide_drives[c]) /* bit 7 clear = use SET FEATURES mode */
			c = (ide_drives[c]->mdma_mode & 0x300) == 0x300;
		else /* no drive here */
			c = 0;
		/* 586A/B datasheet claims bit 5 must be clear for UDMA, unlike later models where
		   it must be set, but the Windows driver doesn't care and always checks if it's set. */
		if (c)
			ret |= 0x20;
		else
			ret &= ~0x20;
	}
    }
    else if ((func < pm_func) && !((func == 2) ? (dev->pci_isa_regs[0x48] & 0x04) : (dev->pci_isa_regs[0x85] & 0x10))) /* USB */
	ret = dev->usb_regs[func - 2][addr];
    else if (func == pm_func) { /* Power */
	ret = dev->power_regs[addr];
	if (addr == 0x42) {
		if (dev->nvr->regs[0x0d] & 0x80)
			ret |= 0x10;
		else
			ret &= ~0x10;
	} else if ((addr == 0xd2) && (dev->local == VIA_PIPC_686B)) {
		/* SMBus clock select bit. */
		if (dev->smbus->clock == 16384)
			ret &= ~0x10;
		else
			ret |= 0x10;
	}
    }
    else if ((func <= (pm_func + 2)) && !(dev->pci_isa_regs[0x85] & ((func == (pm_func + 1)) ? 0x04 : 0x08))) { /* AC97 / MC97 */
	if (addr == 0x40)
		ret = ac97_via_read_status(dev->ac97, func - pm_func - 1);
	else
		ret = dev->ac97_regs[func - pm_func - 1][addr];
    }

    pipc_log("PIPC: read(%d, %02X) = %02X\n", func, addr, ret);

    return ret;
}


static void
nvr_update_io_mapping(pipc_t *dev)
{
    if (dev->nvr_enabled)
	nvr_at_handler(0, 0x0074, dev->nvr);

    if ((dev->pci_isa_regs[0x5b] & 0x02) || (dev->pci_isa_regs[0x48] & 0x08))
	nvr_at_handler(1, 0x0074, dev->nvr);
}


static void
usb_update_io_mapping(pipc_t *dev, int func)
{
    uhci_update_io_mapping(dev->usb[func - 2], dev->usb_regs[func - 2][0x20] & ~0x1f, dev->usb_regs[func - 2][0x21], dev->usb_regs[func - 2][PCI_REG_COMMAND] & PCI_COMMAND_IO);
}


static void
pipc_ddma_update(pipc_t *dev, int addr)
{
    uint32_t base;

    if (dev->local >= VIA_PIPC_8231)
	return;

    base = (dev->pci_isa_regs[addr] & 0xf0) | (((uint32_t) dev->pci_isa_regs[addr | 0x01]) << 8);
    ddma_update_io_mapping(dev->ddma, (addr & 0x0e) >> 1, (dev->pci_isa_regs[addr] & 0xf0), dev->pci_isa_regs[addr | 0x01], (dev->pci_isa_regs[addr] & 0x08) && (base != 0x0000));
}


static void
pipc_write(int func, int addr, uint8_t val, void *priv)
{
    pipc_t *dev = (pipc_t *) priv;
    int c;
    uint8_t pm_func = dev->usb[1] ? 4 : 3;

    if (func > dev->max_func)
	return;

    pipc_log("PIPC: write(%d, %02X, %02X)\n", func, addr, val);

    if (func == 0) { /* PCI-ISA bridge */
	/* Read-only addresses. */
	if ((addr < 4) || (addr == 5) || ((addr >= 8) && (addr < 0x40)) || (addr == 0x49) || (addr == 0x4b) ||
	    (addr == 0x53) || ((addr >= 0x5d) && (addr < 0x5f)) || (addr >= 0x90))
		return;

	if ((dev->local <= VIA_PIPC_586A) && ((addr >= 0x58) && (addr < 0x80)))
		return;

	if ((dev->local <= VIA_PIPC_586B) && (addr >= 0x74))
		return;

	if ((dev->local <= VIA_PIPC_596A) && ((addr == 0x51) || (addr == 0x52) || (addr == 0x5f) || (addr == 0x85) ||
	    (addr == 0x86) || ((addr >= 0x8a) && (addr < 0x90))))
		return;

	switch (addr) {
		case 0x04:
			dev->pci_isa_regs[0x04] = (val & 8) | 7;
			break;
		case 0x07:
			dev->pci_isa_regs[0x07] &= ~(val & 0xb0);
			break;

		case 0x42:
			dev->pci_isa_regs[0x42] = val & 0xcf;

			switch (val & 0xf) {
				/* Divisors on the PCI clock. */
				case 0x8:
					cpu_set_isa_pci_div(3);
					break;

				case 0x9:
					cpu_set_isa_pci_div(2);
					break;

				/* case 0xa: same as default */

				case 0xb:
					cpu_set_isa_pci_div(6);
					break;

				case 0xc:
					cpu_set_isa_pci_div(5);
					break;

				case 0xd:
					cpu_set_isa_pci_div(10);
					break;

				case 0xe:
					cpu_set_isa_pci_div(12);
					break;

				/* Half oscillator clock. */
				case 0xf:
					cpu_set_isa_speed(7159091);
					break;

				/* Divisor 4 on the PCI clock whenever bit 3 is clear. */
				default:
					cpu_set_isa_pci_div(4);
					break;
			}

			break;

		case 0x47:
			if (val & 0x01)
				trc_write(0x0047, (val & 0x80) ? 0x06 : 0x04, NULL);
			pic_set_shadow(!!(val & 0x10));
			pic_elcr_io_handler(!!(val & 0x20));
			dev->pci_isa_regs[0x47] = val & 0xfe;
			break;
		case 0x48:
			dev->pci_isa_regs[0x48] = val;
			nvr_update_io_mapping(dev);
			break;

		case 0x50: case 0x51: case 0x52: case 0x85:
			dev->pci_isa_regs[addr] = val;
			/* Forward Super I/O-related registers to sio_vt82c686.c */
			if (dev->sio)
				vt82c686_sio_write(addr, val, dev->sio);
			break;

		case 0x54:
			pci_set_irq_level(PCI_INTA, !(val & 8));
			pci_set_irq_level(PCI_INTB, !(val & 4));
			pci_set_irq_level(PCI_INTC, !(val & 2));
			pci_set_irq_level(PCI_INTD, !(val & 1));
			dev->pci_isa_regs[0x54] = val & 0x0f;
			break;
		case 0x55:
			pipc_log("PIPC: Steering PIRQ%c to IRQ %d\n", (dev->local >= VIA_PIPC_596A) ? 'A' : 'D', val >> 4);
			pci_set_irq_routing((dev->local >= VIA_PIPC_596A) ? PCI_INTA : PCI_INTD, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
			if (dev->local <= VIA_PIPC_586B) {
				pipc_log("PIPC: Steering MIRQ0 to IRQ %d\n", val & 0x0f);
				pci_set_mirq_routing(PCI_MIRQ0, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
			}
			dev->pci_isa_regs[0x55] = val;
			break;
		case 0x56:
			pipc_log("PIPC: Steering PIRQ%c to IRQ %d\n", (dev->local >= VIA_PIPC_596A) ? 'C' : 'A', val >> 4);
			pipc_log("PIPC: Steering PIRQB to IRQ %d\n", val & 0x0f);
			pci_set_irq_routing((dev->local >= VIA_PIPC_596A) ? PCI_INTC : PCI_INTA, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
			pci_set_irq_routing(PCI_INTB, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
			dev->pci_isa_regs[0x56] = val;
			break;
		case 0x57:
			pipc_log("PIPC: Steering PIRQ%c to IRQ %d\n", (dev->local >= VIA_PIPC_596A) ? 'D' : 'C', val >> 4);
			pci_set_irq_routing((dev->local >= VIA_PIPC_596A) ? PCI_INTD : PCI_INTC, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
			if (dev->local <= VIA_PIPC_586B) {
				pipc_log("PIPC: Steering MIRQ1 to IRQ %d\n", val & 0x0f);
				pci_set_mirq_routing(PCI_MIRQ1, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
			}
			dev->pci_isa_regs[0x57] = val;
			break;
		case 0x58:
			if (dev->local == VIA_PIPC_586B) {
				pipc_log("PIPC: Steering MIRQ2 to IRQ %d\n", val & 0x0f);
				pci_set_mirq_routing(PCI_MIRQ2, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
			}
			dev->pci_isa_regs[0x58] = val;
			break;
		case 0x5b:
			dev->pci_isa_regs[0x5b] = val;
			nvr_update_io_mapping(dev);
			break;

		case 0x60: case 0x62: case 0x64: case 0x66:
		case 0x6a: case 0x6c: case 0x6e:
			dev->pci_isa_regs[addr] = val & 0xf8;
			pipc_ddma_update(dev, addr);
			break;
		case 0x61: case 0x63: case 0x65: case 0x67:
		case 0x6b: case 0x6d: case 0x6f:
			dev->pci_isa_regs[addr] = val;
			pipc_ddma_update(dev, addr & 0xfe);
			break;

		case 0x70: case 0x71: case 0x72: case 0x73:
			dev->pci_isa_regs[(addr - 0x44)] = val;
			break;

		case 0x74: case 0x8b:
		case 0x78: case 0x79: case 0x7a: case 0x7b:
		case 0x8c: case 0x8d: case 0x8e: case 0x8f:
		case 0x80: case 0x8a:
			dev->pci_isa_regs[addr] = val;
			pipc_pcs_update(dev);
			break;

		case 0x77:
			if ((dev->local >= VIA_PIPC_686A) && (val & 0x10))
				pclog("PIPC: Warning: Internal I/O APIC enabled.\n");
			nvr_via_wp_set(!!(val & 0x04), 0x32, dev->nvr);
			nvr_via_wp_set(!!(val & 0x02), 0x0d, dev->nvr);
			break;

		default:
			dev->pci_isa_regs[addr] = val;
			break;
	}
    } else if (func == 1) { /* IDE */
	/* Read-only addresses. */
	if ((addr < 4) || (addr == 5) || (addr == 8) || ((addr >= 0xa) && (addr < 0x0d)) ||
	    ((addr >= 0x0e) && (addr < 0x10)) || ((addr >= 0x12) && (addr < 0x13)) ||
	    ((addr >= 0x16) && (addr < 0x17)) || ((addr >= 0x1a) && (addr < 0x1b)) ||
	    ((addr >= 0x1e) && (addr < 0x1f)) || ((addr >= 0x22) && (addr < 0x3c)) ||
	    ((addr >= 0x3e) && (addr < 0x40)) || ((addr >= 0x55) && (addr < 0x60)) ||
	    ((addr >= 0x62) && (addr < 0x68)) || ((addr >= 0x6a) && (addr < 0x70)) ||
	    (addr == 0x72) || (addr == 0x73) || (addr == 0x76) || (addr == 0x77) ||
	    (addr == 0x7a) || (addr == 0x7b) || (addr == 0x7e) || (addr == 0x7f) ||
	    ((addr >= 0x84) && (addr < 0x88)) || (addr >= 0x8c))
		return;

	if ((dev->local <= VIA_PIPC_586B) && ((addr == 0x54) || (addr >= 0x70)))
		return;

	/* Check disable bit. */
	if (dev->pci_isa_regs[0x48] & 0x02)
		return;

	switch (addr) {
		case 0x04:
			dev->ide_regs[0x04] = val & 0x85;
			pipc_ide_handlers(dev);
			pipc_bus_master_handlers(dev);
			break;
		case 0x07:
			dev->ide_regs[0x07] &= ~(val & 0xf1);
			break;

		case 0x09:
			dev->ide_regs[0x09] = (val & 0x05) | 0x8a;
			pipc_ide_handlers(dev);
			pipc_ide_irqs(dev);
			break;

		case 0x10:
			dev->ide_regs[0x10] = (val & 0xf8) | 1;
			pipc_ide_handlers(dev);
			break;
		case 0x11:
			dev->ide_regs[0x11] = val;
			pipc_ide_handlers(dev);
			break;

		case 0x14:
			dev->ide_regs[0x14] = (val & 0xfc) | 1;
			pipc_ide_handlers(dev);
			break;
		case 0x15:
			dev->ide_regs[0x15] = val;
			pipc_ide_handlers(dev);
			break;

		case 0x18:
			dev->ide_regs[0x18] = (val & 0xf8) | 1;
			pipc_ide_handlers(dev);
			break;
		case 0x19:
			dev->ide_regs[0x19] = val;
			pipc_ide_handlers(dev);
			break;

		case 0x1c:
			dev->ide_regs[0x1c] = (val & 0xfc) | 1;
			pipc_ide_handlers(dev);
			break;
		case 0x1d:
			dev->ide_regs[0x1d] = val;
			pipc_ide_handlers(dev);
			break;

		case 0x20:
			dev->ide_regs[0x20] = (val & 0xf0) | 1;
			pipc_bus_master_handlers(dev);
			break;
		case 0x21:
			dev->ide_regs[0x21] = val;
			pipc_bus_master_handlers(dev);
			break;

		case 0x3d:
			dev->ide_regs[0x3d] = val & 0x01;
			pipc_ide_irqs(dev);
			break;

		case 0x40:
			if (dev->local <= VIA_PIPC_586B)
				dev->ide_regs[0x40] = (val & 0x03) | 0x04;
			else
				dev->ide_regs[0x40] = val & 0x0f;
			pipc_ide_handlers(dev);
			break;

		case 0x41:
			if (dev->local <= VIA_PIPC_686A)
				dev->ide_regs[0x41] = val;
			else if (dev->local == VIA_PIPC_8231)
				dev->ide_regs[0x41] = val & 0xf6;
			else
				dev->ide_regs[0x41] = val & 0xf2;
			break;

		case 0x43:
			if (dev->local <= VIA_PIPC_586A)
				dev->ide_regs[0x43] = (val & 0x6f) | 0x10;
			else if (dev->local <= VIA_PIPC_586B)
				dev->ide_regs[0x43] = (val & 0xef) | 0x10;
			else
				dev->ide_regs[0x43] = val & 0x0f;
			break;

		case 0x44:
			if (dev->local <= VIA_PIPC_586A)
				dev->ide_regs[0x44] = val & 0x78;
			else if (dev->local <= VIA_PIPC_586B)
				dev->ide_regs[0x44] = val & 0x7b;
			else if (dev->local <= VIA_PIPC_596B)
				dev->ide_regs[0x44] = val & 0x7f;
			else if ((dev->local <= VIA_PIPC_686A) || (dev->local == VIA_PIPC_8231))
				dev->ide_regs[0x44] = val & 0x69;
			else
				dev->ide_regs[0x44] = val & 0x7d;
			break;

		case 0x45:
			if (dev->local <= VIA_PIPC_586B)
				dev->ide_regs[0x45] = val & 0x40;
			else if ((dev->local <= VIA_PIPC_596B) || (dev->local == VIA_PIPC_8231))
				dev->ide_regs[0x45] = val & 0x4f;
			else if (dev->local <= VIA_PIPC_686A)
				dev->ide_regs[0x45] = val & 0x5f;
			else
				dev->ide_regs[0x45] = (val & 0x5c) | 0x20;
			break;

		case 0x46:
			if ((dev->local <= VIA_PIPC_686A) || (dev->local == VIA_PIPC_8231))
				dev->ide_regs[0x46] = val & 0xf3;
			else
				dev->ide_regs[0x46] = val & 0xc0;
			break;

		case 0x50: case 0x51: case 0x52: case 0x53:
			if (dev->local <= VIA_PIPC_586B)
				dev->ide_regs[addr] = val & 0xc3;
			else if (dev->local <= VIA_PIPC_596B)
				dev->ide_regs[addr] = val & ((addr & 1) ? 0xc3 : 0xcb);
			else if ((dev->local <= VIA_PIPC_686A) || (dev->local == VIA_PIPC_8231))
				dev->ide_regs[addr] = val & ((addr & 1) ? 0xc7 : 0xcf);
			else
				dev->ide_regs[addr] = val & 0xd7;
			break;

		case 0x61: case 0x69:
			dev->ide_regs[addr] = val & 0x0f;
			break;

		default:
			dev->ide_regs[addr] = val;
			break;
	}
    } else if (func < pm_func) { /* USB */
	/* Read-only addresses. */
	if ((addr < 4) || (addr == 5) || (addr == 6) || ((addr >= 8) && (addr < 0xd)) ||
	    ((addr >= 0xe) && (addr < 0x20)) || ((addr >= 0x22) && (addr < 0x3c)) ||
	    ((addr >= 0x3e) && (addr < 0x40)) || ((addr >= 0x42) && (addr < 0x44)) ||
	    ((addr >= 0x46) && (addr < 0x84)) || ((addr >= 0x85) && (addr < 0xc0)) || (addr >= 0xc2))
		return;

	if ((dev->local <= VIA_PIPC_596B) && (addr == 0x84))
		return;

	/* Check disable bits for both controllers. */
	if ((func == 2) ? (dev->pci_isa_regs[0x48] & 0x04) : (dev->pci_isa_regs[0x85] & 0x10))
		return;

	switch (addr) {
		case 0x04:
			dev->usb_regs[func - 2][0x04] = val & 0x97;
			usb_update_io_mapping(dev, func);
			break;
		case 0x07:
			dev->usb_regs[func - 2][0x07] &= ~(val & 0x78);
			break;

		case 0x20:
			dev->usb_regs[func - 2][0x20] = (val & ~0x1f) | 1;
			usb_update_io_mapping(dev, func);
			break;
		case 0x21:
			dev->usb_regs[func - 2][0x21] = val;
			usb_update_io_mapping(dev, func);
			break;

		default:
			dev->usb_regs[func - 2][addr] = val;
			break;
	}
    } else if (func == pm_func) { /* Power */
	/* Read-only addresses */
	if ((addr < 0xd) || ((addr >= 0xe) && (addr < 0x40)) || (addr == 0x43) || (addr == 0x4a) || (addr == 0x4b) ||
	    (addr == 0x4e) || (addr == 0x4f) || (addr == 0x56) || (addr == 0x57) || ((addr >= 0x5c) && (addr < 0x61)) ||
	    ((addr >= 0x64) && (addr < 0x70)) || (addr == 0x72) || (addr == 0x73) || ((addr >= 0x75) && (addr < 0x80)) ||
	    (addr == 0x83) || ((addr >= 0x85) && (addr < 0x90)) || ((addr >= 0x92) && (addr < 0xd2)) || (addr >= 0xd7))
		return;

	if ((dev->local <= VIA_PIPC_586B) && ((addr == 0x48) || (addr == 0x4c) || (addr == 0x4d) || (addr >= 0x54)))
		return;

	if ((dev->local <= VIA_PIPC_596B) && ((addr >= 0x64) && (addr < (dev->local == VIA_PIPC_596A ? 0x80 : 0x85))))
		return;

	switch (addr) {
		case 0x41: case 0x48: case 0x49:
			if (addr == 0x48) {
				if (dev->local >= VIA_PIPC_596A)
					val = (val & 0x80) | 0x01;
				else
					val = 0x01;
			}

			dev->power_regs[addr] = val;
			c = (dev->power_regs[0x49] << 8);
			if (dev->local >= VIA_PIPC_596A)
				c |= (dev->power_regs[0x48] & 0x80);
			/* Workaround for P3V133 BIOS in 596B mode mapping ACPI to E800 (same as SMBus) instead of E400. */
			if ((dev->local == VIA_PIPC_596B) && (c == ((dev->power_regs[0x91] << 8) | (dev->power_regs[0x90] & 0xf0))) && (dev->power_regs[0xd2] & 0x01))
				c -= 0x400;
			acpi_set_timer32(dev->acpi, dev->power_regs[0x41] & 0x08);
			acpi_update_io_mapping(dev->acpi, c, dev->power_regs[0x41] & 0x80);
			break;

		case 0x42:
			dev->power_regs[addr] = (dev->power_regs[addr] & 0xf0) | (val & 0x0f);
			acpi_set_irq_line(dev->acpi, dev->power_regs[addr] & 0x0f);
			break;

		case 0x54:
			if (dev->local <= VIA_PIPC_596B)
				dev->power_regs[addr] = val; /* write-only on 686A+ */
			else
				smbus_piix4_setclock(dev->smbus, (val & 0x80) ? 65536 : 16384); /* final clock undocumented on 686A, assume RTC*2 like 686B */
			break;

		case 0x61: case 0x62: case 0x63:
			dev->power_regs[(addr - 0x58)] = val;
			break;

		case 0x70: case 0x71: case 0x74:
			dev->power_regs[addr] = val;
			/* Forward hardware monitor-related registers to hwm_vt82c686.c */
			if (dev->hwm)
				vt82c686_hwm_write(addr, val, dev->hwm);
			break;

		case 0x80: case 0x81: case 0x84: /* 596A has the SMBus I/O base and enable bit here instead. */
			dev->power_regs[addr] = val;
			smbus_piix4_remap(dev->smbus, (dev->power_regs[0x81] << 8) | (dev->power_regs[0x80] & 0xf0), dev->power_regs[0x84] & 0x01);
			break;

		case 0xd2:
			if (dev->local == VIA_PIPC_686B)
				smbus_piix4_setclock(dev->smbus, (val & 0x04) ? 65536 : 16384);
			/* fall-through */

		case 0x90: case 0x91:
			dev->power_regs[addr] = val;
			smbus_piix4_remap(dev->smbus, (dev->power_regs[0x91] << 8) | (dev->power_regs[0x90] & 0xf0), dev->power_regs[0xd2] & 0x01);
			break;

		default:
			dev->power_regs[addr] = val;
			break;
	}
    } else if (func <= pm_func + 2) { /* AC97 / MC97 */
	/* Read-only addresses. */
	if ((addr < 0x4) || ((addr >= 0x6) && (addr < 0x9)) || ((addr >= 0xc) && (addr < 0x11)) || (addr == 0x16) ||
	    (addr == 0x17) || (addr == 0x1a) || (addr == 0x1b) || ((addr >= 0x1e) && (addr < 0x2c)) ||
	    ((addr >= 0x30) && (addr < 0x34)) || ((addr >= 0x35) && (addr < 0x3c)) || ((addr >= 0x3d) && (addr < 0x41)) ||
	    ((addr >= 0x45) && (addr < 0x4a)) || (addr >= 0x4c))
		return;

	/* Small shortcut. */
	func = func - pm_func - 1;

	/* Check disable bits and specific read-only addresses for both controllers. */
	if ((func == 0) && (((addr >= 0x09) && (addr < 0xc)) || (addr == 0x44) || (dev->pci_isa_regs[0x85] & 0x04)))
		return;

	if ((func == 1) && ((addr == 0x14) || (addr == 0x15) || (addr == 0x18) || (addr == 0x19) || (addr == 0x42) ||
	    (addr == 0x43) || (addr == 0x48) || (addr == 0x4a) || (addr == 0x4b) || (dev->pci_isa_regs[0x85] & 0x08)))
		return;

	switch (addr) {
		case 0x04:
			dev->ac97_regs[func][addr] = val;
			pipc_sgd_handlers(dev, func);
			pipc_codec_handlers(dev, func);
			pipc_fmnmi_handlers(dev, func);
			break;

		case 0x09: case 0x0a: case 0x0b:
			if (dev->ac97_regs[func][0x44] & 0x20)
				dev->ac97_regs[func][addr] = val;
			break;

		case 0x10: case 0x11:
			dev->ac97_regs[func][addr] = val;
			pipc_sgd_handlers(dev, func);
			break;

		case 0x14: case 0x15:
			if (addr == 0x14)
				val = (val & 0xfc) | 1;
			dev->ac97_regs[func][addr] = val;
			pipc_fmnmi_handlers(dev, func);
			break;

		case 0x18: case 0x19:
			if (addr == 0x18)
				val = (val & 0xfc) | 1;
			dev->ac97_regs[func][addr] = val;
			pipc_sb_handlers(dev, func);
			break;

		case 0x1c: case 0x1d:
			dev->ac97_regs[func][addr] = val;
			pipc_codec_handlers(dev, func);
			break;

		case 0x2c: case 0x2d: case 0x2e: case 0x2f:
			if ((func == 0) && (dev->ac97_regs[func][0x42] & 0x20))
				dev->ac97_regs[func][addr] = val;
			break;

		case 0x41:
			dev->ac97_regs[func][addr] = val;
			ac97_via_write_control(dev->ac97, func, val);
			break;

		case 0x42: case 0x4a: case 0x4b:
			dev->ac97_regs[0][addr] = dev->ac97_regs[1][addr] = val;
			gameport_remap(dev->gameport, (dev->ac97_regs[0][0x42] & 0x08) ? ((dev->ac97_regs[0][0x4b] << 8) | (dev->ac97_regs[0][0x4a] & 0xf8)) : 0);
			if (addr == 0x42)
				pipc_sb_handlers(dev, func);
			break;

		case 0x43:
			dev->ac97_regs[0][addr] = dev->ac97_regs[1][addr] = val;
			break;

		case 0x44:
			dev->ac97_regs[0][addr] = dev->ac97_regs[1][addr] = val & 0xf0;
			break;

		case 0x45: case 0x48:
			dev->ac97_regs[0][addr] = dev->ac97_regs[1][addr] = val & 0x0f;
			break;

		default:
			dev->ac97_regs[func][addr] = val;
			break;
	}
    }
}


static void
pipc_reset(void *p)
{
    pipc_t *dev = (pipc_t *) p;
    uint8_t pm_func = dev->usb[1] ? 4 : 3;

    pipc_write(pm_func, 0x41, 0x00, p);
    pipc_write(pm_func, 0x48, 0x01, p);
    pipc_write(pm_func, 0x49, 0x00, p);

    pipc_write(1, 0x04, 0x80, p);
    pipc_write(1, 0x09, 0x85, p);
    pipc_write(1, 0x10, 0xf1, p);
    pipc_write(1, 0x11, 0x01, p);
    pipc_write(1, 0x14, 0xf5, p);
    pipc_write(1, 0x15, 0x03, p);
    pipc_write(1, 0x18, 0x71, p);
    pipc_write(1, 0x19, 0x01, p);
    pipc_write(1, 0x1c, 0x75, p);
    pipc_write(1, 0x1d, 0x03, p);
    pipc_write(1, 0x20, 0x01, p);
    pipc_write(1, 0x21, 0xcc, p);
    if (dev->local <= VIA_PIPC_586B)
	pipc_write(1, 0x40, 0x04, p);
    else
	pipc_write(1, 0x40, 0x00, p);

    pipc_write(0, 0x77, 0x00, p);
}


static void *
pipc_init(const device_t *info)
{
    pipc_t *dev = (pipc_t *) malloc(sizeof(pipc_t));
    memset(dev, 0, sizeof(pipc_t));

    pipc_log("PIPC: init()\n");

    dev->local = info->local;
    dev->slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, pipc_read, pipc_write, dev);

    dev->bm[0] = device_add_inst(&sff8038i_device, 1);
    sff_set_slot(dev->bm[0], dev->slot);
    sff_set_irq_mode(dev->bm[0], 0, 0);
    sff_set_irq_mode(dev->bm[0], 1, 0);
    sff_set_irq_pin(dev->bm[0], PCI_INTA);

    dev->bm[1] = device_add_inst(&sff8038i_device, 2);
    sff_set_slot(dev->bm[1], dev->slot);
    sff_set_irq_mode(dev->bm[1], 0, 0);
    sff_set_irq_mode(dev->bm[1], 1, 0);
    sff_set_irq_pin(dev->bm[1], PCI_INTA);

    dev->nvr = device_add(&via_nvr_device);

    if (dev->local == VIA_PIPC_686B)
	dev->smbus = device_add(&via_smbus_device);
    else if (dev->local >= VIA_PIPC_596A)
	dev->smbus = device_add(&piix4_smbus_device);

    if (dev->local >= VIA_PIPC_596A) {
	dev->acpi = device_add(&acpi_via_596b_device);
	acpi_set_trap_update(dev->acpi, pipc_trap_update_596, dev);
    } else if (dev->local >= VIA_PIPC_586B) {
	dev->acpi = device_add(&acpi_via_device);
	acpi_set_trap_update(dev->acpi, pipc_trap_update_586, dev);
    }

    dev->usb[0] = device_add_inst(&usb_device, 1);
    if (dev->local >= VIA_PIPC_686A) {
	dev->usb[1] = device_add_inst(&usb_device, 2);

	dev->ac97 = device_add(&ac97_via_device);
	ac97_via_set_slot(dev->ac97, dev->slot, PCI_INTC);

	dev->sb = device_add_inst(&sb_pro_compat_device, 2);
#ifndef VIA_PIPC_FM_EMULATION
	dev->sb->opl_enabled = 1;
#endif
	sound_add_handler(sb_get_buffer_sbpro, dev->sb);

	dev->gameport = gameport_add(&gameport_sio_device);

	dev->sio = device_add(&via_vt82c686_sio_device);
	dev->hwm = device_add(&via_vt82c686_hwm_device);
    }

    pipc_reset_hard(dev);

    device_add(&port_92_pci_device);

    cpu_set_isa_pci_div(4);

    dma_alias_set();

    if (dev->local <= VIA_PIPC_586B) {
	pci_enable_mirq(0);
	pci_enable_mirq(1);
	if (dev->local == VIA_PIPC_586B)
		pci_enable_mirq(2);
    }

    if (dev->local < VIA_PIPC_8231)
	dev->ddma = device_add(&ddma_device);

    if (dev->acpi) {
	acpi_set_slot(dev->acpi, dev->slot);
	acpi_set_nvr(dev->acpi, dev->nvr);

	acpi_init_gporeg(dev->acpi, 0xff, 0xbf, 0xff, 0x7f);
    }

    return dev;
}


static void
pipc_close(void *p)
{
    pipc_t *dev = (pipc_t *) p;

    pipc_log("PIPC: close()\n");

    for (int i = 0; i < TRAP_MAX; i++)
	io_trap_remove(dev->io_traps[i].trap);

    free(dev);
}

const device_t via_vt82c586b_device = {
    .name = "VIA VT82C586B",
    .internal_name = "via_vt82c586b",
    .flags = DEVICE_PCI,
    .local = VIA_PIPC_586B,
    .init = pipc_init,
    .close = pipc_close,
    .reset = pipc_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t via_vt82c596a_device = {
    .name = "VIA VT82C596A",
    .internal_name = "via_vt82c596a",
    .flags = DEVICE_PCI,
    .local = VIA_PIPC_596A,
    .init = pipc_init,
    .close = pipc_close,
    .reset = pipc_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t via_vt82c596b_device = {
    .name = "VIA VT82C596B",
    .internal_name = "via_vt82c596b",
    .flags = DEVICE_PCI,
    .local = VIA_PIPC_596B,
    .init = pipc_init,
    .close = pipc_close,
    .reset = pipc_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t via_vt82c686a_device = {
    .name = "VIA VT82C686A",
    .internal_name = "via_vt82c686a",
    .flags = DEVICE_PCI,
    .local = VIA_PIPC_686A,
    .init = pipc_init,
    .close = pipc_close,
    .reset = pipc_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t via_vt82c686b_device = {
    .name = "VIA VT82C686B",
    .internal_name = "via_vt82c686b",
    .flags = DEVICE_PCI,
    .local = VIA_PIPC_686B,
    .init = pipc_init,
    .close = pipc_close,
    .reset = pipc_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t via_vt8231_device = {
    .name = "VIA VT8231",
    .internal_name = "via_vt8231",
    .flags = DEVICE_PCI,
    .local = VIA_PIPC_8231,
    .init = pipc_init,
    .close = pipc_close,
    .reset = pipc_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
