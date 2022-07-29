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
#include <cstring>

#include "internals.h"

#include "MidiStreamParser.h"
#include "Synth.h"

using namespace MT32Emu;

DefaultMidiStreamParser::DefaultMidiStreamParser(Synth &useSynth, Bit32u initialStreamBufferCapacity) :
	MidiStreamParser(initialStreamBufferCapacity), synth(useSynth), timestampSet(false) {}

void DefaultMidiStreamParser::setTimestamp(const Bit32u useTimestamp) {
	timestampSet = true;
	timestamp = useTimestamp;
}

void DefaultMidiStreamParser::resetTimestamp() {
	timestampSet = false;
}

void DefaultMidiStreamParser::handleShortMessage(const Bit32u message) {
	do {
		if (timestampSet) {
			if (synth.playMsg(message, timestamp)) return;
		}
		else {
			if (synth.playMsg(message)) return;
		}
	} while (synth.reportHandler->onMIDIQueueOverflow());
}

void DefaultMidiStreamParser::handleSysex(const Bit8u *stream, const Bit32u length) {
	do {
		if (timestampSet) {
			if (synth.playSysex(stream, length, timestamp)) return;
		}
		else {
			if (synth.playSysex(stream, length)) return;
		}
	} while (synth.reportHandler->onMIDIQueueOverflow());
}

void DefaultMidiStreamParser::handleSystemRealtimeMessage(const Bit8u realtime) {
	synth.reportHandler->onMIDISystemRealtime(realtime);
}

void DefaultMidiStreamParser::printDebug(const char *debugMessage) {
	synth.printDebug("%s", debugMessage);
}

MidiStreamParser::MidiStreamParser(Bit32u initialStreamBufferCapacity) :
	MidiStreamParserImpl(*this, *this, initialStreamBufferCapacity) {}

MidiStreamParserImpl::MidiStreamParserImpl(MidiReceiver &useReceiver, MidiReporter &useReporter, Bit32u initialStreamBufferCapacity) :
	midiReceiver(useReceiver), midiReporter(useReporter)
{
	if (initialStreamBufferCapacity < SYSEX_BUFFER_SIZE) initialStreamBufferCapacity = SYSEX_BUFFER_SIZE;
	if (MAX_STREAM_BUFFER_SIZE < initialStreamBufferCapacity) initialStreamBufferCapacity = MAX_STREAM_BUFFER_SIZE;
	streamBufferCapacity = initialStreamBufferCapacity;
	streamBuffer = new Bit8u[streamBufferCapacity];
	streamBufferSize = 0;
	runningStatus = 0;

	reserved = NULL;
}

MidiStreamParserImpl::~MidiStreamParserImpl() {
	delete[] streamBuffer;
}

void MidiStreamParserImpl::parseStream(const Bit8u *stream, Bit32u length) {
	while (length > 0) {
		Bit32u parsedMessageLength = 0;
		if (0xF8 <= *stream) {
			// Process System Realtime immediately and go on
			midiReceiver.handleSystemRealtimeMessage(*stream);
			parsedMessageLength = 1;
			// No effect on the running status
		} else if (streamBufferSize > 0) {
			// Check if there is something in streamBuffer waiting for being processed
			if (*streamBuffer == 0xF0) {
				parsedMessageLength = parseSysexFragment(stream, length);
			} else {
				parsedMessageLength = parseShortMessageDataBytes(stream, length);
			}
		} else {
			if (*stream == 0xF0) {
				runningStatus = 0; // SysEx clears the running status
				parsedMessageLength = parseSysex(stream, length);
			} else {
				parsedMessageLength = parseShortMessageStatus(stream);
			}
		}

		// Parsed successfully
		stream += parsedMessageLength;
		length -= parsedMessageLength;
	}
}

void MidiStreamParserImpl::processShortMessage(const Bit32u message) {
	// Adds running status to the MIDI message if it doesn't contain one
	Bit8u status = Bit8u(message & 0xFF);
	if (0xF8 <= status) {
		midiReceiver.handleSystemRealtimeMessage(status);
	} else if (processStatusByte(status)) {
		midiReceiver.handleShortMessage((message << 8) | status);
	} else if (0x80 <= status) { // If no running status available yet, skip this message
		midiReceiver.handleShortMessage(message);
	}
}

// We deal with SysEx messages below 512 bytes long in most cases. Nevertheless, it seems reasonable to support a possibility
// to load bulk dumps using a single message. However, this is known to fail with a real device due to limited input buffer size.
bool MidiStreamParserImpl::checkStreamBufferCapacity(const bool preserveContent) {
	if (streamBufferSize < streamBufferCapacity) return true;
	if (streamBufferCapacity < MAX_STREAM_BUFFER_SIZE) {
		Bit8u *oldStreamBuffer = streamBuffer;
		streamBufferCapacity = MAX_STREAM_BUFFER_SIZE;
		streamBuffer = new Bit8u[streamBufferCapacity];
		if (preserveContent) memcpy(streamBuffer, oldStreamBuffer, streamBufferSize);
		delete[] oldStreamBuffer;
		return true;
	}
	return false;
}

