/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          A virtual IPv4 server for SLiRP and modem links. Packets for it
 *          are answered here instead of being forwarded.
 *
 *          SLiRP's guest forwarding gives no connect or close events and runs
 *          on another thread, so TCP is ended here instead. The link to the
 *          guest is a single, ordered path, which keeps the endpoint small:
 *          no reassembly, a fixed window, and retransmission on one timer.
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
#include <86box/network/net_browser.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_smb.h>
#include <86box/network/net_vserver.h>
#include <86box/network/net_vserver_config.h>
#ifdef ENABLE_VSERVER_FTP
#    include "net_ftp.h"
#endif
#ifdef ENABLE_VSERVER_TFTP
#    include "net_tftp.h"
#endif

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
vserver_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define vserver_log(priv, fmt, ...)
#endif

#define IP_HEADER_LENGTH  20
#define UDP_HEADER_LENGTH 8
#define TCP_HEADER_LENGTH 20
#define IP_PROTOCOL_ICMP  1
#define IP_PROTOCOL_TCP   6
#define IP_PROTOCOL_GRE   47
#define IP_PROTOCOL_UDP   17
#define ICMP_ECHO_REQUEST 8

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCP_MAX_CONNECTIONS 4
#define TCP_DEFAULT_MSS     536
#define TCP_OUR_MSS         1460
#define TCP_WINDOW          4096
#define TCP_TX_BUFFER       32768
#define TCP_RETRANSMIT_MS   1500
#define TCP_MAX_RETRIES     8
#define FTP_CONTROL_PORT    21
#define FTP_ACTIVE_PORT     20
#define FTP_PASSIVE_BASE    49152
#define FTP_PASSIVE_PORTS   TCP_MAX_CONNECTIONS

#define SMB_PORT 139
#define VSERVER_MAX_GRE 1480 /* what fits in one IP packet on a 1500 byte link */

#define ANNOUNCE_FIRST_MS  2000
#define ANNOUNCE_PERIOD_MS 60000

typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_CLOSING
} tcp_state_t;

typedef struct tcp_connection_t {
    tcp_state_t    state;
    uint32_t       peer_ip;
    uint16_t       peer_port;
    uint16_t       port;     /* which of the server's it arrived on */
    bool           tunnel;   /* the tunnel's control connection, rather than SMB */
    bool           close_after_tx;
    uint16_t       mss;
    uint32_t       peer_window;
    uint32_t       snd_una;  /* sequence number of tx[0] */
    uint32_t       rcv_nxt;
    int            tx_len;
    int            tx_sent;
    bool           fin_sent;
    bool           ftp_transfer_complete;
    uint32_t       retransmit_ms;
    int            retries;
    smb_session_t *session;
#ifdef ENABLE_VSERVER_FTP
    vserver_ftp_session_t *ftp;
    struct tcp_connection_t *ftp_control;
    struct tcp_connection_t *ftp_data;
    uint16_t                ftp_passive_port;
    bool                    ftp_transfer_active;
    bool                    ftp_active_data;
#endif
    uint8_t        tx[TCP_TX_BUFFER];
} tcp_connection_t;

struct vserver_t {
    bool              answer_broadcasts;
    void            *log;
    uint32_t         server_ip;
    bool             wins;
    bool             smb;
    bool             ftp;
    bool             tftp;
    vserver_output_t output;
    void            *priv;
    nbns_t          *nbns;
#ifdef ENABLE_VSERVER_TFTP
    vserver_tftp_t  *tftp_server;
#endif
    uint16_t         ip_id;
    uint32_t         guest_ip;
    uint32_t         announce_ms;
    uint32_t         iss;
    uint16_t         max_mss;
    tcp_connection_t connections[TCP_MAX_CONNECTIONS];

    const vserver_tunnel_t *tunnel;
    void                   *tunnel_priv;
    uint16_t                tunnel_port;
    uint32_t                tunnel_peer;
};

static uint16_t
vs_get16(const uint8_t *p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}

static uint32_t
vs_get32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static void
vs_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) (value >> 8);
    p[1] = (uint8_t) value;
}

static void
vs_put32(uint8_t *p, uint32_t value)
{
    vs_put16(p, (uint16_t) (value >> 16));
    vs_put16(p + 2, (uint16_t) value);
}

static uint16_t
vs_checksum(uint32_t sum, const uint8_t *data, int len)
{
    for (int i = 0; i + 1 < len; i += 2)
        sum += vs_get16(data + i);
    if (len & 1)
        sum += (uint32_t) data[len - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t) ~sum;
}

static uint32_t
vs_pseudo_header(uint32_t source, uint32_t destination, uint8_t protocol, int length)
{
    return (source >> 16) + (source & 0xFFFF) + (destination >> 16) + (destination & 0xFFFF)
         + protocol + (uint32_t) length;
}

