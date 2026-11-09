/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IPv6CP (IPv6 Control Protocol, RFC 5072) for PPP modem emulation.
 *          Negotiates the 64-bit interface identifiers used to form link-local
 *          addresses. Failure of IPv6CP never takes the link (or IPv4) down.
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
#include <86box/modem/modem_ipv6cp.h>
#include <86box/log.h>
#include <86box/modem/modem_debug.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
ipv6cp_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ipv6cp_log(priv, fmt, ...)
#endif

static bool
ipv6cp_iid_zero(const uint8_t *iid)
{
    for (int i = 0; i < IPV6CP_IID_LENGTH; i++)
        if (iid[i])
            return false;
    return true;
}

/* A non-zero identifier that differs from `avoid` (which may be NULL). */
static void
ipv6cp_pick_iid(uint8_t *iid, const uint8_t *avoid)
{
    for (int tries = 0; tries < 16; tries++) {
        if (!ppp_random_bytes(iid, IPV6CP_IID_LENGTH))
            memset(iid, 0, IPV6CP_IID_LENGTH);
        iid[0] &= (uint8_t) ~0x02; /* locally generated: universal/local bit clear */
        if (!ipv6cp_iid_zero(iid) && (!avoid || memcmp(iid, avoid, IPV6CP_IID_LENGTH)))
            return;
    }
    /* Random source unusable: fall back to something fixed but valid. */
    memset(iid, 0, IPV6CP_IID_LENGTH);
    iid[7] = avoid && avoid[7] == 1 ? 2 : 1;
}

static void
ipv6cp_check_open(ppp_ctx_t *ctx)
{
    if (!ctx->ipv6cp_open && ctx->ipv6cp_ack_sent && ctx->ipv6cp_ack_received) {
        ctx->ipv6cp_open = true;
        ipv6cp_log(ctx->log, "IPv6CP: opened (our IID %02x%02x:%02x%02x:%02x%02x:%02x%02x)\n",
                   ctx->ipv6cp_our_iid[0], ctx->ipv6cp_our_iid[1], ctx->ipv6cp_our_iid[2],
                   ctx->ipv6cp_our_iid[3], ctx->ipv6cp_our_iid[4], ctx->ipv6cp_our_iid[5],
                   ctx->ipv6cp_our_iid[6], ctx->ipv6cp_our_iid[7]);
    }
}

static void
ipv6cp_send_request(ppp_ctx_t *ctx)
{
    uint8_t pkt[16];
    int     len = 4;

    ctx->ipv6cp_ack_received = false;
    ctx->ipv6cp_timeout_ms   = 0;
    ctx->ipv6cp_request_id   = ctx->ipv6cp_id++;
    pkt[0] = PPP_CODE_CONFIGURE_REQUEST;
    pkt[1] = ctx->ipv6cp_request_id;
    if (ctx->ipv6cp_send_iid) {
        pkt[len++] = IPV6CP_OPT_INTERFACE_ID;
        pkt[len++] = 2 + IPV6CP_IID_LENGTH;
        memcpy(pkt + len, ctx->ipv6cp_our_iid, IPV6CP_IID_LENGTH);
        len += IPV6CP_IID_LENGTH;
    }
    pkt[2] = (uint8_t) (len >> 8);
    pkt[3] = (uint8_t) len;
    ppp_send_frame(ctx, PPP_PROTO_IPV6CP, pkt, len);
    ctx->ipv6cp_req_sent = true;
    ipv6cp_log(ctx->log, "IPv6CP: Sent Configure-Request\n");
}

static void
ipv6cp_begin(ppp_ctx_t *ctx)
{
    ctx->ipv6cp_active       = true;
    ctx->ipv6cp_started      = true;
    ctx->ipv6cp_open         = false;
    ctx->ipv6cp_ack_sent     = false;
    ctx->ipv6cp_ack_received = false;
    ctx->ipv6cp_send_iid     = true;
    ctx->ipv6cp_retries      = 0;
    memset(ctx->ipv6cp_peer_iid, 0, IPV6CP_IID_LENGTH);
    ipv6cp_pick_iid(ctx->ipv6cp_our_iid, NULL);
    ipv6cp_send_request(ctx);
}

void
ppp_ipv6cp_start(ppp_ctx_t *ctx)
{
    /* Automatic mode leaves the first move to the peer, so a guest with no IPv6 stack
       never sees IPv6CP at all. */
    if (!ctx || ctx->ppp_ipv6_mode != PPP_IPV6_ENABLED)
        return;

    ipv6cp_begin(ctx);
}

