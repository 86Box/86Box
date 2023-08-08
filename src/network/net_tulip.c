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
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/dma.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/net_eeprom_nmc93cxx.h>
#include <86box/net_tulip.h>
#include <86box/bswap.h>
#include <86box/plat_unused.h>

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

struct tulip_descriptor {
    uint32_t status;
    uint32_t control;
    uint32_t buf_addr1;
    uint32_t buf_addr2;
};

struct TULIPState {
    uint8_t            pci_slot;
    uint8_t            irq_state;
    const device_t    *device_info;
    uint16_t           subsys_id;
    uint16_t           subsys_ven_id;
    mem_mapping_t      memory;
    netcard_t         *nic;
    nmc93cxx_eeprom_t *eeprom;
    uint32_t           csr[16];
    uint8_t            pci_conf[256];
    uint16_t           mii_regs[32];

    /* state for MII */
    uint32_t old_csr9;
    uint32_t mii_word;
    uint32_t mii_bitcnt;

    uint32_t current_rx_desc;
    uint32_t current_tx_desc;

    uint8_t  rx_frame[2048];
    uint8_t  tx_frame[2048];
    uint16_t tx_frame_len;
    uint16_t rx_frame_len;
    uint16_t rx_frame_size;

    uint32_t rx_status;
    uint8_t  filter[16][6];
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

        dma_bm_write(desc->buf_addr1, s->rx_frame + (s->rx_frame_size - s->rx_frame_len), len, 1);
        s->rx_frame_len -= len;
    }

    if (s->rx_frame_len && len2) {
        if (s->rx_frame_len > len2) {
            len = len2;
        } else {
            len = s->rx_frame_len;
        }

        dma_bm_write(desc->buf_addr2, s->rx_frame + (s->rx_frame_size - s->rx_frame_len), len, 1);
        s->rx_frame_len -= len;
    }
}

