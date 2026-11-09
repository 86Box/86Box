/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PAP (Password Authentication Protocol) implementation for PPP
 *          modem emulation. Server-side processing: receives and validates
 *          Authenticate-Request from the peer.
 *          RFC 1334.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_pap.h>
#include <86box/modem/modem_crypto.h>
#include <86box/log.h>
#include <86box/modem/modem_debug.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
pap_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define pap_log(priv, fmt, ...)
#endif

/* Send a PAP Authenticate-Ack or Authenticate-Nak */
static void
ppp_pap_send_response(ppp_ctx_t *ctx, uint8_t id, bool success)
{
    uint8_t     pkt[64];
    const char *msg     = success ? "Welcome" : "Authentication failed";
    uint8_t     msg_len = (uint8_t) strlen(msg);
    int         len     = 5 + msg_len;

    pkt[0] = success ? PAP_CODE_AUTHENTICATE_ACK : PAP_CODE_AUTHENTICATE_NAK;
    pkt[1] = id;
    pkt[2] = (uint8_t) (len >> 8);
    pkt[3] = (uint8_t) (len & 0xFF);
    pkt[4] = msg_len;
    memcpy(pkt + 5, msg, msg_len);

    ppp_send_frame(ctx, PPP_PROTO_PAP, pkt, len);
    pap_log(ctx->log, "PAP: Sent %s (id=%d)\n", success ? "Ack" : "Nak", id);
}

void
ppp_pap_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    if (pkt_len < 4)
        return;

    uint8_t code  = pkt[0];
    uint8_t id    = pkt[1];
    int     total = (pkt[2] << 8) | pkt[3];
    MODEM_DEBUG_LOG(ctx->log, "PAP: packet code=%d id=%d declared=%d received=%d\n",
                    code, id, total, pkt_len);

    if (code != PAP_CODE_AUTHENTICATE_REQUEST) {
        pap_log(ctx->log, "PAP: Unexpected code %d\n", code);
        return;
    }

    if (total < 6 || total > pkt_len)
        return;

    /* Parse: Peer-ID-Length(1) + Peer-ID + Passwd-Length(1) + Passwd */
    int     pos         = 4;
    uint8_t peer_id_len = pkt[pos++];

    if (pos + peer_id_len + 1 > total)
        return;

    pos += peer_id_len;

    uint8_t passwd_len = pkt[pos++];
    if (pos + passwd_len != total)
        return;

    if (ctx->auth_complete) {
        if (ctx->pap_last_request_valid && total == ctx->pap_last_request_len
            && memcmp(pkt, ctx->pap_last_request, (size_t) total) == 0)
            ppp_pap_send_response(ctx, id, true);
        return;
    }

    MODEM_DEBUG_LOG(ctx->log, "PAP: credential field lengths user=%d password=%d\n",
                    peer_id_len, passwd_len);

    pap_log(ctx->log, "PAP: Authenticate-Request (user length=%u)\n", peer_id_len);

    /* Validate credentials */
    bool ok = false;
    if (ctx->username[0] == '\0' && ctx->password[0] == '\0') {
        /* No credentials configured - accept anyone */
        ok = true;
    } else {
        size_t username_len = strlen(ctx->username);
        size_t password_len = strlen(ctx->password);
        if (peer_id_len == username_len && passwd_len == password_len
            && memcmp(pkt + 5, ctx->username, username_len) == 0
            && modem_constant_time_equal(pkt + pos,
                                         (const uint8_t *) ctx->password,
                                         password_len))
            ok = true;
    }

    MODEM_DEBUG_LOG(ctx->log, "PAP: credential check %s\n", ok ? "accepted" : "rejected");
    ppp_pap_send_response(ctx, id, ok);

    if (ok) {
        ctx->auth_complete = true;
        ctx->pap_last_request_len = (uint16_t) total;
        memcpy(ctx->pap_last_request, pkt, (size_t) total);
        ctx->pap_last_request_valid = true;
        ppp_advance_state(ctx);
    }
}
