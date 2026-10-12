/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Emulation of the DP83815/16 Network Interface Controller used in
 *          NETGEAR FA311/FA312 and etc.
 *
 * Authors: NexGenDriven,
 *
 *          Copyright 2026 NexGenDriven.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/dma.h>
#include <86box/pci.h>
#include <86box/random.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/plat_unused.h>
#include <86box/bswap.h>
#include <86box/nmc93cxx.h>

#define DP83815_VENDOR_ID      0x100B
#define DP83815_DEVICE_ID      0x0020
#define DP83815_IO_SIZE        0x100
#define DP83815_MMIO_SIZE      0x1000
#define DP83815_IO_BAR_MASK    0xFFFFFF00u
#define DP83815_MMIO_BAR_MASK  0xFFFFF000u
#define DP83815_MAX_FRAME      2048
#define DP83815_MAX_DESC_CHAIN 256

#ifdef ENABLE_DP83815_LOG
int dp83815_do_log = ENABLE_DP83815_LOG;

static void
dp83815_log(const char *fmt, ...)
{
    va_list ap;

    if (dp83815_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define dp83815_log(fmt, ...)
#endif

/* Operational register offsets */
enum {
    ChipCmd      = 0x00,
    ChipConfig   = 0x04,
    EECtrl       = 0x08,
    PCIBusCfg    = 0x0C,
    IntrStatus   = 0x10,
    IntrMask     = 0x14,
    IntrEnable   = 0x18,
    IntrHoldoff  = 0x1C,
    TxRingPtr    = 0x20,
    TxConfig     = 0x24,
    RxRingPtr    = 0x30,
    RxConfig     = 0x34,
    ClkRun       = 0x3C,
    WOLCmd       = 0x40,
    PauseCmd     = 0x44,
    RxFilterAddr = 0x48,
    RxFilterData = 0x4C,
    BootRomAddr  = 0x50,
    BootRomData  = 0x54,
    SiliconRev   = 0x58,
    StatsCtrl    = 0x5C,
    BMCR         = 0x80,
    BMSR         = 0x84,
    PHYIDR1      = 0x88,
    PHYIDR2      = 0x8C,
    ANAR         = 0x90,
    ANLPAR       = 0x94,
    ANER         = 0x98,
    ANNPTR       = 0x9C,
    PHYSTS       = 0xC0,
    MICR         = 0xC4,
    MISR         = 0xC8,
    FCSCR        = 0xD0,
    RECR         = 0xD4,
    PCSR         = 0xD8,
    PHYCR        = 0xE4,
    TBTSCR       = 0xE8,
};

#define PHY_REG(off) (((off) - 0x80) >> 2)

enum ChipCmd_bits {
    ChipReset = 0x100,
    SWIntr    = 0x80,
    RxReset   = 0x20,
    TxReset   = 0x10,
    RxOff     = 0x08,
    RxOn      = 0x04,
    TxOff     = 0x02,
    TxOn      = 0x01,
};

#define EEDI              0x00000001u
#define EEDO              0x00000002u
#define EECLK             0x00000004u
#define EECS              0x00000008u
#define PCI_EEPROM_RELOAD 0x00000004u

enum ChipConfig_bits {
    CfgPhyDis     = 0x200,
    CfgPhyRst     = 0x400,
    CfgExtPhy     = 0x1000,
    CfgAnegEnable = 0x2000,
    CfgAneg100    = 0x4000,
    CfgAnegFull   = 0x8000,
    CfgAnegDone   = 0x08000000,
    CfgFullDuplex = 0x20000000,
    CfgSpeed100   = 0x40000000,
    CfgLink       = 0x80000000,
};
#define CFG_RO_MASK    0xF8000000u /* LNKSTS, SPEED100, FDUP, POL, ANEG_DN */
#define CFG_ANEG_SEL   (CfgAnegEnable | CfgAneg100 | CfgAnegFull)

enum IntrStatus_bits {
    IntrRxDone      = 0x0001,
    IntrRxIntr      = 0x0002, /* RXDESC */
    IntrRxErr       = 0x0004,
    IntrRxEarly     = 0x0008,
    IntrRxIdle      = 0x0010,
    IntrRxOverrun   = 0x0020,
    IntrTxDone      = 0x0040,
    IntrTxIntr      = 0x0080, /* TXDESC */
    IntrTxErr       = 0x0100,
    IntrTxIdle      = 0x0200,
    IntrTxUnderrun  = 0x0400,
    IntrSWInt       = 0x1000,
    IntrWOLPkt      = 0x2000,
    IntrLinkChange  = 0x4000,
    IntrHighBitsErr = 0x8000,
    IntrRxResetDone = 0x01000000,
    IntrTxResetDone = 0x02000000,
};

#define TXCFG_MLB 0x20000000u /* MAC loopback */

#define MII_BMCR_RESET        0x8000u
#define MII_BMCR_LOOPBACK     0x4000u
#define MII_BMCR_SPEEDSEL     0x2000u
#define MII_BMCR_AUTONEGEN    0x1000u
#define MII_BMCR_POWERDOWN    0x0800u
#define MII_BMCR_ISOLATE      0x0400u
#define MII_BMCR_RESTARTAN    0x0200u
#define MII_BMCR_DUPLEX       0x0100u
#define MII_BMSR_100BASE_T4   0x8000u
#define MII_BMSR_100BASE_TXF  0x4000u
#define MII_BMSR_100BASE_TXH  0x2000u
#define MII_BMSR_10BASE_TF    0x1000u
#define MII_BMSR_10BASE_TH    0x0800u
#define MII_BMSR_MFPRESUP     0x0040u
#define MII_BMSR_ANEGCOMPLETE 0x0020u
#define MII_BMSR_REMOTEFAULT  0x0010u
#define MII_BMSR_ANEGCAPABLE  0x0008u
#define MII_BMSR_LINKSTATUS   0x0004u
#define MII_BMSR_EXTCAP       0x0001u
#define MII_BMSR_CAPS         (MII_BMSR_100BASE_TXF | MII_BMSR_100BASE_TXH | MII_BMSR_10BASE_TF | \
                               MII_BMSR_10BASE_TH | MII_BMSR_MFPRESUP | MII_BMSR_ANEGCAPABLE | \
                               MII_BMSR_EXTCAP)
#define MII_ANAR_100FD        0x0100u
#define MII_ANAR_100HD        0x0080u
#define MII_ANAR_10FD         0x0040u
#define MII_ANAR_10HD         0x0020u

#define PHYSTS_LINK           0x0001u
#define PHYSTS_SPEED10        0x0002u
#define PHYSTS_DUPLEX         0x0004u
#define PHYSTS_LOOPBACK       0x0008u
#define PHYSTS_ANEG_DONE      0x0010u

/* Descriptor fields */
#define DescOwn          0x80000000u
#define DescMore         0x40000000u
#define DescIntr         0x20000000u
#define DescNoCRC        0x10000000u
#define DescPktOK        0x08000000u
#define DescSizeMask     0x00000FFFu
#define DescRxDestShift  23
#define DescRxLoop       0x00020000u
#define DescStatusMask   0x07FF0000u

/* Receive filter control register */
#define RFCR_RFEN 0x80000000u
#define RFCR_AAB  0x40000000u
#define RFCR_AAM  0x20000000u
#define RFCR_AAU  0x10000000u
#define RFCR_APM  0x08000000u
#define RFCR_MHEN 0x00200000u
#define RFCR_UHEN 0x00100000u
#define RFCR_DEFAULT 0xC8000000u /* RFEN | AAB | APM, as loaded from the EEPROM */

struct dp83815_desc {
    uint32_t next_desc;
    uint32_t cmd_status;
    uint32_t addr;
    uint32_t software_use;
};

typedef struct dp83815_t {
    uint8_t       pci_conf[256];
    uint8_t       pci_slot;
    int           inst;
    uint8_t       irq_state;
    bar_t         pci_bar[2];
    uint32_t      io_base;
    bool          io_mapped;
    uint32_t      mmio_base;
    mem_mapping_t mmio_mapping;

    netcard_t *nic;
    uint8_t    mac[6];

    uint32_t chipconfig;
    uint32_t eectrl;
    uint32_t pci_bus_cfg;
    uint32_t intr_status;
    uint32_t intr_mask;
    uint32_t intr_enable;
    uint32_t intr_holdoff;
    uint32_t tx_ring_ptr;
    uint32_t tx_config;
    uint32_t rx_ring_ptr;
    uint32_t rx_config;
    uint32_t clk_run;
    uint32_t wol_cmd;
    uint32_t pause_cmd;
    uint32_t mib_ctrl;
    uint32_t rfcr;
    uint16_t rf_ram[0x200]; /* receive filter RAM, 16 bits per word, byte address >> 1 */
    uint32_t silicon_rev;
    uint16_t mii_regs[0x20];

    nmc93cxx_eeprom_t *eeprom;
    uint16_t           eeprom_words[64];

    bool tx_enabled;
    bool tx_active;
    bool tx_cur_done; /* "CTDD": current Tx descriptor is done, follow its link next */
    bool rx_enabled;
    bool rx_active;
    bool rx_cur_done; /* "CRDD": current Rx descriptor is done, follow its link next */

    bool net_link_up; /* what the 86Box network layer says */
    bool phy_link_up; /* what the emulated PHY reports */

    uint32_t tx_pkt_len;
    uint8_t  tx_pkt[DP83815_MAX_FRAME];
} dp83815_t;

static void dp83815_update_irq(dp83815_t *dev);
static void dp83815_tx_run(dp83815_t *dev);

static void
dp83815_update_irq(dp83815_t *dev)
{
    uint32_t intr_status = dev->intr_status;

    if (intr_status & 0x03FF0000) /* bits 16-25 set */
        intr_status |= IntrHighBitsErr;

    if ((intr_status & dev->intr_mask) && (dev->intr_enable & 1))
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    else
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
}

/* PHY */
static void
dp83815_update_phy_state(dp83815_t *dev, bool notify)
{
    uint16_t bmcr = dev->mii_regs[0];
    uint16_t bmsr = dev->mii_regs[1];
    bool     link = dev->net_link_up;
    bool     aneg = (bmcr & MII_BMCR_AUTONEGEN) != 0;
    bool     speed100;
    bool     full;
    uint16_t phy_sts = 0;
    uint32_t cfg;

    if (bmcr & MII_BMCR_POWERDOWN)
        link = false;

    if (aneg) {
        uint16_t common = dev->mii_regs[4] & dev->mii_regs[5];

        if (common & MII_ANAR_100FD) {
            speed100 = true;
            full     = true;
        } else if (common & MII_ANAR_100HD) {
            speed100 = true;
            full     = false;
        } else if (common & MII_ANAR_10FD) {
            speed100 = false;
            full     = true;
        } else {
            speed100 = false;
            full     = false;
        }
    } else {
        /* BMCR speed/duplex bits only matter when auto-negotiation is off. */
        speed100 = (bmcr & MII_BMCR_SPEEDSEL) != 0;
        full     = (bmcr & MII_BMCR_DUPLEX) != 0;
    }

    bmsr &= ~(MII_BMSR_ANEGCOMPLETE | MII_BMSR_LINKSTATUS);
    if (link) {
        bmsr |= MII_BMSR_LINKSTATUS;
        if (aneg)
            bmsr |= MII_BMSR_ANEGCOMPLETE;
    }
    dev->mii_regs[1] = bmsr;

    if (link)
        phy_sts |= PHYSTS_LINK;
    if (!speed100)
        phy_sts |= PHYSTS_SPEED10;
    if (full)
        phy_sts |= PHYSTS_DUPLEX;
    if (bmcr & MII_BMCR_LOOPBACK)
        phy_sts |= PHYSTS_LOOPBACK;
    if (link && aneg)
        phy_sts |= PHYSTS_ANEG_DONE;
    dev->mii_regs[PHY_REG(PHYSTS)] = phy_sts;

    cfg = dev->chipconfig & ~CFG_RO_MASK;
    if (link)
        cfg |= CfgLink;
    if (speed100)
        cfg |= CfgSpeed100;
    if (full)
        cfg |= CfgFullDuplex;
    if (link && aneg)
        cfg |= CfgAnegDone;
    dev->chipconfig = cfg;

    if (dev->phy_link_up != link) {
        dev->phy_link_up = link;
        dp83815_log("DP83815: link is now %s\n", link ? "UP" : "DOWN");
        if (notify)
            dev->intr_status |= IntrLinkChange;
    }

    if (dev->nic)
        dev->nic->byte_period = speed100 ? NET_PERIOD_100M : NET_PERIOD_10M;

    if (notify)
        dp83815_update_irq(dev);
}

static void
dp83815_phy_reset(dp83815_t *dev)
{
    memset(dev->mii_regs, 0, sizeof(dev->mii_regs));

    dev->mii_regs[0] = MII_BMCR_AUTONEGEN | MII_BMCR_SPEEDSEL | MII_BMCR_DUPLEX;
    dev->mii_regs[1] = MII_BMSR_CAPS;
    dev->mii_regs[2] = 0x2000;
    dev->mii_regs[3] = 0x5C21;
    dev->mii_regs[4] = 0x05E1;
    dev->mii_regs[5] = 0x45E1;
    dev->mii_regs[6] = 0x0001;

    dp83815_update_phy_state(dev, false);
}

/* EEPROM */
static uint16_t
dp83815_mac_word(const uint8_t *mac, int index)
{
    return ((uint16_t) mac[index + 1] << 8) | ((uint16_t) mac[index]);
}

/* MAC words are bit-serial encoded across consecutive EEPROM words */
static uint16_t
dp83815_encode_mac_word(uint16_t mac_word, uint16_t next_mac_word)
{
    uint16_t encoded = next_mac_word & 1;

    for (int bit = 1; bit < 16; bit++) {
        if (mac_word & (1u << bit))
            encoded |= (uint16_t) (1u << (16 - bit));
    }

    return encoded;
}

static void
dp83815_eeprom_get_mac(const uint16_t *eeprom, uint8_t *mac)
{
    uint64_t bits = (uint64_t) (eeprom[6] & 1);

    for (int j = 0; j < 16; j++) {
        if (eeprom[7] & (1u << (15 - j)))
            bits |= 1ULL << (1 + j);
        if (eeprom[8] & (1u << (15 - j)))
            bits |= 1ULL << (17 + j);
        if ((j < 15) && (eeprom[9] & (1u << (15 - j))))
            bits |= 1ULL << (33 + j);
    }

    for (int i = 0; i < 6; i++)
        mac[i] = (uint8_t) (bits >> (8 * i));
}

static uint8_t
dp83815_eeprom_checksum(const uint16_t *eeprom)
{
    uint32_t sum = 0;

    for (int i = 0; i < 11; i++)
        sum += (eeprom[i] >> 8) + (eeprom[i] & 0xFF);

    sum += 0x55;

    return (uint8_t) (0 - (uint8_t) sum);
}

static void
dp83815_build_eeprom(dp83815_t *dev)
{
    memset(dev->eeprom_words, 0, sizeof(dev->eeprom_words));

    dev->eeprom_words[0x00] = 0xD008;
    dev->eeprom_words[0x01] = 0x0400;
    dev->eeprom_words[0x02] = 0x2CD0;
    dev->eeprom_words[0x03] = 0xCF82;
    dev->eeprom_words[0x04] = 0x0000;
    dev->eeprom_words[0x05] = 0x0000;
    dev->eeprom_words[0x06] = dp83815_mac_word(dev->mac, 0) & 1;
    dev->eeprom_words[0x07] = dp83815_encode_mac_word(dp83815_mac_word(dev->mac, 0),
                                                       dp83815_mac_word(dev->mac, 2));
    dev->eeprom_words[0x08] = dp83815_encode_mac_word(dp83815_mac_word(dev->mac, 2),
                                                       dp83815_mac_word(dev->mac, 4));
    dev->eeprom_words[0x09] = dp83815_encode_mac_word(dp83815_mac_word(dev->mac, 4), 0);
    dev->eeprom_words[0x0A] = 0xA098;

    dev->eeprom_words[0x0B] = ((uint16_t) dp83815_eeprom_checksum(dev->eeprom_words) << 8) | 0x0055;
}

/* Emulates the "EEPROM auto-load": PMATCH, RFCR defaults and CFG defaults */
static void
dp83815_eeprom_reload(dp83815_t *dev)
{
    uint8_t mac[6];

    memcpy(mac, dev->mac, 6);
    if (dev->eeprom)
        dp83815_eeprom_get_mac(nmc93cxx_eeprom_data(dev->eeprom), mac);

    for (int i = 0; i < 3; i++)
        dev->rf_ram[i] = dp83815_mac_word(mac, i * 2);

    dev->rfcr       = RFCR_DEFAULT;
    dev->chipconfig = (dev->chipconfig & CFG_RO_MASK) | CFG_ANEG_SEL;
    dp83815_update_phy_state(dev, false);
}

/* Reset */
static void
dp83815_soft_reset(dp83815_t *dev)
{
    dev->tx_enabled = false;
    dev->tx_active  = false;
    dev->tx_cur_done = false;
    dev->rx_enabled = false;
    dev->rx_active  = false;
    dev->rx_cur_done = false;
    dev->tx_pkt_len = 0;

    dev->eectrl       = 0;
    dev->pci_bus_cfg  = 0;
    dev->intr_status  = IntrRxResetDone | IntrTxResetDone;
    dev->intr_mask    = 0;
    dev->intr_enable  = 0;
    dev->intr_holdoff = 0;
    dev->tx_ring_ptr  = 0;
    dev->tx_config    = 0;
    dev->rx_ring_ptr  = 0;
    dev->rx_config    = 0;
    dev->clk_run      = 0;
    dev->wol_cmd      = 0;
    dev->pause_cmd    = 0;
    dev->mib_ctrl     = 0;
    dev->rfcr         = RFCR_DEFAULT;

    dp83815_update_phy_state(dev, false);
    dp83815_update_irq(dev);
}

static void
dp83815_hard_reset(dp83815_t *dev)
{
    memset(dev->rf_ram, 0, sizeof(dev->rf_ram));
    dev->chipconfig = CFG_ANEG_SEL;
    dp83815_phy_reset(dev);
    dp83815_eeprom_reload(dev);
    dp83815_soft_reset(dev);
}

/* Receive filter */
static uint32_t
dp83815_crc32_fcs(const uint8_t *buf, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < size; i++) {
        crc ^= buf[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }

    return ~crc;
}

/* Big-endian CRC, used to index the 512-bit multicast/unicast hash table */
static uint32_t
dp83815_crc32_be(const uint8_t *buf, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < size; i++) {
        uint8_t c = buf[i];

        for (int bit = 0; bit < 8; bit++) {
            if (((crc >> 31) ^ c) & 1)
                crc = (crc << 1) ^ 0x04C11DB7u;
            else
                crc <<= 1;
            c >>= 1;
        }
    }

    return crc;
}

