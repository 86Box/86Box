/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Emulation of the DP8390 Network Interface Controller used by
 *          the WD family, NE1000/NE2000 family, and 3Com 3C503 NIC's.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Bochs project,
 *
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2008-2018 Bochs project.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <time.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/net_dp8390.h>
#include <86box/plat_unused.h>

static void dp8390_tx(dp8390_t *dev, uint32_t val);
static int  dp8390_rx_common(void *priv, uint8_t *buf, int io_len);
int         dp8390_rx(void *priv, uint8_t *buf, int io_len);

int dp3890_inst = 0;

#ifdef ENABLE_DP8390_LOG
int dp8390_do_log = ENABLE_DP8390_LOG;

static void
dp8390_log(const char *fmt, ...)
{
    va_list ap;

// if (dp8390_do_log >= lvl) {
    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
// }
}
#else
#    define dp8390_log(lvl, fmt, ...)
#endif

/*
 * Return the 6-bit index into the multicast
 * table. Stolen unashamedly from FreeBSD's if_ed.c
 */
static int
mcast_index(const void *dst)
{
#define POLYNOMIAL 0x04c11db6
    uint32_t       crc = 0xffffffffL;
    int            carry;
    uint8_t        b;
    const uint8_t *ep = (const uint8_t *) dst;

    for (int8_t i = 6; --i >= 0;) {
        b = *ep++;
        for (int8_t j = 8; --j >= 0;) {
            carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
            crc <<= 1;
            b >>= 1;
            if (carry)
                crc = ((crc ^ POLYNOMIAL) | carry);
        }
    }
    return (crc >> 26);
#undef POLYNOMIAL
}

/*
 * Access the 32K private RAM.
 *
 * The NE2000 memory is accessed through the data port of the
 * ASIC (offset 0) after setting up a remote-DMA transfer.
 * Both byte and word accesses are allowed.
 * The first 16 bytes contain the MAC address at even locations,
 * and there is 16K of buffer memory starting at 16K.
 */
uint32_t
dp8390_chipmem_read(dp8390_t *dev, uint32_t addr, unsigned int len)
{
    uint32_t retval = 0;

#ifdef ENABLE_DP8390_LOG
    if ((len > 1) && (addr & (len - 1)))
        dp8390_log("DP8390: unaligned chipmem word read\n");
#endif

    dp8390_log("DP8390: Chipmem Read Address=%04x\n", addr);

    /* ROM'd MAC address */
    for (unsigned int i = 0; i < len; i++) {
        if ((addr >= dev->mem_start) && (addr < dev->mem_end))
            retval |= (uint32_t) (dev->mem[addr - dev->mem_start]) << (i << 3);
        else if (addr < dev->macaddr_size)
            retval |= ((uint32_t) dev->macaddr[addr & (dev->macaddr_size - 1)]) << (i << 3);
        else {
            dp8390_log("DP8390: out-of-bounds chipmem read, %04X\n", addr);
            retval |= 0xff << (i << 3);
        }
        addr++;
    }

    return retval;
}

void
dp8390_chipmem_write(dp8390_t *dev, uint32_t addr, uint32_t val, unsigned len)
{
#ifdef ENABLE_DP8390_LOG
    if ((len > 1) && (addr & (len - 1)))
        dp8390_log("DP8390: unaligned chipmem word write\n");
#endif

    dp8390_log("DP8390: Chipmem Write Address=%04x\n", addr);

    for (unsigned int i = 0; i < len; i++) {
        if ((addr < dev->mem_start) || (addr >= dev->mem_end)) {
            dp8390_log("DP8390: out-of-bounds chipmem write, %04X\n", addr);
            return;
        }

        dev->mem[addr - dev->mem_start] = val & 0xff;
        val >>= 8;
        addr++;
    }
}

/* Routines for handling reads/writes to the Command Register. */
uint32_t
dp8390_read_cr(dp8390_t *dev)
{
    uint32_t retval;

    retval = (((dev->CR.pgsel & 0x03) << 6) | ((dev->CR.rdma_cmd & 0x07) << 3) | (dev->CR.tx_packet << 2) | (dev->CR.start << 1) | (dev->CR.stop));
    dp8390_log("DP8390: read CR returns 0x%02x\n", retval);

    return retval;
}

