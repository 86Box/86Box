/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PPTP for the emulated modem's virtual server. RFC 2637.
 *
 *          A guest's VPN adapter dials the server over TCP port 1723 and then
 *          runs PPP inside GRE. What it finds at the far end is a second copy
 *          of the virtual server with its own address, so a tunnel reaches
 *          SMB, WINS, NetBEUI and IPX but not the internet.
 *
 *          The PPP code here is the modem's own. It expects a serial line, so
 *          each frame is dressed in async HDLC on the way in and undressed on
 *          the way out.
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
#include <86box/log.h>
#include <86box/modem/modem_ppp.h>
#include <86box/network/net_ipx.h>
#include <86box/modem/modem_ipxcp.h>
#include <86box/network/net_nbf.h>
#include <86box/modem/modem_nbfcp.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_vserver.h>
#include <86box/modem/modem_pptp.h>
#include <86box/network/net_vserver_config.h>
#include <86box/network/net_smb.h>

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
pptp_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define pptp_log(priv, fmt, ...)
#endif

#define PPTP_HEADER_LENGTH     12
#define PPTP_MAX_MESSAGE       512
#define PPTP_MAGIC             0x1A2B3C4Du
#define PPTP_CONTROL_MESSAGE   1
#define PPTP_VERSION           0x0100

#define PPTP_START_REQUEST     1
#define PPTP_START_REPLY       2
#define PPTP_STOP_REQUEST      3
#define PPTP_STOP_REPLY        4
#define PPTP_ECHO_REQUEST      5
#define PPTP_ECHO_REPLY        6
#define PPTP_CALL_REQUEST      7
#define PPTP_CALL_REPLY        8
#define PPTP_CLEAR_REQUEST     12
#define PPTP_DISCONNECT_NOTIFY 13

#define PPTP_START_LENGTH      156
#define PPTP_STOP_LENGTH       16
#define PPTP_ECHO_LENGTH       16
#define PPTP_ECHO_REPLY_LENGTH 20
#define PPTP_CALL_LENGTH       168
#define PPTP_CALL_REPLY_LENGTH 32
#define PPTP_CLEAR_LENGTH      16
#define PPTP_NOTIFY_LENGTH     148

#define PPTP_RESULT_OK         1
#define PPTP_RESULT_EXISTS     3 /* in a start reply */
#define PPTP_RESULT_REFUSED    7 /* in a call reply */
#define PPTP_RESULT_LOST       1 /* in a disconnect notify */
#define PPTP_RESULT_CLEARED    4
#define PPTP_FRAMING_BOTH      3
#define PPTP_BEARER_BOTH       3
#define PPTP_SPEED             10000000
#define PPTP_RECV_WINDOW       64

/* GRE as RFC 2637 changes it: a call ID in the key, an optional acknowledgement. */
#define GRE_PROTOCOL_PPP       0x880B
#define GRE_FLAG_KEY           0x20
#define GRE_FLAG_SEQUENCE      0x10
#define GRE_FLAG_ACK           0x80
#define GRE_VERSION            0x01
#define GRE_MIN_HEADER         8
#define GRE_MAX_HEADER         16
#define GRE_ACK_DELAY_MS       100

#define TUNNEL_MRU             1400 /* leaves room for the GRE and IP headers around it */
#define TUNNEL_MSS             (TUNNEL_MRU - 40)
#define TUNNEL_MAX_FRAME       1600

#define HDLC_FLAG              0x7E
#define HDLC_ESCAPE            0x7D
#define HDLC_FCS_LENGTH        2

typedef struct pptp_t {
    vserver_t *outer;
    vserver_t *server;
    void      *log;
    bool       netbeui;
    bool       ipx_enabled;

    bool       started;
    int        rx_length;
    uint8_t    rx[PPTP_MAX_MESSAGE];

    bool       call_up;
    uint16_t   call_id;
    uint16_t   peer_call;
    uint16_t   next_call;
    uint16_t   window;

    uint32_t   tx_seq;     /* the next sequence number to use */
    uint32_t   tx_acked;   /* how many of them the peer has seen */
    uint32_t   rx_seq;     /* the last sequence number received */
    bool       rx_any;
    bool       ack_owed;
    int        ack_ms;

    ppp_ctx_t *ppp;
    nbf_t     *nbf;
    ipx_t     *ipx;
    bool       escaped;
    int        frame_length;
    uint8_t    frame[TUNNEL_MAX_FRAME];
} pptp_t;