/* Fills in an IPv4 header for a payload already at packet + 20 and sends it. */
static void
vs_send_ip(vserver_t *server, uint8_t *packet, uint8_t protocol, uint32_t destination, int payload)
{
    memset(packet, 0, IP_HEADER_LENGTH);
    packet[0] = 0x45;
    vs_put16(packet + 2, (uint16_t) (IP_HEADER_LENGTH + payload));
    vs_put16(packet + 4, server->ip_id++);
    packet[8] = 128;
    packet[9] = protocol;
    vs_put32(packet + 12, server->server_ip);
    vs_put32(packet + 16, destination);
    vs_put16(packet + 10, vs_checksum(0, packet, IP_HEADER_LENGTH));
    server->output(server->priv, packet, IP_HEADER_LENGTH + payload);
}

static void
vs_send_udp(vserver_t *server, uint32_t destination, uint16_t source_port, uint16_t port,
            const uint8_t *data, int length)
{
    uint8_t  packet[IP_HEADER_LENGTH + UDP_HEADER_LENGTH + 1024];
    uint8_t *udp = packet + IP_HEADER_LENGTH;
    int      total = UDP_HEADER_LENGTH + length;
    uint16_t checksum;

    if (length > 1024)
        return;
    memset(udp, 0, UDP_HEADER_LENGTH);
    vs_put16(udp, source_port);
    vs_put16(udp + 2, port);
    vs_put16(udp + 4, (uint16_t) total);
    memcpy(udp + UDP_HEADER_LENGTH, data, (size_t) length);
    checksum = vs_checksum(vs_pseudo_header(server->server_ip, destination, IP_PROTOCOL_UDP, total),
                           udp, total);
    vs_put16(udp + 6, checksum ? checksum : 0xFFFF);
    vs_send_ip(server, packet, IP_PROTOCOL_UDP, destination, total);
}

#ifdef ENABLE_VSERVER_TFTP
static void
vs_tftp_output(void *priv, uint32_t destination, uint16_t source_port, uint16_t port, const uint8_t *data, int length)
{
    vs_send_udp((vserver_t *) priv, destination, source_port, port, data, length);
}
#endif

static void
tcp_send(vserver_t *server, uint32_t destination, uint16_t source_port, uint16_t port, uint32_t seq,
         uint32_t ack, uint8_t flags, const uint8_t *data, int length, bool mss_option)
{
    uint8_t  packet[IP_HEADER_LENGTH + TCP_HEADER_LENGTH + 4 + TCP_OUR_MSS];
    uint8_t *tcp    = packet + IP_HEADER_LENGTH;
    int      header = TCP_HEADER_LENGTH + (mss_option ? 4 : 0);
    int      total  = header + length;

    memset(tcp, 0, (size_t) header);
    vs_put16(tcp, source_port);
    vs_put16(tcp + 2, port);
    vs_put32(tcp + 4, seq);
    vs_put32(tcp + 8, ack);
    tcp[12] = (uint8_t) ((header / 4) << 4);
    tcp[13] = flags;
    vs_put16(tcp + 14, TCP_WINDOW);
    if (mss_option) {
        tcp[20] = 2;
        tcp[21] = 4;
        vs_put16(tcp + 22, server->max_mss);
    }
    if (length > 0)
        memcpy(tcp + header, data, (size_t) length);
    vs_put16(tcp + 16, vs_checksum(vs_pseudo_header(server->server_ip, destination, IP_PROTOCOL_TCP, total),
                                   tcp, total));
    vs_send_ip(server, packet, IP_PROTOCOL_TCP, destination, total);
}

static void tcp_flush(vserver_t *server, tcp_connection_t *connection);

static void
tcp_close_connection(vserver_t *server, tcp_connection_t *connection)
{
#ifdef ENABLE_VSERVER_FTP
    if (connection->ftp) {
        for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
            tcp_connection_t *data = &server->connections[i];

            if (data->ftp_control == connection) {
                data->ftp_control = NULL;
                data->state = TCP_CLOSED;
            }
        }
        vserver_ftp_session_close(connection->ftp);
        connection->ftp = NULL;
    }
    if (connection->ftp_control) {
        tcp_connection_t *control = connection->ftp_control;

        if (control->ftp && control->state != TCP_CLOSED) {
            uint8_t reply[64];
            int     length;

            if (connection->ftp_transfer_active || vserver_ftp_transfer_pending(control->ftp))
                length = vserver_ftp_finish_transfer(control->ftp, connection->ftp_transfer_complete, reply,
                                                     sizeof(reply));
            else {
                vserver_ftp_data_closed(control->ftp);
                length = 0;
            }
            if (length > 0 && length <= TCP_TX_BUFFER - control->tx_len) {
                memcpy(control->tx + control->tx_len, reply, (size_t) length);
                control->tx_len += length;
                tcp_flush(server, control);
            }
            if (control->ftp_data == connection)
                control->ftp_data = NULL;
        }
        connection->ftp_control = NULL;
    }
