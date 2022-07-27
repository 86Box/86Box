#ifndef SOUND_CMS_H
#define SOUND_CMS_H

#include <86box/sound.h>
#include <stdint.h>

#define MASTER_CLOCK 7159090

typedef struct cms_t {
    int      addrs[2];
    uint8_t  regs[2][32];
    uint16_t latch[2][6];
    int      freq[2][6];
    float    count[2][6];
    int      vol[2][6][2];
    int      stat[2][6];
    uint16_t noise[2][2];
    uint16_t noisefreq[2][2];
    int      noisecount[2][2];
    int      noisetype[2][2];

    uint8_t latched_data;

    int16_t buffer[SOUNDBUFLEN * 2];

    int pos;
} cms_t;

extern void    cms_update(cms_t *cms);
extern void    cms_write(uint16_t addr, uint8_t val, void *p);
extern uint8_t cms_read(uint16_t addr, void *p);

#endif /*SOUND_CMS_H*/
