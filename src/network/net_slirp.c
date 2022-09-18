/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle SLiRP library processing.
 *
 *		Some of the code was borrowed from libvdeslirp
 *		<https://github.com/virtualsquare/libvdeslirp>
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <wchar.h>
#include <slirp/libslirp.h>
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
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <poll.h>
#endif
#include <86box/net_event.h>

#define SLIRP_PKT_BATCH NET_QUEUE_LEN

enum {
    NET_EVENT_STOP = 0,
    NET_EVENT_TX,
    NET_EVENT_RX,
    NET_EVENT_MAX
};

typedef struct {
    Slirp     *slirp;
    uint8_t    mac_addr[6];
    netcard_t *card; /* netcard attached to us */
    thread_t  *poll_tid;
    net_evt_t  tx_event;
    net_evt_t  stop_event;
    netpkt_t   pkt;
    netpkt_t   pkt_tx_v[SLIRP_PKT_BATCH];
#ifdef _WIN32
    HANDLE sock_event;
#else
    uint32_t       pfd_len, pfd_size;
    struct pollfd *pfd;
#endif
} net_slirp_t;

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

static void
net_slirp_guest_error(const char *msg, void *opaque)
{
    slirp_log("SLiRP: guest_error(): %s\n", msg);
}

static int64_t
net_slirp_clock_get_ns(void *opaque)
{
    return (int64_t) ((double) tsc / cpuclock * 1000000000.0);
}

static void *
net_slirp_timer_new(SlirpTimerCb cb, void *cb_opaque, void *opaque)
{
    pc_timer_t *timer = malloc(sizeof(pc_timer_t));
    timer_add(timer, cb, cb_opaque, 0);
    return timer;
}

static void
net_slirp_timer_free(void *timer, void *opaque)
{
    timer_stop(timer);
    free(timer);
}

static void
net_slirp_timer_mod(void *timer, int64_t expire_timer, void *opaque)
{
    timer_on_auto(timer, expire_timer * 1000);
}

static void
net_slirp_register_poll_fd(int fd, void *opaque)
{
    (void) fd;
    (void) opaque;
}

static void
net_slirp_unregister_poll_fd(int fd, void *opaque)
{
    (void) fd;
    (void) opaque;
}

static void
net_slirp_notify(void *opaque)
{
    (void) opaque;
}

ssize_t
net_slirp_send_packet(const void *qp, size_t pkt_len, void *opaque)
{
    net_slirp_t *slirp = (net_slirp_t *) opaque;

    slirp_log("SLiRP: received %d-byte packet\n", pkt_len);

    memcpy(slirp->pkt.data, (uint8_t *) qp, pkt_len);
    slirp->pkt.len = pkt_len;
    network_rx_put_pkt(slirp->card, &slirp->pkt);

    return pkt_len;
}

#ifdef _WIN32
static int
net_slirp_add_poll(int fd, int events, void *opaque)
{
    net_slirp_t *slirp   = (net_slirp_t *) opaque;
    long         bitmask = 0;
    if (events & SLIRP_POLL_IN)
        bitmask |= FD_READ | FD_ACCEPT;
    if (events & SLIRP_POLL_OUT)
        bitmask |= FD_WRITE | FD_CONNECT;
    if (events & SLIRP_POLL_HUP)
        bitmask |= FD_CLOSE;
    if (events & SLIRP_POLL_PRI)
        bitmask |= FD_OOB;

    WSAEventSelect(fd, slirp->sock_event, bitmask);
    return fd;
}
#else
static int
net_slirp_add_poll(int fd, int events, void *opaque)
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
    if (WSAEnumNetworkEvents(idx, slirp->sock_event, &ev) != 0) {
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
    WSA_TO_POLL(FD_CLOSE, SLIRP_POLL_HUP);

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
    .register_poll_fd   = net_slirp_register_poll_fd,
    .unregister_poll_fd = net_slirp_unregister_poll_fd,
    .notify             = net_slirp_notify
};

/* Send a packet to the SLiRP interface. */
static void
net_slirp_in(net_slirp_t *slirp, uint8_t *pkt, int pkt_len)
{
    if (!slirp)
        return;

    slirp_log("SLiRP: sending %d-byte packet to host network\n", pkt_len);

    slirp_input(slirp->slirp, (const uint8_t *) pkt, pkt_len);
}

