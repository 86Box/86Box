/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NetBIOS name server (NBNS, known to Windows as WINS) for the
 *          virtual server. RFC 1001, RFC 1002.
 *
 *          The name logic knows nothing about PPP or SLiRP: it takes a
 *          message and returns a reply, so other transports can reuse it.
 *          With a single client behind each server there is nobody to
 *          replicate to and no second owner to challenge, so WINS
 *          replication and the challenge exchange are not implemented.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/network/net_vserver_config.h>
#include <86box/log.h>
#include <86box/network/net_nbns.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
nbns_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define nbns_log(priv, fmt, ...)
#endif

#define NBNS_MAX_ENTRIES     128
#define NBNS_MAX_GROUP_ADDRS 25     /* the most a <1C> list holds on NT4 WINS */
#define NBNS_TTL_S           518400 /* six days, the NT4 WINS renewal interval */
#define NBNS_NAME_LENGTH     16
#define NBNS_HEADER_LENGTH   12

/* Header flags (RFC 1002 section 4.2.1.1). */
#define NBNS_FLAG_RESPONSE 0x8000
#define NBNS_OPCODE_SHIFT  11
#define NBNS_OPCODE_MASK   0x0F
#define NBNS_FLAG_AA       0x0400
#define NBNS_FLAG_RD       0x0100
#define NBNS_FLAG_RA       0x0080

/* The RFC lists refresh as 8 in one place and 9 in another; both are seen in practice.
   15 is the Microsoft multihomed registration. */
#define NBNS_OP_QUERY        0
#define NBNS_OP_REGISTER     5
#define NBNS_OP_RELEASE      6
#define NBNS_OP_REFRESH      8
#define NBNS_OP_REFRESH_ALT  9
#define NBNS_OP_MULTIHOMED   15

#define NBNS_TYPE_NB     0x0020
#define NBNS_TYPE_NBSTAT 0x0021
#define NBNS_TYPE_NULL   0x000A
#define NBNS_CLASS_IN    0x0001

#define NBNS_RCODE_FMT 1
#define NBNS_RCODE_NAM 3
#define NBNS_RCODE_RFS 5
#define NBNS_RCODE_ACT 6

#define NBNS_NB_GROUP    0x8000
#define NBNS_NB_PNODE    0x2000
#define NBNS_NAME_ACTIVE 0x0400

#define NBNS_SUFFIX_WORKSTATION 0x00
#define NBNS_SUFFIX_MESSENGER   0x03
#define NBNS_SUFFIX_SERVER      0x20
#define NBNS_SUFFIX_BROWSER     0x1E
#define NBNS_SUFFIX_DOMAIN      0x1C
#define NBNS_SUFFIX_DOMAIN_MASTER  0x1B
#define NBNS_SUFFIX_MASTER_BROWSER 0x1D
#define NBNS_SUFFIX_MSBROWSE    0x01

typedef struct nbns_entry_t {
    bool     used;
    bool     group;
    bool     permanent;
    uint8_t  name[NBNS_NAME_LENGTH];
    uint16_t nb_flags;
    uint32_t addrs[NBNS_MAX_GROUP_ADDRS];
    int      addr_count;
    uint64_t expires_ms;
} nbns_entry_t;

struct nbns_t {
    void        *log;
    uint32_t     server_ip;
    uint16_t     ip_id;
    uint64_t     now_ms;
    bool         broadcast; /* answering a broadcast, where silence means no */
    nbns_entry_t entries[NBNS_MAX_ENTRIES];
};

/* The parts of a request every reply needs. */
typedef struct nbns_request_t {
    uint16_t       id;
    uint16_t       flags;
    const uint8_t *encoded_name; /* as sent, including any scope */
    int            encoded_len;
    uint8_t        name[NBNS_NAME_LENGTH];
    uint16_t       qtype;
    uint16_t       nb_flags;
    uint32_t       address;
} nbns_request_t;

static void
nbns_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) (value >> 8);
    p[1] = (uint8_t) value;
}

static void
nbns_put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t) (value >> 24);
    p[1] = (uint8_t) (value >> 16);
    p[2] = (uint8_t) (value >> 8);
    p[3] = (uint8_t) value;
}

static uint16_t
nbns_get16(const uint8_t *p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}

