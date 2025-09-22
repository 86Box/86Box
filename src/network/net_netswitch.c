/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Network Switch network driver
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <winsock2.h>
#else
#    include <poll.h>
#endif

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/net_event.h>
#include "netswitch.h"
#include "networkmessage.pb.h"

enum {
    NET_EVENT_STOP = 0,
    NET_EVENT_TX,
    NET_EVENT_RX,
    NET_EVENT_SWITCH,
    NET_EVENT_MAX
};

/* Special define for the windows portion. We only need to poll up to
 * NET_EVENT_SWITCH. NET_EVENT_SWITCH gives us a different NET_EVENT_MAX
 * excluding the others, and windows does not like polling events that
 * do not exist. */
#define NET_EVENT_WIN_MAX NET_EVENT_SWITCH

#define SWITCH_PKT_BATCH NET_QUEUE_LEN
/* In µs, how often to send a keepalive and perform connection maintenance */
#define SWITCH_KEEPALIVE_INTERVAL 5000000
/* In ms, how long until we consider a connection gone? */
#define SWITCH_MAX_INTERVAL 10000

typedef struct {
    void           *nsconn;
    uint8_t         mac_addr[6];
    netcard_t      *card;
    thread_t       *poll_tid;
    net_evt_t       tx_event;
    net_evt_t       stop_event;
    netpkt_t        pktv[SWITCH_PKT_BATCH];
    pc_timer_t      stats_timer;
    pc_timer_t      maintenance_timer;
    ns_rx_packet_t  rx_packet;
    char            switch_type[16];
#ifdef _WIN32
    HANDLE sock_event;
#endif
} net_netswitch_t;

// Used for debugging, needs to be moved to an official location
void print_packet(const netpkt_t netpkt) {
#ifdef NET_SWITCH_LOG
    if(netpkt.len == 0) {
        net_switch_log("Something is wrong, len is %d\n", netpkt.len);
        return;
    }
    /* Temporarily disable log suppression for packet dumping to allow specific formatting */
    pclog_toggle_suppr();
    uint8_t linebuff[17] = "\0";
    char src_mac_buf[32] = "";
    char dst_mac_buf[32] = "";
    for(int m_i=0; m_i < 6; m_i++) {
        char src_octet[4];
        char dst_octet[4];
        snprintf(src_octet, sizeof(src_octet), "%02X%s", netpkt.data[m_i+6], m_i < 5 ? ":" : "");
        strncat(src_mac_buf, src_octet, sizeof (src_mac_buf) - 1);

        snprintf(dst_octet, sizeof(dst_octet), "%02X%s", netpkt.data[m_i], m_i < 5 ? ":" : "");
        strncat(dst_mac_buf, dst_octet, sizeof (dst_mac_buf) - 1);
    }
    net_switch_log("%s -> %s\n\n", src_mac_buf, dst_mac_buf);

    // Payload length (bytes 12-13 with zero index)
    uint16_t payload_length = (netpkt.data[12] & 0xFF) << 8;
    payload_length |= (netpkt.data[13] & 0xFF);
    const uint16_t actual_length = netpkt.len - 14;
    if(payload_length <= 1500) {
        // 802.3 / 802.2
        net_switch_log("Payload length according to frame: %i\n", payload_length);
        // remaining length of packet (len - 14) to calculate padding
        net_switch_log("Actual payload length: %i\n", actual_length);
        if(payload_length <=46 ) {
            net_switch_log("Likely has %d bytes padding\n", actual_length - payload_length);
        }
    } else {
        // Type II
        net_switch_log("EtherType: 0x%04X\n", payload_length);
    }
    // actual packet size
    net_switch_log("Full frame size: %i\n", netpkt.len);
    net_switch_log("\n");

    for(int i=0; i< netpkt.len; i++) {

        net_switch_log("%02x ", netpkt.data[i]);
        if ((netpkt.data[i] < 0x20) || (netpkt.data[i] > 0x7e)) {
            linebuff[i % 16] = '.';
        } else {
            linebuff[i % 16] = netpkt.data[i];
        }

        if( (i+1) % 8 == 0) {
            net_switch_log(" ");
        }

        if( (i+1) % 16 == 0) {
            net_switch_log("| %s |\n", (char *)linebuff);
            linebuff[0] = '\0';
        }

        // last char?
        if(i+1 == netpkt.len) {
            const int togo = 16 - (i % 16);
            for(int remaining = 0; remaining < togo-1; remaining++) {
                // This would represent the byte display and the space
                net_switch_log("   ");
            }
            // spacing between byte groupings
            if(togo > 8) {
                net_switch_log(" ");
            }
            linebuff[(i % 16) +1] = '\0';
            net_switch_log(" | %s", (char *)linebuff);

            for(int remaining = 0; remaining < togo-1; remaining++) {
                // This would represent the remaining bytes on the right
                net_switch_log(" ");
            }
            net_switch_log(" |\n");
        }
    }
    net_switch_log("\n");
    pclog_toggle_suppr();
#endif /* NET_SWITCH_LOG*/
}

