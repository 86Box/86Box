/*
 *  Copyright (C) 2002-2015  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* Modified for use with PCem by bit */

#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#ifdef _WIN32
//FIXME: should not be needed. */
# define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdint.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <limits.h> //GCC 2.95
#include <sstream>
#include <vector>
#include <sys/stat.h>
#include "../plat.h"
#include "cdrom_dosbox.h"

#ifndef _WIN32
# include <libgen.h>
#else
# include <string.h>
#endif

using namespace std;

#define MAX_LINE_LENGTH 512
#define MAX_FILENAME_LENGTH 256
#define CROSS_LEN 512

#define safe_strncpy(a,b,n) do { strncpy((a),(b),(n)-1); (a)[(n)-1] = 0; } while (0)

CDROM_Interface_Image::BinaryFile::BinaryFile(const char *filename, bool &error)
{
	memset(fn, 0, sizeof(fn));
	strcpy(fn, filename);
	error = false;
}

CDROM_Interface_Image::BinaryFile::~BinaryFile()
{
	memset(fn, 0, sizeof(fn));
}

bool CDROM_Interface_Image::BinaryFile::read(Bit8u *buffer, uint64_t seek, uint64_t count)
{
	file = fopen64(fn, "rb");
	if (file == NULL) return 0;
	fseeko64(file, seek, SEEK_SET);
	fread(buffer, 1, count, file);
	fclose(file);
	return 1;
}

uint64_t CDROM_Interface_Image::BinaryFile::getLength()
{
	uint64_t ret = 0;
	file = fopen64(fn, "rb");
	if (file == NULL) return 0;
	fseeko64(file, 0, SEEK_END);
	ret = ftello64(file);
	fclose(file);
	return ret;
}

CDROM_Interface_Image::CDROM_Interface_Image()
{
}

CDROM_Interface_Image::~CDROM_Interface_Image()
{
	ClearTracks();
}

void CDROM_Interface_Image::InitNewMedia()
{
}

bool CDROM_Interface_Image::SetDevice(char* path, int forceCD)
{
	(void)forceCD;
	if (LoadCueSheet(path)) return true;
	if (LoadIsoFile(path)) return true;
	
	// print error message on dosbox console
	//printf("Could not load image file: %s\n", path);
	return false;
}

bool CDROM_Interface_Image::GetUPC(unsigned char& attr, char* upc)
{
	attr = 0;
	strcpy(upc, this->mcn.c_str());
	return true;
}

bool CDROM_Interface_Image::GetAudioTracks(int& stTrack, int& end, TMSF& leadOut)
{
	stTrack = 1;
	end = (int)(tracks.size() - 1);
	FRAMES_TO_MSF(tracks[tracks.size() - 1].start + 150, &leadOut.min, &leadOut.sec, &leadOut.fr);
	return true;
}

bool CDROM_Interface_Image::GetAudioTrackInfo(int track, int& track_number, TMSF& start, unsigned char& attr)
{
	if (track < 1 || track > (int)tracks.size()) return false;
	FRAMES_TO_MSF(tracks[track - 1].start + 150, &start.min, &start.sec, &start.fr);
	track_number = tracks[track - 1].track_number;
	attr = tracks[track - 1].attr;
	return true;
}

bool CDROM_Interface_Image::GetAudioSub(int sector, unsigned char& attr, unsigned char& track, unsigned char& index, TMSF& relPos, TMSF& absPos)
{
        int cur_track = GetTrack(sector);
        if (cur_track < 1) return false;
        track = (unsigned char)cur_track;
        attr = tracks[track - 1].attr;
        index = 1;
        FRAMES_TO_MSF(sector + 150, &absPos.min, &absPos.sec, &absPos.fr);
        /* FRAMES_TO_MSF(sector - tracks[track - 1].start + 150, &relPos.min, &relPos.sec, &relPos.fr); */
	/* Note by Kotori: Yes, the absolute position should be adjusted by 150, but not the relative position. */
        FRAMES_TO_MSF(sector - tracks[track - 1].start, &relPos.min, &relPos.sec, &relPos.fr);
        return true;
}

