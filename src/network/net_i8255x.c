/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Intel 8255x NIC.
 *
 *          Ported from the QEMU eepro100.c implementation by Stefan Weil.
 *
 * Authors: Stefan Weil,
 *          chun-awa,
 *
 *          Copyright 2006-2011 Stefan Weil.
 *          Copyright 2026      chun-awa.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
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
#include <86box/plat_unused.h>
#include <86box/bswap.h>

#define BIT(x) (1 << (x))

#ifdef ENABLE_I8255X_LOG
int i8255x_do_log = ENABLE_I8255X_LOG;

static void
i8255x_log(const char *fmt, ...)
{
    va_list ap;

    if (i8255x_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define i8255x_log(fmt, ...)
#endif

#define PCI_PERIOD 30 /* 30 ns period = 33.333333 Mhz frequency */

#define MAX_ETH_FRAME_SIZE 1514

/* This driver supports several different devices which are declared here. */
#define i82550      0x82550
#define i82551      0x82551
#define i82557A     0x82557a
#define i82557B     0x82557b
#define i82557C     0x82557c
#define i82558A     0x82558a
#define i82558B     0x82558b
#define i82559A     0x82559a
#define i82559B     0x82559b
#define i82559C     0x82559c
#define i82559ER    0x82559e
#define i82562      0x82562
#define i82801      0x82801

/* Use 64 word EEPROM. */
#define EEPROM_SIZE     64

#define PCI_MEM_SIZE            (4 * 1024)
#define PCI_IO_SIZE             64
#define PCI_FLASH_SIZE          (128 * 1024)

#define BITS(n, m) (((0xffffffffU << (31 - n)) >> (31 - n + m)) << m)

/* The SCB accepts the following controls for the Tx and Rx units: */
#define  CU_NOP         0x0000  /* No operation. */
#define  CU_START       0x0010  /* CU start. */
#define  CU_RESUME      0x0020  /* CU resume. */
#define  CU_STATSADDR   0x0040  /* Load dump counters address. */
#define  CU_SHOWSTATS   0x0050  /* Dump statistical counters. */
#define  CU_CMD_BASE    0x0060  /* Load CU base address. */
#define  CU_DUMPSTATS   0x0070  /* Dump and reset statistical counters. */
#define  CU_SRESUME     0x00a0  /* CU static resume. */
#define  CU_HPQ_START   0x0030  /* CU start on high priority queue. */
#define  CU_HPQ_RESUME  0x00b0  /* CU resume on high priority queue. */

#define  RU_NOP         0x0000
#define  RX_START       0x0001
#define  RX_RESUME      0x0002
#define  RU_ABORT       0x0004
#define  RX_ADDR_LOAD   0x0006
#define  RX_RESUMENR    0x0007
#define INT_MASK        0x0100
#define DRVR_INT        0x0200  /* Driver generated interrupt. */

typedef struct {
    const char *name;
    const char *desc;
    uint16_t device_id;
    uint8_t revision;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;

    uint32_t device;
    uint8_t stats_size;
    bool has_extended_tcb_support;
    bool power_management;
} e100_device_info_t;

/* Offsets to the various registers.
   All accesses need not be longword aligned. */
typedef enum {
    SCBStatus = 0,              /* Status Word. */
    SCBAck = 1,
    SCBCmd = 2,                 /* Rx/Command Unit command and status. */
    SCBIntmask = 3,
    SCBPointer = 4,             /* General purpose pointer. */
    SCBPort = 8,                /* Misc. commands and operands.  */
    SCBflash = 12,              /* Flash memory control. */
    SCBeeprom = 14,             /* EEPROM control. */
    SCBCtrlMDI = 16,            /* MDI interface control. */
    SCBEarlyRx = 20,            /* Early receive byte count. */
    SCBFlow = 24,               /* Flow Control. */
    SCBpmdr = 27,               /* Power Management Driver. */
    SCBgctrl = 28,              /* General Control. */
    SCBgstat = 29,              /* General Status. */
} e100_register_offset_t;

/* A speedo3 transmit buffer descriptor with two buffers... */
typedef struct {
    uint16_t status;
    uint16_t command;
    uint32_t link;              /* void * */
    uint32_t tbd_array_addr;    /* transmit buffer descriptor array address. */
    uint16_t tcb_bytes;         /* transmit command block byte count (in lower 14 bits */
    uint8_t tx_threshold;       /* transmit threshold */
    uint8_t tbd_count;          /* TBD number */
} eepro100_tx_t;

/* Receive frame descriptor. */
typedef struct {
    int16_t status;
    uint16_t command;
    uint32_t link;              /* struct RxFD * */
    uint32_t rx_buf_addr;       /* void * */
    uint16_t count;
    uint16_t size;
    /* Ethernet frame data follows. */
} eepro100_rx_t;

typedef enum {
    COMMAND_EL = BIT(15),
    COMMAND_S = BIT(14),
    COMMAND_I = BIT(13),
    COMMAND_NC = BIT(4),
    COMMAND_SF = BIT(3),
    COMMAND_CMD = BITS(2, 0),
} scb_command_bit;

typedef enum {
    STATUS_C = BIT(15),
    STATUS_OK = BIT(13),
} scb_status_bit;

typedef struct {
    uint32_t tx_good_frames, tx_max_collisions, tx_late_collisions,
             tx_underruns, tx_lost_crs, tx_deferred, tx_single_collisions,
             tx_multiple_collisions, tx_total_collisions;
    uint32_t rx_good_frames, rx_crc_errors, rx_alignment_errors,
             rx_resource_errors, rx_overrun_errors, rx_cdt_errors,
             rx_short_frame_errors;
    uint32_t fc_xmt_pause, fc_rcv_pause, fc_rcv_unsupported;
    uint16_t xmt_tco_frames, rcv_tco_frames;
    uint32_t reserved[4];
} eepro100_stats_t;

typedef enum {
    cu_idle = 0,
    cu_suspended = 1,
    cu_active = 2,
    cu_lpq_active = 2,
    cu_hqp_active = 3
} cu_state_t;

typedef enum {
    ru_idle = 0,
    ru_suspended = 1,
    ru_no_resources = 2,
    ru_ready = 4
} ru_state_t;

typedef struct {
    uint8_t  tick;
    uint8_t  address;
    uint8_t  command;
    uint8_t  writable;

    uint8_t eecs;
    uint8_t eesk;
    uint8_t eedo;

    uint8_t  addrbits;
    uint16_t size;
    uint16_t data;
    uint16_t contents[EEPROM_SIZE];
} eeprom_t;

typedef struct {
    uint8_t    pci_conf[256];
    uint8_t    pci_slot;
    uint8_t    irq_state;
    uint16_t   io_base;
    uint32_t   mem_base;
    uint32_t   flash_base;
    uint16_t   pci_device_id;
    uint8_t    pci_revision;
    uint16_t   pci_subsystem_vendor_id;
    uint16_t   pci_subsystem_id;

    mem_mapping_t bar_mem;
    mem_mapping_t bar_flash;

    /* Hash register (multicast mask array, multiple individual addresses). */
    uint8_t mult[8];
    netcard_t *nic;
    uint8_t mac[6];
    uint8_t scb_stat;           /* SCB stat/ack byte */
    uint8_t int_stat;           /* PCI interrupt status */
    /* region must not be saved. */
    uint16_t mdimem[32];
    eeprom_t *eeprom;
    uint32_t device;            /* device variant */
    /* (cu_base + cu_offset) address the next command block in the command block list. */
    uint32_t cu_base;           /* CU base address */
    uint32_t cu_offset;         /* CU address offset */
    uint32_t cu_offset_hpq;     /* CU address offset (high priority queue) */
    /* (ru_base + ru_offset) address the RFD in the Receive Frame Area. */
    uint32_t ru_base;           /* RU base address */
    uint32_t ru_offset;         /* RU address offset */
    uint32_t statsaddr;         /* pointer to eepro100_stats_t */

    /* Temporary status information (no need to save these values),
     * used while processing CU commands. */
    eepro100_tx_t tx;           /* transmit buffer descriptor */
    uint32_t cb_address;        /* = cu_base + cu_offset */

    /* Statistical counters. */
    eepro100_stats_t statistics;

    /* Data in mem is always in the byte order of the controller (le).
     * It must be dword aligned to allow direct access to 32 bit values. */
    uint8_t mem[PCI_MEM_SIZE] __attribute__((aligned(8)));

    /* Configuration bytes. */
    uint8_t configuration[22];

    /* Quasi static device properties (no need to save them). */
    uint16_t stats_size;
    bool has_extended_tcb_support;

    uint32_t link_state;
} eepro100_t;

/* The device variants, indexed by the .local field of the device_t. */
static const e100_device_info_t eepro100_devices[] = {
    {
        .name = "i82557b",
        .desc = "Intel i82557B Ethernet",
        .device = i82557B,
        .device_id = 0x1229,
        .revision = 0x02,
        .subsystem_vendor_id = 0x8086,
        .subsystem_id = 0x0001,
        .stats_size = 64,
        .has_extended_tcb_support = false,
        .power_management = false,
    },{
        .name = "i82557c",
        .desc = "Intel i82557C Ethernet",
        .device = i82557C,
        .device_id = 0x1229,
        .revision = 0x03,
        .subsystem_vendor_id = 0x8086,
        .subsystem_id = 0x0001,
        .stats_size = 64,
        .has_extended_tcb_support = false,
        .power_management = false,
    },{
        .name = "i82558b",
        .desc = "Intel i82558B Ethernet",
        .device = i82558B,
        .device_id = 0x1229,
        .revision = 0x05,
        .subsystem_vendor_id = 0x8086,
        .subsystem_id = 0x0001,
        .stats_size = 76,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82559a",
        .desc = "Intel i82559A Ethernet",
        .device = i82559A,
        .device_id = 0x1229,
        .revision = 0x06,
        .subsystem_vendor_id = 0x8086,
        .subsystem_id = 0x0001,
        .stats_size = 80,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82559b",
        .desc = "Intel i82559B Ethernet",
        .device = i82559B,
        .device_id = 0x1229,
        .revision = 0x07,
        .subsystem_vendor_id = 0x8086,
        .subsystem_id = 0x0001,
        .stats_size = 80,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82559c",
        .desc = "Intel i82559C Ethernet",
        .device = i82559C,
        .device_id = 0x1229,
        .revision = 0x08,
        .subsystem_vendor_id = 0x8086,
        .subsystem_id = 0x0040,
        .stats_size = 80,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82559er",
        .desc = "Intel i82559ER Ethernet",
        .device = i82559ER,
        .device_id = 0x1209,
        .revision = 0x09,
        .subsystem_vendor_id = 0x8086,
        .subsystem_id = 0x0001,
        .stats_size = 80,
        .has_extended_tcb_support = true,
        .power_management = true,
    }
};

/* Word indices in EEPROM. */
typedef enum {
    EEPROM_CNFG_MDIX  = 0x03,
    EEPROM_ID         = 0x05,
    EEPROM_PHY_ID     = 0x06,
    EEPROM_VENDOR_ID  = 0x0c,
    EEPROM_CONFIG_ASF = 0x0d,
    EEPROM_DEVICE_ID  = 0x23,
    EEPROM_SMBUS_ADDR = 0x90,
} eeprom_offset_t;

/* Bit values for EEPROM ID word. */
typedef enum {
    EEPROM_ID_MDM = BIT(0),     /* Modem */
    EEPROM_ID_STB = BIT(1),     /* Standby Enable */
    EEPROM_ID_WMR = BIT(2),     /* ??? */
    EEPROM_ID_WOL = BIT(5),     /* Wake on LAN */
    EEPROM_ID_DPD = BIT(6),     /* Deep Power Down */
    EEPROM_ID_ALT = BIT(7),     /* */
    /* BITS(10, 8) device revision */
    EEPROM_ID_BD = BIT(11),     /* boot disable */
    EEPROM_ID_ID = BIT(13),     /* id bit */
    /* BITS(15, 14) signature */
    EEPROM_ID_VALID = BIT(14),  /* signature for valid eeprom */
} eeprom_id_bit;

/* Default values for MDI (PHY) registers */
static const uint16_t eepro100_mdi_default[] = {
    /* MDI Registers 0 - 6, 7 */
    0x3000, 0x780d, 0x02a8, 0x0154, 0x05e1, 0x0000, 0x0000, 0x0000,
    /* MDI Registers 8 - 15 */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* MDI Registers 16 - 31 */
    0x0003, 0x0000, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

/* Readonly mask for MDI (PHY) registers */
static const uint16_t eepro100_mdi_mask[] = {
    0x0000, 0xffff, 0xffff, 0xffff, 0xc01f, 0xffff, 0xffff, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0fff, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

/* Read a 16 bit control/status (CSR) register. */
static uint16_t
e100_read_reg2(eepro100_t *s, uint8_t addr)
{
    return le16_to_cpupu((const uint16_t *) &s->mem[addr]);
}

/* Read a 32 bit control/status (CSR) register. */
static uint32_t
e100_read_reg4(eepro100_t *s, uint8_t addr)
{
    return le32_to_cpupu((const uint32_t *) &s->mem[addr]);
}

/* Write a 16 bit control/status (CSR) register. */
static void
e100_write_reg2(eepro100_t *s, uint8_t addr, uint16_t val)
{
    cpu_to_le16wu((uint16_t *) &s->mem[addr], val);
}

/* Write a 32 bit control/status (CSR) register. */
static void
e100_write_reg4(eepro100_t *s, uint8_t addr, uint32_t val)
{
    cpu_to_le32wu((uint32_t *) &s->mem[addr], val);
}

enum scb_stat_ack {
    stat_ack_not_ours = 0x00,
    stat_ack_sw_gen = 0x04,
    stat_ack_rnr = 0x10,
    stat_ack_cu_idle = 0x20,
    stat_ack_frame_rx = 0x40,
    stat_ack_cu_cmd_done = 0x80,
    stat_ack_not_present = 0xFF,
    stat_ack_rx = (stat_ack_sw_gen | stat_ack_rnr | stat_ack_frame_rx),
    stat_ack_tx = (stat_ack_cu_idle | stat_ack_cu_cmd_done),
};

static void
disable_interrupt(eepro100_t *s)
{
    if (s->int_stat) {
        i8255x_log("interrupt disabled\n");
        pci_clear_irq(s->pci_slot, PCI_INTA, &s->irq_state);
        s->int_stat = 0;
    }
}

static void
enable_interrupt(eepro100_t *s)
{
    if (!s->int_stat) {
        i8255x_log("interrupt enabled\n");
        pci_set_irq(s->pci_slot, PCI_INTA, &s->irq_state);
        s->int_stat = 1;
    }
}

static void
eepro100_acknowledge(eepro100_t *s)
{
    s->scb_stat &= ~s->mem[SCBAck];
    s->mem[SCBAck] = s->scb_stat;
    if (s->scb_stat == 0) {
        disable_interrupt(s);
    }
}

static void
eepro100_interrupt(eepro100_t *s, uint8_t status)
{
    uint8_t mask = ~s->mem[SCBIntmask];
    s->mem[SCBAck] |= status;
    status = s->scb_stat = s->mem[SCBAck];
    status &= (mask | 0x0f);
    if (status && (mask & 0x01)) {
        /* SCB mask and SCB Bit M do not disable interrupt. */
        enable_interrupt(s);
    } else if (s->int_stat) {
        disable_interrupt(s);
    }
}

static void
eepro100_cx_interrupt(eepro100_t *s)
{
    /* CU completed action command. */
    eepro100_interrupt(s, 0x80);
}

static void
eepro100_cna_interrupt(eepro100_t *s)
{
    /* CU left the active state. */
    eepro100_interrupt(s, 0x20);
}

static void
eepro100_fr_interrupt(eepro100_t *s)
{
    /* RU received a complete frame. */
    eepro100_interrupt(s, 0x40);
}

static void
eepro100_rnr_interrupt(eepro100_t *s)
{
    /* RU is not ready. */
    eepro100_interrupt(s, 0x10);
}

static void
eepro100_mdi_interrupt(eepro100_t *s)
{
    /* MDI completed read or write cycle. */
    eepro100_interrupt(s, 0x08);
}

static void
eepro100_swi_interrupt(eepro100_t *s)
{
    /* Software has requested an interrupt. */
    eepro100_interrupt(s, 0x04);
}

/* From FreeBSD */
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

static uint32_t
ldl_le_pci_dma(UNUSED(eepro100_t *s), uint32_t addr)
{
    uint32_t val;

    dma_bm_read(addr, (uint8_t *) &val, 4, 4);
    return le32_to_cpu(val);
}

static uint16_t
lduw_le_pci_dma(UNUSED(eepro100_t *s), uint32_t addr)
{
    uint16_t val;

    dma_bm_read(addr, (uint8_t *) &val, 2, 2);
    return le16_to_cpu(val);
}

static void
stl_le_pci_dma(UNUSED(eepro100_t *s), uint32_t addr, uint32_t val)
{
    val = cpu_to_le32(val);
    dma_bm_write(addr, (uint8_t *) &val, 4, 4);
}

static void
stw_le_pci_dma(UNUSED(eepro100_t *s), uint32_t addr, uint16_t val)
{
    val = cpu_to_le16(val);
    dma_bm_write(addr, (uint8_t *) &val, 2, 2);
}

static void
nic_selective_reset(eepro100_t *s)
{
    size_t   i;
    uint16_t sum = 0;
    uint16_t *eeprom_contents = s->eeprom->contents;

    memcpy(eeprom_contents, s->mac, 6);
    eeprom_contents[EEPROM_ID] = EEPROM_ID_VALID;
    if (s->device == i82557B || s->device == i82557C)
        eeprom_contents[5] = 0x0100;
    eeprom_contents[EEPROM_PHY_ID] = 1;
    for (i = 0; i < EEPROM_SIZE - 1; i++) {
        sum += eeprom_contents[i];
    }
    eeprom_contents[EEPROM_SIZE - 1] = 0xbaba - sum;
    i8255x_log("checksum=0x%04x\n", eeprom_contents[EEPROM_SIZE - 1]);

    memset(s->mem, 0, sizeof(s->mem));
    e100_write_reg4(s, SCBCtrlMDI, BIT(21));

    memcpy(&s->mdimem[0], &eepro100_mdi_default[0], sizeof(s->mdimem));
}

static void
nic_reset(void *opaque)
{
    eepro100_t *s = opaque;
    /* TODO: Clearing of hash register for selective reset, too? */
    memset(&s->mult[0], 0, sizeof(s->mult));
    nic_selective_reset(s);
}

/* Commands that can be put in a command list entry. */
enum commands {
    CmdNOp = 0,
    CmdIASetup = 1,
    CmdConfigure = 2,
    CmdMulticastList = 3,
    CmdTx = 4,
    CmdTDR = 5,                 /* load microcode */
    CmdDump = 6,
    CmdDiagnose = 7,

    /* And some extra flags: */
    CmdSuspend = 0x4000,        /* Suspend after completion. */
    CmdIntr = 0x2000,           /* Interrupt after completion. */
    CmdTxFlex = 0x0008,         /* Use "Flexible mode" for CmdTx command. */
};

static cu_state_t
get_cu_state(eepro100_t *s)
{
    return ((s->mem[SCBStatus] & BITS(7, 6)) >> 6);
}

static void
set_cu_state(eepro100_t *s, cu_state_t state)
{
    s->mem[SCBStatus] = (s->mem[SCBStatus] & ~BITS(7, 6)) + (state << 6);
}

static ru_state_t
get_ru_state(eepro100_t *s)
{
    return ((s->mem[SCBStatus] & BITS(5, 2)) >> 2);
}

static void
set_ru_state(eepro100_t *s, ru_state_t state)
{
    s->mem[SCBStatus] = (s->mem[SCBStatus] & ~BITS(5, 2)) + (state << 2);
}

static void
dump_statistics(eepro100_t *s)
{
    /* Dump statistical data. Most data is never changed by the emulation
     * and always 0, so we first just copy the whole block and then those
     * values which really matter. */
    dma_bm_write(s->statsaddr, (uint8_t *) &s->statistics, s->stats_size, 1);
    stl_le_pci_dma(s, s->statsaddr + 0, s->statistics.tx_good_frames);
    stl_le_pci_dma(s, s->statsaddr + 36, s->statistics.rx_good_frames);
    stl_le_pci_dma(s, s->statsaddr + 48, s->statistics.rx_resource_errors);
    stl_le_pci_dma(s, s->statsaddr + 60, s->statistics.rx_short_frame_errors);
}

static void
read_cb(eepro100_t *s)
{
    dma_bm_read(s->cb_address, (uint8_t *) &s->tx, sizeof(s->tx), 1);
    s->tx.status         = le16_to_cpu(s->tx.status);
    s->tx.command        = le16_to_cpu(s->tx.command);
    s->tx.link           = le32_to_cpu(s->tx.link);
    s->tx.tbd_array_addr = le32_to_cpu(s->tx.tbd_array_addr);
    s->tx.tcb_bytes      = le16_to_cpu(s->tx.tcb_bytes);
}

static void
tx_command(eepro100_t *s)
{
    uint32_t tbd_array = s->tx.tbd_array_addr;
    uint16_t tcb_bytes = s->tx.tcb_bytes & 0x3fff;
    /* Sends larger than MAX_ETH_FRAME_SIZE are allowed, up to 2600 bytes. */
    uint8_t buf[2600];
    uint16_t size = 0;
    uint32_t tbd_address = s->cb_address + 0x10;
    /* Simplified mode: the frame data immediately follows the TxCB.
     * The extended TxCB (i82558/i82559 and later) is 32 bytes long and
     * holds two inline TBDs, so the data starts at offset 0x20 instead
     * of 0x10 as with the standard 16-byte TxCB. */
    uint32_t simplified_address = tbd_address;
    if (s->has_extended_tcb_support && !(s->configuration[6] & BIT(4)))
        simplified_address = s->cb_address + 0x20;
    i8255x_log("transmit, TBD array address 0x%08x, TCB byte count 0x%04x, TBD count %u\n",
               tbd_array, tcb_bytes, s->tx.tbd_count);

    if (tcb_bytes > 2600) {
        i8255x_log("TCB byte count too large, using 2600\n");
        tcb_bytes = 2600;
    }
    if (!((tcb_bytes > 0) || (tbd_array != 0xffffffff))) {
        i8255x_log("illegal values of TBD array address and TCB byte count!\n");
    }
    while (size < tcb_bytes) {
        i8255x_log("TBD (simplified mode): buffer address 0x%08x, size 0x%04x\n",
                   simplified_address, tcb_bytes);
        dma_bm_read(simplified_address, &buf[size], tcb_bytes, 1);
        size += tcb_bytes;
    }
    if (tbd_array == 0xffffffff) {
        /* Simplified mode. Was already handled by code above. */
    } else {
        /* Flexible mode. */
        uint8_t tbd_count = 0;
        uint32_t tx_buffer_address;
        uint16_t tx_buffer_size;
        uint16_t tx_buffer_el;

        if (s->has_extended_tcb_support && !(s->configuration[6] & BIT(4))) {
            /* Extended Flexible TCB. */
            for (; tbd_count < 2; tbd_count++) {
                tx_buffer_address = ldl_le_pci_dma(s, tbd_address);
                tx_buffer_el      = lduw_le_pci_dma(s, tbd_address + 4);
                tx_buffer_size    = tx_buffer_el & 0x7fff;
                tbd_address += 8;
                i8255x_log("TBD (extended flexible mode): buffer address 0x%08x, size 0x%04x\n",
                           tx_buffer_address, tx_buffer_size);
                if (tx_buffer_size > sizeof(buf) - size)
                    tx_buffer_size = sizeof(buf) - size;
                dma_bm_read(tx_buffer_address, &buf[size], tx_buffer_size, 1);
                size += tx_buffer_size;
                if (tx_buffer_el & 0x8000) {
                    break;
                }
            }
        }
        tbd_address = tbd_array;
        for (; tbd_count < s->tx.tbd_count; tbd_count++) {
            tx_buffer_address = ldl_le_pci_dma(s, tbd_address);
            tx_buffer_el      = lduw_le_pci_dma(s, tbd_address + 4);
            tx_buffer_size    = tx_buffer_el & 0x7fff;
            tbd_address += 8;
            i8255x_log("TBD (flexible mode): buffer address 0x%08x, size 0x%04x\n",
                       tx_buffer_address, tx_buffer_size);
            if (tx_buffer_size > sizeof(buf) - size)
                tx_buffer_size = sizeof(buf) - size;
            dma_bm_read(tx_buffer_address, &buf[size], tx_buffer_size, 1);
            size += tx_buffer_size;
            if (tx_buffer_el & 0x8000) {
                break;
            }
        }
    }
    i8255x_log("sending frame, len=%d\n", size);
    network_tx(s->nic, buf, size);
    s->statistics.tx_good_frames++;
}

static void
set_multicast_list(eepro100_t *s)
{
    uint16_t multicast_count = s->tx.tbd_array_addr & BITS(13, 0);
    uint16_t i;
    memset(&s->mult[0], 0, sizeof(s->mult));
    i8255x_log("multicast list, multicast count = %u\n", multicast_count);
    for (i = 0; i < multicast_count; i += 6) {
        uint8_t multicast_addr[6];
        dma_bm_read(s->cb_address + 10 + i, multicast_addr, 6, 1);
        i8255x_log("multicast entry %02x %02x %02x %02x %02x %02x\n",
                   multicast_addr[0], multicast_addr[1], multicast_addr[2],
                   multicast_addr[3], multicast_addr[4], multicast_addr[5]);
        unsigned mcast_idx = (net_crc32(multicast_addr, 6) & BITS(7, 2)) >> 2;
        s->mult[mcast_idx >> 3] |= (1 << (mcast_idx & 7));
    }
}

static void
action_command(eepro100_t *s, uint32_t *cu_offset)
{
    /* The loop below won't stop if it gets special handcrafted data.
       Therefore we limit the number of iterations. */
    unsigned max_loop_count = 16;

    for (;;) {
        bool bit_el;
        bool bit_s;
        bool bit_i;
        bool bit_nc;
        uint16_t ok_status = STATUS_OK;
        s->cb_address = s->cu_base + *cu_offset;
        read_cb(s);
        bit_el = ((s->tx.command & COMMAND_EL) != 0);
        bit_s = ((s->tx.command & COMMAND_S) != 0);
        bit_i = ((s->tx.command & COMMAND_I) != 0);
        bit_nc = ((s->tx.command & COMMAND_NC) != 0);

        if (max_loop_count-- == 0) {
            /* Prevent an endless loop. */
            i8255x_log("loop in action_command\n");
            break;
        }

        *cu_offset = s->tx.link;
        i8255x_log("val=(cu start), status=0x%04x, command=0x%04x, link=0x%08x\n",
                   s->tx.status, s->tx.command, s->tx.link);
        switch (s->tx.command & COMMAND_CMD) {
        case CmdNOp:
            /* Do nothing. */
            break;
        case CmdIASetup:
            dma_bm_read(s->cb_address + 8, &s->mac[0], 6, 1);
            i8255x_log("macaddr: %02x %02x %02x %02x %02x %02x\n",
                       s->mac[0], s->mac[1], s->mac[2],
                       s->mac[3], s->mac[4], s->mac[5]);
            break;
        case CmdConfigure:
            dma_bm_read(s->cb_address + 8,
                        &s->configuration[0], sizeof(s->configuration), 1);
            i8255x_log("configuration: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                       s->configuration[0], s->configuration[1],
                       s->configuration[2], s->configuration[3],
                       s->configuration[4], s->configuration[5],
                       s->configuration[6], s->configuration[7],
                       s->configuration[8], s->configuration[9],
                       s->configuration[10]);
            break;
        case CmdMulticastList:
            set_multicast_list(s);
            break;
        case CmdTx:
            if (bit_nc) {
                i8255x_log("CmdTx: NC = 1\n");
                /* 0: CRC and Source Address are inserted by the controller.
                   1: CRC and Source Address are not inserted by the controller
                   and are assumed to come from memory. */
                ok_status = 0;
                break;
            }
            tx_command(s);
            break;
        case CmdTDR:
            i8255x_log("load microcode\n");
            /* Starting with offset 8, the command contains
             * 64 dwords microcode which we just ignore here. */
            break;
        case CmdDiagnose:
            i8255x_log("diagnose\n");
            /* Make sure error flag is not set. */
            s->tx.status = 0;
            break;
        default:
            i8255x_log("undefined command\n");
            ok_status = 0;
            break;
        }
        /* Write new status. */
        stw_le_pci_dma(s, s->cb_address, s->tx.status | ok_status | STATUS_C);
        if (bit_i) {
            /* CU completed action. */
            eepro100_cx_interrupt(s);
        }
        if (bit_el) {
            /* CU becomes idle. Terminate command loop. */
            set_cu_state(s, cu_idle);
            eepro100_cna_interrupt(s);
            break;
        } else if (bit_s) {
            /* CU becomes suspended. Terminate command loop. */
            set_cu_state(s, cu_suspended);
            eepro100_cna_interrupt(s);
            break;
        } else {
            /* More entries in list. */
            i8255x_log("CU list with at least one more entry\n");
        }
    }
    i8255x_log("CU list empty\n");
    /* List is empty. Now CU is idle or suspended. */
}

static void
eepro100_cu_command(eepro100_t *s, uint8_t val)
{
    cu_state_t cu_state;
    switch (val) {
    case CU_NOP:
        /* No operation. */
        break;
    case CU_START:
        cu_state = get_cu_state(s);
        if (cu_state != cu_idle && cu_state != cu_suspended) {
            /* Intel documentation says that CU must be idle or suspended
             * for the CU start command. */
            i8255x_log("unexpected CU state is %u\n", cu_state);
        }
        set_cu_state(s, cu_active);
        s->cu_offset = e100_read_reg4(s, SCBPointer);
        action_command(s, &s->cu_offset);
        break;
    case CU_HPQ_START:
        cu_state = get_cu_state(s);
        if (cu_state != cu_idle && cu_state != cu_suspended) {
            i8255x_log("unexpected CU state is %u (HPQ start)\n", cu_state);
        }
        set_cu_state(s, cu_hqp_active);
        s->cu_offset_hpq = e100_read_reg4(s, SCBPointer);
        i8255x_log("HPQ start at 0x%08x\n", s->cu_offset_hpq);
        action_command(s, &s->cu_offset_hpq);
        break;
    case CU_HPQ_RESUME:
        if (get_cu_state(s) != cu_hqp_active && get_cu_state(s) != cu_suspended) {
            i8255x_log("bad CU HPQ resume from CU state %u\n", get_cu_state(s));
        }
        if (get_cu_state(s) == cu_suspended) {
            i8255x_log("CU HPQ resuming\n");
            set_cu_state(s, cu_hqp_active);
            action_command(s, &s->cu_offset_hpq);
        }
        break;
    case CU_RESUME:
        if (get_cu_state(s) != cu_suspended) {
            i8255x_log("bad CU resume from CU state %u\n", get_cu_state(s));
            /* Workaround for bad Linux eepro100 driver which resumes
             * from idle state. */
            set_cu_state(s, cu_suspended);
        }
        if (get_cu_state(s) == cu_suspended) {
            i8255x_log("CU resuming\n");
            set_cu_state(s, cu_active);
            action_command(s, &s->cu_offset);
        }
        break;
    case CU_STATSADDR:
        /* Load dump counters address. */
        s->statsaddr = e100_read_reg4(s, SCBPointer);
        i8255x_log("val=0x%02x (dump counters address)\n", val);
        if (s->statsaddr & 3) {
            /* Memory must be Dword aligned. */
            i8255x_log("unaligned dump counters address\n");
            /* Handling of misaligned addresses is undefined.
             * Here we align the address by ignoring the lower bits. */
            s->statsaddr &= ~3;
        }
        break;
    case CU_SHOWSTATS:
        /* Dump statistical counters. */
        i8255x_log("val=0x%02x (dump stats)\n", val);
        dump_statistics(s);
        stl_le_pci_dma(s, s->statsaddr + s->stats_size, 0xa005);
        break;
    case CU_CMD_BASE:
        /* Load CU base. */
        i8255x_log("val=0x%02x (CU base address)\n", val);
        s->cu_base = e100_read_reg4(s, SCBPointer);
        break;
    case CU_DUMPSTATS:
        /* Dump and reset statistical counters. */
        i8255x_log("val=0x%02x (dump stats and reset)\n", val);
        dump_statistics(s);
        stl_le_pci_dma(s, s->statsaddr + s->stats_size, 0xa007);
        memset(&s->statistics, 0, sizeof(s->statistics));
        break;
    case CU_SRESUME:
        /* CU static resume. */
        i8255x_log("CU static resume\n");
        break;
    default:
        i8255x_log("Undefined CU command\n");
    }
}

static void
eepro100_ru_command(eepro100_t *s, uint8_t val)
{
    switch (val) {
    case RU_NOP:
        /* No operation. */
        break;
    case RX_START:
        /* RU start. */
        if (get_ru_state(s) != ru_idle) {
            i8255x_log("RU state is %u, should be %u\n", get_ru_state(s), ru_idle);
        }
        set_ru_state(s, ru_ready);
        s->ru_offset = e100_read_reg4(s, SCBPointer);
        i8255x_log("val=0x%02x (rx start)\n", val);
        break;
    case RX_RESUME:
        /* Restart RU. */
        if (get_ru_state(s) != ru_suspended) {
            i8255x_log("RU state is %u, should be %u\n", get_ru_state(s),
                       ru_suspended);
        }
        set_ru_state(s, ru_ready);
        break;
    case RU_ABORT:
        /* RU abort. */
        if (get_ru_state(s) == ru_ready) {
            eepro100_rnr_interrupt(s);
        }
        set_ru_state(s, ru_idle);
        break;
    case RX_ADDR_LOAD:
        /* Load RU base. */
        i8255x_log("val=0x%02x (RU base address)\n", val);
        s->ru_base = e100_read_reg4(s, SCBPointer);
        break;
    default:
        i8255x_log("val=0x%02x (undefined RU command)\n", val);
    }
}

static void
eepro100_write_command(eepro100_t *s, uint8_t val)
{
    eepro100_ru_command(s, val & 0x0f);
    eepro100_cu_command(s, val & 0xf0);
    if ((val) == 0) {
        i8255x_log("val=0x%02x\n", val);
    }
    /* Clear command byte after command was accepted. */
    s->mem[SCBCmd] = 0;
}

/*****************************************************************************
 *
 * EEPROM emulation.
 *
 ****************************************************************************/

#define EEPROM_CS       0x02
#define EEPROM_SK       0x01
#define EEPROM_DI       0x04
#define EEPROM_DO       0x08

static uint16_t
eepro100_read_eeprom(eepro100_t *s)
{
    uint16_t val = e100_read_reg2(s, SCBeeprom);
    if (s->eeprom->eedo) {
        val |= EEPROM_DO;
    } else {
        val &= ~EEPROM_DO;
    }
    i8255x_log("val=0x%04x\n", val);
    return val;
}

static void
eepro100_write_eeprom(eeprom_t *eeprom, uint8_t val)
{
    i8255x_log("val=0x%02x\n", val);

    int eecs = ((val & EEPROM_CS) != 0);
    int eesk = ((val & EEPROM_SK) != 0);
    int eedi = ((val & EEPROM_DI) != 0);
    uint8_t tick = eeprom->tick;
    uint8_t eedo = eeprom->eedo;
    uint16_t address = eeprom->address;
    uint8_t command = eeprom->command;

    if (!eeprom->eecs && eecs) {
        /* Start chip select cycle. */
        tick = 0;
        command = 0x0;
        address = 0x0;
    } else if (eeprom->eecs && !eecs) {
        /* End chip select cycle. This triggers write / erase. */
        if (eeprom->writable) {
            uint8_t subcommand = address >> (eeprom->addrbits - 2);
            if (command == 0 && subcommand == 2) {
                /* Erase all. */
                for (address = 0; address < eeprom->size; address++) {
                    eeprom->contents[address] = 0xffff;
                }
            } else if (command == 3) {
                /* Erase word. */
                eeprom->contents[address] = 0xffff;
            } else if (tick >= 2 + 2 + eeprom->addrbits + 16) {
                if (command == 1) {
                    /* Write word. */
                    eeprom->contents[address] &= eeprom->data;
                } else if (command == 0 && subcommand == 1) {
                    /* Write all. */
                    for (address = 0; address < eeprom->size; address++) {
                        eeprom->contents[address] &= eeprom->data;
                    }
                }
            }
        }
        /* Output DO is tristate, read results in 1. */
        eedo = 1;
    } else if (eecs && !eeprom->eesk && eesk) {
        /* Raising edge of clock shifts data in. */
        if (tick == 0) {
            /* Wait for 1st start bit. */
            if (eedi == 0) {
                i8255x_log("Got correct 1st start bit, waiting for 2nd start bit (1)\n");
                tick++;
            } else {
                i8255x_log("wrong 1st start bit (is 1, should be 0)\n");
                tick = 2;
            }
        } else if (tick == 1) {
            /* Wait for 2nd start bit. */
            if (eedi != 0) {
                i8255x_log("Got correct 2nd start bit, getting command + address\n");
                tick++;
            } else {
                i8255x_log("1st start bit is longer than needed\n");
            }
        } else if (tick < 2 + 2) {
            /* Got 2 start bits, transfer 2 opcode bits. */
            tick++;
            command <<= 1;
            if (eedi) {
                command += 1;
            }
        } else if (tick < 2 + 2 + eeprom->addrbits) {
            /* Got 2 start bits and 2 opcode bits, transfer all address bits. */
            tick++;
            address = ((address << 1) | eedi);
            if (tick == 2 + 2 + eeprom->addrbits) {
                i8255x_log("command 0x%02x, address = 0x%02x (value 0x%04x)\n",
                           command, address, eeprom->contents[address]);
                if (command == 2) {
                    eedo = 0;
                }
                address = address % eeprom->size;
                if (command == 0) {
                    /* Command code in upper 2 bits of address. */
                    switch (address >> (eeprom->addrbits - 2)) {
                    case 0:
                        i8255x_log("write disable command\n");
                        eeprom->writable = 0;
                        break;
                    case 1:
                        i8255x_log("write all command\n");
                        break;
                    case 2:
                        i8255x_log("erase all command\n");
                        break;
                    case 3:
                        i8255x_log("write enable command\n");
                        eeprom->writable = 1;
                        break;
                    }
                } else {
                    /* Read, write or erase word. */
                    eeprom->data = eeprom->contents[address];
                }
            }
        } else if (tick < 2 + 2 + eeprom->addrbits + 16) {
            /* Transfer 16 data bits. */
            tick++;
            if (command == 2) {
                /* Read word. */
                eedo = ((eeprom->data & 0x8000) != 0);
            }
            eeprom->data <<= 1;
            eeprom->data += eedi;
        } else {
            i8255x_log("additional unneeded tick, not processed\n");
        }
    }
    /* Save status of EEPROM. */
    eeprom->tick = tick;
    eeprom->eecs = eecs;
    eeprom->eesk = eesk;
    eeprom->eedo = eedo;
    eeprom->address = address;
    eeprom->command = command;
}

/*****************************************************************************
 *
 * MDI emulation.
 *
 ****************************************************************************/

static uint32_t
eepro100_read_mdi(eepro100_t *s)
{
    uint32_t val = e100_read_reg4(s, SCBCtrlMDI);

    /* Emulation takes no time to finish MDI transaction. */
    val |= BIT(28);
    i8255x_log("val=0x%08x\n", val);
    return val;
}

static void
eepro100_write_mdi(eepro100_t *s)
{
    uint32_t val = e100_read_reg4(s, SCBCtrlMDI);
    uint8_t raiseint = (val & BIT(29)) >> 29;
    uint8_t opcode = (val & BITS(27, 26)) >> 26;
    uint8_t phy = (val & BITS(25, 21)) >> 21;
    uint8_t reg = (val & BITS(20, 16)) >> 16;
    uint16_t data = (val & BITS(15, 0));
    i8255x_log("val=0x%08x (int=%u, opcode=%u, phy=%u, reg=%u, data=0x%04x\n",
               val, raiseint, opcode, phy, reg, data);
    if (phy != 1) {
        /* Unsupported PHY address. */
        data = 0;
    } else if (opcode != 1 && opcode != 2) {
        /* Unsupported opcode. */
        i8255x_log("opcode must be 1 or 2 but is %u\n", opcode);
        data = 0;
    } else if (reg > 6) {
        /* Unsupported register. */
        i8255x_log("register must be 0...6 but is %u\n", reg);
        data = 0;
    } else {
        i8255x_log("val=0x%08x (int=%u, opcode=%u, phy=%u, reg=%u, data=0x%04x\n",
                   val, raiseint, opcode, phy, reg, data);
        if (opcode == 1) {
            /* MDI write */
            switch (reg) {
            case 0:            /* Control Register */
                if (data & 0x8000) {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = eepro100_mdi_default[0];
                    s->mdimem[1] = eepro100_mdi_default[1];
                    data = s->mdimem[reg];
                } else {
                    /* Restart Auto Configuration = Normal Operation */
                    data &= ~0x0200;
                }
                break;
            case 1:            /* Status Register */
                i8255x_log("not writable\n");
                break;
            case 2:            /* PHY Identification Register (Word 1) */
            case 3:            /* PHY Identification Register (Word 2) */
                i8255x_log("not implemented\n");
                break;
            case 4:            /* Auto-Negotiation Advertisement Register */
            case 5:            /* Auto-Negotiation Link Partner Ability Register */
                break;
            case 6:            /* Auto-Negotiation Expansion Register */
            default:
                i8255x_log("not implemented\n");
            }
            s->mdimem[reg] &= eepro100_mdi_mask[reg];
            s->mdimem[reg] |= data & ~eepro100_mdi_mask[reg];
        } else if (opcode == 2) {
            /* MDI read */
            switch (reg) {
            case 0:            /* Control Register */
                if (data & 0x8000) {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = eepro100_mdi_default[0];
                    s->mdimem[1] = eepro100_mdi_default[1];
                }
                break;
            case 1:            /* Status Register */
                s->mdimem[reg] |= 0x0020;
                break;
            case 2:            /* PHY Identification Register (Word 1) */
            case 3:            /* PHY Identification Register (Word 2) */
            case 4:            /* Auto-Negotiation Advertisement Register */
                break;
            case 5:            /* Auto-Negotiation Link Partner Ability Register */
                s->mdimem[reg] = 0x41fe;
                break;
            case 6:            /* Auto-Negotiation Expansion Register */
                s->mdimem[reg] = 0x0001;
                break;
            }
            data = s->mdimem[reg];
        }
        /* Emulation takes no time to finish MDI transaction.
         * Set MDI bit in SCB status register. */
        s->mem[SCBAck] |= 0x08;
        val |= BIT(28);
        if (raiseint) {
            eepro100_mdi_interrupt(s);
        }
    }
    val = (val & 0xffff0000) + data;
    e100_write_reg4(s, SCBCtrlMDI, val);
}

/*****************************************************************************
 *
 * Port emulation.
 *
 ****************************************************************************/

#define PORT_SOFTWARE_RESET     0
#define PORT_SELFTEST           1
#define PORT_SELECTIVE_RESET    2
#define PORT_DUMP               3
#define PORT_SELECTION_MASK     3

typedef struct {
    uint32_t st_sign;           /* Self Test Signature */
    uint32_t st_result;         /* Self Test Results */
} eepro100_selftest_t;

static uint32_t
eepro100_read_port(UNUSED(eepro100_t *s))
{
    return 0;
}

static void
eepro100_write_port(eepro100_t *s)
{
    uint32_t val = e100_read_reg4(s, SCBPort);
    uint32_t address = (val & ~PORT_SELECTION_MASK);
    uint8_t selection = (val & PORT_SELECTION_MASK);
    switch (selection) {
    case PORT_SOFTWARE_RESET:
        nic_reset(s);
        break;
    case PORT_SELFTEST:
        i8255x_log("selftest address=0x%08x\n", address);
        {
            eepro100_selftest_t data;
            dma_bm_read(address, (uint8_t *) &data, sizeof(data), 1);
            data.st_sign = 0xffffffff;
            data.st_result = 0;
            dma_bm_write(address, (uint8_t *) &data, sizeof(data), 1);
        }
        break;
    case PORT_SELECTIVE_RESET:
        i8255x_log("selective reset, selftest address=0x%08x\n", address);
        nic_selective_reset(s);
        break;
    default:
        i8255x_log("val=0x%08x\n", val);
        i8255x_log("unknown port selection\n");
    }
}

/*****************************************************************************
 *
 * General hardware emulation.
 *
 ****************************************************************************/

static uint8_t
eepro100_read1(eepro100_t *s, uint32_t addr)
{
    uint8_t val = 0;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        val = s->mem[addr];
    }

    switch (addr) {
    case SCBStatus:
    case SCBAck:
    case SCBCmd:
    case SCBIntmask:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        break;
    case SCBPort + 3:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        break;
    case SCBeeprom:
        val = (uint8_t) eepro100_read_eeprom(s);
        break;
    case SCBCtrlMDI:
    case SCBCtrlMDI + 1:
    case SCBCtrlMDI + 2:
    case SCBCtrlMDI + 3:
        val = (uint8_t) (eepro100_read_mdi(s) >> (8 * (addr & 3)));
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        break;
    case SCBpmdr:       /* Power Management Driver Register */
        val = 0;
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        break;
    case SCBgctrl:      /* General Control Register */
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        break;
    case SCBgstat:      /* General Status Register */
        /* 100 Mbps full duplex, valid link */
        val = 0x07;
        i8255x_log("addr=General Status val=0x%02x\n", val);
        break;
    default:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        i8255x_log("unknown byte read\n");
    }
    return val;
}

static uint16_t
eepro100_read2(eepro100_t *s, uint32_t addr)
{
    uint16_t val = 0;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        val = e100_read_reg2(s, addr);
    }

    switch (addr) {
    case SCBStatus:
    case SCBCmd:
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        break;
    case SCBeeprom:
        val = eepro100_read_eeprom(s);
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        break;
    case SCBCtrlMDI:
    case SCBCtrlMDI + 2:
        val = (uint16_t) (eepro100_read_mdi(s) >> (8 * (addr & 3)));
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        break;
    default:
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        i8255x_log("unknown word read\n");
    }
    return val;
}

static uint32_t
eepro100_read4(eepro100_t *s, uint32_t addr)
{
    uint32_t val = 0;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        val = e100_read_reg4(s, addr);
    }

    switch (addr) {
    case SCBStatus:
    case SCBPointer:
        i8255x_log("addr=0x%02x val=0x%08x\n", addr, val);
        break;
    case SCBPort:
        val = eepro100_read_port(s);
        i8255x_log("addr=0x%02x val=0x%08x\n", addr, val);
        break;
    case SCBflash:
        val = eepro100_read_eeprom(s);
        i8255x_log("addr=0x%02x val=0x%08x\n", addr, val);
        break;
    case SCBCtrlMDI:
        val = eepro100_read_mdi(s);
        break;
    default:
        i8255x_log("addr=0x%02x val=0x%08x\n", addr, val);
        i8255x_log("unknown longword read\n");
    }
    return val;
}

static void
eepro100_write1(eepro100_t *s, uint32_t addr, uint8_t val)
{
    /* SCBStatus is readonly. */
    if (addr > SCBStatus && addr <= sizeof(s->mem) - sizeof(val)) {
        s->mem[addr] = val;
    }

    switch (addr) {
    case SCBStatus:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        break;
    case SCBAck:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        eepro100_acknowledge(s);
        break;
    case SCBCmd:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        eepro100_write_command(s, val);
        break;
    case SCBIntmask:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        if (val & BIT(1)) {
            eepro100_swi_interrupt(s);
        }
        eepro100_interrupt(s, 0);
        break;
    case SCBPointer:
    case SCBPointer + 1:
    case SCBPointer + 2:
    case SCBPointer + 3:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        break;
    case SCBPort:
    case SCBPort + 1:
    case SCBPort + 2:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        break;
    case SCBPort + 3:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        eepro100_write_port(s);
        break;
    case SCBFlow:       /* does not exist on 82557 */
    case SCBFlow + 1:
    case SCBFlow + 2:
    case SCBpmdr:       /* does not exist on 82557 */
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        break;
    case SCBeeprom:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        eepro100_write_eeprom(s->eeprom, val);
        break;
    case SCBCtrlMDI:
    case SCBCtrlMDI + 1:
    case SCBCtrlMDI + 2:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        break;
    case SCBCtrlMDI + 3:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        eepro100_write_mdi(s);
        break;
    default:
        i8255x_log("addr=0x%02x val=0x%02x\n", addr, val);
        i8255x_log("unknown byte write\n");
    }
}

static void
eepro100_write2(eepro100_t *s, uint32_t addr, uint16_t val)
{
    /* SCBStatus is readonly. */
    if (addr > SCBStatus && addr <= sizeof(s->mem) - sizeof(val)) {
        e100_write_reg2(s, addr, val);
    }

    switch (addr) {
    case SCBStatus:
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        s->mem[SCBAck] = (val >> 8);
        eepro100_acknowledge(s);
        break;
    case SCBCmd:
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        eepro100_write_command(s, val);
        eepro100_write1(s, SCBIntmask, val >> 8);
        break;
    case SCBPointer:
    case SCBPointer + 2:
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        break;
    case SCBPort:
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        break;
    case SCBPort + 2:
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        eepro100_write_port(s);
        break;
    case SCBeeprom:
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        eepro100_write_eeprom(s->eeprom, val);
        break;
    case SCBCtrlMDI:
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        break;
    case SCBCtrlMDI + 2:
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        eepro100_write_mdi(s);
        break;
    default:
        i8255x_log("addr=0x%02x val=0x%04x\n", addr, val);
        i8255x_log("unknown word write\n");
    }
}

static void
eepro100_write4(eepro100_t *s, uint32_t addr, uint32_t val)
{
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        e100_write_reg4(s, addr, val);
    }

    switch (addr) {
    case SCBPointer:
        i8255x_log("addr=0x%02x val=0x%08x\n", addr, val);
        break;
    case SCBPort:
        i8255x_log("addr=0x%02x val=0x%08x\n", addr, val);
        eepro100_write_port(s);
        break;
    case SCBflash:
        i8255x_log("addr=0x%02x val=0x%08x\n", addr, val);
        val = val >> 16;
        eepro100_write_eeprom(s->eeprom, val);
        break;
    case SCBCtrlMDI:
        i8255x_log("addr=0x%02x val=0x%08x\n", addr, val);
        eepro100_write_mdi(s);
        break;
    default:
        i8255x_log("addr=0x%02x val=0x%08x\n", addr, val);
        i8255x_log("unknown longword write\n");
    }
}

