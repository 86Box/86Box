/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          FIFO infrastructure.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023-2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef FIFO_STANDALONE
#define fatal printf
#define pclog_ex printf
#define pclog printf
#include "include/86box/fifo.h"
#else
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/fifo.h>
#endif

#ifdef ENABLE_FIFO_LOG
int fifo_do_log = ENABLE_FIFO_LOG;

static void
fifo_log(const char *fmt, ...)
{
    va_list ap;

    if (fifo_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define fifo_log(fmt, ...)
#endif

int
fifo_get_count(void *priv)
{
    const fifo_t *fifo = (fifo_t *) priv;
    int           ret  = fifo->len;

    if (fifo->end == fifo->start)
        ret = fifo->full ? fifo->len : 0;
    else
        ret = abs(fifo->end - fifo->start);

    return ret;
}

void
fifo_write(uint8_t val, void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->d_full  = fifo->d_empty = 0;
    fifo->d_ready = fifo->d_overrun = 0;

    if (fifo->full)
        fifo->overrun = 1;
    else {
        fifo->buf[fifo->end] = val;
        fifo->end            = (fifo->end + 1) % fifo->len;

        if (fifo->end == fifo->start)
            fifo->full = 1;

        fifo->empty = 0;

        if (fifo_get_count(fifo) >= fifo->trigger_len)
            fifo->ready = 1;
    }
}

void
fifo_write_tagged(uint8_t tag, uint8_t val, void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->d_full  = fifo->d_empty = 0;
    fifo->d_ready = fifo->d_overrun = 0;

    if (fifo->full)
        fifo->overrun = 1;
    else {
        fifo->buf[fifo->end] = val;
        fifo->tag[fifo->end] = tag;
        fifo->end            = (fifo->end + 1) % fifo->len;

        if (fifo->end == fifo->start)
            fifo->full = 1;

        fifo->empty = 0;

        if (fifo_get_count(fifo) >= fifo->trigger_len)
            fifo->ready = 1;
    }
}

void
fifo_write_evt(uint8_t val, void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->d_full  = fifo->d_empty = 0;
    fifo->d_ready = fifo->d_overrun = 0;

    if (fifo->full) {
        fifo->d_overrun = (fifo->overrun != 1);
        fifo->overrun   = 1;
        if (fifo->d_overrun && (fifo->d_overrun_evt != NULL))
            fifo->d_overrun_evt(fifo->priv);
    } else {
        fifo->buf[fifo->end] = val;
        fifo->end            = (fifo->end + 1) % fifo->len;

        if (fifo->end == fifo->start) {
            fifo->d_full = (fifo->full != 1);
            fifo->full   = 1;
            if (fifo->d_full && (fifo->d_full_evt != NULL))
                fifo->d_full_evt(fifo->priv);
        }

        fifo->d_empty = (fifo->empty != 0);
        fifo->empty = 0;
        if (fifo->d_empty && (fifo->d_empty_evt != NULL))
            fifo->d_empty_evt(fifo->priv);

        if (fifo_get_count(fifo) >= fifo->trigger_len) {
            fifo->d_ready = (fifo->ready != 1);
            fifo->ready   = 1;
            if (fifo->d_ready && (fifo->d_ready_evt != NULL))
                fifo->d_ready_evt(fifo->priv);
        }
    }
}

void
fifo_write_evt_tagged(uint8_t tag, uint8_t val, void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->d_full  = fifo->d_empty = 0;
    fifo->d_ready = fifo->d_overrun = 0;

    if (fifo->full) {
        fifo->d_overrun = (fifo->overrun != 1);
        fifo->overrun   = 1;
        if (fifo->d_overrun && (fifo->d_overrun_evt != NULL))
            fifo->d_overrun_evt(fifo->priv);
    } else {
        fifo->buf[fifo->end] = val;
        fifo->tag[fifo->end] = tag;
        fifo->end            = (fifo->end + 1) % fifo->len;

        if (fifo->end == fifo->start) {
            fifo->d_full = (fifo->full != 1);
            fifo->full   = 1;
            if (fifo->d_full && (fifo->d_full_evt != NULL))
                fifo->d_full_evt(fifo->priv);
        }

        fifo->d_empty = (fifo->empty != 0);
        fifo->empty = 0;
        if (fifo->d_empty && (fifo->d_empty_evt != NULL))
            fifo->d_empty_evt(fifo->priv);

        if (fifo_get_count(fifo) >= fifo->trigger_len) {
            fifo->d_ready = (fifo->ready != 1);
            fifo->ready   = 1;
            if (fifo->d_ready && (fifo->d_ready_evt != NULL))
                fifo->d_ready_evt(fifo->priv);
        }
    }
}

uint8_t
fifo_read(void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;
    uint8_t ret  = 0x00;
    int     count;

    if (!fifo->empty) {
        ret         = fifo->buf[fifo->start];
        fifo->start = (fifo->start + 1) % fifo->len;

        fifo->full = 0;

        count = fifo_get_count(fifo);

        if (count < fifo->trigger_len) {
            fifo->ready = 0;

            if (count == 0)
                fifo->empty = 1;
        }
    }

    return ret;
}

uint8_t
fifo_read_tagged(uint8_t *tag, void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;
    uint8_t ret  = 0x00;
    int     count;

    if (!fifo->empty) {
        ret         = fifo->buf[fifo->start];
        *tag        = fifo->tag[fifo->start];

        fifo->start = (fifo->start + 1) % fifo->len;

        fifo->full = 0;

        count = fifo_get_count(fifo);

        if (count < fifo->trigger_len) {
            fifo->ready = 0;

            if (count == 0)
                fifo->empty = 1;
        }
    } else
        *tag        = 0x00;

    return ret;
}

uint8_t
fifo_read_evt(void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;
    uint8_t ret = 0x00;
    int     count;

    fifo->d_full = fifo->d_empty = 0;
    fifo->d_ready = 0;

    if (!fifo->empty) {
        ret         = fifo->buf[fifo->start];
        fifo->start = (fifo->start + 1) % fifo->len;

        fifo->d_full = (fifo->full != 0);
        fifo->full   = 0;
        if (fifo->d_full && (fifo->d_full_evt != NULL))
            fifo->d_full_evt(fifo->priv);

        count = fifo_get_count(fifo);

        if (count < fifo->trigger_len) {
            fifo->d_ready = (fifo->ready != 0);
            fifo->ready   = 0;
            if (fifo->d_ready && (fifo->d_ready_evt != NULL))
                fifo->d_ready_evt(fifo->priv);

            if (count == 0) {
                fifo->d_empty = (fifo->empty != 1);
                fifo->empty   = 1;
                if (fifo->d_empty && (fifo->d_empty_evt != NULL))
                    fifo->d_empty_evt(fifo->priv);
            }
        }
    }

    return ret;
}

uint8_t
fifo_read_evt_tagged(uint8_t *tag, void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;
    uint8_t ret = 0x00;
    int     count;

    fifo->d_full = fifo->d_empty = 0;
    fifo->d_ready = 0;

    if (!fifo->empty) {
        ret         = fifo->buf[fifo->start];
        *tag        = fifo->tag[fifo->start];

        fifo->start = (fifo->start + 1) % fifo->len;

        fifo->d_full = (fifo->full != 0);
        fifo->full   = 0;
        if (fifo->d_full && (fifo->d_full_evt != NULL))
            fifo->d_full_evt(fifo->priv);

        count = fifo_get_count(fifo);

        if (count < fifo->trigger_len) {
            fifo->d_ready = (fifo->ready != 0);
            fifo->ready   = 0;
            if (fifo->d_ready && (fifo->d_ready_evt != NULL))
                fifo->d_ready_evt(fifo->priv);

            if (count == 0) {
                fifo->d_empty = (fifo->empty != 1);
                fifo->empty   = 1;
                if (fifo->d_empty && (fifo->d_empty_evt != NULL))
                    fifo->d_empty_evt(fifo->priv);
            }
        }
    } else
        *tag        = 0x00;

    return ret;
}

void
fifo_clear_overrun(void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->d_overrun = (fifo->overrun != 0);
    fifo->overrun   = 0;
}

int
fifo_get_full(void *priv)
{
    const fifo_t *fifo = (fifo_t *) priv;

    return fifo->full;
}

int
fifo_get_d_full(void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;
    int     ret  = fifo->d_full;

    fifo->d_full = 0;

    return ret;
}

int
fifo_get_empty(void *priv)
{
    const fifo_t *fifo = (fifo_t *) priv;

    return fifo->empty;
}

int
fifo_get_d_empty(void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;
    int     ret  = fifo->d_empty;

    fifo->d_empty = 0;

    return ret;
}

int
fifo_get_overrun(void *priv)
{
    const fifo_t *fifo = (fifo_t *) priv;

    return fifo->overrun;
}

int
fifo_get_d_overrun(void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;
    int     ret  = fifo->d_overrun;

    fifo->d_overrun = 0;

    return ret;
}

int
fifo_get_ready(void *priv)
{
    const fifo_t *fifo = (fifo_t *) priv;

    return fifo->ready;
}

int
fifo_get_d_ready(void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;
    int     ret  = fifo->d_ready;

    fifo->d_ready = 0;

    return ret;
}

int
fifo_get_trigger_len(void *priv)
{
    const fifo_t *fifo = (fifo_t *) priv;

    return fifo->trigger_len;
}

void
fifo_set_trigger_len(void *priv, int trigger_len)
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->trigger_len = trigger_len;
}

void
fifo_set_len(void *priv, int len)
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->len = len;
}

