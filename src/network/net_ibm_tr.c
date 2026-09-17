#ifdef ENABLE_IBMTR_LOG
#    include <stdarg.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/random.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/log.h>

#include "net_ibm_tr.h"

enum {
    IBM_TR_HEADER_LEN   = 2 + (IBM_TR_MAC_ADDR_LEN * 2),
    IBM_ETH_HEADER_LEN  = 14,
    IBM_SNAP_OUI_LEN    = 3,
    IBM_SNAP_PID_OFFSET = 6,
    IBM_LLC_SNAP_DSAP   = 0xaa,
    IBM_LLC_SNAP_SSAP   = 0xaa,
    IBM_LLC_UI_CONTROL  = 0x03
};

enum {
    IBM_TR_SHARED_RAM_DEFAULT = 0x4000,
    IBM_TR_SHARED_RAM_MAX     = 0x10000,
    IBM_TR_IO_PORT_COUNT     = 0x0005,
    IBM_TR_DEFAULT_IO_BASE   = 0x0a20,
    IBM_TR_DEFAULT_RAM_BASE  = 0x0d0000,
    IBM_TR_DEFAULT_SRB_OFF   = 0x0100,
    IBM_TR_DEFAULT_ASB_OFF   = 0x0120,
    IBM_TR_DEFAULT_ARB_OFF   = 0x0140,
    IBM_TR_DEFAULT_SSB_OFF   = 0x0160,
    IBM_TR_DEFAULT_INIT_OFF  = 0x0180,
    IBM_TR_DEFAULT_OPEN_OFF  = 0x01a0,
    IBM_TR_DEFAULT_RX_OFF    = 0x0800,
    IBM_TR_MAILBOX_SIZE      = 0x0020,
    IBM_TR_MAX_FRAME_STORAGE = 1600
};

enum {
    IBM_TR_IO_STATUS = 0,
    IBM_TR_IO_CONTROL,
    IBM_TR_IO_SRB_LO,
    IBM_TR_IO_SRB_HI,
    IBM_TR_IO_ATTENTION
};

enum {
    IBM_TR_STATUS_READY       = 0x01,
    IBM_TR_STATUS_OPEN        = 0x02,
    IBM_TR_STATUS_RX_PENDING  = 0x04,
    IBM_TR_STATUS_TX_COMPLETE = 0x08,
    IBM_TR_STATUS_INITIALIZED = 0x10,
    IBM_TR_STATUS_IRQ         = 0x80
};

enum {
    IBM_TR_CONTROL_RESET   = 0x01,
    IBM_TR_CONTROL_ACK_IRQ = 0x02
};

enum {
    IBM_TR_ATTN_RESP_IN_ASB = 0x10,
    IBM_TR_ATTN_CMD_IN_SRB  = 0x20
};

enum {
    IBM_TR_RING_STATE_CLOSED    = 0x00,
    IBM_TR_RING_STATE_LOBE_OK   = 0x01,
    IBM_TR_RING_STATE_VERIFY_OK = 0x03,
    IBM_TR_RING_STATE_INSERTED  = 0x04,
    IBM_TR_RING_STATE_OPEN      = 0x05
};

enum {
    IBM_TR_INSERT_PHASE_LOBE_MEDIA_TEST = 0,
    IBM_TR_INSERT_PHASE_PHYSICAL_INSERT,
    IBM_TR_INSERT_PHASE_ADDRESS_VERIFY,
    IBM_TR_INSERT_PHASE_RING_POLL,
    IBM_TR_INSERT_PHASE_REQUEST_INIT
};

enum {
    IBM_TR_POST_SUCCESS      = 0x00,
    IBM_TR_INSERTION_SUCCESS = 0x00,
    IBM_TR_INIT_SUCCESS      = 0x00,
    IBM_TR_OPEN_SUCCESS      = 0x00,
    IBM_TR_XMIT_SUCCESS      = 0x00,
    IBM_TR_INT_ACK_ALL       = 0xff,
    IBM_TR_OPEN_PASS_BCON_MAC = 0x0100
};

enum {
    IBM_TR_ARB_RX_FRAME = 0x81
};

enum {
    IBM_TR_ARB_XMIT_DATA_REQ = 0x82
};

enum {
    IBM_TR_SRB_RETCODE_OFF         = 2,
    IBM_TR_SRB_OPEN_ERROR_CODE_OFF = 6,
    IBM_TR_SRB_ASB_ADDRESS_OFF     = 8,
    IBM_TR_SRB_SRB_ADDRESS_OFF     = 10,
    IBM_TR_SRB_ARB_ADDRESS_OFF     = 12,
    IBM_TR_SRB_SSB_ADDRESS_OFF     = 14
};

enum {
    IBM_TR_SRB_CMD_CORR_OFF   = 1,
    IBM_TR_SRB_STATION_ID_OFF = 4
};

enum {
    IBM_TR_ARB_STATION_ID_OFF = 4,
    IBM_TR_ARB_DHB_ADDR_OFF   = 6
};

enum {
    IBM_TR_ASB_CMD_CORR_OFF   = 1,
    IBM_TR_ASB_RETCODE_OFF    = 2,
    IBM_TR_ASB_STATION_ID_OFF = 4,
    IBM_TR_ASB_FRAME_LEN_OFF  = 6,
    IBM_TR_ASB_HEADER_LEN_OFF = 8,
    IBM_TR_ASB_RSAP_VALUE_OFF = 9
};

enum {
    IBM_TR_OPEN_OPTIONS_OFF = 8,
    IBM_TR_OPEN_NODE_ADDR_OFF = 10,
    IBM_TR_OPEN_GROUP_ADDR_OFF = 16,
    IBM_TR_OPEN_FUNCT_ADDR_OFF = 20,
    IBM_TR_OPEN_NUM_RCV_BUF_OFF = 24,
    IBM_TR_OPEN_RCV_BUF_LEN_OFF = 26,
    IBM_TR_OPEN_DHB_LENGTH_OFF = 28,
    IBM_TR_OPEN_NUM_DHB_OFF = 30,
    IBM_TR_OPEN_DLC_MAX_SAP_OFF = 32,
    IBM_TR_OPEN_DLC_MAX_STA_OFF = 33
};

enum {
    IBM_TR_CMD_DIR_INTERRUPT  = 0x00,
    IBM_TR_CMD_DIR_OPEN       = 0x03,
    IBM_TR_CMD_DIR_CLOSE      = 0x04,
    IBM_TR_CMD_DIR_INITIALIZE = 0x05,
    IBM_TR_CMD_DIR_SET_GROUP_ADDR = 0x06,
    IBM_TR_CMD_DIR_SET_FUNCT_ADDR = 0x07,
    IBM_TR_CMD_DIR_READ_LOG   = 0x08,
    IBM_TR_CMD_DLC_OPEN_SAP   = 0x15,
    IBM_TR_CMD_DLC_CLOSE_SAP  = 0x16,
    IBM_TR_CMD_XMIT_DIR_FRAME = 0x0a,
    IBM_TR_CMD_XMIT_UI_FRAME  = 0x0d
};

enum {
    IBM_TR_READ_LOG_LINE_ERRORS_OFF         = 0,
    IBM_TR_READ_LOG_INTERNAL_ERRORS_OFF     = 1,
    IBM_TR_READ_LOG_BURST_ERRORS_OFF        = 2,
    IBM_TR_READ_LOG_AC_ERRORS_OFF           = 3,
    IBM_TR_READ_LOG_ABORT_DELIMITERS_OFF    = 4,
    IBM_TR_READ_LOG_LOST_FRAMES_OFF         = 6,
    IBM_TR_READ_LOG_RECV_CONGEST_COUNT_OFF  = 7,
    IBM_TR_READ_LOG_FRAME_COPIED_ERRORS_OFF = 8,
    IBM_TR_READ_LOG_FREQUENCY_ERRORS_OFF    = 9,
    IBM_TR_READ_LOG_TOKEN_ERRORS_OFF        = 10
};

enum {
    IBM_TR_DLC_MAX_I_FIELD_OFF  = 14,
    IBM_TR_DLC_SAP_VALUE_OFF    = 16,
    IBM_TR_DLC_SAP_OPTIONS_OFF  = 17,
    IBM_TR_DLC_STATION_COUNT_OFF = 18
};

enum {
    IBM_TR_DLC_MAX_I_FIELD_DEFAULT = 0x0088
};

enum {
    IBM_TR_DLC_SAP_EXTENDED      = 0xaa,
    IBM_TR_DLC_SAP_OPTIONS_OPEN  = 0x24,
    IBM_TR_DLC_STATION_COUNT_ONE = 0x01
};

