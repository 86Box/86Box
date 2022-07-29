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

/* Using two guards since this file may be included twice with different MT32EMU_C_ENUMERATIONS define. */

#if (!defined MT32EMU_CPP_ENUMERATIONS_H && !defined MT32EMU_C_ENUMERATIONS) || (!defined MT32EMU_C_ENUMERATIONS_H && defined MT32EMU_C_ENUMERATIONS)

#ifdef MT32EMU_C_ENUMERATIONS

#define MT32EMU_C_ENUMERATIONS_H

#define MT32EMU_DAC_INPUT_MODE_NAME mt32emu_dac_input_mode
#define MT32EMU_DAC_INPUT_MODE(ident) MT32EMU_DAC_##ident

#define MT32EMU_MIDI_DELAY_MODE_NAME mt32emu_midi_delay_mode
#define MT32EMU_MIDI_DELAY_MODE(ident) MT32EMU_MDM_##ident

#define MT32EMU_ANALOG_OUTPUT_MODE_NAME mt32emu_analog_output_mode
#define MT32EMU_ANALOG_OUTPUT_MODE(ident) MT32EMU_AOM_##ident

#define MT32EMU_PARTIAL_STATE_NAME mt32emu_partial_state
#define MT32EMU_PARTIAL_STATE(ident) MT32EMU_PS_##ident

#define MT32EMU_SAMPLERATE_CONVERSION_QUALITY_NAME mt32emu_samplerate_conversion_quality
#define MT32EMU_SAMPLERATE_CONVERSION_QUALITY(ident) MT32EMU_SRCQ_##ident

#define MT32EMU_RENDERER_TYPE_NAME mt32emu_renderer_type
#define MT32EMU_RENDERER_TYPE(ident) MT32EMU_RT_##ident

#else /* #ifdef MT32EMU_C_ENUMERATIONS */

#define MT32EMU_CPP_ENUMERATIONS_H

#define MT32EMU_DAC_INPUT_MODE_NAME DACInputMode
#define MT32EMU_DAC_INPUT_MODE(ident) DACInputMode_##ident

#define MT32EMU_MIDI_DELAY_MODE_NAME MIDIDelayMode
#define MT32EMU_MIDI_DELAY_MODE(ident) MIDIDelayMode_##ident

#define MT32EMU_ANALOG_OUTPUT_MODE_NAME AnalogOutputMode
#define MT32EMU_ANALOG_OUTPUT_MODE(ident) AnalogOutputMode_##ident

#define MT32EMU_PARTIAL_STATE_NAME PartialState
#define MT32EMU_PARTIAL_STATE(ident) PartialState_##ident

#define MT32EMU_SAMPLERATE_CONVERSION_QUALITY_NAME SamplerateConversionQuality
#define MT32EMU_SAMPLERATE_CONVERSION_QUALITY(ident) SamplerateConversionQuality_##ident

#define MT32EMU_RENDERER_TYPE_NAME RendererType
#define MT32EMU_RENDERER_TYPE(ident) RendererType_##ident

namespace MT32Emu {

#endif /* #ifdef MT32EMU_C_ENUMERATIONS */

/**
 * Methods for emulating the connection between the LA32 and the DAC, which involves
 * some hacks in the real devices for doubling the volume.
 * See also http://en.wikipedia.org/wiki/Roland_MT-32#Digital_overflow
 */
enum MT32EMU_DAC_INPUT_MODE_NAME {
	/**
	 * Produces samples at double the volume, without tricks.
	 * Nicer overdrive characteristics than the DAC hacks (it simply clips samples within range)
	 * Higher quality than the real devices
	 */
	MT32EMU_DAC_INPUT_MODE(NICE),

	/**
	 * Produces samples that exactly match the bits output from the emulated LA32.
	 * Nicer overdrive characteristics than the DAC hacks (it simply clips samples within range)
	 * Much less likely to overdrive than any other mode.
	 * Half the volume of any of the other modes.
	 * Perfect for developers while debugging :)
	 */
	MT32EMU_DAC_INPUT_MODE(PURE),

	/**
	 * Re-orders the LA32 output bits as in early generation MT-32s (according to Wikipedia).
	 * Bit order at DAC (where each number represents the original LA32 output bit number, and XX means the bit is always low):
	 * 15 13 12 11 10 09 08 07 06 05 04 03 02 01 00 XX
	 */
	MT32EMU_DAC_INPUT_MODE(GENERATION1),

