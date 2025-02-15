/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the following network controller:
 *            - 3Com Etherlink 3c500/3c501 (ISA 8-bit).
 *
 *
 *
 * Based on @(#)Dev3C501.cpp Oracle (VirtualBox)
 *
 * Authors: TheCollector1995, <mariogplayer@gmail.com>
 *          Oracle
 *
 *          Copyright 2022 TheCollector1995.
 *          Portions Copyright (C) 2022  Oracle and/or its affilitates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <time.h>
#include <stdbool.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/random.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/plat_unused.h>

/* Maximum number of times we report a link down to the guest (failure to send frame) */
#define ELNK_MAX_LINKDOWN_REPORTED 3

/* Maximum number of times we postpone restoring a link that is temporarily down. */
#define ELNK_MAX_LINKRST_POSTPONED 3

/* Maximum frame size we handle */
#define MAX_FRAME 1536

/* Size of the packet buffer. */
#define ELNK_BUF_SIZE 2048

/* The packet buffer address mask. */
#define ELNK_BUF_ADR_MASK (ELNK_BUF_SIZE - 1)

/* The GP buffer pointer address within the buffer. */
#define ELNK_GP(dev) (dev->uGPBufPtr & ELNK_BUF_ADR_MASK)

/* The GP buffer pointer mask.
 * NB: The GP buffer pointer is internally a 12-bit counter. When addressing into the
 * packet buffer, bit 11 is ignored. Required to pass 3C501 diagnostics.
 */
#define ELNK_GP_MASK 0xfff

/*********************************************************************************************************************************
 *   Structures and Typedefs                                                                                                      *
 *********************************************************************************************************************************/

/**
 *  EtherLink Transmit Command Register.
 */
typedef struct ELNK_XMIT_CMD {
    uint8_t det_ufl   : 1; /* Detect underflow. */
    uint8_t det_coll  : 1; /* Detect collision. */
    uint8_t det_16col : 1; /* Detect collision 16. */
    uint8_t det_succ  : 1; /* Detect successful xmit. */
    uint8_t unused    : 4;
} EL_XMT_CMD;

/**
 *  EtherLink Transmit Status Register.
 *
 *  We will never see any real collisions, although collisions (including 16
 *  successive collisions) may be useful to report when the link is down
 *  (something the 3C501 does not have a concept of).
 */
typedef struct ELNK_XMIT_STAT {
    uint8_t uflow  : 1; /* Underflow on transmit. */
    uint8_t coll   : 1; /* Collision on transmit. */
    uint8_t coll16 : 1; /* 16 collisions on transmit. */
    uint8_t ready  : 1; /* Ready for a new frame. */
    uint8_t undef  : 4;
} EL_XMT_STAT;

/** Address match (adr_match) modes. */
typedef enum {
    EL_ADRM_DISABLED = 0, /* Receiver disabled. */
    EL_ADRM_PROMISC  = 1, /* Receive all addresses. */
    EL_ADRM_BCAST    = 2, /* Receive station + broadcast. */
    EL_ADRM_MCAST    = 3  /* Receive station + multicast. */
} EL_ADDR_MATCH;

/**
 *  EtherLink Receive Command Register.
 */
typedef struct ELNK_RECV_CMD {
    uint8_t det_ofl   : 1; /* Detect overflow errors. */
    uint8_t det_fcs   : 1; /* Detect FCS errors. */
    uint8_t det_drbl  : 1; /* Detect dribble error. */
    uint8_t det_runt  : 1; /* Detect short frames. */
    uint8_t det_eof   : 1; /* Detect EOF (frames without overflow). */
    uint8_t acpt_good : 1; /* Accept good frames. */
    uint8_t adr_match : 2; /* Address match mode. */
} EL_RCV_CMD;

/**
 *  EtherLink Receive Status Register.
 */
typedef struct ELNK_RECV_STAT {
    uint8_t oflow   : 1; /* Overflow on receive. */
    uint8_t fcs     : 1; /* FCS error. */
    uint8_t dribble : 1; /* Dribble error. */
    uint8_t runt    : 1; /* Short frame. */
    uint8_t no_ovf  : 1; /* Received packet w/o overflow. */
    uint8_t good    : 1; /* Received good packet. */
    uint8_t undef   : 1;
    uint8_t stale   : 1; /* Stale receive status. */
} EL_RCV_STAT;

/** Buffer control (buf_ctl) modes. */
typedef enum {
    EL_BCTL_SYSTEM   = 0, /* Host has buffer access. */
    EL_BCTL_XMT_RCV  = 1, /* Transmit, then receive. */
    EL_BCTL_RECEIVE  = 2, /* Receive. */
    EL_BCTL_LOOPBACK = 3  /* Loopback. */
} EL_BUFFER_CONTROL;

/**
 *  EtherLink Auxiliary Status Register.
 */
typedef struct ELNK_AUX_CMD {
    uint8_t ire     : 1; /* Interrupt Request Enable. */
    uint8_t xmit_bf : 1; /* Xmit packets with bad FCS. */
    uint8_t buf_ctl : 2; /* Packet buffer control. */
    uint8_t unused  : 1;
    uint8_t dma_req : 1; /* DMA request. */
    uint8_t ride    : 1; /* Request Interrupt and DMA Enable. */
    uint8_t reset   : 1; /* Card in reset while set. */
} EL_AUX_CMD;

/**
 *  EtherLink Auxiliary Status Register.
 */
typedef struct ELNK_AUX_STAT {
    uint8_t recv_bsy : 1; /* Receive busy. */
    uint8_t xmit_bf  : 1; /* Xmit packets with bad FCS. */
    uint8_t buf_ctl  : 2; /* Packet buffer control. */
    uint8_t dma_done : 1; /* DMA done. */
    uint8_t dma_req  : 1; /* DMA request. */
    uint8_t ride     : 1; /* Request Interrupt and DMA Enable. */
    uint8_t xmit_bsy : 1; /* Transmit busy. */
} EL_AUX_STAT;

/**
 *  Internal interrupt status.
 */
typedef struct ELNK_INTR_STAT {
    uint8_t recv_intr : 1; /* Receive interrupt status. */
    uint8_t xmit_intr : 1; /* Transmit interrupt status. */
    uint8_t dma_intr  : 1; /* DMA interrupt status. */
    uint8_t unused    : 5;
} EL_INTR_STAT;

typedef struct threec501_t {
    uint32_t base_address;
    int      base_irq;
    uint32_t bios_addr;
    uint8_t  maclocal[6];     /* configured MAC (local) address. */
    bool     fISR;            /* Internal interrupt flag. */
    int      fDMA;            /* Internal DMA active flag. */
    int      fInReset;        /* Internal in-reset flag. */
    uint8_t  aPROM[8];        /* The PROM contents. Only 8 bytes addressable, R/O. */
    uint8_t  aStationAddr[6]; /* The station address programmed by the guest, W/O. */
    uint16_t uGPBufPtr;       /* General Purpose (GP) Buffer Pointer, R/W. */
    uint16_t uRCVBufPtr;      /* Receive (RCV) Buffer Pointer, R/W. */
    /** Transmit Command Register, W/O. */
    union {
        uint8_t    XmitCmdReg;
        EL_XMT_CMD XmitCmd;
    };
    /** Transmit Status Register, R/O. */
    union {
        uint8_t     XmitStatReg;
        EL_XMT_STAT XmitStat;
    };
    /** Receive Command Register, W/O. */
    union {
        uint8_t    RcvCmdReg;
        EL_RCV_CMD RcvCmd;
    };
    /** Receive Status Register, R/O. */
    union {
        uint8_t     RcvStatReg;
        EL_RCV_STAT RcvStat;
    };
    /** Auxiliary Command Register, W/O. */
    union {
        uint8_t    AuxCmdReg;
        EL_AUX_CMD AuxCmd;
    };
    /** Auxiliary Status Register, R/O. */
    union {
        uint8_t     AuxStatReg;
        EL_AUX_STAT AuxStat;
    };
    int      fLinkUp;               /* If set the link is currently up. */
    int      fLinkTempDown;         /* If set the link is temporarily down because of a saved state load. */
    uint16_t cLinkDownReported;     /* Number of times we've reported the link down. */
    uint16_t cLinkRestorePostponed; /* Number of times we've postponed the link restore. */
    /* Internal interrupt state. */
    union {
        uint8_t      IntrStateReg;
        EL_INTR_STAT IntrState;
    };
    uint32_t   cMsLinkUpDelay; /* MS to wait before we enable the link. */
    int        dma_channel;
    uint8_t    abLoopBuf[ELNK_BUF_SIZE];   /* The loopback transmit buffer (avoid stack allocations). */
    uint8_t    abRuntBuf[64];              /* The runt pad buffer (only really needs 60 bytes). */
    uint8_t    abPacketBuf[ELNK_BUF_SIZE]; /* The packet buffer. */
    int        dma_pos;
    pc_timer_t timer_restore;
    netcard_t *netcard;
} threec501_t;

#ifdef ENABLE_3COM501_LOG
int threec501_do_log = ENABLE_3COM501_LOG;

static void
threec501_log(const char *fmt, ...)
{
    va_list ap;

    if (threec501_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define threec501_log(fmt, ...)
#endif

static void elnkSoftReset(threec501_t *dev);
static void elnkR3HardReset(threec501_t *dev);

#ifndef ETHER_IS_MULTICAST /* Net/Open BSD macro it seems */
#    define ETHER_IS_MULTICAST(a) ((*(uint8_t *) (a)) & 1)
#endif

#define ETHER_ADDR_LEN ETH_ALEN
#define ETH_ALEN       6
#pragma pack(1)
struct ether_header /** @todo Use RTNETETHERHDR? */
{
    uint8_t  ether_dhost[ETH_ALEN]; /**< destination ethernet address */
    uint8_t  ether_shost[ETH_ALEN]; /**< source ethernet address */
    uint16_t ether_type;            /**< packet type ID field */
};
#pragma pack()

static void
elnk_do_irq(threec501_t *dev, int set)
{
    if (set)
        picint(1 << dev->base_irq);
    else
        picintc(1 << dev->base_irq);
}

/**
 * Checks if the link is up.
 * @returns true if the link is up.
 * @returns false if the link is down.
 */
static __inline int
elnkIsLinkUp(threec501_t *dev)
{
    return !dev->fLinkTempDown && dev->fLinkUp;
}

/**
 * Takes down the link temporarily if it's current status is up.
 *
 * This is used during restore and when replumbing the network link.
 *
 * The temporary link outage is supposed to indicate to the OS that all network
 * connections have been lost and that it for instance is appropriate to
 * renegotiate any DHCP lease.
 *
 * @param  pThis        The shared instance data.
 */
static void
elnkTempLinkDown(threec501_t *dev)
{
    if (dev->fLinkUp) {
        dev->fLinkTempDown         = 1;
        dev->cLinkDownReported     = 0;
        dev->cLinkRestorePostponed = 0;
        timer_set_delay_u64(&dev->timer_restore, (dev->cMsLinkUpDelay * 1000) * TIMER_USEC);
    }
}

/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static void
elnkR3Reset(void *priv)
{
    threec501_t *dev = (threec501_t *) priv;

    if (dev->fLinkTempDown) {
        dev->cLinkDownReported     = 0x1000;
        dev->cLinkRestorePostponed = 0x1000;
        timer_disable(&dev->timer_restore);
    }

    /** @todo How to flush the queues? */
    elnkR3HardReset(dev);
}

static void
elnkR3HardReset(threec501_t *dev)
{
    dev->fISR = false;
    elnk_do_irq(dev, 0);

    /* Clear the packet buffer and station address. */
    memset(dev->abPacketBuf, 0, sizeof(dev->abPacketBuf));
    memset(dev->aStationAddr, 0, sizeof(dev->aStationAddr));

    /* Reset the buffer pointers. */
    dev->uGPBufPtr  = 0;
    dev->uRCVBufPtr = 0;

    elnkSoftReset(dev);
}

/**
 * Check if incoming frame matches the station address.
 */
static __inline int
padr_match(threec501_t *dev, const uint8_t *buf)
{
    const struct ether_header *hdr = (const struct ether_header *) buf;
    int                        result;

    /* Checks own + broadcast as well as own + multicast. */
    result = (dev->RcvCmd.adr_match >= EL_ADRM_BCAST) && !memcmp(hdr->ether_dhost, dev->aStationAddr, 6);

    return result;
}

/**
 * Check if incoming frame is an accepted broadcast frame.
 */
static __inline int
padr_bcast(threec501_t *dev, const uint8_t *buf)
{
    static uint8_t             aBCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    const struct ether_header *hdr       = (const struct ether_header *) buf;
    int                        result    = (dev->RcvCmd.adr_match == EL_ADRM_BCAST) && !memcmp(hdr->ether_dhost, aBCAST, 6);

    return result;
}

/**
 * Check if incoming frame is an accepted multicast frame.
 */
static __inline int
padr_mcast(threec501_t *dev, const uint8_t *buf)
{
    const struct ether_header *hdr    = (const struct ether_header *) buf;
    int                        result = (dev->RcvCmd.adr_match == EL_ADRM_MCAST) && ETHER_IS_MULTICAST(hdr->ether_dhost);

    return result;
}

/**
 * Update the device IRQ line based on internal state.
 */
static void
elnkUpdateIrq(threec501_t *dev)
{
    bool fISR = false;

    /* IRQ is active if any interrupt source is active and interrupts
     * are enabled via RIDE or IRE.
     */
    if (dev->IntrStateReg && (dev->AuxCmd.ride || dev->AuxCmd.ire))
        fISR = true;

#ifdef ENABLE_3COM501_LOG
    threec501_log("3Com501: elnkUpdateIrq: fISR=%d\n", fISR);
#endif

    if (fISR != dev->fISR) {
        elnk_do_irq(dev, fISR);
        dev->fISR = fISR;
    }
}

/**
 * Perform a software reset of the NIC.
 */
static void
elnkSoftReset(threec501_t *dev)
{
#ifdef ENABLE_3COM501_LOG
    threec501_log("3Com501: elnkSoftReset\n");
#endif

    /* Clear some of the user-visible register state. */
    dev->XmitCmdReg  = 0;
    dev->XmitStatReg = 0;
    dev->RcvCmdReg   = 0;
    dev->RcvStatReg  = 0;
    dev->AuxCmdReg   = 0;
    dev->AuxStatReg  = 0;

    /* The "stale receive status" is cleared by receiving an "interesting" packet. */
    dev->RcvStat.stale = 1;

    /* By virtue of setting the buffer control to system, transmit is set to busy. */
    dev->AuxStat.xmit_bsy = 1;

    /* Clear internal interrupt state. */
    dev->IntrStateReg = 0;
    elnkUpdateIrq(dev);

    /* Note that a soft reset does not clear the packet buffer; software often
     * assumes that it survives soft reset. The programmed station address is
     * likewise not reset, and the buffer pointers are not reset either.
     * Verified on a real 3C501.
     */

    /* No longer in reset state. */
    dev->fInReset = 0;
}

/**
 * Write incoming data into the packet buffer.
 */
static int
elnkReceiveLocked(void *priv, uint8_t *src, int size)
{
    threec501_t *dev     = (threec501_t *) priv;
    int          is_padr = 0;
    int          is_bcast = 0;
    int          is_mcast = 0;
    bool         fLoopback = dev->RcvCmd.adr_match == EL_BCTL_LOOPBACK;

    union {
        uint8_t     RcvStatNewReg;
        EL_RCV_STAT RcvStatNew;
    } rcvstatnew;

    /* Drop everything if address matching is disabled. */
    if (dev->RcvCmd.adr_match == EL_ADRM_DISABLED)
        return 0;

    /* Drop zero-length packets (how does that even happen?). */
    if (!size)
        return 0;

    /*
     * Drop all packets if the cable is not connected (and not in loopback).
     */
    if (!elnkIsLinkUp(dev) && !fLoopback)
        return 0;

    /*
     * Do not receive further packets until receive status was read.
     */
    if (dev->RcvStat.stale == 0)
        return 0;

#ifdef ENABLE_3COM501_LOG
    threec501_log("3Com501: size on wire=%d, RCV ptr=%u\n", size, dev->uRCVBufPtr);
#endif

    /*
     * Perform address matching. Packets which do not pass the address
     * filter are always ignored.
     */
    /// @todo cbToRecv must be 6 or more (complete address)
    if ((dev->RcvCmd.adr_match == EL_ADRM_PROMISC) /* promiscuous enabled */
        || (is_padr = padr_match(dev, src))
        || (is_bcast = padr_bcast(dev, src))
        || (is_mcast = padr_mcast(dev, src))) {
        uint8_t *dst = dev->abPacketBuf + dev->uRCVBufPtr;

#ifdef ENABLE_3COM501_LOG
        threec501_log("3Com501 Packet passed address filter (is_padr=%d, is_bcast=%d, is_mcast=%d), size=%d\n", is_padr, is_bcast, is_mcast, size);
#endif

        /* Receive status is evaluated from scratch. The stale bit must remain set until we know better. */
        rcvstatnew.RcvStatNewReg    = 0;
        rcvstatnew.RcvStatNew.stale = 1;
        dev->RcvStatReg             = 0x80;

        /* Detect errors: Runts, overflow, and FCS errors.
         * NB: Dribble errors can not happen because we can only receive an
         * integral number of bytes. FCS errors are only possible in loopback
         * mode in case the FCS is deliberately corrupted.
         */

        /* See if we need to pad, and how much. Have to be careful because the
         * Receive Buffer Pointer might be near the end of the buffer.
         */
        if (size < 60) {
            /* In loopback mode only, short packets are flagged as errors because
             * diagnostic tools want to see the errors. Otherwise they're padded to
             * minimum length (if packet came over the wire, it should have been
             * properly padded).
             */
            /// @todo This really is kind of wrong. We shouldn't be doing any
            /// padding here, it should be done by the sending side!
            if (!fLoopback) {
                memset(dev->abRuntBuf, 0, sizeof(dev->abRuntBuf));
                memcpy(dev->abRuntBuf, src, size);
                size = 60;
                src  = dev->abRuntBuf;
            } else {
#ifdef ENABLE_3COM501_LOG
                threec501_log("3Com501 runt, size=%d\n", size);
#endif
                rcvstatnew.RcvStatNew.runt = 1;
            }
        }

        /* We don't care how big the frame is; if it fits into the buffer, all is
         * good. But conversely if the Receive Buffer Pointer is initially near the
         * end of the buffer, a small frame can trigger an overflow.
         */
        if ((dev->uRCVBufPtr + size) <= ELNK_BUF_SIZE)
            rcvstatnew.RcvStatNew.no_ovf = 1;
        else {
#ifdef ENABLE_3COM501_LOG
            threec501_log("3Com501 overflow, size=%d\n", size);
#endif
            rcvstatnew.RcvStatNew.oflow = 1;
        }

        if (fLoopback && dev->AuxCmd.xmit_bf) {
#ifdef ENABLE_3COM501_LOG
            threec501_log("3Com501 bad FCS\n");
#endif
            rcvstatnew.RcvStatNew.fcs = 1;
        }

        /* Error-free packets are considered good. */
        if (rcvstatnew.RcvStatNew.no_ovf && !rcvstatnew.RcvStatNew.fcs && !rcvstatnew.RcvStatNew.runt)
            rcvstatnew.RcvStatNew.good = 1;

        uint16_t cbCopy = (uint16_t) MIN(ELNK_BUF_SIZE - dev->uRCVBufPtr, size);

        /* All packets that passed the address filter are copied to the buffer. */

        /* Copy incoming data to the packet buffer. NB: Starts at the current
         * Receive Buffer Pointer position.
         */
        memcpy(dst, src, cbCopy);

        /* Packet length is indicated via the receive buffer pointer. */
        dev->uRCVBufPtr = (dev->uRCVBufPtr + cbCopy) & ELNK_GP_MASK;

#ifdef ENABLE_3COM501_LOG
        threec501_log("3Com501 Received packet, size=%d, RP=%u\n", cbCopy, dev->uRCVBufPtr);
#endif

        /*
         * If one of the "interesting" conditions was hit, stop receiving until
         * the status register is read (mark it not stale).
         * NB: The precise receive logic is not very well described in the EtherLink
         * documentation. It was refined using the 3C501.EXE diagnostic utility.
         */
        if ((rcvstatnew.RcvStatNew.good && dev->RcvCmd.acpt_good)
            || (rcvstatnew.RcvStatNew.no_ovf && dev->RcvCmd.det_eof)
            || (rcvstatnew.RcvStatNew.runt && dev->RcvCmd.det_runt)
            || (rcvstatnew.RcvStatNew.dribble && dev->RcvCmd.det_drbl)
            || (rcvstatnew.RcvStatNew.fcs && dev->RcvCmd.det_fcs)
            || (rcvstatnew.RcvStatNew.oflow && dev->RcvCmd.det_ofl)) {
            dev->AuxStat.recv_bsy       = 0;
            dev->IntrState.recv_intr    = 1;
            rcvstatnew.RcvStatNew.stale = 0; /* Prevents further receive until set again. */
        }

        /* Finally update the receive status. */
        dev->RcvStat = rcvstatnew.RcvStatNew;

#ifdef ENABLE_3COM501_LOG
        threec501_log("3Com501: RcvCmd=%02X, RcvStat=%02X, RCVBufPtr=%u\n", dev->RcvCmdReg, dev->RcvStatReg, dev->uRCVBufPtr);
#endif

        elnkUpdateIrq(dev);
    }

    return 1;
}

/**
 * Actually try transmit frames.
 *
 * @threads TX or EMT.
 */
static void
elnkAsyncTransmit(threec501_t *dev)
{
    /*
     * Just drop it if not transmitting. Can happen with delayed transmits
     * if transmit was disabled in the meantime.
     */
    if (!dev->AuxStat.xmit_bsy) {
#ifdef ENABLE_3COM501_LOG
        threec501_log("3Com501: Nope, xmit disabled\n");
#endif
        return;
    }

    if ((dev->AuxCmd.buf_ctl != EL_BCTL_XMT_RCV) && (dev->AuxCmd.buf_ctl != EL_BCTL_LOOPBACK)) {
#ifdef ENABLE_3COM501_LOG
        threec501_log("3Com501: Nope, not in xmit-then-receive or loopback state\n");
#endif
        return;
    }

    /*
     * Blast out data from the packet buffer.
     */
    do {
        /* Don't send anything when the link is down. */
        if (!elnkIsLinkUp(dev)
             && dev->cLinkDownReported > ELNK_MAX_LINKDOWN_REPORTED)
            break;

        bool const fLoopback = dev->AuxCmd.buf_ctl == EL_BCTL_LOOPBACK;

        /*
         * Sending is easy peasy, there is by definition always
         * a complete packet on hand.
         */
        const unsigned cb = ELNK_BUF_SIZE - ELNK_GP(dev); /* Packet size. */
#ifdef ENABLE_3COM501_LOG
        threec501_log("3Com501: cb=%d, loopback=%d.\n", cb, fLoopback);
#endif

        dev->XmitStatReg = 0; /* Clear transmit status before filling it out. */

        if (elnkIsLinkUp(dev) || fLoopback) {
            if (cb <= MAX_FRAME) {
                if (fLoopback) {
                    elnkReceiveLocked(dev, &dev->abPacketBuf[ELNK_GP(dev)], cb);
                } else {
#ifdef ENABLE_3COM501_LOG
                    threec501_log("3Com501: elnkAsyncTransmit: transmit loopbuf xmit pos = %d\n", cb);
#endif
                    network_tx(dev->netcard, &dev->abPacketBuf[ELNK_GP(dev)], cb);
                }
                dev->XmitStat.ready = 1;
            } else {
                /* Signal error, as this violates the Ethernet specs. */
                /** @todo check if the correct error is generated. */
#ifdef ENABLE_3COM501_LOG
                threec501_log("3Com501: illegal giant frame (%u bytes) -> signalling error\n", cb);
#endif
            }
        } else {
            /* Signal a transmit error pretending there was a collision. */
            dev->cLinkDownReported++;
            dev->XmitStat.coll = 1;
        }

        /* Transmit officially done, update register state. */
        dev->AuxStat.xmit_bsy    = 0;
        dev->IntrState.xmit_intr = !!(dev->XmitCmdReg & dev->XmitStatReg);
#ifdef ENABLE_3COM501_LOG
        threec501_log("3Com501: XmitCmd=%02X, XmitStat=%02X\n", dev->XmitCmdReg, dev->XmitStatReg);
#endif

        /* NB: After a transmit, the GP Buffer Pointer points just past
         * the end of the packet buffer (3C501 diagnostics).
         */
        dev->uGPBufPtr = ELNK_BUF_SIZE;

        /* NB: The buffer control does *not* change to Receive and stays the way it was. */
        if (!fLoopback) {
            dev->AuxStat.recv_bsy = 1; /* Receive Busy now set until a packet is received. */
        }
    } while (0); /* No loop, because there isn't ever more than one packet to transmit. */

    elnkUpdateIrq(dev);
}

static void
elnkCsrWrite(threec501_t *dev, uint8_t data)
{
    bool fTransmit = false;
    bool fReceive  = false;
    bool fDMAR;
    int  mode;

    union {
        uint8_t    reg;
        EL_AUX_CMD val;
    } auxcmd;

    auxcmd.reg = data;

    /* Handle reset first. */
    if (dev->AuxCmd.reset != auxcmd.val.reset) {
        if (auxcmd.val.reset) {
            /* Card is placed into reset. Just set the flag. NB: When in reset
             * state, we permit writes to other registers, but those have no
             * effect and will be overwritten when the card is taken out of reset.
             */
#ifdef ENABLE_3COM501_LOG
            threec501_log("3Com501: Card going into reset\n");
#endif
            dev->fInReset = true;

            /* Many EtherLink drivers like to reset the card a lot. That can lead to
             * packet loss if a packet was already received before the card was reset.
             */
        } else {
            /* Card is being taken out of reset. */
#ifdef ENABLE_3COM501_LOG
            threec501_log("3Com501: Card going out of reset\n");
#endif
            elnkSoftReset(dev);
        }
        dev->AuxCmd.reset = auxcmd.val.reset; /* Update the reset bit, if nothing else. */
    }

    /* If the card is in reset, stop right here. */
    if (dev->fInReset) {
#ifdef ENABLE_3COM501_LOG
        threec501_log("3Com501: Reset\n");
#endif
        return;
    }

    /* Evaluate DMA state. If it changed, we'll have to go back to R3. */
    fDMAR = auxcmd.val.dma_req && auxcmd.val.ride;
    if (fDMAR != dev->fDMA) {
        /* Start/stop DMA as requested. */
        dev->fDMA = fDMAR;
        if (fDMAR) {
            dma_set_drq(dev->dma_channel, fDMAR);
            mode = dma_mode(dev->dma_channel);
#ifdef ENABLE_3COM501_LOG
            threec501_log("3Com501: DMA Mode = %02x.\n", mode & 0x0c);
#endif
            if ((mode & 0x0c) == 0x04) {
                while (dev->dma_pos < (ELNK_BUF_SIZE - ELNK_GP(dev))) {
                    dma_channel_write(dev->dma_channel, dev->abPacketBuf[ELNK_GP(dev) + dev->dma_pos]);
                    dev->dma_pos++;
                }
            } else {
                while (dev->dma_pos < (ELNK_BUF_SIZE - ELNK_GP(dev))) {
                    int dma_data                                  = dma_channel_read(dev->dma_channel);
                    dev->abPacketBuf[ELNK_GP(dev) + dev->dma_pos] = dma_data & 0xff;
                    dev->dma_pos++;
                }
            }
            dev->uGPBufPtr = (dev->uGPBufPtr + dev->dma_pos) & ELNK_GP_MASK;
            dma_set_drq(dev->dma_channel, 0);
            dev->dma_pos            = 0;
            dev->IntrState.dma_intr = 1;
            dev->AuxStat.dma_done   = 1;
            elnkUpdateIrq(dev);
        }
#ifdef ENABLE_3COM501_LOG
        threec501_log("3Com501: DMARQ for channel %u set to %u\n", dev->dma_channel, fDMAR);
#endif
    }

    /* Interrupt enable changes. */
    if ((dev->AuxCmd.ire != auxcmd.val.ire) || (dev->AuxCmd.ride != auxcmd.val.ride)) {
        dev->AuxStat.ride = dev->AuxCmd.ride = auxcmd.val.ride;
        dev->AuxCmd.ire                      = auxcmd.val.ire; /* NB: IRE is not visible in the aux status register. */
    }

    /* DMA Request changes. */
    if (dev->AuxCmd.dma_req != auxcmd.val.dma_req) {
        dev->AuxStat.dma_req = dev->AuxCmd.dma_req = auxcmd.val.dma_req;
        if (!auxcmd.val.dma_req) {
            /* Clearing the DMA Request bit also clears the DMA Done status bit and any DMA interrupt. */
            dev->IntrState.dma_intr = 0;
            dev->AuxStat.dma_done   = 0;
        }
    }

    /* Packet buffer control changes. */
    if (dev->AuxCmd.buf_ctl != auxcmd.val.buf_ctl) {
#ifdef ENABLE_3COM501_LOG
        static const char *apszBuffCntrl[4] = { "System", "Xmit then Recv", "Receive", "Loopback" };
        threec501_log("3Com501: Packet buffer control `%s' -> `%s'\n", apszBuffCntrl[dev->AuxCmd.buf_ctl], apszBuffCntrl[auxcmd.val.buf_ctl]);
#endif
        if (auxcmd.val.buf_ctl == EL_BCTL_XMT_RCV) {
            /* Transmit, then receive. */
            fTransmit             = true;
            dev->AuxStat.recv_bsy = 0;
        } else if (auxcmd.val.buf_ctl == EL_BCTL_SYSTEM) {
            dev->AuxStat.xmit_bsy = 1; /* Transmit Busy is set here and cleared once actual transmit completes. */
            dev->AuxStat.recv_bsy = 0;
        } else if (auxcmd.val.buf_ctl == EL_BCTL_RECEIVE) {
            /* Special case: If going from xmit-then-receive mode to receive mode, and we received
             * a packet already (right after the receive), don't restart receive and lose the already
             * received packet.
             */
            if (!dev->uRCVBufPtr)
                fReceive = true;
        } else {
            /* For loopback, we go through the regular transmit and receive path. That may be an
             * overkill but the receive path is too complex for a special loopback-only case.
             */
            fTransmit             = true;
            dev->AuxStat.recv_bsy = 1; /* Receive Busy now set until a packet is received. */
        }
        dev->AuxStat.buf_ctl = dev->AuxCmd.buf_ctl = auxcmd.val.buf_ctl;
    }

    /* NB: Bit 1 (xmit_bf, transmit packets with bad FCS) is a simple control
     * bit which does not require special handling here. Just copy it over.
     */
    dev->AuxStat.xmit_bf = dev->AuxCmd.xmit_bf = auxcmd.val.xmit_bf;

    /* There are multiple bits that affect interrupt state. Handle them now. */
    elnkUpdateIrq(dev);

    /* After fully updating register state, do a transmit (including loopback) or receive. */
    if (fTransmit)
        elnkAsyncTransmit(dev);
    else if (fReceive) {
        dev->AuxStat.recv_bsy = 1; /* Receive Busy now set until a packet is received. */
    }
}

static uint8_t
threec501_read(uint16_t addr, void *priv)
{
    threec501_t *dev    = (threec501_t *) priv;
    uint8_t      retval = 0xff;

    switch (addr & 0x0f) {
        case 0x00: /* Receive status register aliases.  The SEEQ 8001 */
        case 0x02: /* EDLC clearly only decodes one bit for reads.    */
        case 0x04:
        case 0x06: /* Receive status register. */
            retval                   = dev->RcvStatReg;
            dev->RcvStat.stale       = 1; /* Allows further reception. */
            dev->IntrState.recv_intr = 0; /* Reading clears receive interrupt. */
            elnkUpdateIrq(dev);
            break;

        case 0x01: /* Transmit status register aliases. */
        case 0x03:
        case 0x05:
        case 0x07: /* Transmit status register. */
            retval                   = dev->XmitStatReg;
            dev->IntrState.xmit_intr = 0; /* Reading clears transmit interrupt. */
            elnkUpdateIrq(dev);
            break;

        case 0x08: /* GP Buffer pointer LSB. */
            retval = (dev->uGPBufPtr & 0xff);
            break;
        case 0x09: /* GP Buffer pointer MSB. */
            retval = (dev->uGPBufPtr >> 8);
            break;

        case 0x0a: /* RCV Buffer pointer LSB. */
            retval = (dev->uRCVBufPtr & 0xff);
            break;
        case 0x0b: /* RCV Buffer pointer MSB. */
            retval = (dev->uRCVBufPtr >> 8);
            break;

        case 0x0c: /* Ethernet address PROM window. */
        case 0x0d: /* Alias. */
            /* Reads use low 3 bits of GP buffer pointer, no auto-increment. */
            retval = dev->aPROM[dev->uGPBufPtr & 7];
            break;

        case 0x0e: /* Auxiliary status register. */
            retval = dev->AuxStatReg;
            break;

        case 0x0f: /* Buffer window. */
            /* Reads use low 11 bits of GP buffer pointer, auto-increment. */
            retval         = dev->abPacketBuf[ELNK_GP(dev)];
            dev->uGPBufPtr = (dev->uGPBufPtr + 1) & ELNK_GP_MASK;
            break;

        default:
            break;
    }

    elnkUpdateIrq(dev);
#ifdef ENABLE_3COM501_LOG
    threec501_log("3Com501: read addr %x, value %x\n", addr & 0x0f, retval);
#endif
    return retval;
}

static uint8_t
threec501_nic_readb(uint16_t addr, void *priv)
{
    return threec501_read(addr, priv);
}

static uint16_t
threec501_nic_readw(uint16_t addr, void *priv)
{
    return threec501_read(addr, priv) | (threec501_read(addr + 1, priv) << 8);
}

static void
threec501_write(uint16_t addr, uint8_t value, void *priv)
{
    threec501_t *dev = (threec501_t *) priv;
    int          reg = (addr & 0x0f);

    switch (reg) {
        case 0x00: /* Six bytes of station address. */
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
            dev->aStationAddr[reg] = value;
            break;

        case 0x06: /* Receive command. */
            dev->RcvCmdReg = value;
#ifdef ENABLE_3COM501_LOG
            threec501_log("3Com501: Receive Command register set to %02X\n", dev->RcvCmdReg);
#endif
            break;

        case 0x07: /* Transmit command. */
            dev->XmitCmdReg = value;
#ifdef ENABLE_3COM501_LOG
            threec501_log("3Com501: Transmit Command register set to %02X\n", dev->XmitCmdReg);
#endif
            break;

        case 0x08: /* GP Buffer pointer LSB. */
            dev->uGPBufPtr = (dev->uGPBufPtr & 0xff00) | value;
            break;
        case 0x09: /* GP Buffer pointer MSB. */
            dev->uGPBufPtr = (dev->uGPBufPtr & 0x00ff) | (value << 8);
            break;

        case 0x0a: /* RCV Buffer pointer clear. */
            dev->uRCVBufPtr = 0;
#ifdef ENABLE_3COM501_LOG
            threec501_log("3Com501: RCV Buffer Pointer cleared (%02X)\n", value);
#endif
            break;

        case 0x0b: /* RCV buffer pointer MSB. */
        case 0x0c: /* Ethernet address PROM window. */
        case 0x0d: /* Undocumented. */
#ifdef ENABLE_3COM501_LOG
            threec501_log("3Com501: Writing read-only register %02X!\n", reg);
#endif
            break;

        case 0x0e: /* Auxiliary Command (CSR). */
            elnkCsrWrite(dev, value);
            break;

        case 0x0f: /* Buffer window. */
            /* Writes use low 11 bits of GP buffer pointer, auto-increment. */
            if (dev->AuxCmd.buf_ctl != EL_BCTL_SYSTEM) {
                /// @todo Does this still increment GPBufPtr?
                break;
            }
            dev->abPacketBuf[ELNK_GP(dev)] = value;
            dev->uGPBufPtr                 = (dev->uGPBufPtr + 1) & ELNK_GP_MASK;
            break;

        default:
            break;
    }

#ifdef ENABLE_3COM501_LOG
    threec501_log("3Com501: write addr %x, value %x\n", reg, value);
#endif
}

static void
threec501_nic_writeb(uint16_t addr, uint8_t value, void *priv)
{
    threec501_write(addr, value, priv);
}

static void
threec501_nic_writew(uint16_t addr, uint16_t value, void *priv)
{
    threec501_write(addr, value & 0xff, priv);
    threec501_write(addr + 1, value >> 8, priv);
}

static int
elnkSetLinkState(void *priv, uint32_t link_state)
{
    threec501_t *dev = (threec501_t *) priv;

    if (link_state & NET_LINK_TEMP_DOWN) {
        elnkTempLinkDown(dev);
        return 1;
    }

    bool link_up = !(link_state & NET_LINK_DOWN);
    if (dev->fLinkUp != link_up) {
        dev->fLinkUp = link_up;
        if (link_up) {
            dev->fLinkTempDown         = 1;
            dev->cLinkDownReported     = 0;
            dev->cLinkRestorePostponed = 0;
            timer_set_delay_u64(&dev->timer_restore, (dev->cMsLinkUpDelay * 1000) * TIMER_USEC);
        } else {
            dev->cLinkDownReported     = 0;
            dev->cLinkRestorePostponed = 0;
        }
    }

    return 0;
}

static void
elnkR3TimerRestore(void *priv)
{
    threec501_t *dev = (threec501_t *) priv;

    if ((dev->cLinkDownReported <= ELNK_MAX_LINKDOWN_REPORTED) && (dev->cLinkRestorePostponed <= ELNK_MAX_LINKRST_POSTPONED)) {
        timer_advance_u64(&dev->timer_restore, 1500000 * TIMER_USEC);
        dev->cLinkRestorePostponed++;
    } else {
        dev->fLinkTempDown = 0;
    }
}

static void *
threec501_nic_init(UNUSED(const device_t *info))
{
    uint32_t     mac;
    threec501_t *dev;

    dev = calloc(1, sizeof(threec501_t));
    dev->maclocal[0] = 0x02; /* 02:60:8C (3Com OID) */
    dev->maclocal[1] = 0x60;
    dev->maclocal[2] = 0x8C;

    dev->base_address = device_get_config_hex16("base");
    dev->base_irq     = device_get_config_int("irq");
    dev->dma_channel  = device_get_config_int("dma");

    dev->fLinkUp        = 1;
    dev->cMsLinkUpDelay = 5000;

    /* See if we have a local MAC address configured. */
    mac = device_get_config_mac("mac", -1);

    /*
     * Make this device known to the I/O system.
     * PnP and PCI devices start with address spaces inactive.
     */
    io_sethandler(dev->base_address, 0x10,
                  threec501_nic_readb, threec501_nic_readw, NULL,
                  threec501_nic_writeb, threec501_nic_writew, NULL, dev);

    /* Set up our BIA. */
    if (mac & 0xff000000) {
        /* Generate new local MAC. */
        dev->maclocal[3] = random_generate();
        dev->maclocal[4] = random_generate();
        dev->maclocal[5] = random_generate();
        mac              = (((int) dev->maclocal[3]) << 16);
        mac |= (((int) dev->maclocal[4]) << 8);
        mac |= ((int) dev->maclocal[5]);
        device_set_config_mac("mac", mac);
    } else {
        dev->maclocal[3] = (mac >> 16) & 0xff;
        dev->maclocal[4] = (mac >> 8) & 0xff;
        dev->maclocal[5] = (mac & 0xff);
    }

    /* Initialize the PROM */
    memcpy(dev->aPROM, dev->maclocal, sizeof(dev->maclocal));
    dev->aPROM[6] = dev->aPROM[7] = 0; /* The two padding bytes. */

#ifdef ENABLE_3COM501_LOG
    threec501_log("I/O=%04x, IRQ=%d, DMA=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
                  dev->base_address, dev->base_irq, dev->dma_channel,
                  dev->aPROM[0], dev->aPROM[1], dev->aPROM[2],
                  dev->aPROM[3], dev->aPROM[4], dev->aPROM[5]);
#endif

    /* Reset the board. */
    elnkR3HardReset(dev);

    /* Attach ourselves to the network module. */
    dev->netcard = network_attach(dev, dev->aPROM, elnkReceiveLocked, elnkSetLinkState);

    timer_add(&dev->timer_restore, elnkR3TimerRestore, dev, 0);

    return dev;
}

static void
threec501_nic_close(void *priv)
{
    threec501_t *dev = (threec501_t *) priv;

#ifdef ENABLE_3COM501_LOG
    threec501_log("3Com501: closed\n");
#endif

    free(dev);
}

static const device_config_t threec501_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x300,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x280", .value = 0x280 },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x310", .value = 0x310 },
            { .description = "0x320", .value = 0x320 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 2/9", .value = 9 },
            { .description = "IRQ 3", .value = 3 },
            { .description = "IRQ 4", .value = 4 },
            { .description = "IRQ 5", .value = 5 },
            { .description = "IRQ 6", .value = 6 },
            { .description = "IRQ 7", .value = 7 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name = "dma",
        .description = "DMA",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "DMA 1", .value = 1 },
            { .description = "DMA 2", .value = 2 },
            { .description = "DMA 3", .value = 3 },
            { .description = ""                  }
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

const device_t threec501_device = {
    .name          = "3Com EtherLink (3c500/3c501)",
    .internal_name = "3c501",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = threec501_nic_init,
    .close         = threec501_nic_close,
    .reset         = elnkR3Reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = threec501_config
};
