/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2022 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
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

#include <cstdio>

#include "internals.h"

#include "Synth.h"
#include "Analog.h"
#include "BReverbModel.h"
#include "Display.h"
#include "File.h"
#include "MemoryRegion.h"
#include "MidiEventQueue.h"
#include "Part.h"
#include "Partial.h"
#include "PartialManager.h"
#include "Poly.h"
#include "ROMInfo.h"
#include "TVA.h"

#if MT32EMU_MONITOR_SYSEX > 0
#include "mmath.h"
#endif

namespace MT32Emu {

// MIDI interface data transfer rate in samples. Used to simulate the transfer delay.
static const double MIDI_DATA_TRANSFER_RATE = double(SAMPLE_RATE) / 31250.0 * 8.0;

static const ControlROMFeatureSet OLD_MT32_ELDER = {
	true,  // quirkBasePitchOverflow
	true,  // quirkPitchEnvelopeOverflow
	true,  // quirkRingModulationNoMix
	true,  // quirkTVAZeroEnvLevels
	true,  // quirkPanMult
	true,  // quirkKeyShift
	true,  // quirkTVFBaseCutoffLimit
	false, // quirkFastPitchChanges
	true,  // quirkDisplayCustomMessagePriority
	true,  // oldMT32DisplayFeatures
	true,  // defaultReverbMT32Compatible
	true   // oldMT32AnalogLPF
};
static const ControlROMFeatureSet OLD_MT32_LATER = {
	true,  // quirkBasePitchOverflow
	true,  // quirkPitchEnvelopeOverflow
	true,  // quirkRingModulationNoMix
	true,  // quirkTVAZeroEnvLevels
	true,  // quirkPanMult
	true,  // quirkKeyShift
	true,  // quirkTVFBaseCutoffLimit
	false, // quirkFastPitchChanges
	false, // quirkDisplayCustomMessagePriority
	true,  // oldMT32DisplayFeatures
	true,  // defaultReverbMT32Compatible
	true   // oldMT32AnalogLPF
};
static const ControlROMFeatureSet NEW_MT32_COMPATIBLE = {
	false, // quirkBasePitchOverflow
	false, // quirkPitchEnvelopeOverflow
	false, // quirkRingModulationNoMix
	false, // quirkTVAZeroEnvLevels
	false, // quirkPanMult
	false, // quirkKeyShift
	false, // quirkTVFBaseCutoffLimit
	false, // quirkFastPitchChanges
	false, // quirkDisplayCustomMessagePriority
	false, // oldMT32DisplayFeatures
	false, // defaultReverbMT32Compatible
	false  // oldMT32AnalogLPF
};
static const ControlROMFeatureSet CM32LN_COMPATIBLE = {
	false, // quirkBasePitchOverflow
	false, // quirkPitchEnvelopeOverflow
	false, // quirkRingModulationNoMix
	false, // quirkTVAZeroEnvLevels
	false, // quirkPanMult
	false, // quirkKeyShift
	false, // quirkTVFBaseCutoffLimit
	true,  // quirkFastPitchChanges
	false, // quirkDisplayCustomMessagePriority
	false, // oldMT32DisplayFeatures
	false, // defaultReverbMT32Compatible
	false  // oldMT32AnalogLPF
};

static const ControlROMMap ControlROMMaps[] = {
	//     ID                Features        PCMmap  PCMc  tmbrA  tmbrAO, tmbrAC tmbrB   tmbrBO  tmbrBC tmbrR   trC rhythm rhyC  rsrv   panpot   prog   rhyMax  patMax  sysMax  timMax  sndGrp sGC  stMsg   sErMsg
	{"ctrl_mt32_1_04",    OLD_MT32_ELDER,    0x3000, 128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200, 30, 0x73A6, 85, 0x57C7, 0x57E2, 0x57D0, 0x5252, 0x525E, 0x526E, 0x520A, 0x7064, 19, 0x217A, 0x4BB6},
	{"ctrl_mt32_1_05",    OLD_MT32_ELDER,    0x3000, 128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200, 30, 0x7414, 85, 0x57C7, 0x57E2, 0x57D0, 0x5252, 0x525E, 0x526E, 0x520A, 0x70CA, 19, 0x217A, 0x4BB6},
	{"ctrl_mt32_1_06",    OLD_MT32_LATER,    0x3000, 128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200, 30, 0x7414, 85, 0x57D9, 0x57F4, 0x57E2, 0x5264, 0x5270, 0x5280, 0x521C, 0x70CA, 19, 0x217A, 0x4BBA},
	{"ctrl_mt32_1_07",    OLD_MT32_LATER,    0x3000, 128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200, 30, 0x73fe, 85, 0x57B1, 0x57CC, 0x57BA, 0x523C, 0x5248, 0x5258, 0x51F4, 0x70B0, 19, 0x217A, 0x4B92},
	{"ctrl_mt32_bluer",   OLD_MT32_LATER,    0x3000, 128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200, 30, 0x741C, 85, 0x57E5, 0x5800, 0x57EE, 0x5270, 0x527C, 0x528C, 0x5228, 0x70CE, 19, 0x217A, 0x4BC6},
	{"ctrl_mt32_2_03",  NEW_MT32_COMPATIBLE, 0x8100, 128, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F49, 0x4F64, 0x4F52, 0x4885, 0x4889, 0x48A2, 0x48B9, 0x5A44, 19, 0x1EF0, 0x4066},
	{"ctrl_mt32_2_04",  NEW_MT32_COMPATIBLE, 0x8100, 128, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F5D, 0x4F78, 0x4F66, 0x4899, 0x489D, 0x48B6, 0x48CD, 0x5A58, 19, 0x1EF0, 0x406D},
	{"ctrl_mt32_2_06",  NEW_MT32_COMPATIBLE, 0x8100, 128, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F69, 0x4F84, 0x4F72, 0x48A5, 0x48A9, 0x48C2, 0x48D9, 0x5A64, 19, 0x1EF0, 0x4021},
	{"ctrl_mt32_2_07",  NEW_MT32_COMPATIBLE, 0x8100, 128, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F81, 0x4F9C, 0x4F8A, 0x48B9, 0x48BD, 0x48D6, 0x48ED, 0x5A78, 19, 0x1EE7, 0x4035},
	{"ctrl_cm32l_1_00", NEW_MT32_COMPATIBLE, 0x8100, 256, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F65, 0x4F80, 0x4F6E, 0x48A1, 0x48A5, 0x48BE, 0x48D5, 0x5A6C, 19, 0x1EF0, 0x401D},
	{"ctrl_cm32l_1_02", NEW_MT32_COMPATIBLE, 0x8100, 256, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4F93, 0x4FAE, 0x4F9C, 0x48CB, 0x48CF, 0x48E8, 0x48FF, 0x5A96, 19, 0x1EE7, 0x4047},
	{"ctrl_cm32ln_1_00", CM32LN_COMPATIBLE,  0x8100, 256, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500, 64, 0x8580, 85, 0x4EC7, 0x4EE2, 0x4ED0, 0x47FF, 0x4803, 0x481C, 0x4833, 0x55A2, 19, 0x1F59, 0x3F7C}
	// (Note that old MT-32 ROMs actually have 86 entries for rhythmTemp)
};

static const PartialState PARTIAL_PHASE_TO_STATE[8] = {
	PartialState_ATTACK, PartialState_ATTACK, PartialState_ATTACK, PartialState_ATTACK,
	PartialState_SUSTAIN, PartialState_SUSTAIN, PartialState_RELEASE, PartialState_INACTIVE
};

static inline PartialState getPartialState(PartialManager *partialManager, unsigned int partialNum) {
	const Partial *partial = partialManager->getPartial(partialNum);
	return partial->isActive() ? PARTIAL_PHASE_TO_STATE[partial->getTVA()->getPhase()] : PartialState_INACTIVE;
}

template <class I, class O>
static inline void convertSampleFormat(const I *inBuffer, O *outBuffer, const Bit32u len) {
	if (inBuffer == NULL || outBuffer == NULL) return;

	const I *inBufferEnd = inBuffer + len;
	while (inBuffer < inBufferEnd) {
		*(outBuffer++) = Synth::convertSample(*(inBuffer++));
	}
}

class Renderer {
protected:
	Synth &synth;

	void printDebug(const char *msg) const {
		synth.printDebug("%s", msg);
	}

	bool isActivated() const {
		return synth.activated;
	}

	bool isAbortingPoly() const {
		return synth.isAbortingPoly();
	}

	Analog &getAnalog() const {
		return *synth.analog;
	}

	MidiEventQueue &getMidiQueue() {
		return *synth.midiQueue;
	}

	PartialManager &getPartialManager() {
		return *synth.partialManager;
	}

	BReverbModel &getReverbModel() {
		return *synth.reverbModel;
	}

	Bit32u getRenderedSampleCount() {
		return synth.renderedSampleCount;
	}

	void incRenderedSampleCount(const Bit32u count) {
		synth.renderedSampleCount += count;
	}

	void updateDisplayState();

public:
	Renderer(Synth &useSynth) : synth(useSynth) {}

	virtual ~Renderer() {}

	virtual void render(IntSample *stereoStream, Bit32u len) = 0;
	virtual void render(FloatSample *stereoStream, Bit32u len) = 0;
	virtual void renderStreams(const DACOutputStreams<IntSample> &streams, Bit32u len) = 0;
	virtual void renderStreams(const DACOutputStreams<FloatSample> &streams, Bit32u len) = 0;
};

template <class Sample>
class RendererImpl : public Renderer {
	// These buffers are used for building the output streams as they are found at the DAC entrance.
	// The output is mixed down to stereo interleaved further in the analog circuitry emulation.
	Sample tmpNonReverbLeft[MAX_SAMPLES_PER_RUN], tmpNonReverbRight[MAX_SAMPLES_PER_RUN];
	Sample tmpReverbDryLeft[MAX_SAMPLES_PER_RUN], tmpReverbDryRight[MAX_SAMPLES_PER_RUN];
	Sample tmpReverbWetLeft[MAX_SAMPLES_PER_RUN], tmpReverbWetRight[MAX_SAMPLES_PER_RUN];

	const DACOutputStreams<Sample> tmpBuffers;
	DACOutputStreams<Sample> createTmpBuffers() {
		DACOutputStreams<Sample> buffers = {
			tmpNonReverbLeft, tmpNonReverbRight,
			tmpReverbDryLeft, tmpReverbDryRight,
			tmpReverbWetLeft, tmpReverbWetRight
		};
		return buffers;
	}

public:
	RendererImpl(Synth &useSynth) :
		Renderer(useSynth),
		tmpBuffers(createTmpBuffers())
	{}

	void render(IntSample *stereoStream, Bit32u len);
	void render(FloatSample *stereoStream, Bit32u len);
	void renderStreams(const DACOutputStreams<IntSample> &streams, Bit32u len);
	void renderStreams(const DACOutputStreams<FloatSample> &streams, Bit32u len);

	template <class O>
	void doRenderAndConvert(O *stereoStream, Bit32u len);
	void doRender(Sample *stereoStream, Bit32u len);

	template <class O>
	void doRenderAndConvertStreams(const DACOutputStreams<O> &streams, Bit32u len);
	void doRenderStreams(const DACOutputStreams<Sample> &streams, Bit32u len);
	void produceLA32Output(Sample *buffer, Bit32u len);
	void convertSamplesToOutput(Sample *buffer, Bit32u len);
	void produceStreams(const DACOutputStreams<Sample> &streams, Bit32u len);
};

class Extensions {
public:
	RendererType selectedRendererType;
	Bit32s masterTunePitchDelta;
	bool niceAmpRamp;
	bool nicePanning;
	bool nicePartialMixing;

	// Here we keep the reverse mapping of assigned parts per MIDI channel.
	// NOTE: value above 8 means that the channel is not assigned
	Bit8u chantable[16][9];

	// This stores the index of Part in chantable that failed to play and required partial abortion.
	Bit32u abortingPartIx;

	bool preallocatedReverbMemory;

	Bit32u midiEventQueueSize;
	Bit32u midiEventQueueSysexStorageBufferSize;

	Display *display;
	bool oldMT32DisplayFeatures;

