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
#include <inttypes.h>
#include <io.h>
#include "ntddcdrm.h"
#include "ntddscsi.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/plat_unused.h>
#include <86box/plat_cdrom.h>

/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#define MSFtoLBA(m, s, f) ((((m * 60) + s) * 75) + f)

static int toc_valid             = 0;
static CDROM_TOC cur_toc         = { 0 };
static HANDLE handle             = NULL;
static WCHAR ioctl_path[256]     = { 0 };
static WCHAR old_ioctl_path[256] = { 0 };

#ifdef ENABLE_WIN_CDROM_IOCTL_LOG
int win_cdrom_ioctl_do_log = ENABLE_WIN_CDROM_IOCTL_LOG;

void
win_cdrom_ioctl_log(const char *fmt, ...)
{
    va_list ap;

    if (win_cdrom_ioctl_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define win_cdrom_ioctl_log(fmt, ...)
#endif

static int
plat_cdrom_open(void)
{
    plat_cdrom_close();

    handle = CreateFileW((LPCWSTR)ioctl_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    win_cdrom_ioctl_log("handle=%p, error=%x\n", handle, (unsigned int) GetLastError());

    return (handle != INVALID_HANDLE_VALUE);
}

static int
plat_cdrom_load(void)
{
    plat_cdrom_close();

    handle = CreateFileW((LPCWSTR)ioctl_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    win_cdrom_ioctl_log("handle=%p, error=%x\n", handle, (unsigned int) GetLastError());
    if (handle != INVALID_HANDLE_VALUE) {
        long size;
        DeviceIoControl(handle, IOCTL_STORAGE_LOAD_MEDIA, NULL, 0, NULL, 0, (LPDWORD)&size, NULL);
        return 1;
    }
    return 0;
}

static void
plat_cdrom_read_toc(void)
{
    long      size            = 0;

    if (!toc_valid) {
        toc_valid = 1;
        plat_cdrom_open();
        DeviceIoControl(handle, IOCTL_CDROM_READ_TOC, NULL, 0, &cur_toc, sizeof(cur_toc), (LPDWORD)&size, NULL);
        plat_cdrom_close();
    }
}

int
plat_cdrom_is_track_audio(uint32_t sector)
{
    int       control         = 0;
    uint32_t  track_addr      = 0;
    uint32_t  next_track_addr = 0;

    plat_cdrom_read_toc();

    for (int c = 0; cur_toc.TrackData[c].TrackNumber != 0xaa; c++) {
        track_addr = MSFtoLBA(cur_toc.TrackData[c].Address[1], cur_toc.TrackData[c].Address[2], cur_toc.TrackData[c].Address[3]) - 150;
        next_track_addr = MSFtoLBA(cur_toc.TrackData[c + 1].Address[1], cur_toc.TrackData[c + 1].Address[2], cur_toc.TrackData[c + 1].Address[3]) - 150;
        win_cdrom_ioctl_log("F: %i, L: %i, C: %i (%i), c: %02X, A: %08X, S: %08X\n",
                            cur_toc.FirstTrack, cur_toc.LastTrack,
                            cur_toc.TrackData[c].TrackNumber, c,
                            cur_toc.TrackData[c].Control, track_addr, sector);
        if ((cur_toc.TrackData[c].TrackNumber >= cur_toc.FirstTrack) && (cur_toc.TrackData[c].TrackNumber <= cur_toc.LastTrack) &&
            (sector >= track_addr) && (sector < next_track_addr)) {
            control = cur_toc.TrackData[c].Control;
            break;
        }
    }

    const int ret = !(control & 0x04);

    win_cdrom_ioctl_log("plat_cdrom_is_track_audio(%08X): %i\n", sector, ret);

    return ret;
}

int
plat_cdrom_is_track_pre(uint32_t sector)
{
    int       control         = 0;
    uint32_t  track_addr      = 0;
    uint32_t  next_track_addr = 0;

    plat_cdrom_read_toc();

    for (int c = 0; cur_toc.TrackData[c].TrackNumber != 0xaa; c++) {
        track_addr = MSFtoLBA(cur_toc.TrackData[c].Address[1], cur_toc.TrackData[c].Address[2], cur_toc.TrackData[c].Address[3]) - 150;
        next_track_addr = MSFtoLBA(cur_toc.TrackData[c + 1].Address[1], cur_toc.TrackData[c + 1].Address[2], cur_toc.TrackData[c + 1].Address[3]) - 150;
        win_cdrom_ioctl_log("F: %i, L: %i, C: %i (%i), c: %02X, A: %08X, S: %08X\n",
                            cur_toc.FirstTrack, cur_toc.LastTrack,
                            cur_toc.TrackData[c].TrackNumber, c,
                            cur_toc.TrackData[c].Control, track_addr, sector);
        if ((cur_toc.TrackData[c].TrackNumber >= cur_toc.FirstTrack) && (cur_toc.TrackData[c].TrackNumber <= cur_toc.LastTrack) &&
            (sector >= track_addr) && (sector < next_track_addr)) {
            control = cur_toc.TrackData[c].Control;
            break;
            }
    }

    const int ret = (control & 0x01);

    win_cdrom_ioctl_log("plat_cdrom_is_track_audio(%08X): %i\n", sector, ret);

    return ret;
}

uint32_t
plat_cdrom_get_track_start(uint32_t sector, uint8_t *attr, uint8_t *track)
{
    uint32_t  track_addr      = 0;
    uint32_t  next_track_addr = 0;

    plat_cdrom_read_toc();

    for (int c = 0; cur_toc.TrackData[c].TrackNumber != 0xaa; c++) {
        track_addr = MSFtoLBA(cur_toc.TrackData[c].Address[1], cur_toc.TrackData[c].Address[2], cur_toc.TrackData[c].Address[3]) - 150;
        next_track_addr = MSFtoLBA(cur_toc.TrackData[c + 1].Address[1], cur_toc.TrackData[c + 1].Address[2], cur_toc.TrackData[c + 1].Address[3]) - 150;
        win_cdrom_ioctl_log("F: %i, L: %i, C: %i (%i), c: %02X, a: %02X, A: %08X, S: %08X\n",
                            cur_toc.FirstTrack, cur_toc.LastTrack,
                            cur_toc.TrackData[c].TrackNumber, c,
                            cur_toc.TrackData[c].Control, cur_toc.TrackData[c].Adr,
                            track_addr, sector);
        if ((cur_toc.TrackData[c].TrackNumber >= cur_toc.FirstTrack) && (cur_toc.TrackData[c].TrackNumber <= cur_toc.LastTrack) &&
            (sector >= track_addr) && (sector < next_track_addr)) {
            *track = cur_toc.TrackData[c].TrackNumber;
            *attr  = cur_toc.TrackData[c].Control;
            *attr |= ((cur_toc.TrackData[c].Adr << 4) & 0xf0);
            break;
        }
    }

    win_cdrom_ioctl_log("plat_cdrom_get_track_start(%08X): %i\n", sector, track_addr);

    return track_addr;
}

uint32_t
plat_cdrom_get_last_block(void)
{
    uint32_t  lb      = 0;
    uint32_t  address = 0;

    plat_cdrom_read_toc();

    for (int c = 0; c <= cur_toc.LastTrack; c++) {
        address = MSFtoLBA(cur_toc.TrackData[c].Address[1], cur_toc.TrackData[c].Address[2], cur_toc.TrackData[c].Address[3]) - 150;
        if (address > lb)
            lb = address;
    }
    win_cdrom_ioctl_log("LBCapacity=%d\n", lb);
    return lb;
}

int
plat_cdrom_ext_medium_changed(void)
{
    long      size;
    CDROM_TOC toc;
    int       ret  = 0;

    plat_cdrom_open();
    int temp = DeviceIoControl(handle, IOCTL_CDROM_READ_TOC,
                               NULL, 0, &toc, sizeof(toc),
                               (LPDWORD)&size, NULL);
    plat_cdrom_close();

    if (!temp)
        /* There has been some kind of error - not a medium change, but a not ready
           condition. */
        ret =  -1;
    else if (!toc_valid || (memcmp(ioctl_path, old_ioctl_path, sizeof(ioctl_path)) != 0)) {
        /* Changed to a different host drive - we already detect such medium changes. */
        toc_valid = 1;
        cur_toc = toc;
        if (memcmp(ioctl_path, old_ioctl_path, sizeof(ioctl_path)) != 0)
            memcpy(old_ioctl_path, ioctl_path, sizeof(ioctl_path));
    } else if ((toc.TrackData[toc.LastTrack].Address[1] !=
                cur_toc.TrackData[cur_toc.LastTrack].Address[1]) ||
               (toc.TrackData[toc.LastTrack].Address[2] !=
                cur_toc.TrackData[cur_toc.LastTrack].Address[2]) ||
               (toc.TrackData[toc.LastTrack].Address[3] !=
                cur_toc.TrackData[cur_toc.LastTrack].Address[3]))
        /* The TOC has changed. */
        ret = 1;

    win_cdrom_ioctl_log("plat_cdrom_ext_medium_changed(): %i\n", ret);

    return ret;
}

void
plat_cdrom_get_audio_tracks(int *st_track, int *end, TMSF *lead_out)
{
    plat_cdrom_read_toc();

    *st_track       = 1;
    *end            = cur_toc.LastTrack;
    lead_out->min   = cur_toc.TrackData[cur_toc.LastTrack].Address[1];
    lead_out->sec   = cur_toc.TrackData[cur_toc.LastTrack].Address[2];
    lead_out->fr    = cur_toc.TrackData[cur_toc.LastTrack].Address[3];

    win_cdrom_ioctl_log("plat_cdrom_get_audio_tracks(): %02i, %02i, %02i:%02i:%02i\n",
                        *st_track, *end, lead_out->min, lead_out->sec, lead_out->fr);
}

/* This replaces both Info and EndInfo, they are specified by a variable. */
int
plat_cdrom_get_audio_track_info(UNUSED(int end), int track, int *track_num, TMSF *start, uint8_t *attr)
{
    plat_cdrom_read_toc();

    if ((track < 1) || (track == 0xaa) || (track > (cur_toc.LastTrack + 1))) {
        win_cdrom_ioctl_log("plat_cdrom_get_audio_track_info(%02i)\n", track);
        return 0;
    }

    start->min = cur_toc.TrackData[track - 1].Address[1];
    start->sec = cur_toc.TrackData[track - 1].Address[2];
    start->fr  = cur_toc.TrackData[track - 1].Address[3];

    *track_num = cur_toc.TrackData[track - 1].TrackNumber;
    *attr      = cur_toc.TrackData[track - 1].Control;
    *attr     |= ((cur_toc.TrackData[track - 1].Adr << 4) & 0xf0);

    win_cdrom_ioctl_log("plat_cdrom_get_audio_track_info(%02i): %02i:%02i:%02i, %02i, %02X\n",
                        track, start->min, start->sec, start->fr, *track_num, *attr);

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
    *attr |= ((sub.CurrentPosition.ADR << 4) & 0xf0);
    *index = sub.CurrentPosition.IndexNumber;

    rel_pos->min = sub.CurrentPosition.TrackRelativeAddress[1];
    rel_pos->sec = sub.CurrentPosition.TrackRelativeAddress[2];
    rel_pos->fr = sub.CurrentPosition.TrackRelativeAddress[3];
    abs_pos->min = sub.CurrentPosition.AbsoluteAddress[1];
    abs_pos->sec = sub.CurrentPosition.AbsoluteAddress[2];
    abs_pos->fr = sub.CurrentPosition.AbsoluteAddress[3];

    win_cdrom_ioctl_log("plat_cdrom_get_audio_sub(): %02i, %02X, %02i, %02i:%02i:%02i, %02i:%02i:%02i\n",
                        *track, *attr, *index, rel_pos->min, rel_pos->sec, rel_pos->fr, abs_pos->min, abs_pos->sec, abs_pos->fr);

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

    win_cdrom_ioctl_log("BytesPerSector=%d\n", dgCDROM.BytesPerSector);
    return dgCDROM.BytesPerSector;
}

int
plat_cdrom_read_sector(uint8_t *buffer, int raw, uint32_t sector)
{
    BOOL status;
    long size   = 0;
    int  buflen = raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE;

    plat_cdrom_open();

    if (raw) {
        win_cdrom_ioctl_log("Raw\n");
        /* Raw */
        RAW_READ_INFO in;
        in.DiskOffset.LowPart  = sector * COOKED_SECTOR_SIZE;
        in.DiskOffset.HighPart = 0;
        in.SectorCount         = 1;
        in.TrackMode           = CDDA;
        status = DeviceIoControl(handle, IOCTL_CDROM_RAW_READ, &in, sizeof(in),
                                 buffer, buflen, (LPDWORD)&size, NULL);
    } else {
        win_cdrom_ioctl_log("Cooked\n");
        /* Cooked */
        int success  = 0;
        DWORD newPos = SetFilePointer(handle, sector * COOKED_SECTOR_SIZE, 0, FILE_BEGIN);
        if (newPos != 0xFFFFFFFF)
            success = ReadFile(handle, buffer, buflen, (LPDWORD)&size, NULL);
        status  = (success != 0);
    }
    plat_cdrom_close();
    win_cdrom_ioctl_log("ReadSector status=%d, sector=%d, size=%" PRId64 ".\n", status, sector, (long long) size);

    return (size == buflen) && (status > 0);
}

void
plat_cdrom_eject(void)
{
    long size;

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
plat_cdrom_set_drive(const char *drv)
{
    plat_cdrom_close();

    memcpy(old_ioctl_path, ioctl_path, sizeof(ioctl_path));
    memset(ioctl_path, 0x00, sizeof(ioctl_path));

    wsprintf(ioctl_path, L"%S", drv);
    win_cdrom_ioctl_log("Path is %S\n", ioctl_path);

    toc_valid = 0;

    plat_cdrom_load();
    return 1;
}
