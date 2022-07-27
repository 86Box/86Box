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

#ifndef MT32EMU_PARTIAL_H
#define MT32EMU_PARTIAL_H

#include "globals.h"
#include "internals.h"
#include "Types.h"
#include "Structures.h"
#include "LA32Ramp.h"
#include "LA32WaveGenerator.h"
#include "LA32FloatWaveGenerator.h"

namespace MT32Emu {

class Part;
class Poly;
class Synth;
class TVA;
class TVF;
class TVP;
struct ControlROMPCMStruct;

// A partial represents one of up to four waveform generators currently playing within a poly.
class Partial {
private:
	Synth *synth;
	const int partialIndex; // Index of this Partial in the global partial table
	// Number of the sample currently being rendered by produceOutput(), or 0 if no run is in progress
	// This is only kept available for debugging purposes.
	Bit32u sampleNum;

	// Actually, LA-32 receives only 3 bits as a pan setting, but we abuse these to emulate
	// the inverted partial mixing as well. Also we double the values (making them correspond
	// to the panpot range) to enable NicePanning mode, with respect to MoK.
	Bit32s leftPanValue, rightPanValue;

	int ownerPart; // -1 if unassigned
	int mixType;
	int structurePosition; // 0 or 1 of a structure pair

	// Only used for PCM partials
	int pcmNum;
	// FIXME: Give this a better name (e.g. pcmWaveInfo)
	PCMWaveEntry *pcmWave;

	// Final pulse width value, with velfollow applied, matching what is sent to the LA32.
	// Range: 0-255
	int pulseWidthVal;

	Poly *poly;
	Partial *pair;

	TVA *tva;
	TVP *tvp;
	TVF *tvf;

	LA32Ramp ampRamp;
	LA32Ramp cutoffModifierRamp;

	// TODO: This should be owned by PartialPair
	LA32PartialPair *la32Pair;
	const bool floatMode;

	const PatchCache *patchCache;
	PatchCache cachebackup;

	Bit32u getAmpValue();
	Bit32u getCutoffValue();

	template <class Sample, class LA32PairImpl>
	bool doProduceOutput(Sample *leftBuf, Sample *rightBuf, Bit32u length, LA32PairImpl *la32PairImpl);
	bool canProduceOutput();
	template <class LA32PairImpl>
	bool generateNextSample(LA32PairImpl *la32PairImpl);
	void produceAndMixSample(IntSample *&leftBuf, IntSample *&rightBuf, LA32IntPartialPair *la32IntPair);
	void produceAndMixSample(FloatSample *&leftBuf, FloatSample *&rightBuf, LA32FloatPartialPair *la32FloatPair);

public:
	bool alreadyOutputed;

	Partial(Synth *synth, int debugPartialNum);
	~Partial();

	int debugGetPartialNum() const;
	Bit32u debugGetSampleNum() const;

	int getOwnerPart() const;
	const Poly *getPoly() const;
	bool isActive() const;
	void activate(int part);
	void deactivate(void);
	void startPartial(const Part *part, Poly *usePoly, const PatchCache *useCache, const MemParams::RhythmTemp *rhythmTemp, Partial *pairPartial);
	void startAbort();
	void startDecayAll();
	bool shouldReverb();
	bool isRingModulatingNoMix() const;
	bool hasRingModulatingSlave() const;
	bool isRingModulatingSlave() const;
	bool isPCM() const;
	const ControlROMPCMStruct *getControlROMPCMStruct() const;
	Synth *getSynth() const;
	TVA *getTVA() const;

	void backupCache(const PatchCache &cache);

	// Returns true only if data written to buffer
	// These functions produce processed stereo samples
	// made from combining this single partial with its pair, if it has one.
	bool produceOutput(IntSample *leftBuf, IntSample *rightBuf, Bit32u length);
	bool produceOutput(FloatSample *leftBuf, FloatSample *rightBuf, Bit32u length);
}; // class Partial

} // namespace MT32Emu

#endif // #ifndef MT32EMU_PARTIAL_H
