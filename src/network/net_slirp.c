/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Handle SLiRP library processing.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *
 *		Redistribution and  use  in source  and binary forms, with
 *		or  without modification, are permitted  provided that the
 *		following conditions are met:
 *
 *		1. Redistributions of  source  code must retain the entire
 *		   above notice, this list of conditions and the following
 *		   disclaimer.
 *
 *		2. Redistributions in binary form must reproduce the above
 *		   copyright  notice,  this list  of  conditions  and  the
 *		   following disclaimer in  the documentation and/or other
 *		   materials provided with the distribution.
 *
 *		3. Neither the  name of the copyright holder nor the names
 *		   of  its  contributors may be used to endorse or promote
 *		   products  derived from  this  software without specific
 *		   prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <slirp/slirp.h>
#include <slirp/queue.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/network.h>


static volatile queueADT	slirpq;		/* SLiRP library handle */
static volatile thread_t	*poll_tid;
static const netcard_t		*poll_card;	/* netcard attached to us */
static event_t			*poll_state;


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
    uint8_t *mac = (uint8_t *)arg;
    struct queuepacket *qp;
    uint32_t mac_cmp32[2];
    uint16_t mac_cmp16[2];
    event_t *evt;
    int data_valid = 0;

    slirp_log("SLiRP: polling started.\n");
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

	/* Our queue may have been nuked.. */
	if (slirpq == NULL) break;

	/* Wait for the next packet to arrive. */
	data_valid = 0;

	if ((!network_get_wait() && !(poll_card->set_link_state && poll_card->set_link_state(poll_card->priv)) && !(poll_card->wait && poll_card->wait(poll_card->priv))) && (QueuePeek(slirpq) != 0)) {
		/* Grab a packet from the queue. */
		// ui_sb_update_icon(SB_NETWORK, 1);

		qp = QueueDelete(slirpq);
		slirp_log("SLiRP: inQ:%d  got a %dbyte packet @%08lx\n",
				QueuePeek(slirpq), qp->len, qp);

		/* Received MAC. */
		mac_cmp32[0] = *(uint32_t *)(((uint8_t *)qp->data)+6);
		mac_cmp16[0] = *(uint16_t *)(((uint8_t *)qp->data)+10);

		/* Local MAC. */
		mac_cmp32[1] = *(uint32_t *)mac;
		mac_cmp16[1] = *(uint16_t *)(mac+4);
		if ((mac_cmp32[0] != mac_cmp32[1]) ||
		    (mac_cmp16[0] != mac_cmp16[1])) {

			poll_card->rx(poll_card->priv, (uint8_t *)qp->data, qp->len); 
			data_valid = 1;
		}

		/* Done with this one. */
		free(qp);
	}

	/* If we did not get anything, wait a while. */
	if (!data_valid) {
		// ui_sb_update_icon(SB_NETWORK, 0);
		thread_wait_event(evt, 10);
	}

	/* Release ownership of the queue. */
	network_wait(0);
    }

    /* No longer needed. */
    if (evt != NULL)
	thread_destroy_event(evt);

    slirp_log("SLiRP: polling stopped.\n");
    thread_set_event(poll_state);
}


/* Initialize SLiRP for use. */
int
net_slirp_init(void)
{
    slirp_log("SLiRP: initializing..\n");

    if (slirp_init() != 0) {
	slirp_log("SLiRP could not be initialized!\n");
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
net_slirp_reset(const netcard_t *card, uint8_t *mac)
{
    // ui_sb_update_icon(SB_NETWORK, 0);

    /* Save the callback info. */
    poll_card = card;

    slirp_log("SLiRP: creating thread..\n");
    poll_state = thread_create_event();
    poll_tid = thread_create(poll_thread, mac);
    thread_wait_event(poll_state, -1);

    return(0);
}


void
net_slirp_close(void)
{
    queueADT sl;

    // ui_sb_update_icon(SB_NETWORK, 0);

    if (slirpq == NULL) return;

    slirp_log("SLiRP: closing.\n");

    /* Tell the polling thread to shut down. */
    sl = slirpq; slirpq = NULL;

    /* Tell the thread to terminate. */
    if (poll_tid != NULL) {
	network_busy(0);

	/* Wait for the thread to finish. */
	slirp_log("SLiRP: waiting for thread to end...\n");
	thread_wait_event(poll_state, -1);
	slirp_log("SLiRP: thread ended\n");
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

    // ui_sb_update_icon(SB_NETWORK, 1);

    network_busy(1);

    slirp_input((const uint8_t *)pkt, pkt_len);

    network_busy(0);

    // ui_sb_update_icon(SB_NETWORK, 0);
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
