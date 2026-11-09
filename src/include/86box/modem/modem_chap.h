/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CHAP (Challenge-Handshake Authentication Protocol) for PPP
 *          modem emulation. Supports CHAP/MD5, CHAP/SHA-1, CHAP/SHA-256,
 *          CHAP/SHA-384, CHAP/SHA-512, MS-CHAP (RFC 2433), and MS-CHAPv2
 *          (RFC 2759). CHAP packet format follows RFC 1994.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_CHAP_H
#define EMU_MODEM_CHAP_H

struct ppp_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

/* CHAP codes */
#define CHAP_CODE_CHALLENGE 1
#define CHAP_CODE_RESPONSE  2
#define CHAP_CODE_SUCCESS   3
#define CHAP_CODE_FAILURE   4

void ppp_chap_send_challenge(struct ppp_ctx_t *ctx);
void ppp_chap_process(struct ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MODEM_CHAP_H */
