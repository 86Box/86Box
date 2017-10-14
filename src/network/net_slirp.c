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
 * Version:	@(#)net_slirp.c	1.0.8	2017/10/11
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
#include "../ibm.h"
#include "../config.h"
#include "../device.h"
#include "../plat.h"
#include "network.h"


static queueADT	slirpq;			/* SLiRP library handle */
static thread_t	*poll_tid;
static NETRXCB	poll_rx;		/* network RX function to call */
static void	*poll_arg;		/* network RX function arg */
static void	*slirpMutex;



static void
startslirp(void)
{
    thread_wait_mutex(slirpMutex);
}


static void
endslirp(void)
{
    thread_release_mutex(slirpMutex);
}


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


static struct
{
        int busy;
	int queue_in_use;

        event_t *wake_poll_thread;
        event_t *poll_complete;
        event_t *queue_not_in_use;
} poll_data;


void network_slirp_wait_for_poll()
{
        while (poll_data.busy)
                thread_wait_event(poll_data.poll_complete, -1);
        thread_reset_event(poll_data.poll_complete);
}


/* Handle the receiving of frames. */
static void
poll_thread(void *arg)
{
    struct queuepacket *qp;
    event_t *evt;

    pclog("SLiRP: polling thread started, arg %08lx\n", arg);

    /* Create a waitable event. */
    evt = thread_create_event();

    while (slirpq != NULL) {
	startslirp();

	network_slirp_wait_for_poll();

	/* See if there is any work. */
	slirp_tic();

	/* Wait for the next packet to arrive. */
	if (QueuePeek(slirpq) == 0) {
		/* If we did not get anything, wait a while. */
		thread_wait_event(evt, 10);
		continue;
	}

	/* Grab a packet from the queue. */
	qp = QueueDelete(slirpq);
#if 0
	pclog("SLiRP: inQ:%d  got a %dbyte packet @%08lx\n",
				QueuePeek(slirpq), qp->len, qp);
#endif

	if (poll_rx != NULL)
		poll_rx(poll_arg, (uint8_t *)&qp->data, qp->len); 

	/* Done with this one. */
	free(qp);

	endslirp();
    }

    thread_destroy_event(evt);
    evt = poll_tid = NULL;

    thread_close_mutex(slirpMutex);

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

    slirpMutex = thread_create_mutex(L"86Box.SLiRPMutex");

    poll_data.wake_poll_thread = thread_create_event();
    poll_data.poll_complete = thread_create_event();

    pclog("SLiRP: starting thread..\n");
    poll_tid = thread_create(poll_thread, mac);

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

        thread_close_mutex(slirpMutex);

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
	poll_data.busy = 1;

	slirp_input((const uint8_t *)pkt, pkt_len);

	poll_data.busy = 0;
	thread_set_event(poll_data.poll_complete);
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