void
fifo_set_d_full_evt(void *priv, void (*d_full_evt)(void *))
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->d_full_evt = d_full_evt;
}

void
fifo_set_d_empty_evt(void *priv, void (*d_empty_evt)(void *))
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->d_empty_evt = d_empty_evt;
}

void
fifo_set_d_overrun_evt(void *priv, void (*d_overrun_evt)(void *))
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->d_overrun_evt = d_overrun_evt;
}

void
fifo_set_d_ready_evt(void *priv, void (*d_ready_evt)(void *))
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->d_ready_evt = d_ready_evt;
}

void
fifo_set_priv(void *priv, void *sub_priv)
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->priv = sub_priv;
}

void
fifo_reset(void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->start = fifo->end = 0;
    fifo->full  = fifo->overrun = 0;
    fifo->empty = 1;
    fifo->ready = 0;
}

void
fifo_reset_evt(void *priv)
{
    fifo_t *fifo = (fifo_t *) priv;

    fifo->start   = fifo->end = 0;
    fifo->full    = fifo->overrun = 0;
    fifo->empty   = 1;
    fifo->ready   = 0;
    fifo->d_full  = fifo->d_overrun = 0;
    fifo->d_empty = fifo->d_ready = 0;

    if (fifo->d_full_evt != NULL)
        fifo->d_full_evt(fifo->priv);

    if (fifo->d_overrun_evt != NULL)
        fifo->d_overrun_evt(fifo->priv);

    if (fifo->d_empty_evt != NULL)
        fifo->d_empty_evt(fifo->priv);

    if (fifo->d_ready_evt != NULL)
        fifo->d_ready_evt(fifo->priv);
}

