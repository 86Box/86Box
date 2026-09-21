/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2024 Leandro Nini <drfiemost@users.sourceforge.net>
 * Copyright 2007-2010 Antti Lankila
 * Copyright 2004,2010 Dag Lem <resid@nimrod.no>
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

#include "Filter6581.h"

#include "Integrator6581.h"

namespace reSIDfp
{

unsigned short Filter6581::clock(int voice1, int voice2, int voice3)
{
    const int V1 = voice1;
    const int V2 = voice2;
    // Voice 3 is silenced by voice3off if it is not routed through the filter.
    const int V3 = (filt3 || !voice3off) ? voice3 : 0;

    int Vsum = 0;
    int Vmix = 0;

    (filt1 ? Vsum : Vmix) += V1;
    (filt2 ? Vsum : Vmix) += V2;
    (filt3 ? Vsum : Vmix) += V3;
    (filtE ? Vsum : Vmix) += Ve;

    Vhp = currentSummer[currentResonance[Vbp] + Vlp + Vsum];
    Vbp = hpIntegrator.solve(Vhp);
    Vlp = bpIntegrator.solve(Vbp);

    int Vfilt = 0;
    if (lp) Vfilt += Vlp;
    if (bp) Vfilt += Vbp;
    if (hp) Vfilt += Vhp;

    // The filter input resistors are slightly bigger than the voice ones
    // Scale the values accordingly
    constexpr int filterGain = static_cast<int>(0.93 * (1 << 12));
    Vfilt = (Vfilt * filterGain) >> 12;

    return currentVolume[currentMixer[Vmix + Vfilt]];
}

Filter6581::~Filter6581()
{
    delete [] f0_dac;
}

void Filter6581::updateCenterFrequency()
{
    const unsigned short Vw = f0_dac[getFC()];
    hpIntegrator.setVw(Vw);
    bpIntegrator.setVw(Vw);
}

void Filter6581::setFilterCurve(double curvePosition)
{
    delete [] f0_dac;
    f0_dac = FilterModelConfig6581::getInstance()->getDAC(curvePosition);
    updateCenterFrequency();
}

void Filter6581::setFilterRange(double adjustment)
{
    FilterModelConfig6581::getInstance()->setFilterRange(adjustment);
}

} // namespace reSIDfp
