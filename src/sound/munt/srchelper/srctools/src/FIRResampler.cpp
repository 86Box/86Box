/* Copyright (C) 2015-2020 Sergey V. Mikayev
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

#include <cmath>
#include <cstring>

#include "../include/FIRResampler.h"

using namespace SRCTools;

FIRResampler::Constants::Constants(const unsigned int upsampleFactor, const double downsampleFactor, const FIRCoefficient kernel[], const unsigned int kernelLength) {
	usePhaseInterpolation = downsampleFactor != floor(downsampleFactor);
	FIRCoefficient *kernelCopy = new FIRCoefficient[kernelLength];
	memcpy(kernelCopy, kernel, kernelLength * sizeof(FIRCoefficient));
	taps = kernelCopy;
	numberOfTaps = kernelLength;
	numberOfPhases = upsampleFactor;
	phaseIncrement = downsampleFactor;
	unsigned int minDelayLineLength = static_cast<unsigned int>(ceil(double(kernelLength) / upsampleFactor));
	unsigned int delayLineLength = 2;
	while (delayLineLength < minDelayLineLength) delayLineLength <<= 1;
	delayLineMask = delayLineLength - 1;
	ringBuffer = new FloatSample[delayLineLength][FIR_INTERPOLATOR_CHANNEL_COUNT];
	FloatSample *s = *ringBuffer;
	FloatSample *e = ringBuffer[delayLineLength];
	while (s < e) *(s++) = 0;
}

FIRResampler::FIRResampler(const unsigned int upsampleFactor, const double downsampleFactor, const FIRCoefficient kernel[], const unsigned int kernelLength) :
	constants(upsampleFactor, downsampleFactor, kernel, kernelLength),
	ringBufferPosition(0),
	phase(constants.numberOfPhases)
{}

FIRResampler::~FIRResampler() {
	delete[] constants.ringBuffer;
	delete[] constants.taps;
}

void FIRResampler::process(const FloatSample *&inSamples, unsigned int &inLength, FloatSample *&outSamples, unsigned int &outLength) {
	while (outLength > 0) {
		while (needNextInSample()) {
			if (inLength == 0) return;
			addInSamples(inSamples);
			--inLength;
		}
		getOutSamplesStereo(outSamples);
		--outLength;
	}
}

unsigned int FIRResampler::estimateInLength(const unsigned int outLength) const {
	return static_cast<unsigned int>((outLength * constants.phaseIncrement + phase) / constants.numberOfPhases);
}

bool FIRResampler::needNextInSample() const {
	return constants.numberOfPhases <= phase;
}

void FIRResampler::addInSamples(const FloatSample *&inSamples) {
	ringBufferPosition = (ringBufferPosition - 1) & constants.delayLineMask;
	for (unsigned int i = 0; i < FIR_INTERPOLATOR_CHANNEL_COUNT; i++) {
		constants.ringBuffer[ringBufferPosition][i] = *(inSamples++);
	}
	phase -= constants.numberOfPhases;
}

// Optimised for processing stereo interleaved streams
void FIRResampler::getOutSamplesStereo(FloatSample *&outSamples) {
	FloatSample leftSample = 0.0;
	FloatSample rightSample = 0.0;
	unsigned int delaySampleIx = ringBufferPosition;
	if (constants.usePhaseInterpolation) {
		double phaseFraction = phase - floor(phase);
		unsigned int maxTapIx = phaseFraction == 0 ? constants.numberOfTaps : constants.numberOfTaps - 1;
		for (unsigned int tapIx = static_cast<unsigned int>(phase); tapIx < maxTapIx; tapIx += constants.numberOfPhases) {
			FIRCoefficient tap = FIRCoefficient(constants.taps[tapIx] + (constants.taps[tapIx + 1] - constants.taps[tapIx]) * phaseFraction);
			leftSample += tap * constants.ringBuffer[delaySampleIx][0];
			rightSample += tap * constants.ringBuffer[delaySampleIx][1];
			delaySampleIx = (delaySampleIx + 1) & constants.delayLineMask;
		}
	} else {
		// Optimised for rational resampling ratios when phase is always integer
		for (unsigned int tapIx = static_cast<unsigned int>(phase); tapIx < constants.numberOfTaps; tapIx += constants.numberOfPhases) {
			FIRCoefficient tap = constants.taps[tapIx];
			leftSample += tap * constants.ringBuffer[delaySampleIx][0];
			rightSample += tap * constants.ringBuffer[delaySampleIx][1];
			delaySampleIx = (delaySampleIx + 1) & constants.delayLineMask;
		}
	}
	*(outSamples++) = leftSample;
	*(outSamples++) = rightSample;
	phase += constants.phaseIncrement;
}
