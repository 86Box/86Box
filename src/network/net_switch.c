/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Network Switch network driver.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2026 RichardG.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <wchar.h>
#include <fcntl.h>
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
#    include <iphlpapi.h>
#    define IFF_POINTOPOINT IFF_POINTTOPOINT
#else
#    include <unistd.h>
#    include <poll.h>
#    include <sys/types.h>
#    include <sys/socket.h>
#    include <netinet/in.h>
#    include <arpa/inet.h>
#    include <ifaddrs.h>
#    include <net/if.h>
#endif
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
#include <86box/net_event.h>
#include <86box/bswap.h>

#define SWITCH_PKT_BATCH NET_QUEUE_LEN

#define SWITCH_MULTICAST_GROUP 0xefff5656 /* 239.255.86.86 */
#define SWITCH_MULTICAST_PORT  8086

enum {
    NET_EVENT_STOP = 0,
    NET_EVENT_TX,
    NET_EVENT_RX,
    NET_EVENT_MAX
};

typedef union {
    struct sockaddr     sa;
    struct sockaddr_in  sin;
    struct sockaddr_in6 sin6;
} net_switch_sockaddr_t;

typedef struct net_switch_hostaddr_t {
    struct net_switch_hostaddr_t *next;
    net_switch_sockaddr_t         addr;
    net_switch_sockaddr_t         addr_tx;
    int                              socket_tx;
} net_switch_hostaddr_t;

typedef struct net_switch_t {
    int                       socket_rx;
    net_switch_hostaddr_t *hostaddrs;
    uint16_t                  port_out;

    uint8_t        promisc;
    union {
        uint8_t  mac_addr[6];
        uint64_t mac_addr_u64;
    };
    netcard_t *    card; /* netcard attached to us */
    thread_t *     poll_tid;
    net_evt_t      tx_event;
    net_evt_t      stop_event;
    netpkt_t       pkt;
    netpkt_t       pkt_tx_v[SWITCH_PKT_BATCH];
    int            during_tx;
    int            recv_on_tx;
#ifdef _WIN32
    HANDLE         sock_event;
#endif
} net_switch_t;

#ifdef ENABLE_SWITCH_LOG
int switch_do_log = ENABLE_SWITCH_LOG;

