/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		NS8250/16450/16550 UART emulation.
 *
 *		Now passes all the AMIDIAG tests.
 *
 *
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
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
#include <86box/timer.h>
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/serial.h>
#include <86box/mouse.h>


enum
{
    SERIAL_INT_LSR = 1,
    SERIAL_INT_RECEIVE = 2,
    SERIAL_INT_TRANSMIT = 4,
    SERIAL_INT_MSR = 8,
    SERIAL_INT_TIMEOUT = 16
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
    dev->lsr = 0x60;	/* Mark that both THR/FIFO and TXSR are empty. */
    dev->iir = dev->ier = dev->lcr = dev->fcr = 0;
    dev->fifo_enabled = 0;
    dev->xmit_fifo_pos = dev->rcvr_fifo_pos = 0;
    dev->rcvr_fifo_full = 0;
    dev->baud_cycles = 0;
    memset(dev->xmit_fifo, 0, 16);
    memset(dev->rcvr_fifo, 0, 14);
}


void
serial_transmit_period(serial_t *dev)
{
    double ddlab;

    ddlab = (double) dev->dlab;

    /* Bit period based on DLAB. */
    dev->transmit_period = (16000000.0 * ddlab) / dev->clock_src;
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
    } else if ((dev->ier & 1) && (dev->int_status & SERIAL_INT_TIMEOUT)) {
	/* Received data available */
	stat = 1;
	dev->iir = 0x0c;
    } else if ((dev->ier & 1) && (dev->int_status & SERIAL_INT_RECEIVE)) {
	/* Received data available */
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

    if (stat && (dev->irq != 0xff) && ((dev->mctrl & 8) || (dev->type == SERIAL_8250_PCJR))) {
	if (dev->type >= SERIAL_NS16450)
		picintlevel(1 << dev->irq);
	else
		picint(1 << dev->irq);
    } else
	picintc(1 << dev->irq);
}


static void
serial_clear_timeout(serial_t *dev)
{
    /* Disable timeout timer and clear timeout condition. */
    timer_disable(&dev->timeout_timer);
    dev->int_status &= ~SERIAL_INT_TIMEOUT;
    serial_update_ints(dev);
}


static void
write_fifo(serial_t *dev, uint8_t dat)
{
    serial_log("write_fifo(%08X, %02X, %i, %i)\n", dev, dat, (dev->type >= SERIAL_NS16550) && dev->fifo_enabled, dev->rcvr_fifo_pos & 0x0f);

    if ((dev->type >= SERIAL_NS16550) && dev->fifo_enabled) {
	/* FIFO mode. */
	timer_disable(&dev->timeout_timer);
	/* Indicate overrun. */
	if (dev->rcvr_fifo_full)
		dev->lsr |= 0x02;
	else
		dev->rcvr_fifo[dev->rcvr_fifo_pos] = dat;
	dev->lsr &= 0xfe;
	dev->int_status &= ~SERIAL_INT_RECEIVE;
	if (dev->rcvr_fifo_pos == (dev->rcvr_fifo_len - 1)) {
		dev->lsr |= 0x01;
		dev->int_status |= SERIAL_INT_RECEIVE;
	}
	if (dev->rcvr_fifo_pos < 15)
		dev->rcvr_fifo_pos++;
	else
		dev->rcvr_fifo_full = 1;
	serial_update_ints(dev);
        timer_on_auto(&dev->timeout_timer, 4.0 * dev->bits * dev->transmit_period);
    } else {
	/* Non-FIFO mode. */
	/* Indicate overrun. */
	if (dev->lsr & 0x01)
		dev->lsr |= 0x02;
	dev->dat = dat;
	dev->lsr |= 0x01;
	dev->int_status |= SERIAL_INT_RECEIVE;
	serial_update_ints(dev);
    }
}


void
serial_write_fifo(serial_t *dev, uint8_t dat)
{
    serial_log("serial_write_fifo(%08X, %02X, %i, %i)\n", dev, dat, (dev->type >= SERIAL_NS16550) && dev->fifo_enabled, dev->rcvr_fifo_pos & 0x0f);

    if (!(dev->mctrl & 0x10))
	write_fifo(dev, dat);
}


void
serial_transmit(serial_t *dev, uint8_t val)
{
    if (dev->mctrl & 0x10)
	write_fifo(dev, val);
    else if (dev->sd->dev_write)
	dev->sd->dev_write(dev, dev->sd->priv, val);
}


