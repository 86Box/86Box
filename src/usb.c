/* Copyright holders: Melissa Goad
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "usb.h"


void *usb_priv[32];
static int usb_min_card, usb_max_card;


void (*usb_packet_handle[32])(usb_packet_t* packet, void *priv);


void usb_init(int min_card, int max_card)
{
        int c;
        
        for (c = 0; c < 32; c++)
            usb_packet_handle[c] = usb_priv[c] = NULL;
        
        usb_min_card = min_card;
        usb_max_card = max_card;
}


void usb_add_specific(int card, void (*packet_handle)(usb_packet_t *packet, void *priv), void *priv)
{
              usb_packet_handle[card] = packet_handle;
              usb_priv[card] = priv;
}


void usb_add(void (*packet_handle)(usb_packet_t *packet, void *priv), void *priv)
{
        int c;
        
        for (c = usb_min_card; c <= usb_max_card; c++)
        {
                if (!usb_packet_handle[c])
                {
                              usb_packet_handle[c] = packet_handle;
                              usb_priv[c] = priv;
			// pclog("USB device added to card: %i\n", c);
                        return;
                }
        }
}
