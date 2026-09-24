/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The 3Com EtherLink III ISA, revision B (3C509B).
 *
 *          The parallel tasking EtherLink III: eight sixteen byte register
 *          windows behind one command register, programmed I/O only, with
 *          an 8 KB packet RAM split into a transmit and a receive FIFO. The
 *          B differs from the original 3C509 in its RAM, its Internal
 *          Configuration register and Plug and Play; this is the B.
 *          The card has no jumpers. After reset it answers nothing but the
 *          ID sequence at an 01x0h port; the ID command state then reads
 *          the EEPROM out bit by bit and activates the card at the I/O
 *          base the EEPROM or the host names. A Global Reset returns it to
 *          that state, which is how drivers find it again on reload.
 *
 *          Written against the "EtherLink III Parallel Tasking ISA, EISA,
 *          Micro Channel, and PCMCIA Adapter Drivers Technical Reference",
 *          3Com part 09-0398-002B. Register and command names below are
 *          that book's. Plug and Play isolation is not modelled; the ISA
 *          contention mechanism is.
 *
 *          The transmit and receive FIFO handling follows the Fast
 *          EtherLink EISA model in net_3c59x_eisa.c, which shares the
 *          EtherLink III programming model.
 *
 * Authors: Mike Lycett and contributors
 *
 *          Copyright 2026 Mike Lycett.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/random.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/nvr.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_3C509B_LOG
int el3_do_log = ENABLE_3C509B_LOG;