static void
serial_move_to_txsr(serial_t *dev)
{
    int i = 0;

    if (dev->fifo_enabled) {
	dev->txsr = dev->xmit_fifo[0];
	if (dev->xmit_fifo_pos > 0) {
		/* Move the entire fifo forward by one byte. */
		for (i = 1; i < 16; i++)
			dev->xmit_fifo[i - 1] = dev->xmit_fifo[i];
		/* Decrease FIFO position. */
		dev->xmit_fifo_pos--;
	}
    } else {
	dev->txsr = dev->thr;
	dev->thr = 0;
    }

    dev->lsr &= ~0x40;
    serial_log("serial_move_to_txsr(): FIFO %sabled, FIFO pos = %i\n", dev->fifo_enabled ? "en" : "dis", dev->xmit_fifo_pos & 0x0f);

    if (!dev->fifo_enabled || (dev->xmit_fifo_pos == 0x0)) {
	/* Update interrupts to signal THRE and that TXSR is no longer empty. */
	dev->lsr |= 0x20;
	dev->int_status |= SERIAL_INT_TRANSMIT;
	serial_update_ints(dev);
    }
    if (dev->transmit_enabled & 2)
	dev->baud_cycles++;
    else
	dev->baud_cycles = 0;	/* If not moving while transmitting, reset BAUDOUT cycle count. */
    if (!dev->fifo_enabled || (dev->xmit_fifo_pos == 0x0))
	dev->transmit_enabled &= ~1;	/* Stop moving. */
    dev->transmit_enabled |= 2;	/* Start transmitting. */
}


static void
serial_process_txsr(serial_t *dev)
{
    serial_log("serial_process_txsr(): FIFO %sabled\n", dev->fifo_enabled ? "en" : "dis");
    serial_transmit(dev, dev->txsr);
    dev->txsr = 0;
    /* Reset BAUDOUT cycle count. */
    dev->baud_cycles = 0;
    /* If FIFO is enabled and there are bytes left to transmit,
       continue with the FIFO, otherwise stop. */
    if (dev->fifo_enabled && (dev->xmit_fifo_pos != 0x0))
	dev->transmit_enabled |= 1;
    else {
	/* Both FIFO/THR and TXSR are empty. */
	/* If bit 5 is set, also set bit 6 to mark both THR and shift register as empty. */
	if (dev->lsr & 0x20)
		dev->lsr |= 0x40;
	dev->transmit_enabled &= ~2;
    }
    dev->int_status &= ~SERIAL_INT_TRANSMIT;
    serial_update_ints(dev);
}


/* Transmit_enable flags:
	Bit 0 = Do move if set;
	Bit 1 = Do transmit if set. */
static void
serial_transmit_timer(void *priv)
{
    serial_t *dev = (serial_t *) priv;
    int delay = 8;			/* STOP to THRE delay is 8 BAUDOUT cycles. */

    if (dev->transmit_enabled & 3) {
	if ((dev->transmit_enabled & 1) && (dev->transmit_enabled & 2))
		delay = dev->data_bits;		/* Delay by less if already transmitting. */

	dev->baud_cycles++;

	/* We have processed (total bits) BAUDOUT cycles, transmit the byte. */
	if ((dev->baud_cycles == dev->bits) && (dev->transmit_enabled & 2))
		serial_process_txsr(dev);

	/* We have processed (data bits) BAUDOUT cycles. */
	if ((dev->baud_cycles == delay) && (dev->transmit_enabled & 1))
		serial_move_to_txsr(dev);

	if (dev->transmit_enabled & 3)
		timer_on_auto(&dev->transmit_timer, dev->transmit_period);
    } else {
	dev->baud_cycles = 0;
	return;
    }
}


static void
serial_timeout_timer(void *priv)
{
    serial_t *dev = (serial_t *) priv;

#ifdef ENABLE_SERIAL_LOG
    serial_log("serial_timeout_timer()\n");
#endif

    dev->lsr |= 0x01;
    dev->int_status |= SERIAL_INT_TIMEOUT;
    serial_update_ints(dev);
}


static void
serial_update_speed(serial_t *dev)
{
    if (dev->transmit_enabled & 3)
	timer_on_auto(&dev->transmit_timer, dev->transmit_period);

    if (timer_is_enabled(&dev->timeout_timer))
	timer_on_auto(&dev->timeout_timer, 4.0 * dev->bits * dev->transmit_period);
}


static void
serial_reset_fifo(serial_t *dev)
{
    dev->lsr = (dev->lsr & 0xfe) | 0x60;
    dev->int_status = (dev->int_status & ~SERIAL_INT_RECEIVE) | SERIAL_INT_TRANSMIT;
    serial_update_ints(dev);
    dev->xmit_fifo_pos = dev->rcvr_fifo_pos = 0;
    dev->rcvr_fifo_full = 0;
}


void
serial_set_clock_src(serial_t *dev, double clock_src)
{
    dev->clock_src = clock_src;

    serial_transmit_period(dev);
    serial_update_speed(dev);
}


