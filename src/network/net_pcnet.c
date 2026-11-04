/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the AMD PCnet LANCE NIC controller for both the ISA, VLB,
 *          and PCI buses.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          Antony T Curtis
 *
 *          Copyright 2004-2019 Antony T Curtis
 *          Copyright 2016-2019 Miran Grca.
 */
#ifdef _WIN32
#    include <winsock.h>
#else
#    include <arpa/inet.h>
#endif
#include <inttypes.h>
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
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include "cpu.h"
#include <86box/random.h>
#include <86box/device.h>
#include <86box/isapnp.h>
#include <86box/nmc93cxx.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/acpi.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/net_pcnet.h>
#include <86box/scsi_pcscsi.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include <86box/bswap.h>

/* PCI info. */
#define PCI_VENDID  0x1022 /* AMD */
#define PCI_DEVID   0x2000 /* PCnet-PCI II (Am79c970A) */
#define PCI_REGSIZE 256    /* size of PCI space */

#pragma pack(1)
typedef struct RTNETETHERHDR {
    uint8_t DstMac[6];
    uint8_t SrcMac[6];
    /** Ethernet frame type or frame size, depending on the kind of ethernet.
     * This is big endian on the wire. */
    uint16_t EtherType;
} RTNETETHERHDR;
#pragma pack()

#define BCR_MAX_RAP 56
#define MII_MAX_REG 32
#define CSR_MAX_REG 128

/** Maximum number of times we report a link down to the guest (failure to send frame) */
#define PCNET_MAX_LINKDOWN_REPORTED 3

/** Maximum frame size we handle */
#define MAX_FRAME 1536

/** @name Bus configuration registers
 * @{ */
#define BCR_MSRDA     0
#define BCR_MSWRA     1
#define BCR_MC        2
#define BCR_RESERVED3 3
#define BCR_LNKST     4
#define BCR_LED1      5
#define BCR_LED2      6
#define BCR_LED3      7
#define BCR_SWCONFIG  8
#define BCR_FDC       9
/* 10 - 15 = reserved */
#define BCR_IOBASEL 16 /* Reserved but on the Am79C965A */
#define BCR_IOBASEU 17 /* Reserved but on the Am79C965A (it was 16) */
#define BCR_BSBC    18
#define BCR_EECAS   19
#define BCR_SWS     20
#define BCR_INTCON  21 /* Reserved but on the Am79C965A */
#define BCR_PLAT    22
#define BCR_PCISVID 23
#define BCR_PCISID  24
#define BCR_SRAMSIZ 25
#define BCR_SRAMB   26
#define BCR_SRAMIC  27
#define BCR_EBADDRL 28
#define BCR_EBADDRU 29
#define BCR_EBD     30
#define BCR_STVAL   31
#define BCR_MIICAS  32
#define BCR_MIIADDR 33
#define BCR_MIIMDR  34
#define BCR_PCIVID  35
#define BCR_PMC_A   36
#define BCR_DATA0   37
#define BCR_DATA1   38
#define BCR_DATA2   39
#define BCR_DATA3   40
#define BCR_DATA4   41
#define BCR_DATA5   42
#define BCR_DATA6   43
#define BCR_DATA7   44
#define BCR_PMR1    45
#define BCR_PMR2    46
#define BCR_PMR3    47
/** @}  */

/** @name Bus configuration sub register accessors.
 * @{ */
#define BCR_DWIO(S)    !!((S)->aBCR[BCR_BSBC] & 0x0080)
#define BCR_SSIZE32(S) !!((S)->aBCR[BCR_SWS] & 0x0100)
#define BCR_SWSTYLE(S) ((S)->aBCR[BCR_SWS] & 0x00FF)
/** @} */

/** @name CSR subregister accessors.
 * @{ */
#define CSR_INIT(S)      !!((S)->aCSR[0] & 0x0001) /**< Init assertion */
#define CSR_STRT(S)      !!((S)->aCSR[0] & 0x0002) /**< Start assertion */
#define CSR_STOP(S)      !!((S)->aCSR[0] & 0x0004) /**< Stop assertion */
#define CSR_TDMD(S)      !!((S)->aCSR[0] & 0x0008) /**< Transmit demand. (perform xmit poll now (readable, settable, not clearable) */
#define CSR_TXON(S)      !!((S)->aCSR[0] & 0x0010) /**< Transmit on (readonly) */
#define CSR_RXON(S)      !!((S)->aCSR[0] & 0x0020) /**< Receive On */
#define CSR_INEA(S)      !!((S)->aCSR[0] & 0x0040) /**< Interrupt Enable */
#define CSR_LAPPEN(S)    !!((S)->aCSR[3] & 0x0020) /**< Look Ahead Packet Processing Enable */
#define CSR_DXSUFLO(S)   !!((S)->aCSR[3] & 0x0040) /**< Disable Transmit Stop on Underflow error */
#define CSR_ASTRP_RCV(S) !!((S)->aCSR[4] & 0x0400) /**< Auto Strip Receive */
#define CSR_DPOLL(S)     !!((S)->aCSR[4] & 0x1000) /**< Disable Transmit Polling */
#define CSR_SPND(S)      !!((S)->aCSR[5] & 0x0001) /**< Suspend */
#define CSR_LTINTEN(S)   !!((S)->aCSR[5] & 0x4000) /**< Last Transmit Interrupt Enable */
#define CSR_TOKINTD(S)   !!((S)->aCSR[5] & 0x8000) /**< Transmit OK Interrupt Disable */

#define CSR_STINT        !!((S)->aCSR[7] & 0x0800) /**< Software Timer Interrupt */
#define CSR_STINTE       !!((S)->aCSR[7] & 0x0400) /**< Software Timer Interrupt Enable */

#define CSR_DRX(S)       !!((S)->aCSR[15] & 0x0001) /**< Disable Receiver */
#define CSR_DTX(S)       !!((S)->aCSR[15] & 0x0002) /**< Disable Transmit */
#define CSR_LOOP(S)      !!((S)->aCSR[15] & 0x0004) /**< Loopback Enable */
#define CSR_DRCVPA(S)    !!((S)->aCSR[15] & 0x2000) /**< Disable Receive Physical Address */
#define CSR_DRCVBC(S)    !!((S)->aCSR[15] & 0x4000) /**< Disable Receive Broadcast */
#define CSR_PROM(S)      !!((S)->aCSR[15] & 0x8000) /**< Promiscuous Mode */

/** @name CSR register accessors.
 * @{ */
#define CSR_IADR(S)  (*(uint32_t *) ((S)->aCSR + 1))  /**< Initialization Block Address */
#define CSR_CRBA(S)  (*(uint32_t *) ((S)->aCSR + 18)) /**< Current Receive Buffer Address */
#define CSR_CXBA(S)  (*(uint32_t *) ((S)->aCSR + 20)) /**< Current Transmit Buffer Address */
#define CSR_NRBA(S)  (*(uint32_t *) ((S)->aCSR + 22)) /**< Next Receive Buffer Address */
#define CSR_BADR(S)  (*(uint32_t *) ((S)->aCSR + 24)) /**< Base Address of Receive Ring */
#define CSR_NRDA(S)  (*(uint32_t *) ((S)->aCSR + 26)) /**< Next Receive Descriptor Address */
#define CSR_CRDA(S)  (*(uint32_t *) ((S)->aCSR + 28)) /**< Current Receive Descriptor Address */
#define CSR_BADX(S)  (*(uint32_t *) ((S)->aCSR + 30)) /**< Base Address of Transmit Descriptor */
#define CSR_NXDA(S)  (*(uint32_t *) ((S)->aCSR + 32)) /**< Next Transmit Descriptor Address */
#define CSR_CXDA(S)  (*(uint32_t *) ((S)->aCSR + 34)) /**< Current Transmit Descriptor Address */
#define CSR_NNRD(S)  (*(uint32_t *) ((S)->aCSR + 36)) /**< Next Next Receive Descriptor Address */
#define CSR_NNXD(S)  (*(uint32_t *) ((S)->aCSR + 38)) /**< Next Next Transmit Descriptor Address */
#define CSR_CRBC(S)  ((S)->aCSR[40])                  /**< Current Receive Byte Count */
#define CSR_CRST(S)  ((S)->aCSR[41])                  /**< Current Receive Status */
#define CSR_CXBC(S)  ((S)->aCSR[42])                  /**< Current Transmit Byte Count */
#define CSR_CXST(S)  ((S)->aCSR[43])                  /**< Current transmit status */
#define CSR_NRBC(S)  ((S)->aCSR[44])                  /**< Next Receive Byte Count */
#define CSR_NRST(S)  ((S)->aCSR[45])                  /**< Next Receive Status */
#define CSR_POLL(S)  ((S)->aCSR[46])                  /**< Transmit Poll Time Counter */
#define CSR_PINT(S)  ((S)->aCSR[47])                  /**< Transmit Polling Interval */
#define CSR_PXDA(S)  (*(uint32_t *) ((S)->aCSR + 60)) /**< Previous Transmit Descriptor Address*/
#define CSR_PXBC(S)  ((S)->aCSR[62])                  /**< Previous Transmit Byte Count */
#define CSR_PXST(S)  ((S)->aCSR[63])                  /**< Previous Transmit Status */
#define CSR_NXBA(S)  (*(uint32_t *) ((S)->aCSR + 64)) /**< Next Transmit Buffer Address */
#define CSR_NXBC(S)  ((S)->aCSR[66])                  /**< Next Transmit Byte Count */
#define CSR_NXST(S)  ((S)->aCSR[67])                  /**< Next Transmit Status */
#define CSR_RCVRC(S) ((S)->aCSR[72])                  /**< Receive Descriptor Ring Counter */
#define CSR_XMTRC(S) ((S)->aCSR[74])                  /**< Transmit Descriptor Ring Counter */
#define CSR_RCVRL(S) ((S)->aCSR[76])                  /**< Receive Descriptor Ring Length */
#define CSR_XMTRL(S) ((S)->aCSR[78])                  /**< Transmit Descriptor Ring Length */
#define CSR_MISSC(S) ((S)->aCSR[112])                 /**< Missed Frame Count */

/** Calculates the full physical address.  */
#define PHYSADDR(S, A) ((A) | (S)->GCUpperPhys)

/* Resources as in the Am79C961 data sheet's sample configuration record (Appendix C):
   DMA 3/5/6/7 16-bit bus master, IRQ 3/4/5/9/10/11/12/15 high true edge. The card and logical
   device IDs and the vendor register flags stay as they were, so installed guests keep their driver. */
static const uint8_t am79c961_pnp_rom[] = {
    0x04, 0x96, 0x55, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00,                                                                                                         /* ADV55AA, dummy checksum (filled in by isapnp_add_card) */
    0x0a, 0x10, 0x00,                                                                                                                                             /* PnP version 1.0, vendor version 0.0 */
    0x82, 0x1c, 0x00, 'A', 'M', 'D', ' ', 'E', 't', 'h', 'e', 'r', 'n', 'e', 't', ' ', 'N', 'e', 't', 'w', 'o', 'r', 'k', ' ', 'A', 'd', 'a', 'p', 't', 'e', 'r', /* ANSI identifier */

    0x16, 0x04, 0x96, 0x55, 0xaa, 0x00, 0xbd,       /* logical device ADV55AA, supports vendor-specific registers 0x38/0x3A/0x3B/0x3C/0x3D/0x3F */
    0x1c, 0x41, 0xd0, 0x82, 0x8c,                   /* compatible device PNP828C */
    0x47, 0x00, 0x00, 0x02, 0xe0, 0x03, 0x20, 0x18, /* I/O 0x200-0x3E0, decodes 10-bit, 32-byte alignment, 24 addresses */
    0x2a, 0xe8, 0x06,                               /* DMA 3/5/6/7, compatibility, bus master, 16-bit only */
    0x23, 0x38, 0x9e, 0x01,                         /* IRQ 3/4/5/9/10/11/12/15, high true edge sensitive */

    0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
};

/* The PCnet-ISA II (Am79C961A), from its data sheet's sample configuration record
   (Appendix C): card ADV2100, logical device ADV55AA (I/O range check) compatible with PNP828C,
   DMA 8/16-bit bus master, IRQ high true edge or low true level. The serial number (bytes 4-7)
   is set from the MAC address at init. */
static const uint8_t am79c961a_pnp_rom[] = {
    0x04, 0x96, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ADV2100, serial, dummy checksum (filled in by isapnp_add_card) */
    0x0a, 0x10, 0x00,                                     /* PnP version 1.0, vendor version 0.0 */
    0x82, 0x29, 0x00, 'A', 'M', 'D', ' ', 'P', 'C', 'n', 'e', 't', '-', 'I', 'S', 'A', ' ', 'I', 'I', ' ',
    'E', 't', 'h', 'e', 'r', 'n', 'e', 't', ' ', 'N', 'e', 't', 'w', 'o', 'r', 'k', ' ', 'A', 'd', 'a', 'p', 't', 'e', 'r', /* ANSI identifier */

    0x15, 0x04, 0x96, 0x55, 0xaa, 0x02,             /* logical device ADV55AA, I/O range check */
    0x1c, 0x41, 0xd0, 0x82, 0x8c,                   /* compatible device PNP828C */
    0x47, 0x00, 0x00, 0x02, 0xe0, 0x03, 0x20, 0x18, /* I/O 0x200-0x3E0, decodes 10-bit, 32-byte alignment, 24 addresses */
    0x2a, 0xe8, 0x05,                               /* DMA 3/5/6/7, compatibility, bus master, 8- and 16-bit */
    0x23, 0x38, 0x9e, 0x09,                         /* IRQ 3/4/5/9/10/11/12/15, low true level sensitive, high true edge sensitive */

    0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
};

/* The serial EEPROM's map (93C46, auto-loaded after H_RESET and on BCR19 PREAD): bytes
   00h-0Fh are the Address PROM image, then from word `first` one register per word (`regs`: a
   BCR, PCNET_EE_CSR | n a CSR, PCNET_EE_SKIP a word that is not loaded). The 8-bit sum of the
   first `bytes` bytes must be FFh; `adjust` is the checksum adjust byte. */
#define PCNET_EE_CSR  0x100
#define PCNET_EE_SKIP -1

typedef struct pcnet_eeprom_map_t {
    uint8_t        bytes;
    uint8_t        adjust;
    uint8_t        first;
    uint8_t        nregs;
    int8_t         csr116_word; /* word 03h: CSR116 on the Am79C973, -1 elsewhere */
    const int16_t *regs;
} pcnet_eeprom_map_t;

/* Am79C965A Table 39: BCR16/17 (I/O base) are not loaded : the configured base wins. */
static const int16_t pcnet_ee_regs_965[] = { PCNET_EE_SKIP, PCNET_EE_SKIP, 18, 2, 21 };
/* Am79C970A Table 11 (word 0Fh: reserved byte + checksum adjust). */
static const int16_t pcnet_ee_regs_970a[] = { 4, 5, 18, 2, 6, 7, 9, PCNET_EE_SKIP, 22 };
/* Am79C973 Table 13 (word 28h: BCR55[7:0] + checksum adjust). */
static const int16_t pcnet_ee_regs_973[] = { 2, 4, 5, 6, 7, 9, 18, 22, 23, 24, 25, 26, 27, 32, 33, 35,
                                             36, 37, 38, 39, 40, 41, 42, 43, 44,
                                             48, 49, 50, 51, 52, 53, 54, 55 };

/* Am79C971 Table 13 (words 18h-1Fh reserved; byte 3Fh the checksum adjust). */
static const int16_t pcnet_ee_regs_971[] = { 2, 4, 5, 6, 7, 9, 18, 22, 23, 24, 25, 26, 27, 32, 33, 35 };
/* Am79C972 Table 10 (word 21h: reserved byte + checksum adjust). */
static const int16_t pcnet_ee_regs_972[] = { 2, 4, 5, 6, 7, 9, 18, 22, 23, 24, 25, 26, 27, 32, 33, 35,
                                             36, 37, 38, 39, 40, 41, 42, 43, 44 };

static const pcnet_eeprom_map_t pcnet_ee_map_965  = { 36, 0x1f, 8, 5, -1, pcnet_ee_regs_965 };
static const pcnet_eeprom_map_t pcnet_ee_map_970a = { 36, 0x1f, 8, 9, -1, pcnet_ee_regs_970a };
static const pcnet_eeprom_map_t pcnet_ee_map_971  = { 64, 0x3f, 8, 16, -1, pcnet_ee_regs_971 };
static const pcnet_eeprom_map_t pcnet_ee_map_972  = { 68, 0x43, 8, 25, 3, pcnet_ee_regs_972 };
/* The Am79C975's (Table 14) is the same: it loads BCR48-55 into its SMIU. */
static const pcnet_eeprom_map_t pcnet_ee_map_973  = { 82, 0x51, 8, 33, 3, pcnet_ee_regs_973 };

typedef struct {
    mem_mapping_t mmio_mapping;
    const char   *name;
    int           board;
    int           is_pci, is_vlb, is_isa;
    int           PCIBase;
    int           MMIOBase;
    uint32_t      base_address;
    int           base_irq;
    int           dma_channel;
    uint8_t       pci_slot; /* PCI card slot */
    uint8_t       irq_state;
    int           xmit_pos;
    /** Register Address Pointer */
    uint32_t u32RAP;
    /** Internal interrupt service */
    int32_t iISR;
    /** ??? */
    uint32_t u32Lnkst;
    /** Address of the RX descriptor table (ring). Loaded at init. */
    uint32_t GCRDRA;
    /** Address of the TX descriptor table (ring). Loaded at init. */
    uint32_t GCTDRA;
    uint8_t  aPROM[256];
    uint16_t aCSR[CSR_MAX_REG];
    uint16_t aBCR[BCR_MAX_RAP];
    uint16_t aMII[MII_MAX_REG];
    /** The loopback transmit buffer (avoid stack allocations). */
    uint8_t abLoopBuf[4096];
    /** The recv buffer. */
    uint8_t abRecvBuf[4096];
    /** Size of a RX/TX descriptor (8 or 16 bytes according to SWSTYLE */
    int iLog2DescSize;
    /** Bits 16..23 in 16-bit mode */
    uint32_t GCUpperPhys;
    /** We are waiting/about to start waiting for more receive buffers. */
    int fMaybeOutOfSpace;
    /** True if we signal the guest that RX packets are missing. */
    int fSignalRxMiss;
    /** Link speed to be reported through CSR68. */
    uint32_t u32LinkSpeed;
    /** Error counter for bad receive descriptors. */
    uint32_t uCntBadRMD;
    uint16_t u16CSR0LastSeenByGuest;
    /** If set the link is currently up. */
    int fLinkUp;
    /** If set the link is temporarily down because of a saved state load. */
    int fLinkTempDown;
    /** Number of times we've reported the link down. */
    uint32_t cLinkDownReported;
    /** MS to wait before we enable the link. */
    uint32_t   cMsLinkUpDelay;
    int        transfer_size;
    uint8_t    maclocal[6]; /* configured MAC (local) address */
    pc_timer_t timer, timer_soft_int, timer_restore;
    netcard_t *netcard;
    /* The serial EEPROM (NULL on chips without one), its map, and its default contents --
       the H_RESET register values, also loaded when the checksum fails. */
    nmc93cxx_eeprom_t        *eeprom;
    const pcnet_eeprom_map_t *eeprom_map;
    uint16_t                  eeprom_default[64];
    /* The PCnet-ISA II's EEPROM word 10h, MISC Config 2 (ISACSR9), loaded at RESET. */
    uint16_t isa2_dup;

    /* PCI configuration space, per card (it was shared by all PCI cards). */
    uint8_t   pci_regs[PCI_REGSIZE];
    pci_bar_t pci_bar[2];
    /* PCI power management (Am79C972/973/975): PMCSR and the OnNow Pattern
       Match RAM (64 words of 40 bits, BCR45-47) with its pending write. */
    uint16_t pmcsr;
    uint8_t  pmr[64][5];
    uint8_t  pmr_wr[5];
    /* The PCnet-32 (Am79C965A): where its 32 bytes of I/O are decoded (-1: nowhere),
       the interrupt mode last driven (BCR2 INTLEVEL), and Software Relocatable Mode (no
       I/O decode; the position in the 378h write sequence that ends it). */
    int     vlb_io_base;
    int     irq_level;
    int     srm;
    uint8_t srm_pos;
} nic_t;

/** @todo All structs: big endian? */

struct INITBLK16 {
    uint16_t mode;      /**< copied into csr15 */
    uint16_t padr1;     /**< MAC  0..15 */
    uint16_t padr2;     /**< MAC 16..32 */
    uint16_t padr3;     /**< MAC 33..47 */
    uint16_t ladrf1;    /**< logical address filter  0..15 */
    uint16_t ladrf2;    /**< logical address filter 16..31 */
    uint16_t ladrf3;    /**< logical address filter 32..47 */
    uint16_t ladrf4;    /**< logical address filter 48..63 */
    uint32_t rdra : 24; /**< address of receive descriptor ring */
    uint32_t res1 : 5;  /**< reserved */
    uint32_t rlen : 3;  /**< number of receive descriptor ring entries */
    uint32_t tdra : 24; /**< address of transmit descriptor ring */
    uint32_t res2 : 5;  /**< reserved */
    uint32_t tlen : 3;  /**< number of transmit descriptor ring entries */
};

