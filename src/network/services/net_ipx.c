/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IPX for the virtual server. Two things carry SMB
 *          over IPX, and which one a guest uses depends on how it was set up:
 *
 *          - Direct hosting, the default on Windows 95 and Windows for
 *            Workgroups: SMB in bare datagrams on sockets 0x550 to 0x553,
 *            with the client doing all the retrying. [MS-CIFS] 2.1.2.1.
 *          - NetBIOS over IPX, on socket 0x455, which NT clients need: names,
 *            datagrams and sessions much like NetBEUI's.
 *
 *          Novell never published the second one. Its session format here
 *          comes from reading what the Windows for Workgroups 3.11 driver
 *          NWNBLINK.386 builds and checks.
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
#include <86box/network/net_browser.h>
#include <86box/network/net_ipx.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_smb.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
ipx_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ipx_log(priv, fmt, ...)
#endif

/* IPX itself. Every field is big-endian. */
#define IPX_HEADER_LENGTH   30
#define IPX_ADDRESS_LENGTH  12
#define IPX_TYPE_RIP        1
#define IPX_TYPE_PEP        4
#define IPX_TYPE_NETBIOS    20

#define IPX_SOCKET_RIP      0x0453
#define IPX_SOCKET_NETBIOS  0x0455
#define IPX_SOCKET_SMB      0x0550
#define IPX_SOCKET_NAME     0x0551
#define IPX_SOCKET_MAILSLOT 0x0553

#define RIP_REQUEST         1
#define RIP_RESPONSE        2
#define RIP_ENTRY_LENGTH    8
#define RIP_MAX_ENTRIES     50

/* What sits ahead of a broadcast: room for the eight networks it may cross. */
#define IPX_ROUTES_LENGTH   32

/* NetBIOS over IPX. Session fields are little-endian. */
#define NB_NAME_LENGTH      16
#define NB_NAME_FRAME       (IPX_ROUTES_LENGTH + 2 + NB_NAME_LENGTH)
#define NB_FIND_NAME        1
#define NB_NAME_RECOGNIZED  2
#define NB_CHECK_NAME       3
#define NB_NAME_IN_USE      4
#define NB_SESSION_DATA     6
#define NB_SESSION_END      7
#define NB_SESSION_END_ACK  8
#define NB_DATAGRAM         11
#define NB_FLAG_GROUP       0x80
#define NB_FLAG_IN_USE      0x40
#define NB_FLAG_REGISTERED  0x04

#define NB_SESSION_HEADER   18
#define NB_INIT_LENGTH      38
#define NB_CONTROL_SYSTEM   0x80
#define NB_CONTROL_ACK      0x40
#define NB_CONTROL_EOM      0x10
#define NB_CONTROL_RESEND   0x08
#define NB_NEW_SESSION      0xFFFF
#define NB_MAX_SESSIONS     8
#define NB_MAX_MESSAGE      8192
#define NB_RETRY_MS         2000
#define NB_MAX_RETRIES      10

/* Direct hosting. */
#define DH_QUERY            0xF3
#define DH_FOUND            0xF4
#define DH_MAILSLOT_SEND    0xFC
#define DH_MAILSLOT_FIND    0xFD
#define DH_MAILSLOT_NAME    0xFE
#define DH_HEADER           (IPX_ROUTES_LENGTH + 4 + NB_NAME_LENGTH * 2)
#define DH_MAX_CLIENTS      4
#define DH_IDLE_MS          (10 * 60 * 1000) /* [MS-CIFS] asks for at least five minutes */

#define SMB_HEADER_LENGTH   32
#define SMB_OFFSET_COMMAND  4
#define SMB_OFFSET_STATUS   5
#define SMB_OFFSET_FLAGS    9
#define SMB_OFFSET_FEATURES 14
#define SMB_OFFSET_CID      18
#define SMB_OFFSET_SEQUENCE 20
#define SMB_COM_NEGOTIATE   0x72
#define SMB_FLAGS_REPLY     0x80
#define SMB_ERRSRV          2
#define SMB_ERRerror        1
#define SMB_ERRinvsess      0x10

#define NBT_HEADER_LENGTH   4
#define NBT_MESSAGE         0x00
#define DGM_HEADER_LENGTH   14
#define DGM_DIRECT_GROUP    0x11
#define DGM_NAME_LENGTH     34
#define DGM_MAX             1024

#define ANNOUNCE_PERIOD_MS  (60 * 1000)
#define ANNOUNCE_FIRST_MS   2000

typedef struct ipx_address_t {
    uint8_t network[4];
    uint8_t node[IPX_NODE_LENGTH];
    uint8_t socket[2];
} ipx_address_t;

/* One NetBIOS session. Only one packet is ever in flight in either direction. */
typedef struct nb_session_t {
    ipx_address_t  peer;
    smb_session_t *smb;
    uint16_t       local;
    uint16_t       remote;
    uint8_t        init[NB_INIT_LENGTH]; /* what the guest was told, should it ask again */
    uint16_t       max_data;
    uint32_t       idle_ms;

    uint16_t       rx_seq;
    int            rx_length;
    uint8_t        rx[NBT_HEADER_LENGTH + NB_MAX_MESSAGE];

    uint16_t       tx_seq;
    int            tx_pos;     /* where the NBT header of the message being sent starts */
    int            tx_len;
    int            tx_acked;   /* how much of that message has been acknowledged */
    int            in_flight;
    int            retry_ms;
    int            retries;
    uint8_t        tx[SMB_MAX_OUTPUT];
} nb_session_t;

