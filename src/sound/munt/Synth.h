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

#ifndef MT32EMU_SYNTH_H
#define MT32EMU_SYNTH_H

#include <cstdarg>
#include <cstddef>
#include <cstring>

#include "globals.h"
#include "Types.h"
#include "Enumerations.h"

namespace MT32Emu {

class Analog;
class BReverbModel;
class Extensions;
class MemoryRegion;
class MidiEventQueue;
class Part;
class Poly;
class Partial;
class PartialManager;
class Renderer;
class ROMImage;

class PatchTempMemoryRegion;
class RhythmTempMemoryRegion;
class TimbreTempMemoryRegion;
class PatchesMemoryRegion;
class TimbresMemoryRegion;
class SystemMemoryRegion;
class DisplayMemoryRegion;
class ResetMemoryRegion;

struct ControlROMFeatureSet;
struct ControlROMMap;
struct PCMWaveEntry;
struct MemParams;

const Bit8u SYSEX_MANUFACTURER_ROLAND = 0x41;

const Bit8u SYSEX_MDL_MT32 = 0x16;
const Bit8u SYSEX_MDL_D50 = 0x14;

const Bit8u SYSEX_CMD_RQ1 = 0x11; // Request data #1
const Bit8u SYSEX_CMD_DT1 = 0x12; // Data set 1
const Bit8u SYSEX_CMD_WSD = 0x40; // Want to send data
const Bit8u SYSEX_CMD_RQD = 0x41; // Request data
const Bit8u SYSEX_CMD_DAT = 0x42; // Data set
const Bit8u SYSEX_CMD_ACK = 0x43; // Acknowledge
const Bit8u SYSEX_CMD_EOD = 0x45; // End of data
const Bit8u SYSEX_CMD_ERR = 0x4E; // Communications error
const Bit8u SYSEX_CMD_RJC = 0x4F; // Rejection

const Bit32u CONTROL_ROM_SIZE = 64 * 1024;

// Set of multiplexed output streams appeared at the DAC entrance.
template <class T>
struct DACOutputStreams {
	T *nonReverbLeft;
	T *nonReverbRight;
	T *reverbDryLeft;
	T *reverbDryRight;
	T *reverbWetLeft;
	T *reverbWetRight;
};

// Class for the client to supply callbacks for reporting various errors and information
class MT32EMU_EXPORT ReportHandler {
public:
	virtual ~ReportHandler() {}

	// Callback for debug messages, in vprintf() format
	virtual void printDebug(const char *fmt, va_list list);
	// Callbacks for reporting errors
	virtual void onErrorControlROM() {}
	virtual void onErrorPCMROM() {}
	// Callback for reporting about displaying a new custom message on LCD
	virtual void showLCDMessage(const char *message);
	// Callback for reporting actual processing of a MIDI message
	virtual void onMIDIMessagePlayed() {}
	// Callback for reporting an overflow of the input MIDI queue.
	// Returns true if a recovery action was taken and yet another attempt to enqueue the MIDI event is desired.
	virtual bool onMIDIQueueOverflow() { return false; }
	// Callback invoked when a System Realtime MIDI message is detected at the input.
	virtual void onMIDISystemRealtime(Bit8u /* systemRealtime */) {}
	// Callbacks for reporting system events
	virtual void onDeviceReset() {}
	virtual void onDeviceReconfig() {}
	// Callbacks for reporting changes of reverb settings
	virtual void onNewReverbMode(Bit8u /* mode */) {}
	virtual void onNewReverbTime(Bit8u /* time */) {}
	virtual void onNewReverbLevel(Bit8u /* level */) {}
	// Callbacks for reporting various information
	virtual void onPolyStateChanged(Bit8u /* partNum */) {}
	virtual void onProgramChanged(Bit8u /* partNum */, const char * /* soundGroupName */, const char * /* patchName */) {}
};

class Synth {
friend class DefaultMidiStreamParser;
friend class MemoryRegion;
friend class Part;
friend class Partial;
friend class PartialManager;
friend class Poly;
friend class Renderer;
friend class RhythmPart;
friend class SamplerateAdapter;
friend class SoxrAdapter;
friend class TVA;
friend class TVF;
friend class TVP;

private:
	// **************************** Implementation fields **************************