/** bird:  I've changed the type for the bitfields. They should only be 16-bit all together.
 *  frank: I've changed the bitfiled types to uint32_t to prevent compiler warnings. */
struct INITBLK32 {
    uint16_t mode;     /**< copied into csr15 */
    uint16_t res1 : 4; /**< reserved */
    uint16_t rlen : 4; /**< number of receive descriptor ring entries */
    uint16_t res2 : 4; /**< reserved */
    uint16_t tlen : 4; /**< number of transmit descriptor ring entries */
    uint16_t padr1;    /**< MAC  0..15 */
    uint16_t padr2;    /**< MAC 16..31 */
    uint16_t padr3;    /**< MAC 32..47 */
    uint16_t res3;     /**< reserved */
    uint16_t ladrf1;   /**< logical address filter  0..15 */
    uint16_t ladrf2;   /**< logical address filter 16..31 */
    uint16_t ladrf3;   /**< logical address filter 32..47 */
    uint16_t ladrf4;   /**< logical address filter 48..63 */
    uint32_t rdra;     /**< address of receive descriptor ring */
    uint32_t tdra;     /**< address of transmit descriptor ring */
};

/** Transmit Message Descriptor */
typedef struct TMD {
    struct
    {
        uint32_t tbadr; /**< transmit buffer address */
    } tmd0;
    struct
    {
        uint32_t bcnt  : 12; /**< buffer byte count (two's complement) */
        uint32_t ones  : 4;  /**< must be 1111b */
        uint32_t res   : 7;  /**< reserved */
        uint32_t bpe   : 1;  /**< bus parity error */
        uint32_t enp   : 1;  /**< end of packet */
        uint32_t stp   : 1;  /**< start of packet */
        uint32_t def   : 1;  /**< deferred */
        uint32_t one   : 1;  /**< exactly one retry was needed to transmit a frame */
        uint32_t ltint : 1;  /**< suppress interrupts after successful transmission */
        uint32_t nofcs : 1;  /**< when set, the state of DXMTFCS is ignored and
                                  transmitter FCS generation is activated. */
        uint32_t err : 1;    /**< error occurred */
        uint32_t own : 1;    /**< 0=owned by guest driver, 1=owned by controller */
    } tmd1;
    struct
    {
        uint32_t trc   : 4;  /**< transmit retry count */
        uint32_t res   : 12; /**< reserved */
        uint32_t tdr   : 10; /**< ??? */
        uint32_t rtry  : 1;  /**< retry error */
        uint32_t lcar  : 1;  /**< loss of carrier */
        uint32_t lcol  : 1;  /**< late collision */
        uint32_t exdef : 1;  /**< excessive deferral */
        uint32_t uflo  : 1;  /**< underflow error */
        uint32_t buff  : 1;  /**< out of buffers (ENP not found) */
    } tmd2;
    struct
    {
        uint32_t res; /**< reserved for user defined space */
    } tmd3;
} TMD;

/** Receive Message Descriptor */
typedef struct RMD {
    struct
    {
        uint32_t rbadr; /**< receive buffer address */
    } rmd0;
    struct
    {
        uint32_t bcnt : 12; /**< buffer byte count (two's complement) */
        uint32_t ones : 4;  /**< must be 1111b */
        uint32_t res  : 4;  /**< reserved */
        uint32_t bam  : 1;  /**< broadcast address match */
        uint32_t lafm : 1;  /**< logical filter address match */
        uint32_t pam  : 1;  /**< physical address match */
        uint32_t bpe  : 1;  /**< bus parity error */
        uint32_t enp  : 1;  /**< end of packet */
        uint32_t stp  : 1;  /**< start of packet */
        uint32_t buff : 1;  /**< buffer error */
        uint32_t crc  : 1;  /**< crc error on incoming frame */
        uint32_t oflo : 1;  /**< overflow error (lost all or part of incoming frame) */
        uint32_t fram : 1;  /**< frame error */
        uint32_t err  : 1;  /**< error occurred */
        uint32_t own  : 1;  /**< 0=owned by guest driver, 1=owned by controller */
    } rmd1;
    struct
    {
        uint32_t mcnt  : 12; /**< message byte count */
        uint32_t zeros : 4;  /**< 0000b */
        uint32_t rpc   : 8;  /**< receive frame tag */
        uint32_t rcc   : 8;  /**< receive frame tag + reserved */
    } rmd2;
    struct
    {
        uint32_t res; /**< reserved for user defined space */
    } rmd3;
} RMD;

/* What each PCI chip reports, from AMD's data sheets: PCI revision ID, the
   chip ID in CSR88/89 (part number 2420h/2621h/2623h/2624h/2625h/2627h, version 0),
   whether it has the PCI power management capability (PCI PM 1.0 on the Am79C972, 1.1
   on the Am79C973/975), and the address and identifier of the PHY on its MII (-1 none). */
typedef struct pcnet_chip_t {
    int      board;
    uint8_t  rev;
    uint16_t csr88, csr89;
    int      fast;   /* 10/100 Mbps, MII, STINT */
    int      magic;  /* Magic Packet (CSR5 MPMODE) */
    int      pm;     /* PCI power management + OnNow (CSR116, BCR36-47) */
    uint16_t pmc;    /* PMC as an EEPROM would load it into BCR36 */
    int      phyad;
    uint16_t phyid1, phyid2;
} pcnet_chip_t;

static const pcnet_chip_t pcnet_chips[] = {
    /* The 1994 data sheet: revision 00h, no Magic Packet. */
    { DEV_AM79C970,  0x00, 0x0003, 0x0242, 0, 0, 0, 0x0000, -1,   0,      0      },
    /* CSR89 version 6, as the Rev E data sheet gives it. */
    { DEV_AM79C970A, 0x10, 0x1003, 0x6262, 0, 1, 0, 0x0000, -1,   0,      0      },
    /* "The initial revision value will be 21h." External PHY: an Am79C874 NetPHY-1. */
    { DEV_AM79C971,  0x21, 0x3003, 0x0262, 1, 1, 0, 0x0000, 0x00, 0x0022, 0x561b },
    /* PME from D0-D3hot, D1/D2 supported, PM 1.0 (BCR36: the EEPROM default). */
    { DEV_AM79C972,  0x30, 0x4003, 0x0262, 1, 1, 1, 0x7e01, 0x00, 0x0022, 0x561b },
    /* Internal PHY at 1Eh, identifier 0000h/6B60h (ANR2/ANR3); PM 1.1. */
    { DEV_AM79C973,  0x40, 0x5003, 0x0262, 1, 1, 1, 0x7e02, 0x1e, 0x0000, 0x6b60 },
    { DEV_AM79C975,  0x40, 0x7003, 0x0262, 1, 1, 1, 0x7e02, 0x1e, 0x0000, 0x6b60 },
    /* PCnet-SCSI's Ethernet: revision 00h, part number 2420h (the PCnet-PCI's, which
       Linux's lance.c calls "79C970 or 79C974 PCnet-SCSI"). */
    { DEV_AM79C974,  0x00, 0x0003, 0x0242, 0, 0, 0, 0x0000, -1,   0,      0      },
};

static const pcnet_chip_t *
pcnet_chip(const nic_t *dev)
{
    for (size_t i = 0; i < (sizeof(pcnet_chips) / sizeof(pcnet_chips[0])); i++)
        if (pcnet_chips[i].board == dev->board)
            return &pcnet_chips[i];
    return NULL;
}

static __inline int
pcnet_is_fast(const nic_t *dev)
{
    const pcnet_chip_t *c = pcnet_chip(dev);
    return c && c->fast;
}

static __inline int
pcnet_has_pm(const nic_t *dev)
{
    const pcnet_chip_t *c = pcnet_chip(dev);
    return c && c->pm;
}

/* The chips with a full-duplex control register (BCR9): the PCnet-ISA II, PCnet-PCI II
   and the FAST family. The PCnet-ISA, ISA+, PCnet-32, PCnet-PCI and PCnet-SCSI have none. */
static __inline int
pcnet_has_bcr9(const nic_t *dev)
{
    return (dev->board == DEV_AM79C961A) || (dev->board == DEV_AM79C970A) || pcnet_is_fast(dev);
}

/* The chips whose setup registers (CSR80/82/100) may be written while suspended (SPND)
   as well as stopped; the older ones have no SPND and take them only while stopped. */
static __inline int
pcnet_has_spnd(const nic_t *dev)
{
    return pcnet_has_bcr9(dev);
}

static int  pcnet_pm_receive(nic_t *dev, const uint8_t *buf, int size);
static void pcnet_pm_link_change(nic_t *dev);

static void     pcnetAsyncTransmit(nic_t *dev);
static void     pcnetPollRxTx(nic_t *dev);
static void     pcnetUpdateIrq(nic_t *dev);
static uint16_t pcnet_bcr_readw(nic_t *dev, uint16_t rap);
static void     pcnet_bcr_writew(nic_t *dev, uint16_t rap, uint16_t val);
static void     pcnet_csr_writew(nic_t *dev, uint16_t rap, uint16_t val);
static int      pcnetCanReceive(nic_t *dev);
static void     pcnet_vlb_io_update(nic_t *dev);
static void     pcnet_vlb_bsbc_fix(nic_t *dev);

#ifdef ENABLE_PCNET_LOG
int pcnet_do_log = ENABLE_PCNET_LOG;

