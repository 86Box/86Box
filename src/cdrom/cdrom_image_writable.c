/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CD-R(W) writable image file handling module.
 *
 *          The guest burns a disc one sector at a time, but an Aaru image
 *          needs its size and tracks before its first sector is written, so
 *          sectors go to a spool file next to the image. This module serves
 *          the disc out of the spool for as long as it is in the drive, and
 *          every closed session rewrites the Aaru image from it.
 *
 * Authors: Nat Portillo, <claunia@claunia.com>
 *
 *          Copyright 2025 Nat Portillo.
 */

#define __STDC_FORMAT_MACROS
#include <ctype.h>
#include <inttypes.h>

#ifdef ENABLE_IMAGE_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/log.h>
#include <86box/plat.h>
#include <86box/bswap.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/cdrom_image_writable.h>

#define WIMAGE_SECTOR_SIZE  2352
#define WIMAGE_CAPACITY     0x00057da4    /* The free blocks of a blank 80-minute disc. */
#define WIMAGE_TRACK_GAP    152           /* Two run-out blocks and the pre-gap. */
#define WIMAGE_LEAD_OUT_1ST 6750          /* The first session's lead-out is longer. */
#define WIMAGE_LEAD_OUT     2250
#define WIMAGE_LEAD_IN      4500
#define WIMAGE_PREGAP       150
#define WIMAGE_PACKET_LINK  7             /* Run-out, link, and run-in blocks between packets. */
#define WIMAGE_PACKET_SIZE  32            /* The usual size for a formatted CD-RW. */

#define WIMAGE_MAX_TRACKS   99
#define WIMAGE_MAX_CUE      256
#define WIMAGE_MAX_TOC      (WIMAGE_MAX_TRACKS * 6)

/* What an image of an unfinished disc says about itself, as its comment. */
#define WIMAGE_STATE_FORMAT "86Box CD-R session=%i session_track=%i session_open=%i track_open=%i " \
                            "disc_closed=%i nwa=%i fixed_packet=%i"

/* A blank CD-RW's file, a blank CD-R's is empty. */
static const char wimage_blank_cdrw[] = "86Box blank CD-RW\n";

/* Written next to the spool, which is what makes it worth anything after a crash. */
static const char wimage_checkpoint_id[] = "86Box CD-R checkpoint 1";

extern uint8_t cdrom_scramble_table[2352];

/* A stretch of disc described by one cue sheet entry. */
typedef struct cue_area_t {
    int32_t start;
    int     data_block_type;
    int     generated;
} cue_area_t;

typedef struct cd_image_t {
    cdrom_t           *dev;
    FILE              *spool;
    void              *source;           /* The image, until something changes the disc. */
    int                rewritable;
    uint8_t            mcn[13];
    char               path[MAX_IMAGE_PATH_LEN];
    char               spool_path[MAX_IMAGE_PATH_LEN + 8];
    char               state_path[MAX_IMAGE_PATH_LEN + 16];

    aaru_write_track_t tracks[WIMAGE_MAX_TRACKS];
    int                tracks_num;
    int                session;          /* The session being, or to be, recorded. */
    int                session_track;    /* Index of that session's first track. */
    int                session_open;
    int                track_open;
    int                disc_closed;
    int32_t            nwa;
    int                dirty;            /* The spool has what the image does not. */
    uint32_t           reserved;         /* Size of the track that is reserved next. */
    int                packet_open;      /* Packets end at the next SYNCHRONIZE CACHE. */
    int32_t            fixed_packet;     /* Packet size of a formatted disc. */
    int                recording;        /* A session is arriving whole: a cue sheet or raw. */

    /* A cue sheet that has been sent, for as long as it is being recorded. */
    cue_area_t         cue[WIMAGE_MAX_CUE];
    int                cue_num;
    int32_t            cue_end;
    int32_t            cue_written;
    int                cue_tracks_num;   /* The track count from before the cue sheet. */

    raw_track_info_t   toc[WIMAGE_MAX_TOC];
    int                toc_num;
} cd_image_t;

static int     wimage_finalize(cd_image_t *img);

/* Unrecorded blocks of a formatted disc, which reads as if it was all written with zeros. */
static int
wimage_is_unrecorded(const uint8_t *buffer)
{
    static const uint8_t zero[12] = { 0 };

    return !memcmp(buffer, zero, sizeof(zero));
}

static int
wimage_spool_write(cd_image_t *img, const int32_t lba, const uint8_t *buffer)
{
    if ((img->spool == NULL) ||
        fseek(img->spool, (long) lba * WIMAGE_SECTOR_SIZE, SEEK_SET) ||
        (fwrite(buffer, 1, WIMAGE_SECTOR_SIZE, img->spool) != WIMAGE_SECTOR_SIZE))
        return -1;

    img->dirty = 1;

    return 0;
}

/* The disc moves into the spool the first time anything changes, not when it is mounted. */
static int
wimage_take_over(cd_image_t *img)
{
    uint8_t buffer[WIMAGE_SECTOR_SIZE];

    if (img->source == NULL)
        return 0;

    for (int i = 0; i < img->tracks_num; i++)
        for (int32_t lba = img->tracks[i].start; lba <= img->tracks[i].end; lba++)
            if (!aaru_image_source_read(img->source, lba, buffer) &&
                wimage_spool_write(img, lba, buffer))
                return -1;

    aaru_image_source_close(img->source);
    img->source = NULL;
    img->dirty  = 0;

    return 0;
}

static int
wimage_spool_read(void *priv, const int32_t lba, uint8_t *buffer)
{
    cd_image_t *img = (cd_image_t *) priv;
    size_t      len;

    if (img->source != NULL) {
        if (aaru_image_source_read(img->source, lba, buffer))
            memset(buffer, 0x00, WIMAGE_SECTOR_SIZE);
    } else {
        if ((img->spool == NULL) || fseek(img->spool, (long) lba * WIMAGE_SECTOR_SIZE, SEEK_SET))
            return -1;

        /* A sparse spool reads short past the last sector, that is unrecorded disc. */
        len = fread(buffer, 1, WIMAGE_SECTOR_SIZE, img->spool);
        if (len < WIMAGE_SECTOR_SIZE)
            memset(buffer + len, 0x00, WIMAGE_SECTOR_SIZE - len);
    }

    if (img->fixed_packet && wimage_is_unrecorded(buffer))
        cdrom_build_raw_sector(img->dev, buffer, lba, img->tracks[0].mode,
                               (img->tracks[0].mode == 2) ? 1 : 0, (img->tracks[0].mode == 2));

    return 0;
}

