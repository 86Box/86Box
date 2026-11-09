/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handle SLiRP library processing.
 *
 *          Some of the code was borrowed from libvdeslirp
 *          <https://github.com/virtualsquare/libvdeslirp>
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/machine.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/video.h>
#include <86box/bswap.h>

#define _SSIZE_T_DEFINED
#include <slirp/libslirp.h>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <ws2tcpip.h>
#else
#    include <poll.h>
#    include <signal.h>
#endif
#include <86box/net_event.h>
#include <86box/network/net_vserver_config.h>
#include "net_slirp_dhcp.h"
#ifdef ENABLE_VSERVER_RARP
#include "net_slirp_rarp.h"
#endif
#ifdef ENABLE_VSERVER
#    include <stdbool.h>
#    include <86box/log.h>
#    include <86box/network/net_vserver_eth.h>
#    ifdef ENABLE_VSERVER_PPTP
#        include <86box/network/net_vserver.h>
#        include <86box/modem/modem_pptp.h>
#    endif
#endif
#ifdef ENABLE_VSERVER_WINS
#    include <86box/network/net_nbns.h>
#endif

#define SLIRP_PKT_BATCH NET_QUEUE_LEN

enum {
    NET_EVENT_STOP = 0,
    NET_EVENT_TX,
    NET_EVENT_RX,
    NET_EVENT_MAX
};

typedef struct net_slirp_t {
    Slirp *        slirp;
    uint8_t        mac_addr[6];
    netcard_t *    card; /* netcard attached to us */
    thread_t *     poll_tid;
    net_evt_t      rx_event;
    net_evt_t      tx_event;
    net_evt_t      stop_event;
    netpkt_t       pkt;
    netpkt_t       pkt_tx_v[SLIRP_PKT_BATCH];
    int            during_tx;
    int            recv_on_tx;
#ifdef ENABLE_VSERVER
    vserver_eth_t *vserver;    /* the virtual server, for network cards */
    void          *vserver_log;
    uint32_t       vserver_ip;
    int64_t        vserver_ms; /* how far its clock has run */
#endif
    uint32_t       pfd_len;
    uint32_t       pfd_size;
#ifdef _WIN32
    HANDLE         sock_event;
    struct {
        slirp_os_socket fd;
        int             events;
    }             *pfd;
#else
    struct pollfd *pfd;
#endif
} net_slirp_t;

static bool net_slirp_link_up(const netcard_t *card);

/* Pulled off from libslirp code. This is only needed for modem. */
#pragma pack(push, 1)
struct arphdr_local {
    unsigned char h_dest[6]; /* destination eth addr */
    unsigned char h_source[6]; /* source ether addr    */
    unsigned short h_proto; /* packet type ID field */

    unsigned short ar_hrd; /* format of hardware address */
    unsigned short ar_pro; /* format of protocol address */
    unsigned char ar_hln; /* length of hardware address */
    unsigned char ar_pln; /* length of protocol address */
    unsigned short ar_op; /* ARP opcode (command)       */

    /*
     *  Ethernet looks like this : This bit is variable sized however...
     */
    uint8_t ar_sha[6]; /* sender hardware address */
    uint32_t ar_sip; /* sender IP address       */
    uint8_t ar_tha[6]; /* target hardware address */
    uint32_t ar_tip; /* target IP address       */
};
#pragma pack(pop)

#ifdef ENABLE_SLIRP_LOG
int slirp_do_log = ENABLE_SLIRP_LOG;

static void
slirp_log(const char *fmt, ...)
{
    va_list ap;

    if (slirp_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define slirp_log(fmt, ...)
#endif

#ifdef ENABLE_SLIRP_LOG
static uint16_t
slirp_read_be16(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8) | data[1]);
}

static const char *
slirp_ethertype_name(uint16_t ethertype)
{
    switch (ethertype) {
        case 0x0800: return "IPv4";
        case 0x0806: return "ARP";
        case 0x86DD: return "IPv6";
        default:     return "other";
    }
}

static const char *
slirp_ipv4_protocol_name(uint8_t protocol)
{
    switch (protocol) {
        case 1:   return "ICMP";
        case 2:   return "IGMP";
        case 4:   return "IPv4 encapsulation";
        case 6:   return "TCP";
        case 17:  return "UDP";
        case 41:  return "IPv6 encapsulation";
        case 47:  return "GRE";
        case 50:  return "ESP";
        case 51:  return "AH";
        case 58:  return "ICMPv6";
        case 89:  return "OSPF";
        case 132: return "SCTP";
        default:  return "Unknown";
    }
}

static const char *
slirp_ipv4_port_name(uint16_t port)
{
    switch (port) {
        case 20:  return "FTP-data";
        case 21:  return "FTP-control";
        case 22:  return "SSH";
        case 23:  return "Telnet";
        case 25:  return "SMTP";
        case 53:  return "DNS";
        case 67:  return "DHCP-server";
        case 68:  return "DHCP-client";
        case 80:  return "HTTP";
        case 110: return "POP3";
        case 123: return "NTP";
        case 137: return "NetBIOS-NS";
        case 138: return "NetBIOS-DGM";
        case 139: return "NetBIOS-SSN";
        case 143: return "IMAP";
        case 161: return "SNMP";
        case 443: return "HTTPS";
        case 445: return "SMB";
        default:  return NULL;
    }
}