/* One direct hosting client. The last sequenced reply is kept for when it goes astray. */
typedef struct dh_client_t {
    ipx_address_t  peer;
    smb_session_t *smb;
    uint16_t       cid;
    uint16_t       sequence;
    uint32_t       idle_ms;
    int            reply_length;
    uint8_t        reply[IPX_MAX_PACKET];
} dh_client_t;

struct ipx_t {
    void         *log;
    ipx_output_t  output;
    void         *priv;
    uint8_t       network[4];
    uint8_t       node[IPX_NODE_LENGTH];
    int           max_packet;
    uint8_t       generation;

    bool          netbios_seen;
    bool          direct_seen;
    ipx_address_t guest;
    uint32_t      announce_ms;

    nb_session_t *sessions[NB_MAX_SESSIONS];
    dh_client_t  *clients[DH_MAX_CLIENTS];
    uint8_t       request[NBT_HEADER_LENGTH + NB_MAX_MESSAGE];
    uint8_t       scratch[SMB_MAX_OUTPUT];
};

static const uint8_t ipx_broadcast[IPX_NODE_LENGTH] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static uint16_t
ipx_get16(const uint8_t *p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}

static void
ipx_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) (value >> 8);
    p[1] = (uint8_t) value;
}

static uint16_t
nb_get16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static void
nb_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) value;
    p[1] = (uint8_t) (value >> 8);
}

static void
ipx_send(ipx_t *ipx, uint8_t type, const ipx_address_t *to, uint16_t socket, const uint8_t *data, int length)
{
    uint8_t packet[IPX_MAX_PACKET];
    int     total = IPX_HEADER_LENGTH + length;

    if (total > ipx->max_packet)
        return;
    ipx_put16(packet, 0xFFFF);
    ipx_put16(packet + 2, (uint16_t) total);
    packet[4] = 0;
    packet[5] = type;
    memcpy(packet + 6, to, IPX_ADDRESS_LENGTH);
    memcpy(packet + 18, ipx->network, 4);
    memcpy(packet + 22, ipx->node, IPX_NODE_LENGTH);
    ipx_put16(packet + 28, socket);
    memcpy(packet + IPX_HEADER_LENGTH, data, (size_t) length);
    ipx->output(ipx->priv, packet, total);
}

/* --- Names --- */

static void
nb_make_name(char *name, const char *text, uint8_t suffix)
{
    memset(name, ' ', NB_NAME_LENGTH - 1);
    memcpy(name, text, strlen(text));
    name[NB_NAME_LENGTH - 1] = (char) suffix;
}

static void
nb_encode_name(uint8_t *out, const char *name)
{
    out[0] = 32;
    for (int i = 0; i < NB_NAME_LENGTH; i++) {
        out[1 + i * 2] = (uint8_t) ('A' + (((uint8_t) name[i]) >> 4));
        out[2 + i * 2] = (uint8_t) ('A' + (((uint8_t) name[i]) & 0x0F));
    }
    out[33] = 0;
}

static void
nb_decode_name(const uint8_t *in, char *name)
{
    for (int i = 0; i < NB_NAME_LENGTH; i++)
        name[i] = (char) (((in[1 + i * 2] - 'A') << 4) | ((in[2 + i * 2] - 'A') & 0x0F));
}

/* Returns the flags of a name the server answers to, or 0 for any other. */
static uint8_t
nb_name_flags(const uint8_t *name)
{
    char candidate[NB_NAME_LENGTH];

    for (int i = 0; i < 3; i++) {
        static const uint8_t server[3] = { 0x00, 0x03, 0x20 };

        nb_make_name(candidate, VSERVER_SERVER_NAME, server[i]);
        if (!memcmp(candidate, name, NB_NAME_LENGTH))
            return NB_FLAG_IN_USE | NB_FLAG_REGISTERED;
    }
    for (int i = 0; i < 3; i++) {
        static const uint8_t domain[3] = { 0x1B, 0x1C, 0x1D };

        nb_make_name(candidate, VSERVER_WORKGROUP_NAME, domain[i]);
        if (!memcmp(candidate, name, NB_NAME_LENGTH))
            return NB_FLAG_IN_USE | NB_FLAG_REGISTERED | (domain[i] == 0x1C ? NB_FLAG_GROUP : 0);
    }
    return 0;
}

static bool
nb_name_accepts_sessions(const uint8_t *name)
{
    char candidate[NB_NAME_LENGTH];

    nb_make_name(candidate, VSERVER_SERVER_NAME, 0x20);
    return !memcmp(candidate, name, NB_NAME_LENGTH);
}

/* Direct hosting names are plain text, whatever the sender padded them with. */
static bool
dh_name_is(const uint8_t *name, const char *text)
{
    size_t length = strlen(text);

    for (size_t i = 0; i < NB_NAME_LENGTH - 1; i++) {
        uint8_t c = name[i];

        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        if (i < length ? c != (uint8_t) text[i] : c != ' ' && c != 0)
            return false;
    }
    return true;
}

/* --- Datagrams --- */