	ReportHandler2 defaultReportHandler;
	ReportHandler2 *reportHandler2;
};

Bit32u Synth::getLibraryVersionInt() {
	return MT32EMU_CURRENT_VERSION_INT;
}

const char *Synth::getLibraryVersionString() {
	return MT32EMU_VERSION;
}

Bit8u Synth::calcSysexChecksum(const Bit8u *data, const Bit32u len, const Bit8u initChecksum) {
	unsigned int checksum = -initChecksum;
	for (unsigned int i = 0; i < len; i++) {
		checksum -= data[i];
	}
	return Bit8u(checksum & 0x7f);
}

Bit32u Synth::getStereoOutputSampleRate(AnalogOutputMode analogOutputMode) {
	static const unsigned int SAMPLE_RATES[] = {SAMPLE_RATE, SAMPLE_RATE, SAMPLE_RATE * 3 / 2, SAMPLE_RATE * 3};

	return SAMPLE_RATES[analogOutputMode];
}

Synth::Synth(ReportHandler *useReportHandler) :
	mt32ram(*new MemParams),
	mt32default(*new MemParams),
	extensions(*new Extensions)
{
	opened = false;
	reverbOverridden = false;
	partialCount = DEFAULT_MAX_PARTIALS;
	controlROMMap = NULL;
	controlROMFeatures = NULL;

	reportHandler = useReportHandler != NULL ? useReportHandler : &extensions.defaultReportHandler;
	extensions.reportHandler2 = &extensions.defaultReportHandler;

	extensions.preallocatedReverbMemory = false;
	for (int i = REVERB_MODE_ROOM; i <= REVERB_MODE_TAP_DELAY; i++) {
		reverbModels[i] = NULL;
	}
	reverbModel = NULL;
	analog = NULL;
	renderer = NULL;
	setDACInputMode(DACInputMode_NICE);
	setMIDIDelayMode(MIDIDelayMode_DELAY_SHORT_MESSAGES_ONLY);
	setOutputGain(1.0f);
	setReverbOutputGain(1.0f);
	setReversedStereoEnabled(false);
	setNiceAmpRampEnabled(true);
	setNicePanningEnabled(false);
	setNicePartialMixingEnabled(false);
	selectRendererType(RendererType_BIT16S);

	patchTempMemoryRegion = NULL;
	rhythmTempMemoryRegion = NULL;
	timbreTempMemoryRegion = NULL;
	patchesMemoryRegion = NULL;
	timbresMemoryRegion = NULL;
	systemMemoryRegion = NULL;
	displayMemoryRegion = NULL;
	resetMemoryRegion = NULL;
	paddedTimbreMaxTable = NULL;

	partialManager = NULL;
	pcmWaves = NULL;
	pcmROMData = NULL;
	soundGroupNames = NULL;
	midiQueue = NULL;
	extensions.midiEventQueueSize = DEFAULT_MIDI_EVENT_QUEUE_SIZE;
	extensions.midiEventQueueSysexStorageBufferSize = 0;
	lastReceivedMIDIEventTimestamp = 0;
	memset(parts, 0, sizeof(parts));
	renderedSampleCount = 0;
	extensions.display = NULL;
	extensions.oldMT32DisplayFeatures = false;
}

Synth::~Synth() {
	close(); // Make sure we're closed and everything is freed
	delete &mt32ram;
	delete &mt32default;
	delete &extensions;
}

void Synth::setReportHandler2(ReportHandler2 *reportHandler2) {
	if (reportHandler2 != NULL) {
		reportHandler = reportHandler2;
		extensions.reportHandler2 = reportHandler2;
	} else {
		reportHandler = &extensions.defaultReportHandler;
		extensions.reportHandler2 = &extensions.defaultReportHandler;
	}
}

void ReportHandler::showLCDMessage(const char *data) {
	printf("WRITE-LCD: %s\n", data);
}

void ReportHandler::printDebug(const char *fmt, va_list list) {
	vprintf(fmt, list);
	printf("\n");
}

void Synth::rhythmNotePlayed() const {
	extensions.display->rhythmNotePlayed();
}

void Synth::voicePartStateChanged(Bit8u partNum, bool partActivated) const {
	extensions.display->voicePartStateChanged(partNum, partActivated);
}

void Synth::newTimbreSet(Bit8u partNum) const {
	const Part *part = getPart(partNum);
	reportHandler->onProgramChanged(partNum, getSoundGroupName(part), part->getCurrentInstr());
}

const char *Synth::getSoundGroupName(const Part *part) const {
	const PatchParam &patch = part->getPatchTemp()->patch;
	return getSoundGroupName(patch.timbreGroup, patch.timbreNum);
}

const char *Synth::getSoundGroupName(Bit8u timbreGroup, Bit8u timbreNumber) const {
	switch (timbreGroup) {
	case 1:
		timbreNumber += 64;
		// Fall-through
	case 0:
		return soundGroupNames[soundGroupIx[timbreNumber]];
	case 2:
		return soundGroupNames[controlROMMap->soundGroupsCount - 2];
	case 3:
		return soundGroupNames[controlROMMap->soundGroupsCount - 1];
	default:
		return NULL;
	}
}

#define MT32EMU_PRINT_DEBUG \
	va_list ap; \
	va_start(ap, fmt); \
	reportHandler->printDebug(fmt, ap); \
	va_end(ap);

#if MT32EMU_DEBUG_SAMPLESTAMPS > 0
static inline void printSamplestamp(ReportHandler *reportHandler, const char *fmt, ...) {
	MT32EMU_PRINT_DEBUG
}
#endif

void Synth::printDebug(const char *fmt, ...) {
#if MT32EMU_DEBUG_SAMPLESTAMPS > 0
	printSamplestamp(reportHandler, "[%u]", renderedSampleCount);
#endif
	MT32EMU_PRINT_DEBUG
}

#undef MT32EMU_PRINT_DEBUG

void Synth::setReverbEnabled(bool newReverbEnabled) {
	if (!opened) return;
	if (isReverbEnabled() == newReverbEnabled) return;
	if (newReverbEnabled) {
		bool oldReverbOverridden = reverbOverridden;
		reverbOverridden = false;
		refreshSystemReverbParameters();
		reverbOverridden = oldReverbOverridden;
	} else {
		if (!extensions.preallocatedReverbMemory) {
			reverbModel->close();
		}
		reverbModel = NULL;
	}
}

bool Synth::isReverbEnabled() const {
	return reverbModel != NULL;
}

void Synth::setReverbOverridden(bool newReverbOverridden) {
	reverbOverridden = newReverbOverridden;
}

bool Synth::isReverbOverridden() const {
	return reverbOverridden;
}

void Synth::setReverbCompatibilityMode(bool mt32CompatibleMode) {
	if (!opened || (isMT32ReverbCompatibilityMode() == mt32CompatibleMode)) return;
	bool oldReverbEnabled = isReverbEnabled();
	setReverbEnabled(false);
	for (int i = REVERB_MODE_ROOM; i <= REVERB_MODE_TAP_DELAY; i++) {
		delete reverbModels[i];
	}
	initReverbModels(mt32CompatibleMode);
	setReverbEnabled(oldReverbEnabled);
	setReverbOutputGain(reverbOutputGain);
}

bool Synth::isMT32ReverbCompatibilityMode() const {
	return opened && (reverbModels[REVERB_MODE_ROOM]->isMT32Compatible(REVERB_MODE_ROOM));
}

bool Synth::isDefaultReverbMT32Compatible() const {
	return opened && controlROMFeatures->defaultReverbMT32Compatible;
}

void Synth::preallocateReverbMemory(bool enabled) {
	if (extensions.preallocatedReverbMemory == enabled) return;
	extensions.preallocatedReverbMemory = enabled;
	if (!opened) return;
	for (int i = REVERB_MODE_ROOM; i <= REVERB_MODE_TAP_DELAY; i++) {
		if (enabled) {
			reverbModels[i]->open();
		} else if (reverbModel != reverbModels[i]) {
			reverbModels[i]->close();
		}
	}
}

void Synth::setDACInputMode(DACInputMode mode) {
	dacInputMode = mode;
}

DACInputMode Synth::getDACInputMode() const {
	return dacInputMode;
}

void Synth::setMIDIDelayMode(MIDIDelayMode mode) {
	midiDelayMode = mode;
}

MIDIDelayMode Synth::getMIDIDelayMode() const {
	return midiDelayMode;
}

void Synth::setOutputGain(float newOutputGain) {
	if (newOutputGain < 0.0f) newOutputGain = -newOutputGain;
	outputGain = newOutputGain;
	if (analog != NULL) analog->setSynthOutputGain(newOutputGain);
}

float Synth::getOutputGain() const {
	return outputGain;
}

void Synth::setReverbOutputGain(float newReverbOutputGain) {
	if (newReverbOutputGain < 0.0f) newReverbOutputGain = -newReverbOutputGain;
	reverbOutputGain = newReverbOutputGain;
	if (analog != NULL) analog->setReverbOutputGain(newReverbOutputGain, isMT32ReverbCompatibilityMode());
}

float Synth::getReverbOutputGain() const {
	return reverbOutputGain;
}

void Synth::setPartVolumeOverride(Bit8u partNumber, Bit8u volumeOverride) {
	if (opened && partNumber < 9) {
		parts[partNumber]->setVolumeOverride(volumeOverride);
	}
}

Bit8u Synth::getPartVolumeOverride(Bit8u partNumber) const {
	return (!opened || partNumber > 8) ? 255 : parts[partNumber]->getVolumeOverride();
}

void Synth::setReversedStereoEnabled(bool enabled) {
	reversedStereoEnabled = enabled;
}

bool Synth::isReversedStereoEnabled() const {
	return reversedStereoEnabled;
}

void Synth::setNiceAmpRampEnabled(bool enabled) {
	extensions.niceAmpRamp = enabled;
}

bool Synth::isNiceAmpRampEnabled() const {
	return extensions.niceAmpRamp;
}

void Synth::setNicePanningEnabled(bool enabled) {
	extensions.nicePanning = enabled;
}

bool Synth::isNicePanningEnabled() const {
	return extensions.nicePanning;
}

void Synth::setNicePartialMixingEnabled(bool enabled) {
	extensions.nicePartialMixing = enabled;
}

bool Synth::isNicePartialMixingEnabled() const {
	return extensions.nicePartialMixing;
}

bool Synth::loadControlROM(const ROMImage &controlROMImage) {
	File *file = controlROMImage.getFile();
	const ROMInfo *controlROMInfo = controlROMImage.getROMInfo();
	if ((controlROMInfo == NULL)
			|| (controlROMInfo->type != ROMInfo::Control)
			|| (controlROMInfo->pairType != ROMInfo::Full)) {
#if MT32EMU_MONITOR_INIT
		printDebug("Invalid Control ROM Info provided");
#endif
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Found Control ROM: %s, %s", controlROMInfo->shortName, controlROMInfo->description);
#endif
	const Bit8u *fileData = file->getData();
	memcpy(controlROMData, fileData, CONTROL_ROM_SIZE);

	// Control ROM successfully loaded, now check whether it's a known type
	controlROMMap = NULL;
	controlROMFeatures = NULL;
	for (unsigned int i = 0; i < sizeof(ControlROMMaps) / sizeof(ControlROMMaps[0]); i++) {
		if (strcmp(controlROMInfo->shortName, ControlROMMaps[i].shortName) == 0) {
			controlROMMap = &ControlROMMaps[i];
			controlROMFeatures = &controlROMMap->featureSet;
			return true;
		}
	}
#if MT32EMU_MONITOR_INIT
	printDebug("Control ROM failed to load");
#endif
	return false;
}

bool Synth::loadPCMROM(const ROMImage &pcmROMImage) {
	File *file = pcmROMImage.getFile();
	const ROMInfo *pcmROMInfo = pcmROMImage.getROMInfo();
	if ((pcmROMInfo == NULL)
			|| (pcmROMInfo->type != ROMInfo::PCM)
			|| (pcmROMInfo->pairType != ROMInfo::Full)) {
		return false;
	}
#if MT32EMU_MONITOR_INIT
	printDebug("Found PCM ROM: %s, %s", pcmROMInfo->shortName, pcmROMInfo->description);
#endif
	size_t fileSize = file->getSize();
	if (fileSize != (2 * pcmROMSize)) {
#if MT32EMU_MONITOR_INIT
		printDebug("PCM ROM file has wrong size (expected %d, got %d)", 2 * pcmROMSize, fileSize);
#endif
		return false;
	}
	const Bit8u *fileData = file->getData();
	for (size_t i = 0; i < pcmROMSize; i++) {
		Bit8u s = *(fileData++);
		Bit8u c = *(fileData++);

		int order[16] = {0, 9, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 8};

		Bit16s log = 0;
		for (int u = 0; u < 16; u++) {
			int bit;
			if (order[u] < 8) {
				bit = (s >> (7 - order[u])) & 0x1;
			} else {
				bit = (c >> (7 - (order[u] - 8))) & 0x1;
			}
			log = log | Bit16s(bit << (15 - u));
		}
		pcmROMData[i] = log;
	}
	return true;
}

bool Synth::initPCMList(Bit16u mapAddress, Bit16u count) {
	ControlROMPCMStruct *tps = reinterpret_cast<ControlROMPCMStruct *>(&controlROMData[mapAddress]);
	for (int i = 0; i < count; i++) {
		Bit32u rAddr = tps[i].pos * 0x800;
		Bit32u rLenExp = (tps[i].len & 0x70) >> 4;
		Bit32u rLen = 0x800 << rLenExp;
		if (rAddr + rLen > pcmROMSize) {
			printDebug("Control ROM error: Wave map entry %d points to invalid PCM address 0x%04X, length 0x%04X", i, rAddr, rLen);
			return false;
		}
		pcmWaves[i].addr = rAddr;
		pcmWaves[i].len = rLen;
		pcmWaves[i].loop = (tps[i].len & 0x80) != 0;
		pcmWaves[i].controlROMPCMStruct = &tps[i];
		//int pitch = (tps[i].pitchMSB << 8) | tps[i].pitchLSB;
		//bool unaffectedByMasterTune = (tps[i].len & 0x01) == 0;
		//printDebug("PCM %d: pos=%d, len=%d, pitch=%d, loop=%s, unaffectedByMasterTune=%s", i, rAddr, rLen, pitch, pcmWaves[i].loop ? "YES" : "NO", unaffectedByMasterTune ? "YES" : "NO");
	}
	return false;
}

bool Synth::initCompressedTimbre(Bit16u timbreNum, const Bit8u *src, Bit32u srcLen) {
	// "Compressed" here means that muted partials aren't present in ROM (except in the case of partial 0 being muted).
	// Instead the data from the previous unmuted partial is used.
	if (srcLen < sizeof(TimbreParam::CommonParam)) {
		return false;
	}
	TimbreParam *timbre = &mt32ram.timbres[timbreNum].timbre;
	timbresMemoryRegion->write(timbreNum, 0, src, sizeof(TimbreParam::CommonParam), true);
	unsigned int srcPos = sizeof(TimbreParam::CommonParam);
	unsigned int memPos = sizeof(TimbreParam::CommonParam);
	for (int t = 0; t < 4; t++) {
		if (t != 0 && ((timbre->common.partialMute >> t) & 0x1) == 0x00) {
			// This partial is muted - we'll copy the previously copied partial, then
			srcPos -= sizeof(TimbreParam::PartialParam);
		} else if (srcPos + sizeof(TimbreParam::PartialParam) >= srcLen) {
			return false;
		}
		timbresMemoryRegion->write(timbreNum, memPos, src + srcPos, sizeof(TimbreParam::PartialParam));
		srcPos += sizeof(TimbreParam::PartialParam);
		memPos += sizeof(TimbreParam::PartialParam);
	}
	return true;
}

bool Synth::initTimbres(Bit16u mapAddress, Bit16u offset, Bit16u count, Bit16u startTimbre, bool compressed) {
	const Bit8u *timbreMap = &controlROMData[mapAddress];
	for (Bit16u i = 0; i < count * 2; i += 2) {
		Bit16u address = (timbreMap[i + 1] << 8) | timbreMap[i];
		if (!compressed && (address + offset + sizeof(TimbreParam) > CONTROL_ROM_SIZE)) {
			printDebug("Control ROM error: Timbre map entry 0x%04x for timbre %d points to invalid timbre address 0x%04x", i, startTimbre, address);
			return false;
		}
		address += offset;
		if (compressed) {
			if (!initCompressedTimbre(startTimbre, &controlROMData[address], CONTROL_ROM_SIZE - address)) {
				printDebug("Control ROM error: Timbre map entry 0x%04x for timbre %d points to invalid timbre at 0x%04x", i, startTimbre, address);
				return false;
			}
		} else {
			timbresMemoryRegion->write(startTimbre, 0, &controlROMData[address], sizeof(TimbreParam), true);
		}
		startTimbre++;
	}
	return true;
}

void Synth::initReverbModels(bool mt32CompatibleMode) {
	for (int mode = REVERB_MODE_ROOM; mode <= REVERB_MODE_TAP_DELAY; mode++) {
		reverbModels[mode] = BReverbModel::createBReverbModel(ReverbMode(mode), mt32CompatibleMode, getSelectedRendererType());

		if (extensions.preallocatedReverbMemory) {
			reverbModels[mode]->open();
		}
	}
}

void Synth::initSoundGroups(char newSoundGroupNames[][9]) {
	memcpy(soundGroupIx, &controlROMData[controlROMMap->soundGroupsTable - sizeof(soundGroupIx)], sizeof(soundGroupIx));
	const SoundGroup *table = reinterpret_cast<SoundGroup *>(&controlROMData[controlROMMap->soundGroupsTable]);
	for (unsigned int i = 0; i < controlROMMap->soundGroupsCount; i++) {
		memcpy(&newSoundGroupNames[i][0], table[i].name, sizeof(table[i].name));
	}
}

bool Synth::open(const ROMImage &controlROMImage, const ROMImage &pcmROMImage, AnalogOutputMode analogOutputMode) {
	return open(controlROMImage, pcmROMImage, DEFAULT_MAX_PARTIALS, analogOutputMode);
}

bool Synth::open(const ROMImage &controlROMImage, const ROMImage &pcmROMImage, Bit32u usePartialCount, AnalogOutputMode analogOutputMode) {
	if (opened) {
		return false;
	}
	partialCount = usePartialCount;
	abortingPoly = NULL;
	extensions.abortingPartIx = 0;

	// This is to help detect bugs
	memset(&mt32ram, '?', sizeof(mt32ram));

#if MT32EMU_MONITOR_INIT
	printDebug("Loading Control ROM");
#endif
	if (!loadControlROM(controlROMImage)) {
		printDebug("Init Error - Missing or invalid Control ROM image");
		reportHandler->onErrorControlROM();
		dispose();
		return false;
	}

	initMemoryRegions();

	// 512KB PCM ROM for MT-32, etc.
	// 1MB PCM ROM for CM-32L, LAPC-I, CM-64, CM-500
	// Note that the size below is given in samples (16-bit), not bytes
	pcmROMSize = controlROMMap->pcmCount == 256 ? 512 * 1024 : 256 * 1024;
	pcmROMData = new Bit16s[pcmROMSize];

#if MT32EMU_MONITOR_INIT
	printDebug("Loading PCM ROM");
#endif
	if (!loadPCMROM(pcmROMImage)) {
		printDebug("Init Error - Missing PCM ROM image");
		reportHandler->onErrorPCMROM();
		dispose();
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Reverb Models");
#endif
	bool mt32CompatibleReverb = controlROMFeatures->defaultReverbMT32Compatible;
#if MT32EMU_MONITOR_INIT
	printDebug("Using %s Compatible Reverb Models", mt32CompatibleReverb ? "MT-32" : "CM-32L");
#endif
	initReverbModels(mt32CompatibleReverb);

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank A");
#endif
	if (!initTimbres(controlROMMap->timbreAMap, controlROMMap->timbreAOffset, 0x40, 0, controlROMMap->timbreACompressed)) {
		dispose();
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank B");
#endif
	if (!initTimbres(controlROMMap->timbreBMap, controlROMMap->timbreBOffset, 0x40, 64, controlROMMap->timbreBCompressed)) {
		dispose();
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank R");
#endif
	if (!initTimbres(controlROMMap->timbreRMap, 0, controlROMMap->timbreRCount, 192, true)) {
		dispose();
		return false;
	}

	if (controlROMMap->timbreRCount == 30) {
		// We must initialise all 64 rhythm timbres to avoid undefined behaviour.
		// SEMI-CONFIRMED: Old-gen MT-32 units likely map timbres 30..59 to 0..29.
		// Attempts to play rhythm timbres 60..63 exhibit undefined behaviour.
		// We want to emulate the wrap around, so merely copy the entire set of standard
		// timbres once more. The last 4 dangerous timbres are zeroed out.
		memcpy(&mt32ram.timbres[222], &mt32ram.timbres[192], sizeof(*mt32ram.timbres) * 30);
		memset(&mt32ram.timbres[252], 0, sizeof(*mt32ram.timbres) * 4);
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank M");
#endif
	// CM-64 seems to initialise all bytes in this bank to 0.
	memset(&mt32ram.timbres[128], 0, sizeof(mt32ram.timbres[128]) * 64);

	partialManager = new PartialManager(this, parts);

	pcmWaves = new PCMWaveEntry[controlROMMap->pcmCount];

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising PCM List");
#endif
	initPCMList(controlROMMap->pcmTable, controlROMMap->pcmCount);

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Rhythm Temp");
#endif
	memcpy(mt32ram.rhythmTemp, &controlROMData[controlROMMap->rhythmSettings], controlROMMap->rhythmSettingsCount * 4);

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Patches");
#endif
	for (Bit8u i = 0; i < 128; i++) {
		PatchParam *patch = &mt32ram.patches[i];
		patch->timbreGroup = i / 64;
		patch->timbreNum = i % 64;
		patch->keyShift = 24;
		patch->fineTune = 50;
		patch->benderRange = 12;
		patch->assignMode = 0;
		patch->reverbSwitch = 1;
		patch->dummy = 0;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising System");
#endif
	// The MT-32 manual claims that "Standard pitch" is 442Hz.
	mt32ram.system.masterTune = 0x4A; // Confirmed on CM-64
	mt32ram.system.reverbMode = 0; // Confirmed
	mt32ram.system.reverbTime = 5; // Confirmed
	mt32ram.system.reverbLevel = 3; // Confirmed
	memcpy(mt32ram.system.reserveSettings, &controlROMData[controlROMMap->reserveSettings], 9); // Confirmed
	for (Bit8u i = 0; i < 9; i++) {
		// This is the default: {1, 2, 3, 4, 5, 6, 7, 8, 9}
		// An alternative configuration can be selected by holding "Master Volume"
		// and pressing "PART button 1" on the real MT-32's frontpanel.
		// The channel assignment is then {0, 1, 2, 3, 4, 5, 6, 7, 9}
		mt32ram.system.chanAssign[i] = i + 1;
	}
	mt32ram.system.masterVol = 100; // Confirmed

	bool oldReverbOverridden = reverbOverridden;
	reverbOverridden = false;
	refreshSystem();
	resetMasterTunePitchDelta();
	reverbOverridden = oldReverbOverridden;

	char(*writableSoundGroupNames)[9] = new char[controlROMMap->soundGroupsCount][9];
	soundGroupNames = writableSoundGroupNames;
	initSoundGroups(writableSoundGroupNames);

	for (int i = 0; i < 9; i++) {
		MemParams::PatchTemp *patchTemp = &mt32ram.patchTemp[i];

		// Note that except for the rhythm part, these patch fields will be set in setProgram() below anyway.
		patchTemp->patch.timbreGroup = 0;
		patchTemp->patch.timbreNum = 0;
		patchTemp->patch.keyShift = 24;
		patchTemp->patch.fineTune = 50;
		patchTemp->patch.benderRange = 12;
		patchTemp->patch.assignMode = 0;
		patchTemp->patch.reverbSwitch = 1;
		patchTemp->patch.dummy = 0;

		patchTemp->outputLevel = 80;
		patchTemp->panpot = controlROMData[controlROMMap->panSettings + i];
		memset(patchTemp->dummyv, 0, sizeof(patchTemp->dummyv));
		patchTemp->dummyv[1] = 127;

		if (i < 8) {
			parts[i] = new Part(this, i);
			parts[i]->setProgram(controlROMData[controlROMMap->programSettings + i]);
		} else {
			parts[i] = new RhythmPart(this, i);
		}
	}

	// For resetting mt32 mid-execution
	mt32default = mt32ram;

	midiQueue = new MidiEventQueue(extensions.midiEventQueueSize, extensions.midiEventQueueSysexStorageBufferSize);

	analog = Analog::createAnalog(analogOutputMode, controlROMFeatures->oldMT32AnalogLPF, getSelectedRendererType());
#if MT32EMU_MONITOR_INIT
	static const char *ANALOG_OUTPUT_MODES[] = { "Digital only", "Coarse", "Accurate", "Oversampled2x" };
	printDebug("Using Analog output mode %s", ANALOG_OUTPUT_MODES[analogOutputMode]);
#endif
	setOutputGain(outputGain);
	setReverbOutputGain(reverbOutputGain);

	switch (getSelectedRendererType()) {
		case RendererType_BIT16S:
			renderer = new RendererImpl<IntSample>(*this);
#if MT32EMU_MONITOR_INIT
			printDebug("Using integer 16-bit samples in renderer and wave generator");
#endif
			break;
		case RendererType_FLOAT:
			renderer = new RendererImpl<FloatSample>(*this);
#if MT32EMU_MONITOR_INIT
			printDebug("Using float 32-bit samples in renderer and wave generator");
#endif
			break;
		default:
			printDebug("Synth: Unknown renderer type %i\n", getSelectedRendererType());
			dispose();
			return false;
	}

	extensions.display = new Display(*this);
	extensions.oldMT32DisplayFeatures = controlROMFeatures->oldMT32DisplayFeatures;

	opened = true;
	activated = false;

#if MT32EMU_MONITOR_INIT
	printDebug("*** Initialisation complete ***");
#endif
	return true;
}

void Synth::dispose() {
	opened = false;

	delete extensions.display;
	extensions.display = NULL;

	delete midiQueue;
	midiQueue = NULL;

	delete renderer;
	renderer = NULL;

	delete analog;
	analog = NULL;

	delete partialManager;
	partialManager = NULL;

	for (int i = 0; i < 9; i++) {
		delete parts[i];
		parts[i] = NULL;
	}

	delete[] soundGroupNames;
	soundGroupNames = NULL;

	delete[] pcmWaves;
	pcmWaves = NULL;

	delete[] pcmROMData;
	pcmROMData = NULL;

	deleteMemoryRegions();

	for (int i = REVERB_MODE_ROOM; i <= REVERB_MODE_TAP_DELAY; i++) {
		delete reverbModels[i];
		reverbModels[i] = NULL;
	}
	reverbModel = NULL;
	controlROMFeatures = NULL;
	controlROMMap = NULL;
}

void Synth::close() {
	if (opened) {
		dispose();
	}
}

bool Synth::isOpen() const {
	return opened;
}

void Synth::flushMIDIQueue() {
	if (midiQueue == NULL) return;
	for (;;) {
		const volatile MidiEventQueue::MidiEvent *midiEvent = midiQueue->peekMidiEvent();
		if (midiEvent == NULL) break;
		if (midiEvent->sysexData == NULL) {
			playMsgNow(midiEvent->shortMessageData);
		} else {
			playSysexNow(midiEvent->sysexData, midiEvent->sysexLength);
		}
		midiQueue->dropMidiEvent();
	}
	lastReceivedMIDIEventTimestamp = renderedSampleCount;
}

Bit32u Synth::setMIDIEventQueueSize(Bit32u useSize) {
	static const Bit32u MAX_QUEUE_SIZE = (1 << 24); // This results in about 256 Mb - much greater than any reasonable value

	if (extensions.midiEventQueueSize == useSize) return useSize;

	// Find a power of 2 that is >= useSize
	Bit32u binarySize = 1;
	if (useSize < MAX_QUEUE_SIZE) {
		// Using simple linear search as this isn't time critical
		while (binarySize < useSize) binarySize <<= 1;
	} else {
		binarySize = MAX_QUEUE_SIZE;
	}
	extensions.midiEventQueueSize = binarySize;
	if (midiQueue != NULL) {
		flushMIDIQueue();
		delete midiQueue;
		midiQueue = new MidiEventQueue(binarySize, extensions.midiEventQueueSysexStorageBufferSize);
	}
	return binarySize;
}

void Synth::configureMIDIEventQueueSysexStorage(Bit32u storageBufferSize) {
	if (extensions.midiEventQueueSysexStorageBufferSize == storageBufferSize) return;

	extensions.midiEventQueueSysexStorageBufferSize = storageBufferSize;
	if (midiQueue != NULL) {
		flushMIDIQueue();
		delete midiQueue;
		midiQueue = new MidiEventQueue(extensions.midiEventQueueSize, storageBufferSize);
	}
}

Bit32u Synth::getShortMessageLength(Bit32u msg) {
	if ((msg & 0xF0) == 0xF0) {
		switch (msg & 0xFF) {
			case 0xF1:
			case 0xF3:
				return 2;
			case 0xF2:
				return 3;
			default:
				return 1;
		}
	}
	// NOTE: This calculation isn't quite correct
	// as it doesn't consider the running status byte
	return ((msg & 0xE0) == 0xC0) ? 2 : 3;
}

Bit32u Synth::addMIDIInterfaceDelay(Bit32u len, Bit32u timestamp) {
	Bit32u transferTime =  Bit32u(double(len) * MIDI_DATA_TRANSFER_RATE);
	// Dealing with wrapping
	if (Bit32s(timestamp - lastReceivedMIDIEventTimestamp) < 0) {
		timestamp = lastReceivedMIDIEventTimestamp;
	}
	timestamp += transferTime;
	lastReceivedMIDIEventTimestamp = timestamp;
	return timestamp;
}

Bit32u Synth::getInternalRenderedSampleCount() const {
	return renderedSampleCount;
}

bool Synth::playMsg(Bit32u msg) {
	return playMsg(msg, renderedSampleCount);
}

bool Synth::playMsg(Bit32u msg, Bit32u timestamp) {
	if ((msg & 0xF8) == 0xF8) {
		reportHandler->onMIDISystemRealtime(Bit8u(msg & 0xFF));
		return true;
	}
	if (midiQueue == NULL) return false;
	if (midiDelayMode != MIDIDelayMode_IMMEDIATE) {
		timestamp = addMIDIInterfaceDelay(getShortMessageLength(msg), timestamp);
	}
	if (!activated) activated = true;
	do {
		if (midiQueue->pushShortMessage(msg, timestamp)) return true;
	} while (reportHandler->onMIDIQueueOverflow());
	return false;
}

bool Synth::playSysex(const Bit8u *sysex, Bit32u len) {
	return playSysex(sysex, len, renderedSampleCount);
}

bool Synth::playSysex(const Bit8u *sysex, Bit32u len, Bit32u timestamp) {
	if (midiQueue == NULL) return false;
	if (midiDelayMode == MIDIDelayMode_DELAY_ALL) {
		timestamp = addMIDIInterfaceDelay(len, timestamp);
	}
	if (!activated) activated = true;
	do {
		if (midiQueue->pushSysex(sysex, len, timestamp)) return true;
	} while (reportHandler->onMIDIQueueOverflow());
	return false;
}

void Synth::playMsgNow(Bit32u msg) {
	if (!opened) return;

	// NOTE: Active sense IS implemented in real hardware. However, realtime processing is clearly out of the library scope.
	//       It is assumed that realtime consumers of the library respond to these MIDI events as appropriate.

	Bit8u code = Bit8u((msg & 0x0000F0) >> 4);
	Bit8u chan = Bit8u(msg & 0x00000F);
	Bit8u note = Bit8u((msg & 0x007F00) >> 8);
	Bit8u velocity = Bit8u((msg & 0x7F0000) >> 16);

	//printDebug("Playing chan %d, code 0x%01x note: 0x%02x", chan, code, note);

	Bit8u *chanParts = extensions.chantable[chan];
	if (*chanParts > 8) {
#if MT32EMU_MONITOR_MIDI > 0
		printDebug("Play msg on unreg chan %d (%d): code=0x%01x, vel=%d", chan, *chanParts, code, velocity);
#endif
		return;
	}
	for (Bit32u i = extensions.abortingPartIx; i <= 8; i++) {
		const Bit32u partNum = chanParts[i];
		if (partNum > 8) break;
		playMsgOnPart(partNum, code, note, velocity);
		if (isAbortingPoly()) {
			extensions.abortingPartIx = i;
			break;
		} else if (extensions.abortingPartIx) {
			extensions.abortingPartIx = 0;
		}
	}
}

void Synth::playMsgOnPart(Bit8u part, Bit8u code, Bit8u note, Bit8u velocity) {
	if (!opened) return;

	Bit32u bend;

	if (!activated) activated = true;
	//printDebug("Synth::playMsgOnPart(%02x, %02x, %02x, %02x)", part, code, note, velocity);
	switch (code) {
	case 0x8:
		//printDebug("Note OFF - Part %d", part);
		// The MT-32 ignores velocity for note off
		parts[part]->noteOff(note);
		break;
	case 0x9:
		//printDebug("Note ON - Part %d, Note %d Vel %d", part, note, velocity);
		if (velocity == 0) {
			// MIDI defines note-on with velocity 0 as being the same as note-off with velocity 40
			parts[part]->noteOff(note);
		} else if (parts[part]->getVolumeOverride() > 0) {
			parts[part]->noteOn(note, velocity);
		}
		break;
	case 0xB: // Control change
		switch (note) {
		case 0x01:  // Modulation
			//printDebug("Modulation: %d", velocity);
			parts[part]->setModulation(velocity);
			break;
		case 0x06:
			parts[part]->setDataEntryMSB(velocity);
			break;
		case 0x07:  // Set volume
			//printDebug("Volume set: %d", velocity);
			parts[part]->setVolume(velocity);
			break;
		case 0x0A:  // Pan
			//printDebug("Pan set: %d", velocity);
			parts[part]->setPan(velocity);
			break;
		case 0x0B:
			//printDebug("Expression set: %d", velocity);
			parts[part]->setExpression(velocity);
			break;
		case 0x40: // Hold (sustain) pedal
			//printDebug("Hold pedal set: %d", velocity);
			parts[part]->setHoldPedal(velocity >= 64);
			break;

		case 0x62:
		case 0x63:
			parts[part]->setNRPN();
			break;
		case 0x64:
			parts[part]->setRPNLSB(velocity);
			break;
		case 0x65:
			parts[part]->setRPNMSB(velocity);
			break;

		case 0x79: // Reset all controllers
			//printDebug("Reset all controllers");
			parts[part]->resetAllControllers();
			break;

		case 0x7B: // All notes off
			//printDebug("All notes off");
			parts[part]->allNotesOff();
			break;

		case 0x7C:
		case 0x7D:
		case 0x7E:
		case 0x7F:
			// CONFIRMED:Mok: A real LAPC-I responds to these controllers as follows:
			parts[part]->setHoldPedal(false);
			parts[part]->allNotesOff();
			break;

		default:
#if MT32EMU_MONITOR_MIDI > 0
			printDebug("Unknown MIDI Control code: 0x%02x - vel 0x%02x", note, velocity);
#endif
			return;
		}
		extensions.display->midiMessagePlayed();
		break;
	case 0xC: // Program change
		//printDebug("Program change %01x", note);
		parts[part]->setProgram(note);
		if (part < 8) {
			extensions.display->midiMessagePlayed();
			extensions.display->programChanged(part);
		}
		break;
	case 0xE: // Pitch bender
		bend = (velocity << 7) | (note);
		//printDebug("Pitch bender %02x", bend);
		parts[part]->setBend(bend);
		extensions.display->midiMessagePlayed();
		break;
	default:
#if MT32EMU_MONITOR_MIDI > 0
		printDebug("Unknown Midi code: 0x%01x - %02x - %02x", code, note, velocity);
#endif
		return;
	}
	reportHandler->onMIDIMessagePlayed();
}

void Synth::playSysexNow(const Bit8u *sysex, Bit32u len) {
	if (len < 2) {
		printDebug("playSysex: Message is too short for sysex (%d bytes)", len);
	}
	if (sysex[0] != 0xF0) {
		printDebug("playSysex: Message lacks start-of-sysex (0xF0)");
		return;
	}
	// Due to some programs (e.g. Java) sending buffers with junk at the end, we have to go through and find the end marker rather than relying on len.
	Bit32u endPos;
	for (endPos = 1; endPos < len; endPos++) {
		if (sysex[endPos] == 0xF7) {
			break;
		}
	}
	if (endPos == len) {
		printDebug("playSysex: Message lacks end-of-sysex (0xf7)");
		return;
	}
	playSysexWithoutFraming(sysex + 1, endPos - 1);
}

void Synth::playSysexWithoutFraming(const Bit8u *sysex, Bit32u len) {
	if (len < 4) {
		printDebug("playSysexWithoutFraming: Message is too short (%d bytes)!", len);
		return;
	}
	if (sysex[0] != SYSEX_MANUFACTURER_ROLAND) {
		printDebug("playSysexWithoutFraming: Header not intended for this device manufacturer: %02x %02x %02x %02x", int(sysex[0]), int(sysex[1]), int(sysex[2]), int(sysex[3]));
		return;
	}
	if (sysex[2] == SYSEX_MDL_D50) {
		printDebug("playSysexWithoutFraming: Header is intended for model D-50 (not yet supported): %02x %02x %02x %02x", int(sysex[0]), int(sysex[1]), int(sysex[2]), int(sysex[3]));
		return;
	} else if (sysex[2] != SYSEX_MDL_MT32) {
		printDebug("playSysexWithoutFraming: Header not intended for model MT-32: %02x %02x %02x %02x", int(sysex[0]), int(sysex[1]), int(sysex[2]), int(sysex[3]));
		return;
	}
	playSysexWithoutHeader(sysex[1], sysex[3], sysex + 4, len - 4);
}

void Synth::playSysexWithoutHeader(Bit8u device, Bit8u command, const Bit8u *sysex, Bit32u len) {
	if (device > 0x10) {
		// We have device ID 0x10 (default, but changeable, on real MT-32), < 0x10 is for channels
		printDebug("playSysexWithoutHeader: Message is not intended for this device ID (provided: %02x, expected: 0x10 or channel)", int(device));
		return;
	}

	// All models process the checksum before anything else and ignore messages lacking the checksum, or containing the checksum only.
	if (len < 2) {
		printDebug("playSysexWithoutHeader: Message is too short (%d bytes)!", len);
		return;
	}
	Bit8u checksum = calcSysexChecksum(sysex, len - 1);
	if (checksum != sysex[len - 1]) {
		printDebug("playSysexWithoutHeader: Message checksum is incorrect (provided: %02x, expected: %02x)!", sysex[len - 1], checksum);
		if (opened) extensions.display->checksumErrorOccurred();
		return;
	}
	len -= 1; // Exclude checksum

	if (command == SYSEX_CMD_EOD) {
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("playSysexWithoutHeader: Ignored unsupported command %02x", command);
#endif
		return;
	}
	switch (command) {
	case SYSEX_CMD_WSD:
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("playSysexWithoutHeader: Ignored unsupported command %02x", command);
#endif
		break;
	case SYSEX_CMD_DAT:
		/* Outcommented until we (ever) actually implement handshake communication
		if (hasActivePartials()) {
			printDebug("playSysexWithoutHeader: Got SYSEX_CMD_DAT but partials are active - ignoring");
			// FIXME: We should send SYSEX_CMD_RJC in this case
			break;
		}
		*/
		// Fall-through
	case SYSEX_CMD_DT1:
		writeSysex(device, sysex, len);
		break;
	case SYSEX_CMD_RQD:
		if (hasActivePartials()) {
			printDebug("playSysexWithoutHeader: Got SYSEX_CMD_RQD but partials are active - ignoring");
			// FIXME: We should send SYSEX_CMD_RJC in this case
			break;
		}
		// Fall-through
	case SYSEX_CMD_RQ1:
		readSysex(device, sysex, len);
		break;
	default:
		printDebug("playSysexWithoutHeader: Unsupported command %02x", command);
		return;
	}
}

void Synth::readSysex(Bit8u /*device*/, const Bit8u * /*sysex*/, Bit32u /*len*/) const {
	// NYI
}

void Synth::writeSysex(Bit8u device, const Bit8u *sysex, Bit32u len) {
	if (!opened || len < 1) return;

	// This is checked early in the real devices (before any sysex length checks or further processing)
	if (sysex[0] == 0x7F) {
		if (!isDisplayOldMT32Compatible()) extensions.display->midiMessagePlayed();
		reset();
		return;
	}

	extensions.display->midiMessagePlayed();
	reportHandler->onMIDIMessagePlayed();

	if (len < 3) {
		// A short message of just 1 or 2 bytes may be written to the display area yet it may cause a user-visible effect,
		// similarly to the reset area.
		if (sysex[0] == 0x20) {
			extensions.display->displayControlMessageReceived(sysex, len);
			return;
		}
		printDebug("writeSysex: Message is too short (%d bytes)!", len);
		return;
	}

	Bit32u addr = (sysex[0] << 16) | (sysex[1] << 8) | (sysex[2]);
	addr = MT32EMU_MEMADDR(addr);
	sysex += 3;
	len -= 3;

	//printDebug("Sysex addr: 0x%06x", MT32EMU_SYSEXMEMADDR(addr));
	// NOTE: Please keep both lower and upper bounds in each check, for ease of reading

	// Process channel-specific sysex by converting it to device-global
	if (device < 0x10) {
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("WRITE-CHANNEL: Channel %d temp area 0x%06x", device, MT32EMU_SYSEXMEMADDR(addr));
#endif
		if (/*addr >= MT32EMU_MEMADDR(0x000000) && */addr < MT32EMU_MEMADDR(0x010000)) {
			addr += MT32EMU_MEMADDR(0x030000);
			Bit8u *chanParts = extensions.chantable[device];
			if (*chanParts > 8) {
#if MT32EMU_MONITOR_SYSEX > 0
				printDebug(" (Channel not mapped to a part... 0 offset)");
#endif
			} else {
				for (Bit32u partIx = 0; partIx <= 8; partIx++) {
					if (chanParts[partIx] > 8) break;
					int offset;
					if (chanParts[partIx] == 8) {
#if MT32EMU_MONITOR_SYSEX > 0
						printDebug(" (Channel mapped to rhythm... 0 offset)");
#endif
						offset = 0;
					} else {
						offset = chanParts[partIx] * sizeof(MemParams::PatchTemp);
#if MT32EMU_MONITOR_SYSEX > 0
						printDebug(" (Setting extra offset to %d)", offset);
#endif
					}
					writeSysexGlobal(addr + offset, sysex, len);
				}
				return;
			}
		} else if (/*addr >= MT32EMU_MEMADDR(0x010000) && */ addr < MT32EMU_MEMADDR(0x020000)) {
			addr += MT32EMU_MEMADDR(0x030110) - MT32EMU_MEMADDR(0x010000);
		} else if (/*addr >= MT32EMU_MEMADDR(0x020000) && */ addr < MT32EMU_MEMADDR(0x030000)) {
			addr += MT32EMU_MEMADDR(0x040000) - MT32EMU_MEMADDR(0x020000);
			Bit8u *chanParts = extensions.chantable[device];
			if (*chanParts > 8) {
#if MT32EMU_MONITOR_SYSEX > 0
				printDebug(" (Channel not mapped to a part... 0 offset)");
#endif
			} else {
				for (Bit32u partIx = 0; partIx <= 8; partIx++) {
					if (chanParts[partIx] > 8) break;
					int offset;
					if (chanParts[partIx] == 8) {
#if MT32EMU_MONITOR_SYSEX > 0
						printDebug(" (Channel mapped to rhythm... 0 offset)");
#endif
						offset = 0;
					} else {
						offset = chanParts[partIx] * sizeof(TimbreParam);
#if MT32EMU_MONITOR_SYSEX > 0
						printDebug(" (Setting extra offset to %d)", offset);
#endif
					}
					writeSysexGlobal(addr + offset, sysex, len);
				}
				return;
			}
		} else {
#if MT32EMU_MONITOR_SYSEX > 0
			printDebug(" Invalid channel");
#endif
			return;
		}
	}
	writeSysexGlobal(addr, sysex, len);
}

// Process device-global sysex (possibly converted from channel-specific sysex above)
void Synth::writeSysexGlobal(Bit32u addr, const Bit8u *sysex, Bit32u len) {
	for (;;) {
		// Find the appropriate memory region
		const MemoryRegion *region = findMemoryRegion(addr);

		if (region == NULL) {
			printDebug("Sysex write to unrecognised address %06x, len %d", MT32EMU_SYSEXMEMADDR(addr), len);
			// FIXME: Real devices may respond differently to a long SysEx that covers adjacent regions.
			break;
		}
		writeMemoryRegion(region, addr, region->getClampedLen(addr, len), sysex);

		Bit32u next = region->next(addr, len);
		if (next == 0) {
			break;
		}
		addr += next;
		sysex += next;
		len -= next;
	}
}

void Synth::readMemory(Bit32u addr, Bit32u len, Bit8u *data) {
	if (!opened) return;
	const MemoryRegion *region = findMemoryRegion(addr);
	if (region != NULL) {
		readMemoryRegion(region, addr, len, data);
	}
}

void Synth::initMemoryRegions() {
	// Timbre max tables are slightly more complicated than the others, which are used directly from the ROM.
	// The ROM (sensibly) just has maximums for TimbreParam.commonParam followed by just one TimbreParam.partialParam,
	// so we produce a table with all partialParams filled out, as well as padding for PaddedTimbre, for quick lookup.
	paddedTimbreMaxTable = new Bit8u[sizeof(MemParams::PaddedTimbre)];
	memcpy(&paddedTimbreMaxTable[0], &controlROMData[controlROMMap->timbreMaxTable], sizeof(TimbreParam::CommonParam) + sizeof(TimbreParam::PartialParam)); // commonParam and one partialParam
	int pos = sizeof(TimbreParam::CommonParam) + sizeof(TimbreParam::PartialParam);
	for (int i = 0; i < 3; i++) {
		memcpy(&paddedTimbreMaxTable[pos], &controlROMData[controlROMMap->timbreMaxTable + sizeof(TimbreParam::CommonParam)], sizeof(TimbreParam::PartialParam));
		pos += sizeof(TimbreParam::PartialParam);
	}
	memset(&paddedTimbreMaxTable[pos], 0, 10); // Padding
	patchTempMemoryRegion = new PatchTempMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.patchTemp[0]), &controlROMData[controlROMMap->patchMaxTable]);
	rhythmTempMemoryRegion = new RhythmTempMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.rhythmTemp[0]), &controlROMData[controlROMMap->rhythmMaxTable]);
	timbreTempMemoryRegion = new TimbreTempMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.timbreTemp[0]), paddedTimbreMaxTable);
	patchesMemoryRegion = new PatchesMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.patches[0]), &controlROMData[controlROMMap->patchMaxTable]);
	timbresMemoryRegion = new TimbresMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.timbres[0]), paddedTimbreMaxTable);
	systemMemoryRegion = new SystemMemoryRegion(this, reinterpret_cast<Bit8u *>(&mt32ram.system), &controlROMData[controlROMMap->systemMaxTable]);
	displayMemoryRegion = new DisplayMemoryRegion(this);
	resetMemoryRegion = new ResetMemoryRegion(this);
}

