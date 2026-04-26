/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Adlib emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2024-2026 Jasmine Iwanek.
 */
#ifndef SOUND_ADLIB_H
#define SOUND_ADLIB_H

typedef struct adlib_s {
    fm_drv_t opl;
    uint8_t  pos_regs[8];
} adlib_t;

extern void adlib_get_buffer(int32_t *buffer, const int len, void *priv);

#endif /*SOUND_ADLIB_H*/
