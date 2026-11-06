/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Emulation of the AMD Am7990 LANCE Ethernet controller in
 *          the 16-bit byte-swapped bus mode used by the DEC VAXmate.
 *
 *          Register map (VAXmate TRM Vol.1, Table 13-1):
 *            0x0C60 (alias 0x0C64)  16-bit  RDP - register data port
 *            0x0C62 (alias 0x0C66)  16-bit  RAP - register address port
 *            0x0C68 (0x0C69-0x0C6F)  8-bit  NICSR - DEC interface logic
 *
 *          CSR3 must be programmed with BSWP (bit 2) set, see TRM Table 13-2:
 *          the LANCE sits on the byte-swapped 16-bit I/O bus of the VAXmate,
 *          so every word it hands to / takes from system memory has its byte
 *          lanes exchanged.
 *
 *          Descriptor and initialization block layouts follow the Am7990
 *          datasheet; the ring walking code is based on MAME's am79c90.
 *
 * Authors: WNT50
 *          Patrick Mackinlay
 *
 *          Copyright 2026 WNT50.
 *          Copyright 2018-2025 Patrick Mackinlay.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/thread.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/dma.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/random.h>
#include <86box/network.h>
#include <86box/net_lance.h>

/* Defaults used when no board parameters are supplied. */
#define LANCE_BASE    0x0c60
#define LANCE_IRQ     10

/* CSR0 bits */
#define CSR0_INIT 0x0001
#define CSR0_STRT 0x0002
#define CSR0_STOP 0x0004
#define CSR0_TDMD 0x0008
#define CSR0_TXON 0x0010
#define CSR0_RXON 0x0020
#define CSR0_INEA 0x0040
#define CSR0_INTR 0x0080
/* TRM 1-21: INTR is the OR of BABL/MISS/MERR/RINT/TINT/IDON; CERR does not raise an interrupt. */
#define CSR0_INTR_MASK (CSR0_BABL | CSR0_MISS | CSR0_MERR | CSR0_RINT | CSR0_TINT | CSR0_IDON)
#define CSR0_IDON 0x0100
#define CSR0_TINT 0x0200
#define CSR0_RINT 0x0400
#define CSR0_MERR 0x0800
#define CSR0_MISS 0x1000
#define CSR0_CERR 0x2000
#define CSR0_BABL 0x4000
#define CSR0_ERR  0x8000

/* CSR3 bits (VAXmate: BSWP = 1). ACON/BCON describe the address and byte
   control modes; this model always drives the 16-bit bus in the VAXmate
   wiring, so they are decoded but have no effect. */
#define CSR3_BSWP 0x0004
#define CSR3_ACON 0x0002
#define CSR3_BCON 0x0001

/* MODE word of the initialization block */
#define MODE_DRX  0x0001
#define MODE_DTX  0x0002
#define MODE_LOOP 0x0004
#define MODE_DTCR 0x0008 /* CRC comes from the host; this model always appends the FCS itself. */
#define MODE_PROM 0x8000

/* Transmit message descriptor word 1 */
#define TMD1_ENP 0x0100
#define TMD1_STP 0x0200
#define TMD1_ERR 0x4000
#define TMD1_OWN 0x8000

/* Transmit message descriptor word 3 */
#define TMD3_BUFF 0x8000
#define TMD3_UFLO 0x4000

/* Receive message descriptor word 1 */
#define RMD1_ENP  0x0100
#define RMD1_STP  0x0200
#define RMD1_BUFF 0x0400
#define RMD1_CRC  0x0800 /* Decoded but never set: the model computes the FCS itself. */
#define RMD1_OFLO 0x1000
#define RMD1_FRAM 0x2000 /* Same: frames always contain a whole number of bytes. */
#define RMD1_ERR  0x4000
#define RMD1_OWN  0x8000

/* Receive message descriptor word 3 */
#define RMD3_MCNT      0x0fff

#define INIT_ADDR_MASK 0xfffffe
#define RING_ADDR_MASK 0xfffff8

#define LANCE_BUF_MAX  1600

#ifdef ENABLE_LANCE_LOG
int lance_do_log = ENABLE_LANCE_LOG;

static void
lance_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}
#else
#    define lance_log(fmt, ...)
#endif

static void
lance_irq_line(lance_t *dev)
{
    int active = (dev->irq_gate && (dev->csr[0] & CSR0_INEA) && (dev->csr[0] & CSR0_INTR)) ? 1 : 0;

    if (active && !dev->irq) {
        dev->irq = 1;
        lance_log("[LANCE] IRQ%d assert (csr0=%04X gate=%d)\n",
                  dev->irq_num, dev->csr[0], dev->irq_gate);
        picint(1 << dev->irq_num);
    } else if (!active && dev->irq) {
        dev->irq = 0;
        lance_log("[LANCE] IRQ%d clear\n", dev->irq_num);
        picintc(1 << dev->irq_num);
    }
}

