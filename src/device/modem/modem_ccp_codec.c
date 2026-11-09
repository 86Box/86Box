/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PPP CCP data codecs.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_mppe.h>

#define PREDICTOR_TABLE_SIZE 65536
#define PREDICTOR_HEADER_SIZE 2
#define PREDICTOR_TRAILER_SIZE 2
#define PREDICTOR2_STREAM_CAPACITY (PPP_MAX_FRAME * 3)
#define MPPC_HISTORY_SIZE 8192
#define MPPC_HISTORY_MASK (MPPC_HISTORY_SIZE - 1)
#define MPPC_FLUSHED PPP_MPPE_FLAG_FLUSHED
#define MPPC_AT_FRONT PPP_MPPE_FLAG_AT_FRONT
#define MPPC_COMPRESSED PPP_MPPE_FLAG_COMPRESSED
#define MPPC_RESERVED 0x10
#define LZS_WINDOW_SIZE 2047
#define LZS_EXTENDED_PROTOCOL PPP_PROTO_MPPE
#define LZS_EXTENDED_FLUSHED 0x80
#define LZS_EXTENDED_COMPRESSED 0x20
#define V44_CODEWORD_LIMIT 1525
#define V44_FIRST_CODEWORD 4
#define V44_MAX_STRING_LENGTH 255
#define V44_INVALID_INDEX UINT16_MAX
#define NT31RAS_DEFAULT_WINDOW_SIZE CCP_NT31RAS_WINDOW_8K
#define NT31RAS_MAX_WINDOW_SIZE 65536
#define NT31RAS_HASH_SIZE 65536

typedef struct {
    uint16_t hash;
    uint8_t *table;
} predictor_state_t;

typedef enum {
    PREDICTOR2_RECORD_INVALID,
    PREDICTOR2_RECORD_INCOMPLETE,
    PREDICTOR2_RECORD_COMPLETE
} predictor2_record_result_t;

typedef struct {
    uint8_t tx_history[MPPC_HISTORY_SIZE];
    uint8_t rx_history[MPPC_HISTORY_SIZE];
    uint16_t tx_position;
    uint16_t rx_position;
    uint16_t tx_history_length;
    uint16_t rx_history_length;
    uint16_t tx_count;
    uint16_t rx_count;
    bool tx_flush;
    bool rx_flush;
} mppc_state_t;

typedef struct {
    uint8_t *history;
    uint32_t current;
    uint32_t history_length;
    uint32_t window_size;
    uint32_t window_mask;
    int32_t *hash_heads;
    int32_t *hash_next;
    bool flushed;
    bool last_frame_flushed;
} nt31ras_state_t;

typedef struct {
    uint16_t *prefix;
    uint8_t *suffix;
    uint32_t *hash_keys;
    uint16_t *hash_codes;
    uint32_t hash_size;
    uint16_t next_code;
    uint8_t code_bits;
    uint8_t max_bits;
    uint16_t tx_sequence;
    uint16_t rx_sequence;
} bsd_state_t;

typedef struct {
    uint16_t history_position;
    uint16_t length;
    uint16_t down_index;
    uint16_t side_index;
} v44_node_t;

typedef struct {
    uint16_t root_down[256];
    v44_node_t nodes[V44_CODEWORD_LIMIT];
    uint8_t history[PPP_MAX_FRAME];
    uint16_t next_codeword;
    uint8_t codeword_bits;
    uint8_t ordinal_bits;
} v44_state_t;

typedef struct {
    uint8_t tx_history[LZS_WINDOW_SIZE];
    uint8_t rx_history[LZS_WINDOW_SIZE];
    uint16_t tx_history_length;
    uint16_t rx_history_length;
    uint16_t tx_count;
    uint16_t rx_count;
    bool tx_flush;
    bool tx_last_frame_flushed;
} lzs_extended_state_t;

typedef struct {
    uint8_t method;
    predictor_state_t predictor;
    z_stream deflater;
    z_stream inflater;
    mppc_state_t mppc;
    nt31ras_state_t nt31ras;
    bsd_state_t bsd;
    v44_state_t v44;
    lzs_extended_state_t lzs_extended;
    uint16_t tx_sequence;
    uint16_t rx_sequence;
    uint16_t lzs_history_count;
    uint8_t lzs_check_mode;
    uint8_t predictor2_rx_stream[PREDICTOR2_STREAM_CAPACITY];
    size_t predictor2_rx_stream_len;
    bool deflater_ready;
    bool inflater_ready;
} ppp_ccp_codec_state_t;

typedef struct {
    uint8_t *data;
    int bit_position;
    int capacity;
} nt31ras_bit_writer_t;

static bool
nt31ras_write_bits(nt31ras_bit_writer_t *writer, uint32_t value, uint8_t bit_count)
{
    if (writer->bit_position + bit_count > writer->capacity * 8)
        return false;

    for (uint8_t bit = bit_count; bit > 0; bit--) {
        if ((value >> (bit - 1)) & 1)
            writer->data[writer->bit_position >> 3] |=
                (uint8_t) (1u << (7 - (writer->bit_position & 7)));
        writer->bit_position++;
    }
    return true;
}

static bool
nt31ras_read_bits(const uint8_t *data, int bit_limit, int *bit_position,
                  uint8_t bit_count, uint32_t *value)
{
    if (*bit_position + bit_count > bit_limit)
        return false;

    *value = 0;
    for (uint8_t bit = 0; bit < bit_count; bit++) {
        *value = (*value << 1)
               | ((data[*bit_position >> 3] >> (7 - (*bit_position & 7))) & 1);
        (*bit_position)++;
    }
    return true;
}

static uint16_t
nt31ras_crc16(const uint8_t *data, int length)
{
    uint16_t crc = 0;

    for (int position = 0; position < length; position++) {
        crc ^= (uint16_t) data[position] << 8;
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) ? (uint16_t) ((crc << 1) ^ 0x1021)
                                 : (uint16_t) (crc << 1);
    }
    return crc;
}

static void
nt31ras_history_write(nt31ras_state_t *state, uint8_t value)
{
    state->history[state->current & state->window_mask] = value;
    state->current = (state->current + 1) & state->window_mask;
    if (state->history_length < state->window_mask)
        state->history_length++;
}

static bool
nt31ras_emit(nt31ras_state_t *state, uint8_t *output, int output_capacity,
             int *output_length, uint8_t value)
{
    if (*output_length >= output_capacity)
        return false;
    output[(*output_length)++] = value;
    nt31ras_history_write(state, value);
    return true;
}

static bool
nt31ras_find_match(const nt31ras_state_t *state, const uint8_t *frame,
                   int position, int frame_length, uint16_t *best_offset,
                   uint16_t *best_length, uint32_t history_length_start,
                   uint32_t frame_start_current, int *indexed_frame_bytes)
{
    int remaining = frame_length - position;
    int maximum_offset = state->history_length;
    int longest = 1;
    int offset_for_longest = 0;

    if (remaining < 2) {
        *best_offset = 0;
        *best_length = 0;
        return false;
    }
    if (remaining > 1024)
        remaining = 1024;

    if (maximum_offset > (int) state->window_mask)
        maximum_offset = (int) state->window_mask;

    if (state->window_size > NT31RAS_DEFAULT_WINDOW_SIZE) {
        int32_t candidate;
        uint32_t current_sequence = history_length_start + (uint32_t) position;
        uint16_t key;

        while (*indexed_frame_bytes < position) {
            uint32_t frame_byte = (uint32_t) *indexed_frame_bytes;
            uint32_t sequence = history_length_start + frame_byte - 1;
            uint8_t first = sequence < history_length_start
                ? state->history[(frame_start_current - history_length_start + sequence)
                                 & state->window_mask]
                : frame[sequence - history_length_start];
            uint8_t second = frame[frame_byte];
            key = (uint16_t) (((uint16_t) first << 8) | second);
            state->hash_next[sequence] = state->hash_heads[key];
            state->hash_heads[key] = (int32_t) sequence;
            (*indexed_frame_bytes)++;
        }

        key = (uint16_t) (((uint16_t) frame[position] << 8) | frame[position + 1]);
        candidate = state->hash_heads[key];
        while (candidate >= 0) {
            uint32_t candidate_sequence = (uint32_t) candidate;
            uint32_t offset = current_sequence - candidate_sequence;
            if (offset > state->window_mask)
                break;
            if (offset >= 2) {
                int length = 0;
                while (length < remaining) {
                    uint32_t source_sequence = candidate_sequence + (uint32_t) length;
                    uint8_t previous = source_sequence < history_length_start
                        ? state->history[(frame_start_current - history_length_start
                                         + source_sequence) & state->window_mask]
                        : frame[source_sequence - history_length_start];
                    if (frame[position + length] != previous)
                        break;
                    length++;
                }
                if (length > longest) {
                    longest = length;
                    offset_for_longest = (int) offset;
                }
                if (longest == remaining)
                    break;
            }
            candidate = state->hash_next[candidate_sequence];
        }
    } else {
        for (int offset = 2; offset <= maximum_offset; offset++) {
            int length = 0;
            while (length < remaining) {
                uint8_t previous = length < offset
                    ? state->history[((int) state->current - offset + length)
                                     & state->window_mask]
                    : frame[position + length - offset];
                if (frame[position + length] != previous)
                    break;
                length++;
            }
            if (length > longest) {
                longest = length;
                offset_for_longest = offset;
            }
            if (longest == remaining)
                break;
        }
    }

    if (longest < (offset_for_longest > 8512 ? 4 : 2)) {
        *best_offset = 0;
        *best_length = 0;
        return false;
    }

    *best_offset = (uint16_t) offset_for_longest;
    *best_length = (uint16_t) (longest > 1 ? longest : 0);
    return *best_length >= 2;
}

static bool
nt31ras_write_literal(nt31ras_bit_writer_t *writer, uint8_t literal)
{
    return nt31ras_write_bits(writer, literal < 0x80 ? 1u : 2u, 2)
        && nt31ras_write_bits(writer, literal & 0x7F, 7);
}

static bool
nt31ras_write_match_length(nt31ras_bit_writer_t *writer, uint16_t length)
{
    if (length == 2)
        return nt31ras_write_bits(writer, 1, 1);
    if (length == 3)
        return nt31ras_write_bits(writer, 2, 3);
    if (length == 4)
        return nt31ras_write_bits(writer, 3, 3);
    if (length <= 8)
        return nt31ras_write_bits(writer, 1, 3)
            && nt31ras_write_bits(writer, length - 5, 2);

    uint8_t width = 3;
    uint16_t base = 9;
    while (width <= 10) {
        if (length < base + (1u << width))
            return nt31ras_write_bits(writer, 0, width)
                && nt31ras_write_bits(writer, 1, 1)
                && nt31ras_write_bits(writer, length - base, width);
        base = (uint16_t) (base + (1u << width));
        width++;
    }
    return false;
}

static bool
nt31ras_write_copy_item(nt31ras_bit_writer_t *writer, uint16_t offset,
                        uint16_t length, uint32_t window_size)
{
    if (offset < 66) {
        if (!nt31ras_write_bits(writer, 0, 2)
            || !nt31ras_write_bits(writer, offset - 2, 6))
            return false;
    } else if (offset < 322) {
        if (!nt31ras_write_bits(writer, 6, 3)
            || !nt31ras_write_bits(writer, offset - 66, 8))
            return false;
    } else if (offset <= 8512 || window_size == NT31RAS_DEFAULT_WINDOW_SIZE) {
        if (!nt31ras_write_bits(writer, 7, 3)
            || !nt31ras_write_bits(writer, offset - 322, 13))
            return false;
    } else {
        if (!nt31ras_write_bits(writer, 7, 3)
            || !nt31ras_write_bits(writer, 8191, 13)
            || !nt31ras_write_bits(writer, offset - 8513,
                                   window_size == CCP_NT31RAS_WINDOW_16K ? 13
                                   : window_size == CCP_NT31RAS_WINDOW_32K ? 15 : 16))
            return false;
    }
    return nt31ras_write_match_length(writer, length);
}

