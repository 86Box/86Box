/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Multilink PPP fragmentation and reassembly interfaces.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_MPPP_H
#define EMU_MODEM_MPPP_H

#ifdef __cplusplus
extern "C" {
#endif

#define PPP_PROTO_MULTILINK 0x003D
#define PPP_MPPP_MAX_LINKS 16
#define PPP_MPPP_REORDER_WINDOW 64

typedef struct ppp_mppp_fragment_t {
    uint8_t *data;
    size_t   length;
    uint32_t sequence;
    bool     begin;
    bool     end;
    bool     occupied;
} ppp_mppp_fragment_t;

typedef struct ppp_mppp_reassembler_t {
    uint8_t             sequence_bits;
    uint32_t            sequence_mask;
    uint32_t            next_sequence;
    size_t              mrru;
    size_t              packet_length;
    uint8_t            *packet;
    bool                sequence_initialized;
    bool                assembling;
    ppp_mppp_fragment_t fragments[PPP_MPPP_REORDER_WINDOW];
} ppp_mppp_reassembler_t;

typedef void (*ppp_mppp_packet_cb)(void *opaque, const uint8_t *packet, size_t length);
typedef bool (*ppp_mppp_fragment_cb)(void *opaque, const uint8_t *fragment, size_t length);

typedef struct ppp_mppp_tx_link_t {
    ppp_mppp_fragment_cb send;
    void                *opaque;
    size_t               max_fragment_payload;
} ppp_mppp_tx_link_t;

typedef struct ppp_mppp_sender_t {
    uint32_t          next_sequence;
    size_t            mrru;
    uint8_t           sequence_bits;
    uint32_t          sequence_mask;
    size_t            next_link;
    size_t            link_count;
    ppp_mppp_tx_link_t links[PPP_MPPP_MAX_LINKS];
} ppp_mppp_sender_t;

bool   ppp_mppp_reassembler_init(ppp_mppp_reassembler_t *state, size_t mrru,
                                 bool short_sequence);
void   ppp_mppp_reassembler_close(ppp_mppp_reassembler_t *state);
void   ppp_mppp_reassembler_reset(ppp_mppp_reassembler_t *state);
bool   ppp_mppp_reassembler_input(ppp_mppp_reassembler_t *state,
                                  const uint8_t *fragment, size_t fragment_length,
                                  ppp_mppp_packet_cb callback, void *opaque);
bool   ppp_mppp_sender_init(ppp_mppp_sender_t *state, size_t mrru,
                            bool short_sequence);
bool   ppp_mppp_sender_add_link(ppp_mppp_sender_t *state,
                                ppp_mppp_fragment_cb send, void *opaque,
                                size_t max_fragment_payload);
void   ppp_mppp_sender_remove_link(ppp_mppp_sender_t *state, void *opaque);
bool   ppp_mppp_sender_send(ppp_mppp_sender_t *state, uint16_t protocol,
                            const uint8_t *data, size_t length);
size_t ppp_mppp_header_size(bool short_sequence);
bool   ppp_mppp_encode_header(uint8_t *header, size_t capacity, bool short_sequence,
                              uint32_t sequence, bool begin, bool end);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MODEM_MPPP_H */
