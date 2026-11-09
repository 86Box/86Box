/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NetBIOS Frames Control Protocol for the emulated modem's PPP
 *          link. RFC 2097.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_NBFCP_H
#define EMU_MODEM_NBFCP_H

#define NBFCP_OPT_NAME_PROJECTION  1
#define NBFCP_OPT_PEER_INFORMATION 2
#define NBFCP_OPT_MULTICAST_FILTER 3
#define NBFCP_OPT_MAC_REQUIRED     4
#define NBFCP_MAC_HEADER_LENGTH    12
#define NBFCP_TIMEOUT_MS           3000
#define NBFCP_MAX_RETRIES          10

/* Answers an NBFCP packet. The peer goes first, so a guest without NetBEUI never sees it. */
void ppp_nbfcp_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len);

/* Called once per millisecond while the link is in the IPCP or network phase. */
void ppp_nbfcp_tick(ppp_ctx_t *ctx);

/* True once NBFCP is open. */
bool ppp_nbf_ready(const ppp_ctx_t *ctx);

/* Carries an NBF frame from the peer up, removing the header it may have been asked for. */
void ppp_nbf_receive(ppp_ctx_t *ctx, const uint8_t *frame, int len);

/* Carries an LLC frame towards the peer. Dropped unless NBFCP is open. */
void ppp_wrap_nbf(ppp_ctx_t *ctx, const uint8_t *frame, int len);

/* The largest LLC frame the peer has agreed to take. */
int ppp_nbf_max_frame(const ppp_ctx_t *ctx);

#endif /*EMU_MODEM_NBFCP_H*/
