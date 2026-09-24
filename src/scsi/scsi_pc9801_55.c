/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NEC PC-9801-55 C-BUS SCSI interface, using the WD33C93.
 *
 *          The WD33C93 exposes its register file through an address port
 *          and a data port.  This device models the WD register/command
 *          interface and routes completed SCSI commands to 86Box's common
 *          SCSI target layer.  The board's third port supplies the PC-98
 *          DMA gate/configuration and its memory-bank register controls
 *          board reset/interrupt enable.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/dma.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_pc98.h>
#include <86box/rom.h>

#define PC98_55L_ROM_LOW              "roms/scsi/nec/pc9801_55l/eta1b_00.bin"
#define PC98_55L_ROM_HIGH             "roms/scsi/nec/pc9801_55l/eta3b_00.bin"
#define PC98_55U_ROM_LOW              "roms/scsi/nec/pc9801_55u/eta1b_00.bin"
#define PC98_55U_ROM_HIGH             "roms/scsi/nec/pc9801_55u/eta3b_00.bin"
#define PC98_55_ROM_SIZE              0x2000
#define PC98_55_ROM_WINDOW_SIZE       0x1000

#define PC98_55_REG_OWN_ID             0x00
#define PC98_55_REG_CDB1               0x03
#define PC98_55_REG_TARGET_LUN         0x0f
#define PC98_55_REG_COMMAND_PHASE      0x10
#define PC98_55_REG_TRANSFER_MSB       0x12
#define PC98_55_REG_DESTINATION_ID     0x15
#define PC98_55_REG_SOURCE_ID          0x16
#define PC98_55_REG_SCSI_STATUS        0x17
#define PC98_55_REG_COMMAND            0x18
#define PC98_55_REG_DATA               0x19
#define PC98_55_REG_AUX_STATUS         0x1f
#define PC98_55_REG_MEMORY_BANK        0x30
#define PC98_55_REG_RESET_IRQ          0x33

#define PC98_55_CMD_RESET              0x00
#define PC98_55_CMD_ABORT              0x01
#define PC98_55_CMD_ASSERT_ATN         0x02
#define PC98_55_CMD_NEGATE_ACK         0x03
#define PC98_55_CMD_DISCONNECT         0x04
#define PC98_55_CMD_SELECT_ATN         0x06
#define PC98_55_CMD_SELECT             0x07
#define PC98_55_CMD_SELECT_ATN_XFER    0x08
#define PC98_55_CMD_SELECT_XFER        0x09
#define PC98_55_CMD_TRANSFER_INFO      0x20
#define PC98_55_CMD_TRANSFER_PAD       0x21

#define PC98_55_ASR_INT                0x80
#define PC98_55_ASR_CIP                0x10
#define PC98_55_ASR_DBR                0x01

#define PC98_55_CSR_RESET              0x00
#define PC98_55_CSR_SELECT             0x11
#define PC98_55_CSR_SELECT_XFER_DONE   0x16
#define PC98_55_CSR_XFER_DONE          0x18
#define PC98_55_CSR_ABORT              0x28
#define PC98_55_CSR_INVALID            0x40
#define PC98_55_CSR_TIMEOUT            0x42
#define PC98_55_CSR_SEL_ABORT          0x22

#define PC98_55_PHASE_DATA_OUT         0x00
#define PC98_55_PHASE_DATA_IN          0x01
#define PC98_55_PHASE_COMMAND          0x02
#define PC98_55_PHASE_STATUS           0x03
#define PC98_55_PHASE_MESSAGE_OUT      0x06
#define PC98_55_PHASE_MESSAGE_IN       0x07
#define PC98_55_PHASE_COMPLETE         0x60

#define PC98_55_IRQ_ENABLE             0x04
#define PC98_55_BUS_RESET              0x02
#define PC98_55_ROM_BANK               0x01
#define PC98_55_DMA_OUT_TO_MEMORY      0x01
#define PC98_55_DMA_IN_FROM_MEMORY     0x02

typedef struct pc98_55_t {
    uint8_t  regs[0x34];
    uint8_t  address;
    uint8_t  memory_bank;
    uint8_t  board_dma_status;
    uint8_t  dma_channel;
    uint8_t  source_id;
    uint8_t  target_id;
    uint8_t  phase;
    uint8_t  scsi_status;
    uint8_t  data_reg;
    uint8_t  irq;
    uint8_t  irq_pending;
    uint8_t  irq_line;
    uint8_t  last_aux_status;
    uint8_t  aux_status_valid;
    uint8_t  selected;
    uint8_t  transfer_active;
    uint8_t  transfer_direction;
    uint8_t  transfer_special;
    uint8_t  target_data_finished;
    uint8_t  pending_phase;
    uint8_t  pending_csr;
    uint8_t  message_out[8];
    uint8_t  message_out_pos;
    uint8_t  message_in;
    uint8_t  cdb[16];
    uint8_t  cdb_pos;
    uint32_t transfer_remaining;
    uint32_t transfer_position;
    uint8_t  bus;
    uint16_t io_base;
    uint32_t rom_addr;
    rom_t   *bios;
} pc98_55_t;

static uint32_t pc98_55_transfer_data(pc98_55_t *dev, uint32_t requested, int use_dma);
static void     pc98_55_finish_data(pc98_55_t *dev);
static uint32_t pc98_55_get_count(const pc98_55_t *dev);
static int      pc98_55_missing_rom_reported;

static void pc98_55_irq(pc98_55_t *dev, int set)
{
    const int line = set && (dev->memory_bank & PC98_55_IRQ_ENABLE);
    const int previous_pending = dev->irq_pending;
    const int previous_line    = dev->irq_line;

    if (line && !dev->irq_line) {
        picint(1u << dev->irq);
        dev->irq_line = 1;
    } else if (!line && dev->irq_line) {
        picintc(1u << dev->irq);
        dev->irq_line = 0;
    }
    dev->irq_pending = set ? 1 : 0;
    if ((previous_pending != dev->irq_pending) || (previous_line != dev->irq_line))
        always_log("[PC98-55] IRQ%d pending=%u line=%u enable=%u\n", dev->irq,
                   dev->irq_pending, dev->irq_line,
                   !!(dev->memory_bank & PC98_55_IRQ_ENABLE));
}

