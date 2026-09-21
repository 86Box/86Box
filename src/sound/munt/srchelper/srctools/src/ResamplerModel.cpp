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

#include <cmath>
#include <cstddef>

#include "../include/ResamplerModel.h"

#include "../include/ResamplerStage.h"
#include "../include/SincResampler.h"
#include "../include/IIR2xResampler.h"
#include "../include/LinearResampler.h"

namespace SRCTools {

namespace ResamplerModel {

static const unsigned int CHANNEL_COUNT = 2;
static const unsigned int MAX_SAMPLES_PER_RUN = 4096;

class CascadeStage : public FloatSampleProvider {
friend void freeResamplerModel(FloatSampleProvider &model, FloatSampleProvider &source);
public:
	CascadeStage(FloatSampleProvider &source, ResamplerStage &resamplerStage);

	void getOutputSamples(FloatSample *outBuffer, unsigned int size);

protected:
	ResamplerStage &resamplerStage;

private:
	FloatSampleProvider &source;
	FloatSample buffer[CHANNEL_COUNT * MAX_SAMPLES_PER_RUN];
	const FloatSample *bufferPtr;
	unsigned int size;
};

class InternalResamplerCascadeStage : public CascadeStage {
public:
	InternalResamplerCascadeStage(FloatSampleProvider &useSource, ResamplerStage &useResamplerStage) :
		CascadeStage(useSource, useResamplerStage)
	{}

	~InternalResamplerCascadeStage() {
		delete &resamplerStage;
	}
};

} // namespace ResamplerModel

} // namespace SRCTools

using namespace SRCTools;

FloatSampleProvider &ResamplerModel::createResamplerModel(FloatSampleProvider &source, double sourceSampleRate, double targetSampleRate, Quality quality) {
	if (sourceSampleRate == targetSampleRate) {
		return source;
	}
	if (quality == FASTEST) {
		return *new InternalResamplerCascadeStage(source, *new LinearResampler(sourceSampleRate, targetSampleRate));
	}
	const IIRResampler::Quality iirQuality = static_cast<IIRResampler::Quality>(quality);
	const double iirPassbandFraction = IIRResampler::getPassbandFractionForQuality(iirQuality);
	if (sourceSampleRate < targetSampleRate) {
		ResamplerStage *iir2xInterpolator = new IIR2xInterpolator(iirQuality);
		FloatSampleProvider &iir2xInterpolatorStage = *new InternalResamplerCascadeStage(source, *iir2xInterpolator);

		if (2.0 * sourceSampleRate == targetSampleRate) {
			return iir2xInterpolatorStage;
		}

		double passband = 0.5 * sourceSampleRate * iirPassbandFraction;
		double stopband = 1.5 * sourceSampleRate;
		ResamplerStage *sincResampler = SincResampler::createSincResampler(2.0 * sourceSampleRate, targetSampleRate, passband, stopband, DEFAULT_DB_SNR, DEFAULT_WINDOWED_SINC_MAX_UPSAMPLE_FACTOR);
		return *new InternalResamplerCascadeStage(iir2xInterpolatorStage, *sincResampler);
	}

	if (sourceSampleRate == 2.0 * targetSampleRate) {
		ResamplerStage *iir2xDecimator = new IIR2xDecimator(iirQuality);
		return *new InternalResamplerCascadeStage(source, *iir2xDecimator);
	}

	double passband = 0.5 * targetSampleRate * iirPassbandFraction;
	double stopband = 1.5 * targetSampleRate;
	double sincOutSampleRate = 2.0 * targetSampleRate;
	const unsigned int maxUpsampleFactor = static_cast<unsigned int>(ceil(DEFAULT_WINDOWED_SINC_MAX_DOWNSAMPLE_FACTOR * sincOutSampleRate / sourceSampleRate));
	ResamplerStage *sincResampler = SincResampler::createSincResampler(sourceSampleRate, sincOutSampleRate, passband, stopband, DEFAULT_DB_SNR, maxUpsampleFactor);
	FloatSampleProvider &sincResamplerStage = *new InternalResamplerCascadeStage(source, *sincResampler);

	ResamplerStage *iir2xDecimator = new IIR2xDecimator(iirQuality);
	return *new InternalResamplerCascadeStage(sincResamplerStage, *iir2xDecimator);
}

FloatSampleProvider &ResamplerModel::createResamplerModel(FloatSampleProvider &source, ResamplerStage **resamplerStages, unsigned int stageCount) {
	FloatSampleProvider *prevStage = &source;
	for (unsigned int i = 0; i < stageCount; i++) {
		prevStage = new CascadeStage(*prevStage, *(resamplerStages[i]));
	}
	return *prevStage;
}

FloatSampleProvider &ResamplerModel::createResamplerModel(FloatSampleProvider &source, ResamplerStage &stage) {
	return *new CascadeStage(source, stage);
}

void ResamplerModel::freeResamplerModel(FloatSampleProvider &model, FloatSampleProvider &source) {
	FloatSampleProvider *currentStage = &model;
	while (currentStage != &source) {
		CascadeStage *cascadeStage = dynamic_cast<CascadeStage *>(currentStage);
		if (cascadeStage == NULL) return;
		FloatSampleProvider &prevStage = cascadeStage->source;
		delete currentStage;
		currentStage = &prevStage;
	}
}

using namespace ResamplerModel;

CascadeStage::CascadeStage(FloatSampleProvider &useSource, ResamplerStage &useResamplerStage) :
	resamplerStage(useResamplerStage),
	source(useSource),
	bufferPtr(buffer),
	size()
{}

void CascadeStage::getOutputSamples(FloatSample *outBuffer, unsigned int length) {
	while (length > 0) {
		if (size == 0) {
			size = resamplerStage.estimateInLength(length);
			if (size < 1) {
				size = 1;
			} else if (MAX_SAMPLES_PER_RUN < size) {
				size = MAX_SAMPLES_PER_RUN;
			}
			source.getOutputSamples(buffer, size);
			bufferPtr = buffer;
		}
		resamplerStage.process(bufferPtr, size, outBuffer, length);
	}
}
