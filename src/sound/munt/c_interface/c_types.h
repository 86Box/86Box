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

#ifndef MT32EMU_C_TYPES_H
#define MT32EMU_C_TYPES_H

#include <stdarg.h>
#include <stddef.h>

#include "../globals.h"

#define MT32EMU_C_ENUMERATIONS
#include "../Enumerations.h"
#undef MT32EMU_C_ENUMERATIONS

#ifdef _WIN32
#  define MT32EMU_C_CALL __cdecl
#else
#  define MT32EMU_C_CALL
#endif

typedef unsigned int       mt32emu_bit32u;
typedef   signed int       mt32emu_bit32s;
typedef unsigned short int mt32emu_bit16u;
typedef   signed short int mt32emu_bit16s;
typedef unsigned char      mt32emu_bit8u;
typedef   signed char      mt32emu_bit8s;

typedef char mt32emu_sha1_digest[41];

typedef enum {
	MT32EMU_BOOL_FALSE, MT32EMU_BOOL_TRUE
} mt32emu_boolean;

typedef enum {
	/* Operation completed normally. */
	MT32EMU_RC_OK = 0,
	MT32EMU_RC_ADDED_CONTROL_ROM = 1,
	MT32EMU_RC_ADDED_PCM_ROM = 2,
	MT32EMU_RC_ADDED_PARTIAL_CONTROL_ROM = 3,
	MT32EMU_RC_ADDED_PARTIAL_PCM_ROM = 4,

	/* Definite error occurred. */
	MT32EMU_RC_ROM_NOT_IDENTIFIED = -1,
	MT32EMU_RC_FILE_NOT_FOUND = -2,
	MT32EMU_RC_FILE_NOT_LOADED = -3,
	MT32EMU_RC_MISSING_ROMS = -4,
	MT32EMU_RC_NOT_OPENED = -5,
	MT32EMU_RC_QUEUE_FULL = -6,
	MT32EMU_RC_ROMS_NOT_PAIRABLE = -7,
	MT32EMU_RC_MACHINE_NOT_IDENTIFIED = -8,

	/* Undefined error occurred. */
	MT32EMU_RC_FAILED = -100
} mt32emu_return_code;

/** Emulation context */
typedef struct mt32emu_data *mt32emu_context;
typedef const struct mt32emu_data *mt32emu_const_context;

/* Convenience aliases */
#ifndef __cplusplus
typedef enum mt32emu_analog_output_mode mt32emu_analog_output_mode;
typedef enum mt32emu_dac_input_mode mt32emu_dac_input_mode;
typedef enum mt32emu_midi_delay_mode mt32emu_midi_delay_mode;
typedef enum mt32emu_partial_state mt32emu_partial_state;
typedef enum mt32emu_samplerate_conversion_quality mt32emu_samplerate_conversion_quality;
typedef enum mt32emu_renderer_type mt32emu_renderer_type;
#endif

/** Contains identifiers and descriptions of ROM files being used. */
typedef struct {
	const char *control_rom_id;
	const char *control_rom_description;
	const char *control_rom_sha1_digest;
	const char *pcm_rom_id;
	const char *pcm_rom_description;
	const char *pcm_rom_sha1_digest;
} mt32emu_rom_info;

/** Set of multiplexed output bit16s streams appeared at the DAC entrance. */
typedef struct {
	mt32emu_bit16s *nonReverbLeft;
	mt32emu_bit16s *nonReverbRight;
	mt32emu_bit16s *reverbDryLeft;
	mt32emu_bit16s *reverbDryRight;
	mt32emu_bit16s *reverbWetLeft;
	mt32emu_bit16s *reverbWetRight;
} mt32emu_dac_output_bit16s_streams;

/** Set of multiplexed output float streams appeared at the DAC entrance. */
typedef struct {
	float *nonReverbLeft;
	float *nonReverbRight;
	float *reverbDryLeft;
	float *reverbDryRight;
	float *reverbWetLeft;
	float *reverbWetRight;
} mt32emu_dac_output_float_streams;

/* === Interface handling === */