typedef struct ibm_tr_t {
    mem_mapping_t shared_ram_mapping;
    netcard_t    *netcard;
    uint8_t       shared_ram[IBM_TR_SHARED_RAM_MAX];
    uint8_t       mac_ethernet[IBM_TR_MAC_ADDR_LEN];
    uint8_t       mac_token_ring[IBM_TR_MAC_ADDR_LEN];
    uint8_t       active_node_addr[IBM_TR_MAC_ADDR_LEN];
    uint8_t       functional_addr[IBM_TR_MAC_ADDR_LEN];
    uint8_t       group_addr[IBM_TR_MAC_ADDR_LEN];
    uint16_t      io_base;
    uint32_t      ram_base;
    uint32_t      ram_size;
    struct {
        uint16_t srb;
        uint16_t asb;
        uint16_t arb;
        uint16_t ssb;
        uint16_t init_block;
        uint16_t open_block;
        uint16_t rx;
        uint16_t rx_frame_len;
        uint16_t tx_dhb;
    } mailbox;
    struct {
        uint8_t irq;
        uint8_t ring_speed;
        uint8_t status;
        uint8_t control;
        uint8_t command;
        uint8_t attention;
        uint8_t post_status;
        uint8_t insertion_status;
        uint8_t ring_state;
        uint8_t insertion_phase;
    } regs;
    bool          initialized;
    bool          open;
    bool          promisc;
    void         *log;
    struct {
        uint16_t open_options;
        uint16_t num_rcv_buf;
        uint16_t rcv_buf_len;
        uint16_t dhb_length;
        uint8_t  num_dhb;
        uint8_t  dlc_max_sap;
        uint8_t  dlc_max_sta;
    } open_params;
    struct {
        bool     open;
        uint8_t  sap_value;
        uint8_t  sap_options;
        uint8_t  station_count;
        uint16_t station_id;
        uint16_t max_i_field;
    } sap;
    struct {
        bool     active;
        bool     arb_posted;
        uint8_t  command;
        uint8_t  cmd_corr;
        uint16_t station_id;
    } pending_tx;
    struct {
        uint8_t line_errors;
        uint8_t internal_errors;
        uint8_t burst_errors;
        uint8_t ac_errors;
        uint8_t abort_delimiters;
        uint8_t lost_frames;
        uint8_t recv_congest_count;
        uint8_t frame_copied_errors;
        uint8_t frequency_errors;
        uint8_t token_errors;
    } read_log;
} ibm_tr_t;

#ifdef ENABLE_IBMTR_LOG
uint8_t ibmtr_do_log = ENABLE_IBMTR_LOG;

