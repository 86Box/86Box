/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2020 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
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

#include "Partial.h"
#include "Part.h"
#include "PartialManager.h"
#include "Poly.h"
#include "Synth.h"
#include "Tables.h"
#include "TVA.h"
#include "TVF.h"
#include "TVP.h"

namespace MT32Emu {

static const Bit8u PAN_NUMERATOR_MASTER[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7};
static const Bit8u PAN_NUMERATOR_SLAVE[]  = {0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7, 7, 7, 7};

// We assume the pan is applied using the same 13-bit multiplier circuit that is also used for ring modulation
// because of the observed sample overflow, so the panSetting values are likely mapped in a similar way via a LUT.
// FIXME: Sample analysis suggests that the use of panSetting is linear, but there are some quirks that still need to be resolved.
static Bit32s getPanFactor(Bit32s panSetting) {
	static const Bit32u PAN_FACTORS_COUNT = 15;
	static Bit32s PAN_FACTORS[PAN_FACTORS_COUNT];
	static bool firstRun = true;

	if (firstRun) {
		firstRun = false;
		for (Bit32u i = 1; i < PAN_FACTORS_COUNT; i++) {
			PAN_FACTORS[i] = Bit32s(0.5 + i * 8192.0 / double(PAN_FACTORS_COUNT - 1));
		}
	}
	return PAN_FACTORS[panSetting];
}

Partial::Partial(Synth *useSynth, int usePartialIndex) :
	synth(useSynth), partialIndex(usePartialIndex), sampleNum(0),
	floatMode(useSynth->getSelectedRendererType() == RendererType_FLOAT) {
	// Initialisation of tva, tvp and tvf uses 'this' pointer
	// and thus should not be in the initializer list to avoid a compiler warning
	tva = new TVA(this, &ampRamp);
	tvp = new TVP(this);
	tvf = new TVF(this, &cutoffModifierRamp);
	ownerPart = -1;
	poly = NULL;
	pair = NULL;
	switch (synth->getSelectedRendererType()) {
	case RendererType_BIT16S:
		la32Pair = new LA32IntPartialPair;
		break;
	case RendererType_FLOAT:
		la32Pair = new LA32FloatPartialPair;
		break;
	default:
		la32Pair = NULL;
	}
}

Partial::~Partial() {
	delete la32Pair;
	delete tva;
	delete tvp;
	delete tvf;
}

// Only used for debugging purposes
int Partial::debugGetPartialNum() const {
	return partialIndex;
}

// Only used for debugging purposes
Bit32u Partial::debugGetSampleNum() const {
	return sampleNum;
}

int Partial::getOwnerPart() const {
	return ownerPart;
}

bool Partial::isActive() const {
	return ownerPart > -1;
}

const Poly *Partial::getPoly() const {
	return poly;
}

void Partial::activate(int part) {
	// This just marks the partial as being assigned to a part
	ownerPart = part;
}

void Partial::deactivate() {
	if (!isActive()) {
		return;
	}
	ownerPart = -1;
	synth->partialManager->partialDeactivated(partialIndex);
	if (poly != NULL) {
		poly->partialDeactivated(this);
	}
#if MT32EMU_MONITOR_PARTIALS > 2
	synth->printDebug("[+%lu] [Partial %d] Deactivated", sampleNum, partialIndex);
	synth->printPartialUsage(sampleNum);
#endif
	if (isRingModulatingSlave()) {
		pair->la32Pair->deactivate(LA32PartialPair::SLAVE);
	} else {
		la32Pair->deactivate(LA32PartialPair::MASTER);
		if (hasRingModulatingSlave()) {
			pair->deactivate();
			pair = NULL;
		}
	}
	if (pair != NULL) {
		pair->pair = NULL;
	}
}

void Partial::startPartial(const Part *part, Poly *usePoly, const PatchCache *usePatchCache, const MemParams::RhythmTemp *rhythmTemp, Partial *pairPartial) {
	if (usePoly == NULL || usePatchCache == NULL) {
		synth->printDebug("[Partial %d] *** Error: Starting partial for owner %d, usePoly=%s, usePatchCache=%s", partialIndex, ownerPart, usePoly == NULL ? "*** NULL ***" : "OK", usePatchCache == NULL ? "*** NULL ***" : "OK");
		return;
	}
	patchCache = usePatchCache;
	poly = usePoly;
	mixType = patchCache->structureMix;
	structurePosition = patchCache->structurePosition;

	Bit8u panSetting = rhythmTemp != NULL ? rhythmTemp->panpot : part->getPatchTemp()->panpot;
	if (mixType == 3) {
		if (structurePosition == 0) {
			panSetting = PAN_NUMERATOR_MASTER[panSetting] << 1;
		} else {
			panSetting = PAN_NUMERATOR_SLAVE[panSetting] << 1;
		}
		// Do a normal mix independent of any pair partial.
		mixType = 0;
		pairPartial = NULL;
	} else if (!synth->isNicePanningEnabled()) {
		// Mok wanted an option for smoother panning, and we love Mok.
		// CONFIRMED by Mok: exactly bytes like this (right shifted) are sent to the LA32.
		panSetting &= 0x0E;
	}

	leftPanValue = synth->reversedStereoEnabled ? 14 - panSetting : panSetting;
	rightPanValue = 14 - leftPanValue;

	if (!floatMode) {
		leftPanValue = getPanFactor(leftPanValue);
		rightPanValue = getPanFactor(rightPanValue);
	}

	// SEMI-CONFIRMED: From sample analysis:
	// Found that timbres with 3 or 4 partials (i.e. one using two partial pairs) are mixed in two different ways.
	// Either partial pairs are added or subtracted, it depends on how the partial pairs are allocated.
	// It seems that partials are grouped into quarters and if the partial pairs are allocated in different quarters the subtraction happens.
	// Though, this matters little for the majority of timbres, it becomes crucial for timbres which contain several partials that sound very close.
	// In this case that timbre can sound totally different depending on the way it is mixed up.
	// Most easily this effect can be displayed with the help of a special timbre consisting of several identical square wave partials (3 or 4).
	// Say, it is 3-partial timbre. Just play any two notes simultaneously and the polys very probably are mixed differently.
	// Moreover, the partial allocator retains the last partial assignment it did and all the subsequent notes will sound the same as the last released one.
	// The situation is better with 4-partial timbres since then a whole quarter is assigned for each poly. However, if a 3-partial timbre broke the normal
	// whole-quarter assignment or after some partials got aborted, even 4-partial timbres can be found sounding differently.
	// This behaviour is also confirmed with two more special timbres: one with identical sawtooth partials, and one with PCM wave 02.
	// For my personal taste, this behaviour rather enriches the sounding and should be emulated.
	if (!synth->isNicePartialMixingEnabled() && (partialIndex & 4)) {
		leftPanValue = -leftPanValue;
		rightPanValue = -rightPanValue;
	}

	if (patchCache->PCMPartial) {
		pcmNum = patchCache->pcm;
		if (synth->controlROMMap->pcmCount > 128) {
			// CM-32L, etc. support two "banks" of PCMs, selectable by waveform type parameter.
			if (patchCache->waveform > 1) {
				pcmNum += 128;
			}
		}
		pcmWave = &synth->pcmWaves[pcmNum];
	} else {
		pcmWave = NULL;
	}

	// CONFIRMED: pulseWidthVal calculation is based on information from Mok
	pulseWidthVal = (poly->getVelocity() - 64) * (patchCache->srcPartial.wg.pulseWidthVeloSensitivity - 7) + Tables::getInstance().pulseWidth100To255[patchCache->srcPartial.wg.pulseWidth];
	if (pulseWidthVal < 0) {
		pulseWidthVal = 0;
	} else if (pulseWidthVal > 255) {
		pulseWidthVal = 255;
	}

	pair = pairPartial;
	alreadyOutputed = false;
	tva->reset(part, patchCache->partialParam, rhythmTemp);
	tvp->reset(part, patchCache->partialParam);
	tvf->reset(patchCache->partialParam, tvp->getBasePitch());

	LA32PartialPair::PairType pairType;
	LA32PartialPair *useLA32Pair;
	if (isRingModulatingSlave()) {
		pairType = LA32PartialPair::SLAVE;
		useLA32Pair = pair->la32Pair;
	} else {
		pairType = LA32PartialPair::MASTER;
		la32Pair->init(hasRingModulatingSlave(), mixType == 1);
		useLA32Pair = la32Pair;
	}
	if (isPCM()) {
		useLA32Pair->initPCM(pairType, &synth->pcmROMData[pcmWave->addr], pcmWave->len, pcmWave->loop);
	} else {
		useLA32Pair->initSynth(pairType, (patchCache->waveform & 1) != 0, pulseWidthVal, patchCache->srcPartial.tvf.resonance + 1);
	}
	if (!hasRingModulatingSlave()) {
		la32Pair->deactivate(LA32PartialPair::SLAVE);
	}
}

Bit32u Partial::getAmpValue() {
	// SEMI-CONFIRMED: From sample analysis:
	// (1) Tested with a single partial playing PCM wave 77 with pitchCoarse 36 and no keyfollow, velocity follow, etc.
	// This gives results within +/- 2 at the output (before any DAC bitshifting)
	// when sustaining at levels 156 - 255 with no modifiers.
	// (2) Tested with a special square wave partial (internal capture ID tva5) at TVA envelope levels 155-255.
	// This gives deltas between -1 and 0 compared to the real output. Note that this special partial only produces
	// positive amps, so negative still needs to be explored, as well as lower levels.
	//
	// Also still partially unconfirmed is the behaviour when ramping between levels, as well as the timing.
	// TODO: The tests above were performed using the float model, to be refined
	Bit32u ampRampVal = 67117056 - ampRamp.nextValue();
	if (ampRamp.checkInterrupt()) {
		tva->handleInterrupt();
	}
	return ampRampVal;
}

Bit32u Partial::getCutoffValue() {
	if (isPCM()) {
		return 0;
	}
	Bit32u cutoffModifierRampVal = cutoffModifierRamp.nextValue();
	if (cutoffModifierRamp.checkInterrupt()) {
		tvf->handleInterrupt();
	}
	return (tvf->getBaseCutoff() << 18) + cutoffModifierRampVal;
}

bool Partial::hasRingModulatingSlave() const {
	return pair != NULL && structurePosition == 0 && (mixType == 1 || mixType == 2);
}

bool Partial::isRingModulatingSlave() const {
	return pair != NULL && structurePosition == 1 && (mixType == 1 || mixType == 2);
}

bool Partial::isRingModulatingNoMix() const {
	return pair != NULL && ((structurePosition == 1 && mixType == 1) || mixType == 2);
}

bool Partial::isPCM() const {
	return pcmWave != NULL;
}

const ControlROMPCMStruct *Partial::getControlROMPCMStruct() const {
	if (pcmWave != NULL) {
		return pcmWave->controlROMPCMStruct;
	}
	return NULL;
}

Synth *Partial::getSynth() const {
	return synth;
}

TVA *Partial::getTVA() const {
	return tva;
}

void Partial::backupCache(const PatchCache &cache) {
	if (patchCache == &cache) {
		cachebackup = cache;
		patchCache = &cachebackup;
	}
}

bool Partial::canProduceOutput() {
	if (!isActive() || alreadyOutputed || isRingModulatingSlave()) {
		return false;
	}
	if (poly == NULL) {
		synth->printDebug("[Partial %d] *** ERROR: poly is NULL at Partial::produceOutput()!", partialIndex);
		return false;
	}
	return true;
}

template <class LA32PairImpl>
bool Partial::generateNextSample(LA32PairImpl *la32PairImpl) {
	if (!tva->isPlaying() || !la32PairImpl->isActive(LA32PartialPair::MASTER)) {
		deactivate();
		return false;
	}
	la32PairImpl->generateNextSample(LA32PartialPair::MASTER, getAmpValue(), tvp->nextPitch(), getCutoffValue());
	if (hasRingModulatingSlave()) {
		la32PairImpl->generateNextSample(LA32PartialPair::SLAVE, pair->getAmpValue(), pair->tvp->nextPitch(), pair->getCutoffValue());
		if (!pair->tva->isPlaying() || !la32PairImpl->isActive(LA32PartialPair::SLAVE)) {
			pair->deactivate();
			if (mixType == 2) {
				deactivate();
				return false;
			}
		}
	}
	return true;
}

void Partial::produceAndMixSample(IntSample *&leftBuf, IntSample *&rightBuf, LA32IntPartialPair *la32IntPair) {
	IntSampleEx sample = la32IntPair->nextOutSample();

	// FIXME: LA32 may produce distorted sound in case if the absolute value of maximal amplitude of the input exceeds 8191
	// when the panning value is non-zero. Most probably the distortion occurs in the same way it does with ring modulation,
	// and it seems to be caused by limited precision of the common multiplication circuit.
	// From analysis of this overflow, it is obvious that the right channel output is actually found
	// by subtraction of the left channel output from the input.
	// Though, it is unknown whether this overflow is exploited somewhere.

	IntSampleEx leftOut = ((sample * leftPanValue) >> 13) + IntSampleEx(*leftBuf);
	IntSampleEx rightOut = ((sample * rightPanValue) >> 13) + IntSampleEx(*rightBuf);
	*(leftBuf++) = Synth::clipSampleEx(leftOut);
	*(rightBuf++) = Synth::clipSampleEx(rightOut);
}

void Partial::produceAndMixSample(FloatSample *&leftBuf, FloatSample *&rightBuf, LA32FloatPartialPair *la32FloatPair) {
	FloatSample sample = la32FloatPair->nextOutSample();
	FloatSample leftOut = (sample * leftPanValue) / 14.0f;
	FloatSample rightOut = (sample * rightPanValue) / 14.0f;
	*(leftBuf++) += leftOut;
	*(rightBuf++) += rightOut;
}

template <class Sample, class LA32PairImpl>
bool Partial::doProduceOutput(Sample *leftBuf, Sample *rightBuf, Bit32u length, LA32PairImpl *la32PairImpl) {
	if (!canProduceOutput()) return false;
	alreadyOutputed = true;

	for (sampleNum = 0; sampleNum < length; sampleNum++) {
		if (!generateNextSample(la32PairImpl)) break;
		produceAndMixSample(leftBuf, rightBuf, la32PairImpl);
	}
	sampleNum = 0;
	return true;
}

bool Partial::produceOutput(IntSample *leftBuf, IntSample *rightBuf, Bit32u length) {
	if (floatMode) {
		synth->printDebug("Partial: Invalid call to produceOutput()! Renderer = %d\n", synth->getSelectedRendererType());
		return false;
	}
	return doProduceOutput(leftBuf, rightBuf, length, static_cast<LA32IntPartialPair *>(la32Pair));
}

bool Partial::produceOutput(FloatSample *leftBuf, FloatSample *rightBuf, Bit32u length) {
	if (!floatMode) {
		synth->printDebug("Partial: Invalid call to produceOutput()! Renderer = %d\n", synth->getSelectedRendererType());
		return false;
	}
	return doProduceOutput(leftBuf, rightBuf, length, static_cast<LA32FloatPartialPair *>(la32Pair));
}

bool Partial::shouldReverb() {
	if (!isActive()) {
		return false;
	}
	return patchCache->reverb;
}

void Partial::startAbort() {
	// This is called when the partial manager needs to terminate partials for re-use by a new Poly.
	tva->startAbort();
}

void Partial::startDecayAll() {
	tva->startDecay();
	tvp->startDecay();
	tvf->startDecay();
}

} // namespace MT32Emu
