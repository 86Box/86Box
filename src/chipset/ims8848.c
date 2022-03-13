/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the IMS 8848/8849 chipset.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Tiseno100,
 *
 *		Copyright 2021 Miran Grca.
 *		Copyright 2021 Tiseno100.
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
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/chipset.h>


/*
    IMS 884x Configuration Registers

    Note: IMS 884x are rebadged ATMEL AT 40411/40412 chipsets

    By: Tiseno100, Miran Grca(OBattler)

    Register 00h:
	Bit 3: F0000-FFFFF Shadow Enable
	Bit 2: E0000-EFFFF Shadow Enable
	Bit 0: ????

    Register 04h:
	Bit 3: Cache Write Hit Wait State
	Bit 2: Cache Read Hit Wait State

    Register 06h:
	Bit 3: System BIOS Cacheable (1: Yes / 0: No)
	Bit 1: Power Management Mode (1: IRQ / 0: SMI#)

    Register 08h:
	Bit 2: System BIOS Shadow Write (1: Enable / 0: Disable)
	Bit 1: System BIOS Shadow Read?

    Register 0Dh:
	Bit 0: IO 100H-3FFH Idle Detect (1: Enable / 0: Disable)

    Register 0Eh:
	Bit 7: DMA & Local Bus Idle Detect (1: Enable / 0: Disable)
	Bit 6: Floppy Disk Idle Detect (1: Enable / 0: Disable)
	Bit 5: IDE Idle Detect (1: Enable / 0: Disable)
	Bit 4: Serial Port Idle Detect (1: Enable / 0: Disable)
	Bit 3: Parallel Port Idle Detect (1: Enable / 0: Disable)
	Bit 2: Keyboard Idle Detect (1: Enable / 0: Disable)
	Bit 1: Video Idle Detect (1: Enable / 0: Disable)

    Register 12h:
	Bits 3-2: Power Saving Timer (00 = 1 MIN, 01 = 3 MIN, 10 = 5 MIN, 11 = 8 MIN)
	Bit 1: Base Memory (1: 512KB / 0: 640KB)

    Register 1Ah:
	Bit 3: Cache Write Hit W/S For PCI (1: Enabled / 0: Disable)
	Bit 2: Cache Read Hit W/S For PCI (1: Enabled / 0: Disable)
	Bit 1: VESA Clock Skew (1: 4ns/6ns, 0: No Delay/2ns)

    Register 1Bh:
	Bit 6: Enable SMRAM (always at 30000-4FFFF) in SMM
	Bit 5: ????
	Bit 4: Software SMI#
	Bit 3: DC000-DFFFF Shadow Enable
	Bit 2: D8000-DBFFF Shadow Enable
	Bit 1: D4000-D7FFF Shadow Enable
	Bit 0: D0000-D3FFF Shadow Enable

    Register 1Ch:
	Bits 7-4: INTA IRQ routing (0 = disabled, 1 to F = IRQ)
	Bit 3: CC000-CFFFF Shadow Enable
	Bit 2: C8000-CBFFF Shadow Enable
	Bit 1: C4000-C7FFF Shadow Enable
	Bit 0: C0000-C3FFF Shadow Enable

    Register 1Dh:
	Bits 7-4: INTB IRQ routing (0 = disabled, 1 to F = IRQ)

    Register 1Eh:
	Bits 7-4: INTC IRQ routing (0 = disabled, 1 to F = IRQ)
	Bit 1: C4000-C7FFF Cacheable
	Bit 0: C0000-C3FFF Cacheable

    Register 21h:
	Bits 7-4: INTD IRQ routing (0 = disabled, 1 to F = IRQ)

    Register 22h:
	Bit 5: Local Bus Master #2 select (0 = VESA, 1 = PCI)
	Bit 4: Local Bus Master #1 select (0 = VESA, 1 = PCI)
	Bits 1-0: Internal HADS# Delay Always (00 = No Delay, 01 = 1 Clk, 10 = 2 Clks)

    Register 23h:
	Bit 7: Seven Bits Tag (1: Enabled / 0: Disable)
	Bit 3: Extend LBRDY#(VL Master) (1: Enabled / 0: Disable)
	Bit 2: Sync LRDY#(VL Slave) (1: Enabled / 0: Disable)
	Bit 0: HADS# Delay After LB. Cycle (1: Enabled / 0: Disable)
*/

