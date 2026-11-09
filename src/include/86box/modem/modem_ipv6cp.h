/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IPv6CP (IPv6 Control Protocol, RFC 5072) for PPP modem emulation.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_IPV6CP_H
#define EMU_MODEM_IPV6CP_H

#define IPV6CP_OPT_INTERFACE_ID   1 /* Interface-Identifier (8 octets) */
#define IPV6CP_OPT_IPV6_COMPRESS  2 /* IPv6-Compression-Protocol (never accepted) */
#define IPV6CP_IID_LENGTH         8
#define IPV6CP_TIMEOUT_MS         3000 /* Configure-Request retransmit interval */
#define IPV6CP_MAX_RETRIES        10

/* Begin negotiating IPv6CP. Does nothing unless ctx->ppp_ipv6_mode is enabled. */
void ppp_ipv6cp_start(ppp_ctx_t *ctx);
void ppp_ipv6cp_process(ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len);
/* Called once per millisecond while the link is in the IPCP or network phase. */
void ppp_ipv6cp_tick(ppp_ctx_t *ctx);

#endif /* EMU_MODEM_IPV6CP_H */