void Synth::deleteMemoryRegions() {
	delete patchTempMemoryRegion;
	patchTempMemoryRegion = NULL;
	delete rhythmTempMemoryRegion;
	rhythmTempMemoryRegion = NULL;
	delete timbreTempMemoryRegion;
	timbreTempMemoryRegion = NULL;
	delete patchesMemoryRegion;
	patchesMemoryRegion = NULL;
	delete timbresMemoryRegion;
	timbresMemoryRegion = NULL;
	delete systemMemoryRegion;
	systemMemoryRegion = NULL;
	delete displayMemoryRegion;
	displayMemoryRegion = NULL;
	delete resetMemoryRegion;
	resetMemoryRegion = NULL;

	delete[] paddedTimbreMaxTable;
	paddedTimbreMaxTable = NULL;
}

MemoryRegion *Synth::findMemoryRegion(Bit32u addr) {
	MemoryRegion *regions[] = {
		patchTempMemoryRegion,
		rhythmTempMemoryRegion,
		timbreTempMemoryRegion,
		patchesMemoryRegion,
		timbresMemoryRegion,
		systemMemoryRegion,
		displayMemoryRegion,
		resetMemoryRegion,
		NULL
	};
	for (int pos = 0; regions[pos] != NULL; pos++) {
		if (regions[pos]->contains(addr)) {
			return regions[pos];
		}
	}
	return NULL;
}

