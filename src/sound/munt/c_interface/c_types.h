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

#ifndef MT32EMU_C_TYPES_H
#define MT32EMU_C_TYPES_H

#include <stdarg.h>
#include <stddef.h>

#include "../globals.h"

#define MT32EMU_C_ENUMERATIONS
#include "../Enumerations.h"
#undef MT32EMU_C_ENUMERATIONS

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

	/* Definite error occurred. */
	MT32EMU_RC_ROM_NOT_IDENTIFIED = -1,
	MT32EMU_RC_FILE_NOT_FOUND = -2,
	MT32EMU_RC_FILE_NOT_LOADED = -3,
	MT32EMU_RC_MISSING_ROMS = -4,
	MT32EMU_RC_NOT_OPENED = -5,
	MT32EMU_RC_QUEUE_FULL = -6,

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
	MT32EMU_REPORT_HANDLER_VERSION_CURRENT = MT32EMU_REPORT_HANDLER_VERSION_0
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
	MT32EMU_SERVICE_VERSION_CURRENT = MT32EMU_SERVICE_VERSION_3
} mt32emu_service_version;

/* === Report Handler Interface === */

typedef union mt32emu_report_handler_i mt32emu_report_handler_i;

/** Interface for handling reported events (initial version) */
typedef struct {
	/** Returns the actual interface version ID */
	mt32emu_report_handler_version (*getVersionID)(mt32emu_report_handler_i i);

	/** Callback for debug messages, in vprintf() format */
	void (*printDebug)(void *instance_data, const char *fmt, va_list list);
	/** Callbacks for reporting errors */
	void (*onErrorControlROM)(void *instance_data);
	void (*onErrorPCMROM)(void *instance_data);
	/** Callback for reporting about displaying a new custom message on LCD */
	void (*showLCDMessage)(void *instance_data, const char *message);
	/** Callback for reporting actual processing of a MIDI message */
	void (*onMIDIMessagePlayed)(void *instance_data);
	/**
	 * Callback for reporting an overflow of the input MIDI queue.
	 * Returns MT32EMU_BOOL_TRUE if a recovery action was taken
	 * and yet another attempt to enqueue the MIDI event is desired.
	 */
	mt32emu_boolean (*onMIDIQueueOverflow)(void *instance_data);
	/**
	 * Callback invoked when a System Realtime MIDI message is detected in functions
	 * mt32emu_parse_stream and mt32emu_play_short_message and the likes.
	 */
	void (*onMIDISystemRealtime)(void *instance_data, mt32emu_bit8u system_realtime);
	/** Callbacks for reporting system events */
	void (*onDeviceReset)(void *instance_data);
	void (*onDeviceReconfig)(void *instance_data);
	/** Callbacks for reporting changes of reverb settings */
	void (*onNewReverbMode)(void *instance_data, mt32emu_bit8u mode);
	void (*onNewReverbTime)(void *instance_data, mt32emu_bit8u time);
	void (*onNewReverbLevel)(void *instance_data, mt32emu_bit8u level);
	/** Callbacks for reporting various information */
	void (*onPolyStateChanged)(void *instance_data, mt32emu_bit8u part_num);
	void (*onProgramChanged)(void *instance_data, mt32emu_bit8u part_num, const char *sound_group_name, const char *patch_name);
} mt32emu_report_handler_i_v0;

/**
 * Extensible interface for handling reported events.
 * Union intended to view an interface of any subsequent version as any parent interface not requiring a cast.
 * It is caller's responsibility to check the actual interface version in runtime using the getVersionID() method.
 */
union mt32emu_report_handler_i {
	const mt32emu_report_handler_i_v0 *v0;
};

/* === MIDI Receiver Interface === */

typedef union mt32emu_midi_receiver_i mt32emu_midi_receiver_i;

