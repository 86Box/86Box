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

#define __EXTFILT_CC__
#include "extfilt.h"

// ----------------------------------------------------------------------------
// Constructor.
// ----------------------------------------------------------------------------
ExternalFilterFP::ExternalFilterFP()
{
  reset();
  enable_filter(true);
  set_chip_model(MOS6581FP);
  set_clock_frequency(1e6f);
  set_sampling_parameter(15915.6f);
}


// ----------------------------------------------------------------------------
// Enable filter.
// ----------------------------------------------------------------------------
void ExternalFilterFP::enable_filter(bool enable)
{
  enabled = enable;
}

// ----------------------------------------------------------------------------
// Setup of the external filter sampling parameters.
// ----------------------------------------------------------------------------
void ExternalFilterFP::set_clock_frequency(float clock)
{
  clock_frequency = clock;
  _set_sampling_parameter();
}

void ExternalFilterFP::set_sampling_parameter(float freq)
{
  pass_frequency = freq;
  _set_sampling_parameter();
}

void ExternalFilterFP::_set_sampling_parameter()
{
  // Low-pass:  R = 10kOhm, C = 1000pF; w0l = 1/RC = 1/(1e4*1e-9) = 100000
  // High-pass: R =  1kOhm, C =   10uF; w0h = 1/RC = 1/(1e3*1e-5) =    100
  w0hp = 100.f / clock_frequency;
  w0lp = pass_frequency * 2.f * M_PI_f / clock_frequency;
}

// ----------------------------------------------------------------------------
// Set chip model.
// ----------------------------------------------------------------------------
void ExternalFilterFP::set_chip_model(chip_model model)
{
  if (model == MOS6581FP) {
    // Approximate the DC output level to be removed if the external
    // filter is turned off. (0x800 - wave_zero + voice DC) * maxenv * voices
    //  - extin offset...
    mixer_DC = (-0x600 + 0x800) * 0xff * 3 - 0x20000;
  }
  else {
    // No DC offsets in the MOS8580.
    mixer_DC = 0;
  }
}


// ----------------------------------------------------------------------------
// SID reset.
// ----------------------------------------------------------------------------
void ExternalFilterFP::reset()
{
  // State of filter.
  Vlp = 0;
  Vhp = 0;
  Vo = 0;
}
