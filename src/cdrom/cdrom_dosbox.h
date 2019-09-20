/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the CD-ROM image file handling module.
 *
 * Version:	@(#)cdrom_dosbox.h	1.0.3	2019/03/05
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
#ifndef CDROM_INTERFACE
# define CDROM_INTERFACE

#include <stdint.h>
#include <string.h>
#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>

//typedef signed int Bits;
//typedef unsigned int Bitu;
//typedef int8_t   Bit8s;
//typedef int16_t  Bit16s;
//typedef uint16_t Bit16u;
//typedef int32_t  Bit32s;
//typedef uint32_t Bit32u;

typedef size_t PhysPt;


#define RAW_SECTOR_SIZE		2352
#define COOKED_SECTOR_SIZE	2048

#define DATA_TRACK 0x14
#define AUDIO_TRACK 0x10

#define CD_FPS  75
#define FRAMES_TO_MSF(f, M,S,F) {                                       \
        uint64_t value = f;                                             \
        *(F) = (value%CD_FPS) & 0xff;                                   \
        value /= CD_FPS;                                                \
        *(S) = (value%60) & 0xff;                                       \
        value /= 60;                                                    \
        *(M) = value & 0xff;                                            \
}
#define MSF_TO_FRAMES(M, S, F)  ((M)*60*CD_FPS+(S)*CD_FPS+(F))


typedef struct SMSF {
    uint8_t	min;
    uint8_t	sec;
    uint8_t	fr;
} TMSF;

typedef struct SCtrl {
    uint8_t	out[4];			// output channel
    uint8_t	vol[4];			// channel volume
} TCtrl;


class CDROM_Interface {
public:
//  CDROM_Interface(void);

    virtual ~CDROM_Interface(void) {};

    virtual bool	SetDevice(const wchar_t *path, int forceCD) = 0;

    virtual bool	GetUPC(uint8_t& attr, char* upc) = 0;

    virtual bool	GetAudioTracks(int& stTrack, int& end, TMSF& leadOut) = 0;
    virtual bool	GetAudioTrackInfo(int track, int& number, TMSF& start, uint8_t& attr) = 0;
    virtual bool	GetAudioTrackEndInfo(int track, int& number, TMSF& start, unsigned char& attr) = 0;
    virtual bool	GetAudioSub(int sector, uint8_t& attr, uint8_t& track, uint8_t& index, TMSF& relPos, TMSF& absPos) = 0;
    virtual bool	GetMediaTrayStatus(bool& mediaPresent, bool& mediaChanged, bool& trayOpen) = 0;

    virtual bool	ReadSectors(PhysPt buffer, bool raw, uint32_t sector, uint32_t num) = 0;

    virtual bool	LoadUnloadMedia(bool unload) = 0;

    virtual void	InitNewMedia(void) {};
};


class CDROM_Interface_Image : public CDROM_Interface {
private:
    class TrackFile {
	public:
		virtual bool read(uint8_t *buffer, uint64_t seek, size_t count) = 0;
		virtual uint64_t getLength() = 0;
		virtual ~TrackFile() { };
    };
	
    class BinaryFile : public TrackFile {
	public:
		BinaryFile(const wchar_t *filename, bool &error);
		~BinaryFile();
		bool read(uint8_t *buffer, uint64_t seek, size_t count);
		uint64_t getLength();
	private:
		BinaryFile();
		wchar_t fn[260];
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
	int sectorSize;
	bool mode2;
	TrackFile *file;
    };

public:
    CDROM_Interface_Image();
    virtual ~CDROM_Interface_Image(void);
    void	InitNewMedia(void);
    bool	SetDevice(const wchar_t* path, int forceCD);
    bool	GetUPC(uint8_t& attr, char* upc);
    bool	GetAudioTracks(int& stTrack, int& end, TMSF& leadOut);
    bool	GetAudioTrackInfo(int track, int& number, TMSF& start, uint8_t& attr);
    bool	GetAudioTrackEndInfo(int track, int& number, TMSF& start, unsigned char& attr);
    bool	GetAudioSub(int sector, uint8_t& attr, uint8_t& track, uint8_t& index, TMSF& relPos, TMSF& absPos);
    bool	GetMediaTrayStatus(bool& mediaPresent, bool& mediaChanged, bool& trayOpen);
    bool	ReadSectors(PhysPt buffer, bool raw, uint32_t sector, uint32_t num);
    bool	LoadUnloadMedia(bool unload);
    bool	ReadSector(uint8_t *buffer, bool raw, uint32_t sector);
    bool	ReadSectorSub(uint8_t *buffer, uint32_t sector);
    int		GetSectorSize(uint32_t sector);
    bool	IsMode2(uint32_t sector);
    int		GetMode2Form(uint32_t sector);
    bool	HasDataTrack(void);
    bool	HasAudioTracks(void);
	
    int		GetTrack(unsigned int sector);

private:
    // player
    static	void	CDAudioCallBack(unsigned int len);

    void 	ClearTracks();
    bool	IsoLoadFile(const wchar_t *filename);
    bool	CanReadPVD(TrackFile *file, uint64_t sectorSize, bool mode2);

    // cue sheet processing
    bool	CueGetBuffer(char *str, char **line, bool up);
    bool	CueGetString(std::string &str, char **line);
    bool	CueGetKeyword(std::string &keyword, char **line);
    uint64_t	CueGetNumber(char **line);
    bool	CueGetFrame(uint64_t &frames, char **line);
    bool	CueLoadSheet(const wchar_t *cuefile);
    bool	AddTrack(Track &curr, uint64_t &shift, uint64_t prestart, uint64_t &totalPregap, uint64_t currPregap);

    std::vector<Track>	tracks;
typedef	std::vector<Track>::iterator	track_it;
    std::string	mcn;
};


extern int	CDROM_GetMountType(char* path, int force);

extern void	cdrom_image_log(const char *format, ...);


#endif /* __CDROM_INTERFACE__ */