static bool
dp83815_pmatch_hit(const dp83815_t *dev, const uint8_t *da)
{
    for (int i = 0; i < 3; i++) {
        if (da[i * 2] != (dev->rf_ram[i] & 0xFF) || da[i * 2 + 1] != (dev->rf_ram[i] >> 8))
            return false;
    }

    return true;
}

static bool
dp83815_hash_hit(const dp83815_t *dev, const uint8_t *da)
{
    uint32_t idx   = (dp83815_crc32_be(da, 6) >> 23) & 0x1FF;
    uint32_t baddr = 0x200 + (idx >> 3);
    uint16_t word  = dev->rf_ram[baddr >> 1];

    return ((word >> (((baddr & 1) * 8) + (idx & 7))) & 1) != 0;
}

/* Returns the DEST class: 0 = rejected, 1 = unicast match, 2 = multicast, 3 = broadcast. */
static uint32_t
dp83815_rx_filter(const dp83815_t *dev, const uint8_t *da)
{
    uint32_t rfcr     = dev->rfcr;
    bool     is_bcast = (da[0] & da[1] & da[2] & da[3] & da[4] & da[5]) == 0xFF;
    bool     is_mcast = ((da[0] & 1) != 0) && !is_bcast;

    if (!(rfcr & RFCR_RFEN)) {
        if (is_bcast)
            return 3;
        if (!is_mcast && dp83815_pmatch_hit(dev, da))
            return 1;
        return 0;
    }

    if (is_bcast) {
        if (rfcr & (RFCR_AAB | RFCR_AAM))
            return 3;
        if ((rfcr & RFCR_MHEN) && dp83815_hash_hit(dev, da))
            return 3;
        return 0;
    }

    if (is_mcast) {
        if (rfcr & RFCR_AAM)
            return 2;
        if ((rfcr & RFCR_MHEN) && dp83815_hash_hit(dev, da))
            return 2;
        return 0;
    }

    if (rfcr & RFCR_AAU)
        return 1;
    if ((rfcr & RFCR_APM) && dp83815_pmatch_hit(dev, da))
        return 1;
    if ((rfcr & RFCR_UHEN) && dp83815_hash_hit(dev, da))
        return 1;

    return 0;
}