/* Sends a datagram the browser built for UDP, the way the guest's transport wants it. */
static void
ipx_send_datagram(ipx_t *ipx, const ipx_address_t *to, bool direct, const uint8_t *packet, int length)
{
    const int names = DGM_HEADER_LENGTH + DGM_NAME_LENGTH * 2;
    uint8_t   frame[DH_HEADER + DGM_MAX];
    bool      broadcast = !memcmp(to->node, ipx_broadcast, IPX_NODE_LENGTH);
    int       pos       = 0;

    if (length <= names || length - names > DGM_MAX)
        return;
    memset(frame, 0, DH_HEADER);
    if (direct) {
        pos                        = IPX_ROUTES_LENGTH;
        frame[pos]                 = DH_MAILSLOT_SEND;
        frame[pos + 1]             = 2; /* to a workgroup */
        pos += 4;
    } else {
        if (broadcast)
            pos = IPX_ROUTES_LENGTH;
        frame[pos + 1] = NB_DATAGRAM;
        pos += 2;
    }
    nb_decode_name(packet + DGM_HEADER_LENGTH + DGM_NAME_LENGTH, (char *) frame + pos);
    nb_decode_name(packet + DGM_HEADER_LENGTH, (char *) frame + pos + NB_NAME_LENGTH);
    pos += NB_NAME_LENGTH * 2;
    memcpy(frame + pos, packet + names, (size_t) (length - names));
    ipx_send(ipx, direct || broadcast ? IPX_TYPE_NETBIOS : IPX_TYPE_PEP, to,
             direct ? IPX_SOCKET_MAILSLOT : IPX_SOCKET_NETBIOS, frame, pos + length - names);
}

/* Dresses a datagram as the browser expects to see one arriving over UDP. */
static void
ipx_handle_datagram(ipx_t *ipx, const ipx_address_t *from, bool direct, const uint8_t *receiver,
                    const uint8_t *sender, const uint8_t *data, int length)
{
    const int names = DGM_HEADER_LENGTH + DGM_NAME_LENGTH * 2;
    uint8_t   packet[DGM_HEADER_LENGTH + DGM_NAME_LENGTH * 2 + DGM_MAX];
    uint8_t   reply[1024];
    int       reply_length;

    if (length < 1 || length > DGM_MAX)
        return;
    memset(packet, 0, DGM_HEADER_LENGTH);
    packet[0] = DGM_DIRECT_GROUP;
    nb_encode_name(packet + DGM_HEADER_LENGTH, (const char *) sender);
    nb_encode_name(packet + DGM_HEADER_LENGTH + DGM_NAME_LENGTH, (const char *) receiver);
    memcpy(packet + names, data, (size_t) length);

    reply_length = browser_process(0, packet, names + length, reply, sizeof(reply));
    if (reply_length > 0)
        ipx_send_datagram(ipx, from, direct, reply, reply_length);
}

/* --- NetBIOS sessions --- */

static void
nb_session_header(uint8_t *out, const nb_session_t *session, uint8_t control, uint8_t type)
{
    memset(out, 0, NB_SESSION_HEADER);
    out[0] = control;
    out[1] = type;
    nb_put16(out + 2, session->local);
    nb_put16(out + 4, session->remote);
    nb_put16(out + 6, session->tx_seq);
    nb_put16(out + 14, session->rx_seq);
    nb_put16(out + 16, (uint16_t) session->rx_length);
}

static void
nb_session_send(ipx_t *ipx, const nb_session_t *session, const uint8_t *data, int length)
{
    ipx_send(ipx, IPX_TYPE_PEP, &session->peer, IPX_SOCKET_NETBIOS, data, length);
}

static void
nb_session_ack(ipx_t *ipx, const nb_session_t *session, bool resend)
{
    uint8_t header[NB_SESSION_HEADER];

    nb_session_header(header, session, NB_CONTROL_SYSTEM | (resend ? NB_CONTROL_RESEND : 0), NB_SESSION_DATA);
    nb_session_send(ipx, session, header, sizeof(header));
}

static void
nb_session_answer_init(ipx_t *ipx, const nb_session_t *session)
{
    uint8_t packet[NB_SESSION_HEADER + NB_INIT_LENGTH];

    nb_session_header(packet, session, NB_CONTROL_SYSTEM, NB_SESSION_DATA);
    nb_put16(packet + 8, NB_INIT_LENGTH);
    nb_put16(packet + 12, NB_INIT_LENGTH);
    memcpy(packet + NB_SESSION_HEADER, session->init, NB_INIT_LENGTH);
    nb_session_send(ipx, session, packet, sizeof(packet));
}

static void
nb_session_free(ipx_t *ipx, int index)
{
    if (!ipx->sessions[index])
        return;
    smb_session_close(ipx->sessions[index]->smb);
    free(ipx->sessions[index]);
    ipx->sessions[index] = NULL;
}

static void
nb_session_abort(ipx_t *ipx, int index)
{
    uint8_t header[NB_SESSION_HEADER];

    nb_session_header(header, ipx->sessions[index], NB_CONTROL_ACK, NB_SESSION_END);
    nb_session_send(ipx, ipx->sessions[index], header, sizeof(header));
    nb_session_free(ipx, index);
}

