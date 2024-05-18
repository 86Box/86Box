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

static const char ioctl_path[8];
static HANDLE    hIOCTL;
static CDROM_TOC toc;

/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#define MSFtoLBA(m, s, f) ((((m * 60) + s) * 75) + f)

static int
plat_cdrom_get_track(uint32_t sector)
{
    int track = 0;
    uint32_t track_addr;

    for (int i = toc.FirstTrack; i < toc.LastTrack; i++) {
        /* There must be at least two tracks - data and lead out. */
        track_addr = MSFtoLBA(toc.TrackData[i].Address[1], toc.TrackData[i].Address[2], toc.TrackData[i].Address[3]);
        if (track_addr <= sector) {
            track = i;
        }
    }

    pclog("GetTrack = %d.\n", track);
    return track;
}

int
plat_cdrom_get_audio_track(uint32_t sector)
{
    int control = 0;
    uint32_t track_addr;

    for (int i = 0; toc.TrackData[i].TrackNumber != 0xaa; i++) {
        /* There must be at least two tracks - data and lead out. */
        track_addr = MSFtoLBA(toc.TrackData[i].Address[1], toc.TrackData[i].Address[2], toc.TrackData[i].Address[3]);
        if ((toc.TrackData[i].TrackNumber >= toc.FirstTrack) && (toc.TrackData[i].TrackNumber <= toc.LastTrack) &&
            (track_addr >= sector)) {
            control = toc.TrackData[i].Control;
            break;
        }
    }

    return (control & 4) ? DATA_TRACK : AUDIO_TRACK;
}

int
plat_cdrom_get_audio_sub(uint32_t sector, uint8_t *attr, uint8_t *track, uint8_t *index, TMSF *rel_pos, TMSF *abs_pos)
{
	CDROM_SUB_Q_DATA_FORMAT insub;
	SUB_Q_CHANNEL_DATA sub;
	DWORD size;
	int pos = 0;
    int cur_track = plat_cdrom_get_track(sector);

    insub.Format = IOCTL_CDROM_CURRENT_POSITION;
    if (plat_cdrom_open())
        return 0;
	DeviceIoControl(hIOCTL,IOCTL_CDROM_READ_Q_CHANNEL, &insub, sizeof(insub), &sub, sizeof(sub), &size, NULL);
    plat_cdrom_exit();

    *attr  = sub.CurrentPosition.Control;
    *track = (uint8_t)(cur_track + 1);
    *index = sub.CurrentPosition.IndexNumber;

    FRAMES_TO_MSF(sector + 150, &abs_pos->min, &abs_pos->sec, &abs_pos->fr);

    /* Absolute position should be adjusted by 150, not the relative ones. */
    FRAMES_TO_MSF(sector - toc.FirstTrack, &rel_pos->min, &rel_pos->sec, &rel_pos->fr);

    return 1;
}

int
plat_cdrom_get_audio_tracks(int *st_track, int *end, TMSF *lead_out)
{
    CDROM_TOC toc;
    DWORD byteCount;

    *st_track = toc.FirstTrack;
    *end      = toc.LastTrack;
    FRAMES_TO_MSF(toc.TrackData[*end].TrackNumber + 150, &lead_out->min, &lead_out->sec, &lead_out->fr);

    return 1;
}

/* This replaces both Info and EndInfo, they are specified by a variable. */
int
plat_cdrom_get_audio_track_info(int end, int track, int *track_num, TMSF *start, uint8_t *attr)
{
    int pos;
    DWORD byteCount;

    pclog("plat_cdrom_get_audio_track_info(): start track = %d, last track = %d.\n", track, end);

    if ((track < 1) || (track > end))
        return 0;

    pos = toc.FirstTrack + 150;

    FRAMES_TO_MSF(pos, &start->min, &start->sec, &start->fr);

    *track_num = toc.TrackData[track - 1].TrackNumber;
    *attr      = toc.TrackData[track - 1].Control;

    return 1;
}

