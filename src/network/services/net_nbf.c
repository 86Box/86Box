/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NetBEUI for the virtual server: the NetBIOS
 *          Frames protocol and the IEEE 802.2 LLC connection beneath it.
 *
 *          Name lookups and datagrams travel in unnumbered frames. Everything
 *          else shares one numbered LLC connection, with several NetBIOS
 *          sessions multiplexed over it. Each session wraps an SMB session,
 *          which sees the same messages it would over TCP.
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
#include <86box/network/net_nbf.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_smb.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_ras.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
nbf_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define nbf_log(priv, fmt, ...)
#endif

#define LLC_SAP          0xF0 /* NetBIOS */
#define LLC_GLOBAL_SAP   0xFF
#define LLC_NULL_SAP     0x00
#define LLC_RESPONSE     0x01 /* in the source SAP, marks a response */
#define LLC_POLL         0x10 /* the poll/final bit of an unnumbered frame */
#define LLC_UI           0x03
#define LLC_SABME        0x6F
#define LLC_UA           0x63
#define LLC_DISC         0x43
#define LLC_DM           0x0F
#define LLC_FRMR         0x87
#define LLC_XID          0xAF
#define LLC_TEST         0xE3
#define LLC_RR           0x01
#define LLC_RNR          0x05
#define LLC_REJ          0x09
#define LLC_HEADER_U     3
#define LLC_HEADER_I     4
#define LLC_MODULUS      128
#define LLC_WINDOW       7
#define LLC_ACK_MS       1500
#define LLC_MAX_RETRIES  10

#define NBF_DELIMITER        0xEFFF
#define NBF_HEADER_SESSION   14
#define NBF_HEADER_NAMES     44
#define NBF_NAME_LENGTH      16
#define NBF_ADD_GROUP_NAME   0x00
#define NBF_ADD_NAME         0x01
#define NBF_DATAGRAM         0x08
#define NBF_DATAGRAM_BCAST   0x09
#define NBF_NAME_QUERY       0x0A
#define NBF_ADD_NAME_REPLY   0x0D
#define NBF_NAME_RECOGNIZED  0x0E
#define NBF_DATA_ACK         0x14
#define NBF_DATA_FIRST       0x15
#define NBF_DATA_LAST        0x16
#define NBF_SESSION_CONFIRM  0x17
#define NBF_SESSION_END      0x18
#define NBF_SESSION_INIT     0x19
#define NBF_NO_RECEIVE       0x1A
#define NBF_RECEIVE_OUTSTANDING 0x1B
#define NBF_RECEIVE_CONTINUE 0x1C
#define NBF_SESSION_ALIVE    0x1F

#define NBF_FLAG_RECEIVE_CONTINUE 0x01
#define NBF_VERSION_2             0x01 /* in SESSION_INITIALIZE and SESSION_CONFIRM */
#define NBF_LISTEN_NONE           0x00

#define NBF_MAX_SESSIONS   8
#define NBF_MAX_MESSAGE    8192
#define NBF_MAX_DATA       (NBF_MAX_FRAME - LLC_HEADER_I - NBF_HEADER_SESSION)
#define NBF_DEFAULT_DATA   512
#define NBF_CONTROL_SLOTS  (NBF_MAX_SESSIONS * 2)
#define NBF_SUFFIX_SERVER  0x20
#define NBF_SUFFIX_CLIENT  0x00
#define NBT_NAME_ENCODED   34
#define NBT_HEADER_LENGTH  4
#define NBT_SESSION_REQUEST 0x81
#define NBT_MESSAGE        0x00
#define NBF_RAS_GATEWAY_NAME "DIALIN_GATEWAY"

#define DGM_HEADER_LENGTH  14
#define DGM_DIRECT_GROUP   0x11
#define DGM_NAME_LENGTH    34
#define DGM_MAX            1400

#define NBF_ANNOUNCE_FIRST_MS  2000
#define NBF_ANNOUNCE_PERIOD_MS 60000

typedef enum {
    SESSION_FREE,
    SESSION_NAMED,     /* a name query was answered and SESSION_INITIALIZE should follow */
    SESSION_OPEN
} session_state_t;

typedef enum {
    SESSION_SERVICE_SMB,
    SESSION_SERVICE_RAS
} session_service_t;

typedef struct nbf_session_t {
    session_state_t state;
    session_service_t service;
    uint8_t         local;      /* our session number, which is this slot's index plus one */
    uint8_t         remote;
    uint16_t        max_send;
    uint16_t        corr_ours;  /* the correlator the next frame of ours asks about */
    smb_session_t  *smb;
    modem_ras_session_t ras;
    char            calling[NBF_NAME_LENGTH];

    int             rx_len;
    int             tx_len;     /* NetBIOS session frames waiting to be sent */
    int             tx_pos;
    int             tx_message; /* bytes of the current message still to send, once started */
    int             tx_start;   /* where the current message's data begins */
    int             tx_total;
    bool            tx_started;
    bool            wait_ack;   /* the message went out and DATA_ACK hasn't come back */
    bool            wait_continue;
    bool            ending;
    uint8_t         rx[NBF_MAX_MESSAGE];
    uint8_t         tx[SMB_MAX_OUTPUT];
} nbf_session_t;

typedef struct nbf_unacked_t {
    int     length;
    uint8_t frame[NBF_MAX_FRAME];
} nbf_unacked_t;

