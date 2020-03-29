/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of Intel System I/O PCI chip.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/apm.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/port_92.h>
#include <86box/machine.h>
#include <86box/intel_sio.h>


typedef struct
{
    uint8_t	id,
		regs[256];

    uint16_t	timer_base,
		timer_latch;

    pc_timer_t	timer;

    port_92_t *	port_92;
} sio_t;


static void
sio_timer_write(uint16_t addr, uint8_t val, void *priv)
{
    sio_t *dev = (sio_t *) priv;

    if (!(addr & 0x0002)) {
	if (addr & 0x0001)
		dev->timer_latch = (dev->timer_latch & 0xff) | (val << 8);
	else
		dev->timer_latch = (dev->timer_latch & 0xff00) | val;

	timer_set_delay_u64(&dev->timer, ((uint64_t) dev->timer_latch) * TIMER_USEC);
    }
}


static void
sio_timer_writew(uint16_t addr, uint16_t val, void *priv)
{
    sio_t *dev = (sio_t *) priv;

    if (!(addr & 0x0002)) {
	dev->timer_latch = val;

	timer_set_delay_u64(&dev->timer, ((uint64_t) dev->timer_latch) * TIMER_USEC);
    }
}


static uint8_t
sio_timer_read(uint16_t addr, void *priv)
{
    sio_t *dev = (sio_t *) priv;
    uint16_t sio_timer_latch;
    uint8_t ret = 0xff;

    if (!(addr & 0x0002)) {
	sub_cycles((int)(PITCONST >> 32));

	sio_timer_latch = timer_get_remaining_us(&dev->timer);

	if (addr & 0x0001)
		ret = sio_timer_latch >> 8;
	else
		ret = sio_timer_latch & 0xff;
    }

    return ret;
}


static uint16_t
sio_timer_readw(uint16_t addr, void *priv)
{
    sio_t *dev = (sio_t *) priv;
    uint16_t ret = 0xffff;

    if (!(addr & 0x0002)) {
	sub_cycles((int)(PITCONST >> 32));

	ret = timer_get_remaining_us(&dev->timer);
    }

    return ret;
}


static void
sio_write(int func, int addr, uint8_t val, void *priv)
{
    sio_t *dev = (sio_t *) priv;
    uint8_t old;

    if (func > 0)
	return;

    if (((addr >= 0x0f) && (addr < 0x4c)) && (addr != 0x40))
	return;

    /* The IB (original) variant of the SIO has no PCI IRQ steering. */
    if ((addr >= 0x60) && (addr <= 0x63) && (dev->id < 0x03))
	return;

    old = dev->regs[addr];
    dev->regs[addr] = val;

    switch (addr) {
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x08: case 0x09: case 0x0a: case 0x0b:
	case 0x0e:
		return;

	case 0x04: /*Command register*/
		val &= 0x08;
		val |= 0x07;
		break;
	case 0x05:
		val = 0;
		break;

	case 0x06: /*Status*/
		val = 0;
		break;
	case 0x07:
		val = 0x02;
		break;

	case 0x40:
		if (!((val ^ old) & 0x40))
			return;

		dma_alias_remove();
		if (val & 0x40)
			dma_alias_set();
		break;

	case 0x4f:
		if (!((val ^ old) & 0x40))
			return;

		port_92_remove(dev->port_92);
		if (val & 0x40)
			port_92_add(dev->port_92);
		break;

	case 0x60:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
		else
			pci_set_irq_routing(PCI_INTA, val & 0xf);
		break;
	case 0x61:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
		else
			pci_set_irq_routing(PCI_INTC, val & 0xf);
		break;
	case 0x62:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
		else
			pci_set_irq_routing(PCI_INTB, val & 0xf);
		break;
	case 0x63:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
		else
			pci_set_irq_routing(PCI_INTD, val & 0xf);
		break;

	case 0x80:
	case 0x81:
		if (dev->timer_base & 0x01) {
			io_removehandler(dev->timer_base & 0xfffc, 0x0004,
					 sio_timer_read, sio_timer_readw, NULL,
					 sio_timer_write, sio_timer_writew, NULL, dev);
		}
		dev->timer_base = (dev->regs[0x81] << 8) | (dev->regs[0x80] & 0xfd);
		if (dev->timer_base & 0x01) {
			io_sethandler(dev->timer_base & 0xfffc, 0x0004,
				      sio_timer_read, sio_timer_readw, NULL,
				      sio_timer_write, sio_timer_writew, NULL, dev);
		}
		break;
    }
}


