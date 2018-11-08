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
 * Version:	@(#)serial.h	1.0.8	2018/11/04
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef EMU_SERIAL_H
# define EMU_SERIAL_H


#define SERIAL_8250	0
#define SERIAL_NS16540	1
#define SERIAL_NS16550	2

/* Default settings for the standard ports. */
#define SERIAL1_ADDR		0x03f8
#define SERIAL1_IRQ		4
#define SERIAL2_ADDR		0x02f8
#define SERIAL2_IRQ		3


struct serial_device_s;
struct serial_s;

typedef struct serial_s
{
    uint8_t lsr, thr, mctrl, rcr,
	    iir, ier, lcr, msr;
    uint8_t dlab1, dlab2;
    uint8_t dat;
    uint8_t int_status;
    uint8_t scratch;
    uint8_t fcr;

    uint8_t irq, type,
	    inst;
    uint16_t base_address;

    uint8_t fifo_len, fifo_enabled;
    uint8_t rcvr_fifo_pos, rcvr_fifo[14];
    uint8_t xmit_fifo_pos, xmit_fifo[14];

    int64_t recieve_delay;

    struct serial_device_s	*sd;
} serial_t;

typedef struct serial_device_s
{
    void (*rcr_callback)(struct serial_s *serial, void *p);
    void (*dev_write)(struct serial_s *serial, void *p, uint8_t data);
    void *priv;
    serial_t *serial;
} serial_device_t;


extern serial_t *	serial_attach(int port,
			      void (*rcr_callback)(struct serial_s *serial, void *p),
			      void (*dev_write)(struct serial_s *serial, void *p, uint8_t data),
			      void *priv);
extern void	serial_remove(serial_t *dev);
extern void	serial_setup(serial_t *dev, uint16_t addr, int irq);
extern void	serial_clear_fifo(serial_t *dev);
extern void	serial_write_fifo(serial_t *dev, uint8_t dat);

extern const device_t	i8250_device;
extern const device_t	ns16540_device;
extern const device_t	ns16550_device;


#endif	/*EMU_SERIAL_H*/
