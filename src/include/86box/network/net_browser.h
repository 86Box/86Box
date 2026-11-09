/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NetBIOS datagrams and the browser protocol for the virtual
 *          network server. RFC 1002 section 4.4, [MS-BRWS].
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifndef NETWORK_NET_BROWSER_H
#define NETWORK_NET_BROWSER_H

#ifdef __cplusplus
extern "C" {
#endif

#define BROWSER_PORT 138

/* Answers one NetBIOS datagram. Returns the length of the datagram written to reply,
   or 0 when there is nothing to send. */
int browser_process(uint32_t server_ip, const uint8_t *msg, int len, uint8_t *reply, int reply_cap);

/* Builds the announcements that keep the server on a guest's browse list. Which is 0 for
   a host announcement and 1 for a local master announcement. */
int browser_announcement(uint32_t server_ip, int which, uint8_t *reply, int reply_cap);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_NET_BROWSER_H */