static uint16_t
pptp_get16(const uint8_t *p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}

static void
pptp_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) (value >> 8);
    p[1] = (uint8_t) value;
}

static uint32_t
pptp_get32(const uint8_t *p)
{
    return ((uint32_t) pptp_get16(p) << 16) | pptp_get16(p + 2);
}

static void
pptp_put32(uint8_t *p, uint32_t value)
{
    pptp_put16(p, (uint16_t) (value >> 16));
    pptp_put16(p + 2, (uint16_t) value);
}

/* --- GRE --- */

static void
gre_send(pptp_t *pptp, const uint8_t *data, int length)
{
    uint8_t packet[GRE_MAX_HEADER + TUNNEL_MAX_FRAME];
    int     header = GRE_MIN_HEADER;

    packet[0] = GRE_FLAG_KEY;
    packet[1] = GRE_VERSION;
    pptp_put16(packet + 2, GRE_PROTOCOL_PPP);
    pptp_put16(packet + 4, (uint16_t) length);
    pptp_put16(packet + 6, pptp->peer_call);
    if (length) {
        packet[0] |= GRE_FLAG_SEQUENCE;
        pptp_put32(packet + header, pptp->tx_seq++);
        header += 4;
    }
    if (pptp->rx_any) {
        packet[1] |= GRE_FLAG_ACK;
        pptp_put32(packet + header, pptp->rx_seq);
        header += 4;
        pptp->ack_owed = false;
    }
    if (length)
        memcpy(packet + header, data, (size_t) length);
    vserver_tunnel_send_gre(pptp->outer, packet, header + length);
}

/* Frames on the way to the guest, as the PPP code sends them to its serial line. */
static void
pptp_frame_done(pptp_t *pptp)
{
    int length = pptp->frame_length - HDLC_FCS_LENGTH;

    pptp->frame_length = 0;
    if (length <= 0 || !pptp->call_up)
        return;
    /* The peer buffers only so many. PPP can stand losing the odd frame; a flood it can't. */
    if (pptp->tx_seq - pptp->tx_acked >= pptp->window)
        return;
    gre_send(pptp, pptp->frame, length);
}

static void
pptp_serial_push(void *priv, const uint8_t *data, int len)
{
    pptp_t *pptp = (pptp_t *) priv;

    for (int i = 0; i < len; i++) {
        uint8_t byte = data[i];

        if (byte == HDLC_FLAG) {
            pptp->escaped = false;
            pptp_frame_done(pptp);
        } else if (byte == HDLC_ESCAPE) {
            pptp->escaped = true;
        } else if (pptp->frame_length < TUNNEL_MAX_FRAME) {
            pptp->frame[pptp->frame_length++] = pptp->escaped ? byte ^ 0x20 : byte;
            pptp->escaped                     = false;
        }
    }
}

static void
hdlc_put(ppp_ctx_t *ppp, uint8_t byte)
{
    if (byte < 0x20 || byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
        ppp_rx_byte(ppp, HDLC_ESCAPE);
        byte ^= 0x20;
    }
    ppp_rx_byte(ppp, byte);
}

/* Frames from the guest, made to look as though they came down the serial line. */
static void
pptp_feed_ppp(pptp_t *pptp, const uint8_t *frame, int length)
{
    uint16_t fcs = 0xFFFF;

    ppp_rx_byte(pptp->ppp, HDLC_FLAG);
    for (int i = 0; i < length; i++) {
        fcs ^= frame[i];
        for (int bit = 0; bit < 8; bit++)
            fcs = (fcs & 1) ? (uint16_t) ((fcs >> 1) ^ 0x8408) : (uint16_t) (fcs >> 1);
        hdlc_put(pptp->ppp, frame[i]);
    }
    fcs ^= 0xFFFF;
    hdlc_put(pptp->ppp, (uint8_t) fcs);
    hdlc_put(pptp->ppp, (uint8_t) (fcs >> 8));
    ppp_rx_byte(pptp->ppp, HDLC_FLAG);
}