#endif
    if (connection->tunnel)
        server->tunnel->control_closed(server->tunnel_priv);
    connection->tunnel = false;
    smb_session_close(connection->session);
    connection->session = NULL;
    connection->state   = TCP_CLOSED;
}

/* Sends whatever the window allows, from tx_sent onwards. */
static void
tcp_flush(vserver_t *server, tcp_connection_t *connection)
{
    while (connection->tx_sent < connection->tx_len) {
        int     window = (int) connection->peer_window - connection->tx_sent;
        int     length    = connection->tx_len - connection->tx_sent;
        uint8_t flags     = TCP_ACK;

        if (length > connection->mss)
            length = connection->mss;
        if (length > window)
            length = window;
        if (length <= 0)
            break;
        if (connection->tx_sent + length == connection->tx_len)
            flags |= TCP_PSH;
        tcp_send(server, connection->peer_ip, connection->port, connection->peer_port,
                 connection->snd_una + (uint32_t) connection->tx_sent, connection->rcv_nxt, flags,
                 connection->tx + connection->tx_sent, length, false);
        connection->tx_sent += length;
        connection->retransmit_ms = 0;
    }
}

#ifdef ENABLE_VSERVER_FTP
static uint16_t
tcp_ftp_allocate_port(vserver_t *server)
{
    for (int port = 0; port < FTP_PASSIVE_PORTS; port++) {
        uint16_t candidate = (uint16_t) (FTP_PASSIVE_BASE + port);
        bool     used = false;

        for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
            const tcp_connection_t *connection = &server->connections[i];

            if (connection->state != TCP_CLOSED && connection->ftp_passive_port == candidate) {
                used = true;
                break;
            }
        }
        if (!used)
            return candidate;
    }
    return 0;
}

static tcp_connection_t *
tcp_ftp_passive_owner(vserver_t *server, uint16_t port)
{
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_connection_t *connection = &server->connections[i];

        if (connection->state != TCP_CLOSED && connection->ftp && connection->ftp_passive_port == port
            && !connection->ftp_data && vserver_ftp_passive_enabled(connection->ftp))
            return connection;
    }
    return NULL;
}

static void
tcp_ftp_start_transfer(vserver_t *server, tcp_connection_t *control)
{
    tcp_connection_t *data = control->ftp_data;
    const uint8_t    *file;
    int               length;

    if (!data || !control->ftp || control->tx_sent < control->tx_len || data->state != TCP_ESTABLISHED
        || data->ftp_transfer_active
        || !vserver_ftp_start_transfer(control->ftp, &file, &length))
        return;
    if (length < 0 || length > TCP_TX_BUFFER) {
        vserver_ftp_data_closed(control->ftp);
        return;
    }
    memcpy(data->tx, file, (size_t) length);
    data->tx_len = length;
    data->ftp_transfer_active = true;
    data->close_after_tx = true;
    tcp_flush(server, data);
}

static void
tcp_ftp_start_active(vserver_t *server, tcp_connection_t *control)
{
    tcp_connection_t *data = NULL;
    uint32_t          client_ip;
    uint16_t          client_port;

    if (!control->ftp || control->ftp_data || !vserver_ftp_transfer_pending(control->ftp)
        || !vserver_ftp_active_target(control->ftp, &client_ip, &client_port))
        return;
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (server->connections[i].state == TCP_CLOSED) {
            data = &server->connections[i];
            break;
        }
    }
    if (!data) {
        uint8_t reply[64];
        int     length = vserver_ftp_finish_transfer(control->ftp, false, reply, sizeof(reply));

        if (length > 0 && length <= TCP_TX_BUFFER - control->tx_len) {
            memcpy(control->tx + control->tx_len, reply, (size_t) length);
            control->tx_len += length;
            tcp_flush(server, control);
        }
        return;
    }

    data->session = NULL;
    data->ftp = NULL;
    data->ftp_control = control;
    data->ftp_data = NULL;
    data->ftp_passive_port = 0;
    data->ftp_transfer_active = false;
    data->ftp_active_data = true;
    data->tunnel = false;
    data->close_after_tx = false;
    data->mss = TCP_DEFAULT_MSS;
    data->peer_window = 0;
    data->snd_una = server->iss;
    data->rcv_nxt = 0;
    data->tx_len = 0;
    data->tx_sent = 0;
    data->fin_sent = false;
    data->ftp_transfer_complete = false;
    data->retries = 0;
    data->retransmit_ms = 0;
    data->peer_ip = client_ip;
    data->peer_port = client_port;
    data->port = FTP_ACTIVE_PORT;
    data->state = TCP_SYN_SENT;
    control->ftp_data = data;
    server->iss += 64000;
    tcp_send(server, client_ip, FTP_ACTIVE_PORT, client_port, data->snd_una, 0, TCP_SYN, NULL, 0, true);
}
#endif

