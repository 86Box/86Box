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
 * Version:	@(#)serial.c	1.0.2	2017/05/05
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#include "ibm.h"
#include "io.h"
#include "mouse.h"
#include "pic.h"
#include "serial.h"
#include "timer.h"
#include "win-serial.h"


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


static uint16_t	serial_addr[2] = { 0x3f8, 0x2f8 };
static int	serial_irq[2] = { 4, 3 };
static SERIAL	serial1, serial2;


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


/* BHTTY WRITE COMPLETE handler. */
static void
serial_wr_done(void *arg)
{
    SERIAL *sp = (SERIAL *)arg;

    /* The WRITE completed, we are ready for more. */
    sp->lsr |= LSR_THRE;
    sp->int_status |= SERINT_TRANSMIT;
    update_ints(sp);
}


/* Handle a WRITE operation to one of our registers. */
static void
serial_write(uint16_t addr, uint8_t val, void *priv)
{
    SERIAL *sp = (SERIAL *)priv;
    uint8_t wl, sb, pa;
    uint16_t baud;
    long speed;

    switch (addr & 0x07) {
	case 0:		/* DATA / DLAB1 */
		if (sp->lcr & LCR_DLAB) {
			sp->dlab1 = val;
			return;
		}
		sp->thr = val;
#if 0
		bhtty_write((BHTTY *)sp->bh, sp->thr, serial_wrdone, sp);
#else
		bhtty_write((BHTTY *)sp->bh, sp->thr);
		serial_wr_done(sp);
#endif
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
			baud = ((sp->dlab2 << 8) | sp->dlab1);
			speed = 115200UL/baud;
#if 1
			pclog("Serial: new divisor %u, baudrate %ld\n",
							baud, speed);
#endif
			bhtty_speed((BHTTY *)sp->bh, speed);
		}
		wl = (val & LCR_WLS) + 5;		/* databits */
		sb = (val & LCR_SBS) ? 2 : 1;		/* stopbits */
		pa = (val & (LCR_PE|LCR_EP|LCR_PS)) >> 3;
#if 1
		pclog("Serial: WL=%d SB=%d PA=%d\n", wl, sb, pa);
#endif
		bhtty_params((BHTTY *)sp->bh, wl, pa, sb);
		sp->lcr = val;
		break;

	case 4:
		if ((val & MCR_RTS) && !(sp->mctrl & MCR_RTS)) {
			/*
			 * This is old code for use by the Serial Mouse
			 * driver. If the user toggles RTS, any serial
			 * mouse is expected to send an 'M' character,
			 * to inform any enumerator there 'is' something.
			 */
			if (sp->rcr_callback) {
				sp->rcr_callback(sp, sp->rcr_callback_p);
#if 0
				pclog("RTS raised; sending M\n");
#endif
			}
		}

		if ((val & MCR_OUT2) && !(sp->mctrl & MCR_OUT2)) {
			/* Start up reading from the real port. */
			(void)bhtty_read((BHTTY *)sp->bh, &sp->hold, 1);
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
//pclog("%04x: %d bytes available: %02x (%c)\n",sp->port,num,sp->hold,sp->hold);

    /* Stuff the byte in the FIFO and set intr. */
    serial_write_fifo(sp, sp->hold);

    /* Start up the next read from the real port. */
    (void)bhtty_read((BHTTY *)sp->bh, &sp->hold, 1);
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
			break;
		}
		sp->lsr &= ~LSR_DR;
		sp->int_status &= ~SERINT_RECEIVE;
		update_ints(sp);
		ret = read_fifo(sp);
#if 0
		if (sp->fifo_read != sp->fifo_write)
			sp->receive_delay = 1000 * TIMER_USEC;
#endif
		break;

	case 1:		/* LCR / DLAB2 */
		if (sp->lcr & LCR_DLAB)
			ret = sp->dlab2;
		  else
			ret = sp->ier;
		break;

	case 2: 	/* IIR */
		ret = sp->iir;
		if ((ret & IIR_IID) == IID_IDTX) {
			sp->int_status &= ~SERINT_TRANSMIT;
			update_ints(sp);
		}
		if (sp->fcr & 1)
		{
			ret |= 0xc0;
		}
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


/*Tandy might need COM1 at 2f8*/
void
serial1_init(uint16_t addr, int irq)
{
    BHTTY *bh;

    memset(&serial1, 0x00, sizeof(serial1));

    pclog("Serial1, I/O=%04x, IRQ=%d, host ", addr, irq);

    /* Request a port from the host system. */
    bh = bhtty_open(BHTTY_PORT1, 0);	/*FIXME: from config! --FvK */
    if (bh == NULL) {
	return;
    }
    serial1.bh = bh;
    serial1.port = addr;
    serial1.irq = irq;
    serial1.rcr_callback = NULL;
    pclog("'%s'\n", bh->name);

    /* Set up bottom-half I/O callback info. */
    bh->rd_done = serial_rd_done;
    bh->rd_arg = &serial1;

    /* Request an I/O range. */
    io_sethandler(addr, 8,
		  serial_read, NULL, NULL, serial_write, NULL, NULL, &serial1);

#if 0
    timer_add(serial_receive_callback,
	      &serial1.receive_delay, &serial1.receive_delay, &serial1);
#endif

    serial_addr[0] = addr;
    serial_irq[0] = irq;
}


/* Release all resources held by the device. */
void
serial1_remove(void)
{
    /* Close the host device. */
    if (serial1.bh != NULL)
	bhtty_close((BHTTY *)serial1.bh);

    /* Release our I/O range. */
    io_removehandler(serial_addr[0], 8,
		serial_read, NULL, NULL, serial_write, NULL, NULL, &serial1);
}


void
serial1_set(uint16_t addr, int irq)
{
    void *temp;

#if 0
    pclog("serial1_set(%04X, %02X)\n", addr, irq);
#endif
    temp = serial1.bh;
    serial1.bh = NULL;
    serial1_remove();
    serial1.bh = temp;
    serial1.port = addr;
    serial1.irq = irq;

    io_sethandler(addr, 8,
		  serial_read, NULL, NULL, serial_write, NULL, NULL, &serial1);
    serial_addr[0] = addr;
    serial_irq[0] = irq;
}


void
serial2_init(uint16_t addr, int irq)
{
    BHTTY *bh;

    memset(&serial2, 0x00, sizeof(serial2));

    pclog("Serial2, I/O=%04x, IRQ=%d, host ", addr, irq);

    /* Request a port from the host system. */
    bh = bhtty_open(BHTTY_PORT2, 0);	/*FIXME: from config! --FvK */
    if (bh == NULL) {
	return;
    }
    serial2.bh = bh;
    serial2.port = addr;
    serial2.irq = irq;
    serial2.rcr_callback = NULL;
    pclog("'%s'\n", bh->name);

    /* Set up bottom-half I/O callback info. */
    bh->rd_done = serial_rd_done;
    bh->rd_arg = &serial2;

    /* Request an I/O range. */
    io_sethandler(addr, 8,
		  serial_read, NULL, NULL, serial_write, NULL, NULL, &serial2);

    serial_addr[1] = addr;
    serial_irq[1] = irq;
}


/* Release all resources held by the device. */
void
serial2_remove(void)
{
    /* Close the host device. */
    if (serial2.bh != NULL)
	bhtty_close((BHTTY *)serial2.bh);

    /* Release our I/O range. */
    io_removehandler(serial_addr[1], 8,
		serial_read, NULL, NULL, serial_write, NULL, NULL, &serial2);
}


void
serial2_set(uint16_t addr, int irq)
{
    void *temp;

#if 0
    pclog("serial2_set(%04X, %02X)\n", addr, irq);
#endif
    temp = serial2.bh;
    serial2.bh = NULL;
    serial2_remove();
    serial2.bh = temp;
    serial2.port = addr;
    serial2.irq = irq;

    io_sethandler(addr, 8,
		  serial_read, NULL, NULL, serial_write, NULL, NULL, &serial2);

    serial_addr[1] = addr;
    serial_irq[1] = irq;
}


/*
 * Reset the serial ports.
 *
 * This should be a per-port function.
 */
void
serial_reset(void)
{
    serial1.iir = serial1.ier = serial1.lcr = serial1.mctrl = 0;
    serial1.fifo_read = serial1.fifo_write = 0;

    serial2.iir = serial2.ier = serial2.lcr = serial2.mctrl = 0;
    serial2.fifo_read = serial2.fifo_write = 0;
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


/* Attach another device (MOUSE) to a serial port. */
SERIAL *
serial_attach(int port, void *func, void *arg)
{
    SERIAL *sp;

    if (port == 0)
	sp = &serial1;
      else
	sp = &serial2;

    /* Set up callback info. */
    sp->rcr_callback = func;
    sp->rcr_callback_p = arg;

    /* Create a timer to fake RX interrupts for mouse data. */
    timer_add(serial_timer,
	      &sp->receive_delay, &sp->receive_delay, sp);

    return(sp);
}
