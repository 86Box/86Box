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
#undef BITMAP
#include <inttypes.h>
#include "ntddcdrm.h"
#include "ntddscsi.h"
#ifdef ENABLE_IOCTL_LOG
#include <stdarg.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/log.h>
#include <86box/plat_cdrom_ioctl.h>
#include <86box/scsi_device.h>

typedef struct ioctl_t {
    cdrom_t                *dev;
    void                   *log;
    int                     is_dvd;
    int                     has_audio;
    int32_t                 tracks_num;
    uint8_t                 cur_toc[65536];
    CDROM_READ_TOC_EX       cur_read_toc_ex;
    int                     blocks_num;
    uint8_t                 cur_rti[65536];
    HANDLE                  handle;
    WCHAR                   path[256];
} ioctl_t;

static int ioctl_read_dvd_structure(const void *local, uint8_t layer, uint8_t format,
                                    uint8_t *buffer, uint32_t *info);

#ifdef ENABLE_IOCTL_LOG
int ioctl_do_log = ENABLE_IOCTL_LOG;

void
ioctl_log(void *priv, const char *fmt, ...)
{
    if (ioctl_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ioctl_log(priv, fmt, ...)
#endif

/* Internal functions. */
static void
ioctl_close_handle(const ioctl_t *ioctl)
{
    if (ioctl->handle != NULL)
        CloseHandle(ioctl->handle);
}

static int
ioctl_open_handle(ioctl_t *ioctl)
{
    ioctl_log(ioctl->log, "ioctl->path = \"%ls\"\n", ioctl->path);
    ioctl->handle = CreateFileW((LPCWSTR) ioctl->path, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING, 0, NULL);

    ioctl_log(ioctl->log, "handle=%p, error=%x\n",
              ioctl->handle, (unsigned int) GetLastError());

    return (ioctl->handle != INVALID_HANDLE_VALUE);
}

static int
ioctl_read_normal_toc(ioctl_t *ioctl, uint8_t *toc_buf)
{
    long                     size         = 0;
    PCDROM_TOC_FULL_TOC_DATA cur_full_toc = NULL;

    ioctl->tracks_num = 0;
    memset(toc_buf, 0x00, 65536);

    cur_full_toc = (PCDROM_TOC_FULL_TOC_DATA) calloc(1, 65536);

    ioctl->cur_read_toc_ex.Format       = CDROM_READ_TOC_EX_FORMAT_TOC;
    ioctl_log(ioctl->log, "cur_read_toc_ex.Format = %i\n", ioctl->cur_read_toc_ex.Format);
    ioctl->cur_read_toc_ex.Msf          = 1;
    ioctl->cur_read_toc_ex.SessionTrack = 1;

    ioctl_open_handle(ioctl);
    const int temp = DeviceIoControl(ioctl->handle, IOCTL_CDROM_READ_TOC_EX,
                                     &ioctl->cur_read_toc_ex, 65535,
                               cur_full_toc, 65535,
                                     (LPDWORD) &size, NULL);
    ioctl_close_handle(ioctl);
    ioctl_log(ioctl->log, "temp = %i\n", temp);

    if (temp != 0) {
        const int length = ((cur_full_toc->Length[0] << 8) | cur_full_toc->Length[1]) + 2;
        memcpy(toc_buf, cur_full_toc, length);
        ioctl->tracks_num = (length - 4) / 8;
    }

    free(cur_full_toc);

#ifdef ENABLE_IOCTL_LOG
    PCDROM_TOC toc = (PCDROM_TOC) toc_buf;

    ioctl_log(ioctl->log, "%i tracks: %02X %02X %02X %02X\n",
              ioctl->tracks_num, toc_buf[0], toc_buf[1], toc_buf[2], toc_buf[3]);

    for (int i = 0; i < ioctl->tracks_num; i++) {
        const uint8_t *t  = (const uint8_t *) &toc->TrackData[i];
        ioctl_log(ioctl->log, "Track %03i: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  i, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7]);
    }
#endif

    return temp;
}

static void
ioctl_read_raw_toc(ioctl_t *ioctl)
{
    PCDROM_TOC_FULL_TOC_DATA  cur_full_toc = NULL;
    long                      size   = 0;
    raw_track_info_t         *rti    = (raw_track_info_t *) ioctl->cur_rti;
    uint8_t                  *buffer = (uint8_t *) calloc (1, 2052);

    ioctl->is_dvd = (ioctl_read_dvd_structure(ioctl, 0, 0, buffer, NULL) > 0);
    free(buffer);

    ioctl->has_audio  = 0;
    ioctl->blocks_num = 0;
    memset(ioctl->cur_rti, 0x00, 65536);

    cur_full_toc = (PCDROM_TOC_FULL_TOC_DATA) calloc(1, 65536);

    ioctl->cur_read_toc_ex.Format       = CDROM_READ_TOC_EX_FORMAT_FULL_TOC;
    ioctl_log(ioctl->log, "cur_read_toc_ex.Format = %i\n", ioctl->cur_read_toc_ex.Format);
    ioctl->cur_read_toc_ex.Msf          = 1;
    ioctl->cur_read_toc_ex.SessionTrack = 1;

    ioctl_open_handle(ioctl);
    const int status = DeviceIoControl(ioctl->handle, IOCTL_CDROM_READ_TOC_EX,
                                       &ioctl->cur_read_toc_ex, 65535,
                                       cur_full_toc, 65535,
                                       (LPDWORD) &size, NULL);
    ioctl_close_handle(ioctl);
    ioctl_log(ioctl->log, "status = %i\n", status);

    if ((status == 0) && (ioctl->tracks_num >= 1)) {
        /*
           This is needed because in some circumstances (eg. a DVD .MDS
           mounted in Daemon Tools), reading the raw TOC fails but
           reading the cooked TOC does not, so we have to construct the
           raw TOC from the cooked TOC.
         */
        const CDROM_TOC  *toc = (const CDROM_TOC *) ioctl->cur_toc;
        const TRACK_DATA *ct  = &(toc->TrackData[ioctl->tracks_num - 1]);

        rti[0].adr_ctl = ((ct->Adr & 0xf) << 4) | (ct->Control & 0xf);
        rti[0].point   = 0xa0;
        rti[0].pm      = toc->FirstTrack;

        rti[1].adr_ctl = rti[0].adr_ctl;
        rti[1].point   = 0xa1;
        rti[1].pm      = toc->LastTrack;

        rti[2].adr_ctl = rti[0].adr_ctl;
        rti[2].point   = 0xa2;
        rti[2].pm      = ct->Address[1];
        rti[2].ps      = ct->Address[2];
        rti[2].pf      = ct->Address[3];

        ioctl->blocks_num = 3;

        for (int i = 0; i < (ioctl->tracks_num - 1); i++) {
            raw_track_info_t *crt = &(rti[ioctl->blocks_num]);

            ct           = &(toc->TrackData[i]);

            crt->adr_ctl = ((ct->Adr & 0xf) << 4) | (ct->Control & 0xf);
            crt->point   = ct->TrackNumber;
            crt->pm      = ct->Address[1];
            crt->ps      = ct->Address[2];
            crt->pf      = ct->Address[3];

            ioctl->blocks_num++;
        }
    } else if (status != 0) {
        ioctl->blocks_num = (((cur_full_toc->Length[0] << 8) |
                              cur_full_toc->Length[1]) - 2) / 11;
        memcpy(ioctl->cur_rti, cur_full_toc->Descriptors, ioctl->blocks_num * 11);
    }

    if (ioctl->blocks_num)  for (int i = 0; i < ioctl->tracks_num; i++) {
        const raw_track_info_t *crt = &(rti[i]);

        if ((crt->point >= 1) && (crt->point <= 99) && !(crt->adr_ctl & 0x04)) {
            ioctl->has_audio = 1;
            break;
        }
    }

#ifdef ENABLE_IOCTL_LOG
    uint8_t                  *u            = (uint8_t *) cur_full_toc;

    ioctl_log(ioctl->log, "%i blocks: %02X %02X %02X %02X\n",
              ioctl->blocks_num, u[0], u[1], u[2], u[3]);

    for (int i = 0; i < ioctl->blocks_num; i++) {
        uint8_t *t  = (uint8_t *) &rti[i];
        ioctl_log(ioctl->log, "Block %03i: %02X %02X %02X %02X %02X %02X %02X %02X "
                  "%02X %02X %02X\n",
                  i, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8],
                  t[9], t[10]);
    }
#endif

    free(cur_full_toc);
}

static void
ioctl_read_toc(ioctl_t *ioctl)
{
    (void) ioctl_read_normal_toc(ioctl, ioctl->cur_toc);
    ioctl_read_raw_toc(ioctl);
}

static int
ioctl_get_track(const ioctl_t *ioctl, const uint32_t sector) {
    raw_track_info_t *rti   = (raw_track_info_t *) ioctl->cur_rti;
    int               track = -1;

    for (int i = (ioctl->blocks_num - 1); i >= 0; i--) {
         const raw_track_info_t *ct    = &(rti[i]);
         const uint32_t          start = (ct->pm * 60 * 75) + (ct->ps * 75) + ct->pf - 150;

         ioctl_log(ioctl->log, "ioctl_get_track(): ct: %02X, %08X\n",
                   ct->point, start);

         if ((ct->point >= 1) && (ct->point <= 99) && (sector >= start)) {
             track = i;
             ioctl_log(ioctl->log, "ioctl_get_track(): found track: %i\n", i);
             break;
         }
    }

    return track;
}

static int
ioctl_is_track_audio(const ioctl_t *ioctl, const uint32_t pos)
{
    const raw_track_info_t *rti     = (const raw_track_info_t *) ioctl->cur_rti;
    int                     ret     = 0;

    if (ioctl->has_audio && !ioctl->is_dvd) {
        const int track   = ioctl_get_track(ioctl, pos);
        const int control = rti[track].adr_ctl;

        ret     = !(control & 0x04);

        ioctl_log(ioctl->log, "ioctl_is_track_audio(%08X, %02X): %i\n", pos, track, ret);
    }

    return ret;
}

/* Shared functions. */
static int
ioctl_get_track_info(const void *local, const uint32_t track,
                     int end, track_info_t *ti)
{
    const ioctl_t *  ioctl = (const ioctl_t *) local;
    const CDROM_TOC *toc   = (const CDROM_TOC *) ioctl->cur_toc;
    int              ret   = 1;

    if ((track < 1) || (track == 0xaa) || (track > (toc->LastTrack + 1))) {
        ioctl_log(ioctl->log, "ioctl_get_track_info(%02i)\n", track);
        ret = 0;
    } else {
        const TRACK_DATA * td    = &toc->TrackData[track - 1];

        ti->m      = td->Address[1];
        ti->s      = td->Address[2];
        ti->f      = td->Address[3];

        ti->number = td->TrackNumber;
        ti->attr   = td->Control;
        ti->attr  |= ((td->Adr << 4) & 0xf0);

        ioctl_log(ioctl->log, "ioctl_get_track_info(%02i): %02i:%02i:%02i, %02i, %02X\n",
                  track, ti->m, ti->s, ti->f, ti->number, ti->attr);
    }

    return ret;
}

static void
ioctl_get_raw_track_info(const void *local, int *num, uint8_t *rti)
{
    const ioctl_t *ioctl = (const ioctl_t *) local;

    *num = ioctl->blocks_num;
    memcpy(rti, ioctl->cur_rti, ioctl->blocks_num * 11);
}

static int
ioctl_is_track_pre(const void *local, const uint32_t sector)
{
    const ioctl_t          *ioctl   = (const ioctl_t *) local;
    const raw_track_info_t *rti     = (const raw_track_info_t *) ioctl->cur_rti;
    int                     ret     = 0;

    if (ioctl->has_audio && !ioctl->is_dvd) {
        const int track   = ioctl_get_track(ioctl, sector);
        const int control = rti[track].adr_ctl;

        ret     = control & 0x01;

        ioctl_log(ioctl->log, "ioctl_is_track_pre(%08X, %02X): %i\n", sector, track, ret);
    }

    return ret;
}

static int
ioctl_read_sector(const void *local, uint8_t *buffer, uint32_t const sector)
{
    typedef struct SCSI_PASS_THROUGH_DIRECT_BUF {
        SCSI_PASS_THROUGH_DIRECT spt;
        ULONG                    Filler;
        UCHAR                    SenseBuf[64];
    } SCSI_PASS_THROUGH_DIRECT_BUF;

    const ioctl_t *              ioctl     = (const ioctl_t *) local;
    const raw_track_info_t *     rti       = (raw_track_info_t *) ioctl->cur_rti;
    unsigned long int            unused    = 0;
    const int                    sc_offs   = (sector == 0xffffffff) ? 0 : 2352;
    int                          len       = (sector == 0xffffffff) ? 16 : 2368;
    int                          m         = 0;
    int                          s         = 0;
    int                          f         = 0;
    uint32_t                     lba       = sector;
    int                          ret;
    SCSI_PASS_THROUGH_DIRECT_BUF req;

    ioctl_open_handle((ioctl_t *) ioctl);

    if (ioctl->is_dvd) {
        int                          track;

        req.spt.DataTransferLength    = 0;
        ret                           = 0;

        if (lba == 0xffffffff) {
            lba                           = ioctl->dev->seek_pos;
            track                         = ioctl_get_track(ioctl, lba);

            if (track != -1) {
                req.spt.DataTransferLength    = len;
                ret                           = 1;
            }
        } else {
            len                           = COOKED_SECTOR_SIZE;
            track                         = ioctl_get_track(ioctl, lba);

            if (track != -1) {
                DWORD newPos = SetFilePointer(ioctl->handle, (long) lba * COOKED_SECTOR_SIZE,
                                              0, FILE_BEGIN);

                if (newPos != 0xffffffff)
                    ret = ReadFile(ioctl->handle, &(buffer[16]),
                                   COOKED_SECTOR_SIZE, (LPDWORD) &req.spt.DataTransferLength,
                                   NULL);
            }
        }

        if (ret && (req.spt.DataTransferLength >= len) && (track != -1)) {
            const raw_track_info_t *ct    = &(rti[track]);
            const uint32_t          start = (ct->pm * 60 * 75) + (ct->ps * 75) + ct->pf;

            m     = s = f = 0;

            /* Construct sector header and sub-header. */
            if (sector != 0xffffffff) {
                /* Sync bytes. */
                buffer[0] = 0x00;
                memset(&(buffer[1]), 0xff, 10);
                buffer[11] = 0x00;

                /* Sector header. */
                FRAMES_TO_MSF(lba + 150, &m, &s, &f);
                buffer[12] = bin2bcd(m);
                buffer[13] = bin2bcd(s);
                buffer[14] = bin2bcd(f);

                /* Mode 1 data. */
                buffer[15] = 0x01;
            }

            /* Construct Q. */
            buffer[sc_offs + 0] = (ct->adr_ctl >> 4) | ((ct->adr_ctl & 0xf) << 4);
            buffer[sc_offs + 1] = bin2bcd(ct->point);
            buffer[sc_offs + 2] = 1;
            FRAMES_TO_MSF((int32_t) (lba + 150 - start), &m, &s, &f);
            buffer[sc_offs + 3] = bin2bcd(m);
            buffer[sc_offs + 4] = bin2bcd(s);
            buffer[sc_offs + 5] = bin2bcd(f);
            FRAMES_TO_MSF(lba + 150, &m, &s, &f);
            buffer[sc_offs + 7] = bin2bcd(m);
            buffer[sc_offs + 8] = bin2bcd(s);
            buffer[sc_offs + 9] = bin2bcd(f);
        }
    } else {
        memset(&req, 0x00, sizeof(SCSI_PASS_THROUGH_DIRECT_BUF));
        req.spt.Length                = sizeof(SCSI_PASS_THROUGH_DIRECT);
        req.spt.PathId                = 0;
        req.spt.TargetId              = 1;
        req.spt.Lun                   = 0;
        req.spt.CdbLength             = 12;
        req.spt.DataIn                = SCSI_IOCTL_DATA_IN;
        req.spt.SenseInfoLength       = sizeof(req.SenseBuf);
        req.spt.DataTransferLength    = len;
        req.spt.TimeOutValue          = 6;
        req.spt.DataBuffer            = buffer;
        req.spt.SenseInfoOffset       = offsetof(SCSI_PASS_THROUGH_DIRECT_BUF, SenseBuf);

        /* Fill in the CDB. */
        req.spt.Cdb[0]                 = 0xbe;             /* READ CD */
        req.spt.Cdb[1]                 = 0x00;
        req.spt.Cdb[2]                 = (sector >> 24) & 0xff;
        req.spt.Cdb[3]                 = (sector >> 16) & 0xff;
        req.spt.Cdb[4]                 = (sector >> 8) & 0xff;
        req.spt.Cdb[5]                 = sector & 0xff;    /* Starting Logical Block Address. */
        req.spt.Cdb[6]                 = 0x00;
        req.spt.Cdb[7]                 = 0x00;
        req.spt.Cdb[8]                 = 0x01;             /* Transfer Length. */
        /* If sector is FFFFFFFF, only return the subchannel. */
        req.spt.Cdb[9]                 = (sector == 0xffffffff) ? 0x00 : 0xf8;
        req.spt.Cdb[10]                = 0x02;
        req.spt.Cdb[11]                = 0x00;
        DWORD length                   = sizeof(SCSI_PASS_THROUGH_DIRECT_BUF);

#ifdef ENABLE_IOCTL_LOG
        uint8_t *cdb = (uint8_t *) req.spt.Cdb;
        ioctl_log(ioctl->log, "Host CDB: %02X %02X %02X %02X %02X %02X "
                  "%02X %02X %02X %02X %02X %02X\n",
                  ioctl->dev->id, cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5],
                  cdb[6], cdb[7], cdb[8], cdb[9], cdb[10], cdb[11]);
#endif

        ret = DeviceIoControl(ioctl->handle, IOCTL_SCSI_PASS_THROUGH_DIRECT,
                              &req, length,
                              &req, length, &unused, NULL);
    }

    ioctl_log(ioctl->log, "ioctl_read_sector: ret = %d, req.spt.DataTransferLength = %lu\n",
              ret, req.spt.DataTransferLength);
    ioctl_log(ioctl->log, "Sense: %08X, %08X\n", req.spt.SenseInfoLength, req.spt.SenseInfoOffset);
    if (req.spt.SenseInfoLength >= 16) {
        uint8_t *cdb = (uint8_t *) req.SenseBuf;
        if ((cdb[2] == 0x03) && (cdb[12] == 0x11))
            /* Treat this as an error to corectly indicate CIRC error to the guest. */
            ret = 0;
        ioctl_log(ioctl->log, "Host sense: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  cdb[0], cdb[1], cdb[ 2], cdb[ 3], cdb[ 4], cdb[ 5], cdb[ 6], cdb[ 7]);
        ioctl_log(ioctl->log, "            %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  cdb[8], cdb[9], cdb[10], cdb[11], cdb[12], cdb[13], cdb[14], cdb[15]);
    }

    ret = (!!ret > 0) ? (req.spt.DataTransferLength >= len) : -1;
    ioctl_log(ioctl->log, "iocl_read_sector: final ret = %i\n", ret);

    /* Construct raw subchannel data from Q only. */
    if ((ret > 0) && (req.spt.DataTransferLength >= len))
        for (int i = 11; i >= 0; i--)
             for (int j = 7; j >= 0; j--)
                  buffer[2352 + (i * 8) + j] = ((buffer[sc_offs + i] >> (7 - j)) & 0x01) << 6;

    ioctl_close_handle((ioctl_t *) ioctl);

    return ret;
}

static uint8_t
ioctl_get_track_type(const void *local, const uint32_t sector)
{
    ioctl_t *               ioctl = (ioctl_t *) local;
    int                     track = ioctl_get_track(ioctl, sector);
    raw_track_info_t *      rti   = (raw_track_info_t *) ioctl->cur_rti;
    const raw_track_info_t *trk   = &(rti[track]);
    uint8_t                 ret   = 0x00;

    if (ioctl_is_track_audio(ioctl, sector))
        ret = CD_TRACK_AUDIO;
    else  if (track != -1)  for (int i = 0; i < ioctl->blocks_num; i++) {
        const raw_track_info_t *ct = &(rti[i]);
        const raw_track_info_t *nt = &(rti[i + 1]);

        if (ct->point == 0xa0) {
            uint8_t first = ct->pm;
            uint8_t last  = nt->pm;

            if ((trk->point >= first) && (trk->point <= last)) {
                ret = ct->ps;
                break;
            }
        }
    }

    return ret;
}

static uint32_t
ioctl_get_last_block(const void *local)
{
    const ioctl_t   *ioctl   = (const ioctl_t *) local;
    const CDROM_TOC *toc     = (const CDROM_TOC *) ioctl->cur_toc;
    uint32_t         lb      = 0;

    for (int c = 0; c <= toc->LastTrack; c++) {
        const TRACK_DATA *td      = &toc->TrackData[c];
        const uint32_t    address = MSFtoLBA(td->Address[1], td->Address[2],
                                             td->Address[3]) - 150;

        if (address > lb)
            lb = address;
    }

    ioctl_log(ioctl->log, "LBCapacity=%d\n", lb);

    return lb;
}

static int
ioctl_read_dvd_structure(const void *local, const uint8_t layer, const uint8_t format,
                         uint8_t *buffer, uint32_t *info)
{
    typedef struct SCSI_PASS_THROUGH_DIRECT_BUF {
        SCSI_PASS_THROUGH_DIRECT spt;
        ULONG                    Filler;
        UCHAR                    SenseBuf[64];
    } SCSI_PASS_THROUGH_DIRECT_BUF;

    const ioctl_t *              ioctl   = (const ioctl_t *) local;
    unsigned long int            unused  = 0;
    const int                    len     = 2052;
    SCSI_PASS_THROUGH_DIRECT_BUF req;

    ioctl_open_handle((ioctl_t *) ioctl);

    memset(&req, 0x00, sizeof(SCSI_PASS_THROUGH_DIRECT_BUF));
    req.spt.Length                = sizeof(SCSI_PASS_THROUGH_DIRECT);
    req.spt.PathId                = 0;
    req.spt.TargetId              = 1;
    req.spt.Lun                   = 0;
    req.spt.CdbLength             = 12;
    req.spt.DataIn                = SCSI_IOCTL_DATA_IN;
    req.spt.SenseInfoLength       = sizeof(req.SenseBuf);
    req.spt.DataTransferLength    = len;
    req.spt.TimeOutValue          = 6;
    req.spt.DataBuffer            = buffer;
    req.spt.SenseInfoOffset       = offsetof(SCSI_PASS_THROUGH_DIRECT_BUF, SenseBuf);

    /* Fill in the CDB. */
    req.spt.Cdb[0]                 = 0xad;
    req.spt.Cdb[1]                 = 0x00;
    req.spt.Cdb[2]                 = 0x00;
    req.spt.Cdb[3]                 = 0x00;
    req.spt.Cdb[4]                 = 0x00;
    req.spt.Cdb[5]                 = 0x00;
    req.spt.Cdb[6]                 = layer;   /* Layer Number */
    req.spt.Cdb[7]                 = format;  /* Format */
    req.spt.Cdb[8]                 = 0x08;    /* Allocation Length */
    req.spt.Cdb[9]                 = 0x04;
    req.spt.Cdb[10]                = 0x00;    /* AGID */
    req.spt.Cdb[11]                = 0x00;

    DWORD length                   = sizeof(SCSI_PASS_THROUGH_DIRECT_BUF);

#ifdef ENABLE_IOCTL_LOG
    uint8_t *cdb = (uint8_t *) req.spt.Cdb;
    ioctl_log(ioctl->log, "Host CDB: %02X %02X %02X %02X %02X %02X "
              "%02X %02X %02X %02X %02X %02X\n",
              cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5],
              cdb[6], cdb[7], cdb[8], cdb[9], cdb[10], cdb[11]);
#endif

    int ret = DeviceIoControl(ioctl->handle, IOCTL_SCSI_PASS_THROUGH_DIRECT,
                              &req, length,
                              &req, length,
                              &unused, NULL);

    ioctl_log(ioctl->log, "ioctl_read_dvd_structure(): ret = %d, "
              "req.spt.DataTransferLength = %lu\n",
              ret, req.spt.DataTransferLength);
    ioctl_log(ioctl->log, "Sense: %08X, %08X\n", req.spt.SenseInfoLength,
              req.spt.SenseInfoOffset);

    if (req.spt.SenseInfoLength >= 16) {
        uint8_t *sb = (uint8_t *) req.SenseBuf;
        /* Return sense to the host as is. */
        ret = -((sb[2] << 16) | (sb[12] << 8) | sb[13]);
        if (info != NULL)
            *info = *(uint32_t *) &(sb[3]);
        ioctl_log(ioctl->log, "Host sense: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  sb[0], sb[1], sb[ 2], sb[ 3], sb[ 4], sb[ 5], sb[ 6], sb[ 7]);
        ioctl_log(ioctl->log, "            %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  sb[8], sb[9], sb[10], sb[11], sb[12], sb[13], sb[14], sb[15]);
    } else
        ret = ret ? (req.spt.DataTransferLength >= len) : 0;

    ioctl_close_handle((ioctl_t *) ioctl);

    return ret;
}

static int
ioctl_is_dvd(const void *local)
{
    const ioctl_t *ioctl = (const ioctl_t *) local;

    return ioctl->is_dvd;
}

static int
ioctl_has_audio(const void *local)
{
    const ioctl_t *ioctl = (const ioctl_t *) local;

    return ioctl->has_audio;
}

static int
ioctl_is_empty(const void *local)
{
    typedef struct SCSI_PASS_THROUGH_DIRECT_BUF {
        SCSI_PASS_THROUGH_DIRECT spt;
        ULONG                    Filler;
        UCHAR                    SenseBuf[64];
    } SCSI_PASS_THROUGH_DIRECT_BUF;

    const ioctl_t *              ioctl   = (const ioctl_t *) local;
    unsigned long int            unused  = 0;
    SCSI_PASS_THROUGH_DIRECT_BUF req;

    ioctl_open_handle((ioctl_t *) ioctl);

    memset(&req, 0x00, sizeof(SCSI_PASS_THROUGH_DIRECT_BUF));
    req.spt.Length                = sizeof(SCSI_PASS_THROUGH_DIRECT);
    req.spt.PathId                = 0;
    req.spt.TargetId              = 1;
    req.spt.Lun                   = 0;
    req.spt.CdbLength             = 12;
    req.spt.DataIn                = SCSI_IOCTL_DATA_IN;
    req.spt.SenseInfoLength       = sizeof(req.SenseBuf);
    req.spt.DataTransferLength    = 0;
    req.spt.TimeOutValue          = 6;
    req.spt.DataBuffer            = NULL;
    req.spt.SenseInfoOffset       = offsetof(SCSI_PASS_THROUGH_DIRECT_BUF, SenseBuf);

    /* Fill in the CDB. */
    req.spt.Cdb[0]                 = 0x00;
    req.spt.Cdb[1]                 = 0x00;
    req.spt.Cdb[2]                 = 0x00;
    req.spt.Cdb[3]                 = 0x00;
    req.spt.Cdb[4]                 = 0x00;
    req.spt.Cdb[5]                 = 0x00;
    req.spt.Cdb[6]                 = 0x00;
    req.spt.Cdb[7]                 = 0x00;
    req.spt.Cdb[8]                 = 0x00;
    req.spt.Cdb[9]                 = 0x00;
    req.spt.Cdb[10]                = 0x00;
    req.spt.Cdb[11]                = 0x00;

    DWORD length                   = sizeof(SCSI_PASS_THROUGH_DIRECT_BUF);

#ifdef ENABLE_IOCTL_LOG
    uint8_t *cdb = (uint8_t *) req.spt.Cdb;
    ioctl_log(ioctl->log, "Host CDB: %02X %02X %02X %02X %02X %02X "
              "%02X %02X %02X %02X %02X %02X\n",
              cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5],
              cdb[6], cdb[7], cdb[8], cdb[9], cdb[10], cdb[11]);
#endif

    int ret = DeviceIoControl(ioctl->handle, IOCTL_SCSI_PASS_THROUGH_DIRECT,
                              &req, length,
                              &req, length,
                              &unused, NULL);

    ioctl_log(ioctl->log, "ioctl_read_dvd_structure(): ret = %d, "
              "req.spt.DataTransferLength = %lu\n",
              ret, req.spt.DataTransferLength);
    ioctl_log(ioctl->log, "Sense: %08X, %08X\n", req.spt.SenseInfoLength,
              req.spt.SenseInfoOffset);

    if (req.spt.SenseInfoLength >= 16) {
        uint8_t *sb = (uint8_t *) req.SenseBuf;
        /* Return sense to the host as is. */
        ret = ((sb[2] == SENSE_NOT_READY) && (sb[12] == ASC_MEDIUM_NOT_PRESENT));
        ioctl_log(ioctl->log, "Host sense: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  sb[0], sb[1], sb[ 2], sb[ 3], sb[ 4], sb[ 5], sb[ 6], sb[ 7]);
        ioctl_log(ioctl->log, "            %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  sb[8], sb[9], sb[10], sb[11], sb[12], sb[13], sb[14], sb[15]);
    } else
        ret = 0;

    ioctl_close_handle((ioctl_t *) ioctl);

    return ret;
}

static void
ioctl_close(void *local)
{
    ioctl_t *ioctl = (ioctl_t *) local;

    ioctl_close_handle(ioctl);
    ioctl->handle = NULL;

    ioctl_log(ioctl->log, "Log closed\n");

    log_close(ioctl->log);
    ioctl->log = NULL;
}

static void
ioctl_load(const void *local)
{
    const ioctl_t *ioctl = (const ioctl_t *) local;

    if (ioctl_open_handle((ioctl_t *) ioctl)) {
        long size;
        DeviceIoControl(ioctl->handle, IOCTL_STORAGE_LOAD_MEDIA,
                        NULL, 0, NULL, 0,
                        (LPDWORD) &size, NULL);
        ioctl_close_handle((ioctl_t *) ioctl);

        ioctl_read_toc((ioctl_t *) ioctl);
    }
}

static const cdrom_ops_t ioctl_ops = {
    ioctl_get_track_info,
    ioctl_get_raw_track_info,
    ioctl_is_track_pre,
    ioctl_read_sector,
    ioctl_get_track_type,
    ioctl_get_last_block,
    ioctl_read_dvd_structure,
    ioctl_is_dvd,
    ioctl_has_audio,
    ioctl_is_empty,
    ioctl_close,
    ioctl_load
};

/* Public functions. */
void *
ioctl_open(cdrom_t *dev, const char *drv)
{
    ioctl_t *ioctl = (ioctl_t *) calloc(1, sizeof(ioctl_t));

    if (ioctl != NULL) {
        char n[1024]        = { 0 };

        sprintf(n, "CD-ROM %i IOCtl", dev->id + 1);
        ioctl->log          = log_open(n);

        memset(ioctl->path, 0x00, sizeof(ioctl->path));

        wsprintf(ioctl->path, L"%S", &(drv[8]));
        ioctl_log(ioctl->log, "Path is %S\n", ioctl->path);

        ioctl->dev          = dev;

        dev->ops            = &ioctl_ops;

        ioctl_load(ioctl);
    }

    return ioctl;
}