struct nbf_t {
    void         *log;
    nbf_output_t  output;
    void         *priv;

    bool          linked;      /* the LLC connection is up */
    bool          remote_busy;
    bool          reject_sent;
    uint8_t       vs;          /* next to send */
    uint8_t       vr;          /* next to receive */
    uint8_t       va;          /* oldest not yet acknowledged */
    int           ack_timer;
    int           retries;
    nbf_unacked_t unacked[LLC_WINDOW + 1];

    /* Frames with no data, which go ahead of a session's data. */
    uint8_t       control[NBF_CONTROL_SLOTS][NBF_HEADER_SESSION];
    int           control_head;
    int           control_count;

    int           max_data;    /* how much a data frame can carry on this link */
    char          ras_username[MODEM_RAS_CREDENTIAL_LENGTH];
    char          ras_password[MODEM_RAS_CREDENTIAL_LENGTH];
    bool          guest_seen;
    int           announce_ms;
    int           next_session;
    uint16_t      correlator;
    nbf_session_t sessions[NBF_MAX_SESSIONS];
};

static void
nbf_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) value;
    p[1] = (uint8_t) (value >> 8);
}

static uint16_t
nbf_get16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static uint8_t
llc_next(uint8_t sequence)
{
    return (uint8_t) ((sequence + 1) % LLC_MODULUS);
}

static uint16_t
nbf_new_correlator(nbf_t *nbf)
{
    if (++nbf->correlator == 0)
        nbf->correlator = 1;
    return nbf->correlator;
}

static void
nbf_make_name(char *name, const char *text, uint8_t suffix)
{
    size_t length = strlen(text);

    memset(name, ' ', NBF_NAME_LENGTH);
    memcpy(name, text, length < 15 ? length : 15);
    name[15] = (char) suffix;
}

static void
nbf_encode_name(uint8_t *out, const char *name)
{
    out[0] = 32;
    for (int i = 0; i < NBF_NAME_LENGTH; i++) {
        out[1 + i * 2] = (uint8_t) ('A' + (((uint8_t) name[i]) >> 4));
        out[2 + i * 2] = (uint8_t) ('A' + (((uint8_t) name[i]) & 0x0F));
    }
    out[33] = 0;
}

static void
nbf_decode_name(const uint8_t *in, char *name)
{
    for (int i = 0; i < NBF_NAME_LENGTH; i++)
        name[i] = (char) (((in[1 + i * 2] - 'A') << 4) | ((in[2 + i * 2] - 'A') & 0x0F));
}

/* --- LLC --- */

static void
llc_send_unnumbered(nbf_t *nbf, uint8_t control, bool response, const uint8_t *data, int length)
{
    uint8_t frame[NBF_MAX_FRAME];

    if (LLC_HEADER_U + length > NBF_MAX_FRAME)
        return;
    frame[0] = LLC_SAP;
    frame[1] = (uint8_t) (LLC_SAP | (response ? LLC_RESPONSE : 0));
    frame[2] = control;
    if (length)
        memcpy(frame + LLC_HEADER_U, data, (size_t) length);
    nbf->output(nbf->priv, frame, LLC_HEADER_U + length);
}

static void
llc_send_supervisory(nbf_t *nbf, uint8_t type, bool response, bool poll_final)
{
    uint8_t frame[LLC_HEADER_I];

    frame[0] = LLC_SAP;
    frame[1] = (uint8_t) (LLC_SAP | (response ? LLC_RESPONSE : 0));
    frame[2] = type;
    frame[3] = (uint8_t) ((nbf->vr << 1) | (poll_final ? 1 : 0));
    nbf->output(nbf->priv, frame, LLC_HEADER_I);
}

static int
llc_outstanding(const nbf_t *nbf)
{
    return (nbf->vs - nbf->va + LLC_MODULUS) % LLC_MODULUS;
}

static bool
llc_can_send(const nbf_t *nbf)
{
    return nbf->linked && !nbf->remote_busy && llc_outstanding(nbf) < LLC_WINDOW;
}

/* Sends a frame from the unacknowledged list, whose sequence number is its index. */
static void
llc_transmit(nbf_t *nbf, uint8_t sequence)
{
    nbf_unacked_t *slot = &nbf->unacked[sequence % (LLC_WINDOW + 1)];

    slot->frame[2] = (uint8_t) (sequence << 1);
    slot->frame[3] = (uint8_t) (nbf->vr << 1);
    nbf->output(nbf->priv, slot->frame, slot->length);
}

/* Queues the next numbered frame. The caller has checked there is room in the window. */
static void
llc_send_info(nbf_t *nbf, const uint8_t *header, int header_length, const uint8_t *data, int length)
{
    nbf_unacked_t *slot = &nbf->unacked[nbf->vs % (LLC_WINDOW + 1)];

    slot->frame[0] = LLC_SAP;
    slot->frame[1] = LLC_SAP;
    memcpy(slot->frame + LLC_HEADER_I, header, (size_t) header_length);
    if (length)
        memcpy(slot->frame + LLC_HEADER_I + header_length, data, (size_t) length);
    slot->length = LLC_HEADER_I + header_length + length;
    llc_transmit(nbf, nbf->vs);
    nbf->vs = llc_next(nbf->vs);
    if (!nbf->ack_timer)
        nbf->ack_timer = LLC_ACK_MS;
}

