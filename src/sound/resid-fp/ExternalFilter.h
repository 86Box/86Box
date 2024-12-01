/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2020 Leandro Nini <drfiemost@users.sourceforge.net>
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

#ifndef EXTERNALFILTER_H
#define EXTERNALFILTER_H

#include "siddefs-fp.h"

namespace reSIDfp
{

/**
 * The audio output stage in a Commodore 64 consists of two STC networks, a
 * low-pass RC filter with 3 dB frequency 16kHz followed by a DC-blocker which
 * acts as a high-pass filter with a cutoff dependent on the attached audio
 * equipment impedance. Here we suppose an impedance of 10kOhm resulting
 * in a 3 dB attenuation at 1.6Hz.
 * To operate properly the 6581 audio output needs a pull-down resistor
 *(1KOhm recommended, not needed on 8580)
 *
 * ~~~
 *                                 9/12V
 * -----+
 * audio|       10k                  |
 *      +---o----R---o--------o-----(K)          +-----
 *  out |   |        |        |      |           |audio
 * -----+   R 1k     C 1000   |      |    10 uF  |
 *          |        |  pF    +-C----o-----C-----+ 10k
 *                             470   |           |
 *         GND      GND         pF   R 1K        | amp
 *          *                   *    |           +-----
 *
 *                                  GND
 * ~~~
 *
 * The STC networks are connected with a [BJT] based [common collector]
 * used as a voltage follower (featuring a 2SC1815 NPN transistor).
 * * The C64c board additionally includes a [bootstrap] condenser to increase
 * the input impedance of the common collector.
 *
 * [BJT]: https://en.wikipedia.org/wiki/Bipolar_junction_transistor
 * [common collector]: https://en.wikipedia.org/wiki/Common_collector
 * [bootstrap]: https://en.wikipedia.org/wiki/Bootstrapping_(electronics)
 */
class ExternalFilter
{
private:
    /// Lowpass filter voltage
    int Vlp;

    /// Highpass filter voltage
    int Vhp;

    int w0lp_1_s7;

    int w0hp_1_s17;

public:
    /**
     * SID clocking.
     *
     * @param input
     */
    int clock(unsigned short input);

    /**
     * Constructor.
     */
    ExternalFilter();

    /**
     * Setup of the external filter sampling parameters.
     *
     * @param frequency the main system clock frequency
     */
    void setClockFrequency(double frequency);

    /**
     * SID reset.
     */
    void reset();
};

} // namespace reSIDfp

#if RESID_INLINING || defined(EXTERNALFILTER_CPP)

namespace reSIDfp
{

RESID_INLINE
int ExternalFilter::clock(unsigned short input)
{
    const int Vi = (static_cast<unsigned int>(input)<<11) - (1 << (11+15));
    const int dVlp = (w0lp_1_s7 * (Vi - Vlp) >> 7);
    const int dVhp = (w0hp_1_s17 * (Vlp - Vhp) >> 17);
    Vlp += dVlp;
    Vhp += dVhp;
    return (Vlp - Vhp) >> 11;
}

} // namespace reSIDfp

#endif

#endif