void
dp8390_write_cr(dp8390_t *dev, uint32_t val)
{
    dp8390_log("DP8390: wrote 0x%02x to CR\n", val);

    /* Validate remote-DMA */
    if ((val & 0x38) == 0x00) {
#ifdef ENABLE_DP8390_LOG
        dp8390_log("DP8390: CR write - invalid rDMA value 0\n");
#endif
        val |= 0x20; /* dma_cmd == 4 is a safe default */
    }

    /* Check for s/w reset */
    if (val & 0x01) {
        dev->ISR.reset = 1;
        dev->CR.stop   = 1;
    } else {
        dev->CR.stop = 0;
    }

    dev->CR.rdma_cmd = (val & 0x38) >> 3;

    /* If start command issued, the RST bit in the ISR */
    /* must be cleared */
    if ((val & 0x02) && !dev->CR.start)
        dev->ISR.reset = 0;

    dev->CR.start = ((val & 0x02) == 0x02);
    dev->CR.pgsel = (val & 0xc0) >> 6;

    /* Check for send-packet command */
    if (dev->CR.rdma_cmd == 3) {
        /* Set up DMA read from receive ring */
        dev->remote_start = dev->remote_dma = dev->bound_ptr * 256;
        dev->remote_bytes                   = (uint16_t) dp8390_chipmem_read(dev, dev->bound_ptr * 256 + 2, 2);
        dp8390_log("DP8390: sending buffer #x%x length %d\n",
                   dev->remote_start, dev->remote_bytes);
    }

    /* Check for start-tx */
    if ((val & 0x04) && dev->TCR.loop_cntl) {
        if (dev->TCR.loop_cntl) {
            dp8390_rx_common(dev, &dev->mem[((dev->tx_page_start * 256) - dev->mem_start) & dev->mem_wrap],
                             dev->tx_bytes);
        }
    } else if (val & 0x04) {
        if (dev->CR.stop || (!dev->CR.start && (dev->flags & DP8390_FLAG_CHECK_CR))) {
            if (dev->tx_bytes == 0) /* njh@bandsman.co.uk */
                return;             /* Solaris9 probe */
#ifdef ENABLE_DP8390_LOG
            dp8390_log("DP8390: CR write - tx start, dev in reset\n");
#endif
        }

#ifdef ENABLE_DP8390_LOG
        if (dev->tx_bytes == 0)
            dp8390_log("DP8390: CR write - tx start, tx bytes == 0\n");
#endif

        /* Send the packet to the system driver */
        dev->CR.tx_packet = 1;

        /* TODO: report TX error to the driver ? */
        if (!(dev->card->link_state & NET_LINK_DOWN))
            network_tx(dev->card, &dev->mem[((dev->tx_page_start * 256) - dev->mem_start) & dev->mem_wrap], dev->tx_bytes);

            /* some more debug */
#ifdef ENABLE_DP8390_LOG
        if (dev->tx_timer_active)
            dp8390_log("DP8390: CR write, tx timer still active\n");
#endif

        dp8390_tx(dev, val);
    }

    /* Linux probes for an interrupt by setting up a remote-DMA read
     * of 0 bytes with remote-DMA completion interrupts enabled.
     * Detect this here */
    if ((dev->CR.rdma_cmd == 0x01) && dev->CR.start && (dev->remote_bytes == 0)) {
        dev->ISR.rdma_done = 1;
        if (dev->IMR.rdma_inte && dev->interrupt) {
            dev->interrupt(dev->priv, 1);
            if (dev->flags & DP8390_FLAG_CLEAR_IRQ)
                dev->interrupt(dev->priv, 0);
        }
    }
}

static void
dp8390_tx(dp8390_t *dev, UNUSED(uint32_t val))
{
    dev->CR.tx_packet = 0;
    dev->TSR.tx_ok    = 1;
    dev->ISR.pkt_tx   = 1;

    /* Generate an interrupt if not masked */
    if (dev->IMR.tx_inte && dev->interrupt)
        dev->interrupt(dev->priv, 1);
    dev->tx_timer_active = 0;
}

/*
 * Called by the platform-specific code when an Ethernet frame
 * has been received. The destination address is tested to see
 * if it should be accepted, and if the RX ring has enough room,
 * it is copied into it and the receive process is updated.
 */
