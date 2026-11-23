/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Motorola MC3417/MC3418 CVSD decoder, driven by an external
 *          (software) clock.
 *
 *          Ported from MAME's hc55516.cpp (BSD-3-Clause; Aaron Giles,
 *          Jonathan Gevaryahu).
 */
#ifndef SOUND_CVSD_H
#define SOUND_CVSD_H

enum {
    CVSD_MC3417 = 0,
    CVSD_MC3418 = 1
};

typedef struct mc3417_t {
    uint8_t shiftreg_mask;
    uint8_t shiftreg;
    int     last_clock_state;
    int     buffered_bit;

    double  charge;
    double  decay;
    double  leak;
    double  sylfilter;
    double  intfilter;
    double  next_sample;

    /* Output samples (at SOUND_FREQ) since the last clock edge. */
    uint32_t idle_samples;
} mc3417_t;

extern void    mc3417_init(mc3417_t *cvsd, int type);
extern void    mc3417_reset(mc3417_t *cvsd);
extern void    mc3417_digin_w(mc3417_t *cvsd, int state);
extern void    mc3417_clock_w(mc3417_t *cvsd, int state);
/* Current output, -32768..32767; call once per SOUND_FREQ output sample. */
extern int16_t mc3417_get_sample(mc3417_t *cvsd);

#endif /*SOUND_CVSD_H*/
