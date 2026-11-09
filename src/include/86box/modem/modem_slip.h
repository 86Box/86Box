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
#ifndef EMU_MODEM_SLIP_H
#define EMU_MODEM_SLIP_H

#define SLIP_END     0300
#define SLIP_ESC     0333
#define SLIP_ESC_END 0334
#define SLIP_ESC_ESC 0335

#ifdef __cplusplus
extern "C" {
#endif

bool slip_decode_frame(const uint8_t *frame, size_t frame_len,
                       uint8_t *packet, size_t packet_capacity, size_t *packet_len);
bool slip_encode_frame(const uint8_t *packet, size_t packet_len,
                       uint8_t *frame, size_t frame_capacity, size_t *frame_len);
bool slip_decode_frame_logged(void *log, const uint8_t *frame, size_t frame_len,
                              uint8_t *packet, size_t packet_capacity, size_t *packet_len);
bool slip_encode_frame_logged(void *log, const uint8_t *packet, size_t packet_len,
                              uint8_t *frame, size_t frame_capacity, size_t *frame_len);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MODEM_SLIP_H */
