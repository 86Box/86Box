/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Handle WinPcap library processing.
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
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/network.h>


typedef int bpf_int32;
typedef unsigned int bpf_u_int32;

/*
 * The instruction data structure.
 */
struct bpf_insn {
    unsigned short	code;
    unsigned char 	jt;
    unsigned char 	jf;
    bpf_u_int32		k;
};

/*
 * Structure for "pcap_compile()", "pcap_setfilter()", etc..
 */
struct bpf_program {
    unsigned int	bf_len;
    struct bpf_insn	*bf_insns;
};

typedef struct pcap_if	pcap_if_t;

typedef struct net_timeval {
    long		tv_sec;
    long		tv_usec;
} net_timeval;

#define PCAP_ERRBUF_SIZE	256

struct pcap_pkthdr {
    struct net_timeval	ts;
    bpf_u_int32		caplen;
    bpf_u_int32		len;
};

struct pcap_if {
    struct pcap_if *next;
    char *name;
    char *description;
    void *addresses;
    unsigned int flags;
};


static volatile void		*pcap_handle;	/* handle to WinPcap DLL */
static volatile void		*pcap;		/* handle to WinPcap library */
static volatile thread_t	*poll_tid;
static const netcard_t		*poll_card;	/* netcard linked to us */
static event_t			*poll_state;


/* Pointers to the real functions. */
static const char	*(*f_pcap_lib_version)(void);
static int		(*f_pcap_findalldevs)(pcap_if_t **,char *);
static void		(*f_pcap_freealldevs)(void *);
static void		*(*f_pcap_open_live)(const char *,int,int,int,char *);
static int		(*f_pcap_compile)(void *,void *,
					 const char *,int,bpf_u_int32);
static int		(*f_pcap_setfilter)(void *,void *);
static const unsigned char
			*(*f_pcap_next)(void *,void *);
static int		(*f_pcap_sendpacket)(void *,const unsigned char *,int);
static void		(*f_pcap_close)(void *);
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


#ifdef ENABLE_PCAP_LOG
int pcap_do_log = ENABLE_PCAP_LOG;


