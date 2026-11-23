/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Ensoniq ES5506 (OTTO) wavetable synthesizer.
 *
 *          Ported from MAME's es5506.cpp (BSD-3-Clause, Aaron Giles).
 */
#ifndef SOUND_ES5506_H
#define SOUND_ES5506_H

#define ES5506_MAX_CHANNELS 6

typedef struct es5506_voice_t {
    uint32_t control;
    uint64_t freqcount;
    uint64_t start;
    uint32_t lvol;
    uint64_t end;
    uint32_t lvramp;
    uint64_t accum;
    uint32_t rvol;
    uint32_t rvramp;
    uint32_t ecount;
    uint32_t k2;
    uint32_t k2ramp;
    uint32_t k1;
    uint32_t k1ramp;
    int32_t  o4n1;
    int32_t  o3n1;
    int32_t  o3n2;
    int32_t  o2n1;
    int32_t  o2n2;
    int32_t  o1n1;
    uint8_t  index;
    uint8_t  filtcount;
} es5506_voice_t;

typedef struct es5506_t {
    uint32_t master_clock;
    int      sample_rate;
    int      channels;

    uint8_t  current_page;
    uint8_t  active_voices;
    uint16_t mode;
    uint8_t  irqv;

    uint32_t write_latch;
    uint32_t read_latch;
    uint8_t  wst;
    uint8_t  wend;
    uint8_t  lrend;

    es5506_voice_t voice[32];

    int16_t  ulaw_lookup[256];
    uint32_t volume_lookup[4096];

    /* Sample memory: bank is BS1:BS0, addr is a 21-bit word address. */
    uint16_t (*read_sample)(void *priv, int bank, uint32_t addr);
    /* IRQB output, active when state is 1. */
    void     (*irq_cb)(void *priv, int state);
    /* Called when ACTV changes the output sample rate. */
    void     (*rate_cb)(void *priv, int rate);
    void      *priv;
} es5506_t;

extern void    es5506_init(es5506_t *chip, uint32_t clock, int channels);
extern void    es5506_reset(es5506_t *chip);
extern uint8_t es5506_read(es5506_t *chip, uint8_t offset);
extern void    es5506_write(es5506_t *chip, uint8_t offset, uint8_t val);
/* Generates one output sample; out receives 2 * channels values, each a
   20-bit signed sample. */
extern void    es5506_generate(es5506_t *chip, int32_t *out);

#endif /*SOUND_ES5506_H*/