static tcp_connection_t *
tcp_find(vserver_t *server, uint32_t ip, uint16_t port, uint16_t local_port)
{
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_connection_t *connection = &server->connections[i];

        if (connection->state != TCP_CLOSED && connection->peer_ip == ip
            && connection->peer_port == port && connection->port == local_port)
            return connection;
    }
    return NULL;
}

static bool
tcp_listens(vserver_t *server, uint16_t port)
{
    if (server->smb && port == SMB_PORT)
        return true;
    if (server->tunnel && port == server->tunnel_port)
        return true;
#ifdef ENABLE_VSERVER_FTP
    if (server->ftp && port == FTP_CONTROL_PORT)
        return true;
    if (tcp_ftp_passive_owner(server, port))
        return true;
#endif
    return false;
}

static tcp_connection_t *
tcp_tunnel_connection(vserver_t *server)
{
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (server->connections[i].state != TCP_CLOSED && server->connections[i].tunnel)
            return &server->connections[i];
    }
    return NULL;
}

static void
tcp_accept(vserver_t *server, uint32_t ip, uint16_t local_port, uint16_t port, uint32_t seq, uint16_t window,
           const uint8_t *tcp, int header)
{
    tcp_connection_t *connection = NULL;
    bool              tunnel     = server->tunnel && local_port == server->tunnel_port;
#ifdef ENABLE_VSERVER_FTP
    tcp_connection_t *ftp_control = local_port == FTP_CONTROL_PORT ? NULL : tcp_ftp_passive_owner(server, local_port);
#endif

    for (int i = 0; i < TCP_MAX_CONNECTIONS && !connection; i++) {
        if (server->connections[i].state == TCP_CLOSED)
            connection = &server->connections[i];
    }
    /* The tunnel has only the one control connection. */
    if (!connection || (tunnel && tcp_tunnel_connection(server))) {
        tcp_send(server, ip, local_port, port, 0, seq + 1, TCP_RST | TCP_ACK, NULL, 0, false);
        return;
    }

    connection->session = NULL;
#ifdef ENABLE_VSERVER_FTP
    connection->ftp = NULL;
    connection->ftp_control = NULL;
    connection->ftp_data = NULL;
    connection->ftp_passive_port = 0;
    connection->ftp_transfer_active = false;
    connection->ftp_active_data = false;
#endif
    if (local_port == SMB_PORT) {
        connection->session = smb_session_create(server->log);
        if (!connection->session)
            return;
    }
#ifdef ENABLE_VSERVER_FTP
    if (server->ftp && local_port == FTP_CONTROL_PORT) {
        connection->ftp = vserver_ftp_session_create();
        connection->ftp_passive_port = tcp_ftp_allocate_port(server);
        if (!connection->ftp) {
            vserver_ftp_session_close(connection->ftp);
            connection->ftp = NULL;
            tcp_send(server, ip, local_port, port, 0, seq + 1, TCP_RST | TCP_ACK, NULL, 0, false);
            return;
        }
    } else if (ftp_control) {
        connection->ftp_control = ftp_control;
        ftp_control->ftp_data = connection;
    }
#endif
    if (tunnel)
        server->tunnel_peer = ip;
    connection->tunnel      = tunnel;
    connection->state       = TCP_SYN_RECEIVED;
    connection->peer_ip     = ip;
    connection->peer_port   = port;
    connection->port        = local_port;
    connection->peer_window = window;
    connection->mss         = TCP_DEFAULT_MSS;
    connection->rcv_nxt     = seq + 1;
    connection->snd_una     = server->iss;
    connection->tx_len      = 0;
    connection->tx_sent     = 0;
    connection->fin_sent    = false;
    connection->close_after_tx = false;
    connection->ftp_transfer_complete = false;
    connection->retries     = 0;
    connection->retransmit_ms = 0;
    server->iss += 64000;

    for (int pos = TCP_HEADER_LENGTH; pos + 1 < header;) {
        if (tcp[pos] == 0)
            break;
        if (tcp[pos] == 1) {
            pos++;
            continue;
        }
        if (tcp[pos] == 2 && tcp[pos + 1] == 4 && pos + 4 <= header) {
            uint16_t mss = vs_get16(tcp + pos + 2);

            if (mss >= 64 && mss < connection->mss * 3)
                connection->mss = mss < server->max_mss ? mss : server->max_mss;
        }
        if (tcp[pos + 1] < 2)
            break;
        pos += tcp[pos + 1];
    }

    vserver_log(server->log, "VSERVER: TCP connection to port %d\n", local_port);
    tcp_send(server, ip, local_port, port, connection->snd_una, connection->rcv_nxt, TCP_SYN | TCP_ACK,
             NULL, 0, true);
}