static bool
tulip_filter_address(TULIPState *s, const uint8_t *addr)
{
    static const char broadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    bool              ret         = false;

    for (uint8_t i = 0; i < 16 && ret == false; i++) {
        if (!memcmp(&s->filter[i], addr, ETH_ALEN)) {
            ret = true;
        }
    }

    if (!memcmp(addr, broadcast, ETH_ALEN)) {
        return true;
    }

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

    if (size < 14 || size > sizeof(s->rx_frame) - 4
        || s->rx_frame_len || tulip_rx_stopped(s)) {
        return 0;
    }

    if (!tulip_filter_address(s, buf)) {
        return 1;
    }

    do {
        tulip_desc_read(s, s->current_rx_desc, &desc);

        if (!(desc.status & RDES0_OWN)) {
            s->csr[5] |= CSR5_RU;
            tulip_update_int(s);
            return s->rx_frame_size - s->rx_frame_len;
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
    } while (s->rx_frame_len);
    return 1;
}

static const char *
tulip_reg_name(const uint32_t addr)
{
    switch (addr) {
        case CSR(0):
            return "CSR0";

        case CSR(1):
            return "CSR1";

        case CSR(2):
            return "CSR2";

        case CSR(3):
            return "CSR3";

        case CSR(4):
            return "CSR4";

        case CSR(5):
            return "CSR5";

        case CSR(6):
            return "CSR6";

        case CSR(7):
            return "CSR7";

        case CSR(8):
            return "CSR8";

        case CSR(9):
            return "CSR9";

        case CSR(10):
            return "CSR10";

        case CSR(11):
            return "CSR11";

        case CSR(12):
            return "CSR12";

        case CSR(13):
            return "CSR13";

        case CSR(14):
            return "CSR14";

        case CSR(15):
            return "CSR15";

        default:
            break;
    }
    return "";
}

static const char *
tulip_rx_state_name(int state)
{
    switch (state) {
        case CSR5_RS_STOPPED:
            return "STOPPED";

        case CSR5_RS_RUNNING_FETCH:
            return "RUNNING/FETCH";

        case CSR5_RS_RUNNING_CHECK_EOR:
            return "RUNNING/CHECK EOR";

        case CSR5_RS_RUNNING_WAIT_RECEIVE:
            return "WAIT RECEIVE";

        case CSR5_RS_SUSPENDED:
            return "SUSPENDED";

        case CSR5_RS_RUNNING_CLOSE:
            return "RUNNING/CLOSE";

        case CSR5_RS_RUNNING_FLUSH:
            return "RUNNING/FLUSH";

        case CSR5_RS_RUNNING_QUEUE:
            return "RUNNING/QUEUE";

        default:
            break;
    }
    return "";
}

static const char *
tulip_tx_state_name(int state)
{
    switch (state) {
        case CSR5_TS_STOPPED:
            return "STOPPED";

        case CSR5_TS_RUNNING_FETCH:
            return "RUNNING/FETCH";

        case CSR5_TS_RUNNING_WAIT_EOT:
            return "RUNNING/WAIT EOT";

        case CSR5_TS_RUNNING_READ_BUF:
            return "RUNNING/READ BUF";

        case CSR5_TS_RUNNING_SETUP:
            return "RUNNING/SETUP";

        case CSR5_TS_SUSPENDED:
            return "SUSPENDED";

        case CSR5_TS_RUNNING_CLOSE:
            return "RUNNING/CLOSE";

        default:
            break;
    }
    return "";
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
static const uint16_t tulip_mdi_mask[] = {
    /* MDI Registers 0 - 6, 7 */
    0x0000,
    0xffff,
    0xffff,
    0xffff,
    0xc01f,
    0xffff,
    0xffff,
    0x0000,
    /* MDI Registers 8 - 15 */
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    /* MDI Registers 16 - 31 */
    0x0fff,
    0x0000,
    0xffff,
    0xffff,
    0xffff,
    0xffff,
    0xffff,
    0xffff,
    0xffff,
    0xffff,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
};

extern uint16_t l80225_mii_readw(uint16_t* regs, uint16_t addr);
extern void l80225_mii_writew(uint16_t* regs, uint16_t addr, uint16_t val);

static uint16_t
tulip_mii_read(TULIPState *s, int phy, int reg)
{
    uint16_t ret = 0;
    if (phy == 1) {
        ret = l80225_mii_readw(s->mii_regs, reg);
    }
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
        return;
    }

    if (!(s->csr[9] & CSR9_MDC)) {
        return;
    }

    s->mii_bitcnt++;
    s->mii_word <<= 1;

    if (s->csr[9] & CSR9_MDO && (s->mii_bitcnt < 16 || !(s->csr[9] & CSR9_MII))) {
        /* write op or address bits */
        s->mii_word |= 1;
    }

    if (s->mii_bitcnt >= 16 && (s->csr[9] & CSR9_MII)) {
        if (s->mii_word & 0x8000) {
            s->csr[9] |= CSR9_MDI;
        } else {
            s->csr[9] &= ~CSR9_MDI;
        }
    }

    if (s->mii_word == 0xffffffff) {
        s->mii_bitcnt = 0;
    } else if (s->mii_bitcnt == 16) {
        op  = (s->mii_word >> 12) & 0x0f;
        phy = (s->mii_word >> 7) & 0x1f;
        reg = (s->mii_word >> 2) & 0x1f;

        if (op == 6) {
            s->mii_word = tulip_mii_read(s, phy, reg);
        }
    } else if (s->mii_bitcnt == 32) {
        op   = (s->mii_word >> 28) & 0x0f;
        phy  = (s->mii_word >> 23) & 0x1f;
        reg  = (s->mii_word >> 18) & 0x1f;
        data = s->mii_word & 0xffff;

        if (op == 5) {
            tulip_mii_write(s, phy, reg, data);
        }
    }
}

static uint32_t
tulip_csr9_read(TULIPState *s)
{
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
            /* Fake autocompletion complete until we have PHY emulation */
            data = 5 << CSR12_ANS_SHIFT;
            break;

        default:
            if (!(addr & 7))
                data = s->csr[addr >> 3];
            break;
    }
    return data;
}