static void pc98_55_raise(pc98_55_t *dev, uint8_t csr)
{
    dev->scsi_status = csr;
    dev->regs[PC98_55_REG_SCSI_STATUS] = csr;
    dev->regs[PC98_55_REG_COMMAND_PHASE] = dev->phase;
    dev->transfer_active = 0;
    always_log("[PC98-55] controller status=%02X phase=%02X target=%u selected=%u count=%06X\n",
               csr, dev->phase, dev->target_id, dev->selected, pc98_55_get_count(dev));
    pc98_55_irq(dev, 1);
}

static uint32_t pc98_55_get_count(const pc98_55_t *dev)
{
    return ((uint32_t) dev->regs[PC98_55_REG_TRANSFER_MSB] << 16) |
           ((uint32_t) dev->regs[PC98_55_REG_TRANSFER_MSB + 1] << 8) |
           dev->regs[PC98_55_REG_TRANSFER_MSB + 2];
}

static void pc98_55_set_count(pc98_55_t *dev, uint32_t count)
{
    dev->regs[PC98_55_REG_TRANSFER_MSB]     = (uint8_t) (count >> 16);
    dev->regs[PC98_55_REG_TRANSFER_MSB + 1] = (uint8_t) (count >> 8);
    dev->regs[PC98_55_REG_TRANSFER_MSB + 2] = (uint8_t) count;
}

static scsi_device_t *pc98_55_target(pc98_55_t *dev)
{
    if (!dev->selected || (dev->target_id >= SCSI_ID_MAX))
        return NULL;

    return &scsi_devices[dev->bus][dev->target_id];
}

static void pc98_55_disconnect(pc98_55_t *dev)
{
    scsi_device_t *target = pc98_55_target(dev);

    /* Level-2 WD33C93 completions expose the target status through LUN. */
    if (target)
        dev->regs[PC98_55_REG_TARGET_LUN] = target->status;

    dev->selected = 0;
    dev->transfer_active = 0;
    dev->pending_phase = 0;
    dev->phase = PC98_55_PHASE_COMPLETE;
    always_log("[PC98-55] disconnect target=%u target_status=%02X\n", dev->target_id,
               target ? target->status : 0xff);
    pc98_55_raise(dev, PC98_55_CSR_SELECT_XFER_DONE);
}

static void pc98_55_advance_pending(pc98_55_t *dev)
{
    const uint8_t next_phase = dev->pending_phase;
    const uint8_t next_csr   = dev->pending_csr;

    dev->pending_phase = 0;
    dev->pending_csr   = 0;
    if (!next_csr)
        return;

    if (next_phase == PC98_55_PHASE_COMPLETE) {
        pc98_55_disconnect(dev);
        return;
    }

    dev->phase = next_phase;
    pc98_55_raise(dev, next_csr);
}

static void pc98_55_schedule_phase(pc98_55_t *dev, uint8_t phase)
{
    dev->pending_phase = phase;
    dev->pending_csr   = PC98_55_CSR_XFER_DONE | phase;
}

static int pc98_55_cdb_length(const pc98_55_t *dev, uint8_t opcode)
{
    static const uint8_t group_length[8] = { 6, 10, 10, 6, 16, 12, 10, 6 };
    const uint8_t         size_code  = dev->regs[PC98_55_REG_OWN_ID] & 0xc0;
    const uint8_t         max_length = (size_code == 0x80) ? 16 :
                                       (size_code == 0x40) ? 12 : 8;
    const uint8_t         length     = group_length[(opcode >> 5) & 7];

    /* The common SCSI target layer currently accepts command blocks up to 12 bytes. */
    return (length <= max_length) && (length <= sizeof(dev->cdb)) && (length <= 12) ? length : -1;
}

static void pc98_55_command_complete(pc98_55_t *dev, int auto_dma)
{
    scsi_device_t *target = pc98_55_target(dev);
    char           cdb_text[12 * 3 + 1] = { 0 };
    size_t         cdb_used = 0;
    int            cdb_len;

    if (!target) {
        always_log("[PC98-55] command has no selected target; timeout\n");
        dev->phase = PC98_55_PHASE_COMPLETE;
        pc98_55_raise(dev, PC98_55_CSR_TIMEOUT);
        return;
    }

    cdb_len = pc98_55_cdb_length(dev, dev->cdb[0]);
    if (cdb_len > 0) {
        for (int i = 0; i < cdb_len; i++)
            cdb_used += (size_t) snprintf(cdb_text + cdb_used, sizeof(cdb_text) - cdb_used,
                                          (i == 0) ? "%02X" : " %02X", dev->cdb[i]);
    }
    always_log("[PC98-55] SCSI command target=%u lun=%u CDB=[%s] auto_dma=%d\n",
               dev->target_id, dev->regs[PC98_55_REG_TARGET_LUN] & 7, cdb_text, auto_dma);

    scsi_device_command_phase0(target, dev->cdb);
    dev->transfer_position  = 0;
    dev->transfer_remaining = 0;
    dev->target_data_finished = 0;

    if ((target->status == 0) && target->sc &&
        ((target->phase == SCSI_PHASE_DATA_IN) || (target->phase == SCSI_PHASE_DATA_OUT)) &&
        target->sc->temp_buffer && (target->buffer_length > 0)) {
        dev->phase = (target->phase == SCSI_PHASE_DATA_IN) ? PC98_55_PHASE_DATA_IN : PC98_55_PHASE_DATA_OUT;
    } else
        dev->phase = PC98_55_PHASE_STATUS;

    always_log("[PC98-55] target result=%02X phase=%02X buffer=%d\n", target->status,
               dev->phase, target->buffer_length);

    if (dev->phase == PC98_55_PHASE_STATUS)
        pc98_55_finish_data(dev);

    if (auto_dma && (dev->phase != PC98_55_PHASE_STATUS) &&
        (dev->regs[0x01] & 0x80) && (dev->board_dma_status & 1)) {
        const uint32_t count = pc98_55_get_count(dev);
        const uint32_t moved = pc98_55_transfer_data(dev, count, 1);
        pc98_55_set_count(dev, count - moved);
        pc98_55_finish_data(dev);
    }

    pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | dev->phase);
}

