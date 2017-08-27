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

#ifndef SRCTOOLS_RESAMPLER_STAGE_H
#define SRCTOOLS_RESAMPLER_STAGE_H

#include "FloatSampleProvider.h"

namespace SRCTools {

/** Interface defines an abstract source of samples. It can either define a single channel stream or a stream with interleaved channels. */
class ResamplerStage {
public:
	virtual ~ResamplerStage() {}

	/** Returns a lower estimation of required number of input samples to produce the specified number of output samples. */
	virtual unsigned int estimateInLength(const unsigned int outLength) const = 0;

	/** Generates output samples. The arguments are adjusted in accordance with the number of samples processed. */
	virtual void process(const FloatSample *&inSamples, unsigned int &inLength, FloatSample *&outSamples, unsigned int &outLength) = 0;
};

} // namespace SRCTools

#endif // SRCTOOLS_RESAMPLER_STAGE_H
