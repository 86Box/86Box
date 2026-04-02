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
#include <86box/cdrom.h>
#include <86box/cdrom_image_writable.h>

#include <sndfile.h>

#define MAX_LINE_LENGTH     512
#define MAX_FILENAME_LENGTH 256
#define CROSS_LEN           512

static char temp_keyword[1024];

typedef struct track_index_t {
     int64_t       start;
    uint64_t       length;
    uint64_t       file_start;
    uint64_t       file_length;
    wtrack_file_t *file;
} track_index_t;

typedef struct track_t {
    uint8_t       session;
    uint8_t       attr;
    uint8_t       tno;
    uint8_t       point;
    uint8_t       extra[4];
    uint8_t       mode;
    uint8_t       form;
    uint8_t       subch_type;
    uint8_t       skip;
    uint8_t       max_index;
    uint32_t      sector_size;
    track_index_t idx[100];
} track_t;

typedef struct cd_image_t {
    cdrom_t      *dev;
    void         *log;
    int           has_audio;
    int32_t       tracks_num;
    track_t      *tracks;
} cd_image_t;

typedef struct raw_cuesheet_t {
    uint8_t ctl_adr;
    uint8_t tno;
    uint8_t index;
    uint8_t data_form;
    uint8_t scms;
    uint8_t amin;
    uint8_t asec;
    uint8_t aframe;
} raw_cuesheet_t;

#ifdef ENABLE_IMAGE_LOG
int image_do_log = ENABLE_IMAGE_LOG;

void
image_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (image_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}

static char   *cit[4]   = { "SPECIAL", "NONE", "ZERO", "NORMAL" };
#else
#    define image_log(priv, fmt, ...)
#endif


static int
wimage_get_track_info(const void *local, const uint32_t track,
                     const int end, track_info_t *ti)
{
    // TODO: Not implemented

    return -1;
}

static void
wimage_get_raw_track_info(const void *local, int *num, uint8_t *buffer)
{
    // TODO: Not implemented
}

static int
wimage_is_track_pre(const void *local, const uint32_t sector)
{
    // TODO: Not implemented

    return -1;
}

static int
wimage_read_sector(const void *local, uint8_t *buffer,
                  const uint32_t sector)
{
    // TODO: Not implemented

    return -1;
}

static uint8_t
wimage_get_track_type(const void *local, const uint32_t sector)
{
    // TODO: Not implemented

    return 0;
}

static uint32_t
wimage_get_last_block(const void *local)
{
    // TODO: Not implemented

    return 0;
}

static int
wimage_read_dvd_structure(const void *local, const uint8_t layer, const uint8_t format,
                         uint8_t *buffer, uint32_t *info)
{
    // TODO: Not implemented

    return -1;
}

static int
wimage_is_dvd(const void *local)
{
    // TODO: Not implemented

    return 0;
}

static int
wimage_has_audio(const void *local)
{
    // TODO: Not implemented

    return 0;
}

static void
wimage_close(void *local)
{
    // TODO: Not implemented
}

static int
wimage_send_cuesheet(const void *local, const uint8_t *buffer, const int len)
{
    const raw_cuesheet_t* rc = (raw_cuesheet_t*) buffer;

    return 0;
}

int
wimage_write_sector(const void *local, const uint8_t *buffer, const uint32_t sector)
{
    // TODO: Not implemented

    return -1;
}

int
wimage_write_to_toc(const void *local, const uint8_t track)
{
    // TODO: Not implemented

    return -1;
}

static const cdrom_ops_t image_ops = {
    wimage_get_track_info,
    wimage_get_raw_track_info,
    wimage_is_track_pre,
    wimage_read_sector,
    wimage_get_track_type,
    wimage_get_last_block,
    wimage_read_dvd_structure,
    wimage_is_dvd,
    wimage_has_audio,
    NULL,
    wimage_send_cuesheet,
    wimage_write_sector,
    wimage_write_to_toc,
    wimage_close,
    NULL
};

/* Public functions. */
void *
wimage_open(cdrom_t *dev, const char *path)
{
    cd_image_t      *img = (cd_image_t *) calloc(1, sizeof(cd_image_t));

    dev->ops = &image_ops;

    // TODO: Not implemented

    return img;
}