	PatchTempMemoryRegion *patchTempMemoryRegion;
	RhythmTempMemoryRegion *rhythmTempMemoryRegion;
	TimbreTempMemoryRegion *timbreTempMemoryRegion;
	PatchesMemoryRegion *patchesMemoryRegion;
	TimbresMemoryRegion *timbresMemoryRegion;
	SystemMemoryRegion *systemMemoryRegion;
	DisplayMemoryRegion *displayMemoryRegion;
	ResetMemoryRegion *resetMemoryRegion;

	Bit8u *paddedTimbreMaxTable;

	PCMWaveEntry *pcmWaves; // Array

	const ControlROMFeatureSet *controlROMFeatures;
	const ControlROMMap *controlROMMap;
	Bit8u controlROMData[CONTROL_ROM_SIZE];
	Bit16s *pcmROMData;
	size_t pcmROMSize; // This is in 16-bit samples, therefore half the number of bytes in the ROM

	Bit8u soundGroupIx[128]; // For each standard timbre
	const char (*soundGroupNames)[9]; // Array

	Bit32u partialCount;
	Bit8u nukeme[16]; // FIXME: Nuke it. For binary compatibility only.

	MidiEventQueue *midiQueue;
	volatile Bit32u lastReceivedMIDIEventTimestamp;
	volatile Bit32u renderedSampleCount;

	MemParams &mt32ram, &mt32default;

	BReverbModel *reverbModels[4];
	BReverbModel *reverbModel;
	bool reverbOverridden;

	MIDIDelayMode midiDelayMode;
	DACInputMode dacInputMode;

	float outputGain;
	float reverbOutputGain;

	bool reversedStereoEnabled;

	bool opened;
	bool activated;

	bool isDefaultReportHandler;
	ReportHandler *reportHandler;

	PartialManager *partialManager;
	Part *parts[9];

	// When a partial needs to be aborted to free it up for use by a new Poly,
	// the controller will busy-loop waiting for the sound to finish.
	// We emulate this by delaying new MIDI events processing until abortion finishes.
	Poly *abortingPoly;

	Analog *analog;
	Renderer *renderer;

	// Binary compatibility helper.
	Extensions &extensions;

	// **************************** Implementation methods **************************

	Bit32u addMIDIInterfaceDelay(Bit32u len, Bit32u timestamp);
	bool isAbortingPoly() const { return abortingPoly != NULL; }

	void writeSysexGlobal(Bit32u addr, const Bit8u *sysex, Bit32u len);
	void readSysex(Bit8u channel, const Bit8u *sysex, Bit32u len) const;
	void initMemoryRegions();
	void deleteMemoryRegions();
	MemoryRegion *findMemoryRegion(Bit32u addr);
	void writeMemoryRegion(const MemoryRegion *region, Bit32u addr, Bit32u len, const Bit8u *data);
	void readMemoryRegion(const MemoryRegion *region, Bit32u addr, Bit32u len, Bit8u *data);

	bool loadControlROM(const ROMImage &controlROMImage);
	bool loadPCMROM(const ROMImage &pcmROMImage);

	bool initPCMList(Bit16u mapAddress, Bit16u count);
	bool initTimbres(Bit16u mapAddress, Bit16u offset, Bit16u timbreCount, Bit16u startTimbre, bool compressed);
	bool initCompressedTimbre(Bit16u drumNum, const Bit8u *mem, Bit32u memLen);
	void initReverbModels(bool mt32CompatibleMode);
	void initSoundGroups(char newSoundGroupNames[][9]);

	void refreshSystemMasterTune();
	void refreshSystemReverbParameters();
	void refreshSystemReserveSettings();
	void refreshSystemChanAssign(Bit8u firstPart, Bit8u lastPart);
	void refreshSystemMasterVol();
	void refreshSystem();
	void reset();
	void dispose();

