/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of DECchip "Tulip" 21143 NIC.
 *
 * Authors: Sven Schnelle, <svens@stackframe.org>
 *          Cacodemon345,
 *
 *          Copyright 2019-2023 Sven Schnelle.
 *          Copyright 2023 Cacodemon345.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/random.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/dma.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/net_eeprom_nmc93cxx.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include <86box/bswap.h>

#define ROM_PATH_DEC21140            "roms/network/dec21140/BIOS13502.BIN"

#define CSR(_x)                      ((_x) << 3)

#define BIT(x)                       (1 << x)

#define CSR0_SWR                     BIT(0)
#define CSR0_BAR                     BIT(1)
#define CSR0_DSL_SHIFT               2
#define CSR0_DSL_MASK                0x1f
#define CSR0_BLE                     BIT(7)
#define CSR0_PBL_SHIFT               8
#define CSR0_PBL_MASK                0x3f
#define CSR0_CAC_SHIFT               14
#define CSR0_CAC_MASK                0x3
#define CSR0_DAS                     0x10000
#define CSR0_TAP_SHIFT               17
#define CSR0_TAP_MASK                0x7
#define CSR0_DBO                     0x100000
#define CSR1_TPD                     0x01
#define CSR0_RLE                     BIT(23)
#define CSR0_WIE                     BIT(24)

#define CSR2_RPD                     0x01

#define CSR5_TI                      BIT(0)
#define CSR5_TPS                     BIT(1)
#define CSR5_TU                      BIT(2)
#define CSR5_TJT                     BIT(3)
#define CSR5_LNP_ANC                 BIT(4)
#define CSR5_UNF                     BIT(5)
#define CSR5_RI                      BIT(6)
#define CSR5_RU                      BIT(7)
#define CSR5_RPS                     BIT(8)
#define CSR5_RWT                     BIT(9)
#define CSR5_ETI                     BIT(10)
#define CSR5_GTE                     BIT(11)
#define CSR5_LNF                     BIT(12)
#define CSR5_FBE                     BIT(13)
#define CSR5_ERI                     BIT(14)
#define CSR5_AIS                     BIT(15)
#define CSR5_NIS                     BIT(16)
#define CSR5_RS_SHIFT                17
#define CSR5_RS_MASK                 7
#define CSR5_TS_SHIFT                20
#define CSR5_TS_MASK                 7

#define CSR5_TS_STOPPED              0
#define CSR5_TS_RUNNING_FETCH        1
#define CSR5_TS_RUNNING_WAIT_EOT     2
#define CSR5_TS_RUNNING_READ_BUF     3
#define CSR5_TS_RUNNING_SETUP        5
#define CSR5_TS_SUSPENDED            6
#define CSR5_TS_RUNNING_CLOSE        7

#define CSR5_RS_STOPPED              0
#define CSR5_RS_RUNNING_FETCH        1
#define CSR5_RS_RUNNING_CHECK_EOR    2
#define CSR5_RS_RUNNING_WAIT_RECEIVE 3
#define CSR5_RS_SUSPENDED            4
#define CSR5_RS_RUNNING_CLOSE        5
#define CSR5_RS_RUNNING_FLUSH        6
#define CSR5_RS_RUNNING_QUEUE        7

#define CSR5_EB_SHIFT                23
#define CSR5_EB_MASK                 7

#define CSR5_GPI                     BIT(26)
#define CSR5_LC                      BIT(27)

#define CSR6_HP                      BIT(0)
#define CSR6_SR                      BIT(1)
#define CSR6_HO                      BIT(2)
#define CSR6_PB                      BIT(3)
#define CSR6_IF                      BIT(4)
#define CSR6_SB                      BIT(5)
#define CSR6_PR                      BIT(6)
#define CSR6_PM                      BIT(7)
#define CSR6_FKD                     BIT(8)
#define CSR6_FD                      BIT(9)

#define CSR6_OM_SHIFT                10
#define CSR6_OM_MASK                 3
#define CSR6_OM_NORMAL               0
#define CSR6_OM_INT_LOOPBACK         1
#define CSR6_OM_EXT_LOOPBACK         2

#define CSR6_FC                      BIT(12)
#define CSR6_ST                      BIT(13)

#define CSR6_TR_SHIFT                14
#define CSR6_TR_MASK                 3
#define CSR6_TR_72                   0
#define CSR6_TR_96                   1
#define CSR6_TR_128                  2
#define CSR6_TR_160                  3

#define CSR6_CA                      BIT(17)
#define CSR6_RA                      BIT(30)
#define CSR6_SC                      BIT(31)

#define CSR7_TIM                     BIT(0)
#define CSR7_TSM                     BIT(1)
#define CSR7_TUM                     BIT(2)
#define CSR7_TJM                     BIT(3)
#define CSR7_LPM                     BIT(4)
#define CSR7_UNM                     BIT(5)
#define CSR7_RIM                     BIT(6)
#define CSR7_RUM                     BIT(7)
#define CSR7_RSM                     BIT(8)
#define CSR7_RWM                     BIT(9)
#define CSR7_TMM                     BIT(11)
#define CSR7_LFM                     BIT(12)
#define CSR7_SEM                     BIT(13)
#define CSR7_ERM                     BIT(14)
#define CSR7_AIM                     BIT(15)
#define CSR7_NIM                     BIT(16)

#define CSR8_MISSED_FRAME_OVL        BIT(16)
#define CSR8_MISSED_FRAME_CNT_MASK   0xffff

#define CSR9_DATA_MASK               0xff
#define CSR9_SR_CS                   BIT(0)
#define CSR9_SR_SK                   BIT(1)
#define CSR9_SR_DI                   BIT(2)
#define CSR9_SR_DO                   BIT(3)
#define CSR9_REG                     BIT(10)
#define CSR9_SR                      BIT(11)
#define CSR9_BR                      BIT(12)
#define CSR9_WR                      BIT(13)
#define CSR9_RD                      BIT(14)
#define CSR9_MOD                     BIT(15)
#define CSR9_MDC                     BIT(16)
#define CSR9_MDO                     BIT(17)
#define CSR9_MII                     BIT(18)
#define CSR9_MDI                     BIT(19)

#define CSR11_CON                    BIT(16)
#define CSR11_TIMER_MASK             0xffff

#define CSR12_MRA                    BIT(0)
#define CSR12_LS100                  BIT(1)
#define CSR12_LS10                   BIT(2)
#define CSR12_APS                    BIT(3)
#define CSR12_ARA                    BIT(8)
#define CSR12_TRA                    BIT(9)
#define CSR12_NSN                    BIT(10)
#define CSR12_TRF                    BIT(11)
#define CSR12_ANS_SHIFT              12
#define CSR12_ANS_MASK               7
#define CSR12_LPN                    BIT(15)
#define CSR12_LPC_SHIFT              16
#define CSR12_LPC_MASK               0xffff

#define CSR13_SRL                    BIT(0)
#define CSR13_CAC                    BIT(2)
#define CSR13_AUI                    BIT(3)
#define CSR13_SDM_SHIFT              4
#define CSR13_SDM_MASK               0xfff