/* I/O access handlers. */

static uint8_t
eepro100_io_readb(uint16_t addr, void *priv)
{
    eepro100_t *s = priv;

    return eepro100_read1(s, addr & 0xff);
}

static uint16_t
eepro100_io_readw(uint16_t addr, void *priv)
{
    eepro100_t *s = priv;

    return eepro100_read2(s, addr & 0xff);
}

static uint32_t
eepro100_io_readl(uint16_t addr, void *priv)
{
    eepro100_t *s = priv;

    return eepro100_read4(s, addr & 0xff);
}

static void
eepro100_io_writeb(uint16_t addr, uint8_t val, void *priv)
{
    eepro100_t *s = priv;

    eepro100_write1(s, addr & 0xff, val);
}

static void
eepro100_io_writew(uint16_t addr, uint16_t val, void *priv)
{
    eepro100_t *s = priv;

    eepro100_write2(s, addr & 0xff, val);
}

static void
eepro100_io_writel(uint16_t addr, uint32_t val, void *priv)
{
    eepro100_t *s = priv;

    eepro100_write4(s, addr & 0xff, val);
}

/* MMIO access handlers. */

static uint8_t
eepro100_mem_readb(uint32_t addr, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->mem_base) && (addr < (s->mem_base + PCI_MEM_SIZE)))
        return eepro100_read1(s, addr & 0xfff);
    return 0xff;
}