void Synth::readMemoryRegion(const MemoryRegion *region, Bit32u addr, Bit32u len, Bit8u *data) {
	unsigned int first = region->firstTouched(addr);
	//unsigned int last = region->lastTouched(addr, len);
	unsigned int off = region->firstTouchedOffset(addr);
	len = region->getClampedLen(addr, len);

	unsigned int m;

	if (region->isReadable()) {
		region->read(first, off, data, len);
	} else {
		// FIXME: We might want to do these properly in future
		for (m = 0; m < len; m += 2) {
			data[m] = 0xff;
			if (m + 1 < len) {
				data[m+1] = Bit8u(region->type);
			}
		}
	}
}

void Synth::writeMemoryRegion(const MemoryRegion *region, Bit32u addr, Bit32u len, const Bit8u *data) {
	unsigned int first = region->firstTouched(addr);
	unsigned int last = region->lastTouched(addr, len);
	unsigned int off = region->firstTouchedOffset(addr);
	switch (region->type) {
	case MR_PatchTemp:
		region->write(first, off, data, len);
		//printDebug("Patch temp: Patch %d, offset %x, len %d", off/16, off % 16, len);

		for (unsigned int i = first; i <= last; i++) {
			int absTimbreNum = mt32ram.patchTemp[i].patch.timbreGroup * 64 + mt32ram.patchTemp[i].patch.timbreNum;
			char timbreName[11];
			memcpy(timbreName, mt32ram.timbres[absTimbreNum].timbre.common.name, 10);
			timbreName[10] = 0;
#if MT32EMU_MONITOR_SYSEX > 0
			printDebug("WRITE-PARTPATCH (%d-%d@%d..%d): %d; timbre=%d (%s), outlevel=%d", first, last, off, off + len, i, absTimbreNum, timbreName, mt32ram.patchTemp[i].outputLevel);
#endif
			if (parts[i] != NULL) {
				if (i != 8) {
					// Note: Confirmed on CM-64 that we definitely *should* update the timbre here,
					// but only in the case that the sysex actually writes to those values
					if (i == first && off > 2) {
#if MT32EMU_MONITOR_SYSEX > 0
						printDebug(" (Not updating timbre, since those values weren't touched)");
#endif
					} else {
						parts[i]->setTimbre(&mt32ram.timbres[parts[i]->getAbsTimbreNum()].timbre);
					}
				}
				parts[i]->refresh();
			}
		}
		break;
	case MR_RhythmTemp:
		region->write(first, off, data, len);
		for (unsigned int i = first; i <= last; i++) {
			int timbreNum = mt32ram.rhythmTemp[i].timbre;
			char timbreName[11];
			if (timbreNum < 94) {
				memcpy(timbreName, mt32ram.timbres[128 + timbreNum].timbre.common.name, 10);
				timbreName[10] = 0;
			} else {
				strcpy(timbreName, "[None]");
			}
#if MT32EMU_MONITOR_SYSEX > 0
			printDebug("WRITE-RHYTHM (%d-%d@%d..%d): %d; level=%02x, panpot=%02x, reverb=%02x, timbre=%d (%s)", first, last, off, off + len, i, mt32ram.rhythmTemp[i].outputLevel, mt32ram.rhythmTemp[i].panpot, mt32ram.rhythmTemp[i].reverbSwitch, mt32ram.rhythmTemp[i].timbre, timbreName);
#endif
		}
		if (parts[8] != NULL) {
			parts[8]->refresh();
		}
		break;
	case MR_TimbreTemp:
		region->write(first, off, data, len);
		for (unsigned int i = first; i <= last; i++) {
			char instrumentName[11];
			memcpy(instrumentName, mt32ram.timbreTemp[i].common.name, 10);
			instrumentName[10] = 0;
#if MT32EMU_MONITOR_SYSEX > 0
			printDebug("WRITE-PARTTIMBRE (%d-%d@%d..%d): timbre=%d (%s)", first, last, off, off + len, i, instrumentName);
#endif
			if (parts[i] != NULL) {
				parts[i]->refresh();
			}
		}
		break;
	case MR_Patches:
		region->write(first, off, data, len);
#if MT32EMU_MONITOR_SYSEX > 0
		for (unsigned int i = first; i <= last; i++) {
			PatchParam *patch = &mt32ram.patches[i];
			int patchAbsTimbreNum = patch->timbreGroup * 64 + patch->timbreNum;
			char instrumentName[11];
			memcpy(instrumentName, mt32ram.timbres[patchAbsTimbreNum].timbre.common.name, 10);
			instrumentName[10] = 0;
			Bit8u *n = reinterpret_cast<Bit8u *>(patch);
			printDebug("WRITE-PATCH (%d-%d@%d..%d): %d; timbre=%d (%s) %02X%02X%02X%02X%02X%02X%02X%02X", first, last, off, off + len, i, patchAbsTimbreNum, instrumentName, n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7]);
		}
#endif
		break;
	case MR_Timbres:
		// Timbres
		first += 128;
		last += 128;
		region->write(first, off, data, len);
		for (unsigned int i = first; i <= last; i++) {
#if MT32EMU_MONITOR_TIMBRES >= 1
			TimbreParam *timbre = &mt32ram.timbres[i].timbre;
			char instrumentName[11];
			memcpy(instrumentName, timbre->common.name, 10);
			instrumentName[10] = 0;
			printDebug("WRITE-TIMBRE (%d-%d@%d..%d): %d; name=\"%s\"", first, last, off, off + len, i, instrumentName);
#if MT32EMU_MONITOR_TIMBRES >= 2
#define DT(x) printDebug(" " #x ": %d", timbre->x)
			DT(common.partialStructure12);
			DT(common.partialStructure34);
			DT(common.partialMute);
			DT(common.noSustain);

#define DTP(x) \
			DT(partial[x].wg.pitchCoarse); \
			DT(partial[x].wg.pitchFine); \
			DT(partial[x].wg.pitchKeyfollow); \
			DT(partial[x].wg.pitchBenderEnabled); \
			DT(partial[x].wg.waveform); \
			DT(partial[x].wg.pcmWave); \
			DT(partial[x].wg.pulseWidth); \
			DT(partial[x].wg.pulseWidthVeloSensitivity); \
			DT(partial[x].pitchEnv.depth); \
			DT(partial[x].pitchEnv.veloSensitivity); \
			DT(partial[x].pitchEnv.timeKeyfollow); \
			DT(partial[x].pitchEnv.time[0]); \
			DT(partial[x].pitchEnv.time[1]); \
			DT(partial[x].pitchEnv.time[2]); \
			DT(partial[x].pitchEnv.time[3]); \
			DT(partial[x].pitchEnv.level[0]); \
			DT(partial[x].pitchEnv.level[1]); \
			DT(partial[x].pitchEnv.level[2]); \
			DT(partial[x].pitchEnv.level[3]); \
			DT(partial[x].pitchEnv.level[4]); \
			DT(partial[x].pitchLFO.rate); \
			DT(partial[x].pitchLFO.depth); \
			DT(partial[x].pitchLFO.modSensitivity); \
			DT(partial[x].tvf.cutoff); \
			DT(partial[x].tvf.resonance); \
			DT(partial[x].tvf.keyfollow); \
			DT(partial[x].tvf.biasPoint); \
			DT(partial[x].tvf.biasLevel); \
			DT(partial[x].tvf.envDepth); \
			DT(partial[x].tvf.envVeloSensitivity); \
			DT(partial[x].tvf.envDepthKeyfollow); \
			DT(partial[x].tvf.envTimeKeyfollow); \
			DT(partial[x].tvf.envTime[0]); \
			DT(partial[x].tvf.envTime[1]); \
			DT(partial[x].tvf.envTime[2]); \
			DT(partial[x].tvf.envTime[3]); \
			DT(partial[x].tvf.envTime[4]); \
			DT(partial[x].tvf.envLevel[0]); \
			DT(partial[x].tvf.envLevel[1]); \
			DT(partial[x].tvf.envLevel[2]); \
			DT(partial[x].tvf.envLevel[3]); \
			DT(partial[x].tva.level); \
			DT(partial[x].tva.veloSensitivity); \
			DT(partial[x].tva.biasPoint1); \
			DT(partial[x].tva.biasLevel1); \
			DT(partial[x].tva.biasPoint2); \
			DT(partial[x].tva.biasLevel2); \
			DT(partial[x].tva.envTimeKeyfollow); \
			DT(partial[x].tva.envTimeVeloSensitivity); \
			DT(partial[x].tva.envTime[0]); \
			DT(partial[x].tva.envTime[1]); \
			DT(partial[x].tva.envTime[2]); \
			DT(partial[x].tva.envTime[3]); \
			DT(partial[x].tva.envTime[4]); \
			DT(partial[x].tva.envLevel[0]); \
			DT(partial[x].tva.envLevel[1]); \
			DT(partial[x].tva.envLevel[2]); \
			DT(partial[x].tva.envLevel[3]);

			DTP(0);
			DTP(1);
			DTP(2);
			DTP(3);
#undef DTP
#undef DT
#endif
#endif
			// FIXME:KG: Not sure if the stuff below should be done (for rhythm and/or parts)...
			// Does the real MT-32 automatically do this?
			for (unsigned int part = 0; part < 9; part++) {
				if (parts[part] != NULL) {
					parts[part]->refreshTimbre(i);
				}
			}
		}
		break;
	case MR_System:
		region->write(0, off, data, len);

		reportHandler->onDeviceReconfig();
		// FIXME: We haven't properly confirmed any of this behaviour
		// In particular, we tend to reset things such as reverb even if the write contained
		// the same parameters as were already set, which may be wrong.
		// On the other hand, the real thing could be resetting things even when they aren't touched
		// by the write at all.
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("WRITE-SYSTEM:");
#endif
		if (off <= SYSTEM_MASTER_TUNE_OFF && off + len > SYSTEM_MASTER_TUNE_OFF) {
			refreshSystemMasterTune();
		}
		if (off <= SYSTEM_REVERB_LEVEL_OFF && off + len > SYSTEM_REVERB_MODE_OFF) {
			refreshSystemReverbParameters();
		}
		if (off <= SYSTEM_RESERVE_SETTINGS_END_OFF && off + len > SYSTEM_RESERVE_SETTINGS_START_OFF) {
			refreshSystemReserveSettings();
		}
		if (off <= SYSTEM_CHAN_ASSIGN_END_OFF && off + len > SYSTEM_CHAN_ASSIGN_START_OFF) {
			int firstPart = off - SYSTEM_CHAN_ASSIGN_START_OFF;
			if(firstPart < 0)
				firstPart = 0;
			int lastPart = off + len - SYSTEM_CHAN_ASSIGN_START_OFF;
			if(lastPart > 8)
				lastPart = 8;
			refreshSystemChanAssign(Bit8u(firstPart), Bit8u(lastPart));
		}
		if (off <= SYSTEM_MASTER_VOL_OFF && off + len > SYSTEM_MASTER_VOL_OFF) {
			refreshSystemMasterVol();
		}
		break;
	case MR_Display:
		if (len > Display::LCD_TEXT_SIZE) len = Display::LCD_TEXT_SIZE;
		if (!extensions.display->customDisplayMessageReceived(data, off, len)) break;
		// Holds zero-terminated string of the maximum length.
		char buf[Display::LCD_TEXT_SIZE + 1];
		memcpy(&buf, &data[0], len);
		buf[len] = 0;
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("WRITE-LCD: %s", buf);
#endif
		reportHandler->showLCDMessage(buf);
		break;
	case MR_Reset:
		reset();
		break;
	default:
		break;
	}
}

