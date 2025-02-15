/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of Realtek RTL8139C+ NIC.
 *
 * Authors: Igor Kovalenko,
 *          Mark Malakanov,
 *          Jurgen Lock,
 *          Frediano Ziglio,
 *          Benjamin Poirier.
 *          Cacodemon345,
 *
 *          Copyright 2006-2023 Igor Kovalenko.
 *          Copyright 2006-2023 Mark Malakanov.
 *          Copyright 2006-2023 Jurgen Lock.
 *          Copyright 2010-2023 Frediano Ziglio.
 *          Copyright 2011-2023 Benjamin Poirier.
 *          Copyright 2023 Cacodemon345.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#ifdef _MVC_VER
#include <stddef.h>
#endif
#include <time.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/random.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/dma.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/net_eeprom_nmc93cxx.h>
#include <86box/nvr.h>
#include "cpu.h"
#include <86box/plat_unused.h>
#include <86box/bswap.h>

#define PCI_PERIOD 30 /* 30 ns period = 33.333333 Mhz frequency */

#define SET_MASKED(input, mask, curr) \
    (((input) & ~(mask)) | ((curr) & (mask)))

/* arg % size for size which is a power of 2 */
#define MOD2(input, size) \
    ((input) & (size - 1))

#define ETHER_TYPE_LEN 2

#define VLAN_TCI_LEN   2
#define VLAN_HLEN      (ETHER_TYPE_LEN + VLAN_TCI_LEN)

#ifdef ENABLE_RTL8139_LOG
int rtl8139_do_log = ENABLE_RTL8139_LOG;