/** Interface for receiving MIDI messages generated by MIDI stream parser (initial version) */
typedef struct {
	/** Returns the actual interface version ID */
	mt32emu_midi_receiver_version (*getVersionID)(mt32emu_midi_receiver_i i);

	/** Invoked when a complete short MIDI message is parsed in the input MIDI stream. */
	void (*handleShortMessage)(void *instance_data, const mt32emu_bit32u message);

	/** Invoked when a complete well-formed System Exclusive MIDI message is parsed in the input MIDI stream. */
	void (*handleSysex)(void *instance_data, const mt32emu_bit8u stream[], const mt32emu_bit32u length);

	/** Invoked when a System Realtime MIDI message is parsed in the input MIDI stream. */
	void (*handleSystemRealtimeMessage)(void *instance_data, const mt32emu_bit8u realtime);
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
	mt32emu_service_version (*getVersionID)(mt32emu_service_i i); \
	mt32emu_report_handler_version (*getSupportedReportHandlerVersionID)(void); \
	mt32emu_midi_receiver_version (*getSupportedMIDIReceiverVersionID)(void); \
\
	mt32emu_bit32u (*getLibraryVersionInt)(void); \
	const char *(*getLibraryVersionString)(void); \
\
	mt32emu_bit32u (*getStereoOutputSamplerate)(const mt32emu_analog_output_mode analog_output_mode); \
\
	mt32emu_context (*createContext)(mt32emu_report_handler_i report_handler, void *instance_data); \
	void (*freeContext)(mt32emu_context context); \
	mt32emu_return_code (*addROMData)(mt32emu_context context, const mt32emu_bit8u *data, size_t data_size, const mt32emu_sha1_digest *sha1_digest); \
	mt32emu_return_code (*addROMFile)(mt32emu_context context, const char *filename); \
	void (*getROMInfo)(mt32emu_const_context context, mt32emu_rom_info *rom_info); \
	void (*setPartialCount)(mt32emu_context context, const mt32emu_bit32u partial_count); \
	void (*setAnalogOutputMode)(mt32emu_context context, const mt32emu_analog_output_mode analog_output_mode); \
	mt32emu_return_code (*openSynth)(mt32emu_const_context context); \
	void (*closeSynth)(mt32emu_const_context context); \
	mt32emu_boolean (*isOpen)(mt32emu_const_context context); \
	mt32emu_bit32u (*getActualStereoOutputSamplerate)(mt32emu_const_context context); \
	void (*flushMIDIQueue)(mt32emu_const_context context); \
	mt32emu_bit32u (*setMIDIEventQueueSize)(mt32emu_const_context context, const mt32emu_bit32u queue_size); \
	void (*setMIDIReceiver)(mt32emu_context context, mt32emu_midi_receiver_i midi_receiver, void *instance_data); \
\
	void (*parseStream)(mt32emu_const_context context, const mt32emu_bit8u *stream, mt32emu_bit32u length); \
	void (*parseStream_At)(mt32emu_const_context context, const mt32emu_bit8u *stream, mt32emu_bit32u length, mt32emu_bit32u timestamp); \
	void (*playShortMessage)(mt32emu_const_context context, mt32emu_bit32u message); \
	void (*playShortMessageAt)(mt32emu_const_context context, mt32emu_bit32u message, mt32emu_bit32u timestamp); \
	mt32emu_return_code (*playMsg)(mt32emu_const_context context, mt32emu_bit32u msg); \
	mt32emu_return_code (*playSysex)(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len); \
	mt32emu_return_code (*playMsgAt)(mt32emu_const_context context, mt32emu_bit32u msg, mt32emu_bit32u timestamp); \
	mt32emu_return_code (*playSysexAt)(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len, mt32emu_bit32u timestamp); \
\
	void (*playMsgNow)(mt32emu_const_context context, mt32emu_bit32u msg); \
	void (*playMsgOnPart)(mt32emu_const_context context, mt32emu_bit8u part, mt32emu_bit8u code, mt32emu_bit8u note, mt32emu_bit8u velocity); \
	void (*playSysexNow)(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len); \
	void (*writeSysex)(mt32emu_const_context context, mt32emu_bit8u channel, const mt32emu_bit8u *sysex, mt32emu_bit32u len); \
\
	void (*setReverbEnabled)(mt32emu_const_context context, const mt32emu_boolean reverb_enabled); \
	mt32emu_boolean (*isReverbEnabled)(mt32emu_const_context context); \
	void (*setReverbOverridden)(mt32emu_const_context context, const mt32emu_boolean reverb_overridden); \
	mt32emu_boolean (*isReverbOverridden)(mt32emu_const_context context); \
	void (*setReverbCompatibilityMode)(mt32emu_const_context context, const mt32emu_boolean mt32_compatible_mode); \
	mt32emu_boolean (*isMT32ReverbCompatibilityMode)(mt32emu_const_context context); \
	mt32emu_boolean (*isDefaultReverbMT32Compatible)(mt32emu_const_context context); \
\
	void (*setDACInputMode)(mt32emu_const_context context, const mt32emu_dac_input_mode mode); \
	mt32emu_dac_input_mode (*getDACInputMode)(mt32emu_const_context context); \
\
	void (*setMIDIDelayMode)(mt32emu_const_context context, const mt32emu_midi_delay_mode mode); \
	mt32emu_midi_delay_mode (*getMIDIDelayMode)(mt32emu_const_context context); \
\
	void (*setOutputGain)(mt32emu_const_context context, float gain); \
	float (*getOutputGain)(mt32emu_const_context context); \
	void (*setReverbOutputGain)(mt32emu_const_context context, float gain); \
	float (*getReverbOutputGain)(mt32emu_const_context context); \
\
	void (*setReversedStereoEnabled)(mt32emu_const_context context, const mt32emu_boolean enabled); \
	mt32emu_boolean (*isReversedStereoEnabled)(mt32emu_const_context context); \
\
	void (*renderBit16s)(mt32emu_const_context context, mt32emu_bit16s *stream, mt32emu_bit32u len); \
	void (*renderFloat)(mt32emu_const_context context, float *stream, mt32emu_bit32u len); \
	void (*renderBit16sStreams)(mt32emu_const_context context, const mt32emu_dac_output_bit16s_streams *streams, mt32emu_bit32u len); \
	void (*renderFloatStreams)(mt32emu_const_context context, const mt32emu_dac_output_float_streams *streams, mt32emu_bit32u len); \
\
	mt32emu_boolean (*hasActivePartials)(mt32emu_const_context context); \
	mt32emu_boolean (*isActive)(mt32emu_const_context context); \
	mt32emu_bit32u (*getPartialCount)(mt32emu_const_context context); \
	mt32emu_bit32u (*getPartStates)(mt32emu_const_context context); \
	void (*getPartialStates)(mt32emu_const_context context, mt32emu_bit8u *partial_states); \
	mt32emu_bit32u (*getPlayingNotes)(mt32emu_const_context context, mt32emu_bit8u part_number, mt32emu_bit8u *keys, mt32emu_bit8u *velocities); \
	const char *(*getPatchName)(mt32emu_const_context context, mt32emu_bit8u part_number); \
	void (*readMemory)(mt32emu_const_context context, mt32emu_bit32u addr, mt32emu_bit32u len, mt32emu_bit8u *data);

#define MT32EMU_SERVICE_I_V1 \
	mt32emu_analog_output_mode (*getBestAnalogOutputMode)(const double target_samplerate); \
	void (*setStereoOutputSampleRate)(mt32emu_context context, const double samplerate); \
	void (*setSamplerateConversionQuality)(mt32emu_context context, const mt32emu_samplerate_conversion_quality quality); \
	void (*selectRendererType)(mt32emu_context context, mt32emu_renderer_type renderer_type); \
	mt32emu_renderer_type (*getSelectedRendererType)(mt32emu_context context); \
	mt32emu_bit32u (*convertOutputToSynthTimestamp)(mt32emu_const_context context, mt32emu_bit32u output_timestamp); \
	mt32emu_bit32u (*convertSynthToOutputTimestamp)(mt32emu_const_context context, mt32emu_bit32u synth_timestamp);

#define MT32EMU_SERVICE_I_V2 \
	mt32emu_bit32u (*getInternalRenderedSampleCount)(mt32emu_const_context context); \
	void (*setNiceAmpRampEnabled)(mt32emu_const_context context, const mt32emu_boolean enabled); \
	mt32emu_boolean (*isNiceAmpRampEnabled)(mt32emu_const_context context);

#define MT32EMU_SERVICE_I_V3 \
	void (*setNicePanningEnabled)(mt32emu_const_context context, const mt32emu_boolean enabled); \
	mt32emu_boolean (*isNicePanningEnabled)(mt32emu_const_context context); \
	void (*setNicePartialMixingEnabled)(mt32emu_const_context context, const mt32emu_boolean enabled); \
	mt32emu_boolean (*isNicePartialMixingEnabled)(mt32emu_const_context context); \
	void (*preallocateReverbMemory)(mt32emu_const_context context, const mt32emu_boolean enabled); \
	void (*configureMIDIEventQueueSysexStorage)(mt32emu_const_context context, const mt32emu_bit32u storage_buffer_size);

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
};

#undef MT32EMU_SERVICE_I_V0
#undef MT32EMU_SERVICE_I_V1
#undef MT32EMU_SERVICE_I_V2
#undef MT32EMU_SERVICE_I_V3

#endif /* #ifndef MT32EMU_C_TYPES_H */