#ifdef ENABLE_NET_SWITCH_STATS
static void
stats_timer(void *priv)
{
    /* Get the device state structure. */
    net_netswitch_t *netswitch = priv;
    const NSCONN    *nsconn    = netswitch->nsconn;
    net_switch_log("Max (frame / packet)  TX (%zu/%zu)  RX (%zu/%zu)\n",
           nsconn->stats.max_tx_frame, nsconn->stats.max_tx_packet,
           nsconn->stats.max_rx_frame, nsconn->stats.max_rx_packet);
    net_switch_log("Last ethertype (TX/RX) (%02x%02x/%02x%02x)\n", nsconn->stats.last_tx_ethertype[0], nsconn->stats.last_tx_ethertype[1],
           nsconn->stats.last_rx_ethertype[0], nsconn->stats.last_rx_ethertype[1]);
    net_switch_log("Packet totals (all/tx/rx/would fragment/max vec) (%zu/%zu/%zu/%zu/%i)\n", nsconn->stats.total_tx_packets + nsconn->stats.total_rx_packets,
           nsconn->stats.total_tx_packets, nsconn->stats.total_rx_packets, nsconn->stats.total_fragments, nsconn->stats.max_vec);
    net_switch_log("---\n");
    /* Restart the timer */
    timer_on_auto(&netswitch->stats_timer, 60000000);
}
#endif

static void
maintenance_timer(void *priv)
{
    /* Get the device state structure. */
    net_netswitch_t *netswitch = (net_netswitch_t *) priv;
    NSCONN         *nsconn = (NSCONN *) netswitch->nsconn;
    if (!ns_send_control(nsconn, MessageType_MESSAGE_TYPE_KEEPALIVE)) {
        net_switch_log("Failed to send keepalive packet\n");
    }
    const int64_t interval = ns_get_current_millis() - nsconn->last_packet_stamp;
//    net_switch_log("Last packet time: %lld ago\n", interval);
//    net_switch_log("Last packet time: %lld ago\n", interval);

    /* A timeout has likely occurred, try to fix the connection if type is REMOTE */
    if((interval > SWITCH_MAX_INTERVAL) && nsconn->switch_type == SWITCH_TYPE_REMOTE) {
        /* FIXME: This is really rough, needs moar logic */
        nsconn->client_state = CONNECTING;
        net_switch_log("We appear to be disconnected, attempting to reconnect\n");
        /* TODO: Proper connect function! This is duplicated code */
        if(!ns_send_control(nsconn, MessageType_MESSAGE_TYPE_CONNECT_REQUEST)) {
            /* TODO: Failure */
        }
    }
    /* Restart the timer */
    timer_on_auto(&netswitch->maintenance_timer, SWITCH_KEEPALIVE_INTERVAL);
}