static void
pptp_gre_input(void *priv, const uint8_t *data, int length)
{
    pptp_t  *pptp   = (pptp_t *) priv;
    int      header = GRE_MIN_HEADER;
    int      payload;
    uint32_t seq    = 0;

    if (!pptp->call_up || length < GRE_MIN_HEADER)
        return;
    if ((data[0] & 0xEF) != GRE_FLAG_KEY || (data[1] & 0x7F) != GRE_VERSION
        || pptp_get16(data + 2) != GRE_PROTOCOL_PPP || pptp_get16(data + 6) != pptp->call_id)
        return;

    payload = pptp_get16(data + 4);
    if (data[0] & GRE_FLAG_SEQUENCE) {
        if (length < header + 4)
            return;
        seq = pptp_get32(data + header);
        header += 4;
    }
    if (data[1] & GRE_FLAG_ACK) {
        uint32_t acked;

        if (length < header + 4)
            return;
        acked = pptp_get32(data + header) + 1;
        header += 4;
        /* It counts forward from what was already acknowledged, however it wraps. */
        if ((int32_t) (acked - pptp->tx_acked) > 0 && (int32_t) (pptp->tx_seq - acked) >= 0)
            pptp->tx_acked = acked;
    }

    if (!(data[0] & GRE_FLAG_SEQUENCE) || !payload)
        return;
    if (payload > length - header || payload > TUNNEL_MAX_FRAME)
        return;
    if (pptp->rx_any && (int32_t) (seq - pptp->rx_seq) <= 0)
        return; /* late, or a duplicate */

    if (!pptp->ack_owed)
        pptp->ack_ms = GRE_ACK_DELAY_MS;
    pptp->rx_any   = true;
    pptp->rx_seq   = seq;
    pptp->ack_owed = true;
    pptp_feed_ppp(pptp, data + header, payload);
}

/* --- The session inside the tunnel --- */

/* A guest's MRU says nothing of the GRE and IP headers that go around the frame. */
static int
tunnel_limit(int length)
{
    return length < TUNNEL_MRU ? length : TUNNEL_MRU;
}

static void
pptp_ppp_send_ip(void *priv, const uint8_t *packet, int len)
{
    vserver_process_ipv4(((pptp_t *) priv)->server, packet, len);
}

static void
pptp_server_output(void *priv, const uint8_t *packet, int len)
{
    pptp_t *pptp = (pptp_t *) priv;

    if (pptp->ppp)
        ppp_wrap_ip(pptp->ppp, packet, len);
}

static void
pptp_ppp_send_nbf(void *priv, const uint8_t *frame, int len)
{
    pptp_t *pptp = (pptp_t *) priv;

    nbf_set_max_frame(pptp->nbf, tunnel_limit(ppp_nbf_max_frame(pptp->ppp)));
    nbf_input(pptp->nbf, frame, len);
}

static void
pptp_nbf_output(void *priv, const uint8_t *frame, int len)
{
    pptp_t *pptp = (pptp_t *) priv;

    if (pptp->ppp)
        ppp_wrap_nbf(pptp->ppp, frame, len);
}

static void
pptp_ppp_send_ipx(void *priv, const uint8_t *packet, int len)
{
    pptp_t *pptp = (pptp_t *) priv;

    ipx_configure(pptp->ipx, ppp_ipx_network(pptp->ppp), ppp_ipx_node(pptp->ppp),
                  tunnel_limit(ppp_ipx_max_packet(pptp->ppp)));
    ipx_input(pptp->ipx, packet, len);
}

