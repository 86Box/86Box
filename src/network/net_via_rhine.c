/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the VIA Rhine family of PCI Fast Ethernet
 *          controllers: VT86C100A (Rhine I), VT6102 (Rhine II), VT6105
 *          (Rhine III) and VT6105M (Rhine III Management Adapter).
 *
 *          From the VT6105 and VT6105M data sheets (revision 1.0,
 *          2008-11-28) and VIA's own Linux driver (rhinefet 2002, in
 *          VIA's driver package), which gives the descriptor bit names
 *          the data sheets swap (TCR_STP is 20h, TCR_EDP 40h, as the
 *          drivers use them).
 *
 *          Emulated: PCI configuration space (power management capability
 *          on Rhine II/III), the MAC registers through I/O and memory BARs,
 *          the descriptor rings (RX, TX; eight TX queues and the CAM on the
 *          VT6105M, with its checksum and VLAN tag insertion), interrupts
 *          (ISR/IMR and the Rhine II/III MISR/MIMR with the soft timers),
 *          full-duplex PAUSE flow control and VT6105M MIB counters,
 *          VT6105M Wake-on-LAN event matching and PME status,
 *          the MII management port with the built-in PHY (link up,
 *          auto-negotiation completes at once) and the 93C46 EEPROM
 *          (auto-load of the MAC address, subsystem IDs and configuration
 *          registers, and direct programming through EECSR).
 *
 *          Not emulated: Wake-on-LAN PME pin timing, half-duplex jam flow
 *          control, early receive/transmit, and the boot ROM.
 *
 * Authors: 86Box contributors.
 *
 *          Copyright 2026 86Box contributors.
 * 
 *          Copyright 2026 Xeon3D
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
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

#define RHINE_MAX_FRAME      2048
#define RHINE_MAX_DESC_CHAIN 256
#define RHINE_RX_MAX_DESCS   32
#define RHINE_TX_QUEUES      8

#ifdef ENABLE_VIA_RHINE_LOG
int via_rhine_do_log = ENABLE_VIA_RHINE_LOG;