static void
ibm_tr_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (ibmtr_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ibm_tr_log(priv, fmt, ...)
#endif

static uint16_t ibm_tr_readw(uint16_t port, void *priv);
static void ibm_tr_writeb(uint16_t port, uint8_t val, void *priv);
static void ibm_tr_writew(uint16_t port, uint16_t val, void *priv);
static uint8_t *ibm_tr_get_mailbox(ibm_tr_t *dev, uint16_t offset, uint16_t needed);

static void
ibm_tr_update_tx_dhb(ibm_tr_t *dev)
{
    uint16_t dhb_len;
    uint16_t min_offset;
    uint16_t offset;

    dhb_len = dev->open_params.dhb_length ? dev->open_params.dhb_length : 2048;
    if (dhb_len >= dev->ram_size) {
        dev->mailbox.tx_dhb = dev->mailbox.rx;
        return;
    }

    min_offset = (uint16_t) (dev->mailbox.rx + IBM_TR_MAX_FRAME_STORAGE + 2);
    offset     = (uint16_t) (dev->ram_size - dhb_len);

    if (offset < min_offset)
        offset = min_offset;

    if (((uint32_t) offset + dhb_len) > dev->ram_size)
        offset = (uint16_t) (dev->ram_size - dhb_len);

    dev->mailbox.tx_dhb = (uint16_t) (offset & (uint16_t) ~0x000f);
}

static void
ibm_tr_mark_asb_free(ibm_tr_t *dev)
{
    uint8_t *asb = ibm_tr_get_mailbox(dev, dev->mailbox.asb, IBM_TR_MAILBOX_SIZE);

    if (!asb)
        return;

    memset(asb, 0, IBM_TR_MAILBOX_SIZE);
    asb[IBM_TR_ASB_RETCODE_OFF] = 0xff;
}

static void
ibm_tr_set_insertion_phase(ibm_tr_t *dev, uint8_t phase)
{
    dev->regs.insertion_phase = phase;

    switch (phase) {
        case IBM_TR_INSERT_PHASE_LOBE_MEDIA_TEST:
            dev->regs.ring_state = IBM_TR_RING_STATE_LOBE_OK;
            break;
        case IBM_TR_INSERT_PHASE_PHYSICAL_INSERT:
        case IBM_TR_INSERT_PHASE_ADDRESS_VERIFY:
            dev->regs.ring_state = IBM_TR_RING_STATE_VERIFY_OK;
            break;
        case IBM_TR_INSERT_PHASE_RING_POLL:
        case IBM_TR_INSERT_PHASE_REQUEST_INIT:
            dev->regs.ring_state = IBM_TR_RING_STATE_INSERTED;
            break;
        default:
            break;
    }
}

static void
ibm_tr_spoof_insertion(ibm_tr_t *dev, bool final_open)
{
    uint8_t *trace = ibm_tr_get_mailbox(dev, 0x0050, 16);

    ibm_tr_set_insertion_phase(dev, IBM_TR_INSERT_PHASE_LOBE_MEDIA_TEST);
    ibm_tr_set_insertion_phase(dev, IBM_TR_INSERT_PHASE_PHYSICAL_INSERT);
    ibm_tr_set_insertion_phase(dev, IBM_TR_INSERT_PHASE_ADDRESS_VERIFY);
    ibm_tr_set_insertion_phase(dev, IBM_TR_INSERT_PHASE_RING_POLL);
    ibm_tr_set_insertion_phase(dev, IBM_TR_INSERT_PHASE_REQUEST_INIT);

    if (final_open)
        dev->regs.ring_state = IBM_TR_RING_STATE_OPEN;

    if (!trace)
        return;

    memset(trace, 0, 16);
    trace[0] = IBM_TR_INSERT_PHASE_LOBE_MEDIA_TEST;
    trace[1] = IBM_TR_INSERT_PHASE_PHYSICAL_INSERT;
    trace[2] = IBM_TR_INSERT_PHASE_ADDRESS_VERIFY;
    trace[3] = IBM_TR_INSERT_PHASE_RING_POLL;
    trace[4] = IBM_TR_INSERT_PHASE_REQUEST_INIT;
    trace[5] = dev->regs.insertion_phase;
    trace[6] = dev->regs.ring_state;
    trace[7] = final_open ? IBM_TR_OPEN_SUCCESS : IBM_TR_INIT_SUCCESS;
}

static uint16_t
ibm_tr_load_u16(const uint8_t *ptr)
{
    return (uint16_t) (ptr[0] | (ptr[1] << 8));
}

static void
ibm_tr_store_u16(uint8_t *ptr, uint16_t value)
{
    ptr[0] = (uint8_t) value;
    ptr[1] = (uint8_t) (value >> 8);
}

static void
ibm_tr_bump_counter(uint8_t *counter)
{
    if (*counter != 0xff)
        (*counter)++;
}

static void
ibm_tr_raise_irq(ibm_tr_t *dev)
{
    dev->regs.status |= IBM_TR_STATUS_IRQ;
    picint(1 << dev->regs.irq);
}

static void
ibm_tr_clear_irq(ibm_tr_t *dev)
{
    dev->regs.status &= (uint8_t) ~IBM_TR_STATUS_IRQ;
    picintc(1 << dev->regs.irq);
}

static bool
ibm_tr_open_option_pass_bcon_mac(const ibm_tr_t *dev)
{
    return (dev->open_params.open_options & IBM_TR_OPEN_PASS_BCON_MAC) != 0;
}

static void
ibm_tr_update_directory(ibm_tr_t *dev)
{
    dev->shared_ram[0] = 'T';
    dev->shared_ram[1] = 'R';
    ibm_tr_store_u16(&dev->shared_ram[2], dev->mailbox.srb);
    ibm_tr_store_u16(&dev->shared_ram[4], dev->mailbox.asb);
    ibm_tr_store_u16(&dev->shared_ram[6], dev->mailbox.arb);
    ibm_tr_store_u16(&dev->shared_ram[8], dev->mailbox.ssb);
    ibm_tr_store_u16(&dev->shared_ram[10], dev->mailbox.init_block);
    ibm_tr_store_u16(&dev->shared_ram[12], dev->mailbox.open_block);
    ibm_tr_store_u16(&dev->shared_ram[14], dev->mailbox.rx);
    ibm_tr_store_u16(&dev->shared_ram[0x1e], dev->mailbox.tx_dhb);
    memcpy(&dev->shared_ram[0x10], dev->active_node_addr, sizeof(dev->active_node_addr));
    dev->shared_ram[0x18] = dev->regs.ring_speed;
    dev->shared_ram[0x19] = dev->regs.post_status;
    dev->shared_ram[0x1a] = dev->regs.insertion_status;
    dev->shared_ram[0x1b] = dev->regs.ring_state;
    dev->shared_ram[0x1c] = dev->regs.status;
    dev->shared_ram[0x1d] = dev->regs.insertion_phase;
}

static void
ibm_tr_write_bringup_block(ibm_tr_t *dev)
{
    uint8_t *bringup = ibm_tr_get_mailbox(dev, 0x0040, 20);

    if (!bringup)
        return;

    memset(bringup, 0, 20);
    bringup[0] = dev->regs.post_status;
    bringup[1] = dev->regs.insertion_status;
    bringup[2] = dev->regs.ring_state;
    bringup[3] = dev->regs.ring_speed;
    ibm_tr_store_u16(&bringup[4], dev->mailbox.init_block);
    ibm_tr_store_u16(&bringup[6], dev->mailbox.open_block);
    ibm_tr_store_u16(&bringup[8], dev->mailbox.arb);
    ibm_tr_store_u16(&bringup[10], dev->mailbox.ssb);
    memcpy(&bringup[12], dev->active_node_addr, 4);
    bringup[16] = dev->regs.insertion_phase;
    bringup[17] = dev->promisc ? 1 : 0;
}

static void
ibm_tr_seed_shared_ram(ibm_tr_t *dev)
{
    memset(dev->shared_ram, 0, sizeof(dev->shared_ram));

    ibm_tr_update_directory(dev);
    ibm_tr_write_bringup_block(dev);
}

static void
ibm_tr_reset(ibm_tr_t *dev)
{
    dev->regs.status      = IBM_TR_STATUS_READY;
    dev->regs.control     = 0;
    dev->regs.command     = 0;
    dev->regs.attention   = 0;
    dev->regs.post_status = IBM_TR_POST_SUCCESS;
    dev->regs.insertion_status = IBM_TR_INSERTION_SUCCESS;
    dev->regs.ring_state  = IBM_TR_RING_STATE_CLOSED;
    dev->regs.insertion_phase = IBM_TR_INSERT_PHASE_LOBE_MEDIA_TEST;
    dev->initialized = false;
    dev->open        = false;
    dev->promisc     = false;
    dev->mailbox.srb  = IBM_TR_DEFAULT_SRB_OFF;
    dev->mailbox.asb  = IBM_TR_DEFAULT_ASB_OFF;
    dev->mailbox.arb  = IBM_TR_DEFAULT_ARB_OFF;
    dev->mailbox.ssb  = IBM_TR_DEFAULT_SSB_OFF;
    dev->mailbox.init_block = IBM_TR_DEFAULT_INIT_OFF;
    dev->mailbox.open_block = IBM_TR_DEFAULT_OPEN_OFF;
    dev->mailbox.rx   = IBM_TR_DEFAULT_RX_OFF;
    dev->mailbox.rx_frame_len = 0;
    dev->open_params.open_options = 0x0100;
    dev->open_params.num_rcv_buf = 2;
    dev->open_params.rcv_buf_len = 1024;
    dev->open_params.dhb_length = 2048;
    dev->open_params.num_dhb = 2;
    dev->open_params.dlc_max_sap = 2;
    dev->open_params.dlc_max_sta = 1;
    dev->sap.open = false;
    dev->sap.sap_value = 0;
    dev->sap.sap_options = 0;
    dev->sap.station_count = 0;
    dev->sap.station_id = 0x0001;
    dev->sap.max_i_field = 0;
    dev->pending_tx.active = false;
    dev->pending_tx.arb_posted = false;
    dev->pending_tx.command = 0;
    dev->pending_tx.cmd_corr = 0;
    dev->pending_tx.station_id = 0;
    memset(&dev->read_log, 0, sizeof(dev->read_log));
    ibm_tr_update_tx_dhb(dev);
    memcpy(dev->active_node_addr, dev->mac_token_ring, sizeof(dev->active_node_addr));
    memset(dev->functional_addr, 0, sizeof(dev->functional_addr));
    memset(dev->group_addr, 0, sizeof(dev->group_addr));

    ibm_tr_log(dev->log, "reset: ram_size=%#x irq=%u ring_speed=%u\n",
               (unsigned int) dev->ram_size, dev->regs.irq, dev->regs.ring_speed);

    ibm_tr_seed_shared_ram(dev);
    ibm_tr_mark_asb_free(dev);
    ibm_tr_clear_irq(dev);
}

static uint8_t *
ibm_tr_get_mailbox(ibm_tr_t *dev, uint16_t offset, uint16_t needed)
{
    if ((offset >= dev->ram_size) || (needed > dev->ram_size) ||
        ((uint32_t) offset + needed > dev->ram_size))
        return NULL;

    return &dev->shared_ram[offset];
}

static void
ibm_tr_complete_srb(ibm_tr_t *dev, uint8_t *srb, uint8_t rc)
{
    srb[1] = rc;
    srb[IBM_TR_SRB_RETCODE_OFF] = rc;
    ibm_tr_update_directory(dev);
    ibm_tr_raise_irq(dev);
}

static void
ibm_tr_write_read_log_response(ibm_tr_t *dev, uint8_t *srb)
{
    memset(srb, 0, IBM_TR_MAILBOX_SIZE);
    srb[IBM_TR_READ_LOG_LINE_ERRORS_OFF] = dev->read_log.line_errors;
    srb[IBM_TR_READ_LOG_INTERNAL_ERRORS_OFF] = dev->read_log.internal_errors;
    srb[IBM_TR_READ_LOG_BURST_ERRORS_OFF] = dev->read_log.burst_errors;
    srb[IBM_TR_READ_LOG_AC_ERRORS_OFF] = dev->read_log.ac_errors;
    srb[IBM_TR_READ_LOG_ABORT_DELIMITERS_OFF] = dev->read_log.abort_delimiters;
    srb[IBM_TR_READ_LOG_LOST_FRAMES_OFF] = dev->read_log.lost_frames;
    srb[IBM_TR_READ_LOG_RECV_CONGEST_COUNT_OFF] = dev->read_log.recv_congest_count;
    srb[IBM_TR_READ_LOG_FRAME_COPIED_ERRORS_OFF] = dev->read_log.frame_copied_errors;
    srb[IBM_TR_READ_LOG_FREQUENCY_ERRORS_OFF] = dev->read_log.frequency_errors;
    srb[IBM_TR_READ_LOG_TOKEN_ERRORS_OFF] = dev->read_log.token_errors;
    memset(&dev->read_log, 0, sizeof(dev->read_log));
    ibm_tr_update_directory(dev);
    ibm_tr_raise_irq(dev);
}

static void
ibm_tr_write_open_response(const ibm_tr_t *dev, uint8_t *srb, uint8_t rc)
{
    srb[IBM_TR_SRB_RETCODE_OFF] = rc;
    ibm_tr_store_u16(&srb[IBM_TR_SRB_OPEN_ERROR_CODE_OFF], 0);

    if (rc != IBM_TR_OPEN_SUCCESS) {
        ibm_tr_store_u16(&srb[IBM_TR_SRB_ASB_ADDRESS_OFF], 0);
        ibm_tr_store_u16(&srb[IBM_TR_SRB_SRB_ADDRESS_OFF], 0);
        ibm_tr_store_u16(&srb[IBM_TR_SRB_ARB_ADDRESS_OFF], 0);
        ibm_tr_store_u16(&srb[IBM_TR_SRB_SSB_ADDRESS_OFF], 0);
        return;
    }

    ibm_tr_store_u16(&srb[IBM_TR_SRB_ASB_ADDRESS_OFF], dev->mailbox.asb);
    ibm_tr_store_u16(&srb[IBM_TR_SRB_SRB_ADDRESS_OFF], dev->mailbox.srb);
    ibm_tr_store_u16(&srb[IBM_TR_SRB_ARB_ADDRESS_OFF], dev->mailbox.arb);
    ibm_tr_store_u16(&srb[IBM_TR_SRB_SSB_ADDRESS_OFF], dev->mailbox.ssb);
}

static void
ibm_tr_write_init_block(ibm_tr_t *dev)
{
    uint8_t *initblk = ibm_tr_get_mailbox(dev, dev->mailbox.init_block, 16);

    if (!initblk)
        return;

    memset(initblk, 0, 16);
    initblk[0] = IBM_TR_INIT_SUCCESS;
    initblk[1] = dev->regs.ring_speed;
    ibm_tr_store_u16(&initblk[2], dev->mailbox.rx);
    ibm_tr_store_u16(&initblk[4], dev->mailbox.arb);
    ibm_tr_store_u16(&initblk[6], dev->mailbox.ssb);
    memcpy(&initblk[8], dev->active_node_addr, sizeof(dev->active_node_addr));
    initblk[14] = dev->regs.insertion_phase;
}

static void
ibm_tr_write_open_block(ibm_tr_t *dev)
{
    uint8_t *openblk = ibm_tr_get_mailbox(dev, dev->mailbox.open_block, 16);

    if (!openblk)
        return;

    memset(openblk, 0, 16);
    openblk[0] = IBM_TR_OPEN_SUCCESS;
    openblk[1] = dev->regs.ring_speed;
    openblk[2] = dev->regs.ring_state;
    openblk[3] = dev->regs.insertion_status;
    ibm_tr_store_u16(&openblk[4], dev->mailbox.rx);
    ibm_tr_store_u16(&openblk[6], dev->mailbox.rx_frame_len);
    memcpy(&openblk[8], dev->active_node_addr, sizeof(dev->active_node_addr));
    openblk[14] = dev->regs.insertion_phase;
    openblk[15] = dev->promisc ? 1 : 0;
}

static void
ibm_tr_write_ssb(ibm_tr_t *dev, uint8_t command, uint8_t cmd_corr, uint8_t rc,
                 uint16_t station_id)
{
    uint8_t *ssb = ibm_tr_get_mailbox(dev, dev->mailbox.ssb, 16);

    if (!ssb)
        return;

    memset(ssb, 0, 16);
    ssb[0] = command;
    ssb[1] = cmd_corr;
    ssb[2] = rc;
    ssb[3] = 0x00;
    ibm_tr_store_u16(&ssb[4], station_id);
    ssb[6] = 0x00;
}

static void
ibm_tr_write_arb_rx(ibm_tr_t *dev)
{
    uint8_t *arb = ibm_tr_get_mailbox(dev, dev->mailbox.arb, 16);

    if (!arb)
        return;

    memset(arb, 0, 16);
    arb[0] = IBM_TR_ARB_RX_FRAME;
    arb[1] = dev->regs.ring_state;
    arb[2] = dev->regs.status;
    arb[3] = dev->regs.insertion_status;
    ibm_tr_store_u16(&arb[4], dev->mailbox.rx);
    ibm_tr_store_u16(&arb[6], dev->mailbox.rx_frame_len);
}

static void
ibm_tr_write_arb_xmit(ibm_tr_t *dev)
{
    uint8_t *arb = ibm_tr_get_mailbox(dev, dev->mailbox.arb, 16);

    if (!arb)
        return;

    memset(arb, 0, 16);
    arb[0] = IBM_TR_ARB_XMIT_DATA_REQ;
    ibm_tr_store_u16(&arb[IBM_TR_ARB_STATION_ID_OFF], dev->pending_tx.station_id);
    ibm_tr_store_u16(&arb[IBM_TR_ARB_DHB_ADDR_OFF], dev->mailbox.tx_dhb);
}

static void
ibm_tr_post_pending_tx_arb(ibm_tr_t *dev)
{
    if (!dev->pending_tx.active || dev->pending_tx.arb_posted)
        return;

    ibm_tr_write_arb_xmit(dev);
    dev->pending_tx.arb_posted = true;
    ibm_tr_update_directory(dev);
    ibm_tr_raise_irq(dev);
    ibm_tr_log(dev->log, "xmit arb: cmd=%02x cmd_corr=%02x station=%#04x dhb=%#04x\n",
               dev->pending_tx.command, dev->pending_tx.cmd_corr,
               dev->pending_tx.station_id, dev->mailbox.tx_dhb);
}

static bool
ibm_tr_stage_rx_frame(ibm_tr_t *dev, const uint8_t *tr_frame, size_t tr_len)
{
    uint8_t *dst;

    if ((dev->mailbox.rx + 2 + tr_len) > dev->ram_size)
    {
        ibm_tr_bump_counter(&dev->read_log.recv_congest_count);
        return false;
    }

    dst = &dev->shared_ram[dev->mailbox.rx];
    ibm_tr_store_u16(dst, (uint16_t) tr_len);
    memcpy(dst + 2, tr_frame, tr_len);
    dev->mailbox.rx_frame_len = (uint16_t) tr_len;
    dev->regs.status |= IBM_TR_STATUS_RX_PENDING;
    ibm_tr_write_arb_rx(dev);
    ibm_tr_update_directory(dev);
    ibm_tr_raise_irq(dev);

    return true;
}

static bool
ibm_tr_is_broadcast_addr(const uint8_t addr[IBM_TR_MAC_ADDR_LEN])
{
    static const uint8_t broadcast_addr[IBM_TR_MAC_ADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    return !memcmp(addr, broadcast_addr, IBM_TR_MAC_ADDR_LEN);
}

static bool
ibm_tr_has_nonzero_addr(const uint8_t addr[IBM_TR_MAC_ADDR_LEN])
{
    for (uint8_t i = 0; i < IBM_TR_MAC_ADDR_LEN; i++) {
        if (addr[i])
            return true;
    }

    return false;
}

static void
ibm_tr_expand_open_multicast_address(uint8_t dest[IBM_TR_MAC_ADDR_LEN], const uint8_t *src)
{
    static const uint8_t zero_addr[4] = { 0 };

    if (!memcmp(src, zero_addr, sizeof(zero_addr))) {
        memset(dest, 0, IBM_TR_MAC_ADDR_LEN);
        return;
    }

    dest[0] = 0xc0;
    dest[1] = 0x00;
    memcpy(&dest[2], src, 4);
}

static void
ibm_tr_parse_open_parameters(ibm_tr_t *dev, const uint8_t *srb)
{
    dev->open_params.open_options = ibm_tr_load_u16(&srb[IBM_TR_OPEN_OPTIONS_OFF]);
    dev->open_params.num_rcv_buf = ibm_tr_load_u16(&srb[IBM_TR_OPEN_NUM_RCV_BUF_OFF]);
    dev->open_params.rcv_buf_len = ibm_tr_load_u16(&srb[IBM_TR_OPEN_RCV_BUF_LEN_OFF]);
    dev->open_params.dhb_length = ibm_tr_load_u16(&srb[IBM_TR_OPEN_DHB_LENGTH_OFF]);
    dev->open_params.num_dhb = srb[IBM_TR_OPEN_NUM_DHB_OFF];
    dev->open_params.dlc_max_sap = srb[IBM_TR_OPEN_DLC_MAX_SAP_OFF];
    dev->open_params.dlc_max_sta = srb[IBM_TR_OPEN_DLC_MAX_STA_OFF];

    ibm_tr_log(dev->log,
               "open: options=%#04x node=%02x:%02x:%02x:%02x:%02x:%02x group=%02x%02x%02x%02x funct=%02x%02x%02x%02x rcv=%u x %u dhb=%u num_dhb=%u sap=%u sta=%u pass_bcon_mac=%u\n",
               dev->open_params.open_options,
               srb[IBM_TR_OPEN_NODE_ADDR_OFF + 0], srb[IBM_TR_OPEN_NODE_ADDR_OFF + 1],
               srb[IBM_TR_OPEN_NODE_ADDR_OFF + 2], srb[IBM_TR_OPEN_NODE_ADDR_OFF + 3],
               srb[IBM_TR_OPEN_NODE_ADDR_OFF + 4], srb[IBM_TR_OPEN_NODE_ADDR_OFF + 5],
               srb[IBM_TR_OPEN_GROUP_ADDR_OFF + 0], srb[IBM_TR_OPEN_GROUP_ADDR_OFF + 1],
               srb[IBM_TR_OPEN_GROUP_ADDR_OFF + 2], srb[IBM_TR_OPEN_GROUP_ADDR_OFF + 3],
               srb[IBM_TR_OPEN_FUNCT_ADDR_OFF + 0], srb[IBM_TR_OPEN_FUNCT_ADDR_OFF + 1],
               srb[IBM_TR_OPEN_FUNCT_ADDR_OFF + 2], srb[IBM_TR_OPEN_FUNCT_ADDR_OFF + 3],
               dev->open_params.num_rcv_buf, dev->open_params.rcv_buf_len,
               dev->open_params.dhb_length, dev->open_params.num_dhb,
               dev->open_params.dlc_max_sap, dev->open_params.dlc_max_sta,
               ibm_tr_open_option_pass_bcon_mac(dev) ? 1 : 0);
}

static void
ibm_tr_apply_open_addressing(ibm_tr_t *dev, const uint8_t *srb)
{
    static const uint8_t zero_addr[IBM_TR_MAC_ADDR_LEN] = { 0 };

    if (memcmp(&srb[IBM_TR_OPEN_NODE_ADDR_OFF], zero_addr, IBM_TR_MAC_ADDR_LEN))
        memcpy(dev->active_node_addr, &srb[IBM_TR_OPEN_NODE_ADDR_OFF], IBM_TR_MAC_ADDR_LEN);
    else
        memcpy(dev->active_node_addr, dev->mac_token_ring, IBM_TR_MAC_ADDR_LEN);

    ibm_tr_expand_open_multicast_address(dev->group_addr, &srb[IBM_TR_OPEN_GROUP_ADDR_OFF]);
    ibm_tr_expand_open_multicast_address(dev->functional_addr, &srb[IBM_TR_OPEN_FUNCT_ADDR_OFF]);
    dev->promisc = false;

    ibm_tr_log(dev->log,
               "open addressing: active=%02x:%02x:%02x:%02x:%02x:%02x group=%02x:%02x:%02x:%02x:%02x:%02x funct=%02x:%02x:%02x:%02x:%02x:%02x\n",
               dev->active_node_addr[0], dev->active_node_addr[1], dev->active_node_addr[2],
               dev->active_node_addr[3], dev->active_node_addr[4], dev->active_node_addr[5],
               dev->group_addr[0], dev->group_addr[1], dev->group_addr[2],
               dev->group_addr[3], dev->group_addr[4], dev->group_addr[5],
               dev->functional_addr[0], dev->functional_addr[1], dev->functional_addr[2],
               dev->functional_addr[3], dev->functional_addr[4], dev->functional_addr[5]);
}

static void
ibm_tr_apply_direct_multicast_address(uint8_t dest[IBM_TR_MAC_ADDR_LEN], const uint8_t *srb)
{
    ibm_tr_expand_open_multicast_address(dest, &srb[6]);
}

static bool
ibm_tr_accept_rx_frame(const ibm_tr_t *dev, const uint8_t *tr_frame, size_t tr_len)
{
    const uint8_t *dst_addr;

    if (tr_len < IBM_TR_HEADER_LEN)
        return false;

    if (dev->promisc)
        return true;

    dst_addr = tr_frame + 2;
    if (!memcmp(dst_addr, dev->active_node_addr, IBM_TR_MAC_ADDR_LEN))
        return true;

    if (ibm_tr_is_broadcast_addr(dst_addr))
        return true;

    if (ibm_tr_has_nonzero_addr(dev->functional_addr) &&
        !memcmp(dst_addr, dev->functional_addr, IBM_TR_MAC_ADDR_LEN))
        return true;

    if (ibm_tr_has_nonzero_addr(dev->group_addr) &&
        !memcmp(dst_addr, dev->group_addr, IBM_TR_MAC_ADDR_LEN))
        return true;

    return false;
}

static uint8_t
ibm_tr_handle_mac_management_frame(ibm_tr_t *dev, const uint8_t *tr_frame, size_t tr_len)
{
    if (!dev->open)
        return 0xfd;

    if (!ibm_tr_open_option_pass_bcon_mac(dev)) {
        ibm_tr_log(dev->log, "mac frame suppressed: open_options=%#04x len=%u\n",
                   dev->open_params.open_options, (unsigned int) tr_len);
        return IBM_TR_XMIT_SUCCESS;
    }

    if (ibm_tr_accept_rx_frame(dev, tr_frame, tr_len))
        (void) ibm_tr_stage_rx_frame(dev, tr_frame, tr_len);

    ibm_tr_log(dev->log, "mac frame loopback: len=%u\n", (unsigned int) tr_len);

    return IBM_TR_XMIT_SUCCESS;
}

static uint8_t
ibm_tr_transmit_frame(ibm_tr_t *dev, uint8_t command, uint8_t cmd_corr,
                      uint16_t station_id, const uint8_t *frame, uint16_t frame_len)
{
    uint8_t eth_frame[IBM_TR_MAX_FRAME_STORAGE];
    size_t   eth_len;
    ibm_tr_frame_info_t info;
    ibm_tr_result_t result;

    result = ibm_tr_parse_frame(frame, frame_len, &info);
    if (result != IBM_TR_OK)
        return 0xfe;

    if ((info.frame_control & 0xc0) != IBM_TR_FC_LLC_FRAME) {
        result = ibm_tr_handle_mac_management_frame(dev, frame, frame_len);
        ibm_tr_write_ssb(dev, command, cmd_corr, (uint8_t) result, station_id);
        return (uint8_t) result;
    }

    result = ibm_tr_translate_token_ring_to_ethernet(frame, frame_len,
                                                     eth_frame, sizeof(eth_frame), &eth_len);
    if (result != IBM_TR_OK)
        return 0xfe;

    network_tx(dev->netcard, eth_frame, (int) eth_len);
    dev->regs.status |= IBM_TR_STATUS_TX_COMPLETE;
    ibm_tr_write_ssb(dev, command, cmd_corr, IBM_TR_XMIT_SUCCESS, station_id);
    ibm_tr_log(dev->log, "xmit: cmd=%02x cmd_corr=%02x station=%#04x tr_len=%u eth_len=%u\n",
               command, cmd_corr, station_id, frame_len, (unsigned int) eth_len);

    return 0x00;
}

static uint8_t
ibm_tr_queue_xmit_from_srb(ibm_tr_t *dev, const uint8_t *srb)
{
    uint16_t station_id;

    if (dev->pending_tx.active)
        return 0xff;

    station_id = ibm_tr_load_u16(&srb[IBM_TR_SRB_STATION_ID_OFF]);
    if (!dev->sap.open || (station_id != dev->sap.station_id)) {
        ibm_tr_log(dev->log, "xmit rejected: sap_open=%u station=%#04x expected=%#04x\n",
                   dev->sap.open ? 1 : 0, station_id, dev->sap.station_id);
        return 0xff;
    }

    dev->pending_tx.active     = true;
    dev->pending_tx.arb_posted = false;
    dev->pending_tx.command    = srb[0];
    dev->pending_tx.cmd_corr   = srb[IBM_TR_SRB_CMD_CORR_OFF];
    dev->pending_tx.station_id = station_id;

    ibm_tr_log(dev->log, "xmit srb: cmd=%02x cmd_corr=%02x station=%#04x\n",
               dev->pending_tx.command, dev->pending_tx.cmd_corr, dev->pending_tx.station_id);

    return 0x00;
}

static void
ibm_tr_run_asb(ibm_tr_t *dev)
{
    const uint8_t *asb = ibm_tr_get_mailbox(dev, dev->mailbox.asb, IBM_TR_MAILBOX_SIZE);
    uint16_t frame_len;
    uint8_t  rc;
    const uint8_t *frame;

    if (!asb)
        return;

    if (!dev->pending_tx.active || !dev->pending_tx.arb_posted)
        return;

    ibm_tr_log(dev->log,
               "asb xmit: cmd=%02x cmd_corr=%02x station=%#04x rc=%02x frame_len=%u hdr_len=%u rsap=%02x\n",
               asb[0], asb[IBM_TR_ASB_CMD_CORR_OFF],
               ibm_tr_load_u16(&asb[IBM_TR_ASB_STATION_ID_OFF]),
               asb[IBM_TR_ASB_RETCODE_OFF],
               ibm_tr_load_u16(&asb[IBM_TR_ASB_FRAME_LEN_OFF]),
               asb[IBM_TR_ASB_HEADER_LEN_OFF], asb[IBM_TR_ASB_RSAP_VALUE_OFF]);

    rc        = asb[IBM_TR_ASB_RETCODE_OFF];
    frame_len = ibm_tr_load_u16(&asb[IBM_TR_ASB_FRAME_LEN_OFF]);

    if (rc == 0x00) {
        frame = ibm_tr_get_mailbox(dev, dev->mailbox.tx_dhb, frame_len);
        if (!frame)
            rc = 0xff;
        else
            rc = ibm_tr_transmit_frame(dev, dev->pending_tx.command, dev->pending_tx.cmd_corr,
                                       dev->pending_tx.station_id, frame, frame_len);
    }

    if (rc != IBM_TR_XMIT_SUCCESS)
        ibm_tr_write_ssb(dev, dev->pending_tx.command, dev->pending_tx.cmd_corr, rc,
                         dev->pending_tx.station_id);

    dev->pending_tx.active     = false;
    dev->pending_tx.arb_posted = false;
    dev->pending_tx.command    = 0;
    dev->pending_tx.cmd_corr   = 0;
    dev->pending_tx.station_id = 0;
    ibm_tr_mark_asb_free(dev);
    ibm_tr_update_directory(dev);
    ibm_tr_raise_irq(dev);
}

static void
ibm_tr_run_srb(ibm_tr_t *dev)
{
    uint8_t *srb = ibm_tr_get_mailbox(dev, dev->mailbox.srb, IBM_TR_MAILBOX_SIZE);
    uint8_t  rc  = 0x00;

    if (!srb) {
        ibm_tr_raise_irq(dev);
        return;
    }

    switch (srb[0]) {
        case IBM_TR_CMD_DIR_INTERRUPT:
            ibm_tr_log(dev->log, "srb: DIR_INTERRUPT arg=%02x\n", srb[2]);
            if (srb[2] == IBM_TR_INT_ACK_ALL) {
                dev->regs.status &= (uint8_t) ~(IBM_TR_STATUS_RX_PENDING | IBM_TR_STATUS_TX_COMPLETE);
                dev->mailbox.rx_frame_len = 0;
                dev->regs.attention = 0;
                ibm_tr_clear_irq(dev);
                ibm_tr_post_pending_tx_arb(dev);
            }
            break;

        case IBM_TR_CMD_DIR_INITIALIZE:
            ibm_tr_log(dev->log, "srb: DIR_INITIALIZE\n");
            dev->initialized = true;
            dev->regs.status |= IBM_TR_STATUS_INITIALIZED;
            ibm_tr_spoof_insertion(dev, false);
            ibm_tr_write_init_block(dev);
            ibm_tr_write_bringup_block(dev);
            break;

        case IBM_TR_CMD_DIR_OPEN:
            ibm_tr_log(dev->log, "srb: DIR_OPEN\n");
            if (!dev->initialized)
                rc = 0xfd;
            else {
                ibm_tr_parse_open_parameters(dev, srb);
                ibm_tr_apply_open_addressing(dev, srb);
                ibm_tr_update_tx_dhb(dev);
                dev->open = true;
                dev->sap.open = false;
                dev->regs.status |= IBM_TR_STATUS_OPEN;
                ibm_tr_spoof_insertion(dev, true);
                ibm_tr_write_open_block(dev);
                ibm_tr_write_bringup_block(dev);
            }
            ibm_tr_write_open_response(dev, srb, rc);
            break;

        case IBM_TR_CMD_DLC_OPEN_SAP:
        {
            uint16_t max_i_field;
            uint8_t sap_value;
            uint8_t sap_options;
            uint8_t station_count;

            max_i_field = ibm_tr_load_u16(&srb[IBM_TR_DLC_MAX_I_FIELD_OFF]);
            sap_value = srb[IBM_TR_DLC_SAP_VALUE_OFF];
            sap_options = srb[IBM_TR_DLC_SAP_OPTIONS_OFF];
            station_count = srb[IBM_TR_DLC_STATION_COUNT_OFF];

            ibm_tr_log(dev->log,
                       "srb: DLC_OPEN_SAP sap=%02x options=%02x station_count=%u max_i=%#04x\n",
                       sap_value, sap_options, station_count, max_i_field);
            if (!dev->open)
                rc = 0xfd;
            else if ((sap_value != IBM_TR_DLC_SAP_EXTENDED) ||
                     (sap_options != IBM_TR_DLC_SAP_OPTIONS_OPEN) ||
                     (station_count != IBM_TR_DLC_STATION_COUNT_ONE) ||
                     (max_i_field != IBM_TR_DLC_MAX_I_FIELD_DEFAULT))
                rc = 0xff;
            else if (dev->sap.open &&
                     ((sap_value != dev->sap.sap_value) ||
                      (sap_options != dev->sap.sap_options) ||
                      (station_count != dev->sap.station_count) ||
                      (max_i_field != dev->sap.max_i_field)))
                rc = 0xff;
            else {
                dev->sap.open = true;
                dev->sap.sap_value = sap_value;
                dev->sap.sap_options = sap_options;
                dev->sap.station_count = station_count;
                dev->sap.max_i_field = max_i_field;
                ibm_tr_store_u16(&srb[IBM_TR_SRB_STATION_ID_OFF], dev->sap.station_id);
                ibm_tr_log(dev->log,
                           "sap opened: sap=%02x options=%02x station_count=%u station=%#04x max_i=%#04x\n",
                           dev->sap.sap_value, dev->sap.sap_options, dev->sap.station_count,
                           dev->sap.station_id, dev->sap.max_i_field);
            }
            break;
        }

        case IBM_TR_CMD_DLC_CLOSE_SAP:
            ibm_tr_log(dev->log, "srb: DLC_CLOSE_SAP station=%#04x\n",
                       ibm_tr_load_u16(&srb[IBM_TR_SRB_STATION_ID_OFF]));
            if (!dev->sap.open)
                rc = 0xfd;
            else if (ibm_tr_load_u16(&srb[IBM_TR_SRB_STATION_ID_OFF]) != dev->sap.station_id)
                rc = 0xff;
            else {
                dev->sap.open = false;
                dev->sap.sap_value = 0;
                dev->sap.sap_options = 0;
                dev->sap.station_count = 0;
                dev->sap.max_i_field = 0;
                ibm_tr_log(dev->log, "sap closed: station=%#04x\n", dev->sap.station_id);
            }
            break;

        case IBM_TR_CMD_DIR_SET_FUNCT_ADDR:
            ibm_tr_log(dev->log, "srb: DIR_SET_FUNCT_ADDR payload=%02x%02x%02x%02x\n",
                       srb[6], srb[7], srb[8], srb[9]);
            if (!dev->open)
                rc = 0xfd;
            else {
                ibm_tr_apply_direct_multicast_address(dev->functional_addr, srb);
                ibm_tr_log(dev->log, "functional address: %02x:%02x:%02x:%02x:%02x:%02x\n",
                           dev->functional_addr[0], dev->functional_addr[1], dev->functional_addr[2],
                           dev->functional_addr[3], dev->functional_addr[4], dev->functional_addr[5]);
                ibm_tr_write_open_block(dev);
                ibm_tr_write_bringup_block(dev);
            }
            break;

        case IBM_TR_CMD_DIR_SET_GROUP_ADDR:
            ibm_tr_log(dev->log, "srb: DIR_SET_GROUP_ADDR payload=%02x%02x%02x%02x\n",
                       srb[6], srb[7], srb[8], srb[9]);
            if (!dev->open)
                rc = 0xfd;
            else {
                ibm_tr_apply_direct_multicast_address(dev->group_addr, srb);
                ibm_tr_log(dev->log, "group address: %02x:%02x:%02x:%02x:%02x:%02x\n",
                           dev->group_addr[0], dev->group_addr[1], dev->group_addr[2],
                           dev->group_addr[3], dev->group_addr[4], dev->group_addr[5]);
                ibm_tr_write_open_block(dev);
                ibm_tr_write_bringup_block(dev);
            }
            break;

        case IBM_TR_CMD_DIR_CLOSE:
            ibm_tr_log(dev->log, "srb: DIR_CLOSE\n");
            dev->open = false;
            dev->sap.open = false;
            dev->regs.status &= (uint8_t) ~IBM_TR_STATUS_OPEN;
            dev->regs.ring_state = IBM_TR_RING_STATE_CLOSED;
            dev->regs.insertion_phase = IBM_TR_INSERT_PHASE_LOBE_MEDIA_TEST;
            ibm_tr_write_bringup_block(dev);
            break;

        case IBM_TR_CMD_XMIT_UI_FRAME:
        case IBM_TR_CMD_XMIT_DIR_FRAME:
            ibm_tr_log(dev->log, "srb: XMIT cmd=%02x cmd_corr=%02x station=%#04x\n",
                       srb[0], srb[IBM_TR_SRB_CMD_CORR_OFF],
                       ibm_tr_load_u16(&srb[IBM_TR_SRB_STATION_ID_OFF]));
            if (!dev->open)
                rc = 0xfd;
            else
                rc = ibm_tr_queue_xmit_from_srb(dev, srb);
            break;

        default:
            ibm_tr_log(dev->log, "srb: unknown command=%02x\n", srb[0]);
            rc = 0xfc;
            break;
    }

    ibm_tr_complete_srb(dev, srb, rc);
}

static int
ibm_tr_receive(void *priv, uint8_t *bufp, int len)
{
    ibm_tr_t *dev = (ibm_tr_t *) priv;
    uint8_t   tr_frame[IBM_TR_MAX_FRAME_STORAGE];
    size_t    tr_len;
    ibm_tr_result_t result;

    if (!dev->open)
        return 0;

    result = ibm_tr_translate_ethernet_to_token_ring(bufp, (size_t) len,
                                                     tr_frame, sizeof(tr_frame), &tr_len);
    if ((result == IBM_TR_OK) && ibm_tr_accept_rx_frame(dev, tr_frame, tr_len)) {
        ibm_tr_log(dev->log, "rx: eth_len=%d tr_len=%u\n", len, (unsigned int) tr_len);
        (void) ibm_tr_stage_rx_frame(dev, tr_frame, tr_len);
    }

    return 0;
}

static int
ibm_tr_set_link_state(void *priv, uint32_t link_state)
{
    ibm_tr_t *dev = (ibm_tr_t *) priv;

    if (link_state & NET_LINK_DOWN)
        dev->regs.status &= (uint8_t) ~IBM_TR_STATUS_OPEN;
    else if (dev->open)
        dev->regs.status |= IBM_TR_STATUS_OPEN;

    if (!dev->open)
        dev->regs.ring_state = IBM_TR_RING_STATE_CLOSED;
    else if (link_state & NET_LINK_DOWN)
        dev->regs.ring_state = IBM_TR_RING_STATE_INSERTED;
    else
        dev->regs.ring_state = IBM_TR_RING_STATE_OPEN;

    ibm_tr_update_directory(dev);

    return 0;
}

static uint8_t
ibm_tr_ram_readb(uint32_t addr, void *priv)
{
    const ibm_tr_t *dev = (const ibm_tr_t *) priv;
    uint32_t        offset;

    if (addr < dev->ram_base)
        return 0xff;

    offset = addr - dev->ram_base;
    if (offset >= dev->ram_size)
        return 0xff;

    return dev->shared_ram[offset];
}

static uint16_t
ibm_tr_ram_readw(uint32_t addr, void *priv)
{
    uint16_t value = ibm_tr_ram_readb(addr, priv);

    value |= (uint16_t) (ibm_tr_ram_readb(addr + 1, priv) << 8);
    return value;
}

static uint32_t
ibm_tr_ram_readl(uint32_t addr, void *priv)
{
    uint32_t value = ibm_tr_ram_readw(addr, priv);

    value |= (uint32_t) ibm_tr_ram_readw(addr + 2, priv) << 16;
    return value;
}

static void
ibm_tr_ram_writeb(uint32_t addr, uint8_t val, void *priv)
{
    ibm_tr_t *dev = (ibm_tr_t *) priv;
    uint32_t  offset;

    if (addr < dev->ram_base)
        return;

    offset = addr - dev->ram_base;
    if (offset >= dev->ram_size)
        return;

    dev->shared_ram[offset] = val;
}

static void
ibm_tr_ram_writew(uint32_t addr, uint16_t val, void *priv)
{
    ibm_tr_ram_writeb(addr, (uint8_t) val, priv);
    ibm_tr_ram_writeb(addr + 1, (uint8_t) (val >> 8), priv);
}

static void
ibm_tr_ram_writel(uint32_t addr, uint32_t val, void *priv)
{
    ibm_tr_ram_writew(addr, (uint16_t) val, priv);
    ibm_tr_ram_writew(addr + 2, (uint16_t) (val >> 16), priv);
}

static uint8_t
ibm_tr_readb(uint16_t port, void *priv)
{
    const ibm_tr_t *dev = (const ibm_tr_t *) priv;

    switch (port - dev->io_base) {
        case IBM_TR_IO_STATUS:
            return dev->regs.status;

        case IBM_TR_IO_CONTROL:
            return dev->regs.control;

        case IBM_TR_IO_SRB_LO:
            return (uint8_t) dev->mailbox.srb;

        case IBM_TR_IO_SRB_HI:
            return (uint8_t) (dev->mailbox.srb >> 8);

        case IBM_TR_IO_ATTENTION:
            return dev->regs.attention;

        default:
            return 0xff;
    }
}

static uint16_t
ibm_tr_readw(uint16_t port, void *priv)
{
    uint16_t value = ibm_tr_readb(port, priv);

    value |= (uint16_t) (ibm_tr_readb(port + 1, priv) << 8);
    return value;
}

static void
ibm_tr_writeb(uint16_t port, uint8_t val, void *priv)
{
    ibm_tr_t *dev = (ibm_tr_t *) priv;

    switch (port - dev->io_base) {
        case IBM_TR_IO_STATUS:
            dev->regs.command = val;
            break;

        case IBM_TR_IO_CONTROL:
            dev->regs.control = val;
            if (val & IBM_TR_CONTROL_RESET)
                ibm_tr_reset(dev);
            if (val & IBM_TR_CONTROL_ACK_IRQ) {
                ibm_tr_clear_irq(dev);
                ibm_tr_update_directory(dev);
            }
            break;

        case IBM_TR_IO_SRB_LO:
            dev->mailbox.srb = (uint16_t) ((dev->mailbox.srb & 0xff00) | val);
            break;

        case IBM_TR_IO_SRB_HI:
            dev->mailbox.srb = (uint16_t) ((dev->mailbox.srb & 0x00ff) | (val << 8));
            break;

        case IBM_TR_IO_ATTENTION:
            dev->regs.attention = val;
            if (val & IBM_TR_ATTN_CMD_IN_SRB)
                ibm_tr_run_srb(dev);
            if (val & IBM_TR_ATTN_RESP_IN_ASB)
                ibm_tr_run_asb(dev);
            break;

        default:
            break;
    }
}

static void
ibm_tr_writew(uint16_t port, uint16_t val, void *priv)
{
    ibm_tr_writeb(port, (uint8_t) val, priv);
    ibm_tr_writeb(port + 1, (uint8_t) (val >> 8), priv);
}

static void *
ibm_tr_init(const device_t *info)
{
    ibm_tr_t *dev;
    int       mac;

    (void) info;

    dev = calloc(1, sizeof(*dev));
    dev->log = log_open("IBMTR");
    dev->io_base    = IBM_TR_DEFAULT_IO_BASE;
    dev->ram_base   = (uint32_t) device_get_config_hex20("ram_addr");
    dev->ram_size   = (uint32_t) device_get_config_int("ram_size");
    dev->regs.irq   = (uint8_t) device_get_config_int("irq");
    dev->regs.ring_speed = (uint8_t) device_get_config_int("ring_speed");

    if ((dev->ram_size != 0x2000) && (dev->ram_size != 0x4000) &&
        (dev->ram_size != 0x8000) && (dev->ram_size != 0x10000))
        dev->ram_size = IBM_TR_SHARED_RAM_DEFAULT;

    dev->mac_ethernet[0] = 0x08;
    dev->mac_ethernet[1] = 0x00;
    dev->mac_ethernet[2] = 0x5a;

    mac = device_get_config_mac("mac", -1);
    if (mac & 0xff000000) {
        dev->mac_ethernet[3] = random_generate();
        dev->mac_ethernet[4] = random_generate();
        dev->mac_ethernet[5] = random_generate();
        mac = ((int) dev->mac_ethernet[3] << 16) |
              ((int) dev->mac_ethernet[4] << 8) |
              (int) dev->mac_ethernet[5];
        device_set_config_mac("mac", mac);
    } else {
        dev->mac_ethernet[3] = (uint8_t) (mac >> 16);
        dev->mac_ethernet[4] = (uint8_t) (mac >> 8);
        dev->mac_ethernet[5] = (uint8_t) mac;
    }

    ibm_tr_reverse_mac(dev->mac_token_ring, dev->mac_ethernet);
    ibm_tr_log(dev->log, "init: io=%#x ram_base=%#x ram_size=%#x irq=%u ring_speed=%u eth=%02x:%02x:%02x:%02x:%02x:%02x tr=%02x:%02x:%02x:%02x:%02x:%02x\n",
               dev->io_base, (unsigned int) dev->ram_base, (unsigned int) dev->ram_size,
               dev->regs.irq, dev->regs.ring_speed,
               dev->mac_ethernet[0], dev->mac_ethernet[1], dev->mac_ethernet[2],
               dev->mac_ethernet[3], dev->mac_ethernet[4], dev->mac_ethernet[5],
               dev->mac_token_ring[0], dev->mac_token_ring[1], dev->mac_token_ring[2],
               dev->mac_token_ring[3], dev->mac_token_ring[4], dev->mac_token_ring[5]);
    ibm_tr_reset(dev);
    dev->regs.ring_state = IBM_TR_RING_STATE_LOBE_OK;
    dev->regs.insertion_phase = IBM_TR_INSERT_PHASE_LOBE_MEDIA_TEST;
    ibm_tr_update_directory(dev);
    ibm_tr_write_bringup_block(dev);

    io_sethandler(dev->io_base, IBM_TR_IO_PORT_COUNT,
                  ibm_tr_readb, ibm_tr_readw, NULL,
                  ibm_tr_writeb, ibm_tr_writew, NULL, dev);

    mem_mapping_add(&dev->shared_ram_mapping, dev->ram_base, dev->ram_size,
                    ibm_tr_ram_readb, ibm_tr_ram_readw, ibm_tr_ram_readl,
                    ibm_tr_ram_writeb, ibm_tr_ram_writew, ibm_tr_ram_writel,
                    dev->shared_ram, MEM_MAPPING_EXTERNAL, dev);

    dev->netcard = network_attach(dev, dev->mac_ethernet, ibm_tr_receive, ibm_tr_set_link_state);

    return dev;
}

static void
ibm_tr_close(void *priv)
{
    ibm_tr_t *dev = (ibm_tr_t *) priv;

    ibm_tr_log(dev->log, "close\n");

    io_removehandler(dev->io_base, IBM_TR_IO_PORT_COUNT,
                     ibm_tr_readb, ibm_tr_readw, NULL,
                     ibm_tr_writeb, ibm_tr_writew, NULL, dev);
    mem_mapping_disable(&dev->shared_ram_mapping);
    netcard_close(dev->netcard);
    if (dev->log)
        log_close(dev->log);
    free(dev);
}

// clang-format off
static const device_config_t ibm_token_ring_config[] = {
    {
        .name           = "ram_addr",
        .description    = "Shared RAM address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = IBM_TR_DEFAULT_RAM_BASE,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "C8000H", .value = 0x0c8000 },
            { .description = "CC000H", .value = 0x0cc000 },
            { .description = "D0000H", .value = 0x0d0000 },
            { .description = "D4000H", .value = 0x0d4000 },
            { .description = "D8000H", .value = 0x0d8000 },
            { .description = "DC000H", .value = 0x0dc000 },
            { .description = ""                          }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "ram_size",
        .description    = "Shared RAM size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = IBM_TR_SHARED_RAM_DEFAULT,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "8 KB",  .value = 0x2000 },
            { .description = "16 KB", .value = 0x4000 },
            { .description = "32 KB", .value = 0x8000 },
            { .description = "64 KB", .value = 0x10000 },
            { .description = ""                       }
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
            { .description = "IRQ 2", .value = 2 },
            { .description = "IRQ 3", .value = 3 },
            { .description = "IRQ 6", .value = 6 },
            { .description = "IRQ 7", .value = 7 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "ring_speed",
        .description    = "Ring speed",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 16,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "4 Mbps",  .value = 4  },
            { .description = "16 Mbps", .value = 16 },
            { .description = ""                     }
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
// clang-format on

const device_t ibm_token_ring_device = {
    .name          = "IBM Token-Ring Network PC Adapter",
    .internal_name = "ibm_token_ring",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = ibm_tr_init,
    .close         = ibm_tr_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibm_token_ring_config
};

static bool
ibm_tr_is_snap_frame(const uint8_t *llc, size_t llc_len)
{
    return (llc_len >= IBM_TR_SNAP_HDR_LEN) && (llc[0] == IBM_LLC_SNAP_DSAP) &&
           (llc[1] == IBM_LLC_SNAP_SSAP) && (llc[2] == IBM_LLC_UI_CONTROL);
}

static size_t
ibm_tr_payload_len_after_snap(const uint8_t *llc, size_t llc_len)
{
    if (!ibm_tr_is_snap_frame(llc, llc_len))
        return llc_len;

    return llc_len - IBM_TR_SNAP_HDR_LEN;
}

static ibm_tr_result_t
ibm_tr_validate_length(size_t frame_len, size_t required)
{
    return (frame_len < required) ? IBM_TR_ERR_SHORT_FRAME : IBM_TR_OK;
}

uint8_t
ibm_tr_bit_reverse(uint8_t value)
{
    value = (uint8_t) (((value & 0xf0) >> 4) | ((value & 0x0f) << 4));
    value = (uint8_t) (((value & 0xcc) >> 2) | ((value & 0x33) << 2));
    value = (uint8_t) (((value & 0xaa) >> 1) | ((value & 0x55) << 1));

    return value;
}

void
ibm_tr_reverse_mac(uint8_t out[IBM_TR_MAC_ADDR_LEN], const uint8_t in[IBM_TR_MAC_ADDR_LEN])
{
    for (uint8_t i = 0; i < IBM_TR_MAC_ADDR_LEN; i++)
        out[i] = ibm_tr_bit_reverse(in[i]);
}

ibm_tr_result_t
ibm_tr_parse_frame(const uint8_t *frame, size_t frame_len, ibm_tr_frame_info_t *info)
{
    size_t offset = IBM_TR_HEADER_LEN;
    uint8_t rif_len;

    if (!frame || !info)
        return IBM_TR_ERR_SHORT_FRAME;

    if (ibm_tr_validate_length(frame_len, IBM_TR_HEADER_LEN + IBM_TR_LLC_HDR_LEN) != IBM_TR_OK)
        return IBM_TR_ERR_SHORT_FRAME;

    memset(info, 0, sizeof(*info));
    info->access_control = frame[0];
    info->frame_control  = frame[1];
    memcpy(info->dst, frame + 2, IBM_TR_MAC_ADDR_LEN);
    memcpy(info->src, frame + 2 + IBM_TR_MAC_ADDR_LEN, IBM_TR_MAC_ADDR_LEN);

    info->has_rif = (info->src[0] & 0x80) != 0;
    if (info->has_rif) {
        if (ibm_tr_validate_length(frame_len, offset + 2) != IBM_TR_OK)
            return IBM_TR_ERR_INVALID_RIF;

        rif_len = (uint8_t) (frame[offset] & 0x1f);
        if ((rif_len < 2) || (rif_len > IBM_TR_RIF_MAX_LEN) || ((rif_len & 1) != 0))
            return IBM_TR_ERR_INVALID_RIF;
        if (ibm_tr_validate_length(frame_len, offset + rif_len + IBM_TR_LLC_HDR_LEN) != IBM_TR_OK)
            return IBM_TR_ERR_INVALID_RIF;

        memcpy(info->rif, frame + offset, rif_len);
        info->rif_len = rif_len;
        offset += rif_len;
    }

    info->llc     = frame + offset;
    info->llc_len = frame_len - offset;

    return IBM_TR_OK;
}

ibm_tr_result_t
ibm_tr_translate_token_ring_to_ethernet(const uint8_t *tr_frame, size_t tr_frame_len, uint8_t *eth_frame, size_t eth_frame_cap, size_t *eth_frame_len)
{
    ibm_tr_frame_info_t info;
    ibm_tr_result_t     result;
    uint16_t            length_or_type;
    size_t              payload_offset;
    size_t              payload_len;
    size_t              out_len;

    if (!eth_frame || !eth_frame_len)
        return IBM_TR_ERR_BUFFER_TOO_SMALL;

    result = ibm_tr_parse_frame(tr_frame, tr_frame_len, &info);
    if (result != IBM_TR_OK)
        return result;

    if ((info.frame_control & 0xc0) != IBM_TR_FC_LLC_FRAME)
        return IBM_TR_ERR_UNSUPPORTED_FRAME;

    payload_offset = 0;
    payload_len    = info.llc_len;
    if (ibm_tr_is_snap_frame(info.llc, info.llc_len) &&
        (info.llc[3] == 0x00) && (info.llc[4] == 0x00) && (info.llc[5] == 0x00)) {
        payload_offset = IBM_TR_SNAP_HDR_LEN;
        payload_len    = ibm_tr_payload_len_after_snap(info.llc, info.llc_len);
        length_or_type = (uint16_t) ((info.llc[IBM_SNAP_PID_OFFSET] << 8) | info.llc[IBM_SNAP_PID_OFFSET + 1]);
    } else {
        if (payload_len > 1500)
            return IBM_TR_ERR_FRAME_TOO_LARGE;
        length_or_type = (uint16_t) payload_len;
    }

    if (payload_len > 1500)
        return IBM_TR_ERR_FRAME_TOO_LARGE;

    out_len = IBM_ETH_HEADER_LEN + payload_len;
    if (eth_frame_cap < out_len)
        return IBM_TR_ERR_BUFFER_TOO_SMALL;

    ibm_tr_reverse_mac(eth_frame, info.dst);
    ibm_tr_reverse_mac(eth_frame + IBM_TR_MAC_ADDR_LEN, info.src);
    eth_frame[12] = (uint8_t) (length_or_type >> 8);
    eth_frame[13] = (uint8_t) length_or_type;
    memcpy(eth_frame + IBM_ETH_HEADER_LEN, info.llc + payload_offset, payload_len);

    *eth_frame_len = out_len;
    return IBM_TR_OK;
}

ibm_tr_result_t
ibm_tr_translate_ethernet_to_token_ring(const uint8_t *eth_frame, size_t eth_frame_len, uint8_t *tr_frame, size_t tr_frame_cap, size_t *tr_frame_len)
{
    uint16_t length_or_type;
    size_t   payload_len;
    size_t   llc_len;
    size_t   out_len;
    bool     is_eth2;

    if (!tr_frame || !tr_frame_len)
        return IBM_TR_ERR_BUFFER_TOO_SMALL;

    if (ibm_tr_validate_length(eth_frame_len, IBM_ETH_HEADER_LEN) != IBM_TR_OK)
        return IBM_TR_ERR_SHORT_FRAME;

    length_or_type = (uint16_t) ((eth_frame[12] << 8) | eth_frame[13]);
    payload_len    = eth_frame_len - IBM_ETH_HEADER_LEN;
    is_eth2        = length_or_type >= 0x0600;

    if (!is_eth2 && (length_or_type > payload_len))
        return IBM_TR_ERR_SHORT_FRAME;

    llc_len = is_eth2 ? (IBM_TR_SNAP_HDR_LEN + payload_len) : payload_len;
    if (llc_len > 1500)
        return IBM_TR_ERR_FRAME_TOO_LARGE;

    out_len = IBM_TR_HEADER_LEN + llc_len;
    if (tr_frame_cap < out_len)
        return IBM_TR_ERR_BUFFER_TOO_SMALL;

    tr_frame[0] = IBM_TR_AC_DEFAULT;
    tr_frame[1] = IBM_TR_FC_LLC_FRAME;
    ibm_tr_reverse_mac(tr_frame + 2, eth_frame);
    ibm_tr_reverse_mac(tr_frame + 2 + IBM_TR_MAC_ADDR_LEN, eth_frame + IBM_TR_MAC_ADDR_LEN);

    if (is_eth2) {
        uint8_t *llc = tr_frame + IBM_TR_HEADER_LEN;

        llc[0] = IBM_LLC_SNAP_DSAP;
        llc[1] = IBM_LLC_SNAP_SSAP;
        llc[2] = IBM_LLC_UI_CONTROL;
        memset(llc + IBM_TR_LLC_HDR_LEN, 0x00, IBM_SNAP_OUI_LEN);
        llc[6] = eth_frame[12];
        llc[7] = eth_frame[13];
        memcpy(llc + IBM_TR_SNAP_HDR_LEN, eth_frame + IBM_ETH_HEADER_LEN, payload_len);
    } else {
        memcpy(tr_frame + IBM_TR_HEADER_LEN, eth_frame + IBM_ETH_HEADER_LEN, payload_len);
    }

    *tr_frame_len = out_len;
    return IBM_TR_OK;
}

const char *
ibm_tr_result_to_string(ibm_tr_result_t result)
{
    switch (result) {
        case IBM_TR_OK:
            return "ok";
        case IBM_TR_ERR_SHORT_FRAME:
            return "short frame";
        case IBM_TR_ERR_INVALID_RIF:
            return "invalid routing information field";
        case IBM_TR_ERR_UNSUPPORTED_FRAME:
            return "unsupported frame";
        case IBM_TR_ERR_BUFFER_TOO_SMALL:
            return "buffer too small";
        case IBM_TR_ERR_FRAME_TOO_LARGE:
            return "frame too large";
        default:
            return "unknown error";
    }
}