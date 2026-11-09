/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PPP Extensible Authentication Protocol (EAP) authenticator.
 *          Implements the Identity and MD5-Challenge methods from
 *          RFC 3748 (which obsoletes RFC 2284).
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_EAP_H
#define EMU_MODEM_EAP_H

struct ppp_ctx_t;

/* EAP packet codes */
#define EAP_CODE_REQUEST  1
#define EAP_CODE_RESPONSE 2
#define EAP_CODE_SUCCESS  3
#define EAP_CODE_FAILURE  4

/* RFC 2284 EAP types */
#define EAP_TYPE_IDENTITY        1
#define EAP_TYPE_NOTIFICATION    2
#define EAP_TYPE_NAK             3
#define EAP_TYPE_MD5_CHALLENGE   4
#define EAP_TYPE_OTP             5
#define EAP_TYPE_GENERIC_TOKEN   6

#ifdef __cplusplus
extern "C" {
#endif

void ppp_eap_start(struct ppp_ctx_t *ctx);
void ppp_eap_process(struct ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MODEM_EAP_H */