#define CSR14_ECEN                   BIT(0)
#define CSR14_LBK                    BIT(1)
#define CSR14_DREN                   BIT(2)
#define CSR14_LSE                    BIT(3)
#define CSR14_CPEN_SHIFT             4
#define CSR14_CPEN_MASK              3
#define CSR14_MBO                    BIT(6)
#define CSR14_ANE                    BIT(7)
#define CSR14_RSQ                    BIT(8)
#define CSR14_CSQ                    BIT(9)
#define CSR14_CLD                    BIT(10)
#define CSR14_SQE                    BIT(11)
#define CSR14_LTE                    BIT(12)
#define CSR14_APE                    BIT(13)
#define CSR14_SPP                    BIT(14)
#define CSR14_TAS                    BIT(15)

#define CSR15_JBD                    BIT(0)
#define CSR15_HUJ                    BIT(1)
#define CSR15_JCK                    BIT(2)
#define CSR15_ABM                    BIT(3)
#define CSR15_RWD                    BIT(4)
#define CSR15_RWR                    BIT(5)
#define CSR15_LE1                    BIT(6)
#define CSR15_LV1                    BIT(7)
#define CSR15_TSCK                   BIT(8)
#define CSR15_FUSQ                   BIT(9)
#define CSR15_FLF                    BIT(10)
#define CSR15_LSD                    BIT(11)
#define CSR15_DPST                   BIT(12)
#define CSR15_FRL                    BIT(13)
#define CSR15_LE2                    BIT(14)
#define CSR15_LV2                    BIT(15)

#define RDES0_OF                     BIT(0)
#define RDES0_CE                     BIT(1)
#define RDES0_DB                     BIT(2)
#define RDES0_RJ                     BIT(4)
#define RDES0_FT                     BIT(5)
#define RDES0_CS                     BIT(6)
#define RDES0_TL                     BIT(7)
#define RDES0_LS                     BIT(8)
#define RDES0_FS                     BIT(9)
#define RDES0_MF                     BIT(10)
#define RDES0_RF                     BIT(11)
#define RDES0_DT_SHIFT               12
#define RDES0_DT_MASK                3
#define RDES0_DE                     BIT(14)
#define RDES0_ES                     BIT(15)
#define RDES0_FL_SHIFT               16
#define RDES0_FL_MASK                0x3fff
#define RDES0_FF                     BIT(30)
#define RDES0_OWN                    BIT(31)

#define RDES1_BUF1_SIZE_SHIFT        0
#define RDES1_BUF1_SIZE_MASK         0x7ff

#define RDES1_BUF2_SIZE_SHIFT        11
#define RDES1_BUF2_SIZE_MASK         0x7ff
#define RDES1_RCH                    BIT(24)
#define RDES1_RER                    BIT(25)

#define TDES0_DE                     BIT(0)
#define TDES0_UF                     BIT(1)
#define TDES0_LF                     BIT(2)
#define TDES0_CC_SHIFT               3
#define TDES0_CC_MASK                0xf
#define TDES0_HF                     BIT(7)
#define TDES0_EC                     BIT(8)
#define TDES0_LC                     BIT(9)
#define TDES0_NC                     BIT(10)
#define TDES0_LO                     BIT(11)
#define TDES0_TO                     BIT(14)
#define TDES0_ES                     BIT(15)
#define TDES0_OWN                    BIT(31)

#define TDES1_BUF1_SIZE_SHIFT        0
#define TDES1_BUF1_SIZE_MASK         0x7ff

#define TDES1_BUF2_SIZE_SHIFT        11
#define TDES1_BUF2_SIZE_MASK         0x7ff

#define TDES1_FT0                    BIT(22)
#define TDES1_DPD                    BIT(23)
#define TDES1_TCH                    BIT(24)
#define TDES1_TER                    BIT(25)
#define TDES1_AC                     BIT(26)
#define TDES1_SET                    BIT(27)
#define TDES1_FT1                    BIT(28)
#define TDES1_FS                     BIT(29)
#define TDES1_LS                     BIT(30)
#define TDES1_IC                     BIT(31)

#define ETH_ALEN                     6

static bar_t   tulip_pci_bar[3];

struct tulip_descriptor {
    uint32_t status;
    uint32_t control;
    uint32_t buf_addr1;
    uint32_t buf_addr2;
};

struct TULIPState {
    uint8_t            pci_slot;
    uint8_t            irq_state;
    int                PCIBase;
    int                MMIOBase;
    const device_t    *device_info;
    uint16_t           subsys_id;
    uint16_t           subsys_ven_id;
    mem_mapping_t      memory;
    rom_t              bios_rom;
    netcard_t         *nic;
    nmc93cxx_eeprom_t *eeprom;
    uint32_t           csr[16];
    uint8_t            pci_conf[256];
    uint16_t           mii_regs[32];
    uint8_t            eeprom_data[128];

    /* state for MII */
    uint32_t old_csr9;
    uint32_t mii_word;
    uint32_t mii_bitcnt;

    /* 21040 ROM read address. */
    uint8_t rom_read_addr;

    uint32_t current_rx_desc;
    uint32_t current_tx_desc;

    uint8_t  rx_frame[2048];
    uint8_t  tx_frame[2048];
    uint16_t tx_frame_len;
    uint16_t rx_frame_len;
    uint16_t rx_frame_size;

    uint32_t rx_status;
    uint32_t bios_addr;
    uint8_t  filter[16][6];
    int      has_bios;
};

typedef struct TULIPState TULIPState;

static void
tulip_desc_read(TULIPState *s, uint32_t p,
                struct tulip_descriptor *desc)
{
    desc->status    = mem_readl_phys(p);
    desc->control   = mem_readl_phys(p + 4);
    desc->buf_addr1 = mem_readl_phys(p + 8);
    desc->buf_addr2 = mem_readl_phys(p + 12);

    if (s->csr[0] & CSR0_DBO) {
        bswap32s(&desc->status);
        bswap32s(&desc->control);
        bswap32s(&desc->buf_addr1);
        bswap32s(&desc->buf_addr2);
    }
}

static void
tulip_desc_write(TULIPState *s, uint32_t p,
                 struct tulip_descriptor *desc)
{
    if (s->csr[0] & CSR0_DBO) {
        mem_writel_phys(p, bswap32(desc->status));
        mem_writel_phys(p + 4, bswap32(desc->control));
        mem_writel_phys(p + 8, bswap32(desc->buf_addr1));
        mem_writel_phys(p + 12, bswap32(desc->buf_addr2));
    } else {
        mem_writel_phys(p, desc->status);
        mem_writel_phys(p + 4, desc->control);
        mem_writel_phys(p + 8, desc->buf_addr1);
        mem_writel_phys(p + 12, desc->buf_addr2);
    }
}

static void
tulip_update_int(TULIPState *s)
{
    uint32_t ie     = s->csr[5] & s->csr[7];
    bool     assert = false;

    s->csr[5] &= ~(CSR5_AIS | CSR5_NIS);

    if (ie & (CSR5_TI | CSR5_TU | CSR5_RI | CSR5_GTE | CSR5_ERI)) {
        s->csr[5] |= CSR5_NIS;
    }

    if (ie & (CSR5_LC | CSR5_GPI | CSR5_FBE | CSR5_LNF | CSR5_ETI | CSR5_RWT | CSR5_RPS | CSR5_RU | CSR5_UNF | CSR5_LNP_ANC | CSR5_TJT | CSR5_TPS)) {
        s->csr[5] |= CSR5_AIS;
    }

    assert = s->csr[5] & s->csr[7] & (CSR5_AIS | CSR5_NIS);
    if (!assert)
        pci_clear_irq(s->pci_slot, PCI_INTA, &s->irq_state);
    else
        pci_set_irq(s->pci_slot, PCI_INTA, &s->irq_state);
}

