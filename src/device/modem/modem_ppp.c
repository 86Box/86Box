/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PPP (Point-to-Point Protocol) implementation for modem emulation.
 *          HDLC-like and NT31 RAS framing, FCS-16 (RFC 1662), LCP (RFC 1661), PAP,
 *          CHAP, EAP, IPCP, CCP codecs (Stac LZS, MPPC, Deflate, BSD-Compress, Predictor),
 *          MPPE, and Van Jacobson compression.
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
#include <stdlib.h>
#include <string.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_mppe.h>
#include <86box/modem/modem_pap.h>
#include <86box/modem/modem_chap.h>
#include <86box/modem/modem_eap.h>
#include <86box/modem/modem_ipcp.h>
#include <86box/modem/modem_ipv6cp.h>
#include <86box/modem/modem_nbfcp.h>
#include <86box/modem/modem_ipxcp.h>
#include <86box/modem/modem_cslip.h>
#include <86box/modem/modem_mppp.h>
#include <86box/modem/modem_lapb.h>
#include <86box/log.h>
#include <86box/modem/modem_debug.h>
#ifdef _WIN32
#    include <windows.h>
#    include <bcrypt.h>
#endif

#define PPP_LCP_TIMEOUT_MS 3000
#define PPP_LCP_MAX_RETRIES 10
#define PPP_AUTH_TIMEOUT_MS 30000
#define PPP_KEEPALIVE_MAX_MISSED 4
#define PPP_LAPB_T1_MS          3000 /* RFC 1663 T1; the virtual link is far faster than a real modem */
#define PPP_LAPB_N2             10
#define PPP_LAPB_TAKEOVER_MS    (2 * PPP_LAPB_T1_MS) /* larger-Magic side establishes if the peer never does */
#define PPP_NM_DM_HOLDOFF_MS    1000
#define PPP_SDP_DEFAULT_MAX     8
#define PPP_LQM_DEFAULT_PERIOD_CS 3000 /* 30 seconds */
#define PPP_LQM_MIN_PERIOD_CS     50   /* never report faster than every 500 ms */
#define PPP_LQR_LENGTH            48
#define PPP_CBCP_TIMEOUT_MS     3000
#define PPP_CBCP_MAX_RETRIES    3
#define PPP_CBCP_HOLD_MS        500
#define CBCP_CODE_REQUEST       1
#define CBCP_CODE_RESPONSE      2
#define CBCP_CODE_ACK           3
#define CBCP_TYPE_NONE          1
#define CBCP_TYPE_USER          2

struct ppp_mppp_bundle_t {
    char                 group[64];
    ppp_auth_type_t      auth_type;
    char                 username[64];
    uint8_t              peer_endpoint[32];
    uint8_t              peer_endpoint_length;
    uint16_t             rx_mrru;
    uint16_t             tx_mrru;
    bool                 rx_short_sequence;
    bool                 tx_short_sequence;
    ppp_ctx_t           *owner;
    ppp_ctx_t           *links[PPP_MPPP_MAX_LINKS];
    size_t               link_count;
    ppp_mppp_sender_t    sender;
    ppp_mppp_reassembler_t reassembler;
    struct ppp_mppp_bundle_t *next;
};

static struct ppp_mppp_bundle_t *ppp_mppp_bundles;
static void ppp_process_one(ppp_ctx_t *ctx, const uint8_t *frame, int frame_len, bool reassembled);
static void ppp_process_frame(ppp_ctx_t *ctx, const uint8_t *frame, int frame_len,
                              bool reassembled);

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
ppp_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ppp_log(priv, fmt, ...)
#endif

static bool
ppp_mppp_send_fragment(void *opaque, const uint8_t *fragment, size_t length)
{
    ppp_ctx_t *ctx = (ppp_ctx_t *) opaque;

    if (!ctx || ctx->state == PPP_STATE_DEAD || length > PPP_MAX_FRAME - 6)
        return false;

    ppp_send_frame(ctx, PPP_PROTO_MULTILINK, fragment, (int) length);
    return true;
}

static void
ppp_mppp_deliver_packet(void *opaque, const uint8_t *packet, size_t length)
{
    struct ppp_mppp_bundle_t *bundle = (struct ppp_mppp_bundle_t *) opaque;
    uint8_t frame[PPP_MAX_FRAME + 2];

    if (!bundle || !bundle->owner || length < 1 || length > sizeof(frame) - 2)
        return;
    if ((packet[0] & 1) ? !bundle->owner->our_pfc : length < 2)
        return;

    frame[0] = PPP_ADDRESS;
    frame[1] = PPP_CONTROL;
    memcpy(frame + 2, packet, length);
    ppp_process_frame(bundle->owner, frame, (int) length + 2, true);
}

static void
ppp_mppp_bundle_free(struct ppp_mppp_bundle_t *bundle)
{
    if (!bundle)
        return;

    ppp_mppp_reassembler_close(&bundle->reassembler);
    free(bundle);
}

static bool
ppp_multilink_join(ppp_ctx_t *ctx)
{
    struct ppp_mppp_bundle_t *bundle = ppp_mppp_bundles;
    size_t header_length;
    size_t max_fragment_payload;
    bool new_bundle = false;

    if (!ctx || !ctx->multilink_group[0]
        || !ctx->multilink_our_mrru || !ctx->multilink_peer_mrru)
        return false;

    header_length = ppp_mppp_header_size(ctx->multilink_peer_short_sequence);
    if (ctx->peer_mru <= header_length + 2) {
        ppp_log(ctx->log, "PPP: Physical MRU is too small for Multilink headers\n");
        ctx->state = PPP_STATE_DEAD;
        return false;
    }
    max_fragment_payload = ctx->peer_mru - header_length - 2;
    if (max_fragment_payload > PPP_MAX_FRAME - 6 - header_length)
        max_fragment_payload = PPP_MAX_FRAME - 6 - header_length;

    while (bundle && strcmp(bundle->group, ctx->multilink_group) != 0)
        bundle = bundle->next;

    if (bundle) {
        if (bundle->link_count >= PPP_MPPP_MAX_LINKS
            || bundle->auth_type != ctx->auth_type
            || strcmp(bundle->username, ctx->username) != 0
            || bundle->peer_endpoint_length != ctx->multilink_peer_endpoint_length
            || memcmp(bundle->peer_endpoint, ctx->multilink_peer_endpoint_data,
                      bundle->peer_endpoint_length) != 0
            || bundle->rx_mrru != ctx->multilink_our_mrru_value
            || bundle->tx_mrru != ctx->multilink_peer_mrru_value
            || bundle->rx_short_sequence != ctx->multilink_our_short_sequence
            || bundle->tx_short_sequence != ctx->multilink_peer_short_sequence) {
            ppp_log(ctx->log, "PPP: Multilink link does not match the active bundle\n");
            ctx->state = PPP_STATE_DEAD;
            return false;
        }
    } else {
        bundle = (struct ppp_mppp_bundle_t *) calloc(1, sizeof(*bundle));
        if (!bundle) {
            ppp_log(ctx->log, "PPP: Could not allocate Multilink bundle\n");
            ctx->state = PPP_STATE_DEAD;
            return false;
        }
        new_bundle = true;

        memcpy(bundle->group, ctx->multilink_group, sizeof(bundle->group));
        bundle->auth_type = ctx->auth_type;
        memcpy(bundle->username, ctx->username, sizeof(bundle->username));
        bundle->peer_endpoint_length = ctx->multilink_peer_endpoint_length;
        memcpy(bundle->peer_endpoint, ctx->multilink_peer_endpoint_data,
               bundle->peer_endpoint_length);
        bundle->rx_mrru = ctx->multilink_our_mrru_value;
        bundle->tx_mrru = ctx->multilink_peer_mrru_value;
        bundle->rx_short_sequence = ctx->multilink_our_short_sequence;
        bundle->tx_short_sequence = ctx->multilink_peer_short_sequence;

        if (!ppp_mppp_sender_init(&bundle->sender, bundle->tx_mrru,
                                  bundle->tx_short_sequence)
            || !ppp_mppp_reassembler_init(&bundle->reassembler, bundle->rx_mrru,
                                         bundle->rx_short_sequence)) {
            ppp_mppp_bundle_free(bundle);
            ctx->state = PPP_STATE_DEAD;
            return false;
        }

        bundle->owner = ctx;
    }

    if (!ppp_mppp_sender_add_link(&bundle->sender, ppp_mppp_send_fragment, ctx,
                                  max_fragment_payload)) {
        ppp_log(ctx->log, "PPP: Could not attach physical link to Multilink bundle\n");
        if (new_bundle)
            ppp_mppp_bundle_free(bundle);
        ctx->state = PPP_STATE_DEAD;
        return false;
    }

    bundle->links[bundle->link_count++] = ctx;
    ctx->multilink_bundle = bundle;
    if (new_bundle) {
        bundle->next = ppp_mppp_bundles;
        ppp_mppp_bundles = bundle;
    }
    ppp_log(ctx->log, "PPP: Joined Multilink bundle '%s' with %u link(s)\n",
            bundle->group, (unsigned) bundle->link_count);
    return true;
}

static void
ppp_multilink_detach(ppp_ctx_t *ctx)
{
    struct ppp_mppp_bundle_t *bundle;
    struct ppp_mppp_bundle_t **bundle_pos;

    if (!ctx || !ctx->multilink_bundle)
        return;

    bundle = ctx->multilink_bundle;
    ctx->multilink_bundle = NULL;
    ppp_mppp_sender_remove_link(&bundle->sender, ctx);

    for (size_t index = 0; index < bundle->link_count; index++) {
        if (bundle->links[index] != ctx)
            continue;
        bundle->link_count--;
        if (index != bundle->link_count)
            bundle->links[index] = bundle->links[bundle->link_count];
        bundle->links[bundle->link_count] = NULL;
        break;
    }

    if (bundle->owner == ctx) {
        for (size_t index = 0; index < bundle->link_count; index++) {
            bundle->links[index]->multilink_bundle = NULL;
            bundle->links[index]->state = PPP_STATE_DEAD;
        }
        bundle->link_count = 0;
    }

    if (bundle->link_count != 0) {
        ppp_mppp_reassembler_reset(&bundle->reassembler);
        return;
    }

    for (bundle_pos = &ppp_mppp_bundles; *bundle_pos && *bundle_pos != bundle;
         bundle_pos = &(*bundle_pos)->next)
        ;
    if (*bundle_pos)
        *bundle_pos = bundle->next;
    ppp_mppp_bundle_free(bundle);
}

void
ppp_multilink_configure(ppp_ctx_t *ctx, const char *group)
{
    if (!ctx)
        return;

    ctx->multilink_group[0] = '\0';
    if (group && group[0]) {
        strncpy(ctx->multilink_group, group, sizeof(ctx->multilink_group) - 1);
        ctx->multilink_group[sizeof(ctx->multilink_group) - 1] = '\0';
    }
}

bool
ppp_multilink_is_member(const ppp_ctx_t *ctx)
{
    return ctx && ctx->multilink_bundle;
}

bool
ppp_multilink_is_owner(const ppp_ctx_t *ctx)
{
    return ctx && (!ctx->multilink_bundle || ctx->multilink_bundle->owner == ctx);
}

#ifdef ENABLE_MODEM_LOG
static const char *
ppp_auth_name(ppp_auth_type_t auth_type)
{
    switch (auth_type) {
        case PPP_AUTH_NONE:       return "none";
        case PPP_AUTH_PAP:        return "PAP";
        case PPP_AUTH_CHAP_MD5:   return "CHAP-MD5";
        case PPP_AUTH_MSCHAP:     return "MS-CHAP";
        case PPP_AUTH_MSCHAPV2:   return "MS-CHAPv2";
        case PPP_AUTH_CHAP_SHA1:  return "CHAP-SHA1";
        case PPP_AUTH_CHAP_SHA256: return "CHAP-SHA256";
        case PPP_AUTH_CHAP_SHA384: return "CHAP-SHA384";
        case PPP_AUTH_CHAP_SHA512: return "CHAP-SHA512";
        case PPP_AUTH_EAP:        return "EAP-MD5";
        default:                  return "unknown";
    }
}


static const char *
ppp_protocol_name(uint16_t protocol)
{
    switch (protocol) {
        case PPP_PROTO_IP:             return "IPv4";
        case PPP_PROTO_IPCP:           return "IPCP";
        case PPP_PROTO_IPV6:           return "IPv6";
        case PPP_PROTO_IPV6CP:         return "IPv6CP";
        case PPP_PROTO_NBF:            return "NetBEUI";
        case PPP_PROTO_NBFCP:          return "NBFCP";
        case PPP_PROTO_IPX:            return "IPX";
        case PPP_PROTO_IPXCP:          return "IPXCP";
        case PPP_PROTO_LCP:            return "LCP";
        case PPP_PROTO_PAP:            return "PAP";
        case PPP_PROTO_CHAP:           return "CHAP";
        case PPP_PROTO_EAP:            return "EAP";
        case PPP_PROTO_CCP:            return "CCP";
        case PPP_PROTO_MPPE:           return "MPPE";
        case PPP_PROTO_MULTILINK:      return "Multilink";
        case PPP_PROTO_VJ_COMPRESSED:  return "VJ-compressed IPv4";
        case PPP_PROTO_VJ_UNCOMPRESSED:return "VJ-uncompressed IPv4";
        case PPP_AUTH_PROTO_SHIVA_PAP: return "Shiva PAP";
        case PPP_AUTH_PROTO_RSA:       return "RSA Authentication";
        case PPP_AUTH_PROTO_MITSUBISHI_SIEP: return "Mitsubishi SIEP";
        case PPP_AUTH_PROTO_VSAP:      return "Vendor-Specific Authentication";
        case PPP_AUTH_PROTO_PROPRIETARY_C281: return "Proprietary Authentication (0xC281)";
        case PPP_AUTH_PROTO_PROPRIETARY_C283: return "Proprietary Authentication (0xC283)";
        case PPP_AUTH_PROTO_PROPRIETARY_NODE_ID: return "Proprietary Node ID Authentication";
        default:                       return "unknown";
    }
}

#if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
static const char *
ppp_chap_algorithm_name(uint8_t algorithm)
{
    switch (algorithm) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:                 return "Reserved";
        case CHAP_ALG_MD5:      return "CHAP with MD5";
        case CHAP_ALG_SHA1:     return "CHAP with SHA-1";
        case CHAP_ALG_SHA256:   return "CHAP with SHA-256";
        case CHAP_ALG_SHA3_256: return "CHAP with SHA3-256";
        case CHAP_ALG_SHA384:   return "CHAP with SHA-384";
        case CHAP_ALG_SHA3_384: return "CHAP with SHA3-384";
        case CHAP_ALG_SHA512:   return "CHAP with SHA-512";
        case CHAP_ALG_SHA3_512: return "CHAP with SHA3-512";
        case CHAP_ALG_MSCHAP:   return "MS-CHAP";
        case CHAP_ALG_MSCHAPV2: return "MS-CHAP-2";
        default:                return "Unassigned or unsupported";
    }
}
#endif

static const char *
ppp_state_name(ppp_state_t state)
{
    switch (state) {
        case PPP_STATE_DEAD:          return "dead";
        case PPP_STATE_LCP_NEGOTIATE: return "LCP-negotiation";
        case PPP_STATE_AUTH:          return "authentication";
        case PPP_STATE_IPCP_NEGOTIATE:return "IPCP-negotiation";
        case PPP_STATE_NETWORK:       return "network";
        case PPP_STATE_TERMINATING:   return "terminating";
        default:                      return "unknown";
    }
}
#endif

static ppp_auth_type_t
ppp_auth_fallback_next(ppp_auth_type_t auth_type)
{
    switch (auth_type) {
        case PPP_AUTH_CHAP_SHA512: return PPP_AUTH_CHAP_SHA384;
        case PPP_AUTH_CHAP_SHA384: return PPP_AUTH_CHAP_SHA256;
        case PPP_AUTH_CHAP_SHA256: return PPP_AUTH_CHAP_SHA1;
        case PPP_AUTH_CHAP_SHA1:   return PPP_AUTH_CHAP_MD5;
        case PPP_AUTH_MSCHAPV2: return PPP_AUTH_MSCHAP;
        case PPP_AUTH_MSCHAP:   return PPP_AUTH_CHAP_MD5;
        case PPP_AUTH_CHAP_MD5: return PPP_AUTH_PAP;
        default:                return PPP_AUTH_NONE;
    }
}

/* Automatic authentication: one chain through everything we implement. MS-CHAPv2
   comes first because only it and MS-CHAP produce MPPE keys. */
static ppp_auth_type_t
ppp_auth_auto_next(ppp_auth_type_t auth_type)
{
    switch (auth_type) {
        case PPP_AUTH_MSCHAPV2:    return PPP_AUTH_MSCHAP;
        case PPP_AUTH_MSCHAP:      return PPP_AUTH_CHAP_SHA512;
        case PPP_AUTH_CHAP_SHA512: return PPP_AUTH_CHAP_SHA384;
        case PPP_AUTH_CHAP_SHA384: return PPP_AUTH_CHAP_SHA256;
        case PPP_AUTH_CHAP_SHA256: return PPP_AUTH_CHAP_SHA1;
        case PPP_AUTH_CHAP_SHA1:   return PPP_AUTH_CHAP_MD5;
        case PPP_AUTH_CHAP_MD5:    return PPP_AUTH_EAP;
        case PPP_AUTH_EAP:         return PPP_AUTH_PAP;
        default:                   return PPP_AUTH_NONE;
    }
}

static ppp_auth_type_t
ppp_auth_next(const ppp_ctx_t *ctx, ppp_auth_type_t auth_type)
{
    return ctx->auth_auto ? ppp_auth_auto_next(auth_type) : ppp_auth_fallback_next(auth_type);
}

static bool
ppp_auth_fallback_allowed(const ppp_ctx_t *ctx, ppp_auth_type_t suggested_auth)
{
    if (ctx->mppe_min_bits > 0)
        return false;

    ppp_auth_type_t fallback = ctx->auth_type;
    while ((fallback = ppp_auth_next(ctx, fallback)) != PPP_AUTH_NONE) {
        if (fallback == suggested_auth)
            return true;
    }
    return false;
}

#if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
static const char *
ppp_lcp_code_name(uint8_t code)
{
    switch (code) {
        case PPP_CODE_CONFIGURE_REQUEST: return "Configure-Request";
        case PPP_CODE_CONFIGURE_ACK:     return "Configure-Ack";
        case PPP_CODE_CONFIGURE_NAK:     return "Configure-Nak";
        case PPP_CODE_CONFIGURE_REJECT:  return "Configure-Reject";
        case PPP_CODE_TERMINATE_REQUEST: return "Terminate-Request";
        case PPP_CODE_TERMINATE_ACK:     return "Terminate-Ack";
        case PPP_CODE_CODE_REJECT:       return "Code-Reject";
        case PPP_CODE_PROTOCOL_REJECT:   return "Protocol-Reject";
        case PPP_CODE_ECHO_REQUEST:      return "Echo-Request";
        case PPP_CODE_ECHO_REPLY:        return "Echo-Reply";
        case PPP_CODE_DISCARD_REQUEST:   return "Discard-Request";
        case PPP_CODE_IDENTIFICATION:    return "Identification";
        default:                         return "unknown";
    }
}

static const char *
ppp_lcp_option_name(uint8_t option)
{
    switch (option) {
        case LCP_OPT_MRU:            return "MRU";
        case LCP_OPT_ACCM:           return "ACCM";
        case LCP_OPT_AUTH_PROTO:     return "Authentication-Protocol";
        case LCP_OPT_QUALITY_PROTO:  return "Quality-Protocol";
        case LCP_OPT_MAGIC_NUMBER:   return "Magic-Number";
        case LCP_OPT_PFC:            return "Protocol-Field-Compression";
        case LCP_OPT_ACFC:           return "Address-Control-Field-Compression";
        case LCP_OPT_SELF_DESCRIBING_PAD: return "Self-Describing-Pad";
        case LCP_OPT_NUMBERED_MODE:  return "Numbered-Mode";
        case LCP_OPT_CALLBACK:       return "Callback";
        case LCP_OPT_MRRU:           return "MRRU";
        case LCP_OPT_SHORT_SEQUENCE: return "Short-Sequence-Number-Header";
        case LCP_OPT_ENDPOINT_DISC:  return "Endpoint-Discriminator";
        default:                     return "unknown";
    }
}
#endif

/* CCITT 32-bit FCS (RFC 1570 appendix A / RFC 1662): reflected polynomial 0xEDB88320. */
static uint32_t
ppp_fcs32(const uint8_t *data, int len)
{
    uint32_t fcs = 0xFFFFFFFFu;

    for (int i = 0; i < len; i++) {
        fcs ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            fcs = (fcs >> 1) ^ (0xEDB88320u & (0u - (fcs & 1u)));
    }
    return ~fcs;
}

/* FCS-16 lookup table (CRC-CCITT reflected, polynomial 0x8408) from RFC 1662 */
static const uint16_t fcs16_table[256] = {
    0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
    0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
    0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E,
    0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
    0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD,
    0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
    0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C,
    0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
    0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB,
    0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
    0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A,
    0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
    0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9,
    0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
    0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
    0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
    0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7,
    0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
    0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036,
    0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
    0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5,
    0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
    0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134,
    0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
    0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3,
    0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
    0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232,
    0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
    0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1,
    0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
    0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330,
    0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78
};

uint16_t
ppp_fcs16(const uint8_t *data, int len)
{
    uint16_t fcs = 0xFFFF;
    for (int i = 0; i < len; i++)
        fcs = (fcs >> 8) ^ fcs16_table[(fcs ^ data[i]) & 0xFF];
    return fcs ^ 0xFFFF;
}

bool
ppp_random_bytes(uint8_t *buffer, uint8_t len)
{
#ifdef _WIN32
    return BCryptGenRandom(NULL, buffer, (ULONG) len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    FILE   *random_source = fopen("/dev/urandom", "rb");
    size_t  remaining     = len;

    if (!random_source)
        return false;

    while (remaining > 0) {
        size_t bytes_read = fread(buffer, 1, remaining, random_source);
        if (bytes_read == 0) {
            fclose(random_source);
            return false;
        }
        buffer += bytes_read;
        remaining -= bytes_read;
    }

    return fclose(random_source) == 0;
#endif
}

static uint16_t
ppp_ras_crc16(const uint8_t *data, int length)
{
    uint16_t crc = 0;
    for (int position = 0; position < length; position++) {
        crc ^= (uint16_t) data[position] << 8;
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) ? (uint16_t) ((crc << 1) ^ 0x1021)
                                 : (uint16_t) (crc << 1);
    }
    return crc;
}

