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

#ifndef __CDROM_INTERFACE__
#define __CDROM_INTERFACE__

#include <string.h>
#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>

#include <stdint.h>
typedef signed int Bits;
typedef unsigned int Bitu;
typedef int8_t   Bit8s;
typedef uint8_t  Bit8u;
typedef int16_t  Bit16s;
typedef uint16_t Bit16u;
typedef int32_t  Bit32s;
typedef uint32_t Bit32u;

typedef size_t PhysPt;

#define RAW_SECTOR_SIZE		2352
#define COOKED_SECTOR_SIZE	2048

#define DATA_TRACK 0x14
#define AUDIO_TRACK 0x10

#define CD_FPS  75
#define FRAMES_TO_MSF(f, M,S,F) {                                       \
        int value = f;                                                  \
        *(F) = value%CD_FPS;                                            \
        value /= CD_FPS;                                                \
        *(S) = value%60;                                                \
        value /= 60;                                                    \
        *(M) = value;                                                   \
}
#define MSF_TO_FRAMES(M, S, F)  ((M)*60*CD_FPS+(S)*CD_FPS+(F))


typedef struct SMSF {
	unsigned char min;
	unsigned char sec;
	unsigned char fr;
} TMSF;

typedef struct SCtrl {
	Bit8u	out[4];			// output channel
	Bit8u	vol[4];			// channel volume
} TCtrl;

extern int CDROM_GetMountType(char* path, int force);

class CDROM_Interface
{
public:
//	CDROM_Interface						(void);
	virtual ~CDROM_Interface			(void) {};

	virtual bool	SetDevice			(char* path, int forceCD) = 0;

	virtual bool	GetUPC				(unsigned char& attr, char* upc) = 0;

	virtual bool	GetAudioTracks		(int& stTrack, int& end, TMSF& leadOut) = 0;
	virtual bool	GetAudioTrackInfo	(int track, int& number, TMSF& start, unsigned char& attr) = 0;
	virtual bool    GetAudioSub             (int sector, unsigned char& attr, unsigned char& track, unsigned char& index, TMSF& relPos, TMSF& absPos) = 0;
	virtual bool	GetMediaTrayStatus	(bool& mediaPresent, bool& mediaChanged, bool& trayOpen) = 0;

	virtual bool	ReadSectors			(PhysPt buffer, bool raw, unsigned long sector, unsigned long num) = 0;

	virtual bool	LoadUnloadMedia		(bool unload) = 0;

	virtual void	InitNewMedia		(void) {};
};

class CDROM_Interface_Image : public CDROM_Interface
{
private:
	class TrackFile {
	public:
		virtual bool read(Bit8u *buffer, uint64_t seek, uint64_t count) = 0;
		virtual uint64_t getLength() = 0;
		virtual ~TrackFile() { };
	};
	
	class BinaryFile : public TrackFile {
	public:
		BinaryFile(const char *filename, bool &error);
		~BinaryFile();
		bool read(Bit8u *buffer, uint64_t seek, uint64_t count);
		uint64_t getLength();
	private:
		BinaryFile();
		char fn[260];
		FILE *file;
	};
	
	struct Track {
		int number;
		int track_number;
		int attr;
		int form;
		uint64_t start;
		uint64_t length;
		uint64_t skip;
		uint64_t sectorSize;
		bool mode2;
		TrackFile *file;
	};

public:
	CDROM_Interface_Image		();
	virtual ~CDROM_Interface_Image	(void);
	void	InitNewMedia		(void);
	bool	SetDevice		(char* path, int forceCD);
	bool	GetUPC			(unsigned char& attr, char* upc);
	bool	GetAudioTracks		(int& stTrack, int& end, TMSF& leadOut);
	bool	GetAudioTrackInfo	(int track, int& number, TMSF& start, unsigned char& attr);
	bool    GetAudioSub             (int sector, unsigned char& attr, unsigned char& track, unsigned char& index, TMSF& relPos, TMSF& absPos);
	bool	GetMediaTrayStatus	(bool& mediaPresent, bool& mediaChanged, bool& trayOpen);
	bool	ReadSectors		(PhysPt buffer, bool raw, unsigned long sector, unsigned long num);
	bool	LoadUnloadMedia		(bool unload);
	bool	ReadSector		(Bit8u *buffer, bool raw, unsigned long sector);
	bool	ReadSectorSub		(Bit8u *buffer, unsigned long sector);
	int	GetSectorSize		(unsigned long sector);
	bool	IsMode2			(unsigned long sector);
	int	GetMode2Form		(unsigned long sector);
	bool	HasDataTrack		(void);
        bool    HasAudioTracks          (void);
	
        int     GetTrack                (unsigned int sector);

private:
	// player
static	void	CDAudioCallBack(Bitu len);

	void 	ClearTracks();
	bool	LoadIsoFile(char *filename);
	bool	CanReadPVD(TrackFile *file, uint64_t sectorSize, bool mode2);
	// cue sheet processing
	bool	LoadCueSheet(char *cuefile);
	bool	GetRealFileName(std::string& filename, std::string& pathname);
	bool	GetCueKeyword(std::string &keyword, std::istream &in);
	bool	GetCueFrame(uint64_t &frames, std::istream &in);
	bool	GetCueString(std::string &str, std::istream &in);
	bool	AddTrack(Track &curr, uint64_t &shift, uint64_t prestart, uint64_t &totalPregap, uint64_t currPregap);

	std::vector<Track>	tracks;
typedef	std::vector<Track>::iterator	track_it;
	std::string	mcn;
};

void cdrom_image_log(const char *format, ...);

#endif /* __CDROM_INTERFACE__ */
