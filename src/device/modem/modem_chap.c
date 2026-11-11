/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CHAP (Challenge-Handshake Authentication Protocol) implementation
 *          for PPP modem emulation. Server-side processing for:
 *            - CHAP/MD5 and CHAP/SHA-1, SHA-256, SHA-384, and SHA-512
 *              (RFC 1994)
 *            - MS-CHAP (RFC 2433)
 *            - MS-CHAPv2 (RFC 2759)
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
/*
 * The MS-CHAP response helpers are adapted from pppd 2.5.2's chap_ms.c.
 * Copyright (c) 1995 Eric Rosenquist. All rights reserved.
 * Copyright (c) 2002 Google, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name(s) of the authors of this software must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission.
 *
 * THE AUTHORS OF THIS SOFTWARE DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_chap.h>
#include <86box/network/net_vserver_config.h>
#include <86box/modem/modem_crypto.h>
#include <86box/modem/modem_mppe.h>
#include <86box/log.h>
#include <86box/modem/modem_debug.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
chap_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define chap_log(priv, fmt, ...)
#endif

#ifdef ENABLE_MODEM_LOG
static const char *
chap_auth_name(ppp_auth_type_t auth_type)
{
    switch (auth_type) {
        case PPP_AUTH_CHAP_MD5:  return "CHAP-MD5";
        case PPP_AUTH_MSCHAP:    return "MS-CHAP";
        case PPP_AUTH_MSCHAPV2:  return "MS-CHAPv2";
        case PPP_AUTH_CHAP_SHA1: return "CHAP-SHA1";
        case PPP_AUTH_CHAP_SHA256: return "CHAP-SHA256";
        case PPP_AUTH_CHAP_SHA384: return "CHAP-SHA384";
        case PPP_AUTH_CHAP_SHA512: return "CHAP-SHA512";
        default:                 return "unknown";
    }
}

static const char *
chap_code_name(uint8_t code)
{
    switch (code) {
        case CHAP_CODE_CHALLENGE: return "Challenge";
        case CHAP_CODE_RESPONSE:  return "Response";
        case CHAP_CODE_SUCCESS:   return "Success";
        case CHAP_CODE_FAILURE:   return "Failure";
        default:                  return "Unknown";
    }
}
#endif

/* Send CHAP Challenge packet */
void
ppp_chap_send_challenge(ppp_ctx_t *ctx)
{
    uint8_t pkt[64];
    uint8_t random[17];
    int     len;
    uint8_t challenge_len;

    if (ctx->auth_type == PPP_AUTH_MSCHAPV2)
        challenge_len = 16;
    else if (ctx->auth_type == PPP_AUTH_MSCHAP)
        challenge_len = 8;
    else
        challenge_len = 16; /* CHAP/MD5 */

    if (!ppp_random_bytes(random, sizeof(random))) {
        ctx->state = PPP_STATE_DEAD;
        return;
    }

    memcpy(ctx->chap_challenge, random, challenge_len);
    ctx->chap_challenge_len = challenge_len;

    /* Build packet: Code(1) + ID(1) + Length(2) + Value-Size(1) + Value + Name */
    ctx->auth_id = random[challenge_len];
    pkt[0] = CHAP_CODE_CHALLENGE;
    pkt[1] = ctx->auth_id;
    /* Length at [2..3] filled later */
    pkt[4] = challenge_len;
    memcpy(pkt + 5, ctx->chap_challenge, challenge_len);

    /* Append server name */
    const char *name     = VSERVER_PPP_SERVER_NAME;
    int         name_len = (int) strlen(name);
    memcpy(pkt + 5 + challenge_len, name, name_len);

    len    = 5 + challenge_len + name_len;
    pkt[2] = (uint8_t) (len >> 8);
    pkt[3] = (uint8_t) (len & 0xFF);

    ppp_send_frame(ctx, PPP_PROTO_CHAP, pkt, len);
    chap_log(ctx->log, "CHAP: Sent Challenge (id=%u, algorithm=%s)\n",
             (unsigned) ctx->auth_id, chap_auth_name(ctx->auth_type));
    MODEM_DEBUG_LOG(ctx->log, "CHAP: challenge length=%u algorithm=%s\n",
                    (unsigned) challenge_len, chap_auth_name(ctx->auth_type));
}