static void
via_rhine_log(const char *fmt, ...)
{
    va_list ap;

    if (via_rhine_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define via_rhine_log(fmt, ...)
#endif

/* Chip types (device_t local). */
enum {
    RHINE_VT86C100A = 0,
    RHINE_VT6102,
    RHINE_VT6105,
    RHINE_VT6105M
};

/* MAC registers */
enum {
    REG_PAR     = 0x00,
    REG_RCR     = 0x06,
    REG_TCR     = 0x07,
    REG_CR0     = 0x08,
    REG_CR1     = 0x09,
    REG_TQWK    = 0x0A,
    REG_ISR0    = 0x0C,
    REG_ISR1    = 0x0D,
    REG_IMR0    = 0x0E,
    REG_IMR1    = 0x0F,
    REG_MAR     = 0x10,
    REG_CUR_RD  = 0x18,
    REG_CUR_TD  = 0x1C,
    REG_MIICFG  = 0x6C,
    REG_MIISR   = 0x6D,
    REG_BCR0    = 0x6E,
    REG_BCR1    = 0x6F,
    REG_MIICR   = 0x70,
    REG_MIIAD   = 0x71,
    REG_MIIDATA = 0x72,
    REG_EECSR   = 0x74,
    REG_CFGA    = 0x78,
    REG_CFGB    = 0x79,
    REG_CFGC    = 0x7A,
    REG_CFGD    = 0x7B,
    REG_MISCCR0 = 0x80,
    REG_MISCCR1 = 0x81,
    REG_PMCPORT = 0x82,
    REG_STICKHW = 0x83,
    REG_MISR    = 0x84,
    REG_MIMR    = 0x86,
    REG_CAMMASK = 0x88,
    REG_BPCMD   = 0x90,
    REG_CAMC    = 0x92,
    REG_CAMADD  = 0x93,
    REG_MIBCR   = 0x94,
    REG_MIBDAT  = 0x96,
    REG_FCR0    = 0x98,
    REG_FCR1    = 0x99,
    REG_PAUSE   = 0x9A,
    REG_TIMER0  = 0x9C,
    REG_TIMER1  = 0x9E,
    REG_WOLCR_S = 0xA0,
    REG_WOLCR_C = 0xA4,
    REG_PWRCSR_S = 0xA8,
    REG_PWRCSR_C = 0xAC
};

#define RCR_PROM  0x10
#define RCR_AB    0x08
#define RCR_AM    0x04

#define TCR_LB    0x06 /* loopback mode, bits 2-1 */

#define CR0_RDMD  0x40
#define CR0_TDMD  0x20
#define CR0_TXON  0x10
#define CR0_RXON  0x08
#define CR0_STOP  0x04
#define CR0_STRT  0x02
#define CR0_INIT  0x01

#define CR1_SFRST 0x80
#define CR1_RDMD1 0x40
#define CR1_TDMD1 0x20
#define CR1_DPOLL 0x08

#define MISCCR0_TXFC  0x10
#define MISCCR0_RXFC  0x08
#define MISCCR0_HD_FC 0x04

#define PAUSE_DEST0 0x01
#define PAUSE_DEST1 0x80
#define PAUSE_DEST2 0xC2
#define PAUSE_DEST3 0x00
#define PAUSE_DEST4 0x00
#define PAUSE_DEST5 0x01

/* ISR0/ISR1 as one 16-bit word */
#define ISR_PRX   0x0001
#define ISR_PTX   0x0002
#define ISR_RU    0x0020 /* receive descriptor unavailable */
#define ISR_NORBF 0x1000 /* no receive buffer */
#define ISR_SRCI  0x4000 /* port state change */
#define ISR_MIBOVF 0x0080

#define MISR_TM1  0x02
#define MISR_TM0  0x01

#define MIISR_N_FDX 0x04
#define MIISR_LNKFL 0x02
#define MIISR_SPD10 0x01
#define MIISR_NOCBL 0x10

#define MIICR_MAUTO 0x80
#define MIICR_RCMD  0x40
#define MIICR_WCMD  0x20

#define MIIAD_MIDLE 0x80
#define MIIAD_MDONE 0x20

#define EECSR_EEPR   0x80
#define EECSR_AUTOLD 0x20
#define EECSR_DPM    0x10
#define EECSR_CS     0x08
#define EECSR_SK     0x04
#define EECSR_DI     0x02
#define EECSR_DO     0x01

#define CFGA_EELOAD 0x80

#define MISCCR0_TM0US 0x20
#define MISCCR0_TM0SP 0x02 /* timer 0 expired (write 0 to clear) */
#define MISCCR0_TM0EN 0x01
#define MISCCR1_FORSRST 0x40
#define MISCCR1_TM1US 0x04
#define MISCCR1_TM1EN 0x01

#define CAMC_CAMRD  0x08
#define CAMC_CAMWR  0x04
#define CAMC_VCAMSL 0x02
#define CAMC_CAMEN  0x01

/* Descriptors */
#define DESC_OWN      0x80000000u
#define RSR_RXOK      0x8000u
#define RSR_MAR       0x2000u
#define RSR_BAR       0x1000u
#define RSR_PHY       0x0800u
#define RSR_CHN       0x0400u
#define RSR_STP       0x0200u
#define RSR_EDP       0x0100u
#define TDES1_BUFLEN  0x000007FFu
#define TCR_IC        0x80
#define TCR_EDP       0x40
#define TCR_STP       0x20
#define TCR_TCPCK     0x10
#define TCR_UDPCK     0x08
#define TCR_IPCK      0x04
#define TCR_TAG       0x02

#define MII_BMCR_RESET     0x8000u
#define MII_BMCR_LOOPBACK  0x4000u
#define MII_BMCR_SPEED100  0x2000u
#define MII_BMCR_ANEG      0x1000u
#define MII_BMCR_POWERDOWN 0x0800u
#define MII_BMCR_RESTARTAN 0x0200u
#define MII_BMCR_FULL      0x0100u
#define MII_BMSR_ANEGDONE  0x0020u
#define MII_BMSR_LINK      0x0004u
#define MII_ANAR_100FD     0x0100u
#define MII_ANAR_100HD     0x0080u
#define MII_ANAR_10FD      0x0040u

typedef struct rhine_model_t {
    const char *name;
    uint16_t    device_id;
    uint8_t     revision;
    uint16_t    subsys_id;
    uint16_t    phy_id2;
    uint16_t    io_size;
    bool        has_pm;   /* power management capability, MISR/MIMR, soft timers */
    bool        has_mgmt; /* VT6105M: CAM, eight TX queues, checksum and tag insertion */
    uint8_t     cfgd;     /* EEPROM default for CFG_D */
} rhine_model_t;

static const rhine_model_t rhine_models[] = {
    /* Subsystem IDs as VIA's FETNDIS.INF lists them for its reference designs. */
    [RHINE_VT86C100A] = { "VT86C100A", 0x3043, 0x06, 0x0100, 0x8F20, 0x80, false, false, 0x00 },
    [RHINE_VT6102]    = { "VT6102",    0x3065, 0x42, 0x0102, 0x8F20, 0x100, true, false, 0x80 },
    [RHINE_VT6105]    = { "VT6105",    0x3106, 0x86, 0x0105, 0x8F43, 0x100, true, false, 0x80 },
    [RHINE_VT6105M]   = { "VT6105M",   0x3053, 0x96, 0x0106, 0x8F43, 0x100, true, true,  0x80 },
};

typedef struct rhine_t {
    const rhine_model_t *model;

    uint8_t       pci_conf[256];
    uint8_t       pci_slot;
    uint8_t       irq_state;
    uint32_t      io_base;
    bool          io_mapped;
    uint32_t      mmio_base;
    mem_mapping_t mmio_mapping;

    netcard_t *nic;
    uint8_t    mac[6];

    uint8_t  regs[256]; /* plain read/write registers */
    uint8_t  cr0;
    uint8_t  cr1;
    uint16_t isr;
    uint16_t imr;
    uint8_t  misr;
    uint8_t  mimr;
    uint32_t rx_ptr;
    uint32_t tx_ptr[RHINE_TX_QUEUES];
    uint8_t  miiad;
    uint16_t miidata;
    uint8_t  eecsr;
    uint8_t  wol[4];
    uint8_t  pwrcsr[2];
    uint8_t  wol_status[2];
    uint8_t  wol_patterns[64];
    uint8_t  wol_masks[64];
    uint16_t mib[12];
    uint8_t  mib_index;
    uint8_t  mib_byte;

    uint8_t  camc;
    uint8_t  camadd;
    uint8_t  cam_data[8];
    uint8_t  mcam[32][6];
    uint16_t vcam[32];
    uint32_t mcam_mask;
    uint32_t vcam_mask;

    uint16_t mii_regs[32];
    bool     net_link_up;
    bool     phy_link_up;
    bool     speed100;
    bool     full_duplex;

    nmc93cxx_eeprom_t *eeprom;
    uint16_t           eeprom_words[64];

    bool rx_stalled;

    pc_timer_t timer0;
    pc_timer_t timer1;
    pc_timer_t pause_timer;
    bool       tx_paused;

    uint32_t tx_len[RHINE_TX_QUEUES];
    bool     tx_in_frame[RHINE_TX_QUEUES];
    uint8_t  tx_tcr[RHINE_TX_QUEUES];
    uint32_t tx_tdes0[RHINE_TX_QUEUES];
    uint8_t  tx_buf[RHINE_TX_QUEUES][RHINE_MAX_FRAME + 4];
} rhine_t;

static void rhine_tx_run(rhine_t *dev);
static void rhine_pause_timer_cb(void *priv);
static void rhine_update_irq(rhine_t *dev);
static uint32_t rhine_crc32_le(const uint8_t *buf, uint32_t size);

enum {
    MIB_RX_NOBUF,
    MIB_RX_ERROR,
    MIB_RX_FCS,
    MIB_RX_MISSED,
    MIB_RX_ALIGN,
    MIB_RX_LONG,
    MIB_RX_INRANGE,
    MIB_RX_BAD_OPCODE,
    MIB_RX_PAUSE,
    MIB_TX_PAUSE,
    MIB_TX_SQE,
    MIB_RX_SYMBOL
};

static void
rhine_mib_event(rhine_t *dev, int counter)
{
    uint16_t old;

    if (!dev->model->has_mgmt || counter < 0 || counter >= 12)
        return;

    old = dev->mib[counter];
    dev->mib[counter]++;
    if (!dev->mib[counter] && old == UINT16_MAX)
        dev->isr |= ISR_MIBOVF;
    if ((dev->regs[REG_MIBCR] & 0x10) &&
        (dev->mib[counter] >= ((dev->regs[REG_MIBCR] & 0x20) ? 0x8000 : 0xC000)))
        dev->isr |= ISR_MIBOVF;
    rhine_update_irq(dev);
}

static void
rhine_wol_event(rhine_t *dev, uint8_t bit)
{
    if (!dev->model->has_mgmt || !(dev->wol[0] & bit))
        return;

    dev->wol_status[0] |= bit;
    dev->pci_conf[0x45] |= 0x80; /* PME_Status */
}

static bool
rhine_wol_magic(const rhine_t *dev, const uint8_t *buf, int size)
{
    for (int start = 6; start + 16 * 6 <= size; start++) {
        bool match = true;

        if (start < 6 || memcmp(buf + start - 6, "\xff\xff\xff\xff\xff\xff", 6))
            continue;
        for (int i = 0; i < 16; i++)
            if (memcmp(buf + start + i * 6, dev->mac, 6)) {
                match = false;
                break;
            }
        if (match)
            return true;
    }
    return false;
}

static bool
rhine_wol_pattern(const rhine_t *dev, const uint8_t *buf, int size, int pattern)
{
    uint8_t selected[128];
    uint32_t crc;
    int count = 0;

    for (int i = 0; i < size && i < 128; i++) {
        if (dev->wol_masks[pattern * 16 + (i >> 3)] & (1 << (i & 7)))
            selected[count++] = buf[i];
    }
    if (!count)
        return false;
    crc = rhine_crc32_le(selected, count);
    return crc == ((uint32_t) dev->wol_patterns[pattern * 4] |
                   ((uint32_t) dev->wol_patterns[pattern * 4 + 1] << 8) |
                   ((uint32_t) dev->wol_patterns[pattern * 4 + 2] << 16) |
                   ((uint32_t) dev->wol_patterns[pattern * 4 + 3] << 24));
}

static void
rhine_check_wol(rhine_t *dev, const uint8_t *buf, int size)
{
    if (!dev->model->has_mgmt || size < 6)
        return;
    if ((dev->wol[0] & 0x20) && rhine_wol_magic(dev, buf, size))
        rhine_wol_event(dev, 0x20);
    if ((dev->wol[0] & 0x10) && !memcmp(buf, dev->mac, 6))
        rhine_wol_event(dev, 0x10);
    for (int i = 0; i < 4; i++)
        if ((dev->wol[0] & (1 << i)) && rhine_wol_pattern(dev, buf, size, i))
            rhine_wol_event(dev, 1 << i);
}

static void
rhine_mib_reset_read(rhine_t *dev)
{
    dev->mib_index = 0;
    dev->mib_byte = 0;
}

static uint8_t
rhine_mib_read(rhine_t *dev)
{
    uint16_t value;
    uint8_t ret;

    if (!dev->model->has_mgmt || !(dev->regs[REG_MIBCR] & 0x10))
        return 0;

    value = dev->mib[dev->mib_index];
    ret = (dev->mib_byte++ == 0) ? (uint8_t) value : (uint8_t) (value >> 8);
    if (dev->mib_byte == 2) {
        dev->mib_byte = 0;
        if (++dev->mib_index == 12) {
            memset(dev->mib, 0, sizeof(dev->mib));
            rhine_mib_reset_read(dev);
        }
    }
    return ret;
}

static void
rhine_update_irq(rhine_t *dev)
{
    bool irq = (dev->isr & dev->imr) != 0;

    if (dev->model->has_pm && (dev->misr & dev->mimr))
        irq = true;

    if (irq)
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    else
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
}

static bool
rhine_rx_on(const rhine_t *dev)
{
    return (dev->cr0 & (CR0_STRT | CR0_RXON)) == (CR0_STRT | CR0_RXON);
}

static bool
rhine_tx_on(const rhine_t *dev)
{
    return (dev->cr0 & (CR0_STRT | CR0_TXON)) == (CR0_STRT | CR0_TXON);
}

static bool
rhine_flow_control_enabled(const rhine_t *dev, uint8_t bit)
{
    return dev->model->has_mgmt && dev->full_duplex && (dev->regs[REG_MISCCR0] & bit);
}

static void
rhine_set_pause(rhine_t *dev, uint16_t quanta)
{
    if (!quanta) {
        dev->tx_paused = false;
        timer_disable(&dev->pause_timer);
        rhine_tx_run(dev);
        return;
    }

    dev->tx_paused = true;
    timer_on_auto(&dev->pause_timer, (double) quanta * 512.0 / (dev->speed100 ? 100.0 : 10.0));
}

static void
rhine_send_pause(rhine_t *dev, uint16_t quanta)
{
    uint8_t frame[60] = {
        PAUSE_DEST0, PAUSE_DEST1, PAUSE_DEST2, PAUSE_DEST3, PAUSE_DEST4, PAUSE_DEST5,
        0, 0, 0, 0, 0, 0, 0x88, 0x08, 0x00, 0x01,
        (uint8_t) (quanta >> 8), (uint8_t) quanta
    };

    if (!dev->nic || !dev->phy_link_up)
        return;
    memcpy(frame + 6, dev->mac, sizeof(dev->mac));
    network_tx(dev->nic, frame, sizeof(frame));
    rhine_mib_event(dev, MIB_TX_PAUSE);
}

static uint8_t
rhine_pause_low_threshold(const rhine_t *dev)
{
    return (uint8_t[]) { 4, 8, 16, 24 }[(dev->regs[REG_FCR1] >> 6) & 3];
}

static uint8_t
rhine_pause_high_threshold(const rhine_t *dev)
{
    return (uint8_t[]) { 24, 32, 48, 64 }[(dev->regs[REG_FCR1] >> 4) & 3];
}

static bool
rhine_receive_pause(rhine_t *dev, const uint8_t *buf, int size)
{
    static const uint8_t pause_dest[] = {
        PAUSE_DEST0, PAUSE_DEST1, PAUSE_DEST2, PAUSE_DEST3, PAUSE_DEST4, PAUSE_DEST5
    };
    uint16_t quanta;

    if (!rhine_flow_control_enabled(dev, MISCCR0_RXFC) || size < 18)
        return false;
    if (memcmp(buf, pause_dest, sizeof(pause_dest)))
        return false;
    if (buf[12] != 0x88 || buf[13] != 0x08 || buf[14] != 0x00 || buf[15] != 0x01) {
        rhine_mib_event(dev, MIB_RX_BAD_OPCODE);
        return false;
    }

    quanta = ((uint16_t) buf[16] << 8) | buf[17];
    via_rhine_log("VIA Rhine: received PAUSE frame (%u quanta)\n", quanta);
    rhine_mib_event(dev, MIB_RX_PAUSE);
    rhine_set_pause(dev, quanta);
    return true;
}

static void
rhine_pause_timer_cb(void *priv)
{
    rhine_t *dev = (rhine_t *) priv;

    dev->tx_paused = false;
    rhine_tx_run(dev);
}

/* PHY */
static void
rhine_update_phy_state(rhine_t *dev, bool notify)
{
    uint16_t bmcr = dev->mii_regs[0];
    bool     link = dev->net_link_up && !(bmcr & MII_BMCR_POWERDOWN);
    bool     aneg = (bmcr & MII_BMCR_ANEG) != 0;

    if (aneg) {
        uint16_t common = dev->mii_regs[4] & dev->mii_regs[5];

        dev->speed100    = (common & (MII_ANAR_100FD | MII_ANAR_100HD)) != 0;
        dev->full_duplex = dev->speed100 ? !!(common & MII_ANAR_100FD) : !!(common & MII_ANAR_10FD);
    } else {
        dev->speed100    = (bmcr & MII_BMCR_SPEED100) != 0;
        dev->full_duplex = (bmcr & MII_BMCR_FULL) != 0;
    }

    dev->mii_regs[1] &= ~(MII_BMSR_ANEGDONE | MII_BMSR_LINK);
    if (link) {
        dev->mii_regs[1] |= MII_BMSR_LINK;
        if (aneg)
            dev->mii_regs[1] |= MII_BMSR_ANEGDONE;
    }
    /* Auto-negotiation expansion: the partner can auto-negotiate. */
    dev->mii_regs[6] = (dev->mii_regs[6] & ~0x0001) | ((link && aneg) ? 0x0001 : 0x0000);
    /* PHY status (14h): link up, link, auto-negotiation done, speed, duplex */
    dev->mii_regs[0x14] = (link ? 0x5000 : 0x2000) | ((link && aneg) ? 0x0800 : 0) |
                          (dev->speed100 ? 0x0002 : 0) | (dev->full_duplex ? 0x0001 : 0);

    if (dev->phy_link_up != link) {
        bool old_link = dev->phy_link_up;

        dev->phy_link_up = link;
        via_rhine_log("VIA Rhine: link is now %s\n", link ? "up" : "down");
        if (old_link && !link)
            rhine_wol_event(dev, 0x80);
        else if (!old_link && link)
            rhine_wol_event(dev, 0x40);
        if (notify)
            dev->isr |= ISR_SRCI;
    }

    if (dev->nic)
        dev->nic->byte_period = dev->speed100 ? NET_PERIOD_100M : NET_PERIOD_10M;

    if (notify)
        rhine_update_irq(dev);
}

static void
rhine_phy_reset(rhine_t *dev)
{
    memset(dev->mii_regs, 0, sizeof(dev->mii_regs));

    dev->mii_regs[0x00] = MII_BMCR_SPEED100 | MII_BMCR_ANEG | MII_BMCR_FULL; /* 3100h */
    dev->mii_regs[0x01] = 0x7849;
    dev->mii_regs[0x02] = 0x0101;
    dev->mii_regs[0x03] = dev->model->phy_id2;
    dev->mii_regs[0x04] = 0x05E1;
    dev->mii_regs[0x05] = 0x45E1; /* the link partner: everything, acknowledged */
    dev->mii_regs[0x06] = 0x0004;
    dev->mii_regs[0x07] = 0x2001;
    dev->mii_regs[0x10] = 0x0800;
    dev->mii_regs[0x11] = 0xF7FF;
    dev->mii_regs[0x12] = 0x0800;
    dev->mii_regs[0x13] = 0xFFFC;
    dev->mii_regs[0x19] = 0x0001;

    rhine_update_phy_state(dev, false);
}

static uint16_t
rhine_phy_read(rhine_t *dev, uint8_t phy, uint8_t reg)
{
    if (phy != 1)
        return 0xFFFF;

    return dev->mii_regs[reg & 0x1F];
}

static void
rhine_phy_write(rhine_t *dev, uint8_t phy, uint8_t reg, uint16_t val)
{
    if (phy != 1)
        return;

    reg &= 0x1F;
    switch (reg) {
        case 0x00:
            if (val & MII_BMCR_RESET)
                rhine_phy_reset(dev);
            else
                dev->mii_regs[0] = val & ~MII_BMCR_RESTARTAN & 0xFF80; /* negotiation completes at once */
            rhine_update_phy_state(dev, true);
            break;
        case 0x04:
            dev->mii_regs[4] = (val & ~0x4000) | 0x0001;
            rhine_update_phy_state(dev, true);
            break;
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x05:
        case 0x06:
        case 0x08:
        case 0x14:
            break; /* read-only */
        default:
            dev->mii_regs[reg] = val;
            break;
    }
}

static uint8_t
rhine_miisr(const rhine_t *dev)
{
    uint8_t ret = 0;

    if (!dev->phy_link_up)
        ret |= MIISR_LNKFL | MIISR_NOCBL;
    if (!dev->speed100)
        ret |= MIISR_SPD10;
    if (dev->full_duplex)
        ret |= MIISR_N_FDX;

    return ret;
}

/* EEPROM */
static void
rhine_build_eeprom(rhine_t *dev)
{
    uint8_t sum = 0;

    memset(dev->eeprom_words, 0, sizeof(dev->eeprom_words));

    for (int i = 0; i < 3; i++)
        dev->eeprom_words[i] = dev->mac[i * 2] | (dev->mac[i * 2 + 1] << 8);
    dev->eeprom_words[0x04] = dev->model->subsys_id;
    dev->eeprom_words[0x05] = 0x1106;
    dev->eeprom_words[0x08] = dev->model->has_pm ? 0x001F : 0x0000; /* D0-D3cold capable */
    dev->eeprom_words[0x0B] = 0x0803;                               /* MAX_LAT 8, MIN_GNT 3 */
    dev->eeprom_words[0x0C] = 0x0E09;                               /* BCR1, BCR0 */
    dev->eeprom_words[0x0D] = 0x0000;                               /* CFG_B, CFG_A */
    dev->eeprom_words[0x0E] = dev->model->cfgd << 8;                /* CFG_D, CFG_C */

    /* Word 0Fh: 73h (programmed) and a checksum whose algorithm the data sheet does
       not give; the bytes are made to sum to zero, which nothing here checks. */
    for (int i = 0; i < 0x0F; i++)
        sum += (dev->eeprom_words[i] & 0xFF) + (dev->eeprom_words[i] >> 8);
    sum += 0x73;
    dev->eeprom_words[0x0F] = ((uint8_t) (0 - sum) << 8) | 0x73;
}

/* The auto-load: Ethernet address, subsystem IDs and the configuration registers. */
static void
rhine_eeprom_load(rhine_t *dev)
{
    const uint16_t *ee = dev->eeprom ? nmc93cxx_eeprom_data(dev->eeprom) : dev->eeprom_words;

    if ((ee[0x0F] & 0xFF) != 0x73) {
        dev->eecsr &= ~EECSR_EEPR;
        return;
    }
    dev->eecsr |= EECSR_EEPR;

    for (int i = 0; i < 3; i++) {
        dev->regs[REG_PAR + i * 2]     = ee[i] & 0xFF;
        dev->regs[REG_PAR + i * 2 + 1] = ee[i] >> 8;
    }
    dev->pci_conf[0x2C] = ee[0x05] & 0xFF;
    dev->pci_conf[0x2D] = ee[0x05] >> 8;
    dev->pci_conf[0x2E] = ee[0x04] & 0xFF;
    dev->pci_conf[0x2F] = ee[0x04] >> 8;
    dev->regs[REG_PMCPORT] = ee[0x08] & 0xFF;
    dev->pci_conf[0x3E] = ee[0x0B] & 0xFF;
    dev->pci_conf[0x3F] = ee[0x0B] >> 8;
    dev->regs[REG_BCR0] = ee[0x0C] & 0xFF;
    dev->regs[REG_BCR1] = ee[0x0C] >> 8;
    dev->regs[REG_CFGA] = ee[0x0D] & 0xFF;
    dev->regs[REG_CFGB] = ee[0x0D] >> 8;
    dev->regs[REG_CFGC] = ee[0x0E] & 0xFF;
    dev->regs[REG_CFGD] = ee[0x0E] >> 8;
}

/* Soft timers (Rhine II/III). The data sheets name the resolution bits but not
   the unit without them: taken as milliseconds. */
static double
rhine_timer_period(rhine_t *dev, int n)
{
    uint16_t count = dev->regs[REG_TIMER0 + n * 2] | (dev->regs[REG_TIMER0 + n * 2 + 1] << 8);
    bool     us    = n ? (dev->regs[REG_MISCCR1] & MISCCR1_TM1US) : (dev->regs[REG_MISCCR0] & MISCCR0_TM0US);
    double   period;

    if (!count)
        count = 1;
    period = us ? (double) count : (double) count * 1000.0;

    return (period < 10.0) ? 10.0 : period;
}

static void
rhine_timer_update(rhine_t *dev, int n)
{
    pc_timer_t *timer = n ? &dev->timer1 : &dev->timer0;
    bool        on    = n ? (dev->regs[REG_MISCCR1] & MISCCR1_TM1EN) : (dev->regs[REG_MISCCR0] & MISCCR0_TM0EN);

    if (!dev->model->has_pm)
        return;

    if (on) {
        if (!timer_is_enabled(timer))
            timer_on_auto(timer, rhine_timer_period(dev, n));
    } else
        timer_disable(timer);
}

static void
rhine_timer0_cb(void *priv)
{
    rhine_t *dev = (rhine_t *) priv;

    /* Single shot */
    dev->regs[REG_MISCCR0] = (dev->regs[REG_MISCCR0] & ~MISCCR0_TM0EN) | MISCCR0_TM0SP;
    dev->misr |= MISR_TM0;
    rhine_update_irq(dev);
}

static void
rhine_timer1_cb(void *priv)
{
    rhine_t *dev = (rhine_t *) priv;

    /* Periodic */
    dev->misr |= MISR_TM1;
    rhine_update_irq(dev);
    if (dev->regs[REG_MISCCR1] & MISCCR1_TM1EN)
        timer_on_auto(&dev->timer1, rhine_timer_period(dev, 1));
}

/* Reset */
static void
rhine_soft_reset(rhine_t *dev)
{
    dev->cr0  = CR0_STOP;
    dev->cr1  = CR1_DPOLL;
    dev->isr  = 0;
    dev->imr  = 0;
    dev->misr = 0;
    dev->mimr = 0;
    dev->camc = 0;
    dev->regs[REG_TCR] = 0;
    dev->rx_stalled    = false;
    dev->tx_paused     = false;
    memset(dev->mib, 0, sizeof(dev->mib));
    rhine_mib_reset_read(dev);
    for (int q = 0; q < RHINE_TX_QUEUES; q++) {
        dev->tx_len[q]      = 0;
        dev->tx_in_frame[q] = false;
    }

    dev->regs[REG_MISCCR0] &= ~(MISCCR0_TM0EN | MISCCR0_TM0SP);
    dev->regs[REG_MISCCR1] &= ~MISCCR1_TM1EN;
    timer_disable(&dev->timer0);
    timer_disable(&dev->timer1);
    timer_disable(&dev->pause_timer);

    rhine_update_irq(dev);
}

static void
rhine_hard_reset(rhine_t *dev)
{
    memset(dev->regs, 0, sizeof(dev->regs));
    dev->rx_ptr = 0;
    memset(dev->tx_ptr, 0, sizeof(dev->tx_ptr));
    dev->regs[REG_MIICFG] = 0x01; /* the built-in PHY's address */
    dev->miiad   = 0x01;
    dev->miidata = 0;
    dev->eecsr   = 0;
    memset(dev->wol, 0, sizeof(dev->wol));
    memset(dev->pwrcsr, 0, sizeof(dev->pwrcsr));
    memset(dev->wol_status, 0, sizeof(dev->wol_status));
    memset(dev->wol_patterns, 0, sizeof(dev->wol_patterns));
    memset(dev->wol_masks, 0, sizeof(dev->wol_masks));
    dev->pwrcsr[0] = 0x10; /* PWCFG: WOL pin signalling = button */
    dev->mcam_mask = dev->vcam_mask = 0;
    dev->camadd    = 0;
    dev->regs[REG_FCR1] = 0xD8;

    rhine_phy_reset(dev);
    rhine_eeprom_load(dev);
    rhine_soft_reset(dev);
}

/* Receive filter */
static uint32_t
rhine_crc32_le(const uint8_t *buf, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < size; i++) {
        crc ^= buf[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }

    return ~crc;
}

/* Big-endian CRC (Linux's ether_crc), its top six bits index the 64-bit hash table */
static uint32_t
rhine_crc32_be(const uint8_t *buf, uint32_t size)
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

/* Returns the RSR bit the frame is accepted with, 0 if rejected. */
static uint16_t
rhine_rx_filter(const rhine_t *dev, const uint8_t *da)
{
    uint8_t rcr      = dev->regs[REG_RCR];
    bool    is_bcast = (da[0] & da[1] & da[2] & da[3] & da[4] & da[5]) == 0xFF;
    bool    is_mcast = (da[0] & 1) && !is_bcast;

    if (is_bcast)
        return ((rcr & (RCR_AB | RCR_PROM))) ? RSR_BAR : 0;

    if (is_mcast) {
        uint32_t bit;

        if (rcr & RCR_PROM)
            return RSR_MAR;
        if (!(rcr & RCR_AM))
            return 0;
        bit = rhine_crc32_be(da, 6) >> 26;
        if (dev->regs[REG_MAR + (bit >> 3)] & (1 << (bit & 7)))
            return RSR_MAR;
        if (dev->model->has_mgmt) {
            for (int i = 0; i < 32; i++) {
                if ((dev->mcam_mask & (1u << i)) && !memcmp(dev->mcam[i], da, 6))
                    return RSR_MAR;
            }
        }
        return 0;
    }

    if (rcr & RCR_PROM)
        return RSR_PHY;
    if (!memcmp(da, &dev->regs[REG_PAR], 6))
        return RSR_PHY;

    return 0;
}

/* Receive path */
static int
rhine_rx_frame(rhine_t *dev, const uint8_t *buf, int size, bool loopback)
{
    uint8_t  frame[RHINE_MAX_FRAME + 8];
    uint32_t descs[RHINE_RX_MAX_DESCS];
    uint32_t bufs[RHINE_RX_MAX_DESCS];
    uint32_t lens[RHINE_RX_MAX_DESCS];
    int      ndesc = 0;
    uint32_t len;
    uint32_t total;
    uint32_t room = 0;
    uint32_t addr;
    uint32_t available = 0;
    uint32_t crc;
    uint32_t off  = 0;
    uint16_t cls  = RSR_PHY;

    if (!rhine_rx_on(dev))
        return 1;
    if (!loopback && !dev->phy_link_up)
        return 1;
    if (size < 14)
        return 1;

    rhine_check_wol(dev, buf, size);
    if (!loopback && rhine_receive_pause(dev, buf, size))
        return 1;
    if (size > RHINE_MAX_FRAME) {
        rhine_mib_event(dev, MIB_RX_LONG);
        rhine_mib_event(dev, MIB_RX_ERROR);
    }

    if (dev->model->has_mgmt) {
        uint32_t count_addr = dev->rx_ptr;

        while (count_addr && available < RHINE_MAX_DESC_CHAIN) {
            uint32_t desc[4];

            dma_bm_read(count_addr, (uint8_t *) desc, 16, 4);
            if (!(le32_to_cpu(desc[0]) & DESC_OWN))
                break;
            available++;
            count_addr = le32_to_cpu(desc[3]);
        }
        dev->regs[REG_FCR0] = available > 255 ? 255 : (uint8_t) available;
    }

    len = (uint32_t) size;
    if (len > RHINE_MAX_FRAME - 4)
        len = RHINE_MAX_FRAME - 4;
    memcpy(frame, buf, len);
    if (len < 60) {
        memset(frame + len, 0, 60 - len);
        len = 60;
    }

    if (!loopback) {
        cls = rhine_rx_filter(dev, frame);
        via_rhine_log("VIA Rhine: RX %d bytes to %02X:%02X:%02X:%02X:%02X:%02X type %02X%02X: %s\n", size,
                      frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[12], frame[13],
                      cls ? "accepted" : "rejected");
        if (!cls) {
            rhine_mib_event(dev, MIB_RX_ERROR);
            return 1;
        }
    }

    crc = cpu_to_le32(rhine_crc32_le(frame, len));
    memcpy(frame + len, &crc, 4);
    total = len + 4;

    /* Find enough descriptors the driver gave us before writing anything. */
    addr = dev->rx_ptr;
    while (room < total) {
        uint32_t desc[4];

        if (!addr || (ndesc >= RHINE_RX_MAX_DESCS))
            break;
        dma_bm_read(addr, (uint8_t *) desc, 16, 4);
        if (!(le32_to_cpu(desc[0]) & DESC_OWN))
            break;
        descs[ndesc] = addr;
        bufs[ndesc]  = le32_to_cpu(desc[2]);
        lens[ndesc]  = le32_to_cpu(desc[1]) & 0x7FFF;
        if (lens[ndesc] > RHINE_MAX_FRAME)
            lens[ndesc] = RHINE_MAX_FRAME;
        room += lens[ndesc];
        ndesc++;
        addr = le32_to_cpu(desc[3]);
    }

    if (room < total) {
        /* Out of receive buffers: say so once and hold the frame until there are. */
        if (!dev->rx_stalled) {
            via_rhine_log("VIA Rhine: RX out of descriptors at %08X\n", dev->rx_ptr);
            dev->rx_stalled = true;
            rhine_mib_event(dev, MIB_RX_NOBUF);
            rhine_mib_event(dev, MIB_RX_MISSED);
            dev->isr |= ISR_RU | ISR_NORBF;
            rhine_update_irq(dev);
            if (!loopback && available <= rhine_pause_low_threshold(dev) &&
                rhine_flow_control_enabled(dev, MISCCR0_TXFC)) {
                uint16_t quanta = dev->regs[REG_PAUSE] | (dev->regs[REG_PAUSE + 1] << 8);

                if (quanta)
                    rhine_send_pause(dev, quanta);
            }
        }
        return loopback ? 1 : 0;
    }

    for (int i = 0; i < ndesc; i++) {
        uint32_t chunk = total - off;
        uint32_t status;

        if (chunk > lens[i])
            chunk = lens[i];
        if (chunk)
            dma_bm_write(bufs[i], frame + off, chunk, 4);
        off += chunk;

        status = RSR_RXOK | RSR_CHN | cls | (total << 16);
        if (i == 0)
            status |= RSR_STP;
        if (i == (ndesc - 1))
            status |= RSR_EDP;
        status = cpu_to_le32(status);
        dma_bm_write(descs[i], (uint8_t *) &status, 4, 4);

        if (dev->model->has_mgmt) {
            uint32_t rdes1;

            /* No checksum offload status nor VLAN tag (PQSTS, IPKT) */
            dma_bm_read(descs[i] + 4, (uint8_t *) &rdes1, 4, 4);
            rdes1 = cpu_to_le32(le32_to_cpu(rdes1) & 0x0000FFFF);
            dma_bm_write(descs[i] + 4, (uint8_t *) &rdes1, 4, 4);
        }
    }
    dev->rx_ptr = addr;

    if (dev->rx_stalled && available >= rhine_pause_high_threshold(dev) &&
        !loopback && rhine_flow_control_enabled(dev, MISCCR0_TXFC))
        rhine_send_pause(dev, 0);
    dev->rx_stalled = false;
    dev->isr |= ISR_PRX;
    rhine_update_irq(dev);

    return 1;
}

static int
rhine_receive(void *priv, uint8_t *buf, int size)
{
    rhine_t *dev = (rhine_t *) priv;

    return rhine_rx_frame(dev, buf, size, false);
}

/* Transmit path */
static uint16_t
rhine_csum(const uint8_t *p, uint32_t len, uint32_t sum)
{
    while (len > 1) {
        sum += (p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len)
        sum += p[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t) ~sum;
}

/* VT6105M checksum offload: IPv4 header, then TCP or UDP. */
static void
rhine_tx_checksums(uint8_t *pkt, uint32_t len, uint8_t tcr)
{
    uint32_t l3 = 14;
    uint32_t ihl;
    uint32_t ip_len;
    uint32_t l4_len;
    uint32_t sum;
    uint16_t c;
    uint8_t *ip;
    uint8_t *l4;

    if ((len >= 18) && (pkt[12] == 0x81) && (pkt[13] == 0x00))
        l3 = 18;
    if ((len < l3 + 20) || (pkt[l3 - 2] != 0x08) || (pkt[l3 - 1] != 0x00))
        return;

    ip  = pkt + l3;
    ihl = (ip[0] & 0x0F) * 4;
    if (((ip[0] >> 4) != 4) || (ihl < 20) || (len < l3 + ihl))
        return;
    ip_len = (ip[2] << 8) | ip[3];
    if ((ip_len < ihl) || (ip_len > len - l3))
        return;

    if (tcr & TCR_IPCK) {
        ip[10] = ip[11] = 0;
        c      = rhine_csum(ip, ihl, 0);
        ip[10] = c >> 8;
        ip[11] = c & 0xFF;
    }

    l4     = ip + ihl;
    l4_len = ip_len - ihl;
    /* Fragments carry no transport header to sum. */
    if ((((ip[6] << 8) | ip[7]) & 0x3FFF) != 0)
        return;

    sum = ((ip[12] << 8) | ip[13]) + ((ip[14] << 8) | ip[15]) +
          ((ip[16] << 8) | ip[17]) + ((ip[18] << 8) | ip[19]) + ip[9] + l4_len;

    if ((tcr & TCR_TCPCK) && (ip[9] == 6) && (l4_len >= 20)) {
        l4[16] = l4[17] = 0;
        c      = rhine_csum(l4, l4_len, sum);
        l4[16] = c >> 8;
        l4[17] = c & 0xFF;
    } else if ((tcr & TCR_UDPCK) && (ip[9] == 17) && (l4_len >= 8)) {
        l4[6] = l4[7] = 0;
        c     = rhine_csum(l4, l4_len, sum);
        if (!c)
            c = 0xFFFF;
        l4[6] = c >> 8;
        l4[7] = c & 0xFF;
    }
}

static void
rhine_tx_frame(rhine_t *dev, int q)
{
    uint8_t *pkt = dev->tx_buf[q];
    uint32_t len = dev->tx_len[q];
    uint8_t  tcr = dev->tx_tcr[q];

    if (len < 14)
        return;

    if (dev->model->has_mgmt) {
        if ((tcr & TCR_TAG) && (len + 4 <= RHINE_MAX_FRAME)) {
            /* 802.1Q tag from TDES0: VID in 27-16, priority in 30-28 */
            uint16_t tci = ((dev->tx_tdes0[q] >> 16) & 0x0FFF) | (((dev->tx_tdes0[q] >> 28) & 7) << 13);

            memmove(pkt + 16, pkt + 12, len - 12);
            pkt[12] = 0x81;
            pkt[13] = 0x00;
            pkt[14] = tci >> 8;
            pkt[15] = tci & 0xFF;
            len += 4;
        }
        if (tcr & (TCR_IPCK | TCR_TCPCK | TCR_UDPCK))
            rhine_tx_checksums(pkt, len, tcr);
    }

    if (len < 60) {
        memset(pkt + len, 0, 60 - len);
        len = 60;
    }

    via_rhine_log("VIA Rhine: TX %u bytes to %02X:%02X:%02X:%02X:%02X:%02X type %02X%02X\n", len,
                  pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5], pkt[12], pkt[13]);

    if ((dev->regs[REG_TCR] & TCR_LB) || (dev->mii_regs[0] & MII_BMCR_LOOPBACK))
        rhine_rx_frame(dev, pkt, (int) len, true);
    else if (dev->nic && dev->phy_link_up)
        network_tx(dev->nic, pkt, (int) len);
}

static void
rhine_tx_queue(rhine_t *dev, int q)
{
    uint32_t chain = 0;
    bool     irq   = false;

    if (dev->tx_paused)
        return;

    while (dev->tx_ptr[q] && (++chain <= RHINE_MAX_DESC_CHAIN)) {
        uint32_t desc[4];
        uint32_t tdes0;
        uint32_t len;
        uint8_t  tcr;

        dma_bm_read(dev->tx_ptr[q], (uint8_t *) desc, 16, 4);
        tdes0 = le32_to_cpu(desc[0]);
        /* Only the first descriptor of a frame is handed over with OWN: the chip takes
           the fragments after it up to the one with EDP as they are (VIA's drivers
           set OWN on the first one alone). */
        if (!(tdes0 & DESC_OWN) && !dev->tx_in_frame[q])
            break;

        tcr = (le32_to_cpu(desc[1]) >> 16) & 0xFF;
        len = le32_to_cpu(desc[1]) & TDES1_BUFLEN;

        if ((tcr & TCR_STP) || !dev->tx_in_frame[q]) {
            dev->tx_len[q]      = 0;
            dev->tx_tcr[q]      = tcr;
            dev->tx_tdes0[q]    = tdes0;
            dev->tx_in_frame[q] = true;
        }
        dev->tx_tcr[q] |= tcr & TCR_IC;

        if (len > RHINE_MAX_FRAME - dev->tx_len[q])
            len = RHINE_MAX_FRAME - dev->tx_len[q];
        if (len) {
            dma_bm_read(le32_to_cpu(desc[2]), dev->tx_buf[q] + dev->tx_len[q], len, 4);
            dev->tx_len[q] += len;
        }

        if (tcr & TCR_EDP) {
            rhine_tx_frame(dev, q);
            if (dev->tx_tcr[q] & TCR_IC)
                irq = true;
            dev->tx_len[q]      = 0;
            dev->tx_in_frame[q] = false;
        }

        /* Done, no errors: TSR0/TSR1 clear, OWN back to the driver. */
        if (tdes0 & DESC_OWN) {
            tdes0 = cpu_to_le32(tdes0 & 0x7FFF0000);
            dma_bm_write(dev->tx_ptr[q], (uint8_t *) &tdes0, 4, 4);
        }

        dev->tx_ptr[q] = le32_to_cpu(desc[3]);
    }

    if (irq) {
        dev->isr |= ISR_PTX;
        rhine_update_irq(dev);
    }
}

static void
rhine_tx_run(rhine_t *dev)
{
    if (!rhine_tx_on(dev))
        return;

    for (int q = 0; q < (dev->model->has_mgmt ? RHINE_TX_QUEUES : 1); q++)
        rhine_tx_queue(dev, q);
}

/* Register access */
static void
rhine_cr0_write(rhine_t *dev, uint8_t val)
{
    /* STOP wins; STRT stays set until STOP or a reset, RXON/TXON are as written. */
    if (val & CR0_STOP)
        dev->cr0 = CR0_STOP;
    else
        dev->cr0 = (dev->cr0 & CR0_STRT) | (val & (CR0_TXON | CR0_RXON | CR0_STRT));

    if (val & CR0_RDMD)
        dev->rx_stalled = false; /* the next frame looks at the ring again */
    if (rhine_tx_on(dev) && !dev->tx_paused)
        rhine_tx_run(dev);
}

static void
rhine_cr1_write(rhine_t *dev, uint8_t val)
{
    if (val & CR1_SFRST) {
        rhine_soft_reset(dev); /* completes at once */
        return;
    }

    dev->cr1 = val & ~(CR1_SFRST | CR1_RDMD1 | CR1_TDMD1);
    if (val & CR1_RDMD1)
        dev->rx_stalled = false;
    if (val & CR1_TDMD1)
        rhine_tx_run(dev);
}

static void
rhine_eecsr_write(rhine_t *dev, uint8_t val)
{
    dev->eecsr = (dev->eecsr & EECSR_EEPR) | (val & (EECSR_DPM | EECSR_CS | EECSR_SK | EECSR_DI));

    if ((val & EECSR_DPM) && (dev->regs[REG_CFGA] & CFGA_EELOAD) && dev->eeprom)
        nmc93cxx_eeprom_write(dev->eeprom, !!(val & EECSR_CS), !!(val & EECSR_SK), !!(val & EECSR_DI));

    if (val & EECSR_AUTOLD) {
        via_rhine_log("VIA Rhine: EEPROM reload\n");
        rhine_eeprom_load(dev); /* completes at once: AUTOLD reads back 0 */
    }
}

static uint8_t
rhine_read8(rhine_t *dev, uint32_t off)
{
    off &= 0xFF;
    if (off >= dev->model->io_size)
        return 0xFF;

    switch (off) {
        case REG_CR0:
            return dev->cr0;
        case REG_CR1:
            return dev->cr1;
        case REG_TQWK:
            return 0x00;
        case REG_ISR0:
            return dev->isr & 0xFF;
        case REG_ISR1:
            return dev->isr >> 8;
        case REG_IMR0:
            return dev->imr & 0xFF;
        case REG_IMR1:
            return dev->imr >> 8;
        case 0x10 ... 0x17:
            if (dev->model->has_mgmt && (dev->camc & CAMC_CAMEN))
                return dev->cam_data[off - 0x10];
            return dev->regs[off];
        case 0x18 ... 0x1B:
            return dev->rx_ptr >> ((off & 3) * 8);
        case 0x1C ... 0x3B:
            if (!dev->model->has_mgmt && (off >= 0x20))
                return 0x00;
            return dev->tx_ptr[(off - 0x1C) >> 2] >> ((off & 3) * 8);
        case REG_MIISR:
            return rhine_miisr(dev);
        case REG_MIICR:
            return dev->regs[REG_MIICR];
        case REG_MIIAD:
            return dev->miiad | MIIAD_MDONE | ((dev->regs[REG_MIICR] & MIICR_MAUTO) ? 0 : MIIAD_MIDLE);
        case REG_MIIDATA:
            return dev->miidata & 0xFF;
        case REG_MIIDATA + 1:
            return dev->miidata >> 8;
        case REG_MIBDAT:
        case REG_MIBDAT + 1:
            return rhine_mib_read(dev);
        case REG_EECSR:
            {
                uint8_t ret = dev->eecsr;

                if ((ret & EECSR_DPM) && dev->eeprom && nmc93cxx_eeprom_read(dev->eeprom))
                    ret |= EECSR_DO;
                return ret;
            }
        case 0x7C ... 0x7F:
            return 0x00; /* tally counters: no errors */
        default:
            break;
    }

    if (!dev->model->has_pm) {
        /* Rhine I: nothing past the configuration registers */
        return (off >= 0x7C) ? 0x00 : dev->regs[off];
    }

    switch (off) {
        case REG_MISR:
            return dev->misr;
        case REG_MIMR:
            return dev->mimr;
        case 0x88 ... 0x8B:
            if (!dev->model->has_mgmt)
                return 0x00;
            return ((dev->camc & CAMC_VCAMSL) ? dev->vcam_mask : dev->mcam_mask) >> ((off & 3) * 8);
        case REG_BPCMD:
            return 0x80 | (dev->regs[off] & 0x03); /* flash commands complete */
        case REG_CAMC:
            return dev->model->has_mgmt ? dev->camc : 0x00;
        case REG_CAMADD:
            return dev->model->has_mgmt ? dev->camadd : 0x00;
        case 0xA0:
        case 0xA4:
            return dev->wol[0];
        case 0xA1:
        case 0xA5:
            return dev->pwrcsr[0];
        case 0xA3:
        case 0xA7:
            return dev->wol[1];
        case 0xA8:
        case 0xA9:
            return dev->wol_status[off - 0xA8];
        case 0xAC:
        case 0xAD:
            return dev->wol_status[off - 0xAC];
        case 0xAA:
        case 0xAB:
        case 0xAE:
        case 0xAF:
            return 0x00;
        case 0xB0 ... 0xFF:
            if (!dev->model->has_mgmt)
                return 0x00;
            if (off < 0xC0)
                return dev->wol_patterns[off - 0xB0];
            return dev->wol_masks[off - 0xC0];
        default:
            return dev->regs[off];
    }
}

static void
rhine_cam_command(rhine_t *dev, uint8_t val)
{
    int idx = dev->camadd & 0x1F;

    if (!(val & CAMC_CAMEN))
        return;

    if (val & CAMC_CAMWR) {
        if (val & CAMC_VCAMSL)
            dev->vcam[idx] = dev->cam_data[6] | (dev->cam_data[7] << 8);
        else
            memcpy(dev->mcam[idx], dev->cam_data, 6);
    }
    if (val & CAMC_CAMRD) {
        if (val & CAMC_VCAMSL) {
            dev->cam_data[6] = dev->vcam[idx] & 0xFF;
            dev->cam_data[7] = dev->vcam[idx] >> 8;
        } else
            memcpy(dev->cam_data, dev->mcam[idx], 6);
    }
}

static void
rhine_write8(rhine_t *dev, uint32_t off, uint8_t val)
{
    off &= 0xFF;
    if (off >= dev->model->io_size)
        return;

    if ((off != REG_ISR0) && (off != REG_ISR1) && (off != REG_IMR0) && (off != REG_IMR1))
        via_rhine_log("VIA Rhine: [%02X] <- %02X\n", off, val);

    switch (off) {
        case REG_CR0:
            rhine_cr0_write(dev, val);
            return;
        case REG_CR1:
            rhine_cr1_write(dev, val);
            return;
        case REG_TQWK:
            if (dev->model->has_mgmt && val)
                rhine_tx_run(dev);
            return;
        case REG_ISR0:
            dev->isr &= ~val; /* write 1 to clear */
            rhine_update_irq(dev);
            return;
        case REG_ISR1:
            dev->isr &= ~(val << 8);
            rhine_update_irq(dev);
            return;
        case REG_IMR0:
            dev->imr = (dev->imr & 0xFF00) | val;
            rhine_update_irq(dev);
            return;
        case REG_IMR1:
            dev->imr = (dev->imr & 0x00FF) | (val << 8);
            rhine_update_irq(dev);
            return;
        case 0x10 ... 0x17:
            if (dev->model->has_mgmt && (dev->camc & CAMC_CAMEN))
                dev->cam_data[off - 0x10] = val;
            else
                dev->regs[off] = val;
            return;
        case 0x18 ... 0x1B:
            {
                int shift = (off & 3) * 8;

                dev->rx_ptr     = (dev->rx_ptr & ~(0xFFu << shift)) | ((uint32_t) val << shift);
                dev->rx_stalled = false;
                return;
            }
        case 0x1C ... 0x3B:
            {
                int q     = (off - 0x1C) >> 2;
                int shift = (off & 3) * 8;

                if (!dev->model->has_mgmt && q)
                    return;
                dev->tx_ptr[q] = (dev->tx_ptr[q] & ~(0xFFu << shift)) | ((uint32_t) val << shift);
                dev->tx_len[q]      = 0;
                dev->tx_in_frame[q] = false;
                return;
            }
        case REG_MIISR:
            /* Read-only but for the software PHY reset (bit 7, completes at once) */
            if (val & 0x80) {
                rhine_phy_reset(dev);
                rhine_update_phy_state(dev, true);
            }
            return;
        case REG_MIICR:
            dev->regs[REG_MIICR] = val & ~(MIICR_RCMD | MIICR_WCMD);
            if (val & MIICR_RCMD)
                dev->miidata = rhine_phy_read(dev, dev->regs[REG_MIICFG] & 0x1F, dev->miiad & 0x1F);
            else if (val & MIICR_WCMD)
                rhine_phy_write(dev, dev->regs[REG_MIICFG] & 0x1F, dev->miiad & 0x1F, dev->miidata);
            return;
        case REG_MIIAD:
            dev->miiad = val & 0x5F;
            return;
        case REG_MIIDATA:
            dev->miidata = (dev->miidata & 0xFF00) | val;
            return;
        case REG_MIIDATA + 1:
            dev->miidata = (dev->miidata & 0x00FF) | (val << 8);
            return;
        case REG_EECSR:
            rhine_eecsr_write(dev, val);
            return;
        case 0x7C ... 0x7F:
            return;
        default:
            break;
    }

    if (!dev->model->has_pm) {
        if (off < 0x7C)
            dev->regs[off] = val;
        return;
    }

    switch (off) {
        case REG_MISCCR0:
            /* TM0SP: write 0 to clear */
            dev->regs[off] = (val & ~MISCCR0_TM0SP) | (dev->regs[off] & val & MISCCR0_TM0SP);
            rhine_timer_update(dev, 0);
            break;
        case REG_MISCCR1:
            if (val & MISCCR1_FORSRST) {
                rhine_soft_reset(dev);
                val &= ~MISCCR1_FORSRST;
            }
            dev->regs[off] = val;
            rhine_timer_update(dev, 1);
            break;
        case REG_PMCPORT:
            break; /* EEPROM shadow, read-only */
        case REG_MISR:
            dev->misr &= ~val;
            rhine_update_irq(dev);
            break;
        case REG_MIBCR:
            dev->regs[REG_MIBCR] = val & 0x7F;
            if (val & 0x80)
                rhine_mib_reset_read(dev);
            break;
        case REG_MIBDAT:
        case REG_MIBDAT + 1:
            break;
        case REG_MIMR:
            dev->mimr = val;
            rhine_update_irq(dev);
            break;
        case 0x88 ... 0x8B:
            if (dev->model->has_mgmt && (dev->camc & CAMC_CAMEN)) {
                uint32_t *mask  = (dev->camc & CAMC_VCAMSL) ? &dev->vcam_mask : &dev->mcam_mask;
                int       shift = (off & 3) * 8;

                *mask = (*mask & ~(0xFFu << shift)) | ((uint32_t) val << shift);
            }
            break;
        case REG_CAMC:
            if (dev->model->has_mgmt) {
                rhine_cam_command(dev, val);
                dev->camc = val & (CAMC_VCAMSL | CAMC_CAMEN); /* read and write self-clear */
            }
            break;
        case REG_CAMADD:
            if (dev->model->has_mgmt)
                dev->camadd = val;
            else
                dev->regs[off] = val;
            break;
        case REG_TIMER0:
        case REG_TIMER0 + 1:
        case REG_TIMER1:
        case REG_TIMER1 + 1:
            dev->regs[off] = val;
            break;
        case 0xA0:
            if (dev->model->has_mgmt)
                dev->wol[0] |= val;
            break;
        case 0xA1:
            if (dev->model->has_mgmt)
                dev->pwrcsr[0] |= val & 0xB0;
            break;
        case 0xA3:
            if (dev->model->has_mgmt)
                dev->wol[1] |= val & 0xF0;
            break;
        case 0xA4:
            if (dev->model->has_mgmt)
                dev->wol[0] &= ~val;
            break;
        case 0xA5:
            if (dev->model->has_mgmt)
                dev->pwrcsr[0] &= ~(val & 0xB0);
            break;
        case 0xA7:
            if (dev->model->has_mgmt)
                dev->wol[1] &= ~(val & 0xF0);
            break;
        case 0xA8:
        case 0xA9:
            if (dev->model->has_mgmt)
                dev->wol_status[off - 0xA8] |= val;
            break;
        case 0xAC:
        case 0xAD:
            if (dev->model->has_mgmt)
                dev->wol_status[off - 0xAC] &= ~val;
            break;
        case 0xB0 ... 0xBF:
            if (dev->model->has_mgmt)
                dev->wol_patterns[off - 0xB0] = val;
            break;
        case 0xC0 ... 0xFF:
            if (dev->model->has_mgmt)
                dev->wol_masks[off - 0xC0] = val;
            break;
        case 0xAA:
        case 0xAB:
        case 0xAE:
        case 0xAF:
            break;
        default:
            dev->regs[off] = val;
            break;
    }
}

static uint8_t
rhine_mmio_readb(uint32_t addr, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;

    return rhine_read8(dev, addr - dev->mmio_base);
}

static uint16_t
rhine_mmio_readw(uint32_t addr, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;
    uint32_t off = addr - dev->mmio_base;

    return rhine_read8(dev, off) | (rhine_read8(dev, off + 1) << 8);
}

static uint32_t
rhine_mmio_readl(uint32_t addr, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;
    uint32_t off = addr - dev->mmio_base;

    return rhine_read8(dev, off) | (rhine_read8(dev, off + 1) << 8) |
           (rhine_read8(dev, off + 2) << 16) | ((uint32_t) rhine_read8(dev, off + 3) << 24);
}

static void
rhine_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;

    rhine_write8(dev, addr - dev->mmio_base, val);
}

static void
rhine_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;
    uint32_t off = addr - dev->mmio_base;

    rhine_write8(dev, off, val & 0xFF);
    rhine_write8(dev, off + 1, val >> 8);
}

