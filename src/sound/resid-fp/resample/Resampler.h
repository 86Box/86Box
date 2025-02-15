/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2024 Leandro Nini <drfiemost@users.sourceforge.net>
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

#ifndef RESAMPLER_H
#define RESAMPLER_H

#include <cmath>
#include <cassert>

#include "../sidcxx11.h"

#include "../siddefs-fp.h"

namespace reSIDfp
{

/**
 * Abstraction of a resampling process. Given enough input, produces output.
 * Constructors take additional arguments that configure these objects.
 */
class Resampler
{
private:
    template<int m>
    static inline int clipper(int x)
    {
        assert(x >= 0);
        constexpr int threshold = 28000;
        if (likely(x < threshold))
            return x;

        constexpr double max_val = static_cast<double>(m);
        constexpr double t = threshold / max_val;
        constexpr double a = 1. - t;
        constexpr double b = 1. / a;

        double value = static_cast<double>(x - threshold) / max_val;
        value = t + a * std::tanh(b * value);
        return static_cast<int>(value * max_val);
    }

    /*
     * Soft Clipping implementation, splitted for test.
     */
    static inline int softClipImpl(int x)
    {
        return x < 0 ? -clipper<32768>(-x) : clipper<32767>(x);
    }

protected:
    /*
     * Soft Clipping into 16 bit range [-32768,32767]
     */
    static inline short softClip(int x) { return static_cast<short>(softClipImpl(x)); }

    virtual int output() const = 0;

    Resampler() {}

public:
    virtual ~Resampler() = default;

    /**
     * Input a sample into resampler. Output "true" when resampler is ready with new sample.
     *
     * @param sample input sample
     * @return true when a sample is ready
     */
    virtual bool input(int sample) = 0;

    /**
     * Output a sample from resampler.
     *
     * @return resampled sample
     */
    inline short getOutput(int scaleFactor) const
    {
        const int out = (scaleFactor * output()) / 2;
        return softClip(out);
    }

    virtual void reset() = 0;
};

} // namespace reSIDfp

#endif
