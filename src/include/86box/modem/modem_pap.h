/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PAP (Password Authentication Protocol) for PPP modem emulation.
 *          RFC 1334.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_PAP_H
#define EMU_MODEM_PAP_H

struct ppp_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

/* PAP codes */
#define PAP_CODE_AUTHENTICATE_REQUEST 1
#define PAP_CODE_AUTHENTICATE_ACK     2
#define PAP_CODE_AUTHENTICATE_NAK     3

void ppp_pap_process(struct ppp_ctx_t *ctx, const uint8_t *pkt, int pkt_len);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MODEM_PAP_H */