/* Length of the message at tx_pos, which sits after its NBT header. */
static int
nb_session_message_length(const nb_session_t *session)
{
    const uint8_t *nbt = session->tx + session->tx_pos;

    return (nbt[1] << 16) | (nbt[2] << 8) | nbt[3];
}

static void
nb_session_transmit(ipx_t *ipx, nb_session_t *session)
{
    uint8_t packet[NB_SESSION_HEADER + IPX_MAX_PACKET];
    int     total  = nb_session_message_length(session);
    int     length = total - session->tx_acked;

    if (length > session->max_data)
        length = session->max_data;
    nb_session_header(packet, session,
                      NB_CONTROL_ACK | (session->tx_acked + length == total ? NB_CONTROL_EOM : 0), NB_SESSION_DATA);
    nb_put16(packet + 8, (uint16_t) total);
    nb_put16(packet + 10, (uint16_t) session->tx_acked);
    nb_put16(packet + 12, (uint16_t) length);
    memcpy(packet + NB_SESSION_HEADER, session->tx + session->tx_pos + NBT_HEADER_LENGTH + session->tx_acked,
           (size_t) length);
    session->in_flight = length;
    session->retry_ms  = NB_RETRY_MS;
    nb_session_send(ipx, session, packet, NB_SESSION_HEADER + length);
}

/* Starts on the next message, skipping the empty ones NBT uses for its own purposes. */
static void
nb_session_pump(ipx_t *ipx, nb_session_t *session)
{
    if (session->in_flight)
        return;
    while (session->tx_pos < session->tx_len) {
        if (session->tx[session->tx_pos] == NBT_MESSAGE && nb_session_message_length(session) > 0) {
            session->retries = 0;
            nb_session_transmit(ipx, session);
            return;
        }
        session->tx_pos += NBT_HEADER_LENGTH + nb_session_message_length(session);
    }
}

static void
nb_session_deliver(ipx_t *ipx, int index, int length)
{
    nb_session_t *session = ipx->sessions[index];
    int           written;

    session->rx[0] = NBT_MESSAGE;
    session->rx[1] = 0;
    session->rx[2] = (uint8_t) (length >> 8);
    session->rx[3] = (uint8_t) length;

    if (session->tx_pos == session->tx_len)
        session->tx_pos = session->tx_len = 0;
    if ((int) sizeof(session->tx) - session->tx_len < SMB_MAX_OUTPUT / 2 && session->tx_pos) {
        memmove(session->tx, session->tx + session->tx_pos, (size_t) (session->tx_len - session->tx_pos));
        session->tx_len -= session->tx_pos;
        session->tx_pos = 0;
    }
    written = smb_session_input(session->smb, session->rx, NBT_HEADER_LENGTH + length, session->tx + session->tx_len,
                                (int) sizeof(session->tx) - session->tx_len);
    if (written < 0) {
        nb_session_abort(ipx, index);
        return;
    }
    session->tx_len += written;
}

static void
nb_session_open(ipx_t *ipx, const ipx_address_t *from, const uint8_t *header, const uint8_t *init, int length)
{
    nb_session_t *session;
    int           free_slot = -1;
    uint32_t      oldest    = 0;
    uint16_t      max_data;

    if (length < NB_INIT_LENGTH || !nb_name_accepts_sessions(init + NB_NAME_LENGTH))
        return;

    for (int i = 0; i < NB_MAX_SESSIONS; i++) {
        session = ipx->sessions[i];
        if (!session) {
            free_slot = i;
            continue;
        }
        if (session->remote == nb_get16(header + 2) && !memcmp(&session->peer, from, IPX_ADDRESS_LENGTH)) {
            nb_session_answer_init(ipx, session); /* the answer must have gone astray */
            return;
        }
    }
    if (free_slot < 0) {
        for (int i = 0; i < NB_MAX_SESSIONS; i++) {
            if (ipx->sessions[i]->idle_ms >= oldest) {
                oldest    = ipx->sessions[i]->idle_ms;
                free_slot = i;
            }
        }
        nb_session_free(ipx, free_slot);
    }

    session = (nb_session_t *) calloc(1, sizeof(nb_session_t));
    if (!session)
        return;
    session->smb = smb_session_create(ipx->log);
    if (!session->smb) {
        free(session);
        return;
    }
    smb_session_attach(session->smb, 0);
    session->peer   = *from;
    session->remote = nb_get16(header + 2);
    session->local  = (uint16_t) ((free_slot + 1) | (++ipx->generation << 8));
    session->rx_seq = 1; /* the request counted as the first message */

    max_data = nb_get16(init + NB_NAME_LENGTH * 2);
    if (max_data > ipx->max_packet - IPX_HEADER_LENGTH - NB_SESSION_HEADER)
        max_data = (uint16_t) (ipx->max_packet - IPX_HEADER_LENGTH - NB_SESSION_HEADER);
    session->max_data = max_data ? max_data : 512;

    memcpy(session->init, init + NB_NAME_LENGTH, NB_NAME_LENGTH);
    memcpy(session->init + NB_NAME_LENGTH, init, NB_NAME_LENGTH);
    nb_put16(session->init + NB_NAME_LENGTH * 2, session->max_data);
    memcpy(session->init + NB_NAME_LENGTH * 2 + 2, init + NB_NAME_LENGTH * 2 + 2, 4);

    ipx->sessions[free_slot] = session;
    nb_session_answer_init(ipx, session);
    ipx_log(ipx->log, "IPX: NetBIOS session %04X open to %04X\n", session->local, session->remote);
}