static void
ppp_ras_send_flush(ppp_ctx_t *ctx)
{
    uint8_t frame[10] = {
        PPP_RAS_SYN,
        PPP_RAS_SOH_DEST | PPP_RAS_SOH_TYPE | PPP_RAS_SOH_COMPRESS,
        0, 3,
        (uint8_t) (PPP_RAS_IP_TYPE >> 8), (uint8_t) PPP_RAS_IP_TYPE,
        0xFF, PPP_RAS_ETX, 0, 0
    };
    if (!ctx || !ctx->serial_push)
        return;
    uint16_t crc = ppp_ras_crc16(frame + 1, sizeof(frame) - 3);
    frame[8] = (uint8_t) crc;
    frame[9] = (uint8_t) (crc >> 8);
    ctx->serial_push(ctx->modem, frame, sizeof(frame));
}

static bool
ppp_ras_send_ip(ppp_ctx_t *ctx, const uint8_t *ip_packet, int ip_length)
{
    uint8_t compressed[PPP_MAX_FRAME + 256];
    uint8_t frame[PPP_MAX_FRAME + 272];
    int compressed_length;
    int body_length;
    int frame_length;
    uint8_t ticket;

    if (!ctx || !ip_packet || ip_length <= 0 || ip_length > PPP_MAX_FRAME
        || ip_length > ctx->peer_mru)
        return false;
    if (ctx->ras_tx_flush_pending) {
        ppp_ccp_codec_flush(ctx, true);
        ctx->ras_tx_flush_pending = false;
    }
    if (!ppp_ccp_codec_compress(ctx, ip_packet, ip_length, compressed,
                                sizeof(compressed), &compressed_length))
        return false;

    if (ppp_ccp_codec_last_frame_flushed(ctx, true)) {
        ctx->ras_tx_ticket_base = ctx->ras_tx_ticket_base >= 96
                                ? 0 : (uint8_t) (ctx->ras_tx_ticket_base + 16);
        ticket = ctx->ras_tx_ticket_base;
        ctx->ras_tx_ticket_next = (uint8_t) (ticket + 1);
    } else {
        ticket = ctx->ras_tx_ticket_next;
        ctx->ras_tx_ticket_next++;
        if ((ctx->ras_tx_ticket_next & 0x0F) == 0)
            ctx->ras_tx_ticket_next = (uint8_t) (ctx->ras_tx_ticket_base + 1);
    }

    body_length = compressed_length + 3;
    frame[0] = PPP_RAS_SYN;
    frame[1] = PPP_RAS_SOH_DEST | PPP_RAS_SOH_TYPE | PPP_RAS_SOH_COMPRESS;
    frame[2] = (uint8_t) (body_length >> 8);
    frame[3] = (uint8_t) body_length;
    frame[4] = (uint8_t) (PPP_RAS_IP_TYPE >> 8);
    frame[5] = (uint8_t) PPP_RAS_IP_TYPE;
    frame[6] = ticket;
    memcpy(frame + 7, compressed, (size_t) compressed_length);
    frame[7 + compressed_length] = PPP_RAS_ETX;
    frame_length = compressed_length + 10;
    uint16_t crc = ppp_ras_crc16(frame + 1, frame_length - 3);
    frame[frame_length - 2] = (uint8_t) crc;
    frame[frame_length - 1] = (uint8_t) (crc >> 8);
    ctx->serial_push(ctx->modem, frame, frame_length);
    return true;
}

static void
ppp_process_ras_frame(ppp_ctx_t *ctx, const uint8_t *frame, int frame_length)
{
    if (!ctx || !frame || frame_length < 10
        || frame[0] != PPP_RAS_SYN
           || frame[1] != (PPP_RAS_SOH_DEST | PPP_RAS_SOH_TYPE | PPP_RAS_SOH_COMPRESS))
        return;

    int body_length = (frame[2] << 8) | frame[3];
    if (body_length < 3 || body_length + 7 != frame_length
        || frame[frame_length - 3] != PPP_RAS_ETX)
        return;
    uint16_t expected_crc = ppp_ras_crc16(frame + 1, frame_length - 3);
    uint16_t received_crc = (uint16_t) frame[frame_length - 2]
                          | ((uint16_t) frame[frame_length - 1] << 8);
    if (expected_crc != received_crc
        || (((uint16_t) frame[4] << 8) | frame[5]) != PPP_RAS_IP_TYPE)
        return;

    ctx->keepalive_idle_ms = 0;
    ctx->keepalive_missed  = 0;

    uint8_t ticket = frame[6];
    if (ticket & 0x80) {
        ctx->ras_tx_flush_pending = true;
        if (ticket == 0xFF)
            return;
        ticket ^= 0x80;
    }

    const uint8_t *payload = frame + 7;
    int payload_length = frame_length - 10;
    if (ticket == 0x7E) {
        if (ctx->ccp_open && ctx->ccp_rx_method == PPP_CCP_METHOD_NT31RAS
            && ctx->state == PPP_STATE_NETWORK && payload_length > 0
            && payload_length <= ctx->our_mru)
            ctx->network_send_ip(ctx->modem, payload, payload_length);
        return;
    }
    if (ticket >= 0x7E)
        return;

    if ((ticket & 0x0F) == 0) {
        ctx->ras_rx_ticket_base = ticket;
        ctx->ras_rx_ticket_next = (uint8_t) (ticket + 1);
        ppp_ccp_codec_flush(ctx, false);
    } else if (ticket != ctx->ras_rx_ticket_next) {
        ctx->ras_rx_ticket_next = 0;
        ppp_ccp_codec_flush(ctx, false);
        ppp_ras_send_flush(ctx);
        return;
    } else {
        ctx->ras_rx_ticket_next++;
        if ((ctx->ras_rx_ticket_next & 0x0F) == 0)
            ctx->ras_rx_ticket_next = (uint8_t) (ctx->ras_rx_ticket_base + 1);
    }

    if (!ctx->ccp_open || ctx->ccp_rx_method != PPP_CCP_METHOD_NT31RAS)
        return;

    uint8_t ip_packet[PPP_MAX_FRAME];
    int ip_length;
    if (!ppp_ccp_codec_decompress(ctx, payload, payload_length,
                                  ip_packet, sizeof(ip_packet), &ip_length)) {
        ppp_ras_send_flush(ctx);
        return;
    }
    if (ctx->state == PPP_STATE_NETWORK && ip_length > 0 && ip_length <= ctx->our_mru)
        ctx->network_send_ip(ctx->modem, ip_packet, ip_length);
}

/* Send raw HDLC-framed data to the serial line */
static void
ppp_debug_log_hex(ppp_ctx_t *ctx, const char *label, const uint8_t *data, int length)
{
    char   line[128];
    int    dump_length = length < 16 ? length : 16;
    int    written;
    size_t used;

    if (!ctx || !label || (!data && dump_length > 0) || dump_length < 0)
        return;

    written = snprintf(line, sizeof(line), "%s (%d/%d bytes)=", label,
                       dump_length, length);
    if (written < 0)
        return;
    used = (size_t) written < sizeof(line) ? (size_t) written : sizeof(line) - 1;

    for (int i = 0; i < dump_length && used + 2 < sizeof(line); i++) {
        written = snprintf(line + used, sizeof(line) - used, "%02X", data[i]);
        if (written != 2)
            break;
        used += (size_t) written;
    }
    MODEM_DEBUG_LOG(ctx->log, "%s\n", line);
}

/* Self-Describing-Pad applies to compressed datagrams (protocol 0x00FD) of CCP
   methods that use plain framing. MPPE/MPPC frames carry their own header and
   are never padded. */
static void ppp_send_frame_uncounted(ppp_ctx_t *ctx, uint16_t protocol, const uint8_t *data, int len);

/* Append the FCS the peer asked for. 32-bit only applies after LCP has opened (RFC 1570:
   the negotiated FCS takes effect during Authentication and Network phases only). */
static void
ppp_fcs_append(const ppp_ctx_t *ctx, uint8_t *raw, int *raw_len)
{
    if (ctx->fcs_tx32 && !ctx->fcs_default && ctx->state >= PPP_STATE_AUTH
        && ctx->state < PPP_STATE_TERMINATING) {
        uint32_t fcs = ppp_fcs32(raw, *raw_len);

        raw[(*raw_len)++] = (uint8_t) (fcs & 0xFF);
        raw[(*raw_len)++] = (uint8_t) (fcs >> 8);
        raw[(*raw_len)++] = (uint8_t) (fcs >> 16);
        raw[(*raw_len)++] = (uint8_t) (fcs >> 24);
    } else {
        uint16_t fcs = ppp_fcs16(raw, *raw_len);

        raw[(*raw_len)++] = (uint8_t) (fcs & 0xFF);
        raw[(*raw_len)++] = (uint8_t) (fcs >> 8);
    }
}

/* Every frame we send is counted for Link Quality Monitoring (RFC 1989). */
void
ppp_send_frame(ppp_ctx_t *ctx, uint16_t protocol, const uint8_t *data, int len)
{
    ctx->lqm_out_packets++;
    ctx->lqm_out_octets += (uint32_t) len + 2;
    ppp_send_frame_uncounted(ctx, protocol, data, len);
}

static bool
ppp_nm_active(const ppp_ctx_t *ctx)
{
    return ctx->lapb && ctx->nm_our_active && ctx->nm_peer_active && lapb_is_up(ctx->lapb);
}

static void
ppp_send_frame_uncounted(ppp_ctx_t *ctx, uint16_t protocol, const uint8_t *data, int len)
{
    uint8_t  frame[((PPP_MAX_FRAME + 264) * 2) + 2];
    uint8_t  raw[PPP_MAX_FRAME + 264];
    uint8_t  encrypted[PPP_MAX_FRAME * 2];
    uint8_t  plaintext[PPP_MAX_FRAME];
    uint8_t  ccp_compressed[PPP_MAX_FRAME * 2];
    uint8_t  sdp_padded[PPP_MAX_FRAME * 2 + 8];
    uint8_t  nm_info[PPP_MAX_FRAME * 2 + 8];
    uint8_t  mppc_flags = 0;
    int      raw_len = 0;
    int      out_len = 0;
    size_t   encrypted_len = 0;
    uint16_t wire_protocol = protocol;
    const uint8_t *wire_data = data;
    int      wire_len = len;
    bool     compress_ac = ctx->state >= PPP_STATE_AUTH
                        && ctx->peer_acfc && protocol != PPP_PROTO_LCP;
    bool     compress_protocol = ctx->state >= PPP_STATE_AUTH
                              && ctx->peer_pfc && (protocol & 0xFF00) == 0
                              && (protocol & 1) != 0;
    bool     multilink_frame = protocol == PPP_PROTO_MULTILINK;

    if (len < 0 || len > PPP_MAX_FRAME - 6) {
        ppp_log(ctx->log, "PPP: Dropping oversized frame (%d bytes)\n", len);
        return;
    }

    /* MS-CHAPv2 derives MPPE keys even when the peer has disabled encryption.
       Keys being available does not mean plaintext is forbidden: only enforce
       encryption when the local configuration requires MPPE. */
    if (ctx->mppe_min_bits > 0 && !ctx->mppe_tx_enabled
        && protocol >= PPP_PROTO_IP && protocol <= 0x00FA && !multilink_frame) {
        ppp_log(ctx->log, "PPP: Dropping plaintext network packet while required MPPE is inactive\n");
        return;
    }

    if (!multilink_frame && ctx->mppe_tx_enabled
        && protocol >= PPP_PROTO_IP && protocol <= 0x00FA) {
        if (len > PPP_MAX_FRAME - 10) {
            ppp_log(ctx->log, "PPP: Dropping oversized MPPE frame (%d bytes)\n", len);
            return;
        }
        plaintext[0] = (uint8_t) (protocol >> 8);
        plaintext[1] = (uint8_t) protocol;
        if (len > 0) {
            if (!data)
                return;
            memcpy(plaintext + 2, data, (size_t) len);
        }
        const uint8_t *mppe_input = plaintext;
        size_t mppe_input_len = (size_t) len + 2;
        if (ctx->ccp_tx_method == PPP_CCP_METHOD_MPPC) {
            int compressed_len;
            if (!ctx->mppe_tx.stateful
                || (ctx->mppe_tx.next_count & 0x00FF) == 0x00FF
                || ctx->mppe_tx.force_reset)
                ppp_ccp_codec_flush(ctx, true);
            if (!ppp_ccp_codec_compress(ctx, plaintext, (int) mppe_input_len,
                                        ccp_compressed, sizeof(ccp_compressed),
                                        &compressed_len)
                || compressed_len < PPP_MPPE_HEADER_LENGTH)
                return;
            mppc_flags = ccp_compressed[0] & (PPP_MPPE_FLAG_FLUSHED
                                              | PPP_MPPE_FLAG_AT_FRONT
                                              | PPP_MPPE_FLAG_COMPRESSED);
            mppe_input = ccp_compressed + PPP_MPPE_HEADER_LENGTH;
            mppe_input_len = (size_t) compressed_len - PPP_MPPE_HEADER_LENGTH;
        }
        if (!ppp_mppe_encrypt(&ctx->mppe_tx, mppe_input, mppe_input_len, mppc_flags,
                              encrypted, sizeof(encrypted), &encrypted_len))
            return;
        wire_protocol = PPP_PROTO_MPPE;
        wire_data = encrypted;
        wire_len = (int) encrypted_len;
        compress_protocol = ctx->state >= PPP_STATE_AUTH && ctx->peer_pfc;
    } else if (!multilink_frame && ctx->ccp_open
               && ctx->ccp_tx_method == PPP_CCP_METHOD_NT31RAS
               && protocol == PPP_PROTO_IP) {
        (void) ppp_ras_send_ip(ctx, data, len);
        return;
    } else if (!multilink_frame && ctx->ccp_open
               && (ctx->ccp_tx_method == PPP_CCP_METHOD_PREDICTOR1
                || ctx->ccp_tx_method == PPP_CCP_METHOD_PREDICTOR2
                || ctx->ccp_tx_method == PPP_CCP_METHOD_LZS
                || ctx->ccp_tx_method == PPP_CCP_METHOD_LZS_EXTENDED
                || ctx->ccp_tx_method == PPP_CCP_METHOD_LZS_DCP
                || ctx->ccp_tx_method == PPP_CCP_METHOD_V44
                || ctx->ccp_tx_method == PPP_CCP_METHOD_DEFLATE
                || ctx->ccp_tx_method == PPP_CCP_METHOD_MPPC
                || ctx->ccp_tx_method == PPP_CCP_METHOD_BSD)
               && protocol >= PPP_PROTO_IP && protocol <= 0x00FA) {
        int compressed_len;
        int plaintext_len;
        if ((ctx->ccp_tx_method == PPP_CCP_METHOD_BSD
             || ctx->ccp_tx_method == PPP_CCP_METHOD_DEFLATE)
            && protocol < 0x0100) {
            plaintext[0] = (uint8_t) protocol;
            plaintext_len = len + 1;
            if (len > 0) {
                if (!data)
                    return;
                memcpy(plaintext + 1, data, (size_t) len);
            }
        } else {
            plaintext[0] = (uint8_t) (protocol >> 8);
            plaintext[1] = (uint8_t) protocol;
            plaintext_len = len + 2;
            if (len > 0) {
                if (!data)
                    return;
                memcpy(plaintext + 2, data, (size_t) len);
            }
        }
        if (!ppp_ccp_codec_compress(ctx, plaintext, plaintext_len, encrypted,
                                    sizeof(encrypted), &compressed_len))
            return;
                if ((ctx->ccp_tx_method == PPP_CCP_METHOD_LZS
                         || ctx->ccp_tx_method == PPP_CCP_METHOD_LZS_EXTENDED
                         || ctx->ccp_tx_method == PPP_CCP_METHOD_V44)
            && compressed_len + (ctx->peer_pfc ? 1 : 2)
               >= len + (compress_protocol ? 1 : 2)) {
            if (ctx->ccp_tx_method == PPP_CCP_METHOD_LZS
                || ctx->ccp_tx_method == PPP_CCP_METHOD_LZS_EXTENDED)
                ppp_ccp_codec_flush(ctx, true);
            wire_protocol = protocol;
            wire_data = data;
            wire_len = len;
        } else {
            wire_protocol = PPP_PROTO_MPPE;
            wire_data = encrypted;
            wire_len = compressed_len;
            compress_protocol = ctx->state >= PPP_STATE_AUTH && ctx->peer_pfc
                             && ctx->ccp_tx_method != PPP_CCP_METHOD_LZS_EXTENDED;
        }
    }

    /* RFC 1570: with Self-Describing-Pad agreed, a frame whose final octet could be
       mistaken for a pad count gets padding 1, 2, ... N (the last octet is the count).
       One octet suffices when the final octet is 1; otherwise pad up to the value. It
       applies once LCP is open, and never to Multilink fragments. */
    if (ctx->sdp_peer_active && ctx->state >= PPP_STATE_AUTH && !ctx->multilink_group[0]
        && wire_len > 0 && wire_data) {
        uint8_t last = wire_data[wire_len - 1];

        if (last >= 1 && last <= ctx->sdp_peer_max && wire_len + 1 < (int) sizeof(sdp_padded)) {
            /* The pad is the ascending sequence 1..n whose final octet n is the count, so a
               frame ending in octet L needs no pad when L is outside 1..Max; here L is in
               range, and one pad octet of value 1 makes the frame end in 1, which the
               receiver removes together with exactly one octet. */
            memcpy(sdp_padded, wire_data, (size_t) wire_len);
            sdp_padded[wire_len] = 1;
            wire_data = sdp_padded;
            wire_len++;
        }
    }

    if (!multilink_frame && ctx->multilink_bundle
        && ctx->multilink_bundle->owner == ctx
        && protocol >= PPP_PROTO_IP && protocol <= 0x00FA) {
        if (wire_len < 0
            || !ppp_mppp_sender_send(&ctx->multilink_bundle->sender, wire_protocol,
                                     wire_data, (size_t) wire_len))
            ppp_log(ctx->log, "PPP: Could not send packet over Multilink bundle\n");
        return;
    }

    if (wire_len + (compress_protocol ? 1 : 2) > ctx->peer_mru) {
        ppp_log(ctx->log, "PPP: Dropping frame larger than peer MRU\n");
        return;
    }

    /* Numbered-Mode: once the LAPB link is up every frame travels as an I-frame.
       Address-and-Control-Field-Compression is never negotiated alongside it. */
    if (ppp_nm_active(ctx)) {
        int info_len = 0;

        if (compress_protocol) {
            nm_info[info_len++] = (uint8_t) wire_protocol;
        } else {
            nm_info[info_len++] = (uint8_t) (wire_protocol >> 8);
            nm_info[info_len++] = (uint8_t) (wire_protocol & 0xFF);
        }
        if (wire_len > 0) {
            if (!wire_data)
                return;
            memcpy(nm_info + info_len, wire_data, (size_t) wire_len);
            info_len += wire_len;
        }
        if (!lapb_send(ctx->lapb, nm_info, info_len))
            ppp_log(ctx->log, "PPP: Numbered-Mode send queue full; dropping frame\n");
        return;
    }

    /* Build unescaped frame with only negotiated header compression. */
    if (!compress_ac) {
        raw[raw_len++] = PPP_ADDRESS;
        raw[raw_len++] = PPP_CONTROL;
    }
    if (compress_protocol) {
        raw[raw_len++] = (uint8_t) wire_protocol;
    } else {
        raw[raw_len++] = (uint8_t) (wire_protocol >> 8);
        raw[raw_len++] = (uint8_t) (wire_protocol & 0xFF);
    }
    if (wire_len > 0) {
        if (!wire_data)
            return;
        memcpy(raw + raw_len, wire_data, (size_t) wire_len);
        raw_len += wire_len;
    }

    /* Calculate FCS over Address + Control + Protocol + Data */
    ppp_fcs_append(ctx, raw, &raw_len);

    /* HDLC escape and frame */
    frame[out_len++] = PPP_FLAG;
    for (int i = 0; i < raw_len; i++) {
        uint8_t c = raw[i];
        if (c == PPP_FLAG || c == PPP_ESCAPE
            || (c < 0x20 && ((ctx->peer_accm | ctx->flow_accm) & (1u << c)))) {
            frame[out_len++] = PPP_ESCAPE;
            frame[out_len++] = c ^ 0x20;
        } else {
            frame[out_len++] = c;
        }
    }
    frame[out_len++] = PPP_FLAG;

    MODEM_DEBUG_LOG(ctx->log, "PPP: TX frame protocol=%s (0x%04X) payload=%d "
                    "wire-protocol=%s (0x%04X) wire-payload=%d framed=%d "
                    "transform=%s ccp-open=%u ccp-method=%s MPPE-tx=%u "
                    "acfc=%u pfc=%u\n",
                    ppp_protocol_name(protocol), (unsigned) protocol, len,
                    ppp_protocol_name(wire_protocol), (unsigned) wire_protocol,
                    wire_len, out_len,
                    wire_protocol == PPP_PROTO_MPPE
                        ? (ctx->mppe_tx_enabled ? "MPPE"
                                                : ppp_ccp_method_name(ctx->ccp_tx_method))
                        : "none",
                    (unsigned) ctx->ccp_open,
                    ppp_ccp_method_name(ctx->ccp_tx_method),
                    (unsigned) ctx->mppe_tx_enabled,
                    (unsigned) compress_ac, (unsigned) compress_protocol);
    if (wire_protocol == PPP_PROTO_MPPE
        && ctx->ccp_tx_method == PPP_CCP_METHOD_MPPC) {
        ppp_debug_log_hex(ctx, ctx->mppe_tx_enabled
                                   ? "PPP: MPPE TX wire payload"
                                   : "PPP: MPPC TX packet",
                          wire_data, wire_len);
    }
    ctx->serial_push(ctx->modem, frame, out_len);
}

