/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Signetics SCN2661/SCN2651 EPCI -- Enhanced Programmable
 *          Communications Interface (USART), plus the identical
 *          SCN68661 CMOS part.
 *
 *          Based on Signetics SCN2651/SCN2661 emulation in MAME
 *          (src/devices/machine/scn_pci.cpp),
 *          copyright-holders: AJR.
 *
 *          Register model from the 1986 Signetics data book:
 *
 *            port+0  read  receive holding register  write transmit holding
 *            port+1  read  status register           write SYN1/SYN2/DLE
 *            port+2  read  mode register 1 or 2      write mode register 1/2
 *            port+3  read  command register          write command register
 *
 *          The SYN and mode ports are sequenced: the first access hits the
 *          first register, the next the second, and a third access wraps.
 *          Reading the command register rewinds both sequencers.
 *
 *          Two of the three parts from Table 1 are modelled, picked by the
 *          variant byte of device_t.local:
 *
 *            B   BRCLK 4.9152 MHz  45.5 50 75 110 134.5 150 300 600 1200
 *                                   1800 2000 2400 4800 9600 19200 38400
 *            C   BRCLK 5.0688 MHz  50 75 110 134.5 150 300 600 1200 1800
 *                                   2000 2400 3600 4800 7200 9600 19200
 *
 *          The A part is not modelled: it has no 45.5 and decodes 1000
 *          and 0101 as 1050 and 200.
 *
 *          What is not modelled: synchronous (BISYNC/SDLC) framing, the
 *          external clock pins (an external clock source is assumed to run
 *          at the BRG rate), the serial BREAK waveform (the command bit is
 *          passed to the backend as CHAR_COM_BREAK), and the transmitter-empty
 *          interrupt source -- INTR is only raised for received data so a level
 *          line cannot sit asserted forever.
 *
 * Authors: WNT50
 *
 *          Copyright 2026 WNT50.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/char.h>
#include <86box/serial.h>

#ifdef ENABLE_SCN26X1_LOG
int scn26x1_do_log = ENABLE_SCN26X1_LOG;

