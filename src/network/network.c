/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the network module.
 *
 * NOTE		The definition of the netcard_t is currently not optimal;
 *		it should be malloc'ed and then linked to the NETCARD def.
 *		Will be done later.
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
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <time.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/network.h>
#include <86box/net_3c503.h>
#include <86box/net_ne2000.h>
#include <86box/net_pcnet.h>
#include <86box/net_plip.h>
#include <86box/net_wd8003.h>


static netcard_t net_cards[] = {
    { "none",		NULL,				NULL	},
    { "3c503",		&threec503_device,		NULL	},
    { "pcnetisa",	&pcnet_am79c960_device, 	NULL	},
    { "pcnetisaplus",	&pcnet_am79c961_device, 	NULL	},
    { "ne1k",		&ne1000_device,			NULL	},
    { "ne2k",		&ne2000_device,			NULL	},
    { "pcnetracal",	&pcnet_am79c960_eb_device,	NULL	},
    { "ne2kpnp",	&rtl8019as_device,		NULL	},
    { "wd8003e",	&wd8003e_device,		NULL	},
    { "wd8003eb",	&wd8003eb_device,		NULL	},
    { "wd8013ebt",	&wd8013ebt_device,		NULL	},
    { "plip",		&plip_device,			NULL	},
    { "ethernextmc",	&ethernext_mc_device,		NULL	},
    { "wd8003eta",	&wd8003eta_device,		NULL	},
    { "wd8003ea",	&wd8003ea_device,		NULL	},
    { "pcnetfast",	&pcnet_am79c973_device,		NULL	},
    { "pcnetpci",	&pcnet_am79c970a_device,	NULL	},
    { "ne2kpci",	&rtl8029as_device,		NULL	},
    { "pcnetvlb",	&pcnet_am79c960_vlb_device,	NULL	},
    { "",		NULL,				NULL	}
};


/* Global variables. */
int		network_type;
int		network_ndev;
int		network_card;
char		network_host[522];
netdev_t	network_devs[32];
int		network_rx_pause = 0,
		network_tx_pause = 0;
#ifdef ENABLE_NIC_LOG
int		nic_do_log = ENABLE_NIC_LOG;
#endif


/* Local variables. */
static volatile atomic_int	net_wait = 0;
static mutex_t		*network_mutex;
static uint8_t		*network_mac;
static uint8_t		network_timer_active = 0;
static pc_timer_t	network_rx_queue_timer;
static netpkt_t		*first_pkt[2] = { NULL, NULL },
			*last_pkt[2] = { NULL, NULL };


static struct {
    volatile int	busy,
			queue_in_use;

    event_t		*wake_poll_thread,
			*poll_complete,
			*queue_not_in_use;
} poll_data;


#ifdef ENABLE_NETWORK_LOG
int network_do_log = ENABLE_NETWORK_LOG;
static FILE *network_dump = NULL;
static mutex_t *network_dump_mutex;


