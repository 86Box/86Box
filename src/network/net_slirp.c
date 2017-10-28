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
 * Version:	@(#)net_slirp.c	1.0.10	2017/10/16
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


static volatile
       queueADT	slirpq;			/* SLiRP library handle */
static volatile
       thread_t	*poll_tid;
static volatile
       NETRXCB	poll_rx;		/* network RX function to call */
static volatile
       void	*poll_arg;		/* network RX function arg */
static volatile
       event_t	*thread_started;



/* Instead of calling this and crashing some times
   or experencing jitter, this is called by the 
   60Hz clock which seems to do the job. */
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
poll_thread(void *arg)
{
    struct queuepacket *qp;
    event_t *evt;

   thread_set_event((event_t *) thread_started);

    pclog("SLiRP: polling thread started, arg %08lx\n", arg);

    /* Create a waitable event. */
    evt = thread_create_event();

    while (slirpq != NULL) {
	network_mutex_wait(1);

	network_wait_for_poll();

	/* See if there is any work. */
	slirp_tic();

	/* Wait for the next packet to arrive. */
	if (QueuePeek(slirpq) == 0) {
		/* If we did not get anything, wait a while. */
		thread_wait_event(evt, 10);

		network_mutex_wait(0);
		continue;
	}

	/* Grab a packet from the queue. */
	qp = QueueDelete(slirpq);
#if 0
	pclog("SLiRP: inQ:%d  got a %dbyte packet @%08lx\n",
				QueuePeek(slirpq), qp->len, qp);
#endif

	if (poll_rx != NULL)
		poll_rx((void *) poll_arg, (uint8_t *)&qp->data, qp->len); 

	/* Done with this one. */
	free(qp);

	network_mutex_wait(0);
    }

    thread_destroy_event(evt);

    pclog("SLiRP: polling stopped.\n");
}


/* Initialize SLiRP for us. */
int
network_slirp_setup(uint8_t *mac, NETRXCB func, void *arg)
{
    pclog("SLiRP: initializing..\n");

    if (slirp_init() != 0) {
	pclog("SLiRP could not be initialized!\n");
	return(-1);
    }

    slirpq = QueueCreate();
    pclog(" Packet queue is at %08lx\n", &slirpq);

    /* Save the callback info. */
    poll_rx = func;
    poll_arg = arg;

    network_thread_init();

    pclog("SLiRP: starting thread..\n");
    poll_tid = thread_create(poll_thread, mac);

    thread_start = thread_create_event();

    thread_wait_event((event_t *) thread_started, -1);

    return(0);
}


void
network_slirp_close(void)
{
    queueADT sl;

    if (slirpq != NULL) {
	pclog("Closing SLiRP\n");

	/* Tell the polling thread to shut down. */
	sl = slirpq; slirpq = NULL;

#if 0
#if 1
	/* Terminate the polling thread. */
	if (poll_tid != NULL) {
		thread_kill(poll_tid);
		poll_tid = NULL;
	}
#else
	/* Wait for the polling thread to shut down. */
	while (poll_tid != NULL)
		;
#endif
#endif

        /* Tell the thread to terminate. */
	if (poll_tid != NULL) {
		network_busy(0);

		pclog("Waiting for network thread to end...\n");
		/* Wait for the end event. */
		network_wait_for_end((void *) poll_tid);
		pclog("Network thread ended\n");

		poll_tid = NULL;
	}

	if (thread_started) {
		thread_destroy_event((event_t *) thread_started);
		thread_started = NULL;
	}

	/* OK, now shut down SLiRP itself. */
	QueueDestroy(sl);
	slirp_exit(0);
    }

    poll_rx = NULL;
    poll_arg = NULL;
}


/* Test SLiRP - 1 = success, 0 = failure. */
int
network_slirp_test(void)
{
    if (slirp_init() != 0) {
	pclog("SLiRP could not be initialized!\n");
	return 0;
    }
    else
    {
	slirp_exit(0);
	return 1;
    }
}


/* Send a packet to the SLiRP interface. */
void
network_slirp_in(uint8_t *pkt, int pkt_len)
{
    if (slirpq != NULL) {
	network_busy(1);

	slirp_input((const uint8_t *)pkt, pkt_len);

	network_busy(0);
    }
}


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


int
slirp_can_output(void)
{
    return((slirpq != NULL)?1:0);
}
