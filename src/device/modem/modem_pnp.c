/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          External serial modem Plug and Play identification.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <86box/modem/modem_pnp.h>

enum {
    MODEM_PNP_BAUD = 1200,
    MODEM_PNP_BAUD_TOLERANCE = 24
};

static const char *
modem_pnp_id(int profile)
{
    switch (profile) {
        case MODEM_PNP_GENERIC:  return "PNPC103";
        case MODEM_PNP_ENHANCED: return "PNPC107";
        case MODEM_PNP_PNPC100:  return "PNPC100";
        case MODEM_PNP_PNPC101:  return "PNPC101";
        case MODEM_PNP_PNPC102:  return "PNPC102";
        case MODEM_PNP_PNPC104:  return "PNPC104";
        case MODEM_PNP_PNPC105:  return "PNPC105";
        case MODEM_PNP_PNPC106:  return "PNPC106";
        default:                 return NULL;
    }
}

void
modem_pnp_init(modem_pnp_t *pnp, int profile)
{
    memset(pnp, 0, sizeof(*pnp));
    pnp->profile = profile;
}

void
modem_pnp_cancel(modem_pnp_t *pnp)
{
    pnp->low_ms = 0;
    pnp->response_length = 0;
    pnp->response_pos = 0;
}

void
modem_pnp_format(modem_pnp_t *pnp, int baud, int data_bits, int parity, int stop_bits)
{
    pnp->format_ok = baud >= MODEM_PNP_BAUD - MODEM_PNP_BAUD_TOLERANCE
                     && baud <= MODEM_PNP_BAUD + MODEM_PNP_BAUD_TOLERANCE
                     && data_bits == 7 && parity == 0 && stop_bits == 1;
    if (!pnp->format_ok)
        modem_pnp_cancel(pnp);
}

void
modem_pnp_tick(modem_pnp_t *pnp, bool eligible)
{
    if (pnp->profile == MODEM_PNP_DISABLED || !pnp->format_ok || !eligible) {
        modem_pnp_cancel(pnp);
        return;
    }

    if (!pnp->dtr && !pnp->rts && pnp->low_ms < 1001)
        pnp->low_ms++;
}

bool
modem_pnp_control(modem_pnp_t *pnp, bool dtr, bool rts, bool eligible)
{
    const char *id = modem_pnp_id(pnp->profile);
    const bool trigger = id && pnp->format_ok && eligible
                         && !pnp->dtr && dtr && !pnp->rts && !rts
                         && pnp->low_ms >= 100 && pnp->low_ms <= 1000;
    pnp->dtr = dtr;
    pnp->rts = rts;
    if (dtr || rts)
        pnp->low_ms = 0;

    if (trigger) {
        const int length = snprintf((char *) pnp->response, sizeof(pnp->response),
                                    "(\x01$%s\\\\MODEM\\%s\\86Box Emulated Modem", id, id);
        if (length < 0 || (unsigned) length + 3 >= sizeof(pnp->response))
            return false;
        unsigned checksum = ')';
        for (int i = 0; i < length; i++)
            checksum += pnp->response[i];
        snprintf((char *) pnp->response + length, sizeof(pnp->response) - (unsigned) length,
                 "%02X)", checksum & 0xff);
        pnp->response_length = (unsigned) length + 3;
        pnp->response_pos = 0;
    }
    return trigger;
}

bool
modem_pnp_pending(const modem_pnp_t *pnp)
{
    return pnp->response_pos < pnp->response_length;
}

int
modem_pnp_next(modem_pnp_t *pnp)
{
    if (!modem_pnp_pending(pnp))
        return -1;
    return pnp->response[pnp->response_pos++];
}
