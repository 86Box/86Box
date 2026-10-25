/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NS8250/16450/16550 UART emulation.
 *
 *          Now passes all the AMIDIAG tests.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
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
#include "cpu.h"
#include <86box/timer.h>
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/fifo.h>
#include <86box/serial.h>
#include <86box/mouse.h>

serial_port_t com_ports[SERIAL_MAX] = { 0 };

enum {
    SERIAL_INT_LSR       = 1,
    SERIAL_INT_TIMEOUT   = 2,
    SERIAL_INT_RECEIVE   = 4,
    SERIAL_INT_TRANSMIT  = 8,
    SERIAL_INT_MSR       = 16,
    SERIAL_INT_RX_DMA_TC = 32,
    SERIAL_INT_TX_DMA_TC = 64
};

void    serial_update_ints(serial_t *dev);

static int             next_inst = 0;
static serial_device_t serial_devices[SERIAL_MAX];

static void            serial_xmit_d_empty_evt(void *priv);
static void            write_fifo(serial_t *dev, uint8_t dat);

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
#    define serial_log(fmt, ...)
#endif

static uint8_t
serial_data_mask(serial_t *dev)
{
    static const uint8_t masks[4] = {
        0x1f, 0x3f, 0x7f, 0xff
    };

    return masks[dev->lcr & 0x03];
}

static void
serial_lcr_frame(serial_t *dev)
{
    /* data bits + start bit. */
    dev->bits = ((dev->lcr & 0x03) + 5) + 1;
    dev->bits++; /* First stop bit. */
    if (dev->lcr & 0x04)
        dev->bits++; /* Second stop bit. */
    /* 1.5 stop bits */
    dev->half_stop = ((dev->lcr & 0x07) == 0x04);
    /* Parity bit. */
    if (dev->lcr & 0x08)
        dev->bits++;
}

void
serial_reset_port(serial_t *dev)
{
    if (dev->type >= SERIAL_16550) {
        if (dev->fifo_enabled)
            fifo_reset_evt(dev->xmit_fifo);
        else
            fifo_reset(dev->xmit_fifo);
    }

    dev->lsr = 0x60; /* Mark that both THR/FIFO and TXSR are empty. */
    dev->iir = dev->ier = dev->lcr = dev->fcr = 0;
    serial_lcr_frame(dev);

    dev->fifo_enabled                         = 0;
    dev->transmit_enabled                     = 0;
    dev->baud_cycles                          = 0;
    dev->out_new                              = 0xffff;
    dev->pending_rx_error                     = 0;

    dev->txsr_empty = 1;
    dev->txsr_sent  = 0;
    dev->thr_empty  = 1;

    serial_update_ints(dev);
    dev->irq_state = 0;

    if (dev->char_port.chardev.control)
        dev->char_port.chardev.control((dev->mctrl & 0x03) | (dev->lcr & 0x40), dev->char_port.chardev.priv);
}

/* Push the line settings to the host port while DLAB unset */
static void
serial_port_config(serial_t *dev)
{
    if (dev->lcr & 0x80) {
        dev->port_config_pending = 1;
        return;
    }
    dev->port_config_pending = 0;
    if (dev->char_port.chardev.port_config)
        dev->char_port.chardev.port_config(dev->char_port.chardev.priv);
}

static double
serial_divisor(serial_t *dev)
{
    return (dev->dlab != 0x0000) ? (double) dev->dlab : 65536.0;
}

/* calc host line settings from UART's divisor and LCR */
static void
serial_update_com(serial_t *dev)
{
    dev->char_port.com.baud      = dev->clock_src / (16.0 * serial_divisor(dev));
    dev->char_port.com.data_bits = (dev->lcr & 0x03) + 5;
    dev->char_port.com.parity    = (dev->lcr >> 3) & 0x7;
    dev->char_port.com.stop_bits = !!(dev->lcr & 0x04) + 1;
}

void
serial_transmit_period(serial_t *dev)
{
    /* Bit period based on DLAB. */
    dev->transmit_period = (16000000.0 * serial_divisor(dev)) / dev->clock_src;
    if (dev->sd && dev->sd->transmit_period_callback)
        dev->sd->transmit_period_callback(dev, dev->sd->priv, dev->transmit_period);

    serial_update_com(dev);
    serial_port_config(dev);
}

void
serial_do_irq(serial_t *dev, int set)
{
    if (dev->irq != 0xff) {
        if (set || (dev->irq_state != !!set))
            picint_common(1 << dev->irq, !!(dev->type >= SERIAL_16450), set, &dev->irq_state);
        if (dev->type < SERIAL_16450)
            dev->irq_state = !!set;
    }
}

void
serial_update_ints(serial_t *dev)
{
    /* timeouts If both are pending, timeout IIR 0C wins over
       receive. timeout is only in FIFO mode for types >= SERIAL_16550 */
    uint8_t ier_map[7] = { 0x04, 0x01, 0x01, 0x02, 0x08, 0x40, 0x80 };
    uint8_t iir_map[7] = { 0x06, 0x0c, 0x04, 0x02, 0x00, 0x0e, 0x0a };

    dev->iir = (dev->iir & 0xf0) | 0x01;

    for (uint8_t i = 0; i < 7; i++) {
        if ((dev->ier & ier_map[i]) && (dev->int_status & (1 << i))) {
            dev->iir = (dev->iir & 0xf0) | iir_map[i];
            break;
        }
    }

    serial_do_irq(dev, !(dev->iir & 0x01) && ((dev->mctrl & 8) || ((dev->type == SERIAL_8250_PCJR_3F8) || (dev->type == SERIAL_8250_PCJR_2F8))));
}

static void
serial_clear_timeout(serial_t *dev)
{
    /* Disable timeout timer and clear timeout condition. */
    timer_disable(&dev->timeout_timer);
    dev->int_status &= ~SERIAL_INT_TIMEOUT;
    serial_update_ints(dev);
}

/*
   The receive timer polls the host character device, so it should run once
   per character, not once per bit: at 115200 baud that is the difference
   between 11520 and 115200 callbacks per second on the emulation thread.

   bits is 0 until the guest programs the LCR, and arming a timer with 0 is
   presumably why the multiplication was commented out, so assume a full
   10-bit frame until the guest says otherwise.

   Note by OBattler: I originally wrote the receive timer and the per-bit
   polling is intentional because the NT 3.x Logitech serial mouse driver
   likes writing the next byte at a very tight timing after the previous
   byte - changing it to per-byte timing delays the byte movement so much
   that it goes way out of specification, even breaking the Windows 9x
   modem detection.
 */