static uint32_t
ppp_multilink_endpoint_id(const ppp_ctx_t *ctx)
{
    uint32_t hash = 2166136261u;

    for (const char *pos = ctx->multilink_group; *pos; pos++) {
        hash ^= (uint8_t) *pos;
        hash *= 16777619u;
    }

    return hash;
}

/* Build and send an LCP Configuration-Request */
static void
ppp_send_lcp_config_request(ppp_ctx_t *ctx)
{
    uint8_t pkt[64];
    int     len = 0;

    ctx->lcp_ack_received = false;

    /* LCP header: Code(1) + Identifier(1) + Length(2) */
    pkt[0] = PPP_CODE_CONFIGURE_REQUEST;
    pkt[1] = ctx->lcp_id++;
    /* Length filled in later at offset 2-3 */
    len = 4;

    /* Option: MRU (type=1, len=4, value=1500) */
    pkt[len++] = LCP_OPT_MRU;
    pkt[len++] = 4;
    pkt[len++] = (uint8_t) (ctx->our_mru >> 8);
    pkt[len++] = (uint8_t) (ctx->our_mru & 0xFF);

    /* Option: ACCM (type=2, len=6, value=0x00000000) */
    pkt[len++] = LCP_OPT_ACCM;
    pkt[len++] = 6;
    pkt[len++] = (uint8_t) (ctx->our_accm >> 24);
    pkt[len++] = (uint8_t) (ctx->our_accm >> 16);
    pkt[len++] = (uint8_t) (ctx->our_accm >> 8);
    pkt[len++] = (uint8_t) (ctx->our_accm & 0xFF);

    if (ctx->request_pfc) {
        pkt[len++] = LCP_OPT_PFC;
        pkt[len++] = 2;
    }

    /* RFC 1663: Address-and-Control-Field-Compression MUST NOT be negotiated
       together with Numbered-Mode. */
    if (ctx->request_acfc && !ctx->nm_request) {
        pkt[len++] = LCP_OPT_ACFC;
        pkt[len++] = 2;
    }

    if (ctx->sdp_request) {
        pkt[len++] = LCP_OPT_SELF_DESCRIBING_PAD;
        pkt[len++] = 3;
        pkt[len++] = ctx->sdp_our_max;
    }

    if (ctx->compound_request) {
        pkt[len++] = LCP_OPT_COMPOUND_FRAMES;
        pkt[len++] = 2;
    }

    if (ctx->fcs_request) {
        pkt[len++] = LCP_OPT_FCS_ALTERNATIVES;
        pkt[len++] = 3;
        pkt[len++] = ctx->fcs_our_mask;
    }

    if (ctx->lqm_request) {
        pkt[len++] = LCP_OPT_QUALITY_PROTO;
        pkt[len++] = 8;
        pkt[len++] = (uint8_t) (PPP_PROTO_LQR >> 8);
        pkt[len++] = (uint8_t) PPP_PROTO_LQR;
        pkt[len++] = (uint8_t) (ctx->lqm_our_period_cs >> 24);
        pkt[len++] = (uint8_t) (ctx->lqm_our_period_cs >> 16);
        pkt[len++] = (uint8_t) (ctx->lqm_our_period_cs >> 8);
        pkt[len++] = (uint8_t) ctx->lqm_our_period_cs;
    }

    if (ctx->cbcp_client_req) {
        pkt[len++] = LCP_OPT_CALLBACK;
        pkt[len++] = 3;
        pkt[len++] = 6; /* Callback operation 6: Microsoft CBCP */
    }

    if (ctx->nm_request) {
        pkt[len++] = LCP_OPT_NUMBERED_MODE;
        pkt[len++] = 4;
        pkt[len++] = ctx->nm_our_window;
        pkt[len++] = ctx->nm_our_addr;
    }

    /* Option: Auth Protocol (type=3) - if we want authentication */
    if (ctx->auth_type != PPP_AUTH_NONE) {
        pkt[len++] = LCP_OPT_AUTH_PROTO;
        if (ctx->auth_type == PPP_AUTH_PAP || ctx->auth_type == PPP_AUTH_EAP) {
            uint16_t auth_proto = ctx->auth_type == PPP_AUTH_PAP
                                ? PPP_AUTH_PROTO_PAP : PPP_AUTH_PROTO_EAP;

            pkt[len++] = 4;
            pkt[len++] = (uint8_t) (auth_proto >> 8);
            pkt[len++] = (uint8_t) (auth_proto & 0xFF);
        } else {
            /* CHAP variants */
            pkt[len++] = 5;
            pkt[len++] = (uint8_t) (PPP_AUTH_PROTO_CHAP >> 8);
            pkt[len++] = (uint8_t) (PPP_AUTH_PROTO_CHAP & 0xFF);
            switch (ctx->auth_type) {
                case PPP_AUTH_CHAP_MD5:  pkt[len++] = CHAP_ALG_MD5;      break;
                case PPP_AUTH_CHAP_SHA1: pkt[len++] = CHAP_ALG_SHA1;     break;
                case PPP_AUTH_CHAP_SHA256: pkt[len++] = CHAP_ALG_SHA256; break;
                case PPP_AUTH_CHAP_SHA384: pkt[len++] = CHAP_ALG_SHA384; break;
                case PPP_AUTH_CHAP_SHA512: pkt[len++] = CHAP_ALG_SHA512; break;
                case PPP_AUTH_MSCHAP:    pkt[len++] = CHAP_ALG_MSCHAP;   break;
                case PPP_AUTH_MSCHAPV2:  pkt[len++] = CHAP_ALG_MSCHAPV2; break;
                default:                 pkt[len++] = CHAP_ALG_MD5;      break;
            }
        }
    }

    if (ctx->multilink_request_mrru) {
        pkt[len++] = LCP_OPT_MRRU;
        pkt[len++] = 4;
        pkt[len++] = (uint8_t) (ctx->multilink_our_mrru_value >> 8);
        pkt[len++] = (uint8_t) ctx->multilink_our_mrru_value;
    }

    if (ctx->multilink_request_short_sequence) {
        pkt[len++] = LCP_OPT_SHORT_SEQUENCE;
        pkt[len++] = 2;
    }

    if (ctx->multilink_request_endpoint) {
        uint32_t endpoint_id = ppp_multilink_endpoint_id(ctx);
        pkt[len++] = LCP_OPT_ENDPOINT_DISC;
        pkt[len++] = 7;
        pkt[len++] = 0;
        pkt[len++] = (uint8_t) (endpoint_id >> 24);
        pkt[len++] = (uint8_t) (endpoint_id >> 16);
        pkt[len++] = (uint8_t) (endpoint_id >> 8);
        pkt[len++] = (uint8_t) endpoint_id;
    }

    /* Option: Magic Number (type=5, len=6) */
    pkt[len++] = LCP_OPT_MAGIC_NUMBER;
    pkt[len++] = 6;
    pkt[len++] = (uint8_t) (ctx->our_magic >> 24);
    pkt[len++] = (uint8_t) (ctx->our_magic >> 16);
    pkt[len++] = (uint8_t) (ctx->our_magic >> 8);
    pkt[len++] = (uint8_t) (ctx->our_magic & 0xFF);

    /* Fill in length */
    pkt[2] = (uint8_t) (len >> 8);
    pkt[3] = (uint8_t) (len & 0xFF);

    memcpy(ctx->lcp_request, pkt, len);
    ctx->lcp_request_len = (uint8_t) len;
    ppp_send_frame(ctx, PPP_PROTO_LCP, pkt, len);
    ctx->lcp_req_sent = true;
    ctx->lcp_timeout_ms = 0;
    ppp_log(ctx->log, "PPP: Sent LCP Configure-Request (id=%d)\n", pkt[1]);
#if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
        uint16_t auth_protocol = 0;
        if (ctx->auth_type == PPP_AUTH_PAP)
            auth_protocol = PPP_AUTH_PROTO_PAP;
        else if (ctx->auth_type == PPP_AUTH_EAP)
            auth_protocol = PPP_AUTH_PROTO_EAP;
        else if (ctx->auth_type >= PPP_AUTH_CHAP_MD5 && ctx->auth_type <= PPP_AUTH_CHAP_SHA512)
            auth_protocol = PPP_AUTH_PROTO_CHAP;

        MODEM_DEBUG_LOG(ctx->log, "LCP: local MRU=%u ACCM=0x%08X PFC=%s ACFC=%s "
                        "authentication=%s protocol=%s (0x%04X)\n",
                        (unsigned) ctx->our_mru, ctx->our_accm,
                        ctx->request_pfc ? "on" : "off", ctx->request_acfc ? "on" : "off",
                        ppp_auth_name(ctx->auth_type),
                        auth_protocol ? ppp_protocol_name(auth_protocol) : "none",
                        (unsigned) auth_protocol);
    #endif
}

/* ---- Numbered-Mode (RFC 1663) glue between PPP and the LAPB link layer ---- */

/* Frame one HDLC frame whose Address and Control fields are supplied by the caller. */
static void
ppp_hdlc_send(ppp_ctx_t *ctx, uint8_t address, const uint8_t *control, int control_len,
              const uint8_t *info, int info_len)
{
    uint8_t  raw[PPP_MAX_FRAME + 16];
    uint8_t  frame[((PPP_MAX_FRAME + 16) * 2) + 2];
    int      raw_len = 0;
    int      out_len = 0;

    if (control_len < 1 || control_len > 2 || info_len < 0 || info_len > PPP_MAX_FRAME)
        return;

    raw[raw_len++] = address;
    memcpy(raw + raw_len, control, (size_t) control_len);
    raw_len += control_len;
    if (info_len > 0) {
        memcpy(raw + raw_len, info, (size_t) info_len);
        raw_len += info_len;
    }
    ppp_fcs_append(ctx, raw, &raw_len);

    frame[out_len++] = PPP_FLAG;
    for (int i = 0; i < raw_len; i++) {
        uint8_t c = raw[i];

        if (c == PPP_FLAG || c == PPP_ESCAPE
            || (c < 0x20 && ((ctx->peer_accm | ctx->flow_accm) & (1u << c)))) {
            frame[out_len++] = PPP_ESCAPE;
            frame[out_len++] = c ^ 0x20;
        } else {
            frame[out_len++] = c;
        }
    }
    frame[out_len++] = PPP_FLAG;

    MODEM_DEBUG_LOG(ctx->log, "PPP: TX HDLC address=0x%02X control=0x%02X%s info=%d framed=%d\n",
                    (unsigned) address, (unsigned) control[0],
                    control_len > 1 ? "+1" : "", info_len, out_len);
    ctx->serial_push(ctx->modem, frame, out_len);
}

static void
ppp_nm_emit(void *priv, const uint8_t *control, int control_len,
            const uint8_t *info, int info_len)
{
    ppp_ctx_t *ctx = (ppp_ctx_t *) priv;

    ppp_hdlc_send(ctx, ctx->nm_our_addr, control, control_len, info, info_len);
}

/* An in-sequence I-frame: replay its information field (Protocol + data) through
   the normal receive path as if it had arrived in an unnumbered frame. */
static void
ppp_nm_deliver(void *priv, const uint8_t *info, int info_len)
{
    ppp_ctx_t *ctx = (ppp_ctx_t *) priv;
    uint8_t    frame[PPP_MAX_FRAME + 2];

    if (info_len < 1 || info_len > PPP_MAX_FRAME)
        return;

    frame[0] = PPP_ADDRESS;
    frame[1] = PPP_CONTROL;
    memcpy(frame + 2, info, (size_t) info_len);
    ppp_process_frame(ctx, frame, info_len + 2, false);
}

static void
ppp_nm_up(void *priv)
{
    ppp_ctx_t *ctx = (ppp_ctx_t *) priv;

    ppp_log(ctx->log, "PPP: Numbered-Mode link established (modulo %u, send window %u)\n",
            ctx->lapb->modulus, ctx->lapb->window);
    if (ctx->state == PPP_STATE_LCP_NEGOTIATE)
        ppp_advance_state(ctx);
}

static void
ppp_nm_down(void *priv, lapb_down_reason_t reason)
{
    ppp_ctx_t *ctx = (ppp_ctx_t *) priv;

    if (reason == LAPB_DOWN_PEER_DM && ctx->state == PPP_STATE_LCP_NEGOTIATE) {
        /* RFC 1663 section 4: on DM revert to Unnumbered-Information operation and
           immediately send a new Configure-Request. The lapb object stays allocated
           (idle) until the session ends. */
        ppp_log(ctx->log, "PPP: Peer refused Numbered-Mode (DM); falling back to unnumbered operation\n");
        ctx->nm_request       = false;
        ctx->nm_our_active    = false;
        ctx->nm_peer_active   = false;
        ctx->nm_started       = false;
        ctx->lcp_ack_sent     = false;
        ctx->lcp_ack_received = false;
        ctx->lcp_retries      = 0;
        ppp_send_lcp_config_request(ctx);
        return;
    }

    ppp_log(ctx->log, "PPP: Numbered-Mode link lost (%s)\n",
            reason == LAPB_DOWN_TIMEOUT ? "peer stopped answering"
          : reason == LAPB_DOWN_PEER_DISC ? "peer disconnected" : "peer sent DM");
    ctx->state = PPP_STATE_DEAD;
}

static void
ppp_nm_log(void *priv, const char *message)
{
    ppp_ctx_t *ctx = (ppp_ctx_t *) priv;

    ppp_log(ctx->log, "PPP: %s\n", message);
    (void) ctx;
}

/* Create (or re-configure) the LAPB link once both directions have agreed. */
static void
ppp_nm_prepare(ppp_ctx_t *ctx)
{
    static const lapb_ops_t ops = { ppp_nm_emit, ppp_nm_deliver, ppp_nm_up, ppp_nm_down, ppp_nm_log };

    if (!ctx->nm_our_active || !ctx->nm_peer_active || ctx->peer_magic == 0)
        return;

    if (ctx->lapb) {
        if (!lapb_is_up(ctx->lapb))
            lapb_configure(ctx->lapb, ctx->nm_peer_window, PPP_LAPB_T1_MS, PPP_LAPB_N2);
        return;
    }

    ctx->lapb = lapb_create(&ops, ctx);
    if (!ctx->lapb) {
        ctx->nm_our_active  = false;
        ctx->nm_peer_active = false;
        return;
    }
    lapb_configure(ctx->lapb, ctx->nm_peer_window, PPP_LAPB_T1_MS, PPP_LAPB_N2);
    ctx->nm_started = false;
    ppp_log(ctx->log, "PPP: Numbered-Mode agreed: our window %u address 0x%02X, peer window %u address 0x%02X\n",
            (unsigned) ctx->nm_our_window, (unsigned) ctx->nm_our_addr,
            (unsigned) ctx->nm_peer_window, (unsigned) ctx->nm_peer_addr);
}

/* LCP is open but the LAPB link is not yet: authentication must wait. */
static bool
ppp_nm_pending(const ppp_ctx_t *ctx)
{
    return ctx->lapb && ctx->nm_our_active && ctx->nm_peer_active && !lapb_is_up(ctx->lapb);
}

/* RFC 1663: the peer with the numerically smaller Magic-Number sends SABM. The other
   waits, and establishes the link itself only if nothing arrives within two T1. */
static void
ppp_nm_start(ppp_ctx_t *ctx)
{
    if (ctx->nm_started)
        return;
    ctx->nm_started = true;

    if (ctx->our_magic < ctx->peer_magic) {
        ppp_log(ctx->log, "PPP: Numbered-Mode: sending SABM (our Magic-Number is the smaller)\n");
        lapb_connect(ctx->lapb);
    } else {
        ppp_log(ctx->log, "PPP: Numbered-Mode: waiting for the peer's SABM\n");
        lapb_listen(ctx->lapb, PPP_LAPB_TAKEOVER_MS);
    }
}

/* Called for every received HDLC frame before the normal address/control check.
   Returns true if the frame was a numbered-mode frame and has been consumed. */
static bool
ppp_nm_rx(ppp_ctx_t *ctx, const uint8_t *frame, int frame_len)
{
    if (frame_len < 2 || (frame[0] == PPP_ADDRESS && frame[1] == PPP_CONTROL))
        return false;

    if (ctx->lapb && ctx->nm_our_active && ctx->nm_peer_active) {
        /* The peer's announced Address is expected; ours is tolerated for peers that
           address frames to the receiver, as X.25 does. */
        if (frame[0] != ctx->nm_peer_addr && frame[0] != ctx->nm_our_addr)
            return false;
        lapb_rx(ctx->lapb, frame + 1, frame_len - 1);
        return true;
    }

    /* We are not in Numbered-Mode but the peer is trying to bring up a LAPB link
       (SABM or SABME). RFC 1663 section 4: answer DM and renegotiate. Only SABM(E)
       is treated this way so that address/control-compressed frames, whose first
       octet is a protocol byte, are never mistaken for it. */
    if ((frame[0] == 0x01 || frame[0] == 0x03)
        && ((frame[1] & ~0x10) == 0x2F || (frame[1] & ~0x10) == 0x6F)) {
        uint8_t dm = (uint8_t) (0x0F | (frame[1] & 0x10));

        ppp_log(ctx->log, "PPP: Peer sent SABM but Numbered-Mode was not negotiated; answering DM\n");
        ppp_hdlc_send(ctx, frame[0], &dm, 1, NULL, 0);

        if (ctx->state == PPP_STATE_LCP_NEGOTIATE && ctx->nm_dm_holdoff_ms == 0) {
            ctx->nm_dm_holdoff_ms = PPP_NM_DM_HOLDOFF_MS;
            ctx->lcp_ack_sent     = false;
            ctx->lcp_ack_received = false;
            ctx->lcp_retries      = 0;
            ppp_send_lcp_config_request(ctx);
        }
        return true;
    }

    return false;
}

/* ---- Callback Control Protocol (Microsoft CBCP, protocol 0xC029) ---- */

static void
ppp_cbcp_send(ppp_ctx_t *ctx, uint8_t code, uint8_t id, const uint8_t *opts, int opts_len)
{
    uint8_t pkt[4 + 64];

    if (opts_len < 0 || opts_len > 64)
        return;
    pkt[0] = code;
    pkt[1] = id;
    pkt[2] = 0;
    pkt[3] = (uint8_t) (4 + opts_len);
    if (opts_len > 0)
        memcpy(pkt + 4, opts, (size_t) opts_len);
    ppp_send_frame(ctx, PPP_PROTO_CBCP, pkt, 4 + opts_len);
}

static void
ppp_cbcp_send_request(ppp_ctx_t *ctx)
{
    uint8_t opts[8];
    int     len = 0;

    if (ctx->ppp_callback_mode == PPP_CALLBACK_OFFERED) {
        opts[len++] = CBCP_TYPE_NONE;
        opts[len++] = 2;
    }
    opts[len++] = CBCP_TYPE_USER; /* the caller supplies the number to be called */
    opts[len++] = 3;
    opts[len++] = ctx->ppp_callback_delay_s;
    ppp_cbcp_send(ctx, CBCP_CODE_REQUEST, ctx->cbcp_id, opts, len);
    ctx->cbcp_timeout_ms = 0;
}

/* Returns true while CBCP holds the link between authentication and the network
   phase. Only the answering (server) side runs the gate. */
static bool
ppp_cbcp_gate(ppp_ctx_t *ctx)
{
    if (!ctx->cbcp_peer || ctx->ppp_callback_mode == PPP_CALLBACK_DISABLED
        || ctx->ppp_callback_mode == PPP_CALLBACK_CLIENT)
        return false;

    switch (ctx->cbcp_phase) {
        case PPP_CBCP_IDLE:
            ppp_log(ctx->log, "PPP: Starting Callback Control Protocol\n");
            ctx->auth_complete   = true;
            ctx->state           = PPP_STATE_AUTH;
            ctx->auth_timeout_ms = 0;
            ctx->cbcp_id++;
            ctx->cbcp_retries = 0;
            ctx->cbcp_phase   = PPP_CBCP_WAIT_RESPONSE;
            ppp_cbcp_send_request(ctx);
            return true;
        case PPP_CBCP_WAIT_RESPONSE:
        case PPP_CBCP_HOLD:
            return true;
        default:
            return false;
    }
}

static void
ppp_cbcp_terminate_link(ppp_ctx_t *ctx)
{
    uint8_t pkt[4];

    pkt[0] = PPP_CODE_TERMINATE_REQUEST;
    pkt[1] = ctx->lcp_id++;
    pkt[2] = 0;
    pkt[3] = 4;
    ppp_send_frame(ctx, PPP_PROTO_LCP, pkt, 4);
}

