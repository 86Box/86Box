#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "device.h"
#include "machine/machine.h"
#include "io.h"
#include "pic.h"
#include "mem.h"
#include "rom.h"
#include "serial.h"
#include "timer.h"
#include "mouse.h"


enum
{
    SERIAL_INT_LSR = 1,
    SERIAL_INT_RECEIVE = 2,
    SERIAL_INT_TRANSMIT = 4,
    SERIAL_INT_MSR = 8
};


static int		next_inst = 0;
static serial_device_t	serial_devices[SERIAL_MAX];


#ifdef ENABLE_SERIAL_LOG
int serial_do_log = ENABLE_SERIAL_LOG;


static void
serial_log(const char *fmt, ...)
{
    va_list ap;

    if (serial_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define serial_log(fmt, ...)
#endif


void
serial_reset_port(serial_t *dev)
{
    dev->iir = dev->ier = dev->lcr = dev->fcr = 0;
    dev->fifo_enabled = 0;
    dev->xmit_fifo_pos = dev->rcvr_fifo_pos = 0;
    memset(dev->xmit_fifo, 0, 14);
    memset(dev->rcvr_fifo, 0, 14);
}


void
serial_update_ints(serial_t *dev)
{
    int stat = 0;

    dev->iir = 1;

    if ((dev->ier & 4) && (dev->int_status & SERIAL_INT_LSR)) {
	/* Line status interrupt */
	stat = 1;
	dev->iir = 6;
    } else if ((dev->ier & 1) && (dev->int_status & SERIAL_INT_RECEIVE)) {
	/* Recieved data available */
	stat = 1;
	dev->iir = 4;
    } else if ((dev->ier & 2) && (dev->int_status & SERIAL_INT_TRANSMIT)) {
	/* Transmit data empty */
	stat = 1;
	dev->iir = 2;
    } else if ((dev->ier & 8) && (dev->int_status & SERIAL_INT_MSR)) {
	/* Modem status interrupt */
	stat = 1;
	dev->iir = 0;
    }

    if (stat && ((dev->mctrl & 8) || PCJR)) {
	if (dev->type >= SERIAL_NS16540)
		picintlevel(1 << dev->irq);
	else
		picint(1 << dev->irq);
    } else
	picintc(1 << dev->irq);
}


void
serial_write_fifo(serial_t *dev, uint8_t dat)
{
    uint8_t old_lsr;
    serial_log("serial_write_fifo(%08X, %02X, %i)\n", dev, dat, (dev->type >= SERIAL_NS16550) && dev->fifo_enabled);

    if ((dev->type >= SERIAL_NS16550) && dev->fifo_enabled) {
	/* FIFO mode. */
	dev->rcvr_fifo[dev->rcvr_fifo_pos++] = dat;
	dev->rcvr_fifo_pos %= dev->fifo_len;
	old_lsr = dev->lsr;
	dev->lsr &= 0xfe;
	dev->lsr |= (!dev->rcvr_fifo_pos);
	dev->int_status &= SERIAL_INT_RECEIVE;
	if (!dev->rcvr_fifo_pos)
		dev->int_status |= SERIAL_INT_RECEIVE;
	if ((old_lsr ^ dev->lsr) & 0x01)
		serial_update_ints(dev);
    } else {
	/* Non-FIFO mode. */
	dev->dat = dat;
	dev->lsr |= 1;
	dev->int_status |= SERIAL_INT_RECEIVE;
	serial_update_ints(dev);
    }
}


void
serial_dev_write(serial_t *dev, uint8_t val)
{
    if (dev->mctrl & 0x10)
	serial_write_fifo(dev, val);
    else if (dev->sd->dev_write)
	dev->sd->dev_write(dev, dev->sd->priv, val);
}


void
serial_write(uint16_t addr, uint8_t val, void *p)
{
    serial_t *dev = (serial_t *)p;
    uint8_t new_msr, old_lsr, i;

    serial_log("UART: Write %02X to port %02X\n", val, addr);

    switch (addr & 7) {
	case 0:
		if (dev->lcr & 0x80) {
			dev->dlab1 = val;
			return;
                }

		if ((dev->type >= SERIAL_NS16550) && dev->fifo_enabled) {
			/* FIFO mode. */
			dev->xmit_fifo[dev->xmit_fifo_pos++] = val;
			dev->xmit_fifo_pos %= dev->fifo_len;
			old_lsr = dev->lsr;
			dev->lsr &= 0xdf;
			if (!dev->xmit_fifo_pos) {
				for (i = 0; i < dev->fifo_len; i++)
					serial_dev_write(dev, dev->xmit_fifo[i]);
				dev->lsr |= 0x20;
				dev->int_status |= SERIAL_INT_TRANSMIT;
			}
			if ((old_lsr ^ dev->lsr) & 0x20)
				serial_update_ints(dev);
		} else {
			/* Non-FIFO mode. */
			dev->thr = val;
			dev->lsr |= 0x20;
			dev->int_status |= SERIAL_INT_TRANSMIT;
			serial_dev_write(dev, val);
			serial_update_ints(dev);
		}
		break;
	case 1:
		if (dev->lcr & 0x80) {
			dev->dlab2 = val;
			return;
		}
		dev->ier = val & 0xf;
		serial_update_ints(dev);
		break;
	case 2:
		if (dev->type >= SERIAL_NS16550) {
			dev->fcr = val & 0xf9;
			dev->fifo_enabled = val & 0x01;
			if (val & 0x02) {
				memset(dev->rcvr_fifo, 0, 14);
				dev->rcvr_fifo_pos = 0;
			}
			if (val & 0x04) {
				memset(dev->xmit_fifo, 0, 14);
				dev->xmit_fifo_pos = 0;
			}
			switch ((val >> 6) & 0x03) {
				case 0:
					dev->fifo_len = 1;
					break;
				case 1:
					dev->fifo_len = 4;
					break;
				case 2:
					dev->fifo_len = 8;
					break;
				case 3:
					dev->fifo_len = 14;
					break;
			}
		}
		break;
	case 3:
		dev->lcr = val;
		break;
	case 4:
		if ((val & 2) && !(dev->mctrl & 2)) {
			if (dev->sd->rcr_callback)
				dev->sd->rcr_callback(dev, dev->sd->priv);
		}
		dev->mctrl = val;
		if (val & 0x10) {
			new_msr = (val & 0x0c) << 4;
			new_msr |= (val & 0x02) ? 0x10: 0;
			new_msr |= (val & 0x01) ? 0x20: 0;

			if ((dev->msr ^ new_msr) & 0x10)
				new_msr |= 0x01;
			if ((dev->msr ^ new_msr) & 0x20)
				new_msr |= 0x02;
			if ((dev->msr ^ new_msr) & 0x80)
				new_msr |= 0x08;
			if ((dev->msr & 0x40) && !(new_msr & 0x40))
				new_msr |= 0x04;

			dev->msr = new_msr;
		}
		break;
	case 5:
		dev->lsr = val;
		if (dev->lsr & 0x01)
			dev->int_status |= SERIAL_INT_RECEIVE;
		if (dev->lsr & 0x1e)
			dev->int_status |= SERIAL_INT_LSR;
		if (dev->lsr & 0x20)
			dev->int_status |= SERIAL_INT_TRANSMIT;
		serial_update_ints(dev);
		break;
	case 6:
		dev->msr = val;
		if (dev->msr & 0x0f)
			dev->int_status |= SERIAL_INT_MSR;
		serial_update_ints(dev);
		break;
	case 7:
		dev->scratch = val;
		break;
    }
}


uint8_t
serial_read(uint16_t addr, void *p)
{
    serial_t *dev = (serial_t *)p;
    uint8_t old_lsr, ret = 0;

    switch (addr & 7) {
	case 0:
		if (dev->lcr & 0x80) {
			ret = dev->dlab1;
			break;
		}

		if ((dev->type >= SERIAL_NS16550) && dev->fifo_enabled) {
			/* FIFO mode. */
			ret = dev->rcvr_fifo[dev->rcvr_fifo_pos++];
			dev->rcvr_fifo_pos %= dev->fifo_len;
			old_lsr = dev->lsr;
			if (!dev->rcvr_fifo_pos) {
				dev->lsr &= 0xfe;
				dev->int_status &= ~SERIAL_INT_RECEIVE;
				if ((old_lsr ^ dev->lsr) & 0x01)
					serial_update_ints(dev);
			}
		} else {
			ret = dev->dat;
			dev->lsr &= 0xfe;
			dev->int_status &= ~SERIAL_INT_RECEIVE;
			serial_update_ints(dev);
		}
		break;
	case 1:
		if (dev->lcr & 0x80)
			ret = dev->dlab2;
		else
			ret = dev->ier;
		break;
	case 2: 
		ret = dev->iir;
		if ((ret & 0xe) == 2) {
			dev->int_status &= ~SERIAL_INT_TRANSMIT;
			serial_update_ints(dev);
		}
		if (dev->fcr & 1)
			ret |= 0xc0;
		break;
	case 3:
		ret = dev->lcr;
		break;
	case 4:
		ret = dev->mctrl;
		break;
	case 5:
		if (dev->lsr & 0x20)
			dev->lsr |= 0x40;
		dev->lsr |= 0x20;
		ret = dev->lsr;
		if (dev->lsr & 0x1f)
			dev->lsr &= ~0x1e;
		dev->int_status &= ~SERIAL_INT_LSR;
		serial_update_ints(dev);
		break;
	case 6:
		ret = dev->msr;
		dev->msr &= ~0x0f;
		dev->int_status &= ~SERIAL_INT_MSR;
		serial_update_ints(dev);
		break;
	case 7:
		ret = dev->scratch;
		break;
    }

    serial_log("UART: Read %02X from port %02X\n", ret, addr);
    return ret;
}


void
serial_remove(serial_t *dev)
{
    if (!serial_enabled[dev->inst])
	return;

    if (!dev->base_address)
	return;

    serial_log("Removing serial port %i at %04X...\n", dev->inst, dev->base_address);

    io_removehandler(dev->base_address, 0x0008,
		     serial_read, NULL, NULL, serial_write, NULL, NULL, dev);
    dev->base_address = 0x0000;
}


void
serial_setup(serial_t *dev, uint16_t addr, int irq)
{
    serial_log("Adding serial port %i at %04X...\n", dev->inst, addr);

    if (!serial_enabled[dev->inst])
	return;
    if (dev->base_address != 0x0000)
	serial_remove(dev);
    dev->base_address = addr;
    if (addr != 0x0000)
	io_sethandler(addr, 0x0008, serial_read, NULL, NULL, serial_write, NULL, NULL, dev);
    dev->irq = irq;
}


serial_t *
serial_attach(int port,
	      void (*rcr_callback)(struct serial_s *serial, void *p),
	      void (*dev_write)(struct serial_s *serial, void *p, uint8_t data),
	      void *priv)
{
    serial_device_t *sd = &serial_devices[port];

    sd->rcr_callback = rcr_callback;
    sd->dev_write = dev_write;
    sd->priv = priv;

    return sd->serial;
}


static void
serial_close(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    next_inst--;

    free(dev);
}


static void *
serial_init(const device_t *info)
{
    serial_t *dev = (serial_t *) malloc(sizeof(serial_t));
    memset(dev, 0, sizeof(serial_t));

    dev->base_address = next_inst ? 0x03f8 : 0x02f8;
    dev->inst = next_inst;

    if (serial_enabled[next_inst]) {
	serial_log("Adding serial port %i...\n", next_inst);
	io_sethandler(dev->base_address, 0x0008,
		      serial_read,  NULL, NULL, serial_write,  NULL, NULL, dev);
	dev->irq = next_inst ? 4 : 3;
	dev->type = info->local;
	memset(&(serial_devices[next_inst]), 0, sizeof(serial_device_t));
	dev->sd = &(serial_devices[next_inst]);
	dev->sd->serial = dev;
	serial_reset_port(dev);
	if (next_inst)
		serial_setup(dev, SERIAL2_ADDR, SERIAL2_IRQ);
	else
		serial_setup(dev, SERIAL1_ADDR, SERIAL1_IRQ);
    }

    next_inst++;

    return dev;
}


const device_t i8250_device = {
    "Intel 8250(-compatible) UART",
    0,
    SERIAL_8250,
    serial_init, serial_close, NULL,
    NULL, NULL, NULL,
    NULL
};

const device_t ns16540_device = {
    "National Semiconductor NS16540(-compatible) UART",
    0,
    SERIAL_NS16540,
    serial_init, serial_close, NULL,
    NULL, NULL, NULL,
    NULL
};

const device_t ns16550_device = {
    "National Semiconductor NS16550(-compatible) UART",
    0,
    SERIAL_NS16550,
    serial_init, serial_close, NULL,
    NULL, NULL, NULL,
    NULL
};
