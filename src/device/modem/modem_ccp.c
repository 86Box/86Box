/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CCP negotiation and data compression for NT31 RAS, Stac LZS (RFC 1974),
 *          MPPC (RFC 2118), Deflate (RFC 1979), BSD-Compress (RFC 1977), Predictor (RFC 1978),
 *          and stateful/stateless 40-, 56-, and 128-bit MPPE (RFC 3078).
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
#include <86box/log.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_mppe.h>
#include <86box/modem/modem_debug.h>

static uint32_t
ccp_nt31ras_window_size(uint32_t features)
{
    if (features & CCP_NT31RAS_WINDOW_64K_FEATURE)
        return CCP_NT31RAS_WINDOW_64K;
    if (features & CCP_NT31RAS_WINDOW_32K_FEATURE)
        return CCP_NT31RAS_WINDOW_32K;
    if (features & CCP_NT31RAS_WINDOW_16K_FEATURE)
        return CCP_NT31RAS_WINDOW_16K;
    if (features & CCP_NT31RAS_WINDOW_8K_FEATURE)
        return CCP_NT31RAS_WINDOW_8K;
    return 0;
}

static uint32_t
ccp_minimum_key_bit(uint8_t minimum_bits)
{
    if (minimum_bits >= 128)
        return CCP_MPPE_128;
    if (minimum_bits >= 56)
        return CCP_MPPE_56;
    if (minimum_bits >= 40)
        return CCP_MPPE_40;
    return 0;
}

static uint32_t
ccp_get_u32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16)
         | ((uint32_t) p[2] << 8) | p[3];
}

static void
ccp_put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t) (value >> 24);
    p[1] = (uint8_t) (value >> 16);
    p[2] = (uint8_t) (value >> 8);
    p[3] = (uint8_t) value;
}

static uint32_t
ccp_get_le_u32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8)
         | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void
ccp_put_le_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t) value;
    p[1] = (uint8_t) (value >> 8);
    p[2] = (uint8_t) (value >> 16);
    p[3] = (uint8_t) (value >> 24);
}

const char *
ppp_ccp_method_name(uint8_t method)
{
    switch (method) {
        case PPP_CCP_METHOD_NONE:       return "none";
        case PPP_CCP_METHOD_PREDICTOR1: return "Predictor-1";
        case PPP_CCP_METHOD_PREDICTOR2: return "Predictor-2";
        case PPP_CCP_METHOD_LZS:        return "Stac LZS";
        case PPP_CCP_METHOD_LZS_EXTENDED: return "Stac LZS Extended";
        case PPP_CCP_METHOD_LZS_DCP:    return "LZS-DCP";
        case PPP_CCP_METHOD_V44:        return "V.44/LZJH";
        case PPP_CCP_METHOD_MPPE:       return "MPPE";
        case PPP_CCP_METHOD_MPPC:       return "MPPC";
        case PPP_CCP_METHOD_GANDALF_FZA:return "Gandalf FZA";
        case PPP_CCP_METHOD_BSD:        return "BSD-Compress";
        case PPP_CCP_METHOD_DEFLATE:    return "Deflate";
        case PPP_CCP_METHOD_NT31RAS:    return "NT31-RAS";
        default:                        return "unknown";
    }
}

#if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
static const char *
ccp_code_name(uint8_t code)
{
    switch (code) {
        case PPP_CODE_CONFIGURE_REQUEST: return "Configure-Request";
        case PPP_CODE_CONFIGURE_ACK:     return "Configure-Ack";
        case PPP_CODE_CONFIGURE_NAK:     return "Configure-Nak";
        case PPP_CODE_CONFIGURE_REJECT:  return "Configure-Reject";
        case PPP_CODE_TERMINATE_REQUEST: return "Terminate-Request";
        case PPP_CODE_TERMINATE_ACK:     return "Terminate-Ack";
        case PPP_CODE_RESET_REQUEST:     return "Reset-Request";
        case PPP_CODE_RESET_ACK:         return "Reset-Ack";
        default:                         return "Unknown";
    }
}

static const char *
ccp_option_name(uint8_t type, uint32_t value)
{
    if (type >= 28 && type <= 254 && type != CCP_OPT_NT31RAS)
        return "Unassigned CCP option";

    switch (type) {
        case 0:                  return "Vendor-specific OUI";
        case CCP_OPT_PREDICTOR1: return "Predictor-1";
        case CCP_OPT_PREDICTOR2: return "Predictor-2";
        case PPP_CCP_METHOD_PUDDLEJUMPER: return "Puddle Jumper";
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:                 return "Unassigned CCP option";
        case PPP_CCP_METHOD_HPPPC: return "Hewlett-Packard PPC";
        case CCP_OPT_LZS:        return "Stac LZS";
        case CCP_OPT_MPPE:       return value == CCP_MPPC ? "Microsoft PPC (MPPC)"
                              : "Microsoft PPC (MPPE)";
        case PPP_CCP_METHOD_GANDALF_FZA: return "Gandalf FZA";
        case PPP_CCP_METHOD_V42BIS: return "V.42bis";
        case CCP_OPT_BSD:        return "BSD-Compress";
        case PPP_CCP_METHOD_DCP: return "LZS-DCP";
        case PPP_CCP_METHOD_MVRCA: return "MVRCA (Magnalink) / Deflate (draft)";
        case 22:
        case 25:                 return "Unassigned CCP option";
        case CCP_OPT_DEFLATE:    return "Deflate";
        case CCP_OPT_V44:        return "V.44/LZJH";
        case CCP_OPT_NT31RAS:    return "NT31-RAS";
        case 255:                return "Reserved CCP option";
        default:                 return "Unknown";
    }
}

static const char *
ccp_lzs_check_mode_name(uint8_t check_mode)
{
    switch (check_mode) {
        case 0:  return "None";
        case 1:  return "LCB";
        case 2:  return "CRC";
        case 3:  return "Sequence Number";
        case 4:  return "Extended Mode";
        default: return "Unknown";
    }
}