static void
ppp_cbcp_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    const uint8_t *opt;
    int            total;
    int            opt_len;
    uint8_t        code;
    uint8_t        id;

    if (pkt_len < 4)
        return;
    code  = pkt[0];
    id    = pkt[1];
    total = (pkt[2] << 8) | pkt[3];
    if (total < 4 || total > pkt_len)
        return;
    opt     = pkt + 4;
    opt_len = total - 4;

    if (ctx->ppp_callback_mode == PPP_CALLBACK_CLIENT) {
        if (code == CBCP_CODE_REQUEST) {
            uint8_t resp[40];
            int     len = 0;

            if (ctx->cbcp_client_number[0]) {
                size_t nlen = strlen(ctx->cbcp_client_number);

                resp[len++] = CBCP_TYPE_USER;
                resp[len++] = (uint8_t) (4 + nlen + 1);
                resp[len++] = 0; /* delay chosen by the caller */
                resp[len++] = 1; /* address type: PSTN/ISDN */
                memcpy(resp + len, ctx->cbcp_client_number, nlen + 1);
                len += (int) nlen + 1;
            } else {
                resp[len++] = CBCP_TYPE_NONE;
                resp[len++] = 2;
            }
            ppp_cbcp_send(ctx, CBCP_CODE_RESPONSE, id, resp, len);
            ppp_log(ctx->log, "PPP: CBCP: answered Callback-Request (%s)\n",
                    ctx->cbcp_client_number[0] ? "callback" : "no callback");
        } else if (code == CBCP_CODE_ACK && opt_len >= 2) {
            ppp_log(ctx->log, "PPP: CBCP: Callback-Ack received (type %u)\n", (unsigned) opt[0]);
        }
        return;
    }

    if (code != CBCP_CODE_RESPONSE || ctx->cbcp_phase != PPP_CBCP_WAIT_RESPONSE
        || id != ctx->cbcp_id || opt_len < 2 || opt[1] < 2 || opt[1] > opt_len)
        return;

    switch (opt[0]) {
        case CBCP_TYPE_NONE:
            if (ctx->ppp_callback_mode != PPP_CALLBACK_OFFERED) {
                ppp_log(ctx->log, "PPP: CBCP: caller declined the required callback; disconnecting\n");
                ctx->state = PPP_STATE_DEAD;
                return;
            }
            ppp_cbcp_send(ctx, CBCP_CODE_ACK, id, opt, 2);
            ppp_log(ctx->log, "PPP: CBCP: caller chose no callback\n");
            ctx->cbcp_phase = PPP_CBCP_DONE;
            ppp_advance_state(ctx);
            break;

        case CBCP_TYPE_USER: {
            uint8_t delay = opt[1] > 2 ? opt[2] : ctx->ppp_callback_delay_s;
            size_t  nlen  = 0;

            ctx->callback_number[0] = '\0';
            if (opt[1] > 4) {
                nlen = (size_t) opt[1] - 4;
                if (nlen > sizeof(ctx->callback_number) - 1)
                    nlen = sizeof(ctx->callback_number) - 1;
                memcpy(ctx->callback_number, opt + 4, nlen);
                ctx->callback_number[nlen] = '\0';
            }
            ppp_cbcp_send(ctx, CBCP_CODE_ACK, id, opt, opt[1]);
            ppp_log(ctx->log, "PPP: CBCP: callback agreed (delay %u s, number \"%s\")\n",
                    (unsigned) delay, ctx->callback_number);
            ctx->callback_pending = true;
            ctx->callback_delay_s = delay;
            ctx->cbcp_phase       = PPP_CBCP_HOLD;
            ctx->cbcp_hold_ms     = 0;
            ppp_cbcp_terminate_link(ctx);
            break;
        }

        default:
            ppp_log(ctx->log, "PPP: CBCP: unsupported callback type %u; disconnecting\n", (unsigned) opt[0]);
            ctx->state = PPP_STATE_DEAD;
            break;
    }
}

static void
ppp_cbcp_tick(ppp_ctx_t *ctx)
{
    if (ctx->cbcp_phase == PPP_CBCP_WAIT_RESPONSE && ctx->state == PPP_STATE_AUTH) {
        if (++ctx->cbcp_timeout_ms >= PPP_CBCP_TIMEOUT_MS) {
            if (ctx->cbcp_retries++ < PPP_CBCP_MAX_RETRIES) {
                ppp_cbcp_send_request(ctx);
            } else {
                ppp_log(ctx->log, "PPP: CBCP: no Callback-Response; disconnecting\n");
                ctx->state = PPP_STATE_DEAD;
            }
        }
    } else if (ctx->cbcp_phase == PPP_CBCP_HOLD) {
        /* Give the Terminate-Request time to reach the caller before the line drops. */
        if (++ctx->cbcp_hold_ms >= PPP_CBCP_HOLD_MS)
            ctx->state = PPP_STATE_DEAD;
    }
}

/* ---- Link Quality Monitoring (RFC 1989) ---- */

static void
ppp_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static uint32_t
ppp_get32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static void
ppp_lqm_send(ppp_ctx_t *ctx)
{
    uint8_t lqr[PPP_LQR_LENGTH];

    ctx->lqm_out_lqrs++;
    ppp_put32(lqr + 0, ctx->our_magic);
    ppp_put32(lqr + 4, ctx->lqm_last_out_lqrs);
    ppp_put32(lqr + 8, ctx->lqm_last_out_packets);
    ppp_put32(lqr + 12, ctx->lqm_last_out_octets);
    ppp_put32(lqr + 16, ctx->lqm_save_in_lqrs);
    ppp_put32(lqr + 20, ctx->lqm_save_in_packets);
    ppp_put32(lqr + 24, ctx->lqm_save_in_discards);
    ppp_put32(lqr + 28, ctx->lqm_save_in_errors);
    ppp_put32(lqr + 32, ctx->lqm_save_in_octets);
    ppp_put32(lqr + 36, ctx->lqm_out_lqrs);
    ppp_put32(lqr + 40, ctx->lqm_out_packets + 1);                /* includes this report */
    ppp_put32(lqr + 44, ctx->lqm_out_octets + PPP_LQR_LENGTH + 2); /* includes this report */
    ppp_send_frame(ctx, PPP_PROTO_LQR, lqr, PPP_LQR_LENGTH);
}

static void
ppp_lqm_process(ppp_ctx_t *ctx, const uint8_t *data, int len)
{
    if (!ctx->lqm_peer_active && !ctx->lqm_our_active)
        return;
    if (len < PPP_LQR_LENGTH)
        return;

    ctx->lqm_in_lqrs++;
    /* Echo the peer's own counters back in our next report ... */
    ctx->lqm_last_out_lqrs    = ppp_get32(data + 36);
    ctx->lqm_last_out_packets = ppp_get32(data + 40);
    ctx->lqm_last_out_octets  = ppp_get32(data + 44);
    /* ... together with what we have received so far. */
    ctx->lqm_save_in_lqrs     = ctx->lqm_in_lqrs;
    ctx->lqm_save_in_packets  = ctx->lqm_in_packets;
    ctx->lqm_save_in_discards = ctx->lqm_in_discards;
    ctx->lqm_save_in_errors   = ctx->lqm_in_errors;
    ctx->lqm_save_in_octets   = ctx->lqm_in_octets;
    /* The peer's view of the traffic we sent it, for diagnostics. */
    ctx->lqm_rx_peer_in_packets  = ppp_get32(data + 20);
    ctx->lqm_rx_last_out_packets = ppp_get32(data + 8);

    ppp_log(ctx->log, "PPP: LQM: peer has received %u of the %u packets we had sent (%u input errors)\n",
            (unsigned) ctx->lqm_rx_peer_in_packets, (unsigned) ctx->lqm_rx_last_out_packets,
            (unsigned) ppp_get32(data + 28));
}

static void
ppp_lqm_tick(ppp_ctx_t *ctx)
{
    uint32_t period_ms;

    if (!ctx->lqm_peer_active || ctx->state < PPP_STATE_AUTH || ctx->state >= PPP_STATE_TERMINATING)
        return;

    period_ms = ctx->lqm_peer_period_cs < PPP_LQM_MIN_PERIOD_CS ? PPP_LQM_MIN_PERIOD_CS * 10
                                                                : ctx->lqm_peer_period_cs * 10;
    if (++ctx->lqm_timer_ms >= period_ms) {
        ctx->lqm_timer_ms = 0;
        ppp_lqm_send(ctx);
    }
}

/* The peer offered Multilink and no group is configured. RFC 1990 identifies a bundle
   by the peer's Endpoint Discriminator and authenticated name, so derive the group
   from those: every link of one peer lands in one bundle. */
static void
ppp_multilink_mirror(ppp_ctx_t *ctx, const uint8_t *pkt, int total)
{
    char     group[20];
    uint32_t hash = 2166136261u;

    for (int pos = 4; pos < total; pos += pkt[pos + 1]) {
        if (pkt[pos] == LCP_OPT_ENDPOINT_DISC) {
            for (int i = 2; i < pkt[pos + 1]; i++) {
                hash ^= pkt[pos + i];
                hash *= 16777619u;
            }
        }
    }
    for (const char *pos = ctx->username; *pos; pos++) {
        hash ^= (uint8_t) *pos;
        hash *= 16777619u;
    }

    snprintf(group, sizeof(group), "auto-%08X", (unsigned) hash);
    ppp_multilink_configure(ctx, group);
    ctx->multilink_request_mrru            = true;
    ctx->multilink_request_short_sequence  = true;
    ctx->multilink_request_endpoint        = true;
    /* Multilink fragments are not padded, so neither of these can stay offered. */
    ctx->sdp_request      = false;
    ctx->compound_request = false;
}