void
fifo_close(void *priv)
{
    free(priv);
}

void *
fifo_init(int len)
{
    void *fifo = NULL;

    if (len == 64)
        fifo = calloc(1, sizeof(fifo64_t));
    else if (len == 16)
        fifo = calloc(1, sizeof(fifo16_t));
    else {
        fatal("FIFO  : Invalid FIFO length: %i\n", len);
        return NULL;
    }

    if (fifo == NULL)
        fatal("FIFO%i: Failed to allocate memory for the FIFO\n", len);
    else
        ((fifo_t *) fifo)->len = len;

    return fifo;
}

#ifdef FIFO_STANDALONE
enum {
    SERIAL_INT_LSR      = 1,
    SERIAL_INT_RECEIVE  = 2,
    SERIAL_INT_TRANSMIT = 4,
    SERIAL_INT_MSR      = 8,
    SERIAL_INT_TIMEOUT  = 16
};

typedef struct serial_t {
    uint8_t lsr;
    uint8_t int_status;
    uint8_t tsr;
    uint8_t tsr_empty;

    fifo16_t *rcvr_fifo;
    fifo16_t *xmit_fifo;
} serial_t;

static void
serial_receive_timer(fifo16_t *f16, uint8_t val)
{
    fifo_write_evt(val, f16);

    printf("Write %02X to   FIFO [F: %i, E: %i, O: %i, R: %i]\n", val,
           fifo_get_full(f16), fifo_get_empty(f16),
           fifo_get_overrun(f16), fifo_get_ready(f16));

#if 0
    if (fifo_get_d_overrun(f16))
        dev->lsr = (dev->lsr & 0xfd) | (fifo_get_overrun(f16) << 1);
#endif

    if (fifo_get_d_overrun(f16))  printf("    FIFO overrun state changed: %i -> %i\n",
                                        !fifo_get_overrun(f16), fifo_get_overrun(f16));

#if 0
    if (fifo_get_d_empty(f16)) {
        dev->lsr = (dev->lsr & 0xfe) | !fifo_get_empty(f16);
        timer_on_auto(&dev->timeout_timer, 4.0 * dev->bits * dev->transmit_period);
    }
#endif

    if (fifo_get_d_empty(f16))
        printf("    FIFO empty   state changed: %i -> %i\n",
               !fifo_get_empty(f16), fifo_get_empty(f16));

#if 0
    if (fifo_get_d_ready(f16)) {
        dev->int_status = (dev->int_status & ~SERIAL_INT_RECEIVE) |
                          (fifo_get_ready(f16) ? SERIAL_INT_RECEIVE : 0);
        serial_update_ints();
    }
#endif
    if (fifo_get_d_ready(f16))  printf("    FIFO ready   state changed: %i -> %i\n",
                                      !fifo_get_ready(f16), fifo_get_ready(f16));
}