/* Verify CHAP/MD5 response (RFC 1994) */
static bool
chap_verify_md5(ppp_ctx_t *ctx, uint8_t id, const uint8_t *response, int resp_len)
{
    uint8_t expected[MD5_DIGEST_LENGTH];
    modem_md5_ctx_t md5;

    if (resp_len != MD5_DIGEST_LENGTH)
        return false;

    /* Expected = MD5(ID + secret + challenge) */
    modem_md5_init(&md5);
    modem_md5_update(&md5, &id, 1);
    modem_md5_update(&md5, (const uint8_t *) ctx->password, strlen(ctx->password));
    modem_md5_update(&md5, ctx->chap_challenge, ctx->chap_challenge_len);
    modem_md5_final(&md5, expected);

    return modem_constant_time_equal(response, expected, MD5_DIGEST_LENGTH);
}

/* Verify CHAP/SHA-1 response using Hash(ID + secret + challenge). */
static bool
chap_verify_sha1(ppp_ctx_t *ctx, uint8_t id, const uint8_t *response, int resp_len)
{
    uint8_t expected[SHA1_DIGEST_LENGTH];
    modem_sha1_ctx_t sha1;

    if (resp_len != SHA1_DIGEST_LENGTH)
        return false;

    modem_sha1_init(&sha1);
    modem_sha1_update(&sha1, &id, 1);
    modem_sha1_update(&sha1, (const uint8_t *) ctx->password, strlen(ctx->password));
    modem_sha1_update(&sha1, ctx->chap_challenge, ctx->chap_challenge_len);
    modem_sha1_final(&sha1, expected);

    return modem_constant_time_equal(response, expected, SHA1_DIGEST_LENGTH);
}

/* Verify CHAP/SHA-256 response using Hash(ID + secret + challenge). */
static bool
chap_verify_sha256(ppp_ctx_t *ctx, uint8_t id, const uint8_t *response, int resp_len)
{
    uint8_t expected[SHA256_DIGEST_LENGTH];
    modem_sha256_ctx_t sha256;

    if (resp_len != SHA256_DIGEST_LENGTH)
        return false;

    modem_sha256_init(&sha256);
    modem_sha256_update(&sha256, &id, 1);
    modem_sha256_update(&sha256, (const uint8_t *) ctx->password, strlen(ctx->password));
    modem_sha256_update(&sha256, ctx->chap_challenge, ctx->chap_challenge_len);
    modem_sha256_final(&sha256, expected);

    return modem_constant_time_equal(response, expected, SHA256_DIGEST_LENGTH);
}

/* Verify CHAP/SHA-384 response using Hash(ID + secret + challenge). */
static bool
chap_verify_sha384(ppp_ctx_t *ctx, uint8_t id, const uint8_t *response, int resp_len)
{
    uint8_t expected[SHA384_DIGEST_LENGTH];
    modem_sha384_ctx_t sha384;

    if (resp_len != SHA384_DIGEST_LENGTH)
        return false;

    modem_sha384_init(&sha384);
    modem_sha384_update(&sha384, &id, 1);
    modem_sha384_update(&sha384, (const uint8_t *) ctx->password, strlen(ctx->password));
    modem_sha384_update(&sha384, ctx->chap_challenge, ctx->chap_challenge_len);
    modem_sha384_final(&sha384, expected);

    return modem_constant_time_equal(response, expected, SHA384_DIGEST_LENGTH);
}

/* Verify CHAP/SHA-512 response using Hash(ID + secret + challenge). */
static bool
chap_verify_sha512(ppp_ctx_t *ctx, uint8_t id, const uint8_t *response, int resp_len)
{
    uint8_t expected[SHA512_DIGEST_LENGTH];
    modem_sha512_ctx_t sha512;

    if (resp_len != SHA512_DIGEST_LENGTH)
        return false;

    modem_sha512_init(&sha512);
    modem_sha512_update(&sha512, &id, 1);
    modem_sha512_update(&sha512, (const uint8_t *) ctx->password, strlen(ctx->password));
    modem_sha512_update(&sha512, ctx->chap_challenge, ctx->chap_challenge_len);
    modem_sha512_final(&sha512, expected);

    return modem_constant_time_equal(response, expected, SHA512_DIGEST_LENGTH);
}

