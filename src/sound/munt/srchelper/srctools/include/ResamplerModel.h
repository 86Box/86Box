/* Copyright (C) 2015-2017 Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SRCTOOLS_RESAMPLER_MODEL_H
#define SRCTOOLS_RESAMPLER_MODEL_H

#include "FloatSampleProvider.h"

namespace SRCTools {

class ResamplerStage;

/** Model consists of one or more ResampleStage instances connected in a cascade. */
namespace ResamplerModel {

// Seems to be a good choice for 16-bit integer samples.
static const double DEFAULT_DB_SNR = 106;

// When using linear interpolation, oversampling factor necessary to achieve the DEFAULT_DB_SNR is about 256.
// This figure is the upper estimation, and it can be found by analysing the frequency response of the linear interpolator.
// When less SNR is desired, this value should also decrease in accordance.
static const unsigned int DEFAULT_WINDOWED_SINC_MAX_DOWNSAMPLE_FACTOR = 256;

// In the default resampler model, the input to the windowed sinc filter is always at least 2x oversampled during upsampling,
// so oversampling factor of 128 should be sufficient to achieve the DEFAULT_DB_SNR with linear interpolation.
static const unsigned int DEFAULT_WINDOWED_SINC_MAX_UPSAMPLE_FACTOR = DEFAULT_WINDOWED_SINC_MAX_DOWNSAMPLE_FACTOR / 2;


enum Quality {
	// Use when the speed is more important than the audio quality.
	FASTEST,
	// Use FAST quality setting of the IIR stage (50% of passband retained).
	FAST,
	// Use GOOD quality setting of the IIR stage (77% of passband retained).
	GOOD,
	// Use BEST quality setting of the IIR stage (95% of passband retained).
	BEST
};

FloatSampleProvider &createResamplerModel(FloatSampleProvider &source, double sourceSampleRate, double targetSampleRate, Quality quality);
FloatSampleProvider &createResamplerModel(FloatSampleProvider &source, ResamplerStage **stages, unsigned int stageCount);
FloatSampleProvider &createResamplerModel(FloatSampleProvider &source, ResamplerStage &stage);

void freeResamplerModel(FloatSampleProvider &model, FloatSampleProvider &source);

} // namespace ResamplerModel

} // namespace SRCTools

#endif // SRCTOOLS_RESAMPLER_MODEL_H