static uint8_t
serial_read(fifo16_t *f16)
{
    uint8_t ret;

    ret = fifo_read_evt(f16);

    printf("Read  %02X from FIFO [F: %i, E: %i, O: %i, R: %i]\n", ret,
           fifo_get_full(f16), fifo_get_empty(f16),
           fifo_get_overrun(f16), fifo_get_ready(f16));

#if 0
    if (fifo_get_d_ready(f16)) {
        dev->int_status = (dev->int_status & ~SERIAL_INT_RECEIVE) |
                          (fifo_get_ready(f16) ? SERIAL_INT_RECEIVE : 0);
        serial_update_ints();
    }
#endif

    if (fifo_get_d_ready(f16))
        printf("    FIFO ready   state changed: %i -> %i\n",
              !fifo_get_ready(f16), fifo_get_ready(f16));

#if 0
        if (fifo_get_d_empty(f16)) {
            dev->lsr = (dev->lsr & 0xfe) | !fifo_get_empty(f16);
            timer_on_auto(&dev->timeout_timer, 4.0 * dev->bits * dev->transmit_period);
        }
#endif
    if (fifo_get_d_empty(f16))
        printf("    FIFO empty   state changed: %i -> %i\n",
              !fifo_get_empty(f16), fifo_get_empty(f16));

    return ret;
}