static void
nbf_drop_sessions(nbf_t *nbf, bool preserve_named)
{
    for (int i = 0; i < NBF_MAX_SESSIONS; i++) {
        nbf_session_t *session = &nbf->sessions[i];

        if (session->state != SESSION_FREE
            && !(preserve_named && session->state == SESSION_NAMED)) {
            if (session->smb)
                smb_session_close(session->smb);
            session->smb   = NULL;
            session->state = SESSION_FREE;
            session->service = SESSION_SERVICE_SMB;
            modem_ras_session_init(&session->ras);
        }
    }
}

static void
llc_reset(nbf_t *nbf, bool preserve_named)
{
    nbf->linked        = false;
    nbf->remote_busy   = false;
    nbf->reject_sent   = false;
    nbf->vs            = 0;
    nbf->vr            = 0;
    nbf->va            = 0;
    nbf->ack_timer     = 0;
    nbf->retries       = 0;
    nbf->control_head  = 0;
    nbf->control_count = 0;
    nbf_drop_sessions(nbf, preserve_named);
}

/* Accepts an acknowledgement. Returns false if it covers something never sent. */
static bool
llc_acknowledge(nbf_t *nbf, uint8_t received)
{
    int acknowledged = (received - nbf->va + LLC_MODULUS) % LLC_MODULUS;

    if (acknowledged > llc_outstanding(nbf))
        return false;
    if (acknowledged) {
        nbf->va        = received;
        nbf->retries   = 0;
        nbf->ack_timer = llc_outstanding(nbf) ? LLC_ACK_MS : 0;
    }
    return true;
}

static void
llc_retransmit(nbf_t *nbf)
{
    for (uint8_t sequence = nbf->va; sequence != nbf->vs; sequence = llc_next(sequence))
        llc_transmit(nbf, sequence);
    nbf->ack_timer = LLC_ACK_MS;
}

/* --- NetBIOS frames --- */

static void
nbf_session_header(uint8_t *header, uint8_t command, uint8_t data1, uint16_t data2, uint16_t transmit,
                   uint16_t response, uint8_t destination, uint8_t source)
{
    nbf_put16(header, NBF_HEADER_SESSION);
    nbf_put16(header + 2, NBF_DELIMITER);
    header[4] = command;
    header[5] = data1;
    nbf_put16(header + 6, data2);
    nbf_put16(header + 8, transmit);
    nbf_put16(header + 10, response);
    header[12] = destination;
    header[13] = source;
}

static void
nbf_queue_control(nbf_t *nbf, uint8_t command, uint8_t data1, uint16_t data2, uint16_t transmit,
                  uint8_t destination, uint8_t source)
{
    int slot;

    if (nbf->control_count == NBF_CONTROL_SLOTS)
        return;
    slot = (nbf->control_head + nbf->control_count) % NBF_CONTROL_SLOTS;
    nbf_session_header(nbf->control[slot], command, data1, data2, transmit, 0, destination, source);
    nbf->control_count++;
}

static void
nbf_session_close(nbf_session_t *session)
{
    if (session->smb)
        smb_session_close(session->smb);
    session->smb   = NULL;
    session->state = SESSION_FREE;
    session->service = SESSION_SERVICE_SMB;
    modem_ras_session_init(&session->ras);
}

static int
nbf_data_limit(const nbf_t *nbf, const nbf_session_t *session)
{
    return session->max_send < nbf->max_data ? session->max_send : nbf->max_data;
}

/* Sends what fits: first frames with no data, then whatever sessions have waiting. */
static void
nbf_pump(nbf_t *nbf)
{
    while (nbf->control_count && llc_can_send(nbf)) {
        llc_send_info(nbf, nbf->control[nbf->control_head], NBF_HEADER_SESSION, NULL, 0);
        nbf->control_head = (nbf->control_head + 1) % NBF_CONTROL_SLOTS;
        nbf->control_count--;
    }

    for (int turn = 0; turn < NBF_MAX_SESSIONS; turn++) {
        nbf_session_t *session = &nbf->sessions[(nbf->next_session + turn) % NBF_MAX_SESSIONS];

        while (session->state == SESSION_OPEN && !session->wait_ack && !session->wait_continue
               && session->tx_pos < session->tx_len && llc_can_send(nbf)) {
            uint8_t  header[NBF_HEADER_SESSION];
            int      chunk;
            bool     last;
            uint8_t *frame;

            if (!session->tx_started) {
                frame = session->tx + session->tx_pos;
                session->tx_message = ((frame[1] & 1) << 16) | (frame[2] << 8) | frame[3];
                session->tx_pos += NBT_HEADER_LENGTH;
                session->tx_started = true;
                if (frame[0] != NBT_MESSAGE) {
                    session->tx_pos += session->tx_message;
                    session->tx_started = false;
                    continue;
                }
                session->corr_ours = nbf_new_correlator(nbf);
                session->tx_start  = session->tx_pos;
                session->tx_total  = session->tx_message;
            }

            chunk = session->tx_message > nbf_data_limit(nbf, session) ? nbf_data_limit(nbf, session)
                                                                       : session->tx_message;
            last  = chunk == session->tx_message;
            nbf_session_header(header, last ? NBF_DATA_LAST : NBF_DATA_FIRST, 0, 0, 0, session->corr_ours,
                               session->remote, session->local);
            llc_send_info(nbf, header, NBF_HEADER_SESSION, session->tx + session->tx_pos, chunk);
            session->tx_pos += chunk;
            session->tx_message -= chunk;
            if (last) {
                session->tx_started = false;
                session->wait_ack   = true;
            }
        }
    }
    nbf->next_session = (nbf->next_session + 1) % NBF_MAX_SESSIONS;
}

