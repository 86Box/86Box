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

#ifndef SRCTOOLS_IIR_2X_RESAMPLER_H
#define SRCTOOLS_IIR_2X_RESAMPLER_H

#include "ResamplerStage.h"

namespace SRCTools {

static const unsigned int IIR_RESAMPER_CHANNEL_COUNT = 2;
static const unsigned int IIR_SECTION_ORDER = 2;

typedef FloatSample IIRCoefficient;
typedef FloatSample BufferedSample;

typedef BufferedSample SectionBuffer[IIR_SECTION_ORDER];

// Non-trivial coefficients of a 2nd-order section of a parallel bank
// (zero-order numerator coefficient is always zero, zero-order denominator coefficient is always unity)
struct IIRSection {
	IIRCoefficient num1;
	IIRCoefficient num2;
	IIRCoefficient den1;
	IIRCoefficient den2;
};

class IIRResampler : public ResamplerStage {
public:
	enum Quality {
		// Used when providing custom IIR filter coefficients.
		CUSTOM,
		// Use fast elliptic filter with symmetric ripple: N=8, Ap=As=-99 dB, fp=0.125, fs = 0.25 (in terms of sample rate)
		FAST,
		// Use average elliptic filter with symmetric ripple: N=12, Ap=As=-106 dB, fp=0.193, fs = 0.25 (in terms of sample rate)
		GOOD,
		// Use sharp elliptic filter with symmetric ripple: N=18, Ap=As=-106 dB, fp=0.238, fs = 0.25 (in terms of sample rate)
		BEST
	};

	// Returns the retained fraction of the passband for the given standard quality value
	static double getPassbandFractionForQuality(Quality quality);

protected:
	explicit IIRResampler(const Quality quality);
	explicit IIRResampler(const unsigned int useSectionsCount, const IIRCoefficient useFIR, const IIRSection useSections[]);
	~IIRResampler();

	const struct Constants {
		// Coefficient of the 0-order FIR part
		IIRCoefficient fir;
		// 2nd-order sections that comprise a parallel bank
		const IIRSection *sections;
		// Number of 2nd-order sections
		unsigned int sectionsCount;
		// Delay line per channel per section
		SectionBuffer *buffer;

		Constants(const unsigned int useSectionsCount, const IIRCoefficient useFIR, const IIRSection useSections[], const Quality quality);
	} constants;
}; // class IIRResampler

class IIR2xInterpolator : public IIRResampler {
public:
	explicit IIR2xInterpolator(const Quality quality);
	explicit IIR2xInterpolator(const unsigned int useSectionsCount, const IIRCoefficient useFIR, const IIRSection useSections[]);

	void process(const FloatSample *&inSamples, unsigned int &inLength, FloatSample *&outSamples, unsigned int &outLength);
	unsigned int estimateInLength(const unsigned int outLength) const;

private:
	FloatSample lastInputSamples[IIR_RESAMPER_CHANNEL_COUNT];
	unsigned int phase;
};

class IIR2xDecimator : public IIRResampler {
public:
	explicit IIR2xDecimator(const Quality quality);
	explicit IIR2xDecimator(const unsigned int useSectionsCount, const IIRCoefficient useFIR, const IIRSection useSections[]);

	void process(const FloatSample *&inSamples, unsigned int &inLength, FloatSample *&outSamples, unsigned int &outLength);
	unsigned int estimateInLength(const unsigned int outLength) const;
};

} // namespace SRCTools

#endif // SRCTOOLS_IIR_2X_RESAMPLER_H