static const char *
slirp_icmp_type_name(uint8_t type)
{
    switch (type) {
        case 0:  return "Echo Reply";
        case 3:  return "Destination Unreachable";
        case 8:  return "Echo Request";
        case 10: return "Router Solicitation";
        default: return "Unknown";
    }
}

static const char *
slirp_icmp_code_name(uint8_t type, uint8_t code)
{
    if ((type == 0 || type == 8 || type == 10) && code == 0)
        return "No additional code";
    if (type == 3 && code == 3)
        return "Port Unreachable";

    return NULL;
}

static const char *
slirp_arp_operation_name(uint16_t operation)
{
    switch (operation) {
        case 1:  return "Request";
        case 2:  return "Reply";
        default: return "Unknown";
    }
}

static void
slirp_log_dns(const char *direction, const uint8_t *dns, size_t dns_len)
{
    if (dns_len < 12) {
        slirp_log("SLiRP: %s DNS malformed header bytes=%u\n",
                  direction, (unsigned) dns_len);
        return;
    }

    uint16_t flags = slirp_read_be16(dns + 2);
    uint16_t qdcount = slirp_read_be16(dns + 4);
    uint16_t ancount = slirp_read_be16(dns + 6);
    slirp_log("SLiRP: %s DNS %s id=0x%04X opcode=%u rcode=%u questions=%u answers=%u bytes=%u\n",
              direction, (flags & 0x8000) ? "response" : "query",
              (unsigned) slirp_read_be16(dns), (unsigned) ((flags >> 11) & 0x0F),
              (unsigned) (flags & 0x0F), (unsigned) qdcount, (unsigned) ancount,
              (unsigned) dns_len);

    if (qdcount > 0) {
        char   name[256];
        size_t pos = 12;
        size_t name_len = 0;
        bool   complete = false;

        while (pos < dns_len) {
            uint8_t label_len = dns[pos++];
            if (label_len == 0) {
                complete = true;
                break;
            }
            if ((label_len & 0xC0) != 0 || label_len > dns_len - pos)
                break;
            if (name_len && name_len < sizeof(name) - 1)
                name[name_len++] = '.';
            for (uint8_t i = 0; i < label_len && name_len < sizeof(name) - 1; i++) {
                uint8_t c = dns[pos++];
                name[name_len++] = (c >= 33 && c <= 126 && c != '.') ? (char) c : '?';
            }
        }

        name[name_len] = '\0';
        if (complete && pos + 4 <= dns_len) {
            slirp_log("SLiRP: %s DNS question=%s type=%u class=%u\n",
                      direction, name, (unsigned) slirp_read_be16(dns + pos),
                      (unsigned) slirp_read_be16(dns + pos + 2));
        } else {
            slirp_log("SLiRP: %s DNS question malformed or compressed\n", direction);
        }
    }
}

