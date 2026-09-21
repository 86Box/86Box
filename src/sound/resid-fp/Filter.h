/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2024 Leandro Nini <drfiemost@users.sourceforge.net>
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

#ifndef FILTER_H
#define FILTER_H

#include "FilterModelConfig.h"

#include "siddefs-fp.h"

namespace reSIDfp
{

/**
 * SID filter base class
 */
class Filter
{
private:
    unsigned short** mixer;
    unsigned short** summer;
    unsigned short** resonance;
    unsigned short** volume;

protected:
    FilterModelConfig& fmc;

    /// Current filter/voice mixer setting.
    unsigned short* currentMixer = nullptr;

    /// Filter input summer setting.
    unsigned short* currentSummer = nullptr;

    /// Filter resonance value.
    unsigned short* currentResonance = nullptr;

    /// Current volume amplifier setting.
    unsigned short* currentVolume = nullptr;

    /// Filter highpass state.
    int Vhp = 0;

    /// Filter bandpass state.
    int Vbp = 0;

    /// Filter lowpass state.
    int Vlp = 0;

    /// Filter external input.
    int Ve = 0;

    /// Filter cutoff frequency.
    unsigned int fc = 0;

    /// Routing to filter or outside filter
    //@{
    bool filt1 = false;
    bool filt2 = false;
    bool filt3 = false;
    bool filtE = false;
    //@}

    /// Switch voice 3 off.
    bool voice3off = false;

    /// Highpass, bandpass, and lowpass filter modes.
    //@{
    bool hp = false;
    bool bp = false;
    bool lp = false;
    //@}

private:
    /// Current volume.
    unsigned char vol = 0;

    /// Filter enabled.
    bool enabled = true;

    /// Selects which inputs to route through filter.
    unsigned char filt = 0;

protected:
    /**
     * Update filter cutoff frequency.
     */
    virtual void updateCenterFrequency() = 0;

    /**
     * Update filter resonance.
     *
     * @param res the new resonance value
     */
    void updateResonance(unsigned char res) { currentResonance = resonance[res]; }

    /**
     * Mixing configuration modified (offsets change)
     */
    void updateMixing();

    /**
     * Get the filter cutoff register value
     */
    unsigned int getFC() const { return fc; }

public:
    Filter(FilterModelConfig& fmc);

    virtual ~Filter() = default;

    /**
     * SID clocking - 1 cycle
     *
     * @param v1 voice 1 in
     * @param v2 voice 2 in
     * @param v3 voice 3 in
     * @return filtered output
     */
    virtual unsigned short clock(int v1, int v2, int v3) = 0;

    /**
     * Enable filter.
     *
     * @param enable
     */
    void enable(bool enable);

    /**
     * SID reset.
     */
    void reset();

    /**
     * Write Frequency Cutoff Low register.
     *
     * @param fc_lo Frequency Cutoff Low-Byte
     */
    void writeFC_LO(unsigned char fc_lo);

    /**
     * Write Frequency Cutoff High register.
     *
     * @param fc_hi Frequency Cutoff High-Byte
     */
    void writeFC_HI(unsigned char fc_hi);

    /**
     * Write Resonance/Filter register.
     *
     * @param res_filt Resonance/Filter
     */
    void writeRES_FILT(unsigned char res_filt);

    /**
     * Write filter Mode/Volume register.
     *
     * @param mode_vol Filter Mode/Volume
     */
    void writeMODE_VOL(unsigned char mode_vol);

    /**
     * Apply a signal to EXT-IN
     *
     * @param input a signed 16 bit sample
     */
    void input(short input) { Ve = fmc.getNormalizedVoice(input/32768.f, 0); }

    inline int getNormalizedVoice(float value, unsigned int env) const { return fmc.getNormalizedVoice(value, env); }
};

} // namespace reSIDfp

#endif