static void
pptp_ipx_output(void *priv, const uint8_t *packet, int len)
{
    pptp_t *pptp = (pptp_t *) priv;

    if (pptp->ppp)
        ppp_wrap_ipx(pptp->ppp, packet, len);
}

static void
pptp_call_clear(pptp_t *pptp)
{
    nbf_close(pptp->nbf);
    ipx_close(pptp->ipx);
    vserver_close(pptp->server);
    if (pptp->ppp)
        ppp_close(pptp->ppp);
    pptp->nbf     = NULL;
    pptp->ipx     = NULL;
    pptp->server  = NULL;
    pptp->ppp     = NULL;
    pptp->call_up = false;
}

static bool
pptp_call_start(pptp_t *pptp)
{
    static const uint8_t no_node[IPX_NODE_LENGTH] = { 0 };

    pptp->tx_seq       = 0;
    pptp->tx_acked     = 0;
    pptp->rx_seq       = 0;
    pptp->rx_any       = false;
    pptp->ack_owed     = false;
    pptp->escaped      = false;
    pptp->frame_length = 0;

    pptp->server = vserver_init(pptp->log, PPTP_SERVER_IP, true, true, false, false, pptp_server_output, pptp);
    if (pptp->server)
        pptp->ppp = ppp_init(pptp, pptp->log, pptp_serial_push, pptp_ppp_send_ip);
    if (!pptp->ppp) {
        pptp_call_clear(pptp);
        return false;
    }
    vserver_answer_broadcasts(pptp->server);
    vserver_limit_segment(pptp->server, TUNNEL_MSS);

    pptp->ppp->our_ip              = PPTP_SERVER_IP;
    pptp->ppp->peer_ip             = PPTP_GUEST_IP;
    pptp->ppp->dns1                = 0;
    pptp->ppp->dns2                = 0;
    pptp->ppp->wins1               = PPTP_SERVER_IP;
    pptp->ppp->ipcp_dns_wins_mode  = PPP_IPCP_DNS_WINS_OFFER;
    pptp->ppp->our_mru             = TUNNEL_MRU;
    pptp->ppp->auth_type           = PPP_AUTH_MSCHAPV2;
    pptp->ppp->auth_auto           = true;
    pptp->ppp->ccp_mode            = PPP_CCP_MODE_ENABLED;
    pptp->ppp->mppe_key_state_mode = PPP_MPPE_STATE_OFFER_STATELESS; /* GRE can lose packets */
    ppp_mppe_set_policy(pptp->ppp, CCP_MPPE_AUTOMATIC, PPP_MPPE_FORMAT_AUTOMATIC);
    /* Anything that widens the framing would have to be undone at this end of the serial line. */
    pptp->ppp->ppp_fcs_mode        = PPP_FCS_DISABLED;
    pptp->ppp->ppp_numbered_mode   = PPP_NUMBERED_DISABLED;
    snprintf(pptp->ppp->username, sizeof(pptp->ppp->username), "%s", VSERVER_LOGON_USERNAME);
    snprintf(pptp->ppp->password, sizeof(pptp->ppp->password), "%s", VSERVER_LOGON_PASSWORD);

    if (pptp->netbeui) {
        pptp->nbf = nbf_init(pptp->log, pptp_nbf_output, pptp);
        if (pptp->nbf)
            pptp->ppp->network_send_nbf = pptp_ppp_send_nbf;
    }
    if (pptp->ipx_enabled) {
        pptp->ipx = ipx_init(pptp->log, no_node, pptp_ipx_output, pptp);
        if (pptp->ipx)
            pptp->ppp->network_send_ipx = pptp_ppp_send_ipx;
    }

    pptp->call_up = true;
    ppp_start(pptp->ppp);
    return pptp->ppp->state != PPP_STATE_DEAD;
}

/* --- The control connection --- */

static uint8_t *
pptp_begin(uint8_t *message, int length, uint16_t type)
{
    memset(message, 0, (size_t) length);
    pptp_put16(message, (uint16_t) length);
    pptp_put16(message + 2, PPTP_CONTROL_MESSAGE);
    pptp_put32(message + 4, PPTP_MAGIC);
    pptp_put16(message + 8, type);
    return message + PPTP_HEADER_LENGTH;
}