bool CDROM_Interface_Image::GetMediaTrayStatus(bool& mediaPresent, bool& mediaChanged, bool& trayOpen)
{
	mediaPresent = true;
	mediaChanged = false;
	trayOpen = false;
	return true;
}

bool CDROM_Interface_Image::ReadSectors(PhysPt buffer, bool raw, unsigned long sector, unsigned long num)
{
	int sectorSize = raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE;
	Bitu buflen = num * sectorSize;
	Bit8u* buf = new Bit8u[buflen];
	
	bool success = true; //Gobliiins reads 0 sectors
	for(unsigned long i = 0; i < num; i++) {
		success = ReadSector(&buf[i * sectorSize], raw, sector + i);
		if (!success) break;
	}

	memcpy((void*)buffer, buf, buflen);
	delete[] buf;

	return success;
}

bool CDROM_Interface_Image::LoadUnloadMedia(bool unload)
{
	(void)unload;
	return true;
}

int CDROM_Interface_Image::GetTrack(unsigned int sector)
{
	vector<Track>::iterator i = tracks.begin();
	vector<Track>::iterator end = tracks.end() - 1;
	
	while(i != end) {
		Track &curr = *i;
		Track &next = *(i + 1);
		if (curr.start <= sector && sector < next.start) return curr.number;
		i++;
	}
	return -1;
}

