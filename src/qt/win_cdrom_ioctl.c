/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Win32 CD-ROM support via IOCTL.
 *
 *
 *
 * Authors: TheCollector1995, <mariogplayer@gmail.com>,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023 TheCollector1995.
 *          Copyright 2023 Miran Grca.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP
#include <io.h>
#include "ntddcdrm.h"
#include "ntddscsi.h"
#include <stdint.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/plat_unused.h>
#include <86box/plat_cdrom.h>

/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#define MSFtoLBA(m, s, f) ((((m * 60) + s) * 75) + f)

static HANDLE handle;
static WCHAR ioctl_path[256];

static int
plat_cdrom_open(void)
{
    plat_cdrom_close();

    handle = CreateFileW((LPCWSTR)ioctl_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    pclog("handle=%p, error=%x.\n", handle, GetLastError());
    if (handle != INVALID_HANDLE_VALUE) {
        long size;
        DeviceIoControl(handle, IOCTL_STORAGE_LOAD_MEDIA, NULL, 0, NULL, 0, (LPDWORD)&size, NULL);
        return 1;
    }
    return 0;
}

int
plat_cdrom_is_track_audio(uint32_t sector)
{
	CDROM_TOC toc;
	long size = 0;
    int ret;
    int control = 0;
    uint32_t track_addr = 0;

    plat_cdrom_open();
    DeviceIoControl(handle, IOCTL_CDROM_READ_TOC, NULL, 0, &toc, sizeof(toc), (LPDWORD)&size, NULL);
    plat_cdrom_close();

    for (int c = 0; toc.TrackData[c].TrackNumber != 0xaa; c++) {
        track_addr = MSFtoLBA(toc.TrackData[c].Address[1], toc.TrackData[c].Address[2], toc.TrackData[c].Address[3]);
        if ((toc.TrackData[c].TrackNumber >= toc.FirstTrack) && (toc.TrackData[c].TrackNumber <= toc.LastTrack) &&
            (track_addr >= sector))
            control = toc.TrackData[c].Control;
            break;
    }
    ret = (control & 0x04) ? 0 : 1;
    return ret;
}

int
plat_cdrom_get_last_block(void)
{
	CDROM_TOC   toc;
	int         lb = 0;
	long        size = 0;
	uint32_t    address = 0;

	plat_cdrom_open();
    DeviceIoControl(handle, IOCTL_CDROM_READ_TOC, NULL, 0, &toc, sizeof(toc), (LPDWORD)&size, NULL);
    plat_cdrom_close();

    for (int c = 0; c <= toc.LastTrack; c++) {
        address = MSFtoLBA(toc.TrackData[c].Address[1], toc.TrackData[c].Address[2], toc.TrackData[c].Address[3]);
        if (address > lb)
            lb = address;
    }
    pclog("LBCapacity=%d.\n", lb);
    return lb;
}

void
plat_cdrom_get_audio_tracks(int *st_track, int *end, TMSF *lead_out)
{
	CDROM_TOC toc;
	long size       = 0;

	plat_cdrom_open();
    DeviceIoControl(handle, IOCTL_CDROM_READ_TOC, NULL, 0, &toc, sizeof(toc), (LPDWORD)&size, NULL);
    plat_cdrom_close();

    *st_track       = 1;
    *end            = toc.LastTrack;
    lead_out->min   = toc.TrackData[toc.LastTrack].Address[1];
    lead_out->sec   = toc.TrackData[toc.LastTrack].Address[2];
    lead_out->fr    = toc.TrackData[toc.LastTrack].Address[3];
}

/* This replaces both Info and EndInfo, they are specified by a variable. */
int
plat_cdrom_get_audio_track_info(UNUSED(int end), int track, int *track_num, TMSF *start, uint8_t *attr)
{
	CDROM_TOC   toc;
	long size = 0;

	plat_cdrom_open();
    DeviceIoControl(handle, IOCTL_CDROM_READ_TOC, NULL, 0, &toc, sizeof(toc), (LPDWORD)&size, NULL);
    plat_cdrom_close();

    if ((track < 1) || (track == 0xaa) || (track > (toc.LastTrack)))
        return 0;

    start->min = toc.TrackData[track - 1].Address[1];
    start->sec = toc.TrackData[track - 1].Address[2];
    start->fr  = toc.TrackData[track - 1].Address[3];

    *track_num = toc.TrackData[track - 1].TrackNumber;
    *attr      = toc.TrackData[track - 1].Control;

    return 1;
}

/* TODO: See if track start is adjusted by 150 or not. */
int
plat_cdrom_get_audio_sub(UNUSED(uint32_t sector), uint8_t *attr, uint8_t *track, uint8_t *index, TMSF *rel_pos, TMSF *abs_pos)
{
	CDROM_SUB_Q_DATA_FORMAT insub;
	SUB_Q_CHANNEL_DATA sub;
	long size = 0;

    insub.Format = IOCTL_CDROM_CURRENT_POSITION;

	plat_cdrom_open();
	DeviceIoControl(handle, IOCTL_CDROM_READ_Q_CHANNEL, &insub, sizeof(insub), &sub, sizeof(sub), (LPDWORD)&size, NULL);
    plat_cdrom_close();

    if (sub.CurrentPosition.TrackNumber < 1)
        return 0;

    *track = sub.CurrentPosition.TrackNumber;
    *attr = sub.CurrentPosition.Control;
    *index = sub.CurrentPosition.IndexNumber;

	rel_pos->min = sub.CurrentPosition.TrackRelativeAddress[1];
	rel_pos->sec = sub.CurrentPosition.TrackRelativeAddress[2];
	rel_pos->fr = sub.CurrentPosition.TrackRelativeAddress[3];
	abs_pos->min = sub.CurrentPosition.AbsoluteAddress[1];
	abs_pos->sec = sub.CurrentPosition.AbsoluteAddress[2];
	abs_pos->fr = sub.CurrentPosition.AbsoluteAddress[3];

    return 1;
}

int
plat_cdrom_get_sector_size(UNUSED(uint32_t sector))
{
    long size;
    DISK_GEOMETRY dgCDROM;

    plat_cdrom_open();
	DeviceIoControl(handle, IOCTL_CDROM_GET_DRIVE_GEOMETRY, NULL, 0, &dgCDROM, sizeof(dgCDROM), (LPDWORD)&size, NULL);
	plat_cdrom_close();

	pclog("BytesPerSector=%d.\n", dgCDROM.BytesPerSector);
    return dgCDROM.BytesPerSector;
}

int
plat_cdrom_read_sector(uint8_t *buffer, int raw, uint32_t sector)
{
    LARGE_INTEGER pos;
    BOOL status;
    long size = 0;

	int	buflen = raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE;

	plat_cdrom_open();
	if (!raw) {
        pclog("Cooked.\n");
		// Cooked
		int success = 0;
		DWORD newPos = SetFilePointer(handle, sector * COOKED_SECTOR_SIZE, 0, FILE_BEGIN);
		if (newPos != 0xFFFFFFFF)
            success = ReadFile(handle, buffer, buflen, (LPDWORD)&size, NULL);
		status = (success != 0);
	} else {
	    pclog("Raw.\n");
		// Raw
		RAW_READ_INFO in;
		in.DiskOffset.LowPart	= sector * COOKED_SECTOR_SIZE;
		in.DiskOffset.HighPart	= 0;
		in.SectorCount			= 1;
		in.TrackMode			= CDDA;
		status = DeviceIoControl(handle, IOCTL_CDROM_RAW_READ, &in, sizeof(in),
								buffer, buflen, (LPDWORD)&size, NULL);
	}
    plat_cdrom_close();
	pclog("ReadSector status=%d, sector=%d, size=%d.\n", status, sector, size);
	return (size == buflen) && (status > 0);
}

void
plat_cdrom_eject(void)
{
	long size;
    int ret;

    plat_cdrom_open();
    DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, (LPDWORD)&size, NULL);
    plat_cdrom_close();
}

void
plat_cdrom_close(void)
{
    if (handle != NULL) {
        CloseHandle(handle);
        handle = NULL;
    }
}

int
plat_cdrom_set_drive(int drive)
{
    plat_cdrom_close();

    wsprintf(ioctl_path, L"\\\\.\\%c:", drive);
    pclog("Path is %s\n", ioctl_path);

    plat_cdrom_open();
    return 1;
}
