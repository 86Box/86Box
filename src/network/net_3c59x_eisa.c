/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The 3Com EISA bus master network adapters: the Fast EtherLink
 *          EISA (3C597-TX) and the EtherLink III Bus Master EISA (3C592).
 *
 *          Both are the first generation "Vortex" ASIC, which is the
 *          EtherLink III programming model -- eight sixteen byte register
 *          windows behind one command register -- with a Window 7 of bus
 *          master registers on top. On EISA the card lives entirely in
 *          its slot's I/O: the eight windows are always decoded at
 *          zC80h-zCFFh, one after another, which is where the system
 *          firmware configures it, and once cardEnable is set the
 *          switchable window appears at z000h-z00Fh with Window 1 fixed
 *          at z010h-z01Fh behind it.
 *
 *          Written against the "PCI/EISA Bus-Master Adapter Driver
 *          Technical Reference", 3Com part 09-0681-001B, May 1995.
 *          Register names and bit names below are that book's.
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/eisa.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/random.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_3C59X_EISA_LOG
int tc59x_do_log = ENABLE_3C59X_EISA_LOG;

static void
tc59x_log(const char *fmt, ...)
{
    va_list ap;

    if (tc59x_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define tc59x_log(fmt, ...)
#endif

/* The two boards. */
enum {
    BOARD_3C597 = 0, /* Fast EtherLink EISA TX: 10BASE-T and 100BASE-TX */
    BOARD_3C592 = 1  /* EtherLink III Bus Master EISA: 10BASE-T, 10BASE2, AUI */
};

/* ---- the register map ---------------------------------------------------- */

/* Every window has the command register (write) and IntStatus (read) at
   offset Eh. */
#define REG_COMMAND 0x0e

/* Window 0: configuration. */
#define W0_MANUFACTURER_ID 0x00
#define W0_PRODUCT_ID      0x02
#define W0_CONFIG_CONTROL  0x04
#define W0_ADDRESS_CONFIG  0x06
#define W0_RESOURCE_CONFIG 0x08
#define W0_EEPROM_COMMAND  0x0a
#define W0_EEPROM_DATA     0x0c

/* Window 1: the data path. */
#define W1_DATA      0x00 /* TxData on write, RxData on read */
#define W1_RX_ERROR  0x04
#define W1_RX_STATUS 0x08
#define W1_TIMER     0x0a
#define W1_TX_STATUS 0x0b
#define W1_TX_FREE   0x0c

/* Window 2: addresses. */
#define W2_STATION_ADDRESS 0x00
#define W2_STATION_MASK    0x06

/* Window 3: the FIFO and media configuration. */
#define W3_INTERNAL_CONFIG 0x00
#define W3_OTHER_INT       0x04
#define W3_ROM_CONTROL     0x05
#define W3_MAC_CONTROL     0x06
#define W3_RESET_OPTIONS   0x08
#define W3_RX_FREE         0x0a
#define W3_TX_FREE         0x0c

/* Window 4: diagnostics and media. */
#define W4_VCO_DIAGNOSTIC     0x02
#define W4_FIFO_DIAGNOSTIC    0x04
#define W4_NETWORK_DIAGNOSTIC 0x06
#define W4_PHYSICAL_MGMT      0x08
#define W4_MEDIA_STATUS       0x0a
#define W4_BAD_SSD            0x0c

/* Window 5: the thresholds and enables. */
#define W5_TX_START_THRESH   0x00
#define W5_TX_AVAIL_THRESH   0x02
#define W5_RX_EARLY_THRESH   0x06
#define W5_RX_FILTER         0x08
#define W5_INTERRUPT_ENABLE  0x0a
#define W5_INDICATION_ENABLE 0x0c

/* Window 6: statistics. */
#define W6_CARRIER_LOST      0x00
#define W6_SQE_ERRORS        0x01
#define W6_MULTIPLE_COLL     0x02
#define W6_SINGLE_COLL       0x03
#define W6_LATE_COLL         0x04
#define W6_RX_OVERRUNS       0x05
#define W6_FRAMES_XMITTED_OK 0x06
#define W6_FRAMES_RCVD_OK    0x07
#define W6_FRAMES_DEFERRED   0x08
#define W6_UPPER_FRAMES_OK   0x09
#define W6_BYTES_RCVD_OK     0x0a
#define W6_BYTES_XMITTED_OK  0x0c

/* Window 7: bus master. */
#define W7_MASTER_ADDRESS 0x00
#define W7_RX_ERROR       0x04
#define W7_MASTER_LEN     0x06
#define W7_RX_STATUS      0x08
#define W7_TIMER          0x0a
#define W7_TX_STATUS      0x0b
#define W7_MASTER_STATUS  0x0c

/* Commands: the top five bits of the word written to REG_COMMAND. */
#define CMD_GLOBAL_RESET         0x00
#define CMD_SELECT_WINDOW        0x01
#define CMD_ENABLE_DC_CONVERTER  0x02
#define CMD_RX_DISABLE           0x03
#define CMD_RX_ENABLE            0x04
#define CMD_RX_RESET             0x05
#define CMD_TX_DONE              0x07
#define CMD_RX_DISCARD           0x08
#define CMD_TX_ENABLE            0x09
#define CMD_TX_DISABLE           0x0a
#define CMD_TX_RESET             0x0b
#define CMD_REQUEST_INTERRUPT    0x0c
#define CMD_ACK_INTERRUPT        0x0d
#define CMD_SET_INTERRUPT_EN     0x0e
#define CMD_SET_INDICATION_EN    0x0f
#define CMD_SET_RX_FILTER        0x10
#define CMD_SET_RX_EARLY_THRESH  0x11
#define CMD_SET_TX_AVAIL_THRESH  0x12
#define CMD_SET_TX_START_THRESH  0x13
#define CMD_START_DMA            0x14
#define CMD_STATISTICS_ENABLE    0x15
#define CMD_STATISTICS_DISABLE   0x16
#define CMD_DISABLE_DC_CONVERTER 0x17

/* IntStatus. */
#define INT_LATCH           0x0001
#define INT_HOST_ERROR      0x0002
#define INT_TX_COMPLETE     0x0004
#define INT_TX_AVAILABLE    0x0008
#define INT_RX_COMPLETE     0x0010
#define INT_RX_EARLY        0x0020
#define INT_REQUESTED       0x0040
#define INT_UPDATE_STATS    0x0080
#define INT_TRANSFER        0x0100
#define INT_SOURCES         0x01fe
#define INT_BM_IN_PROGRESS  0x0800
#define INT_CMD_IN_PROGRESS 0x1000

/* TxStatus. */
#define TXS_OVERFLOW       0x04
#define TXS_MAX_COLLISIONS 0x08
#define TXS_UNDERRUN       0x10
#define TXS_JABBER         0x20
#define TXS_INT_REQUESTED  0x40
#define TXS_COMPLETE       0x80

/* RxStatus and RxError. */
#define RXS_ERROR      0x4000
#define RXS_INCOMPLETE 0x8000
#define RXE_OVERRUN    0x01
#define RXE_RUNT       0x02
#define RXE_ALIGNMENT  0x04
#define RXE_CRC        0x08
#define RXE_OVERSIZED  0x10
#define RXE_DRIBBLE    0x80

/* RxFilter. */
#define RXF_INDIVIDUAL 0x01
#define RXF_MULTICAST  0x02
#define RXF_BROADCAST  0x04
#define RXF_ALL        0x08

/* MasterStatus. */
#define MS_MASTER_ABORT    0x0001
#define MS_TARGET_ABORT    0x0002
#define MS_TARGET_RETRY    0x0004
#define MS_TARGET_DISC     0x0008
#define MS_MASTER_DOWNLOAD 0x1000
#define MS_MASTER_UPLOAD   0x4000
#define MS_IN_PROGRESS     0x8000

/* MediaStatus. */
#define MEDIA_DATA_RATE_100    0x0002
#define MEDIA_CRC_STRIP_DIS    0x0004
#define MEDIA_SQE_STATS        0x0008
#define MEDIA_JABBER_GUARD     0x0040
#define MEDIA_LINK_BEAT_ENABLE 0x0080
#define MEDIA_LINK_BEAT_DETECT 0x0800
#define MEDIA_DC_CONVERTER     0x4000
#define MEDIA_AUI_DISABLE      0x8000
#define MEDIA_WRITABLE         (MEDIA_CRC_STRIP_DIS | MEDIA_SQE_STATS | MEDIA_JABBER_GUARD | MEDIA_LINK_BEAT_ENABLE)

/* NetworkDiagnostic. */
#define DIAG_STATS_ENABLED 0x0080
#define DIAG_TX_FATAL      0x0100
#define DIAG_RX_ENABLED    0x0400
#define DIAG_TX_ENABLED    0x0800
#define DIAG_LOOPBACK      0xf000 /* fifo, mac, endec, external */

/* MacControl. */
#define MAC_FULL_DUPLEX   0x0020
#define MAC_LARGE_PACKETS 0x0040

/* ResetOptions and the upper byte of ConfigControl: what is on the board. */
#define OPT_BASE_T4          0x0001
#define OPT_BASE_TX          0x0002
#define OPT_BASE_FX          0x0004
#define OPT_10BASE_T         0x0008
#define OPT_COAX             0x0010
#define OPT_AUI              0x0020
#define OPT_MII              0x0040
#define OPT_TEST_MODE_NORMAL 0xe000

/* InternalConfig. */
#define IC_XCVR_SHIFT   20
#define IC_XCVR_MASK    0x00700000
#define XCVR_10BASE_T   0
#define XCVR_AUI        1
#define XCVR_10BASE_2   3
#define XCVR_100BASE_TX 4
#define XCVR_100BASE_FX 5
#define XCVR_MII        6

/* The frame start header a driver writes ahead of each transmit frame. */
#define FSH_TX_INDICATE 0x8000
#define FSH_CRC_DISABLE 0x2000
#define FSH_LENGTH      0x1fff

/* The thresholds reset to this, which is above any frame and so off. */
#define THRESH_OFF 8188

/* The largest frame the card will take: the book truncates at 1,792 or
   6,144 bytes by allowLargePackets; 86Box's network never hands over more
   than NET_MAX_FRAME, and the FCS the card may be asked to pass through is
   the only thing added to it. */
#define RX_FRAME_MAX (NET_MAX_FRAME + 4)
#define TX_FRAME_MAX (NET_MAX_FRAME + 4)

/* How many complete frames the receive FIFO is allowed to queue. The
   byte accounting against the RAM partition is what really limits it;
   this is the ceiling on the bookkeeping. */
#define RX_QUEUE 32

typedef struct tc59x_rx_frame_t {
    uint8_t  data[RX_FRAME_MAX];
    uint16_t len;
    uint16_t pos;
    uint8_t  error;
} tc59x_rx_frame_t;

typedef struct tc59x_t {
    uint8_t board;
    uint8_t slot;
    uint8_t id[4];

    netcard_t *card;
    uint8_t    mac[6];

    /* Interrupts. */
    uint8_t  irq;       /* from ResourceConfig; 0 is disabled */
    uint8_t  irq_state; /* the PIC's level tracking */
    uint8_t  latch;     /* interruptLatch */
    uint8_t  irq_logged;
    uint16_t int_status; /* the sources, unfiltered */
    uint16_t int_enable;
    uint16_t ind_enable;

    /* Window 0 and the configuration. */
    uint8_t  window;
    uint8_t  card_enable;
    uint16_t address_config;
    uint16_t resource_config;
    uint16_t eeprom_command;
    uint16_t eeprom_data;
    uint16_t eeprom[64];
    uint8_t  eeprom_write_enabled;
    uint8_t  cmd_low; /* a byte write to Eh, waiting for its partner at Fh */

    /* Window 2. */
    uint8_t station_addr[6];
    uint8_t station_mask[6];

    /* Window 3. */
    uint32_t internal_config;
    uint8_t  other_int;
    uint8_t  rom_control;
    uint16_t mac_control;
    uint16_t reset_options;

    /* Window 4. */
    uint16_t network_diagnostic;
    uint16_t physical_mgmt;
    uint16_t media_status;

    /* Window 5. */
    uint16_t tx_start_thresh;
    uint16_t tx_avail_thresh;
    uint16_t rx_early_thresh;
    uint16_t rx_filter;

    /* Window 6. */
    uint8_t  stats_enabled;
    uint8_t  carrier_lost;
    uint8_t  sqe_errors;
    uint8_t  multiple_coll;
    uint8_t  single_coll;
    uint8_t  late_coll;
    uint8_t  rx_overruns;
    uint16_t frames_xmitted_ok;
    uint16_t frames_rcvd_ok;
    uint8_t  frames_deferred;
    uint8_t  upper_frames_ok;
    uint16_t bytes_rcvd_ok;
    uint16_t bytes_xmitted_ok;
    uint8_t  bad_ssd;
    uint8_t  stat_latch_hi; /* the high byte of a 16-bit statistic, held for the second byte read */

    /* Window 7. */
    uint32_t master_address;
    uint16_t master_len;
    uint16_t master_status;

    /* The transmit side: one frame is assembled at a time, then sent. */
    uint8_t  tx_enabled;
    uint32_t tx_size; /* the transmit partition of the packet RAM */
    uint8_t  tx_fsh[4];
    uint8_t  tx_fsh_got;
    uint16_t tx_length; /* txLength from the header */
    uint16_t tx_flags;  /* txIndicate and crcAppendDisable */
    uint8_t  tx_frame[TX_FRAME_MAX + 4];
    uint16_t tx_got;
    uint8_t  tx_pad_left; /* bytes still expected to reach the dword boundary */
    uint8_t  tx_status[8];
    uint8_t  tx_status_count;

    /* The receive side. */
    uint8_t          rx_enabled;
    uint32_t         rx_size; /* the receive partition */
    uint32_t         rx_used; /* bytes of it holding frames */
    tc59x_rx_frame_t rx_queue[RX_QUEUE];
    uint8_t          rx_head;
    uint8_t          rx_count;
    uint8_t          rx_pad_read; /* pad bytes read past the end of the top frame */
} tc59x_t;

/* ---- the serial EEPROM ---------------------------------------------------- */

/* The EISA EEPROM data format, table 5-2, with the board's node address in
   both the 3Com and the OEM slots. The values are the book's defaults for
   a 10/100 board and for a 10 Mbps only one. */
static void
tc59x_eeprom_build(tc59x_t *dev)
{
    uint16_t *e   = dev->eeprom;
    uint8_t   sum = 0;

    memset(e, 0, sizeof(dev->eeprom));

    /* The node address is packed a byte pair to a word, first byte high:
       the book's default of 0020h, AFxxh, xxxxh is 00:20:AF:xx:xx:xx, and
       a driver takes each word as big endian. */
    e[0x00] = (uint16_t) ((dev->mac[0] << 8) | dev->mac[1]);
    e[0x01] = (uint16_t) ((dev->mac[2] << 8) | dev->mac[3]);
    e[0x02] = (uint16_t) ((dev->mac[4] << 8) | dev->mac[5]);
    /* ProductId, byte swapped in the EEPROM so that the register reads as
       the two EISA identifier bytes in slot order: 59h 70h for TCM5970. */
    e[0x03] = (uint16_t) ((dev->id[3] << 8) | dev->id[2]);
    e[0x04] = 0x2ca1; /* 22 May 1995 */
    e[0x05] = 0x0001;
    e[0x06] = 0x4141;
    e[0x07] = 0x6d50; /* ManufacturerId: TCM, compressed and byte swapped */
    e[0x08] = 0x0000; /* AddressConfig: no BIOS ROM */
    e[0x09] = 0x3000; /* ResourceConfig: IRQ 3 */
    e[0x0a] = e[0x00];
    e[0x0b] = e[0x01];
    e[0x0c] = e[0x02];
    e[0x0d] = 0x3f10; /* Software Information: DOS client, 500 us, link beat on */
    e[0x0e] = 0x0000; /* Compatibility Word */
    /* Software Information 2: bit 6 says the receive pacing anomaly of
       the first EISA ASIC is fixed, which spares the driver its
       workarounds; there is no such anomaly here. */
    e[0x0f] = 0x0040;
    if (dev->board == BOARD_3C597) {
        e[0x10] = 0x11c6; /* Capabilities Word, with supports100Mbps */
        e[0x12] = 0x001b; /* InternalConfig low: 64 KB word wide RAM, 1 clock, 8 KB ROM */
        e[0x13] = 0x0101; /* InternalConfig high: 3:1 partition, autoSelect */
    } else {
        e[0x10] = 0x01c6;
        e[0x12] = 0x0012; /* 32 KB byte wide RAM */
        e[0x13] = 0x0102; /* 1:1 partition, autoSelect */
    }

    /* "The checksum is a byte-wise XOR computed across component bytes
       in words 0 through 16." */
    for (uint8_t i = 0; i < 0x17; i++)
        sum ^= (uint8_t) ((e[i] & 0xff) ^ (e[i] >> 8));
    e[0x17] = sum;
}

/* What the hardware loads from the EEPROM at reset: AddressConfig,
   ResourceConfig, ProductId and InternalConfig (table on page 3-8). */
static void
tc59x_eeprom_load(tc59x_t *dev)
{
    dev->address_config  = dev->eeprom[0x08];
    dev->resource_config = dev->eeprom[0x09];
    dev->internal_config = ((uint32_t) dev->eeprom[0x13] << 16) | dev->eeprom[0x12];
}

/* ---- the RAM partition ---------------------------------------------------- */

static void
tc59x_partition(tc59x_t *dev)
{
    static const uint32_t ram_size[8] = { 8192, 0, 32768, 65536, 131072, 0, 0, 0 };
    uint32_t              total       = ram_size[dev->internal_config & 7];
    uint8_t               part        = (dev->internal_config >> 16) & 3;

    if (total == 0)
        total = 65536;
    switch (part) {
        case 0: /* 5:3 */
            dev->rx_size = total * 5 / 8;
            break;
        case 1: /* 3:1 */
            dev->rx_size = total * 3 / 4;
            break;
        default: /* 1:1 */
            dev->rx_size = total / 2;
            break;
    }
    dev->tx_size = total - dev->rx_size;
}

/* ---- interrupts ------------------------------------------------------------- */

static uint8_t
tc59x_irq_of(uint16_t resource_config)
{
    switch (resource_config >> 12) {
        case 0x3:
            return 3;
        case 0x5:
            return 5;
        case 0x7:
            return 7;
        case 0x9:
            return 9;
        case 0xa:
            return 10;
        case 0xb:
            return 11;
        case 0xc:
            return 12;
        case 0xf:
            return 15;
        default:
            return 0;
    }
}

/* interruptLatch is "a logical OR of the interrupt-causing bits after they
   have been filtered through the InterruptEnable register", latched, and
   let go by AcknowledgeInterrupt. The sources themselves are acknowledged
   one by one -- RxDiscard, a TxStatus write, and so on -- so the latch is
   re-set the moment any of them is still standing after the acknowledge,
   which is what keeps a driver that acknowledges before it has drained
   everything from losing the rest. */
static uint16_t tc59x_int_status(const tc59x_t *dev);

static void
tc59x_update_irq(tc59x_t *dev)
{
    uint16_t live = dev->int_status & dev->ind_enable & dev->int_enable & INT_SOURCES;

    if (live)
        dev->latch = 1;

    if (dev->irq == 0)
        return;
    /* EISA interrupts from this card are level triggered: the
       configuration file offers TRIGGER = LEVEL for every choice. */
    if (dev->latch)
        picintlevel(1 << dev->irq, &dev->irq_state);
    else
        picintclevel(1 << dev->irq, &dev->irq_state);
    if (dev->latch != dev->irq_logged) {
        dev->irq_logged = dev->latch;
        tc59x_log("3C59x: IRQ %i %s (status %04x)\n", dev->irq, dev->latch ? "up" : "down", tc59x_int_status(dev));
    }
}

static void
tc59x_raise(tc59x_t *dev, uint16_t bits)
{
    dev->int_status |= bits;
    tc59x_update_irq(dev);
}

static void
tc59x_lower(tc59x_t *dev, uint16_t bits)
{
    dev->int_status &= (uint16_t) ~bits;
    tc59x_update_irq(dev);
}

static void
tc59x_set_irq_line(tc59x_t *dev, uint8_t irq)
{
    if (irq == dev->irq)
        return;
    if (dev->irq != 0)
        picintclevel(1 << dev->irq, &dev->irq_state);
    dev->irq = irq;
    tc59x_update_irq(dev);
}

/* ---- statistics ------------------------------------------------------------- */

/* Each counter raises updateStats as it passes the half-way mark. */
#define STAT_BUMP8(dev, field, half)                \
    do {                                            \
        if ((dev)->stats_enabled) {                 \
            (dev)->field++;                         \
            if ((dev)->field == (half))             \
                tc59x_raise(dev, INT_UPDATE_STATS); \
        }                                           \
    } while (0)

static void
tc59x_stat_bytes(tc59x_t *dev, uint16_t *counter, uint16_t n)
{
    uint16_t before = *counter;

    if (!dev->stats_enabled)
        return;
    *counter = (uint16_t) (before + n);
    if ((before < 0x8000) && (*counter >= 0x8000))
        tc59x_raise(dev, INT_UPDATE_STATS);
}

static void
tc59x_stat_frames(tc59x_t *dev, uint16_t *counter)
{
    if (!dev->stats_enabled)
        return;
    *counter = (uint16_t) ((*counter + 1) & 0x3ff);
    if (*counter == 0x200)
        tc59x_raise(dev, INT_UPDATE_STATS);
}

/* ---- CRC -------------------------------------------------------------------- */

/* 86Box frames carry no frame check sequence. The card strips it on
   receive and appends it on transmit, so most of the time neither side
   sees one; when crcStripDisable or crcAppendDisable asks for it, this
   is the one that goes on the wire. */
static uint32_t
tc59x_crc32(const uint8_t *data, uint16_t len)
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

static tc59x_rx_frame_t *
tc59x_rx_top(tc59x_t *dev)
{
    if (dev->rx_count == 0)
        return NULL;
    return &dev->rx_queue[dev->rx_head];
}

static void
tc59x_rx_indicate(tc59x_t *dev)
{
    if (dev->rx_count > 0)
        tc59x_raise(dev, INT_RX_COMPLETE);
    else
        tc59x_lower(dev, INT_RX_COMPLETE);
}

/* RxDiscard: the top frame goes, whether or not it was read out, and the
   next one is the top. */
static void
tc59x_rx_discard(tc59x_t *dev)
{
    tc59x_rx_frame_t *f = tc59x_rx_top(dev);

    if (f == NULL)
        return;
    dev->rx_used -= f->len;
    dev->rx_head = (uint8_t) ((dev->rx_head + 1) % RX_QUEUE);
    dev->rx_count--;
    dev->rx_pad_read = 0;
    tc59x_rx_indicate(dev);
}

static void
tc59x_rx_flush(tc59x_t *dev)
{
    dev->rx_head     = 0;
    dev->rx_count    = 0;
    dev->rx_used     = 0;
    dev->rx_pad_read = 0;
    tc59x_lower(dev, INT_RX_COMPLETE | INT_RX_EARLY);
}

static int
tc59x_rx_accept(const tc59x_t *dev, const uint8_t *dst)
{
    if (dev->rx_filter & RXF_ALL)
        return 1;
    if ((dst[0] & dst[1] & dst[2] & dst[3] & dst[4] & dst[5]) == 0xff)
        return (dev->rx_filter & (RXF_BROADCAST | RXF_MULTICAST)) != 0;
    if (dst[0] & 0x01)
        return (dev->rx_filter & RXF_MULTICAST) != 0;
    if (dev->rx_filter & RXF_INDIVIDUAL) {
        for (uint8_t i = 0; i < 6; i++) {
            if ((dst[i] ^ dev->station_addr[i]) & ~dev->station_mask[i])
                return 0;
        }
        return 1;
    }
    return 0;
}

/* A frame off the network, or one of the card's own looped back. The
   return value is the queue's: one for a frame taken, whether it was kept
   or thrown away, and zero only for one the card could not take yet,
   which is held at the head of the queue and offered again. Answering
   zero for a frame the filter rejects would park it there for ever, in
   front of everything that follows. */
static int
tc59x_rx(void *priv, uint8_t *buf, int io_len)
{
    tc59x_t          *dev = (tc59x_t *) priv;
    tc59x_rx_frame_t *f;
    uint16_t          len = (uint16_t) io_len;
    uint32_t          crc;

    if (!dev->rx_enabled || (io_len < 14)) {
        tc59x_log("3C59x: receiver off, %i byte frame dropped\n", io_len);
        return 1;
    }
    if (!tc59x_rx_accept(dev, buf)) {
        tc59x_log("3C59x: filtered (%02x) %i bytes to %02x:%02x:%02x:%02x:%02x:%02x\n", dev->rx_filter, io_len,
                  buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
        return 1;
    }

    if (len > NET_MAX_FRAME)
        len = NET_MAX_FRAME;

    /* The FIFO is a partition of the packet RAM; a frame that does not
       fit is the overrun the book describes: "frames received while this
       bit is set are discarded". */
    if ((dev->rx_count >= RX_QUEUE) || (dev->rx_used + len + 4 > dev->rx_size)) {
        STAT_BUMP8(dev, rx_overruns, 0x80);
        tc59x_log("3C59x: receive FIFO full, %u byte frame dropped\n", len);
        return 0;
    }

    f = &dev->rx_queue[(dev->rx_head + dev->rx_count) % RX_QUEUE];
    memcpy(f->data, buf, len);
    f->len   = len;
    f->pos   = 0;
    f->error = 0;
    if (dev->media_status & MEDIA_CRC_STRIP_DIS) {
        crc              = tc59x_crc32(buf, len);
        f->data[len]     = (uint8_t) crc;
        f->data[len + 1] = (uint8_t) (crc >> 8);
        f->data[len + 2] = (uint8_t) (crc >> 16);
        f->data[len + 3] = (uint8_t) (crc >> 24);
        f->len += 4;
    }
    if (f->len < 60)
        f->error |= RXE_RUNT;
    dev->rx_used += f->len;
    dev->rx_count++;

    tc59x_stat_frames(dev, &dev->frames_rcvd_ok);
    tc59x_stat_bytes(dev, &dev->bytes_rcvd_ok, f->len);

    tc59x_log("3C59x: received %u bytes, %u queued, intstatus %04x enables %04x/%04x\n", f->len, dev->rx_count,
              tc59x_int_status(dev), dev->int_enable, dev->ind_enable);
    tc59x_rx_indicate(dev);
    return 1;
}

/* One byte out of RxData. Reading past the end of the top frame is
   allowed within its last dword -- "an entire frame to be read with 32-bit
   I/O cycles" -- and undefined beyond it; a host that does it anyway has
   read the receive FIFO empty, which is rxUnderrun and a hostError. */
static uint8_t
tc59x_rx_data_read(tc59x_t *dev)
{
    tc59x_rx_frame_t *f = tc59x_rx_top(dev);

    if (f == NULL) {
        tc59x_raise(dev, INT_HOST_ERROR);
        return 0;
    }
    if (f->pos < f->len)
        return f->data[f->pos++];
    if (dev->rx_pad_read < ((4 - (f->len & 3)) & 3)) {
        dev->rx_pad_read++;
        return 0;
    }
    tc59x_raise(dev, INT_HOST_ERROR);
    return 0;
}

static uint16_t
tc59x_rx_status(const tc59x_t *dev)
{
    const tc59x_rx_frame_t *f;

    if (dev->rx_count == 0)
        return 0;
    f = &dev->rx_queue[dev->rx_head];
    return (uint16_t) (((f->len - f->pos) & 0x1fff) | (f->error ? RXS_ERROR : 0));
}

/* ---- transmit -------------------------------------------------------------- */

static void
tc59x_tx_status_push(tc59x_t *dev, uint8_t status)
{
    if (dev->tx_status_count >= sizeof(dev->tx_status)) {
        /* "the TxStatus stack is full and as a result the transmitter
           has been disabled." */
        dev->tx_status[sizeof(dev->tx_status) - 1] |= TXS_OVERFLOW;
        dev->tx_enabled = 0;
        dev->network_diagnostic &= (uint16_t) ~DIAG_TX_ENABLED;
        tc59x_raise(dev, INT_TX_COMPLETE);
        return;
    }
    dev->tx_status[dev->tx_status_count++] = status;
    tc59x_raise(dev, INT_TX_COMPLETE);
}

static void
tc59x_tx_status_pop(tc59x_t *dev)
{
    if (dev->tx_status_count == 0)
        return;
    memmove(dev->tx_status, dev->tx_status + 1, dev->tx_status_count - 1);
    dev->tx_status_count--;
    if (dev->tx_status_count == 0)
        tc59x_lower(dev, INT_TX_COMPLETE);
}

static uint16_t
tc59x_tx_free(const tc59x_t *dev)
{
    uint32_t used = 4 + dev->tx_got;
    uint32_t free;

    if (dev->tx_fsh_got == 0)
        used = 0;
    free = (used > dev->tx_size) ? 0 : (dev->tx_size - used);
    if (free > 0xffff)
        free = 0xffff;
    return (uint16_t) (free & ~3U);
}

static void
tc59x_tx_avail_check(tc59x_t *dev)
{
    if ((dev->tx_avail_thresh < THRESH_OFF) && (tc59x_tx_free(dev) > dev->tx_avail_thresh))
        tc59x_raise(dev, INT_TX_AVAILABLE);
}

static void
tc59x_tx_frame_reset(tc59x_t *dev)
{
    dev->tx_fsh_got  = 0;
    dev->tx_got      = 0;
    dev->tx_length   = 0;
    dev->tx_flags    = 0;
    dev->tx_pad_left = 0;
}

/* The frame is complete in the FIFO: on to the wire, or back to the
   receiver if a loopback mode is set. */
static void
tc59x_tx_send(tc59x_t *dev)
{
    uint16_t len = dev->tx_got;
    uint8_t  status;

    if (dev->tx_flags & FSH_CRC_DISABLE) {
        /* The driver supplied the FCS; the wire here does not carry one. */
        if (len >= 4)
            len -= 4;
    }
    /* "The adapter automatically appends the appropriate number of
       arbitrary data bytes to the end of the frame's data field to pad the
       frame to 60 bytes in length." */
    while (len < 60)
        dev->tx_frame[len++] = 0;

    tc59x_log("3C59x: transmit %u bytes%s\n", len, (dev->network_diagnostic & DIAG_LOOPBACK) ? " (loopback)" : "");

    if (dev->tx_enabled) {
        if (dev->network_diagnostic & DIAG_LOOPBACK)
            tc59x_rx(dev, dev->tx_frame, len);
        else
            network_tx(dev->card, dev->tx_frame, len);
    }

    tc59x_stat_frames(dev, &dev->frames_xmitted_ok);
    tc59x_stat_bytes(dev, &dev->bytes_xmitted_ok, len);

    /* "Status is not posted unless either an error occurred during frame
       transmission or the txIndicate bit in the frame start header was
       set." */
    status = TXS_COMPLETE;
    if (dev->tx_flags & FSH_TX_INDICATE) {
        status |= TXS_INT_REQUESTED;
        tc59x_tx_status_push(dev, status);
    }

    tc59x_tx_frame_reset(dev);
    tc59x_tx_avail_check(dev);
}

/* One byte into the transmit FIFO, from TxData or a bus master download.
   The first four bytes of every frame are its frame start header. */
static void
tc59x_tx_data_write(tc59x_t *dev, uint8_t val)
{
    if (dev->tx_pad_left) {
        /* Padding out the previous frame to its dword boundary. */
        dev->tx_pad_left--;
        tc59x_log("3C59x: pad byte %02x swallowed\n", val);
        return;
    }
    if (dev->tx_fsh_got < 4) {
        dev->tx_fsh[dev->tx_fsh_got++] = val;
        if (dev->tx_fsh_got == 4) {
            uint16_t fsh   = (uint16_t) (dev->tx_fsh[0] | (dev->tx_fsh[1] << 8));
            dev->tx_length = fsh & FSH_LENGTH;
            dev->tx_flags  = fsh & (FSH_TX_INDICATE | FSH_CRC_DISABLE);
            dev->tx_got    = 0;
            tc59x_log("3C59x: fsh %02x %02x %02x %02x: length %u flags %04x\n", dev->tx_fsh[0], dev->tx_fsh[1],
                      dev->tx_fsh[2], dev->tx_fsh[3], dev->tx_length, dev->tx_flags);
            if (dev->tx_length > TX_FRAME_MAX) {
                tc59x_log("3C59x: frame start header asks for %u bytes\n", dev->tx_length);
                dev->tx_length = TX_FRAME_MAX;
            }
            if (dev->tx_length == 0)
                tc59x_tx_frame_reset(dev);
        }
        return;
    }
    if (dev->tx_got >= dev->tx_size) {
        /* "Writing bytes to the transmit FIFO when there is no space
           causes a transmit overrun condition". */
        tc59x_raise(dev, INT_HOST_ERROR);
        return;
    }
    dev->tx_frame[dev->tx_got++] = val;
    if (dev->tx_got == dev->tx_length) {
        /* Complete. Whatever the driver writes to reach the dword
           boundary is pad and is swallowed; TxDone would end it early.
           The pad count is set after the send, which starts the next
           frame's state afresh. */
        uint8_t pad = (uint8_t) ((4 - (dev->tx_length & 3)) & 3);

        tc59x_tx_send(dev);
        dev->tx_pad_left = pad;
    }
}

/* TxDone: "the data which has been downloaded to the transmit FIFO is a
   complete frame". With the byte count matching the header the frame has
   already gone; this just cancels the pad the card was expecting. */
static void
tc59x_tx_done(tc59x_t *dev)
{
    tc59x_log("3C59x: TxDone: fsh %u/4, %u/%u bytes, pad left %u\n", dev->tx_fsh_got, dev->tx_got, dev->tx_length, dev->tx_pad_left);
    dev->tx_pad_left = 0;
    if ((dev->tx_fsh_got == 4) && (dev->tx_got > 0) && (dev->tx_got < dev->tx_length)) {
        /* A short frame against its own header: send what there is. */
        dev->tx_length = dev->tx_got;
        tc59x_tx_send(dev);
    }
}

static void
tc59x_tx_flush(tc59x_t *dev)
{
    tc59x_tx_frame_reset(dev);
    dev->tx_status_count = 0;
    tc59x_lower(dev, INT_TX_COMPLETE | INT_TX_AVAILABLE);
}

/* ---- bus master ----------------------------------------------------------- */

/* Both directions complete at once. The book allows for any length and
   any alignment; the EISA errata ask drivers to start on 32 byte
   boundaries, which is their business. */
static void
tc59x_dma_upload(tc59x_t *dev)
{
    tc59x_rx_frame_t *f = tc59x_rx_top(dev);
    uint8_t           buf[64];
    uint16_t          n;

    while (dev->master_len && (f != NULL) && (f->pos < f->len)) {
        n = (uint16_t) (f->len - f->pos);
        if (n > dev->master_len)
            n = dev->master_len;
        if (n > sizeof(buf))
            n = sizeof(buf);
        memcpy(buf, f->data + f->pos, n);
        dma_bm_write(dev->master_address, buf, n, 4);
        f->pos += n;
        dev->master_address += n;
        dev->master_len -= n;
    }
    /* "Set when an upload is complete, either because the terminal count
       was reached or because the end of the receive frame was reached." */
    dev->master_status |= MS_MASTER_UPLOAD;
    tc59x_raise(dev, INT_TRANSFER);
}

static void
tc59x_dma_download(tc59x_t *dev)
{
    uint8_t  buf[64];
    uint16_t n;

    tc59x_log("3C59x: download %u bytes from %08x (fsh %u/4, %u/%u so far, pad left %u)\n", dev->master_len,
              dev->master_address, dev->tx_fsh_got, dev->tx_got, dev->tx_length, dev->tx_pad_left);

    while (dev->master_len) {
        n = dev->master_len;
        if (n > sizeof(buf))
            n = sizeof(buf);
        dma_bm_read(dev->master_address, buf, n, 4);
        for (uint16_t i = 0; i < n; i++)
            tc59x_tx_data_write(dev, buf[i]);
        dev->master_address += n;
        dev->master_len -= n;
    }
    dev->master_status |= MS_MASTER_DOWNLOAD;
    tc59x_raise(dev, INT_TRANSFER);
}

static void
tc59x_dma_reset(tc59x_t *dev)
{
    dev->master_address = 0;
    dev->master_len     = 0;
    dev->master_status  = 0;
    tc59x_lower(dev, INT_TRANSFER);
}

/* ---- reset ---------------------------------------------------------------- */

static void
tc59x_rx_reset(tc59x_t *dev, uint8_t mask)
{
    if (!(mask & 0x04)) { /* networkRxReset */
        dev->rx_enabled = 0;
        dev->rx_filter  = 0;
        dev->network_diagnostic &= (uint16_t) ~DIAG_RX_ENABLED;
    }
    if (!(mask & 0x08)) { /* fifoRxReset */
        tc59x_rx_flush(dev);
        dev->rx_early_thresh = THRESH_OFF;
    }
    if (!(mask & 0x40)) /* dmaRxReset */
        tc59x_dma_reset(dev);
    dev->int_status &= (uint16_t) ~INT_HOST_ERROR;
    tc59x_update_irq(dev);
}

static void
tc59x_tx_reset(tc59x_t *dev, uint8_t mask)
{
    if (!(mask & 0x04)) { /* networkTxReset */
        dev->tx_enabled = 0;
        dev->network_diagnostic &= (uint16_t) ~(DIAG_TX_ENABLED | DIAG_TX_FATAL);
        dev->tx_status_count = 0;
        tc59x_lower(dev, INT_TX_COMPLETE);
    }
    if (!(mask & 0x08)) { /* fifoTxReset */
        tc59x_tx_flush(dev);
        dev->tx_start_thresh = THRESH_OFF;
        dev->tx_avail_thresh = THRESH_OFF;
    }
    if (!(mask & 0x40)) /* dmaTxReset */
        tc59x_dma_reset(dev);
    dev->int_status &= (uint16_t) ~INT_HOST_ERROR;
    tc59x_update_irq(dev);
}

/* GlobalReset with its mask, and what a system reset does with the mask at
   zero and the configuration cleared as well. */
static void
tc59x_global_reset(tc59x_t *dev, uint8_t mask)
{
    tc59x_rx_reset(dev, mask);
    tc59x_tx_reset(dev, mask);

    if (!(mask & 0x20)) { /* hostReset */
        dev->int_status = 0;
        dev->int_enable = 0;
        dev->ind_enable = 0;
        dev->latch      = 0;
        dev->window     = 0;
        tc59x_update_irq(dev);
    }
    if (!(mask & 0x10)) /* aismReset: the EEPROM is reloaded */
        tc59x_eeprom_load(dev);

    dev->mac_control        = 0;
    dev->media_status       = 0;
    dev->network_diagnostic = 0;
    dev->physical_mgmt      = 0;
    dev->stats_enabled      = 0;
    dev->eeprom_command     = 0;
    dev->eeprom_data        = 0;
    dev->rom_control        = 0;
    dev->other_int          = 0;
    dev->cmd_low            = 0;
    memset(dev->station_addr, 0, sizeof(dev->station_addr));
    memset(dev->station_mask, 0, sizeof(dev->station_mask));
    tc59x_partition(dev);
}

static void
tc59x_reset(void *priv)
{
    tc59x_t *dev = (tc59x_t *) priv;

    tc59x_global_reset(dev, 0);
    dev->card_enable = 0;
    tc59x_set_irq_line(dev, tc59x_irq_of(dev->resource_config));
}

/* ---- the EEPROM commands -------------------------------------------------- */

static void
tc59x_eeprom_command(tc59x_t *dev, uint16_t val)
{
    uint8_t opcode  = (val >> 6) & 3;
    uint8_t address = val & 0x3f;

    dev->eeprom_command = val & 0x00ff;
    switch (opcode) {
        case 0: /* the subcommands, by bits 5 and 4 */
            switch ((address >> 4) & 3) {
                case 0: /* WriteDisable */
                    dev->eeprom_write_enabled = 0;
                    break;
                case 1: /* WriteAll */
                    if (dev->eeprom_write_enabled) {
                        for (uint8_t i = 0; i < 64; i++)
                            dev->eeprom[i] = dev->eeprom_data;
                    }
                    dev->eeprom_write_enabled = 0;
                    break;
                case 2: /* EraseAll */
                    if (dev->eeprom_write_enabled)
                        memset(dev->eeprom, 0xff, sizeof(dev->eeprom));
                    dev->eeprom_write_enabled = 0;
                    break;
                default: /* WriteEnable */
                    dev->eeprom_write_enabled = 1;
                    break;
            }
            break;
        case 1: /* WriteRegister */
            if (dev->eeprom_write_enabled)
                dev->eeprom[address] &= dev->eeprom_data; /* it can only clear bits */
            dev->eeprom_write_enabled = 0;
            break;
        case 2: /* ReadRegister */
            dev->eeprom_data = dev->eeprom[address];
            break;
        default: /* EraseRegister */
            if (dev->eeprom_write_enabled)
                dev->eeprom[address] = 0xffff;
            dev->eeprom_write_enabled = 0;
            break;
    }
    /* The 162 us to 11 ms the part takes are not modelled: eepromBusy is
       never seen set, and every driver polls it before going on. */
}

/* ---- commands --------------------------------------------------------------- */

static void
tc59x_command(tc59x_t *dev, uint16_t val)
{
    uint8_t  cmd   = (uint8_t) (val >> 11);
    uint16_t param = val & 0x07ff;

    switch (cmd) {
        case CMD_GLOBAL_RESET:
            tc59x_log("3C59x: GlobalReset %02x\n", param & 0xff);
            tc59x_global_reset(dev, (uint8_t) param);
            break;
        case CMD_SELECT_WINDOW:
            dev->window = param & 7;
            break;
        case CMD_ENABLE_DC_CONVERTER:
            dev->media_status |= MEDIA_DC_CONVERTER;
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
            tc59x_rx_reset(dev, (uint8_t) param);
            break;
        case CMD_TX_DONE:
            tc59x_tx_done(dev);
            break;
        case CMD_RX_DISCARD:
            tc59x_rx_discard(dev);
            break;
        case CMD_TX_ENABLE:
            dev->tx_enabled = 1;
            dev->network_diagnostic |= DIAG_TX_ENABLED;
            break;
        case CMD_TX_DISABLE:
            dev->tx_enabled = 0;
            dev->network_diagnostic &= (uint16_t) ~DIAG_TX_ENABLED;
            break;
        case CMD_TX_RESET:
            tc59x_tx_reset(dev, (uint8_t) param);
            break;
        case CMD_REQUEST_INTERRUPT:
            tc59x_raise(dev, INT_REQUESTED);
            break;
        case CMD_ACK_INTERRUPT:
            if (param & INT_LATCH)
                dev->latch = 0;
            if (param & INT_TX_AVAILABLE) {
                /* "the process of acknowledging the interrupt will change
                   the value in TxAvailableThresh to 8188d". */
                dev->tx_avail_thresh = THRESH_OFF;
            }
            dev->int_status &= (uint16_t) ~(param & (INT_TX_AVAILABLE | INT_RX_EARLY | INT_REQUESTED));
            tc59x_update_irq(dev);
            break;
        case CMD_SET_INTERRUPT_EN:
            dev->int_enable = param & INT_SOURCES;
            tc59x_update_irq(dev);
            break;
        case CMD_SET_INDICATION_EN:
            dev->ind_enable = param & INT_SOURCES;
            tc59x_update_irq(dev);
            break;
        case CMD_SET_RX_FILTER:
            dev->rx_filter = param & 0x0f;
            break;
        case CMD_SET_RX_EARLY_THRESH:
            /* "The parameter is written into bits [12:2]" of every
               threshold: the eleven bit parameter scaled by four. */
            dev->rx_early_thresh = (uint16_t) (param << 2);
            break;
        case CMD_SET_TX_AVAIL_THRESH:
            dev->tx_avail_thresh = (uint16_t) (param << 2);
            tc59x_tx_avail_check(dev);
            break;
        case CMD_SET_TX_START_THRESH:
            dev->tx_start_thresh = (uint16_t) (param << 2);
            break;
        case CMD_START_DMA:
            if (param & 1)
                tc59x_dma_download(dev);
            else
                tc59x_dma_upload(dev);
            break;
        case CMD_STATISTICS_ENABLE:
            dev->stats_enabled = 1;
            dev->network_diagnostic |= DIAG_STATS_ENABLED;
            break;
        case CMD_STATISTICS_DISABLE:
            dev->stats_enabled = 0;
            dev->network_diagnostic &= (uint16_t) ~DIAG_STATS_ENABLED;
            break;
        case CMD_DISABLE_DC_CONVERTER:
            dev->media_status &= (uint16_t) ~MEDIA_DC_CONVERTER;
            break;
        default:
            tc59x_log("3C59x: reserved command %02x\n", cmd);
            break;
    }
}

static uint16_t
tc59x_int_status(const tc59x_t *dev)
{
    uint16_t ret = (uint16_t) ((dev->window << 13) | (dev->int_status & dev->ind_enable & INT_SOURCES));

    if (dev->latch)
        ret |= INT_LATCH;
    return ret;
}

/* ---- the register file ----------------------------------------------------- */

/* A 16-bit statistic is read low byte first, and the read clears it; the
   high byte is held for the read that follows. */
static uint8_t
tc59x_stat16_read(tc59x_t *dev, uint16_t *counter, uint8_t high)
{
    uint8_t ret;

    if (high)
        return dev->stat_latch_hi;
    ret                = (uint8_t) (*counter & 0xff);
    dev->stat_latch_hi = (uint8_t) (*counter >> 8);
    *counter           = 0;
    tc59x_lower(dev, INT_UPDATE_STATS);
    return ret;
}

static uint8_t
tc59x_stat8_read(tc59x_t *dev, uint8_t *counter)
{
    uint8_t ret = *counter;

    *counter = 0;
    tc59x_lower(dev, INT_UPDATE_STATS);
    return ret;
}

static uint16_t
tc59x_media_status(const tc59x_t *dev)
{
    uint16_t ret  = dev->media_status & MEDIA_WRITABLE;
    uint8_t  xcvr = (uint8_t) ((dev->internal_config & IC_XCVR_MASK) >> IC_XCVR_SHIFT);

    ret |= dev->media_status & MEDIA_DC_CONVERTER;
    /* "For all speeds, linkBeatDetect is forced on whenever
       linkBeatEnable is cleared" -- and the cable here is always in. */
    ret |= MEDIA_LINK_BEAT_DETECT;
    if ((xcvr == XCVR_100BASE_TX) || (xcvr == XCVR_100BASE_FX))
        ret |= MEDIA_DATA_RATE_100;
    if ((xcvr == XCVR_10BASE_T) || (xcvr == XCVR_10BASE_2))
        ret |= MEDIA_AUI_DISABLE;
    return ret;
}

static uint8_t
tc59x_reg_read(tc59x_t *dev, uint8_t window, uint8_t off)
{
    uint16_t w;
    uint32_t l;

    /* IntStatus sits at Eh in every window. */
    if (off >= REG_COMMAND)
        return (uint8_t) (tc59x_int_status(dev) >> ((off & 1) * 8));

    switch (window) {
        case 0:
            switch (off) {
                case W0_MANUFACTURER_ID:
                case W0_MANUFACTURER_ID + 1:
                case W0_PRODUCT_ID:
                case W0_PRODUCT_ID + 1:
                    /* The same four bytes the slot answers with at zC80h. */
                    return dev->id[off & 3];
                case W0_CONFIG_CONTROL:
                    return dev->card_enable;
                case W0_CONFIG_CONTROL + 1:
                    return (uint8_t) (dev->reset_options & 0x7f);
                case W0_ADDRESS_CONFIG:
                case W0_ADDRESS_CONFIG + 1:
                    /* xcvrSelect [15:13] is a copy of InternalConfig's. */
                    w = (uint16_t) ((dev->address_config & 0x0f00) | (((dev->internal_config & IC_XCVR_MASK) >> IC_XCVR_SHIFT) << 13));
                    return (uint8_t) (w >> ((off & 1) * 8));
                case W0_RESOURCE_CONFIG:
                case W0_RESOURCE_CONFIG + 1:
                    return (uint8_t) (dev->resource_config >> ((off & 1) * 8));
                case W0_EEPROM_COMMAND:
                case W0_EEPROM_COMMAND + 1:
                    return (uint8_t) (dev->eeprom_command >> ((off & 1) * 8));
                case W0_EEPROM_DATA:
                case W0_EEPROM_DATA + 1:
                    return (uint8_t) (dev->eeprom_data >> ((off & 1) * 8));
                default:
                    break;
            }
            break;

        case 1:
        case 7:
            switch (off) {
                case W1_DATA:
                case W1_DATA + 1:
                case W1_DATA + 2:
                case W1_DATA + 3:
                    if (window == 1)
                        return tc59x_rx_data_read(dev);
                    /* MasterAddress: the running count of the last transfer. */
                    return (uint8_t) (dev->master_address >> ((off & 3) * 8));
                case W1_RX_ERROR:
                    return (dev->rx_count > 0) ? dev->rx_queue[dev->rx_head].error : 0;
                case W7_MASTER_LEN:
                case W7_MASTER_LEN + 1:
                    if (window == 7)
                        return (uint8_t) (dev->master_len >> ((off & 1) * 8));
                    break;
                case W1_RX_STATUS:
                case W1_RX_STATUS + 1:
                    return (uint8_t) (tc59x_rx_status(dev) >> ((off & 1) * 8));
                case W1_TIMER:
                    /* Interrupt latency here is nil. */
                    return 0;
                case W1_TX_STATUS:
                    return dev->tx_status_count ? dev->tx_status[0] : 0;
                case W1_TX_FREE:
                case W1_TX_FREE + 1:
                    if (window == 1)
                        return (uint8_t) (tc59x_tx_free(dev) >> ((off & 1) * 8));
                    return (uint8_t) (dev->master_status >> ((off & 1) * 8));
                default:
                    break;
            }
            break;

        case 2:
            if (off < 6)
                return dev->station_addr[off];
            if (off < 12)
                return dev->station_mask[off - 6];
            break;

        case 3:
            switch (off) {
                case W3_INTERNAL_CONFIG:
                case W3_INTERNAL_CONFIG + 1:
                case W3_INTERNAL_CONFIG + 2:
                case W3_INTERNAL_CONFIG + 3:
                    l = dev->internal_config;
                    return (uint8_t) (l >> ((off & 3) * 8));
                case W3_OTHER_INT:
                    return dev->other_int;
                case W3_ROM_CONTROL:
                    return dev->rom_control;
                case W3_MAC_CONTROL:
                case W3_MAC_CONTROL + 1:
                    return (uint8_t) (dev->mac_control >> ((off & 1) * 8));
                case W3_RESET_OPTIONS:
                case W3_RESET_OPTIONS + 1:
                    return (uint8_t) (dev->reset_options >> ((off & 1) * 8));
                case W3_RX_FREE:
                case W3_RX_FREE + 1:
                    w = (uint16_t) ((dev->rx_size - dev->rx_used > 0xffff) ? 0xffff : (dev->rx_size - dev->rx_used));
                    return (uint8_t) (w >> ((off & 1) * 8));
                case W3_TX_FREE:
                case W3_TX_FREE + 1:
                    return (uint8_t) (tc59x_tx_free(dev) >> ((off & 1) * 8));
                default:
                    break;
            }
            break;

        case 4:
            switch (off) {
                case W4_VCO_DIAGNOSTIC:
                case W4_VCO_DIAGNOSTIC + 1:
                    return 0;
                case W4_FIFO_DIAGNOSTIC:
                case W4_FIFO_DIAGNOSTIC + 1:
                    /* receiving [15] and the overruns are never up here. */
                    return 0;
                case W4_NETWORK_DIAGNOSTIC:
                case W4_NETWORK_DIAGNOSTIC + 1:
                    return (uint8_t) (dev->network_diagnostic >> ((off & 1) * 8));
                case W4_PHYSICAL_MGMT:
                case W4_PHYSICAL_MGMT + 1:
                    /* No MII PHY answers: mgmtData reads back one, as an
                       undriven line does. */
                    w = (uint16_t) (dev->physical_mgmt | 0x0002);
                    return (uint8_t) (w >> ((off & 1) * 8));
                case W4_MEDIA_STATUS:
                case W4_MEDIA_STATUS + 1:
                    return (uint8_t) (tc59x_media_status(dev) >> ((off & 1) * 8));
                case W4_BAD_SSD:
                    return tc59x_stat8_read(dev, &dev->bad_ssd);
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
                case W5_RX_FILTER + 1:
                    return (uint8_t) (dev->rx_filter >> ((off & 1) * 8));
                case W5_INTERRUPT_ENABLE:
                case W5_INTERRUPT_ENABLE + 1:
                    return (uint8_t) (dev->int_enable >> ((off & 1) * 8));
                case W5_INDICATION_ENABLE:
                case W5_INDICATION_ENABLE + 1:
                    return (uint8_t) (dev->ind_enable >> ((off & 1) * 8));
                default:
                    break;
            }
            break;

        case 6:
            switch (off) {
                case W6_CARRIER_LOST:
                    return tc59x_stat8_read(dev, &dev->carrier_lost);
                case W6_SQE_ERRORS:
                    return tc59x_stat8_read(dev, &dev->sqe_errors);
                case W6_MULTIPLE_COLL:
                    return tc59x_stat8_read(dev, &dev->multiple_coll);
                case W6_SINGLE_COLL:
                    return tc59x_stat8_read(dev, &dev->single_coll);
                case W6_LATE_COLL:
                    return tc59x_stat8_read(dev, &dev->late_coll);
                case W6_RX_OVERRUNS:
                    return tc59x_stat8_read(dev, &dev->rx_overruns);
                case W6_FRAMES_XMITTED_OK:
                    /* Ten bits: the low eight here, the top two latched
                       into UpperFramesOk by this read. */
                    dev->upper_frames_ok   = (uint8_t) ((dev->upper_frames_ok & 0x03) | (((dev->frames_xmitted_ok >> 8) & 3) << 4));
                    w                      = dev->frames_xmitted_ok;
                    dev->frames_xmitted_ok = 0;
                    tc59x_lower(dev, INT_UPDATE_STATS);
                    return (uint8_t) (w & 0xff);
                case W6_FRAMES_RCVD_OK:
                    dev->upper_frames_ok = (uint8_t) ((dev->upper_frames_ok & 0x30) | ((dev->frames_rcvd_ok >> 8) & 3));
                    w                    = dev->frames_rcvd_ok;
                    dev->frames_rcvd_ok  = 0;
                    tc59x_lower(dev, INT_UPDATE_STATS);
                    return (uint8_t) (w & 0xff);
                case W6_FRAMES_DEFERRED:
                    return tc59x_stat8_read(dev, &dev->frames_deferred);
                case W6_UPPER_FRAMES_OK:
                    return dev->upper_frames_ok;
                case W6_BYTES_RCVD_OK:
                case W6_BYTES_RCVD_OK + 1:
                    return tc59x_stat16_read(dev, &dev->bytes_rcvd_ok, off & 1);
                case W6_BYTES_XMITTED_OK:
                case W6_BYTES_XMITTED_OK + 1:
                    return tc59x_stat16_read(dev, &dev->bytes_xmitted_ok, off & 1);
                default:
                    break;
            }
            break;

        default:
            break;
    }
    return 0;
}

static void
tc59x_reg_write(tc59x_t *dev, uint8_t window, uint8_t off, uint8_t val)
{
    /* The command register. A command with nothing in its low byte may
       be written as one byte at Fh; the rest come as a word, which
       arrives here low byte first. */
    if (off == REG_COMMAND) {
        dev->cmd_low = val;
        return;
    }
    if (off == REG_COMMAND + 1) {
        tc59x_command(dev, (uint16_t) ((val << 8) | dev->cmd_low));
        dev->cmd_low = 0;
        return;
    }

    switch (window) {
        case 0:
            switch (off) {
                case W0_CONFIG_CONTROL:
                    /* cardEnable: with it the card decodes z000h-z01Fh. */
                    dev->card_enable = val & 0x01;
                    tc59x_log("3C59x: card %s\n", dev->card_enable ? "enabled" : "disabled");
                    break;
                case W0_ADDRESS_CONFIG:
                    dev->address_config = (uint16_t) ((dev->address_config & 0xff00) | val);
                    break;
                case W0_ADDRESS_CONFIG + 1:
                    dev->address_config = (uint16_t) ((dev->address_config & 0x00ff) | ((val & 0x0f) << 8));
                    break;
                case W0_RESOURCE_CONFIG:
                    dev->resource_config = (uint16_t) ((dev->resource_config & 0xff00) | val);
                    break;
                case W0_RESOURCE_CONFIG + 1:
                    dev->resource_config = (uint16_t) ((dev->resource_config & 0x00ff) | (val << 8));
                    tc59x_set_irq_line(dev, tc59x_irq_of(dev->resource_config));
                    tc59x_log("3C59x: IRQ %i\n", dev->irq);
                    break;
                case W0_EEPROM_COMMAND:
                    tc59x_eeprom_command(dev, (uint16_t) ((dev->eeprom_command & 0xff00) | val));
                    break;
                case W0_EEPROM_COMMAND + 1:
                    /* The commands live in the low byte; the high byte
                       carries eepromBusy, which is read only. */
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
                tc59x_tx_data_write(dev, val);
            else if (off == W1_TX_STATUS)
                tc59x_tx_status_pop(dev); /* "writing an arbitrary value ... advances" */
            break;

        case 2:
            if (off < 6)
                dev->station_addr[off] = val;
            else if (off < 12)
                dev->station_mask[off - 6] = val;
            break;

        case 3:
            switch (off) {
                case W3_INTERNAL_CONFIG:
                case W3_INTERNAL_CONFIG + 1:
                    /* The low word is the hardware's and read only. */
                    break;
                case W3_INTERNAL_CONFIG + 2:
                    dev->internal_config = (dev->internal_config & 0xff00ffff) | ((uint32_t) (val & 0x73) << 16);
                    tc59x_partition(dev);
                    break;
                case W3_INTERNAL_CONFIG + 3:
                    dev->internal_config = (dev->internal_config & 0x00ffffff) | ((uint32_t) (val & 0x01) << 24);
                    break;
                case W3_OTHER_INT:
                    dev->other_int = 0; /* reserved: writes clear */
                    break;
                case W3_ROM_CONTROL:
                    dev->rom_control = val & 0x03;
                    break;
                case W3_MAC_CONTROL:
                    dev->mac_control = (uint16_t) ((dev->mac_control & 0xff00) | (val & 0x7f));
                    break;
                case W3_MAC_CONTROL + 1:
                    break;
                case W3_RESET_OPTIONS:
                    /* vcoConfig and forcedConfig are the writable bits;
                       neither changes anything here. */
                    break;
                default:
                    break;
            }
            break;

        case 4:
            switch (off) {
                case W4_NETWORK_DIAGNOSTIC:
                    /* testLowVoltageDetector resets the adapter and reads
                       back zero; asicRevision and the enables are read
                       only. */
                    if (val & 0x01)
                        tc59x_global_reset(dev, 0);
                    break;
                case W4_NETWORK_DIAGNOSTIC + 1:
                    dev->network_diagnostic = (uint16_t) ((dev->network_diagnostic & 0x0fff) | ((val & 0xf0) << 8));
                    break;
                case W4_PHYSICAL_MGMT:
                    dev->physical_mgmt = (uint16_t) ((dev->physical_mgmt & 0xff00) | (val & 0x07));
                    break;
                case W4_PHYSICAL_MGMT + 1:
                    dev->physical_mgmt = (uint16_t) ((dev->physical_mgmt & 0x00ff) | ((val & 0x80) << 8));
                    break;
                case W4_MEDIA_STATUS:
                    dev->media_status = (uint16_t) ((dev->media_status & ~(MEDIA_WRITABLE & 0xff)) | (val & MEDIA_WRITABLE & 0xff));
                    break;
                case W4_MEDIA_STATUS + 1:
                    break;
                case W4_BAD_SSD:
                    dev->bad_ssd += val; /* "Writing a value to a statistics register adds that value" */
                    break;
                default:
                    break;
            }
            break;

        case 6:
            switch (off) {
                case W6_CARRIER_LOST:
                    dev->carrier_lost += val;
                    break;
                case W6_SQE_ERRORS:
                    dev->sqe_errors += val;
                    break;
                case W6_MULTIPLE_COLL:
                    dev->multiple_coll += val;
                    break;
                case W6_SINGLE_COLL:
                    dev->single_coll += val;
                    break;
                case W6_LATE_COLL:
                    dev->late_coll += val;
                    break;
                case W6_RX_OVERRUNS:
                    dev->rx_overruns += val;
                    break;
                case W6_FRAMES_XMITTED_OK:
                    dev->frames_xmitted_ok += val;
                    break;
                case W6_FRAMES_RCVD_OK:
                    dev->frames_rcvd_ok += val;
                    break;
                case W6_FRAMES_DEFERRED:
                    dev->frames_deferred += val;
                    break;
                case W6_BYTES_RCVD_OK:
                    dev->bytes_rcvd_ok += val;
                    break;
                case W6_BYTES_RCVD_OK + 1:
                    dev->bytes_rcvd_ok += (uint16_t) (val << 8);
                    break;
                case W6_BYTES_XMITTED_OK:
                    dev->bytes_xmitted_ok += val;
                    break;
                case W6_BYTES_XMITTED_OK + 1:
                    dev->bytes_xmitted_ok += (uint16_t) (val << 8);
                    break;
                default:
                    break;
            }
            break;

        case 7:
            switch (off) {
                case W7_MASTER_ADDRESS:
                case W7_MASTER_ADDRESS + 1:
                case W7_MASTER_ADDRESS + 2:
                case W7_MASTER_ADDRESS + 3:
                    dev->master_address = (dev->master_address & ~(0xffU << ((off & 3) * 8))) | ((uint32_t) val << ((off & 3) * 8));
                    break;
                case W7_MASTER_LEN:
                    dev->master_len = (uint16_t) ((dev->master_len & 0xff00) | val);
                    break;
                case W7_MASTER_LEN + 1:
                    dev->master_len = (uint16_t) ((dev->master_len & 0x00ff) | ((val & 0x1f) << 8));
                    break;
                case W7_TX_STATUS:
                    tc59x_tx_status_pop(dev);
                    break;
                case W7_MASTER_STATUS:
                    /* The abort and retry bits clear on a one written. */
                    dev->master_status &= (uint16_t) ~(val & 0x0f);
                    break;
                case W7_MASTER_STATUS + 1:
                    dev->master_status &= (uint16_t) ~((val << 8) & (MS_MASTER_UPLOAD | MS_MASTER_DOWNLOAD));
                    if (!(dev->master_status & (MS_MASTER_UPLOAD | MS_MASTER_DOWNLOAD)))
                        tc59x_lower(dev, INT_TRANSFER);
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
}

/* ---- the slot's I/O -------------------------------------------------------- */

/* Where a slot address lands: the eight windows at zC80h-zCFFh always,
   and with cardEnable the switchable window at z000h-z00Fh and Window 1
   fixed at z010h-z01Fh. Returns the window, or 0xff for nothing. */
static uint8_t
tc59x_decode(const tc59x_t *dev, uint16_t port, uint8_t *off)
{
    uint16_t p = port & 0x0fff;

    *off = (uint8_t) (p & 0x0f);
    if ((p >= 0x0c80) && (p <= 0x0cff))
        return (uint8_t) ((p - 0x0c80) >> 4);
    if (!dev->card_enable)
        return 0xff;
    if (p < 0x10)
        return dev->window;
    if (p < 0x20)
        return 1;
    return 0xff;
}

static uint8_t
tc59x_read(uint16_t port, void *priv)
{
    tc59x_t *dev = (tc59x_t *) priv;
    uint8_t  off;
    uint8_t  window = tc59x_decode(dev, port, &off);

    if (window == 0xff)
        return 0xff;
    return tc59x_reg_read(dev, window, off);
}

static void
tc59x_write(uint16_t port, uint8_t val, void *priv)
{
    tc59x_t *dev = (tc59x_t *) priv;
    uint8_t  off;
    uint8_t  window = tc59x_decode(dev, port, &off);

    if (window == 0xff)
        return;
    tc59x_reg_write(dev, window, off, val);
}

/* The wide accesses. The data port and the command register are the two
   places width matters: a word to Command is one command, and a dword
   from RxData is four bytes of the frame in order. Everything else is its
   bytes, low first. */
static uint16_t
tc59x_readw(uint16_t port, void *priv)
{
    tc59x_t *dev = (tc59x_t *) priv;
    uint8_t  off;
    uint8_t  window = tc59x_decode(dev, port, &off);
    uint16_t ret;

    if (window == 0xff)
        return 0xffff;
    if (off >= REG_COMMAND)
        return tc59x_int_status(dev);
    ret = tc59x_reg_read(dev, window, off);
    ret |= (uint16_t) (tc59x_reg_read(dev, window, (uint8_t) (off + 1)) << 8);
    return ret;
}

static void
tc59x_writew(uint16_t port, uint16_t val, void *priv)
{
    tc59x_t *dev = (tc59x_t *) priv;
    uint8_t  off;
    uint8_t  window = tc59x_decode(dev, port, &off);

    if (window == 0xff)
        return;
    if (off == REG_COMMAND) {
        tc59x_command(dev, val);
        return;
    }
    tc59x_reg_write(dev, window, off, (uint8_t) val);
    tc59x_reg_write(dev, window, (uint8_t) (off + 1), (uint8_t) (val >> 8));
}

static uint32_t
tc59x_readl(uint16_t port, void *priv)
{
    tc59x_t *dev = (tc59x_t *) priv;
    uint8_t  off;
    uint8_t  window = tc59x_decode(dev, port, &off);
    uint32_t ret    = 0;

    if (window == 0xff)
        return 0xffffffff;
    for (uint8_t i = 0; i < 4; i++)
        ret |= (uint32_t) tc59x_reg_read(dev, window, (uint8_t) (off + i)) << (i * 8);
    return ret;
}

static void
tc59x_writel(uint16_t port, uint32_t val, void *priv)
{
    tc59x_t *dev = (tc59x_t *) priv;
    uint8_t  off;
    uint8_t  window = tc59x_decode(dev, port, &off);

    if (window == 0xff)
        return;
    for (uint8_t i = 0; i < 4; i++)
        tc59x_reg_write(dev, window, (uint8_t) (off + i), (uint8_t) (val >> (i * 8)));
}

/* ---- the device ------------------------------------------------------------- */

static void *
tc59x_init(const device_t *info)
{
    tc59x_t *dev = (tc59x_t *) calloc(1, sizeof(tc59x_t));
    uint16_t product;
    uint8_t  slot;
    int      mac;

    dev->board = (uint8_t) (info->local & 0xff);

    slot = (uint8_t) device_get_config_int("slot");
    if (slot == 0)
        slot = 1;
    dev->slot = slot;

    /* The 3Com OUI, as the EEPROM defaults have it: 00:20:AF. */
    dev->mac[0] = 0x00;
    dev->mac[1] = 0x20;
    dev->mac[2] = 0xaf;
    mac         = device_get_config_mac("mac", -1);
    if (mac & 0xff000000) {
        /* Nothing set: make one up and keep it. */
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

    if (dev->board == BOARD_3C597) {
        product            = 0x5970;
        dev->reset_options = OPT_TEST_MODE_NORMAL | OPT_BASE_TX | OPT_10BASE_T;
    } else {
        product            = 0x5920;
        dev->reset_options = OPT_TEST_MODE_NORMAL | OPT_10BASE_T | OPT_COAX | OPT_AUI;
    }
    eisa_make_id(dev->id, "TCM", product, 0);

    tc59x_eeprom_build(dev);
    tc59x_reset(dev);

    if (!eisa_add(dev->slot, dev->id, tc59x_read, tc59x_write, tc59x_reset, dev)) {
        free(dev);
        return NULL;
    }
    eisa_set_wide(dev->slot, tc59x_readw, tc59x_writew, tc59x_readl, tc59x_writel);

    dev->card = network_attach(dev, dev->mac, tc59x_rx, NULL);
    if (dev->board == BOARD_3C597)
        dev->card->byte_period = NET_PERIOD_100M;

    tc59x_log("3C59x: %s in slot %i, id %02x%02x%02x%02x, %02x:%02x:%02x:%02x:%02x:%02x\n",
              info->name, dev->slot, dev->id[0], dev->id[1], dev->id[2], dev->id[3],
              dev->mac[0], dev->mac[1], dev->mac[2], dev->mac[3], dev->mac[4], dev->mac[5]);

    return dev;
}

static void
tc59x_close(void *priv)
{
    tc59x_t *dev = (tc59x_t *) priv;

    if (dev == NULL)
        return;
    if (dev->irq != 0)
        picintclevel(1 << dev->irq, &dev->irq_state);
    netcard_close(dev->card);
    eisa_remove(dev->slot);
    free(dev);
}

static const device_config_t tc59x_config[] = {
    // clang-format off
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
        .name           = "slot",
        .description    = "EISA slot",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Slot 1", .value = 1 },
            { .description = "Slot 2", .value = 2 },
            { .description = "Slot 3", .value = 3 },
            { .description = "Slot 4", .value = 4 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t threec597_device = {
    .name          = "3Com Fast EtherLink EISA (3C597-TX)",
    .internal_name = "3c597",
    .flags         = DEVICE_EISA,
    .local         = BOARD_3C597,
    .init          = tc59x_init,
    .close         = tc59x_close,
    .reset         = tc59x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = tc59x_config
};

const device_t threec592_device = {
    .name          = "3Com EtherLink III Bus Master EISA (3C592)",
    .internal_name = "3c592",
    .flags         = DEVICE_EISA,
    .local         = BOARD_3C592,
    .init          = tc59x_init,
    .close         = tc59x_close,
    .reset         = tc59x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = tc59x_config
};
