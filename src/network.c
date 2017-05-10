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
 * Version:	@(#)network.c	1.0.1	2017/05/09
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
#include "timer.h"
#include "thread.h"
#include "network.h"
#include "net_ne2000.h"


typedef struct {
    char name[64];
    char internal_name[32];
    device_t *device;
} NETCARD;

typedef struct {
    void (*poller)(void *);
    void *priv;
} NETPOLL;


static int net_handlers_num;
static uint32_t net_poll_time = 100;
static NETPOLL net_handlers[8];
static NETCARD net_cards[] = {
    { "None",			"none",		NULL			},
    { "Novell NE2000",		"ne2k",		&ne2000_device		},
    { "Realtek RTL8029AS",	"ne2kpci",	&rtl8029as_device	},
    { "",			"",		NULL			}
};


int network_card_current = 0;
uint8_t ethif;
int inum;


static void
net_poll(void *priv)
{
    int c;

    /* Reset the poll timer. */
    net_poll_time += (uint32_t)((double)TIMER_USEC * (1000000.0/8.0/3000.0));

    /* If we have active cards.. */
    if (net_handlers_num) {
	/* .. poll each of them. */
	for (c=0; c<net_handlers_num; c++) {
		net_handlers[c].poller(net_handlers[c].priv);
	}
    }
}


/* Initialize the configured network cards. */
void
network_init(void)
{
    /* No network interface right now. */
    network_card_current = 0;
    net_handlers_num = 0;

    /* This should be a config value, really.  --FvK */
    net_poll_time = 100;
}


/* Reset the network card(s). */
void
network_reset(void)
{
    pclog("NETWORK: reset (card=%d)\n", network_card_current);

    if (! network_card_current) return;

    if (net_cards[network_card_current].device) {
	pclog("NETWORK: adding device '%s'\n",
		net_cards[network_card_current].name);
	device_add(net_cards[network_card_current].device);
    }

    pclog("NETWORK: adding timer...\n");
    timer_add(net_poll, &net_poll_time, TIMER_ALWAYS_ENABLED, NULL);
}


/* Add a handler for a network card. */
void
network_add_handler(void (*poller)(void *), void *p)
{
    net_handlers[net_handlers_num].poller = poller;
    net_handlers[net_handlers_num].priv = p;
    net_handlers_num++;
}


int
network_card_available(int card)
{
    if (net_cards[card].device)
	return(device_available(net_cards[card].device));

    return(1);
}


char *
network_card_getname(int card)
{
    return(net_cards[card].name);
}


device_t *
network_card_getdevice(int card)
{
    return(net_cards[card].device);
}


int
network_card_has_config(int card)
{
    if (! net_cards[card].device) return(0);

    return(net_cards[card].device->config ? 1 : 0);
}


char *
network_card_get_internal_name(int card)
{
    return(net_cards[card].internal_name);
}


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
