/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PPP Extensible Authentication Protocol (EAP) authenticator.
 *          Implements Identity and MD5-Challenge per RFC 3748
 *          (which obsoletes RFC 2284).
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <86box/log.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_eap.h>
#include <86box/modem/modem_crypto.h>
#include <86box/modem/modem_debug.h>

#define EAP_MD5_CHALLENGE_LENGTH 16

static void
eap_send_request(ppp_ctx_t *ctx, uint8_t type, const uint8_t *type_data, uint8_t type_data_len,
                 ppp_eap_state_t state)
{
    uint8_t pkt[64];
    int     len = 5 + type_data_len;

    pkt[0] = EAP_CODE_REQUEST;
    pkt[1] = ctx->eap_id++;
    pkt[2] = (uint8_t) (len >> 8);
    pkt[3] = (uint8_t) len;
    pkt[4] = type;
    if (type_data_len > 0)
        memcpy(pkt + 5, type_data, type_data_len);

    ctx->eap_request_id = pkt[1];
    ctx->eap_state = state;
    ppp_send_frame(ctx, PPP_PROTO_EAP, pkt, len);
    MODEM_DEBUG_LOG(ctx->log, "EAP: Sent Request (id=%u, type=%u, length=%d)\n",
                    (unsigned) pkt[1], (unsigned) type, len);
}

static void
eap_send_result(ppp_ctx_t *ctx, uint8_t id, bool success)
{
    uint8_t pkt[4];

    pkt[0] = success ? EAP_CODE_SUCCESS : EAP_CODE_FAILURE;
    pkt[1] = id;
    pkt[2] = 0;
    pkt[3] = sizeof(pkt);
    ppp_send_frame(ctx, PPP_PROTO_EAP, pkt, sizeof(pkt));
    MODEM_DEBUG_LOG(ctx->log, "EAP: Sent %s (id=%u)\n",
                    success ? "Success" : "Failure", (unsigned) id);

    if (success) {
        ctx->eap_state = PPP_EAP_STATE_IDLE;
        ctx->auth_complete = true;
        ppp_advance_state(ctx);
    } else {
        ctx->eap_state = PPP_EAP_STATE_IDLE;
    }
}

static void
eap_send_md5_challenge(ppp_ctx_t *ctx, uint8_t id)
{
    uint8_t type_data[EAP_MD5_CHALLENGE_LENGTH + 1];

    type_data[0] = EAP_MD5_CHALLENGE_LENGTH;
    if (!ppp_random_bytes(ctx->eap_challenge, EAP_MD5_CHALLENGE_LENGTH)) {
        eap_send_result(ctx, id, false);
        return;
    }
    memcpy(type_data + 1, ctx->eap_challenge, EAP_MD5_CHALLENGE_LENGTH);
    eap_send_request(ctx, EAP_TYPE_MD5_CHALLENGE, type_data, sizeof(type_data),
                     PPP_EAP_STATE_MD5_CHALLENGE);
}

void
ppp_eap_start(ppp_ctx_t *ctx)
{
    ctx->eap_id = 0;
    eap_send_request(ctx, EAP_TYPE_IDENTITY, NULL, 0, PPP_EAP_STATE_IDENTITY);
}

void
ppp_eap_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    uint8_t id;
    int     total;

    if (!ctx)
        return;

    if (!pkt || pkt_len < 4) {
        MODEM_DEBUG_LOG(ctx->log, "EAP: dropped short packet length=%d\n", pkt_len);
        return;
    }

    total = (pkt[2] << 8) | pkt[3];
    if (total < 4 || total > pkt_len || pkt[0] != EAP_CODE_RESPONSE) {
        MODEM_DEBUG_LOG(ctx->log, "EAP: dropped malformed packet code=%u declared=%d received=%d\n",
                        (unsigned) pkt[0], total, pkt_len);
        return;
    }

    id = pkt[1];
    if (id != ctx->eap_request_id || total < 5) {
        MODEM_DEBUG_LOG(ctx->log, "EAP: dropped response id=%u expected=%u length=%d\n",
                        (unsigned) id, (unsigned) ctx->eap_request_id, total);
        return;
    }

    MODEM_DEBUG_LOG(ctx->log, "EAP: Received Response (id=%u, type=%u, length=%d, state=%d)\n",
                    (unsigned) id, (unsigned) pkt[4], total, ctx->eap_state);

    if (ctx->eap_state == PPP_EAP_STATE_IDENTITY) {
        size_t identity_len = (size_t) (total - 5);

        if (pkt[4] != EAP_TYPE_IDENTITY) {
            MODEM_DEBUG_LOG(ctx->log, "EAP: expected Identity response, got type=%u\n",
                            (unsigned) pkt[4]);
            return;
        }

        MODEM_DEBUG_LOG(ctx->log, "EAP: Identity response length=%u\n", (unsigned) identity_len);

        if (ctx->username[0] != '\0'
            && (identity_len != strlen(ctx->username)
                || memcmp(pkt + 5, ctx->username, identity_len) != 0)) {
            eap_send_result(ctx, id, false);
            return;
        }

        eap_send_md5_challenge(ctx, id);
        return;
    }

    if (ctx->eap_state != PPP_EAP_STATE_MD5_CHALLENGE)
        return;

    if (pkt[4] == EAP_TYPE_NAK) {
        MODEM_DEBUG_LOG(ctx->log, "EAP: Nak suggested type=%u\n",
                        total > 5 ? (unsigned) pkt[5] : 0U);
        if (total == 6 && pkt[5] == EAP_TYPE_MD5_CHALLENGE)
            eap_send_md5_challenge(ctx, id);
        else
            eap_send_result(ctx, id, false);
        return;
    }

    if (pkt[4] != EAP_TYPE_MD5_CHALLENGE || total < 6
        || pkt[5] != EAP_MD5_CHALLENGE_LENGTH
        || total != 6 + EAP_MD5_CHALLENGE_LENGTH) {
        eap_send_result(ctx, id, false);
        return;
    }

    {
        modem_md5_ctx_t md5;
        uint8_t         expected[MD5_DIGEST_LENGTH];
        bool            valid;

        modem_md5_init(&md5);
        modem_md5_update(&md5, &id, 1);
        modem_md5_update(&md5, (const uint8_t *) ctx->password, strlen(ctx->password));
        modem_md5_update(&md5, ctx->eap_challenge, EAP_MD5_CHALLENGE_LENGTH);
        modem_md5_final(&md5, expected);

        valid = modem_constant_time_equal(pkt + 6, expected, MD5_DIGEST_LENGTH);
        MODEM_DEBUG_LOG(ctx->log, "EAP: MD5-Challenge response %s\n",
                valid ? "verified" : "rejected");
        eap_send_result(ctx, id, valid);
    }
}