static uint16_t
eepro100_mem_readw(uint32_t addr, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->mem_base) && (addr < (s->mem_base + PCI_MEM_SIZE)))
        return eepro100_read2(s, addr & 0xfff);
    return 0xffff;
}

static uint32_t
eepro100_mem_readl(uint32_t addr, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->mem_base) && (addr < (s->mem_base + PCI_MEM_SIZE)))
        return eepro100_read4(s, addr & 0xfff);
    return 0xffffffff;
}

static void
eepro100_mem_writeb(uint32_t addr, uint8_t val, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->mem_base) && (addr < (s->mem_base + PCI_MEM_SIZE)))
        eepro100_write1(s, addr & 0xfff, val);
}

static void
eepro100_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->mem_base) && (addr < (s->mem_base + PCI_MEM_SIZE)))
        eepro100_write2(s, addr & 0xfff, val);
}

static void
eepro100_mem_writel(uint32_t addr, uint32_t val, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->mem_base) && (addr < (s->mem_base + PCI_MEM_SIZE)))
        eepro100_write4(s, addr & 0xfff, val);
}

/* Flash access handlers (aliased to the same registers). */

static uint8_t
eepro100_flash_readb(uint32_t addr, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->flash_base) && (addr < (s->flash_base + PCI_FLASH_SIZE)))
        return eepro100_read1(s, addr & 0xff);
    return 0xff;
}

