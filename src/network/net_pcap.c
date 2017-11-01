/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 * 		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle WinPcap library processing.
 *
 * Version:	@(#)net_pcap.c	1.0.12	2017/10/28
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
#include <pcap.h>
#include "../86box.h"
#include "../ibm.h"
#include "../config.h"
#include "../device.h"
#include "../plat.h"
#include "../plat_dynld.h"
#include "network.h"


static volatile void		*pcap_handle;	/* handle to WinPcap DLL */
static volatile pcap_t		*pcap;		/* handle to WinPcap library */
static volatile thread_t	*poll_tid;
static netcard_t		*poll_card;	/* netcard linked to us */
static event_t			*poll_state;


/* Pointers to the real functions. */
static const char	*(*f_pcap_lib_version)(void);
static int		(*f_pcap_findalldevs)(pcap_if_t **,char *);
static void		(*f_pcap_freealldevs)(pcap_if_t *);
static pcap_t		*(*f_pcap_open_live)(const char *,int,int,int,char *);
static int		(*f_pcap_compile)(pcap_t *,struct bpf_program *,
					 const char *,int,bpf_u_int32);
static int		(*f_pcap_setfilter)(pcap_t *,struct bpf_program *);
static const u_char	*(*f_pcap_next)(pcap_t *,struct pcap_pkthdr *);
static int		(*f_pcap_sendpacket)(pcap_t *,const u_char *,int);
static void		(*f_pcap_close)(pcap_t *);
static dllimp_t pcap_imports[] = {
  { "pcap_lib_version",	&f_pcap_lib_version	},
  { "pcap_findalldevs",	&f_pcap_findalldevs	},
  { "pcap_freealldevs",	&f_pcap_freealldevs	},
  { "pcap_open_live",	&f_pcap_open_live	},
  { "pcap_compile",	&f_pcap_compile		},
  { "pcap_setfilter",	&f_pcap_setfilter	},
  { "pcap_next",	&f_pcap_next		},
  { "pcap_sendpacket",	&f_pcap_sendpacket	},
  { "pcap_close",	&f_pcap_close		},
  { NULL,		NULL			},
};


/* Handle the receiving of frames from the channel. */
static void
poll_thread(void *arg)
{
    uint8_t *mac = (uint8_t *)arg;
    uint8_t *data = NULL;
    struct pcap_pkthdr h;
    uint32_t mac_cmp32[2];
    uint16_t mac_cmp16[2];
    event_t *evt;

    pclog("PCAP: polling started.\n");
    thread_set_event(poll_state);

    /* Create a waitable event. */
    evt = thread_create_event();

    /* As long as the channel is open.. */
    while (pcap != NULL) {
	/* Request ownership of the device. */
	network_wait(1);

	/* Wait for a poll request. */
	network_poll();

	if (pcap == NULL) break;

	/* Wait for the next packet to arrive. */
	data = (uint8_t *)f_pcap_next((pcap_t *)pcap, &h);
	if (data != NULL) {
		/* Received MAC. */
		mac_cmp32[0] = *(uint32_t *)(data+6);
		mac_cmp16[0] = *(uint16_t *)(data+10);

		/* Local MAC. */
		mac_cmp32[1] = *(uint32_t *)mac;
		mac_cmp16[1] = *(uint16_t *)(mac+4);
		if ((mac_cmp32[0] != mac_cmp32[1]) ||
		    (mac_cmp16[0] != mac_cmp16[1])) {

			poll_card->rx(poll_card->priv, data, h.caplen); 
		} else {
			/* Mark as invalid packet. */
			data = NULL;
		}
	}

	/* If we did not get anything, wait a while. */
	if (data == NULL)
		thread_wait_event(evt, 10);

	/* Release ownership of the device. */
	network_wait(0);
    }

    /* No longer needed. */
    if (evt != NULL)
	thread_destroy_event(evt);

    pclog("PCAP: polling stopped.\n");
    thread_set_event(poll_state);
}


/*
 * Prepare the (Win)Pcap module for use.
 *
 * This is called only once, during application init,
 * to check for availability of PCAP, and to retrieve
 * a list of (usable) intefaces for it.
 */
int
net_pcap_prepare(netdev_t *list)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *devlist, *dev;
    int i = 0;

    /* Local variables. */
    pcap = NULL;

    /* Try loading the DLL. */
#ifdef _WIN32
    pcap_handle = dynld_module("wpcap.dll", pcap_imports);
#else
    pcap_handle = dynld_module("libpcap.so", pcap_imports);
#endif
    if (pcap_handle == NULL) return(-1);

    /* Retrieve the device list from the local machine */
    if (f_pcap_findalldevs(&devlist, errbuf) == -1) {
	pclog("PCAP: error in pcap_findalldevs: %s\n", errbuf);
	return(-1);
    }

    for (dev=devlist; dev!=NULL; dev=dev->next) {
	strcpy(list->device, dev->name);
	if (dev->description)
		strcpy(list->description, dev->description);
	  else
		memset(list->description, '\0', sizeof(list->description));
	list++; i++;
    }

    /* Release the memory. */
    f_pcap_freealldevs(devlist);

    return(i);
}


