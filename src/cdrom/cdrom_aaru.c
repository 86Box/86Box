/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Support for Aaru format images via libaaruformat.
 *
 *          Format identification code (C) Natalia Portillo, licensed under LGPLv2.1+.
 *
 * Authors: TheCollector1995, <mariogplayer@gmail.com>,
 *          Miran Grca, <mgrca8@gmail.com>
 *          Natalia Portillo,
 *          Cacodemon345
 *
 *          Copyright 2023 TheCollector1995.
 *          Copyright 2023 Miran Grca.
 *          Copyright 2026 Cacodemon345.
 */
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#ifdef ENABLE_IMAGE_LOG
#    include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <limits.h>
#ifndef _WIN32
#    include <libgen.h>
#endif
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/bswap.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/plat_dynld.h>

#if defined(_WIN32)
#define AARU_CALL                       __stdcall
#else
#define AARU_CALL
#endif

#define AARUF_ERROR_BUFFER_TOO_SMALL    (-10)

#define AARU_HEADER_APP_NAME_LEN        64

#define DIC_MAGIC                       0x544D52464D434944ULL

#define AARU_MAGIC                      0x544D524655524141ULL

#define AARUF_VERSION                   2

typedef struct ImageInfo
{
    uint8_t  HasPartitions;
    uint8_t  HasSessions;
    uint64_t ImageSize;
    uint64_t Sectors;
    uint32_t SectorSize;
    char     Version[32];
    char     Application[64];
    char     ApplicationVersion[32];
    int64_t  CreationTime;
    int64_t  LastModificationTime;
    uint32_t MediaType;
    uint8_t  MetadataMediaType;
} ImageInfo;

#pragma pack(push, 1)
typedef struct TrackEntry
{
    uint8_t  sequence;
    uint8_t  type;
    int64_t  start;
    int64_t  end;
    int64_t  pregap;
    uint8_t  session;
    uint8_t  isrc[13];
    uint8_t  flags;
} TrackEntry;

typedef struct AaruHeader
{
    uint64_t identifier;
    uint8_t  application[AARU_HEADER_APP_NAME_LEN];
    uint8_t  imageMajorVersion;
    uint8_t  imageMinorVersion;
    uint8_t  applicationMajorVersion;
    uint8_t  applicationMinorVersion;
    uint32_t mediaType;
    uint64_t indexOffset;
    int64_t  creationTime;
    int64_t  lastWrittenTime;
} AaruHeader;
#pragma pack(pop)

typedef enum
{
    kTrackTypeAudio           =  0,
    kTrackTypeData            =  1,
    kTrackTypeCdMode1         =  2,
    kTrackTypeCdMode2Formless =  3,
    kTrackTypeCdMode2Form1    =  4,
    kTrackTypeCdMode2Form2    =  5
} TrackType;

typedef enum
{
    kSectorTagCdSubHeader     =  3,

    kSectorTagCdSubchannel    =  8
} SectorTagType;

typedef enum
{
    kMediaTagFullToc          =  2,

    kMediaTagDvdPfi           =  7,
    kMediaTagDvdCmi           =  8,

    kMediaTagDvdDmi           = 11,

    kMediaTagDvdPfi2ndLayer   = 74
} MediaTagType;

typedef enum
{
    DVDROM                    = 40,

    DVDDownload               = 50
} MediaType;

typedef enum
{
    OpticalDisc               =  0
} XmlMediaType;

int     (* AARU_CALL f_aaruf_identify)(const char *filename);
void*   (* AARU_CALL f_aaruf_open)(const char *filepath, bool resume_mode,
                                   const char *options);
void*   (* AARU_CALL f_aaruf_close)(void *context);
int32_t (* AARU_CALL f_aaruf_get_tracks)(const void *context, uint8_t *buffer,
                                         size_t *length);
int32_t (* AARU_CALL f_aaruf_set_tracks)(const void *context, TrackEntry *tracks,
                                         int count);
