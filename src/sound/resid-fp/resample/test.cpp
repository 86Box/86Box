/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2012-2013 Leandro Nini <drfiemost@users.sourceforge.net>
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

#include <map>
#include <memory>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <cmath>

#include "../siddefs-fp.h"

#include "Resampler.h"
#include "TwoPassSincResampler.h"

#if __cplusplus < 201103L
#  define unique_ptr auto_ptr
#endif

#ifndef M_PI
#  define M_PI    3.14159265358979323846
#endif

/**
 * Simple sin waveform in, power output measurement function.
 * It would be far better to use FFT.
 */
int main(int, const char*[])
{
    const double RATE = 985248.4;
    const int RINGSIZE = 2048;

    std::unique_ptr<reSIDfp::TwoPassSincResampler> r(reSIDfp::TwoPassSincResampler::create(RATE, 48000.0, 20000.0));

    std::map<double, double> results;
    clock_t start = clock();

    for (double freq = 1000.; freq < RATE / 2.; freq *= 1.01)
    {
        /* prefill resampler buffer */
        int k = 0;
        double omega = 2 * M_PI * freq / RATE;

        for (int j = 0; j < RINGSIZE; j ++)
        {
            int signal = static_cast<int>(32768.0 * std::sin(k++ * omega) * sqrt(2));
            r->input(signal);
        }

        int n = 0;
        float pwr = 0;

        /* Now, during measurement stage, put 100 cycles of waveform through filter. */
        for (int j = 0; j < 100000; j ++)
        {
            int signal = static_cast<int>(32768.0 * std::sin(k++ * omega) * std::sqrt(2));

            if (r->input(signal))
            {
                float out = r->output();
                pwr += out * out;
                n += 1;
            }
        }

        results.insert(std::make_pair(freq, 10 * std::log10(pwr / n)));
    }

    clock_t end = clock();

    for (std::map<double, double>::iterator it = results.begin(); it != results.end(); ++it)
    {
        std::cout << std::fixed << std::setprecision(0) << std::setw(6) << (*it).first  << " Hz " << (*it).second << " dB" << std::endl;
    }

    std::cout << "Filtering time " << (end - start) * 1000. / CLOCKS_PER_SEC << " ms" << std::endl;
}