static uint8_t
sio_read(int func, int addr, void *priv)
{
    sio_t *dev = (sio_t *) priv;
    uint8_t ret;

    ret = 0xff;

    if (func == 0)
        ret = dev->regs[addr];

    return ret;
}


static void
sio_config_write(uint16_t addr, uint8_t val, void *priv)
{
}


static uint8_t
sio_config_read(uint16_t port, void *priv)
{
    uint8_t ret = 0x00;

    switch (port & 0x000f) {
	case 3:
		ret = 0xff;
		break;
	case 5:
		ret = 0xd3;

		switch (cpu_pci_speed) {
			case 20000000:
				ret |= 0x0c;
				break;
			case 25000000:
			default:
				ret |= 0x00;
				break;
			case 30000000:
				ret |= 0x08;
				break;
			case 33333333:
				ret |= 0x04;
				break;
		}
		break;
    }

    return ret;
}


static void
sio_reset(void *priv)
{
    sio_t *dev = (sio_t *) priv;

    memset(dev->regs, 0, 256);

    dev->regs[0x00] = 0x86; dev->regs[0x01] = 0x80; /*Intel*/
    dev->regs[0x02] = 0x84; dev->regs[0x03] = 0x04; /*82378IB (SIO)*/
    dev->regs[0x04] = 0x07; dev->regs[0x05] = 0x00;
    dev->regs[0x06] = 0x00; dev->regs[0x07] = 0x02;
    dev->regs[0x08] = dev->id;

    dev->regs[0x40] = 0x20; dev->regs[0x41] = 0x00;
    dev->regs[0x42] = 0x04; dev->regs[0x43] = 0x00;
    dev->regs[0x44] = 0x20; dev->regs[0x45] = 0x10;
    dev->regs[0x46] = 0x0f; dev->regs[0x47] = 0x00;
    dev->regs[0x48] = 0x01; dev->regs[0x49] = 0x10;
    dev->regs[0x4a] = 0x10; dev->regs[0x4b] = 0x0f;
    dev->regs[0x4c] = 0x56; dev->regs[0x4d] = 0x40;
    dev->regs[0x4e] = 0x07; dev->regs[0x4f] = 0x4f;
    dev->regs[0x54] = 0x00; dev->regs[0x55] = 0x00; dev->regs[0x56] = 0x00;
    dev->regs[0x60] = 0x80; dev->regs[0x61] = 0x80; dev->regs[0x62] = 0x80; dev->regs[0x63] = 0x80;
    dev->regs[0x80] = 0x78; dev->regs[0x81] = 0x00;
    dev->regs[0xa0] = 0x08;
    dev->regs[0xa8] = 0x0f;

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    if (dev->timer_base & 0x0001) {
	io_removehandler(dev->timer_base & 0xfffc, 0x0004,
			 sio_timer_read, sio_timer_readw, NULL,
			 sio_timer_write, sio_timer_writew, NULL, dev);
    }

    dev->timer_base = 0x0078;
}


static void
sio_close(void *p)
{
    sio_t *sio = (sio_t *)p;

    free(sio);
}


static void
sio_speed_changed(void *priv)
{
    sio_t *dev = (sio_t *) priv;
    int te;

    te = timer_is_enabled(&dev->timer);

    timer_disable(&dev->timer);
    if (te)
	timer_set_delay_u64(&dev->timer, ((uint64_t) dev->timer_latch) * TIMER_USEC);
}


static void *
sio_init(const device_t *info)
{
    sio_t *sio = (sio_t *) malloc(sizeof(sio_t));
    memset(sio, 0, sizeof(sio_t));

    pci_add_card(PCI_ADD_SOUTHBRIDGE, sio_read, sio_write, sio);

    device_add(&apm_device);

    sio->id = info->local;
    sio_reset(sio);

    sio->port_92 = device_add(&port_92_pci_device);

    dma_alias_set();

    io_sethandler(0x0073, 0x0001,
		  sio_config_read, NULL, NULL, sio_config_write, NULL, NULL, sio);
    io_sethandler(0x0075, 0x0001,
		  sio_config_read, NULL, NULL, sio_config_write, NULL, NULL, sio);

    timer_add(&sio->timer, NULL, NULL, 0);

    return sio;
}


const device_t sio_device =
{
    "Intel 82378IB (SIO)",
    DEVICE_PCI,
    0x00,
    sio_init,
    sio_close,
    NULL,
    NULL,
    sio_speed_changed,
    NULL,
    NULL
};


const device_t sio_zb_device =
{
    "Intel 82378ZB (SIO)",
    DEVICE_PCI,
    0x03,
    sio_init,
    sio_close,
    NULL,
    NULL,
    sio_speed_changed,
    NULL,
    NULL
};
