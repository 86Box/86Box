/* Copyright (C) 2015-2022 Sergey V. Mikayev
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

#ifndef SRCTOOLS_FIR_RESAMPLER_H
#define SRCTOOLS_FIR_RESAMPLER_H

#include "ResamplerStage.h"

namespace SRCTools {

typedef FloatSample FIRCoefficient;

static const unsigned int FIR_INTERPOLATOR_CHANNEL_COUNT = 2;

class FIRResampler : public ResamplerStage {
public:
	FIRResampler(const unsigned int upsampleFactor, const double downsampleFactor, const FIRCoefficient kernel[], const unsigned int kernelLength);
	~FIRResampler();

	void process(const FloatSample *&inSamples, unsigned int &inLength, FloatSample *&outSamples, unsigned int &outLength);
	unsigned int estimateInLength(const unsigned int outLength) const;

private:
	const struct Constants {
		// Filter coefficients
		const FIRCoefficient *taps;
		// Indicates whether to interpolate filter taps
		bool usePhaseInterpolation;
		// Size of array of filter coefficients
		unsigned int numberOfTaps;
		// Upsampling factor
		unsigned int numberOfPhases;
		// Downsampling factor
		double phaseIncrement;
		// Index of last delay line element, generally greater than numberOfTaps to form a proper binary mask
		unsigned int delayLineMask;
		// Delay line
		FloatSample(*ringBuffer)[FIR_INTERPOLATOR_CHANNEL_COUNT];

		Constants(const unsigned int upsampleFactor, const double downsampleFactor, const FIRCoefficient kernel[], const unsigned int kernelLength);
	} constants;
	// Index of current sample in delay line
	unsigned int ringBufferPosition;
	// Current phase
	double phase;

	bool needNextInSample() const;
	void addInSamples(const FloatSample *&inSamples);
	void getOutSamplesStereo(FloatSample *&outSamples);
}; // class FIRResampler

} // namespace SRCTools

#endif // SRCTOOLS_FIR_RESAMPLER_H
