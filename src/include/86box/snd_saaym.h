/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Texelec Saaym Emulation.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2024-2026 Jasmine Iwanek.
 */
#ifndef SOUND_SAAYM_H
#define SOUND_SAAYM_H

#include <86box/snd_opl.h>
#include <86box/log.h>

typedef struct saaym_t {
    int     addr;

    uint8_t latched_data;

    cms_t   cms;

    fm_drv_t opm;

    void * log;
} saaym_t;

#endif /*SOUND_SAAYM_H*/