/* Receive path */
static void
dp83815_rx_idle(dp83815_t *dev)
{
    if (dev->rx_active) {
        dev->rx_active = false;
        dev->intr_status |= IntrRxIdle;
    }
    dp83815_update_irq(dev);
}

static bool
dp83815_rx_advance(dp83815_t *dev)
{
    struct dp83815_desc desc;
    uint32_t            link;

    if (!dev->rx_ring_ptr)
        return false;

    dma_bm_read(dev->rx_ring_ptr, (uint8_t *) &desc, sizeof(desc), 4);
    link = le32_to_cpu(desc.next_desc);
    if (!link)
        return false;

    dev->rx_ring_ptr = link;
    dev->rx_cur_done = false;
    return true;
}

static int
dp83815_rx_frame(dp83815_t *dev, const uint8_t *buf, int size, bool loopback)
{
    uint8_t  frame[DP83815_MAX_FRAME + 8];
    uint32_t len;
    uint32_t total;
    uint32_t off   = 0;
    uint32_t dest  = 1;
    uint32_t chain = 0;
    uint32_t crc;

    if (!dev->rx_enabled)
        return 1;
    if (!loopback && !dev->phy_link_up)
        return 1;
    if (size < 14)
        return 1;

    len = (uint32_t) size;
    if (len > DP83815_MAX_FRAME - 4)
        len = DP83815_MAX_FRAME - 4;
    memcpy(frame, buf, len);

    if (len < 60) {
        memset(frame + len, 0, 60 - len);
        len = 60;
    }

    if (!loopback) {
        dest = dp83815_rx_filter(dev, frame);
        if (!dest) {
            dp83815_log("DP83815: RX filter rejected %02X:%02X:%02X:%02X:%02X:%02X\n",
                        frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]);
            return 1;
        }
    }

    crc = cpu_to_le32(dp83815_crc32_fcs(frame, len));
    memcpy(frame + len, &crc, sizeof(crc));
    total = len + 4;

    if (dev->rx_cur_done) {
        if (!dp83815_rx_advance(dev)) {
            dp83815_rx_idle(dev);
            return 1;
        }
    }

    while (off < total) {
        struct dp83815_desc desc;
        uint32_t            cmd;
        uint32_t            buf_len;
        uint32_t            chunk;
        uint32_t            status;
        uint32_t            link;
        bool                last;

        if ((++chain > DP83815_MAX_DESC_CHAIN) || !dev->rx_ring_ptr) {
            dp83815_rx_idle(dev);
            return 1;
        }

        dma_bm_read(dev->rx_ring_ptr, (uint8_t *) &desc, sizeof(desc), 4);
        cmd     = le32_to_cpu(desc.cmd_status);
        buf_len = cmd & DescSizeMask;

        if ((cmd & DescOwn) || (buf_len == 0)) {
            dp83815_log("DP83815: RX out of descriptors (desc=%08X cmd=%08X)\n",
                        dev->rx_ring_ptr, cmd);
            dp83815_rx_idle(dev);
            return 1;
        }

        chunk = total - off;
        if (chunk > buf_len)
            chunk = buf_len;

        dma_bm_write(le32_to_cpu(desc.addr), frame + off, chunk, 4);
        off += chunk;
        last = (off >= total);

        status = (cmd & DescIntr) | DescOwn | chunk;
        if (last) {
            status |= DescPktOK | (dest << DescRxDestShift);
            if (loopback)
                status |= DescRxLoop;
        } else
            status |= DescMore;

        desc.cmd_status = cpu_to_le32(status);
        dma_bm_write(dev->rx_ring_ptr + 4, (uint8_t *) &desc.cmd_status, 4, 4);

        dev->rx_active = true;
        if (cmd & DescIntr)
            dev->intr_status |= IntrRxIntr;

        link = le32_to_cpu(desc.next_desc);
        if (link)
            dev->rx_ring_ptr = link;
        else {
            dev->rx_cur_done = true;
            if (!last) {
                dp83815_rx_idle(dev);
                return 1;
            }
        }
    }

    dev->intr_status |= IntrRxDone;
    if (dev->rx_cur_done)
        dp83815_rx_idle(dev);
    dp83815_update_irq(dev);

    return 1;
}

