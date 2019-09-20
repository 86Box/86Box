/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		CD-ROM image file handling module.
 *
 *		Re-hacked to remove the dirname() function, and to have this
 *		code using stdio instead of C++ fstream - fstream cannot deal
 *		with Unicode pathnames, and we need those.  --FvK
 *
 * **NOTE**	This code will very soon be replaced with a C variant, so
 *		no more changes will be done.
 *
 * Version:	@(#)cdrom_dosbox.cpp	1.0.11	2019/03/05
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		The DOSBox Team, <unknown>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2002-2015 The DOSBox Team.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define __STDC_FORMAT_MACROS
#include <stdarg.h>
#include <cinttypes>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#ifdef _WIN32
# include <string.h>
#else
# include <libgen.h>
#endif
#include <wchar.h>
#include <vector>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../plat.h"
#include "cdrom_dosbox.h"

using namespace std;


#define MAX_LINE_LENGTH 512
#define MAX_FILENAME_LENGTH 256
#define CROSS_LEN 512



#ifdef ENABLE_CDROM_DOSBOX_LOG
int cdrom_dosbox_do_log = ENABLE_CDROM_DOSBOX_LOG;


void
cdrom_dosbox_log(const char *fmt, ...)
{
    va_list ap;

    if (cdrom_dosbox_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define cdrom_dosbox_log(fmt, ...)
#endif


CDROM_Interface_Image::BinaryFile::BinaryFile(const wchar_t *filename, bool &error)
{
    memset(fn, 0x00, sizeof(fn));
    wcscpy(fn, filename);
    file = plat_fopen64(fn, L"rb");
    cdrom_dosbox_log("CDROM: binary_open(%ls) = %08lx\n", fn, file);

    if (file == NULL)
	error = true;
    else
	error = false;
}


CDROM_Interface_Image::BinaryFile::~BinaryFile(void)
{
    if (file != NULL) {
	fclose(file);
	file = NULL;
    }
    memset(fn, 0x00, sizeof(fn));
}


bool
CDROM_Interface_Image::BinaryFile::read(uint8_t *buffer, uint64_t seek, size_t count)
{
    cdrom_dosbox_log("CDROM: binary_read(%08lx, pos=%" PRIu64 " count=%lu\n",
		     file, seek, count);
    if (file == NULL) return 0;

    fseeko64(file, seek, SEEK_SET);
    if (fread(buffer, count, 1, file) != 1) {
#ifdef ENABLE_CDROM_DOSBOX_LOG
	cdrom_dosbox_log("CDROM: binary_read failed!\n");
#endif
	return 0;
    }

    return 1;
}


uint64_t
CDROM_Interface_Image::BinaryFile::getLength(void)
{
    off64_t len;

    cdrom_dosbox_log("CDROM: binary_length(%08lx)\n", file);
    if (file == NULL) return 0;

    fseeko64(file, 0, SEEK_END);
    len = ftello64(file);
    cdrom_dosbox_log("CDROM: binary_length(%08lx) = %" PRIu64 "\n", file, len);

    return len;
}


CDROM_Interface_Image::CDROM_Interface_Image(void)
{
}


CDROM_Interface_Image::~CDROM_Interface_Image(void)
{
    ClearTracks();
}


void
CDROM_Interface_Image::InitNewMedia(void)
{
}


bool
CDROM_Interface_Image::SetDevice(const wchar_t *path, int forceCD)
{
    (void)forceCD;

    if (CueLoadSheet(path)) return true;

    if (IsoLoadFile(path)) return true;
	
    return false;
}


bool
CDROM_Interface_Image::GetUPC(uint8_t& attr, char* upc)
{
    attr = 0;
    strcpy(upc, this->mcn.c_str());

    return true;
}


bool
CDROM_Interface_Image::GetAudioTracks(int& stTrack, int& end, TMSF& leadOut)
{
    stTrack = 1;
    end = (int)(tracks.size() - 1);
    FRAMES_TO_MSF(tracks[tracks.size() - 1].start + 150, &leadOut.min, &leadOut.sec, &leadOut.fr);

    return true;
}


bool
CDROM_Interface_Image::GetAudioTrackInfo(int track, int& track_number, TMSF& start, uint8_t& attr)
{
    if (track < 1 || track > (int)tracks.size()) return false;

    FRAMES_TO_MSF(tracks[track - 1].start + 150, &start.min, &start.sec, &start.fr);
    track_number = tracks[track - 1].track_number;
    attr = tracks[track - 1].attr;

    return true;
}


bool
CDROM_Interface_Image::GetAudioTrackEndInfo(int track, int& track_number, TMSF& start, unsigned char& attr)
{
    if (track < 1 || track > (int)tracks.size()) return false;

    FRAMES_TO_MSF(tracks[track - 1].start + tracks[track - 1].length + 150, &start.min, &start.sec, &start.fr);
    track_number = tracks[track - 1].track_number;
    attr = tracks[track - 1].attr;

    return true;
}


bool
CDROM_Interface_Image::GetAudioSub(int sector, uint8_t& attr, uint8_t& track, uint8_t& index, TMSF& relPos, TMSF& absPos)
{
    int cur_track = GetTrack(sector);

    if (cur_track < 1) return false;

    track = (uint8_t)cur_track;
    attr = tracks[track - 1].attr;
    index = 1;

    FRAMES_TO_MSF(sector + 150, &absPos.min, &absPos.sec, &absPos.fr);

    /* Absolute position should be adjusted by 150, not the relative ones. */
    FRAMES_TO_MSF(sector - tracks[track - 1].start, &relPos.min, &relPos.sec, &relPos.fr);

    return true;
}


bool
CDROM_Interface_Image::GetMediaTrayStatus(bool& mediaPresent, bool& mediaChanged, bool& trayOpen)
{
    mediaPresent = true;
    mediaChanged = false;
    trayOpen = false;

    return true;
}


bool
CDROM_Interface_Image::ReadSectors(PhysPt buffer, bool raw, uint32_t sector, uint32_t num)
{
    int sectorSize = raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE;
    uint8_t buflen = num * sectorSize;
    uint8_t* buf = new uint8_t[buflen];
    bool success = true;	/* reading 0 sectors is OK */
    uint32_t i;

    for (i = 0; i < num; i++) {
	success = ReadSector(&buf[i * sectorSize], raw, sector + i);
	if (! success) break;
    }

    memcpy((void*)buffer, buf, buflen);
    delete[] buf;

    return success;
}


bool
CDROM_Interface_Image::LoadUnloadMedia(bool unload)
{
    (void)unload;

    return true;
}


int
CDROM_Interface_Image::GetTrack(unsigned int sector)
{
    vector<Track>::iterator i = tracks.begin();
    vector<Track>::iterator end = tracks.end() - 1;
	
    while (i != end) {
	Track &curr = *i;
	Track &next = *(i + 1);
	if (curr.start <= sector && sector < next.start)
		return curr.number;
	i++;
    }

    return -1;
}


bool
CDROM_Interface_Image::ReadSector(uint8_t *buffer, bool raw, uint32_t sector)
{
    size_t length;

    int track = GetTrack(sector) - 1;
    if (track < 0) return false;

    uint64_t s = (uint64_t) sector;	
    uint64_t seek = tracks[track].skip + ((s - tracks[track].start) * tracks[track].sectorSize);
    if (tracks[track].mode2)
	length = (raw ? RAW_SECTOR_SIZE : 2336);
    else
	length = (raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE);
    if (tracks[track].sectorSize != RAW_SECTOR_SIZE && raw) return false;
    if (tracks[track].sectorSize == RAW_SECTOR_SIZE && !tracks[track].mode2 && !raw) seek += 16;
    if (tracks[track].mode2 && !raw) seek += 24;

    return tracks[track].file->read(buffer, seek, length);
}


bool
CDROM_Interface_Image::ReadSectorSub(uint8_t *buffer, uint32_t sector)
{
    int track = GetTrack(sector) - 1;
    if (track < 0) return false;

    uint64_t s = (uint64_t) sector;	
    uint64_t seek = tracks[track].skip + ((s - tracks[track].start) * tracks[track].sectorSize);
    if (tracks[track].sectorSize != 2448) return false;

    return tracks[track].file->read(buffer, seek, 2448);
}


int
CDROM_Interface_Image::GetSectorSize(uint32_t sector)
{
    int track = GetTrack(sector) - 1;
    if (track < 0) return 0;

    return tracks[track].sectorSize;
}


bool
CDROM_Interface_Image::IsMode2(uint32_t sector)
{
    int track = GetTrack(sector) - 1;

    if (track < 0) return false;
	
    if (tracks[track].mode2)
	return true;

    return false;
}


int
CDROM_Interface_Image::GetMode2Form(uint32_t sector)
{
    int track = GetTrack(sector) - 1;

    if (track < 0) return false;

    return tracks[track].form;
}


bool
CDROM_Interface_Image::CanReadPVD(TrackFile *file, uint64_t sectorSize, bool mode2)
{
    uint8_t pvd[COOKED_SECTOR_SIZE];
    uint64_t seek = 16 * sectorSize;	// first vd is located at sector 16

    if (sectorSize == RAW_SECTOR_SIZE && !mode2) seek += 16;
    if (mode2) seek += 24;

    file->read(pvd, seek, COOKED_SECTOR_SIZE);

#if 0
    pvd[0] = descriptor type, pvd[1..5] = standard identifier, pvd[6] = iso version (+8 for High Sierra)
#endif

    return ((pvd[0] == 1 && !strncmp((char*)(&pvd[1]), "CD001", 5) && pvd[6] == 1) ||
            (pvd[8] == 1 && !strncmp((char*)(&pvd[9]), "CDROM", 5) && pvd[14] == 1));
}


bool
CDROM_Interface_Image::IsoLoadFile(const wchar_t *filename)
{
    tracks.clear();
	
    // data track
    Track track = {0, 0, 0, 0, 0, 0, 0, 0, false, NULL};
    bool error;
    track.file = new BinaryFile(filename, error);
    if (error) {
	delete track.file;
	return false;
    }
    track.number = 1;
    track.track_number = 1;	//IMPORTANT: This is needed.
    track.attr = DATA_TRACK;	//data
    track.form = 0;

    // try to detect iso type
    if (CanReadPVD(track.file, COOKED_SECTOR_SIZE, false)) {
	track.sectorSize = COOKED_SECTOR_SIZE;
	track.mode2 = false;
    } else if (CanReadPVD(track.file, RAW_SECTOR_SIZE, false)) {
	track.sectorSize = RAW_SECTOR_SIZE;
	track.mode2 = false;		
    } else if (CanReadPVD(track.file, 2336, true)) {
	track.sectorSize = 2336;
	track.mode2 = true;
    } else if (CanReadPVD(track.file, 2324, true)) {
	track.sectorSize = 2324;
	track.form = 2;
	track.mode2 = true;
    } else if (CanReadPVD(track.file, RAW_SECTOR_SIZE, true)) {
	track.sectorSize = RAW_SECTOR_SIZE;
	track.mode2 = true;		
    } else {
	/* Unknown mode: Assume regular 2048-byte sectors, this is needed so Apple Rhapsody ISO's can be mounted. */
	track.sectorSize = COOKED_SECTOR_SIZE;
	track.mode2 = false;
    }

    track.length = track.file->getLength() / track.sectorSize;
    tracks.push_back(track);
	
    // leadout track
    track.number = 2;
    track.track_number = 0xAA;
    track.attr = 0x16;	/* Was 0x00 but I believe 0x16 is appropriate. */
    track.start = track.length;
    track.length = 0;
    track.file = NULL;
    tracks.push_back(track);

    return true;
}


bool
CDROM_Interface_Image::CueGetBuffer(char *str, char **line, bool up)
{
    char *s = *line;
    char *p = str;
    int quote = 0;
    int done = 0;
    int space = 1;

    /* Copy to local buffer until we have end of string or whitespace. */
    while (! done) {
	switch(*s) {
		case '\0':
			if (quote) {
				/* Ouch, unterminated string.. */
				return false;
			}
			done = 1;
			break;

		case '\"':
			quote ^= 1;
			break;

		case ' ':
		case '\t':
			if (space)
				break;

			if (! quote) {
				done = 1;
				break;
			}
			/*FALLTHROUGH*/

		default:
			if (up && islower((int) *s))
				*p++ = toupper((int) *s);
			else
				*p++ = *s;
			space = 0;
			break;
	}

	if (! done)
		s++;
    }
    *p = '\0';

    *line = s;

    return true;
}


/* Get a filename string from the input line. */
bool
CDROM_Interface_Image::CueGetString(string &dest, char **line)
{
    char temp[1024];
    bool success;

    success = CueGetBuffer(temp, line, false);
    if (success)
	dest = temp;

    return success;
}


bool
CDROM_Interface_Image::CueGetKeyword(string &dest, char **line)
{
    char temp[1024];
    bool success;

    success = CueGetBuffer(temp, line, true);
    if (success)
	dest = temp;

    return success;
}


/* Get a string from the input line, handling quotes properly. */
uint64_t
CDROM_Interface_Image::CueGetNumber(char **line)
{
    char temp[128];
    uint64_t num;

    if (! CueGetBuffer(temp, line, false))
	return 0;

    if (sscanf(temp, "%" PRIu64, &num) != 1)
	return 0;

    return num;
}


bool
CDROM_Interface_Image::CueGetFrame(uint64_t &frames, char **line)
{
    char temp[128];
    int min, sec, fr;
    bool success;

    success = CueGetBuffer(temp, line, false);
    if (! success) return false;

    success = sscanf(temp, "%d:%d:%d", &min, &sec, &fr) == 3;
    if (! success) return false;

    frames = MSF_TO_FRAMES(min, sec, fr);

    return true;
}


bool
CDROM_Interface_Image::CueLoadSheet(const wchar_t *cuefile)
{
    Track track = {0, 0, 0, 0, 0, 0, 0, 0, false, NULL};
    wchar_t pathname[MAX_FILENAME_LENGTH];
    uint64_t shift = 0;
    uint64_t currPregap = 0;
    uint64_t totalPregap = 0;
    uint64_t prestart = 0;
    bool canAddTrack = false;
    bool success;
    FILE *fp;
    wstring name(L"r");

    tracks.clear();

    /* Get a copy of the filename into pathname, we need it later. */
    memset(pathname, 0, MAX_FILENAME_LENGTH * sizeof(wchar_t));
    plat_get_dirname(pathname, cuefile);

    /* Open the file. */
    fp = plat_fopen((wchar_t *) cuefile, (wchar_t *) name.c_str());
    if (fp == NULL)
	return false;

    success = false;

    for (;;) {
	char buf[MAX_LINE_LENGTH];
	char *line = buf;

	/* Read a line from the cuesheet file. */
	if (fgets(buf, sizeof(buf), fp) == NULL || ferror(fp) || feof(fp))
		break;
	buf[strlen(buf) - 1] = '\0';	/* nuke trailing newline */

	string command;
	success = CueGetKeyword(command, &line);

	if (command == "TRACK") {
		if (canAddTrack)
			success = AddTrack(track, shift, prestart, totalPregap, currPregap);
		else
			success = true;

		track.start = 0;
		track.skip = 0;
		currPregap = 0;
		prestart = 0;

		track.number = CueGetNumber(&line);
		track.track_number = track.number;
		string type;
		success = CueGetKeyword(type, &line);
		if (! success) break;

		track.form = 0;

		if (type == "AUDIO") {
			track.sectorSize = RAW_SECTOR_SIZE;
			track.attr = AUDIO_TRACK;
			track.mode2 = false;
		} else if (type == "MODE1/2048") {
			track.sectorSize = COOKED_SECTOR_SIZE;
			track.attr = DATA_TRACK;
			track.mode2 = false;
		} else if (type == "MODE1/2352") {
			track.sectorSize = RAW_SECTOR_SIZE;
			track.attr = DATA_TRACK;
			track.mode2 = false;
		} else if (type == "MODE2/2048") {
			track.form = 1;
			track.sectorSize = 2048;
			track.attr = DATA_TRACK;
			track.mode2 = true;
		} else if (type == "MODE2/2324") {
			track.form = 2;
			track.sectorSize = 2324;
			track.attr = DATA_TRACK;
			track.mode2 = true;
		} else if (type == "MODE2/2336") {
			track.sectorSize = 2336;
			track.attr = DATA_TRACK;
			track.mode2 = true;
		} else if (type == "MODE2/2352") {
			track.form = 1;		/* Assume this is XA Mode 2 Form 1. */
			track.sectorSize = RAW_SECTOR_SIZE;
			track.attr = DATA_TRACK;
			track.mode2 = true;
		} else if (type == "CDG/2448") {
			track.sectorSize = 2448;
			track.attr = DATA_TRACK;
			track.mode2 = true;
		} else if (type == "CDI/2336") {
			track.sectorSize = 2336;
			track.attr = DATA_TRACK;
			track.mode2 = true;
		} else if (type == "CDI/2352") {
			track.sectorSize = RAW_SECTOR_SIZE;
			track.attr = DATA_TRACK;
			track.mode2 = true;
		} else
			success = false;

		canAddTrack = true;
	} else if (command == "INDEX") {
		uint64_t frame, index;
		index = CueGetNumber(&line);
		success = CueGetFrame(frame, &line);

		switch(index) {
			case 0:
				prestart = frame;
				break;

			case 1:
				track.start = frame;
				break;

			default:
				/* ignore other indices */
				break;
		}
	} else if (command == "FILE") {
		if (canAddTrack)
			success = AddTrack(track, shift, prestart, totalPregap, currPregap);
		else
			success = true;
		canAddTrack = false;

		char ansi[MAX_FILENAME_LENGTH];
		wchar_t filename[MAX_FILENAME_LENGTH];
		string type;
		memset(ansi, 0, MAX_FILENAME_LENGTH);
		memset(filename, 0, MAX_FILENAME_LENGTH * sizeof(wchar_t));

		success = CueGetBuffer(ansi, &line, false);
		if (! success) break;
		success = CueGetKeyword(type, &line);
		if (! success) break;

		track.file = NULL;
		bool error = true;

		if (type == "BINARY") {
			wchar_t temp[MAX_FILENAME_LENGTH];
			memset(temp, 0, MAX_FILENAME_LENGTH * sizeof(wchar_t));
			mbstowcs(temp, ansi, sizeof_w(temp));
			plat_append_filename(filename, pathname, temp);
			track.file = new BinaryFile(filename, error);
		}
		if (error) {
#ifdef ENABLE_CDROM_DOSBOX_LOG
			cdrom_dosbox_log("CUE: cannot open fille '%ls' in cue sheet!\n",
					 filename);
#endif
			delete track.file;
			track.file = NULL;
			success = false;
		}
	} else if (command == "PREGAP")
		success = CueGetFrame(currPregap, &line);
	else if (command == "CATALOG") {
		success = CueGetString(mcn, &line);
	// ignored commands
	} else if (command == "CDTEXTFILE" || command == "FLAGS" || command == "ISRC"
		|| command == "PERFORMER" || command == "POSTGAP" || command == "REM"
		|| command == "SONGWRITER" || command == "TITLE" || command == "") success = true;
	// failure
	else {
#ifdef ENABLE_CDROM_DOSBOX_LOG
		cdrom_dosbox_log("CUE: unsupported command '%s' in cue sheet!\n",
				 command.c_str());
#endif
		success = false;
	}

	if (! success)
		break;
    }

    fclose(fp);
    if (! success)
	return false;

    // add last track
    if (! AddTrack(track, shift, prestart, totalPregap, currPregap))
	return false;

    // add leadout track
    track.number++;
    track.track_number = 0xAA;
    // track.attr = 0;//sync with load iso
    track.attr = 0x16;	/* Was 0x00 but I believe 0x16 is appropriate. */
    track.start = 0;
    track.length = 0;
    track.file = NULL;
    if (! AddTrack(track, shift, 0, totalPregap, 0))
	return false;

    return true;
}


bool
CDROM_Interface_Image::AddTrack(Track &curr, uint64_t &shift, uint64_t prestart, uint64_t &totalPregap, uint64_t currPregap)
{
    // frames between index 0(prestart) and 1(curr.start) must be skipped
    uint64_t skip;

    if (prestart > 0) {
	if (prestart > curr.start) return false;
	skip = curr.start - prestart;
    } else skip = 0;

    // first track (track number must be 1)
    if (tracks.empty()) {
	if (curr.number != 1) return false;
	curr.skip = skip * curr.sectorSize;
	curr.start += currPregap;
	totalPregap = currPregap;
	tracks.push_back(curr);
	return true;
    }

    Track &prev = *(tracks.end() - 1);

    // current track consumes data from the same file as the previous
    if (prev.file == curr.file) {
	curr.start += shift;
	prev.length = curr.start + totalPregap - prev.start - skip;
	curr.skip += prev.skip + (prev.length * prev.sectorSize) + (skip * curr.sectorSize);
	totalPregap += currPregap;
	curr.start += totalPregap;
	// current track uses a different file as the previous track
    } else {
	uint64_t tmp = prev.file->getLength() - ((uint64_t) prev.skip);
	prev.length = tmp / ((uint64_t) prev.sectorSize);
	if (tmp % prev.sectorSize != 0) prev.length++; // padding

	curr.start += prev.start + prev.length + currPregap;
	curr.skip = skip * curr.sectorSize;
	shift += prev.start + prev.length;
	totalPregap = currPregap;
    }

    // error checks
    if (curr.number <= 1) return false;
    if (prev.number + 1 != curr.number) return false;
    if (curr.start < prev.start + prev.length) return false;

    tracks.push_back(curr);

    return true;
}


bool
CDROM_Interface_Image::HasDataTrack(void)
{
    //Data track has attribute 0x14
    for (track_it it = tracks.begin(); it != tracks.end(); it++) {
	if ((*it).attr == DATA_TRACK) return true;
    }
    return false;
}


bool
CDROM_Interface_Image::HasAudioTracks(void)
{
    for (track_it it = tracks.begin(); it != tracks.end(); it++) {
	if ((*it).attr == AUDIO_TRACK) return true;
    }
    return false;
}


void
CDROM_Interface_Image::ClearTracks(void)
{
    vector<Track>::iterator i = tracks.begin();
    vector<Track>::iterator end = tracks.end();

    TrackFile* last = NULL;	
    while(i != end) {
	Track &curr = *i;
	if (curr.file != last) {
		delete curr.file;
		last = curr.file;
	}
	i++;
    }
    tracks.clear();
}
