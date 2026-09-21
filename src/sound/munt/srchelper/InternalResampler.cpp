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

#include "InternalResampler.h"

#include "srctools/include/SincResampler.h"
#include "srctools/include/ResamplerModel.h"

#include "../Synth.h"

using namespace SRCTools;

namespace MT32Emu {

class SynthWrapper : public FloatSampleProvider {
	Synth &synth;

public:
	SynthWrapper(Synth &useSynth) : synth(useSynth)
	{}

	void getOutputSamples(FloatSample *outBuffer, unsigned int size) {
		synth.render(outBuffer, size);
	}
};

static FloatSampleProvider &createModel(Synth &synth, SRCTools::FloatSampleProvider &synthSource, double targetSampleRate, SamplerateConversionQuality quality) {
	static const double MAX_AUDIBLE_FREQUENCY = 20000.0;

	const double sourceSampleRate = synth.getStereoOutputSampleRate();
	if (quality != SamplerateConversionQuality_FASTEST) {
		const bool oversampledMode = synth.getStereoOutputSampleRate() == Synth::getStereoOutputSampleRate(AnalogOutputMode_OVERSAMPLED);
		// Oversampled input allows to bypass IIR interpolation stage and, in some cases, IIR decimation stage
		if (oversampledMode && (0.5 * sourceSampleRate) <= targetSampleRate) {
			// NOTE: In the oversampled mode, the transition band starts at 20kHz and ends at 28kHz
			double passband = MAX_AUDIBLE_FREQUENCY;
			double stopband = 0.5 * sourceSampleRate + MAX_AUDIBLE_FREQUENCY;
			ResamplerStage &resamplerStage = *SincResampler::createSincResampler(sourceSampleRate, targetSampleRate, passband, stopband, ResamplerModel::DEFAULT_DB_SNR, ResamplerModel::DEFAULT_WINDOWED_SINC_MAX_UPSAMPLE_FACTOR);
			return ResamplerModel::createResamplerModel(synthSource, resamplerStage);
		}
	}
	return ResamplerModel::createResamplerModel(synthSource, sourceSampleRate, targetSampleRate, static_cast<ResamplerModel::Quality>(quality));
}

} // namespace MT32Emu

using namespace MT32Emu;

InternalResampler::InternalResampler(Synth &synth, double targetSampleRate, SamplerateConversionQuality quality) :
	synthSource(*new SynthWrapper(synth)),
	model(createModel(synth, synthSource, targetSampleRate, quality))
{}

InternalResampler::~InternalResampler() {
	ResamplerModel::freeResamplerModel(model, synthSource);
	delete &synthSource;
}

void InternalResampler::getOutputSamples(float *buffer, unsigned int length) {
	model.getOutputSamples(buffer, length);
}
