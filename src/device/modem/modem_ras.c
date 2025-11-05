/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Down-level asynchronous RAS framing for modem emulation.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <86box/modem/modem_crypto.h>
#include <86box/modem/modem_ras.h>

#define MODEM_RAS_PROTOCOL_MESSAGE_LENGTH        13
#define MODEM_RAS_PADDED_PROTOCOL_MESSAGE_LENGTH 333
#define MODEM_RAS_RESPONSE_10_USERNAME_LENGTH    21
#define MODEM_RAS_RESPONSE_10_LENGTH             46
#define MODEM_RAS_RESPONSE_20_USERNAME_LENGTH    257
#define MODEM_RAS_RESPONSE_20_DOMAIN_LENGTH      16
#define MODEM_RAS_RESPONSE_20_LM_OFFSET          274
#define MODEM_RAS_RESPONSE_20_NT_OFFSET          298
#define MODEM_RAS_RESPONSE_20_USE_NT_OFFSET      322
#define MODEM_RAS_RESPONSE_20_LENGTH             326
#define MODEM_RAS_CONFIG_REQUEST_LENGTH          333
#define MODEM_RAS_CONFIG_RESULT_LENGTH           333
#define MODEM_RAS_CONFIG_MAX_SEND_OFFSET         19
#define MODEM_RAS_CONFIG_MAX_RECV_OFFSET         23
#define MODEM_RAS_CONFIG_LINK_SPEED_OFFSET       27
#define MODEM_RAS_CONFIG_NB_PROJECT_OFFSET       31
#define MODEM_RAS_CONFIG_NB_NAME_COUNT_OFFSET   35
#define MODEM_RAS_CONFIG_NB_NAMES_OFFSET        37
#define MODEM_RAS_CONFIG_NAME_RECORD_LENGTH     18
#define MODEM_RAS_CONFIG_IP_PROJECT_OFFSET       325
#define MODEM_RAS_CONFIG_IPX_PROJECT_OFFSET      329
#define MODEM_RAS_CONFIG_RESULT_NAME_COUNT_OFFSET 34
#define MODEM_RAS_CONFIG_RESULT_NAMES_OFFSET    36
#define MODEM_RAS_CONFIG_RESULT_NAME_RECORD_LENGTH 18
#define MODEM_RAS_MAX_FRAME_SIZE                 1514

static uint16_t
ras_crc_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t) byte << 8;
    for (unsigned int bit = 0; bit < 8; bit++)
        crc = (crc & 0x8000) ? (uint16_t) ((crc << 1) ^ 0x1021) : (uint16_t) (crc << 1);
    return crc;
}

static void
ras_rx_reset(modem_ras_decoder_t *decoder, uint8_t byte)
{
    decoder->state = (byte == MODEM_RAS_SYNC || byte == (MODEM_RAS_SYNC | MODEM_RAS_ESCAPE_FLAG))
                         ? MODEM_RAS_RX_ADDRESS
                         : MODEM_RAS_RX_SYNC;
    decoder->escaped_frame = byte == (MODEM_RAS_SYNC | MODEM_RAS_ESCAPE_FLAG);
    decoder->escape_next = false;
    decoder->length = 0;
    decoder->position = 0;
    decoder->crc = 0;
    decoder->wire_length = 0;
    decoder->wire_position = 0;
    decoder->escaped_tail_length = 0;
}

static void
ras_rx_complete(modem_ras_decoder_t *decoder)
{
    decoder->state = MODEM_RAS_RX_SYNC;
    decoder->escaped_frame = false;
    decoder->escape_next = false;
    decoder->length = 0;
    decoder->position = 0;
    decoder->crc = 0;
    decoder->wire_length = 0;
    decoder->wire_position = 0;
    decoder->escaped_tail_length = 0;
}

void
modem_ras_decoder_init(modem_ras_decoder_t *decoder, modem_ras_frame_callback_t callback,
                       modem_ras_event_callback_t event_callback, void *priv)
{
    if (!decoder)
        return;

    memset(decoder, 0, sizeof(*decoder));
    decoder->callback = callback;
    decoder->event_callback = event_callback;
    decoder->priv = priv;
    decoder->state = MODEM_RAS_RX_SYNC;
    decoder->xon_xoff_mask = MODEM_RAS_XONXOFF_MASK;
}

