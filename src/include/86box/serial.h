/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the NS8250/16450/16550 UART emulation.
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
#ifndef EMU_SERIAL_H
# define EMU_SERIAL_H


#define SERIAL_8250		0
#define SERIAL_8250_PCJR	1
#define SERIAL_NS16450		2
#define SERIAL_NS16550		3

/* Default settings for the standard ports. */
#define SERIAL1_ADDR		0x03f8
#define SERIAL1_IRQ		4
#define SERIAL2_ADDR		0x02f8
#define SERIAL2_IRQ		3
#define SERIAL3_ADDR		0x03e8
#define SERIAL3_IRQ		4
#define SERIAL4_ADDR		0x02e8
#define SERIAL4_IRQ		3


struct serial_device_s;
struct serial_s;

typedef struct serial_s
{
    uint8_t lsr, thr, mctrl, rcr,
	    iir, ier, lcr, msr,
	    dat, int_status, scratch, fcr,
	    irq, type, inst, transmit_enabled,
	    fifo_enabled, rcvr_fifo_len, bits, data_bits,
	    baud_cycles, rcvr_fifo_full, txsr, pad;

    uint16_t dlab, base_address;

    uint8_t rcvr_fifo_pos, xmit_fifo_pos,
	    pad0, pad1,
	    rcvr_fifo[16], xmit_fifo[16];

    pc_timer_t transmit_timer, timeout_timer;
    double clock_src, transmit_period;

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
extern void	serial_set_type(serial_t *dev, int type);
extern void	serial_setup(serial_t *dev, uint16_t addr, uint8_t irq);
extern void	serial_clear_fifo(serial_t *dev);
extern void	serial_write_fifo(serial_t *dev, uint8_t dat);
extern void	serial_set_next_inst(int ni);
extern void	serial_standalone_init(void);
extern void	serial_set_clock_src(serial_t *dev, double clock_src);
extern void	serial_reset_port(serial_t *dev);

extern const device_t	i8250_device;
extern const device_t	i8250_pcjr_device;
extern const device_t	ns16450_device;
extern const device_t	ns16550_device;


#endif	/*EMU_SERIAL_H*/
