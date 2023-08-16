/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2023 Leandro Nini <drfiemost@users.sourceforge.net>
 * Copyright 2007-2010 Antti Lankila
 * Copyright 2010 Dag Lem
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

#include "FilterModelConfig6581.h"

#include <cmath>

#include "Integrator6581.h"
#include "OpAmp.h"

namespace reSIDfp
{

#ifndef HAVE_CXX11
/**
 * Compute log(1+x) without losing precision for small values of x
 *
 * @note when compiling with -ffastm-math the compiler will
 * optimize the expression away leaving a plain log(1. + x)
 */
inline double log1p(double x)
{
    return log(1. + x) - (((1. + x) - 1.) - x) / (1. + x);
}
#endif

const unsigned int OPAMP_SIZE = 33;

/**
 * This is the SID 6581 op-amp voltage transfer function, measured on
 * CAP1B/CAP1A on a chip marked MOS 6581R4AR 0687 14.
 * All measured chips have op-amps with output voltages (and thus input
 * voltages) within the range of 0.81V - 10.31V.
 */
const Spline::Point opamp_voltage[OPAMP_SIZE] =
{
  {  0.81, 10.31 },  // Approximate start of actual range
  {  2.40, 10.31 },
  {  2.60, 10.30 },
  {  2.70, 10.29 },
  {  2.80, 10.26 },
  {  2.90, 10.17 },
  {  3.00, 10.04 },
  {  3.10,  9.83 },
  {  3.20,  9.58 },
  {  3.30,  9.32 },
  {  3.50,  8.69 },
  {  3.70,  8.00 },
  {  4.00,  6.89 },
  {  4.40,  5.21 },
  {  4.54,  4.54 },  // Working point (vi = vo)
  {  4.60,  4.19 },
  {  4.80,  3.00 },
  {  4.90,  2.30 },  // Change of curvature
  {  4.95,  2.03 },
  {  5.00,  1.88 },
  {  5.05,  1.77 },
  {  5.10,  1.69 },
  {  5.20,  1.58 },
  {  5.40,  1.44 },
  {  5.60,  1.33 },
  {  5.80,  1.26 },
  {  6.00,  1.21 },
  {  6.40,  1.12 },
  {  7.00,  1.02 },
  {  7.50,  0.97 },
  {  8.50,  0.89 },
  { 10.00,  0.81 },
  { 10.31,  0.81 },  // Approximate end of actual range
};

std::unique_ptr<FilterModelConfig6581> FilterModelConfig6581::instance(nullptr);

FilterModelConfig6581* FilterModelConfig6581::getInstance()
{
    if (!instance.get())
    {
        instance.reset(new FilterModelConfig6581());
    }

    return instance.get();
}

FilterModelConfig6581::FilterModelConfig6581() :
    FilterModelConfig(
        1.5,     // voice voltage range
        5.075,   // voice DC voltage
        470e-12, // capacitor value
        12.18,   // Vdd
        1.31,    // Vth
        20e-6,   // uCox
        opamp_voltage,
        OPAMP_SIZE
    ),
    WL_vcr(9.0 / 1.0),
    WL_snake(1.0 / 115.0),
    dac_zero(6.65),
    dac_scale(2.63),
    dac(DAC_BITS)
{
    dac.kinkedDac(MOS6581);

    // Create lookup tables for gains / summers.

#ifndef _OPENMP
    OpAmp opampModel(
        std::vector<Spline::Point>(
            std::begin(opamp_voltage),
            std::end(opamp_voltage)),
        Vddt,
        vmin,
        vmax);
#endif

    #pragma omp parallel sections
    {
        #pragma omp section
        {
#ifdef _OPENMP
            OpAmp opampModel(
                std::vector<Spline::Point>(
                    std::begin(opamp_voltage),
                    std::end(opamp_voltage)),
                Vddt,
                vmin,
                vmax);
#endif
            // The filter summer operates at n ~ 1, and has 5 fundamentally different
            // input configurations (2 - 6 input "resistors").
            //
            // Note that all "on" transistors are modeled as one. This is not
            // entirely accurate, since the input for each transistor is different,
            // and transistors are not linear components. However modeling all
            // transistors separately would be extremely costly.
            for (int i = 0; i < 5; i++)
            {
                const int idiv = 2 + i;        // 2 - 6 input "resistors".
                const int size = idiv << 16;
                const double n = idiv;
                opampModel.reset();
                summer[i] = new unsigned short[size];

                for (int vi = 0; vi < size; vi++)
                {
                    const double vin = vmin + vi / N16 / idiv; /* vmin .. vmax */
                    summer[i][vi] = getNormalizedValue(opampModel.solve(n, vin));
                }
            }
        }

        #pragma omp section
        {
#ifdef _OPENMP
            OpAmp opampModel(
                std::vector<Spline::Point>(
                    std::begin(opamp_voltage),
                    std::end(opamp_voltage)),
                Vddt,
                vmin,
                vmax);
#endif
            // The audio mixer operates at n ~ 8/6, and has 8 fundamentally different
            // input configurations (0 - 7 input "resistors").
            //
            // All "on", transistors are modeled as one - see comments above for
            // the filter summer.
            for (int i = 0; i < 8; i++)
            {
                const int idiv = (i == 0) ? 1 : i;
                const int size = (i == 0) ? 1 : i << 16;
                const double n = i * 8.0 / 6.0;
                opampModel.reset();
                mixer[i] = new unsigned short[size];

                for (int vi = 0; vi < size; vi++)
                {
                    const double vin = vmin + vi / N16 / idiv; /* vmin .. vmax */
                    mixer[i][vi] = getNormalizedValue(opampModel.solve(n, vin));
                }
            }
        }

        #pragma omp section
        {
#ifdef _OPENMP
            OpAmp opampModel(
                std::vector<Spline::Point>(
                    std::begin(opamp_voltage),
                    std::end(opamp_voltage)),
                Vddt,
                vmin,
                vmax);
#endif
            // 4 bit "resistor" ladders in the audio output gain
            // necessitate 16 gain tables.
            // From die photographs of the volume "resistor" ladders
            // it follows that gain ~ vol/12 (assuming ideal
            // op-amps and ideal "resistors").
            for (int n8 = 0; n8 < 16; n8++)
            {
                const int size = 1 << 16;
                const double n = n8 / 12.0;
                opampModel.reset();
                gain_vol[n8] = new unsigned short[size];

                for (int vi = 0; vi < size; vi++)
                {
                    const double vin = vmin + vi / N16; /* vmin .. vmax */
                    gain_vol[n8][vi] = getNormalizedValue(opampModel.solve(n, vin));
                }
            }
        }

        #pragma omp section
        {
#ifdef _OPENMP
            OpAmp opampModel(
                std::vector<Spline::Point>(
                    std::begin(opamp_voltage),
                    std::end(opamp_voltage)),
                Vddt,
                vmin,
                vmax);
#endif
            // 4 bit "resistor" ladders in the bandpass resonance gain
            // necessitate 16 gain tables.
            // From die photographs of the bandpass "resistor" ladders
            // it follows that 1/Q ~ ~res/8 (assuming ideal
            // op-amps and ideal "resistors").
            for (int n8 = 0; n8 < 16; n8++)
            {
                const int size = 1 << 16;
                const double n = (~n8 & 0xf) / 8.0;
                opampModel.reset();
                gain_res[n8] = new unsigned short[size];

                for (int vi = 0; vi < size; vi++)
                {
                    const double vin = vmin + vi / N16; /* vmin .. vmax */
                    gain_res[n8][vi] = getNormalizedValue(opampModel.solve(n, vin));
                }
            }
        }

        #pragma omp section
        {
            const double nVddt = N16 * (Vddt - vmin);

            for (unsigned int i = 0; i < (1 << 16); i++)
            {
                // The table index is right-shifted 16 times in order to fit in
                // 16 bits; the argument to sqrt is thus multiplied by (1 << 16).
                const double tmp = nVddt - sqrt(static_cast<double>(i << 16));
                assert(tmp > -0.5 && tmp < 65535.5);
                vcr_nVg[i] = static_cast<unsigned short>(tmp + 0.5);
            }
        }

        #pragma omp section
        {
            //  EKV model:
            //
            //  Ids = Is * (if - ir)
            //  Is = (2 * u*Cox * Ut^2)/k * W/L
            //  if = ln^2(1 + e^((k*(Vg - Vt) - Vs)/(2*Ut))
            //  ir = ln^2(1 + e^((k*(Vg - Vt) - Vd)/(2*Ut))

            // moderate inversion characteristic current
            const double Is = (2. * uCox * Ut * Ut) * WL_vcr;

            // Normalized current factor for 1 cycle at 1MHz.
            const double N15 = norm * ((1 << 15) - 1);
            const double n_Is = N15 * 1.0e-6 / C * Is;

            // kVgt_Vx = k*(Vg - Vt) - Vx
            // I.e. if k != 1.0, Vg must be scaled accordingly.
            for (int kVgt_Vx = 0; kVgt_Vx < (1 << 16); kVgt_Vx++)
            {
                const double log_term = log1p(exp((kVgt_Vx / N16) / (2. * Ut)));
                // Scaled by m*2^15
                const double tmp = n_Is * log_term * log_term;
                assert(tmp > -0.5 && tmp < 65535.5);
                vcr_n_Ids_term[kVgt_Vx] = static_cast<unsigned short>(tmp + 0.5);
            }
        }
    }
}

unsigned short* FilterModelConfig6581::getDAC(double adjustment) const
{
    const double dac_zero = getDacZero(adjustment);

    unsigned short* f0_dac = new unsigned short[1 << DAC_BITS];

    for (unsigned int i = 0; i < (1 << DAC_BITS); i++)
    {
        const double fcd = dac.getOutput(i);
        f0_dac[i] = getNormalizedValue(dac_zero + fcd * dac_scale / (1 << DAC_BITS));
    }

    return f0_dac;
}

std::unique_ptr<Integrator6581> FilterModelConfig6581::buildIntegrator()
{
    return MAKE_UNIQUE(Integrator6581, this, WL_snake);
}

} // namespace reSIDfp
