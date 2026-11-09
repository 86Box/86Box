/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IPCP (Internet Protocol Control Protocol) for PPP modem
 *          emulation, including address, DNS, NetBIOS name-server, and
 *          Van Jacobson compression options. RFC 1332, RFC 1877.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef MODEM_IPCP_H
#define MODEM_IPCP_H

/* IPv4 address negotiation: RFC 1332; DNS options: RFC 1877. */

struct ppp_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

void ppp_ipcp_send_config_request(struct ppp_ctx_t *ctx);
void ppp_ipcp_process(struct ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len);

#ifdef __cplusplus
}
#endif

#endif /* MODEM_IPCP_H */