/* Compute NT Password Hash: MD4(UTF-16LE(password)) */
static void
nt_password_hash(const char *password, uint8_t hash[MD4_DIGEST_LENGTH])
{
    /* Convert ASCII password to UTF-16LE */
    size_t  pw_len = strlen(password);
    size_t  ulen   = pw_len * 2;
    uint8_t ubuf[128] = { 0 };

    if (ulen > sizeof(ubuf)) {
        memset(hash, 0, MD4_DIGEST_LENGTH);
        return;
    }

    for (size_t i = 0; i < pw_len; i++) {
        ubuf[i * 2]     = (uint8_t) password[i];
        ubuf[i * 2 + 1] = 0;
    }

    modem_md4(ubuf, ulen, hash);
    memset(ubuf, 0, ulen);
}

/* Compute hash of NT Password Hash */
static void
hash_nt_password_hash(const uint8_t pw_hash[MD4_DIGEST_LENGTH], uint8_t hash_hash[MD4_DIGEST_LENGTH])
{
    modem_md4(pw_hash, MD4_DIGEST_LENGTH, hash_hash);
}

/* DES-encrypt using a 7-byte key segment from a 21-byte padded hash */
static void
challenge_response_des(const uint8_t challenge[8], const uint8_t hash21[21], uint8_t response[24])
{
    modem_des_encrypt_block(hash21 + 0,  challenge, response + 0);
    modem_des_encrypt_block(hash21 + 7,  challenge, response + 8);
    modem_des_encrypt_block(hash21 + 14, challenge, response + 16);
}

static bool
lm_password_hash(const char *password, uint8_t hash[16])
{
    static const uint8_t magic[8] = { 'K', 'G', 'S', '!', '@', '#', '$', '%' };
    uint8_t upper_password[14] = { 0 };
    size_t password_len = strlen(password);

    if (password_len > sizeof(upper_password))
        password_len = sizeof(upper_password);
    for (size_t index = 0; index < password_len; index++) {
        uint8_t value = (uint8_t) password[index];
        if (value >= 0x80) {
            memset(upper_password, 0, sizeof(upper_password));
            return false;
        }
        if (value >= 'a' && value <= 'z')
            value = (uint8_t) (value - 'a' + 'A');
        upper_password[index] = value;
    }

    modem_des_encrypt_block(upper_password, magic, hash);
    modem_des_encrypt_block(upper_password + 7, magic, hash + 8);
    memset(upper_password, 0, sizeof(upper_password));
    return true;
}

/* Verify an MS-CHAP v1 LM-only response (RFC 2433). LM proofs derive no MPPE keys. */
static bool
chap_verify_mschap_lm(ppp_ctx_t *ctx, const uint8_t *response)
{
    uint8_t lm_hash[16];
    uint8_t lm_hash_padded[21];
    uint8_t expected[24];
    bool    ok;

    if (!lm_password_hash(ctx->password, lm_hash)) {
        chap_log(ctx->log, "MS-CHAP: LM response cannot be computed for this password\n");
        return false;
    }
    memset(lm_hash_padded, 0, sizeof(lm_hash_padded));
    memcpy(lm_hash_padded, lm_hash, 16);
    challenge_response_des(ctx->chap_challenge, lm_hash_padded, expected);
    ok = modem_constant_time_equal(response, expected, 24);
    memset(lm_hash, 0, sizeof(lm_hash));
    memset(lm_hash_padded, 0, sizeof(lm_hash_padded));
    chap_log(ctx->log, "MS-CHAP: LM response %s\n", ok ? "accepted" : "rejected");
    return ok;
}