void
serial_write(uint16_t addr, uint8_t val, void *p)
{
    serial_t *dev = (serial_t *)p;
    uint8_t new_msr, old;

    serial_log("UART: Write %02X to port %02X\n", val, addr);

    cycles -= ISA_CYCLES(8);

    switch (addr & 7) {
	case 0:
		if (dev->lcr & 0x80) {
			dev->dlab = (dev->dlab & 0xff00) | val;
			serial_transmit_period(dev);
			serial_update_speed(dev);
			return;
                }

		/* Indicate FIFO/THR is no longer empty. */
		dev->lsr &= 0x9f;
		dev->int_status &= ~SERIAL_INT_TRANSMIT;
		serial_update_ints(dev);

		if ((dev->type >= SERIAL_NS16550) && dev->fifo_enabled && (dev->xmit_fifo_pos < 16)) {
			/* FIFO mode, begin transmitting. */
			timer_on_auto(&dev->transmit_timer, dev->transmit_period);
			dev->transmit_enabled |= 1;	/* Start moving. */
			dev->xmit_fifo[dev->xmit_fifo_pos++] = val;
		} else {
			/* Non-FIFO mode, begin transmitting. */
			timer_on_auto(&dev->transmit_timer, dev->transmit_period);
			dev->transmit_enabled |= 1;	/* Start moving. */
			dev->thr = val;
		}
		break;
	case 1:
		if (dev->lcr & 0x80) {
			dev->dlab = (dev->dlab & 0x00ff) | (val << 8);
			serial_transmit_period(dev);
			serial_update_speed(dev);
			return;
		}
		if ((val & 2) && (dev->lsr & 0x20))
			dev->int_status |= SERIAL_INT_TRANSMIT;
		dev->ier = val & 0xf;
		serial_update_ints(dev);
		break;
	case 2:
		if (dev->type >= SERIAL_NS16550) {
			if ((val ^ dev->fcr) & 0x01)
				serial_reset_fifo(dev);
			dev->fcr = val & 0xf9;
			dev->fifo_enabled = val & 0x01;
			if (!dev->fifo_enabled) {
				memset(dev->rcvr_fifo, 0, 14);
				memset(dev->xmit_fifo, 0, 16);
				dev->xmit_fifo_pos = dev->rcvr_fifo_pos = 0;
				dev->rcvr_fifo_full = 0;
				dev->rcvr_fifo_len = 1;
				break;
			}
			if (val & 0x02) {
				memset(dev->rcvr_fifo, 0, 14);
				dev->rcvr_fifo_pos = 0;
				dev->rcvr_fifo_full = 0;
			}
			if (val & 0x04) {
				memset(dev->xmit_fifo, 0, 16);
				dev->xmit_fifo_pos = 0;
			}
			switch ((val >> 6) & 0x03) {
				case 0:
					dev->rcvr_fifo_len = 1;
					break;
				case 1:
					dev->rcvr_fifo_len = 4;
					break;
				case 2:
					dev->rcvr_fifo_len = 8;
					break;
				case 3:
					dev->rcvr_fifo_len = 14;
					break;
			}
			serial_log("FIFO now %sabled, receive FIFO length = %i\n", dev->fifo_enabled ? "en" : "dis", dev->rcvr_fifo_len);
		}
		break;
	case 3:
		old = dev->lcr;
		dev->lcr = val;
		if ((old ^ val) & 0x0f) {
			/* Data bits + start bit. */
			dev->bits = ((dev->lcr & 0x03) + 5) + 1;
			/* Stop bits. */
			dev->bits++;		/* First stop bit. */
			if (dev->lcr & 0x04)
				dev->bits++;	/* Second stop bit. */
			/* Parity bit. */
			if (dev->lcr & 0x08)
				dev->bits++;

			serial_transmit_period(dev);
			serial_update_speed(dev);
		}
		break;
	case 4:
		if ((val & 2) && !(dev->mctrl & 2)) {
			if (dev->sd->rcr_callback)
				dev->sd->rcr_callback(dev, dev->sd->priv);
		}
		if (!(val & 8) && (dev->mctrl & 8))
			picintc(1 << dev->irq);
		if ((val ^ dev->mctrl) & 0x10)
			serial_reset_fifo(dev);
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

			dev->xmit_fifo_pos = dev->rcvr_fifo_pos = 0;
			dev->rcvr_fifo_full = 0;
		}
		break;
	case 5:
		dev->lsr = (dev->lsr & 0xe0) | (val & 0x1f);
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
		if (dev->type >= SERIAL_NS16450)
			dev->scratch = val;
		break;
    }
}