static void
tulip_tx(TULIPState *s, struct tulip_descriptor *desc)
{
    if (s->tx_frame_len) {
        if ((s->csr[6] >> CSR6_OM_SHIFT) & CSR6_OM_MASK) {
            /* Internal or external Loopback */
            tulip_receive(s, s->tx_frame, s->tx_frame_len);
        } else if (s->tx_frame_len <= sizeof(s->tx_frame)) {
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
                    s->tx_frame + s->tx_frame_len, len1, 1);
        s->tx_frame_len += len1;
    }

    if (s->tx_frame_len + len2 > sizeof(s->tx_frame)) {
        return -1;
    }
    if (len2) {
        dma_bm_read(desc->buf_addr2,
                    s->tx_frame + s->tx_frame_len, len2, 1);
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
        dma_bm_read(desc->buf_addr1, buf, len, 1);
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
    s->csr[5]                   = 0xf0000000;
    s->csr[6]                   = 0x32000040;
    s->csr[7]                   = 0xf3fe0000;
    s->csr[8]                   = 0xe0000000;
    s->csr[9]                   = 0xfff483ff;
    s->csr[11]                  = 0xfffe0000;
    s->csr[12]                  = 0x000000c6;
    s->csr[13]                  = 0xffff0000;
    s->csr[14]                  = 0xffffffff;
    s->csr[15]                  = 0x8ff00000;
    s->subsys_id                = eeprom_data[1];
    s->subsys_ven_id            = eeprom_data[0];
}

static void
tulip_write(uint32_t addr, uint32_t data, void *opaque)
{
    TULIPState *s = opaque;
    addr &= 127;

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
            s->csr[9] = data;
            break;

        case CSR(9):
            tulip_csr9_write(s, s->csr[9], data);
            /* don't clear MII read data */
            s->csr[9] &= CSR9_MDI;
            s->csr[9] |= (data & ~CSR9_MDI);
            tulip_mii(s);
            s->old_csr9 = s->csr[9];
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
tulip_write_io(uint16_t addr, uint32_t data, void *opaque)
{
    return tulip_write(addr, data, opaque);
}

static uint32_t
tulip_read_io(uint16_t addr, void *opaque)
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
tulip_srom_crc(uint8_t *eeprom, size_t len)
{
    unsigned long crc        = 0xffffffff;
    unsigned long flippedcrc = 0;
    unsigned char currentbyte;
    unsigned int  msb;
    unsigned int  bit;

    for (size_t i = 0; i < len; i++) {
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

static const uint8_t eeprom_default[128] = {
    0xf0,
    0x11,
    0x35,
    0x42,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x56,
    0x08,
    0x04,
    0x01,
    0x00,
    0x80,
    0x48,
    0xb3,
    0x0e,
    0xa7,
    0x00,
    0x1e,
    0x00,
    0x00,
    0x00,
    0x08,
    0x01,
    0x8d,
    0x03,
    0x00,
    0x00,
    0x00,
    0x00,
    0x78,
    0xe0,
    0x01,
    0x00,
    0x50,
    0x00,
    0x18,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0xe8,
    0x6b,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x80,
    0x48,
    0xb3,
    0x0e,
    0xa7,
    0x40,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

static const uint8_t eeprom_default_24110[128] = {
    0x46,
    0x26,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x56,
    0x08,
    0x04,
    0x01,
    0x00, /* TODO: Change the MAC Address to the correct one. */
    0x80,
    0x48,
    0xc3,
    0x3e,
    0xa7,
    0x00,
    0x1e,
    0x00,
    0x00,
    0x00,
    0x08,
    0xff,
    0x01,
    0x8d,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x78,
    0xe0,
    0x01,
    0x00,
    0x50,
    0x00,
    0x18,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0xe8,
    0x6b,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x80,
    0x48,
    0xb3,
    0x0e,
    0xa7,
    0x40,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

static void
tulip_fill_eeprom(TULIPState *s)
{
    uint16_t *eeprom = nmc93cxx_eeprom_data(s->eeprom);
    memcpy(eeprom, eeprom_default, 128);

    tulip_idblock_crc(eeprom);
    eeprom[63] = (tulip_srom_crc((uint8_t *) eeprom, 126));
}

static uint8_t
tulip_pci_read(UNUSED(int func), int addr, void *priv)
{
    const TULIPState *s = (TULIPState *) priv;

    switch (addr) {
        default:
            return s->pci_conf[addr & 0xFF];
        case 0x00:
            return 0x11;
        case 0x01:
            return 0x10;
        case 0x02:
            return s->device_info->local ? 0x09 : 0x19;
        case 0x03:
            return 0x00;
        case 0x07:
            return 0x02;
        case 0x06:
            return 0x80;
        case 0x5:
            return s->pci_conf[addr & 0xFF] & 1;
        case 0x8:
            return 0x30;
        case 0x9:
            return 0x0;
        case 0xA:
            return 0x0;
        case 0xB:
            return 0x2;
        case 0x10:
            return (s->pci_conf[addr & 0xFF] & 0x80) | 1;
        case 0x14:
            return s->pci_conf[addr & 0xFF] & 0x80;
        case 0x2C:
            return s->subsys_ven_id & 0xFF;
        case 0x2D:
            return s->subsys_ven_id >> 8;
        case 0x2E:
            return s->subsys_id & 0xFF;
        case 0x2F:
            return s->subsys_id >> 8;
        case 0x3D:
            return PCI_INTA;
    }
}

static void
tulip_pci_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    TULIPState *s = (TULIPState *) priv;

    switch (addr) {
        case 0x4:
            mem_mapping_disable(&s->memory);
            io_removehandler((s->pci_conf[0x10] & 0x80) | (s->pci_conf[0x11] << 8), 128,
                            NULL, NULL, tulip_read_io,
                            NULL, NULL, tulip_write_io,
                            priv);
            s->pci_conf[addr & 0xFF] = val;
            if (val & PCI_COMMAND_IO)
                io_sethandler((s->pci_conf[0x10] & 0x80) | (s->pci_conf[0x11] << 8), 128,
                              NULL, NULL, tulip_read_io,
                              NULL, NULL, tulip_write_io,
                              priv);
            if ((val & PCI_COMMAND_MEM) && s->memory.size)
                mem_mapping_enable(&s->memory);
            break;
        case 0x5:
            s->pci_conf[addr & 0xFF] = val & 1;
            break;
        case 0x10:
        case 0x11:
            io_removehandler((s->pci_conf[0x10] & 0x80) | (s->pci_conf[0x11] << 8), 128,
                             NULL, NULL, tulip_read_io,
                             NULL, NULL, tulip_write_io,
                             priv);
            s->pci_conf[addr & 0xFF] = val;
            if (s->pci_conf[0x4] & PCI_COMMAND_IO)
                io_sethandler((s->pci_conf[0x10] & 0x80) | (s->pci_conf[0x11] << 8), 128,
                              NULL, NULL, tulip_read_io,
                              NULL, NULL, tulip_write_io,
                              priv);
            break;
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            s->pci_conf[addr & 0xFF] = val;
            if (s->pci_conf[0x4] & PCI_COMMAND_MEM)
                mem_mapping_set_addr(&s->memory, (s->pci_conf[0x14] & 0x80) | (s->pci_conf[0x15] << 8) | (s->pci_conf[0x16] << 16) | (s->pci_conf[0x17] << 24), 128);
            break;
        case 0x3C:
            s->pci_conf[addr & 0xFF] = val;
            break;
    }
}

static void *
nic_init(const device_t *info)
{
    uint8_t                  eeprom_default_local[128];
    nmc93cxx_eeprom_params_t params;
    TULIPState              *s              = calloc(1, sizeof(TULIPState));
    char                     filename[1024] = { 0 };

    if (!s)
        return NULL;
    
    s->device_info = info;
    memcpy(eeprom_default_local, s->device_info->local ? eeprom_default_24110 : eeprom_default, sizeof(eeprom_default));
    tulip_idblock_crc((uint16_t *) eeprom_default_local);
    ((uint16_t *) eeprom_default_local)[63] = (tulip_srom_crc((uint8_t *) eeprom_default_local, 126));

    params.nwords          = 64;
    params.default_content = (uint16_t *) eeprom_default_local;
    params.filename        = filename;
    snprintf(filename, sizeof(filename), "nmc93cxx_eeprom_%s_%d.nvr", info->internal_name, device_get_instance());
    s->eeprom = device_add_parameters(&nmc93cxx_device, &params);
    if (!s->eeprom) {
        free(s);
        return NULL;
    }
    memcpy(s->mii_regs, tulip_mdi_default, sizeof(tulip_mdi_default));
    s->nic = network_attach(s, (uint8_t *) &nmc93cxx_eeprom_data(s->eeprom)[10], tulip_receive, NULL);
    pci_add_card(PCI_ADD_NETWORK, tulip_pci_read, tulip_pci_write, s, &s->pci_slot);
    mem_mapping_add(&s->memory, 0, 0, NULL, NULL, tulip_read, NULL, NULL, tulip_write, NULL, MEM_MAPPING_EXTERNAL, s);
    tulip_reset(s);
    return s;
}

static void
nic_close(void *priv)
{
    free(priv);
}

const device_t dec_tulip_device = {
    .name          = "Compu-Shack FASTLine-II UTP 10/100 (DECchip 21143 \"Tulip\")",
    .internal_name = "dec_21143_tulip",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = tulip_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t dec_tulip_21140_device = {
    .name          = "Kingston KNE100TX (DECchip 21140 \"Tulip FasterNet\")",
    .internal_name = "dec_21140_tulip",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = tulip_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