static void
ccp_log_packet(ppp_ctx_t *ctx, const char *direction, const uint8_t *pkt, int total)
{
    uint8_t code;

    if (!ctx || !pkt || total < 4)
        return;
    code = pkt[0];
    MODEM_DEBUG_LOG(ctx->log, "CCP: %s %s (code=%u id=%u length=%d)\n",
                    direction, ccp_code_name(code), (unsigned) code,
                    (unsigned) pkt[1], total);

    if (code < PPP_CODE_CONFIGURE_REQUEST || code > PPP_CODE_CONFIGURE_REJECT)
        return;

    for (int pos = 4; pos < total;) {
        uint8_t type;
        uint8_t option_len;
        uint32_t value = 0;
        char details[256] = "";

        if (pos + 2 > total) {
            MODEM_DEBUG_LOG(ctx->log, "CCP: malformed option header at offset=%d\n", pos);
            return;
        }
        type = pkt[pos];
        option_len = pkt[pos + 1];
        if (option_len < 2 || pos + option_len > total) {
            MODEM_DEBUG_LOG(ctx->log, "CCP: malformed option type=%u length=%u offset=%d\n",
                            (unsigned) type, (unsigned) option_len, pos);
            return;
        }
        if (type == CCP_OPT_MPPE && option_len == 6)
            value = ccp_get_u32(pkt + pos + 2);
        else if (type == CCP_OPT_NT31RAS && option_len >= 6)
            value = ccp_get_le_u32(pkt + pos + 2);

        if (type == CCP_OPT_MPPE && option_len == 6) {
            snprintf(details, sizeof(details), " bits=0x%08X mppc=%s key-40=%s key-56=%s "
                     "key-128=%s stateless=%s",
                     (unsigned) value, (value & CCP_MPPC) ? "yes" : "no",
                     (value & CCP_MPPE_40) ? "yes" : "no",
                     (value & CCP_MPPE_56) ? "yes" : "no",
                     (value & CCP_MPPE_128) ? "yes" : "no",
                     (value & CCP_MPPE_STATELESS) ? "yes" : "no");
        } else if ((type == CCP_OPT_DEFLATE || type == CCP_OPT_DEFLATE_DRAFT) && option_len == 4) {
            snprintf(details, sizeof(details), " window=0x%02X method=0x%02X",
                     pkt[pos + 2], pkt[pos + 3]);
        } else if (type == CCP_OPT_BSD && option_len == 3) {
            snprintf(details, sizeof(details), " version-and-bits=0x%02X", pkt[pos + 2]);
        } else if (type == CCP_OPT_LZS && option_len == 5) {
            uint8_t check_mode = pkt[pos + 4];
            snprintf(details, sizeof(details), " history-count=%u check-mode=%u (%s)",
                     (unsigned) (((uint16_t) pkt[pos + 2] << 8) | pkt[pos + 3]),
                     (unsigned) check_mode, ccp_lzs_check_mode_name(check_mode));
        } else if (type == CCP_OPT_LZS_DCP && option_len == 6) {
            snprintf(details, sizeof(details), " history-count=%u check-mode=%u process-mode=%u",
                     (unsigned) (((uint16_t) pkt[pos + 2] << 8) | pkt[pos + 3]),
                     (unsigned) pkt[pos + 4], (unsigned) pkt[pos + 5]);
        } else if (type == CCP_OPT_NT31RAS && option_len == 22) {
            uint32_t receive_features = ccp_get_le_u32(pkt + pos + 6);
            uint32_t maximum_send = ccp_get_le_u32(pkt + pos + 10);
            uint32_t maximum_receive = ccp_get_le_u32(pkt + pos + 14);
            snprintf(details, sizeof(details), " send-features=0x%08X receive-features=0x%08X"
                     " send-window=%u receive-window=%u max-send=%u max-receive=%u",
                     (unsigned) value, (unsigned) receive_features,
                     (unsigned) ccp_nt31ras_window_size(value),
                     (unsigned) ccp_nt31ras_window_size(receive_features),
                     (unsigned) maximum_send, (unsigned) maximum_receive);
        }
        MODEM_DEBUG_LOG(ctx->log, "CCP: %s option=%s (type=%u) length=%u%s\n",
                        direction, ccp_option_name(type, value), (unsigned) type,
                        (unsigned) option_len, details);
        pos += option_len;
    }
}

static void
ccp_log_state(ppp_ctx_t *ctx, const char *event)
{
    const char *tx_method = (ctx->ccp_tx_bits & CCP_MPPE_KEY_BITS)
                          ? "MPPE" : ppp_ccp_method_name(ctx->ccp_tx_method);
    const char *rx_method = (ctx->ccp_request_bits & CCP_MPPE_KEY_BITS)
                          ? "MPPE" : ppp_ccp_method_name(ctx->ccp_rx_method);

    MODEM_DEBUG_LOG(ctx->log, "CCP: %s open=%s tx-method=%s rx-method=%s "
                    "MPPE-tx=%s MPPE-rx=%s peer-MPPE=%s plaintext-fallback=%s "
                    "tx-bits=0x%08X rx-bits=0x%08X\n",
                    event, ctx->ccp_open ? "yes" : "no",
                    tx_method, rx_method,
                    ctx->mppe_tx_enabled ? "on" : "off",
                    ctx->mppe_rx_enabled ? "on" : "off",
                    ctx->ccp_peer_mppe ? "yes" : "no",
                    ctx->ccp_plaintext_fallback ? "yes" : "no",
                    (unsigned) ctx->ccp_tx_bits, (unsigned) ctx->ccp_request_bits);
}
#else
#    define ccp_log_packet(...) ((void) 0)
#    define ccp_log_state(...)  ((void) 0)
#endif

static void
ccp_update_state(ppp_ctx_t *ctx)
{
    bool tx_only = ctx->ccp_direction == 1;
    bool rx_only = ctx->ccp_direction == 2;
    bool passive = ctx->ccp_mode == 2;
    /* Asymmetric preferred: use whichever direction the peer agreed to. When MPPE
       is required both directions must be protected, so it behaves bidirectionally. */
    bool asymmetric = ctx->ccp_direction == PPP_CCP_DIRECTION_ASYMMETRIC_PREFERRED
                   && ctx->mppe_min_bits == 0;

    ctx->ccp_open = !ctx->ccp_plaintext_fallback
                 && (tx_only ? ctx->ccp_ack_sent
                     : rx_only ? ctx->ccp_ack_received
                     : passive ? ctx->ccp_ack_sent
                     : asymmetric ? (ctx->ccp_ack_sent || ctx->ccp_ack_received)
                     : ctx->ccp_ack_sent && ctx->ccp_ack_received);
    ctx->mppe_tx_enabled = ctx->ccp_open && ctx->ccp_peer_mppe
                        && ctx->mppe_keys_ready;
    ctx->mppe_rx_enabled = ctx->ccp_open && ctx->ccp_ack_received
                        && ctx->mppe_keys_ready
                        && (ctx->ccp_request_bits & CCP_MPPE_KEY_BITS) != 0;
    ccp_log_state(ctx, "state");
}

/* Our own Configure-Request was rejected or went unanswered. In asymmetric mode
   keep the compression we already agreed to transmit (the peer's request we
   Acked) and just give up receiving it; otherwise drop CCP altogether. */
static void
ccp_request_failed(ppp_ctx_t *ctx)
{
    if (ctx->ccp_direction == PPP_CCP_DIRECTION_ASYMMETRIC_PREFERRED
        && ctx->mppe_min_bits == 0 && ctx->ccp_ack_sent
        && !ctx->ccp_plaintext_fallback) {
        ctx->ccp_req_sent = false;
        ctx->ccp_ack_received = false;
        ccp_update_state(ctx);
        return;
    }
    ppp_ccp_fallback_plaintext(ctx);
}

static void
ccp_send_request(ppp_ctx_t *ctx, uint32_t bits)
{
    uint8_t request[10] = {
        PPP_CODE_CONFIGURE_REQUEST, 0, 0, 10,
        CCP_OPT_MPPE, 6, 0, 0, 0, 0
    };

    request[1] = ctx->ccp_id++;
    ccp_put_u32(request + 6, bits);
    memcpy(ctx->ccp_request, request, sizeof(request));
    ctx->ccp_request_len = sizeof(request);
    ctx->ccp_request_id = request[1];
    ctx->ccp_request_bits = bits;
    ctx->ccp_request_method = PPP_CCP_METHOD_MPPE;
    ctx->ccp_req_sent = true;
    ctx->ccp_timeout_ms = 0;
    ccp_log_packet(ctx, "TX", request, sizeof(request));
    ppp_send_frame(ctx, PPP_PROTO_CCP, request, sizeof(request));
}