static uint16_t
eepro100_flash_readw(uint32_t addr, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->flash_base) && (addr < (s->flash_base + PCI_FLASH_SIZE)))
        return eepro100_read2(s, addr & 0xff);
    return 0xffff;
}

static uint32_t
eepro100_flash_readl(uint32_t addr, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->flash_base) && (addr < (s->flash_base + PCI_FLASH_SIZE)))
        return eepro100_read4(s, addr & 0xff);
    return 0xffffffff;
}

static void
eepro100_flash_writeb(uint32_t addr, uint8_t val, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->flash_base) && (addr < (s->flash_base + PCI_FLASH_SIZE)))
        eepro100_write1(s, addr & 0xff, val);
}

static void
eepro100_flash_writew(uint32_t addr, uint16_t val, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->flash_base) && (addr < (s->flash_base + PCI_FLASH_SIZE)))
        eepro100_write2(s, addr & 0xff, val);
}

static void
eepro100_flash_writel(uint32_t addr, uint32_t val, void *priv)
{
    eepro100_t *s = priv;

    if ((addr >= s->flash_base) && (addr < (s->flash_base + PCI_FLASH_SIZE)))
        eepro100_write4(s, addr & 0xff, val);
}

static int
eepro100_do_receive(void *priv, uint8_t *buf, int size)
{
    /* TODO:
     * - Magic packets should set bit 30 in power management driver register.
     * - Interesting packets should set bit 29 in power management driver register.
     */
    eepro100_t *s = priv;
    uint16_t rfd_status = 0xa000;
    uint8_t min_buf[60];
    static const uint8_t broadcast_macaddr[6] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    i8255x_log("received, len=%d\n", size);

    /* Pad to minimum Ethernet frame length */
    if (size < (int) sizeof(min_buf)) {
        memcpy(min_buf, buf, size);
        memset(&min_buf[size], 0, sizeof(min_buf) - size);
        buf = min_buf;
        size = sizeof(min_buf);
    }

    if (s->configuration[8] & 0x80) {
        /* CSMA is disabled. */
        i8255x_log("received while CSMA is disabled\n");
        return 0;
    } else if ((size > MAX_ETH_FRAME_SIZE + 4) && !(s->configuration[18] & BIT(3))) {
        /* Long frame and configuration byte 18/3 (long receive ok) not set:
         * Long frames are discarded. */
        i8255x_log("received long frame (%d byte), ignored\n", size);
        return size;
    } else if (memcmp(buf, s->mac, 6) == 0) {       /* !!! */
        /* Frame matches individual address. */
        i8255x_log("received frame for me, len=%d\n", size);
    } else if (memcmp(buf, broadcast_macaddr, 6) == 0) {
        /* Broadcast frame. */
        i8255x_log("received broadcast, len=%d\n", size);
        rfd_status |= 0x0002;
    } else if (buf[0] & 0x01) {
        /* Multicast frame. */
        i8255x_log("received multicast, len=%d\n", size);
        if (s->configuration[21] & BIT(3)) {
          /* Multicast all bit is set, receive all multicast frames. */
        } else {
          unsigned mcast_idx = (net_crc32(buf, 6) & BITS(7, 2)) >> 2;
          if (s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7))) {
            /* Multicast frame is allowed in hash table. */
          } else if (s->configuration[15] & BIT(0)) {
              /* Promiscuous: receive all. */
              rfd_status |= 0x0004;
          } else {
              i8255x_log("multicast ignored\n");
              return size;
          }
        }
        rfd_status |= 0x0002;
    } else if (s->configuration[15] & BIT(0)) {
        /* Promiscuous: receive all. */
        i8255x_log("received frame in promiscuous mode, len=%d\n", size);
        rfd_status |= 0x0004;
    } else if (s->configuration[20] & BIT(6)) {
        /* Multiple IA bit set. */
        unsigned mcast_idx = net_crc32(buf, 6) >> 26;
        if (s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7))) {
            i8255x_log("accepted, multiple IA bit set\n");
        } else {
            i8255x_log("frame ignored, multiple IA bit set\n");
            return size;
        }
    } else {
        i8255x_log("received frame, ignored, len=%d\n", size);
        return size;
    }

    if (get_ru_state(s) != ru_ready) {
        /* No resources available. */
        i8255x_log("no resources, state=%u\n", get_ru_state(s));
        /* TODO: RNR interrupt only at first failed frame? */
        eepro100_rnr_interrupt(s);
        s->statistics.rx_resource_errors++;
        return 0;
    }
    eepro100_rx_t rx;
    dma_bm_read(s->ru_base + s->ru_offset,
                (uint8_t *) &rx, sizeof(eepro100_rx_t), 1);
    uint16_t rfd_command = le16_to_cpu(rx.command);
    uint16_t rfd_size = le16_to_cpu(rx.size);

    if (size > rfd_size) {
        i8255x_log("Receive buffer (%d bytes) too small for data (%d bytes); data truncated\n",
                   rfd_size, size);
        size = rfd_size;
    }
    i8255x_log("command 0x%04x, link 0x%08x, addr 0x%08x, size %u\n",
               rfd_command, rx.link, rx.rx_buf_addr, rfd_size);
    stw_le_pci_dma(s, s->ru_base + s->ru_offset +
                offsetof(eepro100_rx_t, status), rfd_status);
    stw_le_pci_dma(s, s->ru_base + s->ru_offset +
                offsetof(eepro100_rx_t, count), size);
    /* Receive CRC Transfer not supported. */
    if (s->configuration[18] & BIT(2)) {
        i8255x_log("Receive CRC Transfer\n");
        return 0;
    }
    dma_bm_write(s->ru_base + s->ru_offset +
                 sizeof(eepro100_rx_t), buf, size, 1);
    s->statistics.rx_good_frames++;
    eepro100_fr_interrupt(s);
    s->ru_offset = le32_to_cpu(rx.link);
    if (rfd_command & COMMAND_EL) {
        /* EL bit is set, so this was the last frame. */
        i8255x_log("receive: Running out of frames\n");
        set_ru_state(s, ru_no_resources);
        eepro100_rnr_interrupt(s);
    }
    if (rfd_command & COMMAND_S) {
        /* S bit is set. */
        set_ru_state(s, ru_suspended);
    }
    return size;
}