static void
scn26x1_log(const char *fmt, ...)
{
    va_list ap;

    if (scn26x1_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define scn26x1_log(fmt, ...)
#endif

/* Register select, A1:A0 on top of the base port. */
#define SCN_RDR 0x00 /* read:  receive holding register */
#define SCN_TDR 0x00 /* write: transmit holding register */
#define SCN_SR  0x01 /* read:  status register */
#define SCN_SYN 0x01 /* write: SYN1, SYN2, DLE (sequenced) */
#define SCN_MR  0x02 /* both:  mode register 1, then 2 */
#define SCN_CR  0x03 /* both:  command register */

/* Status register, table 8. */
#define SCN_SR_TXRDY 0x01 /* transmit holding register empty */
#define SCN_SR_RXRDY 0x02 /* received character waiting */
#define SCN_SR_TXEMT 0x04 /* transmit shift register empty */
#define SCN_SR_PE    0x08 /* parity / DLE detect */
#define SCN_SR_OE    0x10 /* overrun */
#define SCN_SR_FE    0x20 /* framing / SYN detect */
#define SCN_SR_DCD   0x40 /* 1 = DCD input low */
#define SCN_SR_DSR   0x80 /* 1 = DSR input low */

/* Command register, table 7. */
#define SCN_CR_TXEN     0x01
#define SCN_CR_DTR      0x02 /* 1 = DTR output low */
#define SCN_CR_RXEN     0x04
#define SCN_CR_BREAK    0x08 /* async: force break, sync: send DLE */
#define SCN_CR_ERRS     0x10 /* reset PE/OE/FE */
#define SCN_CR_RTS      0x20 /* 1 = RTS output low */
#define SCN_CR_MODE(x)  ((uint8_t) (((x) >> 6) & 0x03))

#define SCN_MODE_NORMAL 0
#define SCN_MODE_ECHO   1
#define SCN_MODE_LOCAL  2 /* local loopback */
#define SCN_MODE_REMOTE 3 /* remote loopback */

/* Mode register 1, table 5. */
#define SCN_MR1_CLOCK(x) ((uint8_t) ((x) & 0x03))
#define SCN_MR1_STOP(x)  ((uint8_t) (((x) >> 6) & 0x03))
#define SCN_MR1_LEN(x)   ((uint8_t) (((x) >> 2) & 0x03))
#define SCN_MR1_PAREN    0x10

/* `device_t.local` carries the COM port index in the low byte (0xff = the
   board owns the chip, no COM port) and the variant in the next byte, so
   the two can be OR-ed together the way device_add_params() does it. */
#define SCN26X1_PORT_MASK  0xff /* passed through device_add_params() */
#define SCN26X1_VARIANT_B  2
#define SCN26X1_VARIANT_C  3

typedef struct scn26x1_t {
    uint8_t  mr1, mr2, cr, sr;
    uint8_t  tdr;      /* transmit holding register */
    uint8_t  tsr;      /* transmit shift register */
    uint8_t  rdr;      /* receive holding register */
    uint8_t  syn[3];   /* SYN1, SYN2, DLE */
    int      mr_ptr;   /* 0 = next access is MR1, 1 = MR2 */
    int      syn_ptr;  /* position in the SYN1/SYN2/DLE sequence */
    int      thr_full; /* transmit holding register holds a character */
    int      tx_busy;  /* shift register is serializing */
    int      tx_emt;   /* shift register empty */
    int      dschg;    /* DSR or DCD changed state */
    int      rx_full;  /* a character waits in the holding register */
    int      dsr;      /* modem inputs, idle high */
    int      dcd;
    int      port;     /* COM port this chip is wired to */

    int      irq_mask; /* IRQ line as a PIC bit mask, 0 = not connected */
    int      irq_state;

    const double *rates; /* Table 1 row set: B or C */

    /* The chip's end of the seam; it owns the backend storage. */
    serial_endpoint_t ep;

    uint32_t modem;     /* modem lines driven by an in-machine device */
    uint8_t  modem_set; /* set once a device drives them */

    pc_timer_t timer;
    pc_timer_t rx_timer;
} scn26x1_t;

/* Table 1 of the data book. The A/B parts run from a 4.9152 MHz BRCLK and the
   C part from 5.0688 MHz, so the same MR2[3:0] code selects a different rate
   on each: 0x08 is 1200 on the B and 1800 on the C, 0x0B is 2400 against
   3600, 0x0D is 9600 against 7200. */
static const double scn26x1_rates_b[16] = {
    45.5, 50.0, 75.0, 110.0, 134.5, 150.0, 300.0, 600.0,
    1200.0, 1800.0, 2000.0, 2400.0, 4800.0, 9600.0, 19200.0, 38400.0
};

static const double scn26x1_rates_c[16] = {
    50.0, 75.0, 110.0, 134.5, 150.0, 300.0, 600.0, 1200.0,
    1800.0, 2000.0, 2400.0, 3600.0, 4800.0, 7200.0, 9600.0, 19200.0
};

/* Baud rate selected by MR2[3:0]. */
static double
scn26x1_baud(scn26x1_t *dev)
{
    double baud = 0.0;

    /* The rate table is picked from the variant in .local; an unknown
       variant leaves it unset, so fall back to the default rate
       rather than dereference a NULL table. */
    if (dev->rates != NULL)
        baud = dev->rates[dev->mr2 & 0x0f];

    if (baud <= 0.0)
        baud = 9600.0;

    return baud;
}

/* Bit time of the programmed rate, in microseconds. Mode register 2 bits 7-4
   pick internal or external clocks; the external pins are not modelled, so
   the bit rate generator value is used either way. */
static double
scn26x1_bit_time(scn26x1_t *dev)
{
    return 1000000.0 / scn26x1_baud(dev);
}

/* Start bit + data bits + optional parity + stop bits. */
static double
scn26x1_frame_bits(scn26x1_t *dev)
{
    int    data = SCN_MR1_LEN(dev->mr1) + 5;
    double bits = 1 + data;
    int    stop = SCN_MR1_STOP(dev->mr1);

    if (dev->mr1 & SCN_MR1_PAREN)
        bits++;

    if (SCN_MR1_CLOCK(dev->mr1) == 0) /* synchronous: one character, no framing */
        return data;

    /* 00 is documented as invalid and is treated as a single stop bit; 10 is
       1.5 stop bits, which drops to one stop bit on the 1X clock. */
    if (stop == 0x03)
        bits += 2;
    else if ((stop == 0x02) && (SCN_MR1_CLOCK(dev->mr1) != 0x01))
        bits += 1.5;
    else
        bits += 1;

    return bits;
}

/* Character format as the backend wants it, from MR1/MR2. */
static void
scn26x1_update_backend(scn26x1_t *dev)
{
    int parity = 0;

    if ((dev->ep.be == NULL) || (dev->ep.be->set_format == NULL))
        return;

    if (dev->mr1 & SCN_MR1_PAREN)
        parity = (dev->mr1 & 0x20) ? CHAR_COM_PARITY_EVEN : CHAR_COM_PARITY_ODD;

    dev->ep.be->set_format(dev->ep.be->priv, (int) (scn26x1_baud(dev) + 0.5),
                        SCN_MR1_LEN(dev->mr1) + 5, parity,
                        (SCN_MR1_STOP(dev->mr1) >= 0x02) ? 2 : 1);
}

/* DTR, RTS and break follow the command register. */
static void
scn26x1_update_lines(scn26x1_t *dev)
{
    uint32_t flags = 0;

    if ((dev->ep.be == NULL) || (dev->ep.be->control == NULL))
        return;

    /* Local loopback holds DTR, RTS and TxD in their inactive state and only
       wires the transmitter into the receiver. */
    if (SCN_CR_MODE(dev->cr) == SCN_MODE_LOCAL) {
        dev->ep.be->control(dev->ep.be->priv, 0);
        return;
    }

    if (dev->cr & SCN_CR_DTR)
        flags |= CHAR_COM_DTR;
    if (dev->cr & SCN_CR_RTS)
        flags |= CHAR_COM_RTS;
    if (dev->cr & SCN_CR_BREAK)
        flags |= CHAR_COM_BREAK;

    dev->ep.be->control(dev->ep.be->priv, flags);
}

/* Called by the seam after a backend is installed or released: push the
   character format and line state at whatever is on the other side now. */
static void
scn26x1_backend_changed(void *priv)
{
    scn26x1_t *dev = (scn26x1_t *) priv;

    scn26x1_update_backend(dev);
    scn26x1_update_lines(dev);
}

/* ---- device side: what an in-machine device can do to this chip --------- */

static void scn26x1_set_input_dsr(scn26x1_t *dev, int state);
static void scn26x1_set_input_dcd(scn26x1_t *dev, int state);
static void scn26x1_receive(scn26x1_t *dev, uint8_t val);
static void scn26x1_tx_start(scn26x1_t *dev);

static int
scn26x1_chip_can_receive(void *priv)
{
    return !((scn26x1_t *) priv)->rx_full;
}

static void
scn26x1_chip_set_line(void *priv, uint32_t line, int on)
{
    scn26x1_t *dev = (scn26x1_t *) priv;

    on = on ? 1 : 0;

    if (line == CHAR_COM_DSR) {
        dev->modem_set = 1;
        scn26x1_set_input_dsr(dev, !on); /* the stored state is the pin level */
    } else if (line == CHAR_COM_DCD) {
        dev->modem_set = 1;
        scn26x1_set_input_dcd(dev, !on);
    } else if (line == CHAR_COM_CTS || line == CHAR_COM_RI) {
        dev->modem_set = 1;

        if (on)
            dev->modem |= line;
        else
            dev->modem &= ~line;
    }
}

static int
scn26x1_chip_get_line(void *priv, uint32_t line)
{
    scn26x1_t *dev = (scn26x1_t *) priv;

    if (line == CHAR_COM_DTR)
        return (dev->cr & SCN_CR_DTR) ? 1 : 0;
    if (line == CHAR_COM_RTS)
        return (dev->cr & SCN_CR_RTS) ? 1 : 0;

    /* Inputs as the in-machine device last drove them. */
    return (dev->modem & line) ? 1 : 0;
}

static void
scn26x1_chip_receive(void *priv, uint8_t byte)
{
    scn26x1_receive((scn26x1_t *) priv, byte);
}

static const serial_port_ops_t scn26x1_chip_ops = {
    .can_receive = scn26x1_chip_can_receive,
    .set_line    = scn26x1_chip_set_line,
    .get_line    = scn26x1_chip_get_line,
    .receive     = scn26x1_chip_receive
};

static void
scn26x1_update_irq(scn26x1_t *dev)
{
    int level;

    /* Only received data drives the line; see the file header. */
    level = dev->rx_full ? 1 : 0;

    if (level == dev->irq_state)
        return;

    dev->irq_state = level;

    if (dev->irq_mask == 0)
        return;

    if (level)
        picint(dev->irq_mask);
    else
        picintc(dev->irq_mask);
}

/* Inputs as an in-machine device drove them, else as the backend reports them,
   else floating high (inactive). */
static int
scn26x1_input_state(scn26x1_t *dev, uint32_t line)
{
    if (dev->modem_set)
        return (dev->modem & line) ? 1 : 0;

    if ((dev->ep.be != NULL) && (dev->ep.be->status != NULL))
        return (dev->ep.be->status(dev->ep.be->priv) & line) ? 1 : 0;

    return 0;
}

/* The receiver runs while it is enabled and DCD is asserted; local loopback
   wires DTR to DCD internally, so there DTR takes its place. */
static int
scn26x1_receiver_on(scn26x1_t *dev)
{
    uint8_t mode = SCN_CR_MODE(dev->cr);

    if (mode == SCN_MODE_LOCAL)
        return (dev->cr & SCN_CR_DTR) ? 1 : 0;

    if (!(dev->cr & SCN_CR_RXEN))
        return 0;

    /* With nothing driving DCD the input is treated as asserted, the same
       way an unwired CTS does not stop the transmitter: a board that does
       not model modem lines must not silently disable its receiver. */
    if (!dev->modem_set && ((dev->ep.be == NULL) || (dev->ep.be->status == NULL)))
        return 1;

    /* The stored state is the pin level: 0 means DCD is low, i.e. asserted. */
    return !dev->dcd;
}

/* The transmitter runs while it is enabled and CTS is asserted; with nothing
   driving CTS it is treated as asserted so an unwired port still transmits. */
static int
scn26x1_cts_on(scn26x1_t *dev)
{
    if (SCN_CR_MODE(dev->cr) == SCN_MODE_LOCAL)
        return (dev->cr & SCN_CR_RTS) ? 1 : 0;

    if (dev->modem_set || ((dev->ep.be != NULL) && (dev->ep.be->status != NULL)))
        return scn26x1_input_state(dev, CHAR_COM_CTS);

    return 1;
}

static void
scn26x1_receive(scn26x1_t *dev, uint8_t val)
{
    uint8_t mode = SCN_CR_MODE(dev->cr);

    /* Automatic echo and remote loopback retransmit the character;
       only remote loopback withholds it from the CPU. */
    if ((mode == SCN_MODE_ECHO) || (mode == SCN_MODE_REMOTE)) {
        dev->tdr      = val;
        dev->tx_emt   = 0;
        dev->thr_full = 1;
        scn26x1_tx_start(dev);

        if (mode == SCN_MODE_REMOTE)
            return;
    }

    if (dev->rx_full) {
        dev->sr |= SCN_SR_OE;
        return;
    }

    dev->rx_full = 1;
    dev->rdr     = val;
    dev->sr &= ~(uint8_t) (SCN_SR_PE | SCN_SR_FE);

    scn26x1_log("SCN26X1: Rx %02X\n", val);
    scn26x1_update_irq(dev);
}

/* Take one byte from the backend if the receiver is running and the holding
   register is free. Called from the poll and again as soon as the CPU takes
   a character, so a device with a queue behind the UART is not delayed by a
   poll tick (the VAXmate POST self test polls its mouse with a short loop). */
static int
scn26x1_rx_try_read(scn26x1_t *dev)
{
    int byte;

    if (dev->rx_full || !scn26x1_receiver_on(dev))
        return 0;

    if ((dev->ep.be == NULL) || (dev->ep.be->read_byte == NULL))
        return 0;

    byte = dev->ep.be->read_byte(dev->ep.be->priv);
    if (byte < 0)
        return 0;

    scn26x1_receive(dev, (uint8_t) byte);

    return 1;
}

/* Characters arriving while the receiver is off are lost, as on real
   silicon; leaving them in the backend makes DECMOUSE.DRV fail its
   self test when Windows is started a second time. */
static void
scn26x1_rx_discard(scn26x1_t *dev)
{
    int i;

    if ((dev->ep.be == NULL) || (dev->ep.be->read_byte == NULL))
        return;

    for (i = 0; i < 64; i++) {
        if (dev->ep.be->read_byte(dev->ep.be->priv) < 0)
            break;
    }
}

/* The chip has no receiver front end of its own, so while the receiver is
   enabled and the holding register is free, take whatever the backend has. */
static void
scn26x1_rx_poll(void *priv)
{
    scn26x1_t *dev = (scn26x1_t *) priv;

    /* With nothing driving the modem inputs from the device side, they follow the
       backend; a closed input floats high, which leaves the status bits clear. */
    if (!dev->modem_set && (dev->ep.be != NULL) && (dev->ep.be->status != NULL)) {
        uint32_t status = dev->ep.be->status(dev->ep.be->priv);

        scn26x1_set_input_dsr(dev, (status & CHAR_COM_DSR) ? 0 : 1);
        scn26x1_set_input_dcd(dev, (status & CHAR_COM_DCD) ? 0 : 1);
    }

    if (scn26x1_receiver_on(dev))
        scn26x1_rx_try_read(dev);
    else
        scn26x1_rx_discard(dev);

    timer_advance_u64(&dev->rx_timer, 1000 * TIMER_USEC);
}

/* Called when the shift register has clocked out a character. */
static void
scn26x1_tx_done(void *priv)
{
    scn26x1_t *dev  = (scn26x1_t *) priv;
    uint8_t    mode = SCN_CR_MODE(dev->cr);

    dev->tx_busy = 0;

    /* TxEMT reports a finished transmission; the submodes that drive
       the transmitter from the receiver leave it inactive. */
    if ((mode != SCN_MODE_ECHO) && (mode != SCN_MODE_REMOTE))
        dev->tx_emt = 1;

    /* Only local loopback feeds the transmitter output back into the receiver;
       remote loopback and echo go the other way and are handled on receive. */
    if (mode == SCN_MODE_LOCAL)
        scn26x1_receive(dev, dev->tsr);

    /* A character may have been written while the shift register was busy. */
    scn26x1_tx_start(dev);
}

/* Move a waiting character into the shift register if it is free. */
static void
scn26x1_tx_start(scn26x1_t *dev)
{
    uint8_t mode = SCN_CR_MODE(dev->cr);
    /* In the submodes driven by the receiver, RxEN takes the place of TxEN. */
    int tx_en = ((mode == SCN_MODE_ECHO) || (mode == SCN_MODE_REMOTE))
        ? (dev->cr & SCN_CR_RXEN)
        : (dev->cr & SCN_CR_TXEN);

    if (!dev->thr_full || dev->tx_busy || !tx_en || !scn26x1_cts_on(dev))
        return;

    dev->tsr      = dev->tdr;
    dev->tx_emt   = 0;
    dev->tx_busy  = 1;
    dev->thr_full = 0;

    scn26x1_log("SCN26X1: Tx %02X\n", dev->tsr);

    if ((dev->ep.be != NULL) && (dev->ep.be->write_byte != NULL))
        dev->ep.be->write_byte(dev->ep.be->priv, dev->tsr);

    timer_on_auto(&dev->timer, scn26x1_frame_bits(dev) * scn26x1_bit_time(dev));
}

uint8_t
scn26x1_read(uint16_t port, void *priv)
{
    scn26x1_t *dev = (scn26x1_t *) priv;
    uint8_t    ret = 0xff;

    switch (port & 0x0003) {
        case SCN_RDR:
            ret          = dev->rdr;
            dev->rx_full = 0;
            scn26x1_update_irq(dev);

            /* Hand over the next queued character straight away instead of
               waiting for the next poll tick. */
            scn26x1_rx_try_read(dev);
            break;

        case SCN_SR:
            {
                uint8_t mode = SCN_CR_MODE(dev->cr);

                ret = dev->sr;

                /* TxRDY is not reported in the submodes where the transmitter is driven by the receiver. */
                if (dev->thr_full || !(dev->cr & SCN_CR_TXEN) || (mode == SCN_MODE_ECHO) || (mode == SCN_MODE_REMOTE))
                    ret &= ~(uint8_t) SCN_SR_TXRDY;
                else
                    ret |= SCN_SR_TXRDY;

                if (dev->rx_full)
                    ret |= SCN_SR_RXRDY;
                else
                    ret &= ~(uint8_t) SCN_SR_RXRDY;

                /* SR2 carries TxEMT and DSCHG in the same bit. */
                if (dev->tx_emt || dev->dschg)
                    ret |= SCN_SR_TXEMT;
                else
                    ret &= ~(uint8_t) SCN_SR_TXEMT;

                if (!dev->dcd)
                    ret |= SCN_SR_DCD;
                else
                    ret &= ~(uint8_t) SCN_SR_DCD;

                if (!dev->dsr)
                    ret |= SCN_SR_DSR;
                else
                    ret &= ~(uint8_t) SCN_SR_DSR;

                /* Reading the status register clears the change flag only. */
                dev->dschg = 0;
                break;
            }

        case SCN_MR:
            ret = dev->mr_ptr ? dev->mr2 : dev->mr1;
            dev->mr_ptr ^= 1;
            break;

        case SCN_CR:
            ret = dev->cr;
            /* A command register read rewinds both sequencers. */
            dev->mr_ptr  = 0;
            dev->syn_ptr = 0;
            break;

        default:
            break;
    }

    scn26x1_log("SCN26X1: [%04X] read  %02X -> %02X\n", port, port & 0x0003, ret);
    return ret;
}

void
scn26x1_write(uint16_t port, uint8_t val, void *priv)
{
    scn26x1_t *dev = (scn26x1_t *) priv;

    scn26x1_log("SCN26X1: [%04X] write %02X <- %02X\n", port, port & 0x0003, val);

    switch (port & 0x0003) {
        case SCN_TDR:
            dev->tdr      = val;
            dev->tx_emt   = 0;
            dev->thr_full = 1;
            scn26x1_tx_start(dev);
            break;

        case SCN_SYN:
            dev->syn[dev->syn_ptr] = val;
            dev->syn_ptr           = (dev->syn_ptr + 1) % 3;
            break;

        case SCN_MR:
            if (dev->mr_ptr)
                dev->mr2 = val;
            else
                dev->mr1 = val;
            dev->mr_ptr ^= 1;

            /* Baud rate and character format reach the backend here. */
            scn26x1_update_backend(dev);
            break;

        case SCN_CR:
            {
                uint8_t old = dev->cr;

                dev->cr = val;

                if (val & SCN_CR_ERRS) {
                    dev->sr &= ~(uint8_t) (SCN_SR_PE | SCN_SR_OE | SCN_SR_FE);
                    dev->cr &= ~(uint8_t) SCN_CR_ERRS; /* one-shot */
                }

                /* Disabling the receiver drops RxRDY and the error flags. */
                if (!(val & SCN_CR_RXEN)) {
                    if (dev->rx_full) {
                        dev->rx_full = 0;
                        scn26x1_update_irq(dev);
                    }

                    dev->sr &= ~(uint8_t) (SCN_SR_PE | SCN_SR_OE | SCN_SR_FE);
                }

                /* A disabled transmitter, and the submodes that drive it from the receiver, leave TxEMT inactive. */
                if (!(val & SCN_CR_TXEN) || (SCN_CR_MODE(val) == SCN_MODE_ECHO) || (SCN_CR_MODE(val) == SCN_MODE_REMOTE))
                    dev->tx_emt = 0;

                scn26x1_update_lines(dev);

                if (!(old & SCN_CR_TXEN) && (val & SCN_CR_TXEN))
                    scn26x1_tx_start(dev);
                break;
            }

        default:
            break;
    }
}

/* Modem inputs driven by an in-machine device through the port lines. */
static void
scn26x1_set_input_dsr(scn26x1_t *dev, int state)
{
    state = state ? 1 : 0;

    if (state == dev->dsr)
        return;

    dev->dsr = state;

    /* SR2's DSCHG side is enabled while either half of the chip is on. */
    if (dev->cr & (SCN_CR_TXEN | SCN_CR_RXEN))
        dev->dschg = 1;
}

static void
scn26x1_set_input_dcd(scn26x1_t *dev, int state)
{
    state = state ? 1 : 0;

    if (state == dev->dcd)
        return;

    dev->dcd = state;

    if (dev->cr & (SCN_CR_TXEN | SCN_CR_RXEN))
        dev->dschg = 1;

    /* Losing the carrier stops the receiver and drops a waiting character,
       except in local loopback where DTR stands in for DCD. */
    if (state && (SCN_CR_MODE(dev->cr) != SCN_MODE_LOCAL) && dev->rx_full) {
        dev->rx_full = 0;
        scn26x1_update_irq(dev);
    }
}

/* The board's handle on the chip's end of the seam. */
serial_endpoint_t *
scn26x1_endpoint(void *priv)
{
    return (priv != NULL) ? &((scn26x1_t *) priv)->ep : NULL;
}

/* Bind the interrupt output to a PIC line; 0 disconnects it. */
void
scn26x1_set_irq(void *priv, int irq_mask)
{
    scn26x1_t *dev = (scn26x1_t *) priv;

    if (dev == NULL)
        return;

    if (dev->irq_mask != 0)
        picintc(dev->irq_mask);

    dev->irq_mask  = irq_mask;
    dev->irq_state = 0;

    scn26x1_update_irq(dev);
}

static void
scn26x1_reset_state(scn26x1_t *dev)
{
    timer_disable(&dev->timer);

    dev->tdr = dev->tsr = dev->rdr = 0;
    dev->mr1 = dev->mr2 = dev->cr = dev->sr = 0;
    memset(dev->syn, 0x00, sizeof(dev->syn));
    dev->mr_ptr   = 0;
    dev->syn_ptr  = 0;
    dev->thr_full = 0;
    dev->rx_full  = 0;
    dev->tx_busy  = 0;
    dev->tx_emt   = 0; /* TxEMT only becomes active once a character has gone out */
    dev->dschg    = 0;
    dev->dsr      = 1;
    dev->dcd      = 1;

    scn26x1_update_backend(dev);
    scn26x1_update_lines(dev);
    scn26x1_update_irq(dev);
}

/* The chip decodes no address of its own: the board registers the four
   consecutive ports and the chip uses A1:A0 to pick the register. INTR is a
   level line and the chip registers itself on a COM port (or -1 for none) so
   the host layer can hand it a chardev or an in-machine device. Bind the
   interrupt line with scn26x1_set_irq(). */
static void *
scn26x1_init(const device_t *info)
{
    int        variant = (int) ((info->local >> 8) & 0xff);
    int        port    = (int) (info->local & SCN26X1_PORT_MASK);
    scn26x1_t *dev     = (scn26x1_t *) calloc(1, sizeof(scn26x1_t));

    dev->port = port;

    switch (variant) {
        case SCN26X1_VARIANT_B:
            dev->rates = scn26x1_rates_b;
            break;

        case SCN26X1_VARIANT_C:
            dev->rates = scn26x1_rates_c;
            break;

        default:
            break;
    }

    timer_add(&dev->timer, scn26x1_tx_done, dev, 0);
    timer_add(&dev->rx_timer, scn26x1_rx_poll, dev, 0);
    timer_set_delay_u64(&dev->rx_timer, 1000 * TIMER_USEC);

    scn26x1_reset_state(dev);

    /* The chip decodes no address of its own: the board registers the four ports and
       passes A1:A0 through, the same as the 8251. The port table drives the chardev
       or the in-machine device; the chip has no serial_t to register either. */
    serial_endpoint_init(&dev->ep, dev, &scn26x1_chip_ops);
    dev->ep.on_attach = scn26x1_backend_changed;

    if ((port >= 0) && (port < SERIAL_MAX) && com_ports[port].enabled)
        com_ports[port].ep = &dev->ep;

    return dev;
}

static void
scn26x1_close(void *priv)
{
    scn26x1_t *dev = (scn26x1_t *) priv;

    if (dev == NULL)
        return;

    timer_disable(&dev->timer);
    timer_disable(&dev->rx_timer);

    if (dev->irq_mask != 0)
        picintc(dev->irq_mask);

    dev->ep.be = NULL;

    if ((dev->port >= 0) && (dev->port < SERIAL_MAX) && (com_ports[dev->port].ep == &dev->ep))
        com_ports[dev->port].ep = NULL;

    free(dev);
}

static void
scn26x1_reset(void *priv)
{
    scn26x1_reset_state((scn26x1_t *) priv);
}

/* The low byte of .local is left clear so a board can OR a COM port index into
   it through device_add_params(); a board that owns the chip and wants no COM
   port ORs SCN26X1_PORT_MASK into the low byte instead. */
const device_t scn2661b_device = {
    .name          = "Signetics SCN2661B EPCI",
    .internal_name = "scn2661b",
    .flags         = 0,
    .local         = (SCN26X1_VARIANT_B << 8),
    .init          = scn26x1_init,
    .close         = scn26x1_close,
    .reset         = scn26x1_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t scn2661c_device = {
    .name          = "Signetics SCN2661C EPCI",
    .internal_name = "scn2661c",
    .flags         = 0,
    .local         = (SCN26X1_VARIANT_C << 8),
    .init          = scn26x1_init,
    .close         = scn26x1_close,
    .reset         = scn26x1_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
