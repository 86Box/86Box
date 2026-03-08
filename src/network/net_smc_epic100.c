/*
 * 86Box    A hypervisor and target IBM PC system emulator that enables
 *          the recreation and use of old computer hardware and software.
 *
 *          Emulation of the SMC EPIC/100 (83C170) Fast Ethernet PCI NIC.
 *          Found on the SMC EtherPower II 9432 card.
 *
 * Authors: Plamen Dossev
 *
 *          Copyright 2025 Plamen Dossev.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

/* PCI IDs */
#define EPIC_VENDOR_ID   0x10B8
#define EPIC_DEVICE_ID   0x0005  /* 83C170 EPIC/100 */

/* Register offsets */
#define REG_COMMAND      0x00
#define REG_INTSTAT      0x04
#define REG_INTMASK      0x08
#define REG_GENCTL       0x0C
#define REG_NVCTL        0x10
#define REG_EECTL        0x14
#define REG_PCIBRSTCNT   0x18
#define REG_TEST1        0x1C
#define REG_CRCCNT       0x20
#define REG_ALICNT       0x24
#define REG_MPCNT        0x28
#define REG_MIICTRL      0x30
#define REG_MIIDATA      0x34
#define REG_MIICFG       0x38
#define REG_TIMER0       0x3C
#define REG_LAN0         0x40
#define REG_LAN1         0x44
#define REG_LAN2         0x48
#define REG_MC0          0x50
#define REG_MC1          0x54
#define REG_MC2          0x58
#define REG_MC3          0x5C
#define REG_RXCON        0x60
#define REG_TXCON        0x70
#define REG_TXSTAT       0x74
#define REG_TIMER1       0x7C
#define REG_PRCDAR       0x84
#define REG_RXSTAT       0xA4
#define REG_EARLYRX      0xB0
#define REG_PTCDAR       0xC4
#define REG_TXTHRESH     0xDC

/* Command register bits */
#define CMD_STOP_RX      0x01
#define CMD_START_RX     0x02
#define CMD_TXQUEUED     0x04
#define CMD_RXQUEUED     0x08
#define CMD_STOP_TDMA    0x20
#define CMD_STOP_RDMA    0x40
#define CMD_RESTART_TX   0x80

/* Interrupt status bits */
#define INT_RX_DONE      0x0001
#define INT_RX_HEADER    0x0002
#define INT_RX_FULL      0x0004
#define INT_RX_OVERFLOW  0x0008
#define INT_RX_ERROR     0x0010
#define INT_TX_DONE      0x0020
#define INT_TX_EMPTY     0x0080
#define INT_TX_UNDERRUN  0x0100
#define INT_CNT_FULL     0x0200
#define INT_RX_EARLY     0x0400
#define INT_RX_STARTED   0x0800
#define INT_PCI_ERR      0x1000
#define INT_PHY_EVENT    0x8000
#define INT_SUMMARY      0x010000
#define INT_RX_IDLE      0x020000
#define INT_TX_IDLE      0x040000

/* GENCTL bits */
#define GENCTL_SOFT_RESET       0x0001
#define GENCTL_INT_ENABLE       0x0002
#define GENCTL_SOFT_INT         0x0004
#define GENCTL_POWER_DOWN       0x0008
#define GENCTL_ONECOPY          0x0010
#define GENCTL_BIG_ENDIAN       0x0020
#define GENCTL_RX_DMA_PRI       0x0040
#define GENCTL_TX_DMA_PRI       0x0080
#define GENCTL_MEM_READ_LINE    0x0100
#define GENCTL_MEM_READ_MULT    0x0200
#define GENCTL_MEM_WRITE_INV    0x0400
#define GENCTL_RX_FIFO_THRESH   0x3000
#define GENCTL_TX_FIFO_THRESH   0xC000

/* RXCON bits */
#define RXCON_SAVE_ERRORED     0x01
#define RXCON_SAVE_RUNT        0x02
#define RXCON_ACCEPT_BROADCAST 0x04
#define RXCON_ACCEPT_MULTICAST 0x08
#define RXCON_MULTICAST_HASH   0x10
#define RXCON_PROMISCUOUS      0x20
#define RXCON_MONITOR          0x40
#define RXCON_EARLY_RX         0x80

/* EECTL bits */
#define EE_ENABLE       0x01
#define EE_CS           0x02
#define EE_CLK          0x04
#define EE_DATA_WRITE   0x08
#define EE_DATA_READ    0x10

/* MII operations */
#define MII_READOP   1
#define MII_WRITEOP  2

/* Descriptor ownership */
#define DESC_OWN     0x8000

/* MII PHY registers */
#define MII_BMCR     0x00
#define MII_BMSR     0x01
#define MII_PHYSID1  0x02
#define MII_PHYSID2  0x03
#define MII_ANAR     0x04
#define MII_ANLPAR   0x05
#define MII_ANER     0x06

/* MII BMSR bits */
#define BMSR_LSTATUS      0x0004
#define BMSR_ANEGCAPABLE  0x0008
#define BMSR_ANEGCOMPLETE 0x0020
#define BMSR_10HALF       0x0800
#define BMSR_10FULL       0x1000
#define BMSR_100HALF      0x2000
#define BMSR_100FULL      0x4000

#define EPIC_IO_SIZE  256

