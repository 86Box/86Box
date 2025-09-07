/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
*          running old operating systems and software designed for IBM
*          PC systems and compatibles from 1981 through fairly recent
*          system designs based on the PCI bus.
*
*          This file is part of the 86Box distribution.
*
*          Null network driver
*
*
*
* Authors: cold-brewed
*
*          Copyright 2023 The 86Box development team
*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdbool.h>
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
#include <86box/plat_unused.h>

enum {
    NET_EVENT_STOP = 0,
    NET_EVENT_TX,
    NET_EVENT_RX,
    NET_EVENT_MAX
};

/* Special define for the windows portion. Because we are not interested
 * in NET_EVENT_RX for the null driver, we only need to poll up to
 * NET_EVENT_TX. NET_EVENT_RX gives us a different NET_EVENT_MAX
 * excluding NET_EVENT_RX. */
#define NET_EVENT_TX_MAX NET_EVENT_RX

#define NULL_PKT_BATCH NET_QUEUE_LEN

typedef struct net_null_t {
    uint8_t    mac_addr[6];
    netcard_t *card;
    thread_t  *poll_tid;
    net_evt_t  tx_event;
    net_evt_t  stop_event;
    netpkt_t   pkt;
    netpkt_t   pktv[NULL_PKT_BATCH];
} net_null_t;

#ifdef ENABLE_NET_NULL_LOG
int net_null_do_log = ENABLE_NET_NULL_LOG;

static void
net_null_log(const char *fmt, ...)
{
    va_list ap;

    if (net_null_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define net_null_log(fmt, ...)
#endif

#ifdef _WIN32
static void
net_null_thread(void *priv)
{
    net_null_t *net_null = (net_null_t *) priv;

    net_null_log("Null Network: polling started.\n");

    HANDLE events[NET_EVENT_TX_MAX];
    events[NET_EVENT_STOP] = net_event_get_handle(&net_null->stop_event);
    events[NET_EVENT_TX]   = net_event_get_handle(&net_null->tx_event);

    bool run = true;

    while (run) {
        int ret = WaitForMultipleObjects(NET_EVENT_TX_MAX, events, FALSE, INFINITE);

        switch (ret - WAIT_OBJECT_0) {
            case NET_EVENT_STOP:
                net_event_clear(&net_null->stop_event);
                run = false;
                break;

            case NET_EVENT_TX:
                net_event_clear(&net_null->tx_event);
                int packets = network_tx_popv(net_null->card, net_null->pktv, NULL_PKT_BATCH);
                for (int i = 0; i < packets; i++) {
                    net_null_log("Null Network: Ignoring TX packet (%d bytes)\n", net_null->pktv[i].len);
                }
                break;

            default:
                net_null_log("Null Network: Unknown event.\n");
                break;
        }
    }

    net_null_log("Null Network: polling stopped.\n");
}
#else
static void
net_null_thread(void *priv)
{
    net_null_t *net_null = (net_null_t *) priv;

    net_null_log("Null Network: polling started.\n");

    struct pollfd pfd[NET_EVENT_MAX];
    pfd[NET_EVENT_STOP].fd     = net_event_get_fd(&net_null->stop_event);
    pfd[NET_EVENT_STOP].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_TX].fd     = net_event_get_fd(&net_null->tx_event);
    pfd[NET_EVENT_TX].events = POLLIN | POLLPRI;

    while (1) {
        poll(pfd, NET_EVENT_MAX, -1);

        if (pfd[NET_EVENT_STOP].revents & POLLIN) {
            net_event_clear(&net_null->stop_event);
            break;
        }

        if (pfd[NET_EVENT_TX].revents & POLLIN) {
            net_event_clear(&net_null->tx_event);

            int packets = network_tx_popv(net_null->card, net_null->pktv, NULL_PKT_BATCH);
            for (int i = 0; i < packets; i++) {
                net_null_log("Null Network: Ignoring TX packet (%d bytes)\n", net_null->pktv[i].len);
            }
        }
    }

    net_null_log("Null Network: polling stopped.\n");
}
#endif

void *
net_null_init(const netcard_t *card, const uint8_t *mac_addr, UNUSED(void *priv), UNUSED(char *netdrv_errbuf))
{
    net_null_log("Null Network: Init\n");

    net_null_t *net_null = calloc(1, sizeof(net_null_t));
    net_null->card       = (netcard_t *) card;
    memcpy(net_null->mac_addr, mac_addr, sizeof(net_null->mac_addr));

    for (int i = 0; i < NULL_PKT_BATCH; i++) {
        net_null->pktv[i].data = calloc(1, NET_MAX_FRAME);
    }
    net_null->pkt.data = calloc(1, NET_MAX_FRAME);

    net_event_init(&net_null->tx_event);
    net_event_init(&net_null->stop_event);
    net_null->poll_tid = thread_create(net_null_thread, net_null);

    return net_null;
}



void
net_null_in_available(void *priv)
{
    net_null_t *net_null = (net_null_t *) priv;
    net_event_set(&net_null->tx_event);
}

void
net_null_close(void *priv)
{
    if (!priv)
        return;

    net_null_t *net_null = (net_null_t *) priv;

    net_null_log("Null Network: closing.\n");

    /* Tell the thread to terminate. */
    net_event_set(&net_null->stop_event);

    /* Wait for the thread to finish. */
    net_null_log("Null Network: waiting for thread to end...\n");
    thread_wait(net_null->poll_tid);
    net_null_log("Null Network: thread ended\n");

    for (int i = 0; i < NULL_PKT_BATCH; i++) {
        free(net_null->pktv[i].data);
    }
    free(net_null->pkt.data);

    net_event_close(&net_null->tx_event);
    net_event_close(&net_null->stop_event);

    free(net_null);
}

const netdrv_t net_null_drv = {
    .notify_in = &net_null_in_available,
    .init      = &net_null_init,
    .close     = &net_null_close,
    .priv      = NULL
};