static uint32_t pc98_55_transfer_data(pc98_55_t *dev, uint32_t requested, int use_dma)
{
    scsi_device_t *target = pc98_55_target(dev);
    uint8_t       *buffer;
    uint32_t       moved = 0;

    if (!target || !target->sc || !target->sc->temp_buffer ||
        (target->buffer_length <= 0) ||
        ((dev->phase != PC98_55_PHASE_DATA_IN) && (dev->phase != PC98_55_PHASE_DATA_OUT))) {
        always_log("[PC98-55] transfer unavailable: target=%u phase=%02X buffer=%d\n",
                   dev->target_id, dev->phase, target ? target->buffer_length : 0);
        return 0;
    }

    buffer = target->sc->temp_buffer;
    if (dev->transfer_position >= (uint32_t) target->buffer_length) {
        always_log("[PC98-55] transfer already at end: position=%u length=%d\n",
                   dev->transfer_position, target->buffer_length);
        return 0;
    }

    if (requested > ((uint32_t) target->buffer_length - dev->transfer_position))
        requested = (uint32_t) target->buffer_length - dev->transfer_position;

    if (!use_dma) {
        dev->transfer_remaining = requested;
        dev->transfer_direction  = (dev->phase == PC98_55_PHASE_DATA_IN);
        dev->transfer_active     = (requested != 0);
        always_log("[PC98-55] PIO transfer begin direction=%s requested=%u position=%u\n",
                   dev->transfer_direction ? "device-to-host" : "host-to-device",
                   requested, dev->transfer_position);
        if (dev->transfer_active && dev->transfer_direction)
            dev->data_reg = buffer[dev->transfer_position];
        return 0;
    }

    always_log("[PC98-55] DMA transfer begin direction=%s channel=%u requested=%u position=%u\n",
               (dev->phase == PC98_55_PHASE_DATA_IN) ? "device-to-host" : "host-to-device",
               dev->dma_channel, requested, dev->transfer_position);
    dma_set_drq(dev->dma_channel, 1);
    while (moved < requested) {
        int result;

        if (dev->phase == PC98_55_PHASE_DATA_IN) {
            result = dma_channel_write(dev->dma_channel, buffer[dev->transfer_position]);
            if (result == DMA_NODATA)
                break;
        } else {
            result = dma_channel_read(dev->dma_channel);
            if (result == DMA_NODATA)
                break;
            buffer[dev->transfer_position] = (uint8_t) result;
        }

        dev->transfer_position++;
        moved++;
        if (result & DMA_OVER)
            break;
    }
    dma_set_drq(dev->dma_channel, 0);
    always_log("[PC98-55] DMA transfer end moved=%u requested=%u position=%u remaining=%u\n",
               moved, requested, dev->transfer_position, requested - moved);
    return moved;
}

static void pc98_55_finish_data(pc98_55_t *dev)
{
    scsi_device_t *target = pc98_55_target(dev);

    if (!target || !target->sc || dev->target_data_finished)
        return;

    if (dev->phase == PC98_55_PHASE_STATUS) {
        scsi_device_command_phase1(target);
        dev->target_data_finished = 1;
        always_log("[PC98-55] target status phase complete: status=%02X\n", target->status);
        return;
    }

    if ((target->buffer_length <= 0) || !target->sc->temp_buffer)
        return;

    if (dev->transfer_position >= (uint32_t) target->buffer_length) {
        scsi_device_command_phase1(target);
        dev->target_data_finished = 1;
        pc98_55_schedule_phase(dev, PC98_55_PHASE_STATUS);
        always_log("[PC98-55] target data phase complete: transferred=%u status=%02X\n",
                   dev->transfer_position, target->status);
    }
}

static void pc98_55_start_transfer(pc98_55_t *dev, uint8_t command)
{
    scsi_device_t *target = pc98_55_target(dev);
    uint32_t       count  = pc98_55_get_count(dev);
    int            dma    = ((dev->regs[0x01] & 0x80) && (dev->board_dma_status & 1));

    dev->transfer_special = (command & 0x80) ? 1 : 0;
    always_log("[PC98-55] transfer command=%02X phase=%02X count=%06X dma=%d special=%u\n",
               command, dev->phase, count, dma, dev->transfer_special);

    if ((dev->phase == PC98_55_PHASE_STATUS) || (dev->phase == PC98_55_PHASE_MESSAGE_IN)) {
        if (!count || dev->transfer_special)
            count = 1;
        dev->transfer_remaining = 1;
        dev->transfer_active = 1;
        if (dev->phase == PC98_55_PHASE_STATUS)
            dev->data_reg = target ? target->status : 0x02;
        else
            dev->data_reg = dev->message_in;
        return;
    }

    if (dev->phase == PC98_55_PHASE_DATA_IN || dev->phase == PC98_55_PHASE_DATA_OUT) {
        if (!count) {
            pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | dev->phase);
            return;
        }
        if (!dma) {
            (void) pc98_55_transfer_data(dev, count, 0);
            if (!dev->transfer_active)
                pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | dev->phase);
            else if (dev->transfer_direction)
                dev->data_reg = target->sc->temp_buffer[dev->transfer_position];
            return;
        }

        dev->transfer_direction = (dev->phase == PC98_55_PHASE_DATA_IN);
        {
            const uint32_t moved = pc98_55_transfer_data(dev, count, 1);
            pc98_55_set_count(dev, pc98_55_get_count(dev) - moved);
        }
        pc98_55_finish_data(dev);
        pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | dev->phase);
        return;
    }

    if (dev->phase == PC98_55_PHASE_MESSAGE_OUT) {
        dev->transfer_remaining = count;
        dev->transfer_active = (count != 0);
        dev->message_out_pos = 0;
        if (!count)
            pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | PC98_55_PHASE_MESSAGE_OUT);
        return;
    }

    if (dev->phase == PC98_55_PHASE_COMMAND) {
        dev->transfer_remaining = count;
        dev->transfer_active = (count != 0);
        dev->cdb_pos = 0;
        if (!count)
            pc98_55_raise(dev, PC98_55_CSR_INVALID);
        return;
    }

    pc98_55_raise(dev, PC98_55_CSR_INVALID);
}