void
modem_ras_decoder_set_xon_xoff_mask(modem_ras_decoder_t *decoder, uint32_t mask)
{
    if (decoder)
        decoder->xon_xoff_mask = mask;
}

static void
ras_rx_escaped_body_byte(modem_ras_decoder_t *decoder, uint8_t byte)
{
    uint8_t decoded;

    decoder->wire_position++;
    if (decoder->escape_next) {
        decoded = byte ^ 0x20;
        decoder->escape_next = false;
    } else if (byte == MODEM_RAS_ESCAPE) {
        decoder->escape_next = true;
        if (decoder->wire_position == decoder->wire_length) {
            if (decoder->event_callback)
                decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_INVALID_ESCAPE,
                                        decoder->address, decoder->length, byte, 0);
            ras_rx_complete(decoder);
        }
        return;
    } else if (byte < 32 && (decoder->xon_xoff_mask & (1u << byte)))
        return;
    else
        decoded = byte;

    if (decoder->escaped_tail_length < sizeof(decoder->escaped_tail)) {
        decoder->escaped_tail[decoder->escaped_tail_length++] = decoded;
    } else {
        if (decoder->position >= MODEM_RAS_MAX_PAYLOAD) {
            if (decoder->event_callback)
                decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_INVALID_LENGTH,
                                        decoder->address, decoder->length, decoder->position + 1,
                                        MODEM_RAS_MAX_PAYLOAD);
            ras_rx_complete(decoder);
            return;
        }
        decoder->payload[decoder->position++] = decoder->escaped_tail[0];
        decoder->crc = ras_crc_update(decoder->crc, decoder->escaped_tail[0]);
        memmove(decoder->escaped_tail, decoder->escaped_tail + 1, 2);
        decoder->escaped_tail[2] = decoded;
    }

    if (decoder->wire_position != decoder->wire_length)
        return;

    if (decoder->escape_next) {
        if (decoder->event_callback)
            decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_INVALID_ESCAPE,
                                    decoder->address, decoder->length, MODEM_RAS_ESCAPE, 0);
    } else if (decoder->escaped_tail_length != sizeof(decoder->escaped_tail)) {
        if (decoder->event_callback)
            decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_INVALID_LENGTH,
                                    decoder->address, decoder->length,
                                    decoder->escaped_tail_length, sizeof(decoder->escaped_tail));
    } else if (decoder->escaped_tail[0] != MODEM_RAS_ETX) {
        if (decoder->event_callback)
            decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_INVALID_ETX,
                                    decoder->address, decoder->length,
                                    decoder->escaped_tail[0], MODEM_RAS_ETX);
    } else {
        const uint16_t received_crc = (uint16_t) (decoder->escaped_tail[1]
                                                  | ((uint16_t) decoder->escaped_tail[2] << 8));

        if (received_crc == decoder->crc) {
            if (decoder->event_callback)
                decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_FRAME_RECEIVED,
                                        decoder->address, decoder->position, 0, 0);
            if (decoder->callback)
                decoder->callback(decoder->priv, decoder->address, decoder->payload,
                                  decoder->position);
        } else if (decoder->event_callback)
            decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_BAD_CRC,
                                    decoder->address, decoder->position, received_crc,
                                    decoder->crc);
    }
    ras_rx_complete(decoder);
}