/** Report handler interface versions */
typedef enum {
	MT32EMU_REPORT_HANDLER_VERSION_0 = 0,
	MT32EMU_REPORT_HANDLER_VERSION_1 = 1,
	MT32EMU_REPORT_HANDLER_VERSION_CURRENT = MT32EMU_REPORT_HANDLER_VERSION_1
} mt32emu_report_handler_version;

/** MIDI receiver interface versions */
typedef enum {
	MT32EMU_MIDI_RECEIVER_VERSION_0 = 0,
	MT32EMU_MIDI_RECEIVER_VERSION_CURRENT = MT32EMU_MIDI_RECEIVER_VERSION_0
} mt32emu_midi_receiver_version;

/** Synth interface versions */
typedef enum {
	MT32EMU_SERVICE_VERSION_0 = 0,
	MT32EMU_SERVICE_VERSION_1 = 1,
	MT32EMU_SERVICE_VERSION_2 = 2,
	MT32EMU_SERVICE_VERSION_3 = 3,
	MT32EMU_SERVICE_VERSION_4 = 4,
	MT32EMU_SERVICE_VERSION_5 = 5,
	MT32EMU_SERVICE_VERSION_6 = 6,
	MT32EMU_SERVICE_VERSION_CURRENT = MT32EMU_SERVICE_VERSION_6
} mt32emu_service_version;

/* === Report Handler Interface === */

typedef union mt32emu_report_handler_i mt32emu_report_handler_i;

/** Interface for handling reported events (initial version) */
#define MT32EMU_REPORT_HANDLER_I_V0 \
	/** Returns the actual interface version ID */ \
	mt32emu_report_handler_version (MT32EMU_C_CALL *getVersionID)(mt32emu_report_handler_i i); \
\
	/** Callback for debug messages, in vprintf() format */ \
	void (MT32EMU_C_CALL *printDebug)(void *instance_data, const char *fmt, va_list list); \
	/** Callbacks for reporting errors */ \
	void (MT32EMU_C_CALL *onErrorControlROM)(void *instance_data); \
	void (MT32EMU_C_CALL *onErrorPCMROM)(void *instance_data); \
	/** Callback for reporting about displaying a new custom message on LCD */ \
	void (MT32EMU_C_CALL *showLCDMessage)(void *instance_data, const char *message); \
	/** Callback for reporting actual processing of a MIDI message */ \
	void (MT32EMU_C_CALL *onMIDIMessagePlayed)(void *instance_data); \
	/**
	 * Callback for reporting an overflow of the input MIDI queue.
	 * Returns MT32EMU_BOOL_TRUE if a recovery action was taken
	 * and yet another attempt to enqueue the MIDI event is desired.
	 */ \
	mt32emu_boolean (MT32EMU_C_CALL *onMIDIQueueOverflow)(void *instance_data); \
	/**
	 * Callback invoked when a System Realtime MIDI message is detected in functions
	 * mt32emu_parse_stream and mt32emu_play_short_message and the likes.
	 */ \
	void (MT32EMU_C_CALL *onMIDISystemRealtime)(void *instance_data, mt32emu_bit8u system_realtime); \
	/** Callbacks for reporting system events */ \
	void (MT32EMU_C_CALL *onDeviceReset)(void *instance_data); \
	void (MT32EMU_C_CALL *onDeviceReconfig)(void *instance_data); \
	/** Callbacks for reporting changes of reverb settings */ \
	void (MT32EMU_C_CALL *onNewReverbMode)(void *instance_data, mt32emu_bit8u mode); \
	void (MT32EMU_C_CALL *onNewReverbTime)(void *instance_data, mt32emu_bit8u time); \
	void (MT32EMU_C_CALL *onNewReverbLevel)(void *instance_data, mt32emu_bit8u level); \
	/** Callbacks for reporting various information */ \
	void (MT32EMU_C_CALL *onPolyStateChanged)(void *instance_data, mt32emu_bit8u part_num); \
	void (MT32EMU_C_CALL *onProgramChanged)(void *instance_data, mt32emu_bit8u part_num, const char *sound_group_name, const char *patch_name);