/* Hands a complete message to the SMB session and keeps whatever comes back. */
static void
nbf_session_deliver(nbf_t *nbf, nbf_session_t *session, const uint8_t *data, int length, uint8_t type)
{
    uint8_t packet[NBT_HEADER_LENGTH + NBF_MAX_MESSAGE];
    int     written;

    packet[0] = type;
    packet[1] = (uint8_t) (length >> 16);
    packet[2] = (uint8_t) (length >> 8);
    packet[3] = (uint8_t) length;
    memcpy(packet + NBT_HEADER_LENGTH, data, (size_t) length);

    if (session->tx_pos == session->tx_len && !session->wait_ack)
        session->tx_len = session->tx_pos = 0;
    written = (int) sizeof(session->tx) - session->tx_len;
    written = written < SMB_MAX_OUTPUT / 2 ? -1
                                           : smb_session_input(session->smb, packet, NBT_HEADER_LENGTH + length,
                                                               session->tx + session->tx_len, written);
    if (written < 0) {
        nbf_queue_control(nbf, NBF_SESSION_END, 0, 1, 0, session->remote, session->local);
        session->ending = true;
        return;
    }
    session->tx_len += written;
}

static bool
nbf_session_queue_message(nbf_session_t *session, const uint8_t *data, size_t length)
{
    size_t offset;

    if (!session || !data || length > NBF_MAX_MESSAGE)
        return false;
    if (session->tx_pos == session->tx_len && !session->wait_ack)
        session->tx_len = session->tx_pos = 0;
    if ((size_t) session->tx_len + NBT_HEADER_LENGTH + length > sizeof(session->tx))
        return false;

    offset = (size_t) session->tx_len;
    session->tx[offset] = NBT_MESSAGE;
    session->tx[offset + 1] = (uint8_t) (length >> 16);
    session->tx[offset + 2] = (uint8_t) (length >> 8);
    session->tx[offset + 3] = (uint8_t) length;
    memcpy(session->tx + offset + NBT_HEADER_LENGTH, data, length);
    session->tx_len += (int) (NBT_HEADER_LENGTH + length);
    return true;
}

typedef struct nbf_ras_send_context_t {
    nbf_session_t *session;
} nbf_ras_send_context_t;

static bool
nbf_ras_send_message(void *priv, const uint8_t *message, size_t length)
{
    nbf_ras_send_context_t *context = (nbf_ras_send_context_t *) priv;

    return nbf_session_queue_message(context->session, message, length);
}

static nbf_session_t *
nbf_find_session(nbf_t *nbf, uint8_t local)
{
    if (local < 1 || local > NBF_MAX_SESSIONS || nbf->sessions[local - 1].state == SESSION_FREE)
        return NULL;
    return &nbf->sessions[local - 1];
}

/* A name counts as ours when the server, or a group it speaks for, answers to it. */
static bool
nbf_name_is_ras_gateway(const char *name)
{
    char candidate[NBF_NAME_LENGTH];

    nbf_make_name(candidate, NBF_RAS_GATEWAY_NAME, NBF_SUFFIX_SERVER);
    return !memcmp(candidate, name, NBF_NAME_LENGTH);
}

static bool
nbf_name_is_ours(const nbf_t *nbf, const char *name)
{
    char candidate[NBF_NAME_LENGTH];

    if (nbf_name_is_ras_gateway(name))
        return true;

    for (int suffix = 0; suffix < 3; suffix++) {
        static const uint8_t server[3] = { NBF_SUFFIX_SERVER, NBF_SUFFIX_CLIENT, 0x03 };

        nbf_make_name(candidate, VSERVER_SERVER_NAME, server[suffix]);
        if (!memcmp(candidate, name, NBF_NAME_LENGTH))
            return true;
    }
    for (int suffix = 0; suffix < 3; suffix++) {
        static const uint8_t domain[3] = { 0x1B, 0x1C, 0x1D };

        nbf_make_name(candidate, VSERVER_WORKGROUP_NAME, domain[suffix]);
        if (!memcmp(candidate, name, NBF_NAME_LENGTH))
            return true;
    }

    for (int i = 0; i < NBF_MAX_SESSIONS; i++) {
        const nbf_session_t *session = &nbf->sessions[i];

        if (session->state == SESSION_FREE || session->service != SESSION_SERVICE_RAS)
            continue;

        for (uint16_t j = 0; j < session->ras.projected_name_count; j++) {
            if (!memcmp(session->ras.projected_names[j].name, name, NBF_NAME_LENGTH))
                return true;
        }
    }

    return false;
}

static bool
nbf_name_accepts_sessions(const char *name)
{
    char candidate[NBF_NAME_LENGTH];

    nbf_make_name(candidate, VSERVER_SERVER_NAME, NBF_SUFFIX_SERVER);
    return !memcmp(candidate, name, NBF_NAME_LENGTH) || nbf_name_is_ras_gateway(name);
}