static void
rhine_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;
    uint32_t off = addr - dev->mmio_base;

    for (int i = 0; i < 4; i++)
        rhine_write8(dev, off + i, (val >> (i * 8)) & 0xFF);
}

static uint8_t
rhine_io_readb(uint16_t port, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;

    return rhine_read8(dev, port - dev->io_base);
}

static uint16_t
rhine_io_readw(uint16_t port, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;
    uint32_t off = port - dev->io_base;

    return rhine_read8(dev, off) | (rhine_read8(dev, off + 1) << 8);
}

static uint32_t
rhine_io_readl(uint16_t port, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;
    uint32_t off = port - dev->io_base;

    return rhine_read8(dev, off) | (rhine_read8(dev, off + 1) << 8) |
           (rhine_read8(dev, off + 2) << 16) | ((uint32_t) rhine_read8(dev, off + 3) << 24);
}

static void
rhine_io_writeb(uint16_t port, uint8_t val, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;

    rhine_write8(dev, port - dev->io_base, val);
}

static void
rhine_io_writew(uint16_t port, uint16_t val, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;
    uint32_t off = port - dev->io_base;

    rhine_write8(dev, off, val & 0xFF);
    rhine_write8(dev, off + 1, val >> 8);
}

static void
rhine_io_writel(uint16_t port, uint32_t val, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;
    uint32_t off = port - dev->io_base;

    for (int i = 0; i < 4; i++)
        rhine_write8(dev, off + i, (val >> (i * 8)) & 0xFF);
}

