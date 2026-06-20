/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Linux CD-ROM support via IOCTL (SG_IO).
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *
 *          Copyright 2023      TheCollector1995.
 *          Copyright 2023      Miran Grca.
 *          Copyright 2025      86Box contributors.
 */
#include <inttypes.h>
#ifdef ENABLE_IOCTL_LOG
#include <stdarg.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>
#include <linux/cdrom.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/log.h>
#include <86box/plat_cdrom_ioctl.h>
#include <86box/scsi_device.h>

typedef struct ioctl_t {
    cdrom_t  *dev;
    void     *log;
    int       fd;
    int       is_dvd;
    int       has_audio;
    int       blocks_num;
    uint8_t   cur_rti[65536];
    char      path[256];
    pthread_t poll_tid;
    int       poll_active;
} ioctl_t;

static int ioctl_read_dvd_structure(const void *local, uint8_t layer, uint8_t format,
                                    uint8_t *buffer, uint32_t *info);
static int ioctl_is_empty(const void *local);

/*
 * Wrapper for the system ioctl() call to avoid naming collisions
 * with the local 'ioctl' variable of type ioctl_t*.
 */
static inline int
sys_ioctl(int fd, unsigned long request, void *arg)
{
    return ioctl(fd, request, arg);
}

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
    if (ioctl->fd >= 0)
        close(ioctl->fd);
}

static int
ioctl_open_handle(ioctl_t *ioctl)
{
    ioctl_log(ioctl->log, "ioctl->path = \"%s\"\n", ioctl->path);
    ioctl->fd = open(ioctl->path, O_RDONLY | O_NONBLOCK);

    ioctl_log(ioctl->log, "fd=%d, errno=%d\n", ioctl->fd, errno);

    return (ioctl->fd >= 0);
}

/*
 * Execute a SCSI command via the Linux SG_IO interface.
 * Returns 1 on success, 0 on failure.
 * sense_buf should be at least 64 bytes, sense_len receives actual sense length.
 */