static nbf_session_t *
nbf_claim_session(nbf_t *nbf, uint8_t remote)
{
    nbf_session_t *free_slot = NULL;

    for (int i = 0; i < NBF_MAX_SESSIONS; i++) {
        nbf_session_t *session = &nbf->sessions[i];

        /* A repeated query shouldn't use up another session. */
        if (session->state == SESSION_NAMED && session->remote == remote)
            return session;
        if (session->state == SESSION_FREE && !free_slot)
            free_slot = session;
    }
    return free_slot;
}

static void
nbf_handle_name_query(nbf_t *nbf, const uint8_t *header)
{
    const char *name    = (const char *) header + 12;
    const char *caller  = (const char *) header + 28;
    uint8_t     remote  = (uint8_t) header[6];
    uint8_t     reply[NBF_HEADER_NAMES];
    uint8_t     local   = NBF_LISTEN_NONE;

    if (!nbf_name_is_ours(nbf, name))
        return;

    if (remote && nbf_name_accepts_sessions(name)) {
        nbf_session_t *session = nbf_claim_session(nbf, remote);

        if (!session) {
            local = 0xFF; /* listening, but out of resources */
        } else {
            session->state  = SESSION_NAMED;
            session->service = nbf_name_is_ras_gateway(name)
                                   ? SESSION_SERVICE_RAS : SESSION_SERVICE_SMB;
            session->remote = remote;
            session->local  = (uint8_t) (session - nbf->sessions + 1);
            memcpy(session->calling, caller, NBF_NAME_LENGTH);
            local = session->local;
        }
    }

    memset(reply, 0, sizeof(reply));
    nbf_put16(reply, NBF_HEADER_NAMES);
    nbf_put16(reply + 2, NBF_DELIMITER);
    reply[4] = NBF_NAME_RECOGNIZED;
    nbf_put16(reply + 6, local);
    nbf_put16(reply + 8, nbf_get16(header + 10));
    nbf_put16(reply + 10, nbf_new_correlator(nbf));
    memcpy(reply + 12, caller, NBF_NAME_LENGTH);
    memcpy(reply + 28, name, NBF_NAME_LENGTH);
    llc_send_unnumbered(nbf, LLC_UI, false, reply, NBF_HEADER_NAMES);
    nbf_log(nbf->log, "NBF: Name query for %.15s<%02X> answered with session %d\n", name,
            (uint8_t) name[15], local);
}

static void
nbf_handle_add_name(nbf_t *nbf, const uint8_t *header)
{
    const char *name = (const char *) header + 28;
    uint8_t     reply[NBF_HEADER_NAMES];

    if (!nbf_name_is_ours(nbf, name))
        return;
    memset(reply, 0, sizeof(reply));
    nbf_put16(reply, NBF_HEADER_NAMES);
    nbf_put16(reply + 2, NBF_DELIMITER);
    reply[4] = NBF_ADD_NAME_REPLY;
    nbf_put16(reply + 8, nbf_get16(header + 10));
    memcpy(reply + 12, name, NBF_NAME_LENGTH);
    memcpy(reply + 28, name, NBF_NAME_LENGTH);
    llc_send_unnumbered(nbf, LLC_UI, false, reply, NBF_HEADER_NAMES);
}

/* Sends a NetBIOS datagram made by the browser, peeling off what UDP would have needed. */
static void
nbf_send_datagram(nbf_t *nbf, const uint8_t *packet, int length)
{
    const int names = DGM_HEADER_LENGTH + DGM_NAME_LENGTH * 2;
    uint8_t   frame[NBF_HEADER_NAMES + DGM_MAX];

    if (length <= names || length - names > DGM_MAX)
        return;
    memset(frame, 0, NBF_HEADER_NAMES);
    nbf_put16(frame, NBF_HEADER_NAMES);
    nbf_put16(frame + 2, NBF_DELIMITER);
    frame[4] = NBF_DATAGRAM;
    nbf_decode_name(packet + DGM_HEADER_LENGTH + DGM_NAME_LENGTH, (char *) frame + 12);
    nbf_decode_name(packet + DGM_HEADER_LENGTH, (char *) frame + 28);
    memcpy(frame + NBF_HEADER_NAMES, packet + names, (size_t) (length - names));
    llc_send_unnumbered(nbf, LLC_UI, false, frame, NBF_HEADER_NAMES + length - names);
}

/* Dresses a datagram as the browser expects to see one arriving over UDP. */
static void
nbf_handle_datagram(nbf_t *nbf, const uint8_t *header, const uint8_t *data, int length)
{
    const int names = DGM_HEADER_LENGTH + DGM_NAME_LENGTH * 2;
    uint8_t   packet[DGM_HEADER_LENGTH + DGM_NAME_LENGTH * 2 + DGM_MAX];
    uint8_t   reply[1024];
    int       reply_length;

    nbf->guest_seen = true;
    if (length < 1 || length > DGM_MAX)
        return;
    memset(packet, 0, DGM_HEADER_LENGTH);
    packet[0] = DGM_DIRECT_GROUP;
    nbf_encode_name(packet + DGM_HEADER_LENGTH, (const char *) header + 28);
    nbf_encode_name(packet + DGM_HEADER_LENGTH + DGM_NAME_LENGTH, (const char *) header + 12);
    memcpy(packet + names, data, (size_t) length);

    reply_length = browser_process(0, packet, names + length, reply, sizeof(reply));
    if (reply_length > 0)
        nbf_send_datagram(nbf, reply, reply_length);
}

