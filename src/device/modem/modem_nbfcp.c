/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NetBIOS Frames Control Protocol for the emulated modem's PPP
 *          link. RFC 2097.
 *
 *          The virtual server has no names of its own to project and needs
 *          nothing from a guest, so it agrees to what a guest offers and asks
 *          for no options itself.
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
#include <86box/modem/modem_nbfcp.h>
#include <86box/log.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
nbfcp_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define nbfcp_log(priv, fmt, ...)
#endif

static void
nbfcp_check_open(ppp_ctx_t *ctx)
{
    if (!ctx->nbfcp_open && ctx->nbfcp_ack_sent && ctx->nbfcp_ack_received) {
        ctx->nbfcp_open = true;
        nbfcp_log(ctx->log, "NBFCP: opened\n");
    }
}

static void
nbfcp_send_request(ppp_ctx_t *ctx)
{
    uint8_t pkt[4];

    ctx->nbfcp_ack_received = false;
    ctx->nbfcp_timeout_ms   = 0;
    ctx->nbfcp_req_sent     = true;
    ctx->nbfcp_request_id   = ctx->nbfcp_id++;
    pkt[0] = PPP_CODE_CONFIGURE_REQUEST;
    pkt[1] = ctx->nbfcp_request_id;
    pkt[2] = 0;
    pkt[3] = sizeof(pkt);
    ppp_send_frame(ctx, PPP_PROTO_NBFCP, pkt, sizeof(pkt));
}

static bool
nbfcp_options_valid(const uint8_t *pkt, int pkt_len)
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
nbfcp_handle_request(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    uint8_t reply[PPP_MAX_FRAME];
    uint8_t rejected[PPP_MAX_FRAME];
    int     rejected_len = 4;
    bool    mac_required = false;
    int     pos          = 4;

    if (pkt_len > (int) sizeof(reply))
        return;

    while (pos < pkt_len) {
        switch (pkt[pos]) {
            case NBFCP_OPT_NAME_PROJECTION:
            case NBFCP_OPT_PEER_INFORMATION:
            case NBFCP_OPT_MULTICAST_FILTER:
                break;
            case NBFCP_OPT_MAC_REQUIRED:
                mac_required = true;
                break;
            default:
                memcpy(rejected + rejected_len, pkt + pos, pkt[pos + 1]);
                rejected_len += pkt[pos + 1];
                break;
        }
        pos += pkt[pos + 1];
    }

    if (!ctx->nbfcp_active) {
        ctx->nbfcp_active = true;
        nbfcp_send_request(ctx);
    } else if (ctx->nbfcp_open) {
        ctx->nbfcp_open = false;
        nbfcp_send_request(ctx);
    }

    if (rejected_len > 4) {
        rejected[0] = PPP_CODE_CONFIGURE_REJECT;
        rejected[1] = pkt[1];
        rejected[2] = (uint8_t) (rejected_len >> 8);
        rejected[3] = (uint8_t) rejected_len;
        ppp_send_frame(ctx, PPP_PROTO_NBFCP, rejected, rejected_len);
        ctx->nbfcp_ack_sent = false;
        return;
    }

    memcpy(reply, pkt, (size_t) pkt_len);
    reply[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_send_frame(ctx, PPP_PROTO_NBFCP, reply, pkt_len);
    ctx->nbfcp_ack_sent     = true;
    ctx->nbfcp_mac_required = mac_required;
    nbfcp_check_open(ctx);
}

void
ppp_nbfcp_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    int length;

    if (!ctx || !pkt || pkt_len < 4)
        return;
    length = (pkt[2] << 8) | pkt[3];
    if (length < 4 || length > pkt_len)
        return;
    pkt_len = length;

    switch (pkt[0]) {
        case PPP_CODE_CONFIGURE_REQUEST:
            if (nbfcp_options_valid(pkt, pkt_len))
                nbfcp_handle_request(ctx, pkt, pkt_len);
            break;

        case PPP_CODE_CONFIGURE_ACK:
            if (ctx->nbfcp_req_sent && pkt[1] == ctx->nbfcp_request_id) {
                ctx->nbfcp_req_sent     = false;
                ctx->nbfcp_ack_received = true;
                nbfcp_check_open(ctx);
            }
            break;

        case PPP_CODE_CONFIGURE_NAK:
        case PPP_CODE_CONFIGURE_REJECT:
            /* A request with no options leaves nothing to object to. */
            if (ctx->nbfcp_req_sent && pkt[1] == ctx->nbfcp_request_id)
                nbfcp_send_request(ctx);
            break;

        case PPP_CODE_TERMINATE_REQUEST:
            {
                uint8_t ack[4] = { PPP_CODE_TERMINATE_ACK, pkt[1], 0, 4 };

                ctx->nbfcp_open     = false;
                ctx->nbfcp_active   = false;
                ctx->nbfcp_ack_sent = false;
                ppp_send_frame(ctx, PPP_PROTO_NBFCP, ack, sizeof(ack));
            }
            break;

        case PPP_CODE_TERMINATE_ACK:
        case PPP_CODE_CODE_REJECT:
            break;

        default:
            ppp_send_code_reject(ctx, PPP_PROTO_NBFCP, pkt, pkt_len);
            break;
    }
}

