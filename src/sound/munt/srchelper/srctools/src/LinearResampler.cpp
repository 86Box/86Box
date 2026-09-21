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

#include "../include/LinearResampler.h"

using namespace SRCTools;

LinearResampler::LinearResampler(double sourceSampleRate, double targetSampleRate) :
	inputToOutputRatio(sourceSampleRate / targetSampleRate),
	position(1.0) // Preload delay line which effectively makes resampler zero phase
{}

void LinearResampler::process(const FloatSample *&inSamples, unsigned int &inLength, FloatSample *&outSamples, unsigned int &outLength) {
	if (inLength == 0) return;
	while (outLength > 0) {
		while (1.0 <= position) {
			position--;
			inLength--;
			for (unsigned int chIx = 0; chIx < LINEAR_RESAMPER_CHANNEL_COUNT; ++chIx) {
				lastInputSamples[chIx] = *(inSamples++);
			}
			if (inLength == 0) return;
		}
		for (unsigned int chIx = 0; chIx < LINEAR_RESAMPER_CHANNEL_COUNT; chIx++) {
			*(outSamples++) = FloatSample(lastInputSamples[chIx] + position * (inSamples[chIx] - lastInputSamples[chIx]));
		}
		outLength--;
		position += inputToOutputRatio;
	}
}

unsigned int LinearResampler::estimateInLength(const unsigned int outLength) const {
	return static_cast<unsigned int>(outLength * inputToOutputRatio);
}