static void
nbf_handle_unnumbered(nbf_t *nbf, const uint8_t *data, int length)
{
    if (length < NBF_HEADER_NAMES || nbf_get16(data) != NBF_HEADER_NAMES
        || nbf_get16(data + 2) != NBF_DELIMITER)
        return;

    switch (data[4]) {
        case NBF_NAME_QUERY:
            nbf_handle_name_query(nbf, data);
            break;
        case NBF_ADD_NAME:
        case NBF_ADD_GROUP_NAME:
            nbf_handle_add_name(nbf, data);
            break;
        case NBF_DATAGRAM:
        case NBF_DATAGRAM_BCAST:
            nbf_handle_datagram(nbf, data, data + NBF_HEADER_NAMES, length - NBF_HEADER_NAMES);
            break;
        default:
            nbf_log(nbf->log, "NBF: Ignoring unnumbered command 0x%02X\n", data[4]);
            break;
    }
}

static void
nbf_handle_session_init(nbf_t *nbf, nbf_session_t *session, const uint8_t *header)
{
    uint8_t request[NBT_NAME_ENCODED * 2];
    char    called[NBF_NAME_LENGTH];
    int     max_receive = nbf_get16(header + 6);

    session->remote   = header[13];
    session->max_send = (uint16_t) (max_receive ? max_receive : NBF_DEFAULT_DATA);
    if (session->max_send > NBF_MAX_DATA)
        session->max_send = NBF_MAX_DATA;
    session->rx_len = session->tx_len = session->tx_pos = 0;
    session->tx_message = 0;
    session->tx_started = session->wait_ack = session->wait_continue = session->ending = false;

    if (session->state == SESSION_NAMED) {
        if (session->service == SESSION_SERVICE_RAS) {
            modem_ras_session_init(&session->ras);
            session->state = SESSION_OPEN;
            nbf_log(nbf->log, "RAS: DIALIN_GATEWAY session %d opened to remote session %d\n",
                    session->local, session->remote);
        } else {
            session->smb = smb_session_create(nbf->log);
            if (!session->smb) {
                session->state = SESSION_FREE;
                nbf_queue_control(nbf, NBF_SESSION_END, 0, 1, 0, header[13], header[12]);
                return;
            }
            /* The session layer the SMB code expects, as though over TCP. */
            nbf_make_name(called, VSERVER_SERVER_NAME, NBF_SUFFIX_SERVER);
            nbf_encode_name(request, called);
            nbf_encode_name(request + NBT_NAME_ENCODED, session->calling);
            session->state = SESSION_OPEN;
            nbf_session_deliver(nbf, session, request, sizeof(request), NBT_SESSION_REQUEST);
            session->tx_len = session->tx_pos = 0;
        }
    }
    nbf_queue_control(nbf, NBF_SESSION_CONFIRM, NBF_VERSION_2, NBF_MAX_DATA, nbf_get16(header + 10),
                      session->remote, session->local);
    nbf_log(nbf->log, "NBF: Session %d open to remote session %d\n", session->local, session->remote);
}

static void
nbf_ras_session_deliver(nbf_t *nbf, nbf_session_t *session, const uint8_t *data, int length)
{
    nbf_ras_send_context_t context = { session };
    modem_ras_session_result_t result;
    nbf_log(nbf->log, "RAS: DIALIN_GATEWAY received message type %u, %d bytes\n",
            (unsigned) (length > 0 ? data[0] : 0), length);
    result = modem_ras_session_process_message(&session->ras, data, (size_t) length,
                                               nbf->ras_username, nbf->ras_password,
                                               nbf_ras_send_message, &context, ppp_random_bytes);
    switch (result) {
        case MODEM_RAS_SESSION_PROTOCOL_NEGOTIATED:
            nbf_log(nbf->log, "RAS: Client version %u negotiated; challenge sent\n",
                    (unsigned) session->ras.client_version);
            break;
        case MODEM_RAS_SESSION_AUTHENTICATED_RESULT:
            nbf_log(nbf->log, "RAS: Configured credentials accepted; authentication succeeded\n");
            break;
        case MODEM_RAS_SESSION_AUTH_REJECTED:
            nbf_log(nbf->log, "RAS: Configured credentials did not match; authentication rejected\n");
            break;
        case MODEM_RAS_SESSION_CONFIGURATION_ACCEPTED:
            nbf_log(nbf->log,
                    "RAS: NetBIOS-only configuration accepted; %u names registered; "
                    "type 108 and type 113 sent\n",
                    (unsigned) session->ras.projected_name_count);
            break;
        case MODEM_RAS_SESSION_CONFIGURATION_REJECTED:
            nbf_log(nbf->log,
                    "RAS: Unsupported configuration projection; failure result sent\n");
            break;
        case MODEM_RAS_SESSION_POST_CONFIGURATION_UNIMPLEMENTED:
            nbf_log(nbf->log, "RAS: Configuration accepted; further RAS messages are not implemented\n");
            nbf_queue_control(nbf, NBF_SESSION_END, 0, 1, 0, session->remote, session->local);
            session->ending = true;
            break;
        case MODEM_RAS_SESSION_INVALID_MESSAGE:
            nbf_log(nbf->log, "RAS: Unexpected or malformed DIALIN_GATEWAY message; ending session\n");
            nbf_queue_control(nbf, NBF_SESSION_END, 0, 1, 0, session->remote, session->local);
            session->ending = true;
            break;
        case MODEM_RAS_SESSION_RANDOM_FAILURE:
            nbf_log(nbf->log, "RAS: Secure challenge generation failed; ending session\n");
            nbf_queue_control(nbf, NBF_SESSION_END, 0, 1, 0, session->remote, session->local);
            session->ending = true;
            break;
        case MODEM_RAS_SESSION_SEND_FAILURE:
            nbf_log(nbf->log, "RAS: Could not queue DIALIN_GATEWAY response; ending session\n");
            nbf_queue_control(nbf, NBF_SESSION_END, 0, 1, 0, session->remote, session->local);
            session->ending = true;
            break;
    }
}