static double
serial_receive_period(const serial_t *dev)
{
    return (double) /* (dev->bits ? dev->bits : 10) * */ dev->transmit_period;
}

/* Sets character period and the 4-char rx tmo */
static double
serial_frame_bits(const serial_t *dev)
{
    /* 5b/1.5s handling : count second stop bit as a whole tick and half_stop divides */
    if (!dev->bits)
        return 10.0;
    return (double) dev->bits - (dev->half_stop ? 0.5 : 0.0);
}

/* one character time on the line */
static double
serial_char_period(const serial_t *dev)
{
    return serial_frame_bits(dev) * dev->transmit_period;
}

/* microseconds interval between host reads while nothing is arriving */
#define SERIAL_RX_POLL_MIN_US 50.0
#define SERIAL_RX_POLL_WIRE_MIN_US 1000.0

/* Host polls are capped at one character time 
Emulated devices without a host read keep the per bit timer */
static double
serial_receive_poll_period(const serial_t *dev)
{
    double period = dev->transmit_period;

    if (!dev->char_port.chardev.read)
        return serial_receive_period(dev);
    if ((dev->char_port.chardev.flags & CHAR_COM_SYNC_RX) || (dev->mctrl & 0x10))
        return serial_char_period(dev);
    if (period < SERIAL_RX_POLL_MIN_US)
        period = SERIAL_RX_POLL_MIN_US;
#ifdef _WIN32
    if ((dev->char_port.chardev.flags & CHAR_COM_WIRE) && (period < SERIAL_RX_POLL_WIRE_MIN_US))
        period = SERIAL_RX_POLL_WIRE_MIN_US;
#endif
    if (period > serial_char_period(dev))
        period = serial_char_period(dev);
    return period;
}

/* microseconds interval for host modem-status lines sampling timer */
#define SERIAL_STATUS_PERIOD_US 1000.0

/* modem-status polling runs on its own fixed-rate timer */
static void
serial_status_timer(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    /* stopped until serial_start_polling() active */
    if (!dev->char_port.chardev.status)
        return;

    timer_on_auto(&dev->status_timer, SERIAL_STATUS_PERIOD_US);

    /* for diagnostic loopback , MCR bit 4. MSR inputs come from the MCR
       outputs */
    if (!(dev->mctrl & 0x10)) {
        uint8_t prev_msr = dev->msr;
        uint32_t flags = dev->char_port.chardev.status(dev->char_port.chardev.priv);
        uint8_t mask = CHAR_COM_CTS | CHAR_COM_DSR | CHAR_COM_RI | CHAR_COM_DCD;
        dev->msr = (dev->msr & ~mask) | (flags & mask);
        mask &= ~CHAR_COM_RI;
        dev->msr |= ((prev_msr & mask) ^ (dev->msr & mask)) >> 4;
        if ((prev_msr & CHAR_COM_RI) && !(dev->msr & CHAR_COM_RI))
            dev->msr |= CHAR_COM_RI >> 4;
        if (dev->msr & 0x0f) {
            dev->int_status |= SERIAL_INT_MSR;
            serial_update_ints(dev);
        }
    }
}

/* LSR error bits PE, FE, BI for a chars CHAR_COM_ERR_* flags */
static uint8_t
serial_rx_error_lsr(const serial_t *dev, uint32_t err)
{
    uint8_t ret = 0x00;

    if (err & CHAR_COM_ERR_BREAK) {
        ret = 0x18;
        if ((dev->lcr & 0x18) == 0x08)
            ret |= 0x04;
        return ret;
    }

    if (err & CHAR_COM_ERR_PARITY)
        ret |= 0x04;
    if (err & CHAR_COM_ERR_FRAMING)
        ret |= 0x08;

    return ret;
}

/* FIFO mode PE/FE/BI belong to a char */
static void
serial_rcvr_fifo_top(serial_t *dev)
{
    uint8_t err = fifo_peek_tag(dev->rcvr_fifo);

    if (err) {
        dev->lsr |= err;
        dev->int_status |= SERIAL_INT_LSR;
        serial_update_ints(dev);
    }
}

/* store received character in FIFO with its LSR error bits */
static void
serial_rcvr_fifo_write(serial_t *dev, uint8_t val, uint8_t err)
{
    int was_empty = fifo_get_empty(dev->rcvr_fifo);

    fifo_write_evt_tagged(err, val, dev->rcvr_fifo);

    if (fifo_get_tags(dev->rcvr_fifo))
        dev->lsr |= 0x80;
    if (was_empty)
        serial_rcvr_fifo_top(dev);
}

/* Move a received char from the RSR into the FIFO or RBR with its
   pending errors and raise Data Ready */
static void
serial_rx_deliver(serial_t *dev)
{
    uint8_t err;

    if (dev->out_new == 0xffff)
        return;

    /* We have received a byte into the RSR. */
    serial_log("Byte received: %04X\n", dev->out_new);

    err                   = serial_rx_error_lsr(dev, dev->pending_rx_error);
    dev->pending_rx_error = 0;

    if (dev->fifo_enabled) {
        /* FIFO mode. */

        /* Clear FIFO timeout. */
        serial_clear_timeout(dev);

        serial_rcvr_fifo_write(dev, (uint8_t) (dev->out_new & 0xff), err);
        dev->out_new = 0xffff;

#if 0
        pclog("serial_receive_timer(): lsr = %02X, ier = %02X, iir = %02X, int_status = %02X\n",
              dev->lsr, dev->ier, dev->iir, dev->int_status);
#endif

        timer_on_auto(&dev->timeout_timer, 4.0 * serial_frame_bits(dev) * dev->transmit_period);
    } else {
        /* Non-FIFO mode. */

        /* Indicate overrun. */
        if (dev->lsr & 0x01)
            dev->lsr |= 0x02;
        dev->lsr |= err;

        dev->dat     = (uint8_t) (dev->out_new & 0xff);
        dev->out_new = 0xffff;

        /* Raise Data Ready interrupt. */
        dev->lsr |= 0x01;
        dev->int_status |= SERIAL_INT_RECEIVE;
        if (dev->lsr & 0x1e) /* OE | PE | FE | BI */
            dev->int_status |= SERIAL_INT_LSR;

        serial_update_ints(dev);
    }
}

/* copy one character from host device into the RSR
   returns 1 when a character came off the wire. */
