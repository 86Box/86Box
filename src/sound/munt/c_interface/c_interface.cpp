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

#include <cstring>

#include "../globals.h"
#include "../Types.h"
#include "../File.h"
#include "../FileStream.h"
#include "../ROMInfo.h"
#include "../Synth.h"
#include "../MidiStreamParser.h"
#include "../SampleRateConverter.h"

#include "c_types.h"
#include "c_interface.h"

using namespace MT32Emu;

namespace MT32Emu {

struct SamplerateConversionState {
	double outputSampleRate;
	SamplerateConversionQuality srcQuality;
	SampleRateConverter *src;
};

static mt32emu_service_version MT32EMU_C_CALL getSynthVersionID(mt32emu_service_i) {
	return MT32EMU_SERVICE_VERSION_CURRENT;
}

static const mt32emu_service_i_v6 SERVICE_VTABLE = {
	getSynthVersionID,
	mt32emu_get_supported_report_handler_version,
	mt32emu_get_supported_midi_receiver_version,
	mt32emu_get_library_version_int,
	mt32emu_get_library_version_string,
	mt32emu_get_stereo_output_samplerate,
	mt32emu_create_context,
	mt32emu_free_context,
	mt32emu_add_rom_data,
	mt32emu_add_rom_file,
	mt32emu_get_rom_info,
	mt32emu_set_partial_count,
	mt32emu_set_analog_output_mode,
	mt32emu_open_synth,
	mt32emu_close_synth,
	mt32emu_is_open,
	mt32emu_get_actual_stereo_output_samplerate,
	mt32emu_flush_midi_queue,
	mt32emu_set_midi_event_queue_size,
	mt32emu_set_midi_receiver,
	mt32emu_parse_stream,
	mt32emu_parse_stream_at,
	mt32emu_play_short_message,
	mt32emu_play_short_message_at,
	mt32emu_play_msg,
	mt32emu_play_sysex,
	mt32emu_play_msg_at,
	mt32emu_play_sysex_at,
	mt32emu_play_msg_now,
	mt32emu_play_msg_on_part,
	mt32emu_play_sysex_now,
	mt32emu_write_sysex,
	mt32emu_set_reverb_enabled,
	mt32emu_is_reverb_enabled,
	mt32emu_set_reverb_overridden,
	mt32emu_is_reverb_overridden,
	mt32emu_set_reverb_compatibility_mode,
	mt32emu_is_mt32_reverb_compatibility_mode,
	mt32emu_is_default_reverb_mt32_compatible,
	mt32emu_set_dac_input_mode,
	mt32emu_get_dac_input_mode,
	mt32emu_set_midi_delay_mode,
	mt32emu_get_midi_delay_mode,
	mt32emu_set_output_gain,
	mt32emu_get_output_gain,
	mt32emu_set_reverb_output_gain,
	mt32emu_get_reverb_output_gain,
	mt32emu_set_reversed_stereo_enabled,
	mt32emu_is_reversed_stereo_enabled,
	mt32emu_render_bit16s,
	mt32emu_render_float,
	mt32emu_render_bit16s_streams,
	mt32emu_render_float_streams,
	mt32emu_has_active_partials,
	mt32emu_is_active,
	mt32emu_get_partial_count,
	mt32emu_get_part_states,
	mt32emu_get_partial_states,
	mt32emu_get_playing_notes,
	mt32emu_get_patch_name,
	mt32emu_read_memory,
	mt32emu_get_best_analog_output_mode,
	mt32emu_set_stereo_output_samplerate,
	mt32emu_set_samplerate_conversion_quality,
	mt32emu_select_renderer_type,
	mt32emu_get_selected_renderer_type,
	mt32emu_convert_output_to_synth_timestamp,
	mt32emu_convert_synth_to_output_timestamp,
	mt32emu_get_internal_rendered_sample_count,
	mt32emu_set_nice_amp_ramp_enabled,
	mt32emu_is_nice_amp_ramp_enabled,
	mt32emu_set_nice_panning_enabled,
	mt32emu_is_nice_panning_enabled,
	mt32emu_set_nice_partial_mixing_enabled,
	mt32emu_is_nice_partial_mixing_enabled,
	mt32emu_preallocate_reverb_memory,
	mt32emu_configure_midi_event_queue_sysex_storage,
	mt32emu_get_machine_ids,
	mt32emu_get_rom_ids,
	mt32emu_identify_rom_data,
	mt32emu_identify_rom_file,
	mt32emu_merge_and_add_rom_data,
	mt32emu_merge_and_add_rom_files,
	mt32emu_add_machine_rom_file,
	mt32emu_get_display_state,
	mt32emu_set_main_display_mode,
	mt32emu_set_display_compatibility,
	mt32emu_is_display_old_mt32_compatible,
	mt32emu_is_default_display_old_mt32_compatible,
	mt32emu_set_part_volume_override,
	mt32emu_get_part_volume_override,
	mt32emu_get_sound_group_name,
	mt32emu_get_sound_name
};

} // namespace MT32Emu

struct mt32emu_data {
	ReportHandler2 *reportHandler;
	Synth *synth;
	const ROMImage *controlROMImage;
	const ROMImage *pcmROMImage;
	DefaultMidiStreamParser *midiParser;
	Bit32u partialCount;
	AnalogOutputMode analogOutputMode;
	SamplerateConversionState *srcState;
};

// Internal C++ utility stuff