static void
pcnet_log(int lvl, const char *fmt, ...)
{
    va_list ap;

    if (pcnet_do_log >= lvl) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pcnet_log(lvl, fmt, ...)
#endif

static void
pcnet_do_irq(nic_t *dev, int issue)
{
    if (dev->is_pci) {
        /* The Am79C974's Ethernet interrupts on INTB (its SCSI on INTA). */
        const uint8_t pin = (dev->board == DEV_AM79C974) ? PCI_INTB : PCI_INTA;

        if (issue)
            pci_set_irq(dev->pci_slot, pin, &dev->irq_state);
        else
            pci_clear_irq(dev->pci_slot, pin, &dev->irq_state);
    } else {
        /* The PCnet-32's BCR2 INTLEVEL (Am79C965A, BCR2 bit 7): 0 drives its interrupt
           pin high while asserted (edge, not shareable), 1 low and floats it otherwise (level,
           shareable). A mode change first releases what the old mode drove. */
        int level = dev->is_vlb && (dev->aBCR[BCR_MC] & 0x0080);

        if (level != dev->irq_level) {
            if (dev->irq_level)
                picintclevel(1 << dev->base_irq, &dev->irq_state);
            else
                picintc(1 << dev->base_irq);
            dev->irq_level = level;
        }
        if (level) {
            if (issue)
                picintlevel(1 << dev->base_irq, &dev->irq_state);
            else
                picintclevel(1 << dev->base_irq, &dev->irq_state);
        } else if (issue)
            picint(1 << dev->base_irq);
        else
            picintc(1 << dev->base_irq);
    }
}

/**
 * Checks if the link is up.
 * @returns true if the link is up.
 * @returns false if the link is down.
 */
static __inline int
pcnetIsLinkUp(nic_t *dev)
{
    return !dev->fLinkTempDown && dev->fLinkUp;
}

/* The PCnet-ISA II's (Am79C961A) network port and full duplex. The port is ISACSR2
   (BCR2) MEDSEL: 00 software select by CSR15 PORTSEL (00 AUI, 01 10BASE-T, 10 GPSI), 01
   10BASE-T, 10 auto selection (the default: 10BASE-T while its link passes, else AUI), 11 AUI. */
#define PCNET_PORT_AUI  0
#define PCNET_PORT_10BT 1
#define PCNET_PORT_GPSI 2

static int
pcnet_isa2_port(nic_t *dev)
{
    switch (dev->aBCR[BCR_MC] & 0x0003) {
        default:
        case 0:
            switch ((dev->aCSR[15] >> 7) & 0x0003) {
                case 1:
                    return PCNET_PORT_10BT;
                case 2:
                    return PCNET_PORT_GPSI;
                default: /* 11 is reserved */
                    return PCNET_PORT_AUI;
            }
        case 1:
            return PCNET_PORT_10BT;
        case 2:
            return pcnetIsLinkUp(dev) ? PCNET_PORT_10BT : PCNET_PORT_AUI;
        case 3:
            return PCNET_PORT_AUI;
    }
}

/* ISACSR9 (BCR9): FDEN (bit 0) gives full duplex on the 10BASE-T and GPSI ports, on the AUI
   port only with AUIFD (bit 1) too; never on 10BASE-T while CSR15 DLNKTST is set. */
static int
pcnet_isa2_full_duplex(nic_t *dev)
{
    if ((dev->board != DEV_AM79C961A) || !(dev->aBCR[BCR_FDC] & 0x0001))
        return 0;

    switch (pcnet_isa2_port(dev)) {
        case PCNET_PORT_AUI:
            return !!(dev->aBCR[BCR_FDC] & 0x0002);
        case PCNET_PORT_10BT:
            return !(dev->aCSR[15] & 0x1000);
        default:
            return 1;
    }
}

/* The FDLSE LED function (ISACSR5-7 bit 8): a link pass state with full duplex capability --
   on 10BASE-T link pass with FDEN, on AUI FDEN and AUIFD, on GPSI FDEN. */
static int
pcnet_isa2_fd_link(nic_t *dev)
{
    if (!pcnet_isa2_full_duplex(dev))
        return 0;

    return (pcnet_isa2_port(dev) != PCNET_PORT_10BT) || pcnetIsLinkUp(dev);
}

/**
 * Load transmit message descriptor
 * Make sure we read the own flag first.
 *
 * @param pThis         adapter private data
 * @param addr          physical address of the descriptor
 * @param fRetIfNotOwn  return immediately after reading the own flag if we don't own the descriptor
 * @return              true if we own the descriptor, false otherwise
 */
static __inline int
pcnetTmdLoad(nic_t *dev, TMD *tmd, uint32_t addr, int fRetIfNotOwn)
{
    uint8_t  ownbyte;
    uint8_t  bytes[4] = { 0, 0, 0, 0 };
    uint16_t xda[4];
    uint32_t xda32[4];

    if (BCR_SWSTYLE(dev) == 0) {
        dma_bm_read(addr, (uint8_t *) bytes, 4, dev->transfer_size);
        ownbyte = bytes[3];
        if (!(ownbyte & 0x80) && fRetIfNotOwn)
            return 0;
        dma_bm_read(addr, (uint8_t *) &xda[0], sizeof(xda), dev->transfer_size);
        ((uint32_t *) tmd)[0] = (uint32_t) xda[0] | ((uint32_t) (xda[1] & 0x00ff) << 16);
        ((uint32_t *) tmd)[1] = (uint32_t) xda[2] | ((uint32_t) (xda[1] & 0xff00) << 16);
        ((uint32_t *) tmd)[2] = (uint32_t) xda[3] << 16;
        ((uint32_t *) tmd)[3] = 0;
    } else if (BCR_SWSTYLE(dev) != 3) {
        dma_bm_read(addr + 4, (uint8_t *) bytes, 4, dev->transfer_size);
        ownbyte = bytes[3];
        if (!(ownbyte & 0x80) && fRetIfNotOwn)
            return 0;
        dma_bm_read(addr, (uint8_t *) tmd, 16, dev->transfer_size);
    } else {
        dma_bm_read(addr + 4, (uint8_t *) bytes, 4, dev->transfer_size);
        ownbyte = bytes[3];
        if (!(ownbyte & 0x80) && fRetIfNotOwn)
            return 0;
        dma_bm_read(addr, (uint8_t *) &xda32[0], sizeof(xda32), dev->transfer_size);
        ((uint32_t *) tmd)[0] = xda32[2];
        ((uint32_t *) tmd)[1] = xda32[1];
        ((uint32_t *) tmd)[2] = xda32[0];
        ((uint32_t *) tmd)[3] = xda32[3];
    }
    /* Double check the own bit; guest drivers might be buggy and lock prefixes in the recompiler are ignored by other threads. */
    if (tmd->tmd1.own == 1 && !(ownbyte & 0x80))
        pcnet_log(3, "%s: pcnetTmdLoad: own bit flipped while reading!!\n", dev->name);
    if (!(ownbyte & 0x80))
        tmd->tmd1.own = 0;

    return !!tmd->tmd1.own;
}

/**
 * Store transmit message descriptor and hand it over to the host (the VM guest).
 * Make sure that all data are transmitted before we clear the own flag.
 */
static __inline void
pcnetTmdStorePassHost(nic_t *dev, TMD *tmd, uint32_t addr)
{
    uint16_t xda[4];
    uint32_t xda32[3];

    if (BCR_SWSTYLE(dev) == 0) {
        xda[0] = ((uint32_t *) tmd)[0] & 0xffff;
        xda[1] = ((((uint32_t *) tmd)[0] >> 16) & 0xff) | ((((uint32_t *) tmd)[1] >> 16) & 0xff00);
        xda[2] = ((uint32_t *) tmd)[1] & 0xffff;
        xda[3] = ((uint32_t *) tmd)[2] >> 16;
#if 0
        xda[1] |=  0x8000;
        dma_bm_write(addr, (uint8_t*)&xda[0], sizeof(xda), dev->transfer_size);
#endif
        xda[1] &= ~0x8000;
        dma_bm_write(addr, (uint8_t *) &xda[0], sizeof(xda), dev->transfer_size);
    } else if (BCR_SWSTYLE(dev) != 3) {
#if 0
        ((uint32_t*)tmd)[1] |=  0x80000000;
        dma_bm_write(addr, (uint8_t*)tmd, 12, dev->transfer_size);
#endif
        ((uint32_t *) tmd)[1] &= ~0x80000000;
        dma_bm_write(addr, (uint8_t *) tmd, 12, dev->transfer_size);
    } else {
        xda32[0] = ((uint32_t *) tmd)[2];
        xda32[1] = ((uint32_t *) tmd)[1];
        xda32[2] = ((uint32_t *) tmd)[0];
#if 0
        xda32[1] |=  0x80000000;
        dma_bm_write(addr, (uint8_t*)&xda32[0], sizeof(xda32), dev->transfer_size);
#endif
        xda32[1] &= ~0x80000000;
        dma_bm_write(addr, (uint8_t *) &xda32[0], sizeof(xda32), dev->transfer_size);
    }
}

/**
 * Load receive message descriptor
 * Make sure we read the own flag first.
 *
 * @param pThis         adapter private data
 * @param addr          physical address of the descriptor
 * @param fRetIfNotOwn  return immediately after reading the own flag if we don't own the descriptor
 * @return              true if we own the descriptor, false otherwise
 */
static __inline int
pcnetRmdLoad(nic_t *dev, RMD *rmd, uint32_t addr, int fRetIfNotOwn)
{
    uint8_t  ownbyte;
    uint8_t  bytes[4] = { 0, 0, 0, 0 };
    uint16_t rda[4];
    uint32_t rda32[4];

    if (BCR_SWSTYLE(dev) == 0) {
        dma_bm_read(addr, (uint8_t *) bytes, 4, dev->transfer_size);
        ownbyte = bytes[3];
        if (!(ownbyte & 0x80) && fRetIfNotOwn)
            return 0;
        dma_bm_read(addr, (uint8_t *) &rda[0], sizeof(rda), dev->transfer_size);
        ((uint32_t *) rmd)[0] = (uint32_t) rda[0] | ((rda[1] & 0x00ff) << 16);
        ((uint32_t *) rmd)[1] = (uint32_t) rda[2] | ((rda[1] & 0xff00) << 16);
        ((uint32_t *) rmd)[2] = (uint32_t) rda[3];
        ((uint32_t *) rmd)[3] = 0;
    } else if (BCR_SWSTYLE(dev) != 3) {
        dma_bm_read(addr + 4, (uint8_t *) bytes, 4, dev->transfer_size);
        ownbyte = bytes[3];
        if (!(ownbyte & 0x80) && fRetIfNotOwn)
            return 0;
        dma_bm_read(addr, (uint8_t *) rmd, 16, dev->transfer_size);
    } else {
        dma_bm_read(addr + 4, (uint8_t *) bytes, 4, dev->transfer_size);
        ownbyte = bytes[3];
        if (!(ownbyte & 0x80) && fRetIfNotOwn)
            return 0;
        dma_bm_read(addr, (uint8_t *) &rda32[0], sizeof(rda32), dev->transfer_size);
        ((uint32_t *) rmd)[0] = rda32[2];
        ((uint32_t *) rmd)[1] = rda32[1];
        ((uint32_t *) rmd)[2] = rda32[0];
        ((uint32_t *) rmd)[3] = rda32[3];
    }
    /* Double check the own bit; guest drivers might be buggy and lock prefixes in the recompiler are ignored by other threads. */
    if (rmd->rmd1.own == 1 && !(ownbyte & 0x80))
        pcnet_log(3, "%s: pcnetRmdLoad: own bit flipped while reading!!\n", dev->name);

    if (!(ownbyte & 0x80))
        rmd->rmd1.own = 0;

    return !!rmd->rmd1.own;
}

/**
 * Store receive message descriptor and hand it over to the host (the VM guest).
 * Make sure that all data are transmitted before we clear the own flag.
 */
static __inline void
pcnetRmdStorePassHost(nic_t *dev, RMD *rmd, uint32_t addr)
{
    uint16_t rda[4];
    uint32_t rda32[3];

    if (BCR_SWSTYLE(dev) == 0) {
        rda[0] = ((uint32_t *) rmd)[0] & 0xffff;
        rda[1] = ((((uint32_t *) rmd)[0] >> 16) & 0xff) | ((((uint32_t *) rmd)[1] >> 16) & 0xff00);
        rda[2] = ((uint32_t *) rmd)[1] & 0xffff;
        rda[3] = ((uint32_t *) rmd)[2] & 0xffff;
#if 0
        rda[1] |=  0x8000;
        dma_bm_write(addr, (uint8_t*)&rda[0], sizeof(rda), dev->transfer_size);
#endif
        rda[1] &= ~0x8000;
        dma_bm_write(addr, (uint8_t *) &rda[0], sizeof(rda), dev->transfer_size);
    } else if (BCR_SWSTYLE(dev) != 3) {
#if 0
        ((uint32_t*)rmd)[1] |=  0x80000000;
        dma_bm_write(addr, (uint8_t*)rmd, 12, dev->transfer_size);
#endif
        ((uint32_t *) rmd)[1] &= ~0x80000000;
        dma_bm_write(addr, (uint8_t *) rmd, 12, dev->transfer_size);
    } else {
        rda32[0] = ((uint32_t *) rmd)[2];
        rda32[1] = ((uint32_t *) rmd)[1];
        rda32[2] = ((uint32_t *) rmd)[0];
#if 0
        rda32[1] |=  0x80000000;
        dma_bm_write(addr, (uint8_t*)&rda32[0], sizeof(rda32), dev->transfer_size);
#endif
        rda32[1] &= ~0x80000000;
        dma_bm_write(addr, (uint8_t *) &rda32[0], sizeof(rda32), dev->transfer_size);
    }
}

/** Checks if it's a bad (as in invalid) RMD.*/
#define IS_RMD_BAD(rmd) ((rmd).rmd1.ones != 15)

/** The network card is the owner of the RDTE/TDTE, actually it is this driver */
#define CARD_IS_OWNER(desc) (((desc) &0x8000))

/** The host is the owner of the RDTE/TDTE -- actually the VM guest. */
#define HOST_IS_OWNER(desc) (!((desc) &0x8000))

#ifndef ETHER_IS_MULTICAST /* Net/Open BSD macro it seems */
#    define ETHER_IS_MULTICAST(a) ((*(uint8_t *) (a)) & 1)
#endif

#define ETHER_ADDR_LEN ETH_ALEN
#define ETH_ALEN       6
#pragma pack(1)
struct ether_header /** @todo Use RTNETETHERHDR */
{
    uint8_t  ether_dhost[ETH_ALEN]; /**< destination ethernet address */
    uint8_t  ether_shost[ETH_ALEN]; /**< source ethernet address */
    uint16_t ether_type;            /**< packet type ID field */
};
#pragma pack()

#define CRC(crc, ch)         (crc = (crc >> 8) ^ crctab[(crc ^ (ch)) & 0xff])

#define MULTICAST_FILTER_LEN 8

static __inline uint32_t
lnc_mchash(const uint8_t *ether_addr)
{
#define LNC_POLYNOMIAL 0xEDB88320UL
    uint32_t crc = 0xFFFFFFFF;
    int      bit;
    uint8_t  data;

    for (uint8_t idx = 0; idx < ETHER_ADDR_LEN; idx++) {
        for (data = *ether_addr++, bit = 0; bit < MULTICAST_FILTER_LEN; bit++) {
            crc = (crc >> 1) ^ (((crc ^ data) & 1) ? LNC_POLYNOMIAL : 0);
            data >>= 1;
        }
    }
    return crc;
#undef LNC_POLYNOMIAL
}

#define CRC(crc, ch) (crc = (crc >> 8) ^ crctab[(crc ^ (ch)) & 0xff])

/* generated using the AUTODIN II polynomial
 *   x^32 + x^26 + x^23 + x^22 + x^16 +
 *   x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + 1
 */
static const uint32_t crctab[256] =
{
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
    0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
    0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
    0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
    0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
    0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
    0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
    0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
    0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
    0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
    0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
    0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
    0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
    0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
};

static __inline int
padr_match(nic_t *dev, const uint8_t *buf, UNUSED(int size))
{
    const struct ether_header *hdr = (const struct ether_header *) buf;
    int                        result;
    uint8_t                    padr[6];

    padr[0] = dev->aCSR[12] & 0xff;
    padr[1] = dev->aCSR[12] >> 8;
    padr[2] = dev->aCSR[13] & 0xff;
    padr[3] = dev->aCSR[13] >> 8;
    padr[4] = dev->aCSR[14] & 0xff;
    padr[5] = dev->aCSR[14] >> 8;
    result  = !CSR_DRCVPA(dev) && !memcmp(hdr->ether_dhost, padr, 6);

    pcnet_log(3, "%s: packet dhost=%02x:%02x:%02x:%02x:%02x:%02x, "
                 "padr=%02x:%02x:%02x:%02x:%02x:%02x => %d\n",
              dev->name,
              hdr->ether_dhost[0], hdr->ether_dhost[1], hdr->ether_dhost[2],
              hdr->ether_dhost[3], hdr->ether_dhost[4], hdr->ether_dhost[5],
              padr[0], padr[1], padr[2], padr[3], padr[4], padr[5], result);

    return result;
}

static __inline int
padr_bcast(nic_t *dev, const uint8_t *buf, UNUSED(size_t size))
{
    static uint8_t             aBCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    const struct ether_header *hdr       = (const struct ether_header *) buf;
    int                        result    = !CSR_DRCVBC(dev) && !memcmp(hdr->ether_dhost, aBCAST, 6);

    pcnet_log(3, "%s: padr_bcast result=%d\n", dev->name, result);

    return result;
}

static int
ladr_match(nic_t *dev, const uint8_t *buf, UNUSED(size_t size))
{
    const struct ether_header *hdr = (const struct ether_header *) buf;
    uint64_t *p = (uint64_t *) &dev->aCSR[8];

    if ((hdr->ether_dhost[0] & 0x01) && p[0] != 0LL) {
        int     index;
        uint8_t ladr[8];
        ladr[0] = dev->aCSR[8] & 0xff;
        ladr[1] = dev->aCSR[8] >> 8;
        ladr[2] = dev->aCSR[9] & 0xff;
        ladr[3] = dev->aCSR[9] >> 8;
        ladr[4] = dev->aCSR[10] & 0xff;
        ladr[5] = dev->aCSR[10] >> 8;
        ladr[6] = dev->aCSR[11] & 0xff;
        ladr[7] = dev->aCSR[11] >> 8;
        index   = lnc_mchash(hdr->ether_dhost) >> 26;
        return (ladr[index >> 3] & (1 << (index & 7)));
    }
    return 0;
}

/**
 * Get the receive descriptor ring address with a given index.
 */
static __inline uint32_t
pcnetRdraAddr(nic_t *dev, int idx)
{
    return dev->GCRDRA + ((CSR_RCVRL(dev) - idx) << dev->iLog2DescSize);
}

/**
 * Get the transmit descriptor ring address with a given index.
 */
static __inline uint32_t
pcnetTdraAddr(nic_t *dev, int idx)
{
    return dev->GCTDRA + ((CSR_XMTRL(dev) - idx) << dev->iLog2DescSize);
}

static void
pcnetSoftReset(nic_t *dev)
{
    pcnet_log(3, "%s: pcnetSoftReset\n", dev->name);

    dev->u32Lnkst = 0x40;
    dev->GCRDRA   = 0;
    dev->GCTDRA   = 0;
    dev->u32RAP   = 0;

    dev->aCSR[0]  = 0x0004;
    dev->aCSR[3]  = 0x0000;
    dev->aCSR[4]  = 0x0115;
    dev->aCSR[5]  = 0x0000;
    dev->aCSR[6]  = 0x0000;
    dev->aCSR[8]  = 0;
    dev->aCSR[9]  = 0;
    dev->aCSR[10] = 0;
    dev->aCSR[11] = 0;
    dev->aCSR[12] = le16_to_cpu(((uint16_t *) &dev->aPROM[0])[0]);
    dev->aCSR[13] = le16_to_cpu(((uint16_t *) &dev->aPROM[0])[1]);
    dev->aCSR[14] = le16_to_cpu(((uint16_t *) &dev->aPROM[0])[2]);
    dev->aCSR[15] &= 0x21c4;
    CSR_RCVRC(dev) = 1;
    CSR_XMTRC(dev) = 1;
    CSR_RCVRL(dev) = 1;
    CSR_XMTRL(dev) = 1;
    dev->aCSR[80]  = 0x1410;
    /* The Am79C965A's CSR80 is E810h (bits 15:14 read as ones, RCVFW and XMTSP 64
       bytes, XMTFW 8 cycles, DMACR 16) and its CSR82 (bus activity timer) 0000h after
       H_RESET or S_RESET (register summary). */
    if (dev->is_vlb) {
        dev->aCSR[80] = 0xe810;
        dev->aCSR[82] = 0x0000;
    }

    /* CSR89:CSR88 is the JTAG ID, version << 28 | part number << 12 | 003h (AMD), as each
       chip's data sheet gives it: Am79C960 0003h, Am79C961 2260h, Am79C965A 2430h (version 3),
       Am79C970A 2621h (version 6), Am79C973 2625h. The Am79C961 and Am79C965 reported the
       Am79C960's ID before. */
    switch (dev->board) {
        case DEV_AM79C970:
        case DEV_AM79C970A:
        case DEV_AM79C971:
        case DEV_AM79C972:
        case DEV_AM79C973:
        case DEV_AM79C975:
        case DEV_AM79C974:
            dev->aCSR[88] = pcnet_chip(dev)->csr88;
            dev->aCSR[89] = pcnet_chip(dev)->csr89;
            break;
        case DEV_AM79C960_VLB:
            dev->aCSR[88] = 0x0003;
            dev->aCSR[89] = 0x3243;
            break;
        case DEV_AM79C961:
            dev->aCSR[88] = 0x0003;
            dev->aCSR[89] = 0x0226;
            break;
        case DEV_AM79C960:
        case DEV_AM79C960_EB:
            dev->aCSR[88] = 0x3003;
            dev->aCSR[89] = 0x0000;
            break;
        /* The Am79C961A's data sheet gives 32261003h (part 2261h, version 3). */
        case DEV_AM79C961A:
            dev->aCSR[88] = 0x1003;
            dev->aCSR[89] = 0x3226;
            break;

        default:
            break;
    }

    dev->aCSR[94]  = 0x0000;
    /* The PCnet-PCI II and PCnet-FAST III reset the bus timeout to 0600h (their larger FIFOs). */
    dev->aCSR[100] = dev->is_pci ? 0x0600 : 0x0200;
    dev->aCSR[103] = 0x0105;
    CSR_MISSC(dev) = 0;
    dev->aCSR[114] = 0x0000;
    dev->aCSR[122] = 0x0000;
    dev->aCSR[124] = 0x0000;
}

static void
pcnetUpdateIrq(nic_t *dev)
{
    int      iISR = 0;
    uint16_t csr0;

    csr0 = dev->aCSR[0];

    csr0 &= ~0x0080; /* clear INTR */

    if (((csr0 & ~dev->aCSR[3]) & 0x5f00) || (((dev->aCSR[4] >> 1) & ~dev->aCSR[4]) & 0x0115) || (((dev->aCSR[5] >> 1) & dev->aCSR[5]) & 0x0048)) {
        iISR = !!(csr0 & 0x0040); /* CSR_INEA */
        csr0 |= 0x0080;           /* set INTR */
    }

    if (dev->aCSR[4] & 0x0080) { /* UINTCMD */
        dev->aCSR[4] &= ~0x0080; /* clear UINTCMD */
        dev->aCSR[4] |= 0x0040;  /* set UINT */
        pcnet_log(2, "%s: user int\n", dev->name);
    }

    if (dev->aCSR[4] & csr0 & 0x0040 /* CSR_INEA */) {
        csr0 |= 0x0080; /* set INTR */
        iISR = 1;
    }

    if (((dev->aCSR[5] >> 1) & dev->aCSR[5]) & 0x0500) {
        iISR = 1;
        csr0 |= 0x0080; /* set INTR */
    }

    if ((dev->aCSR[7] & 0x0c00) == 0x0c00) /* STINT + STINTE */
        iISR = 1;

    dev->aCSR[0] = csr0;

    pcnet_log(2, "%s: pcnetUpdateIrq: iISR=%d\n", dev->name, iISR);

    pcnet_do_irq(dev, iISR);
    dev->iISR = iISR;
}

static void
pcnetInit(nic_t *dev)
{
    pcnet_log(3, "%s: pcnetInit: init_addr=%#010x\n", dev->name, PHYSADDR(dev, CSR_IADR(dev)));

#define PCNET_INIT()                                                            \
    do {                                                                        \
        dma_bm_read(PHYSADDR(dev, CSR_IADR(dev)),                               \
                    (uint8_t *) &initblk, sizeof(initblk), dev->transfer_size); \
        dev->aCSR[15]  = le16_to_cpu(initblk.mode);                             \
        CSR_RCVRL(dev) = (initblk.rlen < 9) ? (1 << initblk.rlen) : 512;        \
        CSR_XMTRL(dev) = (initblk.tlen < 9) ? (1 << initblk.tlen) : 512;        \
        dev->aCSR[6]   = (initblk.tlen << 12) | (initblk.rlen << 8);            \
        dev->aCSR[8]   = le16_to_cpu(initblk.ladrf1);                           \
        dev->aCSR[9]   = le16_to_cpu(initblk.ladrf2);                           \
        dev->aCSR[10]  = le16_to_cpu(initblk.ladrf3);                           \
        dev->aCSR[11]  = le16_to_cpu(initblk.ladrf4);                           \
        dev->aCSR[12]  = le16_to_cpu(initblk.padr1);                            \
        dev->aCSR[13]  = le16_to_cpu(initblk.padr2);                            \
        dev->aCSR[14]  = le16_to_cpu(initblk.padr3);                            \
        dev->GCRDRA    = PHYSADDR(dev, initblk.rdra);                           \
        dev->GCTDRA    = PHYSADDR(dev, initblk.tdra);                           \
    } while (0)

    if (BCR_SSIZE32(dev)) {
        struct INITBLK32 initblk;
        dev->GCUpperPhys = 0;
        PCNET_INIT();
        pcnet_log(3, "%s: initblk.rlen=%#04x, initblk.tlen=%#04x\n",
                  dev->name, initblk.rlen, initblk.tlen);
    } else {
        struct INITBLK16 initblk;
        dev->GCUpperPhys = (0xff00 & (uint32_t) dev->aCSR[2]) << 16;
        PCNET_INIT();
        pcnet_log(3, "%s: initblk.rlen=%#04x, initblk.tlen=%#04x\n",
                  dev->name, initblk.rlen, initblk.tlen);
    }

#undef PCNET_INIT

    size_t cbRxBuffers = 0;
    for (int i = CSR_RCVRL(dev); i >= 1; i--) {
        RMD      rmd;
        uint32_t rdaddr = PHYSADDR(dev, pcnetRdraAddr(dev, i));

        /* At this time it is not guaranteed that the buffers are already initialized. */
        if (pcnetRmdLoad(dev, &rmd, rdaddr, 0)) {
            uint32_t cbBuf = 4096U - rmd.rmd1.bcnt;
            cbRxBuffers += cbBuf;
        }
    }

    /*
     * Heuristics: The Solaris pcn driver allocates too few RX buffers (128 buffers of a
     * size of 128 bytes are 16KB in summary) leading to frequent RX buffer overflows. In
     * that case we don't signal RX overflows through the CSR0_MISS flag as the driver
     * re-initializes the device on every miss. Other guests use at least 32 buffers of
     * usually 1536 bytes and should therefore not run into condition. If they are still
     * short in RX buffers we notify this condition.
     */
    dev->fSignalRxMiss = (cbRxBuffers == 0 || cbRxBuffers >= 32 * 1024);

    CSR_RCVRC(dev) = CSR_RCVRL(dev);
    CSR_XMTRC(dev) = CSR_XMTRL(dev);

    /* Reset cached RX and TX states */
    CSR_CRST(dev) = CSR_CRBC(dev) = CSR_NRST(dev) = CSR_NRBC(dev) = 0;
    CSR_CXST(dev) = CSR_CXBC(dev) = CSR_NXST(dev) = CSR_NXBC(dev) = 0;

    pcnet_log(1, "%s: Init: SWSTYLE=%d GCRDRA=%#010x[%d] GCTDRA=%#010x[%d]%s\n",
              dev->name, BCR_SWSTYLE(dev),
              dev->GCRDRA, CSR_RCVRL(dev), dev->GCTDRA, CSR_XMTRL(dev),
              !dev->fSignalRxMiss ? " (CSR0_MISS disabled)" : "");

    if (dev->GCRDRA & (dev->iLog2DescSize - 1))
        pcnet_log(1, "%s: Warning: Misaligned RDRA\n", dev->name);
    if (dev->GCTDRA & (dev->iLog2DescSize - 1))
        pcnet_log(1, "%s: Warning: Misaligned TDRA\n", dev->name);

    dev->aCSR[0] |= 0x0101;  /* Initialization done */
    dev->aCSR[0] &= ~0x0004; /* clear STOP bit */
}

/**
 * Start RX/TX operation.
 */
static void
pcnetStart(nic_t *dev)
{
    pcnet_log(3, "%s: pcnetStart: Poll timer\n", dev->name);

    /* Reset any cached RX/TX descriptor state. */
    CSR_CRDA(dev) = CSR_CRBA(dev) = CSR_NRDA(dev) = CSR_NRBA(dev) = 0;
    CSR_CRBC(dev) = CSR_NRBC(dev) = CSR_CRST(dev) = 0;

    if (!CSR_DTX(dev))
        dev->aCSR[0] |= 0x0010; /* set TXON */
    if (!CSR_DRX(dev))
        dev->aCSR[0] |= 0x0020; /* set RXON */
    dev->aCSR[0] &= ~0x0004;    /* clear STOP bit */
    dev->aCSR[0] |= 0x0002;     /* STRT */
    timer_set_delay_u64(&dev->timer, 2000 * TIMER_USEC);
}

/**
 * Stop RX/TX operation.
 */
static void
pcnetStop(nic_t *dev)
{
    pcnet_log(3, "%s: pcnetStop: Poll timer\n", dev->name);
    dev->aCSR[0] = 0x0004;
    dev->aCSR[4] &= ~0x02c2;
    dev->aCSR[5] &= ~0x0011;
    timer_disable(&dev->timer);
}

/**
 * Poll Receive Descriptor Table Entry and cache the results in the appropriate registers.
 * Note: Once a descriptor belongs to the network card (this driver), it cannot be changed
 * by the host (the guest driver) anymore. Well, it could but the results are undefined by
 * definition.
 */
static void
pcnetRdtePoll(nic_t *dev)
{
    /* assume lack of a next receive descriptor */
    CSR_NRST(dev) = 0;

    if (dev->GCRDRA) {
        /*
         * The current receive message descriptor.
         */
        RMD      rmd;
        int      i = CSR_RCVRC(dev);
        uint32_t addr;

        if (i < 1)
            i = CSR_RCVRL(dev);

        addr          = pcnetRdraAddr(dev, i);
        CSR_CRDA(dev) = CSR_CRBA(dev) = 0;
        CSR_CRBC(dev) = CSR_CRST(dev) = 0;
        if (!pcnetRmdLoad(dev, &rmd, PHYSADDR(dev, addr), 1))
            return;
        if (!IS_RMD_BAD(rmd)) {
            CSR_CRDA(dev) = addr;                         /* Receive Descriptor Address */
            CSR_CRBA(dev) = rmd.rmd0.rbadr;               /* Receive Buffer Address */
            CSR_CRBC(dev) = rmd.rmd1.bcnt;                /* Receive Byte Count */
            CSR_CRST(dev) = ((uint32_t *) &rmd)[1] >> 16; /* Receive Status */
        } else {
            /* This is not problematic since we don't own the descriptor
             * We actually do own it, otherwise pcnetRmdLoad would have returned false.
             * Don't flood the release log with errors.
             */
            if (++dev->uCntBadRMD < 50)
                pcnet_log(1, "%s: BAD RMD ENTRIES AT %#010x (i=%d)\n",
                          dev->name, addr, i);
            return;
        }

        /*
         * The next descriptor.
         */
        if (--i < 1)
            i = CSR_RCVRL(dev);
        addr          = pcnetRdraAddr(dev, i);
        CSR_NRDA(dev) = CSR_NRBA(dev) = 0;
        CSR_NRBC(dev)                 = 0;
        if (!pcnetRmdLoad(dev, &rmd, PHYSADDR(dev, addr), 1))
            return;
        if (!IS_RMD_BAD(rmd)) {
            CSR_NRDA(dev) = addr;                         /* Receive Descriptor Address */
            CSR_NRBA(dev) = rmd.rmd0.rbadr;               /* Receive Buffer Address */
            CSR_NRBC(dev) = rmd.rmd1.bcnt;                /* Receive Byte Count */
            CSR_NRST(dev) = ((uint32_t *) &rmd)[1] >> 16; /* Receive Status */
        } else {
            /* This is not problematic since we don't own the descriptor
             * We actually do own it, otherwise pcnetRmdLoad would have returned false.
             * Don't flood the release log with errors.
             */
            if (++dev->uCntBadRMD < 50)
                pcnet_log(1, "%s: BAD RMD ENTRIES + AT %#010x (i=%d)\n",
                          dev->name, addr, i);
            return;
        }

        /* NNRD (CSR36/37) holds the address of the descriptor after the next one. The
           chip only uses it to read that descriptor ahead; here the ring is polled again for
           each frame, so the address is all there is to it. */
        if (--i < 1)
            i = CSR_RCVRL(dev);
        CSR_NNRD(dev) = pcnetRdraAddr(dev, i);
    } else {
        CSR_CRDA(dev) = CSR_CRBA(dev) = CSR_NRDA(dev) = CSR_NRBA(dev) = 0;
        CSR_CRBC(dev) = CSR_NRBC(dev) = CSR_CRST(dev) = 0;
    }
}

/**
 * Poll Transmit Descriptor Table Entry
 * @return true if transmit descriptors available
 */
static int
pcnetTdtePoll(nic_t *dev, TMD *tmd)
{
    if (dev->GCTDRA) {
        uint32_t cxda = pcnetTdraAddr(dev, CSR_XMTRC(dev));

        if (!pcnetTmdLoad(dev, tmd, PHYSADDR(dev, cxda), 1))
            return 0;

        if (tmd->tmd1.ones != 15) {
            pcnet_log(1, "%s: BAD TMD XDA=%#010x\n",
                      dev->name, PHYSADDR(dev, cxda));
            return 0;
        }

        /* previous xmit descriptor */
        CSR_PXDA(dev) = CSR_CXDA(dev);
        CSR_PXBC(dev) = CSR_CXBC(dev);
        CSR_PXST(dev) = CSR_CXST(dev);

        /* set current transmit descriptor. */
        CSR_CXDA(dev) = cxda;
        CSR_CXBC(dev) = tmd->tmd1.bcnt;
        CSR_CXST(dev) = ((uint32_t *) tmd)[1] >> 16;
        return CARD_IS_OWNER(CSR_CXST(dev));
    } else {
        /** @todo consistency with previous receive descriptor */
        CSR_CXDA(dev) = 0;
        CSR_CXBC(dev) = CSR_CXST(dev) = 0;
        return 0;
    }
}

/**
 * Write data into guest receive buffers.
 */
static int
pcnetReceiveNoSync(void *priv, uint8_t *buf, int size)
{
    nic_t   *dev      = (nic_t *) priv;
    int      is_padr  = 0;
    int      is_bcast = 0;
    int      is_ladr  = 0;
    uint32_t iRxDesc;
    int      cbPacket;
    uint8_t  buf1[60];
    RMD      rmd      = { 0 };

    /* Magic Packet mode flushes every frame; OnNow pattern matching watches them. */
    if (size && pcnet_pm_receive(dev, buf, size))
        return 1;

    if (CSR_DRX(dev) || CSR_STOP(dev) || CSR_SPND(dev) || !size)
        return 0;

    /* if too small buffer, then expand it */
    if (size < 60) {
        memcpy(buf1, buf, size);
        memset(buf1 + size, 0, 60 - size);
        buf  = buf1;
        size = 60;
    }

    /*
     * Drop packets if the cable is not connected
     */
    if (!pcnetIsLinkUp(dev))
        return 0;

    dev->fMaybeOutOfSpace = !pcnetCanReceive(dev);
    if (dev->fMaybeOutOfSpace)
        return 0;

    pcnet_log(1, "%s: pcnetReceiveNoSync: RX %x:%x:%x:%x:%x:%x > %x:%x:%x:%x:%x:%x len %d\n", dev->name,
              buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
              buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
              size);

    /*
     * Perform address matching.
     */
    if (CSR_PROM(dev)
        || (is_padr = padr_match(dev, buf, size))
        || (is_bcast = padr_bcast(dev, buf, size))
        || (is_ladr = ladr_match(dev, buf, size))) {

        if (HOST_IS_OWNER(CSR_CRST(dev)))
            pcnetRdtePoll(dev);

        if (HOST_IS_OWNER(CSR_CRST(dev))) {
            /* Not owned by controller. This should not be possible as
             * we already called pcnetCanReceive(). */
            const unsigned cb     = 1 << dev->iLog2DescSize;
            uint32_t       GCPhys = dev->GCRDRA;
            iRxDesc               = CSR_RCVRL(dev);

            while (iRxDesc-- > 0) {
                pcnetRmdLoad(dev, &rmd, PHYSADDR(dev, GCPhys), 0);
                GCPhys += cb;
            }
            dev->aCSR[0] |= 0x1000; /* Set MISS flag */
            CSR_MISSC(dev)
            ++;
            pcnet_log(2, "%s: pcnetReceiveNoSync: packet missed\n", dev->name);
        } else {
            const RTNETETHERHDR *pEth   = (RTNETETHERHDR *) buf;
            int                  fStrip = 0;
            size_t               len_802_3;
            uint8_t             *src  = &dev->abRecvBuf[8];
            uint32_t             crda = CSR_CRDA(dev);
            uint32_t             next_crda;
            RMD                  rmd;
            RMD                  next_rmd;

            /*
             * Ethernet framing considers these two octets to be
             * payload type; 802.3 framing considers them to be
             * payload length.  IEEE 802.3x-1997 restricts Ethernet
             * type to be greater than or equal to 1536 (0x0600), so
             * that both framings can coexist on the wire.
             *
             * NB: CSR_ASTRP_RCV bit affects only 802.3 frames!
             */
            len_802_3 = cpu_to_be16(pEth->EtherType);
            if (len_802_3 < 46 && CSR_ASTRP_RCV(dev)) {
                size   = MIN(sizeof(RTNETETHERHDR) + len_802_3, size);
                fStrip = 1;
            }

            memcpy(src, buf, size);

            if (!fStrip) {
                /* In loopback mode, Runt Packed Accept is always enabled internally;
                 * don't do any padding because guest may be looping back very short packets.
                 */
                if (!CSR_LOOP(dev))
                    while (size < 60)
                        src[size++] = 0;

                uint32_t       fcs = UINT32_MAX;
                const uint8_t *p   = src;

                while (p != &src[size])
                    CRC(fcs, *p++);

                /* FCS at the end of the packet, one's complement in little endian */
                ((uint32_t *) &src[size])[0] = cpu_to_le32(~fcs);
                size += 4;
            }

            cbPacket = size;

            pcnetRmdLoad(dev, &rmd, PHYSADDR(dev, crda), 0);
            /* if (!CSR_LAPPEN(dev)) */
            rmd.rmd1.stp = 1;

            size_t   cbBuf = MIN(4096 - rmd.rmd1.bcnt, size);
            uint32_t rbadr = PHYSADDR(dev, rmd.rmd0.rbadr);

            /* save the old value to check if it was changed as long as we didn't
             * hold the critical section */
            iRxDesc = CSR_RCVRC(dev);

            /* We have to leave the critical section here or we risk deadlocking
             * with EMT when the write is to an unallocated page or has an access
             * handler associated with it.
             *
             * This shouldn't be a problem because:
             *  - any modification to the RX descriptor by the driver is
             *    forbidden as long as it is owned by the device
             *  - we don't cache any register state beyond this point
             */

            dma_bm_write(rbadr, src, cbBuf, dev->transfer_size);

            /* RX disabled in the meantime? If so, abort RX. */
            if (CSR_DRX(dev) || CSR_STOP(dev) || CSR_SPND(dev)) {
                pcnet_log(3, "%s: RX disabled 1\n", dev->name);
                return 0;
            }

            /* Was the register modified in the meantime? If so, don't touch the
             * register but still update the RX descriptor. */
            if (iRxDesc == CSR_RCVRC(dev)) {
                if (iRxDesc-- < 2)
                    iRxDesc = CSR_RCVRL(dev);
                CSR_RCVRC(dev) = iRxDesc;
            } else
                iRxDesc = CSR_RCVRC(dev);

            src += cbBuf;
            size -= cbBuf;

            while (size > 0) {
                /* Read the entire next descriptor as we're likely to need it. */
                next_crda = pcnetRdraAddr(dev, iRxDesc);

                /* Check next descriptor's own bit. If we don't own it, we have
                 * to quit and write error status into the last descriptor we own.
                 */
                if (!pcnetRmdLoad(dev, &next_rmd, PHYSADDR(dev, next_crda), 1))
                    break;

                /* Write back current descriptor, clear the own bit. */
                pcnetRmdStorePassHost(dev, &rmd, PHYSADDR(dev, crda));

                /* Switch to the next descriptor */
                crda = next_crda;
                rmd  = next_rmd;

                cbBuf           = MIN(4096 - (size_t) rmd.rmd1.bcnt, size);
                uint32_t rbadr2 = PHYSADDR(dev, rmd.rmd0.rbadr);

                /* We have to leave the critical section here or we risk deadlocking
                 * with EMT when the write is to an unallocated page or has an access
                 * handler associated with it. See above for additional comments. */
                dma_bm_write(rbadr2, src, cbBuf, dev->transfer_size);

                /* RX disabled in the meantime? If so, abort RX. */
                if (CSR_DRX(dev) || CSR_STOP(dev) || CSR_SPND(dev)) {
                    pcnet_log(3, "%s: RX disabled 2\n", dev->name);
                    return 0;
                }

                /* Was the register modified in the meantime? If so, don't touch the
                 * register but still update the RX descriptor. */
                if (iRxDesc == CSR_RCVRC(dev)) {
                    if (iRxDesc-- < 2)
                        iRxDesc = CSR_RCVRL(dev);
                    CSR_RCVRC(dev) = iRxDesc;
                } else {
                    iRxDesc = CSR_RCVRC(dev);
                }

                src += cbBuf;
                size -= cbBuf;
            }

            if (size == 0) {
                rmd.rmd1.enp   = 1;
                rmd.rmd1.pam   = !CSR_PROM(dev) && is_padr;
                rmd.rmd1.lafm  = !CSR_PROM(dev) && is_ladr;
                rmd.rmd1.bam   = !CSR_PROM(dev) && is_bcast;
                rmd.rmd2.mcnt  = cbPacket;
                rmd.rmd2.zeros = 0;
            } else {
                pcnet_log(1, "%s: Overflow by %ubytes\n", dev->name, size);
                rmd.rmd1.oflo = 1;
                rmd.rmd1.buff = 1;
                rmd.rmd1.err  = 1;
            }

            /* write back, clear the own bit */
            pcnetRmdStorePassHost(dev, &rmd, PHYSADDR(dev, crda));

            dev->aCSR[0] |= 0x0400;
            pcnet_log(1, "%s: RINT set, RCVRC=%d CRDA=%#010x\n", dev->name,
                      CSR_RCVRC(dev), PHYSADDR(dev, CSR_CRDA(dev)));

            /* guest driver is owner: force repoll of current and next RDTEs */
            CSR_CRST(dev) = 0;
        }
    }

    pcnetUpdateIrq(dev);

    return 1;
}

/**
 * Fails a TMD with a link down error.
 */
static void
pcnetXmitFailTMDLinkDown(nic_t *dev, TMD *pTmd)
{
    /* make carrier error - hope this is correct. */
    dev->cLinkDownReported++;
    /* In full duplex on the AUI or GPSI port the PCnet-ISA II reports neither Loss of
       Carrier nor the SQE test's CERR (Am79C961A data sheet, Full Duplex Operation, MAC Engine
       changes 2 and 4); on 10BASE-T in Link Fail it still reports both. */
    if (pcnet_isa2_full_duplex(dev) && (pcnet_isa2_port(dev) != PCNET_PORT_10BT))
        return;
    pTmd->tmd2.lcar = pTmd->tmd1.err = 1;
    dev->aCSR[0] |= 0x8000 | 0x2000; /* ERR | CERR */
}

/**
 * Fails a TMD with a generic error.
 */
static void
pcnetXmitFailTMDGeneric(nic_t *dev, TMD *pTmd)
{
    /* make carrier error - hope this is correct. */
    pTmd->tmd2.lcar = pTmd->tmd1.err = 1;
    dev->aCSR[0] |= 0x8000 | 0x2000; /* ERR | CERR */
}

/**
 * Actually try transmit frames.
 *
 * @threads TX or EMT.
 */
static void
pcnetAsyncTransmit(nic_t *dev)
{
    /*
     * Just cleared transmit demand if the transmitter is off.
     */
    if (!CSR_TXON(dev)) {
        dev->aCSR[0] &= ~0x0008; /* Clear TDMD */
        return;
    }

    /*
     * Iterate the transmit descriptors.
     */
    unsigned cFlushIrq = 0;
    int      cMax      = 32;
    do {
        TMD tmd;
        if (!pcnetTdtePoll(dev, &tmd))
            break;

        /* Don't continue sending packets when the link is down. */
        if (!pcnetIsLinkUp(dev)
             && dev->cLinkDownReported > PCNET_MAX_LINKDOWN_REPORTED)
            break;

        pcnet_log(3, "%s: TMDLOAD %#010x\n", dev->name, PHYSADDR(dev, CSR_CXDA(dev)));

        int fLoopback = CSR_LOOP(dev);

        /*
         * The typical case - a complete packet.
         */
        if (tmd.tmd1.stp && tmd.tmd1.enp) {
            const int cb = 4096 - tmd.tmd1.bcnt;
            pcnet_log(3, "%s: pcnetAsyncTransmit: stp&enp: cb=%d xmtrc=%#x\n", dev->name, cb, CSR_XMTRC(dev));

            if (pcnetIsLinkUp(dev) || fLoopback) {

                /* From the manual: ``A zero length buffer is acceptable as
                 * long as it is not the last buffer in a chain (STP = 0 and
                 * ENP = 1).'' That means that the first buffer might have a
                 * zero length if it is not the last one in the chain. */
                if (cb <= MAX_FRAME) {
                    dev->xmit_pos = cb;
                    dma_bm_read(PHYSADDR(dev, tmd.tmd0.tbadr), dev->abLoopBuf, cb, dev->transfer_size);

                    if (fLoopback) {
                        if (HOST_IS_OWNER(CSR_CRST(dev)))
                            pcnetRdtePoll(dev);

                        pcnetReceiveNoSync(dev, dev->abLoopBuf, dev->xmit_pos);
                    } else {
                        pcnet_log(3, "%s: pcnetAsyncTransmit: transmit loopbuf stp and enp, xmit pos = %d\n", dev->name, dev->xmit_pos);
                        network_tx(dev->netcard, dev->abLoopBuf, dev->xmit_pos);
                    }
                } else if (cb == 4096) {
                    /* The Windows NT4 pcnet driver sometimes marks the first
                     * unused descriptor as owned by us. Ignore that (by
                     * passing it back). Do not update the ring counter in this
                     * case (otherwise that driver becomes even more confused,
                     * which causes transmit to stall for about 10 seconds).
                     * This is just a workaround, not a final solution.
                     */
                    /* r=frank: IMHO this is the correct implementation. The
                     * manual says: ``If the OWN bit is set and the buffer
                     * length is 0, the OWN bit will be cleared. In the C-LANCE
                     * the buffer length of 0 is interpreted as a 4096-byte
                     * buffer.''
                     */
                    /* r=michaln: Perhaps not quite right. The C-LANCE (Am79C90)
                     * datasheet explains that the old LANCE (Am7990) ignored
                     * the top four bits next to BCNT and a count of 0 was
                     * interpreted as 4096. In the C-LANCE, that is still the
                     * case if the top bits are all ones. If all 16 bits are
                     * zero, the C-LANCE interprets it as zero-length transmit
                     * buffer. It's not entirely clear if the later models
                     * (PCnet-ISA, PCnet-PCI) behave like the C-LANCE or not.
                     * It is possible that the actual behavior of the C-LANCE
                     * and later hardware is that the buffer lengths are *16-bit*
                     * two's complement numbers between 0 and 4096. AMD's drivers
                     * in fact generally treat the length as a 16-bit quantity. */
                    pcnet_log(1, "%s: pcnetAsyncTransmit: illegal 4kb frame -> ignoring\n", dev->name);
                    pcnetTmdStorePassHost(dev, &tmd, PHYSADDR(dev, CSR_CXDA(dev)));
                    break;
                } else {
                    pcnetXmitFailTMDGeneric(dev, &tmd);
                }
            } else {
                pcnetXmitFailTMDLinkDown(dev, &tmd);
            }

            /* Write back the TMD and pass it to the host (clear own bit). */
            pcnetTmdStorePassHost(dev, &tmd, PHYSADDR(dev, CSR_CXDA(dev)));

            /* advance the ring counter register */
            if (CSR_XMTRC(dev) < 2) {
                CSR_XMTRC(dev) = CSR_XMTRL(dev);
            } else {
                CSR_XMTRC(dev)
                --;
            }
        } else if (tmd.tmd1.stp) {
            /*
             * Read TMDs until end-of-packet or tdte poll fails (underflow).
             *
             * We allocate a maximum sized buffer here since we do not wish to
             * waste time finding out how much space we actually need even if
             * we could reliably do that on SMP guests.
             */
            unsigned cb   = 4096 - tmd.tmd1.bcnt;
            /* The next buffers are appended after this one. xmit_pos was the whole
               frame's length (pcnetCalcPacketLen()), so each later buffer landed past a gap of
               stale bytes: every frame a driver built from several buffers (NetWare's ODI
               drivers do: 86Box issue 5602) went out mangled and too long. */
            dev->xmit_pos = cb;
            dma_bm_read(PHYSADDR(dev, tmd.tmd0.tbadr), dev->abLoopBuf, cb, dev->transfer_size);

            for (;;) {
                /*
                 * Advance the ring counter register and check the next tmd.
                 */
                const uint32_t GCPhysPrevTmd = PHYSADDR(dev, CSR_CXDA(dev));
                if (CSR_XMTRC(dev) < 2)
                    CSR_XMTRC(dev) = CSR_XMTRL(dev);
                else
                    CSR_XMTRC(dev)
                --;

                TMD dummy;
                if (!pcnetTdtePoll(dev, &dummy)) {
                    /*
                     * Underflow!
                     */
                    tmd.tmd2.buff = tmd.tmd2.uflo = tmd.tmd1.err = 1;
                    dev->aCSR[0] |= 0x0200; /* set TINT */
                    /* Don't allow the guest to clear TINT before reading it */
                    dev->u16CSR0LastSeenByGuest &= ~0x0200;
                    if (!CSR_DXSUFLO(dev))       /* stop on xmit underflow */
                        dev->aCSR[0] &= ~0x0010; /* clear TXON */
                    pcnetTmdStorePassHost(dev, &tmd, GCPhysPrevTmd);
                    pcnet_log(3, "%s: pcnetAsyncTransmit: Underflow!!!\n", dev->name);
                    break;
                }

                /* release & save the previous tmd, pass it to the host */
                pcnetTmdStorePassHost(dev, &tmd, GCPhysPrevTmd);

                /*
                 * The next tmd.
                 */
                pcnetTmdLoad(dev, &tmd, PHYSADDR(dev, CSR_CXDA(dev)), 0);
                cb = 4096 - tmd.tmd1.bcnt;
                if (dev->xmit_pos + cb <= MAX_FRAME) { /** @todo this used to be ... + cb < MAX_FRAME. */
                    int off       = dev->xmit_pos;
                    dev->xmit_pos = cb + off;
                    dma_bm_read(PHYSADDR(dev, tmd.tmd0.tbadr), dev->abLoopBuf + off, cb, dev->transfer_size);
                }

                /*
                 * Done already?
                 */
                if (tmd.tmd1.enp) {
                    if (fLoopback) {
                        if (HOST_IS_OWNER(CSR_CRST(dev)))
                            pcnetRdtePoll(dev);

                        pcnet_log(3, "%s: pcnetAsyncTransmit: receive loopback enp\n", dev->name);
                        pcnetReceiveNoSync(dev, dev->abLoopBuf, dev->xmit_pos);
                    } else {
                        pcnet_log(3, "%s: pcnetAsyncTransmit: transmit loopbuf enp\n", dev->name);
                        network_tx(dev->netcard, dev->abLoopBuf, dev->xmit_pos);
                    }

                    /* Write back the TMD, pass it to the host */
                    pcnetTmdStorePassHost(dev, &tmd, PHYSADDR(dev, CSR_CXDA(dev)));

                    /* advance the ring counter register */
                    if (CSR_XMTRC(dev) < 2)
                        CSR_XMTRC(dev) = CSR_XMTRL(dev);
                    else
                        CSR_XMTRC(dev)
                    --;
                    break;
                }
            } /* the loop */
        }
        /* Update TDMD, TXSTRT and TINT. */
        dev->aCSR[0] &= ~0x0008; /* clear TDMD */
        dev->aCSR[4] |= 0x0008;  /* set TXSTRT */
        dev->xmit_pos = -1;
        if (!CSR_TOKINTD(dev) /* Transmit OK Interrupt Disable, no infl. on errors. */
            || (CSR_LTINTEN(dev) && tmd.tmd1.ltint)
            || tmd.tmd1.err) {
            cFlushIrq++;
        }
        if (--cMax == 0)
            break;
    } while (CSR_TXON(dev)); /* transfer on */

    if (cFlushIrq) {
        dev->aCSR[0] |= 0x0200; /* set TINT */
        /* Don't allow the guest to clear TINT before reading it */
        dev->u16CSR0LastSeenByGuest &= ~0x0200;
        pcnetUpdateIrq(dev);
    }
}

/**
 * Poll for changes in RX and TX descriptor rings.
 */
static void
pcnetPollRxTx(nic_t *dev)
{
    if (CSR_RXON(dev)) {
        /*
         * The second case is important for pcnetWaitReceiveAvail(): If CSR_CRST(dev) was
         * true but pcnetCanReceive() returned false for some other reason we need to check
         * _now_ if we have to wakeup pcnetWaitReceiveAvail().
         */
        if (HOST_IS_OWNER(CSR_CRST(dev)) || dev->fMaybeOutOfSpace)
            pcnetRdtePoll(dev);
    }

    if (CSR_TDMD(dev) || (CSR_TXON(dev) && !CSR_DPOLL(dev)))
        pcnetAsyncTransmit(dev);
}

static void
pcnetPollTimer(void *priv)
{
    nic_t *dev = (nic_t *) priv;

    timer_advance_u64(&dev->timer, 2000 * TIMER_USEC);

    if (CSR_TDMD(dev))
        pcnetAsyncTransmit(dev);

    pcnetUpdateIrq(dev);

    if (!CSR_STOP(dev) && !CSR_SPND(dev) && (!CSR_DPOLL(dev) || dev->fMaybeOutOfSpace))
        pcnetPollRxTx(dev);
}

/* The H_RESET values of the BCRs (the EEPROM-programmable ones included). */
static void
pcnetBcrDefaults(nic_t *dev)
{
    dev->aBCR[BCR_MSRDA] = 0x0005;
    dev->aBCR[BCR_MSWRA] = 0x0005;
    dev->aBCR[BCR_MC]    = 0x0002;
    dev->aBCR[BCR_LNKST] = 0x00c0;
    dev->aBCR[BCR_LED1]  = 0x0084;
    dev->aBCR[BCR_LED2]  = 0x0088;
    dev->aBCR[BCR_LED3]  = 0x0090;

    dev->aBCR[BCR_FDC]   = 0x0000;
    /* The PCnet-ISA II loads ISACSR9 from its EEPROM's word 10h at RESET (data sheet,
       EEPROM byte map); 0000h, half duplex, is its default without one. */
    if (dev->board == DEV_AM79C961A)
        dev->aBCR[BCR_FDC] = dev->isa2_dup & 0x0003;
    dev->aBCR[BCR_BSBC]  = 0x9001;
    /* The Am79C965A's BCR18 is 2101h: CLL 00100b, bit 8 reads one, LINBC 001. */
    if (dev->is_vlb)
        dev->aBCR[BCR_BSBC] = 0x2101;
    dev->aBCR[BCR_EECAS] = 0x0002;
    dev->aBCR[BCR_STVAL] = 0xffff;
    dev->aCSR[58] = dev->aBCR[BCR_SWS] = 0x0200; /* CSR58 is an alias for BCR20 */
    dev->iLog2DescSize                 = 3;
    dev->aBCR[BCR_PLAT]                = 0xff06;
    dev->aBCR[BCR_MIICAS]              = 0x20; /* Auto-negotiation on. */
    dev->aBCR[BCR_MIIADDR]             = 0;
    /* The Am79C973/975's internal PHY is at 1Eh; an EEPROM sets PHYAD so. */
    if (pcnet_chip(dev) && (pcnet_chip(dev)->phyad > 0))
        dev->aBCR[BCR_MIIADDR] = pcnet_chip(dev)->phyad << 5;
    if (pcnet_has_pm(dev)) {
        dev->aBCR[BCR_PMC_A] = pcnet_chip(dev)->pmc;
        dev->aCSR[116] &= 0x3800; /* the D3cold bits are reset only at power-on */
        dev->aBCR[BCR_PMR1] &= ~0x0080;
    }
    dev->aBCR[BCR_PCIVID]              = 0x1022;
    /* On the FAST family BCR23/24 are what PCI configuration space 2Ch-2Fh shows;
       they default to AMD's own 1022h:2000h, as AMD's reference EEPROMs have it. */
    dev->aBCR[BCR_PCISID]              = pcnet_is_fast(dev) ? 0x2000 : 0x0020;
    dev->aBCR[BCR_PCISVID]             = 0x1022;
}

static uint8_t
pcnet_eeprom_byte(const uint16_t *words, int b)
{
    return (words[b >> 1] >> ((b & 1) << 3)) & 0xff;
}

/* The automatic EEPROM read (after H_RESET, or BCR19 PREAD). A good checksum loads the
   Address PROM and the EEPROM-programmable registers and sets PVALID; a bad one puts those
   registers back to their H_RESET values and leaves the Address PROM alone. */
static void
pcnet_eeprom_load(nic_t *dev)
{
    const pcnet_eeprom_map_t *map   = dev->eeprom_map;
    const uint16_t           *words = nmc93cxx_eeprom_data(dev->eeprom);
    uint8_t                   sum   = 0;
    int                       valid;

    for (int b = 0; b < map->bytes; b++)
        sum += pcnet_eeprom_byte(words, b);
    valid = (sum == 0xff);

    if (valid) {
        for (int b = 0; b < 16; b++)
            dev->aPROM[b] = pcnet_eeprom_byte(words, b);
    } else
        words = dev->eeprom_default;

    for (int i = 0; i < map->nregs; i++) {
        int      w = map->first + i;
        uint16_t v = words[w];

        if (map->regs[i] == PCNET_EE_SKIP)
            continue;
        if ((w << 1) + 1 == map->adjust)
            v &= 0x00ff;
        if (map->regs[i] & PCNET_EE_CSR)
            dev->aCSR[map->regs[i] & 0xff] = v;
        else
            dev->aBCR[map->regs[i]] = v;
    }
    if (map->csr116_word >= 0)
        dev->aCSR[116] = words[map->csr116_word];

    if (dev->netcard)
        dev->netcard->byte_period = (pcnet_is_fast(dev) && (dev->aBCR[BCR_MIICAS] & 0x28)) ? NET_PERIOD_100M : NET_PERIOD_10M;

    /* EEDET: an EEPROM is there; PVALID: its contents passed the checksum. */
    dev->aBCR[BCR_EECAS] = valid ? 0xa000 : 0x2000;

    /* The Am79C965A. ESK is one after H_RESET (BCR19). BCR16/17 hold the I/O base in
       use (the configured one, see pcnet_ee_regs_965). A failed checksum puts it in Software
       Relocatable Mode: it stops decoding I/O and waits for the 378h sequence ("Software
       Relocatable Mode", "EEPROM-Programmable Registers"). */
    if (dev->is_vlb) {
        dev->aBCR[BCR_EECAS] |= 0x0002;
        dev->aBCR[16] = dev->base_address & 0xffe0;
        dev->aBCR[17] = 0x0000;
        dev->srm      = !valid;
        dev->srm_pos  = 0;
        pcnet_vlb_bsbc_fix(dev);
        pcnet_vlb_io_update(dev);
    }

    pcnet_log(1, "%s: EEPROM read, checksum %s\n", dev->name, valid ? "good" : "bad");
}

/* Recompute the station address checksum (bytes 0Ch/0Dh) and the checksum adjust byte. */
static void
pcnet_eeprom_checksums(const pcnet_eeprom_map_t *map, uint16_t *words)
{
    uint16_t sum16 = 0;
    uint8_t  sum   = 0;

    for (int b = 0; b < 16; b++)
        if ((b != 12) && (b != 13))
            sum16 += pcnet_eeprom_byte(words, b);
    words[6] = sum16;

    words[map->adjust >> 1] &= (map->adjust & 1) ? 0x00ff : 0xff00;
    for (int b = 0; b < map->bytes; b++)
        sum += pcnet_eeprom_byte(words, b);
    words[map->adjust >> 1] |= (uint8_t) (0xff - sum) << ((map->adjust & 1) << 3);
}

static void
pcnetHardReset(nic_t *dev)
{
    pcnet_log(2, "%s: pcnetHardReset\n", dev->name);

    dev->iISR = 0;
    pcnet_do_irq(dev, 0);

    pcnetBcrDefaults(dev);
    if (dev->eeprom) {
        nmc93cxx_eeprom_write(dev->eeprom, 0, 0, 0);
        pcnet_eeprom_load(dev);
    }

    /* Reset the error counter. */
    dev->uCntBadRMD = 0;

    pcnetSoftReset(dev);
}

static void
pcnet_csr_writew(nic_t *dev, uint16_t rap, uint16_t val)
{
    pcnet_log(1, "%s: pcnet_csr_writew: rap=%d val=%#06x\n", dev->name, rap, val);
    switch (rap) {
        case 0:
            {
                uint16_t csr0 = dev->aCSR[0];
                /* Clear any interrupt flags.
                 * Don't clear an interrupt flag which was not seen by the guest yet. */
                csr0 &= ~(val & 0x7f00 & dev->u16CSR0LastSeenByGuest);
                csr0 = (csr0 & ~0x0040) | (val & 0x0048);
                val  = (val & 0x007f) | (csr0 & 0x7f00);

                /* If STOP, STRT and INIT are set, clear STRT and INIT */
                if ((val & 7) == 7)
                    val &= ~3;

                pcnet_log(2, "%s: CSR0 val = %04x, val2 = %04x\n", dev->name, val, dev->aCSR[0]);

                dev->aCSR[0] = csr0;

                if (!CSR_STOP(dev) && (val & 4)) {
                    pcnet_log(3, "%s: pcnet_csr_writew(): Stop\n", dev->name);
                    pcnetStop(dev);
                }

                if (!CSR_INIT(dev) && (val & 1)) {
                    pcnet_log(3, "%s: pcnet_csr_writew(): Init\n", dev->name);
                    pcnetInit(dev);
                }

                if (!CSR_STRT(dev) && (val & 2)) {
                    pcnet_log(3, "%s: pcnet_csr_writew(): Start\n", dev->name);
                    pcnetStart(dev);
                }

                if (CSR_TDMD(dev)) {
                    pcnet_log(3, "%s: pcnet_csr_writew(): Transmit\n", dev->name);
                    pcnetAsyncTransmit(dev);
                }
            }
            return;

        case 2: /* IADRH */
            if (dev->is_isa)
                val &= 0x00ff; /* Upper 8 bits ignored on ISA chips. */
        case 1:                /* IADRL */
        case 8:                /* LADRF  0..15 */
        case 9:                /* LADRF 16..31 */
        case 10:               /* LADRF 32..47 */
        case 11:               /* LADRF 48..63 */
        case 12:               /* PADR   0..15 */
        case 13:               /* PADR  16..31 */
        case 14:               /* PADR  32..47 */
        case 18:               /* CRBAL */
        case 19:               /* CRBAU */
        case 20:               /* CXBAL */
        case 21:               /* CXBAU */
        case 22:               /* NRBAL */
        case 23:               /* NRBAU */
        case 26:               /* NRDAL */
        case 27:               /* NRDAU */
        case 28:               /* CRDAL */
        case 29:               /* CRDAU */
        case 32:               /* NXDAL */
        case 33:               /* NXDAU */
        case 34:               /* CXDAL */
        case 35:               /* CXDAU */
        case 36:               /* NNRDL */
        case 37:               /* NNRDU */
        case 38:               /* NNXDL */
        case 39:               /* NNXDU */
        case 40:               /* CRBCL */
        case 41:               /* CRBCU */
        case 42:               /* CXBCL */
        case 43:               /* CXBCU */
        case 44:               /* NRBCL */
        case 45:               /* NRBCU */
        case 46:               /* POLL */
        case 47:               /* POLLINT */
        case 112:              /* MISSC */
            if (CSR_STOP(dev) || CSR_SPND(dev))
                break;
            return;
        case 72: /* RCVRC */
        case 74: /* XMTRC */
            if (!CSR_STOP(dev) && !CSR_SPND(dev))
                return;
            val = -val;
            break;
        case 3: /* Interrupt Mask and Deferral Control */
            break;
        case 4: /* Test and Features Control */
            dev->aCSR[4] &= ~(val & 0x026a);
            val &= ~0x026a;
            val |= dev->aCSR[4] & 0x026a;
            break;
        case 5: /* Extended Control and Interrupt 1 */
            dev->aCSR[5] &= ~(val & 0x0a90);
            val &= ~0x0a90;
            val |= dev->aCSR[5] & 0x0a90;
            break;
        case 7: /* Extended Control and Interrupt 2 */
            {
                uint16_t csr7 = dev->aCSR[7];
                csr7 &= ~0x0400;
                csr7 &= ~(val & 0x0800);
                csr7 |= (val & 0x0400);
                dev->aCSR[7] = csr7;
            }
            return;
        case 15: /* Mode */
            break;
        case 16: /* IADRL */
            pcnet_csr_writew(dev, 1, val);
            return;
        case 17: /* IADRH */
            pcnet_csr_writew(dev, 2, val);
            return;
        /*
         * 24 and 25 are the Base Address of Receive Descriptor.
         * We combine and mirror these in GCRDRA.
         */
        case 24: /* BADRL */
        case 25: /* BADRU */
            if (!CSR_STOP(dev) && !CSR_SPND(dev)) {
                pcnet_log(3, "%s: WRITE CSR%d, %#06x, ignoring!!\n", dev->name, rap, val);
                return;
            }
            if (rap == 24)
                dev->GCRDRA = (dev->GCRDRA & 0xffff0000) | (val & 0x0000ffff);
            else
                dev->GCRDRA = (dev->GCRDRA & 0x0000ffff) | ((val & 0x0000ffff) << 16);
            pcnet_log(3, "%s: WRITE CSR%d, %#06x => GCRDRA=%08x (alt init)\n", dev->name, rap, val, dev->GCRDRA);
            if (dev->GCRDRA & (dev->iLog2DescSize - 1))
                pcnet_log(1, "%s: Warning: Misaligned RDRA (GCRDRA=%#010x)\n", dev->name, dev->GCRDRA);
            break;
        /*
         * 30 & 31 are the Base Address of Transmit Descriptor.
         * We combine and mirrorthese in GCTDRA.
         */
        case 30: /* BADXL */
        case 31: /* BADXU */
            if (!CSR_STOP(dev) && !CSR_SPND(dev)) {
                pcnet_log(3, "%s: WRITE CSR%d, %#06x !!\n", dev->name, rap, val);
                return;
            }
            if (rap == 30)
                dev->GCTDRA = (dev->GCTDRA & 0xffff0000) | (val & 0x0000ffff);
            else
                dev->GCTDRA = (dev->GCTDRA & 0x0000ffff) | ((val & 0x0000ffff) << 16);

            pcnet_log(3, "%s: WRITE CSR%d, %#06x => GCTDRA=%08x (alt init)\n", dev->name, rap, val, dev->GCTDRA);

            if (dev->GCTDRA & (dev->iLog2DescSize - 1))
                pcnet_log(1, "%s: Warning: Misaligned TDRA (GCTDRA=%#010x)\n", dev->name, dev->GCTDRA);
            break;
        case 58: /* Software Style */
            pcnet_bcr_writew(dev, BCR_SWS, val);
            break;
        case 80:  /* CSR80 (burst and FIFO thresholds), CSR82 (bus activity timer) */
        case 82:  /* and CSR100 (bus timeout) are setup registers, written only while stopped */
        case 100: /* (or suspended, on the chips with SPND), as each chip's data sheet has it. */
            if (!CSR_STOP(dev) && !(pcnet_has_spnd(dev) && CSR_SPND(dev)))
                return;
            /* The FAST family's CSR82 is TXDAPL, the transmit descriptor address, written only
               through CSR60; the ISA chips have no CSR100. */
            if ((rap == 82) && pcnet_is_fast(dev))
                return;
            if ((rap == 100) && !dev->is_pci && !dev->is_vlb)
                return;
            if ((rap == 80) && dev->is_vlb) /* the Am79C965A's 15:14 read ones */
                val |= 0xc000;
            break;
        case 116: /* OnNow Power Mode (Am79C972/973/975) */
            if (!pcnet_has_pm(dev))
                return;
            /* LCMODE_D3C/MPPEN_D3C/PMAT_MODE_D3C, EMPPLBA, LCDET and MPMAT always;
               PME_EN_OVR, LCMODE, MPPEN and the RWU bits only while stopped or
               suspended. PMAT is read only. */
            {
                uint16_t mask = 0x3a60;
                if (CSR_STOP(dev) || CSR_SPND(dev))
                    mask |= 0x051e;
                val = (dev->aCSR[116] & ~mask) | (val & mask);
            }
            break;
        /*
         * The chip keeps RCVRC/XMTRC (CSR72/74) and RCVRL/XMTRL (CSR76/78) as two's
         * complement numbers; internally they are kept positive (the ring code counts the
         * descriptors down from the length to 1), so they are negated on the way in and out.
         * A new ring length also points the ring counter at the first descriptor, so the
         * counter never lies outside the new ring.
         */
        case 76: /* RCVRL */
        case 78: /* XMTRL */
            if (!CSR_STOP(dev) && !CSR_SPND(dev)) {
                pcnet_log(3, "%s: WRITE CSR%d, %#06x !!\n", dev->name, rap, val);
                return;
            }
            val = -val;
            if (val == 0) /* 0 is a ring of 65536: not usable */
                return;
            pcnet_log(3, "%s: WRITE CSR%d => length %d (alt init)\n", dev->name, rap, val);
            dev->aCSR[rap - 4] = val;
            break;
        default:
            return;
    }

    dev->aCSR[rap] = val;
}

/**
 * Encode a 32-bit link speed into a custom 16-bit floating-point value
 */
static uint16_t
pcnetLinkSpd(uint32_t speed)
{
    unsigned exp = 0;

    while (speed & 0xFFFFE000) {
        speed /= 10;
        ++exp;
    }
    return (exp << 13) | speed;
}

static uint16_t
pcnet_csr_readw(nic_t *dev, uint16_t rap)
{
    uint16_t val;

    switch (rap) {
        case 0:
            pcnetUpdateIrq(dev);
            val = dev->aCSR[0];
            val |= (val & 0x7800) ? 0x8000 : 0;
            dev->u16CSR0LastSeenByGuest = val;
            break;
        case 16:
            return pcnet_csr_readw(dev, 1);
        case 17:
            return pcnet_csr_readw(dev, 2);
        case 58:
            return pcnet_bcr_readw(dev, BCR_SWS);
        case 68: /* Custom register to pass link speed to driver */
            return pcnetLinkSpd(dev->u32LinkSpeed);
        case 72: /* RCVRC */
        case 74: /* XMTRC */
        case 76: /* RCVRL */
        case 78: /* XMTRL */
            val = -dev->aCSR[rap]; /* Two's complement, see pcnet_csr_writew() */
            break;
        default:
            val = dev->aCSR[rap];
            break;
    }
    pcnet_log(3, "%s: pcnet_csr_readw rap=%d val=0x%04x\n", dev->name, rap, val);
    return val;
}

static void
pcnet_bcr_writew(nic_t *dev, uint16_t rap, uint16_t val)
{
    rap &= 0x7f;
    pcnet_log(3, "%s: pcnet_bcr_writew rap=%d val=0x%04x\n", dev->name, rap, val);
    switch (rap) {
        case BCR_SWS:
            if (!(CSR_STOP(dev) || CSR_SPND(dev)))
                return;
            val &= ~0x0300;
            switch (val & 0x00ff) {
                default:
                case 0:
                    val |= 0x0200; /* 16 bit */
                    dev->iLog2DescSize = 3;
                    dev->GCUpperPhys   = (0xff00 & dev->aCSR[2]) << 16;
                    break;
                case 1:
                    val |= 0x0100; /* 32 bit */
                    dev->iLog2DescSize = 4;
                    dev->GCUpperPhys   = 0;
                    break;
                case 2:
                case 3:
                    val |= 0x0300; /* 32 bit */
                    dev->iLog2DescSize = 4;
                    dev->GCUpperPhys   = 0;
                    break;
            }
            dev->aCSR[58] = val;
            fallthrough;
        case BCR_LNKST:
        case BCR_LED1:
        case BCR_LED2:
        case BCR_LED3:
        case BCR_MC:
        case BCR_FDC:
        case BCR_BSBC:
        case BCR_PLAT:
        case BCR_MIIADDR:
            /* A chip without BCR9 ignores writes to it. */
            if ((rap == BCR_FDC) && !pcnet_has_bcr9(dev))
                break;
            /* The PCnet-ISA II's ISACSR9 has only AUIFD and FDEN (bits 1:0). */
            if ((rap == BCR_FDC) && (dev->board == DEV_AM79C961A))
                val &= 0x0003;
            /* The Am79C965A's BCR2 keeps T-MAULOOP, IESRWE, INTLEVEL, EADISEL, AWAKE,
               ASEL and bit 0, the rest reads zero; INTLEVEL changes the interrupt pin's mode. */
            if (dev->is_vlb && (rap == BCR_MC)) {
                dev->aBCR[rap] = val & 0x418f;
                pcnetUpdateIrq(dev);
                break;
            }
            /* In its BCR18 DWIO (bit 7) is read only -- set by a double word write to
               RDP -- and TSTSHDW (4:3) only the EEPROM sets; bit 8 reads one. */
            if (dev->is_vlb && (rap == BCR_BSBC)) {
                dev->aBCR[rap] = (dev->aBCR[rap] & 0x0098) | (val & 0xfe67);
                pcnet_vlb_bsbc_fix(dev);
                break;
            }
            dev->aBCR[rap] = val;
            break;

        case BCR_IOBASEL: /* The Am79C965A's I/O base, BCR16/17, moves its I/O space */
        case BCR_IOBASEU:
            if (!dev->is_vlb)
                break;
            dev->aBCR[rap] = (rap == BCR_IOBASEL) ? (val & 0xffe0) : val;
            pcnet_vlb_io_update(dev);
            break;

        case BCR_INTCON: /* Am79C965A BCR21: IOAW24, REJECTDIS, INTSEL; only while stopped */
            if (!dev->is_vlb || !CSR_STOP(dev))
                break;
            dev->aBCR[rap] = val & 0x0183;
            pcnet_vlb_io_update(dev); /* IOAW24 */
            break;

        case BCR_MIICAS:
            dev->netcard->byte_period = (pcnet_is_fast(dev) && (val & 0x28)) ? NET_PERIOD_100M : NET_PERIOD_10M;
            dev->aBCR[rap]            = val;
            break;

        case BCR_STVAL:
            val &= 0xffff;
            dev->aBCR[BCR_STVAL] = val;
            if (pcnet_is_fast(dev))
                timer_set_delay_u64(&dev->timer_soft_int, (12.8 * val) * TIMER_USEC);
            break;

        case BCR_MIIMDR:
            /* Only the PHY at its own address takes the write; a soft reset
               (BMCR bit 15) completes at once. */
            if (pcnet_chip(dev) && (pcnet_chip(dev)->phyad >= 0) &&
                (((dev->aBCR[BCR_MIIADDR] >> 5) & 0x1f) == pcnet_chip(dev)->phyad)) {
                if ((dev->aBCR[BCR_MIIADDR] & 0x1f) == 0) {
                    if (val & 0x8000)
                        val = 0x3000;
                    val &= 0x7fff;
                }
                dev->aMII[dev->aBCR[BCR_MIIADDR] & 0x1f] = val;
            }
            break;

        case BCR_PMR1: /* OnNow Pattern Match RAM access */
            if (!pcnet_has_pm(dev))
                break;
            if (!(val & 0x0080))
                dev->pmr_wr[0] = val >> 8;
            dev->aBCR[rap] = val & 0xffff;
            break;
        case BCR_PMR2:
            if (!pcnet_has_pm(dev) || (dev->aBCR[BCR_PMR1] & 0x0080))
                break;
            dev->pmr_wr[1] = val & 0xff;
            dev->pmr_wr[2] = val >> 8;
            break;
        case BCR_PMR3:
            if (!pcnet_has_pm(dev) || (dev->aBCR[BCR_PMR1] & 0x0080))
                break;
            dev->pmr_wr[3] = val & 0xff;
            dev->pmr_wr[4] = val >> 8;
            memcpy(dev->pmr[dev->aBCR[BCR_PMR1] & 0x3f], dev->pmr_wr, 5);
            break;

        case BCR_EECAS:
            if (!dev->eeprom) {
                dev->aBCR[rap] = val;
                break;
            }
            /* PREAD (only while stopped or suspended) re-reads the EEPROM at once;
               otherwise EEN drives ECS/ESK/EDI onto the Microwire pins. */
            if ((val & 0x4000) && (CSR_STOP(dev) || CSR_SPND(dev))) {
                nmc93cxx_eeprom_write(dev->eeprom, 0, 0, 0);
                pcnet_eeprom_load(dev);
                break;
            }
            /* The Am79C965A also keeps EBUSY (bit 3, its SHFBUSY pin). */
            dev->aBCR[rap] = (dev->aBCR[rap] & 0xa000) | (val & (dev->is_vlb ? 0x001f : 0x0017));
            if (val & 0x0010)
                nmc93cxx_eeprom_write(dev->eeprom, !!(val & 0x0004), !!(val & 0x0002), !!(val & 0x0001));
            else
                nmc93cxx_eeprom_write(dev->eeprom, 0, 0, 0);
            break;

        default:
            break;
    }
}

static uint16_t
pcnet_mii_readw(nic_t *dev, uint16_t miiaddr)
{
    uint16_t val;
    int      autoneg;
    int      duplex;
    int      fast;
    int      isolate;

    /* If the DANAS (BCR32.7) bit is set, the MAC does not do any
     * auto-negotiation and the PHY must be set up explicitly. DANAS
     * effectively disables most other BCR32 bits.
     */
    if (dev->aBCR[BCR_MIICAS] & 0x80) {
        /* PHY controls auto-negotiation. */
        autoneg = duplex = fast = 1;
    } else {
        /* BCR32 controls auto-negotiation. */
        autoneg = (dev->aBCR[BCR_MIICAS] & 0x20) != 0;
        duplex  = (dev->aBCR[BCR_MIICAS] & 0x10) != 0;
        fast    = (dev->aBCR[BCR_MIICAS] & 0x08) != 0;
    }

    /* Electrically isolating the PHY mostly disables it. */
    isolate = (dev->aMII[0] & 0x400) != 0;

    switch (miiaddr) {
        case 0:
            /* MII basic mode control register. */
            val = 0;
            if (autoneg)
                val |= 0x1000; /* Enable auto negotiation. */
            if (fast)
                val |= 0x2000; /* 100 Mbps */
            if (duplex)        /* Full duplex forced */
                val |= 0x0100; /* Full duplex */
            if (isolate)       /* PHY electrically isolated. */
                val |= 0x0400; /* Isolated */
            break;

        case 1:
            /* MII basic mode status register. */
            val = 0x7800  /* Can do 100mbps FD/HD and 10mbps FD/HD. */
                | 0x0040  /* Mgmt frame preamble not required. */
                | 0x0020  /* Auto-negotiation complete. */
                | 0x0008  /* Able to do auto-negotiation. */
                | 0x0004  /* Link up. */
                | 0x0001; /* Extended Capability, i.e. registers 4+ valid. */
            if (!pcnetIsLinkUp(dev) || isolate) {
                val &= ~(0x0020 | 0x0004);
                dev->cLinkDownReported++;
            }
            if (!autoneg) {
                /* Auto-negotiation disabled. */
                val &= ~(0x0020 | 0x0008);
                if (duplex)
                    val &= ~0x2800; /* Full duplex forced. */
                else
                    val &= ~0x5000; /* Half duplex forced. */

                if (fast)
                    val &= ~0x1800; /* 100 Mbps forced */
                else
                    val &= ~0x6000; /* 10 Mbps forced */
            }
            break;

        case 2:
            /* PHY identifier 1. */
            val = pcnet_chip(dev)->phyid1; /* Am79C874 on the MII, 0000h inside the Am79C973 */
            break;

        case 3:
            /* PHY identifier 2. */
            val = pcnet_chip(dev)->phyid2;
            break;

        case 4:
            /* Advertisement control register. */
            val = 0x01e0  /* Try 100mbps FD/HD and 10mbps FD/HD. */
                | 0x0001; /* CSMA selector. */
            break;

        case 5:
            /* Link partner ability register. */
            if (pcnetIsLinkUp(dev) && !isolate) {
                val = 0x8000  /* Next page bit. */
                    | 0x4000  /* Link partner acked us. */
                    | 0x0400  /* Can do flow control. */
                    | 0x01e0  /* Can do 100mbps FD/HD and 10mbps FD/HD. */
                    | 0x0001; /* Use CSMA selector. */
            } else {
                val = 0;
                dev->cLinkDownReported++;
            }
            break;

        case 6:
            /* Auto negotiation expansion register. */
            if (pcnetIsLinkUp(dev) && !isolate) {
                val = 0x0008  /* Link partner supports npage. */
                    | 0x0004  /* Enable npage words. */
                    | 0x0001; /* Can do N-way auto-negotiation. */
            } else {
                val = 0;
                dev->cLinkDownReported++;
            }
            break;

        case 18:
            /* Diagnostic Register (FreeBSD pcn/ac101 driver reads this). */
            if (pcnetIsLinkUp(dev) && !isolate) {
                val = 0x1000  /* Receive PLL locked. */
                    | 0x0200; /* Signal detected. */

                if (autoneg) {
                    val |= 0x0400 /* 100Mbps rate. */
                        | 0x0800; /* Full duplex. */
                } else {
                    if (fast)
                        val |= 0x0400; /* 100Mbps rate. */
                    if (duplex)
                        val |= 0x0800; /* Full duplex. */
                }
            } else {
                val = 0;
                dev->cLinkDownReported++;
            }
            break;

        default:
            val = 0;
            break;
    }

    return val;
}

static uint16_t
pcnet_bcr_readw(nic_t *dev, uint16_t rap)
{
    uint16_t val;
    rap &= 0x7f;
    switch (rap) {
        case BCR_FDC: /* Zero on the chips without one */
            val = pcnet_has_bcr9(dev) ? dev->aBCR[rap] : 0x0000;
            break;

        case BCR_LNKST:
        case BCR_LED1:
        case BCR_LED2:
        case BCR_LED3:
            val = dev->aBCR[rap] & ~0x8000;
            if (!(pcnetIsLinkUp(dev))) {
                if (rap == 4)
                    dev->cLinkDownReported++;
                val &= ~0x40;
            }
            val |= (val & 0x017f & dev->u32Lnkst) ? 0x8000 : 0;
            if (dev->board == DEV_AM79C961A) {
                /* On the PCnet-ISA II, LEDOUT also shows FDLSE (bit 8, full duplex
                   link status). ISACSR4 is not programmable: bit 15 is LNKST alone, masked
                   while 10BASE-T runs full duplex with AUIFD clear and an LED has FDLSE. */
                if ((rap != BCR_LNKST) && (val & 0x0100) && pcnet_isa2_fd_link(dev))
                    val |= 0x8000;
                if (rap == BCR_LNKST) {
                    val = pcnetIsLinkUp(dev) ? 0x8000 : 0x0000;
                    if (pcnet_isa2_full_duplex(dev) && (pcnet_isa2_port(dev) == PCNET_PORT_10BT) &&
                        !(dev->aBCR[BCR_FDC] & 0x0002) &&
                        ((dev->aBCR[BCR_LED1] | dev->aBCR[BCR_LED2] | dev->aBCR[BCR_LED3]) & 0x0100))
                        val = 0x0000;
                }
            }
            break;

        case BCR_MIIMDR:
            /* The PHY answers at its own address only (1Eh inside the Am79C973/975). */
            if (pcnet_chip(dev) && (pcnet_chip(dev)->phyad >= 0) &&
                (((dev->aBCR[BCR_MIIADDR] >> 5) & 0x1f) == pcnet_chip(dev)->phyad)) {
                uint16_t miiaddr = dev->aBCR[BCR_MIIADDR] & 0x1f;
                val              = pcnet_mii_readw(dev, miiaddr);
            } else
                val = 0xffff;
            break;

        case BCR_EECAS:
            val = dev->aBCR[rap];
            /* With EEN set, bit 0 reads the EEDO pin. */
            if (dev->eeprom && (val & 0x0010))
                val = (val & ~0x0001) | nmc93cxx_eeprom_read(dev->eeprom);
            break;

        case BCR_PMR1: /* Only PMAT_MODE reads back while matching */
            if (dev->aBCR[BCR_PMR1] & 0x0080)
                val = 0x0080;
            else
                val = (dev->pmr[dev->aBCR[BCR_PMR1] & 0x3f][0] << 8) | (dev->aBCR[BCR_PMR1] & 0x7f);
            break;
        case BCR_PMR2:
            val = dev->pmr[dev->aBCR[BCR_PMR1] & 0x3f][1] | (dev->pmr[dev->aBCR[BCR_PMR1] & 0x3f][2] << 8);
            break;
        case BCR_PMR3:
            val = dev->pmr[dev->aBCR[BCR_PMR1] & 0x3f][3] | (dev->pmr[dev->aBCR[BCR_PMR1] & 0x3f][4] << 8);
            break;

        case BCR_SWCONFIG:
            if ((dev->board == DEV_AM79C961) || (dev->board == DEV_AM79C961A))
                val = ((dev->base_irq & 0x0f) << 4) | (dev->dma_channel & 0x07);
            else
                val = 0xffff;
            break;

        default:
            val = rap < BCR_MAX_RAP ? dev->aBCR[rap] : 0;
            break;
    }

    pcnet_log(3, "pcnet_bcr_readw rap=%d val=0x%04x\n", rap, val);
    return val;
}

static void
pcnet_word_write(nic_t *dev, uint32_t addr, uint16_t val)
{
    pcnet_log(3, "%s: pcnet_word_write: addr = %04x, val = %04x, DWIO not set = %04x\n", dev->name, addr & 0x0f, val, !BCR_DWIO(dev));

    if (!BCR_DWIO(dev)) {
        switch (addr & 0x0f) {
            case 0x00: /* RDP */
                timer_set_delay_u64(&dev->timer, 2000 * TIMER_USEC);
                pcnet_csr_writew(dev, dev->u32RAP, val);
                pcnetUpdateIrq(dev);
                break;
            case 0x02:
                dev->u32RAP = val & 0x7f;
                break;
            case 0x06:
                pcnet_bcr_writew(dev, dev->u32RAP, val);
                break;

            default:
                break;
        }
    }
}

static uint8_t
pcnet_byte_read(nic_t *dev, uint32_t addr)
{
    uint8_t val = 0xff;

    if (!BCR_DWIO(dev)) {
        switch (addr & 0x0f) {
            case 0x04:
                pcnetSoftReset(dev);
                val = 0;
                break;

            default:
                break;
        }
    }

    pcnetUpdateIrq(dev);

    pcnet_log(3, "%s: pcnet_word_read: addr = %04x, val = %04x, DWIO not set = %04x\n", dev->name, addr & 0x0f, val, !BCR_DWIO(dev));

    return val;
}

static uint16_t
pcnet_word_read(nic_t *dev, uint32_t addr)
{
    uint16_t val = 0xffff;

    if (!BCR_DWIO(dev)) {
        switch (addr & 0x0f) {
            case 0x00: /* RDP */
                /** @note if we're not polling, then the guest will tell us when to poll by setting TDMD in CSR0 */
                /** Polling is then useless here and possibly expensive. */
                if (!CSR_DPOLL(dev))
                    timer_set_delay_u64(&dev->timer, 2000 * TIMER_USEC);

                val = pcnet_csr_readw(dev, dev->u32RAP);
                if (dev->u32RAP == 0)
                    goto skip_update_irq;
                break;
            case 0x02:
                val = dev->u32RAP;
                goto skip_update_irq;
            case 0x04:
                pcnetSoftReset(dev);
                val = 0;
                break;
            case 0x06:
                val = pcnet_bcr_readw(dev, dev->u32RAP);
                break;

            default:
                break;
        }
    }

    pcnetUpdateIrq(dev);

skip_update_irq:
    pcnet_log(3, "%s: pcnet_word_read: addr = %04x, val = %04x, DWIO not set = %04x\n", dev->name, addr & 0x0f, val, !BCR_DWIO(dev));

    return val;
}

static void
pcnet_dword_write(nic_t *dev, uint32_t addr, uint32_t val)
{
    if (BCR_DWIO(dev)) {
        switch (addr & 0x0f) {
            case 0x00: /* RDP */
                timer_set_delay_u64(&dev->timer, 2000 * TIMER_USEC);
                pcnet_csr_writew(dev, dev->u32RAP, val & 0xffff);
                pcnetUpdateIrq(dev);
                break;
            case 0x04:
                dev->u32RAP = val & 0x7f;
                break;
            case 0x0c:
                pcnet_bcr_writew(dev, dev->u32RAP, val & 0xffff);
                break;

            default:
                break;
        }
    } else if ((addr & 0x0f) == 0) {
        /* switch device to dword i/o mode */
        dev->aBCR[BCR_BSBC] |= 0x0080; /* Not through BCR18, where DWIO is read only */
        pcnet_log(3, "%s: device switched into dword i/o mode\n", dev->name);
    }
}

static uint32_t
pcnet_dword_read(nic_t *dev, uint32_t addr)
{
    uint32_t val = 0xffffffff;

    if (BCR_DWIO(dev)) {
        switch (addr & 0x0f) {
            case 0x00: /* RDP */
                if (!CSR_DPOLL(dev))
                    timer_set_delay_u64(&dev->timer, 2000 * TIMER_USEC);
                val = pcnet_csr_readw(dev, dev->u32RAP);
                /* In DWIO mode CSR88 reads the whole 32-bit chip ID (Am79C965A,
                   "RDP Access": the upper halves of all CSRs but CSR88 are reserved). */
                if (dev->is_vlb && (dev->u32RAP == 88))
                    val |= (uint32_t) dev->aCSR[89] << 16;
                if (dev->u32RAP == 0)
                    goto skip_update_irq;
                break;
            case 0x04:
                val = dev->u32RAP;
                goto skip_update_irq;
            case 0x08:
                pcnetSoftReset(dev);
                val = 0;
                break;
            case 0x0c:
                val = pcnet_bcr_readw(dev, dev->u32RAP);
                break;

            default:
                break;
        }
    }

    pcnetUpdateIrq(dev);

skip_update_irq:
    pcnet_log(3, "%s: Read Long mode, addr = %08x, val = %08x\n", dev->name, addr, val);
    return val;
}

static void
pcnet_aprom_writeb(nic_t *dev, uint32_t addr, uint32_t val)
{
    /* Check APROMWE bit to enable write access */
    /* It is BCR2 bit 8 on every chip that has one (APROMWE on the PCnet-PCI II and the
       FAST family, IESRWE on the PCnet-PCI and PCnet-32); bit 7 was the C-LANCE's numbering. The
       ISA chips read their address from an external PROM or the EEPROM, which writes never reach. */
    if ((dev->is_pci || dev->is_vlb) && (pcnet_bcr_readw(dev, 2) & 0x0100))
        dev->aPROM[addr & 0x0f] = val & 0xff;
}

static uint32_t
pcnet_aprom_readb(nic_t *dev, uint32_t addr)
{
    uint32_t val = dev->aPROM[addr & 15];

    return val;
}

static void
pcnet_write(nic_t *dev, uint32_t addr, uint32_t val, int len)
{
    uint16_t off = addr & 0x1f;

    pcnet_log(3, "%s: write addr %x, val %x, off %x, len %d\n", dev->name, addr, val, off, len);

    if (off < 0x10) {
        /* The Am79C965A's shadow RAM never takes byte writes ("APROM Access"). */
        if (!BCR_DWIO(dev) && len == 1 && !dev->is_vlb)
            pcnet_aprom_writeb(dev, addr, val);
        else if (!BCR_DWIO(dev) && ((addr & 1) == 0) && len == 2) {
            pcnet_aprom_writeb(dev, addr, val);
            pcnet_aprom_writeb(dev, addr + 1, val >> 8);
        } else if (BCR_DWIO(dev) && ((addr & 3) == 0) && len == 4) {
            pcnet_aprom_writeb(dev, addr, val);
            pcnet_aprom_writeb(dev, addr + 1, val >> 8);
            pcnet_aprom_writeb(dev, addr + 2, val >> 16);
            pcnet_aprom_writeb(dev, addr + 3, val >> 24);
        }
    } else {
        if (len == 2)
            pcnet_word_write(dev, addr, val);
        else if (len == 4)
            pcnet_dword_write(dev, addr, val);
    }
}

static void
pcnet_writeb(uint16_t addr, uint8_t val, void *priv)
{
    pcnet_write((nic_t *) priv, addr, val, 1);
}

static void
pcnet_writew(uint16_t addr, uint16_t val, void *priv)
{
    pcnet_write((nic_t *) priv, addr, val, 2);
}

static void
pcnet_writel(uint16_t addr, uint32_t val, void *priv)
{
    pcnet_write((nic_t *) priv, addr, val, 4);
}

static uint32_t
pcnet_read(nic_t *dev, uint32_t addr, int len)
{
    uint32_t retval = 0xffffffff;
    uint16_t off    = addr & 0x1f;

    pcnet_log(3, "%s: read addr %x, off %x, len %d\n", dev->name, addr, off, len);

    if (off < 0x10) {
        if (!BCR_DWIO(dev) && len == 1)
            retval = pcnet_aprom_readb(dev, addr);
        else if (!BCR_DWIO(dev) && ((addr & 1) == 0) && len == 2)
            retval = pcnet_aprom_readb(dev, addr) | (pcnet_aprom_readb(dev, addr + 1) << 8);
        else if (BCR_DWIO(dev) && ((addr & 3) == 0) && len == 4) {
            retval = pcnet_aprom_readb(dev, addr) | (pcnet_aprom_readb(dev, addr + 1) << 8) | (pcnet_aprom_readb(dev, addr + 2) << 16) | (pcnet_aprom_readb(dev, addr + 3) << 24);
        }
    } else {
        if (len == 1)
            retval = pcnet_byte_read(dev, addr);
        else if (len == 2)
            retval = pcnet_word_read(dev, addr);
        else if (len == 4)
            retval = pcnet_dword_read(dev, addr);
    }

    pcnet_log(3, "%s: value in read - %08x\n", dev->name, retval);
    return retval;
}

static uint8_t
pcnet_readb(uint16_t addr, void *priv)
{
    return (pcnet_read((nic_t *) priv, addr, 1));
}

static uint16_t
pcnet_readw(uint16_t addr, void *priv)
{
    return (pcnet_read((nic_t *) priv, addr, 2));
}

static uint32_t
pcnet_readl(uint16_t addr, void *priv)
{
    return (pcnet_read((nic_t *) priv, addr, 4));
}

static void
pcnet_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    if (!(addr & 0x10)) {
        pcnet_aprom_writeb((nic_t *) priv, addr, val);
        return;
    }
    pcnet_write((nic_t *) priv, addr, val, 1);
}