static bool
tulip_rx_stopped(TULIPState *s)
{
    return ((s->csr[5] >> CSR5_RS_SHIFT) & CSR5_RS_MASK) == CSR5_RS_STOPPED;
}

static void
tulip_next_rx_descriptor(TULIPState              *s,
                         struct tulip_descriptor *desc)
{
    if (desc->control & RDES1_RER) {
        s->current_rx_desc = s->csr[3];
    } else if (desc->control & RDES1_RCH) {
        s->current_rx_desc = desc->buf_addr2;
    } else {
        s->current_rx_desc += sizeof(struct tulip_descriptor) + (((s->csr[0] >> CSR0_DSL_SHIFT) & CSR0_DSL_MASK) << 2);
    }
    s->current_rx_desc &= ~3ULL;
}

static void
tulip_copy_rx_bytes(TULIPState *s, struct tulip_descriptor *desc)
{
    int len1 = (desc->control >> RDES1_BUF1_SIZE_SHIFT) & RDES1_BUF1_SIZE_MASK;
    int len2 = (desc->control >> RDES1_BUF2_SIZE_SHIFT) & RDES1_BUF2_SIZE_MASK;
    int len;

    if (s->rx_frame_len && len1) {
        if (s->rx_frame_len > len1) {
            len = len1;
        } else {
            len = s->rx_frame_len;
        }

        dma_bm_write(desc->buf_addr1, s->rx_frame + (s->rx_frame_size - s->rx_frame_len), len, 4);
        s->rx_frame_len -= len;
    }

    if (s->rx_frame_len && len2) {
        if (s->rx_frame_len > len2) {
            len = len2;
        } else {
            len = s->rx_frame_len;
        }

        dma_bm_write(desc->buf_addr2, s->rx_frame + (s->rx_frame_size - s->rx_frame_len), len, 4);
        s->rx_frame_len -= len;
    }
}

static bool
tulip_filter_address(TULIPState *s, const uint8_t *addr)
{
#ifdef BLOCK_BROADCAST
    static const char broadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
#endif
    bool              ret         = false;

    for (uint8_t i = 0; i < 16 && ret == false; i++) {
        if (!memcmp(&s->filter[i], addr, ETH_ALEN)) {
            ret = true;
        }
    }

/*
   Do not block broadcast packets - needed for connections to the guest
   to succeed when using SLiRP.
 */
#ifdef BLOCK_BROADCAST
    if (!memcmp(addr, broadcast, ETH_ALEN)) {
        return true;
    }
#endif

    if (s->csr[6] & (CSR6_PR | CSR6_RA)) {
        /* Promiscuous mode enabled */
        s->rx_status |= RDES0_FF;
        return true;
    }

    if ((s->csr[6] & CSR6_PM) && (addr[0] & 1)) {
        /* Pass all Multicast enabled */
        s->rx_status |= RDES0_MF;
        return true;
    }

    if (s->csr[6] & CSR6_IF) {
        ret ^= true;
    }
    return ret;
}

static int
tulip_receive(void *priv, uint8_t *buf, int size)
{
    struct tulip_descriptor desc;
    TULIPState             *s = (TULIPState *) priv;
    int                     first = 1;

    if (size < 14 || size > sizeof(s->rx_frame) - 4
        || s->rx_frame_len || tulip_rx_stopped(s))
        return 0;

    if (!tulip_filter_address(s, buf)) {
        //pclog("Not a filter address.\n");
        return 1;
    }

    //pclog("Size = %d, FrameLen = %d, Buffer[%02x:%02x:%02x:%02x:%02x:%02x].\n", size, s->rx_frame_len, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
    do {
        tulip_desc_read(s, s->current_rx_desc, &desc);

        if (!(desc.status & RDES0_OWN)) {
            s->csr[5] |= CSR5_RU;
            tulip_update_int(s);
            if (first)
                /* Stop at the very beginning, tell the host 0 bytes have been received. */
                return 0;
            else
                return (s->rx_frame_size - s->rx_frame_len) % s->rx_frame_size;
        }
        desc.status = 0;

        if (!s->rx_frame_len) {
            s->rx_frame_size = size + 4;
            s->rx_status     = RDES0_LS | ((s->rx_frame_size & RDES0_FL_MASK) << RDES0_FL_SHIFT);
            desc.status |= RDES0_FS;
            memcpy(s->rx_frame, buf, size);
            s->rx_frame_len = s->rx_frame_size;
        }

        tulip_copy_rx_bytes(s, &desc);

        if (!s->rx_frame_len) {
            desc.status |= s->rx_status;
            s->csr[5] |= CSR5_RI;
            tulip_update_int(s);
        }
        tulip_desc_write(s, s->current_rx_desc, &desc);
        tulip_next_rx_descriptor(s, &desc);
        first = 0;
    } while (s->rx_frame_len);

    return 1;
}

static void
tulip_update_rs(TULIPState *s, int state)
{
    s->csr[5] &= ~(CSR5_RS_MASK << CSR5_RS_SHIFT);
    s->csr[5] |= (state & CSR5_RS_MASK) << CSR5_RS_SHIFT;
}

static const uint16_t tulip_mdi_default[] = {
    /* MDI Registers 0 - 6, 7 */
    0x3100,
    0xf02c,
    0x7810,
    0x0000,
    0x0501,
    0x4181,
    0x0000,
    0x0000,
    /* MDI Registers 8 - 15 */
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x3800,
    0x0000,
    /* MDI Registers 16 - 31 */
    0x0003,
    0x0600,
    0x0001,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
};

/* Readonly mask for MDI (PHY) registers */
extern uint16_t l80225_mii_readw(uint16_t* regs, uint16_t addr);
extern void l80225_mii_writew(uint16_t* regs, uint16_t addr, uint16_t val);

static uint16_t
tulip_mii_read(TULIPState *s, int phy, int reg)
{
    uint16_t ret = 0;
    if (phy == 1) {
        ret = l80225_mii_readw(s->mii_regs, reg);
    }
    //pclog("MII read phy = %02x, regs = %x, reg = %x, ret = %04x.\n", phy, s->mii_regs, reg, ret);
    return ret;
}

static void
tulip_mii_write(TULIPState *s, int phy, int reg, uint16_t data)
{
    if (phy != 1) {
        return;
    }

    return l80225_mii_writew(s->mii_regs, reg, data);
}

