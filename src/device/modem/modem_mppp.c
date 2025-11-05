/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Multilink PPP fragmentation and reassembly.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <86box/modem/modem_mppp.h>

size_t
ppp_mppp_header_size(bool short_sequence)
{
    return short_sequence ? 2 : 4;
}

bool
ppp_mppp_encode_header(uint8_t *header, size_t capacity, bool short_sequence,
                       uint32_t sequence, bool begin, bool end)
{
    size_t header_length = ppp_mppp_header_size(short_sequence);

    if (!header || capacity < header_length)
        return false;

    header[0] = (uint8_t) ((begin ? 0x80 : 0) | (end ? 0x40 : 0));
    if (short_sequence) {
        if (sequence > 0x0FFF)
            return false;
        header[0] |= (uint8_t) (sequence >> 8);
        header[1] = (uint8_t) sequence;
    } else {
        if (sequence > 0xFFFFFF)
            return false;
        header[1] = (uint8_t) (sequence >> 16);
        header[2] = (uint8_t) (sequence >> 8);
        header[3] = (uint8_t) sequence;
    }

    return true;
}

bool
ppp_mppp_reassembler_init(ppp_mppp_reassembler_t *state, size_t mrru,
                          bool short_sequence)
{
    if (!state || mrru < 2)
        return false;

    memset(state, 0, sizeof(*state));
    state->packet = (uint8_t *) malloc(mrru);
    if (!state->packet)
        return false;

    state->mrru = mrru;
    state->sequence_bits = short_sequence ? 12 : 24;
    state->sequence_mask = short_sequence ? 0x0FFF : 0xFFFFFF;
    return true;
}

void
ppp_mppp_reassembler_reset(ppp_mppp_reassembler_t *state)
{
    if (!state)
        return;

    for (size_t index = 0; index < PPP_MPPP_REORDER_WINDOW; index++) {
        free(state->fragments[index].data);
        memset(&state->fragments[index], 0, sizeof(state->fragments[index]));
    }
    state->sequence_initialized = false;
    state->assembling = false;
    state->next_sequence = 0;
    state->packet_length = 0;
}

void
ppp_mppp_reassembler_close(ppp_mppp_reassembler_t *state)
{
    if (!state)
        return;

    ppp_mppp_reassembler_reset(state);
    free(state->packet);
    memset(state, 0, sizeof(*state));
}

static bool
ppp_mppp_store_fragment(ppp_mppp_reassembler_t *state, uint32_t sequence,
                        bool begin, bool end, const uint8_t *data, size_t length)
{
    ppp_mppp_fragment_t *slot = &state->fragments[sequence % PPP_MPPP_REORDER_WINDOW];

    if (slot->occupied)
        return slot->sequence == sequence;

    slot->data = (uint8_t *) malloc(length);
    if (!slot->data)
        return false;
    memcpy(slot->data, data, length);
    slot->length = length;
    slot->sequence = sequence;
    slot->begin = begin;
    slot->end = end;
    slot->occupied = true;
    return true;
}

bool
ppp_mppp_reassembler_input(ppp_mppp_reassembler_t *state,
                           const uint8_t *fragment, size_t fragment_length,
                           ppp_mppp_packet_cb callback, void *opaque)
{
    size_t   header_length;
    uint8_t  flags;
    uint32_t sequence;
    bool     begin;
    bool     end;
    uint32_t sequence_space;
    uint32_t distance;
    size_t   slot_index;
    ppp_mppp_fragment_t *slot;

    if (!state || !state->packet || !fragment)
        return false;

    header_length = state->sequence_bits == 12 ? 2 : 4;
    if (fragment_length <= header_length)
        return false;

    flags = fragment[0];
    begin = (flags & 0x80) != 0;
    end = (flags & 0x40) != 0;
    if (state->sequence_bits == 12) {
        if (flags & 0x30)
            return false;
        sequence = ((uint32_t) (flags & 0x0F) << 8) | fragment[1];
    } else {
        if (flags & 0x3F)
            return false;
        sequence = ((uint32_t) fragment[1] << 16)
                 | ((uint32_t) fragment[2] << 8)
                 | (uint32_t) fragment[3];
    }

    sequence_space = state->sequence_mask + 1;
    if (!state->sequence_initialized && !begin)
        return ppp_mppp_store_fragment(state, sequence, begin, end,
                                       fragment + header_length,
                                       fragment_length - header_length);

    if (!state->sequence_initialized) {
        state->next_sequence = sequence;
        state->sequence_initialized = true;
        for (size_t index = 0; index < PPP_MPPP_REORDER_WINDOW; index++) {
            slot = &state->fragments[index];
            if (!slot->occupied)
                continue;
            distance = (slot->sequence - sequence) & state->sequence_mask;
            if (distance == 0 || distance >= sequence_space / 2
                || distance >= PPP_MPPP_REORDER_WINDOW) {
                free(slot->data);
                memset(slot, 0, sizeof(*slot));
            }
        }
    }

    distance = (sequence - state->next_sequence) & state->sequence_mask;
    if (distance >= sequence_space / 2 || distance >= PPP_MPPP_REORDER_WINDOW)
        return false;

    if (!ppp_mppp_store_fragment(state, sequence, begin, end,
                                 fragment + header_length,
                                 fragment_length - header_length))
        return false;

    while (true) {
        slot_index = state->next_sequence % PPP_MPPP_REORDER_WINDOW;
        slot = &state->fragments[slot_index];
        if (!slot->occupied || slot->sequence != state->next_sequence)
            break;

        if (slot->begin) {
            state->assembling = true;
            state->packet_length = 0;
        }

        if (state->assembling) {
            if (slot->length > state->mrru - state->packet_length) {
                state->assembling = false;
                state->packet_length = 0;
            } else {
                memcpy(state->packet + state->packet_length, slot->data, slot->length);
                state->packet_length += slot->length;
                if (slot->end) {
                    if (callback)
                        callback(opaque, state->packet, state->packet_length);
                    state->assembling = false;
                    state->packet_length = 0;
                }
            }
        }

        free(slot->data);
        memset(slot, 0, sizeof(*slot));
        state->next_sequence = (state->next_sequence + 1) & state->sequence_mask;
    }

    return true;
}