static bool
nt31ras_compress_source_format(ppp_ccp_codec_state_t *codec, const uint8_t *input,
                               int input_length, uint8_t *output, int output_capacity,
                               int *output_length)
{
    nt31ras_state_t *state = &codec->nt31ras;
    uint8_t frame[PPP_MAX_FRAME + 2];
    nt31ras_bit_writer_t writer = { output, 0, output_capacity };
    int frame_length;
    uint32_t history_length_start;
    uint32_t frame_start_current;
    int indexed_frame_bytes = 1;

    if (!input || !output || !output_length || input_length <= 0
        || input_length > PPP_MAX_FRAME || output_capacity <= 0)
        return false;

    frame_length = input_length + 2;
    memcpy(frame, input, (size_t) input_length);
    uint16_t crc = nt31ras_crc16(input, input_length);
    frame[input_length] = (uint8_t) crc;
    frame[input_length + 1] = (uint8_t) (crc >> 8);

    bool restart = state->flushed
                || state->current + (uint32_t) frame_length > state->window_size;
    if (state->flushed) {
        memset(state->history, 0, state->window_size);
        state->history_length = 0;
    }
    if (restart)
        state->current = 1;
    history_length_start = state->history_length;
    frame_start_current = state->current;
    state->last_frame_flushed = state->flushed;

    if (state->window_size > NT31RAS_DEFAULT_WINDOW_SIZE) {
        memset(state->hash_heads, 0xFF, NT31RAS_HASH_SIZE * sizeof(*state->hash_heads));
        for (uint32_t sequence = 0; sequence + 1 < history_length_start; sequence++) {
            uint8_t first = state->history[(frame_start_current - history_length_start
                                            + sequence) & state->window_mask];
            uint8_t second = state->history[(frame_start_current - history_length_start
                                             + sequence + 1) & state->window_mask];
            uint16_t key = (uint16_t) (((uint16_t) first << 8) | second);
            state->hash_next[sequence] = state->hash_heads[key];
            state->hash_heads[key] = (int32_t) sequence;
        }
    }

    memset(output, 0, (size_t) output_capacity);
    if (!nt31ras_write_bits(&writer, restart ? 1u : 0u, 1))
        return false;

    for (int position = 0, consecutive_literals = 0; position < frame_length;) {
        uint16_t offset;
        uint16_t match_length;
        if (nt31ras_find_match(state, frame, position, frame_length,
                               &offset, &match_length, history_length_start,
                               frame_start_current, &indexed_frame_bytes)) {
            if (!nt31ras_write_copy_item(&writer, offset, match_length,
                                         state->window_size))
                return false;
            for (uint16_t index = 0; index < match_length; index++)
                nt31ras_history_write(state, frame[position + index]);
            position += match_length;
            consecutive_literals = 0;
            continue;
        }

        if (!nt31ras_write_literal(&writer, frame[position]))
            return false;
        nt31ras_history_write(state, frame[position++]);
        if (++consecutive_literals < 6)
            continue;

        for (;;) {
            int count_position = writer.bit_position;
            uint8_t count = 0;
            if (!nt31ras_write_bits(&writer, 0, 3))
                return false;
            while (position < frame_length && count < 7) {
                if (nt31ras_find_match(state, frame, position, frame_length,
                                       &offset, &match_length, history_length_start,
                                       frame_start_current, &indexed_frame_bytes))
                    break;
                if (!nt31ras_write_bits(&writer, frame[position], 8))
                    return false;
                nt31ras_history_write(state, frame[position++]);
                count++;
            }
            int end_position = writer.bit_position;
            writer.bit_position = count_position;
            bool count_written = nt31ras_write_bits(&writer, count, 3);
            writer.bit_position = end_position;
            if (!count_written)
                return false;
            if (count < 7) {
                if (position < frame_length
                    && nt31ras_find_match(state, frame, position, frame_length,
                                          &offset, &match_length, history_length_start,
                                          frame_start_current, &indexed_frame_bytes)) {
                    if (!nt31ras_write_copy_item(&writer, offset, match_length,
                                                 state->window_size))
                        return false;
                    for (uint16_t index = 0; index < match_length; index++)
                        nt31ras_history_write(state, frame[position + index]);
                    position += match_length;
                }
                break;
            }
        }
        consecutive_literals = 0;
    }

    state->flushed = false;
    *output_length = (writer.bit_position + 7) / 8;
    return *output_length <= output_capacity;
}

static bool
nt31ras_read_copy_length(const uint8_t *input, int bit_limit, int *bit_position,
                         uint16_t *length)
{
    uint32_t bit;
    if (!nt31ras_read_bits(input, bit_limit, bit_position, 1, &bit))
        return false;
    if (bit) {
        *length = 2;
        return true;
    }
    if (!nt31ras_read_bits(input, bit_limit, bit_position, 1, &bit))
        return false;
    if (bit) {
        if (!nt31ras_read_bits(input, bit_limit, bit_position, 1, &bit))
            return false;
        *length = bit ? 4 : 3;
        return true;
    }

    uint8_t width = 2;
    do {
        if (!nt31ras_read_bits(input, bit_limit, bit_position, 1, &bit))
            return false;
        if (bit)
            break;
        if (++width > 10)
            return false;
    } while (true);

    uint32_t remainder;
    if (!nt31ras_read_bits(input, bit_limit, bit_position, width, &remainder))
        return false;
    *length = (uint16_t) (((1u << width) + 1u) + remainder);
    return true;
}

static bool
nt31ras_decompress_source_format(ppp_ccp_codec_state_t *codec, const uint8_t *input,
                                 int input_length, uint8_t *output, int output_capacity,
                                 int *output_length)
{
    nt31ras_state_t *state = &codec->nt31ras;
    uint8_t decoded[PPP_MAX_FRAME + 2];
    uint32_t value;
    int bit_position = 0;
    int decoded_length = 0;
    uint8_t consecutive_literals = 0;

    if (!input || !output || !output_length || input_length < 2
        || input_length > PPP_MAX_FRAME || output_capacity < 0
        || !nt31ras_read_bits(input, input_length * 8, &bit_position, 1, &value))
        return false;

    if (value) {
        state->current = 1;
        if (state->flushed) {
            memset(state->history, 0, state->window_size);
            state->history_length = 0;
        }
    }

    int bit_limit = input_length * 8;
    while (bit_position < bit_limit) {
        int token_start = bit_position;
        uint32_t prefix;
        if (!nt31ras_read_bits(input, bit_limit, &bit_position, 3, &prefix))
            goto invalid_source;
        if (prefix >= 2 && prefix <= 5) {
            uint32_t low_bits;
            if (!nt31ras_read_bits(input, bit_limit, &bit_position, 6, &low_bits))
                goto invalid_source;
            uint8_t literal = (uint8_t) (((prefix - 2) << 6) | low_bits);
            if (!nt31ras_emit(state, decoded, (int) sizeof(decoded),
                              &decoded_length, literal))
                goto invalid_source;
            consecutive_literals++;
            if (consecutive_literals == 6) {
                uint32_t count;
                do {
                    if (!nt31ras_read_bits(input, bit_limit, &bit_position, 3, &count))
                        goto invalid_source;
                    for (uint32_t index = 0; index < count; index++) {
                        uint32_t raw;
                        if (!nt31ras_read_bits(input, bit_limit, &bit_position, 8, &raw)
                            || !nt31ras_emit(state, decoded, (int) sizeof(decoded),
                                             &decoded_length, (uint8_t) raw))
                            goto invalid_source;
                    }
                } while (count == 7);
                consecutive_literals = 0;
            }
        } else {
            uint16_t offset;
            if (prefix == 0 || prefix == 1) {
                uint32_t low;
                if (!nt31ras_read_bits(input, bit_limit, &bit_position, 5, &low))
                    goto invalid_source;
                offset = (uint16_t) (low + (prefix == 0 ? 2 : 34));
            } else if (prefix == 6) {
                if (!nt31ras_read_bits(input, bit_limit, &bit_position, 8, &value))
                    goto invalid_source;
                offset = (uint16_t) (value + 66);
            } else {
                if (!nt31ras_read_bits(input, bit_limit, &bit_position, 13, &value))
                    goto invalid_source;
                if (value == 8191 && state->window_size > NT31RAS_DEFAULT_WINDOW_SIZE) {
                    uint8_t extension_bits = state->window_size == CCP_NT31RAS_WINDOW_16K ? 13
                                           : state->window_size == CCP_NT31RAS_WINDOW_32K ? 15 : 16;
                    if (!nt31ras_read_bits(input, bit_limit, &bit_position,
                                           extension_bits, &value))
                        goto invalid_source;
                    if (value > state->window_mask - 8513)
                        goto invalid_source;
                    offset = (uint16_t) (value + 8513);
                } else {
                    offset = (uint16_t) (value + 322);
                }
            }

            uint16_t length;
            if (!nt31ras_read_copy_length(input, bit_limit, &bit_position, &length)
                || offset > state->window_mask
                || offset > state->history_length || length > sizeof(decoded) - decoded_length)
                goto invalid_source;
            for (uint16_t index = 0; index < length; index++) {
                uint8_t copied = state->history[((int) state->current - offset)
                                                 & state->window_mask];
                if (!nt31ras_emit(state, decoded, (int) sizeof(decoded),
                                  &decoded_length, copied))
                    goto invalid_source;
            }
            consecutive_literals = 0;
        }

        if (decoded_length >= 2 && bit_limit - bit_position < 8) {
            uint16_t expected_crc = (uint16_t) decoded[decoded_length - 2]
                                  | ((uint16_t) decoded[decoded_length - 1] << 8);
            if (nt31ras_crc16(decoded, decoded_length - 2) == expected_crc) {
                while (bit_position < bit_limit) {
                    if (!nt31ras_read_bits(input, bit_limit, &bit_position, 1, &value)
                        || value)
                        goto invalid_source;
                }
                if (decoded_length - 2 > output_capacity)
                    goto invalid_source;
                memcpy(output, decoded, (size_t) decoded_length - 2);
                *output_length = decoded_length - 2;
                state->flushed = false;
                return true;
            }
        }
        if (bit_position <= token_start)
            goto invalid_source;
    }

invalid_source:
    state->current = 1;
    state->history_length = 0;
    state->flushed = true;
    return false;
}

static uint16_t
predictor_crc16(const uint8_t *data, int length)
{
    uint16_t crc = 0xFFFF;

    for (int pos = 0; pos < length; pos++) {
        crc ^= data[pos];
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 1) ? (uint16_t) ((crc >> 1) ^ 0x8408) : (uint16_t) (crc >> 1);
    }
    return crc ^ 0xFFFF;
}

static ppp_ccp_codec_state_t *
ppp_ccp_get_codec_state(ppp_ctx_t *ctx, bool transmit)
{
    return (ppp_ccp_codec_state_t *) (transmit ? ctx->ccp_tx_codec_state
                                               : ctx->ccp_rx_codec_state);
}

static void
ppp_ccp_codec_state_free(ppp_ccp_codec_state_t *state)
{
    if (!state)
        return;
    free(state->predictor.table);
    free(state->nt31ras.history);
    free(state->nt31ras.hash_heads);
    free(state->nt31ras.hash_next);
    free(state->bsd.prefix);
    free(state->bsd.suffix);
    free(state->bsd.hash_keys);
    free(state->bsd.hash_codes);
    if (state->deflater_ready)
        deflateEnd(&state->deflater);
    if (state->inflater_ready)
        inflateEnd(&state->inflater);
    free(state);
}

static void
mppc_write_bits(uint8_t *output, int *bit_pos, uint32_t value, uint8_t bit_count)
{
    for (uint8_t bit = bit_count; bit > 0; bit--) {
        if ((value >> (bit - 1)) & 1)
            output[*bit_pos >> 3] |= (uint8_t) (1u << (7 - (*bit_pos & 7)));
        (*bit_pos)++;
    }
}

static bool
mppc_read_bits(const uint8_t *input, int input_bits, int *bit_pos,
               uint8_t bit_count, uint32_t *value)
{
    if (*bit_pos + bit_count > input_bits)
        return false;
    *value = 0;
    for (uint8_t bit = 0; bit < bit_count; bit++) {
        *value = (*value << 1)
               | ((input[*bit_pos >> 3] >> (7 - (*bit_pos & 7))) & 1);
        (*bit_pos)++;
    }
    return true;
}

static void
lzs_write_length(uint8_t *output, int *bit_pos, uint16_t length)
{
    if (length <= 4) {
        mppc_write_bits(output, bit_pos, length - 2, 2);
    } else if (length <= 7) {
        mppc_write_bits(output, bit_pos, 12 + length - 5, 4);
    } else {
        uint16_t remainder = (uint16_t) (length - 8);
        mppc_write_bits(output, bit_pos, 15, 4);
        while (remainder >= 15) {
            mppc_write_bits(output, bit_pos, 15, 4);
            remainder = (uint16_t) (remainder - 15);
        }
        mppc_write_bits(output, bit_pos, remainder, 4);
    }
}

