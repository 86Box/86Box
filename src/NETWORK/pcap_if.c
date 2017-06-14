/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Simple program to show usage of WinPcap.
 *
 *		Based on the "libpcap" examples.
 *
 * Version:	@(#)pcap_if.c	1.0.3	2017/06/04
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <pcap.h>
#include "plat_dynld.h"


static void	*pcap_handle;		/* handle to WinPcap DLL */


/* Pointers to the real functions. */
static int	(*f_pcap_findalldevs)(pcap_if_t **,char *);
static void	(*f_pcap_freealldevs)(pcap_if_t *);
static pcap_t	*(*f_pcap_open_live)(const char *,int,int,int,char *);
static int	(*f_pcap_next_ex)(pcap_t*,struct pcap_pkthdr**,const unsigned char**);
static void	(*f_pcap_close)(pcap_t *);
static dllimp_t pcap_imports[] = {
  { "pcap_findalldevs",	&f_pcap_findalldevs	},
  { "pcap_freealldevs",	&f_pcap_freealldevs	},
  { "pcap_open_live",	&f_pcap_open_live	},
  { "pcap_next_ex",	&f_pcap_next_ex		},
  { "pcap_close",	&f_pcap_close		},
  { NULL,		NULL			},
};


typedef struct {
    char	device[128];
    char	description[128];
} dev_t;


/* Retrieve an easy-to-use list of devices. */
static int
get_devlist(dev_t *list)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *devlist, *dev;
    int i = 0;

    /* Retrieve the device list from the local machine */
    if (f_pcap_findalldevs(&devlist, errbuf) == -1) {
	fprintf(stderr,"Error in pcap_findalldevs_ex: %s\n", errbuf);
	return(-1);
    }

    for (dev=devlist; dev!=NULL; dev=dev->next) {
	strcpy(list->device, dev->name);
	if (dev->description)
		strcpy(list->description, dev->description);
	  else
		memset(list->description, '\0', sizeof(list->description));
	list++;
	i++;
    }

    /* Release the memory. */
    f_pcap_freealldevs(devlist);

    return(i);
}


/* Simple HEXDUMP routine for raw data. */
static void
hex_dump(unsigned char *bufp, int len)
{
    char asci[20];
    unsigned char c;
    long addr;

    addr = 0;
    while (len-- > 0) {
	c = bufp[addr];
	if ((addr % 16) == 0)
		printf("%04x  %02x", addr, c);
	  else
		printf(" %02x", c);
	asci[(addr & 15)] = (uint8_t)isprint(c) ? c : '.';
	if ((++addr % 16) == 0) {
		asci[16] = '\0';
		printf("  | %s |\n", asci);
	}
    }

    if (addr % 16) {
	while (addr % 16) {
		printf("   ");
		asci[(addr & 15)] = ' ';
		addr++;
	}
	asci[16] = '\0';
	printf("  | %s |\n", asci);
    }
}


/* Print a standard Ethernet MAC address. */
static void
eth_praddr(unsigned char *ptr)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
	ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5]);
}


/* Print a standard Ethernet header. */
static int
eth_prhdr(unsigned char *ptr)
{
    unsigned short type;

    printf("Ethernet ");
    eth_praddr(ptr+6);
    printf(" > ");
    eth_praddr(ptr);
    type = (ptr[12] << 8) | ptr[13];
    printf(" type %04x\n", type);

    return(14);
}


/* Capture packets from the network, and print them. */
static int
start_cap(char *dev)
{
    char temp[PCAP_ERRBUF_SIZE];
    struct pcap_pkthdr *hdr;
    const unsigned char *pkt;
    struct tm *ltime;
    time_t now;
    pcap_t *pcap;
    int rc;

    /* Open the device for reading from it. */
    pcap = f_pcap_open_live(dev,
			    1518,	/* MTU */
			    1,		/* promisc mode */
			    10,		/* timeout */
			    temp);
    if (pcap == NULL) {
	fprintf(stderr, "Pcap: open_live(%s): %s\n", dev, temp);
	return(2);
    }

    printf("Listening on '%s'..\n", dev);
    for (;;) {
	rc = f_pcap_next_ex(pcap, &hdr, &pkt);
	if (rc < 0) break;

	/* Did we time out? */
	if (rc == 0) continue;

        /* Convert the timestamp to readable format. */
        now = hdr->ts.tv_sec;
        ltime = localtime(&now);
        strftime(temp, sizeof(temp), "%H:%M:%S", ltime);
        
	/* Process and print the packet. */
        printf("\n<< %s,%.6d len=%d\n",
		temp, hdr->ts.tv_usec, hdr->len);
	rc = eth_prhdr((unsigned char *)pkt);
	hex_dump((unsigned char *)pkt+rc, hdr->len-rc);
    }

    /* All done, close up. */
    f_pcap_close(pcap);

    return(0);
}


/* Show a list of available network interfaces. */
static void
show_devs(dev_t *list, int num)
{
    int i;

    if (num > 0) {
	printf("Available network interfaces:\n\n");

	for (i=0; i<num; i++) {
		printf(" %d - %s\n", i+1, list->device);
		if (list->description[0] != '\0')
			printf("     (%s)\n", list->description);
		  else
			printf("     (No description available)\n");
		list++;
		printf("\n");
	}
    } else {
	printf("No interfaces found!\nMake sure WinPcap is installed.\n");
    }
}


void
pclog(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}


int
main(int argc, char **argv)
{
    dev_t interfaces[32];
    dev_t *dev = interfaces;
    int numdev, i;

    /* Try loading the DLL. */
    pcap_handle = dynld_module("wpcap.dll", pcap_imports);
    if (pcap_handle == NULL) {
	fprintf(stderr, "Unable to load WinPcap DLL !\n");
	return(1);
    }

    /* Get the list. */
    numdev = get_devlist(interfaces);

    if (argc == 1) {
	/* No arguments, just show the list. */
	show_devs(interfaces, numdev);

	dynld_close(pcap_handle);

	return(numdev);
    }

    /* Assume argument to be the interface number to listen on. */
    i = atoi(argv[1]);
    if (i < 0 || i > numdev) {
	fprintf(stderr, "Invalid interface number %d !\n", i);

	dynld_close(pcap_handle);

	return(1);
    }

    /* Looks good, go and listen.. */
    i = start_cap(interfaces[i-1].device);

    dynld_close(pcap_handle);

    return(i);
}