static void
pcnet_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    if (!(addr & 0x10)) {
        pcnet_aprom_writeb((nic_t *) priv, addr, val);
        pcnet_aprom_writeb((nic_t *) priv, addr + 1, val >> 8);
        return;
    }
    pcnet_write((nic_t *) priv, addr, val, 2);
}

static void
pcnet_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    if (!(addr & 0x10)) {
        pcnet_aprom_writeb((nic_t *) priv, addr, val);
        pcnet_aprom_writeb((nic_t *) priv, addr + 1, val >> 8);
        pcnet_aprom_writeb((nic_t *) priv, addr + 2, val >> 16);
        pcnet_aprom_writeb((nic_t *) priv, addr + 3, val >> 24);
        return;
    }
    pcnet_write((nic_t *) priv, addr, val, 4);
}

static uint8_t
pcnet_mmio_readb(uint32_t addr, void *priv)
{
    if (!(addr & 0x10))
        return pcnet_aprom_readb((nic_t *) priv, addr);
    return (pcnet_read((nic_t *) priv, addr, 1));
}

static uint16_t
pcnet_mmio_readw(uint32_t addr, void *priv)
{
    if (!(addr & 0x10))
        return pcnet_aprom_readb((nic_t *) priv, addr) | (pcnet_aprom_readb((nic_t *) priv, addr + 1) << 8);
    return (pcnet_read((nic_t *) priv, addr, 2));
}