static bool
ipv6cp_options_valid(const uint8_t *pkt, int pkt_len)
{
    int pos = 4;

    while (pos < pkt_len) {
        if (pos + 2 > pkt_len || pkt[pos + 1] < 2 || pos + pkt[pos + 1] > pkt_len)
            return false;
        pos += pkt[pos + 1];
    }
    return true;
}

static void
ipv6cp_handle_request(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    uint8_t ack[64], nak[64], rej[64];
    int     ack_len = 4, nak_len = 4, rej_len = 4;
    uint8_t peer_iid[IPV6CP_IID_LENGTH] = { 0 };
    bool    have_iid = false;
    int     pos = 4;

    ipv6cp_log(ctx->log, "IPv6CP: Received Configure-Request (id=%d)\n", pkt[1]);

    while (pos < pkt_len) {
        const uint8_t type = pkt[pos];
        const uint8_t len  = pkt[pos + 1];

        if (type == IPV6CP_OPT_INTERFACE_ID && len == 2 + IPV6CP_IID_LENGTH) {
            memcpy(peer_iid, pkt + pos + 2, IPV6CP_IID_LENGTH);
            have_iid = true;
            if (ipv6cp_iid_zero(peer_iid)
                || (ctx->ipv6cp_send_iid && !memcmp(peer_iid, ctx->ipv6cp_our_iid, IPV6CP_IID_LENGTH))) {
                /* Zero means "please assign me one"; equal to ours is a clash. */
                uint8_t suggestion[IPV6CP_IID_LENGTH];

                ipv6cp_pick_iid(suggestion, ctx->ipv6cp_our_iid);
                nak[nak_len++] = IPV6CP_OPT_INTERFACE_ID;
                nak[nak_len++] = 2 + IPV6CP_IID_LENGTH;
                memcpy(nak + nak_len, suggestion, IPV6CP_IID_LENGTH);
                nak_len += IPV6CP_IID_LENGTH;
            } else {
                memcpy(ack + ack_len, pkt + pos, len);
                ack_len += len;
            }
        } else if (rej_len + len <= (int) sizeof(rej)) {
            /* Wrong-length Interface-Identifier, IPv6-Compression-Protocol, unknown: reject. */
            memcpy(rej + rej_len, pkt + pos, len);
            rej_len += len;
        }
        pos += len;
    }

    if (ctx->ipv6cp_open) {
        /* Peer restarted negotiation: leave the open state and renegotiate. */
        ctx->ipv6cp_open = false;
        ipv6cp_send_request(ctx);
    }

    if (rej_len > 4) {
        rej[0] = PPP_CODE_CONFIGURE_REJECT;
        rej[1] = pkt[1];
        rej[2] = (uint8_t) (rej_len >> 8);
        rej[3] = (uint8_t) rej_len;
        ppp_send_frame(ctx, PPP_PROTO_IPV6CP, rej, rej_len);
        ctx->ipv6cp_ack_sent = false;
    } else if (nak_len > 4) {
        nak[0] = PPP_CODE_CONFIGURE_NAK;
        nak[1] = pkt[1];
        nak[2] = (uint8_t) (nak_len >> 8);
        nak[3] = (uint8_t) nak_len;
        ppp_send_frame(ctx, PPP_PROTO_IPV6CP, nak, nak_len);
        ctx->ipv6cp_ack_sent = false;
    } else {
        ack[0] = PPP_CODE_CONFIGURE_ACK;
        ack[1] = pkt[1];
        ack[2] = (uint8_t) (ack_len >> 8);
        ack[3] = (uint8_t) ack_len;
        ppp_send_frame(ctx, PPP_PROTO_IPV6CP, ack, ack_len);
        ctx->ipv6cp_ack_sent = true;
        if (have_iid)
            memcpy(ctx->ipv6cp_peer_iid, peer_iid, IPV6CP_IID_LENGTH);
        ipv6cp_check_open(ctx);
    }
}

static void
ipv6cp_handle_ack(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    const int expect = ctx->ipv6cp_send_iid ? 4 + 2 + IPV6CP_IID_LENGTH : 4;

    if (!ctx->ipv6cp_req_sent || pkt[1] != ctx->ipv6cp_request_id || pkt_len != expect)
        return;
    if (ctx->ipv6cp_send_iid
        && (pkt[4] != IPV6CP_OPT_INTERFACE_ID || pkt[5] != 2 + IPV6CP_IID_LENGTH
            || memcmp(pkt + 6, ctx->ipv6cp_our_iid, IPV6CP_IID_LENGTH)))
        return;

    ctx->ipv6cp_req_sent     = false;
    ctx->ipv6cp_ack_received = true;
    ipv6cp_check_open(ctx);
}