typedef struct epic100_t {
    /* PCI */
    uint8_t  pci_conf[256];
    uint8_t  pci_slot;
    uint8_t  irq_state;
    bar_t    pci_bar[2];    /* BAR0=I/O, BAR1=MMIO */
    uint32_t io_base;
    uint32_t mmio_base;
    mem_mapping_t mmio_mapping;

    /* Registers */
    uint32_t command;
    uint32_t intstat;
    uint32_t intmask;
    uint32_t genctl;
    uint32_t nvctl;
    uint32_t eectl;
    uint32_t pci_burst_cnt;
    uint32_t test1;
    uint32_t crccnt;
    uint32_t alicnt;
    uint32_t mpcnt;
    uint32_t miictrl;
    uint32_t miidata;
    uint32_t miicfg;
    uint32_t rxcon;
    uint32_t txcon;
    uint32_t txstat;
    uint32_t rxstat;
    uint32_t prcdar;
    uint32_t ptcdar;
    uint32_t txthresh;
    uint32_t earlyrx;
    uint32_t timer0;
    uint32_t timer1;

    /* MAC address and multicast */
    uint16_t lan[3];
    uint32_t mc[4];

    /* State */
    int rx_active;
    int tx_active;

    /* MII PHY */
    uint16_t mii_regs[32];

    /* EEPROM (simple 93C46: 64 x 16-bit words) */
    uint16_t ee_words[64];
    uint8_t  ee_cs_prev;
    uint8_t  ee_clk_prev;
    uint8_t  ee_do;       /* current DO output */
    uint8_t  ee_state;    /* 0=idle, 1=cmd, 2=data out */
    uint32_t ee_cmd;      /* accumulated command bits */
    uint16_t ee_data;     /* shift register for data output */
    int      ee_bits;     /* bits received/remaining */

    /* Network */
    netcard_t *nic;
} epic100_t;

static void epic100_update_irq(epic100_t *dev);
static void epic100_tx_process(epic100_t *dev);

/* --- Simple 93C46 EEPROM (DO=0 when idle) --- */

static void
epic100_eeprom_write(epic100_t *dev, uint8_t val)
{
    int cs  = (val & EE_CS) ? 1 : 0;
    int clk = (val & EE_CLK) ? 1 : 0;
    int di  = (val & EE_DATA_WRITE) ? 1 : 0;

    /* CS rising edge: reset to idle */
    if (cs && !dev->ee_cs_prev) {
        dev->ee_state = 0;
        dev->ee_bits  = 0;
        dev->ee_cmd   = 0;
        dev->ee_do    = 0;
    }
    dev->ee_cs_prev = cs;

    if (!cs) {
        dev->ee_do = 0;
        dev->ee_clk_prev = clk;
        return;
    }

    /* CLK rising edge */
    if (clk && !dev->ee_clk_prev) {
        switch (dev->ee_state) {
            case 0: /* Wait for start bit (DI=1) */
                if (di) {
                    dev->ee_state = 1;
                    dev->ee_cmd   = 0;
                    dev->ee_bits  = 0;
                }
                break;

            case 1: /* Receiving command + address (93C46: 2-bit cmd + 6-bit addr) */
                dev->ee_cmd = (dev->ee_cmd << 1) | di;
                dev->ee_bits++;
                if (dev->ee_bits >= 8) {
                    int cmd  = (dev->ee_cmd >> 6) & 0x3;
                    int addr = dev->ee_cmd & 0x3F;
                    if (cmd == 2) { /* READ command */
                        dev->ee_data = dev->ee_words[addr];
                        dev->ee_bits = 16;
                        dev->ee_state = 2;
                        dev->ee_do = 0; /* leading zero before data */
                    } else {
                        dev->ee_state = 0;
                    }
                }
                break;

            case 2: /* Shifting out data bits */
                if (dev->ee_bits > 0) {
                    dev->ee_do = (dev->ee_data >> 15) & 1;
                    dev->ee_data <<= 1;
                    dev->ee_bits--;
                } else {
                    dev->ee_do = 0;
                    dev->ee_state = 0;
                }
                break;
        }
    }
    dev->ee_clk_prev = clk;
}

/* --- Interrupt handling --- */

static void
epic100_update_irq(epic100_t *dev)
{
    /* INTMASK: bit=1 means interrupt ENABLED (not masked) */
    /* INT_SUMMARY is set when any other enabled interrupt source is pending */
    uint32_t sources = dev->intstat & dev->intmask & ~INT_SUMMARY;
    if (sources)
        dev->intstat |= INT_SUMMARY;
    else
        dev->intstat &= ~INT_SUMMARY;

    uint32_t active = dev->intstat & dev->intmask;

    if (active && (dev->genctl & GENCTL_INT_ENABLE)) {
        if (!dev->irq_state) {
            pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
        }
    } else {
        if (dev->irq_state) {
            pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
        }
    }
}

/* --- MII PHY --- */

static void
epic100_mii_init(epic100_t *dev)
{
    memset(dev->mii_regs, 0, sizeof(dev->mii_regs));
    dev->mii_regs[MII_BMCR]    = 0x1000; /* auto-negotiate enable */
    dev->mii_regs[MII_BMSR]    = BMSR_LSTATUS | BMSR_ANEGCAPABLE |
                                  BMSR_ANEGCOMPLETE | BMSR_10HALF |
                                  BMSR_10FULL | BMSR_100HALF | BMSR_100FULL;
    dev->mii_regs[MII_PHYSID1] = 0x0022; /* QS6612 PHY */
    dev->mii_regs[MII_PHYSID2] = 0x5530;
    dev->mii_regs[MII_ANAR]    = 0x01E1; /* advertise all speeds */
    dev->mii_regs[MII_ANLPAR]  = 0x45E1; /* partner supports all */
    dev->mii_regs[MII_ANER]    = 0x0001;
}