/* Verify MS-CHAP v1 response (RFC 2433) */
static bool
chap_verify_mschap(ppp_ctx_t *ctx, const uint8_t *response, int resp_len)
{
    uint8_t nt_hash[16];
    uint8_t nt_hash_padded[21];
    uint8_t expected[24];

    /* MS-CHAP response is 49 bytes: 24 LM + 24 NT + 1 flags */
    if (resp_len != 49)
        return false;

    /* A zero flags byte selects the LM response (bytes 0-23); otherwise the NT response is used. */
    if (!response[48])
        return chap_verify_mschap_lm(ctx, response);

    nt_password_hash(ctx->password, nt_hash);
    memset(nt_hash_padded, 0, sizeof(nt_hash_padded));
    memcpy(nt_hash_padded, nt_hash, 16);

    challenge_response_des(ctx->chap_challenge, nt_hash_padded, expected);

    if (!modem_constant_time_equal(response + 24, expected, 24))
        return false;

    uint8_t password_hash_hash[MD4_DIGEST_LENGTH];
    uint8_t lm_hash[16];
    hash_nt_password_hash(nt_hash, password_hash_hash);
    if (lm_password_hash(ctx->password, lm_hash)) {
        ppp_mppe_derive_mschapv1_keys(password_hash_hash, lm_hash,
                                      ctx->chap_challenge,
                                      &ctx->mppe_tx, &ctx->mppe_rx);
        ctx->mppe_keys_ready = true;
    }
    memset(nt_hash, 0, sizeof(nt_hash));
    memset(password_hash_hash, 0, sizeof(password_hash_hash));
    memset(lm_hash, 0, sizeof(lm_hash));
    return true;
}

/* MS-CHAPv2 ChallengeHash: SHA-1(PeerChallenge + AuthChallenge + UserName) truncated to 8 bytes */
static void
mschapv2_challenge_hash(const uint8_t peer_challenge[16],
                        const uint8_t auth_challenge[16],
                        const char *username,
                        uint8_t challenge[8])
{
    modem_sha1_ctx_t sha1;
    uint8_t          digest[SHA1_DIGEST_LENGTH];
    const char      *user = strrchr(username, '\\');

    if (user)
        username = user + 1;

    modem_sha1_init(&sha1);
    modem_sha1_update(&sha1, peer_challenge, 16);
    modem_sha1_update(&sha1, auth_challenge, 16);
    modem_sha1_update(&sha1, (const uint8_t *) username, strlen(username));
    modem_sha1_final(&sha1, digest);

    memcpy(challenge, digest, 8);
}

/* MS-CHAPv2 GenerateAuthenticatorResponse (RFC 2759 section 8.7) */
static void
mschapv2_generate_auth_response(const char *password,
                                const uint8_t nt_response[24],
                                const uint8_t peer_challenge[16],
                                const uint8_t auth_challenge[16],
                                const char *username,
                                uint8_t auth_resp[20])
{
    static const uint8_t magic1[39] = {
        0x4D, 0x61, 0x67, 0x69, 0x63, 0x20, 0x73, 0x65, 0x72, 0x76,
        0x65, 0x72, 0x20, 0x74, 0x6F, 0x20, 0x63, 0x6C, 0x69, 0x65,
        0x6E, 0x74, 0x20, 0x73, 0x69, 0x67, 0x6E, 0x69, 0x6E, 0x67,
        0x20, 0x63, 0x6F, 0x6E, 0x73, 0x74, 0x61, 0x6E, 0x74, /* "Magic server to client signing constant" */
    };
    static const uint8_t magic2[41] = {
        0x50, 0x61, 0x64, 0x20, 0x74, 0x6F, 0x20, 0x6D, 0x61, 0x6B,
        0x65, 0x20, 0x69, 0x74, 0x20, 0x64, 0x6F, 0x20, 0x6D, 0x6F,
        0x72, 0x65, 0x20, 0x74, 0x68, 0x61, 0x6E, 0x20, 0x6F, 0x6E,
        0x65, 0x20, 0x69, 0x74, 0x65, 0x72, 0x61, 0x74, 0x69, 0x6F,
        0x6E, /* "Pad to make it do more than one iteration" */
    };

    uint8_t          pw_hash[MD4_DIGEST_LENGTH];
    uint8_t          pw_hash_hash[MD4_DIGEST_LENGTH];
    uint8_t          challenge[8];
    uint8_t          digest[SHA1_DIGEST_LENGTH];
    modem_sha1_ctx_t sha1;

    nt_password_hash(password, pw_hash);
    hash_nt_password_hash(pw_hash, pw_hash_hash);

    modem_sha1_init(&sha1);
    modem_sha1_update(&sha1, pw_hash_hash, MD4_DIGEST_LENGTH);
    modem_sha1_update(&sha1, nt_response, 24);
    modem_sha1_update(&sha1, magic1, sizeof(magic1));
    modem_sha1_final(&sha1, digest);

    mschapv2_challenge_hash(peer_challenge, auth_challenge, username, challenge);

    modem_sha1_init(&sha1);
    modem_sha1_update(&sha1, digest, SHA1_DIGEST_LENGTH);
    modem_sha1_update(&sha1, challenge, 8);
    modem_sha1_update(&sha1, magic2, sizeof(magic2));
    modem_sha1_final(&sha1, auth_resp);

    memset(pw_hash, 0, sizeof(pw_hash));
    memset(pw_hash_hash, 0, sizeof(pw_hash_hash));
}