/* Lots of #ifdef madness here thanks to the polling differences on windows */
static void
net_netswitch_thread(void *priv)
{
    net_netswitch_t *net_netswitch = (net_netswitch_t *) priv;
    NSCONN         *nsconn        = (NSCONN *) net_netswitch->nsconn;
    bool status;
    char switch_type[32];
    snprintf(switch_type, sizeof(switch_type), "%s", nsconn->switch_type == SWITCH_TYPE_REMOTE ? "Remote" : "Local");

    net_switch_log("%s Net Switch: polling started.\n", switch_type);

#ifdef _WIN32
    WSAEventSelect(ns_pollfd(net_netswitch->nsconn), net_netswitch->sock_event, FD_READ);

    HANDLE events[NET_EVENT_MAX];
    events[NET_EVENT_STOP] = net_event_get_handle(&net_netswitch->stop_event);
    events[NET_EVENT_TX]   = net_event_get_handle(&net_netswitch->tx_event);
    events[NET_EVENT_RX]   = net_netswitch->sock_event;

    bool run = true;
#else
    struct pollfd pfd[NET_EVENT_MAX];
    pfd[NET_EVENT_STOP].fd     = net_event_get_fd(&net_netswitch->stop_event);
    pfd[NET_EVENT_STOP].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_TX].fd     = net_event_get_fd(&net_netswitch->tx_event);
    pfd[NET_EVENT_TX].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_RX].fd     = ns_pollfd(net_netswitch->nsconn);
    pfd[NET_EVENT_RX].events = POLLIN | POLLPRI;
#endif

#ifdef _WIN32
    while (run) {
        int ret = WaitForMultipleObjects(NET_EVENT_WIN_MAX, events, FALSE, INFINITE);

        switch (ret - WAIT_OBJECT_0) {
#else
    while (1) {
        poll(pfd, NET_EVENT_MAX, -1);
#endif

#ifdef _WIN32
            case NET_EVENT_STOP:
                net_event_clear(&net_netswitch->stop_event);
                run = false;
                break;
            case NET_EVENT_TX:
#else
        if (pfd[NET_EVENT_STOP].revents & POLLIN) {
            net_event_clear(&net_netswitch->stop_event);
            break;
        }
        if (pfd[NET_EVENT_TX].revents & POLLIN) {
#endif
                net_event_clear(&net_netswitch->tx_event);

                const int packets = network_tx_popv(net_netswitch->card, net_netswitch->pktv, SWITCH_PKT_BATCH);
                if (packets > nsconn->stats.max_vec) {
                    nsconn->stats.max_vec = packets;
                }
                for (int i = 0; i < packets; i++) {
                    //                net_switch_log("%d packet(s) to send\n", packets);
#if defined(NET_PRINT_PACKET_TX) || defined(NET_PRINT_PACKET_ALL)
                    data_packet_info_t packet_info = get_data_packet_info(&net_netswitch->pktv[i], net_netswitch->mac_addr);
                    /* Temporarily disable log suppression for packet logging */
                    pclog_toggle_suppr();
                    net_switch_log("%s Net Switch: TX: %s\n", switch_type, packet_info.printable);
                    pclog_toggle_suppr();
                    print_packet(net_netswitch->pktv[i]);
#endif
                    /* Only send if we're in a connected state (always true for local) */
                    if(ns_connected(net_netswitch->nsconn)) {
                        const ssize_t nc = ns_send_pb(net_netswitch->nsconn, &net_netswitch->pktv[i], 0);
                        if (nc < 1) {
                            perror("Got");
                            net_switch_log("%s Net Switch: Problem, no bytes sent. Got back %i\n", switch_type, nc);
                        }
                    }
                }
#ifdef _WIN32
                break;
            case NET_EVENT_RX:
#else
        }
        if (pfd[NET_EVENT_RX].revents & POLLIN) {
#endif

                /* Packets are available for reading */
                status = ns_recv_pb(net_netswitch->nsconn, &net_netswitch->rx_packet, NET_MAX_FRAME, 0);
                if (!status) {
                    net_switch_log("Receive packet failed. Skipping.\n");
                    continue;
                }

                /* These types are handled in the backend and don't need to be considered */
                if (is_control_packet(&net_netswitch->rx_packet) || is_fragment_packet(&net_netswitch->rx_packet)) {
                    continue;
                }
                data_packet_info_t packet_info = get_data_packet_info(&net_netswitch->rx_packet.pkt, net_netswitch->mac_addr);
#if defined(NET_PRINT_PACKET_RX) || defined(NET_PRINT_PACKET_ALL)
                print_packet(net_netswitch->rx_packet.pkt);
#endif
                /*
                 * Accept packets that are
                   * Unicast for us
                   * Broadcasts that are not from us
                   * All other packets *if* promiscuous mode is enabled (excluding our own)
                 */
                if (packet_info.is_packet_for_me || (packet_info.is_broadcast && !packet_info.is_packet_from_me)) {
                    /* Temporarily disable log suppression for packet logging */
                    pclog_toggle_suppr();
                    net_switch_log("%s Net Switch: RX: %s\n", switch_type, packet_info.printable);
                    pclog_toggle_suppr();
                    network_rx_put_pkt(net_netswitch->card, &net_netswitch->rx_packet.pkt);
                } else if (packet_info.is_packet_from_me) {
                    net_switch_log("%s Net Switch: Got my own packet... ignoring\n", switch_type);
                } else {
                    /* Not our packet. Pass it along if promiscuous mode is enabled. */
                    if (ns_flags(net_netswitch->nsconn) & FLAGS_PROMISC) {
                        net_switch_log("%s Net Switch: Got packet from %s (not mine, promiscuous is set, getting)\n", switch_type, packet_info.src_mac_h);
                        network_rx_put_pkt(net_netswitch->card, &net_netswitch->rx_packet.pkt);
                    } else {
                        net_switch_log("%s Net Switch: RX: %s (not mine, dest %s != %s, promiscuous not set, ignoring)\n", switch_type, packet_info.printable, packet_info.dest_mac_h, packet_info.my_mac_h);
                    }
                }
#ifdef _WIN32
                break;
        }
#else
        }
#endif
    }

    net_switch_log("%s Net Switch: polling stopped.\n", switch_type);
}

