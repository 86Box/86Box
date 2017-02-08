/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <windows.h>
#include <winsock2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "nethandler.h"

#include "ibm.h"
#include "device.h"

#include "ne2000.h"
#include "timer.h"
#include "thread.h"

int network_card_current = 0;
static int network_card_last = 0;

typedef struct
{
        char name[32];
        device_t *device;
} NETWORK_CARD;

static NETWORK_CARD network_cards[] =
{
        {"None",                  NULL},
        {"Novell NE2000",         &ne2000_device},
        {"Realtek RTL8029AS",     &rtl8029as_device},
        {"", NULL}
};

int network_card_available(int card)
{
        if (network_cards[card].device)
                return device_available(network_cards[card].device);

        return 1;
}

char *network_card_getname(int card)
{
        return network_cards[card].name;
}

device_t *network_card_getdevice(int card)
{
        return network_cards[card].device;
}

int network_card_has_config(int card)
{
        if (!network_cards[card].device)
                return 0;
        return network_cards[card].device->config ? 1 : 0;
}

void network_card_init()
{
        if (network_cards[network_card_current].device)
                device_add(network_cards[network_card_current].device);
        network_card_last = network_card_current;
}

static struct
{
        void (*poller)(void *p);
        void *priv;
} vlan_handlers[8];

static int vlan_handlers_num;

static int vlan_poller_time = 0;

void vlan_handler(void (*poller)(void *p), void *p)
//void vlan_handler(int (*can_receive)(void *p), void (*receive)(void *p, const uint8_t *buf, int size), void *p)
{
      /*  vlan_handlers[vlan_handlers_num].can_receive = can_receive; */
        vlan_handlers[vlan_handlers_num].poller = poller;
        vlan_handlers[vlan_handlers_num].priv = p;
        vlan_handlers_num++;
}

static thread_t *network_thread_h;
static event_t *network_event;

static int network_thread_initialized = 0;
static int network_thread_enable = 0;

static void network_thread(void *param)
{
        int c;

	// pclog("Network thread\n");

	// while(1)
	// {
		// pclog("Waiting...\n");
		// thread_wait_event(network_event, -1);

		// pclog("Processing\n");

	        for (c = 0; c < vlan_handlers_num; c++)
       		        vlan_handlers[c].poller(vlan_handlers[c].priv);
	// }
}

void network_thread_init()
{
#if 0
	pclog("network_thread_init()\n");

	if (network_card_current)
	{
		pclog("Thread enabled...\n");

		network_event = thread_create_event();
		network_thread_h = thread_create(network_thread, NULL);
	}

	network_thread_enable = network_card_current ? 1 : 0;
	network_thread_initialized = 1;
#endif
}

void network_thread_reset()
{
#if 0
	if(!network_thread_initialized)
	{
		network_thread_init();
		return;
	}

	pclog("network_thread_reset()\n");
	if (network_card_current && !network_thread_enable)
	{
		pclog("Thread enabled (disabled before...\n");
		network_event = thread_create_event();
		network_thread_h = thread_create(network_thread, NULL);
	}
	else if (!network_card_current && network_thread_enable)
	{
		pclog("Thread disabled (enabled before...\n");
		thread_destroy_event(network_event);
		thread_kill(network_thread_h);
		network_thread_h = NULL;
	}

	network_thread_enable = network_card_current ? 1 : 0;
#endif
}

void vlan_poller(void *priv)
{
        int c;

        vlan_poller_time += (int)((double)TIMER_USEC * (1000000.0 / 8.0 / 1500.0));

	if (network_thread_enable && vlan_handlers_num)
	{
		// pclog("Setting thread event...\n");
		// thread_set_event(network_event);
		network_thread(priv);
	}
}

void vlan_reset()
{
	pclog("vlan_reset()\n");

	if (network_card_current)
	{
		pclog("Adding timer...\n");

	        timer_add(vlan_poller, &vlan_poller_time, TIMER_ALWAYS_ENABLED, NULL);
	}

        vlan_handlers_num = 0;
}