static int
dp83815_receive(void *priv, uint8_t *buf, int size)
{
    dp83815_t *dev = (dp83815_t *) priv;

    return dp83815_rx_frame(dev, buf, size, false);
}

/* Transmit path */
static void
dp83815_tx_frame(dp83815_t *dev)
{
    uint32_t len = dev->tx_pkt_len;

    if (len < 60) {
        memset(dev->tx_pkt + len, 0, 60 - len);
        len = 60;
    }

    if ((dev->tx_config & TXCFG_MLB) || (dev->mii_regs[0] & MII_BMCR_LOOPBACK))
        dp83815_rx_frame(dev, dev->tx_pkt, (int) len, true);
    else if (dev->nic && dev->phy_link_up)
        network_tx(dev->nic, dev->tx_pkt, (int) len);
}

static void
dp83815_tx_run(dp83815_t *dev)
{
    struct dp83815_desc desc;
    uint32_t            chain = 0;

    if (!dev->tx_enabled || !dev->tx_ring_ptr)
        return;

    if (dev->tx_cur_done) {
        uint32_t link;

        dma_bm_read(dev->tx_ring_ptr, (uint8_t *) &desc, sizeof(desc), 4);
        link = le32_to_cpu(desc.next_desc);
        if (!link)
            return;
        dev->tx_ring_ptr = link;
        dev->tx_cur_done = false;
    }

    dev->tx_active = true;

    while (++chain <= DP83815_MAX_DESC_CHAIN) {
        uint32_t cmd;
        uint32_t len;
        uint32_t newcmd;
        uint32_t link;

        dma_bm_read(dev->tx_ring_ptr, (uint8_t *) &desc, sizeof(desc), 4);
        cmd = le32_to_cpu(desc.cmd_status);

        if (!(cmd & DescOwn))
            break;

        len = cmd & DescSizeMask;
        if (len > DP83815_MAX_FRAME - dev->tx_pkt_len)
            len = DP83815_MAX_FRAME - dev->tx_pkt_len;
        if (len) {
            dma_bm_read(le32_to_cpu(desc.addr), dev->tx_pkt + dev->tx_pkt_len, len, 4);
            dev->tx_pkt_len += len;
        }

        newcmd = cmd & ~(DescOwn | DescPktOK | DescStatusMask);

        if (!(cmd & DescMore)) {
            newcmd |= DescPktOK;
            dp83815_tx_frame(dev);
            dev->tx_pkt_len = 0;
            dev->intr_status |= IntrTxDone;
        }
        if (cmd & DescIntr)
            dev->intr_status |= IntrTxIntr;

        desc.cmd_status = cpu_to_le32(newcmd);
        dma_bm_write(dev->tx_ring_ptr + 4, (uint8_t *) &desc.cmd_status, 4, 4);

        link = le32_to_cpu(desc.next_desc);
        if (!link) {
            dev->tx_cur_done = true;
            break;
        }
        dev->tx_ring_ptr = link;
    }

    dev->tx_active = false;
    dev->intr_status |= IntrTxIdle;
    dp83815_update_irq(dev);
}

