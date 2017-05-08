/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Simple program to show usable interfaces for WinPcap.
 *
 *		Based on the "libpcap" example.
 *
 * Version:	@(#)pcap_if.c	1.0.1	2017/05/08
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <pcap.h>


int
main(int argc, char **argv)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *alldevs;
    pcap_if_t *d;
    int i=0;
    
    /* Retrieve the device list from the local machine */
    if (pcap_findalldevs(&alldevs, errbuf) == -1) {
	fprintf(stderr,"Error in pcap_findalldevs_ex: %s\n", errbuf);
	exit(1);
    }
    
    /* Print the list */
    for (d= alldevs; d != NULL; d= d->next) {
	printf("%d. %s\n", ++i, d->name);
	if (d->description)
		printf("   (%s)\n", d->description);
	  else
		printf("   (No description available)\n");
	printf("\n");
	i++;
    }
    
    if (i == 0) {
	printf("No interfaces found! Make sure WinPcap is installed.\n");
	return(i);
    }

    /* Not really needed as we are about to exit, but oh-well. */
    pcap_freealldevs(alldevs);

    return(i);
}