static bool
ccp_send_codec_request(ppp_ctx_t *ctx, uint8_t method)
{
    uint8_t request[28] = { PPP_CODE_CONFIGURE_REQUEST, 0, 0, 0 };
    int request_len = 4;

    if (!ctx || ctx->mppe_min_bits > 0)
        return false;
    switch (method) {
        case PPP_CCP_METHOD_PREDICTOR1:
            request[request_len++] = CCP_OPT_PREDICTOR1;
            request[request_len++] = 2;
            break;
        case PPP_CCP_METHOD_PREDICTOR2:
            request[request_len++] = CCP_OPT_PREDICTOR2;
            request[request_len++] = 2;
            break;
        case PPP_CCP_METHOD_LZS:
            request[request_len++] = CCP_OPT_LZS;
            request[request_len++] = 5;
            request[request_len++] = 0;
            request[request_len++] = 1;
            request[request_len++] = 3;
            break;
        case PPP_CCP_METHOD_LZS_EXTENDED:
            request[request_len++] = CCP_OPT_LZS;
            request[request_len++] = 5;
            request[request_len++] = 0;
            request[request_len++] = 1;
            request[request_len++] = 4;
            break;
        case PPP_CCP_METHOD_LZS_DCP:
            request[request_len++] = CCP_OPT_LZS_DCP;
            request[request_len++] = 6;
            request[request_len++] = 0;
            request[request_len++] = 0;
            request[request_len++] = 0;
            request[request_len++] = 0;
            break;
        case PPP_CCP_METHOD_V44:
            request[request_len++] = CCP_OPT_V44;
            request[request_len++] = 4;
            request[request_len++] = 0;
            request[request_len++] = 0;
            break;
        case PPP_CCP_METHOD_BSD:
            request[request_len++] = CCP_OPT_BSD;
            request[request_len++] = 3;
            if (ctx->ccp_rx_bsd_bits < 9 || ctx->ccp_rx_bsd_bits > 15)
                ctx->ccp_rx_bsd_bits = 12;
            request[request_len++] = (uint8_t) (0x20 | ctx->ccp_rx_bsd_bits);
            break;
        case PPP_CCP_METHOD_MPPC:
            request[request_len++] = CCP_OPT_MPPE;
            request[request_len++] = 6;
            request[request_len++] = 0;
            request[request_len++] = 0;
            request[request_len++] = 0;
            request[request_len++] = 1;
            break;
        case PPP_CCP_METHOD_DEFLATE:
            request[request_len++] = CCP_OPT_DEFLATE;
            request[request_len++] = 4;
            request[request_len++] = 0x78;
            request[request_len++] = 0;
            break;
        case PPP_CCP_METHOD_NT31RAS:
            request[request_len++] = CCP_OPT_NT31RAS;
            request[request_len++] = 22;
            ccp_put_le_u32(request + request_len, CCP_NT31RAS_FEATURES);
            ccp_put_le_u32(request + request_len + 4, CCP_NT31RAS_FEATURES);
            ccp_put_le_u32(request + request_len + 8, PPP_DEFAULT_MRU);
            ccp_put_le_u32(request + request_len + 12, PPP_DEFAULT_MRU);
            ccp_put_le_u32(request + request_len + 16, 0);
            request_len += 20;
            break;
        default:
            return false;
    }

    request[2] = (uint8_t) (request_len >> 8);
    request[3] = (uint8_t) request_len;
    request[1] = ctx->ccp_id++;
    memcpy(ctx->ccp_request, request, (size_t) request_len);
    ctx->ccp_request_len = request_len;
    ctx->ccp_request_id = request[1];
    ctx->ccp_request_bits = 0;
    ctx->ccp_request_method = method;
    ctx->ccp_req_sent = true;
    ctx->ccp_timeout_ms = 0;
    ctx->ccp_rejected = false;
    ccp_log_packet(ctx, "TX", request, request_len);
    ppp_send_frame(ctx, PPP_PROTO_CCP, request, request_len);
    return true;
}

static bool
ccp_try_next_codec(ppp_ctx_t *ctx)
{
    uint8_t preferred[3];
    int current = -1;

    if (!ctx || ctx->mppe_min_bits > 0 || ctx->ppp_compression == 0)
        return false;

    switch (ctx->ppp_compression) {
        case 1:
            preferred[0] = PPP_CCP_METHOD_LZS_EXTENDED;
            preferred[1] = PPP_CCP_METHOD_MPPC;
            preferred[2] = PPP_CCP_METHOD_PREDICTOR1;
            break;
        case 2:
        case 4:
            preferred[0] = PPP_CCP_METHOD_MPPC;
            preferred[1] = PPP_CCP_METHOD_LZS_EXTENDED;
            preferred[2] = PPP_CCP_METHOD_PREDICTOR1;
            break;
        case 3:
            preferred[0] = PPP_CCP_METHOD_PREDICTOR1;
            preferred[1] = PPP_CCP_METHOD_LZS_EXTENDED;
            preferred[2] = PPP_CCP_METHOD_MPPC;
            break;
        default:
            return false;
    }

    if (ctx->ccp_request_method == PPP_CCP_METHOD_MPPE
        || ctx->ccp_request_method == PPP_CCP_METHOD_DEFLATE) {
        return ccp_send_codec_request(ctx, preferred[0]);
    }

    for (int i = 0; i < 3; i++) {
        if (ctx->ccp_request_method == preferred[i]) {
            current = i;
            break;
        }
    }
    if (current >= 0 && current < 2)
        return ccp_send_codec_request(ctx, preferred[current + 1]);
    return false;
}

static bool
ccp_retry_bsd_nak(ppp_ctx_t *ctx, const uint8_t *pkt, int total)
{
    uint8_t proposed_bits;

    if (ctx->ccp_request_method != PPP_CCP_METHOD_BSD || total != 7
        || pkt[4] != CCP_OPT_BSD || pkt[5] != 3 || (pkt[6] >> 5) != 1)
        return false;
    proposed_bits = pkt[6] & 0x1F;
    if (proposed_bits < 9 || proposed_bits > 15
        || proposed_bits >= ctx->ccp_rx_bsd_bits || ctx->ccp_retries++ >= 10)
        return false;
    ctx->ccp_rx_bsd_bits = proposed_bits;
    return ccp_send_codec_request(ctx, PPP_CCP_METHOD_BSD);
}

static bool
ccp_response_matches_request(const ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    int total;

    if (!ctx->ccp_req_sent || pkt_len < 4 || pkt[1] != ctx->ccp_request_id)
        return false;
    total = (pkt[2] << 8) | pkt[3];
    if (total != pkt_len || total < 4)
        return false;
    return total == ctx->ccp_request_len
        && memcmp(pkt + 4, ctx->ccp_request + 4, (size_t) total - 4) == 0;
}

static bool
ccp_parse_mppe_nak(const uint8_t *pkt, int total, uint32_t *bits)
{
    uint32_t proposed;
    uint32_t key_bits;
    if (total != 10 || pkt[4] != CCP_OPT_MPPE || pkt[5] != 6)
        return false;
    proposed = ccp_get_u32(pkt + 6);
    key_bits = proposed & CCP_MPPE_KEY_BITS;
    if ((proposed & ~(CCP_MPPE_OFFER | CCP_MPPC)) != 0
        || (key_bits != 0 && (key_bits & (key_bits - 1)) != 0)
        || (key_bits == 0 && (proposed & CCP_MPPC) == 0))
        return false;
    *bits = proposed;
    return true;
}

