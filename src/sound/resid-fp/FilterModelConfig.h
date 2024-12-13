/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2024 Leandro Nini <drfiemost@users.sourceforge.net>
 * Copyright 2007-2010 Antti Lankila
 * Copyright 2004,2010 Dag Lem
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

#ifndef FILTERMODELCONFIG_H
#define FILTERMODELCONFIG_H

#include <algorithm>
#include <random>
#include <cassert>

#include "OpAmp.h"
#include "Spline.h"

#include "sidcxx11.h"

namespace reSIDfp
{

class FilterModelConfig
{
private:
    /*
     * Hack to add quick dither when converting values from float to int
     * and avoid quantization noise.
     * Hopefully this can be removed the day we move all the analog part
     * processing to floats.
     *
     * Not sure about the effect of using such small buffer of numbers
     * since the random sequence repeats every 1024 values but for
     * now it seems to do the job.
     */
    class Randomnoise
    {
    private:
        double buffer[1024];
        mutable int index = 0;
    public:
        Randomnoise()
        {
            std::uniform_real_distribution<double> unif(0., 1.);
            std::default_random_engine re;
            for (int i=0; i<1024; i++)
                buffer[i] = unif(re);
        }
        double getNoise() const { index = (index + 1) & 0x3ff; return buffer[index]; }
    };

protected:
    /// Capacitor value.
    const double C;

    /// Transistor parameters.
    //@{
    /// Thermal voltage: Ut = kT/q = 8.61734315e-5*T ~ 26mV
    static constexpr double Ut = 26.0e-3;

    const double Vdd;           ///< Positive supply voltage
    const double Vth;           ///< Threshold voltage
    const double Vddt;          ///< Vdd - Vth
    double uCox;                ///< Transconductance coefficient: u*Cox
    //@}

    // Derived stuff
    const double vmin, vmax;
    const double denorm, norm;

    /// Fixed point scaling for 16 bit op-amp output.
    const double N16;

    const double voice_voltage_range;

    /// Current factor coefficient for op-amp integrators.
    double currFactorCoeff;

    /// Lookup tables for gain and summer op-amps in output stage / filter.
    //@{
    unsigned short* mixer[8];       //-V730_NOINIT this is initialized in the derived class constructor
    unsigned short* summer[5];      //-V730_NOINIT this is initialized in the derived class constructor
    unsigned short* volume[16];     //-V730_NOINIT this is initialized in the derived class constructor
    unsigned short* resonance[16];  //-V730_NOINIT this is initialized in the derived class constructor
    //@}

    /// Reverse op-amp transfer function.
    unsigned short opamp_rev[1 << 16]; //-V730_NOINIT this is initialized in the derived class constructor

private:
    Randomnoise rnd;

private:
    FilterModelConfig(const FilterModelConfig&) = delete;
    FilterModelConfig& operator= (const FilterModelConfig&) = delete;

    inline double getVoiceVoltage(float value, unsigned int env) const
    {
        return value * voice_voltage_range + getVoiceDC(env);
    }

protected:
    /**
     * @param vvr voice voltage range
     * @param c   capacitor value
     * @param vdd Vdd supply voltage
     * @param vth threshold voltage
     * @param ucox u*Cox
     * @param opamp_voltage opamp voltage array
     * @param opamp_size opamp voltage array size
     */
    FilterModelConfig(
        double vvr,
        double c,
        double vdd,
        double vth,
        double ucox,
        const Spline::Point *opamp_voltage,
        int opamp_size
    );

    ~FilterModelConfig();

    void setUCox(double new_uCox);

    virtual double getVoiceDC(unsigned int env) const = 0;

    /**
     * The filter summer operates at n ~ 1, and has 5 fundamentally different
     * input configurations (2 - 6 input "resistors").
     *
     * Note that all "on" transistors are modeled as one. This is not
     * entirely accurate, since the input for each transistor is different,
     * and transistors are not linear components. However modeling all
     * transistors separately would be extremely costly.
     */
    inline void buildSummerTable(const OpAmp& opampModel)
    {
        const double r_N16 = 1. / N16;

        for (int i = 0; i < 5; i++)
        {
            const int idiv = 2 + i;        // 2 - 6 input "resistors".
            const int size = idiv << 16;
            const double n = idiv;
            const double r_idiv = 1. / idiv;
            opampModel.reset();
            summer[i] = new unsigned short[size];

            for (int vi = 0; vi < size; vi++)
            {
                const double vin = vmin + vi * r_N16 * r_idiv; /* vmin .. vmax */
                summer[i][vi] = getNormalizedValue(opampModel.solve(n, vin));
            }
        }
    }