static void
nbf_handle_session_frame(nbf_t *nbf, const uint8_t *header, const uint8_t *data, int length)
{
    uint8_t        command = header[4];
    nbf_session_t *session = nbf_find_session(nbf, header[12]);

    if (command == NBF_SESSION_ALIVE)
        return;

    if (!session) {
        if (command != NBF_SESSION_END) {
            nbf_log(nbf->log,
                    "NBF: Rejecting session command 0x%02X for unknown local session %u\n",
                    (unsigned) command, (unsigned) header[12]);
            nbf_queue_control(nbf, NBF_SESSION_END, 0, 1, 0, header[13], header[12]);
        }
        return;
    }

    switch (command) {
        case NBF_SESSION_INIT:
            nbf_handle_session_init(nbf, session, header);
            break;

        case NBF_DATA_FIRST:
        case NBF_DATA_LAST:
            if (session->state != SESSION_OPEN)
                break;
            if (session->rx_len + length > NBF_MAX_MESSAGE) {
                nbf_queue_control(nbf, NBF_SESSION_END, 0, 1, 0, session->remote, session->local);
                session->ending = true;
                session->rx_len = 0;
                break;
            }
            memcpy(session->rx + session->rx_len, data, (size_t) length);
            session->rx_len += length;
            if (command == NBF_DATA_FIRST) {
                if (header[5] & NBF_FLAG_RECEIVE_CONTINUE)
                    nbf_queue_control(nbf, NBF_RECEIVE_CONTINUE, 0, 0, nbf_get16(header + 10),
                                      session->remote, session->local);
            } else {
                nbf_queue_control(nbf, NBF_DATA_ACK, 0, 0, nbf_get16(header + 10), session->remote,
                                  session->local);
                if (session->service == SESSION_SERVICE_RAS)
                    nbf_ras_session_deliver(nbf, session, session->rx, session->rx_len);
                else
                    nbf_session_deliver(nbf, session, session->rx, session->rx_len, NBT_MESSAGE);
                session->rx_len = 0;
            }
            break;

        case NBF_DATA_ACK:
            session->wait_ack = false;
            break;

        case NBF_RECEIVE_CONTINUE:
            session->wait_continue = false;
            break;

        case NBF_RECEIVE_OUTSTANDING:
            /* The guest wasn't ready, and says how much of the message it did take. */
            session->wait_continue = false;
            if (session->wait_ack || session->tx_started) {
                int accepted = nbf_get16(header + 6);

                if (accepted > session->tx_total)
                    accepted = session->tx_total;
                session->tx_pos     = session->tx_start + accepted;
                session->tx_message = session->tx_total - accepted;
                session->tx_started = true;
                session->wait_ack   = false;
            }
            break;

        case NBF_NO_RECEIVE:
            session->wait_continue = true;
            break;

        case NBF_SESSION_END:
            nbf_log(nbf->log, "NBF: Session %d ended\n", session->local);
            nbf_session_close(session);
            break;

        default:
            nbf_log(nbf->log, "NBF: Ignoring session command 0x%02X\n", command);
            break;
    }
}

static void
nbf_handle_info(nbf_t *nbf, const uint8_t *data, int length)
{
    if (length < NBF_HEADER_SESSION || nbf_get16(data + 2) != NBF_DELIMITER)
        return;
    if (nbf_get16(data) == NBF_HEADER_SESSION)
        nbf_handle_session_frame(nbf, data, data + NBF_HEADER_SESSION, length - NBF_HEADER_SESSION);
}

/* Takes a session out of use once its SESSION_END has gone. */
static void
nbf_reap_sessions(nbf_t *nbf)
{
    for (int i = 0; i < NBF_MAX_SESSIONS; i++) {
        nbf_session_t *session = &nbf->sessions[i];

        if (session->state == SESSION_OPEN && session->ending)
            nbf_session_close(session);
    }
}