static uint32_t
nbns_get32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static void
nbns_make_name(uint8_t *name, const char *text, uint8_t suffix)
{
    size_t length = strlen(text);

    memset(name, ' ', NBNS_NAME_LENGTH);
    memcpy(name, text, length < 15 ? length : 15);
    name[15] = suffix;
}

/* Walks a first-level encoded name. Returns its length in octets, or 0 if malformed. */
static int
nbns_decode_name(const uint8_t *msg, int len, int pos, uint8_t *name)
{
    int start = pos;

    if (pos + 34 > len || msg[pos] != 32)
        return 0;
    for (int i = 0; i < NBNS_NAME_LENGTH; i++) {
        uint8_t high = msg[pos + 1 + i * 2];
        uint8_t low  = msg[pos + 2 + i * 2];

        if (high < 'A' || high > 'P' || low < 'A' || low > 'P')
            return 0;
        name[i] = (uint8_t) (((high - 'A') << 4) | (low - 'A'));
    }
    pos += 33;

    /* Scope labels, if any, end with a zero length. */
    while (pos < len && msg[pos] != 0) {
        if (msg[pos] > 63)
            return 0;
        pos += msg[pos] + 1;
    }
    if (pos >= len || pos - start + 1 > 255)
        return 0;
    return pos - start + 1;
}

/* Skips the name at the start of a resource record, usually a pointer. */
static int
nbns_skip_rr_name(const uint8_t *msg, int len, int pos)
{
    uint8_t scratch[NBNS_NAME_LENGTH];

    if (pos >= len)
        return 0;
    if ((msg[pos] & 0xC0) == 0xC0)
        return pos + 2 <= len ? 2 : 0;
    return nbns_decode_name(msg, len, pos, scratch);
}

static nbns_entry_t *
nbns_find(nbns_t *nbns, const uint8_t *name)
{
    for (int i = 0; i < NBNS_MAX_ENTRIES; i++) {
        nbns_entry_t *entry = &nbns->entries[i];

        if (!entry->used || memcmp(entry->name, name, NBNS_NAME_LENGTH) != 0)
            continue;
        if (!entry->permanent && entry->expires_ms <= nbns->now_ms) {
            entry->used = false;
            return NULL;
        }
        return entry;
    }
    return NULL;
}

static nbns_entry_t *
nbns_allocate(nbns_t *nbns, const uint8_t *name)
{
    for (int i = 0; i < NBNS_MAX_ENTRIES; i++) {
        nbns_entry_t *entry = &nbns->entries[i];

        if (entry->used && (entry->permanent || entry->expires_ms > nbns->now_ms))
            continue;
        memset(entry, 0, sizeof(*entry));
        entry->used = true;
        memcpy(entry->name, name, NBNS_NAME_LENGTH);
        return entry;
    }
    return NULL;
}

static void
nbns_add_permanent(nbns_t *nbns, const char *text, uint8_t suffix, bool group)
{
    uint8_t       name[NBNS_NAME_LENGTH];
    nbns_entry_t *entry;

    nbns_make_name(name, text, suffix);
    entry = nbns_allocate(nbns, name);
    if (!entry)
        return;
    entry->permanent = true;
    entry->group     = group;
    entry->nb_flags  = (uint16_t) (NBNS_NB_PNODE | (group ? NBNS_NB_GROUP : 0));
    if (!group || suffix == NBNS_SUFFIX_DOMAIN) {
        entry->addrs[0]   = nbns->server_ip;
        entry->addr_count = 1;
    }
}

static bool
nbns_is_internet_group(const nbns_entry_t *entry)
{
    return entry->group && entry->name[15] == NBNS_SUFFIX_DOMAIN;
}

static int
nbns_begin_reply(const nbns_request_t *req, uint8_t *reply, int cap, uint16_t flags, uint16_t answers)
{
    if (cap < NBNS_HEADER_LENGTH + req->encoded_len + 10)
        return 0;
    memset(reply, 0, NBNS_HEADER_LENGTH);
    nbns_put16(reply, req->id);
    nbns_put16(reply + 2, flags);
    nbns_put16(reply + 6, answers);
    memcpy(reply + NBNS_HEADER_LENGTH, req->encoded_name, (size_t) req->encoded_len);
    return NBNS_HEADER_LENGTH + req->encoded_len;
}