#define MT32EMU_REPORT_HANDLER_I_V1 \
	/**
	 * Invoked to signal about a change of the emulated LCD state. Use mt32emu_get_display_state to retrieve the actual data.
	 * This callback will not be invoked on further changes, until the client retrieves the LCD state.
	 */ \
	void (MT32EMU_C_CALL *onLCDStateUpdated)(void *instance_data); \
	/** Invoked when the emulated MIDI MESSAGE LED changes state. The led_state parameter represents whether the LED is ON. */ \
	void (MT32EMU_C_CALL *onMidiMessageLEDStateUpdated)(void *instance_data, mt32emu_boolean led_state);

typedef struct {
	MT32EMU_REPORT_HANDLER_I_V0
} mt32emu_report_handler_i_v0;

typedef struct {
	MT32EMU_REPORT_HANDLER_I_V0
	MT32EMU_REPORT_HANDLER_I_V1
} mt32emu_report_handler_i_v1;

/**
 * Extensible interface for handling reported events.
 * Union intended to view an interface of any subsequent version as any parent interface not requiring a cast.
 * It is caller's responsibility to check the actual interface version in runtime using the getVersionID() method.
 */
union mt32emu_report_handler_i {
	const mt32emu_report_handler_i_v0 *v0;
	const mt32emu_report_handler_i_v1 *v1;
};

#undef MT32EMU_REPORT_HANDLER_I_V0
#undef MT32EMU_REPORT_HANDLER_I_V1

/* === MIDI Receiver Interface === */

typedef union mt32emu_midi_receiver_i mt32emu_midi_receiver_i;

/** Interface for receiving MIDI messages generated by MIDI stream parser (initial version) */
typedef struct {
	/** Returns the actual interface version ID */
	mt32emu_midi_receiver_version (MT32EMU_C_CALL *getVersionID)(mt32emu_midi_receiver_i i);

	/** Invoked when a complete short MIDI message is parsed in the input MIDI stream. */
	void (MT32EMU_C_CALL *handleShortMessage)(void *instance_data, const mt32emu_bit32u message);

	/** Invoked when a complete well-formed System Exclusive MIDI message is parsed in the input MIDI stream. */
	void (MT32EMU_C_CALL *handleSysex)(void *instance_data, const mt32emu_bit8u stream[], const mt32emu_bit32u length);

	/** Invoked when a System Realtime MIDI message is parsed in the input MIDI stream. */
	void (MT32EMU_C_CALL *handleSystemRealtimeMessage)(void *instance_data, const mt32emu_bit8u realtime);
} mt32emu_midi_receiver_i_v0;

/**
 * Extensible interface for receiving MIDI messages.
 * Union intended to view an interface of any subsequent version as any parent interface not requiring a cast.
 * It is caller's responsibility to check the actual interface version in runtime using the getVersionID() method.
 */
union mt32emu_midi_receiver_i {
	const mt32emu_midi_receiver_i_v0 *v0;
};

/* === Service Interface === */

typedef union mt32emu_service_i mt32emu_service_i;

/**
 * Basic interface that defines all the library services (initial version).
 * The members closely resemble C functions declared in c_interface.h, and the intention is to provide for easier
 * access when the library is dynamically loaded in run-time, e.g. as a plugin. This way the client only needs
 * to bind to mt32emu_get_service_i() function instead of binding to each function it needs to use.
 * See c_interface.h for parameter description.
 */
#define MT32EMU_SERVICE_I_V0 \
	/** Returns the actual interface version ID */ \
	mt32emu_service_version (MT32EMU_C_CALL *getVersionID)(mt32emu_service_i i); \
	mt32emu_report_handler_version (MT32EMU_C_CALL *getSupportedReportHandlerVersionID)(void); \
	mt32emu_midi_receiver_version (MT32EMU_C_CALL *getSupportedMIDIReceiverVersionID)(void); \