/* As above, for the image, where those blocks do not have to take up any room. */
static int
wimage_finalize_read(void *priv, const int32_t lba, uint8_t *buffer)
{
    cd_image_t *img = (cd_image_t *) priv;
    size_t      len;

    if (!img->fixed_packet)
        return wimage_spool_read(priv, lba, buffer);

    /* The spool is sparse here, it has what was recorded and nothing else. */
    if (fseek(img->spool, (long) lba * WIMAGE_SECTOR_SIZE, SEEK_SET))
        return -1;

    len = fread(buffer, 1, WIMAGE_SECTOR_SIZE, img->spool);

    return ((len < 12) || wimage_is_unrecorded(buffer)) ? 1 : 0;
}

static void
wimage_format_state(const cd_image_t *img, char *state, const size_t len)
{
    snprintf(state, len, WIMAGE_STATE_FORMAT, img->session, img->session_track, img->session_open,
             img->track_open, img->disc_closed, img->nwa, img->fixed_packet);
}

/* From here on, a crash loses nothing that the guest has seen go to the disc. */
static void
wimage_checkpoint(cd_image_t *img)
{
    char  state[128];
    char  temp[sizeof(img->state_path) + 4];
    FILE *fp;

    if ((img->source != NULL) || (img->spool == NULL) || !img->dirty)
        return;

    fflush(img->spool);

    snprintf(temp, sizeof(temp), "%s.tmp", img->state_path);

    fp = plat_fopen(temp, "w");
    if (fp == NULL)
        return;

    wimage_format_state(img, state, sizeof(state));

    fprintf(fp, "%s\n%s\n%i %i\n%.13s\n", wimage_checkpoint_id, state, img->rewritable,
            img->tracks_num, img->mcn[0] ? (const char *) img->mcn : "-");

    for (int i = 0; i < img->tracks_num; i++) {
        const aaru_write_track_t *t = &(img->tracks[i]);

        fprintf(fp, "%i %i %i %i %i %i %i %i %.12s\n", t->number, t->session, t->ctl, t->mode,
                t->packet, t->start, t->index1, t->end, t->isrc[0] ? t->isrc : "-");
    }

    fclose(fp);

    plat_remove(img->state_path);
    rename(temp, img->state_path);
}

static int
wimage_read_checkpoint(cd_image_t *img, int *rewritable)
{
    char  line[192];
    char  field[16];
    FILE *fp  = plat_fopen(img->state_path, "r");
    int   num = -1;
    int   ret = -1;

    if (fp == NULL)
        return -1;

    if ((fgets(line, sizeof(line), fp) == NULL) ||
        strncmp(line, wimage_checkpoint_id, sizeof(wimage_checkpoint_id) - 1) ||
        (fgets(line, sizeof(line), fp) == NULL) ||
        (sscanf(line, WIMAGE_STATE_FORMAT, &img->session, &img->session_track, &img->session_open,
                &img->track_open, &img->disc_closed, &img->nwa, &img->fixed_packet) != 7) ||
        (fscanf(fp, "%i %i %15s", rewritable, &num, field) != 3) ||
        (num < 0) || (num > WIMAGE_MAX_TRACKS))
        goto done;

    if (field[0] != '-')
        memcpy(img->mcn, field, sizeof(img->mcn));

    for (int i = 0; i < num; i++) {
        aaru_write_track_t *t = &(img->tracks[i]);
        int                 v[8];

        if (fscanf(fp, "%i %i %i %i %i %i %i %i %15s", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                   &v[6], &v[7], field) != 9)
            goto done;

        t->number  = v[0];
        t->session = v[1];
        t->ctl     = v[2];
        t->mode    = v[3];
        t->packet  = v[4];
        t->start   = v[5];
        t->index1  = v[6];
        t->end     = v[7];
        memset(t->isrc, 0x00, sizeof(t->isrc));
        if (field[0] != '-')
            memcpy(t->isrc, field, sizeof(t->isrc));
    }

    img->tracks_num = num;
    ret             = 0;

done:
    fclose(fp);

    return ret;
}

/* Records what a host does not send: gaps, and cue sheet areas generated by the drive. */
static int
wimage_generate(cd_image_t *img, const int32_t start, const int32_t end, const int mode)
{
    uint8_t buffer[WIMAGE_SECTOR_SIZE];

    for (int32_t lba = MAX(start, 0); lba < end; lba++) {
        memset(buffer, 0x00, sizeof(buffer));

        if (mode != 0)
            cdrom_build_raw_sector(img->dev, buffer, lba, mode, (mode == 2) ? 1 : 0, (mode == 2));

        if (wimage_spool_write(img, lba, buffer))
            return -1;
    }

    return 0;
}

static const aaru_write_track_t *
wimage_find_track(const cd_image_t *img, const int32_t lba)
{
    for (int i = 0; i < img->tracks_num; i++)
        if ((lba >= img->tracks[i].start) && (lba <= img->tracks[i].end))
            return &(img->tracks[i]);

    return NULL;
}

/* A track is on the disc as soon as the guest could find it in the TOC. */
static int
wimage_track_is_visible(const cd_image_t *img, const int i)
{
    if (img->tracks[i].end < img->tracks[i].index1)
        return 0;

    if (img->recording && (i >= img->cue_tracks_num))
        return 0;

    /* The readable part of an incremental track is all the guest wants of it. */
    return !(img->track_open && (i == (img->tracks_num - 1)) && !img->tracks[i].packet);
}

static void
wimage_toc_lba(uint8_t *m, uint8_t *s, uint8_t *f, const int32_t lba)
{
    const int msf = cdrom_lba_to_msf_accurate(lba);

    *m = (msf >> 16) & 0xff;
    *s = (msf >> 8) & 0xff;
    *f = msf & 0xff;
}

static raw_track_info_t *
wimage_toc_add(cd_image_t *img, const int session, const uint8_t adr_ctl, const uint8_t point)
{
    raw_track_info_t *rti = &(img->toc[img->toc_num++]);

    memset(rti, 0x00, sizeof(raw_track_info_t));

    rti->session = session;
    rti->adr_ctl = adr_ctl;
    rti->point   = point;

    return rti;
}