/* Register access */
static uint32_t
dp83815_reg_read_ex(dp83815_t *dev, uint32_t off, bool side_effects)
{
    switch (off) {
        case ChipCmd:
            return (dev->rx_enabled ? RxOn : 0) | (dev->tx_enabled ? TxOn : 0);
        case ChipConfig:
            return dev->chipconfig;
        case EECtrl:
            {
                uint32_t ret = dev->eectrl;

                if (dev->eeprom && nmc93cxx_eeprom_read(dev->eeprom))
                    ret |= EEDO;

                return ret;
            }
        case PCIBusCfg:
            return dev->pci_bus_cfg;
        case IntrStatus:
            {
                uint32_t status = dev->intr_status;

                if (status & 0x03FF0000) /* bits 16-25 set */
                    status |= IntrHighBitsErr;
                if (side_effects) {
                    dev->intr_status = 0; /* reading the ISR clears it */
                    dp83815_update_irq(dev);
                }
                return status;
            }
        case IntrMask:
            return dev->intr_mask;
        case IntrEnable:
            return dev->intr_enable;
        case IntrHoldoff:
            return dev->intr_holdoff;
        case TxRingPtr:
            return dev->tx_ring_ptr;
        case TxConfig:
            return dev->tx_config;
        case RxRingPtr:
            return dev->rx_ring_ptr;
        case RxConfig:
            return dev->rx_config;
        case ClkRun:
            return dev->clk_run;
        case WOLCmd:
            return dev->wol_cmd;
        case PauseCmd:
            return dev->pause_cmd;
        case RxFilterAddr:
            return dev->rfcr;
        case RxFilterData:
            /* RFADDR is a BYTE address; each data word covers two bytes. */
            return dev->rf_ram[(dev->rfcr & 0x3FE) >> 1];
        case BootRomAddr:
        case BootRomData:
            return 0;
        case SiliconRev:
            return dev->silicon_rev;
        case StatsCtrl:
            return dev->mib_ctrl;
        default:
            if ((off >= 0x60) && (off < 0x80))
                return 0; /* MIB counters */
            if ((off >= 0x80) && (off < 0x100))
                return dev->mii_regs[PHY_REG(off)];
            return 0;
    }
}

static uint32_t
dp83815_reg_read(dp83815_t *dev, uint32_t off)
{
    uint32_t val = dp83815_reg_read_ex(dev, off, true);
    return val;
}

static int
dp83815_set_link_status(void *priv, uint32_t link_state)
{
    dp83815_t *dev = (dp83815_t *) priv;
#ifdef NET_LINK_DOWN
    bool up = !(link_state & NET_LINK_DOWN);
#else
    bool up = (link_state != 0);
#endif

    dp83815_log("DP83815: set_link_status(%08X) -> %s\n", link_state, up ? "up" : "down");

    dev->net_link_up = up;
    dp83815_update_phy_state(dev, true);

    return 0;
}

