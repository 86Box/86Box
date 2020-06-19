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

#include "SamplerateAdapter.h"

#include "../Synth.h"

using namespace MT32Emu;

static const unsigned int CHANNEL_COUNT = 2;

long SamplerateAdapter::getInputSamples(void *cb_data, float **data) {
	SamplerateAdapter *instance = static_cast<SamplerateAdapter *>(cb_data);
	unsigned int length = instance->inBufferSize < 1 ? 1 : (MAX_SAMPLES_PER_RUN < instance->inBufferSize ? MAX_SAMPLES_PER_RUN : instance->inBufferSize);
	instance->synth.render(instance->inBuffer, length);
	*data = instance->inBuffer;
	instance->inBufferSize -= length;
	return length;
}

SamplerateAdapter::SamplerateAdapter(Synth &useSynth, double targetSampleRate, SamplerateConversionQuality quality) :
	synth(useSynth),
	inBuffer(new float[CHANNEL_COUNT * MAX_SAMPLES_PER_RUN]),
	inBufferSize(MAX_SAMPLES_PER_RUN),
	inputToOutputRatio(useSynth.getStereoOutputSampleRate() / targetSampleRate),
	outputToInputRatio(targetSampleRate / useSynth.getStereoOutputSampleRate())
{
	int error;
	int conversionType;
	switch (quality) {
	case SamplerateConversionQuality_FASTEST:
		conversionType = SRC_LINEAR;
		break;
	case SamplerateConversionQuality_FAST:
		conversionType = SRC_SINC_FASTEST;
		break;
	case SamplerateConversionQuality_BEST:
		conversionType = SRC_SINC_BEST_QUALITY;
		break;
	case SamplerateConversionQuality_GOOD:
	default:
		conversionType = SRC_SINC_MEDIUM_QUALITY;
		break;
	};
	resampler = src_callback_new(getInputSamples, conversionType, CHANNEL_COUNT, &error, this);
	if (error != 0) {
		synth.printDebug("SamplerateAdapter: Creation of Samplerate instance failed: %s\n", src_strerror(error));
		src_delete(resampler);
		resampler = NULL;
	}
}

SamplerateAdapter::~SamplerateAdapter() {
	delete[] inBuffer;
	src_delete(resampler);
}

void SamplerateAdapter::getOutputSamples(float *buffer, unsigned int length) {
	if (resampler == NULL) {
		Synth::muteSampleBuffer(buffer, CHANNEL_COUNT * length);
		return;
	}
	while (length > 0) {
		inBufferSize = static_cast<unsigned int>(length * inputToOutputRatio + 0.5);
		long gotFrames = src_callback_read(resampler, outputToInputRatio, long(length), buffer);
		int error = src_error(resampler);
		if (error != 0) {
			synth.printDebug("SamplerateAdapter: Samplerate error during processing: %s > resetting\n", src_strerror(error));
			error = src_reset(resampler);
			if (error != 0) {
				synth.printDebug("SamplerateAdapter: Samplerate failed to reset: %s\n", src_strerror(error));
				src_delete(resampler);
				resampler = NULL;
				Synth::muteSampleBuffer(buffer, CHANNEL_COUNT * length);
				synth.printDebug("SamplerateAdapter: Samplerate disabled\n");
				return;
			}
			continue;
		}
		if (gotFrames <= 0) {
			synth.printDebug("SamplerateAdapter: got %li frames from Samplerate, weird\n", gotFrames);
		}
		buffer += CHANNEL_COUNT * gotFrames;
		length -= gotFrames;
	}
}
