/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Stateful and stateless 40-, 56-, and 128-bit MPPE support for
 *          modem PPP, including RFC 3079 key derivation (RFC 3078/3079).
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_crypto.h>
#include <86box/modem/modem_mppe.h>

static const uint8_t sha_pad1[40] = { 0 };
static const uint8_t sha_pad2[40] = {
    0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2,
    0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2,
    0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2,
    0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2
};

static const uint8_t magic_master[] = "This is the MPPE Master Key";
static const uint8_t magic_client_send[] =
    "On the client side, this is the send key; on the server side, it is the receive key.";
static const uint8_t magic_client_receive[] =
    "On the client side, this is the receive key; on the server side, it is the send key.";

static void
mppe_sha1_segments(const uint8_t *first, size_t first_len,
                   const uint8_t *second, size_t second_len,
                   const uint8_t *third, size_t third_len,
                   const uint8_t *fourth, size_t fourth_len,
                   uint8_t digest[SHA1_DIGEST_LENGTH])
{
    modem_sha1_ctx_t sha1;
    modem_sha1_init(&sha1);
    modem_sha1_update(&sha1, first, first_len);
    modem_sha1_update(&sha1, second, second_len);
    modem_sha1_update(&sha1, third, third_len);
    modem_sha1_update(&sha1, fourth, fourth_len);
    modem_sha1_final(&sha1, digest);
}

static void
mppe_rc4_init(ppp_mppe_rc4_state_t *rc4, const uint8_t *key, uint8_t key_length)
{
    uint8_t j = 0;
    for (int i = 0; i < 256; i++)
        rc4->s[i] = (uint8_t) i;
    for (int i = 0; i < 256; i++) {
        j = (uint8_t) (j + rc4->s[i] + key[i % key_length]);
        uint8_t t = rc4->s[i];
        rc4->s[i] = rc4->s[j];
        rc4->s[j] = t;
    }
    rc4->i = 0;
    rc4->j = 0;
}

static void
mppe_rc4_crypt(ppp_mppe_rc4_state_t *rc4, const uint8_t *input,
               uint8_t *output, size_t len)
{
    for (size_t pos = 0; pos < len; pos++) {
        rc4->i++;
        rc4->j = (uint8_t) (rc4->j + rc4->s[rc4->i]);
        uint8_t t = rc4->s[rc4->i];
        rc4->s[rc4->i] = rc4->s[rc4->j];
        rc4->s[rc4->j] = t;
        uint8_t key_byte = rc4->s[(uint8_t) (rc4->s[rc4->i] + rc4->s[rc4->j])];
        output[pos] = input[pos] ^ key_byte;
    }
}

static void
mppe_get_new_key_from_sha(const uint8_t start_key[PPP_MPPE_KEY_LENGTH],
                          const uint8_t session_key[PPP_MPPE_KEY_LENGTH],
                          uint8_t key_length, uint8_t new_key[PPP_MPPE_KEY_LENGTH])
{
    uint8_t digest[SHA1_DIGEST_LENGTH];
    mppe_sha1_segments(start_key, key_length,
                       sha_pad1, sizeof(sha_pad1),
                       session_key, key_length,
                       sha_pad2, sizeof(sha_pad2), digest);
    memcpy(new_key, digest, key_length);
    memset(digest, 0, sizeof(digest));
}

static void
mppe_reduce_key(ppp_mppe_state_t *state)
{
    if (state->key_bits == 40) {
        state->session_key[0] = 0xD1;
        state->session_key[1] = 0x26;
        state->session_key[2] = 0x9E;
    } else if (state->key_bits == 56) {
        state->session_key[0] = 0xD1;
    }
}

static const uint8_t *
mppe_start_key(const ppp_mppe_state_t *state)
{
    if (state->mschapv1_key_derivation && state->key_bits != 128)
        return state->mschapv1_weak_start_key;
    return state->start_key;
}

static void
mppe_rekey(ppp_mppe_state_t *state)
{
    uint8_t interim[PPP_MPPE_KEY_LENGTH] = { 0 };
    uint8_t next_key[PPP_MPPE_KEY_LENGTH] = { 0 };
    const uint8_t *start_key = mppe_start_key(state);

    mppe_get_new_key_from_sha(start_key, state->session_key,
                              state->key_length, interim);
    mppe_rc4_init(&state->rc4, interim, state->key_length);
    mppe_rc4_crypt(&state->rc4, interim, next_key, state->key_length);
    memcpy(state->session_key, next_key, state->key_length);
    mppe_reduce_key(state);
    mppe_rc4_init(&state->rc4, state->session_key, state->key_length);
    memset(interim, 0, sizeof(interim));
    memset(next_key, 0, sizeof(next_key));
}

