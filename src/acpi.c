/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ACPI emulation.
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
#include "cpu.h"
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/keyboard.h>
#include <86box/nvr.h>
#include <86box/pit.h>
#include <86box/apm.h>
#include <86box/acpi.h>


#ifdef ENABLE_ACPI_LOG
int acpi_do_log = ENABLE_ACPI_LOG;


static void
acpi_log(const char *fmt, ...)
{
    va_list ap;

    if (acpi_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define acpi_log(fmt, ...)
#endif


static void
acpi_update_irq(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    int sci_level;

    sci_level = (dev->regs.pmsts & dev->regs.pmen) & (RTC_EN | PWRBTN_EN | GBL_EN | TMROF_EN);
    
    if (sci_level) {
	if (dev->regs.irq_mode == 1)
		pci_set_irq(dev->regs.slot, dev->regs.irq_pin);
	else
		picintlevel(1 << 9);
    } else {
	if (dev->regs.irq_mode == 1)
		pci_clear_irq(dev->regs.slot, dev->regs.irq_pin);
	else
		picintc(1 << 9);
    }
}


static void
acpi_raise_smi(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    if (dev->vendor == VEN_VIA) {
	    if ((dev->regs.glbctl & 0x01) && (!dev->regs.smi_lock || !dev->regs.smi_active)) {
		smi_line = 1;
		dev->regs.smi_active = 1;
    	}
    } else {
	if (dev->regs.glbctl & 0x01) {
		smi_line = 1;
		/* Clear bit 16 of GLBCTL. */
		dev->regs.glbctl &= ~0x00010000;
	}
    }
}


static uint32_t
acpi_reg_read_intel(int size, uint16_t addr, void *p)
{
    acpi_t *dev = (acpi_t *) p;
    uint32_t ret = 0x00000000;
    int shift16, shift32;

    addr &= 0x3f;
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
	case 0x00: case 0x01:
		/* PMSTS - Power Management Status Register (IO) */
		ret = (dev->regs.pmsts >> shift16) & 0xff;
		break;
	case 0x02: case 0x03:
		/* PMEN - Power Management Resume Enable Register (IO) */
		ret = (dev->regs.pmen >> shift16) & 0xff;
		break;
	case 0x04: case 0x05:
		/* PMCNTRL - Power Management Control Register (IO) */
		ret = (dev->regs.pmcntrl >> shift16) & 0xff;
		break;
	case 0x08: case 0x09: case 0x0a: case 0x0b:
		/* PMTMR - Power Management Timer Register (IO) */
		ret = (dev->regs.timer_val >> shift32) & 0xff;
		if (cpu_use_dynarec)
			update_tsc();
		break;
	case 0x0c: case 0x0d:
		/* GPSTS - General Purpose Status Register (IO) */
		ret = (dev->regs.gpsts >> shift16) & 0xff;
		break;
	case 0x0e: case 0x0f:
		/* GPEN - General Purpose Enable Register (IO) */
		ret = (dev->regs.gpen >> shift16) & 0xff;
		break;
	case 0x10: case 0x11: case 0x12: case 0x13:
		/* PCNTRL - Processor Control Register (IO) */
		ret = (dev->regs.pcntrl >> shift32) & 0xff;
		break;
	case 0x18: case 0x19:
		/* GLBSTS - Global Status Register (IO) */
		ret = (dev->regs.glbsts >> shift16) & 0xff;
		if (addr == 0x18) {
			ret &= 0x25;
			if (dev->regs.gpsts != 0x0000)
				ret |= 0x80;
			if (dev->regs.pmsts != 0x0000)
				ret |= 0x40;
			if (dev->regs.devsts != 0x00000000)
				ret |= 0x10;
		}
		break;
	case 0x1c: case 0x1d: case 0x1e: case 0x1f:
		/* DEVSTS - Device Status Register (IO) */
		ret = (dev->regs.devsts >> shift32) & 0xff;
		break;
	case 0x20: case 0x21:
		/* GLBEN - Global Enable Register (IO) */
		ret = (dev->regs.glben >> shift16) & 0xff;
		break;
	case 0x28: case 0x29: case 0x2a: case 0x2b:
		/* GLBCTL - Global Control Register (IO) */
		ret = (dev->regs.glbctl >> shift32) & 0xff;
		break;
	case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		/* DEVCTL - Device Control Register (IO) */
		ret = (dev->regs.devctl >> shift32) & 0xff;
		break;
	case 0x30: case 0x31: case 0x32:
		/* GPIREG - General Purpose Input Register (IO) */
		if (size == 1)
			ret = dev->regs.gpireg[addr & 3];
		break;
	case 0x34: case 0x35: case 0x36: case 0x37:
		/* GPOREG - General Purpose Output Register (IO) */
		if (size == 1)
			ret = dev->regs.gporeg[addr & 3];
		break;
    }

    acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
    return ret;
}


static uint32_t
acpi_reg_read_via(int size, uint16_t addr, void *p)
{
    acpi_t *dev = (acpi_t *) p;
    uint32_t ret = 0x00000000;
    int shift16, shift32;

    addr &= 0xff;
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
	case 0x00: case 0x01:
		/* PMSTS - Power Management Status Register (IO) */
		ret = (dev->regs.pmsts >> shift16) & 0xff;
		break;
	case 0x02: case 0x03:
		/* PMEN - Power Management Resume Enable Register (IO) */
		ret = (dev->regs.pmen >> shift16) & 0xff;
		break;
	case 0x04: case 0x05:
		/* PMCNTRL - Power Management Control Register (IO) */
		ret = (dev->regs.pmcntrl >> shift16) & 0xff;
		break;
	case 0x08: case 0x09: case 0x0a: case 0x0b:
		/* PMTMR - Power Management Timer Register (IO) */
		ret = (dev->regs.timer_val >> shift32) & 0xff;
		if (cpu_use_dynarec)
			update_tsc();
		break;
	case 0x10: case 0x11: case 0x12: case 0x13:
		/* PCNTRL - Processor Control Register (IO) */
		ret = (dev->regs.pcntrl >> shift32) & 0xff;
		break;
	case 0x20: case 0x21:
		/* GPSTS - General Purpose Status Register (IO) */
		ret = (dev->regs.gpsts >> shift16) & 0xff;
		break;
	case 0x22: case 0x23:
		/* General Purpose SCI Enable */
		ret = (dev->regs.gpscien >> shift16) & 0xff;
		break;
	case 0x24: case 0x25:
		/* General Purpose SMI Enable */
		ret = (dev->regs.gpsmien >> shift16) & 0xff;
		break;
	case 0x26: case 0x27:
		/* Power Supply Control */
		ret = (dev->regs.pscntrl >> shift16) & 0xff;
		break;
	case 0x28: case 0x29:
		/* GLBSTS - Global Status Register (IO) */
		ret = (dev->regs.glbsts >> shift16) & 0xff;
		break;
	case 0x2a: case 0x2b:
		/* GLBEN - Global Enable Register (IO) */
		ret = (dev->regs.glben >> shift16) & 0xff;
		break;
	case 0x2c: case 0x2d:
		/* GLBCTL - Global Control Register (IO) */
		ret = (dev->regs.glbctl >> shift16) & 0xff;
		ret &= ~0x0110;
		ret |= (dev->regs.smi_lock ? 0x10 : 0x00);
		ret |= (dev->regs.smi_active ? 0x01 : 0x00);
		break;
	case 0x2f:
		/* SMI Command */
		if (size == 1)
			ret = dev->regs.smicmd & 0xff;
		break;
	case 0x30: case 0x31: case 0x32: case 0x33:
		/* Primary Activity Detect Status */
		ret = (dev->regs.padsts >> shift32) & 0xff;
		break;
	case 0x34: case 0x35: case 0x36: case 0x37:
		/* Primary Activity Detect Enable */
		ret = (dev->regs.paden >> shift32) & 0xff;
		break;
	case 0x38: case 0x39: case 0x3a: case 0x3b:
		/* GP Timer Reload Enable */
		ret = (dev->regs.gptren >> shift32) & 0xff;
		break;
	case 0x40:
		/* GPIO Direction Control */
		if (size == 1)
			ret = dev->regs.gpio_dir & 0xff;
		break;
	case 0x42:
		/* GPIO port Output Value */
		if (size == 1)
			ret = dev->regs.gpio_val & 0xff;
		break;
	case 0x44:
		/* GPIO port Output Value */
		if (size == 1)
			ret = dev->regs.extsmi_val & 0xff;
		break;
	case 0x46: case 0x47:
		/* GPO Port Output Value */
		ret = (dev->regs.gpo_val >> shift16) & 0xff;
		break;
	case 0x48: case 0x49:
		/* GPO Port Input Value */
		ret = (dev->regs.gpi_val >> shift16) & 0xff;
		break;
    }

    acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
    return ret;
}