static uint32_t
rhine_bar_mask(const rhine_t *dev)
{
    return ~((uint32_t) dev->model->io_size - 1);
}

static void
rhine_io_update(rhine_t *dev)
{
    uint32_t size = dev->model->io_size;

    if (dev->io_mapped) {
        io_removehandler((uint16_t) dev->io_base, size,
                         rhine_io_readb, rhine_io_readw, rhine_io_readl,
                         rhine_io_writeb, rhine_io_writew, rhine_io_writel, dev);
        dev->io_mapped = false;
    }

    dev->io_base = (dev->pci_conf[0x10] | (dev->pci_conf[0x11] << 8) | (dev->pci_conf[0x12] << 16) |
                    ((uint32_t) dev->pci_conf[0x13] << 24)) & rhine_bar_mask(dev);
    if ((dev->pci_conf[0x04] & PCI_COMMAND_IO) && dev->io_base && (dev->io_base <= (0x10000 - size))) {
        io_sethandler((uint16_t) dev->io_base, size,
                      rhine_io_readb, rhine_io_readw, rhine_io_readl,
                      rhine_io_writeb, rhine_io_writew, rhine_io_writel, dev);
        dev->io_mapped = true;
    }
}

static void
rhine_mmio_update(rhine_t *dev)
{
    dev->mmio_base = (dev->pci_conf[0x14] | (dev->pci_conf[0x15] << 8) | (dev->pci_conf[0x16] << 16) |
                      ((uint32_t) dev->pci_conf[0x17] << 24)) & rhine_bar_mask(dev);
    mem_mapping_disable(&dev->mmio_mapping);
    if ((dev->pci_conf[0x04] & PCI_COMMAND_MEM) && dev->mmio_base)
        mem_mapping_set_addr(&dev->mmio_mapping, dev->mmio_base, dev->model->io_size);
}

