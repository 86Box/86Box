/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Western Digital WD33C93 SCSI controller.
 *          The register model follows the WD33C93A data sheet and MAME's
 *          wd33c9x.cpp.
 *
 * Authors: Tyson Smith, <tyfighter@gmail.com>
 *          WNT50, <wnt2195@gmail.com>
 *
 *          Copyright 2018-2026 Tyson Smith.
 *          Copyright 2026 WNT50.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_wd33c93.h>

#ifdef ENABLE_WD33C93_LOG
int wd33c93_do_log = ENABLE_WD33C93_LOG;

static void
wd33c93_log(const char *fmt, ...)
{
    va_list ap;

    if (wd33c93_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define wd33c93_log(fmt, ...)
#endif

static void
wd33c93_set_irq(wd33c93_t *wd, int state)
{
    wd33c93_log("[WD33C93] IRQ %s\n", state ? "assert" : "deassert");

    if (wd->irq_ena != NULL)
        wd->irq_ena(wd->priv, state);
}

static void
wd33c93_fifo_reset(wd33c93_t *wd)
{
    memset(wd->fifo, 0x00, sizeof(wd->fifo));
    wd->fifo_r   = 0;
    wd->fifo_w   = 0;
    wd->fifo_cnt = 0;
}

static int
wd33c93_fifo_empty(const wd33c93_t *wd)
{
    return (wd->fifo_cnt == 0);
}

static int
wd33c93_fifo_full(const wd33c93_t *wd)
{
    return (wd->fifo_cnt >= WD_FIFO_SIZE);
}

static void
wd33c93_fifo_push(wd33c93_t *wd, uint8_t val)
{
    if (wd33c93_fifo_full(wd))
        return;

    wd->fifo[wd->fifo_w] = val;
    wd->fifo_w            = (wd->fifo_w + 1) % WD_FIFO_SIZE;
    wd->fifo_cnt++;
}

static uint8_t
wd33c93_fifo_pop(wd33c93_t *wd)
{
    uint8_t ret;

    if (wd33c93_fifo_empty(wd))
        return 0x00;

    ret         = wd->fifo[wd->fifo_r];
    wd->fifo_r  = (wd->fifo_r + 1) % WD_FIFO_SIZE;
    wd->fifo_cnt--;

    return ret;
}

static void
wd33c93_csr_reset(wd33c93_t *wd)
{
    memset(wd->csr_queue, 0x00, sizeof(wd->csr_queue));
    wd->csr_r   = 0;
    wd->csr_w   = 0;
    wd->csr_cnt = 0;
}

static int
wd33c93_csr_empty(const wd33c93_t *wd)
{
    return (wd->csr_cnt == 0);
}

static int
wd33c93_csr_full(const wd33c93_t *wd)
{
    return (wd->csr_cnt >= WD_CSR_SIZE);
}

static void
wd33c93_csr_push(wd33c93_t *wd, uint8_t status)
{
    uint8_t last;

    if (wd33c93_csr_full(wd))
        return;

    /* Drop a status that would duplicate the one already at the tail. */
    if (wd->csr_cnt > 0) {
        last = wd->csr_queue[(wd->csr_w + WD_CSR_SIZE - 1) % WD_CSR_SIZE];
        if (last == status)
            return;
    }

    wd->csr_queue[wd->csr_w] = status;
    wd->csr_w                 = (wd->csr_w + 1) % WD_CSR_SIZE;
    wd->csr_cnt++;

    wd33c93_log("[WD33C93] CSR queue <- %02X (%d pending)\n", status, wd->csr_cnt);
}

static uint8_t
wd33c93_csr_pop(wd33c93_t *wd)
{
    uint8_t ret;

    if (wd33c93_csr_empty(wd))
        return 0x00;

    ret         = wd->csr_queue[wd->csr_r];
    wd->csr_r   = (wd->csr_r + 1) % WD_CSR_SIZE;
    wd->csr_cnt--;

    return ret;
}

/* Move the next queued interrupt reason into the CSR. Reading the CSR
   clears INT and pops the next entry, which is what the drivers expect. */
static void
wd33c93_update_irq(wd33c93_t *wd)
{
    if (wd->regs[WD_AUX_STATUS] & WD_ASR_INT) {
        wd->regs[WD_AUX_STATUS] &= ~WD_ASR_INT;
        wd33c93_set_irq(wd, 0);
    }

    if (!wd33c93_csr_empty(wd)) {
        wd->regs[WD_SCSI_STATUS] = wd33c93_csr_pop(wd);
        wd->regs[WD_AUX_STATUS] |= WD_ASR_INT;
        wd33c93_set_irq(wd, 1);
    }
}

static void
wd33c93_decrement_count(wd33c93_t *wd)
{
    if (wd->xfer_count == 0)
        return;

    wd->xfer_count--;

    /* The counter is visible to the host: drivers read the three registers
       back after an interrupt to work out how many bytes actually moved. */
    wd->regs[WD_TRANSFER_COUNT_MSB] = (wd->xfer_count >> 16) & 0xff;
    wd->regs[WD_TRANSFER_COUNT_MID] = (wd->xfer_count >> 8) & 0xff;
    wd->regs[WD_TRANSFER_COUNT_LSB] = wd->xfer_count & 0xff;
}

static void wd33c93_finish(wd33c93_t *wd);
static void wd33c93_fill_fifo(wd33c93_t *wd);
static void wd33c93_flush_out(wd33c93_t *wd);
static void wd33c93_dma_in(wd33c93_t *wd);
static void wd33c93_dma_out(wd33c93_t *wd);
static void wd33c93_run(wd33c93_t *wd, uint8_t cc);

static void
wd33c93_set_tx(wd33c93_t *wd, uint8_t mode)
{
    wd->tx_mode         = mode;
    wd->scsibus.tx_mode = mode;
}

static void
wd33c93_load_count(wd33c93_t *wd)
{
    if (wd->regs[WD_COMMAND] & WD_CMD_SBT) {
        wd->xfer_count = 1;
        return;
    }

    wd->xfer_count = ((uint32_t) wd->regs[WD_TRANSFER_COUNT_MSB] << 16) |
                     ((uint32_t) wd->regs[WD_TRANSFER_COUNT_MID] << 8) |
                     (uint32_t) wd->regs[WD_TRANSFER_COUNT_LSB];

    if (wd->xfer_count == 0)
        wd->xfer_count = 1;
}

/* Advance the command state machine. Only the "finished" transition
   is implemented so far; the select/transfer states are added with
   command support in a later step. */
static void
wd33c93_step(wd33c93_t *wd)
{
    switch (wd->state) {
        case WD_STATE_FINISHED:
            wd->state                = WD_STATE_IDLE;
            wd->regs[WD_AUX_STATUS] &= ~(WD_ASR_CIP | WD_ASR_BSY);
            wd33c93_update_irq(wd);
            break;

        default:
            break;
    }
}

static int
wd33c93_pump(wd33c93_t *wd, int count)
{
    int bus = wd->scsibus.bus_out;
    int i;

    for (i = 0; i < count; i++)
        bus = scsi_bus_read(&wd->scsibus);

    return bus;
}

static int
wd33c93_wait_req(wd33c93_t *wd, int count)
{
    int bus = wd->scsibus.bus_out;
    int i;

    for (i = 0; i < count; i++) {
        if (bus & BUS_REQ)
            break;
        bus = scsi_bus_read(&wd->scsibus);
    }

    return bus;
}

/* One ACK pulse. The engine latches the bus on the rising edge,
   and the clear_req handling re-asserts REQ for the next byte. */
static void
wd33c93_ack_pulse(wd33c93_t *wd, uint8_t data)
{
    scsi_bus_update(&wd->scsibus, BUS_SETDATA(data) | BUS_ACK);
    scsi_bus_update(&wd->scsibus, BUS_SETDATA(data));

    if (wd->scsibus.data_wait & 1) {
        wd->scsibus.clear_req  = 3;
        wd->scsibus.data_wait &= ~1;
    }
}

static uint8_t
wd33c93_cdb_length(wd33c93_t *wd)
{
    if ((wd->regs[WD_OWN_ID] & WD_OWN_ID_EAF) &&
        (wd->cdbsize & WD_OWN_ID_CDB_SIZE))
        return wd->cdbsize & WD_OWN_ID_CDB_SIZE;

    switch (wd->regs[WD_CDB1] >> 5) {
        case 1: /* group 1: 10-byte CDB */
        case 2: /* group 2: 10-byte CDB (e.g. READ TOC, 0x43) */
            return 10;
        case 4: /* group 4: 16-byte CDB */
            return 16;
        case 5: /* group 5: 12-byte CDB */
            return 12;
        default: /* group 0: 6-byte CDB */
            return 6;
    }
}

static int
wd33c93_select(wd33c93_t *wd, uint8_t cc)
{
    uint8_t dest = wd->regs[WD_DEST_ID] & WD_DEST_MASK;
    int     bus;

    wd33c93_log("[WD33C93] select id=%d cmd=%02X\n", dest, cc);

    /* A previous command can leave engine parked mid-phase (for example
       a message or status byte that was never delivered). Release the bus,
       if the engine did not come back to idle, force it there: otherwise the
       new selection is ignored and the stale phase is mistaken for an answer
       from the target. */
    scsi_bus_update(&wd->scsibus, 0);
    (void) wd33c93_pump(wd, 64);

    if (wd->scsibus.state != STATE_IDLE) {
        scsi_bus_update(&wd->scsibus, BUS_ARB);
        wd->scsibus.bus_out = 0;
    }

    wd->regs[WD_AUX_STATUS] |= WD_ASR_BSY;

    /* Only the target ID goes into the data lines: the engine picks the
       lowest set bit as the target, so the initiator ID must not be here. */
    scsi_bus_update(&wd->scsibus,
                    BUS_SEL | BUS_SETDATA(1 << dest) |
                        (((cc == WD_CMD_SELECT_ATN_TRANSFER) ||
                          (cc == WD_CMD_SELECT_ATN)) ? BUS_ATN : 0));
    bus = scsi_bus_read(&wd->scsibus);

    if (!(bus & BUS_BSY)) {
        wd33c93_log("[WD33C93] select: no BSY (bus %04X)\n", bus);
        return 0;
    }

    /* Both Select-with-ATN commands leave ATN asserted after SEL is
       released: the data sheet has the chip negate ATN only once the
       Identify message has gone out. The engine needs a few bus cycles
       before it raises REQ for that message. */
    scsi_bus_update(&wd->scsibus,
                    ((cc == WD_CMD_SELECT_ATN) ||
                     (cc == WD_CMD_SELECT_ATN_TRANSFER)) ? BUS_ATN : 0);
    bus = scsi_bus_read(&wd->scsibus);

    if ((cc == WD_CMD_SELECT_ATN) || (cc == WD_CMD_SELECT_ATN_TRANSFER))
        bus = wd33c93_wait_req(wd, 8);

    wd33c93_log("[WD33C93] select: bus %04X\n", bus);

    return (bus & BUS_REQ) ? 1 : 0;
}

/* A Select-With-ATN-And-Transfer command makes the chip send the Identify
   message itself: "1r000ttt", where r comes from the Enable Reselect bit of
   the Source ID register and ttt is the LUN in the Target LUN register. */
static int
wd33c93_send_identify(wd33c93_t *wd)
{
    uint8_t identify;
    int     bus;

    bus = wd33c93_wait_req(wd, 8);

    if ((bus & SCSI_PHASE_MESSAGE_IN) != SCSI_PHASE_MESSAGE_OUT)
        return 0;

    identify = 0x80 | (wd->regs[WD_TARGET_LUN] & WD_LUN_MASK);

    if (wd->regs[WD_SOURCE_ID] & WD_SRC_ER)
        identify |= 0x40;

    wd33c93_log("[WD33C93] identify %02X\n", identify);

    wd33c93_ack_pulse(wd, identify);
    bus = wd33c93_pump(wd, 8);

    wd->regs[WD_COMMAND_PHASE] = WD_PHASE_IDENTIFY;

    return 1;
}

static int
wd33c93_send_cdb(wd33c93_t *wd)
{
    uint8_t len = wd33c93_cdb_length(wd);
    uint8_t i;
    int     bus;

    wd33c93_log("[WD33C93] cdb %02X %02X %02X %02X %02X %02X %02X %02X "
                "%02X %02X (%d bytes), count %06X\n",
                wd->regs[WD_CDB1], wd->regs[WD_CDB1 + 1], wd->regs[WD_CDB1 + 2],
                wd->regs[WD_CDB1 + 3], wd->regs[WD_CDB1 + 4], wd->regs[WD_CDB1 + 5],
                wd->regs[WD_CDB1 + 6], wd->regs[WD_CDB1 + 7], wd->regs[WD_CDB1 + 8],
                wd->regs[WD_CDB1 + 9], len, wd->xfer_count);

    for (i = 0; i < len; i++) {
        bus = wd33c93_wait_req(wd, 8);
        if (!(bus & BUS_REQ))
            return 0;

        wd33c93_ack_pulse(wd, wd->regs[WD_CDB1 + i]);
        bus = wd33c93_pump(wd, 4);
    }

    wd->regs[WD_COMMAND_PHASE] = WD_PHASE_CDB_BYTES_0 + (len - 1);

    return 1;
}

static void
wd33c93_dma_drain(wd33c93_t *wd)
{
    uint8_t val;

    if ((wd->tx_mode == WD_TX_PIO) || (wd->dma_write == NULL))
        return;

    while (!wd33c93_fifo_empty(wd)) {
        val = wd->fifo[wd->fifo_r];
        if (wd->dma_write(wd->priv, val) != 0)
            break;
        (void) wd33c93_fifo_pop(wd);
    }
}

static void
wd33c93_fill_fifo(wd33c93_t *wd)
{
    int bus;
    int guard = WD_FIFO_SIZE + 4;

    if (wd->state != WD_STATE_TRANSFER)
        return;

    bus = wd->scsibus.bus_out;

    while ((guard-- > 0) && !wd33c93_fifo_full(wd)) {
        if ((bus & SCSI_PHASE_MESSAGE_IN) != SCSI_PHASE_DATA_IN)
            break;

        wd33c93_fifo_push(wd, BUS_GETDATA(bus));
        wd->data_pos++;
        wd33c93_decrement_count(wd);

        wd33c93_ack_pulse(wd, BUS_GETDATA(bus));
        bus = wd33c93_wait_req(wd, 8);
    }

    wd33c93_dma_drain(wd);

    /* Once the target has left the data phase, a byte that the DMA channel
       refused can never be delivered, so drop it and end the command rather
       than leaving CIP set forever. PIO keeps the FIFO for the host to read. */
    if ((wd->tx_mode != WD_TX_PIO) &&
        ((bus & SCSI_PHASE_MESSAGE_IN) != SCSI_PHASE_DATA_IN))
        wd33c93_fifo_reset(wd);

    if (!wd33c93_fifo_empty(wd))
        wd->regs[WD_AUX_STATUS] |= WD_ASR_DBR;
    else if ((bus & SCSI_PHASE_MESSAGE_IN) != SCSI_PHASE_DATA_IN)
        wd33c93_finish(wd);
}

static void
wd33c93_flush_out(wd33c93_t *wd)
{
    int bus;
    int guard = WD_FIFO_SIZE + 4;

    if (wd->state != WD_STATE_TRANSFER)
        return;

    bus = wd33c93_wait_req(wd, 8);

    while ((guard-- > 0) && !wd33c93_fifo_empty(wd)) {
        if ((bus & SCSI_PHASE_MESSAGE_IN) != SCSI_PHASE_DATA_OUT)
            break;

        wd33c93_ack_pulse(wd, wd33c93_fifo_pop(wd));
        bus = wd33c93_wait_req(wd, 8);
    }

    if (wd33c93_fifo_empty(wd)) {
        if ((bus & SCSI_PHASE_MESSAGE_IN) == SCSI_PHASE_DATA_OUT)
            wd->regs[WD_AUX_STATUS] |= WD_ASR_DBR;
        else
            wd33c93_finish(wd);
    }
}

/* Feed the DMA channel one byte at a time: the byte is only acknowledged
   once the channel has taken it, so bytes are never pulled off from the bus
   and then dropped because the host had not programmed the channel yet. */
static void
wd33c93_dma_in(wd33c93_t *wd)
{
    uint32_t start = wd->xfer_count;
    int      bus   = wd->scsibus.bus_out;

    while (wd->state == WD_STATE_TRANSFER) {
        bus = wd33c93_wait_req(wd, 8);

        if (!(bus & BUS_REQ))
            break;

        if ((bus & SCSI_PHASE_MESSAGE_IN) != SCSI_PHASE_DATA_IN)
            break;

        if (wd->dma_write(wd->priv, BUS_GETDATA(bus)) != 0)
            break; /* Not ready: leave the byte on the bus and retry later. */

        wd33c93_decrement_count(wd);
        wd33c93_ack_pulse(wd, BUS_GETDATA(bus));
    }

    if (start != wd->xfer_count)
        wd33c93_log("[WD33C93] dma in: %u of %u bytes moved\n",
                    (unsigned) (start - wd->xfer_count), (unsigned) start);

    if ((wd->state == WD_STATE_TRANSFER) &&
        ((bus & SCSI_PHASE_MESSAGE_IN) != SCSI_PHASE_DATA_IN))
        wd33c93_finish(wd);
}

static void
wd33c93_dma_out(wd33c93_t *wd)
{
    uint8_t val;
    int     got;
    int     guard = 8192;

    if ((wd->tx_mode == WD_TX_PIO) || (wd->dma_read == NULL)) {
        wd->regs[WD_AUX_STATUS] |= WD_ASR_DBR;
        return;
    }

    while ((guard-- > 0) && (wd->state == WD_STATE_TRANSFER) &&
           ((wd->scsibus.bus_out & SCSI_PHASE_MESSAGE_IN) == SCSI_PHASE_DATA_OUT)) {
        got = 0;

        while (!wd33c93_fifo_full(wd)) {
            if (wd->dma_read(wd->priv, &val) != 0)
                break;
            wd33c93_fifo_push(wd, val);
            wd33c93_decrement_count(wd);
            got = 1;
        }

        if (!got)
            break;

        wd33c93_flush_out(wd);
    }
}

static void
wd33c93_finish(wd33c93_t *wd)
{
    int bus;

    wd->regs[WD_AUX_STATUS] &= ~WD_ASR_DBR;

    bus = wd33c93_wait_req(wd, 8);

    if ((bus & SCSI_PHASE_MESSAGE_IN) == SCSI_PHASE_STATUS) {
        wd->regs[WD_COMMAND_PHASE] = WD_PHASE_RECEIVE_STATUS;
        wd->status                 = BUS_GETDATA(bus);

        /* The Target LUN register holds the status byte received by
           a Select-And-Transfer command; drivers read it back to
           find out how the target ended the request. */
        wd->regs[WD_TARGET_LUN] = wd->status;

        wd33c93_ack_pulse(wd, 0x00);
        bus = wd33c93_pump(wd, 8);

        if ((bus & SCSI_PHASE_MESSAGE_IN) == SCSI_PHASE_MESSAGE_IN) {
            wd33c93_ack_pulse(wd, 0x00);
            bus = wd33c93_pump(wd, 8);
        }
    }

    wd->regs[WD_COMMAND_PHASE] = WD_PHASE_COMMAND_COMPLETE;
    wd->state                  = WD_STATE_FINISHED;

    wd33c93_log("[WD33C93] finish, status %02X\n", wd->status);

    wd33c93_csr_push(wd, WD_CSR_SELECT_TRANSFER_SUCCESS);
    wd33c93_step(wd);
}

/* MCI (Message/phase Code Information) as reported in bits 2-0 of a
   successful-completion CSR: the SCSI phase the target is asking for. */
static uint8_t
wd33c93_mci(int bus)
{
    switch (bus & SCSI_PHASE_MESSAGE_IN) {
        case SCSI_PHASE_DATA_IN:
            return 0x01;
        case SCSI_PHASE_COMMAND:
            return 0x02;
        case SCSI_PHASE_STATUS:
            return 0x03;
        case SCSI_PHASE_MESSAGE_OUT:
            return 0x06;
        case SCSI_PHASE_MESSAGE_IN:
            return 0x07;
        default:
            return 0x00; /* Data Out. */
    }
}

/* A Select-And-Transfer command ends at the first phase boundary: the new
   phase goes out in the CSR and the host is expected to move the bytes with
   a later Transfer Info command. Ending the command clears CIP, which is
   what the driver polls for before it will service the data phase. */
static void
wd33c93_pause(wd33c93_t *wd, int bus)
{
    uint8_t mci = wd33c93_mci(bus);

    wd->state = WD_STATE_IDLE;
    wd->regs[WD_AUX_STATUS] &= ~WD_ASR_CIP;

    wd33c93_log("[WD33C93] paused, phase %02X, CSR %02X\n",
                bus & SCSI_PHASE_MESSAGE_IN, WD_CSR_TRANSFER_SUCCESS | mci);

    wd33c93_csr_push(wd, WD_CSR_TRANSFER_SUCCESS | mci);
    wd33c93_update_irq(wd);
}

/* Select commands without a transfer end as soon as the target answers:
   the chip reports "select completed" and leaves the bus connected, and
   the host drives the next phase with a Transfer Information command. */
static void
wd33c93_select_only(wd33c93_t *wd, uint8_t cc)
{
    wd->regs[WD_AUX_STATUS] |= WD_ASR_CIP | WD_ASR_BSY;

    if (!wd33c93_select(wd, cc)) {
        wd->regs[WD_AUX_STATUS] &= ~(WD_ASR_BSY | WD_ASR_CIP);
        wd33c93_csr_push(wd, WD_CSR_SELECTION_TIMEOUT);
        wd33c93_update_irq(wd);
        wd->state = WD_STATE_IDLE;
        return;
    }

    wd->regs[WD_AUX_STATUS] &= ~WD_ASR_CIP;
    wd->state                  = WD_STATE_IDLE;
    wd->regs[WD_COMMAND_PHASE] = WD_PHASE_SELECTED;

    wd33c93_csr_push(wd, WD_CSR_SELECT_SUCCESS);
    wd33c93_update_irq(wd);
}

static void
wd33c93_run(wd33c93_t *wd, uint8_t cc)
{
    int bus;

    wd33c93_load_count(wd);
    wd->regs[WD_AUX_STATUS] |= WD_ASR_CIP | WD_ASR_BSY;

    if (!wd33c93_select(wd, cc)) {
        wd->regs[WD_AUX_STATUS] &= ~(WD_ASR_BSY | WD_ASR_CIP);
        wd33c93_csr_push(wd, WD_CSR_SELECTION_TIMEOUT);
        wd33c93_update_irq(wd);
        wd->state = WD_STATE_IDLE;
        return;
    }

    wd->regs[WD_COMMAND_PHASE] = WD_PHASE_SELECTED;

    if ((cc == WD_CMD_SELECT_ATN_TRANSFER) && !wd33c93_send_identify(wd)) {
        wd->regs[WD_AUX_STATUS] &= ~(WD_ASR_BSY | WD_ASR_CIP);
        wd33c93_csr_push(wd, WD_CSR_UNEXPECTED_PHASE);
        wd33c93_update_irq(wd);
        wd->state = WD_STATE_IDLE;
        return;
    }

    if (!wd33c93_send_cdb(wd)) {
        wd->regs[WD_AUX_STATUS] &= ~(WD_ASR_BSY | WD_ASR_CIP);
        wd33c93_csr_push(wd, WD_CSR_UNEXPECTED_PHASE);
        wd33c93_update_irq(wd);
        wd->state = WD_STATE_IDLE;
        return;
    }

    /* Enter the data (or status) phase selected by the target. */
    (void) wd33c93_pump(wd, 4);
    bus = wd33c93_wait_req(wd, 8);

    switch (bus & SCSI_PHASE_MESSAGE_IN) {
        case SCSI_PHASE_DATA_IN:
            if (wd->tx_mode == WD_TX_PIO) {
                wd33c93_pause(wd, bus);
            } else {
                /* The host programs the DMA channel after this command
                   returns and then starts it with base+4/base+5, so
                   only set the phase up here. */
                wd->state = WD_STATE_TRANSFER;
                wd33c93_set_tx(wd, DMA_IN_TX_BUS);
            }
            break;

        case SCSI_PHASE_DATA_OUT:
            if (wd->tx_mode == WD_TX_PIO) {
                wd33c93_pause(wd, bus);
            } else {
                wd->state = WD_STATE_TRANSFER;
                wd33c93_set_tx(wd, DMA_OUT_TX_BUS);
            }
            break;

        default:
            wd->state = WD_STATE_TRANSFER;
            wd33c93_finish(wd);
            break;
    }
}

/* Transfer Information continues the phase the target is currently asking
   for, using the transfer counter as the byte limit. The host programs
   the counter and, in DMA mode, the DMA channel before issuing it. */
static void
wd33c93_transfer_info(wd33c93_t *wd)
{
    int bus;

    wd33c93_load_count(wd);
    wd->regs[WD_AUX_STATUS] |= WD_ASR_CIP;

    /* DBR is also reset whenever a Transfer Info command is issued. */
    wd->regs[WD_AUX_STATUS] &= ~WD_ASR_DBR;

    bus = wd->scsibus.bus_out;

    wd33c93_log("[WD33C93] transfer info, phase %02X, count %06X, %s\n",
                bus & SCSI_PHASE_MESSAGE_IN, wd->xfer_count,
                (wd->tx_mode == WD_TX_PIO) ? "PIO" : "DMA");

    switch (bus & SCSI_PHASE_MESSAGE_IN) {
        case SCSI_PHASE_MESSAGE_OUT:
            /* Messages are short and always move through the DATA register:
               raise DBR and let the host write the byte out. */
            wd->state = WD_STATE_TRANSFER;
            wd33c93_set_tx(wd, WD_TX_PIO);
            wd->regs[WD_AUX_STATUS] |= WD_ASR_DBR;
            break;

        case SCSI_PHASE_DATA_IN:
            wd->state = WD_STATE_TRANSFER;
            if (wd->tx_mode == WD_TX_PIO) {
                wd33c93_set_tx(wd, WD_TX_PIO);
                wd33c93_fill_fifo(wd);
            } else {
                wd33c93_set_tx(wd, DMA_IN_TX_BUS);
                wd33c93_dma_in(wd);
            }
            break;

        case SCSI_PHASE_DATA_OUT:
            wd->state = WD_STATE_TRANSFER;
            if (wd->tx_mode == WD_TX_PIO) {
                wd33c93_set_tx(wd, WD_TX_PIO);
                wd->regs[WD_AUX_STATUS] |= WD_ASR_DBR;
            } else {
                wd33c93_set_tx(wd, DMA_OUT_TX_BUS);
                wd33c93_dma_out(wd);
            }
            break;

        default:
            wd->state = WD_STATE_TRANSFER;
            wd33c93_finish(wd);
            break;
    }
}

static void
wd33c93_start_command(wd33c93_t *wd)
{
    uint8_t cc = wd->regs[WD_COMMAND] & WD_CMD_MASK;

    wd33c93_log("[WD33C93] command %02X\n", cc);

    wd->cmd = cc;

    switch (cc) {
        case WD_CMD_RESET:
            wd->regs[WD_OWN_ID] = wd->cdbsize;
            memset(&wd->regs[WD_CONTROL], 0x00, WD_SOURCE_ID - WD_CONTROL);
            wd->regs[WD_COMMAND]     = 0x00;
            wd->regs[WD_AUX_STATUS] &= ~WD_ASR_DBR;
            wd->own_id               = wd->regs[WD_OWN_ID] & WD_OWN_ID_MASK;
            wd33c93_set_tx(wd, WD_TX_PIO);
            wd33c93_fifo_reset(wd);
            wd33c93_csr_reset(wd);
            wd33c93_update_irq(wd);
            wd->state = WD_STATE_FINISHED;
            wd33c93_csr_push(wd, (wd->regs[WD_OWN_ID] & WD_OWN_ID_EAF) ?
                                     WD_CSR_RESET_EAF : WD_CSR_RESET);
            wd33c93_step(wd);
            break;

        case WD_CMD_SELECT_ATN_TRANSFER:
        case WD_CMD_SELECT_TRANSFER:
            wd33c93_run(wd, cc);
            break;

        case WD_CMD_SELECT_ATN:
        case WD_CMD_SELECT:
            wd33c93_select_only(wd, cc);
            break;

        case WD_CMD_TRANSFER_INFO:
            wd33c93_transfer_info(wd);
            break;

        case WD_CMD_DISCONNECT:
            /* Level I command: release the bus. It does not complete with
               an interrupt unless the host asked for one. */
            wd->regs[WD_AUX_STATUS] &= ~(WD_ASR_BSY | WD_ASR_CIP | WD_ASR_DBR);
            scsi_bus_update(&wd->scsibus, 0);
            (void) wd33c93_pump(wd, 8);
            wd->state = WD_STATE_IDLE;
            break;

        default:
            wd->regs[WD_AUX_STATUS] &= ~(WD_ASR_CIP | WD_ASR_BSY);
            wd33c93_log("[WD33C93] command %02X is not implemented\n", cc);
            wd33c93_csr_push(wd, WD_CSR_INVALID_COMMAND);
            wd33c93_update_irq(wd);
            break;
    }
}

static uint8_t
wd33c93_reg_read(wd33c93_t *wd, uint8_t reg)
{
    uint8_t ret = 0x00;

    reg &= (WD_REG_COUNT - 1);

    switch (reg) {
        case WD_OWN_ID:
            ret = wd->cdbsize;
            break;

        case WD_DATA:
            if (wd->regs[WD_AUX_STATUS] & WD_ASR_DBR) {
                ret = wd33c93_fifo_pop(wd);
                if (wd33c93_fifo_empty(wd))
                    wd->regs[WD_AUX_STATUS] &= ~WD_ASR_DBR;
                wd33c93_fill_fifo(wd);
            }
            /* The data register does not advance the indirect address. */
            return ret;

        case WD_SCSI_STATUS:
            ret = wd->regs[WD_SCSI_STATUS];
            wd33c93_update_irq(wd);
            break;

        default:
            ret = wd->regs[reg];
            break;
    }

    /* Command, data and auxiliary status do not advance the address. */
    if ((reg != WD_COMMAND) && (reg != WD_AUX_STATUS))
        wd->addr = (reg + 1) & (WD_REG_COUNT - 1);

    return ret;
}

static void
wd33c93_reg_write(wd33c93_t *wd, uint8_t reg, uint8_t val)
{
    reg &= (WD_REG_COUNT - 1);

    switch (reg) {
        case WD_SCSI_STATUS:
        case WD_QUEUE_TAG:
        case WD_INVALID_1B:
        case WD_INVALID_1C:
        case WD_INVALID_1D:
        case WD_INVALID_1E:
        case WD_AUX_STATUS:
            /* Read-only registers. */
            return;

        case WD_COMMAND:
            wd->regs[WD_COMMAND] = val;
            wd33c93_start_command(wd);
            return;

        case WD_DATA:
            if (!(wd->regs[WD_AUX_STATUS] & WD_ASR_DBR))
                return;

            /* Message-out bytes go straight onto the bus; the target then
               switches to the phase that follows the message. */
            if ((wd->state == WD_STATE_TRANSFER) &&
                ((wd->scsibus.bus_out & SCSI_PHASE_MESSAGE_IN) ==
                 SCSI_PHASE_MESSAGE_OUT)) {
                wd->regs[WD_AUX_STATUS] &= ~WD_ASR_DBR;
                wd33c93_log("[WD33C93] message out %02X\n", val);
                wd33c93_ack_pulse(wd, val);
                (void) wd33c93_pump(wd, 8);
                return;
            }

            wd->regs[WD_AUX_STATUS] &= ~WD_ASR_DBR;
            wd33c93_fifo_push(wd, val);
            wd33c93_decrement_count(wd);
            wd33c93_flush_out(wd);
            return;

        case WD_CONTROL:
            wd->regs[WD_CONTROL] = val;
            /* This driver moves data with DMA: it sets DM2 (0x80) together
               with the host-speed and parity bits and then programs the DMA
               channel before issuing Transfer Information. DM1/DM0 select
               the burst/WD-bus variants of the same path. */
            wd33c93_set_tx(wd, (val & WD_CONTROL_DM) ? WD_TX_DMA_IN : WD_TX_PIO);
            break;

        case WD_OWN_ID:
            wd->cdbsize = val;
            break;

        default:
            wd->regs[reg] = val;
            break;
    }

    wd->addr = (reg + 1) & (WD_REG_COUNT - 1);
}

void
wd33c93_reset(wd33c93_t *wd)
{
    memset(wd->regs, 0x00, sizeof(wd->regs));
    wd->state      = WD_STATE_IDLE;
    wd->addr       = 0x00;
    wd->cmd        = 0x00;
    wd->own_id     = 0x00;
    wd->cdbsize    = 0x00;
    wd->xfer_count = 0;
    wd->data_pos   = 0;
    wd->status     = 0;

    wd33c93_set_tx(wd, WD_TX_PIO);

    wd33c93_fifo_reset(wd);
    wd33c93_csr_reset(wd);
    wd33c93_set_irq(wd, 0);

    /* A hardware reset raises the reset status interrupt. */
    wd->state = WD_STATE_FINISHED;
    wd33c93_csr_push(wd, WD_CSR_RESET);
    wd33c93_step(wd);
}

/* The DMA channel is programmed by the host and may not be ready at the
   instant the command is issued, so a FIFO that cannot be drained is not an
   error: keep the bytes and try again on the next register access, which is
   what the host does while it polls the ASR. */
void
wd33c93_service_dma(wd33c93_t *wd)
{
    if (wd->state != WD_STATE_TRANSFER)
        return;

    if (wd->tx_mode == WD_TX_PIO)
        return;

    switch (wd->scsibus.bus_out & SCSI_PHASE_MESSAGE_IN) {
        case SCSI_PHASE_DATA_IN:
            wd33c93_dma_in(wd);
            break;

        case SCSI_PHASE_DATA_OUT:
            wd33c93_dma_out(wd);
            break;

        default:
            break;
    }
}

uint8_t
wd33c93_port_read(wd33c93_t *wd, uint16_t off)
{
    /* The host polls the status register while a command runs, so use each
       access as an extra chance to feed the DMA channel: the timer alone
       can be stopped by the bus engine going idle. */
    wd33c93_service_dma(wd);

    switch (wd->iface) {
        case WD_IFACE_DIRECT:
            return wd33c93_reg_read(wd, (uint8_t) off);

        case WD_IFACE_INDIRECT:
        case WD_IFACE_INDIRECT_SEPARATE:
        default:
            if (off == 0)
                return wd->regs[WD_AUX_STATUS];
            return wd33c93_reg_read(wd, wd->addr);
    }
}

void
wd33c93_port_write(wd33c93_t *wd, uint16_t off, uint8_t val)
{
    switch (wd->iface) {
        case WD_IFACE_DIRECT:
            wd33c93_reg_write(wd, (uint8_t) off, val);
            break;

        case WD_IFACE_INDIRECT:
        case WD_IFACE_INDIRECT_SEPARATE:
        default:
            if (off == 0)
                wd->addr = val & (WD_REG_COUNT - 1);
            else
                wd33c93_reg_write(wd, wd->addr, val);
            break;
    }
}
