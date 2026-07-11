#include "libaaruformat/include/aaruformat.h"

#define __STDC_FORMAT_MACROS
#include <ctype.h>
#include <inttypes.h>
#ifdef ENABLE_IMAGE_LOG
#    include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>
#include <zlib.h>
#include <sys/stat.h>
#ifndef _WIN32
#    include <libgen.h>
#endif
#include <86box/86box.h>
#include <86box/log.h>
#include <86box/nvr.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/cdrom_image_viso.h>

/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#define MSFtoLBA(m, s, f) ((((m * 60) + s) * 75) + f)

typedef struct aaru_image_t {
    cdrom_t *dev;
    void    *aaruf_context;
    uint8_t *full_toc; // generated if it does not exist.
    size_t   full_toc_size;
    bool     is_dvd;

    ImageInfo img_info;

    TrackEntry *track_entries;
    uint32_t    track_size;
} aaru_image_t;

/* Shared functions. */
static int
aaru_image_get_track_info(UNUSED(const void *local), UNUSED(const uint32_t track),
                          UNUSED(int end), UNUSED(track_info_t *ti))
{
    const aaru_image_t     *ioctl      = (const aaru_image_t *) local;
    const raw_track_info_t *rti        = (const raw_track_info_t *) (ioctl->full_toc + 4);
    int                     ret        = 1;
    int                     trk        = -1;
    int                     next       = -1;
    int                     blocks_num = (ioctl->full_toc_size - 4) / 11;

    if ((track >= 1) && (track < 99))
        for (int i = 0; i < blocks_num; i++)
            if (rti[i].point == track) {
                trk = i;
                break;
            }

    if ((track >= 1) && (track < 98))
        for (int i = 0; i < blocks_num; i++)
            if ((rti[i].point == (track + 1)) && (rti[i].session == rti[trk].session)) {
                next = i;
                break;
            }

    if ((track >= 1) && (track < 99) && (trk != -1) && (next == -1))
        for (int i = 0; i < blocks_num; i++)
            if ((rti[i].point == 0xa2) && (rti[i].session == rti[trk].session)) {
                next = i;
                break;
            }

    if ((track == 0xaa) || (trk == -1)) {
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
    }

    return ret;
}

static void
aaru_image_get_raw_track_info(UNUSED(const void *local), int *num, uint8_t *rti)
{
    aaru_image_t *ioctl = (aaru_image_t *) local;

    *num = (ioctl->full_toc_size - 4) / 11;
    memcpy(rti, ioctl->full_toc + 4, *num * 11);

    raw_track_info_t* raw_track_info = (raw_track_info_t*)rti;
    for (int i = 0; i < *num; i++) {
        pclog("======================================\n");
        pclog("Track %d\n", i);
        pclog("======================================\n");
        pclog("Session: %d\n", raw_track_info[i].session);
        pclog("ADR/CTL: 0x%02X\n", raw_track_info[i].adr_ctl);
        pclog("Point: %d\n", raw_track_info[i].point);
        pclog("M:S:F: %02d:%02d:%02d (%02X:%02X:%02X, hex)\n", raw_track_info[i].m, raw_track_info[i].s, raw_track_info[i].f, raw_track_info[i].m, raw_track_info[i].s, raw_track_info[i].f);
        pclog("Zero: 0x%02X\n", raw_track_info[i].zero);
        pclog("PM:PS:PF: %d:%d:%d (%X:%X:%X, hex)\n", raw_track_info[i].pm, raw_track_info[i].ps, raw_track_info[i].pf, raw_track_info[i].pm, raw_track_info[i].ps, raw_track_info[i].pf);
        pclog("======================================\n");
    }
}

static int
aaru_image_get_track(const aaru_image_t *ioctl, const uint32_t sector)
{
    raw_track_info_t *rti    = (raw_track_info_t *) (ioctl->full_toc + 4);
    int               track  = -1;
    int               tracks = (ioctl->full_toc_size - 4) / 11;

    for (int i = (tracks - 1); i >= 0; i--) {
        const raw_track_info_t *ct    = &(rti[i]);
        const uint32_t          start = (ct->pm * 60 * 75) + (ct->ps * 75) + ct->pf - 150;

        if ((ct->point >= 1) && (ct->point <= 99) && (sector >= start)) {
            track = i;
            break;
        }
    }

    return track;
}

