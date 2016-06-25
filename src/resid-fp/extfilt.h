//  ---------------------------------------------------------------------------
//  This file is part of reSID, a MOS6581 SID emulator engine.
//  Copyright (C) 2004  Dag Lem <resid@nimrod.no>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//  ---------------------------------------------------------------------------

#ifndef __EXTFILT_H__
#define __EXTFILT_H__

#include <math.h>

#include "siddefs-fp.h"

// ----------------------------------------------------------------------------
// The audio output stage in a Commodore 64 consists of two STC networks,
// a low-pass filter with 3-dB frequency 16kHz followed by a high-pass
// filter with 3-dB frequency 16Hz (the latter provided an audio equipment
// input impedance of 1kOhm).
// The STC networks are connected with a BJT supposedly meant to act as
// a unity gain buffer, which is not really how it works. A more elaborate
// model would include the BJT, however DC circuit analysis yields BJT
// base-emitter and emitter-base impedances sufficiently low to produce
// additional low-pass and high-pass 3dB-frequencies in the order of hundreds
// of kHz. This calls for a sampling frequency of several MHz, which is far
// too high for practical use.
// ----------------------------------------------------------------------------
class ExternalFilterFP
{
public:
  ExternalFilterFP();

  void enable_filter(bool enable);
  void set_sampling_parameter(float pass_freq);
  void set_chip_model(chip_model model);
  void set_clock_frequency(float);

  RESID_INLINE void clock(float Vi);
  void reset();

  // Audio output (20 bits).
  RESID_INLINE float output();

private:
  void _set_sampling_parameter();
  void nuke_denormals();

  // Filter enabled.
  bool enabled;

  // Maximum mixer DC offset.
  float mixer_DC;

  // Relevant clocks
  float clock_frequency, pass_frequency;

  // State of filters.
  float Vlp; // lowpass
  float Vhp; // highpass
  float Vo;

  // Cutoff frequencies.
  float w0lp;
  float w0hp;

friend class SIDFP;
};

// ----------------------------------------------------------------------------
// SID clocking - 1 cycle.
// ----------------------------------------------------------------------------
RESID_INLINE
void ExternalFilterFP::clock(float Vi)
{
  // This is handy for testing.
  if (! enabled) {
    // Remove maximum DC level since there is no filter to do it.
    Vlp = Vhp = 0.f;
    Vo = Vi - mixer_DC;
    return;
  }

  float dVlp = w0lp * (Vi - Vlp);
  float dVhp = w0hp * (Vlp - Vhp);
  Vo = Vlp - Vhp;
  Vlp += dVlp;
  Vhp += dVhp;
}

// ----------------------------------------------------------------------------
// Audio output (19.5 bits).
// ----------------------------------------------------------------------------
RESID_INLINE
float ExternalFilterFP::output()
{
  return Vo;
}

RESID_INLINE
void ExternalFilterFP::nuke_denormals()
{
    if (Vhp > -1e-12f && Vhp < 1e-12f)
        Vhp = 0;
    if (Vlp > -1e-12f && Vlp < 1e-12f)
        Vlp = 0;
}

#endif // not __EXTFILT_H__