static void
slirp_log_packet_summary(const char *direction, const uint8_t *packet, size_t packet_len)
{
    if (!packet || packet_len < 14) {
        slirp_log("SLiRP: %s short Ethernet frame bytes=%u\n",
                  direction, (unsigned) packet_len);
        return;
    }

    uint16_t ethertype = slirp_read_be16(packet + 12);
    slirp_log("SLiRP: %s Ethernet type=%s (0x%04X) bytes=%u\n",
              direction, slirp_ethertype_name(ethertype), (unsigned) ethertype,
              (unsigned) packet_len);

    if (ethertype == 0x0806 && packet_len >= 42) {
        const uint8_t *arp = packet + 14;
        if (arp[4] == 6 && arp[5] == 4 && slirp_read_be16(arp + 2) == 0x0800) {
            uint16_t operation = slirp_read_be16(arp + 6);
            slirp_log("SLiRP: %s ARP op=%s (%u) %u.%u.%u.%u -> %u.%u.%u.%u\n",
                      direction, slirp_arp_operation_name(operation), (unsigned) operation,
                      (unsigned) arp[14], (unsigned) arp[15], (unsigned) arp[16], (unsigned) arp[17],
                      (unsigned) arp[24], (unsigned) arp[25], (unsigned) arp[26], (unsigned) arp[27]);
        }
        return;
    }

    if (ethertype != 0x0800 || packet_len < 34)
        return;

    const uint8_t *ip = packet + 14;
    size_t         ip_available = packet_len - 14;
    size_t         ip_header_len = (size_t) (ip[0] & 0x0F) * 4;
    uint16_t       ip_total_len = slirp_read_be16(ip + 2);
    if ((ip[0] >> 4) != 4 || ip_header_len < 20 || ip_header_len > ip_available) {
        slirp_log("SLiRP: %s malformed IPv4 header bytes=%u\n",
                  direction, (unsigned) ip_available);
        return;
    }

    slirp_log("SLiRP: %s IPv4 %u.%u.%u.%u -> %u.%u.%u.%u protocol=%s (%u) total=%u\n",
              direction,
              (unsigned) ip[12], (unsigned) ip[13], (unsigned) ip[14], (unsigned) ip[15],
              (unsigned) ip[16], (unsigned) ip[17], (unsigned) ip[18], (unsigned) ip[19],
              slirp_ipv4_protocol_name(ip[9]), (unsigned) ip[9], (unsigned) ip_total_len);

    size_t ip_len = ip_total_len < ip_available ? ip_total_len : ip_available;
    if (ip[9] == 1 && ip_len >= ip_header_len + 2) {
        uint8_t type = ip[ip_header_len];
        uint8_t code = ip[ip_header_len + 1];
        const char *code_name = slirp_icmp_code_name(type, code);
        slirp_log("SLiRP: %s ICMP type=%u (%s) code=%u%s%s%s\n", direction,
                  (unsigned) type, slirp_icmp_type_name(type), (unsigned) code,
                  code_name ? " (" : "", code_name ? code_name : "",
                  code_name ? ")" : "");
    } else if (ip[9] == 17 && ip_len >= ip_header_len + 8) {
        const uint8_t *udp = ip + ip_header_len;
        uint16_t       source_port = slirp_read_be16(udp);
        uint16_t       dest_port = slirp_read_be16(udp + 2);
        uint16_t       udp_len = slirp_read_be16(udp + 4);
        const char    *source_name = slirp_ipv4_port_name(source_port);
        const char    *dest_name = slirp_ipv4_port_name(dest_port);
        slirp_log("SLiRP: %s UDP %u%s%s%s -> %u%s%s%s length=%u\n", direction,
              (unsigned) source_port, source_name ? " (" : "",
              source_name ? source_name : "", source_name ? ")" : "",
              (unsigned) dest_port, dest_name ? " (" : "",
              dest_name ? dest_name : "", dest_name ? ")" : "", (unsigned) udp_len);

        if ((source_port == 53 || dest_port == 53) && udp_len >= 20
            && udp_len <= ip_len - ip_header_len) {
            slirp_log_dns(direction, udp + 8, udp_len - 8);
        }
    } else if (ip[9] == 6 && ip_len >= ip_header_len + 20) {
        const uint8_t *tcp = ip + ip_header_len;
        slirp_log("SLiRP: %s TCP %u -> %u flags=0x%02X\n", direction,
                  (unsigned) slirp_read_be16(tcp), (unsigned) slirp_read_be16(tcp + 2),
                  (unsigned) tcp[13]);
    }
}
#else
#    define slirp_log_packet_summary(direction, packet, packet_len) ((void) 0)
#endif

static void
net_slirp_guest_error(UNUSED(const char *msg), UNUSED(void *opaque))
{
    slirp_log("SLiRP: guest_error(): %s\n", msg);
}

static int64_t
net_slirp_clock_get_ns(UNUSED(void *opaque))
{
    return (int64_t) ((double) tsc / cpuclock * 1000000000.0);
}

static void *
net_slirp_timer_new(SlirpTimerCb cb, void *cb_opaque, UNUSED(void *opaque))
{
    pc_timer_t *timer = calloc(1, sizeof(pc_timer_t));
    timer_add(timer, cb, cb_opaque, 0);
    return timer;
}

static void
net_slirp_timer_free(void *timer, UNUSED(void *opaque))
{
    timer_stop(timer);
    free(timer);
}

static void
net_slirp_timer_mod(void *timer, int64_t expire_timer, UNUSED(void *opaque))
{
    timer_on_auto(timer, expire_timer * 1000);
}

static void
#if SLIRP_CHECK_VERSION(4, 9, 0)
net_slirp_register_poll_socket(slirp_os_socket fd, void *opaque)
#else
net_slirp_register_poll_fd(int fd, void *opaque)
#endif
{
    (void) fd;
    (void) opaque;
}

static void
#if SLIRP_CHECK_VERSION(4, 9, 0)
net_slirp_unregister_poll_socket(slirp_os_socket fd, void *opaque)
#else
net_slirp_unregister_poll_fd(int fd, void *opaque)
#endif
{
    (void) fd;
    (void) opaque;
}

static void
net_slirp_notify(void *opaque)
{
    (void) opaque;
}

#if SLIRP_CHECK_VERSION(4, 8, 0)
slirp_ssize_t
#else
ssize_t
#endif
net_slirp_send_packet(const void *qp, size_t pkt_len, void *opaque)
{
    net_slirp_t *slirp = (net_slirp_t *) opaque;
    size_t       frame_len = pkt_len;

    if (pkt_len > NET_MAX_FRAME)
        return -1;

    memcpy(slirp->pkt.data, (uint8_t *) qp, pkt_len);
#if defined(ENABLE_VSERVER_WINS) || defined(ENABLE_VSERVER_TFTP)
    if (slirp->vserver)
        net_slirp_dhcp_add_vserver_options(slirp->pkt.data, &frame_len, NET_MAX_FRAME, slirp->vserver_ip,
#    ifdef ENABLE_VSERVER_WINS
                                           true,
#    else
                                           false,
#    endif
#    ifdef ENABLE_VSERVER_TFTP
                                           true, VSERVER_TFTP_BOOT_FILE_NAME);
#    else
                                           false, NULL);