	void printPartialUsage(Bit32u sampleOffset = 0);

	void newTimbreSet(Bit8u partNum, Bit8u timbreGroup, Bit8u timbreNumber, const char patchName[]);
	void printDebug(const char *fmt, ...);

	// partNum should be 0..7 for Part 1..8, or 8 for Rhythm
	const Part *getPart(Bit8u partNum) const;

	void resetMasterTunePitchDelta();
	Bit32s getMasterTunePitchDelta() const;

public:
	static inline Bit16s clipSampleEx(Bit32s sampleEx) {
		// Clamp values above 32767 to 32767, and values below -32768 to -32768
		// FIXME: Do we really need this stuff? I think these branches are very well predicted. Instead, this introduces a chain.
		// The version below is actually a bit faster on my system...
		//return ((sampleEx + 0x8000) & ~0xFFFF) ? Bit16s((sampleEx >> 31) ^ 0x7FFF) : (Bit16s)sampleEx;
		return ((-0x8000 <= sampleEx) && (sampleEx <= 0x7FFF)) ? Bit16s(sampleEx) : Bit16s((sampleEx >> 31) ^ 0x7FFF);
	}

	static inline float clipSampleEx(float sampleEx) {
		return sampleEx;
	}

	template <class S>
	static inline void muteSampleBuffer(S *buffer, Bit32u len) {
		if (buffer == NULL) return;
		memset(buffer, 0, len * sizeof(S));
	}

	static inline void muteSampleBuffer(float *buffer, Bit32u len) {
		if (buffer == NULL) return;
		// FIXME: Use memset() where compatibility is guaranteed (if this turns out to be a win)
		while (len--) {
			*(buffer++) = 0.0f;
		}
	}

	static inline Bit16s convertSample(float sample) {
		return Synth::clipSampleEx(Bit32s(sample * 32768.0f)); // This multiplier corresponds to normalised floats
	}

	static inline float convertSample(Bit16s sample) {
		return float(sample) / 32768.0f; // This multiplier corresponds to normalised floats
	}

	// Returns library version as an integer in format: 0x00MMmmpp, where:
	// MM - major version number
	// mm - minor version number
	// pp - patch number
	MT32EMU_EXPORT static Bit32u getLibraryVersionInt();
	// Returns library version as a C-string in format: "MAJOR.MINOR.PATCH"
	MT32EMU_EXPORT static const char *getLibraryVersionString();

	MT32EMU_EXPORT static Bit32u getShortMessageLength(Bit32u msg);
	MT32EMU_EXPORT static Bit8u calcSysexChecksum(const Bit8u *data, const Bit32u len, const Bit8u initChecksum = 0);

	// Returns output sample rate used in emulation of stereo analog circuitry of hardware units.
	// See comment for AnalogOutputMode.
	MT32EMU_EXPORT static Bit32u getStereoOutputSampleRate(AnalogOutputMode analogOutputMode);

	// Optionally sets callbacks for reporting various errors, information and debug messages
	MT32EMU_EXPORT explicit Synth(ReportHandler *useReportHandler = NULL);
	MT32EMU_EXPORT ~Synth();

	// Used to initialise the MT-32. Must be called before any other function.
	// Returns true if initialization was sucessful, otherwise returns false.
	// controlROMImage and pcmROMImage represent Control and PCM ROM images for use by synth.
	// usePartialCount sets the maximum number of partials playing simultaneously for this session (optional).
	// analogOutputMode sets the mode for emulation of analogue circuitry of the hardware units (optional).
	MT32EMU_EXPORT bool open(const ROMImage &controlROMImage, const ROMImage &pcmROMImage, Bit32u usePartialCount = DEFAULT_MAX_PARTIALS, AnalogOutputMode analogOutputMode = AnalogOutputMode_COARSE);

