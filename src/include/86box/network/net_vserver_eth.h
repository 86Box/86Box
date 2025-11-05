/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Adapts the virtual server to Ethernet frames for network guests.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifndef NETWORK_NET_VSERVER_ETH_H
#define NETWORK_NET_VSERVER_ETH_H

#include <86box/network/net_vserver.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vserver_eth_t vserver_eth_t;

/* Takes one Ethernet frame headed for the guest. */
typedef void (*vserver_eth_output_t)(void *priv, const uint8_t *frame, int len);

vserver_eth_t *vserver_eth_init(void *log, uint32_t server_ip, bool wins, bool smb, bool ftp, bool tftp,
                                bool netbeui, bool ipx,
                                vserver_eth_output_t output, void *priv);
void           vserver_eth_close(vserver_eth_t *server);

/* The server itself, for what is attached to it. */
vserver_t *vserver_eth_server(vserver_eth_t *server);

/* Offers a frame from the guest. Returns true when it was for the server, and false when
   it should go on to the network as usual. */
bool vserver_eth_input(vserver_eth_t *server, const uint8_t *frame, int len);

/* Call once per millisecond of emulated time. */
void vserver_eth_tick(vserver_eth_t *server);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_NET_VSERVER_ETH_H */
