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

typedef struct win_cdrom_ioctl_t {
    int                     toc_valid;
    uint8_t                 cur_toc[65536];
    CDROM_READ_TOC_EX       cur_read_toc_ex;
    int                     blocks_num;
    uint8_t                 cur_rti[65536];
    HANDLE                  handle;
    WCHAR                   path[256];
    WCHAR                   old_path[256];
} win_cdrom_ioctl_t;

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
plat_cdrom_open(void *local)
{
    win_cdrom_ioctl_t *ioctl = (win_cdrom_ioctl_t *) local;

    plat_cdrom_close(local);

    ioctl->handle = CreateFileW((LPCWSTR) ioctl->path, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    win_cdrom_ioctl_log("handle=%p, error=%x\n", ioctl->handle, (unsigned int) GetLastError());

    return (ioctl->handle != INVALID_HANDLE_VALUE);
}

static int
plat_cdrom_load(void *local)
{
    win_cdrom_ioctl_t *ioctl = (win_cdrom_ioctl_t *) local;

    plat_cdrom_close(local);

    ioctl->handle = CreateFileW((LPCWSTR) ioctl->path, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    win_cdrom_ioctl_log("handle=%p, error=%x\n", ioctl->handle, (unsigned int) GetLastError());
    if (ioctl->handle != INVALID_HANDLE_VALUE) {
        long size;
        DeviceIoControl(ioctl->handle, IOCTL_STORAGE_LOAD_MEDIA, NULL, 0, NULL, 0, (LPDWORD) &size, NULL);
        return 1;
    }
    return 0;
}

static int
plat_cdrom_read_normal_toc(win_cdrom_ioctl_t *ioctl, uint8_t *toc_buf)
{
    long size = 0;

    memset(toc_buf, 0x00, 65536);
    plat_cdrom_open(ioctl);
    int temp = DeviceIoControl(ioctl->handle, IOCTL_CDROM_READ_TOC, NULL, 0, toc_buf, 65535, (LPDWORD) &size, NULL);
    plat_cdrom_close(ioctl);
#ifdef ENABLE_WIN_CDROM_IOCTL_LOG
    win_cdrom_ioctl_log("temp = %i\n", temp);

    PCDROM_TOC toc = (PCDROM_TOC) toc_buf;
    const int tracks_num = (((toc->Length[0] << 8) | toc->Length[1]) - 2) / 8;
    win_cdrom_ioctl_log("%i tracks\n", tracks_num);
    for (int i = 0; i < tracks_num; i++)
        win_cdrom_ioctl_log("Track %03i: Point %02X\n", i, (int) toc->TrackData[i].TrackNumber);
#endif

    return temp;
}

static void
plat_cdrom_read_raw_toc(win_cdrom_ioctl_t *ioctl)
{
    long                     size         = 0;
    int                      status;
    PCDROM_TOC_FULL_TOC_DATA cur_full_toc = NULL;

    memset(ioctl->cur_rti, 0x00, 65536);
    cur_full_toc = (PCDROM_TOC_FULL_TOC_DATA) calloc(1, 65536);
    if (ioctl->blocks_num != 0) {
        memset(ioctl->cur_rti, 0x00, ioctl->blocks_num * 11);
        ioctl->blocks_num = 0;
    }

    ioctl->cur_read_toc_ex.Format       = CDROM_READ_TOC_EX_FORMAT_FULL_TOC;
    win_cdrom_ioctl_log("cur_read_toc_ex.Format = %i\n", ioctl->cur_read_toc_ex.Format);
    ioctl->cur_read_toc_ex.Msf          = 1;
    ioctl->cur_read_toc_ex.SessionTrack = 1;

    plat_cdrom_open(ioctl);
    status = DeviceIoControl(ioctl->handle, IOCTL_CDROM_READ_TOC_EX, &ioctl->cur_read_toc_ex, 65535,
                             cur_full_toc, 65535, (LPDWORD) &size, NULL);
    plat_cdrom_close(ioctl);
    win_cdrom_ioctl_log("status = %i\n", status);

    if (status != 0) {
        ioctl->blocks_num = (((cur_full_toc->Length[0] << 8) | cur_full_toc->Length[1]) - 2) / 11;
        memcpy(ioctl->cur_rti, cur_full_toc->Descriptors, ioctl->blocks_num * 11);
    }

    free(cur_full_toc);

#ifdef ENABLE_WIN_CDROM_IOCTL_LOG
    win_cdrom_ioctl_log("%i blocks\n", ioctl->blocks_num);

    raw_track_info_t *rti = (raw_track_info_t *) ioctl->cur_rti;
    for (int i = 0; i < ioctl->blocks_num; i++)
         win_cdrom_ioctl_log("Block %03i: Session %03i, Point %02X\n", i, (int) rti[i].session, (int) rti[i].point);
#endif
}

void
plat_cdrom_get_raw_track_info(void *local, int *num, raw_track_info_t *rti)
{
    win_cdrom_ioctl_t *ioctl = (win_cdrom_ioctl_t *) local;

    *num = ioctl->blocks_num;
    memcpy(rti, ioctl->cur_rti, ioctl->blocks_num * 11);
}

static void
plat_cdrom_read_toc(win_cdrom_ioctl_t *ioctl)
{
    if (!ioctl->toc_valid) {
        ioctl->toc_valid = 1;
        (void) plat_cdrom_read_normal_toc(ioctl, ioctl->cur_toc);
        plat_cdrom_read_raw_toc(ioctl);
    }
}

int
plat_cdrom_is_track_audio(void *local, uint32_t sector)
{
    win_cdrom_ioctl_t *ioctl     = (win_cdrom_ioctl_t *) local;
    PCDROM_TOC         toc       = (PCDROM_TOC) ioctl->cur_toc;
    int                control   = 0;
    uint32_t           cur_addr  = 0;
    uint32_t           next_addr = 0;

    plat_cdrom_read_toc(ioctl);

    for (int c = 0; toc->TrackData[c].TrackNumber != 0xaa; c++) {
        PTRACK_DATA cur_td  = &toc->TrackData[c];
        PTRACK_DATA next_td = &toc->TrackData[c + 1];

        cur_addr = MSFtoLBA(cur_td->Address[1], cur_td->Address[2], cur_td->Address[3]) - 150;
        next_addr = MSFtoLBA(next_td->Address[1], next_td->Address[2], next_td->Address[3]) - 150;

        win_cdrom_ioctl_log("F: %i, L: %i, C: %i (%i), c: %02X, A: %08X, S: %08X\n",
                            toc->FirstTrack, toc->LastTrack, cur_td->TrackNumber, c,
                            cur_td->Control, track_addr, sector);

        if ((cur_td->TrackNumber >= toc->FirstTrack) && (cur_td->TrackNumber <= toc->LastTrack) &&
            (sector >= cur_addr) && (sector < next_addr)) {
            control = cur_td->Control;
            break;
        }
    }

    const int ret = !(control & 0x04);

    win_cdrom_ioctl_log("plat_cdrom_is_track_audio(%08X): %i\n", sector, ret);

    return ret;
}

int
plat_cdrom_is_track_pre(void *local, uint32_t sector)
{
    win_cdrom_ioctl_t *ioctl     = (win_cdrom_ioctl_t *) local;
    PCDROM_TOC         toc       = (PCDROM_TOC) ioctl->cur_toc;
    int                control   = 0;
    uint32_t           cur_addr  = 0;
    uint32_t           next_addr = 0;

    plat_cdrom_read_toc(ioctl);

    for (int c = 0; toc->TrackData[c].TrackNumber != 0xaa; c++) {
        PTRACK_DATA cur_td  = &toc->TrackData[c];
        PTRACK_DATA next_td = &toc->TrackData[c + 1];

        cur_addr = MSFtoLBA(cur_td->Address[1], cur_td->Address[2], cur_td->Address[3]) - 150;
        next_addr = MSFtoLBA(next_td->Address[1], next_td->Address[2], next_td->Address[3]) - 150;

        win_cdrom_ioctl_log("F: %i, L: %i, C: %i (%i), c: %02X, A: %08X, S: %08X\n",
                            toc->FirstTrack, toc->LastTrack, cur_td->TrackNumber, c,
                            cur_td->Control, cur_addr, sector);

        if ((cur_td->TrackNumber >= toc->FirstTrack) && (cur_td->TrackNumber <= toc->LastTrack) &&
            (sector >= cur_addr) && (sector < next_addr)) {
            control = cur_td->Control;
            break;
        }
    }

    const int ret = (control & 0x01);

    win_cdrom_ioctl_log("plat_cdrom_is_track_audio(%08X): %i\n", sector, ret);

    return ret;
}

uint32_t
plat_cdrom_get_track_start(void *local, uint32_t sector, uint8_t *attr, uint8_t *track)
{
    win_cdrom_ioctl_t *ioctl     = (win_cdrom_ioctl_t *) local;
    PCDROM_TOC         toc       = (PCDROM_TOC) ioctl->cur_toc;
    uint32_t           cur_addr  = 0;
    uint32_t           next_addr = 0;

    plat_cdrom_read_toc(ioctl);

    for (int c = 0; toc->TrackData[c].TrackNumber != 0xaa; c++) {
        PTRACK_DATA cur_td  = &toc->TrackData[c];
        PTRACK_DATA next_td = &toc->TrackData[c + 1];

        cur_addr = MSFtoLBA(cur_td->Address[1], cur_td->Address[2], cur_td->Address[3]) - 150;
        next_addr = MSFtoLBA(next_td->Address[1], next_td->Address[2], next_td->Address[3]) - 150;

        win_cdrom_ioctl_log("F: %i, L: %i, C: %i (%i), c: %02X, a: %02X, A: %08X, S: %08X\n",
                            toc->FirstTrack, toc->LastTrack, cur_td->TrackNumber, c,
                            cur_td->Control, cur_td->Adr, cur_addr, sector);

        if ((cur_td->TrackNumber >= toc->FirstTrack) && (cur_td->TrackNumber <= toc->LastTrack) &&
            (sector >= cur_addr) && (sector < next_addr)) {
            *track = cur_td->TrackNumber;
            *attr  = cur_td->Control;
            *attr |= ((cur_td->Adr << 4) & 0xf0);
            break;
        }
    }

    win_cdrom_ioctl_log("plat_cdrom_get_track_start(%08X): %i\n", sector, cur_addr);

    return cur_addr;
}

uint32_t
plat_cdrom_get_last_block(void *local)
{
    win_cdrom_ioctl_t *ioctl   = (win_cdrom_ioctl_t *) local;
    PCDROM_TOC         toc     = (PCDROM_TOC) ioctl->cur_toc;
    uint32_t           lb      = 0;
    uint32_t           address = 0;

    plat_cdrom_read_toc(ioctl);

    for (int c = 0; c <= toc->LastTrack; c++) {
        PTRACK_DATA td  = &toc->TrackData[c];

        address = MSFtoLBA(td->Address[1], td->Address[2], td->Address[3]) - 150;

        if (address > lb)
            lb = address;
    }

    win_cdrom_ioctl_log("LBCapacity=%d\n", lb);

    return lb;
}

int
plat_cdrom_ext_medium_changed(void *local)
{
    win_cdrom_ioctl_t *ioctl              = (win_cdrom_ioctl_t *) local;
    PCDROM_TOC         toc                = (PCDROM_TOC) ioctl->cur_toc;
    uint8_t            new_toc_buf[65536] = { 0 };
    PCDROM_TOC         new_toc            = (PCDROM_TOC) new_toc_buf;
    int                ret                = 0;
    int                temp               = plat_cdrom_read_normal_toc(ioctl, new_toc_buf);
    PTRACK_DATA        cur_ltd            = &toc->TrackData[toc->LastTrack];

    if (temp != 0)
        plat_cdrom_read_raw_toc(ioctl);

    PTRACK_DATA        new_ltd            = &new_toc->TrackData[new_toc->LastTrack];

    if (temp == 0)
        /* There has been some kind of error - not a medium change, but a not ready
           condition. */
        ret =  -1;
    else if (!ioctl->toc_valid || (memcmp(ioctl->path, ioctl->old_path, sizeof(ioctl->path)) != 0)) {
        /* Changed to a different host drive - we already detect such medium changes. */
        ioctl->toc_valid = 1;
        memcpy(toc, new_toc, 65535);
        if (memcmp(ioctl->path, ioctl->old_path, sizeof(ioctl->path)) != 0)
            memcpy(ioctl->old_path, ioctl->path, sizeof(ioctl->path));
    } else if (memcmp(&(new_ltd->Address[1]), &(cur_ltd->Address[1]), 3))
        /* The TOC has changed. */
        ret = 1;

    win_cdrom_ioctl_log("plat_cdrom_ext_medium_changed(): %i\n", ret);

    return ret;
}

void
plat_cdrom_get_audio_tracks(void *local, int *st_track, int *end, TMSF *lead_out)
{
    win_cdrom_ioctl_t *ioctl = (win_cdrom_ioctl_t *) local;
    PCDROM_TOC         toc   = (PCDROM_TOC) ioctl->cur_toc;

    plat_cdrom_read_toc(ioctl);

    PTRACK_DATA        ltd   = &toc->TrackData[toc->LastTrack];

    *st_track       = 1;
    *end            = toc->LastTrack;
    lead_out->min   = ltd->Address[1];
    lead_out->sec   = ltd->Address[2];
    lead_out->fr    = ltd->Address[3];

    win_cdrom_ioctl_log("plat_cdrom_get_audio_tracks(): %02i, %02i, %02i:%02i:%02i\n",
                        *st_track, *end, lead_out->min, lead_out->sec, lead_out->fr);
}

/* This replaces both Info and EndInfo, they are specified by a variable. */
int
plat_cdrom_get_audio_track_info(void *local, UNUSED(int end), int track, int *track_num, TMSF *start, uint8_t *attr)
{
    win_cdrom_ioctl_t *ioctl = (win_cdrom_ioctl_t *) local;
    PCDROM_TOC         toc   = (PCDROM_TOC) ioctl->cur_toc;

    plat_cdrom_read_toc(ioctl);

    if ((track < 1) || (track == 0xaa) || (track > (toc->LastTrack + 1))) {
        win_cdrom_ioctl_log("plat_cdrom_get_audio_track_info(%02i)\n", track);
        return 0;
    }

    PTRACK_DATA        td    = &toc->TrackData[track - 1];

    start->min = td->Address[1];
    start->sec = td->Address[2];
    start->fr  = td->Address[3];

    *track_num = td->TrackNumber;
    *attr      = td->Control;
    *attr     |= ((td->Adr << 4) & 0xf0);

    win_cdrom_ioctl_log("plat_cdrom_get_audio_track_info(%02i): %02i:%02i:%02i, %02i, %02X\n",
                        track, start->min, start->sec, start->fr, *track_num, *attr);

    return 1;
}

/* TODO: See if track start is adjusted by 150 or not. */
int
plat_cdrom_get_audio_sub(void *local, UNUSED(uint32_t sector), uint8_t *attr, uint8_t *track, uint8_t *index,
                         TMSF *rel_pos, TMSF *abs_pos)
{
    win_cdrom_ioctl_t *     ioctl = (win_cdrom_ioctl_t *) local;
    CDROM_SUB_Q_DATA_FORMAT insub;
    SUB_Q_CHANNEL_DATA      sub;
    long                    size  = 0;

    insub.Format = IOCTL_CDROM_CURRENT_POSITION;

    plat_cdrom_open(ioctl);
    DeviceIoControl(ioctl->handle, IOCTL_CDROM_READ_Q_CHANNEL, &insub, sizeof(insub), &sub, sizeof(sub),
                    (LPDWORD) &size, NULL);
    plat_cdrom_close(ioctl);

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
                        *track, *attr, *index, rel_pos->min, rel_pos->sec, rel_pos->fr, abs_pos->min, abs_pos->sec,
                        abs_pos->fr);

    return 1;
}

int
plat_cdrom_get_sector_size(void *local, UNUSED(uint32_t sector))
{
    win_cdrom_ioctl_t *     ioctl   = (win_cdrom_ioctl_t *) local;
    long                    size;
    DISK_GEOMETRY           dgCDROM;

    plat_cdrom_open(ioctl);
    DeviceIoControl(ioctl->handle, IOCTL_CDROM_GET_DRIVE_GEOMETRY, NULL, 0, &dgCDROM, sizeof(dgCDROM),
                    (LPDWORD) &size, NULL);
    plat_cdrom_close(ioctl);

    win_cdrom_ioctl_log("BytesPerSector=%d\n", dgCDROM.BytesPerSector);
    return dgCDROM.BytesPerSector;
}

int
plat_cdrom_read_sector(void *local, uint8_t *buffer, int raw, uint32_t sector)
{
    win_cdrom_ioctl_t *     ioctl  = (win_cdrom_ioctl_t *) local;
    int                     status;
    long                    size   = 0;
    int                     buflen = raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE;

    plat_cdrom_open(ioctl);

    if (raw) {
        /* Raw */
        win_cdrom_ioctl_log("Raw\n");
        RAW_READ_INFO in;
        in.DiskOffset.LowPart  = sector * COOKED_SECTOR_SIZE;
        in.DiskOffset.HighPart = 0;
        in.SectorCount         = 1;
        in.TrackMode           = CDDA;
        status = DeviceIoControl(ioctl->handle, IOCTL_CDROM_RAW_READ, &in, sizeof(in),
                                 buffer, buflen, (LPDWORD) &size, NULL);
    } else {
        /* Cooked */
        win_cdrom_ioctl_log("Cooked\n");
        int success  = 0;
        DWORD newPos = SetFilePointer(ioctl->handle, sector * COOKED_SECTOR_SIZE, 0, FILE_BEGIN);
        if (newPos != 0xFFFFFFFF)
            success = ReadFile(ioctl->handle, buffer, buflen, (LPDWORD) &size, NULL);
        status  = (success != 0);
    }
    plat_cdrom_close(ioctl);
    win_cdrom_ioctl_log("ReadSector status=%d, sector=%d, size=%" PRId64 ".\n", status, sector, (long long) size);

    return (status > 0) ? (size == buflen) : -1;
}

void
plat_cdrom_eject(void *local)
{
    win_cdrom_ioctl_t *ioctl = (win_cdrom_ioctl_t *) local;
    long               size;

    plat_cdrom_open(ioctl);
    DeviceIoControl(ioctl->handle, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, (LPDWORD) &size, NULL);
    plat_cdrom_close(ioctl);
}

void
plat_cdrom_close(void *local)
{
    win_cdrom_ioctl_t *ioctl = (win_cdrom_ioctl_t *) local;

    if (ioctl->handle != NULL) {
        CloseHandle(ioctl->handle);
        ioctl->handle = NULL;
    }
}

int
plat_cdrom_set_drive(void *local, const char *drv)
{
    win_cdrom_ioctl_t *ioctl = (win_cdrom_ioctl_t *) local;

    plat_cdrom_close(ioctl);

    memcpy(ioctl->old_path, ioctl->path, sizeof(ioctl->path));
    memset(ioctl->path, 0x00, sizeof(ioctl->path));

    wsprintf(ioctl->path, L"%S", drv);
    win_cdrom_ioctl_log("Path is %S\n", ioctl->path);

    ioctl->toc_valid = 0;

    plat_cdrom_load(ioctl);

    return 1;
}

int
plat_cdrom_get_local_size(void)
{
    return sizeof(win_cdrom_ioctl_t);
}