static int
eepro100_set_link_status(void *priv, uint32_t link_state)
{
    eepro100_t *s = priv;

    s->link_state = link_state;

    return 1;
}

static void
e100_pci_reset(eepro100_t *s, e100_device_info_t *info)
{
    uint32_t device = s->device;
    uint8_t *pci_conf = s->pci_conf;

    /* PCI Status */
    pci_conf[PCI_REG_STATUS_L] = PCI_STATUS_L_FAST_B2B;
    pci_conf[PCI_REG_STATUS_H] = PCI_DEVSEL_MEDIUM;
    /* PCI Latency Timer */
    pci_conf[PCI_REG_LATENCY_TIMER] = 0x20;   /* latency timer = 32 clocks */
    /* Interrupt Line */
    /* Interrupt Pin */
    pci_conf[PCI_REG_INT_PIN] = 1;      /* interrupt pin A */
    /* Minimum Grant */
    pci_conf[PCI_REG_MIN_GRANT] = 0x08;
    /* Maximum Latency */
    pci_conf[PCI_REG_MAX_LAT] = 0x18;

    s->stats_size = info->stats_size;
    s->has_extended_tcb_support = info->has_extended_tcb_support;

    switch (device) {
    case i82550:
    case i82551:
    case i82557A:
    case i82557B:
    case i82557C:
    case i82558A:
    case i82558B:
    case i82559A:
    case i82559B:
    case i82559ER:
    case i82562:
    case i82801:
    case i82559C:
        break;
    default:
        i8255x_log("Device %X is undefined!\n", device);
    }

    /* Standard TxCB. */
    s->configuration[6] |= BIT(4);

    /* Standard statistical counters. */
    s->configuration[6] |= BIT(5);

    if (info->power_management) {
        /* Power Management Capabilities */
        int cfg_offset = 0xdc;
        pci_conf[PCI_REG_STATUS_L] |= PCI_STATUS_L_CAPAB;
        pci_conf[PCI_REG_CAPS_PTR] = cfg_offset;
        pci_conf[cfg_offset + 0] = 0x01;    /* PM capability */
        pci_conf[cfg_offset + 1] = 0x00;    /* next pointer */
        pci_conf[cfg_offset + 2] = 0x21;    /* PMC (version 1.0, ...) */
        pci_conf[cfg_offset + 3] = 0x7e;    /* PMC */
        pci_conf[cfg_offset + 4] = 0x00;    /* PMCSR */
        pci_conf[cfg_offset + 5] = 0x00;    /* PMCSR */
        pci_conf[cfg_offset + 6] = 0x00;    /* PMCSR_BSE */
        pci_conf[cfg_offset + 7] = 0x00;    /* Data */
    }
}