namespace MT32Emu {

class DelegatingReportHandlerAdapter : public ReportHandler2 {
public:
	DelegatingReportHandlerAdapter(mt32emu_report_handler_i useReportHandler, void *useInstanceData) :
		delegate(useReportHandler), instanceData(useInstanceData) {}

private:
	const mt32emu_report_handler_i delegate;
	void * const instanceData;

	bool isVersionLess(mt32emu_report_handler_version versionID) {
		return delegate.v0->getVersionID(delegate) < versionID;
	}

	void printDebug(const char *fmt, va_list list) {
		if (delegate.v0->printDebug == NULL) {
			ReportHandler::printDebug(fmt, list);
		} else {
			delegate.v0->printDebug(instanceData, fmt, list);
		}
	}

	void onErrorControlROM() {
		if (delegate.v0->onErrorControlROM == NULL) {
			ReportHandler::onErrorControlROM();
		} else {
			delegate.v0->onErrorControlROM(instanceData);
		}
	}

	void onErrorPCMROM() {
		if (delegate.v0->onErrorPCMROM == NULL) {
			ReportHandler::onErrorPCMROM();
		} else {
			delegate.v0->onErrorPCMROM(instanceData);
		}
	}

	void showLCDMessage(const char *message) {
		if (delegate.v0->showLCDMessage == NULL) {
			ReportHandler::showLCDMessage(message);
		} else {
			delegate.v0->showLCDMessage(instanceData, message);
		}
	}

	void onMIDIMessagePlayed() {
		if (delegate.v0->onMIDIMessagePlayed == NULL) {
			ReportHandler::onMIDIMessagePlayed();
		} else {
			delegate.v0->onMIDIMessagePlayed(instanceData);
		}
	}

	bool onMIDIQueueOverflow() {
		if (delegate.v0->onMIDIQueueOverflow == NULL) {
			return ReportHandler::onMIDIQueueOverflow();
		}
		return delegate.v0->onMIDIQueueOverflow(instanceData) != MT32EMU_BOOL_FALSE;
	}

	void onMIDISystemRealtime(Bit8u systemRealtime) {
		if (delegate.v0->onMIDISystemRealtime == NULL) {
			ReportHandler::onMIDISystemRealtime(systemRealtime);
		} else {
			delegate.v0->onMIDISystemRealtime(instanceData, systemRealtime);
		}
	}

	void onDeviceReset() {
		if (delegate.v0->onDeviceReset == NULL) {
			ReportHandler::onDeviceReset();
		} else {
			delegate.v0->onDeviceReset(instanceData);
		}
	}

	void onDeviceReconfig() {
		if (delegate.v0->onDeviceReconfig == NULL) {
			ReportHandler::onDeviceReconfig();
		} else {
			delegate.v0->onDeviceReconfig(instanceData);
		}
	}

	void onNewReverbMode(Bit8u mode) {
		if (delegate.v0->onNewReverbMode == NULL) {
			ReportHandler::onNewReverbMode(mode);
		} else {
			delegate.v0->onNewReverbMode(instanceData, mode);
		}
	}

	void onNewReverbTime(Bit8u time) {
		if (delegate.v0->onNewReverbTime == NULL) {
			ReportHandler::onNewReverbTime(time);
		} else {
			delegate.v0->onNewReverbTime(instanceData, time);
		}
	}

	void onNewReverbLevel(Bit8u level) {
		if (delegate.v0->onNewReverbLevel == NULL) {
			ReportHandler::onNewReverbLevel(level);
		} else {
			delegate.v0->onNewReverbLevel(instanceData, level);
		}
	}

	void onPolyStateChanged(Bit8u partNum) {
		if (delegate.v0->onPolyStateChanged == NULL) {
			ReportHandler::onPolyStateChanged(partNum);
		} else {
			delegate.v0->onPolyStateChanged(instanceData, partNum);
		}
	}

	void onProgramChanged(Bit8u partNum, const char *soundGroupName, const char *patchName) {
		if (delegate.v0->onProgramChanged == NULL) {
			ReportHandler::onProgramChanged(partNum, soundGroupName, patchName);
		} else {
			delegate.v0->onProgramChanged(instanceData, partNum, soundGroupName, patchName);
		}
	}

	void onLCDStateUpdated() {
		if (isVersionLess(MT32EMU_REPORT_HANDLER_VERSION_1) || delegate.v1->onLCDStateUpdated == NULL) {
			ReportHandler2::onLCDStateUpdated();
		} else {
			delegate.v1->onLCDStateUpdated(instanceData);
		}
	}

	void onMidiMessageLEDStateUpdated(bool ledState) {
		if (isVersionLess(MT32EMU_REPORT_HANDLER_VERSION_1) || delegate.v1->onMidiMessageLEDStateUpdated == NULL) {
			ReportHandler2::onMidiMessageLEDStateUpdated(ledState);
		} else {
			delegate.v1->onMidiMessageLEDStateUpdated(instanceData, ledState ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE);
		}
	}
};

class DelegatingMidiStreamParser : public DefaultMidiStreamParser {
public:
	DelegatingMidiStreamParser(const mt32emu_data *useData, mt32emu_midi_receiver_i useMIDIReceiver, void *useInstanceData) :
		DefaultMidiStreamParser(*useData->synth), delegate(useMIDIReceiver), instanceData(useInstanceData) {}

protected:
	mt32emu_midi_receiver_i delegate;
	void *instanceData;

private:
	void handleShortMessage(const Bit32u message) {
		if (delegate.v0->handleShortMessage == NULL) {
			DefaultMidiStreamParser::handleShortMessage(message);
		} else {
			delegate.v0->handleShortMessage(instanceData, message);
		}
	}

