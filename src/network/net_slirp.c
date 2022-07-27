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
#include <wchar.h>
#include <slirp/libslirp.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/config.h>


/* SLiRP can use poll() or select() for socket polling.
   poll() is best on *nix but slow and limited on Windows. */
#ifndef _WIN32
# define SLIRP_USE_POLL 1
#endif
#ifdef SLIRP_USE_POLL
# ifdef _WIN32
#  include <winsock2.h>
#  define poll WSAPoll
# else
#  include <poll.h>
# endif
#endif


typedef struct {
    Slirp		*slirp;
    void		*mac;
    const netcard_t	*card; /* netcard attached to us */
    volatile thread_t	*poll_tid;
    event_t		*poll_state;
    uint8_t		stop;
#ifdef SLIRP_USE_POLL
    uint32_t		pfd_len, pfd_size;
    struct pollfd 	*pfd;
#else
    uint32_t		nfds;
    fd_set		rfds, wfds, xfds;
#endif
} slirp_t;

static slirp_t	*slirp;


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
#define slirp_log(fmt, ...)
#endif


static void
net_slirp_guest_error(const char *msg, void *opaque)
{
    slirp_log("SLiRP: guest_error(): %s\n", msg);
}


static int64_t
net_slirp_clock_get_ns(void *opaque)
{
    return (TIMER_USEC ? (tsc / (TIMER_USEC / 1000)) : 0);
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
}


static void
net_slirp_timer_mod(void *timer, int64_t expire_timer, void *opaque)
{
    timer_set_delay_u64(timer, expire_timer);
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
    slirp_t *slirp = (slirp_t *) opaque;
    uint8_t *mac = slirp->mac;
    uint32_t mac_cmp32[2];
    uint16_t mac_cmp16[2];

    if (!(slirp->card->set_link_state && slirp->card->set_link_state(slirp->card->priv)) && !(slirp->card->wait && slirp->card->wait(slirp->card->priv))) {
	slirp_log("SLiRP: received %d-byte packet\n", pkt_len);

	/* Received MAC. */
	mac_cmp32[0] = *(uint32_t *) (((uint8_t *) qp) + 6);
	mac_cmp16[0] = *(uint16_t *) (((uint8_t *) qp) + 10);

	/* Local MAC. */
	mac_cmp32[1] = *(uint32_t *) mac;
	mac_cmp16[1] = *(uint16_t *) (mac + 4);
	if ((mac_cmp32[0] != mac_cmp32[1]) ||
	    (mac_cmp16[0] != mac_cmp16[1])) {
		network_queue_put(0, slirp->card->priv, (uint8_t *) qp, pkt_len);
	}

	return pkt_len;
    } else {
	slirp_log("SLiRP: ignored %d-byte packet\n", pkt_len);
    }

    return 0;
}


static int
net_slirp_add_poll(int fd, int events, void *opaque)
{
    slirp_t *slirp = (slirp_t *) opaque;
#ifdef SLIRP_USE_POLL
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
	if (events & SLIRP_POLL_IN) pevents |= POLLIN;
	if (events & SLIRP_POLL_OUT) pevents |= POLLOUT;
# ifndef _WIN32
	/* Windows does not support some events. */
	if (events & SLIRP_POLL_ERR) pevents |= POLLERR;
	if (events & SLIRP_POLL_PRI) pevents |= POLLPRI;
	if (events & SLIRP_POLL_HUP) pevents |= POLLHUP;
# endif
	slirp->pfd[idx].events = pevents;
	return idx;
    } else
	return -1;
#else
    if (events & SLIRP_POLL_IN)
	FD_SET(fd, &slirp->rfds);
    if (events & SLIRP_POLL_OUT)
	FD_SET(fd, &slirp->wfds);
    if (events & SLIRP_POLL_PRI)
	FD_SET(fd, &slirp->xfds);
    if (fd > slirp->nfds)
	slirp->nfds = fd;
    return fd;
#endif
}


static int
net_slirp_get_revents(int idx, void *opaque)
{
    slirp_t *slirp = (slirp_t *) opaque;
    int ret = 0;
#ifdef SLIRP_USE_POLL
    int events = slirp->pfd[idx].revents;
    if (events & POLLIN) ret |= SLIRP_POLL_IN;
    if (events & POLLOUT) ret |= SLIRP_POLL_OUT;
    if (events & POLLPRI) ret |= SLIRP_POLL_PRI;
    if (events & POLLERR) ret |= SLIRP_POLL_ERR;
    if (events & POLLHUP) ret |= SLIRP_POLL_HUP;
#else
    if (FD_ISSET(idx, &slirp->rfds))
	ret |= SLIRP_POLL_IN;
    if (FD_ISSET(idx, &slirp->wfds))
	ret |= SLIRP_POLL_OUT;
    if (FD_ISSET(idx, &slirp->xfds))
	ret |= SLIRP_POLL_PRI;
#endif
    return ret;
}


static void
slirp_tic(slirp_t *slirp)
{
    int ret;
    uint32_t tmo;

    /* Let SLiRP create a list of all open sockets. */
#ifdef SLIRP_USE_POLL
    tmo = -1;
    slirp->pfd_len = 0;
#else
    slirp->nfds = -1;
    FD_ZERO(&slirp->rfds);
    FD_ZERO(&slirp->wfds);
    FD_ZERO(&slirp->xfds);
#endif
    slirp_pollfds_fill(slirp->slirp, &tmo, net_slirp_add_poll, slirp);

    /* Now wait for something to happen, or at most 'tmo' usec. */
#ifdef SLIRP_USE_POLL
    ret = poll(slirp->pfd, slirp->pfd_len, tmo);
#else
    if (tmo < 0)
	tmo = 500;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = tmo;

    ret = select(slirp->nfds + 1, &slirp->rfds, &slirp->wfds, &slirp->xfds, &tv);
#endif

    /* If something happened, let SLiRP handle it. */
    slirp_pollfds_poll(slirp->slirp, (ret <= 0), net_slirp_get_revents, slirp);
}


