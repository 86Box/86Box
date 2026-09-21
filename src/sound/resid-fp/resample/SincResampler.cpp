/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2024 Leandro Nini <drfiemost@users.sourceforge.net>
 * Copyright 2007-2010 Antti Lankila
 * Copyright 2004 Dag Lem <resid@nimrod.no>
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

#include "SincResampler.h"

#ifdef HAVE_CXX20
#  include <numbers>
#endif

#include <algorithm>
#include <iterator>
#include <cassert>
#include <cstring>
#include <cmath>
#include <cstdint>

#include "../siddefs-fp.h"

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_SMMINTRIN_H
#  include <smmintrin.h>
#elif defined(HAVE_ARM_NEON_H)
#  include <arm_neon.h>
#endif

namespace reSIDfp
{

/// Maximum error acceptable in I0 is 1e-6, or ~96 dB.
constexpr double I0E = 1e-6;

constexpr int BITS = 16;

/**
 * Compute the 0th order modified Bessel function of the first kind.
 * This function is originally from resample-1.5/filterkit.c by J. O. Smith.
 * It is used to build the Kaiser window for resampling.
 *
 * @param x evaluate I0 at x
 * @return value of I0 at x.
 */
double I0(double x)
{
    double sum = 1.;
    double u = 1.;
    double n = 1.;
    const double halfx = x / 2.;

    do
    {
        const double temp = halfx / n;
        u *= temp * temp;
        sum += u;
        n += 1.;
    }
    while (u >= I0E * sum);

    return sum;
}

/**
 * Calculate convolution with sample and sinc.
 *
 * @param a sample buffer input
 * @param b sinc buffer
 * @param bLength length of the sinc buffer
 * @return convolved result
 */
int convolve(const int* a, const short* b, int bLength)
{
#ifdef HAVE_EMMINTRIN_H
    int out = 0;

    const uintptr_t offset = (uintptr_t)(a) & 0x0f;

    // check for aligned accesses
    if (offset == ((uintptr_t)(b) & 0x0f))
    {
        if (offset)
        {
            const int l = (0x10 - offset) / 2;

            for (int i = 0; i < l; i++)
            {
                out += *a++ * *b++;
            }

            bLength -= offset;
        }

        __m128i acc = _mm_setzero_si128();

        const int n = bLength / 8;

        for (int i = 0; i < n; i++)
        {
            const __m128i tmp = _mm_madd_epi16(*(__m128i*)a, *(__m128i*)b);
            acc = _mm_add_epi16(acc, tmp);
            a += 8;
            b += 8;
        }

        __m128i vsum = _mm_add_epi32(acc, _mm_srli_si128(acc, 8));
        vsum = _mm_add_epi32(vsum, _mm_srli_si128(vsum, 4));
        out += _mm_cvtsi128_si32(vsum);

        bLength &= 7;
    }
#elif defined HAVE_MMINTRIN_H
    __m64 acc = _mm_setzero_si64();

    const int n = bLength / 4;

    for (int i = 0; i < n; i++)
    {
        const __m64 tmp = _mm_madd_pi16(*(__m64*)a, *(__m64*)b);
        acc = _mm_add_pi16(acc, tmp);
        a += 4;
        b += 4;
    }

    int out = _mm_cvtsi64_si32(acc) + _mm_cvtsi64_si32(_mm_srli_si64(acc, 32));
    _mm_empty();

    bLength &= 3;
#elif defined(HAVE_ARM_NEON_H)
#if (defined(__arm64__) && defined(__APPLE__)) || defined(__aarch64__)
    int32x4_t acc1Low = vdupq_n_s32(0);
    int32x4_t acc1High = vdupq_n_s32(0);
    int32x4_t acc2Low = vdupq_n_s32(0);
    int32x4_t acc2High = vdupq_n_s32(0);

    const int n = bLength / 16;

    for (int i = 0; i < n; i++)
    {
        int16x8_t v11 = vld1q_s16(a);
        int16x8_t v12 = vld1q_s16(a + 8);
        int16x8_t v21 = vld1q_s16(b);
        int16x8_t v22 = vld1q_s16(b + 8);

        acc1Low  = vmlal_s16(acc1Low, vget_low_s16(v11), vget_low_s16(v21));
        acc1High = vmlal_high_s16(acc1High, v11, v21);
        acc2Low  = vmlal_s16(acc2Low, vget_low_s16(v12), vget_low_s16(v22));
        acc2High = vmlal_high_s16(acc2High, v12, v22);

        a += 16;
        b += 16;
    }

    bLength &= 15;

    if (bLength >= 8)
    {
        int16x8_t v1 = vld1q_s16(a);
        int16x8_t v2 = vld1q_s16(b);

        acc1Low  = vmlal_s16(acc1Low, vget_low_s16(v1), vget_low_s16(v2));
        acc1High = vmlal_high_s16(acc1High, v1, v2);

        a += 8;
        b += 8;
    }

    bLength &= 7;

    if (bLength >= 4)
    {
        int16x4_t v1 = vld1_s16(a);
        int16x4_t v2 = vld1_s16(b);

        acc1Low  = vmlal_s16(acc1Low, v1, v2);

        a += 4;
        b += 4;
    }

    int32x4_t accSumsNeon = vaddq_s32(acc1Low, acc1High);
    accSumsNeon = vaddq_s32(accSumsNeon, acc2Low);
    accSumsNeon = vaddq_s32(accSumsNeon, acc2High);

    int out = vaddvq_s32(accSumsNeon);

    bLength &= 3;
#else
    int32x4_t acc = vdupq_n_s32(0);

    const int n = bLength / 4;

    for (int i = 0; i < n; i++)
    {
        const int16x4_t h_vec = vld1_s16(a);
        const int16x4_t x_vec = vld1_s16(b);
        acc = vmlal_s16(acc, h_vec, x_vec);
        a += 4;
        b += 4;
    }

    int out = vgetq_lane_s32(acc, 0) +
              vgetq_lane_s32(acc, 1) +
              vgetq_lane_s32(acc, 2) +
              vgetq_lane_s32(acc, 3);

    bLength &= 3;
#endif
#else
    int out = 0;
#endif

    for (int i = 0; i < bLength; i++)
    {
        out += a[i] * static_cast<int>(b[i]);
    }

    return (out + (1 << 14)) >> 15;
}

int SincResampler::fir(int subcycle)
{
    // Find the first of the nearest fir tables close to the phase
    int firTableFirst = (subcycle * firRES >> 10);
    const int firTableOffset = (subcycle * firRES) & 0x3ff;

    // Find firN most recent samples, plus one extra in case the FIR wraps.
    int sampleStart = sampleIndex - firN + RINGSIZE - 1;

    const int v1 = convolve(sample + sampleStart, (*firTable)[firTableFirst], firN);

    // Use next FIR table, wrap around to first FIR table using
    // previous sample.
    if (unlikely(++firTableFirst == firRES))
    {
        firTableFirst = 0;
        ++sampleStart;
    }

    const int v2 = convolve(sample + sampleStart, (*firTable)[firTableFirst], firN);

    // Linear interpolation between the sinc tables yields good
    // approximation for the exact value.
    return v1 + (firTableOffset * (v2 - v1) >> 10);
}

SincResampler::SincResampler(
        double clockFrequency,
        double samplingFrequency,
        double highestAccurateFrequency) :
    cyclesPerSample(static_cast<int>(clockFrequency / samplingFrequency * 1024.))
{
#if defined(HAVE_CXX20) && defined(__cpp_lib_constexpr_cmath)
    constexpr double PI = std::numbers::pi;
#else
#  ifdef M_PI
        constexpr double PI = M_PI;
#else
        constexpr double PI = 3.14159265358979323846;
#  endif
#endif

    // 16 bits -> -96dB stopband attenuation.
    const double A = -20. * std::log10(1.0 / (1 << BITS));
    // A fraction of the bandwidth is allocated to the transition band, which we double
    // because we design the filter to transition halfway at nyquist.
    const double dw = (1. - 2.*highestAccurateFrequency / samplingFrequency) * PI * 2.;

    // For calculation of beta and N see the reference for the kaiserord
    // function in the MATLAB Signal Processing Toolbox:
    // http://www.mathworks.com/help/signal/ref/kaiserord.html
    const double beta = 0.1102 * (A - 8.7);
    const double I0beta = I0(beta);
    const double cyclesPerSampleD = clockFrequency / samplingFrequency;
    const double inv_cyclesPerSampleD = samplingFrequency / clockFrequency;

    {
        // The filter order will maximally be 124 with the current constraints.
        // N >= (96.33 - 7.95)/(2 * pi * 2.285 * (maxfreq - passbandfreq) >= 123
        // The filter order is equal to the number of zero crossings, i.e.
        // it should be an even number (sinc is symmetric with respect to x = 0).
        int N = static_cast<int>((A - 7.95) / (2.285 * dw) + 0.5);
        N += N & 1;

        // The filter length is equal to the filter order + 1.
        // The filter length must be an odd number (sinc is symmetric with respect to
        // x = 0).
        firN = static_cast<int>(N * cyclesPerSampleD) + 1;
        firN |= 1;

        // Check whether the sample ring buffer would overflow.
        assert(firN < RINGSIZE);

        // Error is bounded by err < 1.234 / L^2, so L = sqrt(1.234 / (2^-16)) = sqrt(1.234 * 2^16).
        firRES = static_cast<int>(std::ceil(std::sqrt(1.234 * (1 << BITS)) * inv_cyclesPerSampleD));

        // firN*firRES represent the total resolution of the sinc sampling. JOS
        // recommends a length of 2^BITS, but we don't quite use that good a filter.
        // The filter test program indicates that the filter performs well, though.
    }

    {
        // Allocate memory for FIR tables.
        firTable = new matrix_t(firRES, firN);

        // The cutoff frequency is midway through the transition band, in effect the same as nyquist.
        const double wc = PI;

        // Calculate the sinc tables.
        const double scale = 32768.0 * wc * inv_cyclesPerSampleD / PI;

        // we're not interested in the fractional part
        // so use int division before converting to double
        const int tmp = firN / 2;
        const double firN_2 = static_cast<double>(tmp);

        for (int i = 0; i < firRES; i++)
        {
            const double jPhase = (double) i / firRES + firN_2;

            for (int j = 0; j < firN; j++)
            {
                const double x = j - jPhase;

                const double xt = x / firN_2;
                const double kaiserXt = std::fabs(xt) < 1. ? I0(beta * std::sqrt(1. - xt * xt)) / I0beta : 0.;

                const double wt = wc * x * inv_cyclesPerSampleD;
                const double sincWt = std::fabs(wt) >= 1e-8 ? std::sin(wt) / wt : 1.;

                (*firTable)[i][j] = static_cast<short>(scale * sincWt * kaiserXt);
            }
        }
    }
}

SincResampler::~SincResampler()
{
    delete firTable;
}

bool SincResampler::input(int input)
{
    bool ready = false;

    sample[sampleIndex] = sample[sampleIndex + RINGSIZE] = input;
    sampleIndex = (sampleIndex + 1) & (RINGSIZE - 1);

    if (sampleOffset < 1024)
    {
        outputValue = fir(sampleOffset);
        ready = true;
        sampleOffset += cyclesPerSample;
    }

    sampleOffset -= 1024;

    return ready;
}

void SincResampler::reset()
{
    std::fill(std::begin(sample), std::end(sample), 0);
    sampleOffset = 0;
}

} // namespace reSIDfp
