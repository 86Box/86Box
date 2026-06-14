/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          SN76489 PSG / Tandy Sound emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2024-2026 Jasmine Iwanek.
 */
#ifndef SOUND_SN76489_H
#define SOUND_SN76489_H

enum sn76489_type {
    SN76496,
    NCR8496,
    PSSJ
};
typedef uint8_t sn_type_t;

extern const device_t sn76489_device;
extern const device_t ncr8496_device;

extern uint8_t sn76489_mute;

typedef struct sn76489_s {
    int8_t    stat[4];
    int32_t   latch[4];
    int32_t   count[4];
    uint8_t   freqlo[4];
    uint8_t   freqhi[4];
    uint8_t   vol[4];
    uint32_t  shift;
    uint32_t  white_noise_tap_1;
    uint32_t  white_noise_tap_2;
    uint32_t  feedback_mask;
    uint8_t   noise;
    uint8_t   lasttone;
    uint8_t   firstdat;
    sn_type_t type;
    uint8_t   extra_divide;

    int16_t  buffer[SOUNDBUFLEN];
    uint16_t pos;

    double psgconst;
} sn76489_t;

extern void sn76489_init(sn76489_t *sn76489, uint16_t base, uint16_t size, int type, int freq);
extern void sn76489_write(uint16_t port, uint8_t data, void *priv);
extern void sn76489_set_extra_divide(sn76489_t *sn76489, uint8_t enable);

#endif /*SOUND_SN76489_H*/