/* Verify MS-CHAPv2 response (RFC 2759) */
static bool
chap_verify_mschapv2(ppp_ctx_t *ctx, const uint8_t *response, int resp_len,
                     const char *peer_name, uint8_t auth_resp[20])
{
    uint8_t nt_hash[16];
    uint8_t nt_hash_padded[21];
    uint8_t challenge[8];
    uint8_t expected[24];

    /* MS-CHAPv2 response is 49 bytes: 16 PeerChallenge + 8 Reserved + 24 NT-Response + 1 Flags */
    if (resp_len != 49)
        return false;

    const uint8_t *peer_challenge = response;
    const uint8_t *nt_response    = response + 24;

    /* Compute ChallengeHash */
    mschapv2_challenge_hash(peer_challenge, ctx->chap_challenge, peer_name, challenge);

    /* Compute expected NT-Response */
    nt_password_hash(ctx->password, nt_hash);
    memset(nt_hash_padded, 0, sizeof(nt_hash_padded));
    memcpy(nt_hash_padded, nt_hash, 16);

    challenge_response_des(challenge, nt_hash_padded, expected);

    if (!modem_constant_time_equal(nt_response, expected, 24)) {
        memset(nt_hash, 0, sizeof(nt_hash));
        return false;
    }

    /* Generate authenticator response for Success message */
    mschapv2_generate_auth_response(ctx->password, nt_response,
                                    peer_challenge, ctx->chap_challenge,
                                    peer_name, auth_resp);

    uint8_t nt_hash_hash[MD4_DIGEST_LENGTH];
    hash_nt_password_hash(nt_hash, nt_hash_hash);
    ppp_mppe_derive_mschapv2_keys(nt_hash_hash, nt_response,
                                  &ctx->mppe_tx, &ctx->mppe_rx);
    ctx->mppe_keys_ready = true;
    memset(nt_hash_hash, 0, sizeof(nt_hash_hash));

    memset(nt_hash, 0, sizeof(nt_hash));
    return true;
}

/* Send CHAP Success/Failure */
static void
chap_send_result(ppp_ctx_t *ctx, uint8_t id, bool success, const char *message)
{
    uint8_t pkt[128];
    int     msg_len = (int) strlen(message);
    int     len     = 4 + msg_len;

    pkt[0] = success ? CHAP_CODE_SUCCESS : CHAP_CODE_FAILURE;
    pkt[1] = id;
    pkt[2] = (uint8_t) (len >> 8);
    pkt[3] = (uint8_t) (len & 0xFF);
    memcpy(pkt + 4, message, msg_len);

    ppp_send_frame(ctx, PPP_PROTO_CHAP, pkt, len);
    chap_log(ctx->log, "CHAP: Sent %s (id=%d)\n", success ? "Success" : "Failure", id);
}