static bool
ccp_retry_nt31ras_nak(ppp_ctx_t *ctx, const uint8_t *pkt, int total)
{
    uint32_t send_features;
    uint32_t recv_features;
    uint32_t old_send_features;
    uint32_t old_recv_features;
    uint32_t max_send;
    uint32_t max_recv;

    if (ctx->ccp_request_method != PPP_CCP_METHOD_NT31RAS || total != 26
        || pkt[4] != CCP_OPT_NT31RAS || pkt[5] != 22
        || ctx->ccp_request_len != 26 || ctx->ccp_retries >= 10)
        return false;

    send_features = ccp_get_le_u32(pkt + 6);
    recv_features = ccp_get_le_u32(pkt + 10);
    max_send = ccp_get_le_u32(pkt + 14);
    max_recv = ccp_get_le_u32(pkt + 18);
    old_send_features = ccp_get_le_u32(ctx->ccp_request + 6);
    old_recv_features = ccp_get_le_u32(ctx->ccp_request + 10);
    if (send_features == 0 || recv_features == 0
        || (send_features & ~old_send_features) != 0
        || (recv_features & ~old_recv_features) != 0
        || (send_features & ~CCP_NT31RAS_FEATURES) != 0
        || (recv_features & ~CCP_NT31RAS_FEATURES) != 0
        || (send_features == old_send_features && recv_features == old_recv_features)
        || max_send < PPP_MIN_MRU || max_send > PPP_MAX_FRAME
        || max_recv < PPP_MIN_MRU || max_recv > PPP_MAX_FRAME)
        return false;

    memcpy(ctx->ccp_request, pkt, (size_t) total);
    ctx->ccp_request[0] = PPP_CODE_CONFIGURE_REQUEST;
    ctx->ccp_request[1] = ctx->ccp_id++;
    ctx->ccp_request_id = ctx->ccp_request[1];
    ctx->ccp_request_len = total;
    ctx->ccp_request_bits = 0;
    ctx->ccp_req_sent = true;
    ctx->ccp_ack_received = false;
    ctx->ccp_retries++;
    ppp_send_frame(ctx, PPP_PROTO_CCP, ctx->ccp_request, total);
    return true;
}

static uint8_t
ccp_key_bits_to_length(uint32_t bits)
{
    if (bits & CCP_MPPE_128)
        return 128;
    if (bits & CCP_MPPE_56)
        return 56;
    return 40;
}

static bool
ccp_request_options_valid(const uint8_t *pkt, int pkt_len)
{
    for (int pos = 4; pos < pkt_len;) {
        if (pos + 2 > pkt_len)
            return false;
        uint8_t option_len = pkt[pos + 1];
        if (option_len < 2 || pos + option_len > pkt_len)
            return false;
        pos += option_len;
    }
    return true;
}

static void
ccp_send_response(ppp_ctx_t *ctx, uint8_t code, uint8_t id,
                  const uint8_t *options, int options_len)
{
    uint8_t response[PPP_MAX_FRAME];
    int total = 4 + options_len;
    response[0] = code;
    response[1] = id;
    response[2] = (uint8_t) (total >> 8);
    response[3] = (uint8_t) total;
    if (options_len > 0)
        memcpy(response + 4, options, options_len);
    ccp_log_packet(ctx, "TX", response, total);
    ppp_send_frame(ctx, PPP_PROTO_CCP, response, total);
}