\
	mt32emu_bit32u (MT32EMU_C_CALL *getLibraryVersionInt)(void); \
	const char *(MT32EMU_C_CALL *getLibraryVersionString)(void); \
\
	mt32emu_bit32u (MT32EMU_C_CALL *getStereoOutputSamplerate)(const mt32emu_analog_output_mode analog_output_mode); \
\
	mt32emu_context (MT32EMU_C_CALL *createContext)(mt32emu_report_handler_i report_handler, void *instance_data); \
	void (MT32EMU_C_CALL *freeContext)(mt32emu_context context); \
	mt32emu_return_code (MT32EMU_C_CALL *addROMData)(mt32emu_context context, const mt32emu_bit8u *data, size_t data_size, const mt32emu_sha1_digest *sha1_digest); \
	mt32emu_return_code (MT32EMU_C_CALL *addROMFile)(mt32emu_context context, const char *filename); \
	void (MT32EMU_C_CALL *getROMInfo)(mt32emu_const_context context, mt32emu_rom_info *rom_info); \
	void (MT32EMU_C_CALL *setPartialCount)(mt32emu_context context, const mt32emu_bit32u partial_count); \
	void (MT32EMU_C_CALL *setAnalogOutputMode)(mt32emu_context context, const mt32emu_analog_output_mode analog_output_mode); \
	mt32emu_return_code (MT32EMU_C_CALL *openSynth)(mt32emu_const_context context); \
	void (MT32EMU_C_CALL *closeSynth)(mt32emu_const_context context); \
	mt32emu_boolean (MT32EMU_C_CALL *isOpen)(mt32emu_const_context context); \
	mt32emu_bit32u (MT32EMU_C_CALL *getActualStereoOutputSamplerate)(mt32emu_const_context context); \
	void (MT32EMU_C_CALL *flushMIDIQueue)(mt32emu_const_context context); \
	mt32emu_bit32u (MT32EMU_C_CALL *setMIDIEventQueueSize)(mt32emu_const_context context, const mt32emu_bit32u queue_size); \
	void (MT32EMU_C_CALL *setMIDIReceiver)(mt32emu_context context, mt32emu_midi_receiver_i midi_receiver, void *instance_data); \
\
	void (MT32EMU_C_CALL *parseStream)(mt32emu_const_context context, const mt32emu_bit8u *stream, mt32emu_bit32u length); \
	void (MT32EMU_C_CALL *parseStream_At)(mt32emu_const_context context, const mt32emu_bit8u *stream, mt32emu_bit32u length, mt32emu_bit32u timestamp); \
	void (MT32EMU_C_CALL *playShortMessage)(mt32emu_const_context context, mt32emu_bit32u message); \
	void (MT32EMU_C_CALL *playShortMessageAt)(mt32emu_const_context context, mt32emu_bit32u message, mt32emu_bit32u timestamp); \
	mt32emu_return_code (MT32EMU_C_CALL *playMsg)(mt32emu_const_context context, mt32emu_bit32u msg); \
	mt32emu_return_code (MT32EMU_C_CALL *playSysex)(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len); \
	mt32emu_return_code (MT32EMU_C_CALL *playMsgAt)(mt32emu_const_context context, mt32emu_bit32u msg, mt32emu_bit32u timestamp); \
	mt32emu_return_code (MT32EMU_C_CALL *playSysexAt)(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len, mt32emu_bit32u timestamp); \
\
	void (MT32EMU_C_CALL *playMsgNow)(mt32emu_const_context context, mt32emu_bit32u msg); \
	void (MT32EMU_C_CALL *playMsgOnPart)(mt32emu_const_context context, mt32emu_bit8u part, mt32emu_bit8u code, mt32emu_bit8u note, mt32emu_bit8u velocity); \
	void (MT32EMU_C_CALL *playSysexNow)(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len); \
	void (MT32EMU_C_CALL *writeSysex)(mt32emu_const_context context, mt32emu_bit8u channel, const mt32emu_bit8u *sysex, mt32emu_bit32u len); \