static int
sg_io_cmd(int fd, const uint8_t *cdb, int cdb_len,
          uint8_t *data_buf, int data_len, int direction,
          uint8_t *sense_buf, int *sense_len)
{
    sg_io_hdr_t io_hdr;

    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id    = 'S';
    io_hdr.cmd_len         = cdb_len;
    io_hdr.mx_sb_len       = 64;
    io_hdr.dxfer_direction = direction;
    io_hdr.dxfer_len       = data_len;
    io_hdr.dxferp          = data_buf;
    io_hdr.cmdp            = (unsigned char *) cdb;
    io_hdr.sbp             = sense_buf;
    io_hdr.timeout         = 6000; /* 6 seconds, matching Windows */

    if (ioctl(fd, SG_IO, &io_hdr) < 0)
        return 0;

    if (sense_len != NULL)
        *sense_len = io_hdr.sb_len_wr;

    /* Check for SCSI errors. */
    if ((io_hdr.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
        if (io_hdr.sb_len_wr > 0) {
            if (sense_len != NULL)
                *sense_len = io_hdr.sb_len_wr;
            return 0;
        }
        return 0;
    }

    return 1;
}

static int
ioctl_read_normal_toc(ioctl_t *ioctl, uint8_t *toc_buf, int32_t *tracks_num)
{
    struct cdrom_tochdr  tochdr;
    struct cdrom_tocentry tocentry;
    int status;

    *tracks_num = 0;
    memset(toc_buf, 0x00, 65536);

    status = sys_ioctl(ioctl->fd, CDROMREADTOCHDR, &tochdr);
    if (status < 0)
        return 0;

    ioctl_log(ioctl->log, "TOC: first=%d, last=%d\n",
              tochdr.cdth_trk0, tochdr.cdth_trk1);

    /*
     * Build a cooked TOC buffer in the same format as the Windows
     * CDROM_TOC structure:
     *   [0..1] = length (big-endian)
     *   [2]    = first track
     *   [3]    = last track
     *   [4..]  = 8-byte track descriptors
     *
     * Each track descriptor:
     *   [0] = reserved
     *   [1] = Adr/Control
     *   [2] = track number
     *   [3] = reserved
     *   [4..7] = MSF address (0, M, S, F)
     */
    toc_buf[2] = tochdr.cdth_trk0;
    toc_buf[3] = tochdr.cdth_trk1;

    int count = 0;
    for (int i = tochdr.cdth_trk0; i <= tochdr.cdth_trk1; i++) {
        memset(&tocentry, 0, sizeof(tocentry));
        tocentry.cdte_track  = i;
        tocentry.cdte_format = CDROM_MSF;

        if (sys_ioctl(ioctl->fd, CDROMREADTOCENTRY, &tocentry) < 0)
            continue;

        uint8_t *t = &toc_buf[4 + count * 8];
        t[0] = 0;
        t[1] = ((tocentry.cdte_adr & 0xf) << 4) | (tocentry.cdte_ctrl & 0xf);
        t[2] = i;
        t[3] = 0;
        t[4] = 0;
        t[5] = tocentry.cdte_addr.msf.minute;
        t[6] = tocentry.cdte_addr.msf.second;
        t[7] = tocentry.cdte_addr.msf.frame;
        count++;
    }

    /* Lead-out (track 0xAA). */
    memset(&tocentry, 0, sizeof(tocentry));
    tocentry.cdte_track  = CDROM_LEADOUT;
    tocentry.cdte_format = CDROM_MSF;

    if (sys_ioctl(ioctl->fd, CDROMREADTOCENTRY, &tocentry) >= 0) {
        uint8_t *t = &toc_buf[4 + count * 8];
        t[0] = 0;
        t[1] = ((tocentry.cdte_adr & 0xf) << 4) | (tocentry.cdte_ctrl & 0xf);
        t[2] = 0xAA;
        t[3] = 0;
        t[4] = 0;
        t[5] = tocentry.cdte_addr.msf.minute;
        t[6] = tocentry.cdte_addr.msf.second;
        t[7] = tocentry.cdte_addr.msf.frame;
        count++;
    }

    /* Set the length field (big-endian). */
    int length = 2 + count * 8;
    toc_buf[0] = (length >> 8) & 0xff;
    toc_buf[1] = length & 0xff;
    *tracks_num = count;

    ioctl_log(ioctl->log, "%i tracks\n", *tracks_num);

    return 1;
}

static void
ioctl_read_raw_toc(ioctl_t *ioctl)
{
    raw_track_info_t *rti    = (raw_track_info_t *) ioctl->cur_rti;
    uint8_t          *buffer = (uint8_t *) calloc(1, 2052);
    int               status = 0;

    ioctl->is_dvd = (ioctl_read_dvd_structure(ioctl, 0, 0, buffer, NULL) > 0);
    free(buffer);

    ioctl->has_audio  = 0;
    ioctl->blocks_num = 0;
    memset(ioctl->cur_rti, 0x00, 65536);

    if (!ioctl->is_dvd) {
        /* Try SG_IO with READ TOC command, Format=2 (full/raw TOC). */
        uint8_t cdb[12];
        uint8_t sense[64];
        uint8_t *raw_buf = (uint8_t *) calloc(1, 65536);
        int      sense_len = 0;

        memset(cdb, 0, sizeof(cdb));
        cdb[0]  = 0x43;         /* READ TOC */
        cdb[1]  = 0x02;         /* MSF */
        cdb[2]  = 0x02;         /* Format = Full TOC (raw) */
        cdb[6]  = 0x01;         /* Session = 1 */
        cdb[7]  = 0xff;         /* Allocation length high */
        cdb[8]  = 0xff;         /* Allocation length low */

        memset(sense, 0, sizeof(sense));
        status = sg_io_cmd(ioctl->fd, cdb, 10, raw_buf, 65535,
                           SG_DXFER_FROM_DEV, sense, &sense_len);

        if (status && sense_len == 0) {
            int length = ((raw_buf[0] << 8) | raw_buf[1]) - 2;
            ioctl->blocks_num = length / 11;
            if (ioctl->blocks_num > 0)
                memcpy(ioctl->cur_rti, &raw_buf[4], ioctl->blocks_num * 11);
        } else {
            status = 0;
        }

        free(raw_buf);
    }

    if (status == 0) {
        /*
         * Raw TOC read failed (or this is a DVD). Fall back to the
         * cooked TOC, and construct raw_track_info_t entries from it,
         * mirroring the Windows fallback path.
         */
        uint8_t  cur_toc[65536] = { 0 };
        int32_t  tracks_num     = 0;

        status = ioctl_read_normal_toc(ioctl, cur_toc, &tracks_num);

        if ((status > 0) && (tracks_num >= 1)) {
            /* Last real entry (the lead-out). */
            const uint8_t *ct = &cur_toc[4 + (tracks_num - 1) * 8];

            rti[0].adr_ctl = ct[1];
            rti[0].point   = 0xa0;
            rti[0].pm      = cur_toc[2]; /* FirstTrack */

            rti[1].adr_ctl = rti[0].adr_ctl;
            rti[1].point   = 0xa1;
            rti[1].pm      = cur_toc[3]; /* LastTrack */

            rti[2].adr_ctl = rti[0].adr_ctl;
            rti[2].point   = 0xa2;
            rti[2].pm      = ct[5]; /* M */
            rti[2].ps      = ct[6]; /* S */
            rti[2].pf      = ct[7]; /* F */

            ioctl->blocks_num = 3;

            for (int i = 0; i < (tracks_num - 1); i++) {
                raw_track_info_t *crt = &(rti[ioctl->blocks_num]);

                ct = &cur_toc[4 + i * 8];

                crt->adr_ctl = ct[1];
                crt->point   = ct[2];
                crt->pm      = ct[5];
                crt->ps      = ct[6];
                crt->pf      = ct[7];

                ioctl->blocks_num++;
            }
        } else if (status > 0)
            /* Announce that we've had a failure. */
            status = 0;
    }

    if (ioctl->blocks_num)  for (int i = 0; i < ioctl->blocks_num; i++) {
        const raw_track_info_t *crt = &(rti[i]);

        if ((crt->point >= 1) && (crt->point <= 99) && !(crt->adr_ctl & 0x04)) {
            ioctl->has_audio = 1;
            break;
        }
    }

#ifdef ENABLE_IOCTL_LOG
    ioctl_log(ioctl->log, "%i blocks\n", ioctl->blocks_num);

    for (int i = 0; i < ioctl->blocks_num; i++) {
        uint8_t *t = (uint8_t *) &rti[i];
        ioctl_log(ioctl->log, "Block %03i: %02X %02X %02X %02X %02X %02X %02X %02X "
                  "%02X %02X %02X\n",
                  i, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8],
                  t[9], t[10]);
    }
#endif
}