static void
tcp_start_close(vserver_t *server, tcp_connection_t *connection)
{
    tcp_send(server, connection->peer_ip, connection->port, connection->peer_port,
             connection->snd_una + (uint32_t) connection->tx_len, connection->rcv_nxt,
             TCP_FIN | TCP_ACK, NULL, 0, false);
    connection->fin_sent = true;
    connection->state    = TCP_CLOSING;
}

static void
tcp_input(vserver_t *server, uint32_t ip, const uint8_t *tcp, int length)
{
    int               header;
    uint16_t          source;
    uint16_t          destination;
    uint32_t          seq;
    uint32_t          ack;
    uint8_t           flags;
    uint16_t          window;
    int               payload;
    tcp_connection_t *connection;
#ifdef ENABLE_VSERVER_FTP
    bool established = false;
#endif

    if (length < TCP_HEADER_LENGTH)
        return;
    source      = vs_get16(tcp);
    destination = vs_get16(tcp + 2);
    seq         = vs_get32(tcp + 4);
    ack         = vs_get32(tcp + 8);
    header      = (tcp[12] >> 4) * 4;
    flags       = tcp[13];
    window      = vs_get16(tcp + 14);
    if (header < TCP_HEADER_LENGTH || header > length)
        return;
    payload = length - header;

    if (flags & TCP_RST) {
        connection = tcp_find(server, ip, source, destination);
        if (connection)
            tcp_close_connection(server, connection);
        return;
    }

    connection = tcp_find(server, ip, source, destination);
    if (!connection && !tcp_listens(server, destination)) {
        tcp_send(server, ip, destination, source, ack, seq + (uint32_t) payload + ((flags & TCP_SYN) ? 1 : 0),
                 TCP_RST | TCP_ACK, NULL, 0, false);
        return;
    }

    if (!connection) {
        if (flags & TCP_SYN)
            tcp_accept(server, ip, destination, source, seq, window, tcp, header);
        else if (!(flags & TCP_ACK) || payload > 0 || (flags & TCP_FIN))
            tcp_send(server, ip, destination, source, ack, seq + (uint32_t) payload, TCP_RST | TCP_ACK,
                     NULL, 0, false);
        return;
    }

    if (flags & TCP_SYN) {
        if (connection->state == TCP_SYN_RECEIVED)
            tcp_send(server, ip, destination, source, connection->snd_una, connection->rcv_nxt,
                     TCP_SYN | TCP_ACK, NULL, 0, true);
        else if (connection->state == TCP_SYN_SENT && (flags & TCP_ACK) && ack == connection->snd_una + 1) {
            connection->snd_una = ack;
            connection->rcv_nxt = seq + 1;
            connection->state = TCP_ESTABLISHED;
#ifdef ENABLE_VSERVER_FTP
            established = true;
#endif
#ifdef ENABLE_VSERVER_FTP
        } else if (connection->state == TCP_ESTABLISHED && connection->ftp_active_data
                   && (flags & TCP_ACK) && ack == connection->snd_una && seq + 1 == connection->rcv_nxt) {
            tcp_send(server, ip, destination, source, connection->snd_una, connection->rcv_nxt, TCP_ACK,
                     NULL, 0, false);
            return;
#endif
        } else
            return;
    }

    connection->peer_window = window;

    if (flags & TCP_ACK) {
        if (connection->state == TCP_SYN_RECEIVED) {
            if (ack == connection->snd_una + 1) {
                connection->snd_una = ack;
                connection->state   = TCP_ESTABLISHED;
#ifdef ENABLE_VSERVER_FTP
                established = true;
#endif
            }
        } else {
            int acked = (int) (ack - connection->snd_una);

            if (acked > 0 && acked <= connection->tx_sent) {
                memmove(connection->tx, connection->tx + acked,
                        (size_t) (connection->tx_len - acked));
                connection->tx_len -= acked;
                connection->tx_sent -= acked;
                connection->snd_una = ack;
                connection->retries = 0;
                connection->retransmit_ms = 0;
            }
            if (connection->state == TCP_CLOSING && connection->fin_sent && !connection->tx_len
                && ack == connection->snd_una + 1) {
#ifdef ENABLE_VSERVER_FTP
                if (connection->ftp_control && connection->ftp_transfer_active)
                    connection->ftp_transfer_complete = true;
#endif
                tcp_close_connection(server, connection);
                return;
            }
        }
    }

#ifdef ENABLE_VSERVER_FTP
    if (established) {
        if (connection->ftp) {
            uint8_t greeting[128];
            int     greeting_length = vserver_ftp_greeting(greeting, sizeof(greeting));

            if (greeting_length < 0 || greeting_length > TCP_TX_BUFFER - connection->tx_len) {
                tcp_close_connection(server, connection);
                return;
            }
            memcpy(connection->tx + connection->tx_len, greeting, (size_t) greeting_length);
            connection->tx_len += greeting_length;
        } else if (connection->ftp_control) {
            if (connection->ftp_active_data)
                tcp_send(server, ip, destination, source, connection->snd_una, connection->rcv_nxt,
                         TCP_ACK, NULL, 0, false);
            tcp_ftp_start_transfer(server, connection->ftp_control);
        }
    }
#endif

    if (connection->state == TCP_ESTABLISHED && payload > 0) {
        if (seq == connection->rcv_nxt) {
            uint8_t out[SMB_MAX_OUTPUT];
            int     written;

            connection->rcv_nxt += (uint32_t) payload;
            if (connection->tunnel) {
                /* Its replies go straight into the buffer. */
                written = 0;
                if (!server->tunnel->control_input(server->tunnel_priv, tcp + header, payload)) {
                    tcp_flush(server, connection);
                    tcp_start_close(server, connection);
                    return;
                }
#ifdef ENABLE_VSERVER_FTP
            } else if (connection->ftp) {
                bool close_after_reply;

                written = vserver_ftp_input(connection->ftp, server->server_ip, connection->peer_ip,
                                            connection->ftp_passive_port, tcp + header, payload, out, sizeof(out),
                                            &close_after_reply);
                if (close_after_reply)
                    connection->close_after_tx = true;
#endif
#ifdef ENABLE_VSERVER_FTP
            } else if (connection->ftp_control) {
                written = 0;
#endif
            } else {
                written = smb_session_input(connection->session, tcp + header, payload, out, sizeof(out));
            }
            if (written < 0 || written > TCP_TX_BUFFER - connection->tx_len) {
                tcp_send(server, ip, destination, source, connection->snd_una, connection->rcv_nxt,
                         TCP_RST | TCP_ACK, NULL, 0, false);
                tcp_close_connection(server, connection);
                return;
            }
            memcpy(connection->tx + connection->tx_len, out, (size_t) written);
            connection->tx_len += written;
#ifdef ENABLE_VSERVER_FTP
#endif
        }
        if (connection->tx_sent == connection->tx_len)
            tcp_send(server, ip, destination, source, connection->snd_una + (uint32_t) connection->tx_len,
                     connection->rcv_nxt, TCP_ACK, NULL, 0, false);
    }

    tcp_flush(server, connection);

#ifdef ENABLE_VSERVER_FTP
    if (connection->ftp) {
        tcp_ftp_start_transfer(server, connection);
        tcp_ftp_start_active(server, connection);
    }
    if (connection->close_after_tx && connection->state == TCP_ESTABLISHED && !connection->tx_len) {
        connection->close_after_tx = false;
        tcp_start_close(server, connection);
    }
#endif

    if ((flags & TCP_FIN) && connection->state == TCP_ESTABLISHED
        && seq + (uint32_t) payload == connection->rcv_nxt) {
        connection->rcv_nxt++;
        tcp_start_close(server, connection);
    } else if ((flags & TCP_FIN) && connection->state == TCP_CLOSING) {
        tcp_send(server, ip, destination, source, connection->snd_una + (uint32_t) connection->tx_len + 1,
                 connection->rcv_nxt, TCP_ACK, NULL, 0, false);
    }
}