static void
rtl8139_log(const char *fmt, ...)
{
    va_list ap;

    if (rtl8139_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define rtl8139_log(fmt, ...)
#endif

struct RTL8139State;
typedef struct RTL8139State RTL8139State;

/* Symbolic offsets to registers. */
enum RTL8139_registers {
    MAC0      = 0,          /* Ethernet hardware address. */
    MAR0      = 8,          /* Multicast filter. */
    TxStatus0 = 0x10,       /* Transmit status (Four 32bit registers). C mode only */
                            /* Dump Tally Conter control register(64bit). C+ mode only */
    TxAddr0         = 0x20, /* Tx descriptors (also four 32bit). */
    RxBuf           = 0x30,
    ChipCmd         = 0x37,
    RxBufPtr        = 0x38,
    RxBufAddr       = 0x3A,
    IntrMask        = 0x3C,
    IntrStatus      = 0x3E,
    TxConfig        = 0x40,
    RxConfig        = 0x44,
    Timer           = 0x48, /* A general-purpose counter. */
    RxMissed        = 0x4C, /* 24 bits valid, write clears. */
    Cfg9346         = 0x50,
    Config0         = 0x51,
    Config1         = 0x52,
    FlashReg        = 0x54,
    MediaStatus     = 0x58,
    Config3         = 0x59,
    Config4         = 0x5A, /* absent on RTL-8139A */
    HltClk          = 0x5B,
    MultiIntr       = 0x5C,
    PCIRevisionID   = 0x5E,
    TxSummary       = 0x60, /* TSAD register. Transmit Status of All Descriptors*/
    BasicModeCtrl   = 0x62,
    BasicModeStatus = 0x64,
    NWayAdvert      = 0x66,
    NWayLPAR        = 0x68,
    NWayExpansion   = 0x6A,
    /* Undocumented registers, but required for proper operation. */
    FIFOTMS = 0x70, /* FIFO Control and test. */
    CSCR    = 0x74, /* Chip Status and Configuration Register. */
    PARA78  = 0x78,
    PARA7c  = 0x7c, /* Magic transceiver parameter register. */
    Config5 = 0xD8, /* absent on RTL-8139A */
    /* C+ mode */
    TxPoll       = 0xD9, /* Tell chip to check Tx descriptors for work */
    RxMaxSize    = 0xDA, /* Max size of an Rx packet (8169 only) */
    CpCmd        = 0xE0, /* C+ Command register (C+ mode only) */
    IntrMitigate = 0xE2, /* rx/tx interrupt mitigation control */
    RxRingAddrLO = 0xE4, /* 64-bit start addr of Rx ring */
    RxRingAddrHI = 0xE8, /* 64-bit start addr of Rx ring */
    TxThresh     = 0xEC, /* Early Tx threshold */
};

enum ClearBitMasks {
    MultiIntrClear = 0xF000,
    ChipCmdClear   = 0xE2,
    Config1Clear   = (1 << 7) | (1 << 6) | (1 << 3) | (1 << 2) | (1 << 1),
};

enum ChipCmdBits {
    CmdReset   = 0x10,
    CmdRxEnb   = 0x08,
    CmdTxEnb   = 0x04,
    RxBufEmpty = 0x01,
};

/* C+ mode */
enum CplusCmdBits {
    CPlusRxVLAN   = 0x0040, /* enable receive VLAN detagging */
    CPlusRxChkSum = 0x0020, /* enable receive checksum offloading */
    CPlusRxEnb    = 0x0002,
    CPlusTxEnb    = 0x0001,
};

/* Interrupt register bits, using my own meaningful names. */
enum IntrStatusBits {
    PCIErr     = 0x8000,
    PCSTimeout = 0x4000,
    RxFIFOOver = 0x40,
    RxUnderrun = 0x20, /* Packet Underrun / Link Change */
    RxOverflow = 0x10,
    TxErr      = 0x08,
    TxOK       = 0x04,
    RxErr      = 0x02,
    RxOK       = 0x01,

    RxAckBits = RxFIFOOver | RxOverflow | RxOK,
};

enum TxStatusBits {
    TxHostOwns    = 0x2000,
    TxUnderrun    = 0x4000,
    TxStatOK      = 0x8000,
    TxOutOfWindow = 0x20000000,
    TxAborted     = 0x40000000,
    TxCarrierLost = 0x80000000,
};
enum RxStatusBits {
    RxMulticast = 0x8000,
    RxPhysical  = 0x4000,
    RxBroadcast = 0x2000,
    RxBadSymbol = 0x0020,
    RxRunt      = 0x0010,
    RxTooLong   = 0x0008,
    RxCRCErr    = 0x0004,
    RxBadAlign  = 0x0002,
    RxStatusOK  = 0x0001,
};

/* Bits in RxConfig. */
enum rx_mode_bits {
    AcceptErr       = 0x20,
    AcceptRunt      = 0x10,
    AcceptBroadcast = 0x08,
    AcceptMulticast = 0x04,
    AcceptMyPhys    = 0x02,
    AcceptAllPhys   = 0x01,
};

/* Bits in TxConfig. */
enum tx_config_bits {

    /* Interframe Gap Time. Only TxIFG96 doesn't violate IEEE 802.3 */
    TxIFGShift = 24,
    TxIFG84    = (0 << TxIFGShift), /* 8.4us / 840ns (10 / 100Mbps) */
    TxIFG88    = (1 << TxIFGShift), /* 8.8us / 880ns (10 / 100Mbps) */
    TxIFG92    = (2 << TxIFGShift), /* 9.2us / 920ns (10 / 100Mbps) */
    TxIFG96    = (3 << TxIFGShift), /* 9.6us / 960ns (10 / 100Mbps) */

    TxLoopBack   = (1 << 18) | (1 << 17), /* enable loopback test mode */
    TxCRC        = (1 << 16),             /* DISABLE appending CRC to end of Tx packets */
    TxClearAbt   = (1 << 0),              /* Clear abort (WO) */
    TxDMAShift   = 8,                     /* DMA burst value (0-7) is shifted this many bits */
    TxRetryShift = 4,                     /* TXRR value (0-15) is shifted this many bits */

    TxVersionMask = 0x7C800000, /* mask out version bits 30-26, 23 */
};

/* Transmit Status of All Descriptors (TSAD) Register */
enum TSAD_bits {
    TSAD_TOK3  = 1 << 15, // TOK bit of Descriptor 3
    TSAD_TOK2  = 1 << 14, // TOK bit of Descriptor 2
    TSAD_TOK1  = 1 << 13, // TOK bit of Descriptor 1
    TSAD_TOK0  = 1 << 12, // TOK bit of Descriptor 0
    TSAD_TUN3  = 1 << 11, // TUN bit of Descriptor 3
    TSAD_TUN2  = 1 << 10, // TUN bit of Descriptor 2
    TSAD_TUN1  = 1 << 9,  // TUN bit of Descriptor 1
    TSAD_TUN0  = 1 << 8,  // TUN bit of Descriptor 0
    TSAD_TABT3 = 1 << 07, // TABT bit of Descriptor 3
    TSAD_TABT2 = 1 << 06, // TABT bit of Descriptor 2
    TSAD_TABT1 = 1 << 05, // TABT bit of Descriptor 1
    TSAD_TABT0 = 1 << 04, // TABT bit of Descriptor 0
    TSAD_OWN3  = 1 << 03, // OWN bit of Descriptor 3
    TSAD_OWN2  = 1 << 02, // OWN bit of Descriptor 2
    TSAD_OWN1  = 1 << 01, // OWN bit of Descriptor 1
    TSAD_OWN0  = 1 << 00, // OWN bit of Descriptor 0
};

/* Bits in Config1 */
enum Config1Bits {
    Cfg1_PM_Enable   = 0x01,
    Cfg1_VPD_Enable  = 0x02,
    Cfg1_PIO         = 0x04,
    Cfg1_MMIO        = 0x08,
    LWAKE            = 0x10, /* not on 8139, 8139A */
    Cfg1_Driver_Load = 0x20,
    Cfg1_LED0        = 0x40,
    Cfg1_LED1        = 0x80,
    SLEEP            = (1 << 1), /* only on 8139, 8139A */
    PWRDN            = (1 << 0), /* only on 8139, 8139A */
};

/* Bits in Config3 */
enum Config3Bits {
    Cfg3_FBtBEn    = (1 << 0), /* 1 = Fast Back to Back */
    Cfg3_FuncRegEn = (1 << 1), /* 1 = enable CardBus Function registers */
    Cfg3_CLKRUN_En = (1 << 2), /* 1 = enable CLKRUN */
    Cfg3_CardB_En  = (1 << 3), /* 1 = enable CardBus registers */
    Cfg3_LinkUp    = (1 << 4), /* 1 = wake up on link up */
    Cfg3_Magic     = (1 << 5), /* 1 = wake up on Magic Packet (tm) */
    Cfg3_PARM_En   = (1 << 6), /* 0 = software can set twister parameters */
    Cfg3_GNTSel    = (1 << 7), /* 1 = delay 1 clock from PCI GNT signal */
};

/* Bits in Config4 */
enum Config4Bits {
    LWPTN = (1 << 2), /* not on 8139, 8139A */
};

/* Bits in Config5 */
enum Config5Bits {
    Cfg5_PME_STS     = (1 << 0), /* 1 = PCI reset resets PME_Status */
    Cfg5_LANWake     = (1 << 1), /* 1 = enable LANWake signal */
    Cfg5_LDPS        = (1 << 2), /* 0 = save power when link is down */
    Cfg5_FIFOAddrPtr = (1 << 3), /* Realtek internal SRAM testing */
    Cfg5_UWF         = (1 << 4), /* 1 = accept unicast wakeup frame */
    Cfg5_MWF         = (1 << 5), /* 1 = accept multicast wakeup frame */
    Cfg5_BWF         = (1 << 6), /* 1 = accept broadcast wakeup frame */
};

enum RxConfigBits {
    /* rx fifo threshold */
    RxCfgFIFOShift = 13,
    RxCfgFIFONone  = (7 << RxCfgFIFOShift),

    /* Max DMA burst */
    RxCfgDMAShift     = 8,
    RxCfgDMAUnlimited = (7 << RxCfgDMAShift),

    /* rx ring buffer length */
    RxCfgRcv8K  = 0,
    RxCfgRcv16K = (1 << 11),
    RxCfgRcv32K = (1 << 12),
    RxCfgRcv64K = (1 << 11) | (1 << 12),

    /* Disable packet wrap at end of Rx buffer. (not possible with 64k) */
    RxNoWrap = (1 << 7),
};

/* Twister tuning parameters from RealTek.
   Completely undocumented, but required to tune bad links on some boards. */
#if 0
enum CSCRBits {
    CSCR_LinkOKBit = 0x0400,
    CSCR_LinkChangeBit = 0x0800,
    CSCR_LinkStatusBits = 0x0f000,
    CSCR_LinkDownOffCmd = 0x003c0,
    CSCR_LinkDownCmd = 0x0f3c0,
#endif
enum CSCRBits {
    CSCR_Testfun       = 1 << 15, /* 1 = Auto-neg speeds up internal timer, WO, def 0 */
    CSCR_Cable_Changed = 1 << 11, /* Undocumented: 1 = Cable status changed, 0 = No change */
    CSCR_Cable         = 1 << 10, /* Undocumented: 1 = Cable connected, 0 = Cable disconnected */
    CSCR_LD            = 1 << 9,  /* Active low TPI link disable signal. When low, TPI still transmits link pulses and TPI stays in good link state. def 1*/
    CSCR_HEART_BIT     = 1 << 8,  /* 1 = HEART BEAT enable, 0 = HEART BEAT disable. HEART BEAT function is only valid in 10Mbps mode. def 1*/
    CSCR_JBEN          = 1 << 7,  /* 1 = enable jabber function. 0 = disable jabber function, def 1*/
    CSCR_F_LINK_100    = 1 << 6,  /* Used to login force good link in 100Mbps for diagnostic purposes. 1 = DISABLE, 0 = ENABLE. def 1*/
    CSCR_F_Connect     = 1 << 5,  /* Assertion of this bit forces the disconnect function to be bypassed. def 0*/
    CSCR_Con_status    = 1 << 3,  /* This bit indicates the status of the connection. 1 = valid connected link detected; 0 = disconnected link detected. RO def 0*/
    CSCR_Con_status_En = 1 << 2,  /* Assertion of this bit configures LED1 pin to indicate connection status. def 0*/
    CSCR_PASS_SCR      = 1 << 0,  /* Bypass Scramble, def 0*/
};

enum Cfg9346Bits {
    Cfg9346_Normal      = 0x00,
    Cfg9346_Autoload    = 0x40,
    Cfg9346_Programming = 0x80,
    Cfg9346_ConfigWrite = 0xC0,
};

typedef enum {
    CH_8139 = 0,
    CH_8139_K,
    CH_8139A,
    CH_8139A_G,
    CH_8139B,
    CH_8130,
    CH_8139C,
    CH_8100,
    CH_8100B_8139D,
    CH_8101,
} chip_t;

enum chip_flags {
    HasHltClk = (1 << 0),
    HasLWake  = (1 << 1),
};

#define HW_REVID(b30, b29, b28, b27, b26, b23, b22) \
    (b30 << 30 | b29 << 29 | b28 << 28 | b27 << 27 | b26 << 26 | b23 << 23 | b22 << 22)
#define HW_REVID_MASK               HW_REVID(1, 1, 1, 1, 1, 1, 1)

#define RTL8139_PCI_REVID_8139      0x10
#define RTL8139_PCI_REVID_8139CPLUS 0x20

/* Return 0x10 - the RTL8139C+ datasheet and Windows 2000 driver both confirm this. */
#define RTL8139_PCI_REVID           RTL8139_PCI_REVID_8139

#pragma pack(push, 1)
typedef struct RTL8139TallyCounters {
    /* Tally counters */
    uint64_t TxOk;
    uint64_t RxOk;
    uint64_t TxERR;
    uint32_t RxERR;
    uint16_t MissPkt;
    uint16_t FAE;
    uint32_t Tx1Col;
    uint32_t TxMCol;
    uint64_t RxOkPhy;
    uint64_t RxOkBrd;
    uint32_t RxOkMul;
    uint16_t TxAbt;
    uint16_t TxUndrn;
} RTL8139TallyCounters;
#pragma pack(pop)

/* Clears all tally counters */
static void RTL8139TallyCounters_clear(RTL8139TallyCounters *counters);

struct RTL8139State {
    /*< private >*/
    uint8_t pci_slot;
    uint8_t inst;
    uint8_t irq_state;
    uint8_t pad;
    /*< public >*/

    uint8_t phys[8]; /* mac address */
    uint8_t mult[8]; /* multicast mask array */
    uint8_t pci_conf[256];

    uint32_t TxStatus[4]; /* TxStatus0 in C mode*/ /* also DTCCR[0] and DTCCR[1] in C+ mode */
    uint32_t TxAddr[4];                            /* TxAddr0 */
    uint32_t RxBuf;                                /* Receive buffer */
    uint32_t RxBufferSize;                         /* internal variable, receive ring buffer size in C mode */
    uint32_t RxBufPtr;
    uint32_t RxBufAddr;

    uint16_t IntrStatus;
    uint16_t IntrMask;

    uint32_t TxConfig;
    uint32_t RxConfig;
    uint32_t RxMissed;

    uint16_t CSCR;

    uint8_t Cfg9346;
    uint8_t Config0;
    uint8_t Config1;
    uint8_t Config3;
    uint8_t Config4;
    uint8_t Config5;

    uint8_t clock_enabled;
    uint8_t bChipCmdState;

    uint16_t MultiIntr;

    uint16_t BasicModeCtrl;
    uint16_t BasicModeStatus;
    uint16_t NWayAdvert;
    uint16_t NWayLPAR;
    uint16_t NWayExpansion;

    uint16_t CpCmd;

    uint8_t TxThresh;
    uint8_t pci_latency;

    netcard_t *nic;

    /* C ring mode */
    uint32_t currTxDesc;

    /* C+ mode */
    uint32_t cplus_enabled;

    uint32_t currCPlusRxDesc;
    uint32_t currCPlusTxDesc;

    uint32_t RxRingAddrLO;
    uint32_t RxRingAddrHI;

    uint32_t TCTR;
    uint32_t TimerInt;
    int64_t  TCTR_base;

    /* Tally counters */
    RTL8139TallyCounters tally_counters;

    /* Non-persistent data */
    uint8_t *cplus_txbuffer;
    int      cplus_txbuffer_len;
    int      cplus_txbuffer_offset;

    uint32_t mem_base;

    /* PCI interrupt timer */
    pc_timer_t timer;

    mem_mapping_t bar_mem;

    /* Support migration to/from old versions */
    int rtl8139_mmio_io_addr_dummy;

    nmc93cxx_eeprom_t *eeprom;
    uint8_t            eeprom_data[128];
};

/* Writes tally counters to memory via DMA */
static void RTL8139TallyCounters_dma_write(RTL8139State *s, uint32_t tc_addr);

static void
rtl8139_update_irq(RTL8139State *s)
{
    uint8_t d = s->pci_slot;
    int     isr;

    isr = (s->IntrStatus & s->IntrMask) & 0xffff;

    rtl8139_log("Set IRQ to %d (%04x %04x)\n", isr ? 1 : 0, s->IntrStatus,
                s->IntrMask);

    if (isr != 0)
        pci_set_irq(d, PCI_INTA, &s->irq_state);
    else
        pci_clear_irq(d, PCI_INTA, &s->irq_state);
}

static int
rtl8139_RxWrap(RTL8139State *s)
{
    /* wrapping enabled; assume 1.5k more buffer space if size < 65536 */
    return (s->RxConfig & (1 << 7));
}

static int
rtl8139_receiver_enabled(RTL8139State *s)
{
    return s->bChipCmdState & CmdRxEnb;
}

static int
rtl8139_transmitter_enabled(RTL8139State *s)
{
    return s->bChipCmdState & CmdTxEnb;
}

static int
rtl8139_cp_receiver_enabled(RTL8139State *s)
{
    return s->CpCmd & CPlusRxEnb;
}

static int
rtl8139_cp_transmitter_enabled(RTL8139State *s)
{
    return s->CpCmd & CPlusTxEnb;
}

static void
rtl8139_write_buffer(RTL8139State *s, const void *buf, int size)
{
    if (s->RxBufAddr + size > s->RxBufferSize) {
        int wrapped = MOD2(s->RxBufAddr + size, s->RxBufferSize);

        /* write packet data */
        if (wrapped && !(s->RxBufferSize < 65536 && rtl8139_RxWrap(s))) {
            rtl8139_log(">>> rx packet wrapped in buffer at %d\n", size - wrapped);

            if (size > wrapped) {
                dma_bm_write(s->RxBuf + s->RxBufAddr,
                             (uint8_t *) buf, size - wrapped, 1);
            }

            /* reset buffer pointer */
            s->RxBufAddr = 0;

            dma_bm_write(s->RxBuf + s->RxBufAddr,
                         (uint8_t *) buf + (size - wrapped), wrapped, 1);

            s->RxBufAddr = wrapped;

            return;
        }
    }

    /* non-wrapping path or overwrapping enabled */
    dma_bm_write(s->RxBuf + s->RxBufAddr, buf, size, 1);

    s->RxBufAddr += size;
}

static __inline uint32_t
rtl8139_addr64(uint32_t low, UNUSED(uint32_t high))
{
    return low;
}

/* Workaround for buggy guest driver such as linux who allocates rx
 * rings after the receiver were enabled. */
static bool
rtl8139_cp_rx_valid(RTL8139State *s)
{
    return !(s->RxRingAddrLO == 0 && s->RxRingAddrHI == 0);
}

static bool
rtl8139_can_receive(RTL8139State *s)
{
    int avail;

    /* Receive (drop) packets if card is disabled.  */
    if (!s->clock_enabled) {
        return true;
    }
    if (!rtl8139_receiver_enabled(s)) {
        return true;
    }

    if (rtl8139_cp_receiver_enabled(s) && rtl8139_cp_rx_valid(s)) {
        /* ??? Flow control not implemented in c+ mode.
           This is a hack to work around slirp deficiencies anyway.  */
        return true;
    }

    avail = MOD2(s->RxBufferSize + s->RxBufPtr - s->RxBufAddr,
                 s->RxBufferSize);
    return avail == 0 || avail >= 1514 || (s->IntrMask & RxOverflow);
}

/* From FreeBSD */
/* XXX: optimize */
static uint32_t
net_crc32(const uint8_t *p, int len)
{
    uint32_t crc;
    int      carry;
    uint8_t  b;

    crc = 0xffffffff;
    for (int i = 0; i < len; i++) {
        b = *p++;
        for (uint8_t j = 0; j < 8; j++) {
            carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
            crc <<= 1;
            b >>= 1;
            if (carry) {
                crc = ((crc ^ 0x04c11db6) | carry);
            }
        }
    }

    return crc;
}

uint32_t
net_crc32_le(const uint8_t *p, int len)
{
    uint32_t crc;
    int      carry;
    uint8_t  b;

    crc = 0xffffffff;
    for (int i = 0; i < len; i++) {
        b = *p++;
        for (uint8_t j = 0; j < 8; j++) {
            carry = (crc & 0x1) ^ (b & 0x01);
            crc >>= 1;
            b >>= 1;
            if (carry) {
                crc ^= 0xedb88320;
            }
        }
    }

    return crc;
}

#define ETH_ALEN 6
static int
rtl8139_do_receive(void *priv, uint8_t *buf, int size_)
{
    RTL8139State *s = (RTL8139State *) priv;
    /* size is the length of the buffer passed to the driver */
    size_t         size      = size_;
    const uint8_t *dot1q_buf = NULL;

    uint32_t packet_header = 0;
    uint8_t  buf1[60 + VLAN_HLEN];

    static const uint8_t broadcast_macaddr[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    rtl8139_log(">>> received len=%u\n", (uint32_t) size);

    if (!rtl8139_can_receive(s))
        return 0;

    /* test if board clock is stopped */
    if (!s->clock_enabled) {
        rtl8139_log("stopped ==========================\n");
        return -1;
    }

    /* first check if receiver is enabled */

    if (!rtl8139_receiver_enabled(s)) {
        rtl8139_log("receiver disabled ================\n");
        return -1;
    }

    /* XXX: check this */
    if (s->RxConfig & AcceptAllPhys) {
        /* promiscuous: receive all */
        rtl8139_log(">>> packet received in promiscuous mode\n");

    } else {
        if (!memcmp(buf, broadcast_macaddr, 6)) {
            /* broadcast address */
            if (!(s->RxConfig & AcceptBroadcast)) {
                rtl8139_log(">>> broadcast packet rejected\n");

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return size;
            }

            packet_header |= RxBroadcast;

            rtl8139_log(">>> broadcast packet received\n");

            /* update tally counter */
            ++s->tally_counters.RxOkBrd;

        } else if (buf[0] & 0x01) {
            /* multicast */
            if (!(s->RxConfig & AcceptMulticast)) {
                rtl8139_log(">>> multicast packet rejected\n");

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return size;
            }

            int mcast_idx = net_crc32(buf, ETH_ALEN) >> 26;

            if (!(s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7)))) {
                rtl8139_log(">>> multicast address mismatch\n");

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return size;
            }

            packet_header |= RxMulticast;

            rtl8139_log(">>> multicast packet received\n");

            /* update tally counter */
            ++s->tally_counters.RxOkMul;

        } else if (s->phys[0] == buf[0] && s->phys[1] == buf[1] && s->phys[2] == buf[2] && s->phys[3] == buf[3] && s->phys[4] == buf[4] && s->phys[5] == buf[5]) {
            /* match */
            if (!(s->RxConfig & AcceptMyPhys)) {
                rtl8139_log(">>> rejecting physical address matching packet\n");

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return size;
            }

            packet_header |= RxPhysical;

            rtl8139_log(">>> physical address matching packet received\n");

            /* update tally counter */
            ++s->tally_counters.RxOkPhy;

        } else {

            rtl8139_log(">>> unknown packet\n");

            /* update tally counter */
            ++s->tally_counters.RxERR;

            return size;
        }
    }

    if (size < 60 + VLAN_HLEN) {
        memcpy(buf1, buf, size);
        memset(buf1 + size, 0, 60 + VLAN_HLEN - size);
        buf = buf1;
        if (size < 60) {
            size = 60;
        }
    }

    if (rtl8139_cp_receiver_enabled(s)) {
        if (!rtl8139_cp_rx_valid(s)) {
            return size;
        }

        rtl8139_log("in C+ Rx mode ================\n");

        /* begin C+ receiver mode */

/* w0 ownership flag */
#define CP_RX_OWN (1 << 31)
/* w0 end of ring flag */
#define CP_RX_EOR (1 << 30)
/* w0 bits 0...12 : buffer size */
#define CP_RX_BUFFER_SIZE_MASK ((1 << 13) - 1)
/* w1 tag available flag */
#define CP_RX_TAVA (1 << 16)
/* w1 bits 0...15 : VLAN tag */
#define CP_RX_VLAN_TAG_MASK ((1 << 16) - 1)
        /* w2 low  32bit of Rx buffer ptr */
        /* w3 high 32bit of Rx buffer ptr */

        int      descriptor = s->currCPlusRxDesc;
        uint32_t cplus_rx_ring_desc;

        cplus_rx_ring_desc = rtl8139_addr64(s->RxRingAddrLO, s->RxRingAddrHI);
        cplus_rx_ring_desc += 16 * descriptor;

        rtl8139_log("+++ C+ mode reading RX descriptor %d from host memory at "
                "%08x %08x = "
                "0x%8X"
                "\n",
                descriptor, s->RxRingAddrHI,
                s->RxRingAddrLO, cplus_rx_ring_desc);

        uint32_t val;
        uint32_t rxdw0;
        uint32_t rxdw1;
        uint32_t rxbufLO;
        uint32_t rxbufHI;

        dma_bm_read(cplus_rx_ring_desc, (uint8_t *) &val, 4, 4);
        rxdw0 = val;
        dma_bm_read(cplus_rx_ring_desc + 4, (uint8_t *) &val, 4, 4);
        rxdw1 = val;
        dma_bm_read(cplus_rx_ring_desc + 8, (uint8_t *) &val, 4, 4);
        rxbufLO = val;
        dma_bm_read(cplus_rx_ring_desc + 12, (uint8_t *) &val, 4, 4);
        rxbufHI = val;

        rtl8139_log("+++ C+ mode RX descriptor %d %08x %08x %08x %08x\n",
                    descriptor, rxdw0, rxdw1, rxbufLO, rxbufHI);

        if (!(rxdw0 & CP_RX_OWN)) {
            rtl8139_log("C+ Rx mode : descriptor %d is owned by host\n",
                        descriptor);

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;

            /* update tally counter */
            ++s->tally_counters.RxERR;
            ++s->tally_counters.MissPkt;

            rtl8139_update_irq(s);
            return size_;
        }

        uint32_t rx_space = rxdw0 & CP_RX_BUFFER_SIZE_MASK;

        /* write VLAN info to descriptor variables. */
        if (s->CpCmd & CPlusRxVLAN && bswap16(*((uint16_t *) &buf[ETH_ALEN * 2])) == 0x8100) {
            dot1q_buf = &buf[ETH_ALEN * 2];
            size -= VLAN_HLEN;
            /* if too small buffer, use the tailroom added duing expansion */
            if (size < 60) {
                size = 60;
            }

            rxdw1 &= ~CP_RX_VLAN_TAG_MASK;
            /* BE + ~le_to_cpu()~ + cpu_to_le() = BE */
            rxdw1 |= CP_RX_TAVA | *((uint16_t *) (&dot1q_buf[ETHER_TYPE_LEN]));

            rtl8139_log("C+ Rx mode : extracted vlan tag with tci: "
                        "%u\n",
                        bswap16(*((uint16_t *) &dot1q_buf[ETHER_TYPE_LEN])));
        } else {
            /* reset VLAN tag flag */
            rxdw1 &= ~CP_RX_TAVA;
        }

        /* TODO: scatter the packet over available receive ring descriptors space */

        if (size + 4 > rx_space) {
            rtl8139_log("C+ Rx mode : descriptor %d size %d received %u + 4\n",
                        descriptor, rx_space, (uint32_t) size);

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;

            /* update tally counter */
            ++s->tally_counters.RxERR;
            ++s->tally_counters.MissPkt;

            rtl8139_update_irq(s);
            return size_;
        }

        uint32_t rx_addr = rtl8139_addr64(rxbufLO, rxbufHI);

        /* receive/copy to target memory */
        if (dot1q_buf) {
            dma_bm_write(rx_addr, buf, 2 * ETH_ALEN, 1);
            dma_bm_write(rx_addr + 2 * ETH_ALEN,
                         buf + 2 * ETH_ALEN + VLAN_HLEN,
                         size - 2 * ETH_ALEN, 1);
        } else {
            dma_bm_write(rx_addr, buf, size, 1);
        }

        if (s->CpCmd & CPlusRxChkSum) {
            /* do some packet checksumming */
        }

        /* write checksum */
        val = (net_crc32_le(buf, size_));
        dma_bm_write(rx_addr + size, (uint8_t *) &val, 4, 4);

/* first segment of received packet flag */
#define CP_RX_STATUS_FS (1 << 29)
/* last segment of received packet flag */
#define CP_RX_STATUS_LS (1 << 28)
/* multicast packet flag */
#define CP_RX_STATUS_MAR (1 << 26)
/* physical-matching packet flag */
#define CP_RX_STATUS_PAM (1 << 25)
/* broadcast packet flag */
#define CP_RX_STATUS_BAR (1 << 24)
/* runt packet flag */
#define CP_RX_STATUS_RUNT (1 << 19)
/* crc error flag */
#define CP_RX_STATUS_CRC (1 << 18)
/* IP checksum error flag */
#define CP_RX_STATUS_IPF (1 << 15)
/* UDP checksum error flag */
#define CP_RX_STATUS_UDPF (1 << 14)
/* TCP checksum error flag */
#define CP_RX_STATUS_TCPF (1 << 13)

        /* transfer ownership to target */
        rxdw0 &= ~CP_RX_OWN;

        /* set first segment bit */
        rxdw0 |= CP_RX_STATUS_FS;

        /* set last segment bit */
        rxdw0 |= CP_RX_STATUS_LS;

        /* set received packet type flags */
        if (packet_header & RxBroadcast)
            rxdw0 |= CP_RX_STATUS_BAR;
        if (packet_header & RxMulticast)
            rxdw0 |= CP_RX_STATUS_MAR;
        if (packet_header & RxPhysical)
            rxdw0 |= CP_RX_STATUS_PAM;

        /* set received size */
        rxdw0 &= ~CP_RX_BUFFER_SIZE_MASK;
        rxdw0 |= (size + 4);

        /* update ring data */
        val = rxdw0;
        dma_bm_write(cplus_rx_ring_desc, (uint8_t *) &val, 4, 4);
        val = rxdw1;
        dma_bm_write(cplus_rx_ring_desc + 4, (uint8_t *) &val, 4, 4);

        /* update tally counter */
        ++s->tally_counters.RxOk;

        /* seek to next Rx descriptor */
        if (rxdw0 & CP_RX_EOR) {
            s->currCPlusRxDesc = 0;
        } else {
            ++s->currCPlusRxDesc;
        }

        rtl8139_log("done C+ Rx mode ----------------\n");

    } else {
        rtl8139_log("in ring Rx mode ================\n");

        /* begin ring receiver mode */
        int avail = MOD2(s->RxBufferSize + s->RxBufPtr - s->RxBufAddr, s->RxBufferSize);

        /* if receiver buffer is empty then avail == 0 */

#define RX_ALIGN(x) (((x) + 3) & ~0x3)

        if (avail != 0 && RX_ALIGN(size + 8) >= avail) {
            rtl8139_log("rx overflow: rx buffer length %d head 0x%04x "
                        "read 0x%04x === available 0x%04x need 0x%04zx\n",
                        s->RxBufferSize, s->RxBufAddr, s->RxBufPtr, avail, size + 8);

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;
            rtl8139_update_irq(s);
            return 0;
        }

        packet_header |= RxStatusOK;

        packet_header |= (((size + 4) << 16) & 0xffff0000);

        /* write header */
        uint32_t val = packet_header;

        rtl8139_write_buffer(s, (uint8_t *) &val, 4);

        rtl8139_write_buffer(s, buf, size);

        /* write checksum */
        val = (net_crc32_le(buf, size));
        rtl8139_write_buffer(s, (uint8_t *) &val, 4);

        /* correct buffer write pointer */
        s->RxBufAddr = MOD2(RX_ALIGN(s->RxBufAddr), s->RxBufferSize);

        /* now we can signal we have received something */

        rtl8139_log("received: rx buffer length %d head 0x%04x read 0x%04x\n",
                    s->RxBufferSize, s->RxBufAddr, s->RxBufPtr);
    }

    s->IntrStatus |= RxOK;

    {
        rtl8139_update_irq(s);
    }

    return size_;
}

static void
rtl8139_reset_rxring(RTL8139State *s, uint32_t bufferSize)
{
    s->RxBufferSize = bufferSize;
    s->RxBufPtr     = 0;
    s->RxBufAddr    = 0;
}

static void
rtl8139_reset_phy(RTL8139State *s)
{
    s->BasicModeStatus = 0x7809;
    s->BasicModeStatus |= 0x0020; /* autonegotiation completed */
    /* preserve link state */
    s->BasicModeStatus |= (net_cards_conf[s->nic->card_num].link_state & NET_LINK_DOWN) ? 0 : 0x04;

    s->NWayAdvert    = 0x05e1; /* all modes, full duplex */
    s->NWayLPAR      = 0x05e1; /* all modes, full duplex */
    s->NWayExpansion = 0x0001; /* autonegotiation supported */

    s->CSCR = CSCR_F_LINK_100 | CSCR_HEART_BIT | CSCR_LD;
}

static void
rtl8139_reset(void *priv)
{
    RTL8139State *s = (RTL8139State *) priv;

    /* reset interrupt mask */
    s->IntrStatus = 0;
    s->IntrMask   = 0;

    rtl8139_update_irq(s);

    /* mark all status registers as owned by host */
    for (uint8_t i = 0; i < 4; ++i) {
        s->TxStatus[i] = TxHostOwns;
    }

    s->currTxDesc      = 0;
    s->currCPlusRxDesc = 0;
    s->currCPlusTxDesc = 0;

    s->RxRingAddrLO = 0;
    s->RxRingAddrHI = 0;

    s->RxBuf = 0;

    rtl8139_reset_rxring(s, 8192);

    /* ACK the reset */
    s->TxConfig = 0;

#if 0
//    s->TxConfig |= HW_REVID(1, 0, 0, 0, 0, 0, 0); // RTL-8139  HasHltClk
    s->clock_enabled = 0;
#else
    s->TxConfig |= HW_REVID(1, 1, 1, 0, 1, 1, 0); // RTL-8139C+ HasLWake
    s->clock_enabled = 1;
#endif

    s->bChipCmdState = CmdReset; /* RxBufEmpty bit is calculated on read from ChipCmd */

    /* set initial state data */
    s->Config0 = 0x0; /* No boot ROM */
    s->Config1 = 0xC; /* IO mapped and MEM mapped registers available */
    s->Config3 = 0x1; /* fast back-to-back compatible */
    s->Config5 = 0x0;

    s->CpCmd         = 0x0; /* reset C+ mode */
    s->cplus_enabled = 0;

#if 0
    s->BasicModeCtrl = 0x2100; // 100Mbps, full duplex
    s->BasicModeCtrl = 0x3100; // 100Mbps, full duplex, autonegotiation
    s->BasicModeCtrl = 0x1000; // autonegotiation
#endif
    s->BasicModeCtrl = 0x1100; // full duplex, autonegotiation

    rtl8139_reset_phy(s);

    /* also reset timer and disable timer interrupt */
    s->TCTR      = 0;
    s->TimerInt  = 0;
    s->TCTR_base = 0;
#if 0
    rtl8139_set_next_tctr_time(s);
#endif

    /* reset tally counters */
    RTL8139TallyCounters_clear(&s->tally_counters);
}

static void
RTL8139TallyCounters_clear(RTL8139TallyCounters *counters)
{
    counters->TxOk    = 0;
    counters->RxOk    = 0;
    counters->TxERR   = 0;
    counters->RxERR   = 0;
    counters->MissPkt = 0;
    counters->FAE     = 0;
    counters->Tx1Col  = 0;
    counters->TxMCol  = 0;
    counters->RxOkPhy = 0;
    counters->RxOkBrd = 0;
    counters->RxOkMul = 0;
    counters->TxAbt   = 0;
    counters->TxUndrn = 0;
}

static void
RTL8139TallyCounters_dma_write(RTL8139State *s, uint32_t tc_addr)
{
    RTL8139TallyCounters *tally_counters = &s->tally_counters;

    dma_bm_write(tc_addr, (uint8_t *) tally_counters, sizeof(RTL8139TallyCounters), 1);
}

static void
rtl8139_ChipCmd_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    rtl8139_log("ChipCmd write val=0x%08x\n", val);

    if (val & CmdReset) {
        rtl8139_log("ChipCmd reset\n");
        rtl8139_reset(s);
    }
    if (val & CmdRxEnb) {
        rtl8139_log("ChipCmd enable receiver\n");

        s->currCPlusRxDesc = 0;
    }
    if (val & CmdTxEnb) {
        rtl8139_log("ChipCmd enable transmitter\n");

        s->currCPlusTxDesc = 0;
    }

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xe3, s->bChipCmdState);

    /* Deassert reset pin before next read */
    val &= ~CmdReset;

    s->bChipCmdState = val;
}

