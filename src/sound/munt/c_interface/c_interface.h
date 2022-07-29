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

#ifndef MT32EMU_C_INTERFACE_H
#define MT32EMU_C_INTERFACE_H

#include <stddef.h>

#include "../globals.h"
#include "c_types.h"

#undef MT32EMU_EXPORT
#undef MT32EMU_EXPORT_V
#define MT32EMU_EXPORT MT32EMU_EXPORT_ATTRIBUTE
#define MT32EMU_EXPORT_V(symbol_version_tag) MT32EMU_EXPORT

#ifdef __cplusplus
extern "C" {
#endif

/* == Context-independent functions == */

/* === Interface handling === */

/** Returns mt32emu_service_i interface. */
MT32EMU_EXPORT mt32emu_service_i MT32EMU_C_CALL mt32emu_get_service_i(void);

#if MT32EMU_EXPORTS_TYPE == 2
#undef MT32EMU_EXPORT
#undef MT32EMU_EXPORT_V
#define MT32EMU_EXPORT
#define MT32EMU_EXPORT_V(symbol_version_tag) MT32EMU_EXPORT
#endif

/**
 * Returns the version ID of mt32emu_report_handler_i interface the library has been compiled with.
 * This allows a client to fall-back gracefully instead of silently not receiving expected event reports.
 */
MT32EMU_EXPORT mt32emu_report_handler_version MT32EMU_C_CALL mt32emu_get_supported_report_handler_version(void);

/**
 * Returns the version ID of mt32emu_midi_receiver_version_i interface the library has been compiled with.
 * This allows a client to fall-back gracefully instead of silently not receiving expected MIDI messages.
 */
MT32EMU_EXPORT mt32emu_midi_receiver_version MT32EMU_C_CALL mt32emu_get_supported_midi_receiver_version(void);

/* === Utility === */

/**
 * Returns library version as an integer in format: 0x00MMmmpp, where:
 * MM - major version number
 * mm - minor version number
 * pp - patch number
 */
MT32EMU_EXPORT mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_library_version_int(void);

/**
 * Returns library version as a C-string in format: "MAJOR.MINOR.PATCH".
 */
MT32EMU_EXPORT const char * MT32EMU_C_CALL mt32emu_get_library_version_string(void);

/**
 * Returns output sample rate used in emulation of stereo analog circuitry of hardware units for particular analog_output_mode.
 * See comment for mt32emu_analog_output_mode.
 */
MT32EMU_EXPORT mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_stereo_output_samplerate(const mt32emu_analog_output_mode analog_output_mode);

/**
 * Returns the value of analog_output_mode for which the output signal may retain its full frequency spectrum
 * at the sample rate specified by the target_samplerate argument.
 * See comment for mt32emu_analog_output_mode.
 */
MT32EMU_EXPORT mt32emu_analog_output_mode MT32EMU_C_CALL mt32emu_get_best_analog_output_mode(const double target_samplerate);

/* === ROM handling === */

/**
 * Retrieves a list of identifiers (as C-strings) of supported machines. Argument machine_ids points to the array of size
 * machine_ids_size to be filled.
 * Returns the number of identifiers available for retrieval. The size of the target array to be allocated can be found
 * by passing NULL in argument machine_ids; argument machine_ids_size is ignored in this case.
 */
MT32EMU_EXPORT_V(2.5) size_t MT32EMU_C_CALL mt32emu_get_machine_ids(const char **machine_ids, size_t machine_ids_size);
/**
 * Retrieves a list of identifiers (as C-strings) of supported ROM images. Argument rom_ids points to the array of size
 * rom_ids_size to be filled. Optional argument machine_id can be used to indicate a specific machine to retrieve ROM identifiers
 * for; if NULL, identifiers of all the ROM images supported by the emulation engine are retrieved.
 * Returns the number of ROM identifiers available for retrieval. The size of the target array to be allocated can be found
 * by passing NULL in argument rom_ids; argument rom_ids_size is ignored in this case. If argument machine_id contains
 * an unrecognised value, 0 is returned.
 */
MT32EMU_EXPORT_V(2.5) size_t MT32EMU_C_CALL mt32emu_get_rom_ids(const char **rom_ids, size_t rom_ids_size, const char *machine_id);

/**
 * Identifies a ROM image the provided data array contains by its SHA1 digest. Optional argument machine_id can be used to indicate
 * a specific machine to identify the ROM image for; if NULL, the ROM image is identified for any supported machine.
 * A mt32emu_rom_info structure supplied in argument rom_info is filled in accordance with the provided ROM image; unused fields
 * are filled with NULLs. If the content of the ROM image is not identified successfully (e.g. when the ROM image is incompatible
 * with the specified machine), all fields of rom_info are filled with NULLs.
 * Returns MT32EMU_RC_OK upon success or a negative error code otherwise.
 */
MT32EMU_EXPORT_V(2.5) mt32emu_return_code MT32EMU_C_CALL mt32emu_identify_rom_data(mt32emu_rom_info *rom_info, const mt32emu_bit8u *data, size_t data_size, const char *machine_id);
/**
 * Loads the content of the file specified by argument filename and identifies a ROM image the file contains by its SHA1 digest.
 * Optional argument machine_id can be used to indicate a specific machine to identify the ROM image for; if NULL, the ROM image
 * is identified for any supported machine.
 * A mt32emu_rom_info structure supplied in argument rom_info is filled in accordance with the provided ROM image; unused fields
 * are filled with NULLs. If the content of the file is not identified successfully (e.g. when the ROM image is incompatible
 * with the specified machine), all fields of rom_info are filled with NULLs.
 * Returns MT32EMU_RC_OK upon success or a negative error code otherwise.
 */
MT32EMU_EXPORT_V(2.5) mt32emu_return_code MT32EMU_C_CALL mt32emu_identify_rom_file(mt32emu_rom_info *rom_info, const char *filename, const char *machine_id);

/* == Context-dependent functions == */

/** Initialises a new emulation context and installs custom report handler if non-NULL. */
MT32EMU_EXPORT mt32emu_context MT32EMU_C_CALL mt32emu_create_context(mt32emu_report_handler_i report_handler, void *instance_data);

/** Closes and destroys emulation context. */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_free_context(mt32emu_context context);

/**
 * Adds a new full ROM data image identified by its SHA1 digest to the emulation context replacing previously added ROM of the same
 * type if any. Argument sha1_digest can be NULL, in this case the digest will be computed using the actual ROM data.
 * If sha1_digest is set to non-NULL, it is assumed being correct and will not be recomputed.
 * The provided data array is NOT copied and used directly for efficiency. The caller should not deallocate it while the emulation
 * context is referring to the ROM data.
 * This function doesn't immediately change the state of already opened synth. Newly added ROM will take effect upon next call of
 * mt32emu_open_synth().
 * Returns positive value upon success.
 */
MT32EMU_EXPORT mt32emu_return_code MT32EMU_C_CALL mt32emu_add_rom_data(mt32emu_context context, const mt32emu_bit8u *data, size_t data_size, const mt32emu_sha1_digest *sha1_digest);

/**
 * Loads a ROM file that contains a full ROM data image, identifies it by the SHA1 digest, and adds it to the emulation context
 * replacing previously added ROM of the same type if any.
 * This function doesn't immediately change the state of already opened synth. Newly added ROM will take effect upon next call of
 * mt32emu_open_synth().
 * Returns positive value upon success.
 */
MT32EMU_EXPORT mt32emu_return_code MT32EMU_C_CALL mt32emu_add_rom_file(mt32emu_context context, const char *filename);

/**
 * Merges a pair of compatible ROM data image parts into a full image and adds it to the emulation context replacing previously
 * added ROM of the same type if any. Each partial image is identified by its SHA1 digest. Arguments partN_sha1_digest can be NULL,
 * in this case the digest will be computed using the actual ROM data. If a non-NULL SHA1 value is provided, it is assumed being
 * correct and will not be recomputed. The provided data arrays may be deallocated as soon as the function completes.
 * This function doesn't immediately change the state of already opened synth. Newly added ROM will take effect upon next call of
 * mt32emu_open_synth().
 * Returns positive value upon success.
 */
MT32EMU_EXPORT_V(2.5) mt32emu_return_code MT32EMU_C_CALL mt32emu_merge_and_add_rom_data(mt32emu_context context, const mt32emu_bit8u *part1_data, size_t part1_data_size, const mt32emu_sha1_digest *part1_sha1_digest, const mt32emu_bit8u *part2_data, size_t part2_data_size, const mt32emu_sha1_digest *part2_sha1_digest);

/**
 * Loads a pair of files that contains compatible parts of a full ROM image, identifies them by the SHA1 digest, merges these
 * parts into a full ROM image and adds it to the emulation context replacing previously added ROM of the same type if any.
 * This function doesn't immediately change the state of already opened synth. Newly added ROM will take effect upon next call of
 * mt32emu_open_synth().
 * Returns positive value upon success.
 */
MT32EMU_EXPORT_V(2.5) mt32emu_return_code MT32EMU_C_CALL mt32emu_merge_and_add_rom_files(mt32emu_context context, const char *part1_filename, const char *part2_filename);

/**
 * Loads a file that contains a ROM image of a specific machine, identifies it by the SHA1 digest, and adds it to the emulation
 * context. The ROM image can only be identified successfully if it is compatible with the specified machine.
 * Full and partial ROM images are supported and handled according to the following rules:
 * - a file with any compatible ROM image is added if none (of the same type) exists in the emulation context;
 * - a file with any compatible ROM image replaces any image of the same type that is incompatible with the specified machine;
 * - a file with a full ROM image replaces the previously added partial ROM of the same type;
 * - a file with a partial ROM image is merged with the previously added ROM image if pairable;
 * - otherwise, the file is ignored.
 * The described behaviour allows the caller application to traverse a directory with ROM files attempting to add each one in turn.
 * As soon as both the full control and the full PCM ROM images are added and / or merged, the iteration can be stopped.
 * This function doesn't immediately change the state of already opened synth. Newly added ROMs will take effect upon next call of
 * mt32emu_open_synth().
 * Returns a positive value in case changes have been made, MT32EMU_RC_OK if the file has been ignored or a negative error code
 * upon failure.
 */
MT32EMU_EXPORT_V(2.5) mt32emu_return_code MT32EMU_C_CALL mt32emu_add_machine_rom_file(mt32emu_context context, const char *machine_id, const char *filename);

/**
 * Fills in mt32emu_rom_info structure with identifiers and descriptions of control and PCM ROM files identified and added to the synth context.
 * If one of the ROM files is not loaded and identified yet, NULL is returned in the corresponding fields of the mt32emu_rom_info structure.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_get_rom_info(mt32emu_const_context context, mt32emu_rom_info *rom_info);

/**
 * Allows to override the default maximum number of partials playing simultaneously within the emulation session.
 * This function doesn't immediately change the state of already opened synth. Newly set value will take effect upon next call of mt32emu_open_synth().
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_partial_count(mt32emu_context context, const mt32emu_bit32u partial_count);

/**
 * Allows to override the default mode for emulation of analogue circuitry of the hardware units within the emulation session.
 * This function doesn't immediately change the state of already opened synth. Newly set value will take effect upon next call of mt32emu_open_synth().
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_analog_output_mode(mt32emu_context context, const mt32emu_analog_output_mode analog_output_mode);

/**
 * Allows to convert the synthesiser output to any desired sample rate. The samplerate conversion
 * processes the completely mixed stereo output signal as it passes the analogue circuit emulation,
 * so emulating the synthesiser output signal passing further through an ADC. When the samplerate
 * argument is set to 0, the default output sample rate is used which depends on the current
 * mode of analog circuitry emulation. See mt32emu_analog_output_mode.
 * This function doesn't immediately change the state of already opened synth.
 * Newly set value will take effect upon next call of mt32emu_open_synth().
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_stereo_output_samplerate(mt32emu_context context, const double samplerate);

/**
 * Several samplerate conversion quality options are provided which allow to trade-off the conversion speed vs.
 * the retained passband width. All the options except FASTEST guarantee full suppression of the aliasing noise
 * in terms of the 16-bit integer samples.
 * This function doesn't immediately change the state of already opened synth.
 * Newly set value will take effect upon next call of mt32emu_open_synth().
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_samplerate_conversion_quality(mt32emu_context context, const mt32emu_samplerate_conversion_quality quality);

/**
 * Selects new type of the wave generator and renderer to be used during subsequent calls to mt32emu_open_synth().
 * By default, MT32EMU_RT_BIT16S is selected.
 * See mt32emu_renderer_type for details.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_select_renderer_type(mt32emu_context context, const mt32emu_renderer_type renderer_type);

/**
 * Returns previously selected type of the wave generator and renderer.
 * See mt32emu_renderer_type for details.
 */
MT32EMU_EXPORT mt32emu_renderer_type MT32EMU_C_CALL mt32emu_get_selected_renderer_type(mt32emu_context context);

/**
 * Prepares the emulation context to receive MIDI messages and produce output audio data using aforehand added set of ROMs,
 * and optionally set the maximum partial count and the analog output mode.
 * Returns MT32EMU_RC_OK upon success.
 */
MT32EMU_EXPORT mt32emu_return_code MT32EMU_C_CALL mt32emu_open_synth(mt32emu_const_context context);

/** Closes the emulation context freeing allocated resources. Added ROMs remain unaffected and ready for reuse. */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_close_synth(mt32emu_const_context context);

/** Returns true if the synth is in completely initialized state, otherwise returns false. */
MT32EMU_EXPORT mt32emu_boolean MT32EMU_C_CALL mt32emu_is_open(mt32emu_const_context context);

/**
 * Returns actual sample rate of the fully processed output stereo signal.
 * If samplerate conversion is used (i.e. when mt32emu_set_stereo_output_samplerate() has been invoked with a non-zero value),
 * the returned value is the desired output samplerate rounded down to the closest integer.
 * Otherwise, the output samplerate is chosen depending on the emulation mode of stereo analog circuitry of hardware units.
 * See comment for mt32emu_analog_output_mode for more info.
 */
MT32EMU_EXPORT mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_actual_stereo_output_samplerate(mt32emu_const_context context);

/**
 * Returns the number of samples produced at the internal synth sample rate (32000 Hz)
 * that correspond to the given number of samples at the output sample rate.
 * Intended to facilitate audio time synchronisation.
 */
MT32EMU_EXPORT mt32emu_bit32u MT32EMU_C_CALL mt32emu_convert_output_to_synth_timestamp(mt32emu_const_context context, mt32emu_bit32u output_timestamp);

/**
 * Returns the number of samples produced at the output sample rate
 * that correspond to the given number of samples at the internal synth sample rate (32000 Hz).
 * Intended to facilitate audio time synchronisation.
 */
MT32EMU_EXPORT mt32emu_bit32u MT32EMU_C_CALL mt32emu_convert_synth_to_output_timestamp(mt32emu_const_context context, mt32emu_bit32u synth_timestamp);

/** All the enqueued events are processed by the synth immediately. */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_flush_midi_queue(mt32emu_const_context context);

/**
 * Sets size of the internal MIDI event queue. The queue size is set to the minimum power of 2 that is greater or equal to the size specified.
 * The queue is flushed before reallocation.
 * Returns the actual queue size being used.
 */
MT32EMU_EXPORT mt32emu_bit32u MT32EMU_C_CALL mt32emu_set_midi_event_queue_size(mt32emu_const_context context, const mt32emu_bit32u queue_size);

/**
 * Configures the SysEx storage of the internal MIDI event queue.
 * Supplying 0 in the storage_buffer_size argument makes the SysEx data stored
 * in multiple dynamically allocated buffers per MIDI event. These buffers are only disposed
 * when a new MIDI event replaces the SysEx event in the queue, thus never on the rendering thread.
 * This is the default behaviour.
 * In contrast, when a positive value is specified, SysEx data will be stored in a single preallocated buffer,
 * which makes this kind of storage safe for use in a realtime thread. Additionally, the space retained
 * by a SysEx event, that has been processed and thus is no longer necessary, is disposed instantly.
 * Note, the queue is flushed and recreated in the process so that its size remains intact.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_configure_midi_event_queue_sysex_storage(mt32emu_const_context context, const mt32emu_bit32u storage_buffer_size);

/**
 * Installs custom MIDI receiver object intended for receiving MIDI messages generated by MIDI stream parser.
 * MIDI stream parser is involved when functions mt32emu_parse_stream() and mt32emu_play_short_message() or the likes are called.
 * By default, parsed short MIDI messages and System Exclusive messages are sent to the synth input MIDI queue.
 * This function allows to override default behaviour. If midi_receiver argument is set to NULL, the default behaviour is restored.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_midi_receiver(mt32emu_context context, mt32emu_midi_receiver_i midi_receiver, void *instance_data);

/**
 * Returns current value of the global counter of samples rendered since the synth was created (at the native sample rate 32000 Hz).
 * This method helps to compute accurate timestamp of a MIDI message to use with the methods below.
 */
MT32EMU_EXPORT mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_internal_rendered_sample_count(mt32emu_const_context context);

/* Enqueues a MIDI event for subsequent playback.
 * The MIDI event will be processed not before the specified timestamp.
 * The timestamp is measured as the global rendered sample count since the synth was created (at the native sample rate 32000 Hz).
 * The minimum delay involves emulation of the delay introduced while the event is transferred via MIDI interface
 * and emulation of the MCU busy-loop while it frees partials for use by a new Poly.
 * Calls from multiple threads must be synchronised, although, no synchronisation is required with the rendering thread.
 * onMIDIQueueOverflow callback is invoked when the MIDI event queue is full and the message cannot be enqueued.
 */

/**
 * Parses a block of raw MIDI bytes and enqueues parsed MIDI messages for further processing ASAP.
 * SysEx messages are allowed to be fragmented across several calls to this method. Running status is also handled for short messages.
 * When a System Realtime MIDI message is parsed, onMIDISystemRealtime callback is invoked.
 * NOTE: the total length of a SysEx message being fragmented shall not exceed MT32EMU_MAX_STREAM_BUFFER_SIZE (32768 bytes).
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_parse_stream(mt32emu_const_context context, const mt32emu_bit8u *stream, mt32emu_bit32u length);

/**
 * Parses a block of raw MIDI bytes and enqueues parsed MIDI messages to play at specified time.
 * SysEx messages are allowed to be fragmented across several calls to this method. Running status is also handled for short messages.
 * When a System Realtime MIDI message is parsed, onMIDISystemRealtime callback is invoked.
 * NOTE: the total length of a SysEx message being fragmented shall not exceed MT32EMU_MAX_STREAM_BUFFER_SIZE (32768 bytes).
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_parse_stream_at(mt32emu_const_context context, const mt32emu_bit8u *stream, mt32emu_bit32u length, mt32emu_bit32u timestamp);

/**
 * Enqueues a single mt32emu_bit32u-encoded short MIDI message with full processing ASAP.
 * The short MIDI message may contain no status byte, the running status is used in this case.
 * When the argument is a System Realtime MIDI message, onMIDISystemRealtime callback is invoked.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_play_short_message(mt32emu_const_context context, mt32emu_bit32u message);

/**
 * Enqueues a single mt32emu_bit32u-encoded short MIDI message to play at specified time with full processing.
 * The short MIDI message may contain no status byte, the running status is used in this case.
 * When the argument is a System Realtime MIDI message, onMIDISystemRealtime callback is invoked.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_play_short_message_at(mt32emu_const_context context, mt32emu_bit32u message, mt32emu_bit32u timestamp);

/** Enqueues a single short MIDI message to be processed ASAP. The message must contain a status byte. */
MT32EMU_EXPORT mt32emu_return_code MT32EMU_C_CALL mt32emu_play_msg(mt32emu_const_context context, mt32emu_bit32u msg);
/** Enqueues a single well formed System Exclusive MIDI message to be processed ASAP. */
MT32EMU_EXPORT mt32emu_return_code MT32EMU_C_CALL mt32emu_play_sysex(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len);

/** Enqueues a single short MIDI message to play at specified time. The message must contain a status byte. */
MT32EMU_EXPORT mt32emu_return_code MT32EMU_C_CALL mt32emu_play_msg_at(mt32emu_const_context context, mt32emu_bit32u msg, mt32emu_bit32u timestamp);
/** Enqueues a single well formed System Exclusive MIDI message to play at specified time. */
MT32EMU_EXPORT mt32emu_return_code MT32EMU_C_CALL mt32emu_play_sysex_at(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len, mt32emu_bit32u timestamp);

/* WARNING:
 * The methods below don't ensure minimum 1-sample delay between sequential MIDI events,
 * and a sequence of NoteOn and immediately succeeding NoteOff messages is always silent.
 * A thread that invokes these methods must be explicitly synchronised with the thread performing sample rendering.
 */

/**
 * Sends a short MIDI message to the synth for immediate playback. The message must contain a status byte.
 * See the WARNING above.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_play_msg_now(mt32emu_const_context context, mt32emu_bit32u msg);
/**
 * Sends unpacked short MIDI message to the synth for immediate playback. The message must contain a status byte.
 * See the WARNING above.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_play_msg_on_part(mt32emu_const_context context, mt32emu_bit8u part, mt32emu_bit8u code, mt32emu_bit8u note, mt32emu_bit8u velocity);

/**
 * Sends a single well formed System Exclusive MIDI message for immediate processing. The length is in bytes.
 * See the WARNING above.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_play_sysex_now(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len);
/**
 * Sends inner body of a System Exclusive MIDI message for direct processing. The length is in bytes.
 * See the WARNING above.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_write_sysex(mt32emu_const_context context, mt32emu_bit8u channel, const mt32emu_bit8u *sysex, mt32emu_bit32u len);

/** Allows to disable wet reverb output altogether. */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_reverb_enabled(mt32emu_const_context context, const mt32emu_boolean reverb_enabled);
/** Returns whether wet reverb output is enabled. */
MT32EMU_EXPORT mt32emu_boolean MT32EMU_C_CALL mt32emu_is_reverb_enabled(mt32emu_const_context context);
/**
 * Sets override reverb mode. In this mode, emulation ignores sysexes (or the related part of them) which control the reverb parameters.
 * This mode is in effect until it is turned off. When the synth is re-opened, the override mode is unchanged but the state
 * of the reverb model is reset to default.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_reverb_overridden(mt32emu_const_context context, const mt32emu_boolean reverb_overridden);
/** Returns whether reverb settings are overridden. */
MT32EMU_EXPORT mt32emu_boolean MT32EMU_C_CALL mt32emu_is_reverb_overridden(mt32emu_const_context context);
/**
 * Forces reverb model compatibility mode. By default, the compatibility mode corresponds to the used control ROM version.
 * Invoking this method with the argument set to true forces emulation of old MT-32 reverb circuit.
 * When the argument is false, emulation of the reverb circuit used in new generation of MT-32 compatible modules is enforced
 * (these include CM-32L and LAPC-I).
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_reverb_compatibility_mode(mt32emu_const_context context, const mt32emu_boolean mt32_compatible_mode);
/** Returns whether reverb is in old MT-32 compatibility mode. */
MT32EMU_EXPORT mt32emu_boolean MT32EMU_C_CALL mt32emu_is_mt32_reverb_compatibility_mode(mt32emu_const_context context);
/** Returns whether default reverb compatibility mode is the old MT-32 compatibility mode. */
MT32EMU_EXPORT mt32emu_boolean MT32EMU_C_CALL mt32emu_is_default_reverb_mt32_compatible(mt32emu_const_context context);

/**
 * If enabled, reverb buffers for all modes are kept around allocated all the time to avoid memory
 * allocating/freeing in the rendering thread, which may be required for realtime operation.
 * Otherwise, reverb buffers that are not in use are deleted to save memory (the default behaviour).
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_preallocate_reverb_memory(mt32emu_const_context context, const mt32emu_boolean enabled);

/** Sets new DAC input mode. See mt32emu_dac_input_mode for details. */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_dac_input_mode(mt32emu_const_context context, const mt32emu_dac_input_mode mode);
/** Returns current DAC input mode. See mt32emu_dac_input_mode for details. */
MT32EMU_EXPORT mt32emu_dac_input_mode MT32EMU_C_CALL mt32emu_get_dac_input_mode(mt32emu_const_context context);

/** Sets new MIDI delay mode. See mt32emu_midi_delay_mode for details. */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_midi_delay_mode(mt32emu_const_context context, const mt32emu_midi_delay_mode mode);
/** Returns current MIDI delay mode. See mt32emu_midi_delay_mode for details. */
MT32EMU_EXPORT mt32emu_midi_delay_mode MT32EMU_C_CALL mt32emu_get_midi_delay_mode(mt32emu_const_context context);

/**
 * Sets output gain factor for synth output channels. Applied to all output samples and unrelated with the synth's Master volume,
 * it rather corresponds to the gain of the output analog circuitry of the hardware units. However, together with mt32emu_set_reverb_output_gain()
 * it offers to the user a capability to control the gain of reverb and non-reverb output channels independently.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_output_gain(mt32emu_const_context context, float gain);
/** Returns current output gain factor for synth output channels. */
MT32EMU_EXPORT float MT32EMU_C_CALL mt32emu_get_output_gain(mt32emu_const_context context);

/**
 * Sets output gain factor for the reverb wet output channels. It rather corresponds to the gain of the output
 * analog circuitry of the hardware units. However, together with mt32emu_set_output_gain() it offers to the user a capability
 * to control the gain of reverb and non-reverb output channels independently.
 *
 * Note: We're currently emulate CM-32L/CM-64 reverb quite accurately and the reverb output level closely
 * corresponds to the level of digital capture. Although, according to the CM-64 PCB schematic,
 * there is a difference in the reverb analogue circuit, and the resulting output gain is 0.68
 * of that for LA32 analogue output. This factor is applied to the reverb output gain.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_reverb_output_gain(mt32emu_const_context context, float gain);
/** Returns current output gain factor for reverb wet output channels. */
MT32EMU_EXPORT float MT32EMU_C_CALL mt32emu_get_reverb_output_gain(mt32emu_const_context context);

/**
 * Sets (or removes) an override for the current volume (output level) on a specific part.
 * When the part volume is overridden, the MIDI controller Volume (7) on the MIDI channel this part is assigned to
 * has no effect on the output level of this part. Similarly, the output level value set on this part via a SysEx that
 * modifies the Patch temp structure is disregarded.
 * To enable the override mode, argument volumeOverride should be in range 0..100, setting a value outside this range
 * disables the previously set override, if any.
 * Note: Setting volumeOverride to 0 mutes the part completely, meaning no sound is generated at all.
 * This is unlike the behaviour of real devices - setting 0 volume on a part may leave it still producing
 * sound at a very low level.
 * Argument partNumber should be 0..7 for Part 1..8, or 8 for Rhythm.
 */
MT32EMU_EXPORT_V(2.6) void MT32EMU_C_CALL mt32emu_set_part_volume_override(mt32emu_const_context context, mt32emu_bit8u part_number, mt32emu_bit8u volume_override);
/**
 * Returns the overridden volume previously set on a specific part; a value outside the range 0..100 means no override
 * is currently in effect.
 * Argument partNumber should be 0..7 for Part 1..8, or 8 for Rhythm.
 */
MT32EMU_EXPORT_V(2.6) mt32emu_bit8u MT32EMU_C_CALL mt32emu_get_part_volume_override(mt32emu_const_context context, mt32emu_bit8u part_number);

/** Swaps left and right output channels. */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_reversed_stereo_enabled(mt32emu_const_context context, const mt32emu_boolean enabled);
/** Returns whether left and right output channels are swapped. */
MT32EMU_EXPORT mt32emu_boolean MT32EMU_C_CALL mt32emu_is_reversed_stereo_enabled(mt32emu_const_context context);

/**
 * Allows to toggle the NiceAmpRamp mode.
 * In this mode, we want to ensure that amp ramp never jumps to the target
 * value and always gradually increases or decreases. It seems that real units
 * do not bother to always check if a newly started ramp leads to a jump.
 * We also prefer the quality improvement over the emulation accuracy,
 * so this mode is enabled by default.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_nice_amp_ramp_enabled(mt32emu_const_context context, const mt32emu_boolean enabled);
/** Returns whether NiceAmpRamp mode is enabled. */
MT32EMU_EXPORT mt32emu_boolean MT32EMU_C_CALL mt32emu_is_nice_amp_ramp_enabled(mt32emu_const_context context);

/**
 * Allows to toggle the NicePanning mode.
 * Despite the Roland's manual specifies allowed panpot values in range 0-14,
 * the LA-32 only receives 3-bit pan setting in fact. In particular, this
 * makes it impossible to set the "middle" panning for a single partial.
 * In the NicePanning mode, we enlarge the pan setting accuracy to 4 bits
 * making it smoother thus sacrificing the emulation accuracy.
 * This mode is disabled by default.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_nice_panning_enabled(mt32emu_const_context context, const mt32emu_boolean enabled);
/** Returns whether NicePanning mode is enabled. */
MT32EMU_EXPORT mt32emu_boolean MT32EMU_C_CALL mt32emu_is_nice_panning_enabled(mt32emu_const_context context);

/**
 * Allows to toggle the NicePartialMixing mode.
 * LA-32 is known to mix partials either in-phase (so that they are added)
 * or in counter-phase (so that they are subtracted instead).
 * In some cases, this quirk isn't highly desired because a pair of closely
 * sounding partials may occasionally cancel out.
 * In the NicePartialMixing mode, the mixing is always performed in-phase,
 * thus making the behaviour more predictable.
 * This mode is disabled by default.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_set_nice_partial_mixing_enabled(mt32emu_const_context context, const mt32emu_boolean enabled);
/** Returns whether NicePartialMixing mode is enabled. */
MT32EMU_EXPORT mt32emu_boolean MT32EMU_C_CALL mt32emu_is_nice_partial_mixing_enabled(mt32emu_const_context context);

/**
 * Renders samples to the specified output stream as if they were sampled at the analog stereo output at the desired sample rate.
 * If the output sample rate is not specified explicitly, the default output sample rate is used which depends on the current
 * mode of analog circuitry emulation. See mt32emu_analog_output_mode.
 * The length is in frames, not bytes (in 16-bit stereo, one frame is 4 bytes). Uses NATIVE byte ordering.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_render_bit16s(mt32emu_const_context context, mt32emu_bit16s *stream, mt32emu_bit32u len);
/** Same as above but outputs to a float stereo stream. */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_render_float(mt32emu_const_context context, float *stream, mt32emu_bit32u len);

/**
 * Renders samples to the specified output streams as if they appeared at the DAC entrance.
 * No further processing performed in analog circuitry emulation is applied to the signal.
 * NULL may be specified in place of any or all of the stream buffers to skip it.
 * The length is in samples, not bytes. Uses NATIVE byte ordering.
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_render_bit16s_streams(mt32emu_const_context context, const mt32emu_dac_output_bit16s_streams *streams, mt32emu_bit32u len);
/** Same as above but outputs to float streams. */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_render_float_streams(mt32emu_const_context context, const mt32emu_dac_output_float_streams *streams, mt32emu_bit32u len);

/** Returns true when there is at least one active partial, otherwise false. */
MT32EMU_EXPORT mt32emu_boolean MT32EMU_C_CALL mt32emu_has_active_partials(mt32emu_const_context context);

/** Returns true if mt32emu_has_active_partials() returns true, or reverb is (somewhat unreliably) detected as being active. */
MT32EMU_EXPORT mt32emu_boolean MT32EMU_C_CALL mt32emu_is_active(mt32emu_const_context context);

/** Returns the maximum number of partials playing simultaneously. */
MT32EMU_EXPORT mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_partial_count(mt32emu_const_context context);

/**
 * Returns current states of all the parts as a bit set. The least significant bit corresponds to the state of part 1,
 * total of 9 bits hold the states of all the parts. If the returned bit for a part is set, there is at least one active
 * non-releasing partial playing on this part. This info is useful in emulating behaviour of LCD display of the hardware units.
 */
MT32EMU_EXPORT mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_part_states(mt32emu_const_context context);

/**
 * Fills in current states of all the partials into the array provided. Each byte in the array holds states of 4 partials
 * starting from the least significant bits. The state of each partial is packed in a pair of bits.
 * The array must be large enough to accommodate states of all the partials.
 * @see getPartialCount()
 */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_get_partial_states(mt32emu_const_context context, mt32emu_bit8u *partial_states);

/**
 * Fills in information about currently playing notes on the specified part into the arrays provided. The arrays must be large enough
 * to accommodate data for all the playing notes. The maximum number of simultaneously playing notes cannot exceed the number of partials.
 * Argument partNumber should be 0..7 for Part 1..8, or 8 for Rhythm.
 * Returns the number of currently playing notes on the specified part.
 */
MT32EMU_EXPORT mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_playing_notes(mt32emu_const_context context, mt32emu_bit8u part_number, mt32emu_bit8u *keys, mt32emu_bit8u *velocities);

/**
 * Returns name of the patch set on the specified part.
 * Argument partNumber should be 0..7 for Part 1..8, or 8 for Rhythm.
 * The returned value is a null-terminated string which is guaranteed to remain valid until the next call to one of functions
 * that perform sample rendering or immediate SysEx processing (e.g. mt32emu_play_sysex_now).
 */
MT32EMU_EXPORT const char * MT32EMU_C_CALL mt32emu_get_patch_name(mt32emu_const_context context, mt32emu_bit8u part_number);

/**
 * Retrieves the name of the sound group the timbre identified by arguments timbre_group and timbre_number is associated with.
 * Values 0-3 of timbre_group correspond to the timbre banks GROUP A, GROUP B, MEMORY and RHYTHM.
 * For all but the RHYTHM timbre bank, allowed values of timbre_number are in range 0-63. The number of timbres
 * contained in the RHYTHM bank depends on the used control ROM version.
 * The argument sound_group_name must point to an array of at least 8 characters. The result is a null-terminated string.
 * Returns whether the specified timbre has been found and the result written in sound_group_name.
 */
MT32EMU_EXPORT_V(2.7) mt32emu_boolean MT32EMU_C_CALL mt32emu_get_sound_group_name(mt32emu_const_context context, char *sound_group_name, mt32emu_bit8u timbre_group, mt32emu_bit8u timbre_number);
/**
 * Retrieves the name of the timbre identified by arguments timbre_group and timbre_number.
 * Values 0-3 of timbre_group correspond to the timbre banks GROUP A, GROUP B, MEMORY and RHYTHM.
 * For all but the RHYTHM timbre bank, allowed values of timbre_number are in range 0-63. The number of timbres
 * contained in the RHYTHM bank depends on the used control ROM version.
 * The argument sound_name must point to an array of at least 11 characters. The result is a null-terminated string.
 * Returns whether the specified timbre has been found and the result written in sound_name.
 */
MT32EMU_EXPORT_V(2.7) mt32emu_boolean MT32EMU_C_CALL mt32emu_get_sound_name(mt32emu_const_context context, char *sound_name, mt32emu_bit8u timbreGroup, mt32emu_bit8u timbreNumber);

/** Stores internal state of emulated synth into an array provided (as it would be acquired from hardware). */
MT32EMU_EXPORT void MT32EMU_C_CALL mt32emu_read_memory(mt32emu_const_context context, mt32emu_bit32u addr, mt32emu_bit32u len, mt32emu_bit8u *data);

/**
 * Retrieves the current state of the emulated MT-32 display facilities.
 * Typically, the state is updated during the rendering. When that happens, a related callback from mt32emu_report_handler_i_v1
 * is invoked. However, there might be no need to invoke this method after each update, e.g. when the render buffer is just
 * a few milliseconds long.
 * The argument target_buffer must point to an array of at least 21 characters. The result is a null-terminated string.
 * The argument narrow_lcd enables a condensed representation of the displayed information in some cases. This is mainly intended
 * to route the result to a hardware LCD that is only 16 characters wide. Automatic scrolling of longer strings is not supported.
 * Returns whether the MIDI MESSAGE LED is ON and fills the target_buffer parameter.
 */
MT32EMU_EXPORT_V(2.6) mt32emu_boolean MT32EMU_C_CALL mt32emu_get_display_state(mt32emu_const_context context, char *target_buffer, const mt32emu_boolean narrow_lcd);

/**
 * Resets the emulated LCD to the main mode (Master Volume). This has the same effect as pressing the Master Volume button
 * while the display shows some other message. Useful for the new-gen devices as those require a special Display Reset SysEx
 * to return to the main mode e.g. from showing a custom display message or a checksum error.
 */
MT32EMU_EXPORT_V(2.6) void MT32EMU_C_CALL mt32emu_set_main_display_mode(mt32emu_const_context context);

/**
 * Permits to select an arbitrary display emulation model that does not necessarily match the actual behaviour implemented
 * in the control ROM version being used.
 * Invoking this method with the argument set to true forces emulation of the old-gen MT-32 display features.
 * Otherwise, emulation of the new-gen devices is enforced (these include CM-32L and LAPC-I as if these were connected to an LCD).
 */
MT32EMU_EXPORT_V(2.6) void MT32EMU_C_CALL mt32emu_set_display_compatibility(mt32emu_const_context context, mt32emu_boolean old_mt32_compatibility_enabled);
/** Returns whether the currently configured features of the emulated display are compatible with the old-gen MT-32 devices. */
MT32EMU_EXPORT_V(2.6) mt32emu_boolean MT32EMU_C_CALL mt32emu_is_display_old_mt32_compatible(mt32emu_const_context context);
/**
 * Returns whether the emulated display features configured by default depending on the actual control ROM version
 * are compatible with the old-gen MT-32 devices.
 */
MT32EMU_EXPORT_V(2.6) mt32emu_boolean MT32EMU_C_CALL mt32emu_is_default_display_old_mt32_compatible(mt32emu_const_context context);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* #ifndef MT32EMU_C_INTERFACE_H */