static void
lance_update_irq(lance_t *dev)
{
    /* INTR is a latch: set when any source is set, cleared by reading
       CSR0 (TRM 1-21). */
    if (dev->csr[0] & CSR0_INTR_MASK)
        dev->csr[0] |= CSR0_INTR;
    else
        dev->csr[0] &= (uint16_t) ~CSR0_INTR;

    lance_irq_line(dev);
}

/* The byte lanes of the 16-bit bus are swapped on the VAXmate (CSR3 BSWP). */
static void
lance_bswap(uint8_t *buf, int len)
{
    int i;

    for (i = 0; i + 1 < len; i += 2) {
        uint8_t t  = buf[i];
        buf[i]     = buf[i + 1];
        buf[i + 1] = t;
    }
}

/* TRM 1-21: a bus master cycle that gets no READY (nothing decodes the
   address) raises MERR and turns both transmitter and receiver off. The
   reachable window depends on the board wiring: 24 bits on ISA, 1 MB on
   the VAXmate. */
static int
lance_dma_check(lance_t *dev, uint32_t addr, int len)
{
    if ((len > 0) && ((addr >= dev->dma_limit) || ((uint32_t) len > (dev->dma_limit - addr)))) {
        dev->csr[0] |= CSR0_MERR | CSR0_ERR;
        dev->csr[0] &= (uint16_t) ~(CSR0_TXON | CSR0_RXON);
        lance_log("[LANCE] MERR addr=%06X len=%d csr0=%04X\n",
                  addr, len, dev->csr[0]);
        lance_update_irq(dev);
        return 0;
    }

    return 1;
}

static void
lance_dma_read(lance_t *dev, uint32_t addr, uint8_t *buf, int len)
{
    if (!lance_dma_check(dev, addr, len)) {
        memset(buf, 0x00, len);
        return;
    }

    dma_bm_read(addr, buf, len, 2);

    if (dev->csr[3] & CSR3_BSWP)
        lance_bswap(buf, len);
}

static void
lance_dma_write(lance_t *dev, uint32_t addr, const uint8_t *buf, int len)
{
    uint8_t tmp[LANCE_BUF_MAX];

    if (!lance_dma_check(dev, addr, len))
        return;

    if (len > (int) sizeof(tmp))
        len = sizeof(tmp);

    memcpy(tmp, buf, len);

    if (dev->csr[3] & CSR3_BSWP)
        lance_bswap(tmp, len);

    dma_bm_write(addr, tmp, len, 2);
}

/* Single entry point for ring descriptors: the slot must be inside
   the ring and the whole descriptor inside the 1 MB window (RLEN/TLEN
   bounds protection). An out-of-range access is handled as MERR. */
static int
lance_ring_entry(lance_t *dev, const char *tag, uint32_t base, uint8_t mask,
                 uint8_t pos, uint32_t *addr)
{
    uint32_t a;

    if (pos > mask) {
        lance_log("[LANCE] %s: position %u past ring mask %02X, wrapped\n",
                  tag, pos, mask);
        pos = (uint8_t) (pos & mask);
    }

    a = base + ((uint32_t) pos << 3);
    if ((a >= dev->dma_limit) || ((a + 8u) > dev->dma_limit)) {
        lance_log("[LANCE] %s: entry %u at %06X outside the %06X window\n",
                  tag, pos, a, dev->dma_limit);
        dev->csr[0] |= CSR0_MERR | CSR0_ERR;
        dev->csr[0] &= (uint16_t) ~(CSR0_TXON | CSR0_RXON);
        lance_update_irq(dev);
        return 0;
    }

    *addr = a;
    return 1;
}

static uint16_t
lance_md_read(lance_t *dev, uint32_t addr)
{
    uint8_t b[2];

    lance_dma_read(dev, addr, b, 2);

    return (uint16_t) (b[0] | (b[1] << 8));
}

static void
lance_md_write(lance_t *dev, uint32_t addr, uint16_t val)
{
    uint8_t b[2];

    b[0] = (uint8_t) (val & 0xff);
    b[1] = (uint8_t) (val >> 8);

    lance_dma_write(dev, addr, b, 2);
}

static uint32_t
lance_md_addr(uint16_t md0, uint16_t md1)
{
    return (uint32_t) (((md1 & 0x00ff) << 16) | md0);
}

