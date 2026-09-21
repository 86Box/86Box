/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Xircom Pocket Ethernet Adapter III (PE3) LPT NIC emulation.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2026 RichardG.
 */
#include <inttypes.h>
#include <memory.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/nmc93cxx.h>
#include <86box/log.h>
#include <86box/plat_unused.h>
#include <86box/random.h>
#include <86box/bswap.h>

/* Control port bits. */
#define PE3_CTRL_STROBE   0x01
#define PE3_CTRL_AUTOFEED 0x02
#define PE3_CTRL_INIT     0x04
#define PE3_CTRL_SELECT   0x08
#define PE3_CTRL_DIR      0x20

/* Command byte operations (top 3 bits). */
#define PE3_OP_MISC      0 /* [00:1F] block write terminator and config data */
#define PE3_OP_WRITE     2 /* [40:5F] write register */
#define PE3_OP_READ      3 /* [60:7F] read register */
#define PE3_OP_WRITE_ALT 4 /* [80:9F] write register (used while probing) */
#define PE3_OP_BLOCK_WR  6 /* [C0:DF] write register in block mode */
#define PE3_OP_BLOCK_RD  7 /* [E0:FF] read register in byte (bidirectional) mode */

/* Adapter registers. */
#define PE3_REG_ISR0     0x00 /* transmit status (write-clear) */
#define PE3_REG_ISR1     0x01 /* receive status (write-clear) */
#define PE3_REG_IMR0     0x02
#define PE3_REG_IMR1     0x03
#define PE3_REG_MODE     0x04 /* transmit status (read) or mode (write) */
#define PE3_REG_RXMODE   0x05 /* receive status (read) or receive filter (write) */
#define PE3_REG_CONTROL  0x06
#define PE3_REG_BANK     0x07
#define PE3_REG_BANKED   0x08 /* banked registers [08:0F] */
#define PE3_BANKED_DATA  0x00 /* 08 */
#define PE3_BANKED_CMD   0x02 /* 0A in bank */
#define PE3_BANKED_ABORT 0x06 /* 0E in bank */
#define PE3_REG_INTEN    0x10
#define PE3_REG_EEPROM   0x11
#define PE3_REG_SCRATCH  0x13
#define PE3_REG_PULSES   0x16 /* strobe edges since the last write to 18 */
#define PE3_REG_CONFIG   0x18

/* Register 00 (transmit status) bits. */
#define PE3_ISR0_TXOK 0x80
#define PE3_ISR0_COLL 0x04

/* Register 01 (receive status) bits. */
#define PE3_ISR1_RXOK 0x80

/* Register 04 (mode) bits. */
#define PE3_MODE_NOLOOP 0x02 /* internal loopback if clear */

/* Register 05 (receive filter) values. */
#define PE3_RX_OFF       0
#define PE3_RX_UNICAST   1
#define PE3_RX_MULTICAST 2
#define PE3_RX_PROMISC   3

/* Register 06 (control) bits. */
#define PE3_CONTROL_STOP 0x80

/* Register 07 selects the bank for registers [08:0F]. */
#define PE3_BANK_ADDRESS   0
#define PE3_BANK_MULTICAST 1
#define PE3_BANK_OPERATE   2

/* Register 10 (interrupt enable) bits. */
#define PE3_INTEN_ON    0x01
#define PE3_INTEN_FORCE 0x02

/* Register 11 (EEPROM) bits. */
#define PE3_EE_DI  0x01
#define PE3_EE_CLK 0x02
#define PE3_EE_CS  0x04

/* Receive header byte 0 (receive status) bits. */
#define PE3_RXHDR_OK    0x01
#define PE3_RXHDR_MCAST 0x20

typedef struct pe3_rx_pkt_t {
    uint16_t len;
    uint8_t  status;
    uint8_t  data[NET_MAX_FRAME];
} pe3_rx_pkt_t;

