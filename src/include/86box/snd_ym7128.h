#ifndef SOUND_YM7128_H
#define SOUND_YM7128_H

typedef struct ym7128_t {
    int     a0;
    int     sci;
    uint8_t dat;

    int     reg_sel;
    uint8_t regs[32];

    int gl[8];
    int gr[8];
    int vm;
    int vc;
    int vl;
    int vr;
    int c0;
    int c1;
    int t[9];

    int16_t filter_dat;
    int16_t prev_l;
    int16_t prev_r;

    int16_t delay_buffer[2400];
    int     delay_pos;

    int16_t last_samp;
} ym7128_t;

void ym7128_init(ym7128_t *ym7128);
void ym7128_write(ym7128_t *ym7128, uint8_t val);
void ym7128_apply(ym7128_t *ym7128, int16_t *buffer, int len);

#endif /*SOUND_YM7128_H*/