static uint8_t
eepro100_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    eepro100_t *s = (eepro100_t *) priv;

    switch (addr) {
        case 0x04:
        case 0x2c:
        case 0x2d:
        case 0x2e:
        case 0x2f:
        case 0x3c:
        case 0x34:
            i8255x_log("cfg R %02X = %02X\n", addr, s->pci_conf[addr & 0xFF]);
            break;
        default:
            break;
    }

    switch (addr) {
        default:
            return s->pci_conf[addr & 0xFF];
        case 0x00:
            return 0x86;
        case 0x01:
            return 0x80;
        case 0x02:
            return (s->pci_conf[0x02]);
        case 0x03:
            return (s->pci_conf[0x03]);
        case 0x07:
            return s->pci_conf[addr & 0xFF] | 0x02;
        case 0x05:
            return s->pci_conf[addr & 0xFF] & 1;
        case 0x09:
            return 0x0;
        case 0x0a:
            return 0x0;
        case 0x0b:
            return 0x2;
        case 0x0d:
            return s->pci_conf[addr & 0xFF];
        case 0x10:
            return 0x08;
        case 0x11:
            return s->pci_conf[addr & 0xFF] & 0xf0;
        case 0x14:
            return (s->pci_conf[addr & 0xFF] & 0xc0) | 0x01;
        case 0x18:
            return 0;
        case 0x19:
            return 0;
        case 0x1a:
            return s->pci_conf[addr & 0xFF] & 0xfe;
        case 0x1b:
            return s->pci_conf[addr & 0xFF];
        case 0x1c ... 0x27:
            return 0;   /* BARs 3-5 do not exist on the 8255x */
        case 0x30 ... 0x33:
            return 0;   /* No expansion ROM */
        case 0x2c:
            return s->pci_conf[0x2c];
        case 0x2d:
            return s->pci_conf[0x2d];
        case 0x2e:
            return s->pci_conf[0x2e];
        case 0x2f:
            return s->pci_conf[0x2f];
        case 0x3c:
            return s->pci_conf[addr & 0xFF];
        case 0x3d:
            return PCI_INTA;
        case 0x34:
            return s->pci_conf[addr & 0xFF];
    }
}

