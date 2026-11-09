/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CSLIP (Compressed SLIP) - Van Jacobson TCP/IP header
 *          compression implementation for modem emulation.
 *          RFC 1144.
 *
 *          This implements the compressor (for outgoing packets to
 *          the guest) and decompressor (for incoming packets from
 *          the guest).
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <86box/modem/modem_cslip.h>
#include <86box/log.h>
#include <86box/modem/modem_debug.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
cslip_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define cslip_log(priv, fmt, ...)
#endif

/* IP header field offsets */
#define IP_VHL       0   /* Version + IHL */
#define IP_TOS       1
#define IP_LEN       2   /* Total length (2 bytes, big-endian) */
#define IP_ID        4   /* Identification (2 bytes) */
#define IP_FLAGS_OFF 6   /* Flags + Fragment offset (2 bytes) */
#define IP_TTL       8
#define IP_PROTO     9
#define IP_CKSUM     10  /* Header checksum (2 bytes) */
#define IP_SRC       12  /* Source IP (4 bytes) */
#define IP_DST       16  /* Destination IP (4 bytes) */

/* TCP header field offsets (relative to start of TCP header) */
#define TCP_SPORT    0   /* Source port (2 bytes) */
#define TCP_DPORT    2   /* Destination port (2 bytes) */
#define TCP_SEQ      4   /* Sequence number (4 bytes) */
#define TCP_ACK      8   /* Acknowledgement number (4 bytes) */
#define TCP_DOFF     12  /* Data offset + flags */
#define TCP_FLAGS    13  /* Flags byte */
#define TCP_WIN      14  /* Window (2 bytes) */
#define TCP_CKSUM    16  /* Checksum (2 bytes) */
#define TCP_URGP     18  /* Urgent pointer (2 bytes) */

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

/* Get 16-bit value from big-endian buffer */
static inline uint16_t
vj_get16(const uint8_t *p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}

/* Put 16-bit value to big-endian buffer */
static inline void
vj_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

/* Get 32-bit value from big-endian buffer */
static inline uint32_t
vj_get32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16)
         | ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

/* Put 32-bit value to big-endian buffer */
static inline void
vj_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

/* Encode a delta value (RFC 1144 style) */
static int
vj_encode_delta(uint8_t *out, uint16_t delta)
{
    if (delta == 0)
        return 0;

    if (delta <= 255) {
        out[0] = (uint8_t) delta;
        return 1;
    } else {
        out[0] = 0;
        out[1] = (uint8_t) (delta >> 8);
        out[2] = (uint8_t) (delta & 0xFF);
        return 3;
    }
}

/* Decode a delta value from compressed stream */
static int
vj_decode_delta(const uint8_t *in, int avail, uint16_t *val)
{
    if (avail < 1)
        return -1;
    if (in[0] != 0) {
        *val = in[0];
        return 1;
    }
    if (avail < 3)
        return -1;
    *val = (uint16_t) ((in[1] << 8) | in[2]);
    return 3;
}

/* Recompute IP header checksum */
static void
vj_recompute_ip_cksum(uint8_t *ip_hdr, int ip_hdr_len)
{
    uint32_t sum = 0;

    ip_hdr[IP_CKSUM]     = 0;
    ip_hdr[IP_CKSUM + 1] = 0;

    for (int i = 0; i < ip_hdr_len; i += 2) {
        sum += (uint16_t) ((ip_hdr[i] << 8) | (i + 1 < ip_hdr_len ? ip_hdr[i + 1] : 0));
    }

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    uint16_t cksum = ~(uint16_t) sum;
    vj_put16(ip_hdr + IP_CKSUM, cksum);
}

cslip_ctx_t *
cslip_init(void *log)
{
    cslip_ctx_t *ctx = (cslip_ctx_t *) calloc(1, sizeof(cslip_ctx_t));
    if (!ctx)
        return NULL;

    ctx->num_slots       = VJ_MAX_SLOTS;
    ctx->last_conn_recv  = 0;
    ctx->last_conn_send  = 0;
    ctx->last_cs         = 0;
    ctx->compress_slot_id = true;
    ctx->flags           = 0;
    ctx->log             = log;

    for (int i = 0; i < VJ_MAX_SLOTS; i++) {
        ctx->slots[i].conn_id = (uint8_t) i;
        ctx->slots[i].hdr_len = 0;
    }

    return ctx;
}