static void
ccp_handle_config_request(ppp_ctx_t *ctx, const uint8_t *pkt, int total)
{
    uint8_t nak[PPP_MAX_FRAME];
    uint8_t rej[PPP_MAX_FRAME];
    int nak_len = 0;
    int rej_len = 0;
    int pos = 4;
    bool saw_mppe = false;
    bool saw_predictor1 = false;
    bool saw_predictor2 = false;
    bool saw_lzs = false;
    bool saw_lzs_dcp = false;
    bool saw_v44 = false;
    bool saw_bsd = false;
    bool saw_deflate = false;
    bool saw_nt31ras = false;
    uint32_t selected_bits = 0;
    uint32_t selected_tx_window = 0;
    uint8_t selected_tx_bsd_bits = 0;
    uint8_t selected_method = PPP_CCP_METHOD_NONE;
    int selected_mppc_pos = -1;

    ctx->ccp_ack_sent = false;
    ctx->ccp_peer_mppe = false;
    ccp_update_state(ctx);

    if (ctx->ccp_direction == 2 || ctx->ccp_mode == 1) {
        ccp_send_response(ctx, PPP_CODE_CONFIGURE_REJECT, pkt[1], pkt + 4, total - 4);
        return;
    }

    while (pos < total) {
        uint8_t option_len = pkt[pos + 1];
        if (pkt[pos] == CCP_OPT_PREDICTOR1 && option_len == 2 && !saw_predictor1
            && selected_method == PPP_CCP_METHOD_NONE && ctx->mppe_min_bits == 0
            && ctx->ppp_compression != 0) {
            saw_predictor1 = true;
            selected_method = PPP_CCP_METHOD_PREDICTOR1;
        } else if (pkt[pos] == CCP_OPT_PREDICTOR2 && option_len == 2 && !saw_predictor2
                   && selected_method == PPP_CCP_METHOD_NONE && ctx->mppe_min_bits == 0
            && ctx->ppp_compression != 0) {
            saw_predictor2 = true;
            selected_method = PPP_CCP_METHOD_PREDICTOR2;
        } else if (pkt[pos] == CCP_OPT_LZS && option_len == 5 && !saw_lzs) {
            uint16_t history_count = (uint16_t) (((uint16_t) pkt[pos + 2] << 8)
                                                | pkt[pos + 3]);
            uint8_t check_mode = pkt[pos + 4];
            saw_lzs = true;
            /* The peer may list MPPC before LZS regardless of our configured
               preference. For preference 1, keep the LZS offer and reject
               the earlier MPPC option so the peer retries with LZS alone. */
            if (selected_method == PPP_CCP_METHOD_MPPC
                && ctx->ppp_compression == 1 && selected_mppc_pos >= 0) {
                memcpy(rej + rej_len, pkt + selected_mppc_pos, 6);
                rej_len += 6;
                selected_method = PPP_CCP_METHOD_NONE;
                selected_mppc_pos = -1;
            }
            if (selected_method != PPP_CCP_METHOD_NONE || ctx->mppe_min_bits > 0
                || ctx->ppp_compression == 0) {
                memcpy(rej + rej_len, pkt + pos, option_len);
                rej_len += option_len;
            } else if (history_count == 1 && check_mode == 4) {
                selected_method = PPP_CCP_METHOD_LZS_EXTENDED;
            } else if (history_count <= 1 && check_mode <= 3) {
                selected_method = PPP_CCP_METHOD_LZS;
            } else {
                nak[nak_len++] = CCP_OPT_LZS;
                nak[nak_len++] = 5;
                nak[nak_len++] = 0;
                nak[nak_len++] = 1;
                nak[nak_len++] = 3;
            }
        } else if (pkt[pos] == CCP_OPT_LZS_DCP && option_len == 6 && !saw_lzs_dcp) {
            uint16_t history_count = (uint16_t) (((uint16_t) pkt[pos + 2] << 8)
                                                | pkt[pos + 3]);
            uint8_t check_mode = pkt[pos + 4];
            uint8_t process_mode = pkt[pos + 5];
            saw_lzs_dcp = true;
            if (selected_method != PPP_CCP_METHOD_NONE || ctx->mppe_min_bits > 0
                || ctx->ppp_compression == 0) {
                memcpy(rej + rej_len, pkt + pos, option_len);
                rej_len += option_len;
            } else if (history_count != 0 || check_mode != 0 || process_mode != 0) {
                nak[nak_len++] = CCP_OPT_LZS_DCP;
                nak[nak_len++] = 6;
                nak[nak_len++] = 0;
                nak[nak_len++] = 0;
                nak[nak_len++] = 0;
                nak[nak_len++] = 0;
            } else {
                selected_method = PPP_CCP_METHOD_LZS_DCP;
            }
        } else if (pkt[pos] == CCP_OPT_V44 && option_len == 4 && !saw_v44) {
            saw_v44 = true;
            if (selected_method != PPP_CCP_METHOD_NONE || ctx->mppe_min_bits > 0
                || ctx->ppp_compression == 0) {
                memcpy(rej + rej_len, pkt + pos, option_len);
                rej_len += option_len;
            } else if (pkt[pos + 2] != 0 || pkt[pos + 3] != 0) {
                nak[nak_len++] = CCP_OPT_V44;
                nak[nak_len++] = 4;
                nak[nak_len++] = 0;
                nak[nak_len++] = 0;
            } else {
                selected_method = PPP_CCP_METHOD_V44;
            }
        } else if ((pkt[pos] == CCP_OPT_DEFLATE || pkt[pos] == CCP_OPT_DEFLATE_DRAFT)
                   && option_len == 4 && !saw_deflate
                   && pkt[pos + 2] == 0x78 && pkt[pos + 3] == 0
                   && selected_method == PPP_CCP_METHOD_NONE && ctx->mppe_min_bits == 0
            && ctx->ppp_compression != 0) {
            /* The draft option number is accepted as Deflate and echoed back
               unchanged in the Ack; we still request Deflate as option 26. */
            saw_deflate = true;
            selected_method = PPP_CCP_METHOD_DEFLATE;
        } else if (pkt[pos] == CCP_OPT_BSD && option_len == 3 && !saw_bsd) {
            saw_bsd = true;
            uint8_t version = pkt[pos + 2] >> 5;
            uint8_t dictionary_bits = pkt[pos + 2] & 0x1F;
            if (selected_method != PPP_CCP_METHOD_NONE || ctx->mppe_min_bits > 0
                || ctx->ppp_compression == 0
                || version != 1 || dictionary_bits < 9 || dictionary_bits > 16) {
                memcpy(rej + rej_len, pkt + pos, option_len);
                rej_len += option_len;
            } else if (dictionary_bits > 15) {
                nak[nak_len++] = CCP_OPT_BSD;
                nak[nak_len++] = 3;
                nak[nak_len++] = 0x2F;
            } else {
                selected_method = PPP_CCP_METHOD_BSD;
                selected_tx_bsd_bits = dictionary_bits;
            }
        } else if (pkt[pos] == CCP_OPT_NT31RAS && option_len == 22 && !saw_nt31ras
                   && selected_method == PPP_CCP_METHOD_NONE && ctx->mppe_min_bits == 0
            && ctx->ppp_compression != 0) {
            uint32_t send_features = ccp_get_le_u32(pkt + pos + 2);
            uint32_t recv_features = ccp_get_le_u32(pkt + pos + 6);
            uint32_t max_send = ccp_get_le_u32(pkt + pos + 10);
            uint32_t max_recv = ccp_get_le_u32(pkt + pos + 14);
            saw_nt31ras = true;
            if ((send_features & CCP_NT31RAS_FEATURES) != 0
                && (send_features & ~CCP_NT31RAS_FEATURES) == 0
                && (recv_features & CCP_NT31RAS_FEATURES) != 0
                && (recv_features & ~CCP_NT31RAS_FEATURES) == 0
                && max_send >= 128 && max_send <= PPP_MAX_FRAME
                && max_recv >= 128 && max_recv <= PPP_MAX_FRAME) {
                selected_method = PPP_CCP_METHOD_NT31RAS;
                selected_tx_window = ccp_nt31ras_window_size(recv_features);
            } else {
                memcpy(rej + rej_len, pkt + pos, option_len);
                rej_len += option_len;
            }
        } else if (pkt[pos] == CCP_OPT_MPPE && option_len == 6 && !saw_mppe) {
            uint32_t bits = ccp_get_u32(pkt + pos + 2);
            uint32_t key_bits = bits & CCP_MPPE_KEY_BITS;
            uint32_t allowed_bits = ctx->mppe_allowed_bits & CCP_MPPE_KEY_BITS;
            uint32_t offered_bits = key_bits & allowed_bits;
            uint32_t preferred_key = (offered_bits & CCP_MPPE_128) ? CCP_MPPE_128
                                   : (offered_bits & CCP_MPPE_56) ? CCP_MPPE_56
                                   : (offered_bits & CCP_MPPE_40) ? CCP_MPPE_40 : 0;
            uint32_t required_key = ctx->mppe_prefer_strongest
                                  ? preferred_key : ccp_minimum_key_bit(ctx->mppe_min_bits);
            uint32_t state_flag = bits & CCP_MPPE_STATELESS;
            /* The Require modes insist. The Auto-Negotiate modes state their preference
               once, then follow the peer. */
            bool want_stateless = ctx->mppe_key_state_mode == 2
                               || (ctx->mppe_key_state_mode == 4 && !ctx->mppe_state_nak_sent);
            bool want_stateful  = ctx->mppe_key_state_mode == 3
                               || (ctx->mppe_key_state_mode == 5 && !ctx->mppe_state_nak_sent);
            bool state_mismatch = (want_stateless && !state_flag) || (want_stateful && state_flag);
            /* A peer that keeps the obsolete D bit after being asked to clear it is
               following the drafts, so stop insisting. */
            bool d_bit_refused = (bits & CCP_MPPE_D) != 0
                              && ctx->mppe_format != PPP_MPPE_FORMAT_LEGACY_DRAFT
                              && !(ctx->mppe_format == PPP_MPPE_FORMAT_AUTOMATIC
                                   && ctx->mppe_d_bit_nak_sent);
            saw_mppe = true;
            if ((bits & ~(CCP_MPPC | CCP_MPPE_OFFER | CCP_MPPE_D)) != 0
                || (key_bits == 0 && bits != CCP_MPPC)) {
                memcpy(rej + rej_len, pkt + pos, option_len);
                rej_len += option_len;
            } else if (key_bits == 0) {
                if (selected_method == PPP_CCP_METHOD_NONE && ctx->mppe_min_bits == 0
                    && ctx->ccp_direction != 2 && ctx->ppp_compression != 0) {
                    selected_method = PPP_CCP_METHOD_MPPC;
                    selected_mppc_pos = pos;
                } else {
                    memcpy(rej + rej_len, pkt + pos, option_len);
                    rej_len += option_len;
                }
            } else if (!ctx->mppe_keys_ready || offered_bits == 0) {
                memcpy(rej + rej_len, pkt + pos, option_len);
                rej_len += option_len;
            } else if (ctx->ppp_compression == 0 && (bits & CCP_MPPC)) {
                uint32_t suggested = bits & ~CCP_MPPC;
                nak[nak_len++] = CCP_OPT_MPPE;
                nak[nak_len++] = 6;
                ccp_put_u32(nak + nak_len, suggested);
                nak_len += 4;
            } else if (ctx->mppe_min_bits > 0
                       && (key_bits & required_key) == 0) {
                uint32_t suggested = (bits & (CCP_MPPE_STATELESS | CCP_MPPC)) | required_key;
                nak[nak_len++] = CCP_OPT_MPPE;
                nak[nak_len++] = 6;
                ccp_put_u32(nak + nak_len, suggested);
                nak_len += 4;
            } else if (d_bit_refused
                       || key_bits != preferred_key || (key_bits & (key_bits - 1)) != 0
                       || state_mismatch) {
                uint32_t chosen = ctx->mppe_min_bits > 0 ? required_key : preferred_key;
                uint32_t suggested = (bits & CCP_MPPC) | chosen;
                if (want_stateless || (ctx->mppe_key_state_mode == 0 && state_flag))
                    suggested |= CCP_MPPE_STATELESS;
                else if (want_stateful || (ctx->mppe_key_state_mode == 1 && !state_flag))
                    suggested &= ~CCP_MPPE_STATELESS;
                else
                    suggested |= state_flag;
                if (state_mismatch)
                    ctx->mppe_state_nak_sent = true;
                if (d_bit_refused)
                    ctx->mppe_d_bit_nak_sent = true;
                nak[nak_len++] = CCP_OPT_MPPE;
                nak[nak_len++] = 6;
                ccp_put_u32(nak + nak_len, suggested);
                nak_len += 4;
            } else if (ccp_key_bits_to_length(key_bits) < ctx->mppe_min_bits) {
                uint32_t suggested = (bits & (CCP_MPPE_STATELESS | CCP_MPPC)) | required_key;
                nak[nak_len++] = CCP_OPT_MPPE;
                nak[nak_len++] = 6;
                ccp_put_u32(nak + nak_len, suggested);
                nak_len += 4;
            } else if (selected_method == PPP_CCP_METHOD_NONE) {
                selected_bits = state_flag | key_bits;
                selected_method = (bits & CCP_MPPC) ? PPP_CCP_METHOD_MPPC : PPP_CCP_METHOD_MPPE;
            } else {
                memcpy(rej + rej_len, pkt + pos, option_len);
                rej_len += option_len;
            }
        } else {
            memcpy(rej + rej_len, pkt + pos, option_len);
            rej_len += option_len;
        }
        pos += option_len;
    }

    if (ctx->mppe_min_bits > 0 && !saw_mppe) {
        ctx->state = PPP_STATE_DEAD;
        return;
    }

    if (rej_len > 0) {
        ccp_send_response(ctx, PPP_CODE_CONFIGURE_REJECT, pkt[1], rej, rej_len);
    } else if (nak_len > 0) {
        ccp_send_response(ctx, PPP_CODE_CONFIGURE_NAK, pkt[1], nak, nak_len);
    } else {
        ccp_send_response(ctx, PPP_CODE_CONFIGURE_ACK, pkt[1], pkt + 4, total - 4);
        ctx->ccp_ack_sent = true;
        ctx->ccp_peer_mppe = selected_bits != 0;
        ctx->ccp_tx_bits = selected_bits;
        if (selected_method == PPP_CCP_METHOD_BSD)
            ctx->ccp_tx_bsd_bits = selected_tx_bsd_bits;
        uint16_t lzs_history_count = 1;
        uint8_t lzs_check_mode = 3;
        if (selected_method == PPP_CCP_METHOD_LZS) {
            for (int option_pos = 4; option_pos < total; option_pos += pkt[option_pos + 1]) {
                if (pkt[option_pos] == CCP_OPT_LZS) {
                    lzs_history_count = (uint16_t) (((uint16_t) pkt[option_pos + 2] << 8)
                                                  | pkt[option_pos + 3]);
                    lzs_check_mode = pkt[option_pos + 4];
                    break;
                }
            }
        }
        if (selected_method == PPP_CCP_METHOD_LZS
            ? !ppp_ccp_codec_set_lzs(ctx, true, selected_method,
                                     lzs_history_count, lzs_check_mode)
            : selected_method != PPP_CCP_METHOD_MPPE
              && !ppp_ccp_codec_set_window(ctx, true, selected_method,
                                           selected_method == PPP_CCP_METHOD_NT31RAS
                                               ? selected_tx_window : CCP_NT31RAS_WINDOW_8K)) {
            ctx->state = PPP_STATE_DEAD;
            return;
        }
        if (selected_method == PPP_CCP_METHOD_NT31RAS)
            ctx->ccp_tx_window_size = selected_tx_window;
        if (selected_method == PPP_CCP_METHOD_MPPE)
            ppp_ccp_codec_set(ctx, true, PPP_CCP_METHOD_NONE);
        if (selected_bits != 0)
            ppp_mppe_configure(&ctx->mppe_tx, ccp_key_bits_to_length(selected_bits),
                               !(selected_bits & CCP_MPPE_STATELESS));
        ccp_update_state(ctx);
        /* An empty Configure-Request means the peer has no CCP options left
           after our Configure-Reject. Treat that as a completed negotiation
           without compression when MPPE is optional, so IPCP can enter the
           network phase instead of waiting for CCP to open. */
        if (total == 4 && selected_method == PPP_CCP_METHOD_NONE
            && selected_bits == 0 && ctx->mppe_min_bits == 0
            && !ctx->ccp_req_sent) {
            ppp_ccp_fallback_plaintext(ctx);
        }
        ppp_advance_state(ctx);
    }
}