\
	void (MT32EMU_C_CALL *setReverbEnabled)(mt32emu_const_context context, const mt32emu_boolean reverb_enabled); \
	mt32emu_boolean (MT32EMU_C_CALL *isReverbEnabled)(mt32emu_const_context context); \
	void (MT32EMU_C_CALL *setReverbOverridden)(mt32emu_const_context context, const mt32emu_boolean reverb_overridden); \
	mt32emu_boolean (MT32EMU_C_CALL *isReverbOverridden)(mt32emu_const_context context); \
	void (MT32EMU_C_CALL *setReverbCompatibilityMode)(mt32emu_const_context context, const mt32emu_boolean mt32_compatible_mode); \
	mt32emu_boolean (MT32EMU_C_CALL *isMT32ReverbCompatibilityMode)(mt32emu_const_context context); \
	mt32emu_boolean (MT32EMU_C_CALL *isDefaultReverbMT32Compatible)(mt32emu_const_context context); \
\
	void (MT32EMU_C_CALL *setDACInputMode)(mt32emu_const_context context, const mt32emu_dac_input_mode mode); \
	mt32emu_dac_input_mode (MT32EMU_C_CALL *getDACInputMode)(mt32emu_const_context context); \
\
	void (MT32EMU_C_CALL *setMIDIDelayMode)(mt32emu_const_context context, const mt32emu_midi_delay_mode mode); \
	mt32emu_midi_delay_mode (MT32EMU_C_CALL *getMIDIDelayMode)(mt32emu_const_context context); \
\
	void (MT32EMU_C_CALL *setOutputGain)(mt32emu_const_context context, float gain); \
	float (MT32EMU_C_CALL *getOutputGain)(mt32emu_const_context context); \
	void (MT32EMU_C_CALL *setReverbOutputGain)(mt32emu_const_context context, float gain); \
	float (MT32EMU_C_CALL *getReverbOutputGain)(mt32emu_const_context context); \
\
	void (MT32EMU_C_CALL *setReversedStereoEnabled)(mt32emu_const_context context, const mt32emu_boolean enabled); \
	mt32emu_boolean (MT32EMU_C_CALL *isReversedStereoEnabled)(mt32emu_const_context context); \
\
	void (MT32EMU_C_CALL *renderBit16s)(mt32emu_const_context context, mt32emu_bit16s *stream, mt32emu_bit32u len); \
	void (MT32EMU_C_CALL *renderFloat)(mt32emu_const_context context, float *stream, mt32emu_bit32u len); \
	void (MT32EMU_C_CALL *renderBit16sStreams)(mt32emu_const_context context, const mt32emu_dac_output_bit16s_streams *streams, mt32emu_bit32u len); \
	void (MT32EMU_C_CALL *renderFloatStreams)(mt32emu_const_context context, const mt32emu_dac_output_float_streams *streams, mt32emu_bit32u len); \
\
	mt32emu_boolean (MT32EMU_C_CALL *hasActivePartials)(mt32emu_const_context context); \
	mt32emu_boolean (MT32EMU_C_CALL *isActive)(mt32emu_const_context context); \
	mt32emu_bit32u (MT32EMU_C_CALL *getPartialCount)(mt32emu_const_context context); \
	mt32emu_bit32u (MT32EMU_C_CALL *getPartStates)(mt32emu_const_context context); \
	void (MT32EMU_C_CALL *getPartialStates)(mt32emu_const_context context, mt32emu_bit8u *partial_states); \
	mt32emu_bit32u (MT32EMU_C_CALL *getPlayingNotes)(mt32emu_const_context context, mt32emu_bit8u part_number, mt32emu_bit8u *keys, mt32emu_bit8u *velocities); \
	const char *(MT32EMU_C_CALL *getPatchName)(mt32emu_const_context context, mt32emu_bit8u part_number); \
	void (MT32EMU_C_CALL *readMemory)(mt32emu_const_context context, mt32emu_bit32u addr, mt32emu_bit32u len, mt32emu_bit8u *data);

