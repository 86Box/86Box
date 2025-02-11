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
 *
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
#include <86box/timer.h>
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/fifo.h>
#include <86box/serial.h>
#include <86box/mouse.h>

serial_port_t com_ports[SERIAL_MAX];

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

    dev->fifo_enabled                         = 0;
    dev->baud_cycles                          = 0;
    dev->out_new                              = 0xffff;

    dev->txsr_empty = 1;
    dev->thr_empty  = 1;

    serial_update_ints(dev);
    dev->irq_state = 0;
}

void
serial_transmit_period(serial_t *dev)
{
    double ddlab;

    if (dev->dlab != 0x0000)
        ddlab = (double) dev->dlab;
    else
        ddlab = 65536.0;

    /* Bit period based on DLAB. */
    dev->transmit_period = (16000000.0 * ddlab) / dev->clock_src;
    if (dev->sd && dev->sd->transmit_period_callback)
        dev->sd->transmit_period_callback(dev, dev->sd->priv, dev->transmit_period);
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
    /* TODO: The IRQ priorities are 6 - we need to find a way to treat timeout and receive
             as equal and still somehow distinguish them. */
    uint8_t ier_map[7] = { 0x04, 0x01, 0x01, 0x02, 0x08, 0x40, 0x80 };
    uint8_t iir_map[7] = { 0x06, 0x0c, 0x04, 0x02, 0x00, 0x0e, 0x0a };

    dev->iir = (dev->iir & 0xf0) | 0x01;

    for (uint8_t i = 0; i < 7; i++) {
        if ((dev->ier & ier_map[i]) && (dev->int_status & (1 << i))) {
            dev->iir = (dev->iir & 0xf0) | iir_map[i];
            break;
        }
    }

    serial_do_irq(dev, !(dev->iir & 0x01) && ((dev->mctrl & 8) || (dev->type == SERIAL_8250_PCJR)));
}

static void
serial_clear_timeout(serial_t *dev)
{
    /* Disable timeout timer and clear timeout condition. */
    timer_disable(&dev->timeout_timer);
    dev->int_status &= ~SERIAL_INT_TIMEOUT;
    serial_update_ints(dev);
}

static void
serial_receive_timer(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    serial_log("serial_receive_timer()\n");

    timer_on_auto(&dev->receive_timer, /* dev->bits * */ dev->transmit_period);

    if (dev->fifo_enabled) {
        /* FIFO mode. */
        if (dev->out_new != 0xffff) {
            /* We have received a byte into the RSR. */

            /* Clear FIFO timeout. */
            serial_clear_timeout(dev);

            fifo_write_evt((uint8_t) (dev->out_new & 0xff), dev->rcvr_fifo);
            dev->out_new = 0xffff;

#if 0
            pclog("serial_receive_timer(): lsr = %02X, ier = %02X, iir = %02X, int_status = %02X\n",
                  dev->lsr, dev->ier, dev->iir, dev->int_status);
#endif

            timer_on_auto(&dev->timeout_timer, 4.0 * dev->bits * dev->transmit_period);
        }
    } else {
        /* Non-FIFO mode. */
        if (dev->out_new != 0xffff) {
            /* We have received a byte into the RSR. */
            serial_log("Byte received: %04X\n", dev->out_new);

            /* Indicate overrun. */
            if (dev->lsr & 0x01)
                dev->lsr |= 0x02;

            dev->dat = (uint8_t) (dev->out_new & 0xff);
            dev->out_new = 0xffff;

            /* Raise Data Ready interrupt. */
            dev->lsr |= 0x01;
            dev->int_status |= SERIAL_INT_RECEIVE;
            if (dev->lsr & 0x02)
                dev->int_status |= SERIAL_INT_LSR;

            serial_update_ints(dev);
        }
    }
}

