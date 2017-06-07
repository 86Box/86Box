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
 * Version:	@(#)serial.h	1.0.4	2017/06/03
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_SERIAL_H
# define EMU_SERIAL_H


/* Default settings for the standard ports. */
#define SERIAL1_ADDR	0x03f8
#define SERIAL1_IRQ	4
#define SERIAL2_ADDR	0x02f8
#define SERIAL2_IRQ	3


typedef struct _serial_ {
    int8_t	port;			/* port number (1,2,..) */
    int8_t	irq;			/* IRQ channel used */
    uint16_t	addr;			/* I/O address used */

    uint8_t	lsr, thr, mctrl, rcr,	/* UART registers */
		iir, ier, lcr, msr;
    uint8_t	dlab1, dlab2;
    uint8_t	dat;
    uint8_t	int_status;
    uint8_t	scratch;
    uint8_t	fcr;

    /* Data for the RTS-toggle callback. */
    void	(*rts_callback)(void *);
    void	*rts_callback_p;

    uint8_t	hold;
    uint8_t	fifo[256];
    int		fifo_read, fifo_write;

    int		receive_delay;

    void	*bh;			/* BottomHalf handler */
} SERIAL;


/* Functions. */
extern void	serial_init(void);
extern void	serial_reset(void);
extern void	serial_setup(int port, uint16_t addr, int irq);
extern void	serial_remove(int port);
extern SERIAL	*serial_attach(int, void *, void *);
extern int	serial_link(int, char *);
extern void	serial_write_fifo(SERIAL *, uint8_t);


#endif	/*EMU_SERIAL_H*/