static bool
lzs_read_length(const uint8_t *input, int input_bits, int *bit_pos, uint16_t *length)
{
    uint32_t value;

    if (!mppc_read_bits(input, input_bits, bit_pos, 2, &value))
        return false;
    if (value < 3) {
        *length = (uint16_t) (value + 2);
        return true;
    }
    if (!mppc_read_bits(input, input_bits, bit_pos, 2, &value))
        return false;
    if (value < 3) {
        *length = (uint16_t) (value + 5);
        return true;
    }

    uint32_t decoded_length = 8;
    do {
        if (!mppc_read_bits(input, input_bits, bit_pos, 4, &value)
            || decoded_length > PPP_MAX_FRAME - value)
            return false;
        decoded_length += value;
    } while (value == 15);
    *length = (uint16_t) decoded_length;
    return true;
}

static bool
lzs_compress_with_history(const uint8_t *history, int history_len,
                          const uint8_t *input, int input_len, uint8_t *output,
                          int output_capacity, int *output_len)
{
    uint8_t source[LZS_WINDOW_SIZE + PPP_MAX_FRAME];
    uint8_t encoded[PPP_MAX_FRAME * 2];
    int bit_pos = 0;

    if ((!history && history_len != 0) || history_len < 0
        || history_len > LZS_WINDOW_SIZE || !input || !output || !output_len
        || input_len < 0 || input_len > PPP_MAX_FRAME || output_capacity < 0)
        return false;
    if (history_len > 0)
        memcpy(source, history, (size_t) history_len);
    if (input_len > 0)
        memcpy(source + history_len, input, (size_t) input_len);
    memset(encoded, 0, sizeof(encoded));

    for (int pos = history_len; pos < history_len + input_len;) {
        int best_length = 0;
        int best_offset = 0;
        int max_offset = pos < LZS_WINDOW_SIZE ? pos : LZS_WINDOW_SIZE;

        for (int offset = 1; offset <= max_offset; offset++) {
            int length = 0;
            while (pos + length < history_len + input_len
                   && source[pos + length] == source[pos + length - offset])
                length++;
            if (length > best_length) {
                best_length = length;
                best_offset = offset;
            }
            if (best_length == input_len - pos)
                break;
        }

        if (best_length >= 2) {
            mppc_write_bits(encoded, &bit_pos, 1, 1);
            if (best_offset <= 127) {
                mppc_write_bits(encoded, &bit_pos, 1, 1);
                mppc_write_bits(encoded, &bit_pos, (uint32_t) best_offset, 7);
            } else {
                mppc_write_bits(encoded, &bit_pos, 0, 1);
                mppc_write_bits(encoded, &bit_pos, (uint32_t) best_offset, 11);
            }
            lzs_write_length(encoded, &bit_pos, (uint16_t) best_length);
            pos += best_length;
        } else {
            mppc_write_bits(encoded, &bit_pos, 0, 1);
            mppc_write_bits(encoded, &bit_pos, source[pos++], 8);
        }
    }

    mppc_write_bits(encoded, &bit_pos, 1, 1);
    mppc_write_bits(encoded, &bit_pos, 1, 1);
    mppc_write_bits(encoded, &bit_pos, 0, 7);
    *output_len = (bit_pos + 7) / 8;
    if (*output_len > output_capacity)
        return false;
    memcpy(output, encoded, (size_t) *output_len);
    return true;
}

static bool
lzs_compress(const uint8_t *input, int input_len, uint8_t *output,
             int output_capacity, int *output_len)
{
    return lzs_compress_with_history(NULL, 0, input, input_len, output,
                                     output_capacity, output_len);
}

static bool
lzs_decompress(const uint8_t *input, int input_len, uint8_t *output,
               int output_capacity, int *output_len)
{
    int bit_pos = 0;
    int input_bits;

    if (!input || !output || !output_len || input_len <= 0
        || input_len > PPP_MAX_FRAME + 1 || output_capacity < 0)
        return false;
    input_bits = input_len * 8;

    for (;;) {
        uint32_t flag;
        if (!mppc_read_bits(input, input_bits, &bit_pos, 1, &flag))
            return false;
        if (flag == 0) {
            uint32_t value;
            if (!mppc_read_bits(input, input_bits, &bit_pos, 8, &value)
                || *output_len >= output_capacity)
                return false;
            output[(*output_len)++] = (uint8_t) value;
            continue;
        }

        uint32_t short_offset;
        uint32_t offset;
        if (!mppc_read_bits(input, input_bits, &bit_pos, 1, &short_offset))
            return false;
        if (short_offset) {
            if (!mppc_read_bits(input, input_bits, &bit_pos, 7, &offset))
                return false;
            if (offset == 0)
                return true;
        } else if (!mppc_read_bits(input, input_bits, &bit_pos, 11, &offset)) {
            return false;
        }

        uint16_t length;
        if (offset == 0 || offset > LZS_WINDOW_SIZE
            || offset > (uint32_t) *output_len
            || !lzs_read_length(input, input_bits, &bit_pos, &length)
            || length > output_capacity - *output_len)
            return false;
        for (uint16_t index = 0; index < length; index++) {
            output[*output_len] = output[*output_len - offset];
            (*output_len)++;
        }
    }
}

/* RFC 1974 section 2.2 and RFC 1967 section 3.2 restore a trailing zero
   before decoding because a sender may remove trailing zero octets. */
static bool
lzs_decompress_with_zero_padding(const uint8_t *input, int input_len,
                                 uint8_t *output, int output_capacity,
                                 int *output_len)
{
    uint8_t padded[PPP_MAX_FRAME + 1];

    if (!input || input_len <= 0 || input_len > PPP_MAX_FRAME || !output
        || output_capacity < 0 || !output_len)
        return false;

    memcpy(padded, input, (size_t) input_len);
    padded[input_len] = 0;
    *output_len = 0;
    return lzs_decompress(padded, input_len + 1, output, output_capacity,
                          output_len);
}

static bool
lzs_decompress_with_history(const uint8_t *input, int input_len,
                            const uint8_t *history, int history_len,
                            uint8_t *output, int output_capacity, int *output_len)
{
    uint8_t combined[LZS_WINDOW_SIZE + PPP_MAX_FRAME];
    int combined_len = history_len;

    if (!input || (!history && history_len != 0)
        || history_len < 0 || history_len > LZS_WINDOW_SIZE
        || !output || !output_len || output_capacity < 0)
        return false;
    if (history_len > 0)
        memcpy(combined, history, (size_t) history_len);
    if (!lzs_decompress(input, input_len, combined, sizeof(combined), &combined_len)
        || combined_len < history_len || combined_len - history_len > output_capacity)
        return false;

    *output_len = combined_len - history_len;
    if (*output_len > 0)
        memcpy(output, combined + history_len, (size_t) *output_len);
    return true;
}

static void
lzs_extended_history_append(uint8_t *history, uint16_t *history_length,
                            const uint8_t *input, int input_len)
{
    if (input_len >= LZS_WINDOW_SIZE) {
        memcpy(history, input + input_len - LZS_WINDOW_SIZE, LZS_WINDOW_SIZE);
        *history_length = LZS_WINDOW_SIZE;
        return;
    }

    int excess = (int) *history_length + input_len - LZS_WINDOW_SIZE;
    if (excess > 0) {
        memmove(history, history + excess, (size_t) (*history_length - excess));
        *history_length = (uint16_t) (*history_length - excess);
    }
    if (input_len > 0) {
        memcpy(history + *history_length, input, (size_t) input_len);
        *history_length = (uint16_t) (*history_length + input_len);
    }
}

static bool
lzs_standard_compress(ppp_ccp_codec_state_t *codec, const uint8_t *input,
                       int input_len, uint8_t *output, int output_capacity,
                       int *output_len)
{
    lzs_extended_state_t *state = &codec->lzs_extended;
    uint8_t compressed[PPP_MAX_FRAME * 2];
    int compressed_len;
    int header_len = codec->lzs_check_mode == 2 ? 2
                   : (codec->lzs_check_mode == 1 || codec->lzs_check_mode == 3) ? 1 : 0;
    bool keep_history = codec->lzs_history_count != 0 && codec->lzs_check_mode != 0;
    uint16_t check_value = 0;
    const uint8_t *history = !keep_history
                           ? NULL : state->tx_history;
    int history_len = !keep_history
                    ? 0 : state->tx_history_length;

    if (!input || input_len <= 0 || input_len > PPP_MAX_FRAME || !output
        || !output_len || output_capacity < header_len + 1
        || !lzs_compress_with_history(history, history_len,
                                      input, input_len, compressed,
                                      sizeof(compressed), &compressed_len)
        || compressed_len + header_len > output_capacity)
        return false;

    if (codec->lzs_check_mode == 1) {
        check_value = 0xFF;
        for (int index = 0; index < input_len; index++)
            check_value ^= input[index];
        output[0] = (uint8_t) check_value;
    } else if (codec->lzs_check_mode == 2) {
        check_value = predictor_crc16(input, input_len);
        output[0] = (uint8_t) check_value;
        output[1] = (uint8_t) (check_value >> 8);
    } else if (codec->lzs_check_mode == 3) {
        output[0] = (uint8_t) codec->tx_sequence;
    }
    memcpy(output + header_len, compressed, (size_t) compressed_len);
    *output_len = compressed_len + header_len;

    if (compressed_len < input_len) {
        if (keep_history)
            lzs_extended_history_append(state->tx_history, &state->tx_history_length,
                                        input, input_len);
        if (codec->lzs_check_mode == 3)
            codec->tx_sequence = (uint8_t) (codec->tx_sequence + 1);
    } else {
        state->tx_history_length = 0;
    }
    return true;
}

static bool
lzs_standard_decompress(ppp_ccp_codec_state_t *codec, const uint8_t *input,
                         int input_len, uint8_t *output, int output_capacity,
                         int *output_len)
{
    lzs_extended_state_t *state = &codec->lzs_extended;
    uint8_t padded[PPP_MAX_FRAME + 1];
    int header_len = codec->lzs_check_mode == 2 ? 2
                   : (codec->lzs_check_mode == 1 || codec->lzs_check_mode == 3) ? 1 : 0;
    bool keep_history = codec->lzs_history_count != 0 && codec->lzs_check_mode != 0;
    int decoded_len;
    uint16_t received_check = 0;
    uint16_t calculated_check = 0;
    const uint8_t *history = !keep_history
                           ? NULL : state->rx_history;
    int history_len = !keep_history
                    ? 0 : state->rx_history_length;

    if (!input || input_len <= header_len || input_len > PPP_MAX_FRAME || !output
        || !output_len || output_capacity < 0)
        return false;
    if (codec->lzs_check_mode == 1)
        received_check = input[0];
    else if (codec->lzs_check_mode == 2)
        received_check = (uint16_t) input[0] | ((uint16_t) input[1] << 8);
    else if (codec->lzs_check_mode == 3
             && input[0] != (uint8_t) codec->rx_sequence)
        return false;

    memcpy(padded, input + header_len, (size_t) input_len - (size_t) header_len);
    padded[input_len - header_len] = 0;
    if (!lzs_decompress_with_history(padded, input_len - header_len + 1, history,
                                     history_len, output, output_capacity,
                                     &decoded_len))
        return false;

    if (codec->lzs_check_mode == 1) {
        calculated_check = 0xFF;
        for (int index = 0; index < decoded_len; index++)
            calculated_check ^= output[index];
    } else if (codec->lzs_check_mode == 2) {
        calculated_check = predictor_crc16(output, decoded_len);
    }
    if ((codec->lzs_check_mode == 1 || codec->lzs_check_mode == 2)
        && received_check != calculated_check)
        return false;

    if (keep_history)
        lzs_extended_history_append(state->rx_history, &state->rx_history_length,
                                    output, decoded_len);
    if (codec->lzs_check_mode == 3)
        codec->rx_sequence = (uint8_t) (codec->rx_sequence + 1);
    *output_len = decoded_len;
    return true;
}