static void
acpi_reg_write_intel(int size, uint16_t addr, uint8_t val, void *p)
{
    acpi_t *dev = (acpi_t *) p;
    int shift16, shift32;
    int sus_typ;

    addr &= 0x3f;
    acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
	case 0x00: case 0x01:
		/* PMSTS - Power Management Status Register (IO) */
		dev->regs.pmsts &= ~((val << shift16) & 0x8d31);
		acpi_update_irq(dev);
		break;
	case 0x02: case 0x03:
		/* PMEN - Power Management Resume Enable Register (IO) */
		dev->regs.pmen = ((dev->regs.pmen & ~(0xff << shift16)) | (val << shift16)) & 0x0521;
		acpi_update_irq(dev);
		break;
	case 0x04: case 0x05:
		/* PMCNTRL - Power Management Control Register (IO) */
		dev->regs.pmcntrl = ((dev->regs.pmcntrl & ~(0xff << shift16)) | (val << shift16)) & 0x3c07;
		/* Setting GBL_RLS also sets BIOS_STS and generates SMI. */
		if ((addr == 0x04) && (dev->regs.pmcntrl & 0x0004)) {
			dev->regs.glbsts |= 0x01;
			if (dev->regs.glben & 0x02)
				acpi_raise_smi(dev);
		}
		if (dev->regs.pmcntrl & 0x2000) {
			sus_typ = (dev->regs.pmcntrl >> 10) & 7;
			switch (sus_typ) {
				case 0:
					/* Soft power off. */
					exit(-1);
					break;
				case 1:
					/* Suspend to RAM. */
					nvr_reg_write(0x000f, 0xff, dev->nvr);

					/* Do a hard reset. */
					device_reset_all_pci();

					cpu_alt_reset = 0;

					pci_reset();
					keyboard_at_reset();

					mem_a20_alt = 0;
					mem_a20_recalc();

					flushmmucache();

					resetx86();
					break;
			}
		}
		break;
	case 0x0c: case 0x0d:
		/* GPSTS - General Purpose Status Register (IO) */
		dev->regs.gpsts &= ~((val << shift16) & 0x0f81);
		break;
	case 0x0e: case 0x0f:
		/* GPEN - General Purpose Enable Register (IO) */
		dev->regs.gpen = ((dev->regs.gpen & ~(0xff << shift16)) | (val << shift16)) & 0x0f01;
		break;
	case 0x10: case 0x11: case 0x12: case 0x13:
		/* PCNTRL - Processor Control Register (IO) */
		dev->regs.pcntrl = ((dev->regs.pcntrl & ~(0xff << shift32)) | (val << shift32)) & 0x00023e1e;
		break;
	case 0x18: case 0x19:
		/* GLBSTS - Global Status Register (IO) */
		dev->regs.glbsts &= ~((val << shift16) & 0x0df7);
		break;
	case 0x1c: case 0x1d: case 0x1e: case 0x1f:
		/* DEVSTS - Device Status Register (IO) */
		dev->regs.devsts &= ~((val << shift32) & 0x3fff0fff);
		break;
	case 0x20: case 0x21:
		/* GLBEN - Global Enable Register (IO) */
		dev->regs.glben = ((dev->regs.glben & ~(0xff << shift16)) | (val << shift16)) & 0x8d1f;
		break;
	case 0x28: case 0x29: case 0x2a: case 0x2b:
		/* GLBCTL - Global Control Register (IO) */
		dev->regs.glbctl = ((dev->regs.glbctl & ~(0xff << shift32)) | (val << shift32)) & 0x0701ff07;
		/* Setting BIOS_RLS also sets GBL_STS and generates SMI. */
		if (dev->regs.glbctl & 0x00000002) {
			dev->regs.pmsts |= 0x20;
			if (dev->regs.pmen & 0x20)
				acpi_update_irq(dev);
		}
		break;
	case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		/* DEVCTL - Device Control Register (IO) */
		dev->regs.devctl = ((dev->regs.devctl & ~(0xff << shift32)) | (val << shift32)) & 0x0fffffff;
		break;
	case 0x34: case 0x35: case 0x36: case 0x37:
		/* GPOREG - General Purpose Output Register (IO) */
		if (size == 1)
			dev->regs.gporeg[addr & 3] = val;
		break;
    }
}


