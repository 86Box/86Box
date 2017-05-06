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

#define __WAVE_CC__
#include "wave.h"

// ----------------------------------------------------------------------------
// Constructor.
// ----------------------------------------------------------------------------
WaveformGeneratorFP::WaveformGeneratorFP()
{
  sync_source = this;

  set_chip_model(MOS6581FP);

  reset();
}


// ----------------------------------------------------------------------------
// Set sync source.
// ----------------------------------------------------------------------------
void WaveformGeneratorFP::set_sync_source(WaveformGeneratorFP* source)
{
  sync_source = source;
  source->sync_dest = this;
}


// ----------------------------------------------------------------------------
// Set chip model.
// ----------------------------------------------------------------------------
void WaveformGeneratorFP::set_chip_model(chip_model model)
{
  if (model == MOS6581FP) {
    wave__ST = wave6581__ST;
    wave_P_T = wave6581_P_T;
    wave_PS_ = wave6581_PS_;
    wave_PST = wave6581_PST;
  }
  else {
    wave__ST = wave8580__ST;
    wave_P_T = wave8580_P_T;
    wave_PS_ = wave8580_PS_;
    wave_PST = wave8580_PST;
  }
}


// ----------------------------------------------------------------------------
// Register functions.
// ----------------------------------------------------------------------------
void WaveformGeneratorFP::writeFREQ_LO(reg8 freq_lo)
{
  freq = (freq & 0xff00) | (freq_lo & 0x00ff);
}

void WaveformGeneratorFP::writeFREQ_HI(reg8 freq_hi)
{
  freq = ((freq_hi << 8) & 0xff00) | (freq & 0x00ff);
}

/* The original form was (acc >> 12) >= pw, where truth value is not affected
 * by the contents of the low 12 bits. Therefore the lowest bits must be zero
 * in the new formulation acc >= (pw << 12). */
void WaveformGeneratorFP::writePW_LO(reg8 pw_lo)
{
  pw = (pw & 0xf00) | (pw_lo & 0x0ff);
  pw_acc_scale = pw << 12;
}

void WaveformGeneratorFP::writePW_HI(reg8 pw_hi)
{
  pw = ((pw_hi << 8) & 0xf00) | (pw & 0x0ff);
  pw_acc_scale = pw << 12;
}

void WaveformGeneratorFP::writeCONTROL_REG(reg8 control)
{
  waveform = (control >> 4) & 0x0f;
  ring_mod = control & 0x04;
  sync = control & 0x02;

  reg8 test_next = control & 0x08;

  /* SounDemoN found out that test bit can be used to control the noise
   * register. Hear the result in Bojojoing.sid. */

  // testbit set. invert bit 19 and write it to bit 1
  if (test_next && !test) {
    accumulator = 0;
    reg24 bit19 = (shift_register >> 19) & 1;
    shift_register = (shift_register & 0x7ffffd) | ((bit19^1) << 1);
    noise_overwrite_delay = 200000; /* 200 ms, probably too generous? */
  }
  // Test bit cleared.
  // The accumulator starts counting, and the shift register is reset to
  // the value 0x7ffff8.
  else if (!test_next && test) {
    reg24 bit0 = ((shift_register >> 22) ^ (shift_register >> 17)) & 0x1;
    shift_register <<= 1;
    shift_register |= bit0;
  }
  // clear output bits of shift register if noise and other waveforms
  // are selected simultaneously
  if (waveform > 8) {
    shift_register &= 0x7fffff^(1<<22)^(1<<20)^(1<<16)^(1<<13)^(1<<11)^(1<<7)^(1<<4)^(1<<2);
  }

  test = test_next;
  
  /* update noise anyway, just in case the above paths triggered */
  noise_output_cached = outputN___();
}

reg8 WaveformGeneratorFP::readOSC()
{
  return output() >> 4;
}

// ----------------------------------------------------------------------------
// SID reset.
// ----------------------------------------------------------------------------
void WaveformGeneratorFP::reset()
{
  accumulator = 0;
  previous = 0;
  shift_register = 0x7ffffc;
  freq = 0;
  pw = 0;
  pw_acc_scale = 0;
  test = 0;
  writeCONTROL_REG(0);
  msb_rising = false;
}