static int
dp8390_rx_common(void *priv, uint8_t *buf, int io_len)
{
    dp8390_t      *dev           = (dp8390_t *) priv;
    static uint8_t bcast_addr[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t        pkthdr[4];
    uint8_t       *startptr;
    int            pages;
    int            avail;
    int            idx;
    int            nextpage;
    int            endbytes;

    if (io_len != 60)
        dp8390_log("rx_frame with length %d\n", io_len);

    if ((dev->CR.stop != 0) || (dev->page_start == 0))
        return 0;

    if (dev->card->link_state & NET_LINK_DOWN)
        return 0;
    /*
     * Add the pkt header + CRC to the length, and work
     * out how many 256-byte pages the frame would occupy.
     */
    pages = (io_len + sizeof(pkthdr) + sizeof(uint32_t) + 255) / 256;
    if (dev->curr_page < dev->bound_ptr) {
        avail = dev->bound_ptr - dev->curr_page;
    } else {
        avail = (dev->page_stop - dev->page_start) - (dev->curr_page - dev->bound_ptr);
    }

    /*
     * Avoid getting into a buffer overflow condition by
     * not attempting to do partial receives. The emulation
     * to handle this condition seems particularly painful.
     */
    if ((avail < pages)
#if DP8390_NEVER_FULL_RING
        || (avail == pages)
#endif
    ) {
#ifdef ENABLE_DP8390_LOG
        dp8390_log("DP8390: no space\n");
#endif

        return 0;
    }

    if ((io_len < 40 /*60*/) && !dev->RCR.runts_ok) {
#ifdef ENABLE_DP8390_LOG
        dp8390_log("DP8390: rejected small packet, length %d\n", io_len);
#endif

        return 1;
    }

    /* Some computers don't care... */
    if (io_len < 60)
        io_len = 60;

    dp8390_log("DP8390: RX %x:%x:%x:%x:%x:%x > %x:%x:%x:%x:%x:%x len %d\n",
               buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
               buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
               io_len);

    /* Do address filtering if not in promiscuous mode. */
    if (!dev->RCR.promisc) {
        /* If this is a broadcast frame.. */
        if (!memcmp(buf, bcast_addr, 6)) {
            /* Broadcast not enabled, we're done. */
            if (!dev->RCR.broadcast) {
#ifdef ENABLE_DP8390_LOG
                dp8390_log("DP8390: RX BC disabled\n");
#endif
                return 1;
            }
        }

        /* If this is a multicast frame.. */
        else if (buf[0] & 0x01) {
            /* Multicast not enabled, we're done. */
            if (!dev->RCR.multicast) {
#ifdef ENABLE_DP8390_LOG
                dp8390_log("DP8390: RX MC disabled\n");
#endif
                return 1;
            }

            /* Are we listening to this multicast address? */
            idx = mcast_index(buf);
            if (!(dev->mchash[idx >> 3] & (1 << (idx & 0x7)))) {
#ifdef ENABLE_DP8390_LOG
                dp8390_log("DP8390: RX MC not listed\n");
#endif
                return 1;
            }
        }

        /* Unicast, must be for us.. */
        else if (memcmp(buf, dev->physaddr, 6))
            return 1;
    } else {
#ifdef ENABLE_DP8390_LOG
        dp8390_log("DP8390: RX promiscuous receive\n");
#endif
    }

    nextpage = dev->curr_page + pages;
    if (nextpage >= dev->page_stop)
        nextpage -= (dev->page_stop - dev->page_start);

    /* Set up packet header. */
    pkthdr[0] = 0x01; /* RXOK - packet is OK */
    if (buf[0] & 0x01)
        pkthdr[0] |= 0x20;                        /* MULTICAST packet */
    pkthdr[1] = nextpage;                         /* ptr to next packet */
    pkthdr[2] = (io_len + sizeof(pkthdr)) & 0xff; /* length-low */
    pkthdr[3] = (io_len + sizeof(pkthdr)) >> 8;   /* length-hi */
    dp8390_log("DP8390: RX pkthdr [%02x %02x %02x %02x]\n",
               pkthdr[0], pkthdr[1], pkthdr[2], pkthdr[3]);

    /* Copy into buffer, update curpage, and signal interrupt if config'd */
    if (((dev->curr_page * 256) - dev->mem_start) >= dev->mem_size)
        /* Do this to fix Windows 2000 crashing the emulator when its
           MPU-401 probe hits the NIC. */
        startptr = dev->sink_buffer;
    else
        startptr = &dev->mem[(dev->curr_page * 256) - dev->mem_start];
    memcpy(startptr, pkthdr, sizeof(pkthdr));
    if ((nextpage > dev->curr_page) || ((dev->curr_page + pages) == dev->page_stop)) {
        memcpy(startptr + sizeof(pkthdr), buf, io_len);
    } else {
        endbytes = (dev->page_stop - dev->curr_page) * 256;
        memcpy(startptr + sizeof(pkthdr), buf, endbytes - sizeof(pkthdr));
        startptr = &dev->mem[((dev->page_start * 256) - dev->mem_start) & dev->mem_wrap];
        memcpy(startptr, buf + endbytes - sizeof(pkthdr), io_len - endbytes + 8);
    }
    dev->curr_page = nextpage;

    dev->RSR.rx_ok   = 1;
    dev->RSR.rx_mbit = (buf[0] & 0x01) ? 1 : 0;
    dev->ISR.pkt_rx  = 1;

    if (dev->IMR.rx_inte && dev->interrupt)
        dev->interrupt(dev->priv, 1);

    return 1;
}

int
dp8390_rx(void *priv, uint8_t *buf, int io_len)
{
    const dp8390_t *dev = (dp8390_t *) priv;

    if ((dev->DCR.loop == 0) || (dev->TCR.loop_cntl != 0))
        return 0;

    return dp8390_rx_common(priv, buf, io_len);
}

/* Handle reads/writes to the 'zeroth' page of the DS8390 register file. */
uint32_t
dp8390_page0_read(dp8390_t *dev, uint32_t off, unsigned int len)
{
    uint8_t retval = 0;

    if (len > 1) {
        /* encountered with win98 hardware probe */
        dp8390_log("DP8390: bad length! Page0 read from register 0x%02x, len=%u\n",
                   off, len);
        return retval;
    }

    switch (off) {
        case 0x01: /* CLDA0 */
            retval = (dev->local_dma & 0xff);
            break;

        case 0x02: /* CLDA1 */
            retval = (dev->local_dma >> 8);
            break;

        case 0x03: /* BNRY */
            retval = dev->bound_ptr;
            break;

        case 0x04: /* TSR */
            retval = ((dev->TSR.ow_coll << 7) | (dev->TSR.cd_hbeat << 6) | (dev->TSR.fifo_ur << 5) | (dev->TSR.no_carrier << 4) | (dev->TSR.aborted << 3) | (dev->TSR.collided << 2) | (dev->TSR.tx_ok));
            break;

        case 0x05: /* NCR */
            retval = dev->num_coll;
            break;

        case 0x06: /* FIFO */
                   /* reading FIFO is only valid in loopback mode */
#ifdef ENABLE_DP8390_LOG
            dp8390_log("DP8390: reading FIFO not supported yet\n");
#endif
            retval = dev->fifo;
            break;

        case 0x07: /* ISR */
            retval = ((dev->ISR.reset << 7) | (dev->ISR.rdma_done << 6) | (dev->ISR.cnt_oflow << 5) | (dev->ISR.overwrite << 4) | (dev->ISR.tx_err << 3) | (dev->ISR.rx_err << 2) | (dev->ISR.pkt_tx << 1) | (dev->ISR.pkt_rx));
            break;

        case 0x08: /* CRDA0 */
            retval = (dev->remote_dma & 0xff);
            break;

        case 0x09: /* CRDA1 */
            retval = (dev->remote_dma >> 8);
            break;

        case 0x0a: /* reserved / RTL8029ID0 */
            retval = dev->id0;
            break;

        case 0x0b: /* reserved / RTL8029ID1 */
            retval = dev->id1;
            break;

        case 0x0c: /* RSR */
            retval = ((dev->RSR.deferred << 7) | (dev->RSR.rx_disabled << 6) | (dev->RSR.rx_mbit << 5) | (dev->RSR.rx_missed << 4) | (dev->RSR.fifo_or << 3) | (dev->RSR.bad_falign << 2) | (dev->RSR.bad_crc << 1) | (dev->RSR.rx_ok));
            break;

        case 0x0d: /* CNTR0 */
            retval = dev->tallycnt_0;
            break;

        case 0x0e: /* CNTR1 */
            retval = dev->tallycnt_1;
            break;

        case 0x0f: /* CNTR2 */
            retval = dev->tallycnt_2;
            break;

        default:
            dp8390_log("DP8390: Page0 register 0x%02x out of range\n", off);
            break;
    }

    dp8390_log("DP8390: Page0 read from register 0x%02x, value=0x%02x\n", off,
               retval);

    return retval;
}

void
dp8390_page0_write(dp8390_t *dev, uint32_t off, uint32_t val, UNUSED(unsigned len))
{
    uint8_t val2;

    dp8390_log("DP839: Page0 write to register 0x%02x, value=0x%02x\n",
               off, val);

    switch (off) {
        case 0x01: /* PSTART */
            dev->page_start = val;
            dp8390_log("DP8390: Starting RAM address: %04X\n", val << 8);
            break;

        case 0x02: /* PSTOP */
            dev->page_stop = val;
            dp8390_log("DP8390: Stopping RAM address: %04X\n", val << 8);
            break;

        case 0x03: /* BNRY */
            dev->bound_ptr = val;
            break;

        case 0x04: /* TPSR */
            dev->tx_page_start = val;
            break;

        case 0x05: /* TBCR0 */
            /* Clear out low byte and re-insert */
            dev->tx_bytes &= 0xff00;
            dev->tx_bytes |= (val & 0xff);
            break;

        case 0x06: /* TBCR1 */
            /* Clear out high byte and re-insert */
            dev->tx_bytes &= 0x00ff;
            dev->tx_bytes |= ((val & 0xff) << 8);
            break;

        case 0x07:       /* ISR */
            val &= 0x7f; /* clear RST bit - status-only bit */
            /* All other values are cleared iff the ISR bit is 1 */
            dev->ISR.pkt_rx &= !((int) ((val & 0x01) == 0x01));
            dev->ISR.pkt_tx &= !((int) ((val & 0x02) == 0x02));
            dev->ISR.rx_err &= !((int) ((val & 0x04) == 0x04));
            dev->ISR.tx_err &= !((int) ((val & 0x08) == 0x08));
            dev->ISR.overwrite &= !((int) ((val & 0x10) == 0x10));
            dev->ISR.cnt_oflow &= !((int) ((val & 0x20) == 0x20));
            dev->ISR.rdma_done &= !((int) ((val & 0x40) == 0x40));
            val = ((dev->ISR.rdma_done << 6) | (dev->ISR.cnt_oflow << 5) | (dev->ISR.overwrite << 4) | (dev->ISR.tx_err << 3) | (dev->ISR.rx_err << 2) | (dev->ISR.pkt_tx << 1) | (dev->ISR.pkt_rx));
            val &= ((dev->IMR.rdma_inte << 6) | (dev->IMR.cofl_inte << 5) | (dev->IMR.overw_inte << 4) | (dev->IMR.txerr_inte << 3) | (dev->IMR.rxerr_inte << 2) | (dev->IMR.tx_inte << 1) | (dev->IMR.rx_inte));
            if ((val == 0x00) && dev->interrupt)
                dev->interrupt(dev->priv, 0);
            break;

        case 0x08: /* RSAR0 */
            /* Clear out low byte and re-insert */
            dev->remote_start &= 0xff00;
            dev->remote_start |= (val & 0xff);
            dev->remote_dma = dev->remote_start;
            break;

        case 0x09: /* RSAR1 */
            /* Clear out high byte and re-insert */
            dev->remote_start &= 0x00ff;
            dev->remote_start |= ((val & 0xff) << 8);
            dev->remote_dma = dev->remote_start;
            break;

        case 0x0a: /* RBCR0 */
            /* Clear out low byte and re-insert */
            dev->remote_bytes &= 0xff00;
            dev->remote_bytes |= (val & 0xff);
            break;

        case 0x0b: /* RBCR1 */
            /* Clear out high byte and re-insert */
            dev->remote_bytes &= 0x00ff;
            dev->remote_bytes |= ((val & 0xff) << 8);
            break;

        case 0x0c: /* RCR */
                   /* Check if the reserved bits are set */
#ifdef ENABLE_DP8390_LOG
            if (val & 0xc0)
                dp8390_log("DP8390: RCR write, reserved bits set\n");
#endif

            /* Set all other bit-fields */
            dev->RCR.errors_ok = ((val & 0x01) == 0x01);
            dev->RCR.runts_ok  = ((val & 0x02) == 0x02);
            dev->RCR.broadcast = ((val & 0x04) == 0x04);
            dev->RCR.multicast = ((val & 0x08) == 0x08);
            dev->RCR.promisc   = ((val & 0x10) == 0x10);
            dev->RCR.monitor   = ((val & 0x20) == 0x20);

            /* Monitor bit is a little suspicious... */
#ifdef ENABLE_DP8390_LOG
            if (val & 0x20)
                dp8390_log("DP8390: RCR write, monitor bit set!\n");
#endif
            break;

        case 0x0d: /* TCR */
                   /* Check reserved bits */
#ifdef ENABLE_DP8390_LOG
            if (val & 0xe0)
                dp8390_log("DP8390: TCR write, reserved bits set\n");
#endif

            /* Test loop mode (not supported) */
            if (val & 0x06) {
                dev->TCR.loop_cntl = (val & 0x6) >> 1;
                dp8390_log("DP8390: TCR write, loop mode %d not supported\n",
                           dev->TCR.loop_cntl);
            } else
                dev->TCR.loop_cntl = 0;

                /* Inhibit-CRC not supported. */
#ifdef ENABLE_DP8390_LOG
            if (val & 0x01)
                dp8390_log("DP8390: TCR write, inhibit-CRC not supported\n");
#endif

                /* Auto-transmit disable very suspicious */
#ifdef ENABLE_DP8390_LOG
            if (val & 0x08)
                dp8390_log("DP8390: TCR write, auto transmit disable not supported\n");
#endif

            /* Allow collision-offset to be set, although not used */
            dev->TCR.coll_prio = ((val & 0x08) == 0x08);
            break;

        case 0x0e: /* DCR */
                   /* the loopback mode is not suppported yet */
#ifdef ENABLE_DP8390_LOG
            if (!(val & 0x08))
                dp8390_log("DP8390: DCR write, loopback mode selected\n");
#endif

                /* It is questionable to set longaddr and auto_rx, since
                 * they are not supported on the NE2000. Print a warning
                 * and continue. */
#ifdef ENABLE_DP8390_LOG
            if (val & 0x04)
                dp8390_log("DP8390: DCR write - LAS set ???\n");
            if (val & 0x10)
                dp8390_log("DP8390: DCR write - AR set ???\n");
#endif

            /* Set other values. */
            dev->DCR.wdsize    = ((val & 0x01) == 0x01);
            dev->DCR.endian    = ((val & 0x02) == 0x02);
            dev->DCR.longaddr  = ((val & 0x04) == 0x04); /* illegal ? */
            dev->DCR.loop      = ((val & 0x08) == 0x08);
            dev->DCR.auto_rx   = ((val & 0x10) == 0x10); /* also illegal ? */
            dev->DCR.fifo_size = (val & 0x60) >> 5;
            break;

        case 0x0f: /* IMR */
                   /* Check for reserved bit */
#ifdef ENABLE_DP8390_LOG
            if (val & 0x80)
                dp8390_log("DP8390: IMR write, reserved bit set\n");
#endif

            /* Set other values */
            dev->IMR.rx_inte    = ((val & 0x01) == 0x01);
            dev->IMR.tx_inte    = ((val & 0x02) == 0x02);
            dev->IMR.rxerr_inte = ((val & 0x04) == 0x04);
            dev->IMR.txerr_inte = ((val & 0x08) == 0x08);
            dev->IMR.overw_inte = ((val & 0x10) == 0x10);
            dev->IMR.cofl_inte  = ((val & 0x20) == 0x20);
            dev->IMR.rdma_inte  = ((val & 0x40) == 0x40);
            val2                = ((dev->ISR.rdma_done << 6) | (dev->ISR.cnt_oflow << 5) | (dev->ISR.overwrite << 4) | (dev->ISR.tx_err << 3) | (dev->ISR.rx_err << 2) | (dev->ISR.pkt_tx << 1) | (dev->ISR.pkt_rx));
            if (dev->interrupt) {
                if (((val & val2) & 0x7f) == 0)
                    dev->interrupt(dev->priv, 0);
                else
                    dev->interrupt(dev->priv, 1);
            }
            break;

        default:
            dp8390_log("DP8390: Page0 write, bad register 0x%02x\n", off);
            break;
    }
}

/* Handle reads/writes to the first page of the DS8390 register file. */
uint32_t
dp8390_page1_read(dp8390_t *dev, uint32_t off, UNUSED(unsigned int len))
{
    dp8390_log("DP8390: Page1 read from register 0x%02x, len=%u\n",
               off, len);

    switch (off) {
        case 0x01: /* PAR0-5 */
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
            return (dev->physaddr[off - 1]);

        case 0x07: /* CURR */
            dp8390_log("DP8390: returning current page: 0x%02x\n",
                       (dev->curr_page));
            return (dev->curr_page);

        case 0x08: /* MAR0-7 */
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            return (dev->mchash[off - 8]);

        default:
            dp8390_log("DP8390: Page1 read register 0x%02x out of range\n",
                       off);
            return 0;
    }
}

void
dp8390_page1_write(dp8390_t *dev, uint32_t off, uint32_t val, UNUSED(unsigned len))
{
    dp8390_log("DP8390: Page1 write to register 0x%02x, len=%u, value=0x%04x\n",
               off, len, val);

    switch (off) {
        case 0x01: /* PAR0-5 */
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
            dev->physaddr[off - 1] = val;
            if (off == 6)
                dp8390_log("DP8390: Physical address set to %02x:%02x:%02x:%02x:%02x:%02x\n",
                           dev->physaddr[0], dev->physaddr[1],
                           dev->physaddr[2], dev->physaddr[3],
                           dev->physaddr[4], dev->physaddr[5]);
            break;

        case 0x07: /* CURR */
            dev->curr_page = val;
            break;

        case 0x08: /* MAR0-7 */
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            dev->mchash[off - 8] = val;
            break;

        default:
            dp8390_log("DP8390: Page1 write register 0x%02x out of range\n",
                       off);
            break;
    }
}

/* Handle reads/writes to the second page of the DS8390 register file. */
uint32_t
dp8390_page2_read(dp8390_t *dev, uint32_t off, UNUSED(unsigned int len))
{
    dp8390_log("DP8390: Page2 read from register 0x%02x, len=%u\n",
               off, len);

    switch (off) {
        case 0x01: /* PSTART */
            return (dev->page_start);

        case 0x02: /* PSTOP */
            return (dev->page_stop);

        case 0x03: /* Remote Next-packet pointer */
            return (dev->rempkt_ptr);

        case 0x04: /* TPSR */
            return (dev->tx_page_start);

        case 0x05: /* Local Next-packet pointer */
            return (dev->localpkt_ptr);

        case 0x06: /* Address counter (upper) */
            return (dev->address_cnt >> 8);

        case 0x07: /* Address counter (lower) */
            return (dev->address_cnt & 0xff);

        case 0x08: /* Reserved */
        case 0x09:
        case 0x0a:
        case 0x0b:
            dp8390_log("DP8390: reserved Page2 read - register 0x%02x\n",
                       off);
            return 0xff;

        case 0x0c: /* RCR */
            return ((dev->RCR.monitor << 5) | (dev->RCR.promisc << 4) | (dev->RCR.multicast << 3) | (dev->RCR.broadcast << 2) | (dev->RCR.runts_ok << 1) | (dev->RCR.errors_ok));

        case 0x0d: /* TCR */
            return ((dev->TCR.coll_prio << 4) | (dev->TCR.ext_stoptx << 3) | ((dev->TCR.loop_cntl & 0x3) << 1) | (dev->TCR.crc_disable));

        case 0x0e: /* DCR */
            return (((dev->DCR.fifo_size & 0x3) << 5) | (dev->DCR.auto_rx << 4) | (dev->DCR.loop << 3) | (dev->DCR.longaddr << 2) | (dev->DCR.endian << 1) | (dev->DCR.wdsize));

        case 0x0f: /* IMR */
            return ((dev->IMR.rdma_inte << 6) | (dev->IMR.cofl_inte << 5) | (dev->IMR.overw_inte << 4) | (dev->IMR.txerr_inte << 3) | (dev->IMR.rxerr_inte << 2) | (dev->IMR.tx_inte << 1) | (dev->IMR.rx_inte));

        default:
            dp8390_log("DP8390: Page2 register 0x%02x out of range\n",
                       off);
            break;
    }

    return 0;
}

void
dp8390_page2_write(dp8390_t *dev, uint32_t off, uint32_t val, UNUSED(unsigned len))
{
    /* Maybe all writes here should be BX_PANIC()'d, since they
       affect internal operation, but let them through for now
       and print a warning. */
    dp8390_log("DP8390: Page2 write to register 0x%02x, len=%u, value=0x%04x\n",
               off, len, val);

    switch (off) {
        case 0x01: /* CLDA0 */
            /* Clear out low byte and re-insert */
            dev->local_dma &= 0xff00;
            dev->local_dma |= (val & 0xff);
            break;

        case 0x02: /* CLDA1 */
            /* Clear out high byte and re-insert */
            dev->local_dma &= 0x00ff;
            dev->local_dma |= ((val & 0xff) << 8);
            break;

        case 0x03: /* Remote Next-pkt pointer */
            dev->rempkt_ptr = val;
            break;

        case 0x04:
#ifdef ENABLE_DP8390_LOG
            dp8390_log("DP8390: Page 2 write to reserved register 0x04\n");
#endif
            break;

        case 0x05: /* Local Next-packet pointer */
            dev->localpkt_ptr = val;
            break;

        case 0x06: /* Address counter (upper) */
            /* Clear out high byte and re-insert */
            dev->address_cnt &= 0x00ff;
            dev->address_cnt |= ((val & 0xff) << 8);
            break;

        case 0x07: /* Address counter (lower) */
            /* Clear out low byte and re-insert */
            dev->address_cnt &= 0xff00;
            dev->address_cnt |= (val & 0xff);
            break;

        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            dp8390_log("DP8390: Page2 write to reserved register 0x%02x\n",
                       off);
            break;

        default:
            dp8390_log("DP8390: Page2 write, illegal register 0x%02x\n",
                       off);
            break;
    }
}

void
dp8390_set_defaults(dp8390_t *dev, uint8_t flags)
{
    dev->macaddr_size = (flags & DP8390_FLAG_EVEN_MAC) ? 32 : 16;

    dev->flags = flags;
}

void
dp8390_mem_alloc(dp8390_t *dev, uint32_t start, uint32_t size)
{
    dev->mem = (uint8_t *) calloc(size, sizeof(uint8_t));
    dev->mem_start = start;
    dev->mem_end   = start + size;
    dev->mem_size  = size;
    dev->mem_wrap  = size - 1;
    dp8390_log("DP8390: Mapped %i bytes of memory at address %04X in the address space\n", size, start);
}

void
dp8390_set_id(dp8390_t *dev, uint8_t id0, uint8_t id1)
{
    dev->id0 = id0;
    dev->id1 = id1;
}

void
dp8390_reset(dp8390_t *dev)
{
    int max;
    int shift = 0;

    if (dev->flags & DP8390_FLAG_EVEN_MAC)
        shift = 1;

    max = 16 << shift;

    /* Initialize the MAC address area by doubling the physical address */
    for (int i = 0; i < max; i++) {
        if (i < (6 << shift))
            dev->macaddr[i] = dev->physaddr[i >> shift];
        else /* Signature */
            dev->macaddr[i] = 0x57;
    }

    /* Zero out registers and memory */
    memset(&dev->CR, 0x00, sizeof(dev->CR));
    memset(&dev->ISR, 0x00, sizeof(dev->ISR));
    memset(&dev->IMR, 0x00, sizeof(dev->IMR));
    memset(&dev->DCR, 0x00, sizeof(dev->DCR));
    memset(&dev->TCR, 0x00, sizeof(dev->TCR));
    memset(&dev->TSR, 0x00, sizeof(dev->TSR));
    memset(&dev->RSR, 0x00, sizeof(dev->RSR));
    dev->tx_timer_active = 0;
    dev->local_dma       = 0;
    dev->page_start      = 0;
    dev->page_stop       = 0;
    dev->bound_ptr       = 0;
    dev->tx_page_start   = 0;
    dev->num_coll        = 0;
    dev->tx_bytes        = 0;
    dev->fifo            = 0;
    dev->remote_dma      = 0;
    dev->remote_start    = 0;

    dev->remote_bytes = 0;

    dev->tallycnt_0 = 0;
    dev->tallycnt_1 = 0;
    dev->tallycnt_2 = 0;

    dev->curr_page = 0;

    dev->rempkt_ptr   = 0;
    dev->localpkt_ptr = 0;
    dev->address_cnt  = 0;

    memset(dev->mem, 0x00, dev->mem_size);

    /* Set power-up conditions */
    dev->CR.stop      = 1;
    dev->CR.rdma_cmd  = 4;
    dev->ISR.reset    = 1;
    dev->DCR.longaddr = 1;

    if (dev->interrupt)
        dev->interrupt(dev->priv, 0);
}

void
dp8390_soft_reset(dp8390_t *dev)
{
    memset(&(dev->ISR), 0x00, sizeof(dev->ISR));
    dev->ISR.reset = 1;
}

static void *
dp8390_init(UNUSED(const device_t *info))
{
    dp8390_t *dp8390 = (dp8390_t *) calloc(1, sizeof(dp8390_t));

    /* Set values assuming WORD and only the clear IRQ flag -
       - the NIC can then call dp8390_set_defaults() again to
       change that. */
    dp8390_set_defaults(dp8390, DP8390_FLAG_CLEAR_IRQ);

    /* Set the two registers some NIC's use as ID bytes,
       to their default values of 0xFF. */
    dp8390_set_id(dp8390, 0xff, 0xff);

    return dp8390;
}

static void
dp8390_close(void *priv)
{
    dp8390_t *dp8390 = (dp8390_t *) priv;

    if (dp8390) {
        if (dp8390->mem)
            free(dp8390->mem);

        if (dp8390->card) {
            netcard_close(dp8390->card);
        }

        free(dp8390);
    }
}

const device_t dp8390_device = {
    .name          = "DP8390 Network Interface Controller",
    .internal_name = "dp8390",
    .flags         = 0,
    .local         = 0,
    .init          = dp8390_init,
    .close         = dp8390_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
