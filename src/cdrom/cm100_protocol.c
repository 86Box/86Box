#include <string.h>
#include "cm100_internal.h"

static unsigned
cm100_bcd(uint8_t value)
{
    return (value >> 4) * 10 + (value & 15);
}

static int
cm100_valid_bcd(uint8_t value)
{
    return (value >> 4) <= 9 && (value & 15) <= 9;
}

int
cm100_decode_msf(const uint8_t msf[3], uint32_t *sector)
{
    if (!cm100_valid_bcd(msf[0]) || !cm100_valid_bcd(msf[1]) ||
        !cm100_valid_bcd(msf[2]))
        return 0;
    unsigned f = cm100_bcd(msf[0]);
    unsigned s = cm100_bcd(msf[1]);
    unsigned m = cm100_bcd(msf[2]);
    if (f >= 75 || s >= 60 || (m == 0 && s < 2))
        return 0;
    *sector = (m * 60 + s) * 75 + f - 150;
    return 1;
}

void
cm100_encode_msf(uint32_t frame, uint8_t msf[3])
{
    unsigned n[3] = { frame % 75, (frame / 75) % 60, frame / 4500 };
    for (unsigned i = 0; i < 3; i++)
        msf[i] = (uint8_t) (((n[i] / 10) << 4) | (n[i] % 10));
}

static unsigned
cm100_parameters(uint8_t opcode)
{
    switch (opcode) {
        case 0x17: case 0x59: case 0xc5: return 3;
        case 0xa6: case 0xb1: return 6;
        case 0x8b: case 0xd2: return 1;
        default: return 0;
    }
}

static int
cm100_status_command(uint8_t opcode)
{
    return opcode == 0x2d || opcode == 0x3a || opcode == 0x4e;
}

static int
cm100_known_command(uint8_t opcode)
{
    switch (opcode) {
        case 0x17: case 0x2d: case 0x3a: case 0x4e:
        case 0x59: case 0x63: case 0x74: case 0x8b:
        case 0xa6: case 0xb1: case 0xc5: case 0xd2:
            return 1;
        default:
            return 0;
    }
}

void
cm100_pack_status(const cm100_drive_t *d, uint8_t packet[CM100_STATUS_BYTES])
{
    memset(packet, 0, CM100_STATUS_BYTES);
    memcpy(packet + 1, d->last_command, 4);
    packet[5] = (d->motion == CM100_SPINNING_UP ||
                 d->motion == CM100_SPINNING_DOWN ||
                 d->motion == CM100_SEEKING ||
                 d->motion == CM100_READING);
    packet[5] |= d->status_error ? 0x02 : 0;
    packet[5] |= d->motion != CM100_STOPPED &&
                 d->motion != CM100_SPINNING_UP ? 0x04 : 0;
    /* Ready means a disc is present behind a closed door. The patent
       explicitly says that the spindle need not be running. */
    packet[5] |= !d->disc_present || d->door_open ? 0x08 : 0;
    packet[5] |= d->door_open ? 0x10 : 0;
    packet[5] |= d->locked ? 0x20 : 0;
    packet[6] = d->drive_error;
    packet[7] = d->communication_error;
    /* An illegal seek reports the end-of-disc address, including if another
       command stopped the spindle after the error (EP 0 241 081 A1, p. 10). */
    if (d->motion != CM100_STOPPED || d->drive_error == CM100_ERROR_ADDRESS) {
        memcpy(packet + 8, d->status_address, 3);
        if (!d->drive_error)
            cm100_encode_msf(d->sector + 150, packet + 8);
    }
}

void
cm100_queue_response(cm100_drive_t *d, uint8_t value)
{
    uint8_t next = (d->queue_tail + 1) % CM100_RESPONSE_QUEUE;
    if (next == d->queue_head) {
        cm100_raise_comm_error(d, CM100_COMM_GENERAL);
        return;
    }
    d->response_queue[d->queue_tail] = value;
    d->queue_tail = next;
}

static void
cm100_read_characteristics(cm100_drive_t *d)
{
    /* The patent enumerates eleven bytes, but the original CM153.MSC
       requests twelve during initialization. Preserve the documented
       revision at byte zero and provide an unspecified zero byte before
       the final non-jukebox disk count. Do not assign an identity to it. */
    memset(d->response_packet, 0, sizeof(d->response_packet));
    d->response_packet[0] = 0x01;
    d->response_size = 12;
    d->response_pos = 0;
}

