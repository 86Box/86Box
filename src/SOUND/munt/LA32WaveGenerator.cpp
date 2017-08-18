/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2017 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
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

#include <cstddef>

#include "internals.h"

#include "LA32WaveGenerator.h"
#include "Tables.h"

namespace MT32Emu {

static const Bit32u SINE_SEGMENT_RELATIVE_LENGTH = 1 << 18;
static const Bit32u MIDDLE_CUTOFF_VALUE = 128 << 18;
static const Bit32u RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE = 144 << 18;
static const Bit32u MAX_CUTOFF_VALUE = 240 << 18;
static const LogSample SILENCE = {65535, LogSample::POSITIVE};

Bit16u LA32Utilites::interpolateExp(const Bit16u fract) {
	Bit16u expTabIndex = fract >> 3;
	Bit16u extraBits = ~fract & 7;
	Bit16u expTabEntry2 = 8191 - Tables::getInstance().exp9[expTabIndex];
	Bit16u expTabEntry1 = expTabIndex == 0 ? 8191 : (8191 - Tables::getInstance().exp9[expTabIndex - 1]);
	return expTabEntry2 + (((expTabEntry1 - expTabEntry2) * extraBits) >> 3);
}

Bit16s LA32Utilites::unlog(const LogSample &logSample) {
	//Bit16s sample = (Bit16s)EXP2F(13.0f - logSample.logValue / 1024.0f);
	Bit32u intLogValue = logSample.logValue >> 12;
	Bit16u fracLogValue = logSample.logValue & 4095;
	Bit16s sample = interpolateExp(fracLogValue) >> intLogValue;
	return logSample.sign == LogSample::POSITIVE ? sample : -sample;
}

void LA32Utilites::addLogSamples(LogSample &logSample1, const LogSample &logSample2) {
	Bit32u logSampleValue = logSample1.logValue + logSample2.logValue;
	logSample1.logValue = logSampleValue < 65536 ? Bit16u(logSampleValue) : 65535;
	logSample1.sign = logSample1.sign == logSample2.sign ? LogSample::POSITIVE : LogSample::NEGATIVE;
}

Bit32u LA32WaveGenerator::getSampleStep() {
	// sampleStep = EXP2F(pitch / 4096.0f + 4.0f)
	Bit32u sampleStep = LA32Utilites::interpolateExp(~pitch & 4095);
	sampleStep <<= pitch >> 12;
	sampleStep >>= 8;
	sampleStep &= ~1;
	return sampleStep;
}

Bit32u LA32WaveGenerator::getResonanceWaveLengthFactor(Bit32u effectiveCutoffValue) {
	// resonanceWaveLengthFactor = (Bit32u)EXP2F(12.0f + effectiveCutoffValue / 4096.0f);
	Bit32u resonanceWaveLengthFactor = LA32Utilites::interpolateExp(~effectiveCutoffValue & 4095);
	resonanceWaveLengthFactor <<= effectiveCutoffValue >> 12;
	return resonanceWaveLengthFactor;
}

Bit32u LA32WaveGenerator::getHighLinearLength(Bit32u effectiveCutoffValue) {
	// Ratio of positive segment to wave length
	Bit32u effectivePulseWidthValue = 0;
	if (pulseWidth > 128) {
		effectivePulseWidthValue = (pulseWidth - 128) << 6;
	}

	Bit32u highLinearLength = 0;
	// highLinearLength = EXP2F(19.0f - effectivePulseWidthValue / 4096.0f + effectiveCutoffValue / 4096.0f) - 2 * SINE_SEGMENT_RELATIVE_LENGTH;
	if (effectivePulseWidthValue < effectiveCutoffValue) {
		Bit32u expArg = effectiveCutoffValue - effectivePulseWidthValue;
		highLinearLength = LA32Utilites::interpolateExp(~expArg & 4095);
		highLinearLength <<= 7 + (expArg >> 12);
		highLinearLength -= 2 * SINE_SEGMENT_RELATIVE_LENGTH;
	}
	return highLinearLength;
}

void LA32WaveGenerator::computePositions(Bit32u highLinearLength, Bit32u lowLinearLength, Bit32u resonanceWaveLengthFactor) {
	// Assuming 12-bit multiplication used here
	squareWavePosition = resonanceSinePosition = (wavePosition >> 8) * (resonanceWaveLengthFactor >> 4);
	if (squareWavePosition < SINE_SEGMENT_RELATIVE_LENGTH) {
		phase = POSITIVE_RISING_SINE_SEGMENT;
		return;
	}
	squareWavePosition -= SINE_SEGMENT_RELATIVE_LENGTH;
	if (squareWavePosition < highLinearLength) {
		phase = POSITIVE_LINEAR_SEGMENT;
		return;
	}
	squareWavePosition -= highLinearLength;
	if (squareWavePosition < SINE_SEGMENT_RELATIVE_LENGTH) {
		phase = POSITIVE_FALLING_SINE_SEGMENT;
		return;
	}
	squareWavePosition -= SINE_SEGMENT_RELATIVE_LENGTH;
	resonanceSinePosition = squareWavePosition;
	if (squareWavePosition < SINE_SEGMENT_RELATIVE_LENGTH) {
		phase = NEGATIVE_FALLING_SINE_SEGMENT;
		return;
	}
	squareWavePosition -= SINE_SEGMENT_RELATIVE_LENGTH;
	if (squareWavePosition < lowLinearLength) {
		phase = NEGATIVE_LINEAR_SEGMENT;
		return;
	}
	squareWavePosition -= lowLinearLength;
	phase = NEGATIVE_RISING_SINE_SEGMENT;
}

void LA32WaveGenerator::advancePosition() {
	wavePosition += getSampleStep();
	wavePosition %= 4 * SINE_SEGMENT_RELATIVE_LENGTH;

	Bit32u effectiveCutoffValue = (cutoffVal > MIDDLE_CUTOFF_VALUE) ? (cutoffVal - MIDDLE_CUTOFF_VALUE) >> 10 : 0;
	Bit32u resonanceWaveLengthFactor = getResonanceWaveLengthFactor(effectiveCutoffValue);
	Bit32u highLinearLength = getHighLinearLength(effectiveCutoffValue);
	Bit32u lowLinearLength = (resonanceWaveLengthFactor << 8) - 4 * SINE_SEGMENT_RELATIVE_LENGTH - highLinearLength;
	computePositions(highLinearLength, lowLinearLength, resonanceWaveLengthFactor);

	resonancePhase = ResonancePhase(((resonanceSinePosition >> 18) + (phase > POSITIVE_FALLING_SINE_SEGMENT ? 2 : 0)) & 3);
}

void LA32WaveGenerator::generateNextSquareWaveLogSample() {
	Bit32u logSampleValue;
	switch (phase) {
		case POSITIVE_RISING_SINE_SEGMENT:
		case NEGATIVE_FALLING_SINE_SEGMENT:
			logSampleValue = Tables::getInstance().logsin9[(squareWavePosition >> 9) & 511];
			break;
		case POSITIVE_FALLING_SINE_SEGMENT:
		case NEGATIVE_RISING_SINE_SEGMENT:
			logSampleValue = Tables::getInstance().logsin9[~(squareWavePosition >> 9) & 511];
			break;
		case POSITIVE_LINEAR_SEGMENT:
		case NEGATIVE_LINEAR_SEGMENT:
		default:
			logSampleValue = 0;
			break;
	}
	logSampleValue <<= 2;
	logSampleValue += amp >> 10;
	if (cutoffVal < MIDDLE_CUTOFF_VALUE) {
		logSampleValue += (MIDDLE_CUTOFF_VALUE - cutoffVal) >> 9;
	}

	squareLogSample.logValue = logSampleValue < 65536 ? Bit16u(logSampleValue) : 65535;
	squareLogSample.sign = phase < NEGATIVE_FALLING_SINE_SEGMENT ? LogSample::POSITIVE : LogSample::NEGATIVE;
}

void LA32WaveGenerator::generateNextResonanceWaveLogSample() {
	Bit32u logSampleValue;
	if (resonancePhase == POSITIVE_FALLING_RESONANCE_SINE_SEGMENT || resonancePhase == NEGATIVE_RISING_RESONANCE_SINE_SEGMENT) {
		logSampleValue = Tables::getInstance().logsin9[~(resonanceSinePosition >> 9) & 511];
	} else {
		logSampleValue = Tables::getInstance().logsin9[(resonanceSinePosition >> 9) & 511];
	}
	logSampleValue <<= 2;
	logSampleValue += amp >> 10;

	// From the digital captures, the decaying speed of the resonance sine is found a bit different for the positive and the negative segments
	Bit32u decayFactor = phase < NEGATIVE_FALLING_SINE_SEGMENT ? resAmpDecayFactor : resAmpDecayFactor + 1;
	// Unsure about resonanceSinePosition here. It's possible that dedicated counter & decrement are used. Although, cutoff is finely ramped, so maybe not.
	logSampleValue += resonanceAmpSubtraction + (((resonanceSinePosition >> 4) * decayFactor) >> 8);

	// To ensure the output wave has no breaks, two different windows are appied to the beginning and the ending of the resonance sine segment
	if (phase == POSITIVE_RISING_SINE_SEGMENT || phase == NEGATIVE_FALLING_SINE_SEGMENT) {
		// The window is synchronous sine here
		logSampleValue += Tables::getInstance().logsin9[(squareWavePosition >> 9) & 511] << 2;
	} else if (phase == POSITIVE_FALLING_SINE_SEGMENT || phase == NEGATIVE_RISING_SINE_SEGMENT) {
		// The window is synchronous square sine here
		logSampleValue += Tables::getInstance().logsin9[~(squareWavePosition >> 9) & 511] << 3;
	}

	if (cutoffVal < MIDDLE_CUTOFF_VALUE) {
		// For the cutoff values below the cutoff middle point, it seems the amp of the resonance wave is expotentially decayed
		logSampleValue += 31743 + ((MIDDLE_CUTOFF_VALUE - cutoffVal) >> 9);
	} else if (cutoffVal < RESONANCE_DECAY_THRESHOLD_CUTOFF_VALUE) {
		// For the cutoff values below this point, the amp of the resonance wave is sinusoidally decayed
		Bit32u sineIx = (cutoffVal - MIDDLE_CUTOFF_VALUE) >> 13;
		logSampleValue += Tables::getInstance().logsin9[sineIx] << 2;
	}

	// After all the amp decrements are added, it should be safe now to adjust the amp of the resonance wave to what we see on captures
	logSampleValue -= 1 << 12;

	resonanceLogSample.logValue = logSampleValue < 65536 ? Bit16u(logSampleValue) : 65535;
	resonanceLogSample.sign = resonancePhase < NEGATIVE_FALLING_RESONANCE_SINE_SEGMENT ? LogSample::POSITIVE : LogSample::NEGATIVE;
}

void LA32WaveGenerator::generateNextSawtoothCosineLogSample(LogSample &logSample) const {
	Bit32u sawtoothCosinePosition = wavePosition + (1 << 18);
	if ((sawtoothCosinePosition & (1 << 18)) > 0) {
		logSample.logValue = Tables::getInstance().logsin9[~(sawtoothCosinePosition >> 9) & 511];
	} else {
		logSample.logValue = Tables::getInstance().logsin9[(sawtoothCosinePosition >> 9) & 511];
	}
	logSample.logValue <<= 2;
	logSample.sign = ((sawtoothCosinePosition & (1 << 19)) == 0) ? LogSample::POSITIVE : LogSample::NEGATIVE;
}

void LA32WaveGenerator::pcmSampleToLogSample(LogSample &logSample, const Bit16s pcmSample) const {
	Bit32u logSampleValue = (32787 - (pcmSample & 32767)) << 1;
	logSampleValue += amp >> 10;
	logSample.logValue = logSampleValue < 65536 ? Bit16u(logSampleValue) : 65535;
	logSample.sign = pcmSample < 0 ? LogSample::NEGATIVE : LogSample::POSITIVE;
}

void LA32WaveGenerator::generateNextPCMWaveLogSamples() {
	// This should emulate the ladder we see in the PCM captures for pitches 01, 02, 07, etc.
	// The most probable cause is the factor in the interpolation formula is one bit less
	// accurate than the sample position counter
	pcmInterpolationFactor = (wavePosition & 255) >> 1;
	Bit32u pcmWaveTableIx = wavePosition >> 8;
	pcmSampleToLogSample(firstPCMLogSample, pcmWaveAddress[pcmWaveTableIx]);
	if (pcmWaveInterpolated) {
		pcmWaveTableIx++;
		if (pcmWaveTableIx < pcmWaveLength) {
			pcmSampleToLogSample(secondPCMLogSample, pcmWaveAddress[pcmWaveTableIx]);
		} else {
			if (pcmWaveLooped) {
				pcmWaveTableIx -= pcmWaveLength;
				pcmSampleToLogSample(secondPCMLogSample, pcmWaveAddress[pcmWaveTableIx]);
			} else {
				secondPCMLogSample = SILENCE;
			}
		}
	} else {
		secondPCMLogSample = SILENCE;
	}
	// pcmSampleStep = (Bit32u)EXP2F(pitch / 4096.0f + 3.0f);
	Bit32u pcmSampleStep = LA32Utilites::interpolateExp(~pitch & 4095);
	pcmSampleStep <<= pitch >> 12;
	// Seeing the actual lengths of the PCM wave for pitches 00..12,
	// the pcmPosition counter can be assumed to have 8-bit fractions
	pcmSampleStep >>= 9;
	wavePosition += pcmSampleStep;
	if (wavePosition >= (pcmWaveLength << 8)) {
		if (pcmWaveLooped) {
			wavePosition -= pcmWaveLength << 8;
		} else {
			deactivate();
		}
	}
}

void LA32WaveGenerator::initSynth(const bool useSawtoothWaveform, const Bit8u usePulseWidth, const Bit8u useResonance) {
	sawtoothWaveform = useSawtoothWaveform;
	pulseWidth = usePulseWidth;
	resonance = useResonance;

	wavePosition = 0;

	squareWavePosition = 0;
	phase = POSITIVE_RISING_SINE_SEGMENT;

	resonanceSinePosition = 0;
	resonancePhase = POSITIVE_RISING_RESONANCE_SINE_SEGMENT;
	resonanceAmpSubtraction = (32 - resonance) << 10;
	resAmpDecayFactor = Tables::getInstance().resAmpDecayFactor[resonance >> 2] << 2;

	pcmWaveAddress = NULL;
	active = true;
}

void LA32WaveGenerator::initPCM(const Bit16s * const usePCMWaveAddress, const Bit32u usePCMWaveLength, const bool usePCMWaveLooped, const bool usePCMWaveInterpolated) {
	pcmWaveAddress = usePCMWaveAddress;
	pcmWaveLength = usePCMWaveLength;
	pcmWaveLooped = usePCMWaveLooped;
	pcmWaveInterpolated = usePCMWaveInterpolated;

	wavePosition = 0;
	active = true;
}

void LA32WaveGenerator::generateNextSample(const Bit32u useAmp, const Bit16u usePitch, const Bit32u useCutoffVal) {
	if (!active) {
		return;
	}

	amp = useAmp;
	pitch = usePitch;

	if (isPCMWave()) {
		generateNextPCMWaveLogSamples();
		return;
	}

	// The 240 cutoffVal limit was determined via sample analysis (internal Munt capture IDs: glop3, glop4).
	// More research is needed to be sure that this is correct, however.
	cutoffVal = (useCutoffVal > MAX_CUTOFF_VALUE) ? MAX_CUTOFF_VALUE : useCutoffVal;

	generateNextSquareWaveLogSample();
	generateNextResonanceWaveLogSample();
	if (sawtoothWaveform) {
		LogSample cosineLogSample;
		generateNextSawtoothCosineLogSample(cosineLogSample);
		LA32Utilites::addLogSamples(squareLogSample, cosineLogSample);
		LA32Utilites::addLogSamples(resonanceLogSample, cosineLogSample);
	}
	advancePosition();
}

LogSample LA32WaveGenerator::getOutputLogSample(const bool first) const {
	if (!isActive()) {
		return SILENCE;
	}
	if (isPCMWave()) {
		return first ? firstPCMLogSample : secondPCMLogSample;
	}
	return first ? squareLogSample : resonanceLogSample;
}

void LA32WaveGenerator::deactivate() {
	active = false;
}

bool LA32WaveGenerator::isActive() const {
	return active;
}

bool LA32WaveGenerator::isPCMWave() const {
	return pcmWaveAddress != NULL;
}

Bit32u LA32WaveGenerator::getPCMInterpolationFactor() const {
	return pcmInterpolationFactor;
}

void LA32IntPartialPair::init(const bool useRingModulated, const bool useMixed) {
	ringModulated = useRingModulated;
	mixed = useMixed;
}

void LA32IntPartialPair::initSynth(const PairType useMaster, const bool sawtoothWaveform, const Bit8u pulseWidth, const Bit8u resonance) {
	if (useMaster == MASTER) {
		master.initSynth(sawtoothWaveform, pulseWidth, resonance);
	} else {
		slave.initSynth(sawtoothWaveform, pulseWidth, resonance);
	}
}

void LA32IntPartialPair::initPCM(const PairType useMaster, const Bit16s *pcmWaveAddress, const Bit32u pcmWaveLength, const bool pcmWaveLooped) {
	if (useMaster == MASTER) {
		master.initPCM(pcmWaveAddress, pcmWaveLength, pcmWaveLooped, true);
	} else {
		slave.initPCM(pcmWaveAddress, pcmWaveLength, pcmWaveLooped, !ringModulated);
	}
}

void LA32IntPartialPair::generateNextSample(const PairType useMaster, const Bit32u amp, const Bit16u pitch, const Bit32u cutoff) {
	if (useMaster == MASTER) {
		master.generateNextSample(amp, pitch, cutoff);
	} else {
		slave.generateNextSample(amp, pitch, cutoff);
	}
}

Bit16s LA32IntPartialPair::unlogAndMixWGOutput(const LA32WaveGenerator &wg) {
	if (!wg.isActive()) {
		return 0;
	}
	Bit16s firstSample = LA32Utilites::unlog(wg.getOutputLogSample(true));
	Bit16s secondSample = LA32Utilites::unlog(wg.getOutputLogSample(false));
	if (wg.isPCMWave()) {
		return Bit16s(firstSample + (((Bit32s(secondSample) - Bit32s(firstSample)) * wg.getPCMInterpolationFactor()) >> 7));
	}
	return firstSample + secondSample;
}

static inline Bit16s produceDistortedSample(Bit16s sample) {
	return ((sample & 0x2000) == 0) ? Bit16s(sample & 0x1fff) : Bit16s(sample | ~0x1fff);
}

Bit16s LA32IntPartialPair::nextOutSample() {
	if (!ringModulated) {
		return unlogAndMixWGOutput(master) + unlogAndMixWGOutput(slave);
	}

	Bit16s masterSample = unlogAndMixWGOutput(master); // Store master partial sample for further mixing

	/* SEMI-CONFIRMED from sample analysis:
	 * We observe that for partial structures with ring modulation the interpolation is not applied to the slave PCM partial.
	 * It's assumed that the multiplication circuitry intended to perform the interpolation on the slave PCM partial
	 * is borrowed by the ring modulation circuit (or the LA32 chip has a similar lack of resources assigned to each partial pair).
	 */
	Bit16s slaveSample = slave.isPCMWave() ? LA32Utilites::unlog(slave.getOutputLogSample(true)) : unlogAndMixWGOutput(slave);

	/* SEMI-CONFIRMED: Ring modulation model derived from sample analysis of specially constructed patches which exploit distortion.
	 * LA32 ring modulator found to produce distorted output in case if the absolute value of maximal amplitude of one of the input partials exceeds 8191.
	 * This is easy to reproduce using synth partials with resonance values close to the maximum. It looks like an integer overflow happens in this case.
	 * As the distortion is strictly bound to the amplitude of the complete mixed square + resonance wave in the linear space,
	 * it is reasonable to assume the ring modulation is performed also in the linear space by sample multiplication.
	 * Most probably the overflow is caused by limited precision of the multiplication circuit as the very similar distortion occurs with panning.
	 */
	Bit16s ringModulatedSample = Bit16s((Bit32s(produceDistortedSample(masterSample)) * Bit32s(produceDistortedSample(slaveSample))) >> 13);

	return mixed ? masterSample + ringModulatedSample : ringModulatedSample;
}

void LA32IntPartialPair::deactivate(const PairType useMaster) {
	if (useMaster == MASTER) {
		master.deactivate();
	} else {
		slave.deactivate();
	}
}

bool LA32IntPartialPair::isActive(const PairType useMaster) const {
	return useMaster == MASTER ? master.isActive() : slave.isActive();
}

} // namespace MT32Emu