static void
eepro100_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    eepro100_t *s = (eepro100_t *) priv;

    i8255x_log("cfg W %02X=%02X (cmd=%02X)\n", addr, val, s->pci_conf[0x04]);

    switch (addr) {
        case 0x04:
            io_removehandler(s->io_base, PCI_IO_SIZE,
                             eepro100_io_readb, eepro100_io_readw, eepro100_io_readl,
                             eepro100_io_writeb, eepro100_io_writew, eepro100_io_writel,
                             priv);
            mem_mapping_disable(&s->bar_mem);
            mem_mapping_disable(&s->bar_flash);
            s->pci_conf[addr & 0xFF] = val;
            if ((val & PCI_COMMAND_IO) && s->io_base)
                io_sethandler(s->io_base, PCI_IO_SIZE,
                              eepro100_io_readb, eepro100_io_readw, eepro100_io_readl,
                              eepro100_io_writeb, eepro100_io_writew, eepro100_io_writel,
                              priv);
            i8255x_log("cmd: IO=%d base=%04X -> %s\n", !!(val & PCI_COMMAND_IO), s->io_base,
                       ((val & PCI_COMMAND_IO) && s->io_base) ? "handler SET" : "handler NOT set");
            if ((val & PCI_COMMAND_MEM) && s->bar_mem.size && s->mem_base)
                mem_mapping_enable(&s->bar_mem);
            if ((val & PCI_COMMAND_MEM) && s->bar_flash.size && s->flash_base)
                mem_mapping_enable(&s->bar_flash);
            break;
        case 0x05:
            s->pci_conf[addr & 0xFF] = val & 1;
            break;
        case 0x0c:
            s->pci_conf[addr & 0xFF] = val;
            break;
        case 0x0d:
            s->pci_conf[addr & 0xFF] = val;
            break;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            mem_mapping_disable(&s->bar_mem);
            s->pci_conf[addr & 0xFF] = val;
            s->mem_base = (s->pci_conf[0x13] << 24) | (s->pci_conf[0x12] << 16) |
                          (s->pci_conf[0x11] << 8) | (s->pci_conf[0x10] & 0xf0);
            s->mem_base &= 0xfffff000;
            if (s->mem_base != 0) {
                mem_mapping_set_addr(&s->bar_mem, s->mem_base, PCI_MEM_SIZE);
                if (!(s->pci_conf[0x04] & PCI_COMMAND_MEM))
                    mem_mapping_disable(&s->bar_mem);
            }
            break;
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            io_removehandler(s->io_base, PCI_IO_SIZE,
                             eepro100_io_readb, eepro100_io_readw, eepro100_io_readl,
                             eepro100_io_writeb, eepro100_io_writew, eepro100_io_writel,
                             priv);
            s->pci_conf[addr & 0xFF] = val;
            s->io_base = (s->pci_conf[0x17] << 24) | (s->pci_conf[0x16] << 16) |
                         (s->pci_conf[0x15] << 8) | (s->pci_conf[0x14] & 0xc0);
            s->io_base &= 0xffc0;
            i8255x_log("New I/O base: %04X\n", s->io_base);
            if (s->pci_conf[0x4] & PCI_COMMAND_IO) {
                if (s->io_base != 0) {
                    io_sethandler(s->io_base, PCI_IO_SIZE,
                                  eepro100_io_readb, eepro100_io_readw, eepro100_io_readl,
                                  eepro100_io_writeb, eepro100_io_writew, eepro100_io_writel,
                                  priv);
                    i8255x_log("io handler SET at %04X\n", s->io_base);
                } else {
                    i8255x_log("io handler SKIPPED (base=0)\n");
                }
            } else {
                i8255x_log("io handler SKIPPED (cmd IO=0)\n");
            }
            break;
        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x1b:
            mem_mapping_disable(&s->bar_flash);
            s->pci_conf[addr & 0xFF] = val;
            s->flash_base = (s->pci_conf[0x1b] << 24) | (s->pci_conf[0x1a] << 16) |
                            (s->pci_conf[0x19] << 8) | s->pci_conf[0x18];
            s->flash_base &= 0xfffe0000;
            if (s->flash_base != 0) {
                mem_mapping_set_addr(&s->bar_flash, s->flash_base, PCI_FLASH_SIZE);
                if (!(s->pci_conf[0x04] & PCI_COMMAND_MEM))
                    mem_mapping_disable(&s->bar_flash);
            }
            break;
        case 0x2c:
        case 0x2d:
        case 0x2e:
        case 0x2f:
            break;   /* Subsystem ID registers are read-only */
        case 0x1c ... 0x27:
        case 0x30 ... 0x33:
            break;   /* Non-existent BARs and expansion ROM */
        case 0x3c:
            s->pci_conf[addr & 0xFF] = val;
            break;
        default:
            s->pci_conf[addr & 0xFF] = val;
            break;
    }
}

