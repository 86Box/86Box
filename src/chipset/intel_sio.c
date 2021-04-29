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
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
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
#include <86box/chipset.h>


typedef struct
{
    uint8_t	id,
		regs[256];

    uint16_t	timer_base,
		timer_latch;

    double	fast_off_period;

    pc_timer_t	timer, fast_off_timer;

    apm_t *	apm;
    port_92_t *	port_92;
} sio_t;


#ifdef ENABLE_SIO_LOG
int sio_do_log = ENABLE_SIO_LOG;


static void
sio_log(const char *fmt, ...)
{
    va_list ap;

    if (sio_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define sio_log(fmt, ...)
#endif


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
	cycles -= ((int) (PITCONST >> 32));

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
	cycles -= ((int) (PITCONST >> 32));

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

    switch (addr) {
	case 0x04: /*Command register*/
		if (dev->id == 0x03)
			dev->regs[addr] = (dev->regs[addr] & 0xf7) | (val & 0x08);
		break;

	case 0x07:
		dev->regs[addr] &= ~(val & 0x38);
		break;

	case 0x40:
		if (dev->id == 0x03) {
			dev->regs[addr] = (val & 0x7f);

			if (!((val ^ old) & 0x40))
				return;

			dma_alias_remove();
			if (!(val & 0x40))
				dma_alias_set();
		} else
			dev->regs[addr] = (val & 0x3f);
		break;
	case 0x41: case 0x44:
		dev->regs[addr] = (val & 0x1f);
		break;
	case 0x42:
		if (dev->id == 0x03)
			dev->regs[addr] = val;
		else
			dev->regs[addr] = (val & 0x77);
		break;
	case 0x43:
		if (dev->id == 0x03)
			dev->regs[addr] = (val & 0x01);
		break;
	case 0x45: case 0x46:
	case 0x47: case 0x48:
	case 0x49: case 0x4a:
	case 0x4b: case 0x4e:
	case 0x54: case 0x55:
	case 0x56:
		dev->regs[addr] = val;
		break;
	case 0x4c: case 0x4d:
		dev->regs[addr] = (val & 0x7f);
		break;
	case 0x4f:
		dev->regs[addr] = val;

		if (!((val ^ old) & 0x40))
			return;

		port_92_remove(dev->port_92);
		if (val & 0x40)
			port_92_add(dev->port_92);
		break;
	case 0x57:
		dev->regs[addr] = val;

		dma_remove_sg();
		dma_set_sg_base(val);
		break;
	case 0x60: case 0x61: case 0x62: case 0x63:
		if (dev->id == 0x03) {
			sio_log("Set IRQ routing: INT %c -> %02X\n", 0x41 + (addr & 0x03), val);
			dev->regs[addr] = val & 0x8f;
			if (val & 0x80)
				pci_set_irq_routing(PCI_INTA + (addr & 0x03), PCI_IRQ_DISABLED);
			else
				pci_set_irq_routing(PCI_INTA + (addr & 0x03), val & 0xf);
		}
		break;
	case 0x80:
	case 0x81:
		if (addr == 0x80)
			dev->regs[addr] = val & 0xfd;
		else
			dev->regs[addr] = val;

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
	case 0xa0:
		if (dev->id == 0x03) {
			dev->regs[addr] = val & 0x1f;
			apm_set_do_smi(dev->apm, !!(val & 0x01) && !!(dev->regs[0xa2] & 0x80));
			switch ((val & 0x18) >> 3) {
				case 0x00:
					dev->fast_off_period = PCICLK * 32768.0 * 60000.0;
					break;
				case 0x01:
				default:
					dev->fast_off_period = 0.0;
					break;
				case 0x02:
					dev->fast_off_period = PCICLK;
					break;
				case 0x03:
					dev->fast_off_period = PCICLK * 32768.0;
					break;
			}
			cpu_fast_off_count = dev->regs[0xa8] + 1;
			timer_disable(&dev->fast_off_timer);
			if (dev->fast_off_period != 0.0)
				timer_on_auto(&dev->fast_off_timer, dev->fast_off_period);
		}
		break;
	case 0xa2:
		if (dev->id == 0x03) {
			dev->regs[addr] = val & 0xff;
			apm_set_do_smi(dev->apm, !!(dev->regs[0xa0] & 0x01) && !!(val & 0x80));
		}
		break;
	case 0xaa:
		if (dev->id == 0x03)
			dev->regs[addr] &= (val & 0xff);
		break;
	case 0xac: case 0xae:
		if (dev->id == 0x03)
			dev->regs[addr] = val & 0xff;
		break;
	case 0xa4:
		if (dev->id == 0x03) {
			dev->regs[addr] = val & 0xfb;
			cpu_fast_off_flags = (cpu_fast_off_flags & 0xffffff00) | dev->regs[addr];
		}
		break;
	case 0xa5:
		if (dev->id == 0x03) {
			dev->regs[addr] = val & 0xff;
			cpu_fast_off_flags = (cpu_fast_off_flags & 0xffff00ff) | (dev->regs[addr] << 8);
		}
		break;
	case 0xa7:
		if (dev->id == 0x03) {
			dev->regs[addr] = val & 0xa0;
			cpu_fast_off_flags = (cpu_fast_off_flags & 0x00ffffff) | (dev->regs[addr] << 24);
		}
		break;
	case 0xa8:
		dev->regs[addr] = val & 0xff;
		cpu_fast_off_val = val;
		cpu_fast_off_count = val + 1;
		timer_disable(&dev->fast_off_timer);
		if (dev->fast_off_period != 0.0)
			timer_on_auto(&dev->fast_off_timer, dev->fast_off_period);
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
sio_reset_hard(void *priv)
{
    sio_t *dev = (sio_t *) priv;

    memset(dev->regs, 0, 256);

    dev->regs[0x00] = 0x86; dev->regs[0x01] = 0x80; /*Intel*/
    dev->regs[0x02] = 0x84; dev->regs[0x03] = 0x04; /*82378IB (SIO)*/
    dev->regs[0x04] = 0x07;
    dev->regs[0x07] = 0x02;
    dev->regs[0x08] = dev->id;

    dev->regs[0x40] = 0x20; dev->regs[0x41] = 0x00;
    dev->regs[0x42] = 0x04;
    dev->regs[0x45] = 0x10; dev->regs[0x46] = 0x0f;
    dev->regs[0x48] = 0x01;
    dev->regs[0x4a] = 0x10; dev->regs[0x4b] = 0x0f;
    dev->regs[0x4c] = 0x56; dev->regs[0x4d] = 0x40;
    dev->regs[0x4e] = 0x07; dev->regs[0x4f] = 0x4f;
    dev->regs[0x57] = 0x04;
    if (dev->id == 0x03) {
	dev->regs[0x60] = 0x80; dev->regs[0x61] = 0x80; dev->regs[0x62] = 0x80; dev->regs[0x63] = 0x80;
    }
    dev->regs[0x80] = 0x78;
    if (dev->id == 0x03) {
	dev->regs[0xa0] = 0x08;
	dev->regs[0xa8] = 0x0f;
    }

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
sio_apm_out(uint16_t port, uint8_t val, void *p)
{
    sio_t *dev = (sio_t *) p;

    if (dev->apm->do_smi)
	dev->regs[0xaa] |= 0x80;
}


static void
sio_fast_off_count(void *priv)
{
    sio_t *dev = (sio_t *) priv;

    cpu_fast_off_count--;

    if (cpu_fast_off_count == 0) {
	smi_line = 1;
	dev->regs[0xaa] |= 0x20;
	cpu_fast_off_count = dev->regs[0xa8] + 1;
    }

    timer_on_auto(&dev->fast_off_timer, dev->fast_off_period);
}


static void
sio_reset(void *p)
{
    sio_t *dev = (sio_t *) p;

    sio_write(0, 0x57, 0x04, p);

    dma_set_params(1, 0xffffffff);

    if (dev->id == 0x03) {
	sio_write(0, 0xa0, 0x08, p);
	sio_write(0, 0xa2, 0x00, p);
	sio_write(0, 0xa4, 0x00, p);
	sio_write(0, 0xa5, 0x00, p);
	sio_write(0, 0xa6, 0x00, p);
	sio_write(0, 0xa7, 0x00, p);
	sio_write(0, 0xa8, 0x0f, p);
    }
}


static void
sio_close(void *p)
{
    sio_t *dev = (sio_t *)p;

    free(dev);
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

    if (dev->id == 0x03) {
	te = timer_is_enabled(&dev->fast_off_timer);

	timer_stop(&dev->fast_off_timer);
	if (te)
		timer_on_auto(&dev->fast_off_timer, dev->fast_off_period);
    }
}


static void *
sio_init(const device_t *info)
{
    sio_t *dev = (sio_t *) malloc(sizeof(sio_t));
    memset(dev, 0, sizeof(sio_t));

    pci_add_card(PCI_ADD_SOUTHBRIDGE, sio_read, sio_write, dev);

    dev->id = info->local;

    if (dev->id == 0x03)
	timer_add(&dev->fast_off_timer, sio_fast_off_count, dev, 0);

    sio_reset_hard(dev);

    cpu_fast_off_flags = 0x00000000;

    if (dev->id == 0x03) {
	cpu_fast_off_val = dev->regs[0xa8];
	cpu_fast_off_count = cpu_fast_off_val + 1;
    } else
	cpu_fast_off_val = cpu_fast_off_count = 0;

    if (dev->id == 0x03) {
	dev->apm = device_add(&apm_pci_device);
	/* APM intercept handler to update 82378ZB SMI status on APM SMI. */
	io_sethandler(0x00b2, 0x0001, NULL, NULL, NULL, sio_apm_out, NULL, NULL, dev);
    }

    dev->port_92 = device_add(&port_92_pci_device);

    dma_set_sg_base(0x04);
    dma_set_params(1, 0xffffffff);
    dma_ext_mode_init();
    dma_high_page_init();

    if (dev->id == 0x03)
	dma_alias_set();

    io_sethandler(0x0073, 0x0001,
		  sio_config_read, NULL, NULL, sio_config_write, NULL, NULL, dev);
    io_sethandler(0x0075, 0x0001,
		  sio_config_read, NULL, NULL, sio_config_write, NULL, NULL, dev);

    timer_add(&dev->timer, NULL, NULL, 0);

    // device_add(&i8254_sec_device);

    return dev;
}


const device_t sio_device =
{
    "Intel 82378IB (SIO)",
    DEVICE_PCI,
    0x00,
    sio_init,
    sio_close,
    sio_reset,
    { NULL },
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
    sio_reset,
    { NULL },
    sio_speed_changed,
    NULL,
    NULL
};
