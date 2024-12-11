/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2014 Leandro Nini <drfiemost@users.sourceforge.net>
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

#include "Integrator6581.h"

#ifdef SLOPE_FACTOR
#  include <cmath>
#  include "sidcxx11.h"
#endif

namespace reSIDfp
{

int Integrator6581::solve(int vi) const
{
    // Make sure Vgst>0 so we're not in subthreshold mode
    assert(vx < nVddt);

    // Check that transistor is actually in triode mode
    // Vds < Vgs - Vth
    assert(vi < nVddt);

    // "Snake" voltages for triode mode calculation.
    const unsigned int Vgst = nVddt - vx;
    const unsigned int Vgdt = nVddt - vi;

    const unsigned int Vgst_2 = Vgst * Vgst;
    const unsigned int Vgdt_2 = Vgdt * Vgdt;

    // "Snake" current, scaled by (1/m)*2^13*m*2^16*m*2^16*2^-15 = m*2^30
    const int n_I_snake = fmc.getNormalizedCurrentFactor(wlSnake) * (static_cast<int>(Vgst_2 - Vgdt_2) >> 15);

    // VCR gate voltage.       // Scaled by m*2^16
    // Vg = Vddt - sqrt(((Vddt - Vw)^2 + Vgdt^2)/2)
    const int nVg = static_cast<int>(fmc.getVcr_nVg((nVddt_Vw_2 + (Vgdt_2 >> 1)) >> 16));
#ifdef SLOPE_FACTOR
    const double nVp = static_cast<double>(nVg - nVt) / n; // Pinch-off voltage
    const int kVgt = static_cast<int>(nVp + 0.5) - nVmin;
#else
    const int kVgt = (nVg - nVt) - nVmin;
#endif

    // VCR voltages for EKV model table lookup.
    const int kVgt_Vs = (kVgt - vx) + (1 << 15);
    assert((kVgt_Vs >= 0) && (kVgt_Vs < (1 << 16)));
    const int kVgt_Vd = (kVgt - vi) + (1 << 15);
    assert((kVgt_Vd >= 0) && (kVgt_Vd < (1 << 16)));

    // VCR current, scaled by m*2^15*2^15 = m*2^30
    const unsigned int If = static_cast<unsigned int>(fmc.getVcr_n_Ids_term(kVgt_Vs)) << 15;
    const unsigned int Ir = static_cast<unsigned int>(fmc.getVcr_n_Ids_term(kVgt_Vd)) << 15;
#ifdef SLOPE_FACTOR
    const double iVcr = static_cast<double>(If - Ir);
    const int n_I_vcr = static_cast<int>(iVcr * n);
#else
    const int n_I_vcr = If - Ir;
#endif

#ifdef SLOPE_FACTOR
    // estimate new slope factor based on gate voltage
    constexpr double gamma = 1.0;   // body effect factor
    constexpr double phi = 0.8;     // bulk Fermi potential
    const double Vp = nVp / fmc.getN16();
    n = 1. + (gamma / (2. * std::sqrt(Vp + phi + 4. * fmc.getUt())));
    assert((n > 1.2) && (n < 1.8));
#endif

    // Change in capacitor charge.
    vc += n_I_snake + n_I_vcr;

    // vx = g(vc)
    const int tmp = (vc >> 15) + (1 << 15);
    assert(tmp < (1 << 16));
    vx = fmc.getOpampRev(tmp);

    // Return vo.
    return vx - (vc >> 14);
}

} // namespace reSIDfp