static void
epic100_mii_access(epic100_t *dev)
{
    int phy_id = (dev->miictrl >> 9) & 0x1F;
    int reg    = (dev->miictrl >> 4) & 0x1F;
    int op     = dev->miictrl & 0x03;

    if (phy_id != 3) {
        /* Only PHY ID 3 is connected (common for EPIC/100) */
        dev->miictrl &= ~(MII_READOP | MII_WRITEOP);
        return;
    }

    if (op & MII_READOP) {
        dev->miidata = dev->mii_regs[reg];
        dev->miictrl &= ~MII_READOP;
    } else if (op & MII_WRITEOP) {
        if (reg != MII_BMSR) /* BMSR is read-only */
            dev->mii_regs[reg] = dev->miidata;
        dev->miictrl &= ~MII_WRITEOP;
    }
}

/* --- Multicast hash --- */

static uint32_t
epic100_mcast_hash(const uint8_t *addr)
{
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < 6; i++) {
        uint8_t byte = addr[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t xor_val = (crc ^ byte) & 1;
            crc >>= 1;
            if (xor_val)
                crc ^= 0xEDB88320;
            byte >>= 1;
        }
    }
    return crc >> 26; /* Top 6 bits = hash index (0-63) */
}

/* --- TX processing --- */

static void
epic100_tx_process(epic100_t *dev)
{
    uint32_t desc_addr = dev->ptcdar;
    uint8_t  tx_buf[16384];
    int      count = 0;

    if (!dev->tx_active || desc_addr == 0)
        return;

    while (count++ < 64) {
        uint32_t desc[4];

        /* Read descriptor from host memory */
        dma_bm_read(desc_addr, (uint8_t *) desc, 16, 4);

        uint32_t txstatus  = desc[0];
        uint32_t bufaddr   = desc[1];
        uint32_t buflength = desc[2];
        uint32_t next      = desc[3];
        uint32_t buflen_lo = buflength & 0xFFFF;

        /* Check ownership: bit 15 of txstatus */
        if (!(txstatus & 0x8000))
            break;

        /* Total packet length is in txstatus bits [31:16] */
        uint32_t len = (txstatus >> 16) & 0xFFFF;
        if (len > sizeof(tx_buf))
            len = sizeof(tx_buf);

        if (len > 0 && bufaddr != 0) {
            if (buflen_lo > 0) {
                /* Direct buffer mode (Linux-style): buflength[15:0] has the
                   buffer length, bufaddr points directly to packet data */
                uint32_t dlen = buflen_lo;
                if (dlen > len)
                    dlen = len;
                dma_bm_read(bufaddr, tx_buf, dlen, 4);
                len = dlen;
            } else {
                /* Fragment list mode (Windows-style): buflength[15:0] == 0,
                   bufaddr points to a fragment list:
                   header(4): uint32 fragment_count
                   entries[count]: addr(4) + len(4) each */
                uint32_t frag_hdr;
                dma_bm_read(bufaddr, (uint8_t *) &frag_hdr, 4, 4);
                uint32_t nfrags = frag_hdr;

                if (nfrags > 0 && nfrags <= 63) {
                    uint32_t offset = 0;
                    for (uint32_t f = 0; f < nfrags && offset < len; f++) {
                        uint32_t fentry[2];
                        dma_bm_read(bufaddr + 4 + f * 8, (uint8_t *) fentry, 8, 4);
                        uint32_t faddr = fentry[0];
                        uint32_t flen  = fentry[1];

                        if (flen > 0 && faddr != 0) {
                            if (offset + flen > len)
                                flen = len - offset;
                            if (offset + flen > sizeof(tx_buf))
                                flen = sizeof(tx_buf) - offset;
                            dma_bm_read(faddr, tx_buf + offset, flen, 4);
                            offset += flen;
                        }
                    }
                    len = offset;
                } else {
                    /* Fallback: treat bufaddr as direct data pointer */
                    dma_bm_read(bufaddr, tx_buf, len, 4);
                }
            }

            if (len > 0) {
                network_tx(dev->nic, tx_buf, len);
            }
        }

        /* Clear DescOwn, set good status */
        desc[0] = txstatus & ~0x8000;
        dma_bm_write(desc_addr, (uint8_t *) desc, 4, 4);

        /* Advance to next descriptor */
        if (next == 0 || next == desc_addr) {
            dev->ptcdar = next;
            break;
        }
        desc_addr = next;
        dev->ptcdar = next;
    }

    /* Signal TX completion */
    dev->intstat |= INT_TX_DONE | INT_TX_EMPTY | INT_TX_IDLE;
    epic100_update_irq(dev);
}

/* --- RX callback --- */

