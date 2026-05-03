/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of a PLIP parallel port network device.
 *
 *          Tested against the Linux plip.c driver and the DOS plip.com
 *          packet driver. PLIP is not particularly fast, as it's a 4-bit
 *          half-duplex protocol operating over SPP.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *          Copyright 2020 RichardG.
 */
#include <inttypes.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/log.h>
#include <86box/plat_unused.h>

enum {
    PLIP_START          = 0x00,
    PLIP_TX_LEN_LSB_LOW = 0x10,
    PLIP_TX_LEN_LSB_HIGH,
    PLIP_TX_LEN_MSB_LOW,
    PLIP_TX_LEN_MSB_HIGH,
    PLIP_TX_DATA_LOW,
    PLIP_TX_DATA_HIGH,
    PLIP_TX_CHECKSUM_LOW,
    PLIP_TX_CHECKSUM_HIGH,
    PLIP_RX_LEN_LSB_LOW = 0x20,
    PLIP_RX_LEN_LSB_HIGH,
    PLIP_RX_LEN_MSB_LOW,
    PLIP_RX_LEN_MSB_HIGH,
    PLIP_RX_DATA_LOW,
    PLIP_RX_DATA_HIGH,
    PLIP_RX_CHECKSUM_LOW,
    PLIP_RX_CHECKSUM_HIGH,
    PLIP_END = 0x40
};

typedef struct plip_t {
    void      *log;
    void      *lpt;
    netcard_t *card;
    uint8_t    mac[6];
    uint8_t    status;
    uint8_t    state;

    uint8_t  rx_checksum;
    uint8_t  rx_return_state;
    uint16_t rx_len;
    uint16_t rx_ptr;
    uint8_t  tx_checksum;
    uint8_t  tx_checksum_calc;
    uint16_t tx_len;
    uint16_t tx_ptr;

    pc_timer_t rx_timer;
    pc_timer_t timeout_timer;

    uint8_t rx_pkt[NET_MAX_FRAME];
    uint8_t tx_pkt[NET_MAX_FRAME];
} plip_t;

#ifdef ENABLE_PLIP_LOG
int plip_do_log = ENABLE_PLIP_LOG;

