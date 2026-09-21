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

#ifndef MT32EMU_SAMPLERATE_ADAPTER_H
#define MT32EMU_SAMPLERATE_ADAPTER_H

#include <samplerate.h>

#include "../Enumerations.h"

namespace MT32Emu {

class Synth;

class SamplerateAdapter {
public:
	SamplerateAdapter(Synth &synth, double targetSampleRate, SamplerateConversionQuality quality);
	~SamplerateAdapter();

	void getOutputSamples(float *outBuffer, unsigned int length);

private:
	Synth &synth;
	float * const inBuffer;
	unsigned int inBufferSize;
	const double inputToOutputRatio;
	const double outputToInputRatio;
	SRC_STATE *resampler;

	static long getInputSamples(void *cb_data, float **data);
};

} // namespace MT32Emu

#endif // MT32EMU_SAMPLERATE_ADAPTER_H