static void
nb_session_handle_ack(ipx_t *ipx, nb_session_t *session, const uint8_t *header)
{
    uint16_t received = nb_get16(header + 14);
    int      bytes    = nb_get16(header + 16);
    int      total;

    if (header[0] & NB_CONTROL_ACK)
        nb_session_ack(ipx, session, false); /* just asking whether anyone is there */
    if (!session->in_flight)
        return;

    total = nb_session_message_length(session);
    if (received == (uint16_t) (session->tx_seq + 1)) {
        session->tx_pos += NBT_HEADER_LENGTH + total;
        session->tx_seq++;
        session->tx_acked  = 0;
        session->in_flight = 0;
        nb_session_pump(ipx, session);
    } else if (received == session->tx_seq && bytes < total
               && (bytes > session->tx_acked || (header[0] & NB_CONTROL_RESEND))) {
        session->tx_acked  = bytes;
        session->in_flight = 0;
        session->retries   = 0;
        nb_session_transmit(ipx, session);
    }
}

static void
nb_session_handle_data(ipx_t *ipx, int index, const uint8_t *header, const uint8_t *data, int length)
{
    nb_session_t *session = ipx->sessions[index];
    int           total   = nb_get16(header + 8);
    int           offset  = nb_get16(header + 10);
    uint16_t      seq     = nb_get16(header + 6);
    int           count   = nb_get16(header + 12);
    bool          ack     = (header[0] & NB_CONTROL_ACK) != 0;

    if (count > length)
        return;

    if (seq != session->rx_seq) {
        /* The one before means its acknowledgement was lost. Anything else went missing. */
        if (ack)
            nb_session_ack(ipx, session, seq != (uint16_t) (session->rx_seq - 1));
        return;
    }
    if (total > NB_MAX_MESSAGE) {
        nb_session_abort(ipx, index);
        return;
    }
    if (offset == session->rx_length && offset + count <= total) {
        memcpy(session->rx + NBT_HEADER_LENGTH + offset, data, (size_t) count);
        session->rx_length += count;
    } else if (offset > session->rx_length) {
        if (ack)
            nb_session_ack(ipx, session, true);
        return;
    }

    if (session->rx_length < total) {
        if (ack)
            nb_session_ack(ipx, session, false);
        return;
    }

    session->rx_seq++;
    session->rx_length = 0;
    nb_session_ack(ipx, session, false);
    nb_session_deliver(ipx, index, total);
    if (ipx->sessions[index])
        nb_session_pump(ipx, session);
}

static void
nb_handle_session(ipx_t *ipx, const ipx_address_t *from, const uint8_t *data, int length)
{
    nb_session_t *session = NULL;
    uint16_t      dest;
    int           index;

    if (length < NB_SESSION_HEADER)
        return;
    dest = nb_get16(data + 4);
    if (data[1] == NB_SESSION_DATA && dest == NB_NEW_SESSION && nb_get16(data + 6) == 0) {
        nb_session_open(ipx, from, data, data + NB_SESSION_HEADER, length - NB_SESSION_HEADER);
        return;
    }

    index = (dest & 0xFF) - 1;
    if (index >= 0 && index < NB_MAX_SESSIONS)
        session = ipx->sessions[index];
    if (!session || session->local != dest || session->remote != nb_get16(data + 2)
        || memcmp(session->peer.node, from->node, IPX_NODE_LENGTH))
        return;
    session->idle_ms = 0;

    switch (data[1]) {
        case NB_SESSION_DATA:
            if (data[0] & NB_CONTROL_SYSTEM)
                nb_session_handle_ack(ipx, session, data);
            else
                nb_session_handle_data(ipx, index, data, data + NB_SESSION_HEADER, length - NB_SESSION_HEADER);
            break;

        case NB_SESSION_END:
            {
                uint8_t header[NB_SESSION_HEADER];

                nb_session_header(header, session, NB_CONTROL_SYSTEM, NB_SESSION_END_ACK);
                nb_session_send(ipx, session, header, sizeof(header));
                nb_session_free(ipx, index);
                ipx_log(ipx->log, "IPX: NetBIOS session %04X closed\n", dest);
            }
            break;

        default:
            break;
    }
}

/* --- NetBIOS names and datagrams --- */

static void
nb_answer_name(ipx_t *ipx, const ipx_address_t *from, uint8_t type, uint8_t flags, const uint8_t *name)
{
    uint8_t frame[NB_NAME_FRAME];

    memset(frame, 0, sizeof(frame));
    frame[1]                     = type; /* NWNBLINK puts the type in the routes too */
    frame[IPX_ROUTES_LENGTH]     = flags;
    frame[IPX_ROUTES_LENGTH + 1] = type;
    memcpy(frame + IPX_ROUTES_LENGTH + 2, name, NB_NAME_LENGTH);
    ipx_send(ipx, IPX_TYPE_PEP, from, IPX_SOCKET_NETBIOS, frame, sizeof(frame));
}