void Synth::refreshSystemMasterTune() {
	// 171 is ~half a semitone.
	extensions.masterTunePitchDelta = ((mt32ram.system.masterTune - 64) * 171) >> 6; // PORTABILITY NOTE: Assumes arithmetic shift.
#if MT32EMU_MONITOR_SYSEX > 0
	//FIXME:KG: This is just an educated guess.
	// The LAPC-I documentation claims a range of 427.5Hz-452.6Hz (similar to what we have here)
	// The MT-32 documentation claims a range of 432.1Hz-457.6Hz
	float masterTune = 440.0f * EXP2F((mt32ram.system.masterTune - 64.0f) / (128.0f * 12.0f));
	printDebug(" Master Tune: %f", masterTune);
#endif
}

void Synth::refreshSystemReverbParameters() {
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug(" Reverb: mode=%d, time=%d, level=%d", mt32ram.system.reverbMode, mt32ram.system.reverbTime, mt32ram.system.reverbLevel);
#endif
	if (reverbOverridden) {
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug(" (Reverb overridden - ignoring)");
#endif
		return;
	}
	reportHandler->onNewReverbMode(mt32ram.system.reverbMode);
	reportHandler->onNewReverbTime(mt32ram.system.reverbTime);
	reportHandler->onNewReverbLevel(mt32ram.system.reverbLevel);

	BReverbModel *oldReverbModel = reverbModel;
	if (mt32ram.system.reverbTime == 0 && mt32ram.system.reverbLevel == 0) {
		// Setting both time and level to 0 effectively disables wet reverb output on real devices.
		// Take a shortcut in this case to reduce CPU load.
		reverbModel = NULL;
	} else {
		reverbModel = reverbModels[mt32ram.system.reverbMode];
	}
	if (reverbModel != oldReverbModel) {
		if (extensions.preallocatedReverbMemory) {
			if (isReverbEnabled()) {
				reverbModel->mute();
			}
		} else {
			if (oldReverbModel != NULL) {
				oldReverbModel->close();
			}
			if (isReverbEnabled()) {
				reverbModel->open();
			}
		}
	}
	if (isReverbEnabled()) {
		reverbModel->setParameters(mt32ram.system.reverbTime, mt32ram.system.reverbLevel);
	}
}