static void
acpi_reg_write_via(int size, uint16_t addr, uint8_t val, void *p)
{
    acpi_t *dev = (acpi_t *) p;
    int shift16, shift32;
    int sus_typ;

    addr &= 0xff;
    acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
	case 0x00: case 0x01:
		/* PMSTS - Power Management Status Register (IO) */
		dev->regs.pmsts &= ~((val << shift16) & 0x8d31);
		acpi_update_irq(dev);
		if ((addr == 0x00) && !(dev->regs.pmsts & 0x20))
			dev->regs.glbctl &= ~0x0002;
		break;
	case 0x02: case 0x03:
		/* PMEN - Power Management Resume Enable Register (IO) */
		dev->regs.pmen = ((dev->regs.pmen & ~(0xff << shift16)) | (val << shift16)) & 0x0521;
		acpi_update_irq(dev);
		break;
	case 0x04: case 0x05:
		/* PMCNTRL - Power Management Control Register (IO) */
		dev->regs.pmcntrl = ((dev->regs.pmcntrl & ~(0xff << shift16)) | (val << shift16)) & 0x3c07;
		/* Setting GBL_RLS also sets BIOS_STS and generates SMI. */
		if ((addr == 0x04) && (dev->regs.pmcntrl & 0x0004)) {
			dev->regs.glbsts |= 0x20;
			if (dev->regs.glben & 0x20)
				acpi_raise_smi(dev);
		}
		if (dev->regs.pmcntrl & 0x2000) {
			sus_typ = (dev->regs.pmcntrl >> 10) & 7;
			switch (sus_typ) {
				case 0:
					/* Soft power off. */
					exit(-1);
					break;
				case 1:
					/* Suspend to RAM. */
					nvr_reg_write(0x000f, 0xff, dev->nvr);

					/* Do a hard reset. */
					device_reset_all_pci();

					cpu_alt_reset = 0;

					pci_reset();
					keyboard_at_reset();

					mem_a20_alt = 0;
					mem_a20_recalc();

					flushmmucache();

					resetx86();
					break;
			}
		}
		break;
	case 0x10: case 0x11: case 0x12: case 0x13:
		/* PCNTRL - Processor Control Register (IO) */
		dev->regs.pcntrl = ((dev->regs.pcntrl & ~(0xff << shift32)) | (val << shift32)) & 0x0000001e;
		break;
	case 0x20: case 0x21:
		/* GPSTS - General Purpose Status Register (IO) */
		dev->regs.gpsts &= ~((val << shift16) & 0x03ff);
		break;
	case 0x22: case 0x23:
		/* General Purpose SCI Enable */
		dev->regs.gpscien = ((dev->regs.gpscien & ~(0xff << shift16)) | (val << shift16)) & 0x03ff;
		break;
	case 0x24: case 0x25:
		/* General Purpose SMI Enable */
		dev->regs.gpsmien = ((dev->regs.gpsmien & ~(0xff << shift16)) | (val << shift16)) & 0x03ff;
		break;
	case 0x26: case 0x27:
		/* Power Supply Control */
		dev->regs.pscntrl = ((dev->regs.pscntrl & ~(0xff << shift16)) | (val << shift16)) & 0x0701;
		break;
	case 0x28: case 0x29:
		/* GLBSTS - Global Status Register (IO) */
		dev->regs.glbsts &= ~((val << shift16) & 0x007f);
		break;
	case 0x2a: case 0x2b:
		/* GLBEN - Global Enable Register (IO) */
		dev->regs.glben = ((dev->regs.glben & ~(0xff << shift16)) | (val << shift16)) & 0x007f;
		break;
	case 0x2c:
		/* GLBCTL - Global Control Register (IO) */
		dev->regs.glbctl = (dev->regs.glbctl & ~0xff) | (val & 0xff);
		dev->regs.smi_lock = !!(dev->regs.glbctl & 0x0010);
		/* Setting BIOS_RLS also sets GBL_STS and generates SMI. */
		if (dev->regs.glbctl & 0x0002) {
			dev->regs.pmsts |= 0x20;
			if (dev->regs.pmen & 0x20)
				acpi_update_irq(dev);
		}
		break;
	case 0x2d:
		/* GLBCTL - Global Control Register (IO) */
		dev->regs.glbctl &= ~((val << 8) & 0x0100);
		if (val & 0x01)
			dev->regs.smi_active = 0;
		break;
	case 0x2f:
		/* SMI Command */
		if (size == 1) {
			dev->regs.smicmd = val & 0xff;
			dev->regs.glbsts |= 0x40;
			if (dev->regs.glben & 0x40)
				acpi_raise_smi(dev);
		}
		break;
	case 0x30: case 0x31: case 0x32: case 0x33:
		/* Primary Activity Detect Status */
		dev->regs.padsts &= ~((val << shift32) & 0x000000fd);
		break;
	case 0x34: case 0x35: case 0x36: case 0x37:
		/* Primary Activity Detect Enable */
		dev->regs.paden = ((dev->regs.paden & ~(0xff << shift32)) | (val << shift32)) & 0x000000fd;
		break;
	case 0x38: case 0x39: case 0x3a: case 0x3b:
		/* GP Timer Reload Enable */
		dev->regs.gptren = ((dev->regs.gptren & ~(0xff << shift32)) | (val << shift32)) & 0x000000d9;
		break;
	case 0x40:
		/* GPIO Direction Control */
		if (size == 1)
			dev->regs.gpio_dir = val & 0xff;
		break;
	case 0x42:
		/* GPIO port Output Value */
		if (size == 1)
			dev->regs.gpio_val = val & 0xff;
		break;
	case 0x46: case 0x47:
		/* GPO Port Output Value */
		dev->regs.gpo_val = ((dev->regs.gpo_val & ~(0xff << shift16)) | (val << shift16)) & 0xffff;
		break;
    }
}


