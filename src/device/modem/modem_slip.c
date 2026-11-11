/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          SLIP framing for modem emulation. RFC 1055.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <86box/log.h>
#include <86box/modem/modem_slip.h>
#include <86box/modem/modem_debug.h>

#if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
static size_t
slip_escape_count(const uint8_t *frame, size_t frame_len)
{
    size_t escapes = 0;

    if (!frame)
        return 0;

    for (size_t pos = 0; pos < frame_len; pos++) {
        if (frame[pos] == SLIP_ESC) {
            escapes++;
            if (pos + 1 < frame_len)
                pos++;
        }
    }
    return escapes;
}
#endif

bool
slip_decode_frame(const uint8_t *frame, size_t frame_len,
                  uint8_t *packet, size_t packet_capacity, size_t *packet_len)
{
    size_t pos = 0;
    size_t decoded_len = 0;

    if (!packet_len)
        return false;

    *packet_len = 0;
    if (!frame || frame_len == 0 || frame[frame_len - 1] != SLIP_END)
        return false;

    if (frame[0] == SLIP_END)
        pos++;

    while (pos < frame_len - 1) {
        uint8_t byte = frame[pos++];

        if (byte == SLIP_END)
            return false;

        if (byte == SLIP_ESC) {
            if (pos >= frame_len - 1)
                return false;

            byte = frame[pos++];
            if (byte == SLIP_ESC_END)
                byte = SLIP_END;
            else if (byte == SLIP_ESC_ESC)
                byte = SLIP_ESC;
            else
                return false;
        }

        if (!packet || decoded_len >= packet_capacity)
            return false;

        packet[decoded_len++] = byte;
    }

    *packet_len = decoded_len;
    return true;
}

bool
slip_encode_frame(const uint8_t *packet, size_t packet_len,
                  uint8_t *frame, size_t frame_capacity, size_t *frame_len)
{
    size_t encoded_len = 0;

    if (!frame_len)
        return false;

    *frame_len = 0;
    if ((!packet && packet_len != 0) || !frame || frame_capacity < 2)
        return false;

    frame[encoded_len++] = SLIP_END;
    for (size_t pos = 0; pos < packet_len; pos++) {
        uint8_t byte = packet[pos];
        size_t required = (byte == SLIP_END || byte == SLIP_ESC) ? 2 : 1;
        if (required + 1 > frame_capacity - encoded_len)
            return false;

        if (byte == SLIP_END) {
            frame[encoded_len++] = SLIP_ESC;
            frame[encoded_len++] = SLIP_ESC_END;
        } else if (byte == SLIP_ESC) {
            frame[encoded_len++] = SLIP_ESC;
            frame[encoded_len++] = SLIP_ESC_ESC;
        } else {
            frame[encoded_len++] = byte;
        }
    }

    frame[encoded_len++] = SLIP_END;
    *frame_len = encoded_len;
    return true;
}

bool
slip_packet_looks_like_ppp(const uint8_t *packet, size_t packet_len)
{
    size_t pos = 0;

    if (!packet)
        return false;

    while (pos < packet_len && pos < 2 && packet[pos] == 0x7E)
        pos++;
    if (packet_len < pos + 2 || packet[pos] != 0xFF)
        return false;
    return packet[pos + 1] == 0x03
        || (packet_len >= pos + 3 && packet[pos + 1] == 0x7D && packet[pos + 2] == 0x23);
}

bool
slip_decode_frame_logged(void *log, const uint8_t *frame, size_t frame_len,
                         uint8_t *packet, size_t packet_capacity, size_t *packet_len)
{
    MODEM_DEBUG_LOG(log, "SLIP: RX wire frame bytes=%u\n", (unsigned) frame_len);
    bool decoded = slip_decode_frame(frame, frame_len, packet, packet_capacity, packet_len);
    if (!decoded || !packet_len || *packet_len == 0) {
        MODEM_DEBUG_LOG(log, "SLIP: RX frame rejected bytes=%u escapes=%u result=%s\n",
                        (unsigned) frame_len,
                        (unsigned) slip_escape_count(frame, frame_len),
                        decoded ? "empty" : "malformed-or-too-large");
        return decoded;
    }

    MODEM_DEBUG_LOG(log, "SLIP: RX decoded packet bytes=%u escapes=%u\n",
                    (unsigned) *packet_len,
                    (unsigned) slip_escape_count(frame, frame_len));
    return true;
}

bool
slip_encode_frame_logged(void *log, const uint8_t *packet, size_t packet_len,
                         uint8_t *frame, size_t frame_capacity, size_t *frame_len)
{
    bool encoded = slip_encode_frame(packet, packet_len, frame, frame_capacity, frame_len);
    if (encoded) {
        MODEM_DEBUG_LOG(log, "SLIP: TX frame payload=%u wire-bytes=%u escapes=%u\n",
                        (unsigned) packet_len, (unsigned) *frame_len,
                        (unsigned) slip_escape_count(frame, *frame_len));
    }
    return encoded;
}
