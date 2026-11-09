/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IPCP (Internet Protocol Control Protocol) implementation for
 *          PPP modem emulation. Server-side: negotiates client IP address,
 *          DNS and NetBIOS name servers, and Van Jacobson TCP/IP compression.
 *          RFC 1332 (IPCP), RFC 1877 (DNS and NetBIOS name-server options).
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
#include <86box/modem/modem_ipcp.h>
#include <86box/log.h>
#include <86box/modem/modem_debug.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
ipcp_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ipcp_log(priv, fmt, ...)
#endif

#if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
static const char *
ipcp_option_name(uint8_t option)
{
    switch (option) {
        case IPCP_OPT_IP_COMPRESSION: return "IP-Compression-Protocol";
        case IPCP_OPT_IP_ADDRESS:    return "IP-Address";
        case IPCP_OPT_DNS_PRIMARY:   return "Primary-DNS";
        case IPCP_OPT_DNS_SECONDARY: return "Secondary-DNS";
        case IPCP_OPT_NBNS_PRIMARY:  return "Primary-NBNS";
        case IPCP_OPT_NBNS_SECONDARY:return "Secondary-NBNS";
        default:                     return "unknown";
    }
}
#endif

static inline void
ipcp_put_ip(uint8_t *p, uint32_t ip)
{
    p[0] = (uint8_t) (ip >> 24);
    p[1] = (uint8_t) (ip >> 16);
    p[2] = (uint8_t) (ip >> 8);
    p[3] = (uint8_t) ip;
}

static inline uint32_t
ipcp_get_ip(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16)
         | ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

/* Send IPCP Configure-Request (we request our server IP) */
void
ppp_ipcp_send_config_request(ppp_ctx_t *ctx)
{
    uint8_t pkt[32];
    int     len = 4;

    ctx->ipcp_ack_received = false;
    ctx->ipcp_timeout_ms = 0;
    pkt[0] = PPP_CODE_CONFIGURE_REQUEST;
    ctx->ipcp_request_id = ctx->ipcp_id++;
    ctx->ipcp_request_ip = ctx->our_ip;
    pkt[1] = ctx->ipcp_request_id;

    /* Option: IP Address (our server IP) */
    pkt[len++] = IPCP_OPT_IP_ADDRESS;
    pkt[len++] = 6;
    ipcp_put_ip(pkt + len, ctx->our_ip);
    len += 4;

    if (ctx->ipcp_vj_request) {
        pkt[len++] = IPCP_OPT_IP_COMPRESSION;
        pkt[len++] = 6;
        pkt[len++] = (uint8_t) (PPP_PROTO_VJ_COMPRESSED >> 8);
        pkt[len++] = (uint8_t) PPP_PROTO_VJ_COMPRESSED;
        pkt[len++] = ctx->vj_rx_max_slot_id;
        pkt[len++] = ctx->vj_rx_comp_slot_id ? IPCP_VJ_COMP_SLOT_ID : 0;
    }

    pkt[2] = (uint8_t) (len >> 8);
    pkt[3] = (uint8_t) (len & 0xFF);

    ppp_send_frame(ctx, PPP_PROTO_IPCP, pkt, len);
    ctx->ipcp_req_sent = true;
    ipcp_log(ctx->log, "IPCP: Sent Configure-Request (our_ip=%d.%d.%d.%d)\n",
             (ctx->our_ip >> 24) & 0xFF, (ctx->our_ip >> 16) & 0xFF,
             (ctx->our_ip >> 8) & 0xFF, ctx->our_ip & 0xFF);
}

