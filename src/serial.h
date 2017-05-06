/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the SERIAL card.
 *
 * Version:	@(#)serial.h	1.0.1	2017/04/14
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef SERIAL_H
# define SERIAL_H


typedef struct _serial_ {
    uint16_t	port;
    int16_t	irq;

    uint8_t	lsr, thr, mctrl, rcr, iir, ier, lcr, msr;
    uint8_t	dlab1, dlab2;
    uint8_t	dat;
    uint8_t	int_status;
    uint8_t	scratch;
    uint8_t	fcr;

    void	(*rcr_callback)(struct _serial_ *, void *);
    void	*rcr_callback_p;

    uint8_t	hold;
    uint8_t	fifo[256];
    int		fifo_read, fifo_write;

    int		receive_delay;

    void	*bh;
} SERIAL;


extern void serial1_init(uint16_t addr, int irq);
extern void serial2_init(uint16_t addr, int irq);
extern void serial1_set(uint16_t addr, int irq);
extern void serial2_set(uint16_t addr, int irq);
extern void serial1_remove();
extern void serial2_remove();

extern void serial_reset();
extern SERIAL *serial_attach(int, void *, void *);
extern void serial_write_fifo(SERIAL *, uint8_t);


#endif	/*SERIAL_H*/