static void
serial_xmit_d_empty_evt(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    dev->lsr = (dev->lsr & 0x9f) | (fifo_get_empty(dev->xmit_fifo) << 5) |
                                   ((dev->tsr_empty && fifo_get_empty(dev->xmit_fifo)) << 6);
    dev->int_status = (dev->int_status & ~SERIAL_INT_TRANSMIT) |
                      (fifo_get_empty(dev->xmit_fifo) ? SERIAL_INT_TRANSMIT : 0);
    // serial_update_ints();

    printf("NS16550:   serial_xmit_d_empty_evt(%08X): dev->lsr        = %02X\n", priv, dev->lsr);
    printf("NS16550:   serial_xmit_d_empty_evt(%08X): dev->int_status = %02X\n", priv, dev->int_status);
}

static void
serial_rcvr_d_empty_evt(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    dev->lsr = (dev->lsr & 0xfe) | !fifo_get_empty(dev->rcvr_fifo);
    // timer_on_auto(&dev->timeout_timer, 4.0 * dev->bits * dev->transmit_period);

    printf("NS16550:   serial_rcvr_d_empty_evt(%08X): dev->lsr        = %02X\n", priv, dev->lsr);
}

static void
serial_rcvr_d_overrun_evt(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    dev->lsr = (dev->lsr & 0xfd) | (fifo_get_overrun(dev->rcvr_fifo) << 1);

    printf("NS16550: serial_rcvr_d_overrun_evt(%08X): dev->lsr        = %02X\n", priv, dev->lsr);
}

static void
serial_rcvr_d_ready_evt(void *priv)
{
    serial_t *dev = (serial_t *) priv;

    dev->int_status = (dev->int_status & ~SERIAL_INT_RECEIVE) |
                      (fifo_get_ready(dev->rcvr_fifo) ? SERIAL_INT_RECEIVE : 0);
    // serial_update_ints();

    printf("NS16550:   serial_rcvr_d_ready_evt(%08X): dev->int_status = %02X\n", priv, dev->int_status);
}

int
main(int argc, char *argv[])
{
    uint8_t val;
    uint8_t ret;

    printf("Initializing serial...\n");
    serial_t *dev = (serial_t *) calloc(1, sizeof(serial_t));
    dev->tsr_empty = 1;

    printf("Initializing dev->xmit_fifo...\n");
    dev->xmit_fifo = fifo16_init();
    fifo_set_trigger_len(dev->xmit_fifo, 255);

    fifo_set_priv(dev->xmit_fifo, dev);
    fifo_set_d_empty_evt(dev->xmit_fifo, serial_xmit_d_empty_evt);

    printf("\nResetting dev->xmit_fifo...\n");
    fifo_reset_evt(dev->xmit_fifo);

    printf("\nInitializing dev->rcvr_fifo...\n");
    dev->rcvr_fifo = fifo16_init();
    fifo_set_trigger_len(dev->rcvr_fifo, 4);

    fifo_set_priv(dev->rcvr_fifo, dev);
    fifo_set_d_empty_evt(dev->rcvr_fifo, serial_rcvr_d_empty_evt);
    fifo_set_d_overrun_evt(dev->rcvr_fifo, serial_rcvr_d_overrun_evt);
    fifo_set_d_ready_evt(dev->rcvr_fifo, serial_rcvr_d_ready_evt);

    printf("\nResetting dev->rcvr_fifo...\n");
    fifo_reset_evt(dev->rcvr_fifo);

    printf("\nSending/receiving data...\n");
    serial_receive_timer(dev->rcvr_fifo, '8');
    serial_receive_timer(dev->rcvr_fifo, '6');
    ret = serial_read(dev->rcvr_fifo);
    serial_receive_timer(dev->rcvr_fifo, 'B');
    ret = serial_read(dev->rcvr_fifo);
    serial_receive_timer(dev->rcvr_fifo, 'o');
    ret = serial_read(dev->rcvr_fifo);
    serial_receive_timer(dev->rcvr_fifo, 'x');
    ret = serial_read(dev->rcvr_fifo);
    ret = serial_read(dev->rcvr_fifo);

    fifo_close(dev->rcvr_fifo);
    fifo_close(dev->xmit_fifo);

    free(dev);

    return 0;
}
#endif