static int
epic100_receive(void *priv, uint8_t *buf, int size)
{
    epic100_t *dev = (epic100_t *) priv;

    if (!dev->rx_active || dev->prcdar == 0)
        return 0;

    /* MAC filtering */
    if (buf[0] & 0x01) {
        /* Multicast/broadcast */
        if (buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == 0xFF &&
            buf[3] == 0xFF && buf[4] == 0xFF && buf[5] == 0xFF) {
            /* Broadcast */
            if (!(dev->rxcon & RXCON_ACCEPT_BROADCAST))
                return 0;
        } else {
            /* Multicast */
            if (dev->rxcon & RXCON_PROMISCUOUS) {
                /* accept */
            } else if (dev->rxcon & RXCON_MULTICAST_HASH) {
                uint32_t hash = epic100_mcast_hash(buf);
                uint32_t word = hash >> 5;
                uint32_t bit  = hash & 0x1F;
                if (!(dev->mc[word & 3] & (1 << bit)))
                    return 0;
            } else if (!(dev->rxcon & RXCON_ACCEPT_MULTICAST)) {
                return 0;
            }
        }
    } else {
        /* Unicast - check if it's for us */
        uint8_t mac[6];
        mac[0] = dev->lan[0] & 0xFF;
        mac[1] = (dev->lan[0] >> 8) & 0xFF;
        mac[2] = dev->lan[1] & 0xFF;
        mac[3] = (dev->lan[1] >> 8) & 0xFF;
        mac[4] = dev->lan[2] & 0xFF;
        mac[5] = (dev->lan[2] >> 8) & 0xFF;

        if (!(dev->rxcon & RXCON_PROMISCUOUS) && memcmp(buf, mac, 6) != 0)
            return 0;
    }

    /* Read RX descriptor - same field order as TX:
       rxstatus(0), bufaddr(4), buflength(8), next(12)
       DescOwn (0x8000) is in rxstatus, not buflength */
    uint32_t desc[4];
    dma_bm_read(dev->prcdar, (uint8_t *) desc, 16, 4);

    uint32_t rxstatus  = desc[0];
    uint32_t bufaddr   = desc[1];
    uint32_t buflength = desc[2];
    uint32_t next      = desc[3];

    /* Check ownership: bit 15 of rxstatus means NIC owns it */
    if (!(rxstatus & DESC_OWN))
        return 0;

    /* DMA packet data to host memory.
       buflength bit 16 = FRAGLIST: bufaddr points to a fragment list
       (same format as TX: numfrags(u32) + entries[](addr(u32) + len(u32))).
       Otherwise bufaddr is a direct data buffer. */
    uint32_t dma_size = (uint32_t) size;

    if (buflength & 0x10000) {
        /* Fragment list mode: scatter packet into fragment buffers */
        uint32_t numfrags;
        dma_bm_read(bufaddr, (uint8_t *) &numfrags, 4, 4);
        if (numfrags > 63)
            numfrags = 63;

        uint32_t offset = 0;
        for (uint32_t f = 0; f < numfrags && offset < dma_size; f++) {
            uint32_t frag_entry[2];
            dma_bm_read(bufaddr + 4 + f * 8, (uint8_t *) frag_entry, 8, 4);
            uint32_t frag_addr = frag_entry[0];
            uint32_t frag_len  = frag_entry[1];
            uint32_t chunk = dma_size - offset;
            if (chunk > frag_len)
                chunk = frag_len;
            if (frag_addr != 0 && chunk > 0)
                dma_bm_write(frag_addr, buf + offset, chunk, 4);
            offset += chunk;
        }
        dma_size = offset; /* actual bytes scattered */
    } else {
        /* Direct buffer mode */
        uint32_t bufsize = buflength & 0xFFFF;
        if (bufsize == 0)
            bufsize = 1518;
        if (dma_size > bufsize)
            dma_size = bufsize;
        if (bufaddr != 0 && dma_size > 0)
            dma_bm_write(bufaddr, buf, dma_size, 4);
    }

    /* Write RX status: length in bits 30:16 INCLUDING 4-byte CRC.
       Bit 0 (PKT_INTACT) = packet received OK.
       Bit 4 = multicast, bit 5 = broadcast.
       Error bits 1,2,13 (mask 0x2006) must be clear. */
    uint32_t status = ((uint32_t) (dma_size + 4) << 16) | 0x0001; /* PKT_INTACT */
    if (buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == 0xFF &&
        buf[3] == 0xFF && buf[4] == 0xFF && buf[5] == 0xFF)
        status |= 0x0020; /* broadcast */
    else if (buf[0] & 0x01)
        status |= 0x0010; /* multicast */
    /* Write back full descriptor (real hw only modifies desc[0] but write full for safety) */
    desc[0] = status;
    uint32_t desc_addr = dev->prcdar; /* save before advancing */
    dma_bm_write(desc_addr, (uint8_t *) desc, 16, 4);

    /* Advance to next descriptor */
    if (next != 0)
        dev->prcdar = next;

    /* Signal RX completion */
    dev->intstat |= INT_RX_DONE;
    epic100_update_irq(dev);

    return 1;
}

/* --- Register read --- */

