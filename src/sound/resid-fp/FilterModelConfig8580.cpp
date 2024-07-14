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

#include "FilterModelConfig8580.h"

#include "Integrator8580.h"
#include "OpAmp.h"


namespace reSIDfp
{

/*
 * R1 = 15.3*Ri
 * R2 =  7.3*Ri
 * R3 =  4.7*Ri
 * Rf =  1.4*Ri
 * R4 =  1.4*Ri
 * R8 =  2.0*Ri
 * RC =  2.8*Ri
 *
 * res  feedback  input
 * ---  --------  -----
 *  0   Rf        Ri
 *  1   Rf|R1     Ri
 *  2   Rf|R2     Ri
 *  3   Rf|R3     Ri
 *  4   Rf        R4
 *  5   Rf|R1     R4
 *  6   Rf|R2     R4
 *  7   Rf|R3     R4
 *  8   Rf        R8
 *  9   Rf|R1     R8
 *  A   Rf|R2     R8
 *  B   Rf|R3     R8
 *  C   Rf        RC
 *  D   Rf|R1     RC
 *  E   Rf|R2     RC
 *  F   Rf|R3     RC
 */
const double resGain[16] =
{
                        1.4/1.0, //      Rf/Ri   1.4
    ((1.4*15.3)/(1.4+15.3))/1.0, // (Rf|R1)/Ri   1.28263
      ((1.4*7.3)/(1.4+7.3))/1.0, // (Rf|R2)/Ri   1.17471
      ((1.4*4.7)/(1.4+4.7))/1.0, // (Rf|R3)/Ri   1.07869
                        1.4/1.4, //      Rf/R4   1
    ((1.4*15.3)/(1.4+15.3))/1.4, // (Rf|R1)/R4   0.916168
      ((1.4*7.3)/(1.4+7.3))/1.4, // (Rf|R2)/R4   0.83908
      ((1.4*4.7)/(1.4+4.7))/1.4, // (Rf|R3)/R4   0.770492
                        1.4/2.0, //      Rf/R8   0.7
    ((1.4*15.3)/(1.4+15.3))/2.0, // (Rf|R1)/R8   0.641317
      ((1.4*7.3)/(1.4+7.3))/2.0, // (Rf|R2)/R8   0.587356
      ((1.4*4.7)/(1.4+4.7))/2.0, // (Rf|R3)/R8   0.539344
                        1.4/2.8, //      Rf/RC   0.5
    ((1.4*15.3)/(1.4+15.3))/2.8, // (Rf|R1)/RC   0.458084
      ((1.4*7.3)/(1.4+7.3))/2.8, // (Rf|R2)/RC   0.41954
      ((1.4*4.7)/(1.4+4.7))/2.8, // (Rf|R3)/RC   0.385246
};

const unsigned int OPAMP_SIZE = 21;

/**
 * This is the SID 8580 op-amp voltage transfer function, measured on
 * CAP1B/CAP1A on a chip marked CSG 8580R5 1690 25.
 */
const Spline::Point opamp_voltage[OPAMP_SIZE] =
{
    {  1.30,  8.91 },  // Approximate start of actual range
    {  4.76,  8.91 },
    {  4.77,  8.90 },
    {  4.78,  8.88 },
    {  4.785, 8.86 },
    {  4.79,  8.80 },
    {  4.795, 8.60 },
    {  4.80,  8.25 },
    {  4.805, 7.50 },
    {  4.81,  6.10 },
    {  4.815, 4.05 },  // Change of curvature
    {  4.82,  2.27 },
    {  4.825, 1.65 },
    {  4.83,  1.55 },
    {  4.84,  1.47 },
    {  4.85,  1.43 },
    {  4.87,  1.37 },
    {  4.90,  1.34 },
    {  5.00,  1.30 },
    {  5.10,  1.30 },
    {  8.91,  1.30 },  // Approximate end of actual range
};

std::unique_ptr<FilterModelConfig8580> FilterModelConfig8580::instance(nullptr);

FilterModelConfig8580* FilterModelConfig8580::getInstance()
{
    if (!instance.get())
    {
        instance.reset(new FilterModelConfig8580());
    }

    return instance.get();
}

FilterModelConfig8580::FilterModelConfig8580() :
    FilterModelConfig(
        0.30,   // voice voltage range FIXME measure
        4.84,   // voice DC voltage FIXME measure
        22e-9,  // capacitor value
        9.09,   // Vdd
        0.80,   // Vth
        100e-6, // uCox
        opamp_voltage,
        OPAMP_SIZE
    )
{
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
            // The audio mixer operates at n ~ 8/5, and has 8 fundamentally different
            // input configurations (0 - 7 input "resistors").
            //
            // All "on", transistors are modeled as one - see comments above for
            // the filter summer.
            for (int i = 0; i < 8; i++)
            {
                const int idiv = (i == 0) ? 1 : i;
                const int size = (i == 0) ? 1 : i << 16;
                const double n = i * 8.0 / 5.0;
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
            // it follows that gain ~ vol/16 (assuming ideal
            // op-amps and ideal "resistors").
            for (int n8 = 0; n8 < 16; n8++)
            {
                const int size = 1 << 16;
                const double n = n8 / 16.0;
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
            // it follows that 1/Q ~ 2^((4 - res)/8) (assuming ideal
            // op-amps and ideal "resistors").
            for (int n8 = 0; n8 < 16; n8++)
            {
                const int size = 1 << 16;
                opampModel.reset();
                gain_res[n8] = new unsigned short[size];

                for (int vi = 0; vi < size; vi++)
                {
                    const double vin = vmin + vi / N16; /* vmin .. vmax */
                    gain_res[n8][vi] = getNormalizedValue(opampModel.solve(resGain[n8], vin));
                }
            }
        }
    }
}

std::unique_ptr<Integrator8580> FilterModelConfig8580::buildIntegrator()
{
    return MAKE_UNIQUE(Integrator8580, this);
}

} // namespace reSIDfp
