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

#ifndef MT32EMU_SOXR_ADAPTER_H
#define MT32EMU_SOXR_ADAPTER_H

#include <soxr.h>

#include "../Enumerations.h"

namespace MT32Emu {

class Synth;

class SoxrAdapter {
public:
	SoxrAdapter(Synth &synth, double targetSampleRate, SamplerateConversionQuality quality);
	~SoxrAdapter();

	void getOutputSamples(float *buffer, unsigned int length);

private:
	Synth &synth;
	float * const inBuffer;
	soxr_t resampler;

	static size_t getInputSamples(void *input_fn_state, soxr_in_t *data, size_t requested_len);
};

} // namespace MT32Emu

#endif // MT32EMU_SOXR_ADAPTER_H
