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

#ifndef MT32EMU_SAMPLE_RATE_CONVERTER_H
#define MT32EMU_SAMPLE_RATE_CONVERTER_H

#include "globals.h"
#include "Types.h"
#include "Enumerations.h"

namespace MT32Emu {

class Synth;

/* SampleRateConverter class allows to convert the synthesiser output to any desired sample rate.
 * It processes the completely mixed stereo output signal as it passes the analogue circuit emulation,
 * so emulating the synthesiser output signal passing further through an ADC.
 * Several conversion quality options are provided which allow to trade-off the conversion speed vs. the passband width.
 * All the options except FASTEST guarantee full suppression of the aliasing noise in terms of the 16-bit integer samples.
 */
class MT32EMU_EXPORT SampleRateConverter {
public:
	// Returns the value of AnalogOutputMode for which the output signal may retain its full frequency spectrum
	// at the sample rate specified by the targetSampleRate argument.
	static AnalogOutputMode getBestAnalogOutputMode(double targetSampleRate);

	// Returns the sample rate supported by the sample rate conversion implementation currently in effect
	// that is closest to the one specified by the desiredSampleRate argument.
	static double getSupportedOutputSampleRate(double desiredSampleRate);

	// Creates a SampleRateConverter instance that converts output signal from the synth to the given sample rate
	// with the specified conversion quality.
	SampleRateConverter(Synth &synth, double targetSampleRate, SamplerateConversionQuality quality);
	~SampleRateConverter();

	// Fills the provided output buffer with the results of the sample rate conversion.
	// The input samples are automatically retrieved from the synth as necessary.
	void getOutputSamples(MT32Emu::Bit16s *buffer, unsigned int length);

	// Fills the provided output buffer with the results of the sample rate conversion.
	// The input samples are automatically retrieved from the synth as necessary.
	void getOutputSamples(float *buffer, unsigned int length);

	// Returns the number of samples produced at the internal synth sample rate (32000 Hz)
	// that correspond to the number of samples at the target sample rate.
	// Intended to facilitate audio time synchronisation.
	double convertOutputToSynthTimestamp(double outputTimestamp) const;

	// Returns the number of samples produced at the target sample rate
	// that correspond to the number of samples at the internal synth sample rate (32000 Hz).
	// Intended to facilitate audio time synchronisation.
	double convertSynthToOutputTimestamp(double synthTimestamp) const;

private:
	const double synthInternalToTargetSampleRateRatio;
	const bool useSynthDelegate;
	void * const srcDelegate;
}; // class SampleRateConverter

} // namespace MT32Emu

#endif // MT32EMU_SAMPLE_RATE_CONVERTER_H
