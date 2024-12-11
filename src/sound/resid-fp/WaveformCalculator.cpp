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

#include "sidcxx11.h"

#include <map>
#include <mutex>
#include <cmath>

namespace reSIDfp
{

/**
 * Combined waveform model parameters.
 */
using distance_t = float (*)(float, int);

using CombinedWaveformConfig = struct
{
    distance_t distFunc;
    float threshold;
    float topbit;
    float pulsestrength;
    float distance1;
    float distance2;
};

using cw_cache_t = std::map<const CombinedWaveformConfig*, matrix_t>;

cw_cache_t PULLDOWN_CACHE;

std::mutex PULLDOWN_CACHE_Lock;

WaveformCalculator* WaveformCalculator::getInstance()
{
    static WaveformCalculator instance;
    return &instance;
}

// Distance functions
static float exponentialDistance(float distance, int i)
{
    return pow(distance, -i);
}

MAYBE_UNUSED static float linearDistance(float distance, int i)
{
    return 1.f / (1.f + i * distance);
}

static float quadraticDistance(float distance, int i)
{
    return 1.f / (1.f + (i*i) * distance);
}

/**
 * Parameters derived with the Monte Carlo method based on
 * samplings from real machines.
 * Code and data available in the project repository [1].
 * Sampling program made by Dag Lem [2].
 *
 * The score here reported is the acoustic error
 * calculated XORing the estimated and the sampled values.
 * In parentheses the number of mispredicted bits.
 *
 * [1] https://github.com/libsidplayfp/combined-waveforms
 * [2] https://github.com/daglem/reDIP-SID/blob/master/research/combsample.d64
 */
const CombinedWaveformConfig configAverage[2][5] =
{
    { /* 6581 R3 0486S sampled by Trurl */
        // TS  error  3555 (324/32768) [RMS: 73.98]
        { exponentialDistance,  0.877322257f, 1.11349654f, 0.f, 2.14537621f, 9.08618164f },
        // PT  error  4590 (124/32768) [RMS: 68.90]
        { linearDistance, 0.941692829f, 1.f, 1.80072665f, 0.033124879f, 0.232303441f },
        // PS  error 19352 (763/32768) [RMS: 96.91]
        { linearDistance, 1.66494179f, 1.03760982f, 5.62705326f, 0.291590303f, 0.283631504f },
        // PTS error  5068 ( 94/32768) [RMS: 41.69]
        { linearDistance, 1.09762526f, 0.975265801f, 1.52196741f, 0.151528224f, 0.841949463f },
        // NP  guessed
        { exponentialDistance, 0.96f, 1.f, 2.5f, 1.1f, 1.2f },
    },
    { /* 8580 R5 1088 sampled by reFX-Mike */
        // TS  error 10660 (353/32768) [RMS: 58.34]
        { exponentialDistance, 0.853578329f, 1.09615636f, 0.f, 1.8819375f, 6.80794907f },
        // PT  error 10635 (289/32768) [RMS: 108.81]
        { exponentialDistance, 0.929835618f, 1.f, 1.12836814f, 1.10453653f, 1.48065746f },
        // PS  error 12255 (554/32768) [RMS: 102.27]
        { quadraticDistance, 0.911938608f, 0.996440411f, 1.2278074f, 0.000117214302f, 0.18948476f },
        // PTS error  6913 (127/32768) [RMS: 55.80]
        { exponentialDistance, 0.938004673f, 1.04827631f, 1.21178246f, 0.915959001f, 1.42698038f },
        // NP  guessed
        { exponentialDistance, 0.95f, 1.f, 1.15f, 1.f, 1.45f },
    },
};

const CombinedWaveformConfig configWeak[2][5] =
{
    { /* 6581 R2 4383 sampled by ltx128 */
        // TS  error 1474 (198/32768) [RMS: 62.81]
        { exponentialDistance, 0.892563999f, 1.11905622f, 0.f, 2.21876144f, 9.63837719f },
        // PT  error  612 (102/32768) [RMS: 43.71]
        { linearDistance, 1.01262534f, 1.f, 2.46070528f, 0.0537485816f, 0.0986242667f },
        // PS  error 8135 (575/32768) [RMS: 75.10]
        { linearDistance, 2.14896345f, 1.0216713f, 10.5400085f, 0.244498149f, 0.126134038f },
        // PTS error 2489 (60/32768) [RMS: 24.41]
        { linearDistance, 1.22330308f, 0.933797896f, 2.83245254f, 0.0615176819f, 0.323831677f },
        // NP  guessed
        { exponentialDistance, 0.96f, 1.f, 2.5f, 1.1f, 1.2f },
    },
    { /* 8580 R5 4887 sampled by reFX-Mike */
        // TS  error  741 (76/32768) [RMS: 53.74]
        { exponentialDistance, 0.812351167f, 1.1727736f, 0.f, 1.87459648f, 2.31578159f },
        // PT  error 7199 (192/32768) [RMS: 88.43]
        { exponentialDistance, 0.917997837f, 1.f, 1.01248944f, 1.05761552f, 1.37529826f },
        // PS  error 9856 (332/32768) [RMS: 86.29]
        { quadraticDistance, 0.968754232f, 1.00669801f, 1.29909098f, 0.00962483883f, 0.146850556f },
        // PTS error 4809 (60/32768) [RMS: 45.37]
        { exponentialDistance, 0.941834152f, 1.06401193f, 0.991132736f, 0.995310068f, 1.41105855f },
        // NP  guessed
        { exponentialDistance, 0.95f, 1.f, 1.15f, 1.f, 1.45f },
    },
};

const CombinedWaveformConfig configStrong[2][5] =
{
    { /* 6581 R2 0384 sampled by Trurl */
        // TS  error 20337 (1579/32768) [RMS: 88.57]
        { exponentialDistance, 0.000637792516f, 1.56725872f, 0.f, 0.00036806846f, 1.51800942f },
        // PT  error  5190 (238/32768) [RMS: 83.54]
        { linearDistance, 0.924780309f, 1.f, 1.96809769f, 0.0888123438f, 0.234606609f },
        // PS  error 31015 (2181/32768) [RMS: 114.99]
        { linearDistance, 1.2328074f, 0.73079139f, 3.9719491f, 0.00156516861f, 0.314677745f },
        // PTS error  9874 (201/32768) [RMS: 52.30]
        { linearDistance, 1.08558261f, 0.857638359f, 1.52781796f, 0.152927235f, 1.02657032f },
        // NP  guessed
        { exponentialDistance, 0.96f, 1.f, 2.5f, 1.1f, 1.2f },
    },
    { /* 8580 R5 1489 sampled by reFX-Mike */
        // TS  error  4837 (388/32768) [RMS: 76.07]
        { exponentialDistance, 0.89762634f, 56.7594185f, 0.f, 7.68995237f, 12.0754194f },
        // PT  error  9266 (508/32768) [RMS: 127.83]
        { exponentialDistance,  0.87147671f, 1.f, 1.44887495f, 1.05899632f, 1.43786001f },
        // PS  error 13168 (718/32768) [RMS: 123.35]
        { quadraticDistance, 0.89255774f, 1.2253896f, 1.75615835f, 0.0245045591f, 0.12982437f },
        // PTS error  6702 (300/32768) [RMS: 71.01]
        { linearDistance, 0.91124934f, 0.963609755f, 0.909965038f, 1.07445884f, 1.82399702f },
        // NP  guessed
        { exponentialDistance, 0.95f, 1.f, 1.15f, 1.f, 1.45f },
    },
};

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
short calculatePulldown(float distancetable[], float topbit, float pulsestrength, float threshold, unsigned int accumulator)
{
    float bit[12];

    for (unsigned int i = 0; i < 12; i++)
    {
        bit[i] = (accumulator & (1u << i)) != 0 ? 1.f : 0.f;
    }

    bit[11] *= topbit;

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
            avg += (1.f - bit[cb]) * weight;
            n += weight;
        }