/* Writes the fixed part of a resource record after its name. */
static int
nbns_put_rr(uint8_t *reply, int pos, uint16_t type, uint32_t ttl, uint16_t rdlength)
{
    nbns_put16(reply + pos, type);
    nbns_put16(reply + pos + 2, NBNS_CLASS_IN);
    nbns_put32(reply + pos + 4, ttl);
    nbns_put16(reply + pos + 8, rdlength);
    return pos + 10;
}

/* Registration and release share one reply format (RFC 1002 sections 4.2.5, 4.2.6,
   4.2.10 and 4.2.11). A refresh is answered as a registration. */
static int
nbns_reply_name(const nbns_request_t *req, uint8_t *reply, int cap, int opcode, uint8_t rcode,
                uint32_t ttl, uint16_t nb_flags, uint32_t address)
{
    uint16_t flags = (uint16_t) (NBNS_FLAG_RESPONSE | (opcode << NBNS_OPCODE_SHIFT) | NBNS_FLAG_AA | rcode);
    int      pos;

    if (opcode == NBNS_OP_REGISTER)
        flags |= NBNS_FLAG_RD | NBNS_FLAG_RA;

    pos = nbns_begin_reply(req, reply, cap, flags, 1);
    if (!pos || pos + 16 > cap)
        return 0;
    pos = nbns_put_rr(reply, pos, NBNS_TYPE_NB, ttl, 6);
    nbns_put16(reply + pos, nb_flags);
    nbns_put32(reply + pos + 2, address);
    return pos + 6;
}

static int
nbns_handle_register(nbns_t *nbns, const nbns_request_t *req, uint8_t *reply, int cap)
{
    bool          group = (req->nb_flags & NBNS_NB_GROUP) != 0;
    nbns_entry_t *entry = nbns_find(nbns, req->name);
    uint8_t       rcode = 0;
    uint32_t      owner = req->address;

    if (entry && entry->group != group) {
        rcode = NBNS_RCODE_ACT;
        owner = entry->addr_count ? entry->addrs[0] : 0xFFFFFFFFu;
    } else if (entry && !group && entry->addrs[0] != req->address) {
        rcode = NBNS_RCODE_ACT;
        owner = entry->addrs[0];
    } else if (!entry) {
        entry = nbns_allocate(nbns, req->name);
        if (entry) {
            entry->group    = group;
            entry->nb_flags = req->nb_flags;
            if (!group) {
                entry->addrs[0]   = req->address;
                entry->addr_count = 1;
            }
        } else {
            rcode = NBNS_RCODE_RFS;
        }
    }

    if (!rcode && nbns_is_internet_group(entry)) {
        bool known = false;

        for (int i = 0; i < entry->addr_count; i++)
            known = known || entry->addrs[i] == req->address;
        if (!known && entry->addr_count >= NBNS_MAX_GROUP_ADDRS)
            rcode = NBNS_RCODE_RFS;
        else if (!known)
            entry->addrs[entry->addr_count++] = req->address;
    }

    if (!rcode && !entry->permanent)
        entry->expires_ms = nbns->now_ms + (uint64_t) NBNS_TTL_S * 1000;

    nbns_log(nbns->log, "NBNS: Registration of %.15s<%02X> %s (rcode %u)\n", req->name, req->name[15],
             rcode ? "refused" : "accepted", rcode);
    return nbns_reply_name(req, reply, cap, NBNS_OP_REGISTER, rcode, rcode ? 0 : NBNS_TTL_S,
                           req->nb_flags, owner);
}

static int
nbns_handle_release(nbns_t *nbns, const nbns_request_t *req, uint8_t *reply, int cap)
{
    nbns_entry_t *entry = nbns_find(nbns, req->name);
    uint8_t       rcode = 0;

    if (entry && !entry->permanent) {
        if (!entry->group && entry->addrs[0] != req->address) {
            rcode = NBNS_RCODE_ACT;
        } else if (nbns_is_internet_group(entry)) {
            for (int i = 0; i < entry->addr_count; i++) {
                if (entry->addrs[i] == req->address) {
                    entry->addrs[i] = entry->addrs[--entry->addr_count];
                    break;
                }
            }
            if (!entry->addr_count)
                entry->used = false;
        } else {
            entry->used = false;
        }
    } else if (entry && !entry->group) {
        rcode = NBNS_RCODE_ACT; /* the guest cannot give away the server's own name */
    }

    nbns_log(nbns->log, "NBNS: Release of %.15s<%02X> (rcode %u)\n", req->name, req->name[15], rcode);
    return nbns_reply_name(req, reply, cap, NBNS_OP_RELEASE, rcode, 0, req->nb_flags, req->address);
}

