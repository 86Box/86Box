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

#define __VOICE_CC__
#include "voice.h"
#include "sid.h"

// ----------------------------------------------------------------------------
// Constructor.
// ----------------------------------------------------------------------------
VoiceFP::VoiceFP()
{
  nonlinearity = 1.f;
  set_chip_model(MOS6581FP);
}

/* Keep this at 1.f for 8580, there are no 6581-only codepaths in this file! */
void VoiceFP::set_nonlinearity(float nl)
{
    nonlinearity = nl;
    calculate_dac_tables();
}

// ----------------------------------------------------------------------------
// Set chip model.
// ----------------------------------------------------------------------------
void VoiceFP::set_chip_model(chip_model model)
{
  wave.set_chip_model(model);

  if (model == MOS6581FP) {
    /* there is some level from each voice even if the env is down and osc
     * is stopped. You can hear this by routing a voice into filter (filter
     * should be kept disabled for this) as the master level changes. This
     * tunable affects the volume of digis. */
    voice_DC = 0x800 * 0xff;
    /* In 8580 the waveforms seem well centered, but on the 6581 there is some
     * offset change as envelope grows, indicating that the waveforms are not
     * perfectly centered. I estimate the value ~ 0x600 for my R4AR, and ReSID
     * has used another measurement technique and got 0x380. */
    wave_zero = 0x600;
    calculate_dac_tables();
  }
  else {
    /* 8580 is thought to be perfect, apart from small negative offset due to
     * ext-in mixing, I think. */
    voice_DC = 0;
    wave_zero = 0x800;
    calculate_dac_tables();
  }
}

void VoiceFP::calculate_dac_tables()
{
    int i;
    for (i = 0; i < 256; i ++)
        env_dac[i] = SIDFP::kinked_dac(i, nonlinearity, 8);
    for (i = 0; i < 4096; i ++)
        voice_dac[i] = SIDFP::kinked_dac(i, nonlinearity, 12) - wave_zero;
}

// ----------------------------------------------------------------------------
// Set sync source.
// ----------------------------------------------------------------------------
void VoiceFP::set_sync_source(VoiceFP* source)
{
  wave.set_sync_source(&source->wave);
}

// ----------------------------------------------------------------------------
// Register functions.
// ----------------------------------------------------------------------------
void VoiceFP::writeCONTROL_REG(reg8 control)
{
  wave.writeCONTROL_REG(control);
  envelope.writeCONTROL_REG(control);
}

// ----------------------------------------------------------------------------
// SID reset.
// ----------------------------------------------------------------------------
void VoiceFP::reset()
{
  wave.reset();
  envelope.reset();
}