static void
dp83815_cmd_write(dp83815_t *dev, uint32_t val)
{
    dp83815_log("DP83815: CR write %08X\n", val);

    if (val & ChipReset) {
        dp83815_soft_reset(dev);
        return;
    }

    if (val & RxReset) {
        dev->rx_enabled  = false;
        dev->rx_active   = false;
        dev->rx_cur_done = false;
        dev->intr_status |= IntrRxIdle | IntrRxResetDone;
    }
    if (val & TxReset) {
        dev->tx_enabled  = false;
        dev->tx_active   = false;
        dev->tx_cur_done = false;
        dev->tx_pkt_len  = 0;
        dev->intr_status |= IntrTxIdle | IntrTxResetDone;
    }

    /* RXD/TXD take precedence over RXE/TXE in the same write */
    if (val & RxOff) {
        dev->rx_enabled = false;
        dev->rx_active  = false;
    } else if (val & RxOn) {
        dev->rx_enabled = true;
        dev->rx_active  = true;
    }

    if (val & TxOff) {
        dev->tx_enabled = false;
        dev->tx_active  = false;
    } else if (val & TxOn) {
        dev->tx_enabled = true;
        dp83815_tx_run(dev);
    }

    if (val & SWIntr)
        dev->intr_status |= IntrSWInt;

    dp83815_update_irq(dev);
}

static void
dp83815_reg_write(dp83815_t *dev, uint32_t off, uint32_t val)
{
    switch (off) {
        case ChipCmd:
            dp83815_cmd_write(dev, val);
            break;
        case ChipConfig:
            dev->chipconfig = (dev->chipconfig & CFG_RO_MASK) | (val & ~CFG_RO_MASK & ~(uint32_t) CfgPhyRst);
            if (val & CfgPhyRst)
                dp83815_phy_reset(dev); /* self-clearing */
            dp83815_update_phy_state(dev, true);
            break;
        case EECtrl:
            {
                bool eecs = (val & EECS) != 0;
                bool eesk = (val & EECLK) != 0;
                bool eedi = (val & EEDI) != 0;

                if (dev->eeprom)
                    nmc93cxx_eeprom_write(dev->eeprom, eecs, eesk, eedi);

                dev->eectrl = val & (EECS | EECLK | EEDI);
                break;
            }
        case PCIBusCfg:
            dev->pci_bus_cfg = val & ~PCI_EEPROM_RELOAD;
            if (val & PCI_EEPROM_RELOAD) {
                dp83815_log("DP83815: EEPROM reload\n");
                dp83815_eeprom_reload(dev); /* completes immediately */
            }
            break;
        case IntrStatus:
            break; /* read-only, cleared by reading */
        case IntrMask:
            dev->intr_mask = val;
            dp83815_update_irq(dev);
            break;
        case IntrEnable:
            dev->intr_enable = val & 1;
            dp83815_update_irq(dev);
            break;
        case IntrHoldoff:
            dev->intr_holdoff = val;
            break;
        case TxRingPtr:
            dp83815_log("DP83815: TXDP = %08X\n", val);
            dev->tx_ring_ptr = val & ~3u;
            dev->tx_cur_done = false;
            dev->tx_pkt_len  = 0;
            break;
        case TxConfig:
            dev->tx_config = val;
            break;
        case RxRingPtr:
            dp83815_log("DP83815: RXDP = %08X\n", val);
            dev->rx_ring_ptr = val & ~3u;
            dev->rx_cur_done = false;
            break;
        case RxConfig:
            dev->rx_config = val;
            break;
        case ClkRun:
            dev->clk_run = val;
            break;
        case WOLCmd:
            dev->wol_cmd = val;
            break;
        case PauseCmd:
            dev->pause_cmd = val;
            break;
        case RxFilterAddr:
            dev->rfcr = val;
            break;
        case RxFilterData:
            dev->rf_ram[(dev->rfcr & 0x3FE) >> 1] = (uint16_t) val; /* no auto-increment */
            break;
        case StatsCtrl:
            dev->mib_ctrl = val;
            break;
        case BMCR:
            if (val & MII_BMCR_RESET) {
                dp83815_phy_reset(dev);
                dp83815_update_phy_state(dev, true);
            } else {
                /* RESTART_AN is self-clearing; negotiation completes instantly */
                dev->mii_regs[0] = (uint16_t) val & ~MII_BMCR_RESTARTAN;
                dp83815_update_phy_state(dev, true);
            }
            break;
        case BMSR:
        case PHYIDR1:
        case PHYIDR2:
        case ANLPAR:
        case ANER:
        case PHYSTS:
            break; /* read-only */
        case ANAR:
            dev->mii_regs[4] = (uint16_t) val;
            dp83815_update_phy_state(dev, true);
            break;
        default:
            if ((off >= 0x80) && (off < 0x100))
                dev->mii_regs[PHY_REG(off)] = (uint16_t) val;
            break;
    }
}

/* Sub-dword accesses */
static uint32_t
dp83815_access_read(dp83815_t *dev, uint32_t off, int len)
{
    uint32_t val = dp83815_reg_read(dev, off & ~3u) >> ((off & 3u) * 8);

    return (len == 1) ? (val & 0xFF) : (len == 2) ? (val & 0xFFFF) : val;
}

static void
dp83815_access_write(dp83815_t *dev, uint32_t off, int len, uint32_t val)
{
    uint32_t reg   = off & ~3u;
    uint32_t shift = (off & 3u) * 8;
    uint32_t mask;

    if (len == 4) {
        dp83815_reg_write(dev, reg, val);
        return;
    }

    mask = ((len == 1) ? 0xFFu : 0xFFFFu) << shift;

    if (reg == ChipCmd) {
        dp83815_reg_write(dev, reg, (val << shift) & mask);
        return;
    }

    dp83815_reg_write(dev, reg,
                      (dp83815_reg_read_ex(dev, reg, false) & ~mask) | ((val << shift) & mask));
}

static uint8_t
dp83815_mmio_readb(uint32_t addr, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = addr - dev->mmio_base;

    return (off >= DP83815_MMIO_SIZE) ? 0xFF : (uint8_t) dp83815_access_read(dev, off, 1);
}

static uint16_t
dp83815_mmio_readw(uint32_t addr, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = addr - dev->mmio_base;

    return (off >= DP83815_MMIO_SIZE) ? 0xFFFF : (uint16_t) dp83815_access_read(dev, off, 2);
}

static uint32_t
dp83815_mmio_readl(uint32_t addr, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = addr - dev->mmio_base;

    return (off >= DP83815_MMIO_SIZE) ? 0xFFFFFFFF : dp83815_access_read(dev, off, 4);
}

static void
dp83815_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = addr - dev->mmio_base;

    if (off < DP83815_MMIO_SIZE)
        dp83815_access_write(dev, off, 1, val);
}

