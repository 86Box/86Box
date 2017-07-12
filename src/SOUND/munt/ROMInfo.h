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

#ifndef MT32EMU_ROMINFO_H
#define MT32EMU_ROMINFO_H

#include <cstddef>

#include "globals.h"
#include "File.h"

namespace MT32Emu {

// Defines vital info about ROM file to be used by synth and applications

struct ROMInfo {
public:
	size_t fileSize;
	const File::SHA1Digest &sha1Digest;
	enum Type {PCM, Control, Reverb} type;
	const char *shortName;
	const char *description;
	enum PairType {Full, FirstHalf, SecondHalf, Mux0, Mux1} pairType;
	ROMInfo *pairROMInfo;

	// Returns a ROMInfo struct by inspecting the size and the SHA1 hash
	MT32EMU_EXPORT static const ROMInfo* getROMInfo(File *file);

	// Currently no-op
	MT32EMU_EXPORT static void freeROMInfo(const ROMInfo *romInfo);

	// Allows retrieving a NULL-terminated list of ROMInfos for a range of types and pairTypes
	// (specified by bitmasks)
	// Useful for GUI/console app to output information on what ROMs it supports
	MT32EMU_EXPORT static const ROMInfo** getROMInfoList(Bit32u types, Bit32u pairTypes);

	// Frees the list of ROMInfos given
	MT32EMU_EXPORT static void freeROMInfoList(const ROMInfo **romInfos);
};

// Synth::open() is to require a full control ROMImage and a full PCM ROMImage to work

class ROMImage {
private:
	File * const file;
	const ROMInfo * const romInfo;

	ROMImage(File *file);
	~ROMImage();

public:
	// Creates a ROMImage object given a ROMInfo and a File. Keeps a reference
	// to the File and ROMInfo given, which must be freed separately by the user
	// after the ROMImage is freed
	MT32EMU_EXPORT static const ROMImage* makeROMImage(File *file);

	// Must only be done after all Synths using the ROMImage are deleted
	MT32EMU_EXPORT static void freeROMImage(const ROMImage *romImage);

	MT32EMU_EXPORT File *getFile() const;
	MT32EMU_EXPORT const ROMInfo *getROMInfo() const;
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_ROMINFO_H