static int
serial_rx_take(serial_t *dev)
{
    uint8_t val;

    /* only read when the receive shift register is free */
    if (!dev->char_port.chardev.read || (dev->out_new != 0xffff))
        return 0;
    if (dev->char_port.chardev.read(&val, sizeof(val), dev->char_port.chardev.priv) <= 0)
        return 0;

    /* preserve pending_rx_error for chars in loopback mode */
    if (dev->mctrl & 0x10)
        return 1;

    dev->pending_rx_error = dev->char_port.chardev.read_error
        ? dev->char_port.chardev.read_error(dev->char_port.chardev.priv)
        : 0;
    write_fifo(dev, val);
    return 1;
}

static void
serial_receive_timer(void *priv)
{
    serial_t *dev = (serial_t *) priv;
    int       got;

    serial_log("serial_receive_timer()\n");

    got = serial_rx_take(dev);
    serial_rx_deliver(dev);

    /* rearm timer after byte rx. wait one full character time before polling. 
    With nothing received, serial_receive_poll_period() polls faster to avoid char slot latency */
    timer_on_auto(&dev->receive_timer, got ? serial_char_period(dev) : serial_receive_poll_period(dev));
}

static void
write_fifo(serial_t *dev, uint8_t dat)
{
    serial_log("write_fifo(%08X, %02X, %i, %i)\n", dev, dat,
               (dev->type >= SERIAL_16550) && dev->fifo_enabled,
               ((dev->type >= SERIAL_16550) && dev->fifo_enabled) ?
               fifo_get_count(dev->rcvr_fifo) : 0);

    /* Do this here, because in non-FIFO mode, this is read directly. */
    dev->out_new = (uint16_t) (dat & serial_data_mask(dev));
}

static void
serial_receive_loopback_break(serial_t *dev)
{
    /* loopback: tx feeds rx internally. Break rx's a zero char; FE/PE
       follow from the LCR in serial_rx_error_lsr(). In FIFO mode errors
       stay with the char. */
    dev->out_new          = 0x0000;
    dev->pending_rx_error = CHAR_COM_ERR_BREAK;
    serial_rx_deliver(dev);
}

void
serial_write_fifo(serial_t *dev, uint8_t dat)
{
    serial_log("serial_write_fifo(%08X, %02X, %i, %i)\n", dev, dat,
               (dev->type >= SERIAL_16550) && dev->fifo_enabled,
               ((dev->type >= SERIAL_16550) && dev->fifo_enabled) ?
               fifo_get_count(dev->rcvr_fifo) : 0);

    if ((dev != NULL) && !(dev->mctrl & 0x10))
        write_fifo(dev, dat);
}

void
serial_transmit(serial_t *dev, uint8_t val)
{
    val &= serial_data_mask(dev);

    if (dev->mctrl & 0x10) {
        /* loopback mode: rx is clocked with tx, char is ready at frame end.
           note: the receive timer adds up to one more char time delay */
        write_fifo(dev, val);
        serial_rx_deliver(dev);
    } else if (dev->sd && dev->sd->dev_write)
        dev->sd->dev_write(dev, dev->sd->priv, val);
    else if (dev->char_port.chardev.write) {
        dev->char_port.chardev.write(&val, sizeof(val), dev->char_port.chardev.priv);

        /* loopback mode: SYNC_RX answers write() immediately */
        if ((dev->char_port.chardev.flags & CHAR_COM_SYNC_RX) && serial_rx_take(dev))
            serial_rx_deliver(dev);
    }
}

static void
serial_move_to_txsr(serial_t *dev)
{
    dev->txsr_empty = 0;
    if (dev->fifo_enabled)
        dev->txsr = fifo_read_evt(dev->xmit_fifo);
    else {
        dev->txsr = dev->thr;
        dev->thr  = 0;
        dev->thr_empty = 1;
        serial_xmit_d_empty_evt(dev);
    }

    dev->lsr &= ~0x40;

    /* emulated frame & wire. */
    dev->txsr_sent = 0;
    if (!(dev->mctrl & 0x10) && !(dev->sd && dev->sd->dev_write)
        && (dev->char_port.chardev.flags & CHAR_COM_WIRE)) {
        serial_transmit(dev, dev->txsr);
        dev->txsr_sent = 1;
    }

    serial_log("serial_move_to_txsr(): FIFO %sabled, FIFO pos = %i\n", dev->fifo_enabled ? "en" : "dis",
               fifo_get_count(dev->xmit_fifo) & 0x0f);

    if (!dev->fifo_enabled || (fifo_get_count(dev->xmit_fifo) == 0x0)) {
        /* Update interrupts to signal THRE and that TXSR is no longer empty. */
        serial_update_ints(dev);
    }
    if (!dev->fifo_enabled || (fifo_get_count(dev->xmit_fifo) == 0x0))
        dev->transmit_enabled &= ~1; /* Stop moving. */
    dev->transmit_enabled |= 2;      /* Start transmitting. */
}

static void
serial_process_txsr(serial_t *dev)
{
    serial_log("serial_process_txsr(): FIFO %sabled\n", dev->fifo_enabled ? "en" : "dis");
    if (!dev->txsr_sent)
        serial_transmit(dev, dev->txsr);
    dev->txsr_sent = 0;
    dev->txsr = 0;
    dev->txsr_empty = 1;
     serial_xmit_d_empty_evt(dev);
    /* Reset BAUDOUT cycle count. */
    dev->baud_cycles = 0;
    /* If FIFO is enabled and there are bytes left to transmit,
       continue with the FIFO, otherwise stop. */
    if (dev->fifo_enabled && (fifo_get_count(dev->xmit_fifo) != 0x0))
        dev->transmit_enabled |= 1;
    /* Both FIFO/THR and TXSR are empty. */
    else
        dev->transmit_enabled &= ~2;

    serial_update_ints(dev);
}

/* Transmit_enable flags:
        Bit 0 = Do move if set;
        Bit 1 = Do transmit if set. */
static void
serial_transmit_timer(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    if (!(dev->transmit_enabled & 3)) {
        dev->baud_cycles = 0;
        return;
    }

    dev->baud_cycles++;

    if (dev->transmit_enabled & 2) {
        /* baud_cycles is 1 at load, +1 per tick */
        if (dev->baud_cycles >= (dev->bits + 1)) {
            serial_process_txsr(dev);
            /* the next start bit follows the last stop bit
               directly */
            if (dev->transmit_enabled & 1) {
                serial_move_to_txsr(dev);
                dev->baud_cycles = 1;
            }
        }
    } else if (dev->transmit_enabled & 1) {
        /* idle line, THR has data:
            load TXSR on first tick after the write, start the frame count */
        serial_move_to_txsr(dev);
        dev->baud_cycles = 1;
    }

    /* 1.5 stop bits:
        frame-end tick comes half bit early. the next frame starts from that half tick. */
    if ((dev->transmit_enabled & 2) && dev->half_stop && (dev->baud_cycles == dev->bits))
        timer_on_auto(&dev->transmit_timer, 0.5 * dev->transmit_period);
    else if (dev->transmit_enabled & 3)
        timer_on_auto(&dev->transmit_timer, dev->transmit_period);
}

