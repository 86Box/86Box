#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "resid-fp/sid.h"
#include "sound_resid.h"

typedef struct psid_t
{
        /* resid sid implementation */
        SIDFP *sid;
        int16_t last_sample;
} psid_t;

psid_t *psid;

void *sid_init()
{
//        psid_t *psid;
        int c;
        sampling_method method=SAMPLE_INTERPOLATE;
        float cycles_per_sec = 14318180.0 / 16.0;
        
        psid = new psid_t;
//        psid = (psid_t *)malloc(sizeof(sound_t));
        psid->sid = new SIDFP;
        
        psid->sid->set_chip_model(MOS8580FP);
        
        psid->sid->set_voice_nonlinearity(1.0f);
        psid->sid->get_filter().set_distortion_properties(0.f, 0.f, 0.f);
        psid->sid->get_filter().set_type4_properties(6.55f, 20.0f);

        psid->sid->enable_filter(true);
        psid->sid->enable_external_filter(true);

        psid->sid->reset();
        
        for (c=0;c<32;c++)
                psid->sid->write(c,0);
                
        if (!psid->sid->set_sampling_parameters((float)cycles_per_sec, method,
                                            (float)48000, 0.9*48000.0/2.0))
                                            {
  //                                                      printf("reSID failed!\n");
                                                }

        psid->sid->set_chip_model(MOS6581FP);
        psid->sid->set_voice_nonlinearity(0.96f);
        psid->sid->get_filter().set_distortion_properties(3.7e-3f, 2048.f, 1.2e-4f);

        psid->sid->input(0);
        psid->sid->get_filter().set_type3_properties(1.33e6f, 2.2e9f, 1.0056f, 7e3f);

        return (void *)psid;
}

void sid_close(void *p)
{
//        psid_t *psid = (psid_t *)p;
        delete psid->sid;
//        free(psid);
}

void sid_reset(void *p)
{
//        psid_t *psid = (psid_t *)p;
        int c;
        
        psid->sid->reset();

        for (c = 0; c < 32; c++)
                psid->sid->write(c, 0);
}


uint8_t sid_read(uint16_t addr, void *p)
{
//        psid_t *psid = (psid_t *)p;
        
        return psid->sid->read(addr & 0x1f);
//        return 0xFF;
}

void sid_write(uint16_t addr, uint8_t val, void *p)
{
//        psid_t *psid = (psid_t *)p;
        
        psid->sid->write(addr & 0x1f,val);
}

#define CLOCK_DELTA(n) (int)(((14318180.0 * n) / 16.0) / 48000.0)

static void fillbuf2(int& count, int16_t *buf, int len)
{
        int c;
        c = psid->sid->clock(count, buf, len, 1);
        if (!c)
                *buf = psid->last_sample;
        psid->last_sample = *buf;
}
void sid_fillbuf(int16_t *buf, int len, void *p)
{
//        psid_t *psid = (psid_t *)p;
        int x = CLOCK_DELTA(len);
        
        fillbuf2(x, buf, len);
}