int32_t (* AARU_CALL f_aaruf_read_sector)(void *context, uint64_t sector_address,
                                          bool negative, uint8_t *data,
                                          uint32_t *length, uint8_t *sector_status);
int32_t (* AARU_CALL f_aaruf_read_sector_long)(void *context, uint64_t sector_address,
                                               bool negative, uint8_t *data,
                                               uint32_t *length, uint8_t *sector_status);
int32_t (* AARU_CALL f_aaruf_read_sector_tag)(const void *context, uint64_t sector_address,
                                              bool negative, uint8_t *buffer,
                                              uint32_t *length, int32_t tag);
int32_t (* AARU_CALL f_aaruf_read_media_tag)(void *context, uint8_t *data,
                                             int32_t tag, uint32_t *length);
int32_t (* AARU_CALL f_aaruf_get_image_info)(const void *context, ImageInfo *image_info);

static dllimp_t aaruf_imports[] = {
    { "aaruf_identify",         &f_aaruf_identify         },
    { "aaruf_open",             &f_aaruf_open             },
    { "aaruf_close",            &f_aaruf_close            },
    { "aaruf_get_tracks",       &f_aaruf_get_tracks       },
    { "aaruf_set_tracks",       &f_aaruf_set_tracks       },
    { "aaruf_read_sector",      &f_aaruf_read_sector      },
    { "aaruf_read_sector_long", &f_aaruf_read_sector_long },
    { "aaruf_read_sector_tag",  &f_aaruf_read_sector_tag  },
    { "aaruf_read_media_tag",   &f_aaruf_read_media_tag   },
    { "aaruf_get_image_info",   &f_aaruf_get_image_info   },
    { NULL,                     NULL                      },
};

static volatile void* libaaruformat_handle = NULL;
static bool load_failed = false;

static bool
ensure_libaaruformat(void)
{
    if (load_failed)
        return false;
    if (!libaaruformat_handle) {
#ifdef _WIN32
        libaaruformat_handle = dynld_module("libaaruformat.dll", aaruf_imports);
#elif defined(__APPLE__)
        libaaruformat_handle = dynld_module("libaaruformat.dylib", aaruf_imports);
#else
        libaaruformat_handle = dynld_module("libaaruformat.so", aaruf_imports);
#endif
        if (!libaaruformat_handle) {
            warning("Failed to load libaaruformat library.");
            load_failed = true;
            return false;
        }
    } else {
        return true;
    }
    return true;
}