void
ppp_ccp_start(ppp_ctx_t *ctx)
{
    if (!ctx)
        return;
    ppp_ccp_codec_close(ctx);
    ctx->ccp_ack_sent = false;
    ctx->ccp_ack_received = false;
    ctx->ccp_peer_mppe = false;
    ctx->ccp_open = false;
    ctx->ccp_rejected = false;
    ctx->ccp_plaintext_fallback = false;
    ctx->ccp_retries = 0;
    ctx->ccp_timeout_ms = 0;
    ctx->ccp_timeout_retries = 0;
    ctx->ccp_req_sent = false;
    ctx->ccp_reset_pending = false;
    ctx->ccp_tx_bsd_bits = 0;
    ctx->ccp_rx_bsd_bits = 0;
    ctx->mppe_tx_enabled = false;
    ctx->mppe_rx_enabled = false;
    ctx->mppe_state_nak_sent = false;
    ctx->mppe_d_bit_nak_sent = false;
    ctx->mppe_56_retried = false;
    ctx->ras_tx_flush_pending = false;
    ctx->ccp_tx_window_size = CCP_NT31RAS_WINDOW_8K;
    ctx->ccp_rx_window_size = CCP_NT31RAS_WINDOW_8K;
    ctx->ras_tx_ticket_base = 0;
    ctx->ras_tx_ticket_next = 0;
    ctx->ras_rx_ticket_base = 0;
    ctx->ras_rx_ticket_next = 0;
    ctx->ras_rx_len = 0;
    ctx->ras_rx_expected = 0;
    ctx->ras_rx_in_frame = false;
    if (ctx->ccp_mode == 1) {
        if (ctx->mppe_min_bits > 0)
            ctx->state = PPP_STATE_DEAD;
        else {
            ctx->ccp_plaintext_fallback = true;
            ccp_update_state(ctx);
        }
        return;
    }
    if (ctx->ccp_mode == 2 || ctx->ccp_direction == 1) {
        if (ctx->mppe_min_bits > 0 && ctx->ccp_direction != 1) {
            ctx->state = PPP_STATE_DEAD;
            return;
        }
        return;
    }
    if (ctx->mppe_keys_ready && (ctx->mppe_allowed_bits & CCP_MPPE_KEY_BITS) != 0) {
        uint32_t key_bits = ctx->mppe_allowed_bits & CCP_MPPE_KEY_BITS;
        uint32_t state_flag = ctx->mppe_key_state_mode == 1
                            || ctx->mppe_key_state_mode == 3
                            || ctx->mppe_key_state_mode == 5 ? 0 : CCP_MPPE_STATELESS;
        ccp_send_request(ctx, (ctx->ppp_compression != 0 ? CCP_MPPC : 0)
                              | state_flag | key_bits);
    } else if (ctx->mppe_min_bits > 0) {
        ctx->state = PPP_STATE_DEAD;
    } else if (ctx->ccp_mode != 2 && ctx->ccp_direction != 1) {
        switch (ctx->ppp_compression) {
            case 0:
                break;
            case 1:
                ccp_send_codec_request(ctx, PPP_CCP_METHOD_LZS_EXTENDED);
                break;
            case 2:
                ccp_send_codec_request(ctx, PPP_CCP_METHOD_MPPC);
                break;
            case 3:
                ccp_send_codec_request(ctx, PPP_CCP_METHOD_PREDICTOR1);
                break;
            case 4:
                ccp_send_codec_request(ctx, PPP_CCP_METHOD_MPPC);
                break;
            default:
                ccp_send_codec_request(ctx, PPP_CCP_METHOD_DEFLATE);
                break;
        }
    }
}