/*
 * Initialize (Win)Pcap for use.
 *
 * This is called on every 'cycle' of the emulator,
 * if and as long the NetworkType is set to PCAP,
 * and also as long as we have a NetCard defined.
 */
int
net_pcap_init(void)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    char *str;

    /* Did we already load the library? */
    if (pcap_handle == NULL) return(-1);
#if 0
    // no, we don't..
    /* Load the DLL if needed. We already know it exists. */
#ifdef _WIN32
    pcap_handle = dynld_module("wpcap.dll", pcap_imports);
#else
    pcap_handle = dynld_module("libpcap.so", pcap_imports);
#endif
    if (pcap_handle == NULL) return(-1);
#endif

    /* Get the PCAP library name and version. */
    strcpy(errbuf, f_pcap_lib_version());
    str = strchr(errbuf, '(');
    if (str != NULL) *(str-1) = '\0';
    pclog("PCAP: initializing, %s\n", errbuf);

    /* Get the value of our capture interface. */
    if ((network_pcap == NULL) ||
	(network_pcap[0] == '\0') ||
	!strcmp(network_pcap, "none")) {
	pclog("PCAP: no interface configured!\n");
	return(-1);
    }

    poll_tid = NULL;
    poll_state = NULL;
    poll_card = NULL;

    return(0);
}


/* Close up shop. */
void
net_pcap_close(void)
{
    pcap_t *pc;

    if (pcap == NULL) return;

    pclog("PCAP: closing.\n");

    /* Tell the polling thread to shut down. */
    pc = (pcap_t *)pcap; pcap = NULL;

    /* Tell the thread to terminate. */
    if (poll_tid != NULL) {
	network_busy(0);

	/* Wait for the thread to finish. */
	pclog("PCAP: waiting for thread to end...\n");
	thread_wait_event(poll_state, -1);
	pclog("PCAP: thread ended\n");
	thread_destroy_event(poll_state);

	poll_tid = NULL;
	poll_state = NULL;
	poll_card = NULL;
    }

    /* OK, now shut down Pcap itself. */
    f_pcap_close(pc);
    pcap = NULL;

#if 0
    // no, we don't..
    /* Unload the DLL if possible. */
    if (pcap_handle != NULL) {
	dynld_close((void *)pcap_handle);
	pcap_handle = NULL;
    }
#endif
}


/*
 * Reset (Win)Pcap and activate it.
 *
 * This is called on every 'cycle' of the emulator,
 * if and as long the NetworkType is set to PCAP,
 * and also as long as we have a NetCard defined.
 *
 * We already know we have PCAP available, as this
 * is called when the network activates itself and
 * tries to attach to the network module.
 */
int
net_pcap_reset(netcard_t *card)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    char filter_exp[255];
    struct bpf_program fp;

    /* Open a PCAP live channel. */
    if ((pcap = f_pcap_open_live(network_pcap,		/* interface name */
				 1518,			/* max packet size */
				 1,			/* promiscuous mode? */
				 10,			/* timeout in msec */
			         errbuf)) == NULL) {	/* error buffer */
	pclog(" Unable to open device: %s!\n", network_pcap);
	return(-1);
    }
    pclog("PCAP: interface: %s\n", network_pcap);

    /* Create a MAC address based packet filter. */
    pclog("PCAP: installing filter for MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
			card->mac[0], card->mac[1], card->mac[2],
			card->mac[3], card->mac[4], card->mac[5]);
    sprintf(filter_exp,
	"( ((ether dst ff:ff:ff:ff:ff:ff) or (ether dst %02x:%02x:%02x:%02x:%02x:%02x)) and not (ether src %02x:%02x:%02x:%02x:%02x:%02x) )",
		card->mac[0], card->mac[1], card->mac[2],
		card->mac[3], card->mac[4], card->mac[5],
		card->mac[0], card->mac[1], card->mac[2],
		card->mac[3], card->mac[4], card->mac[5]);
    if (f_pcap_compile((pcap_t *)pcap, &fp, filter_exp, 0, 0xffffffff) != -1) {
	if (f_pcap_setfilter((pcap_t *)pcap, &fp) != 0) {
		pclog("PCAP: error installing filter (%s) !\n", filter_exp);
		f_pcap_close((pcap_t *)pcap);
		return(-1);
	}
    } else {
	pclog("PCAP: could not compile filter (%s) !\n", filter_exp);
	f_pcap_close((pcap_t *)pcap);
	return(-1);
    }

    /* Save the callback info. */
    poll_card = card;

    pclog("PCAP: starting thread..\n");
    poll_state = thread_create_event();
    poll_tid = thread_create(poll_thread, card->mac);
    thread_wait_event(poll_state, -1);

    return(0);
}


/* Send a packet to the Pcap interface. */
void
net_pcap_in(uint8_t *bufp, int len)
{
    if (pcap == NULL) return;

    network_busy(1);

    f_pcap_sendpacket((pcap_t *)pcap, bufp, len);

    network_busy(0);
}