bool
ppp_mppp_sender_init(ppp_mppp_sender_t *state, size_t mrru, bool short_sequence)
{
    if (!state || mrru < 2)
        return false;

    memset(state, 0, sizeof(*state));
    state->mrru = mrru;
    state->sequence_bits = short_sequence ? 12 : 24;
    state->sequence_mask = short_sequence ? 0x0FFF : 0xFFFFFF;
    return true;
}

bool
ppp_mppp_sender_add_link(ppp_mppp_sender_t *state,
                         ppp_mppp_fragment_cb send, void *opaque,
                         size_t max_fragment_payload)
{
    if (!state || !send || max_fragment_payload == 0
        || state->link_count >= PPP_MPPP_MAX_LINKS)
        return false;

    for (size_t index = 0; index < state->link_count; index++) {
        if (state->links[index].opaque == opaque)
            return false;
    }

    ppp_mppp_tx_link_t *link = &state->links[state->link_count++];
    link->send = send;
    link->opaque = opaque;
    link->max_fragment_payload = max_fragment_payload;
    return true;
}

void
ppp_mppp_sender_remove_link(ppp_mppp_sender_t *state, void *opaque)
{
    if (!state)
        return;

    for (size_t index = 0; index < state->link_count; index++) {
        if (state->links[index].opaque != opaque)
            continue;

        state->link_count--;
        if (index != state->link_count)
            state->links[index] = state->links[state->link_count];
        memset(&state->links[state->link_count], 0, sizeof(state->links[0]));
        if (state->next_link >= state->link_count)
            state->next_link = 0;
        return;
    }
}

bool
ppp_mppp_sender_send(ppp_mppp_sender_t *state, uint16_t protocol,
                     const uint8_t *data, size_t length)
{
    uint8_t *packet;
    size_t   packet_length;
    size_t   offset = 0;
    size_t   header_length;

    if (!state || state->link_count == 0 || (length > 0 && !data)
        || length > state->mrru - 2)
        return false;

    packet_length = length + 2;
    packet = (uint8_t *) malloc(packet_length);
    if (!packet)
        return false;
    packet[0] = (uint8_t) (protocol >> 8);
    packet[1] = (uint8_t) protocol;
    if (length > 0)
        memcpy(packet + 2, data, length);

    header_length = state->sequence_bits == 12 ? 2 : 4;
    while (offset < packet_length) {
        ppp_mppp_tx_link_t *link = &state->links[state->next_link];
        size_t fragment_length = packet_length - offset;
        if (fragment_length > link->max_fragment_payload)
            fragment_length = link->max_fragment_payload;

        uint8_t *fragment = (uint8_t *) malloc(header_length + fragment_length);
        if (!fragment) {
            free(packet);
            return false;
        }

        bool begin = offset == 0;
        bool end = offset + fragment_length == packet_length;
        bool encoded = ppp_mppp_encode_header(fragment, header_length,
                                               state->sequence_bits == 12,
                                               state->next_sequence, begin, end);
        if (encoded)
            memcpy(fragment + header_length, packet + offset, fragment_length);

        bool sent = encoded && link->send(link->opaque, fragment,
                                           header_length + fragment_length);
        free(fragment);
        if (!sent) {
            free(packet);
            return false;
        }

        state->next_sequence = (state->next_sequence + 1) & state->sequence_mask;
        state->next_link = (state->next_link + 1) % state->link_count;
        offset += fragment_length;
    }

    free(packet);
    return true;
}
