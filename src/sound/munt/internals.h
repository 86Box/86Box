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

#ifndef MT32EMU_INTERNALS_H
#define MT32EMU_INTERNALS_H

#include "Types.h"

// Debugging

// 0: Standard debug output is not stamped with the rendered sample count
// 1: Standard debug output is stamped with the rendered sample count
// NOTE: The "samplestamp" corresponds to the end of the last completed rendering run.
//       This is important to bear in mind for debug output that occurs during a run.
#ifndef MT32EMU_DEBUG_SAMPLESTAMPS
#define MT32EMU_DEBUG_SAMPLESTAMPS 0
#endif

// 0: No debug output for initialisation progress
// 1: Debug output for initialisation progress
#ifndef MT32EMU_MONITOR_INIT
#define MT32EMU_MONITOR_INIT 0
#endif

// 0: No debug output for MIDI events
// 1: Debug output for weird MIDI events
#ifndef MT32EMU_MONITOR_MIDI
#define MT32EMU_MONITOR_MIDI 0
#endif

// 0: No debug output for note on/off
// 1: Basic debug output for note on/off
// 2: Comprehensive debug output for note on/off
#ifndef MT32EMU_MONITOR_INSTRUMENTS
#define MT32EMU_MONITOR_INSTRUMENTS 0
#endif

// 0: No debug output for partial allocations
// 1: Show partial stats when an allocation fails
// 2: Show partial stats with every new poly
// 3: Show individual partial allocations/deactivations
#ifndef MT32EMU_MONITOR_PARTIALS
#define MT32EMU_MONITOR_PARTIALS 0
#endif

// 0: No debug output for sysex
// 1: Basic debug output for sysex
#ifndef MT32EMU_MONITOR_SYSEX
#define MT32EMU_MONITOR_SYSEX 0
#endif

// 0: No debug output for sysex writes to the timbre areas
// 1: Debug output with the name and location of newly-written timbres
// 2: Complete dump of timbre parameters for newly-written timbres
#ifndef MT32EMU_MONITOR_TIMBRES
#define MT32EMU_MONITOR_TIMBRES 0
#endif

// 0: No TVA/TVF-related debug output.
// 1: Shows changes to TVA/TVF target, increment and phase.
#ifndef MT32EMU_MONITOR_TVA
#define MT32EMU_MONITOR_TVA 0
#endif
#ifndef MT32EMU_MONITOR_TVF
#define MT32EMU_MONITOR_TVF 0
#endif

// Configuration

// 0: Maximum speed at the cost of a bit lower emulation accuracy.
// 1: Maximum achievable emulation accuracy.
#ifndef MT32EMU_BOSS_REVERB_PRECISE_MODE
#define MT32EMU_BOSS_REVERB_PRECISE_MODE 0
#endif

namespace MT32Emu {

typedef Bit16s IntSample;
typedef Bit32s IntSampleEx;
typedef float FloatSample;

enum PolyState {
	POLY_Playing,
	POLY_Held, // This marks keys that have been released on the keyboard, but are being held by the pedal
	POLY_Releasing,
	POLY_Inactive
};

enum ReverbMode {
	REVERB_MODE_ROOM,
	REVERB_MODE_HALL,
	REVERB_MODE_PLATE,
	REVERB_MODE_TAP_DELAY
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_INTERNALS_H