/* Handle LCP Configure-Request from peer */
static bool
ppp_handle_lcp_config_request(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    uint8_t ack[PPP_MAX_FRAME];
    uint8_t nak[PPP_MAX_FRAME];
    uint8_t rej[PPP_MAX_FRAME];
    int     ack_len = 4, nak_len = 4, rej_len = 4;
    uint8_t id      = pkt[1];
    int     total   = (pkt[2] << 8) | pkt[3];
    int     pos     = 4;
    uint16_t peer_mru = PPP_DEFAULT_MRU;
    uint32_t peer_accm = 0xFFFFFFFF;
    uint32_t peer_magic = 0;
    bool    peer_pfc = false;
    bool    peer_acfc = false;
    bool    peer_mrru_enabled = false;
    bool    peer_short_sequence = false;
    bool    peer_endpoint_enabled = false;
    uint16_t peer_mrru_value = 0;
    uint8_t peer_endpoint_data[32] = { 0 };
    uint8_t peer_endpoint_length = 0;
    bool    peer_sdp = false;
    bool    peer_cbcp = false;
    bool    peer_lqm = false;
    bool    peer_fcs32 = false;
    uint32_t peer_lqm_period = 0;
    uint8_t peer_sdp_max = 0;
    bool    peer_nm = false;
    uint8_t peer_nm_window = 0;
    uint8_t peer_nm_addr = 0;
    bool    saw_nm = false;
    bool    saw_sdp = false;
    bool    saw_compound = false;
    bool    saw_mrru = false;
    bool    resend_request = false;
    bool    peer_compound = false;
    uint32_t scanned_magic = 0;

    if (total < 4 || total > pkt_len)
        return false;

    for (int option_pos = 4; option_pos < total;) {
        if (option_pos + 2 > total)
            return false;
        uint8_t option_len = pkt[option_pos + 1];
        if (option_len < 2 || option_pos + option_len > total)
            return false;
        if (pkt[option_pos] == LCP_OPT_NUMBERED_MODE)
            saw_nm = true;
        else if (pkt[option_pos] == LCP_OPT_SELF_DESCRIBING_PAD)
            saw_sdp = true;
        else if (pkt[option_pos] == LCP_OPT_COMPOUND_FRAMES)
            saw_compound = true;
        else if (pkt[option_pos] == LCP_OPT_MRRU)
            saw_mrru = true;
        else if (pkt[option_pos] == LCP_OPT_MAGIC_NUMBER && option_len == 6)
            scanned_magic = ((uint32_t) pkt[option_pos + 2] << 24)
                          | ((uint32_t) pkt[option_pos + 3] << 16)
                          | ((uint32_t) pkt[option_pos + 4] << 8)
                          | (uint32_t) pkt[option_pos + 5];
        option_pos += option_len;
    }

    ctx->lcp_ack_sent = false;
    ppp_log(ctx->log, "PPP: Received LCP Configure-Request (id=%d, len=%d)\n", id, total);

    /* Some options only work once both sides have asked for them. In automatic mode
       we stay silent until the peer shows it wants one, then ask for it as well. */
    if (ctx->state == PPP_STATE_LCP_NEGOTIATE) {
        /* Our first request goes out with CONNECT, often before the guest reads the port.
           Its own request shows it is listening now, so repeat ours without waiting for
           the retry timer. An Ack for the earlier request is ignored by its identifier. */
        if (!ctx->lcp_peer_heard) {
            ctx->lcp_peer_heard = true;
            if (ctx->lcp_req_sent && !ctx->lcp_ack_received)
                resend_request = true;
        }
        if (saw_mrru && ctx->multilink_auto && !ctx->multilink_group[0]) {
            ppp_multilink_mirror(ctx, pkt, total);
            resend_request = true;
        }
        if (saw_nm && ctx->ppp_numbered_mode == PPP_NUMBERED_AUTOMATIC && !ctx->nm_request
            && !ctx->nm_mirrored && scanned_magic != 0
            && ctx->ppp_acfc_mode != PPP_LCP_COMPRESSION_REQUIRE) {
            ctx->nm_request  = true;
            ctx->nm_mirrored = true;
            resend_request   = true;
        }
        if (saw_compound && saw_sdp && ctx->ppp_compound_mode == PPP_COMPOUND_AUTOMATIC
            && !ctx->compound_request && !ctx->compound_mirrored
            && !ctx->multilink_group[0]) {
            ctx->compound_request  = true;
            ctx->compound_mirrored = true;
            ctx->sdp_request       = true;
            resend_request         = true;
        }
    }

    while (pos < total) {
        uint8_t opt_type = pkt[pos];
        uint8_t opt_len  = pkt[pos + 1];

        MODEM_DEBUG_LOG(ctx->log, "LCP: peer option=%s (type=%u) length=%u\n",
                ppp_lcp_option_name(opt_type), (unsigned) opt_type,
                (unsigned) opt_len);
    #if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
        if (opt_type == LCP_OPT_AUTH_PROTO && opt_len >= 4) {
            uint16_t auth_protocol = (uint16_t) ((pkt[pos + 2] << 8) | pkt[pos + 3]);
            if (auth_protocol == PPP_AUTH_PROTO_CHAP && opt_len == 5) {
                uint8_t algorithm = pkt[pos + 4];
                MODEM_DEBUG_LOG(ctx->log,
                                "LCP: peer authentication=%s (0x%04X) algorithm=%s (%u)\n",
                                ppp_protocol_name(auth_protocol), (unsigned) auth_protocol,
                                ppp_chap_algorithm_name(algorithm), (unsigned) algorithm);
            } else {
                MODEM_DEBUG_LOG(ctx->log, "LCP: peer authentication=%s (0x%04X)\n",
                                ppp_protocol_name(auth_protocol), (unsigned) auth_protocol);
            }
        }
#endif

        switch (opt_type) {
            case LCP_OPT_MRU:
                if (opt_len == 4) {
                    uint16_t requested_mru = (uint16_t) ((pkt[pos + 2] << 8) | pkt[pos + 3]);
                    if (requested_mru < PPP_MIN_MRU) {
                        nak[nak_len++] = LCP_OPT_MRU;
                        nak[nak_len++] = 4;
                        nak[nak_len++] = (uint8_t) (PPP_DEFAULT_MRU >> 8);
                        nak[nak_len++] = (uint8_t) PPP_DEFAULT_MRU;
                    } else {
                        peer_mru = requested_mru;
                        memcpy(ack + ack_len, pkt + pos, opt_len);
                        ack_len += opt_len;
                    }
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case LCP_OPT_ACCM:
                if (opt_len == 6) {
                    peer_accm = ((uint32_t) pkt[pos + 2] << 24)
                              | ((uint32_t) pkt[pos + 3] << 16)
                              | ((uint32_t) pkt[pos + 4] << 8)
                              | (uint32_t) pkt[pos + 5];
                    memcpy(ack + ack_len, pkt + pos, opt_len);
                    ack_len += opt_len;
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case LCP_OPT_MAGIC_NUMBER:
                if (opt_len == 6) {
                    peer_magic = ((uint32_t) pkt[pos + 2] << 24)
                               | ((uint32_t) pkt[pos + 3] << 16)
                               | ((uint32_t) pkt[pos + 4] << 8)
                               | (uint32_t) pkt[pos + 5];
                    if (peer_magic == 0 || peer_magic == ctx->our_magic) {
                        uint32_t suggested_magic = ctx->our_magic + 1;
                        if (suggested_magic == 0)
                            suggested_magic = 1;
                        nak[nak_len++] = LCP_OPT_MAGIC_NUMBER;
                        nak[nak_len++] = 6;
                        nak[nak_len++] = (uint8_t) (suggested_magic >> 24);
                        nak[nak_len++] = (uint8_t) (suggested_magic >> 16);
                        nak[nak_len++] = (uint8_t) (suggested_magic >> 8);
                        nak[nak_len++] = (uint8_t) suggested_magic;
                    } else {
                        memcpy(ack + ack_len, pkt + pos, opt_len);
                        ack_len += opt_len;
                    }
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case LCP_OPT_PFC:
                if (opt_len == 2 && ctx->ppp_pfc_mode != PPP_LCP_COMPRESSION_DISABLED) {
                    peer_pfc = true;
                    memcpy(ack + ack_len, pkt + pos, opt_len);
                    ack_len += opt_len;
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case LCP_OPT_ACFC:
                /* Refused when the same request carries Numbered-Mode (RFC 1663). */
                if (opt_len == 2 && ctx->ppp_acfc_mode != PPP_LCP_COMPRESSION_DISABLED
                    && !(saw_nm && ctx->nm_request)) {
                    peer_acfc = true;
                    memcpy(ack + ack_len, pkt + pos, opt_len);
                    ack_len += opt_len;
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case LCP_OPT_FCS_ALTERNATIVES:
                if (ctx->ppp_fcs_mode == PPP_FCS_DISABLED || opt_len != 3) {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                } else if ((pkt[pos + 2] & 0x06) == 0) {
                    /* Only the Null FCS was offered; we always send a real one. */
                    nak[nak_len++] = LCP_OPT_FCS_ALTERNATIVES;
                    nak[nak_len++] = 3;
                    nak[nak_len++] = 0x06;
                } else {
                    peer_fcs32 = (pkt[pos + 2] & 0x04) != 0;
                    memcpy(ack + ack_len, pkt + pos, opt_len);
                    ack_len += opt_len;
                }
                break;

            case LCP_OPT_QUALITY_PROTO:
                if (ctx->ppp_lqm_mode != PPP_LQM_DISABLED && opt_len == 8
                    && (((uint16_t) pkt[pos + 2] << 8) | pkt[pos + 3]) == PPP_PROTO_LQR) {
                    uint32_t period = ppp_get32(pkt + pos + 4);

                    if (period == 0) {
                        /* A period of zero is meaningless; suggest our default. */
                        nak[nak_len++] = LCP_OPT_QUALITY_PROTO;
                        nak[nak_len++] = 8;
                        nak[nak_len++] = (uint8_t) (PPP_PROTO_LQR >> 8);
                        nak[nak_len++] = (uint8_t) PPP_PROTO_LQR;
                        ppp_put32(nak + nak_len, PPP_LQM_DEFAULT_PERIOD_CS);
                        nak_len += 4;
                    } else {
                        peer_lqm        = true;
                        peer_lqm_period = period;
                        memcpy(ack + ack_len, pkt + pos, opt_len);
                        ack_len += opt_len;
                    }
                } else if (ctx->ppp_lqm_mode != PPP_LQM_DISABLED && opt_len >= 4) {
                    /* Some other quality protocol: ask for Link Quality Report instead. */
                    nak[nak_len++] = LCP_OPT_QUALITY_PROTO;
                    nak[nak_len++] = 8;
                    nak[nak_len++] = (uint8_t) (PPP_PROTO_LQR >> 8);
                    nak[nak_len++] = (uint8_t) PPP_PROTO_LQR;
                    ppp_put32(nak + nak_len, PPP_LQM_DEFAULT_PERIOD_CS);
                    nak_len += 4;
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case LCP_OPT_CALLBACK:
                /* Only Microsoft CBCP (operation 6) is supported, and only when enabled. */
                if ((ctx->ppp_callback_mode == PPP_CALLBACK_OFFERED
                     || ctx->ppp_callback_mode == PPP_CALLBACK_REQUIRED)
                    && opt_len == 3 && pkt[pos + 2] == 6) {
                    peer_cbcp = true;
                    memcpy(ack + ack_len, pkt + pos, opt_len);
                    ack_len += opt_len;
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case LCP_OPT_COMPOUND_FRAMES:
                /* Compound frames need Self-Describing-Pad in the same request (RFC 1570). */
                if (ctx->ppp_compound_mode != PPP_COMPOUND_DISABLED && opt_len == 2 && saw_sdp
                    && !ctx->multilink_group[0]) {
                    peer_compound = true;
                    memcpy(ack + ack_len, pkt + pos, opt_len);
                    ack_len += opt_len;
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case LCP_OPT_SELF_DESCRIBING_PAD:
                if (ctx->ppp_sdp_mode != PPP_SDP_DISABLED && !ctx->multilink_group[0]
                    && opt_len == 3 && pkt[pos + 2] != 0) {
                    peer_sdp = true;
                    peer_sdp_max = pkt[pos + 2];
                    memcpy(ack + ack_len, pkt + pos, opt_len);
                    ack_len += opt_len;
                } else if (ctx->ppp_sdp_mode != PPP_SDP_DISABLED && !ctx->multilink_group[0] && opt_len == 3) {
                    /* A Maximum of zero is meaningless (valid range 1-255). */
                    nak[nak_len++] = LCP_OPT_SELF_DESCRIBING_PAD;
                    nak[nak_len++] = 3;
                    nak[nak_len++] = PPP_SDP_DEFAULT_MAX;
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case LCP_OPT_NUMBERED_MODE:
                /* Rejected when not offered, malformed, without a Magic-Number in the
                   same request, or with a multi-octet address (not supported). */
                if (!ctx->nm_request || opt_len != 4 || scanned_magic == 0
                    || (pkt[pos + 3] != 0 && (pkt[pos + 3] & 1) == 0)) {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                } else {
                    uint8_t requested_window = pkt[pos + 2];
                    uint8_t requested_addr   = pkt[pos + 3];
                    uint8_t window           = requested_window;
                    uint8_t addr             = requested_addr;
                    bool    our_ext          = ctx->nm_our_window >= 8;
                    bool    refuse           = false;

                    if (window == 0)
                        window = ctx->nm_our_window;
                    if (window > 127)
                        window = 127;
                    /* Both directions must use the same modulus. A Nak may only
                       reduce the window, so a modulo-8 peer cannot be raised: drop
                       our own window to modulo 8 instead. */
                    if ((window >= 8) != our_ext) {
                        if (window >= 8) {
                            window = 7;
                        } else if (ctx->state == PPP_STATE_LCP_NEGOTIATE) {
                            ctx->nm_our_window = 7;
                            resend_request     = true;
                        } else {
                            refuse = true;
                        }
                    }
                    /* Address zero asks us to choose. Equal addresses are fine: each side
                       only labels the frames it transmits, and both accept either value. */
                    if (addr == 0)
                        addr = ctx->nm_our_addr;

                    if (refuse) {
                        memcpy(rej + rej_len, pkt + pos, opt_len);
                        rej_len += opt_len;
                    } else if (window != requested_window || addr != requested_addr) {
                        nak[nak_len++] = LCP_OPT_NUMBERED_MODE;
                        nak[nak_len++] = 4;
                        nak[nak_len++] = window;
                        nak[nak_len++] = addr;
                    } else {
                        peer_nm        = true;
                        peer_nm_window = window;
                        peer_nm_addr   = addr;
                        memcpy(ack + ack_len, pkt + pos, opt_len);
                        ack_len += opt_len;
                    }
                }
                break;

            case LCP_OPT_AUTH_PROTO:
                /* We are the server; we don't accept the client requesting auth from us.
                   Reject this option so they don't try to authenticate us. */
                memcpy(rej + rej_len, pkt + pos, opt_len);
                rej_len += opt_len;
                break;

            case LCP_OPT_MRRU:
                if (ctx->multilink_group[0] && opt_len == 4) {
                    uint16_t requested_mrru = (uint16_t) ((pkt[pos + 2] << 8) | pkt[pos + 3]);
                    if (requested_mrru < PPP_MIN_MRU || requested_mrru > PPP_MAX_FRAME) {
                        uint16_t suggested_mrru = PPP_DEFAULT_MRU + 2;
                        nak[nak_len++] = LCP_OPT_MRRU;
                        nak[nak_len++] = 4;
                        nak[nak_len++] = (uint8_t) (suggested_mrru >> 8);
                        nak[nak_len++] = (uint8_t) suggested_mrru;
                    } else {
                        peer_mrru_enabled = true;
                        peer_mrru_value = requested_mrru;
                        memcpy(ack + ack_len, pkt + pos, opt_len);
                        ack_len += opt_len;
                    }
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case LCP_OPT_SHORT_SEQUENCE:
                if (ctx->multilink_group[0] && opt_len == 2) {
                    peer_short_sequence = true;
                    memcpy(ack + ack_len, pkt + pos, opt_len);
                    ack_len += opt_len;
                } else {
                    memcpy(rej + rej_len, pkt + pos, opt_len);
                    rej_len += opt_len;
                }
                break;

            case LCP_OPT_ENDPOINT_DISC:
                if (ctx->multilink_group[0] && opt_len >= 3 && opt_len <= 34) {
                    peer_endpoint_enabled = true;
                    peer_endpoint_length = (uint8_t) (opt_len - 2);
                    memcpy(peer_endpoint_data, pkt + pos + 2, peer_endpoint_length);
                    memcpy(ack + ack_len, pkt + pos, opt_len);
                    ack_len += opt_len;
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

    if (rej_len > 4) {
        /* Send Configure-Reject */
        rej[0] = PPP_CODE_CONFIGURE_REJECT;
        rej[1] = id;
        rej[2] = (uint8_t) (rej_len >> 8);
        rej[3] = (uint8_t) (rej_len & 0xFF);
        ppp_send_frame(ctx, PPP_PROTO_LCP, rej, rej_len);
        ppp_log(ctx->log, "PPP: Sent LCP Configure-Reject\n");
    } else if (nak_len > 4) {
        /* Send Configure-Nak */
        nak[0] = PPP_CODE_CONFIGURE_NAK;
        nak[1] = id;
        nak[2] = (uint8_t) (nak_len >> 8);
        nak[3] = (uint8_t) (nak_len & 0xFF);
        ppp_send_frame(ctx, PPP_PROTO_LCP, nak, nak_len);
        ppp_log(ctx->log, "PPP: Sent LCP Configure-Nak\n");
    } else {
        /* Send Configure-Ack */
        ack[0] = PPP_CODE_CONFIGURE_ACK;
        ack[1] = id;
        ack[2] = (uint8_t) (ack_len >> 8);
        ack[3] = (uint8_t) (ack_len & 0xFF);
        ppp_send_frame(ctx, PPP_PROTO_LCP, ack, ack_len);
        ctx->peer_mru = peer_mru;
        ctx->peer_accm = peer_accm;
        ctx->peer_magic = peer_magic;
        ctx->peer_pfc = peer_pfc;
        ctx->peer_acfc = peer_acfc;
        ctx->nm_peer_active  = peer_nm;
        ctx->nm_peer_window  = peer_nm_window;
        ctx->nm_peer_addr    = peer_nm_addr;
        ctx->sdp_peer_active = peer_sdp;
        ctx->cbcp_peer       = peer_cbcp;
        ctx->lqm_peer_active = peer_lqm;
        ctx->fcs_tx32        = peer_fcs32;
        (void) peer_compound; /* we never send compound frames, so the peer's request needs no state */
        ctx->lqm_peer_period_cs = peer_lqm_period;
        ctx->sdp_peer_max    = peer_sdp_max;
         ctx->multilink_peer_mrru = peer_mrru_enabled;
         ctx->multilink_peer_mrru_value = peer_mrru_value;
         ctx->multilink_peer_short_sequence = peer_short_sequence;
         ctx->multilink_peer_endpoint = peer_endpoint_enabled;
         ctx->multilink_peer_endpoint_length = peer_endpoint_length;
         memcpy(ctx->multilink_peer_endpoint_data, peer_endpoint_data,
             sizeof(ctx->multilink_peer_endpoint_data));
        ctx->lcp_ack_sent = true;
        ppp_log(ctx->log, "PPP: Sent LCP Configure-Ack\n");
        ppp_nm_prepare(ctx);
    }

    if (resend_request) {
        ppp_log(ctx->log, "PPP: Sending a new LCP Configure-Request to match the peer\n");
        ppp_send_lcp_config_request(ctx);
    }

    return true;
}

/* Handle LCP Configure-Ack from peer */
static void
ppp_handle_lcp_config_ack(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    int total;
    int pos = 4;

    if (pkt_len < 4 || !ctx->lcp_req_sent)
    {
        MODEM_DEBUG_LOG(ctx->log, "LCP: ignored Configure-Ack (length=%d request-pending=%u)\n",
                        pkt_len, (unsigned) ctx->lcp_req_sent);
        return;
    }

    total = (pkt[2] << 8) | pkt[3];
    if (total != pkt_len || total != ctx->lcp_request_len
        || pkt[1] != ctx->lcp_request[1]) {
        MODEM_DEBUG_LOG(ctx->log, "LCP: ignored Configure-Ack id=%u length=%d expected id=%u length=%u\n",
                        (unsigned) pkt[1], total, (unsigned) ctx->lcp_request[1],
                        (unsigned) ctx->lcp_request_len);
        return;
    }
    if (memcmp(pkt + 4, ctx->lcp_request + 4, total - 4) != 0) {
        MODEM_DEBUG_LOG(ctx->log, "LCP: ignored Configure-Ack id=%u because options differ from request\n",
                        (unsigned) pkt[1]);
        return;
    }

    ctx->our_pfc  = false;
    ctx->our_acfc = false;
    ctx->nm_our_active  = false;
    ctx->sdp_our_active = false;
    ctx->lqm_our_active = false;
    ctx->fcs_rx32_ok    = false;
    ctx->compound_rx    = false;
    ctx->multilink_our_mrru = false;
    ctx->multilink_our_short_sequence = false;
    ctx->multilink_our_endpoint = false;
    while (pos < total) {
        uint8_t opt_type = pkt[pos];
        uint8_t opt_len  = pkt[pos + 1];

        if (opt_len < 2 || pos + opt_len > total)
            return;

        if (opt_type == LCP_OPT_PFC && opt_len == 2)
            ctx->our_pfc = true;
        else if (opt_type == LCP_OPT_ACFC && opt_len == 2)
            ctx->our_acfc = true;
        else if (opt_type == LCP_OPT_COMPOUND_FRAMES && opt_len == 2)
            ctx->compound_rx = true;
        else if (opt_type == LCP_OPT_FCS_ALTERNATIVES && opt_len == 3)
            ctx->fcs_rx32_ok = (pkt[pos + 2] & 0x04) != 0;
        else if (opt_type == LCP_OPT_QUALITY_PROTO && opt_len == 8)
            ctx->lqm_our_active = true;
        else if (opt_type == LCP_OPT_SELF_DESCRIBING_PAD && opt_len == 3) {
            ctx->sdp_our_active = true;
            ctx->sdp_our_max    = pkt[pos + 2];
        } else if (opt_type == LCP_OPT_NUMBERED_MODE && opt_len == 4) {
            ctx->nm_our_active = true;
            ctx->nm_our_window = pkt[pos + 2];
            ctx->nm_our_addr   = pkt[pos + 3];
        } else if (opt_type == LCP_OPT_MRRU && opt_len == 4)
            ctx->multilink_our_mrru = true;
        else if (opt_type == LCP_OPT_SHORT_SEQUENCE && opt_len == 2)
            ctx->multilink_our_short_sequence = true;
        else if (opt_type == LCP_OPT_ENDPOINT_DISC && opt_len >= 3)
            ctx->multilink_our_endpoint = true;

        pos += opt_len;
    }

    ppp_log(ctx->log, "PPP: Received LCP Configure-Ack\n");
    ctx->lcp_ack_received = true;
    ctx->lcp_req_sent = false;
    ctx->lcp_timeout_ms = 0;
    ppp_nm_prepare(ctx);
    ppp_advance_state(ctx);
}

static bool
ppp_lcp_response_matches_request(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len,
                                 bool require_exact_options)
{
    int total;
    int pos = 4;
    int req_pos = 4;

    if (pkt_len < 4 || !ctx->lcp_req_sent || ctx->lcp_request_len < 4
        || ctx->lcp_request_len > sizeof(ctx->lcp_request)
        || pkt[1] != ctx->lcp_request[1])
        return false;

    total = (pkt[2] << 8) | pkt[3];
    if (total != pkt_len || total <= 4)
        return false;

    while (pos < total) {
        uint8_t opt_type = pkt[pos];
        uint8_t opt_len = pkt[pos + 1];

        if (opt_len < 2 || pos + opt_len > total)
            return false;

        while (req_pos < ctx->lcp_request_len
               && ctx->lcp_request[req_pos] != opt_type) {
            uint8_t req_len = ctx->lcp_request[req_pos + 1];

            if (req_len < 2 || req_pos + req_len > ctx->lcp_request_len)
                return false;
            req_pos += req_len;
        }

        if (req_pos >= ctx->lcp_request_len)
            return false;

        uint8_t req_len = ctx->lcp_request[req_pos + 1];
        if (req_len < 2 || req_pos + req_len > ctx->lcp_request_len
            || (req_len != opt_len && opt_type != LCP_OPT_AUTH_PROTO)
            || (require_exact_options
                && (req_len != opt_len
                    || memcmp(ctx->lcp_request + req_pos, pkt + pos, opt_len) != 0)))
            return false;

        req_pos += req_len;
        pos += opt_len;
    }

    return true;
}

/* Handle LCP Configure-Nak from peer */
static void
ppp_handle_lcp_config_nak(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    int      total = (pkt[2] << 8) | pkt[3];
    int      pos = 4;
    uint16_t our_mru = ctx->our_mru;
    uint32_t our_accm = ctx->our_accm;
    uint32_t our_magic = ctx->our_magic;
    ppp_auth_type_t auth_type = ctx->auth_type;
    bool     request_pfc = ctx->request_pfc;
    bool     request_acfc = ctx->request_acfc;
    bool     magic_nak = false;
    bool     nm_request = ctx->nm_request;
    uint8_t  nm_window = ctx->nm_our_window;
    uint8_t  nm_addr = ctx->nm_our_addr;
    uint8_t  sdp_max = ctx->sdp_our_max;

    if (!ppp_lcp_response_matches_request(ctx, pkt, pkt_len, false))
    {
        MODEM_DEBUG_LOG(ctx->log, "LCP: ignored Configure-Nak id=%u (does not match outstanding request id=%u)\n",
                        (unsigned) pkt[1], (unsigned) ctx->lcp_request[1]);
        return;
    }

    while (pos < total) {
        uint8_t opt_type = pkt[pos];
        uint8_t opt_len  = pkt[pos + 1];

        switch (opt_type) {
            case LCP_OPT_MRU:
                if (opt_len != 4)
                    return;
                our_mru = (uint16_t) ((pkt[pos + 2] << 8) | pkt[pos + 3]);
                if (our_mru < PPP_MIN_MRU || our_mru > PPP_MAX_FRAME)
                    return;
                break;

            case LCP_OPT_MRRU:
                if (opt_len != 4)
                    return;
                ctx->multilink_our_mrru_value = (uint16_t) ((pkt[pos + 2] << 8)
                                                            | pkt[pos + 3]);
                if (ctx->multilink_our_mrru_value < PPP_MIN_MRU
                    || ctx->multilink_our_mrru_value > PPP_MAX_FRAME)
                    return;
                break;

            case LCP_OPT_SHORT_SEQUENCE:
                if (opt_len != 2)
                    return;
                ctx->multilink_request_short_sequence = false;
                break;

            case LCP_OPT_ENDPOINT_DISC:
                if (opt_len < 3 || opt_len > 34)
                    return;
                ctx->multilink_request_endpoint = false;
                break;

            case LCP_OPT_ACCM:
                if (opt_len != 6)
                    return;
                our_accm |= ((uint32_t) pkt[pos + 2] << 24)
                          | ((uint32_t) pkt[pos + 3] << 16)
                          | ((uint32_t) pkt[pos + 4] << 8)
                          | (uint32_t) pkt[pos + 5];
                break;

            case LCP_OPT_MAGIC_NUMBER:
                if (opt_len != 6)
                    return;
                magic_nak = true;
                break;

            case LCP_OPT_PFC:
                if (opt_len != 2)
                    return;
                if (ctx->ppp_pfc_mode == 2) {
                    ppp_log(ctx->log, "PPP: Peer cannot satisfy required PFC\n");
                    ctx->state = PPP_STATE_DEAD;
                    ctx->lcp_req_sent = false;
                    return;
                }
                request_pfc = false;
                break;

            case LCP_OPT_ACFC:
                if (opt_len != 2)
                    return;
                if (ctx->ppp_acfc_mode == 2) {
                    ppp_log(ctx->log, "PPP: Peer cannot satisfy required ACFC\n");
                    ctx->state = PPP_STATE_DEAD;
                    ctx->lcp_req_sent = false;
                    return;
                }
                request_acfc = false;
                break;

            case LCP_OPT_AUTH_PROTO:
                if (opt_len < 4)
                    return;
                {
                    uint16_t proto = (uint16_t) ((pkt[pos + 2] << 8) | pkt[pos + 3]);
                    ppp_auth_type_t suggested_auth = PPP_AUTH_NONE;

                    if (proto == PPP_AUTH_PROTO_PAP && opt_len == 4) {
                        suggested_auth = PPP_AUTH_PAP;
                    } else if (proto == PPP_AUTH_PROTO_EAP && opt_len == 4) {
                        suggested_auth = PPP_AUTH_EAP;
                    } else if (proto == PPP_AUTH_PROTO_CHAP && opt_len == 5) {
                        switch (pkt[pos + 4]) {
                            case CHAP_ALG_MD5:      suggested_auth = PPP_AUTH_CHAP_MD5; break;
                            case CHAP_ALG_SHA1:     suggested_auth = PPP_AUTH_CHAP_SHA1; break;
                            case CHAP_ALG_SHA256:   suggested_auth = PPP_AUTH_CHAP_SHA256; break;
                            case CHAP_ALG_SHA384:   suggested_auth = PPP_AUTH_CHAP_SHA384; break;
                            case CHAP_ALG_SHA512:   suggested_auth = PPP_AUTH_CHAP_SHA512; break;
                            case CHAP_ALG_MSCHAP:   suggested_auth = PPP_AUTH_MSCHAP;   break;
                            case CHAP_ALG_MSCHAPV2: suggested_auth = PPP_AUTH_MSCHAPV2; break;
                            default:                break;
                        }
                    }

                    /* In automatic mode a suggestion we cannot use just means
                       "not this one", so carry on down the chain. */
                    if (ctx->auth_auto && ctx->mppe_min_bits == 0
                        && (suggested_auth == PPP_AUTH_NONE
                            || (suggested_auth != auth_type
                                && !ppp_auth_fallback_allowed(ctx, suggested_auth))))
                        suggested_auth = ppp_auth_next(ctx, auth_type);

                    if (suggested_auth == PPP_AUTH_NONE) {
                        ctx->state = PPP_STATE_DEAD;
                        ctx->lcp_req_sent = false;
                        return;
                    }
                    if (suggested_auth != auth_type) {
                        if (!ppp_auth_fallback_allowed(ctx, suggested_auth)) {
                            ppp_log(ctx->log, "PPP: Peer suggested %s instead of configured %s; refusing authentication change\n",
                                    ppp_auth_name(suggested_auth), ppp_auth_name(auth_type));
                            ctx->state = PPP_STATE_DEAD;
                            ctx->lcp_req_sent = false;
                            return;
                        }
                        auth_type = suggested_auth;
                    }
                }
                break;

            case LCP_OPT_FCS_ALTERNATIVES:
                if (opt_len == 3 && (pkt[pos + 2] & 0x06) != 0)
                    ctx->fcs_our_mask = pkt[pos + 2] & 0x06;
                else
                    ctx->fcs_request = false;
                break;

            case LCP_OPT_QUALITY_PROTO:
                /* Accept a different Reporting-Period; any other protocol means no LQM. */
                if (opt_len == 8 && (((uint16_t) pkt[pos + 2] << 8) | pkt[pos + 3]) == PPP_PROTO_LQR
                    && ppp_get32(pkt + pos + 4) != 0)
                    ctx->lqm_our_period_cs = ppp_get32(pkt + pos + 4);
                else
                    ctx->lqm_request = false;
                break;

            case LCP_OPT_CALLBACK:
                /* The answering side does not want callback after all. */
                ctx->cbcp_client_req = false;
                break;

            case LCP_OPT_SELF_DESCRIBING_PAD:
                if (opt_len != 3 || pkt[pos + 2] == 0)
                    return;
                sdp_max = pkt[pos + 2];
                break;

            case LCP_OPT_NUMBERED_MODE:
                if (opt_len != 4)
                    return;
                /* A Nak may only reduce the window; anything else, or an address that is
                   not a valid single-octet ISO 3309 value, means we cannot agree. */
                if (pkt[pos + 2] == 0 || pkt[pos + 2] > nm_window
                    || pkt[pos + 3] == 0 || (pkt[pos + 3] & 1) == 0) {
                    nm_request = false;
                } else {
                    nm_window = pkt[pos + 2];
                    nm_addr   = pkt[pos + 3];
                }
                break;

            default:
                return;
        }
        pos += opt_len;
    }

    if (magic_nak) {
        uint8_t random[4];
        if (!ppp_random_bytes(random, sizeof(random))) {
            ctx->state = PPP_STATE_DEAD;
            ctx->lcp_req_sent = false;
            return;
        }
        our_magic = ((uint32_t) random[0] << 24) | ((uint32_t) random[1] << 16)
                  | ((uint32_t) random[2] << 8) | (uint32_t) random[3];
    }

    ctx->our_mru = our_mru;
    ctx->our_accm = our_accm;
    ctx->our_magic = our_magic;
    ctx->auth_type = auth_type;
    ctx->request_pfc = request_pfc;
    ctx->request_acfc = request_acfc;
    ctx->nm_request = nm_request;
    ctx->nm_our_window = nm_window;
    ctx->nm_our_addr = nm_addr;
    ctx->sdp_our_max = sdp_max;
    ctx->lcp_req_sent = false;
    ctx->lcp_ack_received = false;
    ppp_log(ctx->log, "PPP: Received LCP Configure-Nak\n");

    if (ctx->lcp_retries++ < PPP_LCP_MAX_RETRIES) {
        ppp_send_lcp_config_request(ctx);
    } else {
        ppp_log(ctx->log, "PPP: LCP negotiation failed after Configure-Nak retry limit\n");
        ctx->state = PPP_STATE_DEAD;
    }
}

/* Handle LCP Configure-Reject from peer */
static void
ppp_handle_lcp_config_reject(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    int total = (pkt[2] << 8) | pkt[3];
    int pos   = 4;

    if (!ppp_lcp_response_matches_request(ctx, pkt, pkt_len, true)) {
        MODEM_DEBUG_LOG(ctx->log, "LCP: ignored Configure-Reject id=%u (does not match outstanding request id=%u)\n",
                        (unsigned) pkt[1], (unsigned) ctx->lcp_request[1]);
        return;
    }

    ctx->lcp_req_sent = false;
    ctx->lcp_ack_received = false;
    ppp_log(ctx->log, "PPP: Received LCP Configure-Reject\n");

    while (pos < total && pos < pkt_len) {
        uint8_t opt_type = pkt[pos];
        uint8_t opt_len  = (pos + 1 < pkt_len) ? pkt[pos + 1] : 0;

        if (opt_len < 2 || pos + opt_len > pkt_len)
            break;

        if (opt_type == LCP_OPT_AUTH_PROTO) {
            ppp_auth_type_t fallback = ppp_auth_next(ctx, ctx->auth_type);
            if (fallback == PPP_AUTH_NONE || ctx->mppe_min_bits > 0) {
                ppp_log(ctx->log, "PPP: Peer rejected configured %s authentication; no permitted fallback\n",
                        ppp_auth_name(ctx->auth_type));
                ctx->state = PPP_STATE_DEAD;
                return;
            }
            ppp_log(ctx->log, "PPP: Peer rejected %s authentication; falling back to %s\n",
                    ppp_auth_name(ctx->auth_type), ppp_auth_name(fallback));
            ctx->auth_type = fallback;
        } else if (opt_type == LCP_OPT_PFC) {
            if (ctx->ppp_pfc_mode == 2) {
                ppp_log(ctx->log, "PPP: Peer rejected required PFC\n");
                ctx->state = PPP_STATE_DEAD;
                return;
            }
            ctx->request_pfc = false;
            ctx->our_pfc     = false;
        } else if (opt_type == LCP_OPT_ACFC) {
            if (ctx->ppp_acfc_mode == 2) {
                ppp_log(ctx->log, "PPP: Peer rejected required ACFC\n");
                ctx->state = PPP_STATE_DEAD;
                return;
            }
            ctx->request_acfc = false;
            ctx->our_acfc     = false;
        } else if (opt_type == LCP_OPT_COMPOUND_FRAMES) {
            ctx->compound_request = false;
            ctx->compound_rx      = false;
        } else if (opt_type == LCP_OPT_FCS_ALTERNATIVES) {
            ctx->fcs_request  = false;
            ctx->fcs_rx32_ok  = false;
        } else if (opt_type == LCP_OPT_QUALITY_PROTO) {
            ctx->lqm_request    = false;
            ctx->lqm_our_active = false;
        } else if (opt_type == LCP_OPT_CALLBACK) {
            ctx->cbcp_client_req = false;
        } else if (opt_type == LCP_OPT_NUMBERED_MODE) {
            ctx->nm_request    = false;
            ctx->nm_our_active = false;
        } else if (opt_type == LCP_OPT_SELF_DESCRIBING_PAD) {
            ctx->sdp_request    = false;
            ctx->sdp_our_active = false;
        } else if (opt_type == LCP_OPT_MRRU) {
            ctx->multilink_request_mrru = false;
            ctx->multilink_our_mrru = false;
        } else if (opt_type == LCP_OPT_SHORT_SEQUENCE) {
            ctx->multilink_request_short_sequence = false;
            ctx->multilink_our_short_sequence = false;
        } else if (opt_type == LCP_OPT_ENDPOINT_DISC) {
            ctx->multilink_request_endpoint = false;
            ctx->multilink_our_endpoint = false;
        }
        pos += opt_len;
    }

    /* Resend without rejected options */
    if (ctx->lcp_retries++ < PPP_LCP_MAX_RETRIES) {
        ppp_send_lcp_config_request(ctx);
    } else {
        ppp_log(ctx->log, "PPP: LCP negotiation failed after Configure-Reject retry limit\n");
        ctx->state = PPP_STATE_DEAD;
    }
}

/* Handle LCP Echo-Request */
static void
ppp_handle_lcp_echo_request(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    uint8_t reply[PPP_MAX_FRAME];
    int     total = (pkt[2] << 8) | pkt[3];

    if (total < 4 || total > pkt_len)
        return;

    memcpy(reply, pkt, total);
    reply[0] = PPP_CODE_ECHO_REPLY;

    /* Replace magic number with ours */
    if (total >= 8) {
        reply[4] = (uint8_t) (ctx->our_magic >> 24);
        reply[5] = (uint8_t) (ctx->our_magic >> 16);
        reply[6] = (uint8_t) (ctx->our_magic >> 8);
        reply[7] = (uint8_t) (ctx->our_magic & 0xFF);
    }

    ppp_send_frame(ctx, PPP_PROTO_LCP, reply, total);
    ppp_log(ctx->log, "PPP: Sent LCP Echo-Reply\n");
}

/* Handle LCP Identification; the packet is informational and has no reply. */
static void
ppp_handle_lcp_identification(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    if (pkt_len < 8) {
        ppp_log(ctx->log, "PPP: Received malformed LCP Identification\n");
        return;
    }

    uint32_t magic = ((uint32_t) pkt[4] << 24) | ((uint32_t) pkt[5] << 16)
                     | ((uint32_t) pkt[6] << 8) | pkt[7];
    char   text[128];
    size_t used = 0;
    int    pos;

    for (pos = 8; pos < pkt_len && used + 5 < sizeof(text); pos++) {
        uint8_t character = pkt[pos];
        if (character >= 32 && character <= 126 && character != '"' && character != '\\')
            text[used++] = (char) character;
        else
            used += (size_t) snprintf(text + used, sizeof(text) - used, "\\x%02X",
                                      (unsigned) character);
    }
    text[used] = '\0';
    (void) text;
    (void) magic;
    ppp_log(ctx->log, "PPP: LCP Identification magic=0x%08X message=\"%s\"%s\n",
            (unsigned) magic, text, pos < pkt_len ? "..." : "");
}

/* Handle LCP Terminate-Request */
static void
ppp_handle_lcp_terminate_request(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    uint8_t reply[8];

    (void) pkt_len;

    ppp_log(ctx->log, "PPP: Received LCP Terminate-Request\n");
    reply[0] = PPP_CODE_TERMINATE_ACK;
    reply[1] = pkt[1]; /* echo identifier */
    reply[2] = 0;
    reply[3] = 4;
    ppp_send_frame(ctx, PPP_PROTO_LCP, reply, 4);

    ctx->state = PPP_STATE_DEAD;
}

/* RFC 1661 section 5.6: answer a packet with an unknown Code with Code-Reject. */
void
ppp_send_code_reject(ppp_ctx_t *ctx, uint16_t protocol, const uint8_t *pkt, int pkt_len)
{
    uint8_t reject[4 + 128];
    int     copy;

    if (!ctx || !pkt || pkt_len < 4)
        return;

    copy = pkt_len > 128 ? 128 : pkt_len;
    reject[0] = PPP_CODE_CODE_REJECT;
    reject[1] = ctx->lcp_id++;
    reject[2] = (uint8_t) ((4 + copy) >> 8);
    reject[3] = (uint8_t) (4 + copy);
    memcpy(reject + 4, pkt, (size_t) copy);
    ppp_send_frame(ctx, protocol, reject, 4 + copy);
    ppp_log(ctx->log, "PPP: Sent Code-Reject for code %u on %s\n",
            (unsigned) pkt[0], protocol == PPP_PROTO_LCP ? "LCP"
                             : protocol == PPP_PROTO_IPCP ? "IPCP" : "CCP");
}

/* Process a complete LCP packet */
static void
ppp_process_lcp(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len)
{
    if (pkt_len < 4)
        return;

    uint8_t code = pkt[0];
    int total = (pkt[2] << 8) | pkt[3];
    if (total < 4 || total > pkt_len)
        return;
    pkt_len = total;

    MODEM_DEBUG_LOG(ctx->log, "LCP: received %s (code=%u) id=%u length=%u\n",
                    ppp_lcp_code_name(code), (unsigned) code, (unsigned) pkt[1],
                    (unsigned) ((pkt[2] << 8) | pkt[3]));

    switch (code) {
        case PPP_CODE_CONFIGURE_REQUEST:
            ctx->fcs_default = true; /* RFC 1570: renegotiation reverts to the default FCS */
            if (ppp_handle_lcp_config_request(ctx, pkt, pkt_len))
                ppp_advance_state(ctx);
            break;
        case PPP_CODE_CONFIGURE_ACK:
            ppp_handle_lcp_config_ack(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_CONFIGURE_NAK:
            ppp_handle_lcp_config_nak(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_CONFIGURE_REJECT:
            ppp_handle_lcp_config_reject(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_ECHO_REQUEST:
            ppp_handle_lcp_echo_request(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_IDENTIFICATION:
            ppp_handle_lcp_identification(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_TERMINATE_REQUEST:
            ctx->fcs_default = true;
            ppp_handle_lcp_terminate_request(ctx, pkt, pkt_len);
            break;
        case PPP_CODE_TIME_REMAINING:
            /* RFC 1570: informational only, never answered. */
            if (total >= 12) {
                /* Magic-Number occupies octets 4-7, Seconds-Remaining octets 8-11. */
                uint32_t seconds = ((uint32_t) pkt[8] << 24) | ((uint32_t) pkt[9] << 16)
                                 | ((uint32_t) pkt[10] << 8) | pkt[11];
                (void) seconds;
                ppp_log(ctx->log, "PPP: LCP Time-Remaining: %u seconds\n", (unsigned) seconds);
            }
            break;
        case PPP_CODE_ECHO_REPLY:
        case PPP_CODE_DISCARD_REQUEST:
        case PPP_CODE_TERMINATE_ACK:
        case PPP_CODE_CODE_REJECT: /* never answer a Code-Reject */
            /* Silently ignore */
            break;
        case PPP_CODE_PROTOCOL_REJECT:
            if (total >= 6 && pkt[4] == (uint8_t) (PPP_PROTO_CCP >> 8)
                && pkt[5] == (uint8_t) PPP_PROTO_CCP) {
                if (ctx->mppe_min_bits > 0) {
                    ppp_log(ctx->log, "PPP: Peer rejected CCP while MPPE is required\n");
                    ctx->state = PPP_STATE_DEAD;
                } else {
                    ppp_log(ctx->log, "PPP: Peer rejected CCP; continuing without compression or MPPE\n");
                    ppp_ccp_fallback_plaintext(ctx);
                    ppp_advance_state(ctx);
                }
            } else if (total >= 6) {
                ppp_log(ctx->log, "PPP: Peer rejected protocol %s (0x%04X)\n",
                        ppp_protocol_name((uint16_t) (((uint16_t) pkt[4] << 8) | pkt[5])),
                        (unsigned) (((uint16_t) pkt[4] << 8) | pkt[5]));
                ppp_debug_log_hex(ctx, "PPP: Protocol-Reject data", pkt + 4, total - 4);
            } else {
                ppp_log(ctx->log, "PPP: Received malformed Protocol-Reject\n");
            }
            break;
        default:
            ppp_log(ctx->log, "PPP: Unknown LCP code %d\n", code);
            ppp_send_code_reject(ctx, PPP_PROTO_LCP, pkt, pkt_len);
            break;
    }
}

/* Advance the PPP state machine */
void
ppp_advance_state(ppp_ctx_t *ctx)
{
    if (ctx->state == PPP_STATE_DEAD)
        return;

    if (ctx->lcp_ack_sent && ctx->lcp_ack_received)
        ctx->fcs_default = false; /* LCP is open again: the negotiated FCS applies */

    switch (ctx->state) {
        case PPP_STATE_LCP_NEGOTIATE:
            if (ctx->lcp_ack_sent && ctx->lcp_ack_received) {
                if (ppp_nm_pending(ctx)) {
                    /* RFC 1663: the LAPB link is established before authentication. */
                    ppp_nm_start(ctx);
                    break;
                }
                ppp_log(ctx->log, "PPP: LCP opened, moving to auth phase\n");
                if (ctx->auth_type != PPP_AUTH_NONE && !ctx->auth_complete) {
                    ctx->state = PPP_STATE_AUTH;
                    ctx->auth_timeout_ms = 0;
                    if (ctx->auth_type == PPP_AUTH_EAP) {
                    } else if (ctx->auth_type == PPP_AUTH_CHAP_MD5
                     || ctx->auth_type == PPP_AUTH_CHAP_SHA1
                     || ctx->auth_type == PPP_AUTH_CHAP_SHA256
                     || ctx->auth_type == PPP_AUTH_CHAP_SHA384
                     || ctx->auth_type == PPP_AUTH_CHAP_SHA512
                     || ctx->auth_type == PPP_AUTH_MSCHAP
                     || ctx->auth_type == PPP_AUTH_MSCHAPV2) {
                        ppp_chap_send_challenge(ctx);
                    }
                    /* PAP: server waits for client to send Authenticate-Request */
                } else {
                    if (ppp_cbcp_gate(ctx))
                        break;
                    ctx->auth_complete = true;
                    bool multilink_joined = ppp_multilink_join(ctx);
                    if (ctx->state == PPP_STATE_DEAD)
                        break;
                    if (multilink_joined && !ppp_multilink_is_owner(ctx)) {
                        ctx->state = PPP_STATE_NETWORK;
                        ppp_log(ctx->log, "PPP: Multilink link ready; bundle owner handles IPCP\n");
                        break;
                    }
                    ctx->state         = PPP_STATE_IPCP_NEGOTIATE;
                    ppp_ccp_start(ctx);
                    if (ctx->state == PPP_STATE_DEAD)
                        break;
                    ppp_ipcp_send_config_request(ctx);
                    ppp_ipv6cp_start(ctx);
                }
            }
            break;

        case PPP_STATE_AUTH:
            if (ctx->auth_complete) {
                if (ctx->mppe_min_bits > 0
                    && (!ctx->mppe_keys_ready || ctx->auth_type != PPP_AUTH_MSCHAPV2)) {
                    ppp_log(ctx->log, "PPP: Required MPPE keys unavailable\n");
                    ctx->state = PPP_STATE_DEAD;
                    break;
                }
                if (ppp_cbcp_gate(ctx))
                    break;
                ppp_log(ctx->log, "PPP: Auth complete, moving to IPCP\n");
                bool multilink_joined = ppp_multilink_join(ctx);
                if (ctx->state == PPP_STATE_DEAD)
                    break;
                if (multilink_joined && !ppp_multilink_is_owner(ctx)) {
                    ctx->state = PPP_STATE_NETWORK;
                    ctx->auth_timeout_ms = 0;
                    ppp_log(ctx->log, "PPP: Multilink link ready; bundle owner handles IPCP\n");
                    break;
                }
                ctx->state = PPP_STATE_IPCP_NEGOTIATE;
                ctx->auth_timeout_ms = 0;
                ppp_ccp_start(ctx);
                if (ctx->state == PPP_STATE_DEAD)
                    break;
                ppp_ipcp_send_config_request(ctx);
                ppp_ipv6cp_start(ctx);
            }
            break;

        case PPP_STATE_IPCP_NEGOTIATE:
            if (ctx->ipcp_ack_sent && ctx->ipcp_ack_received) {
                if ((ctx->mppe_min_bits == 0 && !ctx->ccp_req_sent
                     && !ctx->ccp_ack_sent && !ctx->ccp_ack_received)
                    || ctx->ccp_open
                    || (ctx->ccp_plaintext_fallback && ctx->mppe_min_bits == 0)) {
                    ppp_log(ctx->log, "PPP: IPCP opened, entering network phase\n");
                    ctx->state = PPP_STATE_NETWORK;
                }
            }
            break;

        default:
            break;
    }
}

/* LCP Echo-Request keepalive. Only fires on a quiet link: any received frame
   resets the idle and miss counters, so a busy or answering peer is never
   probed more than once per interval and an Echo-Reply needs no special case. */
static void
ppp_keepalive_tick(ppp_ctx_t *ctx)
{
    uint8_t  pkt[8];
    uint32_t interval_ms = (uint32_t) ctx->ppp_keepalive_s * 1000;
    uint8_t  max_missed  = PPP_KEEPALIVE_MAX_MISSED;

    if (ctx->ppp_keepalive_s == 0)
        return;

    if (++ctx->keepalive_idle_ms < interval_ms)
        return;
    ctx->keepalive_idle_ms = 0;

    if (ctx->keepalive_missed >= max_missed) {
        ppp_log(ctx->log, "PPP: Peer did not answer %u keepalive probes; closing link\n",
                (unsigned) ctx->keepalive_missed);
        ctx->state = PPP_STATE_DEAD;
        return;
    }

    ctx->keepalive_missed++;
    pkt[0] = PPP_CODE_ECHO_REQUEST;
    pkt[1] = ctx->lcp_id++;
    pkt[2] = 0;
    pkt[3] = sizeof(pkt);
    pkt[4] = (uint8_t) (ctx->our_magic >> 24);
    pkt[5] = (uint8_t) (ctx->our_magic >> 16);
    pkt[6] = (uint8_t) (ctx->our_magic >> 8);
    pkt[7] = (uint8_t) ctx->our_magic;
    ppp_send_frame(ctx, PPP_PROTO_LCP, pkt, sizeof(pkt));
    ppp_log(ctx->log, "PPP: Sent LCP Echo-Request keepalive (%u/%u)\n",
            (unsigned) ctx->keepalive_missed, (unsigned) max_missed);
}

void
ppp_timer_tick(ppp_ctx_t *ctx)
{
    if (ctx->nm_dm_holdoff_ms > 0)
        ctx->nm_dm_holdoff_ms--;
    if (ctx->lapb) {
        lapb_tick(ctx->lapb);
        if (ctx->state == PPP_STATE_DEAD)
            return;
    }

    ppp_cbcp_tick(ctx);
    if (ctx->state == PPP_STATE_DEAD)
        return;
    ppp_lqm_tick(ctx);
    if (ctx->state == PPP_STATE_IPCP_NEGOTIATE || ctx->state == PPP_STATE_NETWORK) {
        ppp_ipv6cp_tick(ctx);
        ppp_nbfcp_tick(ctx);
        ppp_ipxcp_tick(ctx);
    }

    if (ctx->state == PPP_STATE_AUTH) {
        if (++ctx->auth_timeout_ms >= PPP_AUTH_TIMEOUT_MS) {
            ppp_log(ctx->log, "PPP: Authentication timed out waiting for peer\n");
            ctx->state = PPP_STATE_DEAD;
        }
        return;
    }

    if (ctx->state == PPP_STATE_IPCP_NEGOTIATE) {
        if (ctx->ipcp_req_sent && !ctx->ipcp_ack_received
            && ++ctx->ipcp_timeout_ms >= PPP_LCP_TIMEOUT_MS) {
            ctx->ipcp_timeout_ms = 0;
            if (ctx->ipcp_timeout_retries++ < PPP_LCP_MAX_RETRIES) {
                ppp_log(ctx->log, "PPP: IPCP Configure-Request timed out; retrying (%d/%d)\n",
                        ctx->ipcp_timeout_retries, PPP_LCP_MAX_RETRIES);
                ppp_ipcp_send_config_request(ctx);
            } else if (ctx->nbfcp_active || ctx->ipxcp_active) {
                /* A guest with only NetBEUI or IPX never answers IPCP, and doesn't need to. */
                ppp_log(ctx->log, "PPP: No IPCP from the peer; carrying on without IP\n");
                ctx->ipcp_req_sent = false;
            } else {
                ppp_log(ctx->log, "PPP: IPCP negotiation timed out\n");
                ctx->state = PPP_STATE_DEAD;
                return;
            }
        }
        if (ctx->state == PPP_STATE_IPCP_NEGOTIATE && ctx->ccp_req_sent
            && ++ctx->ccp_timeout_ms >= PPP_LCP_TIMEOUT_MS) {
            ctx->ccp_timeout_ms = 0;
            ppp_log(ctx->log, "PPP: CCP Configure-Request timed out\n");
            ppp_ccp_timeout(ctx);
        }
        return;
    }

    if (ctx->state == PPP_STATE_NETWORK) {
        if (ctx->ccp_req_sent && ++ctx->ccp_timeout_ms >= PPP_LCP_TIMEOUT_MS) {
            ctx->ccp_timeout_ms = 0;
            ppp_log(ctx->log, "PPP: CCP Configure-Request timed out\n");
            ppp_ccp_timeout(ctx);
        }
        ppp_keepalive_tick(ctx);
        return;
    }

    if (ctx->state != PPP_STATE_LCP_NEGOTIATE || !ctx->lcp_req_sent)
        return;

    if (++ctx->lcp_timeout_ms < PPP_LCP_TIMEOUT_MS)
        return;

    ctx->lcp_timeout_ms = 0;
    if (ctx->lcp_retries++ < PPP_LCP_MAX_RETRIES) {
        ppp_log(ctx->log, "PPP: LCP Configure-Request timed out; retrying (%d/%d)\n",
                ctx->lcp_retries, PPP_LCP_MAX_RETRIES);
        ppp_send_lcp_config_request(ctx);
    } else {
        ppp_log(ctx->log, "PPP: LCP negotiation timed out after %d retries\n",
                PPP_LCP_MAX_RETRIES);
        ctx->state = PPP_STATE_DEAD;
        ctx->lcp_req_sent = false;
    }
}

/* Process a complete PPP frame (after HDLC un-escaping and FCS check) */
static void
ppp_ccp_send_reset_request(ppp_ctx_t *ctx)
{
    uint8_t reset_request[6] = {
        PPP_CODE_RESET_REQUEST, 0, 0, 4, 0, 1
    };
    int reset_length = (ctx->ccp_rx_method == PPP_CCP_METHOD_LZS_EXTENDED
                        || ctx->ccp_rx_method == PPP_CCP_METHOD_LZS) ? 6 : 4;

    reset_request[1] = ctx->ccp_reset_pending
                     ? ctx->ccp_reset_request_id : ctx->ccp_reset_id++;
    reset_request[3] = (uint8_t) reset_length;
    ctx->ccp_reset_request_id = reset_request[1];
    ctx->ccp_reset_pending = true;
    ppp_send_frame(ctx, PPP_PROTO_CCP, reset_request, reset_length);
}

/* Process a complete PPP frame (after HDLC un-escaping and FCS check) */
static void
ppp_process_one(ppp_ctx_t *ctx, const uint8_t *frame, int frame_len, bool reassembled)
{
    uint16_t protocol;
    int      data_offset;
    bool     ac_present = false;
    bool     compression_active = ctx->state >= PPP_STATE_AUTH;

    if (frame_len < 1)
        return;

    /* Any intact frame proves the peer is alive (see ppp_keepalive_tick). */
    ctx->keepalive_idle_ms = 0;
    ctx->keepalive_missed  = 0;

    /* LAPB supervisory frames never reach here, so only PPP packets are counted. */
    ctx->lqm_in_packets++;
    ctx->lqm_in_octets += (uint32_t) frame_len;

    /* Check for Address and Control fields */
    if (frame_len >= 2 && frame[0] == PPP_ADDRESS && frame[1] == PPP_CONTROL) {
        ac_present  = true;
        data_offset = 2;
    } else if (compression_active && ctx->our_acfc) {
        data_offset = 0;
    } else {
        ppp_log(ctx->log, "PPP: Frame missing Address/Control fields\n");
        return;
    }

    if (frame_len - data_offset
        > (reassembled ? ctx->multilink_our_mrru_value : ctx->our_mru))
        return;

    if (data_offset >= frame_len)
        return;

    if (frame[data_offset] & 0x01) {
        if (!compression_active || !ctx->our_pfc)
            return;
        protocol = frame[data_offset++];
    } else {
        if (data_offset + 1 >= frame_len)
            return;
        protocol = (uint16_t) ((frame[data_offset] << 8) | frame[data_offset + 1]);
        data_offset += 2;
    }

    if (protocol == PPP_PROTO_LCP && !ac_present)
        return;
    const uint8_t *data     = frame + data_offset;
    int            data_len = frame_len - data_offset;
    uint16_t       wire_protocol = protocol;
    int            wire_data_len = data_len;
#if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
    const char    *decode_method = "none";
#endif
    uint8_t        decrypted[PPP_MAX_FRAME];
    uint8_t        mppc_input[PPP_MAX_FRAME + PPP_MPPE_HEADER_LENGTH];
    uint8_t        decompressed[PPP_MAX_FRAME];

    MODEM_DEBUG_LOG(ctx->log, "PPP: RX wire-protocol=%s (0x%04X) wire-payload=%d "
                    "state=%s CCP-open=%u CCP-method=%s MPPE-rx=%u\n",
                    ppp_protocol_name(wire_protocol), (unsigned) wire_protocol,
                    wire_data_len, ppp_state_name(ctx->state),
                    (unsigned) ctx->ccp_open,
                    ppp_ccp_method_name(ctx->ccp_rx_method),
                    (unsigned) ctx->mppe_rx_enabled);

    if (protocol == PPP_PROTO_MULTILINK) {
        if (ctx->multilink_bundle)
            ppp_mppp_reassembler_input(&ctx->multilink_bundle->reassembler,
                                       data, (size_t) data_len,
                                       ppp_mppp_deliver_packet,
                                       ctx->multilink_bundle);
        else
            ppp_log(ctx->log, "PPP: Received Multilink frame outside a bundle\n");
        return;
    }

    if (ctx->mppe_min_bits > 0
        && ((protocol >= PPP_PROTO_IP && protocol <= 0x00FA)
            || (protocol == PPP_PROTO_MPPE && !ctx->mppe_rx_enabled))) {
        ppp_log(ctx->log, "PPP: Dropping unencrypted network packet while MPPE is required\n");
        return;
    }

    if (protocol == PPP_PROTO_MPPE && ctx->mppe_rx_enabled) {
        size_t decrypted_len;
        if (data_len > PPP_MPPE_HEADER_LENGTH
            && (data[0] & (PPP_MPPE_FLAG_ENCRYPTED | PPP_MPPE_FLAG_FLUSHED)) == PPP_MPPE_FLAG_FLUSHED
            && (data[0] & 0x0F) == 0 && data[1] == 0
            && !ctx->mppe_rx.receive_initialized && ctx->mppe_min_bits == 0
            && ctx->ccp_rx_method == PPP_CCP_METHOD_MPPC
            && ctx->ccp_tx_method == PPP_CCP_METHOD_MPPC && !ctx->ccp_peer_mppe) {
            /* A peer may Ack MPPE but actually send MPPC only. Renegotiate
               before accepting plaintext; never weaken required encryption. */
            ppp_log(ctx->log, "PPP: Peer sent unencrypted MPPC; renegotiating without optional MPPE\n");
            ppp_ccp_retry_mppc_without_mppe(ctx);
            return;
        }
        if (!ctx->mppe_rx_enabled
            || !ppp_mppe_decrypt(&ctx->mppe_rx, data, (size_t) data_len,
                                 decrypted, sizeof(decrypted), &decrypted_len)) {
            if (ctx->mppe_rx.reset_requested) {
                uint8_t reset_request[4] = {
                    PPP_CODE_RESET_REQUEST, ctx->ccp_reset_id++, 0, 4
                };
                ppp_send_frame(ctx, PPP_PROTO_CCP, reset_request, sizeof(reset_request));
                ctx->mppe_rx.reset_requested = false;
            }
            return;
        }
    #if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
        decode_method = "MPPE";
    #endif
        if (ctx->ccp_rx_method == PPP_CCP_METHOD_MPPC) {
            int decompressed_len;
            uint16_t count = (uint16_t) (((data[0] & 0x0F) << 8) | data[1]);
            mppc_input[0] = (uint8_t) ((data[0] & (PPP_MPPE_FLAG_FLUSHED
                                                   | PPP_MPPE_FLAG_AT_FRONT
                                                   | PPP_MPPE_FLAG_COMPRESSED))
                                       | (count >> 8));
            mppc_input[1] = (uint8_t) count;
            memcpy(mppc_input + PPP_MPPE_HEADER_LENGTH, decrypted, decrypted_len);
            if (!ppp_ccp_codec_decompress(ctx, mppc_input,
                                         (int) decrypted_len + PPP_MPPE_HEADER_LENGTH,
                                          decompressed, sizeof(decompressed),
                                          &decompressed_len)
                || decompressed_len < 2)
                return;
            protocol = (uint16_t) (((uint16_t) decompressed[0] << 8)
                                 | decompressed[1]);
            data = decompressed + 2;
            data_len = decompressed_len - 2;
        } else {
            if (decrypted_len < 2)
                return;
            protocol = (uint16_t) (((uint16_t) decrypted[0] << 8) | decrypted[1]);
            data = decrypted + 2;
            data_len = (int) decrypted_len - 2;
        }
        if (protocol < PPP_PROTO_IP || protocol > 0x00FA)
            return;
    } else if (protocol == PPP_PROTO_MPPE
               && ctx->ccp_open
               && (ctx->ccp_rx_method == PPP_CCP_METHOD_PREDICTOR1
                   || ctx->ccp_rx_method == PPP_CCP_METHOD_PREDICTOR2
                   || ctx->ccp_rx_method == PPP_CCP_METHOD_LZS
                   || ctx->ccp_rx_method == PPP_CCP_METHOD_LZS_EXTENDED
                   || ctx->ccp_rx_method == PPP_CCP_METHOD_LZS_DCP
                   || ctx->ccp_rx_method == PPP_CCP_METHOD_V44
                   || ctx->ccp_rx_method == PPP_CCP_METHOD_DEFLATE
                         || ctx->ccp_rx_method == PPP_CCP_METHOD_MPPC
                         || ctx->ccp_rx_method == PPP_CCP_METHOD_BSD)) {
        int decompressed_len;
                #if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
                    decode_method = ppp_ccp_method_name(ctx->ccp_rx_method);
                #endif
        if (ctx->ccp_reset_pending
            && (ctx->ccp_rx_method == PPP_CCP_METHOD_PREDICTOR2
                || ctx->ccp_rx_method == PPP_CCP_METHOD_LZS))
            return;
        if (ctx->ccp_rx_method == PPP_CCP_METHOD_PREDICTOR2) {
            bool first_segment = true;
            for (;;) {
                if (!ppp_ccp_codec_decompress(ctx, first_segment ? data : NULL,
                                              first_segment ? data_len : 0,
                                              decompressed, sizeof(decompressed),
                                              &decompressed_len)) {
                    if (!ctx->ccp_reset_pending)
                        ppp_ccp_send_reset_request(ctx);
                    return;
                }
                first_segment = false;
                if (decompressed_len == 0)
                    return;
                if (decompressed_len < 2) {
                    if (!ctx->ccp_reset_pending)
                        ppp_ccp_send_reset_request(ctx);
                    return;
                }

                uint16_t decoded_protocol = (uint16_t) (((uint16_t) decompressed[0] << 8)
                                                       | decompressed[1]);
                if (decoded_protocol < PPP_PROTO_IP || decoded_protocol > 0x00FA)
                    continue;

                uint8_t decoded_frame[PPP_MAX_FRAME + 4] = {
                    PPP_ADDRESS, PPP_CONTROL,
                    (uint8_t) (decoded_protocol >> 8), (uint8_t) decoded_protocol
                };
                memcpy(decoded_frame + 4, decompressed + 2, (size_t) decompressed_len - 2);
                ppp_process_one(ctx, decoded_frame, decompressed_len + 2, reassembled);
            }
        }
        if (!ppp_ccp_codec_decompress(ctx, data, data_len, decompressed,
                                      sizeof(decompressed), &decompressed_len)
            || decompressed_len < 2) {
            if ((ctx->ccp_rx_method == PPP_CCP_METHOD_LZS_EXTENDED
                 || ctx->ccp_rx_method == PPP_CCP_METHOD_LZS
                 || !ctx->ccp_reset_pending)
                && ctx->ccp_rx_method != PPP_CCP_METHOD_LZS_DCP
                && ctx->ccp_rx_method != PPP_CCP_METHOD_V44)
                ppp_ccp_send_reset_request(ctx);
            return;
        }
        if (ctx->ccp_rx_method == PPP_CCP_METHOD_BSD
            || ctx->ccp_rx_method == PPP_CCP_METHOD_DEFLATE) {
            if (decompressed[0] & 1) {
                protocol = decompressed[0];
                data = decompressed + 1;
                data_len = decompressed_len - 1;
            } else {
                if (decompressed_len < 2)
                    return;
                protocol = (uint16_t) (((uint16_t) decompressed[0] << 8) | decompressed[1]);
                data = decompressed + 2;
                data_len = decompressed_len - 2;
            }
        } else {
            protocol = (uint16_t) (((uint16_t) decompressed[0] << 8) | decompressed[1]);
            data = decompressed + 2;
            data_len = decompressed_len - 2;
        }
        if (protocol < PPP_PROTO_IP || protocol > 0x00FA)
            return;
    } else if (protocol == PPP_PROTO_MPPE) {
        /* MPPE control frames are not handled here; they are usually processed
           during the decryption phase or ignored if not needed. */
        return;
    } else if (ctx->mppe_keys_ready && !ctx->ccp_plaintext_fallback && ctx->ccp_open
               && protocol >= PPP_PROTO_IP && protocol <= 0x00FA
               && ctx->mppe_rx_enabled) {
        /* If we're here, it means an IP packet arrived that SHOULD have been encrypted,
           but it's arriving in plaintext (or vice versa). For compatibility with Windows 95,
           we allow it to pass through instead of dropping it. */
        MODEM_DEBUG_LOG(ctx->log, "PPP: Allowing potentially mis-encrypted packet (proto=0x%04X)\\\n", protocol);
    } else if (ctx->mppe_keys_ready && !ctx->ccp_plaintext_fallback && !ctx->ccp_open
               && protocol >= PPP_PROTO_IP && protocol <= 0x00FA) {
        /* Similarly, allow packets when CCP is not yet open but MPPE keys are ready. */
        MODEM_DEBUG_LOG(ctx->log, "PPP: Allowing potentially mis-encrypted packet (proto=0x%04X, CCP closed)\\\n", protocol);
    }

            ppp_log(ctx->log, "PPP: Received frame proto=%s (0x%04X) len=%d (state=%s)\n",
                ppp_protocol_name(protocol), protocol, data_len, ppp_state_name(ctx->state));
        if (protocol != wire_protocol || data_len != wire_data_len) {
            MODEM_DEBUG_LOG(ctx->log, "PPP: RX decoded protocol=%s (0x%04X) payload=%d "
                            "from-wire=%s (0x%04X) wire-payload=%d transform=%s\n",
                            ppp_protocol_name(protocol), (unsigned) protocol, data_len,
                            ppp_protocol_name(wire_protocol), (unsigned) wire_protocol,
                            wire_data_len, decode_method);
        } else {
            MODEM_DEBUG_LOG(ctx->log, "PPP: RX protocol=%s (0x%04X) payload=%d "
                            "address-control=%u compression=%u state=%s\n",
                            ppp_protocol_name(protocol), (unsigned) protocol, data_len,
                            (unsigned) ac_present, (unsigned) compression_active,
                            ppp_state_name(ctx->state));
        }

    switch (protocol) {
        case PPP_PROTO_LCP:
            ppp_process_lcp(ctx, data, data_len);
            break;

        case PPP_PROTO_PAP:
            if (ctx->state == PPP_STATE_AUTH
                || (ctx->state >= PPP_STATE_IPCP_NEGOTIATE
                    && ctx->state <= PPP_STATE_NETWORK
                    && ctx->auth_type == PPP_AUTH_PAP && ctx->auth_complete
                    && ctx->pap_last_request_valid)) {
                if (ctx->state == PPP_STATE_AUTH)
                    ctx->auth_timeout_ms = 0;
                ppp_pap_process(ctx, data, data_len);
            } else {
                MODEM_DEBUG_LOG(ctx->log, "PPP: Ignoring PAP packet outside auth phase (state=%s length=%d)\n",
                                ppp_state_name(ctx->state), data_len);
            }
            break;

        case PPP_PROTO_CHAP:
            if (ctx->state == PPP_STATE_AUTH) {
                ctx->auth_timeout_ms = 0;
                ppp_chap_process(ctx, data, data_len);
            } else {
                MODEM_DEBUG_LOG(ctx->log, "PPP: Ignoring CHAP packet outside auth phase (state=%s length=%d)\n",
                                ppp_state_name(ctx->state), data_len);
            }
            break;

        case PPP_PROTO_EAP:
            if (ctx->state == PPP_STATE_AUTH && ctx->auth_type == PPP_AUTH_EAP) {
                ctx->auth_timeout_ms = 0;
                ppp_eap_process(ctx, data, data_len);
            }
            break;

        case PPP_PROTO_IPCP:
            if (ctx->state >= PPP_STATE_IPCP_NEGOTIATE) {
                ppp_ipcp_process(ctx, data, data_len);
            }
            break;

        case PPP_PROTO_CCP:
            if (ctx->state >= PPP_STATE_IPCP_NEGOTIATE && ctx->state < PPP_STATE_TERMINATING)
                ppp_ccp_process(ctx, data, data_len);
            break;

        case PPP_PROTO_LQR:
            if (ctx->state >= PPP_STATE_AUTH && ctx->state < PPP_STATE_TERMINATING)
                ppp_lqm_process(ctx, data, data_len);
            break;

        case PPP_PROTO_CBCP:
            if (ctx->state >= PPP_STATE_AUTH && ctx->state < PPP_STATE_TERMINATING
                && ctx->ppp_callback_mode != PPP_CALLBACK_DISABLED)
                ppp_cbcp_process(ctx, data, data_len);
            break;

        case PPP_PROTO_IP:
            if (ctx->state == PPP_STATE_NETWORK
                && (!ctx->multilink_bundle || ppp_multilink_is_owner(ctx))) {
                ctx->network_send_ip(ctx->modem, data, data_len);
            }
            break;

        case PPP_PROTO_IPV6CP:
            if (ctx->ppp_ipv6_mode != PPP_IPV6_DISABLED && ctx->state >= PPP_STATE_IPCP_NEGOTIATE
                && ctx->state < PPP_STATE_TERMINATING) {
                ppp_ipv6cp_process(ctx, data, data_len);
                break;
            }
            goto unknown_protocol;

        case PPP_PROTO_IPV6:
            if (ctx->ppp_ipv6_mode != PPP_IPV6_DISABLED) {
                if (ppp_ipv6_ready(ctx) && ctx->network_send_ip6
                    && (!ctx->multilink_bundle || ppp_multilink_is_owner(ctx)))
                    ctx->network_send_ip6(ctx->modem, data, data_len);
                break;
            }
            goto unknown_protocol;

        case PPP_PROTO_NBFCP:
            if (ctx->network_send_nbf && ctx->state >= PPP_STATE_IPCP_NEGOTIATE
                && ctx->state < PPP_STATE_TERMINATING
                && (!ctx->multilink_bundle || ppp_multilink_is_owner(ctx))) {
                ppp_nbfcp_process(ctx, data, data_len);
                break;
            }
            goto unknown_protocol;

        case PPP_PROTO_NBF:
            if (ctx->network_send_nbf) {
                ppp_nbf_receive(ctx, data, data_len);
                break;
            }
            goto unknown_protocol;

        case PPP_PROTO_IPXCP:
            if (ctx->network_send_ipx && ctx->state >= PPP_STATE_IPCP_NEGOTIATE
                && ctx->state < PPP_STATE_TERMINATING
                && (!ctx->multilink_bundle || ppp_multilink_is_owner(ctx))) {
                ppp_ipxcp_process(ctx, data, data_len);
                break;
            }
            goto unknown_protocol;

        case PPP_PROTO_IPX:
            if (ctx->network_send_ipx) {
                ppp_ipx_receive(ctx, data, data_len);
                break;
            }
            goto unknown_protocol;

        case PPP_PROTO_VJ_COMPRESSED:
        case PPP_PROTO_VJ_UNCOMPRESSED:
            if (ctx->state == PPP_STATE_NETWORK && ctx->vj_rx_enabled && ctx->vj_rx_ctx
                && (!ctx->multilink_bundle || ppp_multilink_is_owner(ctx))) {
                uint8_t ip_packet[PPP_MAX_FRAME + VJ_MAX_HDR];
                uint8_t normalized[PPP_MAX_FRAME];
                int     vj_type = protocol == PPP_PROTO_VJ_COMPRESSED
                                ? VJ_TYPE_COMPRESSED_TCP : VJ_TYPE_UNCOMPRESSED_TCP;

                if (protocol == PPP_PROTO_VJ_UNCOMPRESSED) {
                    if (data_len < 20 || data[9] > ctx->vj_rx_max_slot_id)
                        break;
                    memcpy(normalized, data, data_len);
                    normalized[9] |= VJ_TYPE_UNCOMPRESSED_TCP;
                    data = normalized;
                }

                ctx->vj_rx_ctx->num_slots = (int) ctx->vj_rx_max_slot_id + 1;
                int ip_len = cslip_decompress(ctx->vj_rx_ctx, data, data_len,
                                              ip_packet, vj_type);
                if (ip_len > 0)
                    ctx->network_send_ip(ctx->modem, ip_packet, ip_len);
            }
            break;

        default:
        unknown_protocol:
            /* Send Protocol-Reject for unknown protocols */
            if (ctx->state >= PPP_STATE_LCP_NEGOTIATE) {
                uint8_t reject[PPP_MAX_FRAME];
                int     rlen = 4;
                reject[0] = PPP_CODE_PROTOCOL_REJECT;
                reject[1] = ctx->lcp_id++;
                /* Include rejected protocol and up to 64 bytes of data */
                reject[rlen++] = (uint8_t) (protocol >> 8);
                reject[rlen++] = (uint8_t) (protocol & 0xFF);
                int copy = (data_len > 64) ? 64 : data_len;
                memcpy(reject + rlen, data, copy);
                rlen += copy;
                reject[2] = (uint8_t) (rlen >> 8);
                reject[3] = (uint8_t) (rlen & 0xFF);
                ppp_send_frame(ctx, PPP_PROTO_LCP, reject, rlen);
                ppp_log(ctx->log, "PPP: Sent Protocol-Reject for 0x%04X\n", protocol);
            }
            break;
    }
}

/* Length of a PPP packet whose size can be read from its own header, or -1. */
static int
ppp_packet_length(uint16_t protocol, const uint8_t *data, int avail)
{
    int length;

    switch (protocol) {
        case PPP_PROTO_LCP:
        case PPP_PROTO_PAP:
        case PPP_PROTO_CHAP:
        case PPP_PROTO_EAP:
        case PPP_PROTO_IPCP:
        case PPP_PROTO_CCP:
        case PPP_PROTO_CBCP:
        case PPP_PROTO_IPV6CP:
        case PPP_PROTO_NBFCP:
        case PPP_PROTO_IPXCP:
            if (avail < 4)
                return -1;
            length = (data[2] << 8) | data[3];
            return (length >= 4 && length <= avail) ? length : -1;
        case PPP_PROTO_LQR:
            return avail >= PPP_LQR_LENGTH ? PPP_LQR_LENGTH : -1;
        case PPP_PROTO_IP:
            if (avail < 20 || (data[0] >> 4) != 4)
                return -1;
            length = (data[2] << 8) | data[3];
            return (length >= 20 && length <= avail) ? length : -1;
        case PPP_PROTO_IPV6:
            if (avail < 40 || (data[0] >> 4) != 6)
                return -1;
            length = 40 + ((data[4] << 8) | data[5]);
            return (length > 40 && length <= avail) ? length : -1;
        default:
            return -1;
    }
}

/* RFC 1570 Compound-Frames: a frame may carry several packets, each with its own
   Protocol field, as long as every packet but the last has a determinate length. */
static void
ppp_process_compound(ppp_ctx_t *ctx, const uint8_t *frame, int frame_len, int depth)
{
    uint8_t  rest[PPP_MAX_FRAME + 4];
    int      header = 0;
    int      plen;
    uint16_t protocol;
    int      avail;
    int      length;

    if (frame_len >= 2 && frame[0] == PPP_ADDRESS && frame[1] == PPP_CONTROL)
        header = 2;
    else if (!ctx->our_acfc)
        goto whole;

    if (header >= frame_len)
        goto whole;
    if (frame[header] & 0x01) {
        if (!ctx->our_pfc)
            goto whole;
        protocol = frame[header];
        plen     = 1;
    } else {
        if (header + 1 >= frame_len)
            goto whole;
        protocol = (uint16_t) ((frame[header] << 8) | frame[header + 1]);
        plen     = 2;
    }
    avail  = frame_len - header - plen;
    length = ppp_packet_length(protocol, frame + header + plen, avail);

    if (depth < 16 && length > 0 && length < avail) {
        int cut  = header + plen + length;
        int left = frame_len - cut;

        if (left + 2 > (int) sizeof(rest))
            return;
        ppp_process_one(ctx, frame, cut, false);
        if (ctx->state == PPP_STATE_DEAD)
            return;
        rest[0] = PPP_ADDRESS;
        rest[1] = PPP_CONTROL;
        memcpy(rest + 2, frame + cut, (size_t) left);
        ppp_process_compound(ctx, rest, left + 2, depth + 1);
        return;
    }

whole:
    ppp_process_one(ctx, frame, frame_len, false);
}

/* Entry point for every received frame: Numbered-Mode, then Self-Describing-Pad
   removal and Compound-Frames splitting, then the single-packet processor. */
static void
ppp_process_frame(ppp_ctx_t *ctx, const uint8_t *frame, int frame_len, bool reassembled)
{
    uint8_t unpadded[PPP_MAX_FRAME + 4];

    if (frame_len < 1)
        return;

    /* Numbered-Mode frames (I, S and U frames with a negotiated Address). */
    if (!reassembled && ppp_nm_rx(ctx, frame, frame_len))
        return;

    if (reassembled || ctx->state < PPP_STATE_AUTH
        || !(ctx->sdp_our_active || ctx->compound_rx)) {
        ppp_process_one(ctx, frame, frame_len, reassembled);
        return;
    }

    /* RFC 1570 Self-Describing-Pad: the final octet counts the pad, and the pad octets
       are 1, 2, ... up to that count. A final octet outside 1..Maximum means no pad.
       A pad whose indexes are wrong is discarded with the frame. */
    if (ctx->sdp_our_active) {
        uint8_t pad = frame[frame_len - 1];

        if (pad >= 1 && pad <= ctx->sdp_our_max) {
            if (frame_len - pad < 3) {
                ppp_log(ctx->log, "PPP: Discarding frame shorter than its self-describing pad\n");
                return;
            }
            for (int k = 0; k < pad; k++) {
                if (frame[frame_len - pad + k] != (uint8_t) (k + 1)) {
                    ppp_log(ctx->log, "PPP: Discarding frame with a malformed self-describing pad\n");
                    return;
                }
            }
            if (frame_len - pad > (int) sizeof(unpadded))
                return;
            memcpy(unpadded, frame, (size_t) (frame_len - pad));
            frame     = unpadded;
            frame_len -= pad;
        }
    }

    if (ctx->compound_rx && ctx->sdp_our_active)
        ppp_process_compound(ctx, frame, frame_len, 0);
    else
        ppp_process_one(ctx, frame, frame_len, false);
}


/* Process a byte received from the serial line (HDLC framing) */
void
ppp_rx_byte(ppp_ctx_t *ctx, uint8_t byte)
{
    if (ctx->ras_rx_in_frame) {
        if (ctx->ras_rx_len >= (int) sizeof(ctx->ras_rx_buf)) {
            ctx->ras_rx_in_frame = false;
            ctx->ras_rx_len = 0;
            ctx->ras_rx_expected = 0;
            return;
        }
        ctx->ras_rx_buf[ctx->ras_rx_len++] = byte;
        if (ctx->ras_rx_len == 4) {
            int body_length = (ctx->ras_rx_buf[2] << 8) | ctx->ras_rx_buf[3];
            ctx->ras_rx_expected = body_length + 7;
            if (body_length < 3 || ctx->ras_rx_expected > (int) sizeof(ctx->ras_rx_buf)) {
                ctx->ras_rx_in_frame = false;
                ctx->ras_rx_len = 0;
                ctx->ras_rx_expected = 0;
                return;
            }
        }
        if (ctx->ras_rx_expected > 0 && ctx->ras_rx_len == ctx->ras_rx_expected) {
            ppp_process_ras_frame(ctx, ctx->ras_rx_buf, ctx->ras_rx_len);
            ctx->ras_rx_in_frame = false;
            ctx->ras_rx_len = 0;
            ctx->ras_rx_expected = 0;
        }
        return;
    }

    if (ctx->ccp_rx_method == PPP_CCP_METHOD_NT31RAS && ctx->rx_len == 0
        && byte == PPP_RAS_SYN) {
        ctx->ras_rx_buf[0] = byte;
        ctx->ras_rx_len = 1;
        ctx->ras_rx_expected = 0;
        ctx->ras_rx_in_frame = true;
        return;
    }

    if (byte == PPP_FLAG) {
        if (ctx->rx_in_frame && ctx->rx_len > 2) {
            /* End of frame - check FCS */
            uint16_t calc_fcs = ppp_fcs16(ctx->rx_buf, ctx->rx_len - 2);
            uint16_t recv_fcs = (uint16_t) (ctx->rx_buf[ctx->rx_len - 2])
                              | ((uint16_t) (ctx->rx_buf[ctx->rx_len - 1]) << 8);

            if (calc_fcs == recv_fcs) {
                MODEM_DEBUG_LOG(ctx->log, "PPP: HDLC frame complete length=%d FCS valid\n", ctx->rx_len);
                ppp_process_frame(ctx, ctx->rx_buf, ctx->rx_len - 2, false);
            } else if (ctx->fcs_rx32_ok && ctx->rx_len > 4
                       && ppp_fcs32(ctx->rx_buf, ctx->rx_len - 4)
                              == ((uint32_t) ctx->rx_buf[ctx->rx_len - 4]
                                  | ((uint32_t) ctx->rx_buf[ctx->rx_len - 3] << 8)
                                  | ((uint32_t) ctx->rx_buf[ctx->rx_len - 2] << 16)
                                  | ((uint32_t) ctx->rx_buf[ctx->rx_len - 1] << 24))) {
                ppp_process_frame(ctx, ctx->rx_buf, ctx->rx_len - 4, false);
            } else {
                ppp_log(ctx->log, "PPP: FCS error (calc=0x%04X recv=0x%04X)\n", calc_fcs, recv_fcs);
                ctx->lqm_in_errors++;
            }
        }
        /* Start new frame */
        ctx->rx_len      = 0;
        ctx->rx_in_frame = true;
        ctx->rx_escaped  = false;
        return;
    }

    if (!ctx->rx_in_frame)
        return;

    if (byte == PPP_ESCAPE) {
        ctx->rx_escaped = true;
        return;
    }

    if (ctx->rx_escaped) {
        byte ^= 0x20;
        ctx->rx_escaped = false;
    }

    if (ctx->rx_len < PPP_MAX_FRAME)
        ctx->rx_buf[ctx->rx_len++] = byte;
}

/* Wrap an IP packet in PPP HDLC framing for delivery to the guest */
void
ppp_wrap_ip(ppp_ctx_t *ctx, const uint8_t *ip_pkt, int len)
{
    if (ctx->state != PPP_STATE_NETWORK)
        return;
    if (!ip_pkt || len <= 0 || len > PPP_MAX_FRAME - 6) {
        ppp_log(ctx->log, "PPP: Dropping invalid IP packet (%d bytes)\n", len);
        return;
    }
    if (ctx->vj_tx_enabled && ctx->vj_tx_ctx && len > 0
        && ctx->ccp_tx_method != PPP_CCP_METHOD_NT31RAS) {
        uint8_t compressed[PPP_MAX_FRAME + VJ_MAX_HDR];
        int     type = VJ_TYPE_IP;

        ctx->vj_tx_ctx->num_slots = (int) ctx->vj_tx_max_slot_id + 1;
        ctx->vj_tx_ctx->compress_slot_id = ctx->vj_tx_comp_slot_id;
        int compressed_len = cslip_compress(ctx->vj_tx_ctx, ip_pkt, len, compressed, &type);
        if (compressed_len > 0 && type == VJ_TYPE_COMPRESSED_TCP) {
            ppp_send_frame(ctx, PPP_PROTO_VJ_COMPRESSED, compressed, compressed_len);
            return;
        }
        if (compressed_len > 0 && type == VJ_TYPE_UNCOMPRESSED_TCP) {
            compressed[9] &= VJ_MAX_SLOTS - 1;
            ppp_send_frame(ctx, PPP_PROTO_VJ_UNCOMPRESSED, compressed, compressed_len);
            return;
        }
    }

    ppp_send_frame(ctx, PPP_PROTO_IP, ip_pkt, len);
}

/* True once IPv6CP is open and the link is in the network phase. */
bool
ppp_ipv6_ready(const ppp_ctx_t *ctx)
{
    return ctx && ctx->state == PPP_STATE_NETWORK && ctx->ipv6cp_active && ctx->ipv6cp_open;
}

/* Carry an IPv6 packet toward the peer. Dropped unless IPv6CP is open. */
void
ppp_wrap_ip6(ppp_ctx_t *ctx, const uint8_t *ip_pkt, int len)
{
    if (!ppp_ipv6_ready(ctx))
        return;
    if (!ip_pkt || len < 40 || len > PPP_MAX_FRAME - 6) {
        ppp_log(ctx->log, "PPP: Dropping invalid IPv6 packet (%d bytes)\n", len);
        return;
    }
    ppp_send_frame(ctx, PPP_PROTO_IPV6, ip_pkt, len);
}

/* Initialize PPP context */
ppp_ctx_t *
ppp_init(void *modem, void *log,
         void (*serial_push)(void *, const uint8_t *, int),
         void (*network_send_ip)(void *, const uint8_t *, int))
{
    uint8_t   magic[4];
    ppp_ctx_t *ctx = (ppp_ctx_t *) calloc(1, sizeof(ppp_ctx_t));
    if (!ctx)
        return NULL;

    if (!ppp_random_bytes(magic, sizeof(magic))) {
        free(ctx);
        return NULL;
    }

    ctx->modem           = modem;
    ctx->log             = log;
    ctx->serial_push     = serial_push;
    ctx->network_send_ip = network_send_ip;
    ctx->state           = PPP_STATE_DEAD;
    ctx->our_mru         = PPP_DEFAULT_MRU;
    ctx->peer_mru        = PPP_DEFAULT_MRU;
    /* ACCM we request for data the peer sends us. Our receiver accepts escaped and
       unescaped control characters alike, so ask for none to escape. peer_accm
       below stays all-ones: until the peer says otherwise we escape everything. */
    ctx->our_accm        = 0x00000000;
    ctx->peer_accm       = 0xFFFFFFFF;
    ctx->our_magic       = ((uint32_t) magic[0] << 24) | ((uint32_t) magic[1] << 16)
                         | ((uint32_t) magic[2] << 8) | (uint32_t) magic[3];
    ctx->auth_type       = PPP_AUTH_NONE;
    ctx->request_pfc     = true;
    ctx->request_acfc    = true;
    ctx->ppp_pfc_mode    = PPP_LCP_COMPRESSION_OFFER;
    ctx->ppp_acfc_mode   = PPP_LCP_COMPRESSION_OFFER;
    ctx->vj_tx_ctx       = cslip_init(log);
    ctx->vj_rx_ctx       = cslip_init(log);
    if (!ctx->vj_tx_ctx || !ctx->vj_rx_ctx) {
        cslip_close(ctx->vj_tx_ctx);
        cslip_close(ctx->vj_rx_ctx);
        ctx->vj_tx_ctx = NULL;
        ctx->vj_rx_ctx = NULL;
    }

    /* Default IP configuration (SLiRP defaults) */
    ctx->our_ip  = 0x0A000202; /* 10.0.2.2 */
    ctx->peer_ip = 0x0A00020F; /* 10.0.2.15 */
    ctx->dns1    = 0x0A000203; /* 10.0.2.3 */
    ctx->dns2    = 0x0A000203; /* 10.0.2.3 */

    return ctx;
}

void
ppp_close(ppp_ctx_t *ctx)
{
    if (ctx) {
        ppp_multilink_detach(ctx);
        if (ctx->state != PPP_STATE_DEAD && ctx->state != PPP_STATE_TERMINATING) {
            /* Send Terminate-Request */
            uint8_t pkt[4];
            pkt[0] = PPP_CODE_TERMINATE_REQUEST;
            pkt[1] = ctx->lcp_id++;
            pkt[2] = 0;
            pkt[3] = 4;
            ppp_send_frame(ctx, PPP_PROTO_LCP, pkt, 4);
        }
        cslip_close(ctx->vj_tx_ctx);
        cslip_close(ctx->vj_rx_ctx);
        ppp_ccp_codec_close(ctx);
        lapb_destroy(ctx->lapb);
        free(ctx);
    }
}

/* Start PPP negotiation (called when entering PPP mode) */
void
ppp_start(ppp_ctx_t *ctx)
{
    /* With no credentials there is nothing to check a challenge response against, so
       CHAP and EAP could only ever fail. PAP already accepts anyone in this case.
       Required MPPE keeps its method: its keys come from the (empty) password. */
    if (ctx->username[0] == '\0' && ctx->password[0] == '\0'
        && ctx->auth_type != PPP_AUTH_NONE && ctx->auth_type != PPP_AUTH_PAP
        && ctx->mppe_min_bits == 0) {
        ppp_log(ctx->log, "PPP: No credentials configured; not authenticating the peer\n");
        ctx->auth_type = PPP_AUTH_NONE;
    }

    if (ctx->mppe_min_bits > 0 && ctx->auth_type != PPP_AUTH_MSCHAPV2) {
        ppp_log(ctx->log, "PPP: Required MPPE needs MS-CHAPv2 authentication\n");
        ctx->state = PPP_STATE_DEAD;
        return;
    }

    ctx->state            = PPP_STATE_LCP_NEGOTIATE;
    ctx->lcp_peer_heard   = false;
    ctx->lcp_ack_sent     = false;
    ctx->lcp_ack_received = false;
    ctx->lcp_req_sent     = false;
    ctx->lcp_retries      = 0;
    ctx->lcp_timeout_ms   = 0;
    ctx->auth_complete    = false;
    ctx->pap_last_request_len = 0;
    ctx->pap_last_request_valid = false;
    ctx->auth_timeout_ms  = 0;
    ctx->our_pfc          = false;
    ctx->our_acfc         = false;
    ctx->peer_pfc         = false;
    ctx->peer_acfc        = false;
    ctx->multilink_request_mrru = ctx->multilink_group[0] != '\0';
    ctx->multilink_request_short_sequence = ctx->multilink_group[0] != '\0';
    ctx->multilink_request_endpoint = ctx->multilink_group[0] != '\0';
    ctx->multilink_our_mrru = false;
    ctx->multilink_peer_mrru = false;
    ctx->multilink_our_short_sequence = false;
    ctx->multilink_peer_short_sequence = false;
    ctx->multilink_our_endpoint = false;
    ctx->multilink_peer_endpoint = false;
    ctx->multilink_our_mrru_value = PPP_DEFAULT_MRU + 2;
    ctx->multilink_peer_mrru_value = PPP_DEFAULT_MRU + 2;
    ctx->multilink_peer_endpoint_length = 0;
    ctx->rx_in_frame      = false;
    ctx->rx_len           = 0;
    ctx->rx_escaped       = false;
    ctx->eap_state        = PPP_EAP_STATE_IDLE;
    ctx->eap_id           = 0;
    ctx->eap_request_id   = 0;
    ctx->ipcp_ack_sent    = false;
    ctx->ipcp_ack_received = false;
    ctx->ipcp_req_sent    = false;
    ctx->ipcp_retries     = 0;
    ctx->ipcp_timeout_ms  = 0;
    ctx->ipcp_timeout_retries = 0;
    ctx->nbfcp_active     = false;
    ctx->nbfcp_open       = false;
    ctx->nbfcp_req_sent   = false;
    ctx->nbfcp_ack_sent   = false;
    ctx->nbfcp_ack_received = false;
    ctx->nbfcp_mac_required = false;
    ctx->nbfcp_timeout_ms = 0;
    ctx->nbfcp_retries    = 0;
    ctx->ipxcp_active     = false;
    ctx->ipxcp_open       = false;
    ctx->ipxcp_req_sent   = false;
    ctx->ipxcp_ack_sent   = false;
    ctx->ipxcp_ack_received = false;
    ctx->ipxcp_timeout_ms = 0;
    ctx->ipxcp_retries    = 0;
    ctx->ipv6cp_active    = false;
    ctx->ipv6cp_started   = false;
    ctx->ipv6cp_open      = false;
    ctx->ipv6cp_req_sent  = false;
    ctx->ipv6cp_ack_sent  = false;
    ctx->ipv6cp_ack_received = false;
    ctx->ipv6cp_send_iid  = true;
    ctx->ipv6cp_retries   = 0;
    ctx->ipv6cp_timeout_ms = 0;
    ctx->ccp_timeout_ms   = 0;
    ctx->ccp_timeout_retries = 0;
    ctx->keepalive_idle_ms = 0;
    ctx->keepalive_missed = 0;
    ctx->ipcp_vj_request  = ctx->ipcp_vj_request && ctx->vj_tx_ctx && ctx->vj_rx_ctx;
    if (ctx->ipcp_vj_mode == 2 && !ctx->ipcp_vj_request) {
        ppp_log(ctx->log, "PPP: Required Van Jacobson compression is unavailable\n");
        ctx->state = PPP_STATE_DEAD;
        return;
    }
    ctx->vj_tx_enabled    = false;
    ctx->vj_rx_enabled    = false;
    ctx->vj_tx_max_slot_id = IPCP_VJ_MAX_SLOT_ID;
    ctx->vj_rx_max_slot_id = IPCP_VJ_MAX_SLOT_ID;
    ctx->vj_tx_comp_slot_id = ctx->ipcp_vj_mode != 2;
    ctx->vj_rx_comp_slot_id = ctx->ipcp_vj_mode != 2;
    ctx->ccp_req_sent = false;
    ctx->ccp_ack_sent = false;
    ctx->ccp_ack_received = false;
    ctx->ccp_peer_mppe = false;
    ctx->ccp_open = false;
    ctx->ccp_rejected = false;
    ctx->ccp_plaintext_fallback = false;
    ctx->mppe_tx_enabled = false;
    ctx->mppe_rx_enabled = false;

    if (ctx->lapb) {
        lapb_destroy(ctx->lapb);
        ctx->lapb = NULL;
    }
    /* RFC 1663 forbids ACFC with Numbered-Mode, so a required ACFC rules it out. */
    ctx->nm_request       = ctx->ppp_numbered_mode == PPP_NUMBERED_ENABLED && ctx->ppp_acfc_mode != 2;
    ctx->nm_mirrored      = false;
    ctx->nm_our_window    = ctx->ppp_numbered_window ? ctx->ppp_numbered_window : 7;
    if (ctx->nm_our_window > 127)
        ctx->nm_our_window = 127;
    ctx->nm_our_addr      = 1;
    ctx->nm_our_active    = false;
    ctx->nm_peer_active   = false;
    ctx->nm_peer_window   = 0;
    ctx->nm_peer_addr     = 0;
    ctx->nm_started       = false;
    ctx->nm_dm_holdoff_ms = 0;
    ctx->fcs_request      = ctx->ppp_fcs_mode == PPP_FCS_32;
    ctx->fcs_our_mask     = 0x06; /* we can receive CCITT 16-bit and 32-bit FCS */
    ctx->fcs_rx32_ok      = false;
    ctx->fcs_tx32         = false;
    ctx->fcs_default      = false;
    ctx->lqm_request      = ctx->ppp_lqm_mode == PPP_LQM_REQUEST;
    ctx->lqm_our_period_cs = ctx->ppp_lqm_period_cs ? ctx->ppp_lqm_period_cs : PPP_LQM_DEFAULT_PERIOD_CS;
    ctx->lqm_our_active   = false;
    ctx->lqm_peer_active  = false;
    ctx->lqm_peer_period_cs = 0;
    ctx->lqm_timer_ms     = 0;
    ctx->lqm_out_lqrs = ctx->lqm_out_packets = ctx->lqm_out_octets = 0;
    ctx->lqm_in_lqrs = ctx->lqm_in_packets = ctx->lqm_in_discards = ctx->lqm_in_errors = ctx->lqm_in_octets = 0;
    ctx->lqm_save_in_lqrs = ctx->lqm_save_in_packets = ctx->lqm_save_in_discards = 0;
    ctx->lqm_save_in_errors = ctx->lqm_save_in_octets = 0;
    ctx->lqm_last_out_lqrs = ctx->lqm_last_out_packets = ctx->lqm_last_out_octets = 0;
    ctx->cbcp_peer        = false;
    ctx->cbcp_client_req  = ctx->ppp_callback_mode == PPP_CALLBACK_CLIENT;
    ctx->cbcp_phase       = PPP_CBCP_IDLE;
    ctx->cbcp_id          = 0;
    ctx->cbcp_timeout_ms  = 0;
    ctx->cbcp_retries     = 0;
    ctx->cbcp_hold_ms     = 0;
    ctx->callback_pending = false;
    ctx->callback_delay_s = 0;
    ctx->callback_number[0] = '\0';
    /* Compound-Frames needs Self-Describing-Pad in both directions. Multilink fragments
       are not padded, so neither option is offered on a bundle. */
    if (ctx->ppp_compound_mode == PPP_COMPOUND_ACCEPT && ctx->ppp_sdp_mode < PPP_SDP_REQUEST)
        ctx->ppp_sdp_mode = PPP_SDP_REQUEST;
    if (ctx->ppp_compound_mode == PPP_COMPOUND_AUTOMATIC && ctx->ppp_sdp_mode == PPP_SDP_DISABLED)
        ctx->ppp_sdp_mode = PPP_SDP_ACCEPT;
    ctx->compound_request = ctx->ppp_compound_mode == PPP_COMPOUND_ACCEPT && !ctx->multilink_group[0];
    ctx->compound_mirrored = false;
    ctx->compound_rx      = false;
    ctx->sdp_request      = ctx->ppp_sdp_mode == PPP_SDP_REQUEST && !ctx->multilink_group[0];
    ctx->sdp_our_max      = PPP_SDP_DEFAULT_MAX;
    ctx->sdp_our_active   = false;
    ctx->sdp_peer_active  = false;
    ctx->sdp_peer_max     = 0;

    ppp_send_lcp_config_request(ctx);
}

/* Apply the MPPE key-strength policy and specification format.

   Standard (RFC 3078/3079): 40-, 56- and 128-bit keys, and a peer that sets the
   obsolete D bit in its CCP option is Nak'd to clear it.

   Legacy draft: the pre-RFC MPPE drafts only defined 40- and 128-bit keys (56-bit
   came with RFC 3078), so 56-bit is removed from what we offer or accept, and
   a peer's D bit is tolerated instead of Nak'd (see ccp_handle_config_request).
   If 56-bit was the only permitted strength, the allowed set becomes empty while
   the minimum stays set, so a connection that requires MPPE fails rather than
   silently running unencrypted. */
void
ppp_mppe_set_policy(ppp_ctx_t *ctx, uint32_t bitmask, uint8_t format)
{
    uint32_t allowed;
    uint8_t  min_bits;

    if (!ctx)
        return;

    ctx->mppe_prefer_strongest = false;
    if (bitmask == CCP_MPPE_AUTOMATIC) {
        allowed  = CCP_MPPE_KEY_BITS;
        min_bits = 0;
    } else if (bitmask == CCP_MPPE_REQUIRED_ANY) {
        allowed  = CCP_MPPE_KEY_BITS;
        min_bits = 40;
        ctx->mppe_prefer_strongest = true;
    } else {
        allowed  = bitmask;
        min_bits = ppp_mppe_minimum_strength(bitmask);
    }

    if (format == PPP_MPPE_FORMAT_LEGACY_DRAFT) {
        uint32_t legacy = allowed & ~CCP_MPPE_56;

        if (legacy & CCP_MPPE_KEY_BITS) {
            allowed = legacy;
            if (min_bits > 0)
                min_bits = ppp_mppe_minimum_strength(legacy);
        } else if (allowed & CCP_MPPE_KEY_BITS) {
            allowed = legacy;
        }
    }

    ctx->mppe_format       = format;
    ctx->mppe_allowed_bits = allowed;
    ctx->mppe_min_bits     = min_bits;
}
