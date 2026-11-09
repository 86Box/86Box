/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IPX Control Protocol for the emulated modem's PPP link. RFC 1552.
 *
 *          The link is a network of two, so this end picks the network number
 *          and hands the guest a node unless it brought its own.
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
#include <86box/network/net_ipx.h>
#include <86box/modem/modem_ipxcp.h>
#include <86box/log.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
ipxcp_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ipxcp_log(priv, fmt, ...)
#endif

#define IPXCP_NETWORK_LENGTH 6
#define IPXCP_NODE_LENGTH    8

static const uint8_t ipxcp_server_node[IPX_NODE_LENGTH] = { 0x02, 0x86, 0xB0, 0x00, 0x00, 0x01 };
static const uint8_t ipxcp_guest_node[IPX_NODE_LENGTH]  = { 0x02, 0x86, 0xB0, 0x00, 0x00, 0x02 };

static uint32_t
ipxcp_get32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static int
ipxcp_put_network(uint8_t *out, uint32_t network)
{
    out[0] = IPXCP_OPT_NETWORK;
    out[1] = IPXCP_NETWORK_LENGTH;
    out[2] = (uint8_t) (network >> 24);
    out[3] = (uint8_t) (network >> 16);
    out[4] = (uint8_t) (network >> 8);
    out[5] = (uint8_t) network;
    return IPXCP_NETWORK_LENGTH;
}

static int
ipxcp_put_node(uint8_t *out, const uint8_t *node)
{
    out[0] = IPXCP_OPT_NODE;
    out[1] = IPXCP_NODE_LENGTH;
    memcpy(out + 2, node, IPX_NODE_LENGTH);
    return IPXCP_NODE_LENGTH;
}

static void
ipxcp_check_open(ppp_ctx_t *ctx)
{
    if (!ctx->ipxcp_open && ctx->ipxcp_ack_sent && ctx->ipxcp_ack_received) {
        ctx->ipxcp_open = true;
        ipxcp_log(ctx->log, "IPXCP: opened on network %08X\n", ctx->ipxcp_network);
    }
}

static void
ipxcp_send_request(ppp_ctx_t *ctx)
{
    uint8_t pkt[4 + IPXCP_NETWORK_LENGTH + IPXCP_NODE_LENGTH];
    int     length = 4;

    ctx->ipxcp_ack_received = false;
    ctx->ipxcp_timeout_ms   = 0;
    ctx->ipxcp_req_sent     = true;
    ctx->ipxcp_request_id   = ctx->ipxcp_id++;
    if (ctx->ipxcp_send_network)
        length += ipxcp_put_network(pkt + length, ctx->ipxcp_network);
    if (ctx->ipxcp_send_node)
        length += ipxcp_put_node(pkt + length, ctx->ipxcp_node);
    pkt[0] = PPP_CODE_CONFIGURE_REQUEST;
    pkt[1] = ctx->ipxcp_request_id;
    pkt[2] = 0;
    pkt[3] = (uint8_t) length;
    ppp_send_frame(ctx, PPP_PROTO_IPXCP, pkt, length);
}

static bool
ipxcp_options_valid(const uint8_t *pkt, int pkt_len)
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
ipxcp_send_reply(ppp_ctx_t *ctx, uint8_t code, uint8_t id, uint8_t *pkt, int length)
{
    pkt[0] = code;
    pkt[1] = id;
    pkt[2] = (uint8_t) (length >> 8);
    pkt[3] = (uint8_t) length;
    ppp_send_frame(ctx, PPP_PROTO_IPXCP, pkt, length);
}