bool CDROM_Interface_Image::ReadSector(Bit8u *buffer, bool raw, unsigned long sector)
{
	uint64_t length;

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

bool CDROM_Interface_Image::ReadSectorSub(Bit8u *buffer, unsigned long sector)
{
	int track = GetTrack(sector) - 1;
	if (track < 0) return false;

	uint64_t s = (uint64_t) sector;	
	uint64_t seek = tracks[track].skip + ((s - tracks[track].start) * tracks[track].sectorSize);
	if (tracks[track].sectorSize != 2448) return false;

	return tracks[track].file->read(buffer, seek, 2448);
}

int CDROM_Interface_Image::GetSectorSize(unsigned long sector)
{
	int track = GetTrack(sector) - 1;
	if (track < 0) return 0;

	return tracks[track].sectorSize;
}

bool CDROM_Interface_Image::IsMode2(unsigned long sector)
{
	int track = GetTrack(sector) - 1;
	if (track < 0) return false;
	
	if (tracks[track].mode2)
	{
		return true;
	}
	else
	{
		return false;
	}
}

int CDROM_Interface_Image::GetMode2Form(unsigned long sector)
{
	int track = GetTrack(sector) - 1;
	if (track < 0) return false;

	return tracks[track].form;
}

bool CDROM_Interface_Image::LoadIsoFile(char* filename)
{
	tracks.clear();
	
	// data track
	Track track = {0, 0, 0, 0, 0, 0, 0, false, NULL};
	bool error;
	track.file = new BinaryFile(filename, error);
	if (error) {
		delete track.file;
		return false;
	}
	track.number = 1;
	track.track_number = 1;//IMPORTANT: This is needed.
	track.attr = DATA_TRACK;//data
	track.form = 0;
	
	// try to detect iso type
	if (CanReadPVD(track.file, COOKED_SECTOR_SIZE, false)) {
		track.sectorSize = COOKED_SECTOR_SIZE;
		track.mode2 = false;
	} else if (CanReadPVD(track.file, RAW_SECTOR_SIZE, false)) {
		track.sectorSize = RAW_SECTOR_SIZE;
		track.mode2 = false;		
	} else if (CanReadPVD(track.file, 2324, true)) {
		track.sectorSize = 2324;
		track.form = 2;
		track.mode2 = true;
	} else if (CanReadPVD(track.file, 2336, true)) {
		track.sectorSize = 2336;
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

bool CDROM_Interface_Image::CanReadPVD(TrackFile *file, uint64_t sectorSize, bool mode2)
{
	Bit8u pvd[COOKED_SECTOR_SIZE];
	uint64_t seek = 16 * sectorSize;	// first vd is located at sector 16
	if (sectorSize == RAW_SECTOR_SIZE && !mode2) seek += 16;
	if (mode2) seek += 24;
	file->read(pvd, seek, COOKED_SECTOR_SIZE);
	// pvd[0] = descriptor type, pvd[1..5] = standard identifier, pvd[6] = iso version (+8 for High Sierra)
	return ((pvd[0] == 1 && !strncmp((char*)(&pvd[1]), "CD001", 5) && pvd[6] == 1) ||
			(pvd[8] == 1 && !strncmp((char*)(&pvd[9]), "CDROM", 5) && pvd[14] == 1));
}

#ifdef _WIN32
static string dirname(char * file) {
	char * sep = strrchr(file, '\\');
	if (sep == NULL)
		sep = strrchr(file, '/');
	if (sep == NULL)
		return "";
	else {
		int len = (int)(sep - file);
		char tmp[MAX_FILENAME_LENGTH];
		safe_strncpy(tmp, file, len+1);
		return tmp;
	}
}
#endif

bool CDROM_Interface_Image::LoadCueSheet(char *cuefile)
{
	Track track = {0, 0, 0, 0, 0, 0, 0, false, NULL};
	tracks.clear();
	uint64_t shift = 0;
	uint64_t currPregap = 0;
	uint64_t totalPregap = 0;
	uint64_t prestart = 0;
	bool success;
	bool canAddTrack = false;
	char tmp[MAX_FILENAME_LENGTH];	// dirname can change its argument
	safe_strncpy(tmp, cuefile, MAX_FILENAME_LENGTH);
	string pathname(dirname(tmp));
	ifstream in;
	in.open(cuefile, ios::in);
	if (in.fail()) return false;
	
	while(!in.eof()) {
		// get next line
		char buf[MAX_LINE_LENGTH];
		in.getline(buf, MAX_LINE_LENGTH);
		if (in.fail() && !in.eof()) return false;  // probably a binary file
		istringstream line(buf);
		
		string command;
		GetCueKeyword(command, line);
		
		if (command == "TRACK") {
			if (canAddTrack) success = AddTrack(track, shift, prestart, totalPregap, currPregap);
			else success = true;
			
			track.start = 0;
			track.skip = 0;
			currPregap = 0;
			prestart = 0;
	
			line >> track.number;
			track.track_number = track.number;
			string type;
			GetCueKeyword(type, line);

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
			} else success = false;
			
			canAddTrack = true;
		}
		else if (command == "INDEX") {
			uint64_t index;
			line >> index;
			uint64_t frame;
			success = GetCueFrame(frame, line);
			
			if (index == 1) track.start = frame;
			else if (index == 0) prestart = frame;
			// ignore other indices
		}
		else if (command == "FILE") {
			if (canAddTrack) success = AddTrack(track, shift, prestart, totalPregap, currPregap);
			else success = true;
			canAddTrack = false;
			
			string filename;
			GetCueString(filename, line);
			GetRealFileName(filename, pathname);
			string type;
			GetCueKeyword(type, line);

			track.file = NULL;
			bool error = true;
			if (type == "BINARY") {
				track.file = new BinaryFile(filename.c_str(), error);
			}
			if (error) {
				delete track.file;
				success = false;
			}
		}
		else if (command == "PREGAP") success = GetCueFrame(currPregap, line);
		else if (command == "CATALOG") success = GetCueString(mcn, line);
		// ignored commands
		else if (command == "CDTEXTFILE" || command == "FLAGS" || command == "ISRC"
			|| command == "PERFORMER" || command == "POSTGAP" || command == "REM"
			|| command == "SONGWRITER" || command == "TITLE" || command == "") success = true;
		// failure
		else success = false;

		if (!success) return false;
	}
	// add last track
	if (!AddTrack(track, shift, prestart, totalPregap, currPregap)) return false;
	
	// add leadout track
	track.number++;
	track.track_number = 0xAA;
	// track.attr = 0;//sync with load iso
	track.attr = 0x16;	/* Was 0x00 but I believe 0x16 is appropriate. */
	track.start = 0;
	track.length = 0;
	track.file = NULL;
	if(!AddTrack(track, shift, 0, totalPregap, 0)) return false;

	return true;
}

bool CDROM_Interface_Image::AddTrack(Track &curr, uint64_t &shift, uint64_t prestart, uint64_t &totalPregap, uint64_t currPregap)
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
#if 0
	/* curr.length is unsigned, so... --FvK */
	if (curr.length < 0) return false;
#endif
	
	tracks.push_back(curr);
	return true;
}

bool CDROM_Interface_Image::HasDataTrack(void)
{
	//Data track has attribute 0x14
	for(track_it it = tracks.begin(); it != tracks.end(); it++) {
		if ((*it).attr == DATA_TRACK) return true;
	}
	return false;
}

bool CDROM_Interface_Image::HasAudioTracks(void)
{
        for(track_it it = tracks.begin(); it != tracks.end(); it++) {
                if ((*it).attr == AUDIO_TRACK) return true;
        }
        return false;
}


bool CDROM_Interface_Image::GetRealFileName(string &filename, string &pathname)
{
	// check if file exists
	struct stat test;
	if (stat(filename.c_str(), &test) == 0) return true;

	// check if file with path relative to cue file exists
	string tmpstr(pathname + "/" + filename);
	if (stat(tmpstr.c_str(), &test) == 0) {
		filename = tmpstr;
		return true;
	}
#if defined (_WIN32) || defined(OS2)
	//Nothing
#else
	//Consider the possibility that the filename has a windows directory seperator (inside the CUE file)
	//which is common for some commercial rereleases of DOS games using DOSBox

	string copy = filename;
	size_t l = copy.size();
	for (size_t i = 0; i < l;i++) {
		if(copy[i] == '\\') copy[i] = '/';
	}

	if (stat(copy.c_str(), &test) == 0) {
		filename = copy;
		return true;
	}

	tmpstr = pathname + "/" + copy;
	if (stat(tmpstr.c_str(), &test) == 0) {
		filename = tmpstr;
		return true;
	}

#endif
	return false;
}

bool CDROM_Interface_Image::GetCueKeyword(string &keyword, istream &in)
{
	in >> keyword;
	for(Bitu i = 0; i < keyword.size(); i++) keyword[i] = toupper(keyword[i]);
	
	return true;
}

bool CDROM_Interface_Image::GetCueFrame(uint64_t &frames, istream &in)
{
	string msf;
	in >> msf;
	int min, sec, fr;
	bool success = sscanf(msf.c_str(), "%d:%d:%d", &min, &sec, &fr) == 3;
	frames = MSF_TO_FRAMES(min, sec, fr);
	
	return success;
}

bool CDROM_Interface_Image::GetCueString(string &str, istream &in)
{
	int pos = (int)in.tellg();
	in >> str;
	if (str[0] == '\"') {
		if (str[str.size() - 1] == '\"') {
			str.assign(str, 1, str.size() - 2);
		} else {
			in.seekg(pos, ios::beg);
			char buffer[MAX_FILENAME_LENGTH];
			in.getline(buffer, MAX_FILENAME_LENGTH, '\"');	// skip
			in.getline(buffer, MAX_FILENAME_LENGTH, '\"');
			str = buffer;
		}
	}
	return true;
}

void CDROM_Interface_Image::ClearTracks()
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