static int
aaru_image_read_sector(const void *local, UNUSED(uint8_t *buffer), UNUSED(uint32_t const sector))
{
    aaru_image_t           *ioctl         = (aaru_image_t *) local;
    uint8_t                 sector_status = 0;
    uint64_t                lba           = sector;
    uint32_t                length        = 2352;
    uint32_t                track         = aaru_image_get_track(local, sector == ~0u ? ioctl->dev->seek_pos : sector);
    raw_track_info_t       *rti           = (raw_track_info_t *) (ioctl->full_toc + 4);
    const raw_track_info_t *ct            = &(rti[track]);
    const uint32_t          start         = (ct->pm * 60 * 75) + (ct->ps * 75) + ct->pf;
    int                     m, s, f;

    memset(buffer, 0, 2448);

    if (sector == 0xFFFFFFFF)
        lba = ioctl->dev->seek_pos;

    track = aaru_image_get_track(local, lba);

    if (aaruf_read_sector_long(ioctl->aaruf_context, lba, false, buffer, &length, &sector_status)) {
        length = 2048;
        if (!aaruf_read_sector(ioctl->aaruf_context, lba, false, buffer, &length, &sector_status))
            goto generate_headers;
        return -1;
    }

    length = 96;
    if (aaruf_read_sector_tag(ioctl->aaruf_context, lba, false, &buffer[2352], &length, kSectorTagCdSubchannel)) {
generate_headers:
        m = s = f = 0;
        if (length == 96)
            goto generate_subchannel;
        /* Construct sector header and sub-header. */
        {
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

generate_subchannel:
        /* Construct Q. */
        buffer[2352 + 0] = (ct->adr_ctl >> 4) | ((ct->adr_ctl & 0xf) << 4);
        buffer[2352 + 1] = bin2bcd(ct->point);
        buffer[2352 + 2] = 1;
        FRAMES_TO_MSF((int32_t) (lba + 150 - start), &m, &s, &f);
        buffer[2352 + 3] = bin2bcd(m);
        buffer[2352 + 4] = bin2bcd(s);
        buffer[2352 + 5] = bin2bcd(f);
        FRAMES_TO_MSF(lba + 150, &m, &s, &f);
        buffer[2352 + 7] = bin2bcd(m);
        buffer[2352 + 8] = bin2bcd(s);
        buffer[2352 + 9] = bin2bcd(f);
        for (int i = 11; i >= 0; i--)
            for (int j = 7; j >= 0; j--)
                buffer[2352 + (i * 8) + j] = ((buffer[2352 + i] >> (7 - j)) & 0x01) << 6;
    }

    return 1;
}

static uint32_t
aaru_image_get_last_block(const void *local)
{
    aaru_image_t *ioctl = (aaru_image_t *) local;

    return ioctl->img_info.Sectors - 1;
}

static int
aaru_image_read_dvd_structure(UNUSED(const void *local), UNUSED(const uint8_t layer), UNUSED(const uint8_t format),
                              UNUSED(uint8_t *buffer), UNUSED(uint32_t *info))
{
    // FIXME: How to do this?
    return 0;
}

static int
aaru_image_is_dvd(UNUSED(const void *local))
{
    aaru_image_t *img = (aaru_image_t *) local;
    return img->img_info.MediaType >= DVDROM && img->img_info.MediaType <= DVDDownload;
}

static int
aaru_image_has_audio(UNUSED(const void *local))
{
    aaru_image_t *img = (aaru_image_t *) local;
    for (unsigned int i = 0; i < img->track_size; i++) {
        if (img->track_entries[i].type == kTrackTypeAudio)
            return 1;
    }
    return 0;
}

static void
aaru_image_close(void *local)
{
    aaru_image_t *img = local;
    if (img->track_entries)
        free(img->track_entries);
    if (img->full_toc)
        free(img->full_toc);
    if (img->aaruf_context)
        aaruf_close(img->aaruf_context);
    free(img);
}


static int
aaru_image_track_audio(const aaru_image_t *ioctl, const uint32_t pos)
{
    raw_track_info_t *      rti   = (raw_track_info_t *) (ioctl->full_toc + 4);
    int                     ret     = 0;

    if (!ioctl->is_dvd) {
        const int track   = aaru_image_get_track(ioctl, pos);
        const int control = rti[track].adr_ctl;

        ret     = !(control & 0x04);
    }

    return ret;
}

static uint8_t
aaru_image_get_track_type(const void *local, const uint32_t sector)
{
    aaru_image_t *          ioctl = (aaru_image_t *) local;
    int                     track = aaru_image_get_track(ioctl, sector);
    raw_track_info_t *      rti   = (raw_track_info_t *) (ioctl->full_toc + 4);
    const raw_track_info_t *trk   = &(rti[track]);
    uint8_t                 ret   = 0x00;

    if (aaru_image_track_audio(ioctl, sector))
        ret = CD_TRACK_AUDIO;
    else  if (track != -1)  for (int i = 0; i < (ioctl->full_toc_size - 4) / 11; i++) {
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

static const cdrom_ops_t aaru_image_ops = {
    aaru_image_get_track_info,
    aaru_image_get_raw_track_info,
    aaru_image_read_sector,
    aaru_image_get_track_type,
    aaru_image_get_last_block,
    aaru_image_read_dvd_structure,
    aaru_image_is_dvd,
    aaru_image_has_audio,
    NULL,
    aaru_image_close,
    NULL
};

static raw_track_info_t *
aaru_image_allocate_track(aaru_image_t *img)
{
    img->full_toc                    = realloc(img->full_toc, img->full_toc_size + sizeof(raw_track_info_t));
    raw_track_info_t *raw_track_info = (raw_track_info_t *) (img->full_toc + img->full_toc_size);
    memset(raw_track_info, 0, sizeof(raw_track_info_t));
    img->full_toc_size += sizeof(raw_track_info_t);
    return raw_track_info;
}

/* Public functions */
void *
aaru_image_open(cdrom_t *dev, const char *path)
{
    int32_t       cur_sess = -1;
    aaru_image_t *img      = (aaru_image_t *) calloc(1, sizeof(aaru_image_t));

    if (img) {
        img->dev           = dev;
        img->aaruf_context = aaruf_open(path, false, NULL);
        if (img->aaruf_context) {
            uint32_t length       = 0;
            ptrdiff_t offset_lead  = 0;
            size_t   large_length = 0;
            int32_t  res          = 0;

            if (aaruf_get_image_info(img->aaruf_context, &img->img_info)) {
                pclog("Failed to get Aaru image info\n");
                goto cleanup_error;
            }

            if (img->img_info.MetadataMediaType != OpticalDisc) {
                pclog("Aaru image is not a optical disc image\n");
                goto cleanup_error;
            }

            res = aaruf_read_media_tag(img->aaruf_context, NULL, kMediaTagFullToc, &length);

            if (res == AARUF_ERROR_BUFFER_TOO_SMALL && length != 0) {
                img->full_toc = calloc(1, length);
                res           = aaruf_read_media_tag(img->aaruf_context, img->full_toc, kMediaTagFullToc, &length);
                if (!res)
                    goto toc_skip;
            }
            // Start generating the full TOC.
            res = aaruf_get_tracks(img->aaruf_context, NULL, &large_length);

            if (res == AARUF_ERROR_BUFFER_TOO_SMALL) {
                img->track_entries = calloc(1, large_length);
                if (!(res = aaruf_get_tracks(img->aaruf_context, (uint8_t *) img->track_entries, &large_length))) {
                    img->track_size = large_length / sizeof(TrackEntry);
                } else {
                    pclog("Failed to allocate tracks for Aaru images (1)\n");
                    goto cleanup_error;
                }
            }

            img->full_toc      = calloc(4 + sizeof(raw_track_info_t) * 3, 1);
            img->full_toc_size = 4 + sizeof(raw_track_info_t) * 3;

            raw_track_info_t *start_track_info = (raw_track_info_t *) (img->full_toc + 4);
            offset_lead = 4;

            start_track_info[0].point = 0xA0;
            start_track_info[1].point = 0xA1;
            start_track_info[2].point = 0xA2;

            int64_t first_track_sess = (int64_t) LLONG_MAX;
            int64_t last_track_sess  = (int64_t) LLONG_MIN;

            int64_t  end_lba  = (int64_t) LLONG_MIN;
            uint32_t cur_sess = img->track_entries[0].session;

            for (unsigned int i = 0; i < img->track_size; i++) {
                if (img->track_entries[i].sequence == 0)
                    continue; // This is not actionable.
                if (img->track_entries[i].session != cur_sess) {
                    start_track_info[0].adr_ctl = 0x14;
                    start_track_info[1].adr_ctl = 0x10;
                    start_track_info[2].adr_ctl = 0x10;

                    start_track_info[0].tno  = 0;
                    start_track_info[0].session = cur_sess;
                    start_track_info[0].m    = 0;
                    start_track_info[0].s    = 0;
                    start_track_info[0].f    = 0;
                    start_track_info[0].zero = 0;
                    start_track_info[0].pm   = first_track_sess;
                    start_track_info[0].ps   = 0x00; // TODO: Fill this in actually.
                    start_track_info[0].pf   = 0x00;

                    start_track_info[1].tno  = 0;
                    start_track_info[1].session = cur_sess;
                    start_track_info[1].m    = 0;
                    start_track_info[1].s    = 0;
                    start_track_info[1].f    = 0;
                    start_track_info[1].zero = 0;
                    start_track_info[1].pm   = last_track_sess;
                    start_track_info[1].ps   = 0x00;
                    start_track_info[1].pf   = 0x00;

                    start_track_info[2].tno  = 0;
                    start_track_info[2].session = cur_sess;
                    start_track_info[2].pm   = (cdrom_lba_to_msf_accurate(end_lba + 1) >> 16) & 0xFF;
                    start_track_info[2].ps   = (cdrom_lba_to_msf_accurate(end_lba + 1) >> 8) & 0xFF;
                    start_track_info[2].pf   = cdrom_lba_to_msf_accurate(end_lba + 1) & 0xFF;
                    start_track_info[2].m    = 0;
                    start_track_info[2].s    = 0;
                    start_track_info[2].f    = 0;
                    start_track_info[2].zero = 0;

                    raw_track_info_t *last_track = aaru_image_allocate_track(img);
                    last_track->adr_ctl          = 0x54;
                    last_track->point            = 0xB0;
                    last_track->session          = cur_sess;
                    last_track->m                = (cdrom_lba_to_msf_accurate(img->track_entries[i].start - img->track_entries[i].pregap) >> 16) & 0xFF;
                    last_track->s                = (cdrom_lba_to_msf_accurate(img->track_entries[i].start - img->track_entries[i].pregap) >> 8) & 0xFF;
                    last_track->f                = cdrom_lba_to_msf_accurate(img->track_entries[i].start - img->track_entries[i].pregap) & 0xFF;
                    last_track->tno              = 0;
                    last_track->zero             = cur_sess == 1 ? 2 : 1;
                    last_track->pm               = 0x40;
                    last_track->ps               = 0x02;
                    last_track->pf               = 0x00;
                    if (cur_sess == 1) {
                        last_track          = aaru_image_allocate_track(img);
                        last_track->adr_ctl = 0x54;
                        last_track->point   = 0xC0;
                        last_track->m       = 0;
                        last_track->s       = 0;
                        last_track->f       = 0;
                        last_track->tno     = 0;
                        last_track->zero    = 0;
                        last_track->pm      = 0x5f;
                        last_track->ps      = 0x00;
                        last_track->pf      = 0x00;
                    }

                    first_track_sess = (int64_t) LLONG_MAX;
                    last_track_sess  = (int64_t) LLONG_MIN;
                    end_lba          = (int64_t) LLONG_MIN;

                    // Generate new 0xA0/0xA1/0xA2 tracks.
                    start_track_info = (raw_track_info_t *) (img->full_toc + img->full_toc_size);
                    offset_lead = (uint8_t*)start_track_info - img->full_toc;
                    (void) aaru_image_allocate_track(img);
                    (void) aaru_image_allocate_track(img);
                    (void) aaru_image_allocate_track(img);
                    start_track_info             = (raw_track_info_t *) (img->full_toc + offset_lead);

                    start_track_info[0].point = 0xA0;
                    start_track_info[1].point = 0xA1;
                    start_track_info[2].point = 0xA2;
                }
                offset_lead = (uint8_t*)start_track_info - img->full_toc;
                raw_track_info_t *last_track = aaru_image_allocate_track(img);
                start_track_info             = (raw_track_info_t *) (img->full_toc + offset_lead);

                last_track->adr_ctl = (img->track_entries[i].flags) | 0x10;
                last_track->m = last_track->s = last_track->f = last_track->zero = 0;
                last_track->tno                                                  = 0;
                last_track->session                                              = img->track_entries[i].session;
                last_track->point                                                = img->track_entries[i].sequence;
                if (img->track_entries[i].type == kTrackTypeAudio) {
                    last_track->pm                                                   = (cdrom_lba_to_msf_accurate(img->track_entries[i].start + img->track_entries[i].pregap) >> 16) & 0xFF;
                    last_track->ps                                                   = (cdrom_lba_to_msf_accurate(img->track_entries[i].start + img->track_entries[i].pregap) >> 8) & 0xFF;
                    last_track->pf                                                   = cdrom_lba_to_msf_accurate(img->track_entries[i].start + img->track_entries[i].pregap) & 0xFF;
                } else {
                    last_track->pm                                                   = (cdrom_lba_to_msf_accurate(img->track_entries[i].start) >> 16) & 0xFF;
                    last_track->ps                                                   = (cdrom_lba_to_msf_accurate(img->track_entries[i].start) >> 8) & 0xFF;
                    last_track->pf                                                   = cdrom_lba_to_msf_accurate(img->track_entries[i].start) & 0xFF;
                }
                if (img->track_entries[i].end > end_lba) {
                    end_lba = img->track_entries[i].end;
                }
                if (img->track_entries[i].sequence > last_track_sess) {
                    last_track_sess = img->track_entries[i].sequence;
                }
                if (img->track_entries[i].sequence < first_track_sess) {
                    first_track_sess = img->track_entries[i].sequence;
                }
            }

            start_track_info[0].tno     = 0;
            start_track_info[0].session = cur_sess;
            start_track_info[0].adr_ctl = 0x14;
            start_track_info[0].m       = 0;
            start_track_info[0].s       = 0;
            start_track_info[0].f       = 0;
            start_track_info[0].zero    = 0;
            start_track_info[0].pm      = first_track_sess;
            start_track_info[0].ps      = 0x00; // TODO: Fill this in actually.
            start_track_info[0].pf      = 0x00;

            start_track_info[1].tno     = 0;
            start_track_info[1].session = cur_sess;
            start_track_info[1].adr_ctl = 0x10;
            start_track_info[1].m       = 0;
            start_track_info[1].s       = 0;
            start_track_info[1].f       = 0;
            start_track_info[1].zero    = 0;
            start_track_info[1].pm      = last_track_sess;
            start_track_info[1].ps      = 0x00;
            start_track_info[1].pf      = 0x00;

            start_track_info[2].tno     = 0;
            start_track_info[2].session = cur_sess;
            start_track_info[2].adr_ctl = 0x10;
            start_track_info[2].pm      = (cdrom_lba_to_msf_accurate(end_lba + 1) >> 16) & 0xFF;
            start_track_info[2].ps      = (cdrom_lba_to_msf_accurate(end_lba + 1) >> 8) & 0xFF;
            start_track_info[2].pf      = cdrom_lba_to_msf_accurate(end_lba + 1) & 0xFF;
            start_track_info[2].m       = 0;
            start_track_info[2].s       = 0;
            start_track_info[2].f       = 0;
            start_track_info[2].zero    = 0;

            if (cur_sess != 1) {
                raw_track_info_t *last_track = aaru_image_allocate_track(img);
                last_track->adr_ctl          = 0x54;
                last_track->point            = 0xB0;
                last_track->m                = (cdrom_lba_to_msf_accurate(end_lba) >> 16) & 0xFF;
                last_track->s                = (cdrom_lba_to_msf_accurate(end_lba) >> 8) & 0xFF;
                last_track->f                = cdrom_lba_to_msf_accurate(end_lba) & 0xFF;
                last_track->tno              = 0;
                last_track->zero             = cur_sess == 1 ? 2 : 1;
                last_track->pm               = 0x40;
                last_track->ps               = 0x02;
                last_track->pf               = 0x00;
            }

            // We don't handle first and last session numbers in the backend for now
            // so the 3rd and 4th bytes of the Raw TOC is meaningless.
toc_skip:
            if (!img->track_entries) {
                res = aaruf_get_tracks(img->aaruf_context, NULL, &large_length);

                if (res == AARUF_ERROR_BUFFER_TOO_SMALL) {
                    img->track_entries = calloc(1, large_length);
                    if (!(res = aaruf_get_tracks(img->aaruf_context, (uint8_t *) img->track_entries, &large_length))) {
                        img->track_size = large_length / sizeof(TrackEntry);
                    } else {
                        pclog("Failed to allocate tracks for Aaru images\n");
                        goto cleanup_error;
                    }
                }
            }

            dev->ops = &aaru_image_ops;
        } else {
            goto cleanup_error;
        }
        img->dev = dev;
        return img;
    } else {
        return NULL;
    }
cleanup_error:
    if (img->track_entries)
        free(img->track_entries);
    if (img->full_toc)
        free(img->full_toc);
    if (img->aaruf_context)
        aaruf_close(img->aaruf_context);
    free(img);

    return NULL;
}