void Synth::refreshSystemReserveSettings() {
	Bit8u *rset = mt32ram.system.reserveSettings;
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug(" Partial reserve: 1=%02d 2=%02d 3=%02d 4=%02d 5=%02d 6=%02d 7=%02d 8=%02d Rhythm=%02d", rset[0], rset[1], rset[2], rset[3], rset[4], rset[5], rset[6], rset[7], rset[8]);
#endif
	partialManager->setReserve(rset);
}

void Synth::refreshSystemChanAssign(Bit8u firstPart, Bit8u lastPart) {
	memset(extensions.chantable, 0xFF, sizeof(extensions.chantable));

	// CONFIRMED: In the case of assigning a MIDI channel to multiple parts,
	//            the messages received on that MIDI channel are handled by all the parts.
	for (Bit32u i = 0; i <= 8; i++) {
		if (parts[i] != NULL && i >= firstPart && i <= lastPart) {
			// CONFIRMED: Decay is started for all polys, and all controllers are reset, for every part whose assignment was touched by the sysex write.
			parts[i]->allSoundOff();
			parts[i]->resetAllControllers();
		}
		Bit8u chan = mt32ram.system.chanAssign[i];
		if (chan > 15) continue;
		Bit8u *chanParts = extensions.chantable[chan];
		for (Bit32u j = 0; j <= 8; j++) {
			if (chanParts[j] > 8) {
				chanParts[j] = Bit8u(i);
				break;
			}
		}
	}

#if MT32EMU_MONITOR_SYSEX > 0
	Bit8u *rset = mt32ram.system.chanAssign;
	printDebug(" Part assign:     1=%02d 2=%02d 3=%02d 4=%02d 5=%02d 6=%02d 7=%02d 8=%02d Rhythm=%02d", rset[0], rset[1], rset[2], rset[3], rset[4], rset[5], rset[6], rset[7], rset[8]);
#endif
}

void Synth::refreshSystemMasterVol() {
	// Note, this should only occur when the user turns the volume knob. When the master volume is set via a SysEx, display
	// doesn't actually update on all real devices. However, we anyway update the display, as we don't foresee a dedicated
	// API for setting the master volume yet it's rather dubious that one really needs this quirk to be fairly emulated.
	if (opened) extensions.display->masterVolumeChanged();
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug(" Master volume: %d", mt32ram.system.masterVol);
#endif
}

void Synth::refreshSystem() {
	refreshSystemMasterTune();
	refreshSystemReverbParameters();
	refreshSystemReserveSettings();
	refreshSystemChanAssign(0, 8);
	refreshSystemMasterVol();
}

void Synth::reset() {
	if (!opened) return;
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug("RESET");
#endif
	reportHandler->onDeviceReset();
	partialManager->deactivateAll();
	mt32ram = mt32default;
	for (int i = 0; i < 9; i++) {
		parts[i]->reset();
		if (i != 8) {
			parts[i]->setProgram(controlROMData[controlROMMap->programSettings + i]);
		} else {
			parts[8]->refresh();
		}
	}
	refreshSystem();
	resetMasterTunePitchDelta();
	isActive();
}

void Synth::resetMasterTunePitchDelta() {
	// This effectively resets master tune to 440.0Hz.
	// Despite that the manual claims 442.0Hz is the default setting for master tune,
	// it doesn't actually take effect upon a reset due to a bug in the reset routine.
	// CONFIRMED: This bug is present in all supported Control ROMs.
	extensions.masterTunePitchDelta = 0;
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug(" Actual Master Tune reset to 440.0");
#endif
}

Bit32s Synth::getMasterTunePitchDelta() const {
	return extensions.masterTunePitchDelta;
}

bool Synth::getDisplayState(char *targetBuffer, bool narrowLCD) const {
	if (!opened) {
		memset(targetBuffer, ' ', Display::LCD_TEXT_SIZE);
		targetBuffer[Display::LCD_TEXT_SIZE] = 0;
		return false;
	}
	return extensions.display->getDisplayState(targetBuffer, narrowLCD);
}

void Synth::setMainDisplayMode() {
	if (opened) extensions.display->setMainDisplayMode();
}


void Synth::setDisplayCompatibility(bool oldMT32CompatibilityEnabled) {
	extensions.oldMT32DisplayFeatures = oldMT32CompatibilityEnabled;
}

bool Synth::isDisplayOldMT32Compatible() const {
	return extensions.oldMT32DisplayFeatures;
}

bool Synth::isDefaultDisplayOldMT32Compatible() const {
	return opened && controlROMFeatures->oldMT32DisplayFeatures;
}

/** Defines an interface of a class that maintains storage of variable-sized data of SysEx messages. */
class MidiEventQueue::SysexDataStorage {
public:
	static MidiEventQueue::SysexDataStorage *create(Bit32u storageBufferSize);

	virtual ~SysexDataStorage() {}
	virtual Bit8u *allocate(Bit32u sysexLength) = 0;
	virtual void reclaimUnused(const Bit8u *sysexData, Bit32u sysexLength) = 0;
	virtual void dispose(const Bit8u *sysexData, Bit32u sysexLength) = 0;
};

/** Storage space for SysEx data is allocated dynamically on demand and is disposed lazily. */
class DynamicSysexDataStorage : public MidiEventQueue::SysexDataStorage {
public:
	Bit8u *allocate(Bit32u sysexLength) {
		return new Bit8u[sysexLength];
	}

	void reclaimUnused(const Bit8u *, Bit32u) {}

	void dispose(const Bit8u *sysexData, Bit32u) {
		delete[] sysexData;
	}
};

/**
 * SysEx data is stored in a preallocated buffer, that makes this kind of storage safe
 * for use in a realtime thread. Additionally, the space retained by a SysEx event,
 * that has been processed and thus is no longer necessary, is disposed instantly.
 */
class BufferedSysexDataStorage : public MidiEventQueue::SysexDataStorage {
public:
	explicit BufferedSysexDataStorage(Bit32u useStorageBufferSize) :
		storageBuffer(new Bit8u[useStorageBufferSize]),
		storageBufferSize(useStorageBufferSize),
		startPosition(),
		endPosition()
	{}

	~BufferedSysexDataStorage() {
		delete[] storageBuffer;
	}

	Bit8u *allocate(Bit32u sysexLength) {
		Bit32u myStartPosition = startPosition;
		Bit32u myEndPosition = endPosition;

		// When the free space isn't contiguous, the data is allocated either right after the end position
		// or at the buffer beginning, wherever it fits.
		if (myStartPosition > myEndPosition) {
			if (myStartPosition - myEndPosition <= sysexLength) return NULL;
		} else if (storageBufferSize - myEndPosition < sysexLength) {
			// There's not enough free space at the end to place the data block.
			if (myStartPosition == myEndPosition) {
				// The buffer is empty -> reset positions to the buffer beginning.
				if (storageBufferSize <= sysexLength) return NULL;
				if (myStartPosition != 0) {
					myStartPosition = 0;
					// It's OK to write startPosition here non-atomically. We don't expect any
					// concurrent reads, as there must be no SysEx messages in the queue.
					startPosition = myStartPosition;
				}
			} else if (myStartPosition <= sysexLength) return NULL;
			myEndPosition = 0;
		}
		endPosition = myEndPosition + sysexLength;
		return storageBuffer + myEndPosition;
	}

	void reclaimUnused(const Bit8u *sysexData, Bit32u sysexLength) {
		if (sysexData == NULL) return;
		Bit32u allocatedPosition = startPosition;
		if (storageBuffer + allocatedPosition == sysexData) {
			startPosition = allocatedPosition + sysexLength;
		} else if (storageBuffer == sysexData) {
			// Buffer wrapped around.
			startPosition = sysexLength;
		}
	}