static void pc98_55_transfer_byte_done(pc98_55_t *dev)
{
    scsi_device_t *target = pc98_55_target(dev);

    if (dev->transfer_remaining)
        dev->transfer_remaining--;
    if (pc98_55_get_count(dev))
        pc98_55_set_count(dev, pc98_55_get_count(dev) - 1);

    if (((dev->phase == PC98_55_PHASE_DATA_IN) || (dev->phase == PC98_55_PHASE_DATA_OUT)) &&
        target && (target->buffer_length > 0) &&
        (dev->transfer_position >= (uint32_t) target->buffer_length)) {
        always_log("[PC98-55] PIO transfer reached target buffer end at %u bytes\n",
                   dev->transfer_position);
        pc98_55_finish_data(dev);
        pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | dev->phase);
        return;
    }

    if (dev->transfer_remaining)
        return;

    always_log("[PC98-55] PIO transfer command completed: phase=%02X position=%u\n",
               dev->phase, dev->transfer_position);
    dev->transfer_active = 0;
    switch (dev->phase) {
        case PC98_55_PHASE_DATA_IN:
        case PC98_55_PHASE_DATA_OUT:
            pc98_55_finish_data(dev);
            pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | dev->phase);
            break;
        case PC98_55_PHASE_STATUS:
            pc98_55_schedule_phase(dev, PC98_55_PHASE_MESSAGE_IN);
            pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | PC98_55_PHASE_STATUS);
            break;
        case PC98_55_PHASE_MESSAGE_IN:
            pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | PC98_55_PHASE_MESSAGE_IN);
            break;
        default:
            break;
    }
}

static void pc98_55_data_write(pc98_55_t *dev, uint8_t value)
{
    scsi_device_t *target = pc98_55_target(dev);

    dev->data_reg = value;
    if (!dev->transfer_active) {
        always_log("[PC98-55] ignored data write=%02X without active transfer (phase=%02X)\n",
                   value, dev->phase);
        return;
    }

    switch (dev->phase) {
        case PC98_55_PHASE_DATA_OUT:
            if (target && target->sc && target->sc->temp_buffer &&
                (dev->transfer_position < (uint32_t) target->buffer_length))
                target->sc->temp_buffer[dev->transfer_position++] = value;
            pc98_55_transfer_byte_done(dev);
            break;
        case PC98_55_PHASE_MESSAGE_OUT:
            if (dev->message_out_pos < sizeof(dev->message_out))
                always_log("[PC98-55] message-out byte[%u]=%02X\n", dev->message_out_pos, value);
            if (dev->message_out_pos < sizeof(dev->message_out))
                dev->message_out[dev->message_out_pos++] = value;
            if (dev->transfer_remaining)
                dev->transfer_remaining--;
            if (pc98_55_get_count(dev))
                pc98_55_set_count(dev, pc98_55_get_count(dev) - 1);
            if (!dev->transfer_remaining) {
                dev->transfer_active = 0;
                if (dev->message_out_pos && ((dev->message_out[0] & 0x80) != 0) && target)
                    scsi_device_identify(target, dev->message_out[0] & 7);
                dev->message_in = 0x00;
                for (uint8_t i = 0; i < dev->message_out_pos; i++) {
                    if (dev->message_out[i] == 0x01) {
                        dev->message_in = 0x07; /* MESSAGE REJECT: async transfers only */
                        break;
                    }
                }
                pc98_55_schedule_phase(dev,
                    (dev->message_in == 0x07) ?
                    PC98_55_PHASE_MESSAGE_IN : PC98_55_PHASE_COMMAND);
                pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | PC98_55_PHASE_MESSAGE_OUT);
            }
            break;
        case PC98_55_PHASE_COMMAND:
            always_log("[PC98-55] CDB byte[%u]=%02X\n", dev->cdb_pos, value);
            if (dev->cdb_pos < sizeof(dev->cdb))
                dev->cdb[dev->cdb_pos++] = value;
            if (dev->transfer_remaining)
                dev->transfer_remaining--;
            if (pc98_55_get_count(dev))
                pc98_55_set_count(dev, pc98_55_get_count(dev) - 1);
            if (!dev->transfer_remaining) {
                const int expected = dev->cdb_pos ? pc98_55_cdb_length(dev, dev->cdb[0]) : -1;
                dev->transfer_active = 0;
                if (expected > 0 && dev->cdb_pos >= (uint8_t) expected)
                    pc98_55_command_complete(dev, 0);
                else
                    pc98_55_raise(dev, PC98_55_CSR_INVALID);
            }
            break;
        default:
            break;
    }
}

