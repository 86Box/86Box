/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          808x CPU emulation, mostly ported from reenigne's XTCE, which
 *          is cycle-accurate.
 *
 * Authors: Andrew Jenner, <https://www.reenigne.org>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2015-2020 Andrew Jenner.
 *          Copyright 2016-2020 Miran Grca.
 */
#include <stdint.h>
#include "cpu.h"

void        (*prefetch_queue_set_pos)(int pos);
void        (*prefetch_queue_set_ip)(uint16_t ip);
void        (*prefetch_queue_set_prefetching)(int p);
int         (*prefetch_queue_get_pos)(void);
uint16_t    (*prefetch_queue_get_ip)(void);
int         (*prefetch_queue_get_prefetching)(void);
int         (*prefetch_queue_get_size)(void);

static void (*cpu_wait)(int c, int bus);

void
i808x_hook_prefetch_queue(void     (*pf_set_pos)(int pos),
                          void     (*pf_set_ip)(uint16_t ip),
                          void     (*pf_set_prefetching)(int p),
                          int      (*pf_get_pos)(void),
                          uint16_t (*pf_get_ip)(void),
                          int      (*pf_get_prefetching)(void),
                          int      (*pf_get_size)(void),
                          void     (*c_wait)(int c, int bus))
{
    prefetch_queue_set_pos         = pf_set_pos;
    prefetch_queue_set_ip          = pf_set_ip;
    prefetch_queue_set_prefetching = pf_set_prefetching;
    prefetch_queue_get_pos         = pf_get_pos;
    prefetch_queue_get_ip          = pf_get_ip;
    prefetch_queue_get_prefetching = pf_get_prefetching;
    prefetch_queue_get_size        = pf_get_size;
    cpu_wait                       = c_wait;
}

void
wait_cycs(int c, int bus)
{
    cpu_wait(c, bus);
}