void
modem_ras_rx_byte(modem_ras_decoder_t *decoder, uint8_t byte)
{
    if (!decoder)
        return;

    switch (decoder->state) {
        case MODEM_RAS_RX_SYNC:
            if (byte == MODEM_RAS_SYNC)
                decoder->state = MODEM_RAS_RX_ADDRESS;
            else if (byte == (MODEM_RAS_SYNC | MODEM_RAS_ESCAPE_FLAG)) {
                decoder->state = MODEM_RAS_RX_ADDRESS;
                decoder->escaped_frame = true;
            }
            break;

        case MODEM_RAS_RX_ADDRESS:
        {
            const uint8_t address = decoder->escaped_frame
                                        ? (byte & (uint8_t) ~MODEM_RAS_ESCAPE_FLAG)
                                        : byte;

            if ((decoder->escaped_frame && !(byte & MODEM_RAS_ESCAPE_FLAG))
                || (address != MODEM_RAS_BROADCAST && address != MODEM_RAS_DESTINATION)) {
                if (decoder->event_callback)
                    decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_INVALID_ADDRESS,
                                            byte, 0, byte, 0);
                ras_rx_reset(decoder, byte);
                break;
            }
            decoder->address = address;
            decoder->crc = decoder->escaped_frame ? 0 : ras_crc_update(0, address);
            decoder->state = MODEM_RAS_RX_LENGTH_HIGH;
            break;
        }

        case MODEM_RAS_RX_LENGTH_HIGH:
            if (decoder->escaped_frame) {
                if (!(byte & 0x80)) {
                    if (decoder->event_callback)
                        decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_INVALID_LENGTH,
                                                decoder->address, 0, byte, 0x80);
                    ras_rx_reset(decoder, byte);
                    break;
                }
                decoder->wire_length = (uint16_t) (byte & 0x7f) << 7;
            } else {
                decoder->length = (uint16_t) byte << 8;
                decoder->crc = ras_crc_update(decoder->crc, byte);
            }
            decoder->state = MODEM_RAS_RX_LENGTH_LOW;
            break;

        case MODEM_RAS_RX_LENGTH_LOW:
            if (decoder->escaped_frame) {
                if (!(byte & 0x80)) {
                    if (decoder->event_callback)
                        decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_INVALID_LENGTH,
                                                decoder->address, 0, byte, 0x80);
                    ras_rx_reset(decoder, byte);
                    break;
                }
                decoder->wire_length |= byte & 0x7f;
                if (decoder->wire_length < 4
                    || decoder->wire_length > 2 * (MODEM_RAS_MAX_PAYLOAD + 3)) {
                    if (decoder->event_callback)
                        decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_INVALID_LENGTH,
                                                decoder->address, decoder->wire_length,
                                                decoder->wire_length,
                                                2 * (MODEM_RAS_MAX_PAYLOAD + 3));
                    ras_rx_reset(decoder, byte);
                    break;
                }
                decoder->state = MODEM_RAS_RX_ESCAPED_BODY;
            } else {
                decoder->length |= byte;
                decoder->crc = ras_crc_update(decoder->crc, byte);
                if (!decoder->length || decoder->length > MODEM_RAS_MAX_PAYLOAD) {
                    if (decoder->event_callback)
                        decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_INVALID_LENGTH,
                                                decoder->address, decoder->length, decoder->length,
                                                MODEM_RAS_MAX_PAYLOAD);
                    ras_rx_reset(decoder, byte);
                    break;
                }
                decoder->position = 0;
                decoder->state = MODEM_RAS_RX_PAYLOAD;
            }
            break;

        case MODEM_RAS_RX_PAYLOAD:
            decoder->payload[decoder->position++] = byte;
            decoder->crc = ras_crc_update(decoder->crc, byte);
            if (decoder->position == decoder->length)
                decoder->state = MODEM_RAS_RX_ETX;
            break;

        case MODEM_RAS_RX_ETX:
            if (byte != MODEM_RAS_ETX) {
                if (decoder->event_callback)
                    decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_INVALID_ETX,
                                            decoder->address, decoder->length, byte,
                                            MODEM_RAS_ETX);
                ras_rx_reset(decoder, byte);
                break;
            }
            decoder->crc = ras_crc_update(decoder->crc, byte);
            decoder->state = MODEM_RAS_RX_CRC_LOW;
            break;

        case MODEM_RAS_RX_CRC_LOW:
            decoder->received_crc_low = byte;
            decoder->state = MODEM_RAS_RX_CRC_HIGH;
            break;

        case MODEM_RAS_RX_CRC_HIGH:
        {
            const uint16_t received_crc = ((uint16_t) byte << 8) | decoder->received_crc_low;

            if (received_crc == decoder->crc) {
                if (decoder->event_callback)
                    decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_FRAME_RECEIVED,
                                            decoder->address, decoder->length, 0, 0);
                if (decoder->callback)
                    decoder->callback(decoder->priv, decoder->address, decoder->payload,
                                      decoder->length);
            } else if (decoder->event_callback)
                decoder->event_callback(decoder->priv, MODEM_RAS_EVENT_BAD_CRC,
                                        decoder->address, decoder->length, received_crc,
                                        decoder->crc);
            ras_rx_complete(decoder);
            break;
        }

        case MODEM_RAS_RX_ESCAPED_BODY:
            ras_rx_escaped_body_byte(decoder, byte);
            break;
    }
}