uint8_t
serial_read(uint16_t addr, void *p)
{
    serial_t *dev = (serial_t *)p;
    uint8_t i, ret = 0;

    cycles -= ISA_CYCLES(8);

    switch (addr & 7) {
	case 0:
		if (dev->lcr & 0x80) {
			ret = dev->dlab & 0xff;
			break;
		}

		if ((dev->type >= SERIAL_NS16550) && dev->fifo_enabled) {
			/* FIFO mode. */

			serial_clear_timeout(dev);

			ret = dev->rcvr_fifo[0];
			dev->rcvr_fifo_full = 0;
			if (dev->rcvr_fifo_pos > 0) {
				for (i = 1; i < 16; i++)
					dev->rcvr_fifo[i - 1] = dev->rcvr_fifo[i];
				serial_log("FIFO position %i: read %02X, next %02X\n", dev->rcvr_fifo_pos, ret, dev->rcvr_fifo[0]);
				dev->rcvr_fifo_pos--;
				/* At least one byte remains to be read, start the timeout
				   timer so that a timeout is indicated in case of no read. */
				timer_on_auto(&dev->timeout_timer, 4.0 * dev->bits * dev->transmit_period);
			} else {
				dev->lsr &= 0xfe;
				dev->int_status &= ~SERIAL_INT_RECEIVE;
				serial_update_ints(dev);
			}
		} else {
			ret = dev->dat;
			dev->lsr &= 0xfe;
			dev->int_status &= ~SERIAL_INT_RECEIVE;
			serial_update_ints(dev);
		}
		serial_log("Read data: %02X\n", ret);
		break;
	case 1:
		if (dev->lcr & 0x80)
			ret = (dev->dlab >> 8) & 0xff;
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
    if (dev == NULL)
	return;

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
serial_setup(serial_t *dev, uint16_t addr, uint8_t irq)
{
    serial_log("Adding serial port %i at %04X...\n", dev->inst, addr);

    if (dev == NULL)
	return;

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
serial_speed_changed(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    serial_update_speed(dev);
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

    dev->inst = next_inst;

    if (serial_enabled[next_inst]) {
	serial_log("Adding serial port %i...\n", next_inst);
	dev->type = info->local;
	memset(&(serial_devices[next_inst]), 0, sizeof(serial_device_t));
	dev->sd = &(serial_devices[next_inst]);
	dev->sd->serial = dev;
	serial_reset_port(dev);
	if (next_inst == 3)
		serial_setup(dev, SERIAL4_ADDR, SERIAL4_IRQ);
	else if (next_inst == 2)
		serial_setup(dev, SERIAL3_ADDR, SERIAL3_IRQ);
	else if ((next_inst == 1) || (info->flags & DEVICE_PCJR))
		serial_setup(dev, SERIAL2_ADDR, SERIAL2_IRQ);
	else if (next_inst == 0)
		serial_setup(dev, SERIAL1_ADDR, SERIAL1_IRQ);

	/* Default to 1200,N,7. */
	dev->dlab = 96;
	dev->fcr = 0x06;
	dev->clock_src = 1843200.0;
	serial_transmit_period(dev);
	timer_add(&dev->transmit_timer, serial_transmit_timer, dev, 0);
	timer_add(&dev->timeout_timer, serial_timeout_timer, dev, 0);
    }

    next_inst++;

    return dev;
}


void
serial_set_next_inst(int ni)
{
    next_inst = ni;
}


void
serial_standalone_init(void) {
    if (next_inst == 0) {
	device_add_inst(&i8250_device, 1);
	device_add_inst(&i8250_device, 2);
	device_add_inst(&i8250_device, 3);
	device_add_inst(&i8250_device, 4);
    } else if (next_inst == 1) {
	device_add_inst(&i8250_device, 2);
	device_add_inst(&i8250_device, 3);
	device_add_inst(&i8250_device, 4);
    } else if (next_inst == 2) {
	device_add_inst(&i8250_device, 3);
	device_add_inst(&i8250_device, 4);
    } else
	device_add_inst(&i8250_device, 4);
};


const device_t i8250_device = {
    "Intel 8250(-compatible) UART",
    0,
    SERIAL_8250,
    serial_init, serial_close, NULL,
    { NULL }, serial_speed_changed, NULL,
    NULL
};

const device_t i8250_pcjr_device = {
    "Intel 8250(-compatible) UART for PCjr",
    DEVICE_PCJR,
    SERIAL_8250_PCJR,
    serial_init, serial_close, NULL,
    { NULL }, serial_speed_changed, NULL,
    NULL
};

const device_t ns16450_device = {
    "National Semiconductor NS16450(-compatible) UART",
    0,
    SERIAL_NS16450,
    serial_init, serial_close, NULL,
    { NULL }, serial_speed_changed, NULL,
    NULL
};

const device_t ns16550_device = {
    "National Semiconductor NS16550(-compatible) UART",
    0,
    SERIAL_NS16550,
    serial_init, serial_close, NULL,
    { NULL }, serial_speed_changed, NULL,
    NULL
};
