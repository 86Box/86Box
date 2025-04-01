#ifndef SOUND_CMS_H
#define SOUND_CMS_H

#include <86box/sound.h>
#include <stdint.h>

#define MASTER_CLOCK 7159090

typedef struct cms_t {
#ifdef SAASOUND_H_INCLUDED
    SAASND saasound;
    SAASND saasound2;
#else
    void*  saasound;
    void*  saasound2;
#endif

    uint8_t latched_data;

    int16_t buffer[WTBUFLEN * 2];
    int16_t buffer2[WTBUFLEN * 2];

    int pos, pos2;
} cms_t;

extern void    cms_update(cms_t *cms);
extern void    cms_write(uint16_t addr, uint8_t val, void *priv);
extern uint8_t cms_read(uint16_t addr, void *priv);

#endif /*SOUND_CMS_H*/