static bool
ipcp_response_matches_request(const ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len,
                              bool require_exact)
{
    int total;
    int pos = 4;
    bool got_address = false;
    bool got_vj = false;

    if (!ctx->ipcp_req_sent || pkt_len < 4 || pkt[1] != ctx->ipcp_request_id)
        return false;

    total = (pkt[2] << 8) | pkt[3];
    if (total != pkt_len || total < 4)
        return false;

    while (pos < total) {
        uint8_t option_len;

        if (pos + 2 > total)
            return false;
        option_len = pkt[pos + 1];
        if (option_len < 2 || pos + option_len > total)
            return false;
        if (require_exact
            && ((pos == 4 && pkt[pos] != IPCP_OPT_IP_ADDRESS)
                || (pos > 4 && pkt[pos] != IPCP_OPT_IP_COMPRESSION)))
            return false;

        if (pkt[pos] == IPCP_OPT_IP_ADDRESS) {
            if (got_address || option_len != 6)
                return false;
            if ((require_exact || pkt[0] == PPP_CODE_CONFIGURE_REJECT)
                && ipcp_get_ip(pkt + pos + 2) != ctx->ipcp_request_ip)
                return false;
            got_address = true;
        } else if (pkt[pos] == IPCP_OPT_IP_COMPRESSION) {
            if (got_vj || !ctx->ipcp_vj_request || option_len != 6
                || pkt[pos + 2] != (uint8_t) (PPP_PROTO_VJ_COMPRESSED >> 8)
                || pkt[pos + 3] != (uint8_t) PPP_PROTO_VJ_COMPRESSED
                || pkt[pos + 4] > IPCP_VJ_MAX_SLOT_ID
                || pkt[pos + 5] > IPCP_VJ_COMP_SLOT_ID)
                return false;
            if ((require_exact || pkt[0] == PPP_CODE_CONFIGURE_REJECT)
                && (pkt[pos + 4] != ctx->vj_rx_max_slot_id
                    || (pkt[pos + 5] != 0) != ctx->vj_rx_comp_slot_id))
                return false;
            got_vj = true;
        } else {
            return false;
        }

        pos += option_len;
    }

    if (require_exact)
        return got_address && got_vj == ctx->ipcp_vj_request;
    return got_address || got_vj;
}

static bool
ipcp_request_options_valid(const uint8_t *pkt, int pkt_len)
{
    int pos = 4;

    while (pos < pkt_len) {
        if (pos + 2 > pkt_len)
            return false;
        uint8_t option_len = pkt[pos + 1];
        if (option_len < 2 || pos + option_len > pkt_len)
            return false;
        pos += option_len;
    }

    return true;
}