#    endif
#endif
    slirp_log_packet_summary("from SLiRP", slirp->pkt.data, frame_len);
    slirp->pkt.len = frame_len;

    if (!(net_cards_conf[slirp->card->card_num].link_state & NET_LINK_DOWN)) {
        if (slirp->during_tx || slirp->recv_on_tx) {
            network_rx_on_tx_put_pkt(slirp->card, &slirp->pkt);
            slirp->recv_on_tx = 1;
        } else
            network_rx_put_pkt(slirp->card, &slirp->pkt);
    }

    return pkt_len;
}

#ifdef _WIN32
static int
#    if SLIRP_CHECK_VERSION(4, 9, 0)
net_slirp_add_poll(slirp_os_socket fd, int events, void *opaque)
#    else
net_slirp_add_poll(int fd, int events, void *opaque)
#    endif
{
    net_slirp_t *slirp   = (net_slirp_t *) opaque;
    long         bitmask = 0;

    if (slirp->pfd_len >= slirp->pfd_size) {
        uint32_t newsize = slirp->pfd_size + 16;
        void    *new     = realloc(slirp->pfd, newsize * sizeof(*slirp->pfd));
        if (!new)
            return -1;
        slirp->pfd      = new;
        slirp->pfd_size = newsize;
    }
    int idx                = slirp->pfd_len++;
    slirp->pfd[idx].fd     = fd;
    slirp->pfd[idx].events = events;

    if (events & SLIRP_POLL_IN)
        bitmask |= FD_READ | FD_ACCEPT;
    if (events & SLIRP_POLL_OUT)
        bitmask |= FD_WRITE | FD_CONNECT;
    if (events & SLIRP_POLL_HUP)
        bitmask |= FD_CLOSE;
    if (events & SLIRP_POLL_PRI)
        bitmask |= FD_OOB;

    WSAEventSelect(fd, slirp->sock_event, bitmask);
    return idx;
}
#else
static int
#    if SLIRP_CHECK_VERSION(4, 9, 0)
net_slirp_add_poll(slirp_os_socket fd, int events, void *opaque)
#    else
net_slirp_add_poll(int fd, int events, void *opaque)
#    endif
{
    net_slirp_t *slirp = (net_slirp_t *) opaque;

    if (slirp->pfd_len >= slirp->pfd_size) {
        int newsize = slirp->pfd_size + 16;
        struct pollfd *new = realloc(slirp->pfd, newsize * sizeof(struct pollfd));
        if (new) {
            slirp->pfd = new;
            slirp->pfd_size = newsize;
        }
    }
    if ((slirp->pfd_len < slirp->pfd_size)) {
        int idx = slirp->pfd_len++;
        slirp->pfd[idx].fd = fd;
        int pevents = 0;
        if (events & SLIRP_POLL_IN)
            pevents |= POLLIN;
        if (events & SLIRP_POLL_OUT)
            pevents |= POLLOUT;
        if (events & SLIRP_POLL_ERR)
            pevents |= POLLERR;
        if (events & SLIRP_POLL_PRI)
            pevents |= POLLPRI;
        if (events & SLIRP_POLL_HUP)
            pevents |= POLLHUP;
        slirp->pfd[idx].events = pevents;
        return idx;
    } else
        return -1;
}
#endif