static uint32_t
epic100_reg_read(epic100_t *dev, uint32_t addr)
{
    uint32_t ret = 0;

    switch (addr) {
        case REG_COMMAND:
            ret = dev->command;
            break;
        case REG_INTSTAT:
            ret = dev->intstat;
            break;
        case REG_INTMASK:
            ret = dev->intmask;
            break;
        case REG_GENCTL:
            ret = dev->genctl;
            break;
        case REG_NVCTL:
            ret = dev->nvctl;
            break;
        case REG_EECTL:
            ret = dev->eectl & (EE_ENABLE | EE_CS | EE_CLK | EE_DATA_WRITE);
            if (dev->ee_do)
                ret |= EE_DATA_READ;
            break;
        case REG_PCIBRSTCNT:
            ret = dev->pci_burst_cnt;
            break;
        case REG_TEST1:
            ret = dev->test1;
            break;
        case REG_CRCCNT:
            ret = dev->crccnt;
            break;
        case REG_ALICNT:
            ret = dev->alicnt;
            break;
        case REG_MPCNT:
            ret = dev->mpcnt;
            break;
        case REG_MIICTRL:
            ret = dev->miictrl;
            break;
        case REG_MIIDATA:
            ret = dev->miidata;
            break;
        case REG_MIICFG:
            ret = dev->miicfg;
            break;
        case REG_TIMER0:
            ret = dev->timer0;
            break;
        case REG_LAN0:
            ret = dev->lan[0];
            break;
        case REG_LAN1:
            ret = dev->lan[1];
            break;
        case REG_LAN2:
            ret = dev->lan[2];
            break;
        case REG_MC0:
            ret = dev->mc[0];
            break;
        case REG_MC1:
            ret = dev->mc[1];
            break;
        case REG_MC2:
            ret = dev->mc[2];
            break;
        case REG_MC3:
            ret = dev->mc[3];
            break;
        case REG_RXCON:
            ret = dev->rxcon;
            break;
        case REG_TXCON:
            ret = dev->txcon;
            break;
        case REG_TXSTAT:
            ret = dev->txstat;
            break;
        case REG_TIMER1:
            ret = dev->timer1;
            break;
        case REG_PRCDAR:
            ret = dev->prcdar;
            break;
        case REG_RXSTAT:
            ret = dev->rxstat;
            break;
        case REG_EARLYRX:
            ret = dev->earlyrx;
            break;
        case REG_PTCDAR:
            ret = dev->ptcdar;
            break;
        case REG_TXTHRESH:
            ret = dev->txthresh;
            break;
        default:
            break;
    }

    return ret;
}

/* --- Register write --- */

static void
epic100_reg_write(epic100_t *dev, uint32_t addr, uint32_t val)
{

    switch (addr) {
        case REG_COMMAND:
            if (val & CMD_START_RX) {
                dev->rx_active = 1;
                dev->intstat &= ~INT_RX_IDLE;
            }
            if (val & CMD_STOP_RX) {
                dev->rx_active = 0;
                dev->intstat |= INT_RX_IDLE;
            }
            if (val & CMD_TXQUEUED) {
                dev->tx_active = 1;
                dev->intstat &= ~INT_TX_IDLE;
                epic100_tx_process(dev);
            }
            if (val & CMD_RXQUEUED) {
                /* Acknowledge RX queue - hardware picks up new descriptors */
            }
            if (val & CMD_STOP_TDMA) {
                dev->tx_active = 0;
                dev->intstat |= INT_TX_IDLE;
            }
            if (val & CMD_STOP_RDMA) {
                dev->rx_active = 0;
                dev->intstat |= INT_RX_IDLE;
            }
            if (val & CMD_RESTART_TX) {
                dev->tx_active = 1;
                dev->intstat &= ~INT_TX_IDLE;
                epic100_tx_process(dev);
            }
            /* Command bits are self-clearing triggers - don't store them */
            dev->command = 0;
            break;

        case REG_INTSTAT:
            /* Write 1 to clear bits */
            dev->intstat &= ~(val & 0x7FFFF);
            epic100_update_irq(dev);
            break;

        case REG_INTMASK:
            dev->intmask = val;
            epic100_update_irq(dev);
            break;

        case REG_GENCTL:
            if (val & GENCTL_SOFT_RESET) {
                /* Soft reset - clear most state but preserve MAC */
                dev->command = 0;
                dev->intstat = INT_RX_IDLE | INT_TX_IDLE;
                dev->intmask = 0x00000000;  /* All interrupts disabled */
                dev->genctl  = val & ~GENCTL_SOFT_RESET;
                dev->rx_active = 0;
                dev->tx_active = 0;
                if (dev->irq_state) {
                    pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
                }
                return;
            }
            dev->genctl = val;
            if (val & GENCTL_SOFT_INT) {
                dev->genctl &= ~GENCTL_SOFT_INT;
            }
            epic100_update_irq(dev);
            break;

        case REG_NVCTL:
            dev->nvctl = val;
            break;

        case REG_EECTL:
            dev->eectl = val;
            epic100_eeprom_write(dev, val);
            break;

        case REG_PCIBRSTCNT:
            dev->pci_burst_cnt = val;
            break;

        case REG_TEST1:
            dev->test1 = val;
            break;

        case REG_CRCCNT:
            dev->crccnt = val;
            break;
        case REG_ALICNT:
            dev->alicnt = val;
            break;
        case REG_MPCNT:
            dev->mpcnt = val;
            break;

        case REG_MIICTRL:
            dev->miictrl = val;
            epic100_mii_access(dev);
            break;

        case REG_MIIDATA:
            dev->miidata = val & 0xFFFF;
            break;

        case REG_MIICFG:
            dev->miicfg = val;
            break;

        case REG_TIMER0:
            dev->timer0 = val;
            break;

        case REG_LAN0:
            dev->lan[0] = val & 0xFFFF;
            break;
        case REG_LAN1:
            dev->lan[1] = val & 0xFFFF;
            break;
        case REG_LAN2:
            dev->lan[2] = val & 0xFFFF;
            break;

        case REG_MC0:
            dev->mc[0] = val;
            break;
        case REG_MC1:
            dev->mc[1] = val;
            break;
        case REG_MC2:
            dev->mc[2] = val;
            break;
        case REG_MC3:
            dev->mc[3] = val;
            break;

        case REG_RXCON:
            dev->rxcon = val;
            break;

        case REG_TXCON:
            dev->txcon = val;
            break;

        case REG_TXSTAT:
            dev->txstat = val;
            break;

        case REG_TIMER1:
            dev->timer1 = val;
            break;

        case REG_PRCDAR:
            dev->prcdar = val;
            break;

        case REG_RXSTAT:
            dev->rxstat = val;
            break;

        case REG_EARLYRX:
            dev->earlyrx = val;
            break;

        case REG_PTCDAR:
            dev->ptcdar = val;
            break;

        case REG_TXTHRESH:
            dev->txthresh = val;
            break;

        default:
            break;
    }
}