/* Handle IPCP Configure-Request from peer */
static void
ipcp_handle_config_request(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    uint8_t ack[PPP_MAX_FRAME];
    uint8_t nak[PPP_MAX_FRAME];
    uint8_t rej[PPP_MAX_FRAME];
    int     ack_len = 4, nak_len = 4, rej_len = 4;
    uint8_t id      = pkt[1];
    int     total   = (pkt[2] << 8) | pkt[3];
    int     pos     = 4;
    bool    peer_vj = false;
    bool    seen_dns1 = false;
    bool    seen_dns2 = false;
    bool    seen_wins1 = false;
    bool    seen_wins2 = false;
    uint8_t peer_vj_max_slot_id = 0;
    bool    peer_vj_comp_slot_id = false;

    ipcp_log(ctx->log, "IPCP: Received Configure-Request (id=%d)\n", id);

    while (pos < total && pos < pkt_len) {
        uint8_t opt_type = pkt[pos];
        uint8_t opt_len  = (pos + 1 < pkt_len) ? pkt[pos + 1] : 0;

        if (opt_len < 2 || pos + opt_len > pkt_len)
            break;

        MODEM_DEBUG_LOG(ctx->log, "IPCP: peer option=%s (type=%u) length=%u\n",
                ipcp_option_name(opt_type), (unsigned) opt_type,
                (unsigned) opt_len);

        switch (opt_type) {
            case IPCP_OPT_IP_ADDRESS:
                if (opt_len == 6) {
                    uint32_t requested_ip = ipcp_get_ip(pkt + pos + 2);
                    if (requested_ip == ctx->peer_ip || requested_ip == 0) {
                        /* Accept the request or offer our configured IP */
                        if (requested_ip == 0) {
                            /* Peer is requesting an IP - NAK with our assigned IP */
                            nak[nak_len++] = IPCP_OPT_IP_ADDRESS;
                            nak[nak_len++] = 6;
                            ipcp_put_ip(nak + nak_len, ctx->peer_ip);
                            nak_len += 4;
                        } else {
                            memcpy(ack + ack_len, pkt + pos, opt_len);
                            ack_len += opt_len;
                        }
                    } else {
                        /* Different IP requested - NAK with our preference */
                        nak[nak_len++] = IPCP_OPT_IP_ADDRESS;
                        nak[nak_len++] = 6;
                        ipcp_put_ip(nak + nak_len, ctx->peer_ip);
                        nak_len += 4;
                    }
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case IPCP_OPT_DNS_PRIMARY:
                seen_dns1 = true;
                if (ctx->ipcp_dns_wins_mode == 0) {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                } else if (opt_len == 6) {
                    uint32_t requested_dns = ipcp_get_ip(pkt + pos + 2);
                    if (requested_dns == 0 && ctx->dns1 == 0) {
                        memcpy(rej + rej_len, pkt + pos, opt_len);
                        rej_len += opt_len;
                    } else if (requested_dns == ctx->dns1) {
                        memcpy(ack + ack_len, pkt + pos, opt_len);
                        ack_len += opt_len;
                    } else {
                        /* NAK with our DNS */
                        nak[nak_len++] = IPCP_OPT_DNS_PRIMARY;
                        nak[nak_len++] = 6;
                        ipcp_put_ip(nak + nak_len, ctx->dns1);
                        nak_len += 4;
                    }
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case IPCP_OPT_DNS_SECONDARY:
                seen_dns2 = true;
                if (ctx->ipcp_dns_wins_mode == 0) {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                } else if (opt_len == 6) {
                    uint32_t requested_dns = ipcp_get_ip(pkt + pos + 2);
                    if (requested_dns == 0 && ctx->dns2 == 0) {
                        memcpy(rej + rej_len, pkt + pos, opt_len);
                        rej_len += opt_len;
                    } else if (requested_dns == ctx->dns2) {
                        memcpy(ack + ack_len, pkt + pos, opt_len);
                        ack_len += opt_len;
                    } else {
                        nak[nak_len++] = IPCP_OPT_DNS_SECONDARY;
                        nak[nak_len++] = 6;
                        ipcp_put_ip(nak + nak_len, ctx->dns2);
                        nak_len += 4;
                    }
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case IPCP_OPT_NBNS_PRIMARY:
            case IPCP_OPT_NBNS_SECONDARY:
                if (opt_type == IPCP_OPT_NBNS_PRIMARY)
                    seen_wins1 = true;
                else
                    seen_wins2 = true;
                if (ctx->ipcp_dns_wins_mode == 0) {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                } else if (opt_len == 6) {
                    uint32_t server = opt_type == IPCP_OPT_NBNS_PRIMARY ? ctx->wins1 : ctx->wins2;
                    uint32_t requested_server = ipcp_get_ip(pkt + pos + 2);
                    if (server == 0 && requested_server == 0) {
                        memcpy(rej + rej_len, pkt + pos, opt_len);
                        rej_len += opt_len;
                    } else if (requested_server == server) {
                        memcpy(ack + ack_len, pkt + pos, opt_len);
                        ack_len += opt_len;
                    } else {
                        nak[nak_len++] = opt_type;
                        nak[nak_len++] = 6;
                        ipcp_put_ip(nak + nak_len, server);
                        nak_len += 4;
                    }
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case IPCP_OPT_IP_COMPRESSION:
                if (opt_len == 6
                    && pkt[pos + 2] == (uint8_t) (PPP_PROTO_VJ_COMPRESSED >> 8)
                    && pkt[pos + 3] == (uint8_t) PPP_PROTO_VJ_COMPRESSED
                    && pkt[pos + 5] <= 1) {
                    uint8_t max_slot_id = pkt[pos + 4];
                    if (max_slot_id <= IPCP_VJ_MAX_SLOT_ID
                        && !(ctx->ipcp_vj_mode == 2 && pkt[pos + 5] != 0)) {
                        memcpy(ack + ack_len, pkt + pos, opt_len);
                        ack_len += opt_len;
                        peer_vj = true;
                        peer_vj_max_slot_id = max_slot_id;
                        peer_vj_comp_slot_id = pkt[pos + 5] != 0;
                    } else if (max_slot_id <= IPCP_VJ_MAX_SLOT_ID) {
                        nak[nak_len++] = IPCP_OPT_IP_COMPRESSION;
                        nak[nak_len++] = 6;
                        nak[nak_len++] = 0;
                        nak[nak_len++] = (uint8_t) PPP_PROTO_VJ_COMPRESSED;
                        nak[nak_len++] = max_slot_id;
                        nak[nak_len++] = 0;
                    } else {
                        nak[nak_len++] = IPCP_OPT_IP_COMPRESSION;
                        nak[nak_len++] = 6;
                        nak[nak_len++] = 0;
                        nak[nak_len++] = (uint8_t) PPP_PROTO_VJ_COMPRESSED;
                        nak[nak_len++] = IPCP_VJ_MAX_SLOT_ID;
                        nak[nak_len++] = pkt[pos + 5];
                    }
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            default:
                /* Unknown option - reject */
                memcpy(rej + rej_len, pkt + pos, opt_len);
                rej_len += opt_len;
                break;
        }
        pos += opt_len;
    }

    if (ctx->ipcp_vj_mode == 2 && !peer_vj && nak_len == 4 && rej_len == 4) {
        ipcp_log(ctx->log, "IPCP: Client omitted required Van Jacobson compression\n");
        ctx->state = PPP_STATE_DEAD;
        return;
    }

    if (ctx->ipcp_dns_wins_mode == 2
        && ((ctx->dns1 != 0 && !seen_dns1) || (ctx->dns2 != 0 && !seen_dns2)
            || (ctx->wins1 != 0 && !seen_wins1) || (ctx->wins2 != 0 && !seen_wins2))) {
        ipcp_log(ctx->log, "IPCP: Client omitted a required DNS/WINS option\n");
        ctx->state = PPP_STATE_DEAD;
        return;
    }

    if (rej_len > 4) {
        ctx->vj_tx_enabled = false;
        rej[0] = PPP_CODE_CONFIGURE_REJECT;
        rej[1] = id;
        rej[2] = (uint8_t) (rej_len >> 8);
        rej[3] = (uint8_t) (rej_len & 0xFF);
        ppp_send_frame(ctx, PPP_PROTO_IPCP, rej, rej_len);
        ipcp_log(ctx->log, "IPCP: Sent Configure-Reject\n");
    } else if (nak_len > 4) {
        ctx->vj_tx_enabled = false;
        nak[0] = PPP_CODE_CONFIGURE_NAK;
        nak[1] = id;
        nak[2] = (uint8_t) (nak_len >> 8);
        nak[3] = (uint8_t) (nak_len & 0xFF);
        ppp_send_frame(ctx, PPP_PROTO_IPCP, nak, nak_len);
        ipcp_log(ctx->log, "IPCP: Sent Configure-Nak\n");
    } else {
        ctx->vj_tx_enabled = peer_vj;
        ctx->vj_tx_max_slot_id = peer_vj_max_slot_id;
        ctx->vj_tx_comp_slot_id = peer_vj_comp_slot_id;
        ack[0] = PPP_CODE_CONFIGURE_ACK;
        ack[1] = id;
        ack[2] = (uint8_t) (ack_len >> 8);
        ack[3] = (uint8_t) (ack_len & 0xFF);
        ppp_send_frame(ctx, PPP_PROTO_IPCP, ack, ack_len);
        ctx->ipcp_ack_sent = true;
        ipcp_log(ctx->log, "IPCP: Sent Configure-Ack\n");
    }
}

/* Handle IPCP Configure-Ack from peer */
static void
ipcp_handle_config_ack(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    if (!ipcp_response_matches_request(ctx, pkt, pkt_len, true))
        return;

    ipcp_log(ctx->log, "IPCP: Received Configure-Ack\n");
    ctx->ipcp_req_sent = false;
    ctx->ipcp_ack_received = true;
    ctx->vj_rx_enabled = ctx->ipcp_vj_request;
    ppp_advance_state(ctx);
}

/* Handle IPCP Configure-Nak from peer */
static void
ipcp_handle_config_nak(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    int total = (pkt[2] << 8) | pkt[3];
    int pos   = 4;

    if (!ipcp_response_matches_request(ctx, pkt, pkt_len, false))
        return;

    ctx->ipcp_req_sent = false;
    ctx->ipcp_ack_received = false;
    ipcp_log(ctx->log, "IPCP: Received Configure-Nak\n");

    while (pos < total && pos < pkt_len) {
        uint8_t opt_type = pkt[pos];
        uint8_t opt_len  = (pos + 1 < pkt_len) ? pkt[pos + 1] : 0;

        if (opt_len < 2 || pos + opt_len > pkt_len)
            break;

        MODEM_DEBUG_LOG(ctx->log, "IPCP: Nak option=%s (type=%u) length=%u\n",
                ipcp_option_name(opt_type), (unsigned) opt_type,
                (unsigned) opt_len);

        if (opt_type == IPCP_OPT_IP_ADDRESS && opt_len == 6) {
            ctx->our_ip = ipcp_get_ip(pkt + pos + 2);
            ipcp_log(ctx->log, "IPCP: Peer suggests our IP = %d.%d.%d.%d\n",
                     (ctx->our_ip >> 24) & 0xFF, (ctx->our_ip >> 16) & 0xFF,
                     (ctx->our_ip >> 8) & 0xFF, ctx->our_ip & 0xFF);
             } else if (opt_type == IPCP_OPT_IP_COMPRESSION && opt_len == 6
                     && pkt[pos + 2] == (uint8_t) (PPP_PROTO_VJ_COMPRESSED >> 8)
                     && pkt[pos + 3] == (uint8_t) PPP_PROTO_VJ_COMPRESSED
                     && pkt[pos + 4] <= IPCP_VJ_MAX_SLOT_ID && pkt[pos + 5] <= 1) {
                 if (ctx->ipcp_vj_mode == 2 && pkt[pos + 5] != 0) {
                     ctx->state = PPP_STATE_DEAD;
                     return;
                 }
                 ctx->vj_rx_max_slot_id = pkt[pos + 4];
                 ctx->vj_rx_comp_slot_id = pkt[pos + 5] != 0;
        }
        pos += opt_len;
    }

    if (ctx->ipcp_retries++ < 10)
        ppp_ipcp_send_config_request(ctx);
}

/* Handle IPCP Configure-Reject from peer */
static void
ipcp_handle_config_reject(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    if (!ipcp_response_matches_request(ctx, pkt, pkt_len, false))
        return;

    ctx->ipcp_req_sent = false;
    ctx->ipcp_ack_received = false;
    ipcp_log(ctx->log, "IPCP: Received Configure-Reject\n");

    int total = (pkt[2] << 8) | pkt[3];
    bool rejected_address = false;
    for (int pos = 4; pos < total;) {
        uint8_t opt_len = pkt[pos + 1];
        if (pkt[pos] == IPCP_OPT_IP_ADDRESS)
            rejected_address = true;
        else if (pkt[pos] == IPCP_OPT_IP_COMPRESSION) {
            if (ctx->ipcp_vj_mode == 2) {
                ctx->state = PPP_STATE_DEAD;
                return;
            }
            ctx->ipcp_vj_request = false;
        }
        pos += opt_len;
    }

    if (rejected_address) {
        ctx->state = PPP_STATE_DEAD;
    } else {
        ctx->vj_rx_enabled = false;
        if (ctx->ipcp_retries++ < 10)
            ppp_ipcp_send_config_request(ctx);
    }
}

void
ppp_ipcp_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    if (!ctx || !pkt || pkt_len < 4)
        return;

    int total = (pkt[2] << 8) | pkt[3];
    if (total < 4 || total > pkt_len)
        return;
    pkt_len = total;

    uint8_t code = pkt[0];
    MODEM_DEBUG_LOG(ctx->log, "IPCP: received code=%u id=%u length=%d\n",
                    (unsigned) code, (unsigned) pkt[1], (pkt[2] << 8) | pkt[3]);

    switch (code) {
        case PPP_CODE_CONFIGURE_REQUEST:
            if (!ipcp_request_options_valid(pkt, pkt_len))
                return;
            ctx->ipcp_ack_sent = false;
            ipcp_handle_config_request(ctx, pkt, pkt_len);
            ppp_advance_state(ctx);
            break;
        case PPP_CODE_CONFIGURE_ACK:
            ipcp_handle_config_ack(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_CONFIGURE_NAK:
            ipcp_handle_config_nak(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_CONFIGURE_REJECT:
            ipcp_handle_config_reject(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_TERMINATE_REQUEST:
            {
                uint8_t reply[4];
                reply[0] = PPP_CODE_TERMINATE_ACK;
                reply[1] = pkt[1];
                reply[2] = 0;
                reply[3] = 4;
                ppp_send_frame(ctx, PPP_PROTO_IPCP, reply, 4);
            }
            break;
        case PPP_CODE_TERMINATE_ACK:
        case PPP_CODE_CODE_REJECT:
            break;
        default:
            ipcp_log(ctx->log, "IPCP: Unknown code %d\n", code);
            ppp_send_code_reject(ctx, PPP_PROTO_IPCP, pkt, pkt_len);
            break;
    }
}
