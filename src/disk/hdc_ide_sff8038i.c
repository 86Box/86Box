/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the SFF-8038i IDE Bus Master.
 *
 *		PRD format :
 *		    word 0 - base address
 *		    word 1 - bits 1-15 = byte count, bit 31 = end of transfer
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/scsi_device.h>
#include <86box/scsi_cdrom.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/zip.h>
#include <86box/mo.h>


static int	next_id = 0;


static uint8_t	sff_bus_master_read(uint16_t port, void *priv);
static uint16_t	sff_bus_master_readw(uint16_t port, void *priv);
static uint32_t	sff_bus_master_readl(uint16_t port, void *priv);
static void	sff_bus_master_write(uint16_t port, uint8_t val, void *priv);
static void	sff_bus_master_writew(uint16_t port, uint16_t val, void *priv);
static void	sff_bus_master_writel(uint16_t port, uint32_t val, void *priv);


#ifdef ENABLE_SFF_LOG
int sff_do_log = ENABLE_SFF_LOG;


static void
sff_log(const char *fmt, ...)
{
    va_list ap;

    if (sff_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define sff_log(fmt, ...)
#endif


void
sff_bus_master_handler(sff8038i_t *dev, int enabled, uint16_t base)
{
    if (dev->base != 0x0000) {
	io_removehandler(dev->base, 0x08,
			 sff_bus_master_read, sff_bus_master_readw, sff_bus_master_readl,
			 sff_bus_master_write, sff_bus_master_writew, sff_bus_master_writel,
			 dev);
    }

    if (enabled && (base != 0x0000)) {
	io_sethandler(base, 0x08,
		      sff_bus_master_read, sff_bus_master_readw, sff_bus_master_readl,
		      sff_bus_master_write, sff_bus_master_writew, sff_bus_master_writel,
		      dev);
    }

    dev->enabled = enabled;
    dev->base = base;
}


static void
sff_bus_master_next_addr(sff8038i_t *dev)
{
    dma_bm_read(dev->ptr_cur, (uint8_t *)&(dev->addr), 4, 4);
    dma_bm_read(dev->ptr_cur + 4, (uint8_t *)&(dev->count), 4, 4);
    sff_log("SFF-8038i Bus master DWORDs: %08X %08X\n", dev->addr, dev->count);
    dev->eot = dev->count >> 31;
    dev->count &= 0xfffe;
    if (!dev->count)
	dev->count = 65536;
    dev->addr &= 0xfffffffe;
    dev->ptr_cur += 8;
}


static void
sff_bus_master_write(uint16_t port, uint8_t val, void *priv)
{
    sff8038i_t *dev = (sff8038i_t *) priv;
#ifdef ENABLE_SFF_LOG
    int channel = (port & 8) ? 1 : 0;
#endif

    sff_log("SFF-8038i Bus master BYTE  write: %04X       %02X\n", port, val);

    switch (port & 7) {
	case 0:
		sff_log("sff Cmd   : val = %02X, old = %02X\n", val, dev->command);
		if ((val & 1) && !(dev->command & 1)) {	/*Start*/
			sff_log("sff Bus Master start on channel %i\n", channel);
			dev->ptr_cur = dev->ptr;
			sff_bus_master_next_addr(dev);
			dev->status |= 1;
		}
		if (!(val & 1) && (dev->command & 1)) {	/*Stop*/
			sff_log("sff Bus Master stop on channel %i\n", channel);
			dev->status &= ~1;
		}

		dev->command = val;
		break;
	case 2:
		sff_log("sff Status: val = %02X, old = %02X\n", val, dev->status);
		dev->status &= 0x07;
		dev->status |= (val & 0x60);
		if (val & 0x04)
			dev->status &= ~0x04;
		if (val & 0x02)
			dev->status &= ~0x02;
		break;
	case 4:
		dev->ptr = (dev->ptr & 0xffffff00) | (val & 0xfc);
		dev->ptr %= (mem_size * 1024);
		dev->ptr0 = val;
		break;
	case 5:
		dev->ptr = (dev->ptr & 0xffff00fc) | (val << 8);
		dev->ptr %= (mem_size * 1024);
		break;
	case 6:
		dev->ptr = (dev->ptr & 0xff00fffc) | (val << 16);
		dev->ptr %= (mem_size * 1024);
		break;
	case 7:
		dev->ptr = (dev->ptr & 0x00fffffc) | (val << 24);
		dev->ptr %= (mem_size * 1024);
		break;
    }
}


static void
sff_bus_master_writew(uint16_t port, uint16_t val, void *priv)
{
    sff8038i_t *dev = (sff8038i_t *) priv;

    sff_log("SFF-8038i Bus master WORD  write: %04X     %04X\n", port, val);

    switch (port & 7) {
	case 0:
	case 2:
		sff_bus_master_write(port, val & 0xff, priv);
		break;
	case 4:
                dev->ptr = (dev->ptr & 0xffff0000) | (val & 0xfffc);
		dev->ptr %= (mem_size * 1024);
		dev->ptr0 = val & 0xff;
                break;
	case 6:
		dev->ptr = (dev->ptr & 0x0000fffc) | (val << 16);
		dev->ptr %= (mem_size * 1024);
		break;
    }
}


static void
sff_bus_master_writel(uint16_t port, uint32_t val, void *priv)
{
    sff8038i_t *dev = (sff8038i_t *) priv;

    sff_log("SFF-8038i Bus master DWORD write: %04X %08X\n", port, val);

    switch (port & 7) {
	case 0:
	case 2:
		sff_bus_master_write(port, val & 0xff, priv);
		break;
	case 4:
                dev->ptr = (val & 0xfffffffc);
		dev->ptr %= (mem_size * 1024);
		dev->ptr0 = val & 0xff;
                break;
    }
}


static uint8_t
sff_bus_master_read(uint16_t port, void *priv)
{
    sff8038i_t *dev = (sff8038i_t *) priv;

    uint8_t ret = 0xff;

    switch (port & 7) {
	case 0:
		ret = dev->command;
		break;
	case 2:
		ret = dev->status & 0x67;
		break;
	case 4:
		ret = dev->ptr0;
		break;
	case 5:
		ret = dev->ptr >> 8;
		break;
	case 6:
		ret = dev->ptr >> 16;
		break;
	case 7:
		ret = dev->ptr >> 24;
		break;
    }

    sff_log("SFF-8038i Bus master BYTE  read : %04X       %02X\n", port, ret);

    return ret;
}


static uint16_t
sff_bus_master_readw(uint16_t port, void *priv)
{
    sff8038i_t *dev = (sff8038i_t *) priv;

    uint16_t ret = 0xffff;

    switch (port & 7) {
	case 0:
	case 2:
		ret = (uint16_t) sff_bus_master_read(port, priv);
		break;
	case 4:
		ret = dev->ptr0 | (dev->ptr & 0xff00);
		break;
	case 6:
		ret = dev->ptr >> 16;
		break;
    }

    sff_log("SFF-8038i Bus master WORD  read : %04X     %04X\n", port, ret);

    return ret;
}


static uint32_t
sff_bus_master_readl(uint16_t port, void *priv)
{
    sff8038i_t *dev = (sff8038i_t *) priv;

    uint32_t ret = 0xffffffff;

    switch (port & 7) {
	case 0:
	case 2:
		ret = (uint32_t) sff_bus_master_read(port, priv);
		break;
	case 4:
		ret = dev->ptr0 | (dev->ptr & 0xffffff00);
		break;
    }

    sff_log("sff Bus master DWORD read : %04X %08X\n", port, ret);

    return ret;
}


static int
sff_bus_master_dma(int channel, uint8_t *data, int transfer_length, int out, void *priv)
{
    sff8038i_t *dev = (sff8038i_t *) priv;
#ifdef ENABLE_SFF_LOG
    char *sop;
#endif

    int force_end = 0, buffer_pos = 0;

#ifdef ENABLE_SFF_LOG
    sop = out ? "Read" : "Writ";
#endif

    if (!(dev->status & 1))
	return 2;                                    /*DMA disabled*/

    sff_log("SFF-8038i Bus master %s: %i bytes\n", out ? "write" : "read", transfer_length);

    while (1) {
	if (dev->count <= transfer_length) {
		sff_log("%sing %i bytes to %08X\n", sop, dev->count, dev->addr);
		if (out)
			dma_bm_read(dev->addr, (uint8_t *)(data + buffer_pos), dev->count, 4);
		else
			dma_bm_write(dev->addr, (uint8_t *)(data + buffer_pos), dev->count, 4);
		transfer_length -= dev->count;
		buffer_pos += dev->count;
	} else {
		sff_log("%sing %i bytes to %08X\n", sop, transfer_length, dev->addr);
		if (out)
			dma_bm_read(dev->addr, (uint8_t *)(data + buffer_pos), transfer_length, 4);
		else
			dma_bm_write(dev->addr, (uint8_t *)(data + buffer_pos), transfer_length, 4);
		/* Increase addr and decrease count so that resumed transfers do not mess up. */
		dev->addr += transfer_length;
		dev->count -= transfer_length;
		transfer_length = 0;
		force_end = 1;
	}

	if (force_end) {
		sff_log("Total transfer length smaller than sum of all blocks, partial block\n");
		dev->status &= ~2;
		return 1;		/* This block has exhausted the data to transfer and it was smaller than the count, break. */
	} else {
		if (!transfer_length && !dev->eot) {
			sff_log("Total transfer length smaller than sum of all blocks, full block\n");
			dev->status &= ~2;
			return 1;	/* We have exhausted the data to transfer but there's more blocks left, break. */
		} else if (transfer_length && dev->eot) {
			sff_log("Total transfer length greater than sum of all blocks\n");
			dev->status |= 2;
			return 0;	/* There is data left to transfer but we have reached EOT - return with error. */
		} else if (dev->eot) {
			sff_log("Regular EOT\n");
			dev->status &= ~3;
			return 1;	/* We have regularly reached EOT - clear status and break. */
		} else {
			/* We have more to transfer and there are blocks left, get next block. */
			sff_bus_master_next_addr(dev);
		}
	}
    }

    return 1;
}


void
sff_bus_master_set_irq(int channel, void *priv)
{
    sff8038i_t *dev = (sff8038i_t *) priv;
    uint8_t irq = !!(channel & 0x40);

    if (!(dev->status & 0x04) || (channel & 0x40)) {
	dev->status &= ~4;
	dev->status |= (channel >> 4);
    }

    channel &= 0x01;
    if (irq) {
	sff_log("SFF8038i: Channel %i IRQ raise\n", channel);
	if (dev->irq_mode[channel] == 3)
		picintlevel(1 << dev->irq_line);
	else if ((dev->irq_mode[channel] == 2) && channel && pci_use_mirq(0))
		pci_set_mirq(0, 0);
	else if (dev->irq_mode[channel] == 1)
		pci_set_irq(dev->slot, dev->irq_pin);
	else
		picint(1 << (14 + channel));
    } else {
	sff_log("SFF8038i: Channel %i IRQ lower\n", channel);
	if (dev->irq_mode[channel] == 3)
		picintc(1 << dev->irq_line);
	else if ((dev->irq_mode[channel] == 2) && channel && pci_use_mirq(0))
		pci_clear_mirq(0, 0);
	else if (dev->irq_mode[channel] == 1)
		pci_clear_irq(dev->slot, dev->irq_pin);
	else
		picintc(1 << (14 + channel));
    }
}


void
sff_bus_master_reset(sff8038i_t *dev, uint16_t old_base)
{
    if (dev->enabled) {
	io_removehandler(old_base, 0x08,
			 sff_bus_master_read, sff_bus_master_readw, sff_bus_master_readl,
			 sff_bus_master_write, sff_bus_master_writew, sff_bus_master_writel,
			 dev);

	dev->enabled = 0;
    }

    dev->command = 0x00;
    dev->status = 0x00;
    dev->ptr = dev->ptr_cur = 0x00000000;
    dev->addr = 0x00000000;
    dev->ptr0 = 0x00;
    dev->count = dev->eot = 0x00000000;

    ide_pri_disable();
    ide_sec_disable();
}


static void
sff_reset(void *p)
{
    int i = 0;

#ifdef ENABLE_SFF_LOG
    sff_log("SFF8038i: Reset\n");
#endif

    for (i = 0; i < CDROM_NUM; i++) {
	if ((cdrom[i].bus_type == CDROM_BUS_ATAPI) &&
	    (cdrom[i].ide_channel < 4) && cdrom[i].priv)
		scsi_cdrom_reset((scsi_common_t *) cdrom[i].priv);
    }
    for (i = 0; i < ZIP_NUM; i++) {
	if ((zip_drives[i].bus_type == ZIP_BUS_ATAPI) &&
	    (zip_drives[i].ide_channel < 4) && zip_drives[i].priv)
		zip_reset((scsi_common_t *) zip_drives[i].priv);
    }
	for (i = 0; i < MO_NUM; i++) {
	if ((mo_drives[i].bus_type == MO_BUS_ATAPI) &&
	    (mo_drives[i].ide_channel < 4) && mo_drives[i].priv)
		mo_reset((scsi_common_t *) mo_drives[i].priv);
	}

    sff_bus_master_set_irq(0x00, p);
    sff_bus_master_set_irq(0x01, p);
}


void
sff_set_slot(sff8038i_t *dev, int slot)
{
    dev->slot = slot;
}


void
sff_set_irq_line(sff8038i_t *dev, int irq_line)
{
    dev->irq_line = irq_line;
}


void
sff_set_irq_mode(sff8038i_t *dev, int channel, int irq_mode)
{
    dev->irq_mode[channel] = irq_mode;
}


void
sff_set_irq_pin(sff8038i_t *dev, int irq_pin)
{
    dev->irq_pin = irq_pin;
}


static void
sff_close(void *p)
{
    sff8038i_t *dev = (sff8038i_t *)p;

    free(dev);

    next_id--;
    if (next_id < 0)
	next_id = 0;
}


static void
*sff_init(const device_t *info)
{
    sff8038i_t *dev = (sff8038i_t *) malloc(sizeof(sff8038i_t));
    memset(dev, 0, sizeof(sff8038i_t));

    /* Make sure to only add IDE once. */
    if (next_id == 0)
	device_add(&ide_pci_2ch_device);

    ide_set_bus_master(next_id, sff_bus_master_dma, sff_bus_master_set_irq, dev);

    dev->slot = 7;
    dev->irq_mode[0] = dev->irq_mode[1] = 2;
    dev->irq_pin = PCI_INTA;
    dev->irq_line = 14;

    next_id++;

    return dev;
}


const device_t sff8038i_device =
{
    "SFF-8038i IDE Bus Master",
    DEVICE_PCI,
    0,
    sff_init, 
    sff_close, 
    sff_reset,
    { NULL },
    NULL,
    NULL,
    NULL
};
