/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Motorola MC3417/MC3418 Continuously Variable Slope Delta
 *          decoder, driven by an external (software) clock.
 *
 *          Ported from MAME's hc55516.cpp.
 *
 * Authors: Aaron Giles,
 *          Jonathan Gevaryahu,
 *
 *          Copyright Aaron Giles, Jonathan Gevaryahu.
 *          Distributed under the BSD-3-Clause license.
 */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/snd_cvsd.h>

#define INTEGRATOR_LEAK_TC 0.001
#define FILTER_DECAY_TC    0.004
#define FILTER_CHARGE_TC   0.004
#define FILTER_MIN         0.0416
#define FILTER_MAX         1.0954
#define SAMPLE_GAIN        (10000.0 / 32768.0)

void
mc3417_init(mc3417_t *cvsd, int type)
{
    memset(cvsd, 0, sizeof(mc3417_t));

    cvsd->shiftreg_mask = (type == CVSD_MC3418) ? 0x0f : 0x07;
    cvsd->charge        = pow(exp(-1.0), 1.0 / (FILTER_CHARGE_TC * 16000.0));
    cvsd->decay         = pow(exp(-1.0), 1.0 / (FILTER_DECAY_TC * 16000.0));
    cvsd->leak          = pow(exp(-1.0), 1.0 / (INTEGRATOR_LEAK_TC * 16000.0));

    mc3417_reset(cvsd);
}

void
mc3417_reset(mc3417_t *cvsd)
{
    cvsd->last_clock_state = 0;
}

void
mc3417_digin_w(mc3417_t *cvsd, int state)
{
    cvsd->buffered_bit = !!state;
}

static void
mc3417_process_bit(mc3417_t *cvsd, int bit)
{
    /* Shift the new bit into the shift register. */
    cvsd->shiftreg = (cvsd->shiftreg << 1) | (bit ? 1 : 0);

    /* Move the estimator up or down a step based on the bit. */
    if (!bit)
        cvsd->intfilter += cvsd->sylfilter;
    else
        cvsd->intfilter -= cvsd->sylfilter;

    /* Simulate leakage. */
    cvsd->intfilter *= cvsd->leak;

    /* If we got all 0's or all 1's in the last n bits, bump the step up. */
    if (((cvsd->shiftreg & cvsd->shiftreg_mask) == 0) ||
        ((cvsd->shiftreg & cvsd->shiftreg_mask) == cvsd->shiftreg_mask)) {
        cvsd->sylfilter = FILTER_MAX - ((FILTER_MAX - cvsd->sylfilter) * cvsd->charge);
        if (cvsd->sylfilter > FILTER_MAX)
            cvsd->sylfilter = FILTER_MAX;
    } else {
        cvsd->sylfilter *= cvsd->decay;
        if (cvsd->sylfilter < FILTER_MIN)
            cvsd->sylfilter = FILTER_MIN;
    }

    cvsd->next_sample = cvsd->intfilter * SAMPLE_GAIN;
}

void
mc3417_clock_w(mc3417_t *cvsd, int state)
{
    state = !!state;

    if (state != cvsd->last_clock_state) {
        cvsd->idle_samples = 0;

        /* The MC3417/MC3418 latch data on the falling clock edge. */
        if (!state)
            mc3417_process_bit(cvsd, cvsd->buffered_bit);
    }

    cvsd->last_clock_state = state;
}

int16_t
mc3417_get_sample(mc3417_t *cvsd)
{
    double sample;

    /* If it has been more than 1/32 of a second without a clock, go silent. */
    if (cvsd->idle_samples > (SOUND_FREQ / 32))
        cvsd->next_sample = 0.0;
    else
        cvsd->idle_samples++;

    sample = cvsd->next_sample * 32768.0;
    if (sample > 32767.0)
        sample = 32767.0;
    else if (sample < -32768.0)
        sample = -32768.0;

    return (int16_t) sample;
}