bool
modem_ras_encode_frame(uint8_t address, const uint8_t *payload, size_t length,
                       uint8_t *output, size_t output_capacity, size_t *output_length)
{
    uint16_t crc;
    size_t   frame_length;

    if (output_length)
        *output_length = 0;
    if ((address != MODEM_RAS_BROADCAST && address != MODEM_RAS_DESTINATION)
        || !payload || !length || length > MODEM_RAS_MAX_PAYLOAD || !output || !output_length)
        return false;

    frame_length = length + 7;
    if (output_capacity < frame_length)
        return false;

    output[0] = MODEM_RAS_SYNC;
    output[1] = address;
    output[2] = (uint8_t) (length >> 8);
    output[3] = (uint8_t) length;
    memcpy(output + 4, payload, length);
    output[4 + length] = MODEM_RAS_ETX;

    crc = 0;
    for (size_t i = 1; i < length + 5; i++)
        crc = ras_crc_update(crc, output[i]);
    output[5 + length] = (uint8_t) crc;
    output[6 + length] = (uint8_t) (crc >> 8);
    *output_length = frame_length;
    return true;
}

static bool
ras_xon_xoff_escape(uint8_t byte, uint32_t mask)
{
    return byte == MODEM_RAS_ESCAPE || (byte < 32 && (mask & (1u << byte)));
}

bool
modem_ras_encode_frame_xon_xoff(uint8_t address, const uint8_t *payload, size_t length,
                                uint32_t mask, uint8_t *output, size_t output_capacity,
                                size_t *output_length)
{
    uint16_t crc = 0;
    size_t   body_length = 0;
    size_t   frame_length;
    size_t   position;

    if (output_length)
        *output_length = 0;
    if ((address != MODEM_RAS_BROADCAST && address != MODEM_RAS_DESTINATION)
        || !payload || !length || length > MODEM_RAS_MAX_PAYLOAD || !output || !output_length)
        return false;

    for (size_t i = 0; i < length; i++) {
        crc = ras_crc_update(crc, payload[i]);
        body_length += ras_xon_xoff_escape(payload[i], mask) ? 2 : 1;
    }

    const uint8_t trailer[] = {
        MODEM_RAS_ETX,
        (uint8_t) crc,
        (uint8_t) (crc >> 8)
    };
    for (size_t i = 0; i < sizeof(trailer); i++)
        body_length += ras_xon_xoff_escape(trailer[i], mask) ? 2 : 1;

    frame_length = body_length + 4;
    if (body_length > 0x3fff || output_capacity < frame_length)
        return false;

    output[0] = MODEM_RAS_SYNC | MODEM_RAS_ESCAPE_FLAG;
    output[1] = address | MODEM_RAS_ESCAPE_FLAG;
    output[2] = (uint8_t) (((body_length >> 7) & 0x7f) | 0x80);
    output[3] = (uint8_t) ((body_length & 0x7f) | 0x80);
    position = 4;
    for (size_t i = 0; i < length; i++) {
        if (ras_xon_xoff_escape(payload[i], mask))
            output[position++] = MODEM_RAS_ESCAPE;
        output[position++] = ras_xon_xoff_escape(payload[i], mask) ? payload[i] ^ 0x20 : payload[i];
    }
    for (size_t i = 0; i < sizeof(trailer); i++) {
        if (ras_xon_xoff_escape(trailer[i], mask))
            output[position++] = MODEM_RAS_ESCAPE;
        output[position++] = ras_xon_xoff_escape(trailer[i], mask) ? trailer[i] ^ 0x20 : trailer[i];
    }

    *output_length = frame_length;
    return true;
}

bool
modem_ras_send_nbf(modem_ras_serial_push_t serial_push, void *priv,
                   const uint8_t *frame, int length)
{
    uint8_t output[MODEM_RAS_MAX_PAYLOAD + 7];
    size_t  output_length;

    if (!serial_push || !frame || length <= 0
        || !modem_ras_encode_frame(MODEM_RAS_DESTINATION, frame, (size_t) length,
                                   output, sizeof(output), &output_length))
        return false;

    serial_push(priv, output, (int) output_length);
    return true;
}