static int
ioctl_get_track(const ioctl_t *ioctl, const uint32_t sector)
{
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
    const raw_track_info_t *rti = (const raw_track_info_t *) ioctl->cur_rti;
    int                     ret = 0;

    if (ioctl->has_audio && !ioctl->is_dvd) {
        const int track   = ioctl_get_track(ioctl, pos);
        const int control = rti[track].adr_ctl;

        ret = !(control & 0x04);

        ioctl_log(ioctl->log, "ioctl_is_track_audio(%08X, %02X): %i\n", pos, track, ret);
    }

    return ret;
}

/* Shared functions (cdrom_ops_t interface). */
static int
ioctl_get_track_info(const void *local, const uint32_t track,
                     int end, track_info_t *ti)
{
    const ioctl_t          *ioctl = (const ioctl_t *) local;
    const raw_track_info_t *rti   = (const raw_track_info_t *) ioctl->cur_rti;
    int                     ret   = 1;
    int                     trk   = -1;
    int                     next  = -1;

    if ((track >= 1) && (track < 99))
        for (int i = 0; i < ioctl->blocks_num; i++)
            if (rti[i].point == track) {
                trk = i;
                break;
            }

    if ((track >= 1) && (track < 98))
        for (int i = 0; i < ioctl->blocks_num; i++)
            if ((rti[i].point == (track + 1)) && (rti[i].session == rti[trk].session)) {
                next = i;
                break;
            }

    if ((track >= 1) && (track < 99) && (trk != -1) && (next == -1))
        for (int i = 0; i < ioctl->blocks_num; i++)
            if ((rti[i].point == 0xa2) && (rti[i].session == rti[trk].session)) {
                next = i;
                break;
            }

    if ((track == 0xaa) || (trk == -1)) {
        ioctl_log(ioctl->log, "ioctl_get_track_info(%02i)\n", track);
        ret = 0;
    } else {
        if (end) {
            if (next != -1) {
                ti->m = rti[next].pm;
                ti->s = rti[next].ps;
                ti->f = rti[next].pf;
            }
        } else {
            ti->m = rti[trk].pm;
            ti->s = rti[trk].ps;
            ti->f = rti[trk].pf;
        }

        ti->number = rti[trk].point;
        ti->attr   = rti[trk].adr_ctl;

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
    const ioctl_t          *ioctl = (const ioctl_t *) local;
    const raw_track_info_t *rti   = (const raw_track_info_t *) ioctl->cur_rti;
    int                     ret   = 0;

    if (ioctl->has_audio && !ioctl->is_dvd) {
        const int track   = ioctl_get_track(ioctl, sector);
        const int control = rti[track].adr_ctl;

        ret = control & 0x01;

        ioctl_log(ioctl->log, "ioctl_is_track_pre(%08X, %02X): %i\n", sector, track, ret);
    }

    return ret;
}

static int
ioctl_read_sector(const void *local, uint8_t *buffer, uint32_t const sector)
{
    const ioctl_t          *ioctl   = (const ioctl_t *) local;
    const raw_track_info_t *rti     = (raw_track_info_t *) ioctl->cur_rti;
    const int               sc_offs = (sector == 0xffffffff) ? 0 : 2352;
    int                     len     = (sector == 0xffffffff) ? 16 : 2368;
    int                     m       = 0;
    int                     s       = 0;
    int                     f       = 0;
    uint32_t                lba     = sector;
    int                     ret;
    int                     data_len = 0;

    if (ioctl->is_dvd) {
        int track;

        data_len = 0;
        ret      = 0;

        if (lba == 0xffffffff) {
            lba   = ioctl->dev->seek_pos;
            track = ioctl_get_track(ioctl, lba);

            if (track != -1) {
                data_len = len;
                ret      = 1;
            }
        } else {
            len   = COOKED_SECTOR_SIZE;
            track = ioctl_get_track(ioctl, lba);

            if (track != -1) {
                ssize_t nread = pread(ioctl->fd, &(buffer[16]),
                                      COOKED_SECTOR_SIZE,
                                      (off_t) lba * COOKED_SECTOR_SIZE);
                if (nread > 0) {
                    data_len = (int) nread;
                    ret      = 1;
                }
            }
        }

        if (ret && (data_len >= len) && (track != -1)) {
            const raw_track_info_t *ct    = &(rti[track]);
            const uint32_t          start = (ct->pm * 60 * 75) + (ct->ps * 75) + ct->pf;

            m = s = f = 0;

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
        /* CD: use SG_IO with READ CD (0xBE). */
        uint8_t cdb[12];
        uint8_t sense[64];
        int     sense_len = 0;

        memset(cdb, 0, sizeof(cdb));
        cdb[0]  = 0xbe;                         /* READ CD */
        cdb[1]  = 0x00;
        cdb[2]  = (sector >> 24) & 0xff;
        cdb[3]  = (sector >> 16) & 0xff;
        cdb[4]  = (sector >> 8) & 0xff;
        cdb[5]  = sector & 0xff;                 /* Starting LBA */
        cdb[6]  = 0x00;
        cdb[7]  = 0x00;
        cdb[8]  = 0x01;                          /* Transfer Length = 1 */
        /* If sector is FFFFFFFF, only return the subchannel. */
        cdb[9]  = (sector == 0xffffffff) ? 0x00 : 0xf8;
        cdb[10] = 0x02;
        cdb[11] = 0x00;

#ifdef ENABLE_IOCTL_LOG
        ioctl_log(ioctl->log, "Host CDB: %02X %02X %02X %02X %02X %02X "
                  "%02X %02X %02X %02X %02X %02X\n",
                  cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5],
                  cdb[6], cdb[7], cdb[8], cdb[9], cdb[10], cdb[11]);
#endif

        memset(sense, 0, sizeof(sense));
        ret = sg_io_cmd(ioctl->fd, cdb, 12, buffer, len,
                        SG_DXFER_FROM_DEV, sense, &sense_len);

        ioctl_log(ioctl->log, "ioctl_read_sector: ret = %d, sense_len = %d\n",
                  ret, sense_len);

        if (sense_len >= 16) {
            if ((sense[2] == 0x03) && (sense[12] == 0x11))
                /* Treat this as an error to correctly indicate CIRC error to the guest. */
                ret = 0;
            ioctl_log(ioctl->log, "Host sense: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                      sense[0], sense[1], sense[2], sense[3],
                      sense[4], sense[5], sense[6], sense[7]);
            ioctl_log(ioctl->log, "            %02X %02X %02X %02X %02X %02X %02X %02X\n",
                      sense[8], sense[9], sense[10], sense[11],
                      sense[12], sense[13], sense[14], sense[15]);
        }

        ret = ret ? 1 : -1;
        data_len = len; /* sg_io_cmd handles this internally */
    }

    ioctl_log(ioctl->log, "ioctl_read_sector: final ret = %i\n", ret);

    /* Construct raw subchannel data from Q only. */
    if ((ret > 0) && !ioctl->is_dvd)
        for (int i = 11; i >= 0; i--)
            for (int j = 7; j >= 0; j--)
                buffer[2352 + (i * 8) + j] = ((buffer[sc_offs + i] >> (7 - j)) & 0x01) << 6;

    return ret;
}

static uint8_t
ioctl_get_track_type(const void *local, const uint32_t sector)
{
    ioctl_t                *ioctl = (ioctl_t *) local;
    int                     track = ioctl_get_track(ioctl, sector);
    raw_track_info_t       *rti   = (raw_track_info_t *) ioctl->cur_rti;
    const raw_track_info_t *trk   = &(rti[track]);
    uint8_t                 ret   = 0x00;

    if (ioctl_is_track_audio(ioctl, sector))
        ret = CD_TRACK_AUDIO;
    else if (track != -1)  for (int i = 0; i < ioctl->blocks_num; i++) {
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
    const ioctl_t    *ioctl = (const ioctl_t *) local;
    raw_track_info_t *rti   = (raw_track_info_t *) ioctl->cur_rti;
    uint32_t          lb    = 0;

    for (int i = (ioctl->blocks_num - 1); i >= 0; i--)
        if (rti[i].point == 0xa2) {
            lb = MSFtoLBA(rti[i].pm, rti[i].ps, rti[i].pf) - 151;
            break;
        }

    ioctl_log(ioctl->log, "LBCapacity=%d\n", lb);

    return lb;
}

static int
ioctl_read_dvd_structure(const void *local, const uint8_t layer, const uint8_t format,
                         uint8_t *buffer, uint32_t *info)
{
    const ioctl_t *ioctl = (const ioctl_t *) local;
    const int      len   = 2052;
    uint8_t        cdb[12];
    uint8_t        sense[64];
    int            sense_len = 0;

    memset(cdb, 0, sizeof(cdb));
    cdb[0]  = 0xad;             /* READ DVD STRUCTURE */
    cdb[6]  = layer;            /* Layer Number */
    cdb[7]  = format;           /* Format */
    cdb[8]  = 0x08;             /* Allocation Length high */
    cdb[9]  = 0x04;             /* Allocation Length low */

#ifdef ENABLE_IOCTL_LOG
    ioctl_log(ioctl->log, "Host CDB: %02X %02X %02X %02X %02X %02X "
              "%02X %02X %02X %02X %02X %02X\n",
              cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5],
              cdb[6], cdb[7], cdb[8], cdb[9], cdb[10], cdb[11]);
#endif

    memset(sense, 0, sizeof(sense));
    int ret = sg_io_cmd(ioctl->fd, cdb, 12, buffer, len,
                        SG_DXFER_FROM_DEV, sense, &sense_len);

    ioctl_log(ioctl->log, "ioctl_read_dvd_structure(): ret = %d, sense_len = %d\n",
              ret, sense_len);

    if (sense_len >= 16) {
        /* Return sense to the host as is. */
        ret = -((sense[2] << 16) | (sense[12] << 8) | sense[13]);
        if (info != NULL)
            *info = *(uint32_t *) &(sense[3]);
        ioctl_log(ioctl->log, "Host sense: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  sense[0], sense[1], sense[2], sense[3],
                  sense[4], sense[5], sense[6], sense[7]);
        ioctl_log(ioctl->log, "            %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  sense[8], sense[9], sense[10], sense[11],
                  sense[12], sense[13], sense[14], sense[15]);
    } else
        ret = ret ? 1 : 0;

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
    const ioctl_t *ioctl = (const ioctl_t *) local;
    uint8_t        cdb[12];
    uint8_t        sense[64];
    int            sense_len = 0;

    /* TEST UNIT READY */
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x00;

#ifdef ENABLE_IOCTL_LOG
    ioctl_log(ioctl->log, "Host CDB: %02X %02X %02X %02X %02X %02X "
              "%02X %02X %02X %02X %02X %02X\n",
              cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5],
              cdb[6], cdb[7], cdb[8], cdb[9], cdb[10], cdb[11]);
#endif

    memset(sense, 0, sizeof(sense));
    int ret = sg_io_cmd(ioctl->fd, cdb, 6, NULL, 0,
                        SG_DXFER_NONE, sense, &sense_len);

    ioctl_log(ioctl->log, "ioctl_is_empty(): ret = %d, sense_len = %d\n",
              ret, sense_len);

    if (sense_len >= 16) {
        /* Check for NOT READY + MEDIUM NOT PRESENT. */
        ret = ((sense[2] == SENSE_NOT_READY) && (sense[12] == ASC_MEDIUM_NOT_PRESENT));
        ioctl_log(ioctl->log, "Host sense: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  sense[0], sense[1], sense[2], sense[3],
                  sense[4], sense[5], sense[6], sense[7]);
        ioctl_log(ioctl->log, "            %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  sense[8], sense[9], sense[10], sense[11],
                  sense[12], sense[13], sense[14], sense[15]);
    } else if (!ret)
        ret = 1; /* SG_IO itself failed, assume empty */
    else
        ret = 0; /* No sense data and command succeeded = media present */

    return ret;
}