/* PCI configuration space */
static uint8_t
rhine_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    const rhine_t *dev = (const rhine_t *) priv;

    if (func)
        return 0xFF;

    return dev->pci_conf[addr & 0xFF];
}

static void
rhine_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    rhine_t *dev = (rhine_t *) priv;

    if (func)
        return;

    addr &= 0xFF;
    switch (addr) {
        case 0x04:
            dev->pci_conf[0x04] = val & 0x07;
            rhine_io_update(dev);
            rhine_mmio_update(dev);
            break;
        case 0x0C:
        case 0x0D:
        case 0x3C:
            dev->pci_conf[addr] = val;
            break;
        case 0x10:
            dev->pci_conf[0x10] = (val & rhine_bar_mask(dev)) | 0x01;
            rhine_io_update(dev);
            break;
        case 0x11 ... 0x13:
            dev->pci_conf[addr] = val;
            rhine_io_update(dev);
            break;
        case 0x14:
            dev->pci_conf[0x14] = val & rhine_bar_mask(dev);
            rhine_mmio_update(dev);
            break;
        case 0x15 ... 0x17:
            dev->pci_conf[addr] = val;
            rhine_mmio_update(dev);
            break;
        case 0x44:
            if (dev->model->has_pm)
                dev->pci_conf[0x44] = val & 0x03; /* power state */
            else
                dev->pci_conf[addr] = val;
            break;
        case 0x45:
            if (dev->model->has_pm) {
                if (val & 0x80)
                    dev->pci_conf[0x45] &= ~0x80; /* PME_Status is write-one-to-clear */
                dev->pci_conf[0x45] = (dev->pci_conf[0x45] & 0x80) | (val & 0x01); /* PME_En */
            }
            else
                dev->pci_conf[addr] = val;
            break;
        default:
            /* The chip's own registers past the header (and past the capability) */
            if (addr >= (dev->model->has_pm ? 0x48 : 0x40))
                dev->pci_conf[addr] = val;
            break;
    }
}