void
cslip_close(cslip_ctx_t *ctx)
{
    free(ctx);
}

#if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
static const char *
cslip_vj_type_name(int type)
{
    switch (type) {
        case VJ_TYPE_IP:               return "plain-IP";
        case VJ_TYPE_UNCOMPRESSED_TCP: return "uncompressed-TCP";
        case VJ_TYPE_COMPRESSED_TCP:   return "compressed-TCP";
        default:                       return "unknown";
    }
}
#endif

/*
 * Compress an outgoing IP packet for transmission to the guest.
 * This is the "compressor" - it takes a normal IP packet and produces
 * either:
 *   - VJ_TYPE_IP: unmodified IP (non-TCP, fragmented, etc.)
 *   - VJ_TYPE_UNCOMPRESSED_TCP: full header, sets up connection state
 *   - VJ_TYPE_COMPRESSED_TCP: delta-compressed header + payload
 *
 * Returns compressed length, type stored in *type.
 */
int
cslip_compress(cslip_ctx_t *ctx, const uint8_t *in, int in_len,
               uint8_t *out, int *type)
{
    int      ip_hdr_len;
    int      tcp_hdr_len;
    int      total_hdr;
    uint8_t  changes = 0;
    int      i;
    int      out_len = 0;
    uint8_t  delta_buf[16];
    int      delta_len = 0;

    if (!ctx || !in || in_len <= 0 || !out || !type)
        return 0;

    MODEM_DEBUG_LOG(ctx->log, "VJ: compress input length=%d slots=%u\n",
                    in_len, (unsigned) ctx->num_slots);

    /* Minimum IP header check */
    if (in_len < 20) {
        *type = VJ_TYPE_IP;
        memcpy(out, in, in_len);
        return in_len;
    }

    /* Check IP version and get header length */
    if ((in[IP_VHL] >> 4) != 4) {
        *type = VJ_TYPE_IP;
        memcpy(out, in, in_len);
        return in_len;
    }

    ip_hdr_len = (in[IP_VHL] & 0x0F) * 4;
    if (ip_hdr_len < 20 || ip_hdr_len > in_len) {
        *type = VJ_TYPE_IP;
        memcpy(out, in, in_len);
        return in_len;
    }

    /* Only compress TCP */
    if (in[IP_PROTO] != 6) { /* TCP = protocol 6 */
        *type = VJ_TYPE_IP;
        memcpy(out, in, in_len);
        return in_len;
    }

    /* Check for fragmentation */
    uint16_t frag = vj_get16(in + IP_FLAGS_OFF);
    if (frag & 0x3FFF) { /* Fragment offset or MF flag */
        *type = VJ_TYPE_IP;
        memcpy(out, in, in_len);
        return in_len;
    }

    /* Need at least IP + TCP headers */
    if (in_len < ip_hdr_len + 20) {
        *type = VJ_TYPE_IP;
        memcpy(out, in, in_len);
        return in_len;
    }

    const uint8_t *tcp = in + ip_hdr_len;
    tcp_hdr_len = (tcp[TCP_DOFF] >> 4) * 4;
    if (tcp_hdr_len < 20 || tcp_hdr_len > in_len - ip_hdr_len) {
        *type = VJ_TYPE_IP;
        memcpy(out, in, in_len);
        return in_len;
    }

    total_hdr = ip_hdr_len + tcp_hdr_len;

    /* VJ only compresses established TCP data and acknowledgment packets. */
    if (!(tcp[TCP_FLAGS] & TCP_FLAG_ACK)
        || (tcp[TCP_FLAGS] & (TCP_FLAG_SYN | TCP_FLAG_FIN | TCP_FLAG_RST))) {
        *type = VJ_TYPE_IP;
        memcpy(out, in, in_len);
        return in_len;
    }

    /* Search for a matching connection (src/dst IP + src/dst port) */
    vj_slot_t *cs = NULL;
    for (i = 0; i < ctx->num_slots; i++) {
        int idx = (ctx->last_cs + i) % ctx->num_slots;
        vj_slot_t *s = &ctx->slots[idx];
        if (s->hdr_len == 0)
            continue;
        if (vj_get32(s->hdr + IP_SRC) == vj_get32(in + IP_SRC)
         && vj_get32(s->hdr + IP_DST) == vj_get32(in + IP_DST)
         && vj_get16(s->hdr + ip_hdr_len + TCP_SPORT) == vj_get16(tcp + TCP_SPORT)
         && vj_get16(s->hdr + ip_hdr_len + TCP_DPORT) == vj_get16(tcp + TCP_DPORT)) {
            cs = s;
            ctx->last_cs = idx;
            break;
        }
    }

    if (!cs) {
        goto send_uncompressed;
    }

    /* Found a match - compute deltas */
    int old_ip_hdr_len = (cs->hdr[IP_VHL] & 0x0F) * 4;
    if (old_ip_hdr_len != ip_hdr_len
        || in[IP_TOS] != cs->hdr[IP_TOS]
        || in[IP_TTL] != cs->hdr[IP_TTL]
        || vj_get16(in + IP_FLAGS_OFF) != vj_get16(cs->hdr + IP_FLAGS_OFF)
        || memcmp(in + 20, cs->hdr + 20, ip_hdr_len - 20) != 0)
        goto send_uncompressed;

    const uint8_t *old_tcp = cs->hdr + old_ip_hdr_len;

    /* Header options and unencoded flags must remain identical. */
    if (tcp_hdr_len != (int) ((old_tcp[TCP_DOFF] >> 4) * 4)
        || memcmp(tcp + 20, old_tcp + 20, tcp_hdr_len - 20) != 0
        || ((tcp[TCP_FLAGS] ^ old_tcp[TCP_FLAGS]) & ~(TCP_FLAG_PSH | TCP_FLAG_URG)) != 0) {
        goto send_uncompressed;
    }

    /* Urgent pointer */
    if (tcp[TCP_FLAGS] & TCP_FLAG_URG) {
        uint16_t urgp = vj_get16(tcp + TCP_URGP);
        int      n    = vj_encode_delta(delta_buf + delta_len, urgp);
        if (n == 0)
            goto send_uncompressed;
        delta_len += n;
        changes |= VJ_NEW_U;
    } else if (vj_get16(tcp + TCP_URGP) != vj_get16(old_tcp + TCP_URGP)) {
        goto send_uncompressed;
    }

    /* Window */
    {
        int16_t dwin = (int16_t) (vj_get16(tcp + TCP_WIN) - vj_get16(old_tcp + TCP_WIN));
        if (dwin != 0) {
            int n = vj_encode_delta(delta_buf + delta_len, (uint16_t) dwin);
            delta_len += n;
            changes |= VJ_NEW_W;
        }
    }

    /* Ack */
    {
        uint32_t old_ack = vj_get32(old_tcp + TCP_ACK);
        uint32_t new_ack = vj_get32(tcp + TCP_ACK);
        uint32_t dack    = new_ack - old_ack;
        if (dack != 0) {
            if (dack > 0xFFFF)
                goto send_uncompressed;
            int n = vj_encode_delta(delta_buf + delta_len, (uint16_t) dack);
            delta_len += n;
            changes |= VJ_NEW_A;
        }
    }

    /* Sequence */
    {
        uint32_t old_seq = vj_get32(old_tcp + TCP_SEQ);
        uint32_t new_seq = vj_get32(tcp + TCP_SEQ);
        uint32_t dseq    = new_seq - old_seq;
        if (dseq != 0) {
            if (dseq > 0xFFFF)
                goto send_uncompressed;
            int n = vj_encode_delta(delta_buf + delta_len, (uint16_t) dseq);
            delta_len += n;
            changes |= VJ_NEW_S;
        }
    }

    /* If nothing changed except possibly IP ID, send uncompressed */
    if (changes == 0) {
        /* Check if only the IP ID changed by 1 (common case) */
        uint16_t old_id = vj_get16(cs->hdr + IP_ID);
        uint16_t new_id = vj_get16(in + IP_ID);
        if (new_id != old_id + 1)
            goto send_uncompressed;
        /* Also check TOS hasn't changed */
        if (in[IP_TOS] != cs->hdr[IP_TOS])
            goto send_uncompressed;
        /* Nothing to compress: same as old packet */
        goto send_uncompressed;
    }

    /* Check for special cases */
    {
        int      previous_payload_len = (int) vj_get16(cs->hdr + IP_LEN) - total_hdr;
        uint16_t did          = vj_get16(in + IP_ID) - vj_get16(cs->hdr + IP_ID);

        if (changes == VJ_NEW_S) {
            uint32_t dseq2 = vj_get32(tcp + TCP_SEQ) - vj_get32(old_tcp + TCP_SEQ);
            uint32_t dack2 = vj_get32(tcp + TCP_ACK) - vj_get32(old_tcp + TCP_ACK);
            if (previous_payload_len >= 0
                && dseq2 == (uint32_t) previous_payload_len && dack2 == 0 && did == 1) {
                changes = VJ_SPECIAL_D;
                delta_len = 0;
            }
        } else if (changes == (VJ_NEW_S | VJ_NEW_A)) {
            uint32_t dack2 = vj_get32(tcp + TCP_ACK) - vj_get32(old_tcp + TCP_ACK);
            uint32_t dseq2 = vj_get32(tcp + TCP_SEQ) - vj_get32(old_tcp + TCP_SEQ);
            int16_t  dwin2 = (int16_t) (vj_get16(tcp + TCP_WIN) - vj_get16(old_tcp + TCP_WIN));
            if (previous_payload_len >= 0 && dack2 == (uint32_t) dseq2
                && dseq2 == (uint32_t) previous_payload_len && dwin2 == 0 && did == 1) {
                changes = VJ_SPECIAL_I;
                delta_len = 0;
            }
        } else if (changes == VJ_SPECIAL_I || changes == VJ_SPECIAL_D) {
            goto send_uncompressed;
        }

        uint8_t ip_id_delta[3];
        int ip_id_delta_len = 0;
        if (did != 1) {
            changes |= VJ_NEW_I;
            if (did == 0) {
                ip_id_delta[0] = 0;
                ip_id_delta[1] = 0;
                ip_id_delta[2] = 0;
                ip_id_delta_len = 3;
            } else {
                ip_id_delta_len = vj_encode_delta(ip_id_delta, did);
            }
        }

        /* Build compressed packet */
        if (tcp[TCP_FLAGS] & TCP_FLAG_PSH)
            changes |= VJ_TCP_PUSH_BIT;

        /* Connection ID */
        if (!ctx->compress_slot_id || cs->conn_id != ctx->last_conn_send) {
            changes |= VJ_NEW_C;
            ctx->last_conn_send = cs->conn_id;
        }

        out[out_len++] = changes | VJ_TYPE_COMPRESSED_TCP;
        if (changes & VJ_NEW_C)
            out[out_len++] = cs->conn_id;

        /* TCP checksum (unmodified) */
        out[out_len++] = tcp[TCP_CKSUM];
        out[out_len++] = tcp[TCP_CKSUM + 1];

        /* TCP deltas precede the optional IP ID delta on the wire. */
        memcpy(out + out_len, delta_buf, delta_len);
        out_len += delta_len;
        memcpy(out + out_len, ip_id_delta, ip_id_delta_len);
        out_len += ip_id_delta_len;

        /* Payload */
        if (in_len > total_hdr) {
            memcpy(out + out_len, in + total_hdr, in_len - total_hdr);
            out_len += (in_len - total_hdr);
        }

        /* Update saved state */
        memcpy(cs->hdr, in, total_hdr);
        cs->hdr_len = total_hdr;

        *type = VJ_TYPE_COMPRESSED_TCP;
        return out_len;
    }

send_uncompressed:
    {
        /* Find or allocate a slot */
        vj_slot_t *slot = NULL;
        for (i = 0; i < ctx->num_slots; i++) {
            int idx = (ctx->last_cs + 1 + i) % ctx->num_slots;
            if (ctx->slots[idx].hdr_len == 0) {
                slot = &ctx->slots[idx];
                ctx->last_cs = idx;
                break;
            }
        }
        if (!slot) {
            /* Overwrite LRU (next slot after last used) */
            ctx->last_cs = (ctx->last_cs + 1) % ctx->num_slots;
            slot = &ctx->slots[ctx->last_cs];
        }

        /* Save connection state */
        memcpy(slot->hdr, in, (total_hdr < VJ_MAX_HDR) ? total_hdr : VJ_MAX_HDR);
        slot->hdr_len  = total_hdr;
        slot->ip_proto = in[IP_PROTO];
        ctx->last_conn_send = slot->conn_id;

        /* RFC 1144 carries the connection number in the IP protocol field. */
        memcpy(out, in, in_len);
        out[IP_PROTO] = slot->conn_id;

        *type = VJ_TYPE_UNCOMPRESSED_TCP;
        return in_len;
    }
}