static int
rtl8139_RxBufferEmpty(RTL8139State *s)
{
    int unread = MOD2(s->RxBufferSize + s->RxBufAddr - s->RxBufPtr, s->RxBufferSize);

    if (unread != 0) {
        rtl8139_log("receiver buffer data available 0x%04x\n", unread);
        return 0;
    }

    rtl8139_log("receiver buffer is empty\n");

    return 1;
}

static uint32_t
rtl8139_ChipCmd_read(RTL8139State *s)
{
    uint32_t ret = s->bChipCmdState;

    if (rtl8139_RxBufferEmpty(s))
        ret |= RxBufEmpty;

    rtl8139_log("ChipCmd read val=0x%04x\n", ret);

    return ret;
}

static void
rtl8139_CpCmd_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    rtl8139_log("C+ command register write(w) val=0x%04x\n", val);

    s->cplus_enabled = 1;

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xff84, s->CpCmd);

    s->CpCmd = val;
}

static uint32_t
rtl8139_CpCmd_read(RTL8139State *s)
{
    uint32_t ret = s->CpCmd;

    rtl8139_log("C+ command register read(w) val=0x%04x\n", ret);

    return ret;
}

static void
rtl8139_IntrMitigate_write(UNUSED(RTL8139State *s), UNUSED(uint32_t val))
{
    rtl8139_log("C+ IntrMitigate register write(w) val=0x%04x\n", val);
}