void
ppp_ccp_retry_mppc_without_mppe(ppp_ctx_t *ctx)
{
    ctx->ccp_ack_received = false;
    ctx->ccp_retries = 0;
    ctx->ccp_timeout_retries = 0;
    ppp_ccp_codec_set(ctx, false, PPP_CCP_METHOD_NONE);
    ccp_update_state(ctx);
    ccp_send_request(ctx, CCP_MPPC);
}

void
ppp_ccp_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    if (!ctx || !pkt || pkt_len < 4)
        return;
    int total = (pkt[2] << 8) | pkt[3];
    if (total < 4 || total != pkt_len)
        return;
    ccp_log_packet(ctx, "RX", pkt, total);

    switch (pkt[0]) {
        case PPP_CODE_CONFIGURE_REQUEST:
            if (ccp_request_options_valid(pkt, total))
                ccp_handle_config_request(ctx, pkt, total);
            break;

        case PPP_CODE_CONFIGURE_ACK:
            if (ccp_response_matches_request(ctx, pkt, total)) {
                ctx->ccp_req_sent = false;
                ctx->ccp_ack_received = true;
                if ((ctx->ccp_request_bits & CCP_MPPE_KEY_BITS) != 0) {
                    ppp_mppe_configure(&ctx->mppe_rx,
                                       ccp_key_bits_to_length(ctx->ccp_request_bits),
                                       !(ctx->ccp_request_bits & CCP_MPPE_STATELESS));
                    if (ctx->ccp_request_bits & CCP_MPPC)
                        ppp_ccp_codec_set(ctx, false, PPP_CCP_METHOD_MPPC);
                    else
                        ppp_ccp_codec_set(ctx, false, PPP_CCP_METHOD_NONE);
                } else if (ctx->ccp_request_bits & CCP_MPPC) {
                    if (!ppp_ccp_codec_set(ctx, false, PPP_CCP_METHOD_MPPC)) {
                        ctx->state = PPP_STATE_DEAD;
                        break;
                    }
                } else if (ctx->ccp_request_method == PPP_CCP_METHOD_NT31RAS) {
                    uint32_t recv_features = ccp_get_le_u32(ctx->ccp_request + 10);
                    ctx->ccp_rx_window_size = ccp_nt31ras_window_size(recv_features);
                    if (!ctx->ccp_rx_window_size
                        || !ppp_ccp_codec_set_window(ctx, false,
                                                     ctx->ccp_request_method,
                                                     ctx->ccp_rx_window_size)) {
                        ctx->state = PPP_STATE_DEAD;
                        break;
                    }
                } else if (ctx->ccp_request_method == PPP_CCP_METHOD_BSD) {
                    ctx->ccp_rx_bsd_bits = ctx->ccp_request[6] & 0x1F;
                    if (!ppp_ccp_codec_set_window(ctx, false,
                                                  ctx->ccp_request_method,
                                                  CCP_NT31RAS_WINDOW_8K)) {
                        ctx->state = PPP_STATE_DEAD;
                        break;
                    }
                } else if (ctx->ccp_request_method == PPP_CCP_METHOD_LZS
                           || ctx->ccp_request_method == PPP_CCP_METHOD_LZS_EXTENDED) {
                    uint16_t history_count = (uint16_t) (((uint16_t) ctx->ccp_request[6] << 8)
                                                        | ctx->ccp_request[7]);
                    uint8_t check_mode = ctx->ccp_request[8];
                    if (!ppp_ccp_codec_set_lzs(ctx, false, ctx->ccp_request_method,
                                               history_count, check_mode)) {
                        ctx->state = PPP_STATE_DEAD;
                        break;
                    }
                } else if (!ppp_ccp_codec_set(ctx, false, ctx->ccp_request_method)) {
                    ctx->state = PPP_STATE_DEAD;
                    break;
                }
                ccp_update_state(ctx);
                ppp_advance_state(ctx);
            }
            break;

        case PPP_CODE_CONFIGURE_NAK:
            {
                uint32_t proposed_bits;
                if (ctx->ccp_req_sent && pkt[1] == ctx->ccp_request_id
                    && ccp_parse_mppe_nak(pkt, total, &proposed_bits)
                    && proposed_bits != ctx->ccp_request_bits
                    /* A Nak may only narrow our offer, except that the peer may ask
                       for stateless; the Require Stateful check below still refuses it. */
                    && (proposed_bits & ~(ctx->ccp_request_bits | CCP_MPPE_STATELESS)) == 0
                    && ((ctx->mppe_min_bits == 0)
                        || ccp_key_bits_to_length(proposed_bits) >= ctx->mppe_min_bits)
                    && !((ctx->mppe_key_state_mode == 2
                          && !(proposed_bits & CCP_MPPE_STATELESS))
                         || (ctx->mppe_key_state_mode == 3
                             && (proposed_bits & CCP_MPPE_STATELESS)))
                    && (((proposed_bits & CCP_MPPE_KEY_BITS) == 0
                         && ctx->mppe_min_bits == 0)
                        || ((proposed_bits & CCP_MPPE_KEY_BITS) != 0
                            && ccp_key_bits_to_length(proposed_bits) >= ctx->mppe_min_bits))
                    && ctx->ccp_retries++ < 10) {
                    ctx->ccp_req_sent = false;
                    ctx->ccp_ack_received = false;
                    ccp_send_request(ctx, proposed_bits);
                } else if (ctx->ccp_req_sent && pkt[1] == ctx->ccp_request_id
                           && ccp_retry_bsd_nak(ctx, pkt, total)) {
                    break;
                } else if (ctx->ccp_req_sent && pkt[1] == ctx->ccp_request_id
                           && ccp_retry_nt31ras_nak(ctx, pkt, total)) {
                    break;
                } else if (ctx->ccp_req_sent && pkt[1] == ctx->ccp_request_id
                           && ccp_try_next_codec(ctx)) {
                    break;
                } else if (ctx->ccp_req_sent && pkt[1] == ctx->ccp_request_id) {
                    ccp_request_failed(ctx);
                    ppp_advance_state(ctx);
                }
            }
            break;

        case PPP_CODE_CONFIGURE_REJECT:
            if (ccp_response_matches_request(ctx, pkt, total)) {
                /* Draft-era peers know nothing of the 56-bit flag and may refuse the
                   whole option because of it, so try once more without it. */
                if (ctx->mppe_format == PPP_MPPE_FORMAT_AUTOMATIC && !ctx->mppe_56_retried
                    && ctx->ccp_request_method == PPP_CCP_METHOD_MPPE
                    && (ctx->ccp_request_bits & CCP_MPPE_56)
                    && (ctx->ccp_request_bits & (CCP_MPPE_40 | CCP_MPPE_128))) {
                    ctx->mppe_56_retried = true;
                    ctx->ccp_ack_received = false;
                    ctx->mppe_allowed_bits &= ~CCP_MPPE_56;
                    ccp_send_request(ctx, ctx->ccp_request_bits & ~CCP_MPPE_56);
                    break;
                }
                ctx->ccp_rejected = true;
                if (!ccp_try_next_codec(ctx)) {
                    ccp_request_failed(ctx);
                    ppp_advance_state(ctx);
                }
            }
            break;

        case PPP_CODE_TERMINATE_REQUEST:
            ccp_send_response(ctx, PPP_CODE_TERMINATE_ACK, pkt[1], NULL, 0);
            ctx->ccp_ack_sent = false;
            ctx->ccp_ack_received = false;
            ctx->ccp_peer_mppe = false;
            ppp_ccp_fallback_plaintext(ctx);
            ppp_advance_state(ctx);
            break;

        case PPP_CODE_TERMINATE_ACK:
            ctx->ccp_ack_sent = false;
            ctx->ccp_ack_received = false;
            ctx->ccp_peer_mppe = false;
            ppp_ccp_fallback_plaintext(ctx);
            ppp_advance_state(ctx);
            break;

        case PPP_CODE_RESET_REQUEST:
            if (ctx->ccp_tx_method == PPP_CCP_METHOD_LZS_EXTENDED) {
                ppp_ccp_codec_flush(ctx, true);
            } else if (ctx->ccp_tx_method == PPP_CCP_METHOD_LZS && total == 6
                && pkt[4] == 0 && pkt[5] == 1) {
                static const uint8_t lzs_history[] = { 0, 1 };
                ppp_ccp_codec_flush(ctx, true);
                ccp_send_response(ctx, PPP_CODE_RESET_ACK, pkt[1], lzs_history,
                                  sizeof(lzs_history));
            } else {
                ccp_send_response(ctx, PPP_CODE_RESET_ACK, pkt[1], NULL, 0);
            }
            ppp_mppe_request_reset(&ctx->mppe_tx);
            if (ctx->ccp_tx_method != PPP_CCP_METHOD_NONE
                && ctx->ccp_tx_method != PPP_CCP_METHOD_LZS_EXTENDED
                && ctx->ccp_tx_method != PPP_CCP_METHOD_LZS)
                ppp_ccp_codec_set_window(ctx, true, ctx->ccp_tx_method,
                                         ctx->ccp_tx_window_size
                                             ? ctx->ccp_tx_window_size
                                             : CCP_NT31RAS_WINDOW_8K);
            break;

        case PPP_CODE_RESET_ACK:
            if (ctx->ccp_rx_method == PPP_CCP_METHOD_LZS
                && total == 6 && ctx->ccp_reset_pending
                && pkt[1] == ctx->ccp_reset_request_id
                && pkt[4] == 0 && pkt[5] == 1) {
                ctx->ccp_reset_pending = false;
                ppp_ccp_codec_flush(ctx, false);
            } else if (ctx->ccp_rx_method != PPP_CCP_METHOD_LZS_EXTENDED
                       && total == 4 && ctx->ccp_reset_pending
                       && pkt[1] == ctx->ccp_reset_request_id) {
                ctx->ccp_reset_pending = false;
                if (ctx->ccp_rx_method != PPP_CCP_METHOD_NONE)
                    ppp_ccp_codec_set_window(ctx, false, ctx->ccp_rx_method,
                                             ctx->ccp_rx_window_size
                                                 ? ctx->ccp_rx_window_size
                                                 : CCP_NT31RAS_WINDOW_8K);
            }
            break;

        case PPP_CODE_CODE_REJECT:
            break;

        default:
            ppp_send_code_reject(ctx, PPP_PROTO_CCP, pkt, pkt_len);
            break;
    }
}

