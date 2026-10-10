// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef KBC_AT_VECTRA_H
#define KBC_AT_VECTRA_H

#include <stdint.h>

/* Phase 1 expects the DE subcommand; higher phases count payload bytes plus
   one. These lengths are observed in the HP T.04.02/T.04.05 BIOS sequences.
   Consuming them in the controller prevents spurious keyboard FE replies. */
static inline uint8_t
kbc_vectra_next_phase(uint8_t phase, uint8_t data)
{
    if (phase == 1) {
        switch (data) {
            case 0x93:
                return 9; /* eight payload bytes */
            case 0x94:
                return 2; /* one payload byte */
            default:
                return 0;
        }
    }
    return phase > 2 ? phase - 1 : 0;
}

#endif