static void
tulip_mii(TULIPState *s)
{
    uint32_t changed = s->old_csr9 ^ s->csr[9];
    uint16_t data;
    int      op;
    int      phy;
    int      reg;

    if (!(changed & CSR9_MDC)) {
        //pclog("No Change.\n");
        return;
    }

    if (!(s->csr[9] & CSR9_MDC)) {
        //pclog("No Change to MDC.\n");
        return;
    }

    s->mii_bitcnt++;
    s->mii_word <<= 1;

    if ((s->csr[9] & CSR9_MDO) && (s->mii_bitcnt < 16 || !(s->csr[9] & CSR9_MII))) {
        /* write op or address bits */
        s->mii_word |= 1;
        //pclog("WriteOp.\n");
    }

    if ((s->mii_bitcnt >= 16) && (s->csr[9] & CSR9_MII)) {
        if (s->mii_word & 0x8000) {
            s->csr[9] |= CSR9_MDI;
            //pclog("CSR9 MDI set.\n");
        } else {
            s->csr[9] &= ~CSR9_MDI;
            //pclog("CSR9 MDI cleared.\n");
        }
    }

    if (s->mii_word == 0xffffffff) {
        s->mii_bitcnt = 0;
        //pclog("BitCnt = 0.\n");
    } else if (s->mii_bitcnt == 16) {
        op  = (s->mii_word >> 12) & 0x0f;
        phy = (s->mii_word >> 7) & 0x1f;
        reg = (s->mii_word >> 2) & 0x1f;

        //pclog("BitCnt = 16, op=%d, phy=%x, reg=%x.\n");
        if (op == 6) {
            s->mii_word = tulip_mii_read(s, phy, reg);
        }
    } else if (s->mii_bitcnt == 32) {
        op   = (s->mii_word >> 28) & 0x0f;
        phy  = (s->mii_word >> 23) & 0x1f;
        reg  = (s->mii_word >> 18) & 0x1f;
        data = s->mii_word & 0xffff;

        //pclog("BitCnt = 32, op=%d, phy=%x, reg=%x.\n");
        if (op == 5) {
            tulip_mii_write(s, phy, reg, data);
        }
    }
}

static uint32_t
tulip_csr9_read(TULIPState *s)
{
    if (s->device_info->local == 3) {
        return s->eeprom_data[s->rom_read_addr++];
    }
    if (s->csr[9] & CSR9_SR) {
        if (nmc93cxx_eeprom_read(s->eeprom)) {
            s->csr[9] |= CSR9_SR_DO;
        } else {
            s->csr[9] &= ~CSR9_SR_DO;
        }
    }

    tulip_mii(s);
    return s->csr[9];
}

static void
tulip_update_ts(TULIPState *s, int state)
{
    s->csr[5] &= ~(CSR5_TS_MASK << CSR5_TS_SHIFT);
    s->csr[5] |= (state & CSR5_TS_MASK) << CSR5_TS_SHIFT;
}

static uint32_t
tulip_read(uint32_t addr, void *opaque)
{
    TULIPState *s    = opaque;
    uint32_t    data = 0;
    addr &= 127;

    switch (addr) {
        case CSR(9):
            data = tulip_csr9_read(s);
            break;

        case CSR(12):
            if (s->device_info->local == 3) {
                data = 0;
                break;
            }
            /* Fake autocompletion complete until we have PHY emulation */
            data = 5 << CSR12_ANS_SHIFT;
            break;

        default:
            if (!(addr & 7))
                data = s->csr[addr >> 3];
            break;
    }
    //pclog("[%04X:%08X]: CSR9 read %02x, data = %08x.\n", CS, cpu_state.pc, addr, data);
    return data;
}

static void
tulip_tx(TULIPState *s, struct tulip_descriptor *desc)
{
    //pclog("TX FrameLen = %d.\n", s->tx_frame_len);
    if (s->tx_frame_len) {
        if ((s->csr[6] >> CSR6_OM_SHIFT) & CSR6_OM_MASK) {
            /* Internal or external Loopback */
            tulip_receive(s, s->tx_frame, s->tx_frame_len);
        } else if (s->tx_frame_len <= sizeof(s->tx_frame)) {
            //pclog("Transmit!.\n");
            network_tx(s->nic, s->tx_frame, s->tx_frame_len);
        }
    }

    if (desc->control & TDES1_IC) {
        s->csr[5] |= CSR5_TI;
        tulip_update_int(s);
    }
}

static int
tulip_copy_tx_buffers(TULIPState *s, struct tulip_descriptor *desc)
{
    int len1 = (desc->control >> TDES1_BUF1_SIZE_SHIFT) & TDES1_BUF1_SIZE_MASK;
    int len2 = (desc->control >> TDES1_BUF2_SIZE_SHIFT) & TDES1_BUF2_SIZE_MASK;

    if (s->tx_frame_len + len1 > sizeof(s->tx_frame)) {
        return -1;
    }
    if (len1) {
        dma_bm_read(desc->buf_addr1,
                    s->tx_frame + s->tx_frame_len, len1, 4);
        s->tx_frame_len += len1;
    }

    if (s->tx_frame_len + len2 > sizeof(s->tx_frame)) {
        return -1;
    }
    if (len2) {
        dma_bm_read(desc->buf_addr2,
                    s->tx_frame + s->tx_frame_len, len2, 4);
        s->tx_frame_len += len2;
    }
    desc->status = (len1 + len2) ? 0 : 0x7fffffff;

    return 0;
}

static void
tulip_setup_filter_addr(TULIPState *s, uint8_t *buf, int n)
{
    int offset = n * 12;

    s->filter[n][0] = buf[offset];
    s->filter[n][1] = buf[offset + 1];

    s->filter[n][2] = buf[offset + 4];
    s->filter[n][3] = buf[offset + 5];

    s->filter[n][4] = buf[offset + 8];
    s->filter[n][5] = buf[offset + 9];
}

static void
tulip_setup_frame(TULIPState              *s,
                  struct tulip_descriptor *desc)
{
    uint8_t buf[4096];
    int     len = (desc->control >> TDES1_BUF1_SIZE_SHIFT) & TDES1_BUF1_SIZE_MASK;

    if (len == 192) {
        dma_bm_read(desc->buf_addr1, buf, len, 4);
        for (uint8_t i = 0; i < 16; i++) {
            tulip_setup_filter_addr(s, buf, i);
        }
    }

    desc->status = 0x7fffffff;

    if (desc->control & TDES1_IC) {
        s->csr[5] |= CSR5_TI;
        tulip_update_int(s);
    }
}

static void
tulip_next_tx_descriptor(TULIPState              *s,
                         struct tulip_descriptor *desc)
{
    if (desc->control & TDES1_TER) {
        s->current_tx_desc = s->csr[4];
    } else if (desc->control & TDES1_TCH) {
        s->current_tx_desc = desc->buf_addr2;
    } else {
        s->current_tx_desc += sizeof(struct tulip_descriptor) + (((s->csr[0] >> CSR0_DSL_SHIFT) & CSR0_DSL_MASK) << 2);
    }
    s->current_tx_desc &= ~3ULL;
}

static uint32_t
tulip_ts(TULIPState *s)
{
    return (s->csr[5] >> CSR5_TS_SHIFT) & CSR5_TS_MASK;
}