int
cslip_compress_logged(cslip_ctx_t *ctx, const uint8_t *in, int in_len,
                      uint8_t *out, int *type)
{
    if (!ctx || !type)
        return 0;

    int output_len = cslip_compress(ctx, in, in_len, out, type);
    if (output_len <= 0)
        return output_len;

    MODEM_DEBUG_LOG(ctx->log, "CSLIP: TX VJ type=%s (%d) input=%d output=%d saved=%d\n",
                    cslip_vj_type_name(*type), *type, in_len, output_len,
                    output_len > 0 ? in_len - output_len : 0);
    return output_len;
}

/*
 * Decompress an incoming packet from the guest.
 * type indicates the VJ packet type.
 * Returns decompressed IP packet length.
 */
int
cslip_decompress(cslip_ctx_t *ctx, const uint8_t *in, int in_len,
                 uint8_t *out, int type)
{
    if (!ctx || !in || in_len <= 0 || !out)
        return 0;

    MODEM_DEBUG_LOG(ctx->log, "VJ: decompress type=%d input length=%d last-connection=%u flags=0x%02X\n",
                    type, in_len, (unsigned) ctx->last_conn_recv, (unsigned) ctx->flags);

    if (type == VJ_TYPE_IP) {
        memcpy(out, in, in_len);
        return in_len;
    }

    if (type == VJ_TYPE_UNCOMPRESSED_TCP) {
        if (in_len < 20)
            return 0;

        int ip_hdr_len = (in[IP_VHL] & 0x0F) * 4;
        if ((in[IP_VHL] >> 4) != 4 || ip_hdr_len < 20 || in_len < ip_hdr_len + 20)
            return 0;

        int tcp_hdr_len = (in[ip_hdr_len + TCP_DOFF] >> 4) * 4;
        if (tcp_hdr_len < 20 || tcp_hdr_len > in_len - ip_hdr_len)
            return 0;

        int total_hdr = ip_hdr_len + tcp_hdr_len;
        uint16_t ip_total_len = vj_get16(in + IP_LEN);
        if (ip_total_len < total_hdr || ip_total_len > in_len) {
            ctx->flags |= VJ_FLAG_TOSS;
            return 0;
        }

        /* Connection ID is in IP protocol field */
        uint8_t conn_id = in[IP_PROTO] & (VJ_MAX_SLOTS - 1);
        if (conn_id >= ctx->num_slots) {
            cslip_log(ctx->log, "VJ: Bad connection ID %d in uncompressed TCP\n", conn_id);
            ctx->flags |= VJ_FLAG_TOSS;
            return 0;
        }

        ctx->last_conn_recv = conn_id;
        ctx->flags &= ~VJ_FLAG_TOSS;

        /* Restore the real protocol and save state */
        memcpy(out, in, in_len);
        out[IP_PROTO] = 6; /* TCP */

        vj_slot_t *cs = &ctx->slots[conn_id];
        memcpy(cs->hdr, out, total_hdr);
        cs->hdr_len = total_hdr;

        return in_len;
    }

    if (type != VJ_TYPE_COMPRESSED_TCP) {
        memcpy(out, in, in_len);
        return in_len;
    }

    /* Compressed TCP */
    if (ctx->flags & VJ_FLAG_TOSS) {
        cslip_log(ctx->log, "VJ: Tossing packet (error recovery)\n");
        return 0;
    }

    if (in_len < 3) {
        ctx->flags |= VJ_FLAG_TOSS;
        return 0;
    }

    int     pos     = 0;
    uint8_t changes = in[pos++];

    /* Get connection ID */
    uint8_t conn_id;
    if (changes & VJ_NEW_C) {
        if (pos >= in_len) {
            ctx->flags |= VJ_FLAG_TOSS;
            return 0;
        }
        conn_id = in[pos++];
        if (conn_id >= ctx->num_slots) {
            ctx->flags |= VJ_FLAG_TOSS;
            return 0;
        }
        ctx->last_conn_recv = conn_id;
    } else {
        conn_id = ctx->last_conn_recv;
    }

    if (conn_id >= ctx->num_slots) {
        ctx->flags |= VJ_FLAG_TOSS;
        return 0;
    }

    vj_slot_t *cs = &ctx->slots[conn_id];
    if (cs->hdr_len == 0) {
        cslip_log(ctx->log, "VJ: No saved state for connection %d\n", conn_id);
        ctx->flags |= VJ_FLAG_TOSS;
        return 0;
    }

    int ip_hdr_len  = (cs->hdr[IP_VHL] & 0x0F) * 4;
    int total_hdr   = cs->hdr_len;

    /* Start with saved header */
    uint8_t hdr[VJ_MAX_HDR];
    memcpy(hdr, cs->hdr, total_hdr);
    uint8_t *tcp_hdr = hdr + ip_hdr_len;

    /* TCP checksum (always present) */
    if (pos + 2 > in_len) {
        ctx->flags |= VJ_FLAG_TOSS;
        return 0;
    }
    tcp_hdr[TCP_CKSUM]     = in[pos++];
    tcp_hdr[TCP_CKSUM + 1] = in[pos++];

    /* Handle PSH flag */
    if (changes & VJ_TCP_PUSH_BIT)
        tcp_hdr[TCP_FLAGS] |= TCP_FLAG_PSH;
    else
        tcp_hdr[TCP_FLAGS] &= ~TCP_FLAG_PSH;

    /* Check for special encodings */
    uint8_t change_bits = changes & 0x0F; /* Lower 4 bits (excluding C and PUSH) */

    switch (change_bits) {
        case VJ_SPECIAL_I: /* Interactive: advance ack and seq by prior payload length */
            {
                uint16_t old_ip_len = vj_get16(cs->hdr + IP_LEN);
                if (old_ip_len < total_hdr) { ctx->flags |= VJ_FLAG_TOSS; return 0; }
                uint16_t payload = old_ip_len - (uint16_t) total_hdr;
                uint32_t ack = vj_get32(tcp_hdr + TCP_ACK) + payload;
                vj_put32(tcp_hdr + TCP_ACK, ack);
                uint32_t seq = vj_get32(tcp_hdr + TCP_SEQ) + payload;
                vj_put32(tcp_hdr + TCP_SEQ, seq);
            }
            break;

        case VJ_SPECIAL_D: /* Unidirectional data: seq += data length of previous packet */
            {
                uint16_t old_ip_len = vj_get16(cs->hdr + IP_LEN);
                uint16_t payload    = old_ip_len - (uint16_t) total_hdr;
                uint32_t seq        = vj_get32(tcp_hdr + TCP_SEQ) + payload;
                vj_put32(tcp_hdr + TCP_SEQ, seq);
            }
            break;

        default: /* General case: apply individual deltas */
            if (change_bits & VJ_NEW_U) {
                uint16_t delta;
                int      n = vj_decode_delta(in + pos, in_len - pos, &delta);
                if (n < 0) { ctx->flags |= VJ_FLAG_TOSS; return 0; }
                pos += n;
                vj_put16(tcp_hdr + TCP_URGP, delta);
                tcp_hdr[TCP_FLAGS] |= TCP_FLAG_URG;
            } else {
                tcp_hdr[TCP_FLAGS] &= ~TCP_FLAG_URG;
            }

            if (change_bits & VJ_NEW_W) {
                uint16_t delta;
                int      n = vj_decode_delta(in + pos, in_len - pos, &delta);
                if (n < 0) { ctx->flags |= VJ_FLAG_TOSS; return 0; }
                pos += n;
                uint16_t win = vj_get16(tcp_hdr + TCP_WIN) + delta;
                vj_put16(tcp_hdr + TCP_WIN, win);
            }

            if (change_bits & VJ_NEW_A) {
                uint16_t delta;
                int      n = vj_decode_delta(in + pos, in_len - pos, &delta);
                if (n < 0) { ctx->flags |= VJ_FLAG_TOSS; return 0; }
                pos += n;
                uint32_t ack = vj_get32(tcp_hdr + TCP_ACK) + delta;
                vj_put32(tcp_hdr + TCP_ACK, ack);
            }

            if (change_bits & VJ_NEW_S) {
                uint16_t delta;
                int      n = vj_decode_delta(in + pos, in_len - pos, &delta);
                if (n < 0) { ctx->flags |= VJ_FLAG_TOSS; return 0; }
                pos += n;
                uint32_t seq = vj_get32(tcp_hdr + TCP_SEQ) + delta;
                vj_put32(tcp_hdr + TCP_SEQ, seq);
            }
            break;
    }

    uint16_t ip_id_delta = 1;
    if (changes & VJ_NEW_I) {
        int n = vj_decode_delta(in + pos, in_len - pos, &ip_id_delta);
        if (n < 0) { ctx->flags |= VJ_FLAG_TOSS; return 0; }
        pos += n;
    }

    /* IP ID increments by one unless the packet carries an explicit delta. */
    {
        uint16_t id = vj_get16(hdr + IP_ID) + ip_id_delta;
        vj_put16(hdr + IP_ID, id);
    }

    /* Compute payload length and set IP total length */
    int payload = in_len - pos;
    {
        uint16_t ip_total = (uint16_t) (total_hdr + payload);
        vj_put16(hdr + IP_LEN, ip_total);
    }

    /* Recompute IP checksum */
    vj_recompute_ip_cksum(hdr, ip_hdr_len);

    /* Save updated state */
    memcpy(cs->hdr, hdr, total_hdr);

    /* Assemble output: header + payload */
    memcpy(out, hdr, total_hdr);
    if (payload > 0)
        memcpy(out + total_hdr, in + pos, payload);

    return total_hdr + payload;
}