	/**
	 * Re-orders the LA32 output bits as in later generations (personally confirmed on my CM-32L - KG).
	 * Bit order at DAC (where each number represents the original LA32 output bit number):
	 * 15 13 12 11 10 09 08 07 06 05 04 03 02 01 00 14
	 */
	MT32EMU_DAC_INPUT_MODE(GENERATION2)
};

/** Methods for emulating the effective delay of incoming MIDI messages introduced by a MIDI interface. */
enum MT32EMU_MIDI_DELAY_MODE_NAME {
	/** Process incoming MIDI events immediately. */
	MT32EMU_MIDI_DELAY_MODE(IMMEDIATE),

	/**
	 * Delay incoming short MIDI messages as if they where transferred via a MIDI cable to a real hardware unit and immediate sysex processing.
	 * This ensures more accurate timing of simultaneous NoteOn messages.
	 */
	MT32EMU_MIDI_DELAY_MODE(DELAY_SHORT_MESSAGES_ONLY),

	/** Delay all incoming MIDI events as if they where transferred via a MIDI cable to a real hardware unit.*/
	MT32EMU_MIDI_DELAY_MODE(DELAY_ALL)
};

/** Methods for emulating the effects of analogue circuits of real hardware units on the output signal. */
enum MT32EMU_ANALOG_OUTPUT_MODE_NAME {
	/** Only digital path is emulated. The output samples correspond to the digital signal at the DAC entrance. */
	MT32EMU_ANALOG_OUTPUT_MODE(DIGITAL_ONLY),
	/** Coarse emulation of LPF circuit. High frequencies are boosted, sample rate remains unchanged. */
	MT32EMU_ANALOG_OUTPUT_MODE(COARSE),
	/**
	 * Finer emulation of LPF circuit. Output signal is upsampled to 48 kHz to allow emulation of audible mirror spectra above 16 kHz,
	 * which is passed through the LPF circuit without significant attenuation.
	 */
	MT32EMU_ANALOG_OUTPUT_MODE(ACCURATE),
	/**
	 * Same as AnalogOutputMode_ACCURATE mode but the output signal is 2x oversampled, i.e. the output sample rate is 96 kHz.
	 * This makes subsequent resampling easier. Besides, due to nonlinear passband of the LPF emulated, it takes fewer number of MACs
	 * compared to a regular LPF FIR implementations.
	 */
	MT32EMU_ANALOG_OUTPUT_MODE(OVERSAMPLED)
};

enum MT32EMU_PARTIAL_STATE_NAME {
	MT32EMU_PARTIAL_STATE(INACTIVE),
	MT32EMU_PARTIAL_STATE(ATTACK),
	MT32EMU_PARTIAL_STATE(SUSTAIN),
	MT32EMU_PARTIAL_STATE(RELEASE)
};

enum MT32EMU_SAMPLERATE_CONVERSION_QUALITY_NAME {
	/** Use this only when the speed is more important than the audio quality. */
	MT32EMU_SAMPLERATE_CONVERSION_QUALITY(FASTEST),
	MT32EMU_SAMPLERATE_CONVERSION_QUALITY(FAST),
	MT32EMU_SAMPLERATE_CONVERSION_QUALITY(GOOD),
	MT32EMU_SAMPLERATE_CONVERSION_QUALITY(BEST)
};

enum MT32EMU_RENDERER_TYPE_NAME {
	/** Use 16-bit signed samples in the renderer and the accurate wave generator model based on logarithmic fixed-point computations and LUTs. Maximum emulation accuracy and speed. */
	MT32EMU_RENDERER_TYPE(BIT16S),
	/** Use float samples in the renderer and simplified wave generator model. Maximum output quality and minimum noise. */
	MT32EMU_RENDERER_TYPE(FLOAT)
};

#ifndef MT32EMU_C_ENUMERATIONS

} // namespace MT32Emu

#endif

#undef MT32EMU_DAC_INPUT_MODE_NAME
#undef MT32EMU_DAC_INPUT_MODE

#undef MT32EMU_MIDI_DELAY_MODE_NAME
#undef MT32EMU_MIDI_DELAY_MODE

#undef MT32EMU_ANALOG_OUTPUT_MODE_NAME
#undef MT32EMU_ANALOG_OUTPUT_MODE

#undef MT32EMU_PARTIAL_STATE_NAME
#undef MT32EMU_PARTIAL_STATE

#undef MT32EMU_SAMPLERATE_CONVERSION_QUALITY_NAME
#undef MT32EMU_SAMPLERATE_CONVERSION_QUALITY

#undef MT32EMU_RENDERER_TYPE_NAME
#undef MT32EMU_RENDERER_TYPE

#endif /* #if (!defined MT32EMU_CPP_ENUMERATIONS_H && !defined MT32EMU_C_ENUMERATIONS) || (!defined MT32EMU_C_ENUMERATIONS_H && defined MT32EMU_C_ENUMERATIONS) */