static void
tulip_xmit_list_update(TULIPState *s)
{
#define TULIP_DESC_MAX 128
    struct tulip_descriptor desc;

    if (tulip_ts(s) != CSR5_TS_SUSPENDED) {
        return;
    }

    for (uint8_t i = 0; i < TULIP_DESC_MAX; i++) {
        tulip_desc_read(s, s->current_tx_desc, &desc);

        if (!(desc.status & TDES0_OWN)) {
            tulip_update_ts(s, CSR5_TS_SUSPENDED);
            s->csr[5] |= CSR5_TU;
            tulip_update_int(s);
            return;
        }

        if (desc.control & TDES1_SET) {
            tulip_setup_frame(s, &desc);
        } else {
            if (desc.control & TDES1_FS) {
                s->tx_frame_len = 0;
            }

            if (!tulip_copy_tx_buffers(s, &desc)) {
                if (desc.control & TDES1_LS) {
                    tulip_tx(s, &desc);
                }
            }
        }
        tulip_desc_write(s, s->current_tx_desc, &desc);
        tulip_next_tx_descriptor(s, &desc);
    }
}

static void
tulip_csr9_write(TULIPState *s, UNUSED(uint32_t old_val),
                 uint32_t    new_val)
{
    if (new_val & CSR9_SR) {
        nmc93cxx_eeprom_write(s->eeprom,
                              !!(new_val & CSR9_SR_CS),
                              !!(new_val & CSR9_SR_SK),
                              !!(new_val & CSR9_SR_DI));
    }
}

static void
tulip_reset(void *priv)
{
    TULIPState     *s           = (TULIPState *) priv;
    const uint16_t *eeprom_data = nmc93cxx_eeprom_data(s->eeprom);
    s->csr[0]                   = 0xfe000000;
    s->csr[1]                   = 0xffffffff;
    s->csr[2]                   = 0xffffffff;
    s->csr[5]                   = 0xfc000000;
    s->csr[6]                   = 0x32000040;
    s->csr[7]                   = 0xfffe0000;
    s->csr[8]                   = 0x00000000;
    s->csr[9]                   = 0xfff483ff;
    s->csr[11]                  = 0xfffe0000;
    s->csr[12]                  = 0xfffffec6;
    s->csr[13]                  = 0xffff0000;
    s->csr[14]                  = 0xffffffff;
    s->csr[15]                  = 0x8ff00000;
    if (s->device_info->local != 3) {
        s->subsys_id                = eeprom_data[1];
        s->subsys_ven_id            = eeprom_data[0];
    }
}

static void
tulip_write(uint32_t addr, uint32_t data, void *opaque)
{
    TULIPState *s = opaque;
    addr &= 127;

    //pclog("[%04X:%08X]: Tulip Write >> 3: %02x, val=%08x.\n", CS, cpu_state.pc, addr >> 3, data);
    switch (addr) {
        case CSR(0):
            s->csr[0] = data;
            if (data & CSR0_SWR) {
                tulip_reset(s);
                tulip_update_int(s);
            }
            break;

        case CSR(1):
            tulip_xmit_list_update(s);
            break;

        case CSR(2):
            break;

        case CSR(3):
            s->csr[3]          = data & ~3ULL;
            s->current_rx_desc = s->csr[3];
            break;

        case CSR(4):
            s->csr[4]          = data & ~3ULL;
            s->current_tx_desc = s->csr[4];
            tulip_xmit_list_update(s);
            break;

        case CSR(5):
            /* Status register, write clears bit */
            s->csr[5] &= ~(data & (CSR5_TI | CSR5_TPS | CSR5_TU | CSR5_TJT | CSR5_LNP_ANC | CSR5_UNF | CSR5_RI | CSR5_RU | CSR5_RPS | CSR5_RWT | CSR5_ETI | CSR5_GTE | CSR5_LNF | CSR5_FBE | CSR5_ERI | CSR5_AIS | CSR5_NIS | CSR5_GPI | CSR5_LC));
            tulip_update_int(s);
            break;

        case CSR(6):
            s->csr[6] = data;
            if (s->csr[6] & CSR6_SR) {
                tulip_update_rs(s, CSR5_RS_RUNNING_WAIT_RECEIVE);
            } else {
                tulip_update_rs(s, CSR5_RS_STOPPED);
            }

            if (s->csr[6] & CSR6_ST) {
                tulip_update_ts(s, CSR5_TS_SUSPENDED);
                tulip_xmit_list_update(s);
            } else {
                tulip_update_ts(s, CSR5_TS_STOPPED);
                s->csr[5] |= CSR5_TPS;
            }
            break;

        case CSR(7):
            s->csr[7] = data;
            tulip_update_int(s);
            break;

        case CSR(8):
            s->csr[8] = data;
            break;

        case CSR(9):
            if (s->device_info->local != 3) {
                tulip_csr9_write(s, s->csr[9], data);
                /* don't clear MII read data */
                s->csr[9] &= CSR9_MDI;
                s->csr[9] |= (data & ~CSR9_MDI);
                tulip_mii(s);
                s->old_csr9 = s->csr[9];
            } else {
                s->rom_read_addr = 0;
            }
            break;

        case CSR(10):
            s->csr[10] = data;
            break;

        case CSR(11):
            s->csr[11] = data;
            break;

        case CSR(12):
            /* SIA Status register, some bits are cleared by writing 1 */
            s->csr[12] &= ~(data & (CSR12_MRA | CSR12_TRA | CSR12_ARA));
            break;

        case CSR(13):
            s->csr[13] = data;
            if (s->device_info->local == 3 && (data & 0x4)) {
                s->csr[13] = 0x8f01;
                s->csr[14] = 0xfffd;
                s->csr[15] = 0;
            }
            break;

        case CSR(14):
            s->csr[14] = data;
            break;

        case CSR(15):
            s->csr[15] = data;
            break;

        default:
            pclog("%s: write to CSR at unknown address "
                  "0x%u\n",
                  __func__, addr);
            break;
    }
}

static void
tulip_writeb_io(uint16_t addr, uint8_t data, void *opaque)
{
    return tulip_write(addr, data, opaque);
}

static void
tulip_writew_io(uint16_t addr, uint16_t data, void *opaque)
{
    return tulip_write(addr, data, opaque);
}

static void
tulip_writel_io(uint16_t addr, uint32_t data, void *opaque)
{
    return tulip_write(addr, data, opaque);
}

static void
tulip_mem_writeb(uint32_t addr, uint8_t data, void *opaque)
{
    if ((addr & 0xfff) < 0x100)
        tulip_write(addr, data, opaque);
}

static void
tulip_mem_writew(uint32_t addr, uint16_t data, void *opaque)
{
    if ((addr & 0xfff) < 0x100)
        tulip_write(addr, data, opaque);
}

static void
tulip_mem_writel(uint32_t addr, uint32_t data, void *opaque)
{
    if ((addr & 0xfff) < 0x100)
        tulip_write(addr, data, opaque);
}

static uint8_t
tulip_mem_readb(uint32_t addr, void *opaque)
{
    uint8_t ret = 0xff;

    if ((addr & 0xfff) < 0x100)
        ret = tulip_read(addr, opaque);

    return ret;
}

static uint16_t
tulip_mem_readw(uint32_t addr, void *opaque)
{
    uint16_t ret = 0xffff;

    if ((addr & 0xfff) < 0x100)
        ret = tulip_read(addr, opaque);

    return ret;
}

