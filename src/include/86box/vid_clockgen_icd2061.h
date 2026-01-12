/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ICD2061 clock generator emulation.
 *          Also emulates the ICS9161 which is the same as the ICD2016,
 *          but without the need for tuning (which is irrelevant in
 *          emulation anyway).
 *
 *          Used by ET4000w32/p (Diamond Stealth 32) and the S3
 *          Vision964 family.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 */
#ifndef VIDEO_CLOCKGEN_ICD2061_H
#define VIDEO_CLOCKGEN_ICD2061_H

typedef struct icd2061_t {
    float freq[3];
    float ref_clock;

    int      count;
    int      bit_count;
    int      unlocked;
    int      state;
    uint32_t data;
    uint32_t ctrl;
} icd2061_t;

#endif /*VIDEO_CLOCKGEN_ICD2061_H*/