static void
tcp_tick(vserver_t *server)
{
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_connection_t *connection = &server->connections[i];
        bool              waiting    = connection->state == TCP_SYN_SENT || connection->state == TCP_SYN_RECEIVED
                       || connection->tx_sent > 0 || connection->state == TCP_CLOSING;

        if (connection->state == TCP_CLOSED || !waiting)
            continue;
        if (++connection->retransmit_ms < TCP_RETRANSMIT_MS)
            continue;
        connection->retransmit_ms = 0;
        if (++connection->retries > TCP_MAX_RETRIES) {
            vserver_log(server->log, "VSERVER: Giving up on a TCP connection\n");
            tcp_close_connection(server, connection);
            continue;
        }

        if (connection->state == TCP_SYN_SENT) {
            tcp_send(server, connection->peer_ip, connection->port, connection->peer_port, connection->snd_una,
                     connection->rcv_nxt, TCP_SYN, NULL, 0, true);
        } else if (connection->state == TCP_SYN_RECEIVED) {
            tcp_send(server, connection->peer_ip, connection->port, connection->peer_port, connection->snd_una,
                     connection->rcv_nxt, TCP_SYN | TCP_ACK, NULL, 0, true);
        } else if (connection->tx_sent > 0) {
            connection->tx_sent = 0;
            tcp_flush(server, connection);
        } else {
            tcp_send(server, connection->peer_ip, connection->port, connection->peer_port,
                     connection->snd_una, connection->rcv_nxt, TCP_FIN | TCP_ACK, NULL, 0, false);
        }
    }
}