static void
wimage_build_toc(cd_image_t *img)
{
    img->toc_num = 0;

    for (int i = 0; i < img->tracks_num;) {
        const int session = img->tracks[i].session;
        int       first   = -1;
        int       last    = -1;
        int       j;

        for (j = i; (j < img->tracks_num) && (img->tracks[j].session == session); j++)
            if (wimage_track_is_visible(img, j)) {
                if (first == -1)
                    first = j;
                last = j;
            }

        if (first != -1) {
            const aaru_write_track_t *ft = &(img->tracks[first]);
            const aaru_write_track_t *lt = &(img->tracks[last]);
            raw_track_info_t         *rti;

            rti     = wimage_toc_add(img, session, 0x10 | ft->ctl, 0xa0);
            rti->pm = ft->number;
            rti->ps = (ft->mode == 2) ? 0x20 : 0x00;

            rti     = wimage_toc_add(img, session, 0x10 | lt->ctl, 0xa1);
            rti->pm = lt->number;

            rti     = wimage_toc_add(img, session, 0x10 | lt->ctl, 0xa2);
            wimage_toc_lba(&rti->pm, &rti->ps, &rti->pf, lt->end + 1);

            for (int k = first; k <= last; k++) {
                rti = wimage_toc_add(img, session, 0x10 | img->tracks[k].ctl, img->tracks[k].number);
                wimage_toc_lba(&rti->pm, &rti->ps, &rti->pf, img->tracks[k].index1);
            }

            /* A closed session the disc goes on after points at the next program area. */
            if ((j < img->tracks_num) || ((session < img->session) && !img->disc_closed)) {
                const int32_t next = (j < img->tracks_num) ? img->tracks[j].index1 : img->nwa;

                rti       = wimage_toc_add(img, session, 0x50, 0xb0);
                wimage_toc_lba(&rti->m, &rti->s, &rti->f, next);
                rti->zero = (session == 1) ? 2 : 1;
                rti->pm   = 0x4f;
                rti->ps   = 0x3b;
                rti->pf   = 0x47;

                if (session == 1) {
                    rti     = wimage_toc_add(img, session, 0x50, 0xc0);
                    rti->pm = 0x61;
                    rti->ps = 0x1a;
                    rti->pf = 0x42;
                }
            }
        }

        i = j;
    }
}

static int
wimage_get_track_info(const void *local, const uint32_t track,
                      const int end, track_info_t *ti)
{
    cd_image_t             *img  = (cd_image_t *) local;
    const raw_track_info_t *rti  = img->toc;
    int                     trk  = -1;
    int                     next = -1;

    wimage_build_toc(img);

    if ((track < 1) || (track >= 99))
        return 0;

    for (int i = 0; i < img->toc_num; i++)
        if (rti[i].point == track) {
            trk = i;
            break;
        }

    if (trk == -1)
        return 0;

    for (int i = 0; i < img->toc_num; i++)
        if ((rti[i].point == (track + 1)) && (rti[i].session == rti[trk].session)) {
            next = i;
            break;
        }

    if (next == -1)
        for (int i = 0; i < img->toc_num; i++)
            if ((rti[i].point == 0xa2) && (rti[i].session == rti[trk].session)) {
                next = i;
                break;
            }

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

    return 1;
}

static void
wimage_get_raw_track_info(const void *local, int *num, uint8_t *buffer)
{
    cd_image_t *img = (cd_image_t *) local;

    wimage_build_toc(img);

    *num = img->toc_num;
    memcpy(buffer, img->toc, img->toc_num * sizeof(raw_track_info_t));
}

static void
wimage_build_subchannel(const aaru_write_track_t *track, const int32_t lba, uint8_t *sub)
{
    const int32_t rel = lba - track->index1;
    uint8_t       q[12] = { 0 };
    int           m;
    int           s;
    int           f;

    q[0] = 0x01 | (track->ctl << 4);
    q[1] = bin2bcd(track->number);

    if (rel < 0) {
        /* The relative time counts down to index 1 in the pre-gap. */
        q[2] = 0;
        FRAMES_TO_MSF(-rel - 1, &m, &s, &f);
    } else {
        q[2] = 1;
        FRAMES_TO_MSF(rel, &m, &s, &f);
    }

    q[3] = bin2bcd(m);
    q[4] = bin2bcd(s);
    q[5] = bin2bcd(f);

    FRAMES_TO_MSF(lba + 150, &m, &s, &f);
    q[7] = bin2bcd(m);
    q[8] = bin2bcd(s);
    q[9] = bin2bcd(f);

    *(uint16_t *) &(q[10]) = bswap16(cdrom_crc16(0xffff, q, 10));

    for (int i = 0; i < 12; i++)
        for (int j = 0; j < 8; j++)
            sub[(i * 8) + j] = ((q[i] >> (7 - j)) & 0x01) << 6;
}

static int
wimage_read_sector(const void *local, uint8_t *buffer, const uint32_t sector)
{
    cd_image_t               *img = (cd_image_t *) local;
    const int32_t             lba = (sector == 0xffffffff) ? (int32_t) img->dev->seek_pos : (int32_t) sector;
    const aaru_write_track_t *track;

    memset(buffer, 0x00, 2448);

    track = wimage_find_track(img, lba);

    if ((track == NULL) || wimage_spool_read(img, lba, buffer))
        return -1;

    wimage_build_subchannel(track, lba, &(buffer[WIMAGE_SECTOR_SIZE]));

    return 1;
}

static uint8_t
wimage_get_track_type(const void *local, const uint32_t sector)
{
    const cd_image_t         *img   = (const cd_image_t *) local;
    const aaru_write_track_t *track = wimage_find_track(img, (int32_t) sector);

    if (track == NULL)
        return 0x00;

    if (track->mode == 0)
        return CD_TRACK_AUDIO;

    for (int i = 0; i < img->tracks_num; i++)
        if (img->tracks[i].session == track->session)
            return (img->tracks[i].mode == 2) ? 0x20 : 0x00;

    return 0x00;
}

static uint32_t
wimage_get_last_block(const void *local)
{
    const cd_image_t *img = (const cd_image_t *) local;
    int32_t           lb  = 0;

    for (int i = 0; i < img->tracks_num; i++)
        if (wimage_track_is_visible(img, i) && (img->tracks[i].end > lb))
            lb = img->tracks[i].end;

    return lb;
}

static int
wimage_read_dvd_structure(UNUSED(const void *local), UNUSED(const uint8_t layer),
                          UNUSED(const uint8_t format), UNUSED(uint8_t *buffer),
                          UNUSED(uint32_t *info))
{
    return 0;
}

static int
wimage_is_dvd(UNUSED(const void *local))
{
    return 0;
}

static int
wimage_has_audio(const void *local)
{
    const cd_image_t *img = (const cd_image_t *) local;

    for (int i = 0; i < img->tracks_num; i++)
        if (wimage_track_is_visible(img, i) && (img->tracks[i].mode == 0))
            return 1;

    return 0;
}

static int
wimage_has_data(const void *local)
{
    const cd_image_t *img = (const cd_image_t *) local;

    for (int i = 0; i < img->tracks_num; i++)
        if (wimage_track_is_visible(img, i) && (img->tracks[i].mode != 0))
            return 1;

    return 0;
}

