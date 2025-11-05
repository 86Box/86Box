/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IPX Control Protocol for the emulated modem's PPP link. RFC 1552.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_IPXCP_H
#define EMU_MODEM_IPXCP_H

#define IPXCP_OPT_NETWORK         1
#define IPXCP_OPT_NODE            2
#define IPXCP_OPT_COMPRESSION     3
#define IPXCP_OPT_ROUTING         4
#define IPXCP_OPT_ROUTER_NAME     5
#define IPXCP_OPT_CONFIG_COMPLETE 6
#define IPXCP_ROUTING_NONE        0
#define IPXCP_ROUTING_RIP_SAP     2
#define IPXCP_TIMEOUT_MS          3000
#define IPXCP_MAX_RETRIES         10

/* Answers an IPXCP packet. The peer goes first, so a guest without IPX never sees it. */
void ppp_ipxcp_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len);

/* Called once per millisecond while the link is in the IPCP or network phase. */
void ppp_ipxcp_tick(ppp_ctx_t *ctx);

/* True once IPXCP is open. */
bool ppp_ipx_ready(const ppp_ctx_t *ctx);

/* Carries an IPX packet from the peer up. */
void ppp_ipx_receive(ppp_ctx_t *ctx, const uint8_t *packet, int len);

/* Carries an IPX packet towards the peer. Dropped unless IPXCP is open. */
void ppp_wrap_ipx(ppp_ctx_t *ctx, const uint8_t *packet, int len);

/* The network both ends agreed on. */
uint32_t ppp_ipx_network(const ppp_ctx_t *ctx);

/* This end's node. */
const uint8_t *ppp_ipx_node(const ppp_ctx_t *ctx);

/* The largest IPX packet the peer has agreed to take. */
int ppp_ipx_max_packet(const ppp_ctx_t *ctx);

#endif /* EMU_MODEM_IPXCP_H */
