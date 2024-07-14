/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2023 Leandro Nini <drfiemost@users.sourceforge.net>
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
#include <cassert>

#include "Spline.h"

#include "sidcxx11.h"

namespace reSIDfp
{

class FilterModelConfig
{
protected:
    const double voice_voltage_range;
    const double voice_DC_voltage;

    /// Capacitor value.
    const double C;

    /// Transistor parameters.
    //@{
    const double Vdd;
    const double Vth;           ///< Threshold voltage
    const double Ut;            ///< Thermal voltage: Ut = kT/q = 8.61734315e-5*T ~ 26mV
    const double uCox;          ///< Transconductance coefficient: u*Cox
    const double Vddt;          ///< Vdd - Vth
    //@}

    // Derived stuff
    const double vmin, vmax;
    const double denorm, norm;

    /// Fixed point scaling for 16 bit op-amp output.
    const double N16;

    /// Current factor coefficient for op-amp integrators.
    const double currFactorCoeff;

    /// Lookup tables for gain and summer op-amps in output stage / filter.
    //@{
    unsigned short* mixer[8];       //-V730_NOINIT this is initialized in the derived class constructor
    unsigned short* summer[5];      //-V730_NOINIT this is initialized in the derived class constructor
    unsigned short* gain_vol[16];   //-V730_NOINIT this is initialized in the derived class constructor
    unsigned short* gain_res[16];   //-V730_NOINIT this is initialized in the derived class constructor
    //@}

    /// Reverse op-amp transfer function.
    unsigned short opamp_rev[1 << 16]; //-V730_NOINIT this is initialized in the derived class constructor

private:
    FilterModelConfig (const FilterModelConfig&) DELETE;
    FilterModelConfig& operator= (const FilterModelConfig&) DELETE;

protected:
    /**
     * @param vvr voice voltage range
     * @param vdv voice DC voltage
     * @param c   capacitor value
     * @param vdd Vdd
     * @param vth threshold voltage
     * @param ucox u*Cox
     * @param ominv opamp min voltage
     * @param omaxv opamp max voltage
     */
    FilterModelConfig(
        double vvr,
        double vdv,
        double c,
        double vdd,
        double vth,
        double ucox,
        const Spline::Point *opamp_voltage,
        int opamp_size
    );

    ~FilterModelConfig()
    {
        for (int i = 0; i < 8; i++)
        {
            delete [] mixer[i];
        }

        for (int i = 0; i < 5; i++)
        {
            delete [] summer[i];
        }

        for (int i = 0; i < 16; i++)
        {
            delete [] gain_vol[i];
            delete [] gain_res[i];
        }
    }

public:
    unsigned short** getGainVol() { return gain_vol; }
    unsigned short** getGainRes() { return gain_res; }
    unsigned short** getSummer() { return summer; }
    unsigned short** getMixer() { return mixer; }

    /**
     * The digital range of one voice is 20 bits; create a scaling term
     * for multiplication which fits in 11 bits.
     */
    int getVoiceScaleS11() const { return static_cast<int>((norm * ((1 << 11) - 1)) * voice_voltage_range); }

    /**
     * The "zero" output level of the voices.
     */
    int getNormalizedVoiceDC() const { return static_cast<int>(N16 * (voice_DC_voltage - vmin)); }

    inline unsigned short getOpampRev(int i) const { return opamp_rev[i]; }
    inline double getVddt() const { return Vddt; }
    inline double getVth() const { return Vth; }

    // helper functions
    inline unsigned short getNormalizedValue(double value) const
    {
        const double tmp = N16 * (value - vmin);
        assert(tmp > -0.5 && tmp < 65535.5);
        return static_cast<unsigned short>(tmp + 0.5);
    }

    inline unsigned short getNormalizedCurrentFactor(double wl) const
    {
        const double tmp = (1 << 13) * currFactorCoeff * wl;
        assert(tmp > -0.5 && tmp < 65535.5);
        return static_cast<unsigned short>(tmp + 0.5);
    }

    inline unsigned short getNVmin() const {
        const double tmp = N16 * vmin;
        assert(tmp > -0.5 && tmp < 65535.5);
        return static_cast<unsigned short>(tmp + 0.5);
    }
};

} // namespace reSIDfp

#endif