bool
modem_ras_send_nbf_xon_xoff(modem_ras_serial_push_t serial_push, void *priv,
                            const uint8_t *frame, int length, uint32_t mask)
{
    uint8_t output[MODEM_RAS_MAX_ENCODED_FRAME_SIZE];
    size_t  output_length;

    if (!serial_push || !frame || length <= 0
        || !modem_ras_encode_frame_xon_xoff(MODEM_RAS_DESTINATION, frame, (size_t) length,
                                            mask, output, sizeof(output), &output_length))
        return false;

    serial_push(priv, output, (int) output_length);
    return true;
}

static uint16_t
ras_read_le16(const uint8_t *data)
{
    return (uint16_t) (data[0] | ((uint16_t) data[1] << 8));
}

static void
ras_write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) value;
    data[1] = (uint8_t) (value >> 8);
}

static uint32_t
ras_read_le32(const uint8_t *data)
{
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8)
         | ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);
}

static void
ras_write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t) value;
    data[1] = (uint8_t) (value >> 8);
    data[2] = (uint8_t) (value >> 16);
    data[3] = (uint8_t) (value >> 24);
}

static uint32_t
ras_min_frame_size(uint32_t requested)
{
    return requested < MODEM_RAS_MAX_FRAME_SIZE ? requested : MODEM_RAS_MAX_FRAME_SIZE;
}

static bool
ras_zero_padding(const uint8_t *data, size_t start, size_t length)
{
    for (size_t i = start; i < length; i++) {
        if (data[i])
            return false;
    }
    return true;
}

typedef struct modem_ras_response_view_t {
    const uint8_t *username;
    size_t         username_length;
    const uint8_t *response;
    bool           use_nt_response;
} modem_ras_response_view_t;

static bool
ras_parse_response(const uint8_t *message, size_t length,
                   modem_ras_response_view_t *view)
{
    size_t username_length;
    size_t response_offset;
    size_t message_length;
    const uint8_t *terminator;

    if (!message || !view || !length)
        return false;

    switch (message[0]) {
        case MODEM_RAS_FRAME_RESPONSE:
            message_length = MODEM_RAS_RESPONSE_10_LENGTH;
            username_length = MODEM_RAS_RESPONSE_10_USERNAME_LENGTH;
            response_offset = 1 + username_length;
            if (length != message_length && length != MODEM_RAS_PADDED_PROTOCOL_MESSAGE_LENGTH)
                return false;
            break;

        case MODEM_RAS_FRAME_RESPONSE_20:
            message_length = MODEM_RAS_RESPONSE_20_LENGTH;
            username_length = MODEM_RAS_RESPONSE_20_USERNAME_LENGTH;
            response_offset = MODEM_RAS_RESPONSE_20_LM_OFFSET;
            if ((length != message_length && length != MODEM_RAS_PADDED_PROTOCOL_MESSAGE_LENGTH)
                || !memchr(message + 1 + username_length, 0,
                           MODEM_RAS_RESPONSE_20_DOMAIN_LENGTH))
                return false;
            {
                const uint32_t use_nt_response =
                    ras_read_le32(message + MODEM_RAS_RESPONSE_20_USE_NT_OFFSET);
                if (use_nt_response > 1)
                    return false;
                view->use_nt_response = use_nt_response != 0;
                if (view->use_nt_response)
                    response_offset = MODEM_RAS_RESPONSE_20_NT_OFFSET;
            }
            break;

        default:
            return false;
    }

    if (length > message_length && !ras_zero_padding(message, message_length, length))
        return false;

    terminator = (const uint8_t *) memchr(message + 1, 0, username_length);
    if (!terminator)
        return false;

    view->username = message + 1;
    view->username_length = (size_t) (terminator - view->username);
    view->response = message + response_offset;
    if (message[0] == MODEM_RAS_FRAME_RESPONSE)
        view->use_nt_response = false;
    return true;
}