        avg -= pulsestrength;

        pulldown[sb] = avg / n;
    }

    // Get the predicted value
    short value = 0;

    for (unsigned int i = 0; i < 12; i++)
    {
        const float bitValue = bit[i] > 0.f ? 1.f - pulldown[i] : 0.f;
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

matrix_t* WaveformCalculator::buildPulldownTable(ChipModel model, CombinedWaveforms cws)
{
    std::lock_guard<std::mutex> lock(PULLDOWN_CACHE_Lock);

    const int modelIdx = model == MOS6581 ? 0 : 1;
    const CombinedWaveformConfig* cfgArray;

    switch (cws)
    {
    default:
    case AVERAGE:
        cfgArray = configAverage[modelIdx];
        break;
    case WEAK:
        cfgArray = configWeak[modelIdx];
        break;
    case STRONG:
        cfgArray = configStrong[modelIdx];
        break;
    }

    cw_cache_t::iterator lb = PULLDOWN_CACHE.lower_bound(cfgArray);

    if (lb != PULLDOWN_CACHE.end() && !(PULLDOWN_CACHE.key_comp()(cfgArray, lb->first)))
    {
        return &(lb->second);
    }

    matrix_t pdTable(5, 4096);

    for (int wav = 0; wav < 5; wav++)
    {
        const CombinedWaveformConfig& cfg = cfgArray[wav];

        const distance_t distFunc = cfg.distFunc;

        float distancetable[12 * 2 + 1];
        distancetable[12] = 1.f;
        for (int i = 12; i > 0; i--)
        {
            distancetable[12-i] = distFunc(cfg.distance1, i);
            distancetable[12+i] = distFunc(cfg.distance2, i);
        }

        for (unsigned int idx = 0; idx < (1u << 12); idx++)
        {
            pdTable[wav][idx] = calculatePulldown(distancetable, cfg.topbit, cfg.pulsestrength, cfg.threshold, idx);
        }
    }

    return &(PULLDOWN_CACHE.emplace_hint(lb, cw_cache_t::value_type(cfgArray, pdTable))->second);
}

} // namespace reSIDfp