/* --- I/O port handlers --- */

static uint8_t
epic100_io_readb(uint16_t addr, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->io_base;
    uint32_t   reg = off & ~3;
    int        shift = (off & 3) * 8;
    return (epic100_reg_read(dev, reg) >> shift) & 0xFF;
}

static uint16_t
epic100_io_readw(uint16_t addr, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->io_base;
    uint32_t   reg = off & ~3;
    int        shift = (off & 3) * 8;
    return (epic100_reg_read(dev, reg) >> shift) & 0xFFFF;
}

static uint32_t
epic100_io_readl(uint16_t addr, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->io_base;
    return epic100_reg_read(dev, off);
}

static void
epic100_io_writeb(uint16_t addr, uint8_t val, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->io_base;
    uint32_t   reg = off & ~3;
    int        shift = (off & 3) * 8;
    uint32_t   cur = epic100_reg_read(dev, reg);
    uint32_t   mask = 0xFF << shift;
    epic100_reg_write(dev, reg, (cur & ~mask) | ((uint32_t) val << shift));
}

static void
epic100_io_writew(uint16_t addr, uint16_t val, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->io_base;
    uint32_t   reg = off & ~3;
    int        shift = (off & 3) * 8;
    uint32_t   cur = epic100_reg_read(dev, reg);
    uint32_t   mask = 0xFFFF << shift;
    epic100_reg_write(dev, reg, (cur & ~mask) | ((uint32_t) val << shift));
}

static void
epic100_io_writel(uint16_t addr, uint32_t val, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->io_base;
    epic100_reg_write(dev, off, val);
}

/* --- MMIO handlers --- */

static uint8_t
epic100_mmio_readb(uint32_t addr, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->mmio_base;
    if (off >= EPIC_IO_SIZE) return 0xFF;
    uint32_t reg = off & ~3;
    int shift = (off & 3) * 8;
    return (epic100_reg_read(dev, reg) >> shift) & 0xFF;
}

static uint16_t
epic100_mmio_readw(uint32_t addr, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->mmio_base;
    if (off >= EPIC_IO_SIZE) return 0xFFFF;
    uint32_t reg = off & ~3;
    int shift = (off & 3) * 8;
    return (epic100_reg_read(dev, reg) >> shift) & 0xFFFF;
}

static uint32_t
epic100_mmio_readl(uint32_t addr, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->mmio_base;
    if (off >= EPIC_IO_SIZE) return 0xFFFFFFFF;
    return epic100_reg_read(dev, off);
}

static void
epic100_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->mmio_base;
    if (off >= EPIC_IO_SIZE) return;
    uint32_t reg = off & ~3;
    int shift = (off & 3) * 8;
    uint32_t cur = epic100_reg_read(dev, reg);
    uint32_t mask = 0xFF << shift;
    epic100_reg_write(dev, reg, (cur & ~mask) | ((uint32_t) val << shift));
}

static void
epic100_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->mmio_base;
    if (off >= EPIC_IO_SIZE) return;
    uint32_t reg = off & ~3;
    int shift = (off & 3) * 8;
    uint32_t cur = epic100_reg_read(dev, reg);
    uint32_t mask = 0xFFFF << shift;
    epic100_reg_write(dev, reg, (cur & ~mask) | ((uint32_t) val << shift));
}

static void
epic100_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;
    uint32_t   off = addr - dev->mmio_base;
    if (off >= EPIC_IO_SIZE) return;
    epic100_reg_write(dev, off, val);
}

/* --- PCI config space --- */

