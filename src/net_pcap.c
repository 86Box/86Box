/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle WinPcap library processing.
 *
 * Version:	@(#)net_pcap.c	1.0.2	2017/05/17
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pcap.h>
#include "ibm.h"
#include "config.h"
#include "thread.h"
#include "device.h"
#include "network.h"


static pcap_t	*pcap;			/* handle to WinPcap library */
static thread_t	*poll_tid;
static NETRXCB	poll_rx;		/* network RX function to call */
static void	*poll_arg;		/* network RX function arg */


#ifdef WALTJE
int pcap_do_log = 1;
# define ENABLE_PCAP_LOG
#else
int pcap_do_log = 0;
#endif


static void
pcap_log(const char *format, ...)
{
#ifdef ENABLE_PCAP_LOG
    va_list ap;

    if (pcap_do_log) {
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	fflush(stdout);
    }
#endif
}
#define pclog	pcap_log


/* Check if the interface has a packet for us. */
static void
poll_thread(void *arg)
{
    const unsigned char *data;
    uint8_t *mac = (uint8_t *)arg;
    struct pcap_pkthdr h;
    event_t *evt;
    uint32_t mac_cmp32[2];
    uint16_t mac_cmp16[2];

    pclog("PCAP: polling thread started, arg %08lx\n", arg);

    /* Create a waitable event. */
    evt = thread_create_event();
    pclog("PCAP: poll event is %08lx\n", evt);

    while (pcap != NULL) {
	/* Wait for the next packet to arrive. */
	data = pcap_next(pcap, &h);
	if (data != NULL) {
		/* Received MAC. */
		mac_cmp32[0] = *(uint32_t *)(data+6);
		mac_cmp16[0] = *(uint16_t *)(data+10);

		/* Local MAC. */
		mac_cmp32[1] = *(uint32_t *)mac;
		mac_cmp16[1] = *(uint16_t *)(mac+4);
		if ((mac_cmp32[0] != mac_cmp32[1]) ||
		    (mac_cmp16[0] != mac_cmp16[1])) {
			if (poll_rx != NULL)
				poll_rx(poll_arg, (uint8_t *)data, h.caplen); 
		} else {
			/* Mark as invalid packet. */
			data = NULL;
		}
	}

	/* If we did not get anything, wait a while. */
	if (data == NULL)
		thread_wait_event(evt, 10);
    }

    thread_destroy_event(evt);
    poll_tid = NULL;

    pclog("PCAP: polling stopped.\n");
}


/* Initialize WinPcap for us. */
int
network_pcap_setup(uint8_t *mac, NETRXCB func, void *arg)
{
    char temp[PCAP_ERRBUF_SIZE];
    char filter_exp[255];
    struct bpf_program fp;
    char *dev;

    /* Messy, but gets rid of a lot of useless info. */
    dev = (char *)pcap_lib_version();
    if (dev == NULL) {
	/* Hmm, WinPcap doesn't seem to be alive.. */
	pclog("PCAP: WinPcap library not found, disabling network!\n");
	network_type = -1;
	return(-1);
    }

    /* OK, good for now.. */
    strcpy(temp, dev);
    dev = strchr(temp, '(');
    if (dev != NULL) *(dev-1) = '\0';
    pclog("Initializing WinPcap, version %s\n", temp);

    /* Get the value of our capture interface. */
    dev = network_pcap;
    if ((dev[0] == '\0') || !strcmp(dev, "none")) {
	pclog(" No network device configured!\n");
	return(-1);
    }
    pclog(" Network interface: '%s'\n", dev);

    pcap = pcap_open_live(dev,		/* interface name */
			  1518,		/* maximum packet size */
			  1,		/* promiscuous mode? */
			  10,		/* timeout in msec */
			  temp);	/* error buffer */
    if (pcap == NULL) {
	pclog("Unable to open WinPcap: %s!\n", temp);
	return(-1);
    }

    /* Create a MAC address based packet filter. */
    pclog("Building packet filter ...");
    sprintf(filter_exp,
	"( ((ether dst ff:ff:ff:ff:ff:ff) or (ether dst %02x:%02x:%02x:%02x:%02x:%02x)) and not (ether src %02x:%02x:%02x:%02x:%02x:%02x) )",
	mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
	mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (pcap_compile(pcap, &fp, filter_exp, 0, 0xffffffff) != -1) {
	pclog("...");
	if (pcap_setfilter(pcap, &fp) == -1) {
		pclog(" error installing filter!\n");
	} else {
		pclog("!\nUsing filter\t[%s]\n", filter_exp);
	}
    } else {
	pclog(" could not compile filter!\n");
    }

    /* Save the callback info. */
    poll_rx = func;
    poll_arg = arg;

    pclog("PCAP: creating thread..\n");
    poll_tid = thread_create(poll_thread, mac);

    return(0);
}


/* Close up shop. */
void
network_pcap_close(void)
{
    pcap_t *pc;

    if (pcap != NULL) {
	pclog("Closing WinPcap\n");

	/* Tell the polling thread to shut down. */
	pc = pcap; pcap = NULL;
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

	/* OK, now shut down WinPcap itself. */
	pcap_close(pc);
    }
    poll_rx = NULL;
    poll_arg = NULL;
}


/* Send a packet to the Pcap interface. */
void
network_pcap_in(uint8_t *bufp, int len)
{
    if (pcap != NULL)
	pcap_sendpacket(pcap, bufp, len);
}


/* Retrieve an easy-to-use list of devices. */
int
network_devlist(netdev_t *list)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *devlist, *dev;
    int i = 0;

    /* Create a first entry that's always there - needed by UI. */
    strcpy(list->device, "none");
    strcpy(list->description, "None");
    list++; i++;

    /* Retrieve the device list from the local machine */
    if (pcap_findalldevs(&devlist, errbuf) == -1) {
	pclog("NETWORK: error in pcap_findalldevs: %s\n", errbuf);
	return(i);
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
    pcap_freealldevs(devlist);

    return(i);
}