#define MT32EMU_SERVICE_I_V1 \
	mt32emu_analog_output_mode (MT32EMU_C_CALL *getBestAnalogOutputMode)(const double target_samplerate); \
	void (MT32EMU_C_CALL *setStereoOutputSampleRate)(mt32emu_context context, const double samplerate); \
	void (MT32EMU_C_CALL *setSamplerateConversionQuality)(mt32emu_context context, const mt32emu_samplerate_conversion_quality quality); \
	void (MT32EMU_C_CALL *selectRendererType)(mt32emu_context context, mt32emu_renderer_type renderer_type); \
	mt32emu_renderer_type (MT32EMU_C_CALL *getSelectedRendererType)(mt32emu_context context); \
	mt32emu_bit32u (MT32EMU_C_CALL *convertOutputToSynthTimestamp)(mt32emu_const_context context, mt32emu_bit32u output_timestamp); \
	mt32emu_bit32u (MT32EMU_C_CALL *convertSynthToOutputTimestamp)(mt32emu_const_context context, mt32emu_bit32u synth_timestamp);

#define MT32EMU_SERVICE_I_V2 \
	mt32emu_bit32u (MT32EMU_C_CALL *getInternalRenderedSampleCount)(mt32emu_const_context context); \
	void (MT32EMU_C_CALL *setNiceAmpRampEnabled)(mt32emu_const_context context, const mt32emu_boolean enabled); \
	mt32emu_boolean (MT32EMU_C_CALL *isNiceAmpRampEnabled)(mt32emu_const_context context);

#define MT32EMU_SERVICE_I_V3 \
	void (MT32EMU_C_CALL *setNicePanningEnabled)(mt32emu_const_context context, const mt32emu_boolean enabled); \
	mt32emu_boolean (MT32EMU_C_CALL *isNicePanningEnabled)(mt32emu_const_context context); \
	void (MT32EMU_C_CALL *setNicePartialMixingEnabled)(mt32emu_const_context context, const mt32emu_boolean enabled); \
	mt32emu_boolean (MT32EMU_C_CALL *isNicePartialMixingEnabled)(mt32emu_const_context context); \
	void (MT32EMU_C_CALL *preallocateReverbMemory)(mt32emu_const_context context, const mt32emu_boolean enabled); \
	void (MT32EMU_C_CALL *configureMIDIEventQueueSysexStorage)(mt32emu_const_context context, const mt32emu_bit32u storage_buffer_size);

#define MT32EMU_SERVICE_I_V4 \
	size_t (MT32EMU_C_CALL *getMachineIDs)(const char **machine_ids, size_t machine_ids_size); \
	size_t (MT32EMU_C_CALL *getROMIDs)(const char **rom_ids, size_t rom_ids_size, const char *machine_id); \
	mt32emu_return_code (MT32EMU_C_CALL *identifyROMData)(mt32emu_rom_info *rom_info, const mt32emu_bit8u *data, size_t data_size, const char *machine_id); \
	mt32emu_return_code (MT32EMU_C_CALL *identifyROMFile)(mt32emu_rom_info *rom_info, const char *filename, const char *machine_id); \
\
	mt32emu_return_code (MT32EMU_C_CALL *mergeAndAddROMData)(mt32emu_context context, const mt32emu_bit8u *part1_data, size_t part1_data_size, const mt32emu_sha1_digest *part1_sha1_digest, const mt32emu_bit8u *part2_data, size_t part2_data_size, const mt32emu_sha1_digest *part2_sha1_digest); \
	mt32emu_return_code (MT32EMU_C_CALL *mergeAndAddROMFiles)(mt32emu_context context, const char *part1_filename, const char *part2_filename); \
	mt32emu_return_code (MT32EMU_C_CALL *addMachineROMFile)(mt32emu_context context, const char *machine_id, const char *filename);