	void handleSysex(const Bit8u *stream, const Bit32u length) {
		if (delegate.v0->handleSysex == NULL) {
			DefaultMidiStreamParser::handleSysex(stream, length);
		} else {
			delegate.v0->handleSysex(instanceData, stream, length);
		}
	}

	void handleSystemRealtimeMessage(const Bit8u realtime) {
		if (delegate.v0->handleSystemRealtimeMessage == NULL) {
			DefaultMidiStreamParser::handleSystemRealtimeMessage(realtime);
		} else {
			delegate.v0->handleSystemRealtimeMessage(instanceData, realtime);
		}
	}
};

static void fillROMInfo(mt32emu_rom_info *rom_info, const ROMInfo *controlROMInfo, const ROMInfo *pcmROMInfo) {
	if (controlROMInfo != NULL) {
		rom_info->control_rom_id = controlROMInfo->shortName;
		rom_info->control_rom_description = controlROMInfo->description;
		rom_info->control_rom_sha1_digest = controlROMInfo->sha1Digest;
	} else {
		rom_info->control_rom_id = NULL;
		rom_info->control_rom_description = NULL;
		rom_info->control_rom_sha1_digest = NULL;
	}
	if (pcmROMInfo != NULL) {
		rom_info->pcm_rom_id = pcmROMInfo->shortName;
		rom_info->pcm_rom_description = pcmROMInfo->description;
		rom_info->pcm_rom_sha1_digest = pcmROMInfo->sha1Digest;
	} else {
		rom_info->pcm_rom_id = NULL;
		rom_info->pcm_rom_description = NULL;
		rom_info->pcm_rom_sha1_digest = NULL;
	}
}

static const MachineConfiguration *findMachineConfiguration(const char *machine_id) {
	Bit32u configurationCount;
	const MachineConfiguration * const *configurations = MachineConfiguration::getAllMachineConfigurations(&configurationCount);
	for (Bit32u i = 0; i < configurationCount; i++) {
		if (!strcmp(configurations[i]->getMachineID(), machine_id)) return configurations[i];
	}
	return NULL;
}

static mt32emu_return_code identifyROM(mt32emu_rom_info *rom_info, File *romFile, const char *machineID) {
	const ROMInfo *romInfo;
	if (machineID == NULL) {
		romInfo = ROMInfo::getROMInfo(romFile);
	} else {
		const MachineConfiguration *configuration = findMachineConfiguration(machineID);
		if (configuration == NULL) {
			fillROMInfo(rom_info, NULL, NULL);
			return MT32EMU_RC_MACHINE_NOT_IDENTIFIED;
		}
		romInfo = ROMInfo::getROMInfo(romFile, configuration->getCompatibleROMInfos());
	}
	if (romInfo == NULL) {
		fillROMInfo(rom_info, NULL, NULL);
		return MT32EMU_RC_ROM_NOT_IDENTIFIED;
	}
	if (romInfo->type == ROMInfo::Control) fillROMInfo(rom_info, romInfo, NULL);
	else if (romInfo->type == ROMInfo::PCM) fillROMInfo(rom_info, NULL, romInfo);
	else fillROMInfo(rom_info, NULL, NULL);
	return MT32EMU_RC_OK;
}

static bool isROMInfoCompatible(const MachineConfiguration *machineConfiguration, const ROMInfo *romInfo) {
	Bit32u romCount;
	const ROMInfo * const *compatibleROMInfos = machineConfiguration->getCompatibleROMInfos(&romCount);
	for (Bit32u i = 0; i < romCount; i++) {
		if (romInfo == compatibleROMInfos[i]) return true;
	}
	return false;
}

static mt32emu_return_code replaceOrMergeROMImage(const ROMImage *&contextROMImage, const ROMImage *newROMImage, const MachineConfiguration *machineConfiguration, mt32emu_return_code addedFullROM, mt32emu_return_code addedPartialROM) {
	if (contextROMImage != NULL) {
		if (machineConfiguration != NULL) {
			const ROMImage *mergedROMImage = ROMImage::mergeROMImages(contextROMImage, newROMImage);
			if (mergedROMImage != NULL) {
				if (newROMImage->isFileUserProvided()) delete newROMImage->getFile();
				ROMImage::freeROMImage(newROMImage);
				if (contextROMImage->isFileUserProvided()) delete contextROMImage->getFile();
				ROMImage::freeROMImage(contextROMImage);
				contextROMImage = mergedROMImage;
				return addedFullROM;
			}
			if (newROMImage->getROMInfo() == contextROMImage->getROMInfo()
				|| (newROMImage->getROMInfo()->pairType != ROMInfo::Full
					&& isROMInfoCompatible(machineConfiguration, contextROMImage->getROMInfo()))) {
				ROMImage::freeROMImage(newROMImage);
				return MT32EMU_RC_OK;
			}
		}
		if (contextROMImage->isFileUserProvided()) delete contextROMImage->getFile();
		ROMImage::freeROMImage(contextROMImage);
	}
	contextROMImage = newROMImage;
	return newROMImage->getROMInfo()->pairType == ROMInfo::Full ? addedFullROM: addedPartialROM;
}

static mt32emu_return_code addROMFiles(mt32emu_data *data, File *file1, File *file2 = NULL, const MachineConfiguration *machineConfiguration = NULL) {
	const ROMImage *romImage;
	if (machineConfiguration != NULL) {
		romImage = ROMImage::makeROMImage(file1, machineConfiguration->getCompatibleROMInfos());
	} else {
		romImage = file2 == NULL ? ROMImage::makeROMImage(file1, ROMInfo::getFullROMInfos()) : ROMImage::makeROMImage(file1, file2);
	}
	if (romImage == NULL) return MT32EMU_RC_ROMS_NOT_PAIRABLE;
	const ROMInfo *info = romImage->getROMInfo();
	if (info == NULL) {
		ROMImage::freeROMImage(romImage);
		return MT32EMU_RC_ROM_NOT_IDENTIFIED;
	}
	switch (info->type) {
	case ROMInfo::Control:
		return replaceOrMergeROMImage(data->controlROMImage, romImage, machineConfiguration, MT32EMU_RC_ADDED_CONTROL_ROM, MT32EMU_RC_ADDED_PARTIAL_CONTROL_ROM);
	case ROMInfo::PCM:
		return replaceOrMergeROMImage(data->pcmROMImage, romImage, machineConfiguration, MT32EMU_RC_ADDED_PCM_ROM, MT32EMU_RC_ADDED_PARTIAL_PCM_ROM);
	default:
		ROMImage::freeROMImage(romImage);
		return MT32EMU_RC_OK; // No support for reverb ROM yet.
	}
}

static mt32emu_return_code createFileStream(const char *filename, FileStream *&fileStream) {
	mt32emu_return_code rc;
	fileStream = new FileStream;
	if (!fileStream->open(filename)) {
		rc = MT32EMU_RC_FILE_NOT_FOUND;
	} else if (fileStream->getSize() == 0) {
		rc = MT32EMU_RC_FILE_NOT_LOADED;
	} else {
		return MT32EMU_RC_OK;
	}
	delete fileStream;
	fileStream = NULL;
	return rc;
}

} // namespace MT32Emu