static bool
ras_lm_response(const char *password, const uint8_t challenge[MODEM_RAS_CHALLENGE_LENGTH],
                uint8_t response[MODEM_RAS_RESPONSE_LENGTH])
{
    static const uint8_t magic[8] = { 'K', 'G', 'S', '!', '@', '#', '$', '%' };
    uint8_t upper_password[14] = { 0 };
    uint8_t password_hash[16];
    uint8_t padded_hash[21] = { 0 };
    size_t  password_length;

    if (!password || !challenge || !response)
        return false;

    password_length = strlen(password);
    if (password_length > sizeof(upper_password))
        password_length = sizeof(upper_password);
    for (size_t i = 0; i < password_length; i++) {
        uint8_t value = (uint8_t) password[i];
        if (value >= 0x80) {
            memset(upper_password, 0, sizeof(upper_password));
            return false;
        }
        if (value >= 'a' && value <= 'z')
            value = (uint8_t) (value - 'a' + 'A');
        upper_password[i] = value;
    }

    modem_des_encrypt_block(upper_password, magic, password_hash);
    modem_des_encrypt_block(upper_password + 7, magic, password_hash + 8);
    memcpy(padded_hash, password_hash, sizeof(password_hash));
    for (size_t i = 0; i < 3; i++)
        modem_des_encrypt_block(padded_hash + i * 7, challenge, response + i * 8);

    memset(upper_password, 0, sizeof(upper_password));
    memset(password_hash, 0, sizeof(password_hash));
    memset(padded_hash, 0, sizeof(padded_hash));
    return true;
}

static bool
ras_nt_response(const char *password, const uint8_t challenge[MODEM_RAS_CHALLENGE_LENGTH],
                uint8_t response[MODEM_RAS_RESPONSE_LENGTH])
{
    uint8_t unicode_password[128] = { 0 };
    uint8_t password_hash[MD4_DIGEST_LENGTH];
    uint8_t padded_hash[21] = { 0 };
    size_t  password_length;

    if (!password || !challenge || !response)
        return false;

    password_length = strlen(password);
    if (password_length > sizeof(unicode_password) / 2)
        return false;
    for (size_t i = 0; i < password_length; i++) {
        unicode_password[i * 2] = (uint8_t) password[i];
        unicode_password[i * 2 + 1] = 0;
    }

    modem_md4(unicode_password, password_length * 2, password_hash);
    memcpy(padded_hash, password_hash, sizeof(password_hash));
    for (size_t i = 0; i < 3; i++)
        modem_des_encrypt_block(padded_hash + i * 7, challenge, response + i * 8);

    memset(unicode_password, 0, sizeof(unicode_password));
    memset(password_hash, 0, sizeof(password_hash));
    memset(padded_hash, 0, sizeof(padded_hash));
    return true;
}

static bool
ras_response_matches_credentials(const modem_ras_response_view_t *view,
                                 const char *username, const char *password,
                                 const uint8_t challenge[MODEM_RAS_CHALLENGE_LENGTH])
{
    uint8_t expected[MODEM_RAS_RESPONSE_LENGTH];
    const size_t username_length = strlen(username);
    bool matches;

    if (view->username_length != username_length
        || memcmp(view->username, username, username_length))
        return false;

    if (!(view->use_nt_response ? ras_nt_response(password, challenge, expected)
                                : ras_lm_response(password, challenge, expected)))
        return false;

    matches = modem_constant_time_equal(view->response, expected, sizeof(expected));
    memset(expected, 0, sizeof(expected));
    return matches;
}

static bool
ras_read_projected_names(modem_ras_session_t *session, const uint8_t *message,
                         uint32_t name_count)
{
    memset(session->projected_names, 0, sizeof(session->projected_names));
    session->projected_name_count = 0;

    for (uint32_t i = 0; i < name_count; i++) {
        const uint8_t *record = message + MODEM_RAS_CONFIG_NB_NAMES_OFFSET
                                + i * MODEM_RAS_CONFIG_NAME_RECORD_LENGTH;
        modem_ras_projected_name_t *name = &session->projected_names[i];
        bool                        nonempty = false;

        for (size_t j = 0; j < MODEM_RAS_PROJECTED_NAME_LENGTH - 1; j++) {
            if (record[j] == 0)
                return false;
            nonempty |= record[j] != ' ';
        }
        if (!nonempty)
            return false;

        name->flags = ras_read_le16(record + MODEM_RAS_PROJECTED_NAME_LENGTH);
        if (name->flags > UINT8_MAX)
            return false;
        memcpy(name->name, record, sizeof(name->name));
    }

    session->projected_name_count = (uint16_t) name_count;
    return true;
}