/* Decodes a cue sheet Data Form byte, using the data block types of the write parameters page. */
static int
wimage_decode_data_form(const uint8_t form, int *data_block_type, int *mode, int *generated)
{
    const int sub = form & 0xc0;

    *generated = 0;

    switch (form & 0x3f) {
        default:
            return -1;
        case 0x00:
            *mode            = 0;
            *data_block_type = 0;
            break;
        case 0x01:
            *mode            = 0;
            *data_block_type = 0;
            *generated       = 1;
            break;
        case 0x10 ... 0x14:
            *mode            = 1;
            *data_block_type = 8;
            if (form & 0x01)
                *data_block_type |= 32;
            if (form & 0x06)
                *data_block_type |= 16;
            break;
        case 0x20 ... 0x23:
            *mode            = 2;
            *data_block_type = 13;
            if (form & 0x01)
                *data_block_type |= 32;
            if (form & 0x02)
                *data_block_type |= 16;
            break;
        case 0x24:
            *mode            = 2;
            *data_block_type = 0x1c;
            break;
        case 0x30 ... 0x34:
            *mode            = 2;
            *data_block_type = 10;
            if (form & 0x01)
                *data_block_type |= 32;
            if (form & 0x06)
                *data_block_type |= 16;
            break;
    }

    /* Sub-channel can only follow raw data, as that is all a data block type can say. */
    if (sub != 0x00) {
        if ((*data_block_type != 0) || *generated)
            return -1;

        *data_block_type = 2;
    }

    return 0;
}

static void
wimage_drop_cue(cd_image_t *img)
{
    if (img->recording) {
        img->tracks_num = img->cue_tracks_num;
        img->cue_num    = 0;
        img->recording  = 0;

        if (img->tracks_num == img->session_track)
            img->session_open = 0;
    }
}

static int
wimage_send_cuesheet(const void *local, const uint8_t *buffer, const int len)
{
    cd_image_t *img          = (cd_image_t *) local;
    int         tracks_num   = img->tracks_num;
    int         cue_num      = 0;
    int32_t     lead_out     = -1;
    int         first_track  = -1;
    int         mcn_pos      = 0;
    char        isrc[100][12];

    memset(isrc, 0x00, sizeof(isrc));

    if (wimage_take_over(img))
        return -1;

    if (img->disc_closed || img->track_open || (len <= 0) || (len % 8))
        return -1;

    if (img->session_open && (img->tracks_num != img->session_track))
        return -1;

    wimage_drop_cue(img);
    tracks_num = img->tracks_num;

    for (int i = 0; i < len; i += 8) {
        const uint8_t *e       = &(buffer[i]);
        const uint8_t  tno     = e[1];
        const uint8_t  index   = e[2];
        const int32_t  lba     = (((e[5] * 60) + e[6]) * 75) + e[7] - 150;
        int            dbt;
        int            mode;
        int            generated;

        /* Two entries each, for thirteen digits and for twelve characters. */
        if (((e[0] & 0x0f) == 0x02) && (mcn_pos < 13)) {
            const int n = MIN(13 - mcn_pos, 7 - (mcn_pos ? 1 : 0));

            memcpy(&(img->mcn[mcn_pos]), &(e[1]), n);
            mcn_pos += n;
        } else if (((e[0] & 0x0f) == 0x03) && (tno >= 1) && (tno <= 99)) {
            const int pos = isrc[tno][0] ? 6 : 0;

            memcpy(&(isrc[tno][pos]), &(e[2]), 6);
        }

        if ((e[0] & 0x0f) != 0x01)
            continue;

        if (tno == 0x00)
            continue;

        if (wimage_decode_data_form(e[3], &dbt, &mode, &generated))
            return -1;

        if (tno == 0xaa) {
            lead_out = lba;
            break;
        }

        if ((tno > 99) || (cue_num >= WIMAGE_MAX_CUE))
            return -1;

        img->cue[cue_num].start           = lba;
        img->cue[cue_num].data_block_type = dbt;
        img->cue[cue_num].generated       = generated;
        cue_num++;

        if ((first_track == -1) || (tno != img->tracks[tracks_num - 1].number)) {
            aaru_write_track_t *t;

            if ((first_track == -1) && (tno != (img->tracks_num + 1)))
                return -1;

            if (tracks_num >= WIMAGE_MAX_TRACKS)
                return -1;

            if (first_track == -1)
                first_track = tno;

            t          = &(img->tracks[tracks_num++]);
            t->number  = tno;
            t->session = img->session;
            t->ctl     = (e[0] >> 4) & 0x0f;
            t->mode    = mode;
            t->packet  = 0;
            memset(t->isrc, 0x00, sizeof(t->isrc));
            t->start   = MAX(lba, 0);
            t->index1  = (index == 0) ? -1 : lba;
            t->end     = -1;
        } else if (index == 1)
            img->tracks[tracks_num - 1].index1 = lba;
    }

    if ((lead_out <= 0) || (lead_out > WIMAGE_CAPACITY) || (first_track == -1) ||
        (img->cue[0].start < (img->nwa - WIMAGE_PREGAP))) {
        img->cue_num = 0;
        return -1;
    }

    for (int i = img->tracks_num; i < tracks_num; i++) {
        if (img->tracks[i].index1 < 0) {
            img->cue_num = 0;
            return -1;
        }

        img->tracks[i].end = ((i + 1) < tracks_num) ? (img->tracks[i + 1].start - 1) : (lead_out - 1);
        memcpy(img->tracks[i].isrc, isrc[img->tracks[i].number], sizeof(img->tracks[i].isrc));
    }

    img->cue_tracks_num = img->tracks_num;
    img->tracks_num     = tracks_num;
    img->cue_num        = cue_num;
    img->cue_end        = lead_out;
    img->cue_written    = -1;
    img->recording      = 1;
    img->session_open   = 1;

    for (int i = 0; i < cue_num; i++)
        if (img->cue[i].generated) {
            const int32_t             end   = ((i + 1) < cue_num) ? img->cue[i + 1].start : lead_out;
            const aaru_write_track_t *track = wimage_find_track(img, MAX(img->cue[i].start, 0));

            if (track == NULL)
                continue;

            if (wimage_generate(img, img->cue[i].start, end, track->mode))
                return -1;

            if ((end - 1) > img->cue_written)
                img->cue_written = end - 1;
        }

    return 0;
}