int
cslip_decompress_packet(cslip_ctx_t *ctx, const uint8_t *in, int in_len,
                        uint8_t *out)
{
    if (!ctx || !in || in_len <= 0 || !out)
        return 0;

    int type = VJ_TYPE_IP;

    if (in_len > 0) {
        uint8_t first = in[0];
        if (first & 0x80)
            type = VJ_TYPE_COMPRESSED_TCP;
        else if (first >= 0x70) {
            /*
             * CSLIP marks an uncompressed TCP packet in the high bits of
             * the first IP header byte. Restore the IPv4 version/IHL before
             * passing the packet to the normal VJ decoder; its protocol
             * field contains the connection slot number.
             */
            type = VJ_TYPE_UNCOMPRESSED_TCP;
            memcpy(out, in, in_len);
            out[IP_VHL] &= (uint8_t) ~VJ_UNCOMPRESSED_MARKER;
            in = out;
        }
        else if (in_len >= 20 && (first >> 4) == 4) {
            int      ip_hdr_len = (first & 0x0F) * 4;
            uint16_t ip_len     = vj_get16(in + IP_LEN);

            if (ip_hdr_len >= 20 && ip_hdr_len <= in_len
                && ip_len >= ip_hdr_len + 20 && ip_len <= in_len) {
                uint8_t proto = in[IP_PROTO];
                bool    legacy_marker = (proto & 0xF0) == VJ_TYPE_UNCOMPRESSED_TCP;

                /* Accept the older 0x7x protocol-field marker as well. */
                if (legacy_marker) {
                    int tcp_hdr_len = (in[ip_hdr_len + TCP_DOFF] >> 4) * 4;

                    if (tcp_hdr_len >= 20
                        && tcp_hdr_len <= ip_len - ip_hdr_len)
                        type = VJ_TYPE_UNCOMPRESSED_TCP;
                }
            }
        }
    }

    MODEM_DEBUG_LOG(ctx->log, "CSLIP: RX VJ type=%s (%d) input=%d toss=%u\n",
                    cslip_vj_type_name(type), type, in_len,
                    (unsigned) !!(ctx->flags & VJ_FLAG_TOSS));
    int output_len = cslip_decompress(ctx, in, in_len, out, type);
    if (output_len <= 0) {
        MODEM_DEBUG_LOG(ctx->log, "CSLIP: RX decompression failed type=%s toss=%u\n",
                        cslip_vj_type_name(type),
                        (unsigned) !!(ctx->flags & VJ_FLAG_TOSS));
        return output_len;
    }

    MODEM_DEBUG_LOG(ctx->log, "CSLIP: RX decoded IPv4 type=%s bytes=%d\n",
                    cslip_vj_type_name(type), output_len);
    return output_len;
}