static void
plip_log(void *priv, int lvl, const char *fmt, ...)
{
    va_list ap;

    if (plip_do_log >= lvl) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define plip_log(priv, lvl, fmt, ...)
#endif

static void
timeout_timer(void *priv)
{
    plip_t *dev = (plip_t *) priv;

    plip_log(dev->log, 1, "Timeout at state %02X status %02X\n", dev->state, dev->status);

    /* Throw everything out the window. */
    dev->state  = PLIP_START;
    dev->status = 0x80; /* nBusy */

    timer_disable(&dev->timeout_timer);
}

static void
plip_write_data(uint8_t val, void *priv)
{
    plip_t *dev = (plip_t *) priv;

    plip_log(dev->log, 3, "write_data(%02X)\n", val);

    switch (dev->state) {
        case PLIP_START:
            if (val & 0x08) { /* D3==nAck wakes us up */
                plip_log(dev->log, 2, "nAck wakeup\n");
start_tx:
                dev->state  = PLIP_TX_LEN_LSB_LOW;
                dev->status = 0x08; /* nFault */
                break;
            }
            return;

        case PLIP_TX_LEN_LSB_LOW:
            if (!(val & 0x10))
                return; /* D4==!nBusy not asserted yet */
            dev->tx_len = val & 0xf;
            dev->state = PLIP_TX_LEN_LSB_HIGH;
            dev->status &= ~0x88; /* clear nFault|nBusy */
            break;

        case PLIP_TX_LEN_LSB_HIGH:
            if (val & 0x10)
                return; /* !D4==nBusy not asserted yet */
            dev->tx_len |= (val & 0xf) << 4;
            dev->state = PLIP_TX_LEN_MSB_LOW;
            dev->status |= 0x80; /* toggling nBusy from now on */
            break;

        case PLIP_TX_LEN_MSB_LOW:
            if (!(val & 0x10))
                return; /* D4==!nBusy not asserted yet */
            dev->tx_len |= (val & 0xf) << 8;
            dev->state = PLIP_TX_LEN_MSB_HIGH;
            dev->status &= ~0x80;
            break;

        case PLIP_TX_LEN_MSB_HIGH:
            if (val & 0x10)
                return; /* !D4==nBusy not asserted yet */
            dev->tx_len |= (val & 0xf) << 12;
            plip_log(dev->log, 2, "tx_len = %04X\n", dev->tx_len);

            /* Begin transmitting data. */
            dev->tx_ptr           = 0;
            dev->tx_checksum_calc = 0;

            dev->state = PLIP_TX_DATA_LOW;
            dev->status |= 0x80;
            break;

        case PLIP_TX_DATA_LOW:
            if (!(val & 0x10))
                return; /* D4==!nBusy not asserted yet */
            if (LIKELY(dev->tx_ptr) < sizeof(dev->tx_pkt))
                dev->tx_pkt[dev->tx_ptr] = val & 0x0f;
            dev->state = PLIP_TX_DATA_HIGH;
            dev->status &= ~0x80;
            break;

        case PLIP_TX_DATA_HIGH:
            if (val & 0x10)
                return; /* !D4==nBusy not asserted yet */
            if (LIKELY(dev->tx_ptr) < sizeof(dev->tx_pkt)) {
                dev->tx_pkt[dev->tx_ptr] |= val << 4;
                dev->tx_checksum_calc    += dev->tx_pkt[dev->tx_ptr];
                plip_log(dev->log, 2, "tx_pkt[%d] = %02X\n", dev->tx_ptr, dev->tx_pkt[dev->tx_ptr]);
            } else {
                plip_log(dev->log, 2, "tx_pkt[%d] = overflow\n", dev->tx_ptr);
            }
            dev->tx_ptr++;

            /* Are we done yet? */
            if (dev->tx_ptr < dev->tx_len) /* no, receive another byte */
                dev->state = PLIP_TX_DATA_LOW;
            else /* yes, move on to checksum */
                dev->state = PLIP_TX_CHECKSUM_LOW;
            dev->status |= 0x80;
            break;

        case PLIP_TX_CHECKSUM_LOW:
            if (!(val & 0x10))
                return; /* D4==!nBusy not asserted yet */
            dev->tx_checksum = val & 0x0f;
            dev->state = PLIP_TX_CHECKSUM_HIGH;
            dev->status &= ~0x80;
            break;

        case PLIP_TX_CHECKSUM_HIGH:
            if (val & 0x10)
                return; /* !D4==nBusy not asserted yet */
            dev->tx_checksum |= (val & 0x0f) << 4;
            plip_log(dev->log, 2, "tx_checksum = %02X\n", dev->tx_checksum);

            /* Verify checksum. */
            if (dev->tx_checksum_calc == dev->tx_checksum) {
                /* Make sure we know the other end's MAC address. */
                memcpy(dev->mac, dev->tx_pkt + 6, 6);

                /* Transmit packet. */
                plip_log(dev->log, 1, "Transmitting %d-byte packet\n", dev->tx_len);
                network_tx(dev->card, dev->tx_pkt, dev->tx_len);
            } else {
                plip_log(dev->log, 1, "Transmit checksum error, expected %02X got %02X\n", dev->tx_checksum_calc, dev->tx_checksum);
            }

            /* We're done with this packet. */
            dev->tx_len = 0;

            dev->state = PLIP_END;
            dev->status |= 0x80;
            break;

        case PLIP_RX_LEN_LSB_LOW:
            if (!(val & 0x01)) { /* D0==nFault not asserted yet */
                if (val & 0x08) { /* collision if D3==nAck */
                    plip_log(dev->log, 2, "Receive collision\n");
                    goto start_tx;
                }
                return;
            }
            dev->status = (dev->rx_len & 0x0f) << 3;
            dev->state  = PLIP_RX_LEN_LSB_HIGH;
            break;

        case PLIP_RX_LEN_LSB_HIGH:
            if (!(val & 0x10))
                return; /* D4==!nBusy not asserted yet */
            dev->status = ((dev->rx_len >> 4) & 0x0f) << 3;
            dev->status |= 0x80;
            dev->state = PLIP_RX_LEN_MSB_LOW;
            break;

        case PLIP_RX_LEN_MSB_LOW:
            if (val & 0x10)
                return; /* !D4==nBusy not asserted yet */
            dev->status = ((dev->rx_len >> 8) & 0x0f) << 3;
            dev->state  = PLIP_RX_LEN_MSB_HIGH;
            break;

        case PLIP_RX_LEN_MSB_HIGH:
            if (!(val & 0x10))
                return; /* D4==!nBusy not asserted yet */
            plip_log(dev->log, 2, "rx_len = %04X\n", dev->rx_len);
            dev->status = ((dev->rx_len >> 12) & 0x0f) << 3;
            dev->status |= 0x80;

            /* Begin receiving data. */
            dev->rx_ptr      = 0;
            dev->rx_checksum = 0;
            dev->state       = PLIP_RX_DATA_LOW;
            break;

        case PLIP_RX_DATA_LOW:
            if (val & 0x10)
                return; /* !D4==nBusy not asserted yet */
            dev->status = (dev->rx_pkt[dev->rx_ptr] & 0x0f) << 3;
            dev->state  = PLIP_RX_DATA_HIGH;
            break;

        case PLIP_RX_DATA_HIGH:
            if (!(val & 0x10))
                return; /* D4==!nBusy not asserted yet */
            plip_log(dev->log, 2, "rx_pkt[%d] = %02X\n", dev->rx_ptr, dev->rx_pkt[dev->rx_ptr]);
            dev->status = ((dev->rx_pkt[dev->rx_ptr] >> 4) & 0x0f) << 3;
            dev->status |= 0x80;
            dev->rx_checksum += dev->rx_pkt[dev->rx_ptr++];

            /* Are we done yet? */
            if (dev->rx_ptr < dev->rx_len) /* no, send another byte */
                dev->state = PLIP_RX_DATA_LOW;
            else /* yes, move on to checksum */
                dev->state = PLIP_RX_CHECKSUM_LOW;
            break;

        case PLIP_RX_CHECKSUM_LOW:
            if (val & 0x10)
                return; /* !D4==nBusy not asserted yet */
            dev->status = (dev->rx_checksum & 0x0f) << 3;
            dev->state  = PLIP_RX_CHECKSUM_HIGH;
            break;

        case PLIP_RX_CHECKSUM_HIGH:
            if (!(val & 0x10))
                return; /* D4==!nBusy not asserted yet */
            plip_log(dev->log, 2, "rx_checksum = %02X\n", dev->rx_checksum);
            dev->status = ((dev->rx_checksum >> 4) & 0x0f) << 3;
            dev->status |= 0x80;

            /* We're done with this packet. */
            dev->rx_len = 0;

            dev->state = PLIP_END;
            break;

        case PLIP_END:
            if (val == 0x00) { /* written after TX or RX is done */
                plip_log(dev->log, 2, "End of packet\n");
                dev->status = 0x80; /* nBusy */
                dev->state  = PLIP_START;

                timer_set_delay_u64(&dev->rx_timer, ISACONST); /* for DOS */
            }

            /* Disengage timeout timer. */
            timer_disable(&dev->timeout_timer);
            return;

        default:
            break;
    }

    /* Engage timeout timer unless otherwise specified. */
    timer_set_delay_u64(&dev->timeout_timer, 1000000 * TIMER_USEC);
}

static void
plip_write_ctrl(uint8_t val, void *priv)
{
    plip_t *dev = (plip_t *) priv;

    plip_log(dev->log, 3, "write_ctrl(%02X)\n", val);

    if (val & 0x10) /* ackIntEn set by Linux after operation, by DOS on driver init */
        timer_set_delay_u64(&dev->rx_timer, ISACONST);
}

static uint8_t
plip_read_status(void *priv)
{
    const plip_t *dev = (plip_t *) priv;

    plip_log(dev->log, 3, "read_status() = %02X\n", dev->status);

    return dev->status;
}

static void
plip_receive_packet(plip_t *dev)
{
    /* Stop if we have nothing to receive. */
    if (!dev->rx_len)
        return;

    /* Stop if we're already transmitting or receiving. */
    if (dev->state > PLIP_START) {
        plip_log(dev->log, 2, "Cannot receive %d-byte packet, operation already in progress\n", dev->rx_len);

        /* Re-dispatch IRQ if ackIntEn was toggled on while we're waiting for a response to PLIP_RX_LEN_LSB_LOW. (Linux) */
        if ((dev->status & 0x40) && dev->lpt)
            lpt_irq(dev->lpt, 1);
        return;
    }

    plip_log(dev->log, 1, "Receiving %d-byte packet\n", dev->rx_len);

    /* Set up to receive a packet. */
    dev->status = 0xc7; /* Linux expects nBusy|nAck|!PError|!Select|!nFault, DOS also |reserved[2:0] */
    dev->state  = PLIP_RX_LEN_LSB_LOW;
    if (dev->lpt) /* wake guest up */
        lpt_irq(dev->lpt, 1);

    /* Engage timeout timer. */
    timer_set_delay_u64(&dev->timeout_timer, 1000000 * TIMER_USEC);
}

/* This timer defers a call to plip_receive_packet to
   the next ISA clock, in order to avoid IRQ weirdness. */
static void
rx_timer(void *priv)
{
    plip_t *dev = (plip_t *) priv;

    plip_receive_packet(dev);

    timer_disable(&dev->rx_timer);
}

static int
plip_rx(void *priv, uint8_t *buf, int io_len)
{
    plip_t *dev = (plip_t *) priv;

    if (dev->rx_len) {
        plip_log(dev->log, 2, "Incoming %d-byte packet declined\n", io_len);
        return 0;
    }

    plip_log(dev->log, 2, "Incoming %d-byte packet accepted\n", io_len);

    /* Copy this packet to our buffer. */
    dev->rx_len = MIN(io_len, sizeof(dev->rx_pkt));
    memcpy(dev->rx_pkt, buf, dev->rx_len);

    /* Start receiving this packet immediately if we're doing nothing. */
    plip_receive_packet(dev);

    return 1;
}

static void *
plip_init(const device_t *info)
{
    plip_t *dev = (plip_t *) calloc(1, sizeof(plip_t));

    dev->log = log_open("PLIP");
    plip_log(dev->log, 1, "init()\n");

    dev->lpt  = lpt_attach_ex(device_get_config_int("port"), plip_write_data, plip_write_ctrl, NULL, plip_read_status, NULL, NULL, NULL, dev);
    dev->card = network_attach(dev, dev->mac, plip_rx, NULL);

    memset(dev->mac, 0xfc, 6); /* static MAC used by Linux; just a placeholder */

    dev->status = 0x80; /* nBusy */

    timer_add(&dev->rx_timer, rx_timer, dev, 0);
    timer_add(&dev->timeout_timer, timeout_timer, dev, 0);

    return dev;
}

static void
plip_close(void *priv)
{
    plip_t *dev = (plip_t *) priv;

    plip_log(dev->log, 1, "close()\n");

    if (dev->card)
        netcard_close(dev->card);

    free(dev);
}

static const device_config_t plip_config[] = {
    {
        .name           = "port",
        .description    = "Parallel Port",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "LPT1", .value = 0 },
            { .description = "LPT2", .value = 1 },
            { .description = "LPT3", .value = 2 },
            { .description = "LPT4", .value = 3 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t plip_device = {
    .name          = "Parallel Line Internet Protocol",
    .internal_name = "plip",
    .flags         = 0,
    .local         = 0,
    .init          = plip_init,
    .close         = plip_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = plip_config
};
