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

#ifndef SRCTOOLS_LINEAR_RESAMPLER_H
#define SRCTOOLS_LINEAR_RESAMPLER_H

#include "ResamplerStage.h"

namespace SRCTools {

static const unsigned int LINEAR_RESAMPER_CHANNEL_COUNT = 2;

class LinearResampler : public ResamplerStage {
public:
	LinearResampler(double sourceSampleRate, double targetSampleRate);
	~LinearResampler() {}

	unsigned int estimateInLength(const unsigned int outLength) const;
	void process(const FloatSample *&inSamples, unsigned int &inLength, FloatSample *&outSamples, unsigned int &outLength);

private:
	const double inputToOutputRatio;
	double position;
	FloatSample lastInputSamples[LINEAR_RESAMPER_CHANNEL_COUNT];
};

} // namespace SRCTools

#endif // SRCTOOLS_LINEAR_RESAMPLER_H