bool
ppp_mppe_configure(ppp_mppe_state_t *state, uint8_t key_bits, bool stateful)
{
    const uint8_t *start_key;

    if (!state || (key_bits != 40 && key_bits != 56 && key_bits != 128))
        return false;

    state->key_bits = key_bits;
    state->key_length = key_bits == 128 ? 16 : 8;
    start_key = mppe_start_key(state);
    state->stateful = stateful;
    state->next_count = 0;
    state->last_count = 0x0FFF;
    state->receive_initialized = false;
    state->force_reset = false;
    state->discard = false;
    state->reset_requested = false;
    memset(state->session_key, 0, sizeof(state->session_key));
    mppe_get_new_key_from_sha(start_key, start_key,
                              state->key_length, state->session_key);
    mppe_reduce_key(state);
    mppe_rc4_init(&state->rc4, state->session_key, state->key_length);
    return true;
}

static void
mppe_derive_direction(const uint8_t master_key[PPP_MPPE_KEY_LENGTH],
                      const uint8_t *magic, size_t magic_len,
                      ppp_mppe_state_t *state)
{
    uint8_t digest[SHA1_DIGEST_LENGTH];

    memset(state, 0, sizeof(*state));
    mppe_sha1_segments(master_key, PPP_MPPE_KEY_LENGTH,
                       sha_pad1, sizeof(sha_pad1), magic, magic_len,
                       sha_pad2, sizeof(sha_pad2), digest);
    memcpy(state->start_key, digest, PPP_MPPE_KEY_LENGTH);
    memset(digest, 0, sizeof(digest));
    ppp_mppe_configure(state, 128, false);
}

void
ppp_mppe_derive_mschapv2_keys(const uint8_t password_hash_hash[16],
                              const uint8_t nt_response[24],
                              ppp_mppe_state_t *send_state,
                              ppp_mppe_state_t *receive_state)
{
    uint8_t master_key[SHA1_DIGEST_LENGTH];
    modem_sha1_ctx_t sha1;

    modem_sha1_init(&sha1);
    modem_sha1_update(&sha1, password_hash_hash, 16);
    modem_sha1_update(&sha1, nt_response, 24);
    modem_sha1_update(&sha1, magic_master, sizeof(magic_master) - 1);
    modem_sha1_final(&sha1, master_key);

    mppe_derive_direction(master_key, magic_client_receive,
                          sizeof(magic_client_receive) - 1, send_state);
    mppe_derive_direction(master_key, magic_client_send,
                          sizeof(magic_client_send) - 1, receive_state);
    memset(master_key, 0, sizeof(master_key));
}

void
ppp_mppe_derive_mschapv1_keys(const uint8_t password_hash_hash[16],
                              const uint8_t lm_password_hash[16],
                              const uint8_t challenge[8],
                              ppp_mppe_state_t *send_state,
                              ppp_mppe_state_t *receive_state)
{
    uint8_t initial_session_key[SHA1_DIGEST_LENGTH];

    mppe_sha1_segments(password_hash_hash, 16,
                       password_hash_hash, 16,
                       challenge, 8, NULL, 0, initial_session_key);
    memset(send_state, 0, sizeof(*send_state));
    memset(receive_state, 0, sizeof(*receive_state));
    memcpy(send_state->start_key, initial_session_key, PPP_MPPE_KEY_LENGTH);
    memcpy(receive_state->start_key, initial_session_key, PPP_MPPE_KEY_LENGTH);
    memcpy(send_state->mschapv1_weak_start_key, lm_password_hash, 8);
    memcpy(receive_state->mschapv1_weak_start_key, lm_password_hash, 8);
    send_state->mschapv1_key_derivation = true;
    receive_state->mschapv1_key_derivation = true;
    ppp_mppe_configure(send_state, 128, false);
    ppp_mppe_configure(receive_state, 128, false);
    memset(initial_session_key, 0, sizeof(initial_session_key));
}