static uint32_t
acpi_reg_read_common(int size, uint16_t addr, void *p)
{
    acpi_t *dev = (acpi_t *) p;
    uint8_t ret = 0xff;

    if (dev->vendor == VEN_VIA)
	ret = acpi_reg_read_via(size, addr, p);
    else
	ret = acpi_reg_read_intel(size, addr, p);

    return ret;
}


static void
acpi_reg_write_common(int size, uint16_t addr, uint8_t val, void *p)
{
    acpi_t *dev = (acpi_t *) p;

    if (dev->vendor == VEN_VIA)
	acpi_reg_write_via(size, addr, val, p);
    else
	acpi_reg_write_intel(size, addr, val, p);
}


static uint32_t
acpi_reg_readl(uint16_t addr, void *p)
{
    uint32_t ret = 0x00000000;

    ret = acpi_reg_read_common(4, addr, p);
    ret |= (acpi_reg_read_common(4, addr + 1, p) << 8);
    ret |= (acpi_reg_read_common(4, addr + 2, p) << 16);
    ret |= (acpi_reg_read_common(4, addr + 3, p) << 24);

    acpi_log("ACPI: Read L %08X from %04X\n", ret, addr);

    return ret;
}


static uint16_t
acpi_reg_readw(uint16_t addr, void *p)
{
    uint16_t ret = 0x0000;

    ret = acpi_reg_read_common(2, addr, p);
    ret |= (acpi_reg_read_common(2, addr + 1, p) << 8);

    acpi_log("ACPI: Read W %08X from %04X\n", ret, addr);

    return ret;
}