static void
cm100_execute(cm100_drive_t *d)
{
    const uint8_t opcode = d->packet[0];
    uint32_t start, end;
    if (d->packet_blocked)
        return;

    switch (opcode) {
        case 0x2d:
            cm100_read_characteristics(d);
            return;
        case 0x3a:
            cm100_pack_status(d, d->response_packet);
            d->response_size = CM100_STATUS_BYTES;
            d->response_pos = 0;
            return;
        case 0x4e:
            d->drive_error = d->communication_error = d->status_error = 0;
            d->latched_attention = 0;
            cm100_emit(d, CM100_ATTENTION, d->data_attention);
            return;
        case 0x63:
            cm100_start_spin(d);
            return;
        case 0x74:
            cm100_stop_read(d);
            d->pending_seek = 0;
            d->motion = CM100_SPINNING_DOWN;
            d->motion_start_ns = d->now_ns;
            d->motion_end_ns = d->now_ns + UINT64_C(1000000000);
            cm100_raise_drive_error(d, CM100_EVENT_SPIN_DOWN);
            return;
        case 0x8b:
            cm100_raise_comm_error(d, CM100_COMM_ILLEGAL);
            return;
        case 0xd2:
            /* The patent names the operation but does not specify the
               parameter encoding. Do not guess which value locks the door. */
            cm100_raise_comm_error(d, CM100_COMM_PARAMETER);
            return;
        case 0x17: case 0x59: case 0xa6: case 0xb1: case 0xc5:
            if (!cm100_decode_msf(d->packet + 1, &start)) {
                cm100_raise_comm_error(d, CM100_COMM_PARAMETER);
                return;
            }
            if (opcode == 0xa6 || opcode == 0xb1) {
                if (!cm100_decode_msf(d->packet + 4, &end)) {
                    cm100_raise_comm_error(d, CM100_COMM_PARAMETER);
                    return;
                }
                if (start > end) {
                    cm100_raise_drive_error(d, CM100_ERROR_RANGE);
                    return;
                }
            }
            d->read_start = start;
            cm100_start_seek(d, start, opcode != 0x59 && opcode != 0xc5);
            if (opcode == 0xa6 || opcode == 0xb1) {
                d->reading_bounded = end < d->medium_sectors;
                d->read_end = end;
            }
            return;
        default:
            cm100_raise_comm_error(d, CM100_COMM_ILLEGAL);
            return;
    }
}

void
cm100_protocol_reset(cm100_drive_t *d)
{
    d->packet_size = d->packet_needed = d->packet_blocked = 0;
    d->response_size = d->response_pos = 0;
    d->queue_head = d->queue_tail = d->response_bit_pos = 0;
    d->rx_phase = d->rx_byte = d->rx_parity = d->low_bits = 0;
    d->break_latched = 0;
}

void
cm100_protocol_byte(cm100_drive_t *d, uint8_t value, int parity_ok)
{
    if (!parity_ok) {
        cm100_raise_comm_error(d, CM100_COMM_PARITY);
        cm100_queue_response(d, value);
        d->packet_size = d->packet_needed = 0;
        return;
    }

    if (!d->packet_size && value == 0x9c &&
        d->response_pos < d->response_size) {
        cm100_queue_response(d, d->response_packet[d->response_pos++]);
        return;
    }

    if (!d->packet_size) {
        d->response_size = d->response_pos = 0;
        d->packet[0] = value;
        d->packet_size = 1;
        d->packet_needed = cm100_parameters(value) + 1;
        d->packet_blocked = d->attention && value != 0x3a && value != 0x4e;
        if (!cm100_status_command(value) && !d->packet_blocked) {
            cm100_stop_read(d);
            d->pending_seek = 0;
            d->seek_invalid = 0;
        }
        if (!cm100_status_command(value)) {
            memset(d->last_command, 0, sizeof(d->last_command));
            d->last_command[0] = value;
        }
    } else {
        if (d->packet_size < sizeof(d->packet))
            d->packet[d->packet_size++] = value;
        if (!cm100_status_command(d->packet[0]) && d->packet_size <= 4)
            d->last_command[d->packet_size - 1] = value;
    }

    cm100_queue_response(d, value);
    if (d->packet_size == d->packet_needed) {
        if (d->packet_blocked)
            cm100_raise_comm_error(d, CM100_COMM_ATTENTION);
        else if (cm100_known_command(d->packet[0]))
            cm100_execute(d);
        else
            cm100_raise_comm_error(d, CM100_COMM_ILLEGAL);
        d->packet_size = d->packet_needed = 0;
    }
}

void
cm100_command_bit(cm100_drive_t *d, int level, uint64_t now_ns)
{
    cm100_advance(d, now_ns);
    level = !!level;
    cm100_emit(d, CM100_COMMAND, level);
    if (level) {
        d->low_bits = 0;
        d->break_latched = 0;
    } else if (d->low_bits < 255) {
        d->low_bits++;
    }
    if (d->low_bits >= 21 && !d->break_latched) {
        cm100_reset(d, now_ns);
        d->input_command = 0;
        d->low_bits = 21;
        d->break_latched = 1;
        return;
    }
    if (d->break_latched)
        return;
    if (d->rx_phase == 0) {
        if (!level) {
            d->rx_phase = 1;
            d->rx_byte = d->rx_parity = 0;
        }
        return;
    }
    if (d->rx_phase <= 8) {
        d->rx_byte |= level << (d->rx_phase - 1);
        d->rx_parity ^= level;
    } else if (d->rx_phase == 9) {
        d->rx_parity ^= level;
    } else {
        if (level)
            cm100_protocol_byte(d, d->rx_byte, d->rx_parity == 1);
        d->rx_phase = 0;
        return;
    }
    d->rx_phase++;
}

int
cm100_response_bit(cm100_drive_t *d, uint64_t now_ns)
{
    cm100_advance(d, now_ns);
    if (d->queue_head == d->queue_tail) {
        cm100_emit(d, CM100_RESPONSE, 1);
        return 1;
    }
    uint8_t value = d->response_queue[d->queue_head];
    unsigned pos = d->response_bit_pos++;
    int level = 1;
    if (!pos)
        level = 0;
    else if (pos <= 8)
        level = (value >> (pos - 1)) & 1;
    else if (pos == 9) {
        unsigned parity = value;
        parity ^= parity >> 4;
        parity ^= parity >> 2;
        parity ^= parity >> 1;
        level = !(parity & 1);
    }
    cm100_emit(d, CM100_RESPONSE, level);
    if (d->response_bit_pos == CM100_UART_BITS) {
        d->response_bit_pos = 0;
        d->queue_head = (d->queue_head + 1) % CM100_RESPONSE_QUEUE;
    }
    return level;
}