void
ppp_nbfcp_tick(ppp_ctx_t *ctx)
{
    if (!ctx->nbfcp_active || !ctx->nbfcp_req_sent || ++ctx->nbfcp_timeout_ms < NBFCP_TIMEOUT_MS)
        return;

    if (++ctx->nbfcp_retries > NBFCP_MAX_RETRIES) {
        nbfcp_log(ctx->log, "NBFCP: No answer; giving up on NetBEUI\n");
        ctx->nbfcp_active   = false;
        ctx->nbfcp_req_sent = false;
        return;
    }
    nbfcp_send_request(ctx);
}

bool
ppp_nbf_ready(const ppp_ctx_t *ctx)
{
    return ctx && ctx->nbfcp_open && ctx->state >= PPP_STATE_IPCP_NEGOTIATE
        && ctx->state < PPP_STATE_TERMINATING;
}

int
ppp_nbf_max_frame(const ppp_ctx_t *ctx)
{
    return ctx->peer_mru - (ctx->nbfcp_mac_required ? NBFCP_MAC_HEADER_LENGTH : 0);
}

void
ppp_nbf_receive(ppp_ctx_t *ctx, const uint8_t *frame, int len)
{
    if (!ppp_nbf_ready(ctx) || !ctx->network_send_nbf)
        return;
    if (ctx->nbfcp_mac_required) {
        if (len < NBFCP_MAC_HEADER_LENGTH)
            return;
        frame += NBFCP_MAC_HEADER_LENGTH;
        len -= NBFCP_MAC_HEADER_LENGTH;
    }
    ctx->network_send_nbf(ctx->modem, frame, len);
}

void
ppp_wrap_nbf(ppp_ctx_t *ctx, const uint8_t *frame, int len)
{
    uint8_t packet[PPP_MAX_FRAME];
    int     header = 0;

    if (!ppp_nbf_ready(ctx) || !frame || len <= 0)
        return;
    if (ctx->nbfcp_mac_required) {
        /* Both ends of a point-to-point link know who they are, so any address serves. */
        static const uint8_t addresses[NBFCP_MAC_HEADER_LENGTH] = {
            0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
            0x02, 0x00, 0x00, 0x00, 0x00, 0x02
        };

        memcpy(packet, addresses, sizeof(addresses));
        header = NBFCP_MAC_HEADER_LENGTH;
    }
    if (header + len > (int) sizeof(packet))
        return;
    memcpy(packet + header, frame, (size_t) len);
    ppp_send_frame(ctx, PPP_PROTO_NBF, packet, header + len);
}