static uint8_t
acpi_reg_read(uint16_t addr, void *p)
{
    uint8_t ret = 0x00;

    ret = acpi_reg_read_common(1, addr, p);

    return ret;
}


static void
acpi_reg_writel(uint16_t addr, uint32_t val, void *p)
{
    acpi_reg_write_common(4, addr, val & 0xff, p);
    acpi_reg_write_common(4, addr + 1, (val >> 8) & 0xff, p);
    acpi_reg_write_common(4, addr + 1, (val >> 16) & 0xff, p);
    acpi_reg_write_common(4, addr + 1, (val >> 24) & 0xff, p);
}


static void
acpi_reg_writew(uint16_t addr, uint16_t val, void *p)
{
    acpi_reg_write_common(2, addr, val & 0xff, p);
    acpi_reg_write_common(2, addr + 1, (val >> 8) & 0xff, p);
}


static void
acpi_reg_write(uint16_t addr, uint8_t val, void *p)
{
    acpi_reg_write_common(1, addr, val, p);
}


void
acpi_update_io_mapping(acpi_t *dev, uint32_t base, int chipset_en)
{
    if (dev->regs.io_base != 0x0000) {
	io_removehandler(dev->regs.io_base, 0x40,
			 acpi_reg_read, acpi_reg_readw, acpi_reg_readl,
			 acpi_reg_write, acpi_reg_writew, acpi_reg_writel, dev);
    }

    dev->regs.io_base = base;

    if (chipset_en && (dev->regs.io_base != 0x0000)) {
	io_sethandler(dev->regs.io_base, 0x40,
		      acpi_reg_read, acpi_reg_readw, acpi_reg_readl,
		      acpi_reg_write, acpi_reg_writew, acpi_reg_writel, dev);
    }
}