static int
wimage_get_sao_block_type(const void *local, const int32_t sector, int *data_block_type)
{
    const cd_image_t *img = (const cd_image_t *) local;

    if ((img->cue_num == 0) || (sector >= img->cue_end))
        return -1;

    for (int i = img->cue_num - 1; i >= 0; i--)
        if ((sector >= img->cue[i].start) && !img->cue[i].generated) {
            *data_block_type = img->cue[i].data_block_type;
            return 0;
        }

    return -1;
}

static void
wimage_open_track(cd_image_t *img, const int32_t lba, const int mode, const int ctl, const int packet)
{
    aaru_write_track_t *t = &(img->tracks[img->tracks_num]);

    t->number  = img->tracks_num + 1;
    t->session = img->session;
    t->ctl     = ctl & 0x0f;
    t->mode    = mode;
    t->packet  = packet;
    memset(t->isrc, 0x00, sizeof(t->isrc));
    t->start   = (img->session_open && (img->tracks_num > img->session_track)) ?
                 (img->tracks[img->tracks_num - 1].end + 1) : lba;
    t->index1  = lba;
    t->end     = lba - 1;

    img->tracks_num++;
    img->track_open   = 1;
    img->session_open = 1;

    (void) wimage_generate(img, t->start, lba, mode);
}

static int
wimage_write_sector(const void *local, const uint8_t *buffer, const int32_t sector,
                    const int write_type, const int mode, const int ctl)
{
    cd_image_t *img    = (cd_image_t *) local;
    const int   packet = (write_type == 0);

    if (wimage_take_over(img))
        return -1;

    /* A formatted disc is overwritten in place, and it can not be recorded on otherwise. */
    if (img->fixed_packet) {
        if (!packet || (sector < img->tracks[0].start) || (sector > img->tracks[0].end))
            return -1;

        img->tracks[0].mode = mode;

        return wimage_spool_write(img, sector, buffer);
    }

    if (img->disc_closed || (sector >= WIMAGE_CAPACITY))
        return -1;

    /* A cue sheet's first pre-gap is before LBA 0, and an Aaru disc does not start until there. */
    if (sector < 0)
        return ((write_type == 2) && img->recording) ? 0 : -1;

    if (write_type == 2) {
        if ((img->cue_num == 0) || !img->recording)
            return -1;

        if (wimage_spool_write(img, sector, buffer))
            return -1;

        if (sector > img->cue_written)
            img->cue_written = sector;

        return 0;
    }

    /* The next writable address moves past a track's gap, and never back. */
    if (!img->track_open) {
        if ((img->tracks_num >= WIMAGE_MAX_TRACKS) || (sector < img->nwa) || img->recording)
            return -1;

        wimage_open_track(img, sector, mode, ctl, packet);
    } else if (img->tracks[img->tracks_num - 1].packet != packet)
        return -1;
    else if (sector < (packet ? img->nwa : img->tracks[img->tracks_num - 1].index1))
        return -1;

    if (wimage_spool_write(img, sector, buffer))
        return -1;

    img->packet_open = packet;

    if (sector > img->tracks[img->tracks_num - 1].end)
        img->tracks[img->tracks_num - 1].end = sector;

    img->nwa = img->tracks[img->tracks_num - 1].end + 1;

    return 0;
}

/* Five six-bit characters, then seven digits packed two to a byte. */
static void
wimage_decode_q_isrc(const uint8_t *q, char *isrc)
{
    isrc[0] = 0x30 + (q[1] >> 2);
    isrc[1] = 0x30 + (((q[1] & 0x03) << 4) | (q[2] >> 4));
    isrc[2] = 0x30 + (((q[2] & 0x0f) << 2) | (q[3] >> 6));
    isrc[3] = 0x30 + (q[3] & 0x3f);
    isrc[4] = 0x30 + (q[4] >> 2);

    for (int i = 0; i < 7; i++)
        isrc[5 + i] = '0' + ((q[5 + (i >> 1)] >> ((i & 1) ? 0 : 4)) & 0x0f);
}

/* Raw recording: everything about the disc comes out of the Q sub-channel. */
static int
wimage_write_raw(const void *local, const uint8_t *buffer, const uint8_t *q, const int32_t sector)
{
    cd_image_t         *img = (cd_image_t *) local;
    const int           adr = q[0] & 0x0f;
    aaru_write_track_t *t;
    uint8_t             raw[WIMAGE_SECTOR_SIZE];

    if (wimage_take_over(img))
        return -1;

    if (img->disc_closed || img->fixed_packet || img->track_open ||
        (img->recording && img->cue_num))
        return -1;

    if (!img->recording) {
        if (sector > img->nwa)
            return -1;

        img->recording      = 1;
        img->cue_tracks_num = img->tracks_num;
        img->cue_written    = -1;
        img->cue_end        = 0;
    }

    if (adr == 2) {
        /* Thirteen digits, packed two to a byte. */
        for (int i = 0; i < 13; i++)
            img->mcn[i] = '0' + ((q[1 + (i >> 1)] >> ((i & 1) ? 0 : 4)) & 0x0f);
    } else if ((adr == 3) && (img->tracks_num > img->cue_tracks_num)) {
        wimage_decode_q_isrc(q, img->tracks[img->tracks_num - 1].isrc);
    }

    /* Everything past the start of the lead-out is the drive's to deal with. */
    if ((adr == 1) && (q[1] == 0xaa))
        img->cue_end = 1;

    if ((sector < 0) || img->cue_end || ((adr == 1) && (q[1] == 0x00)))
        return 0;

    if (sector >= WIMAGE_CAPACITY)
        return -1;

    if ((adr == 1) && ((img->tracks_num == img->cue_tracks_num) ||
                       (bcd2bin(q[1]) != img->tracks[img->tracks_num - 1].number))) {
        if ((bcd2bin(q[1]) != (img->tracks_num + 1)) || (img->tracks_num >= WIMAGE_MAX_TRACKS))
            return -1;

        t          = &(img->tracks[img->tracks_num]);
        t->number  = img->tracks_num + 1;
        t->session = img->session;
        t->ctl     = q[0] >> 4;
        t->mode    = 0;
        t->packet  = 0;
        memset(t->isrc, 0x00, sizeof(t->isrc));
        t->start   = (img->tracks_num > img->cue_tracks_num) ? (t[-1].end + 1) : sector;
        t->index1  = -1;
        t->end     = sector - 1;

        img->tracks_num++;
        img->session_open = 1;
    }

    /* Sectors that do not say where they are before the first one that does are lost. */
    if (img->tracks_num == img->cue_tracks_num)
        return 0;

    t = &(img->tracks[img->tracks_num - 1]);

    if ((adr == 1) && (q[2] != 0x00) && (t->index1 < 0))
        t->index1 = sector;

    memcpy(raw, buffer, WIMAGE_SECTOR_SIZE);

    if (t->ctl & 0x04) {
        /* Data comes scrambled off of most hosts, the sector time gives that away. */
        int m;
        int s;
        int f;

        FRAMES_TO_MSF(sector + 150, &m, &s, &f);

        if ((raw[12] != bin2bcd(m)) || (raw[13] != bin2bcd(s)) || (raw[14] != bin2bcd(f)))
            for (int i = 12; i < WIMAGE_SECTOR_SIZE; i++)
                raw[i] ^= cdrom_scramble_table[i];

        if (t->mode == 0)
            t->mode = raw[15] & 0x03;
    }

    if (wimage_spool_write(img, sector, raw))
        return -1;

    t->end           = sector;
    img->cue_written = sector;

    return 0;
}