/* Disc change polling thread. */
static void *
ioctl_poll_thread(void *arg)
{
    ioctl_t *ioctl     = (ioctl_t *) arg;
    int      was_empty = ioctl_is_empty(ioctl);

    while (ioctl->poll_active) {
        sleep(2); /* Poll every 2 seconds. */
        if (!ioctl->poll_active)
            break;

        int now_empty = ioctl_is_empty(ioctl);
        if (now_empty != was_empty) {
            if (now_empty)
                cdrom_set_empty(ioctl->dev);
            else
                cdrom_update_status(ioctl->dev);
            was_empty = now_empty;
        }
    }

    return NULL;
}

static void
ioctl_close(void *local)
{
    ioctl_t *ioctl = (ioctl_t *) local;

    /* Stop the polling thread. */
    if (ioctl->poll_active) {
        ioctl->poll_active = 0;
        pthread_join(ioctl->poll_tid, NULL);
    }

    ioctl_close_handle(ioctl);
    ioctl->fd = -1;

    ioctl_log(ioctl->log, "Log closed\n");

    log_close(ioctl->log);
    ioctl->log = NULL;

    free(ioctl);
}

static void
ioctl_load(const void *local)
{
    ioctl_t *ioctl = (ioctl_t *) local;

    if ((ioctl->fd >= 0) || ioctl_open_handle(ioctl)) {
        /* Try to close the tray. */
        (void) sys_ioctl(ioctl->fd, CDROMCLOSETRAY, NULL);

        ioctl_read_raw_toc(ioctl);
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
        char n[1024] = { 0 };

        sprintf(n, "CD-ROM %i IOCtl", dev->id + 1);
        ioctl->log = log_open(n);

        memset(ioctl->path, 0x00, sizeof(ioctl->path));
        ioctl->fd = -1;

        /* drv is "ioctl:///dev/sr0", extract the path part. */
        snprintf(ioctl->path, sizeof(ioctl->path), "%s", &(drv[8]));
        ioctl_log(ioctl->log, "Path is %s\n", ioctl->path);

        ioctl->dev = dev;
        dev->ops   = &ioctl_ops;

        ioctl_load(ioctl);

        /* Start the disc change polling thread. */
        ioctl->poll_active = 1;
        if (pthread_create(&ioctl->poll_tid, NULL, ioctl_poll_thread, ioctl) != 0)
            ioctl->poll_active = 0;
    }

    return ioctl;
}