static int
nbns_reply_negative_query(const nbns_request_t *req, uint8_t *reply, int cap)
{
    uint16_t flags = NBNS_FLAG_RESPONSE | NBNS_FLAG_AA | NBNS_FLAG_RD | NBNS_FLAG_RA | NBNS_RCODE_NAM;
    int      pos   = nbns_begin_reply(req, reply, cap, flags, 0);

    if (!pos)
        return 0;
    return nbns_put_rr(reply, pos, NBNS_TYPE_NULL, 0, 0);
}

static int
nbns_handle_query(nbns_t *nbns, const nbns_request_t *req, uint8_t *reply, int cap)
{
    nbns_entry_t *entry = nbns_find(nbns, req->name);
    int           count;
    int           pos;

    if (nbns->broadcast
        && (!entry || !entry->permanent || (entry->group && !nbns_is_internet_group(entry))))
        return 0;

    if (!entry) {
        nbns_log(nbns->log, "NBNS: Query for unknown name %.15s<%02X>\n", req->name, req->name[15]);
        return nbns_reply_negative_query(req, reply, cap);
    }

    /* A normal group has no address list: its members are reached by broadcast. */
    count = (entry->group && !nbns_is_internet_group(entry)) ? 1 : entry->addr_count;
    pos   = nbns_begin_reply(req, reply, cap,
                             NBNS_FLAG_RESPONSE | NBNS_FLAG_AA | NBNS_FLAG_RD | NBNS_FLAG_RA, 1);
    if (!pos || pos + 10 + count * 6 > cap)
        return 0;
    pos = nbns_put_rr(reply, pos, NBNS_TYPE_NB, NBNS_TTL_S, (uint16_t) (count * 6));
    for (int i = 0; i < count; i++) {
        uint32_t address = (entry->group && !nbns_is_internet_group(entry)) ? 0xFFFFFFFFu
                                                                             : entry->addrs[i];
        nbns_put16(reply + pos, entry->nb_flags);
        nbns_put32(reply + pos + 2, address);
        pos += 6;
    }
    return pos;
}

/* Node status of the virtual server itself (RFC 1002 section 4.2.18). */
static int
nbns_handle_node_status(nbns_t *nbns, const nbns_request_t *req, uint8_t *reply, int cap)
{
    enum { NODE_NAME_LENGTH = 18, STATISTICS_LENGTH = 46 };
    int count = 0;
    int pos;

    for (int i = 0; i < NBNS_MAX_ENTRIES; i++)
        count += nbns->entries[i].used && nbns->entries[i].permanent;

    pos = nbns_begin_reply(req, reply, cap, NBNS_FLAG_RESPONSE | NBNS_FLAG_AA, 1);
    if (!pos || pos + 11 + count * NODE_NAME_LENGTH + STATISTICS_LENGTH > cap)
        return 0;
    pos = nbns_put_rr(reply, pos, NBNS_TYPE_NBSTAT, 0,
                      (uint16_t) (1 + count * NODE_NAME_LENGTH + STATISTICS_LENGTH));
    reply[pos++] = (uint8_t) count;
    for (int i = 0; i < NBNS_MAX_ENTRIES; i++) {
        const nbns_entry_t *entry = &nbns->entries[i];

        if (!entry->used || !entry->permanent)
            continue;
        memcpy(reply + pos, entry->name, NBNS_NAME_LENGTH);
        nbns_put16(reply + pos + NBNS_NAME_LENGTH,
                   (uint16_t) (NBNS_NAME_ACTIVE | (entry->group ? NBNS_NB_GROUP : 0)));
        pos += NODE_NAME_LENGTH;
    }
    memset(reply + pos, 0, STATISTICS_LENGTH);
    return pos + STATISTICS_LENGTH;
}