static void
wimage_close_track(cd_image_t *img)
{
    aaru_write_track_t *t;

    if (!img->track_open)
        return;

    t = &(img->tracks[img->tracks_num - 1]);

    img->track_open  = 0;
    img->packet_open = 0;

    if (t->end < t->index1)
        img->tracks_num--;
    else {
        /* A reservation takes up its room whether or not it was used. */
        img->nwa = MAX(t->end + 1, (int32_t) (t->index1 + img->reserved)) + WIMAGE_TRACK_GAP;
    }

    img->reserved = 0;

    if (img->tracks_num == img->session_track)
        img->session_open = 0;

    wimage_checkpoint(img);
}

/* Ends the recording a cue sheet was sent for, keeping the tracks that made it to the disc. */
static void
wimage_end_cue(cd_image_t *img)
{
    while (img->tracks_num > img->cue_tracks_num) {
        aaru_write_track_t *t = &(img->tracks[img->tracks_num - 1]);

        if ((t->index1 >= 0) && (img->cue_written >= t->index1)) {
            if (t->end > img->cue_written)
                t->end = img->cue_written;
            break;
        }

        img->tracks_num--;
    }

    if (img->tracks_num > img->cue_tracks_num)
        img->nwa = img->tracks[img->tracks_num - 1].end + 1 + WIMAGE_TRACK_GAP;
    else if (img->tracks_num == img->session_track)
        img->session_open = 0;

    img->cue_num   = 0;
    img->recording = 0;
}

static int
wimage_close_session(cd_image_t *img, const int multi_session)
{
    int32_t end;

    wimage_close_track(img);

    if (img->recording)
        wimage_end_cue(img);

    if (!img->session_open) {
        /* Nothing left to record, but the guest may still be closing the disc. */
        if (!multi_session && (img->tracks_num > 0) && !img->disc_closed) {
            img->disc_closed = 1;
            return wimage_finalize(img);
        }

        return 0;
    }

    end = img->tracks[img->tracks_num - 1].end;

    img->nwa           = end + 1 + WIMAGE_LEAD_IN + WIMAGE_PREGAP +
                         ((img->session == 1) ? WIMAGE_LEAD_OUT_1ST : WIMAGE_LEAD_OUT);
    img->session_open  = 0;
    img->session++;
    img->session_track = img->tracks_num;

    if (!multi_session || (img->nwa >= WIMAGE_CAPACITY) || (img->tracks_num >= WIMAGE_MAX_TRACKS))
        img->disc_closed = 1;

    return wimage_finalize(img);
}

static int
wimage_write_to_toc(const void *local, const uint8_t track)
{
    cd_image_t *img = (cd_image_t *) local;

    if (track == CD_WTOC_ABORT) {
        wimage_drop_cue(img);
        return 0;
    }

    if (wimage_take_over(img))
        return -1;

    switch (track) {
        case CD_WTOC_CLOSE_DISC:
            return wimage_close_session(img, 0);
        case CD_WTOC_CLOSE_SESSION:
            return wimage_close_session(img, 1);
        default:
            wimage_close_track(img);
            return 0;
    }
}

static int
wimage_flush(const void *local, const int multi_session)
{
    cd_image_t *img = (cd_image_t *) local;

    if ((multi_session & 0x0f) == 0x01)
        wimage_close_track(img);

    if (img->recording && (img->cue_written >= 0))
        return wimage_close_session(img, multi_session & 0x80);

    /* The drive writes run-out, link, and run-in blocks after every packet. */
    if (img->packet_open) {
        const int32_t nwa = img->nwa;

        img->packet_open = 0;
        img->nwa         = nwa + WIMAGE_PACKET_LINK;

        if (wimage_generate(img, nwa, img->nwa, img->tracks[img->tracks_num - 1].mode))
            return -1;
    }

    /* Formatted discs and incremental tracks can go on like this for as long as they like. */
    wimage_checkpoint(img);

    return 0;
}

/* Turns the disc back to the way it came, the image file included. */
static int
wimage_reset(cd_image_t *img)
{
    FILE *fp;

    if (img->source != NULL) {
        aaru_image_source_close(img->source);
        img->source = NULL;
    }

    if (img->spool != NULL)
        fclose(img->spool);

    img->spool = plat_fopen(img->spool_path, "wb+");

    plat_remove(img->state_path);

    memset(img->mcn, 0x00, sizeof(img->mcn));

    img->tracks_num    = 0;
    img->session       = 1;
    img->session_track = 0;
    img->session_open  = 0;
    img->track_open    = 0;
    img->packet_open   = 0;
    img->disc_closed   = 0;
    img->recording     = 0;
    img->cue_num       = 0;
    img->fixed_packet  = 0;
    img->reserved      = 0;
    img->nwa           = 0;
    img->dirty         = 0;

    fp = plat_fopen(img->path, "wb");
    if (fp != NULL) {
        if (img->rewritable)
            fputs(wimage_blank_cdrw, fp);

        fclose(fp);
    }

    return ((fp == NULL) || (img->spool == NULL)) ? -1 : 0;
}

/* Where the program area goes on after a session. */
static int32_t
wimage_after_session(const int32_t end, const int session)
{
    return end + 1 + WIMAGE_LEAD_IN + WIMAGE_PREGAP +
           ((session == 1) ? WIMAGE_LEAD_OUT_1ST : WIMAGE_LEAD_OUT);
}