static uint8_t
epic100_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    const epic100_t *dev = (const epic100_t *) priv;
    uint8_t ret = 0;

    switch (addr) {
        case 0x00: ret = EPIC_VENDOR_ID & 0xFF; break;
        case 0x01: ret = EPIC_VENDOR_ID >> 8; break;
        case 0x02: ret = EPIC_DEVICE_ID & 0xFF; break;
        case 0x03: ret = EPIC_DEVICE_ID >> 8; break;
        case 0x04: ret = dev->pci_conf[0x04]; break;
        case 0x05: ret = dev->pci_conf[0x05]; break;
        case 0x06: ret = 0x80; break; /* Status: DEVSEL medium */
        case 0x07: ret = 0x02; break;
        case 0x08: ret = 0x06; break; /* Revision ID */
        case 0x09: ret = 0x00; break; /* Prog IF */
        case 0x0A: ret = 0x00; break; /* Subclass: Ethernet */
        case 0x0B: ret = 0x02; break; /* Class: Network */
        case 0x0C: ret = 0x00; break; /* Cache line size */
        case 0x0D: ret = 0x20; break; /* Latency timer */
        case 0x0E: ret = 0x00; break; /* Header type */
        /* BAR0: I/O space (256 bytes) */
        case 0x10: ret = (dev->pci_bar[0].addr_regs[0] & 0x00) | 0x01; break;
        case 0x11: ret = dev->pci_bar[0].addr_regs[1]; break;
        case 0x12: ret = dev->pci_bar[0].addr_regs[2]; break;
        case 0x13: ret = dev->pci_bar[0].addr_regs[3]; break;
        /* BAR1: Memory space (256 bytes) */
        case 0x14: ret = dev->pci_bar[1].addr_regs[0] & 0x00; break;
        case 0x15: ret = dev->pci_bar[1].addr_regs[1]; break;
        case 0x16: ret = dev->pci_bar[1].addr_regs[2]; break;
        case 0x17: ret = dev->pci_bar[1].addr_regs[3]; break;
        /* Subsystem vendor/device */
        case 0x2C: ret = EPIC_VENDOR_ID & 0xFF; break;
        case 0x2D: ret = EPIC_VENDOR_ID >> 8; break;
        case 0x2E: ret = 0x11; break; /* Subsystem device: 0xA011 (SMC9432TX EtherPower II) */
        case 0x2F: ret = 0xA0; break;
        case 0x3C: ret = dev->pci_conf[0x3C]; break;
        case 0x3D: ret = PCI_INTA; break;
        case 0x3E: ret = 0x0A; break; /* Min grant */
        case 0x3F: ret = 0x0A; break; /* Max latency */
        default:
            if (addr < 256)
                ret = dev->pci_conf[addr];
            break;
    }

    return ret;
}

