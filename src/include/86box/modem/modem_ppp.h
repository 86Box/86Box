/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PPP (Point-to-Point Protocol) definitions for modem emulation.
 *          HDLC-like framing, FCS-16, LCP (RFC 1661/1662), IPCP (RFC 1332),
 *          CCP (RFC 1962), Stac LZS (RFC 1974), MPPC (RFC 2118), BSD-Compress
 *          (RFC 1977), Deflate (RFC 1979), Predictor (RFC 1978), MPPE (RFC 3078), PAP (RFC 1334),
 *          CHAP (RFC 1994), EAP (RFC 3748), and Van Jacobson compression.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_PPP_H
#define EMU_MODEM_PPP_H

#ifdef __cplusplus
extern "C" {
#endif

struct cslip_ctx_t;

#define PPP_MPPE_KEY_LENGTH 16
#define PPP_PAP_MAX_REQUEST_LENGTH (4 + 1 + 255 + 1 + 255)

typedef struct {
    uint8_t s[256];
    uint8_t i;
    uint8_t j;
} ppp_mppe_rc4_state_t;

typedef struct ppp_mppe_state_t {
    uint8_t  start_key[PPP_MPPE_KEY_LENGTH];
    uint8_t  mschapv1_weak_start_key[8];
    uint8_t  session_key[PPP_MPPE_KEY_LENGTH];
    ppp_mppe_rc4_state_t rc4;
    uint16_t next_count;
    uint16_t last_count;
    uint8_t  key_bits;
    uint8_t  key_length;
    bool     stateful;
    bool     receive_initialized;
    bool     force_reset;
    bool     discard;
    bool     reset_requested;
    bool     mschapv1_key_derivation;
} ppp_mppe_state_t;

/* PPP HDLC framing constants */
#define PPP_FLAG      0x7E       /* Async HDLC Flag Sequence (RFC 1662) */
#define PPP_ESCAPE    0x7D       /* Asynchronous Control Octet Escape (RFC 1662) */
#define PPP_TRANS     0x20       /* Control Octet Transparency Modifier (XOR value, RFC 1662) */
#define PPP_ADDRESS   0xFF       /* All-Stations Address Field (RFC 1662) */
#define PPP_CONTROL   0x03       /* Unnumbered Information (UI) Control Field (RFC 1662) */
#define PPP_INITFCS   0xFFFF     /* Initial FCS 16-bit CRC Seed (RFC 1662) */
#define PPP_GOODFCS   0xF0B8     /* Valid 16-bit FCS Checksum Constant (RFC 1662) */
#define PPP_INITFCS32 0xFFFFFFFF /* Initial FCS 32-bit CRC Seed (RFC 1662) */
#define PPP_GOODFCS32 0xDEBB20E3 /* Valid 32-bit FCS Checksum Constant (RFC 1662) */

/* Microsoft Point-to-Point Encryption flags (RFC 3078) */
#define CCP_MPPE_STATELESS 0x01000000u
#define CCP_MPPE_D         0x00000010u /* Obsolete MPPE option bit (RFC 3078) */
#define CCP_MPPE_40        0x00000020u
#define CCP_MPPE_128       0x00000040u
#define CCP_MPPE_56        0x00000080u
#define CCP_MPPE_KEY_BITS  (CCP_MPPE_128 | CCP_MPPE_56 | CCP_MPPE_40)
#define CCP_MPPE_DISABLED  0u
#define CCP_MPPE_AUTOMATIC 0x20000000u /* Config selector: accept any strength, choose strongest offered */
#define CCP_MPPE_REQUIRED_ANY 0x40000000u /* Config selector: as automatic, but the link fails without MPPE */
#define CCP_MPPC           0x00000001u
#define CCP_MPPE_OFFER     (CCP_MPPE_STATELESS | CCP_MPPE_KEY_BITS)
#define CCP_NT31RAS_FEATURES 0x0000000Fu
#define CCP_NT31RAS_WINDOW_8K_FEATURE  0x00000001u
#define CCP_NT31RAS_WINDOW_16K_FEATURE 0x00000002u
#define CCP_NT31RAS_WINDOW_32K_FEATURE 0x00000004u
#define CCP_NT31RAS_WINDOW_64K_FEATURE 0x00000008u
#define CCP_NT31RAS_WINDOW_8K  8192u
#define CCP_NT31RAS_WINDOW_16K 16384u
#define CCP_NT31RAS_WINDOW_32K 32768u
#define CCP_NT31RAS_WINDOW_64K 65536u

/* CCP option numbers */
#define CCP_OPT_PREDICTOR1 PPP_CCP_METHOD_PREDICTOR1
#define CCP_OPT_PREDICTOR2 PPP_CCP_METHOD_PREDICTOR2
#define CCP_OPT_LZS        PPP_CCP_METHOD_LZS
#define CCP_OPT_MPPE       PPP_CCP_METHOD_MPPE
#define CCP_OPT_BSD        PPP_CCP_METHOD_BSD
#define CCP_OPT_LZS_DCP    PPP_CCP_METHOD_LZS_DCP
#define CCP_OPT_DEFLATE    PPP_CCP_METHOD_DEFLATE
#define CCP_OPT_DEFLATE_DRAFT 24 /* Option number used by the original Deflate draft (pppd: CI_DEFLATE_DRAFT); same 4-byte format as option 26 */
#define CCP_OPT_V44        PPP_CCP_METHOD_V44
#define CCP_OPT_NT31RAS    PPP_CCP_METHOD_NT31RAS

static inline uint8_t
ppp_mppe_minimum_strength(uint32_t bitmask)
{
    if (bitmask & CCP_MPPE_40)
        return 40;
    if (bitmask & CCP_MPPE_56)
        return 56;
    if (bitmask & CCP_MPPE_128)
        return 128;
    return 0;
}

/* PPP Protocol Numbers */
#define PPP_PROTO_IP              0x0021 /* Internet Protocol (RFC 1661) */
#define PPP_PROTO_VJ_COMPRESSED   0x002D /* Van Jacobson Compressed TCP/IP (RFC 1144 / RFC 1332) */
#define PPP_PROTO_VJ_UNCOMPRESSED 0x002F /* Van Jacobson Uncompressed TCP/IP (RFC 1144 / RFC 1332) */
#define PPP_PROTO_IPV6            0x0057 /* Internet Protocol Version 6 (RFC 5072) */
#define PPP_PROTO_COMPRESSED      0x00FB /* Compressed Datagram / Standard CCP Encapsulated Payload (RFC 1962) */
#define PPP_PROTO_MPPE            0x00FD /* Compressed Datagram / MPPE Payload (RFC 1962 / RFC 2419) */
#define PPP_PROTO_IPCP            0x8021 /* Internet Protocol Control Protocol (RFC 1332) */
#define PPP_PROTO_MPLSCP          0x802B /* MPLS Control Protocol (RFC 3032) */
#define PPP_PROTO_IPV6CP          0x8057 /* IPv6 Control Protocol (RFC 5072) */
#define PPP_PROTO_NBF             0x003F /* NetBIOS Frames (RFC 2097) */
#define PPP_PROTO_NBFCP           0x803F /* NetBIOS Frames Control Protocol (RFC 2097) */
#define PPP_PROTO_IPX             0x002B /* Internetwork Packet Exchange (RFC 1552) */
#define PPP_PROTO_IPXCP           0x802B /* IPX Control Protocol (RFC 1552) */
#define PPP_PROTO_IPV6            0x0057 /* Internet Protocol version 6 (RFC 5072) */
#define PPP_PROTO_CCP             0x80FD /* Compression Control Protocol (RFC 1962) */
#define PPP_PROTO_LCP             0xC021 /* Link Control Protocol (RFC 1661) */
#define PPP_PROTO_PAP             0xC023 /* Password Authentication Protocol (RFC 1334) */
#define PPP_PROTO_SPAP            0xC027 /* Shiva Password Authentication Protocol */
#define PPP_PROTO_LQR             0xC025 /* Link Quality Report (RFC 1989) */
#define PPP_PROTO_CBCP            0xC029 /* Callback Control Protocol (RFC 1570) */
#define PPP_PROTO_BAP             0xC02D /* Bandwidth Allocation Protocol (RFC 2125) */
#define PPP_PROTO_CHAP            0xC223 /* Challenge Handshake Authentication Protocol (RFC 1994) */
#define PPP_PROTO_EAP             0xC227 /* Extensible Authentication Protocol (RFC 2284 / RFC 3748) */
#define PPP_PROTO_BACP            0xC22B /* Bandwidth Allocation Control Protocol (RFC 2125) */

/* LCP/IPCP Packet Codes */
#define PPP_CODE_VENDOR_EXTENSION   0 /* Vendor-Specific Code (RFC 2153) */
#define PPP_CODE_CONFIGURE_REQUEST  1 /* Configure-Request (RFC 1661) */
#define PPP_CODE_CONFIGURE_ACK      2 /* Configure-Ack (RFC 1661) */
#define PPP_CODE_CONFIGURE_NAK      3 /* Configure-Nak (RFC 1661) */
#define PPP_CODE_CONFIGURE_REJECT   4 /* Configure-Reject (RFC 1661) */
#define PPP_CODE_TERMINATE_REQUEST  5 /* Terminate-Request (RFC 1661) */
#define PPP_CODE_TERMINATE_ACK      6 /* Terminate-Ack (RFC 1661) */
#define PPP_CODE_CODE_REJECT        7 /* Code-Reject (RFC 1661) */
#define PPP_CODE_PROTOCOL_REJECT    8 /* Protocol-Reject (LCP Only, RFC 1661) */
#define PPP_CODE_ECHO_REQUEST       9 /* Echo-Request (LCP Only, RFC 1661) */
#define PPP_CODE_ECHO_REPLY        10 /* Echo-Reply (LCP Only, RFC 1661) */
#define PPP_CODE_DISCARD_REQUEST   11 /* Discard-Request (LCP Only, RFC 1661) */
#define PPP_CODE_IDENTIFICATION    12 /* Identification (LCP Only, RFC 1570) */
#define PPP_CODE_TIME_REMAINING    13 /* Time-Remaining (LCP Only, RFC 1570) */
#define PPP_CODE_RESET_REQUEST     14 /* Reset-Request (CCP/NCP Specific, RFC 1962) */
#define PPP_CODE_RESET_ACK         15 /* Reset-Ack (CCP/NCP Specific, RFC 1962) */
#define PPP_CODE_SKIP_ALGORITHM    16 /* Skip-Algorithm (LCP/CCP Extensions) */
#define PPP_CODE_BONDING_COMM      17 /* Bandwidth Allocation Protocol (BAP) / Bonding Request */

/* LCP option types */
#define LCP_OPT_VENDOR_SPECIFIC        0 /* Vendor-Specific (RFC 2153) */
#define LCP_OPT_MRU                    1 /* Maximum-Receive-Unit (RFC 1661) */
#define LCP_OPT_ACCM                   2 /* Async-Control-Character-Map (RFC 1662) */
#define LCP_OPT_AUTH_PROTO             3 /* Authentication-Protocol (RFC 1661) */
#define LCP_OPT_QUALITY_PROTO          4 /* Quality-Protocol (RFC 1661) */
#define LCP_OPT_MAGIC_NUMBER           5 /* Magic-Number (RFC 1661) */
#define LCP_OPT_QUALITY_PROTO_OLD      6 /* Deprecated Quality-Protocol (RFC 1172) */
#define LCP_OPT_PFC                    7 /* Protocol-Field-Compression (RFC 1661) */
#define LCP_OPT_ACFC                   8 /* Address-and-Control-Field-Compression (RFC 1661) */
#define LCP_OPT_FCS_ALTERNATIVES       9 /* FCS-Alternatives (RFC 1570) */
#define LCP_OPT_SELF_DESCRIBING_PAD   10 /* Self-Describing-Pad (RFC 1570) */
#define LCP_OPT_NUMBERED_MODE         11 /* Numbered-Mode (RFC 1663) */
#define LCP_OPT_MULTILINK_PROCEDURE   12 /* Deprecated Multi-Link-Procedure (RFC 1351) */
#define LCP_OPT_CALLBACK              13 /* Callback (RFC 1570) */
#define LCP_OPT_CONNECT_TIME          14 /* Deprecated Connect-Time (RFC 1570) */
#define LCP_OPT_COMPOUND_FRAMES       15 /* Deprecated Compound-Frames (RFC 1570) */
#define LCP_OPT_NOMINAL_DATA_ENCAP    16 /* Deprecated Nominal-Data-Encapsulation (RFC 1570) */
#define LCP_OPT_MRRU                  17 /* Multilink Maximum-Receive-Reconstructed-Unit (RFC 1990) */
#define LCP_OPT_SHORT_SEQUENCE        18 /* Multilink Short-Sequence-Number-Header (RFC 1990) */
#define LCP_OPT_ENDPOINT_DISC         19 /* Multilink Endpoint Discriminator (RFC 1990) */
#define LCP_OPT_PROPRIETARY           20 /* Proprietary (RFC 1570) */
#define LCP_OPT_DCE_IDENTIFIER        21 /* DCE-Identifier (RFC 1976) */
#define LCP_OPT_MULTILINK_PLUS        22 /* Multi-Link-Plus-Procedure (RFC 1934) */
#define LCP_OPT_BACP_LINK_DISC        23 /* Link Discriminator for BACP (RFC 2125) */
#define LCP_OPT_LCP_AUTH              24 /* LCP-Authentication-Option (RFC 1570 / RFC 2484) */
#define LCP_OPT_COBS                  25 /* Consistent Overhead Byte Stuffing (RFC 2823) */
#define LCP_OPT_PREFIX_ELISION        26 /* Prefix elision (RFC 2686) */
#define LCP_OPT_MULTILINK_HEADER      27 /* Multilink header format (RFC 2686) */
#define LCP_OPT_INTERNATIONALIZATION  28 /* Internationalization (RFC 2484) */
#define LCP_OPT_SIMPLE_SONET_LINK     29 /* Simple Data Link on SONET/SDH (RFC 2823) */
#define LCP_OPT_LOG_OR_STATUS_COLLECT 30 /* Management-Option / Log or Status Collection */

/* IPCP option types */
#define IPCP_OPT_IP_ADDRESSES            1 /* Deprecated IP-Addresses (RFC 1172 / RFC 1332) */
#define IPCP_OPT_IP_COMPRESSION          2 /* IP-Compression-Protocol (RFC 1332) */
#define IPCP_OPT_IP_ADDRESS              3 /* IP-Address (RFC 1332)*/
#define IPCP_OPT_MOBILE_IPV4             4 /* Mobile-IPv4 (RFC 2290)*/
#define IPCP_OPT_DNS_PRIMARY           129 /* Primary DNS Server Address (RFC 1877) */
#define IPCP_OPT_NBNS_PRIMARY          130 /* Primary NetBIOS Name Server Address (RFC 1877) */
#define IPCP_OPT_DNS_SECONDARY         131 /* Secondary DNS Server Address (RFC 1877) */
#define IPCP_OPT_NBNS_SECONDARY        132 /* Secondary NetBIOS Name Server Address (RFC 1877) */
#define IPCP_OPT_IP_ADDRESS_ALLOCATION 144 /* IP-Address-Allocation / AT&T (RFC 2153) */

#define IPCP_VJ_MAX_SLOT_ID  15 /* Maximum Van Jacobson header compression slot index (RFC 1144, 16 total slots: 0-15) */
#define IPCP_VJ_COMP_SLOT_ID  1 /* Slot ID Compression flag bit in IPCP Option 2 (RFC 1332, 1 = allow slot ID compression) */

/* Auth protocol values for LCP option 3 */
#define PPP_AUTH_PROTO_SPAP_OLD            0xC021 /* Shiva Password Authentication Protocol (Early/Alternate Assignment) */
#define PPP_AUTH_PROTO_PAP                 PPP_PROTO_PAP /* Password Authentication Protocol */
#define PPP_AUTH_PROTO_SHIVA_PAP           PPP_PROTO_SPAP /* Shiva Password Authentication Protocol */
#define PPP_AUTH_PROTO_DES_AUTH            0xC221 /* Individual Software Products DES Authentication Protocol */
#define PPP_AUTH_PROTO_CHAP                PPP_PROTO_CHAP /* Challenge-Handshake Authentication Protocol */
#define PPP_AUTH_PROTO_RSA                 0xC225 /* RSA Authentication Protocol */
#define PPP_AUTH_PROTO_EAP                 PPP_PROTO_EAP /* Extensible Authentication Protocol */
#define PPP_AUTH_PROTO_MITSUBISHI_SIEP     0xC229 /* Mitsubishi Security Info Exchange Protocol */
#define PPP_AUTH_PROTO_VSAP                0xC05B /* Vendor-Specific Authentication Protocol */
#define PPP_AUTH_PROTO_PUBLIC_KEY          0xC26F /* Stampede Technologies Public Key Authentication Protocol */
#define PPP_AUTH_PROTO_PROPRIETARY_C281    0xC281 /* Proprietary Authentication Protocol */
#define PPP_AUTH_PROTO_PROPRIETARY_C283    0xC283 /* Proprietary Authentication Protocol */
#define PPP_AUTH_PROTO_NORTEL_MSAP         0xC285 /* Nortel Proprietary Authentication Protocol (MSAP) */
#define PPP_AUTH_PROTO_PROPRIETARY_NODE_ID 0xC481 /* Proprietary Node ID Authentication Protocol */

/* CHAP algorithm values */
#define CHAP_ALG_MD5        5   /* CHAP with MD5 (RFC 1994) */
#define CHAP_ALG_SHA1       6   /* CHAP with SHA-1 */
#define CHAP_ALG_SHA256     7   /* CHAP with SHA-256 */
#define CHAP_ALG_SHA3_256   8   /* CHAP with SHA3-256 */
#define CHAP_ALG_SHA384     9   /* CHAP with SHA-384 */
#define CHAP_ALG_SHA3_384  10   /* CHAP with SHA3-384 */
#define CHAP_ALG_SHA512    11   /* CHAP with SHA-512 */
#define CHAP_ALG_SHA3_512  12   /* CHAP with SHA3-512 */
#define CHAP_ALG_MSCHAP    0x80 /* Microsoft CHAP version 1 (RFC 2433) */
#define CHAP_ALG_MSCHAPV2  0x81 /* Microsoft CHAP version 2 (RFC 2759) */

/* PPP state machine */
typedef enum {
    PPP_STATE_DEAD,
    PPP_STATE_LCP_NEGOTIATE,
    PPP_STATE_AUTH,
    PPP_STATE_IPCP_NEGOTIATE,
    PPP_STATE_NETWORK,
    PPP_STATE_TERMINATING
} ppp_state_t;

/* Authentication method */
typedef enum {
    PPP_AUTH_NONE,
    PPP_AUTH_PAP,
    PPP_AUTH_CHAP_MD5,
    PPP_AUTH_MSCHAP,
    PPP_AUTH_MSCHAPV2,
    PPP_AUTH_CHAP_SHA1,
    PPP_AUTH_CHAP_SHA256,
    PPP_AUTH_CHAP_SHA384,
    PPP_AUTH_CHAP_SHA512,
    PPP_AUTH_EAP
} ppp_auth_type_t;

/* Config values retain the historical extra MS-CHAP/MPPE selection. */
typedef enum {
    PPP_AUTH_CONFIG_NONE = PPP_AUTH_NONE,
    PPP_AUTH_CONFIG_PAP = PPP_AUTH_PAP,
    PPP_AUTH_CONFIG_CHAP_MD5 = PPP_AUTH_CHAP_MD5,
    PPP_AUTH_CONFIG_MSCHAP = PPP_AUTH_MSCHAP,
    PPP_AUTH_CONFIG_MSCHAPV2 = PPP_AUTH_MSCHAPV2,
    PPP_AUTH_CONFIG_MSCHAP_MPPE = PPP_AUTH_CONFIG_MSCHAPV2 + 1,
    PPP_AUTH_CONFIG_CHAP_SHA1 = PPP_AUTH_CHAP_SHA1 + 1,
    PPP_AUTH_CONFIG_CHAP_SHA256 = PPP_AUTH_CHAP_SHA256 + 1,
    PPP_AUTH_CONFIG_CHAP_SHA384 = PPP_AUTH_CHAP_SHA384 + 1,
    PPP_AUTH_CONFIG_CHAP_SHA512 = PPP_AUTH_CHAP_SHA512 + 1,
    PPP_AUTH_CONFIG_EAP = PPP_AUTH_EAP + 1,
    PPP_AUTH_CONFIG_AUTOMATIC = PPP_AUTH_CONFIG_EAP + 1 /* Strongest method the peer accepts */
} ppp_auth_config_t;

static inline ppp_auth_type_t
ppp_auth_type_from_config(int config_value)
{
    switch (config_value) {
        case PPP_AUTH_CONFIG_PAP:         return PPP_AUTH_PAP;
        case PPP_AUTH_CONFIG_CHAP_MD5:    return PPP_AUTH_CHAP_MD5;
        case PPP_AUTH_CONFIG_MSCHAP:      return PPP_AUTH_MSCHAP;
        case PPP_AUTH_CONFIG_MSCHAPV2:
        case PPP_AUTH_CONFIG_MSCHAP_MPPE: /* no longer offered; kept for saved configurations */
        case PPP_AUTH_CONFIG_AUTOMATIC:   return PPP_AUTH_MSCHAPV2;
        case PPP_AUTH_CONFIG_CHAP_SHA1:   return PPP_AUTH_CHAP_SHA1;
        case PPP_AUTH_CONFIG_CHAP_SHA256: return PPP_AUTH_CHAP_SHA256;
        case PPP_AUTH_CONFIG_CHAP_SHA384: return PPP_AUTH_CHAP_SHA384;
        case PPP_AUTH_CONFIG_CHAP_SHA512: return PPP_AUTH_CHAP_SHA512;
        case PPP_AUTH_CONFIG_EAP:         return PPP_AUTH_EAP;
        case PPP_AUTH_CONFIG_NONE:
        default:                          return PPP_AUTH_NONE;
    }
}

typedef enum {
    PPP_COMPRESSION_DISABLED = 0,
    PPP_COMPRESSION_PREFER_LZS = 1,
    PPP_COMPRESSION_PREFER_MPPC = 2,
    PPP_COMPRESSION_PREFER_PREDICTOR = 3,
    PPP_COMPRESSION_PREFER_MPPC_LZS = 4
} ppp_compression_preference_t;

typedef enum {
    PPP_CCP_MODE_ENABLED = 0,
    PPP_CCP_MODE_DISABLED = 1,
    PPP_CCP_MODE_PASSIVE = 2
} ppp_ccp_mode_t;

typedef enum {
    PPP_CCP_DIRECTION_BIDIRECTIONAL = 0,
    PPP_CCP_DIRECTION_TRANSMIT_ONLY = 1,
    PPP_CCP_DIRECTION_RECEIVE_ONLY = 2,
    PPP_CCP_DIRECTION_ASYMMETRIC_PREFERRED = 3
} ppp_ccp_direction_t;

typedef enum {
    PPP_VJ_COMPRESSION_DISABLED = 0,
    PPP_VJ_COMPRESSION_ALLOWED = 1,
    PPP_VJ_COMPRESSION_REQUIRED = 2
} ppp_vj_compression_mode_t;

typedef enum {
    PPP_MPPE_STATE_OFFER_STATELESS = 0,
    PPP_MPPE_STATE_OFFER_STATEFUL = 1,
    PPP_MPPE_STATE_REQUIRE_STATELESS = 2,
    PPP_MPPE_STATE_REQUIRE_STATEFUL = 3,
    PPP_MPPE_STATE_AUTO_STATELESS_PREFERRED = 4,
    PPP_MPPE_STATE_AUTO_STATEFUL_PREFERRED = 5
} ppp_mppe_key_state_mode_t;

typedef enum {
    PPP_MPPE_FORMAT_STANDARD = 0,
    PPP_MPPE_FORMAT_LEGACY_DRAFT = 1,
    PPP_MPPE_FORMAT_AUTOMATIC = 2 /* Standard, adapting if the peer turns out to follow the drafts */
} ppp_mppe_format_t;

typedef enum {
    PPP_LCP_COMPRESSION_DISABLED = 0,
    PPP_LCP_COMPRESSION_OFFER = 1,
    PPP_LCP_COMPRESSION_REQUIRE = 2
} ppp_lcp_compression_mode_t;

typedef enum {
    PPP_IPCP_DNS_WINS_DISABLED = 0,
    PPP_IPCP_DNS_WINS_OFFER = 1,
    PPP_IPCP_DNS_WINS_REQUIRE = 2
} ppp_ipcp_dns_wins_mode_t;

#define PPP_KEEPALIVE_DEFAULT_S 30 /* Seconds of silence between Echo-Requests; 0 turns keepalive off */

typedef enum {
    PPP_NUMBERED_DISABLED = 0,
    PPP_NUMBERED_ENABLED  = 1, /* Request RFC 1663 Numbered-Mode and accept the peer's request */
    PPP_NUMBERED_AUTOMATIC = 2 /* Use Numbered-Mode only if the peer asks for it */
} ppp_numbered_mode_t;

typedef enum {
    PPP_COMPOUND_DISABLED = 0,
    PPP_COMPOUND_ACCEPT   = 1, /* Accept Compound-Frames from the peer (implies Self-Describing-Pad) */
    PPP_COMPOUND_AUTOMATIC = 2 /* Use Compound-Frames only if the peer asks for them */
} ppp_compound_mode_t;

typedef enum {
    PPP_FCS_DISABLED = 0,
    PPP_FCS_32       = 1, /* FCS-Alternatives: offer CCITT 32-bit FCS and use it when the peer allows */
    PPP_FCS_ACCEPT   = 2  /* Do not offer it, but honour the peer's request */
} ppp_fcs_mode_t;

typedef enum {
    PPP_LQM_DISABLED = 0,
    PPP_LQM_ACCEPT   = 1, /* Send Link-Quality-Reports when the peer asks */
    PPP_LQM_REQUEST  = 2  /* Also ask the peer to send reports to us */
} ppp_lqm_mode_t;

typedef enum {
    PPP_IPV6_DISABLED = 0, /* IPv6CP is not offered; its packets are Protocol-Rejected */
    PPP_IPV6_ENABLED,      /* negotiate IPv6CP and carry IPv6 alongside IPv4 */
    PPP_IPV6_AUTOMATIC     /* as enabled, but start IPv6CP only when the peer does */
} ppp_ipv6_mode_t;

/* Compression and encryption policy shown in normal builds. */
typedef enum {
    PPP_SECURITY_AUTOMATIC = 0, /* Whatever the peer supports, strongest and best first */
    PPP_SECURITY_OFF       = 1, /* No CCP, so no compression or MPPE */
    PPP_SECURITY_REQUIRE   = 2  /* The link fails unless MPPE is negotiated */
} ppp_security_mode_t;

typedef enum {
    PPP_CALLBACK_DISABLED = 0,
    PPP_CALLBACK_OFFERED  = 1, /* Callback Control Protocol: the caller may choose callback or no callback */
    PPP_CALLBACK_REQUIRED = 2, /* Callback is mandatory; a caller that declines is disconnected */
    PPP_CALLBACK_CLIENT   = 3  /* Caller role (used by tests and peer-to-peer links) */
} ppp_callback_mode_t;

typedef enum {
    PPP_CBCP_IDLE = 0,
    PPP_CBCP_WAIT_RESPONSE, /* Server: Callback-Request sent */
    PPP_CBCP_HOLD,          /* Callback agreed: terminating so the modem can hang up */
    PPP_CBCP_DONE           /* No callback: continue to the network phase */
} ppp_cbcp_phase_t;

typedef enum {
    PPP_SDP_DISABLED = 0,
    PPP_SDP_ACCEPT   = 1, /* Pad frames when the peer asks for Self-Describing-Pad */
    PPP_SDP_REQUEST  = 2  /* Also ask the peer to pad frames sent to us */
} ppp_sdp_mode_t;

typedef enum {
    PPP_EAP_STATE_IDLE,
    PPP_EAP_STATE_IDENTITY,
    PPP_EAP_STATE_MD5_CHALLENGE
} ppp_eap_state_t;

struct ppp_mppp_bundle_t;

/* Maximum PPP frame size */
#define PPP_MAX_FRAME   2048 /* Maximum receive unit buffer size for raw HDLC/PPP packet framing */
#define PPP_DEFAULT_MRU 1500 /* Default Maximum Receive Unit length (RFC 1661) */
#define PPP_MIN_MRU      128 /* Minimum MRU supported by this implementation */

#define PPP_CCP_METHOD_NONE           0   /* OUI-based / Proprietary Compression (RFC 1962) */
#define PPP_CCP_METHOD_PREDICTOR1     1   /* Predictor Type 1 (RFC 1978) */
#define PPP_CCP_METHOD_PREDICTOR2     2   /* Predictor Type 2 (RFC 1978) */
/* CCP option numbers follow the IANA "PPP CCP Configuration Option Types"
   registry (RFC 1962, RFC 3818). 4-15, 22, 25 and 28-254 are unassigned there.
   Values marked "Not implemented" have no public algorithm specification, so
   there is nothing to build a codec from. */
#define PPP_CCP_METHOD_PUDDLEJUMPER   3   /* Puddle Jumper: registry row only, no specification found - Not implemented */
#define PPP_CCP_METHOD_HPPPC         16   /* Hewlett-Packard PPC: only an expired 1993 draft, algorithm unspecified - Not implemented */
#define PPP_CCP_METHOD_LZS           17   /* Stac Electronics LZS (RFC 1974) */
#define PPP_CCP_METHOD_MPPE          18   /* Microsoft Point-to-Point Encryption / Compression (RFC 2118 / RFC 3078) */
#define PPP_CCP_METHOD_MPPC          0xFF /* Internal MPPC codec identifier; wire negotiation uses option 18 */
#define PPP_CCP_METHOD_GANDALF_FZA   19   /* Gandalf FZA (RFC 1993): algorithm not in the RFC, needs a reliable link and
                                             Self-Describing-Pad, licensed - Not implemented */
#define PPP_CCP_METHOD_V42BIS        20   /* V.42bis: no PPP specification exists to interoperate with - Not implemented */
#define PPP_CCP_METHOD_BSD           21   /* BSD Compress / LZW (RFC 1977) */
#define PPP_CCP_METHOD_DCP           23   /* LZS-DCP (RFC 1967) */
#define PPP_CCP_METHOD_LZS_DCP       23   /* Alias for PPP_CCP_METHOD_DCP */
#define PPP_CCP_METHOD_MVRCA         24   /* MVRCA / Magnalink (RFC 1975): algorithm not public - Not implemented.
                                             The same number was used by the original Deflate draft; see CCP_OPT_DEFLATE_DRAFT */
#define PPP_CCP_METHOD_DEFLATE       26   /* Deflate Compression (RFC 1979) */
#define PPP_CCP_METHOD_V44           27   /* ITU-T V.44 / LZJH Packet Method Compression (RFC 3051) */
#define PPP_CCP_METHOD_LZS_EXTENDED  0x91 /* Internal identifier only: Stac LZS (option 17) negotiated with check mode 4 */
#define PPP_CCP_METHOD_NT31RAS      254   /* Microsoft Windows NT 3.1 RAS proprietary compression; 254 is not an IANA assignment */

#define PPP_RAS_SYN          0x16
#define PPP_RAS_ETX          0x03
#define PPP_RAS_SOH_DEST     0x02
#define PPP_RAS_SOH_TYPE     0x80
#define PPP_RAS_SOH_COMPRESS 0x40
#define PPP_RAS_IP_TYPE      0x0800

/* PPP context */
typedef struct ppp_ctx_t {
    ppp_state_t     state;

    /* LCP negotiated parameters */
    uint16_t        our_mru;
    uint16_t        peer_mru;
    uint32_t        our_accm;
    uint32_t        peer_accm;
    uint32_t        flow_accm;
    uint32_t        our_magic;
    uint32_t        peer_magic;
    bool            our_pfc;
    bool            our_acfc;
    bool            peer_pfc;
    bool            peer_acfc;
    bool            request_pfc;
    bool            request_acfc;
    uint8_t         ppp_pfc_mode;
    uint8_t         ppp_acfc_mode;

    /* Self-Describing-Pad (RFC 1570 option 10) */
    uint8_t         ppp_sdp_mode;
    bool            sdp_request;     /* option present in our Configure-Request */
    uint8_t         sdp_our_max;     /* Maximum we ask for */
    bool            sdp_our_active;  /* peer Acked: it may pad frames sent to us */
    bool            sdp_peer_active; /* we Acked: we pad frames sent to the peer */
    uint8_t         sdp_peer_max;

    /* Compound-Frames (RFC 1570, LCP option 15): receive only */
    uint8_t         ppp_compound_mode;
    bool            compound_request;
    bool            compound_rx;      /* peer acknowledged: it may send several packets per frame */
    bool            compound_mirrored; /* automatic mode: added after the peer asked */

    /* FCS-Alternatives (RFC 1570, LCP option 9) */
    uint8_t         ppp_fcs_mode;
    bool            fcs_request;      /* option present in our Configure-Request */
    uint8_t         fcs_our_mask;     /* FCS types we say we can receive */
    bool            fcs_rx32_ok;      /* peer acknowledged: 32-bit frames may arrive */
    bool            fcs_tx32;         /* we acknowledged: send 32-bit FCS after LCP opens */
    bool            fcs_default;      /* LCP is renegotiating: use the default 16-bit FCS */

    /* Link Quality Monitoring (RFC 1989, LCP option 4) */
    uint8_t         ppp_lqm_mode;
    uint8_t         ppp_ipv6_mode;
    uint32_t        ppp_lqm_period_cs;    /* Reporting-Period we ask for, in 1/100 s (0 = default) */
    bool            lqm_request;
    bool            lqm_our_active;       /* peer acknowledged: it sends reports to us */
    bool            lqm_peer_active;      /* we acknowledged: we send reports to the peer */
    uint32_t        lqm_our_period_cs;
    uint32_t        lqm_peer_period_cs;
    uint32_t        lqm_timer_ms;
    uint32_t        lqm_out_lqrs, lqm_out_packets, lqm_out_octets;
    uint32_t        lqm_in_lqrs, lqm_in_packets, lqm_in_discards, lqm_in_errors, lqm_in_octets;
    uint32_t        lqm_save_in_lqrs, lqm_save_in_packets, lqm_save_in_discards,
                    lqm_save_in_errors, lqm_save_in_octets;
    uint32_t        lqm_last_out_lqrs, lqm_last_out_packets, lqm_last_out_octets;
    uint32_t        lqm_rx_peer_in_packets;   /* what the peer says it received from us */
    uint32_t        lqm_rx_last_out_packets;  /* what we had sent when that was measured */

    /* Callback Control Protocol (Microsoft CBCP, LCP option 13 operation 6) */
    uint8_t         ppp_callback_mode;
    uint8_t         ppp_callback_delay_s;      /* Delay offered to the caller */
    char            cbcp_client_number[32];    /* Client role: number to ask to be called back on */
    bool            cbcp_peer;                 /* Server: caller negotiated the CBCP callback option */
    bool            cbcp_client_req;           /* Client: option present in our Configure-Request */
    uint8_t         cbcp_phase;
    uint8_t         cbcp_id;
    uint16_t        cbcp_timeout_ms;
    uint8_t         cbcp_retries;
    uint16_t        cbcp_hold_ms;
    bool            callback_pending;          /* Result: hang up, then call the caller back */
    uint8_t         callback_delay_s;
    char            callback_number[32];

    /* Numbered-Mode (RFC 1663 option 11) over LAPB */
    uint8_t         ppp_numbered_mode;
    uint8_t         ppp_numbered_window;
    bool            nm_request;      /* option present in our Configure-Request */
    uint8_t         nm_our_window;
    bool            nm_mirrored;     /* automatic mode: added after the peer asked */
    uint8_t         nm_our_addr;     /* Address placed in frames we transmit */
    bool            nm_our_active;   /* peer Acked our option */
    uint8_t         nm_peer_window;  /* limits how many frames we may have outstanding */
    uint8_t         nm_peer_addr;
    bool            nm_peer_active;  /* we Acked the peer's option */
    bool            nm_started;
    uint32_t        nm_dm_holdoff_ms;
    struct lapb_t  *lapb;
    char            multilink_group[64];
    bool            multilink_auto;  /* no group configured: derive one if the peer offers Multilink */
    bool            multilink_request_mrru;
    bool            multilink_request_short_sequence;
    bool            multilink_request_endpoint;
    bool            multilink_our_mrru;
    bool            multilink_peer_mrru;
    bool            multilink_our_short_sequence;
    bool            multilink_peer_short_sequence;
    bool            multilink_our_endpoint;
    bool            multilink_peer_endpoint;
    uint16_t        multilink_our_mrru_value;
    uint16_t        multilink_peer_mrru_value;
    uint8_t         multilink_peer_endpoint_data[32];
    uint8_t         multilink_peer_endpoint_length;
    struct ppp_mppp_bundle_t *multilink_bundle;

    /* LCP state tracking */
    uint8_t         lcp_id;
    bool            lcp_ack_sent;
    bool            lcp_ack_received;
    bool            ccp_reset_pending;
    int             lcp_retries;
    uint16_t        lcp_timeout_ms;
    bool            lcp_req_sent;
    bool            lcp_peer_heard;  /* the peer's first Configure-Request has arrived */
    uint8_t         lcp_request[64];
    uint8_t         lcp_request_len;

    /* Authentication */
    ppp_auth_type_t auth_type;
    bool            auth_auto;       /* walk ppp_auth_auto_next() until the peer agrees */
    uint8_t         auth_id;
    char            username[64];
    char            password[64];
    uint16_t        auth_timeout_ms;
    bool            auth_complete;
    uint16_t        pap_last_request_len;
    uint8_t         pap_last_request[PPP_PAP_MAX_REQUEST_LENGTH];
    bool            pap_last_request_valid;
    bool            mppe_keys_ready;
    bool            mppe_tx_enabled;
    bool            mppe_rx_enabled;
    uint8_t         mppe_min_bits;
    uint32_t        mppe_allowed_bits;
    bool            mppe_prefer_strongest; /* required MPPE takes the strongest key, not the minimum */
    bool            mppe_state_nak_sent;   /* preference expressed once; now follow the peer */
    bool            mppe_d_bit_nak_sent;   /* asked once to clear the D bit; now tolerate it */
    bool            mppe_56_retried;       /* retried without 56-bit after a Reject */
    uint8_t         ccp_mode;
    uint8_t         ccp_direction;
    uint8_t         ppp_compression;
    uint8_t         mppe_key_state_mode;
    uint8_t         mppe_format;
    uint16_t        ppp_keepalive_s;
    uint32_t        keepalive_idle_ms;
    uint8_t         keepalive_missed;
    ppp_mppe_state_t mppe_tx;
    ppp_mppe_state_t mppe_rx;

    /* CCP state tracking */
    uint8_t         ccp_id;
    uint8_t         ccp_request_id;
    uint8_t         ccp_reset_id;
    bool            ccp_req_sent;
    bool            ccp_ack_sent;
    bool            ccp_ack_received;
    bool            ccp_peer_mppe;
    bool            ccp_open;
    bool            ccp_rejected;
    bool            ccp_plaintext_fallback;
    uint8_t         ccp_reset_request_id;
    uint32_t        ccp_request_bits;
    uint32_t        ccp_tx_bits;
    uint8_t         ccp_request_method;
    uint8_t         ccp_tx_method;
    uint8_t         ccp_rx_method;
    uint32_t        ccp_tx_window_size;
    uint32_t        ccp_rx_window_size;
    uint8_t         ccp_tx_bsd_bits;
    uint8_t         ccp_rx_bsd_bits;
    void           *ccp_tx_codec_state;
    void           *ccp_rx_codec_state;
    int             ccp_retries;
    uint8_t         ccp_request[32];
    uint8_t         ccp_request_len;
    uint16_t        ccp_timeout_ms;
    int             ccp_timeout_retries;

    /* NT31 RAS framing and coherency tickets. */
    bool            ras_tx_flush_pending;
    uint8_t         ras_tx_ticket_base;
    uint8_t         ras_tx_ticket_next;
    uint8_t         ras_rx_ticket_base;
    uint8_t         ras_rx_ticket_next;
    uint8_t         ras_rx_buf[PPP_MAX_FRAME + 272];
    int             ras_rx_len;
    int             ras_rx_expected;
    bool            ras_rx_in_frame;

    /* CHAP challenge state */
    uint8_t         chap_challenge[16];
    uint8_t         chap_challenge_len;

    /* EAP authenticator state */
    ppp_eap_state_t eap_state;
    uint8_t         eap_id;
    uint8_t         eap_request_id;
    uint8_t         eap_challenge[16];

    /* IPCP state tracking */
    uint8_t         ipcp_id;
    uint8_t         ipcp_request_id;
    bool            ipcp_ack_sent;
    bool            ipcp_ack_received;
    int             ipcp_retries;
    bool            ipcp_req_sent;
    uint16_t        ipcp_timeout_ms;
    int             ipcp_timeout_retries;
    uint32_t        ipcp_request_ip;
    bool            ipcp_vj_request;
    uint8_t         ipcp_vj_mode;
    bool            vj_tx_enabled;
    bool            vj_rx_enabled;
    uint8_t         vj_tx_max_slot_id;
    uint8_t         vj_rx_max_slot_id;
    bool            vj_tx_comp_slot_id;
    bool            vj_rx_comp_slot_id;
    struct cslip_ctx_t *vj_tx_ctx;
    struct cslip_ctx_t *vj_rx_ctx;

    /* IP configuration */
    uint32_t        our_ip;
    uint32_t        peer_ip;
    uint32_t        dns1;
    uint32_t        dns2;
    uint32_t        wins1;
    uint32_t        wins2;
    uint8_t         ipcp_dns_wins_mode;

    /* IPv6CP (RFC 5072) */
    bool            nbfcp_active;        /* the peer has asked for NetBEUI */
    bool            nbfcp_open;
    bool            nbfcp_req_sent;
    bool            nbfcp_ack_sent;
    bool            nbfcp_ack_received;
    bool            nbfcp_mac_required;  /* frames carry the 12 octet header RFC 2097 allows */
    uint8_t         nbfcp_id;
    uint8_t         nbfcp_request_id;
    uint16_t        nbfcp_timeout_ms;
    uint8_t         nbfcp_retries;
    bool            ipxcp_active;        /* the peer has asked for IPX */
    bool            ipxcp_open;
    bool            ipxcp_req_sent;
    bool            ipxcp_ack_sent;
    bool            ipxcp_ack_received;
    bool            ipxcp_send_network;  /* the peer hasn't rejected these */
    bool            ipxcp_send_node;
    uint8_t         ipxcp_id;
    uint8_t         ipxcp_request_id;
    uint16_t        ipxcp_timeout_ms;
    uint8_t         ipxcp_retries;
    uint32_t        ipxcp_network;
    uint8_t         ipxcp_node[6];
    bool            ipv6cp_active;       /* negotiating or open; cleared when given up */
    bool            ipv6cp_started;      /* tried once this session */
    bool            ipv6cp_open;
    bool            ipv6cp_req_sent;
    bool            ipv6cp_ack_sent;
    bool            ipv6cp_ack_received;
    bool            ipv6cp_send_iid;     /* include Interface-Identifier in our requests */
    uint8_t         ipv6cp_id;
    uint8_t         ipv6cp_request_id;
    uint16_t        ipv6cp_timeout_ms;
    int             ipv6cp_retries;
    uint8_t         ipv6cp_our_iid[8];
    uint8_t         ipv6cp_peer_iid[8];

    /* HDLC frame assembly (receiving from serial) */
    uint8_t         rx_buf[PPP_MAX_FRAME];
    int             rx_len;
    bool            rx_in_frame;
    bool            rx_escaped;

    /* Back-pointer and callbacks */
    void           *modem;
    void           *log;
    void          (*serial_push)(void *modem, const uint8_t *data, int len);
    void          (*network_send_ip)(void *modem, const uint8_t *ip_pkt, int len);
    void          (*network_send_ip6)(void *modem, const uint8_t *ip_pkt, int len);
    void          (*network_send_nbf)(void *modem, const uint8_t *frame, int len); /* NULL refuses NetBEUI */
    void          (*network_send_ipx)(void *modem, const uint8_t *packet, int len); /* NULL refuses IPX */
} ppp_ctx_t;

/* Fixed PPP settings for normal builds. ENABLE_MODEM_ADVANCED_CONFIG builds read each of these
   from the device configuration instead, so every forced mode stays reachable. */
#ifdef ENABLE_MODEM_IPV6
#    define PPP_CONFIG_IPV6_DEFAULT PPP_IPV6_AUTOMATIC
#else
#    define PPP_CONFIG_IPV6_DEFAULT PPP_IPV6_DISABLED
#endif

#define PPP_CONFIG_APPLY_AUTOMATIC(m, security)                                         \
    do {                                                                                \
        (m)->ppp_auth_type              = PPP_AUTH_CONFIG_AUTOMATIC;                    \
        (m)->ppp_compression            = (security) == PPP_SECURITY_OFF                \
                                              ? PPP_COMPRESSION_DISABLED                \
                                              : PPP_COMPRESSION_PREFER_MPPC;            \
        (m)->ccp_mode                   = (security) == PPP_SECURITY_OFF                \
                                              ? PPP_CCP_MODE_DISABLED                   \
                                              : PPP_CCP_MODE_ENABLED;                   \
        (m)->mppe_bitmask               = (int) ((security) == PPP_SECURITY_OFF         \
                                              ? CCP_MPPE_DISABLED                       \
                                              : (security) == PPP_SECURITY_REQUIRE      \
                                                  ? CCP_MPPE_REQUIRED_ANY               \
                                                  : CCP_MPPE_AUTOMATIC);                \
        (m)->mppe_key_state_mode        = PPP_MPPE_STATE_OFFER_STATELESS;               \
        (m)->mppe_format                = PPP_MPPE_FORMAT_AUTOMATIC;                    \
        (m)->ipcp_dns_wins_mode         = PPP_IPCP_DNS_WINS_OFFER;                      \
        (m)->ipcp_vj_header_compression = PPP_VJ_COMPRESSION_ALLOWED;                   \
        (m)->ppp_acfc                   = PPP_LCP_COMPRESSION_OFFER;                    \
        (m)->ppp_pfc                    = PPP_LCP_COMPRESSION_OFFER;                    \
        (m)->ppp_numbered_mode          = PPP_NUMBERED_AUTOMATIC;                       \
        (m)->ppp_numbered_window        = 7;                                            \
        (m)->ppp_sdp                    = PPP_SDP_ACCEPT;                               \
        (m)->ppp_compound               = PPP_COMPOUND_AUTOMATIC;                       \
        (m)->ppp_fcs                    = PPP_FCS_ACCEPT;                               \
        (m)->ppp_lqm                    = PPP_LQM_ACCEPT;                               \
        (m)->ppp_ipv6                   = PPP_CONFIG_IPV6_DEFAULT;                      \
        (m)->ppp_callback_delay         = 5;                                            \
    } while (0)

/* PPP core functions */
ppp_ctx_t *ppp_init(void *modem, void *log,
                    void (*serial_push)(void *, const uint8_t *, int),
                    void (*network_send_ip)(void *, const uint8_t *, int));
void       ppp_close(ppp_ctx_t *ctx);
void       ppp_start(ppp_ctx_t *ctx);
void       ppp_multilink_configure(ppp_ctx_t *ctx, const char *group);
void       ppp_mppe_set_policy(ppp_ctx_t *ctx, uint32_t bitmask, uint8_t format);
bool       ppp_multilink_is_member(const ppp_ctx_t *ctx);
bool       ppp_multilink_is_owner(const ppp_ctx_t *ctx);
void       ppp_timer_tick(ppp_ctx_t *ctx);
void       ppp_rx_byte(ppp_ctx_t *ctx, uint8_t byte);
void       ppp_wrap_ip(ppp_ctx_t *ctx, const uint8_t *ip_pkt, int len);
void       ppp_wrap_ip6(ppp_ctx_t *ctx, const uint8_t *ip_pkt, int len);
bool       ppp_ipv6_ready(const ppp_ctx_t *ctx);

/* PPP frame sending (used by sub-protocols) */
void       ppp_send_frame(ppp_ctx_t *ctx, uint16_t protocol, const uint8_t *data, int len);
void       ppp_advance_state(ppp_ctx_t *ctx);
void       ppp_send_code_reject(ppp_ctx_t *ctx, uint16_t protocol,
                                const uint8_t *pkt, int pkt_len);
bool       ppp_random_bytes(uint8_t *buffer, uint8_t len);
void       ppp_ccp_start(ppp_ctx_t *ctx);
void       ppp_ccp_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len);
void       ppp_ccp_retry_mppc_without_mppe(ppp_ctx_t *ctx);
void       ppp_ccp_fallback_plaintext(ppp_ctx_t *ctx);
void       ppp_ccp_timeout(ppp_ctx_t *ctx);
const char *ppp_ccp_method_name(uint8_t method);
bool       ppp_ccp_codec_set(ppp_ctx_t *ctx, bool transmit, uint8_t method);
bool       ppp_ccp_codec_set_window(ppp_ctx_t *ctx, bool transmit, uint8_t method,
                                    uint32_t window_size);
bool       ppp_ccp_codec_set_lzs(ppp_ctx_t *ctx, bool transmit, uint8_t method,
                                 uint16_t history_count, uint8_t check_mode);
void       ppp_ccp_codec_close(ppp_ctx_t *ctx);
bool       ppp_ccp_codec_compress(ppp_ctx_t *ctx, const uint8_t *input, int input_len,
                                  uint8_t *output, int output_capacity, int *output_len);
bool       ppp_ccp_codec_decompress(ppp_ctx_t *ctx, const uint8_t *input, int input_len,
                                    uint8_t *output, int output_capacity, int *output_len);
void       ppp_ccp_codec_flush(ppp_ctx_t *ctx, bool transmit);
bool       ppp_ccp_codec_last_frame_flushed(ppp_ctx_t *ctx, bool transmit);

/* FCS-16 CRC */
uint16_t   ppp_fcs16(const uint8_t *data, int len);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MODEM_PPP_H */