void
net_slirp_in_available(void *priv)
{
    net_slirp_t *slirp = (net_slirp_t *) priv;
    net_event_set(&slirp->tx_event);
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
        slirp_pollfds_fill(slirp->slirp, &timeout, net_slirp_add_poll, slirp);
        if (timeout < 0)
            timeout = INFINITE;

        int ret = WaitForMultipleObjects(3, events, FALSE, (DWORD) timeout);
        switch (ret - WAIT_OBJECT_0) {
            case NET_EVENT_STOP:
                run = false;
                break;

            case NET_EVENT_TX:
                {
                    int packets = network_tx_popv(slirp->card, slirp->pkt_tx_v, SLIRP_PKT_BATCH);
                    for (int i = 0; i < packets; i++) {
                        net_slirp_in(slirp, slirp->pkt_tx_v[i].data, slirp->pkt_tx_v[i].len);
                    }
                }
                break;

            default:
                slirp_pollfds_poll(slirp->slirp, ret == WAIT_FAILED, net_slirp_get_revents, slirp);
                break;
        }
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

        slirp_pollfds_fill(slirp->slirp, &timeout, net_slirp_add_poll, slirp);

        int ret = poll(slirp->pfd, slirp->pfd_len, timeout);

        slirp_pollfds_poll(slirp->slirp, (ret < 0), net_slirp_get_revents, slirp);

        if (slirp->pfd[NET_EVENT_STOP].revents & POLLIN) {
            net_event_clear(&slirp->stop_event);
            break;
        }

        if (slirp->pfd[NET_EVENT_TX].revents & POLLIN) {
            net_event_clear(&slirp->tx_event);

            int packets = network_tx_popv(slirp->card, slirp->pkt_tx_v, SLIRP_PKT_BATCH);
            for (int i = 0; i < packets; i++) {
                net_slirp_in(slirp, slirp->pkt_tx_v[i].data, slirp->pkt_tx_v[i].len);
            }
        }
    }

    slirp_log("SLiRP: polling stopped.\n");
}
#endif

static int slirp_card_num = 2;

/* Initialize SLiRP for use. */
void *
net_slirp_init(const netcard_t *card, const uint8_t *mac_addr, void *priv)
{
    slirp_log("SLiRP: initializing...\n");
    net_slirp_t *slirp = calloc(1, sizeof(net_slirp_t));
    memcpy(slirp->mac_addr, mac_addr, sizeof(slirp->mac_addr));
    slirp->card = (netcard_t *) card;

#ifndef _WIN32
    slirp->pfd_size = 16 * sizeof(struct pollfd);
    slirp->pfd      = malloc(slirp->pfd_size);
    memset(slirp->pfd, 0, slirp->pfd_size);
#endif

    /* Set the IP addresses to use. */
    struct in_addr  net        = { .s_addr = htonl(0x0a000000 | (slirp_card_num << 8)) }; /* 10.0.x.0 */
    struct in_addr  mask       = { .s_addr = htonl(0xffffff00) };                         /* 255.255.255.0 */
    struct in_addr  host       = { .s_addr = htonl(0x0a000002 | (slirp_card_num << 8)) }; /* 10.0.x.2 */
    struct in_addr  dhcp       = { .s_addr = htonl(0x0a00000f | (slirp_card_num << 8)) }; /* 10.0.x.15 */
    struct in_addr  dns        = { .s_addr = htonl(0x0a000003 | (slirp_card_num << 8)) }; /* 10.0.x.3 */
    struct in_addr  bind       = { .s_addr = htonl(0x00000000) };                         /* 0.0.0.0 */
    struct in6_addr ipv6_dummy = { 0 };                                                   /* contents don't matter; we're not using IPv6 */

    /* Initialize SLiRP. */
    slirp->slirp = slirp_init(0, 1, net, mask, host, 0, ipv6_dummy, 0, ipv6_dummy, NULL, NULL, NULL, NULL, dhcp, dns, ipv6_dummy, NULL, NULL, &slirp_cb, slirp);
    if (!slirp->slirp) {
        slirp_log("SLiRP: initialization failed\n");
        free(slirp);
        return NULL;
    }

    /* Set up port forwarding. */
    int  udp, external, internal, i = 0;
    char category[32];
    snprintf(category, sizeof(category), "SLiRP Port Forwarding #%i", card->card_num + 1);
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
    net_event_init(&slirp->tx_event);
    net_event_init(&slirp->stop_event);
#ifdef _WIN32
    slirp->sock_event = CreateEvent(NULL, FALSE, FALSE, NULL);
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

    net_event_close(&slirp->tx_event);
    net_event_close(&slirp->stop_event);
    slirp_cleanup(slirp->slirp);
    for (int i = 0; i < SLIRP_PKT_BATCH; i++) {
        free(slirp->pkt_tx_v[i].data);
    }
    free(slirp->pkt.data);
    free(slirp);
    slirp_card_num--;
}

const netdrv_t net_slirp_drv = {
    &net_slirp_in_available,
    &net_slirp_init,
    &net_slirp_close
};

/* Stubs to stand in for the parts of libslirp we skip compiling. */
void ncsi_input(void *slirp, const uint8_t *pkt, int pkt_len) {}
void ip6_init(void *slirp) {}
void ip6_cleanup(void *slirp) {}
void ip6_input(void *m) {}
int ip6_output(void *so, void *m, int fast) { return 0; }
void in6_compute_ethaddr(struct in6_addr ip, uint8_t *eth) {}
bool in6_equal(const void *a, const void *b) { return 0; }
const struct in6_addr in6addr_any = { { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } } };
const struct in6_addr in6addr_loopback = { { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } };
int udp6_output(void *so, void *m, void *saddr, void *daddr) { return 0; }
void icmp6_send_error(void *m, uint8_t type, uint8_t code) {}
void ndp_send_ns(void *slirp, struct in6_addr addr) {}
bool ndp_table_search(void *slirp, struct in6_addr ip_addr, uint8_t *out_ethaddr) { return 0; }
void tftp_input(void *srcsas, void *m) {}