#define MT32EMU_SERVICE_I_V5 \
	mt32emu_boolean (MT32EMU_C_CALL *getDisplayState)(mt32emu_const_context context, char *target_buffer, const mt32emu_boolean narrow_lcd); \
	void (MT32EMU_C_CALL *setMainDisplayMode)(mt32emu_const_context context); \
	void (MT32EMU_C_CALL *setDisplayCompatibility)(mt32emu_const_context context, mt32emu_boolean old_mt32_compatibility_enabled); \
	mt32emu_boolean (MT32EMU_C_CALL *isDisplayOldMT32Compatible)(mt32emu_const_context context); \
	mt32emu_boolean (MT32EMU_C_CALL *isDefaultDisplayOldMT32Compatible)(mt32emu_const_context context); \
	void (MT32EMU_C_CALL *setPartVolumeOverride)(mt32emu_const_context context, mt32emu_bit8u part_number, mt32emu_bit8u volume_override); \
	mt32emu_bit8u (MT32EMU_C_CALL *getPartVolumeOverride)(mt32emu_const_context context, mt32emu_bit8u part_number);

#define MT32EMU_SERVICE_I_V6 \
	mt32emu_boolean (MT32EMU_C_CALL *getSoundGroupName)(mt32emu_const_context context, char *sound_group_name, mt32emu_bit8u timbre_group, mt32emu_bit8u timbre_number); \
	mt32emu_boolean (MT32EMU_C_CALL *getSoundName)(mt32emu_const_context context, char *sound_name, mt32emu_bit8u timbre_group, mt32emu_bit8u timbre_number);

typedef struct {
	MT32EMU_SERVICE_I_V0
} mt32emu_service_i_v0;

typedef struct {
	MT32EMU_SERVICE_I_V0
	MT32EMU_SERVICE_I_V1
} mt32emu_service_i_v1;

typedef struct {
	MT32EMU_SERVICE_I_V0
	MT32EMU_SERVICE_I_V1
	MT32EMU_SERVICE_I_V2
} mt32emu_service_i_v2;

typedef struct {
	MT32EMU_SERVICE_I_V0
	MT32EMU_SERVICE_I_V1
	MT32EMU_SERVICE_I_V2
	MT32EMU_SERVICE_I_V3
} mt32emu_service_i_v3;

typedef struct {
	MT32EMU_SERVICE_I_V0
	MT32EMU_SERVICE_I_V1
	MT32EMU_SERVICE_I_V2
	MT32EMU_SERVICE_I_V3
	MT32EMU_SERVICE_I_V4
} mt32emu_service_i_v4;

typedef struct {
	MT32EMU_SERVICE_I_V0
	MT32EMU_SERVICE_I_V1
	MT32EMU_SERVICE_I_V2
	MT32EMU_SERVICE_I_V3
	MT32EMU_SERVICE_I_V4
	MT32EMU_SERVICE_I_V5
} mt32emu_service_i_v5;

typedef struct {
	MT32EMU_SERVICE_I_V0
	MT32EMU_SERVICE_I_V1
	MT32EMU_SERVICE_I_V2
	MT32EMU_SERVICE_I_V3
	MT32EMU_SERVICE_I_V4
	MT32EMU_SERVICE_I_V5
	MT32EMU_SERVICE_I_V6
} mt32emu_service_i_v6;

/**
 * Extensible interface for all the library services.
 * Union intended to view an interface of any subsequent version as any parent interface not requiring a cast.
 * It is caller's responsibility to check the actual interface version in runtime using the getVersionID() method.
 */
union mt32emu_service_i {
	const mt32emu_service_i_v0 *v0;
	const mt32emu_service_i_v1 *v1;
	const mt32emu_service_i_v2 *v2;
	const mt32emu_service_i_v3 *v3;
	const mt32emu_service_i_v4 *v4;
	const mt32emu_service_i_v5 *v5;
	const mt32emu_service_i_v6 *v6;
};

#undef MT32EMU_SERVICE_I_V0
#undef MT32EMU_SERVICE_I_V1
#undef MT32EMU_SERVICE_I_V2
#undef MT32EMU_SERVICE_I_V3
#undef MT32EMU_SERVICE_I_V4
#undef MT32EMU_SERVICE_I_V5
#undef MT32EMU_SERVICE_I_V6

#endif /* #ifndef MT32EMU_C_TYPES_H */