static void
vs_echo_reply(vserver_t *server, uint32_t destination, const uint8_t *icmp, int length)
{
    uint8_t packet[IP_HEADER_LENGTH + 1480];

    if (length < 8 || length > 1480)
        return;
    memcpy(packet + IP_HEADER_LENGTH, icmp, (size_t) length);
    packet[IP_HEADER_LENGTH]     = 0;
    packet[IP_HEADER_LENGTH + 2] = 0;
    packet[IP_HEADER_LENGTH + 3] = 0;
    vs_put16(packet + IP_HEADER_LENGTH + 2, vs_checksum(0, packet + IP_HEADER_LENGTH, length));
    vs_send_ip(server, packet, IP_PROTOCOL_ICMP, destination, length);
}

/* Treat the guest's broadcast queries as belonging to this virtual server. */
static bool
vs_is_broadcast(const vserver_t *server, uint32_t address)
{
    (void) server;
    return (address & 0xFFu) == 0xFFu; /* limited, subnet or class A, as the guest's mask has it */
}

static void
vs_browser_input(vserver_t *server, uint32_t source, uint16_t port, const uint8_t *data, int length)
{
    uint8_t reply[1024];
    int     reply_length = browser_process(server->server_ip, data, length, reply, sizeof(reply));

    if (reply_length > 0)
        vs_send_udp(server, source, BROWSER_PORT, port, reply, reply_length);
}

bool
vserver_process_ipv4(vserver_t *server, const uint8_t *ip_pkt, int len)
{
    uint8_t  reply[NBNS_MAX_REPLY];
    int      header;
    int      total;
    uint8_t  protocol;
    uint32_t source;
    uint32_t destination;
    int      result;

    if (!server || !ip_pkt)
        return false;

    if (server->wins && server->nbns) {
        result = nbns_process_ipv4(server->nbns, ip_pkt, len, reply, sizeof(reply));
        if (result >= 0) {
            if (result > 0)
                server->output(server->priv, reply, result);
            if (len >= 16)
                server->guest_ip = vs_get32(ip_pkt + 12);
            return true;
        }
    }

    if (len < IP_HEADER_LENGTH || (ip_pkt[0] >> 4) != 4)
        return false;
    header = (ip_pkt[0] & 0x0F) * 4;
    total  = vs_get16(ip_pkt + 2);
    if (header < IP_HEADER_LENGTH || total < header || total > len
        || (vs_get16(ip_pkt + 6) & 0x3FFF) != 0)
        return false;

    protocol    = ip_pkt[9];
    source      = vs_get32(ip_pkt + 12);
    destination = vs_get32(ip_pkt + 16);

    /* A hybrid node falls back to broadcasting after a negative WINS reply. Those would
       otherwise leave through SLiRP, onto the host's network. */
    if (protocol == IP_PROTOCOL_UDP && total - header >= UDP_HEADER_LENGTH
        && vs_get16(ip_pkt + header + 2) == NBNS_PORT && vs_is_broadcast(server, destination)) {
        if (server->wins && server->answer_broadcasts) {
            result = nbns_process_broadcast(server->nbns, ip_pkt, len, reply, sizeof(reply));
            if (result > 0)
                server->output(server->priv, reply, result);
        }
        if (server->wins)
            return true;
    }

    if (server->smb && protocol == IP_PROTOCOL_UDP && total - header >= UDP_HEADER_LENGTH
        && vs_get16(ip_pkt + header + 2) == BROWSER_PORT
        && (destination == server->server_ip || vs_is_broadcast(server, destination))) {
        server->guest_ip = source;
        vs_browser_input(server, source, vs_get16(ip_pkt + header), ip_pkt + header + UDP_HEADER_LENGTH,
                         total - header - UDP_HEADER_LENGTH);
        return true;
    }

    if (destination != server->server_ip)
        return false;

    if (!server->smb && !server->ftp && !server->tftp && !server->tunnel)
        return false;

#ifdef ENABLE_VSERVER_TFTP
    if (protocol == IP_PROTOCOL_UDP && server->tftp && total - header >= UDP_HEADER_LENGTH) {
        const uint8_t *udp = ip_pkt + header;
        uint16_t       udp_length = vs_get16(udp + 4);

        if (udp_length >= UDP_HEADER_LENGTH && udp_length <= total - header
            && vserver_tftp_input(server->tftp_server, source, vs_get16(udp), vs_get16(udp + 2),
                                  udp + UDP_HEADER_LENGTH, udp_length - UDP_HEADER_LENGTH))
            return true;
    }
#endif

    if (protocol == IP_PROTOCOL_TCP) {
        tcp_input(server, source, ip_pkt + header, total - header);
    } else if (protocol == IP_PROTOCOL_GRE && server->tunnel) {
        server->tunnel->gre_input(server->tunnel_priv, ip_pkt + header, total - header);
    } else if (protocol == IP_PROTOCOL_ICMP && total - header >= 8
               && ip_pkt[header] == ICMP_ECHO_REQUEST) {
        vs_echo_reply(server, source, ip_pkt + header, total - header);
    }
    return true;
}

