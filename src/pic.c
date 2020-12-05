/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel PIC chip emulation, partially
 *		ported from reenigne's XTCE.
 *
 * Authors:	Andrew Jenner, <https://www.reenigne.org>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2015-2020 Andrew Jenner.
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
#include "cpu.h"
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/device.h>
#include <86box/apm.h>
#include <86box/nvr.h>
#include <86box/acpi.h>


enum
{
    STATE_NONE = 0,
    STATE_ICW2,
    STATE_ICW3,
    STATE_ICW4
};


pic_t		pic, pic2;


static pc_timer_t	pic_timer;

static int	shadow = 0, elcr_enabled = 0,
		tmr_inited = 0, latched = 0;


#ifdef ENABLE_PIC_LOG
int pic_do_log = ENABLE_PIC_LOG;


static void
pic_log(const char *fmt, ...)
{
    va_list ap;

    if (pic_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define pic_log(fmt, ...)
#endif


void
pic_elcr_write(uint16_t port, uint8_t val, void *priv)
{
    pic_t *dev = (pic_t *) priv;

    pic_log("ELCR%i: WRITE %02X\n", port & 1, val);

    if (port & 1)
	val &= 0xde;
    else
	val &= 0xf8;

    dev->elcr = val;

    pic_log("ELCR %i: %c %c %c %c %c %c %c %c\n",
	    port & 1,
	    (val & 1) ? 'L' : 'E',
	    (val & 2) ? 'L' : 'E',
	    (val & 4) ? 'L' : 'E',
	    (val & 8) ? 'L' : 'E',
	    (val & 0x10) ? 'L' : 'E',
	    (val & 0x20) ? 'L' : 'E',
	    (val & 0x40) ? 'L' : 'E',
	    (val & 0x80) ? 'L' : 'E');
}


uint8_t
pic_elcr_read(uint16_t port, void *priv)
{
    pic_t *dev = (pic_t *) priv;

    pic_log("ELCR%i: READ %02X\n", port & 1, dev->elcr);

    return dev->elcr;
}


int
pic_elcr_get_enabled(void)
{
    return elcr_enabled;
}


void
pic_elcr_set_enabled(int enabled)
{
    elcr_enabled = enabled;
}


void
pic_elcr_io_handler(int set)
{
    io_handler(set, 0x04d0, 0x0001,
	       pic_elcr_read, NULL, NULL,
	       pic_elcr_write, NULL, NULL, &pic);
    io_handler(set, 0x04d1, 0x0001,
	       pic_elcr_read, NULL, NULL,
	       pic_elcr_write, NULL, NULL, &pic2);
}


static uint8_t
pic_cascade_mode(pic_t *dev)
{
    return !(dev->icw1 & 2);
}


static __inline uint8_t
pic_slave_on(pic_t *dev, int channel)
{
    pic_log("pic_slave_on(%i): %i, %02X, %02X\n", channel, pic_cascade_mode(dev), dev->icw4 & 0x0c, dev->icw3 & (1 << channel));

    return pic_cascade_mode(dev) && (dev->is_master || ((dev->icw4 & 0x0c) == 0x0c)) &&
	   (dev->icw3 & (1 << channel));
}


static __inline int
find_best_interrupt(pic_t *dev)
{
    uint8_t b, s;
    int i, j;
    int is_at, ret = -1;

    for (i = 0; i < 8; i++) {
	j = (i + dev->priority) & 7;
	b = 1 << j;
	s = (dev->icw4 & 0x10) && pic_slave_on(dev, j);

	if ((dev->isr & b) && !dev->special_mask_mode && !s)
		break;
	if ((dev->state == 0) && ((dev->irr & ~dev->imr) & b) && (((dev->isr & b) == 0x00) || s)) {
		ret = j;
		break;
	}
	if ((dev->isr & b) && !dev->special_mask_mode && s)
		break;
    }

    dev->interrupt = (ret == -1) ? 7 : ret;

    is_at = IS_AT(machine);
    if (is_at && (ret != -1) && (cpu_fast_off_flags & (1 << dev->interrupt)))
	cpu_fast_off_count = cpu_fast_off_val + 1;

    return ret;
}


static __inline void
pic_update_pending(void)
{
    int is_at = IS_AT(machine);

    if (is_at) {
	pic2.int_pending = (find_best_interrupt(&pic2) != -1);

	if (pic2.int_pending)
		pic.irr |= (1 << pic2.icw3);
	else
		pic.irr &= ~(1 << pic2.icw3);

	pic.int_pending = (find_best_interrupt(&pic) != -1);
	return;
    }

    if (find_best_interrupt(&pic) != -1) {
	latched++;
	if (latched == 1)
		timer_on_auto(&pic_timer, is_at ? 0.3 : 0.35);
		/* 300 ms on AT+, 350 ns on PC/PCjr/XT */
    } else if (latched == 0)
	pic.int_pending = 0;
}


static void
pic_callback(void *priv)
{
    pic_t *dev = (pic_t *) priv;

    dev->int_pending = 1;

    latched--;
    if (latched > 0)
	timer_on_auto(&pic_timer, 0.35);
}


void
pic_reset()
{
    int is_at = IS_AT(machine);

    memset(&pic, 0, sizeof(pic_t));
    memset(&pic2, 0, sizeof(pic_t));

    pic.is_master = 1;

    if (is_at)
	pic.slaves[2] = &pic2;

    if (tmr_inited)
	timer_on_auto(&pic_timer, 0.0);
    memset(&pic_timer, 0x00, sizeof(pc_timer_t));
    timer_add(&pic_timer, pic_callback, &pic, 0);
    tmr_inited = 1;
}


void
pic_set_shadow(int sh)
{
    shadow = sh;
}


static uint8_t
pic_level_triggered(pic_t *dev, int irq)
{
    if (elcr_enabled)
	return !!(dev->elcr & (1 << irq));
    else
	return !!(dev->icw1 & 8);
}


int
picint_is_level(int irq)
{
    return pic_level_triggered(((irq > 7) ? &pic2 : &pic), irq & 7);
}


static void
pic_acknowledge(pic_t *dev)
{
    int pic_int = dev->interrupt;
    int pic_int_num = 1 << pic_int;

    dev->isr |= pic_int_num;
    if (!pic_level_triggered(dev, pic_int) || !(dev->lines & pic_int_num))
	dev->irr &= ~pic_int_num;
}


/* Find IRQ for non-specific EOI (either by command or automatic) by finding the highest IRQ
   priority with ISR bit set, that is also not masked if the PIC is in special mask mode. */
static uint8_t
pic_non_specific_find(pic_t *dev)
{
    int i, j;
    uint8_t b, irq = 0xff;

    for (i = 0; i < 8; i++) {
	j = (i + dev->priority) & 7;
	b = (1 << j);

	if ((dev->isr & b) && (!dev->special_mask_mode || !(dev->imr & b))) {
		irq = j;
		break;
	}
    }

    return irq;
}


/* Do the EOI and rotation, if either is requested, on the given IRQ. */
static void
pic_action(pic_t *dev, uint8_t irq, uint8_t eoi, uint8_t rotate)
{
    uint8_t b = (1 << irq);

    if (irq != 0xff) {
	if (eoi)
		dev->isr &= ~b;
	if (rotate)
		dev->priority = (irq + 1) & 7;

	pic_update_pending();
    }
}


/* Automatic non-specific EOI. */
static __inline void
pic_auto_non_specific_eoi(pic_t *dev)
{
    uint8_t irq;

    if (dev->icw4 & 2) {
	irq = pic_non_specific_find(dev);

	pic_action(dev, irq, 1, dev->auto_eoi_rotate);
    }
}


/* Do the PIC command specified by bits 7-5 of the value written to the OCW2 register. */
static void
pic_command(pic_t *dev)
{
    uint8_t irq = 0xff;

    if (dev->ocw2 & 0x60) {	/* SL and/or EOI set */
	if (dev->ocw2 & 0x40)	/* SL set, specific priority level */
		irq = (dev->ocw2 & 0x07);
	else			/* SL clear, non-specific priority level (find highest with ISR set) */
		irq = pic_non_specific_find(dev);

        pic_action(dev, irq, dev->ocw2 & 0x20, dev->ocw2 & 0x80);
    } else			/* SL and EOI clear */
	dev->auto_eoi_rotate = !!(dev->ocw2 & 0x80);
}


uint8_t
pic_read(uint16_t addr, void *priv)
{
    pic_t *dev = (pic_t *) priv;

    if (shadow) {
	/* VIA PIC shadow read */
	if (addr & 0x0001)
		dev->data_bus  = ((dev->icw2 & 0xf8) >> 3) << 0;
	else {
		dev->data_bus  = ((dev->ocw3 & 0x20) >> 5) << 4;
		dev->data_bus |= ((dev->ocw2 & 0x80) >> 7) << 3;
		dev->data_bus |= ((dev->icw4 & 0x10) >> 4) << 2;
		dev->data_bus |= ((dev->icw4 & 0x02) >> 1) << 1;
		dev->data_bus |= ((dev->icw4 & 0x08) >> 3) << 0;
	}
    } else {
	/* Standard 8259 PIC read */
	if (dev->ocw3 & 0x04) {
		if (dev->int_pending) {
			dev->data_bus = 0x80 | (dev->interrupt & 7);
			pic_acknowledge(dev);
			dev->int_pending = 0;
			pic_update_pending();
		} else
			dev->data_bus = 0x00;
		dev->ocw3 &= ~0x04;
	} else if (addr & 0x0001)
		dev->data_bus = dev->imr;
	else if (dev->ocw3 & 0x02) {
		if (dev->ocw3 & 0x01)
			dev->data_bus = dev->isr;
		else
			dev->data_bus = dev->irr;
	}
	/* If A0 = 0, VIA shadow is disabled, and poll mode is disabled,
	   simply read whatever is currently on the data bus. */
    }

    pic_log("pic_read(%04X, %08X) = %02X\n", addr, priv, dev->data_bus);

    return dev->data_bus;
}


static void
pic_write(uint16_t addr, uint8_t val, void *priv)
{
    pic_t *dev = (pic_t *) priv;

    pic_log("pic_write(%04X, %02X, %08X)\n", addr, val, priv);

    dev->data_bus = val;

    if (addr & 0x0001) {
	switch (dev->state) {
		case STATE_ICW2:
			dev->icw2 = val;
			if (pic_cascade_mode(dev))
				dev->state = STATE_ICW3;
			else
				dev->state = (dev->icw1 & 1) ? STATE_ICW4 : STATE_NONE;
			break;
		case STATE_ICW3:
			dev->icw3 = val;
			dev->state = (dev->icw1 & 1) ? STATE_ICW4 : STATE_NONE;
			break;
		case STATE_ICW4:
			dev->icw4 = val;
			dev->state = STATE_NONE;
			break;
		case STATE_NONE:
			dev->imr = val;
			pic_update_pending();
			break;
	}
    } else {
	if (val & 0x10) {
		/* Treat any write with any of the bits 7 to 5 set as invalid if PCI. */
		if (PCI && (val & 0xe0))
			return;

		dev->icw1 = val;
		dev->icw2 = dev->icw3 = 0x00;
		if (!(dev->icw1 & 1))
			dev->icw4 = 0x00;
		dev->ocw2 = dev->ocw3 = 0x00;
		dev->irr = dev->lines;
		dev->imr = dev->isr = 0x00;
		dev->ack_bytes = dev->priority = 0x00;
		dev->auto_eoi_rotate = dev->special_mask_mode = 0x00;
		dev->interrupt = dev->int_pending = 0x00;
		dev->state = STATE_ICW2;
		pic_update_pending();
	} else if (val & 0x08) {
		dev->ocw3 = val;
		if (dev->ocw3 & 0x40)
			dev->special_mask_mode = !!(dev->ocw3 & 0x20);
	} else {
		dev->ocw2 = val;
		pic_command(dev);
	}
    }
}


void
pic_init(void)
{
    pic_reset();

    shadow = 0;
    io_sethandler(0x0020, 0x0002, pic_read, NULL, NULL, pic_write, NULL, NULL, &pic);
}


void
pic_init_pcjr(void)
{
    pic_reset();

    shadow = 0;
    io_sethandler(0x0020, 0x0008, pic_read, NULL, NULL, pic_write, NULL, NULL, &pic);
}


void
pic2_init(void)
{
    io_sethandler(0x00a0, 0x0002, pic_read, NULL, NULL, pic_write, NULL, NULL, &pic2);
    pic.slaves[2] = &pic2;
}


void
picint_common(uint16_t num, int level, int set)
{
    int i, raise;
    int is_at;
    uint8_t b, slaves = 0;

    is_at = IS_AT(machine);

    /* Make sure to ignore all slave IRQ's, and in case of AT+,
       translate IRQ 2 to IRQ 9. */
    for (i = 0; i < 8; i++) {
	b = (1 << i);
	raise = num & b;

	if (pic.icw3 & b) {
		slaves++;

		if (raise) {
			num &= ~b;
			if (is_at && (i == 2))
				num |= (1 << 9);
		}
	}
    }

    if (!slaves)
	num &= 0x00ff;

    if (!num) {
	pic_log("Attempting to %s null IRQ\n", set ? "raise" : "lower");
	return;
    }

    if (num & 0x0100)
	acpi_rtc_status = !!set;

    if (set) {
	if (num & 0xff00) {
		if (level)
			pic2.lines |= (num >> 8);

		pic2.irr |= (num >> 8);
	}

	if (num & 0x00ff) {
		if (level)
			pic.lines |= (num >> 8);

		pic.irr |= num;
	}
    } else {
	if (num & 0xff00) {
		pic2.lines &= ~(num >> 8);
		pic2.irr &= ~(num >> 8);
	}

	if (num & 0x00ff) {
		pic.lines &= ~num;
		pic.irr &= ~num;
	}
    }

    pic_update_pending();
}


void
picint(uint16_t num)
{
    picint_common(num, 0, 1);
}


void
picintlevel(uint16_t num)
{
    picint_common(num, 1, 1);
}


void
picintc(uint16_t num)
{
    picint_common(num, 0, 0);
}


static uint8_t
pic_i86_mode(pic_t *dev)
{
    return !!(dev->icw4 & 1);
}


static uint8_t
pic_irq_ack_read(pic_t *dev, int phase)
{
    pic_log("    pic_irq_ack_read(%08X, %i)\n", dev, phase);

    if (dev != NULL) {
	if (phase == 0) {
		pic_acknowledge(dev);
		if (pic_slave_on(dev, dev->interrupt))
			dev->data_bus = pic_irq_ack_read(dev->slaves[dev->interrupt], phase);
		else
			dev->data_bus = pic_i86_mode(dev) ? 0xff : 0xcd;
	} else if (pic_i86_mode(dev)) {
		dev->int_pending = 0;
		if (pic_slave_on(dev, dev->interrupt))
			dev->data_bus = pic_irq_ack_read(dev->slaves[dev->interrupt], phase);
		else
			dev->data_bus = dev->interrupt + (dev->icw2 & 0xf8);
		pic_auto_non_specific_eoi(dev);
	} else if (phase == 1) {
		if (pic_slave_on(dev, dev->interrupt))
			dev->data_bus = pic_irq_ack_read(dev->slaves[dev->interrupt], phase);
		else if (dev->icw1 & 0x04)
			dev->data_bus = (dev->interrupt << 2) + (dev->icw1 & 0xe0);
		else
			dev->data_bus = (dev->interrupt << 3) + (dev->icw1 & 0xc0);
	} else if (phase == 2) {
		dev->int_pending = 0;
		if (pic_slave_on(dev, dev->interrupt))
			dev->data_bus = pic_irq_ack_read(dev->slaves[dev->interrupt], phase);
		else
			dev->data_bus = dev->icw2;
		pic_auto_non_specific_eoi(dev);
	}
    }

    return dev->data_bus;
}


uint8_t
pic_irq_ack(void)
{
    int ret;

    ret = pic_irq_ack_read(&pic, pic.ack_bytes);
    pic.ack_bytes = (pic.ack_bytes + 1) % (pic_i86_mode(&pic) ? 2 : 3);

    if (pic.ack_bytes == 0)
	pic_update_pending();

    return ret;
}


int
picinterrupt()
{
    int i, ret = -1;

    if (pic.int_pending) {
	if (pic_slave_on(&pic, pic.interrupt) && !pic.slaves[pic.interrupt]->int_pending) {
		/* If we are on AT, IRQ 2 is pending, and we cannot find a pending IRQ on PIC 2, fatal out. */
		fatal("IRQ %i pending on AT without a pending IRQ on PIC %i (normal)\n", pic.interrupt, pic.interrupt);
		exit(-1);
		return -1;
	}

	if ((pic.interrupt == 0) && (pit2 != NULL))
		pit_ctr_set_gate(&pit2->counters[0], 0);

	/* Two ACK's - do them in a loop to avoid potential compiler misoptimizations. */
	for (i = 0; i < 2; i++) {
		ret = pic_irq_ack_read(&pic, pic.ack_bytes);
		pic.ack_bytes = (pic.ack_bytes + 1) % (pic_i86_mode(&pic) ? 2 : 3);

		if (pic.ack_bytes == 0)
			pic_update_pending();
	}
    }

    return ret;
}
