/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2023 Leandro Nini <drfiemost@users.sourceforge.net>
 * Copyright 2007-2010 Antti Lankila
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "WaveformCalculator.h"

#include <cmath>

namespace reSIDfp
{

WaveformCalculator* WaveformCalculator::getInstance()
{
    static WaveformCalculator instance;
    return &instance;
}

/**
 * Parameters derived with the Monte Carlo method based on
 * samplings by kevtris. Code and data available in the project repository [1].
 *
 * The score here reported is the acoustic error
 * calculated XORing the estimated and the sampled values.
 * In parentheses the number of mispredicted bits.
 *
 * [1] https://github.com/libsidplayfp/combined-waveforms
 */
const CombinedWaveformConfig config[2][5] =
{
    { /* kevtris chip G (6581 R2) */
        {0.862147212f, 0.f,          10.8962431f,    2.50848103f }, // TS  error  1941 (327/28672)
        {0.932746708f, 2.07508397f,   1.03668225f,   1.14876997f }, // PT  error  5992 (126/32768)
        {0.860927045f, 2.43506575f,   0.908603609f,  1.07907593f }, // PS  error  3693 (521/28672)
        {0.741343081f, 0.0452554375f, 1.1439606f,    1.05711341f }, // PTS error   338 ( 29/28672)
        {0.96f,        2.5f,          1.1f,          1.2f        }, // NP  guessed
    },
    { /* kevtris chip V (8580 R5) */
        {0.715788841f, 0.f,           1.32999945f,   2.2172699f  }, // TS  error   928 (135/32768)
        {0.93500334f,  1.05977178f,   1.08629429f,   1.43518543f }, // PT  error  7991 (212/32768)
        {0.920648575f, 0.943601072f,  1.13034654f,   1.41881108f }, // PS  error 12566 (394/32768)
        {0.90921098f,  0.979807794f,  0.942194462f,  1.40958893f }, // PTS error  2092 ( 60/32768)
        {0.95f,        1.15f,         1.f,           1.45f       }, // NP  guessed
    },
};

typedef float (*distance_t)(float, int);

// Distance functions
static float exponentialDistance(float distance, int i)
{
    return pow(distance, -i);
}

MAYBE_UNUSED static float linearDistance(float distance, int i)
{
    return 1.f / (1.f + i * distance);
}

MAYBE_UNUSED static float quadraticDistance(float distance, int i)
{
    return 1.f / (1.f + (i*i) * distance);
}

/// Calculate triangle waveform
static unsigned int triXor(unsigned int val)
{
    return (((val & 0x800) == 0) ? val : (val ^ 0xfff)) << 1;
}

/**
 * Generate bitstate based on emulation of combined waves pulldown.
 *
 * @param distancetable
 * @param pulsestrength
 * @param threshold
 * @param accumulator the high bits of the accumulator value
 */
short calculatePulldown(float distancetable[], float pulsestrength, float threshold, unsigned int accumulator)
{
    unsigned char bit[12];

    for (unsigned int i = 0; i < 12; i++)
    {
        bit[i] = (accumulator & (1u << i)) != 0 ? 1 : 0;
    }

    float pulldown[12];

    for (int sb = 0; sb < 12; sb++)
    {
        float avg = 0.f;
        float n = 0.f;

        for (int cb = 0; cb < 12; cb++)
        {
            if (cb == sb)
                continue;
            const float weight = distancetable[sb - cb + 12];
            avg += static_cast<float>(1 - bit[cb]) * weight;
            n += weight;
        }

        avg -= pulsestrength;

        pulldown[sb] = avg / n;
    }

    // Get the predicted value
    short value = 0;

    for (unsigned int i = 0; i < 12; i++)
    {
        const float bitValue = bit[i] != 0 ? 1.f - pulldown[i] : 0.f;
        if (bitValue > threshold)
        {
            value |= 1u << i;
        }
    }

    return value;
}

WaveformCalculator::WaveformCalculator() :
    wftable(4, 4096)
{
    // Build waveform table.
    for (unsigned int idx = 0; idx < (1u << 12); idx++)
    {
        const short saw = static_cast<short>(idx);
        const short tri = static_cast<short>(triXor(idx));

        wftable[0][idx] = 0xfff;
        wftable[1][idx] = tri;
        wftable[2][idx] = saw;
        wftable[3][idx] = saw & (saw << 1);
    }
}

matrix_t* WaveformCalculator::buildPulldownTable(ChipModel model)
{
    const CombinedWaveformConfig* cfgArray = config[model == MOS6581 ? 0 : 1];

    cw_cache_t::iterator lb = PULLDOWN_CACHE.lower_bound(cfgArray);

    if (lb != PULLDOWN_CACHE.end() && !(PULLDOWN_CACHE.key_comp()(cfgArray, lb->first)))
    {
        return &(lb->second);
    }

    matrix_t pdTable(5, 4096);

    for (int wav = 0; wav < 5; wav++)
    {
        const CombinedWaveformConfig& cfg = cfgArray[wav];

        const distance_t distFunc = exponentialDistance;

        float distancetable[12 * 2 + 1];
        distancetable[12] = 1.f;
        for (int i = 12; i > 0; i--)
        {
            distancetable[12-i] = distFunc(cfg.distance1, i);
            distancetable[12+i] = distFunc(cfg.distance2, i);
        }

        for (unsigned int idx = 0; idx < (1u << 12); idx++)
        {
            pdTable[wav][idx] = calculatePulldown(distancetable, cfg.pulsestrength, cfg.threshold, idx);
        }
    }
#ifdef HAVE_CXX11
    return &(PULLDOWN_CACHE.emplace_hint(lb, cw_cache_t::value_type(cfgArray, pdTable))->second);
#else
    return &(PULLDOWN_CACHE.insert(lb, cw_cache_t::value_type(cfgArray, pdTable))->second);
#endif
}

} // namespace reSIDfp