static uint8_t pc98_55_data_read(pc98_55_t *dev)
{
    scsi_device_t *target = pc98_55_target(dev);
    uint8_t        value  = dev->data_reg;

    if (!dev->transfer_active)
        return value;

    switch (dev->phase) {
        case PC98_55_PHASE_DATA_IN:
            if (target && target->sc && target->sc->temp_buffer &&
                (dev->transfer_position < (uint32_t) target->buffer_length)) {
                value = target->sc->temp_buffer[dev->transfer_position++];
                dev->data_reg = value;
            }
            pc98_55_transfer_byte_done(dev);
            if (dev->transfer_active && target && target->sc && target->sc->temp_buffer &&
                (dev->transfer_position < (uint32_t) target->buffer_length))
                dev->data_reg = target->sc->temp_buffer[dev->transfer_position];
            break;
        case PC98_55_PHASE_STATUS:
        case PC98_55_PHASE_MESSAGE_IN:
            always_log("[PC98-55] PIO control byte read phase=%02X value=%02X\n",
                       dev->phase, value);
            pc98_55_transfer_byte_done(dev);
            break;
        default:
            break;
    }

    return value;
}

static void pc98_55_reset_controller(pc98_55_t *dev, int reset_targets)
{
    always_log("[PC98-55] controller reset reset_targets=%d bus=%u\n", reset_targets, dev->bus);
    pc98_55_irq(dev, 0);
    dev->selected = 0;
    dev->transfer_active = 0;
    dev->transfer_remaining = 0;
    dev->pending_phase = 0;
    dev->pending_csr = 0;
    dev->phase = PC98_55_PHASE_COMPLETE;
    dev->scsi_status = PC98_55_CSR_RESET;
    dev->regs[PC98_55_REG_COMMAND_PHASE] = dev->phase;
    dev->regs[PC98_55_REG_SCSI_STATUS] = dev->scsi_status;
    if (reset_targets) {
        for (uint8_t id = 0; id < SCSI_ID_MAX; id++)
            scsi_device_reset(&scsi_devices[dev->bus][id]);
        always_log("[PC98-55] reset all targets on bus %u\n", dev->bus);
    }
}

static void pc98_55_select(pc98_55_t *dev, uint8_t command)
{
    scsi_device_t *target;
    const uint8_t  destination = dev->regs[PC98_55_REG_DESTINATION_ID];
    int            cdb_len;

    always_log("[PC98-55] select command=%02X host_id=%u target_id=%u lun=%u\n",
               command, dev->source_id, destination & 7,
               dev->regs[PC98_55_REG_TARGET_LUN] & 7);
    pc98_55_irq(dev, 0);
    dev->target_id = destination & 7;
    dev->selected = 0;
    dev->pending_phase = 0;
    dev->pending_csr = 0;
    dev->transfer_position = 0;
    dev->transfer_remaining = 0;
    dev->target_data_finished = 0;
    target = (dev->target_id == dev->source_id) ? NULL :
             &scsi_devices[dev->bus][dev->target_id];

    if (!target || !scsi_device_present(target)) {
        always_log("[PC98-55] target %u absent on bus %u\n", dev->target_id, dev->bus);
        dev->phase = PC98_55_PHASE_COMPLETE;
        pc98_55_raise(dev, (command == PC98_55_CMD_SELECT_ATN || command == PC98_55_CMD_SELECT) ?
                      PC98_55_CSR_TIMEOUT : PC98_55_CSR_SEL_ABORT);
        return;
    }

    dev->selected = 1;
    scsi_device_identify(target, dev->regs[PC98_55_REG_TARGET_LUN] & 7);

    if ((command == PC98_55_CMD_SELECT_ATN) || (command == PC98_55_CMD_SELECT)) {
        dev->phase = (command == PC98_55_CMD_SELECT_ATN) ?
                     PC98_55_PHASE_MESSAGE_OUT : PC98_55_PHASE_COMMAND;
        pc98_55_raise(dev, PC98_55_CSR_SELECT);
        return;
    }

    cdb_len = pc98_55_cdb_length(dev, dev->regs[PC98_55_REG_CDB1]);
    if (cdb_len < 0) {
        dev->phase = PC98_55_PHASE_COMPLETE;
        pc98_55_raise(dev, PC98_55_CSR_INVALID);
        return;
    }

    memcpy(dev->cdb, &dev->regs[PC98_55_REG_CDB1], (size_t) cdb_len);
    pc98_55_command_complete(dev, 1);
}

static void pc98_55_command_write(pc98_55_t *dev, uint8_t command)
{
    always_log("[PC98-55] WD command=%02X phase=%02X target=%u count=%06X\n",
               command, dev->phase, dev->target_id, pc98_55_get_count(dev));
    switch (command & 0x7f) {
        case PC98_55_CMD_RESET:
            /* WD chip reset does not assert the board's separate SCSI bus reset. */
            pc98_55_reset_controller(dev, 0);
            pc98_55_raise(dev, PC98_55_CSR_RESET);
            break;
        case PC98_55_CMD_ABORT:
            pc98_55_reset_controller(dev, 0);
            pc98_55_raise(dev, PC98_55_CSR_ABORT);
            break;
        case PC98_55_CMD_ASSERT_ATN:
            if (dev->selected) {
                dev->phase = PC98_55_PHASE_MESSAGE_OUT;
                pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | dev->phase);
            } else
                pc98_55_raise(dev, PC98_55_CSR_INVALID);
            break;
        case PC98_55_CMD_NEGATE_ACK:
            if (dev->selected && (dev->phase == PC98_55_PHASE_MESSAGE_IN)) {
                if (dev->message_in == 0x00)
                    pc98_55_disconnect(dev);
                else {
                    dev->phase = PC98_55_PHASE_COMMAND;
                    pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | PC98_55_PHASE_COMMAND);
                }
            } else if (dev->selected)
                pc98_55_raise(dev, PC98_55_CSR_XFER_DONE | dev->phase);
            else
                pc98_55_raise(dev, PC98_55_CSR_INVALID);
            break;
        case PC98_55_CMD_DISCONNECT:
            if (dev->selected)
                pc98_55_disconnect(dev);
            else
                pc98_55_raise(dev, PC98_55_CSR_INVALID);
            break;
        case PC98_55_CMD_SELECT_ATN:
        case PC98_55_CMD_SELECT:
        case PC98_55_CMD_SELECT_ATN_XFER:
        case PC98_55_CMD_SELECT_XFER:
            pc98_55_select(dev, command & 0x7f);
            break;
        case PC98_55_CMD_TRANSFER_INFO:
        case PC98_55_CMD_TRANSFER_PAD:
            pc98_55_start_transfer(dev, command);
            break;
        default:
            pc98_55_raise(dev, PC98_55_CSR_INVALID);
            break;
    }
}

