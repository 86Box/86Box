/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU type handler.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2024 Miran Grca.
 */
#ifndef EMU_X87_SF_H
#define EMU_X87_SF_H

#include "softfloat3e/softfloat.h"

typedef struct {
    uint16_t      cwd;
    uint16_t      swd;
    uint16_t      tag;
    uint16_t      foo;
    uint32_t      fip;
    uint32_t      fdp;
    uint16_t      fcs;
    uint16_t      fds;
    floatx80      st_space[8];
    unsigned char tos;
    unsigned char align1;
    unsigned char align2;
    unsigned char align3;
} fpu_state_t;

extern fpu_state_t fpu_state;

#endif /*EMU_X87_SF_H*/