static void
network_log(const char *fmt, ...)
{
    va_list ap;

    if (network_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}


static void
network_dump_packet(netpkt_t *pkt)
{
    if (!network_dump)
	return;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct {
	uint32_t ts_sec, ts_usec, incl_len, orig_len;
    } pcap_packet_hdr = {
	tv.tv_sec, tv.tv_usec, pkt->len, pkt->len
    };

    if (network_dump_mutex)
	thread_wait_mutex(network_dump_mutex);

    size_t written;
    if ((written = fwrite(&pcap_packet_hdr, 1, sizeof(pcap_packet_hdr), network_dump)) < sizeof(pcap_packet_hdr)) {
	network_log("NETWORK: failed to write dump packet header\n");
	fseek(network_dump, -written, SEEK_CUR);
    } else {
	if ((written = fwrite(pkt->data, 1, pkt->len, network_dump)) < pkt->len) {
		network_log("NETWORK: failed to write dump packet data\n");
		fseek(network_dump, -written - sizeof(pcap_packet_hdr), SEEK_CUR);
	}
	fflush(network_dump);
    }

    if (network_dump_mutex)
	thread_release_mutex(network_dump_mutex);
}
#else
#define network_log(fmt, ...)
#define network_dump_packet(pkt)
#endif


void
network_wait(uint8_t wait)
{
    if (wait)
	thread_wait_mutex(network_mutex);
      else
	thread_release_mutex(network_mutex);
}


void
network_poll(void)
{
    while (poll_data.busy)
	thread_wait_event(poll_data.wake_poll_thread, -1);

    thread_reset_event(poll_data.wake_poll_thread);
}


void
network_busy(uint8_t set)
{
    poll_data.busy = !!set;

    if (! set)
	thread_set_event(poll_data.wake_poll_thread);
}


void
network_end(void)
{
    thread_set_event(poll_data.poll_complete);
}


/*
 * Initialize the configured network cards.
 *
 * This function gets called only once, from the System
 * Platform initialization code (currently in pc.c) to
 * set our local stuff to a known state.
 */
void
network_init(void)
{
    int i;

    /* Initialize to a known state. */
    network_type = NET_TYPE_NONE;
    network_card = 0;

    /* Create a first device entry that's always there, as needed by UI. */
    strcpy(network_devs[0].device, "none");
    strcpy(network_devs[0].description, "None");
    network_ndev = 1;

    /* Initialize the Pcap system module, if present. */
    i = net_pcap_prepare(&network_devs[network_ndev]);
    if (i > 0)
	network_ndev += i;

#ifdef ENABLE_NETWORK_LOG
    /* Start packet dump. */
    network_dump = fopen("network.pcap", "wb");

    struct {
	uint32_t magic_number;
	uint16_t version_major, version_minor;
	int32_t	 thiszone;
	uint32_t sigfigs, snaplen, network;
    } pcap_hdr = {
	0xa1b2c3d4,
	2, 4,
	0,
	0, 65535, 1
    };
    fwrite(&pcap_hdr, sizeof(pcap_hdr), 1, network_dump);
    fflush(network_dump);
#endif
}


void
network_queue_put(int tx, void *priv, uint8_t *data, int len)
{
    netpkt_t *temp;

    temp = (netpkt_t *) malloc(sizeof(netpkt_t));
    memset(temp, 0, sizeof(netpkt_t));
    temp->priv = priv;
    memcpy(temp->data, data, len);
    temp->len = len;
    temp->prev = last_pkt[tx];
    temp->next = NULL;

    if (last_pkt[tx] != NULL)
	last_pkt[tx]->next = temp;
    last_pkt[tx] = temp;

    if (first_pkt[tx] == NULL)
	first_pkt[tx] = temp;
}


static void
network_queue_get(int tx, netpkt_t **pkt)
{
    if (first_pkt[tx] == NULL)
	*pkt = NULL;
    else
	*pkt = first_pkt[tx];
}


static void
network_queue_advance(int tx)
{
    netpkt_t *temp;

    temp = first_pkt[tx];

    if (temp == NULL)
	return;

    first_pkt[tx] = temp->next;
    free(temp);

    if (first_pkt[tx] == NULL)
	last_pkt[tx] = NULL;
}


static void
network_queue_clear(int tx)
{
    netpkt_t *temp = first_pkt[tx], *temp2;

    if (temp == NULL)
	return;

    do {
	temp2 = temp->next;
	free(temp);
	temp = temp2;
    } while (temp != NULL);

    first_pkt[tx] = last_pkt[tx] = NULL;
}


static void
network_rx_queue(void *priv)
{
    int ret = 1;

    if (network_rx_pause) {
	timer_on_auto(&network_rx_queue_timer, 0.762939453125 * 2.0 * 128.0);
	return;
    }

    netpkt_t *pkt = NULL;

    network_busy(1);

    network_queue_get(0, &pkt);
    if ((pkt != NULL) && (pkt->len > 0)) {
	network_dump_packet(pkt);
	ret = net_cards[network_card].rx(pkt->priv, pkt->data, pkt->len);
	if (pkt->len >= 128)
		timer_on_auto(&network_rx_queue_timer, 0.762939453125 * 2.0 * ((double) pkt->len));
	else
		timer_on_auto(&network_rx_queue_timer, 0.762939453125 * 2.0 * 128.0);
    } else
	timer_on_auto(&network_rx_queue_timer, 0.762939453125 * 2.0 * 128.0);
    if (ret)
	network_queue_advance(0);

    network_busy(0);
}


/*
 * Attach a network card to the system.
 *
 * This function is called by a hardware driver ("card") after it has
 * finished initializing itself, to link itself to the platform support
 * modules.
 */
void
network_attach(void *dev, uint8_t *mac, NETRXCB rx, NETWAITCB wait, NETSETLINKSTATE set_link_state)
{
    if (network_card == 0) return;

    /* Save the card's info. */
    net_cards[network_card].priv = dev;
    net_cards[network_card].rx = rx;
    net_cards[network_card].wait = wait;
    net_cards[network_card].set_link_state = set_link_state;
    network_mac = mac;

    network_set_wait(0);

    /* Create the network events. */
    poll_data.poll_complete = thread_create_event();
    poll_data.wake_poll_thread = thread_create_event();

    /* Activate the platform module. */
    switch(network_type) {
	case NET_TYPE_PCAP:
		(void)net_pcap_reset(&net_cards[network_card], network_mac);
		break;

	case NET_TYPE_SLIRP:
		(void)net_slirp_reset(&net_cards[network_card], network_mac);
		break;
    }

    first_pkt[0] = first_pkt[1] = NULL;
    last_pkt[0] = last_pkt[1] = NULL;
    memset(&network_rx_queue_timer, 0x00, sizeof(pc_timer_t));
    timer_add(&network_rx_queue_timer, network_rx_queue, NULL, 0);
    /* 10 mbps. */
    timer_on_auto(&network_rx_queue_timer, 0.762939453125 * 2.0);
    network_timer_active = 1;
}


/* Stop the network timer. */
void
network_timer_stop(void)
{
    if (network_timer_active) {
	timer_stop(&network_rx_queue_timer);
	memset(&network_rx_queue_timer, 0x00, sizeof(pc_timer_t));
	network_timer_active = 0;
    }
}


/* Stop any network activity. */
void
network_close(void)
{
    network_timer_stop();

    /* If already closed, do nothing. */
    if (network_mutex == NULL) return;

    /* Force-close the PCAP module. */
    net_pcap_close();

    /* Force-close the SLIRP module. */
    net_slirp_close();
 
    /* Close the network events. */
    if (poll_data.wake_poll_thread != NULL) {
	thread_destroy_event(poll_data.wake_poll_thread);
	poll_data.wake_poll_thread = NULL;
    }
    if (poll_data.poll_complete != NULL) {
	thread_destroy_event(poll_data.poll_complete);
	poll_data.poll_complete = NULL;
    }

    /* Close the network thread mutex. */
    thread_close_mutex(network_mutex);
    network_mutex = NULL;
    network_mac = NULL;
#ifdef ENABLE_NETWORK_LOG
    thread_close_mutex(network_dump_mutex);
    network_dump_mutex = NULL;
#endif

    /* Here is where we clear the queues. */
    network_queue_clear(0);
    network_queue_clear(1);

    network_log("NETWORK: closed.\n");
}


/*
 * Reset the network card(s).
 *
 * This function is called each time the system is reset,
 * either a hard reset (including power-up) or a soft reset
 * including C-A-D reset.)  It is responsible for connecting
 * everything together.
 */
void
network_reset(void)
{
    int i = -1;

#ifdef ENABLE_NIC_LOG
    network_log("NETWORK: reset (type=%d, card=%d) debug=%d\n",
			network_type, network_card, nic_do_log);
#else
    network_log("NETWORK: reset (type=%d, card=%d)\n",
				network_type, network_card);
#endif
    ui_sb_update_icon(SB_NETWORK, 0);

    /* Just in case.. */
    network_close();

    /* If no active card, we're done. */
    if ((network_type==NET_TYPE_NONE) || (network_card==0)) return;

    network_mutex = thread_create_mutex();
#ifdef ENABLE_NETWORK_LOG
    network_dump_mutex = thread_create_mutex();
#endif

    /* Initialize the platform module. */
    switch(network_type) {
	case NET_TYPE_PCAP:
		i = net_pcap_init();
		break;

	case NET_TYPE_SLIRP:
		i = net_slirp_init();
		break;
    }

    if (i < 0) {
	/* Tell user we can't do this (at the moment.) */
	ui_msgbox_header(MBX_ERROR, (wchar_t *) IDS_2093, (wchar_t *) IDS_2129);

	// FIXME: we should ask in the dialog if they want to
	//	  reconfigure or quit, and throw them into the
	//	  Settings dialog if yes.

	/* Disable network. */
	network_type = NET_TYPE_NONE;

	return;
    }

    network_log("NETWORK: set up for %s, card='%s'\n",
	(network_type==NET_TYPE_SLIRP)?"SLiRP":"Pcap",
			net_cards[network_card].name);

    /* Add the (new?) card to the I/O system. */
    if (net_cards[network_card].device) {
	network_log("NETWORK: adding device '%s'\n",
		net_cards[network_card].name);
	device_add(net_cards[network_card].device);
    }
}


/* Queue a packet for transmission to one of the network providers. */
void
network_tx(uint8_t *bufp, int len)
{
    network_busy(1);

    ui_sb_update_icon(SB_NETWORK, 1);

    network_queue_put(1, NULL, bufp, len);

    ui_sb_update_icon(SB_NETWORK, 0);

    network_busy(0);
}


/* Actually transmit the packet. */
void
network_do_tx(void)
{
    netpkt_t *pkt = NULL;

    if (network_tx_pause)
	return;

    network_queue_get(1, &pkt);
    if ((pkt != NULL) && (pkt->len > 0)) {
	network_dump_packet(pkt);
	switch(network_type) {
		case NET_TYPE_PCAP:
			net_pcap_in(pkt->data, pkt->len);
			break;

		case NET_TYPE_SLIRP:
			net_slirp_in(pkt->data, pkt->len);
			break;
	}
    }
    network_queue_advance(1);
}


int
network_tx_queue_check(void)
{
    if ((first_pkt[1] == NULL) && (last_pkt[1] == NULL))
	return 0;

    return 1;
}


int
network_dev_to_id(char *devname)
{
    int i = 0;

    for (i=0; i<network_ndev; i++) {
	if (! strcmp((char *)network_devs[i].device, devname)) {
		return(i);
	}
    }

    /* If no match found, assume "none". */
    return(0);
}


/* UI */
int
network_available(void)
{
    if ((network_type == NET_TYPE_NONE) || (network_card == 0)) return(0);

    return(1);
}


/* UI */
int
network_card_available(int card)
{
    if (net_cards[card].device)
	return(device_available(net_cards[card].device));

    return(1);
}


/* UI */
const device_t *
network_card_getdevice(int card)
{
    return(net_cards[card].device);
}


/* UI */
int
network_card_has_config(int card)
{
    if (! net_cards[card].device) return(0);

    return(net_cards[card].device->config ? 1 : 0);
}


/* UI */
char *
network_card_get_internal_name(int card)
{
    return((char *)net_cards[card].internal_name);
}


/* UI */
int
network_card_get_from_internal_name(char *s)
{
    int c = 0;
	
    while (strlen((char *)net_cards[c].internal_name)) {
	if (! strcmp((char *)net_cards[c].internal_name, s))
			return(c);
	c++;
    }
	
    return(-1);
}


void
network_set_wait(int wait)
{
    net_wait = wait;
}


int
network_get_wait(void)
{
    int ret;

    ret = net_wait;
    return ret;
}
