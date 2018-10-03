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

#ifndef __SID_FP_H__
#define __SID_FP_H__

#include "siddefs-fp.h"
#include "voice.h"
#include "filter.h"
#include "extfilt.h"
#include "pot.h"

class SIDFP
{
public:
  SIDFP();
  ~SIDFP();

  static float kinked_dac(const int x, const float nonlinearity, const int bits);
  bool sse_enabled() { return can_use_sse; }

  void set_chip_model(chip_model model);
  FilterFP& get_filter() { return filter; }
  void enable_filter(bool enable);
  void enable_external_filter(bool enable);
  bool set_sampling_parameters(float clock_freq, sampling_method method,
                               float sample_freq, float pass_freq = -1);
  void adjust_sampling_frequency(float sample_freq);
  void set_voice_nonlinearity(float nonlinearity);

  void clock();
  int clock(cycle_count& delta_t, short* buf, int n, int interleave = 1);
  void reset();
  
  // Read/write registers.
  reg8 read(reg8 offset);
  void write(reg8 offset, reg8 value);

  // Read/write state.
  class State
  {
  public:
    State();

    char sid_register[0x20];

    reg8 bus_value;
    cycle_count bus_value_ttl;

    reg24 accumulator[3];
    reg24 shift_register[3];
    reg16 rate_counter[3];
    reg16 rate_counter_period[3];
    reg16 exponential_counter[3];
    reg16 exponential_counter_period[3];
    reg8 envelope_counter[3];
    EnvelopeGeneratorFP::State envelope_state[3];
    bool hold_zero[3];
  };
    
  State read_state();
  void write_state(const State& state);

  // 16-bit input (EXT IN).
  void input(int sample);

  // output in range -32768 .. 32767, not clipped (AUDIO OUT)
  float output();

protected:
  static double I0(double x);
  RESID_INLINE int clock_interpolate(cycle_count& delta_t, short* buf, int n,
                                     int interleave);
  RESID_INLINE int clock_resample_interpolate(cycle_count& delta_t, short* buf,
                                              int n, int interleave);
  RESID_INLINE void age_bus_value(cycle_count);

  VoiceFP voice[3];
  FilterFP filter;
  ExternalFilterFP extfilt;
  PotentiometerFP potx;
  PotentiometerFP poty;

  reg8 bus_value;
  cycle_count bus_value_ttl;

  float clock_frequency;

  // External audio input.
  float ext_in;

  enum { RINGSIZE = 16384 };

  // Sampling variables.
  sampling_method sampling;
  float cycles_per_sample;
  float sample_offset;
  int sample_index;
  int fir_N;
  int fir_RES;
  
  // Linear interpolation helper
  float sample_prev;

  // Ring buffer with overflow for contiguous storage of RINGSIZE samples.
  float* sample;

  // FIR_RES filter tables (FIR_N*FIR_RES).
  float* fir;

  bool can_use_sse;
};

#endif // not __SID_H__