int
nbns_process(nbns_t *nbns, uint32_t src_ip, const uint8_t *msg, int len, uint8_t *reply, int reply_cap)
{
    nbns_request_t req;
    int            opcode;
    int            pos = NBNS_HEADER_LENGTH;

    (void) src_ip;
    if (!nbns || !msg || !reply || len < NBNS_HEADER_LENGTH)
        return 0;

    memset(&req, 0, sizeof(req));
    req.id    = nbns_get16(msg);
    req.flags = nbns_get16(msg + 2);
    if ((req.flags & NBNS_FLAG_RESPONSE) || nbns_get16(msg + 4) != 1)
        return 0;
    opcode = (req.flags >> NBNS_OPCODE_SHIFT) & NBNS_OPCODE_MASK;

    req.encoded_name = msg + pos;
    req.encoded_len  = nbns_decode_name(msg, len, pos, req.name);
    if (!req.encoded_len)
        return 0;
    pos += req.encoded_len;
    if (pos + 4 > len)
        return 0;
    req.qtype = nbns_get16(msg + pos);
    if (nbns_get16(msg + pos + 2) != NBNS_CLASS_IN)
        return 0;
    pos += 4;

    /* Nothing listens to a broadcast registration: it's a guest defending its own name. */
    if (nbns->broadcast && (opcode != NBNS_OP_QUERY || req.qtype != NBNS_TYPE_NB))
        return 0;

    if (opcode == NBNS_OP_QUERY) {
        if (req.qtype == NBNS_TYPE_NBSTAT)
            return nbns_handle_node_status(nbns, &req, reply, reply_cap);
        if (req.qtype == NBNS_TYPE_NB)
            return nbns_handle_query(nbns, &req, reply, reply_cap);
        return 0;
    }

    if (opcode != NBNS_OP_REGISTER && opcode != NBNS_OP_REFRESH && opcode != NBNS_OP_REFRESH_ALT
        && opcode != NBNS_OP_MULTIHOMED && opcode != NBNS_OP_RELEASE)
        return 0;

    /* Everything else carries the address in one additional record. */
    {
        int name_length = nbns_get16(msg + 10) == 1 ? nbns_skip_rr_name(msg, len, pos) : 0;

        if (!name_length || pos + name_length + 16 > len
            || nbns_get16(msg + pos + name_length + 8) < 6)
            return nbns_reply_name(&req, reply, reply_cap,
                                   opcode == NBNS_OP_RELEASE ? NBNS_OP_RELEASE : NBNS_OP_REGISTER,
                                   NBNS_RCODE_FMT, 0, 0, 0);
        req.nb_flags = nbns_get16(msg + pos + name_length + 10);
        req.address  = nbns_get32(msg + pos + name_length + 12);
    }

    if (opcode == NBNS_OP_RELEASE)
        return nbns_handle_release(nbns, &req, reply, reply_cap);
    return nbns_handle_register(nbns, &req, reply, reply_cap);
}

static uint16_t
nbns_checksum(uint32_t sum, const uint8_t *data, int len)
{
    for (int i = 0; i + 1 < len; i += 2)
        sum += nbns_get16(data + i);
    if (len & 1)
        sum += (uint32_t) data[len - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t) ~sum;
}