static void
ipxcp_handle_request(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    uint8_t  reply[PPP_MAX_FRAME];
    uint8_t  rejected[PPP_MAX_FRAME];
    uint8_t  naked[PPP_MAX_FRAME];
    int      rejected_len = 4;
    int      naked_len    = 4;
    uint32_t network      = ctx->ipxcp_network;
    int      pos          = 4;

    if (pkt_len > (int) sizeof(reply))
        return;

    while (pos < pkt_len) {
        const uint8_t *option = pkt + pos;
        int            length = option[1];
        bool           reject = false;

        switch (option[0]) {
            case IPXCP_OPT_NETWORK:
                if (length != IPXCP_NETWORK_LENGTH)
                    reject = true;
                else if (ipxcp_get32(option + 2) < ctx->ipxcp_network)
                    naked_len += ipxcp_put_network(naked + naked_len, ctx->ipxcp_network);
                else
                    network = ipxcp_get32(option + 2); /* the higher number wins */
                break;
            case IPXCP_OPT_NODE:
                if (length != IPXCP_NODE_LENGTH)
                    reject = true;
                else if (!memcmp(option + 2, "\0\0\0\0\0\0", IPX_NODE_LENGTH)
                         || !memcmp(option + 2, ctx->ipxcp_node, IPX_NODE_LENGTH))
                    naked_len += ipxcp_put_node(naked + naked_len, ipxcp_guest_node);
                break;
            case IPXCP_OPT_ROUTING:
                /* Nothing routes here, so the guest can have RIP or nothing at all. */
                if (length < 4 || option[2] != 0
                    || (option[3] != IPXCP_ROUTING_NONE && option[3] != IPXCP_ROUTING_RIP_SAP))
                    reject = true;
                break;
            case IPXCP_OPT_ROUTER_NAME:
            case IPXCP_OPT_CONFIG_COMPLETE:
                break;
            default:
                reject = true;
                break;
        }
        if (reject) {
            memcpy(rejected + rejected_len, option, (size_t) length);
            rejected_len += length;
        }
        pos += length;
    }

    if (!ctx->ipxcp_active) {
        ctx->ipxcp_active = true;
        ctx->ipxcp_retries = 0;
        ctx->ipxcp_network = network;
        ipxcp_send_request(ctx);
    } else if (ctx->ipxcp_open || network != ctx->ipxcp_network) {
        ctx->ipxcp_open    = false;
        ctx->ipxcp_network = network;
        ipxcp_send_request(ctx);
    }

    if (rejected_len > 4) {
        ipxcp_send_reply(ctx, PPP_CODE_CONFIGURE_REJECT, pkt[1], rejected, rejected_len);
        ctx->ipxcp_ack_sent = false;
        return;
    }
    if (naked_len > 4) {
        ipxcp_send_reply(ctx, PPP_CODE_CONFIGURE_NAK, pkt[1], naked, naked_len);
        ctx->ipxcp_ack_sent = false;
        return;
    }

    memcpy(reply, pkt, (size_t) pkt_len);
    reply[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_send_frame(ctx, PPP_PROTO_IPXCP, reply, pkt_len);
    ctx->ipxcp_ack_sent = true;
    ipxcp_check_open(ctx);
}

/* The peer wants other values for what was asked, or none of it. */
static void
ipxcp_handle_objection(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    int pos = 4;

    if (!ipxcp_options_valid(pkt, pkt_len))
        return;
    while (pos < pkt_len) {
        const uint8_t *option = pkt + pos;

        if (pkt[0] == PPP_CODE_CONFIGURE_REJECT) {
            if (option[0] == IPXCP_OPT_NETWORK)
                ctx->ipxcp_send_network = false;
            else if (option[0] == IPXCP_OPT_NODE)
                ctx->ipxcp_send_node = false;
        } else if (option[0] == IPXCP_OPT_NETWORK && option[1] == IPXCP_NETWORK_LENGTH) {
            if (ipxcp_get32(option + 2) > ctx->ipxcp_network)
                ctx->ipxcp_network = ipxcp_get32(option + 2);
        } else if (option[0] == IPXCP_OPT_NODE && option[1] == IPXCP_NODE_LENGTH
                   && memcmp(option + 2, "\0\0\0\0\0\0", IPX_NODE_LENGTH)) {
            memcpy(ctx->ipxcp_node, option + 2, IPX_NODE_LENGTH);
        }
        pos += option[1];
    }
    ipxcp_send_request(ctx);
}

void
ppp_ipxcp_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    int length;

    if (!ctx || !pkt || pkt_len < 4)
        return;
    length = (pkt[2] << 8) | pkt[3];
    if (length < 4 || length > pkt_len)
        return;
    pkt_len = length;

    if (!ctx->ipxcp_active) {
        ctx->ipxcp_network      = IPX_NETWORK;
        ctx->ipxcp_send_network = true;
        ctx->ipxcp_send_node    = true;
        memcpy(ctx->ipxcp_node, ipxcp_server_node, IPX_NODE_LENGTH);
    }

    switch (pkt[0]) {
        case PPP_CODE_CONFIGURE_REQUEST:
            if (ipxcp_options_valid(pkt, pkt_len))
                ipxcp_handle_request(ctx, pkt, pkt_len);
            break;

        case PPP_CODE_CONFIGURE_ACK:
            if (ctx->ipxcp_req_sent && pkt[1] == ctx->ipxcp_request_id) {
                ctx->ipxcp_req_sent     = false;
                ctx->ipxcp_ack_received = true;
                ipxcp_check_open(ctx);
            }
            break;

        case PPP_CODE_CONFIGURE_NAK:
        case PPP_CODE_CONFIGURE_REJECT:
            if (ctx->ipxcp_req_sent && pkt[1] == ctx->ipxcp_request_id)
                ipxcp_handle_objection(ctx, pkt, pkt_len);
            break;

        case PPP_CODE_TERMINATE_REQUEST:
            {
                uint8_t ack[4] = { PPP_CODE_TERMINATE_ACK, pkt[1], 0, 4 };

                ctx->ipxcp_open     = false;
                ctx->ipxcp_active   = false;
                ctx->ipxcp_ack_sent = false;
                ppp_send_frame(ctx, PPP_PROTO_IPXCP, ack, sizeof(ack));
            }
            break;

        case PPP_CODE_TERMINATE_ACK:
        case PPP_CODE_CODE_REJECT:
            break;

        default:
            ppp_send_code_reject(ctx, PPP_PROTO_IPXCP, pkt, pkt_len);
            break;
    }
}

