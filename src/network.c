/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ibm.h"
#include "device.h"
#include "network.h"
#include "timer.h"
#include "thread.h"

#include "net_ne2000.h"


typedef struct {
    char name[64];
    char internal_name[32];
    device_t *device;
} NETCARD;

typedef struct {
    void (*poller)(void *p);
    void *priv;
} NETPOLL;


static int net_handlers_num;
static int net_card_last = 0;
static uint32_t net_poll_time = 0;
static NETPOLL net_handlers[8];
static NETCARD net_cards[] = {
    { "None",			"none",		NULL			},
    { "Novell NE2000",		"ne2k",		&ne2000_device		},
    { "Realtek RTL8029AS",	"ne2kpci",	&rtl8029as_device	},
    { "",			"",		NULL			}
};

int network_card_current = 0;


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
    if (net_cards[network_card_current].device)
	device_add(net_cards[network_card_current].device);

    net_card_last = network_card_current;

    if (network_card_current != 0) {
	network_reset();
    }
}


/* Add a handler for a network card. */
void
network_add_handler(void (*poller)(void *p), void *p)
{
    net_handlers[net_handlers_num].poller = poller;
    net_handlers[net_handlers_num].priv = p;
    net_handlers_num++;
}


/* Reset the network card(s). */
void
network_reset(void)
{
    pclog("network_reset()\n");

    net_handlers_num = 0;

    if (network_card_current) {
	pclog("NETWORK: adding timer...\n");

        timer_add(net_poll, &net_poll_time, TIMER_ALWAYS_ENABLED, NULL);
    }
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