void
net_netswitch_error(char *errbuf, const char *message) {
    strncpy(errbuf, message, NET_DRV_ERRBUF_SIZE);
    net_switch_log("Net Switch: %s\n", message);
}

void *
net_netswitch_init(const netcard_t *card, const uint8_t *mac_addr, void *priv, char *netdrv_errbuf)
{
    net_switch_log("Net Switch: Init\n");

    netcard_conf_t *netcard = (netcard_conf_t *) priv;

    ns_flags_t flags = FLAGS_NONE;
    ns_type_t  switch_type;

    const int net_type = netcard->net_type;
    if(net_type == NET_TYPE_NRSWITCH) {
        net_switch_log("Switch type: Remote\n");
        switch_type = SWITCH_TYPE_REMOTE;
    } else if (net_type == NET_TYPE_NMSWITCH) {
        net_switch_log("Switch type: Local Multicast\n");
        switch_type = SWITCH_TYPE_LOCAL;
        if(netcard->promisc_mode) {
            flags |= FLAGS_PROMISC;
        }
    } else {
        net_switch_log("Failed: Unknown net switch type %d\n", net_type);
        return NULL;
    }

    // FIXME: Only here during dev. This would be an error otherwise (hostname not specified)
    if(strlen(netcard->nrs_hostname) == 0) {
        strncpy(netcard->nrs_hostname, "127.0.0.1", 128 - 1);
    }

    net_netswitch_t *net_netswitch = calloc(1, sizeof(net_netswitch_t));
    net_netswitch->card           = (netcard_t *) card;
    memcpy(net_netswitch->mac_addr, mac_addr, sizeof(net_netswitch->mac_addr));
    snprintf(net_netswitch->switch_type, sizeof(net_netswitch->switch_type), "%s", net_type == NET_TYPE_NRSWITCH ? "Remote" : "Local");

//    net_switch_log("%s Net Switch: mode: %d, group %d, hostname %s len %lu\n", net_netswitch->switch_type, netcard->promisc_mode, netcard->switch_group, netcard->nrs_hostname, strlen(netcard->nrs_hostname));

    struct ns_open_args ns_args;
    ns_args.type = switch_type;
    /* Setting FLAGS_PROMISC here lets all packets through except the ones from us */
    ns_args.flags = flags;
    /* This option sets which switch group you want to be a part of.
     * Functionally equivalent to being plugged into a different switch */
    ns_args.group = netcard->switch_group;
    /* You could also set the client_id here. If 0, it will be generated. */
    ns_args.client_id = 0;
    memcpy(ns_args.mac_addr, net_netswitch->mac_addr, 6);
    /* The remote switch hostname */
    strncpy(ns_args.nrs_hostname, netcard->nrs_hostname, sizeof(ns_args.nrs_hostname) - 1);
    ns_args.nrs_hostname[127] = 0x00;

    net_switch_log("%s Net Switch: Starting up virtual switch with group %d, flags %d\n", net_netswitch->switch_type, ns_args.group, ns_args.flags);

    if ((net_netswitch->nsconn = ns_open(&ns_args)) == NULL) {
        char buf[NET_DRV_ERRBUF_SIZE];
        /* We're using some errnos for our own purposes */
        switch (errno) {
            case EFAULT:
                snprintf(buf, NET_DRV_ERRBUF_SIZE, "Unable to open switch group %d: Cannot resolve remote switch hostname %s", ns_args.group, ns_args.nrs_hostname);
                break;
            default:
                snprintf(buf, NET_DRV_ERRBUF_SIZE, "Unable to open switch group %d (%s)", ns_args.group, strerror(errno));
                break;

        }
        net_netswitch_error(netdrv_errbuf, buf);
        free(net_netswitch);
        return NULL;
    }

    for (int i = 0; i < SWITCH_PKT_BATCH; i++) {
        net_netswitch->pktv[i].data = calloc(1, NET_MAX_FRAME);
    }
    net_netswitch->rx_packet.pkt.data = calloc(1, NET_MAX_FRAME);

    net_event_init(&net_netswitch->tx_event);
    net_event_init(&net_netswitch->stop_event);
#ifdef _WIN32
    net_netswitch->sock_event = CreateEvent(NULL, FALSE, FALSE, NULL);
#endif
    net_netswitch->poll_tid = thread_create(net_netswitch_thread, net_netswitch);

    /* Add the timers */
#ifdef ENABLE_NET_SWITCH_STATS
    timer_add(&net_netswitch->stats_timer, stats_timer, net_netswitch, 0);
    timer_on_auto(&net_netswitch->stats_timer, 5000000);
#endif
    timer_add(&net_netswitch->maintenance_timer, maintenance_timer, net_netswitch, 0);
    timer_on_auto(&net_netswitch->maintenance_timer, SWITCH_KEEPALIVE_INTERVAL);

    /* Send join message. Return status not checked here. */
    ns_send_control(net_netswitch->nsconn, MessageType_MESSAGE_TYPE_JOIN);

    return net_netswitch;
}