static void
acpi_timer_count(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    int overflow;
    uint32_t old;

    old = dev->regs.timer_val;
    dev->regs.timer_val++;

    if (dev->regs.timer32)
	overflow = (old ^ dev->regs.timer_val) & 0x80000000;
    else {
	dev->regs.timer_val &= 0x00ffffff;
	overflow = (old ^ dev->regs.timer_val) & 0x00800000;
    }

    if (overflow) {
	dev->regs.pmsts |= TMROF_EN;
	acpi_update_irq(dev);
    }

    timer_advance_u64(&dev->timer, ACPICONST);
}


void
acpi_init_gporeg(acpi_t *dev, uint8_t val0, uint8_t val1, uint8_t val2, uint8_t val3)
{
    dev->regs.gporeg[0] = dev->gporeg_default[0] = val0;
    dev->regs.gporeg[1] = dev->gporeg_default[1] = val1;
    dev->regs.gporeg[2] = dev->gporeg_default[2] = val2;
    dev->regs.gporeg[3] = dev->gporeg_default[3] = val3;
    acpi_log("acpi_init_gporeg(): %02X %02X %02X %02X\n", dev->regs.gporeg[0], dev->regs.gporeg[1], dev->regs.gporeg[2], dev->regs.gporeg[3]);
}