static void
pptp_handle_start(pptp_t *pptp)
{
    uint8_t  message[PPTP_START_LENGTH];
    uint8_t *body = pptp_begin(message, sizeof(message), PPTP_START_REPLY);

    pptp_put16(body, PPTP_VERSION);
    body[2] = pptp->started ? PPTP_RESULT_EXISTS : PPTP_RESULT_OK;
    pptp_put32(body + 4, PPTP_FRAMING_BOTH);
    pptp_put32(body + 8, PPTP_BEARER_BOTH);
    pptp_put16(body + 12, 1); /* channels */
    pptp_put16(body + 14, 1); /* firmware revision */
    memcpy(body + 16, VSERVER_SERVER_NAME, sizeof(VSERVER_SERVER_NAME) - 1);
    memcpy(body + 80, VSERVER_PPTP_VENDOR_NAME, sizeof(VSERVER_PPTP_VENDOR_NAME) - 1);
    pptp->started = true;
    vserver_tunnel_send_control(pptp->outer, message, sizeof(message));
}

static void
pptp_handle_echo(pptp_t *pptp, const uint8_t *body)
{
    uint8_t  message[PPTP_ECHO_REPLY_LENGTH];
    uint8_t *reply = pptp_begin(message, sizeof(message), PPTP_ECHO_REPLY);

    memcpy(reply, body, 4);
    reply[4] = PPTP_RESULT_OK;
    vserver_tunnel_send_control(pptp->outer, message, sizeof(message));
}

static void
pptp_handle_call(pptp_t *pptp, const uint8_t *body)
{
    uint8_t  message[PPTP_CALL_REPLY_LENGTH];
    uint8_t *reply = pptp_begin(message, sizeof(message), PPTP_CALL_REPLY);
    bool     up    = false;

    if (!pptp->call_up) {
        pptp->peer_call = pptp_get16(body);
        pptp->call_id   = ++pptp->next_call;
        pptp->window    = pptp_get16(body + 20);
        if (!pptp->window)
            pptp->window = 1;
        up = true;
    }

    pptp_put16(reply, up ? pptp->call_id : 0);
    pptp_put16(reply + 2, pptp_get16(body));
    reply[4] = up ? PPTP_RESULT_OK : PPTP_RESULT_REFUSED;
    pptp_put32(reply + 8, PPTP_SPEED);
    pptp_put16(reply + 12, PPTP_RECV_WINDOW);
    vserver_tunnel_send_control(pptp->outer, message, sizeof(message));

    if (up && !pptp_call_start(pptp)) {
        pptp_log(pptp->log, "PPTP: Couldn't start PPP in the tunnel\n");
        pptp_call_clear(pptp);
    }
}

static void
pptp_notify_disconnect(pptp_t *pptp, uint8_t result)
{
    uint8_t  message[PPTP_NOTIFY_LENGTH];
    uint8_t *body = pptp_begin(message, sizeof(message), PPTP_DISCONNECT_NOTIFY);

    pptp_put16(body, pptp->call_id);
    body[2] = result;
    pptp_call_clear(pptp);
    vserver_tunnel_send_control(pptp->outer, message, sizeof(message));
}

static void
pptp_handle_stop(pptp_t *pptp)
{
    uint8_t  message[PPTP_STOP_LENGTH];
    uint8_t *body = pptp_begin(message, sizeof(message), PPTP_STOP_REPLY);

    body[0] = PPTP_RESULT_OK;
    pptp_call_clear(pptp);
    pptp->started = false;
    vserver_tunnel_send_control(pptp->outer, message, sizeof(message));
}

