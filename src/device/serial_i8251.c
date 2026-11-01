/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel 8251A UART.
 *
 *          Based on Intel 8251 emulation in MAME
 *          (src/devices/machine/i8251.cpp),
 *          copyright-holders: smf, Robbbert.
 *
 *          Only the asynchronous mode is modelled; the synchronous mode's
 *          sync characters are accepted and discarded. Transmit and receive
 *          timing comes from the TxC/RxC rate supplied by the machine, which
 *          on the Tandy 2000 is an 8253 channel.
 *
 * Authors: WNT50
 *
 *          Copyright 2026 WNT50.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
#include <86box/plat_unused.h>

#ifdef ENABLE_I8251_LOG
int i8251_do_log = ENABLE_I8251_LOG;

static void
i8251_log(const char *fmt, ...)
{
    va_list ap;

    if (i8251_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define i8251_log(fmt, ...)
#endif

typedef struct i8251_t {
    uint8_t mode;
    uint8_t command;
    uint8_t tx_data;
    uint8_t rx_data;

    int expect_mode; /* the next control write is a mode word */
    int rx_ready;
    int txrdy;
    int txe;

    int port; /* COM port this chip is wired to */

    double clock; /* TxC/RxC in Hz, 0 = not programmed */

    int irq_mask; /* IRQ line as a PIC bit mask, 0 = not connected */

    /* Whatever is on the other side of the port; NULL when nothing is attached. */
    const serial_backend_t *be;
    /* Storage for `be`; owned by this chip, so it goes away with the chip. */
    serial_backend_store_t  be_store;

    uint32_t modem;     /* modem lines driven by an in-machine device */
    uint8_t  modem_set; /* set once a device drives them */

    pc_timer_t rx_timer;
    pc_timer_t tx_timer;
    pc_timer_t tx_done_timer;
} i8251_t;

static void
i8251_update_irq(i8251_t *dev)
{
    /* The ready flags stay visible in the status register regardless of the
       enables; only the interrupt output is gated by them, so that an idle
       transmitter cannot hold the line into the 8259 forever. */
    int active = (dev->rx_ready && (dev->command & 0x04)) ||
                 (dev->txrdy && (dev->command & 0x01));

    if (dev->irq_mask == 0)
        return;

    if (active)
        picint(dev->irq_mask);
    else
        picintc(dev->irq_mask);
}

static int
i8251_baud_factor(const i8251_t *dev)
{
    switch (dev->mode & 0x03) {
        case 0x01:
            return 1;
        case 0x03:
            return 64;
        default:
            /* 00 selects synchronous mode, which the clock is not scaled for. */
            return 16;
    }
}

static int
i8251_data_bits(const i8251_t *dev)
{
    return 5 + ((dev->mode >> 2) & 0x03);
}

static int
i8251_stop_bits(const i8251_t *dev)
{
    switch ((dev->mode >> 6) & 0x03) {
        case 0x02:
            return 2; /* 1.5 stop bits, rounded up for the host backend */
        case 0x03:
            return 2;
        default:
            return 1;
    }
}

static int
i8251_char_bits(const i8251_t *dev)
{
    return 1 + i8251_data_bits(dev) + i8251_stop_bits(dev) + ((dev->mode & 0x10) ? 1 : 0);
}

static double
i8251_bit_period(const i8251_t *dev)
{
    double hz = dev->clock;

    /* Keep the port usable while the guest has not programmed its clock yet. */
    if (hz < 1.0)
        hz = 153600.0;

    return ((double) i8251_baud_factor(dev) * 1000000.0) / hz;
}

static void
i8251_update_backend(i8251_t *dev)
{
    double  hz;
    uint8_t parity;

    if ((dev->be == NULL) || (dev->be->set_format == NULL))
        return;

    hz = dev->clock;
    if (hz < 1.0)
        hz = 153600.0;

    if (dev->mode & 0x10)
        parity = (dev->mode & 0x20) ? CHAR_COM_PARITY_EVEN : CHAR_COM_PARITY_ODD;
    else
        parity = 0;

    dev->be->set_format(dev->be->priv, (int) ((hz / (double) i8251_baud_factor(dev)) + 0.5),
                        (int) i8251_data_bits(dev), parity, (int) i8251_stop_bits(dev));
}

/* Called by the host layer with the port's chardev, or with NULL when the
   port is released. The backend storage stays in this chip. */
static void
i8251_set_host_backend(void *priv, char_port_t *port)
{
    i8251_t *dev = (i8251_t *) priv;

    dev->be = (port != NULL) ? serial_backend_from_chardev(port, &dev->be_store) : NULL;
}

/* ---- device side: what an in-machine device can do to this chip --------- */

static void
i8251_chip_receive(void *priv, uint8_t byte)
{
    i8251_t *dev = (i8251_t *) priv;

    if (dev->rx_ready)
        return;

    dev->rx_data  = byte;
    dev->rx_ready = 1;
    i8251_update_irq(dev);
}

static int
i8251_chip_can_receive(void *priv)
{
    return !((i8251_t *) priv)->rx_ready;
}

static void
i8251_chip_set_line(void *priv, uint32_t line, int on)
{
    i8251_t *dev = (i8251_t *) priv;

    dev->modem_set = 1;

    if (on)
        dev->modem |= line;
    else
        dev->modem &= ~line;
}

static int
i8251_chip_get_line(void *priv, uint32_t line)
{
    i8251_t *dev = (i8251_t *) priv;

    if (line == CHAR_COM_RTS)
        return (dev->command & 0x20) ? 1 : 0;
    if (line == CHAR_COM_DTR)
        return (dev->command & 0x02) ? 1 : 0;

    return (dev->modem & line) ? 1 : 0;
}

static const serial_port_ops_t i8251_chip_ops = {
    .can_receive = i8251_chip_can_receive,
    .set_line    = i8251_chip_set_line,
    .get_line    = i8251_chip_get_line,
    .receive     = i8251_chip_receive
};

static void
i8251_set_device_backend(void *priv, serial_device_t *sd)
{
    i8251_t *dev = (i8251_t *) priv;

    dev->be = serial_backend_from_device(NULL, sd, &dev->be_store);
}

static void
i8251_update_lines(i8251_t *dev)
{
    uint32_t flags = 0;

    if ((dev->be == NULL) || (dev->be->control == NULL))
        return;

    if (dev->command & 0x02)
        flags |= CHAR_COM_DTR;
    if (dev->command & 0x20)
        flags |= CHAR_COM_RTS;
    if (dev->command & 0x08)
        flags |= CHAR_COM_BREAK;

    dev->be->control(dev->be->priv, flags);
}

static void
i8251_reset_state(i8251_t *dev)
{
    dev->mode        = 0x00;
    dev->command     = 0x00;
    dev->expect_mode = 1;
    dev->rx_ready    = 0;
    dev->txrdy       = 1;
    dev->txe         = 1;

    timer_disable(&dev->tx_timer);
    timer_disable(&dev->tx_done_timer);

    i8251_update_irq(dev);
}

static void
i8251_tx_done(void *priv)
{
    i8251_t *dev = (i8251_t *) priv;

    dev->txe = 1;
    i8251_update_irq(dev);
}

static void
i8251_tx_start(void *priv)
{
    i8251_t *dev = (i8251_t *) priv;
    int      bits = i8251_char_bits(dev) - 1;

    /* The byte has moved to the shift register, so the buffer is free again. */
    dev->txrdy = 1;
    i8251_update_irq(dev);

    if ((dev->be != NULL) && (dev->be->write_byte != NULL))
        dev->be->write_byte(dev->be->priv, dev->tx_data);

    if (bits < 1)
        bits = 1;
    timer_set_delay_u64(&dev->tx_done_timer,
                        (uint64_t) ((double) bits * i8251_bit_period(dev) * TIMER_USEC));
}

static void
i8251_rx_poll(void *priv)
{
    i8251_t *dev = (i8251_t *) priv;

    if (!dev->rx_ready && (dev->command & 0x04) && (dev->be != NULL) && (dev->be->read_byte != NULL)) {
        int byte = dev->be->read_byte(dev->be->priv);

        if (byte >= 0) {
            dev->rx_data  = (uint8_t) byte;
            dev->rx_ready = 1;
            i8251_update_irq(dev);
        }
    }

    timer_advance_u64(&dev->rx_timer, 1000 * TIMER_USEC);
}

uint8_t
i8251_read(uint16_t addr, void *priv)
{
    i8251_t *dev = (i8251_t *) priv;
    uint8_t  ret;

    if (addr & 0x0001)
        return 0xff;

    if (addr & 0x0002) {
        ret = (dev->rx_ready ? 0x02 : 0x00) |
              (dev->txrdy ? 0x01 : 0x00) |
              (dev->txe ? 0x04 : 0x00);

        /* A device driving the modem lines wins over the host backend; with
           nothing attached the inputs float high. */
        if (dev->modem_set) {
            if (dev->modem & CHAR_COM_DSR)
                ret |= 0x80;
        } else if ((dev->be == NULL) || (dev->be->status == NULL) ||
                   (dev->be->status(dev->be->priv) & CHAR_COM_DSR))
            ret |= 0x80;
    } else {
        ret           = dev->rx_data;
        dev->rx_ready = 0;
        i8251_update_irq(dev);
    }

    i8251_log("i8251_read(%04X) = %02X\n", addr, ret);
    return ret;
}

void
i8251_write(uint16_t addr, uint8_t val, void *priv)
{
    i8251_t *dev = (i8251_t *) priv;

    if (addr & 0x0001)
        return;

    i8251_log("i8251_write(%04X, %02X)\n", addr, val);

    if (!(addr & 0x0002)) {
        if (!dev->txrdy)
            return;
        dev->tx_data = val;
        dev->txrdy   = 0;
        dev->txe     = 0;
        i8251_update_irq(dev);
        timer_set_delay_u64(&dev->tx_timer,
                            (uint64_t) (i8251_bit_period(dev) * TIMER_USEC));
        return;
    }

    if (dev->expect_mode) {
        dev->expect_mode = 0;
        dev->mode        = val;
        i8251_update_backend(dev);
        return;
    }

    if (val & 0x40) {
        /* Internal reset: the next control write is taken as a mode word. */
        i8251_reset_state(dev);
        i8251_update_backend(dev);
        return;
    }

    dev->command = val;
    i8251_update_lines(dev);
    i8251_update_irq(dev);
}

void
i8251_set_irq(void *priv, int irq_mask)
{
    i8251_t *dev = (i8251_t *) priv;

    if (dev == NULL)
        return;

    if (dev->irq_mask != 0)
        picintc(dev->irq_mask);

    dev->irq_mask = irq_mask;
}

void
i8251_set_clock(void *priv, double hz)
{
    i8251_t *dev = (i8251_t *) priv;

    if (dev == NULL)
        return;

    dev->clock = hz;
    i8251_update_backend(dev);
}

static void *
i8251_init(const device_t *info)
{
    i8251_t *dev  = (i8251_t *) calloc(1, sizeof(i8251_t));
    int      port = (int) (info->local & 0xff);

    dev->expect_mode = 1;
    dev->irq_mask    = 0;
    dev->txrdy       = 1;
    dev->txe         = 1;

    timer_add(&dev->rx_timer, i8251_rx_poll, dev, 0);
    timer_add(&dev->tx_timer, i8251_tx_start, dev, 0);
    timer_add(&dev->tx_done_timer, i8251_tx_done, dev, 0);
    timer_set_delay_u64(&dev->rx_timer, 1000 * TIMER_USEC);

    dev->port = port;

    /* The port table drives the chardev; the 16550 path does the same through
       serial_t, this chip has no serial_t to register. */
    if ((port < SERIAL_MAX) && com_ports[port].enabled) {
        com_ports[port].chip               = dev;
        com_ports[port].ops                = &i8251_chip_ops;
        com_ports[port].set_host_backend   = i8251_set_host_backend;
        com_ports[port].set_device_backend = i8251_set_device_backend;

    }

    return dev;
}

static void
i8251_close(void *priv)
{
    i8251_t *dev = (i8251_t *) priv;

    timer_disable(&dev->rx_timer);
    timer_disable(&dev->tx_timer);
    timer_disable(&dev->tx_done_timer);

    if (dev->irq_mask != 0)
        picintc(dev->irq_mask);

    dev->be = NULL;

    if ((dev->port < SERIAL_MAX) && (com_ports[dev->port].chip == dev)) {
        com_ports[dev->port].chip               = NULL;
        com_ports[dev->port].ops                = NULL;
        com_ports[dev->port].set_host_backend   = NULL;
        com_ports[dev->port].set_device_backend = NULL;
    }

    free(dev);
}

static void
i8251_reset(void *priv)
{
    i8251_t *dev = (i8251_t *) priv;

    i8251_reset_state(dev);
    i8251_update_lines(dev);
}

const device_t i8251_device = {
    .name          = "Intel 8251A UART",
    .internal_name = "i8251",
    .flags         = 0,
    .local         = 0,
    .init          = i8251_init,
    .close         = i8251_close,
    .reset         = i8251_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