void
ppp_ipxcp_tick(ppp_ctx_t *ctx)
{
    if (!ctx->ipxcp_active || !ctx->ipxcp_req_sent || ++ctx->ipxcp_timeout_ms < IPXCP_TIMEOUT_MS)
        return;

    if (++ctx->ipxcp_retries > IPXCP_MAX_RETRIES) {
        ipxcp_log(ctx->log, "IPXCP: No answer; giving up on IPX\n");
        ctx->ipxcp_active   = false;
        ctx->ipxcp_req_sent = false;
        return;
    }
    ipxcp_send_request(ctx);
}

bool
ppp_ipx_ready(const ppp_ctx_t *ctx)
{
    return ctx && ctx->ipxcp_open && ctx->state >= PPP_STATE_IPCP_NEGOTIATE
        && ctx->state < PPP_STATE_TERMINATING;
}

uint32_t
ppp_ipx_network(const ppp_ctx_t *ctx)
{
    return ctx->ipxcp_network;
}

const uint8_t *
ppp_ipx_node(const ppp_ctx_t *ctx)
{
    return ctx->ipxcp_node;
}

int
ppp_ipx_max_packet(const ppp_ctx_t *ctx)
{
    return ctx->peer_mru;
}

void
ppp_ipx_receive(ppp_ctx_t *ctx, const uint8_t *packet, int len)
{
    if (ppp_ipx_ready(ctx) && ctx->network_send_ipx)
        ctx->network_send_ipx(ctx->modem, packet, len);
}

void
ppp_wrap_ipx(ppp_ctx_t *ctx, const uint8_t *packet, int len)
{
    if (ppp_ipx_ready(ctx) && packet && len > 0)
        ppp_send_frame(ctx, PPP_PROTO_IPX, packet, len);
}
