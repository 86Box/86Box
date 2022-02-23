#ifndef SOUND_SN76489_H
#define SOUND_SN76489_H

enum {
    SN76496,
    NCR8496,
    PSSJ
};

extern const device_t sn76489_device;
extern const device_t ncr8496_device;

extern int sn76489_mute;

typedef struct sn76489_t {
    int      stat[4];
    int      latch[4], count[4];
    int      freqlo[4], freqhi[4];
    int      vol[4];
    uint32_t shift;
    uint8_t  noise;
    int      lasttone;
    uint8_t  firstdat;
    int      type;
    int      extra_divide;

    int16_t buffer[SOUNDBUFLEN];
    int     pos;

    double psgconst;
} sn76489_t;

void sn76489_init(sn76489_t *sn76489, uint16_t base, uint16_t size, int type, int freq);
void sn74689_set_extra_divide(sn76489_t *sn76489, int enable);

#endif /*SOUND_SN76489_H*/