static uint8_t pc98_55_read_reg(pc98_55_t *dev, uint8_t reg)
{
    if (reg == PC98_55_REG_AUX_STATUS) {
        uint8_t status = 0;
        if (dev->irq_pending)
            status |= PC98_55_ASR_INT;
        if (dev->transfer_active)
            status |= PC98_55_ASR_DBR;
        if (!dev->aux_status_valid || (dev->last_aux_status != status)) {
            always_log("[PC98-55] AUX status=%02X (IRQ pending=%u, data ready=%u)\n",
                       status, !!(status & PC98_55_ASR_INT), !!(status & PC98_55_ASR_DBR));
            dev->last_aux_status = status;
            dev->aux_status_valid = 1;
        }
        return status;
    }

    if (reg == PC98_55_REG_SCSI_STATUS) {
        const uint8_t value = dev->scsi_status;
        always_log("[PC98-55] SCSI status read/ack=%02X pending_phase=%02X\n",
                   value, dev->pending_phase);
        pc98_55_irq(dev, 0);
        pc98_55_advance_pending(dev);
        return value;
    }

    if (reg == PC98_55_REG_DATA)
        return pc98_55_data_read(dev);

    if (reg == PC98_55_REG_MEMORY_BANK)
        return dev->memory_bank;

    if (reg == PC98_55_REG_RESET_IRQ)
        return (dev->source_id & 7) | ((dev->irq == 3) ? 0x00 :
               (dev->irq == 5) ? 0x08 : (dev->irq == 6) ? 0x10 :
               (dev->irq == 9) ? 0x18 : (dev->irq == 12) ? 0x20 : 0x28);

    if (reg < sizeof(dev->regs)) {
        const uint8_t value = dev->regs[reg];

        /* WD33C93 count/CDB register sequences auto-increment on SCMD reads. */
        if ((reg != PC98_55_REG_DATA) && (reg != PC98_55_REG_COMMAND))
            dev->address = (reg + 1) & 0x3f;

        return value;
    }

    return 0xff;
}

static void pc98_55_write_reg(pc98_55_t *dev, uint8_t reg, uint8_t value)
{
    if (reg == PC98_55_REG_DATA) {
        pc98_55_data_write(dev, value);
        return;
    }

    if (reg == PC98_55_REG_COMMAND) {
        dev->regs[reg] = value;
        pc98_55_command_write(dev, value);
        return;
    }

    if (reg == PC98_55_REG_MEMORY_BANK) {
        const uint8_t previous = dev->memory_bank;
        dev->memory_bank = value;
        always_log("[PC98-55] board control=%02X (ROM bank=%u IRQ enable=%u bus reset=%u)\n",
                   value, !!(value & PC98_55_ROM_BANK),
                   !!(value & PC98_55_IRQ_ENABLE), !!(value & PC98_55_BUS_RESET));
        pc98_55_irq(dev, dev->irq_pending);
        if ((previous ^ value) & PC98_55_ROM_BANK)
            mem_mapping_set_exec(&dev->bios->mapping,
                                 dev->bios->rom + ((value & PC98_55_ROM_BANK) ? PC98_55_ROM_WINDOW_SIZE : 0));
        if ((value & PC98_55_BUS_RESET) && !(previous & PC98_55_BUS_RESET))
            pc98_55_reset_controller(dev, 1);
        return;
    }

    if (reg < sizeof(dev->regs))
        dev->regs[reg] = value;
}

static uint8_t pc98_55_in(uint16_t port, void *priv)
{
    pc98_55_t *dev = (pc98_55_t *) priv;

    switch ((port - dev->io_base) & 0x07) {
        case 0: {
            const uint8_t value = pc98_55_read_reg(dev, PC98_55_REG_AUX_STATUS);
            return value;
        }
        case 2: {
            const uint8_t reg = dev->address;
            const int     bulk_data = (reg == PC98_55_REG_DATA) && dev->transfer_active &&
                                      (dev->phase == PC98_55_PHASE_DATA_IN);
            const uint8_t value = pc98_55_read_reg(dev, reg);
            if (!bulk_data && (reg != PC98_55_REG_AUX_STATUS))
                always_log("[PC98-55] I/O read port=%04X reg=%02X value=%02X phase=%02X\n",
                           port, reg, value, dev->phase);
            return value;
        }
        case 4:
            always_log("[PC98-55] DMA gate read port=%04X value=%02X\n", port,
                       dev->board_dma_status);
            return dev->board_dma_status;
        default:
            always_log("[PC98-55] unmapped I/O read port=%04X\n", port);
            return 0xff;
    }
}

static void pc98_55_out(uint16_t port, uint8_t value, void *priv)
{
    pc98_55_t *dev = (pc98_55_t *) priv;

    switch ((port - dev->io_base) & 0x07) {
        case 0:
            dev->address = value & 0x3f;
            always_log("[PC98-55] I/O register select port=%04X address=%02X\n", port,
                       dev->address);
            break;
        case 2:
            if ((dev->address != PC98_55_REG_DATA) &&
                (dev->address != PC98_55_REG_AUX_STATUS))
                always_log("[PC98-55] I/O write port=%04X reg=%02X value=%02X phase=%02X\n",
                           port, dev->address, value, dev->phase);
            pc98_55_write_reg(dev, dev->address, value);
            if ((dev->address != PC98_55_REG_COMMAND) &&
                (dev->address != PC98_55_REG_DATA) &&
                (dev->address != PC98_55_REG_MEMORY_BANK) &&
                (dev->address != PC98_55_REG_RESET_IRQ) &&
                (dev->address != PC98_55_REG_AUX_STATUS))
                dev->address = (dev->address + 1) & 0x3f;
            break;
        case 4:
            /* The board's C-BUS DMA gate is controlled by 01h/02h strobes. */
            if (value == 0x01)
                dev->board_dma_status = 1;
            else if (value == 0x02)
                dev->board_dma_status = 0;
            always_log("[PC98-55] DMA gate write port=%04X value=%02X enabled=%u\n",
                       port, value, dev->board_dma_status);
            break;
        default:
            always_log("[PC98-55] unmapped I/O write port=%04X value=%02X\n", port, value);
            break;
    }
}

