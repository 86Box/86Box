/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PPTP for the emulated modem's virtual server, for guests with the
 *          Dial-Up Networking VPN adapter. RFC 2637.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_PPTP_H
#define EMU_MODEM_PPTP_H

#ifdef __cplusplus
extern "C" {
#endif

#define PPTP_PORT      1723
#define PPTP_SERVER_IP 0xC0A85601u /* 192.168.86.1, the server as seen through the tunnel */
#define PPTP_GUEST_IP  0xC0A85602u /* 192.168.86.2 */

/* Starts listening on the server. It goes when the server does. */
bool pptp_attach(vserver_t *server, void *log, bool netbeui, bool ipx);

#ifdef __cplusplus
}
#endif

#endif /* MODEM_PPTP_H */