static int
wimage_blank(const void *local, const int type, const uint32_t lba)
{
    cd_image_t *img = (cd_image_t *) local;
    int         session;
    int         first;

    /* Dropping a reservation works on any disc, that is not recorded yet. */
    if (type == 3) {
        img->reserved = 0;
        return 0;
    }

    if (!img->rewritable)
        return -1;

    /* The whole disc, or the minimum that makes it look blank. */
    if ((type == 0) || (type == 1))
        return wimage_reset(img);

    /* Only what is at the end of a disc that is not formatted can be taken apart. */
    if ((type < 2) || (type > 6) || (img->tracks_num == 0) || img->fixed_packet ||
        img->track_open || img->recording || wimage_take_over(img))
        return -1;

    session = img->tracks[img->tracks_num - 1].session;

    for (first = img->tracks_num; (first > 0) && (img->tracks[first - 1].session == session); first--)
        ;

    /* Cut the last track short. */
    if (type == 4) {
        aaru_write_track_t *t = &(img->tracks[img->tracks_num - 1]);

        if (((int32_t) lba <= t->index1) || ((int32_t) lba > t->end))
            return -1;

        t->end             = lba - 1;
        img->session       = session;
        img->session_track = first;
        img->session_open  = 1;
        img->disc_closed   = 0;
        img->nwa           = t->end + 1 + WIMAGE_TRACK_GAP;

        return wimage_finalize(img);
    }

    /* Blank the last track, the one a session is made of counts as the session. */
    if ((type == 2) && ((img->tracks_num - 1) > first)) {
        img->tracks_num--;
        img->session       = session;
        img->session_track = first;
        img->session_open  = 1;
        img->disc_closed   = 0;
        img->nwa           = img->tracks[img->tracks_num - 1].end + 1 + WIMAGE_TRACK_GAP;

        return wimage_finalize(img);
    }

    /* Unclose the last session. */
    if (type == 5) {
        if (img->session_open)
            return -1;

        img->session       = session;
        img->session_track = first;
        img->session_open  = 1;
        img->disc_closed   = 0;
        img->nwa           = img->tracks[img->tracks_num - 1].end + 1 + WIMAGE_TRACK_GAP;

        return wimage_finalize(img);
    }

    /* Erase the last session. */
    if (first == 0)
        return wimage_reset(img);

    img->tracks_num    = first;
    img->session       = session;
    img->session_track = first;
    img->session_open  = 0;
    img->disc_closed   = 0;
    img->nwa           = wimage_after_session(img->tracks[first - 1].end, session - 1);

    return wimage_finalize(img);
}

static int
wimage_format(const void *local, const uint32_t blocks, const uint32_t packet_size, const int grow)
{
    cd_image_t         *img    = (cd_image_t *) local;
    const uint32_t      packet = packet_size ? packet_size : WIMAGE_PACKET_SIZE;
    const uint32_t      max    = (WIMAGE_CAPACITY / (packet + WIMAGE_PACKET_LINK)) * packet;
    aaru_write_track_t *t      = &(img->tracks[0]);

    /* What is on the disc stays, it just goes on for longer. */
    if (grow) {
        if (!img->fixed_packet || (blocks > max) || ((blocks != 0) && ((int32_t) blocks < (t->end + 1))) ||
            wimage_take_over(img))
            return -1;

        t->end = (blocks ? blocks : max) - 1;

        return wimage_finalize(img);
    }

    if (!img->rewritable || (blocks > max) || (packet > 0xffff) || wimage_reset(img))
        return -1;

    t->number  = 1;
    t->session = 1;
    t->ctl     = 0x07;    /* Data, recorded incrementally. */
    t->mode    = 1;
    t->packet  = 1;
    t->start   = 0;
    t->index1  = 0;
    t->end     = (blocks ? blocks : max) - 1;

    img->tracks_num    = 1;
    img->session       = 2;
    img->session_track = 1;
    img->disc_closed   = 1;
    img->fixed_packet  = packet;
    img->nwa           = WIMAGE_CAPACITY;
    img->dirty         = 1;

    return wimage_finalize(img);
}

static int
wimage_reserve_track(const void *local, const uint32_t size)
{
    cd_image_t *img = (cd_image_t *) local;

    /* A reservation is only useful to the track that gets recorded right after it. */
    if (img->disc_closed || img->track_open || img->recording || img->reserved ||
        (size == 0) || (size > (WIMAGE_CAPACITY - img->nwa)))
        return -1;

    img->reserved = size;

    return 0;
}

static void
wimage_get_write_info(const void *local, cdrom_write_info_t *wi)
{
    const cd_image_t *img = (const cd_image_t *) local;
    int32_t           nwa = img->nwa;

    memset(wi, 0x00, sizeof(cdrom_write_info_t));

    if (img->disc_closed)
        wi->state = CD_WSTATE_CLOSED;
    else if ((img->recording ? img->cue_tracks_num : img->tracks_num) == 0)
        wi->state = CD_WSTATE_BLANK;
    else
        wi->state = CD_WSTATE_APPENDABLE;

    wi->session_open   = img->session_open;
    wi->sessions       = img->session;
    wi->ls_first_track = img->session_track + 1;
    wi->next_track     = img->tracks_num + (img->track_open ? 0 : 1);
    wi->track_start    = img->track_open ? img->tracks[img->tracks_num - 1].index1 : nwa;
    wi->nwa            = nwa;
    wi->free_blocks    = (nwa < WIMAGE_CAPACITY) ? (WIMAGE_CAPACITY - nwa) : 0;
    wi->lead_in_start  = nwa + 150 - WIMAGE_PREGAP - WIMAGE_LEAD_IN;
    wi->track_packet   = img->track_open && img->tracks[img->tracks_num - 1].packet;
    wi->fixed_packet   = img->fixed_packet;
}

static int
wimage_finalize(cd_image_t *img)
{
    aaru_write_track_t tracks[WIMAGE_MAX_TRACKS];
    uint8_t            toc[2 + (WIMAGE_MAX_TOC * sizeof(raw_track_info_t))];
    char               state[128];
    int                tracks_num = 0;
    int                sessions   = 0;
    int                ret;

    if (wimage_take_over(img))
        return -1;

    wimage_build_toc(img);

    for (int i = 0; i < img->tracks_num; i++)
        if (wimage_track_is_visible(img, i)) {
            tracks[tracks_num++] = img->tracks[i];
            if (img->tracks[i].session > sessions)
                sessions = img->tracks[i].session;
        }

    if (tracks_num == 0)
        return 0;

    wimage_format_state(img, state, sizeof(state));

    /* Should this not make it, the guest has still been told that the disc is good. */
    img->dirty = 1;
    wimage_checkpoint(img);

    /* This tag has the TOC's own header, without the data length that is ahead of it. */
    toc[0] = 1;
    toc[1] = sessions;
    memcpy(&(toc[2]), img->toc, img->toc_num * sizeof(raw_track_info_t));

    ret = aaru_image_write_disc(img->path, img->rewritable, state, tracks, tracks_num, toc,
                                2 + (img->toc_num * sizeof(raw_track_info_t)), img->mcn,
                                wimage_finalize_read, img);

    if (ret)
        warning("CD-R: Could not write image %s\n", img->path);
    else {
        img->dirty = 0;
        plat_remove(img->state_path);
    }

    return ret;
}

