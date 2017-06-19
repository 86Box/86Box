/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2017 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
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

#ifndef MT32EMU_MEMORY_REGION_H
#define MT32EMU_MEMORY_REGION_H

#include <cstddef>

#include "globals.h"
#include "Types.h"
#include "Structures.h"

namespace MT32Emu {

enum MemoryRegionType {
	MR_PatchTemp, MR_RhythmTemp, MR_TimbreTemp, MR_Patches, MR_Timbres, MR_System, MR_Display, MR_Reset
};

class Synth;

class MemoryRegion {
private:
	Synth *synth;
	Bit8u *realMemory;
	Bit8u *maxTable;
public:
	MemoryRegionType type;
	Bit32u startAddr, entrySize, entries;

	MemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable, MemoryRegionType useType, Bit32u useStartAddr, Bit32u useEntrySize, Bit32u useEntries) {
		synth = useSynth;
		realMemory = useRealMemory;
		maxTable = useMaxTable;
		type = useType;
		startAddr = useStartAddr;
		entrySize = useEntrySize;
		entries = useEntries;
	}
	int lastTouched(Bit32u addr, Bit32u len) const {
		return (offset(addr) + len - 1) / entrySize;
	}
	int firstTouchedOffset(Bit32u addr) const {
		return offset(addr) % entrySize;
	}
	int firstTouched(Bit32u addr) const {
		return offset(addr) / entrySize;
	}
	Bit32u regionEnd() const {
		return startAddr + entrySize * entries;
	}
	bool contains(Bit32u addr) const {
		return addr >= startAddr && addr < regionEnd();
	}
	int offset(Bit32u addr) const {
		return addr - startAddr;
	}
	Bit32u getClampedLen(Bit32u addr, Bit32u len) const {
		if (addr + len > regionEnd())
			return regionEnd() - addr;
		return len;
	}
	Bit32u next(Bit32u addr, Bit32u len) const {
		if (addr + len > regionEnd()) {
			return regionEnd() - addr;
		}
		return 0;
	}
	Bit8u getMaxValue(int off) const {
		if (maxTable == NULL)
			return 0xFF;
		return maxTable[off % entrySize];
	}
	Bit8u *getRealMemory() const {
		return realMemory;
	}
	bool isReadable() const {
		return getRealMemory() != NULL;
	}
	void read(unsigned int entry, unsigned int off, Bit8u *dst, unsigned int len) const;
	void write(unsigned int entry, unsigned int off, const Bit8u *src, unsigned int len, bool init = false) const;
}; // class MemoryRegion

class PatchTempMemoryRegion : public MemoryRegion {
public:
	PatchTempMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_PatchTemp, MT32EMU_MEMADDR(0x030000), sizeof(MemParams::PatchTemp), 9) {}
};
class RhythmTempMemoryRegion : public MemoryRegion {
public:
	RhythmTempMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_RhythmTemp, MT32EMU_MEMADDR(0x030110), sizeof(MemParams::RhythmTemp), 85) {}
};
class TimbreTempMemoryRegion : public MemoryRegion {
public:
	TimbreTempMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_TimbreTemp, MT32EMU_MEMADDR(0x040000), sizeof(TimbreParam), 8) {}
};
class PatchesMemoryRegion : public MemoryRegion {
public:
	PatchesMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_Patches, MT32EMU_MEMADDR(0x050000), sizeof(PatchParam), 128) {}
};
class TimbresMemoryRegion : public MemoryRegion {
public:
	TimbresMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_Timbres, MT32EMU_MEMADDR(0x080000), sizeof(MemParams::PaddedTimbre), 64 + 64 + 64 + 64) {}
};
class SystemMemoryRegion : public MemoryRegion {
public:
	SystemMemoryRegion(Synth *useSynth, Bit8u *useRealMemory, Bit8u *useMaxTable) : MemoryRegion(useSynth, useRealMemory, useMaxTable, MR_System, MT32EMU_MEMADDR(0x100000), sizeof(MemParams::System), 1) {}
};
class DisplayMemoryRegion : public MemoryRegion {
public:
	DisplayMemoryRegion(Synth *useSynth) : MemoryRegion(useSynth, NULL, NULL, MR_Display, MT32EMU_MEMADDR(0x200000), SYSEX_BUFFER_SIZE - 1, 1) {}
};
class ResetMemoryRegion : public MemoryRegion {
public:
	ResetMemoryRegion(Synth *useSynth) : MemoryRegion(useSynth, NULL, NULL, MR_Reset, MT32EMU_MEMADDR(0x7F0000), 0x3FFF, 1) {}
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_MEMORY_REGION_H