static uint32_t
tulip_mem_readl(uint32_t addr, void *opaque)
{
    uint32_t ret = 0xffffffff;

    if ((addr & 0xfff) < 0x100)
        ret = tulip_read(addr, opaque);

    return ret;
}

static uint8_t
tulip_readb_io(uint16_t addr, void *opaque)
{
    return tulip_read(addr, opaque);
}

static uint16_t
tulip_readw_io(uint16_t addr, void *opaque)
{
    return tulip_read(addr, opaque);
}

static uint32_t
tulip_readl_io(uint16_t addr, void *opaque)
{
    return tulip_read(addr, opaque);
}

static void
tulip_idblock_crc(uint16_t *srom)
{
    unsigned char bitval;
    unsigned char crc;
    const int     len = 9;
    crc               = -1;

    for (int word = 0; word < len; word++) {
        for (int8_t bit = 15; bit >= 0; bit--) {
            if ((word == (len - 1)) && (bit == 7)) {
                /*
                 * Insert the correct CRC result into input data stream
                 * in place.
                 */
                srom[len - 1] = (srom[len - 1] & 0xff00) | (unsigned short) crc;
                break;
            }
            bitval = ((srom[word] >> bit) & 1) ^ ((crc >> 7) & 1);
            crc    = crc << 1;
            if (bitval == 1) {
                crc ^= 6;
                crc |= 0x01;
            }
        }
    }
}

static uint16_t
tulip_srom_crc(uint8_t *eeprom)
{
    unsigned long crc        = 0xffffffff;
    unsigned long flippedcrc = 0;
    unsigned char currentbyte;
    unsigned int  msb;
    unsigned int  bit;

    for (size_t i = 0; i < 126; i++) {
        currentbyte = eeprom[i];
        for (bit = 0; bit < 8; bit++) {
            msb = (crc >> 31) & 1;
            crc <<= 1;
            if (msb ^ (currentbyte & 1)) {
                crc ^= 0x04c11db6;
                crc |= 0x00000001;
            }
            currentbyte >>= 1;
        }
    }

    for (uint8_t i = 0; i < 32; i++) {
        flippedcrc <<= 1;
        bit = crc & 1;
        crc >>= 1;
        flippedcrc += bit;
    }
    return (flippedcrc ^ 0xffffffff) & 0xffff;
}

static uint8_t
tulip_pci_read(UNUSED(int func), int addr, void *priv)
{
    const TULIPState *s = (TULIPState *) priv;
    uint8_t ret = 0;

    switch (addr) {
        case 0x00:
            ret = 0x11;
            break;
        case 0x01:
            ret = 0x10;
            break;
        case 0x02:
            if (s->device_info->local == 3)
                ret = 0x02;
            else if (s->device_info->local)
                ret = 0x09;
            else
                ret = 0x19;
            break;
        case 0x03:
            ret = 0x00;
            break;
        case 0x04:
            ret = s->pci_conf[0x04];
            break;
        case 0x05:
            ret = s->pci_conf[0x05];
            break;
        case 0x06:
            ret = 0x80;
            break;
        case 0x07:
            ret = 0x02;
            break;
        case 0x08:
            ret = 0x20;
            break;
        case 0x09:
            ret = 0x00;
            break;
        case 0x0A:
            ret = 0x00;
            break;
        case 0x0B:
            ret = 0x02;
            break;
        case 0x10:
            ret = (tulip_pci_bar[0].addr_regs[0] & 0x80) | 0x01;
            break;
        case 0x11:
            ret = tulip_pci_bar[0].addr_regs[1];
            break;
        case 0x12:
            ret = tulip_pci_bar[0].addr_regs[2];
            break;
        case 0x13:
            ret = tulip_pci_bar[0].addr_regs[3];
            break;
#ifdef USE_128_BYTE_BAR
        case 0x14:
            ret = (tulip_pci_bar[1].addr_regs[0] & 0x80);
            break;
#endif
        case 0x15:
#ifdef USE_128_BYTE_BAR
            ret = tulip_pci_bar[1].addr_regs[1];
#else
            ret = tulip_pci_bar[1].addr_regs[1] & 0xf0;
#endif
            break;
        case 0x16:
            ret = tulip_pci_bar[1].addr_regs[2];
            break;
        case 0x17:
            ret = tulip_pci_bar[1].addr_regs[3];
            break;
        case 0x2C:
            ret = s->subsys_ven_id & 0xFF;
            break;
        case 0x2D:
            ret = s->subsys_ven_id >> 8;
            break;
        case 0x2E:
            ret = s->subsys_id & 0xFF;
            break;
        case 0x2F:
            ret = s->subsys_id >> 8;
            break;
        case 0x30:
            ret = (tulip_pci_bar[2].addr_regs[0] & 0x01);
            break;
        case 0x31:
            ret = tulip_pci_bar[2].addr_regs[1];
            break;
        case 0x32:
            ret = tulip_pci_bar[2].addr_regs[2];
            break;
        case 0x33:
            ret = tulip_pci_bar[2].addr_regs[3];
            break;
        case 0x3C:
            ret = s->pci_conf[0x3C];
            break;
        case 0x3D:
            ret = PCI_INTA;
            break;
        case 0x3E:
        case 0x3F:
        case 0x41:
            ret = s->pci_conf[addr & 0xff];
            break;
    }

    //pclog("PCI read=%02x, ret=%02x.\n", addr, ret);
    return ret;
}