static void
serial_timeout_timer(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    serial_log("serial_timeout_timer()\n");

    /* a timeout only reports characters still waiting in the RX FIFO */
    if (!dev->fifo_enabled || fifo_get_empty(dev->rcvr_fifo))
        return;

    dev->lsr |= 0x01;
    dev->int_status |= SERIAL_INT_TIMEOUT;
    serial_update_ints(dev);
}

void
serial_device_timeout(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    serial_log("serial_device_timeout()\n");

    if (!dev->fifo_enabled) {
        dev->lsr |= 0x10;
        dev->int_status |= SERIAL_INT_LSR;
        serial_update_ints(dev);
    }
}

/* Start the host receive and status timers, begin polling */
static void
serial_start_polling(serial_t *dev)
{
    if (dev->base_address == 0x0000)
        return;

    timer_on_auto(&dev->receive_timer, serial_receive_period(dev));

    if (dev->char_port.chardev.status && !timer_is_enabled(&dev->status_timer))
        timer_on_auto(&dev->status_timer, SERIAL_STATUS_PERIOD_US);
}

static void
serial_update_speed(serial_t *dev)
{
    serial_log("serial_update_speed(%lf)\n", dev->transmit_period);
    serial_start_polling(dev);

    if (dev->transmit_enabled & 3)
        timer_on_auto(&dev->transmit_timer, dev->transmit_period);

    if (timer_is_enabled(&dev->timeout_timer))
        timer_on_auto(&dev->timeout_timer, 4.0 * serial_frame_bits(dev) * dev->transmit_period);
}

/* FIFO mode switch: THR and the TX FIFO are one store, so whatever is
   queued is dropped (the character already in TXSR still finishes), and a
   receive timeout for the old RX FIFO contents must not fire. */
static void
serial_reset_fifo(serial_t *dev)
{
    dev->thr_empty = 1;
    dev->transmit_enabled &= ~1;
    fifo_reset_evt(dev->xmit_fifo);
    fifo_reset_evt(dev->rcvr_fifo);

    serial_clear_timeout(dev);
}

void
serial_set_dsr(serial_t *dev, uint8_t enabled)
{
    if (dev->mctrl & 0x10)
        return;

    dev->msr &= ~0x2;
    dev->msr |= ((dev->msr & 0x20) ^ ((!!enabled) << 5)) >> 4;
    dev->msr &= ~0x20;
    dev->msr |= (!!enabled) << 5;
    dev->msr_set &= ~0x20;
    dev->msr_set |= (!!enabled) << 5;

    if (dev->msr & 0x2) {
        dev->int_status |= SERIAL_INT_MSR;
        serial_update_ints(dev);
    }
}

void
serial_set_cts(serial_t *dev, uint8_t enabled)
{
    if (dev->mctrl & 0x10)
        return;

    dev->msr &= ~0x1;
    dev->msr |= ((dev->msr & 0x10) ^ ((!!enabled) << 4)) >> 4;
    dev->msr &= ~0x10;
    dev->msr |= (!!enabled) << 4;
    dev->msr_set &= ~0x10;
    dev->msr_set |= (!!enabled) << 4;

    if (dev->msr & 0x1) {
        dev->int_status |= SERIAL_INT_MSR;
        serial_update_ints(dev);
    }
}

void
serial_set_dcd(serial_t *dev, uint8_t enabled)
{
    if (dev->mctrl & 0x10)
        return;

    dev->msr &= ~0x8;
    dev->msr |= ((dev->msr & 0x80) ^ ((!!enabled) << 7)) >> 4;
    dev->msr &= ~0x80;
    dev->msr |= (!!enabled) << 7;
    dev->msr_set &= ~0x80;
    dev->msr_set |= (!!enabled) << 7;

    if (dev->msr & 0x8) {
        dev->int_status |= SERIAL_INT_MSR;
        serial_update_ints(dev);
    }
}

void
serial_set_ri(serial_t *dev, uint8_t enabled)
{
    uint8_t prev_state = !!(dev->msr & 0x40);
    if (dev->mctrl & 0x10)
        return;

    dev->msr &= ~0x40;
    dev->msr |= (!!enabled) << 6;
    dev->msr_set &= ~0x40;
    dev->msr_set |= (!!enabled) << 6;

    if (prev_state == 0 && (!!enabled) == 1) {
        dev->msr |= 0x4;
        dev->int_status |= SERIAL_INT_MSR;
        serial_update_ints(dev);
    }
}

int
serial_get_ri(serial_t *dev)
{
    return !!(dev->msr & (1 << 6));
}

void
serial_set_clock_src(serial_t *dev, double clock_src)
{
    dev->clock_src = clock_src;

    serial_transmit_period(dev);
    serial_update_speed(dev);
}

void
serial_set_type(serial_t *dev, uint8_t type)
{
    /* 16450, 8250 UART has no FIFO */
    if ((type < SERIAL_16550) && (dev->type >= SERIAL_16550) && dev->fifo_enabled) {
        dev->fcr          = 0x00;
        dev->fifo_enabled = 0;
        serial_reset_fifo(dev);
        fifo_reset(dev->xmit_fifo);
        fifo_reset(dev->rcvr_fifo);
        dev->lsr &= ~0x80;
        serial_update_ints(dev);
    }
    dev->type = type;
}

void
serial_set_card_selected_feedback(serial_t *dev, uint8_t *reg_91)
{
    dev->reg_91 = reg_91;
}