static void
dp83815_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = addr - dev->mmio_base;

    if (off < DP83815_MMIO_SIZE)
        dp83815_access_write(dev, off, 2, val);
}

static void
dp83815_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = addr - dev->mmio_base;

    if (off < DP83815_MMIO_SIZE)
        dp83815_access_write(dev, off, 4, val);
}

static uint8_t
dp83815_io_readb(uint16_t port, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = (uint32_t) port - dev->io_base;

    return (off >= DP83815_IO_SIZE) ? 0xFF : (uint8_t) dp83815_access_read(dev, off, 1);
}

static uint16_t
dp83815_io_readw(uint16_t port, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = (uint32_t) port - dev->io_base;

    return (off >= DP83815_IO_SIZE) ? 0xFFFF : (uint16_t) dp83815_access_read(dev, off, 2);
}

static uint32_t
dp83815_io_readl(uint16_t port, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = (uint32_t) port - dev->io_base;

    return (off >= DP83815_IO_SIZE) ? 0xFFFFFFFF : dp83815_access_read(dev, off, 4);
}

static void
dp83815_io_writeb(uint16_t port, uint8_t val, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = (uint32_t) port - dev->io_base;

    if (off < DP83815_IO_SIZE)
        dp83815_access_write(dev, off, 1, val);
}

static void
dp83815_io_writew(uint16_t port, uint16_t val, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = (uint32_t) port - dev->io_base;

    if (off < DP83815_IO_SIZE)
        dp83815_access_write(dev, off, 2, val);
}

static void
dp83815_io_writel(uint16_t port, uint32_t val, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;
    uint32_t   off = (uint32_t) port - dev->io_base;

    if (off < DP83815_IO_SIZE)
        dp83815_access_write(dev, off, 4, val);
}

static void
dp83815_io_update(dp83815_t *dev)
{
    if (dev->io_mapped) {
        io_removehandler((uint16_t) dev->io_base, DP83815_IO_SIZE,
                         dp83815_io_readb, dp83815_io_readw, dp83815_io_readl,
                         dp83815_io_writeb, dp83815_io_writew, dp83815_io_writel,
                         dev);
        dev->io_mapped = false;
    }

    dev->io_base = dev->pci_bar[0].addr & DP83815_IO_BAR_MASK;
    if ((dev->pci_conf[0x04] & PCI_COMMAND_IO) && dev->io_base &&
        (dev->io_base <= UINT16_MAX - DP83815_IO_SIZE + 1)) {
        io_sethandler((uint16_t) dev->io_base, DP83815_IO_SIZE,
                      dp83815_io_readb, dp83815_io_readw, dp83815_io_readl,
                      dp83815_io_writeb, dp83815_io_writew, dp83815_io_writel,
                      dev);
        dev->io_mapped = true;
    }
}

static void
dp83815_mmio_update(dp83815_t *dev)
{
    dev->mmio_base = dev->pci_bar[1].addr & DP83815_MMIO_BAR_MASK;
    mem_mapping_disable(&dev->mmio_mapping);
    if ((dev->pci_conf[0x04] & PCI_COMMAND_MEM) && dev->mmio_base)
        mem_mapping_set_addr(&dev->mmio_mapping, dev->mmio_base, DP83815_MMIO_SIZE);
}

/* PCI configuration space */
static uint8_t
dp83815_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    const dp83815_t *dev = (const dp83815_t *) priv;
    uint8_t          ret = 0x00;

    switch (addr) {
        case 0x00: ret = DP83815_VENDOR_ID & 0xFF; break;
        case 0x01: ret = DP83815_VENDOR_ID >> 8; break;
        case 0x02: ret = DP83815_DEVICE_ID & 0xFF; break;
        case 0x03: ret = DP83815_DEVICE_ID >> 8; break;
        case 0x04: ret = dev->pci_conf[0x04]; break;
        case 0x05: ret = dev->pci_conf[0x05]; break;
        case 0x06: ret = 0x80; break; /* fast back-to-back capable */
        case 0x07: ret = 0x02; break; /* DEVSEL timing = medium (datasheet: status 0x0290/0x0280) */
        case 0x08: ret = 0x01; break;
        case 0x09: ret = 0x00; break;
        case 0x0A: ret = 0x00; break;
        case 0x0B: ret = 0x02; break;
        case 0x0C: ret = 0x00; break;
        case 0x0D: ret = dev->pci_conf[0x0D]; break; /* latency timer: read back what was written */
        case 0x0E: ret = 0x00; break;
        case 0x0F: ret = 0x00; break;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            ret = (((dev->pci_bar[0].addr & DP83815_IO_BAR_MASK) | 0x01) >> ((addr & 3) * 8)) & 0xFF;
            break;
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            ret = ((dev->pci_bar[1].addr & DP83815_MMIO_BAR_MASK) >> ((addr & 3) * 8)) & 0xFF;
            break;
        case 0x2C:
        case 0x2D:
        case 0x2E:
        case 0x2F:
            ret = dev->pci_conf[addr];
            break;
        case 0x34: ret = 0x00; break; /* no capability list */
        case 0x3C: ret = dev->pci_conf[0x3C]; break;
        case 0x3D: ret = PCI_INTA; break;
        case 0x3E: ret = 0x0B; break; /* MIN_GNT = 11 */
        case 0x3F: ret = 0x34; break; /* MAX_LAT = 52 */
        default:
            break;
    }

    return ret;
}

static void
dp83815_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;

    switch (addr) {
        case 0x04:
            dev->pci_conf[0x04] = val & 0x47;
            dp83815_io_update(dev);
            dp83815_mmio_update(dev);
            break;
        case 0x05:
            dev->pci_conf[0x05] = val & 0x03;
            break;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            {
                int      shift = (addr & 3) * 8;
                uint32_t mask  = 0xFFu << shift;

                dev->pci_bar[0].addr = (dev->pci_bar[0].addr & ~mask) | (((uint32_t) val << shift) & mask);
                dp83815_io_update(dev);
                break;
            }
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            {
                int      shift = (addr & 3) * 8;
                uint32_t mask  = 0xFFu << shift;

                dev->pci_bar[1].addr = (dev->pci_bar[1].addr & ~mask) | (((uint32_t) val << shift) & mask);
                dp83815_mmio_update(dev);
                break;
            }
        case 0x0D:
            dev->pci_conf[0x0D] = val;
            break;
        case 0x3C:
            dev->pci_conf[0x3C] = val;
            break;
        default:
            break;
    }
}