static uint32_t
pcnet_mmio_readl(uint32_t addr, void *priv)
{
    if (!(addr & 0x10))
        return pcnet_aprom_readb((nic_t *) priv, addr) | (pcnet_aprom_readb((nic_t *) priv, addr + 1) << 8) | (pcnet_aprom_readb((nic_t *) priv, addr + 2) << 16) | (pcnet_aprom_readb((nic_t *) priv, addr + 3) << 24);
    return (pcnet_read((nic_t *) priv, addr, 4));
}

static void
pcnet_mem_init(nic_t *dev, uint32_t addr)
{
    mem_mapping_add(&dev->mmio_mapping, addr, 32,
                    pcnet_mmio_readb, pcnet_mmio_readw, pcnet_mmio_readl,
                    pcnet_mmio_writeb, pcnet_mmio_writew, pcnet_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
}

static void
pcnet_mem_set_addr(nic_t *dev, uint32_t base)
{
    mem_mapping_set_addr(&dev->mmio_mapping, base, 32);
}

static void
pcnet_mem_disable(nic_t *dev)
{
    mem_mapping_disable(&dev->mmio_mapping);
}

static void
pcnet_ioremove(nic_t *dev, uint16_t addr, int len)
{
    if (dev->is_pci || dev->is_vlb) {
        io_removehandler(addr, len,
                         pcnet_readb, pcnet_readw, pcnet_readl,
                         pcnet_writeb, pcnet_writew, pcnet_writel, dev);
    } else {
        io_removehandler(addr, len,
                         pcnet_readb, pcnet_readw, NULL,
                         pcnet_writeb, pcnet_writew, NULL, dev);
    }
}

static void
pcnet_ioset(nic_t *dev, uint16_t addr, int len)
{
    pcnet_ioremove(dev, addr, len);

    if (dev->is_pci || dev->is_vlb) {
        io_sethandler(addr, len,
                      pcnet_readb, pcnet_readw, pcnet_readl,
                      pcnet_writeb, pcnet_writew, pcnet_writel, dev);
    } else {
        io_sethandler(addr, len,
                      pcnet_readb, pcnet_readw, NULL,
                      pcnet_writeb, pcnet_writew, NULL, dev);
    }
}

/* The Am79C965A's Software Relocatable Mode (data sheet, "Software Relocation of I/O
   Resources", Table 34). It snoops byte writes to 378h: "AMD" and 01h, then IOBASEL, IOBASEU,
   BCR2 and BCR21, low byte first. A wrong byte in the first four starts the match again. */
static const uint8_t pcnet_srm_key[4]  = { 0x41, 0x4d, 0x44, 0x01 };
static const uint8_t pcnet_srm_regs[4] = { BCR_IOBASEL, BCR_IOBASEU, BCR_MC, BCR_INTCON };

static void
pcnet_srm_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    nic_t *dev = (nic_t *) priv;
    int    reg;

    if (!dev->srm)
        return;

    if (dev->srm_pos < 4) {
        if (val == pcnet_srm_key[dev->srm_pos])
            dev->srm_pos++;
        else
            dev->srm_pos = (val == pcnet_srm_key[0]) ? 1 : 0;
        return;
    }

    reg = pcnet_srm_regs[(dev->srm_pos - 4) >> 1];
    if (dev->srm_pos & 1)
        dev->aBCR[reg] = (dev->aBCR[reg] & 0x00ff) | (val << 8);
    else
        dev->aBCR[reg] = (dev->aBCR[reg] & 0xff00) | val;

    if (++dev->srm_pos == 12) {
        dev->aBCR[BCR_IOBASEL] &= 0xffe0;
        dev->aBCR[BCR_MC] &= 0x418f;
        dev->aBCR[BCR_INTCON] &= 0x0183;
        dev->srm     = 0;
        dev->srm_pos = 0;
        pcnet_log(1, "%s: left Software Relocatable Mode, I/O at %04X%04X\n", dev->name,
                  dev->aBCR[BCR_IOBASEU], dev->aBCR[BCR_IOBASEL]);
        pcnet_vlb_io_update(dev);
        pcnetUpdateIrq(dev);
    }
}