static bool
lzs_extended_compress(ppp_ccp_codec_state_t *codec, const uint8_t *input,
                      int input_len, uint8_t *output, int output_capacity,
                      int *output_len)
{
    lzs_extended_state_t *state = &codec->lzs_extended;
    uint8_t compressed[PPP_MAX_FRAME * 2];
    int compressed_len;
    bool flushed = state->tx_flush;

    if (!input || input_len <= 0 || input_len > PPP_MAX_FRAME || !output
        || !output_len || output_capacity < 2
        || !lzs_compress_with_history(flushed ? NULL : state->tx_history,
                                      flushed ? 0 : state->tx_history_length,
                                      input, input_len, compressed,
                                      sizeof(compressed), &compressed_len))
        return false;

    bool use_compressed = compressed_len < input_len;
    int payload_len = use_compressed ? compressed_len : input_len;
    if (payload_len > output_capacity - 2)
        return false;

    uint16_t header = (uint16_t) ((flushed || !use_compressed
                                   ? 0x8000 : 0)
                                  | (use_compressed ? 0x2000 : 0)
                                  | (state->tx_count & 0x0FFF));
    output[0] = (uint8_t) (header >> 8);
    output[1] = (uint8_t) header;
    memcpy(output + 2, use_compressed ? compressed : input, (size_t) payload_len);
    *output_len = payload_len + 2;

    state->tx_last_frame_flushed = flushed || !use_compressed;
    if (use_compressed)
        lzs_extended_history_append(state->tx_history, &state->tx_history_length,
                                    input, input_len);
    else
        state->tx_history_length = 0;
    state->tx_flush = false;
    state->tx_count = (uint16_t) ((state->tx_count + 1) & 0x0FFF);
    return true;
}

static bool
lzs_extended_decompress(ppp_ctx_t *ctx, ppp_ccp_codec_state_t *codec,
                        const uint8_t *input, int input_len, uint8_t *output,
                        int output_capacity, int *output_len)
{
    lzs_extended_state_t *state = &codec->lzs_extended;
    if (!input || input_len < 2 || input_len > PPP_MAX_FRAME || !output
        || !output_len || output_capacity < 0
        || (input[0] & 0x50) != 0)
        return false;

    bool flushed = (input[0] & LZS_EXTENDED_FLUSHED) != 0;
    bool compressed = (input[0] & LZS_EXTENDED_COMPRESSED) != 0;
    uint16_t count = (uint16_t) (((uint16_t) (input[0] & 0x0F) << 8) | input[1]);
    if (ctx->ccp_reset_pending && !flushed)
        return false;
    if (!flushed && count != state->rx_count)
        return false;
    if (!compressed && !flushed)
        return false;

    if (flushed) {
        state->rx_history_length = 0;
        state->rx_count = count;
    }

    int payload_len = input_len - 2;
    if (compressed) {
        if (!lzs_decompress_with_history(input + 2, payload_len,
                                         state->rx_history, state->rx_history_length,
                                         output, output_capacity, output_len))
            return false;
        lzs_extended_history_append(state->rx_history, &state->rx_history_length,
                                    output, *output_len);
    } else {
        if (payload_len < 2 || payload_len > output_capacity)
            return false;
        memcpy(output, input + 2, (size_t) payload_len);
        *output_len = payload_len;
    }

    state->rx_count = (uint16_t) ((count + 1) & 0x0FFF);
    ctx->ccp_reset_pending = false;
    return true;
}

static bool
lzs_dcp_compress(const uint8_t *input, int input_len, uint8_t *output,
                 int output_capacity, int *output_len)
{
    uint8_t encoded[PPP_MAX_FRAME * 2];
    int encoded_len;

    if (!input || !output || !output_len || input_len < 0
        || input_len > PPP_MAX_FRAME || output_capacity < 1
        || !lzs_compress(input, input_len, encoded, sizeof(encoded), &encoded_len))
        return false;

    if (encoded_len < input_len && encoded_len + 1 <= output_capacity) {
        output[0] = 0xE0;
        memcpy(output + 1, encoded, (size_t) encoded_len);
        *output_len = encoded_len + 1;
    } else if (input_len + 1 <= output_capacity) {
        output[0] = 0xA0;
        if (input_len > 0)
            memcpy(output + 1, input, (size_t) input_len);
        *output_len = input_len + 1;
    } else {
        return false;
    }

    return true;
}

static bool
lzs_dcp_decompress(const uint8_t *input, int input_len, uint8_t *output,
                   int output_capacity, int *output_len)
{
    uint8_t header;

    if (!input || !output || !output_len || input_len < 1 || input_len > PPP_MAX_FRAME
        || output_capacity < 0)
        return false;

    header = input[0];
    if ((header & 0xBF) != 0xA0)
        return false;

    if (header & 0x40) {
        return lzs_decompress_with_zero_padding(input + 1, input_len - 1,
                                                output, output_capacity,
                                                output_len);
    }

    if (input_len - 1 > output_capacity)
        return false;
    memcpy(output, input + 1, (size_t) (input_len - 1));
    *output_len = input_len - 1;
    return true;
}

static void
mppc_history_reset(uint8_t *history, uint16_t *position, uint16_t *history_length)
{
    memset(history, 0, MPPC_HISTORY_SIZE);
    *position = 0;
    *history_length = 0;
}

static void
mppc_history_move_to_front(uint8_t *history, uint16_t *position, uint16_t history_length)
{
    uint8_t ordered_history[MPPC_HISTORY_SIZE];
    size_t start = ((size_t) *position + MPPC_HISTORY_SIZE - history_length) & MPPC_HISTORY_MASK;

    for (size_t index = 0; index < history_length; index++)
        ordered_history[index] = history[(start + index) & MPPC_HISTORY_MASK];
    memcpy(history + MPPC_HISTORY_SIZE - history_length, ordered_history, history_length);
    *position = 0;
}

static void
mppc_history_write(uint8_t *history, uint16_t *position, uint16_t *history_length,
                   uint8_t value)
{
    history[*position] = value;
    *position = (uint16_t) ((*position + 1) & MPPC_HISTORY_MASK);
    if (*history_length < MPPC_HISTORY_SIZE)
        (*history_length)++;
}

static uint8_t
mppc_history_read(const uint8_t *history, uint16_t position, uint16_t offset)
{
    size_t index = ((size_t) position + MPPC_HISTORY_SIZE - offset) & MPPC_HISTORY_MASK;
    return history[index];
}

static void
mppc_write_match(uint8_t *output, int *bit_pos, uint16_t offset, uint16_t length)
{
    if (offset < 64) {
        mppc_write_bits(output, bit_pos, 0xF, 4);
        mppc_write_bits(output, bit_pos, offset, 6);
    } else if (offset < 320) {
        mppc_write_bits(output, bit_pos, 0xE, 4);
        mppc_write_bits(output, bit_pos, offset - 64, 8);
    } else {
        mppc_write_bits(output, bit_pos, 0x6, 3);
        mppc_write_bits(output, bit_pos, offset - 320, 13);
    }

    if (length == 3) {
        mppc_write_bits(output, bit_pos, 0, 1);
    } else {
        uint8_t value_bits = 0;
        for (uint16_t value = length; value > 1; value >>= 1)
            value_bits++;
        for (uint8_t bit = 1; bit < value_bits; bit++)
            mppc_write_bits(output, bit_pos, 1, 1);
        mppc_write_bits(output, bit_pos, 0, 1);
        mppc_write_bits(output, bit_pos, length, value_bits);
    }
}

static bool
mppc_read_match(const uint8_t *input, int input_bits, int *bit_pos,
                uint16_t *offset, uint16_t *length)
{
    uint32_t bit;
    uint32_t value;
    if (!mppc_read_bits(input, input_bits, bit_pos, 1, &bit) || bit == 0)
        return false;
    if (!mppc_read_bits(input, input_bits, bit_pos, 1, &bit))
        return false;
    if (bit == 0) {
        *offset = 0;
        *length = 0;
        return true;
    }

    if (!mppc_read_bits(input, input_bits, bit_pos, 1, &bit))
        return false;
    if (bit != 0) {
        if (!mppc_read_bits(input, input_bits, bit_pos, 1, &bit))
            return false;
        if (bit == 0) {
            if (!mppc_read_bits(input, input_bits, bit_pos, 8, &value))
                return false;
            *offset = (uint16_t) (value + 64);
        } else {
            if (!mppc_read_bits(input, input_bits, bit_pos, 6, &value))
                return false;
            *offset = (uint16_t) value;
        }
    } else {
        if (!mppc_read_bits(input, input_bits, bit_pos, 13, &value))
            return false;
        *offset = (uint16_t) (value + 320);
    }

    if (!mppc_read_bits(input, input_bits, bit_pos, 1, &bit))
        return false;
    if (bit == 0) {
        *length = 3;
        return true;
    }
    uint8_t value_bits = 2;
    while (true) {
        if (!mppc_read_bits(input, input_bits, bit_pos, 1, &bit))
            return false;
        if (bit == 0)
            break;
        value_bits++;
        if (value_bits > 12)
            return false;
    }
    if (!mppc_read_bits(input, input_bits, bit_pos, value_bits, &value))
        return false;
    *length = (uint16_t) ((1u << value_bits) | value);
    return true;
}

static bool
bsd_find_code(const bsd_state_t *state, uint16_t prefix, uint8_t suffix, uint16_t *code)
{
    uint32_t key = ((uint32_t) prefix << 8) | suffix;
    uint32_t stored_key = key + 1;
    uint32_t slot = (key * 2654435761u) & (state->hash_size - 1);

    for (uint32_t attempts = 0; attempts < state->hash_size; attempts++) {
        if (state->hash_keys[slot] == 0)
            return false;
        if (state->hash_keys[slot] == stored_key) {
            *code = state->hash_codes[slot];
            return true;
        }
        slot = (slot + 1) & (state->hash_size - 1);
    }
    return false;
}

static void
bsd_add_code(bsd_state_t *state, uint16_t prefix, uint8_t suffix)
{
    if (state->next_code >= (1u << state->max_bits))
        return;
    uint32_t key = ((uint32_t) prefix << 8) | suffix;
    uint32_t slot = (key * 2654435761u) & (state->hash_size - 1);
    while (state->hash_keys[slot] != 0)
        slot = (slot + 1) & (state->hash_size - 1);
    state->hash_keys[slot] = key + 1;
    state->hash_codes[slot] = state->next_code;
    state->prefix[state->next_code] = prefix;
    state->suffix[state->next_code] = suffix;
    state->next_code++;
}

static void
bsd_step_up(bsd_state_t *state)
{
    if (state->code_bits < state->max_bits
        && state->next_code >= (1u << state->code_bits))
        state->code_bits++;
}

static void
bsd_clear_dictionary(bsd_state_t *state)
{
    memset(state->hash_keys, 0, (size_t) state->hash_size * sizeof(*state->hash_keys));
    memset(state->hash_codes, 0, (size_t) state->hash_size * sizeof(*state->hash_codes));
    state->next_code = 257;
    state->code_bits = 9;
}

bool
ppp_ccp_codec_set(ppp_ctx_t *ctx, bool transmit, uint8_t method)
{
    return ppp_ccp_codec_set_window(ctx, transmit, method,
                                    NT31RAS_DEFAULT_WINDOW_SIZE);
}

