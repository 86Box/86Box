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

#ifndef MT32EMU_INTERNAL_RESAMPLER_H
#define MT32EMU_INTERNAL_RESAMPLER_H

#include "../Enumerations.h"

#include "srctools/include/FloatSampleProvider.h"

namespace MT32Emu {

class Synth;

class InternalResampler {
public:
	InternalResampler(Synth &synth, double targetSampleRate, SamplerateConversionQuality quality);
	~InternalResampler();

	void getOutputSamples(float *buffer, unsigned int length);

private:
	SRCTools::FloatSampleProvider &synthSource;
	SRCTools::FloatSampleProvider &model;
};

} // namespace MT32Emu

#endif // MT32EMU_INTERNAL_RESAMPLER_H