/* Where the Am79C965A decodes its 32 bytes of I/O: at BCR17:BCR16, compared on all 32
   address bits (only the low 24 with BCR21 IOAW24) -- a 486's I/O cycles drive A31-A16 low,
   so an upper half the bus never carries leaves it unreachable -- and nowhere in Software
   Relocatable Mode, which snoops 378h instead. */
static void
pcnet_vlb_io_update(nic_t *dev)
{
    const uint16_t upper = dev->aBCR[BCR_IOBASEU] & ((dev->aBCR[BCR_INTCON] & 0x0100) ? 0x00ff : 0xffff);
    const int      base  = (dev->srm || upper) ? -1 : (dev->aBCR[BCR_IOBASEL] & 0xffe0);

    if (base != dev->vlb_io_base) {
        if (dev->vlb_io_base >= 0)
            pcnet_ioremove(dev, dev->vlb_io_base, 0x20);
        if (base >= 0)
            pcnet_ioset(dev, base, 0x20);
        dev->vlb_io_base = base;
    }

    io_removehandler(0x0378, 1, NULL, NULL, NULL, pcnet_srm_write, NULL, NULL, dev);
    if (dev->srm)
        io_sethandler(0x0378, 1, NULL, NULL, NULL, pcnet_srm_write, NULL, NULL, dev);
}

