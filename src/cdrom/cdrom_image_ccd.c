/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Support for CloneCD images.
 *
 *
 * Authors: TheCollector1995, <mariogplayer@gmail.com>,
 *          Miran Grca, <mgrca8@gmail.com>
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
#include <86box/ini.h>

// Note: The entire raw sector data (excluding subchannels) is encoded in the 'img' file, and subchannels are encoded in 'sub' file.
// TODO: Do multi-session images actually store the space between multi-sessions in the image?
// Note 2: Data tracks are not supposed to have pregaps, only audio tracks, and the latter are fixed to 150 sectors too?

// Note 3: Amend pregap behaviour if real CloneCD files are found wit

typedef struct ccd_image_t {
    cdrom_t *dev;
    FILE* main_file;
    FILE* sub_file;

    int64_t end_lba;

    raw_track_info_t* rti_infos;
    uint32_t rti_size; // NOT in bytes.

    bool data_tracks_scrambled;
} ccd_image_t;

static void
ccd_image_get_raw_track_info(UNUSED(const void *local), int *num, uint8_t *rti)
{
    ccd_image_t *ioctl = (ccd_image_t *) local;

    *num = ioctl->rti_size;
    memcpy(rti, ioctl->rti_infos, *num * 11);
}

static int
ccd_image_get_track_info(UNUSED(const void *local), UNUSED(const uint32_t track),
                          UNUSED(int end), UNUSED(track_info_t *ti))
{
    const ccd_image_t      *ioctl      = (const ccd_image_t *) local;
    const raw_track_info_t *rti        = (const raw_track_info_t *) ioctl->rti_infos;
    int                     ret        = 1;
    int                     trk        = -1;
    int                     next       = -1;
    uint32_t                blocks_num = (ioctl->rti_size);

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

static int
ccd_image_get_track(const ccd_image_t *ioctl, const uint32_t sector)
{
    raw_track_info_t *rti    = (raw_track_info_t *) ioctl->rti_infos;
    int               track  = -1;
    int               tracks = ioctl->rti_size;

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
ccd_image_has_audio(UNUSED(const void *local))
{
    ccd_image_t *img = (ccd_image_t *) local;
    for (unsigned int i = 0; i < img->rti_size; i++) {
        if (!(img->rti_infos[i].adr_ctl & 4))
            return 1;
    }
    return 0;
}

static int
ccd_image_track_audio(const ccd_image_t *ioctl, const uint32_t pos)
{
    raw_track_info_t *rti = (raw_track_info_t *) ioctl->rti_infos;
    int               ret = 0;

    if (1) {
        const int track   = ccd_image_get_track(ioctl, pos);
        const int control = rti[track].adr_ctl;

        ret = !(control & 0x04);
    }

    return ret;
}

static uint8_t
ccd_image_get_track_type(const void *local, const uint32_t sector)
{
    ccd_image_t            *ioctl = (ccd_image_t *) local;
    int                     track = ccd_image_get_track(ioctl, sector);
    raw_track_info_t       *rti   = (raw_track_info_t *) (ioctl->rti_infos);
    const raw_track_info_t *trk   = &(rti[track]);
    uint8_t                 ret   = 0x00;

    if (ccd_image_track_audio(ioctl, sector))
        ret = CD_TRACK_AUDIO;
    else if (track != -1)
        for (int i = 0; i < ioctl->rti_size; i++) {
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

static int
ccd_image_read_sector(const void *local, UNUSED(uint8_t *buffer), UNUSED(uint32_t const sector))
{
    ccd_image_t            *ioctl         = (ccd_image_t *) local;
    uint64_t                lba           = sector == ~0u ? ioctl->dev->seek_pos : sector;
    int                     track         = ccd_image_get_track(local, sector == ~0u ? ioctl->dev->seek_pos : sector);
    raw_track_info_t       *rti           = (raw_track_info_t *) ioctl->rti_infos;
    const raw_track_info_t *ct            = &(rti[track]);
    const uint32_t          start         = (ct->pm * 60 * 75) + (ct->ps * 75) + ct->pf;
    int                     m = 0, s = 0, f = 0;
    int64_t                 pregap_length = 0;
    int                     res = -1;

    memset(buffer, 0, 2448);

    if (!fseek(ioctl->main_file, lba * 2352, SEEK_SET)) {
        if (fread(buffer, 2352, 1, ioctl->main_file)) {
            res = 1;
            if (ioctl->data_tracks_scrambled && (rti->adr_ctl & 4)) {
                for (int i = 0; i < 2352; i++) {
                    buffer[i] ^= cdrom_scramble_table[i];
                }
            }
            if (ioctl->sub_file) {
                if (!fseek(ioctl->sub_file, sector * 96, SEEK_SET)) {
                    uint8_t deinterleaved_subch[96] = { };
                    if (fread(deinterleaved_subch, 1, 96, ioctl->sub_file))
                        cdrom_interleave_subch(buffer + 2352, deinterleaved_subch);
                } else
                    goto generate_subchannels;
            } else {
generate_subchannels:
                /* Construct Q. */
                buffer[2352 + 0] = (ct->adr_ctl >> 4) | ((ct->adr_ctl & 0xf) << 4);
                buffer[2352 + 1] = bin2bcd(ct->point);
                pregap_length = (ct->adr_ctl & 0x4) ? 0 : 150;
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
        }
    }
    return res;
}

static int
ccd_image_read_dvd_structure(UNUSED(const void *local), UNUSED(const uint8_t layer), UNUSED(const uint8_t format),
                              UNUSED(uint8_t *buffer), UNUSED(uint32_t *info))
{
    return 0;
}

static int
ccd_image_is_dvd(UNUSED(const void *local))
{
    return 0;
}

static uint32_t
ccd_image_get_last_block(const void *local)
{
    ccd_image_t *ioctl = (ccd_image_t *) local;

    return ioctl->end_lba;
}

static raw_track_info_t *
ccd_image_allocate_track(ccd_image_t *img)
{
    img->rti_infos = realloc(img->rti_infos, img->rti_size * sizeof(raw_track_info_t) + sizeof(raw_track_info_t));
    raw_track_info_t *raw_track_info = img->rti_infos + img->rti_size;
    memset(raw_track_info, 0, sizeof(raw_track_info_t));
    img->rti_size += 1;
    return raw_track_info;
}

static void
ccd_image_close(void *local)
{
    ccd_image_t *img = local;
    if (img->main_file)
        fclose(img->main_file);
    if (img->sub_file)
        fclose(img->sub_file);
    if (img->rti_infos)
        free(img->rti_infos);
    free(img);
}

static const cdrom_ops_t ccd_image_ops = {
    ccd_image_get_track_info,
    ccd_image_get_raw_track_info,
    ccd_image_read_sector,
    ccd_image_get_track_type,
    ccd_image_get_last_block,
    ccd_image_read_dvd_structure,
    ccd_image_is_dvd,
    ccd_image_has_audio,
    NULL,
    ccd_image_close,
    NULL
};

void *
ccd_image_open(cdrom_t *dev, const char *path)
{
    ccd_image_t *img = (ccd_image_t*)calloc(1, sizeof(ccd_image_t));
    char* img_path = strdup(path);
    int real_tracks_num = 0;

    img_path[strlen(img_path) - 1] = 'g';
    img_path[strlen(img_path) - 2] = 'm';
    img_path[strlen(img_path) - 3] = 'i';
    img->main_file = plat_fopen64(img_path, "rb");
    if (!img->main_file) {
        img_path[strlen(img_path) - 1] = 'G';
        img_path[strlen(img_path) - 2] = 'M';
        img_path[strlen(img_path) - 3] = 'I';
        img->main_file = plat_fopen64(img_path, "rb");
    }

    if (!img->main_file) {
        free(img);
        return NULL;
    }

    img_path[strlen(img_path) - 1] = 'b';
    img_path[strlen(img_path) - 2] = 'u';
    img_path[strlen(img_path) - 3] = 's';
    img->sub_file = plat_fopen(img_path, "rb");
    if (!img->sub_file) {
        img_path[strlen(img_path) - 1] = 'b';
        img_path[strlen(img_path) - 2] = 'u';
        img_path[strlen(img_path) - 3] = 'b';
        img->sub_file = plat_fopen(img_path, "rb");
    }

    free(img_path);

    ini_t ccd_ini = ini_read(path);
    if (ccd_ini) {
        img->data_tracks_scrambled = !!ini_get_uint(ccd_ini, "CloneCD", "DataTracksScrambled", 0);
        ini_section_t sec = ini_find_section(ccd_ini, "Disc");
        if (sec) {
            uint32_t toc_entries = ini_section_get_uint(sec, "TocEntries", 0);
            // We just parse the TOC entries here to generate a full TOC.

            for (int i = 0; i < toc_entries; i++) {
                char sec_name[256] = { 0 };
                snprintf(sec_name, sizeof(sec_name) - 1, "Entry %d", i);
                sec = ini_find_section(ccd_ini, sec_name);
                if (sec) {
                    raw_track_info_t* rti = ccd_image_allocate_track(img);
                    rti->session = ini_section_get_uint(sec, "Session", 1);

                    rti->adr_ctl = ini_section_get_uint(sec, "ADR", 1) << 4;
                    rti->adr_ctl |= ini_section_get_uint(sec, "Control", 1) & 0xf;

                    rti->tno = 0;
                    rti->point = ini_section_get_uint(sec, "Point", 0);
                    rti->m = ini_section_get_uint(sec, "AMin", 0);
                    rti->s = ini_section_get_uint(sec, "ASec", 0);
                    rti->f = ini_section_get_uint(sec, "AFrame", 0);
                    rti->zero = ini_section_get_uint(sec, "Zero", 0);
                    rti->pm = ini_section_get_uint(sec, "PMin", 0);
                    rti->ps = ini_section_get_uint(sec, "PSec", 0);
                    rti->pf = ini_section_get_uint(sec, "PFrame", 0);

                    if (rti->point >= 0 && rti->point <= 99)
                        real_tracks_num += 1;
                }
            }
        }
    }

    if (real_tracks_num == 0) {
        ccd_image_close(img);
        return NULL;
    }

    fseeko64(img->main_file, 0, SEEK_END);
    img->end_lba = ftello64(img->main_file) / 2352ll;
    fseeko64(img->main_file, 0, SEEK_SET);

    if (img->sub_file) {
        fseek(img->sub_file, 0, SEEK_END);
        long sub_size = ftell(img->sub_file) / 96;
        fseek(img->sub_file, 0, SEEK_SET);
        if (!sub_size) {
            fclose(img->sub_file);
            img->sub_file = NULL;
        }
    }

    img->dev = dev;
    img->dev->ops = &ccd_image_ops;
    return img;
}

int
cdrom_image_is_ccd(const char *fn)
{
    int res = 0;
    ini_t ccd_ini = ini_read(fn);
    if (ccd_ini && ini_find_section(ccd_ini, "CloneCD")) {
        res = 1;
        ini_close(ccd_ini);
    }
    return res;
}