static void
el3_log(const char *fmt, ...)
{
    va_list ap;

    if (el3_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define el3_log(fmt, ...)
#endif

/* The connectors, by EEPROM product ID. */
enum {
    BOARD_TPO   = 0, /* 3C509B-TPO: 10BASE-T */
    BOARD_TP    = 1, /* 3C509B-TP: 10BASE-T and AUI */
    BOARD_COMBO = 2, /* 3C509B-COMBO: 10BASE-T, 10BASE2 and AUI */
    BOARD_BNC   = 3  /* 3C509B: 10BASE2 and AUI */
};

/* ---- the register map ---------------------------------------------------- */

#define REG_COMMAND 0x0e

#define W0_MANUFACTURER_ID 0x00
#define W0_PRODUCT_ID      0x02
#define W0_CONFIG_CONTROL  0x04
#define W0_ADDRESS_CONFIG  0x06
#define W0_RESOURCE_CONFIG 0x08
#define W0_EEPROM_COMMAND  0x0a
#define W0_EEPROM_DATA     0x0c

#define W1_DATA      0x00 /* TX PIO data on write, RX PIO data on read */
#define W1_RX_STATUS 0x08
#define W1_TIMER     0x0a
#define W1_TX_STATUS 0x0b
#define W1_TX_FREE   0x0c

#define W3_INTERNAL_CONFIG 0x00
#define W3_ROM_CONTROL     0x05
#define W3_RX_FREE         0x0a
#define W3_TX_FREE         0x0c

#define W4_FIFO_DIAGNOSTIC    0x04
#define W4_NETWORK_DIAGNOSTIC 0x06
#define W4_CONTROLLER_STATUS  0x08
#define W4_MEDIA_STATUS       0x0a

#define W5_TX_START_THRESH 0x00
#define W5_TX_AVAIL_THRESH 0x02
#define W5_RX_EARLY_THRESH 0x06
#define W5_RX_FILTER       0x08
#define W5_INTERRUPT_MASK  0x0a
#define W5_READ_ZERO_MASK  0x0c

#define W6_CARRIER_LOST      0x00
#define W6_SQE_ERRORS        0x01
#define W6_MULTIPLE_COLL     0x02
#define W6_SINGLE_COLL       0x03
#define W6_LATE_COLL         0x04
#define W6_RX_OVERRUNS       0x05
#define W6_FRAMES_XMITTED_OK 0x06
#define W6_FRAMES_RCVD_OK    0x07
#define W6_FRAMES_DEFERRED   0x08
#define W6_BYTES_RCVD_OK     0x0a
#define W6_BYTES_XMITTED_OK  0x0c

/* Commands: the top five bits of the word written to REG_COMMAND. */
#define CMD_GLOBAL_RESET        0x00
#define CMD_SELECT_WINDOW       0x01
#define CMD_START_COAX          0x02
#define CMD_RX_DISABLE          0x03
#define CMD_RX_ENABLE           0x04
#define CMD_RX_RESET            0x05
#define CMD_RX_DISCARD          0x08
#define CMD_TX_ENABLE           0x09
#define CMD_TX_DISABLE          0x0a
#define CMD_TX_RESET            0x0b
#define CMD_REQUEST_INTERRUPT   0x0c
#define CMD_ACK_INTERRUPT       0x0d
#define CMD_SET_INTERRUPT_MASK  0x0e
#define CMD_SET_READ_ZERO_MASK  0x0f
#define CMD_SET_RX_FILTER       0x10
#define CMD_SET_RX_EARLY_THRESH 0x11
#define CMD_SET_TX_AVAIL_THRESH 0x12
#define CMD_SET_TX_START_THRESH 0x13
#define CMD_STATISTICS_ENABLE   0x15
#define CMD_STATISTICS_DISABLE  0x16
#define CMD_STOP_COAX           0x17
#define CMD_SET_TX_RECLAIM      0x18
#define CMD_POWER_UP            0x1b
#define CMD_POWER_DOWN_FULL     0x1c
#define CMD_POWER_AUTO          0x1d

/* Status. */
#define INT_LATCH           0x0001
#define INT_ADAPTER_FAILURE 0x0002
#define INT_TX_COMPLETE     0x0004
#define INT_TX_AVAILABLE    0x0008
#define INT_RX_COMPLETE     0x0010
#define INT_RX_EARLY        0x0020
#define INT_REQUESTED       0x0040
#define INT_UPDATE_STATS    0x0080
#define INT_SOURCES         0x00fe

/* TX Status. */
#define TXS_OVERFLOW       0x04
#define TXS_MAX_COLLISIONS 0x08
#define TXS_UNDERRUN       0x10
#define TXS_JABBER         0x20
#define TXS_INT_REQUESTED  0x40
#define TXS_COMPLETE       0x80

/* RX Status. */
#define RXS_INCOMPLETE 0x8000
#define RXS_ERROR      0x4000
#define RXS_OVERSIZE   0x0800 /* error code 001 in bits 13:11 */
#define RXS_BYTES      0x07ff

#define RXF_INDIVIDUAL 0x01
#define RXF_MULTICAST  0x02
#define RXF_BROADCAST  0x04
#define RXF_ALL        0x08

#define FIFO_TX_OVERRUN  0x0400
#define FIFO_RX_OVERRUN  0x0800
#define FIFO_RX_UNDERRUN 0x2000

#define MEDIA_CRC_STRIP_DIS    0x0004
#define MEDIA_SQE_STATS        0x0008
#define MEDIA_JABBER_ENABLE    0x0040
#define MEDIA_LINK_BEAT_ENABLE 0x0080
#define MEDIA_LINK_BEAT_DETECT 0x0800
#define MEDIA_RESERVED_ONE     0x2000
#define MEDIA_COAX_ENABLED     0x4000
#define MEDIA_TP_ENABLED       0x8000
#define MEDIA_WRITABLE         (MEDIA_CRC_STRIP_DIS | MEDIA_SQE_STATS | MEDIA_JABBER_ENABLE | MEDIA_LINK_BEAT_ENABLE)

#define DIAG_ASIC_REV_B    0x0004 /* revision 2 in bits 5:1 */
#define DIAG_STATS_ENABLED 0x0080
#define DIAG_TX_RESET_REQD 0x0100
#define DIAG_RX_ENABLED    0x0400
#define DIAG_TX_ENABLED    0x0800
#define DIAG_LOOPBACK      0xf000 /* external, encoder/decoder, controller, FIFO */
#define DIAG_FIFO_LOOPBACK 0x1000

#define CC_ENABLE 0x0001
#define CC_RESET  0x0004

#define AC_IO_BASE 0x001f
#define AC_EISA    0x001f
#define AC_ROM     0x3f00 /* ROM size and base */
#define AC_XCVR    0xc000

/* The transmit preamble's first word. */
#define TXP_INTERRUPT   0x8000
#define TXP_CRC_DISABLE 0x2000
#define TXP_LENGTH      0x07ff

/* Every threshold is a byte count with the low two bits dropped. Anything
   above 1,792 disables the three of them; 2,044 is the power-on value. */
#define THRESH_MASK 0x07fc
#define THRESH_MAX  1792
#define THRESH_OFF  2044

#define RX_FRAME_MAX (NET_MAX_FRAME + 4)
#define TX_FRAME_MAX (NET_MAX_FRAME + 4)
#define RX_QUEUE     32
#define TX_STATUS_MAX 31
#define RAM_SIZE     8192

/* The ID sequence state machine. */
enum {
    IDS_WAIT = 0,
    IDS_CMD
};

typedef struct el3_rx_frame_t {
    uint8_t  data[RX_FRAME_MAX];
    uint16_t len;
    uint16_t pos;
    uint16_t error; /* RX Status bits 14:11 */
} el3_rx_frame_t;

typedef struct el3_t {
    uint8_t board;

    netcard_t *card;
    uint8_t    mac[6];

    /* The EEPROM file, and the configuration it was built from. */
    char    nvr_name[64];
    uint8_t nvr_stamp[8];

    /* The ISA activation mechanism. */
    uint8_t  ids_state;
    uint16_t id_port;    /* the last 01x0h port a zero was written to */
    uint8_t  id_next;    /* the next byte the sequence expects */
    uint8_t  id_count;
    uint8_t  tag;
    uint16_t contention; /* the EEPROM word being shifted out onto bit 0 */
    uint8_t  active;
    uint16_t io_base;

    /* Interrupts. */
    uint8_t  irq;
    uint8_t  irq_line; /* what the card is driving now */
    uint8_t  latch;
    uint8_t  timer_running;
    uint64_t timer_start;
    uint16_t int_status;
    uint16_t int_mask;
    uint16_t read_zero_mask;

    /* Window 0. */
    uint8_t  window;
    uint16_t config_control;
    uint16_t address_config;
    uint16_t resource_config;
    uint16_t product_id;
    uint16_t eeprom_command;
    uint16_t eeprom_data;
    uint16_t eeprom[64];
    uint8_t  eeprom_write_enabled;
    uint8_t  cmd_low;

    uint8_t station_addr[6];

    uint32_t internal_config;
    uint8_t  rom_control;

    uint16_t fifo_diag;
    uint16_t network_diagnostic;
    uint16_t media_status;
    uint8_t  coax_running;

    uint16_t tx_start_thresh;
    uint16_t tx_avail_thresh;
    uint16_t rx_early_thresh;
    uint16_t rx_filter;

    uint8_t  stats_enabled;
    uint8_t  carrier_lost;
    uint8_t  sqe_errors;
    uint8_t  multiple_coll;
    uint8_t  single_coll;
    uint8_t  late_coll;
    uint8_t  rx_overruns;
    uint8_t  frames_xmitted_ok;
    uint8_t  frames_rcvd_ok;
    uint8_t  frames_deferred;
    uint16_t bytes_rcvd_ok;
    uint16_t bytes_xmitted_ok;
    uint8_t  stat_latch_hi;

    uint8_t link_up;

    /* Transmit: one frame assembled at a time. Frames completed while the
       transmitter is disabled wait in tx_pend as a six byte header (length
       kept, length written, preamble flags) and their data. */
    uint8_t  tx_enabled;
    uint32_t tx_size;
    uint8_t  tx_pre[4];
    uint8_t  tx_pre_got;
    uint16_t tx_length;
    uint16_t tx_flags;
    uint8_t  tx_frame[TX_FRAME_MAX + 4];
    uint16_t tx_got;
    uint16_t tx_count;
    uint8_t  tx_pend[(RAM_SIZE * 2) + TX_FRAME_MAX + 8];
    uint32_t tx_pend_len;
    uint32_t tx_pend_fifo;
    uint8_t  tx_out[TX_FRAME_MAX + 64];
    uint8_t  tx_pad_left;
    uint8_t  tx_status[TX_STATUS_MAX];
    uint8_t  tx_status_count;

    /* Receive. */
    uint8_t        rx_enabled;
    uint32_t       rx_size;
    uint32_t       rx_used;
    el3_rx_frame_t rx_queue[RX_QUEUE];
    uint8_t        rx_head;
    uint8_t        rx_count;
} el3_t;

static void el3_update_irq(el3_t *dev);
static uint16_t el3_status(const el3_t *dev);

/* ---- the serial EEPROM ---------------------------------------------------- */

static uint8_t
el3_xor_bytes(const uint16_t *e, uint8_t from, uint8_t to)
{
    uint8_t sum = 0;

    for (uint8_t i = from; i <= to; i++)
        sum ^= (uint8_t) ((e[i] & 0xff) ^ (e[i] >> 8));
    return sum;
}

/* A real 3C509B-COMBO's EEPROM, read through the ID port. Word 13h selects
   ISA contention only, so the card never takes part in Plug and Play
   isolation. Words 18h-3Fh are its Plug and Play resource data, as read. */
static const uint16_t el3_eeprom_image[64] = {
    0x0020, 0xaf6f, 0x105e, 0x9450, 0xbd6c, 0x0036, 0x4841, 0x6d50,
    0x0812, 0x3000, 0x0020, 0xaf6f, 0x105e, 0xbf10, 0x0000, 0x1785,
    0x2083, 0x0000, 0x0000, 0x0004, 0x0001, 0x0000, 0x0000, 0xc405,
    0x6d50, 0x9450, 0x105e, 0xaf6f, 0x0a2e, 0x1010, 0x1982, 0x3300,
    0x6f43, 0x206d, 0x4333, 0x3035, 0x4239, 0x4520, 0x6874, 0x7265,
    0x694c, 0x6b6e, 0x4920, 0x4949, 0x5015, 0x506d, 0x0394, 0x411c,
    0x80d0, 0x22f7, 0x9ea8, 0x0147, 0x0210, 0x03e0, 0x1010, 0x0981,
    0x4300, 0x0c80, 0x0de0, 0x2000, 0x0020, 0xb179, 0x0000, 0x0000
};

/* That image with the configuration the user picked written into it, as
   3C5X9CFG would: node address, product, transceiver, I/O base and IRQ,
   and the checksum over them. The node address is packed a byte pair to a
   word, first byte high. */
static void
el3_eeprom_build(el3_t *dev, uint16_t base, uint8_t irq)
{
    static const uint16_t product[4] = { 0x9550, 0x9050, 0x9450, 0x9150 };
    static const uint16_t xcvr[4]    = { 0x0000, 0x0000, 0x0000, 0xc000 };
    uint16_t             *e          = dev->eeprom;
    uint8_t               hi;
    uint8_t               lo;

    memcpy(e, el3_eeprom_image, sizeof(dev->eeprom));

    e[0x00] = (uint16_t) ((dev->mac[0] << 8) | dev->mac[1]);
    e[0x01] = (uint16_t) ((dev->mac[2] << 8) | dev->mac[3]);
    e[0x02] = (uint16_t) ((dev->mac[4] << 8) | dev->mac[5]);
    e[0x03] = product[dev->board];
    /* No boot ROM is modelled, so the card must not advertise one. */
    e[0x08] = (uint16_t) ((e[0x08] & ~(AC_XCVR | AC_ROM | AC_IO_BASE)) | xcvr[dev->board] | (((base - 0x200) >> 4) & AC_IO_BASE));
    e[0x09] = (uint16_t) ((e[0x09] & 0x0fff) | (irq << 12));
    e[0x0a] = e[0x00];
    e[0x0b] = e[0x01];
    e[0x0c] = e[0x02];

    /* High byte over words 0-0Eh less 8, 9 and 0Dh; low byte over those
       three. The real card's own checksum agrees with this. */
    hi      = el3_xor_bytes(e, 0x00, 0x07) ^ el3_xor_bytes(e, 0x0a, 0x0c) ^ el3_xor_bytes(e, 0x0e, 0x0e);
    lo      = el3_xor_bytes(e, 0x08, 0x09) ^ el3_xor_bytes(e, 0x0d, 0x0d);
    e[0x0f] = (uint16_t) ((hi << 8) | lo);
}

/* The EEPROM is kept in the VM's folder, so what a configuration utility
   writes survives power-off as it does on the card. The file carries the
   settings it was built from: if those have since been changed in the
   device's configuration, the EEPROM is rebuilt from them instead. */
static void
el3_eeprom_save(const el3_t *dev)
{
    FILE   *fp = nvr_fopen((char *) dev->nvr_name, "wb");
    uint8_t buf[sizeof(dev->nvr_stamp) + 128];

    if (fp == NULL)
        return;
    memcpy(buf, dev->nvr_stamp, sizeof(dev->nvr_stamp));
    for (uint8_t i = 0; i < 64; i++) {
        buf[sizeof(dev->nvr_stamp) + (i * 2)]     = (uint8_t) dev->eeprom[i];
        buf[sizeof(dev->nvr_stamp) + (i * 2) + 1] = (uint8_t) (dev->eeprom[i] >> 8);
    }
    fwrite(buf, 1, sizeof(buf), fp);
    fclose(fp);
}

static int
el3_eeprom_restore(el3_t *dev)
{
    FILE   *fp = nvr_fopen((char *) dev->nvr_name, "rb");
    uint8_t buf[sizeof(dev->nvr_stamp) + 128];
    size_t  got;

    if (fp == NULL)
        return 0;
    got = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if ((got != sizeof(buf)) || memcmp(buf, dev->nvr_stamp, sizeof(dev->nvr_stamp)))
        return 0;
    for (uint8_t i = 0; i < 64; i++)
        dev->eeprom[i] = (uint16_t) (buf[sizeof(dev->nvr_stamp) + (i * 2)] | (buf[sizeof(dev->nvr_stamp) + (i * 2) + 1] << 8));
    return 1;
}

/* The automatic configuration at reset: Address Configuration, Resource
   Configuration and the Product ID from words 8, 9 and 3, and Internal
   Configuration from words 12h and 13h. */
static void
el3_eeprom_load(el3_t *dev)
{
    dev->address_config  = dev->eeprom[0x08];
    dev->resource_config = dev->eeprom[0x09];
    dev->product_id      = dev->eeprom[0x03];
    dev->internal_config = ((uint32_t) dev->eeprom[0x13] << 16) | dev->eeprom[0x12];
    /* RAM width reads zero on this ASIC whatever the EEPROM says. */
    dev->internal_config &= ~0x00000008U;
}

/* ---- the RAM partition ---------------------------------------------------- */

static void
el3_partition(el3_t *dev)
{
    uint32_t total = ((dev->internal_config & 7) == 2) ? 32768 : RAM_SIZE;

    switch ((dev->internal_config >> 16) & 3) {
        case 1: /* TX:RX 1:3 */
            dev->tx_size = total / 4;
            break;
        case 2: /* 1:1 */
            dev->tx_size = total / 2;
            break;
        default: /* 3:5 */
            dev->tx_size = total * 3 / 8;
            break;
    }
    dev->rx_size = total - dev->tx_size;
}

/* ---- interrupts ------------------------------------------------------------- */

static uint8_t
el3_irq_of(uint16_t resource_config)
{
    switch (resource_config >> 12) {
        case 0x3:
        case 0x5:
        case 0x7:
        case 0x9:
        case 0xa:
        case 0xb:
        case 0xc:
        case 0xf:
            return (uint8_t) (resource_config >> 12);
        default:
            return 0; /* the other values disable the IRQ line drivers */
    }
}

static uint64_t
el3_now(void)
{
#ifdef USE_DYNAREC
    if (cpu_use_dynarec)
        update_tsc();
#endif
    return tsc;
}

/* Timer: free running at 3.2 us a count from the rising edge of the
   interrupt, stopping at FFh. Drivers start it with Request Interrupt to
   time their own start-up. */
static uint8_t
el3_timer_read(const el3_t *dev)
{
    double per_tick = ((double) TIMER_USEC / 4294967296.0) * 3.2;
    double ticks;

    if (!dev->timer_running || (per_tick <= 0.0))
        return 0;

    ticks = (double) (el3_now() - dev->timer_start) / per_tick;
    return (ticks >= 255.0) ? 0xff : (uint8_t) ticks;
}

/* The Interrupt Latch is the interrupt. The line drivers are off until the
   driver sets ENA, and off whenever Window 0 is selected. ISA interrupts
   are edges, so the PIC hears only the transitions. */
static void
el3_update_irq(el3_t *dev)
{
    uint16_t live = dev->int_status & dev->read_zero_mask & dev->int_mask & INT_SOURCES;
    uint8_t  line;

    if (live && !dev->latch) {
        dev->latch         = 1;
        dev->timer_start   = el3_now();
        dev->timer_running = 1;
    }

    line = dev->latch && dev->irq && dev->active && (dev->config_control & CC_ENABLE) && (dev->window != 0);
    if (line == dev->irq_line)
        return;
    dev->irq_line = line;
    if (line)
        picint(1 << dev->irq);
    else
        picintc(1 << dev->irq);
    el3_log("3C509B: IRQ %i %s (status %04x)\n", dev->irq, line ? "up" : "down", el3_status(dev));
}

static void
el3_set_irq(el3_t *dev, uint8_t irq)
{
    if (irq == dev->irq)
        return;
    if (dev->irq_line)
        picintc(1 << dev->irq);
    dev->irq_line = 0;
    dev->irq      = irq;
    el3_update_irq(dev);
}

static void
el3_raise(el3_t *dev, uint16_t bits)
{
    dev->int_status |= bits;
    el3_update_irq(dev);
}

static void
el3_lower(el3_t *dev, uint16_t bits)
{
    dev->int_status &= (uint16_t) ~bits;
    el3_update_irq(dev);
}

/* Adapter Failure stands for its causes in FIFO Diagnostic, each cleared
   only by the reset of its own side. */
static void
el3_adapter_failure(el3_t *dev, uint16_t cause)
{
    dev->fifo_diag |= cause;
    el3_raise(dev, INT_ADAPTER_FAILURE);
}

static void
el3_adapter_failure_check(el3_t *dev)
{
    if (!(dev->fifo_diag & (FIFO_TX_OVERRUN | FIFO_RX_UNDERRUN)))
        el3_lower(dev, INT_ADAPTER_FAILURE);
}

static uint16_t
el3_status(const el3_t *dev)
{
    uint16_t ret = (uint16_t) ((dev->window << 13) | (dev->int_status & dev->read_zero_mask & INT_SOURCES));

    if (dev->latch)
        ret |= INT_LATCH;
    return ret;
}

/* ---- statistics ------------------------------------------------------------- */

/* Update Statistics stands while any counter is past half way; the two
   16-bit counters wait for their top three bits. */
static void
el3_stats_indicate(el3_t *dev)
{
    if ((dev->carrier_lost >= 0x08) || (dev->sqe_errors >= 0x08) || (dev->multiple_coll >= 0x20) ||
        (dev->single_coll >= 0x20) || (dev->late_coll >= 0x80) || (dev->rx_overruns >= 0x80) ||
        (dev->frames_xmitted_ok >= 0x80) || (dev->frames_rcvd_ok >= 0x80) || (dev->frames_deferred >= 0x80) ||
        ((dev->bytes_rcvd_ok & 0xe000) == 0xe000) || ((dev->bytes_xmitted_ok & 0xe000) == 0xe000))
        el3_raise(dev, INT_UPDATE_STATS);
    else
        el3_lower(dev, INT_UPDATE_STATS);
}

static void
el3_stat_count(el3_t *dev, uint8_t *frames, uint16_t *bytes, uint16_t n)
{
    if (!dev->stats_enabled)
        return;
    (*frames)++;
    *bytes = (uint16_t) (*bytes + n);
    el3_stats_indicate(dev);
}

static uint8_t
el3_stat8_read(el3_t *dev, uint8_t *counter)
{
    uint8_t ret = *counter;

    *counter = 0;
    el3_stats_indicate(dev);
    return ret;
}

/* A 16-bit statistic read as two bytes: the low byte read takes the whole
   counter, holding the high byte for the read that follows. */
static uint8_t
el3_stat16_read(el3_t *dev, uint16_t *counter, uint8_t high)
{
    uint8_t ret;

    if (high)
        return dev->stat_latch_hi;
    ret                = (uint8_t) (*counter & 0xff);
    dev->stat_latch_hi = (uint8_t) (*counter >> 8);
    *counter           = 0;
    el3_stats_indicate(dev);
    return ret;
}

/* Writes add, and the narrow counters saturate. */
static void
el3_stat_add(el3_t *dev, uint8_t *counter, uint8_t n, uint8_t max)
{
    unsigned v = *counter + n;

    *counter = (uint8_t) ((v > max) ? max : v);
    el3_stats_indicate(dev);
}

/* ---- CRC -------------------------------------------------------------------- */

/* 86Box frames carry no FCS; this is the one the wire would have. */
static uint32_t
el3_crc32(const uint8_t *data, uint16_t len)
{
    uint32_t crc = 0xffffffff;

    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return ~crc;
}

/* ---- receive ------------------------------------------------------------- */

static el3_rx_frame_t *
el3_rx_top(el3_t *dev)
{
    if (dev->rx_count == 0)
        return NULL;
    return &dev->rx_queue[dev->rx_head];
}

static void
el3_rx_indicate(el3_t *dev)
{
    /* RX Complete masks RX Early, and every frame here arrives whole. */
    if (dev->rx_count > 0)
        el3_raise(dev, INT_RX_COMPLETE);
    else
        el3_lower(dev, INT_RX_COMPLETE);
}

static void
el3_rx_discard(el3_t *dev)
{
    el3_rx_frame_t *f = el3_rx_top(dev);

    if (f == NULL)
        return;
    dev->rx_used -= (f->len + 3U) & ~3U;
    dev->rx_head = (uint8_t) ((dev->rx_head + 1) % RX_QUEUE);
    dev->rx_count--;
    dev->fifo_diag &= (uint16_t) ~FIFO_RX_OVERRUN;
    el3_rx_indicate(dev);
}

static void
el3_rx_flush(el3_t *dev)
{
    dev->rx_head  = 0;
    dev->rx_count = 0;
    dev->rx_used  = 0;
    dev->fifo_diag &= (uint16_t) ~FIFO_RX_OVERRUN;
    el3_lower(dev, INT_RX_COMPLETE | INT_RX_EARLY);
}

static int
el3_rx_accept(const el3_t *dev, const uint8_t *dst)
{
    if (dev->rx_filter & RXF_ALL)
        return 1;
    if ((dst[0] & dst[1] & dst[2] & dst[3] & dst[4] & dst[5]) == 0xff)
        return (dev->rx_filter & (RXF_BROADCAST | RXF_MULTICAST)) != 0;
    if (dst[0] & 0x01)
        return (dev->rx_filter & RXF_MULTICAST) != 0;
    if (dev->rx_filter & RXF_INDIVIDUAL)
        return memcmp(dst, dev->station_addr, 6) == 0;
    return 0;
}

/* A frame into the RX FIFO. mac is zero for FIFO loopback, which bypasses
   the address filter, padding and CRC handling. Returns zero only when the
   FIFO has no room, so the network offers the frame again later; a frame
   the filter rejects is taken and dropped. */
static int
el3_rx_frame(el3_t *dev, const uint8_t *buf, int io_len, int mac)
{
    el3_rx_frame_t *f;
    uint16_t        len = (uint16_t) io_len;
    uint32_t        crc;

    if (!dev->rx_enabled || (io_len < (mac ? 14 : 1)))
        return 1;
    if (mac && !el3_rx_accept(dev, buf))
        return 1;

    if (len > NET_MAX_FRAME)
        len = NET_MAX_FRAME;
    /* The host stack does not pad; every sender on a wire does. */
    if (mac && (len < 60))
        len = 60;

    if ((dev->rx_count >= RX_QUEUE) || (dev->rx_used + ((len + 4 + 3U) & ~3U) > dev->rx_size)) {
        dev->fifo_diag |= FIFO_RX_OVERRUN;
        return 0;
    }

    f = &dev->rx_queue[(dev->rx_head + dev->rx_count) % RX_QUEUE];
    memcpy(f->data, buf, (io_len < len) ? io_len : len);
    if (io_len < len)
        memset(f->data + io_len, 0, len - io_len);
    f->len   = len;
    f->pos   = 0;
    f->error = 0;
    if (mac && (len > 1514))
        f->error = RXS_ERROR | RXS_OVERSIZE;
    if (mac && (dev->media_status & MEDIA_CRC_STRIP_DIS)) {
        crc              = el3_crc32(f->data, len);
        f->data[len]     = (uint8_t) crc;
        f->data[len + 1] = (uint8_t) (crc >> 8);
        f->data[len + 2] = (uint8_t) (crc >> 16);
        f->data[len + 3] = (uint8_t) (crc >> 24);
        f->len += 4;
    }
    dev->rx_used += (f->len + 3U) & ~3U;
    dev->rx_count++;

    if (mac && !f->error)
        el3_stat_count(dev, &dev->frames_rcvd_ok, &dev->bytes_rcvd_ok, f->len);

    el3_log("3C509B: received %u bytes, %u queued\n", f->len, dev->rx_count);
    el3_rx_indicate(dev);
    return 1;
}

static int
el3_rx(void *priv, uint8_t *buf, int io_len)
{
    return el3_rx_frame((el3_t *) priv, buf, io_len, 1);
}

/* Reading into the padding to the dword boundary is allowed; RX Bytes goes
   negative through it. Reading past the padding, or an empty FIFO, is an
   RX underrun. */
static uint8_t
el3_rx_data_read(el3_t *dev)
{
    el3_rx_frame_t *f = el3_rx_top(dev);

    if (f == NULL) {
        el3_adapter_failure(dev, FIFO_RX_UNDERRUN);
        return 0;
    }
    if (f->pos < f->len)
        return f->data[f->pos++];
    if (f->pos < ((f->len + 3U) & ~3U)) {
        f->pos++;
        return 0;
    }
    el3_adapter_failure(dev, FIFO_RX_UNDERRUN);
    return 0;
}

static uint16_t
el3_rx_status(const el3_t *dev)
{
    const el3_rx_frame_t *f;

    if (dev->rx_count == 0)
        return RXS_INCOMPLETE;
    f = &dev->rx_queue[dev->rx_head];
    return (uint16_t) (((f->len - f->pos) & RXS_BYTES) | f->error);
}

/* ---- transmit -------------------------------------------------------------- */

static void
el3_tx_status_push(el3_t *dev, uint8_t status)
{
    if (dev->tx_status_count >= TX_STATUS_MAX) {
        dev->tx_status[TX_STATUS_MAX - 1] |= TXS_OVERFLOW;
        dev->tx_enabled = 0;
        dev->network_diagnostic &= (uint16_t) ~DIAG_TX_ENABLED;
    } else
        dev->tx_status[dev->tx_status_count++] = status;
    el3_raise(dev, INT_TX_COMPLETE);
}

static void
el3_tx_status_pop(el3_t *dev)
{
    if (dev->tx_status_count == 0)
        return;
    memmove(dev->tx_status, dev->tx_status + 1, dev->tx_status_count - 1);
    dev->tx_status_count--;
    if (dev->tx_status_count == 0)
        el3_lower(dev, INT_TX_COMPLETE);
}

/* The space the FIFO has left: each frame costs its preamble and its data
   padded to a dword. */
static uint16_t
el3_tx_free(const el3_t *dev)
{
    uint32_t used = dev->tx_pend_fifo;

    if (dev->tx_pre_got != 0)
        used += 4 + ((dev->tx_count + 3U) & ~3U);
    return (uint16_t) ((used > dev->tx_size) ? 0 : (dev->tx_size - used));
}

static void
el3_tx_avail_check(el3_t *dev)
{
    if ((dev->tx_avail_thresh <= THRESH_MAX) && (el3_tx_free(dev) > dev->tx_avail_thresh))
        el3_raise(dev, INT_TX_AVAILABLE);
}

static void
el3_tx_frame_reset(el3_t *dev)
{
    dev->tx_pre_got  = 0;
    dev->tx_got      = 0;
    dev->tx_count    = 0;
    dev->tx_length   = 0;
    dev->tx_flags    = 0;
    dev->tx_pad_left = 0;
}

/* A frame leaves the FIFO: onto the wire, or back into the receiver under
   a loopback mode. Status is posted only when the preamble asked for it. */
static void
el3_tx_emit(el3_t *dev, const uint8_t *data, uint16_t len, uint16_t flags)
{
    uint8_t *frame = dev->tx_out;

    memcpy(frame, data, len);

    if (dev->network_diagnostic & DIAG_FIFO_LOOPBACK) {
        el3_rx_frame(dev, frame, len, 0);
    } else {
        /* The driver supplied the FCS; 86Box's wire carries none. */
        if ((flags & TXP_CRC_DISABLE) && (len >= 4))
            len -= 4;
        while (len < 60)
            frame[len++] = 0;
        if (dev->network_diagnostic & DIAG_LOOPBACK)
            el3_rx_frame(dev, frame, len, 1);
        else
            network_tx(dev->card, frame, len);
        el3_stat_count(dev, &dev->frames_xmitted_ok, &dev->bytes_xmitted_ok, len);
    }

    if (flags & TXP_INTERRUPT)
        el3_tx_status_push(dev, TXS_COMPLETE | TXS_INT_REQUESTED);
}

/* Frames completed while the transmitter was disabled are neither sent nor
   discarded; TX Enable sends them. */
static void
el3_tx_drain(el3_t *dev)
{
    uint32_t pos = 0;

    while (dev->tx_enabled && (pos < dev->tx_pend_len)) {
        const uint8_t *r       = dev->tx_pend + pos;
        uint16_t       kept    = (uint16_t) (r[0] | (r[1] << 8));
        uint16_t       written = (uint16_t) (r[2] | (r[3] << 8));
        uint16_t       flags   = (uint16_t) (r[4] | (r[5] << 8));

        el3_tx_emit(dev, r + 6, kept, flags);
        dev->tx_pend_fifo -= 4 + ((written + 3U) & ~3U);
        pos += 6 + kept;
    }
    if (pos > 0) {
        memmove(dev->tx_pend, dev->tx_pend + pos, dev->tx_pend_len - pos);
        dev->tx_pend_len -= pos;
    }
    el3_tx_avail_check(dev);
}

static void
el3_tx_send(el3_t *dev)
{
    if (dev->tx_enabled && (dev->tx_pend_len == 0))
        el3_tx_emit(dev, dev->tx_frame, dev->tx_got, dev->tx_flags);
    else {
        uint8_t *r = dev->tx_pend + dev->tx_pend_len;

        r[0] = (uint8_t) dev->tx_got;
        r[1] = (uint8_t) (dev->tx_got >> 8);
        r[2] = (uint8_t) dev->tx_count;
        r[3] = (uint8_t) (dev->tx_count >> 8);
        r[4] = (uint8_t) dev->tx_flags;
        r[5] = (uint8_t) (dev->tx_flags >> 8);
        memcpy(r + 6, dev->tx_frame, dev->tx_got);
        dev->tx_pend_len += 6 + dev->tx_got;
        dev->tx_pend_fifo += 4 + ((dev->tx_count + 3U) & ~3U);
    }

    el3_tx_frame_reset(dev);
    el3_tx_avail_check(dev);
}

/* One byte into the TX FIFO. Each frame opens with the two word preamble,
   and its data is padded to a dword; the pad is swallowed. */
static void
el3_tx_data_write(el3_t *dev, uint8_t val)
{
    if (dev->tx_pad_left) {
        dev->tx_pad_left--;
        return;
    }
    if (dev->tx_pre_got < 4) {
        dev->tx_pre[dev->tx_pre_got++] = val;
        if (dev->tx_pre_got == 4) {
            uint16_t pre   = (uint16_t) (dev->tx_pre[0] | (dev->tx_pre[1] << 8));
            dev->tx_length = pre & TXP_LENGTH;
            dev->tx_flags  = pre & (TXP_INTERRUPT | TXP_CRC_DISABLE);
            if (dev->tx_length == 0)
                el3_tx_frame_reset(dev);
        }
        return;
    }
    if (dev->tx_pend_fifo + 4 + dev->tx_count >= dev->tx_size) {
        el3_adapter_failure(dev, FIFO_TX_OVERRUN);
        dev->tx_enabled = 0;
        dev->network_diagnostic &= (uint16_t) ~DIAG_TX_ENABLED;
        return;
    }
    if (dev->tx_got < TX_FRAME_MAX)
        dev->tx_frame[dev->tx_got++] = val;
    dev->tx_count++;
    if (dev->tx_count == dev->tx_length) {
        uint8_t pad = (uint8_t) ((4 - (dev->tx_length & 3)) & 3);

        el3_tx_send(dev);
        dev->tx_pad_left = pad;
    }
}

static void
el3_tx_flush(el3_t *dev)
{
    el3_tx_frame_reset(dev);
    dev->tx_pend_len  = 0;
    dev->tx_pend_fifo = 0;
    el3_lower(dev, INT_TX_AVAILABLE);
}

/* ---- reset ---------------------------------------------------------------- */

/* The masks name what is kept out of the reset: bit 2 the network side,
   bit 3 the FIFO. */
static void
el3_rx_reset(el3_t *dev, uint8_t mask)
{
    if (!(mask & 0x04)) {
        dev->rx_enabled = 0;
        dev->rx_filter  = 0;
        dev->network_diagnostic &= (uint16_t) ~DIAG_RX_ENABLED;
    }
    if (!(mask & 0x08)) {
        el3_rx_flush(dev);
        dev->rx_filter       = 0;
        dev->rx_early_thresh = THRESH_OFF;
        dev->fifo_diag &= (uint16_t) ~FIFO_RX_UNDERRUN;
    }
    el3_adapter_failure_check(dev);
}

static void
el3_tx_reset(el3_t *dev, uint8_t mask)
{
    if (!(mask & 0x04)) {
        dev->tx_enabled = 0;
        dev->network_diagnostic &= (uint16_t) ~(DIAG_TX_ENABLED | DIAG_TX_RESET_REQD);
        dev->tx_status_count = 0;
        el3_lower(dev, INT_TX_COMPLETE);
    }
    if (!(mask & 0x08)) {
        el3_tx_flush(dev);
        dev->tx_start_thresh = THRESH_OFF;
        dev->tx_avail_thresh = THRESH_OFF;
        dev->fifo_diag &= (uint16_t) ~FIFO_TX_OVERRUN;
    }
    el3_adapter_failure_check(dev);
}

static void el3_deactivate(el3_t *dev);

/* Global Reset. Bit 4 is the auto-initialize state machine: resetting it
   rereads the EEPROM and returns the card to ID_WAIT, inactive, so a driver
   that reset it has to find it again with the ID sequence. Bit 5 is the
   host interface. */
static void
el3_global_reset(el3_t *dev, uint8_t mask)
{
    el3_rx_reset(dev, mask);
    el3_tx_reset(dev, mask);

    if (!(mask & 0x04)) {
        dev->media_status       = 0;
        dev->network_diagnostic = DIAG_ASIC_REV_B;
        dev->stats_enabled      = 0;
        dev->coax_running       = 0;
        memset(dev->station_addr, 0, sizeof(dev->station_addr));
    }
    if (!(mask & 0x20)) {
        dev->int_status     = 0;
        dev->int_mask       = 0;
        dev->read_zero_mask = 0;
        dev->latch          = 0;
        dev->timer_running  = 0;
        dev->window         = 0;
        dev->cmd_low        = 0;
        dev->rom_control    = 0;
    }
    if (!(mask & 0x10)) {
        dev->eeprom_command       = 0;
        dev->eeprom_data          = 0;
        dev->eeprom_write_enabled = 0;
        dev->config_control       = 0;
        el3_eeprom_load(dev);
        el3_deactivate(dev);
        dev->ids_state = IDS_WAIT;
        dev->id_next   = 0xff;
        dev->id_count  = 0;
        dev->tag       = 0;
    }
    el3_partition(dev);
    el3_set_irq(dev, el3_irq_of(dev->resource_config));
    el3_update_irq(dev);
}

/* ---- the EEPROM commands -------------------------------------------------- */

/* Writes can only clear bits, erases set them. The part's 162 us to 11 ms
   are not modelled; EEPROM Busy never reads set. */
static void
el3_eeprom_command(el3_t *dev, uint8_t val)
{
    uint8_t address = val & 0x3f;
    uint8_t changed = 0;

    dev->eeprom_command = val;
    switch (val >> 6) {
        case 0:
            switch ((address >> 4) & 3) {
                case 0: /* Erase/Write Disable */
                    dev->eeprom_write_enabled = 0;
                    break;
                case 1: /* Write All */
                    if (dev->eeprom_write_enabled) {
                        for (uint8_t i = 0; i < 64; i++)
                            dev->eeprom[i] &= dev->eeprom_data;
                        changed = 1;
                    }
                    break;
                case 2: /* Erase All */
                    if (dev->eeprom_write_enabled) {
                        memset(dev->eeprom, 0xff, sizeof(dev->eeprom));
                        changed = 1;
                    }
                    break;
                default: /* Erase/Write Enable */
                    dev->eeprom_write_enabled = 1;
                    break;
            }
            break;
        case 1: /* Write */
            if (dev->eeprom_write_enabled) {
                dev->eeprom[address] &= dev->eeprom_data;
                changed = 1;
            }
            break;
        case 2: /* Read */
            dev->eeprom_data = dev->eeprom[address];
            break;
        default: /* Erase */
            if (dev->eeprom_write_enabled) {
                dev->eeprom[address] = 0xffff;
                changed = 1;
            }
            break;
    }
    if (changed)
        el3_eeprom_save(dev);
}

/* ---- commands --------------------------------------------------------------- */

static void
el3_command(el3_t *dev, uint16_t val)
{
    uint8_t  cmd   = (uint8_t) (val >> 11);
    uint16_t param = val & 0x07ff;

    /* Commands act in every window. The book lists only Select Register
       Window as valid in Window 0, but ELNK3.VXD issues TX Reset, RX Reset
       and Acknowledge Interrupt there and depends on them. */
    switch (cmd) {
        case CMD_GLOBAL_RESET:
            el3_global_reset(dev, (uint8_t) (param & 0x3f));
            break;
        case CMD_SELECT_WINDOW:
            dev->window = param & 7;
            el3_update_irq(dev);
            break;
        case CMD_START_COAX:
            dev->coax_running = 1;
            break;
        case CMD_RX_DISABLE:
            dev->rx_enabled = 0;
            dev->network_diagnostic &= (uint16_t) ~DIAG_RX_ENABLED;
            break;
        case CMD_RX_ENABLE:
            dev->rx_enabled = 1;
            dev->network_diagnostic |= DIAG_RX_ENABLED;
            break;
        case CMD_RX_RESET:
            el3_rx_reset(dev, (uint8_t) (param & 0x0f));
            break;
        case CMD_RX_DISCARD:
            el3_rx_discard(dev);
            break;
        case CMD_TX_ENABLE:
            dev->tx_enabled = 1;
            dev->network_diagnostic |= DIAG_TX_ENABLED;
            el3_tx_drain(dev);
            break;
        case CMD_TX_DISABLE:
            dev->tx_enabled = 0;
            dev->network_diagnostic &= (uint16_t) ~DIAG_TX_ENABLED;
            break;
        case CMD_TX_RESET:
            el3_tx_reset(dev, (uint8_t) (param & 0x0f));
            break;
        case CMD_REQUEST_INTERRUPT:
            el3_raise(dev, INT_REQUESTED);
            break;
        case CMD_ACK_INTERRUPT:
            if (param & INT_LATCH)
                dev->latch = 0;
            /* Acknowledging TX Available also disables its threshold. */
            if ((param & INT_TX_AVAILABLE) && (dev->int_status & INT_TX_AVAILABLE))
                dev->tx_avail_thresh = THRESH_OFF;
            dev->int_status &= (uint16_t) ~(param & (INT_TX_AVAILABLE | INT_RX_EARLY | INT_REQUESTED));
            el3_update_irq(dev);
            break;
        case CMD_SET_INTERRUPT_MASK:
            dev->int_mask = param & INT_SOURCES;
            el3_update_irq(dev);
            break;
        case CMD_SET_READ_ZERO_MASK:
            dev->read_zero_mask = param & INT_SOURCES;
            el3_update_irq(dev);
            break;
        case CMD_SET_RX_FILTER:
            dev->rx_filter = param & 0x0f;
            break;
        case CMD_SET_RX_EARLY_THRESH:
            dev->rx_early_thresh = param & THRESH_MASK;
            break;
        case CMD_SET_TX_AVAIL_THRESH:
            dev->tx_avail_thresh = param & THRESH_MASK;
            el3_tx_avail_check(dev);
            break;
        case CMD_SET_TX_START_THRESH:
            dev->tx_start_thresh = param & THRESH_MASK;
            break;
        case CMD_STATISTICS_ENABLE:
            dev->stats_enabled = 1;
            dev->network_diagnostic |= DIAG_STATS_ENABLED;
            break;
        case CMD_STATISTICS_DISABLE:
            dev->stats_enabled = 0;
            dev->network_diagnostic &= (uint16_t) ~DIAG_STATS_ENABLED;
            break;
        case CMD_STOP_COAX:
            dev->coax_running = 0;
            break;
        case CMD_SET_TX_RECLAIM: /* Micro Channel only */
        case CMD_POWER_UP:
        case CMD_POWER_DOWN_FULL:
        case CMD_POWER_AUTO:
            break;
        default:
            el3_log("3C509B: reserved command %02x\n", cmd);
            break;
    }
}

/* ---- the register file ----------------------------------------------------- */

static uint16_t
el3_media_status(const el3_t *dev)
{
    uint16_t ret  = (uint16_t) ((dev->media_status & MEDIA_WRITABLE) | MEDIA_RESERVED_ONE);
    uint16_t xcvr = dev->address_config & AC_XCVR;

    if (xcvr == 0x0000)
        ret |= MEDIA_TP_ENABLED;
    else if ((xcvr == 0xc000) && dev->coax_running)
        ret |= MEDIA_COAX_ENABLED;
    /* Link beat from 86Box's link state, and forced on while it is not
       being checked. */
    if (dev->link_up || !(dev->media_status & MEDIA_LINK_BEAT_ENABLE))
        ret |= MEDIA_LINK_BEAT_DETECT;
    return ret;
}

/* The POR jumper bits in the high byte of Configuration Control: an ISA
   bus interface, normal mode, internal VCO, and the connectors fitted. */
static uint8_t
el3_config_control_hi(const el3_t *dev)
{
    static const uint8_t connectors[4] = { 0x02, 0x22, 0x32, 0x30 }; /* AUI 0x20, coax 0x10, TP 0x02 */

    return (uint8_t) (0x80 | 0x40 | 0x0c | 0x01 | connectors[dev->board]);
}

static uint8_t
el3_reg_read(el3_t *dev, uint8_t off)
{
    uint16_t w;

    if (off >= REG_COMMAND)
        return (uint8_t) (el3_status(dev) >> ((off & 1) * 8));

    switch (dev->window) {
        case 0:
            switch (off) {
                case W0_MANUFACTURER_ID:
                    return 0x50;
                case W0_MANUFACTURER_ID + 1:
                    return 0x6d;
                case W0_PRODUCT_ID:
                case W0_PRODUCT_ID + 1:
                    return (uint8_t) (dev->product_id >> ((off & 1) * 8));
                case W0_CONFIG_CONTROL:
                    return (uint8_t) (dev->config_control & CC_ENABLE);
                case W0_CONFIG_CONTROL + 1:
                    return el3_config_control_hi(dev);
                case W0_ADDRESS_CONFIG:
                case W0_ADDRESS_CONFIG + 1:
                    return (uint8_t) (dev->address_config >> ((off & 1) * 8));
                case W0_RESOURCE_CONFIG:
                case W0_RESOURCE_CONFIG + 1:
                    return (uint8_t) (dev->resource_config >> ((off & 1) * 8));
                case W0_EEPROM_COMMAND:
                    return (uint8_t) dev->eeprom_command;
                case W0_EEPROM_COMMAND + 1:
                    /* EEPROM Busy and Test Mode are never set. The book does
                       not give TAG's bit position here; the low bits are
                       assumed. */
                    return dev->tag;
                case W0_EEPROM_DATA:
                case W0_EEPROM_DATA + 1:
                    return (uint8_t) (dev->eeprom_data >> ((off & 1) * 8));
                default:
                    break;
            }
            break;

        case 1:
            switch (off) {
                case W1_DATA:
                case W1_DATA + 1:
                case W1_DATA + 2:
                case W1_DATA + 3:
                    return el3_rx_data_read(dev);
                case W1_RX_STATUS:
                case W1_RX_STATUS + 1:
                    return (uint8_t) (el3_rx_status(dev) >> ((off & 1) * 8));
                case W1_TIMER:
                    return el3_timer_read(dev);
                case W1_TX_STATUS:
                    return dev->tx_status_count ? dev->tx_status[0] : 0;
                case W1_TX_FREE:
                case W1_TX_FREE + 1:
                    /* Truncated to a dword multiple here, exact in Window 3. */
                    return (uint8_t) ((el3_tx_free(dev) & ~3U) >> ((off & 1) * 8));
                default:
                    break;
            }
            break;

        case 2:
            if (off < 6)
                return dev->station_addr[off];
            break;

        case 3:
            switch (off) {
                case W3_INTERNAL_CONFIG:
                case W3_INTERNAL_CONFIG + 1:
                case W3_INTERNAL_CONFIG + 2:
                case W3_INTERNAL_CONFIG + 3:
                    return (uint8_t) (dev->internal_config >> ((off & 3) * 8));
                case W3_ROM_CONTROL:
                    return dev->rom_control;
                case W3_RX_FREE:
                case W3_RX_FREE + 1:
                    w = (uint16_t) ((dev->rx_used >= dev->rx_size) ? 0 : (dev->rx_size - dev->rx_used));
                    return (uint8_t) (w >> ((off & 1) * 8));
                case W3_TX_FREE:
                case W3_TX_FREE + 1:
                    return (uint8_t) (el3_tx_free(dev) >> ((off & 1) * 8));
                default:
                    break;
            }
            break;

        case 4:
            switch (off) {
                case W4_FIFO_DIAGNOSTIC:
                case W4_FIFO_DIAGNOSTIC + 1:
                    return (uint8_t) (dev->fifo_diag >> ((off & 1) * 8));
                case W4_NETWORK_DIAGNOSTIC:
                case W4_NETWORK_DIAGNOSTIC + 1:
                    return (uint8_t) (dev->network_diagnostic >> ((off & 1) * 8));
                case W4_CONTROLLER_STATUS:
                case W4_CONTROLLER_STATUS + 1:
                    return 0;
                case W4_MEDIA_STATUS:
                case W4_MEDIA_STATUS + 1:
                    return (uint8_t) (el3_media_status(dev) >> ((off & 1) * 8));
                default:
                    break;
            }
            break;

        case 5:
            switch (off) {
                case W5_TX_START_THRESH:
                case W5_TX_START_THRESH + 1:
                    return (uint8_t) (dev->tx_start_thresh >> ((off & 1) * 8));
                case W5_TX_AVAIL_THRESH:
                case W5_TX_AVAIL_THRESH + 1:
                    return (uint8_t) (dev->tx_avail_thresh >> ((off & 1) * 8));
                case W5_RX_EARLY_THRESH:
                case W5_RX_EARLY_THRESH + 1:
                    return (uint8_t) (dev->rx_early_thresh >> ((off & 1) * 8));
                case W5_RX_FILTER:
                    return (uint8_t) dev->rx_filter;
                case W5_INTERRUPT_MASK:
                case W5_INTERRUPT_MASK + 1:
                    return (uint8_t) (dev->int_mask >> ((off & 1) * 8));
                case W5_READ_ZERO_MASK:
                case W5_READ_ZERO_MASK + 1:
                    return (uint8_t) (dev->read_zero_mask >> ((off & 1) * 8));
                default:
                    break;
            }
            break;

        case 6:
            switch (off) {
                case W6_CARRIER_LOST:
                    return el3_stat8_read(dev, &dev->carrier_lost);
                case W6_SQE_ERRORS:
                    return el3_stat8_read(dev, &dev->sqe_errors);
                case W6_MULTIPLE_COLL:
                    return el3_stat8_read(dev, &dev->multiple_coll);
                case W6_SINGLE_COLL:
                    return el3_stat8_read(dev, &dev->single_coll);
                case W6_LATE_COLL:
                    return el3_stat8_read(dev, &dev->late_coll);
                case W6_RX_OVERRUNS:
                    return el3_stat8_read(dev, &dev->rx_overruns);
                case W6_FRAMES_XMITTED_OK:
                    return el3_stat8_read(dev, &dev->frames_xmitted_ok);
                case W6_FRAMES_RCVD_OK:
                    return el3_stat8_read(dev, &dev->frames_rcvd_ok);
                case W6_FRAMES_DEFERRED:
                    return el3_stat8_read(dev, &dev->frames_deferred);
                case W6_BYTES_RCVD_OK:
                case W6_BYTES_RCVD_OK + 1:
                    return el3_stat16_read(dev, &dev->bytes_rcvd_ok, off & 1);
                case W6_BYTES_XMITTED_OK:
                case W6_BYTES_XMITTED_OK + 1:
                    return el3_stat16_read(dev, &dev->bytes_xmitted_ok, off & 1);
                default:
                    break;
            }
            break;

        default:
            break;
    }
    return 0;
}

static void el3_activate(el3_t *dev, uint16_t base);

static void
el3_reg_write(el3_t *dev, uint8_t off, uint8_t val)
{
    /* A command runs when its high byte is written, so a word write and a
       low-then-high byte pair are the same command. */
    if (off == REG_COMMAND) {
        dev->cmd_low = val;
        return;
    }
    if (off == REG_COMMAND + 1) {
        el3_command(dev, (uint16_t) ((val << 8) | dev->cmd_low));
        dev->cmd_low = 0;
        return;
    }

    switch (dev->window) {
        case 0:
            switch (off) {
                case W0_CONFIG_CONTROL:
                    if (val & CC_RESET) {
                        el3_global_reset(dev, 0);
                        break;
                    }
                    dev->config_control = val & CC_ENABLE;
                    el3_update_irq(dev);
                    break;
                case W0_ADDRESS_CONFIG:
                    dev->address_config = (uint16_t) ((dev->address_config & 0xff00) | (val & 0xbf));
                    break;
                case W0_ADDRESS_CONFIG + 1:
                    dev->address_config = (uint16_t) ((dev->address_config & 0x00ff) | (val << 8));
                    break;
                case W0_RESOURCE_CONFIG:
                    dev->resource_config = (uint16_t) ((dev->resource_config & 0xff00) | val);
                    break;
                case W0_RESOURCE_CONFIG + 1:
                    dev->resource_config = (uint16_t) ((dev->resource_config & 0x00ff) | (val << 8));
                    el3_set_irq(dev, el3_irq_of(dev->resource_config));
                    break;
                case W0_EEPROM_COMMAND:
                    el3_eeprom_command(dev, val);
                    break;
                case W0_EEPROM_DATA:
                    dev->eeprom_data = (uint16_t) ((dev->eeprom_data & 0xff00) | val);
                    break;
                case W0_EEPROM_DATA + 1:
                    dev->eeprom_data = (uint16_t) ((dev->eeprom_data & 0x00ff) | (val << 8));
                    break;
                default:
                    break;
            }
            break;

        case 1:
            if (off < 4)
                el3_tx_data_write(dev, val);
            else if (off == W1_TX_STATUS)
                el3_tx_status_pop(dev);
            break;

        case 2:
            if (off < 6)
                dev->station_addr[off] = val;
            break;

        case 3:
            switch (off) {
                case W3_INTERNAL_CONFIG + 2:
                    /* The partition and the activation select; RAM size,
                       width and speed are the board's. */
                    dev->internal_config = (dev->internal_config & 0xfff0ffff) | ((uint32_t) (val & 0x0f) << 16);
                    el3_partition(dev);
                    break;
                case W3_ROM_CONTROL:
                    dev->rom_control = val & 0x03;
                    break;
                default:
                    break;
            }
            break;

        case 4:
            switch (off) {
                case W4_NETWORK_DIAGNOSTIC:
                    /* Testing the low-voltage detector resets the ASIC. */
                    if (val & 0x01)
                        el3_global_reset(dev, 0);
                    break;
                case W4_NETWORK_DIAGNOSTIC + 1:
                    dev->network_diagnostic = (uint16_t) ((dev->network_diagnostic & 0x0fff) | ((val & 0xf0) << 8));
                    break;
                case W4_MEDIA_STATUS:
                    dev->media_status = (uint16_t) ((dev->media_status & ~MEDIA_WRITABLE) | (val & MEDIA_WRITABLE));
                    break;
                default:
                    break;
            }
            break;

        case 6:
            switch (off) {
                case W6_CARRIER_LOST:
                    el3_stat_add(dev, &dev->carrier_lost, val, 0x0f);
                    break;
                case W6_SQE_ERRORS:
                    el3_stat_add(dev, &dev->sqe_errors, val, 0x0f);
                    break;
                case W6_MULTIPLE_COLL:
                    el3_stat_add(dev, &dev->multiple_coll, val, 0x3f);
                    break;
                case W6_SINGLE_COLL:
                    el3_stat_add(dev, &dev->single_coll, val, 0x3f);
                    break;
                case W6_LATE_COLL:
                    el3_stat_add(dev, &dev->late_coll, val, 0xff);
                    break;
                case W6_RX_OVERRUNS:
                    el3_stat_add(dev, &dev->rx_overruns, val, 0xff);
                    break;
                case W6_FRAMES_XMITTED_OK:
                    el3_stat_add(dev, &dev->frames_xmitted_ok, val, 0xff);
                    break;
                case W6_FRAMES_RCVD_OK:
                    el3_stat_add(dev, &dev->frames_rcvd_ok, val, 0xff);
                    break;
                case W6_FRAMES_DEFERRED:
                    el3_stat_add(dev, &dev->frames_deferred, val, 0xff);
                    break;
                case W6_BYTES_RCVD_OK:
                case W6_BYTES_RCVD_OK + 1:
                    dev->bytes_rcvd_ok += (uint16_t) (val << ((off & 1) * 8));
                    el3_stats_indicate(dev);
                    break;
                case W6_BYTES_XMITTED_OK:
                case W6_BYTES_XMITTED_OK + 1:
                    dev->bytes_xmitted_ok += (uint16_t) (val << ((off & 1) * 8));
                    el3_stats_indicate(dev);
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
}

/* ---- the card's I/O -------------------------------------------------------- */

static uint8_t
el3_read(uint16_t port, void *priv)
{
    return el3_reg_read((el3_t *) priv, (uint8_t) (port & 0x0f));
}

static void
el3_write(uint16_t port, uint8_t val, void *priv)
{
    el3_reg_write((el3_t *) priv, (uint8_t) (port & 0x0f), val);
}

static uint16_t
el3_readw(uint16_t port, void *priv)
{
    el3_t  *dev = (el3_t *) priv;
    uint8_t off = port & 0x0f;

    if (off >= REG_COMMAND)
        return el3_status(dev);
    return (uint16_t) (el3_reg_read(dev, off) | (el3_reg_read(dev, (uint8_t) (off + 1)) << 8));
}

static void
el3_writew(uint16_t port, uint16_t val, void *priv)
{
    el3_t  *dev = (el3_t *) priv;
    uint8_t off = port & 0x0f;

    if (off == REG_COMMAND) {
        el3_command(dev, val);
        return;
    }
    el3_reg_write(dev, off, (uint8_t) val);
    el3_reg_write(dev, (uint8_t) (off + 1), (uint8_t) (val >> 8));
}

static uint32_t
el3_readl(uint16_t port, void *priv)
{
    el3_t   *dev = (el3_t *) priv;
    uint8_t  off = port & 0x0f;
    uint32_t ret = 0;

    for (uint8_t i = 0; i < 4; i++)
        ret |= (uint32_t) el3_reg_read(dev, (uint8_t) (off + i)) << (i * 8);
    return ret;
}

static void
el3_writel(uint16_t port, uint32_t val, void *priv)
{
    el3_t  *dev = (el3_t *) priv;
    uint8_t off = port & 0x0f;

    for (uint8_t i = 0; i < 4; i++)
        el3_reg_write(dev, (uint8_t) (off + i), (uint8_t) (val >> (i * 8)));
}

static void
el3_deactivate(el3_t *dev)
{
    if (!dev->active)
        return;
    io_removehandler(dev->io_base, 0x10, el3_read, el3_readw, el3_readl, el3_write, el3_writew, el3_writel, dev);
    dev->active = 0;
    el3_update_irq(dev);
}

static void
el3_activate(el3_t *dev, uint16_t base)
{
    el3_deactivate(dev);
    dev->io_base = base;
    io_sethandler(dev->io_base, 0x10, el3_read, el3_readw, el3_readl, el3_write, el3_writew, el3_writel, dev);
    dev->active = 1;
    el3_log("3C509B: active at %03x, IRQ %i\n", dev->io_base, dev->irq);
    el3_update_irq(dev);
}

/* ---- the ID port ------------------------------------------------------------ */

static void
el3_id_command(el3_t *dev, uint8_t val)
{
    if (val < 0x80) {
        dev->ids_state = IDS_WAIT;
    } else if (val < 0xc0) {
        dev->eeprom_data = dev->eeprom[val & 0x3f];
        dev->contention  = dev->eeprom_data;
    } else if (val < 0xd0) {
        el3_global_reset(dev, 0);
    } else if (val < 0xd8) {
        if ((dev->tag == 0) || (val == 0xd0))
            dev->tag = val & 7;
    } else if (val < 0xe0) {
        if (dev->tag != (val & 7))
            dev->ids_state = IDS_WAIT;
    } else {
        /* Activate: E0h-FEh names the base, FFh keeps the EEPROM's. The
           last value is also EISA mode, which an ISA slot cannot reach. */
        if (val != 0xff)
            dev->address_config = (uint16_t) ((dev->address_config & ~AC_IO_BASE) | (val & AC_IO_BASE));
        if ((dev->address_config & AC_IO_BASE) != AC_EISA)
            el3_activate(dev, (uint16_t) (0x200 + ((dev->address_config & AC_IO_BASE) << 4)));
        dev->ids_state = IDS_WAIT;
    }
}

/* Every write to an 01x0h port is watched. A zero makes that port the ID
   port and restarts the sequence; the 255 byte sequence written to it moves
   the card into the command state, where the next writes are commands. */
static void
el3_id_write(uint16_t port, uint8_t val, void *priv)
{
    el3_t *dev = (el3_t *) priv;

    if (val == 0) {
        dev->id_port   = port;
        dev->ids_state = IDS_WAIT;
        dev->id_next   = 0xff;
        dev->id_count  = 0;
        return;
    }
    if (port != dev->id_port)
        return;

    if (dev->ids_state == IDS_CMD) {
        el3_id_command(dev, val);
        return;
    }

    if (val != dev->id_next) {
        /* A wrong value restarts the sequence; FFh is also its first byte. */
        dev->id_next  = 0xff;
        dev->id_count = 0;
        if (val != 0xff)
            return;
    }
    dev->id_count++;
    dev->id_next = (uint8_t) (val << 1);
    if (val & 0x80)
        dev->id_next ^= 0xcf;
    if (dev->id_count == 255) {
        dev->ids_state = IDS_CMD;
        dev->id_next   = 0xff;
        dev->id_count  = 0;
    }
}

/* Contention: bit 15 of the EEPROM word goes out on data bit 0, open drain,
   and the word shifts left. Contention between two of these cards is not
   modelled; each behaves as if it won. A tagged card stays off the bus. */
static uint8_t
el3_id_read(uint16_t port, void *priv)
{
    el3_t  *dev = (el3_t *) priv;
    uint8_t bit;

    if ((port != dev->id_port) || (dev->ids_state != IDS_CMD) || (dev->tag != 0))
        return 0xff;
    bit             = (uint8_t) (dev->contention >> 15);
    dev->contention = (uint16_t) (dev->contention << 1);
    return (uint8_t) (0xfe | bit);
}

/* ---- the device ------------------------------------------------------------- */

static int
el3_set_link_state(void *priv, uint32_t link_state)
{
    el3_t *dev = (el3_t *) priv;

    dev->link_up = !(link_state & (NET_LINK_DOWN | NET_LINK_TEMP_DOWN));
    return 0;
}

static void
el3_reset(void *priv)
{
    el3_global_reset((el3_t *) priv, 0);
}

static void *
el3_init(const device_t *info)
{
    el3_t   *dev = (el3_t *) calloc(1, sizeof(el3_t));
    uint16_t base;
    uint8_t  irq;
    int      mac;

    dev->board = (uint8_t) device_get_config_int("board");
    base       = (uint16_t) device_get_config_hex16("base");
    irq        = (uint8_t) device_get_config_int("irq");

    dev->mac[0] = 0x00;
    dev->mac[1] = 0x20;
    dev->mac[2] = 0xaf;
    mac         = device_get_config_mac("mac", -1);
    if (mac & 0xff000000) {
        dev->mac[3] = random_generate();
        dev->mac[4] = random_generate();
        dev->mac[5] = random_generate();
        mac         = (dev->mac[3] << 16) | (dev->mac[4] << 8) | dev->mac[5];
        device_set_config_mac("mac", mac);
    } else {
        dev->mac[3] = (mac >> 16) & 0xff;
        dev->mac[4] = (mac >> 8) & 0xff;
        dev->mac[5] = mac & 0xff;
    }

    dev->nvr_stamp[0] = (uint8_t) base;
    dev->nvr_stamp[1] = (uint8_t) (base >> 8);
    dev->nvr_stamp[2] = irq;
    dev->nvr_stamp[3] = dev->board;
    memcpy(&dev->nvr_stamp[4], &dev->mac[3], 3);
    dev->nvr_stamp[7] = 1; /* file format */
    snprintf(dev->nvr_name, sizeof(dev->nvr_name), "eeprom_%s_%d.nvr", info->internal_name, device_get_instance());

    if (!el3_eeprom_restore(dev)) {
        el3_eeprom_build(dev, base, irq);
        el3_eeprom_save(dev);
    }
    el3_global_reset(dev, 0);

    /* The ID port can be any 01x0h port the host picks. */
    for (uint16_t p = 0x100; p < 0x200; p += 0x10)
        io_sethandler(p, 1, el3_id_read, NULL, NULL, el3_id_write, NULL, NULL, dev);

    dev->link_up = 1;
    dev->card    = network_attach(dev, dev->mac, el3_rx, el3_set_link_state);
    if (dev->card->link_state & NET_LINK_DOWN)
        dev->link_up = 0;

    el3_log("3C509B: %s, EEPROM base %03x IRQ %i, %02x:%02x:%02x:%02x:%02x:%02x\n", info->name, base, irq,
            dev->mac[0], dev->mac[1], dev->mac[2], dev->mac[3], dev->mac[4], dev->mac[5]);

    return dev;
}

static void
el3_close(void *priv)
{
    el3_t *dev = (el3_t *) priv;

    if (dev == NULL)
        return;
    el3_deactivate(dev);
    for (uint16_t p = 0x100; p < 0x200; p += 0x10)
        io_removehandler(p, 1, el3_id_read, NULL, NULL, el3_id_write, NULL, NULL, dev);
    if (dev->irq_line)
        picintc(1 << dev->irq);
    netcard_close(dev->card);
    free(dev);
}

static const device_config_t el3_config[] = {
    // clang-format off
    {
        .name           = "board",
        .description    = "Model",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = BOARD_COMBO,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "3C509B-TPO (10BASE-T)",               .value = BOARD_TPO   },
            { .description = "3C509B-TP (10BASE-T, AUI)",           .value = BOARD_TP    },
            { .description = "3C509B-COMBO (10BASE-T, BNC, AUI)",   .value = BOARD_COMBO },
            { .description = "3C509B (BNC, AUI)",                   .value = BOARD_BNC   },
            { .description = ""                                                          }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x300,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x200", .value = 0x200 },
            { .description = "0x210", .value = 0x210 },
            { .description = "0x220", .value = 0x220 },
            { .description = "0x240", .value = 0x240 },
            { .description = "0x250", .value = 0x250 },
            { .description = "0x280", .value = 0x280 },
            { .description = "0x2a0", .value = 0x2a0 },
            { .description = "0x2c0", .value = 0x2c0 },
            { .description = "0x2e0", .value = 0x2e0 },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x310", .value = 0x310 },
            { .description = "0x320", .value = 0x320 },
            { .description = "0x330", .value = 0x330 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x350", .value = 0x350 },
            { .description = "0x360", .value = 0x360 },
            { .description = "0x380", .value = 0x380 },
            { .description = "0x3a0", .value = 0x3a0 },
            { .description = "0x3c0", .value = 0x3c0 },
            { .description = "0x3e0", .value = 0x3e0 },
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
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t threec509b_device = {
    .name          = "3Com EtherLink III ISA (3C509B)",
    .internal_name = "3c509b",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = el3_init,
    .close         = el3_close,
    .reset         = el3_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = el3_config
};