void
net_netswitch_in_available(void *priv)
{
    net_netswitch_t *net_netswitch = (net_netswitch_t *) priv;
    net_event_set(&net_netswitch->tx_event);
}

void
net_netswitch_close(void *priv)
{
    if (priv == NULL)
        return;

    net_netswitch_t *net_netswitch = (net_netswitch_t *) priv;

    net_switch_log("%s Net Switch: closing.\n", net_netswitch->switch_type);

    /* Tell the thread to terminate. */
    net_event_set(&net_netswitch->stop_event);

    /* Wait for the thread to finish. */
    net_switch_log("%s Net Switch: waiting for thread to end...\n", net_netswitch->switch_type);
    thread_wait(net_netswitch->poll_tid);
    net_switch_log("%s Net Switch: thread ended\n", net_netswitch->switch_type);

    for (int i = 0; i < SWITCH_PKT_BATCH; i++) {
        free(net_netswitch->pktv[i].data);
    }
    free(net_netswitch->rx_packet.pkt.data);

    net_event_close(&net_netswitch->tx_event);
    net_event_close(&net_netswitch->stop_event);

#ifdef _WIN32
    WSACleanup();
#endif

    ns_close(net_netswitch->nsconn);
    free(net_netswitch);
}

const netdrv_t net_netswitch_drv = {
    .notify_in = &net_netswitch_in_available,
    .init      = &net_netswitch_init,
    .close     = &net_netswitch_close,
    .priv      = NULL,
};
