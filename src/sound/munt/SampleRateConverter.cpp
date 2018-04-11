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

#include "SampleRateConverter.h"

#if MT32EMU_WITH_LIBSOXR_RESAMPLER
#include "srchelper/SoxrAdapter.h"
#elif MT32EMU_WITH_LIBSAMPLERATE_RESAMPLER
#include "srchelper/SamplerateAdapter.h"
#else
#include "srchelper/InternalResampler.h"
#endif

#include "Synth.h"

using namespace MT32Emu;

static inline void *createDelegate(Synth &synth, double targetSampleRate, SamplerateConversionQuality quality) {
#if MT32EMU_WITH_LIBSOXR_RESAMPLER
	return new SoxrAdapter(synth, targetSampleRate, quality);
#elif MT32EMU_WITH_LIBSAMPLERATE_RESAMPLER
	return new SamplerateAdapter(synth, targetSampleRate, quality);
#else
	return new InternalResampler(synth, targetSampleRate, quality);
#endif
}

AnalogOutputMode SampleRateConverter::getBestAnalogOutputMode(double targetSampleRate) {
	if (Synth::getStereoOutputSampleRate(AnalogOutputMode_ACCURATE) < targetSampleRate) {
		return AnalogOutputMode_OVERSAMPLED;
	} else if (Synth::getStereoOutputSampleRate(AnalogOutputMode_COARSE) < targetSampleRate) {
		return AnalogOutputMode_ACCURATE;
	}
	return AnalogOutputMode_COARSE;
}

SampleRateConverter::SampleRateConverter(Synth &useSynth, double targetSampleRate, SamplerateConversionQuality useQuality) :
	synthInternalToTargetSampleRateRatio(SAMPLE_RATE / targetSampleRate),
	useSynthDelegate(useSynth.getStereoOutputSampleRate() == targetSampleRate),
	srcDelegate(useSynthDelegate ? &useSynth : createDelegate(useSynth, targetSampleRate, useQuality))
{}

SampleRateConverter::~SampleRateConverter() {
	if (!useSynthDelegate) {
#if MT32EMU_WITH_LIBSOXR_RESAMPLER
		delete static_cast<SoxrAdapter *>(srcDelegate);
#elif MT32EMU_WITH_LIBSAMPLERATE_RESAMPLER
		delete static_cast<SamplerateAdapter *>(srcDelegate);
#else
		delete static_cast<InternalResampler *>(srcDelegate);
#endif
	}
}

void SampleRateConverter::getOutputSamples(float *buffer, unsigned int length) {
	if (useSynthDelegate) {
		static_cast<Synth *>(srcDelegate)->render(buffer, length);
		return;
	}

#if MT32EMU_WITH_LIBSOXR_RESAMPLER
	static_cast<SoxrAdapter *>(srcDelegate)->getOutputSamples(buffer, length);
#elif MT32EMU_WITH_LIBSAMPLERATE_RESAMPLER
	static_cast<SamplerateAdapter *>(srcDelegate)->getOutputSamples(buffer, length);
#else
	static_cast<InternalResampler *>(srcDelegate)->getOutputSamples(buffer, length);
#endif
}

void SampleRateConverter::getOutputSamples(Bit16s *outBuffer, unsigned int length) {
	static const unsigned int CHANNEL_COUNT = 2;

	if (useSynthDelegate) {
		static_cast<Synth *>(srcDelegate)->render(outBuffer, length);
		return;
	}

	float floatBuffer[CHANNEL_COUNT * MAX_SAMPLES_PER_RUN];
	while (length > 0) {
		const unsigned int size = MAX_SAMPLES_PER_RUN < length ? MAX_SAMPLES_PER_RUN : length;
		getOutputSamples(floatBuffer, size);
		float *outs = floatBuffer;
		float *ends = floatBuffer + CHANNEL_COUNT * size;
		while (outs < ends) {
			*(outBuffer++) = Synth::convertSample(*(outs++));
		}
		length -= size;
	}
}

double SampleRateConverter::convertOutputToSynthTimestamp(double outputTimestamp) const {
	return outputTimestamp * synthInternalToTargetSampleRateRatio;
}

double SampleRateConverter::convertSynthToOutputTimestamp(double synthTimestamp) const {
	return synthTimestamp / synthInternalToTargetSampleRateRatio;
}
