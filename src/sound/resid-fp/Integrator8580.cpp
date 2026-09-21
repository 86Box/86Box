/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2014-2016 Leandro Nini <drfiemost@users.sourceforge.net>
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

#include "Integrator8580.h"

namespace reSIDfp
{

int Integrator8580::solve(int vi) const
{
    // Make sure we're not in subthreshold mode
    assert(vx < nVgt);

    // DAC voltages
    const unsigned int Vgst = nVgt - vx;
    const unsigned int Vgdt = (vi < nVgt) ? nVgt - vi : 0;  // triode/saturation mode

    const unsigned int Vgst_2 = Vgst * Vgst;
    const unsigned int Vgdt_2 = Vgdt * Vgdt;

    // DAC current, scaled by (1/m)*2^13*m*2^16*m*2^16*2^-15 = m*2^30
    const int n_I_dac = n_dac * (static_cast<int>(Vgst_2 - Vgdt_2) >> 15);

    // Change in capacitor charge.
    vc += n_I_dac;

    // vx = g(vc)
    const int tmp = (vc >> 15) + (1 << 15);
    assert(tmp < (1 << 16));
    vx = fmc.getOpampRev(tmp);

    // Return vo.
    return vx - (vc >> 14);
}

} // namespace reSIDfp