static int
nbns_ipv4(nbns_t *nbns, const uint8_t *ip_pkt, int len, uint8_t *reply, int reply_cap, bool broadcast)
{
    enum { IP_HEADER_LENGTH = 20, UDP_HEADER_LENGTH = 8, IP_PROTOCOL_UDP = 17 };
    int      header;
    int      udp_len;
    int      message;
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t checksum;

    if (!nbns || !ip_pkt || len < IP_HEADER_LENGTH + UDP_HEADER_LENGTH || (ip_pkt[0] >> 4) != 4)
        return -1;
    header = (ip_pkt[0] & 0x0F) * 4;
    if (header < IP_HEADER_LENGTH || header + UDP_HEADER_LENGTH > len
        || ip_pkt[9] != IP_PROTOCOL_UDP || (!broadcast && nbns_get32(ip_pkt + 16) != nbns->server_ip)
        || nbns_get16(ip_pkt + header + 2) != NBNS_PORT)
        return -1;

    /* From here on the packet is ours, even if it turns out to be unusable. */
    udp_len = nbns_get16(ip_pkt + header + 4);
    if (udp_len < UDP_HEADER_LENGTH || header + udp_len > len
        || (nbns_get16(ip_pkt + 6) & 0x3FFF) != 0 /* fragments */
        || reply_cap < IP_HEADER_LENGTH + UDP_HEADER_LENGTH)
        return 0;

    src_ip   = nbns_get32(ip_pkt + 12);
    src_port = nbns_get16(ip_pkt + header);
    nbns->broadcast = broadcast;
    message  = nbns_process(nbns, src_ip, ip_pkt + header + UDP_HEADER_LENGTH,
                            udp_len - UDP_HEADER_LENGTH,
                            reply + IP_HEADER_LENGTH + UDP_HEADER_LENGTH,
                            reply_cap - IP_HEADER_LENGTH - UDP_HEADER_LENGTH);
    nbns->broadcast = false;
    if (message <= 0)
        return 0;

    udp_len = UDP_HEADER_LENGTH + message;
    memset(reply, 0, IP_HEADER_LENGTH + UDP_HEADER_LENGTH);
    reply[0] = 0x45;
    nbns_put16(reply + 2, (uint16_t) (IP_HEADER_LENGTH + udp_len));
    nbns_put16(reply + 4, nbns->ip_id++);
    reply[8] = 128;
    reply[9] = IP_PROTOCOL_UDP;
    nbns_put32(reply + 12, nbns->server_ip);
    nbns_put32(reply + 16, src_ip);
    nbns_put16(reply + 10, nbns_checksum(0, reply, IP_HEADER_LENGTH));

    nbns_put16(reply + IP_HEADER_LENGTH, NBNS_PORT);
    nbns_put16(reply + IP_HEADER_LENGTH + 2, src_port);
    nbns_put16(reply + IP_HEADER_LENGTH + 4, (uint16_t) udp_len);
    checksum = nbns_checksum((nbns->server_ip >> 16) + (nbns->server_ip & 0xFFFF)
                                 + (src_ip >> 16) + (src_ip & 0xFFFF)
                                 + IP_PROTOCOL_UDP + (uint32_t) udp_len,
                             reply + IP_HEADER_LENGTH, udp_len);
    /* In UDP an all-zero checksum means "none", so a computed zero is sent as all ones. */
    nbns_put16(reply + IP_HEADER_LENGTH + 6, checksum ? checksum : 0xFFFF);
    return IP_HEADER_LENGTH + udp_len;
}

int
nbns_process_ipv4(nbns_t *nbns, const uint8_t *ip_pkt, int len, uint8_t *reply, int reply_cap)
{
    return nbns_ipv4(nbns, ip_pkt, len, reply, reply_cap, false);
}

int
nbns_process_broadcast(nbns_t *nbns, const uint8_t *ip_pkt, int len, uint8_t *reply, int reply_cap)
{
    return nbns_ipv4(nbns, ip_pkt, len, reply, reply_cap, true);
}

void
nbns_tick(nbns_t *nbns)
{
    if (nbns)
        nbns->now_ms++;
}

nbns_t *
nbns_init(void *log, uint32_t server_ip)
{
    nbns_t *nbns = (nbns_t *) calloc(1, sizeof(nbns_t));

    if (!nbns)
        return NULL;

    nbns->log       = log;
    nbns->server_ip = server_ip;
    nbns_add_permanent(nbns, VSERVER_SERVER_NAME, NBNS_SUFFIX_WORKSTATION, false);
    nbns_add_permanent(nbns, VSERVER_SERVER_NAME, NBNS_SUFFIX_MESSENGER, false);
    nbns_add_permanent(nbns, VSERVER_SERVER_NAME, NBNS_SUFFIX_SERVER, false);
    nbns_add_permanent(nbns, VSERVER_WORKGROUP_NAME, NBNS_SUFFIX_WORKSTATION, true);
    nbns_add_permanent(nbns, VSERVER_WORKGROUP_NAME, NBNS_SUFFIX_BROWSER, true);
    return nbns;
}

/* The names that make the virtual server the workgroup's master browser. */
void
nbns_add_browser_names(nbns_t *nbns)
{
    static const char msbrowse[] = "\x01\x02__MSBROWSE__\x02";

    if (!nbns)
        return;
    nbns_add_permanent(nbns, VSERVER_WORKGROUP_NAME, NBNS_SUFFIX_DOMAIN, true);
    nbns_add_permanent(nbns, VSERVER_WORKGROUP_NAME, NBNS_SUFFIX_DOMAIN_MASTER, false);
    nbns_add_permanent(nbns, VSERVER_WORKGROUP_NAME, NBNS_SUFFIX_MASTER_BROWSER, false);
    nbns_add_permanent(nbns, msbrowse, NBNS_SUFFIX_MSBROWSE, true);
}

void
nbns_close(nbns_t *nbns)
{
    free(nbns);
}