typedef struct aaru_image_t {
    cdrom_t *   dev;
    void    *   aaruf_context;
    uint8_t *   full_toc;    /* Generated if it does not exist. */
    size_t      full_toc_size;
    bool        is_dvd;

    ImageInfo   img_info;

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

static void
aaru_image_get_track_mode(const aaru_image_t *img, int64_t lba, uint8_t *mode, uint8_t *form)
{
    uint8_t  buffer[1024];
    uint32_t length = 0;
    // Mode shall be determined by getting the type of the track.
    // Form is determined by reading the sector metadata.
    *mode = 0;
    *form = 0;
    for (int i = 0; i < img->track_size; i++) {
        if (lba <= img->track_entries[i].end && lba >= img->track_entries[i].start) {
            if (img->track_entries[i].type == kTrackTypeAudio) {
                *mode = 0;
                *form = 0;
                return;
            }
            if ((img->track_entries[i].type == kTrackTypeCdMode1) ||
                (img->track_entries[i].type == kTrackTypeData)) {
                *mode = 1;
                *form = 0;
                return;
            }
            if (img->track_entries[i].type == kTrackTypeCdMode2Form1) {
                *mode = 2;
                *form = 1;
                return;
            }
            if (img->track_entries[i].type == kTrackTypeCdMode2Form2) {
                *mode = 2;
                *form = 2;
                return;
            }
            // Mode Formless, try to extract the metadata.
            length = 8;
            *mode  = 2;
            *form  = 0;
            if (!f_aaruf_read_sector_tag(img->aaruf_context, (uint64_t) lba,
                false, buffer, &length, kSectorTagCdSubHeader)) {
                *form = 1 + !!(buffer[2] & (1 << 5));
            }
        }
    }
    return;
}

static int64_t
aaru_image_get_pregap_length(const aaru_image_t *img, int track)
{
    for (int i = 0; i < img->track_size; i++) {
        if (img->track_entries[i].sequence == track) {
            if (i == 0)
                return 0; // The pregap starts in the negative area.
            return img->track_entries[i].pregap;
        }
    }
    return 0;
}

static int
aaru_image_read_sector(const void *local, UNUSED(uint8_t *buffer), UNUSED(uint32_t const sector))
{
    aaru_image_t           *ioctl         = (aaru_image_t *) local;
    uint8_t                 sector_status = 0;
    uint64_t                lba           = sector;
    uint32_t                length        = 2352;
    uint32_t                track         = aaru_image_get_track(local, (sector == ~0u) ?
                                                                 ioctl->dev->seek_pos : sector);
    raw_track_info_t       *rti           = (raw_track_info_t *) (ioctl->full_toc + 4);
    const raw_track_info_t *ct            = &(rti[track]);
    const uint32_t          start         = (ct->pm * 60 * 75) + (ct->ps * 75) + ct->pf;
    int                     m, s, f;
    uint8_t                 mode = 0, form = 0;
    int64_t                 pregap_length = 0;

    memset(buffer, 0, 2448);

    if (sector == 0xFFFFFFFF)
        lba = ioctl->dev->seek_pos;

    aaru_image_get_track_mode(local, (int64_t) (uint64_t) sector, &mode, &form);

    if (mode == 0) {
        length = 2352;
        // Just read the audio sector. Errors can be ignored here.
        (void)f_aaruf_read_sector(ioctl->aaruf_context, lba,
                                  false, buffer, &length, &sector_status);
    } else if (ioctl->is_dvd || f_aaruf_read_sector_long(ioctl->aaruf_context, lba,
                                                         false, buffer,
                                                         &length, &sector_status)) {
        length = 2048;
        if (!f_aaruf_read_sector(ioctl->aaruf_context, lba, false, buffer,
                                 &length, &sector_status))
            goto generate_headers;
        return -1;
    }

    length = 96;
    if (f_aaruf_read_sector_tag(ioctl->aaruf_context, lba, false, &buffer[2352],
                                &length, kSectorTagCdSubchannel)) {

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

            /* Mode 1/Mode 2 data. */
            buffer[15] = mode;
            if (form >= 1) {
                uint8_t *subheader = buffer + 16;
                /* Construct the CD-I/XA sub-header. */
                subheader[2] = subheader[6] = (form - 1) << 5;
            }

            uint32_t crc;

            if ((mode == 2) && (form == 1)) {
                crc = cdrom_crc32(0xffffffff, &(buffer[16]), 2056) ^ 0xffffffff;
                memcpy(&(buffer[2072]), &crc, 4);
            } else if ((mode == 2) && (form == 2)) {
                crc = cdrom_crc32(0xffffffff, &(buffer[16]), 2332) ^ 0xffffffff;
                memcpy(&(buffer[2348]), &crc, 4);
            } else {
                crc = cdrom_crc32(0xffffffff, buffer, 2064) ^ 0xffffffff;
                memcpy(&(buffer[2064]), &crc, 4);
            }

            int m2f1 = (mode == 2) && (form == 1);

            if ((mode == 1) || m2f1) {
                /* Compute ECC P code. */
                cdrom_compute_ecc_block(ioctl->dev, &(buffer[2076]),
                                        &(buffer[12]), 86, 24, 2,
                                        86, m2f1);

                /* Compute ECC Q code. */
                cdrom_compute_ecc_block(ioctl->dev, &(buffer[2248]),
                                        &(buffer[12]), 52, 43, 86,
                                        88, m2f1);
            }
        }

generate_subchannel:
        /* Construct Q. */
        buffer[2352 + 0] = (ct->adr_ctl >> 4) | ((ct->adr_ctl & 0xf) << 4);
        buffer[2352 + 1] = bin2bcd(ct->point);
        pregap_length = aaru_image_get_pregap_length(ioctl, ct->point);
        if (pregap_length && ((lba + 150) - start) < pregap_length) {
            /*
                Pre-gap sector relative frame addresses count from
                the pregap length downwards.
            */
            buffer[2352 + 2] = 0;
            FRAMES_TO_MSF((int32_t) ((pregap_length - 1) - (lba + 150 - start)), &m, &s, &f);
        } else {
            buffer[2352 + 2] = 1;
            FRAMES_TO_MSF((int32_t) (lba + 150 - start), &m, &s, &f);
        }
        buffer[2352 + 3] = bin2bcd(m);
        buffer[2352 + 4] = bin2bcd(s);
        buffer[2352 + 5] = bin2bcd(f);
        FRAMES_TO_MSF(lba + 150, &m, &s, &f);
        buffer[2352 + 7] = bin2bcd(m);
        buffer[2352 + 8] = bin2bcd(s);
        buffer[2352 + 9] = bin2bcd(f);
        *(uint16_t*)&buffer[2352 + 10] = bswap16(cdrom_crc16(0xffff, &buffer[2352], 10));
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
    aaru_image_t *img = (aaru_image_t *) local;
    if (img->is_dvd) {
        uint8_t res[2052] = { };
        uint32_t length = 2052;
        switch (format) {
            default:
                break;
            case 0x00:    /* Physical Format Information (PFI). */
                if (!f_aaruf_read_media_tag(img->aaruf_context, res,
                                            layer ? kMediaTagDvdPfi2ndLayer : kMediaTagDvdPfi,
                                            &length)) {
                    if (length == 2048) {
                        memcpy(buffer + 4, res, 2048);
                        return 2048 + 2;
                    }

                    if (length == 2052) {
                        memcpy(buffer, res, 2052);
                        return 2048 + 2;
                    }
                }
                break;
            case 0x01:    /* DVD copyright information (CMI). */
                length = 8;
                if (!f_aaruf_read_media_tag(img->aaruf_context, res,
                                            kMediaTagDvdCmi, &length)) {
                    if (length == 8) {
                        memcpy(buffer, res, 8);
                        return 4 + 2;
                    }
                }
                break;
            case 0x04:    /* DVD disc manufacturing information (DMI). */
                if (!f_aaruf_read_media_tag(img->aaruf_context, res,
                                            kMediaTagDvdDmi, &length)) {
                    if (length == 2052) {
                        memcpy(buffer, res, 2052);
                        return 2048 + 2;
                    }
                }
                break;
        }
    }
    return 0;
}

static int
aaru_image_is_dvd(UNUSED(const void *local))
{
    aaru_image_t *img = (aaru_image_t *) local;
    return (img->img_info.MediaType >= DVDROM) &&
           (img->img_info.MediaType <= DVDDownload);
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
        f_aaruf_close(img->aaruf_context);
    free(img);
}

static int
aaru_image_track_audio(const aaru_image_t *ioctl, const uint32_t pos)
{
    raw_track_info_t *rti = (raw_track_info_t *) (ioctl->full_toc + 4);
    int               ret = 0;

    if (!ioctl->is_dvd) {
        const int track   = aaru_image_get_track(ioctl, pos);
        const int control = rti[track].adr_ctl;

        ret = !(control & 0x04);
    }

    return ret;
}

static uint8_t
aaru_image_get_track_type(const void *local, const uint32_t sector)
{
    aaru_image_t           *ioctl = (aaru_image_t *) local;
    int                     track = aaru_image_get_track(ioctl, sector);
    raw_track_info_t       *rti   = (raw_track_info_t *) (ioctl->full_toc + 4);
    const raw_track_info_t *trk   = &(rti[track]);
    uint8_t                 ret   = 0x00;

    if (aaru_image_track_audio(ioctl, sector))
        ret = CD_TRACK_AUDIO;
    else if (track != -1)
        for (int i = 0; i < (ioctl->full_toc_size - 4) / 11; i++) {
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
    img->full_toc                    = realloc(img->full_toc,
                                                img->full_toc_size + sizeof(raw_track_info_t));
    raw_track_info_t *raw_track_info = (raw_track_info_t *) (img->full_toc + img->full_toc_size);
    memset(raw_track_info, 0, sizeof(raw_track_info_t));
    img->full_toc_size += sizeof(raw_track_info_t);
    return raw_track_info;
}

/* Public functions */
void *
aaru_image_open(cdrom_t *dev, const char *path)
{
    aaru_image_t *img = (aaru_image_t *) calloc(1, sizeof(aaru_image_t));

    if (!ensure_libaaruformat()) {
        free(img);
        return NULL;
    }

    if (img) {
        img->dev           = dev;
        img->aaruf_context = f_aaruf_open(path, false, NULL);
        if (img->aaruf_context) {
            uint32_t  length       = 0;
            ptrdiff_t offset_lead  = 0;
            size_t    large_length = 0;
            int32_t   res          = 0;

            if (f_aaruf_get_image_info(img->aaruf_context, &img->img_info)) {
                pclog("Failed to get Aaru image info\n");
                goto cleanup_error;
            }

            if (img->img_info.MetadataMediaType != OpticalDisc) {
                pclog("Aaru image is not a optical disc image\n");
                goto cleanup_error;
            }

            res = f_aaruf_read_media_tag(img->aaruf_context, NULL, kMediaTagFullToc, &length);

            if (res == AARUF_ERROR_BUFFER_TOO_SMALL && length != 0) {
                img->full_toc = calloc(1, length + 2);
                res           = f_aaruf_read_media_tag(img->aaruf_context, img->full_toc + 2,
                                                       kMediaTagFullToc, &length);
                if (!res) {
                    img->full_toc_size = length + 2;
                    goto toc_skip;
                }
            }
            // Start generating the full TOC.
            res = f_aaruf_get_tracks(img->aaruf_context, NULL, &large_length);

            if (res == AARUF_ERROR_BUFFER_TOO_SMALL) {
                img->track_entries = calloc(1, large_length);

                res = f_aaruf_get_tracks(img->aaruf_context, (uint8_t *) img->track_entries,
                                         &large_length);

                if (!res)
                    img->track_size = large_length / sizeof(TrackEntry);
                else {
                    warning("Failed to allocate tracks for Aaru images (1)\n");
                    goto cleanup_error;
                }
            }

            img->full_toc      = calloc(4 + sizeof(raw_track_info_t) * 3, 1);
            img->full_toc_size = 4 + sizeof(raw_track_info_t) * 3;

            raw_track_info_t *start_track_info = (raw_track_info_t *) (img->full_toc + 4);

            start_track_info[0].point = 0xA0;
            start_track_info[1].point = 0xA1;
            start_track_info[2].point = 0xA2;
            /*
               This at least satisfies both the Hexen and
               Microsoft Music Sampler disc images.
              */
            start_track_info[0].ps    = ((img->track_entries[0].type == kTrackTypeCdMode2Formless) ||
                                         (img->track_entries[0].type == kTrackTypeCdMode2Form1) ||
                                         (img->track_entries[0].type == kTrackTypeCdMode2Form2)) ? 0x20 : 0x00;

            int64_t first_track_sess = (int64_t) LLONG_MAX;
            int64_t last_track_sess  = (int64_t) LLONG_MIN;

            int64_t  end_lba  = (int64_t) LLONG_MIN;
            uint32_t cur_sess = img->track_entries[0].session;

            for (unsigned int i = 0; i < img->track_size; i++) {
                if (img->track_entries[i].session != cur_sess) {
                    start_track_info[0].adr_ctl = 0x14;
                    start_track_info[1].adr_ctl = 0x10;
                    start_track_info[2].adr_ctl = 0x10;

                    start_track_info[0].tno     = 0;
                    start_track_info[0].session = cur_sess;
                    start_track_info[0].m       = 0;
                    start_track_info[0].s       = 0;
                    start_track_info[0].f       = 0;
                    start_track_info[0].zero    = 0;
                    start_track_info[0].pm      = first_track_sess;
                    start_track_info[0].ps      = 0x00;    /* TODO: Fill this in actually. */
                    start_track_info[0].pf      = 0x00;

                    start_track_info[1].tno     = 0;
                    start_track_info[1].session = cur_sess;
                    start_track_info[1].m       = 0;
                    start_track_info[1].s       = 0;
                    start_track_info[1].f       = 0;
                    start_track_info[1].zero    = 0;
                    start_track_info[1].pm      = last_track_sess;
                    start_track_info[1].ps      = 0x00;
                    start_track_info[1].pf      = 0x00;

                    start_track_info[2].tno     = 0;
                    start_track_info[2].session = cur_sess;
                    start_track_info[2].pm      = (cdrom_lba_to_msf_accurate(end_lba + 1) >> 16) & 0xFF;
                    start_track_info[2].ps      = (cdrom_lba_to_msf_accurate(end_lba + 1) >> 8) & 0xFF;
                    start_track_info[2].pf      = cdrom_lba_to_msf_accurate(end_lba + 1) & 0xFF;
                    start_track_info[2].m       = 0;
                    start_track_info[2].s       = 0;
                    start_track_info[2].f       = 0;
                    start_track_info[2].zero    = 0;

                    raw_track_info_t *last_track = aaru_image_allocate_track(img);
                    last_track->adr_ctl          = 0x54;
                    last_track->point            = 0xB0;
                    last_track->session          = cur_sess;
                    last_track->m                = (cdrom_lba_to_msf_accurate(img->track_entries[i].start) >> 16) & 0xFF;
                    last_track->s                = (cdrom_lba_to_msf_accurate(img->track_entries[i].start) >> 8) & 0xFF;
                    last_track->f                = cdrom_lba_to_msf_accurate(img->track_entries[i].start) & 0xFF;
                    last_track->tno              = 0;
                    last_track->zero             = cur_sess == 1 ? 2 : 1;
                    last_track->pm               = 0x40;
                    last_track->ps               = 0x02;
                    last_track->pf               = 0x00;
                    last_track->session          = cur_sess;
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
                        last_track->session = cur_sess;
                    }

                    first_track_sess = (int64_t) LLONG_MAX;
                    last_track_sess  = (int64_t) LLONG_MIN;
                    end_lba          = (int64_t) LLONG_MIN;

                    // Generate new 0xA0/0xA1/0xA2 tracks.
                    start_track_info = (raw_track_info_t *) (img->full_toc + img->full_toc_size);
                    offset_lead      = (uint8_t *) start_track_info - img->full_toc;
                    (void) aaru_image_allocate_track(img);
                    (void) aaru_image_allocate_track(img);
                    (void) aaru_image_allocate_track(img);
                    start_track_info = (raw_track_info_t *) (img->full_toc + offset_lead);

                    start_track_info[0].point = 0xA0;
                    start_track_info[1].point = 0xA1;
                    start_track_info[2].point = 0xA2;
                    start_track_info[0].ps    = ((img->track_entries[i].type == kTrackTypeCdMode2Formless) ||
                                                 (img->track_entries[i].type == kTrackTypeCdMode2Form1) ||
                                                 (img->track_entries[i].type == kTrackTypeCdMode2Form2)) ? 0x20 : 0x00;
                    cur_sess                  = img->track_entries[i].session;
                }

                offset_lead                  = (uint8_t *) start_track_info - img->full_toc;

                raw_track_info_t *last_track = aaru_image_allocate_track(img);
                start_track_info             = (raw_track_info_t *) (img->full_toc + offset_lead);

                last_track->adr_ctl = (img->track_entries[i].flags) | 0x10;
                last_track->m = last_track->s = last_track->f = last_track->zero = 0;
                last_track->tno                                                  = 0;
                last_track->session                                              = img->track_entries[i].session;
                last_track->point                                                = img->track_entries[i].sequence;

                if (i != 0) {
                    last_track->pm = (cdrom_lba_to_msf_accurate(img->track_entries[i].start + img->track_entries[i].pregap) >> 16) & 0xFF;
                    last_track->ps = (cdrom_lba_to_msf_accurate(img->track_entries[i].start + img->track_entries[i].pregap) >> 8) & 0xFF;
                    last_track->pf = cdrom_lba_to_msf_accurate(img->track_entries[i].start + img->track_entries[i].pregap) & 0xFF;
                } else {
                    last_track->pm = (cdrom_lba_to_msf_accurate(img->track_entries[i].start) >> 16) & 0xFF;
                    last_track->ps = (cdrom_lba_to_msf_accurate(img->track_entries[i].start) >> 8) & 0xFF;
                    last_track->pf = cdrom_lba_to_msf_accurate(img->track_entries[i].start) & 0xFF;
                }

                if (img->track_entries[i].end > end_lba)
                    end_lba = img->track_entries[i].end;

                if (img->track_entries[i].sequence > last_track_sess)
                    last_track_sess = img->track_entries[i].sequence;

                if (img->track_entries[i].sequence < first_track_sess)
                    first_track_sess = img->track_entries[i].sequence;
            }

            start_track_info[0].tno     = 0;
            start_track_info[0].session = cur_sess;
            start_track_info[0].adr_ctl = 0x14;
            start_track_info[0].m       = 0;
            start_track_info[0].s       = 0;
            start_track_info[0].f       = 0;
            start_track_info[0].zero    = 0;
            start_track_info[0].pm      = first_track_sess;
            start_track_info[0].pf      = 0x00;
            // Session type already filled in beforehand.

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
                last_track->zero             = 1;
                last_track->pm               = 0x40;
                last_track->ps               = 0x02;
                last_track->pf               = 0x00;
                last_track->session          = cur_sess;
            }

            // We don't handle first and last session numbers in the backend for now
            // so the 3rd and 4th bytes of the Raw TOC is meaningless.
toc_skip:
            if (!img->track_entries) {
                res = f_aaruf_get_tracks(img->aaruf_context, NULL, &large_length);

                if (res == AARUF_ERROR_BUFFER_TOO_SMALL) {
                    img->track_entries = calloc(1, large_length);
                    res = f_aaruf_get_tracks(img->aaruf_context, (uint8_t *) img->track_entries, &large_length);
                    if (!res)
                        img->track_size = large_length / sizeof(TrackEntry);
                    else {
                        warning("Failed to allocate tracks for Aaru images\n");
                        goto cleanup_error;
                    }
                }
            }

            dev->ops = &aaru_image_ops;
        } else
            goto cleanup_error;

        img->dev = dev;
        img->is_dvd = aaru_image_is_dvd(img);
        return img;
    }

    return NULL;

cleanup_error:
    if (img->track_entries)
        free(img->track_entries);
    if (img->full_toc)
        free(img->full_toc);
    if (img->aaruf_context)
        f_aaruf_close(img->aaruf_context);
    free(img);

    return NULL;
}

static int aaruf_identify_local(const char *filename)
{
    if (filename == NULL)
        return EINVAL;

    FILE *stream = NULL;

    stream = fopen(filename, "rb");

    if (stream == NULL)
        return errno;

    AaruHeader header;

    size_t ret = fread(&header, sizeof(AaruHeader), 1, stream);

    if(ret != 1) {
        fclose(stream);
        return 0;
    }

    if (((header.identifier == DIC_MAGIC) || (header.identifier == AARU_MAGIC)) &&
        (header.imageMajorVersion <= AARUF_VERSION))
        ret = 100;

    fclose(stream);

    return ret;
}

int
cdrom_image_is_aaru(const char *fn)
{
    return (aaruf_identify_local(fn) == 100);
}