void
ppp_ccp_fallback_plaintext(ppp_ctx_t *ctx)
{
    if (!ctx)
        return;
    ctx->ccp_req_sent = false;
    ctx->ccp_ack_sent = false;
    ctx->ccp_ack_received = false;
    ctx->ccp_peer_mppe = false;
    ctx->ccp_open = false;
    ctx->ccp_plaintext_fallback = true;
    ctx->mppe_tx_enabled = false;
    ctx->mppe_rx_enabled = false;
    ctx->ccp_reset_pending = false;
    ctx->ras_rx_len = 0;
    ctx->ras_rx_expected = 0;
    ctx->ras_rx_in_frame = false;
    ppp_ccp_codec_close(ctx);
    if (ctx->mppe_min_bits > 0) {
        ctx->ccp_plaintext_fallback = false;
        ctx->state = PPP_STATE_DEAD;
    }
    ccp_log_state(ctx, "plaintext fallback");
}

/* Restart timer expiry (RFC 1661 TO+): the peer did not answer our
   Configure-Request, so send it again with a fresh identifier. */
void
ppp_ccp_timeout(ppp_ctx_t *ctx)
{
    uint8_t request[sizeof(ctx->ccp_request)];
    int     len;

    if (!ctx || !ctx->ccp_req_sent || ctx->ccp_request_len < 4
        || ctx->ccp_request_len > sizeof(request))
        return;

    if (ctx->ccp_timeout_retries++ >= 10) {
        if (ctx->mppe_min_bits > 0) {
            ctx->state = PPP_STATE_DEAD;
            return;
        }
        /* Give up. Unless asymmetric mode keeps the transmit side, tell the peer
           so it stops compressing toward us. */
        if (ctx->ccp_ack_sent
            && ctx->ccp_direction != PPP_CCP_DIRECTION_ASYMMETRIC_PREFERRED)
            ccp_send_response(ctx, PPP_CODE_TERMINATE_REQUEST, ctx->ccp_id++, NULL, 0);
        ccp_request_failed(ctx);
        ppp_advance_state(ctx);
        return;
    }

    len = ctx->ccp_request_len;
    memcpy(request, ctx->ccp_request, (size_t) len);
    request[1] = ctx->ccp_id++;
    memcpy(ctx->ccp_request, request, (size_t) len);
    ctx->ccp_request_id = request[1];
    ctx->ccp_timeout_ms = 0;
    ccp_log_packet(ctx, "TX", request, len);
    ppp_send_frame(ctx, PPP_PROTO_CCP, request, len);
}
