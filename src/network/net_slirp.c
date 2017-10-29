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
 * Version:	@(#)net_slirp.c	1.0.11	2017/10/28
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "slirp/slirp.h"
#include "slirp/queue.h"
#include "../86box.h"
#include "../ibm.h"
#include "../config.h"
#include "../device.h"
#include "../plat.h"
#include "network.h"


static volatile queueADT	slirpq;		/* SLiRP library handle */
static volatile thread_t	*poll_tid;
static netcard_t		*poll_card;	/* netcard attached to us */
static event_t			*poll_state;


static void
slirp_tic(void)
{
    int ret2, nfds;
    struct timeval tv;
    fd_set rfds, wfds, xfds;
    int tmo;

    /* Let SLiRP create a list of all open sockets. */
    nfds = -1;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&xfds);
    tmo = slirp_select_fill(&nfds, &rfds, &wfds, &xfds); /* this can crash */
    if (tmo < 0)
	tmo = 500;

    tv.tv_sec = 0;
    tv.tv_usec = tmo;

    /* Now wait for something to happen, or at most 'tmo' usec. */
    ret2 = select(nfds+1, &rfds, &wfds, &xfds, &tv);

    /* If something happened, let SLiRP handle it. */
    if (ret2 >= 0)
	slirp_select_poll(&rfds, &wfds, &xfds);
}


/* Handle the receiving of frames. */
static void
poll_thread(UNUSED(void *arg))
{
    struct queuepacket *qp;
    event_t *evt;

    pclog("SLiRP: polling started.\n");
    thread_set_event(poll_state);

    /* Create a waitable event. */
    evt = thread_create_event();

    while (slirpq != NULL) {
	/* Request ownership of the queue. */
	network_wait(1);

	/* Wait for a poll request. */
	network_poll();

	/* See if there is any work. */
	slirp_tic();

	/* Wait for the next packet to arrive. */
	if (QueuePeek(slirpq) != 0) {
		/* Grab a packet from the queue. */
		qp = QueueDelete(slirpq);
#if 0
		pclog("SLiRP: inQ:%d  got a %dbyte packet @%08lx\n",
				QueuePeek(slirpq), qp->len, qp);
#endif

		poll_card->rx(poll_card->priv, (uint8_t *)qp->data, qp->len); 

		/* Done with this one. */
		free(qp);
	} else {
		/* If we did not get anything, wait a while. */
		thread_wait_event(evt, 10);
	}

	/* Release ownership of the queue. */
	network_wait(0);
    }

    /* No longer needed. */
    thread_destroy_event(evt);

    pclog("SLiRP: polling stopped.\n");
    thread_set_event(poll_state);
}


/* Initialize SLiRP for use. */
int
net_slirp_init(void)
{
    pclog("SLiRP: initializing..\n");

    if (slirp_init() != 0) {
	pclog("SLiRP could not be initialized!\n");
	return(-1);
    }

    slirpq = QueueCreate();

    poll_tid = NULL;
    poll_state = NULL;
    poll_card = NULL;

    return(0);
}


/* Initialize SLiRP for use. */
int
net_slirp_reset(netcard_t *card)
{
    /* Save the callback info. */
    poll_card = card;

    pclog("SLiRP: creating thread..\n");
    poll_state = thread_create_event();
    poll_tid = thread_create(poll_thread, card->mac);
    thread_wait_event(poll_state, -1);

    return(0);
}


void
net_slirp_close(void)
{
    queueADT sl;

    if (slirpq == NULL) return;

    pclog("SLiRP: closing.\n");

    /* Tell the polling thread to shut down. */
    sl = slirpq; slirpq = NULL;

    /* Tell the thread to terminate. */
    if (poll_tid != NULL) {
	network_busy(0);

	/* Wait for the thread to finish. */
	pclog("SLiRP: waiting for thread to end...\n");
	thread_wait_event(poll_state, -1);
	pclog("SLiRP: thread ended\n");
	thread_destroy_event(poll_state);

	poll_tid = NULL;
	poll_state = NULL;
	poll_card = NULL;
    }

    /* OK, now shut down SLiRP itself. */
    QueueDestroy(sl);
    slirp_exit(0);
}


/* Send a packet to the SLiRP interface. */
void
net_slirp_in(uint8_t *pkt, int pkt_len)
{
    if (slirpq == NULL) return;

    network_busy(1);

    slirp_input((const uint8_t *)pkt, pkt_len);

    network_busy(0);
}


/* Needed by SLiRP library. */
void
slirp_output(const uint8_t *pkt, int pkt_len)
{
    struct queuepacket *qp;

    if (slirpq != NULL) {
	qp = (struct queuepacket *)malloc(sizeof(struct queuepacket));
	qp->len = pkt_len;
	memcpy(qp->data, pkt, pkt_len);
	QueueEnter(slirpq, qp);
    }
}


/* Needed by SLiRP library. */
int
slirp_can_output(void)
{
    return((slirpq != NULL)?1:0);
}
