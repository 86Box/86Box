/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Ensoniq AudioPCI family emulation.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2024-2025 Cacodemon345.
 */
struct akm4531_t
{
    unsigned char registers[256];
};

typedef struct akm4531_t akm4531_t;

double akm4531_apply_master_vol(unsigned short sample);