void
acpi_set_timer32(acpi_t *dev, uint8_t timer32)
{
    dev->regs.timer32 = timer32;

    if (!dev->regs.timer32)
	dev->regs.timer_val &= 0x00ffffff;
}


void
acpi_set_slot(acpi_t *dev, int slot)
{
    dev->regs.slot = slot;
}


void
acpi_set_irq_mode(acpi_t *dev, int irq_mode)
{
    dev->regs.irq_mode = irq_mode;
}


void
acpi_set_irq_pin(acpi_t *dev, int irq_pin)
{
    dev->regs.irq_pin = irq_pin;
}


void
acpi_set_nvr(acpi_t *dev, nvr_t *nvr)
{
    dev->nvr = nvr;
}


static void
acpi_apm_out(uint16_t port, uint8_t val, void *p)
{
    acpi_t *dev = (acpi_t *) p;

    acpi_log("[%04X:%08X] APM write: %04X = %02X (BX = %04X, CX = %04X)\n", CS, cpu_state.pc, port, val, BX, CX);

    port &= 0x0001;

    if (port == 0x0000) {
	dev->apm->cmd = val;
	if (dev->apm->do_smi) {
		if (dev->vendor == VEN_INTEL)
			dev->regs.glbsts |= 0x20;
		acpi_raise_smi(dev);
	}
    } else
	dev->apm->stat = val;
}


static uint8_t
acpi_apm_in(uint16_t port, void *p)
{
    acpi_t *dev = (acpi_t *) p;
    uint8_t ret = 0xff;

    port &= 0x0001;

    if (port == 0x0000)
	ret = dev->apm->cmd;
    else
	ret = dev->apm->stat;

    acpi_log("[%04X:%08X] APM read: %04X = %02X\n", CS, cpu_state.pc, port, ret);

    return ret;
}


static void
acpi_reset(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    int i;

    memset(&dev->regs, 0x00, sizeof(acpi_regs_t));
    for (i = 0; i < 4; i++)
	dev->regs.gporeg[i] = dev->gporeg_default[i];
}


static void
acpi_speed_changed(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    timer_disable(&dev->timer);
    timer_set_delay_u64(&dev->timer, ACPICONST);
}


static void
acpi_close(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    timer_disable(&dev->timer);

    free(dev);
}


static void *
acpi_init(const device_t *info)
{
    acpi_t *dev;

    dev = (acpi_t *)malloc(sizeof(acpi_t));
    if (dev == NULL) return(NULL);
    memset(dev, 0x00, sizeof(acpi_t));

    dev->vendor = info->local;

    if (dev->vendor == VEN_INTEL) {
	dev->apm = device_add(&apm_pci_acpi_device);
	io_sethandler(0x00b2, 0x0002, acpi_apm_in, NULL, NULL, acpi_apm_out, NULL, NULL, dev);
    }

    timer_add(&dev->timer, acpi_timer_count, dev, 0);
    timer_set_delay_u64(&dev->timer, ACPICONST);

    dev->regs.gpireg[0] = dev->regs.gpireg[1] = dev->regs.gpireg[2] = 0xff;

    return dev;
}


const device_t acpi_intel_device =
{
    "ACPI v1.0",
    DEVICE_PCI,
    VEN_INTEL,
    acpi_init, 
    acpi_close, 
    acpi_reset,
    NULL,
    acpi_speed_changed,
    NULL,
    NULL
};


const device_t acpi_via_device =
{
    "ACPI v1.2",
    DEVICE_PCI,
    VEN_VIA,
    acpi_init, 
    acpi_close, 
    acpi_reset,
    NULL,
    acpi_speed_changed,
    NULL,
    NULL
};
