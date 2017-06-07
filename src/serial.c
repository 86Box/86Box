/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of NS8250-series UART devices.
 *
 *		The original IBM-PC design did not have any serial ports of
 *		any kind. Rather, these were offered as add-on devices, most
 *		likely because a) most people did not need one at the time,
 *		and, b) this way, IBM could make more money off them.
 *
 *		So, for the PC, the offerings were for an IBM Asynchronous
 *		Communications Adapter, and, later, a model for synchronous
 *		communications.
 *
 *		The "Async Adapter" was based on the NS8250 UART chip, and
 *		is what we now call the "serial" or "com" port of the PC.
 *
 *		Of course, many system builders came up with similar boards,
 *		and even more boards were designed where several I/O functions
 *		were combined into a single board: the Multi-I/O adapters.
 *		Initially, these had all the chips as-is, but later many of
 *		these functions were integrated into a single MIO chip.
 *
 *		This file implements the standard NS8250 series of chips, with
 *		support for the later (16450 and 16550) FIFO additions. On the
 *		lower half of the driver, we interface to the host system's
 *		serial ports for real-world access.
 *
 *		Based on the 86Box serial port driver as a framework.
 *
 * Version:	@(#)serial.c	1.0.7	2017/06/04
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdarg.h>
#include "ibm.h"
#include "io.h"
#include "pic.h"
#include "timer.h"
#include "serial.h"
#include "plat_serial.h"


#define NUM_SERIAL	2		/* we support 2 ports */


enum {
    SERINT_LSR = 1,
    SERINT_RECEIVE = 2,
    SERINT_TRANSMIT = 4,
    SERINT_MSR = 8
};


/* IER register bits. */
#define IER_RDAIE	(0x01)
#define IER_THREIE	(0x02)
#define IER_RXLSIE	(0x04)
#define IER_MSIE	(0x08)
#define IER_SLEEP	(0x10)		/* NS16750 */
#define IER_LOWPOWER	(0x20)		/* NS16750 */
#define IER_MASK	(0x0f)		/* not including SLEEP|LOWP */

/* IIR register bits. */
#define IIR_IP		(0x01)
#define IIR_IID		(0x0e)
# define IID_IDMDM	(0x00)
# define IID_IDTX	(0x02)
# define IID_IDRX	(0x04)
# define IID_IDERR	(0x06)
# define IID_IDTMO	(0x0c)
#define IIR_IIRFE	(0xc0)
# define IIR_FIFO64	(0x20)
# define IIR_FIFOBAD	(0x80)		/* 16550 */
# define IIR_FIFOENB	(0xc0)

/* FCR register bits. */
#define FCR_FCRFE	(0x01)
#define FCR_RFR		(0x02)
#define FCR_TFR		(0x04)
#define FCR_SELDMA1	(0x08)
#define FCR_FENB64	(0x20)		/* 16750 */
#define FCR_RTLS	(0xc0)
# define FCR_RTLS1	(0x00)
# define FCR_RTLS4	(0x40)
# define FCR_RTLS8	(0x80)
# define FCR_RTLS14	(0xc0)

/* LCR register bits. */
#define LCR_WLS		(0x03)
# define WLS_BITS5	(0x00)
# define WLS_BITS6	(0x01)
# define WLS_BITS7	(0x02)
# define WLS_BITS8	(0x03)
#define LCR_SBS		(0x04)
#define LCR_PE		(0x08)
#define LCR_EP		(0x10)
#define LCR_PS		(0x20)
# define PAR_NONE	(0x00)
# define PAR_EVEN	(LCR_PE | LCR_EP)
# define PAR_ODD	(LCR_PE)
# define PAR_MARK	(LCR_PE | LCR_PS)
# define PAR_SPACE	(LCR_PE | LCR_PS | LCR_EP)
#define LCR_BC		(0x40)
#define LCR_DLAB	(0x80)

/* MCR register bits. */
#define MCR_DTR		(0x01)
#define MCR_RTS		(0x02)
#define MCR_OUT1	(0x04)		/* 8250 */
#define MCR_OUT2	(0x08)		/* 8250, INTEN on IBM-PC */
#define MCR_LMS		(0x10)
#define MCR_AUTOFLOW	(0x20)		/* 16750

/* LSR register bits. */
#define	LSR_DR		(0x01)
#define	LSR_OE		(0x02)
#define	LSR_PE		(0x04)
#define	LSR_FE		(0x08)
#define	LSR_BI		(0x10)
#define	LSR_THRE	(0x20)
#define	LSR_TEMT	(0x40)
#define	LSR_RXFE	(0x80)

/* MSR register bits. */
#define MSR_DCTS	(0x01)
#define MSR_DDSR	(0x02)
#define MSR_TERI	(0x04)
#define MSR_DDCD	(0x08)
#define MSR_CTS		(0x10)
#define MSR_DSR		(0x20)
#define MSR_RI		(0x40)
#define MSR_DCD		(0x80)
#define MSR_MASK	(0x0f)


static SERIAL	ports[NUM_SERIAL];	/* serial port data */
       int	serial_do_log;


static void
serial_log(int lvl, const char *fmt, ...)
{
#ifdef ENABLE_SERIAL_LOG
    va_list ap;

    if (serial_do_log >= lvl) {
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
    }
#endif
}


static void
update_ints(SERIAL *sp)
{
    int stat = 0;
        
    sp->iir = IIR_IP;
    if ((sp->ier & IER_RXLSIE) && (sp->int_status & SERINT_LSR)) {
	/* Line Status interrupt. */
	stat = 1;
	sp->iir = IID_IDERR;
    } else if ((sp->ier & IER_RDAIE) && (sp->int_status & SERINT_RECEIVE)) {
	/* Received Data available. */
	stat = 1;
	sp->iir = IID_IDRX;
    } else if ((sp->ier & IER_THREIE) && (sp->int_status & SERINT_TRANSMIT)) {
	/* Transmit Data empty. */
	stat = 1;
	sp->iir = IID_IDTX;
    } else if ((sp->ier & IER_MSIE) && (sp->int_status & SERINT_MSR)) {
	/* Modem Status interrupt. */
	stat = 1;
	sp->iir = IID_IDMDM;
    }

    /* Raise or clear the level-based IRQ. */
    if (stat && ((sp->mctrl & MCR_OUT2) || PCJR))
	picintlevel(1 << sp->irq);               
      else
	picintc(1 << sp->irq);
}


/* Fake interrupt generator, needed for Serial Mouse. */
static void
serial_timer(void *priv)
{
    SERIAL *sp = (SERIAL *)priv;

    sp->receive_delay = 0;

    if (sp->fifo_read != sp->fifo_write) {
	sp->lsr |= LSR_DR;
	sp->int_status |= SERINT_RECEIVE;
	update_ints(sp);
    }
}


/* Write data to the (input) FIFO. Used by MOUSE driver. */
void
serial_write_fifo(SERIAL *sp, uint8_t dat)
{
    /* Stuff data into FIFO. */
    sp->fifo[sp->fifo_write] = dat;
    sp->fifo_write = (sp->fifo_write + 1) & 0xFF;

    if (! (sp->lsr & LSR_DR)) {
	sp->lsr |= LSR_DR;
	sp->int_status |= SERINT_RECEIVE;
	update_ints(sp);
    }
}


static uint8_t
read_fifo(SERIAL *sp)
{
    if (sp->fifo_read != sp->fifo_write) {
	sp->dat = sp->fifo[sp->fifo_read];
	sp->fifo_read = (sp->fifo_read + 1) & 0xFF;
    }

    return(sp->dat);
}


/* Handle a WRITE operation to one of our registers. */
static void
serial_write(uint16_t addr, uint8_t val, void *priv)
{
    SERIAL *sp = (SERIAL *)priv;
    uint8_t wl, sb, pa;
    uint16_t baud;
    long speed;

#if ENABLE_SERIAL_LOG
    serial_log(2, "Serial%d: write(%04x, %02x)\n", sp->port, addr, val);
#endif
    switch (addr & 0x07) {
	case 0:		/* DATA / DLAB1 */
		if (sp->lcr & LCR_DLAB) {
			sp->dlab1 = val;
			return;
		}
		sp->thr = val;
		if (sp->bh != NULL) {
			/* We are linked, so send to BH layer. */
			bhtty_write((BHTTY *)sp->bh, sp->thr);

			/* The WRITE completed, we are ready for more. */
			sp->lsr |= LSR_THRE;
			sp->int_status |= SERINT_TRANSMIT;
			update_ints(sp);
		} else {
			/* Not linked. Just fake LOOPBACK mode. */
			if (! (sp->mctrl & MCR_LMS))
                        	serial_write_fifo(sp, val);
		}

		if (sp->mctrl & MCR_LMS) {
			/* Echo data back to RX. */
                        serial_write_fifo(sp, val);
		}
		break;

	case 1:		/* IER / DLAB2 */
		if (sp->lcr & LCR_DLAB) {
			sp->dlab2 = val;
			return;
		}
		sp->ier = (val & IER_MASK);
		update_ints(sp);
		break;

	case 2:		/* FCR */
		sp->fcr = val;
		break;

	case 3:		/* LCR */
		if ((sp->lcr & LCR_DLAB) && !(val & LCR_DLAB)) {
			/* We dropped DLAB, so handle baudrate. */
			baud = ((sp->dlab2<<8) | sp->dlab1);
			if (baud > 0) {
				speed = 115200UL/baud;
#ifdef ENABLE_SERIAL_LOG
				serial_log(2, "Serial%d: divisor %u, baudrate %ld\n",
						sp->port, baud, speed);
#endif
				if ((sp->bh != NULL) && (speed > 0))
					bhtty_speed((BHTTY *)sp->bh, speed);
			} else {
#ifdef ENABLE_SERIAL_LOG
				serial_log(1, "Serial%d: divisor %u invalid!\n",
							sp->port, baud);
#endif
			}
		}
		wl = (val & LCR_WLS) + 5;		/* databits */
		sb = (val & LCR_SBS) ? 2 : 1;		/* stopbits */
		pa = (val & (LCR_PE|LCR_EP|LCR_PS)) >> 3;
#ifdef ENABLE_SERIAL_LOG
		serial_log(2, "Serial%d: WL=%d SB=%d PA=%d\n", sp->port, wl, sb, pa);
#endif
		if (sp->bh != NULL)
			bhtty_params((BHTTY *)sp->bh, wl, pa, sb);
		sp->lcr = val;
		break;

	case 4:
		if ((val & MCR_RTS) && !(sp->mctrl & MCR_RTS)) {
			/*
			 * This is old code for use by the Serial Mouse
			 * driver. If the user toggles RTS, serial mice
			 * are expected to send an ID, to inform any
			 * enumerator there 'is' something.
			 */
			if (sp->rts_callback) {
				sp->rts_callback(sp->rts_callback_p);
#ifdef ENABLE_SERIAL_LOG
				serial_log(1, "RTS raised; sending ID\n");
#endif
			}
		}

		if ((val & MCR_OUT2) && !(sp->mctrl & MCR_OUT2)) {
			if (sp->bh != NULL) {
				/* Linked, start host port. */
				(void)bhtty_active(sp->bh, 1);
			} else {
				/* Not linked, start RX timer. */
				timer_add(serial_timer,
					  &sp->receive_delay,
					  &sp->receive_delay, sp);

				/* Fake CTS, DSR and DCD (for now.) */
				sp->msr = (MSR_CTS | MSR_DCTS |
					   MSR_DSR | MSR_DDSR |
					   MSR_DCD | MSR_DDCD);
				sp->int_status |= SERINT_MSR;
				update_ints(sp);
			}
		}
		sp->mctrl = val;
		if (val & MCR_LMS) {	/* loopback mode */
			uint8_t new_msr;

			/*FIXME: WTF does this do?? --FvK */
			new_msr = (val & 0x0c) << 4;
			new_msr |= (val & MCR_RTS) ? MCR_LMS : 0;
			new_msr |= (val & MCR_DTR) ? MCR_AUTOFLOW : 0;

			if ((sp->msr ^ new_msr) & 0x10)
				new_msr |= MCR_DTR;
			if ((sp->msr ^ new_msr) & 0x20)
				new_msr |= MCR_RTS;
			if ((sp->msr ^ new_msr) & 0x80)
				new_msr |= 0x08;
			if ((sp->msr & 0x40) && !(new_msr & 0x40))
				new_msr |= 0x04;

			sp->msr = new_msr;
		}
		break;

	case 5:
		sp->lsr = val;
		if (sp->lsr & LSR_DR)
			sp->int_status |= SERINT_RECEIVE;
		if (sp->lsr & 0x1e)
			sp->int_status |= SERINT_LSR;
		if (sp->lsr & LSR_THRE)
			sp->int_status |= SERINT_TRANSMIT;
		update_ints(sp);
		break;

	case 6:
		sp->msr = val;
		if (sp->msr & MSR_MASK)
			sp->int_status |= SERINT_MSR;
		update_ints(sp);
		break;

	case 7:
		sp->scratch = val;
		break;
    }
}


/* BHTTY READ COMPLETE handler. */
static void
serial_rd_done(void *arg, int num)
{
    SERIAL *sp = (SERIAL *)arg;

    /* We can do at least 'num' bytes.. */
    while (num-- > 0) {
	/* Get a byte from them. */
	if (bhtty_read(sp->bh, &sp->hold, 1) < 0) break;

	/* Stuff it into the FIFO and set intr. */
	serial_write_fifo(sp, sp->hold);
    }
}


/* Handle a READ operation from one of our registers. */
static uint8_t
serial_read(uint16_t addr, void *priv)
{
    SERIAL *sp = (SERIAL *)priv;
    uint8_t ret = 0x00;

    switch (addr&0x07) {
	case 0:		/* DATA / DLAB1 */
		if (sp->lcr & LCR_DLAB) {
			ret = sp->dlab1;
		} else {
			sp->lsr &= ~LSR_DR;
			sp->int_status &= ~SERINT_RECEIVE;
			update_ints(sp);
			ret = read_fifo(sp);
			if ((sp->bh == NULL) &&
			    (sp->fifo_read != sp->fifo_write))
				sp->receive_delay = 1000 * TIMER_USEC;
		}
		break;

	case 1:		/* LCR / DLAB2 */
		ret = (sp->lcr & LCR_DLAB) ? sp->dlab2 : sp->ier;
		break;

	case 2: 	/* IIR */
		ret = sp->iir;
		if ((ret & IIR_IID) == IID_IDTX) {
			sp->int_status &= ~SERINT_TRANSMIT;
			update_ints(sp);
		}
		if (sp->fcr & 0x01)
			ret |= 0xc0;
		break;

	case 3:		/* LCR */
		ret = sp->lcr;
		break;

	case 4:		/* MCR */
		ret = sp->mctrl;
		break;

	case 5:		/* LSR */
		if (sp->lsr & LSR_THRE)
			sp->lsr |= LSR_TEMT;
		sp->lsr |= LSR_THRE;
		ret = sp->lsr;
		if (sp->lsr & 0x1f)
			sp->lsr &= ~0x1e;
#if 0
		sp->lsr |= (LSR_THRE | LSR_TEMT);
#endif
		sp->int_status &= ~SERINT_LSR;
		update_ints(sp);
		break;

	case 6:
		ret = sp->msr;
		sp->msr &= ~0x0f;
		sp->int_status &= ~SERINT_MSR;
		update_ints(sp);
		break;

	case 7:
		ret = sp->scratch;
		break;
    }

    return(ret);
}


/* Set up a serial port for use. */
void
serial_setup(int port, uint16_t addr, int irq)
{
    SERIAL *sp;

#ifdef ENABLE_SERIAL_LOG
    serial_log(0, "Serial%d: I/O=%04x, IRQ=%d\n", port, addr, irq);
#endif

    /* Grab the desired port block. */
    sp = &ports[port-1];

    /* Set up the basic info. */
    if (sp->addr != 0x0000) {
	/* Unlink the previous handler. Just in case. */
	io_removehandler(sp->addr, 8,
			 serial_read, NULL, NULL, serial_write, NULL, NULL, sp);
    }
    sp->addr = addr;
    sp->irq = irq;

    /* Request an I/O range. */
    io_sethandler(sp->addr, 8,
		  serial_read, NULL, NULL, serial_write, NULL, NULL, sp);
}


/* Release all resources held by a serial port. */
void
serial_remove(int port)
{
    SERIAL *sp;

    /* Grab the desired port block. */
    sp = &ports[port-1];

    // FIXME: stop timer, if enabled!

    /* Close the host device. */
    if (sp->bh != NULL)
	(void)serial_link(port, NULL);

    /* Release our I/O range. */
    if (sp->addr != 0x0000) {
	io_removehandler(sp->addr, 8,
			 serial_read, NULL, NULL, serial_write, NULL, NULL, sp);
    }
    sp->addr = 0x0000;
    sp->irq = 0;
}


/* Initialize the serial ports. */
void
serial_init(void)
{
    SERIAL *sp;
    int i;

#if ENABLE_SERIAL_LOG
    serial_do_log = ENABLE_SERIAL_LOG;
#endif

    /* FIXME: we should probably initialize the platform module here. */

    /* Initialize each port. */
    for (i=0; i<NUM_SERIAL; i++) {
	sp = &ports[i];
	memset(sp, 0x00, sizeof(SERIAL));
	sp->port = (i+1);

	if (i == 0)
		serial_setup(sp->port, SERIAL1_ADDR, SERIAL1_IRQ);
	  else
		serial_setup(sp->port, SERIAL2_ADDR, SERIAL2_IRQ);
    }

#ifdef WALTJE
    /* Link to host port. */
    serial_link(1, "COM1");
    serial_link(2, "COM2");
#endif
}


/*
 * Reset the serial ports.
 *
 * This should be a per-port function.
 */
void
serial_reset(void)
{
    SERIAL *sp;
    int i;

    for (i=0; i<NUM_SERIAL; i++) {
	sp = &ports[i];

	sp->iir = sp->ier = sp->lcr = sp->mctrl = 0x00;
	sp->fifo_read = sp->fifo_write = 0x00;
    }
}


/* Link a serial port to a host (serial) port. */
int
serial_link(int port, char *arg)
{
    SERIAL *sp;
    BHTTY *bh;

    /* Grab the desired port block. */
    sp = &ports[port-1];

    if (arg != NULL) {
	/* Make sure we're not already linked. */
	if (sp->bh != NULL) {
#if ENABLE_SERIAL_LOG
		serial_log(0, "Serial%d already linked!\n", port);
#endif
		return(-1);
	}

	/* Request a port from the host system. */
	bh = bhtty_open(arg, 0);
	if (bh == NULL) {
#if ENABLE_SERIAL_LOG
		serial_log(0, "Serial%d unable to link to '%s' !\n", port, arg);
#endif
		return(-1);
	}
	sp->bh = bh;

	/* Set up bottom-half I/O callback info. */
	bh->rd_done = serial_rd_done;
	bh->rd_arg = sp;
    } else {
	/* If we are linked, unlink it. */
	if (sp->bh != NULL) {
		bhtty_close((BHTTY *)sp->bh);
		sp->bh = NULL;
	}

    }

    return(0);
}


/* Attach another device (MOUSE) to a serial port. */
SERIAL *
serial_attach(int port, void *func, void *arg)
{
    SERIAL *sp;

    /* Grab the desired port block. */
    sp = &ports[port-1];

    /* Set up callback info. */
    sp->rts_callback = func;
    sp->rts_callback_p = arg;

    return(sp);
}