bool
ppp_mppe_encrypt(ppp_mppe_state_t *state, const uint8_t *plain, size_t plain_len,
                 uint8_t mppc_flags, uint8_t *out, size_t out_capacity, size_t *out_len)
{
    uint16_t count;
    bool flushed;

    if (!state || !plain || !out || !out_len || plain_len == 0
        || state->key_length == 0
        || out_capacity < plain_len + PPP_MPPE_HEADER_LENGTH)
        return false;

    count = state->next_count;
    flushed = !state->stateful || (count & 0x00FF) == 0x00FF || state->force_reset;
    if (!state->stateful || (count & 0x00FF) == 0x00FF)
        mppe_rekey(state);
    else if (state->force_reset)
        mppe_rc4_init(&state->rc4, state->session_key, state->key_length);
    state->force_reset = false;
    out[0] = (uint8_t) (PPP_MPPE_FLAG_ENCRYPTED
                        | (flushed ? PPP_MPPE_FLAG_FLUSHED : 0)
                        | (mppc_flags & (PPP_MPPE_FLAG_AT_FRONT | PPP_MPPE_FLAG_COMPRESSED))
                        | (count >> 8));
    out[1] = (uint8_t) count;
    mppe_rc4_crypt(&state->rc4, plain, out + PPP_MPPE_HEADER_LENGTH, plain_len);
    state->next_count = (uint16_t) ((count + 1) & 0x0FFF);
    *out_len = plain_len + PPP_MPPE_HEADER_LENGTH;
    return true;
}

bool
ppp_mppe_decrypt(ppp_mppe_state_t *state, const uint8_t *packet, size_t packet_len,
                 uint8_t *out, size_t out_capacity, size_t *out_len)
{
    uint16_t count;
    uint16_t delta;
    uint8_t  flags;
    size_t   encrypted_len;

    if (!state || !packet || !out || !out_len || packet_len <= PPP_MPPE_HEADER_LENGTH)
        return false;
    encrypted_len = packet_len - PPP_MPPE_HEADER_LENGTH;
    flags = packet[0] & PPP_MPPE_FLAGS_MASK;
    /* MPPE does not interpret B/C. When MPPC is also negotiated, those
       bits carry MPPC's AT_FRONT and COMPRESSED flags for the CCP decoder. */
    if (out_capacity < encrypted_len || state->key_length == 0
        || !(flags & PPP_MPPE_FLAG_ENCRYPTED))
        return false;

    count = (uint16_t) (((packet[0] & 0x0F) << 8) | packet[1]);
    if (!state->stateful) {
        if (!(flags & PPP_MPPE_FLAG_FLUSHED))
            return false;
        delta = (uint16_t) ((count - state->last_count) & 0x0FFF);
        if (delta == 0 || delta > 0x0800)
            return false;
        while (delta-- > 0) {
            mppe_rekey(state);
            state->last_count = (uint16_t) ((state->last_count + 1) & 0x0FFF);
        }
    } else if (state->discard) {
        if (!(flags & PPP_MPPE_FLAG_FLUSHED))
            return false;
        delta = (uint16_t) ((count - state->next_count) & 0x0FFF);
        if (delta > 0x0800)
            return false;
        for (uint16_t i = 0; i <= delta; i++) {
            if (((state->next_count + i) & 0x00FF) == 0x00FF)
                mppe_rekey(state);
        }
        mppe_rc4_init(&state->rc4, state->session_key, state->key_length);
        state->discard = false;
        state->reset_requested = false;
    } else {
        if (count != state->next_count
            || ((count & 0x00FF) == 0x00FF && !(flags & PPP_MPPE_FLAG_FLUSHED))) {
            state->discard = true;
            state->reset_requested = true;
            return false;
        }
        if ((count & 0x00FF) == 0x00FF)
            mppe_rekey(state);
        else if (flags & PPP_MPPE_FLAG_FLUSHED)
            mppe_rc4_init(&state->rc4, state->session_key, state->key_length);
    }

    mppe_rc4_crypt(&state->rc4, packet + PPP_MPPE_HEADER_LENGTH, out, encrypted_len);
    state->last_count = count;
    state->receive_initialized = true;
    if (state->stateful)
        state->next_count = (uint16_t) ((count + 1) & 0x0FFF);
    *out_len = encrypted_len;
    return true;
}

void
ppp_mppe_request_reset(ppp_mppe_state_t *state)
{
    if (state && state->key_length > 0)
        state->force_reset = true;
}
