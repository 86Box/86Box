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

#include <stdint.h>
#include <stdio.h>
#include "../../plat.h"
#include "SampleRateConverter.h"

#include "Synth.h"

using namespace MT32Emu;

static inline void *createDelegate(UNUSED(Synth &synth), UNUSED(double targetSampleRate), UNUSED(SamplerateConversionQuality quality)) {
        return 0;
}

AnalogOutputMode SampleRateConverter::getBestAnalogOutputMode(UNUSED(double targetSampleRate)) {
	return AnalogOutputMode_COARSE;
}

SampleRateConverter::SampleRateConverter(Synth &useSynth, double targetSampleRate, SamplerateConversionQuality useQuality) :
	synthInternalToTargetSampleRateRatio(SAMPLE_RATE / targetSampleRate),
	useSynthDelegate(useSynth.getStereoOutputSampleRate() == targetSampleRate),
	srcDelegate(useSynthDelegate ? &useSynth : createDelegate(useSynth, targetSampleRate, useQuality))
{}

SampleRateConverter::~SampleRateConverter() {
}

void SampleRateConverter::getOutputSamples(float *buffer, unsigned int length) {
	if (useSynthDelegate) {
		static_cast<Synth *>(srcDelegate)->render(buffer, length);
		return;
	}
}

void SampleRateConverter::getOutputSamples(Bit16s *outBuffer, unsigned int length) {
	if (useSynthDelegate) {
		static_cast<Synth *>(srcDelegate)->render(outBuffer, length);
		return;
	}
}

double SampleRateConverter::convertOutputToSynthTimestamp(double outputTimestamp) const {
	return outputTimestamp * synthInternalToTargetSampleRateRatio;
}

double SampleRateConverter::convertSynthToOutputTimestamp(double synthTimestamp) const {
	return synthTimestamp / synthInternalToTargetSampleRateRatio;
}