/* Address filter: physical address, broadcast, or multicast through LADRF. */
static int
lance_address_filter(lance_t *dev, const uint8_t *buf, int len)
{
    static const uint8_t bc[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint64_t             hash;
    uint8_t              bit;

    if (len < 6)
        return 0;

    if (dev->mode & MODE_PROM)
        return 1;

    if (!memcmp(buf, dev->padr, 6))
        return 1;

    if (!memcmp(buf, bc, 6))
        return 1;

    if (buf[0] & 0x01) {
        hash = (uint64_t) dev->ladrf[0] | ((uint64_t) dev->ladrf[1] << 8) | ((uint64_t) dev->ladrf[2] << 16) | ((uint64_t) dev->ladrf[3] << 24) | ((uint64_t) dev->ladrf[4] << 32) | ((uint64_t) dev->ladrf[5] << 40) | ((uint64_t) dev->ladrf[6] << 48) | ((uint64_t) dev->ladrf[7] << 56);
        bit  = (uint8_t) ((buf[2] ^ buf[4] ^ buf[5]) & 0x3f);
        if (hash & (1ULL << bit))
            return 1;
    }

    return 0;
}

/* Initialization block: 12 words, see TRM chapter 13. */
static void
lance_init_block(lance_t *dev)
{
    uint8_t ib[24];
    int     i;

    dev->init_addr = ((uint32_t) dev->csr[1] | ((uint32_t) dev->csr[2] << 16)) & INIT_ADDR_MASK;

    memset(ib, 0x00, sizeof(ib));
    lance_dma_read(dev, dev->init_addr, ib, sizeof(ib));

    dev->mode = (uint16_t) (ib[0] | (ib[1] << 8));

    for (i = 0; i < 6; i++)
        dev->padr[i] = ib[2 + i];
    for (i = 0; i < 8; i++)
        dev->ladrf[i] = ib[8 + i];

    dev->rx_ring_base = ((uint32_t) ib[16] | ((uint32_t) ib[17] << 8) | ((uint32_t) ib[18] << 16)) & RING_ADDR_MASK;
    dev->tx_ring_base = ((uint32_t) ib[20] | ((uint32_t) ib[21] << 8) | ((uint32_t) ib[22] << 16)) & RING_ADDR_MASK;
    /* The mask is "entries - 1": RLEN/TLEN are 3 bits, i.e. 1..128 entries. */
    dev->rx_ring_mask = (uint8_t) ((1u << ((ib[19] >> 5) & 7)) - 1u);
    dev->tx_ring_mask = (uint8_t) ((1u << ((ib[23] >> 5) & 7)) - 1u);

    dev->rx_ring_pos = 0;
    dev->tx_ring_pos = 0;

    memcpy(dev->mac, dev->padr, 6);

    /* The rings must fit in the window the board can master to; out-of-range accesses raise MERR. */
    if ((dev->rx_ring_base + ((uint32_t) (dev->rx_ring_mask + 1) << 3)) > dev->dma_limit)
        lance_log("[LANCE] rx ring %05X + %u entries runs past the %06X window\n",
                  dev->rx_ring_base, (unsigned) (dev->rx_ring_mask + 1), dev->dma_limit);
    if ((dev->tx_ring_base + ((uint32_t) (dev->tx_ring_mask + 1) << 3)) > dev->dma_limit)
        lance_log("[LANCE] tx ring %05X + %u entries runs past the %06X window\n",
                  dev->tx_ring_base, (unsigned) (dev->tx_ring_mask + 1), dev->dma_limit);

    lance_log("[LANCE] init: mode=%04X mac=%02X:%02X:%02X:%02X:%02X:%02X "
              "rx=%05X/%d tx=%05X/%d\n",
              dev->mode, dev->padr[0], dev->padr[1], dev->padr[2],
              dev->padr[3], dev->padr[4], dev->padr[5],
              dev->rx_ring_base, 1 << ((ib[19] >> 5) & 7),
              dev->tx_ring_base, 1 << ((ib[23] >> 5) & 7));

    /* TRM 1-18: INIT is only cleared by RESET or by setting STOP, not by
       the chip when the block has been read; IDON reports completion. The
       NetWare driver relies on that and expects CSR0 = 0x33 after STRT. */
    dev->csr[0] |= CSR0_IDON;
}

static int  lance_receive(lance_t *dev, uint8_t *buf, int length);
static void lance_rx_complete(lance_t *dev, int result);
static void lance_tx_poll(void *priv);

static void
lance_tx_complete(lance_t *dev, int result)
{
    uint32_t ring_address;

    if (!lance_ring_entry(dev, "tx", dev->tx_ring_base, dev->tx_ring_mask,
                          dev->tx_ring_pos, &ring_address))
        return;

    if (result < 0)
        dev->tx_md[1] |= TMD1_ERR;

    /* Hand the descriptor back to the driver and move on. */
    lance_md_write(dev, ring_address | 6, dev->tx_md[3]);
    lance_md_write(dev, ring_address | 2, dev->tx_md[1]);

    dev->tx_ring_pos = (uint8_t) ((dev->tx_ring_pos + 1) & dev->tx_ring_mask);

    dev->csr[0] |= CSR0_TINT;

    /* TX completion must reach the host: the driver waits on IRQ10. */
    lance_update_irq(dev);

    /* Back-to-back transmit: poll again shortly. */
    timer_set_delay_u64(&dev->tx_timer, 20 * TIMER_USEC);
}

static void
lance_transmit(lance_t *dev)
{
    uint32_t ring_address;
    uint8_t  buf[LANCE_BUF_MAX];
    int      length = 0; /* bytes copied into buf; anything past it is only counted */
    int      total  = 0; /* whole frame length from TMD2, used to detect BABL */
    int      failed = 0; /* descriptor chain broke -> BUFF/UFLO */
    int      sent;

    memset(buf, 0x00, sizeof(buf));

    if (!lance_ring_entry(dev, "tx", dev->tx_ring_base, dev->tx_ring_mask,
                          dev->tx_ring_pos, &ring_address))
        return;

    for (;;) {
        int tx_buf_length;
        int copy;

        dev->tx_md[0] = lance_md_read(dev, ring_address | 0);
        dev->tx_md[2] = lance_md_read(dev, ring_address | 4);
        dev->tx_md[3] = 0x0000;

        /* TRM 1-25: the 12-bit BCNT field of TMD2 holds the two's complement
           of the buffer byte count; the upper nibble is "must be ones". */
        tx_buf_length = (int) (0u - dev->tx_md[2]) & 0x0fff;
        total += tx_buf_length;

        copy = tx_buf_length;
        if (copy > (int) sizeof(buf) - length)
            copy = (int) sizeof(buf) - length;
        if (copy > 0) {
            lance_log("[LANCE] tx frag md0=%04X md1=%04X addr=%06X len=%d\n",
                      dev->tx_md[0], dev->tx_md[1],
                      lance_md_addr(dev->tx_md[0], dev->tx_md[1]), copy);
            lance_dma_read(dev, lance_md_addr(dev->tx_md[0], dev->tx_md[1]),
                           &buf[length], copy);
        }
        length += copy;

        dev->tx_md[1] &= (uint16_t) ~TMD1_OWN;

        if (!(dev->tx_md[1] & TMD1_ENP)) {
            uint8_t  next_pos = (uint8_t) ((dev->tx_ring_pos + 1) & dev->tx_ring_mask);
            uint32_t next_addr;
            uint16_t next_md1;

            if (!lance_ring_entry(dev, "tx", dev->tx_ring_base,
                                  dev->tx_ring_mask, next_pos, &next_addr)) {
                failed = 1;
                break;
            }

            next_md1 = (next_addr != ring_address) ? lance_md_read(dev, next_addr | 2) : dev->tx_md[1];

            if (next_md1 & TMD1_OWN) {
                lance_md_write(dev, ring_address | 2, dev->tx_md[1]);
                dev->tx_ring_pos = next_pos;
                ring_address     = next_addr;
                dev->tx_md[1]    = next_md1;
                continue;
            }

            /* No next descriptor in the chain: the chip cannot fetch the
               whole frame -> BUFF + UFLO and the transmitter is turned off
               (TRM 4.9.2: BFU/UFLO clear TXON). */
            dev->tx_md[1] |= TMD1_ERR;
            dev->tx_md[3] |= TMD3_BUFF | TMD3_UFLO;
            dev->csr[0] &= (uint16_t) ~CSR0_TXON;
            failed = 1;
            break;
        }

        break;
    }

    if (failed) {
        if (dev->csr[0] & CSR0_MERR) {
            lance_log("[LANCE] tx aborted: MERR on the ring\n");
            dev->tx_md[1] |= TMD1_ERR;
            lance_tx_complete(dev, -1);
            return;
        }

        lance_log("[LANCE] tx aborted: BUFF/UFLO\n");
        lance_tx_complete(dev, -1);
        return;
    }

    /* The bus master cycle got no READY: MERR already turned the
       transmitter off, so nothing is sent. */
    if (dev->csr[0] & CSR0_MERR) {
        lance_log("[LANCE] tx aborted: MERR\n");
        dev->tx_md[1] |= TMD1_ERR;
        lance_tx_complete(dev, -1);
        return;
    }

    /* TRM 1-20: BABL is set on the 1519th byte, but the frame is still sent
       and the transmitter stays on. */
    if (total > 1518) {
        dev->csr[0] |= CSR0_BABL | CSR0_ERR;
        lance_log("[LANCE] BABL (%d bytes)\n", total);
    }

    sent = (length > 1518) ? 1518 : length;

    if (sent >= 14) {
        if (!(dev->mode & MODE_LOOP) && (dev->card != NULL)) {
            network_tx(dev->card, buf, sent);
            lance_log("[LANCE] tx %d bytes\n", sent);
        }
        lance_tx_complete(dev, sent);
    } else {
        lance_tx_complete(dev, -1);
    }
}

static void
lance_tx_poll(void *priv)
{
    lance_t *dev = (lance_t *) priv;
    uint32_t ring_address;

    if (!(dev->csr[0] & CSR0_TXON))
        return;

    dev->csr[0] &= (uint16_t) ~CSR0_TDMD;

    if (!lance_ring_entry(dev, "tx", dev->tx_ring_base, dev->tx_ring_mask,
                          dev->tx_ring_pos, &ring_address))
        return;

    dev->tx_md[1] = lance_md_read(dev, ring_address | 2);

    if (!(dev->tx_md[1] & TMD1_OWN))
        return;

    if (!(dev->tx_md[1] & TMD1_STP)) {
        /* Descriptor does not start a packet: release it. */
        lance_md_write(dev, ring_address | 2, dev->tx_md[1] & (uint16_t) ~TMD1_OWN);
        dev->tx_ring_pos = (uint8_t) ((dev->tx_ring_pos + 1) & dev->tx_ring_mask);
        timer_set_delay_u64(&dev->tx_timer, 20 * TIMER_USEC);
        return;
    }

    lance_transmit(dev);
}

/* Reflected CRC-32 polynomial, as used for the Ethernet FCS. */
static uint32_t
lance_crc32(const uint8_t *p, int length)
{
    uint32_t crc = 0xffffffff;
    int      i, bit;

    for (i = 0; i < length; i++) {
        crc ^= p[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320 & (uint32_t) -(int32_t) (crc & 1));
    }

    return ~crc;
}

static int
lance_receive(lance_t *dev, uint8_t *buf, int length)
{
    uint8_t  frame[LANCE_BUF_MAX + 4];
    uint32_t ring_address;
    uint32_t fcs;
    int      offset = 0;

    if (!(dev->csr[0] & CSR0_RXON) || (dev->mode & MODE_DRX))
        return -1;

    /* Overlong (jabber) frames are dropped as well. */
    if (length > 1518)
        return -1;

    /* Too short to be a real frame even once padded. */
    if (length < 40)
        return -1;

    /* The backend hands over the frame without the Ethernet pad. On the wire
       the transmitter pads it to 60 bytes, so outside loopback the LANCE
       never sees anything shorter than 64 bytes including the FCS. */
    memcpy(frame, buf, length);
    if (length < 60) {
        memset(&frame[length], 0x00, 60 - length);
        length = 60;
    }
    buf = frame;

    if (!lance_address_filter(dev, buf, length))
        return -1;

    /* The real chip writes the FCS into the buffer too and counts it in MCNT. */
    fcs = lance_crc32(buf, length);
    lance_log("[LANCE] rx %d bytes fcs=%08x\n", length, fcs);
    frame[length + 0] = (uint8_t) (fcs >> 0);
    frame[length + 1] = (uint8_t) (fcs >> 8);
    frame[length + 2] = (uint8_t) (fcs >> 16);
    frame[length + 3] = (uint8_t) (fcs >> 24);
    buf               = frame;
    length += 4;

    if (!lance_ring_entry(dev, "rx", dev->rx_ring_base, dev->rx_ring_mask,
                          dev->rx_ring_pos, &ring_address))
        return -1;

    dev->rx_md[1] = lance_md_read(dev, ring_address | 2);

    if (!(dev->rx_md[1] & RMD1_OWN))
        return -2;

    dev->rx_md[1] |= RMD1_STP;

    while (offset < length) {
        int rx_buf_length;
        int count;

        dev->rx_md[0] = lance_md_read(dev, ring_address | 0);
        dev->rx_md[2] = lance_md_read(dev, ring_address | 4);

        /* TRM 18: RMD2 BCNT is the two's complement of the buffer size too. */
        rx_buf_length = (int) (0u - dev->rx_md[2]) & 0x0fff;
        count         = length - offset;
        if (count > rx_buf_length)
            count = rx_buf_length;
        if (count < 0)
            count = 0;

        if (count > 0)
            lance_dma_write(dev, lance_md_addr(dev->rx_md[0], dev->rx_md[1]),
                            &buf[offset], count);
        offset += count;

        dev->rx_md[1] &= (uint16_t) ~RMD1_OWN;

        if (offset < length) {
            uint8_t  next_pos = (uint8_t) ((dev->rx_ring_pos + 1) & dev->rx_ring_mask);
            uint32_t next_addr;
            uint16_t next_md1;

            if (!lance_ring_entry(dev, "rx", dev->rx_ring_base,
                                  dev->rx_ring_mask, next_pos, &next_addr)) {
                dev->rx_md[1] |= RMD1_ERR | RMD1_BUFF | RMD1_OFLO;
                break;
            }

            next_md1 = (next_addr != ring_address) ? lance_md_read(dev, next_addr | 2) : dev->rx_md[1];

            if (next_md1 & RMD1_OWN) {
                lance_md_write(dev, ring_address | 2, dev->rx_md[1]);
                dev->rx_ring_pos = next_pos;
                ring_address     = next_addr;
                dev->rx_md[1]    = next_md1;
            } else {
                /* No free next buffer: the rest of the frame is lost. */
                dev->rx_md[1] |= RMD1_ERR | RMD1_BUFF | RMD1_OFLO;
                break;
            }
        }
    }

    if (offset == length)
        dev->rx_md[1] |= RMD1_ENP;

    return offset;
}

static void
lance_rx_complete(lance_t *dev, int result)
{
    uint32_t ring_address;

    if (result == -1)
        return;

    if (!lance_ring_entry(dev, "rx", dev->rx_ring_base, dev->rx_ring_mask,
                          dev->rx_ring_pos, &ring_address))
        return;

    if (result == -2) {
        lance_log("[LANCE] rx miss (no owned descriptor), csr0=%04x\n",
                  (unsigned) (dev->csr[0] | CSR0_ERR | CSR0_MISS));
        dev->csr[0] |= CSR0_ERR | CSR0_MISS;
    } else {
        lance_md_write(dev, ring_address | 2, dev->rx_md[1]);
        {
            uint16_t mcnt = (dev->rx_md[1] & RMD1_ERR) ? 0x0000 : (uint16_t) (result & RMD3_MCNT);

            lance_md_write(dev, ring_address | 6, mcnt);
            lance_log("[LANCE] rx done md1=%04x mcnt=%d\n",
                      dev->rx_md[1], mcnt);
        }
        dev->rx_ring_pos = (uint8_t) ((dev->rx_ring_pos + 1) & dev->rx_ring_mask);
    }

    dev->csr[0] |= CSR0_RINT;
    lance_update_irq(dev);
}

int
lance_rx(void *card_drv, uint8_t *buf, int length)
{
    lance_t *dev = (lance_t *) card_drv;
    int      result;

    result = lance_receive(dev, buf, length);
    lance_rx_complete(dev, result);

    /* Network layer contract: zero means "could not take this frame", the
       packet stays queued and is offered again. Only a full receive ring
       defers it; an accepted or filtered-out frame is consumed. */
    return (result == -2) ? 0 : 1;
}

static uint16_t
lance_csr_read(lance_t *dev, int reg)
{
    uint16_t ret;

    switch (reg) {
        case 0:
            ret = dev->csr[0];
            /* Reading CSR0 clears INTR; the line comes back on the next event. */
            dev->csr[0] &= (uint16_t) ~CSR0_INTR;
            lance_irq_line(dev);
            break;

        case 3:
            /* CSR3 is only accessible while STOP is set. */
            if (!(dev->csr[0] & CSR0_STOP))
                ret = 0x0000;
            else
                ret = dev->csr[3];
            break;

        case 0x58:
            /* Chip version, low word. 0x3003 identifies the Am7990/C-LANCE
               as seen by the Linux and NetWare drivers. */
            ret = 0x3003;
            break;

        case 0x59:
            /* Chip version, high word. */
            ret = 0x0000;
            break;

        default:
            ret = 0x0000;
            break;
    }

    return ret;
}

static void
lance_csr_write(lance_t *dev, int reg, uint16_t val)
{
    switch (reg) {
        case 0:
            /* INIT and STRT are write-one-to-act bits. */
            if (val & CSR0_STOP) {
                /* STOP clears the chip state, CSR1/2/3 included. */
                dev->csr[0] = CSR0_STOP;
                dev->csr[1] = 0x0000;
                dev->csr[2] = 0x0000;
                dev->csr[3] = 0x0000;
                timer_disable(&dev->tx_timer);
                break;
            }

            /* TRM 1.9.2: command bits are write-one only ("the LANCE updates
               CSR0 by ORing the previous and present value"), so writing a
               zero must not clear INEA or TDMD.  STOP is the only bit that
               clears the register. INEA is applied after STRT/INIT because
               TRM 1-21 forbids setting it while STOP is still set. */
            lance_log("[LANCE] CSR0 write %04X (IADR=%05X)\n", val, dev->init_addr);

            /* Status bits are cleared by writing 1 (TRM: IDON/TINT/RINT/MERR/MISS/CERR/BABL/ERR). */
            dev->csr[0] &= (uint16_t) ~(val & (CSR0_IDON | CSR0_TINT | CSR0_RINT | CSR0_MERR | CSR0_MISS | CSR0_CERR | CSR0_BABL | CSR0_ERR));

            if (val & CSR0_STRT) {
                /* STRT must read back as 1 (VAXmate polls bit 1 at F6BA9).
                   INIT is only cleared by STOP or reset, so a started chip
                   reads 0x33 (INIT|STRT|TXON|RXON); the NetWare driver
                   checks exactly that after issuing STRT. */
                dev->csr[0] &= (uint16_t) ~CSR0_STOP;
                dev->csr[0] |= CSR0_STRT;
                if (dev->mode & MODE_DRX)
                    dev->csr[0] &= (uint16_t) ~CSR0_RXON;
                else
                    dev->csr[0] |= CSR0_RXON;
                if (dev->mode & MODE_DTX)
                    dev->csr[0] &= (uint16_t) ~CSR0_TXON;
                else
                    dev->csr[0] |= CSR0_TXON;
                timer_set_delay_u64(&dev->tx_timer, 20 * TIMER_USEC);
            }
            if ((val & CSR0_INIT) && !(dev->csr[0] & CSR0_INIT)) {
                /* Setting INIT (or STRT) also clears STOP, TRM 1.9.2. Only the
                   0 -> 1 transition starts the procedure: the driver writes
                   CSR0 back with INIT still set to acknowledge IDON. */
                dev->csr[0] &= (uint16_t) ~CSR0_STOP;
                dev->csr[0] |= CSR0_INIT;
                dev->init_addr = ((uint32_t) dev->csr[1] | ((uint32_t) dev->csr[2] << 16)) & INIT_ADDR_MASK;
                lance_init_block(dev);
            }
            if (!(dev->csr[0] & CSR0_STOP))
                dev->csr[0] |= (uint16_t) (val & CSR0_INEA);

            if ((val & CSR0_TDMD) && (dev->csr[0] & CSR0_TXON))
                timer_set_delay_u64(&dev->tx_timer, 20 * TIMER_USEC);
            break;

        case 1:
        case 2:
            dev->csr[reg] = val;
            break;

        case 3:
            if (dev->csr[0] & CSR0_STOP) {
                dev->csr[3] = val;
                lance_log("[LANCE] CSR3 = %04X\n", val);
            } else
                lance_log("[LANCE] CSR3 write ignored (STOP=0)\n");
            break;
    }

    lance_update_irq(dev);
}

uint16_t
lance_rdp_read(uint16_t addr, void *priv)
{
    lance_t *dev = (lance_t *) priv;

    (void) addr;

    lance_log("[LANCE] RDP read  csr%d\n", dev->rap & 3);
    return lance_csr_read(dev, dev->rap);
}

void
lance_rdp_write(uint16_t addr, uint16_t val, void *priv)
{
    lance_t *dev = (lance_t *) priv;

    (void) addr;

    lance_log("[LANCE] RDP write csr%d = %04X\n", dev->rap & 3, val);
    lance_csr_write(dev, dev->rap, val);
}

uint16_t
lance_rap_read(uint16_t addr, void *priv)
{
    lance_t *dev = (lance_t *) priv;

    (void) addr;

    lance_log("[LANCE] RAP read -> %d\n", dev->rap);
    return dev->rap;
}

void
lance_rap_write(uint16_t addr, uint16_t val, void *priv)
{
    lance_t *dev = (lance_t *) priv;

    (void) addr;

    dev->rap = (uint8_t) (val & 0xff);
    lance_log("[LANCE] RAP write %04X -> %d\n", val, dev->rap);
}

void
lance_reset(void *priv)
{
    lance_t *dev = (lance_t *) priv;

    if (dev == NULL)
        return;

    memset(dev->csr, 0x00, sizeof(dev->csr));
    dev->csr[0] = CSR0_STOP;
    dev->rap    = 0;
    dev->irq    = 0;
    dev->mode   = 0x0000;
    memset(dev->padr, 0x00, sizeof(dev->padr));
    memset(dev->ladrf, 0x00, sizeof(dev->ladrf));
    dev->rx_ring_pos  = 0;
    dev->tx_ring_pos  = 0;
    dev->rx_ring_base = 0x000000;
    dev->tx_ring_base = 0x000000;
    dev->rx_ring_mask = 0xff;
    dev->tx_ring_mask = 0xff;
    /* The interrupt gate belongs to the board glue, not to the chip;
       each board sets its own power-on state. */
    timer_disable(&dev->tx_timer);

    picintc(1 << dev->irq_num);
}

static void *
lance_init(const device_t *info)
{
    lance_t *dev;

    dev = (lance_t *) calloc(1, sizeof(lance_t));

    (void) info;

    if (dev == NULL)
        return NULL;

    /* Defaults; the board that instantiates the chip overrides these. */
    dev->base      = LANCE_BASE;
    dev->irq_num   = LANCE_IRQ;
    /* The LANCE drives 24 address lines, so an ISA board can master the
       whole 16 MB space; the VAXmate narrows this to 1 MB. */
    dev->dma_limit = 0x01000000;
    dev->mac[0]  = 0x08;
    dev->mac[1]  = 0x00;
    dev->mac[2]  = 0x2b;
    dev->mac[3]  = 0x12;
    dev->mac[4]  = 0x34;
    dev->mac[5]  = 0x56;

    timer_add(&dev->tx_timer, lance_tx_poll, dev, 0);
    lance_reset(dev);

    return dev;
}

static void
lance_close(void *priv)
{
    lance_t *dev = (lance_t *) priv;

    if (dev != NULL)
        free(dev);
}

/* DEC interface logic (board glue) gates the interrupt line. */
void
lance_set_irq_gate(void *priv, int on)
{
    lance_t *dev = (lance_t *) priv;

    if (dev == NULL)
        return;

    dev->irq_gate = on ? 1 : 0;
    lance_update_irq(dev);
}

int
lance_get_irq_gate(void *priv)
{
    lance_t *dev = (lance_t *) priv;

    return (dev != NULL) ? dev->irq_gate : 0;
}

const device_t lance_device = {
    .name          = "AMD Am7990 LANCE",
    .internal_name = "lance_am7990",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = lance_init,
    .close         = lance_close,
    .reset         = lance_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * -----------------------------------------------------------------------
 * DEC VAXmate board variant: The VAXmate wires the chip straight onto its
 * own bus at 0x0C60 with a separate interrupt/LED gate (NICSR) at 0x0C68;
 * there is no ISA slot, so it lives here next to the chip instead of in a
 * card module. The only user setting is the last three bytes of the
 * station address; the DEC OUI in front of them is fixed.
 * -----------------------------------------------------------------------
 */
#define LANCE_VAXMATE_NICSR 0x0c68
#define LANCE_VAXMATE_BASE  0x0c60
#define LANCE_VAXMATE_IRQ   10

/* NICSR bits (TRM page 13-17, write only, set/clear style). */
#define NICSR_INT_SET   0x01
#define NICSR_INT_CLEAR 0x04
#define NICSR_LED_CLEAR 0x08
#define NICSR_LED_SET   0x10

/* DEC OUI (08:00:2B) in front of the configured station address bytes. */
static const uint8_t lance_vaxmate_oui[3] = { 0x08, 0x00, 0x2b };

static void *lance_vaxmate;

static void
lance_vaxmate_nicsr_write(uint16_t addr, uint8_t val, void *priv)
{
    int gate = 0;

    (void) addr;
    (void) priv;

    /* TRM 13-17: a 1 in the set bit enables, a 1 in the clear bit disables,
       and setting both toggles the state. */
    if ((val & NICSR_INT_SET) && (val & NICSR_INT_CLEAR))
        gate = -1;
    else if (val & NICSR_INT_SET)
        gate = 1;
    else if (val & NICSR_INT_CLEAR)
        gate = 0;

    if (gate >= 0)
        lance_set_irq_gate(lance_vaxmate, gate);
    else
        lance_set_irq_gate(lance_vaxmate, !lance_get_irq_gate(lance_vaxmate));
}

static void *
lance_vaxmate_init(const device_t *info)
{
    lance_t *dev;
    uint8_t  mac[6];
    int      macval;

    (void) info;

    dev = (lance_t *) device_add(&lance_device);
    if (dev == NULL)
        return NULL;

    lance_vaxmate = dev;

    dev->base    = LANCE_VAXMATE_BASE;
    dev->irq_num = LANCE_VAXMATE_IRQ;

    /* The DEC OUI is fixed; the last three bytes come from the device
       configuration, or are generated once and kept. */
    memcpy(mac, lance_vaxmate_oui, sizeof(lance_vaxmate_oui));
    macval = device_get_config_mac("mac", -1);
    if (macval & 0xff000000) {
        mac[3] = random_generate();
        mac[4] = random_generate();
        mac[5] = random_generate();
        macval = (((int) mac[3]) << 16) | (((int) mac[4]) << 8) | ((int) mac[5]);
        device_set_config_mac("mac", macval);
    } else {
        mac[3] = (macval >> 16) & 0xff;
        mac[4] = (macval >> 8) & 0xff;
        mac[5] = macval & 0xff;
    }
    memcpy(dev->mac, mac, sizeof(dev->mac));

    lance_reset(dev);

    /* NICSR interrupt enable and diag LED are both off after reset. */
    lance_set_irq_gate(dev, 0);

    /* The VAXmate decodes only the first megabyte of system memory. */
    dev->dma_limit = 0x00100000;

    /* RDP is a single 16-bit port; it must not be registered over 0x0C62,
       or the RAP handler at that address is shadowed by the wider range. */
    io_sethandler(dev->base, 0x0002,
                  NULL, lance_rdp_read, NULL,
                  NULL, lance_rdp_write, NULL, dev);
    io_sethandler(dev->base + 0x0004, 0x0002,
                  NULL, lance_rdp_read, NULL,
                  NULL, lance_rdp_write, NULL, dev);
    io_sethandler(dev->base + 0x0002, 0x0002,
                  NULL, lance_rap_read, NULL,
                  NULL, lance_rap_write, NULL, dev);
    io_sethandler(dev->base + 0x0006, 0x0002,
                  NULL, lance_rap_read, NULL,
                  NULL, lance_rap_write, NULL, dev);

    dev->card = network_attach(dev, dev->mac, lance_rx, NULL);

    io_sethandler(LANCE_VAXMATE_NICSR, 0x0008,
                  NULL, NULL, NULL,
                  lance_vaxmate_nicsr_write, NULL, NULL, NULL);

    return dev;
}

static void
lance_vaxmate_close(void *priv)
{
    (void) priv;

    io_removehandler(LANCE_VAXMATE_NICSR, 0x0008,
                     NULL, NULL, NULL,
                     lance_vaxmate_nicsr_write, NULL, NULL, NULL);
    device_close(&lance_device);
    lance_vaxmate = NULL;
}

static void
lance_vaxmate_reset(void *priv)
{
    /* Power-on state: interface interrupt gate disabled. */
    lance_set_irq_gate(priv, 0);
}

// clang-format off
static const device_config_t lance_vaxmate_config[] = {
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

const device_t lance_vaxmate_device = {
    .name          = "AMD Am7990 (VAXmate On-Board)",
    .internal_name = "lance_vaxmate",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = lance_vaxmate_init,
    .close         = lance_vaxmate_close,
    .reset         = lance_vaxmate_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lance_vaxmate_config
};