/* Am79C965A BCR18. Bit 8 reads one. In VESA VL-Bus mode BREADE and BWRITE are forced to
   zero while VLBEN is low, which a VL-Bus card wires from the slot's ID lines so that it is low
   above 33 MHz, where linear bursts are not allowed. */
static void
pcnet_vlb_bsbc_fix(nic_t *dev)
{
    dev->aBCR[BCR_BSBC] |= 0x0100;
    if (cpu_busspeed > 33333334.0)
        dev->aBCR[BCR_BSBC] &= ~0x0060;
}

/* PCI power management and wake-up (Am79C972/973/975 data sheets, "Power
   Savings Mode"). A wake-up event sets PME_STATUS; PME is asserted while PME_EN (or,
   for Magic Packet and link change, CSR116 PME_EN_OVR) is set too. PME reaches the
   board's ACPI GPE wake path as the general-purpose input (VIA PME#, PIIX4 GPI). */
static void
pcnet_pm_wake(nic_t *dev, int override)
{
    if (!pcnet_has_pm(dev))
        return;

    dev->pmcsr |= 0x8000; /* PME_STATUS */
    if ((dev->pmcsr & 0x0100) || (override && (dev->aCSR[116] & 0x0400))) {
        pcnet_log(1, "%s: PME\n", dev->name);
#ifdef XEON3D_ACPI_PR_WAS_MERGED
        acpi_gpe_event(ACPI_GPE_GPI);
#endif
    }
}

/* Magic Packet mode: MPMODE (or MPPEN/MPPEN_D3C) with MPEN (CSR5 bit 2) set, or PG
   deasserted. PG has no line of its own here: the function's D3 state stands for it
   (the system takes PCI power away only after putting the card in D3). */
static int
pcnet_pm_magic_mode(const nic_t *dev)
{
    const pcnet_chip_t *c = pcnet_chip(dev);
    int                 mode;
    int                 pg_low;

    if (!c || !c->magic)
        return 0;

    mode   = !!(dev->aCSR[5] & 0x0002);
    pg_low = c->pm && ((dev->pmcsr & 0x0003) == 0x0003);
    if (c->pm && (dev->aCSR[116] & 0x1010))
        mode = 1;
    return mode && ((dev->aCSR[5] & 0x0004) || pg_low);
}

static int
pcnet_pm_is_magic(const nic_t *dev, const uint8_t *buf, int size)
{
    const uint8_t *padr = (const uint8_t *) &dev->aCSR[12];
    int            run  = 0;

    /* The destination must be the physical address unless MPPLBA/EMPPLBA. */
    if (!(dev->aCSR[5] & 0x0020) && !(pcnet_has_pm(dev) && (dev->aCSR[116] & 0x0040)) &&
        memcmp(buf, padr, 6))
        return 0;

    /* Sixteen consecutive copies of PADR anywhere after the length/type field. */
    for (int i = 14; (i + 6) <= size;) {
        if (!memcmp(&buf[i], padr, 6)) {
            if (++run == 16)
                return 1;
            i += 6;
        } else {
            i -= run * 6 - 1;
            run = 0;
        }
    }
    return 0;
}

/* OnNow pattern match: PMR word 0 byte 0 enables patterns 0-7, bytes 1-4 of words 0
   and 1 point to their first words. Each pattern word skips SKIP dwords of the frame,
   compares the bytes MASK selects and moves on a dword; EOP ends the pattern. */
static int
pcnet_pm_pattern(const nic_t *dev, const uint8_t *buf, int size)
{
    for (int p = 0; p < 8; p++) {
        int w;
        int off = 0;
        int ok  = 1;

        if (!(dev->pmr[0][0] & (1 << p)))
            continue;
        w = dev->pmr[p >> 2][1 + (p & 3)];
        if ((w < 2) || (w > 63))
            continue;
        for (; w < 64; w++) {
            const uint8_t ctl = dev->pmr[w][0];

            off += ((ctl >> 4) & 7) * 4;
            for (int b = 0; b < 4; b++)
                if ((ctl & (1 << b)) && (((off + b) >= size) || (buf[off + b] != dev->pmr[w][1 + b])))
                    ok = 0;
            off += 4;
            if (!ok || (ctl & 0x80))
                break;
        }
        if (ok && (w < 64))
            return 1;
    }
    return 0;
}

/* Called for every received frame: 1 if it was taken (Magic Packet mode flushes all). */
static int
pcnet_pm_receive(nic_t *dev, const uint8_t *buf, int size)
{
    if (pcnet_has_pm(dev) && ((dev->aBCR[BCR_PMR1] & 0x0080) || (dev->aCSR[116] & 0x0800)) &&
        !(dev->aCSR[116] & 0x0080) && pcnet_pm_pattern(dev, buf, size)) {
        pcnet_log(1, "%s: OnNow pattern matched\n", dev->name);
        dev->aCSR[116] |= 0x0080; /* PMAT */
        pcnet_pm_wake(dev, 0);
    }

    if (!pcnet_pm_magic_mode(dev))
        return 0;

    if (pcnet_pm_is_magic(dev, buf, size)) {
        pcnet_log(1, "%s: Magic Packet\n", dev->name);
        dev->aCSR[5] |= 0x0010; /* MPINT */
        if (pcnet_has_pm(dev))
            dev->aCSR[116] |= 0x0020; /* MPMAT */
        pcnetUpdateIrq(dev);
        pcnet_pm_wake(dev, 1);
    }
    return 1;
}

static void
pcnet_pm_link_change(nic_t *dev)
{
    if (pcnet_has_pm(dev) && (dev->aCSR[116] & 0x2100)) {
        dev->aCSR[116] |= 0x0200; /* LCDET */
        pcnet_pm_wake(dev, 1);
    }
}

static void
pcnet_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    nic_t  *dev = (nic_t *) priv;
    uint8_t valxor;

    pcnet_log(4, "%s: Write value %02X to register %02X\n", dev->name, val, addr & 0xff);

    switch (addr) {
        case 0x04:
            valxor = (val & 0x57) ^ dev->pci_regs[addr];
            if (valxor & PCI_COMMAND_IO) {
                pcnet_ioremove(dev, dev->PCIBase, 32);
                if ((dev->PCIBase != 0) && (val & PCI_COMMAND_IO))
                    pcnet_ioset(dev, dev->PCIBase, 32);
            }
            if (valxor & PCI_COMMAND_MEM) {
                pcnet_mem_disable(dev);
                if ((dev->MMIOBase != 0) && (val & PCI_COMMAND_MEM))
                    pcnet_mem_set_addr(dev, dev->MMIOBase);
            }
            dev->pci_regs[addr] = val & 0x57;
            break;

        case 0x05:
            dev->pci_regs[addr] = val & 0x01;
            break;

        case 0x0D:
            dev->pci_regs[addr] = val;
            break;

        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            /* I/O Base set. */
            /* First, remove the old I/O. */
            pcnet_ioremove(dev, dev->PCIBase, 32);
            /* Then let's set the PCI regs. */
            dev->pci_bar[0].addr_regs[addr & 3] = val;
            /* Then let's calculate the new I/O base. */
            dev->pci_bar[0].addr &= 0xffe0;
            dev->PCIBase = dev->pci_bar[0].addr;
            /* Log the new base. */
            pcnet_log(4, "%s: New I/O base is %04X\n", dev->name, dev->PCIBase);
            /* We're done, so get out of the here. */
            if (dev->pci_regs[4] & PCI_COMMAND_IO) {
                if (dev->PCIBase != 0)
                    pcnet_ioset(dev, dev->PCIBase, 32);
            }
            return;

        case 0x15:
        case 0x16:
        case 0x17:
            /* MMIO Base set. */
            /* First, remove the old I/O. */
            pcnet_mem_disable(dev);
            /* Then let's set the PCI regs. */
            dev->pci_bar[1].addr_regs[addr & 3] = val;
            /* Then let's calculate the new I/O base. */
            dev->pci_bar[1].addr &= 0xffffc000;
            dev->MMIOBase = dev->pci_bar[1].addr & 0xffffc000;
            /* Log the new base. */
            pcnet_log(4, "%s: New MMIO base is %08X\n", dev->name, dev->MMIOBase);
            /* We're done, so get out of the here. */
            if (dev->pci_regs[4] & PCI_COMMAND_MEM) {
                if (dev->MMIOBase != 0)
                    pcnet_mem_set_addr(dev, dev->MMIOBase);
            }
            return;

        case 0x3C:
            dev->base_irq        = val;
            dev->pci_regs[addr] = val;
            return;

        /* PMCSR. PowerState and DATA_SEL are stored (the power state has no
           effect on the chip, per the data sheet); PME_STATUS is write-1-to-clear. */
        case 0x44:
            if (pcnet_has_pm(dev))
                dev->pmcsr = (dev->pmcsr & ~0x0003) | (val & 0x03);
            return;
        case 0x45:
            if (pcnet_has_pm(dev)) {
                dev->pmcsr = (dev->pmcsr & ~0x1f00) | ((val << 8) & 0x1f00);
                dev->pmcsr &= ~((val << 8) & 0x8000);
            }
            return;

        default:
            break;
    }
}

static uint8_t
pcnet_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    const nic_t *dev = (nic_t *) priv;

    pcnet_log(4, "%s: Read to register %02X\n", dev->name, addr & 0xff);

    switch (addr) {
        case 0x00:
            return 0x22;
        case 0x01:
            return 0x10;
        case 0x02:
            return 0x00;
        case 0x03:
            return 0x20;
        case 0x04:
            return dev->pci_regs[0x04] & 0x57; /*Respond to IO and memory accesses*/
        case 0x05:
            return dev->pci_regs[0x05] & 0x01;
        case 0x06:
            return pcnet_has_pm(dev) ? 0x90 : 0x80; /* Bit 4, capabilities list */
        case 0x07:
            return 2;
        case 0x08:
            return pcnet_chip(dev)->rev; /* Per chip, from the data sheets */
        case 0x09:
            return 0; /*Programming interface*/
        case 0x0A:
            return 0; /*devubclass*/
        case 0x0B:
            return 2; /*Class code*/
        case 0x0D:
            return dev->pci_regs[addr];
        case 0x0E:
            return 0; /*Header type */
        case 0x10:
            return dev->pci_bar[0].addr_regs[0] | 1; /*I/O space*/
        case 0x11:
            return dev->pci_bar[0].addr_regs[1];
        case 0x12:
            return dev->pci_bar[0].addr_regs[2];
        case 0x13:
            return dev->pci_bar[0].addr_regs[3];
        case 0x14:
            return 0; /*Memory space*/
        case 0x15:
            return dev->pci_bar[1].addr_regs[1];
        case 0x16:
            return dev->pci_bar[1].addr_regs[2];
        case 0x17:
            return dev->pci_bar[1].addr_regs[3];
        /* The Am79C970 and 974 have no subsystem IDs; the Am79C971/972/973/975 read
           BCR23/BCR24 (EEPROM-loaded; the 973's defaults are the 1022h:2000h it always
           reported). The Am79C970A keeps that constant. */
        case 0x2C:
        case 0x2D:
            if ((dev->board == DEV_AM79C970) || (dev->board == DEV_AM79C974))
                return 0x00;
            if (dev->board == DEV_AM79C970A)
                return (addr & 1) ? 0x10 : 0x22;
            return dev->aBCR[BCR_PCISVID] >> ((addr & 1) * 8);
        case 0x2E:
        case 0x2F:
            if ((dev->board == DEV_AM79C970) || (dev->board == DEV_AM79C974))
                return 0x00;
            if (dev->board == DEV_AM79C970A)
                return (addr & 1) ? 0x20 : 0x00;
            return dev->aBCR[BCR_PCISID] >> ((addr & 1) * 8);
        case 0x34:
            return pcnet_has_pm(dev) ? 0x40 : 0x00;
        case 0x3C:
            return dev->base_irq;
        case 0x3D:
            /* The Am79C974's Ethernet is on INTB (data sheet amendment: 02h). */
            return (dev->board == DEV_AM79C974) ? PCI_INTB : PCI_INTA;
        /* MIN_GNT/MAX_LAT are BCR22 (PCI Latency, FF06h) on the FAST family; the
           Am79C974 reads 0. */
        case 0x3E:
            if (dev->board == DEV_AM79C974)
                return 0x00;
            return pcnet_is_fast(dev) ? (dev->aBCR[BCR_PLAT] & 0xff) : 0x06;
        case 0x3F:
            if (dev->board == DEV_AM79C974)
                return 0x00;
            return pcnet_is_fast(dev) ? (dev->aBCR[BCR_PLAT] >> 8) : 0xff;

        /* The PCI power management capability. */
        case 0x40:
            return pcnet_has_pm(dev) ? 0x01 : 0x00;
        case 0x41:
            return 0x00;
        case 0x42:
        case 0x43:
            return pcnet_has_pm(dev) ? (dev->aBCR[BCR_PMC_A] >> ((addr & 1) * 8)) : 0x00;
        case 0x44:
            return pcnet_has_pm(dev) ? (dev->pmcsr & 0xff) : 0x00;
        case 0x45:
            if (!pcnet_has_pm(dev))
                return 0x00;
            /* DATA_SCALE from BCR37-44 bits 9:8 as DATA_SEL picks them. */
            if (((dev->pmcsr >> 9) & 0x0f) < 8)
                return ((dev->pmcsr >> 8) & 0x9f) |
                       (((dev->aBCR[BCR_DATA0 + ((dev->pmcsr >> 9) & 0x0f)] >> 8) & 3) << 5);
            return (dev->pmcsr >> 8) & 0x9f;
        case 0x47:
            if (pcnet_has_pm(dev) && (((dev->pmcsr >> 9) & 0x0f) < 8))
                return dev->aBCR[BCR_DATA0 + ((dev->pmcsr >> 9) & 0x0f)] & 0xff;
            return 0x00;

        default:
            break;
    }

    return 0;
}

static void
pcnet_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    if (ld)
        return;

    nic_t *dev = (nic_t *) priv;

    /* Remove the old I/O range before forgetting it (it was cleared first, so a
       reconfigured card kept answering at its old address too). */
    if (dev->base_address && (dev->base_address != ISAPNP_IO_DISABLED))
        pcnet_ioremove(dev, dev->base_address, 0x20);

    dev->base_address = 0;
    dev->base_irq     = 0;
    dev->dma_channel  = -1;

    if (config->activate) {
        dev->base_address = config->io[0].base;
        if (dev->base_address != ISAPNP_IO_DISABLED)
            pcnet_ioset(dev, dev->base_address, 0x20);

        dev->base_irq    = config->irq[0].irq;
        dev->dma_channel = config->dma[0].dma;
        if (dev->dma_channel == ISAPNP_DMA_DISABLED)
            dev->dma_channel = -1;

        /* Update PnP register mirrors in ROM. */
        dev->aPROM[32] = dev->base_address >> 8;
        dev->aPROM[33] = dev->base_address;
        dev->aPROM[34] = dev->base_irq;
        dev->aPROM[35] = (config->irq[0].level << 1) | config->irq[0].type;
        dev->aPROM[36] = (dev->dma_channel == -1) ? ISAPNP_DMA_DISABLED : dev->dma_channel;
    }
}

