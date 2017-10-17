/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the network module.
 *
 * NOTE		The definition of the netcard_t is currently not optimal;
 *		it should be malloc'ed and then linked to the NETCARD def.
 *		Will be done later.
 *
 * Version:	@(#)network.c	1.0.15	2017/10/16
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
#include "../86box.h"
#include "../ibm.h"
#include "../device.h"
#include "../plat.h"
#include "../ui.h"
#include "network.h"
#include "net_ne2000.h"


static netcard_t net_cards[] = {
    { "None",			"none",		NULL,
      NULL,			NULL					},
#if defined(DEV_BRANCH) && defined(USE_NE1000)
    { "Novell NE1000",		"ne1k",		&ne1000_device,
      NULL,			NULL					},
#endif
    { "Novell NE2000",		"ne2k",		&ne2000_device,
      NULL,			NULL					},
    { "Realtek RTL8029AS",	"ne2kpci",	&rtl8029as_device,
      NULL,			NULL					},
    { "",			"",		NULL,
      NULL,			NULL					}
};


/* Global variables. */
int		network_type;
int		network_ndev;
int		network_card;
netdev_t	network_devs[32];
char		network_pcap[512];
int		nic_do_log;
static volatile
mutex_t	*netMutex;


static struct
{
    volatile int
    busy,
    queue_in_use;

    volatile event_t
    *wake_poll_thread,
    *poll_complete,
    *queue_not_in_use;
} poll_data;


void
network_mutex_wait(uint8_t wait)
{
    if (wait)
	thread_wait_mutex((mutex_t *) netMutex);
    else
	thread_release_mutex((mutex_t *) netMutex);
}


void
network_wait_for_poll()
{
    while (poll_data.busy)
	thread_wait_event((event_t *) poll_data.poll_complete, -1);
    thread_reset_event((event_t *) poll_data.poll_complete);
}


void
network_thread_init(void)
{
    poll_data.wake_poll_thread = thread_create_event();
    poll_data.poll_complete = thread_create_event();
}

void
network_busy(uint8_t set)
{
    poll_data.busy = !!set;
    if (!set)
	thread_set_event((event_t *) poll_data.poll_complete);
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

#if ENABLE_NIC_LOG
    nic_do_log = ENABLE_NIC_LOG;
#else
    nic_do_log = 0;
#endif

    /* Initialize to a known state. */
    network_type = NET_TYPE_NONE;
    network_card = 0;

    /* Create a first device entry that's always there, as needed by UI. */
    strcpy(network_devs[0].device, "none");
    strcpy(network_devs[0].description, "None");
    network_ndev = 1;

    /* Initialize the Pcap system module, if present. */
    i = network_pcap_init(&network_devs[network_ndev]);
    if (i > 0)
	network_ndev += i;

    if (network_type != NET_TYPE_PCAP)
	network_pcap_close();
}


/*
 * Attach a network card to the system.
 *
 * This function is called by a hardware driver ("card") after it has
 * finished initializing itself, to link itself to the platform support
 * modules.
 */
int
network_attach(void *dev, uint8_t *mac, NETRXCB rx)
{
    int ret = -1;

    if (network_card == 0) return(ret);

    /* Save the card's callback info. */
    net_cards[network_card].priv = dev;
    net_cards[network_card].rx = rx;

    netMutex = thread_create_mutex(L"86Box.NetMutex");

    /* Start the platform module. */
    switch(network_type) {
	case NET_TYPE_PCAP:
		ret = network_pcap_setup(mac, rx, dev);
		if (ret < 0) {
			ui_msgbox(MBX_ERROR, (wchar_t *)IDS_2139);
			network_type = NET_TYPE_NONE;
		}
		break;

	case NET_TYPE_SLIRP:
		ret = network_slirp_setup(mac, rx, dev);
		break;
    }

    return(ret);
}


/* Stop any network activity. */
void
network_close(void)
{
    switch(network_type) {
	case NET_TYPE_PCAP:
		network_pcap_close();
		break;

	case NET_TYPE_SLIRP:
		network_slirp_close();
		break;
    }

    thread_close_mutex((event_t *) netMutex);
}


/* Test the network. */
int
network_test(void)
{
    switch(network_type) {
	case NET_TYPE_PCAP:
		return network_pcap_test();
		break;

	case NET_TYPE_SLIRP:
		return network_slirp_test();
		break;
    }

    return 0;
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
    pclog("NETWORK: reset (type=%d, card=%d)\n", network_type, network_card);

    /* Just in case.. */
    network_close();

    /* If no active card, we're done. */
    if ((network_type==NET_TYPE_NONE) || (network_card==0)) return;

    if (network_type==NET_TYPE_PCAP)  network_pcap_reset();

    pclog("NETWORK: set up for %s, card='%s'\n",
	(network_type==NET_TYPE_SLIRP)?"SLiRP":"WinPcap",
				net_cards[network_card].name);

    /* Add the (new?) card to the I/O system. */
    if (net_cards[network_card].device) {
	pclog("NETWORK: adding device '%s'\n",
		net_cards[network_card].name);
	device_add(net_cards[network_card].device);
    }
}


/* Transmit a packet to one of the network providers. */
void
network_tx(uint8_t *bufp, int len)
{
    switch(network_type) {
	case NET_TYPE_PCAP:
		network_pcap_in(bufp, len);
		break;

	case NET_TYPE_SLIRP:
		network_slirp_in(bufp, len);
		break;
    }
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
char *
network_card_getname(int card)
{
    return(net_cards[card].name);
}


/* UI */
device_t *
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
    return(net_cards[card].internal_name);
}


/* UI */
int
network_card_get_from_internal_name(char *s)
{
    int c = 0;
	
    while (strlen(net_cards[c].internal_name)) {
	if (! strcmp(net_cards[c].internal_name, s))
			return(c);
	c++;
    }
	
    return(-1);
}


int
network_dev_to_id(char *dev)
{
    int i = 0;

    for (i=0; i<network_ndev; i++) {
	if (! strcmp(network_devs[i].device, dev)) {
		return(i);
	}
    }

    /* If no match found, assume "none". */
    return(0);
}