static void
nb_handle(ipx_t *ipx, const ipx_address_t *from, uint8_t type, const uint8_t *data, int length)
{
    int pos = 0;

    /* Broadcasts carry their routes ahead of them. */
    if (type == IPX_TYPE_NETBIOS
        || (length == NB_NAME_FRAME && data[IPX_ROUTES_LENGTH + 1] >= NB_FIND_NAME
            && data[IPX_ROUTES_LENGTH + 1] < NB_SESSION_DATA))
        pos = IPX_ROUTES_LENGTH;
    if (length < pos + 2)
        return;

    ipx->netbios_seen = true;
    ipx->guest        = *from;

    switch (data[pos + 1]) {
        case NB_FIND_NAME:
            if (length >= pos + 2 + NB_NAME_LENGTH) {
                uint8_t flags = nb_name_flags(data + pos + 2);

                if (flags)
                    nb_answer_name(ipx, from, NB_NAME_RECOGNIZED, flags, data + pos + 2);
            }
            break;

        case NB_CHECK_NAME:
            if (length >= pos + 2 + NB_NAME_LENGTH) {
                uint8_t flags = nb_name_flags(data + pos + 2);

                if (flags && !(flags & NB_FLAG_GROUP) && !(data[pos] & NB_FLAG_GROUP))
                    nb_answer_name(ipx, from, NB_NAME_IN_USE, flags, data + pos + 2);
            }
            break;

        case NB_SESSION_DATA:
        case NB_SESSION_END:
        case NB_SESSION_END_ACK:
            nb_handle_session(ipx, from, data + pos, length - pos);
            break;

        case NB_DATAGRAM:
            if (length > pos + 2 + NB_NAME_LENGTH * 2)
                ipx_handle_datagram(ipx, from, false, data + pos + 2, data + pos + 2 + NB_NAME_LENGTH,
                                    data + pos + 2 + NB_NAME_LENGTH * 2, length - pos - 2 - NB_NAME_LENGTH * 2);
            break;

        default:
            break;
    }
}

/* --- Direct hosting --- */

static void
dh_free(ipx_t *ipx, int index)
{
    if (!ipx->clients[index])
        return;
    smb_session_close(ipx->clients[index]->smb);
    free(ipx->clients[index]);
    ipx->clients[index] = NULL;
}

static dh_client_t *
dh_create(ipx_t *ipx, const ipx_address_t *from)
{
    dh_client_t *client;
    int          slot   = -1;
    uint32_t     oldest = 0;

    for (int i = 0; i < DH_MAX_CLIENTS; i++) {
        if (ipx->clients[i] && !memcmp(&ipx->clients[i]->peer, from, IPX_ADDRESS_LENGTH))
            dh_free(ipx, i); /* it has started over */
        if (!ipx->clients[i])
            slot = i;
    }
    if (slot < 0) {
        for (int i = 0; i < DH_MAX_CLIENTS; i++) {
            if (ipx->clients[i]->idle_ms >= oldest) {
                oldest = ipx->clients[i]->idle_ms;
                slot   = i;
            }
        }
        dh_free(ipx, slot);
    }

    client = (dh_client_t *) calloc(1, sizeof(dh_client_t));
    if (!client)
        return NULL;
    client->smb = smb_session_create(ipx->log);
    if (!client->smb) {
        free(client);
        return NULL;
    }
    smb_session_attach(client->smb, ipx->max_packet - IPX_HEADER_LENGTH);
    client->peer = *from;
    client->cid  = (uint16_t) ((slot + 1) | (++ipx->generation << 8));
    ipx->clients[slot] = client;
    ipx_log(ipx->log, "IPX: Direct hosting client %04X\n", client->cid);
    return client;
}

static void
dh_send_error(ipx_t *ipx, const ipx_address_t *from, const uint8_t *request, uint16_t error)
{
    uint8_t reply[SMB_HEADER_LENGTH + 3];

    memset(reply, 0, sizeof(reply));
    memcpy(reply, request, SMB_HEADER_LENGTH);
    reply[SMB_OFFSET_STATUS]     = SMB_ERRSRV;
    reply[SMB_OFFSET_STATUS + 1] = 0;
    reply[SMB_OFFSET_STATUS + 2] = (uint8_t) error;
    reply[SMB_OFFSET_STATUS + 3] = (uint8_t) (error >> 8);
    reply[SMB_OFFSET_FLAGS] |= SMB_FLAGS_REPLY;
    ipx_send(ipx, IPX_TYPE_PEP, from, IPX_SOCKET_SMB, reply, sizeof(reply));
}