static bool
ras_build_configuration_result(modem_ras_session_t *session, const uint8_t *message, size_t length,
                               uint8_t result[MODEM_RAS_CONFIG_RESULT_LENGTH],
                               bool *supported)
{
    uint32_t default_callback;
    uint32_t nb_project;
    uint32_t ip_project;
    uint32_t ipx_project;
    uint32_t name_count;

    if (!session || !message || !result || !supported
        || length != MODEM_RAS_CONFIG_REQUEST_LENGTH
        || message[0] != MODEM_RAS_FRAME_CONFIGURATION_REQUEST
        || ras_read_le32(message + 1) != MODEM_RAS_PROTOCOL_VERSION_20)
        return false;

    default_callback = ras_read_le32(message + 5);
    nb_project = ras_read_le32(message + MODEM_RAS_CONFIG_NB_PROJECT_OFFSET);
    name_count = ras_read_le16(message + MODEM_RAS_CONFIG_NB_NAME_COUNT_OFFSET);
    ip_project = ras_read_le32(message + MODEM_RAS_CONFIG_IP_PROJECT_OFFSET);
    ipx_project = ras_read_le32(message + MODEM_RAS_CONFIG_IPX_PROJECT_OFFSET);
    if (default_callback > 1 || nb_project > 1 || ip_project > 1 || ipx_project > 1
        || name_count > MODEM_RAS_MAX_PROJECTED_NAMES)
        return false;
    if (!ras_read_projected_names(session, message, name_count))
        return false;

    *supported = default_callback == 0 && nb_project != 0 && name_count != 0
                  && ip_project == 0 && ipx_project == 0;
    if (!*supported) {
        memset(session->projected_names, 0, sizeof(session->projected_names));
        session->projected_name_count = 0;
    }

    memset(result, 0, MODEM_RAS_CONFIG_RESULT_LENGTH);
    result[0] = MODEM_RAS_FRAME_CONFIGURATION_RESULT;
    if (!*supported)
        ras_write_le32(result + 1, MODEM_RAS_RESULT_PROJECTIONS_FAILED);

    /* WFW reports negotiated sizes and the client-requested DTE rate here. */
    ras_write_le32(result + 13,
                   ras_min_frame_size(ras_read_le32(message + MODEM_RAS_CONFIG_MAX_SEND_OFFSET)));
    ras_write_le32(result + 17,
                   ras_min_frame_size(ras_read_le32(message + MODEM_RAS_CONFIG_MAX_RECV_OFFSET)));
    ras_write_le32(result + 21,
                   ras_read_le32(message + MODEM_RAS_CONFIG_LINK_SPEED_OFFSET));
    ras_write_le32(result + 25, *supported ? MODEM_RAS_RESULT_NAMES_ADDED
                                          : MODEM_RAS_RESULT_PROJECTIONS_FAILED);
    if (*supported) {
        /* These two words are set in the captured WFW type-108 response. */
        ras_write_le16(result + 29, 1);
        ras_write_le16(result + 31, 1);
        ras_write_le16(result + MODEM_RAS_CONFIG_RESULT_NAME_COUNT_OFFSET, name_count);
        for (uint32_t i = 0; i < name_count; i++) {
            const modem_ras_projected_name_t *name = &session->projected_names[i];
            uint8_t *record = result + MODEM_RAS_CONFIG_RESULT_NAMES_OFFSET
                              + i * MODEM_RAS_CONFIG_RESULT_NAME_RECORD_LENGTH;

            memcpy(record, name->name, MODEM_RAS_PROJECTED_NAME_LENGTH);
            ras_write_le16(record + MODEM_RAS_PROJECTED_NAME_LENGTH, name->flags);
        }
    }

    return true;
}

void
modem_ras_session_init(modem_ras_session_t *session)
{
    if (session)
        memset(session, 0, sizeof(*session));
}