uint32_t
plat_get_sector_size(void)
{
	DISK_GEOMETRY dgCDROM;
    DWORD size;

    if (plat_cdrom_open())
        return 0;
	DeviceIoControl(hIOCTL, IOCTL_CDROM_GET_DRIVE_GEOMETRY, NULL, 0, &dgCDROM, sizeof(DISK_GEOMETRY), &size, NULL);
    plat_cdrom_exit();

	if (dgCDROM.MediaType != 11) // Removable Media Check
        return 0;

	return dgCDROM.BytesPerSector;
}

int
plat_cdrom_read_sector(uint8_t *buffer, int raw, uint32_t sector)
{
    int ret;
    LARGE_INTEGER pos;
    RAW_READ_INFO in;
    DWORD byteCount;
    pclog("plat_cdrom_read_sector(): raw? = %d, sector = %02x.\n", raw, sector);

    if (raw) {
        in.TrackMode = CDDA;
        in.SectorCount = 1;
        in.DiskOffset.QuadPart = sector * RAW_SECTOR_SIZE;
        if (plat_cdrom_open())
            return 0;
        ret = DeviceIoControl(hIOCTL, IOCTL_CDROM_RAW_READ, &in, sizeof(RAW_READ_INFO), buffer, RAW_SECTOR_SIZE, &byteCount, NULL);
        plat_cdrom_exit();
        return ret;
    } else {
        pos.QuadPart = sector * COOKED_SECTOR_SIZE;
        if (plat_cdrom_open())
            return 0;
        SetFilePointer(hIOCTL, pos.LowPart, &pos.HighPart, FILE_BEGIN);
        ret = ReadFile(hIOCTL, buffer, COOKED_SECTOR_SIZE, &byteCount, NULL);
        plat_cdrom_exit();
        pclog("plat_cdrom_read_sector(): ret = %x.\n", !ret);
        return !ret;
    }
    return 0;
}

uint32_t
plat_cdrom_get_capacity(void)
{
    DWORD size;
    int c;
    DISK_GEOMETRY dgCDROM;
    uint32_t totals;

    if (plat_cdrom_open())
        return 0;
	DeviceIoControl(hIOCTL, IOCTL_CDROM_GET_DRIVE_GEOMETRY, NULL, 0, &dgCDROM, sizeof(DISK_GEOMETRY), &size, NULL);
    plat_cdrom_exit();

    totals = dgCDROM.SectorsPerTrack * dgCDROM.TracksPerCylinder * dgCDROM.Cylinders.QuadPart;

    pclog("Total = %08x.\n", totals);
	return totals;
}

int
plat_cdrom_load(void)
{
    int ret;
    DWORD size;

    if (plat_cdrom_open())
        return 0;
    DeviceIoControl(hIOCTL, IOCTL_STORAGE_LOAD_MEDIA, NULL, 0, NULL, 0, &size, NULL);
    plat_cdrom_exit();
    return 1;
}

int
plat_cdrom_eject(void)
{
    int ret;
    DWORD size;

    if (plat_cdrom_open())
        return 0;
    ret = DeviceIoControl(hIOCTL, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &size, NULL);
    plat_cdrom_exit();
    return ret;
}

void
plat_cdrom_exit(void)
{
    if (hIOCTL) {
        CloseHandle(hIOCTL);
        hIOCTL = NULL;
    }
}

void
plat_cdrom_close(void)
{
    plat_cdrom_exit();
}

int
plat_cdrom_reset(void)
{
    CDROM_TOC ltoc;
    DWORD size;

    if (plat_cdrom_open())
        return 0;
    DeviceIoControl(hIOCTL, IOCTL_CDROM_READ_TOC, NULL, 0, &ltoc, sizeof(ltoc), &size, NULL);
    plat_cdrom_exit();

    toc = ltoc;
    return 1;
}

int
plat_cdrom_open(void)
{
    plat_cdrom_exit();
    hIOCTL = CreateFile((LPCWSTR)ioctl_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hIOCTL == NULL)
        return 1;

    return 0;
}