static void
netswitch_log(const char *fmt, ...)
{
    va_list ap;

    if (switch_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define netswitch_log(fmt, ...)
#endif

static void
net_switch_in_available(void *priv)
{
    net_switch_t *netswitch = (net_switch_t *) priv;
    net_event_set(&netswitch->tx_event);
}

static unsigned int
net_switch_add_hostaddr(net_switch_t *netswitch, net_switch_sockaddr_t *addr, net_switch_sockaddr_t *broadcast, net_switch_sockaddr_t *netmask, unsigned int flags)
{
    if (!addr || !(flags & IFF_UP))
        return 0;

    /* Iterate to the end of the list. */
    net_switch_hostaddr_t **p = &netswitch->hostaddrs;
    for (; *p; p = &(*p)->next) {
        /* Check for duplicates. */
        switch (addr->sa.sa_family) {
            case AF_INET:
                if ((*p)->addr.sin.sin_addr.s_addr == addr->sin.sin_addr.s_addr)
                    return 0;
                break;
#ifdef UNUSED
            case AF_INET6:
                if (AS_U64((*p)->addr.sin6.sin6_addr.s6_addr[0]) == AS_U64(addr->sin6.sin6_addr.s6_addr[0]) &&
                    AS_U64((*p)->addr.sin6.sin6_addr.s6_addr[8]) == AS_U64(addr->sin6.sin6_addr.s6_addr[8]))
                    return 0;
                break;
#endif
            default:
                return 0;
        }
    }

    /* Handle address. */
    net_switch_hostaddr_t *hostaddr = (net_switch_hostaddr_t *) calloc(1, sizeof(net_switch_hostaddr_t));
    hostaddr->socket_tx = -1;
    unsigned int ret = 1;
    if (addr->sa.sa_family == AF_INET) {
#ifdef ENABLE_SWITCH_LOG
        char buf[INET_ADDRSTRLEN];
        buf[0] = '\0';
        inet_ntop(addr->sin.sin_family, &addr->sin.sin_addr.s_addr, buf, sizeof(buf));
#endif

        /* Initialize transmit socket for this interface. */
        hostaddr->socket_tx = socket(addr->sin.sin_family, SOCK_DGRAM, 0);
        if (hostaddr->socket_tx < 0) {
            netswitch_log("Network Switch: could not initialize transmit socket for interface %s\n", buf);
            goto fail;
        }

        /* Initialize addresses. */
        memcpy(&hostaddr->addr.sin, &addr->sin, sizeof(struct sockaddr_in));
        hostaddr->addr_tx.sin.sin_family = addr->sin.sin_family;
        hostaddr->addr_tx.sin.sin_port   = netswitch->port_out;

        /* The problem with multicasting through multiple interfaces is loopback, in which
           all copies of the datagram get reflected back to us and to other instances on the
           same host. Disabling IP_MULTICAST_LOOP on all but one transmit socket can mitigate
           that, but not on Windows where that option applies to the receive socket, so we
           instead disable loopback on all multicast sockets and use a broadcast for loopback. */
        if ((flags & (IFF_MULTICAST | IFF_LOOPBACK)) == IFF_MULTICAST) {
            /* Set multicast interface for the transmit socket. */
            if (setsockopt(hostaddr->socket_tx, IPPROTO_IP, IP_MULTICAST_IF, (char *) &hostaddr->addr.sin.sin_addr.s_addr, sizeof(hostaddr->addr.sin.sin_addr.s_addr)) < 0) {
                netswitch_log("Network Switch: could not configure multicast on interface %s\n", buf);
                goto broadcast;
            }

            /* Join IPv4 multicast group. */
            struct ip_mreq mreq = {
                .imr_multiaddr = { .s_addr = htonl(SWITCH_MULTICAST_GROUP) },
                .imr_interface = { .s_addr = hostaddr->addr.sin.sin_addr.s_addr }
            };
            if (setsockopt(netswitch->socket_rx, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) &mreq, sizeof(mreq)) < 0) {
                netswitch_log("Network Switch: could not join multicast group on interface %s\n", buf);
                goto broadcast;
            }

            /* Destination address is multicast to our group. */
            hostaddr->addr_tx.sin.sin_addr.s_addr = mreq.imr_multiaddr.s_addr;

            /* Disable multicast loopback on non-Windows platforms. (no harm on Windows) */
            int val = 0;
            setsockopt(hostaddr->socket_tx, IPPROTO_IP, IP_MULTICAST_LOOP, (char *) &val, sizeof(val));

            netswitch_log("Network Switch: added multicast interface %s", buf);
        } else {
broadcast:
            /* Determine destination address. */
            if (flags & IFF_LOOPBACK) {
                /* Loopback interfaces don't advertise broadcast support and therefore
                   the broadcast address is invalid, so we build one from the netmask. */
                hostaddr->addr_tx.sin.sin_addr.s_addr = hostaddr->addr.sin.sin_addr.s_addr | (netmask ? ~netmask->sin.sin_addr.s_addr : htonl(0x00ffffff));
                ret = 0;
            } else if (!(flags & (IFF_BROADCAST | IFF_POINTOPOINT)) ||
                       !broadcast || !broadcast->sin.sin_addr.s_addr ||
                       (broadcast->sin.sin_addr.s_addr == hostaddr->addr.sin.sin_addr.s_addr)) {
                /* This interface is unicast-only or P2P with a bad peer address, nothing we can do. */
                netswitch_log("Network Switch: ignored %s interface %s\n", (flags & (IFF_LOOPBACK | IFF_BROADCAST)) ? "broadcast" : "unicast", buf);
                goto fail;
            } else {
                /* Valid broadcast/peer address. */
                hostaddr->addr_tx.sin.sin_addr.s_addr = broadcast->sin.sin_addr.s_addr;
            }

            /* Enable broadcast on the transmit socket if required. */
            if (flags & (IFF_LOOPBACK | IFF_BROADCAST)) {
                int val = 1;
                if (setsockopt(hostaddr->socket_tx, SOL_SOCKET, SO_BROADCAST, (char *) &val, sizeof(val)) < 0) {
                    netswitch_log("Network Switch: could not configure broadcast on interface %s\n", buf);
                    goto fail;
                }
            }

            netswitch_log("Network Switch: added %s interface %s", (flags & (IFF_LOOPBACK | IFF_BROADCAST)) ? "broadcast" : "unicast", buf);
        }
#ifdef ENABLE_SWITCH_LOG
        buf[0] = '\0';
        inet_ntop(hostaddr->addr_tx.sin.sin_family, &hostaddr->addr_tx.sin.sin_addr.s_addr, buf, sizeof(buf));
        netswitch_log(" -> %s:%d\n", buf, ntohs(netswitch->port_out));
#endif
    } else {
        goto fail;
    }

    /* Add address to list. */
    *p = hostaddr;
    return ret;

fail:
    if (hostaddr->socket_tx >= 0)
        close(hostaddr->socket_tx);
    free(hostaddr);
    return 0;
}

static void
net_switch_update_hostaddrs(net_switch_t *netswitch)
{
    unsigned int added = 0;
#ifdef _WIN32
    DWORD buf_size = 16 * sizeof(INTERFACE_INFO);
    INTERFACE_INFO *buf;
retry:
    buf = (INTERFACE_INFO *) malloc(buf_size);
    DWORD returned;
    if (WSAIoctl(netswitch->socket_rx, SIO_GET_INTERFACE_LIST, NULL, 0, buf, buf_size, &returned, NULL, NULL) == SOCKET_ERROR) {
        free(buf);
        if (WSAGetLastError() == WSAEFAULT) {
            buf_size *= 2;
            goto retry;
        }
    } else {
        returned /= sizeof(INTERFACE_INFO);
        for (int i = 0; i < returned; i++) {
            added += net_switch_add_hostaddr(netswitch,
                (net_switch_sockaddr_t *) &buf[i].iiAddress.Address,
                (net_switch_sockaddr_t *) &buf[i].iiBroadcastAddress.Address,
                (net_switch_sockaddr_t *) &buf[i].iiNetmask.Address,
                buf[i].iiFlags);
        }
        free(buf);
    }
#else
    struct ifaddrs *buf;
    if (getifaddrs(&buf) >= 0) {
        for (struct ifaddrs *addr = buf; addr; addr = addr->ifa_next) {
            added += net_switch_add_hostaddr(netswitch,
                (net_switch_sockaddr_t *) addr->ifa_addr,
                (net_switch_sockaddr_t *) ((addr->ifa_flags & IFF_POINTOPOINT) ? addr->ifa_dstaddr : addr->ifa_broadaddr),
                (net_switch_sockaddr_t *) addr->ifa_netmask,
                addr->ifa_flags);
        }
        freeifaddrs(buf);
    }
#endif

    /* Add loopback if it's not present. */
    struct sockaddr_in fallback = {
        .sin_family = AF_INET,
        .sin_addr = { .s_addr = htonl(INADDR_LOOPBACK) }
    };
    struct sockaddr_in fallback_broadcast = {
        .sin_family = AF_INET,
        .sin_addr = { .s_addr = htonl(INADDR_BROADCAST) }
    };
    added += net_switch_add_hostaddr(netswitch,
        (net_switch_sockaddr_t *) (struct sockaddr *) &fallback,
        (net_switch_sockaddr_t *) (struct sockaddr *) &fallback_broadcast,
        NULL, IFF_UP | IFF_LOOPBACK);

    /* If no non-loopback interfaces have been successfully added,
       fall back to IPv4 multicast on a single OS-selected interface. */
    if (!added) {
        fallback.sin_addr.s_addr = htonl(INADDR_ANY);
        net_switch_add_hostaddr(netswitch,
            (net_switch_sockaddr_t *) (struct sockaddr *) &fallback,
            (net_switch_sockaddr_t *) (struct sockaddr *) &fallback_broadcast,
            NULL, IFF_UP | IFF_MULTICAST);
    }
}

static void
net_switch_thread(void *priv)
{
    net_switch_t *netswitch = (net_switch_t *) priv;

    /* Start polling. */
    netswitch_log("Network Switch: polling started\n");

#ifdef _WIN32
    WSAEventSelect(netswitch->socket_rx, netswitch->sock_event, FD_READ);

    HANDLE events[NET_EVENT_MAX];
    events[NET_EVENT_STOP] = net_event_get_handle(&netswitch->stop_event);
    events[NET_EVENT_TX]   = net_event_get_handle(&netswitch->tx_event);
    events[NET_EVENT_RX]   = netswitch->sock_event;
#else
    struct pollfd pfd[NET_EVENT_MAX];
    pfd[NET_EVENT_STOP].fd     = net_event_get_fd(&netswitch->stop_event);
    pfd[NET_EVENT_STOP].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_TX].fd     = net_event_get_fd(&netswitch->tx_event);
    pfd[NET_EVENT_TX].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_RX].fd     = netswitch->socket_rx;
    pfd[NET_EVENT_RX].events = POLLIN | POLLPRI;