	// Overloaded method which opens the synth with default partial count.
	MT32EMU_EXPORT bool open(const ROMImage &controlROMImage, const ROMImage &pcmROMImage, AnalogOutputMode analogOutputMode);

	// Closes the MT-32 and deallocates any memory used by the synthesizer
	MT32EMU_EXPORT void close();

	// Returns true if the synth is in completely initialized state, otherwise returns false.
	MT32EMU_EXPORT bool isOpen() const;

	// All the enqueued events are processed by the synth immediately.
	MT32EMU_EXPORT void flushMIDIQueue();

	// Sets size of the internal MIDI event queue. The queue size is set to the minimum power of 2 that is greater or equal to the size specified.
	// The queue is flushed before reallocation.
	// Returns the actual queue size being used.
	MT32EMU_EXPORT Bit32u setMIDIEventQueueSize(Bit32u requestedSize);

	// Configures the SysEx storage of the internal MIDI event queue.
	// Supplying 0 in the storageBufferSize argument makes the SysEx data stored
	// in multiple dynamically allocated buffers per MIDI event. These buffers are only disposed
	// when a new MIDI event replaces the SysEx event in the queue, thus never on the rendering thread.
	// This is the default behaviour.
	// In contrast, when a positive value is specified, SysEx data will be stored in a single preallocated buffer,
	// which makes this kind of storage safe for use in a realtime thread. Additionally, the space retained
	// by a SysEx event, that has been processed and thus is no longer necessary, is disposed instantly.
	// Note, the queue is flushed and recreated in the process so that its size remains intact.
	MT32EMU_EXPORT void configureMIDIEventQueueSysexStorage(Bit32u storageBufferSize);

	// Returns current value of the global counter of samples rendered since the synth was created (at the native sample rate 32000 Hz).
	// This method helps to compute accurate timestamp of a MIDI message to use with the methods below.
	MT32EMU_EXPORT Bit32u getInternalRenderedSampleCount() const;

	// Enqueues a MIDI event for subsequent playback.
	// The MIDI event will be processed not before the specified timestamp.
	// The timestamp is measured as the global rendered sample count since the synth was created (at the native sample rate 32000 Hz).
	// The minimum delay involves emulation of the delay introduced while the event is transferred via MIDI interface
	// and emulation of the MCU busy-loop while it frees partials for use by a new Poly.
	// Calls from multiple threads must be synchronised, although, no synchronisation is required with the rendering thread.
	// The methods return false if the MIDI event queue is full and the message cannot be enqueued.

	// Enqueues a single short MIDI message to play at specified time. The message must contain a status byte.
	MT32EMU_EXPORT bool playMsg(Bit32u msg, Bit32u timestamp);
	// Enqueues a single well formed System Exclusive MIDI message to play at specified time.
	MT32EMU_EXPORT bool playSysex(const Bit8u *sysex, Bit32u len, Bit32u timestamp);

	// Enqueues a single short MIDI message to be processed ASAP. The message must contain a status byte.
	MT32EMU_EXPORT bool playMsg(Bit32u msg);
	// Enqueues a single well formed System Exclusive MIDI message to be processed ASAP.
	MT32EMU_EXPORT bool playSysex(const Bit8u *sysex, Bit32u len);

	// WARNING:
	// The methods below don't ensure minimum 1-sample delay between sequential MIDI events,
	// and a sequence of NoteOn and immediately succeeding NoteOff messages is always silent.
	// A thread that invokes these methods must be explicitly synchronised with the thread performing sample rendering.

	// Sends a short MIDI message to the synth for immediate playback. The message must contain a status byte.
	// See the WARNING above.
	MT32EMU_EXPORT void playMsgNow(Bit32u msg);
	// Sends unpacked short MIDI message to the synth for immediate playback. The message must contain a status byte.
	// See the WARNING above.
	MT32EMU_EXPORT void playMsgOnPart(Bit8u part, Bit8u code, Bit8u note, Bit8u velocity);

