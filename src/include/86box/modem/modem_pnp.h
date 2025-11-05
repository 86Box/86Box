/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          External serial modem Plug and Play identification.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef MODEM_PNP_H
#define MODEM_PNP_H

enum {
    MODEM_PNP_DISABLED = 0,
    MODEM_PNP_GENERIC = 1,
    MODEM_PNP_ENHANCED = 2,
    MODEM_PNP_PNPC100 = 3,
    MODEM_PNP_PNPC101 = 4,
    MODEM_PNP_PNPC102 = 5,
    MODEM_PNP_PNPC104 = 6,
    MODEM_PNP_PNPC105 = 7,
    MODEM_PNP_PNPC106 = 8
};

typedef struct modem_pnp_t {
    int      profile;
    bool     dtr;
    bool     rts;
    bool     format_ok;
    uint32_t low_ms;
    uint8_t  response[64];
    unsigned response_length;
    unsigned response_pos;
} modem_pnp_t;

void modem_pnp_init(modem_pnp_t *pnp, int profile);
void modem_pnp_format(modem_pnp_t *pnp, int baud, int data_bits, int parity, int stop_bits);
void modem_pnp_tick(modem_pnp_t *pnp, bool eligible);
bool modem_pnp_control(modem_pnp_t *pnp, bool dtr, bool rts, bool eligible);
void modem_pnp_cancel(modem_pnp_t *pnp);
bool modem_pnp_pending(const modem_pnp_t *pnp);
int modem_pnp_next(modem_pnp_t *pnp);

#endif