static void
wimage_close(void *local)
{
    cd_image_t *img = (cd_image_t *) local;
    int         ret = 0;

    /* A burn that was never finished still gets to keep what made it to the disc. */
    if (img->dirty) {
        /* Incremental tracks stay open from one time in the drive to the next. */
        if (img->track_open && !img->tracks[img->tracks_num - 1].packet)
            wimage_close_track(img);

        if (img->recording)
            wimage_end_cue(img);

        ret = wimage_finalize(img);
    }

    if (img->source != NULL)
        aaru_image_source_close(img->source);

    if (img->spool != NULL)
        fclose(img->spool);

    /* If the image could not be written, the spool is all that is left of the burn. */
    if (!ret) {
        plat_remove(img->spool_path);
        plat_remove(img->state_path);
    }

    free(img);
}

static int
wimage_get_mcn(const void *local, uint8_t *mcn)
{
    const cd_image_t *img = (const cd_image_t *) local;

    if (img->mcn[0] == 0)
        return 0;

    memcpy(mcn, img->mcn, sizeof(img->mcn));

    return 1;
}

static const cdrom_ops_t image_ops = {
    wimage_get_track_info,
    wimage_get_raw_track_info,
    wimage_read_sector,
    wimage_get_track_type,
    wimage_get_last_block,
    wimage_read_dvd_structure,
    wimage_is_dvd,
    wimage_has_audio,
    wimage_has_data,
    NULL,
    wimage_send_cuesheet,
    wimage_write_sector,
    wimage_write_to_toc,
    wimage_close,
    NULL,
    wimage_get_mcn,
    wimage_get_write_info,
    wimage_get_sao_block_type,
    wimage_flush,
    wimage_write_raw,
    wimage_blank,
    wimage_format,
    wimage_reserve_track
};

/* Public functions. */

/*
   Until something is burned, the image is a placeholder file, which is what lets a
   blank disc come back after a hard reset or restart, like any other medium.
 */
int
wimage_get_blank_file_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    char        buffer[sizeof(wimage_blank_cdrw)];
    FILE       *fp;
    size_t      len;

    if ((ext == NULL) || (strcasecmp(ext, ".aaruf") && strcasecmp(ext, ".aaruformat")))
        return CD_BLANK_NOT;

    fp = plat_fopen(path, "rb");
    if (fp == NULL)
        return CD_BLANK_NOT;

    len = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);

    if (len == 0)
        return CD_BLANK_CDR;

    if ((len == (sizeof(wimage_blank_cdrw) - 1)) &&
        !memcmp(buffer, wimage_blank_cdrw, len))
        return CD_BLANK_CDRW;

    return CD_BLANK_NOT;
}

static cd_image_t *
wimage_alloc(cdrom_t *dev, const char *path, const int rewritable)
{
    cd_image_t *img = (cd_image_t *) calloc(1, sizeof(cd_image_t));

    if (img == NULL)
        return NULL;

    img->dev        = dev;
    img->session    = 1;
    img->rewritable = rewritable;
    snprintf(img->path, sizeof(img->path), "%s", path);
    snprintf(img->spool_path, sizeof(img->spool_path), "%s.spool", path);
    snprintf(img->state_path, sizeof(img->state_path), "%s.spool.state", path);

    return img;
}

void *
wimage_open(cdrom_t *dev, const char *path, const int type)
{
    cd_image_t *img;

    if ((path == NULL) || (path[0] == 0))
        return NULL;

    if (!aaru_image_writer_available()) {
        warning(plat_get_string(STRING_CDROM_LOAD_AARU_ERROR), path);
        return NULL;
    }

    img = wimage_alloc(dev, path, (type == CD_BLANK_CDRW));
    if (img == NULL)
        return NULL;

    if (wimage_reset(img)) {
        warning("CD-R: Could not create %s\n", img->spool_path);
        if (img->spool != NULL)
            fclose(img->spool);
        free(img);
        return NULL;
    }

    dev->ops = &image_ops;

    return img;
}

/* Rewritable discs, and write-once ones with room left, can go on being recorded. */
int
wimage_can_resume(const char *path)
{
    char  state[128];
    char  checkpoint[MAX_IMAGE_PATH_LEN + 16];
    int   rewritable = 0;
    int   closed     = 1;
    FILE *fp;

    /* What was left behind by a crash comes first, whatever the image still says. */
    snprintf(checkpoint, sizeof(checkpoint), "%s.spool.state", path);
    fp = plat_fopen(checkpoint, "r");
    if (fp != NULL) {
        fclose(fp);
        return 1;
    }

    if (!cdrom_image_is_aaru(path) || aaru_image_get_disc_state(path, &rewritable, state, sizeof(state)))
        return 0;

    if (sscanf(state, "86Box CD-R session=%*i session_track=%*i session_open=%*i track_open=%*i "
                      "disc_closed=%i", &closed) != 1)
        return 0;

    return rewritable || !closed;
}

void *
wimage_resume(cdrom_t *dev, const char *path, int *type)
{
    cd_image_t *img;
    char        state[128];
    int         rewritable = 0;
    int         tracks_num = WIMAGE_MAX_TRACKS;

    img = wimage_alloc(dev, path, 0);
    if (img == NULL)
        return NULL;

    if (!wimage_read_checkpoint(img, &rewritable)) {
        /* The spool goes on from where the crash left it. */
        img->spool = plat_fopen(img->spool_path, "rb+");
        if (img->spool != NULL) {
            img->rewritable = rewritable;
            img->dirty      = 1;
            goto opened;
        }
    }

    if (aaru_image_get_disc_state(path, &rewritable, state, sizeof(state)) ||
        (sscanf(state, WIMAGE_STATE_FORMAT, &img->session, &img->session_track, &img->session_open,
                &img->track_open, &img->disc_closed, &img->nwa, &img->fixed_packet) != 7) ||
        ((img->source = aaru_image_source_open(path, img->tracks, &tracks_num, img->mcn)) == NULL) ||
        ((img->spool = plat_fopen(img->spool_path, "wb+")) == NULL)) {
        if (img->source != NULL)
            aaru_image_source_close(img->source);
        free(img);
        return NULL;
    }

    img->rewritable = rewritable;
    img->tracks_num = tracks_num;

    if (img->track_open && tracks_num)
        img->tracks[tracks_num - 1].packet = 1;

opened:
    *type    = img->rewritable ? CD_BLANK_CDRW : CD_BLANK_CDR;
    dev->ops = &image_ops;

    return img;
}