static void
pcap_log(const char *fmt, ...)
{
    va_list ap;

    if (pcap_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define pcap_log(fmt, ...)
#endif


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
    int tx;

    pcap_log("PCAP: polling started.\n");
    thread_set_event(poll_state);

    /* Create a waitable event. */
    pcap_log("PCAP: Creating event...\n");
    evt = thread_create_event();

    /* As long as the channel is open.. */
    while (pcap != NULL) {
	/* Request ownership of the device. */
	network_wait(1);

	if (pcap == NULL) {
		network_wait(0);
		break;
	}

	if (network_get_wait() || (poll_card->set_link_state && poll_card->set_link_state(poll_card->priv)) || (poll_card->wait && poll_card->wait(poll_card->priv)))
		data = NULL;
	else
		data = (uint8_t *)f_pcap_next((void *)pcap, &h);
	if (data != NULL) {
		/* Received MAC. */
		mac_cmp32[0] = *(uint32_t *)(data+6);
		mac_cmp16[0] = *(uint16_t *)(data+10);

		/* Local MAC. */
		mac_cmp32[1] = *(uint32_t *)mac;
		mac_cmp16[1] = *(uint16_t *)(mac+4);
		if ((mac_cmp32[0] != mac_cmp32[1]) ||
		    (mac_cmp16[0] != mac_cmp16[1]))
			network_queue_put(0, poll_card->priv, data, h.caplen);
		else {
			/* Mark as invalid packet. */
			data = NULL;
		}
	}

	/* Wait for the next packet to arrive - network_do_tx() is called from there. */
	tx = network_tx_queue_check();

	/* Release ownership of the device. */
	network_wait(0);

	/* If we did not get anything, wait a while. */
	if (!tx)
		thread_wait_event(evt, 10);
    }

    /* No longer needed. */
    if (evt != NULL)
	thread_destroy_event(evt);

    pcap_log("PCAP: polling stopped.\n");
    if (poll_state != NULL)
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
#elif defined __APPLE__
    pcap_handle = dynld_module("libpcap.dylib", pcap_imports);
#else
    pcap_handle = dynld_module("libpcap.so", pcap_imports);
#endif
    if (pcap_handle == NULL) return(-1);

    /* Retrieve the device list from the local machine */
    if (f_pcap_findalldevs(&devlist, errbuf) == -1) {
	pcap_log("PCAP: error in pcap_findalldevs: %s\n", errbuf);
	return(-1);
    }

    for (dev=devlist; dev!=NULL; dev=dev->next) {
		/**
		 * we initialize the strings to NULL first for strncpy
		 */

		memset(list->device, '\0', sizeof(list->device));
		memset(list->description, '\0', sizeof(list->description));

		strncpy(list->device, dev->name, sizeof(list->device) - 1);
		if (dev->description) {
			strncpy(list->description, dev->description, sizeof(list->description) - 1);
		} else {
			/* if description is NULL, set the name. This allows pcap to display *something* useful under WINE */
			strncpy(list->description, dev->name, sizeof(list->description) - 1);
		}

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
    if (pcap_handle == NULL)
	return(-1);

    /* Get the PCAP library name and version. */
    strcpy(errbuf, f_pcap_lib_version());
    str = strchr(errbuf, '(');
    if (str != NULL) *(str-1) = '\0';
    pcap_log("PCAP: initializing, %s\n", errbuf);

    /* Get the value of our capture interface. */
    if ((network_host[0] == '\0') || !strcmp(network_host, "none")) {
	pcap_log("PCAP: no interface configured!\n");
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
    void *pc;

    if (pcap == NULL) return;

    pcap_log("PCAP: closing.\n");

    /* Tell the polling thread to shut down. */
    pc = (void *)pcap; pcap = NULL;

    /* Tell the thread to terminate. */
    if (poll_tid != NULL) {
	/* Wait for the thread to finish. */
	pcap_log("PCAP: waiting for thread to end...\n");
	thread_wait_event(poll_state, -1);
	pcap_log("PCAP: thread ended\n");
	thread_destroy_event(poll_state);

	poll_tid = NULL;
	poll_state = NULL;
	poll_card = NULL;
    }

    /* OK, now shut down Pcap itself. */
    f_pcap_close(pc);
    pcap = NULL;
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
net_pcap_reset(const netcard_t *card, uint8_t *mac)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    char filter_exp[255];
    struct bpf_program fp;

    /* Open a PCAP live channel. */
    if ((pcap = f_pcap_open_live(network_host,		/* interface name */
				 1518,			/* max packet size */
				 1,			/* promiscuous mode? */
				 10,			/* timeout in msec */
			         errbuf)) == NULL) {	/* error buffer */
	pcap_log(" Unable to open device: %s!\n", network_host);
	return(-1);
    }
    pcap_log("PCAP: interface: %s\n", network_host);

    /* Create a MAC address based packet filter. */
    pcap_log("PCAP: installing filter for MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    sprintf(filter_exp,
	"( ((ether dst ff:ff:ff:ff:ff:ff) or (ether dst %02x:%02x:%02x:%02x:%02x:%02x)) and not (ether src %02x:%02x:%02x:%02x:%02x:%02x) )",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (f_pcap_compile((void *)pcap, &fp, filter_exp, 0, 0xffffffff) != -1) {
	if (f_pcap_setfilter((void *)pcap, &fp) != 0) {
		pcap_log("PCAP: error installing filter (%s) !\n", filter_exp);
		f_pcap_close((void *)pcap);
		return(-1);
	}
    } else {
	pcap_log("PCAP: could not compile filter (%s) !\n", filter_exp);
	f_pcap_close((void *)pcap);
	return(-1);
    }

    /* Save the callback info. */
    poll_card = card;

    pcap_log("PCAP: starting thread..\n");
    poll_state = thread_create_event();
    poll_tid = thread_create(poll_thread, mac);
    thread_wait_event(poll_state, -1);

    return(0);
}


/* Send a packet to the Pcap interface. */
void
net_pcap_in(uint8_t *bufp, int len)
{
    if (pcap == NULL)
	return;

    f_pcap_sendpacket((void *)pcap, bufp, len);
}