typedef struct pe3_t {
    void      *log;
    void      *lpt;
    netcard_t *card;

    union {
        uint8_t  mac[6];
        uint64_t mac_u64;
    };
    uint8_t mchash[8];

    uint8_t data;
    uint8_t ctrl;
    uint8_t strobe_mask;
    uint8_t strobe_cand;
    uint8_t pulses;

    uint8_t reg;
    uint8_t pend_read;
    uint8_t pend_write;
    uint8_t block;
    uint8_t epp_run;
    uint8_t cfg_left;
    uint8_t cfg[3];

    uint8_t status;
    uint8_t rd_phase;
    uint8_t rd_bidir;

    uint8_t isr0;
    uint8_t isr1;
    uint8_t imr0;
    uint8_t imr1;
    uint8_t mode;
    uint8_t rxmode;
    uint8_t control;
    uint8_t bank;
    uint8_t tx_stage;
    uint8_t scratch;
    uint8_t inten;

    nmc93cxx_eeprom_t *eeprom;
    uint16_t           eeprom_data[64];
    uint8_t            ee_ctrl;

    uint16_t tx_len;
    uint16_t tx_ptr;
    uint8_t  tx_pkt[NET_MAX_FRAME];

    pe3_rx_pkt_t rx_queue[16];
    uint8_t      rx_head;
    uint8_t      rx_count;

    uint8_t  rx_active;
    uint8_t  rx_hdr[4];
    uint8_t  rx_hdr_ptr;
    uint16_t rx_ptr;
    uint16_t rx_left;
} pe3_t;

#ifdef ENABLE_PE3_LOG
int pe3_do_log = ENABLE_PE3_LOG;

