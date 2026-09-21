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

#include "internals.h"

#include "File.h"
#include "ROMInfo.h"

namespace MT32Emu {

namespace {

struct ROMInfoList {
	const ROMInfo * const *romInfos;
	const Bit32u itemCount;
};

struct ROMInfoLists {
	ROMInfoList mt32_1_04;
	ROMInfoList mt32_1_05;
	ROMInfoList mt32_1_06;
	ROMInfoList mt32_1_07;
	ROMInfoList mt32_bluer;
	ROMInfoList mt32_2_03;
	ROMInfoList mt32_2_04;
	ROMInfoList mt32_2_06;
	ROMInfoList mt32_2_07;
	ROMInfoList cm32l_1_00;
	ROMInfoList cm32l_1_02;
	ROMInfoList cm32ln_1_00;
	ROMInfoList fullROMInfos;
	ROMInfoList partialROMInfos;
	ROMInfoList allROMInfos;
};

}

#define _CALC_ARRAY_LENGTH(x) Bit32u(sizeof (x) / sizeof *(x) - 1)

static const ROMInfoLists &getROMInfoLists() {
	static ROMInfo CTRL_MT32_V1_04_A = {32768, "9cd4858014c4e8a9dff96053f784bfaac1092a2e", ROMInfo::Control, "ctrl_mt32_1_04_a", "MT-32 Control v1.04", ROMInfo::Mux0, NULL};
	static ROMInfo CTRL_MT32_V1_04_B = {32768, "fe8db469b5bfeb37edb269fd47e3ce6d91014652", ROMInfo::Control, "ctrl_mt32_1_04_b", "MT-32 Control v1.04", ROMInfo::Mux1, &CTRL_MT32_V1_04_A};
	static ROMInfo CTRL_MT32_V1_04 = {65536, "5a5cb5a77d7d55ee69657c2f870416daed52dea7", ROMInfo::Control, "ctrl_mt32_1_04", "MT-32 Control v1.04", ROMInfo::Full, NULL};
	static ROMInfo CTRL_MT32_V1_05_A = {32768, "57a09d80d2f7ca5b9734edbe9645e6e700f83701", ROMInfo::Control, "ctrl_mt32_1_05_a", "MT-32 Control v1.05", ROMInfo::Mux0, NULL};
	static ROMInfo CTRL_MT32_V1_05_B = {32768, "52e3c6666db9ef962591a8ee99be0cde17f3a6b6", ROMInfo::Control, "ctrl_mt32_1_05_b", "MT-32 Control v1.05", ROMInfo::Mux1, &CTRL_MT32_V1_05_A};
	static ROMInfo CTRL_MT32_V1_05 = {65536, "e17a3a6d265bf1fa150312061134293d2b58288c", ROMInfo::Control, "ctrl_mt32_1_05", "MT-32 Control v1.05", ROMInfo::Full, NULL};
	static ROMInfo CTRL_MT32_V1_06_A = {32768, "cc83bf23cee533097fb4c7e2c116e43b50ebacc8", ROMInfo::Control, "ctrl_mt32_1_06_a", "MT-32 Control v1.06", ROMInfo::Mux0, NULL};
	static ROMInfo CTRL_MT32_V1_06_B = {32768, "bf4f15666bc46679579498386704893b630c1171", ROMInfo::Control, "ctrl_mt32_1_06_b", "MT-32 Control v1.06", ROMInfo::Mux1, &CTRL_MT32_V1_06_A};
	static ROMInfo CTRL_MT32_V1_06 = {65536, "a553481f4e2794c10cfe597fef154eef0d8257de", ROMInfo::Control, "ctrl_mt32_1_06", "MT-32 Control v1.06", ROMInfo::Full, NULL};
	static ROMInfo CTRL_MT32_V1_07_A = {32768, "13f06b38f0d9e0fc050b6503ab777bb938603260", ROMInfo::Control, "ctrl_mt32_1_07_a", "MT-32 Control v1.07", ROMInfo::Mux0, NULL};
	static ROMInfo CTRL_MT32_V1_07_B = {32768, "c55e165487d71fa88bd8c5e9c083bc456c1a89aa", ROMInfo::Control, "ctrl_mt32_1_07_b", "MT-32 Control v1.07", ROMInfo::Mux1, &CTRL_MT32_V1_07_A};
	static ROMInfo CTRL_MT32_V1_07 = {65536, "b083518fffb7f66b03c23b7eb4f868e62dc5a987", ROMInfo::Control, "ctrl_mt32_1_07", "MT-32 Control v1.07", ROMInfo::Full, NULL};
	static ROMInfo CTRL_MT32_BLUER_A = {32768, "11a6ae5d8b6ee328b371af7f1e40b82125aa6b4d", ROMInfo::Control, "ctrl_mt32_bluer_a", "MT-32 Control BlueRidge", ROMInfo::Mux0, NULL};
	static ROMInfo CTRL_MT32_BLUER_B = {32768, "e0934320d7cbb5edfaa29e0d01ae835ef620085b", ROMInfo::Control, "ctrl_mt32_bluer_b", "MT-32 Control BlueRidge", ROMInfo::Mux1, &CTRL_MT32_BLUER_A};
	static ROMInfo CTRL_MT32_BLUER = {65536, "7b8c2a5ddb42fd0732e2f22b3340dcf5360edf92", ROMInfo::Control, "ctrl_mt32_bluer", "MT-32 Control BlueRidge", ROMInfo::Full, NULL};

	static const ROMInfo CTRL_MT32_V2_03 = {131072, "5837064c9df4741a55f7c4d8787ac158dff2d3ce", ROMInfo::Control, "ctrl_mt32_2_03", "MT-32 Control v2.03", ROMInfo::Full, NULL};
	static const ROMInfo CTRL_MT32_V2_04 = {131072, "2c16432b6c73dd2a3947cba950a0f4c19d6180eb", ROMInfo::Control, "ctrl_mt32_2_04", "MT-32 Control v2.04", ROMInfo::Full, NULL};
	static const ROMInfo CTRL_MT32_V2_06 = {131072, "2869cf4c235d671668cfcb62415e2ce8323ad4ed", ROMInfo::Control, "ctrl_mt32_2_06", "MT-32 Control v2.06", ROMInfo::Full, NULL};
	static const ROMInfo CTRL_MT32_V2_07 = {131072, "47b52adefedaec475c925e54340e37673c11707c", ROMInfo::Control, "ctrl_mt32_2_07", "MT-32 Control v2.07", ROMInfo::Full, NULL};
	static const ROMInfo CTRL_CM32L_V1_00 = {65536, "73683d585cd6948cc19547942ca0e14a0319456d", ROMInfo::Control, "ctrl_cm32l_1_00", "CM-32L/LAPC-I Control v1.00", ROMInfo::Full, NULL};
	static const ROMInfo CTRL_CM32L_V1_02 = {65536, "a439fbb390da38cada95a7cbb1d6ca199cd66ef8", ROMInfo::Control, "ctrl_cm32l_1_02", "CM-32L/LAPC-I Control v1.02", ROMInfo::Full, NULL};
	static const ROMInfo CTRL_CM32LN_V1_00 = {65536, "dc1c5b1b90a4646d00f7daf3679733c7badc7077", ROMInfo::Control, "ctrl_cm32ln_1_00", "CM-32LN/CM-500/LAPC-N Control v1.00", ROMInfo::Full, NULL};

	static ROMInfo PCM_MT32_L = {262144, "3a1e19b0cd4036623fd1d1d11f5f25995585962b", ROMInfo::PCM, "pcm_mt32_l", "MT-32 PCM ROM", ROMInfo::FirstHalf, NULL};
	static ROMInfo PCM_MT32_H = {262144, "2cadb99d21a6a4a6f5b61b6218d16e9b43f61d01", ROMInfo::PCM, "pcm_mt32_h", "MT-32 PCM ROM", ROMInfo::SecondHalf, &PCM_MT32_L};
	static ROMInfo PCM_MT32 = {524288, "f6b1eebc4b2d200ec6d3d21d51325d5b48c60252", ROMInfo::PCM, "pcm_mt32", "MT-32 PCM ROM", ROMInfo::Full, NULL};
	// Alias of PCM_MT32 ROM, only useful for pairing with PCM_CM32L_H.
	static ROMInfo PCM_CM32L_L = {524288, "f6b1eebc4b2d200ec6d3d21d51325d5b48c60252", ROMInfo::PCM, "pcm_cm32l_l", "CM-32L/CM-64/LAPC-I PCM ROM", ROMInfo::FirstHalf, NULL};
	static ROMInfo PCM_CM32L_H = {524288, "3ad889fde5db5b6437cbc2eb6e305312fec3df93", ROMInfo::PCM, "pcm_cm32l_h", "CM-32L/CM-64/LAPC-I PCM ROM", ROMInfo::SecondHalf, &PCM_CM32L_L};
	static ROMInfo PCM_CM32L = {1048576, "289cc298ad532b702461bfc738009d9ebe8025ea", ROMInfo::PCM, "pcm_cm32l", "CM-32L/CM-64/LAPC-I PCM ROM", ROMInfo::Full, NULL};

	static const ROMInfo * const FULL_ROM_INFOS[] = {
		&CTRL_MT32_V1_04,
		&CTRL_MT32_V1_05,
		&CTRL_MT32_V1_06,
		&CTRL_MT32_V1_07,
		&CTRL_MT32_BLUER,
		&CTRL_MT32_V2_03,
		&CTRL_MT32_V2_04,
		&CTRL_MT32_V2_06,
		&CTRL_MT32_V2_07,
		&CTRL_CM32L_V1_00,
		&CTRL_CM32L_V1_02,
		&CTRL_CM32LN_V1_00,
		&PCM_MT32,
		&PCM_CM32L,
		NULL
	};
	static const ROMInfo * const PARTIAL_ROM_INFOS[] = {
		&CTRL_MT32_V1_04_A, &CTRL_MT32_V1_04_B,
		&CTRL_MT32_V1_05_A, &CTRL_MT32_V1_05_B,
		&CTRL_MT32_V1_06_A, &CTRL_MT32_V1_06_B,
		&CTRL_MT32_V1_07_A, &CTRL_MT32_V1_07_B,
		&CTRL_MT32_BLUER_A, &CTRL_MT32_BLUER_B,
		&PCM_MT32_L, &PCM_MT32_H,
		&PCM_CM32L_L, &PCM_CM32L_H,
		NULL
	};
	static const ROMInfo *ALL_ROM_INFOS[_CALC_ARRAY_LENGTH(FULL_ROM_INFOS) + _CALC_ARRAY_LENGTH(PARTIAL_ROM_INFOS) + 1];

	if (CTRL_MT32_V1_04_A.pairROMInfo == NULL) {
		CTRL_MT32_V1_04_A.pairROMInfo = &CTRL_MT32_V1_04_B;
		CTRL_MT32_V1_05_A.pairROMInfo = &CTRL_MT32_V1_05_B;
		CTRL_MT32_V1_06_A.pairROMInfo = &CTRL_MT32_V1_06_B;
		CTRL_MT32_V1_07_A.pairROMInfo = &CTRL_MT32_V1_07_B;
		CTRL_MT32_BLUER_A.pairROMInfo = &CTRL_MT32_BLUER_B;
		PCM_MT32_L.pairROMInfo = &PCM_MT32_H;
		PCM_CM32L_L.pairROMInfo = &PCM_CM32L_H;

		memcpy(&ALL_ROM_INFOS[0], FULL_ROM_INFOS, sizeof FULL_ROM_INFOS);
		memcpy(&ALL_ROM_INFOS[_CALC_ARRAY_LENGTH(FULL_ROM_INFOS)], PARTIAL_ROM_INFOS, sizeof PARTIAL_ROM_INFOS); // Includes NULL terminator.
	}

	static const ROMInfo * const MT32_V1_04_ROMS[] = {&CTRL_MT32_V1_04, &PCM_MT32, &CTRL_MT32_V1_04_A, &CTRL_MT32_V1_04_B, &PCM_MT32_L, &PCM_MT32_H, NULL};
	static const ROMInfo * const MT32_V1_05_ROMS[] = {&CTRL_MT32_V1_05, &PCM_MT32, &CTRL_MT32_V1_05_A, &CTRL_MT32_V1_05_B, &PCM_MT32_L, &PCM_MT32_H, NULL};
	static const ROMInfo * const MT32_V1_06_ROMS[] = {&CTRL_MT32_V1_06, &PCM_MT32, &CTRL_MT32_V1_06_A, &CTRL_MT32_V1_06_B, &PCM_MT32_L, &PCM_MT32_H, NULL};
	static const ROMInfo * const MT32_V1_07_ROMS[] = {&CTRL_MT32_V1_07, &PCM_MT32, &CTRL_MT32_V1_07_A, &CTRL_MT32_V1_07_B, &PCM_MT32_L, &PCM_MT32_H, NULL};
	static const ROMInfo * const MT32_BLUER_ROMS[] = {&CTRL_MT32_BLUER, &PCM_MT32, &CTRL_MT32_BLUER_A, &CTRL_MT32_BLUER_B, &PCM_MT32_L, &PCM_MT32_H, NULL};
	static const ROMInfo * const MT32_V2_03_ROMS[] = {&CTRL_MT32_V2_03, &PCM_MT32, &PCM_MT32_L, &PCM_MT32_H, NULL};
	static const ROMInfo * const MT32_V2_04_ROMS[] = {&CTRL_MT32_V2_04, &PCM_MT32, &PCM_MT32_L, &PCM_MT32_H, NULL};
	static const ROMInfo * const MT32_V2_06_ROMS[] = {&CTRL_MT32_V2_06, &PCM_MT32, &PCM_MT32_L, &PCM_MT32_H, NULL};
	static const ROMInfo * const MT32_V2_07_ROMS[] = {&CTRL_MT32_V2_07, &PCM_MT32, &PCM_MT32_L, &PCM_MT32_H, NULL};
	static const ROMInfo * const CM32L_V1_00_ROMS[] = {&CTRL_CM32L_V1_00, &PCM_CM32L, &PCM_CM32L_L, &PCM_CM32L_H, NULL};
	static const ROMInfo * const CM32L_V1_02_ROMS[] = {&CTRL_CM32L_V1_02, &PCM_CM32L, &PCM_CM32L_L, &PCM_CM32L_H, NULL};
	static const ROMInfo * const CM32LN_V1_00_ROMS[] = {&CTRL_CM32LN_V1_00, &PCM_CM32L, NULL};

	static const ROMInfoLists romInfoLists = {
		{MT32_V1_04_ROMS, _CALC_ARRAY_LENGTH(MT32_V1_04_ROMS)},
		{MT32_V1_05_ROMS, _CALC_ARRAY_LENGTH(MT32_V1_05_ROMS)},
		{MT32_V1_06_ROMS, _CALC_ARRAY_LENGTH(MT32_V1_06_ROMS)},
		{MT32_V1_07_ROMS, _CALC_ARRAY_LENGTH(MT32_V1_07_ROMS)},
		{MT32_BLUER_ROMS, _CALC_ARRAY_LENGTH(MT32_BLUER_ROMS)},
		{MT32_V2_03_ROMS, _CALC_ARRAY_LENGTH(MT32_V2_03_ROMS)},
		{MT32_V2_04_ROMS, _CALC_ARRAY_LENGTH(MT32_V2_04_ROMS)},
		{MT32_V2_06_ROMS, _CALC_ARRAY_LENGTH(MT32_V2_06_ROMS)},
		{MT32_V2_07_ROMS, _CALC_ARRAY_LENGTH(MT32_V2_07_ROMS)},
		{CM32L_V1_00_ROMS, _CALC_ARRAY_LENGTH(CM32L_V1_00_ROMS)},
		{CM32L_V1_02_ROMS, _CALC_ARRAY_LENGTH(CM32L_V1_02_ROMS)},
		{CM32LN_V1_00_ROMS, _CALC_ARRAY_LENGTH(CM32LN_V1_00_ROMS)},
		{FULL_ROM_INFOS, _CALC_ARRAY_LENGTH(FULL_ROM_INFOS)},
		{PARTIAL_ROM_INFOS, _CALC_ARRAY_LENGTH(PARTIAL_ROM_INFOS)},
		{ALL_ROM_INFOS, _CALC_ARRAY_LENGTH(ALL_ROM_INFOS)}
	};
	return romInfoLists;
}

static const ROMInfo * const *getKnownROMInfoList() {
	return getROMInfoLists().allROMInfos.romInfos;
}

static const ROMInfo *getKnownROMInfoFromList(Bit32u index) {
	return getKnownROMInfoList()[index];
}

const ROMInfo *ROMInfo::getROMInfo(File *file) {
	return getROMInfo(file, getKnownROMInfoList());
}

const ROMInfo *ROMInfo::getROMInfo(File *file, const ROMInfo * const *romInfos) {
	size_t fileSize = file->getSize();
	for (Bit32u i = 0; romInfos[i] != NULL; i++) {
		const ROMInfo *romInfo = romInfos[i];
		if (fileSize == romInfo->fileSize && !strcmp(file->getSHA1(), romInfo->sha1Digest)) {
			return romInfo;
		}
	}
	return NULL;
}

void ROMInfo::freeROMInfo(const ROMInfo *romInfo) {
	(void) romInfo;
}

const ROMInfo **ROMInfo::getROMInfoList(Bit32u types, Bit32u pairTypes) {
	Bit32u romCount = getROMInfoLists().allROMInfos.itemCount; // Excludes the NULL terminator.
	const ROMInfo **romInfoList = new const ROMInfo*[romCount + 1];
	const ROMInfo **currentROMInList = romInfoList;
	for (Bit32u i = 0; i < romCount; i++) {
		const ROMInfo *romInfo = getKnownROMInfoFromList(i);
		if ((types & (1 << romInfo->type)) && (pairTypes & (1 << romInfo->pairType))) {
			*currentROMInList++ = romInfo;
		}
	}
	*currentROMInList = NULL;
	return romInfoList;
}

void ROMInfo::freeROMInfoList(const ROMInfo **romInfoList) {
	delete[] romInfoList;
}

const ROMInfo * const *ROMInfo::getAllROMInfos(Bit32u *itemCount) {
	if (itemCount != NULL) *itemCount = getROMInfoLists().allROMInfos.itemCount;
	return getROMInfoLists().allROMInfos.romInfos;
}

const ROMInfo * const *ROMInfo::getFullROMInfos(Bit32u *itemCount) {
	if (itemCount != NULL) *itemCount = getROMInfoLists().fullROMInfos.itemCount;
	return getROMInfoLists().fullROMInfos.romInfos;
}

const ROMInfo * const *ROMInfo::getPartialROMInfos(Bit32u *itemCount) {
	if (itemCount != NULL) *itemCount = getROMInfoLists().partialROMInfos.itemCount;
	return getROMInfoLists().partialROMInfos.romInfos;
}

const ROMImage *ROMImage::makeFullROMImage(Bit8u *data, size_t dataSize) {
	return new ROMImage(new ArrayFile(data, dataSize), true, getKnownROMInfoList());
}

const ROMImage *ROMImage::appendImages(const ROMImage *romImageLow, const ROMImage *romImageHigh) {
	const Bit8u *romDataLow = romImageLow->getFile()->getData();
	const Bit8u *romDataHigh = romImageHigh->getFile()->getData();
	size_t partSize = romImageLow->getFile()->getSize();
	Bit8u *data = new Bit8u[2 * partSize];
	memcpy(data, romDataLow, partSize);
	memcpy(data + partSize, romDataHigh, partSize);
	const ROMImage *romImageFull = makeFullROMImage(data, 2 * partSize);
	if (romImageFull->getROMInfo() == NULL) {
		freeROMImage(romImageFull);
		return NULL;
	}
	return romImageFull;
}

const ROMImage *ROMImage::interleaveImages(const ROMImage *romImageEven, const ROMImage *romImageOdd) {
	const Bit8u *romDataEven = romImageEven->getFile()->getData();
	const Bit8u *romDataOdd = romImageOdd->getFile()->getData();
	size_t partSize = romImageEven->getFile()->getSize();
	Bit8u *data = new Bit8u[2 * partSize];
	Bit8u *writePtr = data;
	for (size_t romDataIx = 0; romDataIx < partSize; romDataIx++) {
		*(writePtr++) = romDataEven[romDataIx];
		*(writePtr++) = romDataOdd[romDataIx];
	}
	const ROMImage *romImageFull = makeFullROMImage(data, 2 * partSize);
	if (romImageFull->getROMInfo() == NULL) {
		freeROMImage(romImageFull);
		return NULL;
	}
	return romImageFull;
}

ROMImage::ROMImage(File *useFile, bool useOwnFile, const ROMInfo * const *romInfos) :
	file(useFile), ownFile(useOwnFile), romInfo(ROMInfo::getROMInfo(file, romInfos))
{}

ROMImage::~ROMImage() {
	ROMInfo::freeROMInfo(romInfo);
	if (ownFile) {
		const Bit8u *data = file->getData();
		delete file;
		delete[] data;
	}
}

const ROMImage *ROMImage::makeROMImage(File *file) {
	return new ROMImage(file, false, getKnownROMInfoList());
}

const ROMImage *ROMImage::makeROMImage(File *file, const ROMInfo * const *romInfos) {
	return new ROMImage(file, false, romInfos);
}

const ROMImage *ROMImage::makeROMImage(File *file1, File *file2) {
	const ROMInfo * const *partialROMInfos = getROMInfoLists().partialROMInfos.romInfos;
	const ROMImage *image1 = makeROMImage(file1, partialROMInfos);
	const ROMImage *image2 = makeROMImage(file2, partialROMInfos);
	const ROMImage *fullImage = image1->getROMInfo() == NULL || image2->getROMInfo() == NULL ? NULL : mergeROMImages(image1, image2);
	freeROMImage(image1);
	freeROMImage(image2);
	return fullImage;
}

void ROMImage::freeROMImage(const ROMImage *romImage) {
	delete romImage;
}

const ROMImage *ROMImage::mergeROMImages(const ROMImage *romImage1, const ROMImage *romImage2) {
	if (romImage1->romInfo->pairROMInfo != romImage2->romInfo) {
		return NULL;
	}
	switch (romImage1->romInfo->pairType) {
	case ROMInfo::FirstHalf:
		return appendImages(romImage1, romImage2);
	case ROMInfo::SecondHalf:
		return appendImages(romImage2, romImage1);
	case ROMInfo::Mux0:
		return interleaveImages(romImage1, romImage2);
	case ROMInfo::Mux1:
		return interleaveImages(romImage2, romImage1);
	default:
		break;
	}
	return NULL;
}

File *ROMImage::getFile() const {
	return file;
}

bool ROMImage::isFileUserProvided() const {
	return !ownFile;
}

const ROMInfo *ROMImage::getROMInfo() const {
	return romInfo;
}

const MachineConfiguration * const *MachineConfiguration::getAllMachineConfigurations(Bit32u *itemCount) {
	static const ROMInfoLists &romInfoLists = getROMInfoLists();
	static const MachineConfiguration MT32_1_04 = MachineConfiguration("mt32_1_04", romInfoLists.mt32_1_04.romInfos, romInfoLists.mt32_1_04.itemCount);
	static const MachineConfiguration MT32_1_05 = MachineConfiguration("mt32_1_05", romInfoLists.mt32_1_05.romInfos, romInfoLists.mt32_1_05.itemCount);
	static const MachineConfiguration MT32_1_06 = MachineConfiguration("mt32_1_06", romInfoLists.mt32_1_06.romInfos, romInfoLists.mt32_1_06.itemCount);
	static const MachineConfiguration MT32_1_07 = MachineConfiguration("mt32_1_07", romInfoLists.mt32_1_07.romInfos, romInfoLists.mt32_1_07.itemCount);
	static const MachineConfiguration MT32_BLUER = MachineConfiguration("mt32_bluer", romInfoLists.mt32_bluer.romInfos, romInfoLists.mt32_bluer.itemCount);
	static const MachineConfiguration MT32_2_03 = MachineConfiguration("mt32_2_03", romInfoLists.mt32_2_03.romInfos, romInfoLists.mt32_2_03.itemCount);
	static const MachineConfiguration MT32_2_04 = MachineConfiguration("mt32_2_04", romInfoLists.mt32_2_04.romInfos, romInfoLists.mt32_2_04.itemCount);
	static const MachineConfiguration MT32_2_06 = MachineConfiguration("mt32_2_06", romInfoLists.mt32_2_06.romInfos, romInfoLists.mt32_2_06.itemCount);
	static const MachineConfiguration MT32_2_07 = MachineConfiguration("mt32_2_07", romInfoLists.mt32_2_07.romInfos, romInfoLists.mt32_2_07.itemCount);
	static const MachineConfiguration CM32L_1_00 = MachineConfiguration("cm32l_1_00", romInfoLists.cm32l_1_00.romInfos, romInfoLists.cm32l_1_00.itemCount);
	static const MachineConfiguration CM32L_1_02 = MachineConfiguration("cm32l_1_02", romInfoLists.cm32l_1_02.romInfos, romInfoLists.cm32l_1_02.itemCount);
	static const MachineConfiguration CM32LN_1_00 = MachineConfiguration("cm32ln_1_00", romInfoLists.cm32ln_1_00.romInfos, romInfoLists.cm32ln_1_00.itemCount);
	static const MachineConfiguration * const MACHINE_CONFIGURATIONS[] = {
		&MT32_1_04, &MT32_1_05, &MT32_1_06, &MT32_1_07, &MT32_BLUER, &MT32_2_03, &MT32_2_04, &MT32_2_06, &MT32_2_07, &CM32L_1_00, &CM32L_1_02, &CM32LN_1_00, NULL
	};

	if (itemCount != NULL) *itemCount = _CALC_ARRAY_LENGTH(MACHINE_CONFIGURATIONS);
	return MACHINE_CONFIGURATIONS;
}

MachineConfiguration::MachineConfiguration(const char *useMachineID, const ROMInfo * const *useROMInfos, Bit32u useROMInfosCount) :
	machineID(useMachineID), romInfos(useROMInfos), romInfosCount(useROMInfosCount)
{}

const char *MachineConfiguration::getMachineID() const {
	return machineID;
}

const ROMInfo * const *MachineConfiguration::getCompatibleROMInfos(Bit32u *itemCount) const {
	if (itemCount != NULL) *itemCount = romInfosCount;
	return romInfos;
}

} // namespace MT32Emu