static void
write_fifo(serial_t *dev, uint8_t dat)
{
    serial_log("write_fifo(%08X, %02X, %i, %i)\n", dev, dat,
               (dev->type >= SERIAL_16550) && dev->fifo_enabled,
               ((dev->type >= SERIAL_16550) && dev->fifo_enabled) ?
               fifo_get_count(dev->rcvr_fifo) : 0);

    /* Do this here, because in non-FIFO mode, this is read directly. */
    dev->out_new = (uint16_t) dat;
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
    if (dev->mctrl & 0x10)
        write_fifo(dev, val);
    else if (dev->sd->dev_write)
        dev->sd->dev_write(dev, dev->sd->priv, val);

#ifdef ENABLE_SERIAL_CONSOLE
    if ((val >= ' ' && val <= '~') || val == '\r' || val == '\n') {
        fputc(val, stdout);
        if (val == '\n')
            fflush(stdout);
    } else
        fprintf(stdout, "[%02X]", val);
#endif
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
    serial_transmit(dev, dev->txsr);
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
    serial_t *dev   = (serial_t *) priv;
    /*
       Norton Diagnostics waits for up to 2 bit periods, this is
       confirmed by the NS16550A timings graph, which shows operation
       as follows after write: 1 bit of delay, then start bit, and at
       the end of the start bit, move from THR to TXSR.
     */
    int       delay = 1;

    if (dev->transmit_enabled & 3) {
        /*
           If already transmitting, move from THR to TXSR at the end of
           the last data bit.
         */
        if ((dev->transmit_enabled & 1) && (dev->transmit_enabled & 2))
            delay = dev->data_bits + 1;

        dev->baud_cycles++;

        /* We have processed (delay + total bits) BAUDOUT cycles, transmit the byte. */
        if ((dev->baud_cycles == (dev->bits + 1)) && (dev->transmit_enabled & 2))
            serial_process_txsr(dev);

        /* We have processed (data bits) BAUDOUT cycles. */
        if ((dev->baud_cycles == delay) && (dev->transmit_enabled & 1))
            serial_move_to_txsr(dev);

        if (dev->transmit_enabled & 3)
            timer_on_auto(&dev->transmit_timer, dev->transmit_period);
    } else {
        dev->baud_cycles = 0;
        return;
    }
}