static uint32_t
rtl8139_IntrMitigate_read(UNUSED(RTL8139State *s))
{
    uint32_t ret = 0;

    rtl8139_log("C+ IntrMitigate register read(w) val=0x%04x\n", ret);

    return ret;
}

static int
rtl8139_config_writable(RTL8139State *s)
{
    if ((s->Cfg9346 & 0xc0) == 0xc0)
        return 1;

    rtl8139_log("Configuration registers are write-protected\n");

    return 0;
}

static void
rtl8139_BasicModeCtrl_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    rtl8139_log("BasicModeCtrl register write(w) val=0x%04x\n", val);

    /* mask unwritable bits */
    uint32_t mask = 0xccff;

    if (1 || !rtl8139_config_writable(s)) {
        /* Speed setting and autonegotiation enable bits are read-only */
        mask |= 0x3000;
        /* Duplex mode setting is read-only */
        mask |= 0x0100;
    }

    if (val & 0x8000) {
        /* Reset PHY */
        rtl8139_reset_phy(s);
    }

    val = SET_MASKED(val, mask, s->BasicModeCtrl);

    s->BasicModeCtrl = val;
}

static uint32_t
rtl8139_BasicModeCtrl_read(RTL8139State *s)
{
    uint32_t ret = s->BasicModeCtrl;

    rtl8139_log("BasicModeCtrl register read(w) val=0x%04x\n", ret);

    return ret;
}

static void
rtl8139_BasicModeStatus_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    rtl8139_log("BasicModeStatus register write(w) val=0x%04x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xff3f, s->BasicModeStatus);

    s->BasicModeStatus = val;
}

static uint32_t
rtl8139_BasicModeStatus_read(RTL8139State *s)
{
    uint32_t ret = s->BasicModeStatus;

    rtl8139_log("BasicModeStatus register read(w) val=0x%04x\n", ret);

    return ret;
}

static void
rtl8139_Cfg9346_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    rtl8139_log("Cfg9346 write val=0x%02x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0x31, s->Cfg9346);

    uint32_t opmode     = val & 0xc0;
    uint32_t eeprom_val = val & 0xf;

    if (opmode == 0x80) {
        /* eeprom access */
        nmc93cxx_eeprom_write(s->eeprom,
                              !!(eeprom_val & 0x08),
                              !!(eeprom_val & 0x04),
                              !!(eeprom_val & 0x02));
    } else if (opmode == 0x40) {
        /* Reset.  */
        val = 0;
        rtl8139_reset(s);
    }

    s->Cfg9346 = val;
}

static uint32_t
rtl8139_Cfg9346_read(RTL8139State *s)
{
    uint32_t ret = s->Cfg9346;

    uint32_t opmode = ret & 0xc0;

    if (opmode == 0x80) {
        if (nmc93cxx_eeprom_read(s->eeprom))
            ret |= 0x01;
        else
            ret &= ~0x01;
    }

    rtl8139_log("Cfg9346 read val=0x%02x\n", ret);

    return ret;
}

static void
rtl8139_Config0_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    rtl8139_log("Config0 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xf8, s->Config0);

    s->Config0 = val;
}

static uint32_t
rtl8139_Config0_read(RTL8139State *s)
{
    uint32_t ret = s->Config0;

    rtl8139_log("Config0 read val=0x%02x\n", ret);

    return ret;
}

static void
rtl8139_Config1_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    rtl8139_log("Config1 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xC, s->Config1);

    s->Config1 = val;
}

static uint32_t
rtl8139_Config1_read(RTL8139State *s)
{
    uint32_t ret = s->Config1;

    rtl8139_log("Config1 read val=0x%02x\n", ret);

    return ret;
}

static void
rtl8139_Config3_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    rtl8139_log("Config3 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    /* mask unwritable bits */
    val = SET_MASKED(val, 0x8F, s->Config3);

    s->Config3 = val;
}

static uint32_t
rtl8139_Config3_read(RTL8139State *s)
{
    uint32_t ret = s->Config3;

    rtl8139_log("Config3 read val=0x%02x\n", ret);

    return ret;
}

static void
rtl8139_Config4_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    rtl8139_log("Config4 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    /* mask unwritable bits */
    val = SET_MASKED(val, 0x0a, s->Config4);

    s->Config4 = val;
}

static uint32_t
rtl8139_Config4_read(RTL8139State *s)
{
    uint32_t ret = s->Config4;

    rtl8139_log("Config4 read val=0x%02x\n", ret);

    return ret;
}

static void
rtl8139_Config5_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    rtl8139_log("Config5 write val=0x%02x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0x80, s->Config5);

    s->Config5 = val;
}

static uint32_t
rtl8139_Config5_read(RTL8139State *s)
{
    uint32_t ret = s->Config5;

    rtl8139_log("Config5 read val=0x%02x\n", ret);

    return ret;
}

static void
rtl8139_TxConfig_write(RTL8139State *s, uint32_t val)
{
    if (!rtl8139_transmitter_enabled(s)) {
        rtl8139_log("transmitter disabled; no TxConfig write val=0x%08x\n", val);
        return;
    }

    rtl8139_log("TxConfig write val=0x%08x\n", val);

    val = SET_MASKED(val, TxVersionMask | 0x8070f80f, s->TxConfig);

    s->TxConfig = val;
}

static void
rtl8139_TxConfig_writeb(RTL8139State *s, uint32_t val)
{
    rtl8139_log("RTL8139C TxConfig via write(b) val=0x%02x\n", val);

    uint32_t tc = s->TxConfig;

    tc &= 0xFFFFFF00;
    tc |= (val & 0x000000FF);
    rtl8139_TxConfig_write(s, tc);
}

static uint32_t
rtl8139_TxConfig_read(RTL8139State *s)
{
    uint32_t ret = s->TxConfig;

    rtl8139_log("TxConfig read val=0x%04x\n", ret);

    return ret;
}