static void
nic_init_pci(eepro100_t *s)
{
    uint8_t *pci_conf = s->pci_conf;

    pci_conf[0x00] = 0x86; /* vendor */
    pci_conf[0x01] = 0x80;
    pci_conf[0x02] = (s->pci_device_id) & 0xff;
    pci_conf[0x03] = (s->pci_device_id) >> 8;
    pci_conf[0x04] = 0x07; /* command: IO, memory, bus master */
    pci_conf[0x07] = 0x02; /* status */
    pci_conf[0x08] = s->pci_revision; /* revision */
    pci_conf[0x0b] = 0x02; /* class: network ethernet */
    pci_conf[0x0a] = 0x00; /* subclass */
    pci_conf[0x09] = 0x00; /* prog if */
    pci_conf[0x2c] = 0x86; /* subsystem vendor */
    pci_conf[0x2d] = 0x80;
    pci_conf[0x2e] = (s->pci_subsystem_id) & 0xff;
    pci_conf[0x2f] = (s->pci_subsystem_id) >> 8;
    pci_conf[0x3d] = PCI_INTA;
}

static void *
nic_init(const device_t *info)
{
    eepro100_t *s = calloc(1, sizeof(eepro100_t));
    const e100_device_info_t *device_info;
    uint8_t mac_bytes[6];
    uint32_t mac;
    uint32_t variant;

    if (!s)
        return NULL;

    /* Resolve the device variant from the configuration, if needed. */
    if (info->local == 0xff)
        variant = device_get_bios_local(info, device_get_config_bios("bios"));
    else
        variant = info->local;
    device_info = &eepro100_devices[variant];

    s->device = device_info->device;
    s->pci_device_id = device_info->device_id;
    s->pci_revision = device_info->revision;
    s->pci_subsystem_id = device_info->subsystem_id;
    s->pci_subsystem_vendor_id = device_info->subsystem_vendor_id;

    s->eeprom = calloc(1, offsetof(eeprom_t, contents) + sizeof(s->eeprom->contents));
    s->eeprom->size = EEPROM_SIZE;
    s->eeprom->addrbits = 6;
    s->eeprom->writable = 1;
    /* Output DO is tristate, read results in 1. */
    s->eeprom->eedo = 1;

    /* Intel OUI. */
    mac_bytes[0] = 0x00;
    mac_bytes[1] = 0xaa;
    mac_bytes[2] = 0x00;

    /* Set up our BIA. */
    mac = device_get_config_mac("mac", -1);
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

    memcpy(s->mac, mac_bytes, 6);

    mem_mapping_add(&s->bar_mem, 0, 0,
                    eepro100_mem_readb, eepro100_mem_readw, eepro100_mem_readl,
                    eepro100_mem_writeb, eepro100_mem_writew, eepro100_mem_writel,
                    NULL, MEM_MAPPING_EXTERNAL, s);
    mem_mapping_disable(&s->bar_mem);

    mem_mapping_add(&s->bar_flash, 0, 0,
                    eepro100_flash_readb, eepro100_flash_readw, eepro100_flash_readl,
                    eepro100_flash_writeb, eepro100_flash_writew, eepro100_flash_writel,
                    NULL, MEM_MAPPING_EXTERNAL, s);
    mem_mapping_disable(&s->bar_flash);

    nic_init_pci(s);
    e100_pci_reset(s, (e100_device_info_t *) device_info);

    s->nic = network_attach(s, s->mac, eepro100_do_receive, eepro100_set_link_status);
    s->nic->byte_period = NET_PERIOD_100M;
    pci_add_card(PCI_ADD_NORMAL, eepro100_pci_read, eepro100_pci_write, s, &s->pci_slot);

    nic_reset(s);
    return s;
}

static void
nic_close(void *priv)
{
    eepro100_t *s = priv;

    free(s->eeprom);
    free(s);
}

static void
eepro100_reset(void *priv)
{
    eepro100_t *s = priv;

    nic_reset(s);
}

// clang-format off
static const device_config_t i82557_config[] = {
    {
        .name           = "bios",
        .description    = "Variant",
        .type           = CONFIG_BIOS,
        .default_string = "i82557b",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "i82557B",
                .internal_name = "i82557b",
                .bios_type     = BIOS_NORMAL,
                .files_no      = -1,
                .local         = 0,
                .size          = 0,
                .files         = { "" }
            },
            {
                .name          = "i82557C",
                .internal_name = "i82557c",
                .bios_type     = BIOS_NORMAL,
                .files_no      = -1,
                .local         = 1,
                .size          = 0,
                .files         = { "" }
            },
            { .files_no = 0 }
        }
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

static const device_config_t i8255xp_config[] = {
    {
        .name           = "bios",
        .description    = "Variant",
        .type           = CONFIG_BIOS,
        .default_string = "i82559c",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "i82558B",
                .internal_name = "i82558b",
                .bios_type     = BIOS_NORMAL,
                .files_no      = -1,
                .local         = 2,
                .size          = 0,
                .files         = { "" }
            },
            {
                .name          = "i82559A",
                .internal_name = "i82559a",
                .bios_type     = BIOS_NORMAL,
                .files_no      = -1,
                .local         = 3,
                .size          = 0,
                .files         = { "" }
            },
            {
                .name          = "i82559B",
                .internal_name = "i82559b",
                .bios_type     = BIOS_NORMAL,
                .files_no      = -1,
                .local         = 4,
                .size          = 0,
                .files         = { "" }
            },
            {
                .name          = "i82559C",
                .internal_name = "i82559c",
                .bios_type     = BIOS_NORMAL,
                .files_no      = -1,
                .local         = 5,
                .size          = 0,
                .files         = { "" }
            },
            {
                .name          = "i82559ER (Intel PRO/100+ VE)",
                .internal_name = "i82559er",
                .bios_type     = BIOS_NORMAL,
                .files_no      = -1,
                .local         = 6,
                .size          = 0,
                .files         = { "" }
            },
            { .files_no = 0 }
        }
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

const device_t i82557_device = {
    .name          = "Intel PRO/100",
    .internal_name = "i82557",
    .flags         = DEVICE_PCI,
    .local         = 0xff,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = eepro100_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = i82557_config,
    .alias         = "Intel EtherExpress PRO/100B"
};

const device_t i82558_device = {
    .name          = "Intel PRO/100+",
    .internal_name = "i82558",
    .flags         = DEVICE_PCI,
    .local         = 0xff,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = eepro100_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = i8255xp_config
};