	// Sends a single well formed System Exclusive MIDI message for immediate processing. The length is in bytes.
	// See the WARNING above.
	MT32EMU_EXPORT void playSysexNow(const Bit8u *sysex, Bit32u len);
	// Sends inner body of a System Exclusive MIDI message for direct processing. The length is in bytes.
	// See the WARNING above.
	MT32EMU_EXPORT void playSysexWithoutFraming(const Bit8u *sysex, Bit32u len);
	// Sends inner body of a System Exclusive MIDI message for direct processing. The length is in bytes.
	// See the WARNING above.
	MT32EMU_EXPORT void playSysexWithoutHeader(Bit8u device, Bit8u command, const Bit8u *sysex, Bit32u len);
	// Sends inner body of a System Exclusive MIDI message for direct processing. The length is in bytes.
	// See the WARNING above.
	MT32EMU_EXPORT void writeSysex(Bit8u channel, const Bit8u *sysex, Bit32u len);

	// Allows to disable wet reverb output altogether.
	MT32EMU_EXPORT void setReverbEnabled(bool reverbEnabled);
	// Returns whether wet reverb output is enabled.
	MT32EMU_EXPORT bool isReverbEnabled() const;
	// Sets override reverb mode. In this mode, emulation ignores sysexes (or the related part of them) which control the reverb parameters.
	// This mode is in effect until it is turned off. When the synth is re-opened, the override mode is unchanged but the state
	// of the reverb model is reset to default.
	MT32EMU_EXPORT void setReverbOverridden(bool reverbOverridden);
	// Returns whether reverb settings are overridden.
	MT32EMU_EXPORT bool isReverbOverridden() const;
	// Forces reverb model compatibility mode. By default, the compatibility mode corresponds to the used control ROM version.
	// Invoking this method with the argument set to true forces emulation of old MT-32 reverb circuit.
	// When the argument is false, emulation of the reverb circuit used in new generation of MT-32 compatible modules is enforced
	// (these include CM-32L and LAPC-I).
	MT32EMU_EXPORT void setReverbCompatibilityMode(bool mt32CompatibleMode);
	// Returns whether reverb is in old MT-32 compatibility mode.
	MT32EMU_EXPORT bool isMT32ReverbCompatibilityMode() const;
	// Returns whether default reverb compatibility mode is the old MT-32 compatibility mode.
	MT32EMU_EXPORT bool isDefaultReverbMT32Compatible() const;
	// If enabled, reverb buffers for all modes are keept around allocated all the time to avoid memory
	// allocating/freeing in the rendering thread, which may be required for realtime operation.
	// Otherwise, reverb buffers that are not in use are deleted to save memory (the default behaviour).
	MT32EMU_EXPORT void preallocateReverbMemory(bool enabled);
	// Sets new DAC input mode. See DACInputMode for details.
	MT32EMU_EXPORT void setDACInputMode(DACInputMode mode);
	// Returns current DAC input mode. See DACInputMode for details.
	MT32EMU_EXPORT DACInputMode getDACInputMode() const;
	// Sets new MIDI delay mode. See MIDIDelayMode for details.
	MT32EMU_EXPORT void setMIDIDelayMode(MIDIDelayMode mode);
	// Returns current MIDI delay mode. See MIDIDelayMode for details.
	MT32EMU_EXPORT MIDIDelayMode getMIDIDelayMode() const;

	// Sets output gain factor for synth output channels. Applied to all output samples and unrelated with the synth's Master volume,
	// it rather corresponds to the gain of the output analog circuitry of the hardware units. However, together with setReverbOutputGain()
	// it offers to the user a capability to control the gain of reverb and non-reverb output channels independently.
	MT32EMU_EXPORT void setOutputGain(float gain);
	// Returns current output gain factor for synth output channels.
	MT32EMU_EXPORT float getOutputGain() const;