    /**
     * The audio mixer operates at n ~ 8/6 (6581) or 8/5 (8580),
     * and has 8 fundamentally different input configurations
     * (0 - 7 input "resistors").
     *
     * All "on", transistors are modeled as one - see comments above for
     * the filter summer.
     */
    inline void buildMixerTable(const OpAmp& opampModel, double nRatio)
    {
        const double r_N16 = 1. / N16;

        for (int i = 0; i < 8; i++)
        {
            const int idiv = (i == 0) ? 1 : i;
            const int size = (i == 0) ? 1 : i << 16;
            const double n = i * nRatio;
            const double r_idiv = 1. / idiv;
            opampModel.reset();
            mixer[i] = new unsigned short[size];

            for (int vi = 0; vi < size; vi++)
            {
                const double vin = vmin + vi * r_N16 * r_idiv; /* vmin .. vmax */
                mixer[i][vi] = getNormalizedValue(opampModel.solve(n, vin));
            }
        }
    }

    /**
     * 4 bit "resistor" ladders in the audio output gain
     * necessitate 16 gain tables.
     * From die photographs of the volume "resistor" ladders
     * it follows that gain ~ vol/12 (6581) or vol/16 (8580)
     * (assuming ideal op-amps and ideal "resistors").
     */
    inline void buildVolumeTable(const OpAmp& opampModel, double nDivisor)
    {
        const double r_N16 = 1. / N16;

        for (int n8 = 0; n8 < 16; n8++)
        {
            const int size = 1 << 16;
            const double n = n8 / nDivisor;
            opampModel.reset();
            volume[n8] = new unsigned short[size];

            for (int vi = 0; vi < size; vi++)
            {
                const double vin = vmin + vi * r_N16; /* vmin .. vmax */
                volume[n8][vi] = getNormalizedValue(opampModel.solve(n, vin));
            }
        }
    }

    /**
     * 4 bit "resistor" ladders in the bandpass resonance gain
     * necessitate 16 gain tables.
     * From die photographs of the bandpass "resistor" ladders
     * it follows that 1/Q ~ ~res/8 (6581) or 2^((4 - res)/8) (8580)
     * (assuming ideal op-amps and ideal "resistors").
     */
    inline void buildResonanceTable(const OpAmp& opampModel, const double resonance_n[16])
    {
        const double r_N16 = 1. / N16;

        for (int n8 = 0; n8 < 16; n8++)
        {
            const int size = 1 << 16;
            opampModel.reset();
            resonance[n8] = new unsigned short[size];

            for (int vi = 0; vi < size; vi++)
            {
                const double vin = vmin + vi * r_N16; /* vmin .. vmax */
                resonance[n8][vi] = getNormalizedValue(opampModel.solve(resonance_n[n8], vin));
            }
        }
    }

public:
    unsigned short** getVolume() { return volume; }
    unsigned short** getResonance() { return resonance; }
    unsigned short** getSummer() { return summer; }
    unsigned short** getMixer() { return mixer; }

    inline unsigned short getOpampRev(int i) const { return opamp_rev[i]; }
    inline double getVddt() const { return Vddt; }
    inline double getVth() const { return Vth; }

    // helper functions

    inline unsigned short getNormalizedValue(double value) const
    {
        const double tmp = N16 * (value - vmin);
        assert(tmp >= 0. && tmp <= 65535.);
        return static_cast<unsigned short>(tmp + rnd.getNoise());
    }

    inline unsigned short getNormalizedCurrentFactor(double wl) const
    {
        const double tmp = (1 << 13) * currFactorCoeff * wl;
        assert(tmp > -0.5 && tmp < 65535.5);
        return static_cast<unsigned short>(tmp + 0.5);
    }

    inline unsigned short getNVmin() const
    {
        const double tmp = N16 * vmin;
        assert(tmp > -0.5 && tmp < 65535.5);
        return static_cast<unsigned short>(tmp + 0.5);
    }

    inline int getNormalizedVoice(float value, unsigned int env) const
    {
        return static_cast<int>(getNormalizedValue(getVoiceVoltage(value, env)));
    }
};

} // namespace reSIDfp

#endif
