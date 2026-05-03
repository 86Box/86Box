/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          C/MS emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2024-2026 Jasmine Iwanek.
 */
#ifndef SOUND_CMS_H
#define SOUND_CMS_H

#define CMS_MASTER_CLOCK 7159090

typedef struct cms_s {
    uint8_t  ports[2];
    uint8_t  regs[2][32];
    uint16_t latch[2][6];
    uint32_t freq[2][6];
    float    count[2][6];
    uint8_t  vol[2][6][2];
    uint8_t  stat[2][6];
    uint16_t noise[2][2];
    uint16_t noisefreq[2][2];
    uint16_t noisecount[2][2];
    uint8_t  noisetype[2][2];

    uint8_t latched_data;

    int16_t buffer[SOUNDBUFLEN * 2];

    uint16_t pos;
} cms_t;

extern void    cms_update(cms_t *cms);
extern void    cms_get_buffer(int32_t *buffer, int len, void *priv);
extern void    cms_write(uint16_t port, uint8_t val, void *priv);
extern uint8_t cms_read(uint16_t port, void *priv);

#endif /*SOUND_CMS_H*/