#ifdef _WIN32
static int
net_slirp_get_revents(int idx, void *opaque)
{
    net_slirp_t     *slirp = (net_slirp_t *) opaque;
    int              ret   = 0;
    WSANETWORKEVENTS ev;
    if (WSAEnumNetworkEvents(slirp->pfd[idx].fd, slirp->sock_event, &ev) != 0) {
        return ret;
    }

#    define WSA_TO_POLL(_wsaev, _pollev)                \
        do {                                            \
            if (ev.lNetworkEvents & (_wsaev)) {         \
                ret |= (_pollev);                       \
                if (ev.iErrorCode[_wsaev##_BIT] != 0) { \
                    ret |= SLIRP_POLL_ERR;              \
                }                                       \
            }                                           \
        } while (0)

    WSA_TO_POLL(FD_READ, SLIRP_POLL_IN);
    WSA_TO_POLL(FD_ACCEPT, SLIRP_POLL_IN);
    WSA_TO_POLL(FD_WRITE, SLIRP_POLL_OUT);
    WSA_TO_POLL(FD_CONNECT, SLIRP_POLL_OUT);
    WSA_TO_POLL(FD_OOB, SLIRP_POLL_PRI);
    WSA_TO_POLL(FD_CLOSE, SLIRP_POLL_IN);
    WSA_TO_POLL(FD_CLOSE, SLIRP_POLL_HUP);

    /* Only fall back to readable if slirp asked for it. A socket whose
       connect() is still in progress is polled for SLIRP_POLL_OUT only, and
       recv() on it fails with WSAENOTCONN, which makes slirp reset the
       guest's connection ("Connection refused"). */
    if ((ret == 0) && (slirp->pfd[idx].events & SLIRP_POLL_IN)) {
        /* TCP in slirp shrugs off a read that finds nothing, but UDP reports any failed
           recvfrom() to the guest as ICMP Port Unreachable, so only say a datagram
           socket is readable when something is actually waiting on it. */
        int     type   = SOCK_STREAM;
        int     length = sizeof(type);
        u_long  queued = 0;

        getsockopt(slirp->pfd[idx].fd, SOL_SOCKET, SO_TYPE, (char *) &type, &length);
        if ((type == SOCK_STREAM)
            || ((ioctlsocket(slirp->pfd[idx].fd, FIONREAD, &queued) == 0) && (queued > 0)))
            ret |= SLIRP_POLL_IN;
    }

    return ret;
}
#else
static int
net_slirp_get_revents(int idx, void *opaque)
{
    net_slirp_t *slirp = (net_slirp_t *) opaque;
    int ret = 0;
    int events = slirp->pfd[idx].revents;
    if (events & POLLIN)
        ret |= SLIRP_POLL_IN;
    if (events & POLLOUT)
        ret |= SLIRP_POLL_OUT;
    if (events & POLLPRI)
        ret |= SLIRP_POLL_PRI;
    if (events & POLLERR)
        ret |= SLIRP_POLL_ERR;
    if (events & POLLHUP)
        ret |= SLIRP_POLL_HUP;
    return ret;
}
#endif

static const SlirpCb slirp_cb = {
    .send_packet        = net_slirp_send_packet,
    .guest_error        = net_slirp_guest_error,
    .clock_get_ns       = net_slirp_clock_get_ns,
    .timer_new          = net_slirp_timer_new,
    .timer_free         = net_slirp_timer_free,
    .timer_mod          = net_slirp_timer_mod,
#if SLIRP_CHECK_VERSION(4, 9, 0)
    .register_poll_socket   = net_slirp_register_poll_socket,
    .unregister_poll_socket = net_slirp_unregister_poll_socket,
#else
    .register_poll_fd   = net_slirp_register_poll_fd,
    .unregister_poll_fd = net_slirp_unregister_poll_fd,
#endif
    .notify             = net_slirp_notify
};

/* Send a packet to the SLiRP interface. */
static void
net_slirp_in(net_slirp_t *slirp, uint8_t *pkt, int pkt_len)
{
    if (!slirp)
        return;

#ifdef ENABLE_VSERVER_RARP
    {
        uint8_t rarp_reply[42];
        uint8_t server_mac[6] = {
            0x52, 0x52, (uint8_t) (slirp->card->slirp_host_ip >> 24),
            (uint8_t) (slirp->card->slirp_host_ip >> 16), (uint8_t) (slirp->card->slirp_host_ip >> 8),
            (uint8_t) slirp->card->slirp_host_ip
        };
        size_t rarp_reply_length;

        if (net_slirp_rarp_reply(pkt, (size_t) pkt_len, slirp->mac_addr, slirp->card->slirp_dhcp_ip, server_mac,
                                 slirp->card->slirp_host_ip, rarp_reply, sizeof(rarp_reply), &rarp_reply_length)) {
            net_slirp_send_packet(rarp_reply, rarp_reply_length, slirp);
            return;
        }
    }
#endif

#ifdef ENABLE_VSERVER_BOOTP
    size_t frame_len = (size_t) pkt_len;
    if (net_slirp_dhcp_prepare_bootp_request(pkt, &frame_len, NET_MAX_FRAME))
        pkt_len = (int) frame_len;
#endif

#ifdef ENABLE_VSERVER
    if (vserver_eth_input(slirp->vserver, pkt, pkt_len))
        return;
#endif

    slirp_log_packet_summary("to SLiRP", pkt, (size_t) pkt_len);

    slirp_input(slirp->slirp, (const uint8_t *) pkt, pkt_len);
}

#ifdef ENABLE_VSERVER
#    define VSERVER_POLL_MS 100

static void
net_slirp_vserver_output(void *priv, const uint8_t *frame, int len)
{
    net_slirp_send_packet(frame, (size_t) len, priv);
}

/* Runs the virtual server's clock, which only needs to be roughly right. */
static void
net_slirp_vserver_tick(net_slirp_t *slirp)
{
    int64_t now;

    if (!slirp->vserver)
        return;
    now = net_slirp_clock_get_ns(NULL) / 1000000;
    if (now - slirp->vserver_ms > 1000)
        slirp->vserver_ms = now - 1000;
    for (; slirp->vserver_ms < now; slirp->vserver_ms++)
        vserver_eth_tick(slirp->vserver);
}
#endif

void
net_slirp_in_available(void *priv)
{
    net_slirp_t *slirp = (net_slirp_t *) priv;
    net_event_set(&slirp->tx_event);
}

static void
net_slirp_rx_deferred_packets(net_slirp_t *slirp)
{
    if (!slirp->recv_on_tx)
        return;

    if (net_cards_conf[slirp->card->card_num].link_state & NET_LINK_DOWN) {
        int packets;

        do {
            packets = network_rx_on_tx_popv(slirp->card, slirp->pkt_tx_v, SLIRP_PKT_BATCH);
        } while (packets > 0);
    }
    slirp->recv_on_tx = network_rx_on_tx_flush(slirp->card);
}

static bool
net_slirp_link_up(const netcard_t *card)
{
    return card->internal_modem
        || !(net_cards_conf[card->card_num].link_state & NET_LINK_DOWN);
}

#ifdef _WIN32
static void
net_slirp_thread(void *priv)
{
    net_slirp_t *slirp = (net_slirp_t *) priv;

    /* Start polling. */
    slirp_log("SLiRP: polling started.\n");

    HANDLE events[3];
    events[NET_EVENT_STOP] = net_event_get_handle(&slirp->stop_event);
    events[NET_EVENT_TX]   = net_event_get_handle(&slirp->tx_event);
    events[NET_EVENT_RX]   = slirp->sock_event;
    bool run               = true;
    while (run) {
        uint32_t timeout = -1;

        slirp->pfd_len = 0;
#    if SLIRP_CHECK_VERSION(4, 9, 0)
        slirp_pollfds_fill_socket(slirp->slirp, &timeout, net_slirp_add_poll, slirp);
#    else
        slirp_pollfds_fill(slirp->slirp, &timeout, net_slirp_add_poll, slirp);
#    endif
        if (timeout < 0)
            timeout = INFINITE;
#    ifdef ENABLE_VSERVER
        if (slirp->vserver && timeout > VSERVER_POLL_MS)
            timeout = VSERVER_POLL_MS;
#    endif

        int ret = WaitForMultipleObjects(3, events, FALSE, (DWORD) timeout);
        switch (ret - WAIT_OBJECT_0) {
            case NET_EVENT_STOP:
                run = false;
                break;

            case NET_EVENT_TX:
                {
                    slirp->during_tx = 1;
                    int packets = network_tx_popv(slirp->card, slirp->pkt_tx_v, SLIRP_PKT_BATCH);
                    if (net_slirp_link_up(slirp->card)) {
                        for (int i = 0; i < packets; i++)
                            net_slirp_in(slirp, slirp->pkt_tx_v[i].data, slirp->pkt_tx_v[i].len);
                    }
                    slirp->during_tx = 0;
                }
                break;

            default:
                slirp_pollfds_poll(slirp->slirp, ret == WAIT_FAILED, net_slirp_get_revents, slirp);
                break;
        }
#    ifdef ENABLE_VSERVER
        net_slirp_vserver_tick(slirp);
#    endif
        if (run)
            net_slirp_rx_deferred_packets(slirp);
    }

    slirp_log("SLiRP: polling stopped.\n");
}
#else
/* Handle the receiving of frames. */
static void
net_slirp_thread(void *priv)
{
    net_slirp_t *slirp = (net_slirp_t *) priv;

    /* Start polling. */
    slirp_log("SLiRP: polling started.\n");

    while (1) {
        uint32_t timeout = -1;

        slirp->pfd_len = 0;
        net_slirp_add_poll(net_event_get_fd(&slirp->stop_event), SLIRP_POLL_IN, slirp);
        net_slirp_add_poll(net_event_get_fd(&slirp->tx_event), SLIRP_POLL_IN, slirp);

#    if SLIRP_CHECK_VERSION(4, 9, 0)
        slirp_pollfds_fill_socket(slirp->slirp, &timeout, net_slirp_add_poll, slirp);
#    else
        slirp_pollfds_fill(slirp->slirp, &timeout, net_slirp_add_poll, slirp);
#    endif

#    ifdef ENABLE_VSERVER
        if (slirp->vserver && timeout > VSERVER_POLL_MS)
            timeout = VSERVER_POLL_MS;
#    endif

        int ret = poll(slirp->pfd, slirp->pfd_len, timeout);

        slirp_pollfds_poll(slirp->slirp, (ret < 0), net_slirp_get_revents, slirp);

        if (slirp->pfd[NET_EVENT_STOP].revents & POLLIN) {
            net_event_clear(&slirp->stop_event);
            break;
        }

        if (slirp->pfd[NET_EVENT_TX].revents & POLLIN) {
            net_event_clear(&slirp->tx_event);

            slirp->during_tx = 1;
            int packets = network_tx_popv(slirp->card, slirp->pkt_tx_v, SLIRP_PKT_BATCH);
            if (net_slirp_link_up(slirp->card)) {
                for (int i = 0; i < packets; i++)
                    net_slirp_in(slirp, slirp->pkt_tx_v[i].data, slirp->pkt_tx_v[i].len);
            }
            slirp->during_tx = 0;
        }
#    ifdef ENABLE_VSERVER
        net_slirp_vserver_tick(slirp);
#    endif
        net_slirp_rx_deferred_packets(slirp);
    }

    slirp_log("SLiRP: polling stopped.\n");
}
#endif

int slirp_card_num = 2;

/* Initialize SLiRP for use. */
void *
net_slirp_init(const netcard_t *card, const uint8_t *mac_addr, UNUSED(void *priv), char *netdrv_errbuf)
{
    slirp_log("SLiRP: initializing with range %d...\n", slirp_card_num);
    net_slirp_t *slirp = calloc(1, sizeof(net_slirp_t));
    memcpy(slirp->mac_addr, mac_addr, sizeof(slirp->mac_addr));
    slirp->card = (netcard_t *) card;

    slirp->pfd_size = 16;
    slirp->pfd      = calloc(slirp->pfd_size, sizeof(*slirp->pfd));

    struct in_addr net;
    struct in_addr host;
    struct in_addr dhcp;
    struct in_addr dns;

    /* Set the IP addresses to use.
       Use a configured address if set, otherwise 10.0.x.0 */
    const char *slirp_net = card->slirp_force_auto_range
                          ? "" : net_cards_conf[card->card_num].slirp_net;
    if (slirp_net[0] != '\0') {
        struct in_addr addr;
        inet_pton(AF_INET, slirp_net, &addr);
        net.s_addr = htonl(ntohl(addr.s_addr) & 0xffffff00);
        host.s_addr = htonl(ntohl(addr.s_addr) + 2);
        dhcp.s_addr = htonl(ntohl(addr.s_addr) + 15);
        dns.s_addr = htonl(ntohl(addr.s_addr) + 3);
    } else {
        net.s_addr = htonl(0x0a000000 | (slirp_card_num << 8));      /* 10.0.x.0 */
        host.s_addr = htonl(0x0a000002 | (slirp_card_num << 8));     /* 10.0.x.2 */
        dhcp.s_addr = htonl(0x0a00000f | (slirp_card_num << 8));     /* 10.0.x.15 */
        dns.s_addr = htonl(0x0a000003 | (slirp_card_num << 8));      /* 10.0.x.3 */
    }

    struct in_addr  mask       = { .s_addr = htonl(0xffffff00) };    /* 255.255.255.0 */
    struct in_addr  bind       = { .s_addr = htonl(0x00000000) };    /* 0.0.0.0 */

#ifdef ENABLE_VSERVER
    slirp->vserver_ip = (ntohl(net.s_addr) & 0xFFFFFF00u) | VSERVER_SLIRP_HOST_ID;
#endif

    /* IPv6 is only switched on for the emulated modem, where PPP's IPv6CP can carry it.
       libslirp then answers router solicitations with a router advertisement for this
       prefix, so the guest can configure an address on its own. Other cards stay IPv4-only. */
#ifdef ENABLE_MODEM_IPV6
    const char *v6_nic_name = network_card_get_internal_name(net_cards_conf[net_card_current].device_num);
    const int   modem_v6    = card->internal_modem || (v6_nic_name && !strcmp(v6_nic_name, "modem"));
#else
    const int   modem_v6    = 0;
#endif
    struct in6_addr prefix6 = { .s6_addr = { 0xfd, 0x86, 0xb0, 0x0b, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
    struct in6_addr host6;
    struct in6_addr dns6;

    prefix6.s6_addr[5] = (uint8_t) slirp_card_num;
    host6 = prefix6;
    dns6  = prefix6;
    host6.s6_addr[15] = 0x02;
    dns6.s6_addr[15]  = 0x03;

    const SlirpConfig slirp_config = {
#if SLIRP_CHECK_VERSION(4, 9, 0)
        .version = 6,
#else
        .version = 1,
#endif
        .restricted            = 0,
        .in_enabled            = 1,
        .vnetwork              = net,
        .vnetmask              = mask,
        .vhost                 = host,
        .in6_enabled           = modem_v6,
        .vprefix_addr6         = prefix6,
        .vprefix_len           = 64,
        .vhost6                = host6,
        .vhostname             = VSERVER_SLIRP_HOSTNAME,
        .tftp_server_name      = NULL,
        .tftp_path             = NULL,
        .bootfile              = NULL,
        .vdhcp_start           = dhcp,
        .vnameserver           = dns,
        .vnameserver6          = dns6,
        .vdnssearch            = NULL,
        .vdomainname           = NULL,
        .if_mtu                = 0,
        .if_mru                = 0,
        .disable_host_loopback = 0,
        .enable_emu            = 0,
#if SLIRP_CHECK_VERSION(4, 9, 0)
        .outbound_addr         = NULL,
        .outbound_addr6        = NULL,
        .disable_dns           = 0,
        .disable_dhcp          = 0,
        .mfr_id                = 0,
        .oob_eth_addr          = { 0, 0, 0, 0, 0, 0 }
#endif
    };

#ifndef _WIN32
    /* libslirp writes to host sockets without MSG_NOSIGNAL, so a host peer
       closing a forwarded connection mid-transfer raises SIGPIPE, whose
       default action kills the emulator. */
    signal(SIGPIPE, SIG_IGN);
#endif

    /* Initialize SLiRP. */
    slirp->slirp = slirp_new(&slirp_config, &slirp_cb, slirp);
    if (!slirp->slirp) {
        slirp_log("SLiRP: initialization failed\n");
        snprintf(netdrv_errbuf, NET_DRV_ERRBUF_SIZE, "SLiRP initialization failed");
        free(slirp);
        return NULL;
    }

    /* Keep the effective allocation available to devices using this network. */
    ((netcard_t *) card)->slirp_host_ip = ntohl(host.s_addr);
    ((netcard_t *) card)->slirp_dhcp_ip = ntohl(dhcp.s_addr);
    ((netcard_t *) card)->slirp_dns_ip = ntohl(dns.s_addr);

    /* Set up port forwarding. */
    int  udp;
    int  i = 0;
    int  external;
    int  internal;
    char category[32];
    snprintf(category, sizeof(category), "SLiRP Port Forwarding #%d", card->card_num + 1);
    char key[20];
    while (1) {
        sprintf(key, "%d_protocol", i);
        udp = strcmp(config_get_string(category, key, "tcp"), "udp") == 0;
        sprintf(key, "%d_external", i);
        external = config_get_int(category, key, 0);
        sprintf(key, "%d_internal", i);
        internal = config_get_int(category, key, 0);
        if ((external <= 0) && (internal <= 0))
            break;
        else if (internal <= 0)
            internal = external;
        else if (external <= 0)
            external = internal;

        if (slirp_add_hostfwd(slirp->slirp, udp, bind, external, dhcp, internal) == 0)
            pclog("SLiRP: Forwarded %s port external:%d to internal:%d\n", udp ? "UDP" : "TCP", external, internal);
        else
            pclog("SLiRP: Failed to forward %s port external:%d to internal:%d\n", udp ? "UDP" : "TCP", external, internal);

        i++;
    }

    for (int i = 0; i < SLIRP_PKT_BATCH; i++) {
        slirp->pkt_tx_v[i].data = calloc(1, NET_MAX_FRAME);
    }
    slirp->pkt.data = calloc(1, NET_MAX_FRAME);
    net_event_init(&slirp->rx_event);
    net_event_init(&slirp->tx_event);
    net_event_init(&slirp->stop_event);
#ifdef _WIN32
    slirp->sock_event = CreateEvent(NULL, FALSE, FALSE, NULL);
#endif

    const char *nic_name = network_card_get_internal_name(net_cards_conf[net_card_current].device_num);
    if (!strcmp(nic_name, "modem") || !strcmp(nic_name, "plip")) {
        /* Send a gratuitous ARP here to make SLiRP work properly with SLIP/PLIP connections. */
        struct arphdr_local arphdr;
        /* ARP part. */
        arphdr.ar_hrd = htons(1);
        arphdr.ar_pro = htons(0x0800);
        arphdr.ar_hln = 6;
        arphdr.ar_pln = 4;
        arphdr.ar_op  = htons(1);
        memcpy(&arphdr.ar_sha, mac_addr, 6);
        memcpy(&arphdr.ar_tha, mac_addr, 6);
        arphdr.ar_sip = dhcp.s_addr;
        arphdr.ar_tip = dhcp.s_addr;

        /* Ethernet header part. */
        arphdr.h_proto = htons(0x0806);
        memset(arphdr.h_dest, 0xff, 6);
        memset(arphdr.h_source, 0x52, 6);
        AS_U32(arphdr.h_source[2]) = host.s_addr;
        slirp_input(slirp->slirp, (const uint8_t *) &arphdr, sizeof(struct arphdr_local));
    }

#ifdef ENABLE_VSERVER
    /* The modem brings its own server, and carries it over PPP rather than Ethernet. */
    if (!card->internal_modem) {
        slirp->vserver_log = log_open("VSERVER");
        slirp->vserver = vserver_eth_init(slirp->vserver_log, slirp->vserver_ip,
#    ifdef ENABLE_VSERVER_WINS
                                          true,
#    else
                                          false,
#    endif
#    ifdef ENABLE_VSERVER_SMB
                                          true,
#    else
                                          false,
#    endif
#    ifdef ENABLE_VSERVER_FTP
                                          true,
#    else
                                          false,
#    endif
#    ifdef ENABLE_VSERVER_TFTP
                                          true,
#    else
                                          false,
#    endif
#    ifdef ENABLE_VSERVER_NETBEUI
                                          true,
#    else
                                          false,
#    endif
#    ifdef ENABLE_VSERVER_IPX
                                          true,
#    else
                                          false,
#    endif
                                          net_slirp_vserver_output, slirp);
#    ifdef ENABLE_VSERVER_PPTP
        pptp_attach(vserver_eth_server(slirp->vserver), slirp->vserver_log,
#        ifdef ENABLE_VSERVER_NETBEUI
                    true,
#        else
                    false,
#        endif
#        ifdef ENABLE_VSERVER_IPX
                    true);
#        else
                    false);
#        endif
#    endif
        slirp->vserver_ms = net_slirp_clock_get_ns(NULL) / 1000000;
    }
#endif

    slirp_log("SLiRP: creating thread...\n");
    slirp->poll_tid = thread_create(net_slirp_thread, slirp);

    slirp_card_num++;
    return slirp;
}

void
net_slirp_close(void *priv)
{
    if (!priv)
        return;

    net_slirp_t *slirp = (net_slirp_t *) priv;

    slirp_log("SLiRP: closing\n");
    /* Tell the polling thread to shut down. */
    net_event_set(&slirp->stop_event);

    /* Wait for the thread to finish. */
    slirp_log("SLiRP: waiting for thread to end...\n");
    thread_wait(slirp->poll_tid);

    net_event_close(&slirp->stop_event);
    net_event_close(&slirp->tx_event);
    net_event_close(&slirp->rx_event);
#ifdef ENABLE_VSERVER
    vserver_eth_close(slirp->vserver);
    if (slirp->vserver_log)
        log_close(slirp->vserver_log);
#endif
    slirp_cleanup(slirp->slirp);
    for (int i = 0; i < SLIRP_PKT_BATCH; i++) {
        free(slirp->pkt_tx_v[i].data);
    }
    free(slirp->pkt.data);
    free(slirp->pfd);
    free(slirp);
}

const netdrv_t net_slirp_drv = {
    .notify_in = &net_slirp_in_available,
    .init      = &net_slirp_init,
    .close     = &net_slirp_close,
	.priv      = NULL
};