static void
rtl8139_RxConfig_write(RTL8139State *s, uint32_t val)
{
    rtl8139_log("RxConfig write val=0x%08x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xf0fc0040, s->RxConfig);

    s->RxConfig = val;

    /* reset buffer size and read/write pointers */
    rtl8139_reset_rxring(s, 8192 << ((s->RxConfig >> 11) & 0x3));

    rtl8139_log("RxConfig write reset buffer size to %d\n", s->RxBufferSize);
}

static uint32_t
rtl8139_RxConfig_read(RTL8139State *s)
{
    uint32_t ret = s->RxConfig;

    rtl8139_log("RxConfig read val=0x%08x\n", ret);

    return ret;
}

void
rtl8139_network_rx_put(netcard_t *card, uint8_t *bufp, int len)
{
    (void) network_rx_put(card, bufp, len);
}

static void
rtl8139_transfer_frame(RTL8139State *s, uint8_t *buf, int size,
                       UNUSED(int do_interrupt), const uint8_t *dot1q_buf)
{
    void (*network_func)(netcard_t *, uint8_t *, int) = (TxLoopBack == (s->TxConfig & TxLoopBack)) ? rtl8139_network_rx_put : network_tx;
    if (!size) {
        rtl8139_log("+++ empty ethernet frame\n");
        return;
    }

    if (dot1q_buf && size >= ETH_ALEN * 2) {
        network_func(s->nic, buf, ETH_ALEN * 2);
        network_func(s->nic, (uint8_t *) dot1q_buf, VLAN_HLEN);
        network_func(s->nic, buf + ETH_ALEN * 2, size - ETH_ALEN * 2);
        return;
    }

    network_func(s->nic, buf, size);
    return;
}

static int
rtl8139_transmit_one(RTL8139State *s, int descriptor)
{
    int     txsize = s->TxStatus[descriptor] & 0x1fff;
    uint8_t txbuffer[0x2000];

    if (!rtl8139_transmitter_enabled(s)) {
        rtl8139_log("+++ cannot transmit from descriptor %d: transmitter "
                    "disabled\n", descriptor);
        return 0;
    }

    if (s->TxStatus[descriptor] & TxHostOwns) {
        rtl8139_log("+++ cannot transmit from descriptor %d: owned by host "
                    "(%08x)\n", descriptor, s->TxStatus[descriptor]);
        return 0;
    }

    rtl8139_log("+++ transmitting from descriptor %d\n", descriptor);

    rtl8139_log("+++ transmit reading %d bytes from host memory at 0x%08x\n",
                txsize, s->TxAddr[descriptor]);

    dma_bm_read(s->TxAddr[descriptor], txbuffer, txsize, 1);

    /* Mark descriptor as transferred */
    s->TxStatus[descriptor] |= TxHostOwns;
    s->TxStatus[descriptor] |= TxStatOK;

    rtl8139_transfer_frame(s, txbuffer, txsize, 0, NULL);

    rtl8139_log("+++ transmitted %d bytes from descriptor %d\n", txsize,
                descriptor);

    /* update interrupt */
    s->IntrStatus |= TxOK;
    rtl8139_update_irq(s);

    return 1;
}

#define TCP_HEADER_CLEAR_FLAGS(tcp, off) ((tcp)->th_offset_flags &= cpu_to_be16(~TCP_FLAGS_ONLY(off)))

/* produces ones' complement sum of data */
static uint16_t
ones_complement_sum(uint8_t *data, size_t len)
{
    uint32_t result = 0;

    for (; len > 1; data += 2, len -= 2) {
        result += *(uint16_t *) data;
    }

    /* add the remainder byte */
    if (len) {
        uint8_t odd[2] = { *data, 0 };
        result += *(uint16_t *) odd;
    }

    while (result >> 16)
        result = (result & 0xffff) + (result >> 16);

    return result;
}

static uint16_t
ip_checksum(void *data, size_t len)
{
    return ~ones_complement_sum((uint8_t *) data, len);
}

/* TODO: Replace these with equivalents in 86Box if applicable. */
struct ip_header {
    uint8_t  ip_ver_len;     /* version and header length */
    uint8_t  ip_tos;         /* type of service */
    uint16_t ip_len;         /* total length */
    uint16_t ip_id;          /* identification */
    uint16_t ip_off;         /* fragment offset field */
    uint8_t  ip_ttl;         /* time to live */
    uint8_t  ip_p;           /* protocol */
    uint16_t ip_sum;         /* checksum */
    uint32_t ip_src, ip_dst; /* source and destination address */
};

typedef struct tcp_header {
    uint16_t th_sport;        /* source port */
    uint16_t th_dport;        /* destination port */
    uint32_t th_seq;          /* sequence number */
    uint32_t th_ack;          /* acknowledgment number */
    uint16_t th_offset_flags; /* data offset, reserved 6 bits, */
                              /* TCP protocol flags */
    uint16_t th_win;          /* window */
    uint16_t th_sum;          /* checksum */
    uint16_t th_urp;          /* urgent pointer */
} tcp_header;

typedef struct ip_pseudo_header {
    uint32_t ip_src;
    uint32_t ip_dst;
    uint8_t  zeros;
    uint8_t  ip_proto;
    uint16_t ip_payload;
} ip_pseudo_header;

typedef struct udp_header {
    uint16_t uh_sport; /* source port */
    uint16_t uh_dport; /* destination port */
    uint16_t uh_ulen;  /* udp length */
    uint16_t uh_sum;   /* udp checksum */
} udp_header;

#define ETH_HLEN            14
#define ETH_P_IP            (0x0800)
#define IP_PROTO_TCP        (6)
#define IP_PROTO_UDP        (17)
#define IP_HEADER_VERSION_4 (4)
#define TH_PUSH             0x08
#define TH_FIN              0x01

#define IP_HDR_GET_LEN(p) \
    ((*(uint8_t *) ((p + __builtin_offsetof(struct ip_header, ip_ver_len))) & 0x0F) << 2)

#define IP_HEADER_VERSION(ip) \
    (((ip)->ip_ver_len >> 4) & 0xf)

#define TCP_HEADER_DATA_OFFSET(tcp) \
    (((be16_to_cpu((tcp)->th_offset_flags) >> 12) & 0xf) << 2)

static int
rtl8139_cplus_transmit_one(RTL8139State *s)
{
    if (!rtl8139_transmitter_enabled(s)) {
        rtl8139_log("+++ C+ mode: transmitter disabled\n");
        return 0;
    }

    if (!rtl8139_cp_transmitter_enabled(s)) {
        rtl8139_log("+++ C+ mode: C+ transmitter disabled\n");
        return 0;
    }
    int descriptor = s->currCPlusTxDesc;

    uint32_t cplus_tx_ring_desc = rtl8139_addr64(s->TxAddr[0], s->TxAddr[1]);

    /* Normal priority ring */
    cplus_tx_ring_desc += 16 * descriptor;

    rtl8139_log("+++ C+ mode reading TX descriptor %d from host memory at "
                "%08x %08x = 0x%08X\n", descriptor, s->TxAddr[1],
                s->TxAddr[0], cplus_tx_ring_desc);

    uint32_t val;
    uint32_t txdw0;
    uint32_t txdw1;
    uint32_t txbufLO;
    uint32_t txbufHI;

    dma_bm_read(cplus_tx_ring_desc, (uint8_t *) &val, 4, 4);
    txdw0 = le32_to_cpu(val);
    dma_bm_read(cplus_tx_ring_desc + 4, (uint8_t *) &val, 4, 4);
    txdw1 = le32_to_cpu(val);
    dma_bm_read(cplus_tx_ring_desc + 8, (uint8_t *) &val, 4, 4);
    txbufLO = le32_to_cpu(val);
    dma_bm_read(cplus_tx_ring_desc + 12, (uint8_t *) &val, 4, 4);
    txbufHI = le32_to_cpu(val);

    rtl8139_log("+++ C+ mode TX descriptor %d %08x %08x %08x %08x\n", descriptor,
                txdw0, txdw1, txbufLO, txbufHI);

/* w0 ownership flag */
#define CP_TX_OWN (1 << 31)
/* w0 end of ring flag */
#define CP_TX_EOR (1 << 30)
/* first segment of received packet flag */
#define CP_TX_FS (1 << 29)
/* last segment of received packet flag */
#define CP_TX_LS (1 << 28)
/* large send packet flag */
#define CP_TX_LGSEN (1 << 27)
/* large send MSS mask, bits 16...26 */
#define CP_TC_LGSEN_MSS_SHIFT 16
#define CP_TC_LGSEN_MSS_MASK  ((1 << 11) - 1)

/* IP checksum offload flag */
#define CP_TX_IPCS (1 << 18)
/* UDP checksum offload flag */
#define CP_TX_UDPCS (1 << 17)
/* TCP checksum offload flag */
#define CP_TX_TCPCS (1 << 16)

/* w0 bits 0...15 : buffer size */
#define CP_TX_BUFFER_SIZE      (1 << 16)
#define CP_TX_BUFFER_SIZE_MASK (CP_TX_BUFFER_SIZE - 1)
/* w1 add tag flag */
#define CP_TX_TAGC (1 << 17)
/* w1 bits 0...15 : VLAN tag (big endian) */
#define CP_TX_VLAN_TAG_MASK ((1 << 16) - 1)
/* w2 low  32bit of Rx buffer ptr */
/* w3 high 32bit of Rx buffer ptr */

/* set after transmission */
/* FIFO underrun flag */
#define CP_TX_STATUS_UNF (1 << 25)
/* transmit error summary flag, valid if set any of three below */
#define CP_TX_STATUS_TES (1 << 23)
/* out-of-window collision flag */
#define CP_TX_STATUS_OWC (1 << 22)
/* link failure flag */
#define CP_TX_STATUS_LNKF (1 << 21)
/* excessive collisions flag */
#define CP_TX_STATUS_EXC (1 << 20)

    if (!(txdw0 & CP_TX_OWN)) {
        rtl8139_log("C+ Tx mode : descriptor %d is owned by host\n", descriptor);
        return 0;
    }

    rtl8139_log("+++ C+ Tx mode : transmitting from descriptor %d\n", descriptor);

    if (txdw0 & CP_TX_FS) {
        rtl8139_log("+++ C+ Tx mode : descriptor %d is first segment "
                    "descriptor\n", descriptor);

        /* reset internal buffer offset */
        s->cplus_txbuffer_offset = 0;
    }

    int      txsize  = txdw0 & CP_TX_BUFFER_SIZE_MASK;
    uint32_t tx_addr = rtl8139_addr64(txbufLO, txbufHI);

    /* make sure we have enough space to assemble the packet */
    if (!s->cplus_txbuffer) {
        s->cplus_txbuffer_len    = CP_TX_BUFFER_SIZE;
        s->cplus_txbuffer        = calloc(s->cplus_txbuffer_len, 1);
        s->cplus_txbuffer_offset = 0;

        rtl8139_log("+++ C+ mode transmission buffer allocated space %d\n",
                    s->cplus_txbuffer_len);
    }

    if (s->cplus_txbuffer_offset + txsize >= s->cplus_txbuffer_len) {
        /* The spec didn't tell the maximum size, stick to CP_TX_BUFFER_SIZE */
        txsize = s->cplus_txbuffer_len - s->cplus_txbuffer_offset;
        rtl8139_log("+++ C+ mode transmission buffer overrun, truncated descriptor"
                    "length to %d\n", txsize);
    }

    /* append more data to the packet */

    rtl8139_log("+++ C+ mode transmit reading %d bytes from host memory at "
                "%08X to offset %d\n", txsize, tx_addr, s->cplus_txbuffer_offset);

    dma_bm_read(tx_addr,
                s->cplus_txbuffer + s->cplus_txbuffer_offset, txsize, 1);
    s->cplus_txbuffer_offset += txsize;

    /* seek to next Rx descriptor */
    if (txdw0 & CP_TX_EOR) {
        s->currCPlusTxDesc = 0;
    } else {
        ++s->currCPlusTxDesc;
        if (s->currCPlusTxDesc >= 64)
            s->currCPlusTxDesc = 0;
    }

    /* Build the Tx Status Descriptor */
    uint32_t tx_status = txdw0;

    /* transfer ownership to target */
    tx_status &= ~CP_TX_OWN;

    /* reset error indicator bits */
    tx_status &= ~CP_TX_STATUS_UNF;
    tx_status &= ~CP_TX_STATUS_TES;
    tx_status &= ~CP_TX_STATUS_OWC;
    tx_status &= ~CP_TX_STATUS_LNKF;
    tx_status &= ~CP_TX_STATUS_EXC;

    /* update ring data */
    val = cpu_to_le32(tx_status);
    dma_bm_write(cplus_tx_ring_desc, (uint8_t *) &val, 4, 4);

    /* Now decide if descriptor being processed is holding the last segment of packet */
    if (txdw0 & CP_TX_LS) {
        uint8_t   dot1q_buffer_space[VLAN_HLEN];
        uint16_t *dot1q_buffer;

        rtl8139_log("+++ C+ Tx mode : descriptor %d is last segment descriptor\n",
                    descriptor);

        /* can transfer fully assembled packet */

        uint8_t *saved_buffer     = s->cplus_txbuffer;
        int      saved_size       = s->cplus_txbuffer_offset;
        int      saved_buffer_len = s->cplus_txbuffer_len;

        /* create vlan tag */
        if (txdw1 & CP_TX_TAGC) {
            /* the vlan tag is in BE byte order in the descriptor
             * BE + le_to_cpu() + ~swap()~ = cpu */
            rtl8139_log("+++ C+ Tx mode : inserting vlan tag with "
                    "tci: %u\n", bswap16(txdw1 & CP_TX_VLAN_TAG_MASK));

            dot1q_buffer    = (uint16_t *) dot1q_buffer_space;
            dot1q_buffer[0] = cpu_to_be16(0x8100);
            /* BE + le_to_cpu() + ~cpu_to_le()~ = BE */
            dot1q_buffer[1] = cpu_to_le16(txdw1 & CP_TX_VLAN_TAG_MASK);
        } else {
            dot1q_buffer = NULL;
        }

        /* reset the card space to protect from recursive call */
        s->cplus_txbuffer        = NULL;
        s->cplus_txbuffer_offset = 0;
        s->cplus_txbuffer_len    = 0;

        if (txdw0 & (CP_TX_IPCS | CP_TX_UDPCS | CP_TX_TCPCS | CP_TX_LGSEN)) {
            rtl8139_log("+++ C+ mode offloaded task checksum\n");

            /* Large enough for Ethernet and IP headers? */
            if (saved_size < ETH_HLEN + sizeof(struct ip_header)) {
                goto skip_offload;
            }

            /* ip packet header */
            struct ip_header *ip          = NULL;
            int               hlen        = 0;
            uint8_t           ip_protocol = 0;
            uint16_t          ip_data_len = 0;

            uint8_t *eth_payload_data = NULL;
            size_t   eth_payload_len  = 0;

            int proto = be16_to_cpu(*(uint16_t *) (saved_buffer + 12));
            if (proto != ETH_P_IP) {
                goto skip_offload;
            }

            rtl8139_log("+++ C+ mode has IP packet\n");

            /* Note on memory alignment: eth_payload_data is 16-bit aligned
             * since saved_buffer is allocated with g_malloc() and ETH_HLEN is
             * even.  32-bit accesses must use ldl/stl wrappers to avoid
             * unaligned accesses.
             */
            eth_payload_data = saved_buffer + ETH_HLEN;
            eth_payload_len  = saved_size - ETH_HLEN;

            ip = (struct ip_header *) eth_payload_data;

            if (IP_HEADER_VERSION(ip) != IP_HEADER_VERSION_4) {
                rtl8139_log("+++ C+ mode packet has bad IP version %d "
                            "expected %d\n", IP_HEADER_VERSION(ip),
                            IP_HEADER_VERSION_4);
                goto skip_offload;
            }

            hlen = IP_HDR_GET_LEN(ip);
            if (hlen < sizeof(struct ip_header) || hlen > eth_payload_len) {
                goto skip_offload;
            }

            ip_protocol = ip->ip_p;

            ip_data_len = be16_to_cpu(ip->ip_len);
            if (ip_data_len < hlen || ip_data_len > eth_payload_len) {
                goto skip_offload;
            }
            ip_data_len -= hlen;

            if (!(txdw0 & CP_TX_LGSEN) && (txdw0 & CP_TX_IPCS)) {
                rtl8139_log("+++ C+ mode need IP checksum\n");

                ip->ip_sum = 0;
                ip->ip_sum = ip_checksum(ip, hlen);
                rtl8139_log("+++ C+ mode IP header len=%d checksum=%04x\n",
                            hlen, ip->ip_sum);
            }

            if ((txdw0 & CP_TX_LGSEN) && ip_protocol == IP_PROTO_TCP) {
                /* Large enough for the TCP header? */
                if (ip_data_len < sizeof(tcp_header)) {
                    goto skip_offload;
                }

                int large_send_mss = (txdw0 >> CP_TC_LGSEN_MSS_SHIFT) & CP_TC_LGSEN_MSS_MASK;
                if (large_send_mss == 0) {
                    goto skip_offload;
                }

                rtl8139_log("+++ C+ mode offloaded task TSO IP data %d "
                            "frame data %d specified MSS=%d\n",
                            ip_data_len, saved_size - ETH_HLEN, large_send_mss);

                /* maximum IP header length is 60 bytes */
                uint8_t saved_ip_header[60];

                /* save IP header template; data area is used in tcp checksum calculation */
                memcpy(saved_ip_header, eth_payload_data, hlen);

                /* a placeholder for checksum calculation routine in tcp case */
                uint8_t *data_to_checksum = eth_payload_data + hlen - 12;
#if 0
                size_t   data_to_checksum_len = eth_payload_len  - hlen + 12;
#endif

                /* pointer to TCP header */
                tcp_header *p_tcp_hdr = (tcp_header *) (eth_payload_data + hlen);

                int tcp_hlen = TCP_HEADER_DATA_OFFSET(p_tcp_hdr);

                /* Invalid TCP data offset? */
                if (tcp_hlen < sizeof(tcp_header) || tcp_hlen > ip_data_len) {
                    goto skip_offload;
                }

                int tcp_data_len = ip_data_len - tcp_hlen;

                rtl8139_log("+++ C+ mode TSO IP data len %d TCP hlen %d TCP "
                            "data len %d\n", ip_data_len, tcp_hlen, tcp_data_len);

                /* note the cycle below overwrites IP header data,
                   but restores it from saved_ip_header before sending packet */

                int is_last_frame = 0;

                for (int tcp_send_offset = 0; tcp_send_offset < tcp_data_len; tcp_send_offset += large_send_mss) {
                    uint16_t chunk_size = large_send_mss;

                    /* check if this is the last frame */
                    if (tcp_send_offset + large_send_mss >= tcp_data_len) {
                        is_last_frame = 1;
                        chunk_size    = tcp_data_len - tcp_send_offset;
                    }

#if 0
                    rtl8139_log("+++ C+ mode TSO TCP seqno %08x\n",
                                ldl_be_p(&p_tcp_hdr->th_seq));
#endif

                    /* add 4 TCP pseudoheader fields */
                    /* copy IP source and destination fields */
                    memcpy(data_to_checksum, saved_ip_header + 12, 8);

                    rtl8139_log("+++ C+ mode TSO calculating TCP checksum for "
                                "packet with %d bytes data\n", tcp_hlen + chunk_size);

                    if (tcp_send_offset) {
                        memcpy((uint8_t *) p_tcp_hdr + tcp_hlen, (uint8_t *) p_tcp_hdr + tcp_hlen + tcp_send_offset, chunk_size);
                    }

#define TCP_FLAGS_ONLY(flags)            ((flags) &0x3f)
#define TCP_HEADER_CLEAR_FLAGS(tcp, off) ((tcp)->th_offset_flags &= cpu_to_be16(~TCP_FLAGS_ONLY(off)))
                    /* keep PUSH and FIN flags only for the last frame */
                    if (!is_last_frame) {
                        TCP_HEADER_CLEAR_FLAGS(p_tcp_hdr, TH_PUSH | TH_FIN);
                    }

                    /* recalculate TCP checksum */
                    ip_pseudo_header *p_tcpip_hdr = (ip_pseudo_header *) data_to_checksum;
                    p_tcpip_hdr->zeros            = 0;
                    p_tcpip_hdr->ip_proto         = IP_PROTO_TCP;
                    p_tcpip_hdr->ip_payload       = cpu_to_be16(tcp_hlen + chunk_size);

                    p_tcp_hdr->th_sum = 0;

                    int tcp_checksum = ip_checksum(data_to_checksum, tcp_hlen + chunk_size + 12);
                    rtl8139_log("+++ C+ mode TSO TCP checksum %04x\n", tcp_checksum);

                    p_tcp_hdr->th_sum = tcp_checksum;

                    /* restore IP header */
                    memcpy(eth_payload_data, saved_ip_header, hlen);

                    /* set IP data length and recalculate IP checksum */
                    ip->ip_len = cpu_to_be16(hlen + tcp_hlen + chunk_size);

                    /* increment IP id for subsequent frames */
                    ip->ip_id = cpu_to_be16(tcp_send_offset / large_send_mss + be16_to_cpu(ip->ip_id));

                    ip->ip_sum = 0;
                    ip->ip_sum = ip_checksum(eth_payload_data, hlen);
                    rtl8139_log("+++ C+ mode TSO IP header len=%d checksum=%04x\n",
                                hlen, ip->ip_sum);

                    int tso_send_size = ETH_HLEN + hlen + tcp_hlen + chunk_size;
                    rtl8139_log("+++ C+ mode TSO transferring packet size %d\n",
                                tso_send_size);
                    rtl8139_transfer_frame(s, saved_buffer, tso_send_size,
                                           0, (uint8_t *) dot1q_buffer);

                    /* add transferred count to TCP sequence number */
#if 0
                    stl_be_p(&p_tcp_hdr->th_seq,
                             chunk_size + ldl_be_p(&p_tcp_hdr->th_seq));
#endif
                    p_tcp_hdr->th_seq = bswap32(chunk_size + bswap32(p_tcp_hdr->th_seq));
                }

                /* Stop sending this frame */
                saved_size = 0;
            } else if (!(txdw0 & CP_TX_LGSEN) && (txdw0 & (CP_TX_TCPCS | CP_TX_UDPCS))) {
                rtl8139_log("+++ C+ mode need TCP or UDP checksum\n");

                /* maximum IP header length is 60 bytes */
                uint8_t saved_ip_header[60];
                memcpy(saved_ip_header, eth_payload_data, hlen);

                uint8_t *data_to_checksum = eth_payload_data + hlen - 12;
#if 0
                size_t   data_to_checksum_len = eth_payload_len  - hlen + 12;
#endif

                /* add 4 TCP pseudoheader fields */
                /* copy IP source and destination fields */
                memcpy(data_to_checksum, saved_ip_header + 12, 8);

                if ((txdw0 & CP_TX_TCPCS) && ip_protocol == IP_PROTO_TCP) {
                    rtl8139_log("+++ C+ mode calculating TCP checksum for "
                                "packet with %d bytes data\n", ip_data_len);

                    ip_pseudo_header *p_tcpip_hdr = (ip_pseudo_header *) data_to_checksum;
                    p_tcpip_hdr->zeros            = 0;
                    p_tcpip_hdr->ip_proto         = IP_PROTO_TCP;
                    p_tcpip_hdr->ip_payload       = cpu_to_be16(ip_data_len);

                    tcp_header *p_tcp_hdr = (tcp_header *) (data_to_checksum + 12);

                    p_tcp_hdr->th_sum = 0;

                    int tcp_checksum = ip_checksum(data_to_checksum, ip_data_len + 12);
                    rtl8139_log("+++ C+ mode TCP checksum %04x\n", tcp_checksum);

                    p_tcp_hdr->th_sum = tcp_checksum;
                } else if ((txdw0 & CP_TX_UDPCS) && ip_protocol == IP_PROTO_UDP) {
                    rtl8139_log("+++ C+ mode calculating UDP checksum for "
                                "packet with %d bytes data\n", ip_data_len);

                    ip_pseudo_header *p_udpip_hdr = (ip_pseudo_header *) data_to_checksum;
                    p_udpip_hdr->zeros            = 0;
                    p_udpip_hdr->ip_proto         = IP_PROTO_UDP;
                    p_udpip_hdr->ip_payload       = cpu_to_be16(ip_data_len);

                    udp_header *p_udp_hdr = (udp_header *) (data_to_checksum + 12);

                    p_udp_hdr->uh_sum = 0;

                    int udp_checksum = ip_checksum(data_to_checksum, ip_data_len + 12);
                    rtl8139_log("+++ C+ mode UDP checksum %04x\n", udp_checksum);

                    p_udp_hdr->uh_sum = udp_checksum;
                }

                /* restore IP header */
                memcpy(eth_payload_data, saved_ip_header, hlen);
            }
        }

skip_offload:
        /* update tally counter */
        ++s->tally_counters.TxOk;

        rtl8139_log("+++ C+ mode transmitting %d bytes packet\n", saved_size);

        rtl8139_transfer_frame(s, saved_buffer, saved_size, 1,
                               (uint8_t *) dot1q_buffer);

        /* restore card space if there was no recursion and reset offset */
        if (!s->cplus_txbuffer) {
            s->cplus_txbuffer        = saved_buffer;
            s->cplus_txbuffer_len    = saved_buffer_len;
            s->cplus_txbuffer_offset = 0;
        } else
            free(saved_buffer);
    } else {
        rtl8139_log("+++ C+ mode transmission continue to next descriptor\n");
    }

    return 1;
}

static void
rtl8139_cplus_transmit(RTL8139State *s)
{
    int txcount = 0;

    while (txcount < 64 && rtl8139_cplus_transmit_one(s)) {
        ++txcount;
    }

    /* Mark transfer completed */
    if (!txcount) {
        rtl8139_log("C+ mode : transmitter queue stalled, current TxDesc = %d\n",
                    s->currCPlusTxDesc);
    } else {
        /* update interrupt status */
        s->IntrStatus |= TxOK;
        rtl8139_update_irq(s);
    }
}

static void
rtl8139_transmit(RTL8139State *s)
{
    int descriptor = s->currTxDesc;
    int txcount    = 0;

    /*while*/
    if (rtl8139_transmit_one(s, descriptor)) {
        ++s->currTxDesc;
        s->currTxDesc %= 4;
        ++txcount;
    }

    /* Mark transfer completed */
    if (!txcount) {
        rtl8139_log("transmitter queue stalled, current TxDesc = %d\n",
                    s->currTxDesc);
    }
}

static void
rtl8139_TxStatus_write(RTL8139State *s, uint32_t txRegOffset, uint32_t val)
{
    int descriptor = txRegOffset / 4;

    /* handle C+ transmit mode register configuration */

    if (s->cplus_enabled) {
        rtl8139_log("RTL8139C+ DTCCR write offset=0x%x val=0x%08x "
                    "descriptor=%d\n", txRegOffset, val, descriptor);

        /* handle Dump Tally Counters command */
        s->TxStatus[descriptor] = val;

        if (descriptor == 0 && (val & 0x8)) {
            uint32_t tc_addr = rtl8139_addr64(s->TxStatus[0] & ~0x3f, s->TxStatus[1]);

            /* dump tally counters to specified memory location */
            RTL8139TallyCounters_dma_write(s, tc_addr);

            /* mark dump completed */
            s->TxStatus[0] &= ~0x8;
        }

        return;
    }

    rtl8139_log("TxStatus write offset=0x%x val=0x%08x descriptor=%d\n",
                txRegOffset, val, descriptor);

    /* mask only reserved bits */
    val &= ~0xff00c000; /* these bits are reset on write */
    val = SET_MASKED(val, 0x00c00000, s->TxStatus[descriptor]);

    s->TxStatus[descriptor] = val;

    /* attempt to start transmission */
    rtl8139_transmit(s);
}

static uint32_t
rtl8139_TxStatus_TxAddr_read(UNUSED(RTL8139State *s), uint32_t regs[],
                             uint32_t base, uint8_t addr,
                             int size)
{
    uint32_t reg    = (addr - base) / 4;
    uint32_t offset = addr & 0x3;
    uint32_t ret    = 0;

    if (addr & (size - 1)) {
        rtl8139_log("not implemented read for TxStatus/TxAddr "
                    "addr=0x%x size=0x%x\n", addr, size);
        return ret;
    }

    switch (size) {
        case 1: /* fall through */
        case 2: /* fall through */
        case 4:
            ret = (regs[reg] >> offset * 8) & (((uint64_t) 1 << (size * 8)) - 1);
            rtl8139_log("TxStatus/TxAddr[%d] read addr=0x%x size=0x%x val=0x%08x\n",
                        reg, addr, size, ret);
            break;

        default:
            rtl8139_log("unsupported size 0x%x of TxStatus/TxAddr reading\n", size);
            break;
    }

    return ret;
}

static uint16_t
rtl8139_TSAD_read(RTL8139State *s)
{
    uint16_t ret = 0;

    /* Simulate TSAD, it is read only anyway */

    ret = ((s->TxStatus[3] & TxStatOK) ? TSAD_TOK3 : 0)
        | ((s->TxStatus[2] & TxStatOK) ? TSAD_TOK2 : 0)
        | ((s->TxStatus[1] & TxStatOK) ? TSAD_TOK1 : 0)
        | ((s->TxStatus[0] & TxStatOK) ? TSAD_TOK0 : 0)

        | ((s->TxStatus[3] & TxUnderrun) ? TSAD_TUN3 : 0)
        | ((s->TxStatus[2] & TxUnderrun) ? TSAD_TUN2 : 0)
        | ((s->TxStatus[1] & TxUnderrun) ? TSAD_TUN1 : 0)
        | ((s->TxStatus[0] & TxUnderrun) ? TSAD_TUN0 : 0)

        | ((s->TxStatus[3] & TxAborted) ? TSAD_TABT3 : 0)
        | ((s->TxStatus[2] & TxAborted) ? TSAD_TABT2 : 0)
        | ((s->TxStatus[1] & TxAborted) ? TSAD_TABT1 : 0)
        | ((s->TxStatus[0] & TxAborted) ? TSAD_TABT0 : 0)

        | ((s->TxStatus[3] & TxHostOwns) ? TSAD_OWN3 : 0)
        | ((s->TxStatus[2] & TxHostOwns) ? TSAD_OWN2 : 0)
        | ((s->TxStatus[1] & TxHostOwns) ? TSAD_OWN1 : 0)
        | ((s->TxStatus[0] & TxHostOwns) ? TSAD_OWN0 : 0);

    rtl8139_log("TSAD read val=0x%04x\n", ret);

    return ret;
}

static uint16_t
rtl8139_CSCR_read(RTL8139State *s)
{
    static uint16_t old_ret = 0xffff;
    uint16_t ret = s->CSCR |
                   ((net_cards_conf[s->nic->card_num].link_state & NET_LINK_DOWN) ? 0 : CSCR_Cable);

    if (old_ret != 0xffff) {
        ret &= ~CSCR_Cable_Changed;
        if ((ret ^ old_ret) & CSCR_Cable)
            ret |= CSCR_Cable_Changed;
    }

    old_ret = ret;

    rtl8139_log("CSCR read val=0x%04x\n", ret);

    return ret;
}

static void
rtl8139_TxAddr_write(RTL8139State *s, uint32_t txAddrOffset, uint32_t val)
{
    rtl8139_log("TxAddr write offset=0x%x val=0x%08x\n", txAddrOffset, val);

    s->TxAddr[txAddrOffset / 4] = val;
}

static uint32_t
rtl8139_TxAddr_read(RTL8139State *s, uint32_t txAddrOffset)
{
    uint32_t ret = s->TxAddr[txAddrOffset / 4];

    rtl8139_log("TxAddr read offset=0x%x val=0x%08x\n", txAddrOffset, ret);

    return ret;
}

static void
rtl8139_RxBufPtr_write(RTL8139State *s, uint32_t val)
{
    rtl8139_log("RxBufPtr write val=0x%04x\n", val);

    /* this value is off by 16 */
    s->RxBufPtr = MOD2(val + 0x10, s->RxBufferSize);

    rtl8139_log(" CAPR write: rx buffer length %d head 0x%04x read 0x%04x\n",
                s->RxBufferSize, s->RxBufAddr, s->RxBufPtr);
}

static uint32_t
rtl8139_RxBufPtr_read(RTL8139State *s)
{
    /* this value is off by 16 */
    uint32_t ret = s->RxBufPtr - 0x10;

    rtl8139_log("RxBufPtr read val=0x%04x\n", ret);

    return ret;
}

static uint32_t
rtl8139_RxBufAddr_read(RTL8139State *s)
{
    /* this value is NOT off by 16 */
    uint32_t ret = s->RxBufAddr;

    rtl8139_log("RxBufAddr read val=0x%04x\n", ret);

    return ret;
}

static void
rtl8139_RxBuf_write(RTL8139State *s, uint32_t val)
{
    rtl8139_log("RxBuf write val=0x%08x\n", val);

    s->RxBuf = val;

    /* may need to reset rxring here */
}

static uint32_t
rtl8139_RxBuf_read(RTL8139State *s)
{
    uint32_t ret = s->RxBuf;

    rtl8139_log("RxBuf read val=0x%08x\n", ret);

    return ret;
}

static void
rtl8139_IntrMask_write(RTL8139State *s, uint32_t val)
{
    rtl8139_log("IntrMask write(w) val=0x%04x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0x1e00, s->IntrMask);

    s->IntrMask = val;

    rtl8139_update_irq(s);
}

static uint32_t
rtl8139_IntrMask_read(RTL8139State *s)
{
    uint32_t ret = s->IntrMask;

    rtl8139_log("IntrMask read(w) val=0x%04x\n", ret);

    return ret;
}

static void
rtl8139_IntrStatus_write(RTL8139State *s, uint32_t val)
{
    rtl8139_log("IntrStatus write(w) val=0x%04x\n", val);

#if 0

    /* writing to ISR has no effect */

    return;

#else
    uint16_t newStatus = s->IntrStatus & ~val;

    /* mask unwritable bits */
    newStatus = SET_MASKED(newStatus, 0x1e00, s->IntrStatus);

    /* writing 1 to interrupt status register bit clears it */
    s->IntrStatus = 0;
    rtl8139_update_irq(s);

    s->IntrStatus = newStatus;
    rtl8139_update_irq(s);

#endif
}

static uint32_t
rtl8139_IntrStatus_read(RTL8139State *s)
{
    uint32_t ret = s->IntrStatus;

    rtl8139_log("IntrStatus read(w) val=0x%04x\n", ret);

#if 0

    /* reading ISR clears all interrupts */
    s->IntrStatus = 0;

    rtl8139_update_irq(s);

#endif

    return ret;
}

static void
rtl8139_MultiIntr_write(RTL8139State *s, uint32_t val)
{
    rtl8139_log("MultiIntr write(w) val=0x%04x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xf000, s->MultiIntr);

    s->MultiIntr = val;
}

static uint32_t
rtl8139_MultiIntr_read(RTL8139State *s)
{
    uint32_t ret = s->MultiIntr;

    rtl8139_log("MultiIntr read(w) val=0x%04x\n", ret);

    return ret;
}

static void
rtl8139_io_writeb(uint32_t addr, uint8_t val, void *priv)
{
    RTL8139State *s = priv;

    addr &= 0xFF;
    switch (addr) {
        case MAC0 ... MAC0 + 4:
            s->phys[addr - MAC0] = val;
            break;
        case MAC0 + 5:
            s->phys[addr - MAC0] = val;
            break;
        case MAC0 + 6 ... MAC0 + 7:
            /* reserved */
            break;
        case MAR0 ... MAR0 + 7:
            s->mult[addr - MAR0] = val;
            break;
        case ChipCmd:
            rtl8139_ChipCmd_write(s, val);
            break;
        case Cfg9346:
            rtl8139_Cfg9346_write(s, val);
            break;
        case TxConfig: /* windows driver sometimes writes using byte-lenth call */
            rtl8139_TxConfig_writeb(s, val);
            break;
        case Config0:
            rtl8139_Config0_write(s, val);
            break;
        case Config1:
            rtl8139_Config1_write(s, val);
            break;
        case Config3:
            rtl8139_Config3_write(s, val);
            break;
        case Config4:
            rtl8139_Config4_write(s, val);
            break;
        case Config5:
            rtl8139_Config5_write(s, val);
            break;
        case MediaStatus:
            /* ignore */
            rtl8139_log("not implemented write(b) to MediaStatus val=0x%02x\n", val);
            break;

        case HltClk:
            rtl8139_log("HltClk write val=0x%08x\n", val);
            if (val == 'R') {
                s->clock_enabled = 1;
            } else if (val == 'H') {
                s->clock_enabled = 0;
            }
            break;

        case TxThresh:
            rtl8139_log("C+ TxThresh write(b) val=0x%02x\n", val);
            s->TxThresh = val;
            break;

        case TxPoll:
            rtl8139_log("C+ TxPoll write(b) val=0x%02x\n", val);
            if (val & (1 << 7)) {
                rtl8139_log("C+ TxPoll high priority transmission (not "
                            "implemented)\n");
#if 0
                rtl8139_cplus_transmit(s);
#endif
            }
            if (val & (1 << 6)) {
                rtl8139_log("C+ TxPoll normal priority transmission\n");
                rtl8139_cplus_transmit(s);
            }

            break;

        default:
            rtl8139_log("not implemented write(b) addr=0x%x val=0x%02x\n", addr, val);
            break;
    }
}

static void
rtl8139_io_writew(uint32_t addr, uint16_t val, void *priv)
{
    RTL8139State *s = priv;

    addr &= 0xFF;
    switch (addr) {
        case IntrMask:
            rtl8139_IntrMask_write(s, val);
            break;

        case IntrStatus:
            rtl8139_IntrStatus_write(s, val);
            break;

        case MultiIntr:
            rtl8139_MultiIntr_write(s, val);
            break;

        case RxBufPtr:
            rtl8139_RxBufPtr_write(s, val);
            break;

        case BasicModeCtrl:
            rtl8139_BasicModeCtrl_write(s, val);
            break;
        case BasicModeStatus:
            rtl8139_BasicModeStatus_write(s, val);
            break;
        case NWayAdvert:
            rtl8139_log("NWayAdvert write(w) val=0x%04x\n", val);
            s->NWayAdvert = val;
            break;
        case NWayLPAR:
            rtl8139_log("forbidden NWayLPAR write(w) val=0x%04x\n", val);
            break;
        case NWayExpansion:
            rtl8139_log("NWayExpansion write(w) val=0x%04x\n", val);
            s->NWayExpansion = val;
            break;

        case CpCmd:
            rtl8139_CpCmd_write(s, val);
            break;

        case IntrMitigate:
            rtl8139_IntrMitigate_write(s, val);
            break;

        default:
            rtl8139_log("ioport write(w) addr=0x%x val=0x%04x via write(b)\n",
                        addr, val);

            rtl8139_io_writeb(addr, val & 0xff, priv);
            rtl8139_io_writeb(addr + 1, (val >> 8) & 0xff, priv);
            break;
    }
}

/* TODO: Implement timer. */

static void
rtl8139_io_writel(uint32_t addr, uint32_t val, void *priv)
{
    RTL8139State *s = priv;

    addr &= 0xFF;
    switch (addr) {
        case RxMissed:
            rtl8139_log("RxMissed clearing on write\n");
            s->RxMissed = 0;
            break;

        case TxConfig:
            rtl8139_TxConfig_write(s, val);
            break;

        case RxConfig:
            rtl8139_RxConfig_write(s, val);
            break;

        case TxStatus0 ... TxStatus0 + 4 * 4 - 1:
            rtl8139_TxStatus_write(s, addr - TxStatus0, val);
            break;

        case TxAddr0 ... TxAddr0 + 4 * 4 - 1:
            rtl8139_TxAddr_write(s, addr - TxAddr0, val);
            break;

        case RxBuf:
            rtl8139_RxBuf_write(s, val);
            break;

        case RxRingAddrLO:
            rtl8139_log("C+ RxRing low bits write val=0x%08x\n", val);
            s->RxRingAddrLO = val;
            break;

        case RxRingAddrHI:
            rtl8139_log("C+ RxRing high bits write val=0x%08x\n", val);
            s->RxRingAddrHI = val;
            break;

        case Timer:
            rtl8139_log("TCTR Timer reset on write\n");
            s->TCTR = 0;
#if 0
            s->TCTR_base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            rtl8139_set_next_tctr_time(s);
#endif
            break;

        case FlashReg:
            rtl8139_log("FlashReg TimerInt write val=0x%08x\n", val);
            if (s->TimerInt != val)
                s->TimerInt = val;
            break;

        default:
            rtl8139_log("ioport write(l) addr=0x%x val=0x%08x via write(b)\n",
                        addr, val);
            rtl8139_io_writeb(addr, val & 0xff, priv);
            rtl8139_io_writeb(addr + 1, (val >> 8) & 0xff, priv);
            rtl8139_io_writeb(addr + 2, (val >> 16) & 0xff, priv);
            rtl8139_io_writeb(addr + 3, (val >> 24) & 0xff, priv);
            break;
    }
}

static uint8_t
rtl8139_io_readb(uint32_t addr, void *priv)
{
    RTL8139State *s = priv;
    uint8_t       ret;

    addr &= 0xFF;
    switch (addr) {
        case MAC0 ... MAC0 + 5:
            ret = s->phys[addr - MAC0];
            break;
        case MAC0 + 6 ... MAC0 + 7:
            ret = 0;
            break;
        case MAR0 ... MAR0 + 7:
            ret = s->mult[addr - MAR0];
            break;
        case TxStatus0 ... TxStatus0 + 4 * 4 - 1:
            ret = rtl8139_TxStatus_TxAddr_read(s, s->TxStatus, TxStatus0,
                                               addr, 1);
            break;
        case ChipCmd:
            ret = rtl8139_ChipCmd_read(s);
            break;
        case Cfg9346:
            ret = rtl8139_Cfg9346_read(s);
            break;
        case Config0:
            ret = rtl8139_Config0_read(s);
            break;
        case Config1:
            ret = rtl8139_Config1_read(s);
            break;
        case Config3:
            ret = rtl8139_Config3_read(s);
            break;
        case Config4:
            ret = rtl8139_Config4_read(s);
            break;
        case Config5:
            ret = rtl8139_Config5_read(s);
            break;

        case MediaStatus:
            /* The LinkDown bit of MediaStatus is inverse with link status */
            ret = 0xd0 | (~s->BasicModeStatus & 0x04);
            rtl8139_log("MediaStatus read 0x%x\n", ret);
            break;

        case HltClk:
            ret = s->clock_enabled;
            rtl8139_log("HltClk read 0x%x\n", ret);
            break;

        case PCIRevisionID:
            ret = RTL8139_PCI_REVID;
            rtl8139_log("PCI Revision ID read 0x%x\n", ret);
            break;

        case TxThresh:
            ret = s->TxThresh;
            rtl8139_log("C+ TxThresh read(b) val=0x%02x\n", ret);
            break;

        case 0x43: /* Part of TxConfig register. Windows driver tries to read it */
            ret = s->TxConfig >> 24;
            rtl8139_log("RTL8139C TxConfig at 0x43 read(b) val=0x%02x\n", ret);
            break;

        case CSCR:
            ret = rtl8139_CSCR_read(s) & 0xff;
            break;
        case CSCR + 1:
            ret = rtl8139_CSCR_read(s) >> 8;
            break;

        default:
            rtl8139_log("not implemented read(b) addr=0x%x\n", addr);
            ret = 0;
            break;
    }

    return ret;
}

static uint16_t
rtl8139_io_readw(uint32_t addr, void *priv)
{
    RTL8139State *s = priv;
    uint16_t      ret;

    addr &= 0xFF;
    switch (addr) {
        case TxAddr0 ... TxAddr0 + 4 * 4 - 1:
            ret = rtl8139_TxStatus_TxAddr_read(s, s->TxAddr, TxAddr0, addr, 2);
            break;
        case IntrMask:
            ret = rtl8139_IntrMask_read(s);
            break;

        case IntrStatus:
            ret = rtl8139_IntrStatus_read(s);
            break;

        case MultiIntr:
            ret = rtl8139_MultiIntr_read(s);
            break;

        case RxBufPtr:
            ret = rtl8139_RxBufPtr_read(s);
            break;

        case RxBufAddr:
            ret = rtl8139_RxBufAddr_read(s);
            break;

        case BasicModeCtrl:
            ret = rtl8139_BasicModeCtrl_read(s);
            break;
        case BasicModeStatus:
            ret = rtl8139_BasicModeStatus_read(s);
            break;
        case NWayAdvert:
            ret = s->NWayAdvert;
            rtl8139_log("NWayAdvert read(w) val=0x%04x\n", ret);
            break;
        case NWayLPAR:
            ret = s->NWayLPAR;
            rtl8139_log("NWayLPAR read(w) val=0x%04x\n", ret);
            break;
        case NWayExpansion:
            ret = s->NWayExpansion;
            rtl8139_log("NWayExpansion read(w) val=0x%04x\n", ret);
            break;

        case CpCmd:
            ret = rtl8139_CpCmd_read(s);
            break;

        case IntrMitigate:
            ret = rtl8139_IntrMitigate_read(s);
            break;

        case TxSummary:
            ret = rtl8139_TSAD_read(s);
            break;

        case CSCR:
            ret = rtl8139_CSCR_read(s);
            break;

        default:
            rtl8139_log("ioport read(w) addr=0x%x via read(b)\n", addr);

            ret = rtl8139_io_readb(addr, priv);
            ret |= rtl8139_io_readb(addr + 1, priv) << 8;

            rtl8139_log("ioport read(w) addr=0x%x val=0x%04x\n", addr, ret);
            break;
    }

    return ret;
}

static uint32_t
rtl8139_io_readl(uint32_t addr, void *priv)
{
    RTL8139State *s = priv;
    uint32_t      ret;

    addr &= 0xFF;
    switch (addr) {
        case RxMissed:
            ret = s->RxMissed;

            rtl8139_log("RxMissed read val=0x%08x\n", ret);
            break;

        case TxConfig:
            ret = rtl8139_TxConfig_read(s);
            break;

        case RxConfig:
            ret = rtl8139_RxConfig_read(s);
            break;

        case TxStatus0 ... TxStatus0 + 4 * 4 - 1:
            ret = rtl8139_TxStatus_TxAddr_read(s, s->TxStatus, TxStatus0,
                                               addr, 4);
            break;

        case TxAddr0 ... TxAddr0 + 4 * 4 - 1:
            ret = rtl8139_TxAddr_read(s, addr - TxAddr0);
            break;

        case RxBuf:
            ret = rtl8139_RxBuf_read(s);
            break;

        case RxRingAddrLO:
            ret = s->RxRingAddrLO;
            rtl8139_log("C+ RxRing low bits read val=0x%08x\n", ret);
            break;

        case RxRingAddrHI:
            ret = s->RxRingAddrHI;
            rtl8139_log("C+ RxRing high bits read val=0x%08x\n", ret);
            break;

        case Timer:
#if 0
            ret = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->TCTR_base) /
                  PCI_PERIOD;
#endif
            ret = s->TCTR;
            rtl8139_log("TCTR Timer read val=0x%08x\n", ret);
            break;

        case FlashReg:
            ret = s->TimerInt;
            rtl8139_log("FlashReg TimerInt read val=0x%08x\n", ret);
            break;

        default:
            rtl8139_log("ioport read(l) addr=0x%x via read(b)\n", addr);

            ret = rtl8139_io_readb(addr, priv);
            ret |= rtl8139_io_readb(addr + 1, priv) << 8;
            ret |= rtl8139_io_readb(addr + 2, priv) << 16;
            ret |= rtl8139_io_readb(addr + 3, priv) << 24;

            rtl8139_log("read(l) addr=0x%x val=%08x\n", addr, ret);
            break;
    }

    return ret;
}

static uint32_t
rtl8139_io_readl_ioport(uint16_t addr, void *priv)
{
    uint32_t ret = 0xffffffff;

    ret = rtl8139_io_readl(addr, priv);

    rtl8139_log("[%04X:%08X] [RLI] %04X = %08X\n", CS, cpu_state.pc, addr, ret);

    return ret;
}

static uint16_t
rtl8139_io_readw_ioport(uint16_t addr, void *priv)
{
    uint16_t ret = 0xffff;

    ret = rtl8139_io_readw(addr, priv);

    rtl8139_log("[%04X:%08X] [RWI] %04X = %04X\n", CS, cpu_state.pc, addr, ret);

    return ret;
}

static uint8_t
rtl8139_io_readb_ioport(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;

    ret = rtl8139_io_readb(addr, priv);

    rtl8139_log("[%04X:%08X] [RBI] %04X = %02X\n", CS, cpu_state.pc, addr, ret);

    return ret;
}

static void
rtl8139_io_writel_ioport(uint16_t addr, uint32_t val, void *priv)
{
    rtl8139_log("[%04X:%08X] [WLI] %04X = %08X\n", CS, cpu_state.pc, addr, val);

    rtl8139_io_writel(addr, val, priv);
}

static void
rtl8139_io_writew_ioport(uint16_t addr, uint16_t val, void *priv)
{
    rtl8139_log("[%04X:%08X] [WWI] %04X = %04X\n", CS, cpu_state.pc, addr, val);

    rtl8139_io_writew(addr, val, priv);
}

static void
rtl8139_io_writeb_ioport(uint16_t addr, uint8_t val, void *priv)
{
    rtl8139_log("[%04X:%08X] [WBI] %04X = %02X\n", CS, cpu_state.pc, addr, val);

    rtl8139_io_writeb(addr, val, priv);
}

static uint32_t
rtl8139_io_readl_mem(uint32_t addr, void *priv)
{
    RTL8139State *s = (RTL8139State *) priv;
    uint32_t ret = 0xffffffff;

    if ((addr >= s->mem_base) && (addr < (s->mem_base + 0xff)))
        ret = rtl8139_io_readl(addr, priv);

    rtl8139_log("[%04X:%08X] [RLM] %08X = %08X\n", CS, cpu_state.pc, addr, ret);

    return ret;
 }

static uint16_t
rtl8139_io_readw_mem(uint32_t addr, void *priv)
{
    RTL8139State *s = (RTL8139State *) priv;
    uint16_t ret = 0xffff;

    if ((addr >= s->mem_base) && (addr < (s->mem_base + 0xff)))
        ret = rtl8139_io_readw(addr, priv);

    rtl8139_log("[%04X:%08X] [RWM] %08X = %04X\n", CS, cpu_state.pc, addr, ret);

    return ret;
}

static uint8_t
rtl8139_io_readb_mem(uint32_t addr, void *priv)
{
    RTL8139State *s = (RTL8139State *) priv;
    uint8_t ret = 0xff;

    if ((addr >= s->mem_base) && (addr < (s->mem_base + 0xff)))
        ret = rtl8139_io_readb(addr, priv);

    rtl8139_log("[%04X:%08X] [RBM] %08X = %02X\n", CS, cpu_state.pc, addr, ret);

    return ret;
}

static void
rtl8139_io_writel_mem(uint32_t addr, uint32_t val, void *priv)
{
    RTL8139State *s = (RTL8139State *) priv;

    rtl8139_log("[%04X:%08X] [WLM] %08X = %08X\n", CS, cpu_state.pc, addr, val);

    if ((addr >= s->mem_base) && (addr < (s->mem_base + 0xff)))
        rtl8139_io_writel(addr, val, priv);
}

static void
rtl8139_io_writew_mem(uint32_t addr, uint16_t val, void *priv)
{
    RTL8139State *s = (RTL8139State *) priv;

    rtl8139_log("[%04X:%08X] [WWM] %08X = %04X\n", CS, cpu_state.pc, addr, val);

    if ((addr >= s->mem_base) && (addr < (s->mem_base + 0xff)))
        rtl8139_io_writew(addr, val, priv);
}

static void
rtl8139_io_writeb_mem(uint32_t addr, uint8_t val, void *priv)
{
    RTL8139State *s = (RTL8139State *) priv;

    rtl8139_log("[%04X:%08X] [WBM] %08X = %02X\n", CS, cpu_state.pc, addr, val);

    if ((addr >= s->mem_base) && (addr < (s->mem_base + 0xff)))
        rtl8139_io_writeb(addr, val, priv);
}

static int
rtl8139_set_link_status(void *priv, uint32_t link_state)
{
    RTL8139State *s = (RTL8139State *) priv;

    if (link_state & NET_LINK_DOWN) {
        s->BasicModeStatus &= ~0x04;
    } else {
        s->BasicModeStatus |= 0x04;
    }

    s->IntrStatus |= RxUnderrun;
    rtl8139_update_irq(s);
    return 0;
}

static void
rtl8139_timer(void *priv)
{
    RTL8139State *s = priv;

    timer_on_auto(&s->timer, 1000000.0 / ((double) cpu_pci_speed));

    if (!s->clock_enabled) {
        rtl8139_log(">>> timer: clock is not running\n");
        return;
    }

    s->TCTR++;

    if (s->TCTR == s->TimerInt && s->TimerInt != 0) {
        s->IntrStatus |= PCSTimeout;
        rtl8139_update_irq(s);
    }
}

static uint8_t
rtl8139_pci_read(UNUSED(int func), int addr, void *priv)
{
    const RTL8139State *s = (RTL8139State *) priv;

    switch (addr) {
        default:
            return s->pci_conf[addr & 0xFF];
        case 0x00:
            return 0xEC;
        case 0x01:
            return 0x10;
        case 0x02:
            return 0x39;
        case 0x03:
            return 0x81;
        case 0x07:
            return 0x02;
        case 0x06:
            return 0x80;
        case 0x05:
            return s->pci_conf[addr & 0xFF] & 1;
        case 0x08:
            return RTL8139_PCI_REVID;
        case 0x09:
            return 0x0;
        case 0x0a:
            return 0x0;
        case 0x0b:
            return 0x2;
        case 0x0d:
            return s->pci_latency;
        case 0x10:
            return 1;
        case 0x14:
            return 0;
        case 0x15:
#ifdef USE_256_BYTE_BAR
            return s->pci_conf[addr & 0xFF];
#else
            return s->pci_conf[addr & 0xFF] & 0xf0;
#endif
        case 0x2c:
            return 0xEC;
        case 0x2d:
            return 0x10;
        case 0x2e:
            return 0x39;
        case 0x2f:
            return 0x81;
        case 0x34:
            return 0xdc;
        case 0x3d:
            return PCI_INTA;
    }
}

static void
rtl8139_pci_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    RTL8139State *s = (RTL8139State *) priv;

    switch (addr) {
        case 0x04:
            mem_mapping_disable(&s->bar_mem);
            io_removehandler((s->pci_conf[0x11] << 8), 256,
                             rtl8139_io_readb_ioport, rtl8139_io_readw_ioport, rtl8139_io_readl_ioport,
                             rtl8139_io_writeb_ioport, rtl8139_io_writew_ioport, rtl8139_io_writel_ioport,
                             priv);
            s->pci_conf[addr & 0xFF] = val;
            if (val & PCI_COMMAND_IO)
                io_sethandler((s->pci_conf[0x11] << 8), 256,
                              rtl8139_io_readb_ioport, rtl8139_io_readw_ioport, rtl8139_io_readl_ioport,
                              rtl8139_io_writeb_ioport, rtl8139_io_writew_ioport, rtl8139_io_writel_ioport,
                              priv);
            if ((val & PCI_COMMAND_MEM) && s->bar_mem.size)
                mem_mapping_enable(&s->bar_mem);
            break;
        case 0x05:
            s->pci_conf[addr & 0xFF] = val & 1;
            break;
        case 0x0c:
            s->pci_conf[addr & 0xFF] = val;
            break;
        case 0x0d:
            s->pci_latency = val;
            break;
        case 0x10:
        case 0x11:
            io_removehandler((s->pci_conf[0x11] << 8), 256,
                             rtl8139_io_readb_ioport, rtl8139_io_readw_ioport, rtl8139_io_readl_ioport,
                             rtl8139_io_writeb_ioport, rtl8139_io_writew_ioport, rtl8139_io_writel_ioport,
                             priv);
            s->pci_conf[addr & 0xFF] = val;
            rtl8139_log("New I/O base: %04X\n", s->pci_conf[0x11] << 8);
            if (s->pci_conf[0x4] & PCI_COMMAND_IO)
                io_sethandler((s->pci_conf[0x11] << 8), 256,
                              rtl8139_io_readb_ioport, rtl8139_io_readw_ioport, rtl8139_io_readl_ioport,
                              rtl8139_io_writeb_ioport, rtl8139_io_writew_ioport, rtl8139_io_writel_ioport,
                              priv);
            break;
#ifndef USE_256_BYTE_BAR
        case 0x14:
#endif
        case 0x15:
        case 0x16:
        case 0x17:
            s->pci_conf[addr & 0xFF] = val;
            s->mem_base = (s->pci_conf[0x15] << 8) | (s->pci_conf[0x16] << 16) |
                          (s->pci_conf[0x17] << 24);
#ifndef USE_256_BYTE_BAR
            s->mem_base &= 0xfffff000;
#endif
            rtl8139_log("New memory base: %08X\n", s->mem_base);
            if (s->pci_conf[0x4] & PCI_COMMAND_MEM)
#ifdef USE_256_BYTE_BAR
                mem_mapping_set_addr(&s->bar_mem, (s->pci_conf[0x15] << 8) | (s->pci_conf[0x16] << 16) |
                                     (s->pci_conf[0x17] << 24), 256);
#else
                mem_mapping_set_addr(&s->bar_mem, ((s->pci_conf[0x15] & 0xf0) << 8) |
                                     (s->pci_conf[0x16] << 16) | (s->pci_conf[0x17] << 24), 4096);
#endif
            break;
        case 0x3c:
            s->pci_conf[addr & 0xFF] = val;
            break;
    }
}

static void *
nic_init(const device_t *info)
{
    RTL8139State *s                     = calloc(1, sizeof(RTL8139State));
    nmc93cxx_eeprom_params_t params;
    char          eeprom_filename[1024] = { 0 };
    char          filename[1024] = { 0 };
    uint8_t      *mac_bytes;
    uint16_t     *eep_data;
    uint32_t      mac;

    mem_mapping_add(&s->bar_mem, 0, 0,
                    rtl8139_io_readb_mem, rtl8139_io_readw_mem, rtl8139_io_readl_mem,
                    rtl8139_io_writeb_mem, rtl8139_io_writew_mem, rtl8139_io_writel_mem,
                    NULL, MEM_MAPPING_EXTERNAL, s);
    pci_add_card(PCI_ADD_NORMAL, rtl8139_pci_read, rtl8139_pci_write, s, &s->pci_slot);
    s->inst = device_get_instance();

    snprintf(eeprom_filename, sizeof(eeprom_filename), "eeprom_rtl8139c_plus_%d.nvr", s->inst);

    eep_data = (uint16_t *) s->eeprom_data;

    /* prepare eeprom */
    eep_data[0] = 0x8129;

    /* PCI vendor and device ID should be mirrored here */
    eep_data[1] = 0x10EC;
    eep_data[2] = 0x8139;

    /* XXX: Get proper MAC addresses from real EEPROM dumps. OID taken from net_ne2000.c */
#ifdef USE_REALTEK_OID
    eep_data[7] = 0xe000;
    eep_data[8] = 0x124c;
#else
    eep_data[7] = 0x1400;
    eep_data[8] = 0x122a;
#endif
    eep_data[9] = 0x1413;

    mac_bytes = (uint8_t *) &(eep_data[7]);

    /* See if we have a local MAC address configured. */
    mac = device_get_config_mac("mac", -1);

    /* Set up our BIA. */
    if (mac & 0xff000000) {
        /* Generate new local MAC. */
        mac_bytes[3] = random_generate();
        mac_bytes[4] = random_generate();
        mac_bytes[5] = random_generate();
        mac              = (((int) mac_bytes[3]) << 16);
        mac             |= (((int) mac_bytes[4]) << 8);
        mac             |= ((int) mac_bytes[5]);
        device_set_config_mac("mac", mac);
    } else {
        mac_bytes[3] = (mac >> 16) & 0xff;
        mac_bytes[4] = (mac >> 8) & 0xff;
        mac_bytes[5] = (mac & 0xff);
    }

    for (uint32_t i = 0; i < 6; i++)
        s->phys[MAC0 + i] = mac_bytes[i];

    params.nwords          = 64;
    params.default_content = (uint16_t *) s->eeprom_data;
    params.filename        = filename;
    snprintf(filename, sizeof(filename), "nmc93cxx_eeprom_%s_%d.nvr", info->internal_name, device_get_instance());
    s->eeprom = device_add_params(&nmc93cxx_device, &params);
    if (s->eeprom == NULL) {
        free(s);
        return NULL;
    }

    s->nic = network_attach(s, (uint8_t *) &s->phys[MAC0], rtl8139_do_receive, rtl8139_set_link_status);
    timer_add(&s->timer, rtl8139_timer, s, 0);
    timer_on_auto(&s->timer, 1000000.0 / cpu_pci_speed);

    s->cplus_txbuffer        = NULL;
    s->cplus_txbuffer_len    = 0;
    s->cplus_txbuffer_offset = 0;

    return s;
}

static void
nic_close(void *priv)
{
    free(priv);
}

// clang-format off
static const device_config_t rtl8139c_config[] = {
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

const device_t rtl8139c_plus_device = {
    .name          = "Realtek RTL8139C+",
    .internal_name = "rtl8139c+",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = rtl8139_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = rtl8139c_config
};