static const SlirpCb slirp_cb = {
    .send_packet = net_slirp_send_packet,
    .guest_error = net_slirp_guest_error,
    .clock_get_ns = net_slirp_clock_get_ns,
    .timer_new = net_slirp_timer_new,
    .timer_free = net_slirp_timer_free,
    .timer_mod = net_slirp_timer_mod,
    .register_poll_fd = net_slirp_register_poll_fd,
    .unregister_poll_fd = net_slirp_unregister_poll_fd,
    .notify = net_slirp_notify
};


/* Handle the receiving of frames. */
static void
poll_thread(void *arg)
{
    slirp_t *slirp = (slirp_t *) arg;
    event_t *evt;
    int tx;

    slirp_log("SLiRP: initializing...\n");

    /* Set the IP addresses to use. */
    struct in_addr net  = { .s_addr = htonl(0x0a000200) }; /* 10.0.2.0 */
    struct in_addr mask = { .s_addr = htonl(0xffffff00) }; /* 255.255.255.0 */
    struct in_addr host = { .s_addr = htonl(0x0a000202) }; /* 10.0.2.2 */
    struct in_addr dhcp = { .s_addr = htonl(0x0a00020f) }; /* 10.0.2.15 */
    struct in_addr dns  = { .s_addr = htonl(0x0a000203) }; /* 10.0.2.3 */
    struct in_addr bind = { .s_addr = htonl(0x00000000) }; /* 0.0.0.0 */
    struct in6_addr ipv6_dummy = { 0 }; /* contents don't matter; we're not using IPv6 */

    /* Initialize SLiRP. */
    slirp->slirp = slirp_init(0, 1, net, mask, host, 0, ipv6_dummy, 0, ipv6_dummy, NULL, NULL, NULL, NULL, dhcp, dns, ipv6_dummy, NULL, NULL, &slirp_cb, arg);
    if (!slirp->slirp) {
	slirp_log("SLiRP: initialization failed\n");
	return;
    }

    /* Set up port forwarding. */
    int udp, external, internal, i = 0;
    char *category = "SLiRP Port Forwarding";
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

    /* Start polling. */
    slirp_log("SLiRP: polling started.\n");
    thread_set_event(slirp->poll_state);

    /* Create a waitable event. */
    evt = thread_create_event();

    while (!slirp->stop) {
	/* Request ownership of the queue. */
	network_wait(1);

	/* Stop processing if asked to. */
	if (slirp->stop) {
		network_wait(0);
		break;
	}

	/* See if there is any work. */
	slirp_tic(slirp);

	/* Wait for the next packet to arrive - network_do_tx() is called from there. */
	tx = network_tx_queue_check();

	/* Release ownership of the queue. */
	network_wait(0);

	/* If we did not get anything, wait a while. */
	if (!tx)
		thread_wait_event(evt, 10);
    }

    /* No longer needed. */
    if (evt)
	thread_destroy_event(evt);

    slirp_log("SLiRP: polling stopped.\n");
    thread_set_event(slirp->poll_state);

    /* Destroy event here to avoid a crash. */
    slirp_log("SLiRP: thread ended\n");
    thread_destroy_event(slirp->poll_state);
    /* Free here instead of immediately freeing the global slirp on the main
       thread to avoid a race condition. */
    slirp_cleanup(slirp->slirp);
    free(slirp);
}


/* Initialize SLiRP for use. */
int
net_slirp_init(void)
{
    return 0;
}


/* Initialize SLiRP for use. */
int
net_slirp_reset(const netcard_t *card, uint8_t *mac)
{
    slirp_t *new_slirp = malloc(sizeof(slirp_t));
    memset(new_slirp, 0, sizeof(slirp_t));
    new_slirp->mac = mac;
    new_slirp->card = card;
#ifdef SLIRP_USE_POLL
    new_slirp->pfd_size = 16 * sizeof(struct pollfd);
    new_slirp->pfd = malloc(new_slirp->pfd_size);
    memset(new_slirp->pfd, 0, new_slirp->pfd_size);
#endif

    /* Save the callback info. */
    slirp = new_slirp;

    slirp_log("SLiRP: creating thread...\n");
    slirp->poll_state = thread_create_event();
    slirp->poll_tid = thread_create(poll_thread, new_slirp);
    thread_wait_event(slirp->poll_state, -1);

    return 0;
}


void
net_slirp_close(void)
{
    if (!slirp)
	return;

    slirp_log("SLiRP: closing\n");

    /* Tell the polling thread to shut down. */
    slirp->stop = 1;

    /* Tell the thread to terminate. */
    if (slirp->poll_tid) {
	/* Wait for the thread to finish. */
	slirp_log("SLiRP: waiting for thread to end...\n");
	thread_wait_event(slirp->poll_state, -1);
    }

    /* Shutdown work is done by the thread on its local copy of slirp. */
    slirp = NULL;
}


/* Send a packet to the SLiRP interface. */
void
net_slirp_in(uint8_t *pkt, int pkt_len)
{
    if (!slirp || !slirp->slirp)
	return;

    slirp_log("SLiRP: sending %d-byte packet\n", pkt_len);

    slirp_input(slirp->slirp, (const uint8_t *) pkt, pkt_len);
}


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