static uint8_t pc98_55_rom_readb(uint32_t addr, void *priv)
{
    const pc98_55_t *dev = (const pc98_55_t *) priv;
    const uint32_t   bank = (dev->memory_bank & PC98_55_ROM_BANK) ? PC98_55_ROM_WINDOW_SIZE : 0;
    const uint32_t   offset = (addr - dev->rom_addr) & (PC98_55_ROM_WINDOW_SIZE - 1);

    return dev->bios->rom[bank + offset];
}

static uint16_t pc98_55_rom_readw(uint32_t addr, void *priv)
{
    return (uint16_t) (pc98_55_rom_readb(addr, priv) |
                       ((uint16_t) pc98_55_rom_readb(addr + 1, priv) << 8));
}

static uint32_t pc98_55_rom_readl(uint32_t addr, void *priv)
{
    return (uint32_t) pc98_55_rom_readb(addr, priv) |
           ((uint32_t) pc98_55_rom_readb(addr + 1, priv) << 8) |
           ((uint32_t) pc98_55_rom_readb(addr + 2, priv) << 16) |
           ((uint32_t) pc98_55_rom_readb(addr + 3, priv) << 24);
}

static int pc98_55_rom_pair_present(const char *low, const char *high)
{
    return low && high && rom_present(low) && rom_present(high);
}

static int pc98_55_available(void)
{
    const int available = pc98_55_rom_pair_present(PC98_55L_ROM_LOW, PC98_55L_ROM_HIGH) ||
                          pc98_55_rom_pair_present(PC98_55U_ROM_LOW, PC98_55U_ROM_HIGH);

    if (available) {
        if (pc98_55_missing_rom_reported)
            always_log("[PC98-55] required ROM pair is now available\n");
        pc98_55_missing_rom_reported = 0;
    } else if (!pc98_55_missing_rom_reported) {
        always_log("[PC98-55] SCSI option hidden: install eta1b_00.bin and eta3b_00.bin under roms/scsi/nec/pc9801_55l/ (or pc9801_55u/)\n");
        pc98_55_missing_rom_reported = 1;
    }

    return available;
}

static void *pc98_55_init(const device_t *info)
{
    pc98_55_t *dev = (pc98_55_t *) calloc(1, sizeof(pc98_55_t));
    const char *bios_ver;
    const char *bios_low;
    const char *bios_high;
    uint16_t    base;

    if (!dev)
        return NULL;

    base            = (uint16_t) device_get_config_int("base");
    dev->irq        = (uint8_t) device_get_config_int("irq");
    dev->source_id  = (uint8_t) device_get_config_int("scsi_id");
    dev->dma_channel = (uint8_t) device_get_config_int("dma");
    dev->io_base     = base;
    dev->rom_addr    = device_get_config_hex20("bios_addr");
    if ((dev->irq >= 16) || (dev->source_id >= 8) || (dev->dma_channel >= 8)) {
        always_log("[PC98-55] init rejected invalid config: IRQ=%u host_id=%u DMA=%u\n",
                   dev->irq, dev->source_id, dev->dma_channel);
        free(dev);
        return NULL;
    }

    dev->bios = (rom_t *) calloc(1, sizeof(rom_t));
    if (dev->bios == NULL) {
        free(dev);
        return NULL;
    }

    bios_ver  = device_get_config_bios("bios_ver");
    bios_low  = device_get_bios_file(info, bios_ver, 0);
    bios_high = device_get_bios_file(info, bios_ver, 1);
    /* Use a complete installed variant when the saved selection is missing. */
    if (!pc98_55_rom_pair_present(bios_low, bios_high)) {
        if (pc98_55_rom_pair_present(PC98_55L_ROM_LOW, PC98_55L_ROM_HIGH)) {
            bios_low  = PC98_55L_ROM_LOW;
            bios_high = PC98_55L_ROM_HIGH;
        } else if (pc98_55_rom_pair_present(PC98_55U_ROM_LOW, PC98_55U_ROM_HIGH)) {
            bios_low  = PC98_55U_ROM_LOW;
            bios_high = PC98_55U_ROM_HIGH;
        }
    }
    /* Each 32 KiB dump repeats a 4 KiB lane. Interleave eta1b on even bytes
       and eta3b on odd bytes to form the board's 8 KiB banked ROM image. */
    if (!pc98_55_rom_pair_present(bios_low, bios_high) ||
        (rom_init_interleaved(dev->bios, bios_low, bios_high, dev->rom_addr,
                              PC98_55_ROM_SIZE, PC98_55_ROM_SIZE - 1, 0,
                              MEM_MAPPING_EXTERNAL) != 0)) {
        always_log("[PC98-55] init failed to load ROM pair: low=%s high=%s\n",
                   bios_low ? bios_low : "(missing)", bios_high ? bios_high : "(missing)");
        free(dev->bios);
        free(dev);
        return NULL;
    }
    always_log("[PC98-55] ROM pair loaded: low=%s high=%s map=%05X size=%04X window=%04X\n",
               bios_low, bios_high, dev->rom_addr, PC98_55_ROM_SIZE,
               PC98_55_ROM_WINDOW_SIZE);

    /* The board exposes one 4 KiB bank at a time from its 8 KiB ROM. */
    mem_mapping_set_addr(&dev->bios->mapping, dev->rom_addr, PC98_55_ROM_WINDOW_SIZE);
    mem_mapping_set_handler(&dev->bios->mapping, pc98_55_rom_readb, pc98_55_rom_readw,
                            pc98_55_rom_readl, NULL, NULL, NULL);
    mem_mapping_set_p(&dev->bios->mapping, dev);
    mem_mapping_set_exec(&dev->bios->mapping, dev->bios->rom);

    dev->bus = scsi_get_bus();
    if (dev->bus == 0xff) {
        always_log("[PC98-55] init failed: no free SCSI bus\n");
        mem_mapping_disable(&dev->bios->mapping);
        mem_mapping_set_handler(&dev->bios->mapping, NULL, NULL, NULL, NULL, NULL, NULL);
        mem_mapping_set_exec(&dev->bios->mapping, NULL);
        mem_mapping_set_p(&dev->bios->mapping, NULL);
        free(dev->bios->rom);
        dev->bios->rom = NULL;
        /* The memory-map node stays linked for the lifetime of the emulator. */
        free(dev);
        return NULL;
    }

    dev->regs[PC98_55_REG_OWN_ID] = (uint8_t) (0x40 | dev->source_id); /* 12-byte CDB-size encoding */
    dev->regs[PC98_55_REG_SOURCE_ID] = dev->source_id;
    dev->phase = PC98_55_PHASE_COMPLETE;
    dev->scsi_status = PC98_55_CSR_RESET;
    dev->regs[PC98_55_REG_COMMAND_PHASE] = dev->phase;
    dev->regs[PC98_55_REG_SCSI_STATUS] = dev->scsi_status;
    io_sethandler(base, 6, pc98_55_in, NULL, NULL, pc98_55_out, NULL, NULL, dev);
    scsi_bus_set_speed(dev->bus, 5000000.0);
    for (uint8_t id = 0; id < SCSI_ID_MAX; id++)
        scsi_device_reset(&scsi_devices[dev->bus][id]);

    always_log("[PC98-55] initialized: bus=%u I/O=%04X IRQ=%u DMA=%u host_id=%u ROM=%05X\n",
               dev->bus, dev->io_base, dev->irq, dev->dma_channel, dev->source_id,
               dev->rom_addr);

    return dev;
}