// Checks input byte whether it is a status byte. If not, replaces it with running status when available.
// Returns true if the input byte was changed to running status.
bool MidiStreamParserImpl::processStatusByte(Bit8u &status) {
	if (status < 0x80) {
		// First byte isn't status, try running status
		if (runningStatus < 0x80) {
			// No running status available yet
			midiReporter.printDebug("processStatusByte: No valid running status yet, MIDI message ignored");
			return false;
		}
		status = runningStatus;
		return true;
	} else if (status < 0xF0) {
		// Store current status as running for a Voice message
		runningStatus = status;
	} else if (status < 0xF8) {
		// System Common clears running status
		runningStatus = 0;
	} // System Realtime doesn't affect running status
	return false;
}

// Returns # of bytes parsed
Bit32u MidiStreamParserImpl::parseShortMessageStatus(const Bit8u stream[]) {
	Bit8u status = *stream;
	Bit32u parsedLength = processStatusByte(status) ? 0 : 1;
	if (0x80 <= status) { // If no running status available yet, skip one byte
		*streamBuffer = status;
		++streamBufferSize;
	}
	return parsedLength;
}

// Returns # of bytes parsed
Bit32u MidiStreamParserImpl::parseShortMessageDataBytes(const Bit8u stream[], Bit32u length) {
	const Bit32u shortMessageLength = Synth::getShortMessageLength(*streamBuffer);
	Bit32u parsedLength = 0;

	// Append incoming bytes to streamBuffer
	while ((streamBufferSize < shortMessageLength) && (length-- > 0)) {
		Bit8u dataByte = *(stream++);
		if (dataByte < 0x80) {
			// Add data byte to streamBuffer
			streamBuffer[streamBufferSize++] = dataByte;
		} else if (dataByte < 0xF8) {
			// Discard invalid bytes and start over
			char s[128];
			sprintf(s, "parseShortMessageDataBytes: Invalid short message: status %02x, expected length %i, actual %i -> ignored", *streamBuffer, shortMessageLength, streamBufferSize);
			midiReporter.printDebug(s);
			streamBufferSize = 0; // Clear streamBuffer
			return parsedLength;
		} else {
			// Bypass System Realtime message
			midiReceiver.handleSystemRealtimeMessage(dataByte);
		}
		++parsedLength;
	}
	if (streamBufferSize < shortMessageLength) return parsedLength; // Still lacks data bytes

	// Assemble short message
	Bit32u shortMessage = streamBuffer[0];
	for (Bit32u i = 1; i < shortMessageLength; ++i) {
		shortMessage |= streamBuffer[i] << (i << 3);
	}
	midiReceiver.handleShortMessage(shortMessage);
	streamBufferSize = 0; // Clear streamBuffer
	return parsedLength;
}

// Returns # of bytes parsed
Bit32u MidiStreamParserImpl::parseSysex(const Bit8u stream[], const Bit32u length) {
	// Find SysEx length
	Bit32u sysexLength = 1;
	while (sysexLength < length) {
		Bit8u nextByte = stream[sysexLength++];
		if (0x80 <= nextByte) {
			if (nextByte == 0xF7) {
				// End of SysEx
				midiReceiver.handleSysex(stream, sysexLength);
				return sysexLength;
			}
			if (0xF8 <= nextByte) {
				// The System Realtime message must be processed right after return
				// but the SysEx is actually fragmented and to be reconstructed in streamBuffer
				--sysexLength;
				break;
			}
			// Illegal status byte in SysEx message, aborting
			midiReporter.printDebug("parseSysex: SysEx message lacks end-of-sysex (0xf7), ignored");
			// Continue parsing from that point
			return sysexLength - 1;
		}
	}

	// Store incomplete SysEx message for further processing
	streamBufferSize = sysexLength;
	if (checkStreamBufferCapacity(false)) {
		memcpy(streamBuffer, stream, sysexLength);
	} else {
		// Not enough buffer capacity, don't care about the real buffer content, just mark the first byte
		*streamBuffer = *stream;
		streamBufferSize = streamBufferCapacity;
	}
	return sysexLength;
}

// Returns # of bytes parsed
Bit32u MidiStreamParserImpl::parseSysexFragment(const Bit8u stream[], const Bit32u length) {
	Bit32u parsedLength = 0;
	while (parsedLength < length) {
		Bit8u nextByte = stream[parsedLength++];
		if (nextByte < 0x80) {
			// Add SysEx data byte to streamBuffer
			if (checkStreamBufferCapacity(true)) streamBuffer[streamBufferSize++] = nextByte;
			continue;
		}
		if (0xF8 <= nextByte) {
			// Bypass System Realtime message
			midiReceiver.handleSystemRealtimeMessage(nextByte);
			continue;
		}
		if (nextByte != 0xF7) {
			// Illegal status byte in SysEx message, aborting
			midiReporter.printDebug("parseSysexFragment: SysEx message lacks end-of-sysex (0xf7), ignored");
			// Clear streamBuffer and continue parsing from that point
			streamBufferSize = 0;
			--parsedLength;
			break;
		}
		// End of SysEx
		if (checkStreamBufferCapacity(true)) {
			streamBuffer[streamBufferSize++] = nextByte;
			midiReceiver.handleSysex(streamBuffer, streamBufferSize);
			streamBufferSize = 0; // Clear streamBuffer
			break;
		}
		// Encountered streamBuffer overrun
		midiReporter.printDebug("parseSysexFragment: streamBuffer overrun while receiving SysEx message, ignored. Max allowed size of fragmented SysEx is 32768 bytes.");
		streamBufferSize = 0; // Clear streamBuffer
		break;
	}
	return parsedLength;
}
