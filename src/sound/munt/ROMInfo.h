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
	enum PairType {
		// Complete ROM image ready to use with Synth.
		Full,
		// ROM image contains data that occupies lower addresses. Needs pairing before use.
		FirstHalf,
		// ROM image contains data that occupies higher addresses. Needs pairing before use.
		SecondHalf,
		// ROM image contains data that occupies even addresses. Needs pairing before use.
		Mux0,
		// ROM image contains data that occupies odd addresses. Needs pairing before use.
		Mux1
	} pairType;
	// NULL for Full images or a pointer to the corresponding other image for pairing.
	const ROMInfo *pairROMInfo;

	// Returns a ROMInfo struct by inspecting the size and the SHA1 hash of the file
	// among all the known ROMInfos.
	MT32EMU_EXPORT static const ROMInfo *getROMInfo(File *file);

	// Returns a ROMInfo struct by inspecting the size and the SHA1 hash of the file
	// among the ROMInfos listed in the NULL-terminated list romInfos.
	MT32EMU_EXPORT_V(2.5) static const ROMInfo *getROMInfo(File *file, const ROMInfo * const *romInfos);

	// Currently no-op
	MT32EMU_EXPORT static void freeROMInfo(const ROMInfo *romInfo);

	// Allows retrieving a NULL-terminated list of ROMInfos for a range of types and pairTypes
	// (specified by bitmasks)
	// Useful for GUI/console app to output information on what ROMs it supports
	// The caller must free the returned list with freeROMInfoList when finished.
	MT32EMU_EXPORT static const ROMInfo **getROMInfoList(Bit32u types, Bit32u pairTypes);

	// Frees the list of ROMInfos given that has been created by getROMInfoList.
	MT32EMU_EXPORT static void freeROMInfoList(const ROMInfo **romInfos);

	// Returns an immutable NULL-terminated list of all (full and partial) supported ROMInfos.
	// For convenience, this method also can fill the number of non-NULL items present in the list
	// if a non-NULL value is provided in optional argument itemCount.
	MT32EMU_EXPORT_V(2.5) static const ROMInfo * const *getAllROMInfos(Bit32u *itemCount = NULL);
	// Returns an immutable NULL-terminated list of all supported full ROMInfos.
	// For convenience, this method also can fill the number of non-NULL items present in the list
	// if a non-NULL value is provided in optional argument itemCount.
	MT32EMU_EXPORT_V(2.5) static const ROMInfo * const *getFullROMInfos(Bit32u *itemCount = NULL);
	// Returns an immutable NULL-terminated list of all supported partial ROMInfos.
	// For convenience, this method also can fill the number of non-NULL items present in the list
	// if a non-NULL value is provided in optional argument itemCount.
	MT32EMU_EXPORT_V(2.5) static const ROMInfo * const *getPartialROMInfos(Bit32u *itemCount = NULL);
};

// Synth::open() requires a full control ROMImage and a compatible full PCM ROMImage to work

class ROMImage {
public:
	// Creates a ROMImage object given a ROMInfo and a File. Keeps a reference
	// to the File and ROMInfo given, which must be freed separately by the user
	// after the ROMImage is freed.
	// CAVEAT: This method always prefers full ROM images over partial ones.
	// Because the lower half of CM-32L/CM-64/LAPC-I PCM ROM is essentially the full
	// MT-32 PCM ROM, it is therefore aliased. In this case a partial image can only be
	// created by the overridden method makeROMImage(File *, const ROMInfo * const *).
	MT32EMU_EXPORT static const ROMImage *makeROMImage(File *file);

	// Same as the method above but only permits creation of a ROMImage if the file content
	// matches one of the ROMs described in a NULL-terminated list romInfos. This list can be
	// created using e.g. method ROMInfo::getROMInfoList.
	MT32EMU_EXPORT_V(2.5) static const ROMImage *makeROMImage(File *file, const ROMInfo * const *romInfos);

	// Creates a ROMImage object given a couple of files that contain compatible partial ROM images.
	// The files aren't referenced by the resulting ROMImage and may be freed anytime afterwards.
	// The file in the resulting image will be automatically freed along with the ROMImage.
	// If the given files contain incompatible partial images, NULL is returned.
	MT32EMU_EXPORT_V(2.5) static const ROMImage *makeROMImage(File *file1, File *file2);

	// Must only be done after all Synths using the ROMImage are deleted
	MT32EMU_EXPORT static void freeROMImage(const ROMImage *romImage);

	// Checks whether the given ROMImages are pairable and merges them into a full image, if possible.
	// If the check fails, NULL is returned.
	MT32EMU_EXPORT_V(2.5) static const ROMImage *mergeROMImages(const ROMImage *romImage1, const ROMImage *romImage2);

	MT32EMU_EXPORT File *getFile() const;

	// Returns true in case this ROMImage is built with a user provided File that has to be deallocated separately.
	// For a ROMImage created via merging two partial ROMImages, this method returns false.
	MT32EMU_EXPORT_V(2.5) bool isFileUserProvided() const;
	MT32EMU_EXPORT const ROMInfo *getROMInfo() const;

private:
	static const ROMImage *makeFullROMImage(Bit8u *data, size_t dataSize);
	static const ROMImage *appendImages(const ROMImage *romImageLow, const ROMImage *romImageHigh);
	static const ROMImage *interleaveImages(const ROMImage *romImageEven, const ROMImage *romImageOdd);

	File * const file;
	const bool ownFile;
	const ROMInfo * const romInfo;

	ROMImage(File *file, bool ownFile, const ROMInfo * const *romInfos);
	~ROMImage();

	// Make ROMIMage an identity class.
	ROMImage(const ROMImage &);
	ROMImage &operator=(const ROMImage &);
};

class MachineConfiguration {
public:
	// Returns an immutable NULL-terminated list of all supported machine configurations.
	// For convenience, this method also can fill the number of non-NULL items present in the list
	// if a non-NULL value is provided in optional argument itemCount.
	MT32EMU_EXPORT_V(2.5) static const MachineConfiguration * const *getAllMachineConfigurations(Bit32u *itemCount = NULL);

	// Returns a string identifier of this MachineConfiguration.
	MT32EMU_EXPORT_V(2.5) const char *getMachineID() const;

	// Returns an immutable NULL-terminated list of ROMInfos that are compatible with this
	// MachineConfiguration. That means the respective ROMImages can be successfully used together
	// by the emulation engine. Calling ROMInfo::getROMInfo or ROMImage::makeROMImage with this list
	// supplied enables identification of all files containing desired ROM images while filtering out
	// any incompatible ones.
	// For convenience, this method also can fill the number of non-NULL items present in the list
	// if a non-NULL value is provided in optional argument itemCount.
	MT32EMU_EXPORT_V(2.5) const ROMInfo * const *getCompatibleROMInfos(Bit32u *itemCount = NULL) const;

private:
	const char * const machineID;
	const ROMInfo * const * const romInfos;
	const Bit32u romInfosCount;

	MachineConfiguration(const char *machineID, const ROMInfo * const *romInfos, Bit32u romInfosCount);

	// Make MachineConfiguration an identity class.
	MachineConfiguration(const MachineConfiguration &);
	~MachineConfiguration() {}
	MachineConfiguration &operator=(const MachineConfiguration &);
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_ROMINFO_H