	// Sets output gain factor for the reverb wet output channels. It rather corresponds to the gain of the output
	// analog circuitry of the hardware units. However, together with setOutputGain() it offers to the user a capability
	// to control the gain of reverb and non-reverb output channels independently.
	//
	// Note: We're currently emulate CM-32L/CM-64 reverb quite accurately and the reverb output level closely
	// corresponds to the level of digital capture. Although, according to the CM-64 PCB schematic,
	// there is a difference in the reverb analogue circuit, and the resulting output gain is 0.68
	// of that for LA32 analogue output. This factor is applied to the reverb output gain.
	MT32EMU_EXPORT void setReverbOutputGain(float gain);
	// Returns current output gain factor for reverb wet output channels.
	MT32EMU_EXPORT float getReverbOutputGain() const;

	// Swaps left and right output channels.
	MT32EMU_EXPORT void setReversedStereoEnabled(bool enabled);
	// Returns whether left and right output channels are swapped.
	MT32EMU_EXPORT bool isReversedStereoEnabled() const;

	// Allows to toggle the NiceAmpRamp mode.
	// In this mode, we want to ensure that amp ramp never jumps to the target
	// value and always gradually increases or decreases. It seems that real units
	// do not bother to always check if a newly started ramp leads to a jump.
	// We also prefer the quality improvement over the emulation accuracy,
	// so this mode is enabled by default.
	MT32EMU_EXPORT void setNiceAmpRampEnabled(bool enabled);
	// Returns whether NiceAmpRamp mode is enabled.
	MT32EMU_EXPORT bool isNiceAmpRampEnabled() const;

	// Allows to toggle the NicePanning mode.
	// Despite the Roland's manual specifies allowed panpot values in range 0-14,
	// the LA-32 only receives 3-bit pan setting in fact. In particular, this
	// makes it impossible to set the "middle" panning for a single partial.
	// In the NicePanning mode, we enlarge the pan setting accuracy to 4 bits
	// making it smoother thus sacrificing the emulation accuracy.
	// This mode is disabled by default.
	MT32EMU_EXPORT void setNicePanningEnabled(bool enabled);
	// Returns whether NicePanning mode is enabled.
	MT32EMU_EXPORT bool isNicePanningEnabled() const;

	// Allows to toggle the NicePartialMixing mode.
	// LA-32 is known to mix partials either in-phase (so that they are added)
	// or in counter-phase (so that they are subtracted instead).
	// In some cases, this quirk isn't highly desired because a pair of closely
	// sounding partials may occasionally cancel out.
	// In the NicePartialMixing mode, the mixing is always performed in-phase,
	// thus making the behaviour more predictable.
	// This mode is disabled by default.
	MT32EMU_EXPORT void setNicePartialMixingEnabled(bool enabled);
	// Returns whether NicePartialMixing mode is enabled.
	MT32EMU_EXPORT bool isNicePartialMixingEnabled() const;

	// Selects new type of the wave generator and renderer to be used during subsequent calls to open().
	// By default, RendererType_BIT16S is selected.
	// See RendererType for details.
	MT32EMU_EXPORT void selectRendererType(RendererType);
	// Returns previously selected type of the wave generator and renderer.
	// See RendererType for details.
	MT32EMU_EXPORT RendererType getSelectedRendererType() const;

	// Returns actual sample rate used in emulation of stereo analog circuitry of hardware units.
	// See comment for render() below.
	MT32EMU_EXPORT Bit32u getStereoOutputSampleRate() const;

	// Renders samples to the specified output stream as if they were sampled at the analog stereo output.
	// When AnalogOutputMode is set to ACCURATE (OVERSAMPLED), the output signal is upsampled to 48 (96) kHz in order
	// to retain emulation accuracy in whole audible frequency spectra. Otherwise, native digital signal sample rate is retained.
	// getStereoOutputSampleRate() can be used to query actual sample rate of the output signal.
	// The length is in frames, not bytes (in 16-bit stereo, one frame is 4 bytes). Uses NATIVE byte ordering.
	MT32EMU_EXPORT void render(Bit16s *stream, Bit32u len);
	// Same as above but outputs to a float stereo stream.
	MT32EMU_EXPORT void render(float *stream, Bit32u len);