void
nbf_input(nbf_t *nbf, const uint8_t *frame, int len)
{
    uint8_t control;
    bool    response;

    if (!nbf || !frame || len < LLC_HEADER_U
        || (frame[0] != LLC_SAP && frame[0] != LLC_GLOBAL_SAP && frame[0] != LLC_NULL_SAP))
        return;
    response = (frame[1] & LLC_RESPONSE) != 0;
    control  = frame[2];

    if (!(control & 1)) {
        uint8_t sequence;
        uint8_t acknowledged;
        uint8_t sent_before;
        bool    poll;

        if (len < LLC_HEADER_I)
            return;
        if (!nbf->linked) {
            llc_send_unnumbered(nbf, LLC_DM | LLC_POLL, true, NULL, 0);
            return;
        }
        sequence     = control >> 1;
        acknowledged = frame[3] >> 1;
        poll         = frame[3] & 1;
        llc_acknowledge(nbf, acknowledged);

        if (sequence != nbf->vr) {
            /* One rejection covers the whole gap, and poll bits still need an answer. */
            if (!nbf->reject_sent) {
                nbf->reject_sent = true;
                llc_send_supervisory(nbf, LLC_REJ, true, poll);
            } else if (poll) {
                llc_send_supervisory(nbf, LLC_RR, true, true);
            }
            return;
        }

        nbf->vr          = llc_next(nbf->vr);
        nbf->reject_sent = false;
        nbf_handle_info(nbf, frame + LLC_HEADER_I, len - LLC_HEADER_I);
        sent_before = nbf->vs;
        nbf_pump(nbf);
        /* Anything just sent already carries the acknowledgement. */
        if (poll || nbf->vs == sent_before)
            llc_send_supervisory(nbf, LLC_RR, true, poll);
        nbf_reap_sessions(nbf);
        return;
    }

    if ((control & 3) == 1) {
        uint8_t acknowledged;
        bool    poll;

        if (len < LLC_HEADER_I || !nbf->linked)
            return;
        acknowledged = frame[3] >> 1;
        poll         = frame[3] & 1;
        llc_acknowledge(nbf, acknowledged);
        if (control == LLC_RNR) {
            nbf->remote_busy = true;
        } else {
            nbf->remote_busy = false;
            if (control == LLC_REJ)
                llc_retransmit(nbf);
        }
        if (poll && !response)
            llc_send_supervisory(nbf, LLC_RR, true, true);
        nbf_pump(nbf);
        nbf_reap_sessions(nbf);
        return;
    }

    switch (control & (uint8_t) ~LLC_POLL) {
        case LLC_UI:
            nbf_handle_unnumbered(nbf, frame + LLC_HEADER_U, len - LLC_HEADER_U);
            break;

        case LLC_SABME:
            llc_reset(nbf, true);
            nbf->linked = true;
            llc_send_unnumbered(nbf, (uint8_t) (LLC_UA | (control & LLC_POLL)), true, NULL, 0);
            nbf_log(nbf->log, "NBF: LLC connection up\n");
            break;

        case LLC_DISC:
            llc_reset(nbf, false);
            llc_send_unnumbered(nbf, (uint8_t) (LLC_UA | (control & LLC_POLL)), true, NULL, 0);
            nbf_log(nbf->log, "NBF: LLC connection closed\n");
            break;

        case LLC_DM:
            llc_reset(nbf, false);
            break;

        case LLC_XID:
        case LLC_TEST:
            /* Both ask the far end to show it is there by sending the same back. */
            if (!response)
                llc_send_unnumbered(nbf, control, true, frame + LLC_HEADER_U, len - LLC_HEADER_U);
            break;

        default:
            nbf_log(nbf->log, "NBF: Ignoring LLC control 0x%02X\n", control);
            break;
    }
}

void
nbf_tick(nbf_t *nbf)
{
    if (!nbf)
        return;

    if (nbf->ack_timer && --nbf->ack_timer == 0) {
        if (++nbf->retries > LLC_MAX_RETRIES) {
            nbf_log(nbf->log, "NBF: Nothing acknowledged; dropping the LLC connection\n");
            llc_reset(nbf, false);
            llc_send_unnumbered(nbf, LLC_DM | LLC_POLL, true, NULL, 0);
        } else {
            llc_retransmit(nbf);
        }
    }

    if (nbf->guest_seen && ++nbf->announce_ms >= NBF_ANNOUNCE_PERIOD_MS) {
        uint8_t packet[1024];

        nbf->announce_ms = 0;
        for (int which = 0; which < 2; which++) {
            int length = browser_announcement(0, which, packet, sizeof(packet));

            if (length > 0)
                nbf_send_datagram(nbf, packet, length);
        }
    }
}

void
nbf_set_max_frame(nbf_t *nbf, int length)
{
    int data = length - LLC_HEADER_I - NBF_HEADER_SESSION;

    if (nbf && data > 0)
        nbf->max_data = data < NBF_MAX_DATA ? data : NBF_MAX_DATA;
}

bool
nbf_set_ras_credentials(nbf_t *nbf, const char *username, const char *password)
{
    size_t username_length;
    size_t password_length;

    if (!nbf || !username || !password)
        return false;

    username_length = strlen(username);
    password_length = strlen(password);
    if (username_length >= sizeof(nbf->ras_username)
        || password_length >= sizeof(nbf->ras_password))
        return false;

    memcpy(nbf->ras_username, username, username_length + 1);
    memcpy(nbf->ras_password, password, password_length + 1);
    return true;
}

void
nbf_reset(nbf_t *nbf)
{
    if (!nbf)
        return;
    llc_reset(nbf, false);
    nbf->guest_seen  = false;
    nbf->announce_ms = NBF_ANNOUNCE_PERIOD_MS - NBF_ANNOUNCE_FIRST_MS;
}

nbf_t *
nbf_init(void *log, nbf_output_t output, void *priv)
{
    nbf_t *nbf = (nbf_t *) calloc(1, sizeof(nbf_t));

    if (!nbf)
        return NULL;
    nbf->log    = log;
    nbf->output = output;
    nbf->priv   = priv;
    nbf->max_data = NBF_MAX_DATA;
    nbf_reset(nbf);
    return nbf;
}

void
nbf_close(nbf_t *nbf)
{
    if (!nbf)
        return;
    nbf_drop_sessions(nbf, false);
    free(nbf);
}
