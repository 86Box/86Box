/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of memory expansion boards for the MCA bus.
 *
 *          This module mirrors the ISA "isamem" memory-board model so
 *          that up to MCAMEM_MAX boards can be configured in the same
 *          way as ISA memory cards.
 *
 *          NOTE: unlike isamem_reset() (which runs before the machine is
 *          set up), mcamem_reset() must be called AFTER the machine has
 *          been initialized, because MCA boards need mca_init() to have
 *          run before they can register themselves on the bus.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          WNT50
 *
 *          Copyright 2018 Fred N. van Kempen.
 *          Copyright 2026 WNT50.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mcamem.h>

/* Board list, structured like net_cards[] in src/network/network.c: each
   entry holds a const device_t pointer, ended by a NULL sentinel.  The
   individual board device_t symbols are declared in mcamem.h under
   EMU_DEVICE_H (see the 86box/network.h pattern). */
typedef struct {
    const device_t *device;
} MCAMEM_CARD;

static const MCAMEM_CARD mcamem_cards[] = {
    // clang-format off
    { &device_none              },
    /* MCA Memory Expansion Boards */
    { &ibm_xma_mca_2mb_device   },
    { NULL                      }
    // clang-format on
};

void
mcamem_reset(void)
{
    /* Add all configured MCA memory boards to the system. */
    for (uint8_t i = 0; i < MCAMEM_MAX; i++) {
        int k = mcamem_type[i];

        if (k == 0)
            continue;

        /* Only add boards whose bus the current machine provides. */
        if (mcamem_cards[k].device && !device_is_valid(mcamem_cards[k].device, machine))
            continue;

        device_add_inst(mcamem_cards[k].device, i + 1);
    }
}

const char *
mcamem_get_name(int board)
{
    if (mcamem_cards[board].device == NULL)
        return (NULL);

    return (mcamem_cards[board].device->name);
}

const char *
mcamem_get_internal_name(int board)
{
    return device_get_internal_name(mcamem_cards[board].device);
}

int
mcamem_get_from_internal_name(const char *str)
{
    int c = 0;

    while (mcamem_cards[c].device != NULL) {
        if (!strcmp(mcamem_cards[c].device->internal_name, str))
            return c;
        c++;
    }

    /* Not found. */
    return 0;
}

const device_t *
mcamem_get_device(int board)
{
    /* Add the instance to the system. */
    return mcamem_cards[board].device;
}

int
mcamem_has_config(int board)
{
    if (mcamem_cards[board].device == NULL)
        return 0;

    return (mcamem_cards[board].device->config ? 1 : 0);
}