static void
tulip_pci_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    TULIPState *s = (TULIPState *) priv;

    //pclog("PCI write=%02x, ret=%02x.\n", addr, val);
    switch (addr) {
        case 0x04:
            s->pci_conf[0x04] = val & 0x07;
            //pclog("PCI write cmd: IOBase=%04x, MMIOBase=%08x, val=%02x.\n", s->PCIBase, s->MMIOBase, s->pci_conf[0x04]);
            io_removehandler(s->PCIBase, 128,
                         tulip_readb_io, tulip_readw_io, tulip_readl_io,
                         tulip_writeb_io, tulip_writew_io, tulip_writel_io,
                         priv);
            if ((s->PCIBase != 0) && (val & PCI_COMMAND_IO))
                io_sethandler(s->PCIBase, 128,
                         tulip_readb_io, tulip_readw_io, tulip_readl_io,
                         tulip_writeb_io, tulip_writew_io, tulip_writel_io,
                         priv);
            //pclog("PCI write cmd: IOBase=%04x, MMIOBase=%08x, val=%02x.\n", s->PCIBase, s->MMIOBase, s->pci_conf[0x04]);
            mem_mapping_disable(&s->memory);
            if ((s->MMIOBase != 0) && (val & PCI_COMMAND_MEM))
                mem_mapping_enable(&s->memory);
            break;
        case 0x05:
            s->pci_conf[0x05] = val & 1;
            break;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            io_removehandler(s->PCIBase, 128,
                             tulip_readb_io, tulip_readw_io, tulip_readl_io,
                             tulip_writeb_io, tulip_writew_io, tulip_writel_io,
                             priv);
            tulip_pci_bar[0].addr_regs[addr & 3] = val;
            tulip_pci_bar[0].addr &= 0xffffff80;
            s->PCIBase = tulip_pci_bar[0].addr;
            if (s->pci_conf[0x4] & PCI_COMMAND_IO) {
                //pclog("PCI write=%02x, base=%04x, io?=%x.\n", addr, s->PCIBase, s->pci_conf[0x4] & PCI_COMMAND_IO);
                if (s->PCIBase != 0)
                    io_sethandler(s->PCIBase, 128,
                             tulip_readb_io, tulip_readw_io, tulip_readl_io,
                             tulip_writeb_io, tulip_writew_io, tulip_writel_io,
                             priv);
            }
            break;
#ifndef USE_128_BYTE_BAR
        case 0x14:
#endif
        case 0x15:
        case 0x16:
        case 0x17:
            mem_mapping_disable(&s->memory);
            tulip_pci_bar[1].addr_regs[addr & 3] = val;
#ifdef USE_128_BYTE_BAR
            tulip_pci_bar[1].addr &= 0xffffff80;
#else
            tulip_pci_bar[1].addr &= 0xfffff000;
#endif
            s->MMIOBase = tulip_pci_bar[1].addr;
            if (s->pci_conf[0x4] & PCI_COMMAND_MEM) {
                //pclog("PCI write=%02x, mmiobase=%08x, mmio?=%x.\n", addr, s->PCIBase, s->pci_conf[0x4] & PCI_COMMAND_MEM);
                if (s->MMIOBase != 0)
#ifdef USE_128_BYTE_BAR
                    mem_mapping_set_addr(&s->memory, s->MMIOBase, 128);
#else
                    mem_mapping_set_addr(&s->memory, s->MMIOBase, 4096);
#endif
            }
            break;
        case 0x30: /* PCI_ROMBAR */
        case 0x31: /* PCI_ROMBAR */
        case 0x32: /* PCI_ROMBAR */
        case 0x33: /* PCI_ROMBAR */
            if (!s->has_bios)
                return;

            mem_mapping_disable(&s->bios_rom.mapping);
            tulip_pci_bar[2].addr_regs[addr & 3] = val;
            tulip_pci_bar[2].addr &= 0xffff0001;
            s->bios_addr = tulip_pci_bar[2].addr & 0xffff0000;
            if (tulip_pci_bar[2].addr_regs[0] & 0x01) {
                if (s->bios_addr != 0)
                    mem_mapping_set_addr(&s->bios_rom.mapping, s->bios_addr, 0x10000);
            }
            return;
        case 0x3C:
            s->pci_conf[0x3c] = val;
            return;
        case 0x3E:
        case 0x3F:
        case 0x41:
            s->pci_conf[addr & 0xff] = val;
            return;
    }
}