#endif

    int packets;
    ssize_t len;
#ifdef _WIN32
    uint8_t run = 1;
    while (run) {
        int ret = WaitForMultipleObjects(NET_EVENT_MAX, events, FALSE, INFINITE);
        switch (ret - WAIT_OBJECT_0) {
            case NET_EVENT_STOP:
                run = 0;
#else
    while (1) {
        poll(pfd, NET_EVENT_MAX, -1);
        if (pfd[NET_EVENT_STOP].revents & POLLIN) {
#endif
            net_event_clear(&netswitch->stop_event);
            break;
#ifdef _WIN32

            case NET_EVENT_TX:
#else
        }
        if (pfd[NET_EVENT_TX].revents & POLLIN) {
#endif
            net_event_clear(&netswitch->tx_event);
            netswitch->during_tx = 1;
            packets = network_tx_popv(netswitch->card, netswitch->pkt_tx_v, SWITCH_PKT_BATCH);
            for (int i = 0; i < packets; i++) {
#define MAC_FORMAT "(%02X:%02X:%02X:%02X:%02X:%02X -> %02X:%02X:%02X:%02X:%02X:%02X)"
#define MAC_FORMAT_ARGS(p) (p)[6], (p)[7], (p)[8], (p)[9], (p)[10], (p)[11], (p)[0], (p)[1], (p)[2], (p)[3], (p)[4], (p)[5]
                netswitch_log("Network Switch: sending %d-byte packet " MAC_FORMAT "\n",
                              netswitch->pkt_tx_v[i].len, MAC_FORMAT_ARGS(netswitch->pkt_tx_v[i].data));

                /* Send through all known host interfaces. */
                for (net_switch_hostaddr_t *hostaddr = netswitch->hostaddrs; hostaddr; hostaddr = hostaddr->next)
                    sendto(hostaddr->socket_tx,
                           (char *) netswitch->pkt_tx_v[i].data, netswitch->pkt_tx_v[i].len, 0,
                           &hostaddr->addr_tx.sa, sizeof(hostaddr->addr_tx.sa));
            }
            netswitch->during_tx = 0;

            if (netswitch->recv_on_tx) {
                do {
                    packets = network_rx_on_tx_popv(netswitch->card, netswitch->pkt_tx_v, SWITCH_PKT_BATCH);
                    for (int i = 0; i < packets; i++)
                        network_rx_put_pkt(netswitch->card, &(netswitch->pkt_tx_v[i]));
                } while (packets > 0);
                netswitch->recv_on_tx = 0;
            }
#ifdef _WIN32
                break;

            case NET_EVENT_RX:
#else
        }
        if (pfd[NET_EVENT_RX].revents & POLLIN) {
#endif
            len = recv(netswitch->socket_rx, (char *) netswitch->pkt.data, NET_MAX_FRAME, 0);
            if (len < 12) {
                netswitch_log("Network Switch: recv error (%d)\n", len);
            } else if ((AS_U64(netswitch->pkt.data[6]) & le64_to_cpu(0xffffffffffffULL)) == netswitch->mac_addr_u64) {
                /* A packet we've sent has looped back, drop it. */
            } else if (netswitch->promisc || /* promiscuous mode? */
                       (netswitch->pkt.data[0] & 1) || /* broadcast packet? */
                       ((AS_U64(netswitch->pkt.data[0]) & le64_to_cpu(0xffffffffffffULL)) == netswitch->mac_addr_u64)) { /* packet for me? */
                netswitch_log("Network Switch: receiving %d-byte packet " MAC_FORMAT "\n",
                              len, MAC_FORMAT_ARGS(netswitch->pkt.data));
                netswitch->pkt.len = len;
                if (netswitch->during_tx) {
                    network_rx_on_tx_put_pkt(netswitch->card, &netswitch->pkt);
                    netswitch->recv_on_tx = 1;
                } else {
                    network_rx_put_pkt(netswitch->card, &netswitch->pkt);
                }
            } else {
                netswitch_log("Network Switch: dropping %d-byte packet " MAC_FORMAT "\n",
                              len, MAC_FORMAT_ARGS(netswitch->pkt.data));
            }
#ifdef _WIN32
                break;
#endif
        }
    }

    netswitch_log("Network Switch: polling stopped\n");
}

static void net_switch_close(void *priv);

void *
net_switch_init(const netcard_t *card, const uint8_t *mac_addr, void *priv, char *netdrv_errbuf)
{
    netcard_conf_t *netcard = (netcard_conf_t *) priv;

    netswitch_log("Network Switch: initializing with group %d...\n", netcard->switch_group);

    net_switch_t *netswitch = calloc(1, sizeof(net_switch_t));
    memcpy(netswitch->mac_addr, mac_addr, sizeof(netswitch->mac_addr));
    netswitch->card = (netcard_t *) card;
    netswitch->promisc = !!netcard->promisc_mode;

    /* Initialize receive socket. */
    netswitch->socket_rx = socket(AF_INET, SOCK_DGRAM, 0);
    if (netswitch->socket_rx < 0) {
        strncpy(netdrv_errbuf, "Could not initialize receive socket\n", NET_DRV_ERRBUF_SIZE);
        goto fail;
    }

    int val = 1;
    if (setsockopt(netswitch->socket_rx, SOL_SOCKET, SO_REUSEADDR, (char *) &val, sizeof(val)) < 0) {
        strncpy(netdrv_errbuf, "Could not set SO_REUSEADDR\n", NET_DRV_ERRBUF_SIZE);
        goto fail;
    }
#ifndef _WIN32
    if (setsockopt(netswitch->socket_rx, SOL_SOCKET, SO_REUSEPORT, (char *) &val, sizeof(val)) < 0) {
        strncpy(netdrv_errbuf, "Could not set SO_REUSEPORT\n", NET_DRV_ERRBUF_SIZE);
        goto fail;
    }
#endif

    /* Disable multicast loopback on Windows. (no harm on other platforms) */
    val = 0;
    setsockopt(netswitch->socket_rx, IPPROTO_IP, IP_MULTICAST_LOOP, (char *) &val, sizeof(val));

    netswitch->port_out = htons(SWITCH_MULTICAST_PORT - NET_SWITCH_GRP_MIN + netcard->switch_group);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr = { .s_addr = htonl(INADDR_ANY) },
        .sin_port = netswitch->port_out
    };
    if (bind(netswitch->socket_rx, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        snprintf(netdrv_errbuf, NET_DRV_ERRBUF_SIZE, "Could not bind to port %d\n", (int) addr.sin_port);
        goto fail;
    }

    /* Add host interfaces. */
    net_switch_update_hostaddrs(netswitch);
    if (!netswitch->hostaddrs) {
        strncpy(netdrv_errbuf, "Could not add any interfaces\n", NET_DRV_ERRBUF_SIZE);
        goto fail;
    }

    for (int i = 0; i < SWITCH_PKT_BATCH; i++)
        netswitch->pkt_tx_v[i].data = calloc(1, NET_MAX_FRAME);
    netswitch->pkt.data = calloc(1, NET_MAX_FRAME);
    net_event_init(&netswitch->tx_event);
    net_event_init(&netswitch->stop_event);
#ifdef _WIN32
    netswitch->sock_event = CreateEvent(NULL, FALSE, FALSE, NULL);
#endif

    netswitch_log("Network Switch: creating thread...\n");
    netswitch->poll_tid = thread_create(net_switch_thread, netswitch);

    return netswitch;

fail:
    net_switch_close(netswitch);
    return NULL;
}

void
net_switch_close(void *priv)
{
    if (!priv)
        return;

    net_switch_t *netswitch = (net_switch_t *) priv;

    netswitch_log("Network Switch: closing\n");

    if (netswitch->poll_tid) {
        /* Tell the polling thread to shut down. */
        net_event_set(&netswitch->stop_event);

        /* Wait for the thread to finish. */
        netswitch_log("Network Switch: waiting for thread to end...\n");
        thread_wait(netswitch->poll_tid);
    }

    net_switch_hostaddr_t *hostaddr = netswitch->hostaddrs;
    while (hostaddr) {
        if (hostaddr->socket_tx >= 0)
            close(hostaddr->socket_tx);
        net_switch_hostaddr_t *next = hostaddr->next;
        free(hostaddr);
        hostaddr = next;
    }
    if (netswitch->socket_rx >= 0)
        close(netswitch->socket_rx);
    net_event_close(&netswitch->stop_event);
    net_event_close(&netswitch->tx_event);
    for (int i = 0; i < SWITCH_PKT_BATCH; i++)
        free(netswitch->pkt_tx_v[i].data);
    free(netswitch->pkt.data);
    free(netswitch);
}

const netdrv_t net_switch_drv = {
    .notify_in = &net_switch_in_available,
    .init      = &net_switch_init,
    .close     = &net_switch_close,
    .priv      = NULL
};
