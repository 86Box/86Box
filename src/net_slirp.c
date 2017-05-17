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
 * Version:	@(#)net_slirp.c	1.0.1	2017/05/11
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "slirp/slirp.h"
#include "slirp/queue.h"
#include "ibm.h"
#include "config.h"
#include "device.h"
#include "thread.h"
#include "network.h"


static queueADT	slirpq;			/* SLiRP library handle */
static thread_t	*poll_tid;
static NETRXCB	poll_rx;		/* network RX function to call */
static void	*poll_arg;		/* network RX function arg */


#ifdef WALTJE
int slirp_do_log = 1;
# define ENABLE_SLIRP_LOG
#else
int slirp_do_log = 0;
#endif


static void
slirp_log(const char *format, ...)
{
#ifdef ENABLE_SLIRP_LOG
    va_list ap;

    if (slirp_do_log) {
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	fflush(stdout);
    }
#endif
}
#define pclog	slirp_log


/* Instead of calling this and crashing some times
   or experencing jitter, this is called by the 
   60Hz clock which seems to do the job. */
static void
slirp_tic(void)
{
    int ret2,nfds;
    struct timeval tv;
    fd_set rfds, wfds, xfds;
    int tmo;
    nfds=-1;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&xfds);
    tmo = slirp_select_fill(&nfds, &rfds, &wfds, &xfds); /* this can crash */
    if (tmo < 0) {
	tmo = 500;
    }

    tv.tv_sec = 0;
    tv.tv_usec = tmo;	/* basilisk default 10000 */

    ret2 = select(nfds+1, &rfds, &wfds, &xfds, &tv);
    if (ret2 >= 0) {
	slirp_select_poll(&rfds, &wfds, &xfds);
    }
}


/* Check if the interface has a packet for us. */
static void
poll_thread(void *arg)
{
    uint8_t *mac = (uint8_t *)arg;
    struct queuepacket *qp;
    event_t *evt;

    pclog("SLiRP: polling thread started, arg %08lx\n", arg);

    /* Create a waitable event. */
    evt = thread_create_event();
    pclog("SLiRP: poll event is %08lx\n", evt);

    while (slirpq != NULL) {
	slirp_tic();

	/* Wait for the next packet to arrive. */
	if (QueuePeek(slirpq) == 0) {
		/* If we did not get anything, wait a while. */
		thread_wait_event(evt, 10);
		continue;
	}

	/* Grab a packet from the queue. */
	qp = QueueDelete(slirpq);
	pclog("SLiRP: inQ:%d  got a %dbyte packet @%08lx\n",
				QueuePeek(slirpq), qp->len, qp);

	if (poll_rx != NULL)
		poll_rx(poll_arg, (uint8_t *)&qp->data, qp->len); 

	/* Done with this one. */
	free(qp);
    }

    thread_destroy_event(evt);
    poll_tid = NULL;

    pclog("SLiRP: polling stopped.\n");
}


/* Initialize SLiRP for us. */
int
network_slirp_setup(uint8_t *mac, NETRXCB func, void *arg)
{
    int rc;

    pclog("Initializing SLiRP\n");

    if (slirp_init() != 0) {
	pclog("SLiRP could not be initialized!\n");
	return(-1);
    }

    slirpq = QueueCreate();
    pclog(" Packet queue is at %08lx\n", &slirpq);

    /* Save the callback info. */
    poll_rx = func;
    poll_arg = arg;

    pclog("SLiRP: creating thread..\n");
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

	/* OK, now shut down SLiRP itself. */
	QueueDestroy(sl);
	slirp_exit(0);
    }

    poll_rx = NULL;
    poll_arg = NULL;
}


/* Send a packet to the SLiRP interface. */
void
network_slirp_in(uint8_t *pkt, int pkt_len)
{
    if (slirpq != NULL)
	slirp_input((const uint8_t *)pkt, pkt_len);
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