static void
rhine_pci_reset(rhine_t *dev)
{
    const rhine_model_t *m = dev->model;

    memset(dev->pci_conf, 0, sizeof(dev->pci_conf));
    dev->pci_conf[0x00] = 0x06;
    dev->pci_conf[0x01] = 0x11;
    dev->pci_conf[0x02] = m->device_id & 0xFF;
    dev->pci_conf[0x03] = m->device_id >> 8;
    dev->pci_conf[0x06] = m->has_pm ? 0x90 : 0x80; /* fast back-to-back, capability list */
    dev->pci_conf[0x07] = 0x02;                    /* DEVSEL medium */
    dev->pci_conf[0x08] = m->revision;
    dev->pci_conf[0x0B] = 0x02; /* network controller, Ethernet */
    dev->pci_conf[0x10] = 0x01;
    dev->pci_conf[0x3D] = PCI_INTA;
    dev->pci_conf[0x3E] = 0x03;
    dev->pci_conf[0x3F] = 0x08;
    if (m->has_pm) {
        dev->pci_conf[0x34] = 0x40;
        dev->pci_conf[0x40] = 0x01; /* power management, last capability */
        dev->pci_conf[0x42] = 0x02; /* version 1.1, D1/D2, PME from D0-D3hot */
        dev->pci_conf[0x43] = 0x7E;
    }
}