/* Process CHAP Response from peer */
void
ppp_chap_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    if (pkt_len < 4)
        return;

    uint8_t code  = pkt[0];
    uint8_t id    = pkt[1];
    int     total = (pkt[2] << 8) | pkt[3];
    MODEM_DEBUG_LOG(ctx->log, "CHAP: packet code=%s (%u) id=%u declared=%d received=%d algorithm=%s\n",
                    chap_code_name(code), (unsigned) code, (unsigned) id, total, pkt_len,
                    chap_auth_name(ctx->auth_type));

    if (code != CHAP_CODE_RESPONSE) {
        chap_log(ctx->log, "CHAP: Unexpected code %s (%u)\n",
                 chap_code_name(code), (unsigned) code);
        return;
    }

    if (total < 5 || total > pkt_len || id != ctx->auth_id)
        return;

    int     pos       = 4;
    uint8_t value_len = pkt[pos++];

    if (pos + value_len > total)
        return;

    const uint8_t *value = pkt + pos;
    pos += value_len;

    /* Extract peer name */
    int  name_len = total - pos;
    char peer_name[64];
    if (name_len >= (int) sizeof(peer_name)) {
        chap_send_result(ctx, id, false, "E=691 R=0");
        return;
    }
    int  copy = (name_len < (int) sizeof(peer_name) - 1) ? name_len : (int) sizeof(peer_name) - 1;
    if (copy > 0)
        memcpy(peer_name, pkt + pos, copy);
    peer_name[copy > 0 ? copy : 0] = '\0';

    chap_log(ctx->log, "CHAP: Response from '%s' (value_len=%d)\n", peer_name, value_len);
    MODEM_DEBUG_LOG(ctx->log, "CHAP: response value length=%d peer-name length=%d\n",
                    value_len, name_len);

    bool ok = false;

    /* Check username first (if configured) */
    if (ctx->username[0] != '\0'
        && ((size_t) name_len != strlen(ctx->username)
            || memcmp(pkt + pos, ctx->username, (size_t) name_len) != 0)) {
        chap_log(ctx->log, "CHAP: Username mismatch\n");
        chap_send_result(ctx, id, false, "E=691 R=0");
        return;
    }

    switch (ctx->auth_type) {
        case PPP_AUTH_CHAP_MD5:
            ok = chap_verify_md5(ctx, id, value, value_len);
            if (ok)
                chap_send_result(ctx, id, true, "");
            else
                chap_send_result(ctx, id, false, "");
            break;

        case PPP_AUTH_CHAP_SHA1:
            ok = chap_verify_sha1(ctx, id, value, value_len);
            if (ok)
                chap_send_result(ctx, id, true, "");
            else
                chap_send_result(ctx, id, false, "");
            break;

        case PPP_AUTH_CHAP_SHA256:
            ok = chap_verify_sha256(ctx, id, value, value_len);
            if (ok)
                chap_send_result(ctx, id, true, "");
            else
                chap_send_result(ctx, id, false, "");
            break;

        case PPP_AUTH_CHAP_SHA384:
            ok = chap_verify_sha384(ctx, id, value, value_len);
            if (ok)
                chap_send_result(ctx, id, true, "");
            else
                chap_send_result(ctx, id, false, "");
            break;

        case PPP_AUTH_CHAP_SHA512:
            ok = chap_verify_sha512(ctx, id, value, value_len);
            if (ok)
                chap_send_result(ctx, id, true, "");
            else
                chap_send_result(ctx, id, false, "");
            break;

        case PPP_AUTH_MSCHAP:
            ok = chap_verify_mschap(ctx, value, value_len);
            if (ok)
                chap_send_result(ctx, id, true, "");
            else
                chap_send_result(ctx, id, false, "E=691 R=0 V=2");
            break;

        case PPP_AUTH_MSCHAPV2:
            {
                uint8_t auth_resp[20];
                char    success_msg[64];

                ok = chap_verify_mschapv2(ctx, value, value_len, peer_name, auth_resp);
                if (ok) {
                    /* Build S= success message with hex-encoded authenticator response */
                    success_msg[0] = 'S';
                    success_msg[1] = '=';
                    for (int i = 0; i < 20; i++)
                        snprintf(success_msg + 2 + i * 2, 3, "%02X", auth_resp[i]);
                    success_msg[42] = '\0';
                    chap_send_result(ctx, id, true, success_msg);
                } else {
                    chap_send_result(ctx, id, false, "E=691 R=0 C=00000000000000000000000000000000 V=3");
                }
            }
            break;

        default:
            chap_send_result(ctx, id, false, "");
            break;
    }

    if (ok) {
        ctx->auth_complete = true;
        ppp_advance_state(ctx);
    }
    MODEM_DEBUG_LOG(ctx->log, "CHAP: credential check %s\n", ok ? "accepted" : "rejected");
}
