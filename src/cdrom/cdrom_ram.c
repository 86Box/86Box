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
#include <limits.h>
#include <sys/stat.h>
#ifndef _WIN32
#    include <libgen.h>
#endif

#include <86box/86box.h>
#include <86box/log.h>
#include <86box/nvr.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/bswap.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/cdrom_image_viso.h>
#include <86box/plat_dynld.h>

typedef struct ram_image_t {
    cdrom_t *dev;
    uint8_t* cd_image_data;

    int64_t end_lba;

    raw_track_info_t* rti_infos;
    uint32_t rti_size; // NOT in bytes.
} ram_image_t;

static void
ram_image_get_raw_track_info(UNUSED(const void *local), int *num, uint8_t *rti)
{
    ram_image_t *ioctl = (ram_image_t *) local;

    *num = ioctl->rti_size;
    memcpy(rti, ioctl->rti_infos, *num * 11);
}

static int
ram_image_get_track_info(UNUSED(const void *local), UNUSED(const uint32_t track),
                          UNUSED(int end), UNUSED(track_info_t *ti))
{
    const ram_image_t      *ioctl      = (const ram_image_t *) local;
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
ram_image_get_track(const ram_image_t *ioctl, const uint32_t sector)
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
ram_image_has_audio(UNUSED(const void *local))
{
    ram_image_t *img = (ram_image_t *) local;
    for (unsigned int i = 0; i < img->rti_size; i++) {
        if (!(img->rti_infos[i].adr_ctl & 4))
            return 1;
    }
    return 0;
}

static int
ram_image_track_audio(const ram_image_t *ioctl, const uint32_t pos)
{
    raw_track_info_t *rti = (raw_track_info_t *) ioctl->rti_infos;
    int               ret = 0;

    if (1) {
        const int track   = ram_image_get_track(ioctl, pos);
        const int control = rti[track].adr_ctl;

        ret = !(control & 0x04);
    }

    return ret;
}

static uint8_t
ram_image_get_track_type(const void *local, const uint32_t sector)
{
    ram_image_t            *ioctl = (ram_image_t *) local;
    int                     track = ram_image_get_track(ioctl, sector);
    raw_track_info_t       *rti   = (raw_track_info_t *) (ioctl->rti_infos);
    const raw_track_info_t *trk   = &(rti[track]);
    uint8_t                 ret   = 0x00;

    if (ram_image_track_audio(ioctl, sector))
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
ram_image_read_dvd_structure(UNUSED(const void *local), UNUSED(const uint8_t layer), UNUSED(const uint8_t format),
                              UNUSED(uint8_t *buffer), UNUSED(uint32_t *info))
{
    return 0;
}

static int
ram_image_is_dvd(UNUSED(const void *local))
{
    return 0;
}

static uint32_t
ram_image_get_last_block(const void *local)
{
    ram_image_t *ioctl = (ram_image_t *) local;

    return ioctl->end_lba;
}

static int
ram_image_read_sector(const void *local, UNUSED(uint8_t *buffer), UNUSED(uint32_t const sector))
{
    ram_image_t *ioctl = (ram_image_t *) local;
    if (sector > ioctl->end_lba)
        return 0;
    memcpy(buffer, &ioctl->cd_image_data[(uint64_t)sector * (uint64_t)2448], 2448);
    return 1;
}

static void
ram_image_close(void *local)
{
    ram_image_t *img = local;
    free(img->cd_image_data);
    if (img->rti_infos)
        free(img->rti_infos);
    free(img);
}

static const cdrom_ops_t ram_image_ops = {
    ram_image_get_track_info,
    ram_image_get_raw_track_info,
    ram_image_read_sector,
    ram_image_get_track_type,
    ram_image_get_last_block,
    ram_image_read_dvd_structure,
    ram_image_is_dvd,
    ram_image_has_audio,
    NULL,
    ram_image_close,
    NULL
};

void*
ram_image_open(cdrom_t *dev, uint8_t* cd_image_data, int64_t end_lba, raw_track_info_t* rti_infos, uint32_t rti_size)
{
    ram_image_t* ram_img = (ram_image_t*)calloc(1, sizeof(ram_image_t));
    ram_img->cd_image_data = cd_image_data;
    ram_img->end_lba = end_lba;
    ram_img->rti_infos = calloc(rti_size, sizeof(raw_track_info_t));
    ram_img->rti_size  = rti_size;
    ram_img->dev = dev;
    memcpy(ram_img->rti_infos, rti_infos, rti_size * sizeof(raw_track_info_t));
    dev->ops = &ram_image_ops;
    return ram_img;
}
