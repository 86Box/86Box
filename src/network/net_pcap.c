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
 * Version:	@(#)net_pcap.c	1.0.6	2017/09/24
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <pcap.h>
#include "../ibm.h"
#include "../config.h"
#include "../device.h"
#include "network.h"
#include "../win/plat_dynld.h"
#include "../win/plat_thread.h"


static void	*pcap_handle;		/* handle to WinPcap DLL */
static pcap_t	*pcap;			/* handle to WinPcap library */
static thread_t	*poll_tid;
static NETRXCB	poll_rx;		/* network RX function to call */
static void	*poll_arg;		/* network RX function arg */


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
    const uint8_t *data = NULL;
    struct pcap_pkthdr h;
    uint32_t mac_cmp32[2];
    uint16_t mac_cmp16[2];
    event_t *evt;

    pclog("PCAP: polling thread started, arg %08lx\n", arg);

    /* Create a waitable event. */
    evt = thread_create_event();

    /* As long as the channel is open.. */
    while (pcap != NULL) {
	/* Wait for the next packet to arrive. */
	data = f_pcap_next(pcap, &h);
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


/* Initialize the (Win)Pcap module for use. */
int
network_pcap_init(netdev_t *list)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *devlist, *dev;
    int i = 0;

    /* Local variables. */
    pcap = NULL;

    /* Try loading the DLL. */
    pcap_handle = dynld_module("wpcap.dll", pcap_imports);
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


/* Initialize the (Win)Pcap module for use. */
void
network_pcap_reset(void)
{
    /* Try loading the DLL. */
    pcap_handle = dynld_module("wpcap.dll", pcap_imports);
    return;
}


/* Initialize WinPcap for us. */
int
network_pcap_setup(uint8_t *mac, NETRXCB func, void *arg)
{
    char temp[PCAP_ERRBUF_SIZE];
    char filter_exp[255];
    struct bpf_program fp;
    char *dev;

    /* Did we already load the DLL? */
    if (pcap_handle == NULL) return(-1);

#if 1
    /* Get the value of our capture interface. */
    dev = network_pcap;
    if (dev == NULL) {
	pclog(" PCap device is a null pointer!\n");
	return(-1);
    }
    if ((dev[0] == '\0') || !strcmp(dev, "none")) {
	pclog(" No network device configured!\n");
	return(-1);
    }
    pclog(" Network interface: '%s'\n", dev);
#endif

    strcpy(temp, f_pcap_lib_version());
    dev = strchr(temp, '(');
    if (dev != NULL) *(dev-1) = '\0';
    pclog("PCAP: initializing, %s\n", temp);

#if 0
    /* Get the value of our capture interface. */
    dev = network_pcap;
    if ((dev[0] == '\0') || !strcmp(dev, "none")) {
	pclog(" No network device configured!\n");
	return(-1);
    }
    pclog(" Network interface: '%s'\n", dev);
#else
    dev = network_pcap;
#endif

    pcap = f_pcap_open_live(dev,		/* interface name */
			   1518,	/* maximum packet size */
			   1,		/* promiscuous mode? */
			   10,		/* timeout in msec */
			   temp);	/* error buffer */
    if (pcap == NULL) {
	pclog(" Unable to open device: %s!\n", temp);
	return(-1);
    }

    /* Create a MAC address based packet filter. */
    pclog(" Installing packet filter for MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    sprintf(filter_exp,
	"( ((ether dst ff:ff:ff:ff:ff:ff) or (ether dst %02x:%02x:%02x:%02x:%02x:%02x)) and not (ether src %02x:%02x:%02x:%02x:%02x:%02x) )",
	mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
	mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (f_pcap_compile(pcap, &fp, filter_exp, 0, 0xffffffff) != -1) {
	if (f_pcap_setfilter(pcap, &fp) == -1) {
		pclog(" Error installing filter (%s) !\n", filter_exp);
		f_pcap_close(pcap);
		return (-1);
	}
    } else {
	pclog(" Could not compile filter (%s) !\n", filter_exp);
	f_pcap_close(pcap);
	return (-1);
    }

    /* Save the callback info. */
    poll_rx = func;
    poll_arg = arg;

    pclog(" Starting thread..\n");
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
	f_pcap_close(pc);
	pc = pcap = NULL;

	/* Unload the DLL if possible. */
	if (pcap_handle != NULL) {
		dynld_close(pcap_handle);
		pcap_handle = NULL;
	}
    }
    poll_rx = NULL;
    poll_arg = NULL;
}


void
network_pcap_stop(void)
{
	/* OK, now shut down WinPcap itself. */
	f_pcap_close(pcap);
	pcap = NULL;

	/* Unload the DLL if possible. */
	if (pcap_handle != NULL) {
		dynld_close(pcap_handle);
		pcap_handle = NULL;
	}
}


/* Test WinPcap - 1 = success, 0 = failure. */
int
network_pcap_test(void)
{
    char temp[PCAP_ERRBUF_SIZE];
    char filter_exp[255];
    struct bpf_program fp;
    char *dev;

    /* Did we already load the DLL? */
    if (pcap_handle == NULL)
    {
	pcap_handle = dynld_module("wpcap.dll", pcap_imports);
    }

#if 1
    /* Get the value of our capture interface. */
    dev = network_pcap;
    if (dev == NULL) {
	pclog(" PCap device is a null pointer!\n");
	return 0;
    }
    if ((dev[0] == '\0') || !strcmp(dev, "none")) {
	pclog(" No network device configured!\n");
	return 0;
    }
    pclog(" Network interface: '%s'\n", dev);
#endif

    strcpy(temp, f_pcap_lib_version());
    dev = strchr(temp, '(');
    if (dev != NULL) *(dev-1) = '\0';
    pclog("PCAP: initializing, %s\n", temp);

#if 0
    /* Get the value of our capture interface. */
    dev = network_pcap;
    if ((dev[0] == '\0') || !strcmp(dev, "none")) {
	pclog(" No network device configured!\n");

	/* Unload the DLL if possible. */
	if (pcap_handle != NULL) {
		dynld_close(pcap_handle);
		pcap_handle = NULL;
	}

	return 0;
    }
    pclog(" Network interface: '%s'\n", dev);
#else
    dev = network_pcap;
#endif

    pcap = f_pcap_open_live(dev,		/* interface name */
			   1518,	/* maximum packet size */
			   1,		/* promiscuous mode? */
			   10,		/* timeout in msec */
			   temp);	/* error buffer */
    if (pcap == NULL) {
	pclog(" Unable to open device: %s!\n", temp);

	/* Unload the DLL if possible. */
	if (pcap_handle != NULL) {
		dynld_close(pcap_handle);
		pcap_handle = NULL;
	}

	return 0;
    }

    /* Create a MAC address based packet filter. */
    sprintf(filter_exp,
	"( ((ether dst ff:ff:ff:ff:ff:ff) or (ether dst %02x:%02x:%02x:%02x:%02x:%02x)) and not (ether src %02x:%02x:%02x:%02x:%02x:%02x) )",
	0, 1, 2, 3, 4, 5,
	0, 1, 2, 3, 4, 5);
    if (f_pcap_compile(pcap, &fp, filter_exp, 0, 0xffffffff) != -1) {
	if (f_pcap_setfilter(pcap, &fp) == -1) {
		pclog(" Error installing filter (%s) !\n", filter_exp);
		network_pcap_stop();
		return 0;
	}
    } else {
	pclog(" Could not compile filter (%s) !\n", filter_exp);
	network_pcap_stop();
	return 0;
    }

    network_pcap_stop();

    return 1;
}


/* Send a packet to the Pcap interface. */
void
network_pcap_in(uint8_t *bufp, int len)
{
    if (pcap != NULL)
	f_pcap_sendpacket(pcap, bufp, len);
}
