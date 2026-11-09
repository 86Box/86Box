/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Stateful and stateless 40-, 56-, and 128-bit MPPE support for
 *          modem PPP. RFC 3078/3079.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 *
 */
#ifndef EMU_MODEM_MPPE_H
#define EMU_MODEM_MPPE_H

#ifdef __cplusplus
extern "C" {
#endif

#define PPP_MPPE_HEADER_LENGTH 2
#define PPP_MPPE_FLAG_FLUSHED  0x80
#define PPP_MPPE_FLAG_AT_FRONT 0x40
#define PPP_MPPE_FLAG_COMPRESSED 0x20
#define PPP_MPPE_FLAG_ENCRYPTED 0x10
#define PPP_MPPE_FLAGS_MASK    0xF0

struct ppp_mppe_state_t;

void ppp_mppe_derive_mschapv2_keys(const uint8_t password_hash_hash[16],
                                   const uint8_t nt_response[24],
                                   struct ppp_mppe_state_t *send_state,
                                   struct ppp_mppe_state_t *receive_state);
void ppp_mppe_derive_mschapv1_keys(const uint8_t password_hash_hash[16],
                                   const uint8_t lm_password_hash[16],
                                   const uint8_t challenge[8],
                                   struct ppp_mppe_state_t *send_state,
                                   struct ppp_mppe_state_t *receive_state);
bool ppp_mppe_configure(struct ppp_mppe_state_t *state, uint8_t key_bits, bool stateful);
void ppp_mppe_request_reset(struct ppp_mppe_state_t *state);
bool ppp_mppe_encrypt(struct ppp_mppe_state_t *state, const uint8_t *plain, size_t plain_len,
                      uint8_t mppc_flags, uint8_t *out, size_t out_capacity, size_t *out_len);
bool ppp_mppe_decrypt(struct ppp_mppe_state_t *state, const uint8_t *packet, size_t packet_len,
                      uint8_t *out, size_t out_capacity, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MODEM_MPPE_H */
