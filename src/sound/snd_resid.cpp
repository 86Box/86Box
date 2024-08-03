#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resid-fp/sid.h"
#include <86box/plat.h>
#include <86box/snd_resid.h>

#define RESID_FREQ 48000

using reSIDfp::SID;

typedef struct psid_t {
    /* resid sid implementation */
    SID    *sid;
    int16_t last_sample;
} psid_t;

psid_t *psid;

void *
sid_init(void)
{
#if 0
    psid_t *psid;
#endif
    reSIDfp::SamplingMethod method         = reSIDfp::DECIMATE;
    float                   cycles_per_sec = 14318180.0 / 16.0;

    psid = new psid_t;
#if 0
    psid = (psid_t *) malloc(sizeof(sound_t));
#endif
    psid->sid = new SID;

    psid->sid->setChipModel(reSIDfp::MOS8580);
    psid->sid->enableFilter(true);

    psid->sid->reset();

    for (uint8_t c = 0; c < 32; c++)
        psid->sid->write(c, 0);

    try {
        psid->sid->setSamplingParameters(cycles_per_sec, method, (float) RESID_FREQ, 0.9 * (float) RESID_FREQ / 2.0);
    } catch (reSIDfp::SIDError) {
#if 0
        printf("reSID failed!\n");
#endif
    }

    psid->sid->setChipModel(reSIDfp::MOS6581);
    psid->sid->input(0);

    return (void *) psid;
}

void
sid_close(UNUSED(void *priv))
{
#if 0
    psid_t *psid = (psid_t *) priv;
#endif
    delete psid->sid;
#if 0
    free(psid);
#endif
}

void
sid_reset(UNUSED(void *priv))
{
#if 0
    psid_t *psid = (psid_t *) priv;
#endif

    psid->sid->reset();

    for (uint8_t c = 0; c < 32; c++)
        psid->sid->write(c, 0);
}

uint8_t
sid_read(uint16_t addr, UNUSED(void *priv))
{
#if 0
    psid_t *psid = (psid_t *) priv;
#endif

    return psid->sid->read(addr & 0x1f);
#if 0
    return 0xFF;
#endif
}

void
sid_write(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
#if 0
    psid_t *psid = (psid_t *) priv;
#endif

    psid->sid->write(addr & 0x1f, val);
}

#define CLOCK_DELTA(n) (int) (((14318180.0 * n) / 16.0) / (float) RESID_FREQ)

static void
fillbuf2(int &count, int16_t *buf, int len)
{
    int c;
    c = psid->sid->clock(count, buf);
    if (!c)
        *buf = psid->last_sample;
    psid->last_sample = *buf;
}
void
sid_fillbuf(int16_t *buf, int len, UNUSED(void *priv))
{
#if 0
    psid_t *psid = (psid_t *) priv;
#endif
    int x = CLOCK_DELTA(len);

    fillbuf2(x, buf, len);
}