void
serial_write(uint16_t addr, uint8_t val, void *priv)
{
    serial_t *dev = (serial_t *) priv;
    uint8_t   new_msr;
    uint8_t   old;

    serial_log("UART: [%04X:%08X] Write %02X to port %02X\n", CS, cpu_state.pc, val, addr);

    if (dev->reg_91 != NULL)
        *dev->reg_91 |= 0x01;

    cycles -= ISA_CYCLES(8);

    switch (addr & 7) {
        case 0:
            if (dev->lcr & 0x80) {
                dev->dlab = (dev->dlab & 0xff00) | val;
                serial_transmit_period(dev);
                serial_update_speed(dev);
                return;
            }

            if (dev->fifo_enabled && (fifo_get_count(dev->xmit_fifo) < 16)) {
                /* FIFO mode, begin transmitting: bit clock keeps
                   running while a character is tx'ed. only start it
                   from idle */
                if (!timer_is_enabled(&dev->transmit_timer))
                    timer_on_auto(&dev->transmit_timer, dev->transmit_period);
                dev->transmit_enabled |= 1;
                fifo_write_evt(val, dev->xmit_fifo);
            } else if (!dev->fifo_enabled) {
                /* flag THR = not empty */
                dev->lsr &= 0x9f;
                dev->int_status &= ~SERIAL_INT_TRANSMIT;
                serial_update_ints(dev);

                /* Non-FIFO mode: begin transmitting */
                if (!timer_is_enabled(&dev->transmit_timer))
                    timer_on_auto(&dev->transmit_timer, dev->transmit_period);
                dev->transmit_enabled |= 1;
                dev->thr = val;
                dev->thr_empty = 0;
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
            if (dev->type >= SERIAL_16550) {
                uint8_t mode_change = (val ^ dev->fcr) & 0x01;

                dev->fcr          = val & 0xf9;
                dev->fifo_enabled = val & 0x01;
                /* reset events recompute THRE/TEMT from the new mode. */
                if (mode_change)
                    serial_reset_fifo(dev);
                /* Only types >= SERIAL_16550 */

                if (!dev->fifo_enabled) {
                    fifo_reset(dev->xmit_fifo);
                    fifo_reset(dev->rcvr_fifo);
                    dev->lsr &= ~0x80; /* No RCVR FIFO, so no error in it. */
                    break;
                }
                if (val & 0x02) {
                    fifo_reset_evt(dev->rcvr_fifo);
                    serial_clear_timeout(dev);
                }
                if (val & 0x04) {
                    /* TXSR finishes its character, nothing follows it */
                    dev->transmit_enabled &= ~1;
                    fifo_reset_evt(dev->xmit_fifo);
                    serial_update_ints(dev);
                }
                switch ((val >> 6) & 0x03) {
                    case 0:
                        fifo_set_trigger_len(dev->rcvr_fifo, 1);
                        break;
                    case 1:
                        fifo_set_trigger_len(dev->rcvr_fifo, 4);
                        break;
                    case 2:
                        fifo_set_trigger_len(dev->rcvr_fifo, 8);
                        break;
                    case 3:
                        fifo_set_trigger_len(dev->rcvr_fifo, 14);
                        break;

                    default:
                        break;
                }
                fifo_set_trigger_len(dev->xmit_fifo, 16);
                dev->out_new = 0xffff;
                serial_log("FIFO now %sabled\n", dev->fifo_enabled ? "en" : "dis");
            }
            break;
        case 3:
            old      = dev->lcr;
            dev->lcr = val;
            if ((old ^ val) & 0x3f) {
                serial_lcr_frame(dev);

                serial_transmit_period(dev);
                serial_update_speed(dev);

                if (dev->sd && dev->sd->lcr_callback)
                    dev->sd->lcr_callback(dev, dev->sd->priv, dev->lcr);
            } else if ((old & 0x80) && !(val & 0x80) && dev->port_config_pending)
                /* divisor written with DLAB set, frame unchanged */
                serial_port_config(dev);
            if (!(old & 0x40) && (val & 0x40) && (dev->mctrl & 0x10))
                serial_receive_loopback_break(dev);
            if (((old ^ val) & 0x40) && dev->char_port.chardev.control) {
                dev->char_port.chardev.control((dev->mctrl & 0x03) | (val & 0x40), dev->char_port.chardev.priv);

                /* loopback plug: SYNC_RX answers a break immediately */
                if (!(old & 0x40) && (val & 0x40) && !(dev->mctrl & 0x10)
                    && (dev->char_port.chardev.flags & CHAR_COM_SYNC_RX) && serial_rx_take(dev))
                    serial_rx_deliver(dev);
            }
            break;
        case 4:
            old = dev->mctrl;
            if ((val & 2) && !(dev->mctrl & 2)) {
                if (dev->sd && dev->sd->rcr_callback) {
                    serial_log("RTS toggle callback\n");
                    dev->sd->rcr_callback(dev, dev->sd->priv);
                }
            }
            if (!(val & 8) && (dev->mctrl & 8))
                serial_do_irq(dev, 0);
            if (dev->sd && dev->sd->dtr_callback && (val ^ dev->mctrl) & 1)
                dev->sd->dtr_callback(dev, val & 1, dev->sd->priv);
            if (((dev->mctrl ^ val) & 0x03) && dev->char_port.chardev.control)
                dev->char_port.chardev.control((val & 0x03) | (dev->lcr & 0x40), dev->char_port.chardev.priv);
            dev->mctrl = val & 0x1f;
            if (val & 0x10) {
                new_msr = (val & 0x0c) << 4;
                new_msr |= (val & 0x02) ? 0x10 : 0;
                new_msr |= (val & 0x01) ? 0x20 : 0;
                new_msr |= dev->msr & 0x0f;

                if ((dev->msr ^ new_msr) & 0x10)
                    new_msr |= 0x01;
                if ((dev->msr ^ new_msr) & 0x20)
                    new_msr |= 0x02;
                if ((dev->msr ^ new_msr) & 0x80)
                    new_msr |= 0x08;
                if ((dev->msr & 0x40) && !(new_msr & 0x40))
                    new_msr |= 0x04;

                dev->msr = new_msr;

                if (dev->msr & 0x0f) {
                    dev->int_status |= SERIAL_INT_MSR;
                    serial_update_ints(dev);
                }
            } else if (old & 0x10) {
                /*
                 * Leaving diagnostic loopback reconnects the MSR condition
                 * bits to the external modem-status inputs.  The transition
                 * also latches DCTS, DDSR, TERI, and DDCD exactly as an 8250
                 * input transition would.
                 */
                new_msr = (dev->msr_set & 0xf0) | (dev->msr & 0x0f);
                /* host port lines are sampled by the status timer, which
                   skips loopback: read them now, msr_set doesn't hold them */
                if (dev->char_port.chardev.status)
                    new_msr |= dev->char_port.chardev.status(dev->char_port.chardev.priv) &
                               (CHAR_COM_CTS | CHAR_COM_DSR | CHAR_COM_RI | CHAR_COM_DCD);

                if ((dev->msr ^ new_msr) & 0x10)
                    new_msr |= 0x01;
                if ((dev->msr ^ new_msr) & 0x20)
                    new_msr |= 0x02;
                if ((dev->msr & 0x40) && !(new_msr & 0x40))
                    new_msr |= 0x04;
                if ((dev->msr ^ new_msr) & 0x80)
                    new_msr |= 0x08;

                dev->msr = new_msr;

                if (dev->msr & 0x0f) {
                    dev->int_status |= SERIAL_INT_MSR;
                    serial_update_ints(dev);
                }
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
#if 0
            dev->msr = (val & 0xf0) | (dev->msr & 0x0f);
            dev->msr = val;
#endif
            /* The actual condition bits of the MSR are read-only, but the delta bits are
               undocumentedly writable, and the PCjr BIOS uses them to raise MSR interrupts. */
            dev->msr = (dev->msr & 0xf0) | (val & 0x0f);
            if (dev->msr & 0x0f)
                dev->int_status |= SERIAL_INT_MSR;
            else
                dev->int_status &= ~SERIAL_INT_MSR;
            serial_update_ints(dev);
            break;
        case 7:
            if (dev->type >= SERIAL_16450)
                dev->scratch = val;
            break;
        default:
            break;
    }
}

uint8_t
serial_read(uint16_t addr, void *priv)
{
    serial_t *dev = (serial_t *) priv;
    uint8_t   ret = 0;

    if (dev->reg_91 != NULL)
        *dev->reg_91 |= 0x01;

    cycles -= ISA_CYCLES(8);

    switch (addr & 7) {
        case 0:
            if (dev->lcr & 0x80) {
                ret = dev->dlab & 0xff;
                break;
            }

            if (dev->fifo_enabled) {
                /* FIFO mode. */
                serial_clear_timeout(dev);
                ret = fifo_read_evt(dev->rcvr_fifo);
                serial_rcvr_fifo_top(dev);

                if (dev->lsr & 0x01)
                    timer_on_auto(&dev->timeout_timer, 4.0 * serial_frame_bits(dev) * dev->transmit_period);
            } else {
                /* Non-FIFO mode. */
                ret = dev->dat;

                /* Always clear Data Ready interrupt. */
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
            /* Error in RCVR FIFO stays set while an errored character is left. */
            if (!dev->fifo_enabled || !fifo_get_tags(dev->rcvr_fifo))
                dev->lsr &= ~0x80;
            if (dev->type >= SERIAL_16550)
                fifo_clear_overrun(dev->rcvr_fifo);
            dev->int_status &= ~SERIAL_INT_LSR;
            serial_update_ints(dev);
            break;
        case 6:
            if (dev->mctrl & 0x10)
                ret = dev->msr;
            else
                ret = dev->msr | dev->msr_set;
            dev->msr &= ~0x0f;
            dev->int_status &= ~SERIAL_INT_MSR;
            serial_update_ints(dev);
            break;
        case 7:
            ret = dev->scratch;
            break;
        default:
            break;
    }

    serial_log("UART: [%04X:%08X] Read %02X from port %02X\n", CS, cpu_state.pc, ret, addr);
    return ret;
}

uint8_t
serial_get_shadow(serial_t *dev)
{
    uint8_t ret = dev->fcr;

    return ret;
}

void
serial_remove(serial_t *dev)
{
    if (dev == NULL)
        return;

    if (!com_ports[dev->inst].enabled)
        return;

    if (!dev->base_address)
        return;

    serial_log("Removing serial port %i at %04X...\n", dev->inst, dev->base_address);

    io_removehandler(dev->base_address, 0x0008,
                     serial_read, NULL, NULL, serial_write, NULL, NULL, dev);
    dev->base_address = 0x0000;

    /* stop polling the host port until serial_setup() maps it again */
    timer_disable(&dev->receive_timer);
    timer_disable(&dev->status_timer);
}

void
serial_setup(serial_t *dev, uint16_t addr, uint8_t irq)
{
    serial_log("Adding serial port %i at %04X...\n", dev->inst, addr);

    if (dev == NULL)
        return;

    if (!com_ports[dev->inst].enabled)
        return;
    if (dev->base_address != 0x0000)
        serial_remove(dev);
    dev->base_address = addr;
    if (addr != 0x0000) {
        io_sethandler(addr, 0x0008, serial_read, NULL, NULL, serial_write, NULL, NULL, dev);
        serial_start_polling(dev);
    }
    dev->irq = irq;
}

void
serial_irq(serial_t *dev, const uint8_t irq)
{
    if (dev == NULL)
        return;

    if (com_ports[dev->inst].enabled)
        dev->irq = irq;
    else
        dev->irq = 0xff;

    serial_log("Port %i IRQ = %02X\n", dev->inst, irq);
}

static void
serial_rcvr_d_empty_evt(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    dev->lsr = (dev->lsr & 0xfe) | (fifo_get_empty(dev->rcvr_fifo) ? 0 : 1);
}

static void
serial_rcvr_d_overrun_evt(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    dev->lsr = (dev->lsr & 0xfd) | (fifo_get_overrun(dev->rcvr_fifo) << 1);

    /* OE raises the line status interrupt as in non-FIFO mode */
    if (dev->lsr & 0x02) {
        dev->int_status |= SERIAL_INT_LSR;
        serial_update_ints(dev);
    }
}

static void
serial_rcvr_d_ready_evt(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    dev->int_status = (dev->int_status & ~SERIAL_INT_RECEIVE) |
                      (fifo_get_ready(dev->rcvr_fifo) ? SERIAL_INT_RECEIVE : 0);
    serial_update_ints(dev);
}

static void
serial_xmit_d_empty_evt(void *priv)
{
    serial_t *dev = (serial_t *) priv;
    uint8_t is_empty = dev->fifo_enabled ? fifo_get_empty(dev->xmit_fifo) : dev->thr_empty;

    dev->lsr = (dev->lsr & 0x9f) | (is_empty << 5) | ((dev->txsr_empty && is_empty) << 6);
    dev->int_status = (dev->int_status & ~SERIAL_INT_TRANSMIT) | (is_empty ? SERIAL_INT_TRANSMIT : 0);
}

serial_t *
serial_attach_ex(int port,
                 void (*rcr_callback)(struct serial_s *serial, void *priv),
                 void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data),
                 void (*transmit_period_callback)(struct serial_s *serial, void *priv, double transmit_period),
                 void (*lcr_callback)(struct serial_s *serial, void *priv, uint8_t data_bits),
                 void *priv)
{
    /* Make sure this port becomes non-hotunpluggable if a hotunpluggable dropdown
       device and a non-hotunpluggable external device are both trying to claim it. */
    uint8_t hotunplug         = com_ports[port].hotunplug;
    com_ports[port].hotunplug = CHAR_PORT_NOHOTUNPLUG;
    if (hotunplug != CHAR_PORT_DETACHED)
        return NULL;

    serial_device_t *sd = &serial_devices[port];

    sd->rcr_callback             = rcr_callback;
    sd->dev_write                = dev_write;
    sd->transmit_period_callback = transmit_period_callback;
    sd->lcr_callback             = lcr_callback;
    sd->priv                     = priv;

    return com_ports[port].serial;
}

serial_t *
serial_attach_ex_2(int port,
                   void (*rcr_callback)(struct serial_s *serial, void *priv),
                   void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data),
                   void (*dtr_callback)(struct serial_s *serial, int status, void *priv),
                   void *priv)
{
    /* Make sure this port becomes non-hotunpluggable if a hotunpluggable dropdown
       device and a non-hotunpluggable external device are both trying to claim it. */
    uint8_t hotunplug         = com_ports[port].hotunplug;
    com_ports[port].hotunplug = CHAR_PORT_NOHOTUNPLUG;
    if (hotunplug != CHAR_PORT_DETACHED)
        return NULL;

    serial_device_t *sd = &serial_devices[port];

    sd->rcr_callback             = rcr_callback;
    sd->dtr_callback             = dtr_callback;
    sd->dev_write                = dev_write;
    sd->transmit_period_callback = NULL;
    sd->lcr_callback             = NULL;
    sd->priv                     = priv;

    return com_ports[port].serial;
}

void
serial_devices_init(void)
{
    for (uint8_t i = 0; i < SERIAL_MAX; i++) {
        /* Leave non-hotunpluggable ports and their devices alone. */
        if (com_ports[i].hotunplug >= CHAR_PORT_NOHOTUNPLUG)
            continue;

        memset(&(serial_devices[i]), 0, sizeof(serial_device_t));

        com_ports[i].hotunplug = CHAR_PORT_DETACHED;

        serial_t *serial = com_ports[i].serial;
        if (!serial)
            continue;

        memset(&serial->char_port, 0, sizeof(serial->char_port));
        if (com_ports[i].device) {
            serial->char_port.type = CHAR_PORT_COM;
            snprintf(serial->char_port.name, sizeof(serial->char_port.name), "COM%i", i + 1);
            /* preserve port settings on output device swap */
            if (serial->clock_src > 0.0)
                serial_update_com(serial);
            const device_t *device = char_get_device(com_ports[i].device);
            char_init(&serial->char_port, device, i + 1);

            if (serial->clock_src > 0.0)
                serial_port_config(serial);

            if (serial->char_port.chardev.control)
                serial->char_port.chardev.control((serial->mctrl & 0x03) | (serial->lcr & 0x40), serial->char_port.chardev.priv);

            /* check for new sample, status lines may have changed */
            serial_start_polling(serial);

            /* This port is hotunpluggable if the device allows it, unless further serial_attach attempts are made. */
            com_ports[i].hotunplug = (device->flags & DEVICE_HOTPLUG_OUT) ? CHAR_PORT_HOTUNPLUG : CHAR_PORT_NOHOTUNPLUG;
        }
    }
}

void
serial_devices_close(int hotplug)
{
    for (uint8_t i = 0; i < SERIAL_MAX; i++) {
        /* Leave non-hotunpluggable ports and their devices alone in a hotplug operation. */
        if (hotplug && (com_ports[i].hotunplug >= CHAR_PORT_NOHOTUNPLUG))
            continue;
        com_ports[i].hotunplug = CHAR_PORT_DETACHED;

        memset(&(serial_devices[i]), 0, sizeof(serial_device_t));

        if (com_ports[i].serial)
            memset(&com_ports[i].serial->char_port, 0, sizeof(char_port_t));
    }
}

void
serial_devices_reset(void)
{
    device_close_by_flags(DEVICE_COM | DEVICE_HOTPLUG_OUT);

    serial_devices_close(1);

    serial_devices_init();
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

    if (dev->sd || dev->char_port.type) {
        if (dev->sd)
            memset(dev->sd, 0, sizeof(serial_device_t));
        fifo_close(dev->rcvr_fifo);
    }

    /*
       Needed to avoid a segmentation fault at serial_devices_init() for any serial port
       that used be enabled and was then disabled.
     */
    com_ports[dev->inst].serial = NULL;

    free(dev);
}

static void
serial_reset(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    if (dev->sd || dev->char_port.type) {
        timer_disable(&dev->transmit_timer);
        timer_disable(&dev->timeout_timer);
        timer_disable(&dev->receive_timer);
        timer_disable(&dev->status_timer);

        dev->lsr = dev->thr = dev->mctrl = dev->rcr = 0x00;
        dev->iir = dev->ier = dev->lcr = dev->msr = 0x00;
        dev->dat = dev->int_status = dev->scratch = dev->fcr = 0x00;
        dev->fifo_enabled = dev->bits = dev->half_stop = 0x000;
        dev->data_bits = dev->baud_cycles = 0x00;
        dev->txsr = 0x00;
        dev->txsr_empty = 0x01;
        dev->thr_empty = 0x0001;

        dev->dlab = dev->out_new = 0x0000;

        if (dev->rcvr_fifo != NULL)
            fifo_reset(dev->rcvr_fifo);

        serial_reset_port(dev);

        dev->dlab      = 96;
        dev->fcr       = 0x06;

        serial_transmit_period(dev);
        serial_update_speed(dev);
    }
}

static void *
serial_init(const device_t *info)
{
    serial_t *dev = (serial_t *) calloc(1, sizeof(serial_t));
    int orig_inst = next_inst;

    if (info->local & 0xFFF00000)
        next_inst = SERIAL_MAX - 1;

    dev->inst = next_inst;

    if (com_ports[next_inst].enabled || (info->local & 0xFFF00000)) {
        serial_log("Adding serial port %i...\n", next_inst);
        dev->type = info->local;
        dev->sd         = &(serial_devices[next_inst]);

        com_ports[next_inst].serial    = dev;
        com_ports[next_inst].hotunplug = CHAR_PORT_DETACHED;

        /* Default to 1200,N,7. The timers must exist before serial_setup(),
           which starts polling as soon as the port is mapped. */
        dev->dlab = 96;
        dev->fcr  = 0x06;
        if ((info->local == SERIAL_8250_PCJR_3F8) || (info->local == SERIAL_8250_PCJR_2F8))
            dev->clock_src = 1789500.0;
        else
            dev->clock_src = 1843200.0;
        timer_add(&dev->transmit_timer, serial_transmit_timer, dev, 0);
        timer_add(&dev->timeout_timer, serial_timeout_timer, dev, 0);
        timer_add(&dev->receive_timer, serial_receive_timer, dev, 0);
        timer_add(&dev->status_timer, serial_status_timer, dev, 0);
        serial_transmit_period(dev);

        if (info->local & 0xfff00000) {
            dev->base_address = info->local >> 20;
            dev->irq          = (info->local >> 16) & 0xF;
            io_sethandler(dev->base_address, 0x0008, serial_read, NULL, NULL, serial_write, NULL, NULL, dev);
            next_inst = orig_inst;
        } else if (next_inst == 6)
            serial_setup(dev, COM7_ADDR, COM7_IRQ);
        else if (next_inst == 5)
            serial_setup(dev, COM6_ADDR, COM6_IRQ);
        else if (next_inst == 4)
            serial_setup(dev, COM5_ADDR, COM5_IRQ);
        else if (next_inst == 3)
            serial_setup(dev, COM4_ADDR, COM4_IRQ);
        else if (next_inst == 2)
            serial_setup(dev, COM3_ADDR, COM3_IRQ);
        else if ((next_inst == 1) || (info->local == SERIAL_8250_PCJR_2F8))
            serial_setup(dev, COM2_ADDR, COM2_IRQ);
        // TODO
#if 0
        else if ((next_inst == 1) || (info->local == SERIAL_8250_PCJR_3F8))
            serial_setup(dev, COM1_ADDR, COM1_IRQ);
#endif
        else if (next_inst == 0)
            serial_setup(dev, COM1_ADDR, COM1_IRQ);

        serial_update_speed(dev);

        dev->rcvr_fifo = fifo64_init();
        fifo_set_priv(dev->rcvr_fifo, dev);
        fifo_set_d_empty_evt(dev->rcvr_fifo, serial_rcvr_d_empty_evt);
        fifo_set_d_overrun_evt(dev->rcvr_fifo, serial_rcvr_d_overrun_evt);
        fifo_set_d_ready_evt(dev->rcvr_fifo, serial_rcvr_d_ready_evt);
        fifo_reset_evt(dev->rcvr_fifo);
        fifo_set_len(dev->rcvr_fifo, 16);

        dev->xmit_fifo = fifo64_init();
        fifo_set_priv(dev->xmit_fifo, dev);
        fifo_set_d_empty_evt(dev->xmit_fifo, serial_xmit_d_empty_evt);
        fifo_reset_evt(dev->xmit_fifo);
        fifo_set_len(dev->xmit_fifo, 16);

        serial_reset_port(dev);
    }

    if (!(info->local & 0xfff00000))
        next_inst++;

    return dev;
}

void
serial_set_next_inst(int ni)
{
    next_inst = ni;
}

void
serial_standalone_init(void)
{
    while (next_inst < (SERIAL_MAX - 1))
        device_add_inst(&ns8250_device, next_inst + 1);
}

const device_t ns8250_device = {
    .name          = "National Semiconductor 8250(-compatible) UART",
    .internal_name = "ns8250",
    .flags         = 0,
    .local         = SERIAL_8250,
    .init          = serial_init,
    .close         = serial_close,
    .reset         = serial_reset,
    .available     = NULL,
    .speed_changed = serial_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ns8250_pcjr_3f8_device = {
    .name          = "National Semiconductor 8250(-compatible) UART for PCjr (0x3f8)",
    .internal_name = "ns8250_pcjr_3f8",
    .flags         = 0,
    .local         = SERIAL_8250_PCJR_3F8,
    .init          = serial_init,
    .close         = serial_close,
    .reset         = serial_reset,
    .available     = NULL,
    .speed_changed = serial_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ns8250_pcjr_2f8_device = {
    .name          = "National Semiconductor 8250(-compatible) UART for PCjr (0x2f8)",
    .internal_name = "ns8250_pcjr_2f8",
    .flags         = 0,
    .local         = SERIAL_8250_PCJR_2F8,
    .init          = serial_init,
    .close         = serial_close,
    .reset         = serial_reset,
    .available     = NULL,
    .speed_changed = serial_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ns16450_device = {
    .name          = "National Semiconductor NS16450(-compatible) UART",
    .internal_name = "ns16450",
    .flags         = 0,
    .local         = SERIAL_16450,
    .init          = serial_init,
    .close         = serial_close,
    .reset         = serial_reset,
    .available     = NULL,
    .speed_changed = serial_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ns16550_device = {
    .name          = "National Semiconductor NS16550(-compatible) UART",
    .internal_name = "ns16550",
    .flags         = 0,
    .local         = SERIAL_16550,
    .init          = serial_init,
    .close         = serial_close,
    .reset         = serial_reset,
    .available     = NULL,
    .speed_changed = serial_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ns16650_device = {
    .name          = "Startech Semiconductor 16650(-compatible) UART",
    .internal_name = "ns16650",
    .flags         = 0,
    .local         = SERIAL_16650,
    .init          = serial_init,
    .close         = serial_close,
    .reset         = serial_reset,
    .available     = NULL,
    .speed_changed = serial_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ns16750_device = {
    .name          = "Texas Instruments 16750(-compatible) UART",
    .internal_name = "ns16750",
    .flags         = 0,
    .local         = SERIAL_16750,
    .init          = serial_init,
    .close         = serial_close,
    .reset         = serial_reset,
    .available     = NULL,
    .speed_changed = serial_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ns16850_device = {
    .name          = "Exar Corporation NS16850(-compatible) UART",
    .internal_name = "ns16850",
    .flags         = 0,
    .local         = SERIAL_16850,
    .init          = serial_init,
    .close         = serial_close,
    .reset         = serial_reset,
    .available     = NULL,
    .speed_changed = serial_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ns16950_device = {
    .name          = "Oxford Semiconductor NS16950(-compatible) UART",
    .internal_name = "ns16950",
    .flags         = 0,
    .local         = SERIAL_16950,
    .init          = serial_init,
    .close         = serial_close,
    .reset         = serial_reset,
    .available     = NULL,
    .speed_changed = serial_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