static void
pe3_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (pe3_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define pe3_log(priv, fmt, ...)
#endif

static uint8_t
mcast_index(const uint8_t *dst)
{
    uint32_t crc = 0xffffffff;

    for (uint8_t i = 0; i < 6; i++) {
        uint8_t b = dst[i];
        for (uint8_t j = 0; j < 8; j++) {
            uint8_t carry = ((crc >> 31) ^ b) & 0x01;
            crc <<= 1;
            b >>= 1;
            if (carry)
                crc ^= 0x04c11db7;
        }
    }

    /* Same CRC32 as NE2000, but the final hash is reverse([5:0]) instead. */
    uint8_t ret = 0;
    for (uint8_t i = 0; i < 6; i++)
        ret = (ret << 1) | ((crc >> i) & 0x01);

    return ret;
}

static void
pe3_update_irq(pe3_t *dev)
{
    if (dev->lpt) {
        int raise = (dev->inten & PE3_INTEN_FORCE) || ((dev->inten & PE3_INTEN_ON) && ((dev->isr0 & dev->imr0) || (dev->isr1 & dev->imr1)));
        lpt_irq(dev->lpt, raise);
    }
}

static void
pe3_rx_flush(pe3_t *dev)
{
    dev->rx_head    = 0;
    dev->rx_count   = 0;
    dev->rx_active  = 0;
    dev->rx_hdr_ptr = 0;
    dev->rx_ptr     = 0;
    dev->rx_left    = 0;
    dev->isr1 &= ~PE3_ISR1_RXOK;
    pe3_update_irq(dev);
}

static int
pe3_rx_queue(pe3_t *dev, uint8_t *buf, int io_len)
{
    if ((dev->rxmode == PE3_RX_OFF) || (dev->control & PE3_CONTROL_STOP))
        return 0;

    if (dev->rxmode != PE3_RX_PROMISC) {
        if ((AS_U64(buf[0]) & le64_to_cpu(0xffffffffffffULL)) == le64_to_cpu(0xffffffffffffULL)) { /* brodcast */
            /* Accepted by all modes except off. */
        } else if (buf[0] & 0x01) { /* multicast */
            if (dev->rxmode < PE3_RX_MULTICAST)
                return 1;
            uint8_t idx = mcast_index(buf);
            if (!(dev->mchash[idx >> 3] & (1 << (idx & 7))))
                return 1;
        } else if ((AS_U64(buf[0]) & le64_to_cpu(0xffffffffffffULL)) != dev->mac_u64) { /* unicast not for me */
            return 1;
        }
    }

    if (dev->rx_count >= (sizeof(dev->rx_queue) / sizeof(dev->rx_queue[0]))) {
        pe3_log(dev->log, "Dropped %d-byte packet, buffer full\n", io_len);
        return 0;
    }

    pe3_rx_pkt_t *pkt = &dev->rx_queue[(dev->rx_head + dev->rx_count) % (sizeof(dev->rx_queue) / sizeof(dev->rx_queue[0]))];
    if (io_len > (int) sizeof(pkt->data))
        io_len = sizeof(pkt->data);
    memcpy(pkt->data, buf, io_len);
    if (io_len < 60) { /* apply padding */
        memset(pkt->data + io_len, 0x00, 60 - io_len);
        io_len = 60;
    }
    pkt->len    = io_len;
    pkt->status = PE3_RXHDR_OK | ((buf[0] & 0x01) ? PE3_RXHDR_MCAST : 0);
    dev->rx_count++;

    dev->isr1 |= PE3_ISR1_RXOK;
    pe3_update_irq(dev);

    return 1;
}

static int
pe3_rx(void *priv, uint8_t *buf, int io_len)
{
    pe3_t *dev = (pe3_t *) priv;

    if (!(dev->mode & PE3_MODE_NOLOOP)) /* cut off from the network during loopback */
        return 0;

    return pe3_rx_queue(dev, buf, io_len);
}

static void
pe3_rx_open(pe3_t *dev)
{
    if (dev->rx_active || !dev->rx_count)
        return;

    pe3_rx_pkt_t *pkt = &dev->rx_queue[dev->rx_head];
    dev->rx_hdr[0]    = pkt->status;
    dev->rx_hdr[1]    = 0x00; /* the drivers never look at this one */
    dev->rx_hdr[2]    = pkt->len & 0xff;
    dev->rx_hdr[3]    = pkt->len >> 8;
    dev->rx_hdr_ptr   = 0;

    dev->rx_ptr    = 0;
    dev->rx_left   = pkt->len;
    dev->rx_active = 1;

    pe3_log(dev->log, "Receiving %d-byte packet\n", pkt->len);
}

static void
pe3_rx_close(pe3_t *dev)
{
    if (!dev->rx_active) {
        pe3_update_irq(dev);
        return;
    }

    dev->rx_head = (dev->rx_head + 1) % (sizeof(dev->rx_queue) / sizeof(dev->rx_queue[0]));
    dev->rx_count--;
    dev->rx_active = 0;
    dev->rx_left   = 0;

    if (dev->rx_count) /* another frame? */
        dev->isr1 |= PE3_ISR1_RXOK;
    pe3_update_irq(dev);
}

static uint8_t
pe3_rx_byte(pe3_t *dev)
{
    if (!dev->rx_active) {
        pe3_rx_open(dev);
        if (!dev->rx_active)
            return 0xff;
    }

    if (dev->rx_hdr_ptr < sizeof(dev->rx_hdr))
        return dev->rx_hdr[dev->rx_hdr_ptr++];

    if (!dev->rx_left) {
        pe3_rx_close(dev);
        return 0xff;
    }

    uint8_t ret = dev->rx_queue[dev->rx_head].data[dev->rx_ptr++];

    if (!--dev->rx_left)
        pe3_rx_close(dev);

    return ret;
}

static void
pe3_transmit(pe3_t *dev)
{
    uint16_t len = dev->tx_len;
    if (len > dev->tx_ptr)
        len = dev->tx_ptr;
    if (len > sizeof(dev->tx_pkt))
        len = sizeof(dev->tx_pkt);

    if (len) {
        if (dev->mode & PE3_MODE_NOLOOP) { /* regular */
            pe3_log(dev->log, "Transmitting %d-byte packet\n", len);
            network_tx(dev->card, dev->tx_pkt, len);
        } else { /* loopback */
            pe3_log(dev->log, "Looping back %d-byte packet\n", len);
            pe3_rx_queue(dev, dev->tx_pkt, len);
        }
    }

    dev->tx_ptr   = 0;
    dev->tx_stage = 0;

    dev->isr0 |= PE3_ISR0_TXOK;
    pe3_update_irq(dev);
}

static void
pe3_banked_write(pe3_t *dev, uint8_t idx, uint8_t val)
{
    switch ((dev->bank >> 2) & 0x03) {
        case PE3_BANK_ADDRESS:
            if (idx < sizeof(dev->mac))
                dev->mac[idx] = val;
            break;

        case PE3_BANK_MULTICAST:
            dev->mchash[idx] = val;
            break;

        case PE3_BANK_OPERATE:
            switch (idx) {
                case PE3_BANKED_DATA: /* outgoing frame: 16-bit length followed by the frame itself */
                    if (dev->tx_stage < 2) {
                        if (dev->tx_stage++)
                            dev->tx_len |= val << 8;
                        else
                            dev->tx_len = val;
                        dev->tx_ptr = 0;
                    } else if (dev->tx_ptr < sizeof(dev->tx_pkt)) {
                        dev->tx_pkt[dev->tx_ptr++] = val;
                    }
                    break;

                case PE3_BANKED_CMD:
                    if (val & 0x80)
                        pe3_transmit(dev);
                    break;

                case PE3_BANKED_ABORT:
                    if (val & 0x04)
                        pe3_rx_close(dev);
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static void
pe3_write_reg(pe3_t *dev, uint8_t reg, uint8_t val)
{
    pe3_log(dev->log, "write_reg(%02X, %02X)\n", reg, val);

    switch (reg) {
        case PE3_REG_ISR0:
            dev->isr0 &= ~val;
            pe3_update_irq(dev);
            break;

        case PE3_REG_ISR1:
            dev->isr1 &= ~val;
            pe3_update_irq(dev);
            break;

        case PE3_REG_IMR0:
            dev->imr0 = val;
            pe3_update_irq(dev);
            break;

        case PE3_REG_IMR1:
            dev->imr1 = val;
            pe3_update_irq(dev);
            break;

        case PE3_REG_MODE:
            dev->mode = val;
            break;

        case PE3_REG_RXMODE:
            dev->rxmode = val & 0x03;
            break;

        case PE3_REG_CONTROL:
            if ((val & PE3_CONTROL_STOP) && !(dev->control & PE3_CONTROL_STOP)) {
                pe3_rx_close(dev); /* any ongoing packet read is expected to be discarded... */
                pe3_rx_flush(dev); /* ...and so is anything still waiting behind it */
            }
            dev->control = val;
            break;

        case PE3_REG_BANK:
            dev->bank     = val;
            dev->tx_stage = 0; /* restart the outgoing frame */
            break;

        case PE3_REG_BANKED ...(PE3_REG_BANKED + 7):
            pe3_banked_write(dev, reg - PE3_REG_BANKED, val);
            break;

        case PE3_REG_INTEN:
            dev->inten = val;
            pe3_update_irq(dev);
            break;

        case PE3_REG_EEPROM:
            dev->ee_ctrl = val;
            if (dev->eeprom)
                nmc93cxx_eeprom_write(dev->eeprom, !!(val & PE3_EE_CS), !!(val & PE3_EE_CLK),
                                      !!(val & PE3_EE_DI));
            break;

        case PE3_REG_SCRATCH:
            dev->scratch = val;
            break;

        case PE3_REG_CONFIG:
            dev->pulses   = 0; /* reset strobe count */
            dev->cfg_left = 0; /* reset configuration command position */
            break;

        default:
            break;
    }
}

static uint8_t
pe3_read_reg(pe3_t *dev, uint8_t reg)
{
    uint8_t ret = 0x00;

    switch (reg) {
        case PE3_REG_ISR0:
            ret = dev->isr0;
            break;

        case PE3_REG_ISR1:
            ret = dev->isr1;
            break;

        case PE3_REG_MODE:
            ret = 0x00; /* no collisions */
            break;

        case PE3_REG_RXMODE:
            ret = (dev->rx_active || dev->rx_count) ? 0x00 : 0x40; /* bit 6 if nothing left to receive */
            break;

        case PE3_REG_CONTROL:
            ret = dev->control;
            break;

        case PE3_REG_BANKED:
            ret = pe3_rx_byte(dev); /* pull from receive stream */
            break;

        case PE3_REG_EEPROM:
            ret = dev->ee_ctrl & ~PE3_EE_DI;
            if (dev->eeprom && nmc93cxx_eeprom_read(dev->eeprom))
                ret |= PE3_EE_DI;
            break;

        case PE3_REG_SCRATCH:
            ret = dev->scratch;
            break;

        case PE3_REG_PULSES:
            ret = dev->pulses;
            break;

        default:
            break;
    }

    pe3_log(dev->log, "read_reg(%02X) = %02X\n", reg, ret);

    return ret;
}

static void
pe3_reset(pe3_t *dev)
{
    pe3_log(dev->log, "reset()\n");

    dev->strobe_mask = PE3_CTRL_STROBE;
    dev->strobe_cand = PE3_CTRL_STROBE | PE3_CTRL_INIT;
    dev->pulses      = 0;
    dev->pend_read   = 0;
    dev->pend_write  = 0;
    dev->block       = 0;
    dev->epp_run     = 0;
    dev->cfg_left    = 0;
    dev->status      = 0x00;
    dev->rd_phase    = 0;
    dev->rd_bidir    = 0;

    dev->imr0     = 0x00;
    dev->imr1     = 0xff; /* NT driver never writes this */
    dev->mode     = PE3_MODE_NOLOOP;
    dev->control  = 0x00;
    dev->bank     = 0x00;
    dev->tx_stage = 0;
    dev->scratch  = 0x00;
    dev->inten    = 0x00;

    dev->ee_ctrl = 0x00;
    if (dev->eeprom)
        nmc93cxx_eeprom_write(dev->eeprom, 0, 0, 0);

    dev->rxmode = PE3_RX_OFF;

    dev->isr0      = 0x00;
    dev->isr1      = 0x00;
    dev->rx_active = 0;
    dev->tx_ptr    = 0;
    dev->tx_len    = 0;

    dev->rx_head   = 0;
    dev->rx_count  = 0;
    dev->rx_active = 0;

    pe3_update_irq(dev);
}

static void
pe3_command(pe3_t *dev, uint8_t val)
{
    if (dev->cfg_left) { /* configuration command is followed by 3 more bytes */
        dev->cfg[3 - dev->cfg_left] = val;
        if (--dev->cfg_left == 0) {
            dev->pend_write = 0;
            /* Configuration only selects whether or not the data strobe is autofd, not whether it's strobe or nInit. */
            if (dev->cfg[1] & 0x04)
                dev->strobe_cand = PE3_CTRL_STROBE | PE3_CTRL_INIT;
            else
                dev->strobe_cand = PE3_CTRL_AUTOFEED;
            if (!(dev->strobe_mask & dev->strobe_cand))
                dev->strobe_mask = dev->strobe_cand & -dev->strobe_cand;
            pe3_log(dev->log, "config=%02X/%02X/%02X strobe mask=%02X cand=%02X\n",
                    dev->cfg[0], dev->cfg[1], dev->cfg[2], dev->strobe_mask, dev->strobe_cand);
        }
        return;
    }

    if ((val >> 5) == PE3_OP_MISC) {
        if (val == 0x18) { /* end block write */
            dev->block   = 0;
            dev->epp_run = 0;
        }
        pe3_log(dev->log, "command(%02X)\n", val);
        return;
    }

    dev->pend_read  = 0;
    dev->pend_write = 0;
    dev->block      = 0;
    dev->epp_run    = 0;
    dev->reg        = val & 0x1f;

    switch (val >> 5) {
        case PE3_OP_WRITE:
        case PE3_OP_WRITE_ALT:
            dev->pend_write = 1;
            if (dev->reg == PE3_REG_CONFIG)
                dev->cfg_left = 3;
            break;

        case PE3_OP_READ:
        case PE3_OP_BLOCK_RD:
            dev->pend_read = 1; /* prepare for read */
            dev->rd_phase  = 0;
            dev->rd_bidir  = 0; /* assume not bidirectional at first */
            break;

        case PE3_OP_BLOCK_WR:
            dev->block = 1;
            if (dev->reg == PE3_REG_BANKED)
                dev->tx_ptr = 0;
            break;

        default:
            break;
    }

    pe3_log(dev->log, "command(%02X)\n", val);
}

static void
pe3_write_data(uint8_t val, void *priv)
{
    pe3_t *dev = (pe3_t *) priv;

    dev->data = val;
}

static void
pe3_write_ctrl(uint8_t val, void *priv)
{
    pe3_t *dev = (pe3_t *) priv;

    uint8_t prev = dev->ctrl;
    dev->ctrl    = val;

    if ((prev ^ val) & PE3_CTRL_SELECT) { /* SelectIn = command */
        if (prev & PE3_CTRL_SELECT)
            pe3_command(dev, dev->data);
        return;
    }

    if (val & PE3_CTRL_SELECT) /* no data cycles while SelectIn held (i.e. LPT probes) */
        return;

    /* Work out which line the driver settled on as the data strobe. */
    uint8_t moved = (prev ^ val) & dev->strobe_cand;
    if (moved && !(moved & (moved - 1)))
        dev->strobe_mask = moved;

    if (dev->pend_read && (val & PE3_CTRL_DIR)) { /* bidirectional read engaged and port is reading */
        if ((prev ^ val) & dev->strobe_mask) {
            dev->status   = pe3_read_reg(dev, dev->reg);
            dev->rd_bidir = 1;
            lpt_write_to_dat(dev->lpt, dev->status);
        }
        return;
    }

    if (dev->pend_read && dev->rd_bidir) /* bidirectional read engaged but port is not reading */
        return;

    if ((prev ^ val) & ~(PE3_CTRL_STROBE | PE3_CTRL_AUTOFEED | PE3_CTRL_INIT)) /* ignore changes to just direction or interrupt enable */
        return;

    if (dev->block) { /* block write */
        if (dev->reg == PE3_REG_BANKED) {
            if (dev->tx_ptr < sizeof(dev->tx_pkt))
                dev->tx_pkt[dev->tx_ptr++] = dev->data;
        } else {
            pe3_write_reg(dev, dev->reg, dev->data);
        }
        return;
    }

    if (dev->pend_read) { /* nibble read engaged */
        if (!(dev->rd_phase & 1))
            dev->status = pe3_read_reg(dev, dev->reg);
        dev->rd_phase++;
    }

    if (!((prev ^ val) & dev->strobe_mask))
        return;

    dev->pulses++;

    if (dev->pend_write) {
        dev->pend_write = 0;
        pe3_write_reg(dev, dev->reg, dev->data);
    }
}

static void
pe3_epp_write_data(uint8_t is_addr, uint8_t val, void *priv)
{
    pe3_t *dev = (pe3_t *) priv;

    if (is_addr) { /* command on EPP address cycle */
        pe3_command(dev, val);
        return;
    }

    if (dev->pend_write || dev->block || dev->epp_run) { /* once command is latched, EPP data cycles keep writing to the register */
        dev->pend_write = 0;
        dev->epp_run    = 1;
        pe3_write_reg(dev, dev->reg, val);
    }
}

static void
pe3_epp_request_read(uint8_t is_addr, void *priv)
{
    pe3_t *dev = (pe3_t *) priv;

    lpt_write_to_dat(dev->lpt, is_addr ? 0xff : pe3_read_reg(dev, dev->reg)); /* nothing reads address back */
}

static uint8_t
pe3_read_status(void *priv)
{
    pe3_t *dev = (pe3_t *) priv;

    /* Unusual nibble encoding. */
    uint8_t ret;
    if (dev->rd_phase & 1) /* [6] [ ] [2] [1] [0] [ ] [ ] [ ] */
        ret = ((dev->status & 0x07) << 3) | ((dev->status & 0x40) << 1);
    else /* [7] [ ] [5] [4] [3] [ ] [ ] [ ] */
        ret = (dev->status & 0x38) | (dev->status & 0x80);
    return ret | 0x40; /* ack left alone */
}

static void *
pe3_init(const device_t *info)
{
    pe3_t *dev = (pe3_t *) calloc(1, sizeof(pe3_t));

    dev->log = log_open("PE3");
    pe3_log(dev->log, "init()\n");

    dev->lpt = lpt_attach_ex(device_get_config_int("port"), pe3_write_data, pe3_write_ctrl,
                             NULL, pe3_read_status, NULL, pe3_epp_write_data,
                             pe3_epp_request_read, dev);
    if (!dev->lpt) {
        pe3_log(dev->log, "parallel port already claimed by another device\n");
        log_close(dev->log);
        free(dev);
        return NULL;
    }

    dev->mac[0] = 0x00; /* Xircom OUI, checked by drivers */
    dev->mac[1] = 0x80;
    dev->mac[2] = 0xc7;

    int mac = device_get_config_mac("mac", -1);
    if (mac & 0xff000000) {
        dev->mac[3] = random_generate();
        dev->mac[4] = random_generate();
        dev->mac[5] = random_generate();
        mac         = (((int) dev->mac[3]) << 16) | (((int) dev->mac[4]) << 8) | ((int) dev->mac[5]);
        device_set_config_mac("mac", mac);
    } else {
        dev->mac[3] = (mac >> 16) & 0xff;
        dev->mac[4] = (mac >> 8) & 0xff;
        dev->mac[5] = mac & 0xff;
    }

    memset(dev->eeprom_data, 0x00, sizeof(dev->eeprom_data));

    dev->eeprom_data[1] = dev->mac[1] | (dev->mac[2] << 8);
    dev->eeprom_data[2] = 'T' | (dev->mac[0] << 8); /* PE3-10BT (twisted pair only model) */

    uint32_t mfg =   /* manufacturing date (stored big endian) */
        (7 << 28) |  /* month */
        (11 << 23) | /* day */
        (5 << 17) |  /* year from 1990 (not Y2K-compliant in PE3TEST.EXE) */
        (9 << 12) |  /* hour */
        (50 << 6) |  /* minute */
        0;           /* second */
    dev->eeprom_data[4] = bswap16(mfg & 0xffff);
    dev->eeprom_data[5] = bswap16(mfg >> 16);

    dev->eeprom_data[6] = dev->mac[3];
    dev->eeprom_data[7] = dev->mac[5] | (dev->mac[4] << 8);

    memcpy(&dev->eeprom_data[8], "4\x00"
                                 "99 1omrcXi) (C",
           16); /* copyright string */

    for (uint8_t i = 1; i < 16; i++) /* checksum */
        dev->eeprom_data[0] -= dev->eeprom_data[i];

    int  inst           = device_get_instance();
    char filename[1024] = { 0 };
    snprintf(filename, sizeof(filename), "nmc93cxx_eeprom_%s_%d.nvr", info->internal_name, inst);
    nmc93cxx_eeprom_params_t params = {
        .type            = NMC_93C46_x16_64,
        .default_content = dev->eeprom_data,
        .filename        = filename
    };
    dev->eeprom = device_add_inst_params(&nmc93cxx_device, inst, &params);

    dev->card = network_attach(dev, dev->mac, pe3_rx, NULL);

    pe3_reset(dev);

    return dev;
}

static void
pe3_close(void *priv)
{
    pe3_t *dev = (pe3_t *) priv;

    pe3_log(dev->log, "close()\n");

    if (dev->card)
        netcard_close(dev->card);

    log_close(dev->log);

    free(dev);
}

// clang-format off
static const device_config_t pe3_config[] = {
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
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t pe3_device = {
    .name          = "Xircom Pocket Ethernet III",
    .internal_name = "pe3",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = pe3_init,
    .close         = pe3_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pe3_config
};
