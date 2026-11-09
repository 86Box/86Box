/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NetBIOS name server (NBNS, known to Windows as WINS) for the
 *          virtual server. RFC 1001, RFC 1002.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifndef NETWORK_NET_NBNS_H
#define NETWORK_NET_NBNS_H

#ifdef __cplusplus
extern "C" {
#endif

#define NBNS_PORT           137
#define NBNS_MAX_REPLY      576  /* the largest datagram NBNS has to answer with */

typedef struct nbns_t nbns_t;

nbns_t *nbns_init(void *log, uint32_t server_ip);
void    nbns_close(nbns_t *nbns);

/* Adds the master browser names the SMB server needs. */
void nbns_add_browser_names(nbns_t *nbns);

/* Advances name expiry; call once per millisecond of emulated time. */
void nbns_tick(nbns_t *nbns);

/* Answers one NBNS message. src_ip is the sender's address in host byte order.
   Returns the reply length, or 0 when there is nothing to send. */
int nbns_process(nbns_t *nbns, uint32_t src_ip, const uint8_t *msg, int len,
                 uint8_t *reply, int reply_cap);

/* Transport wrapper for a raw IPv4 packet. Returns -1 when the packet is not for this
   server and should go on as normal, 0 when it was consumed with no reply, or the
   length of the IPv4 reply written to reply. */
int nbns_process_ipv4(nbns_t *nbns, const uint8_t *ip_pkt, int len,
                      uint8_t *reply, int reply_cap);

/* The same for a broadcast from a guest with no WINS address, which only hears from whoever
   owns the name: anything else is ignored and gets no reply. */
int nbns_process_broadcast(nbns_t *nbns, const uint8_t *ip_pkt, int len,
                           uint8_t *reply, int reply_cap);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_NET_NBNS_H */