static void
serial_timeout_timer(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    serial_log("serial_timeout_timer()\n");

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

static void
serial_update_speed(serial_t *dev)
{
    serial_log("serial_update_speed(%lf)\n", dev->transmit_period);
    timer_on_auto(&dev->receive_timer, /* dev->bits * */ dev->transmit_period);

    if (dev->transmit_enabled & 3)
        timer_on_auto(&dev->transmit_timer, dev->transmit_period);

    if (timer_is_on(&dev->timeout_timer))
        timer_on_auto(&dev->timeout_timer, 4.0 * dev->bits * dev->transmit_period);
}

static void
serial_reset_fifo(serial_t *dev)
{
    fifo_reset_evt(dev->xmit_fifo);
    fifo_reset_evt(dev->rcvr_fifo);

    serial_update_ints(dev);
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
serial_write(uint16_t addr, uint8_t val, void *priv)
{
    serial_t *dev = (serial_t *) priv;
    uint8_t   new_msr;
    uint8_t   old;

    serial_log("UART: [%04X:%08X] Write %02X to port %02X\n", CS, cpu_state.pc, val, addr);

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
                /* FIFO mode, begin transmitting. */
                timer_on_auto(&dev->transmit_timer, dev->transmit_period);
                dev->transmit_enabled |= 1; /* Start moving. */
                fifo_write_evt(val, dev->xmit_fifo);
            } else if (!dev->fifo_enabled) {
                /* Indicate THR is no longer empty. */
                dev->lsr &= 0x9f;
                dev->int_status &= ~SERIAL_INT_TRANSMIT;
                serial_update_ints(dev);

                /* Non-FIFO mode, begin transmitting. */
                timer_on_auto(&dev->transmit_timer, dev->transmit_period);
                dev->transmit_enabled |= 1; /* Start moving. */
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
                if ((val ^ dev->fcr) & 0x01)
                    serial_reset_fifo(dev);
                dev->fcr          = val & 0xf9;
                dev->fifo_enabled = val & 0x01;
                /* TODO: When switching modes, shouldn't we reset the LSR
                         based on the new conditions? */
                if (!dev->fifo_enabled) {
                    fifo_reset(dev->xmit_fifo);
                    fifo_reset(dev->rcvr_fifo);
                    break;
                }
                if (val & 0x02) {
                    if (dev->fifo_enabled)
                        fifo_reset_evt(dev->rcvr_fifo);
                    else
                        fifo_reset(dev->rcvr_fifo);
                }
                if (val & 0x04) {
                    if (dev->fifo_enabled)
                        fifo_reset_evt(dev->xmit_fifo);
                    else
                        fifo_reset(dev->xmit_fifo);
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
                /* Data bits + start bit. */
                dev->bits = ((dev->lcr & 0x03) + 5) + 1;
                /* Stop bits. */
                dev->bits++; /* First stop bit. */
                if (dev->lcr & 0x04)
                    dev->bits++; /* Second stop bit. */
                /* Parity bit. */
                if (dev->lcr & 0x08)
                    dev->bits++;

                serial_transmit_period(dev);
                serial_update_speed(dev);

                if (dev->sd && dev->sd->lcr_callback)
                    dev->sd->lcr_callback(dev, dev->sd->priv, dev->lcr);
            }
            break;
        case 4:
            if ((val & 2) && !(dev->mctrl & 2)) {
                if (dev->sd && dev->sd->rcr_callback) {
                    serial_log("RTS toggle callback\n");
                    dev->sd->rcr_callback(dev, dev->sd->priv);
                }
            }
            if (!(val & 8) && (dev->mctrl & 8))
                serial_do_irq(dev, 0);
            if ((val ^ dev->mctrl) & 0x10)
                serial_reset_fifo(dev);
            if (dev->sd && dev->sd->dtr_callback && (val ^ dev->mctrl) & 1)
                dev->sd->dtr_callback(dev, val & 1, dev->sd->priv);
            dev->mctrl = val & 0x1f;
            if (val & 0x10) {
                new_msr = (val & 0x0c) << 4;
                new_msr |= (val & 0x02) ? 0x10 : 0;
                new_msr |= (val & 0x01) ? 0x20 : 0;

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

                /* TODO: Why reset the FIFO's here?! */
                fifo_reset(dev->xmit_fifo);
                fifo_reset(dev->rcvr_fifo);
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

                if (dev->lsr & 0x01)
                    timer_on_auto(&dev->timeout_timer, 4.0 * dev->bits * dev->transmit_period);
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
    if (addr != 0x0000)
        io_sethandler(addr, 0x0008, serial_read, NULL, NULL, serial_write, NULL, NULL, dev);
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
    serial_device_t *sd = &serial_devices[port];

    sd->rcr_callback             = rcr_callback;
    sd->dev_write                = dev_write;
    sd->transmit_period_callback = transmit_period_callback;
    sd->lcr_callback             = lcr_callback;
    sd->priv                     = priv;

    return sd->serial;
}

serial_t *
serial_attach_ex_2(int port,
                 void (*rcr_callback)(struct serial_s *serial, void *priv),
                 void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data),
                 void (*dtr_callback)(struct serial_s *serial, int status, void *priv),
                 void *priv)
{
    serial_device_t *sd = &serial_devices[port];

    sd->rcr_callback             = rcr_callback;
    sd->dtr_callback             = dtr_callback;
    sd->dev_write                = dev_write;
    sd->transmit_period_callback = NULL;
    sd->lcr_callback             = NULL;
    sd->priv                     = priv;

    return sd->serial;
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

    next_inst--;

    if (com_ports[dev->inst].enabled)
        fifo_close(dev->rcvr_fifo);

    free(dev);
}

static void
serial_reset(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    if (com_ports[dev->inst].enabled) {
        timer_disable(&dev->transmit_timer);
        timer_disable(&dev->timeout_timer);
        timer_disable(&dev->receive_timer);

        dev->lsr = dev->thr = dev->mctrl = dev->rcr = 0x00;
        dev->iir = dev->ier = dev->lcr = dev->msr = 0x00;
        dev->dat = dev->int_status = dev->scratch = dev->fcr = 0x00;
        dev->fifo_enabled = dev->bits = 0x000;
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

    dev->inst = next_inst;

    if (com_ports[next_inst].enabled) {
        serial_log("Adding serial port %i...\n", next_inst);
        dev->type = info->local;
        memset(&(serial_devices[next_inst]), 0, sizeof(serial_device_t));
        dev->sd         = &(serial_devices[next_inst]);
        dev->sd->serial = dev;
        if (next_inst == 6)
            serial_setup(dev, COM7_ADDR, COM7_IRQ);
        else if (next_inst == 5)
            serial_setup(dev, COM6_ADDR, COM6_IRQ);
        else if (next_inst == 4)
            serial_setup(dev, COM5_ADDR, COM5_IRQ);
        else if (next_inst == 3)
            serial_setup(dev, COM4_ADDR, COM4_IRQ);
        else if (next_inst == 2)
            serial_setup(dev, COM3_ADDR, COM3_IRQ);
        else if ((next_inst == 1) || (info->flags & DEVICE_PCJR))
            serial_setup(dev, COM2_ADDR, COM2_IRQ);
        else if (next_inst == 0)
            serial_setup(dev, COM1_ADDR, COM1_IRQ);

        /* Default to 1200,N,7. */
        dev->dlab = 96;
        dev->fcr  = 0x06;
        if (info->local == SERIAL_8250_PCJR)
            dev->clock_src = 1789500.0;
        else
            dev->clock_src = 1843200.0;
        timer_add(&dev->transmit_timer, serial_transmit_timer, dev, 0);
        timer_add(&dev->timeout_timer, serial_timeout_timer, dev, 0);
        timer_add(&dev->receive_timer, serial_receive_timer, dev, 0);
        serial_transmit_period(dev);
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
    while (next_inst < SERIAL_MAX)
        device_add_inst(&ns8250_device, next_inst + 1);
};

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

const device_t ns8250_pcjr_device = {
    .name          = "National Semiconductor 8250(-compatible) UART for PCjr",
    .internal_name = "ns8250_pcjr",
    .flags         = DEVICE_PCJR,
    .local         = SERIAL_8250_PCJR,
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