void
vserver_answer_broadcasts(vserver_t *server)
{
    if (server)
        server->answer_broadcasts = true;
}

void
vserver_limit_segment(vserver_t *server, int mss)
{
    if (server && mss < server->max_mss)
        server->max_mss = (uint16_t) mss;
}

void
vserver_set_tunnel(vserver_t *server, const vserver_tunnel_t *tunnel, uint16_t port, void *priv)
{
    server->tunnel      = tunnel;
    server->tunnel_port = port;
    server->tunnel_priv = priv;
}

void
vserver_tunnel_send_control(vserver_t *server, const uint8_t *data, int len)
{
    tcp_connection_t *connection = tcp_tunnel_connection(server);

    if (!connection || len > TCP_TX_BUFFER - connection->tx_len)
        return;
    memcpy(connection->tx + connection->tx_len, data, (size_t) len);
    connection->tx_len += len;
    tcp_flush(server, connection);
}

void
vserver_tunnel_send_gre(vserver_t *server, const uint8_t *data, int len)
{
    uint8_t packet[IP_HEADER_LENGTH + VSERVER_MAX_GRE];

    if (len > VSERVER_MAX_GRE || !server->tunnel_peer)
        return;
    memcpy(packet + IP_HEADER_LENGTH, data, (size_t) len);
    vs_send_ip(server, packet, IP_PROTOCOL_GRE, server->tunnel_peer, len);
}

void
vserver_tick(vserver_t *server)
{
    if (!server)
        return;
    if (server->wins && server->nbns)
        nbns_tick(server->nbns);
    if (server->smb || server->ftp)
        tcp_tick(server);
#ifdef ENABLE_VSERVER_TFTP
    if (server->tftp)
        vserver_tftp_tick(server->tftp_server);
#endif
    if (!server->smb)
        return;
    if (server->tunnel)
        server->tunnel->tick(server->tunnel_priv);

    if (server->guest_ip && ++server->announce_ms >= ANNOUNCE_PERIOD_MS) {
        uint8_t reply[1024];

        server->announce_ms = 0;
        for (int which = 0; which < 2; which++) {
            int length = browser_announcement(server->server_ip, which, reply, sizeof(reply));

            if (length > 0)
                vs_send_udp(server, server->guest_ip, BROWSER_PORT, BROWSER_PORT, reply, length);
        }
    }
}

vserver_t *
vserver_init(void *log, uint32_t server_ip, bool wins, bool smb, bool ftp, bool tftp, vserver_output_t output,
             void *priv)
{
    vserver_t *server = (vserver_t *) calloc(1, sizeof(vserver_t));

    if (!server)
        return NULL;
    server->log         = log;
    server->server_ip   = server_ip;
    server->wins        = wins || smb;
    server->smb         = smb;
    server->ftp         = ftp;
    server->tftp        = tftp;
    server->output      = output;
    server->priv        = priv;
    server->iss         = 0x10000000u;
    server->max_mss     = TCP_OUR_MSS;
    server->announce_ms = ANNOUNCE_PERIOD_MS - ANNOUNCE_FIRST_MS;
    if (server->wins) {
        server->nbns = nbns_init(log, server_ip);
        if (!server->nbns) {
            free(server);
            return NULL;
        }
    }
    if (server->smb)
        nbns_add_browser_names(server->nbns);
#ifdef ENABLE_VSERVER_TFTP
    if (server->tftp) {
        server->tftp_server = vserver_tftp_create(vs_tftp_output, server);
        if (!server->tftp_server) {
            nbns_close(server->nbns);
            free(server);
            return NULL;
        }
    }
#endif
    return server;
}

void
vserver_close(vserver_t *server)
{
    if (!server)
        return;
    if (server->tunnel)
        server->tunnel->close(server->tunnel_priv);
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        smb_session_close(server->connections[i].session);
#ifdef ENABLE_VSERVER_FTP
        vserver_ftp_session_close(server->connections[i].ftp);
#endif
    }
#ifdef ENABLE_VSERVER_TFTP
    vserver_tftp_close(server->tftp_server);
#endif
    nbns_close(server->nbns);
    free(server);
}