static void
epic100_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    epic100_t *dev = (epic100_t *) priv;

    switch (addr) {
        case 0x04:
            dev->pci_conf[0x04] = val & 0x07;
            /* Remap I/O */
            io_removehandler(dev->io_base, EPIC_IO_SIZE,
                             epic100_io_readb, epic100_io_readw, epic100_io_readl,
                             epic100_io_writeb, epic100_io_writew, epic100_io_writel,
                             dev);
            if ((dev->io_base != 0) && (val & PCI_COMMAND_IO))
                io_sethandler(dev->io_base, EPIC_IO_SIZE,
                              epic100_io_readb, epic100_io_readw, epic100_io_readl,
                              epic100_io_writeb, epic100_io_writew, epic100_io_writel,
                              dev);
            /* Remap MMIO */
            mem_mapping_disable(&dev->mmio_mapping);
            if ((dev->mmio_base != 0) && (val & PCI_COMMAND_MEM))
                mem_mapping_set_addr(&dev->mmio_mapping, dev->mmio_base, EPIC_IO_SIZE);
            break;

        case 0x05:
            dev->pci_conf[0x05] = val & 0x01;
            break;

        /* BAR0: I/O */
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            io_removehandler(dev->io_base, EPIC_IO_SIZE,
                             epic100_io_readb, epic100_io_readw, epic100_io_readl,
                             epic100_io_writeb, epic100_io_writew, epic100_io_writel,
                             dev);
            dev->pci_bar[0].addr_regs[addr & 3] = val;
            dev->pci_bar[0].addr &= 0xFFFFFF00;
            dev->io_base = dev->pci_bar[0].addr & 0xFFFC;
            if ((dev->pci_conf[0x04] & PCI_COMMAND_IO) && (dev->io_base != 0))
                io_sethandler(dev->io_base, EPIC_IO_SIZE,
                              epic100_io_readb, epic100_io_readw, epic100_io_readl,
                              epic100_io_writeb, epic100_io_writew, epic100_io_writel,
                              dev);
            break;

        /* BAR1: MMIO */
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            mem_mapping_disable(&dev->mmio_mapping);
            dev->pci_bar[1].addr_regs[addr & 3] = val;
            dev->pci_bar[1].addr &= 0xFFFFFF00;
            dev->mmio_base = dev->pci_bar[1].addr;
            if ((dev->pci_conf[0x04] & PCI_COMMAND_MEM) && (dev->mmio_base != 0))
                mem_mapping_set_addr(&dev->mmio_mapping, dev->mmio_base, EPIC_IO_SIZE);
            break;

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

/* --- Device init/close/reset --- */

static void
epic100_reset(void *priv)
{
    epic100_t *dev = (epic100_t *) priv;

    dev->command       = 0;
    dev->intstat       = INT_RX_IDLE | INT_TX_IDLE;
    dev->intmask       = 0x00000000; /* All interrupts disabled (bit=1 means enabled) */
    dev->genctl        = 0;
    dev->rxcon         = 0;
    dev->txcon         = 0;
    dev->rx_active     = 0;
    dev->tx_active     = 0;
    dev->prcdar        = 0;
    dev->ptcdar        = 0;
    dev->txthresh      = 0;
    dev->nvctl         = 0;
    dev->test1         = 0;
    dev->miictrl       = 0;
    dev->miidata       = 0;
    dev->miicfg        = 0;
    dev->crccnt        = 0;
    dev->alicnt        = 0;
    dev->mpcnt         = 0;
    dev->pci_burst_cnt = 0;
    dev->earlyrx       = 0;
    dev->txstat        = 0;
    dev->rxstat        = 0;

    if (dev->nic)
        dev->nic->byte_period = NET_PERIOD_100M;
}

static void *
epic100_init(const device_t *info)
{
    epic100_t *dev;
    uint32_t   mac;
    uint8_t    mac_bytes_raw[6];



    dev = calloc(1, sizeof(epic100_t));
    if (!dev)
        return NULL;

    /* Set up MAC address */
    mac = device_get_config_mac("mac", -1);

    /* SMC OUI: 00:E0:29 */
    mac_bytes_raw[0] = 0x00;
    mac_bytes_raw[1] = 0xE0;
    mac_bytes_raw[2] = 0x29;
    if (mac & 0xFF000000) {
        mac_bytes_raw[3] = random_generate();
        mac_bytes_raw[4] = random_generate();
        mac_bytes_raw[5] = random_generate();
        mac  = ((int) mac_bytes_raw[3]) << 16;
        mac |= ((int) mac_bytes_raw[4]) << 8;
        mac |= ((int) mac_bytes_raw[5]);
        device_set_config_mac("mac", mac);
    } else {
        mac_bytes_raw[3] = (mac >> 16) & 0xFF;
        mac_bytes_raw[4] = (mac >> 8) & 0xFF;
        mac_bytes_raw[5] = mac & 0xFF;
    }

    /* Initialize simple 93C46 EEPROM with full card data */
    memset(dev->ee_words, 0xFF, sizeof(dev->ee_words));
    /* Words 0-2: MAC address */
    dev->ee_words[0] = mac_bytes_raw[0] | (mac_bytes_raw[1] << 8);
    dev->ee_words[1] = mac_bytes_raw[2] | (mac_bytes_raw[3] << 8);
    dev->ee_words[2] = mac_bytes_raw[4] | (mac_bytes_raw[5] << 8);
    /* Word 3: Board ID (low) + Checksum (high) */
    {
        uint8_t sum = 0;
        for (int i = 0; i < 6; i++) sum += mac_bytes_raw[i];
        sum += 0x06; /* board revision */
        dev->ee_words[3] = 0x06 | ((uint16_t)(-(int8_t)sum) << 8);
    }
    /* Word 4: NVCTL config (memory map enabled) */
    dev->ee_words[4] = 0x0001;
    /* Word 5: PCI Min Grant (low) / Max Latency (high) */
    dev->ee_words[5] = 0x0A0A;
    /* Word 6: Subsystem Vendor ID */
    dev->ee_words[6] = EPIC_VENDOR_ID;
    /* Word 7: Subsystem Device ID (SMC9432TX) */
    dev->ee_words[7] = 0xA011;

    /* Set LAN registers from EEPROM */
    dev->lan[0] = dev->ee_words[0];
    dev->lan[1] = dev->ee_words[1];
    dev->lan[2] = dev->ee_words[2];

    /* Initialize MII PHY */
    epic100_mii_init(dev);

    /* Set up MMIO mapping */
    mem_mapping_add(&dev->mmio_mapping, 0, EPIC_IO_SIZE,
                    epic100_mmio_readb, epic100_mmio_readw, epic100_mmio_readl,
                    epic100_mmio_writeb, epic100_mmio_writew, epic100_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->mmio_mapping);

    /* BAR0: I/O, BAR1: MMIO */
    dev->pci_bar[0].addr_regs[0] = 0x01; /* I/O space */
    dev->pci_bar[1].addr_regs[0] = 0x00; /* Memory space */
    dev->pci_conf[0x04] = 0x07; /* IO + MEM + Bus Master */

    /* MAC for network_attach: extract bytes from LAN regs */
    uint8_t mac_bytes[6];
    mac_bytes[0] = dev->lan[0] & 0xFF;
    mac_bytes[1] = (dev->lan[0] >> 8) & 0xFF;
    mac_bytes[2] = dev->lan[1] & 0xFF;
    mac_bytes[3] = (dev->lan[1] >> 8) & 0xFF;
    mac_bytes[4] = dev->lan[2] & 0xFF;
    mac_bytes[5] = (dev->lan[2] >> 8) & 0xFF;

    /* Register with network subsystem */
    dev->nic = network_attach(dev, mac_bytes, epic100_receive, NULL);

    /* Register PCI device */
    pci_add_card(PCI_ADD_NORMAL, epic100_pci_read, epic100_pci_write, dev, &dev->pci_slot);

    /* Reset hardware state */
    epic100_reset(dev);

    return dev;
}

static void
epic100_close(void *priv)
{
    free(priv);
}

/* --- Device config and definition --- */

// clang-format off
static const device_config_t epic100_config[] = {
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

const device_t smc_epic100_device = {
    .name          = "SMC EtherPower II 9432 (SMC 83C170 \"EPIC/100\")",
    .internal_name = "smc_epic100",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = epic100_init,
    .close         = epic100_close,
    .reset         = epic100_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = epic100_config
};