static void
dh_handle_smb(ipx_t *ipx, const ipx_address_t *from, const uint8_t *msg, int length)
{
    dh_client_t *client = NULL;
    uint16_t     cid;
    uint16_t     sequence;
    int          index  = -1;
    int          written;
    int          reply;

    if (length < SMB_HEADER_LENGTH || memcmp(msg, "\xFFSMB", 4) != 0 || length > NB_MAX_MESSAGE)
        return;
    ipx->direct_seen = true;
    ipx->guest       = *from;
    cid              = nb_get16(msg + SMB_OFFSET_CID);
    sequence         = nb_get16(msg + SMB_OFFSET_SEQUENCE);

    if (cid == 0 && msg[SMB_OFFSET_COMMAND] == SMB_COM_NEGOTIATE) {
        client = dh_create(ipx, from);
        if (!client)
            return;
    } else {
        for (int i = 0; i < DH_MAX_CLIENTS; i++) {
            if (ipx->clients[i] && ipx->clients[i]->cid == cid
                && !memcmp(ipx->clients[i]->peer.node, from->node, IPX_NODE_LENGTH)) {
                client = ipx->clients[i];
                break;
            }
        }
        if (!client) {
            dh_send_error(ipx, from, msg, SMB_ERRinvsess);
            return;
        }
    }
    client->idle_ms = 0;
    client->peer    = *from;

    if (sequence) {
        if (sequence == client->sequence) {
            /* The reply went astray, and the request can't be run twice. */
            if (client->reply_length)
                ipx_send(ipx, IPX_TYPE_PEP, from, IPX_SOCKET_SMB, client->reply, client->reply_length);
            return;
        }
        if (client->sequence
            && sequence != (client->sequence == 0xFFFF ? 1 : (uint16_t) (client->sequence + 1)))
            return;
        client->sequence     = sequence;
        client->reply_length = 0;
    }

    ipx->request[0] = NBT_MESSAGE;
    ipx->request[1] = 0;
    ipx->request[2] = (uint8_t) (length >> 8);
    ipx->request[3] = (uint8_t) length;
    memcpy(ipx->request + NBT_HEADER_LENGTH, msg, (size_t) length);
    written = smb_session_input(client->smb, ipx->request, NBT_HEADER_LENGTH + length, ipx->scratch,
                                (int) sizeof(ipx->scratch));
    if (written < 0) {
        for (int i = 0; i < DH_MAX_CLIENTS; i++) {
            if (ipx->clients[i] == client)
                index = i;
        }
        dh_free(ipx, index);
        return;
    }

    if (written <= NBT_HEADER_LENGTH)
        return;
    reply = written - NBT_HEADER_LENGTH;
    if (reply > ipx->max_packet - IPX_HEADER_LENGTH || reply > (int) sizeof(client->reply)) {
        dh_send_error(ipx, from, msg, SMB_ERRerror);
        return;
    }

    memcpy(client->reply, ipx->scratch + NBT_HEADER_LENGTH, (size_t) reply);
    memcpy(client->reply + SMB_OFFSET_FEATURES, msg + SMB_OFFSET_FEATURES, 8);
    nb_put16(client->reply + SMB_OFFSET_CID, client->cid);
    if (sequence)
        client->reply_length = reply;
    ipx_send(ipx, IPX_TYPE_PEP, from, IPX_SOCKET_SMB, client->reply, reply);
}

static void
dh_handle_name(ipx_t *ipx, const ipx_address_t *from, const uint8_t *data, int length)
{
    uint8_t frame[DH_HEADER];
    char    name[NB_NAME_LENGTH];

    if (length < DH_HEADER)
        return;
    ipx->direct_seen = true;
    ipx->guest       = *from;
    if (data[IPX_ROUTES_LENGTH] != DH_QUERY && data[IPX_ROUTES_LENGTH] != DH_MAILSLOT_FIND)
        return;
    if (!dh_name_is(data + IPX_ROUTES_LENGTH + 4, VSERVER_SERVER_NAME)
        && !dh_name_is(data + IPX_ROUTES_LENGTH + 4, VSERVER_WORKGROUP_NAME))
        return;

    memcpy(frame, data, DH_HEADER);
    memset(frame, 0, IPX_ROUTES_LENGTH);
    frame[IPX_ROUTES_LENGTH] = data[IPX_ROUTES_LENGTH] == DH_QUERY ? DH_FOUND : DH_MAILSLOT_NAME;
    nb_make_name(name, VSERVER_SERVER_NAME, 0);
    memcpy(frame + IPX_ROUTES_LENGTH + 4 + NB_NAME_LENGTH, name, NB_NAME_LENGTH);
    ipx_send(ipx, IPX_TYPE_PEP, from, IPX_SOCKET_NAME, frame, sizeof(frame));
}

static void
dh_handle_mailslot(ipx_t *ipx, const ipx_address_t *from, const uint8_t *data, int length)
{
    if (length <= DH_HEADER || data[IPX_ROUTES_LENGTH] != DH_MAILSLOT_SEND)
        return;
    ipx->direct_seen = true;
    ipx->guest       = *from;
    ipx_handle_datagram(ipx, from, true, data + IPX_ROUTES_LENGTH + 4, data + IPX_ROUTES_LENGTH + 4 + NB_NAME_LENGTH,
                        data + DH_HEADER, length - DH_HEADER);
}

/* --- IPX --- */

static void
rip_handle(ipx_t *ipx, const ipx_address_t *from, const uint8_t *data, int length)
{
    uint8_t reply[2 + RIP_ENTRY_LENGTH];

    if (length < 2 + RIP_ENTRY_LENGTH || ipx_get16(data) != RIP_REQUEST)
        return;
    for (int pos = 2; pos + RIP_ENTRY_LENGTH <= length && pos < 2 + RIP_ENTRY_LENGTH * RIP_MAX_ENTRIES;
         pos += RIP_ENTRY_LENGTH) {
        if (!memcmp(data + pos, "\xFF\xFF\xFF\xFF", 4) || !memcmp(data + pos, ipx->network, 4)) {
            ipx_put16(reply, RIP_RESPONSE);
            memcpy(reply + 2, ipx->network, 4);
            ipx_put16(reply + 6, 1); /* hops */
            ipx_put16(reply + 8, 1); /* ticks */
            ipx_send(ipx, IPX_TYPE_RIP, from, IPX_SOCKET_RIP, reply, sizeof(reply));
            return;
        }
    }
}