static void pc98_55_close(void *priv)
{
    pc98_55_t *dev = (pc98_55_t *) priv;

    if (dev) {
        always_log("[PC98-55] closing bus=%u I/O=%04X\n", dev->bus, dev->io_base);
        io_removehandler(dev->io_base, 6, pc98_55_in, NULL, NULL, pc98_55_out, NULL, NULL, dev);
        pc98_55_irq(dev, 0);
        mem_mapping_disable(&dev->bios->mapping);
        mem_mapping_set_handler(&dev->bios->mapping, NULL, NULL, NULL, NULL, NULL, NULL);
        mem_mapping_set_exec(&dev->bios->mapping, NULL);
        mem_mapping_set_p(&dev->bios->mapping, NULL);
        free(dev->bios->rom);
        dev->bios->rom = NULL;
        /* The memory-map node stays linked for the lifetime of the emulator. */
        free(dev);
    }
}

// clang-format off
static const device_config_t pc98_55_config[] = {
    {
        .name           = "bios_ver",
        .description    = "Board ROM (8 KiB, two chips required)",
        .type           = CONFIG_BIOS,
        .default_string = "pc9801_55l",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "PC-9801-55L",
                .internal_name = "pc9801_55l",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = PC98_55_ROM_SIZE,
                .files         = { PC98_55L_ROM_LOW, PC98_55L_ROM_HIGH, "" }
            },
            {
                .name          = "PC-9801-55U",
                .internal_name = "pc9801_55u",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = PC98_55_ROM_SIZE,
                .files         = { PC98_55U_ROM_LOW, PC98_55U_ROM_HIGH, "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xdc000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "C800H", .value = 0xc8000 },
            { .description = "CC00H", .value = 0xcc000 },
            { .description = "D000H", .value = 0xd0000 },
            { .description = "D400H", .value = 0xd4000 },
            { .description = "D800H", .value = 0xd8000 },
            { .description = "DC00H", .value = 0xdc000 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "base",
        .description    = "I/O base",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0x0cc0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0CC0H", .value = 0x0cc0 },
            { .description = "0CD0H", .value = 0x0cd0 },
            { .description = "0CE0H", .value = 0x0ce0 },
            { .description = "0CF0H", .value = 0x0cf0 },
            { .description = ""                         }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 5,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 3",  .value = 3 },
            { .description = "IRQ 5",  .value = 5 },
            { .description = "IRQ 6",  .value = 6 },
            { .description = "IRQ 9",  .value = 9 },
            { .description = "IRQ 12", .value = 12 },
            { .description = "IRQ 13", .value = 13 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "scsi_id",
        .description    = "Host SCSI ID",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 7,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "ID 0", .value = 0 },
            { .description = "ID 1", .value = 1 },
            { .description = "ID 2", .value = 2 },
            { .description = "ID 3", .value = 3 },
            { .description = "ID 4", .value = 4 },
            { .description = "ID 5", .value = 5 },
            { .description = "ID 6", .value = 6 },
            { .description = "ID 7", .value = 7 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "dma",
        .description    = "DMA channel",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Channel 0", .value = 0 },
            { .description = "Channel 2", .value = 2 },
            { .description = "Channel 3", .value = 3 },
            { .description = ""                        }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t scsi_pc9801_55_device = {
    .name          = "NEC PC-9801-55 SCSI (WD33C93)",
    .internal_name = "pc9801_55_scsi",
    .flags         = DEVICE_CBUS,
    .local         = 0,
    .init          = pc98_55_init,
    .close         = pc98_55_close,
    .reset         = NULL,
    .available     = pc98_55_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pc98_55_config
};
