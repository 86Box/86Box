/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM Music Feature Card Emulation.
 *
 * Authors: Cacodemon345
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *          win2kgamer
 *
 *          Copyright 2023 Cacodemon345
 *          Copyright 2025-2026 Jasmine Iwanek.
 *          Copyright 2026 win2kgamer
 */

/*
 * SPDX-FileCopyrightText:  2023-2026 The DOSBox Staging Team
 * SPDX-FileCopyrightText:  2017-2020  Loris Chiocca
 * SPDX-FileCopyrightText:  1999-2000  Daisuke Nagano <breeze.nagano@nifty.ne.jp>
 * SPDX-FileCopyrightText:  1997-1999  Jarek Burczynski <s0246@priv4.onet.pl>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * In reverse chronological order:
 *
 *  Copyright (C) 2023-2023  The DOSBox Staging Team
 *    - Applied C++ modernization adjustments.
 *    - Fixed struct naming conflict with DOSBox generic type 'Fraction'
 *
 *  Copyright (C) 2017-2020  Loris Chiocca
 *    - Authored the IBM Music Feature card (IMFC) emulator, as follows:
 *    - Reverse-engineered the IMF's Z80 ROM and tested against hardware.
 *    - Used IDA Pro (licensed) to help structure and anonotate the assembly.
 *    - Ported the assembly to C++ (without IDA Pro's assembly-to-C module).
 *    - Integrated the results into a working DOSBox 0.74.3 patch.
 *    - Used the GPL v2+ Virtual FB-01 project for FB-01 emulation (below).
 *
 *  Copyright (C) 1999-2000  Daisuke Nagano <breeze.nagano@nifty.ne.jp>
 *    - Authored the Virtual FB-01 project (sources inline here).
 *    - Used the YM-2151 emulator from MAME's GPL v2+ sources (below).
 *
 *  Copyright (C) 1997-1999  Jarek Burczynski <s0246@priv4.onet.pl>
 *    - Authored the YM-2151 emulator in MAME, which is used in the Virtual
 *      FB-01 project and dervices sources here.
 *
 *  ---
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 51
 *  Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

// Comments from Loris:
//  - Some IMFC applications don't work because they use the portamento feature,
//    which in turn relies on YM timer and interrupt handling, which isn't
//    currently implemented.
//
//  - Games from Sierra (and several applications) don't use this feature, so
//    from their perspective it's functionally complete.
//
//  - The reverse-engineered assembly (see contrib/ibm_music_feature_asm/) shows
//    there is a way to upload Z80 programs to the IMFC card and execute it! Who
//    knew that IBM added this back-door :)

#include <cassert>
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <memory>

extern "C"
{
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/sound.h>
#include <86box/thread.h>
#include <86box/plat.h>
#include <86box/midi.h>

extern double cpuclock;
}

// Enable to activate IMF_LOG calls
#define IMFC_VERBOSE_LOGGING 0

#define NDEBUG 1

constexpr uint8_t AVAILABLE_MIDI_CHANNELS = 16;
constexpr uint8_t AVAILABLE_INSTRUMENTS   = 8;

#if IMFC_VERBOSE_LOGGING
mutex_t* m_loggerMutex = nullptr;
template <typename... Args>
void IMF_LOG(std::string format, const Args&... args)
{
	thread_wait_mutex(m_loggerMutex);
	printf((format + "\n").c_str(), args...);
	thread_release_mutex(m_loggerMutex);
}
#else
#	define IMF_LOG(...)
#endif

inline uint8_t leftRotate8(uint8_t n)
{
	return (n << 1) | (n >> (8 - 1));
}

enum DataParty : uint8_t {
	DATAPARTY_SYSTEM,
	DATAPARTY_MIDI,
};

static void Intel8253_TimerEvent(const uint32_t val);
static void Intel8253_TimerEvent2(const uint32_t val);
static void CallInterruptHandler();

int midiin_r;
int midiin_w;
int midiin_sysex;
uint8_t midiin_queue[256];

enum CARD_MODE : uint8_t {
	MUSIC_MODE = 0,
	THRU_MODE  = 1,
};

enum CHAIN_MODE : uint8_t {
	CHAIN_MODE_DISABLED = 0,
	CHAIN_MODE_ENABLED  = 1,
};

enum MEMORY_PROTECTION : uint8_t {
	MEMORY_WRITABLE = 0,
	MEMORY_READONLY = 1,
};

enum ERROR_REPORTING : uint8_t {
	ERROR_REPORTING_DISABLED = 0,
	ERROR_REPORTING_ENABLED  = 1,
};
enum HANDSHAKE_MESSAGE : uint8_t {
	ACK_MESSAGE    = 2,
	NAK_MESSAGE    = 3,
	CANCEL_MESSAGE = 4,
};

enum MUSICCARD_ERROR_CODE : uint8_t {
	FIFO_OVERFLOW_ERROR_MUSICCARD_TO_SYSTEM = 0xF0,
	FIFO_OVERFLOW_ERROR_MIDI_TO_MUSICCARD   = 0xF1,
	MIDI_RECEPTION_ERROR                    = 0xF2,
	MIDI_OFFLINE_ERROR                      = 0xF3,
	TIMEOUT_ERROR_MIDI_TO_MUSICCARD         = 0xF4,
	TIMEOUT_ERROR_SYSTEM_TO_MUSICCARD       = 0xF5,
};

enum MidiDataPacketState : uint8_t {
	PACKET_STATE_00 = 0x00,
	PACKET_STATE_01 = 0x01,
	PACKET_STATE_02 = 0x02,
	PACKET_STATE_03 = 0x03,
	PACKET_STATE_04 = 0x04,
	PACKET_STATE_05 = 0x05,
	PACKET_STATE_06 = 0x06,
	PACKET_STATE_07 = 0x07,
	PACKET_STATE_08 = 0x08,
	PACKET_STATE_09 = 0x09,
	PACKET_STATE_0A = 0x0A,
	PACKET_STATE_0B = 0x0B,
	PACKET_STATE_0C = 0x0C,
	PACKET_STATE_0D = 0x0D,
	PACKET_STATE_0E = 0x0E,
	PACKET_STATE_0F = 0x0F,
	PACKET_STATE_10 = 0x10,
	PACKET_STATE_11 = 0x11,
	PACKET_STATE_12 = 0x12,
	PACKET_STATE_13 = 0x13,
	PACKET_STATE_14 = 0x14,
	PACKET_STATE_15 = 0x15,
	PACKET_STATE_16 = 0x16,
	PACKET_STATE_17 = 0x17,
	PACKET_STATE_18 = 0x18,
	PACKET_STATE_19 = 0x19,
	PACKET_STATE_1A = 0x1A,
	PACKET_STATE_1B = 0x1B,
	PACKET_STATE_1C = 0x1C,
	PACKET_STATE_1D = 0x1D,
	PACKET_STATE_1E = 0x1E,
	PACKET_STATE_1F = 0x1F,
	PACKET_STATE_20 = 0x20,
	PACKET_STATE_21 = 0x21,
	PACKET_STATE_22 = 0x22,
	PACKET_STATE_23 = 0x23,
	PACKET_STATE_24 = 0x24,
	PACKET_STATE_25 = 0x25,
	PACKET_STATE_26 = 0x26,
	PACKET_STATE_27 = 0x27,
	PACKET_STATE_28 = 0x28,
	PACKET_STATE_29 = 0x29,
	PACKET_STATE_2A = 0x2A,
	PACKET_STATE_2B = 0x2B,
	PACKET_STATE_2C = 0x2C,
	PACKET_STATE_2D = 0x2D,
	PACKET_STATE_2E = 0x2E,
	PACKET_STATE_2F = 0x2F,
	PACKET_STATE_30 = 0x30,
	PACKET_STATE_31 = 0x31,
	PACKET_STATE_32 = 0x32,
	PACKET_STATE_33 = 0x33,
	PACKET_STATE_34 = 0x34,
	PACKET_STATE_35 = 0x35,
	PACKET_STATE_36 = 0x36,
	PACKET_STATE_37 = 0x37,
	PACKET_STATE_38 = 0x38,
	PACKET_STATE_39 = 0x39,
	PACKET_STATE_3A = 0x3A,
	PACKET_STATE_3B = 0x3B,
	PACKET_STATE_3C = 0x3C,
	PACKET_STATE_3D = 0x3D,
	PACKET_STATE_3E = 0x3E,
	PACKET_STATE_3F = 0x3F,
	PACKET_STATE_40 = 0x40,
};

typedef struct AudioFrame {
	float outl, outr;

	constexpr float& operator[](const size_t i) noexcept {
		assert(i < 2);
		return i == 0 ? outl : outr;
	}
	constexpr const float& operator[](const size_t i) const noexcept {
		assert(i < 2);
		return i == 0 ? outl : outr;
	}
} AudioFrame;

typedef uint16_t io_port_t;
typedef uint32_t io_val_t;

#define PIC_ActivateIRQ(irq) (picint(1 << irq))
#define PIC_FullIndex() (((double)tsc) / cpuclock * 1000.0)

typedef struct pic_timer {
	std::function<void(uint32_t)> callback;
	uint32_t val;
	double interval_miliseconds;

	pc_timer_t timer; /* 86Box timer. */
} pic_timer;

typedef struct mfc_timer {
        std::function<void()> callback;
        uint32_t val;
        double interval_miliseconds;

        pc_timer_t timer; /* 86Box timer. */
} mfc_timer;

static std::vector<std::shared_ptr<pic_timer>> pic_timers;
static std::vector<std::shared_ptr<mfc_timer>> mfc_timers;

void PIC_RemoveEvents(std::function<void(uint32_t)> func)
{
	for (size_t i = 0; i < pic_timers.size(); i++) {
		auto& cur = pic_timers[i];
		if (cur->callback.target<void(*)(uint32_t)>() == func.target<void(*)(uint32_t)>()) {
			timer_disable(&cur->timer);
			pic_timers.erase(pic_timers.begin() + i);
		}
	}
}

void MFC_RemoveEvents(std::function<void()> func)
{
        for (size_t i = 0; i < mfc_timers.size(); i++) {
                auto& cur = mfc_timers[i];
                if (cur->callback.target<void(*)()>() == func.target<void(*)()>()) {
                        timer_disable(&cur->timer);
                        mfc_timers.erase(mfc_timers.begin() + i);
                }
        }
}

void pic_timer_callback(void* priv)
{
	pic_timer* timer = (pic_timer*) priv;

	timer->callback(timer->val);
	timer_on_auto(&timer->timer, timer->interval_miliseconds * 1000.0);
}

void mfc_timer_callback(void* p)
{
        mfc_timer* timer = (mfc_timer*) p;

        timer->callback();
        timer_on_auto(&timer->timer, timer->interval_miliseconds * 1000.0);
}

void PIC_AddEvent(std::function<void(uint32_t)> func, double interval_miliseconds, uint32_t val)
{
	std::shared_ptr<pic_timer> timer(new pic_timer{func, val, interval_miliseconds, { 0 }});
	
	timer_add(&timer->timer, pic_timer_callback, timer.get(), 0);
	timer_on_auto(&timer->timer, timer->interval_miliseconds * 1000.0);
#if 0
    // TODO
	pclog("PIC_AddEvent: func: 0x%016tX @ %f miliseconds\n", func.target<void(*)(uint32_t)>(), interval_miliseconds);
#endif
	pic_timers.push_back(timer);
}

void MFC_AddEvent(std::function<void()> func, double interval_miliseconds, uint32_t val)
{
        std::shared_ptr<mfc_timer> timer(new mfc_timer{func, val, interval_miliseconds, { 0 }});
        timer_add(&timer->timer, mfc_timer_callback, timer.get(), 0);
        timer_on_auto(&timer->timer, timer->interval_miliseconds * 1000.0);
#if 0
    // TODO
        pclog("MFC_AddEvent: func: 0x%016tX @ %f miliseconds\n", func.target<void(*)()>(), interval_miliseconds);
#endif
        mfc_timers.push_back(timer);
}

enum class io_width_t : uint8_t {
	byte = 1, // bytes
	word = 2, // bytes
	dword = 4, // bytes
};

template <typename T>
constexpr uint32_t clamp_to_uint32(const T val)
{
	static_assert(!std::is_same_v<T, uint32_t>,
	              "clamping unnecessary: val is already an uint32_t");

	static_assert(std::is_signed_v<T> || sizeof(T) > sizeof(uint32_t),
	              "clamping unnecessary: val type fits within uint32_t");

	constexpr auto min_val = static_cast<T>(0);
	constexpr auto max_val = static_cast<T>(UINT32_MAX);
	return static_cast<uint32_t>(std::clamp(val, min_val, max_val));
}

// Left-shifts a signed value by a given amount, with overflow detection
template <typename T1, typename T2>
constexpr T1 left_shift_signed(T1 value, T2 amount)
{
	// Ensure we're using a signed type
	static_assert(std::is_signed<T1>::value, "T1 must be signed");

	// Ensure the two types are integers
	static_assert(std::numeric_limits<T1>::is_integer, "T1 must be an integer type");
	static_assert(std::numeric_limits<T2>::is_integer, "T2 must be an integer type");

	// Ensure the amount we're shifting isn't negative
	assert(amount >= 0);

	// Ensure the amount we're shifting doesn't exceed the value's bit-size
	assert(amount <= std::numeric_limits<T1>::digits);

#if defined(NDEBUG)
	// For release builds, simply cast the value to the unsigned-equivalent
	// to ensure performance isn't impacted. Debug and UBSAN builds catch issues.
	typedef typename std::make_unsigned<T1>::type unsigned_T1;
	const auto shifted = static_cast<unsigned_T1>(value) << amount;

#else
	// Ensure we can accommodate the value being shifted
	static_assert(sizeof(T1) <= sizeof(next_uint_t<T1>),
	              "T1 cannot be a larger than its next larger type");

	// cast the value to the next larger unsigned-type before shifting
	const auto shifted = static_cast<next_uint_t<T1>>(value) << amount;

	// Ensure the value is in-bounds of its signed limits
	assert(static_cast<next_int_t<T1>>(shifted) >= (std::numeric_limits<T1>::min)());
	assert(static_cast<next_int_t<T1>>(shifted) <= (std::numeric_limits<T1>::max)());
#endif

	// Cast it back to its original type
	return static_cast<T1>(shifted);
}

struct BufferFlags {
	// bit-7:true=no data / false=has data / bit6:read errors encountered /
	// bit5: overflow error / bit4: offline error
	volatile uint8_t _unused1 : 4;
	volatile uint8_t _hasOfflineError : 1;
	volatile uint8_t _hasOverflowError : 1;
	volatile uint8_t _hasReadError : 1;
	volatile uint8_t _isBufferEmpty : 1;

	inline void setDataAdded()
	{
		_isBufferEmpty = 0;
	}
	inline void setDataCleared()
	{
		_isBufferEmpty = 1;
	}
	inline void setReadErrorFlag()
	{
		_hasReadError = 1;
	}
	inline void setOverflowErrorFlag()
	{
		_hasOverflowError = 1;
	}
	inline void setOfflineErrorFlag()
	{
		_hasOfflineError = 1;
	}
	inline bool isEmpty() const
	{
		return _isBufferEmpty;
	}
	inline bool hasData() const
	{
		return !isEmpty();
	}
	inline bool hasError() const
	{
		return _hasOfflineError || _hasOverflowError || _hasReadError;
	}
	inline bool hasReadError() const
	{
		return _hasReadError;
	}
	inline bool hasOverflowError() const
	{
		return _hasOverflowError;
	}
	inline bool hasOfflineError() const
	{
		return _hasOfflineError;
	}
	inline uint8_t getByteValue() const
	{
		return (_unused1 << 0) | (_hasOfflineError << 4) |
		       (_hasOverflowError << 5) | (_hasReadError << 6) |
		       (_isBufferEmpty << 7);
	}
};

template <typename BufferDataType>
struct CyclicBufferState {
public:
	CyclicBufferState(const CyclicBufferState&) = delete;
	CyclicBufferState& operator=(const CyclicBufferState&) = delete;

private:
	const std::string m_name                    = {};
	mutex_t* m_mutex                             = nullptr;
	volatile bool m_locked                      = false;
	volatile unsigned int lastReadByteIndex     = 0;
	volatile unsigned int indexForNextWriteByte = 0;
	BufferFlags flags                           = {};
	volatile unsigned int m_bufferSize          = 0;
	volatile bool m_debug                       = false;

	// volatile BufferDataType m_buffer[2048]; // maximum that is used in
	// the IMF code
	volatile BufferDataType m_buffer[0x2000] = {}; // FIXME

	void increaseLastReadByteIndex()
	{
		if (m_debug) {
			IMF_LOG("%s - increaseLastReadByteIndex()", m_name.c_str());
		}
		lastReadByteIndex = (lastReadByteIndex + 1) % m_bufferSize;
	}

	void increaseIndexForNextWriteByte()
	{
		if (m_debug) {
			IMF_LOG("%s - increaseIndexForNextWriteByte()",
			        m_name.c_str());
		}
		indexForNextWriteByte = (indexForNextWriteByte + 1) % m_bufferSize;
	}

	inline void setDataAdded()
	{
		flags.setDataAdded();
	}
	inline void setDataCleared()
	{
		flags.setDataCleared();
	}

public:
	CyclicBufferState(const std::string& name, unsigned int bufferSize)
	        : m_name(name),
	          m_mutex(thread_create_mutex()),
	          m_bufferSize(bufferSize)
	{}

	~CyclicBufferState()
	{
		assert(m_mutex);
		thread_close_mutex(m_mutex);
	}

	void setDebug(bool debug)
	{
		m_debug = debug;
	}

	unsigned int getLastReadByteIndex() const
	{
		return lastReadByteIndex;
	}

	unsigned int getIndexForNextWriteByte() const
	{
		return indexForNextWriteByte;
	}

	bool isBufferFull() const
	{
		return lastReadByteIndex ==
		       ((indexForNextWriteByte + 1) % m_bufferSize);
	}

	void pushData(BufferDataType data)
	{
		if (m_debug) {
			IMF_LOG("%s - pushing data 0x%02X into queue @ %i",
			        m_name.c_str(),
			        data,
			        indexForNextWriteByte);
		}
		m_buffer[indexForNextWriteByte] = data;
		increaseIndexForNextWriteByte();
		setDataAdded();
	}

	BufferDataType popData()
	{
		BufferDataType data = m_buffer[lastReadByteIndex];
		if (m_debug) {
			IMF_LOG("%s - popping data 0x%02X from queue @ %i",
			        m_name.c_str(),
			        data,
			        lastReadByteIndex);
		}
		increaseLastReadByteIndex();
		if (getLastReadByteIndex() == getIndexForNextWriteByte()) {
			setDataCleared();
		}
		return data;
	}

	BufferDataType peekData() const
	{
		return m_buffer[lastReadByteIndex];
	}

	void lock()
	{
		thread_wait_mutex(m_mutex);
		assert(!m_locked);
		m_locked = true;
	}
	void unlock()
	{
		assert(m_locked);
		m_locked = false;
		thread_release_mutex(m_mutex);
	}

	void reset()
	{
		// IMF_LOG("%s - reset()", m_name.c_str());
		lock();
		lastReadByteIndex               = 0;
		indexForNextWriteByte           = 0;
		flags._unused1                  = 0;
		flags._hasOfflineError          = 0;
		flags._hasOverflowError         = 0;
		flags._hasReadError             = 0;
		flags._isBufferEmpty            = 1;
		memset((void*)&m_buffer, 0xFF, sizeof(m_buffer));
		unlock();
	}
	inline void setReadErrorFlag()
	{
		flags.setReadErrorFlag();
	}
	inline void setOverflowErrorFlag()
	{
		flags.setOverflowErrorFlag();
	}
	inline void setOfflineErrorFlag()
	{
		flags.setOfflineErrorFlag();
	}
	inline bool isEmpty() const
	{
		return flags.isEmpty();
	}
	inline bool hasData() const
	{
		return flags.hasData();
	}
	inline bool hasError() const
	{
		return flags.hasError();
	}
	inline bool hasReadError() const
	{
		return flags.hasReadError();
	}
	inline bool hasOverflowError() const
	{
		return flags.hasOverflowError();
	}
	inline bool hasOfflineError() const
	{
		return flags.hasOfflineError();
	}
	inline uint8_t getFlagsByteValue() const
	{
		return flags.getByteValue();
	}
};

enum class ReadStatus : uint8_t {
	Success,
	NoData,
	Error,
};
enum class WriteStatus : uint8_t {
	Success,
	Error,
};

struct ReadResult {
	ReadStatus status = {};
	uint8_t data      = 0;
};

enum SystemDataAvailability {
	NoDataAvailable,
	MidiDataAvailable,
	SystemDataAvailable,
};

struct SystemReadResult {
	SystemDataAvailability status = {};
	uint8_t data                  = 0;
};

#pragma pack(push, 1)
struct MidiDataPacket {
	uint8_t data[16]                      = {};
	uint8_t /*MidiDataPacketState*/ state = 0;
	uint8_t reserved[15]                  = {};
};
#pragma pack(pop)
static_assert(sizeof(MidiDataPacket) == 0x20,
              "MidiDataPacket needs to be 0x20 in size!");

static const std::string m_noteToString[12] =
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

#pragma pack(push, 1)
struct Note {
	uint8_t value = 0;

	constexpr Note() = default;
	constexpr Note(const uint8_t v) : value(v) {}

	std::string toString() const
	{
		return m_noteToString[value % 12] + "-" + std::to_string(value / 12);
	}
};
#pragma pack(pop)
static_assert(sizeof(Note) == 1, "Note needs to be 1 in size!");

constexpr bool operator==(const Note& a, const Note& b)
{
	return a.value == b.value;
}

#pragma pack(push, 1)
struct ImfcFraction {
	uint8_t value = 0;

	constexpr ImfcFraction() = default;
	constexpr ImfcFraction(const uint8_t v) : value(v) {}
};
#pragma pack(pop)
static_assert(sizeof(ImfcFraction) == 1, "Fraction needs to be 1 in size!");

constexpr bool operator==(const ImfcFraction& a, const ImfcFraction& b)
{
	return a.value == b.value;
}
static ImfcFraction ZERO_FRACTION(0);

#pragma pack(push, 1)
struct FractionalNote {
	ImfcFraction fraction = {};
	Note note             = {};

	constexpr FractionalNote() = default;
	constexpr FractionalNote(const Note& nn, const ImfcFraction& nf)
	        : fraction(nf),
	          note(nn)
	{}
	constexpr uint16_t GetUint16() const noexcept
	{
		return note.value << 8 | fraction.value;
	}
};
#pragma pack(pop)
static_assert(sizeof(FractionalNote) == 2, "FractionalNote needs to be  in size!");

constexpr std::pair<uint8_t, uint8_t> split_uint16_t(const uint16_t value) noexcept
{
	const auto upper = static_cast<uint8_t>((value >> 8) & 0xFF);
	const auto lower = static_cast<uint8_t>(value & 0xFF);
	return {upper, lower};
}

static constexpr FractionalNote to_fractional_note(const uint16_t value) noexcept
{
	const auto [note, fraction] = split_uint16_t(value);
	return {Note(note), ImfcFraction(fraction)};
}



constexpr bool operator==(const FractionalNote& a, const FractionalNote& b) noexcept
{
	return a.note == b.note && a.fraction == b.fraction;
}
constexpr bool operator!=(const FractionalNote& a, const FractionalNote& b) noexcept
{
	return !(a == b);
}
constexpr FractionalNote operator-(const FractionalNote& a,
                                   const FractionalNote& b) noexcept
{
	// 16-bit wrap-around is expected and normal
	const auto val = (uint16_t)(a.GetUint16() - b.GetUint16());
	return to_fractional_note(val);
}

constexpr FractionalNote operator+(const FractionalNote& a,
                                   const FractionalNote& b) noexcept
{
	// 16-bit wrap-around is expected and normal
	const auto val = static_cast<uint16_t>(a.GetUint16() + b.GetUint16());
	return to_fractional_note(val);
}

#pragma pack(push, 1)
struct KeyVelocity {
	uint8_t value = 0;

	KeyVelocity() : value(0) {}
	explicit KeyVelocity(uint8_t v) : value(v) {}
};
#pragma pack(pop)
static_assert(sizeof(KeyVelocity) == 1, "KeyVelocity needs to be 1 in size!");

#pragma pack(push, 1)
struct Duration {
	uint16_t value = 0;

	Duration() : value(0) {}
	explicit Duration(uint16_t v) : value(v) {}
	inline bool isEmpty() const
	{
		return (value & 0x8000) != 0;
	}
	inline bool isNotEmpty() const
	{
		return !isEmpty();
	}
	inline void setEmpty()
	{
		value |= 0x8000;
	}
};
#pragma pack(pop)
static_assert(sizeof(Duration) == 2, "Duration2 needs to be 1 in size!");

static Duration ZERO_DURATION(0);

#pragma pack(push, 1)
struct PitchbenderValueMSB {
	uint8_t value = 0;

	PitchbenderValueMSB() : value(0) {}
	explicit PitchbenderValueMSB(uint8_t v) : value(v) {}
};
#pragma pack(pop)
static_assert(sizeof(PitchbenderValueMSB) == 1,
              "PitchbenderValueMSB needs to be 1 in size!");

#pragma pack(push, 1)
struct PitchbenderValueLSB {
	uint8_t value = 0;

	PitchbenderValueLSB() : value(0) {}
	explicit PitchbenderValueLSB(uint8_t v) : value(v) {}
};
#pragma pack(pop)
static_assert(sizeof(PitchbenderValueLSB) == 1,
              "PitchbenderValueLSB needs to be 1 in size!");

// clang-format off
/*
Format: <00fedcba>
Bit Affected Message                    Status Byte                         Status Byte (Event List)
--- ----------------------------------- ----------------------------------- --------------------------
a   Note ON/OFF                         [80]~[9F]                           [00]~[2F]
b   After Touch and Pitchbend           [A0]~[AF], [D0]~[DF], [E0]~[EF]     [50]~[5F], [60]~[6F]
c   Control Change                      [B0]~[BF]                           [30]~[3F]
    Program Change                      [C0]~[CD]                           [40]~[4F]
d   System Exclusive                    [F0], [F7]
    System Common                       [F1], [F2], [F3], [F6]              [70]~[7F]
e   System Real-Time                    [F8], [FA]~[FC]
f   ??? only to SP                      N/A                                 N/A

Note: Undefined MIDI status bytes ([F4], [F5], [F9], [FD], [FF]) and the active sensing code ([FE]) not listed
      in the preceding table are blocked. Succeeding data bytes are also blocked.
*/
// clang-format on

#pragma pack(push, 1)
struct MidiFlowPath {
	// This is the path from MIDI IN to the system. This path is enabled
	// when the data received at MIDI IN is to be processed by the system.
	//
	uint8_t MidiIn_To_System = 0;

	// This is the patch from the system to MIDI OUT. This path is used when
	// sending a message from the system to an external MIDI device. If this
	// path is enabled, it is automatically closed when CHAIN mode is set.
	// When CHAIN mode is cancelled, the path returns to its previous state.
	// Even if this path has been closed by entry into CHAIN mode, status
	// reports from the music card will reflect its programmed state.
	//
	uint8_t System_To_MidiOut = 0;

	// This is the path from MIDI IN to the sound processor. This path is set
	// so that the music card can be controlled by an external MIDI devince.
	//
	uint8_t MidiIn_To_SP = 0;

	// This is the route from the system to the sound processor. This path
	// is set so that the music card can be controlled by the processor.
	// Note: If you try to set both the "MIDI IN -> Sound Processor" and
	// "System
	// -> Sound Processor" paths to pass system real-time messages,
	//       only the "MIDI IN -> Sound Processor" path is set. The other
	//       path is ignored with no error report message.
	//
	uint8_t System_To_SP = 0;

	// This is the path from MIDI IN to MIDI OUT. When this path is set, the
	// MIDI OUT terminal functions in essentially the same manner as the
	// MIDI THRU terminal. When the "System -> MIDI OUT" path is
	// concurrently set, the data from the system and the data input to MIDI
	// IN can be merged in MIDI OUT. When this path is set, the program
	// request from a MIDI line is not valid, and a MIDI handshaking message
	// cannot be output to a MIDI line. This path automatically closes when
	// CHAIN mode is set. When CHAIN mode is cancelled, this path returns to
	// its previous state. Even if this path has been closed by entry into
	// CHAIN mode, status reports from the music card reflect its programmed
	// state.
	//
	uint8_t MidiIn_To_MidiOut = 0;
};
#pragma pack(pop)
static_assert(sizeof(MidiFlowPath) == 5, "MidiFlowPath needs to be 1 in size!");

#pragma pack(push, 1)
struct OperatorDefinition {
private:
	uint8_t bytes[8] = {};
	// clang-format off
	//// +00: <0*******> <*>: Total level (TL) 0(Max)~127
	//uint8_t __totalLevel : 7;
	//uint8_t __unused1 : 1;
	//// +01: <a***0000> <a>: Keyboard level scaling type (KLS Bit0) / <*>: Velocity sensitivity to total level (VSTL)
	//uint8_t __unused2 : 4; 
	//uint8_t __velocitySensitivityToTotalLevel : 3;
	//uint8_t __keyboardLevelScalingType : 1; // KLS bit0
	//// +02: <aaaabbbb> <a>: Keyboard level scaling depth (KLSD) / <b>: Addition to "Total level" (undocumented)
	//uint8_t __additionToTotalLevel : 4; // undocumented
	//uint8_t __keyboardLevelScalingDepth : 4;
	//// +03: <abbbcccc> <a>: KLS bit 1 / <b>: Detune (DT) / <c>: Multiple
	//uint8_t __multiple : 4;
	//uint8_t __detune : 3;
	//uint8_t __keyboardLevelScaling : 1;
	//// +04: <aa0*****> <a>: Keyboard rate scaling depth (KRSD) / <*>: Attack rate (AR)
	//uint8_t __attackRate : 5;
	//uint8_t __unused3 : 1;
	//uint8_t __keyboardRateScalingDepth : 2;
	//// +05: <abb*****> <a>: Modulator(0)/Carrier(1) / <b>: Velocity sensitivity to attack rate (VSAR) / <*>: Decay 1 rate (D1R)
	//uint8_t __decay1Rate : 5;
	//uint8_t __velocitySensitivityToAttackRate : 2;
	//uint8_t __modulatorCarrierSelect : 1;
	//// +06: <aa0*****> <a>: Inharmonic / <*>: Decay 2 rate (D2R)
	//uint8_t __decay2Rate : 5;
	//uint8_t __unused4 : 1;
	//uint8_t __inharmonic : 2;
	//// +07: <aaaabbbb> <a>: Sustain level (SL) / <b>: Release rate (RR)
	//uint8_t __releaseRate : 4;
	//uint8_t __sustainLevel : 4;
	// clang-format on

public:
	uint8_t getTotalLevel() const
	{
		return (bytes[0] & 0x7F) >> 0;
	}
	uint8_t getUnused1() const
	{
		return (bytes[0] >> 7) & 0x01;
	}
	uint8_t getUnused2() const
	{
		return (bytes[1] >> 0) & 0x0F;
	}
	uint8_t getVelocitySensitivityToTotalLevel() const
	{
		return (bytes[1] >> 4) & 0x07;
	}
	uint8_t getKeyboardLevelScalingType() const
	{
		return (bytes[1] >> 7) & 0x01;
	}
	uint8_t getAdditionToTotalLevel() const
	{
		return (bytes[2] >> 0) & 0x0F;
	}
	uint8_t getKeyboardLevelScalingDepth() const
	{
		return (bytes[2] >> 4) & 0x0F;
	}
	uint8_t getMultiple() const
	{
		return (bytes[3] >> 0) & 0x0F;
	}
	uint8_t getDetune() const
	{
		return (bytes[3] >> 4) & 0x07;
	}
	uint8_t getKeyboardLevelScaling() const
	{
		return (bytes[3] >> 7) & 0x01;
	}
	uint8_t getAttackRate() const
	{
		return (bytes[4] >> 0) & 0x1F;
	}
	uint8_t getUnused3() const
	{
		return (bytes[4] >> 5) & 0x01;
	}
	uint8_t getKeyboardRateScalingDepth() const
	{
		return (bytes[4] >> 6) & 0x03;
	}
	uint8_t getDecay1Rate() const
	{
		return (bytes[5] >> 0) & 0x1F;
	}
	uint8_t getVelocitySensitivityToAttackRate() const
	{
		return (bytes[5] >> 5) & 0x03;
	}
	uint8_t getModulatorCarrierSelect() const
	{
		return (bytes[5] >> 7) & 0x01;
	}
	uint8_t getDecay2Rate() const
	{
		return (bytes[6] >> 0) & 0x1F;
	}
	uint8_t getUnused4() const
	{
		return (bytes[6] >> 5) & 0x01;
	}
	uint8_t getInharmonic() const
	{
		return (bytes[6] >> 6) & 0x03;
	}
	uint8_t getReleaseRate() const
	{
		return (bytes[7] >> 0) & 0x0F;
	}
	uint8_t getSustainLevel() const
	{
		return (bytes[7] >> 4) & 0x0F;
	}

	void clear()
	{
		bytes[0] = bytes[1] = bytes[2] = bytes[3] = bytes[4] =
		        bytes[5] = bytes[6] = bytes[7] = 0;
	}
	void copyFrom(const OperatorDefinition* other)
	{
		bytes[0] = other->bytes[0];
		bytes[1] = other->bytes[1];
		bytes[2] = other->bytes[2];
		bytes[3] = other->bytes[3];
		bytes[4] = other->bytes[4];
		bytes[5] = other->bytes[5];
		bytes[6] = other->bytes[6];
		bytes[7] = other->bytes[7];
	}

	const uint8_t* SetFromStream(const uint8_t* data)
	{
		auto data_ptr      = data;
		auto get_next_byte = [&data_ptr]() { return *data_ptr++; };

		for (auto& b : bytes) {
			b = get_next_byte();
		}
		return data_ptr;
	}

	inline uint8_t getByte3() const
	{
		return bytes[3];
	}
	inline uint8_t getByte4() const
	{
		return bytes[4];
	}
	inline uint8_t getByte5() const
	{
		return bytes[5];
	}
	inline uint8_t getByte6() const
	{
		return bytes[6];
	}
	inline uint8_t getReleaseRateSustainLevel() const
	{
		return bytes[7];
	}
};
#pragma pack(pop)
static_assert(sizeof(OperatorDefinition) == 8,
              "OperatorDefinition needs to be 8 in size!");

#pragma pack(push, 1)
struct VoiceDefinition {
private:
	// clang-format off
	// +00~+06: Name of instrument
	char name[7] = {};
	// +07: <********> Reserved
	uint8_t reserved1 = 0;
	// +08: <********> LFO Speed
	uint8_t lfoSpeed = 0;
	// +09: <a*******> <a>: LFO load mode 0,1 (Enable) / <*>: Amplitude modulation depth (AMD)
	uint8_t byte9 = 0;
	//uint8_t __amplitudeModulationDepth : 7;
	//uint8_t __lfoLoadMode : 1;
	// +0A: <a*******> <a>: LFO sync mode 0,1 (Sync ON) / <*>: Pitch modulation depth (PMD)
	uint8_t byteA = 0;
	//uint8_t __pitchModulationDepth : 7;
	//uint8_t __lfoSyncMode : 1;
	// +0B: <0abcd000> Operator enable a=operator #4 ... d=operator #1
	uint8_t byteB = 0;
	//uint8_t __unused01 : 3;
	//uint8_t __operatorsEnabled : 4;
	//uint8_t __unused02 : 1;
	// +0C: <11aaabbb> <a>: Feedback level / <b>: Algorithm
	uint8_t byteC = 0;
	//uint8_t __algorithm : 3;
	//uint8_t __feedbackLevel : 3;
	//uint8_t __unused03_allOnes : 2; // these two bits would be the enable Left/Right channel
	// +0D: <0aaa00bb> <a>: Pitch modulation sensitivity (PMS) / <b>: AMS
	uint8_t byteD = 0;
	//uint8_t __amplitudeModulationSensitivity : 2;
	//uint8_t __unused04 : 2;
	//uint8_t __pitchModulationSensitivity : 3;
	//uint8_t __unused05 : 1;
	// +0E: <0**00000> <*>: LFO wave form
	uint8_t byteE = 0;
	//uint8_t __unused06 : 5;
	//uint8_t __lfoWaveForm : 2;
	//uint8_t __unused07 : 1;
	// +0F: <********> <*>: Transpose -128~127
	// clang-format on
	uint8_t transpose               = 0;
	OperatorDefinition operators[4] = {};
	uint8_t reserved2[10]           = {};
	uint8_t field_3A = 0; // <a*******> : <a> Mono/Poly mode <*> portamento
	                      // time
	uint8_t field_3B = 0; // <*aaabbbb> : <b> PitchbenderRange <a>
	                      // PMDController
	uint8_t reserved3[4] = {};

public:
	char getName(uint8_t i) const
	{
		return name[i];
	}
	uint8_t getLfoSpeed() const
	{
		return lfoSpeed;
	}
	void setLfoSpeed(uint8_t val)
	{
		lfoSpeed = val;
	}
	uint8_t getAmplitudeModulationDepth() const
	{
		return (byte9 >> 0) & 0x7F;
	}
	void setAmplitudeModulationDepth(uint8_t val)
	{
		byte9 = (byte9 & 0x80) | (val & 0x7F);
	}
	uint8_t getLfoLoadMode() const
	{
		return (byte9 >> 7) & 0x01;
	}
	void setLfoLoadMode(uint8_t val)
	{
		byte9 = (byte9 & 0x7F) | (val << 7);
	}
	uint8_t getPitchModulationDepth() const
	{
		return (byteA >> 0) & 0x7F;
	}
	void setPitchModulationDepth(uint8_t val)
	{
		byteA = (byteA & 0x7F) | (val << 7);
	}
	uint8_t getLfoSyncMode() const
	{
		return (byteA >> 7) & 0x01;
	}
	void setLfoSyncMode(uint8_t val)
	{
		byteA = (byteA & 0x7F) | (val << 7);
	}
	uint8_t getOperatorsEnabled() const
	{
		const uint8_t val = (byteB >> 3) & 0x0F;
		// val = (((val >> 0) & 1) << 0) | (((val >> 2) & 1) << 1) |
		// (((val >> 1) & 1) << 2) | (((val >> 3) & 1) << 3); // Test if
		// oper 1 and 2 are inverted
		return val;
	}
	uint8_t getAlgorithm() const
	{
		return (byteC >> 0) & 0x07;
	}
	uint8_t getFeedbackLevel() const
	{
		return (byteC >> 3) & 0x07;
	}
	uint8_t getAmplitudeModulationSensitivity() const
	{
		return (byteD >> 0) & 0x03;
	}
	void setAmplitudeModulationSensitivity(uint8_t val)
	{
		byteD = (byteD & 0xFC) | (val & 3);
	}
	uint8_t getPitchModulationSensitivity() const
	{
		return (byteD >> 4) & 0x07;
	}
	void setPitchModulationSensitivity(uint8_t val)
	{
		byteD = (byteD & 0x8F) | ((val & 7) << 4);
	}
	uint8_t getLfoWaveForm() const
	{
		return (byteE >> 5) & 0x03;
	}
	void setLfoWaveForm(uint8_t val)
	{
		byteE = (byteE & 0x9F) | ((val & 3) << 5);
	}
	uint8_t getTranspose() const
	{
		return transpose;
	}
	const OperatorDefinition* getOperator(uint8_t i) const
	{
		return &operators[i];
		// static uint8_t mapping[4] = {0, 2, 1, 3};  // Test if oper 1
		// and 2 are inverted return &operators[mapping[i]];
	}
	uint8_t getPortamentoTime() const
	{
		return (field_3A >> 0) & 0x7F;
	}
	uint8_t getPolyMonoMode() const
	{
		return (field_3A >> 7) & 0x01;
	}
	uint8_t getPitchbenderRange() const
	{
		return (field_3B >> 0) & 0x0F;
	}
	uint8_t getPMDController() const
	{
		return (field_3B >> 4) & 0x07;
	}

	void veryShallowClear()
	{
		name[0] = name[1] = name[2] = name[3] = name[4] = name[5] = name[6] = 0;
		reserved1 = lfoSpeed = byte9 = byteA = byteB = byteC = byteD =
		        byteE = transpose = 0;
	}
	void shallowClear()
	{
		name[0] = name[1] = name[2] = name[3] = name[4] = name[5] = name[6] = 0;
		reserved1 = lfoSpeed = byte9 = byteA = byteB = byteC = byteD =
		        byteE = transpose = 0;
		reserved2[0] = reserved2[1] = reserved2[2] = reserved2[3] =
		        reserved2[4] = reserved2[5] = reserved2[6] =
		                reserved2[7] = reserved2[8] = reserved2[9] = 0;
		field_3A = field_3B = 0;
		reserved3[0] = reserved3[1] = reserved3[2] = reserved3[3] = 0;
	}
	void deepClear()
	{
		shallowClear();
		operators[0].clear();
		operators[1].clear();
		operators[2].clear();
		operators[3].clear();
	}
	void shallowCopyFrom(const VoiceDefinition* other)
	{
		name[0]      = other->name[0];
		name[1]      = other->name[1];
		name[2]      = other->name[2];
		name[3]      = other->name[3];
		name[4]      = other->name[4];
		name[5]      = other->name[5];
		name[6]      = other->name[6];
		reserved1    = other->reserved1;
		lfoSpeed     = other->lfoSpeed;
		byte9        = other->byte9;
		byteA        = other->byteA;
		byteB        = other->byteB;
		byteC        = other->byteC;
		byteD        = other->byteD;
		byteE        = other->byteE;
		transpose    = other->transpose;
		reserved2[0] = other->reserved2[0];
		reserved2[1] = other->reserved2[1];
		reserved2[2] = other->reserved2[2];
		reserved2[3] = other->reserved2[3];
		reserved2[4] = other->reserved2[4];
		reserved2[5] = other->reserved2[5];
		reserved2[6] = other->reserved2[6];
		reserved2[7] = other->reserved2[7];
		reserved2[8] = other->reserved2[8];
		reserved2[9] = other->reserved2[9];
		field_3A     = other->field_3A;
		field_3B     = other->field_3B;
		reserved3[0] = other->reserved3[0];
		reserved3[1] = other->reserved3[1];
		reserved3[2] = other->reserved3[2];
		reserved3[3] = other->reserved3[3];
	}
	void deepCopyFrom(const VoiceDefinition* other)
	{
		shallowCopyFrom(other);
		operators[0].copyFrom(&other->operators[0]);
		operators[1].copyFrom(&other->operators[1]);
		operators[2].copyFrom(&other->operators[2]);
		operators[3].copyFrom(&other->operators[3]);
	}

	const uint8_t* SetFromStream(const uint8_t* data)
	{
		auto data_ptr      = data;
		auto get_next_byte = [&data_ptr]() { return *data_ptr++; };

		for (auto& c : name) {
			c = get_next_byte();
		}
		reserved1 = get_next_byte();

		lfoSpeed  = get_next_byte();
		byte9     = get_next_byte();
		byteA     = get_next_byte();
		byteB     = get_next_byte();
		byteC     = get_next_byte();
		byteD     = get_next_byte();
		byteE     = get_next_byte();
		transpose = get_next_byte();

		for (auto& op : operators) {
			data_ptr = op.SetFromStream(data_ptr);
		}

		for (auto& r : reserved2) {
			r = get_next_byte();
		}

		field_3A = get_next_byte();
		field_3B = get_next_byte();

		for (auto& r : reserved3) {
			r = get_next_byte();
		}
		return data_ptr;
	}

	inline uint8_t getModulationSensitivity() const
	{
		return byteD;
	}

	void dumpToLog()
	{
		// clang-format off
		//IMF_LOG("      name: '%c%c%c%c%c%c%c'", name[0], name[1], name[2], name[3], name[4], name[5], name[6]);
		//IMF_LOG("      operatorsEnabled: %X", getOperatorsEnabled());
		// clang-format on
	}
};
#pragma pack(pop)
static_assert(sizeof(VoiceDefinition) == 0x40,
              "VoiceDefinition needs to be 0x40 in size!");

#pragma pack(push, 1)
struct VoiceDefinitionBank {
private:
	VoiceDefinitionBank& operator=(const VoiceDefinitionBank& other) = delete;

public:
	char name[8]                              = {};
	uint8_t reserved[24]                      = {};
	VoiceDefinition instrumentDefinitions[48] = {};

	void shallowClear()
	{
		for (char& i : name) {
			i = 0;
		}
		for (unsigned char& i : reserved) {
			i = 0;
		}
	}
	void deepClear()
	{
		shallowClear();
		for (auto& instrumentDefinition : instrumentDefinitions) {
			instrumentDefinition.deepClear();
		}
	}
	void shallowCopyFrom(const VoiceDefinitionBank* other)
	{
		for (uint8_t i = 0; i < 8; i++) {
			name[i] = other->name[i];
		}
		for (uint8_t i = 0; i < 24; i++) {
			reserved[i] = other->reserved[i];
		}
	}
	void deepCopyFrom(const VoiceDefinitionBank* other)
	{
		shallowCopyFrom(other);
		for (uint8_t i = 0; i < 48; i++) {
			instrumentDefinitions[i].deepCopyFrom(
			        &other->instrumentDefinitions[i]);
		}
	}
	const uint8_t* SetFromStream(const uint8_t* data)
	{
		auto data_ptr      = data;
		auto get_next_byte = [&data_ptr]() { return *data_ptr++; };

		for (auto& c : name) {
			c = get_next_byte();
		}
		for (auto& r : reserved) {
			r = get_next_byte();
		}
		for (auto& id : instrumentDefinitions) {
			data_ptr = id.SetFromStream(data_ptr);
		}
		return data_ptr;
	}

	void dumpToLog() const
	{
		// clang-format off
		//IMF_LOG("Dump of VoiceDefinitionBank:");
		//IMF_LOG("   name: '%c%c%c%c%c%c%c%c'", name[0], name[1], name[2], name[3], name[4], name[5], name[6], name[7]);
		//for (uint8_t i = 0; i < 48; i++) {
		//	IMF_LOG("   instrumentDefinition[%i]:", i);
		//	instrumentDefinitions[i].dumpToLog();
		//}
		// clang-format on
	}
};
#pragma pack(pop)
static_assert(sizeof(VoiceDefinitionBank) == 0xC20,
              "VoiceDefinitionBank needs to be 0xC20 in size!");

#pragma pack(push, 1)
struct InstrumentConfiguration {
	uint8_t numberOfNotes = 0;
	// Number of notes                  / 0-8

	uint8_t midiChannel = 0;
	// MIDI channel number              / 0-15

	Note noteNumberLimitHigh = {};
	// Note number limit high           / 0-127

	Note noteNumberLimitLow = {};
	// Note number limit low            / 0-127

	uint8_t voiceBankNumber = 0;
	// Voice bank number                / 0-6

	uint8_t voiceNumber = 0;
	// Voice number                     / 0-47

	uint8_t detune = 0;
	// Detune                           / -64-63 (2's complement)

	uint8_t octaveTranspose = 0;
	// Octave transpose                 / 0-4 (2=Center)
private:
	uint8_t outputLevel = 0;
	// Output level                     / 0(mute)-127(max)

public:
	uint8_t pan = 0;
	// Pan                              / 0=L, 64=L+R, 127=R

	uint8_t lfoEnable = 0;
	// LFO enable                       / 0,1(OFF)

	uint8_t portamentoTime = 0;
	// Portamento time (pitch slide)       / 0=OFF, 1(fast)-127(slow)

	uint8_t pitchbenderRange = 0;
	// Pitchbender range                / 0-12 number of half-steps (0=no
	// pitch fluctuation)

	uint8_t polyMonoMode = 0;
	// POLY/MONO mode                   / 0(POLY), 1(MONO) FIXME: Based in
	// the code, this is a single bit

	uint8_t pmdController = 0;
	// PMD controller                   / 0=OFF, 1=Touch, 2=Wheel, 3=Breath,
	// 4=Foot

	uint8_t reserved1 = 0;

	void clear()
	{
		// IMF_LOG("InstrumentConfiguration.clear - voiceBankNumber=%i,
		// voiceNumber=%i, outputLevel=0x%02X", voiceBankNumber,
		// voiceNumber, outputLevel);
		numberOfNotes = midiChannel = 0;
		noteNumberLimitHigh = noteNumberLimitLow = Note(0);
		voiceBankNumber = voiceNumber = detune = octaveTranspose = outputLevel =
		        pan = lfoEnable = portamentoTime = pitchbenderRange =
		                polyMonoMode = pmdController = reserved1 = 0;
	}
	void copyFrom(const InstrumentConfiguration* other)
	{
		numberOfNotes       = other->numberOfNotes;
		midiChannel         = other->midiChannel;
		noteNumberLimitHigh = other->noteNumberLimitHigh;
		noteNumberLimitLow  = other->noteNumberLimitLow;
		voiceBankNumber     = other->voiceBankNumber;
		voiceNumber         = other->voiceNumber;
		detune              = other->detune;
		octaveTranspose     = other->octaveTranspose;
		outputLevel         = other->outputLevel;
		pan                 = other->pan;
		lfoEnable           = other->lfoEnable;
		portamentoTime      = other->portamentoTime;
		pitchbenderRange    = other->pitchbenderRange;
		polyMonoMode        = other->polyMonoMode;
		pmdController       = other->pmdController;
		reserved1           = other->reserved1;
		// IMF_LOG("InstrumentConfiguration.copyFrom -
		// voiceBankNumber=%i, voiceNumber=%i, outputLevel=0x%02X",
		// voiceBankNumber, voiceNumber, outputLevel);
	}

	const uint8_t* SetFromStream(const uint8_t* data)
	{
		auto data_ptr            = data;
		auto get_and_check_range =
		        [&data_ptr]([[maybe_unused]] const uint8_t val_min,
		                    [[maybe_unused]] const uint8_t val_max) {
			        const auto val = *data_ptr++;
                                // PLAYREC sends invalid values when opening some music files. Log this instead of asserting.
                                if ((val < val_min) || (val > val_max))
                                    pclog("IMFC: Value out of range! val = %i, min = %i, max = %i\n", val, val_min, val_max);
			        return val;
		        };
		//                                            min, max
		numberOfNotes             = get_and_check_range(0, 8);
		midiChannel               = get_and_check_range(0, 15);
		noteNumberLimitHigh.value = get_and_check_range(0, 127);
		noteNumberLimitLow.value  = get_and_check_range(0, 127);
		voiceBankNumber           = get_and_check_range(0, 6);
		voiceNumber               = get_and_check_range(0, 47);
		detune                    = get_and_check_range(0, UINT8_MAX);
		octaveTranspose           = get_and_check_range(0, 4);
		outputLevel               = get_and_check_range(0, 127);
		pan                       = get_and_check_range(0, 127);
		lfoEnable                 = get_and_check_range(0, 1);
		portamentoTime            = get_and_check_range(0, 127);
		pitchbenderRange          = get_and_check_range(0, 12);
		polyMonoMode              = get_and_check_range(0, 1);
		pmdController             = get_and_check_range(0, 4);
		reserved1                 = get_and_check_range(0, UINT8_MAX);

		return data_ptr;
	}

	void copySpecialFrom(const InstrumentConfiguration* other)
	{
		numberOfNotes       = other->numberOfNotes;
		midiChannel         = other->midiChannel;
		noteNumberLimitHigh = other->noteNumberLimitHigh;
		noteNumberLimitLow  = other->noteNumberLimitLow;
		voiceBankNumber     = other->voiceBankNumber;
		voiceNumber         = other->voiceNumber;
		detune              = other->detune;
		octaveTranspose     = other->octaveTranspose;
		outputLevel         = other->outputLevel;
		pan                 = other->pan;
		lfoEnable           = other->lfoEnable;
		// IMF_LOG("InstrumentConfiguration.copySpecialFrom -
		// voiceBankNumber=%i, voiceNumber=%i, outputLevel=0x%02X",
		// voiceBankNumber, voiceNumber, outputLevel);
	}

	uint8_t getOutputLevel() const
	{
		return outputLevel;
	}
	void setOutputLevel(uint8_t val)
	{
		outputLevel = val;
		// IMF_LOG("InstrumentConfiguration.setOutputLevel -
		// voiceBankNumber=%i, voiceNumber=%i, outputLevel=0x%02X",
		// voiceBankNumber, voiceNumber, outputLevel);
	}
};
#pragma pack(pop)
static_assert(sizeof(InstrumentConfiguration) == 0x10,
              "InstrumentConfiguration needs to be 0x10 in size!");

#pragma pack(push, 1)
struct ConfigurationData {
private:
	ConfigurationData& operator=(const ConfigurationData& other) = delete;

public:
	char name[8]                     = {};
	uint8_t combineMode              = 0;
	uint8_t lfoSpeed                 = 0;
	uint8_t amplitudeModulationDepth = 0;
	uint8_t pitchModulationDepth     = 0;
	uint8_t lfoWaveForm              = 0;
	uint8_t noteNumberReceptionMode  = 0; // FIXME: This should be an enum
	uint8_t reserved[18]             = {};
	InstrumentConfiguration instrumentConfigurations[8];

	void shallowClear()
	{
		for (char& i : name) {
			i = 0;
		}
		combineMode = lfoSpeed = amplitudeModulationDepth = pitchModulationDepth =
		        lfoWaveForm = noteNumberReceptionMode = 0;
		for (unsigned char& i : reserved) {
			i = 0;
		}
	}
	void deepClear()
	{
		shallowClear();
		for (auto& instrumentConfiguration : instrumentConfigurations) {
			instrumentConfiguration.clear();
		}
	}
	void shallowCopyFrom(const ConfigurationData* other)
	{
		for (uint8_t i = 0; i < 8; i++) {
			name[i] = other->name[i];
		}
		combineMode              = other->combineMode;
		lfoSpeed                 = other->lfoSpeed;
		amplitudeModulationDepth = other->amplitudeModulationDepth;
		pitchModulationDepth     = other->pitchModulationDepth;
		lfoWaveForm              = other->lfoWaveForm;
		noteNumberReceptionMode  = other->noteNumberReceptionMode;
		for (uint8_t i = 0; i < 18; i++) {
			reserved[i] = other->reserved[i];
		}
	}
	void deepCopyFrom(const ConfigurationData* other)
	{
		shallowCopyFrom(other);
		for (uint8_t i = 0; i < 8; i++) {
			instrumentConfigurations[i].copyFrom(
			        &other->instrumentConfigurations[i]);
		}
	}

	// Set just the configuration section without touching
	// instrumentConfigurations.
	const uint8_t* SetConfigurationFromStream(const uint8_t* data)
	{
		auto data_ptr      = data;
		auto get_next_byte = [&data_ptr]() { return *data_ptr++; };

		for (auto& c : name) {
			c = get_next_byte();
		}
		combineMode              = get_next_byte();
		lfoSpeed                 = get_next_byte();
		amplitudeModulationDepth = get_next_byte();
		pitchModulationDepth     = get_next_byte();
		lfoWaveForm              = get_next_byte();
		noteNumberReceptionMode  = get_next_byte();

		for (auto& r : reserved) {
			r = get_next_byte();
		}
		return data_ptr;
	}

	// Set configuration and instrumentConfigurations
	const uint8_t* SetFromStream(const uint8_t* data)
	{
		auto data_ptr = SetConfigurationFromStream(data);

		for (auto& ic : instrumentConfigurations) {
			data_ptr = ic.SetFromStream(data_ptr);
		}
		return data_ptr;
	}
};
#pragma pack(pop)
static_assert(sizeof(ConfigurationData) == 0xA0,
              "ConfigurationData needs to be 0xA0 in size!");

struct YmChannelData; // forward declaration

struct InstrumentParameters {
	InstrumentParameters()
	        : _sustain(0),
	          _portamento(0),
	          _unused0(0),
	          _lfoSyncMode(0)
	{}
	InstrumentConfiguration instrumentConfiguration = {};
	VoiceDefinition voiceDefinition                 = {};
	PitchbenderValueMSB pitchbenderValueMSB         = {};
	PitchbenderValueLSB pitchbenderValueLSB         = {};

	FractionalNote detuneAndPitchbendAsNoteFraction = {};
	// considers detuneAsNoteFraction and
	// instrumentConfiguration.pitchbenderRange * pitchbenderValue

	FractionalNote detuneAsNoteFraction = {};
	// considers instrumentConfiguration.octaveTranspose and
	// instrumentConfiguration.detune

	uint8_t volume = 0;
	// flags: <a*****ps> <a>: LFO sync mode 0,1 (Sync ON) / <p> portamento
	// ON/OFF / <s> sustain ON/OFF
	uint8_t _sustain : 1;
	uint8_t _portamento : 1;
	uint8_t _unused0 : 5;
	uint8_t _lfoSyncMode : 1;
	// next byte
	uint8_t operator1TotalLevel = 0; // 0(Max)~127
	uint8_t operator2TotalLevel = 0; // 0(Max)~127
	uint8_t operator3TotalLevel = 0; // 0(Max)~127
	uint8_t operator4TotalLevel = 0; // 0(Max)~127
	uint8_t unused1             = 0;
	uint8_t channelMask         = 0;
	uint8_t lastUsedChannel     = 0;
	// uint16_t __lastMidiOnOff_Duration_XXX : 15;
	// uint16_t __lastMidiOnOff_Duration_IsEmpty_XXX : 1;
	Duration _lastMidiOnOff_Duration_XX                     = {};
	FractionalNote _lastMidiOnOff_FractionAndNoteNumber_XXX = {};
	// uint16_t __lastMidiOnOff_Duration_YYY : 15;
	// uint16_t __lastMidiOnOff_Duration_IsEmpty_YYY : 1;
	Duration _lastMidiOnOff_Duration_YY                     = {};
	FractionalNote _lastMidiOnOff_FractionAndNoteNumber_YYY = {};
	YmChannelData* ymChannelData                            = nullptr;
	uint8_t overflowToMidiOut = 0; // FIXME: This is a bit flag
	uint8_t unused2[22]       = {};
};

struct YmChannelData {
private:
	YmChannelData& operator=(const YmChannelData& other) = delete;

public:
	FractionalNote originalFractionAndNoteNumber = {};
	// this is the original note that triggered the note ON.

	// uint16_t unused1;
	InstrumentParameters* instrumentParameters = nullptr;

	FractionalNote currentlyPlaying = {};
	// this is the note/fraction that is being play right now

	uint8_t operatorVolumes[4] = {};
	// ym_channel
	uint8_t channelNumber : 3;
	uint8_t operatorsEnabled : 4;
	uint8_t unused2 : 1;
	uint8_t _unused3 : 4;
	uint8_t _hasActiveSostenuto : 1;
	uint8_t _hasActivePortamento : 1;
	uint8_t _flag6 : 1;

	uint8_t _hasActiveNoteON : 1;
	// this is probably an "is playing"-flag

	Duration remainingDuration = {};
	// if set, the number of clock messages (midi code 0xF8) to play this not

	FractionalNote portamentoAdjustment = {};
	// This might be a positive or negative fractionNote!

	FractionalNote portamentoTarget = {};
	// target note/fraction for portamento

	inline uint8_t getChannelNumberAndOperatorEnabled() const
	{
		return channelNumber | (operatorsEnabled << 3);
	}
};

#pragma pack(push, 1)
struct ChannelMaskInfo {
	uint8_t mask         = 0;
	uint8_t nrOfChannels = 0;
};
#pragma pack(pop)
static_assert(sizeof(ChannelMaskInfo) == 2, "ChannelMaskInfo needs to be 2 in size!");

// masks for bit-field members
enum FieldMask : uint8_t {
	LfoSyncMode      = 0b1,
	ChannelNumber    = 0b111,
	OperatorsEnabled = 0b1111,
};

////////////////////////////////////////////////////////
/// Classes
////////////////////////////////////////////////////////

template <typename DataType>
class DataChangedConsumer {
public:
	virtual ~DataChangedConsumer() = default;
	virtual void valueChanged(DataType oldValue, DataType newValue) = 0;
};

template <typename DataType>
class DataProvider {
private:
	std::deque<DataChangedConsumer<DataType>*> m_consumers = {};

protected:
	void notifyConsumers(DataType oldValue, DataType newValue)
	{
		for (auto& consumer : m_consumers) {
			consumer->valueChanged(oldValue, newValue);
		}
	}

public:
	DataProvider()              = default;
	virtual ~DataProvider()     = default;

	virtual DataType getValue() const = 0;
	void notifyOnChange(DataChangedConsumer<DataType>* dataConsumer)
	{
		m_consumers.push_back(dataConsumer);
	}
};

template <typename DataType>
class DataContainer : public DataProvider<DataType> {
private:
	bool m_debug                  = false;
	const std::string m_name      = {};
	std::atomic<DataType> m_value = {};

public:
	DataContainer(const std::string& name, DataType inititalValue)
	        : m_name(name),
	          m_value(inititalValue)
	{}
	std::string getName() const
	{
		return m_name;
	}
	DataType getValue() const override
	{
		return m_value.load();
	}
	void setValue(DataType newValue)
	{
		DataType oldValue = m_value;
		if (oldValue != newValue) {
			m_value = newValue;
			if (m_debug) {
				IMF_LOG("%s changed from %X to %X",
				        this->getName().c_str(),
				        oldValue,
				        newValue);
			}
			this->notifyConsumers(oldValue, newValue);
		}
	}
	void setDebug(bool debug)
	{
		m_debug = debug;
	}
};

template <typename DataType>
class InputPin {
private:
	const std::string m_name = {};

public:
	virtual ~InputPin() = default;
	InputPin() = default;
	explicit InputPin(const std::string& name) : m_name(name) {}
	std::string getName() const
	{
		return m_name;
	}
	virtual DataType getValue() const = 0;
};

template <typename DataType>
class DataDrivenInputPin : public InputPin<DataType> {
private:
	const DataProvider<DataType>* m_dataProvider = nullptr;

public:
	DataDrivenInputPin(const DataDrivenInputPin&) = delete;
	DataDrivenInputPin& operator=(const DataDrivenInputPin&) = delete;

	explicit DataDrivenInputPin(const std::string& name)
	        : InputPin<DataType>(name),
	          m_dataProvider(nullptr)
	{}
	void connect(const DataProvider<DataType>* source)
	{
		m_dataProvider = source;
	}
	DataType getValue() const override
	{
		if (!m_dataProvider) {
			IMF_LOG("Pin %s is not connected (DataDrivenInputPin.getValue)",
			        this->getName().c_str());
			return false;
		}
		return this->m_dataProvider->getValue();
	}
};

template <typename DataType>
class DataPin : public InputPin<DataType> {
private:
	DataContainer<DataType> m_dataContainer;

public:
	DataPin(const std::string& name, DataType initialValue)
	        : InputPin<DataType>(name),
	          m_dataContainer(name, initialValue)
	{}
	DataProvider<DataType>* getDataProvider() const
	{
		return const_cast<DataContainer<DataType>*>(&m_dataContainer);
	}
	DataType getValue() const override
	{
		return m_dataContainer.getValue();
	}
	void setValue(DataType val)
	{
		m_dataContainer.setValue(val);
	}
	void setDebug(bool debug)
	{
		m_dataContainer.setDebug(debug);
	}
};

template <typename DataType>
class InputOutputPin : public InputPin<DataType> {
private:
	DataContainer<DataType>* m_dataContainer = nullptr;

public:
	InputOutputPin(const InputOutputPin&) = delete;
	InputOutputPin& operator=(const InputOutputPin&) = delete;

	explicit InputOutputPin(const std::string& name)
	        : InputPin<DataType>(name),
	          m_dataContainer(nullptr)
	{}
	void connect(DataContainer<DataType>* dataContainer)
	{
		m_dataContainer = dataContainer;
	}
	DataType getValue() const override
	{
		if (!m_dataContainer) {
			IMF_LOG("Pin %s is not connected (InputOutputPin.getValue)",
			        this->getName().c_str());
			return false;
		}
		return this->m_dataContainer->getValue();
	}
	void setValue(DataType val)
	{
		if (!m_dataContainer) {
			IMF_LOG("Pin %s is not connected (InputOutputPin.setValue)",
			        this->getName().c_str());
			return;
		}
		this->m_dataContainer->setValue(val);
	}
};

class InverterGate : public DataChangedConsumer<bool> {
private:
	DataDrivenInputPin<bool> m_input;
	DataPin<bool> m_output;

public:
	explicit InverterGate(const std::string& name)
	        : m_input(name + ".IN"),
	          m_output(name + ".OUT", false)
	{}
	void valueChanged(bool /*oldValue*/, bool /*newValue*/) override
	{
		// inputs have changed: generate the new output
		const bool newOutputValue = !m_input.getValue();
		m_output.setValue(newOutputValue);
	}
	void connectInput(DataProvider<bool>* dataProvider)
	{
		m_input.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	DataProvider<bool>* getOutput() const
	{
		return m_output.getDataProvider();
	}
};

class TriStateBuffer : public DataChangedConsumer<bool> {
private:
	DataDrivenInputPin<bool> m_dataInput;
	DataDrivenInputPin<bool> m_enableInput;
	DataPin<bool> m_output;

public:
	explicit TriStateBuffer(const std::string& name)
	        : m_dataInput(name + ".DATA"),
	          m_enableInput(name + ".ENABLE"),
	          m_output(name + ".OUT", false)
	{}
	void valueChanged(bool /*oldValue*/, bool /*newValue*/) override
	{
		// inputs have changed: generate the new output
		bool newOutputValue = m_output.getValue();
		if (m_enableInput.getValue()) {
			newOutputValue = m_dataInput.getValue();
		}
		m_output.setValue(newOutputValue);
	}
	void connectDataInput(DataProvider<bool>* dataProvider)
	{
		m_dataInput.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	void connectEnableInput(DataProvider<bool>* dataProvider)
	{
		m_enableInput.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	DataProvider<bool>* getOutput() const
	{
		return m_output.getDataProvider();
	}
};

class AndGate : public DataChangedConsumer<bool> {
private:
	DataDrivenInputPin<bool> m_input1;
	DataDrivenInputPin<bool> m_input2;
	DataPin<bool> m_output;

public:
	explicit AndGate(const std::string& name)
	        : m_input1(name + ".IN1"),
	          m_input2(name + ".IN2"),
	          m_output(name + ".OUT", false)
	{}
	void valueChanged(bool /*oldValue*/, bool /*newValue*/) override
	{
		// inputs have changed: generate the new output
		const bool newOutputValue = m_input1.getValue() &&
		                            m_input2.getValue();
		m_output.setValue(newOutputValue);
	}
	void connectInput1(DataProvider<bool>* dataProvider)
	{
		m_input1.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	void connectInput2(DataProvider<bool>* dataProvider)
	{
		m_input2.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	DataProvider<bool>* getOutput() const
	{
		return m_output.getDataProvider();
	}
};

class OrGate : public DataChangedConsumer<bool> {
private:
	DataDrivenInputPin<bool> m_input1;
	DataDrivenInputPin<bool> m_input2;
	DataDrivenInputPin<bool> m_input3;
	DataDrivenInputPin<bool> m_input4;
	DataPin<bool> m_output;

public:
	explicit OrGate(const std::string& name)
	        : m_input1(name + ".IN1"),
	          m_input2(name + ".IN2"),
	          m_input3(name + ".IN3"),
	          m_input4(name + ".IN4"),
	          m_output(name + ".OUT", false)
	{}
	void valueChanged(bool /*oldValue*/, bool /*newValue*/) override
	{
		// inputs have changed: generate the new output
		const bool newOutputValue = m_input1.getValue() ||
		                            m_input2.getValue() ||
		                            m_input3.getValue() ||
		                            m_input4.getValue();
		m_output.setValue(newOutputValue);
	}
	void connectInput1(DataProvider<bool>* dataProvider)
	{
		m_input1.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	void connectInput2(DataProvider<bool>* dataProvider)
	{
		m_input2.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	void connectInput3(DataProvider<bool>* dataProvider)
	{
		m_input3.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	void connectInput4(DataProvider<bool>* dataProvider)
	{
		m_input4.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	DataProvider<bool>* getOutput() const
	{
		return m_output.getDataProvider();
	}
};

class DFlipFlop : public DataChangedConsumer<bool> {
private:
	bool m_clockValue{};
	DataDrivenInputPin<bool> m_dataInput;
	DataDrivenInputPin<bool> m_clockInput;
	DataDrivenInputPin<bool> m_clearInput;
	DataPin<bool> m_output;

public:
	explicit DFlipFlop(const std::string& name)
	        : m_dataInput(name + ".DATA"),
	          m_clockInput(name + ".CLK"),
	          m_clearInput(name + ".CLR"),
	          m_output(name + ".OUT", false)
	{}
	void valueChanged(bool /*oldValue*/, bool /*newValue*/) override
	{
		// inputs have changed: generate the new output
		bool newOutputValue = m_output.getValue();
		if (m_clearInput.getValue()) {
			m_clockValue   = m_clockInput.getValue();
			newOutputValue = false;
		} else if (!m_clockValue && m_clockInput.getValue()) {
			// from the last call, the clock went from LOW to HIGH
			// -> read the data line
			m_clockValue   = m_clockInput.getValue();
			newOutputValue = m_dataInput.getValue();
		} else {
			m_clockValue = m_clockInput.getValue();
		}
		m_output.setValue(newOutputValue);
	}
	void connectDataInput(DataProvider<bool>* dataProvider)
	{
		m_dataInput.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	void connectClockInput(DataProvider<bool>* dataProvider)
	{
		m_clockInput.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	void connectClearInput(DataProvider<bool>* dataProvider)
	{
		m_clearInput.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	DataProvider<bool>* getOutput() const
	{
		return m_output.getDataProvider();
	}
};

class TotalControlRegister {
private:
	DataPin<bool> m_timerAClear;
	DataPin<bool> m_timerBClear;
	DataPin<bool> m_timerAEnable;
	DataPin<bool> m_timerBEnable;
	DataPin<bool> m_dataBit8FromSystem;
	DataPin<bool> m_totalIrqMask;
	DataPin<bool> m_irqBufferEnable;

public:
	explicit TotalControlRegister(const std::string& name)
	        : m_timerAClear(name + ".timerAClear", false),
	          m_timerBClear(name + ".timerBClear", false),
	          m_timerAEnable(name + ".timerAEnable", false),
	          m_timerBEnable(name + ".timerBEnable", false),
	          m_dataBit8FromSystem(name + ".dataBit8FromSystem", false),
	          m_totalIrqMask(name + ".totalIrqMask", false),
	          m_irqBufferEnable(name + ".irqBufferEnable", false)
	{}
	DataProvider<bool>* getTimerAClear() const
	{
		return m_timerAClear.getDataProvider();
	}
	DataProvider<bool>* getTimerBClear() const
	{
		return m_timerBClear.getDataProvider();
	}
	DataProvider<bool>* getTimerAEnable() const
	{
		return m_timerAEnable.getDataProvider();
	}
	DataProvider<bool>* getTimerBEnable() const
	{
		return m_timerBEnable.getDataProvider();
	}
	DataProvider<bool>* getDataBit8FromSystem() const
	{
		return m_dataBit8FromSystem.getDataProvider();
	}
	DataProvider<bool>* getTotalIrqMask() const
	{
		return m_totalIrqMask.getDataProvider();
	}
	DataProvider<bool>* getIrqBufferEnable() const
	{
		return m_irqBufferEnable.getDataProvider();
	}
	uint8_t readPort()
	{
		const uint8_t value = (m_timerAClear.getValue() ? 0x01 : 0x00) |
		                      (m_timerBClear.getValue() ? 0x02 : 0x00) |
		                      (m_timerAEnable.getValue() ? 0x04 : 0x00) |
		                      (m_timerBEnable.getValue() ? 0x08 : 0x00) |
		                      (m_dataBit8FromSystem.getValue() ? 0x10 : 0x00) |
		                      (m_totalIrqMask.getValue() ? 0x40 : 0x00) |
		                      (m_irqBufferEnable.getValue() ? 0x80 : 0x00);
		// IMF_LOG("IMFC.TCR:readPort -> 0x%X", value);
		return value;
	}
	void writePort(uint8_t val)
	{
		m_timerAClear.setValue((val & 0x01) != 0);
		m_timerBClear.setValue((val & 0x02) != 0);
		m_timerAEnable.setValue((val & 0x04) != 0);
		m_timerBEnable.setValue((val & 0x08) != 0);
		m_dataBit8FromSystem.setValue((val & 0x10) != 0);
		// IMF_LOG("TCR: new value for m_dataBit8FromSystem = %i", val &
		// 0x10 ? 1 : 0);
		m_totalIrqMask.setValue((val & 0x40) != 0);
		m_irqBufferEnable.setValue((val & 0x80) != 0);

		// Based on the IMFC documentation, the timerAClear and
		// timerBClear:
		//  The power-on default value is 0. This bit clears interrupts
		//  from timer A/B. It is usually set to 1. When clearing an
		//  interrupt from timer A/B, this bit is set to 0. It is
		//  immediately reset to 1.
		if ((val & 0x01) == 0) {
			// IMF_LOG("Resetting timerAClear to 1!");
			m_timerAClear.setValue(true);
		}
		if ((val & 0x02) == 0) {
			// IMF_LOG("Resetting timerBClear to 1!");
			m_timerBClear.setValue(true);
		}
	}
};

enum CounterMode : uint8_t {
	COUNTERMODE_INVALID,
	COUNTERMODE_MODE2,
	COUNTERMODE_MODE3,
	COUNTERMODE_MODE4
};

enum CounterReadSource : uint8_t {
	COUNTERREADSOURCE_LIVE_1,
	COUNTERREADSOURCE_LIVE_2,
	COUNTERREADSOURCE_LATCH_1,
	COUNTERREADSOURCE_LATCH_2,
};

enum CounterWriteTarget : uint8_t {
	COUNTERWRITETARGET_BYTE1,
	COUNTERWRITETARGET_BYTE2,
};

struct CounterData {
	const std::string m_name = {};
	CounterMode m_counterMode{COUNTERMODE_INVALID};
	unsigned int m_counter               = 0;
	unsigned int m_runningCounter        = 0;
	unsigned int m_latchedCounter        = 0;
	uint8_t m_tmpWrite                   = 0;
	CounterReadSource m_nextReadSource   = COUNTERREADSOURCE_LIVE_1;
	CounterWriteTarget m_nextWriteTarget = COUNTERWRITETARGET_BYTE1;
	explicit CounterData(const std::string& name) : m_name(name) {}
	void writeCounterByte(uint8_t val)
	{
		switch (m_nextWriteTarget) {
		case COUNTERWRITETARGET_BYTE1:
			m_tmpWrite        = val;
			m_nextWriteTarget = COUNTERWRITETARGET_BYTE2;
			break;
		case COUNTERWRITETARGET_BYTE2:
			m_counter = m_tmpWrite << 8 | val;
			IMF_LOG("%s has been assigned a new COUNTER of value %i",
			        m_name.c_str(),
			        m_counter);
			m_nextWriteTarget = COUNTERWRITETARGET_BYTE1;
			m_runningCounter  = m_counter;
			break;
		}
	}
	void writeCounterByteSwapped(uint8_t val)
	{
		switch (m_nextWriteTarget) {
		case COUNTERWRITETARGET_BYTE1:
			m_tmpWrite        = val;
			m_nextWriteTarget = COUNTERWRITETARGET_BYTE2;
			break;
			case COUNTERWRITETARGET_BYTE2:
			m_counter = val << 8 | m_tmpWrite;
			//pclog("%s has been assigned a new COUNTER of value %i\n",
			//	m_name.c_str(),
			//	m_counter);
			m_nextWriteTarget = COUNTERWRITETARGET_BYTE1;
			m_runningCounter  = m_counter;
			break;
		}
	}
	uint8_t readCounterByte()
	{
		switch (m_nextReadSource) {
		case COUNTERREADSOURCE_LIVE_1:
			m_nextReadSource = COUNTERREADSOURCE_LIVE_2;
			return 0; // TODO
		case COUNTERREADSOURCE_LIVE_2:
			m_nextReadSource = COUNTERREADSOURCE_LIVE_1;
			return 0; // TODO
		case COUNTERREADSOURCE_LATCH_1:
			m_nextReadSource = COUNTERREADSOURCE_LATCH_2;
			return m_latchedCounter & 0xFF;
		case COUNTERREADSOURCE_LATCH_2:
		default:
			m_nextReadSource = COUNTERREADSOURCE_LIVE_1;
			return (m_latchedCounter >> 8) & 0xFF;
		}
	}
};

class Intel8253 {
private:
	const std::string m_name = {};

	bool m_debug = false;

	DataPin<bool> m_timerA{"timerAOut", true};
	DataPin<bool> m_timerB{"timerBOut", true};

	CounterData m_counter0{"timer.counter0"};
	CounterData m_counter1{"timer.counter1"};
	CounterData m_counter2{"timer.counter2"};

	static void registerNextEvent()
	{
		// PIC_AddEvent takes milliseconds as argument
		// the counter0 has a resolution of 2 microseconds
		PIC_AddEvent(Intel8253_TimerEvent, 0.002, 0); // FIXME
		// counter1 has a resolution of 0.5 microseconds
		PIC_AddEvent(Intel8253_TimerEvent2, 0.0005, 0); // FIXME
	}

public:
	~Intel8253()
	{
		PIC_RemoveEvents(Intel8253_TimerEvent);
        PIC_RemoveEvents(Intel8253_TimerEvent2);
	}

	explicit Intel8253(const std::string& name) : m_name(name)
	{
		Intel8253::registerNextEvent(); // FIXME
	}
	DataProvider<bool>* getTimerA() const
	{
		return m_timerA.getDataProvider();
	}
	DataProvider<bool>* getTimerB() const
	{
		return m_timerB.getDataProvider();
	}

	void setDebug(bool debug)
	{
		m_debug = debug;
	}

	uint8_t readPortCNTR0()
	{
		const uint8_t val = m_counter0.readCounterByte();
		IMF_LOG("readPortCNTR0 -> 0x%X", val);
		return val;
	}

	void writePortCNTR0(uint8_t val)
	{
		IMF_LOG("writePortCNTR0 / value=0x%X", val);
		m_counter0.writeCounterByteSwapped(val);
	}

	uint8_t readPortCNTR1()
	{
		const uint8_t val = m_counter1.readCounterByte();
		IMF_LOG("readPortCNTR1 -> 0x%X", val);
		return val;
	}

	void writePortCNTR1(uint8_t val)
	{
		IMF_LOG("writePortCNTR1 / value=0x%X", val);
		m_counter1.writeCounterByteSwapped(val);
	}

	uint8_t readPortCNTR2()
	{
		const uint8_t val = m_counter2.readCounterByte();
		IMF_LOG("readPortCNTR2 -> 0x%X", val);
		return val;
	}

	void writePortCNTR2(uint8_t val)
	{
		IMF_LOG("writePortCNTR2 / value=0x%X", val);
		m_counter2.writeCounterByteSwapped(val);
	}

	static uint8_t readPortTCWR()
	{
		IMF_LOG("readPortTCWR -> 0x%X", 0);
		// I don't think we should be able to read this port
		return 0;
	}
	void writePortTCWR(uint8_t val)
	{
		IMF_LOG("writePortTCWR / value=0x%X", val);
		// clang-format off
		// The two "normal" commands are:
		//  AAFFMMMB: AA is either "00" for "Set mode of counter 0", "01" for "Set mode of counter 1" or "10" for "Set mode of counter 2"
		//            FF is select the format that will be used for subsequent read/write access to the counter register. This is commonly set to a mode where accesses alternate between the least-significant and most-significant bytes.
		//            MMM select the mode that the counter will operate in
		//            B selects whether the counter will operate in binary or BCD
		//  CC00****  Latch counter value. Next read of counter will read snapshot of value. CC is the counter number 00, 01 or 10
		// more information: https://en.wikipedia.org/wiki/Intel_8253
		// clang-format on

		if (val == 0x34) {
			// Sets counter 0 for mode 2 and the 16-bit binary
			// counter 0x34 = 00(Set mode of counter 0) 11(low-order
			// to high-order bits) 010(Mode2) 0(binary)
			m_counter0.m_counterMode = COUNTERMODE_MODE2;
			IMF_LOG("counter0 is now set to MODE2", "");
		} else if (val == 0x38) {
			// Sets counter 0 for mode 4 and the 16-bit binary
			// counter 0x38 = 00(Set mode of counter 0) 11(low-order
			// to high-order bits) 100 (Mode4) 0(binary)
			m_counter0.m_counterMode = COUNTERMODE_MODE4;
		} else if (val == 0x74) {
			// Sets counter 1 for mode 2 and the 16-bit binary
			// counter 0x74 = 01(Set mode of counter 1) 11(low-order
			// to high-order bits) 010(Mode2) 0(binary)
			m_counter1.m_counterMode = COUNTERMODE_MODE2;
			IMF_LOG("counter1 is now set to MODE2", "");
		} else if (val == 0x78) {
			// Sets counter 1 for mode 4 and the 16-bit binary
			// counter 0x78 = 01(Set mode of counter 1) 11(low-order
			// to high-order bits) 100(Mode4) 0(binary)
			m_counter1.m_counterMode = COUNTERMODE_MODE4;
			IMF_LOG("counter1 is now set to MODE4", "");
		} else if (val == 0xB6) {
			// Sets counter 2 for mode 3 and the 16-bit binary
			// counter 0xB6 = 10(Set mode of counter 2) 11(low-order
			// to high-order bits) 011(Mode3) 0(binary)
			m_counter2.m_counterMode = COUNTERMODE_MODE3;
			IMF_LOG("counter2 is now set to MODE3", "");
		} else if (val == 0xB8) {
			// Sets counter 2 for mode 4 and the 16-bit binary
			// counter 0xB8 = 10(Set mode of counter 2) 11(low-order
			// to high-order bits) 100(Mode4) 0(binary)
			m_counter2.m_counterMode = COUNTERMODE_MODE4;
			IMF_LOG("counter2 is now set to MODE4", "");
		} else if (val == 0x00) {
			// Latches the contents of counter 0 to the register
			m_counter0.m_latchedCounter = m_counter0.m_runningCounter; // FIXME
			m_counter0.m_nextReadSource = COUNTERREADSOURCE_LATCH_1;
		} else if (val == 0x40) {
			// Latches the contents of counter 1 to the register
			m_counter1.m_latchedCounter = m_counter1.m_runningCounter; // FIXME
			m_counter1.m_nextReadSource = COUNTERREADSOURCE_LATCH_1;
		} else if (val == 0x80) {
			// Latches the contents of counter 2 to the register
			m_counter2.m_latchedCounter = m_counter2.m_runningCounter; // FIXME
			m_counter2.m_nextReadSource = COUNTERREADSOURCE_LATCH_1;
		} else {
			IMF_LOG("writePortTCWR: Unsupported command 0x%X", val);
		}
	}

	void timerEvent(uint32_t /*val*/)
	{
		if (m_counter0.m_counter != 0U) {
			// counter was initialized with a value
			if (m_counter0.m_runningCounter > 1) {
				// normal case -> decrease the counter
				m_counter0.m_runningCounter--;
				// IMF_LOG("m_counter0.m_runningCounter -> %i",
				// m_counter0.m_runningCounter);
			} else if (m_counter0.m_runningCounter == 1) {
				// if the counter is 1, then the timer out goes LOW
				if (m_debug) {
					IMF_LOG("%s - m_runningCounter == 1 -> m_timerA = LOW",
					        m_name.c_str());
				}
				m_timerA.setValue(false);
				m_counter0.m_runningCounter--;
				// IMF_LOG("m_counter0.m_runningCounter -> %i",
				// m_counter0.m_runningCounter);
				// IMF_LOG("m_timerA.OUT is going LOW");
			} else {
				// if the counter is 0, then the timer out does
				// HIGH again
				if (m_debug) {
					IMF_LOG("%s - m_runningCounter == 0 -> m_timerA = HIGH",
					        m_name.c_str());
				}
				m_timerA.setValue(true);
				if (m_counter0.m_counterMode != COUNTERMODE_MODE4)
					m_counter0.m_runningCounter = m_counter0.m_counter;
				// IMF_LOG("m_counter0.m_runningCounter -> %i",
				// m_counter0.m_runningCounter);
				// IMF_LOG("m_timerA.OUT is going HIGH");
			}
		}
	}
        void timerEvent2(uint32_t /*val*/)
        {
                if (m_counter2.m_counter != 0U) {
                        // counter was initialized with a value
                        if (m_counter2.m_runningCounter > 1) {
                                // normal case -> decrease the counter
                                m_counter2.m_runningCounter--;
                                // IMF_LOG("m_counter2.m_runningCounter -> %i",
                                // m_counter2.m_runningCounter);
                        } else if (m_counter2.m_runningCounter == 1) {
                                m_counter2.m_runningCounter--;
                                // IMF_LOG("m_counter2.m_runningCounter -> %i",
                                // m_counter2.m_runningCounter);
                        } else {
                                // End of Counter 2 decrements Counter 1
                                m_counter1.m_runningCounter--;
				if (m_counter2.m_counterMode != COUNTERMODE_MODE4)
	                                m_counter2.m_runningCounter = m_counter2.m_counter;
                                // IMF_LOG("m_counter2.m_runningCounter -> %i",
                                // m_counter2.m_runningCounter);
                        }
                }
                if (m_counter1.m_counter != 0U) {
                        // counter was initialized with a value
                        if (m_counter1.m_runningCounter > 1) {
                                // Do nothing: Counter 1 is decremented by Counter 2
                                // IMF_LOG("m_counter1.m_runningCounter -> %i",
                                // m_counter1.m_runningCounter);
                        } else if (m_counter1.m_runningCounter == 1) {
                                // if the counter is 1, then the timer out goes LOW
                                if (m_debug) {
                                        IMF_LOG("%s - m_runningCounter == 1 -> m_timerB = LOW",
                                                m_name.c_str());
                                }
                                m_timerB.setValue(false);
                                // IMF_LOG("m_counter1.m_runningCounter -> %i",
                                // m_counter1.m_runningCounter);
                                // IMF_LOG("m_timerB.OUT is going LOW");
                        } else {
                                // if the counter is 0, then the timer out does
                                // HIGH again
                                if (m_debug) {
                                        IMF_LOG("%s - m_runningCounter == 0 -> m_timerB = HIGH",
                                                m_name.c_str());
                                }
                                m_timerB.setValue(true);
				if (m_counter1.m_counterMode != COUNTERMODE_MODE4)
	                                m_counter1.m_runningCounter = m_counter1.m_counter;
                                // IMF_LOG("m_counter1.m_runningCounter -> %i",
                                // m_counter1.m_runningCounter);
                                // IMF_LOG("m_timerB.OUT is going HIGH");
                        }
                }

        }
};

class TotalStatusRegister : public DataChangedConsumer<bool> {
private:
	DataDrivenInputPin<bool> m_timerAStatus;
	DataDrivenInputPin<bool> m_timerBStatus;
	DataDrivenInputPin<bool> m_totalCardStatus;

public:
	explicit TotalStatusRegister(const std::string& name)
	        : m_timerAStatus(name + ".TAS"),
	          m_timerBStatus(name + ".TBS"),
	          m_totalCardStatus(name + ".TCS")
	{}

	void connectTimerAStatus(DataProvider<bool>* dataProvider)
	{
		m_timerAStatus.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	void connectTimerBStatus(DataProvider<bool>* dataProvider)
	{
		m_timerBStatus.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	void connectTotalCardStatus(DataProvider<bool>* dataProvider)
	{
		m_totalCardStatus.connect(dataProvider);
		dataProvider->notifyOnChange(this);
	}

	void valueChanged([[maybe_unused]] bool oldValue,
	                  [[maybe_unused]] bool newValue) override
	{}

	uint8_t readPort()
	{
		const uint8_t value = (m_timerAStatus.getValue() ? 0x01 : 0x00) |
		                      (m_timerBStatus.getValue() ? 0x02 : 0x00) |
		                      (m_totalCardStatus.getValue() ? 0x80 : 0x00);
		// IMF_LOG("IMFC.TSR:readPort -> 0x%X", value);
		return value;
	}
	void writePort([[maybe_unused]] uint8_t val)
	{
		// IMF_LOG("IMFC.TSR:writePort / value=0x%X", val);
		/// I really don't think that we are allowed to write to this
		/// port based on the schematics
	}
};

// This is a Parallel Interface Unit (PIU)
class PD71055 : public DataChangedConsumer<bool> {
public:
	enum PortInOut : uint8_t {
		OUTPUT,
		INPUT,
	};

	enum GroupMode : uint8_t {
		MODE0,
		MODE1,
		MODE2,
	};

private:
	const std::string m_name = {};
	// group 0 control
	GroupMode m_group0_mode            = {};
	PortInOut m_group0_port0InOut      = {};
	PortInOut m_group0_port2UpperInOut = {};
	// group 1 control
	GroupMode m_group1_mode            = {};
	PortInOut m_group1_port1InOut      = {};
	PortInOut m_group1_port2LowerInOut = {};
	// data ports
	InputOutputPin<uint8_t> m_port0;
	InputOutputPin<uint8_t> m_port1;
	std::deque<InputOutputPin<bool>> m_port2 = {};
	// internal signals
	bool m_group0_readInterruptEnable{false};
	// This signal is internal to the PD71055. They are not output to port 2
	// pins and they cannot be read by the host.

	bool m_group0_writeInterruptEnable{false};
	// This signal is internal to the PD71055. They are not output to port 2
	// pins and they cannot be read by the host.

	bool m_group1_readInterruptEnable{false};
	// This signal is internal to the PD71055. They are not output to port 2
	// pins and they cannot be read by the host.

	bool m_group1_writeInterruptEnable{false};
	// This signal is internal to the PD71055. They are not output to port 2
	// pins and they cannot be read by the host.

	uint8_t readPort0() const
	{
		return m_port0.getValue();
	}
	void writePort0(uint8_t val)
	{
		m_port0.setValue(val);
	}
	uint8_t readPort1() const
	{
		return m_port1.getValue();
	}
	void writePort1(uint8_t val)
	{
		m_port1.setValue(val);
	}
	uint8_t readPort2()
	{
		uint8_t val = 0;
		for (uint8_t i = 0; i < 8; i++) {
			if (m_port2[i].getValue()) {
				val |= 1 << i;
			}
		}
		return val;
	}
	void writePort2(uint8_t val)
	{
		for (uint8_t i = 0; i < 8; i++) {
			m_port2[i].setValue((val & (1 << i)) != 0);
		}
	}

	void clearAllData()
	{
		writePort0(0);
		writePort1(0);
		writePort2(0);
		m_group0_readInterruptEnable  = false;
		m_group0_writeInterruptEnable = false;
		m_group1_readInterruptEnable  = false;
		m_group1_writeInterruptEnable = false;
	}

	// clang-format off
	/*
		This is my simplified status/control protocol. It is quite different from the original implementation in the PD71055, but
		for the IMF not more is needed. So here goes: (note: this is just for Mode1!)
			Two data lines are needed: One from an output to an input and vice-versa
						State	Output-Side		Input-Side
			LineA:		S0		0				0			: Initial state where no data needs to be sent
						S1		1				0			: The output has been written to the port and is awaiting the input side to acknowledge the data
						S2		1				1			: The input side has read the data from the port
						S3		0				1			: The output side has received the acknowledge and is waiting for the input-side to lower its line
						S0		0				0			: Ready for transmission again
			An output interrupt line is HIGH in state S0 and LOW in all the other states
			An input interrupt line is HIGH in state S1, and LOW in all the other states
	*/
	// clang-format on

	void setGroup0DataAvailability(bool available)
	{
		if (m_group0_mode != MODE1) {
			IMF_LOG("ERROR: You can only use setGroup0DataAvailability() in MODE1",
			        "");
			return;
		}
		if (m_group0_port0InOut == INPUT) {
			IMF_LOG("ERROR: Cannot call setGroup0DataAvailability() in INPUT mode",
			        "");
			return;
		}
		// IMF_LOG("%s: setGroup0DataAvailability(%s)", m_name.c_str(),
		// available ? "true" : "false");
		m_port2[7].setValue(available);
	}
	bool getGroup0DataAvailability()
	{
		if (m_group0_mode != MODE1) {
			IMF_LOG("ERROR: You can only use getGroup0DataAvailability() in MODE1",
			        "");
			return false;
		}
		return m_port2[m_group0_port0InOut == OUTPUT ? 7 : 5].getValue();
	}
	void setGroup0DataAcknowledgement(bool acknowledge)
	{
		if (m_group0_mode != MODE1) {
			IMF_LOG("ERROR: You can only use setGroup0DataAcknowledgement() in MODE1",
			        "");
			return;
		}
		if (m_group0_port0InOut == OUTPUT) {
			IMF_LOG("ERROR: Cannot call setGroup0DataAcknowledgement() in OUTPUT mode",
			        "");
			return;
		}
		// IMF_LOG("%s: setGroup0DataAcknowledgement(%s)",
		// m_name.c_str(), acknowledge ? "true" : "false");
		m_port2[4].setValue(acknowledge);
	}
	bool getGroup0DataAcknowledgement()
	{
		if (m_group0_mode != MODE1) {
			IMF_LOG("ERROR: You can only use getGroup0DataAcknowledgement() in MODE1",
			        "");
			return false;
		}
		return m_port2[m_group0_port0InOut == INPUT ? 4 : 6].getValue();
	}
	void setGroup1DataAvailability(bool available)
	{
		if (m_group1_mode != MODE1) {
			IMF_LOG("ERROR: You can only use setGroup1DataAvailability() in MODE1",
			        "");
			return;
		}
		if (m_group1_port1InOut == INPUT) {
			IMF_LOG("ERROR: Cannot call setGroup1DataAvailability() in INPUT mode",
			        "");
			return;
		}
		// IMF_LOG("%s: setGroup1DataAvailability(%s)", m_name.c_str(),
		// available ? "true" : "false");
		m_port2[2].setValue(available);
	}
	bool getGroup1DataAvailability()
	{
		if (m_group1_mode != MODE1) {
			IMF_LOG("ERROR: You can only use getGroup1DataAvailability() in MODE1",
			        "");
			return false;
		}
		return m_port2[2].getValue();
	}
	void setGroup1DataAcknowledgement(bool acknowledge)
	{
		if (m_group1_mode != MODE1) {
			IMF_LOG("ERROR: You can only use setGroup1DataAcknowledgement() in MODE1",
			        "");
			return;
		}
		if (m_group1_port1InOut == OUTPUT) {
			IMF_LOG("ERROR: Cannot call setGroup1DataAcknowledgement() in OUTPUT mode",
			        "");
			return;
		}
		// IMF_LOG("%s: setGroup1DataAcknowledgement(%s)",
		// m_name.c_str(), acknowledge ? "true" : "false");
		m_port2[1].setValue(acknowledge);
	}
	bool getGroup1DataAcknowledgement()
	{
		if (m_group1_mode != MODE1) {
			IMF_LOG("ERROR: You can only use getGroup1DataAcknowledgement() in MODE1",
			        "");
			return false;
		}
		return m_port2[1].getValue();
	}

public:
	virtual ~PD71055() = default;
	explicit PD71055(const std::string& name)
	        : m_name(name),
	          m_port0(name + ".p0"),
	          m_port1(name + ".p1")
	{
		assert(m_port2.empty());
		m_port2.emplace_back(name + ".P2-0");
		m_port2.emplace_back(name + ".P2-1");
		m_port2.emplace_back(name + ".P2-2");
		m_port2.emplace_back(name + ".P2-3");
		m_port2.emplace_back(name + ".P2-4");
		m_port2.emplace_back(name + ".P2-5");
		m_port2.emplace_back(name + ".P2-6");
		m_port2.emplace_back(name + ".P2-7");
	}
	void connectPort0(DataContainer<uint8_t>* dataContainer)
	{
		m_port0.connect(dataContainer);
	}
	void connectPort1(DataContainer<uint8_t>* dataContainer)
	{
		m_port1.connect(dataContainer);
	}
	void connectPort2(unsigned int bitNr, DataContainer<bool>* dataContainer)
	{
		m_port2[bitNr].connect(dataContainer);
		if (bitNr < 8 && bitNr != 3 && bitNr != 0) {
			dataContainer->notifyOnChange(this);
		}
	}

	virtual void updateInterruptLines()
	{
		if (m_group0_mode == MODE1) {
			bool int0 = false;
			if (m_group0_readInterruptEnable) {
				int0 |= getGroup0DataAvailability() &&
				        !getGroup0DataAcknowledgement();
			}
			if (m_group0_writeInterruptEnable) {
				int0 |= !getGroup0DataAvailability() &&
				        !getGroup0DataAcknowledgement();
			}
			m_port2[3].setValue(int0);
		}
		if (m_group1_mode == MODE1) {
			bool int1 = false;
			if (m_group1_readInterruptEnable) {
				int1 |= getGroup1DataAvailability() &&
				        !getGroup1DataAcknowledgement();
			}
			if (m_group1_writeInterruptEnable) {
				int1 |= !getGroup1DataAvailability() &&
				        !getGroup1DataAcknowledgement();
			}
			m_port2[0].setValue(int1);
		}
	}
	void valueChanged(bool /*oldValue*/, bool /*newValue*/) override
	{
		// we might need to change states
		if (m_group0_mode == MODE1 && m_group0_port0InOut == OUTPUT &&
		    getGroup0DataAvailability() && getGroup0DataAcknowledgement()) {
			// IMF_LOG("%s (valueChanged):
			// setGroup0DataAvailability(false)", m_name.c_str());
			setGroup0DataAvailability(false);
		}
		if (m_group0_mode == MODE1 && m_group0_port0InOut == INPUT &&
		    !getGroup0DataAvailability() && getGroup0DataAcknowledgement()) {
			// IMF_LOG("%s (valueChanged):
			// setGroup0DataAcknowledgement(false)", m_name.c_str());
			setGroup0DataAcknowledgement(false);
		}
		if (m_group1_mode == MODE1 && m_group1_port1InOut == OUTPUT &&
		    getGroup1DataAvailability() && getGroup1DataAcknowledgement()) {
			// IMF_LOG("%s (valueChanged):
			// setGroup1DataAvailability(false)", m_name.c_str());
			setGroup1DataAvailability(false);
		}
		if (m_group1_mode == MODE1 && m_group1_port1InOut == INPUT &&
		    !getGroup1DataAvailability() && getGroup1DataAcknowledgement()) {
			// IMF_LOG("%s (valueChanged):
			// setGroup1DataAcknowledgement(false)", m_name.c_str());
			setGroup1DataAcknowledgement(false);
		}
		updateInterruptLines();
	}

	void reset()
	{
		// When the RESET input is high, the PD71055 is reset. The group
		// 0 and the group 1 ports are set to mode 0 (basic I/O port
		// mode). All port bits are cleared to zero and all ports are
		// set for input
		m_group0_mode            = MODE0;
		m_group0_port0InOut      = INPUT;
		m_group0_port2UpperInOut = INPUT;
		m_group1_mode            = MODE0;
		m_group1_port1InOut      = INPUT;
		m_group1_port2LowerInOut = INPUT;
		clearAllData();
	}

	uint8_t readPortPIU0()
	{
		// reading PIU0 is not dependent on the mode of group0
		// if group0 was specified as OUTPUT then it returns the last
		// value written
		const uint8_t val = readPort0();
		// IMF_LOG("%s: readPortPIU0 -> value=0x%X", m_name.c_str(), val);
		if (m_group0_mode == MODE1 && m_group0_port0InOut == INPUT) {
			setGroup0DataAcknowledgement(true);
		}
		return val;
	}
	void writePortPIU0(uint8_t val)
	{
		if (m_group0_port0InOut == OUTPUT) {
			// IMF_LOG("%s: writePortPIU0 / value=0x%X",
			// m_name.c_str(), val);
			writePort0(val);
			if (m_group0_mode == MODE1 && m_group0_port0InOut == OUTPUT) {
				setGroup0DataAvailability(true);
			}
		} else {
			IMF_LOG("%s: writePortPIU0 / value=0x%X in an input port -> NOP",
			        m_name.c_str(),
			        val);
		}
	}
	uint8_t readPortPIU1()
	{
		// reading PIU1 is not dependant on the mode of group1
		// if group1 was specified as OUTPUT then it returns the last
		// value written
		const uint8_t val = readPort1();
		// IMF_LOG("%s: readPortPIU1 -> value=0x%X", m_name.c_str(), val);
		if (m_group1_mode == MODE1 && m_group1_port1InOut == INPUT) {
			setGroup1DataAcknowledgement(true);
		}
		return val;
	}
	void writePortPIU1(uint8_t val)
	{
		if (m_group1_port1InOut == OUTPUT) {
			// IMF_LOG("%s: writePortPIU1 / value=0x%X",
			// m_name.c_str(), val);
			writePort1(val);
			if (m_group1_mode == MODE1 && m_group1_port1InOut == OUTPUT) {
				setGroup1DataAvailability(true);
			}
		} else {
			IMF_LOG("%s: writePortPIU1 / value=0x%X in an input port -> NOP",
			        m_name.c_str(),
			        val);
		}
	}
	uint8_t readPortPIU2()
	{
		uint8_t val = readPort2();
		if (m_group1_mode == MODE1) {
			val = (val & (0x04 ^ 0xFF)) |
			      (m_group1_readInterruptEnable || m_group1_writeInterruptEnable
			               ? 0x04
			               : 0x00);
		}
		if (m_group0_mode == MODE1 && m_group0_port0InOut == INPUT) {
			val = (val & (0x10 ^ 0xFF)) |
			      (m_group1_readInterruptEnable || m_group1_writeInterruptEnable
			               ? 0x10
			               : 0x00);
		}
		// IMF_LOG("%s: readPortPIU2 -> value=0x%X", m_name.c_str(), val);
		/* The VAPI driver included with MusicPrinterPlus relies on a reserved bit being set */
		val |= 0x02;
		return val;
	}
	void writePortPIU2(uint8_t val)
	{
		// IMF_LOG("%s: writePortPIU2 / value=0x%X", m_name.c_str(),
		// val);
		//  bit0, bit1, bit2
		if (m_group1_mode == MODE0) {
			m_port2[0].setValue((val & 0x01) != 0);
			m_port2[1].setValue((val & 0x02) != 0);
			m_port2[2].setValue((val & 0x04) != 0);
		}
		// bit3
		if (m_group1_mode == MODE0 && m_group0_mode == MODE0) {
			m_port2[3].setValue((val & 0x08) != 0);
		}
		// bit4, bit5, bit6, bit7
		if (m_group0_mode == MODE0) {
			m_port2[4].setValue((val & 0x10) != 0);
			m_port2[5].setValue((val & 0x20) != 0);
			m_port2[6].setValue((val & 0x40) != 0);
			m_port2[7].setValue((val & 0x80) != 0);
		}
	}
	static uint8_t readPortPCR()
	{
		// IMF_LOG("%s: readPortPCR -> 0x00", m_name.c_str());
		//  Based on the PD71055 datasheet:
		//   The host writes command words to the PD71055 in
		//   this register. These commands control group 0 and
		//   group 1. Note that the contents of this register cannot
		//   be read.
		return 0;
	}
	void writePortPCR(uint8_t val)
	{
		// IMF_LOG("writePortPCR / value=0x%X", val);
		//  bit 7: Command select
		if ((val & 0b10000000) != 0) {
			// clang-format off
			// Command: Mode select
			//  bit 6&5: Group 0 Mode (00=Mode 0 / 01=Mode 1 / 1X=Mode 2)
			//  bit 4:   Group 0 Port0 (0=Output / 1=Input)
			//  bit 3:   Group 0 Port2Upper (0=Output / 1=Input)
			//  bit 2:   Group 1 Mode (0=Mode 0 / 1=Mode 1)
			//  bit 1:   Group 1 Port1 (0=Output / 1=Input)
			//  bit 0:   Group 1 Port2Lower (0=Output / 1=Input)
			// clang-format on

			// set the group 0 mode
			switch (val & 0b01100000) {
			case 0b00000000: // Mode 0
				m_group0_mode = MODE0;
				break;
			case 0b00100000: // Mode 1
				m_group0_mode = MODE1;
				break;
			case 0b01000000: // Mode 2
			case 0b01100000: // Mode 2
				m_group0_mode = MODE2;
				break;
			}
			m_group0_port0InOut = (val & 0b00010000) != 0 ? INPUT
			                                              : OUTPUT;
			m_group0_port2UpperInOut = (val & 0b00001000) != 0
			                                 ? INPUT
			                                 : OUTPUT;
			m_group1_mode = (val & 0b00000100) != 0 ? MODE1 : MODE0;
			m_group1_port1InOut = (val & 0b00000010) != 0 ? INPUT
			                                              : OUTPUT;
			m_group1_port2LowerInOut = (val & 0b00000001) != 0
			                                 ? INPUT
			                                 : OUTPUT;
			IMF_LOG("%s: new mode: Group0(%s/%s/%s) / Group1(%s/%s/%s)",
			        m_name.c_str(),
			        m_group0_mode == MODE0   ? "MODE0"
			        : m_group0_mode == MODE1 ? "MODE1"
			                                 : "MODE2",
			        m_group0_port0InOut == INPUT ? "INPUT" : "OUTPUT",
			        m_group0_port2UpperInOut == INPUT ? "INPUT" : "OUTPUT",
			        m_group1_mode == MODE0   ? "MODE0"
			        : m_group0_mode == MODE1 ? "MODE1"
			                                 : "MODE2",
			        m_group1_port1InOut == INPUT ? "INPUT" : "OUTPUT",
			        m_group1_port2LowerInOut == INPUT ? "INPUT"
			                                          : "OUTPUT");
			// The bits of all ports are cleared when a mode is
			// selected or when the PD71055 is reset
			clearAllData();
		} else {
			// Command: Bit manipulation
			//  bit 6&5&4: unused
			//  bit 3&2&1: Port 2 bit select (000=Bit0 / 001=Bit1 /
			//  ... / 111=Bit7)
			const int bitNr     = (val & 0xE) >> 1;
			const bool bitValue = (val & 1) != 0;
			if (bitNr == 2 && m_group1_mode == MODE1) {
				if (m_group1_port1InOut == INPUT) {
					// IMF_LOG("%s:
					// m_group1_readInterruptEnable now set
					// to %s", m_name.c_str(), bitValue ?
					// "TRUE" : "FALSE");
					m_group1_readInterruptEnable = bitValue;
					m_group1_writeInterruptEnable = false;
				} else {
					// IMF_LOG("%s:
					// m_group1_writeInterruptEnable now set
					// to %s", m_name.c_str(), bitValue ?
					// "TRUE" : "FALSE");
					m_group1_readInterruptEnable  = false;
					m_group1_writeInterruptEnable = bitValue;
				}
				updateInterruptLines();
			} else if (bitNr == 4 && ((m_group0_mode == MODE1 &&
			                           m_group0_port0InOut == INPUT) ||
			                          (m_group0_mode == MODE2))) {
				// IMF_LOG("%s: m_group0_readInterruptEnable now
				// set to %s", m_name.c_str(), bitValue ? "TRUE"
				// : "FALSE");
				m_group0_readInterruptEnable = bitValue;
				updateInterruptLines();
			} else if (bitNr == 6 && ((m_group0_mode == MODE1 &&
			                           m_group0_port0InOut == OUTPUT) ||
			                          (m_group0_mode == MODE2))) {
				// IMF_LOG("%s: m_group0_writeInterruptEnable
				// now set to %s", m_name.c_str(), bitValue ?
				// "TRUE" : "FALSE");
				m_group0_writeInterruptEnable = bitValue;
				updateInterruptLines();
			} else if (m_group1_mode == MODE0 && bitNr <= 2) {
				assert(bitNr >= 0);
				if (m_group1_port2LowerInOut == OUTPUT) {
					m_port2[bitNr].setValue(bitValue);
				} else {
					IMF_LOG("%s: WARNING: trying to set a bit for an INPUT configuration",
					        m_name.c_str());
				}
			} else if (m_group1_mode == MODE0 &&
			           m_group0_mode == MODE0 && bitNr == 3) {
				if (m_group1_port2LowerInOut == OUTPUT) {
					m_port2[bitNr].setValue(bitValue);
				} else {
					IMF_LOG("%s: WARNING: trying to set a bit for an INPUT configuration",
					        m_name.c_str());
				}
			} else if (m_group1_mode == MODE1 &&
			           m_group0_mode == MODE0 && bitNr == 3) {
				if (m_group1_port2LowerInOut == OUTPUT) {
					m_port2[bitNr].setValue(bitValue);
				} else {
					IMF_LOG("%s: WARNING: trying to set a bit for an INPUT configuration",
					        m_name.c_str());
				}
			} else if (m_group0_mode == MODE0 && bitNr >= 4) {
				assert(bitNr <= 7);
				if (m_group0_port2UpperInOut == OUTPUT) {
					m_port2[bitNr].setValue(bitValue);
				} else {
					IMF_LOG("%s: WARNING: trying to set a bit for an INPUT configuration",
					        m_name.c_str());
				}
			} else if (m_group0_mode == MODE1 && bitNr >= 6 &&
			           m_group0_port2UpperInOut == INPUT) {
				assert(bitNr <= 7);
				IMF_LOG("%s: WARNING: trying to set a bit for an INPUT configuration",
				        m_name.c_str());
			} else if (m_group0_mode == MODE1 && bitNr >= 4 &&
			           bitNr <= 5 && m_group0_port2UpperInOut == OUTPUT) {
				m_port2[bitNr].setValue(bitValue);
			}
		}
	}
	void setGroupModes(GroupMode group0_mode, PortInOut port0InOut,
	                   PortInOut port2UpperInOut, GroupMode group1_mode,
	                   PortInOut port1InOut, PortInOut port2LowerInOut)
	{
		// clang-format off
		// Command: Mode select
		//  bit 6&5: Group 0 Mode (00=Mode 0 / 01=Mode 1 / 1X=Mode 2)
		//  bit 4:   Group 0 Port0 (0=Output / 1=Input)
		//  bit 3:   Group 0 Port2Upper (0=Output / 1=Input)
		//  bit 2:   Group 1 Mode (0=Mode 0 / 1=Mode 1)
		//  bit 1:   Group 1 Port1 (0=Output / 1=Input)
		//  bit 0:   Group 1 Port2Lower (0=Output / 1=Input)
		// clang-format on

		writePortPCR(0x80 |
		             (group0_mode == MODE0   ? 0x00
		              : group0_mode == MODE1 ? 0x20
		                                     : 0x40) |
		             (port0InOut == OUTPUT ? 0x00 : 0x10) |
		             (port2UpperInOut == OUTPUT ? 0x00 : 0x08) |
		             (group1_mode == MODE0 ? 0x00 : 0x04) |
		             (port1InOut == OUTPUT ? 0x00 : 0x02) |
		             (port2LowerInOut == OUTPUT ? 0x00 : 0x01));
	}
	void setPort2Bit(uint8_t bitNr, bool val)
	{
		// Command: Bit manipulation
		//  bit 6&5&4: unused
		//  bit 3&2&1: Port 2 bit select (000=Bit0 / 001=Bit1 / ... /
		//  111=Bit7)
		writePortPCR(((bitNr & 7) << 1) | (val ? 1 : 0));
	}
};

class IrqController : public DataChangedConsumer<bool> {
private:
	const std::string m_name = {};
	std::atomic<bool> m_enabled{false};
	bool m_debug{false};
	std::atomic<bool> m_interruptOutput{false};
	std::deque<DataProvider<bool>*> m_interruptLines  = {};
	std::function<void(void)> m_callbackOnLowToHigh   = {};
	std::function<void(void)> m_callbackOnHighToLow   = {};

	bool atLeastOneInterruptLineIsHigh()
	{
		bool result = false;
		for (auto& m_interruptLine : m_interruptLines) {
			if (m_interruptLine->getValue()) {
				result = true;
			}
		}
		return result;
	}
	void triggerCallIfNeeded()
	{
		if (m_debug) {
			IMF_LOG("%s - triggerCallIfNeeded()", m_name.c_str());
		}
		const bool oldInterruptOutput = m_interruptOutput;
		const bool currentStatus      = atLeastOneInterruptLineIsHigh();
		m_interruptOutput = m_enabled ? currentStatus : false;
		if (m_debug) {
			IMF_LOG("%s - triggerCallIfNeeded() - oldInterruptOutput=%i / currentStatus=%i / m_interruptOutput=%i",
			        m_name.c_str(),
			        oldInterruptOutput,
			        currentStatus,
			        m_interruptOutput);
		}
		if (!oldInterruptOutput && m_interruptOutput) {
			if (m_callbackOnLowToHigh) {
				if (m_debug) {
					IMF_LOG("%s - calling m_callbackOnLowToHigh",
					        m_name.c_str());
				}
				m_callbackOnLowToHigh();
			}
		} else if (oldInterruptOutput && !m_interruptOutput) {
			if (m_callbackOnHighToLow) {
				if (m_debug) {
					IMF_LOG("%s - calling m_callbackOnHighToLow",
					        m_name.c_str());
				}
				m_callbackOnHighToLow();
			}
		}
	}

public:
	IrqController(const std::string& name,
	              std::function<void(void)> callbackOnLowToHigh,
	              std::function<void(void)> callbackOnToHighToLow)
	        : m_name(name),
	          m_callbackOnLowToHigh(std::move(callbackOnLowToHigh)),
	          m_callbackOnHighToLow(std::move(callbackOnToHighToLow))
	{}
	void enableInterrupts()
	{
		if (m_enabled) {
			return;
		}
		if (m_debug) {
			IMF_LOG("%s - interrupts enabled", m_name.c_str());
		}
		m_enabled = true;
		triggerCallIfNeeded();
	}
	void disableInterrupts()
	{
		if (!m_enabled) {
			return;
		}
		if (m_debug) {
			IMF_LOG("%s - interrupts disabled", m_name.c_str());
		}
		m_enabled = false;
		triggerCallIfNeeded();
	}
	void connectInterruptLine(DataProvider<bool>* dataProvider)
	{
		m_interruptLines.push_back(dataProvider);
		dataProvider->notifyOnChange(this);
	}
	void valueChanged(bool /*oldValue*/, bool /*newValue*/) override
	{
		triggerCallIfNeeded();
	}
	void setDebug(bool val)
	{
		m_debug = val;
	}
};

// clang-format off
/*
From the looks of it, there is a different clock frequency being used.
Based on the output generated, the following frequencies are captured
(Original/MyVersion in Hz):

	 259                    187
	 778                    542
	1047                    742
	1301                    915
	1567                    1102
	3670                    2595
	4192                    2953
*/
// clang-format on

#define u8     uint8_t
#define offs_t uint8_t

// 16.16 fixed point (frequency calculations)
constexpr int8_t FREQ_SH = 16;
constexpr auto FREQ_MASK = (1 << FREQ_SH) - 1;

// 16.16 fixed point (envelope generator timing)
constexpr int8_t EG_SH = 16;

// 22.10 fixed point (LFO calculations)
constexpr int8_t LFO_SH = 10;

// 16.16 fixed point (timers calculations)
[[maybe_unused]] constexpr int8_t TIMER_SH = 16;

constexpr int8_t ENV_BITS = 10;
constexpr auto ENV_LEN    = (1 << ENV_BITS);
constexpr auto ENV_STEP   = (128.0 / ENV_LEN);

constexpr auto MAX_ATT_INDEX = (ENV_LEN - 1); /* 1023 */
constexpr auto MIN_ATT_INDEX = (0);           /* 0 */

constexpr auto EG_ATT = 4;
constexpr auto EG_DEC = 3;
constexpr auto EG_SUS = 2;
constexpr auto EG_REL = 1;
constexpr auto EG_OFF = 0;

#define ENV_QUIET (TL_TAB_LEN >> 3)

class ym2151_device {
public:
	// construction/destruction
	explicit ym2151_device();
	~ym2151_device();

	// configuration helpers
	// auto irq_handler() { return m_irqhandler.bind(); }
	// auto port_write_handler() { return m_portwritehandler.bind(); }

	// read/write
	uint8_t read(const offs_t offset) const;
	void write(const offs_t offset, const uint8_t data);

	uint8_t status_r() const;
	void register_w(const uint8_t data);
	void data_w(const uint8_t data);

	// DECLARE_WRITE_LINE_MEMBER(reset_w);

	// device-level overrides
	void device_start();
	void device_reset();
	// void device_timer(emu_timer &timer, device_timer_id id, int param,
	// void *ptr);
	void device_post_load();
	void device_clock_changed();

	// sound stream update overrides
	void sound_stream_update(const uint16_t requested_frames, int32_t* buffer);

private:
	AudioFrame RenderFrame();
	void RenderUpToNow();

	enum : uint8_t {
		TIMER_IRQ_A_OFF,
		TIMER_IRQ_B_OFF,
		TIMER_A,
		TIMER_B,
	};

	enum : uint16_t {
		RATE_STEPS = 8,
		TL_RES_LEN = 256, /* 8 bits addressing (real chip) */

		/*  TL_TAB_LEN is calculated as:
		 *   13 - sinus amplitude bits     (Y axis)
		 *   2  - sinus sign bit           (Y axis)
		 *   TL_RES_LEN - sinus resolution (X axis)
		 */
		TL_TAB_LEN = 13 * 2 * TL_RES_LEN,

		SIN_BITS = 10,
		SIN_LEN  = 1 << SIN_BITS,
		SIN_MASK = SIN_LEN - 1,
	};

	// Playback related
	std::queue<AudioFrame> fifo   = {};
	double last_rendered_ms       = 0.0;
	double ms_per_render          = 0.0;
	int frame_rate_hz             = 0;

	int tl_tab[TL_TAB_LEN]{};
	unsigned int sin_tab[SIN_LEN]{};
	uint32_t d1l_tab[16]{};

	static const uint8_t eg_inc[19 * RATE_STEPS];
	static const uint8_t eg_rate_select[32 + 64 + 32];
	static const uint8_t eg_rate_shift[32 + 64 + 32];
	static const uint32_t dt2_tab[4];
	static const uint8_t dt1_tab[4 * 32];
	static const uint16_t phaseinc_rom[768];
	static const uint8_t lfo_noise_waveform[256];

	// clang-format off
	/* struct describing a single operator */
	struct YM2151Operator {
		uint32_t      phase = 0;                  /* accumulated operator phase */
		uint32_t      freq = 0;                   /* operator frequency count */
		int32_t       dt1 = 0;                    /* current DT1 (detune 1 phase inc/decrement) value */
		uint32_t      mul = 0;                    /* frequency count multiply */
		uint32_t      dt1_i = 0;                  /* DT1 index * 32 */
		uint32_t      dt2 = 0;                    /* current DT2 (detune 2) value */

		int32_t*      connect = nullptr;          /* operator output 'direction' */

		/* only M1 (operator 0) is filled with this data: */
		int32_t*      mem_connect = nullptr;      /* where to put the delayed sample (MEM) */
		int32_t       mem_value = 0;              /* delayed sample (MEM) value */

		/* channel specific data = 0; note: each operator number 0 contains channel specific data */
		uint32_t      fb_shift = 0;               /* feedback shift value for operators 0 in each channel */
		int32_t       fb_out_curr = 0;            /* operator feedback value (used only by operators 0) */
		int32_t       fb_out_prev = 0;            /* previous feedback value (used only by operators 0) */
		uint32_t      kc = 0;                     /* channel KC (copied to all operators) */
		uint32_t      kc_i = 768;                 /* just for speedup */
		uint32_t      pms = 0;                    /* channel PMS */
		uint32_t      ams = 0;                    /* channel AMS */
		/* end of channel specific data */

		uint32_t      AMmask = 0;                 /* LFO Amplitude Modulation enable mask */
		uint32_t      state = 0;                  /* Envelope state: 4-attack(AR) 3-decay(D1R) 2-sustain(D2R) 1-release(RR) 0-off */
		uint8_t       eg_sh_ar = 0;               /*  (attack state) */
		uint8_t       eg_sel_ar = 0;              /*  (attack state) */
		uint32_t      tl = 0;                     /* Total attenuation Level */
		int32_t       volume = MAX_ATT_INDEX;     /* current envelope attenuation level */
		uint8_t       eg_sh_d1r = 0;              /*  (decay state) */
		uint8_t       eg_sel_d1r = 0;             /*  (decay state) */
		uint32_t      d1l = 0;                    /* envelope switches to sustain state after reaching this level */
		uint8_t       eg_sh_d2r = 0;              /*  (sustain state) */
		uint8_t       eg_sel_d2r = 0;             /*  (sustain state) */
		uint8_t       eg_sh_rr = 0;               /*  (release state) */
		uint8_t       eg_sel_rr = 0;              /*  (release state) */

		uint32_t      key = 0;                    /* 0=last key was KEY OFF, 1=last key was KEY ON */

		uint32_t      ks = 0;                     /* key scale    */
		uint32_t      ar = 0;                     /* attack rate  */
		uint32_t      d1r = 0;                    /* decay rate   */
		uint32_t      d2r = 0;                    /* sustain rate */
		uint32_t      rr = 0;                     /* release rate */

		uint32_t      reserved0 = 0;              /**/
		uint32_t      reserved1 = 0;              /**/
		// clang-format on

		void key_on(uint32_t key_set, uint32_t eg_cnt);
		void key_off(uint32_t key_set);
	};

	signed int chanout[8] = {};
	signed int m2         = 0;
	signed int c1         = 0;
	signed int c2         = 0; // Phase Modulation input for operators 2,3,4
	signed int mem        = 0; // one sample delay memory

	std::array<YM2151Operator, 32> oper = {}; // the 32 operators

	uint32_t pan[16] = {}; // channels output masks (0xffffffff = enable)

	uint32_t eg_cnt   = 0; // global envelope generator counter
	uint32_t eg_timer = 0; // global envelope generator counter works at
	                       // frequency = chipclock/64/3
	uint32_t eg_timer_add      = 0; // step of eg_timer
	uint32_t eg_timer_overflow = 0; // envelope generator timer overflows
	                                // every 3 samples (on real chip)

	uint32_t lfo_phase     = 0;   // accumulated LFO phase (0 to 255)
	uint32_t lfo_timer     = 0;   // LFO timer
	uint32_t lfo_timer_add = 0;   // step of lfo_timer
	uint32_t lfo_overflow  = 0;   // LFO generates new output when lfo_timer
	                              // reaches this value
	uint32_t lfo_counter     = 0; // LFO phase increment counter
	uint32_t lfo_counter_add = 0; // step of lfo_counter

	uint8_t lfo_wsel = 0; // LFO waveform (0-saw, 1-square, 2-triangle,
	                      // 3-random noise)
	uint8_t amd  = 0;     // LFO Amplitude Modulation Depth
	int8_t pmd   = 0;     // LFO Phase Modulation Depth
	uint32_t lfa = 0;     // LFO current AM output
	int32_t lfp  = 0;     // LFO current PM output

	uint8_t test = 0; // TEST register
	uint8_t ct   = 0; // output control pins (bit1-CT2, bit0-CT1)

	uint32_t noise = 0; // noise enable/period register (bit 7 - noise
	                    // enable, bits 4-0 - noise period

	uint32_t noise_rng = 0; // 17 bit noise shift register
	uint32_t noise_p   = 0; // current noise 'phase'*/
	uint32_t noise_f   = 0; // current noise period

	uint32_t csm_req = 0; // CSM  KEY ON / KEY OFF sequence request

	uint32_t irq_enable = 0; // IRQ enable for timer B (bit 3) and timer A
	                         // (bit 2); bit 7 - CSM mode (keyon to all
	                         // slots, everytime timer A overflows)

	uint8_t status     = 0;  // chip status (BUSY, IRQ Flags)
	uint8_t connect[8] = {}; // channels connections

	// emu_timer   *timer_A, *timer_A_irq_off;
	// emu_timer   *timer_B, *timer_B_irq_off;

	// attotime    timer_A_time[1024];     // timer A times for MAME
	// attotime    timer_B_time[256];      // timer B times for MAME
	int irqlinestate = 0;

	uint32_t timer_A_index     = 0; // timer A index
	uint32_t timer_B_index     = 0; // timer B index
	uint32_t timer_A_index_old = 0; // timer A previous index
	uint32_t timer_B_index_old = 0; // timer B previous index

	// clang-format off
	/*  Frequency-deltas to get the closest frequency possible.
	*   There are 11 octaves because of DT2 (max 950 cents over base frequency)
	*   and LFO phase modulation (max 800 cents below AND over base frequency)
	*   Summary:   octave  explanation
	*              0       note code - LFO PM
	*              1       note code
	*              2       note code
	*              3       note code
	*              4       note code
	*              5       note code
	*              6       note code
	*              7       note code
	*              8       note code
	*              9       note code + DT2 + LFO PM
	*              10      note code + DT2 + LFO PM
	*/
	// clang-format on

	uint32_t freq[11 * 768] = {}; /* 11 octaves, 768 'cents' per octave */

	/*  Frequency deltas for DT1. These deltas alter operator frequency
	 *   after it has been taken from frequency-deltas table.
	 */
	int32_t dt1_freq[8 * 32] = {}; /* 8 DT1 levels, 32 KC values */

	uint32_t noise_tab[32] = {}; /* 17bit Noise Generator periods */

	// internal state
	// sound_stream *         m_stream;
	uint8_t m_lastreg{0};
	// devcb_write_line       m_irqhandler;
	// devcb_write8           m_portwritehandler;
	bool m_reset_active{false};

	void init_tables();
	void calculate_timers();
	void envelope_KONKOFF(YM2151Operator* op, int v) const;
	void set_connect(YM2151Operator* om1, int cha, int v);
	void advance();
	void advance_eg();
	void write_reg(const int reg, const int val);
	void chan_calc(const uint8_t chan);
	void chan7_calc();
	int op_calc(YM2151Operator* OP, unsigned int env, signed int pm);
	int op_calc1(YM2151Operator* OP, unsigned int env, signed int pm);
	static void refresh_EG(YM2151Operator* op);
};

constexpr uint8_t ym2151_device::eg_inc[19 * RATE_STEPS] = {
        // clang-format off
	/*cycle:0 1  2 3  4 5  6 7*/

	/* 0 */ 0,1, 0,1, 0,1, 0,1, /* rates 00..11 0 (increment by 0 or 1) */
	/* 1 */ 0,1, 0,1, 1,1, 0,1, /* rates 00..11 1 */
	/* 2 */ 0,1, 1,1, 0,1, 1,1, /* rates 00..11 2 */
	/* 3 */ 0,1, 1,1, 1,1, 1,1, /* rates 00..11 3 */

	/* 4 */ 1,1, 1,1, 1,1, 1,1, /* rate 12 0 (increment by 1) */
	/* 5 */ 1,1, 1,2, 1,1, 1,2, /* rate 12 1 */
	/* 6 */ 1,2, 1,2, 1,2, 1,2, /* rate 12 2 */
	/* 7 */ 1,2, 2,2, 1,2, 2,2, /* rate 12 3 */

	/* 8 */ 2,2, 2,2, 2,2, 2,2, /* rate 13 0 (increment by 2) */
	/* 9 */ 2,2, 2,4, 2,2, 2,4, /* rate 13 1 */
	/*10 */ 2,4, 2,4, 2,4, 2,4, /* rate 13 2 */
	/*11 */ 2,4, 4,4, 2,4, 4,4, /* rate 13 3 */

	/*12 */ 4,4, 4,4, 4,4, 4,4, /* rate 14 0 (increment by 4) */
	/*13 */ 4,4, 4,8, 4,4, 4,8, /* rate 14 1 */
	/*14 */ 4,8, 4,8, 4,8, 4,8, /* rate 14 2 */
	/*15 */ 4,8, 8,8, 4,8, 8,8, /* rate 14 3 */

	/*16 */ 8,8, 8,8, 8,8, 8,8, /* rates 15 0, 15 1, 15 2, 15 3 (increment by 8) */
	/*17 */ 16,16,16,16,16,16,16,16, /* rates 15 2, 15 3 for attack */
	/*18 */ 0,0, 0,0, 0,0, 0,0, /* infinity rates for attack and decay(s) */
        // clang-format on
};

#define O(a) ((a)*RATE_STEPS)

/*note that there is no O(17) in this table - it's directly in the code */
constexpr uint8_t ym2151_device::eg_rate_select[32 + 64 + 32] = {
        // clang-format off
/* Envelope Generator rates (32 + 64 rates + 32 RKS) */
	/* 32 dummy (infinite time) rates */
	O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),
	O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),
	O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),
	O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),

	/* rates 00-11 */
	O(0),O(1),O(2),O(3),
	O(0),O(1),O(2),O(3),
	O(0),O(1),O(2),O(3),
	O(0),O(1),O(2),O(3),
	O(0),O(1),O(2),O(3),
	O(0),O(1),O(2),O(3),
	O(0),O(1),O(2),O(3),
	O(0),O(1),O(2),O(3),
	O(0),O(1),O(2),O(3),
	O(0),O(1),O(2),O(3),
	O(0),O(1),O(2),O(3),
	O(0),O(1),O(2),O(3),

	/* rate 12 */
	O(4),O(5),O(6),O(7),

	/* rate 13 */
	O(8),O(9),O(10),O(11),

	/* rate 14 */
	O(12),O(13),O(14),O(15),

	/* rate 15 */
	O(16),O(16),O(16),O(16),

	/* 32 dummy rates (same as 15 3) */
	O(16),O(16),O(16),O(16),O(16),O(16),O(16),O(16),
	O(16),O(16),O(16),O(16),O(16),O(16),O(16),O(16),
	O(16),O(16),O(16),O(16),O(16),O(16),O(16),O(16),
	O(16),O(16),O(16),O(16),O(16),O(16),O(16),O(16)
        // clang-format on
};
#undef O

// clang-format off
/*rate  0,    1,    2,   3,   4,   5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15*/
/*shift 11,   10,   9,   8,   7,   6,  5,  4,  3,  2, 1,  0,  0,  0,  0,  0 */
/*mask  2047, 1023, 511, 255, 127, 63, 31, 15, 7,  3, 1,  0,  0,  0,  0,  0 */
// clang-format on

#define O(a) ((a)*1)
constexpr uint8_t ym2151_device::eg_rate_shift[32 + 64 + 32] = {
        // clang-format off
   /* Envelope Generator counter shifts (32 + 64 rates + 32 RKS) */
	/* 32 infinite time rates */
	O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0),
	O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0),
	O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0),
	O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0),

	/* rates 00-11 */
	O(11),O(11),O(11),O(11),
	O(10),O(10),O(10),O(10),
	O(9),O(9),O(9),O(9),
	O(8),O(8),O(8),O(8),
	O(7),O(7),O(7),O(7),
	O(6),O(6),O(6),O(6),
	O(5),O(5),O(5),O(5),
	O(4),O(4),O(4),O(4),
	O(3),O(3),O(3),O(3),
	O(2),O(2),O(2),O(2),
	O(1),O(1),O(1),O(1),
	O(0),O(0),O(0),O(0),

	/* rate 12 */
	O(0),O(0),O(0),O(0),

	/* rate 13 */
	O(0),O(0),O(0),O(0),

	/* rate 14 */
	O(0),O(0),O(0),O(0),

	/* rate 15 */
	O(0),O(0),O(0),O(0),

	/* 32 dummy rates (same as 15 3) */
	O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0),
	O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0),
	O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0),
	O(0),O(0),O(0),O(0),O(0),O(0),O(0),O(0)
        // clang-format on
};
#undef O

// clang-format off
/*  DT2 defines offset in cents from base note
*
*   This table defines offset in frequency-deltas table.
*   User's Manual page 22
*
*   Values below were calculated using formula: value =  orig.val / 1.5625
*
*   DT2=0 DT2=1 DT2=2 DT2=3
*   0     600   781   950
*/
// clang-format on
constexpr uint32_t ym2151_device::dt2_tab[4] = {0, 384, 500, 608};

/*  DT1 defines offset in Hertz from base note
 *   This table is converted while initialization...
 *   Detune table shown in YM2151 User's Manual is wrong (verified on the real
 * chip)
 */

constexpr uint8_t ym2151_device::dt1_tab[4 * 32] = {
        /* 4*32 DT1 values */
        // clang-format off
/* DT1=0 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	/* DT1=1 */
		0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
		2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8,

		/* DT1=2 */
			1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5,
			5, 6, 6, 7, 8, 8, 9,10,11,12,13,14,16,16,16,16,

			/* DT1=3 */
				2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
				8, 8, 9,10,11,12,13,14,16,17,19,20,22,22,22,22
        // clang-format on
};

constexpr uint16_t ym2151_device::phaseinc_rom[768] = {
        // clang-format off
	1299,1300,1301,1302,1303,1304,1305,1306,1308,1309,1310,1311,1313,1314,1315,1316,
	1318,1319,1320,1321,1322,1323,1324,1325,1327,1328,1329,1330,1332,1333,1334,1335,
	1337,1338,1339,1340,1341,1342,1343,1344,1346,1347,1348,1349,1351,1352,1353,1354,
	1356,1357,1358,1359,1361,1362,1363,1364,1366,1367,1368,1369,1371,1372,1373,1374,
	1376,1377,1378,1379,1381,1382,1383,1384,1386,1387,1388,1389,1391,1392,1393,1394,
	1396,1397,1398,1399,1401,1402,1403,1404,1406,1407,1408,1409,1411,1412,1413,1414,
	1416,1417,1418,1419,1421,1422,1423,1424,1426,1427,1429,1430,1431,1432,1434,1435,
	1437,1438,1439,1440,1442,1443,1444,1445,1447,1448,1449,1450,1452,1453,1454,1455,
	1458,1459,1460,1461,1463,1464,1465,1466,1468,1469,1471,1472,1473,1474,1476,1477,
	1479,1480,1481,1482,1484,1485,1486,1487,1489,1490,1492,1493,1494,1495,1497,1498,
	1501,1502,1503,1504,1506,1507,1509,1510,1512,1513,1514,1515,1517,1518,1520,1521,
	1523,1524,1525,1526,1528,1529,1531,1532,1534,1535,1536,1537,1539,1540,1542,1543,
	1545,1546,1547,1548,1550,1551,1553,1554,1556,1557,1558,1559,1561,1562,1564,1565,
	1567,1568,1569,1570,1572,1573,1575,1576,1578,1579,1580,1581,1583,1584,1586,1587,
	1590,1591,1592,1593,1595,1596,1598,1599,1601,1602,1604,1605,1607,1608,1609,1610,
	1613,1614,1615,1616,1618,1619,1621,1622,1624,1625,1627,1628,1630,1631,1632,1633,
	1637,1638,1639,1640,1642,1643,1645,1646,1648,1649,1651,1652,1654,1655,1656,1657,
	1660,1661,1663,1664,1666,1667,1669,1670,1672,1673,1675,1676,1678,1679,1681,1682,
	1685,1686,1688,1689,1691,1692,1694,1695,1697,1698,1700,1701,1703,1704,1706,1707,
	1709,1710,1712,1713,1715,1716,1718,1719,1721,1722,1724,1725,1727,1728,1730,1731,
	1734,1735,1737,1738,1740,1741,1743,1744,1746,1748,1749,1751,1752,1754,1755,1757,
	1759,1760,1762,1763,1765,1766,1768,1769,1771,1773,1774,1776,1777,1779,1780,1782,
	1785,1786,1788,1789,1791,1793,1794,1796,1798,1799,1801,1802,1804,1806,1807,1809,
	1811,1812,1814,1815,1817,1819,1820,1822,1824,1825,1827,1828,1830,1832,1833,1835,
	1837,1838,1840,1841,1843,1845,1846,1848,1850,1851,1853,1854,1856,1858,1859,1861,
	1864,1865,1867,1868,1870,1872,1873,1875,1877,1879,1880,1882,1884,1885,1887,1888,
	1891,1892,1894,1895,1897,1899,1900,1902,1904,1906,1907,1909,1911,1912,1914,1915,
	1918,1919,1921,1923,1925,1926,1928,1930,1932,1933,1935,1937,1939,1940,1942,1944,
	1946,1947,1949,1951,1953,1954,1956,1958,1960,1961,1963,1965,1967,1968,1970,1972,
	1975,1976,1978,1980,1982,1983,1985,1987,1989,1990,1992,1994,1996,1997,1999,2001,
	2003,2004,2006,2008,2010,2011,2013,2015,2017,2019,2021,2022,2024,2026,2028,2029,
	2032,2033,2035,2037,2039,2041,2043,2044,2047,2048,2050,2052,2054,2056,2058,2059,
	2062,2063,2065,2067,2069,2071,2073,2074,2077,2078,2080,2082,2084,2086,2088,2089,
	2092,2093,2095,2097,2099,2101,2103,2104,2107,2108,2110,2112,2114,2116,2118,2119,
	2122,2123,2125,2127,2129,2131,2133,2134,2137,2139,2141,2142,2145,2146,2148,2150,
	2153,2154,2156,2158,2160,2162,2164,2165,2168,2170,2172,2173,2176,2177,2179,2181,
	2185,2186,2188,2190,2192,2194,2196,2197,2200,2202,2204,2205,2208,2209,2211,2213,
	2216,2218,2220,2222,2223,2226,2227,2230,2232,2234,2236,2238,2239,2242,2243,2246,
	2249,2251,2253,2255,2256,2259,2260,2263,2265,2267,2269,2271,2272,2275,2276,2279,
	2281,2283,2285,2287,2288,2291,2292,2295,2297,2299,2301,2303,2304,2307,2308,2311,
	2315,2317,2319,2321,2322,2325,2326,2329,2331,2333,2335,2337,2338,2341,2342,2345,
	2348,2350,2352,2354,2355,2358,2359,2362,2364,2366,2368,2370,2371,2374,2375,2378,
	2382,2384,2386,2388,2389,2392,2393,2396,2398,2400,2402,2404,2407,2410,2411,2414,
	2417,2419,2421,2423,2424,2427,2428,2431,2433,2435,2437,2439,2442,2445,2446,2449,
	2452,2454,2456,2458,2459,2462,2463,2466,2468,2470,2472,2474,2477,2480,2481,2484,
	2488,2490,2492,2494,2495,2498,2499,2502,2504,2506,2508,2510,2513,2516,2517,2520,
	2524,2526,2528,2530,2531,2534,2535,2538,2540,2542,2544,2546,2549,2552,2553,2556,
	2561,2563,2565,2567,2568,2571,2572,2575,2577,2579,2581,2583,2586,2589,2590,2593
        // clang-format on
};

/*
        Noise LFO waveform.

        Here are just 256 samples out of much longer data.

        It does NOT repeat every 256 samples on real chip and I wasnt able to
   find the point where it repeats (even in strings as long as 131072 samples).

        I only put it here because its better than nothing and perhaps
        someone might be able to figure out the real algorithm.


        Note that (due to the way the LFO output is calculated) it is quite
        possible that two values: 0x80 and 0x00 might be wrong in this table.
        To be exact:
                some 0x80 could be 0x81 as well as some 0x00 could be 0x01.
*/

constexpr uint8_t ym2151_device::lfo_noise_waveform[256] = {
        // clang-format off
	0xFF,0xEE,0xD3,0x80,0x58,0xDA,0x7F,0x94,0x9E,0xE3,0xFA,0x00,0x4D,0xFA,0xFF,0x6A,
	0x7A,0xDE,0x49,0xF6,0x00,0x33,0xBB,0x63,0x91,0x60,0x51,0xFF,0x00,0xD8,0x7F,0xDE,
	0xDC,0x73,0x21,0x85,0xB2,0x9C,0x5D,0x24,0xCD,0x91,0x9E,0x76,0x7F,0x20,0xFB,0xF3,
	0x00,0xA6,0x3E,0x42,0x27,0x69,0xAE,0x33,0x45,0x44,0x11,0x41,0x72,0x73,0xDF,0xA2,

	0x32,0xBD,0x7E,0xA8,0x13,0xEB,0xD3,0x15,0xDD,0xFB,0xC9,0x9D,0x61,0x2F,0xBE,0x9D,
	0x23,0x65,0x51,0x6A,0x84,0xF9,0xC9,0xD7,0x23,0xBF,0x65,0x19,0xDC,0x03,0xF3,0x24,
	0x33,0xB6,0x1E,0x57,0x5C,0xAC,0x25,0x89,0x4D,0xC5,0x9C,0x99,0x15,0x07,0xCF,0xBA,
	0xC5,0x9B,0x15,0x4D,0x8D,0x2A,0x1E,0x1F,0xEA,0x2B,0x2F,0x64,0xA9,0x50,0x3D,0xAB,

	0x50,0x77,0xE9,0xC0,0xAC,0x6D,0x3F,0xCA,0xCF,0x71,0x7D,0x80,0xA6,0xFD,0xFF,0xB5,
	0xBD,0x6F,0x24,0x7B,0x00,0x99,0x5D,0xB1,0x48,0xB0,0x28,0x7F,0x80,0xEC,0xBF,0x6F,
	0x6E,0x39,0x90,0x42,0xD9,0x4E,0x2E,0x12,0x66,0xC8,0xCF,0x3B,0x3F,0x10,0x7D,0x79,
	0x00,0xD3,0x1F,0x21,0x93,0x34,0xD7,0x19,0x22,0xA2,0x08,0x20,0xB9,0xB9,0xEF,0x51,

	0x99,0xDE,0xBF,0xD4,0x09,0x75,0xE9,0x8A,0xEE,0xFD,0xE4,0x4E,0x30,0x17,0xDF,0xCE,
	0x11,0xB2,0x28,0x35,0xC2,0x7C,0x64,0xEB,0x91,0x5F,0x32,0x0C,0x6E,0x00,0xF9,0x92,
	0x19,0xDB,0x8F,0xAB,0xAE,0xD6,0x12,0xC4,0x26,0x62,0xCE,0xCC,0x0A,0x03,0xE7,0xDD,
	0xE2,0x4D,0x8A,0xA6,0x46,0x95,0x0F,0x8F,0xF5,0x15,0x97,0x32,0xD4,0x28,0x1E,0x55
        // clang-format on
};

void ym2151_device::init_tables()
{
	for (int x = 0; x < TL_RES_LEN; x++) {
		const double m = floor(1 << 16) /
		                 pow(2, (x + 1) * (ENV_STEP / 4.0) / 8.0);

		// we never reach (1<<16) here due to the (x+1) result fits
		// within 16 bits at maximum

		int n = (int)m;     // 16 bits here
		n >>= 4;            // 12 bits here
		if ((n & 1) != 0) { // round to closest
			n = (n >> 1) + 1;
		} else {
			n = n >> 1;
		}
		// 11 bits here (rounded)
		n <<= 2; // 13 bits here (as in real chip)
		tl_tab[x * 2 + 0] = n;
		tl_tab[x * 2 + 1] = -tl_tab[x * 2 + 0];

		for (uint8_t i = 1; i < 13; i++) {
			tl_tab[x * 2 + 0 + i * 2 * TL_RES_LEN] = tl_tab[x * 2 + 0] >> i;
			tl_tab[x * 2 + 1 + i * 2 * TL_RES_LEN] =
			        -tl_tab[x * 2 + 0 + i * 2 * TL_RES_LEN];
		}
	}

	// non-standard sinus
	for (uint16_t i = 0; i < SIN_LEN; i++) {
		// verified on the real chip
		const double m = sin(((i * 2) + 1) * M_PI / static_cast<int>(SIN_LEN));

		// we never reach zero here due to ((i*2)+1)

		// convert to 'decibels'
		double o = 8 * log(1.0 / fabs(m)) / log(2.0);

		o = o / (ENV_STEP / 4);

		int n = (int)(2.0 * o);
		if ((n & 1) != 0) { // round to closest
			n = (n >> 1) + 1;
		} else {
			n = n >> 1;
		}

		sin_tab[i] = n * 2 + (m >= 0.0 ? 0 : 1);
	}

	// calculate d1l_tab table
	for (uint8_t i = 0; i < 16; i++) {
		// every 3 'dB' except for all bits = 1 =  45+48 'dB'
		d1l_tab[i] = clamp_to_uint32((i != 15 ? i : i + 16) *
		                             (4.0 / ENV_STEP));
	}

	// this loop calculates Hertz values for notes from c-0 to b-7 including
	// 64 'cents' (100/64 that is 1.5625 of real cent) per note
	// i*100/64/1200 is equal to i/768

	// real chip works with 10 bits fixed point values (10.10)

	// 768 = 12 notes and for each note 64 semitones
	for (uint16_t i = 0; i < 768; i++) {
		// octave 2 - reference octave

		// adjust to X.10 fixed point
		freq[768 + 2 * 768 + i] = (phaseinc_rom[i] << (FREQ_SH - 10)) &
		                          0xffffffc0;

		// Loris: Fix for different clock frequency
		freq[768 + 2 * 768 + i] = clamp_to_uint32(
		        3.4375 * pow(2, 15.0 + (i + 1 * 64) / 768.0));

		// octave 0 and octave 1
		for (uint8_t j = 0; j < 2; j++) {
			/* adjust to X.10 fixed point */
			freq[768 + j * 768 + i] = (freq[768 + 2 * 768 + i] >>
			                           (2 - j)) &
			                          0xffffffc0;
		}
		// octave 3 to 7
		for (uint8_t j = 3; j < 8; j++) {
			freq[768 + j * 768 + i] = freq[768 + 2 * 768 + i]
			                       << (j - 2);
		}
	}

	// octave -1 (all equal to: oct 0, _KC_00_, _KF_00_)
	for (uint16_t i = 0; i < 768; i++) {
		freq[0 * 768 + i] = freq[1 * 768 + 0];
	}

	// octave 8 and 9 (all equal to: oct 7, _KC_14_, _KF_63_)
	for (uint8_t j = 8; j < 10; j++) {
		for (uint16_t i = 0; i < 768; i++) {
			freq[768 + j * 768 + i] = freq[768 + 8 * 768 - 1];
		}
	}

	for (uint8_t j = 0; j < 4; j++) {
		for (uint16_t i = 0; i < 32; i++) {
			// calculate phase increment, positive and negative values
			dt1_freq[(j + 0) * 32 + i] = (dt1_tab[j * 32 + i] * SIN_LEN) >>
			                             (20 - FREQ_SH);
			dt1_freq[(j + 4) * 32 + i] = -dt1_freq[(j + 0) * 32 + i];
		}
	}

	// calculate noise periods table
	for (uint16_t i = 0; i < 32; i++) {
		 // rate 30 and 31 are the same
		uint16_t j = (i != 31 ? i : 30);
		j = 32 - j;

		// number of samples per one shift of the shift register
		j = (uint16_t)(65536 / (j * 32));

		// number of chip clock cycles per one shift
		noise_tab[i] = j * 64;
	}
}

// clang-format off
//void ym2151_device::calculate_timers() {
//	/* calculate timers' deltas */
//	for (uint16_t i = 0; i < 1024; i++) {
//		/* ASG 980324: changed to compute both tim_A_tab and timer_A_time */
//		timer_A_time[i] = clocks_to_attotime(64 * (1024 - i));
//	}
//	for (uint16_t i = 0; i < 256; i++) {
//		/* ASG 980324: changed to compute both tim_B_tab and timer_B_time */
//		timer_B_time[i] = clocks_to_attotime(1024 * (256 - i));
//	}
//}
// clang-format on

void ym2151_device::YM2151Operator::key_on(uint32_t key_set, uint32_t eg_count)
{
	// IMF_LOG("ym2151_device::YM2151Operator::key_on");
	if (key == 0U) {
		phase = 0;      /* clear phase */
		state = EG_ATT; /* KEY ON = attack */
		// IMF_LOG("ym2151_device - operator now in state EG_ATT");
		volume += (~volume *
		           (eg_inc[eg_sel_ar + ((eg_count >> eg_sh_ar) & 7)])) >>
		          4;
		if (volume <= MIN_ATT_INDEX) {
			volume = MIN_ATT_INDEX;
			state  = EG_DEC;
			// IMF_LOG("ym2151_device - operator now in state EG_DEC");
		}
	}
	key |= key_set;
}

void ym2151_device::YM2151Operator::key_off(uint32_t key_set)
{
	// IMF_LOG("ym2151_device::YM2151Operator::key_off");
	if (key != 0U) {
		key &= ~key_set;
		if (key == 0U) {
			if (state > EG_REL) {
				state = EG_REL; /* KEY OFF = release */
				// IMF_LOG("ym2151_device - operator now in
				// state EG_REL");
			}
		}
	}
}

void ym2151_device::envelope_KONKOFF(YM2151Operator* op, int v) const
{
	// m1, m2, c1, c2
	static constexpr uint8_t masks[4] = {0x08, 0x20, 0x10, 0x40};
	for (uint8_t i = 0; i != 4; i++) {
		if ((v & masks[i]) != 0) { /* M1 */
			op[i].key_on(1, eg_cnt);
		} else {
			op[i].key_off(1);
		}
	}
}

void ym2151_device::set_connect(YM2151Operator* om1, int cha, int v)
{
	YM2151Operator* om2 = om1 + 1;
	YM2151Operator* oc1 = om1 + 2;

	/* set connect algorithm */

	/* MEM is simply one sample delay */

	switch (v & 7) {
	case 0:
		/* M1---C1---MEM---M2---C2---OUT */
		om1->connect     = &c1;
		oc1->connect     = &mem;
		om2->connect     = &c2;
		om1->mem_connect = &m2;
		break;

	case 1:
		/* M1------+-MEM---M2---C2---OUT */
		/*      C1-+                     */
		om1->connect     = &mem;
		oc1->connect     = &mem;
		om2->connect     = &c2;
		om1->mem_connect = &m2;
		break;

	case 2:
		/* M1-----------------+-C2---OUT */
		/*      C1---MEM---M2-+          */
		om1->connect     = &c2;
		oc1->connect     = &mem;
		om2->connect     = &c2;
		om1->mem_connect = &m2;
		break;

	case 3:
		/* M1---C1---MEM------+-C2---OUT */
		/*                 M2-+          */
		om1->connect     = &c1;
		oc1->connect     = &mem;
		om2->connect     = &c2;
		om1->mem_connect = &c2;
		break;

	case 4:
		/* M1---C1-+-OUT */
		/* M2---C2-+     */
		/* MEM: not used */
		om1->connect     = &c1;
		oc1->connect     = &chanout[cha];
		om2->connect     = &c2;
		om1->mem_connect = &mem; /* store it anywhere where it will not
		                            be used */
		break;

	case 5:
		/*    +----C1----+     */
		/* M1-+-MEM---M2-+-OUT */
		/*    +----C2----+     */
		om1->connect     = nullptr; /* special mark */
		oc1->connect     = &chanout[cha];
		om2->connect     = &chanout[cha];
		om1->mem_connect = &m2;
		break;

	case 6:
		/* M1---C1-+     */
		/*      M2-+-OUT */
		/*      C2-+     */
		/* MEM: not used */
		om1->connect     = &c1;
		oc1->connect     = &chanout[cha];
		om2->connect     = &chanout[cha];
		om1->mem_connect = &mem; /* store it anywhere where it will not
		                            be used */
		break;

	case 7:
		/* M1-+     */
		/* C1-+-OUT */
		/* M2-+     */
		/* C2-+     */
		/* MEM: not used*/
		om1->connect     = &chanout[cha];
		oc1->connect     = &chanout[cha];
		om2->connect     = &chanout[cha];
		om1->mem_connect = &mem; /* store it anywhere where it will not
		                            be used */
		break;
	}
}

void ym2151_device::refresh_EG(YM2151Operator* op)
{
	uint32_t kc = 0;
	uint32_t v  = 0;

	kc = op->kc;

	/* v = 32 + 2*RATE + RKS = max 126 */

	v = kc >> op->ks;
	if ((op->ar + v) < 32 + 62) {
		op->eg_sh_ar  = eg_rate_shift[op->ar + v];
		op->eg_sel_ar = eg_rate_select[op->ar + v];
	} else {
		op->eg_sh_ar  = 0;
		op->eg_sel_ar = 17 * RATE_STEPS;
	}
	op->eg_sh_d1r  = eg_rate_shift[op->d1r + v];
	op->eg_sel_d1r = eg_rate_select[op->d1r + v];
	op->eg_sh_d2r  = eg_rate_shift[op->d2r + v];
	op->eg_sel_d2r = eg_rate_select[op->d2r + v];
	op->eg_sh_rr   = eg_rate_shift[op->rr + v];
	op->eg_sel_rr  = eg_rate_select[op->rr + v];

	op += 1;

	v = kc >> op->ks;
	if ((op->ar + v) < 32 + 62) {
		op->eg_sh_ar  = eg_rate_shift[op->ar + v];
		op->eg_sel_ar = eg_rate_select[op->ar + v];
	} else {
		op->eg_sh_ar  = 0;
		op->eg_sel_ar = 17 * RATE_STEPS;
	}
	op->eg_sh_d1r  = eg_rate_shift[op->d1r + v];
	op->eg_sel_d1r = eg_rate_select[op->d1r + v];
	op->eg_sh_d2r  = eg_rate_shift[op->d2r + v];
	op->eg_sel_d2r = eg_rate_select[op->d2r + v];
	op->eg_sh_rr   = eg_rate_shift[op->rr + v];
	op->eg_sel_rr  = eg_rate_select[op->rr + v];

	op += 1;

	v = kc >> op->ks;
	if ((op->ar + v) < 32 + 62) {
		op->eg_sh_ar  = eg_rate_shift[op->ar + v];
		op->eg_sel_ar = eg_rate_select[op->ar + v];
	} else {
		op->eg_sh_ar  = 0;
		op->eg_sel_ar = 17 * RATE_STEPS;
	}
	op->eg_sh_d1r  = eg_rate_shift[op->d1r + v];
	op->eg_sel_d1r = eg_rate_select[op->d1r + v];
	op->eg_sh_d2r  = eg_rate_shift[op->d2r + v];
	op->eg_sel_d2r = eg_rate_select[op->d2r + v];
	op->eg_sh_rr   = eg_rate_shift[op->rr + v];
	op->eg_sel_rr  = eg_rate_select[op->rr + v];

	op += 1;

	v = kc >> op->ks;
	if ((op->ar + v) < 32 + 62) {
		op->eg_sh_ar  = eg_rate_shift[op->ar + v];
		op->eg_sel_ar = eg_rate_select[op->ar + v];
	} else {
		op->eg_sh_ar  = 0;
		op->eg_sel_ar = 17 * RATE_STEPS;
	}
	op->eg_sh_d1r  = eg_rate_shift[op->d1r + v];
	op->eg_sel_d1r = eg_rate_select[op->d1r + v];
	op->eg_sh_d2r  = eg_rate_shift[op->d2r + v];
	op->eg_sel_d2r = eg_rate_select[op->d2r + v];
	op->eg_sh_rr   = eg_rate_shift[op->rr + v];
	op->eg_sel_rr  = eg_rate_select[op->rr + v];
}

/* write a register on YM2151 chip number 'n' */
void ym2151_device::write_reg(int r, int v)
{
	// IMF_LOG("ym2151_device - write to reg 0x%02x = 0x%02x", r, v);
	YM2151Operator* op = &oper[(r & 0x07) * 4 + ((r & 0x18) >> 3)];

	/* adjust bus to 8 bits */
	r &= 0xff;
	v &= 0xff;

#if 0
	/* There is no info on what YM2151 really does when busy flag is set */
	if (status & 0x80) return;
	timer_set(attotime::from_hz(clock()) * 64, chip, 0, timer_callback_chip_busy);
	status |= 0x80;   /* set busy flag for 64 chip clock cycles */
#endif

	switch (r & 0xe0) {
	case 0x00:
		switch (r) {
		case 0x09: /* LFO reset(bit 1), Test Register (other bits) */
			test = static_cast<uint8_t>(v);
			if ((v & 2) != 0) {
				lfo_phase = 0;
			}
			break;

		case 0x08: envelope_KONKOFF(&oper[(v & 7) * 4], v); break;

		case 0x0f: /* noise mode enable, noise period */
			noise   = v;
			noise_f = noise_tab[v & 0x1f];
			break;

		case 0x10: /* timer A hi */
			timer_A_index = (timer_A_index & 0x003) | (v << 2);
			break;

		case 0x11: /* timer A low */
			timer_A_index = (timer_A_index & 0x3fc) | (v & 3);
			break;

		case 0x12: /* timer B */ timer_B_index = v; break;

		case 0x14: /* CSM, irq flag reset, irq enable, timer start/stop */

			irq_enable = v; /* bit 3-timer B, bit 2-timer A, bit 7 -
			                   CSM */

			// clang-format off
			//if (v & 0x10) /* reset timer A irq flag */
			//{
			//	status &= ~1;
			//	timer_A_irq_off->adjust(attotime::zero);
			//}

			//if (v & 0x20) /* reset timer B irq flag */
			//{
			//	status &= ~2;
			//	timer_B_irq_off->adjust(attotime::zero);
			//}

			//if (v & 0x02) {   /* load and start timer B */
			//	/* ASG 980324: added a real timer */
			//	/* start timer _only_ if it wasn't already started (it will reload time value next round) */
			//	if (!timer_B->enable(true)) {
			//		timer_B->adjust(timer_B_time[timer_B_index]);
			//		timer_B_index_old = timer_B_index;
			//	}
			//} else {       /* stop timer B */
			//	timer_B->enable(false);
			//}

			//if (v & 0x01) {   /* load and start timer A */
			//	/* ASG 980324: added a real timer */
			//	/* start timer _only_ if it wasn't already started (it will reload time value next round) */
			//	if (!timer_A->enable(true)) {
			//		timer_A->adjust(timer_A_time[timer_A_index]);
			//		timer_A_index_old = timer_A_index;
			//	}
			//} else {       /* stop timer A */
			//	/* ASG 980324: added a real timer */
			//	timer_A->enable(false);
			//}
			// clang-format on
			break;

		case 0x18: /* LFO frequency */
		{
			lfo_overflow = (1 << ((15 - (v >> 4)) + 3)) * (1 << LFO_SH);
			lfo_counter_add = 0x10 + (v & 0x0f);
		} break;

		case 0x19: /* PMD (bit 7==1) or AMD (bit 7==0) */
			if ((v & 0x80) != 0) {
				pmd = v & 0x7f;
			} else {
				amd = v & 0x7f;
			}
			break;

		case 0x1b: /* CT2, CT1, LFO waveform */
			ct       = static_cast<uint8_t>(v >> 6);
			lfo_wsel = v & 3;
			// m_portwritehandler(0, ct, 0xff);
			break;

		default:
			IMF_LOG("YM2151 Write %02x to undocumented register #%02x\n",
			        v,
			        r);
			break;
		}
		break;

	case 0x20:
		op = &oper[(r & 7) * 4];
		switch (r & 0x18) {
		case 0x00: /* RL enable, Feedback, Connection */
			op->fb_shift = ((v >> 3) & 7) != 0 ? ((v >> 3) & 7) + 6 : 0;
			pan[(r & 7) * 2]     = (v & 0x40) != 0 ? ~0 : 0;
			pan[(r & 7) * 2 + 1] = (v & 0x80) != 0 ? ~0 : 0;
			connect[r & 7]       = v & 7;
			set_connect(op, r & 7, v & 7);
			break;

		case 0x08: /* Key Code */
			v &= 0x7f;
			if ((uint8_t)(v) != op->kc) {
				uint32_t kc         = 0;
				uint32_t kc_channel = 0;

				kc_channel = (v - (v >> 2)) * 64;
				kc_channel += 768;
				kc_channel |= (op->kc_i & 63);

				(op + 0)->kc   = v;
				(op + 0)->kc_i = kc_channel;
				(op + 1)->kc   = v;
				(op + 1)->kc_i = kc_channel;
				(op + 2)->kc   = v;
				(op + 2)->kc_i = kc_channel;
				(op + 3)->kc   = v;
				(op + 3)->kc_i = kc_channel;

				kc = v >> 2;

				(op + 0)->dt1 = dt1_freq[(op + 0)->dt1_i + kc];
				(op + 0)->freq = ((freq[kc_channel + (op + 0)->dt2] +
				                   (op + 0)->dt1) *
				                  (op + 0)->mul) >>
				                 1;

				(op + 1)->dt1 = dt1_freq[(op + 1)->dt1_i + kc];
				(op + 1)->freq = ((freq[kc_channel + (op + 1)->dt2] +
				                   (op + 1)->dt1) *
				                  (op + 1)->mul) >>
				                 1;

				(op + 2)->dt1 = dt1_freq[(op + 2)->dt1_i + kc];
				(op + 2)->freq = ((freq[kc_channel + (op + 2)->dt2] +
				                   (op + 2)->dt1) *
				                  (op + 2)->mul) >>
				                 1;

				(op + 3)->dt1 = dt1_freq[(op + 3)->dt1_i + kc];
				(op + 3)->freq = ((freq[kc_channel + (op + 3)->dt2] +
				                   (op + 3)->dt1) *
				                  (op + 3)->mul) >>
				                 1;

				refresh_EG(op);
			}
			break;

		case 0x10: /* Key Fraction */
			v >>= 2;
			if (static_cast<uint8_t>(v) != (op->kc_i & 63)) {
				uint32_t kc_channel = 0;

				kc_channel = v;
				kc_channel |= (op->kc_i & ~63);

				(op + 0)->kc_i = kc_channel;
				(op + 1)->kc_i = kc_channel;
				(op + 2)->kc_i = kc_channel;
				(op + 3)->kc_i = kc_channel;

				(op + 0)->freq = ((freq[kc_channel + (op + 0)->dt2] +
				                   (op + 0)->dt1) *
				                  (op + 0)->mul) >>
				                 1;
				(op + 1)->freq = ((freq[kc_channel + (op + 1)->dt2] +
				                   (op + 1)->dt1) *
				                  (op + 1)->mul) >>
				                 1;
				(op + 2)->freq = ((freq[kc_channel + (op + 2)->dt2] +
				                   (op + 2)->dt1) *
				                  (op + 2)->mul) >>
				                 1;
				(op + 3)->freq = ((freq[kc_channel + (op + 3)->dt2] +
				                   (op + 3)->dt1) *
				                  (op + 3)->mul) >>
				                 1;
			}
			break;

		case 0x18: /* PMS, AMS */
			op->pms = (v >> 4) & 7;
			op->ams = (v & 3);
			break;
		}
		break;

	case 0x40: /* DT1, MUL */
	{
		const uint32_t olddt1_i = op->dt1_i;
		const uint32_t oldmul   = op->mul;

		op->dt1_i = (v & 0x70) << 1;
		op->mul   = (v & 0x0f) != 0 ? (v & 0x0f) << 1 : 1;

		if (olddt1_i != op->dt1_i) {
			op->dt1 = dt1_freq[op->dt1_i + (op->kc >> 2)];
		}

		if ((olddt1_i != op->dt1_i) || (oldmul != op->mul)) {
			op->freq = ((freq[op->kc_i + op->dt2] + op->dt1) * op->mul) >>
			           1;
		}
	} break;

	case 0x60:                                     /* TL */
		op->tl = (v & 0x7f) << (ENV_BITS - 7); /* 7bit TL */
		break;

	case 0x80: /* KS, AR */
	{
		const uint32_t oldks = op->ks;
		const uint32_t oldar = op->ar;

		op->ks = 5 - (v >> 6);
		op->ar = (v & 0x1f) != 0 ? 32 + ((v & 0x1f) << 1) : 0;

		if ((op->ar != oldar) || (op->ks != oldks)) {
			if ((op->ar + (op->kc >> op->ks)) < 32 + 62) {
				op->eg_sh_ar = eg_rate_shift[op->ar + (op->kc >> op->ks)];
				op->eg_sel_ar =
				        eg_rate_select[op->ar + (op->kc >> op->ks)];
			} else {
				op->eg_sh_ar  = 0;
				op->eg_sel_ar = 17 * RATE_STEPS;
			}
		}

		if (op->ks != oldks) {
			op->eg_sh_d1r = eg_rate_shift[op->d1r + (op->kc >> op->ks)];
			op->eg_sel_d1r = eg_rate_select[op->d1r + (op->kc >> op->ks)];
			op->eg_sh_d2r = eg_rate_shift[op->d2r + (op->kc >> op->ks)];
			op->eg_sel_d2r = eg_rate_select[op->d2r + (op->kc >> op->ks)];
			op->eg_sh_rr = eg_rate_shift[op->rr + (op->kc >> op->ks)];
			op->eg_sel_rr = eg_rate_select[op->rr + (op->kc >> op->ks)];
		}
	} break;

	case 0xa0: /* LFO AM enable, D1R */
		op->AMmask     = (v & 0x80) != 0 ? ~0 : 0;
		op->d1r        = (v & 0x1f) != 0 ? 32 + ((v & 0x1f) << 1) : 0;
		op->eg_sh_d1r  = eg_rate_shift[op->d1r + (op->kc >> op->ks)];
		op->eg_sel_d1r = eg_rate_select[op->d1r + (op->kc >> op->ks)];
		break;

	case 0xc0: /* DT2, D2R */
	{
		const uint32_t olddt2 = op->dt2;
		op->dt2               = dt2_tab[v >> 6];
		if (op->dt2 != olddt2) {
			op->freq = ((freq[op->kc_i + op->dt2] + op->dt1) * op->mul) >>
			           1;
		}
	}
		op->d2r        = (v & 0x1f) != 0 ? 32 + ((v & 0x1f) << 1) : 0;
		op->eg_sh_d2r  = eg_rate_shift[op->d2r + (op->kc >> op->ks)];
		op->eg_sel_d2r = eg_rate_select[op->d2r + (op->kc >> op->ks)];
		break;

	case 0xe0: /* D1L, RR */
		op->d1l       = d1l_tab[v >> 4];
		op->rr        = 34 + ((v & 0x0f) << 2);
		op->eg_sh_rr  = eg_rate_shift[op->rr + (op->kc >> op->ks)];
		op->eg_sel_rr = eg_rate_select[op->rr + (op->kc >> op->ks)];
		break;
	}
}

void ym2151_device::device_post_load()
{
	for (uint8_t j = 0; j < 8; j++) {
		set_connect(&oper[j * 4], j, connect[j]);
	}
}

void ym2151_device::device_start()
{
	init_tables();

	// clang-format off
	//m_irqhandler.resolve_safe();
	//m_portwritehandler.resolve_safe();

	//m_stream = stream_alloc(0, 2, clock() / 64);

	//timer_A_irq_off = timer_alloc(TIMER_IRQ_A_OFF);
	//timer_B_irq_off = timer_alloc(TIMER_IRQ_B_OFF);
	//timer_A = timer_alloc(TIMER_A);
	//timer_B = timer_alloc(TIMER_B);
	// clang-format on

	lfo_timer_add = 1 << LFO_SH;

	eg_timer_add      = 1 << EG_SH;
	eg_timer_overflow = 3 * eg_timer_add;

	// clang-format off
	/* save all 32 operators */
	//for (uint8_t j = 0; j < 32; j++) {
	//	YM2151Operator &op = oper[(j & 7) * 4 + (j >> 3)];

	//	save_item(NAME(op.phase), j);
	//	save_item(NAME(op.freq), j);
	//	save_item(NAME(op.dt1), j);
	//	save_item(NAME(op.mul), j);
	//	save_item(NAME(op.dt1_i), j);
	//	save_item(NAME(op.dt2), j);
	//	/* operators connection is saved in chip data block */
	//	save_item(NAME(op.mem_value), j);

	//	save_item(NAME(op.fb_shift), j);
	//	save_item(NAME(op.fb_out_curr), j);
	//	save_item(NAME(op.fb_out_prev), j);
	//	save_item(NAME(op.kc), j);
	//	save_item(NAME(op.kc_i), j);
	//	save_item(NAME(op.pms), j);
	//	save_item(NAME(op.ams), j);
	//	save_item(NAME(op.AMmask), j);

	//	save_item(NAME(op.state), j);
	//	save_item(NAME(op.eg_sh_ar), j);
	//	save_item(NAME(op.eg_sel_ar), j);
	//	save_item(NAME(op.tl), j);
	//	save_item(NAME(op.volume), j);
	//	save_item(NAME(op.eg_sh_d1r), j);
	//	save_item(NAME(op.eg_sel_d1r), j);
	//	save_item(NAME(op.d1l), j);
	//	save_item(NAME(op.eg_sh_d2r), j);
	//	save_item(NAME(op.eg_sel_d2r), j);
	//	save_item(NAME(op.eg_sh_rr), j);
	//	save_item(NAME(op.eg_sel_rr), j);

	//	save_item(NAME(op.key), j);
	//	save_item(NAME(op.ks), j);
	//	save_item(NAME(op.ar), j);
	//	save_item(NAME(op.d1r), j);
	//	save_item(NAME(op.d2r), j);
	//	save_item(NAME(op.rr), j);

	//	save_item(NAME(op.reserved0), j);
	//	save_item(NAME(op.reserved1), j);
	//}

	//save_item(NAME(pan));

	//save_item(NAME(eg_cnt));
	//save_item(NAME(eg_timer));
	//save_item(NAME(eg_timer_add));
	//save_item(NAME(eg_timer_overflow));

	//save_item(NAME(lfo_phase));
	//save_item(NAME(lfo_timer));
	//save_item(NAME(lfo_timer_add));
	//save_item(NAME(lfo_overflow));
	//save_item(NAME(lfo_counter));
	//save_item(NAME(lfo_counter_add));
	//save_item(NAME(lfo_wsel));
	//save_item(NAME(amd));
	//save_item(NAME(pmd));
	//save_item(NAME(lfa));
	//save_item(NAME(lfp));

	//save_item(NAME(test));
	//save_item(NAME(ct));

	//save_item(NAME(noise));
	//save_item(NAME(noise_rng));
	//save_item(NAME(noise_p));
	//save_item(NAME(noise_f));

	//save_item(NAME(csm_req));
	//save_item(NAME(irq_enable));
	//save_item(NAME(status));

	//save_item(NAME(timer_A_index));
	//save_item(NAME(timer_B_index));
	//save_item(NAME(timer_A_index_old));
	//save_item(NAME(timer_B_index_old));

	//save_item(NAME(irqlinestate));

	//save_item(NAME(connect));

	//save_item(NAME(m_reset_active));
	// clang-format on
}

// clang-format off
//void ym2151_device::device_clock_changed() {
//	m_stream->set_sample_rate(clock() / 64);
//	calculate_timers();
//}
// clang-format on

int ym2151_device::op_calc(YM2151Operator* OP, unsigned int env, signed int pm)
{
	uint32_t p = 0;

	p = (env << 3) +
	    sin_tab[(((signed int)((OP->phase & ~FREQ_MASK) + left_shift_signed(pm, 15))) >> FREQ_SH) &
	            SIN_MASK];

	if (p >= TL_TAB_LEN) {
		return 0;
	}

	return tl_tab[p];
}

int ym2151_device::op_calc1(YM2151Operator* OP, unsigned int env, signed int pm)
{
	uint32_t p = 0;
	int32_t i  = 0;

	i = (OP->phase & ~FREQ_MASK) + pm;

	/*logerror("i=%08x (i>>16)&511=%8i phase=%i [pm=%08x] ",i, (i>>16)&511,
	 * OP->phase>>FREQ_SH, pm);*/

	p = (env << 3) + sin_tab[(i >> FREQ_SH) & SIN_MASK];

	/*logerror("(p&255=%i p>>8=%i) out= %i\n", p&255,p>>8,
	 * tl_tab[p&255]>>(p>>8) );*/

	if (p >= TL_TAB_LEN) {
		return 0;
	}

	return tl_tab[p];
}

#define volume_calc(OP) \
	((OP)->tl + ((uint32_t)(OP)->volume) + (AM & (OP)->AMmask))

void ym2151_device::chan_calc(const uint8_t chan)
{
	YM2151Operator* op = nullptr;
	unsigned int env   = 0;
	uint32_t AM        = 0;

	m2 = c1 = c2 = mem = 0;
	op                 = &oper[chan * 4]; /* M1 */

	*op->mem_connect = op->mem_value; /* restore delayed sample (MEM) value
	                                     to m2 or c2 */

	if (op->ams != 0U) {
		AM = lfa << (op->ams - 1);
	}
	env = volume_calc(op);
	{
		int32_t out     = op->fb_out_prev + op->fb_out_curr;
		op->fb_out_prev = op->fb_out_curr;

		if (op->connect == nullptr) {
			/* algorithm 5 */
			mem = c1 = c2 = op->fb_out_prev;
		} else {
			/* other algorithms */
			*op->connect = op->fb_out_prev;
		}

		op->fb_out_curr = 0;
		if (env < ENV_QUIET) {
			if (op->fb_shift == 0U) {
				out = 0;
			}
			op->fb_out_curr = op_calc1(
			        op, env, left_shift_signed(out, op->fb_shift));
		}
	}

	env = volume_calc(op + 1); /* M2 */
	if (env < ENV_QUIET) {
		*(op + 1)->connect += op_calc(op + 1, env, m2);
	}

	env = volume_calc(op + 2); /* C1 */
	if (env < ENV_QUIET) {
		*(op + 2)->connect += op_calc(op + 2, env, c1);
	}

	env = volume_calc(op + 3); /* C2 */
	if (env < ENV_QUIET) {
		chanout[chan] += op_calc(op + 3, env, c2);
	}
	//  if(chan==3) printf("%d\n", chanout[chan]);

	/* M1 */
	op->mem_value = mem;
}

void ym2151_device::chan7_calc()
{
	YM2151Operator* op = nullptr;
	unsigned int env   = 0;
	uint32_t AM        = 0;

	m2 = c1 = c2 = mem = 0;
	op                 = &oper[7 * 4]; /* M1 */

	*op->mem_connect = op->mem_value; /* restore delayed sample (MEM) value
	                                     to m2 or c2 */

	if (op->ams != 0U) {
		AM = lfa << (op->ams - 1);
	}
	env = volume_calc(op);
	{
		int32_t out     = op->fb_out_prev + op->fb_out_curr;
		op->fb_out_prev = op->fb_out_curr;

		if (op->connect == nullptr) {
			/* algorithm 5 */
			mem = c1 = c2 = op->fb_out_prev;
		} else {
			/* other algorithms */
			*op->connect = op->fb_out_prev;
		}

		op->fb_out_curr = 0;
		if (env < ENV_QUIET) {
			if (op->fb_shift == 0U) {
				out = 0;
			}
			op->fb_out_curr = op_calc1(
			        op, env, left_shift_signed(out, op->fb_shift));
		}
	}

	env = volume_calc(op + 1); /* M2 */
	if (env < ENV_QUIET) {
		*(op + 1)->connect += op_calc(op + 1, env, m2);
	}

	env = volume_calc(op + 2); /* C1 */
	if (env < ENV_QUIET) {
		*(op + 2)->connect += op_calc(op + 2, env, c1);
	}

	env = volume_calc(op + 3); /* C2 */
	if ((noise & 0x80) != 0U) {
		if (env < 0x3ff) {
			const auto noise_amplitude = static_cast<int16_t>(2 * (0x3ff - env));
			const int8_t noise_direction = (noise_rng & 0x10000) ? 1 : -1;

			// Calculate the value of the noise signal
			const int16_t noise_val = noise_amplitude * noise_direction;

			// The range of the YM2151 noise output is -2044 to 2040
			constexpr int16_t noise_min = -2044;
			constexpr int16_t noise_max = 2040;
			chanout[7] += std::clamp(noise_val, noise_min, noise_max);
		}
	} else {
		if (env < ENV_QUIET) {
			chanout[7] += op_calc(op + 3, env, c2);
		}
	}
	/* M1 */
	op->mem_value = mem;
}

void ym2151_device::advance_eg()
{
	YM2151Operator* op = nullptr;
	unsigned int i     = 0;

	eg_timer += eg_timer_add;

	while (eg_timer >= eg_timer_overflow) {
		eg_timer -= eg_timer_overflow;

		eg_cnt++;

		/* envelope generator */
		op = &oper[0]; /* CH 0 M1 */
		i  = 32;
		do {
			switch (op->state) {
			case EG_ATT: /* attack phase */
				if ((eg_cnt & ((1 << op->eg_sh_ar) - 1)) == 0U) {
					op->volume +=
					        (~op->volume *
					         (eg_inc[op->eg_sel_ar +
					                 ((eg_cnt >> op->eg_sh_ar) & 7)])) >>
					        4;

					if (op->volume <= MIN_ATT_INDEX) {
						op->volume = MIN_ATT_INDEX;
						op->state  = EG_DEC;
						// IMF_LOG("ym2151_device -
						// operator %i now in state
						// EG_DEC", i);
					}
				}
				break;

			case EG_DEC: /* decay phase */
				if ((eg_cnt & ((1 << op->eg_sh_d1r) - 1)) == 0U) {
					op->volume += eg_inc[op->eg_sel_d1r +
					                     ((eg_cnt >> op->eg_sh_d1r) & 7)];

					if ((uint32_t)(op->volume) >=
					    op->d1l) {
						op->state = EG_SUS;
						// IMF_LOG("ym2151_device -
						// operator %i now in state
						// EG_SUS", i);
					}
				}
				break;

			case EG_SUS: /* sustain phase */
				if ((eg_cnt & ((1 << op->eg_sh_d2r) - 1)) == 0U) {
					op->volume += eg_inc[op->eg_sel_d2r +
					                     ((eg_cnt >> op->eg_sh_d2r) & 7)];

					if (op->volume >= MAX_ATT_INDEX) {
						op->volume = MAX_ATT_INDEX;
						op->state  = EG_OFF;
						// IMF_LOG("ym2151_device -
						// operator %i now in state
						// EG_OFF", i);
					}
				}
				break;

			case EG_REL: /* release phase */
				if ((eg_cnt & ((1 << op->eg_sh_rr) - 1)) == 0U) {
					op->volume += eg_inc[op->eg_sel_rr +
					                     ((eg_cnt >> op->eg_sh_rr) & 7)];

					if (op->volume >= MAX_ATT_INDEX) {
						op->volume = MAX_ATT_INDEX;
						op->state  = EG_OFF;
						// IMF_LOG("ym2151_device -
						// operator %i now in state
						// EG_OFF", i);
					}
				}
				break;
			}
			op++;
			i--;
		} while (i != 0U);
	}
}

void ym2151_device::advance()
{
	YM2151Operator* op = nullptr;
	unsigned int i     = 0;
	int a              = 0;
	int p              = 0;

	/* LFO */
	if ((test & 2) != 0) {
		lfo_phase = 0;
	} else {
		lfo_timer += lfo_timer_add;
		if (lfo_timer >= lfo_overflow) {
			lfo_timer -= lfo_overflow;
			lfo_counter += lfo_counter_add;
			lfo_phase += (lfo_counter >> 4);
			lfo_phase &= 255;
			lfo_counter &= 15;
		}
	}

	i = lfo_phase;
	/* calculate LFO AM and PM waveform value (all verified on real chip,
	 * except for noise algorithm which is impossible to analyse)*/
	switch (lfo_wsel) {
	case 0:
		/* saw */
		/* AM: 255 down to 0 */
		/* PM: 0 to 127, -127 to 0 (at PMD=127: LFP = 0 to 126, -126 to
		 * 0) */
		a = 255 - i;
		if (i < 128) {
			p = i;
		} else {
			p = i - 255;
		}
		break;
	case 1:
		/* square */
		/* AM: 255, 0 */
		/* PM: 128,-128 (LFP = exactly +PMD, -PMD) */
		if (i < 128) {
			a = 255;
			p = 128;
		} else {
			a = 0;
			p = -128;
		}
		break;
	case 2:
		/* triangle */
		/* AM: 255 down to 1 step -2; 0 up to 254 step +2 */
		/* PM: 0 to 126 step +2, 127 to 1 step -2, 0 to -126 step -2,
		 * -127 to -1 step +2*/
		if (i < 128) {
			a = 255 - (i * 2);
		} else {
			a = (i * 2) - 256;
		}

		if (i < 64) {            /* i = 0..63 */
			p = i * 2;       /* 0 to 126 step +2 */
		} else if (i < 128) {    /* i = 64..127 */
			p = 255 - i * 2; /* 127 to 1 step -2 */
		} else if (i < 192) {    /* i = 128..191 */
			p = 256 - i * 2; /* 0 to -126 step -2*/
		} else {                 /* i = 192..255 */
			p = i * 2 - 511; /*-127 to -1 step +2*/
		}
		break;
	case 3:
	default: /*keep the compiler happy*/
		/* random */
		/* the real algorithm is unknown !!!
		        We just use a snapshot of data from real chip */

		/* AM: range 0 to 255    */
		/* PM: range -128 to 127 */

		a = lfo_noise_waveform[i];
		p = a - 128;
		break;
	}
	lfa = a * amd / 128;
	lfp = p * pmd / 128;

	/*  The Noise Generator of the YM2151 is 17-bit shift register.
	 *   Input to the bit16 is negated (bit0 XOR bit3) (EXNOR).
	 *   Output of the register is negated (bit0 XOR bit3).
	 *   Simply use bit16 as the noise output.
	 */
	noise_p += noise_f;
	i = (noise_p >> 16); /* number of events (shifts of the shift register) */
	noise_p &= 0xffff;
	while (i != 0U) {
		uint32_t j = 0;
		j          = ((noise_rng ^ (noise_rng >> 3)) & 1) ^ 1;
		noise_rng  = (j << 16) | (noise_rng >> 1);
		i--;
	}

	/* phase generator */
	op = &oper[0]; /* CH 0 M1 */
	i  = 8;
	do {
		if (op->pms != 0U) /* only when phase modulation from LFO is
		                      enabled for this channel */
		{
			int32_t mod_ind = lfp; /* -128..+127 (8bits signed) */
			if (op->pms < 6) {
				mod_ind >>= (6 - op->pms);
			} else {
				mod_ind <<= (op->pms - 5);
			}

			if (mod_ind != 0) {
				const uint32_t kc_channel = op->kc_i + mod_ind;
				(op + 0)->phase += ((freq[kc_channel + (op + 0)->dt2] +
				                     (op + 0)->dt1) *
				                    (op + 0)->mul) >>
				                   1;
				(op + 1)->phase += ((freq[kc_channel + (op + 1)->dt2] +
				                     (op + 1)->dt1) *
				                    (op + 1)->mul) >>
				                   1;
				(op + 2)->phase += ((freq[kc_channel + (op + 2)->dt2] +
				                     (op + 2)->dt1) *
				                    (op + 2)->mul) >>
				                   1;
				(op + 3)->phase += ((freq[kc_channel + (op + 3)->dt2] +
				                     (op + 3)->dt1) *
				                    (op + 3)->mul) >>
				                   1;
			} else /* phase modulation from LFO is equal to zero */
			{
				(op + 0)->phase += (op + 0)->freq;
				(op + 1)->phase += (op + 1)->freq;
				(op + 2)->phase += (op + 2)->freq;
				(op + 3)->phase += (op + 3)->freq;
			}
		} else /* phase modulation from LFO is disabled */
		{
			(op + 0)->phase += (op + 0)->freq;
			(op + 1)->phase += (op + 1)->freq;
			(op + 2)->phase += (op + 2)->freq;
			(op + 3)->phase += (op + 3)->freq;
		}

		op += 4;
		i--;
	} while (i != 0U);

	/* CSM is calculated *after* the phase generator calculations (verified
	 * on real chip) CSM keyon line seems to be ORed with the KO line inside
	 * of the chip. The result is that it only works when KO (register 0x08)
	 * is off, ie. 0
	 *
	 * Interesting effect is that when timer A is set to 1023, the KEY ON
	 * happens on every sample, so there is no KEY OFF at all - the result
	 * is that the sound played is the same as after normal KEY ON.
	 */

	if (csm_req != 0U) /* CSM KEYON/KEYOFF seqeunce request */
	{
		if (csm_req == 2) /* KEY ON */
		{
			op = &oper[0]; /* CH 0 M1 */
			i  = 32;
			do {
				op->key_on(2, eg_cnt);
				op++;
				i--;
			} while (i != 0U);
			csm_req = 1;
		} else /* KEY OFF */
		{
			op = &oper[0]; /* CH 0 M1 */
			i  = 32;
			do {
				op->key_off(2);
				op++;
				i--;
			} while (i != 0U);
			csm_req = 0;
		}
	}
}

//-------------------------------------------------
//  ym2151_device - constructor
//-------------------------------------------------

ym2151_device::ym2151_device()
{
	device_start();
	device_post_load();
	device_reset();

	ms_per_render = 1000.0 / 44100.0;
}

//-------------------------------------------------
//  ym2151_device - destructor
//-------------------------------------------------

ym2151_device::~ym2151_device()
{
	// Deregister the mixer channel, after which it's cleaned up
	//assert(audio_channel);
	//MIXER_DeregisterChannel(audio_channel);
}

//-------------------------------------------------
//  read - read from the device
//-------------------------------------------------

uint8_t ym2151_device::read(const offs_t offset) const
{
	if ((offset & 1) != 0) {
		// m_stream->update();
		return status;
	}
	return 0xff; /* confirmed on a real YM2151 */
}

//-------------------------------------------------
//  write - write from the device
//-------------------------------------------------

void ym2151_device::write(const offs_t offset, const uint8_t data)
{
	RenderUpToNow();
	if ((offset & 1) != 0) {
		if (!m_reset_active) {
			// m_stream->update();
			write_reg(m_lastreg, data);
		}
	} else {
		m_lastreg = data;
	}
}

uint8_t ym2151_device::status_r() const
{
	return read(1);
}

void ym2151_device::register_w(const uint8_t data)
{
	write(0, data);
}

void ym2151_device::data_w(const uint8_t data)
{
	write(1, data);
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void ym2151_device::device_reset()
{
	int i = 0;
	/* initialize hardware registers */
	for (auto& op : oper) {
		op = {};
		assert(op.volume == MAX_ATT_INDEX);
		assert(op.kc_i == 768); // min kc_i value
	}

	eg_timer = 0;
	eg_cnt   = 0;

	lfo_timer   = 0;
	lfo_counter = 0;
	lfo_phase   = 0;
	lfo_wsel    = 0;
	pmd         = 0;
	amd         = 0;
	lfa         = 0;
	lfp         = 0;

	test = 0;

	irq_enable = 0;
	/* ASG 980324 -- reset the timers before writing to the registers */
	// timer_A->enable(false);
	// timer_B->enable(false);
	timer_A_index     = 0;
	timer_B_index     = 0;
	timer_A_index_old = 0;
	timer_B_index_old = 0;

	noise     = 0;
	noise_rng = 0;
	noise_p   = 0;
	noise_f   = noise_tab[0];

	csm_req = 0;
	status  = 0;

	write_reg(0x1b, 0); /* only because of CT1, CT2 output pins */
	write_reg(0x18, 0); /* set LFO frequency */
	for (i = 0x20; i < 0x100; i++) /* set the operators */
	{
		write_reg(i, 0);
	}
}

// clang-format off
//-------------------------------------------------
//  reset_w - handle writes to the reset lines of
//  the YM2151 and its associated DAC
//-------------------------------------------------

//WRITE_LINE_MEMBER(ym2151_device::reset_w) {
//	// active low reset
//	if (!m_reset_active && !state)
//		reset();
//
//	m_reset_active = !state;
//}
// clang-format on

AudioFrame ym2151_device::RenderFrame()
{
	advance_eg();

	// Reset the channels before calculating
	std::fill(std::begin(chanout), std::end(chanout), 0);

	for (uint8_t ch = 0; ch < 7; ch++) {
		chan_calc(ch);
	}
	chan7_calc();

	int outl = 0;
	int outr = 0;
	for (uint8_t ch = 0; ch < 8; ch++) {
		outl += chanout[ch] & pan[2 * ch];
		outr += chanout[ch] & pan[2 * ch + 1];
	}
	advance();
	return {static_cast<float>(outl), static_cast<float>(outr)};
}

void ym2151_device::RenderUpToNow()
{
	const auto now = PIC_FullIndex();
	// Keep rendering until we're current
	while (last_rendered_ms < now) {
		last_rendered_ms += ms_per_render;
		fifo.emplace(RenderFrame());
	}
}
//-------------------------------------------------
//  sound_stream_update - handle a stream update
//-------------------------------------------------

void ym2151_device::sound_stream_update(const uint16_t requested_frames, int32_t* buffer)
{
	uint32_t i = 0;

	if (m_reset_active) {
		IMF_LOG("ym2151_device - sending silence", "");
		return;
	}
	// if (fifo.size())
	//	LOG_MSG("IMFC: Queued %2lu cycle-accurate frames", fifo.size());

	auto frames_remaining = requested_frames;

	// First, send any frames we've queued since the last callback
	while (frames_remaining && fifo.size()) {
		buffer[(i * 2)] += fifo.front().outl * 2.0;
		buffer[(i * 2) + 1] += fifo.front().outr * 2.0;
		i++;
		fifo.pop();
		--frames_remaining;
	}
	// If the queue's run dry, render the remainder and sync-up our time datum
	while (frames_remaining) {
		const auto frame = RenderFrame();
		buffer[(i * 2)] += frame.outl * 2.0;
		buffer[(i * 2) + 1] += frame.outr * 2.0;
		i++;
		--frames_remaining;
	}
	last_rendered_ms = PIC_FullIndex();
}

// clang-format off
//void ym2151_device::device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr) {
//	switch (id) {
//	case TIMER_IRQ_A_OFF: {
//		int old = irqlinestate;
//		irqlinestate &= ~1;
//		if (old && !irqlinestate)
//			m_irqhandler(0);
//		break;
//	}
//
//	case TIMER_IRQ_B_OFF: {
//		int old = irqlinestate;
//		irqlinestate &= ~2;
//		if (old && !irqlinestate)
//			m_irqhandler(0);
//		break;
//	}
//
//	case TIMER_A: {
//		timer_A->adjust(timer_A_time[timer_A_index]);
//		timer_A_index_old = timer_A_index;
//		if (irq_enable & 0x80)
//			csm_req = 2;      /* request KEY ON / KEY OFF sequence */
//		if (irq_enable & 0x04) {
//			status |= 1;
//			int old = irqlinestate;
//			irqlinestate |= 1;
//			if (!old)
//				m_irqhandler(1);
//		}
//		break;
//	}
//
//	case TIMER_B: {
//		timer_B->adjust(timer_B_time[timer_B_index]);
//		timer_B_index_old = timer_B_index;
//		if (irq_enable & 0x08) {
//			status |= 2;
//			int old = irqlinestate;
//			irqlinestate |= 2;
//			if (!old)
//				m_irqhandler(1);
//		}
//		break;
//	}
//	}
//}
// clang-format on

class PD71051 {
private:
	enum State : uint8_t {
		WAITING_FOR_WRITE_MODE,
		WAITING_FOR_SYNC_CHAR1,
		WAITING_FOR_SYNC_CHAR2,
		NORMAL_OPERATION,
	};
	State m_state{WAITING_FOR_WRITE_MODE};
	uint8_t m_mode = 0;

	void setState(State newState)
	{
		m_state = newState;

		/* Switch is only used for state debugging
		switch (m_state) {
		case WAITING_FOR_WRITE_MODE:
			// IMF_LOG("Midi State now WAITING_FOR_WRITE_MODE");
			break;
		case WAITING_FOR_SYNC_CHAR1:
			// IMF_LOG("Midi State now WAITING_FOR_SYNC_CHAR1");
			break;
		case WAITING_FOR_SYNC_CHAR2:
			// IMF_LOG("Midi State now WAITING_FOR_SYNC_CHAR2");
			break;
		case NORMAL_OPERATION:
			// IMF_LOG("Midi State now NORMAL_OPERATION");
			break;
		}
		*/
	}

public:
	PD71051() = default;
	// MIDI_PORT_1 = 0x10
	void writePort1([[maybe_unused]] uint8_t value)
	{
		// This doesn't do anything for now
	}

	static uint8_t readPort1()
	{
		uint8_t ret;
		ret = midiin_queue[midiin_r];
		if (midiin_r != midiin_w) {
			midiin_r++;
			midiin_r &= 0xff;
		}
		return ret;
	}

	// MIDI_PORT_2 = 0x11
	void writePort2(uint8_t value)
	{
		switch (m_state) {
		case WAITING_FOR_WRITE_MODE:
			m_mode = value;
			switch (m_mode & 0x03) {
			case 0x00:
				// synchronous mode
				setState((m_mode & 0x80) != 0
				                 ? WAITING_FOR_SYNC_CHAR2
				                 : WAITING_FOR_SYNC_CHAR1);
				break;
			case 0x01:
			case 0x02:
			case 0x03: setState(NORMAL_OPERATION);
			}
			break;
		case WAITING_FOR_SYNC_CHAR1:
			setState(WAITING_FOR_SYNC_CHAR2);
			break;
		case WAITING_FOR_SYNC_CHAR2: setState(NORMAL_OPERATION); break;
		case NORMAL_OPERATION:
			// command mode
			/* -- unused ---
			const bool transmitEnable     = (value & 0x01) != 0;
			const bool notDTR_pin_control = (value & 0x02) != 0;
			const bool receiveEnable      = (value & 0x04) != 0;
			const bool sendBreak          = (value & 0x08) != 0;
			const bool errorClear         = (value & 0x10) != 0;
			const bool notRTS_pin_control = (value & 0x20) != 0;
			const bool softwareReset      = (value & 0x40) != 0;
			const bool enterHuntPhase     = (value & 0x80) != 0;
			------- */

			const bool softwareReset = (value & 0x40) != 0;
			if (softwareReset) {
				setState(WAITING_FOR_WRITE_MODE);
			}
			break;
		}
	}

	uint8_t readPort2()
	{
		if (m_state == NORMAL_OPERATION) {
			// clang-format off
			/*
				bit0: TxRDY
				bit1: RxRDY (Same as the output pin function with the same name)
				bit2: TxEMP (Same as the output pin function with the same name)
				bit3: Parity Error
				bit4: Overrun Error
				bit5: Framing Error
				bit6: SYNC/BRK (Same as the output pin function with the same name)
				bit7: ^DSR Input Pin State
			*/
			// clang-format on
			if (midiin_r != midiin_w)
				return 3; /* TxRDY + RxRDY */
			else
				return 1 /* TxRDY */;
		}
		return 0;
	}
};

#include "snd_imfc_rom.c"

static void
imfc_write(uint16_t port, uint8_t val, void *priv);

static uint8_t
imfc_read(uint16_t port, void *priv);

class MusicFeatureCard {
private:
	uint16_t host_port;

	ym2151_device m_ya2151;
	PD71051 m_midi = {};
	TotalControlRegister m_tcr;

	PD71055 m_piuPC;
	// this is the PIU that connects to the PC

	DataContainer<bool> m_piuPC_int0;
	// This is the INT0 line for the PIU connected to the PC

	DataContainer<bool> m_piuPC_int1;
	// This is the INT1 line for the PIU connected to the PC

	PD71055 m_piuIMF;
	// this is the PIU that connects to the card processor

	DataContainer<bool> m_piuIMF_int0;
	// This is the INT0 line for the PIU connected to the IMF

	DataContainer<bool> m_piuIMF_int1;
	// This is the INT1 line for the PIU connected to the IMF

	DataContainer<uint8_t> m_piuPort0Data;
	// port0 data that connects the PIU_PC to PIU_IMF

	DataContainer<uint8_t> m_piuPort1Data;
	// port1 data that connects the PIU_PC to PIU_IMF

	DataContainer<bool> m_piuEXR8;
	// Extended bit8

	DataContainer<bool> m_piuEXR9;
	// Extended bit9

	DataContainer<bool> m_piuGroup0DataAvailable;
	DataContainer<bool> m_piuGroup0DataAcknowledgement;
	DataContainer<bool> m_piuGroup1DataAvailable;
	DataContainer<bool> m_piuGroup1DataAcknowledgement;

	mutex_t* m_hardwareMutex = {};
	Intel8253 m_timer;
	InverterGate m_invTimerAClear;
	DFlipFlop m_df1;
	InverterGate m_invTimerBClear;
	DFlipFlop m_df2;
	OrGate m_totalCardStatus;
	AndGate m_irqMaskGate;
	TriStateBuffer m_irqStatus;
	IrqController m_irqTriggerPc;
	IrqController m_irqTriggerImf;
	TotalStatusRegister m_tsr;

	std::atomic_bool keepRunning               = {};
	std::atomic<bool> m_finishedBootupSequence = {};
	thread_t* m_mainThread                     = nullptr;

	// memory allocation on the IMF
	// ROM
	static constexpr char m_cardName[] = "YAMAHA IBM MUSIC";
	// ConfigurationData m_presetConfigurations[4];
	// VoiceDefinitionBank m_voiceDefinitionBankRom[5];

	// RAM
	VoiceDefinitionBank m_voiceDefinitionBankCustom[2]   = {};
	ConfigurationData m_configurationRAM[16]             = {};
	char m_copyOfCardName[16]                            = {};
	ConfigurationData m_activeConfiguration              = {};
	uint8_t m_nodeNumber                                 = 0;
	uint8_t m_activeConfigurationNr                      = 0;
	CHAIN_MODE m_chainMode                               = {};
	InstrumentParameters m_activeInstrumentParameters[8] = {};
	// uint8_t m_bufferFromMidiIn[2048];
	// uint8_t m_bufferToMidiOut[256];
	// uint8_t m_bufferAFromSystem[256];
	// uint8_t m_bufferBFromSystem[256];
	// uint8_t m_bufferAToSystem[256];
	// uint8_t m_bufferBToSystem[256];
	YmChannelData m_ymChannelData[8]          = {};
	MEMORY_PROTECTION m_memoryProtection      = {};
	uint8_t m_systemRealtimeMessageInProgress = 0;
	uint8_t m_masterTune = 0; // Master tune (-64~63 in 7-bit 2's complement
	                          // notation)
	uint8_t m_masterOutputLevel               = 0; // 0(Max)~127(muted)
	FractionalNote m_masterTuneAsNoteFraction = {};
	FractionalNote m_lastMidiOnOff_FractionAndNoteNumber = {};
	KeyVelocity m_lastMidiOnOff_KeyVelocity              = {};
	Duration m_lastMidiOnOff_Duration                    = {};
	uint8_t m_musicProcessingDepth                       = 0;
	uint8_t m_ya2151_timerA_counter                      = 0;
	uint8_t m_ya2151_timerB_counter                      = 0;
	uint8_t m_ym2151_IRQ_stuff                           = 0;
	CARD_MODE m_cardMode                                 = {};
	ERROR_REPORTING m_errorReport                        = {};
	MidiFlowPath m_configuredMidiFlowPath                = {};
	MidiFlowPath m_actualMidiFlowPath                    = {};
	uint8_t m_incomingMusicCardMessage_Expected          = 0;
	uint8_t m_incomingMusicCardMessage_Size              = 0;
	uint8_t m_incomingMusicCardMessageData[16]           = {};
	MidiDataPacket m_midiDataPacketFromMidiIn{};
	MidiDataPacket m_midiDataPacketFromSystem{};
	uint8_t m_outgoingMusicCardMessageData[16] = {};
	uint8_t m_readMidiDataTimeoutCountdown     = 0;
	uint8_t m_midi_ReceiveSource_SendTarget    = 0;
	uint8_t m_midiTransmitReceiveFlag = 0; // FIXME: change to flags
	CyclicBufferState<uint8_t> m_bufferFromMidiInState;
	uint8_t m_bufferFromMidiIn_lastActiveSenseCodeCountdown = 0;
	CyclicBufferState<uint8_t> m_bufferToMidiOutState;
	uint8_t m_midiOutActiveSensingCountdown        = 0;
	uint8_t m_midiOut_CommandInProgress            = 0;
	uint8_t m_runningCommandOnMidiInTimerCountdown = 0;
	uint8_t m_activeSenseSendingState              = 0;
	CyclicBufferState<uint16_t> m_bufferFromSystemState;
	CyclicBufferState<uint16_t> m_bufferToSystemState;
	uint8_t m_sendDataToSystemTimoutCountdown         = 0;
	uint8_t m_system_CommandInProgress                = 0;
	uint8_t m_runningCommandOnSystemInTimerCountdown  = 0;
	uint8_t m_sp_MidiDataOfMidiCommandInProgress[256] = {};
	uint8_t m_soundProcessorMidiInterpreterState      = 0;
	uint8_t m_soundProcessorMidiInterpreterSysExState = 0;
	uint8_t m_sysEx_ChannelNumber                     = 0;
	uint8_t m_sysEx_InstrumentNumber                  = 0;
	uint8_t* m_soundProcessorSysExCurrentMatchPtr     = nullptr;
	uint8_t m_sp_SysExStateMatchTable[256]            = {};
	uint8_t m_midiChannelToAssignedInstruments[AVAILABLE_MIDI_CHANNELS]
	                                          [AVAILABLE_INSTRUMENTS + 1] =
	                                                  {}; // size:0x90 / 9 x
	                                                      // 16 bytes / 0xFF
	                                                      // := end of list
	                                                      // / e.g. for
	                                                      // MidiChannel 2
	                                                      // ->
	                                                      // MidiChannelToAssignedInstruments[2*9]
	                                                      // = 0,1,2,0xFF
	uint8_t m_receiveDataPacketTypeAState = 0;

	void RegisterIoHandlers(const io_port_t port);

	const char* getCurrentThreadName()
	{
		return "DOSBOX";
	}

	template <typename... Args>
	void log_debug([[maybe_unused]] std::string format,
	               [[maybe_unused]] const Args&... args)
	{
		// IMF_LOG(("[%s] [DEBUG] " + format).c_str(),
		// getCurrentThreadName().c_str(), args...);
	}

	template <typename... Args>
	void log_info([[maybe_unused]] std::string format,
	              [[maybe_unused]] const Args&... args)
	{
		// IMF_LOG(("[%s] [INFO] " + format).c_str(),
		// getCurrentThreadName().c_str(), args...);
	}

	template <typename... Args>
	void log_error([[maybe_unused]] const char* format,
	               [[maybe_unused]] const Args&... args)
	{
#if IMFC_VERBOSE_LOGGING
		static std::string message = {};
		message                    = std::string("IMFC: ") + format;
		LOG_ERR(static_cast<const char*>(message.c_str()), args...);
#endif
	}

	void disableInterrupts()
	{
		m_irqTriggerImf.disableInterrupts();
	}
	void enableInterrupts()
	{
		m_irqTriggerImf.enableInterrupts();
	}
	void delayNop()
	{ /* FIXME:wait for 1xNOP */
	}
	void delayExEx()
	{ /* FIXME:wait for 2x"ex (sp), hl" */
	}

	// ROM Address: 0x0072
	InstrumentParameters* getActiveInstrumentParameters(uint8_t instrumentParametersNr)
	{
		// log_debug("getActiveInstrumentParameters(%i) -
		// outputLevel=0x%02X", instrumentParametersNr,
		// m_activeInstrumentParameters[instrumentParametersNr].instrumentConfiguration.getOutputLevel());
		return &m_activeInstrumentParameters[instrumentParametersNr];
	}

	uint8_t getMidiChannel(InstrumentParameters* instr)
	{
		uint8_t instrIdx = 0;
		for (instrIdx = 0; instrIdx < 8; instrIdx++) {
			if (&m_activeInstrumentParameters[instrIdx] == instr) {
				break;
			}
		}
		if (instrIdx >= 8) {
			return 0xFF;
		}
		for (uint8_t midiChannel = 0; midiChannel < 16; midiChannel++) {
			for (uint8_t i = 0; i < 9; i++) {
				if (m_midiChannelToAssignedInstruments[midiChannel][i] ==
				    instrIdx) {
					return midiChannel;
				}
			}
		}
		return 0x80 + instrIdx;
	}

	// ROM Address: 0x008C
	YmChannelData* getYmChannelData(uint8_t val)
	{
		return &m_ymChannelData[val];
	}

	// ROM Address: 0x00A6
	void sendToYM2151_no_interrupts_allowed(uint8_t registerNr, uint8_t value)
	{
		sendToYM2151_with_disabled_interrupts(registerNr, value);
		enableInterrupts();
	}

	// ROM Address: 0x00AB
	void sendToYM2151_with_disabled_interrupts(uint8_t registerNr, uint8_t value)
	{
		disableInterrupts();
		thread_wait_mutex(m_hardwareMutex);
		m_ya2151.register_w(registerNr);
		thread_release_mutex(m_hardwareMutex);

		delayNop();

		thread_wait_mutex(m_hardwareMutex);
		m_ya2151.data_w(value);
		thread_release_mutex(m_hardwareMutex);
		delayExEx();
	}

	// ROM Address: 0x00B7
	const VoiceDefinition* getVoiceDefinitionOfSameBank(InstrumentParameters* instr,
	                                                    uint8_t instrNr) const
	{
		return &(getVoiceDefinitionBank(instr->instrumentConfiguration.voiceBankNumber)
		                 ->instrumentDefinitions[instrNr]);
	}

	// ROM Address: 0x00C5
	// instrNr = 0x00..0x30 instrument nr in CustomBankA, 0x31..0x5F
	// instrument nr in CustomBankB
	VoiceDefinition* getCustomVoiceDefinition(uint8_t instrNr)
	{
		if (instrNr < 0x30) {
			return &(m_voiceDefinitionBankCustom[0]
			                 .instrumentDefinitions[instrNr]);
		}
		return &(m_voiceDefinitionBankCustom[1].instrumentDefinitions[instrNr - 0x30]);
	}

	// ROM Address: 0x00E7
	const VoiceDefinitionBank* getVoiceDefinitionBank(uint8_t bankNr) const
	{
		// safe-guard that is NOT in the original implementation
		switch (bankNr % 7) {
		default:
		case 0: return &m_voiceDefinitionBankCustom[0];
		case 1: return &m_voiceDefinitionBankCustom[1];
		case 2:
			return (VoiceDefinitionBank*)&m_voiceDefinitionBankRom1Binary;
		case 3:
			return (VoiceDefinitionBank*)&m_voiceDefinitionBankRom2Binary;
		case 4:
			return (VoiceDefinitionBank*)&m_voiceDefinitionBankRom3Binary;
		case 5:
			return (VoiceDefinitionBank*)&m_voiceDefinitionBankRom4Binary;
		case 6:
			return (VoiceDefinitionBank*)&m_voiceDefinitionBankRom5Binary;
		}
	}

	// ROM Address: 0x0101
	const ConfigurationData* getReadOnlyConfigurationData(uint8_t configNr) const
	{
		// WARNING: the original code has a buffer overflow here. If the
		// configNr>20 then random memory will be read!
		if (configNr < 16) {
			return &m_configurationRAM[configNr];
		}
		if (configNr == 16) {
			return reinterpret_cast<const ConfigurationData*>(
			        &m_romPresetConfiguration16Binary);
		}
		if (configNr == 17) {
			return reinterpret_cast<const ConfigurationData*>(
			        &m_romPresetConfiguration17Binary);
		}
		if (configNr == 18) {
			return reinterpret_cast<const ConfigurationData*>(
			        &m_romPresetConfiguration18Binary);
		}
		if (configNr == 19) {
			return reinterpret_cast<const ConfigurationData*>(
			        &m_romPresetConfiguration19Binary);
		}
		// This is not in the original
		return &m_configurationRAM[0];
	}
	// This provides a writable block of config data
	ConfigurationData* getWritableConfigurationData(const uint8_t configNr)
	{
		// Writable config RAM only available in config records 15 and
		// under
		assert(configNr <= 15);

		if (configNr <= 15) {
			return &m_configurationRAM[configNr];
		}
		// This is not in the original
		return &m_configurationRAM[0];
	}

	// ROM Address: 0x013B
	// a=idx 0..39 / c=value
	void setNodeParameter(uint8_t parameterNr, uint8_t val)
	{
		if (parameterNr >= 40) {
			return;
		}
		startMusicProcessing();
		switch (parameterNr) {
		case 0: setNodeParameterName0(val); break;
		case 1: setNodeParameterName1(val); break;
		case 2: setNodeParameterName2(val); break;
		case 3: setNodeParameterName3(val); break;
		case 4: setNodeParameterName4(val); break;
		case 5: setNodeParameterName5(val); break;
		case 6: setNodeParameterName6(val); break;
		case 7: setNodeParameterName7(val); break;
		case 8: setNodeParameterCombineMode(val); break;
		case 9: setNodeParameterLFOSpeed(val); break;
		case 10: setNodeParameterAmpModDepth(val); break;
		case 11: setNodeParameterPitchModDepth(val); break;
		case 12: setNodeParameterLFOWaveForm(val); break;
		case 13: setNodeParameterNoteNrReceptionMode(val); break;
		case 32: setNodeParameterNodeNumber(val); break;
		case 33: setNodeParameterMemoryProtection(val); break;
		case 34: setNodeParameterActiveConfigurationNr(val); break;
		case 35: setNodeParameterMasterTune(val); break;
		case 36: setNodeParameterMasterOutputLevel(val); break;
		case 37: setNodeParameterChainMode(val); break;
		default: break;
		}
		stopMusicProcessing();
	}

	// ROM Address: 0x0194
	void setInstrumentParameter(InstrumentParameters* instr,
	                            uint8_t paramNr, uint8_t value)
	{
		log_debug("setInstrumentParameter(param=0x%02X, value=0x%02X)",
		          paramNr,
		          value);
		if (paramNr >= 0x40) {
			startMusicProcessing();
			setInstrumentParameter_VoiceDefinition(instr,
			                                       paramNr - 0x40,
			                                       value);
			stopMusicProcessing();
			return;
		}
		if (paramNr >= 0x20) {
			return;
		}
		startMusicProcessing();
		switch (paramNr) {
		case 0x00:
			setInstrumentParameter_NumberOfNotes(instr, value);
			break;
		case 0x01:
			setInstrumentParameter_MidiChannelNumber(instr, value);
			break;
		case 0x02:
			setInstrumentParameter_NoteNumberLimitHigh(instr, value);
			break;
		case 0x03:
			setInstrumentParameter_NoteNumberLimitLow(instr, value);
			break;
		case 0x04:
			setInstrumentParameter_VoiceBankNumber(instr, value);
			break;
		case 0x05:
			setInstrumentParameter_VoiceNumber(instr, value);
			break;
		case 0x06: setInstrumentParameter_Detune(instr, value); break;
		case 0x07:
			setInstrumentParameter_OctaveTranspose(instr, value);
			break;
		case 0x08:
			setInstrumentParameter_OutputLevel(instr, value);
			break;
		case 0x09: setInstrumentParameter_Pan(instr, value); break;
		case 0x0A:
			setInstrumentParameter_LFOEnable(instr, value);
			break;
		case 0x0B:
			setInstrumentParameter_PortamentoTime(instr, value);
			break;
		case 0x0C:
			setInstrumentParameter_PitchbenderRange(instr, value);
			break;
		case 0x0D:
			setInstrumentParameter_MonoPolyMode(instr, value);
			break;
		case 0x0E:
			setInstrumentParameter_PMDController(instr, value);
			break;
		case 0x10: setInstrumentParameter_LFOSpeed(instr, value); break;
		case 0x11:
			setInstrumentParameter_AmplitudeModulationDepth(instr, value);
			break;
		case 0x12:
			setInstrumentParameter_PitchModulationDepth(instr, value);
			break;
		case 0x13:
			setInstrumentParameter_LFOWaveform(instr, value);
			break;
		case 0x14:
			setInstrumentParameter_LFOLoadEnable(instr, value);
			break;
		case 0x15: setInstrumentParameter_LFOSync(instr, value); break;
		case 0x16:
			setInstrumentParameter_AmplitudeModulationSensitivity(instr,
			                                                      value);
			break;
		case 0x17:
			setInstrumentParameter_PitchModulationSensitivity(instr,
			                                                  value);
			break;

		/* Inactive cases
		case 0x0F: break;
		case 0x18: break;
		case 0x19: break;
		case 0x1A: break;
		case 0x1B: break;
		case 0x1C: break;
		case 0x1D: break;
		case 0x1E: break;
		case 0x1F: break;
		// don't know why these are here ?!
		case 0x20: break;
		case 0x21: break;
		case 0x22: break;
		case 0x23: break;
		*/
		}
		stopMusicProcessing();
	}

	// ROM Address: 0x01F6
	void coldStart()
	{
		disableInterrupts();
		// Check memory -> skipping
		reset_PIU_device();
		hardReboot();
	}

	// ROM Address: 0x020F
	void hardReboot()
	{
		disableInterrupts();
		// reset the stack pointer :)

		m_copyOfCardName[0] = 0;
		initAllMemoryBuffers();
		m_memoryProtection = MEMORY_WRITABLE;
		softReboot(0xE5); // 0xE5 = Reboot Command
	}

	// ROM Address: 0x0223
	void restartInMusicMode()
	{
		softReboot(0xE0); // 0xE0 = Music Mode Selection
	}

	// int debug_count = 0;

	// ROM Address: 0x0225
	// Note: the input value used are 0xE0 (select music card mode command)
	// and 0xE5 (reboot command). This value will be sent to the system.
	void softReboot(uint8_t commandThatRequestedTheSoftReboot)
	{
		using namespace std::chrono_literals;

		disableInterrupts();
		// reset the stack pointer :)
		m_cardMode = MUSIC_MODE;
		initInterruptHandler();
		startMusicProcessing();
		// log_debug("softReboot - copy start");
		for (uint8_t i = 0; i < 8; i++) {
			m_activeConfiguration.instrumentConfigurations[i].copyFrom(
			        &(getActiveInstrumentParameters(i)->instrumentConfiguration));
		}
		// log_debug("softReboot - copy end");
		if (commandThatRequestedTheSoftReboot !=
		    0xE5 /* Reboot Command */) {
			proc_13EB_called_for_SelectMusicCardMode();
		} else {
			proc_1393_called_for_Reboot();
		}
		processSystemRealTimeMessage_FC();
		setNodeParameter(0x25, m_chainMode);
		logSuccess();
		clearIncomingMusicCardMessageBuffer();
		set_MidiIn_To_SP_InitialState();
		set_System_To_SP_InitialState();
		initialize_ym2151_timers();
		m_outgoingMusicCardMessageData[0] = commandThatRequestedTheSoftReboot;
		send_card_bytes_to_System((const uint8_t*)&m_outgoingMusicCardMessageData,
		                          1);

		log_debug("softReboot - starting infinite loop");
		m_finishedBootupSequence = true;
		while (keepRunning.load()) {
			// log_debug("DEBUG: heartbeat in MUSIC_MODE_LOOP %i",
			// debug_count++);
			MUSIC_MODE_LOOP_read_MidiIn_And_Dispatch(); //FIXME:
			// reenable
			MUSIC_MODE_LOOP_read_System_And_Dispatch();
			logSuccess();
			plat_delay_ms(1);
		}
	}

	// ROM Address: 0x0288
	void restartInThruMode()
	{
		disableInterrupts();
		// init stack pointer
		m_cardMode = THRU_MODE;
		initInterruptHandler();
		startMusicProcessing();
		ym_key_off_on_all_channels();
		clearIncomingMusicCardMessageBuffer();
		initialize_ym2151_timers();
		m_outgoingMusicCardMessageData[0] = 0xE0;
		send_card_bytes_to_System((const uint8_t*)&m_outgoingMusicCardMessageData,
		                          1);
		while (keepRunning.load()) {
			ReadResult readResult = midiIn_readMidiDataByte();
			if (readResult.status == ReadStatus::Error) {
				disableInterrupts();
				resetMidiInBuffersAndPorts();
				enableInterrupts();
				break;
			}
			if (readResult.status == ReadStatus::Success) {
				send_midi_byte_to_System_in_THRU_mode(readResult.data);
			}
			const SystemReadResult systemReadResult =
			        system_read9BitMidiDataByte();
			if (systemReadResult.status == SystemDataAvailable) {
				processIncomingMusicCardMessageByte(
				        systemReadResult.data);
			} else if (systemReadResult.status == MidiDataAvailable) {
				send_midi_byte_to_MidiOut(systemReadResult.data);
				clearIncomingMusicCardMessageBuffer();
			}
		}
	}

	// ROM Address: 0x02D2
	void initAllMemoryBuffers()
	{
		initConfigurationMemory();
		// TODO: clear all memory from Clear memory from 0xE800 to
		// 0xFEFA clear everything except: voiceDefinitionBankCustom1,
		// voiceDefinitionBankCustom2, configurationRAM, copyOfCardName,
		// activeConfiguration, nodeNumber, activeConfigurationNr,
		// chainMode, activeInstrumentParameters
		for (uint8_t i = 0; i < 8; i++) {
			getActiveInstrumentParameters(i)->voiceDefinition.veryShallowClear();
		}
		for (uint8_t i = 0; i < 8; i++) {
			getYmChannelData(i)->channelNumber = i & FieldMask::ChannelNumber;
			getYmChannelData(i)->operatorsEnabled = 0;
			getYmChannelData(i)->unused2          = 0;
		}
		m_masterOutputLevel = 0; // max volume
		m_cardMode          = MUSIC_MODE;
		m_errorReport       = ERROR_REPORTING_DISABLED;
		m_configuredMidiFlowPath.MidiIn_To_System  = 0;
		m_configuredMidiFlowPath.System_To_MidiOut = 0;
		m_configuredMidiFlowPath.MidiIn_To_SP      = 0x1F;
		m_configuredMidiFlowPath.System_To_SP      = 0;
		m_configuredMidiFlowPath.MidiIn_To_MidiOut = 0;
	}

	// ROM Address: 0x0334
	void initConfigurationMemory()
	{
		static constexpr char defaultConfigurationSlotNameSuffix[] =
		        " 1 2 3 4 5 6 7 8 910111213141516";

		// Compare only the first 12 bytes (not the full name)
		const auto name_copy_start = std::begin(m_copyOfCardName);
		const auto name_copy_end_after_12_bytes = name_copy_start + 12;

		if (std::equal(name_copy_start,
		               name_copy_end_after_12_bytes,
		               std::begin(m_cardName))) {
			return;
		}

		// clear memory from 0xC0C0 to 0xE7FF
		// (voiceDefinitionBankCustom1, voiceDefinitionBankCustom2,
		// configurationRAM, copyOfCardName, activeConfiguration,
		// nodeNumber, activeConfigurationNr, chainMode,
		// activeInstrumentParameters)
		m_voiceDefinitionBankCustom[0].deepClear();
		m_voiceDefinitionBankCustom[1].deepClear();
		for (auto& i : m_configurationRAM) {
			i.deepClear();
		}
		memset(&m_copyOfCardName, 0, sizeof(m_copyOfCardName));
		m_activeConfiguration.deepClear();
		m_nodeNumber            = 0;
		m_activeConfigurationNr = 0;
		m_chainMode             = CHAIN_MODE_DISABLED;
		log_debug("initConfigurationMemory - start copy");
		for (auto& m_activeInstrumentParameter : m_activeInstrumentParameters) {
			m_activeInstrumentParameter = {};
		}
		log_debug("initConfigurationMemory - end copy");
		for (uint8_t i = 0; i < 16; i++) {
			initConfigurationRAMSlot(
			        &m_configurationRAM[i],
			        &defaultConfigurationSlotNameSuffix[i * 2]);
		}
		initCustomInstrumentData(&m_voiceDefinitionBankCustom[0], "user 1  ");
		initCustomInstrumentData(&m_voiceDefinitionBankCustom[1], "user 2  ");
		memcpy(&m_copyOfCardName, &m_cardName, sizeof(m_copyOfCardName));
		m_activeConfiguration.shallowCopyFrom(
		        reinterpret_cast<const ConfigurationData*>(
		                &m_romPresetConfiguration16Binary)); // ROMPresetConfiguration16
		log_debug("initConfigurationMemory - copy start");
		for (uint8_t i = 0; i < 8; i++) {
			getActiveInstrumentParameters(i)
			        ->instrumentConfiguration.copyFrom(
			                &(reinterpret_cast<const ConfigurationData*>(
			                          &m_romPresetConfiguration16Binary))
			                         ->instrumentConfigurations[i]);
		}
		log_debug("initConfigurationMemory - copy end");
		m_nodeNumber            = 0;
		m_activeConfigurationNr = 16;
		m_chainMode             = CHAIN_MODE_DISABLED;
	}

	// ROM Address: 0x03AD
	static void initConfigurationRAMSlot(ConfigurationData* config, const char* c)
	{
		// this would be after the name copy in the original code
		config->SetFromStream(m_romPresetConfiguration16Binary);
		memcpy(&config->name, " user ", 6);
		config->name[6] = c[0];
		config->name[7] = c[1];
	}

	// ROM Address: 0x03AD
	static void initCustomInstrumentData(VoiceDefinitionBank* customBank,
	                                     const char* name)
	{
		// this would be after the name copy in the original code
		customBank->SetFromStream(m_voiceDefinitionBankRom1Binary);
		memcpy(&customBank->name, name, 8);
	}

	// ROM Address: 0x0410
	ReadResult readMidiData()
	{
		return readMidiDataWithTimeout();
	}

	// ROM Address: 0x0422
	ReadResult readMidiDataWithTimeout()
	{
		m_readMidiDataTimeoutCountdown = 0xFF;
		if ((m_midi_ReceiveSource_SendTarget & 2) == 0) {
			// read midi data from midi in
			while (keepRunning.load()) {
				const ReadResult readResult = midiIn_readMidiDataByte();
				switch (readResult.status) {
				case ReadStatus::Error:
					return midiInReadError(
					        &m_midiDataPacketFromMidiIn);
				case ReadStatus::NoData:
					if (!isMidiDataPacket_in_state_01_36_37_38(
					            &m_midiDataPacketFromMidiIn)) {
						return {ReadStatus::NoData, 0};
					}
					if (hasReadMidiDataTimeoutExpired()) {
						reportErrorIfNeeded(
						        TIMEOUT_ERROR_MIDI_TO_MUSICCARD);
						return midiInReadError(
						        &m_midiDataPacketFromMidiIn);
					}
					break;
				case ReadStatus::Success:
					if (readResult.data < 0xF8) {
						return {ReadStatus::Success,
						        midiIn_MidiDataDispatcher_00_to_F7(
						                &m_midiDataPacketFromMidiIn,
						                readResult.data)};
					}
					if (readResult.data != 0xF9 &&
					    readResult.data < 0xFD) {
						// only for 0xFA, 0xFB and 0xFC
						if ((m_actualMidiFlowPath.MidiIn_To_MidiOut &
						     0x10) != 0) {
							send_midi_byte_to_MidiOut(
							        readResult.data);
						}
						if ((m_actualMidiFlowPath.MidiIn_To_System &
						     0x10) != 0) {
							send_midi_byte_to_System(
							        readResult.data);
						}
						if ((m_actualMidiFlowPath.MidiIn_To_SP &
						     0x10) != 0) {
							processSystemRealTimeMessage(
							        readResult.data);
						}
					}
					break;
				}
			}
		} else {
			// read midi data from system
			while (keepRunning.load()) {
				// log_debug("readMidiDataWithTimeout() - system
				// while loop");
				switch (system_isMidiDataAvailable()) {
				case NoDataAvailable:
					// log_debug("readMidiDataWithTimeout()
					// - case NoDataAvailable");
					if (!isMidiDataPacket_in_state_01_36_37_38(
					            &m_midiDataPacketFromSystem)) {
						return {ReadStatus::NoData, 0};
					}
					// log_debug("readMidiDataWithTimeout()
					// - MidiDataPacket is in state
					// 01_36_37_38");
					if (hasReadMidiDataTimeoutExpired()) {
						return sendTimeoutToSystem(
						        &m_midiDataPacketFromSystem);
					}
					// log_debug("readMidiDataWithTimeout()
					// - MidiDataPacket is in state
					// 01_36_37_38 and timeout not
					// expired");
					break;
				case SystemDataAvailable:
					// log_debug("readMidiDataWithTimeout()
					// - case SystemDataAvailable");
					set_System_To_SP_InitialState();
					// log_debug("readMidiDataWithTimeout()
					// - calling
					// midiDataDispatcher_transitionToNewState");
					midiDataDispatcher_transitionToNewState(
					        &m_midiDataPacketFromSystem,
					        0xF7); // End-of-command
					// log_debug("readMidiDataWithTimeout()
					// - calling
					// conditional_send_midi_byte_to_MidiOut");
					conditional_send_midi_byte_to_MidiOut(
					        &m_midiDataPacketFromSystem,
					        m_actualMidiFlowPath.System_To_MidiOut);
					return {ReadStatus::Error, 0xF7};
				case MidiDataAvailable:
					const SystemReadResult systemReadResult =
					        system_read9BitMidiDataByte();
					// log_debug("readMidiDataWithTimeout()
					// - case MidiDataAvailable (0x%02X)",
					// systemReadResult.data);
					if (systemReadResult.data < 0xF8) {
						return {ReadStatus::Success,
						        system_MidiDataDispatcher_00_to_F7(
						                &m_midiDataPacketFromSystem,
						                systemReadResult.data)};
					}
					if (systemReadResult.data != 0xF9 &&
					    systemReadResult.data < 0xFD) {
						if ((m_actualMidiFlowPath.System_To_MidiOut &
						     0x10) != 0) {
							send_midi_byte_to_MidiOut(
							        systemReadResult.data);
						}
						if ((m_actualMidiFlowPath.System_To_SP &
						     0x10) != 0) {
							processSystemRealTimeMessage(
							        systemReadResult.data);
						}
					}
					break;
				}
			}
		}
		return {ReadStatus::NoData, 0};
	}

	uint8_t midiIn_MidiDataDispatcher_00_to_F7(MidiDataPacket* packet,
	                                           uint8_t midiData)
	{
		midiDataDispatcher_transitionToNewState(packet, midiData);
		conditional_send_midi_byte_to_MidiOut(
		        packet, m_actualMidiFlowPath.MidiIn_To_MidiOut);
		conditional_send_midi_byte_to_System(packet,
		                                     m_actualMidiFlowPath.MidiIn_To_System);
		return midiData;
	}

	ReadResult midiInReadError(MidiDataPacket* packet)
	{
		disableInterrupts();
		resetMidiInBuffersAndPorts();
		enableInterrupts();
		if ((m_actualMidiFlowPath.MidiIn_To_MidiOut & 0x1F) != 0) {
			sendActiveSenseCodeSafe();
		}
		if ((m_actualMidiFlowPath.MidiIn_To_SP & 0x1F) != 0) {
			ym_key_off_on_all_channels();
		}
		if ((m_actualMidiFlowPath.MidiIn_To_SP & 0x10) != 0) {
			processSystemRealTimeMessage_FC();
		}
		set_MidiIn_To_SP_InitialState();
		return {ReadStatus::Error,
		        midiIn_MidiDataDispatcher_00_to_F7(packet, 0xF7)};
	}

	uint8_t system_MidiDataDispatcher_00_to_F7(MidiDataPacket* packet,
	                                           uint8_t midiData)
	{
		clearIncomingMusicCardMessageBuffer();
		midiDataDispatcher_transitionToNewState(packet, midiData);
		conditional_send_midi_byte_to_MidiOut(
		        packet, m_actualMidiFlowPath.System_To_MidiOut);
		return midiData;
	}

	ReadResult sendTimeoutToSystem(MidiDataPacket* packet)
	{
		reportErrorIfNeeded(TIMEOUT_ERROR_SYSTEM_TO_MUSICCARD);
		if ((m_actualMidiFlowPath.System_To_MidiOut & 0x1F) != 0) {
			sendActiveSenseCodeSafe();
		}
		if ((m_actualMidiFlowPath.System_To_SP & 0x1F) != 0) {
			ym_key_off_on_all_channels();
		}
		if ((m_actualMidiFlowPath.System_To_SP & 0x10) != 0) {
			processSystemRealTimeMessage_FC();
		}
		set_System_To_SP_InitialState();
		return {ReadStatus::Error,
		        system_MidiDataDispatcher_00_to_F7(packet, 0xF7)};
	}

	// ROM Address: 0x052E
	static bool isMidiDataPacket_in_state_01_36_37_38(MidiDataPacket* packet)
	{
		return packet->state == 0x01 || packet->state == 0x36 ||
		       packet->state == 0x37 || packet->state == 0x38;
	}

	// ROM Address: 0x053D
	bool hasReadMidiDataTimeoutExpired() const
	{
		return m_readMidiDataTimeoutCountdown == 0;
	}

	// ROM Address: 0x0542
	// RealTimeMessage midi code (F8-FF)
	void processSystemRealTimeMessage(uint8_t midiCode)
	{
		startMusicProcessing();
		switch (midiCode - 0xF8) {
		case 0: processSystemRealTimeMessage_F8(); break;
		case 2: processSystemRealTimeMessage_FA_and_FB(); break;
		case 3: processSystemRealTimeMessage_FA_and_FB(); break;
		case 4: processSystemRealTimeMessage_FC(); break;

		/* Inactive cases
		case 1: break;
		case 5: break;
		case 6: break;
		case 7: break;
		*/
		}
		stopMusicProcessing();
	}

	// ROM Address: 0x055B
	void decreaseReadMidiDataTimeout()
	{
		if (m_readMidiDataTimeoutCountdown != 0U) {
			m_readMidiDataTimeoutCountdown--;
		}
	}

	// ROM Address: 0x0565
	WriteStatus send_midi_byte(uint8_t data)
	{
		log_debug("send_midi_byte %02X", data);
		if ((m_midi_ReceiveSource_SendTarget & 1) != 0) {
			// send to system
			if ((m_actualMidiFlowPath.System_To_SP & 0x20) != 0) {
				return send_midi_byte_to_System(data);
			}
			return WriteStatus::Success;
		} // send to midi out
		if ((m_actualMidiFlowPath.MidiIn_To_SP & 0x20) != 0) {
			send_midi_byte_to_MidiOut(data);
		}
		return WriteStatus::Success;
	}

	// ROM Address: 0x0584
	void MUSIC_MODE_LOOP_read_MidiIn_And_Dispatch()
	{
		static constexpr char midi_error_message_2[] = "MIDI/error   !!!";

		m_midi_ReceiveSource_SendTarget = 0; // read data from MIDI IN /
		                                     // send data to MIDI OUT
		do {
			const ReadResult readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				if ((readResult.data & 0x60) == 0) {
					return;
				}
				return logError(midi_error_message_2);
			}
			if (readResult.status == ReadStatus::Success) {
				conditional_send_midi_byte_to_SP(
				        &m_midiDataPacketFromMidiIn,
				        m_actualMidiFlowPath.MidiIn_To_SP,
				        readResult.data);
			}
		} while (doesMidiDataPacketNeedAdditionalData_MUSIC_MODE(
		        m_midiDataPacketFromMidiIn.state));
	}

	// ROM Address: 0x05BE
	void MUSIC_MODE_LOOP_read_System_And_Dispatch()
	{
		m_midi_ReceiveSource_SendTarget = 3; // read data from SYSTEM /
		                                     // send data to SYSTEM
		do {
			// log("MUSIC_MODE_LOOP_read_System_And_Dispatch -
			// readMidiData()");
			const ReadResult readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				log_debug("MUSIC_MODE_LOOP_read_System_And_Dispatch - system_read9BitMidiDataByte()");
				const SystemReadResult systemReadResult =
				        system_read9BitMidiDataByte();
				if (systemReadResult.status == SystemDataAvailable) {
					log_debug("PC->IMFC: Found system data [1%02X] in queue",
					          systemReadResult.data);
					// log("MUSIC_MODE_LOOP_read_System_And_Dispatch
					// - calling
					// processIncomingMusicCardMessageByte");
					processIncomingMusicCardMessageByte(
					        systemReadResult.data);
				}
				return;
			}
			if (readResult.status == ReadStatus::Success) {
				log_debug("PC->IMFC: Found midi data [%02X] in queue",
				          readResult.data);
				conditional_send_midi_byte_to_SP(
				        &m_midiDataPacketFromSystem,
				        m_actualMidiFlowPath.System_To_SP,
				        readResult.data);
			}
		} while (doesMidiDataPacketNeedAdditionalData_MUSIC_MODE(
		        m_midiDataPacketFromSystem.state));
	}

	// ROM Address: 0x05E7
	bool doesMidiDataPacketNeedAdditionalData_MUSIC_MODE(uint8_t midiDataPacketState)
	{
		send_F7_to_MidiOut_if_timed_out();
		send_F7_to_System_if_timed_out();
		return midiDataPacketState == 0x01 || midiDataPacketState == 0x36 ||
		       midiDataPacketState == 0x37 || midiDataPacketState == 0x38;
	}

	// ROM Address: 0x05FB
	void set_MidiIn_To_SP_InitialState()
	{
		m_midiDataPacketFromMidiIn.state = PACKET_STATE_00;
		SoundProcessor_SetToInitialState();
	}

	// ROM Address: 0x060B
	void set_System_To_SP_InitialState()
	{
		m_midiDataPacketFromSystem.state = PACKET_STATE_00;
		SoundProcessor_SetToInitialState();
	}

	// ROM Address: 0x061B
	void midiDataDispatcher_transitionToNewState(MidiDataPacket* packet,
	                                             uint8_t midiData)
	{
		if (midiData >= 0x80) {
			// midi command byte
			packet->data[0] = midiData;
			packet->state = convert_midi_command_byte_to_initial_state(
			        midiData);
		} else {
			// midi data byte
			uint8_t state = packet->state;
			switch (state) {
			case 0x00:
				state = processMidiState_00(packet, midiData);
				break;
			case 0x03:
				state = processMidiState_03(packet, midiData);
				break;

			case 0x02: // dupelicate action as 0x04
			case 0x04:
				state = processMidiState_02_04(packet, midiData);
				break;

			case 0x06:
				state = processMidiState_06(packet, midiData);
				break;

			case 0x05: // dupelicate action as 0x07
			case 0x07:
				state = processMidiState_05_07(packet, midiData);
				break;

			case 0x08: // dupelicate action as 0x09
			case 0x09:
				state = processMidiState_08_09(packet, midiData);
				break;

			case 0x0B:
				state = processMidiState_0B(packet, midiData);
				break;

			case 0x0A: // dupelicate action as 0x0C
			case 0x0C:
				state = processMidiState_0A_0C(packet, midiData);
				break;

			case 0x0D: // dupelicate action as 0x0E
			case 0x0E:
				state = processMidiState_0D_0E(packet, midiData);
				break;

			case 0x0F:
				state = processMidiState_0F(packet, midiData);
				break;
			case 0x10:
				state = processMidiState_10(packet, midiData);
				break;
			case 0x11:
				state = processMidiState_11(packet, midiData);
				break;
			case 0x12:
				state = processMidiState_12(packet, midiData);
				break;
			case 0x13:
				state = processMidiState_13(packet, midiData);
				break;
			case 0x14:
				state = processMidiState_14(packet, midiData);
				break;
			case 0x15:
				state = processMidiState_15(packet, midiData);
				break;
			case 0x16:
				state = processMidiState_16(packet, midiData);
				break;
			case 0x17:
				state = processMidiState_17(packet, midiData);
				break;
			case 0x19:
				state = processMidiState_19(packet, midiData);
				break;
			case 0x1A:
				state = processMidiState_1A(packet, midiData);
				break;
			case 0x1C:
				state = processMidiState_1C(packet, midiData);
				break;
			case 0x1D:
				state = processMidiState_1D(packet, midiData);
				break;
			case 0x1E:
				state = processMidiState_1E(packet, midiData);
				break;
			case 0x20:
				state = processMidiState_20(packet, midiData);
				break;
			case 0x21:
				state = processMidiState_21(packet, midiData);
				break;
			case 0x22:
				state = processMidiState_22(packet, midiData);
				break;
			case 0x23:
				state = processMidiState_23(packet, midiData);
				break;
			case 0x24:
				state = processMidiState_24(packet, midiData);
				break;
			case 0x26:
				state = processMidiState_26(packet, midiData);
				break;
			case 0x27:
				state = processMidiState_27(packet, midiData);
				break;
			case 0x29:
				state = processMidiState_29(packet, midiData);
				break;
			case 0x2B:
				state = processMidiState_2B(packet, midiData);
				break;
			case 0x2D:
				state = processMidiState_2D(packet, midiData);
				break;
			case 0x2E:
				state = processMidiState_2E(packet, midiData);
				break;
			case 0x30:
				state = processMidiState_30(packet, midiData);
				break;
			case 0x31:
				state = processMidiState_31(packet, midiData);
				break;
			case 0x33:
				state = processMidiState_33(packet, midiData);
				break;
			case 0x34:
				state = processMidiState_34(packet, midiData);
				break;

			// duplicate actions
			case 0x18:
			case 0x1B:
			case 0x1F:
			case 0x25:
			case 0x28:
			case 0x2A:
			case 0x2C:
			case 0x2F:
			case 0x32:
			case 0x35:
				state = processMidiState_18_1B_1F_25_28_2A_2C_2F_32_35(
				        packet, midiData);
				break;

			// duplicate actions
			case 0x01:
			case 0x36:
			case 0x37:
			case 0x38:
				state = processMidiState_01_36_37_38(packet, midiData);
				break;
			case 0x3A:
				state = processMidiState_3A(packet, midiData);
				break;
			case 0x3B:
				state = processMidiState_3B(packet, midiData);
				break;
			case 0x3D:
				state = processMidiState_3D(packet, midiData);
				break;
			case 0x3E:
				state = processMidiState_3E(packet, midiData);
				break;

			// duplicate actions
			case 0x39:
			case 0x3C:
			case 0x3F:
				state = processMidiState_39_3C_3F(packet, midiData);
				break;

			case 0x40:
				state = processMidiState_40(packet, midiData);
				break;
			}
			packet->state = state;
		}
	}

	// ROM Address: 0x063F
	// Note: midiCommandByte will always be >= 0x80 !
	static uint8_t convert_midi_command_byte_to_initial_state(uint8_t midiCommandByte)
	{
		static constexpr uint8_t initialMidiStateTable[16] = {
		        0x15 /* 0xF0 */,
		        0x12 /* 0xF1 */,
		        0x0F /* 0xF2 */,
		        0x12 /* 0xF3 */,
		        0x00 /* 0xF4 */,
		        0x00 /* 0xF5 */,
		        0x14 /* 0xF6 */,
		        0x40 /* 0xF7 */,
		        0x02 /* 0x8n/0xF8 */,
		        0x02 /* 0x9n/0xF9 */,
		        0x05 /* 0xAn/0xFA */,
		        0x0A /* 0xBn/0xFB */,
		        0x0D /* 0xCn/0xFC */,
		        0x08 /* 0xDn/0xFD */,
		        0x05 /* 0xEn/0xFE */,
		        0x00 /* 0xFF */};

		if (midiCommandByte < 0xF0) {
			midiCommandByte = midiCommandByte >> 4;
		}
		return initialMidiStateTable[midiCommandByte & 0x0F];
	}

	// ROM Address: 0x06E3
	static uint8_t processMidiState_00(MidiDataPacket* /*packet*/,
	                                   uint8_t /*midiData*/)
	{
		return 0x00;
	}

	// ROM Address: 0x06E6
	static uint8_t processMidiState_01_36_37_38(MidiDataPacket* packet,
	                                            uint8_t midiData)
	{
		packet->data[0] = midiData;
		return 0x01;
	}

	// ROM Address: 0x06EC
	static uint8_t processMidiState_02_04(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[1] = midiData;
		return 0x03;
	}

	// ROM Address: 0x06F2
	static uint8_t processMidiState_03(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return 0x04;
	}

	// ROM Address: 0x06F8
	static uint8_t processMidiState_05_07(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[1] = midiData;
		return 0x06;
	}

	// ROM Address: 0x06FE
	static uint8_t processMidiState_06(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return 0x07;
	}

	// ROM Address: 0x0704
	static uint8_t processMidiState_08_09(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[1] = midiData;
		return 0x09;
	}

	// ROM Address: 0x070A
	static uint8_t processMidiState_0A_0C(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[1] = midiData;
		return 0x0B;
	}

	// ROM Address: 0x0710
	static uint8_t processMidiState_0B(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return 0x0C;
	}

	// ROM Address: 0x0716
	static uint8_t processMidiState_0D_0E(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[1] = midiData;
		return 0x0E;
	}

	// ROM Address: 0x071C
	static uint8_t processMidiState_0F(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[1] = midiData;
		return 0x10;
	}

	// ROM Address: 0x0722
	static uint8_t processMidiState_10(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return 0x11;
	}

	// ROM Address: 0x0728
	static uint8_t processMidiState_11(MidiDataPacket* /*packet*/,
	                                   uint8_t /*midiData*/)
	{
		return 0x00;
	}

	// ROM Address: 0x072B
	static uint8_t processMidiState_12(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[1] = midiData;
		return 0x13;
	}

	// ROM Address: 0x0731
	static uint8_t processMidiState_13(MidiDataPacket* /*packet*/,
	                                   uint8_t /*midiData*/)
	{
		return 0x00;
	}

	// ROM Address: 0x0734
	static uint8_t processMidiState_14(MidiDataPacket* /*packet*/,
	                                   uint8_t /*midiData*/)
	{
		return 0x00;
	}

	// ROM Address: 0x0737
	static uint8_t processMidiState_15(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[1] = midiData;
		return midiData == 0x43 ? 0x16 : 0x36;
		return 0x00;
	}

	// ROM Address: 0x0745
	static uint8_t processMidiState_16(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return midiData == 0x75 ? 0x17 : 0x37;
		return 0x00;
	}

	// ROM Address: 0x0753
	static uint8_t processMidiState_17(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[3] = midiData;
		return midiData == 0x70 ? 0x18 : midiData == 0x71 ? 0x39 : 0x38;
	}

	// ROM Address: 0x0768
	static uint8_t processMidiState_18_1B_1F_25_28_2A_2C_2F_32_35(
	        MidiDataPacket* /*packet*/, uint8_t midiData)
	{
		static constexpr uint8_t transition[8] = {
		        0x19, 0x1C, 0x20, 0x26, 0x29, 0x2B, 0x2D, 0x30};
		return transition[(midiData >> 4) & 7];
	}

	// ROM Address: 0x0786
	static uint8_t processMidiState_19(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return 0x1A;
	}

	// ROM Address: 0x078C
	static uint8_t processMidiState_1A(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[3] = midiData;
		return 0x1B;
	}

	// ROM Address: 0x0792
	static uint8_t processMidiState_1C(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return 0x1D;
	}

	// ROM Address: 0x0798
	static uint8_t processMidiState_1D(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[3] = midiData;
		return 0x1E;
	}

	// ROM Address: 0x079E
	static uint8_t processMidiState_1E(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[4] = midiData;
		return 0x1F;
	}

	// ROM Address: 0x07A4
	static uint8_t processMidiState_20(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return 0x21;
	}

	// ROM Address: 0x07AA
	static uint8_t processMidiState_21(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[3] = midiData;
		return 0x22;
	}

	// ROM Address: 0x07B0
	static uint8_t processMidiState_22(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[4] = midiData;
		return 0x23;
	}

	// ROM Address: 0x07B6
	static uint8_t processMidiState_23(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[5] = midiData;
		return 0x24;
	}

	// ROM Address: 0x07BC
	static uint8_t processMidiState_24(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[6] = midiData;
		return 0x25;
	}

	// ROM Address: 0x07C2
	static uint8_t processMidiState_26(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return 0x27;
	}

	// ROM Address: 0x07C8
	static uint8_t processMidiState_27(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[3] = midiData;
		return 0x28;
	}

	// ROM Address: 0x07CE
	static uint8_t processMidiState_29(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return 0x2A;
	}

	// ROM Address: 0x07D4
	static uint8_t processMidiState_2B(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return 0x2C;
	}

	// ROM Address: 0x07DA
	static uint8_t processMidiState_2D(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return 0x2E;
	}

	// ROM Address: 0x07E0
	static uint8_t processMidiState_2E(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[3] = midiData;
		return 0x2F;
	}

	// ROM Address: 0x07E6
	static uint8_t processMidiState_30(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return (midiData & 0b01000000) != 0 ? 0x33 : 0x31;
	}

	// ROM Address: 0x07F3
	static uint8_t processMidiState_31(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[3] = midiData;
		return 0x32;
	}

	// ROM Address: 0x07F9
	static uint8_t processMidiState_33(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[3] = midiData;
		return 0x34;
	}

	// ROM Address: 0x07FF
	static uint8_t processMidiState_34(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[4] = midiData;
		return 0x35;
	}

	// ROM Address: 0x0805
	static uint8_t processMidiState_39_3C_3F(MidiDataPacket* packet,
	                                         uint8_t midiData)
	{
		packet->data[0] = 0xFE;
		packet->data[1] = midiData;
		return 0x3A;
	}

	// ROM Address: 0x080F
	static uint8_t processMidiState_3A(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[2] = midiData;
		return (midiData & 0b01000000) != 0 ? 0x3D : 0x3B;
	}

	// ROM Address: 0x081C
	static uint8_t processMidiState_3B(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[3] = midiData;
		return 0x3C;
	}

	// ROM Address: 0x0822
	static uint8_t processMidiState_3D(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[3] = midiData;
		return 0x3E;
	}

	// ROM Address: 0x0828
	static uint8_t processMidiState_3E(MidiDataPacket* packet, uint8_t midiData)
	{
		packet->data[4] = midiData;
		return 0x3F;
	}

	// ROM Address: 0x082E
	static uint8_t processMidiState_40(MidiDataPacket* /*packet*/,
	                                   uint8_t /*midiData*/)
	{
		return 0x00;
	}

	struct StateTermination {
		uint8_t flowMask   = 0;
		uint8_t totalBytes = 0;
	};
	static constexpr StateTermination stateTerminationTable[0x41] = {
	        // clang-format off
		{0,0},{8,1},{0,0},{0,0},{1,3},{0,0},{0,0},{2,3},
		{0,0},{2,2},{0,0},{0,0},{4,3},{0,0},{4,2},{0,0},
		{0,0},{8,3},{0,0},{8,2},{8,1},{0,0},{0,0},{0,0},
		{0,0},{0,0},{0,0},{1,4},{0,0},{0,0},{0,0},{1,5},
		{0,0},{0,0},{0,0},{0,0},{0,0},{1,7},{0,0},{0,0},
		{4,4},{0,0},{4,3},{0,0},{2,3},{0,0},{0,0},{2,4},
		{0,0},{0,0},{8,4},{0,0},{0,0},{8,5},{8,2},{8,3},
		{8,4},{0,0},{0,0},{0,0},{8,4},{0,0},{0,0},{8,5},
		{15,1}
	        // clang-format on
	};

	// ROM Address: 0x0831
	void conditional_send_midi_byte_to_SP(MidiDataPacket* packet,
	                                      uint8_t flow, uint8_t /*midiData*/)
	{
		// log("conditional_send_midi_byte_to_SP - %02X for state %02X",
		// midiData, packet->state);
		if (((stateTerminationTable[packet->state].flowMask & flow) != 0) &&
		    stateTerminationTable[packet->state].totalBytes != 0) {
			log_debug("conditional_send_midi_byte_to_SP - midi packet complete. Sending to sound processor");
			uint8_t i = 0;
			if (packet->data[0] >= 0xFE) {
				soundProcessor_handle_midi_0xFE_0xFF(packet->data[0]);
				i++;
			}
			for (; i < stateTerminationTable[packet->state].totalBytes;
			     i++) {
				SoundProcessor_processMidiByte(packet->data[i]);
			}
		}
	}

	// ROM Address: 0x085C
	void soundProcessor_handle_midi_0xFE_0xFF(uint8_t data)
	{
		if (data == 0xFE) {
			SoundProcessor_SetState_SysEx_ParameterListTransferToMusicCard();
		} else {
			SoundProcessor_SetState_SysEx_EventListTransferToMusicCard();
		}
	}

	// ROM Address: 0x0869
	void conditional_send_midi_byte_to_MidiOut(MidiDataPacket* packet, uint8_t flow)
	{
		if (packet->state == 0x01) {
			if ((flow & 0x08) != 0) {
				send_midi_byte_to_MidiOut(packet->data[0]);
			}
		} else {
			if (((stateTerminationTable[packet->state].flowMask &
			      flow) != 0) &&
			    stateTerminationTable[packet->state].totalBytes != 0) {
				sendMidiResponse_to_MidiOut(
				        (const uint8_t*)&(packet->data),
				        stateTerminationTable[packet->state].totalBytes);
			}
		}
	}

	// ROM Address: 0x0894
	void conditional_send_midi_byte_to_System(MidiDataPacket* packet, uint8_t flow)
	{
		if (packet->state == 0x01) {
			if ((flow & 0x08) != 0) {
				send_midi_byte_to_System(packet->data[0]);
			}
		} else {
			if (((stateTerminationTable[packet->state].flowMask &
			      flow) != 0) &&
			    stateTerminationTable[packet->state].totalBytes != 0) {
				sendMidiResponse_to_System(
				        (const uint8_t*)&(packet->data),
				        stateTerminationTable[packet->state].totalBytes);
			}
		}
	}

	// ROM Address: 0x0941
	void setNodeParameterChainMode(uint8_t val)
	{
		log_debug("setNodeParameterChainMode()");
		if (val >= 2) {
			return;
		}
		// wait until all the data has been sent
		while (m_bufferToMidiOutState.hasData()) {
			log_info("setNodeParameterChainMode - waiting for m_bufferToMidiOutState to be empty");
		};
		m_chainMode = val == 0 ? CHAIN_MODE_DISABLED : CHAIN_MODE_ENABLED;
		if (m_cardMode == THRU_MODE) {
			return;
		}
		m_actualMidiFlowPath.MidiIn_To_System = m_configuredMidiFlowPath.MidiIn_To_System;
		m_actualMidiFlowPath.System_To_MidiOut =
		        m_configuredMidiFlowPath.System_To_MidiOut;
		m_actualMidiFlowPath.MidiIn_To_MidiOut =
		        m_configuredMidiFlowPath.MidiIn_To_MidiOut;
		m_actualMidiFlowPath.MidiIn_To_SP = m_configuredMidiFlowPath.MidiIn_To_SP |
		                                    0x20;
		m_actualMidiFlowPath.System_To_SP = m_configuredMidiFlowPath.System_To_SP |
		                                    0x20;
		if (m_chainMode == CHAIN_MODE_ENABLED) {
			modifyActualMidiFlowPathForChainMode();
		}
		if (m_actualMidiFlowPath.MidiIn_To_MidiOut != 0) {
			sub_9AB();
		}
		log_debug("setNodeParameterChainMode - copy start");
		for (uint8_t i = 0; i < 8; i++) {
			m_activeConfiguration.instrumentConfigurations[i].copyFrom(
			        &(getActiveInstrumentParameters(i)->instrumentConfiguration));
		}
		log_debug("setNodeParameterChainMode - copy end");
		proc_13EB_called_for_SelectMusicCardMode();
	}

	// ROM Address: 0x099A
	void modifyActualMidiFlowPathForChainMode()
	{
		m_actualMidiFlowPath.MidiIn_To_MidiOut = m_actualMidiFlowPath.MidiIn_To_SP &
		                                         0x1E;
		m_actualMidiFlowPath.System_To_MidiOut = m_actualMidiFlowPath.System_To_SP &
		                                         0x1E;
	}

	// ROM Address: 0x09AB
	void sub_9AB()
	{
		m_actualMidiFlowPath.MidiIn_To_SP = m_actualMidiFlowPath.MidiIn_To_SP &
		                                    0x1F;
	}

	// ROM Address: 0x09B4
	void clearIncomingMusicCardMessageBuffer()
	{
		m_incomingMusicCardMessage_Expected = 0;
		m_incomingMusicCardMessage_Size     = 0;
	}

	// ROM Address: 0x09BC
	void processIncomingMusicCardMessageByte(uint8_t messageByte)
	{
		log_debug("IMFC: processIncomingMusicCardMessageByte(0x%02X)",
		          messageByte);
		if (messageByte >= 0x80) {
			setIncomingMusicCardMessageBufferExpected(messageByte);
			log_debug("IMFC: expecting total bytes of %i",
			          m_incomingMusicCardMessage_Expected);
		}
		if (m_incomingMusicCardMessage_Expected == 0) {
			return;
		}
		m_incomingMusicCardMessageData[m_incomingMusicCardMessage_Size] = messageByte;
		m_incomingMusicCardMessage_Size++;
		if (--m_incomingMusicCardMessage_Expected != 0U) {
			return;
		};
		log_debug("IMFC: reached expected message size... dispatching");
		startMusicProcessing();
		switch (m_incomingMusicCardMessageData[0] - 0xD0) {
		case 0x00: processMusicCardMessageCardModeStatus(); break;
		case 0x01: processMusicCardMessageErrorReportStatus(); break;
		case 0x02: processMusicCardMessagePathParameterStatus(); break;
		case 0x03: processMusicCardMessageNodeParameterStatus(); break;
		/* Inactive cases
		case 0x04: break;
		case 0x05: break;
		case 0x06: break;
		case 0x07: break;
		case 0x08: break;
		case 0x09: break;
		case 0x0A: break;
		case 0x0B: break;
		case 0x0C: break;
		case 0x0D: break;
		case 0x0E: break;
		case 0x0F: break;
		*/
		case 0x10: processMusicCardMessageSelectCardMode(); break;
		case 0x11:
			processMusicCardMessageSelectErrorReportMode();
			break;
		case 0x12: processMusicCardMessageSetPaths(); break;
		case 0x13: processMusicCardMessageSetNodeParameters(); break;
		case 0x14: processMusicCardMessage1E4(); break;
		case 0x15: processMusicCardMessageReboot(); break;
		case 0x16: processMusicCardMessageDebugWriteToMemory(); break;
		/* Inactive cases
		case 0x17: break;
		case 0x18: break;
		case 0x19: break;
		case 0x1A: break;
		case 0x1B: break;
		case 0x1C: break;
		case 0x1D: break;
		case 0x1E: break;
		case 0x1F: break;
		*/
		}
		stopMusicProcessing();
	}

	// ROM Address: 0x09E5
	void setIncomingMusicCardMessageBufferExpected(uint8_t messageByte)
	{
		// clang-format off
		static constexpr uint8_t expectedSizes[0x20] = {1, 1, 1, 1, 0, 0, 0,
		                                                0, 0, 0, 0, 0, 0, 0,
		                                                0, 0, 2, 2, 6, 9, 1,
		                                                1, 1, 0, 0, 0, 0, 0,
		                                                0, 0, 0, 0};
		// clang-format on

		clearIncomingMusicCardMessageBuffer();
		if (messageByte >= 0xF0) {
			return;
		}
		if (messageByte < 0xD0) {
			return;
		}
		m_incomingMusicCardMessage_Expected = expectedSizes[messageByte - 0xD0];
		m_incomingMusicCardMessage_Size = 0;
	}

	// ROM Address: 0x0A5F
	void processMusicCardMessageCardModeStatus()
	{
		log_debug("processMusicCardMessageCardModeStatus()");
		m_outgoingMusicCardMessageData[0] = 0xD0;
		m_outgoingMusicCardMessageData[1] = m_cardMode;
		send_card_bytes_to_System((const uint8_t*)&m_outgoingMusicCardMessageData,
		                          2);
	}

	// ROM Address: 0x0A72
	void processMusicCardMessageErrorReportStatus()
	{
		log_debug("processMusicCardMessageErrorReportStatus()");
		m_outgoingMusicCardMessageData[0] = 0xD1;
		m_outgoingMusicCardMessageData[1] = m_errorReport;
		send_card_bytes_to_System((const uint8_t*)&m_outgoingMusicCardMessageData,
		                          2);
	}

	// ROM Address: 0x0A82
	void processMusicCardMessagePathParameterStatus()
	{
		log_debug("processMusicCardMessagePathParameterStatus()");
		m_outgoingMusicCardMessageData[0] = 0xD2;
		m_outgoingMusicCardMessageData[1] = m_configuredMidiFlowPath.MidiIn_To_System;
		m_outgoingMusicCardMessageData[2] = m_configuredMidiFlowPath.System_To_MidiOut;
		m_outgoingMusicCardMessageData[3] = m_configuredMidiFlowPath.MidiIn_To_SP;
		m_outgoingMusicCardMessageData[4] = m_configuredMidiFlowPath.System_To_SP;
		m_outgoingMusicCardMessageData[5] = m_configuredMidiFlowPath.MidiIn_To_MidiOut;
		send_card_bytes_to_System((const uint8_t*)&m_outgoingMusicCardMessageData,
		                          6);
	}

	// ROM Address: 0x0A97
	void processMusicCardMessageNodeParameterStatus()
	{
		log_debug("processMusicCardMessageNodeParameterStatus()");
		m_outgoingMusicCardMessageData[0] = 0xD3;
		m_outgoingMusicCardMessageData[1] = m_nodeNumber;
		m_outgoingMusicCardMessageData[2] = m_memoryProtection;
		m_outgoingMusicCardMessageData[3] = m_activeConfigurationNr;
		m_outgoingMusicCardMessageData[4] = m_masterTune;
		m_outgoingMusicCardMessageData[5] = (m_masterOutputLevel ^ 0xFF) &
		                                    0x7F;
		m_outgoingMusicCardMessageData[6] = m_chainMode;
		m_outgoingMusicCardMessageData[7] = 0;
		m_outgoingMusicCardMessageData[8] = 0;
		send_card_bytes_to_System((const uint8_t*)&m_outgoingMusicCardMessageData,
		                          9);
	}

	// ROM Address: 0x0AC9
	void processMusicCardMessageSelectCardMode()
	{
		log_debug("IMFC: processMusicCardMessageSelectCardMode() - start");
		if (m_incomingMusicCardMessageData[1] >= 2) {
			return;
		}
		m_cardMode = m_incomingMusicCardMessageData[1] == 0 ? MUSIC_MODE
		                                                    : THRU_MODE;
		if (m_cardMode == MUSIC_MODE) {
			log_info("IMFC: Restarting in music mode");
			restartInMusicMode();
		} else {
			log_info("IMFC: Restarting in THRU mode");
			restartInThruMode();
		}
	}

	// ROM Address: 0x0AD9
	void processMusicCardMessageSelectErrorReportMode()
	{
		log_debug("IMFC: processMusicCardMessageSelectErrorReportMode() - start");
		if (m_incomingMusicCardMessageData[1] >= 2) {
			return;
		}
		m_errorReport = m_incomingMusicCardMessageData[1] == 0
		                      ? ERROR_REPORTING_DISABLED
		                      : ERROR_REPORTING_ENABLED;
		m_outgoingMusicCardMessageData[0] = 0xE1;
		send_card_bytes_to_System((const uint8_t*)&m_outgoingMusicCardMessageData,
		                          1);
		log_debug("IMFC: processMusicCardMessageSelectErrorReportMode() - end");
	}

	// ROM Address: 0x0AEC
	void processMusicCardMessageSetPaths()
	{
		log_debug("IMFC: processMusicCardMessageSetPaths() - start");
		m_configuredMidiFlowPath.MidiIn_To_System =
		        m_incomingMusicCardMessageData[1] & 0x1F;
		m_configuredMidiFlowPath.System_To_MidiOut =
		        m_incomingMusicCardMessageData[2] & 0x1F;
		m_configuredMidiFlowPath.MidiIn_To_SP =
		        m_incomingMusicCardMessageData[3] & 0x1F;
		m_configuredMidiFlowPath.System_To_SP =
		        m_incomingMusicCardMessageData[4] &
		        ((m_configuredMidiFlowPath.MidiIn_To_SP & 0x10) != 0 ? 0xF
		                                                             : 0x1F);
		m_configuredMidiFlowPath.MidiIn_To_MidiOut =
		        m_incomingMusicCardMessageData[5] & 0x1F;
		log_debug("IMFC: processMusicCardMessageSetPaths() - setNodeParameter - start");
		setNodeParameter(0x25, m_chainMode);
		log_debug("IMFC: processMusicCardMessageSetPaths() - setNodeParameter - end");
		m_outgoingMusicCardMessageData[0] = 0xE2;
		send_card_bytes_to_System((const uint8_t*)&m_outgoingMusicCardMessageData,
		                          1);
		log_debug("IMFC: processMusicCardMessageSetPaths() - end");
	}

	// ROM Address: 0x0B2E
	void processMusicCardMessageSetNodeParameters()
	{
		log_debug("processMusicCardMessageSetNodeParameters()");
		setNodeParameter(0x20, m_incomingMusicCardMessageData[1]);
		setNodeParameter(0x21, m_incomingMusicCardMessageData[2]);
		setNodeParameter(0x22, m_incomingMusicCardMessageData[3]);
		setNodeParameter(0x23, m_incomingMusicCardMessageData[4]);
		setNodeParameter(0x24, m_incomingMusicCardMessageData[5]);
		setNodeParameter(0x25, m_incomingMusicCardMessageData[6]);
		SoundProcessor_SetToInitialState();
		m_outgoingMusicCardMessageData[0] = 0xE3;
		send_card_bytes_to_System((const uint8_t*)&m_outgoingMusicCardMessageData,
		                          1);
	}

	// ROM Address: 0x0B71
	void processMusicCardMessage1E4()
	{
		log_debug("processMusicCardMessage1E4()");
		if (m_cardMode == MUSIC_MODE) {
			sendActiveSenseCodeSafe();
		}
		m_outgoingMusicCardMessageData[0] = 0xE4;
		send_card_bytes_to_System((const uint8_t*)&m_outgoingMusicCardMessageData,
		                          1);
	}

	// ROM Address: 0x0B82
	void processMusicCardMessageReboot()
	{
		log_debug("processMusicCardMessageReboot()");
		disableInterrupts();
		hardReboot();
	}

	// ROM Address: 0x0B86
	void processMusicCardMessageDebugWriteToMemory()
	{
		log_error("processMusicCardMessageDebugWriteToMemory()");
		// THIS WILL NEVER EVER BE IMPLEMENTED !!
	}

	// ROM Address: 0x0BF8
	void initInterruptHandler()
	{
		// byte_FFFC = 0xC3;
		// word_FFFD = &interruptHandler;
		reset_ym2151();
		reset_midi_device();
	}

	// ROM Address: 0x0C3F
	void reset_ym2151()
	{
		for (uint8_t i = 0; i < 0x20; i++) {
			sendToYM2151_with_disabled_interrupts(0x60 + i, 0x7F); // TotalLevel=0x7F(mute)
			delayExEx();
		}
		for (uint8_t i = 0; i < 8; i++) {
			sendToYM2151_with_disabled_interrupts(0x00 + i, 0x10); // ???
			delayExEx();
		}
		// set ClockA to 0x0063(=99d) -> TA(ms) = 64*(1024-99)/KHz =
		// 59200/Khz =
		sendToYM2151_with_disabled_interrupts(0x10, 0x63);
		delayExEx();
		sendToYM2151_with_disabled_interrupts(0x11, 0x00);
		delayExEx();
		// set ClockB to 0xC5(=197) -> TB(ms) = 1024*(256-197)/KHz =
		// 60416/Khz =
		sendToYM2151_with_disabled_interrupts(0x12, 0xC5);
		initialize_ym2151_timers();
	}

	// ROM Address: 0x0C74
	void initialize_ym2151_timers()
	{
		m_ya2151_timerA_counter = 0;
		m_ya2151_timerB_counter = 0;
		m_musicProcessingDepth  = 0;
		m_ym2151_IRQ_stuff      = 0x3F;
		sendToYM2151_with_disabled_interrupts(0x14,
		                                      m_ym2151_IRQ_stuff); // ya2151:
		                                                           // reset
		                                                           // the
		                                                           // timers
		                                                           // and
		                                                           // enable
		                                                           // IRQs
	}

	// ROM Address: 0x0C89
	[[maybe_unused]] void process_ya2151_interrupts_callInInterruptHandler(uint8_t irqMask)
	{
		if ((irqMask & 1) != 0) {
			// TimerA triggered an IRQ
			m_ya2151_timerA_counter++;
			m_runningCommandOnMidiInTimerCountdown--;
			m_runningCommandOnSystemInTimerCountdown--;
			m_sendDataToSystemTimoutCountdown--;
			decreaseMidiInActiveSenseCounter();
			sendActiveSenseCodeToMidiOutAfterTimeout();
			decreaseReadMidiDataTimeout();
		}
		if ((irqMask & 2) != 0) {
			m_ya2151_timerB_counter++;
		}
		if (m_musicProcessingDepth != 0) {
			return;
		}
		m_musicProcessingDepth++;
		finalizeMusicProcessing();
	}

	// ROM Address: 0x0CB7
	void finalizeMusicProcessing()
	{
		while (keepRunning.load()) {
			disableInterrupts();
			if (m_ya2151_timerA_counter != 0U) {
				m_ya2151_timerA_counter--;
				enableInterrupts();
				ym_updateAllCurrentlyPlayingByPortamentoAdjustment();
				if (m_ya2151_timerA_counter == 0) {
					ym_updateKeyCodeAndFractionOnAllChannels();
				}
			} else if (m_ya2151_timerB_counter != 0U) {
				m_ya2151_timerB_counter--;
				enableInterrupts();
				logSuccess();
			} else {
				m_musicProcessingDepth--;
				return;
			}
		}
	}

	// ROM Address: 0x0CEC
	void startMusicProcessing()
	{
		disableInterrupts();
		m_musicProcessingDepth++;
		enableInterrupts();
	}

	// ROM Address: 0x0CF6
	void stopMusicProcessing()
	{
		disableInterrupts();
		m_musicProcessingDepth--;
		if (m_musicProcessingDepth == 0) {
			m_musicProcessingDepth = 1;
			finalizeMusicProcessing();
		}
		enableInterrupts();
	}

	// ROM Address: 0x0D10
	void reset_midi_device()
	{
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(0);
		thread_release_mutex(m_hardwareMutex);
		delayExEx();
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(0);
		thread_release_mutex(m_hardwareMutex);
		delayExEx();
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(0);
		thread_release_mutex(m_hardwareMutex);
		delayExEx();
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(0x40);
		thread_release_mutex(m_hardwareMutex);
		delayExEx();
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(0x4E);
		thread_release_mutex(m_hardwareMutex);
		delayExEx();
		m_midiTransmitReceiveFlag = 4;
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(0x14);
		thread_release_mutex(m_hardwareMutex);
		resetMidiOutBuffersAndPorts();
		resetMidiInBuffersAndPorts();
	}

	// ROM Address: 0x0D39
	void resetMidiInBuffersAndPorts()
	{
		m_bufferFromMidiInState.reset();
		// clear all the error flags on the midi controller
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(m_midiTransmitReceiveFlag | 0x10);
		thread_release_mutex(m_hardwareMutex);
		m_bufferFromMidiIn_lastActiveSenseCodeCountdown = 0;
	}

	// ROM Address: 0x0D55
	void readMidiInPortDuringInterruptHandler()
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t midiData   = PD71051::readPort1();
		const uint8_t midiStatus = m_midi.readPort2();
		thread_release_mutex(m_hardwareMutex);
		if ((midiStatus & 0x38) != 0) { // 0x38 = 00111000 :
			                        // bit5(FramingError),
			                        // bit4(OverrunError),
			                        // bit3(ParityError)
			resetMidiInBuffersAndPorts();
			m_bufferFromMidiInState.setReadErrorFlag();
			return;
		}
		if (m_cardMode == MUSIC_MODE) {
			if (midiData == 0xFE) {
				m_bufferFromMidiIn_lastActiveSenseCodeCountdown = 0x20;
				return;
			}
			resetMidiInActiveSenseCodeCountdownIfZero();
		}
		m_bufferFromMidiInState.pushData(midiData);
		// m_bufferFromMidiIn[m_bufferFromMidiInState.getIndexForNextWriteByte()]
		// = midiData;
		// m_bufferFromMidiInState.increaseIndexForNextWriteByte();
		// m_bufferFromMidiInState.setDataAdded();
		if (m_bufferFromMidiInState.getIndexForNextWriteByte() ==
		    m_bufferFromMidiInState.getLastReadByteIndex()) {
			// oops... we just wrote into the data that hans't
			// already been read :(
			resetMidiInBuffersAndPorts();
			m_bufferFromMidiInState.setOverflowErrorFlag();
		}
	}

	// ROM Address: 0x0DA0
	ReadResult midiIn_readMidiDataByte()
	{
		disableInterrupts();
		if (m_bufferFromMidiInState.hasError()) {
			resetMidiInBuffersAndPorts();
			enableInterrupts();
			reportErrorIfNeeded(
			        m_bufferFromMidiInState.hasReadError() ? MIDI_RECEPTION_ERROR
			        : m_bufferFromMidiInState.hasOverflowError()
			                ? FIFO_OVERFLOW_ERROR_MIDI_TO_MUSICCARD
			                : MIDI_OFFLINE_ERROR);
			enableInterrupts();
			return {ReadStatus::Error,
			        m_bufferFromMidiInState.getFlagsByteValue()};
		}
		if (m_bufferFromMidiInState.isEmpty()) {
			enableInterrupts();
			return {ReadStatus::NoData,
			        m_bufferFromMidiInState.getFlagsByteValue()};
		}
		const uint8_t midiData = m_bufferFromMidiInState.popData();
		// uint8_t midiData =
		// m_bufferFromMidiIn[m_bufferFromMidiInState.getLastReadByteIndex()];
		// m_bufferFromMidiInState.increaseLastReadByteIndex();
		// if (m_bufferFromMidiInState.getLastReadByteIndex() ==
		// m_bufferFromMidiInState.getIndexForNextWriteByte()) {
		//	m_bufferFromMidiInState.setDataCleared();
		// }
		enableInterrupts();
		return {ReadStatus::Success, midiData};
	}

	// ROM Address: 0x0DF4
	void resetMidiInActiveSenseCodeCountdownIfZero()
	{
		if (m_bufferFromMidiIn_lastActiveSenseCodeCountdown != 0U) {
			return;
		}
		m_bufferFromMidiIn_lastActiveSenseCodeCountdown = 0x20;
	}

	// ROM Address: 0x0DFF
	void decreaseMidiInActiveSenseCounter()
	{
		if (m_bufferFromMidiIn_lastActiveSenseCodeCountdown == 0) {
			return;
		}
		m_bufferFromMidiIn_lastActiveSenseCodeCountdown--;
		if (m_bufferFromMidiIn_lastActiveSenseCodeCountdown != 0U) {
			return;
		}
		// active sense was not received within 300ms -> flag it
		m_bufferFromMidiInState.setOfflineErrorFlag();
	}

	// ROM Address: 0x0E0F
	void resetMidiOutBuffersAndPorts()
	{
		m_bufferToMidiOutState.reset();
		m_midiOut_CommandInProgress            = 0;
		m_runningCommandOnMidiInTimerCountdown = 10;
		m_midiTransmitReceiveFlag &= 0xFE; // clear bit0 - disable
		                                   // transmission
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(m_midiTransmitReceiveFlag);
		thread_release_mutex(m_hardwareMutex);
		sendActiveSenseCode();
	}

	// ROM Address: 0x0E36
	void writeMidiOutPortDuringInterruptHandler()
	{
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort1(m_bufferToMidiOutState.popData());
		thread_release_mutex(m_hardwareMutex);
		// m_midi.writePort1(m_bufferToMidiOut[m_bufferToMidiOutState.getLastReadByteIndex()]);
		// m_bufferToMidiOutState.increaseLastReadByteIndex();
		// if (m_bufferToMidiOutState.getLastReadByteIndex() ==
		// m_bufferToMidiOutState.getIndexForNextWriteByte()) {
		//	m_bufferToMidiOutState.setDataCleared();
		// }
		m_midiTransmitReceiveFlag &= 0xFE; // Transmit Disable
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(m_midiTransmitReceiveFlag);
		thread_release_mutex(m_hardwareMutex);
		initMidiOutActiveSensingCountdownMusicMode();
	}

	// ROM Address: 0x0E5B
	void send_midi_byte_to_MidiOut(uint8_t midiData)
	{
		if (midiData <= 0xF7 && midiData >= 0x80) {
			if (midiData <= 0xF0) {
				m_midiOut_CommandInProgress = midiData;
			} else {
				m_midiOut_CommandInProgress = 0;
			}
		}
		midi_raw_out_byte(midiData);

		/* FIXME: remove when midi out and timeouts are done

		m_bufferToMidiOutState.lock();
		while (m_bufferToMidiOutState.isBufferFull()) {
		        m_bufferToMidiOutState.unlock();
		        m_bufferToMidiOutState.lock();
		}
		m_bufferToMidiOutState.pushData(midiData);
		m_midiTransmitReceiveFlag |= 1; // Transmit Enable
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(m_midiTransmitReceiveFlag);
		thread_release_mutex(m_hardwareMutex);
		m_bufferToMidiOutState.unlock();

		end of FIXME
		*/

		// do {
		//	enableInterrupts();
		//	delayNop();
		//	disableInterrupts();
		//	m_bufferToMidiOut[m_bufferToMidiOutState.getIndexForNextWriteByte()]
		//= midiData; } while
		// (m_bufferToMidiOutState.getLastReadByteIndex() ==
		// m_bufferToMidiOutState.getIndexForNextWriteByte() + 1);
		// m_bufferToMidiOutState.increaseIndexForNextWriteByte();
		// m_bufferToMidiOutState.setDataAdded();
		// m_midiTransmitReceiveFlag |= 1; // Transmit Enable
		// m_midi.writePort2(m_midiTransmitReceiveFlag);
		// enableInterrupts();
	}

	// ROM Address: 0x0E9B
	void sendMidiResponse_to_MidiOut(const uint8_t* data, uint8_t size)
	{
		auto data_ptr                 = data;
		const uint8_t midiCommandByte = *data_ptr;
		if (midiCommandByte < 0xF7) {
			if (midiCommandByte < 0x80) {
				return;
			} // first byte must be a midi command
			send_F7_to_MidiOut_if_needed();
			if (*data_ptr != m_midiOut_CommandInProgress) {
				send_midi_byte_to_MidiOut(*data_ptr);
			}
		} else if (midiCommandByte == 0xF7) {
			return send_F7_to_MidiOut_if_needed();
		} else if (midiCommandByte == 0xFF) {
			send_F0_43_75_70_to_MidiOut();
		} else if (midiCommandByte == 0xFE) {
			send_F0_43_75_71_to_MidiOut();
		} else {
			return;
		}
		data_ptr++;
		size--;
		while (size != 0U) {
			send_midi_byte_to_MidiOut(*data_ptr);
			data_ptr++;
			size--;
		}
		m_runningCommandOnMidiInTimerCountdown = 0x0A;
	}

	// ROM Address: 0x0ED6
	void send_F7_to_MidiOut_if_needed()
	{
		// TODO: not completely sure about this return logic
		if (m_midiOut_CommandInProgress != 0xF0 &&
		    m_midiOut_CommandInProgress < 0xFE) {
			return;
		}
		send_midi_byte_to_MidiOut(0xF7);
	}

	// ROM Address: 0x0EE5
	void send_F0_43_75_70_to_MidiOut()
	{
		if (m_midiOut_CommandInProgress == 0xFF) {
			return;
		}
		send_F7_to_MidiOut_if_needed();
		send_midi_byte_to_MidiOut(0xF0);
		send_midi_byte_to_MidiOut(0x43);
		send_midi_byte_to_MidiOut(0x75);
		send_midi_byte_to_MidiOut(0x70);
		m_midiOut_CommandInProgress = 0xFF;
	}

	// ROM Address: 0x0F08
	void send_F0_43_75_71_to_MidiOut()
	{
		if (m_midiOut_CommandInProgress == 0xFE) {
			return;
		}
		send_F7_to_MidiOut_if_needed();
		send_midi_byte_to_MidiOut(0xF0);
		send_midi_byte_to_MidiOut(0x43);
		send_midi_byte_to_MidiOut(0x75);
		send_midi_byte_to_MidiOut(0x71);
		m_midiOut_CommandInProgress = 0xFE;
	}

	// ROM Address: 0x0F2B
	void send_F7_to_MidiOut_if_timed_out()
	{
		if (m_runningCommandOnMidiInTimerCountdown < 0x0B) {
			return;
		}
		m_runningCommandOnMidiInTimerCountdown = 0x0A;
		if (m_midiOut_CommandInProgress >= 0xFE) {
			return send_midi_byte_to_MidiOut(0xF7);
		}
		if (m_midiOut_CommandInProgress == 0xF0) {
			return;
		}
		m_midiOut_CommandInProgress = 0;
	}

	// ROM Address: 0x0F4B
	void sendActiveSenseCodeSafe()
	{
		wait(3);
		disableInterrupts();
		if ((m_activeSenseSendingState & 1) == 0) {
			m_activeSenseSendingState |= 1;
			sendActiveSenseCode();
			m_midiOutActiveSensingCountdown = 0;
			enableInterrupts();
			wait(5000);
			disableInterrupts();
			resetMidiOutBuffersAndPorts();
			m_activeSenseSendingState &= 0xFE;
		}
		enableInterrupts();
	}

	// ROM Address: 0x0F74
	static void wait(uint16_t delayCounter)
	{
		do {
			uint8_t i = 61;
			while ((i--) != 0U) {
				;
			}
		} while (--delayCounter != 0U);
	}

	// ROM Address: 0x0F7E
	void sendActiveSenseCodeToMidiOutAfterTimeout()
	{
		if (m_midiOutActiveSensingCountdown == 0) {
			return;
		}
		if (--m_midiOutActiveSensingCountdown != 0U) {
			sendActiveSenseCodeToMidiOut();
		}
	}

	// ROM Address: 0x0F8A
	void sendActiveSenseCode()
	{
		switch (m_cardMode) {
		case MUSIC_MODE: sendActiveSenseCodeToMidiOut(); break;
		case THRU_MODE: m_midiOutActiveSensingCountdown = 0; break;
		}
		if (m_cardMode == THRU_MODE) {
			return;
		}
	}

	// ROM Address: 0x0F95
	void sendActiveSenseCodeToMidiOut()
	{
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(5); // Transmit Enable / Receive Enable
		thread_release_mutex(m_hardwareMutex);
		delayExEx();
		thread_wait_mutex(m_hardwareMutex);
		// wait until TxRDY
		while ((m_midi.readPort2() & 1) == 0) {
			thread_release_mutex(m_hardwareMutex);
			thread_wait_mutex(m_hardwareMutex);
		};
		m_midi.writePort1(0xFE); // send "Active Sense" code
		thread_release_mutex(m_hardwareMutex);
		delayExEx();
		thread_wait_mutex(m_hardwareMutex);
		m_midi.writePort2(4); // Transmit Disable / Receive Enable
		thread_release_mutex(m_hardwareMutex);
		initMidiOutActiveSensingCountdown();
	}

	// ROM Address: 0x0FAD
	void initMidiOutActiveSensingCountdownMusicMode()
	{
		if (m_cardMode == MUSIC_MODE) {
			initMidiOutActiveSensingCountdown();
		}
	}

	// ROM Address: 0x0FB2
	void initMidiOutActiveSensingCountdown()
	{
		m_midiOutActiveSensingCountdown = 0x10;
	}

	// ROM Address: 0x0FB8
	void reset_PIU_device()
	{
		thread_wait_mutex(m_hardwareMutex);
		m_piuIMF.setGroupModes(PD71055::MODE1,
		                       PD71055::OUTPUT,
		                       PD71055::OUTPUT,
		                       PD71055::MODE1,
		                       PD71055::INPUT,
		                       PD71055::OUTPUT); // Strange that the
		                                         // grp1LowerBits are
		                                         // output but since it
		                                         // has no impact
		thread_release_mutex(m_hardwareMutex);
		initializePIUOutput();
		initializePIUInput();
	}

	// ROM Address: 0x0FC2
	void sendOrReceiveNextValueToFromSystemDuringInterruptHandler()
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t irqStatus = m_piuIMF.readPortPIU2();
		thread_release_mutex(m_hardwareMutex);
		if ((irqStatus & 0x08) != 0) { // Test for interrupt request on
			                       // group 0 (INT0)
			sendNextValueToSystemDuringInterruptHandler();
		} else if ((irqStatus & 0x01) != 0) { // Test for interrupt
			                              // request on group 1
			                              // (INT1)
			// receiveNextValueFromSystemDuringInterruptHandler();
			// // off-loaded to portWrite, since it is too unreliable
		}
	}

	// ROM Address: 0x0FCF
	void initializePIUInput()
	{
		m_bufferFromSystemState.reset();
		log_debug("IMFC: PIU read interrupt = enabled");
		thread_wait_mutex(m_hardwareMutex);
		m_piuIMF.setPort2Bit(2, true); // Group 1 (INPUT) - Read
		                               // interrupt enable = true
		thread_release_mutex(m_hardwareMutex);
	}

	// ROM Address: 0x0FE0
	void receiveNextValueFromSystemDuringInterruptHandler()
	{
		m_bufferFromSystemState.lock();
		// wow... the 9th bit comes from the midi port :)
		// m_bufferAFromSystem[m_bufferFromSystemState.indexForNextWriteByte]
		// = (m_midi.readPort2() >> 7) ^ 1;

		thread_wait_mutex(m_hardwareMutex);
		const uint16_t data =
		        (static_cast<int>(m_tcr.getDataBit8FromSystem()->getValue())
		         << 8) |
		        m_piuIMF.readPortPIU1(); // FIXME: use the midi port
		// log_debug("PC->IMFC: Reading port data [%03X] and adding to
		// queue", data);
		m_bufferFromSystemState.pushData(data);
		thread_release_mutex(m_hardwareMutex);
		if (m_bufferFromSystemState.getLastReadByteIndex() ==
		    m_bufferFromSystemState.getIndexForNextWriteByte()) {
			thread_wait_mutex(m_hardwareMutex);
			m_piuIMF.setPort2Bit(2, false); // Group 1 (INPUT) -
			                                // Read interrupt enable
			                                // = false
			thread_release_mutex(m_hardwareMutex);
		}
		// clang-format off
		//m_bufferAFromSystem[m_bufferFromSystemState.getIndexForNextWriteByte()] = m_tcr.getDataBit8FromSystem()->getValue(); // FIXME: revert to midi port
		//m_bufferBFromSystem[m_bufferFromSystemState.getIndexForNextWriteByte()] = m_piuIMF.readPortPIU1();
		//log("PC->IMF(1/2): Reading port data [%X%02X] and adding to queue[%02X]", m_bufferAFromSystem[m_bufferFromSystemState.getIndexForNextWriteByte()], m_bufferBFromSystem[m_bufferFromSystemState.getIndexForNextWriteByte()], m_bufferFromSystemState.getIndexForNextWriteByte());
		//m_bufferFromSystemState.increaseIndexForNextWriteByte();
		////log("PC->IMF(2/2): m_bufferFromSystemState.indexForNextWriteByte = %02X / (lastReadByteIndex = %02X)", m_bufferFromSystemState.getIndexForNextWriteByte(), m_bufferFromSystemState.getLastReadByteIndex());
		//if (m_bufferFromSystemState.getLastReadByteIndex() == m_bufferFromSystemState.getIndexForNextWriteByte()) {
		//	m_piuIMF.setPort2Bit(2, 0); // Group 1 (INPUT) - Read interrupt enable = false
		//}
		//m_bufferFromSystemState.setDataAdded();
		// clang-format on

		m_bufferFromSystemState.unlock();
	}

	// ROM Address: 0x1006
	// This is more: advance to next system data and peek at value
	SystemDataAvailability system_isMidiDataAvailable()
	{
		m_bufferFromSystemState.lock();
		if (m_bufferFromSystemState.isEmpty()) {
			m_bufferFromSystemState.unlock();
			return NoDataAvailable;
		}
		// disableInterrupts();
		const auto [dataA, dataB] = split_uint16_t(
		        m_bufferFromSystemState.peekData());

		if ((dataA & 1) == 0) {
			// if
			// ((m_bufferAFromSystem[m_bufferFromSystemState.getLastReadByteIndex()]
			// & 1) == 0) {
			//  current data is not system data
			m_bufferFromSystemState.unlock(); // enableInterrupts();
			return MidiDataAvailable;
		}
		// uint8_t dataB =
		// m_bufferBFromSystem[m_bufferFromSystemState.getLastReadByteIndex()];
		if (dataB < 0xF0) {
			m_bufferFromSystemState.unlock(); // enableInterrupts();
			return SystemDataAvailable;
		}
		log_error("WOW: found some strange data in the system buffer (%02X). Discarding :(",
		          dataB);
		m_bufferFromSystemState.popData();

		// clang-format off
		//m_bufferFromSystemState.increaseLastReadByteIndex();
		///* The following code is not in the original */
		//if (m_bufferFromSystemState.getIndexForNextWriteByte() == m_bufferFromSystemState.getLastReadByteIndex()) {
		//	m_bufferFromSystemState.setDataCleared();
		//}
		///* end */
		// clang-format on

		m_bufferFromSystemState.unlock(); // enableInterrupts();
		return system_isMidiDataAvailable();
	}

	// ROM Address: 0x1032
	SystemReadResult system_read9BitMidiDataByte()
	{
		m_bufferFromSystemState.lock();
		if (!m_bufferFromSystemState.hasData()) {
			m_bufferFromSystemState.unlock();
			return {NoDataAvailable, 0};
		}
		// disableInterrupts();
		const auto [a, b] = split_uint16_t(m_bufferFromSystemState.popData());

		// clang-format off
		//log("DEBUG2(1/2): m_bufferFromSystemState: lastReadByteIndex=%02X indexForNextWriteByte=%02X", m_bufferFromSystemState.getLastReadByteIndex(), m_bufferFromSystemState.getIndexForNextWriteByte());
		//uint8_t a = m_bufferAFromSystem[m_bufferFromSystemState.getLastReadByteIndex()];
		//uint8_t b = m_bufferBFromSystem[m_bufferFromSystemState.getLastReadByteIndex()];
		//m_bufferFromSystemState.increaseLastReadByteIndex();
		////log("DEBUG2(2/2): m_bufferFromSystemState.lastReadByteIndex = %02X / (indexForNextWriteByte = %02X)", m_bufferFromSystemState.getLastReadByteIndex(), m_bufferFromSystemState.getIndexForNextWriteByte());
		//if (m_bufferFromSystemState.getIndexForNextWriteByte() == m_bufferFromSystemState.getLastReadByteIndex()) {
		//	m_bufferFromSystemState.setDataCleared();
		//}
		//log("IMFC: PIU read interrupt = enabled");
		// clang-format on

		thread_wait_mutex(m_hardwareMutex);
		m_piuIMF.setPort2Bit(2, true); // Group 1 (INPUT) - Read
		                               // interrupt enable = true
		thread_release_mutex(m_hardwareMutex);
		m_bufferFromSystemState.unlock(); // enableInterrupts();
		if ((a & 1) == 0) {
			return {MidiDataAvailable, b};
		}
		if (b < 0xF0) {
			return {SystemDataAvailable, b};
		}
		return system_read9BitMidiDataByte();
	}

	// ROM Address: 0x106E
	void initializePIUOutput()
	{
		m_bufferToSystemState.reset();
		m_system_CommandInProgress               = 0;
		m_runningCommandOnSystemInTimerCountdown = 10;
		thread_wait_mutex(m_hardwareMutex);
		m_piuIMF.setPort2Bit(4, false); // clear the "unused data bit"
		                                // (group 0 - I/O bit 4)
		m_piuIMF.setPort2Bit(5, false); // set extended "bit8" to zero
		m_piuIMF.setPort2Bit(6, false); // Group 0 (OUTPUT) - Write
		                                // interrupt enable = false
		thread_release_mutex(m_hardwareMutex);
	}

	// ROM Address: 0x108F
	void sendNextValueToSystemDuringInterruptHandler()
	{
		m_bufferToSystemState.lock();
		const auto data16u = m_bufferToSystemState.popData();
		const auto [upper_byte, lower_byte] = split_uint16_t(data16u);

		log_debug("IMF->PC: Reading queue data [%X%02X] and sending to port",
		          upper_byte,
		          lower_byte);
		thread_wait_mutex(m_hardwareMutex);
		m_piuIMF.setPort2Bit(5, data16u >= 0x100); // set bit 5 to
		                                           // bufferA value
		m_piuIMF.writePortPIU0(lower_byte);
		thread_release_mutex(m_hardwareMutex);
		if (m_bufferToSystemState.getIndexForNextWriteByte() ==
		    m_bufferToSystemState.getLastReadByteIndex()) {
			thread_wait_mutex(m_hardwareMutex);
			m_piuIMF.setPort2Bit(6, false); // Group 0 (OUTPUT) -
			                                // Write interrupt
			                                // enable = false
			thread_release_mutex(m_hardwareMutex);
		}
		// clang-format off
		//m_piuIMF.setPort2Bit(5, m_bufferAToSystem[m_bufferToSystemState.getLastReadByteIndex()]); // set bit 5 to bufferA value
		//m_piuIMF.writePortPIU0(m_bufferBToSystem[m_bufferToSystemState.getLastReadByteIndex()]);
		//m_bufferToSystemState.increaseLastReadByteIndex();
		//if (m_bufferToSystemState.getIndexForNextWriteByte() == m_bufferToSystemState.getLastReadByteIndex()) {
		//	m_bufferToSystemState.setDataCleared();
		//	m_piuIMF.setPort2Bit(6, 0); // Group 0 (OUTPUT) - Write interrupt enable = false
		//}
		// clang-format on

		m_bufferToSystemState.unlock();
	}

	// ROM Address: 0x10B1
	void reportErrorIfNeeded(MUSICCARD_ERROR_CODE errorCode)
	{
		if (m_errorReport == ERROR_REPORTING_DISABLED) {
			return;
		}
		send9bitDataToSystem_with_timeout(1, errorCode);
	}

	// ROM Address: 0x10BF
	WriteStatus send_card_byte_to_System(uint8_t data)
	{
		if (data >= 0xF0) {
			return send9bitDataToSystem_with_timeout(1, data);
		};
		if (data < 0x80) {
			return send9bitDataToSystem_with_timeout(1, data);
		};
		m_system_CommandInProgress = 0;
		return send9bitDataToSystem_with_timeout(1, data);
	}

	// ROM Address: 0x10D1
	WriteStatus send_midi_byte_to_System_in_THRU_mode(uint8_t data)
	{
		m_system_CommandInProgress = 0;
		return send9bitDataToSystem_with_timeout(0, data);
	}

	// ROM Address: 0x10DB
	WriteStatus send_midi_byte_to_System(uint8_t data)
	{
		if (data >= 0xF8) {
			return send9bitDataToSystem_with_timeout(0, data);
		}
		if (data < 0x80) {
			return send9bitDataToSystem_with_timeout(0, data);
		}
		if (data < 0xF1) {
			return send_midi_command_byte_to_System(data, 0, data);
		}
		m_system_CommandInProgress = 0;
		return send9bitDataToSystem_with_timeout(0, data);
	}

	// ROM Address: 0x10F3
	WriteStatus send_midi_command_byte_to_System(uint8_t midiCommandByte,
	                                             uint8_t dataMSB, uint8_t dataLSB)
	{
		m_system_CommandInProgress = midiCommandByte;
		return send9bitDataToSystem_with_timeout(dataMSB, dataLSB);
	}

	// ROM Address: 0x10F6
	WriteStatus send9bitDataToSystem_with_timeout(uint8_t dataMSB, uint8_t dataLSB)
	{
		log_debug("IMF->PC: Adding data [%X%02X] to queue", dataMSB, dataLSB);
		m_sendDataToSystemTimoutCountdown = 3;
		m_bufferToSystemState.lock();
		while (m_bufferToSystemState.isBufferFull()) {
			m_bufferToSystemState.unlock();
			if (m_sendDataToSystemTimoutCountdown >= 4) {
				// timed out
				initializePIUOutput();
				// enableInterrupts();
				reportErrorIfNeeded(
				        FIFO_OVERFLOW_ERROR_MUSICCARD_TO_SYSTEM);
				return WriteStatus::Error;
			}
			m_bufferToSystemState.lock();
		}
		m_bufferToSystemState.pushData((dataMSB << 8) | dataLSB);

		// clang-format off
		//m_bufferToSystemState.lock();
		//do {
		//	m_bufferToSystemState.unlock();//enableInterrupts();
		//	if (m_sendDataToSystemTimoutCountdown >= 4) {
		//		// timed out
		//		initializePIUOutput();
		//		//enableInterrupts();
		//		reportErrorIfNeeded(FIFO_OVERFLOW_ERROR_MUSICCARD_TO_SYSTEM);
		//		return WriteStatus::Error;
		//	}
		//	m_bufferToSystemState.lock();//disableInterrupts();
		//	m_bufferAToSystem[m_bufferToSystemState.getIndexForNextWriteByte()] = dataMSB;
		//	m_bufferBToSystem[m_bufferToSystemState.getIndexForNextWriteByte()] = dataLSB;
		//} while (m_bufferToSystemState.getLastReadByteIndex() == m_bufferToSystemState.getIndexForNextWriteByte() + 1);
		//m_bufferToSystemState.increaseIndexForNextWriteByte();
		//m_bufferToSystemState.setDataAdded();
		// clang-format on

		m_bufferToSystemState.unlock();
		thread_wait_mutex(m_hardwareMutex);
		m_piuIMF.setPort2Bit(6, true); // Group 0 (OUTPUT) - Write
		                               // interrupt enable = true
		thread_release_mutex(m_hardwareMutex);
		// enableInterrupts();
		return WriteStatus::Success;
	}

	// ROM Address: 0x1133
	WriteStatus sendMidiResponse_to_System(const uint8_t* data, uint8_t size)
	{
		auto data_ptr = data;
		if (*data_ptr < 0xF7) {
			if (*data_ptr < 0x80) {
				return WriteStatus::Success;
			}
			if (send_F7_to_System_if_needed_0() != WriteStatus::Success) {
				return WriteStatus::Error;
			}
			if (m_system_CommandInProgress != *data_ptr) {
				if (send_midi_byte_to_System(*data_ptr) !=
				    WriteStatus::Success) {
					return WriteStatus::Error;
				}
			}
		} else if (*data_ptr == 0xF7) {
			return send_F7_to_System_if_needed_0();
		} else if (*data_ptr == 0xFF) {
			if (send_F0_43_75_70_to_System() != WriteStatus::Success) {
				return WriteStatus::Error;
			}
		} else if (*data_ptr == 0xFE) {
			if (send_F0_43_75_71_to_System() != WriteStatus::Success) {
				return WriteStatus::Error;
			}
		} else {
			return WriteStatus::Success;
		}
		data_ptr++;
		size--;
		while (size != 0U) {
			if (send_midi_byte_to_System(*data_ptr) !=
			    WriteStatus::Success) {
				return WriteStatus::Error;
			}
			data_ptr++;
			size--;
		}
		m_runningCommandOnSystemInTimerCountdown = 0x0A;
		return WriteStatus::Success;
	}

	// ROM Address: 0x1176
	WriteStatus send_F7_to_System_if_needed_0()
	{
		if (m_system_CommandInProgress != 0xF0 &&
		    m_system_CommandInProgress < 0xFE) {
			return WriteStatus::Success;
		}
		return send_midi_byte_to_System(0xF7);
	}

	// ROM Address: 0x1186
	WriteStatus send_F0_43_75_70_to_System()
	{
		if (m_system_CommandInProgress == 0xFF) {
			return WriteStatus::Success;
		}
		if (send_F7_to_System_if_needed_0() != WriteStatus::Success) {
			return WriteStatus::Error;
		}
		if (send_midi_byte_to_System(0xF0) != WriteStatus::Success) {
			return WriteStatus::Error;
		}
		if (send_midi_byte_to_System(0x43) != WriteStatus::Success) {
			return WriteStatus::Error;
		}
		if (send_midi_byte_to_System(0x75) != WriteStatus::Success) {
			return WriteStatus::Error;
		}
		if (send_midi_byte_to_System(0x70) != WriteStatus::Success) {
			return WriteStatus::Error;
		}
		m_system_CommandInProgress = 0xFF;
		return WriteStatus::Success;
	}

	// ROM Address: 0x11AE
	WriteStatus send_F0_43_75_71_to_System()
	{
		if (m_system_CommandInProgress == 0xFE) {
			return WriteStatus::Success;
		}
		if (send_F7_to_System_if_needed_0() != WriteStatus::Success) {
			return WriteStatus::Error;
		}
		if (send_midi_byte_to_System(0xF0) != WriteStatus::Success) {
			return WriteStatus::Error;
		}
		if (send_midi_byte_to_System(0x43) != WriteStatus::Success) {
			return WriteStatus::Error;
		}
		if (send_midi_byte_to_System(0x75) != WriteStatus::Success) {
			return WriteStatus::Error;
		}
		if (send_midi_byte_to_System(0x71) != WriteStatus::Success) {
			return WriteStatus::Error;
		}
		m_system_CommandInProgress = 0xFE;
		return WriteStatus::Success;
	}

	// ROM Address: 0x11D6
	WriteStatus send_card_bytes_to_System(const uint8_t* data, uint8_t size)
	{
		auto data_ptr = data;

		if (*data_ptr < 0x80) {
			return WriteStatus::Success;
		}
		if (*data_ptr >= 0xF0) {
			return WriteStatus::Success;
		}
		if (send_F7_to_System_if_needed() != WriteStatus::Success) {
			return WriteStatus::Error;
		}
		do {
			if (send_card_byte_to_System(*data_ptr) !=
			    WriteStatus::Success) {
				return WriteStatus::Error;
			}
			data_ptr++;
			size--;
		} while (size != 0U);
		m_runningCommandOnSystemInTimerCountdown = 0x0A;
		return WriteStatus::Success;
	}

	// ROM Address: 0x11F2
	WriteStatus send_F7_to_System_if_needed()
	{
		if (m_system_CommandInProgress != 0xF0 &&
		    m_system_CommandInProgress < 0xFE) {
			return WriteStatus::Success;
		}
		return send_midi_byte_to_System(0xF7);
	}

	// ROM Address: 0x1202
	void send_F7_to_System_if_timed_out()
	{
		if (m_runningCommandOnSystemInTimerCountdown < 0x0B) {
			return;
		}
		// oops, we have timed out
		m_runningCommandOnSystemInTimerCountdown = 0x0A;
		if (m_system_CommandInProgress >= 0xFE) {
			send_midi_byte_to_System(0xF7);
			return;
		}
		if (m_system_CommandInProgress == 0xF0) {
			return;
		}
		m_system_CommandInProgress = 0;
	}

	// ROM Address: 0x1222
	void restartIntoCustomMem()
	{
		// NOPE... NEVER...
	}

	// ROM Address: 0x1268
	void logError([[maybe_unused]] const char* errorMsg)
	{
		// this really does nothing except clearing the a-register
	}

	// ROM Address: 0x1268
	void logSuccess() {}

	// ROM Address: 0x126A
	void setNodeParameterNodeNumber(uint8_t val)
	{
		log_debug("setNodeParameterNodeNumber()");
		if (val >= 16) {
			return;
		}
		m_nodeNumber = val;
		ym_key_off_on_all_channels();
	}

	// ROM Address: 0x1274
	void setNodeParameterMemoryProtection(uint8_t val)
	{
		log_debug("setNodeParameterMemoryProtection()");
		if (val >= 2) {
			return;
		}
		m_memoryProtection = val == 0 ? MEMORY_WRITABLE : MEMORY_READONLY;
	}

	// ROM Address: 0x127C
	void storeActiveConfigurationToCustomConfiguration(uint8_t targetConfigNr)
	{
		if (targetConfigNr >= 16) {
			return;
		}
		log_debug("storeActiveConfigurationToCustomConfiguration - copy start");
		for (uint8_t i = 0; i < 8; i++) {
			m_activeConfiguration.instrumentConfigurations[i].copyFrom(
			        &(m_activeInstrumentParameters[i].instrumentConfiguration));
		}
		log_debug("storeActiveConfigurationToCustomConfiguration - copy end");
		getWritableConfigurationData(targetConfigNr)->deepCopyFrom(&m_activeConfiguration);
	}

	// ROM Address: 0x127C
	void setInstrumentParameter_MidiChannelNumber(InstrumentParameters* instr,
	                                              uint8_t newMidiChannel)
	{
		log_debug("setInstrumentParameter_MidiChannelNumber()");
		if (newMidiChannel >= 16) {
			return;
		}
		instr->instrumentConfiguration.midiChannel = newMidiChannel;
		instr->overflowToMidiOut                   = 0;
		if (m_chainMode == CHAIN_MODE_ENABLED) {
			uint8_t overflowToMidiOut = 1;
			for (auto& m_activeInstrumentParameter :
			     m_activeInstrumentParameters) {
				if (m_activeInstrumentParameter
				            .instrumentConfiguration.midiChannel ==
				    newMidiChannel) {
					m_activeInstrumentParameter.overflowToMidiOut =
					        overflowToMidiOut;
					overflowToMidiOut = 0;
				}
			}
		}
		setDefaultInstrumentParameters(instr);
	}

	// ROM Address: 0x12E6
	// instrumentNr=0x00..0x30 instrument nr in CustomBankA, 0x31..0x5F
	// instrument nr in CustomBankB
	void storeInstrumentParametersToCustomBank(InstrumentParameters* instr,
	                                           uint8_t instrumentNr)
	{
		if (instrumentNr >= 0x60) {
			return;
		}
		getCustomVoiceDefinition(instrumentNr)->deepCopyFrom(&(instr->voiceDefinition));
	}

	// ROM Address: 0x12FB
	void ym2151_executeMidiCommand(InstrumentParameters* instr,
	                               uint8_t midiCmdByte, uint8_t midiDataByte0,
	                               uint8_t midiDataByte1)
	{
		startMusicProcessing();
		switch ((midiCmdByte >> 4) & 7) {
		case 0: // 0x8n - Note OFF Message
			log_debug("ym2151_executeMidiCommand - Note OFF Message");
			executeMidiCommand_NoteONOFF(instr,
			                             Note(midiDataByte0),
			                             KeyVelocity(0));
			break;
		case 1: // 0x9n - Note ON/OFF Message
			log_debug("ym2151_executeMidiCommand - Note ON/OFF Message");
			executeMidiCommand_NoteONOFF(instr,
			                             Note(midiDataByte0),
			                             KeyVelocity(midiDataByte1));
			break;
		case 2: // 0xAn - ???
			break;
		case 3: // 0xBn - Channel Mode Message
			log_debug("ym2151_executeMidiCommand - Channel Mode Message");
			switch (midiDataByte0) {
			case 1:
				setInstrumentParameterModulationWheel(instr,
				                                      midiDataByte1);
				break;
			case 2:
				setInstrumentParameterBreathController(instr,
				                                       midiDataByte1);
				break;
			case 4:
				setInstrumentParameterFootController(instr,
				                                     midiDataByte1);
				break;
			case 5:
				setInstrumentParameter_PortamentoTime(instr,
				                                      midiDataByte1);
				logSuccess();
				break;
			case 7:
				setInstrumentParameterVolume(instr, midiDataByte1);
				break;
			case 0x0A:
				setInstrumentParameter_Pan(instr, midiDataByte1);
				logSuccess();
				break;
			case 0x40:
				setInstrumentParameterSustainOnOff(instr,
				                                   midiDataByte1);
				break;
			case 0x41:
				setInstrumentParameterPortamentoOnOff(instr,
				                                      midiDataByte1);
				break;
			case 0x42:
				setInstrumentParameterSostenutoOnOff(instr,
				                                     midiDataByte1);
				break;
			case 0x7B:
				setInstrumentParameter_AllNotesOFF(instr,
				                                   midiDataByte1);
				break; // when midiDataByte1=0, all notes are OFF
			case 0x7E:
				setInstrumentParameter_MonoMode(instr, midiDataByte1);
				logSuccess();
				break; // when midiDataByte1=1, MONO mode is ON
			case 0x7F:
				setInstrumentParameter_PolyMode(instr, midiDataByte1);
				logSuccess();
				break; // when midiDataByte1=0, POLY mode is ON
				       // (This is what the spec says... but not
				       // the code)
			}
			break;
		case 4: // 0xCn - Voice Change Message
			log_debug("ym2151_executeMidiCommand - Voice Change Message");
			setInstrumentParameter_VoiceNumber(instr, midiDataByte0);
			logSuccess();
			break;
		case 5: // 0xDn - After-Touch Message
			log_debug("ym2151_executeMidiCommand - After-Touch Message");
			setInstrumentParameter_AfterTouch(instr, midiDataByte0);
			break;
		case 6: // 0xEn - Pitchbender Message
			log_debug("ym2151_executeMidiCommand - Pitchbender Message");
			executeMidiCommand_PitchBender(
			        instr,
			        PitchbenderValueLSB(midiDataByte0),
			        PitchbenderValueMSB(midiDataByte1));
			break;
		case 7: // 0xFn - These commands shouldn't even come this far
			break;
		}
		stopMusicProcessing();
	}

	// ROM Address: 0x1381
	void setNodeParameterActiveConfigurationNr(uint8_t val)
	{
		log_debug("setNodeParameterActiveConfigurationNr()");
		if (val >= 0x14) {
			return;
		}
		m_activeConfigurationNr = val;
		m_activeConfiguration.deepCopyFrom(getReadOnlyConfigurationData(val));
		proc_1393_called_for_Reboot();
	}

	// ROM Address: 0x1393
	void proc_1393_called_for_Reboot()
	{
		const uint8_t lfoSpeed = m_activeConfiguration.lfoSpeed;
		const uint8_t amplitudeModulationDepth =
		        m_activeConfiguration.amplitudeModulationDepth;
		const uint8_t pitchModulationDepth = m_activeConfiguration.pitchModulationDepth;
		const uint8_t lfoWaveForm = m_activeConfiguration.lfoWaveForm;
		sub_1405(); // b=8, c=7, ix=0xE3A0
		for (int8_t i = 7; i >= 0; i--) {
			loadInstrumentParameters_InstrumentConfiguration(
			        getActiveInstrumentParameters(i),
			        &m_activeConfiguration.instrumentConfigurations[i]);
		}
		setNodeParameterLFOWaveForm(lfoWaveForm);
		setNodeParameterPitchModDepth(pitchModulationDepth);
		setNodeParameterAmpModDepth(amplitudeModulationDepth);
		setNodeParameterLFOSpeed(lfoSpeed);
		stopMusicProcessing();
	}

	// ROM Address: 0x13EB
	void sub_13D1()
	{
		log_debug("sub_13D1 - begin");
		sub_1405();
		for (int8_t i = 7; i >= 0; i--) {
			setInstrumentParameter00_05_safe(
			        getActiveInstrumentParameters(i),
			        &m_activeConfiguration.instrumentConfigurations[i]);
		}
		stopMusicProcessing();
		log_debug("sub_13D1 - end");
	}

	// ROM Address: 0x13EB
	void proc_13EB_called_for_SelectMusicCardMode()
	{
		log_debug("proc_13EB_called_for_SelectMusicCardMode - begin");
		sub_1405();
		for (int8_t i = 7; i >= 0; i--) {
			setInstrumentParameter00to0A_safe(
			        getActiveInstrumentParameters(i),
			        &m_activeConfiguration.instrumentConfigurations[i]);
		}
		stopMusicProcessing();
		log_debug("proc_13EB_called_for_SelectMusicCardMode - end");
	}

	// ROM Address: 0x1405
	void sub_1405()
	{
		startMusicProcessing();
		setNodeParameterNoteNrReceptionMode(
		        m_activeConfiguration.noteNumberReceptionMode);
		for (uint8_t i = 0; i < 8; i++) {
			InstrumentParameters* p = getActiveInstrumentParameters(i);
			YmChannelData* s        = getYmChannelData(i);
			p->channelMask          = 0;
			s->instrumentParameters = nullptr;
		}
		setNodeParameterMasterTune(m_masterTune);
		sendToYM2151_no_interrupts_allowed(0x0F, 0); // setNoiseEnable

		// Note: returns b=8, c=7, ix=0xE3A0
	}

	// ROM Address: 0x1447
	void loadInstrumentParameters_InstrumentConfiguration(
	        InstrumentParameters* instr, InstrumentConfiguration* config)
	{
		log_debug("loadInstrumentParameters_InstrumentConfiguration - begin");
		startMusicProcessing();
		log_debug("loadInstrumentParameters_InstrumentConfiguration - copy begin");
		instr->instrumentConfiguration.copyFrom(config);
		log_debug("loadInstrumentParameters_InstrumentConfiguration - copy end");
		const uint8_t combineMode = m_activeConfiguration.combineMode;
		setNodeParameterCombineMode(0);
		setInstrumentParameter_ForceRefreshOfParam_00_05(instr);
		setNodeParameterCombineMode(combineMode);
		stopMusicProcessing();
		log_debug("loadInstrumentParameters_InstrumentConfiguration - end");
	}

	// ROM Address: 0x146B
	void setInstrumentParameter00_05_safe(InstrumentParameters* instr,
	                                      InstrumentConfiguration* sourceDataOfSize0x0B)
	{
		log_debug("setInstrumentParameter00_05_safe - begin");
		startMusicProcessing();
		instr->instrumentConfiguration.copySpecialFrom(sourceDataOfSize0x0B);
		setInstrumentParameter_ForceRefreshOfParam_00_05(instr);
		stopMusicProcessing();
		log_debug("setInstrumentParameter00_05_safe - end");
	}

	// ROM Address: 0x1481
	void setInstrumentParameter00to0A_safe(InstrumentParameters* instr,
	                                       InstrumentConfiguration* sourceDataOfSize0x0B)
	{
		log_debug("setInstrumentParameter00to0A_safe - begin");
		startMusicProcessing();
		instr->instrumentConfiguration.copySpecialFrom(sourceDataOfSize0x0B);
		setInstrumentParameter_ForceRefreshOfParam_00to0A(instr);
		stopMusicProcessing();
		log_debug("setInstrumentParameter00to0A_safe - end");
	}

	// ROM Address: 0x1499
	void setInstrumentParameter_ForceRefreshOfParam_00_05(InstrumentParameters* instr)
	{
		log_debug("setInstrumentParameter_ForceRefreshOfParam_00_05 - begin");
		setInstrumentParameter(instr, 0, instr->instrumentConfiguration.numberOfNotes);
		setInstrumentParameter(instr, 5, instr->instrumentConfiguration.voiceNumber);
		// common with setInstrumentParameter_ForceRefreshOfParam_00to0A
		setInstrumentParameter(instr, 1, instr->instrumentConfiguration.midiChannel);
		setInstrumentParameter(
		        instr,
		        2,
		        instr->instrumentConfiguration.noteNumberLimitHigh.value);
		setInstrumentParameter(
		        instr,
		        3,
		        instr->instrumentConfiguration.noteNumberLimitLow.value);
		setInstrumentParameter(instr, 6, instr->instrumentConfiguration.detune);
		setInstrumentParameter(instr, 7, instr->instrumentConfiguration.octaveTranspose);
		setInstrumentParameter(
		        instr, 8, instr->instrumentConfiguration.getOutputLevel());
		setInstrumentParameter(instr, 9, instr->instrumentConfiguration.pan);
		setInstrumentParameter(instr,
		                       10,
		                       instr->instrumentConfiguration.lfoEnable);
		log_debug("setInstrumentParameter_ForceRefreshOfParam_00_05 - end");
	}

	// ROM Address: 0x14AB
	void setInstrumentParameter_ForceRefreshOfParam_00to0A(InstrumentParameters* instr)
	{
		log_debug("setInstrumentParameter_ForceRefreshOfParam_00to0A - begin");
		setInstrumentParameter(instr, 0, instr->instrumentConfiguration.numberOfNotes);
		// common with setInstrumentParameter_ForceRefreshOfParam_00_05
		setInstrumentParameter(instr, 1, instr->instrumentConfiguration.midiChannel);
		setInstrumentParameter(
		        instr,
		        2,
		        instr->instrumentConfiguration.noteNumberLimitHigh.value);
		setInstrumentParameter(
		        instr,
		        3,
		        instr->instrumentConfiguration.noteNumberLimitLow.value);
		setInstrumentParameter(instr, 6, instr->instrumentConfiguration.detune);
		setInstrumentParameter(instr, 7, instr->instrumentConfiguration.octaveTranspose);
		setInstrumentParameter(
		        instr, 8, instr->instrumentConfiguration.getOutputLevel());
		setInstrumentParameter(instr, 9, instr->instrumentConfiguration.pan);
		setInstrumentParameter(instr,
		                       10,
		                       instr->instrumentConfiguration.lfoEnable);
		log_debug("setInstrumentParameter_ForceRefreshOfParam_00to0A - end");
	}

	// ROM Address: 0x14F4
	void setNodeParameterName0(uint8_t val)
	{
		log_debug("setNodeParameterName0()");
		m_activeConfiguration.name[0] = val;
	}
	// ROM Address: 0x14F9
	void setNodeParameterName1(uint8_t val)
	{
		log_debug("setNodeParameterName1()");
		m_activeConfiguration.name[1] = val;
	}
	// ROM Address: 0x14FE
	void setNodeParameterName2(uint8_t val)
	{
		log_debug("setNodeParameterName2()");
		m_activeConfiguration.name[2] = val;
	}
	// ROM Address: 0x1503
	void setNodeParameterName3(uint8_t val)
	{
		log_debug("setNodeParameterName3()");
		m_activeConfiguration.name[3] = val;
	}
	// ROM Address: 0x1508
	void setNodeParameterName4(uint8_t val)
	{
		log_debug("setNodeParameterName4()");
		m_activeConfiguration.name[4] = val;
	}
	// ROM Address: 0x150D
	void setNodeParameterName5(uint8_t val)
	{
		log_debug("setNodeParameterName5()");
		m_activeConfiguration.name[5] = val;
	}
	// ROM Address: 0x1512
	void setNodeParameterName6(uint8_t val)
	{
		log_debug("setNodeParameterName6()");
		m_activeConfiguration.name[6] = val;
	}
	// ROM Address: 0x1517
	void setNodeParameterName7(uint8_t val)
	{
		log_debug("setNodeParameterName7()");
		m_activeConfiguration.name[7] = val;
	}

	// ROM Address: 0x151C
	void setNodeParameterCombineMode(uint8_t val)
	{
		log_debug("setNodeParameterCombineMode()");
		if (val >= 2) {
			return;
		}
		m_activeConfiguration.combineMode = val;
	}

	// ROM Address: 0x1524
	// val = 0=All, 1=Even, 2=Odd
	void setNodeParameterNoteNrReceptionMode(uint8_t val)
	{
		log_debug("setNodeParameterNoteNrReceptionMode()");
		if (val >= 3) {
			return;
		}
		m_activeConfiguration.noteNumberReceptionMode = val;
		ym_key_off_on_all_channels();
	}

	void logInstrumentMasks()
	{
		for (uint8_t i = 0; i < 8; i++) {
			InstrumentParameters* instr = getActiveInstrumentParameters(i);
			log_error("     instrument %i (midi channel %i) - mask 0x%02X",
			          i,
			          getMidiChannel(instr),
			          instr->channelMask);
		}
	}

	// ROM Address: 0x152E
	void setInstrumentParameter_NumberOfNotes(InstrumentParameters* instr,
	                                          uint8_t val)
	{
		log_error("setInstrumentParameter_NumberOfNotes(midichannel=%i, %i) - begin",
		          getMidiChannel(instr),
		          val);
		if (val >= 9) {
			return;
		}
		deallocateAssignedChannels(instr);
		instr->instrumentConfiguration.numberOfNotes = val;
		if (val == 0) {
			sub_154F(instr);
			logInstrumentMasks();
			return;
		}
		const ChannelMaskInfo availableChannels = getFreeChannels();
		log_debug("setInstrumentParameter_NumberOfNotes() - getFreeChannels() returned [mask=%02X, freeChannels=%i]",
		          availableChannels.mask,
		          availableChannels.nrOfChannels);
		if (availableChannels.nrOfChannels <
		    instr->instrumentConfiguration.numberOfNotes) {
			log_debug("setInstrumentParameter_NumberOfNotes() - not enough free channels. Calling sub_1555...");
			sub_1555(instr);
			logInstrumentMasks();
			return;
		}
		instr->channelMask =
		        allocateChannels(instr,
		                         availableChannels.mask,
		                         instr->instrumentConfiguration.numberOfNotes)
		                .mask;
		log_debug("setInstrumentParameter_NumberOfNotes() - allocateChannels() returned mask %02X",
		          instr->channelMask);
		sub_154F(instr);
		logInstrumentMasks();
		log_debug("setInstrumentParameter_NumberOfNotes() - end");
	}

	// ROM Address: 0x154F
	void sub_154F(InstrumentParameters* instr)
	{
		setDefaultInstrumentParameters(instr);
		setAllYmRegistersForAssignedChannels(instr);
	}

	// ROM Address: 0x1555
	void sub_1555(InstrumentParameters* instr)
	{
		log_debug("sub_1555() - begin");
		for (uint8_t i = 0; i < 8; i++) {
			deallocateAssignedChannels(getActiveInstrumentParameters(i));
		}
		const ChannelMaskInfo availableChannels = getFreeChannels();
		instr->channelMask =
		        allocateChannels(instr,
		                         availableChannels.mask,
		                         instr->instrumentConfiguration.numberOfNotes)
		                .mask;
		log_debug("sub_1555() - allocateChannels() returned mask %02X for new channel",
		          instr->channelMask);
		sub_154F(instr);
		for (uint8_t i = 0; i < 8; i++) {
			InstrumentParameters* tmpInstr = getActiveInstrumentParameters(
			        i);
			if (tmpInstr != instr &&
			    tmpInstr->instrumentConfiguration.numberOfNotes != 0) {
				const ChannelMaskInfo tmpAvailableChannels =
				        getFreeChannels();
				const ChannelMaskInfo allocatedChannels = allocateChannels(
				        tmpInstr,
				        tmpAvailableChannels.mask,
				        tmpInstr->instrumentConfiguration.numberOfNotes);
				tmpInstr->channelMask = allocatedChannels.mask;
				tmpInstr->instrumentConfiguration.numberOfNotes =
				        allocatedChannels.nrOfChannels;
				log_debug("sub_1555() - allocateChannels() returned mask %02X / NrOfChannels=%i for other channel",
				          tmpInstr->channelMask,
				          allocatedChannels.nrOfChannels);
				sub_154F(tmpInstr);
			}
		}
		log_debug("sub_1555() - end");
	}

	// ROM Address: 0x15AE
	void deallocateAssignedChannels(InstrumentParameters* instr)
	{
		log_debug("deallocateAssignedChannels() - begin");
		setDefaultInstrumentParameters(instr);
		const uint8_t channelMask = instr->channelMask;
		for (uint8_t i = 0; i < 8; i++) {
			if ((channelMask & (1 << i)) != 0) {
				m_ymChannelData[i].instrumentParameters = nullptr;
			}
		}
		log_debug("deallocateAssignedChannels() - channelMask = 0");
		instr->channelMask     = 0;
		instr->lastUsedChannel = 1;
		log_debug("deallocateAssignedChannels() - end");
	}

	// ROM Address: 0x15D6
	ChannelMaskInfo getFreeChannels()
	{
		uint8_t totalMask = 0;
		for (auto& m_activeInstrumentParameter : m_activeInstrumentParameters) {
			totalMask |= m_activeInstrumentParameter.channelMask;
		}
		totalMask ^= 0xFF;
		uint8_t freeChannels = 0;
		for (uint8_t i = 0; i < 8; i++) {
			if ((totalMask & (1 << i)) != 0) {
				freeChannels++;
			}
		}
		return {totalMask, freeChannels};
	}

	// ROM Address: 0x15F2
	ChannelMaskInfo allocateChannels(InstrumentParameters* instr,
	                                 uint8_t freeMask, uint8_t numberOfNotes)
	{
		uint8_t channelMask               = 0;
		uint8_t numberOfAllocatedChannels = 0;
		for (uint8_t i = 0; i < 8; i++) {
			if ((freeMask & (1 << i)) != 0) {
				m_ymChannelData[i].instrumentParameters = instr;
				channelMask |= 1 << i;
				numberOfAllocatedChannels++;
				numberOfNotes--;
				if (numberOfNotes == 0) {
					return {channelMask,
					        numberOfAllocatedChannels};
				}
			}
		}
		return {channelMask, numberOfAllocatedChannels};
	}

	// ROM Address: 0x1622
	void setInstrumentParameter_NoteNumberLimitHigh(InstrumentParameters* instr,
	                                                uint8_t val)
	{
		log_debug("setInstrumentParameter_NoteNumberLimitHigh()");
		instr->instrumentConfiguration.noteNumberLimitHigh.value = val;
		if (val < instr->instrumentConfiguration.noteNumberLimitLow.value) {
			instr->instrumentConfiguration.noteNumberLimitLow.value = val;
		}
		setDefaultInstrumentParameters(instr);
	}

	// ROM Address: 0x1631
	void setInstrumentParameter_NoteNumberLimitLow(InstrumentParameters* instr,
	                                               uint8_t val)
	{
		log_debug("setInstrumentParameter_NoteNumberLimitLow()");
		instr->instrumentConfiguration.noteNumberLimitLow.value = val;
		if (val > instr->instrumentConfiguration.noteNumberLimitHigh.value) {
			instr->instrumentConfiguration.noteNumberLimitHigh.value = val;
		}
		setDefaultInstrumentParameters(instr);
	}

	// ROM Address: 0x1640
	void setInstrumentParameter_MonoMode(InstrumentParameters* instr, uint8_t val)
	{
		if (val == 1) {
			setInstrumentParameter_MonoPolyMode(instr, val);
		}
	}

	// ROM Address: 0x1647
	void setInstrumentParameter_PolyMode(InstrumentParameters* instr,
	                                     uint8_t /*val*/)
	{
		setInstrumentParameter_MonoPolyMode(instr, 0);
	}

	// ROM Address: 0x164B
	void setInstrumentParameter_MonoPolyMode(InstrumentParameters* instr,
	                                         uint8_t val)
	{
		log_debug("setInstrumentParameter_MonoPolyMode()");
		if (val >= 2) {
			return;
		}
		const uint8_t oldMonoPolyMode = instr->instrumentConfiguration.polyMonoMode;
		instr->instrumentConfiguration.polyMonoMode = val;
		if ((oldMonoPolyMode ^ val) != 0) {
			// mode changed
			applyInstrumentConfiguration(instr);
		}
	}

	// ROM Address: 0x165A
	void setInstrumentParameter_LFOLoadEnable(InstrumentParameters* instr,
	                                          uint8_t val)
	{
		log_debug("setInstrumentParameter_LFOLoadEnable()");
		if (val >= 2) {
			return;
		}
		instr->voiceDefinition.setLfoLoadMode(val);
		if (!isLfoModeEnabled(instr)) {
			return;
		}
		setNodeParameterLFOSpeed(instr->voiceDefinition.getLfoSpeed() >> 1);
		setNodeParameterAmpModDepth(
		        instr->voiceDefinition.getAmplitudeModulationDepth());
		setNodeParameterPitchModDepth(
		        instr->voiceDefinition.getPitchModulationDepth());
		setNodeParameterLFOWaveForm(instr->voiceDefinition.getLfoWaveForm());
	}

	// ROM Address: 0x1694
	static bool isLfoModeEnabled(InstrumentParameters* instr)
	{
		return (instr->voiceDefinition.getLfoLoadMode() != 0U) &&
		       (instr->instrumentConfiguration.numberOfNotes != 0U);
	}

	// ROM Address: 0x169E
	void setInstrumentParameter_LFOSpeed(InstrumentParameters* instr, uint8_t val)
	{
		log_debug("setInstrumentParameter_LFOSpeed()");
		instr->voiceDefinition.setLfoSpeed(val << 1);
		if (isLfoModeEnabled(instr)) {
			setNodeParameterLFOSpeed(val);
		}
	}

	// ROM Address: 0x16A7
	void setNodeParameterLFOSpeed(uint8_t val)
	{
		log_debug("setNodeParameterLFOSpeed()");
		m_activeConfiguration.lfoSpeed = val;
		sendToYM2151_no_interrupts_allowed(0x18, val); // setLFOFreq
	}

	// ROM Address: 0x16B2
	void setInstrumentParameter_AmplitudeModulationDepth(InstrumentParameters* instr,
	                                                     uint8_t val)
	{
		log_debug("setInstrumentParameter_AmplitudeModulationDepth()");
		instr->voiceDefinition.setAmplitudeModulationDepth(val);
		if (isLfoModeEnabled(instr)) {
			setNodeParameterAmpModDepth(val);
		}
	}

	// ROM Address: 0x16BF
	void setNodeParameterAmpModDepth(uint8_t val)
	{
		log_debug("setNodeParameterAmpModDepth()");
		m_activeConfiguration.amplitudeModulationDepth = val;
		sendToYM2151_no_interrupts_allowed(0x19, val); // setPhaseDepth
		                                               // or setAmpDepth
	}

	// ROM Address: 0x16CA
	void setInstrumentParameter_PitchModulationDepth(InstrumentParameters* instr,
	                                                 uint8_t val)
	{
		log_debug("setInstrumentParameter_PitchModulationDepth()");
		instr->voiceDefinition.setPitchModulationDepth(val);
		if (isLfoModeEnabled(instr)) {
			setNodeParameterPitchModDepth(val);
		}
	}

	// ROM Address: 0x16D7
	void setNodeParameterPitchModDepth(uint8_t val)
	{
		log_debug("setNodeParameterPitchModDepth()");
		m_activeConfiguration.pitchModulationDepth = val;
		sendToYM2151_no_interrupts_allowed(0x19, val | 0x80); // setPhaseDepth
		                                                      // or
		                                                      // setAmpDepth
	}

	// ROM Address: 0x16E3
	void setInstrumentParameter_LFOWaveform(InstrumentParameters* instr,
	                                        uint8_t val)
	{
		log_debug("setInstrumentParameter_LFOWaveform()");
		if (val >= 4) {
			return;
		}
		instr->voiceDefinition.setLfoWaveForm(val);
		if (isLfoModeEnabled(instr)) {
			setNodeParameterLFOWaveForm(val);
		}
	}

	// ROM Address: 0x16F8
	void setNodeParameterLFOWaveForm(uint8_t val)
	{
		log_debug("setNodeParameterLFOWaveForm()");
		if (val >= 4) {
			return;
		}
		m_activeConfiguration.lfoWaveForm = val;
		sendToYM2151_no_interrupts_allowed(0x1B, val); // setWaveForm
	}

	// ROM Address: 0x1705
	void setInstrumentParameter_LFOSync(InstrumentParameters* instr, uint8_t val)
	{
		log_debug("setInstrumentParameter_LFOSync()");
		if (val >= 2) {
			return;
		}
		instr->voiceDefinition.setLfoSyncMode(val);
		instr->_lfoSyncMode = val & FieldMask::LfoSyncMode;
	}

	// ROM Address: 0x171E
	void setInstrumentParameter_PMDController(InstrumentParameters* instr,
	                                          uint8_t val)
	{
		log_debug("setInstrumentParameter_PMDController()");
		if (val >= 5) {
			return;
		}
		instr->instrumentConfiguration.pmdController = val;
	}

	// ROM Address: 0x1726
	void setInstrumentParameter_AfterTouch(InstrumentParameters* instr, uint8_t val)
	{
		setInstrumentParameterController(instr, 1, val);
	}

	// ROM Address: 0x1729
	void setInstrumentParameterModulationWheel(InstrumentParameters* instr,
	                                           uint8_t val)
	{
		setInstrumentParameterController(instr, 2, val);
	}

	// ROM Address: 0x172C
	void setInstrumentParameterBreathController(InstrumentParameters* instr,
	                                            uint8_t val)
	{
		setInstrumentParameterController(instr, 3, val);
	}

	// ROM Address: 0x172F
	void setInstrumentParameterFootController(InstrumentParameters* instr,
	                                          uint8_t val)
	{
		setInstrumentParameterController(instr, 4, val);
	}

	// ROM Address: 0x1731
	void setInstrumentParameterController(InstrumentParameters* instr,
	                                      uint8_t controlType, uint8_t val)
	{
		if (instr->instrumentConfiguration.pmdController != controlType) {
			return;
		}
		if (instr->instrumentConfiguration.numberOfNotes != 0U) {
			setNodeParameterPitchModDepth(val);
		}
	}

	// ROM Address: 0x173D
	void setInstrumentParameter_AmplitudeModulationSensitivity(
	        InstrumentParameters* instr, uint8_t val)
	{
		log_debug("setInstrumentParameter_AmplitudeModulationSensitivity()");
		if (val >= 4) {
			return;
		}
		instr->voiceDefinition.setAmplitudeModulationSensitivity(val);
		setInstrumentParameter_LFOEnable(instr,
		                                 instr->instrumentConfiguration.lfoEnable);
	}

	// ROM Address: 0x174F
	void setInstrumentParameter_PitchModulationSensitivity(InstrumentParameters* instr,
	                                                       uint8_t val)
	{
		log_debug("setInstrumentParameter_PitchModulationSensitivity()");
		if (val >= 8) {
			return;
		}
		instr->voiceDefinition.setPitchModulationSensitivity(val);
		setInstrumentParameter_LFOEnable(instr,
		                                 instr->instrumentConfiguration.lfoEnable);
	}

	// ROM Address: 0x1764
	void setInstrumentParameter_LFOEnable(InstrumentParameters* instr, uint8_t val)
	{
		log_debug("setInstrumentParameter_LFOEnable()");
		if (val >= 2) {
			return;
		}
		instr->instrumentConfiguration.lfoEnable = val;
		// TODO: the parameter condition seems weird
		sub_1792(instr,
		         0x38,
		         val != 0U ? 0
		                   : instr->voiceDefinition
		                             .getModulationSensitivity()); // set
		                                                           // PhaseModulationSensitivity
		                                                           // (bits6&5&4)
		                                                           // and
		                                                           // AmplitudeModumationSensitivity
		                                                           // (bits1&0)
	}

	// ROM Address: 0x1778
	void setInstrumentParameter_Pan(InstrumentParameters* instr, uint8_t val)
	{
		log_debug("setInstrumentParameter_Pan(0x%02X)", val);
		instr->instrumentConfiguration.pan = val;
		// Start by setting the Left/Right channel enable flags
		uint8_t regValue =
		        (val & 0x60) == 0x00 ? 0b01000000 /*Left channel enabled*/
		        : (val & 0x60) == 0x60
		                ? 0b10000000 /*Right channel enabled*/
		                : 0b11000000 /*Left+Right channel enabled*/;
		regValue |= (uint8_t)(
		        instr->voiceDefinition.getFeedbackLevel() << 3u);
		regValue |= instr->voiceDefinition.getAlgorithm();
		sub_1792(instr, 0x20, regValue);
	}

	// ROM Address: 0x1792
	void sub_1792(InstrumentParameters* instr, uint8_t reg, uint8_t val)
	{
		const uint8_t channelMask = instr->channelMask;
		for (uint8_t i = 0; i < 8; i++) {
			if ((channelMask & (1 << i)) != 0) {
				sendToYM2151_no_interrupts_allowed(reg + i, val);
			}
		}
	}

	// ROM Address: 0x17A0
	void setInstrumentParameter_VoiceBankNumber(InstrumentParameters* instr,
	                                            uint8_t val)
	{
		log_debug("setInstrumentParameter_VoiceBankNumber()");
		if (val >= 7) {
			return;
		}
		if (val == instr->instrumentConfiguration.voiceBankNumber) {
			return;
		}
		instr->instrumentConfiguration.voiceBankNumber = val;
		setInstrumentParameter_VoiceNumber(
		        instr, instr->instrumentConfiguration.voiceNumber);
	}

	// ROM Address: 0x17AE
	void setInstrumentParameter_VoiceNumber(InstrumentParameters* instr,
	                                        uint8_t val)
	{
		log_debug("setInstrumentParameter_VoiceNumber()");
		if (val >= 0x30) {
			return;
		}
		instr->instrumentConfiguration.voiceNumber = val;
		instr->voiceDefinition.deepCopyFrom(
		        getVoiceDefinitionOfSameBank(instr, val));
		applyInstrumentParameter(instr);
	}

	// ROM Address: 0x17C8
	void setInstrumentParameter_VoiceDefinition(InstrumentParameters* instr,
	                                            uint8_t param, uint8_t val)
	{
		// ugly, but that's how they did it
		((uint8_t*)&(instr->voiceDefinition))[param] = val;
		applyVoiceDefinition(instr);
	}

	// ROM Address: 0x17D6
	void applyInstrumentParameter(InstrumentParameters* instr)
	{
		startMusicProcessing();
		applyInstrumentConfiguration(instr);
		applyVoiceDefinition(instr);
		stopMusicProcessing();
	}

	static inline uint8_t addWithUpperBoundary(uint8_t a, uint8_t b,
	                                           uint8_t upperBoundary)
	{
		const auto bounded_sum = std::min(a + b,
		                                  static_cast<int>(upperBoundary));
		return (uint8_t)(bounded_sum);
	}

	// ROM Address: 0x17E2
	void applyVoiceDefinition(InstrumentParameters* instr)
	{
		instr->_lfoSyncMode = instr->voiceDefinition.getLfoSyncMode() &
		                      FieldMask::LfoSyncMode;
		setInstrumentParameter_LFOLoadEnable(
		        instr, instr->voiceDefinition.getLfoLoadMode());
		// loop unrolling here. Original was a loop
		// Note to self: The
		// "instr->voiceDefinition.operators[0].__totalLevel" in the
		// original is getting the entire byte, not just 7bits. But
		// since it is being cropped.. well...
		instr->operator1TotalLevel = addWithUpperBoundary(
		        instr->voiceDefinition.getOperator(0)->getAdditionToTotalLevel(),
		        instr->voiceDefinition.getOperator(0)->getTotalLevel(),
		        0x7F);
		instr->operator2TotalLevel = addWithUpperBoundary(
		        instr->voiceDefinition.getOperator(1)->getAdditionToTotalLevel(),
		        instr->voiceDefinition.getOperator(1)->getTotalLevel(),
		        0x7F);
		instr->operator3TotalLevel = addWithUpperBoundary(
		        instr->voiceDefinition.getOperator(2)->getAdditionToTotalLevel(),
		        instr->voiceDefinition.getOperator(2)->getTotalLevel(),
		        0x7F);
		instr->operator4TotalLevel = addWithUpperBoundary(
		        instr->voiceDefinition.getOperator(3)->getAdditionToTotalLevel(),
		        instr->voiceDefinition.getOperator(3)->getTotalLevel(),
		        0x7F);
		setAllYmRegistersForAssignedChannels(instr);
		if (m_activeConfiguration.combineMode == 0) {
			return;
		}
		setInstrumentParameter_PitchbenderRange(
		        instr, instr->voiceDefinition.getPitchbenderRange());
		setInstrumentParameter_PortamentoTime(
		        instr, instr->voiceDefinition.getPortamentoTime());
		setInstrumentParameter_MonoPolyMode(
		        instr, instr->voiceDefinition.getPolyMonoMode());
		setInstrumentParameter_PMDController(
		        instr, instr->voiceDefinition.getPMDController());
	}

	// ROM Address: 0x1857
	void setAllYmRegistersForAssignedChannels(InstrumentParameters* instr)
	{
		log_debug("setAllYmRegistersForAssignedChannels - begin");
		const uint8_t operatorsEnabled =
		        instr->voiceDefinition.getOperatorsEnabled();
		const uint8_t mask = instr->channelMask;
		for (uint8_t i = 0; i < 8; i++) {
			if ((mask & (1 << i)) != 0) {
				m_ymChannelData[i].channelNumber = i & FieldMask::ChannelNumber;
				m_ymChannelData[i].operatorsEnabled =
				        operatorsEnabled & FieldMask::OperatorsEnabled;
			}
		}
		// quick reference pointers
		const auto voice_operator_0 = instr->voiceDefinition.getOperator(0);
		const auto voice_operator_1 = instr->voiceDefinition.getOperator(1);
		const auto voice_operator_2 = instr->voiceDefinition.getOperator(2);
		const auto voice_operator_3 = instr->voiceDefinition.getOperator(3);

		// send DT1 (Detune1) & MUL (Phase Multiply)
		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0x40, voice_operator_0->getByte3());
		// send for register 40h to 47h

		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0x48, voice_operator_2->getByte3());
		// send for register 48h to 4Fh

		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0x50, voice_operator_1->getByte3());
		// send for register 50h to 57h

		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0x58, voice_operator_3->getByte3());
		// send for register 58h to 5Fh

		// send AMS-EN (Amplitude Modulation Sensitivity Enable) & D1R
		// (First Decay Rate)
		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0xA0, voice_operator_0->getByte5());
		// send for register A0h to A7h

		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0xA8, voice_operator_2->getByte5());
		// send for register A8h to AFh

		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0xB0, voice_operator_1->getByte5());
		// send for register B0h to B7h

		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0xB8, voice_operator_3->getByte5());
		// send for register B8h to BFh

		// send DT2 (Detune2) & D2R (Second Decay Rate)
		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0xC0, voice_operator_0->getByte6());
		// send for register C0h to C7h

		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0xC8, voice_operator_2->getByte6());
		// send for register C8h to CFh

		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0xD0, voice_operator_1->getByte6());
		// send for register D0h to D7h

		sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
		        instr, 0xD8, voice_operator_3->getByte6());
		// send for register D8h to DFh

		setInstrumentParameter_LFOEnable(instr,
		                                 instr->instrumentConfiguration.lfoEnable);
		setInstrumentParameter_Pan(instr, instr->instrumentConfiguration.pan);
		log_debug("setAllYmRegistersForAssignedChannels - end");
	}

	// ROM Address: 0x18CE
	void sendToYM2151_no_interrupts_allowed_ForAllAssignedChannels(
	        InstrumentParameters* instr, uint8_t startReg, uint8_t val)
	{
		const uint8_t mask = instr->channelMask;
		for (uint8_t i = 0; i < 8; i++) {
			if ((mask & (1 << i)) != 0) {
				sendToYM2151_no_interrupts_allowed(startReg + i, val);
			}
		}
	}

	// ROM Address: 0x18DC
	void ym_updateKeyCodeAndFractionOnAllChannels()
	{
		for (auto& i : m_ymChannelData) {
			if (i._flag6) {
				ym_setKeyCodeAndFraction(&i, i.instrumentParameters);
			}
		}
	}

	static constexpr uint8_t YM_KEY_C_SHARP = 0;
	static constexpr uint8_t YM_KEY_D       = 1;
	static constexpr uint8_t YM_KEY_D_SHARP = 2;
	static constexpr uint8_t YM_KEY_E       = 4;
	static constexpr uint8_t YM_KEY_F       = 5;
	static constexpr uint8_t YM_KEY_F_SHARP = 6;
	static constexpr uint8_t YM_KEY_G       = 8;
	static constexpr uint8_t YM_KEY_G_SHARP = 9;
	static constexpr uint8_t YM_KEY_A       = 10;
	static constexpr uint8_t YM_KEY_A_SHARP = 12;
	static constexpr uint8_t YM_KEY_B       = 13;
	static constexpr uint8_t YM_KEY_C       = 14;

#define YM_KEYCODE(octave, key) (((octave) << 4) | (key))

	// ROM Address: 0x18FC
	void ym_setKeyCodeAndFraction(YmChannelData* ymChannelData,
	                              InstrumentParameters* instr)
	{
		static constexpr uint8_t ym2151KeyCodeTable[128] = {
		        YM_KEYCODE(0, YM_KEY_C),
		        YM_KEYCODE(0, YM_KEY_C_SHARP),
		        YM_KEYCODE(0, YM_KEY_D),
		        YM_KEYCODE(0, YM_KEY_D_SHARP),
		        YM_KEYCODE(0, YM_KEY_E),
		        YM_KEYCODE(0, YM_KEY_F),
		        YM_KEYCODE(0, YM_KEY_F_SHARP),
		        YM_KEYCODE(0, YM_KEY_G),
		        YM_KEYCODE(0, YM_KEY_G_SHARP),
		        YM_KEYCODE(0, YM_KEY_A),
		        YM_KEYCODE(0, YM_KEY_A_SHARP),
		        YM_KEYCODE(0, YM_KEY_B),
		        YM_KEYCODE(0, YM_KEY_C),
		        YM_KEYCODE(0, YM_KEY_C_SHARP),
		        YM_KEYCODE(0, YM_KEY_D),
		        YM_KEYCODE(0, YM_KEY_D_SHARP),
		        YM_KEYCODE(0, YM_KEY_E),
		        YM_KEYCODE(0, YM_KEY_F),
		        YM_KEYCODE(0, YM_KEY_F_SHARP),
		        YM_KEYCODE(0, YM_KEY_G),
		        YM_KEYCODE(0, YM_KEY_G_SHARP),
		        YM_KEYCODE(0, YM_KEY_A),
		        YM_KEYCODE(0, YM_KEY_A_SHARP),
		        YM_KEYCODE(0, YM_KEY_B),
		        YM_KEYCODE(0, YM_KEY_C),
		        YM_KEYCODE(1, YM_KEY_C_SHARP),
		        YM_KEYCODE(1, YM_KEY_D),
		        YM_KEYCODE(1, YM_KEY_D_SHARP),
		        YM_KEYCODE(1, YM_KEY_E),
		        YM_KEYCODE(1, YM_KEY_F),
		        YM_KEYCODE(1, YM_KEY_F_SHARP),
		        YM_KEYCODE(1, YM_KEY_G),
		        YM_KEYCODE(1, YM_KEY_G_SHARP),
		        YM_KEYCODE(1, YM_KEY_A),
		        YM_KEYCODE(1, YM_KEY_A_SHARP),
		        YM_KEYCODE(1, YM_KEY_B),
		        YM_KEYCODE(1, YM_KEY_C),
		        YM_KEYCODE(2, YM_KEY_C_SHARP),
		        YM_KEYCODE(2, YM_KEY_D),
		        YM_KEYCODE(2, YM_KEY_D_SHARP),
		        YM_KEYCODE(2, YM_KEY_E),
		        YM_KEYCODE(2, YM_KEY_F),
		        YM_KEYCODE(2, YM_KEY_F_SHARP),
		        YM_KEYCODE(2, YM_KEY_G),
		        YM_KEYCODE(2, YM_KEY_G_SHARP),
		        YM_KEYCODE(2, YM_KEY_A),
		        YM_KEYCODE(2, YM_KEY_A_SHARP),
		        YM_KEYCODE(2, YM_KEY_B),
		        YM_KEYCODE(2, YM_KEY_C),
		        YM_KEYCODE(3, YM_KEY_C_SHARP),
		        YM_KEYCODE(3, YM_KEY_D),
		        YM_KEYCODE(3, YM_KEY_D_SHARP),
		        YM_KEYCODE(3, YM_KEY_E),
		        YM_KEYCODE(3, YM_KEY_F),
		        YM_KEYCODE(3, YM_KEY_F_SHARP),
		        YM_KEYCODE(3, YM_KEY_G),
		        YM_KEYCODE(3, YM_KEY_G_SHARP),
		        YM_KEYCODE(3, YM_KEY_A),
		        YM_KEYCODE(3, YM_KEY_A_SHARP),
		        YM_KEYCODE(3, YM_KEY_B),
		        YM_KEYCODE(3, YM_KEY_C),
		        YM_KEYCODE(4, YM_KEY_C_SHARP),
		        YM_KEYCODE(4, YM_KEY_D),
		        YM_KEYCODE(4, YM_KEY_D_SHARP),
		        YM_KEYCODE(4, YM_KEY_E),
		        YM_KEYCODE(4, YM_KEY_F),
		        YM_KEYCODE(4, YM_KEY_F_SHARP),
		        YM_KEYCODE(4, YM_KEY_G),
		        YM_KEYCODE(4, YM_KEY_G_SHARP),
		        YM_KEYCODE(4, YM_KEY_A),
		        YM_KEYCODE(4, YM_KEY_A_SHARP),
		        YM_KEYCODE(4, YM_KEY_B),
		        YM_KEYCODE(4, YM_KEY_C),
		        YM_KEYCODE(5, YM_KEY_C_SHARP),
		        YM_KEYCODE(5, YM_KEY_D),
		        YM_KEYCODE(5, YM_KEY_D_SHARP),
		        YM_KEYCODE(5, YM_KEY_E),
		        YM_KEYCODE(5, YM_KEY_F),
		        YM_KEYCODE(5, YM_KEY_F_SHARP),
		        YM_KEYCODE(5, YM_KEY_G),
		        YM_KEYCODE(5, YM_KEY_G_SHARP),
		        YM_KEYCODE(5, YM_KEY_A),
		        YM_KEYCODE(5, YM_KEY_A_SHARP),
		        YM_KEYCODE(5, YM_KEY_B),
		        YM_KEYCODE(5, YM_KEY_C),
		        YM_KEYCODE(6, YM_KEY_C_SHARP),
		        YM_KEYCODE(6, YM_KEY_D),
		        YM_KEYCODE(6, YM_KEY_D_SHARP),
		        YM_KEYCODE(6, YM_KEY_E),
		        YM_KEYCODE(6, YM_KEY_F),
		        YM_KEYCODE(6, YM_KEY_F_SHARP),
		        YM_KEYCODE(6, YM_KEY_G),
		        YM_KEYCODE(6, YM_KEY_G_SHARP),
		        YM_KEYCODE(6, YM_KEY_A),
		        YM_KEYCODE(6, YM_KEY_A_SHARP),
		        YM_KEYCODE(6, YM_KEY_B),
		        YM_KEYCODE(6, YM_KEY_C),
		        YM_KEYCODE(7, YM_KEY_C_SHARP),
		        YM_KEYCODE(7, YM_KEY_D),
		        YM_KEYCODE(7, YM_KEY_D_SHARP),
		        YM_KEYCODE(7, YM_KEY_E),
		        YM_KEYCODE(7, YM_KEY_F),
		        YM_KEYCODE(7, YM_KEY_F_SHARP),
		        YM_KEYCODE(7, YM_KEY_G),
		        YM_KEYCODE(7, YM_KEY_G_SHARP),
		        YM_KEYCODE(7, YM_KEY_A),
		        YM_KEYCODE(7, YM_KEY_A_SHARP),
		        YM_KEYCODE(7, YM_KEY_B),
		        YM_KEYCODE(7, YM_KEY_C),
		        YM_KEYCODE(7, YM_KEY_C_SHARP),
		        YM_KEYCODE(7, YM_KEY_D),
		        YM_KEYCODE(7, YM_KEY_D_SHARP),
		        YM_KEYCODE(7, YM_KEY_E),
		        YM_KEYCODE(7, YM_KEY_F),
		        YM_KEYCODE(7, YM_KEY_F_SHARP),
		        YM_KEYCODE(7, YM_KEY_G),
		        YM_KEYCODE(7, YM_KEY_G_SHARP),
		        YM_KEYCODE(7, YM_KEY_A),
		        YM_KEYCODE(7, YM_KEY_A_SHARP),
		        YM_KEYCODE(7, YM_KEY_B),
		        YM_KEYCODE(7, YM_KEY_C),
		        YM_KEYCODE(7, YM_KEY_C_SHARP),
		        YM_KEYCODE(7, YM_KEY_D),
		        YM_KEYCODE(7, YM_KEY_D_SHARP),
		        YM_KEYCODE(7, YM_KEY_E),
		        YM_KEYCODE(7, YM_KEY_F),
		        YM_KEYCODE(7, YM_KEY_F_SHARP),
		        YM_KEYCODE(7, YM_KEY_G)};

		// KC KeyCode KF Key Fraction
		FractionalNote cropped = cropToPlayableRange(
		        ymChannelData->currentlyPlaying,
		        instr->detuneAndPitchbendAsNoteFraction);
		cropped = cropToPlayableRange(cropped, m_masterTuneAsNoteFraction);
		sendToYM2151_with_disabled_interrupts(
		        0x28 + ymChannelData->channelNumber,
		        ym2151KeyCodeTable[cropped.note.value]); // send KC
		                                                 // (KeyCode)
		delayNop();
		delayNop();
		sendToYM2151_no_interrupts_allowed(0x30 + ymChannelData->channelNumber,
		                                   cropped.fraction.value); // send KF (Key Fraction)
	}

	// ROM Address: 0x19B4
	static FractionalNote cropToPlayableRange(const FractionalNote& root /*hl*/,
	                                          const FractionalNote& adjustment /*de*/)
	{
		auto result = (root + adjustment).GetUint16();

		if ((adjustment.note.value & 0x80) != 0) {
			// adjustment is negative

			if (result >= 0x8000) {
				// The resulting note is outside the positive
				// 15-bit range (it's now a negative int16_t
				// value), and needs to be incremented in
				// one-octive steps until it's no longer negative.

				auto signed_result = static_cast<int16_t>(result);
				assert(signed_result < 0);

				while (signed_result < 0) {
					signed_result += (12 << 8); // up an octave
				}
				result = (uint16_t)(signed_result);
				assert(result < 0x8000);
			}
		} else {
			// adjustment is positive
			while (result >= 0x8000) {
				result -= 12 << 8; // drop an octave
			}
		}
		return to_fractional_note(result);
	}

	// ROM Address: 0x19CE
	void setInstrumentParameter_PortamentoTime(InstrumentParameters* instr,
	                                           uint8_t val)
	{
		log_debug("setInstrumentParameter_PortamentoTime()");
		instr->instrumentConfiguration.portamentoTime = val;
		setInstrumentParameterPortamentoOnOff(instr, instr->_portamento);
	}

	// ROM Address: 0x19D3
	// val is either 0 (OFF) or 127 (ON)
	void setInstrumentParameterPortamentoOnOff(InstrumentParameters* instr,
	                                           uint8_t val)
	{
		if (val == 0) {
			instr->_portamento = 0;
		} else if (val == 0x7F) {
			instr->_portamento = 1;
		} else {
			return;
		}

		if (instr->_portamento &&
		    (instr->instrumentConfiguration.portamentoTime != 0U)) {
			return;
		}
		for (uint8_t i = 0; i < 8; i++) {
			if ((instr->channelMask & (1 << i)) != 0) {
				ym_finishPortamento(&m_ymChannelData[i]);
			}
		}
	}

	// ROM Address: 0x1A05
	void ym_registerKey_setKeyCodeAndFraction_IncludingPortamento(
	        InstrumentParameters* instr, YmChannelData* ymChannelData)
	{
		if (instr->_portamento == 0) {
			return ym_registerKey_setKeyCodeAndFraction_Special(instr,
			                                                    ymChannelData);
		}
		if (instr->instrumentConfiguration.portamentoTime == 0) {
			return ym_registerKey_setKeyCodeAndFraction_Special(instr,
			                                                    ymChannelData);
		}
		YmChannelData* tmpYmChannelData = instr->ymChannelData;
		if (m_lastMidiOnOff_FractionAndNoteNumber ==
		    tmpYmChannelData->originalFractionAndNoteNumber) {
			return ym_registerKey_setKeyCodeAndFraction_Special(instr,
			                                                    ymChannelData);
		}
		ymChannelData->currentlyPlaying = tmpYmChannelData->currentlyPlaying;
		if (ymChannelData->portamentoTarget ==
		    tmpYmChannelData->currentlyPlaying) {
			return ym_registerKey_setKeyCodeAndFraction_Special(instr,
			                                                    ymChannelData);
		}
		FractionalNote hl = ymChannelData->portamentoTarget -
		                    tmpYmChannelData->currentlyPlaying;
		if ((hl.note.value & 0x80) == 0) {
			// hl is positive
			ymChannelData->_hasActivePortamento = 1;
			uint16_t val = (hl.note.value >> 1) & 0x3F;
			if (val == 0) {
				val = 1;
			}
			val = val * ym_getPortamentoTimeFactor(instr);
			ymChannelData->portamentoAdjustment = to_fractional_note(val);
			return ym_registerKey_setKeyCodeAndFraction(instr,
			                                            ymChannelData);
		} // hl is negative
		ymChannelData->_hasActivePortamento = 1;
		int16_t val                         = hl.GetUint16();
		val                                 = val >> (8 + 1);
		val = val * ym_getPortamentoTimeFactor(instr);
		ymChannelData->portamentoAdjustment = to_fractional_note(val);
		return ym_registerKey_setKeyCodeAndFraction(instr, ymChannelData);
	}

	// ROM Address: 0x1A8C
	static uint8_t ym_getPortamentoTimeFactor(InstrumentParameters* instr)
	{
		static constexpr uint8_t byte_1A9E[32] = {
		        0xFF, 0x80, 0x56, 0x40, 0x34, 0x2B, 0x25, 0x20,
		        0x1D, 0x1A, 0x18, 0x16, 0x14, 0x13, 0x12, 0x11,
		        0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09,
		        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
		return byte_1A9E[(instr->instrumentConfiguration.portamentoTime >> 2) & 0x1F];
	}

	// ROM Address: 0x1ABE
	void ym_registerKey_setKeyCodeAndFraction_Special(InstrumentParameters* instr,
	                                                  YmChannelData* ymChannelData)
	{
		ymChannelData->_hasActivePortamento = 0;
		ymChannelData->currentlyPlaying = ymChannelData->portamentoTarget;
		ym_registerKey_setKeyCodeAndFraction(instr, ymChannelData);
	}

	// ROM Address: 0x1ACE
	void ym_registerKey_setKeyCodeAndFraction(InstrumentParameters* instr,
	                                          YmChannelData* ymChannelData)
	{
		ymChannelData->originalFractionAndNoteNumber = m_lastMidiOnOff_FractionAndNoteNumber;
		ymChannelData->remainingDuration = m_lastMidiOnOff_Duration;
		instr->ymChannelData             = ymChannelData;
		ym_setKeyCodeAndFraction(ymChannelData, instr);
	}

	// ROM Address: 0x1AEC
	void ym_updateAllCurrentlyPlayingByPortamentoAdjustment()
	{
		startMusicProcessing();
		for (auto& i : m_ymChannelData) {
			if (i._hasActivePortamento && i._flag6) {
				ym_updateCurrentlyPlayingByPortamentoAdjustment(&i);
			}
		}
		stopMusicProcessing();
	}

	// ROM Address: 0x1B10
	void ym_updateCurrentlyPlayingByPortamentoAdjustment(YmChannelData* ymChannelData)
	{
		if ((ymChannelData->portamentoAdjustment.note.value & 0x80) != 0) {
			// portamentoAdjustment is a "negative" note
			FractionalNote newCurrentlyPlaying =
			        ymChannelData->currentlyPlaying +
			        ymChannelData->portamentoAdjustment;
			// did it overflow?
			if (newCurrentlyPlaying.GetUint16() >
			    ymChannelData->currentlyPlaying.GetUint16()) {
				return ym_finishPortamento(ymChannelData);
			}
			ymChannelData->currentlyPlaying = newCurrentlyPlaying;
			if (ymChannelData->currentlyPlaying.GetUint16() <=
			    ymChannelData->portamentoTarget.GetUint16()) {
				return ym_finishPortamento(ymChannelData);
			}
		} else {
			// portamentoAdjustment is a "positive" note
			FractionalNote newCurrentlyPlaying =
			        ymChannelData->currentlyPlaying +
			        ymChannelData->portamentoAdjustment;
			// did it overflow?
			if (newCurrentlyPlaying.GetUint16() <
			    ymChannelData->currentlyPlaying.GetUint16()) {
				return ym_finishPortamento(ymChannelData);
			}
			ymChannelData->currentlyPlaying = newCurrentlyPlaying;
			if (ymChannelData->currentlyPlaying.GetUint16() >=
			    ymChannelData->portamentoTarget.GetUint16()) {
				return ym_finishPortamento(ymChannelData);
			}
		}
	}

	// ROM Address: 0x1B4C
	static void ym_finishPortamento(YmChannelData* ymChannelData)
	{
		ymChannelData->_hasActivePortamento = 0; // reset bit5
		ymChannelData->currentlyPlaying = ymChannelData->portamentoTarget;
	}

	// ROM Address: 0x1B5D
	void setNodeParameterMasterTune(uint8_t val)
	{
		log_debug("setNodeParameterMasterTune()");
		m_masterTune             = val;
		const int16_t masterTune   = ((int8_t)(val << 1)) - 0x1EC;
		m_masterTuneAsNoteFraction = to_fractional_note(masterTune);
	}

	// ROM Address: 0x1B73
	void setInstrumentParameter_Detune(InstrumentParameters* instr, uint8_t val)
	{
		log_debug("setInstrumentParameter_Detune()");
		instr->instrumentConfiguration.detune = val;
		setInstrumentParameter_06_07_common(instr);
	}

	// ROM Address: 0x1B78
	void setInstrumentParameter_OctaveTranspose(InstrumentParameters* instr,
	                                            uint8_t val)
	{
		log_debug("setInstrumentParameter_OctaveTranspose(midichannel=%i, %i)",
		          getMidiChannel(instr),
		          val);
		if (val >= 5) {
			return;
		}
		instr->instrumentConfiguration.octaveTranspose = val;
		setInstrumentParameter_06_07_common(instr);
	}

	// ROM Address: 0x1B7F
	void setInstrumentParameter_06_07_common(InstrumentParameters* instr)
	{
		static constexpr int16_t octaveTransposeTable[5] = {
		        -24 * 256, -12 * 256, 0 * 256, 12 * 256, 24 * 256}; // In
		                                                            // the
		                                                            // original:
		                                                            // actually
		                                                            // a
		                                                            // int8_t

		// if (instr->instrumentConfiguration.octaveTranspose != 2) {
		//	log_error("DEBUG:
		// instr->instrumentConfiguration.octaveTranspose is %i",
		// instr->instrumentConfiguration.octaveTranspose);
		// }

		const int16_t detuneAsNoteFraction =
		        octaveTransposeTable[instr->instrumentConfiguration.octaveTranspose] +
		        ((int16_t)((int8_t)(instr->instrumentConfiguration.detune
		                            << 1)));

		instr->detuneAsNoteFraction = to_fractional_note(detuneAsNoteFraction);

		executeMidiCommand_PitchBender(instr,
		                               instr->pitchbenderValueLSB,
		                               instr->pitchbenderValueMSB);
	}
	// clang-format off
	// ROM Address: 0x1BAD
	// The pitchbender is a type of wheel controller built into a device, such as a synthesizer, used to modify pitch. The pitchbnder range sets
	// the width of pitch fluctuation generated by the pitchbend wheel. The value range is from 0 through 12. When the pitchbender range is set to 0, no pitch fluctuation
	// occurs. Each time the pitchbender parameter is incremeted by one, the variable width expands both a half-step higher and lower. Weh nthe pitchbender parameter is
	// set to 12, the pitchbend is altered plus or minus one cotave.
	// clang-format on

	void setInstrumentParameter_PitchbenderRange(InstrumentParameters* instr,
	                                             uint8_t val)
	{
		log_debug("setInstrumentParameter_PitchbenderRange()");
		if (val > 12) {
			return;
		}
		instr->instrumentConfiguration.pitchbenderRange = val;
		executeMidiCommand_PitchBender(instr,
		                               instr->pitchbenderValueLSB,
		                               instr->pitchbenderValueMSB);
	}

	// ROM Address: 0x1BBA
	// pitchbenderValue 14-bit signed value (-8192 ~ 8192)
	static void executeMidiCommand_PitchBender(InstrumentParameters* instr,
	                                           PitchbenderValueLSB pitchbenderValueLSB,
	                                           PitchbenderValueMSB pitchbenderValueMSB)
	{
		instr->pitchbenderValueLSB = pitchbenderValueLSB;
		instr->pitchbenderValueMSB = pitchbenderValueMSB;
		int16_t pitchbenderValue =
		        (((int16_t)((pitchbenderValueMSB.value << 9) |
		                    (pitchbenderValueLSB.value << 2))) >>
		         2) -
		        0x2000; // convert for 2x7bit->14bit and then offset it
		                // from 0x2000
		// not the exact code, but should do the same thing
		pitchbenderValue = (int16_t)(
		        instr->detuneAsNoteFraction.GetUint16() +
		        instr->instrumentConfiguration.pitchbenderRange *
		                pitchbenderValue / 0x2000);

		instr->detuneAndPitchbendAsNoteFraction = to_fractional_note(
		        pitchbenderValue);
	}

	inline uint8_t getOutputLevel(InstrumentParameters* instr) const
	{
		uint16_t outputLevel =
		        (instr->instrumentConfiguration.getOutputLevel() ^ 0xFF) &
		        0x7F;
		outputLevel += instr->volume;
		outputLevel += m_masterOutputLevel;
		// log_debug("getOutputLevel -
		// instr->instrumentConfiguration.outputLevel=0x%02X,
		// instr->volume=0x%02X, m_masterOutputLevel=0x%02X",
		// (instr->instrumentConfiguration.getOutputLevel() ^ 0xFF) &
		// 0x7F, instr->volume, m_masterOutputLevel);

		constexpr uint16_t max_level = 0x7F;
		return (uint8_t)(std::min(max_level, outputLevel));

		// clang-format off
		// FIXME: Not sure about the signing!! Might be wrong!
		//if (outputLevel + instr->volume < 0x80) {
		//	outputLevel += instr->volume;
		//	if (outputLevel + m_masterOutputLevel < 0x80) {
		//		outputLevel += m_masterOutputLevel;
		//	} else {
		//		outputLevel = 0x7F;
		//	}
		//} else {
		//	outputLevel = 0x7F;
		//}
		//return outputLevel;
		// clang-format on
	}

	// ROM Address: 0x1C15
	void setInstrumentVolume(InstrumentParameters* instr)
	{
		const uint8_t channelMask = instr->channelMask;
		const uint8_t outputLevel = getOutputLevel(instr);
		for (uint8_t i = 0; i < 8; i++) {
			if ((channelMask & (1 << i)) != 0) {
				ym_setOperatorVolumes(instr,
				                      &m_ymChannelData[i],
				                      outputLevel);
			}
		}
	}

	// ROM Address: 0x1C42
	void ym_setOperatorVolumes(InstrumentParameters* instr,
	                           YmChannelData* ymChannelData, uint8_t volume)
	{
		// log_debug("ym_setOperatorVolumes - using volume 0x%02X for
		// channel %i", volume, ymChannelData->channelNumber);
		const uint8_t ymRegister = ymChannelData->channelNumber +
		                           0x60; // TL (Total Level)
		uint8_t operatorVolume = 0;
		// set operator 0
		operatorVolume = ymChannelData->operatorVolumes[0];
		if (instr->voiceDefinition.getOperator(0)->getModulatorCarrierSelect() !=
		    0U) {
			operatorVolume += volume;
			if (operatorVolume >= 0x80) {
				operatorVolume = 0x7f;
			}
		}
		sendToYM2151_no_interrupts_allowed(ymRegister + 0 * 8,
		                                   0x80 | operatorVolume);
		// set operator 2
		operatorVolume = ymChannelData->operatorVolumes[2];
		if (instr->voiceDefinition.getOperator(2)->getModulatorCarrierSelect() !=
		    0U) {
			operatorVolume += volume;
			if (operatorVolume >= 0x80) {
				operatorVolume = 0x7f;
			}
		}
		sendToYM2151_no_interrupts_allowed(ymRegister + 1 * 8,
		                                   0x80 | operatorVolume);
		// set operator 1
		operatorVolume = ymChannelData->operatorVolumes[1];
		if (instr->voiceDefinition.getOperator(1)->getModulatorCarrierSelect() !=
		    0U) {
			operatorVolume += volume;
			if (operatorVolume >= 0x80) {
				operatorVolume = 0x7f;
			}
		}
		sendToYM2151_no_interrupts_allowed(ymRegister + 2 * 8,
		                                   0x80 | operatorVolume);
		// set operator 3
		operatorVolume = ymChannelData->operatorVolumes[3];
		if (instr->voiceDefinition.getOperator(3)->getModulatorCarrierSelect() !=
		    0U) {
			operatorVolume += volume;
			if (operatorVolume >= 0x80) {
				operatorVolume = 0x7f;
			}
		}
		sendToYM2151_no_interrupts_allowed(ymRegister + 3 * 8,
		                                   0x80 | operatorVolume);
	}

	// ROM Address: 0x1C81
	void ym_updateOperatorVolumes(InstrumentParameters* instr,
	                              YmChannelData* ymChannelData)
	{
		const uint8_t volume = getOutputLevel(instr);
		// log_debug("ym_updateOperatorVolumes - using volume 0x%02X for
		// channel %i", volume, ymChannelData->channelNumber);
		const uint8_t ymRegister = ymChannelData->channelNumber +
		                           0x60; // TL (Total Level)
		uint8_t operatorVolume = 0;
		operatorVolume         = ymChannelData->operatorVolumes[0];
		if (instr->voiceDefinition.getOperator(0)->getModulatorCarrierSelect() !=
		    0U) {
			operatorVolume += volume;
			if (operatorVolume >= 0x80) {
				operatorVolume = 0x7f;
			}
		}
		sendToYM2151_no_interrupts_allowed(ymRegister + 0 * 8, operatorVolume);
		// set operator 2
		operatorVolume = ymChannelData->operatorVolumes[2];
		if (instr->voiceDefinition.getOperator(2)->getModulatorCarrierSelect() !=
		    0U) {
			operatorVolume += volume;
			if (operatorVolume >= 0x80) {
				operatorVolume = 0x7f;
			}
		}
		sendToYM2151_no_interrupts_allowed(ymRegister + 1 * 8, operatorVolume);
		// set operator 1
		operatorVolume = ymChannelData->operatorVolumes[1];
		if (instr->voiceDefinition.getOperator(1)->getModulatorCarrierSelect() !=
		    0U) {
			operatorVolume += volume;
			if (operatorVolume >= 0x80) {
				operatorVolume = 0x7f;
			}
		}
		sendToYM2151_no_interrupts_allowed(ymRegister + 2 * 8, operatorVolume);
		// set operator 3
		operatorVolume = ymChannelData->operatorVolumes[3];
		if (instr->voiceDefinition.getOperator(3)->getModulatorCarrierSelect() !=
		    0U) {
			operatorVolume += volume;
			if (operatorVolume >= 0x80) {
				operatorVolume = 0x7f;
			}
		}
		sendToYM2151_no_interrupts_allowed(ymRegister + 3 * 8, operatorVolume);
	}

	// ROM Address: 0x1CD5
	void ym_calculateAndUpdateOperatorVolumes(InstrumentParameters* instr,
	                                          YmChannelData* ymChannelData)
	{
		const uint8_t h = (ymChannelData->portamentoTarget.note.value >> 1) &
		                  0x3F; // ?!?!?!?!?!?
		uint8_t l = m_lastMidiOnOff_KeyVelocity.value > 2
		                  ? m_lastMidiOnOff_KeyVelocity.value - 2
		                  : 0;
		l         = (l >> 2) & 0x1F;

		uint8_t tmp = 0;
		tmp         = carrierOrModulatorTableLookup(
                              instr->voiceDefinition.getOperator(0),
                              l,
                              getKeyboardLevelScaling(
                                      instr->voiceDefinition.getOperator(0), h)) +
		      instr->operator1TotalLevel;
		ymChannelData->operatorVolumes[0] = tmp < 0x80 ? tmp : 0x7F;
		tmp = carrierOrModulatorTableLookup(
		              instr->voiceDefinition.getOperator(2),
		              l,
		              getKeyboardLevelScaling(
		                      instr->voiceDefinition.getOperator(2), h)) +
		      instr->operator3TotalLevel;
		ymChannelData->operatorVolumes[2] = tmp < 0x80 ? tmp : 0x7F;
		tmp = carrierOrModulatorTableLookup(
		              instr->voiceDefinition.getOperator(1),
		              l,
		              getKeyboardLevelScaling(
		                      instr->voiceDefinition.getOperator(1), h)) +
		      instr->operator2TotalLevel;
		ymChannelData->operatorVolumes[1] = tmp < 0x80 ? tmp : 0x7F;
		tmp = carrierOrModulatorTableLookup(
		              instr->voiceDefinition.getOperator(3),
		              l,
		              getKeyboardLevelScaling(
		                      instr->voiceDefinition.getOperator(3), h)) +
		      instr->operator4TotalLevel;
		ymChannelData->operatorVolumes[3] = tmp < 0x80 ? tmp : 0x7F;
		ym_updateOperatorVolumes(instr, ymChannelData);
	}

	// ROM Address: 0x1D6D
	static uint8_t carrierOrModulatorTableLookup(const OperatorDefinition* operatorDefinition,
	                                             uint8_t l, uint8_t c)
	{
		static constexpr uint8_t carrierTable[8][32] = {
		        // clang-format off
			{ 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18 },
			{ 0x1B, 0x1B, 0x1B, 0x1B, 0x1A, 0x1A, 0x1A, 0x1A, 0x19, 0x19, 0x19, 0x19, 0x19, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x17, 0x17, 0x17, 0x17, 0x17, 0x16, 0x16, 0x16, 0x16, 0x15, 0x15, 0x15, 0x15 },
			{ 0x1E, 0x1E, 0x1D, 0x1D, 0x1C, 0x1C, 0x1B, 0x1B, 0x1A, 0x1A, 0x1A, 0x19, 0x19, 0x19, 0x18, 0x18, 0x18, 0x18, 0x17, 0x17, 0x17, 0x16, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x13, 0x12, 0x12 },
			{ 0x21, 0x20, 0x1F, 0x1E, 0x1E, 0x1D, 0x1D, 0x1C, 0x1C, 0x1B, 0x1B, 0x1A, 0x1A, 0x19, 0x19, 0x18, 0x18, 0x17, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x13, 0x12, 0x12, 0x11, 0x10, 0x0F },
			{ 0x24, 0x23, 0x22, 0x21, 0x20, 0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1B, 0x1A, 0x1A, 0x19, 0x19, 0x18, 0x18, 0x17, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C },
			{ 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x20, 0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09 },
			{ 0x2A, 0x28, 0x27, 0x26, 0x25, 0x24, 0x22, 0x21, 0x20, 0x1F, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x11, 0x10, 0x0F, 0x0E, 0x0C, 0x0B, 0x0A, 0x09, 0x07, 0x06 },
			{ 0x2D, 0x2B, 0x2A, 0x28, 0x27, 0x25, 0x24, 0x22, 0x21, 0x1F, 0x1E, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x18, 0x17, 0x15, 0x14, 0x13, 0x11, 0x10, 0x0E, 0x0D, 0x0B, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00 }
		        // clang-format on
		};
		static constexpr uint8_t modulatorTable[8][32] = {
		        // clang-format off
			{  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8 },
			{  9,  9,  9,  9,  9,  9,  9,  9,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  7,  7,  7,  7,  7,  7,  7,  7 },
			{ 10, 10, 10, 10, 10, 10,  9,  9,  9,  9,  9,  9,  8,  8,  8,  8,  8,  8,  8,  8,  7,  7,  7,  7,  7,  7,  6,  6,  6,  6,  6,  6 },
			{ 11, 11, 11, 11, 10, 10, 10, 10,  9,  9,  9,  9,  9,  8,  8,  8,  8,  8,  8,  7,  7,  7,  7,  7,  6,  6,  6,  6,  5,  5,  5,  5 },
			{ 12, 12, 12, 11, 11, 11, 10, 10, 10,  9,  9,  9,  9,  8,  8,  8,  8,  8,  8,  7,  7,  7,  7,  6,  6,  6,  5,  5,  5,  4,  4,  4 },
			{ 13, 13, 12, 12, 12, 11, 11, 11, 10, 10, 10,  9,  9,  9,  8,  8,  8,  8,  7,  7,  7,  6,  6,  6,  5,  5,  5,  4,  4,  4,  3,  3 },
			{ 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 10,  9,  9,  9,  8,  8,  8,  8,  7,  7,  7,  6,  6,  6,  5,  5,  4,  4,  3,  3,  2,  2 },
			{ 15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10,  9,  9,  8,  8,  8,  8,  7,  7,  6,  6,  5,  5,  4,  4,  3,  3,  2,  2,  1,  0 }
		        // clang-format on
		};
		if (operatorDefinition->getModulatorCarrierSelect() != 0U) {
			return c +
			       carrierTable[operatorDefinition->getVelocitySensitivityToTotalLevel()]
			                   [l];
		}
		return c +
		       modulatorTable[operatorDefinition->getVelocitySensitivityToTotalLevel()][l];
	}

	// ROM Address: 0x1D84
	static uint8_t getKeyboardLevelScaling(const OperatorDefinition* operatorDefinition,
	                                       uint8_t h)
	{
		static constexpr uint8_t scale[4][64] = {
		        // clang-format off
			{
				// Linear attenuation with increasing KC#; maximum of -24 dB per 8 octaves.
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x05, 0x07, 0x0A, 0x0C, 0x0F, 0x11, 0x13, 0x16,
				0x18, 0x1B, 0x1D, 0x20, 0x22, 0x25, 0x27, 0x2A, 0x2C, 0x2F, 0x31, 0x33, 0x36, 0x38, 0x3B, 0x3D,
				0x40, 0x42, 0x45, 0x47, 0x4A, 0x4C, 0x4F, 0x51, 0x53, 0x56, 0x58, 0x5B, 0x5D, 0x60, 0x62, 0x65,
				0x67, 0x6A, 0x6C, 0x6F, 0x71, 0x73, 0x76, 0x78, 0x7B, 0x7D, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F
			},
			{
				// Reverse S-shaped attenuation with decreasing KC#; maximum of -24 dB per 8 octaves.
				0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7E, 0x7E, 0x7D, 0x7C, 0x7B, 0x79, 0x78,
				0x76, 0x74, 0x72, 0x6F, 0x6D, 0x6A, 0x67, 0x64, 0x61, 0x5D, 0x59, 0x55, 0x51, 0x4D, 0x49, 0x44,
				0x3F, 0x3A, 0x36, 0x31, 0x2D, 0x29, 0x25, 0x21, 0x1E, 0x1B, 0x17, 0x15, 0x12, 0x0F, 0x0D, 0x0B,
				0x09, 0x07, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
			},
			{
				// Non-linear attenuation with increasing KC#; maximum of -48 dB per 8 octaves.
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x06,
				0x09, 0x0C, 0x0F, 0x14, 0x18, 0x1E, 0x23, 0x2A, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x48,
				0x4C, 0x50, 0x54, 0x58, 0x5C, 0x61, 0x77, 0x7F, 0x88, 0x93, 0x9F, 0xAB, 0xB8, 0xC4, 0xD0, 0xDC,
				0xE7, 0xF1, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
			},
			{
				// Reverse S-shaped attenuation with decreasing KC#; maximum of -48 dB per 8 octaves.
				0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFE, 0xFD, 0xFA, 0xF6, 0xF1,
				0xEB, 0xE4, 0xDC, 0xD2, 0xC6, 0xB9, 0xAB, 0x9C, 0x8E, 0x81, 0x75, 0x6A, 0x60, 0x57, 0x4F, 0x48,
				0x41, 0x3B, 0x36, 0x31, 0x2D, 0x29, 0x25, 0x21, 0x1E, 0x1B, 0x17, 0x15, 0x12, 0x0F, 0x0D, 0x0B,
				0x09, 0x07, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
			}
		        // clang-format on
		};
		const uint16_t val =
		        operatorDefinition->getKeyboardLevelScalingDepth() *
		        scale[(operatorDefinition->getKeyboardLevelScaling() << 1) |
		              operatorDefinition->getKeyboardLevelScalingType()][h];
		return (uint8_t)(val >> 8);
	}

	// ROM Address: 0x20BA
	void setNodeParameterMasterOutputLevel(uint8_t val)
	{
		log_debug("setNodeParameterMasterOutputLevel()");
		m_masterOutputLevel = (val ^ 0xFF) & 0x7F;
		for (uint8_t i = 0; i < 8; i++) {
			setInstrumentVolume(getActiveInstrumentParameters(i));
		}
	}

	// ROM Address: 0x20D4
	void setInstrumentParameter_OutputLevel(InstrumentParameters* instr,
	                                        uint8_t val)
	{
		log_debug("setInstrumentParameter_OutputLevel(0x%02X)", val);
		instr->instrumentConfiguration.setOutputLevel(val);
		setInstrumentVolume(instr);
	}

	// ROM Address: 0x20DA
	void setInstrumentParameterVolume(InstrumentParameters* instr, uint8_t val)
	{
		static constexpr uint8_t volume_table[64] = {
		        // clang-format off
			0x3F, 0x3D, 0x3B, 0x39, 0x37, 0x35, 0x33, 0x31, 0x2F, 0x2D, 0x2B, 0x29, 0x27, 0x25, 0x23, 0x21,
			0x20, 0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
			0x10, 0x0F, 0x0F, 0x0E, 0x0E, 0x0D, 0x0D, 0x0C, 0x0C, 0x0B, 0x0B, 0x0A, 0x0A, 0x09, 0x09, 0x08,
			0x08, 0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00
		        // clang-format on
		};
		instr->volume = volume_table[val >> 1];
		setInstrumentVolume(instr);
	}

	// ROM Address: 0x212C
	void ym_allOperators_sendKeyScaleAndAttackRate(InstrumentParameters* instr,
	                                               YmChannelData* ymChannelData)
	{
		const uint8_t ymRegister = ymChannelData->channelNumber + 0x80; // KS and AR
		ym_singleOperator_sendKeyScaleAndAttackRate(
		        instr->voiceDefinition.getOperator(0), ymRegister + 0 * 8);
		ym_singleOperator_sendKeyScaleAndAttackRate(
		        instr->voiceDefinition.getOperator(2), ymRegister + 1 * 8);
		ym_singleOperator_sendKeyScaleAndAttackRate(
		        instr->voiceDefinition.getOperator(1), ymRegister + 2 * 8);
		ym_singleOperator_sendKeyScaleAndAttackRate(
		        instr->voiceDefinition.getOperator(3), ymRegister + 3 * 8);
	}

	// ROM Address: 0x2155
	void ym_singleOperator_sendKeyScaleAndAttackRate(
	        const OperatorDefinition* operatorDefinition, uint8_t ymRegister)
	{
		// clang-format off
		static constexpr int8_t byte_2194[4][16] = {
			{ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0  },
			{-4,  -3,  -3,  -2,  -2,  -1,  -1,   0,   0,   1,   1,   2,   2,   3,   3,   4  },
			{-8,  -6,  -5,  -4,  -3,  -2,  -1,   0,   0,   1,   2,   3,   4,   5,   6,   8  },
			{-12, -10, -8,  -6,  -4,  -2,  -1,   0,   0,   1,   2,   4,   6,   8,   10,  12 }
		};
		// clang-format on

		if (operatorDefinition->getVelocitySensitivityToAttackRate() != 0U) {
			auto a_sum =
			        operatorDefinition->getAttackRate() +
			        byte_2194[operatorDefinition->getVelocitySensitivityToAttackRate()]
			                 [(m_lastMidiOnOff_KeyVelocity.value >> 3) & 0x0F];

			constexpr auto a_lower = 2;
			constexpr auto a_upper = 0x1F;

			const auto a = (uint8_t)(
			        std::clamp(a_sum, a_lower, a_upper));

			sendToYM2151_no_interrupts_allowed(
			        ymRegister,
			        (operatorDefinition->getKeyboardRateScalingDepth()
			         << 6) | a);
		} else {
			sendToYM2151_no_interrupts_allowed(
			        ymRegister, operatorDefinition->getByte4());
		}
	}

	// ROM Address: 0x21D4
	void ym_key_off_on_all_channels()
	{
		startMusicProcessing();
		for (uint8_t i = 0; i < 0x20; i++) {
			sendToYM2151_no_interrupts_allowed(0xE0 + i,
			                                   0x0F); // D1L (First
			                                          // Decay
			                                          // Level) / RR
			                                          // (Release
			                                          // Rate)
		}
		// KEY OFF on all channels
		for (uint8_t i = 0; i < 8; i++) {
			sendToYM2151_no_interrupts_allowed(0x08, i); // (SN)^KON
			                                             // / (CH #)
		}

		for (uint8_t i = 0; i < 8; i++) {
			setDefaultInstrumentParameters(
			        getActiveInstrumentParameters(i));
		}
		stopMusicProcessing();
	}

	// ROM Address: 0x2205
	void setDefaultInstrumentParameters(InstrumentParameters* instr)
	{
		startMusicProcessing();
		applyInstrumentConfiguration(instr);
		setInstrumentParameterSustainOnOff(instr, 0);
		setInstrumentParameterPortamentoOnOff(instr, 0x7F);
		setInstrumentParameterSostenutoOnOff(instr, 0);
		executeMidiCommand_PitchBender(instr,
		                               PitchbenderValueLSB(0x40),
		                               PitchbenderValueMSB(0x00)); // set
		                                                           // the
		                                                           // pitchbender
		                                                           // value
		                                                           // in
		                                                           // the
		                                                           // center
		                                                           // (no
		                                                           // pitchbend)
		setInstrumentParameterVolume(instr, 0x7F);
		const uint8_t channelMask = instr->channelMask;
		for (uint8_t i = 0; i < 8; i++) {
			if ((channelMask & (1 << i)) != 0) {
				resetYmChannelData(instr, &m_ymChannelData[i]);
			}
		}
		stopMusicProcessing();
	}

	// ROM Address: 0x223C
	static void resetYmChannelData(InstrumentParameters* instr,
	                               YmChannelData* ymChannelData)
	{
		ymChannelData->currentlyPlaying = FractionalNote(Note(0),
		                                                 ImfcFraction(0));
		ymChannelData->portamentoTarget = FractionalNote(Note(0),
		                                                 ImfcFraction(0));
		ymChannelData->originalFractionAndNoteNumber =
		        FractionalNote(Note(0), ImfcFraction(0));
		instr->ymChannelData = ymChannelData;
	}

	// ROM Address: 0x225B
	void applyInstrumentConfiguration(InstrumentParameters* instr)
	{
		startMusicProcessing();
		instr->_lastMidiOnOff_Duration_XX.setEmpty();
		instr->_lastMidiOnOff_Duration_YY.setEmpty();
		const uint8_t channelMask = instr->channelMask;
		for (uint8_t i = 0; i < 8; i++) {
			if ((channelMask & (1 << i)) != 0) {
				ym_noteOFF_fastRelease(instr, getYmChannelData(i));
			}
		}
		stopMusicProcessing();
	}

	// ROM Address: 0x2284
	void setInstrumentParameter_AllNotesOFF(InstrumentParameters* instr,
	                                        uint8_t /*val*/)
	{
		instr->_lastMidiOnOff_Duration_XX.setEmpty();
		instr->_lastMidiOnOff_Duration_YY.setEmpty();
		const uint8_t channelMask = instr->channelMask;
		for (uint8_t i = 0; i < 8; i++) {
			if ((channelMask & (1 << i)) != 0) {
				ym_noteOFF(instr, getYmChannelData(i));
			}
		}
	}

	// ROM Address: 0x22A8
	// 0xFA: When the music card receives a start message(0xFA), the MIDI
	// real - time clock resets and begins to count 0xFB : When the music
	// card receives a continue message(0xFB), the MIDI real - time clock
	// resumes counting

	void processSystemRealTimeMessage_FA_and_FB()
	{
		startMusicProcessing();
		m_systemRealtimeMessageInProgress = 1;
		logSuccess();
		stopMusicProcessing();
	}
	// ROM Address: 0x22B6
	// 0xFC : clock stop message

	// When the music card receives a stop message (0xFC), the MIDI real-time
	// clock stops counting and stops playing notes started by an event list
	void processSystemRealTimeMessage_FC()
	{
		startMusicProcessing();
		for (uint8_t i = 0; i < 8; i++) {
			InstrumentParameters* inst = getActiveInstrumentParameters(i);
			if ((inst->channelMask != 0U) &&
			    ((inst->instrumentConfiguration.polyMonoMode & 1) != 0)) {
				realTimeMessage_FC_MonoMode(inst);
			}
		}
		for (auto& i : m_ymChannelData) {
			if (i.remainingDuration.value != 0) {
				noteOffDueToMidiRealTimeClock(&i);
			}
		}
		m_systemRealtimeMessageInProgress = 0;
		logSuccess();
		stopMusicProcessing();
	}

	// ROM Address: 0x22F4
	static void realTimeMessage_FC_MonoMode(InstrumentParameters* instr)
	{
		if (instr->_lastMidiOnOff_Duration_YY.isNotEmpty() &&
		    (instr->_lastMidiOnOff_Duration_YY.value != 0U)) {
			instr->_lastMidiOnOff_Duration_YY.setEmpty();
		}
		if (instr->_lastMidiOnOff_Duration_XX.isNotEmpty() &&
		    (instr->_lastMidiOnOff_Duration_XX.value != 0U)) {
			instr->_lastMidiOnOff_Duration_XX = instr->_lastMidiOnOff_Duration_YY;
			instr->_lastMidiOnOff_FractionAndNoteNumber_XXX =
			        instr->_lastMidiOnOff_FractionAndNoteNumber_YYY;
			instr->_lastMidiOnOff_Duration_YY.setEmpty();
		}
	}

	// ROM Address: 0x232D
	// The music card uses the duration count of the MIDI real-time clock
	// (sync clock) when processing the system exclusive message event list
	void processSystemRealTimeMessage_F8()
	{
		if ((m_systemRealtimeMessageInProgress & 1) == 0) {
			return;
		}
		startMusicProcessing();
		for (uint8_t i = 0; i < 8; i++) {
			InstrumentParameters* instr = getActiveInstrumentParameters(i);
			if ((instr->channelMask != 0U) &&
			    (instr->instrumentConfiguration.polyMonoMode & 1) == 1) {
				sub_2377(instr);
			}
		}
		// Do we need to stop any playing channels that had a duration
		// specified?
		for (auto& i : m_ymChannelData) {
			if (i.remainingDuration.value != 0U) {
				i.remainingDuration.value--;
				if (i.remainingDuration.value == 0) {
					noteOffDueToMidiRealTimeClock(&i);
				}
			}
		}
		stopMusicProcessing();
	}

	// ROM Address: 0x2377
	static void sub_2377(InstrumentParameters* instr)
	{
		if (instr->_lastMidiOnOff_Duration_YY.isNotEmpty() &&
		    (instr->_lastMidiOnOff_Duration_YY.value != 0U)) {
			instr->_lastMidiOnOff_Duration_YY.value--;
			if (instr->_lastMidiOnOff_Duration_YY.value == 0) {
				instr->_lastMidiOnOff_Duration_YY.setEmpty();
			}
		}
		if (instr->_lastMidiOnOff_Duration_XX.isNotEmpty() &&
		    (instr->_lastMidiOnOff_Duration_XX.value != 0U)) {
			instr->_lastMidiOnOff_Duration_XX.value--;
			if (instr->_lastMidiOnOff_Duration_XX.value == 0) {
				instr->_lastMidiOnOff_Duration_XX = instr->_lastMidiOnOff_Duration_YY;
				instr->_lastMidiOnOff_FractionAndNoteNumber_XXX =
				        instr->_lastMidiOnOff_FractionAndNoteNumber_YYY;
				instr->_lastMidiOnOff_Duration_YY.setEmpty();
			}
		}
	}

	// ROM Address: 0x23CA
	void noteOffDueToMidiRealTimeClock(YmChannelData* ymChannelData)
	{
		InstrumentParameters* instr = ymChannelData->instrumentParameters;
		if ((instr->instrumentConfiguration.polyMonoMode & 1) == 0) {
			// poly mode
			ym_noteOFF(instr, ymChannelData);
		} else {
			// mono mode
			sub_2613(instr, ymChannelData);
		}
	}

	// ROM Address: 0x23E7
	// val is either 0 (OFF) or 127 (ON)
	void setInstrumentParameterSustainOnOff(InstrumentParameters* instr,
	                                        uint8_t val)
	{
		instr->_sustain = 1;
		if ((val & 0b01000000) != 0) {
			return;
		}
		instr->_sustain = 0;
		// since the sustain "pedal" was release, we might need to turn
		// off a couple of notes that are still playing
		const uint8_t channelMask = instr->channelMask;
		for (uint8_t i = 0; i < 8; i++) {
			if ((channelMask & (1 << i)) != 0) {
				// turn off the note only if it's been
				// "released" (note was turned off)
				if (m_ymChannelData[i]._flag6 &&
				    !m_ymChannelData[i]._hasActiveNoteON) {
					ym_noteOFF(instr, &m_ymChannelData[i]);
				}
			}
		}
	}

	// ROM Address: 0x2418
	// val is either 0 (OFF) or 127 (ON)
	void setInstrumentParameterSostenutoOnOff(InstrumentParameters* instr,
	                                          uint8_t val)
	{
		if ((val & 0b01000000) == 0) {
			// Sostenuto OFF
			const uint8_t channelMask = instr->channelMask;
			for (uint8_t i = 0; i < 8; i++) {
				if ((channelMask & (1 << i)) != 0) {
					m_ymChannelData[i]._hasActiveSostenuto = 0;
					if (m_ymChannelData[i]._flag6 &&
					    !m_ymChannelData[i]._hasActiveNoteON) {
						ym_noteOFF(instr,
						           &m_ymChannelData[i]);
					}
				}
			}
		} else {
			// Sostenuto ON
			const uint8_t channelMask = instr->channelMask;
			for (uint8_t i = 0; i < 8; i++) {
				if ((channelMask & (1 << i)) != 0) {
					if (m_ymChannelData[i]._hasActiveNoteON) {
						m_ymChannelData[i]._hasActiveSostenuto = 1;
					}
				}
			}
		}
	}

	// ROM Address: 0x2465
	void noActiveChannels_forward_to_midiout(InstrumentParameters* instr)
	{
		if ((instr->overflowToMidiOut & 1) == 0) {
			return;
		}

		const auto durationMSB = (uint8_t)(
		        m_lastMidiOnOff_Duration.value >> 7);
		const uint8_t durationLSB = m_lastMidiOnOff_Duration.value & 0x7F;
		const uint8_t noteNumber =
		        m_lastMidiOnOff_FractionAndNoteNumber.note.value;
		const uint8_t fraction =
		        (m_lastMidiOnOff_FractionAndNoteNumber.fraction.value >> 1) &
		        0x7F;

		if ((durationMSB != 0U) || (durationLSB != 0U)) {
			return forwardToMidiOut_7bytes(instr,
			                               noteNumber,
			                               fraction,
			                               durationLSB,
			                               durationMSB);
		}
		if (fraction != 0U) {
			return forwardToMidiOut_5bytes(instr, noteNumber, fraction);
		}
		m_outgoingMusicCardMessageData[0] =
		        0x90 | instr->instrumentConfiguration.midiChannel;
		m_outgoingMusicCardMessageData[1] =
		        m_lastMidiOnOff_FractionAndNoteNumber.note.value;
		m_outgoingMusicCardMessageData[2] = m_lastMidiOnOff_KeyVelocity.value;
		forwardToMidiOut(3);
	}

	// ROM Address: 0x249B
	void forwardToMidiOut_5bytes(InstrumentParameters* instr,
	                             uint8_t noteNumber, uint8_t fraction)
	{
		m_outgoingMusicCardMessageData[0] = 0xFF;
		m_outgoingMusicCardMessageData[1] =
		        0x10 | instr->instrumentConfiguration.midiChannel;
		m_outgoingMusicCardMessageData[2] = noteNumber;
		m_outgoingMusicCardMessageData[3] = fraction;
		m_outgoingMusicCardMessageData[4] = m_lastMidiOnOff_KeyVelocity.value;
		forwardToMidiOut(5);
	}

	// ROM Address: 0x24B6
	void forwardToMidiOut_7bytes(InstrumentParameters* instr,
	                             uint8_t noteNumber, uint8_t fraction,
	                             uint8_t durationLSB, uint8_t durationMSB)
	{
		m_outgoingMusicCardMessageData[0] = 0xFF;
		m_outgoingMusicCardMessageData[1] =
		        0x20 | instr->instrumentConfiguration.midiChannel;
		m_outgoingMusicCardMessageData[2] = noteNumber;
		m_outgoingMusicCardMessageData[3] = fraction;
		m_outgoingMusicCardMessageData[4] = m_lastMidiOnOff_KeyVelocity.value;
		m_outgoingMusicCardMessageData[5] = durationLSB;
		m_outgoingMusicCardMessageData[6] = durationMSB;
		forwardToMidiOut(7);
	}

	// ROM Address: 0x24D2
	void forwardToMidiOut(uint8_t nrOfBytes)
	{
		sendMidiResponse_to_MidiOut((const uint8_t*)&m_outgoingMusicCardMessageData,
		                            nrOfBytes);
	}

	// ROM Address: 0x24D8
	void executeMidiCommand_NoteONOFF(InstrumentParameters* instr,
	                                  Note noteNumber, KeyVelocity velocity)
	{
		executeMidiCommand_NoteONOFF_internal(
		        instr, noteNumber, ZERO_FRACTION, velocity, ZERO_DURATION);
	}

	// ROM Address: 0x24E1
	void executeMidiCommand_NoteONOFF_internal_guard(InstrumentParameters* instr,
	                                                 Note noteNumber,
	                                                 ImfcFraction fraction,
	                                                 KeyVelocity velocity,
	                                                 Duration duration)
	{
		startMusicProcessing();
		executeMidiCommand_NoteONOFF_internal(
		        instr, noteNumber, fraction, velocity, duration);
		stopMusicProcessing();
	}

	// ROM Address: 0x24EA
	void executeMidiCommand_NoteONOFF_internal(InstrumentParameters* instr,
	                                           Note noteNumber, ImfcFraction fraction,
	                                           KeyVelocity velocity,
	                                           Duration duration)
	{
		log_error("executeMidiCommand_NoteONOFF_internal(midichannel=%i, note=%s, velocity=0x%02X)",
		          getMidiChannel(instr),
		          noteNumber.toString().c_str(),
		          velocity);
		m_lastMidiOnOff_FractionAndNoteNumber = FractionalNote(noteNumber,
		                                                       fraction);
		m_lastMidiOnOff_KeyVelocity = velocity;
		m_lastMidiOnOff_Duration    = duration;
		if (instr->channelMask == 0) {
			log_error("executeMidiCommand_NoteONOFF_internal() - no channelMask -> forward to midiout");
			return noActiveChannels_forward_to_midiout(instr);
		}
		if (m_activeConfiguration.noteNumberReceptionMode == 1) {
			// only play even notes
			if ((noteNumber.value & 1) == 1) {
				return;
			}
		} else if (m_activeConfiguration.noteNumberReceptionMode == 2) {
			// only play odd notes
			if ((noteNumber.value & 1) == 0) {
				return;
			}
		}
		if (noteNumber.value >
		    instr->instrumentConfiguration.noteNumberLimitHigh.value) {
			return;
		}
		if (noteNumber.value <
		    instr->instrumentConfiguration.noteNumberLimitLow.value) {
			return;
		}
		if ((instr->instrumentConfiguration.polyMonoMode & 1) != 0) {
			// mono mode
			// log_debug("executeMidiCommand_NoteONOFF_internal() -
			// applySub25B9_for_first_active_channel_because_MONO_MODE");
			return applySub25B9_for_first_active_channel_because_MONO_MODE(
			        instr, FractionalNote(noteNumber, fraction));
		}
		// we are now in poly mode
		if (velocity.value == 0) {
			// log_debug("executeMidiCommand_NoteONOFF_internal() -
			// note OFF");
			//  note OFF
			//  find the same note and turn it off
			for (uint8_t i = 0; i < 8; i++) {
				if (((instr->channelMask & (1 << i)) != 0) &&
				    m_ymChannelData[i]._hasActiveNoteON) {
					if (m_ymChannelData[i].originalFractionAndNoteNumber ==
					    FractionalNote(noteNumber, fraction)) {
						return ym_noteOFF(instr,
						                  &m_ymChannelData[i]);
					}
				}
			}
			return noActiveChannels_forward_to_midiout(instr);
		}
		// note ON
		// log_debug("executeMidiCommand_NoteONOFF_internal() - note ON");
		uint8_t lastUsedChannel = instr->lastUsedChannel;
		for (uint8_t i = 0; i < 8; i++) {
			lastUsedChannel = leftRotate8(lastUsedChannel);
			if ((instr->channelMask & lastUsedChannel) != 0) {
				YmChannelData* ymChannelData = getYmChannelData_for_first_active_channel(
				        instr->channelMask & lastUsedChannel);
				if (ymChannelData->_flag6 == 0) {
					instr->lastUsedChannel = lastUsedChannel;
					log_error("executeMidiCommand_NoteONOFF_internal() - ym_fastNoteOFF_noteON");
					return ym_fastNoteOFF_noteON(instr,
					                             ymChannelData);
				}
			}
		}
		if (m_chainMode == CHAIN_MODE_ENABLED) {
			return noActiveChannels_forward_to_midiout(instr);
		}
		// note ON and chain mode DISABLED
		// log_debug("executeMidiCommand_NoteONOFF_internal() - note ON
		// and chain mode DISABLED");
		for (uint8_t i = 0; i < 8; i++) {
			lastUsedChannel = leftRotate8(lastUsedChannel);
			if ((instr->channelMask & lastUsedChannel) != 0) {
				YmChannelData* ymChannelData = getYmChannelData_for_first_active_channel(
				        instr->channelMask & lastUsedChannel);
				if (ymChannelData->_hasActiveNoteON == 0) {
					instr->lastUsedChannel = lastUsedChannel;
					log_error("executeMidiCommand_NoteONOFF_internal() - ym_fastNoteOFF_delay_noteON");
					return ym_fastNoteOFF_delay_noteON(instr,
					                                   ymChannelData);
				}
			}
		}
		// log_debug("executeMidiCommand_NoteONOFF_internal() - if all
		// fails");
		for (;;) {
			lastUsedChannel = leftRotate8(lastUsedChannel);
			// log_debug("executeMidiCommand_NoteONOFF_internal() -
			// channelMask = %02X / lastUsedChannel = %02X",
			// instr->channelMask, lastUsedChannel);
			if ((instr->channelMask & lastUsedChannel) != 0) {
				YmChannelData* ymChannelData = getYmChannelData_for_first_active_channel(
				        instr->channelMask & lastUsedChannel);
				instr->lastUsedChannel = lastUsedChannel;
				log_error("executeMidiCommand_NoteONOFF_internal() - ym_fastNoteOFF_delay_noteON");
				return ym_fastNoteOFF_delay_noteON(instr,
				                                   ymChannelData);
			}
		}
		// log_debug("executeMidiCommand_NoteONOFF_internal() - end");
	}

	// ROM Address: 0x259E
	YmChannelData* getYmChannelData_for_first_active_channel(uint8_t channelMask)
	{
		uint8_t channelNr = 0;
		while ((channelMask & (1 << channelNr)) == 0) {
			channelNr++;
		}
		return &m_ymChannelData[channelNr];
	}

	// ROM Address: 0x25A8
	void applySub25B9_for_first_active_channel_because_MONO_MODE(
	        InstrumentParameters* instr, FractionalNote val)
	{
		const uint8_t channelMask = instr->channelMask;
		uint8_t channelNr         = 0;
		while ((channelMask & (1 << channelNr)) == 0) {
			channelNr++;
		}
		if (m_lastMidiOnOff_KeyVelocity.value != 0U) {
			// key ON
			return sub_264B(instr, &m_ymChannelData[channelNr]);
		}
		// key OFF
		if (instr->_lastMidiOnOff_Duration_YY.isEmpty()) {
			return sub_25D6(instr, &m_ymChannelData[channelNr], val);
		}
		if (instr->_lastMidiOnOff_FractionAndNoteNumber_YYY == val) {
			instr->_lastMidiOnOff_Duration_YY.setEmpty();
			return;
		}
		sub_25D6(instr, &m_ymChannelData[channelNr], val);
	}

	// ROM Address: 0x25D6
	void sub_25D6(InstrumentParameters* instr, YmChannelData* ymChannelData,
	              FractionalNote val)
	{
		if (instr->_lastMidiOnOff_Duration_XX.isNotEmpty() &&
		    instr->_lastMidiOnOff_FractionAndNoteNumber_XXX == val) {
			instr->_lastMidiOnOff_Duration_XX = instr->_lastMidiOnOff_Duration_YY;
			instr->_lastMidiOnOff_FractionAndNoteNumber_XXX =
			        instr->_lastMidiOnOff_FractionAndNoteNumber_YYY;
			instr->_lastMidiOnOff_Duration_YY.setEmpty();
			return;
		}
		if (ymChannelData->_hasActiveNoteON == 0) {
			return;
		}
		if (ymChannelData->currentlyPlaying != val) {
			return;
		}
		sub_2613(instr, ymChannelData);
	}

	// ROM Address: 0x2613
	void sub_2613(InstrumentParameters* instr, YmChannelData* ymChannelData)
	{
		if (instr->_lastMidiOnOff_Duration_XX.isEmpty()) {
			return ym_noteOFF(instr, ymChannelData);
		}
		m_lastMidiOnOff_Duration = instr->_lastMidiOnOff_Duration_XX;
		m_lastMidiOnOff_FractionAndNoteNumber =
		        instr->_lastMidiOnOff_FractionAndNoteNumber_XXX;
		instr->_lastMidiOnOff_Duration_XX = instr->_lastMidiOnOff_Duration_YY;
		instr->_lastMidiOnOff_FractionAndNoteNumber_XXX =
		        instr->_lastMidiOnOff_FractionAndNoteNumber_YYY;
		instr->_lastMidiOnOff_Duration_YY.setEmpty();
		sub_26DA(instr, ymChannelData);
	}

	// ROM Address: 0x264B
	void sub_264B(InstrumentParameters* instr, YmChannelData* ymChannelData)
	{
		if (ymChannelData->_hasActiveNoteON == 0) {
			return sub_26E0(instr, ymChannelData);
		}
		instr->_lastMidiOnOff_FractionAndNoteNumber_YYY =
		        instr->_lastMidiOnOff_FractionAndNoteNumber_XXX;
		instr->_lastMidiOnOff_Duration_YY = instr->_lastMidiOnOff_Duration_XX;
		instr->_lastMidiOnOff_FractionAndNoteNumber_XXX =
		        ymChannelData->originalFractionAndNoteNumber;
		instr->_lastMidiOnOff_Duration_XX = ymChannelData->remainingDuration;
		sub_26DA(instr, ymChannelData);
	}

	// ROM Address: 0x2685
	void ym_noteOFF_fastRelease(InstrumentParameters* instr,
	                            YmChannelData* ymChannelData)
	{
		ym_setFirstDecayLevelAndReleaseRate(instr, ymChannelData, 0x0F);
		ymChannelData->remainingDuration.value = 0;
		ymChannelData->_hasActiveNoteON        = 0;
		ymChannelData->_flag6                  = 0;
		ymChannelData->_hasActivePortamento    = 0;
		ymChannelData->_hasActiveSostenuto     = 0;
		sendToYM2151_no_interrupts_allowed(8, ymChannelData->channelNumber);
	}

	// ROM Address: 0x2689
	void ym_noteOFF(InstrumentParameters* instr, YmChannelData* ymChannelData)
	{
		log_debug("ym_noteOFF()");
		ymChannelData->_hasActiveNoteON = 0;
		if (instr->_sustain) {
			return;
		}
		if (ymChannelData->_hasActiveSostenuto) {
			return;
		}
		ym_setFirstDecayLevelAndReleaseRate(instr, ymChannelData, 0);
		ymChannelData->remainingDuration.value = 0;
		ymChannelData->_hasActiveNoteON        = 0;
		ymChannelData->_flag6                  = 0;
		ymChannelData->_hasActivePortamento    = 0;
		ymChannelData->_hasActiveSostenuto     = 0;
		sendToYM2151_no_interrupts_allowed(8, ymChannelData->channelNumber);
	}

	// ROM Address: 0x26B4
	// value to be ORed to ymCmdValue (0x00 or 0x0F)
	void ym_setFirstDecayLevelAndReleaseRate(InstrumentParameters* instr,
	                                         YmChannelData* ymChannelData,
	                                         uint8_t val)
	{
		const uint8_t ymReg = ymChannelData->channelNumber + 0xE0;
		// Send D1L (First Decay Level) and RR (Release Rate)
		sendToYM2151_no_interrupts_allowed(
		        ymReg + 0 * 8,
		        instr->voiceDefinition.getOperator(0)->getReleaseRateSustainLevel() |
		                val);
		sendToYM2151_no_interrupts_allowed(
		        ymReg + 1 * 8,
		        instr->voiceDefinition.getOperator(2)->getReleaseRateSustainLevel() |
		                val);
		sendToYM2151_no_interrupts_allowed(
		        ymReg + 2 * 8,
		        instr->voiceDefinition.getOperator(1)->getReleaseRateSustainLevel() |
		                val);
		sendToYM2151_no_interrupts_allowed(
		        ymReg + 3 * 8,
		        instr->voiceDefinition.getOperator(3)->getReleaseRateSustainLevel() |
		                val);
	}

	// ROM Address: 0x26DA
	void sub_26DA(InstrumentParameters* instr, YmChannelData* ymChannelData)
	{
		sub_2724(instr, ymChannelData);
		ym_registerKey_setKeyCodeAndFraction_IncludingPortamento(instr,
		                                                         ymChannelData);
	}

	// ROM Address: 0x26E0
	void sub_26E0(InstrumentParameters* instr, YmChannelData* ymChannelData)
	{
		ym_noteOFF_fastRelease(instr, ymChannelData);
		sub_2724(instr, ymChannelData);
		ym_registerKey_setKeyCodeAndFraction_Special(instr, ymChannelData);
		ym_noteON(instr, ymChannelData);
	}

	// ROM Address: 0x26EB
	void ym_fastNoteOFF_delay_noteON(InstrumentParameters* instr,
	                                 YmChannelData* ymChannelData)
	{
		log_debug("ym_fastNoteOFF_delay_noteON()");
		ym_noteOFF_fastRelease(instr, ymChannelData);
		// FIXME: delay code
		sub_26FB(instr, ymChannelData);
	}

	// ROM Address: 0x26F8
	void ym_fastNoteOFF_noteON(InstrumentParameters* instr,
	                           YmChannelData* ymChannelData)
	{
		log_debug("ym_fastNoteOFF_noteON()");
		ym_noteOFF_fastRelease(instr, ymChannelData);
		sub_26FB(instr, ymChannelData);
	}

	// ROM Address: 0x26FB
	void sub_26FB(InstrumentParameters* instr, YmChannelData* ymChannelData)
	{
		sub_2724(instr, ymChannelData);
		ym_registerKey_setKeyCodeAndFraction_IncludingPortamento(instr,
		                                                         ymChannelData);
		ym_noteON(instr, ymChannelData);
	}

	// ROM Address: 0x2701
	void ym_noteON(InstrumentParameters* instr, YmChannelData* ymChannelData)
	{
		ym_calculateAndUpdateOperatorVolumes(instr, ymChannelData);
		ym_allOperators_sendKeyScaleAndAttackRate(instr, ymChannelData);
		if (instr->_lfoSyncMode) {
			// LFO sync mode
			sendToYM2151_with_disabled_interrupts(9, 2); // 9=Test
			                                             // register
			                                             // on the
			                                             // ym2164 -
			                                             // Reset LFO
			                                             // phase
			delayNop();
			delayNop();
			sendToYM2151_no_interrupts_allowed(9, 2); // 9=Test
			                                          // register on
			                                          // the ym2164
			                                          // - Reset LFO
			                                          // phase
		}
		sendToYM2151_no_interrupts_allowed(
		        8, ymChannelData->getChannelNumberAndOperatorEnabled()); // Set KeyON/OFF
	}

	// ROM Address: 0x2724
	void sub_2724(InstrumentParameters* instr, YmChannelData* ymChannelData)
	{
		ymChannelData->_hasActiveNoteON     = 1;
		ymChannelData->_flag6               = 1;
		ymChannelData->_hasActivePortamento = 0;
		ymChannelData->_hasActiveSostenuto  = 0;

		// if (instr->voiceDefinition.getTranspose() != 0) {
		//	log_error("DEBUG: instr->voiceDefinition.getTranspose()
		// is %i", instr->voiceDefinition.getTranspose());
		// }

		ymChannelData->portamentoTarget = cropToPlayableRange(
		        m_lastMidiOnOff_FractionAndNoteNumber,
		        FractionalNote(Note(instr->voiceDefinition.getTranspose()),
		                       ImfcFraction(0)));
	}

	// ROM Address: 0x273A
	void SoundProcessor_SetToInitialState()
	{
		// log("SoundProcessor_SetToInitialState() - begin");
		m_soundProcessorMidiInterpreterState = 0;
		memset(&m_sp_MidiDataOfMidiCommandInProgress,
		       0,
		       sizeof(m_sp_MidiDataOfMidiCommandInProgress));
		initializeSysExStateMatchTable();
		initMidiChannelToAssignedInstruments();
		// log("SoundProcessor_SetToInitialState() - end");
	}

	// ROM Address: 0x2750
	void SoundProcessor_SetState_SysEx_EventListTransferToMusicCard()
	{
		m_soundProcessorMidiInterpreterState      = 0x44;
		m_soundProcessorMidiInterpreterSysExState = 0;
	}

	// ROM Address: 0x275B
	void SoundProcessor_SetState_SysEx_ParameterListTransferToMusicCard()
	{
		m_soundProcessorMidiInterpreterState      = 0x45;
		m_soundProcessorMidiInterpreterSysExState = 0;
	}

	// ROM Address: 0x2766
	void SoundProcessor_processMidiByte(uint8_t midiData)
	{
		// log("SoundProcessor_processMidiByte - processing %02X",
		// midiData);
		switch (m_soundProcessorMidiInterpreterState & 0xF0) {
		case 0x00:
			return SoundProcessor_processMidiCommandByte(midiData);
		case 0x10:
			return SoundProcessor_processMidiByte_3ByteMidiCommandState(
			        midiData);
		case 0x20:
			return SoundProcessor_processMidiByte_2ByteMidiCommandState(
			        midiData);
		default: return SoundProcessor_processSysExStates(midiData);
		}
	}

	// ROM Address: 0x2782
	void logMidiError()
	{
		static constexpr char midiError[] = "  MIDI error !!!";
		logError(midiError);
	}

	// ROM Address: 0x2798
	void SoundProcessor_processMidiCommandByte(uint8_t midiCommandByte)
	{
		log_debug("SoundProcessor_processMidiCommandByte - processing %02X",
		          midiCommandByte);
		m_soundProcessorMidiInterpreterState = 0;
		if (midiCommandByte < 0x80) {
			return;
		}
		if (midiCommandByte == 0xF0) {
			m_soundProcessorSysExCurrentMatchPtr = (uint8_t*)&m_sp_SysExStateMatchTable;
			m_soundProcessorMidiInterpreterSysExState = 0;
			m_soundProcessorMidiInterpreterState      = 0x30;
		} else {
			switch (midiCommandByte & 0xF0) {
			case 0xA0: // 0xA0 (Polyphonic Key Pressure (Aftertouch))
				return;
			case 0xF0: // 0xF0 (SysEx)
				return;
			case 0xC0: // 0xC0 (Program Change)
			case 0xD0: // 0xD0 (Channel Pressure (After-touch))
				m_soundProcessorMidiInterpreterState = 0x20;
				break;
			default: m_soundProcessorMidiInterpreterState = 0x10;
			}
			m_sp_MidiDataOfMidiCommandInProgress[0] = midiCommandByte;
		}
	}

	// ROM Address: 0x27F4
	void SoundProcessor_processMidiByte_3ByteMidiCommandState(uint8_t midiData)
	{
		if (midiData >= 0x80) {
			return SoundProcessor_processMidiCommandByte(midiData);
		}
		if ((m_soundProcessorMidiInterpreterState & 0x0F) == 0) {
			// log("SoundProcessor_processMidiByte_3ByteMidiCommandState
			// - first data byte - processing %02X", midiData);
			//  read first data byte
			m_sp_MidiDataOfMidiCommandInProgress[1] = midiData;
			m_soundProcessorMidiInterpreterState    = 0x11;
		} else {
			// log("SoundProcessor_processMidiByte_3ByteMidiCommandState
			// - second data byte - processing %02X", midiData);
			//  read second data byte
			m_sp_MidiDataOfMidiCommandInProgress[2] = midiData;
			SoundProcessor_executeMidiCommand();
			m_soundProcessorMidiInterpreterState = 0x10;
		}
	}

	// ROM Address: 0x27F4
	void SoundProcessor_processMidiByte_2ByteMidiCommandState(uint8_t midiData)
	{
		log_debug("SoundProcessor_processMidiByte_2ByteMidiCommandState - processing %02X",
		          midiData);
		if (midiData >= 0x80) {
			return SoundProcessor_processMidiCommandByte(midiData);
		}
		m_sp_MidiDataOfMidiCommandInProgress[1] = midiData;
		m_sp_MidiDataOfMidiCommandInProgress[2] = 0;
		SoundProcessor_executeMidiCommand();
	}

	// ROM Address: 0x2800
	void SoundProcessor_executeMidiCommand()
	{
		// log("SoundProcessor_executeMidiCommand");
		const uint8_t midiChannel = m_sp_MidiDataOfMidiCommandInProgress[0] &
		                            0x0F;
		if (m_midiChannelToAssignedInstruments[midiChannel][0] == 0xFF &&
		    m_chainMode == CHAIN_MODE_ENABLED &&
		    m_sp_MidiDataOfMidiCommandInProgress[0] < 0xA0) {
			if (m_sp_MidiDataOfMidiCommandInProgress[0] == 0x90) {
				// confirm we've already got the value as expected
				assert(m_sp_MidiDataOfMidiCommandInProgress[0] ==
				       ((m_sp_MidiDataOfMidiCommandInProgress[0] & 0x0F) |
				        0x90));
				m_sp_MidiDataOfMidiCommandInProgress[2] = 0;
			}
			return sendMidiResponse_to_MidiOut(
			        (const uint8_t*)&m_sp_MidiDataOfMidiCommandInProgress,
			        3);
		}
		uint8_t i       = 0;
		uint8_t instrNr = 0;
		while ((instrNr = m_midiChannelToAssignedInstruments[midiChannel][i]) !=
		       0xFF) {
			InstrumentParameters* instr = getActiveInstrumentParameters(
			        instrNr);
			ym2151_executeMidiCommand(
			        instr,
			        m_sp_MidiDataOfMidiCommandInProgress[0],
			        m_sp_MidiDataOfMidiCommandInProgress[1],
			        m_sp_MidiDataOfMidiCommandInProgress[2]);
			i++;
		}
	}

	// ROM Address: 0x285B
	void executeMidiCommand_NoteONOFFForAllAssignInstruments()
	{
		// clang-format off
		/*
			The format is:
			 byte 0: 0010nnnn : [2n] Status byte / n=Channel number (0-15)
			 byte 1: 0kkkkkkk : Note number / k=0(C-2) ~ 127(G8)
			 byte 2: 0fffffff : Fraction / f=0~127 (+100 cents)
			 byte 3: 0vvvvvvv : Key velocity / v=0: Note OFF / v=1~127: Note ON
			 byte 4: 0ddddddd : Duration2 LSB
			 byte 5: 0ddddddd : Duration2 MSB / d=0: Note ON/OFF Only
		*/
		// clang-format on
		const uint8_t midiChannel = m_sp_MidiDataOfMidiCommandInProgress[0] &
		                            0x0F;
		if (m_midiChannelToAssignedInstruments[midiChannel][0] == 0xFF &&
		    m_chainMode == CHAIN_MODE_ENABLED) {
			m_outgoingMusicCardMessageData[0] = 0xFF;
			m_outgoingMusicCardMessageData[1] =
			        (m_sp_MidiDataOfMidiCommandInProgress[0] & 0x0F) |
			        0x20;
			m_outgoingMusicCardMessageData[2] =
			        m_sp_MidiDataOfMidiCommandInProgress[1];
			m_outgoingMusicCardMessageData[3] =
			        m_sp_MidiDataOfMidiCommandInProgress[2];
			m_outgoingMusicCardMessageData[4] =
			        m_sp_MidiDataOfMidiCommandInProgress[3];
			m_outgoingMusicCardMessageData[5] =
			        m_sp_MidiDataOfMidiCommandInProgress[4];
			return sendMidiResponse_to_MidiOut(
			        (const uint8_t*)&m_outgoingMusicCardMessageData,
			        m_outgoingMusicCardMessageData[4] == 0 &&
			                        m_outgoingMusicCardMessageData[5] == 0
			                ? 3
			                : 5);
		}
		uint8_t i       = 0;
		uint8_t instrNr = 0;
		while ((instrNr = m_midiChannelToAssignedInstruments[midiChannel][i]) !=
		       0xFF) {
			InstrumentParameters* instr = getActiveInstrumentParameters(
			        instrNr);
			executeMidiCommand_NoteONOFF_internal_guard(
			        instr,
			        Note(m_sp_MidiDataOfMidiCommandInProgress[1]) /* note number */,
			        ImfcFraction(m_sp_MidiDataOfMidiCommandInProgress[2]) /* fraction */,
			        KeyVelocity(m_sp_MidiDataOfMidiCommandInProgress[3]) /* velocity */,
			        Duration((uint16_t)(
			                m_sp_MidiDataOfMidiCommandInProgress[5] * 128 +
			                m_sp_MidiDataOfMidiCommandInProgress[4])) /* duration */
			);
			i++;
		}
	}

	// ROM Address: 0x28D3
	void processSysExCmd_InstrumentParameterChange()
	{
		log_debug("processSysExCmd_InstrumentParameterChange - begin");
		if (m_sp_MidiDataOfMidiCommandInProgress[0] >= 0x40) {
			m_sp_MidiDataOfMidiCommandInProgress[1] =
			        (m_sp_MidiDataOfMidiCommandInProgress[2] << 4) |
			        m_sp_MidiDataOfMidiCommandInProgress[1];
		}
		uint8_t i       = 0;
		uint8_t instrNr = 0;
		while ((instrNr = m_midiChannelToAssignedInstruments[m_sysEx_ChannelNumber][i]) !=
		       0xFF) {
			InstrumentParameters* instr = getActiveInstrumentParameters(
			        instrNr);
			setInstrumentParameter(
			        instr,
			        m_sp_MidiDataOfMidiCommandInProgress[0],
			        m_sp_MidiDataOfMidiCommandInProgress[1]);
			i++;
		}
		log_debug("processSysExCmd_InstrumentParameterChange - end");
	}

	// ROM Address: 0x2913
	void SoundProcessor_processSysExStates(uint8_t midiData)
	{
		// log("SoundProcessor_processSysExStates - processing %02X",
		// midiData);
		if (m_soundProcessorMidiInterpreterState == 0x30) {
			// midiData is the first byte after a 0xF0 (SysEx) command
			if (midiData >= 0x80) {
				log_debug("SoundProcessor_processSysExStates() - unexpected midi command");
				return SoundProcessor_processMidiCommandByte(midiData);
			}
			uint8_t* templateValuePtr = m_soundProcessorSysExCurrentMatchPtr;
			uint8_t expectedMidiData = templateValuePtr[0];
			while (keepRunning.load()) {
				uint8_t actualMidiData = midiData;
				if (expectedMidiData >= 0x80) {
					processSysExTemplateCommand(&expectedMidiData,
					                            &actualMidiData);
				}
				// log("SoundProcessor_processSysExStates -
				// expectedMidiData=%02X / actualMidiData=%02X",
				// expectedMidiData, actualMidiData);
				if (expectedMidiData == actualMidiData) {
					// log("SoundProcessor_processSysExStates
					// - expectedMidiData == actualMidiData");
					break;
				}
				if (expectedMidiData >= actualMidiData) {
					// log("SoundProcessor_processSysExStates
					// - expectedMidiData != actualMidiData");
					log_debug("SoundProcessor_processSysExStates() - unexpected sysex command");
					return SoundProcessor_processMidiCommandByte(
					        midiData);
				}
				templateValuePtr += 7;
				expectedMidiData = templateValuePtr[0];
				if (expectedMidiData == 0xF0) {
					log_debug("SoundProcessor_processSysExStates() - restart new sysex command");
					return SoundProcessor_processMidiCommandByte(
					        midiData);
				}
			}
			// midi data matches the template
			templateValuePtr++;
			if (templateValuePtr[0] == 0xFF) {
				// log("SoundProcessor_processSysExStates - midi
				// data matches the template - terminal state");
				templateValuePtr++;
				m_soundProcessorMidiInterpreterState = templateValuePtr[0];
				// log("SoundProcessor_processSysExStates -
				// terminate state %02X",
				// m_soundProcessorMidiInterpreterState);
			} else {
				// log("SoundProcessor_processSysExStates - midi
				// data matches the template - next match");
				m_soundProcessorSysExCurrentMatchPtr = templateValuePtr;
			}
			return;
		}
		log_debug("SoundProcessor_processSysExStates() - processing command in state %02X",
		          m_soundProcessorMidiInterpreterState);
		switch (m_soundProcessorMidiInterpreterState - 0x31) {
		case 0x00: return processSysExCmd_F0_43_0n_0C(midiData);
		case 0x01:
			return processSysExCmd_InstrumentParameterChange_ByMidiChannel(
			        midiData);
		case 0x02: return processSysExCmd_F0_43_2n_0C(midiData);
		case 0x03:
			return processSysExCmd_NodeMessage_SetVoiceBankData(midiData);
		case 0x04:
			return processSysExCmd_NodeMessage_SetConfiguration1(midiData);
		case 0x05:
			return processSysExCmd_NodeMessage_SetConfigurationMemory(
			        midiData);
		case 0x06:
			return processSysExCmd_NodeMessage_SetConfigurationRAM(midiData);
		case 0x07:
			return processSysExCmd_InstrumentMessage_SetInstrumentVoice(
			        midiData);
		case 0x08:
			return processSysExCmd_NodeParameterChangeMessage(midiData);
		case 0x09:
			return processSysExCmd_InstrumentParameterChange_ByInstrument(
			        midiData);
		case 0x0A:
			return processSysExCmd_NodeDumpRequestMessage_VoiceMemoryBank(
			        midiData);
		case 0x0B:
			return processSysExCmd_NodeDumpRequestMessage_ConfigurationBuffer1(
			        midiData);
		case 0x0C:
			return processSysExCmd_NodeDumpRequestMessage_IndividualConfiguration(
			        midiData);
		case 0x0D:
			return processSysExCmd_NodeDumpRequestMessage_ConfigurationRAM(
			        midiData);
		case 0x0E:
			return processSysExCmd_NodeDumpRequestMessage_MusicCardID(
			        midiData);
		case 0x0F:
			return processSysExCmd_NodeDumpRequestMessage_MusicCardRevision(
			        midiData);
		case 0x10:
			return processSysExCmd_StoreRequest_StoreConfigurationData(
			        midiData);
		case 0x11: return processSysExCmd_F0_43_75_0n_2i_00(midiData);
		case 0x12:
			return processSysExCmd_StoreRequest_StoreVoiceData(midiData);
		case 0x13:
			return processSysExCmd_EventListTransferToMusicCard(midiData);
		case 0x14:
			return processSysExCmd_ParameterListTransferToMusicCard(
			        midiData);
		case 0x15:
			return processSysExCmd_NodeMessage_SetConfiguration2(midiData);
		case 0x16:
			return processSysExCmd_InstrumentMessage_SetInstrumentConfiguration1(
			        midiData);
		case 0x17:
			return processSysExCmd_InstrumentMessage_SetInstrumentConfiguration2(
			        midiData);
		case 0x18:
			return processSysExCmd_NodeDumpRequestMessage_ConfigurationBuffer2(
			        midiData);
		case 0x19: return processSysExCmd_F0_43_75_0n_2i_01(midiData);
		case 0x1A: return processSysExCmd_F0_43_75_0n_2i_02(midiData);
		}
	}

	// ROM Address: 0x2997
	void processSysExTemplateCommand(uint8_t* expectedMidiData,
	                                 uint8_t* actualMidiData)
	{
		if ((*expectedMidiData & 0xF0) == 0xA0) {
			// Template byte is of type ChannelNumber
			m_sysEx_ChannelNumber = *actualMidiData & 0x0F;
			*expectedMidiData     = (*expectedMidiData << 4) & 0xF0;
			*actualMidiData = *actualMidiData & 0xF0; // strip out
			                                          // the channel
			                                          // number from
			                                          // the midi
			                                          // byte (4
			                                          // lower bits)
		} else if ((*expectedMidiData & 0xF0) == 0x90) {
			// Template byte is of type InstrumentNumber
			m_sysEx_InstrumentNumber = *actualMidiData & 0x07;
			*expectedMidiData = ((*expectedMidiData << 4) & 0xF0) | 0x08;
			*actualMidiData   = *actualMidiData & 0xF8; // clear the
			                                          // instrument
			                                          // number from
			                                          // the midi
			                                          // data (3
			                                          // lower bits)
		}
	}

	// ROM Address: 0x29C9
	void processSysExCmd_F0_43_0n_0C(uint8_t midiData)
	{
		log_debug("processSysExCmd_F0_43_0n_0C()");
		if (m_memoryProtection == MEMORY_READONLY) {
			return sendResponse(0x03, CANCEL_MESSAGE);
		}
		if (midiData >= 0x80) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		if (receiveDataPacketTypeA(midiData,
		                           (uint8_t*)&m_voiceDefinitionBankCustom[0],
		                           sizeof(VoiceDefinitionBank)) !=
		    ReadStatus::Success) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		sendResponse(0x01, ACK_MESSAGE);
	}

	// ROM Address: 0x29EF
	void processSysExCmd_InstrumentParameterChange_ByMidiChannel(uint8_t midiData)
	{
		log_debug("processSysExCmd_InstrumentParameterChange_ByMidiChannel()");
		if (midiData >= 0x80) {
			return SoundProcessor_processMidiCommandByte(midiData);
		}
		switch (m_soundProcessorMidiInterpreterSysExState) {
		case 0x00:
			m_sp_MidiDataOfMidiCommandInProgress[0] = midiData;
			m_soundProcessorMidiInterpreterSysExState = midiData < 0x40
			                                                  ? 0x01
			                                                  : 0x10;
			return;
		case 0x01:
			m_sp_MidiDataOfMidiCommandInProgress[1] = midiData;
			break;
		case 0x10:
			if (midiData >= 0x10) {
				return SoundProcessor_processMidiCommandByte(midiData);
			}
			m_sp_MidiDataOfMidiCommandInProgress[1]   = midiData;
			m_soundProcessorMidiInterpreterSysExState = 0x11;
			return;
		default:
			if (midiData >= 0x10) {
				return SoundProcessor_processMidiCommandByte(midiData);
			}
			m_sp_MidiDataOfMidiCommandInProgress[2] = midiData;
			break;
		}
		processSysExCmd_InstrumentParameterChange();
		if (m_sp_MidiDataOfMidiCommandInProgress[0] == 1) {
			initMidiChannelToAssignedInstruments();
		}
		logSuccess();
		m_soundProcessorMidiInterpreterState = 0;
	}

	// ROM Address: 0x2A48
	void processSysExCmd_F0_43_2n_0C(uint8_t midiData)
	{
		log_debug("processSysExCmd_F0_43_2n_0C()");
		if (midiData < 0x80) {
			return sendResponse(0x00, CANCEL_MESSAGE);
		}
		if (send_midi_byte(0xF0) == WriteStatus::Success &&
		    send_midi_byte(0x43) == WriteStatus::Success &&
		    send_midi_byte(m_nodeNumber) == WriteStatus::Success &&
		    send_midi_byte(0x0C) == WriteStatus::Success) {
			sendVoiceDefinitionBank(0);
		}
		SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x2A79
	void processSysExCmd_NodeMessage_SetVoiceBankData(uint8_t midiData)
	{
		log_debug("processSysExCmd_NodeMessage_SetVoiceBankData(%i) - begin",
		          midiData);
		ReadResult readResult = {};

		if (m_memoryProtection == MEMORY_READONLY) {
			log_debug("processSysExCmd_NodeMessage_SetVoiceBankData() - error - memory is read only");
			return sendResponse(0x03, CANCEL_MESSAGE);
		}
		if (midiData >= 0x02) {
			log_debug("processSysExCmd_NodeMessage_SetVoiceBankData() - error - invalid memory bank");
			return sendResponse(0x00, CANCEL_MESSAGE);
		}
		const VoiceDefinitionBank* bank = getVoiceDefinitionBank(midiData);
		// read the high size byte
		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0x00, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		log_debug("processSysExCmd_NodeMessage_SetVoiceBankData() - calling receiveDataPacketTypeA(0x%02x)",
		          readResult.data);
		if (receiveDataPacketTypeA(readResult.data,
		                           (uint8_t*)bank,
		                           sizeof(VoiceDefinitionBank)) !=
		    ReadStatus::Success) {
			log_debug("processSysExCmd_NodeMessage_SetVoiceBankData() - receiveDataPacketTypeA returned ERROR");
			return sendResponse(0x02, NAK_MESSAGE);
		}
		bank->dumpToLog();
		sendResponse(0x01, ACK_MESSAGE);
		log_debug("processSysExCmd_NodeMessage_SetVoiceBankData() - end");
	}

	// ROM Address: 0x2AB8
	void processSysExCmd_NodeMessage_SetConfiguration1(uint8_t /*midiData*/)
	{
		log_debug("processSysExCmd_NodeMessage_SetConfiguration1()");
		ReadResult readResult = {};

		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0x00, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		if (receiveDataPacketTypeB(readResult.data,
		                           (uint8_t*)&m_sp_MidiDataOfMidiCommandInProgress,
		                           sizeof(ConfigurationData)) !=
		    ReadStatus::Success) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		m_activeConfiguration.SetFromStream(
		        m_sp_MidiDataOfMidiCommandInProgress);
		proc_1393_called_for_Reboot();
		initMidiChannelToAssignedInstruments();
		logSuccess();
		sendResponse(0x00, ACK_MESSAGE);
	}

	// ROM Address: 0x2AF5
	void processSysExCmd_NodeMessage_SetConfigurationMemory(uint8_t midiData)
	{
		log_debug("processSysExCmd_NodeMessage_SetConfigurationMemory()");
		ReadResult readResult = {};

		if (m_memoryProtection == MEMORY_READONLY) {
			return sendResponse(0x03, CANCEL_MESSAGE);
		}
		if (midiData >= 0x10) {
			return sendResponse(0x00, CANCEL_MESSAGE);
		}
		auto config = getWritableConfigurationData(midiData);
		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0x00, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		if (receiveDataPacketTypeB(readResult.data,
		                           (uint8_t*)&m_sp_MidiDataOfMidiCommandInProgress,
		                           sizeof(ConfigurationData)) !=
		    ReadStatus::Success) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		config->SetFromStream(m_sp_MidiDataOfMidiCommandInProgress);
		sendResponse(0x00, ACK_MESSAGE);
	}

	// ROM Address: 0x2B40
	void processSysExCmd_NodeMessage_SetConfigurationRAM(uint8_t /*midiData*/)
	{
		log_debug("processSysExCmd_NodeMessage_SetConfigurationRAM()");
		ReadResult readResult = {};

		if (m_memoryProtection == MEMORY_READONLY) {
			return sendResponse(0x03, CANCEL_MESSAGE);
		}
		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0x00, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		if (receiveDataPacketTypeB(readResult.data,
		                           (uint8_t*)&m_configurationRAM,
		                           sizeof(m_configurationRAM)) !=
		    ReadStatus::Success) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		sendResponse(0x01, ACK_MESSAGE);
	}

	// ROM Address: 0x2B74
	void processSysExCmd_NodeMessage_SetConfiguration2(uint8_t /*midiData*/)
	{
		log_debug("processSysExCmd_NodeMessage_SetConfiguration2()");
		ReadResult readResult = {};

		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0x00, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		if (receiveDataPacketTypeB(readResult.data,
		                           (uint8_t*)&m_sp_MidiDataOfMidiCommandInProgress,
		                           sizeof(ConfigurationData)) !=
		    ReadStatus::Success) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		const uint8_t lfoSpeed = m_activeConfiguration.lfoSpeed;
		const uint8_t amplitudeModulationDepth =
		        m_activeConfiguration.amplitudeModulationDepth;
		const uint8_t pitchModulationDepth = m_activeConfiguration.pitchModulationDepth;
		const uint8_t lfoWaveForm = m_activeConfiguration.lfoWaveForm;

		m_activeConfiguration.SetConfigurationFromStream(
		        m_sp_MidiDataOfMidiCommandInProgress);
		m_activeConfiguration.lfoWaveForm = lfoWaveForm;
		m_activeConfiguration.pitchModulationDepth = pitchModulationDepth;
		m_activeConfiguration.amplitudeModulationDepth = amplitudeModulationDepth;
		m_activeConfiguration.lfoSpeed = lfoSpeed;

		for (uint8_t i = 0; i < 8; i++) {
			// this copy is kind of strange. It's not copying all
			// the bytes. So the following would not work:
			//  m_activeConfiguration.instrumentConfigurations[m_sysEx_InstrumentNumber].copyFrom((InstrumentConfiguration*)&m_sp_MidiDataOfMidiCommandInProgress);
			// instead we're just gonna do a memcpy
			m_activeConfiguration.instrumentConfigurations[i].copySpecialFrom(
			        (InstrumentConfiguration*)&m_sp_MidiDataOfMidiCommandInProgress
			                [0x20 + 0x10 * i]);
		}
		sub_13D1();
		initMidiChannelToAssignedInstruments();
		sendResponse(0x00, ACK_MESSAGE);
	}

	// ROM Address: 0x2BE1
	void processSysExCmd_InstrumentMessage_SetInstrumentVoice(uint8_t /*midiData*/)
	{
		log_debug("processSysExCmd_InstrumentMessage_SetInstrumentVoice()");
		ReadResult readResult = {};

		// read the high size byte
		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0x00, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0x02, NAK_MESSAGE);
		}

		if (receiveDataPacketTypeA(readResult.data,
		                           (uint8_t*)&m_sp_MidiDataOfMidiCommandInProgress,
		                           sizeof(VoiceDefinition)) !=
		    ReadStatus::Success) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		InstrumentParameters* instr = getActiveInstrumentParameters(
		        m_sysEx_InstrumentNumber);
		instr->voiceDefinition.SetFromStream(
		        m_sp_MidiDataOfMidiCommandInProgress);
		applyInstrumentParameter(instr);
		logSuccess();
		sendResponse(0x00, ACK_MESSAGE);
	}

	// ROM Address: 0x2C26
	void processSysExCmd_InstrumentMessage_SetInstrumentConfiguration1(uint8_t /*midiData*/)
	{
		log_debug("processSysExCmd_InstrumentMessage_SetInstrumentConfiguration1()");
		ReadResult readResult = {};

		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0x00, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		if (receiveDataPacketTypeB(readResult.data,
		                           (uint8_t*)&m_sp_MidiDataOfMidiCommandInProgress,
		                           sizeof(InstrumentConfiguration)) !=
		    ReadStatus::Success) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		log_debug("processSysExCmd_InstrumentMessage_SetInstrumentConfiguration1() - copy start");
		m_activeConfiguration
		        .instrumentConfigurations[m_sysEx_InstrumentNumber]
		        .SetFromStream(m_sp_MidiDataOfMidiCommandInProgress);
		log_debug("processSysExCmd_InstrumentMessage_SetInstrumentConfiguration1() - copy end");
		InstrumentParameters* instr = getActiveInstrumentParameters(
		        m_sysEx_InstrumentNumber);
		loadInstrumentParameters_InstrumentConfiguration(
		        instr,
		        &m_activeConfiguration.instrumentConfigurations[m_sysEx_InstrumentNumber]);
		initMidiChannelToAssignedInstruments();
		sendResponse(0x00, ACK_MESSAGE);
	}

	// ROM Address: 0x2C77
	void processSysExCmd_InstrumentMessage_SetInstrumentConfiguration2(uint8_t /*midiData*/)
	{
		log_debug("processSysExCmd_InstrumentMessage_SetInstrumentConfiguration2()");
		ReadResult readResult = {};

		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0x00, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		if (receiveDataPacketTypeB(readResult.data,
		                           (uint8_t*)&m_sp_MidiDataOfMidiCommandInProgress,
		                           0x0B) != ReadStatus::Success) {
			return sendResponse(0x02, NAK_MESSAGE);
		}
		// this copy is kind of strange. It's not copying all the bytes.
		// So the following would not work:
		//  m_activeConfiguration.instrumentConfigurations[m_sysEx_InstrumentNumber].copyFrom((InstrumentConfiguration*)&m_sp_MidiDataOfMidiCommandInProgress);
		// instead we're just gonna do a memcpy
		m_activeConfiguration
		        .instrumentConfigurations[m_sysEx_InstrumentNumber]
		        .copySpecialFrom(
		                (InstrumentConfiguration*)&m_sp_MidiDataOfMidiCommandInProgress);
		InstrumentParameters* instr = getActiveInstrumentParameters(
		        m_sysEx_InstrumentNumber);
		setInstrumentParameter00_05_safe(
		        instr,
		        &m_activeConfiguration.instrumentConfigurations[m_sysEx_InstrumentNumber]);
		initMidiChannelToAssignedInstruments();
		sendResponse(0x00, ACK_MESSAGE);
	}

	// ROM Address: 0x2CC8
	void processSysExCmd_NodeParameterChangeMessage(uint8_t midiData)
	{
		log_debug("processSysExCmd_NodeParameterChangeMessage()");
		if (midiData >= 0x80) {
			return SoundProcessor_processMidiCommandByte(midiData);
		}
		if (m_soundProcessorMidiInterpreterSysExState == 0) {
			m_sp_MidiDataOfMidiCommandInProgress[0]   = midiData;
			m_soundProcessorMidiInterpreterSysExState = 0x10;
			return;
		}
		setNodeParameter(
		        m_sp_MidiDataOfMidiCommandInProgress[0] /* Parameter no */,
		        midiData);
		if (m_sp_MidiDataOfMidiCommandInProgress[0] == 0x20 ||
		    m_sp_MidiDataOfMidiCommandInProgress[0] == 0x22) {
			initializeSysExStateMatchTable();
			initMidiChannelToAssignedInstruments();
		}
		logSuccess();
		m_soundProcessorMidiInterpreterState = 0;
	}

	// ROM Address: 0x2CFD
	void processSysExCmd_InstrumentParameterChange_ByInstrument(uint8_t midiData)
	{
		log_debug("processSysExCmd_InstrumentParameterChange_ByInstrument() - begin");
		if (midiData >= 0x80) {
			return SoundProcessor_processMidiCommandByte(midiData);
		}

		switch (m_soundProcessorMidiInterpreterSysExState) {
		case 0:
			m_sp_MidiDataOfMidiCommandInProgress[0] = midiData;
			m_soundProcessorMidiInterpreterSysExState = midiData < 0x40
			                                                  ? 0x01
			                                                  : 0x10;
			return;
		case 1: break;
		case 0x10:
			if (midiData >= 0x10) {
				return SoundProcessor_processMidiCommandByte(midiData);
			}
			m_sp_MidiDataOfMidiCommandInProgress[1]   = midiData;
			m_soundProcessorMidiInterpreterSysExState = 0x11;
			return;
		default:
			if (midiData >= 0x10) {
				return SoundProcessor_processMidiCommandByte(midiData);
			}
			midiData = (midiData << 4) |
			           m_sp_MidiDataOfMidiCommandInProgress[1];
			break;
		}
		setInstrumentParameter(getActiveInstrumentParameters(
		                               m_sysEx_InstrumentNumber),
		                       m_sp_MidiDataOfMidiCommandInProgress[0],
		                       midiData);
		if (m_sp_MidiDataOfMidiCommandInProgress[0] == 1) {
			initMidiChannelToAssignedInstruments();
		}
		logSuccess();
		m_soundProcessorMidiInterpreterState = 0;
		log_debug("processSysExCmd_InstrumentParameterChange_ByInstrument() - end");
	}

	// ROM Address: 0x2D65
	void processSysExCmd_NodeDumpRequestMessage_VoiceMemoryBank(uint8_t midiData)
	{
		log_debug("processSysExCmd_NodeDumpRequestMessage_VoiceMemoryBank() - start");
		if (m_soundProcessorMidiInterpreterSysExState == 0) {
			if (midiData >= 0x07) {
				log_debug("processSysExCmd_NodeDumpRequestMessage_VoiceMemoryBank() - cancelling because midiData >= 0x07");
				return sendResponse(0x00, CANCEL_MESSAGE);
			}
			log_debug("processSysExCmd_NodeDumpRequestMessage_VoiceMemoryBank() - going into next state (0x10)");
			m_sp_MidiDataOfMidiCommandInProgress[0]   = midiData;
			m_soundProcessorMidiInterpreterSysExState = 0x10;
			return;
		}
		log_debug("processSysExCmd_NodeDumpRequestMessage_VoiceMemoryBank() - reached next state");
		if (midiData < 0x80) {
			return sendResponse(0x00, CANCEL_MESSAGE);
		}

		constexpr uint8_t first_midi_msg_byte = 0x00;
		constexpr uint8_t last_midi_msg_byte  = 0x00;

		if (send_F0_43_75_NodeNumber() == WriteStatus::Success &&
		    send_midi_byte(first_midi_msg_byte) == WriteStatus::Success &&
		    send_midi_byte(last_midi_msg_byte) == WriteStatus::Success &&
		    send_midi_byte(m_sp_MidiDataOfMidiCommandInProgress[0]) ==
		            WriteStatus::Success) {
			log_debug("processSysExCmd_NodeDumpRequestMessage_VoiceMemoryBank() - sending voice definition bank %i",
			          m_sp_MidiDataOfMidiCommandInProgress[0]);
			sendVoiceDefinitionBank(
			        m_sp_MidiDataOfMidiCommandInProgress[0]);
		}
		log_debug("processSysExCmd_NodeDumpRequestMessage_VoiceMemoryBank() - almost end");
		SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x2DAD
	void processSysExCmd_NodeDumpRequestMessage_ConfigurationBuffer1(uint8_t midiData)
	{
		log_debug("processSysExCmd_NodeDumpRequestMessage_ConfigurationBuffer1()");
		ReadResult readResult = {};

		// read the "source ID" byte (Technical Reference Manual 6-24)
		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0x00, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data < 0x80) {
			return sendResponse(0x00, CANCEL_MESSAGE);
		}
		log_debug("processSysExCmd_NodeDumpRequestMessage_ConfigurationBuffer1() - copy start");
		for (uint8_t i = 0; i < 8; i++) {
			m_activeConfiguration.instrumentConfigurations[i].copyFrom(
			        &(m_activeInstrumentParameters[i].instrumentConfiguration));
		}
		log_debug("processSysExCmd_NodeDumpRequestMessage_ConfigurationBuffer1() - copy end");

		constexpr uint8_t first_midi_msg_byte = 0x00;
		constexpr uint8_t last_midi_msg_byte  = 0x00;

		if (send_F0_43_75_NodeNumber() == WriteStatus::Success &&
		    send_midi_byte(first_midi_msg_byte) == WriteStatus::Success &&
		    send_midi_byte(0x01) == WriteStatus::Success &&
		    send_midi_byte(last_midi_msg_byte) == WriteStatus::Success) {
			sendDataPacketTypeBInChunksOf2048ByteBlocks(
			        (uint8_t*)m_activeConfiguration.instrumentConfigurations,
			        0xA0);
		}
		SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x2E07
	void processSysExCmd_NodeDumpRequestMessage_IndividualConfiguration(uint8_t midiData)
	{
		log_debug("processSysExCmd_NodeDumpRequestMessage_IndividualConfiguration()");
		if (m_soundProcessorMidiInterpreterSysExState == 0) {
			if (midiData >= 0x14) {
				return sendResponse(0x00, CANCEL_MESSAGE);
			}
			m_sp_MidiDataOfMidiCommandInProgress[0]   = midiData;
			m_soundProcessorMidiInterpreterSysExState = 0x10;
			return;
		}
		if (midiData < 0x80) {
			return sendResponse(0x00, CANCEL_MESSAGE);
		}
		if (send_F0_43_75_NodeNumber() == WriteStatus::Success &&
		    send_midi_byte(0x00) == WriteStatus::Success &&
		    send_midi_byte(0x02) == WriteStatus::Success &&
		    send_midi_byte(m_sp_MidiDataOfMidiCommandInProgress[0]) ==
		            WriteStatus::Success) {
			sendDataPacketTypeBInChunksOf2048ByteBlocks(
			        (const uint8_t*)getReadOnlyConfigurationData(
			                m_sp_MidiDataOfMidiCommandInProgress[0]),
			        0xA0);
		}
		SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x2E56
	void processSysExCmd_NodeDumpRequestMessage_ConfigurationRAM(uint8_t midiData)
	{
		log_debug("processSysExCmd_NodeDumpRequestMessage_ConfigurationRAM()");
		if (m_soundProcessorMidiInterpreterSysExState == 0) {
			m_soundProcessorMidiInterpreterSysExState = 1;
			return;
		}
		if (midiData < 0x80) {
			return sendResponse(0x00, CANCEL_MESSAGE);
		}

		constexpr uint8_t first_midi_msg_byte = 0x00;
		constexpr uint8_t last_midi_msg_byte  = 0x00;

		if (send_F0_43_75_NodeNumber() == WriteStatus::Success &&
		    send_midi_byte(first_midi_msg_byte) == WriteStatus::Success &&
		    send_midi_byte(0x03) == WriteStatus::Success &&
		    send_midi_byte(last_midi_msg_byte) == WriteStatus::Success) {
			sendAllConfigurations();
		}
		SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x2E8D
	void processSysExCmd_NodeDumpRequestMessage_MusicCardID(uint8_t midiData)
	{
		log_debug("processSysExCmd_NodeDumpRequestMessage_MusicCardID()");
		static constexpr char CARD_NAME[] = "YAMAHA IBM MUSIC";
		if (m_soundProcessorMidiInterpreterSysExState == 0) {
			m_soundProcessorMidiInterpreterSysExState = 1;
			return;
		}
		if (midiData < 0x80) {
			return sendResponse(0x00, CANCEL_MESSAGE);
		}

		constexpr uint8_t first_midi_msg_byte = 0x00;
		constexpr uint8_t last_midi_msg_byte  = 0x00;

		if (send_F0_43_75_NodeNumber() == WriteStatus::Success &&
		    send_midi_byte(first_midi_msg_byte) == WriteStatus::Success &&
		    send_midi_byte(0x04) == WriteStatus::Success &&
		    send_midi_byte(last_midi_msg_byte) == WriteStatus::Success) {
			sendDataPacketTypeBInChunksOf2048ByteBlocks((uint8_t*)&CARD_NAME,
			                                            16);
		}
		SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x2ECA
	void processSysExCmd_NodeDumpRequestMessage_MusicCardRevision(uint8_t midiData)
	{
		log_debug("processSysExCmd_NodeDumpRequestMessage_MusicCardRevision()");
		static constexpr char CARD_REV[] = "rel. M102.00.010";
		if (m_soundProcessorMidiInterpreterSysExState == 0) {
			m_soundProcessorMidiInterpreterSysExState = 1;
			return;
		}
		if (midiData < 0x80) {
			return sendResponse(0x00, CANCEL_MESSAGE);
		}

		constexpr uint8_t first_midi_msg_byte = 0x00;
		constexpr uint8_t last_midi_msg_byte  = 0x00;

		if (send_F0_43_75_NodeNumber() == WriteStatus::Success &&
		    send_midi_byte(first_midi_msg_byte) == WriteStatus::Success &&
		    send_midi_byte(0x05) == WriteStatus::Success &&
		    send_midi_byte(last_midi_msg_byte) == WriteStatus::Success) {
			sendDataPacketTypeBInChunksOf2048ByteBlocks((uint8_t*)&CARD_REV,
			                                            16);
		}
		SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x2F07
	void processSysExCmd_NodeDumpRequestMessage_ConfigurationBuffer2(uint8_t midiData1)
	{
		log_debug("processSysExCmd_NodeDumpRequestMessage_ConfigurationBuffer2()");
		ReadResult readResult = {};

		// read the "source ID" byte (Technical Reference Manual 6-24)
		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0, CANCEL_MESSAGE);
		}

		constexpr uint8_t first_midi_msg_byte = 0x00;
		constexpr uint8_t last_midi_msg_byte  = 0x00;

		// Note: There was a loop here that does absolutely nothing ?!
		if (send_F0_43_75_NodeNumber() == WriteStatus::Success &&
		    send_midi_byte(first_midi_msg_byte) == WriteStatus::Success &&
		    send_midi_byte(0x06) == WriteStatus::Success &&
		    send_midi_byte(last_midi_msg_byte) == WriteStatus::Success) {
			sendDataPacketTypeBInChunksOf2048ByteBlocks(
			        (uint8_t*)&m_activeConfiguration, 0xA0);
		}
		SoundProcessor_processMidiCommandByte(midiData1);
	}

	// ROM Address: 0x2F5C
	void processSysExCmd_StoreRequest_StoreConfigurationData(uint8_t midiData)
	{
		log_debug("processSysExCmd_StoreRequest_StoreConfigurationData()");
		if (m_soundProcessorMidiInterpreterSysExState == 0) {
			if (midiData >= 0x10) {
				return sendResponse(0, CANCEL_MESSAGE);
			}
			m_sp_MidiDataOfMidiCommandInProgress[0]   = midiData;
			m_soundProcessorMidiInterpreterSysExState = 0x10;
			return;
		}
		if (midiData < 0x80) {
			return sendResponse(0, CANCEL_MESSAGE);
		}
		if (m_memoryProtection == MEMORY_READONLY) {
			sendHandshakingMessage(CANCEL_MESSAGE);
		} else {
			storeActiveConfigurationToCustomConfiguration(
			        m_sp_MidiDataOfMidiCommandInProgress[0]);
			sendHandshakingMessage(ACK_MESSAGE);
		}
		SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x2F9C
	void processSysExCmd_F0_43_75_0n_2i_00(uint8_t midiData)
	{
		log_debug("processSysExCmd_F0_43_75_0n_2i_00");
		ReadResult readResult = {};

		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0, CANCEL_MESSAGE);
		}

		constexpr uint8_t first_midi_msg_byte = 0x00;
		constexpr uint8_t last_midi_msg_byte  = 0x00;

		if (send_F0_43_75_NodeNumber() == WriteStatus::Success &&
		    send_midi_byte(m_sysEx_InstrumentNumber | 0x08) ==
		            WriteStatus::Success &&
		    send_midi_byte(first_midi_msg_byte) == WriteStatus::Success &&
		    send_midi_byte(last_midi_msg_byte) == WriteStatus::Success) {
			InstrumentParameters* instr = getActiveInstrumentParameters(
			        m_sysEx_InstrumentNumber);
			sendDataPacketTypeAInChunksOf2048ByteBlocks(
			        (uint8_t*)&(instr->voiceDefinition), 0x40);
		}
		SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x2FE5
	void processSysExCmd_F0_43_75_0n_2i_01(uint8_t midiData)
	{
		log_debug("processSysExCmd_F0_43_75_0n_2i_01()");
		ReadResult readResult = {};

		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0, CANCEL_MESSAGE);
		}

		if (send_F0_43_75_NodeNumber() == WriteStatus::Success &&
		    send_midi_byte(m_sysEx_InstrumentNumber | 0x08) ==
		            WriteStatus::Success &&
		    send_midi_byte(0x01) == WriteStatus::Success &&
		    send_midi_byte(0x00) == WriteStatus::Success) {
			InstrumentParameters* instr = getActiveInstrumentParameters(
			        m_sysEx_InstrumentNumber);
			sendDataPacketTypeBInChunksOf2048ByteBlocks((uint8_t*)instr,
			                                            0x10);
		}
		SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x302B
	// This method is identical to "processSysExCmd_F0_43_75_0n_2i_01"
	void processSysExCmd_F0_43_75_0n_2i_02(uint8_t midiData)
	{
		log_debug("processSysExCmd_F0_43_75_0n_2i_02()");
		ReadResult readResult = {};

		do {
			readResult = readMidiData();
			if (readResult.status == ReadStatus::Error) {
				logMidiError();
				return sendResponse(0, NAK_MESSAGE);
			}
		} while (readResult.status == ReadStatus::NoData);
		if (readResult.data >= 0x80) {
			return sendResponse(0, CANCEL_MESSAGE);
		}

		if (send_F0_43_75_NodeNumber() == WriteStatus::Success &&
		    send_midi_byte(m_sysEx_InstrumentNumber | 0x08) ==
		            WriteStatus::Success &&
		    send_midi_byte(0x01) == WriteStatus::Success &&
		    send_midi_byte(0x00) == WriteStatus::Success) {
			InstrumentParameters* instr = getActiveInstrumentParameters(
			        m_sysEx_InstrumentNumber);
			sendDataPacketTypeBInChunksOf2048ByteBlocks((uint8_t*)instr,
			                                            0x10);
		}
		SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x3071
	void processSysExCmd_StoreRequest_StoreVoiceData(uint8_t midiData)
	{
		log_debug("processSysExCmd_StoreRequest_StoreVoiceData()");
		if (m_soundProcessorMidiInterpreterSysExState ==
		    0x00 /*SP_MidiInterpreterSysExState_InitialState*/) {
			if (midiData >= 0x60) {
				return sendResponse(0x00, CANCEL_MESSAGE);
			}
			m_sp_MidiDataOfMidiCommandInProgress[0]   = midiData;
			m_soundProcessorMidiInterpreterSysExState = 0x10;
			return;
		}
		if (midiData < 0x80) {
			return sendResponse(0x00, CANCEL_MESSAGE);
		}
		if (m_memoryProtection == MEMORY_READONLY) {
			sendHandshakingMessage(CANCEL_MESSAGE);
			return SoundProcessor_processMidiCommandByte(midiData);
		}
		InstrumentParameters* instr = getActiveInstrumentParameters(
		        m_sysEx_InstrumentNumber);
		storeInstrumentParametersToCustomBank(
		        instr, m_sp_MidiDataOfMidiCommandInProgress[0]);
		sendHandshakingMessage(ACK_MESSAGE);
		return SoundProcessor_processMidiCommandByte(midiData);
	}

	// ROM Address: 0x30BA
	void processSysExCmd_EventListTransferToMusicCard(uint8_t midiData)
	{
		log_debug("processSysExCmd_EventListTransferToMusicCard()");
		if (midiData >= 0x80) {
			return SoundProcessor_processMidiCommandByte(midiData);
		}

		if (m_soundProcessorMidiInterpreterSysExState ==
		    0x00 /*SP_MidiInterpreterSysExState_InitialState*/) {
			m_sp_MidiDataOfMidiCommandInProgress[0] = midiData | 0x80;
			m_soundProcessorMidiInterpreterSysExState = (midiData & 0x70) |
			                                            1;
			return;
		}
		switch ((m_soundProcessorMidiInterpreterSysExState & 0xf0) >> 4) {
		case 0: return processSysExCmd_EventList_NoteOFF(midiData);
		case 1: return processSysExCmd_EventList_NoteONOFF(midiData);
		case 2:
			return processSysExCmd_EventList_NoteONOFFWithDuration(midiData);
		case 3:
			return processSysExCmd_EventList_ControlChange(midiData);
		case 4:
			return processSysExCmd_EventList_ProgramChangeAndAfterTouch(
			        midiData);
		case 5:
			return processSysExCmd_EventList_ProgramChangeAndAfterTouch(
			        midiData);
		case 6:
			return processSysExCmd_EventList_PitchbenderRange(midiData);
		case 7:
			return processSysExCmd_EventList_ParameterChange(midiData);
		}
	}

	// ROM Address: 0x30F5
	void processSysExCmd_EventList_NoteOFF(uint8_t midiData)
	{
		switch (m_soundProcessorMidiInterpreterSysExState) {
		case 0x01 /*SP_MidiInterpreterSysExState_0x01*/:
			m_sp_MidiDataOfMidiCommandInProgress[1] = midiData;
			m_soundProcessorMidiInterpreterSysExState =
			        0x02 /*SP_MidiInterpreterSysExState_0x02*/;
			return;
		default:
			m_sp_MidiDataOfMidiCommandInProgress[2] = midiData;
			m_sp_MidiDataOfMidiCommandInProgress[3] = 0;
			m_sp_MidiDataOfMidiCommandInProgress[4] = 0;
			m_sp_MidiDataOfMidiCommandInProgress[5] = 0;
			executeMidiCommand_NoteONOFFForAllAssignInstruments();
			m_soundProcessorMidiInterpreterSysExState =
			        0x00 /*SP_MidiInterpreterSysExState_InitialState*/;
			return;
		}
	}

	// ROM Address: 0x311C
	void processSysExCmd_EventList_NoteONOFF(uint8_t midiData)
	{
		switch (m_soundProcessorMidiInterpreterSysExState) {
		case 0x11 /*SP_MidiInterpreterSysExState_0x11*/:
			m_sp_MidiDataOfMidiCommandInProgress[1] = midiData;
			m_soundProcessorMidiInterpreterSysExState =
			        0x12 /*SP_MidiInterpreterSysExState_0x12*/;
			return;
		case 0x12 /*SP_MidiInterpreterSysExState_0x12*/:
			m_sp_MidiDataOfMidiCommandInProgress[2] = midiData;
			m_soundProcessorMidiInterpreterSysExState =
			        0x13 /*SP_MidiInterpreterSysExState_0x13*/;
			return;
		default: // case 0x13/*SP_MidiInterpreterSysExState_0x13*/:
			m_sp_MidiDataOfMidiCommandInProgress[3] = midiData;
			m_sp_MidiDataOfMidiCommandInProgress[4] = 0;
			m_sp_MidiDataOfMidiCommandInProgress[5] = 0;
			executeMidiCommand_NoteONOFFForAllAssignInstruments();
			m_soundProcessorMidiInterpreterSysExState =
			        0x00 /*SP_MidiInterpreterSysExState_InitialState*/;
			return;
		}
	}

	// ROM Address: 0x314F
	void processSysExCmd_EventList_NoteONOFFWithDuration(uint8_t midiData)
	{
		m_sp_MidiDataOfMidiCommandInProgress[m_soundProcessorMidiInterpreterSysExState -
		                                     0x20] = midiData;
		if (m_soundProcessorMidiInterpreterSysExState ==
		    0x25 /*SP_MidiInterpreterSysExState_0x25*/) {
			executeMidiCommand_NoteONOFFForAllAssignInstruments();
			m_soundProcessorMidiInterpreterSysExState =
			        0x00 /*SP_MidiInterpreterSysExState_InitialState*/;
			return;
		}
		m_soundProcessorMidiInterpreterSysExState++;
	}

	// ROM Address: 0x316F
	void processSysExCmd_EventList_ControlChange(uint8_t midiData)
	{
		switch (m_soundProcessorMidiInterpreterSysExState) {
		case 0x31 /*SP_MidiInterpreterSysExState_0x31*/:
			m_sp_MidiDataOfMidiCommandInProgress[1] = midiData;
			m_soundProcessorMidiInterpreterSysExState =
			        0x32 /*SP_MidiInterpreterSysExState_0x32*/;
			return;
		case 0x32 /*SP_MidiInterpreterSysExState_0x32*/:
		default:
			m_sp_MidiDataOfMidiCommandInProgress[2] = midiData;
			SoundProcessor_executeMidiCommand();
			m_soundProcessorMidiInterpreterSysExState =
			        0x00 /*SP_MidiInterpreterSysExState_InitialState*/;
			return;
		}
	}

	// ROM Address: 0x318C
	void processSysExCmd_EventList_ProgramChangeAndAfterTouch(uint8_t midiData)
	{
		m_sp_MidiDataOfMidiCommandInProgress[1] = midiData;
		m_sp_MidiDataOfMidiCommandInProgress[2] = 0;
		SoundProcessor_executeMidiCommand();
		m_soundProcessorMidiInterpreterSysExState =
		        0x00 /*SP_MidiInterpreterSysExState_InitialState*/;
	}

	// ROM Address: 0x319A
	void processSysExCmd_EventList_PitchbenderRange(uint8_t midiData)
	{
		m_soundProcessorMidiInterpreterSysExState = 0x31 /*SP_MidiInterpreterSysExState_0x31*/;
		processSysExCmd_EventList_ControlChange(midiData);
	}

	// ROM Address: 0x31A1
	void processSysExCmd_EventList_ParameterChange(uint8_t midiData)
	{
		switch (m_soundProcessorMidiInterpreterSysExState) {
		case 0x71 /*SP_MidiInterpreterSysExState_0x71*/:
			m_sysEx_ChannelNumber =
			        m_sp_MidiDataOfMidiCommandInProgress[0] & 0x0F;
			m_sp_MidiDataOfMidiCommandInProgress[0] = midiData;
			m_soundProcessorMidiInterpreterSysExState =
			        midiData >= 0x40 ? 0x73 /*SP_MidiInterpreterSysExState_0x73*/ : 0x72 /*SP_MidiInterpreterSysExState_0x72*/;
			return;

		case 0x72 /*SP_MidiInterpreterSysExState_0x72*/:
			m_sp_MidiDataOfMidiCommandInProgress[1] = midiData;
			break;

		case 0x73 /*SP_MidiInterpreterSysExState_0x73*/:
			if (midiData >= 16) {
				return SoundProcessor_processMidiCommandByte(midiData);
			}
			m_sp_MidiDataOfMidiCommandInProgress[1] = midiData;
			m_soundProcessorMidiInterpreterSysExState =
			        0x74 /*SP_MidiInterpreterSysExState_0x74*/;
			return;

		default:
			if (midiData >= 16) {
				return SoundProcessor_processMidiCommandByte(midiData);
			}
			m_sp_MidiDataOfMidiCommandInProgress[2] = midiData;
			break;
		}
		processSysExCmd_InstrumentParameterChange();
		if (m_sp_MidiDataOfMidiCommandInProgress[0] == 0x01) {
			initMidiChannelToAssignedInstruments();
		}
		logSuccess();
		m_soundProcessorMidiInterpreterSysExState =
		        0x00 /*SP_MidiInterpreterSysExState_InitialState*/;
	}

	// ROM Address: 0x31FD
	void processSysExCmd_ParameterListTransferToMusicCard(uint8_t midiData)
	{
		log_debug("processSysExCmd_ParameterListTransferToMusicCard()");
		if (midiData >= 0x80) {
			return SoundProcessor_processMidiCommandByte(midiData);
		}

		switch (m_soundProcessorMidiInterpreterSysExState) {
		case 0x00 /*SP_MidiInterpreterSysExState_InitialState*/:
			m_sp_MidiDataOfMidiCommandInProgress[0] = midiData;
			m_soundProcessorMidiInterpreterSysExState =
			        0x01 /*SP_MidiInterpreterSysExState_0x01*/;
			return;

		case 0x01 /*SP_MidiInterpreterSysExState_0x01*/:
			m_sp_MidiDataOfMidiCommandInProgress[1] = midiData & 0x7F;
			m_soundProcessorMidiInterpreterSysExState =
			        (midiData & 0b00100000) == 0 ? 0x02 /*SP_MidiInterpreterSysExState_0x02*/ : 0x03 /*SP_MidiInterpreterSysExState_0x03*/;
			return;

		case 0x03 /*SP_MidiInterpreterSysExState_0x03*/:
			if (midiData >= 16) {
				return SoundProcessor_processMidiCommandByte(midiData);
			}
			m_sp_MidiDataOfMidiCommandInProgress[2] = midiData;
			m_soundProcessorMidiInterpreterSysExState =
			        0x04 /*SP_MidiInterpreterSysExState_0x04*/;
			return;

		default: // Cases 0x02 and others land here ...
			// SP_MidiInterpreterSysExState_0x04
			if (m_soundProcessorMidiInterpreterSysExState != 0x02) {
				if (midiData >= 16) {
					return SoundProcessor_processMidiCommandByte(
					        midiData);
				}
				midiData = midiData << 4 |
				           m_sp_MidiDataOfMidiCommandInProgress[2];
				// no break! Continue like if it was case 2
			}
			// case 0x02 /*SP_MidiInterpreterSysExState_0x02*/:
			if ((m_sp_MidiDataOfMidiCommandInProgress[0] & 0x0F) ==
			    m_nodeNumber) {
				InstrumentParameters* inst = getActiveInstrumentParameters(
				        (m_sp_MidiDataOfMidiCommandInProgress[0] &
				         0x70) >>
				        4);
				setInstrumentParameter(
				        inst,
				        m_sp_MidiDataOfMidiCommandInProgress[1],
				        midiData);
				if (m_sp_MidiDataOfMidiCommandInProgress[1] == 1) {
					initMidiChannelToAssignedInstruments();
				}
				logSuccess();
			}
			m_soundProcessorMidiInterpreterSysExState =
			        0x00 /*SP_MidiInterpreterSysExState_InitialState*/;
		}
	}

	// ROM Address: 0x327F
	void sendResponse(uint8_t errorNumber,
	                  HANDSHAKE_MESSAGE handshakingMessageNumber)
	{
		logDumpError(errorNumber);
		sendHandshakingMessage(handshakingMessageNumber); // ignore err
		                                                  // code
		m_soundProcessorMidiInterpreterState = 0 /*SP_MidiInterpreterState_InitialState*/;
	}

	// ROM Address: 0x328A
	WriteStatus sendDataPacketTypeAInChunksOf2048ByteBlocks(const uint8_t* data,
	                                                        uint16_t dataSize)
	{
		WriteStatus writeStatus;
		if (dataSize <= 0x0800) {
			writeStatus = sendDataPacketTypeA(data, dataSize);
			if (writeStatus != WriteStatus::Success) {
				return writeStatus;
			}
			return send_midi_byte(0xF7);
		}
		writeStatus = sendDataPacketTypeA(data, 0x0800);
		if (writeStatus != WriteStatus::Success) {
			return writeStatus;
		}
		waitForDataToBeSent();
		return sendDataPacketTypeAInChunksOf2048ByteBlocks(data + 0x0800,
		                                                   dataSize - 0x0800);
	}

	// ROM Address: 0x32B7
	WriteStatus sendDataPacketTypeBInChunksOf2048ByteBlocks(const uint8_t* data,
	                                                        uint16_t dataSize)
	{
		WriteStatus writeStatus;
		if (dataSize <= 0x0800) {
			writeStatus = sendDataPacketTypeB(data, dataSize);
			if (writeStatus != WriteStatus::Success) {
				return writeStatus;
			}
			return send_midi_byte(0xF7);
		}
		writeStatus = sendDataPacketTypeB(data, 0x0800);
		if (writeStatus != WriteStatus::Success) {
			return writeStatus;
		}
		waitForDataToBeSent();
		return sendDataPacketTypeBInChunksOf2048ByteBlocks(data + 0x0800,
		                                                   dataSize - 0x0800);
	}

	// ROM Address: 0x32E4
	WriteStatus sendVoiceDefinitionBank(uint8_t instrumentBankNumber)
	{
		log_debug("sendVoiceDefinitionBank() - start");
		WriteStatus writeStatus;

		const VoiceDefinitionBank* bank = getVoiceDefinitionBank(
		        instrumentBankNumber);
		log_debug("sendVoiceDefinitionBank() - bank.name %c%c%c%c%c%c",
		          bank->name[0],
		          bank->name[1],
		          bank->name[2],
		          bank->name[3],
		          bank->name[4],
		          bank->name[5]);
		// send bank header (i.e. name and reserved field)
		log_debug("sendVoiceDefinitionBank() - sendDataPacketTypeA() - header");
		writeStatus = sendDataPacketTypeA((const uint8_t*)bank, 0x20);
		if (writeStatus != WriteStatus::Success) {
			return writeStatus;
		}
		// send all the voice data
		for (auto& instrumentDefinition : bank->instrumentDefinitions) {
			log_debug("sendVoiceDefinitionBank() - waitForDataToBeSent()");
			waitForDataToBeSent();
			log_debug("sendVoiceDefinitionBank() - sendDataPacketTypeA() - data");
			writeStatus = sendDataPacketTypeA((const uint8_t*)&instrumentDefinition,
			                                  sizeof(VoiceDefinition));
			if (writeStatus != WriteStatus::Success) {
				return writeStatus;
			}
		}
		log_debug("sendVoiceDefinitionBank() - almost end");
		return send_midi_byte(0xF7);
	}

	// ROM Address: 0x3303
	WriteStatus sendAllConfigurations()
	{
		WriteStatus writeStatus;
		for (auto& i : m_configurationRAM) {
			writeStatus = sendDataPacketTypeB((const uint8_t*)&i,
			                                  0xA0 /*sizeof(ConfigurationType)*/);
			if (writeStatus != WriteStatus::Success) {
				return writeStatus;
			}
			waitForDataToBeSent();
		}
		return send_midi_byte(0xF7);
	}

	// ROM Address: 0x331A
	void waitForDataToBeSent()
	{
		// FIXME: Implementation that looks more like the original
		if ((m_midi_ReceiveSource_SendTarget & 1) != 0) {
			// target is "system"
			m_bufferToSystemState.lock();
			while (!m_bufferToSystemState.isEmpty()) {
				m_bufferToSystemState.unlock();
				m_bufferToSystemState.lock();
			};
			m_bufferToSystemState.unlock();
		} else {
			// target is "midi out"
			m_bufferToMidiOutState.lock();
			while (!m_bufferToMidiOutState.isEmpty()) {
				m_bufferToMidiOutState.unlock();
				m_bufferToMidiOutState.lock();
			}
			m_bufferToMidiOutState.unlock();
		}

		// clang-format off
		/* Original implementation:
		uint16_t timeout = 0;
		do {
			if (m_midi_ReceiveSource_SendTarget & 1) {
				// target is "system"
				if (m_bufferToSystemState.isEmpty()) { break; }
			} else {
				// target is "midi out"
				if (m_bufferToMidiOutState.isEmpty()) { break; }
			}
			timeout--;
		} while (timeout > 0);
		timeout = 0x85E;
		do {
			timeout--;
		} while (timeout);
		*/
		// clang-format on
	}

#define send_midi_byte_with_error_handling(data) \
	writeStatus = send_midi_byte(data); \
	if (writeStatus != WriteStatus::Success) { \
		return writeStatus; \
	}

	// ROM Address: 0x3343
	WriteStatus sendDataPacketTypeA(const uint8_t* data, uint16_t dataSize)
	{
		WriteStatus writeStatus;
		uint8_t checksum = 0;

		// we need to double the size since we're sending two bytes per
		// input byte
		const uint16_t numberOfBytesToSend = dataSize * 2;

		// send size
		send_midi_byte_with_error_handling((numberOfBytesToSend >> 7) & 0x7F);
		send_midi_byte_with_error_handling(numberOfBytesToSend & 0x7F);

		// send data
		checksum = 0;
		auto data_ptr = data;
		do {
			const uint8_t dataByte = *data_ptr;
			send_midi_byte_with_error_handling(dataByte & 0x0F);
			checksum += dataByte & 0x0F;
			send_midi_byte_with_error_handling((dataByte >> 4) & 0x0F);
			checksum += (dataByte >> 4) & 0x0F;
			data_ptr++;
			dataSize--;
		} while (dataSize != 0U);

		// return checksum
		return send_midi_byte((-checksum) & 0x7F);
	}

	// ROM Address: 0x338D
	WriteStatus sendDataPacketTypeB(const uint8_t* data, uint16_t dataSize)
	{
		WriteStatus writeStatus;
		uint8_t checksum = 0;

		// send size
		send_midi_byte_with_error_handling((dataSize >> 7) & 0x7F);
		send_midi_byte_with_error_handling(dataSize & 0x7F);

		// send data
		auto data_ptr = data;
		checksum = 0;
		do {
			const uint8_t dataByte = *data_ptr;
			send_midi_byte_with_error_handling(dataByte);
			checksum += dataByte;
			data_ptr++;
			dataSize--;
		} while (dataSize != 0U);

		// return checksum
		return send_midi_byte((-checksum) & 0x7F);
	}

#define readMidiDataWithErrorHandling(target) \
	do { \
		(target) = readMidiData(); \
		if ((target).status == ReadStatus::Error) { \
			logMidiError(); \
			return ReadStatus::Error; \
		} \
	} while ((target).status == ReadStatus::NoData);

	// ROM Address: 0x33BE
	ReadStatus receiveDataPacketTypeA(uint8_t byteCountHigh,
	                                  uint8_t* data_rw, uint16_t bufferSize)
	{
		m_receiveDataPacketTypeAState = 0;
		return receiveDataPacketTypeA_internal(byteCountHigh, data_rw, bufferSize);
	}

	// ROM Address: 0x33D7
	ReadStatus receiveDataPacketTypeA_internal(uint8_t byteCountHigh,
	                                           uint8_t* data_rw,
	                                           uint16_t bufferSize)
	{
		ReadResult readResult   = {};
		uint8_t checksum        = 0;
		uint16_t dataPacketSize = 0;

		log_debug("receiveDataPacketTypeA_internal(%02X) - begin - bufferSize=0x%X",
		          byteCountHigh,
		          bufferSize);

		checksum = 0;

		if (m_receiveDataPacketTypeAState == 0) { /* do nothing ?!?!
			                                     Really ?!?! WTF! */
		}

		readMidiDataWithErrorHandling(readResult);
		if (readResult.data >= 0x80) {
			log_debug("receiveDataPacketTypeA_internal - midi command not allowed (0)");
			return ReadStatus::Error;
		}

		if (readResult.data == 0x20 && byteCountHigh == 0 &&
		    m_soundProcessorMidiInterpreterState == 0x31) {
			m_receiveDataPacketTypeAState = 1;
		} else if (m_receiveDataPacketTypeAState != 0) {
			// in m_receiveDataPacketTypeAState=1, the
			// dataPacketSize must be 0x840, otherwise ->
			// ReadStatus::Error
			if (readResult.data != 0x10) {
				log_debug("receiveDataPacketTypeA_internal - size error(1) %i != 0x10",
				          readResult.data);
				return ReadStatus::Error;
			}
			if (byteCountHigh != 0x40) {
				log_debug("receiveDataPacketTypeA_internal - size error(1) %i != 0x40",
				          byteCountHigh);
				return ReadStatus::Error;
			}
		}
		dataPacketSize = (uint16_t)(byteCountHigh * 128 +
		                                      readResult.data);
		// log_debug("receiveDataPacketTypeA_internal - new
		// dataPacketSize(1) 0x%04x", dataPacketSize);

		while (keepRunning.load()) {
			// did we reach the end of the packet size?
			if (dataPacketSize == 0) {
				// we're expecting a checksum byte
				readMidiDataWithErrorHandling(readResult);
				if (readResult.data >= 0x80) {
					log_debug("receiveDataPacketTypeA_internal - midi command not allowed (1)");
					return ReadStatus::Error;
				}
				if (((checksum + readResult.data) & 0x7F) != 0) {
					log_debug("receiveDataPacketTypeA_internal - checksum error(1)");
					return ReadStatus::Error;
				}

				// read the high byte of the size
				readMidiDataWithErrorHandling(readResult);
				log_debug("receiveDataPacketTypeA_internal - recursive call");
				return receiveDataPacketTypeA_internal(
				        readResult.data, data_rw, bufferSize);
			}

			// read the data byte (low half)
			readMidiDataWithErrorHandling(readResult);
			if (readResult.data >= 0x40) {
				log_debug("receiveDataPacketTypeA_internal - ERROR");
				return ReadStatus::Error;
			}
			// "remember" the low-nibble in
			// m_soundProcessorMidiInterpreterState
			m_soundProcessorMidiInterpreterState = readResult.data;
			// log("receiveDataPacketTypeA_internal -
			// m_soundProcessorMidiInterpreterState now at %02X",
			// m_soundProcessorMidiInterpreterState);
			checksum += readResult.data;
			dataPacketSize--;

			// we have reach the end of the transmitted data packet.
			// Read and check the checksum and read the size of the
			// next packet
			while (dataPacketSize == 0) {
				// read the checksum byte
				readMidiDataWithErrorHandling(readResult);
				if (readResult.data >= 0x80) {
					log_debug("receiveDataPacketTypeA_internal - midi command not allowed (2)");
					return ReadStatus::Error;
				}
				if (((checksum + readResult.data) & 0x7F) != 0) {
					log_debug("receiveDataPacketTypeA_internal - checksum error(2)");
					return ReadStatus::Error;
				}
				checksum = 0;
				// read the high byte of the size
				readMidiDataWithErrorHandling(readResult);
				if (readResult.data >= 0x21) {
					return ReadStatus::Error;
				}
				byteCountHigh = readResult.data;
				// read the low byte of the size
				readMidiDataWithErrorHandling(readResult);
				if (readResult.data >= 0x80) {
					return ReadStatus::Error;
				}
				dataPacketSize = (uint16_t)(
				        byteCountHigh * 128 + readResult.data);
				log_debug("receiveDataPacketTypeA_internal - new dataPacketSize(2) 0x%04x",
				          dataPacketSize);
			}

			// read the data byte (high half)
			readMidiDataWithErrorHandling(readResult);
			if (readResult.data >= 0x40) {
				return ReadStatus::Error;
			}
			checksum += readResult.data;
			data_rw[0] = (readResult.data << 4) |
			             m_soundProcessorMidiInterpreterState;
			// log_debug("receiveDataPacketTypeA_internal - writing
			// 0x%02X - bufferSize=%i", data_rw[0], bufferSize-1);
			data_rw++;
			bufferSize--;
			if (bufferSize == 0) {
				log_debug("receiveDataPacketTypeA_internal - reached the end of the target buffer. There are still %i bytes left to read",
				          dataPacketSize - 1);
				break;
			}
			dataPacketSize--;
		}

		// read the additional data
		while (--dataPacketSize != 0U) {
			readMidiDataWithErrorHandling(readResult);
			if (readResult.data >= 0x80) {
				return ReadStatus::Error;
			}
			checksum += readResult.data;
		}

		// read the checksum
		readMidiDataWithErrorHandling(readResult);
		if (readResult.data >= 0x80) {
			log_debug("receiveDataPacketTypeA_internal - midi command not allowed (3)");
			return ReadStatus::Error;
		}
		if (((checksum + readResult.data) & 0x7F) != 0) {
			log_debug("receiveDataPacketTypeA_internal - checksum error(3)");
			return ReadStatus::Error;
		}
		log_debug("receiveDataPacketTypeA_internal - end");
		return ReadStatus::Success;
	}

	// ROM Address: 0x351C
	ReadStatus receiveDataPacketTypeB(uint8_t byteCountHigh,
	                                  uint8_t* data_rw, uint16_t bufferSize)
	{
		ReadResult readResult   = {};
		uint8_t checksum        = 0;
		uint16_t dataPacketSize = 0;

		checksum = 0;
		// read the second byte of the size
		readMidiDataWithErrorHandling(readResult);
		if (readResult.data >= 0x80) {
			return ReadStatus::Error;
		}
		dataPacketSize = (uint16_t)(byteCountHigh * 0x80 +
		                                      readResult.data);

		while (keepRunning.load()) {
			// read the data
			if (dataPacketSize == 0) {
				// read the checksum
				readMidiDataWithErrorHandling(readResult);
				if (readResult.data >= 0x80) {
					return ReadStatus::Error;
				}
				if (((checksum + readResult.data) & 0x7F) != 0) {
					return ReadStatus::Error;
				}

				// we need a new packet: read the first byte of
				// the size and recurse
				readMidiDataWithErrorHandling(readResult);
				return receiveDataPacketTypeB(readResult.data,
				                              data_rw,
				                              bufferSize);
			}
			// read data byte
			readMidiDataWithErrorHandling(readResult);
			if (readResult.data >= 0x80) {
				return ReadStatus::Error;
			}
			checksum += readResult.data;
			data_rw[0] = readResult.data;
			data_rw++;
			bufferSize--;
			if (bufferSize == 0) {
				break;
			}
			dataPacketSize--;
		}

		// read the additional data
		while (--dataPacketSize != 0U) {
			readMidiDataWithErrorHandling(readResult);
			if (readResult.data >= 0x80) {
				return ReadStatus::Error;
			}
			checksum += readResult.data;
		}

		// read the checksum
		readMidiDataWithErrorHandling(readResult);
		if (readResult.data >= 0x80) {
			return ReadStatus::Error;
		}
		if (((checksum + readResult.data) & 0x7F) != 0) {
			return ReadStatus::Error;
		}
		return ReadStatus::Success;
	}

	// ROM Address: 0x35C9
	WriteStatus send_F0_43_75_NodeNumber()
	{
		WriteStatus writeStatus;
		writeStatus = send_midi_byte(0xf0);
		if (writeStatus != WriteStatus::Success) {
			return writeStatus;
		}
		writeStatus = send_midi_byte(0x43);
		if (writeStatus != WriteStatus::Success) {
			return writeStatus;
		}
		writeStatus = send_midi_byte(0x75);
		if (writeStatus != WriteStatus::Success) {
			return writeStatus;
		}
		return send_midi_byte(m_nodeNumber);
	}

	// ROM Address: 0x3600
	void logDumpError(const uint8_t messageNr)
	{
		switch (messageNr) {
		case 0: return;
		case 1: logError("dump/received !!"); break;
		case 2: logError("dump/error   !!!"); break;
		case 3: logError("dump/protected !"); break;
		default: logError("dump/unknown !!"); break;
		}
	}

	// ROM Address: 0x3648
	WriteStatus sendHandshakingMessage(HANDSHAKE_MESSAGE handshakingMessageNumber)
	{
		WriteStatus writeStatus;
		writeStatus = send_midi_byte(0xf0);
		if (writeStatus != WriteStatus::Success) {
			return writeStatus;
		}
		writeStatus = send_midi_byte(0x43);
		if (writeStatus != WriteStatus::Success) {
			return writeStatus;
		}
		writeStatus = send_midi_byte(m_nodeNumber | 0x60);
		if (writeStatus != WriteStatus::Success) {
			return writeStatus;
		}
		writeStatus = send_midi_byte(handshakingMessageNumber);
		if (writeStatus != WriteStatus::Success) {
			return writeStatus;
		}
		return send_midi_byte(0xf7);
	}
	// clang-format off
	// ROM Address: 0x3648
	/*
		Populates the mapping table "midiChannel to currently assigned instruments"
		Note: 0xFF is used as a "terminator" byte; that's why there is a "+1"
	*/
	// clang-format on
	void initMidiChannelToAssignedInstruments()
	{
		// log("initMidiChannelToAssignedInstruments() - begin");
		//  clear out the existing data
		for (auto& m_midiChannelToAssignedInstrument :
		     m_midiChannelToAssignedInstruments) {
			for (uint8_t i = 0; i < AVAILABLE_INSTRUMENTS + 1; i++) {
				m_midiChannelToAssignedInstrument[i] = 0xFF;
			}
		}
		// iterate through all the instruments, extract the midi channel
		// and add it to the end of the list
		// log("initMidiChannelToAssignedInstruments() - assign");
		for (uint8_t i = 0; i < AVAILABLE_INSTRUMENTS; i++) {
			InstrumentParameters* curInstParams =
			        getActiveInstrumentParameters(i);
			const uint8_t curMidiChannel =
			        curInstParams->instrumentConfiguration.midiChannel &
			        0x0F;
			uint8_t t = 0;
			while (m_midiChannelToAssignedInstruments[curMidiChannel][t] !=
			       0xFF) {
				t++;
			}; // go to first 0xFF occurance
			m_midiChannelToAssignedInstruments[curMidiChannel][t] = i;
		}
		log_debug("initMidiChannelToAssignedInstruments:");
		for (uint8_t c = 0; c < AVAILABLE_MIDI_CHANNELS; c++) {
			log_debug("   Channel %2i: %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			          c,
			          m_midiChannelToAssignedInstruments[c][0],
			          m_midiChannelToAssignedInstruments[c][1],
			          m_midiChannelToAssignedInstruments[c][2],
			          m_midiChannelToAssignedInstruments[c][3],
			          m_midiChannelToAssignedInstruments[c][4],
			          m_midiChannelToAssignedInstruments[c][5],
			          m_midiChannelToAssignedInstruments[c][6],
			          m_midiChannelToAssignedInstruments[c][7],
			          m_midiChannelToAssignedInstruments[c][8]);
		}
		// log("initMidiChannelToAssignedInstruments() - end");
	}

	// clang-format off
	// ROM Address: 0x3680
	/*
		Populates the list of sysEx commands that are processable during runtime.
		I don't know why IBM/Yamaha chose to do it this way. They could have
		checked the NodeNumber at runtime instead doubling the structures
		called during card initialization or when the NodeNumber changes
	*/
	// clang-format on
	void initializeSysExStateMatchTable()
	{
		// clang-format off
		//log("initializeSysExStateMatchTable() - begin");
		/*
			0x00-0x7F:normal bytes
			0x8n:NodeByte -> 0xn0 | NodeNumber
			0x9n:InstrumentNumber
			0xAn:ChannelNumber
			0xF0:ignore
			0xFF+0xss:following byte is the new state (0xss)
		*/
		// clang-format on

		static constexpr uint8_t SP_SysExStateMatchTableTemplate[] = {
		        // clang-format off
			0x43,0x80,0x0C,0xFF,0x31,0x00,0x00,
			0x43,0x80,0xF0,0x00,0x00,0x00,0x00,
			0x43,0xA1,0x15,0xFF,0x32,0x00,0x00,
			0x43,0xA1,0xF0,0x00,0x00,0x00,0x00,
			0x43,0x82,0x0C,0xFF,0x33,0x00,0x00,
			0x43,0x82,0xF0,0x00,0x00,0x00,0x00,
			0x43,0x75,0x80,0x00,0x00,0xFF,0x34,
			0x43,0x75,0x80,0x00,0x01,0xFF,0x35,
			0x43,0x75,0x80,0x00,0x02,0xFF,0x36,
			0x43,0x75,0x80,0x00,0x03,0xFF,0x37,
			0x43,0x75,0x80,0x00,0x06,0xFF,0x46,
			0x43,0x75,0x80,0x00,0xF0,0x00,0x00,
			0x43,0x75,0x80,0x90,0x00,0xFF,0x38,
			0x43,0x75,0x80,0x90,0x01,0xFF,0x47,
			0x43,0x75,0x80,0x90,0x02,0xFF,0x48,
			0x43,0x75,0x80,0x90,0xF0,0x00,0x00,
			0x43,0x75,0x80,0x10,0xFF,0x39,0x00,
			0x43,0x75,0x80,0x91,0xFF,0x3A,0x00,
			0x43,0x75,0x80,0x20,0x00,0xFF,0x3B,
			0x43,0x75,0x80,0x20,0x01,0xFF,0x3C,
			0x43,0x75,0x80,0x20,0x02,0xFF,0x3D,
			0x43,0x75,0x80,0x20,0x03,0xFF,0x3E,
			0x43,0x75,0x80,0x20,0x04,0xFF,0x3F,
			0x43,0x75,0x80,0x20,0x05,0xFF,0x40,
			0x43,0x75,0x80,0x20,0x06,0xFF,0x49,
			0x43,0x75,0x80,0x20,0x40,0xFF,0x41,
			0x43,0x75,0x80,0x20,0xF0,0x00,0x00,
			0x43,0x75,0x80,0x92,0x00,0xFF,0x42,
			0x43,0x75,0x80,0x92,0x01,0xFF,0x4A,
			0x43,0x75,0x80,0x92,0x02,0xFF,0x4B,
			0x43,0x75,0x80,0x92,0x40,0xFF,0x43,
			0x43,0x75,0x80,0xF0,0xF0,0x00,0x00,
			0x43,0x75,0x70,0xFF,0x44,0x00,0x00,
			0x43,0x75,0x71,0xFF,0x45,0x00,0x00,
			0xF0,0xF0,0xF0
		        // clang-format on
		};
		for (uint8_t i = 0; i < sizeof(SP_SysExStateMatchTableTemplate); i++) {
			uint8_t b = SP_SysExStateMatchTableTemplate[i];
			if (b < 0x80) {
				// normal midi data -> keep data as-is
			} else if (b >= 0x90) {
				if (b == 0xFF) {
					m_sp_SysExStateMatchTable[i] = b;
					i++;

					// Although incrementing 'i' can technically exceed the for
					// loop's bounds, we know the prior "b == 0xFF" condition
					// per the read-only table value means that 'i' will never
					// be close to the loop end; so simply assert it:
					//
					assert(i < sizeof(SP_SysExStateMatchTableTemplate));

					b = SP_SysExStateMatchTableTemplate[i];
				}
			} else {
				// for all the 0x8X values -> Xn
				b = ((b << 4) & 0xF0) | m_nodeNumber;
			}
			m_sp_SysExStateMatchTable[i] = b;
		}
		// log("initializeSysExStateMatchTable() - end");
	}

public:
	MusicFeatureCard(const MusicFeatureCard&)            = delete;
	MusicFeatureCard& operator=(const MusicFeatureCard&) = delete;

	MusicFeatureCard(const io_port_t port,
	                 const uint8_t irq)
	        : m_ya2151(),
	          // create all the instances
	          m_tcr("TCR"),
	          m_piuPC("PIU_PC"),
	          m_piuPC_int0("PIU_PC.RxRDY(INT0)", false),
	          m_piuPC_int1("PIU_PC.TxRDY(INT1)", false),
	          m_piuIMF("PIU_IMF"),
	          m_piuIMF_int0("PIU_IMF.TxRDY(INT0)", false),
	          m_piuIMF_int1("PIU_IMF.RxRDY(INT1)", false),
	          m_piuPort0Data("PIU.port0", 0),
	          m_piuPort1Data("PIU.port1", 0),
	          m_piuEXR8("PIU.EXR8", false),
	          m_piuEXR9("PIU.EXR9", false),
	          m_piuGroup0DataAvailable("PIU.Group0DataAvailable", false),
	          m_piuGroup0DataAcknowledgement("PIU.Group0DataAcknowledgement",
	                                         false),
	          m_piuGroup1DataAvailable("PIU.Group1DataAvailable", false),
	          m_piuGroup1DataAcknowledgement("PIU.Group1DataAcknowledgement",
	                                         false),
	          m_timer("TIMER"),
	          m_invTimerAClear("TCR.timerAClear.inverted"),
	          m_df1("DF1"),
	          m_invTimerBClear("TCR.timerBClear.inverted"),
	          m_df2("DF2"),
	          m_totalCardStatus("TCS"),
	          m_irqMaskGate("IRQ Mask Gate"),
	          m_irqStatus("TriStateIrqBuffer"),
	          m_irqTriggerPc(
	                  "TriggerPcIrq",
	                  [irq]() {
		                  IMF_LOG("ACTIVATING PC IRQ!!!", "");
		                  PIC_ActivateIRQ(irq);
	                  } /*callbackOnLowToHigh*/,
	                  nullptr /*callbackOnToHighToLow*/),
	          m_irqTriggerImf(
	                  "TriggerImfIrq",
	                  [this]() {
	                  } /*callbackOnLowToHigh*/,
	                  [this]() {
	                  } /*callbackOnToHighToLow*/),
	          m_tsr("TSR"),
	          // initialize all the internal structures
	          m_bufferFromMidiInState("bufferFromMidiInState", 2048),
	          m_bufferToMidiOutState("bufferToMidiOutState", 256),

	          // FIXME: The original only has a buffer of 256, but since the
	          // "main" thread is a bit slow, we need to increase it a LOT
	          m_bufferFromSystemState("bufferFromSystemState", 0x2000),
	          m_bufferToSystemState("bufferToSystemState", 256)
	{
		using namespace std::chrono_literals;

		// now wire everything up (see Figure "2-1 Music Card Interrupt
		// System" in the Techniucal Reference Manual)

		// the ports 0 and 1 of PIU_A and PIU_B are inter-connected
		m_piuPC.connectPort0(&m_piuPort0Data);
		m_piuIMF.connectPort0(&m_piuPort0Data);
		m_piuPC.connectPort1(&m_piuPort1Data);
		m_piuIMF.connectPort1(&m_piuPort1Data);
		// port 2 is a bit more complicated
		m_piuPC.connectPort2(0, &m_piuPC_int1);
		m_piuPC.connectPort2(1, &m_piuGroup1DataAcknowledgement);
		m_piuPC.connectPort2(2, &m_piuGroup1DataAvailable);
		m_piuPC.connectPort2(3, &m_piuPC_int0);
		m_piuPC.connectPort2(4, &m_piuGroup0DataAcknowledgement);
		m_piuPC.connectPort2(5, &m_piuGroup0DataAvailable);
		m_piuPC.connectPort2(6, &m_piuEXR9);
		m_piuPC.connectPort2(7, &m_piuEXR8);
		m_piuIMF.connectPort2(0, &m_piuIMF_int1);
		m_piuIMF.connectPort2(1, &m_piuGroup1DataAcknowledgement);
		m_piuIMF.connectPort2(2, &m_piuGroup1DataAvailable);
		m_piuIMF.connectPort2(3, &m_piuIMF_int0);
		m_piuIMF.connectPort2(4, &m_piuEXR9);
		m_piuIMF.connectPort2(5, &m_piuEXR8);
		m_piuIMF.connectPort2(6, &m_piuGroup0DataAcknowledgement);
		m_piuIMF.connectPort2(7, &m_piuGroup0DataAvailable);

		// clang-format off
		//m_timer.setDebug(true);
		//m_irqTriggerPc.setDebug(true);

		//m_bufferToMidiOutState.setDebug(true);
		//m_bufferFromSystemState.setDebug(true);

		//m_piuPort1Data.setDebug(true);
		//m_piuPC_int0.setDebug(true);
		//m_piuPC_int1.setDebug(true);
		//m_piuIMF_int0.setDebug(true);
		//m_piuIMF_int1.setDebug(true);
		//m_piuGroup0DataAvailable.setDebug(true);
		//m_piuGroup0DataAcknowledgement.setDebug(true);
		//m_piuGroup1DataAvailable.setDebug(true);
		//m_piuGroup1DataAcknowledgement.setDebug(true);
		//m_piuEXR8.setDebug(true);
		//m_piuEXR9.setDebug(true);

		//IMF_LOG(" --- DEBUG: INIT ---");
		//m_piuPC.setGroupModes(PD71055::MODE1, PD71055::INPUT, PD71055::INPUT, PD71055::MODE1, PD71055::OUTPUT, PD71055::OUTPUT);
		//m_piuIMF.setGroupModes(PD71055::MODE1, PD71055::OUTPUT, PD71055::OUTPUT, PD71055::MODE1, PD71055::INPUT, PD71055::INPUT);
		//IMF_LOG(" --- DEBUG: PC.WRITE ---");
		//m_piuPC.writePortPIU1(0x55);
		//IMF_LOG(" --- DEBUG: IMF.READ ---");
		//m_piuIMF.readPortPIU1();
		//IMF_LOG(" --- DEBUG: IMF.WRITE ---");
		//m_piuIMF.writePortPIU0(0x55);
		//IMF_LOG(" --- DEBUG: PC.READ ---");
		//m_piuPC.readPortPIU0();
		//IMF_LOG(" --- DEBUG ---");
		// clang-format on

		m_invTimerAClear.connectInput(m_tcr.getTimerAClear());
		m_invTimerBClear.connectInput(m_tcr.getTimerBClear());

		m_df1.connectDataInput(m_tcr.getTimerAEnable());
		m_df1.connectClockInput(m_timer.getTimerA());
		m_df1.connectClearInput(m_invTimerAClear.getOutput());
		m_df2.connectDataInput(m_tcr.getTimerBEnable());
		m_df2.connectClockInput(m_timer.getTimerB());
		m_df2.connectClearInput(m_invTimerBClear.getOutput());

		m_tsr.connectTimerAStatus(m_df1.getOutput());
		m_tsr.connectTimerBStatus(m_df2.getOutput());
		m_tsr.connectTotalCardStatus(m_totalCardStatus.getOutput());

		m_totalCardStatus.connectInput1(m_df1.getOutput());
		m_totalCardStatus.connectInput2(m_df2.getOutput());
		m_totalCardStatus.connectInput3(&m_piuPC_int0);
		m_totalCardStatus.connectInput4(&m_piuPC_int1);

		m_irqMaskGate.connectInput1(m_totalCardStatus.getOutput());
		m_irqMaskGate.connectInput2(m_tcr.getTotalIrqMask());

		m_irqStatus.connectDataInput(m_irqMaskGate.getOutput());
		m_irqStatus.connectEnableInput(m_tcr.getIrqBufferEnable());

		m_irqTriggerPc.connectInterruptLine(m_irqStatus.getOutput());
		m_irqTriggerPc.enableInterrupts();
		m_irqTriggerImf.connectInterruptLine(&m_piuIMF_int0);
		m_irqTriggerImf.connectInterruptLine(&m_piuIMF_int1);
		// TODO: would also need to connect the YM and the MIDI
		m_irqTriggerImf.disableInterrupts();

		// start up all the components
		m_piuPC.reset();
		m_piuIMF.reset();

		// now start the main program
		keepRunning        = true;
		m_hardwareMutex    = thread_create_mutex();
		m_mainThread       = thread_create_named(imfMainThreadStart, this, "imfc-main");
		MFC_AddEvent(CallInterruptHandler, 0.5, 0);

		// wait until we're ready to receive data... it's a workaround
		// for now, but well....
		while (!m_finishedBootupSequence) {
			plat_delay_ms(1);
		}

		// We're read to receive data, so register the IO handlers
		RegisterIoHandlers(port);

#if 0
    // TODO
		LOG_MSG("IMFC: IBM Music Feature Card running on port %xh and IRQ %d",
		        port,
		        irq);
#endif
	}

	static void imfMainThreadStart(void* data)
	{
		((MusicFeatureCard*)data)->threadMainStart();
	}

	void threadMainStart()
	{
		log_debug("IMFC: processor main thread started");
		coldStart();
	}

	uint8_t readPortPIU0(const io_port_t, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t val = m_piuPC.readPortPIU0();
		thread_release_mutex(m_hardwareMutex);
		return val;
	}
	void writePortPIU0(const io_port_t, const io_val_t value, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		m_piuPC.writePortPIU0((uint8_t)(value));
		thread_release_mutex(m_hardwareMutex);
	}
	uint8_t readPortPIU1(const io_port_t, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t val = m_piuPC.readPortPIU1();
		thread_release_mutex(m_hardwareMutex);
		return val;
	}
	void writePortPIU1(const io_port_t, const io_val_t value, const io_width_t)
	{
		// IMF_LOG("PC->IMFC: write to PIU1 = %02X", val);
		thread_wait_mutex(m_hardwareMutex);
		m_piuPC.writePortPIU1((uint8_t)(value));
		thread_release_mutex(m_hardwareMutex);
		receiveNextValueFromSystemDuringInterruptHandler(); // moved from
		                                                    // sendOrReceiveNextValueToFromSystemDuringInterruptHandler
	}
	uint8_t readPortPIU2(const io_port_t, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t val = m_piuPC.readPortPIU2();
		thread_release_mutex(m_hardwareMutex);
		return val;
	}
	void writePortPIU2(const io_port_t, const io_val_t value, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		m_piuPC.writePortPIU2((uint8_t)(value));
		thread_release_mutex(m_hardwareMutex);
	}
	uint8_t readPortPCR(const io_port_t, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t val = PD71055::readPortPCR();
		thread_release_mutex(m_hardwareMutex);
		return val;
	}
	void writePortPCR(const io_port_t, const io_val_t value, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		m_piuPC.writePortPCR((uint8_t)(value));
		thread_release_mutex(m_hardwareMutex);
	}
	uint8_t readPortCNTR0(const io_port_t, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t val = m_timer.readPortCNTR0();
		thread_release_mutex(m_hardwareMutex);
		return val;
	}
	void writePortCNTR0(const io_port_t, const io_val_t value, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		m_timer.writePortCNTR0((uint8_t)(value));
		thread_release_mutex(m_hardwareMutex);
	}
	uint8_t readPortCNTR1(const io_port_t, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t val = m_timer.readPortCNTR1();
		thread_release_mutex(m_hardwareMutex);
		return val;
	}
	void writePortCNTR1(const io_port_t, const io_val_t value, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		m_timer.writePortCNTR1((uint8_t)(value));
		thread_release_mutex(m_hardwareMutex);
	}
	uint8_t readPortCNTR2(const io_port_t, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t val = m_timer.readPortCNTR2();
		thread_release_mutex(m_hardwareMutex);
		return val;
	}
	void writePortCNTR2(const io_port_t, const io_val_t value, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		m_timer.writePortCNTR2((uint8_t)(value));
		thread_release_mutex(m_hardwareMutex);
	}
	uint8_t readPortTCWR(const io_port_t, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t val = Intel8253::readPortTCWR();
		thread_release_mutex(m_hardwareMutex);
		return val;
	}
	void writePortTCWR(const io_port_t, const io_val_t value, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		m_timer.writePortTCWR((uint8_t)(value));
		thread_release_mutex(m_hardwareMutex);
	}
	uint8_t readPortTCR(const io_port_t, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t val = m_tcr.readPort();
		thread_release_mutex(m_hardwareMutex);
		return val;
	}
	void writePortTCR(const io_port_t, const io_val_t value, const io_width_t)
	{
		// log("DEBUG DEBUG: write to TotalControlRegister = %02X", val);
		thread_wait_mutex(m_hardwareMutex);
		m_tcr.writePort((uint8_t)(value));
		thread_release_mutex(m_hardwareMutex);
	}
	uint8_t readPortTSR(const io_port_t, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		const uint8_t val = m_tsr.readPort();
		thread_release_mutex(m_hardwareMutex);
		return val;
	}
	void writePortTSR(const io_port_t, const io_val_t value, const io_width_t)
	{
		thread_wait_mutex(m_hardwareMutex);
		m_tsr.writePort((uint8_t)(value));
		thread_release_mutex(m_hardwareMutex);
	}

	void mixerCallback(const uint16_t requested_frames, int32_t* buffer)
	{
		thread_wait_mutex(m_hardwareMutex);
		m_ya2151.sound_stream_update(requested_frames, buffer);
		thread_release_mutex(m_hardwareMutex);
	}

	void onTimerEvent(const uint32_t val)
	{
		// IMF_LOG("->Intel8253_TimerEvent");
		m_timer.timerEvent(val);
	}

        void onTimerEvent2(const uint32_t val)
        {
                // IMF_LOG("->Intel8253_TimerEvent2");
                m_timer.timerEvent2(val);
        }

        // ROM Address: 0x0C09
        void interruptHandler()
        {
                // log("IMFC: interruptHandler() - start");
                thread_wait_mutex(m_hardwareMutex);
                const uint8_t midiStatus = m_midi.readPort2();
                thread_release_mutex(m_hardwareMutex);
                if ((midiStatus & 2) != 0) {
                        readMidiInPortDuringInterruptHandler();
                } else if (((m_midiTransmitReceiveFlag & 1) != 0) &&
                           ((midiStatus & 1) == 0)) {
                        writeMidiOutPortDuringInterruptHandler();
                } else {
                        // clang-format off

                        /* FIXME -- YM-2151 interrupt handling currently missing
                        thread_wait_mutex(m_hardwareMutex);
                        // m_ya2151.register_w(0x14); // select clock
                        // manipulation/status register uint8_t yaStatus =
                        // m_ya2151.status_r();
                        const uint8_t yaStatus = 0;
                        // m_ya2151.data_w(m_ym2151_IRQ_stuff);
                        thread_release_mutex(m_hardwareMutex);
                        if ((yaStatus & 3) != 0) {
                                process_ya2151_interrupts_callInInterruptHandler(
                                        yaStatus & 3);
                        } else { */
                                sendOrReceiveNextValueToFromSystemDuringInterruptHandler();
                        // }
                        // clang-format on
                }
                enableInterrupts();
                // log("IMFC: interruptHandler() - end");
        }

	~MusicFeatureCard()
	{
		using namespace std::chrono_literals;

		//LOG_MSG("IMFC: Shutting down");

		keepRunning = false;
		MFC_RemoveEvents(CallInterruptHandler);

		// Remove access to the IO ports
        io_removehandler(host_port, 0x10,
                         imfc_read, nullptr, nullptr,
                         imfc_write, nullptr, nullptr,
                         this);

		// Give the threads a small bit of time to gracefully complete
		plat_delay_ms(20);

		thread_wait(m_mainThread);
		thread_close_mutex(m_hardwareMutex);
	}
};

static void
imfc_write(uint16_t port, uint8_t val, void *priv)
{
    MusicFeatureCard *imfc_card = (MusicFeatureCard *) priv;

    switch (port & 0xF) {
        case 0x0: /* PIU0 */
            imfc_card->writePortPIU0(port, val, io_width_t::byte);
            break;
        case 0x1: /* PIU1 */
            imfc_card->writePortPIU1(port, val, io_width_t::byte);
            break;
        case 0x2: /* PIU2 */
            imfc_card->writePortPIU2(port, val, io_width_t::byte);
            break;
        case 0x3: /* PCR */
            imfc_card->writePortPCR(port, val, io_width_t::byte);
            break;
        case 0x4: /* CNTR0 */
            imfc_card->writePortCNTR0(port, val, io_width_t::byte);
            break;
        case 0x5: /* CNTR1 */
            imfc_card->writePortCNTR1(port, val, io_width_t::byte);
            break;
        case 0x6: /* CNTR2 */
            imfc_card->writePortCNTR2(port, val, io_width_t::byte);
            break;
        case 0x7: /* TCWR */
            imfc_card->writePortTCWR(port, val, io_width_t::byte);
            break;
        case 0x8: /* TCR */
        case 0x9:
        case 0xA:
        case 0xB:
            imfc_card->writePortTCR(port, val, io_width_t::byte);
            break;
        case 0xC: /* TSR */
        case 0xD:
        case 0xE:
        case 0xF:
            imfc_card->writePortTSR(port, val, io_width_t::byte);
            break;
    }
}

static uint8_t
imfc_read(uint16_t port, void *priv)
{
    MusicFeatureCard *imfc_card = (MusicFeatureCard *) priv;
    uint8_t ret = 0xFF;

    switch (port & 0xF) {
        case 0x0: /* PIU0 */
            ret = imfc_card->readPortPIU0(port, io_width_t::byte);
            break;
        case 0x1: /* PIU1 */
            ret = imfc_card->readPortPIU1(port, io_width_t::byte);
            break;
        case 0x2: /* PIU2 */
            ret = imfc_card->readPortPIU2(port, io_width_t::byte);
            break;
        case 0x3: /* PCR */
            ret = imfc_card->readPortPCR(port, io_width_t::byte);
            break;
        case 0x4: /* CNTR0 */
            ret = imfc_card->readPortCNTR0(port, io_width_t::byte);
            break;
        case 0x5: /* CNTR1 */
            ret = imfc_card->readPortCNTR1(port, io_width_t::byte);
            break;
        case 0x6: /* CNTR2 */
            ret = imfc_card->readPortCNTR2(port, io_width_t::byte);
            break;
        case 0x7: /* TCWR */
            ret = imfc_card->readPortTCWR(port, io_width_t::byte);
            break;
        case 0x8: /* TCR */
        case 0x9:
        case 0xA:
        case 0xB:
            ret = imfc_card->readPortTCR(port, io_width_t::byte);
            break;
        case 0xC: /* TSR */
        case 0xD:
        case 0xE:
        case 0xF:
            ret = imfc_card->readPortTSR(port, io_width_t::byte);
            break;
    }
    return ret;
}

void MusicFeatureCard::RegisterIoHandlers(const io_port_t port)
{
    io_sethandler(port, 0x10,
                 imfc_read, nullptr, nullptr,
                 imfc_write, nullptr, nullptr,
                 this);
    host_port = port;
}

///////////////////////////////////////////////////////////
/// DOSBOX stuff
///////////////////////////////////////////////////////////

static std::unique_ptr<MusicFeatureCard> imfc = {};

static void Intel8253_TimerEvent(const uint32_t val)
{
	imfc->onTimerEvent(val);
}

static void Intel8253_TimerEvent2(const uint32_t val)
{
        imfc->onTimerEvent2(val);
}

static void CallInterruptHandler()
{
	imfc->interruptHandler();
}

static void
imfc_get_buffer(int32_t *buffer, int len, void *priv)
{
    MusicFeatureCard *imfc_card = (MusicFeatureCard *) priv;

    imfc_card->mixerCallback(len, buffer);;
}

//TODO
#if IMFC_VERBOSE_LOGGING
	m_loggerMutex = thread_create_mutex();
#endif

//TODO
#if IMFC_VERBOSE_LOGGING
	assert(m_loggerMutex);
	thread_close_mutex(m_loggerMutex);
	m_loggerMutex = nullptr;
#endif

static void
imfc_input_msg(void *priv, uint8_t *msg, uint32_t len)
{
    if (midiin_sysex)
        return;
    for (uint32_t i = 0; i < len; i++) {
        midiin_queue[midiin_w++] = msg[i];
        midiin_w &= 0xff;
    }
}

static int
imfc_input_sysex(void *priv, uint8_t *buffer, uint32_t len, int abort)
{
    if (abort) {
        midiin_sysex = 0;
        return 0;
    }
    midiin_sysex = 1;
    for (uint32_t i = 0; i < len; i++) {
        if (midiin_r == midiin_w)
            return (int) (len -i);
        midiin_queue[midiin_w++] = buffer[i];
        midiin_w &= 0xff;
    }
    midiin_sysex = 0;
    return 0;
}

void*
imfc_card_init(const device_t *info)
{
    imfc = std::make_unique<MusicFeatureCard>(device_get_config_hex16("base"), device_get_config_int("irq"));
    wavetable_add_handler(imfc_get_buffer, imfc.get());
    if (device_get_config_int("receive_input"))
        midi_in_handler(1, imfc_input_msg, imfc_input_sysex, imfc.get());

    return imfc.get();
}

void
imfc_card_close(void *priv)
{
    imfc.reset(nullptr);
}

static const device_config_t imfc_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x2A20,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x2A20", .value = 0x2A20 },
            { .description = "0x2A30", .value = 0x2A30 },
            { .description = "" }
        },
        .bios           = { { 0 } }

    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 2", .value = 2 },
            { .description = "IRQ 3", .value = 3 },
            { .description = "IRQ 4", .value = 4 },
            { .description = "IRQ 5", .value = 5 },
            { .description = "IRQ 6", .value = 6 },
            { .description = "IRQ 7", .value = 7 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
	{ .name = "", .description = "", .type = CONFIG_END }
};

const device_t imfc_device = {
    .name          = "IBM Music Feature Card",
    .internal_name = "imfc",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = imfc_card_init,
    .close         = imfc_card_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = imfc_config
};
