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
//  Filter distortion code written by Antti S. Lankila 2007 - 2008.

#define __FILTER_CC__
#include "filter.h"
#include "sid.h"
  
#ifndef HAVE_LOGF_PROTOTYPE
extern float logf(float val);
#endif

#ifndef HAVE_EXPF_PROTOTYPE
extern float expf(float val);
#endif

#ifndef HAVE_LOGF
float logf(float val)
{
    return (float)log((double)val);
}
#endif

#ifndef HAVE_EXPF
float expf(float val)
{
    return (float)exp((double)val);
}
#endif

// ----------------------------------------------------------------------------
// Constructor.
// ----------------------------------------------------------------------------
FilterFP::FilterFP()
{
  model = (chip_model) 0; // neither 6581/8580; init time only
  enable_filter(true);
  /* approximate; sid.cc calls us when set_sampling_parameters() occurs. */
  set_clock_frequency(1e6f);
  /* these parameters are a work-in-progress. */
  set_distortion_properties(2.5e-3f, 1536.f, 1e-4f);
  /* sound similar to alankila6581r4ar3789 */
  set_type3_properties(1.40e6f, 1.47e8f, 1.0059f, 1.55e4f);
  /* sound similar to trurl8580r5_3691 */
  set_type4_properties(6.55f, 20.f);
  reset();
  set_chip_model(MOS6581FP);
}


// ----------------------------------------------------------------------------
// Enable filter.
// ----------------------------------------------------------------------------
void FilterFP::enable_filter(bool enable)
{
  enabled = enable;
}


// ----------------------------------------------------------------------------
// Set chip model.
// ----------------------------------------------------------------------------
void FilterFP::set_chip_model(chip_model model)
{
    this->model = model;
    set_Q();
    set_w0();
}

/* dist_CT eliminates 1/x at hot spot */
void FilterFP::set_clock_frequency(float clock) {
    clock_frequency = clock;
    calculate_helpers();
}

void FilterFP::set_distortion_properties(float r, float p, float cft)
{
    distortion_rate = r;
    distortion_point = p;
    /* baseresistance is used to determine material resistivity later */
    distortion_cf_threshold = cft;
    calculate_helpers();
}

void FilterFP::set_type4_properties(float k, float b)
{
    type4_k = k;
    type4_b = b;
}

void FilterFP::set_type3_properties(float br, float o, float s, float mfr)
{
    type3_baseresistance = br;
    type3_offset = o;
    type3_steepness = -logf(s); /* s^x to e^(x*ln(s)), 1/e^x == e^-x. */
    type3_minimumfetresistance = mfr;
}

void FilterFP::calculate_helpers()
{
    if (clock_frequency != 0.f)
        distortion_CT = 1.f / (sidcaps_6581 * clock_frequency);
    set_w0();
}

// ----------------------------------------------------------------------------
// SID reset.
// ----------------------------------------------------------------------------
void FilterFP::reset()
{
  fc = 0;
  res = filt = voice3off = hp_bp_lp = 0; 
  vol = 0;
  volf = Vhp = Vbp = Vlp = 0;
  set_w0();
  set_Q();
}

// ----------------------------------------------------------------------------
// Register functions.
// ----------------------------------------------------------------------------
void FilterFP::writeFC_LO(reg8 fc_lo)
{
  fc = (fc & 0x7f8) | (fc_lo & 0x007);
  set_w0();
}

void FilterFP::writeFC_HI(reg8 fc_hi)
{
  fc = ((fc_hi << 3) & 0x7f8) | (fc & 0x007);
  set_w0();
}

void FilterFP::writeRES_FILT(reg8 res_filt)
{
  res = (res_filt >> 4) & 0x0f;
  set_Q();

  filt = res_filt & 0x0f;
}

void FilterFP::writeMODE_VOL(reg8 mode_vol)
{
  voice3off = mode_vol & 0x80;

  hp_bp_lp = (mode_vol >> 4) & 0x07;

  vol = mode_vol & 0x0f;
  volf = (float) vol / 15.f;
}

// Set filter cutoff frequency.
void FilterFP::set_w0()
{
  if (model == MOS6581FP) {
    /* div once by extra kinkiness because I fitted the type3 eq with that variant. */
    float type3_fc_kink = SIDFP::kinked_dac(fc, kinkiness, 11) / kinkiness;
    type3_fc_kink_exp = type3_offset * expf(type3_fc_kink * type3_steepness);
    if (distortion_rate != 0.f) {
        type3_fc_distortion_offset_hp = (distortion_point - type3_fc_kink) * (0.5f) / distortion_rate;
        type3_fc_distortion_offset_bp = type3_fc_distortion_offset_hp;
    }
    else {
        type3_fc_distortion_offset_bp = 9e9f;
        type3_fc_distortion_offset_hp = 9e9f;
    }
  }
  if (model == MOS8580FP) {
    type4_w0_cache = type4_w0();
  }
}

// Set filter resonance.
void FilterFP::set_Q()
{
  float Q = res / 15.f;
  _1_div_Q = 1.f / (0.707f + Q * 1.5f);
}