static void *
nic_init(const device_t *info)
{
    nmc93cxx_eeprom_params_t params;
    TULIPState              *s              = calloc(1, sizeof(TULIPState));
    char                     filename[1024] = { 0 };
    uint32_t                 mac;
    uint8_t                 *eeprom_data;

    if (!s)
        return NULL;

    if (info->local && info->local != 3) {
        s->bios_addr = 0xD0000;
        s->has_bios  = device_get_config_int("bios");
    } else {
        s->bios_addr = 0;
        s->has_bios  = 0;
    }

#ifdef USE_128_BYTE_BAR
    mem_mapping_add(&s->memory, 0x0fffff00, 128, tulip_mem_readb, tulip_mem_readw, tulip_mem_readl, tulip_mem_writeb, tulip_mem_writew, tulip_mem_writel, NULL, MEM_MAPPING_EXTERNAL, s);
#else
    mem_mapping_add(&s->memory, 0x0ffff000, 4096, tulip_mem_readb, tulip_mem_readw, tulip_mem_readl, tulip_mem_writeb, tulip_mem_writew, tulip_mem_writel, NULL, MEM_MAPPING_EXTERNAL, s);
#endif
    mem_mapping_disable(&s->memory);

    s->device_info = info;

    if (info->local != 3) {
        if (info->local == 2) {
           /*Subsystem Vendor ID*/
            s->eeprom_data[0] = 0x00;
            s->eeprom_data[1] = 0x0a;

            /*Subsystem ID*/
            s->eeprom_data[2] = 0x14;
            s->eeprom_data[3] = 0x21;
        } else {
           /*Subsystem Vendor ID*/
            s->eeprom_data[0] = info->local ? 0x25 : 0x11;
            s->eeprom_data[1] = 0x10;

            /*Subsystem ID*/
            s->eeprom_data[2] = info->local ? 0x10 : 0x0a;
            s->eeprom_data[3] = info->local ? 0x03 : 0x50;
        }

        /*Cardbus CIS Pointer low*/
        s->eeprom_data[4] = 0x00;
        s->eeprom_data[5] = 0x00;

        /*Cardbus CIS Pointer high*/
        s->eeprom_data[6] = 0x00;
        s->eeprom_data[7] = 0x00;

        /*ID Reserved1*/
        for (int i = 0; i < 7; i++)
            s->eeprom_data[8 + i] = 0x00;

        /*MiscHwOptions*/
        s->eeprom_data[15] = 0x00;

        /*ID_BLOCK_CRC*/
        tulip_idblock_crc((uint16_t *) s->eeprom_data);

        /*Func0_HwOptions*/
        s->eeprom_data[17] = 0x00;

        /*SROM Format Version 1, compatible with older guests*/
        s->eeprom_data[18] = 0x01;

        /*Controller Count*/
        s->eeprom_data[19] = 0x01;

        /*DEC OID*/
        s->eeprom_data[20] = 0x00;
        s->eeprom_data[21] = 0x00;
        s->eeprom_data[22] = 0xf8;

        if (info->local == 2) {
            /* Microsoft VPC DEC Tulip. */
            s->eeprom_data[20] = 0x00;
            s->eeprom_data[21] = 0x03;
            s->eeprom_data[22] = 0x0f;
        }

        /* See if we have a local MAC address configured. */
        mac = device_get_config_mac("mac", -1);

        /* Set up our BIA. */
        if (mac & 0xff000000) {
            /* Generate new local MAC. */
            s->eeprom_data[23] = random_generate();
            s->eeprom_data[24] = random_generate();
            s->eeprom_data[25] = random_generate();
            mac              = (((int) s->eeprom_data[23]) << 16);
            mac             |= (((int) s->eeprom_data[24]) << 8);
            mac             |= ((int) s->eeprom_data[25]);
            device_set_config_mac("mac", mac);
        } else {
            s->eeprom_data[23] = (mac >> 16) & 0xff;
            s->eeprom_data[24] = (mac >> 8) & 0xff;
            s->eeprom_data[25] = (mac & 0xff);
        }

        /*Controller_0 Device_Number*/
        s->eeprom_data[26] = 0x00;

        /*Controller_0 Info Leaf_Offset*/
        s->eeprom_data[27] = 0x1e;
        s->eeprom_data[28] = 0x00;

        /*Selected Connection Type, Powerup AutoSense and Dynamic AutoSense if the board supports it*/
        s->eeprom_data[30] = 0x00;
        s->eeprom_data[31] = 0x08;

        if (info->local) {
            /*General Purpose Control*/
            s->eeprom_data[32] = 0xff;

            /*Block Count*/
            s->eeprom_data[33] = 0x01;

            /*Extended Format (first part)*/
            /*Length (0:6) and Format Indicator (7)*/
            s->eeprom_data[34] = 0x81;

            /*Block Type (first part)*/
            s->eeprom_data[35] = 0x01;

            /*Extended Format (second part) - Block Type 0 for 21140*/
            /*Length (0:6) and Format Indicator (7)*/
            s->eeprom_data[36] = 0x85;

            /*Block Type (second part)*/
            s->eeprom_data[37] = 0x00;

            /*Media Code (0:5), EXT (6), Reserved (7)*/
            s->eeprom_data[38] = 0x01;

            /*General Purpose Data*/
            s->eeprom_data[39] = 0x00;

            /*Command*/
            s->eeprom_data[40] = 0x00;
            s->eeprom_data[41] = 0x00;
        } else {
            /*SROM Format Version 3*/
            s->eeprom_data[18] = 0x03;

            /*Block Count*/
            s->eeprom_data[32] = 0x01;

            /*Extended Format - Block Type 2 for 21142/21143*/
            /*Length (0:6) and Format Indicator (7)*/
            s->eeprom_data[33] = 0x86;

            /*Block Type*/
            s->eeprom_data[34] = 0x02;

            /*Media Code (0:5), EXT (6), Reserved (7)*/
            s->eeprom_data[35] = 0x01;

            /*General Purpose Control*/
            s->eeprom_data[36] = 0xff;
            s->eeprom_data[37] = 0xff;

            /*General Purpose Data*/
            s->eeprom_data[38] = 0x00;
            s->eeprom_data[39] = 0x00;
        }

        s->eeprom_data[126] = tulip_srom_crc(s->eeprom_data) & 0xff;
        s->eeprom_data[127] = tulip_srom_crc(s->eeprom_data) >> 8;
    } else {
        uint32_t checksum = 0;
        /* 21040 is supposed to only have MAC address in its serial ROM if Linux is correct. */
        memset(s->eeprom_data, 0, sizeof(s->eeprom_data));
        /* See if we have a local MAC address configured. */
        mac = device_get_config_mac("mac", -1);
        /*DEC OID*/
        s->eeprom_data[0] = 0x00;
        s->eeprom_data[1] = 0x00;
        s->eeprom_data[2] = 0xF8;
        if (mac & 0xff000000) {
            /* Generate new local MAC. */
            s->eeprom_data[3] = random_generate();
            s->eeprom_data[4] = random_generate();
            s->eeprom_data[5] = random_generate();
            mac              = (((int) s->eeprom_data[3]) << 16);
            mac             |= (((int) s->eeprom_data[4]) << 8);
            mac             |= ((int) s->eeprom_data[5]);
            device_set_config_mac("mac", mac);
        } else {
            s->eeprom_data[3] = (mac >> 16) & 0xff;
            s->eeprom_data[4] = (mac >> 8) & 0xff;
            s->eeprom_data[5] = (mac & 0xff);
        }

        /* Generate checksum. */
        checksum = (s->eeprom_data[0] * 256) | s->eeprom_data[1];
        checksum *= 2;
        if (checksum > 65535)
            checksum = checksum % 65535;

        /* 2nd pair. */
        checksum += (s->eeprom_data[2] * 256) | s->eeprom_data[3];
        if (checksum > 65535)
            checksum = checksum % 65535;
        checksum *= 2;
        if (checksum > 65535)
            checksum = checksum % 65535;
        
        /* 3rd pair. */
        checksum += (s->eeprom_data[4] * 256) | s->eeprom_data[5];
        if (checksum > 65535)
            checksum = checksum % 65535;

        if (checksum >= 65535)
            checksum = 0;
        
        s->eeprom_data[6] = (checksum >> 8) & 0xFF;
        s->eeprom_data[7] = checksum & 0xFF;
    }

    if (info->local != 3) {
        params.nwords          = 64;
        params.default_content = (uint16_t *) s->eeprom_data;
        params.filename        = filename;
        snprintf(filename, sizeof(filename), "nmc93cxx_eeprom_%s_%d.nvr", info->internal_name, device_get_instance());
        s->eeprom = device_add_params(&nmc93cxx_device, &params);
        if (s->eeprom == NULL) {
            free(s);
            return NULL;
        }
    }

    tulip_pci_bar[0].addr_regs[0] = 1;
    tulip_pci_bar[1].addr_regs[0] = 0;
    s->pci_conf[0x04]             = 7;

    /* Enable our BIOS space in PCI, if needed. */
    if (s->has_bios) {
        rom_init(&s->bios_rom, ROM_PATH_DEC21140, s->bios_addr, 0x10000, 0xffff, 0, MEM_MAPPING_EXTERNAL);
        tulip_pci_bar[2].addr         = 0xffff0000;
    } else
        tulip_pci_bar[2].addr         = 0;

    mem_mapping_disable(&s->bios_rom.mapping);
    eeprom_data = (info->local == 3) ? s->eeprom_data : (uint8_t *) &nmc93cxx_eeprom_data(s->eeprom)[0];

    //pclog("EEPROM Data Format=%02x, Count=%02x, MAC=%02x:%02x:%02x:%02x:%02x:%02x.\n", eeprom_data[0x12], eeprom_data[0x13], eeprom_data[0x14], eeprom_data[0x15], eeprom_data[0x16], eeprom_data[0x17], eeprom_data[0x18], eeprom_data[0x19]);
    memcpy(s->mii_regs, tulip_mdi_default, sizeof(tulip_mdi_default));
    s->nic = network_attach(s, &eeprom_data[(info->local == 3) ? 0 : 20], tulip_receive, NULL);
    pci_add_card(PCI_ADD_NORMAL, tulip_pci_read, tulip_pci_write, s, &s->pci_slot);
    tulip_reset(s);
    return s;
}

static void
nic_close(void *priv)
{
    free(priv);
}

// clang-format off
static const device_config_t dec_tulip_21143_config[] = {
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

static const device_config_t dec_tulip_21140_config[] = {
    {
        .name           = "bios",
        .description    = "Enable BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
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

const device_t dec_tulip_device = {
    .name          = "DEC DE-500A Fast Ethernet (DECchip 21143 \"Tulip\")",
    .internal_name = "dec_21143_tulip",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = tulip_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = dec_tulip_21143_config
};

const device_t dec_tulip_21140_device = {
    .name          = "DEC 21140 Fast Ethernet (DECchip 21140 \"Tulip FasterNet\")",
    .internal_name = "dec_21140_tulip",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = tulip_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = dec_tulip_21140_config
};

const device_t dec_tulip_21140_vpc_device = {
    .name          = "Microsoft Virtual PC Network (DECchip 21140 \"Tulip FasterNet\")",
    .internal_name = "dec_21140_tulip_vpc",
    .flags         = DEVICE_PCI,
    .local         = 2,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = tulip_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = dec_tulip_21140_config
};

const device_t dec_tulip_21040_device = {
    .name          = "DEC DE-435 EtherWorks Turbo (DECchip 21040 \"Tulip\")",
    .internal_name = "dec_21040_tulip",
    .flags         = DEVICE_PCI,
    .local         = 3,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = tulip_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = dec_tulip_21143_config
};