static int
rhine_set_link_status(void *priv, uint32_t link_state)
{
    rhine_t *dev = (rhine_t *) priv;

    dev->net_link_up = !(link_state & NET_LINK_DOWN);
    rhine_update_phy_state(dev, true);

    return 0;
}

static void
rhine_reset(void *priv)
{
    rhine_t *dev = (rhine_t *) priv;

    rhine_pci_reset(dev);
    rhine_io_update(dev);
    rhine_mmio_update(dev);
    rhine_hard_reset(dev);
}

static void *
rhine_init(const device_t *info)
{
    rhine_t                 *dev = calloc(1, sizeof(rhine_t));
    nmc93cxx_eeprom_params_t eeprom_params;
    char                     eeprom_filename[64];
    int                      inst = device_get_instance();
    int                      mac;
    uint16_t                *ee;

    dev->model = &rhine_models[info->local];

    /* VIA's OUI */
    dev->mac[0] = 0x00;
    dev->mac[1] = 0x40;
    dev->mac[2] = 0x63;
    mac         = device_get_config_mac("mac", -1);
    if (mac < 0) {
        dev->mac[3] = random_generate();
        dev->mac[4] = random_generate();
        dev->mac[5] = random_generate();
        device_set_config_mac("mac", (dev->mac[3] << 16) | (dev->mac[4] << 8) | dev->mac[5]);
    } else {
        dev->mac[3] = (mac >> 16) & 0xFF;
        dev->mac[4] = (mac >> 8) & 0xFF;
        dev->mac[5] = mac & 0xFF;
    }

    rhine_build_eeprom(dev);

    memset(&eeprom_params, 0, sizeof(eeprom_params));
    snprintf(eeprom_filename, sizeof(eeprom_filename), "via_rhine_%d.nvr", inst);
    eeprom_params.type            = NMC_93C46_x16_64;
    eeprom_params.filename        = eeprom_filename;
    eeprom_params.default_content = dev->eeprom_words;
    dev->eeprom = device_add_inst_params(&nmc93cxx_device, inst, &eeprom_params);
    if (dev->eeprom) {
        /* The configured MAC address wins over a saved EEPROM's. */
        ee = (uint16_t *) nmc93cxx_eeprom_data(dev->eeprom);
        for (int i = 0; i < 3; i++)
            ee[i] = dev->eeprom_words[i];
        ee[0x0F] = dev->eeprom_words[0x0F];
    }

    timer_add(&dev->timer0, rhine_timer0_cb, dev, 0);
    timer_add(&dev->timer1, rhine_timer1_cb, dev, 0);
    timer_add(&dev->pause_timer, rhine_pause_timer_cb, dev, 0);

    mem_mapping_add(&dev->mmio_mapping, 0, 0,
                    rhine_mmio_readb, rhine_mmio_readw, rhine_mmio_readl,
                    rhine_mmio_writeb, rhine_mmio_writew, rhine_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->mmio_mapping);

    dev->net_link_up = true;
    rhine_pci_reset(dev);
    rhine_hard_reset(dev);

    dev->nic = network_attach(dev, dev->mac, rhine_receive, rhine_set_link_status);
    pci_add_card(PCI_ADD_NORMAL, rhine_pci_read, rhine_pci_write, dev, &dev->pci_slot);

    rhine_update_phy_state(dev, false);

    via_rhine_log("VIA Rhine %s: MAC %02X:%02X:%02X:%02X:%02X:%02X\n", dev->model->name,
                  dev->mac[0], dev->mac[1], dev->mac[2], dev->mac[3], dev->mac[4], dev->mac[5]);

    return dev;
}