static void
dp83815_reset(void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;

    dp83815_hard_reset(dev);
}

static void *
dp83815_init(UNUSED(const device_t *info))
{
    dp83815_t *dev = calloc(1, sizeof(dp83815_t));
    if (!dev)
        return NULL;

    dev->inst = device_get_instance();

    int card_model = (info->local & 0x8000) ? 0 : device_get_config_int("model");

    int mac = device_get_config_mac("mac", -1);

    if (card_model > 0) {
        dev->mac[0] = 0x00;
        dev->mac[1] = 0x02;
        dev->mac[2] = 0xE3;
    } else {
        dev->mac[0] = 0x08;
        dev->mac[1] = 0x00;
        dev->mac[2] = 0x17;
    }

    if (mac < 0) {
        dev->mac[3] = random_generate();
        dev->mac[4] = random_generate();
        dev->mac[5] = random_generate();

        device_set_config_mac("mac",
                              ((int) dev->mac[3] << 16) |
                              ((int) dev->mac[4] << 8) |
                              dev->mac[5]);
    } else {
        dev->mac[3] = (mac >> 16) & 0xff;
        dev->mac[4] = (mac >> 8) & 0xff;
        dev->mac[5] = mac & 0xff;
    }

    dp83815_build_eeprom(dev);

    nmc93cxx_eeprom_params_t eeprom_params;
    char                     eeprom_filename[512];

    memset(&eeprom_params, 0, sizeof(eeprom_params));
    memset(eeprom_filename, 0, sizeof(eeprom_filename));

    snprintf(eeprom_filename, sizeof(eeprom_filename),
             "dp83815_%d.nvr", dev->inst);

    eeprom_params.type            = NMC_93C46_x16_64;
    eeprom_params.filename        = eeprom_filename;
    eeprom_params.default_content = dev->eeprom_words;

    dev->eeprom = device_add_inst_params(&nmc93cxx_device,
                                         dev->inst,
                                         &eeprom_params);

    if (!dev->eeprom) {
        free(dev);
        return NULL;
    }

    uint16_t *eeprom_data = (uint16_t *) nmc93cxx_eeprom_data(dev->eeprom);
        for (uint16_t word = 0x06; word <= 0x09; word++)
            eeprom_data[word] = dev->eeprom_words[word];
        eeprom_data[0x0B] = ((uint16_t) dp83815_eeprom_checksum(eeprom_data) << 8) | 0x0055;

    dev->silicon_rev = 0x00000302;

    mem_mapping_add(&dev->mmio_mapping, 0, DP83815_MMIO_SIZE,
                    dp83815_mmio_readb, dp83815_mmio_readw, dp83815_mmio_readl,
                    dp83815_mmio_writeb, dp83815_mmio_writew, dp83815_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->mmio_mapping);

    /* Setup default PCI config */
    dev->pci_conf[0x04] = PCI_COMMAND_L_MEM | PCI_COMMAND_L_BM;
    dev->pci_conf[0x05] = 0x00;
    dev->pci_conf[0x08] = 0x01;
    dev->pci_conf[0x0A] = 0x00;
    dev->pci_conf[0x0B] = 0x02;

    switch (card_model) {
        case 1: /* NETGEAR FA311 */
            dev->pci_conf[0x2C] = 0x85;
            dev->pci_conf[0x2D] = 0x13;
            dev->pci_conf[0x2E] = 0x11;
            dev->pci_conf[0x2F] = 0xF3;
            break;
        case 2: /* NETGEAR FA312 */
            dev->pci_conf[0x2C] = 0x85;
            dev->pci_conf[0x2D] = 0x13;
            dev->pci_conf[0x2E] = 0x12;
            dev->pci_conf[0x2F] = 0xF3;
            break;
        case 0: /* Generic DP83815 */
        default:
            dev->pci_conf[0x2C] = 0x0B;
            dev->pci_conf[0x2D] = 0x10;
            dev->pci_conf[0x2E] = 0x20;
            dev->pci_conf[0x2F] = 0x00;
            break;
    }

    dev->net_link_up = true;
    dp83815_hard_reset(dev);

    dev->nic = network_attach(dev, dev->mac, dp83815_receive, dp83815_set_link_status);
    pci_add_card((info->local & 0x8000) ? PCI_ADD_NETWORK : PCI_ADD_NORMAL, dp83815_pci_read, dp83815_pci_write, dev, &dev->pci_slot);

    dp83815_update_phy_state(dev, false);

    dp83815_log("DP83815: init inst=%d MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                dev->inst,
                dev->mac[0], dev->mac[1], dev->mac[2],
                dev->mac[3], dev->mac[4], dev->mac[5]);

    return dev;
}

static void
dp83815_close(void *priv)
{
    dp83815_t *dev = (dp83815_t *) priv;

    if (dev->io_mapped)
        io_removehandler((uint16_t) dev->io_base, DP83815_IO_SIZE,
                         dp83815_io_readb, dp83815_io_readw, dp83815_io_readl,
                         dp83815_io_writeb, dp83815_io_writew, dp83815_io_writel,
                         dev);
    mem_mapping_disable(&dev->mmio_mapping);
    free(dev);
}

static const device_config_t dp83815_config[] = {
    {
        .name           = "model",
        .description    = "Model",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            {
                .description = "Generic DP83815",
                .value       = 0
            },
            {
                .description = "NETGEAR FA311",
                .value       = 1
            },
            {
                .description = "NETGEAR FA312",
                .value       = 2
            },
            { .description = "" }
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

const device_t dp83815_device = {
    .name          = "National Semiconductor DP83815/16",
    .internal_name = "dp83815",
    .flags         = DEVICE_PCI,
    .local         = 0x0001,
    .init          = dp83815_init,
    .close         = dp83815_close,
    .reset         = dp83815_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .alias         = "National Semiconductor DP83815/16",
    .config        = dp83815_config
};

const device_t dp83815_onboard_device = {
    .name          = "National Semiconductor DP83815/16 (Onboard)",
    .internal_name = "dp83815_onboard",
    .flags         = DEVICE_PCI,
    .local         = 0x8001,
    .init          = dp83815_init,
    .close         = dp83815_close,
    .reset         = dp83815_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .alias         = "National Semiconductor DP83815/16",
    .config        = &dp83815_config[1]
};