	void dispose(const Bit8u *, Bit32u) {}

private:
	Bit8u * const storageBuffer;
	const Bit32u storageBufferSize;

	volatile Bit32u startPosition;
	volatile Bit32u endPosition;
};

MidiEventQueue::SysexDataStorage *MidiEventQueue::SysexDataStorage::create(Bit32u storageBufferSize) {
	if (storageBufferSize > 0) {
		return new BufferedSysexDataStorage(storageBufferSize);
	} else {
		return new DynamicSysexDataStorage;
	}
}

MidiEventQueue::MidiEventQueue(Bit32u useRingBufferSize, Bit32u storageBufferSize) :
	sysexDataStorage(*SysexDataStorage::create(storageBufferSize)),
	ringBuffer(new MidiEvent[useRingBufferSize]), ringBufferMask(useRingBufferSize - 1)
{
	for (Bit32u i = 0; i <= ringBufferMask; i++) {
		ringBuffer[i].sysexData = NULL;
	}
	reset();
}

MidiEventQueue::~MidiEventQueue() {
	for (Bit32u i = 0; i <= ringBufferMask; i++) {
		volatile MidiEvent &currentEvent = ringBuffer[i];
		sysexDataStorage.dispose(currentEvent.sysexData, currentEvent.sysexLength);
	}
	delete &sysexDataStorage;
	delete[] ringBuffer;
}

void MidiEventQueue::reset() {
	startPosition = 0;
	endPosition = 0;
}

bool MidiEventQueue::pushShortMessage(Bit32u shortMessageData, Bit32u timestamp) {
	Bit32u newEndPosition = (endPosition + 1) & ringBufferMask;
	// If ring buffer is full, bail out.
	if (startPosition == newEndPosition) return false;
	volatile MidiEvent &newEvent = ringBuffer[endPosition];
	sysexDataStorage.dispose(newEvent.sysexData, newEvent.sysexLength);
	newEvent.sysexData = NULL;
	newEvent.shortMessageData = shortMessageData;
	newEvent.timestamp = timestamp;
	endPosition = newEndPosition;
	return true;
}

bool MidiEventQueue::pushSysex(const Bit8u *sysexData, Bit32u sysexLength, Bit32u timestamp) {
	Bit32u newEndPosition = (endPosition + 1) & ringBufferMask;
	// If ring buffer is full, bail out.
	if (startPosition == newEndPosition) return false;
	volatile MidiEvent &newEvent = ringBuffer[endPosition];
	sysexDataStorage.dispose(newEvent.sysexData, newEvent.sysexLength);
	Bit8u *dstSysexData = sysexDataStorage.allocate(sysexLength);
	if (dstSysexData == NULL) return false;
	memcpy(dstSysexData, sysexData, sysexLength);
	newEvent.sysexData = dstSysexData;
	newEvent.sysexLength = sysexLength;
	newEvent.timestamp = timestamp;
	endPosition = newEndPosition;
	return true;
}

const volatile MidiEventQueue::MidiEvent *MidiEventQueue::peekMidiEvent() {
	return isEmpty() ? NULL : &ringBuffer[startPosition];
}

void MidiEventQueue::dropMidiEvent() {
	if (isEmpty()) return;
	volatile MidiEvent &unusedEvent = ringBuffer[startPosition];
	sysexDataStorage.reclaimUnused(unusedEvent.sysexData, unusedEvent.sysexLength);
	startPosition = (startPosition + 1) & ringBufferMask;
}

bool MidiEventQueue::isEmpty() const {
	return startPosition == endPosition;
}

void Synth::selectRendererType(RendererType newRendererType) {
	extensions.selectedRendererType = newRendererType;
}

RendererType Synth::getSelectedRendererType() const {
	return extensions.selectedRendererType;
}

Bit32u Synth::getStereoOutputSampleRate() const {
	return (analog == NULL) ? SAMPLE_RATE : analog->getOutputSampleRate();
}

void Renderer::updateDisplayState() {
	bool midiMessageLEDState;
	bool midiMessageLEDStateUpdated;
	bool lcdUpdated;
	synth.extensions.display->checkDisplayStateUpdated(midiMessageLEDState, midiMessageLEDStateUpdated, lcdUpdated);
	if (midiMessageLEDStateUpdated) synth.extensions.reportHandler2->onMidiMessageLEDStateUpdated(midiMessageLEDState);
	if (lcdUpdated) synth.extensions.reportHandler2->onLCDStateUpdated();
}

template <class Sample>
void RendererImpl<Sample>::doRender(Sample *stereoStream, Bit32u len) {
	if (!isActivated()) {
		incRenderedSampleCount(getAnalog().getDACStreamsLength(len));
		if (!getAnalog().process(NULL, NULL, NULL, NULL, NULL, NULL, stereoStream, len)) {
			printDebug("RendererImpl: Invalid call to Analog::process()!\n");
		}
		Synth::muteSampleBuffer(stereoStream, len << 1);
		updateDisplayState();
		return;
	}

	while (len > 0) {
		// As in AnalogOutputMode_ACCURATE mode output is upsampled, MAX_SAMPLES_PER_RUN is more than enough for the temp buffers.
		Bit32u thisPassLen = len > MAX_SAMPLES_PER_RUN ? MAX_SAMPLES_PER_RUN : len;
		doRenderStreams(tmpBuffers, getAnalog().getDACStreamsLength(thisPassLen));
		if (!getAnalog().process(stereoStream, tmpNonReverbLeft, tmpNonReverbRight, tmpReverbDryLeft, tmpReverbDryRight, tmpReverbWetLeft, tmpReverbWetRight, thisPassLen)) {
			printDebug("RendererImpl: Invalid call to Analog::process()!\n");
			Synth::muteSampleBuffer(stereoStream, len << 1);
			return;
		}
		stereoStream += thisPassLen << 1;
		len -= thisPassLen;
	}
}

template <class Sample>
template <class O>
void RendererImpl<Sample>::doRenderAndConvert(O *stereoStream, Bit32u len) {
	Sample renderingBuffer[MAX_SAMPLES_PER_RUN << 1];
	while (len > 0) {
		Bit32u thisPassLen = len > MAX_SAMPLES_PER_RUN ? MAX_SAMPLES_PER_RUN : len;
		doRender(renderingBuffer, thisPassLen);
		convertSampleFormat(renderingBuffer, stereoStream, thisPassLen << 1);
		stereoStream += thisPassLen << 1;
		len -= thisPassLen;
	}
}

template<>
void RendererImpl<IntSample>::render(IntSample *stereoStream, Bit32u len) {
	doRender(stereoStream, len);
}

template<>
void RendererImpl<IntSample>::render(FloatSample *stereoStream, Bit32u len) {
	doRenderAndConvert(stereoStream, len);
}

template<>
void RendererImpl<FloatSample>::render(IntSample *stereoStream, Bit32u len) {
	doRenderAndConvert(stereoStream, len);
}

template<>
void RendererImpl<FloatSample>::render(FloatSample *stereoStream, Bit32u len) {
	doRender(stereoStream, len);
}

template <class S>
static inline void renderStereo(bool opened, Renderer *renderer, S *stream, Bit32u len) {
	if (opened) {
		renderer->render(stream, len);
	} else {
		Synth::muteSampleBuffer(stream, len << 1);
	}
}

void Synth::render(Bit16s *stream, Bit32u len) {
	renderStereo(opened, renderer, stream, len);
}

void Synth::render(float *stream, Bit32u len) {
	renderStereo(opened, renderer, stream, len);
}

template <class Sample>
static inline void advanceStream(Sample *&stream, Bit32u len) {
	if (stream != NULL) {
		stream += len;
	}
}

template <class Sample>
static inline void advanceStreams(DACOutputStreams<Sample> &streams, Bit32u len) {
	advanceStream(streams.nonReverbLeft, len);
	advanceStream(streams.nonReverbRight, len);
	advanceStream(streams.reverbDryLeft, len);
	advanceStream(streams.reverbDryRight, len);
	advanceStream(streams.reverbWetLeft, len);
	advanceStream(streams.reverbWetRight, len);
}

template <class Sample>
static inline void muteStreams(const DACOutputStreams<Sample> &streams, Bit32u len) {
	Synth::muteSampleBuffer(streams.nonReverbLeft, len);
	Synth::muteSampleBuffer(streams.nonReverbRight, len);
	Synth::muteSampleBuffer(streams.reverbDryLeft, len);
	Synth::muteSampleBuffer(streams.reverbDryRight, len);
	Synth::muteSampleBuffer(streams.reverbWetLeft, len);
	Synth::muteSampleBuffer(streams.reverbWetRight, len);
}

template <class I, class O>
static inline void convertStreamsFormat(const DACOutputStreams<I> &inStreams, const DACOutputStreams<O> &outStreams, Bit32u len) {
	convertSampleFormat(inStreams.nonReverbLeft, outStreams.nonReverbLeft, len);
	convertSampleFormat(inStreams.nonReverbRight, outStreams.nonReverbRight, len);
	convertSampleFormat(inStreams.reverbDryLeft, outStreams.reverbDryLeft, len);
	convertSampleFormat(inStreams.reverbDryRight, outStreams.reverbDryRight, len);
	convertSampleFormat(inStreams.reverbWetLeft, outStreams.reverbWetLeft, len);
	convertSampleFormat(inStreams.reverbWetRight, outStreams.reverbWetRight, len);
}

template <class Sample>
void RendererImpl<Sample>::doRenderStreams(const DACOutputStreams<Sample> &streams, Bit32u len)
{
	DACOutputStreams<Sample> tmpStreams = streams;
	while (len > 0) {
		// We need to ensure zero-duration notes will play so add minimum 1-sample delay.
		Bit32u thisLen = 1;
		if (!isAbortingPoly()) {
			const volatile MidiEventQueue::MidiEvent *nextEvent = getMidiQueue().peekMidiEvent();
			Bit32s samplesToNextEvent = (nextEvent != NULL) ? Bit32s(nextEvent->timestamp - getRenderedSampleCount()) : MAX_SAMPLES_PER_RUN;
			if (samplesToNextEvent > 0) {
				thisLen = len > MAX_SAMPLES_PER_RUN ? MAX_SAMPLES_PER_RUN : len;
				if (thisLen > Bit32u(samplesToNextEvent)) {
					thisLen = samplesToNextEvent;
				}
			} else {
				if (nextEvent->sysexData == NULL) {
					synth.playMsgNow(nextEvent->shortMessageData);
					// If a poly is aborting we don't drop the event from the queue.
					// Instead, we'll return to it again when the abortion is done.
					if (!isAbortingPoly()) {
						getMidiQueue().dropMidiEvent();
					}
				} else {
					synth.playSysexNow(nextEvent->sysexData, nextEvent->sysexLength);
					getMidiQueue().dropMidiEvent();
				}
			}
		}
		produceStreams(tmpStreams, thisLen);
		advanceStreams(tmpStreams, thisLen);
		len -= thisLen;
	}
}

template <class Sample>
template <class O>
void RendererImpl<Sample>::doRenderAndConvertStreams(const DACOutputStreams<O> &streams, Bit32u len) {
	Sample cnvNonReverbLeft[MAX_SAMPLES_PER_RUN], cnvNonReverbRight[MAX_SAMPLES_PER_RUN];
	Sample cnvReverbDryLeft[MAX_SAMPLES_PER_RUN], cnvReverbDryRight[MAX_SAMPLES_PER_RUN];
	Sample cnvReverbWetLeft[MAX_SAMPLES_PER_RUN], cnvReverbWetRight[MAX_SAMPLES_PER_RUN];

	const DACOutputStreams<Sample> cnvStreams = {
		cnvNonReverbLeft, cnvNonReverbRight,
		cnvReverbDryLeft, cnvReverbDryRight,
		cnvReverbWetLeft, cnvReverbWetRight
	};

	DACOutputStreams<O> tmpStreams = streams;

	while (len > 0) {
		Bit32u thisPassLen = len > MAX_SAMPLES_PER_RUN ? MAX_SAMPLES_PER_RUN : len;
		doRenderStreams(cnvStreams, thisPassLen);
		convertStreamsFormat(cnvStreams, tmpStreams, thisPassLen);
		advanceStreams(tmpStreams, thisPassLen);
		len -= thisPassLen;
	}
}

template<>
void RendererImpl<IntSample>::renderStreams(const DACOutputStreams<IntSample> &streams, Bit32u len) {
	doRenderStreams(streams, len);
}

template<>
void RendererImpl<IntSample>::renderStreams(const DACOutputStreams<FloatSample> &streams, Bit32u len) {
	doRenderAndConvertStreams(streams, len);
}

template<>
void RendererImpl<FloatSample>::renderStreams(const DACOutputStreams<IntSample> &streams, Bit32u len) {
	doRenderAndConvertStreams(streams, len);
}

template<>
void RendererImpl<FloatSample>::renderStreams(const DACOutputStreams<FloatSample> &streams, Bit32u len) {
	doRenderStreams(streams, len);
}

template <class S>
static inline void renderStreams(bool opened, Renderer *renderer, const DACOutputStreams<S> &streams, Bit32u len) {
	if (opened) {
		renderer->renderStreams(streams, len);
	} else {
		muteStreams(streams, len);
	}
}

void Synth::renderStreams(const DACOutputStreams<Bit16s> &streams, Bit32u len) {
	MT32Emu::renderStreams(opened, renderer, streams, len);
}

void Synth::renderStreams(const DACOutputStreams<float> &streams, Bit32u len) {
	MT32Emu::renderStreams(opened, renderer, streams, len);
}