typedef struct
{
    uint8_t	idx, access_data,
		regs[256], pci_conf[256];

    smram_t	*smram;
} ims8848_t;


#ifdef ENABLE_IMS8848_LOG
int ims8848_do_log = ENABLE_IMS8848_LOG;


static void
ims8848_log(const char *fmt, ...)
{
    va_list ap;

    if (ims8848_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ims8848_log(fmt, ...)
#endif


/* Shadow write always enabled, 1B and 1C control C000-DFFF read. */
static void
ims8848_recalc(ims8848_t *dev)
{
    int i, state_on;
    uint32_t base;
    ims8848_log("SHADOW: 00 = %02X, 08 = %02X, 1B = %02X, 1C = %02X\n",
		dev->regs[0x00], dev->regs[0x08], dev->regs[0x1b], dev->regs[0x1c]);

    state_on = MEM_READ_INTERNAL;
    state_on |= (dev->regs[0x08] & 0x04) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;

    for (i = 0; i < 2; i++) {
	base = 0xe0000 + (i << 16);
	if (dev->regs[0x00] & (1 << (i + 2)))
		mem_set_mem_state_both(base, 0x10000, state_on);
	else
		mem_set_mem_state_both(base, 0x10000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
    }

    for (i = 0; i < 4; i++) {
	base = 0xc0000 + (i << 14);
	if (dev->regs[0x1c] & (1 << i))
		mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	else
		mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);

	base = 0xd0000 + (i << 14);
	if (dev->regs[0x1b] & (1 << i))
		mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	else
		mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
    }

    flushmmucache_nopc();
}


static void
ims8848_base_memory(ims8848_t *dev)
{
    /* We can use the proper mem_set_access to handle that. */
    mem_set_mem_state_both(0x80000, 0x20000, (dev->regs[0x12] & 2) ?
			   (MEM_READ_DISABLED | MEM_WRITE_DISABLED) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));
}


static void
ims8848_smram(ims8848_t *dev)
{
    smram_disable_all();

    smram_enable(dev->smram, 0x00030000, 0x00030000, 0x20000, dev->regs[0x1b] & 0x40, 1);
}


static void
ims8848_write(uint16_t addr, uint8_t val, void *priv)
{
    ims8848_t *dev = (ims8848_t *) priv;
    uint8_t old = dev->regs[dev->idx];

    switch (addr) {
	case 0x22:
		ims8848_log("[W]     IDX    = %02X\n", val);
		dev->idx = val;
		break;
	case 0x23:
		ims8848_log("[W]     IDX IN = %02X\n", val);
		if (((val & 0x0f) == ((dev->idx >> 4) & 0x0f)) && ((val & 0xf0) == ((dev->idx << 4) & 0xf0)))
			dev->access_data = 1;
		break;
	case 0x24:
		ims8848_log("[W] [%i] REG %02X = %02X\n", dev->access_data, dev->idx, val);
		if (dev->access_data) {
			dev->regs[dev->idx] = val;
			switch (dev->idx) {
				case 0x00: case 0x08: case 0x1b: case 0x1c:
					/* Shadow RAM */
					ims8848_recalc(dev);
					if (dev->idx == 0x1b) {
						ims8848_smram(dev);
						if (!(old & 0x10) && (val & 0x10))
							smi_line = 1;
					} else if (dev->idx == 0x1c)
						pci_set_irq_routing(PCI_INTA, (val >> 4) ? (val >> 4) : PCI_IRQ_DISABLED);
					break;

				case 0x1d: case 0x1e:
					pci_set_irq_routing(PCI_INTB + (dev->idx - 0x1d), (val >> 4) ? (val >> 4) : PCI_IRQ_DISABLED);
					break;
				case 0x21:
					pci_set_irq_routing(PCI_INTD, (val >> 4) ? (val >> 4) : PCI_IRQ_DISABLED);
					break;

				case 0x12:
					/* Base Memory */
					ims8848_base_memory(dev);
					break;
			}
			dev->access_data = 0;
		}
		break;
    }
}


static uint8_t
ims8848_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    ims8848_t *dev = (ims8848_t *) priv;
#ifdef ENABLE_IMS8848_LOG
    uint8_t old_ad = dev->access_data;
#endif

    switch (addr) {
	case 0x22:
		ims8848_log("[R]     IDX    = %02X\n", ret);
                ret = dev->idx;
		break;
	case 0x23:
		ims8848_log("[R]     IDX IN = %02X\n", ret);
                ret = (dev->idx >> 4) | (dev->idx << 4);
		break;
	case 0x24:
		if (dev->access_data) {
			ret = dev->regs[dev->idx];
			dev->access_data = 0;
		}
		ims8848_log("[R] [%i] REG %02X = %02X\n", old_ad, dev->idx, ret);
		break;
    }

    return ret;
}


