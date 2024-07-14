/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2022 Leandro Nini <drfiemost@users.sourceforge.net>
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

#ifndef VOICE_H
#define VOICE_H

#include <memory>

#include "siddefs-fp.h"
#include "WaveformGenerator.h"
#include "EnvelopeGenerator.h"

#include "sidcxx11.h"

namespace reSIDfp
{

/**
 * Representation of SID voice block.
 */
class Voice
{
private:
    std::unique_ptr<WaveformGenerator> const waveformGenerator;

    std::unique_ptr<EnvelopeGenerator> const envelopeGenerator;

    /// The DAC LUT for analog waveform output
    float* wavDAC; //-V730_NOINIT this is initialized in the SID constructor

    /// The DAC LUT for analog envelope output
    float* envDAC; //-V730_NOINIT this is initialized in the SID constructor

public:
    /**
     * Amplitude modulated waveform output.
     *
     * The waveform DAC generates a voltage between virtual ground and Vdd
     * (5-12 V for the 6581 and 4.75-9 V for the 8580)
     * corresponding to oscillator state 0 .. 4095.
     *
     * The envelope DAC generates a voltage between waveform gen output and
     * the virtual ground level, corresponding to envelope state 0 .. 255.
     *
     * Ideal range [-2048*255, 2047*255].
     *
     * @param ringModulator Ring-modulator for waveform
     * @return the voice analog output
     */
    RESID_INLINE
    int output(const WaveformGenerator* ringModulator) const
    {
        unsigned int const wav = waveformGenerator->output(ringModulator);
        unsigned int const env = envelopeGenerator->output();

        // DAC imperfections are emulated by using the digital output
        // as an index into a DAC lookup table.
        return static_cast<int>(wavDAC[wav] * envDAC[env]);
    }

    /**
     * Constructor.
     */
    Voice() :
        waveformGenerator(new WaveformGenerator()),
        envelopeGenerator(new EnvelopeGenerator()) {}

    /**
     * Set the analog DAC emulation for waveform generator.
     * Must be called before any operation.
     *
     * @param dac
     */
    void setWavDAC(float* dac) { wavDAC = dac; }

    /**
     * Set the analog DAC emulation for envelope.
     * Must be called before any operation.
     *
     * @param dac
     */
    void setEnvDAC(float* dac) { envDAC = dac; }

    WaveformGenerator* wave() const { return waveformGenerator.get(); }

    EnvelopeGenerator* envelope() const { return envelopeGenerator.get(); }

    /**
     * Write control register.
     *
     * @param control Control register value.
     */
    void writeCONTROL_REG(unsigned char control)
    {
        waveformGenerator->writeCONTROL_REG(control);
        envelopeGenerator->writeCONTROL_REG(control);
    }

    /**
     * SID reset.
     */
    void reset()
    {
        waveformGenerator->reset();
        envelopeGenerator->reset();
    }
};

} // namespace reSIDfp

#endif