bool
ppp_ccp_codec_set_window(ppp_ctx_t *ctx, bool transmit, uint8_t method,
                         uint32_t window_size)
{
    void **state_slot;
    uint8_t *method_slot;

    if (!ctx)
        return false;
    if (method == PPP_CCP_METHOD_NT31RAS
        && window_size != CCP_NT31RAS_WINDOW_8K
        && window_size != CCP_NT31RAS_WINDOW_16K
        && window_size != CCP_NT31RAS_WINDOW_32K
        && window_size != CCP_NT31RAS_WINDOW_64K)
        return false;
    state_slot = transmit ? &ctx->ccp_tx_codec_state : &ctx->ccp_rx_codec_state;
    method_slot = transmit ? &ctx->ccp_tx_method : &ctx->ccp_rx_method;

    ppp_ccp_codec_state_free((ppp_ccp_codec_state_t *) *state_slot);
    *state_slot = NULL;
    *method_slot = PPP_CCP_METHOD_NONE;
    if (method == PPP_CCP_METHOD_NONE)
        return true;
    if (method != PPP_CCP_METHOD_PREDICTOR1 && method != PPP_CCP_METHOD_PREDICTOR2
        && method != PPP_CCP_METHOD_LZS && method != PPP_CCP_METHOD_LZS_EXTENDED
        && method != PPP_CCP_METHOD_LZS_DCP
        && method != PPP_CCP_METHOD_V44
        && method != PPP_CCP_METHOD_DEFLATE
        && method != PPP_CCP_METHOD_MPPC && method != PPP_CCP_METHOD_BSD
        && method != PPP_CCP_METHOD_NT31RAS)
        return false;

    ppp_ccp_codec_state_t *state = (ppp_ccp_codec_state_t *) calloc(1, sizeof(*state));
    if (!state)
        return false;
    if (method == PPP_CCP_METHOD_V44) {
        state->method = method;
        *state_slot = state;
        *method_slot = method;
        return true;
    } else if (method == PPP_CCP_METHOD_PREDICTOR1
               || method == PPP_CCP_METHOD_PREDICTOR2) {
        state->predictor.table = (uint8_t *) calloc(PREDICTOR_TABLE_SIZE, 1);
        if (!state->predictor.table) {
            free(state);
            return false;
        }
    } else if (method == PPP_CCP_METHOD_MPPC) {
        state->method = method;
        state->mppc.tx_flush = true;
        state->mppc.rx_flush = true;
        *state_slot = state;
        *method_slot = method;
        return true;
    } else if (method == PPP_CCP_METHOD_LZS) {
        state->method = method;
        state->lzs_history_count = 0;
        state->lzs_check_mode = 0;
        *state_slot = state;
        *method_slot = method;
        return true;
    } else if (method == PPP_CCP_METHOD_LZS_EXTENDED) {
        state->method = method;
        state->lzs_extended.tx_flush = transmit;
        *state_slot = state;
        *method_slot = method;
        return true;
    } else if (method == PPP_CCP_METHOD_NT31RAS) {
        state->method = method;
        state->nt31ras.window_size = window_size;
        state->nt31ras.window_mask = window_size - 1;
        state->nt31ras.history = (uint8_t *) calloc(window_size, 1);
        if (window_size > NT31RAS_DEFAULT_WINDOW_SIZE) {
            state->nt31ras.hash_heads = (int32_t *) malloc(
                NT31RAS_HASH_SIZE * sizeof(*state->nt31ras.hash_heads));
            state->nt31ras.hash_next = (int32_t *) malloc(
                (NT31RAS_MAX_WINDOW_SIZE + PPP_MAX_FRAME + 2)
                * sizeof(*state->nt31ras.hash_next));
        }
        if (!state->nt31ras.history
            || (window_size > NT31RAS_DEFAULT_WINDOW_SIZE
                && (!state->nt31ras.hash_heads || !state->nt31ras.hash_next))) {
            ppp_ccp_codec_state_free(state);
            return false;
        }
        state->nt31ras.current = 1;
        state->nt31ras.flushed = true;
        *state_slot = state;
        *method_slot = method;
        return true;
    } else if (method == PPP_CCP_METHOD_BSD) {
        uint8_t dictionary_bits = transmit ? ctx->ccp_tx_bsd_bits : ctx->ccp_rx_bsd_bits;
        if (dictionary_bits < 9 || dictionary_bits > 15)
            dictionary_bits = 9;
        state->bsd.max_bits = dictionary_bits;
        state->bsd.code_bits = 9;
        state->bsd.next_code = 257;
        state->bsd.hash_size = 1u << (dictionary_bits + 1);
        state->bsd.prefix = (uint16_t *) calloc(1u << dictionary_bits,
                                                sizeof(*state->bsd.prefix));
        state->bsd.suffix = (uint8_t *) calloc(1u << dictionary_bits,
                                               sizeof(*state->bsd.suffix));
        state->bsd.hash_keys = (uint32_t *) calloc(state->bsd.hash_size,
                                                   sizeof(*state->bsd.hash_keys));
        state->bsd.hash_codes = (uint16_t *) calloc(state->bsd.hash_size,
                                                    sizeof(*state->bsd.hash_codes));
        if (!state->bsd.prefix || !state->bsd.suffix || !state->bsd.hash_keys
            || !state->bsd.hash_codes) {
            ppp_ccp_codec_state_free(state);
            return false;
        }
    } else if (deflateInit2(&state->deflater, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                            -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        free(state);
        return false;
    } else {
        state->deflater_ready = true;
        if (inflateInit2(&state->inflater, -MAX_WBITS) != Z_OK) {
            deflateEnd(&state->deflater);
            free(state);
            return false;
        }
        state->inflater_ready = true;
    }
    state->method = method;
    state->bsd.next_code = 257;
    *state_slot = state;
    *method_slot = method;
    return true;
}

bool
ppp_ccp_codec_set_lzs(ppp_ctx_t *ctx, bool transmit, uint8_t method,
                      uint16_t history_count, uint8_t check_mode)
{
    if ((method == PPP_CCP_METHOD_LZS
            && (check_mode > 3 || history_count > 1))
        || (method == PPP_CCP_METHOD_LZS_EXTENDED
            && (history_count != 1 || check_mode != 4)))
        return false;
    if (!ppp_ccp_codec_set(ctx, transmit, method))
        return false;
    ppp_ccp_codec_state_t *codec = ppp_ccp_get_codec_state(ctx, transmit);
    if (codec && (method == PPP_CCP_METHOD_LZS
                  || method == PPP_CCP_METHOD_LZS_EXTENDED)) {
        codec->lzs_history_count = history_count;
        codec->lzs_check_mode = check_mode;
    }
    return true;
}

void
ppp_ccp_codec_close(ppp_ctx_t *ctx)
{
    if (!ctx)
        return;

    ppp_ccp_codec_state_t *tx = ppp_ccp_get_codec_state(ctx, true);
    ppp_ccp_codec_state_t *rx = ppp_ccp_get_codec_state(ctx, false);
    ppp_ccp_codec_state_free(tx);
    ppp_ccp_codec_state_free(rx);
    ctx->ccp_tx_codec_state = NULL;
    ctx->ccp_rx_codec_state = NULL;
    ctx->ccp_tx_method = PPP_CCP_METHOD_NONE;
    ctx->ccp_rx_method = PPP_CCP_METHOD_NONE;
}

void
ppp_ccp_codec_flush(ppp_ctx_t *ctx, bool transmit)
{
    ppp_ccp_codec_state_t *codec = ctx ? ppp_ccp_get_codec_state(ctx, transmit) : NULL;
    if (codec && codec->method == PPP_CCP_METHOD_LZS) {
        if (transmit)
            codec->lzs_extended.tx_history_length = 0;
        else
            codec->lzs_extended.rx_history_length = 0;
        return;
    }
    if (codec && codec->method == PPP_CCP_METHOD_LZS_EXTENDED && transmit) {
        codec->lzs_extended.tx_history_length = 0;
        codec->lzs_extended.tx_flush = true;
        return;
    }
    if (codec && codec->method == PPP_CCP_METHOD_MPPC) {
        if (transmit) {
            codec->mppc.tx_flush = true;
            codec->mppc.tx_position = 0;
            codec->mppc.tx_history_length = 0;
        } else {
            codec->mppc.rx_flush = true;
            codec->mppc.rx_position = 0;
            codec->mppc.rx_history_length = 0;
        }
        return;
    }
    if (!codec || codec->method != PPP_CCP_METHOD_NT31RAS)
        return;
    codec->nt31ras.current = 1;
    codec->nt31ras.history_length = 0;
    codec->nt31ras.flushed = true;
}

bool
ppp_ccp_codec_last_frame_flushed(ppp_ctx_t *ctx, bool transmit)
{
    ppp_ccp_codec_state_t *codec = ctx ? ppp_ccp_get_codec_state(ctx, transmit) : NULL;
    return codec && ((codec->method == PPP_CCP_METHOD_NT31RAS
                      && codec->nt31ras.last_frame_flushed)
                     || (codec->method == PPP_CCP_METHOD_LZS_EXTENDED
                         && codec->lzs_extended.tx_last_frame_flushed));
}

static void
predictor_update(predictor_state_t *state, uint8_t value)
{
    state->hash = (uint16_t) ((state->hash << 4) ^ value);
}

typedef struct {
    uint8_t *data;
    int bit_position;
    int capacity;
} v44_bit_writer_t;

typedef struct {
    const uint8_t *data;
    int bit_position;
    int bit_count;
} v44_bit_reader_t;

enum {
    V44_CONTROL_ETM = 0,
    V44_CONTROL_FLUSH = 1,
    V44_CONTROL_STEPUP = 2
};

static void
v44_reset(v44_state_t *state)
{
    memset(state->nodes, 0, sizeof(state->nodes));
    for (size_t index = 0; index < sizeof(state->root_down) / sizeof(state->root_down[0]);
         index++)
        state->root_down[index] = V44_INVALID_INDEX;
    state->next_codeword = V44_FIRST_CODEWORD;
    state->codeword_bits = 6;
    state->ordinal_bits = 7;
}

static bool
v44_write_bit(v44_bit_writer_t *writer, uint8_t bit)
{
    if (writer->bit_position >= writer->capacity * 8)
        return false;
    if (bit != 0)
        writer->data[writer->bit_position >> 3] |=
            (uint8_t) (1u << (writer->bit_position & 7));
    writer->bit_position++;
    return true;
}

static bool
v44_write_bits(v44_bit_writer_t *writer, uint32_t value, uint8_t bit_count)
{
    for (uint8_t bit = 0; bit < bit_count; bit++) {
        if (!v44_write_bit(writer, (uint8_t) ((value >> bit) & 1)))
            return false;
    }
    return true;
}

static bool
v44_write_subfield(v44_bit_writer_t *writer, uint32_t value, uint8_t bit_count)
{
    for (uint8_t bit = 0; bit < bit_count; bit++) {
        if (!v44_write_bit(writer, (uint8_t) ((value >> bit) & 1)))
            return false;
    }
    return true;
}

static bool
v44_write_control(v44_state_t *state, v44_bit_writer_t *writer, uint8_t control)
{
    return v44_write_bit(writer, 1)
        && v44_write_bits(writer, control, state->codeword_bits);
}

static bool
v44_write_codeword(v44_state_t *state, v44_bit_writer_t *writer, uint16_t codeword)
{
    while (codeword >= (1u << state->codeword_bits)) {
        if (state->codeword_bits >= 11
            || !v44_write_control(state, writer, V44_CONTROL_STEPUP))
            return false;
        state->codeword_bits++;
    }
    return v44_write_bit(writer, 1)
        && v44_write_bits(writer, codeword, state->codeword_bits);
}

static bool
v44_write_ordinal(v44_state_t *state, v44_bit_writer_t *writer, uint8_t ordinal,
                  bool previous_codeword)
{
    bool stepped = ordinal >= (1u << state->ordinal_bits);
    if (stepped) {
        if (state->ordinal_bits >= 8
            || !v44_write_control(state, writer, V44_CONTROL_STEPUP))
            return false;
        state->ordinal_bits++;
    }
    if (!v44_write_bit(writer, 0))
        return false;
    if (previous_codeword && !stepped && !v44_write_bit(writer, 0))
        return false;
    return v44_write_bits(writer, ordinal, state->ordinal_bits);
}

static bool
v44_write_extension_length(v44_bit_writer_t *writer, uint16_t length)
{
    if (length == 0 || length > V44_MAX_STRING_LENGTH - 2)
        return false;
    if (length == 1)
        return v44_write_bit(writer, 1);
    if (!v44_write_bit(writer, 0))
        return false;
    if (length <= 4)
        return v44_write_subfield(writer, length - 1, 2);
    if (!v44_write_subfield(writer, 0, 2))
        return false;
    if (length <= 12)
        return v44_write_bit(writer, 0)
            && v44_write_subfield(writer, length - 5, 3);
    return v44_write_bit(writer, 1)
        && v44_write_subfield(writer, length - 13, 8);
}

static bool
v44_add_encoder_node(v44_state_t *state, uint16_t parent, uint8_t root,
                     uint16_t history_position, uint16_t length)
{
    if (state->next_codeword >= V44_CODEWORD_LIMIT)
        return true;
    if (length == 0 || length > V44_MAX_STRING_LENGTH)
        return false;

    uint16_t codeword = state->next_codeword++;
    v44_node_t *node = &state->nodes[codeword];
    node->history_position = history_position;
    node->length = length;
    node->down_index = V44_INVALID_INDEX;
    node->side_index = V44_INVALID_INDEX;

    uint16_t *head = parent == V44_INVALID_INDEX
                   ? &state->root_down[root] : &state->nodes[parent].down_index;
    if (*head == V44_INVALID_INDEX) {
        *head = codeword;
    } else {
        uint16_t sibling = *head;
        while (state->nodes[sibling].side_index != V44_INVALID_INDEX)
            sibling = state->nodes[sibling].side_index;
        state->nodes[sibling].side_index = codeword;
    }
    return true;
}

static bool
v44_compress_datagram(v44_state_t *state, const uint8_t *input, int input_len,
                      uint8_t *output, int output_capacity, int *output_len)
{
    if (input_len < 0 || input_len > (int) sizeof(state->history)
        || output_capacity < 1 || !output || !output_len
        || (input_len > 0 && !input))
        return false;

    v44_reset(state);
    if (input_len > 0)
        memcpy(state->history, input, (size_t) input_len);
    memset(output, 0, (size_t) output_capacity);
    v44_bit_writer_t writer = { output, 0, output_capacity };
    int position = 0;
    bool previous_codeword = false;

    while (position < input_len) {
        uint8_t root = state->history[position];
        uint16_t node_index = state->root_down[root];
        uint16_t matched_node = V44_INVALID_INDEX;
        int matched_length = 1;

        while (node_index != V44_INVALID_INDEX) {
            v44_node_t *node = &state->nodes[node_index];
            int available = input_len - position - matched_length;
            int remaining_string_length = V44_MAX_STRING_LENGTH - matched_length;
            if (node->length <= available && node->length <= remaining_string_length
                && memcmp(state->history + node->history_position,
                          state->history + position + matched_length,
                          node->length) == 0) {
                matched_node = node_index;
                matched_length += node->length;
                node_index = node->down_index;
            } else {
                node_index = node->side_index;
            }
        }

        if (matched_node == V44_INVALID_INDEX) {
            if (!v44_write_ordinal(state, &writer, root, previous_codeword))
                return false;
            if (position + 1 < input_len
                && !v44_add_encoder_node(state, V44_INVALID_INDEX, root,
                                         (uint16_t) (position + 1), 1))
                return false;
            position++;
            previous_codeword = false;
            continue;
        }

        if (matched_length > V44_MAX_STRING_LENGTH
            || !v44_write_codeword(state, &writer, matched_node))
            return false;
        previous_codeword = true;

        v44_node_t *matched = &state->nodes[matched_node];
        int extension_length = 0;
        int extension_limit = input_len - position - matched_length;
        int string_limit = V44_MAX_STRING_LENGTH - matched_length;
        int extension_source = matched->history_position + matched->length;
        int available_source = position - extension_source;
        if (extension_limit > string_limit)
            extension_limit = string_limit;
        if (extension_limit > available_source)
            extension_limit = available_source;
        int history_position = extension_source;
        while (extension_length < extension_limit
               && history_position + extension_length < input_len
               && state->history[position + matched_length + extension_length]
                  == state->history[history_position + extension_length])
            extension_length++;

        if (extension_length > 0) {
            if (!v44_write_bit(&writer, 0) || !v44_write_bit(&writer, 1)
                || !v44_write_extension_length(&writer, (uint16_t) extension_length)
                || !v44_add_encoder_node(state, matched_node, root,
                                         (uint16_t) (position + matched_length),
                                         (uint16_t) extension_length))
                return false;
            position += matched_length + extension_length;
            previous_codeword = false;
        } else {
            if (position + matched_length < input_len
                && matched_length < V44_MAX_STRING_LENGTH
                && !v44_add_encoder_node(state, matched_node, root,
                                         (uint16_t) (position + matched_length), 1))
                return false;
            position += matched_length;
        }
    }

    if (!v44_write_control(state, &writer, V44_CONTROL_FLUSH))
        return false;
    *output_len = (writer.bit_position + 7) / 8;
    return true;
}

static bool
v44_read_bit(v44_bit_reader_t *reader, uint32_t *bit)
{
    if (reader->bit_position >= reader->bit_count)
        return false;
    *bit = (reader->data[reader->bit_position >> 3]
            >> (reader->bit_position & 7)) & 1;
    reader->bit_position++;
    return true;
}

static bool
v44_read_bits(v44_bit_reader_t *reader, uint8_t bit_count, uint32_t *value)
{
    uint32_t result = 0;
    for (uint8_t bit = 0; bit < bit_count; bit++) {
        uint32_t next;
        if (!v44_read_bit(reader, &next))
            return false;
        result |= next << bit;
    }
    *value = result;
    return true;
}

static bool
v44_read_subfield(v44_bit_reader_t *reader, uint8_t bit_count, uint32_t *value)
{
    uint32_t result = 0;
    for (uint8_t bit = 0; bit < bit_count; bit++) {
        uint32_t next;
        if (!v44_read_bit(reader, &next))
            return false;
        result |= next << bit;
    }
    *value = result;
    return true;
}

static bool
v44_read_extension_length(v44_bit_reader_t *reader, uint16_t *length)
{
    uint32_t first;
    uint32_t value;
    if (!v44_read_bit(reader, &first))
        return false;
    if (first != 0) {
        *length = 1;
        return true;
    }

    if (!v44_read_subfield(reader, 2, &value))
        return false;
    if (value != 0) {
        *length = (uint16_t) (value + 1);
        return true;
    }

    if (!v44_read_bit(reader, &first))
        return false;
    if (first == 0) {
        if (!v44_read_subfield(reader, 3, &value))
            return false;
        *length = (uint16_t) (value + 5);
    } else {
        if (!v44_read_subfield(reader, 8, &value))
            return false;
        *length = (uint16_t) (value + 13);
    }
    return *length <= V44_MAX_STRING_LENGTH - 2;
}

static void
v44_add_decoder_node(v44_state_t *state, uint16_t history_position, uint16_t length)
{
    if (state->next_codeword >= V44_CODEWORD_LIMIT || length == 0
        || length > V44_MAX_STRING_LENGTH)
        return;
    v44_node_t *node = &state->nodes[state->next_codeword++];
    node->history_position = history_position;
    node->length = length;
}

static bool
v44_decompress_datagram(v44_state_t *state, const uint8_t *input, int input_len,
                        uint8_t *output, int output_capacity, int *output_len)
{
    if (!input || input_len < 1 || input_len > PPP_MAX_FRAME || !output
        || output_capacity < 0 || !output_len)
        return false;

    v44_reset(state);
    v44_bit_reader_t reader = { input, 0, input_len * 8 };
    int output_position = 0;
    uint16_t previous_start = 0;
    uint16_t previous_length = 0;
    uint16_t previous_codeword = V44_INVALID_INDEX;
    bool have_previous = false;
    bool previous_extended = false;
    bool prefix_after_codeword = false;

    while (reader.bit_position < reader.bit_count) {
        uint32_t prefix;
        if (!v44_read_bit(&reader, &prefix))
            return false;

        if (prefix != 0) {
            uint32_t value;
            if (!v44_read_bits(&reader, state->codeword_bits, &value))
                return false;
            if (value < V44_FIRST_CODEWORD) {
                if (value == V44_CONTROL_FLUSH) {
                    int aligned_position = (reader.bit_position + 7) & ~7;
                    if (aligned_position != reader.bit_count)
                        return false;
                    while (reader.bit_position < reader.bit_count) {
                        uint32_t padding;
                        if (!v44_read_bit(&reader, &padding) || padding != 0)
                            return false;
                    }
                    *output_len = output_position;
                    return true;
                }
                if (value != V44_CONTROL_STEPUP || reader.bit_position >= reader.bit_count)
                    return false;
                uint32_t next_prefix;
                if (!v44_read_bit(&(v44_bit_reader_t) { input, reader.bit_position,
                                                       reader.bit_count }, &next_prefix))
                    return false;
                if (next_prefix != 0) {
                    if (state->codeword_bits >= 11)
                        return false;
                    state->codeword_bits++;
                } else {
                    if (state->ordinal_bits >= 8)
                        return false;
                    state->ordinal_bits++;
                }
                prefix_after_codeword = false;
                continue;
            }

            uint16_t codeword = (uint16_t) value;
            uint16_t current_start;
            uint16_t current_length;
            bool created_current = false;
            if (codeword < state->next_codeword) {
                v44_node_t *node = &state->nodes[codeword];
                if (codeword < V44_FIRST_CODEWORD || node->length == 0
                    || node->history_position + node->length > output_position
                    || node->length > output_capacity - output_position)
                    return false;
                current_start = (uint16_t) output_position;
                current_length = node->length;
                memcpy(output + output_position, output + node->history_position,
                       node->length);
                output_position += node->length;
            } else if (codeword == state->next_codeword
                       && codeword < V44_CODEWORD_LIMIT && have_previous
                       && previous_length > 0
                       && previous_length < output_capacity - output_position) {
                current_start = (uint16_t) output_position;
                current_length = (uint16_t) (previous_length + 1);
                memcpy(output + output_position, output + previous_start, previous_length);
                output_position += previous_length;
                output[output_position++] = output[previous_start];
                v44_node_t *node = &state->nodes[state->next_codeword++];
                node->history_position = current_start;
                node->length = current_length;
                created_current = true;
            } else {
                return false;
            }

            if (!created_current && have_previous && !previous_extended
                && previous_start + previous_length == current_start
                && previous_length < V44_MAX_STRING_LENGTH)
                v44_add_decoder_node(state, previous_start,
                                     (uint16_t) (previous_length + 1));
            previous_start = current_start;
            previous_length = current_length;
            previous_codeword = codeword;
            have_previous = true;
            previous_extended = false;
            prefix_after_codeword = true;
            continue;
        }

        bool extension = false;
        if (prefix_after_codeword) {
            uint32_t second_prefix;
            if (!v44_read_bit(&reader, &second_prefix))
                return false;
            extension = second_prefix != 0;
        }

        if (extension) {
            uint16_t extension_length;
            if (!have_previous || previous_codeword < V44_FIRST_CODEWORD
                || previous_codeword >= state->next_codeword
                || !v44_read_extension_length(&reader, &extension_length)
                || previous_length + extension_length > V44_MAX_STRING_LENGTH
                || extension_length > output_capacity - output_position)
                return false;
            v44_node_t *node = &state->nodes[previous_codeword];
            uint32_t source_position = node->history_position + node->length;
            if (source_position >= (uint32_t) output_position)
                return false;
            uint16_t extended_start = previous_start;
            for (uint16_t index = 0; index < extension_length; index++) {
                output[output_position++] = output[source_position + index];
            }
            previous_length = (uint16_t) (previous_length + extension_length);
            v44_add_decoder_node(state, extended_start, previous_length);
            previous_start = extended_start;
            previous_extended = true;
        } else {
            uint32_t ordinal;
            if (!v44_read_bits(&reader, state->ordinal_bits, &ordinal)
                || ordinal > 0xFF || output_position >= output_capacity)
                return false;
            uint16_t current_start = (uint16_t) output_position;
            output[output_position++] = (uint8_t) ordinal;
            if (have_previous && !previous_extended
                && previous_start + previous_length == current_start
                && previous_length < V44_MAX_STRING_LENGTH)
                v44_add_decoder_node(state, previous_start,
                                     (uint16_t) (previous_length + 1));
            previous_start = current_start;
            previous_length = 1;
            previous_codeword = V44_INVALID_INDEX;
            have_previous = true;
            previous_extended = false;
        }
        prefix_after_codeword = false;
    }
    return false;
}

static predictor2_record_result_t
predictor2_decode_record(ppp_ccp_codec_state_t *codec, const uint8_t *record,
                         size_t record_len, uint8_t *output, int output_capacity,
                         int *output_len, size_t *consumed)
{
    uint8_t crc_input[PPP_MAX_FRAME + PREDICTOR_HEADER_SIZE];
    size_t data_pos = PREDICTOR_HEADER_SIZE;
    size_t record_size;
    uint16_t encoded_length;
    uint16_t expected_crc;
    int expected_len;
    bool compressed;

    if (record_len < PREDICTOR_HEADER_SIZE)
        return PREDICTOR2_RECORD_INCOMPLETE;

    encoded_length = (uint16_t) (((uint16_t) record[0] << 8) | record[1]);
    compressed = (encoded_length & 0x8000) != 0;
    expected_len = encoded_length & 0x7FFF;
    if (expected_len == 0 || expected_len > output_capacity || expected_len > PPP_MAX_FRAME)
        return PREDICTOR2_RECORD_INVALID;

    if (compressed) {
        int decoded_pos = 0;
        while (decoded_pos < expected_len) {
            if (data_pos >= record_len)
                return PREDICTOR2_RECORD_INCOMPLETE;
            uint8_t flags = record[data_pos++];
            for (uint8_t bit = 0; bit < 8 && decoded_pos < expected_len; bit++, decoded_pos++) {
                if (!(flags & (1u << bit))) {
                    if (data_pos >= record_len)
                        return PREDICTOR2_RECORD_INCOMPLETE;
                    data_pos++;
                }
            }
        }
    } else {
        if ((size_t) expected_len > record_len - data_pos)
            return PREDICTOR2_RECORD_INCOMPLETE;
        data_pos += (size_t) expected_len;
    }

    if (record_len - data_pos < PREDICTOR_TRAILER_SIZE)
        return PREDICTOR2_RECORD_INCOMPLETE;
    record_size = data_pos + PREDICTOR_TRAILER_SIZE;

    if (!compressed) {
        memcpy(output, record + PREDICTOR_HEADER_SIZE, (size_t) expected_len);
        for (int pos = 0; pos < expected_len; pos++) {
            codec->predictor.table[codec->predictor.hash] = output[pos];
            predictor_update(&codec->predictor, output[pos]);
        }
    } else {
        data_pos = PREDICTOR_HEADER_SIZE;
        for (int pos = 0; pos < expected_len;) {
            uint8_t flags = record[data_pos++];
            for (uint8_t bit = 0; bit < 8 && pos < expected_len; bit++, pos++) {
                uint8_t value;
                if (flags & (1u << bit)) {
                    value = codec->predictor.table[codec->predictor.hash];
                } else {
                    value = record[data_pos++];
                    codec->predictor.table[codec->predictor.hash] = value;
                }
                output[pos] = value;
                predictor_update(&codec->predictor, value);
            }
        }
    }

    memcpy(crc_input, record, PREDICTOR_HEADER_SIZE);
    memcpy(crc_input + PREDICTOR_HEADER_SIZE, output, (size_t) expected_len);
    expected_crc = predictor_crc16(crc_input, PREDICTOR_HEADER_SIZE + expected_len);
    if (record[record_size - PREDICTOR_TRAILER_SIZE] != (uint8_t) expected_crc
        || record[record_size - 1] != (uint8_t) (expected_crc >> 8))
        return PREDICTOR2_RECORD_INVALID;

    *output_len = expected_len;
    *consumed = record_size;
    return PREDICTOR2_RECORD_COMPLETE;
}

static bool
predictor2_decompress(ppp_ccp_codec_state_t *codec, const uint8_t *input, int input_len,
                      uint8_t *output, int output_capacity, int *output_len)
{
    size_t consumed = 0;
    predictor2_record_result_t result;

    *output_len = 0;
    if (input_len < 0 || input_len > PPP_MAX_FRAME || (input_len > 0 && !input)
        || (size_t) input_len > sizeof(codec->predictor2_rx_stream)
                               - codec->predictor2_rx_stream_len)
        return false;

    if (input_len > 0) {
        memcpy(codec->predictor2_rx_stream + codec->predictor2_rx_stream_len,
               input, (size_t) input_len);
        codec->predictor2_rx_stream_len += (size_t) input_len;
    }

    result = predictor2_decode_record(codec, codec->predictor2_rx_stream,
                                      codec->predictor2_rx_stream_len, output,
                                      output_capacity, output_len, &consumed);
    if (result == PREDICTOR2_RECORD_INVALID)
        return false;
    if (result == PREDICTOR2_RECORD_INCOMPLETE)
        return true;

    codec->predictor2_rx_stream_len -= consumed;
    memmove(codec->predictor2_rx_stream, codec->predictor2_rx_stream + consumed,
            codec->predictor2_rx_stream_len);
    return true;
}

bool
ppp_ccp_codec_compress(ppp_ctx_t *ctx, const uint8_t *input, int input_len,
                       uint8_t *output, int output_capacity, int *output_len)
{
    ppp_ccp_codec_state_t *codec = ctx ? ppp_ccp_get_codec_state(ctx, true) : NULL;
    uint8_t compressed[PPP_MAX_FRAME + (PPP_MAX_FRAME / 8) + 1];
    uint8_t crc_input[PPP_MAX_FRAME + PREDICTOR_HEADER_SIZE];
    int compressed_len = 0;
    int header_len;
    uint16_t crc;

    if (!codec || (codec->method != PPP_CCP_METHOD_PREDICTOR1
                   && codec->method != PPP_CCP_METHOD_PREDICTOR2
                   && codec->method != PPP_CCP_METHOD_LZS
                   && codec->method != PPP_CCP_METHOD_LZS_EXTENDED
                   && codec->method != PPP_CCP_METHOD_LZS_DCP
                   && codec->method != PPP_CCP_METHOD_V44
                   && codec->method != PPP_CCP_METHOD_DEFLATE
                   && codec->method != PPP_CCP_METHOD_MPPC
                   && codec->method != PPP_CCP_METHOD_BSD
                   && codec->method != PPP_CCP_METHOD_NT31RAS) || !input || !output
        || !output_len || input_len < 0 || input_len > PPP_MAX_FRAME)
        return false;

    if (codec->method == PPP_CCP_METHOD_NT31RAS)
        return nt31ras_compress_source_format(codec, input, input_len, output,
                                              output_capacity, output_len);
    if (codec->method == PPP_CCP_METHOD_LZS)
        return lzs_standard_compress(codec, input, input_len, output,
                                     output_capacity, output_len);
    if (codec->method == PPP_CCP_METHOD_LZS_EXTENDED)
        return lzs_extended_compress(codec, input, input_len, output,
                                     output_capacity, output_len);
    if (codec->method == PPP_CCP_METHOD_LZS_DCP)
        return lzs_dcp_compress(input, input_len, output, output_capacity, output_len);
    if (codec->method == PPP_CCP_METHOD_V44)
        return v44_compress_datagram(&codec->v44, input, input_len, output,
                                     output_capacity, output_len);

    if (codec->method == PPP_CCP_METHOD_MPPC) {
        uint8_t encoded[PPP_MAX_FRAME * 2];
        int bit_pos = 0;
        uint16_t count = codec->mppc.tx_count & 0x0FFF;
        bool flushed = codec->mppc.tx_flush;
        bool at_front = false;
        uint16_t packet_start_position;

        if (output_capacity < input_len + 2 || input_len > MPPC_HISTORY_SIZE)
            return false;

        if (flushed || (size_t) codec->mppc.tx_position + (size_t) input_len
                        > MPPC_HISTORY_SIZE) {
            mppc_history_reset(codec->mppc.tx_history, &codec->mppc.tx_position,
                               &codec->mppc.tx_history_length);
            flushed = true;
            at_front = true;
        }
        packet_start_position = codec->mppc.tx_position;

        memset(encoded, 0, sizeof(encoded));
        for (int pos = 0; pos < input_len;) {
            int best_length = 0;
            int best_offset = 0;
            int max_offset = codec->mppc.tx_history_length < MPPC_HISTORY_MASK
                           ? codec->mppc.tx_history_length : MPPC_HISTORY_MASK;
            for (int offset = 1; offset <= max_offset; offset++) {
                int length = 0;
                while (pos + length < input_len) {
                    int source_pos = pos + length - offset;
                    uint8_t source = source_pos < 0
                                   ? codec->mppc.tx_history[
                                         ((size_t) packet_start_position + MPPC_HISTORY_SIZE
                                          + source_pos) & MPPC_HISTORY_MASK]
                                   : input[source_pos];
                    if (input[pos + length] != source)
                        break;
                    length++;
                }
                if (length > best_length) {
                    best_length = length;
                    best_offset = offset;
                }
                if (best_length == input_len - pos)
                    break;
            }
            if (best_length >= 3) {
                mppc_write_match(encoded, &bit_pos, (uint16_t) best_offset,
                                 (uint16_t) best_length);
                for (int index = 0; index < best_length; index++)
                    mppc_history_write(codec->mppc.tx_history, &codec->mppc.tx_position,
                                       &codec->mppc.tx_history_length, input[pos + index]);
                pos += best_length;
            } else {
                uint8_t value = input[pos++];
                if (value < 0x80) {
                    mppc_write_bits(encoded, &bit_pos, value, 8);
                } else {
                    mppc_write_bits(encoded, &bit_pos, 2, 2);
                    mppc_write_bits(encoded, &bit_pos, value & 0x7F, 7);
                }
                mppc_history_write(codec->mppc.tx_history, &codec->mppc.tx_position,
                                   &codec->mppc.tx_history_length, value);
            }
        }
        int encoded_len = (bit_pos + 7) / 8;
        bool use_compressed = encoded_len < input_len;
        int payload_len = use_compressed ? encoded_len : input_len;
        uint8_t flags = (flushed ? MPPC_FLUSHED : 0)
                      | (at_front ? MPPC_AT_FRONT : 0)
                      | (use_compressed ? MPPC_COMPRESSED : 0);
        output[0] = (uint8_t) (flags | (count >> 8));
        output[1] = (uint8_t) count;
        memcpy(output + 2, use_compressed ? encoded : input, (size_t) payload_len);
        codec->mppc.tx_count = (uint16_t) ((count + 1) & 0x0FFF);
        codec->mppc.tx_flush = !use_compressed;
        *output_len = payload_len + 2;
        return true;
    }

    if (codec->method == PPP_CCP_METHOD_BSD) {
        uint8_t encoded[PPP_MAX_FRAME * 2];
        int bit_pos = 0;
        if (input_len == 0 || output_capacity < 3)
            return false;
        memset(encoded, 0, sizeof(encoded));
        uint16_t prefix = input[0];
        for (int pos = 1; pos < input_len; pos++) {
            uint16_t code;
            if (bsd_find_code(&codec->bsd, prefix, input[pos], &code)) {
                prefix = code;
                continue;
            }
            mppc_write_bits(encoded, &bit_pos, prefix, codec->bsd.code_bits);
            bsd_step_up(&codec->bsd);
            bsd_add_code(&codec->bsd, prefix, input[pos]);
            prefix = input[pos];
        }
        mppc_write_bits(encoded, &bit_pos, prefix, codec->bsd.code_bits);
        bsd_step_up(&codec->bsd);
        int encoded_len = (bit_pos + 7) / 8;
        if (encoded_len + 2 > output_capacity)
            return false;
        output[0] = (uint8_t) (codec->bsd.tx_sequence >> 8);
        output[1] = (uint8_t) codec->bsd.tx_sequence;
        memcpy(output + 2, encoded, (size_t) encoded_len);
        codec->bsd.tx_sequence++;
        *output_len = encoded_len + 2;
        return true;
    }

    if (codec->method == PPP_CCP_METHOD_DEFLATE) {
        if (output_capacity < 8)
            return false;
        codec->deflater.next_in = (Bytef *) input;
        codec->deflater.avail_in = (uInt) input_len;
        codec->deflater.next_out = output + 2;
        codec->deflater.avail_out = (uInt) (output_capacity - 2);
        if (deflate(&codec->deflater, Z_SYNC_FLUSH) != Z_OK
            || codec->deflater.avail_in != 0 || codec->deflater.avail_out > output_capacity - 6)
            return false;
        int compressed_len = output_capacity - 2 - (int) codec->deflater.avail_out - 4;
        output[0] = (uint8_t) (codec->tx_sequence >> 8);
        output[1] = (uint8_t) codec->tx_sequence;
        codec->tx_sequence++;
        *output_len = compressed_len + 2;
        return true;
    }

    if (output_capacity < input_len + 4)
        return false;

    for (int pos = 0; pos < input_len;) {
        int flag_pos = compressed_len++;
        uint8_t flags = 0;
        for (uint8_t bit = 0; bit < 8 && pos < input_len; bit++, pos++) {
            uint8_t value = input[pos];
            if (codec->predictor.table[codec->predictor.hash] == value) {
                flags |= (uint8_t) (1u << bit);
            } else {
                codec->predictor.table[codec->predictor.hash] = value;
                compressed[compressed_len++] = value;
            }
            predictor_update(&codec->predictor, value);
        }
        compressed[flag_pos] = flags;
    }

    bool use_compressed = compressed_len < input_len;
    uint16_t encoded_length = (uint16_t) input_len;
    if (use_compressed)
        encoded_length |= 0x8000;
    output[0] = (uint8_t) (encoded_length >> 8);
    output[1] = (uint8_t) encoded_length;
    if (use_compressed) {
        if (output_capacity < PREDICTOR_HEADER_SIZE + compressed_len + PREDICTOR_TRAILER_SIZE)
            return false;
        memcpy(output + PREDICTOR_HEADER_SIZE, compressed, (size_t) compressed_len);
        header_len = PREDICTOR_HEADER_SIZE + compressed_len;
    } else {
        memcpy(output + PREDICTOR_HEADER_SIZE, input, (size_t) input_len);
        header_len = PREDICTOR_HEADER_SIZE + input_len;
    }

    memcpy(crc_input, output, PREDICTOR_HEADER_SIZE);
    memcpy(crc_input + PREDICTOR_HEADER_SIZE, input, (size_t) input_len);
    crc = predictor_crc16(crc_input, PREDICTOR_HEADER_SIZE + input_len);
    output[header_len] = (uint8_t) crc;
    output[header_len + 1] = (uint8_t) (crc >> 8);
    *output_len = header_len + PREDICTOR_TRAILER_SIZE;
    return true;
}

bool
ppp_ccp_codec_decompress(ppp_ctx_t *ctx, const uint8_t *input, int input_len,
                         uint8_t *output, int output_capacity, int *output_len)
{
    ppp_ccp_codec_state_t *codec = ctx ? ppp_ccp_get_codec_state(ctx, false) : NULL;
    uint8_t crc_input[PPP_MAX_FRAME + PREDICTOR_HEADER_SIZE];
    int expected_len;
    int input_pos = PREDICTOR_HEADER_SIZE;
    int input_end = input_len - PREDICTOR_TRAILER_SIZE;
    uint16_t crc;
    bool compressed;

    if (!codec || !output || !output_len || input_len < 0 || input_len > PPP_MAX_FRAME)
        return false;

    if (codec->method == PPP_CCP_METHOD_NT31RAS)
        return nt31ras_decompress_source_format(codec, input, input_len, output,
                                                output_capacity, output_len);
    if (codec->method == PPP_CCP_METHOD_LZS)
        return lzs_standard_decompress(codec, input, input_len, output,
                                       output_capacity, output_len);
    if (codec->method == PPP_CCP_METHOD_LZS_EXTENDED)
        return lzs_extended_decompress(ctx, codec, input, input_len, output,
                                      output_capacity, output_len);
    if (codec->method == PPP_CCP_METHOD_LZS_DCP)
        return lzs_dcp_decompress(input, input_len, output, output_capacity, output_len);
    if (codec->method == PPP_CCP_METHOD_V44)
        return v44_decompress_datagram(&codec->v44, input, input_len, output,
                                       output_capacity, output_len);

    if (codec->method == PPP_CCP_METHOD_PREDICTOR2)
        return predictor2_decompress(codec, input, input_len, output, output_capacity,
                                     output_len);
    if (!input || input_len < 2)
        return false;

    if (codec->method == PPP_CCP_METHOD_MPPC) {
        if (input_len < 2 || (input[0] & MPPC_RESERVED) != 0)
            return false;
        uint8_t flags = input[0] & 0xF0;
        bool flushed = (flags & MPPC_FLUSHED) != 0;
        bool at_front = (flags & MPPC_AT_FRONT) != 0;
        bool compressed = (flags & MPPC_COMPRESSED) != 0;
        uint16_t count = (uint16_t) (((input[0] & 0x0F) << 8) | input[1]);

        if (flushed) {
            mppc_history_reset(codec->mppc.rx_history, &codec->mppc.rx_position,
                               &codec->mppc.rx_history_length);
            codec->mppc.rx_flush = false;
            codec->mppc.rx_count = count;
            ctx->ccp_reset_pending = false;
        } else if (codec->mppc.rx_flush || count != codec->mppc.rx_count) {
            return false;
        }
        if (at_front)
            mppc_history_move_to_front(codec->mppc.rx_history, &codec->mppc.rx_position,
                                       codec->mppc.rx_history_length);

        if (!compressed) {
            int raw_len = input_len - 2;
            if (raw_len > output_capacity || raw_len > MPPC_HISTORY_SIZE)
                return false;
            memcpy(output, input + 2, (size_t) raw_len);
            codec->mppc.rx_count = (uint16_t) ((count + 1) & 0x0FFF);
            codec->mppc.rx_flush = true;
            *output_len = raw_len;
            return true;
        }
        if (input_len < 3)
            return false;
        int bit_pos = 0;
        int input_bits = (input_len - 2) * 8;
        int output_pos = 0;
        while (bit_pos + 8 <= input_bits) {
            uint32_t first;
            uint32_t second;
            if (!mppc_read_bits(input + 2, input_bits, &bit_pos, 1, &first))
                return false;
            if (first == 0) {
                if (!mppc_read_bits(input + 2, input_bits, &bit_pos, 7, &second)
                    || output_pos >= output_capacity)
                    return false;
                uint8_t value = (uint8_t) second;
                output[output_pos++] = value;
                mppc_history_write(codec->mppc.rx_history, &codec->mppc.rx_position,
                                   &codec->mppc.rx_history_length, value);
            } else {
                if (!mppc_read_bits(input + 2, input_bits, &bit_pos, 1, &second))
                    return false;
                if (second == 0) {
                    if (!mppc_read_bits(input + 2, input_bits, &bit_pos, 7, &second)
                        || output_pos >= output_capacity)
                        return false;
                    uint8_t value = (uint8_t) (second | 0x80);
                    output[output_pos++] = value;
                    mppc_history_write(codec->mppc.rx_history, &codec->mppc.rx_position,
                                       &codec->mppc.rx_history_length, value);
                } else {
                    bit_pos -= 2;
                    uint16_t offset;
                    uint16_t length;
                    if (!mppc_read_match(input + 2, input_bits, &bit_pos, &offset, &length)
                        || offset == 0 || offset > codec->mppc.rx_history_length
                        || length > output_capacity - output_pos)
                        return false;
                    for (uint16_t index = 0; index < length; index++) {
                        uint8_t value = mppc_history_read(codec->mppc.rx_history,
                                                          codec->mppc.rx_position, offset);
                        output[output_pos++] = value;
                        mppc_history_write(codec->mppc.rx_history, &codec->mppc.rx_position,
                                           &codec->mppc.rx_history_length, value);
                    }
                }
            }
        }
        while (bit_pos < input_bits) {
            uint32_t padding;
            if (!mppc_read_bits(input + 2, input_bits, &bit_pos, 1, &padding) || padding != 0)
                return false;
        }
        if (output_pos == 0)
            return false;
        codec->mppc.rx_count = (uint16_t) ((count + 1) & 0x0FFF);
        codec->mppc.rx_flush = false;
        *output_len = output_pos;
        return true;
    }

    if (codec->method == PPP_CCP_METHOD_BSD) {
        if (input_len < 3
            || input[0] != (uint8_t) (codec->bsd.rx_sequence >> 8)
            || input[1] != (uint8_t) codec->bsd.rx_sequence)
            return false;
        int bit_pos = 0;
        int input_bits = (input_len - 2) * 8;
        int output_pos = 0;
        int previous_code = -1;
        uint8_t stack[1u << 15];
        while (bit_pos + codec->bsd.code_bits <= input_bits) {
            uint32_t raw_code;
            if (!mppc_read_bits(input + 2, input_bits, &bit_pos,
                                codec->bsd.code_bits, &raw_code))
                return false;
            uint16_t code = (uint16_t) raw_code;
            if (code == 256) {
                if (input_bits - bit_pos >= codec->bsd.code_bits)
                    return false;
                bsd_clear_dictionary(&codec->bsd);
                previous_code = -1;
                break;
            }
            uint16_t current = code;
            int stack_len = 0;
            if (code == codec->bsd.next_code) {
                if (previous_code < 0
                    || codec->bsd.next_code >= (1u << codec->bsd.max_bits))
                    return false;
                current = (uint16_t) previous_code;
                while (current >= 256) {
                    if (current >= codec->bsd.next_code || stack_len >= (int) sizeof(stack))
                        return false;
                    stack[stack_len++] = codec->bsd.suffix[current];
                    current = codec->bsd.prefix[current];
                }
                stack[stack_len++] = (uint8_t) current;
                uint8_t first = stack[stack_len - 1];
                while (stack_len > 0) {
                    if (output_pos >= output_capacity)
                        return false;
                    output[output_pos++] = stack[--stack_len];
                }
                if (output_pos >= output_capacity)
                    return false;
                output[output_pos++] = first;
                bsd_add_code(&codec->bsd, (uint16_t) previous_code, first);
                bsd_step_up(&codec->bsd);
            } else {
                if (code > 255 && code >= codec->bsd.next_code)
                    return false;
                current = code;
                while (current >= 256) {
                    if (stack_len >= (int) sizeof(stack))
                        return false;
                    stack[stack_len++] = codec->bsd.suffix[current];
                    current = codec->bsd.prefix[current];
                }
                uint8_t first = (uint8_t) current;
                stack[stack_len++] = first;
                while (stack_len > 0) {
                    if (output_pos >= output_capacity)
                        return false;
                    output[output_pos++] = stack[--stack_len];
                }
                if (previous_code >= 0)
                    bsd_add_code(&codec->bsd, (uint16_t) previous_code, first);
                if (previous_code >= 0)
                    bsd_step_up(&codec->bsd);
            }
            previous_code = code;
        }
        codec->bsd.rx_sequence++;
        *output_len = output_pos;
        return true;
    }

    if (codec->method == PPP_CCP_METHOD_DEFLATE) {
        uint8_t deflate_input[PPP_MAX_FRAME + 4];
        if (input_len < 2 || input_len + 4 > (int) sizeof(deflate_input)
            || input[0] != (uint8_t) (codec->rx_sequence >> 8)
            || input[1] != (uint8_t) codec->rx_sequence)
            return false;
        memcpy(deflate_input, input + 2, (size_t) input_len - 2);
        deflate_input[input_len - 2] = 0;
        deflate_input[input_len - 1] = 0;
        deflate_input[input_len] = 0xFF;
        deflate_input[input_len + 1] = 0xFF;
        codec->inflater.next_in = deflate_input;
        codec->inflater.avail_in = (uInt) (input_len + 2);
        codec->inflater.next_out = output;
        codec->inflater.avail_out = (uInt) output_capacity;
        uInt output_before = codec->inflater.avail_out;
        if (inflate(&codec->inflater, Z_SYNC_FLUSH) != Z_OK
            || codec->inflater.avail_in != 0)
            return false;
        codec->rx_sequence++;
        *output_len = (int) (output_before - codec->inflater.avail_out);
        return true;
    }

    if (codec->method != PPP_CCP_METHOD_PREDICTOR1)
        return false;
    if (input_len < 4)
        return false;

    compressed = (input[0] & 0x80) != 0;
    expected_len = ((input[0] & 0x7F) << 8) | input[1];
    if (expected_len > output_capacity || expected_len > PPP_MAX_FRAME)
        return false;

    if (!compressed) {
        if (input_end - input_pos != expected_len)
            return false;
        memcpy(output, input + input_pos, (size_t) expected_len);
        for (int pos = 0; pos < expected_len; pos++) {
            codec->predictor.table[codec->predictor.hash] = output[pos];
            predictor_update(&codec->predictor, output[pos]);
        }
    } else {
        for (int pos = 0; pos < expected_len;) {
            if (input_pos >= input_end)
                return false;
            uint8_t flags = input[input_pos++];
            for (uint8_t bit = 0; bit < 8 && pos < expected_len; bit++, pos++) {
                uint8_t value;
                if (flags & (1u << bit)) {
                    value = codec->predictor.table[codec->predictor.hash];
                } else {
                    if (input_pos >= input_end)
                        return false;
                    value = input[input_pos++];
                    codec->predictor.table[codec->predictor.hash] = value;
                }
                output[pos] = value;
                predictor_update(&codec->predictor, value);
            }
        }
        if (input_pos != input_end)
            return false;
    }

    memcpy(crc_input, input, PREDICTOR_HEADER_SIZE);
    memcpy(crc_input + PREDICTOR_HEADER_SIZE, output, (size_t) expected_len);
    crc = predictor_crc16(crc_input, PREDICTOR_HEADER_SIZE + expected_len);
    if (input[input_end] != (uint8_t) crc || input[input_end + 1] != (uint8_t) (crc >> 8))
        return false;

    *output_len = expected_len;
    return true;
}