static void
ims8849_pci_write(int func, int addr, uint8_t val, void *priv)
{
    ims8848_t *dev = (ims8848_t *)priv;

    ims8848_log("IMS 884x-PCI: dev->regs[%02x] = %02x POST: %02x\n", addr, val, inb(0x80));

    if (func == 0)  switch (addr) {
	case 0x04:
		dev->pci_conf[addr] = val;
		break;

	case 0x05:
		dev->pci_conf[addr] = val & 3;
		break;

	case 0x07:
		dev->pci_conf[addr] &= val & 0xf7;
		break;

	case 0x0c ... 0x0d:
		dev->pci_conf[addr] = val;
		break;

	case 0x52 ... 0x55:
		dev->pci_conf[addr] = val;
		break;
    }
}


static uint8_t
ims8849_pci_read(int func, int addr, void *priv)
{
    ims8848_t *dev = (ims8848_t *)priv;
    uint8_t ret = 0xff;

    if (func == 0)
	ret = dev->pci_conf[addr];

    return ret;
}


static void
ims8848_reset(void *priv)
{
    ims8848_t *dev = (ims8848_t *)priv;

    memset(dev->regs, 0x00, sizeof(dev->regs));
    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf));

    dev->pci_conf[0x00] = 0xe0; /* Integrated Micro Solutions (IMS) */
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x49; /* 8849 */
    dev->pci_conf[0x03] = 0x88;

    dev->pci_conf[0x04] = 0x07;
    dev->pci_conf[0x07] = 0x02;

    dev->pci_conf[0x0b] = 0x06;

    ims8848_recalc(dev); /* Shadow RAM Setup */
    ims8848_base_memory(dev); /* Base Memory Setup */

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    ims8848_smram(dev);
}


static void
ims8848_close(void *priv)
{
    ims8848_t *dev = (ims8848_t *) priv;

    smram_del(dev->smram);

    free(dev);
}


static void *
ims8848_init(const device_t *info)
{
    ims8848_t *dev = (ims8848_t *) malloc(sizeof(ims8848_t));
    memset(dev, 0, sizeof(ims8848_t));

    device_add(&port_92_device);

    /* IMS 8848:
		22h Index
		23h Data Unlock
		24h Data

       IMS 8849:
		PCI Device 0: IMS 8849 Dummy for compatibility reasons
    */
    io_sethandler(0x0022, 0x0003, ims8848_read, NULL, NULL, ims8848_write, NULL, NULL, dev);
    pci_add_card(PCI_ADD_NORTHBRIDGE, ims8849_pci_read, ims8849_pci_write, dev);

    dev->smram = smram_add();
    smram_set_separate_smram(1);

    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();

    ims8848_reset(dev);

    return dev;
}

const device_t ims8848_device = {
    .name = "IMS 8848/8849",
    .internal_name = "ims8848",
    .flags = 0,
    .local = 0,
    .init = ims8848_init,
    .close = ims8848_close,
    .reset = ims8848_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
