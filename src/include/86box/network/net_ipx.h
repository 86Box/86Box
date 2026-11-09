/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IPX for the virtual server: NetBIOS over IPX,
 *          and SMB hosted directly on IPX as NWLink clients do by default.
 *          It works on IPX packets, so it doesn't mind what carries them.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifndef NETWORK_NET_IPX_H
#define NETWORK_NET_IPX_H

#ifdef __cplusplus
extern "C" {
#endif

#define IPX_MAX_PACKET  1500 /* the largest IPX packet sent or received */
#define IPX_MIN_PACKET  576  /* RFC 1552 requires every link to carry this much */
#define IPX_NODE_LENGTH 6
#define IPX_NETWORK     0x0086B0C5u /* the network the server's segment answers to */

typedef struct ipx_t ipx_t;

/* Takes one IPX packet headed for the guest. */
typedef void (*ipx_output_t)(void *priv, const uint8_t *packet, int len);

ipx_t *ipx_init(void *log, const uint8_t *node, ipx_output_t output, void *priv);
void   ipx_close(ipx_t *ipx);

/* Sets the server's address and the largest packet the link can carry towards the guest. */
void ipx_configure(ipx_t *ipx, uint32_t network, const uint8_t *node, int max_packet);

/* Forgets every connection, for when the link carrying them starts again. */
void ipx_reset(ipx_t *ipx);

/* Offers a packet from the guest. Returns true when it was for the server. */
bool ipx_input(ipx_t *ipx, const uint8_t *packet, int len);

/* Runs retransmission and announcements; call once per millisecond of emulated time. */
void ipx_tick(ipx_t *ipx);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_NET_IPX_H */