modem_ras_session_result_t
modem_ras_session_process_message(modem_ras_session_t *session, const uint8_t *message,
                                  size_t length, const char *username, const char *password,
                                  modem_ras_message_send_t send, void *send_priv,
                                  modem_ras_random_bytes_t random_bytes)
{
    if (!session || !message || !send)
        return MODEM_RAS_SESSION_INVALID_MESSAGE;
    if (!username)
        username = "";
    if (!password)
        password = "";

    switch (session->state) {
        case MODEM_RAS_SESSION_WAIT_PROTOCOL:
        {
            uint8_t protocol_response[MODEM_RAS_PROTOCOL_MESSAGE_LENGTH] = { MODEM_RAS_FRAME_PROTOCOL };
            uint8_t challenge_frame[1 + MODEM_RAS_CHALLENGE_LENGTH] = { MODEM_RAS_FRAME_CHALLENGE };

            if ((length != sizeof(protocol_response)
                 && length != MODEM_RAS_PADDED_PROTOCOL_MESSAGE_LENGTH)
                || message[0] != MODEM_RAS_FRAME_PROTOCOL)
                return MODEM_RAS_SESSION_INVALID_MESSAGE;
            for (size_t i = sizeof(protocol_response); i < length; i++) {
                if (message[i] != 0)
                    return MODEM_RAS_SESSION_INVALID_MESSAGE;
            }
            if (!random_bytes)
                return MODEM_RAS_SESSION_RANDOM_FAILURE;

            session->client_version = ras_read_le16(message + 1);
            ras_write_le16(protocol_response + 1, MODEM_RAS_PROTOCOL_VERSION_20);
            if (!send(send_priv, protocol_response, sizeof(protocol_response)))
                return MODEM_RAS_SESSION_SEND_FAILURE;
            if (!random_bytes(session->challenge, sizeof(session->challenge)))
                return MODEM_RAS_SESSION_RANDOM_FAILURE;

            memcpy(challenge_frame + 1, session->challenge, sizeof(session->challenge));
            if (!send(send_priv, challenge_frame, sizeof(challenge_frame)))
                return MODEM_RAS_SESSION_SEND_FAILURE;

            session->state = MODEM_RAS_SESSION_WAIT_RESPONSE;
            return MODEM_RAS_SESSION_PROTOCOL_NEGOTIATED;
        }

        case MODEM_RAS_SESSION_WAIT_RESPONSE:
        {
            uint8_t result_frame[3] = { MODEM_RAS_FRAME_RESULT };
            modem_ras_response_view_t response;
            bool credentials_configured = username[0] || password[0];
            bool authenticated;

            if (!ras_parse_response(message, length, &response))
                return MODEM_RAS_SESSION_INVALID_MESSAGE;

            authenticated = !credentials_configured
                            || ras_response_matches_credentials(&response, username, password,
                                                                session->challenge);
            ras_write_le16(result_frame + 1, authenticated
                                                ? MODEM_RAS_RESULT_AUTHENTICATED
                                                : MODEM_RAS_RESULT_NOT_AUTHENTICATED);
            if (!send(send_priv, result_frame, sizeof(result_frame)))
                return MODEM_RAS_SESSION_SEND_FAILURE;

            session->state = authenticated ? MODEM_RAS_SESSION_AUTHENTICATED
                                           : MODEM_RAS_SESSION_REJECTED;
            return authenticated ? MODEM_RAS_SESSION_AUTHENTICATED_RESULT
                                 : MODEM_RAS_SESSION_AUTH_REJECTED;
        }

        case MODEM_RAS_SESSION_AUTHENTICATED:
        {
            uint8_t configuration_result[MODEM_RAS_CONFIG_RESULT_LENGTH];
            uint8_t link_speed_result[5] = { MODEM_RAS_FRAME_LINK_SPEED };
            bool    supported;

            if (!ras_build_configuration_result(session, message, length, configuration_result,
                                                &supported))
                return MODEM_RAS_SESSION_INVALID_MESSAGE;
            if (!send(send_priv, configuration_result, sizeof(configuration_result)))
                return MODEM_RAS_SESSION_SEND_FAILURE;
            ras_write_le32(link_speed_result + 1,
                           MODEM_RAS_LINK_SPEED_NOT_MEASURED);
            if (supported && !send(send_priv, link_speed_result, sizeof(link_speed_result)))
                return MODEM_RAS_SESSION_SEND_FAILURE;

            session->state = supported ? MODEM_RAS_SESSION_CONFIGURED
                                       : MODEM_RAS_SESSION_REJECTED;
            return supported ? MODEM_RAS_SESSION_CONFIGURATION_ACCEPTED
                             : MODEM_RAS_SESSION_CONFIGURATION_REJECTED;
        }

        case MODEM_RAS_SESSION_CONFIGURED:
            return MODEM_RAS_SESSION_POST_CONFIGURATION_UNIMPLEMENTED;

        case MODEM_RAS_SESSION_REJECTED:
            return MODEM_RAS_SESSION_INVALID_MESSAGE;
    }

    return MODEM_RAS_SESSION_INVALID_MESSAGE;
}