// C-visible implementation

extern "C" {

mt32emu_service_i MT32EMU_C_CALL mt32emu_get_service_i() {
	mt32emu_service_i i;
	i.v6 = &SERVICE_VTABLE;
	return i;
}

mt32emu_report_handler_version MT32EMU_C_CALL mt32emu_get_supported_report_handler_version() {
	return MT32EMU_REPORT_HANDLER_VERSION_CURRENT;
}

mt32emu_midi_receiver_version MT32EMU_C_CALL mt32emu_get_supported_midi_receiver_version() {
	return MT32EMU_MIDI_RECEIVER_VERSION_CURRENT;
}

mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_library_version_int() {
	return Synth::getLibraryVersionInt();
}

const char * MT32EMU_C_CALL mt32emu_get_library_version_string() {
	return Synth::getLibraryVersionString();
}

mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_stereo_output_samplerate(const mt32emu_analog_output_mode analog_output_mode) {
	return Synth::getStereoOutputSampleRate(static_cast<AnalogOutputMode>(analog_output_mode));
}

mt32emu_analog_output_mode MT32EMU_C_CALL mt32emu_get_best_analog_output_mode(const double target_samplerate) {
	return mt32emu_analog_output_mode(SampleRateConverter::getBestAnalogOutputMode(target_samplerate));
}

size_t MT32EMU_C_CALL mt32emu_get_machine_ids(const char **machine_ids, size_t machine_ids_size) {
	Bit32u configurationCount;
	const MachineConfiguration * const *configurations = MachineConfiguration::getAllMachineConfigurations(&configurationCount);
	if (machine_ids != NULL) {
		for (Bit32u i = 0; i < machine_ids_size; i++) {
			machine_ids[i] = i < configurationCount ? configurations[i]->getMachineID() : NULL;
		}
	}
	return configurationCount;
}

size_t MT32EMU_C_CALL mt32emu_get_rom_ids(const char **rom_ids, size_t rom_ids_size, const char *machine_id) {
	const ROMInfo * const *romInfos;
	Bit32u romCount;
	if (machine_id != NULL) {
		const MachineConfiguration *configuration = findMachineConfiguration(machine_id);
		if (configuration != NULL) {
			romInfos = configuration->getCompatibleROMInfos(&romCount);
		} else {
			romInfos = NULL;
			romCount = 0U;
		}
	} else {
		romInfos = ROMInfo::getAllROMInfos(&romCount);
	}
	if (rom_ids != NULL) {
		for (size_t i = 0; i < rom_ids_size; i++) {
			rom_ids[i] = i < romCount ? romInfos[i]->shortName : NULL;
		}
	}
	return romCount;
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_identify_rom_data(mt32emu_rom_info *rom_info, const mt32emu_bit8u *data, size_t data_size, const char *machine_id) {
	ArrayFile romFile = ArrayFile(data, data_size);
	return identifyROM(rom_info, &romFile, machine_id);
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_identify_rom_file(mt32emu_rom_info *rom_info, const char *filename, const char *machine_id) {
	FileStream *fs;
	mt32emu_return_code rc = createFileStream(filename, fs);
	if (fs == NULL) return rc;
	rc = identifyROM(rom_info, fs, machine_id);
	delete fs;
	return rc;
}

mt32emu_context MT32EMU_C_CALL mt32emu_create_context(mt32emu_report_handler_i report_handler, void *instance_data) {
	mt32emu_data *data = new mt32emu_data;
	data->synth = new Synth;
	if (report_handler.v0 != NULL) {
		data->reportHandler = new DelegatingReportHandlerAdapter(report_handler, instance_data);
		data->synth->setReportHandler2(data->reportHandler);
	} else {
		data->reportHandler = NULL;
	}
	data->midiParser = new DefaultMidiStreamParser(*data->synth);
	data->controlROMImage = NULL;
	data->pcmROMImage = NULL;
	data->partialCount = DEFAULT_MAX_PARTIALS;
	data->analogOutputMode = AnalogOutputMode_COARSE;

	data->srcState = new SamplerateConversionState;
	data->srcState->outputSampleRate = 0.0;
	data->srcState->srcQuality = SamplerateConversionQuality_GOOD;
	data->srcState->src = NULL;

	return data;
}

void MT32EMU_C_CALL mt32emu_free_context(mt32emu_context data) {
	if (data == NULL) return;

	delete data->srcState->src;
	data->srcState->src = NULL;
	delete data->srcState;
	data->srcState = NULL;

	if (data->controlROMImage != NULL) {
		if (data->controlROMImage->isFileUserProvided()) delete data->controlROMImage->getFile();
		ROMImage::freeROMImage(data->controlROMImage);
		data->controlROMImage = NULL;
	}
	if (data->pcmROMImage != NULL) {
		if (data->pcmROMImage->isFileUserProvided()) delete data->pcmROMImage->getFile();
		ROMImage::freeROMImage(data->pcmROMImage);
		data->pcmROMImage = NULL;
	}
	delete data->midiParser;
	data->midiParser = NULL;
	delete data->synth;
	data->synth = NULL;
	delete data->reportHandler;
	data->reportHandler = NULL;
	delete data;
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_add_rom_data(mt32emu_context context, const mt32emu_bit8u *data, size_t data_size, const mt32emu_sha1_digest *sha1_digest) {
	if (sha1_digest == NULL) return addROMFiles(context, new ArrayFile(data, data_size));
	return addROMFiles(context, new ArrayFile(data, data_size, *sha1_digest));
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_add_rom_file(mt32emu_context context, const char *filename) {
	FileStream *fs;
	mt32emu_return_code rc = createFileStream(filename, fs);
	if (fs != NULL) rc = addROMFiles(context, fs);
	if (rc <= MT32EMU_RC_OK) delete fs;
	return rc;
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_merge_and_add_rom_data(mt32emu_context context, const mt32emu_bit8u *part1_data, size_t part1_data_size, const mt32emu_sha1_digest *part1_sha1_digest, const mt32emu_bit8u *part2_data, size_t part2_data_size, const mt32emu_sha1_digest *part2_sha1_digest) {
	ArrayFile *file1 = part1_sha1_digest == NULL ? new ArrayFile(part1_data, part1_data_size) : new ArrayFile(part1_data, part1_data_size, *part1_sha1_digest);
	ArrayFile *file2 = part2_sha1_digest == NULL ? new ArrayFile(part2_data, part2_data_size) : new ArrayFile(part2_data, part2_data_size, *part2_sha1_digest);
	mt32emu_return_code rc = addROMFiles(context, file1, file2);
	delete file1;
	delete file2;
	return rc;
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_merge_and_add_rom_files(mt32emu_context context, const char *part1_filename, const char *part2_filename) {
	FileStream *fs1;
	mt32emu_return_code rc = createFileStream(part1_filename, fs1);
	if (fs1 != NULL) {
		FileStream *fs2;
		rc = createFileStream(part2_filename, fs2);
		if (fs2 != NULL) {
			rc = addROMFiles(context, fs1, fs2);
			delete fs2;
		}
		delete fs1;
	}
	return rc;
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_add_machine_rom_file(mt32emu_context context, const char *machine_id, const char *filename) {
	const MachineConfiguration *machineConfiguration = findMachineConfiguration(machine_id);
	if (machineConfiguration == NULL) return MT32EMU_RC_MACHINE_NOT_IDENTIFIED;

	FileStream *fs;
	mt32emu_return_code rc = createFileStream(filename, fs);
	if (fs == NULL) return rc;
	rc = addROMFiles(context, fs, NULL, machineConfiguration);
	if (rc <= MT32EMU_RC_OK) delete fs;
	return rc;
}

void MT32EMU_C_CALL mt32emu_get_rom_info(mt32emu_const_context context, mt32emu_rom_info *rom_info) {
	const ROMInfo *controlROMInfo = context->controlROMImage == NULL ? NULL : context->controlROMImage->getROMInfo();
	const ROMInfo *pcmROMInfo = context->pcmROMImage == NULL ? NULL : context->pcmROMImage->getROMInfo();
	fillROMInfo(rom_info, controlROMInfo, pcmROMInfo);
}

void MT32EMU_C_CALL mt32emu_set_partial_count(mt32emu_context context, const mt32emu_bit32u partial_count) {
	context->partialCount = partial_count;
}

void MT32EMU_C_CALL mt32emu_set_analog_output_mode(mt32emu_context context, const mt32emu_analog_output_mode analog_output_mode) {
	context->analogOutputMode = static_cast<AnalogOutputMode>(analog_output_mode);
}

void MT32EMU_C_CALL mt32emu_set_stereo_output_samplerate(mt32emu_context context, const double samplerate) {
	context->srcState->outputSampleRate = SampleRateConverter::getSupportedOutputSampleRate(samplerate);
}

void MT32EMU_C_CALL mt32emu_set_samplerate_conversion_quality(mt32emu_context context, const mt32emu_samplerate_conversion_quality quality) {
	context->srcState->srcQuality = SamplerateConversionQuality(quality);
}

void MT32EMU_C_CALL mt32emu_select_renderer_type(mt32emu_context context, const mt32emu_renderer_type renderer_type) {
	context->synth->selectRendererType(static_cast<RendererType>(renderer_type));
}

mt32emu_renderer_type MT32EMU_C_CALL mt32emu_get_selected_renderer_type(mt32emu_context context) {
	return static_cast<mt32emu_renderer_type>(context->synth->getSelectedRendererType());
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_open_synth(mt32emu_const_context context) {
	if ((context->controlROMImage == NULL) || (context->pcmROMImage == NULL)) {
		return MT32EMU_RC_MISSING_ROMS;
	}
	if (!context->synth->open(*context->controlROMImage, *context->pcmROMImage, context->partialCount, context->analogOutputMode)) {
		return MT32EMU_RC_FAILED;
	}
	SamplerateConversionState &srcState = *context->srcState;
	const double outputSampleRate = (0.0 < srcState.outputSampleRate) ? srcState.outputSampleRate : context->synth->getStereoOutputSampleRate();
	srcState.src = new SampleRateConverter(*context->synth, outputSampleRate, srcState.srcQuality);
	return MT32EMU_RC_OK;
}

void MT32EMU_C_CALL mt32emu_close_synth(mt32emu_const_context context) {
	context->synth->close();
	delete context->srcState->src;
	context->srcState->src = NULL;
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_open(mt32emu_const_context context) {
	return context->synth->isOpen() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_actual_stereo_output_samplerate(mt32emu_const_context context) {
	if (context->srcState->src == NULL) {
		return context->synth->getStereoOutputSampleRate();
	}
	return mt32emu_bit32u(0.5 + context->srcState->src->convertSynthToOutputTimestamp(SAMPLE_RATE));
}

mt32emu_bit32u MT32EMU_C_CALL mt32emu_convert_output_to_synth_timestamp(mt32emu_const_context context, mt32emu_bit32u output_timestamp) {
	if (context->srcState->src == NULL) {
		return output_timestamp;
	}
	return mt32emu_bit32u(0.5 + context->srcState->src->convertOutputToSynthTimestamp(output_timestamp));
}

mt32emu_bit32u MT32EMU_C_CALL mt32emu_convert_synth_to_output_timestamp(mt32emu_const_context context, mt32emu_bit32u synth_timestamp) {
	if (context->srcState->src == NULL) {
		return synth_timestamp;
	}
	return mt32emu_bit32u(0.5 + context->srcState->src->convertSynthToOutputTimestamp(synth_timestamp));
}

void MT32EMU_C_CALL mt32emu_flush_midi_queue(mt32emu_const_context context) {
	context->synth->flushMIDIQueue();
}

mt32emu_bit32u MT32EMU_C_CALL mt32emu_set_midi_event_queue_size(mt32emu_const_context context, const mt32emu_bit32u queue_size) {
	return context->synth->setMIDIEventQueueSize(queue_size);
}

void MT32EMU_C_CALL mt32emu_configure_midi_event_queue_sysex_storage(mt32emu_const_context context, const mt32emu_bit32u storage_buffer_size) {
	context->synth->configureMIDIEventQueueSysexStorage(storage_buffer_size);
}

void MT32EMU_C_CALL mt32emu_set_midi_receiver(mt32emu_context context, mt32emu_midi_receiver_i midi_receiver, void *instance_data) {
	delete context->midiParser;
	context->midiParser = (midi_receiver.v0 != NULL) ? new DelegatingMidiStreamParser(context, midi_receiver, instance_data) : new DefaultMidiStreamParser(*context->synth);
}

mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_internal_rendered_sample_count(mt32emu_const_context context) {
	return context->synth->getInternalRenderedSampleCount();
}

void MT32EMU_C_CALL mt32emu_parse_stream(mt32emu_const_context context, const mt32emu_bit8u *stream, mt32emu_bit32u length) {
	context->midiParser->resetTimestamp();
	context->midiParser->parseStream(stream, length);
}

void MT32EMU_C_CALL mt32emu_parse_stream_at(mt32emu_const_context context, const mt32emu_bit8u *stream, mt32emu_bit32u length, mt32emu_bit32u timestamp) {
	context->midiParser->setTimestamp(timestamp);
	context->midiParser->parseStream(stream, length);
}

void MT32EMU_C_CALL mt32emu_play_short_message(mt32emu_const_context context, mt32emu_bit32u message) {
	context->midiParser->resetTimestamp();
	context->midiParser->processShortMessage(message);
}

void MT32EMU_C_CALL mt32emu_play_short_message_at(mt32emu_const_context context, mt32emu_bit32u message, mt32emu_bit32u timestamp) {
	context->midiParser->setTimestamp(timestamp);
	context->midiParser->processShortMessage(message);
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_play_msg(mt32emu_const_context context, mt32emu_bit32u msg) {
	if (!context->synth->isOpen()) return MT32EMU_RC_NOT_OPENED;
	return (context->synth->playMsg(msg)) ? MT32EMU_RC_OK : MT32EMU_RC_QUEUE_FULL;
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_play_sysex(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len) {
	if (!context->synth->isOpen()) return MT32EMU_RC_NOT_OPENED;
	return (context->synth->playSysex(sysex, len)) ? MT32EMU_RC_OK : MT32EMU_RC_QUEUE_FULL;
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_play_msg_at(mt32emu_const_context context, mt32emu_bit32u msg, mt32emu_bit32u timestamp) {
	if (!context->synth->isOpen()) return MT32EMU_RC_NOT_OPENED;
	return (context->synth->playMsg(msg, timestamp)) ? MT32EMU_RC_OK : MT32EMU_RC_QUEUE_FULL;
}

mt32emu_return_code MT32EMU_C_CALL mt32emu_play_sysex_at(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len, mt32emu_bit32u timestamp) {
	if (!context->synth->isOpen()) return MT32EMU_RC_NOT_OPENED;
	return (context->synth->playSysex(sysex, len, timestamp)) ? MT32EMU_RC_OK : MT32EMU_RC_QUEUE_FULL;
}

void MT32EMU_C_CALL mt32emu_play_msg_now(mt32emu_const_context context, mt32emu_bit32u msg) {
	context->synth->playMsgNow(msg);
}

void MT32EMU_C_CALL mt32emu_play_msg_on_part(mt32emu_const_context context, mt32emu_bit8u part, mt32emu_bit8u code, mt32emu_bit8u note, mt32emu_bit8u velocity) {
	context->synth->playMsgOnPart(part, code, note, velocity);
}

void MT32EMU_C_CALL mt32emu_play_sysex_now(mt32emu_const_context context, const mt32emu_bit8u *sysex, mt32emu_bit32u len) {
	context->synth->playSysexNow(sysex, len);
}

void MT32EMU_C_CALL mt32emu_write_sysex(mt32emu_const_context context, mt32emu_bit8u channel, const mt32emu_bit8u *sysex, mt32emu_bit32u len) {
	context->synth->writeSysex(channel, sysex, len);
}

void MT32EMU_C_CALL mt32emu_set_reverb_enabled(mt32emu_const_context context, const mt32emu_boolean reverb_enabled) {
	context->synth->setReverbEnabled(reverb_enabled != MT32EMU_BOOL_FALSE);
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_reverb_enabled(mt32emu_const_context context) {
	return context->synth->isReverbEnabled() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

void MT32EMU_C_CALL mt32emu_set_reverb_overridden(mt32emu_const_context context, const mt32emu_boolean reverb_overridden) {
	context->synth->setReverbOverridden(reverb_overridden != MT32EMU_BOOL_FALSE);
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_reverb_overridden(mt32emu_const_context context) {
	return context->synth->isReverbOverridden() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

void MT32EMU_C_CALL mt32emu_set_reverb_compatibility_mode(mt32emu_const_context context, const mt32emu_boolean mt32_compatible_mode) {
	context->synth->setReverbCompatibilityMode(mt32_compatible_mode != MT32EMU_BOOL_FALSE);
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_mt32_reverb_compatibility_mode(mt32emu_const_context context) {
	return context->synth->isMT32ReverbCompatibilityMode() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_default_reverb_mt32_compatible(mt32emu_const_context context) {
	return context->synth->isDefaultReverbMT32Compatible() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

void MT32EMU_C_CALL mt32emu_preallocate_reverb_memory(mt32emu_const_context context, const mt32emu_boolean enabled) {
	context->synth->preallocateReverbMemory(enabled != MT32EMU_BOOL_FALSE);
}

void MT32EMU_C_CALL mt32emu_set_dac_input_mode(mt32emu_const_context context, const mt32emu_dac_input_mode mode) {
	context->synth->setDACInputMode(static_cast<DACInputMode>(mode));
}

mt32emu_dac_input_mode MT32EMU_C_CALL mt32emu_get_dac_input_mode(mt32emu_const_context context) {
	return static_cast<mt32emu_dac_input_mode>(context->synth->getDACInputMode());
}

void MT32EMU_C_CALL mt32emu_set_midi_delay_mode(mt32emu_const_context context, const mt32emu_midi_delay_mode mode) {
	context->synth->setMIDIDelayMode(static_cast<MIDIDelayMode>(mode));
}

mt32emu_midi_delay_mode MT32EMU_C_CALL mt32emu_get_midi_delay_mode(mt32emu_const_context context) {
	return static_cast<mt32emu_midi_delay_mode>(context->synth->getMIDIDelayMode());
}

void MT32EMU_C_CALL mt32emu_set_output_gain(mt32emu_const_context context, float gain) {
	context->synth->setOutputGain(gain);
}

float MT32EMU_C_CALL mt32emu_get_output_gain(mt32emu_const_context context) {
	return context->synth->getOutputGain();
}

void MT32EMU_C_CALL mt32emu_set_reverb_output_gain(mt32emu_const_context context, float gain) {
	context->synth->setReverbOutputGain(gain);
}

float MT32EMU_C_CALL mt32emu_get_reverb_output_gain(mt32emu_const_context context) {
	return context->synth->getReverbOutputGain();
}

void MT32EMU_C_CALL mt32emu_set_part_volume_override(mt32emu_const_context context, mt32emu_bit8u part_number, mt32emu_bit8u volume_override) {
	context->synth->setPartVolumeOverride(part_number, volume_override);
}

mt32emu_bit8u MT32EMU_C_CALL mt32emu_get_part_volume_override(mt32emu_const_context context, mt32emu_bit8u part_number) {
	return context->synth->getPartVolumeOverride(part_number);
}

void MT32EMU_C_CALL mt32emu_set_reversed_stereo_enabled(mt32emu_const_context context, const mt32emu_boolean enabled) {
	context->synth->setReversedStereoEnabled(enabled != MT32EMU_BOOL_FALSE);
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_reversed_stereo_enabled(mt32emu_const_context context) {
	return context->synth->isReversedStereoEnabled() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

void MT32EMU_C_CALL mt32emu_set_nice_amp_ramp_enabled(mt32emu_const_context context, const mt32emu_boolean enabled) {
	context->synth->setNiceAmpRampEnabled(enabled != MT32EMU_BOOL_FALSE);
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_nice_amp_ramp_enabled(mt32emu_const_context context) {
	return context->synth->isNiceAmpRampEnabled() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

void MT32EMU_C_CALL mt32emu_set_nice_panning_enabled(mt32emu_const_context context, const mt32emu_boolean enabled) {
	context->synth->setNicePanningEnabled(enabled != MT32EMU_BOOL_FALSE);
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_nice_panning_enabled(mt32emu_const_context context) {
	return context->synth->isNicePanningEnabled() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

void MT32EMU_C_CALL mt32emu_set_nice_partial_mixing_enabled(mt32emu_const_context context, const mt32emu_boolean enabled) {
	context->synth->setNicePartialMixingEnabled(enabled != MT32EMU_BOOL_FALSE);
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_nice_partial_mixing_enabled(mt32emu_const_context context) {
	return context->synth->isNicePartialMixingEnabled() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

void MT32EMU_C_CALL mt32emu_render_bit16s(mt32emu_const_context context, mt32emu_bit16s *stream, mt32emu_bit32u len) {
	if (context->srcState->src != NULL) {
		context->srcState->src->getOutputSamples(stream, len);
	} else {
		context->synth->render(stream, len);
	}
}

void MT32EMU_C_CALL mt32emu_render_float(mt32emu_const_context context, float *stream, mt32emu_bit32u len) {
	if (context->srcState->src != NULL) {
		context->srcState->src->getOutputSamples(stream, len);
	} else {
		context->synth->render(stream, len);
	}
}

void MT32EMU_C_CALL mt32emu_render_bit16s_streams(mt32emu_const_context context, const mt32emu_dac_output_bit16s_streams *streams, mt32emu_bit32u len) {
	context->synth->renderStreams(*reinterpret_cast<const DACOutputStreams<Bit16s> *>(streams), len);
}

void MT32EMU_C_CALL mt32emu_render_float_streams(mt32emu_const_context context, const mt32emu_dac_output_float_streams *streams, mt32emu_bit32u len) {
	context->synth->renderStreams(*reinterpret_cast<const DACOutputStreams<float> *>(streams), len);
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_has_active_partials(mt32emu_const_context context) {
	return context->synth->hasActivePartials() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_active(mt32emu_const_context context) {
	return context->synth->isActive() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_partial_count(mt32emu_const_context context) {
	return context->synth->getPartialCount();
}

mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_part_states(mt32emu_const_context context) {
	return context->synth->getPartStates();
}

void MT32EMU_C_CALL mt32emu_get_partial_states(mt32emu_const_context context, mt32emu_bit8u *partial_states) {
	context->synth->getPartialStates(partial_states);
}

mt32emu_bit32u MT32EMU_C_CALL mt32emu_get_playing_notes(mt32emu_const_context context, mt32emu_bit8u part_number, mt32emu_bit8u *keys, mt32emu_bit8u *velocities) {
	return context->synth->getPlayingNotes(part_number, keys, velocities);
}

const char * MT32EMU_C_CALL mt32emu_get_patch_name(mt32emu_const_context context, mt32emu_bit8u part_number) {
	return context->synth->getPatchName(part_number);
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_get_sound_group_name(mt32emu_const_context context, char *sound_group_name, mt32emu_bit8u timbre_group, mt32emu_bit8u timbre_number) {
	return context->synth->getSoundGroupName(sound_group_name, timbre_group, timbre_number) ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_get_sound_name(mt32emu_const_context context, char *sound_name, mt32emu_bit8u timbre_group, mt32emu_bit8u timbre_number) {
	return context->synth->getSoundName(sound_name, timbre_group, timbre_number) ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

void MT32EMU_C_CALL mt32emu_read_memory(mt32emu_const_context context, mt32emu_bit32u addr, mt32emu_bit32u len, mt32emu_bit8u *data) {
	context->synth->readMemory(addr, len, data);
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_get_display_state(mt32emu_const_context context, char *target_buffer, const mt32emu_boolean narrow_lcd) {
	return context->synth->getDisplayState(target_buffer, narrow_lcd != MT32EMU_BOOL_FALSE) ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

void MT32EMU_C_CALL mt32emu_set_main_display_mode(mt32emu_const_context context) {
	context->synth->setMainDisplayMode();
}

void MT32EMU_C_CALL mt32emu_set_display_compatibility(mt32emu_const_context context, mt32emu_boolean old_mt32_compatibility_enabled) {
	context->synth->setDisplayCompatibility(old_mt32_compatibility_enabled != MT32EMU_BOOL_FALSE);
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_display_old_mt32_compatible(mt32emu_const_context context) {
	return context->synth->isDisplayOldMT32Compatible() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

mt32emu_boolean MT32EMU_C_CALL mt32emu_is_default_display_old_mt32_compatible(mt32emu_const_context context) {
	return context->synth->isDefaultDisplayOldMT32Compatible() ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE;
}

} // extern "C"