	// Renders samples to the specified output streams as if they appeared at the DAC entrance.
	// No further processing performed in analog circuitry emulation is applied to the signal.
	// NULL may be specified in place of any or all of the stream buffers to skip it.
	// The length is in samples, not bytes. Uses NATIVE byte ordering.
	MT32EMU_EXPORT void renderStreams(Bit16s *nonReverbLeft, Bit16s *nonReverbRight, Bit16s *reverbDryLeft, Bit16s *reverbDryRight, Bit16s *reverbWetLeft, Bit16s *reverbWetRight, Bit32u len);
	MT32EMU_EXPORT void renderStreams(const DACOutputStreams<Bit16s> &streams, Bit32u len);
	// Same as above but outputs to float streams.
	MT32EMU_EXPORT void renderStreams(float *nonReverbLeft, float *nonReverbRight, float *reverbDryLeft, float *reverbDryRight, float *reverbWetLeft, float *reverbWetRight, Bit32u len);
	MT32EMU_EXPORT void renderStreams(const DACOutputStreams<float> &streams, Bit32u len);

	// Returns true when there is at least one active partial, otherwise false.
	MT32EMU_EXPORT bool hasActivePartials() const;

	// Returns true if the synth is active and subsequent calls to render() may result in non-trivial output (i.e. silence).
	// The synth is considered active when either there are pending MIDI events in the queue, there is at least one active partial,
	// or the reverb is (somewhat unreliably) detected as being active.
	MT32EMU_EXPORT bool isActive();

	// Returns the maximum number of partials playing simultaneously.
	MT32EMU_EXPORT Bit32u getPartialCount() const;

	// Fills in current states of all the parts into the array provided. The array must have at least 9 entries to fit values for all the parts.
	// If the value returned for a part is true, there is at least one active non-releasing partial playing on this part.
	// This info is useful in emulating behaviour of LCD display of the hardware units.
	MT32EMU_EXPORT void getPartStates(bool *partStates) const;

	// Returns current states of all the parts as a bit set. The least significant bit corresponds to the state of part 1,
	// total of 9 bits hold the states of all the parts. If the returned bit for a part is set, there is at least one active
	// non-releasing partial playing on this part. This info is useful in emulating behaviour of LCD display of the hardware units.
	MT32EMU_EXPORT Bit32u getPartStates() const;

	// Fills in current states of all the partials into the array provided. The array must be large enough to accommodate states of all the partials.
	MT32EMU_EXPORT void getPartialStates(PartialState *partialStates) const;

	// Fills in current states of all the partials into the array provided. Each byte in the array holds states of 4 partials
	// starting from the least significant bits. The state of each partial is packed in a pair of bits.
	// The array must be large enough to accommodate states of all the partials (see getPartialCount()).
	MT32EMU_EXPORT void getPartialStates(Bit8u *partialStates) const;

	// Fills in information about currently playing notes on the specified part into the arrays provided. The arrays must be large enough
	// to accommodate data for all the playing notes. The maximum number of simultaneously playing notes cannot exceed the number of partials.
	// Argument partNumber should be 0..7 for Part 1..8, or 8 for Rhythm.
	// Returns the number of currently playing notes on the specified part.
	MT32EMU_EXPORT Bit32u getPlayingNotes(Bit8u partNumber, Bit8u *keys, Bit8u *velocities) const;

	// Returns name of the patch set on the specified part.
	// Argument partNumber should be 0..7 for Part 1..8, or 8 for Rhythm.
	MT32EMU_EXPORT const char *getPatchName(Bit8u partNumber) const;

	// Stores internal state of emulated synth into an array provided (as it would be acquired from hardware).
	MT32EMU_EXPORT void readMemory(Bit32u addr, Bit32u len, Bit8u *data);
}; // class Synth

} // namespace MT32Emu

#endif // #ifndef MT32EMU_SYNTH_H
