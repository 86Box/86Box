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

#include <cstdlib>
#include <cstring>

#include "internals.h"

#include "Display.h"
#include "Part.h"
#include "Structures.h"
#include "Synth.h"

namespace MT32Emu {

/* Details on the emulation model.
 *
 * There are four display modes emulated:
 * - main (Master Volume), set upon startup after showing the welcoming banner;
 * - program change notification;
 * - custom display message received via a SysEx;
 * - error banner (e.g. the MIDI message checksum error).
 * Stuff like cursor blinking, patch selection mode, test mode, reaction to the front panel buttons, etc. is out of scope, as more
 * convenient UI/UX solutions are likely desired in applications, if at all.
 *
 * Note, despite the LAPC and CM devices come without the LCD and the front panel buttons, the control ROM does support these,
 * if connected to the main board. That's intended for running the test mode in a service centre, as documented.
 *
 * Within the aforementioned scope, the observable hardware behaviour differs noticeably, depending on the control ROM version.
 * At least three milestones can be identified:
 * - with MT-32 control ROM V1.06, custom messages are no longer shown unless the display is in the main (Master Volume) mode;
 * - with MT-32 control ROM V2.04, new function introduced - Display Reset yet added many other changes (taking the full SysEx
 *   address into account when processing custom messages and special handling of the ASCII control characters are among them);
 *   all the second-gen devices, including LAPC-I and CM-32L, behave very similarly;
 * - in the third-gen devices, the LCD support was partially cut down in the control ROM (basically, only the status
 *   of the test mode, the ROM version and the checksum warnings are shown) - it's not fun, so this is NOT emulated.
 *
 * Features of the old-gen units.
 * - Any message with the first address byte 0x20 is processed and has some effect on the LCD. Messages with any other first
 *   address byte (e.g. starting with 0x21 or 0x1F7F7F with an overlap) are not considered display-relevant.
 * - The second and the third address byte are largely irrelevant. Only presence of the second address byte makes an observable
 *   difference, not the data within.
 * - Any string received in the custom message is normalised - all ASCII control characters are replaced with spaces, messages
 *   shorter than 20 bytes are filled up with spaces to the full supported length. However, should a timbre name contain an ASCII
 *   control character, it is displayed nevertheless, with zero meaning the end-of-string.
 * - Special message 0x20 (of just 1 address byte) shows the contents of the custom message buffer with either the last received
 *   message or the empty buffer initially filled with spaces. See the note below about the priorities of the display modes.
 * - Messages containing two or three bytes with just the address are considered empty and fill the custom message buffer with
 *   all spaces. The contents of the empty buffer is then shown, depending on the priority of the current display mode.
 * - Timing: custom messages are shown until an external event occurs like pressing a front panel button, receiving a new custom
 *   message, program change, etc., and for indefinitely long otherwise. A program change notification is shown for about 1300
 *   milliseconds; when the timer expires, the display returns to the main mode (irrespective to the current display mode).
 *   When an error occurs, the warning is shown for a limited time only, similarly to the program change notifications.
 * - The earlier old-gen devices treat all display modes with equal priority, except the main mode, which has a lower one. This
 *   makes it possible e.g. to replace the error banner with a custom message or a program change notification, and so on.
 *   A slightly improved behaviour is observed since the control ROM V1.06, when custom messages were de-prioritised. But still,
 *   a program change beats an error banner even in the later models.
 *
 * Features of the second-gen units.
 * - All three bytes in SysEx address are now relevant.
 *   - It is possible to replace individual characters in the custom message buffer which are addressed individually within
 *     the range 0x200000-0x200013.
 *   - Writes to higher addresses up to 0x20007F simply make the custom message buffer shown, with either the last received message
 *     or the empty buffer initially filled with spaces.
 *   - Writes to address 0x200100 trigger the Display Reset function which resets the display to the main (Master Volume) mode.
 *     Similarly, showing an error banner is ended. If a program change notification is shown, this function does nothing, however.
 *   - Writes to other addresses are not considered display-relevant, albeit writing a long string to lower addresses
 *     (e.g. 0x1F7F7F) that overlaps the display range does result in updating and showing the custom display message.
 *   - Writing a long string that covers the custom message buffer and address 0x200100 does both things, i.e. updates the buffer
 *     and triggers the Display Reset function.
 * - While the display is not in a user interaction mode, custom messages and error banners have the highest display priority.
 *   As long as these are shown, program change notifications are suppressed. The display only leaves this mode when the Display
 *   Reset function is triggered or a front panel button is pressed. Notably, when the user enters the menu, all custom messages
 *   are ignored, including the Display Reset command, but error banners are shown nevertheless.
 * - Sending cut down messages with partially specified address rather leads to undefined behaviour, except for a two-byte message
 *   0x20 0x00 which consistently shows the content of the custom message buffer (if priority permits). Otherwise, the behaviour
 *   depends on the previously submitted address, e.g. the two-byte version of Display Reset may fail depending on the third byte
 *   of the previous message. One-byte message 0x20 seemingly does Display Reset yet writes a zero character to a position derived
 *   from the third byte of the preceding message.
 *
 * Some notes on the behaviour that is common to all hardware models.
 * - The display is DM2011 with LSI SED1200D-0A. This unit supports 4 user-programmable characters stored in CGRAM, all 4 get
 *   loaded at startup. Character #0 is empty (with the cursor underline), #1 is the full block (used to mark active parts),
 *   #2 is the pipe character (identical to #124 from the CGROM) and #3 is a variation on "down arrow". During normal operation,
 *   those duplicated characters #2 and #124 are both used in different places and character #3 can only be made visible by adding
 *   it either to a custom timbre name or a custom message. Character #0 is probably never shown as this code has special meaning
 *   in the processing routines. For simplicity, we only use characters #124 and #1 in this model.
 * - When the main mode is active, the current state of the first 5 parts and the rhythm part is represented by replacing the part
 *   symbol with the full rectangle character (#1 from the CGRAM). For voice parts, the rectangle is shown as long as at least one
 *   partial is playing in a non-releasing phase on that part. For the rhythm part, the rectangle blinks briefly when a new NoteOn
 *   message is received on that part (sometimes even when that actually produces no sound).
 */

static const char MASTER_VOLUME_WITH_DELIMITER[] = "|  0";
static const char MASTER_VOLUME_WITH_DELIMITER_AND_PREFIX[] = "|vol:  0";
static const Bit8u RHYTHM_PART_CODE = 'R';
static const Bit8u FIELD_DELIMITER = '|';
static const Bit8u ACTIVE_PART_INDICATOR = 1;

static const Bit32u DISPLAYED_VOICE_PARTS_COUNT = 5;
static const Bit32u SOUND_GROUP_NAME_WITH_DELIMITER_SIZE = 8;
static const Bit32u MASTER_VOLUME_WITH_DELIMITER_SIZE = sizeof(MASTER_VOLUME_WITH_DELIMITER) - 1;
static const Bit32u MASTER_VOLUME_WITH_DELIMITER_AND_PREFIX_SIZE = sizeof(MASTER_VOLUME_WITH_DELIMITER_AND_PREFIX) - 1;

// This is the period to show those short blinks of MIDI MESSAGE LED and the rhythm part state.
// Two related countdowns are initialised to 8 and touched each 10 milliseconds by the software timer 0 interrupt handler.
static const Bit32u BLINK_TIME_MILLIS = 80;
static const Bit32u BLINK_TIME_FRAMES = BLINK_TIME_MILLIS * SAMPLE_RATE / 1000;

// This is based on the (free-running) TIMER1 overflow interrupt. The timer is 16-bit and clocked at 500KHz.
// The message is displayed until 10 overflow interrupts occur. At the standard sample rate, it counts
// precisely as 41943.04 frame times.
static const Bit32u SCHEDULED_DISPLAY_MODE_RESET_FRAMES = 41943;

/**
 * Copies up to lengthLimit characters from possibly null-terminated source to destination. The character of destination located
 * at the position of the null terminator (if any) in source and the rest of destination are left untouched.
 */
static void copyNullTerminatedString(Bit8u *destination, const Bit8u *source, Bit32u lengthLimit) {
	for (Bit32u i = 0; i < lengthLimit; i++) {
		Bit8u c = source[i];
		if (c == 0) break;
		destination[i] = c;
	}
}

Display::Display(Synth &useSynth) :
	synth(useSynth),
	lastLEDState(),
	lcdDirty(),
	lcdUpdateSignalled(),
	lastRhythmPartState(),
	mode(Mode_STARTUP_MESSAGE),
	midiMessagePlayedSinceLastReset(),
	rhythmNotePlayedSinceLastReset()
{
	scheduleDisplayReset();
	const Bit8u *startupMessage = &synth.controlROMData[synth.controlROMMap->startupMessage];
	memcpy(displayBuffer, startupMessage, LCD_TEXT_SIZE);
	memset(customMessageBuffer, ' ', LCD_TEXT_SIZE);
	memset(voicePartStates, 0, sizeof voicePartStates);
}

void Display::checkDisplayStateUpdated(bool &midiMessageLEDState, bool &midiMessageLEDUpdated, bool &lcdUpdated) {
	midiMessageLEDState = midiMessagePlayedSinceLastReset;
	maybeResetTimer(midiMessagePlayedSinceLastReset, midiMessageLEDResetTimestamp);
	// Note, the LED represents activity of the voice parts only.
	for (Bit32u partIndex = 0; !midiMessageLEDState && partIndex < 8; partIndex++) {
		midiMessageLEDState = voicePartStates[partIndex];
	}
	midiMessageLEDUpdated = lastLEDState != midiMessageLEDState;
	lastLEDState = midiMessageLEDState;

	if (displayResetScheduled && shouldResetTimer(displayResetTimestamp)) setMainDisplayMode();

	if (lastRhythmPartState != rhythmNotePlayedSinceLastReset && mode == Mode_MAIN) lcdDirty = true;
	lastRhythmPartState = rhythmNotePlayedSinceLastReset;
	maybeResetTimer(rhythmNotePlayedSinceLastReset, rhythmStateResetTimestamp);

	lcdUpdated = lcdDirty && !lcdUpdateSignalled;
	if (lcdUpdated) lcdUpdateSignalled = true;
}

bool Display::getDisplayState(char *targetBuffer, bool narrowLCD) {
	if (lcdUpdateSignalled) {
		lcdDirty = false;
		lcdUpdateSignalled = false;

		switch (mode) {
		case Mode_CUSTOM_MESSAGE:
			if (synth.isDisplayOldMT32Compatible()) {
				memcpy(displayBuffer, customMessageBuffer, LCD_TEXT_SIZE);
			} else {
				copyNullTerminatedString(displayBuffer, customMessageBuffer, LCD_TEXT_SIZE);
			}
			break;
		case Mode_ERROR_MESSAGE: {
			const Bit8u *sysexErrorMessage = &synth.controlROMData[synth.controlROMMap->sysexErrorMessage];
			memcpy(displayBuffer, sysexErrorMessage, LCD_TEXT_SIZE);
			break;
		}
		case Mode_PROGRAM_CHANGE: {
			Bit8u *writePosition = displayBuffer;
			*writePosition++ = '1' + lastProgramChangePartIndex;
			*writePosition++ = FIELD_DELIMITER;
			if (narrowLCD) {
				writePosition[TIMBRE_NAME_SIZE] = 0;
			} else {
				memcpy(writePosition, lastProgramChangeSoundGroupName, SOUND_GROUP_NAME_WITH_DELIMITER_SIZE);
				writePosition += SOUND_GROUP_NAME_WITH_DELIMITER_SIZE;
			}
			copyNullTerminatedString(writePosition, lastProgramChangeTimbreName, TIMBRE_NAME_SIZE);
			break;
		}
		case Mode_MAIN: {
			Bit8u *writePosition = displayBuffer;
			for (Bit32u partIndex = 0; partIndex < DISPLAYED_VOICE_PARTS_COUNT; partIndex++) {
				*writePosition++ = voicePartStates[partIndex] ? ACTIVE_PART_INDICATOR : '1' + partIndex;
				*writePosition++ = ' ';
			}
			*writePosition++ = lastRhythmPartState ? ACTIVE_PART_INDICATOR : RHYTHM_PART_CODE;
			*writePosition++ = ' ';
			if (narrowLCD) {
				memcpy(writePosition, MASTER_VOLUME_WITH_DELIMITER, MASTER_VOLUME_WITH_DELIMITER_SIZE);
				writePosition += MASTER_VOLUME_WITH_DELIMITER_SIZE;
				*writePosition = 0;
			} else {
				memcpy(writePosition, MASTER_VOLUME_WITH_DELIMITER_AND_PREFIX, MASTER_VOLUME_WITH_DELIMITER_AND_PREFIX_SIZE);
				writePosition += MASTER_VOLUME_WITH_DELIMITER_AND_PREFIX_SIZE;
			}
			Bit32u masterVol = synth.mt32ram.system.masterVol;
			while (masterVol > 0) {
				std::div_t result = std::div(masterVol, 10);
				*--writePosition = '0' + result.rem;
				masterVol = result.quot;
			}
			break;
		}
		default:
			break;
		}
	}

	memcpy(targetBuffer, displayBuffer, LCD_TEXT_SIZE);
	targetBuffer[LCD_TEXT_SIZE] = 0;
	return lastLEDState;
}

void Display::setMainDisplayMode() {
	displayResetScheduled = false;
	mode = Mode_MAIN;
	lcdDirty = true;
}

void Display::midiMessagePlayed() {
	midiMessagePlayedSinceLastReset = true;
	midiMessageLEDResetTimestamp = synth.renderedSampleCount + BLINK_TIME_FRAMES;
}

void Display::rhythmNotePlayed() {
	rhythmNotePlayedSinceLastReset = true;
	rhythmStateResetTimestamp = synth.renderedSampleCount + BLINK_TIME_FRAMES;
	midiMessagePlayed();
	if (synth.isDisplayOldMT32Compatible() && mode == Mode_CUSTOM_MESSAGE) setMainDisplayMode();
}

void Display::voicePartStateChanged(Bit8u partIndex, bool activated) {
	if (mode == Mode_MAIN) lcdDirty = true;
	voicePartStates[partIndex] = activated;
	if (synth.isDisplayOldMT32Compatible() && mode == Mode_CUSTOM_MESSAGE) setMainDisplayMode();
}

void Display::masterVolumeChanged() {
	if (mode == Mode_MAIN) lcdDirty = true;
}

void Display::programChanged(Bit8u partIndex) {
	if (!synth.isDisplayOldMT32Compatible() && (mode == Mode_CUSTOM_MESSAGE || mode == Mode_ERROR_MESSAGE)) return;
	mode = Mode_PROGRAM_CHANGE;
	lcdDirty = true;
	scheduleDisplayReset();
	lastProgramChangePartIndex = partIndex;
	const Part *part = synth.getPart(partIndex);
	lastProgramChangeSoundGroupName = synth.getSoundGroupName(part);
	memcpy(lastProgramChangeTimbreName, part->getCurrentInstr(), TIMBRE_NAME_SIZE);
}

void Display::checksumErrorOccurred() {
	if (mode != Mode_ERROR_MESSAGE) {
		mode = Mode_ERROR_MESSAGE;
		lcdDirty = true;
	}
	if (synth.isDisplayOldMT32Compatible()) {
		scheduleDisplayReset();
	} else {
		displayResetScheduled = false;
	}
}

bool Display::customDisplayMessageReceived(const Bit8u *message, Bit32u startIndex, Bit32u length) {
	if (synth.isDisplayOldMT32Compatible()) {
		for (Bit32u i = 0; i < LCD_TEXT_SIZE; i++) {
			Bit8u c = i < length ? message[i] : ' ';
			if (c < 32 || 127 < c) c = ' ';
			customMessageBuffer[i] = c;
		}
		if (!synth.controlROMFeatures->quirkDisplayCustomMessagePriority
			&& (mode == Mode_PROGRAM_CHANGE || mode == Mode_ERROR_MESSAGE)) return false;
		// Note, real devices keep the display reset timer running.
	} else {
		if (startIndex > 0x80) return false;
		if (startIndex == 0x80) {
			if (mode != Mode_PROGRAM_CHANGE) setMainDisplayMode();
			return false;
		}
		displayResetScheduled = false;
		if (startIndex < LCD_TEXT_SIZE) {
			if (length > LCD_TEXT_SIZE - startIndex) length = LCD_TEXT_SIZE - startIndex;
			memcpy(customMessageBuffer + startIndex, message, length);
		}
	}
	mode = Mode_CUSTOM_MESSAGE;
	lcdDirty = true;
	return true;
}

void Display::displayControlMessageReceived(const Bit8u *messageBytes, Bit32u length) {
	Bit8u emptyMessage[] = { 0 };
	if (synth.isDisplayOldMT32Compatible()) {
		if (length == 1) {
			customDisplayMessageReceived(customMessageBuffer, 0, LCD_TEXT_SIZE);
		} else {
			customDisplayMessageReceived(emptyMessage, 0, 0);
		}
	} else {
		// Always assume the third byte to be zero for simplicity.
		if (length == 2) {
			customDisplayMessageReceived(emptyMessage, messageBytes[1] << 7, 0);
		} else if (length == 1) {
			customMessageBuffer[0] = 0;
			customDisplayMessageReceived(emptyMessage, 0x80, 0);
		}
	}
}

void Display::scheduleDisplayReset() {
	displayResetTimestamp = synth.renderedSampleCount + SCHEDULED_DISPLAY_MODE_RESET_FRAMES;
	displayResetScheduled = true;
}

bool Display::shouldResetTimer(Bit32u scheduledResetTimestamp) {
	// Deals with wrapping of renderedSampleCount.
	return Bit32s(scheduledResetTimestamp - synth.renderedSampleCount) < 0;
}

void Display::maybeResetTimer(bool &timerState, Bit32u scheduledResetTimestamp) {
	if (timerState && shouldResetTimer(scheduledResetTimestamp)) timerState = false;
}

} // namespace MT32Emu