void Synth::renderStreams(
	Bit16s *nonReverbLeft, Bit16s *nonReverbRight,
	Bit16s *reverbDryLeft, Bit16s *reverbDryRight,
	Bit16s *reverbWetLeft, Bit16s *reverbWetRight,
	Bit32u len)
{
	DACOutputStreams<IntSample> streams = {
		nonReverbLeft, nonReverbRight,
		reverbDryLeft, reverbDryRight,
		reverbWetLeft, reverbWetRight
	};
	renderStreams(streams, len);
}

void Synth::renderStreams(
	float *nonReverbLeft, float *nonReverbRight,
	float *reverbDryLeft, float *reverbDryRight,
	float *reverbWetLeft, float *reverbWetRight,
	Bit32u len)
{
	DACOutputStreams<FloatSample> streams = {
		nonReverbLeft, nonReverbRight,
		reverbDryLeft, reverbDryRight,
		reverbWetLeft, reverbWetRight
	};
	renderStreams(streams, len);
}

// In GENERATION2 units, the output from LA32 goes to the Boss chip already bit-shifted.
// In NICE mode, it's also better to increase volume before the reverb processing to preserve accuracy.
template <>
void RendererImpl<IntSample>::produceLA32Output(IntSample *buffer, Bit32u len) {
	switch (synth.getDACInputMode()) {
		case DACInputMode_GENERATION2:
			while (len--) {
				*buffer = (*buffer & 0x8000) | ((*buffer << 1) & 0x7FFE) | ((*buffer >> 14) & 0x0001);
				++buffer;
			}
			break;
		case DACInputMode_NICE:
			while (len--) {
				*buffer = Synth::clipSampleEx(IntSampleEx(*buffer) << 1);
				++buffer;
			}
			break;
		default:
			break;
	}
}

template <>
void RendererImpl<IntSample>::convertSamplesToOutput(IntSample *buffer, Bit32u len) {
	if (synth.getDACInputMode() == DACInputMode_GENERATION1) {
		while (len--) {
			*buffer = IntSample((*buffer & 0x8000) | ((*buffer << 1) & 0x7FFE));
			++buffer;
		}
	}
}

static inline float produceDistortedSample(float sample) {
	// Here we roughly simulate the distortion caused by the DAC bit shift.
	if (sample < -1.0f) {
		return sample + 2.0f;
	} else if (1.0f < sample) {
		return sample - 2.0f;
	}
	return sample;
}

template <>
void RendererImpl<FloatSample>::produceLA32Output(FloatSample *buffer, Bit32u len) {
	switch (synth.getDACInputMode()) {
	case DACInputMode_NICE:
		// Note, we do not do any clamping for floats here to avoid introducing distortions.
		// This means that the output signal may actually overshoot the unity when the volume is set too high.
		// We leave it up to the consumer whether the output is to be clamped or properly normalised further on.
		while (len--) {
			*buffer *= 2.0f;
			buffer++;
		}
		break;
	case DACInputMode_GENERATION2:
		while (len--) {
			*buffer = produceDistortedSample(2.0f * *buffer);
			buffer++;
		}
		break;
	default:
		break;
	}
}

template <>
void RendererImpl<FloatSample>::convertSamplesToOutput(FloatSample *buffer, Bit32u len) {
	if (synth.getDACInputMode() == DACInputMode_GENERATION1) {
		while (len--) {
			*buffer = produceDistortedSample(2.0f * *buffer);
			buffer++;
		}
	}
}

template <class Sample>
void RendererImpl<Sample>::produceStreams(const DACOutputStreams<Sample> &streams, Bit32u len) {
	if (isActivated()) {
		// Even if LA32 output isn't desired, we proceed anyway with temp buffers
		Sample *nonReverbLeft = streams.nonReverbLeft == NULL ? tmpNonReverbLeft : streams.nonReverbLeft;
		Sample *nonReverbRight = streams.nonReverbRight == NULL ? tmpNonReverbRight : streams.nonReverbRight;
		Sample *reverbDryLeft = streams.reverbDryLeft == NULL ? tmpReverbDryLeft : streams.reverbDryLeft;
		Sample *reverbDryRight = streams.reverbDryRight == NULL ? tmpReverbDryRight : streams.reverbDryRight;

		Synth::muteSampleBuffer(nonReverbLeft, len);
		Synth::muteSampleBuffer(nonReverbRight, len);
		Synth::muteSampleBuffer(reverbDryLeft, len);
		Synth::muteSampleBuffer(reverbDryRight, len);

		for (unsigned int i = 0; i < synth.getPartialCount(); i++) {
			if (getPartialManager().shouldReverb(i)) {
				getPartialManager().produceOutput(i, reverbDryLeft, reverbDryRight, len);
			} else {
				getPartialManager().produceOutput(i, nonReverbLeft, nonReverbRight, len);
			}
		}

		produceLA32Output(reverbDryLeft, len);
		produceLA32Output(reverbDryRight, len);

		if (synth.isReverbEnabled()) {
			if (!getReverbModel().process(reverbDryLeft, reverbDryRight, streams.reverbWetLeft, streams.reverbWetRight, len)) {
				printDebug("RendererImpl: Invalid call to BReverbModel::process()!\n");
			}
			if (streams.reverbWetLeft != NULL) convertSamplesToOutput(streams.reverbWetLeft, len);
			if (streams.reverbWetRight != NULL) convertSamplesToOutput(streams.reverbWetRight, len);
		} else {
			Synth::muteSampleBuffer(streams.reverbWetLeft, len);
			Synth::muteSampleBuffer(streams.reverbWetRight, len);
		}

		// Don't bother with conversion if the output is going to be unused
		if (streams.nonReverbLeft != NULL) {
			produceLA32Output(nonReverbLeft, len);
			convertSamplesToOutput(nonReverbLeft, len);
		}
		if (streams.nonReverbRight != NULL) {
			produceLA32Output(nonReverbRight, len);
			convertSamplesToOutput(nonReverbRight, len);
		}
		if (streams.reverbDryLeft != NULL) convertSamplesToOutput(reverbDryLeft, len);
		if (streams.reverbDryRight != NULL) convertSamplesToOutput(reverbDryRight, len);
	} else {
		muteStreams(streams, len);
	}

	getPartialManager().clearAlreadyOutputed();
	incRenderedSampleCount(len);
	updateDisplayState();
}

void Synth::printPartialUsage(Bit32u sampleOffset) {
	unsigned int partialUsage[9];
	partialManager->getPerPartPartialUsage(partialUsage);
	if (sampleOffset > 0) {
		printDebug("[+%u] Partial Usage: 1:%02d 2:%02d 3:%02d 4:%02d 5:%02d 6:%02d 7:%02d 8:%02d R: %02d  TOTAL: %02d", sampleOffset, partialUsage[0], partialUsage[1], partialUsage[2], partialUsage[3], partialUsage[4], partialUsage[5], partialUsage[6], partialUsage[7], partialUsage[8], getPartialCount() - partialManager->getFreePartialCount());
	} else {
		printDebug("Partial Usage: 1:%02d 2:%02d 3:%02d 4:%02d 5:%02d 6:%02d 7:%02d 8:%02d R: %02d  TOTAL: %02d", partialUsage[0], partialUsage[1], partialUsage[2], partialUsage[3], partialUsage[4], partialUsage[5], partialUsage[6], partialUsage[7], partialUsage[8], getPartialCount() - partialManager->getFreePartialCount());
	}
}

bool Synth::hasActivePartials() const {
	if (!opened) {
		return false;
	}
	for (unsigned int partialNum = 0; partialNum < getPartialCount(); partialNum++) {
		if (partialManager->getPartial(partialNum)->isActive()) {
			return true;
		}
	}
	return false;
}

bool Synth::isActive() {
	if (!opened) {
		return false;
	}
	if (!midiQueue->isEmpty() || hasActivePartials()) {
		return true;
	}
	if (isReverbEnabled() && reverbModel->isActive()) {
		return true;
	}
	activated = false;
	return false;
}

Bit32u Synth::getPartialCount() const {
	return partialCount;
}

void Synth::getPartStates(bool *partStates) const {
	if (!opened) {
		memset(partStates, 0, 9 * sizeof(bool));
		return;
	}
	for (int partNumber = 0; partNumber < 9; partNumber++) {
		const Part *part = parts[partNumber];
		partStates[partNumber] = part->getActiveNonReleasingPartialCount() > 0;
	}
}

Bit32u Synth::getPartStates() const {
	if (!opened) return 0;
	bool partStates[9];
	getPartStates(partStates);
	Bit32u bitSet = 0;
	for (int partNumber = 8; partNumber >= 0; partNumber--) {
		bitSet = (bitSet << 1) | (partStates[partNumber] ? 1 : 0);
	}
	return bitSet;
}

void Synth::getPartialStates(PartialState *partialStates) const {
	if (!opened) {
		memset(partialStates, 0, partialCount * sizeof(PartialState));
		return;
	}
	for (unsigned int partialNum = 0; partialNum < partialCount; partialNum++) {
		partialStates[partialNum] = getPartialState(partialManager, partialNum);
	}
}

void Synth::getPartialStates(Bit8u *partialStates) const {
	if (!opened) {
		memset(partialStates, 0, ((partialCount + 3) >> 2));
		return;
	}
	for (unsigned int quartNum = 0; (4 * quartNum) < partialCount; quartNum++) {
		Bit8u packedStates = 0;
		for (unsigned int i = 0; i < 4; i++) {
			unsigned int partialNum = (4 * quartNum) + i;
			if (partialCount <= partialNum) break;
			PartialState partialState = getPartialState(partialManager, partialNum);
			packedStates |= (partialState & 3) << (2 * i);
		}
		partialStates[quartNum] = packedStates;
	}
}

Bit32u Synth::getPlayingNotes(Bit8u partNumber, Bit8u *keys, Bit8u *velocities) const {
	Bit32u playingNotes = 0;
	if (opened && (partNumber < 9)) {
		const Part *part = parts[partNumber];
		const Poly *poly = part->getFirstActivePoly();
		while (poly != NULL) {
			keys[playingNotes] = Bit8u(poly->getKey());
			velocities[playingNotes] = Bit8u(poly->getVelocity());
			playingNotes++;
			poly = poly->getNext();
		}
	}
	return playingNotes;
}

const char *Synth::getPatchName(Bit8u partNumber) const {
	return (!opened || partNumber > 8) ? NULL : parts[partNumber]->getCurrentInstr();
}

bool Synth::getSoundGroupName(char *soundGroupName, Bit8u timbreGroup, Bit8u timbreNumber) const {
	if (!opened || 63 < timbreNumber) return false;
	const char *foundGroupName = getSoundGroupName(timbreGroup, timbreNumber);
	if (foundGroupName == NULL) return false;
	memcpy(soundGroupName, foundGroupName, 7);
	soundGroupName[7] = 0;
	return true;
}

bool Synth::getSoundName(char *soundName, Bit8u timbreGroup, Bit8u timbreNumber) const {
	if (!opened || 3 < timbreGroup) return false;
	Bit8u timbresInGroup = 3 == timbreGroup ? controlROMMap->timbreRCount : 64;
	if (timbresInGroup <= timbreNumber) return false;
	TimbreParam::CommonParam &timbreCommon = mt32ram.timbres[timbreGroup * 64 + timbreNumber].timbre.common;
	if (timbreCommon.partialMute == 0) return false;
	memcpy(soundName, timbreCommon.name, sizeof timbreCommon.name);
	soundName[sizeof timbreCommon.name] = 0;
	return true;
}

const Part *Synth::getPart(Bit8u partNum) const {
	if (partNum > 8) {
		return NULL;
	}
	return parts[partNum];
}

void MemoryRegion::read(unsigned int entry, unsigned int off, Bit8u *dst, unsigned int len) const {
	off += entry * entrySize;
	// This method should never be called with out-of-bounds parameters,
	// or on an unsupported region - seeing any of this debug output indicates a bug in the emulator
	if (off > entrySize * entries - 1) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("read[%d]: parameters start out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		return;
	}
	if (off + len > entrySize * entries) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("read[%d]: parameters end out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		len = entrySize * entries - off;
	}
	Bit8u *src = getRealMemory();
	if (src == NULL) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("read[%d]: unreadable region: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		return;
	}
	memcpy(dst, src + off, len);
}

void MemoryRegion::write(unsigned int entry, unsigned int off, const Bit8u *src, unsigned int len, bool init) const {
	unsigned int memOff = entry * entrySize + off;
	// This method should never be called with out-of-bounds parameters,
	// or on an unsupported region - seeing any of this debug output indicates a bug in the emulator
	if (off > entrySize * entries - 1) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("write[%d]: parameters start out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		return;
	}
	if (off + len > entrySize * entries) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("write[%d]: parameters end out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		len = entrySize * entries - off;
	}
	Bit8u *dest = getRealMemory();
	if (dest == NULL) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("write[%d]: unwritable region: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		return;
	}

	for (unsigned int i = 0; i < len; i++) {
		Bit8u desiredValue = src[i];
		Bit8u maxValue = getMaxValue(memOff);
		// maxValue == 0 means write-protected unless called from initialisation code, in which case it really means the maximum value is 0.
		if (maxValue != 0 || init) {
			if (desiredValue > maxValue) {
#if MT32EMU_MONITOR_SYSEX > 0
				synth->printDebug("write[%d]: Wanted 0x%02x at %d, but max 0x%02x", type, desiredValue, memOff, maxValue);
#endif
				desiredValue = maxValue;
			}
			dest[memOff] = desiredValue;
		} else if (desiredValue != 0) {
#if MT32EMU_MONITOR_SYSEX > 0
			// Only output debug info if they wanted to write non-zero, since a lot of things cause this to spit out a lot of debug info otherwise.
			synth->printDebug("write[%d]: Wanted 0x%02x at %d, but write-protected", type, desiredValue, memOff);
#endif
		}
		memOff++;
	}
}

} // namespace MT32Emu