static uint8_t
pcnet_pnp_read_vendor_reg(uint8_t ld, uint8_t reg, void *priv)
{
    const nic_t *dev = (nic_t *) priv;

    if (!ld && (reg == 0xf0))
        return dev->aPROM[50];
    else
        return 0x00;
}

static void
pcnet_pnp_write_vendor_reg(uint8_t ld, uint8_t reg, uint8_t val, void *priv)
{
    if (ld)
        return;

    nic_t *dev = (nic_t *) priv;

    if (reg == 0xf0)
        dev->aPROM[50] = val & 0x1f;
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
 * @param  pThis        The PCnet shared instance data.
 */
static void
pcnetTempLinkDown(nic_t *dev)
{
    if (dev->fLinkUp) {
        dev->fLinkTempDown     = 1;
        dev->cLinkDownReported = 0;
        dev->aCSR[0] |= 0x8000 | 0x2000; /* ERR | CERR (this is probably wrong) */
        timer_set_delay_u64(&dev->timer_restore, (dev->cMsLinkUpDelay * 1000) * TIMER_USEC);
    }
}

/**
 * Check if the device/driver can receive data now.
 *
 * Worker for pcnetNetworkDown_WaitReceiveAvail().  This must be called before
 * the pfnRecieve() method is called.
 *
 * @returns VBox status code.
 * @param   pThis           The PCnet instance data.
 */
static int
pcnetCanReceive(nic_t *dev)
{
    int rc = 0;

    if (!CSR_DRX(dev) && !CSR_STOP(dev) && !CSR_SPND(dev)) {
        if (HOST_IS_OWNER(CSR_CRST(dev)) && dev->GCRDRA)
            pcnetRdtePoll(dev);

        if (HOST_IS_OWNER(CSR_CRST(dev))) {
            /** @todo Notify the guest _now_. Will potentially increase the interrupt load */
            if (dev->fSignalRxMiss)
                dev->aCSR[0] |= 0x1000; /* Set MISS flag */
        } else
            rc = 1;
    }

    return rc;
}

static int
pcnetSetLinkState(void *priv, uint32_t link_state)
{
    nic_t *dev = (nic_t *) priv;

    if (link_state & NET_LINK_TEMP_DOWN) {
        pcnetTempLinkDown(dev);
        return 1;
    }

    bool link_up = !(link_state & NET_LINK_DOWN);
    if (dev->fLinkUp != link_up) {
        dev->fLinkUp = link_up;
        pcnet_pm_link_change(dev);
        if (link_up) {
            dev->fLinkTempDown     = 1;
            dev->cLinkDownReported = 0;
            dev->aCSR[0] |= 0x8000 | 0x2000;
            timer_set_delay_u64(&dev->timer_restore, (dev->cMsLinkUpDelay * 1000) * TIMER_USEC);
        } else {
            dev->cLinkDownReported = 0;
            dev->aCSR[0] |= 0x8000 | 0x2000;
        }
    }

    return 0;
}

static void
pcnetTimerSoftInt(void *priv)
{
    nic_t *dev = (nic_t *) priv;

    dev->aCSR[7] |= 0x0800; /* STINT */
    pcnetUpdateIrq(dev);
    timer_advance_u64(&dev->timer_soft_int, (12.8 * (dev->aBCR[BCR_STVAL] & 0xffff)) * TIMER_USEC);
}

static void
pcnetTimerRestore(void *priv)
{
    nic_t *dev = (nic_t *) priv;

    if (dev->cLinkDownReported <= PCNET_MAX_LINKDOWN_REPORTED) {
        timer_advance_u64(&dev->timer_restore, 1500000 * TIMER_USEC);
    } else {
        dev->fLinkTempDown = 0;
        if (dev->fLinkUp) {
            dev->aCSR[0] &= ~(0x8000 | 0x2000); /* ERR | CERR - probably not 100% correct either... */
        }
    }
}

static void *
pcnet_init(const device_t *info)
{
    uint32_t mac;
    nic_t   *dev;
    int      c;
    uint16_t checksum;

    dev = calloc(1, sizeof(nic_t));
    dev->name  = info->name;
    dev->board = info->local & 0xff;

    dev->is_pci = !!(info->flags & DEVICE_PCI);
    dev->is_vlb = !!(info->flags & DEVICE_VLB);
    dev->is_isa = !!(info->flags & (DEVICE_ISA16));

    if (dev->is_pci || dev->is_vlb)
        dev->transfer_size = 4;
    else
        dev->transfer_size = 2;

    if (dev->is_pci) {
        pcnet_mem_init(dev, 0x0fffff00);
        pcnet_mem_disable(dev);
    }

    dev->fLinkUp        = 1;
    dev->cMsLinkUpDelay = 5000;

    if (dev->board == DEV_AM79C960_EB) {
        dev->maclocal[0] = 0x02; /* 02:07:01 (Racal OID) */
        dev->maclocal[1] = 0x07;
        dev->maclocal[2] = 0x01;
    } else {
        dev->maclocal[0] = 0x00; /* 00:0C:87 (AMD OID) */
        dev->maclocal[1] = 0x0C;
        dev->maclocal[2] = 0x87;
    }

    /* See if we have a local MAC address configured. */
    mac = device_get_config_mac("mac", -1);

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

    memcpy(dev->aPROM, dev->maclocal, sizeof(dev->maclocal));

    /* Reserved Location: must be 00h */
    dev->aPROM[6] = dev->aPROM[7] = 0x00;

    /* Reserved Location: must be 00h */
    dev->aPROM[8] = 0x00;

    /* Hardware ID: must be 11h if compatibility to AMD drivers is desired */
    /* 0x00/0xFF=ISA, 0x01=PnP, 0x10=VLB, 0x11=PCI */
    if (dev->is_pci)
        dev->aPROM[9] = 0x11;
    else if (dev->is_vlb)
        dev->aPROM[9] = 0x10;
    else if ((dev->board == DEV_AM79C961) || (dev->board == DEV_AM79C961A))
        dev->aPROM[9] = 0x01;
    else
        dev->aPROM[9] = 0x00;

    /* User programmable space, init with 0 */
    dev->aPROM[10] = dev->aPROM[11] = 0x00;

    if (dev->board == DEV_AM79C960_EB) {
        dev->aPROM[14] = 0x52;
        dev->aPROM[15] = 0x44; /* NI6510 EtherBlaster 'RD' signature. */
    } else {
        /* Must be ASCII W (57h) if compatibility to AMD
         driver software is desired */
        dev->aPROM[14] = dev->aPROM[15] = 0x57;
    }

    for (c = 0, checksum = 0; c < 16; c++)
        checksum += dev->aPROM[c];

    *(uint16_t *) &dev->aPROM[12] = cpu_to_le16(checksum);

    /*
     * Make this device known to the I/O system.
     * PCI devices start with address spaces inactive.
     */
    if (dev->is_pci) {
        /*
         * Configure the PCI space registers.
         *
         * We do this here, so the I/O routines are generic.
         */

        /* Enable our address space in PCI. */
        dev->pci_bar[0].addr_regs[0] = 1;
        dev->pci_bar[1].addr_regs[0] = 0;
        dev->pci_regs[0x04]          = 3;

        /* Add device to the PCI bus, keep its slot number. */
        if (info->local & 0x0100)
            pci_add_card(PCI_ADD_NETWORK, pcnet_pci_read, pcnet_pci_write, dev, &dev->pci_slot);
        else
            pci_add_card(PCI_ADD_NORMAL, pcnet_pci_read, pcnet_pci_write, dev, &dev->pci_slot);

        /* The Am79C974 answers a second IDSEL as the Am53C974. */
        if (dev->board == DEV_AM79C974)
            am79c974_scsi_add(device_get_config_int("bios"));
    } else if ((dev->board == DEV_AM79C961) || (dev->board == DEV_AM79C961A)) {
        dev->dma_channel = -1;

        /* Weird secondary checksum. The datasheet isn't clear on what
           role it might play with the PnP register mirrors before it. */
        for (c = 0, checksum = 0; c < 54; c++)
            checksum += dev->aPROM[c];

        dev->aPROM[51] = checksum;

        if (dev->board == DEV_AM79C961A) {
            /* The duplex setting its EEPROM holds for ISACSR9. */
            dev->isa2_dup = device_get_config_int("duplex");
            memcpy(&dev->aPROM[0x40], am79c961a_pnp_rom, sizeof(am79c961a_pnp_rom));
            /* Serial number from the MAC address, so two cards isolate apart. */
            dev->aPROM[0x44] = dev->maclocal[5];
            dev->aPROM[0x45] = dev->maclocal[4];
            dev->aPROM[0x46] = dev->maclocal[3];
            isapnp_add_card(&dev->aPROM[0x40], sizeof(am79c961a_pnp_rom), pcnet_pnp_config_changed, NULL, pcnet_pnp_read_vendor_reg, pcnet_pnp_write_vendor_reg, dev);
        } else {
            memcpy(&dev->aPROM[0x40], am79c961_pnp_rom, sizeof(am79c961_pnp_rom));
            isapnp_add_card(&dev->aPROM[0x40], sizeof(am79c961_pnp_rom), pcnet_pnp_config_changed, NULL, pcnet_pnp_read_vendor_reg, pcnet_pnp_write_vendor_reg, dev);
        }
    } else {
        dev->base_address = device_get_config_hex16("base");
        dev->base_irq     = device_get_config_int("irq");
        if (dev->is_vlb)
            dev->dma_channel = -1;
        else
            dev->dma_channel = device_get_config_int("dma");
        pcnet_ioset(dev, dev->base_address, 0x20);
        dev->vlb_io_base = dev->base_address;
    }

    /* The PCnet-32, PCnet-PCI II and PCnet-FAST family load their Address PROM and some
       registers from a 93C46 serial EEPROM, kept in nmc93cxx_eeprom_<card>_<n>.nvr. Its default
       contents are the configured MAC and the H_RESET register values. */
    if (dev->board == DEV_AM79C960_VLB)
        dev->eeprom_map = &pcnet_ee_map_965;
    else if (dev->board == DEV_AM79C970A)
        dev->eeprom_map = &pcnet_ee_map_970a;
    else if (dev->board == DEV_AM79C971)
        dev->eeprom_map = &pcnet_ee_map_971;
    else if (dev->board == DEV_AM79C972)
        dev->eeprom_map = &pcnet_ee_map_972;
    else if ((dev->board == DEV_AM79C973) || (dev->board == DEV_AM79C975))
        dev->eeprom_map = &pcnet_ee_map_973;
    if (dev->eeprom_map) {
        const pcnet_eeprom_map_t *map = dev->eeprom_map;
        nmc93cxx_eeprom_params_t  params;
        char                      filename[1024] = { 0 };
        uint16_t                 *words;
        int                       changed = 0;

        if (dev->is_vlb) {
            /* Am79C965A: byte 8 is the IRQ for AMD's drivers. */
            dev->aPROM[8] = dev->base_irq;
            for (c = 0, checksum = 0; c < 16; c++)
                if ((c != 12) && (c != 13))
                    checksum += dev->aPROM[c];
            *(uint16_t *) &dev->aPROM[12] = cpu_to_le16(checksum);
        }

        pcnetBcrDefaults(dev);
        for (c = 0; c < 8; c++)
            dev->eeprom_default[c] = dev->aPROM[c << 1] | (dev->aPROM[(c << 1) + 1] << 8);
        for (c = 0; c < map->nregs; c++) {
            int16_t r = map->regs[c];

            if (r == PCNET_EE_SKIP)
                dev->eeprom_default[map->first + c] = 0;
            else if (r & PCNET_EE_CSR)
                dev->eeprom_default[map->first + c] = dev->aCSR[r & 0xff];
            else
                dev->eeprom_default[map->first + c] = dev->aBCR[r];
        }
        if (dev->is_vlb)
            dev->eeprom_default[8] = dev->base_address; /* BCR16, as programmed */
        pcnet_eeprom_checksums(map, dev->eeprom_default);

        params.type            = NMC_93C46_x16_64;
        params.default_content = dev->eeprom_default;
        params.filename        = filename;
        snprintf(filename, sizeof(filename), "nmc93cxx_eeprom_%s_%d.nvr", info->internal_name, device_get_instance());
        dev->eeprom = device_add_inst_params(&nmc93cxx_device, device_get_instance(), &params);

        /* The MAC address (and the PCnet-32's IRQ byte) in Settings win over the stored ones. */
        words = (uint16_t *) nmc93cxx_eeprom_data(dev->eeprom);
        for (c = 0; c < 16; c++) {
            if ((c < 6) || (dev->is_vlb && (c == 8))) {
                if (pcnet_eeprom_byte(words, c) != dev->aPROM[c]) {
                    words[c >> 1] = (words[c >> 1] & ((c & 1) ? 0x00ff : 0xff00)) | (dev->aPROM[c] << ((c & 1) << 3));
                    changed = 1;
                }
            }
        }
        if (changed)
            pcnet_eeprom_checksums(map, words);
    }

    pcnet_log(2, "%s: I/O=%04x, IRQ=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
              dev->name, dev->base_address, dev->base_irq,
              dev->aPROM[0], dev->aPROM[1], dev->aPROM[2],
              dev->aPROM[3], dev->aPROM[4], dev->aPROM[5]);

    pcnet_log(1, "%s: %s attached IO=0x%X IRQ=%d\n", dev->name,
              dev->is_pci ? "PCI" : "VLB/ISA", dev->base_address, dev->base_irq);

    /* Reset the board. */
    pcnetHardReset(dev);

    /* Attach ourselves to the network module. */
    dev->netcard              = network_attach(dev, dev->aPROM, pcnetReceiveNoSync, pcnetSetLinkState);
    dev->netcard->byte_period = pcnet_is_fast(dev) ? NET_PERIOD_100M : NET_PERIOD_10M;

    timer_add(&dev->timer, pcnetPollTimer, dev, 0);

    if (pcnet_is_fast(dev))
        timer_add(&dev->timer_soft_int, pcnetTimerSoftInt, dev, 0);

    timer_add(&dev->timer_restore, pcnetTimerRestore, dev, 0);

    return dev;
}

static void
pcnet_close(void *priv)
{
    nic_t *dev = (nic_t *) priv;

    pcnet_log(1, "%s: closed\n", dev->name);

    if (dev->is_vlb && dev->srm) /* Stop snooping 378h */
        io_removehandler(0x0378, 1, NULL, NULL, NULL, pcnet_srm_write, NULL, NULL, dev);

    netcard_close(dev->netcard);

    if (dev) {
        free(dev);
        dev = NULL;
    }
}

// clang-format off
static const device_config_t pcnet_pci_config[] = {
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

/* The PCnet-ISA II's EEPROM word 10h, ISACSR9 at RESET (FDEN bit 0, AUIFD bit 1). */
static const device_config_t pcnet_isa2_config[] = {
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
    {
        .name           = "duplex",
        .description    = "Duplex (EEPROM)",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Half duplex",                    .value = 0 },
            { .description = "Full duplex (10BASE-T)",         .value = 1 },
            { .description = "Full duplex (10BASE-T and AUI)", .value = 3 },
            { .description = ""                                           }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t pcnet_isa_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x300,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x300", .value = 0x300 },
            { .description = "0x320", .value = 0x320 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x360", .value = 0x360 },
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
            { .description = "IRQ 3",  .value =  3 },
            { .description = "IRQ 4",  .value =  4 },
            { .description = "IRQ 5",  .value =  5 },
            { .description = "IRQ 7",  .value =  7 },
            { .description = "IRQ 9",  .value =  9 },
            { .description = "IRQ 10", .value = 10 },
            { .description = "IRQ 11", .value = 11 },
            { .description = "IRQ 12", .value = 12 },
            { .description = "IRQ 15", .value = 15 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "dma",
        .description    = "DMA",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 5,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "DMA 0", .value = 0 },
            { .description = "DMA 3", .value = 3 },
            { .description = "DMA 5", .value = 5 },
            { .description = "DMA 6", .value = 6 },
            { .description = "DMA 7", .value = 7 },
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

static const device_config_t pcnet_vlb_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x300,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x300", .value = 0x300 },
            { .description = "0x320", .value = 0x320 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x360", .value = 0x360 },
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
        .selection = {
            { .description = "IRQ 3",  .value =  3 },
            { .description = "IRQ 4",  .value =  4 },
            { .description = "IRQ 5",  .value =  5 },
            { .description = "IRQ 7",  .value =  7 },
            { .description = "IRQ 9",  .value =  9 },
            { .description = "IRQ 10", .value = 10 },
            { .description = "IRQ 11", .value = 11 },
            { .description = "IRQ 12", .value = 12 },
            { .description = "IRQ 15", .value = 15 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = "",
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

/* The PCnet-SCSI's MAC and its SCSI side's BIOS. */
static const device_config_t pcnet_scsi_config[] = {
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
    {
        .name           = "bios",
        .description    = "Enable SCSI BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

/* Its SCSI controller takes a bus, planned with the network cards'. */
static uint32_t
pcnet_scsi_buses(UNUSED(const device_t *info))
{
    return 1;
}

const device_t pcnet_am79c960_device = {
    .name          = "AMD PCnet-ISA",
    .internal_name = "pcnetisa",
    .flags         = DEVICE_ISA16,
    .local         = DEV_AM79C960,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_isa_config
};

const device_t pcnet_am79c960_eb_device = {
    .name          = "Racal Interlan EtherBlaster",
    .internal_name = "pcnetracal",
    .flags         = DEVICE_ISA16,
    .local         = DEV_AM79C960_EB,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_isa_config
};

/*
   Used to be incorrectly called "AMD PCnet-VL" but the real name of the chip is "AMD PCnet-32" per the relevant datasheet.
   https://theretroweb.com/chip/documentation/am79c965-66c24a7e6969d347126123.pdf
*/
const device_t pcnet_am79c960_vlb_device = {
    .name          = "AMD PCnet-32",
    .internal_name = "pcnetvlb",
    .flags         = DEVICE_VLB,
    .local         = DEV_AM79C960_VLB,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_vlb_config
};

const device_t pcnet_am79c961_device = {
    .name          = "AMD PCnet-ISA+",
    .internal_name = "pcnetisaplus",
    .flags         = DEVICE_ISA16,
    .local         = DEV_AM79C961,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_pci_config
};

const device_t pcnet_am79c961a_device = {
    .name          = "AMD PCnet-ISA II",
    .internal_name = "pcnetisa2",
    .flags         = DEVICE_ISA16,
    .local         = DEV_AM79C961A,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_isa2_config
};

const device_t pcnet_am79c970a_device = {
    .name          = "AMD PCnet-PCI II",
    .internal_name = "pcnetpci",
    .flags         = DEVICE_PCI,
    .local         = DEV_AM79C970A,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_pci_config
};

const device_t pcnet_am79c970a_onboard_device = {
    .name          = "AMD PCnet-PCI II (On-Board)",
    .internal_name = "pcnetpci_onboard",
    .flags         = DEVICE_PCI,
    .local         = DEV_AM79C970A | 0x0100,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_pci_config
};

const device_t pcnet_am79c973_device = {
    .name          = "AMD PCnet-FAST III",
    .internal_name = "pcnetfast",
    .flags         = DEVICE_PCI,
    .local         = DEV_AM79C973,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_pci_config
};

const device_t pcnet_am79c973_onboard_device = {
    .name          = "AMD PCnet-FAST III (On-Board)",
    .internal_name = "pcnetfast_onboard",
    .flags         = DEVICE_PCI,
    .local         = DEV_AM79C973 | 0x0100,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_pci_config
};

/* The PCI chips the code base did not have. */
const device_t pcnet_am79c970_device = {
    .name          = "AMD PCnet-PCI",
    .internal_name = "pcnetpci_970",
    .flags         = DEVICE_PCI,
    .local         = DEV_AM79C970,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_pci_config
};

const device_t pcnet_am79c971_device = {
    .name          = "AMD PCnet-FAST",
    .internal_name = "pcnetfast_971",
    .flags         = DEVICE_PCI,
    .local         = DEV_AM79C971,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_pci_config
};

const device_t pcnet_am79c972_device = {
    .name          = "AMD PCnet-FAST+",
    .internal_name = "pcnetfastplus",
    .flags         = DEVICE_PCI,
    .local         = DEV_AM79C972,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_pci_config
};

const device_t pcnet_am79c975_device = {
    .name          = "AMD PCnet-FAST III (Am79C975)",
    .internal_name = "pcnetfast_975",
    .flags         = DEVICE_PCI,
    .local         = DEV_AM79C975,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_pci_config
};

/* The AMD PCnet-SCSI (Am79C974). Its Ethernet side here; the chip's
   SCSI side, a second single-function device on its own IDSEL, is the
   Am53C974 of scsi_pcscsi.c (am79c974_scsi_add()). */
const device_t pcnet_am79c974_device = {
    .name          = "AMD PCnet-SCSI (Am79C974)",
    .internal_name = "pcnetscsi",
    .flags         = DEVICE_PCI,
    .local         = DEV_AM79C974,
    .init          = pcnet_init,
    .close         = pcnet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcnet_scsi_config,
    .scsi_buses    = pcnet_scsi_buses
};
