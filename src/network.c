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
 * Version:	@(#)network.c	1.0.3	2017/05/12
 *
 * Authors:	Kotori, <oubattler@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ibm.h"
#include "device.h"
#include "network.h"
#include "net_ne2000.h"


static netcard_t net_cards[] = {
    { "None",			"none",		NULL,
      NULL,			NULL					},
    { "Novell NE1000",		"ne1k",		&ne1000_device,
      NULL,			NULL					},
    { "Novell NE2000",		"ne2k",		&ne2000_device,
      NULL,			NULL					},
    { "Realtek RTL8029AS",	"ne2kpci",	&rtl8029as_device,
      NULL,			NULL					},
    { "",			"",		NULL,
      NULL,			NULL					}
};


int	network_card;
int	network_type;


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
    network_card = 0;
    network_type = -1;
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

    if (! network_card) return(ret);

    /* Save the card's callback info. */
    net_cards[network_card].private = dev;
    net_cards[network_card].rx = rx;

    /* Start the platform module. */
    switch(network_type) {
	case 0:
		ret = network_pcap_setup(mac, rx, dev);
		break;

	case 1:
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
	case 0:
		network_pcap_close();
		break;

	case 1:
		network_slirp_close();
		break;
    }

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
    pclog("NETWORK: reset (type=%d, card=%d\n", network_type, network_card);

    /* Just in case.. */
    network_close();

    /* If no active card, we're done. */
    if (!network_card || (network_type<0)) return;

    pclog("NETWORK: set up for %s, card=%s\n",
	(network_type==1)?"SLiRP":"WinPcap", net_cards[network_card].name);

    pclog("NETWORK: reset (card=%d)\n", network_card);
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
	case 0:
		network_pcap_in(bufp, len);
		break;

	case 1:
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
	
    return(0);
}