static void
ipv6cp_handle_nak(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    int pos = 4;

    if (!ctx->ipv6cp_req_sent || pkt[1] != ctx->ipv6cp_request_id)
        return;
    ctx->ipv6cp_req_sent = false;

    while (pos < pkt_len) {
        if (pkt[pos] == IPV6CP_OPT_INTERFACE_ID && pkt[pos + 1] == 2 + IPV6CP_IID_LENGTH) {
            const uint8_t *iid = pkt + pos + 2;

            if (!ipv6cp_iid_zero(iid)
                && !(ctx->ipv6cp_ack_sent && !memcmp(iid, ctx->ipv6cp_peer_iid, IPV6CP_IID_LENGTH)))
                memcpy(ctx->ipv6cp_our_iid, iid, IPV6CP_IID_LENGTH);
            else
                ipv6cp_pick_iid(ctx->ipv6cp_our_iid, ctx->ipv6cp_peer_iid);
        }
        pos += pkt[pos + 1];
    }

    if (ctx->ipv6cp_retries++ < IPV6CP_MAX_RETRIES)
        ipv6cp_send_request(ctx);
    else {
        ipv6cp_log(ctx->log, "IPv6CP: Too many Configure-Naks; giving up on IPv6\n");
        ctx->ipv6cp_active = false;
    }
}

static void
ipv6cp_handle_reject(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    int pos = 4;

    if (!ctx->ipv6cp_req_sent || pkt[1] != ctx->ipv6cp_request_id)
        return;
    ctx->ipv6cp_req_sent = false;

    while (pos < pkt_len) {
        if (pkt[pos] == IPV6CP_OPT_INTERFACE_ID)
            ctx->ipv6cp_send_iid = false; /* peer does not want identifiers: ask for nothing */
        pos += pkt[pos + 1];
    }

    if (ctx->ipv6cp_retries++ < IPV6CP_MAX_RETRIES)
        ipv6cp_send_request(ctx);
    else
        ctx->ipv6cp_active = false;
}

void
ppp_ipv6cp_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    int total;

    if (!ctx || !pkt || pkt_len < 4)
        return;
    if (ctx->ppp_ipv6_mode == PPP_IPV6_AUTOMATIC && !ctx->ipv6cp_started
        && pkt[0] == PPP_CODE_CONFIGURE_REQUEST)
        ipv6cp_begin(ctx);
    if (!ctx->ipv6cp_active)
        return;
    total = (pkt[2] << 8) | pkt[3];
    if (total < 4 || total > pkt_len)
        return;
    pkt_len = total;

    switch (pkt[0]) {
        case PPP_CODE_CONFIGURE_REQUEST:
            if (ipv6cp_options_valid(pkt, pkt_len))
                ipv6cp_handle_request(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_CONFIGURE_ACK:
            if (ipv6cp_options_valid(pkt, pkt_len))
                ipv6cp_handle_ack(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_CONFIGURE_NAK:
            if (ipv6cp_options_valid(pkt, pkt_len))
                ipv6cp_handle_nak(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_CONFIGURE_REJECT:
            if (ipv6cp_options_valid(pkt, pkt_len))
                ipv6cp_handle_reject(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_TERMINATE_REQUEST: {
            uint8_t reply[4] = { PPP_CODE_TERMINATE_ACK, pkt[1], 0, 4 };

            ctx->ipv6cp_open         = false;
            ctx->ipv6cp_ack_sent     = false;
            ctx->ipv6cp_ack_received = false;
            ppp_send_frame(ctx, PPP_PROTO_IPV6CP, reply, 4);
            break;
        }
        case PPP_CODE_TERMINATE_ACK:
        case PPP_CODE_CODE_REJECT:
            break;
        default:
            ppp_send_code_reject(ctx, PPP_PROTO_IPV6CP, pkt, pkt_len);
            break;
    }
}

void
ppp_ipv6cp_tick(ppp_ctx_t *ctx)
{
    if (!ctx->ipv6cp_active || !ctx->ipv6cp_req_sent || ctx->ipv6cp_ack_received)
        return;
    if (++ctx->ipv6cp_timeout_ms < IPV6CP_TIMEOUT_MS)
        return;
    ctx->ipv6cp_timeout_ms = 0;
    if (ctx->ipv6cp_retries++ < IPV6CP_MAX_RETRIES) {
        ipv6cp_log(ctx->log, "IPv6CP: Configure-Request timed out; retrying\n");
        ipv6cp_send_request(ctx);
    } else {
        ipv6cp_log(ctx->log, "IPv6CP: negotiation timed out; continuing without IPv6\n");
        ctx->ipv6cp_active = false;
    }
}