static void
rhine_close(void *priv)
{
    rhine_t *dev = (rhine_t *) priv;

    if (dev->io_mapped)
        io_removehandler((uint16_t) dev->io_base, dev->model->io_size,
                         rhine_io_readb, rhine_io_readw, rhine_io_readl,
                         rhine_io_writeb, rhine_io_writew, rhine_io_writel, dev);
    mem_mapping_disable(&dev->mmio_mapping);
    timer_disable(&dev->timer0);
    timer_disable(&dev->timer1);
    timer_disable(&dev->pause_timer);
    free(dev);
}

static const device_config_t rhine_config[] = {
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

const device_t via_vt86c100a_device = {
    .name          = "VIA VT86C100A Rhine",
    .internal_name = "via_vt86c100a",
    .flags         = DEVICE_PCI,
    .local         = RHINE_VT86C100A,
    .init          = rhine_init,
    .close         = rhine_close,
    .reset         = rhine_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = rhine_config
};

const device_t via_vt6102_device = {
    .name          = "VIA VT6102 Rhine II",
    .internal_name = "via_vt6102",
    .flags         = DEVICE_PCI,
    .local         = RHINE_VT6102,
    .init          = rhine_init,
    .close         = rhine_close,
    .reset         = rhine_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = rhine_config
};

const device_t via_vt6105_device = {
    .name          = "VIA VT6105 Rhine III",
    .internal_name = "via_vt6105",
    .flags         = DEVICE_PCI,
    .local         = RHINE_VT6105,
    .init          = rhine_init,
    .close         = rhine_close,
    .reset         = rhine_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = rhine_config
};

const device_t via_vt6105m_device = {
    .name          = "VIA VT6105M Rhine III Management Adapter",
    .internal_name = "via_vt6105m",
    .flags         = DEVICE_PCI,
    .local         = RHINE_VT6105M,
    .init          = rhine_init,
    .close         = rhine_close,
    .reset         = rhine_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = rhine_config
};