/* Returns false once the connection should close. */
static bool
pptp_handle_message(pptp_t *pptp, const uint8_t *message, int length)
{
    const uint8_t *body = message + PPTP_HEADER_LENGTH;
    uint16_t       type = pptp_get16(message + 8);

    if (pptp_get16(message + 2) != PPTP_CONTROL_MESSAGE || pptp_get32(message + 4) != PPTP_MAGIC)
        return false;

    switch (type) {
        case PPTP_START_REQUEST:
            if (length >= PPTP_START_LENGTH)
                pptp_handle_start(pptp);
            break;
        case PPTP_STOP_REQUEST:
            pptp_handle_stop(pptp);
            return false;
        case PPTP_ECHO_REQUEST:
            if (length >= PPTP_ECHO_LENGTH)
                pptp_handle_echo(pptp, body);
            break;
        case PPTP_CALL_REQUEST:
            if (length >= PPTP_CALL_LENGTH && pptp->started)
                pptp_handle_call(pptp, body);
            break;
        case PPTP_CLEAR_REQUEST:
            if (length >= PPTP_CLEAR_LENGTH && pptp->call_up && pptp_get16(body) == pptp->peer_call) {
                pptp_log(pptp->log, "PPTP: Call cleared by the guest\n");
                pptp_notify_disconnect(pptp, PPTP_RESULT_CLEARED);
            }
            break;
        default:
            /* Set-Link-Info changes nothing: every byte is escaped on the way into PPP. */
            break;
    }
    return true;
}

static bool
pptp_control_input(void *priv, const uint8_t *data, int length)
{
    pptp_t *pptp = (pptp_t *) priv;

    while (length > 0) {
        int want = PPTP_HEADER_LENGTH;
        int take;

        if (pptp->rx_length >= 2) {
            want = pptp_get16(pptp->rx);
            if (want < PPTP_HEADER_LENGTH || want > PPTP_MAX_MESSAGE)
                return false;
        }
        take = want - pptp->rx_length;
        if (take > length)
            take = length;
        memcpy(pptp->rx + pptp->rx_length, data, (size_t) take);
        pptp->rx_length += take;
        data += take;
        length -= take;

        if (pptp->rx_length >= PPTP_HEADER_LENGTH && pptp->rx_length == pptp_get16(pptp->rx)) {
            int complete = pptp->rx_length;

            pptp->rx_length = 0;
            if (!pptp_handle_message(pptp, pptp->rx, complete))
                return false;
        }
    }
    return true;
}

static void
pptp_control_closed(void *priv)
{
    pptp_t *pptp = (pptp_t *) priv;

    pptp_call_clear(pptp);
    pptp->started   = false;
    pptp->rx_length = 0;
}

static void
pptp_tick(void *priv)
{
    pptp_t *pptp = (pptp_t *) priv;

    if (!pptp->call_up)
        return;

    ppp_timer_tick(pptp->ppp);
    vserver_tick(pptp->server);
    if (ppp_nbf_ready(pptp->ppp))
        nbf_tick(pptp->nbf);
    if (ppp_ipx_ready(pptp->ppp))
        ipx_tick(pptp->ipx);

    if (pptp->ppp->state == PPP_STATE_DEAD) {
        pptp_log(pptp->log, "PPTP: PPP in the tunnel has ended\n");
        pptp_notify_disconnect(pptp, PPTP_RESULT_LOST);
        return;
    }

    if (pptp->ack_owed && --pptp->ack_ms <= 0)
        gre_send(pptp, NULL, 0);
}

static void
pptp_close(void *priv)
{
    pptp_t *pptp = (pptp_t *) priv;

    pptp_call_clear(pptp);
    free(pptp);
}

bool
pptp_attach(vserver_t *server, void *log, bool netbeui, bool ipx)
{
    static const vserver_tunnel_t tunnel = {
        pptp_control_input, pptp_control_closed, pptp_gre_input, pptp_tick, pptp_close
    };
    pptp_t *pptp;

    if (!server)
        return false;
    pptp = (pptp_t *) calloc(1, sizeof(pptp_t));
    if (!pptp)
        return false;
    pptp->outer       = server;
    pptp->log         = log;
    pptp->netbeui     = netbeui;
    pptp->ipx_enabled = ipx;
    vserver_set_tunnel(server, &tunnel, PPTP_PORT, pptp);
    return true;
}