bool
ipx_input(ipx_t *ipx, const uint8_t *packet, int len)
{
    ipx_address_t from;
    uint16_t      socket;
    uint8_t       type;
    int           length;

    if (!ipx || !packet || len < IPX_HEADER_LENGTH)
        return false;
    length = ipx_get16(packet + 2);
    if (length < IPX_HEADER_LENGTH || length > len || length > IPX_MAX_PACKET)
        return false;
    if (memcmp(packet + 10, ipx->node, IPX_NODE_LENGTH) && memcmp(packet + 10, ipx_broadcast, IPX_NODE_LENGTH))
        return false;

    memcpy(&from, packet + 18, IPX_ADDRESS_LENGTH);
    socket = ipx_get16(packet + 16);
    type   = packet[5];
    packet += IPX_HEADER_LENGTH;
    length -= IPX_HEADER_LENGTH;

    switch (socket) {
        case IPX_SOCKET_RIP:
            rip_handle(ipx, &from, packet, length);
            break;
        case IPX_SOCKET_NETBIOS:
            nb_handle(ipx, &from, type, packet, length);
            break;
        case IPX_SOCKET_SMB:
            dh_handle_smb(ipx, &from, packet, length);
            break;
        case IPX_SOCKET_NAME:
            dh_handle_name(ipx, &from, packet, length);
            break;
        case IPX_SOCKET_MAILSLOT:
            dh_handle_mailslot(ipx, &from, packet, length);
            break;
        default:
            return false;
    }
    return true;
}

static void
ipx_announce(ipx_t *ipx)
{
    ipx_address_t to = ipx->guest;
    uint8_t       packet[1024];

    memcpy(to.node, ipx_broadcast, IPX_NODE_LENGTH);
    for (int which = 0; which < 2; which++) {
        int length = browser_announcement(0, which, packet, sizeof(packet));

        if (length <= 0)
            continue;
        if (ipx->netbios_seen) {
            ipx_put16(to.socket, IPX_SOCKET_NETBIOS);
            ipx_send_datagram(ipx, &to, false, packet, length);
        }
        if (ipx->direct_seen) {
            ipx_put16(to.socket, IPX_SOCKET_MAILSLOT);
            ipx_send_datagram(ipx, &to, true, packet, length);
        }
    }
}

void
ipx_tick(ipx_t *ipx)
{
    if (!ipx)
        return;

    for (int i = 0; i < NB_MAX_SESSIONS; i++) {
        nb_session_t *session = ipx->sessions[i];

        if (!session)
            continue;
        session->idle_ms++;
        if (session->in_flight && --session->retry_ms <= 0) {
            if (++session->retries > NB_MAX_RETRIES) {
                ipx_log(ipx->log, "IPX: Nothing acknowledged; dropping session %04X\n", session->local);
                nb_session_free(ipx, i);
            } else {
                nb_session_transmit(ipx, session);
            }
        }
    }

    for (int i = 0; i < DH_MAX_CLIENTS; i++) {
        if (ipx->clients[i] && ++ipx->clients[i]->idle_ms >= DH_IDLE_MS)
            dh_free(ipx, i);
    }

    if ((ipx->netbios_seen || ipx->direct_seen) && ++ipx->announce_ms >= ANNOUNCE_PERIOD_MS) {
        ipx->announce_ms = 0;
        ipx_announce(ipx);
    }
}

void
ipx_configure(ipx_t *ipx, uint32_t network, const uint8_t *node, int max_packet)
{
    if (!ipx)
        return;
    ipx->network[0] = (uint8_t) (network >> 24);
    ipx->network[1] = (uint8_t) (network >> 16);
    ipx->network[2] = (uint8_t) (network >> 8);
    ipx->network[3] = (uint8_t) network;
    memcpy(ipx->node, node, IPX_NODE_LENGTH);
    if (max_packet >= IPX_MIN_PACKET)
        ipx->max_packet = max_packet < IPX_MAX_PACKET ? max_packet : IPX_MAX_PACKET;
}

void
ipx_reset(ipx_t *ipx)
{
    if (!ipx)
        return;
    for (int i = 0; i < NB_MAX_SESSIONS; i++)
        nb_session_free(ipx, i);
    for (int i = 0; i < DH_MAX_CLIENTS; i++)
        dh_free(ipx, i);
    ipx->netbios_seen = false;
    ipx->direct_seen  = false;
    ipx->announce_ms  = ANNOUNCE_PERIOD_MS - ANNOUNCE_FIRST_MS;
}

ipx_t *
ipx_init(void *log, const uint8_t *node, ipx_output_t output, void *priv)
{
    ipx_t *ipx = (ipx_t *) calloc(1, sizeof(ipx_t));

    if (!ipx)
        return NULL;
    ipx->log    = log;
    ipx->output = output;
    ipx->priv   = priv;
    ipx_configure(ipx, IPX_NETWORK, node, IPX_MAX_PACKET);
    ipx_reset(ipx);
    return ipx;
}

void
ipx_close(ipx_t *ipx)
{
    if (!ipx)
        return;
    ipx_reset(ipx);
    free(ipx);
}
