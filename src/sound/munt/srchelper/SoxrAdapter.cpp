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

#include "SoxrAdapter.h"

#include "../Synth.h"

using namespace MT32Emu;

static const unsigned int CHANNEL_COUNT = 2;

size_t SoxrAdapter::getInputSamples(void *input_fn_state, soxr_in_t *data, size_t requested_len) {
	unsigned int length = requested_len < 1 ? 1 : (MAX_SAMPLES_PER_RUN < requested_len ? MAX_SAMPLES_PER_RUN : static_cast<unsigned int>(requested_len));
	SoxrAdapter *instance = static_cast<SoxrAdapter *>(input_fn_state);
	instance->synth.render(instance->inBuffer, length);
	*data = instance->inBuffer;
	return length;
}

SoxrAdapter::SoxrAdapter(Synth &useSynth, double targetSampleRate, SamplerateConversionQuality quality) :
	synth(useSynth),
	inBuffer(new float[CHANNEL_COUNT * MAX_SAMPLES_PER_RUN])
{
	soxr_io_spec_t ioSpec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
	unsigned long qualityRecipe;
	switch (quality) {
	case SamplerateConversionQuality_FASTEST:
		qualityRecipe = SOXR_QQ;
		break;
	case SamplerateConversionQuality_FAST:
		qualityRecipe = SOXR_LQ;
		break;
	case SamplerateConversionQuality_GOOD:
		qualityRecipe = SOXR_MQ;
		break;
	case SamplerateConversionQuality_BEST:
	default:
		qualityRecipe = SOXR_16_BITQ;
		break;
	};
	soxr_quality_spec_t qSpec = soxr_quality_spec(qualityRecipe, 0);
	soxr_runtime_spec_t rtSpec = soxr_runtime_spec(1);
	soxr_error_t error;
	resampler = soxr_create(synth.getStereoOutputSampleRate(), targetSampleRate, CHANNEL_COUNT, &error, &ioSpec, &qSpec, &rtSpec);
	if (error != NULL) {
		synth.printDebug("SoxrAdapter: Creation of SOXR instance failed: %s\n", soxr_strerror(error));
		soxr_delete(resampler);
		resampler = NULL;
		return;
	}
	error = soxr_set_input_fn(resampler, getInputSamples, this, MAX_SAMPLES_PER_RUN);
	if (error != NULL) {
		synth.printDebug("SoxrAdapter: Installing sample feed for SOXR failed: %s\n", soxr_strerror(error));
		soxr_delete(resampler);
		resampler = NULL;
	}
}

SoxrAdapter::~SoxrAdapter() {
	delete[] inBuffer;
	if (resampler != NULL) {
		soxr_delete(resampler);
	}
}

void SoxrAdapter::getOutputSamples(float *buffer, unsigned int length) {
	if (resampler == NULL) {
		Synth::muteSampleBuffer(buffer, CHANNEL_COUNT * length);
		return;
	}
	while (length > 0) {
		size_t gotFrames = soxr_output(resampler, buffer, size_t(length));
		soxr_error_t error = soxr_error(resampler);
		if (error != NULL) {
			synth.printDebug("SoxrAdapter: SOXR error during processing: %s > resetting\n", soxr_strerror(error));
			error = soxr_clear(resampler);
			if (error != NULL) {
				synth.printDebug("SoxrAdapter: SOXR failed to reset: %s\n", soxr_strerror(error));
				soxr_delete(resampler);
				resampler = NULL;
				Synth::muteSampleBuffer(buffer, CHANNEL_COUNT * length);
				synth.printDebug("SoxrAdapter: SOXR disabled\n");
				return;
			}
			continue;
		}
		if (gotFrames == 0) {
			synth.printDebug("SoxrAdapter: got 0 frames from SOXR, weird\n");
		}
		buffer += CHANNEL_COUNT * gotFrames;
		length -= static_cast<unsigned int>(gotFrames);
	}
}
