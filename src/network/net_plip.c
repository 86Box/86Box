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
 *
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
#include <86box/lpt.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/network.h>
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
    uint8_t mac[6];

    void      *lpt;
    pc_timer_t rx_timer;
    pc_timer_t timeout_timer;
    uint8_t    status;
    uint8_t    ctrl;

    uint8_t   state;
    uint8_t   ack;
    uint8_t   tx_checksum;
    uint8_t   tx_checksum_calc;
    uint8_t  *tx_pkt;
    uint16_t  tx_len;
    uint16_t  tx_ptr;

    uint8_t   *rx_pkt;
    uint8_t    rx_checksum;
    uint8_t    rx_return_state;
    uint16_t   rx_len;
    uint16_t   rx_ptr;
    netcard_t *card;
} plip_t;

static void plip_receive_packet(plip_t *dev);

plip_t *instance;

#ifdef ENABLE_PLIP_LOG
int plip_do_log = ENABLE_PLIP_LOG;

static void
plip_log(uint8_t lvl, const char *fmt, ...)
{
    va_list ap;

    if (plip_do_log >= lvl) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define plip_log(lvl, fmt, ...)
#endif

static void
timeout_timer(void *priv)
{
    plip_t *dev = (plip_t *) priv;

    plip_log(1, "PLIP: timeout at state %d status %02X\n", dev->state, dev->status);

    /* Throw everything out the window. */
    dev->state  = PLIP_START;
    dev->status = 0x80;

    if (dev->tx_pkt) {
        free(dev->tx_pkt);
        dev->tx_pkt = NULL;
    }
    if (dev->rx_pkt) {
        free(dev->rx_pkt);
        dev->rx_pkt = NULL;
    }

    timer_disable(&dev->timeout_timer);
}

static void
plip_write_data(uint8_t val, void *priv)
{
    plip_t *dev = (plip_t *) priv;

    plip_log(3, "PLIP: write_data(%02X)\n", val);

    switch (dev->state) {
        case PLIP_START:
            if (val == 0x08) { /* D3/ACK wakes us up */
                plip_log(2, "PLIP: ACK wakeup\n");
                dev->state  = PLIP_TX_LEN_LSB_LOW;
                dev->status = 0x08;
                break;
            }
            return;

        case PLIP_TX_LEN_LSB_LOW:
            if (!(val & 0x10))
                return; /* D4/BUSY not high yet */
            dev->tx_len = val & 0xf;
            plip_log(2, "PLIP: tx_len = %04X (1/4)\n", dev->tx_len);
            dev->state = PLIP_TX_LEN_LSB_HIGH;
            dev->status &= ~0x88;
            break;

        case PLIP_TX_LEN_LSB_HIGH:
            if (val & 0x10)
                return; /* D4/BUSY not low yet */
            dev->tx_len |= (val & 0xf) << 4;
            plip_log(2, "PLIP: tx_len = %04X (2/4)\n", dev->tx_len);
            dev->state = PLIP_TX_LEN_MSB_LOW;
            dev->status |= 0x80;
            break;

        case PLIP_TX_LEN_MSB_LOW:
            if (!(val & 0x10))
                return; /* D4/BUSY not high yet */
            dev->tx_len |= (val & 0xf) << 8;
            plip_log(2, "PLIP: tx_len = %04X (3/4)\n", dev->tx_len);
            dev->state = PLIP_TX_LEN_MSB_HIGH;
            dev->status &= ~0x80;
            break;

        case PLIP_TX_LEN_MSB_HIGH:
            if (val & 0x10)
                return; /* D4/BUSY not low yet */
            dev->tx_len |= (val & 0xf) << 12;
            plip_log(2, "PLIP: tx_len = %04X (4/4)\n", dev->tx_len);

            /* We have the length, allocate a packet. */
            if (!(dev->tx_pkt = malloc(dev->tx_len))) /* unlikely */
                fatal("PLIP: unable to allocate tx_pkt\n");
            dev->tx_ptr           = 0;
            dev->tx_checksum_calc = 0;

            dev->state = PLIP_TX_DATA_LOW;
            dev->status |= 0x80;
            break;

        case PLIP_TX_DATA_LOW:
            if (!(val & 0x10))
                return; /* D4/BUSY not high yet */
            dev->tx_pkt[dev->tx_ptr] = val & 0x0f;
            plip_log(2, "PLIP: tx_pkt[%d] = %02X (1/2)\n", dev->tx_ptr, dev->tx_pkt[dev->tx_ptr]);
            dev->state = PLIP_TX_DATA_HIGH;
            dev->status &= ~0x80;
            break;

        case PLIP_TX_DATA_HIGH:
            if (val & 0x10)
                return; /* D4/BUSY not low yet */
            dev->tx_pkt[dev->tx_ptr] |= (val & 0x0f) << 4;
            plip_log(2, "PLIP: tx_pkt[%d] = %02X (2/2)\n", dev->tx_ptr, dev->tx_pkt[dev->tx_ptr]);
            dev->tx_checksum_calc += dev->tx_pkt[dev->tx_ptr++];

            /* Are we done yet? */
            if (dev->tx_ptr < dev->tx_len) /* no, receive another byte */
                dev->state = PLIP_TX_DATA_LOW;
            else /* yes, move on to checksum */
                dev->state = PLIP_TX_CHECKSUM_LOW;
            dev->status |= 0x80;
            break;

        case PLIP_TX_CHECKSUM_LOW:
            if (!(val & 0x10))
                return; /* D4/BUSY not high yet */
            dev->tx_checksum = val & 0x0f;
            plip_log(2, "PLIP: tx_checksum = %02X (1/2)\n", dev->tx_checksum);
            dev->state = PLIP_TX_CHECKSUM_HIGH;
            dev->status &= ~0x80;
            break;

        case PLIP_TX_CHECKSUM_HIGH:
            if (val & 0x10)
                return; /* D4/BUSY not low yet */
            dev->tx_checksum |= (val & 0x0f) << 4;
            plip_log(2, "PLIP: tx_checksum = %02X (2/2)\n", dev->tx_checksum);

            /* Verify checksum. */
            if (dev->tx_checksum_calc == dev->tx_checksum) {
                /* Make sure we know the other end's MAC address. */
                memcpy(dev->mac, dev->tx_pkt + 6, 6);

                /* Transmit packet. */
                plip_log(2, "PLIP: transmitting %d-byte packet\n", dev->tx_len);
                network_tx(dev->card, dev->tx_pkt, dev->tx_len);
            } else {
                plip_log(1, "PLIP: checksum error: expected %02X, got %02X\n", dev->tx_checksum_calc, dev->tx_checksum);
            }

            /* We're done with this packet. */
            free(dev->tx_pkt);
            dev->tx_pkt = NULL;
            dev->tx_len = 0;

            dev->state = PLIP_END;
            dev->status |= 0x80;
            break;

        case PLIP_RX_LEN_LSB_LOW:
            if (!(val & 0x01))
                return; /* D3/ACK not high yet */
            plip_log(2, "PLIP: rx_len = %04X (1/4)\n", dev->rx_len);
            dev->status = (dev->rx_len & 0x0f) << 3;
            dev->state  = PLIP_RX_LEN_LSB_HIGH;
            break;

        case PLIP_RX_LEN_LSB_HIGH:
            if (!(val & 0x10))
                return; /* D4/BUSY not high yet */
            plip_log(2, "PLIP: rx_len = %04X (2/4)\n", dev->rx_len);
            dev->status = ((dev->rx_len >> 4) & 0x0f) << 3;
            dev->status |= 0x80;
            dev->state = PLIP_RX_LEN_MSB_LOW;
            break;

        case PLIP_RX_LEN_MSB_LOW:
            if (val & 0x10)
                return; /* D4/BUSY not low yet */
            plip_log(2, "PLIP: rx_len = %04X (3/4)\n", dev->rx_len);
            dev->status = ((dev->rx_len >> 8) & 0x0f) << 3;
            dev->state  = PLIP_RX_LEN_MSB_HIGH;
            break;

        case PLIP_RX_LEN_MSB_HIGH:
            if (!(val & 0x10))
                return; /* D4/BUSY not high yet */
            plip_log(2, "PLIP: rx_len = %04X (4/4)\n", dev->rx_len);
            dev->status = ((dev->rx_len >> 12) & 0x0f) << 3;
            dev->status |= 0x80;

            dev->rx_ptr      = 0;
            dev->rx_checksum = 0;
            dev->state       = PLIP_RX_DATA_LOW;
            break;

        case PLIP_RX_DATA_LOW:
            if (val & 0x10)
                return; /* D4/BUSY not low yet */
            plip_log(2, "PLIP: rx_pkt[%d] = %02X (1/2)\n", dev->rx_ptr, dev->rx_pkt[dev->rx_ptr]);
            dev->status = (dev->rx_pkt[dev->rx_ptr] & 0x0f) << 3;
            dev->state  = PLIP_RX_DATA_HIGH;
            break;

        case PLIP_RX_DATA_HIGH:
            if (!(val & 0x10))
                return; /* D4/BUSY not high yet */
            plip_log(2, "PLIP: rx_pkt[%d] = %02X (2/2)\n", dev->rx_ptr, dev->rx_pkt[dev->rx_ptr]);
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
                return; /* D4/BUSY not low yet */
            plip_log(2, "PLIP: rx_checksum = %02X (1/2)\n", dev->rx_checksum);
            dev->status = (dev->rx_checksum & 0x0f) << 3;
            dev->state  = PLIP_RX_CHECKSUM_HIGH;
            break;

        case PLIP_RX_CHECKSUM_HIGH:
            if (!(val & 0x10))
                return; /* D4/BUSY not high yet */
            plip_log(2, "PLIP: rx_checksum = %02X (2/2)\n", dev->rx_checksum);
            dev->status = ((dev->rx_checksum >> 4) & 0x0f) << 3;
            dev->status |= 0x80;

            /* We're done with this packet. */
            free(dev->rx_pkt);
            dev->rx_pkt = NULL;
            dev->rx_len = 0;

            dev->state = PLIP_END;
            break;

        case PLIP_END:
            if (val == 0x00) { /* written after TX or RX is done */
                plip_log(2, "PLIP: end\n");
                dev->status = 0x80;
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

    plip_log(3, "PLIP: write_ctrl(%02X)\n", val);

    dev->ctrl = val;

    if (val & 0x10) /* for Linux */
        timer_set_delay_u64(&dev->rx_timer, ISACONST);
}

static uint8_t
plip_read_status(void *priv)
{
    const plip_t *dev = (plip_t *) priv;

    plip_log(3, "PLIP: read_status() = %02X\n", dev->status);

    return dev->status;
}

static void
plip_receive_packet(plip_t *dev)
{
    /* At least the Linux driver supports being interrupted
       in the PLIP_TX_LEN_LSB_LOW state, but let's be safe. */
    if (dev->state > PLIP_START) {
        plip_log(3, "PLIP: cannot receive, operation already in progress\n");
        return;
    }

    if (!dev->rx_pkt || !dev->rx_len) { /* unpause RX queue if there's no packet to receive */
        return;
    }

    if (!(dev->ctrl & 0x10)) { /* checking this is essential to avoid collisions */
        plip_log(3, "PLIP: cannot receive, interrupts are off\n");
        return;
    }

    plip_log(2, "PLIP: receiving %d-byte packet\n", dev->rx_len);

    /* Set up to receive a packet. */
    dev->status = 0xc7; /* DOS expects exactly 0xc7, while Linux masks the 7 off */
    dev->state  = PLIP_RX_LEN_LSB_LOW;

    /* Engage timeout timer. */
    timer_set_delay_u64(&dev->timeout_timer, 1000000 * TIMER_USEC);

    /* Wake the other end up. */
    lpt_irq(dev->lpt, 1);
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

    plip_log(2, "PLIP: incoming %d-byte packet\n", io_len);

    if (dev->rx_pkt) { /* shouldn't really happen with the RX queue paused */
        plip_log(3, "PLIP: already have a packet to receive");
        return 0;
    }

    if (!(dev->rx_pkt = malloc(io_len))) /* unlikely */
        fatal("PLIP: unable to allocate rx_pkt\n");

    /* Copy this packet to our buffer. */
    dev->rx_len = io_len;
    memcpy(dev->rx_pkt, buf, dev->rx_len);

    /* Dispatch this packet immediately if we're doing nothing. */
    plip_receive_packet(dev);

    return 1;
}

static void *
plip_lpt_init(void *lpt)
{
    plip_t *dev = (plip_t *) calloc(1, sizeof(plip_t));

    plip_log(1, "PLIP: lpt_init()\n");

    dev->lpt = lpt;
    memset(dev->mac, 0xfc, 6); /* static MAC used by Linux; just a placeholder */

    dev->status = 0x80;

    timer_add(&dev->rx_timer, rx_timer, dev, 0);
    timer_add(&dev->timeout_timer, timeout_timer, dev, 0);

    instance = dev;

    return dev;
}

static void *
plip_net_init(UNUSED(const device_t *info))
{
    plip_log(1, "PLIP: net_init()");

    if (!instance) {
        plip_log(1, " (not attached to LPT)\n");
        return NULL;
    }

    plip_log(1, " (attached to LPT)\n");
    instance->card = network_attach(instance, instance->mac, plip_rx, NULL);

    return instance;
}

static void
plip_close(void *priv)
{
    if (instance->card) {
        netcard_close(instance->card);
    }
    free(priv);
}

const lpt_device_t lpt_plip_device = {
    .name          = "Parallel Line Internet Protocol",
    .internal_name = "plip",
    .init          = plip_lpt_init,
    .close         = plip_close,
    .write_data    = plip_write_data,
    .write_ctrl    = plip_write_ctrl,
    .read_data     = NULL,
    .read_status   = plip_read_status,
    .read_ctrl     = NULL
};

const device_t plip_device = {
    .name          = "Parallel Line Internet Protocol",
    .internal_name = "plip",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = plip_net_init,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
