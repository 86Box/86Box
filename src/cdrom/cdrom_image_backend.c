/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CD-ROM image file handling module, translated to C from
 *          cdrom_dosbox.cpp.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          The DOSBox Team, <unknown>
 *          Cacodemon345
 *
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2002-2020 The DOSBox Team.
 *          Copyright 2024 Cacodemon345.
 */
#define __STDC_FORMAT_MACROS
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#    include <string.h>
#    include <sys/types.h>
#else
#    include <libgen.h>
#endif
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/cdrom_image_backend.h>

#include <sndfile.h>

#define CDROM_BCD(x)        (((x) % 10) | (((x) / 10) << 4))

#define MAX_LINE_LENGTH     512
#define MAX_FILENAME_LENGTH 256
#define CROSS_LEN           512

static char temp_keyword[1024];

#ifdef ENABLE_CDROM_IMAGE_BACKEND_LOG
int cdrom_image_backend_do_log = ENABLE_CDROM_IMAGE_BACKEND_LOG;

void
cdrom_image_backend_log(const char *fmt, ...)
{
    va_list ap;

    if (cdrom_image_backend_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cdrom_image_backend_log(fmt, ...)
#endif

typedef struct audio_file_t {
    SNDFILE *file;
    SF_INFO  info;
} audio_file_t;

/* Audio file functions */
static int
audio_read(void *priv, uint8_t *buffer, uint64_t seek, size_t count)
{
    track_file_t *tf            = (track_file_t *) priv;
    audio_file_t *audio         = (audio_file_t *) tf->priv;
    uint64_t      samples_seek  = seek / 4;
    uint64_t      samples_count = count / 4;

    if ((seek & 3) || (count & 3)) {
        cdrom_image_backend_log("CD Audio file: Reading on non-4-aligned boundaries.\n");
    }

    sf_count_t res = sf_seek(audio->file, samples_seek, SEEK_SET);

    if (res == -1)
        return 0;

    return !!sf_readf_short(audio->file, (short *) buffer, samples_count);
}

static uint64_t
audio_get_length(void *priv)
{
    track_file_t *tf    = (track_file_t *) priv;
    audio_file_t *audio = (audio_file_t *) tf->priv;

    /* Assume 16-bit audio, 2 channel. */
    return audio->info.frames * 4ull;
}

static void
audio_close(void *priv)
{
    track_file_t *tf    = (track_file_t *) priv;
    audio_file_t *audio = (audio_file_t *) tf->priv;

    memset(tf->fn, 0x00, sizeof(tf->fn));
    if (audio && audio->file)
        sf_close(audio->file);
    free(audio);
    free(tf);
}

static track_file_t *
audio_init(const char *filename, int *error)
{
    track_file_t *tf    = (track_file_t *) calloc(sizeof(track_file_t), 1);
    audio_file_t *audio = (audio_file_t *) calloc(sizeof(audio_file_t), 1);
#ifdef _WIN32
    wchar_t filename_w[4096];
#endif

    if (tf == NULL || audio == NULL) {
        goto cleanup_error;
    }

    memset(tf->fn, 0x00, sizeof(tf->fn));
    strncpy(tf->fn, filename, sizeof(tf->fn) - 1);
#ifdef _WIN32
    mbstowcs(filename_w, filename, 4096);
    audio->file = sf_wchar_open(filename_w, SFM_READ, &audio->info);
#else
    audio->file = sf_open(filename, SFM_READ, &audio->info);
#endif

    if (!audio->file) {
        cdrom_image_backend_log("Audio file open error!");
        goto cleanup_error;
    }

    if (audio->info.channels != 2 || audio->info.samplerate != 44100 || !audio->info.seekable) {
        cdrom_image_backend_log("Audio file not seekable or in non-CD format!");
        sf_close(audio->file);
        goto cleanup_error;
    }

    *error         = 0;
    tf->priv       = audio;
    tf->fp         = NULL;
    tf->close      = audio_close;
    tf->get_length = audio_get_length;
    tf->read       = audio_read;
    return tf;
cleanup_error:
    free(tf);
    free(audio);
    *error = 1;
    return NULL;
}

/* Binary file functions. */
static int
bin_read(void *priv, uint8_t *buffer, uint64_t seek, size_t count)
{
    track_file_t *tf = NULL;

    if ((tf = (track_file_t *) priv)->fp == NULL)
        return 0;

    cdrom_image_backend_log("CDROM: binary_read(%08lx, pos=%" PRIu64 " count=%lu)\n",
                            tf->fp, seek, count);

    if (fseeko64(tf->fp, seek, SEEK_SET) == -1) {
        cdrom_image_backend_log("CDROM: binary_read failed during seek!\n");

        return -1;
    }

    if (fread(buffer, count, 1, tf->fp) != 1) {
        cdrom_image_backend_log("CDROM: binary_read failed during read!\n");

        return -1;
    }

    if (UNLIKELY(tf->motorola)) {
        for (uint64_t i = 0; i < count; i += 2) {
            uint8_t buffer0 = buffer[i];
            uint8_t buffer1 = buffer[i + 1];
            buffer[i] = buffer1;
            buffer[i + 1] = buffer0;
        }
    }

    return 1;
}

static uint64_t
bin_get_length(void *priv)
{
    track_file_t *tf = NULL;

    if ((tf = (track_file_t *) priv)->fp == NULL)
        return 0;

    fseeko64(tf->fp, 0, SEEK_END);
    const off64_t len = ftello64(tf->fp);
    cdrom_image_backend_log("CDROM: binary_length(%08lx) = %" PRIu64 "\n", tf->fp, len);

    return len;
}

static void
bin_close(void *priv)
{
    track_file_t *tf = (track_file_t *) priv;

    if (tf == NULL)
        return;

    if (tf->fp != NULL) {
        fclose(tf->fp);
        tf->fp = NULL;
    }

    memset(tf->fn, 0x00, sizeof(tf->fn));

    free(priv);
}

static track_file_t *
bin_init(const char *filename, int *error)
{
    track_file_t *tf = (track_file_t *) calloc(1, sizeof(track_file_t));
    struct stat   stats;

    if (tf == NULL) {
        *error = 1;
        return NULL;
    }

    memset(tf->fn, 0x00, sizeof(tf->fn));
    strncpy(tf->fn, filename, sizeof(tf->fn) - 1);
    tf->fp = plat_fopen64(tf->fn, "rb");
    cdrom_image_backend_log("CDROM: binary_open(%s) = %08lx\n", tf->fn, tf->fp);

    if (stat(tf->fn, &stats) != 0) {
        /* Use a blank structure if stat failed. */
        memset(&stats, 0, sizeof(struct stat));
    }
    *error = ((tf->fp == NULL) || ((stats.st_mode & S_IFMT) == S_IFDIR));

    /* Set the function pointers. */
    if (!*error) {
        tf->read       = bin_read;
        tf->get_length = bin_get_length;
        tf->close      = bin_close;
    } else {
        /* From the check above, error may still be non-zero if opening a directory.
         * The error is set for viso to try and open the directory following this function.
         * However, we need to make sure the descriptor is closed. */
        if ((tf->fp != NULL) && ((stats.st_mode & S_IFMT) == S_IFDIR)) {
            /* tf is freed by bin_close */
            bin_close(tf);
        } else
            free(tf);
        tf = NULL;
    }

    return tf;
}

static track_file_t *
track_file_init(const char *filename, int *error)
{
    /* Current we only support .BIN files, either combined or one per
       track. In the future, more is planned. */
    return bin_init(filename, error);
}

static void
track_file_close(track_t *trk)
{
    if (trk == NULL)
        return;

    if (trk->file == NULL)
        return;

    if (trk->file->close == NULL)
        return;

    trk->file->close(trk->file);
    trk->file = NULL;
}

/* Root functions. */
static void
cdi_clear_tracks(cd_img_t *cdi)
{
    const track_file_t *last = NULL;
    track_t            *cur  = NULL;

    if ((cdi->tracks == NULL) || (cdi->tracks_num == 0))
        return;

    for (int i = 0; i < cdi->tracks_num; i++) {
        cur = &cdi->tracks[i];

        /* Make sure we do not attempt to close a NULL file. */
        if (cur->file != last) {
            last = cur->file;
            track_file_close(cur);
        } else
            cur->file = NULL;
    }

    /* Now free the array. */
    free(cdi->tracks);
    cdi->tracks = NULL;

    /* Mark that there's no tracks. */
    cdi->tracks_num = 0;
}

void
cdi_close(cd_img_t *cdi)
{
    cdi_clear_tracks(cdi);
    free(cdi);
}

int
cdi_set_device(cd_img_t *cdi, const char *path)
{
    int ret;

    if ((ret = cdi_load_cue(cdi, path)))
        return ret;

    if ((ret = cdi_load_iso(cdi, path)))
        return ret;

    return 0;
}

void
cdi_get_audio_tracks(cd_img_t *cdi, int *st_track, int *end, TMSF *lead_out)
{
    *st_track = 1;
    *end      = cdi->tracks_num - 1;
    FRAMES_TO_MSF(cdi->tracks[*end].start + 150, &lead_out->min, &lead_out->sec, &lead_out->fr);
}

void
cdi_get_audio_tracks_lba(cd_img_t *cdi, int *st_track, int *end, uint32_t *lead_out)
{
    *st_track = 1;
    *end      = cdi->tracks_num - 1;
    *lead_out = cdi->tracks[*end].start;
}

int
cdi_get_audio_track_pre(cd_img_t *cdi, int track)
{
    const track_t *trk = &cdi->tracks[track - 1];

    if ((track < 1) || (track > cdi->tracks_num))
        return 0;

    return trk->pre;
}

/* This replaces both Info and EndInfo, they are specified by a variable. */
int
cdi_get_audio_track_info(cd_img_t *cdi, UNUSED(int end), int track, int *track_num, TMSF *start, uint8_t *attr)
{
    const track_t *trk = &cdi->tracks[track - 1];
    // const int      pos = trk->start + 150;
    const int      pos = trk->indexes[1].start;

    if ((track < 1) || (track > cdi->tracks_num))
        return 0;

    FRAMES_TO_MSF(pos, &start->min, &start->sec, &start->fr);

    *track_num = trk->track_number;
    *attr      = trk->attr;

    return 1;
}

int
cdi_get_audio_track_info_lba(cd_img_t *cdi, UNUSED(int end), int track, int *track_num, uint32_t *start, uint8_t *attr)
{
    const track_t *trk = &cdi->tracks[track - 1];

    if ((track < 1) || (track > cdi->tracks_num))
        return 0;

    *start = (uint32_t) trk->start;
    // *start     = (uint32_t) trk->indexes[1].start - 150ULL;

    *track_num = trk->track_number;
    *attr      = trk->attr;

    return 1;
}

void
cdi_get_raw_track_info(cd_img_t *cdi, int *num, uint8_t *buffer)
{
    TMSF              tmsf;
    int               track_num   = 0;
    uint8_t           attr        = 0;
    int               len         = 0;
    int               first_track;
    int               last_track;

    cdi_get_audio_tracks(cdi, &first_track, &last_track, &tmsf);

    *num = last_track + 3;

    cdi_get_audio_track_info(cdi, 0, 1, &track_num, &tmsf, &attr);

    buffer[len++] = 1;         /* Session number */
    buffer[len++] = attr;      /* Track ADR and Control */
    buffer[len++] = 0;         /* TNO (always 0) */
    buffer[len++] = 0xA0;      /* Point (for track points - track number) */
    buffer[len++] = 0;
    buffer[len++] = 0;
    buffer[len++] = 0;
    buffer[len++] = 0;
    buffer[len++] = track_num; /* First track number */
    buffer[len++] = 0;
    buffer[len++] = 0;

    cdi_get_audio_track_info(cdi, 0, last_track, &track_num, &tmsf, &attr);

    buffer[len++] = 1;         /* Session number */
    buffer[len++] = attr;      /* Track ADR and Control */
    buffer[len++] = 0;         /* TNO (always 0) */
    buffer[len++] = 0xA1;      /* Point (for track points - track number) */
    buffer[len++] = 0;
    buffer[len++] = 0;
    buffer[len++] = 0;
    buffer[len++] = 0;
    buffer[len++] = track_num; /* Last track number */
    buffer[len++] = 0;
    buffer[len++] = 0;

    cdi_get_audio_track_info(cdi, 0, last_track + 1, &track_num, &tmsf, &attr);

    cdrom_image_backend_log("    tracks(%i) = %02X, %02X, %02i:%02i.%02i\n", last_track, attr,
                            track_num, tmsf.min, tmsf.sec, tmsf.fr);

    buffer[len++] = 1;         /* Session number */
    buffer[len++] = attr;      /* Track ADR and Control */
    buffer[len++] = 0;         /* TNO (always 0) */
    buffer[len++] = 0xA2;      /* Point (for track points - track number) */
    buffer[len++] = 0;
    buffer[len++] = 0;
    buffer[len++] = 0;
    buffer[len++] = 0;
    buffer[len++] = tmsf.min;  /* PM */
    buffer[len++] = tmsf.sec;  /* PS */
    buffer[len++] = tmsf.fr;   /* PF */

    for (int i = 0; i < last_track; i++) {
        cdi_get_audio_track_info(cdi, 0, i + 1, &track_num, &tmsf, &attr);

        cdrom_image_backend_log("    tracks(%i) = %02X, %02X, %02i:%02i.%02i\n", i, attr,
                                track_num, tmsf.min, tmsf.sec, tmsf.fr);

        buffer[len++] = 1;         /* Session number */
        buffer[len++] = attr;      /* Track ADR and Control */
        buffer[len++] = 0;         /* TNO (always 0) */
        buffer[len++] = track_num; /* Point (for track points - track number) */
        /* Yes, this is correct - MSF followed by PMSF, the actual position is in PMSF. */
        buffer[len++] = 0;
        buffer[len++] = 0;
        buffer[len++] = 0;
        buffer[len++] = 0;
        buffer[len++] = tmsf.min;  /* PM */
        buffer[len++] = tmsf.sec;  /* PS */
        buffer[len++] = tmsf.fr;   /* PF */
    }
}

int
cdi_get_track(cd_img_t *cdi, uint32_t sector)
{
    /* There must be at least two tracks - data and lead out. */
    if (cdi->tracks_num < 2)
        return -1;

    /* This has a problem - the code skips the last track, which is
       lead out - is that correct? */
    for (int i = 0; i < (cdi->tracks_num - 1); i++) {
        const track_t *cur  = &cdi->tracks[i];
        const track_t *next = &cdi->tracks[i + 1];

        /* Take into account cue sheets that do not start on sector 0. */
        // if ((i == 0) && (sector < cur->start))
        if ((i == 0) && (sector < cur->indexes[0].start))
            return cur->number;

        // if ((cur->start <= sector) && (sector < next->start))
        if ((cur->indexes[0].start <= sector) && (sector < next->indexes[0].start))
            return cur->number;
    }

    return -1;
}

/* TODO: See if track start is adjusted by 150 or not. */
int
cdi_get_audio_sub(cd_img_t *cdi, uint32_t sector, uint8_t *attr, uint8_t *track, uint8_t *index, TMSF *rel_pos, TMSF *abs_pos)
{
    const int cur_track = cdi_get_track(cdi, sector);

    if (cur_track < 1)
        return 0;

    *track             = (uint8_t) cur_track;
    const track_t *trk = &cdi->tracks[*track - 1];
    *attr              = trk->attr;
    *index             = 1;

    FRAMES_TO_MSF(sector + 150, &abs_pos->min, &abs_pos->sec, &abs_pos->fr);

    /* Absolute position should be adjusted by 150, not the relative ones. */
    // FRAMES_TO_MSF(sector - trk->start, &rel_pos->min, &rel_pos->sec, &rel_pos->fr);
    /* Relative position is relative Index 1 start - pre-gap values will be negative. */
    FRAMES_TO_MSF((int32_t) (sector + 150 - trk->indexes[1].start), &rel_pos->min, &rel_pos->sec, &rel_pos->fr);

    return 1;
}

static int
cdi_get_sector_index(const track_t *trk, const uint32_t sector)
{
    int ret = 1;

    if ((sector + 150) < trk->indexes[1].start)
        ret = 0;
    else if ((sector + 150) >= trk->indexes[2].start)
        ret = 2;

    return ret;
}

static __inline int
bin2bcd(int x)
{
    return (x % 10) | ((x / 10) << 4);
}

int
cdi_read_sector(cd_img_t *cdi, uint8_t *buffer, int raw, uint32_t sector)
{
    const int      track = cdi_get_track(cdi, sector) - 1;
    const uint64_t sect  = (uint64_t) sector;
    int            raw_size;
    int            cooked_size;
    uint64_t       offset;
    int            m     = 0;
    int            s     = 0;
    int            f     = 0;
    int            ret   = 0;
    uint8_t        q[16] = { 0x00 };

    if (track < 0)
        return 0;

    const track_t *trk          = &cdi->tracks[track];
    const int      track_is_raw = ((trk->sector_size == RAW_SECTOR_SIZE) || (trk->sector_size == 2448));

    const uint64_t seek         = trk->skip + ((sect - trk->start) * trk->sector_size);
    const int      index        = cdi_get_sector_index(trk, sector);
    cdrom_image_backend_log("cdrom_read_sector(%08X): track %02X, index %02X, %016" PRIX64 ", %016" PRIX64 ", %i\n",
                            sector, track, index, trk->skip, trk->start, trk->sector_size);

    if (track_is_raw)
        raw_size = trk->sector_size;
    else
        raw_size = 2448;

    if (trk->mode2 && (trk->form != 1)) {
        if (trk->form == 2)
            cooked_size = (track_is_raw ? 2328 : trk->sector_size); /* Both 2324 + ECC and 2328 variants are valid. */
        else
            cooked_size = 2336;
    } else
        cooked_size = COOKED_SECTOR_SIZE;

    const size_t length = (raw ? raw_size : cooked_size);

    if (trk->mode2 && (trk->form >= 1))
        offset = 24ULL;
    else
        offset = 16ULL;

    if (!trk->indexes[index].in_file) {
        memset(buffer, 0x00, 2448);
        if (trk->attr & 0x04) {
            /* Construct the rest of the raw sector. */
            memset(buffer + 1, 0xff, 10);
            buffer += 12;
            FRAMES_TO_MSF(sector + 150, &m, &s, &f);
            /* These have to be BCD. */
            buffer[0] = CDROM_BCD(m & 0xff);
            buffer[1] = CDROM_BCD(s & 0xff);
            buffer[2] = CDROM_BCD(f & 0xff);
            /* Data, should reflect the actual sector type. */
            buffer[3] = trk->mode2 ? 2 : 1;
            ret = 1;
        }
    } else if (raw && !track_is_raw) {
        memset(buffer, 0x00, 2448);
        const int temp = trk->file->read(trk->file, buffer + offset, seek, length);
        if (temp <= 0)
            return temp;
        if (trk->attr & 0x04) {
            /* Construct the rest of the raw sector. */
            memset(buffer + 1, 0xff, 10);
            buffer += 12;
            FRAMES_TO_MSF(sector + 150, &m, &s, &f);
            /* These have to be BCD. */
            buffer[0] = CDROM_BCD(m & 0xff);
            buffer[1] = CDROM_BCD(s & 0xff);
            buffer[2] = CDROM_BCD(f & 0xff);
            /* Data, should reflect the actual sector type. */
            buffer[3] = trk->mode2 ? 2 : 1;
            ret = 1;
        }
    } else if (!raw && track_is_raw)
        return trk->file->read(trk->file, buffer, seek + offset, length);
    else {
        ret = trk->file->read(trk->file, buffer, seek, length);
        if (raw && (raw_size == 2448))
            return ret;
    }

    /* Construct Q. */
    q[0] = (trk->attr >> 4) | ((trk->attr & 0xf) << 4);
    q[1] = bin2bcd(trk->track_number);
    q[2] = 1; /* TODO: Index number. */
    // FRAMES_TO_MSF(sector - trk->start, &m, &s, &f);
    FRAMES_TO_MSF((int32_t) (sector + 150 - trk->indexes[1].start), &m, &s, &f);
    q[3] = bin2bcd(m);
    q[4] = bin2bcd(s);
    q[5] = bin2bcd(f);
    FRAMES_TO_MSF(sector + 150, &m, &s, &f);
    q[7] = bin2bcd(m);
    q[8] = bin2bcd(s);
    q[9] = bin2bcd(f);

    /* Construct raw subchannel data from Q only. */
    for (int i = 0; i < 12; i++)
         for (int j = 0; j < 8; j++)
              buffer[2352 + (i << 3) + j] = ((q[i] >> (7 - j)) & 0x01) << 6;

    return ret;
}

int
cdi_read_sectors(cd_img_t *cdi, uint8_t *buffer, int raw, uint32_t sector, uint32_t num)
{
    int success = 1;

    /* TODO: This fails to account for Mode 2. Shouldn't we have a function
             to get sector size? */
    const int      sector_size = raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE;
    const uint32_t buf_len     = num * sector_size;
    uint8_t       *buf         = (uint8_t *) calloc(1, buf_len * sizeof(uint8_t));

    for (uint32_t i = 0; i < num; i++) {
        success = cdi_read_sector(cdi, &buf[i * sector_size], raw, sector + i);
        if (success <= 0)
            break;
        /* Based on the DOSBox patch, but check all 8 bytes and makes sure it's not an
           audio track. */
        if (raw && (sector < cdi->tracks[0].length) && !cdi->tracks[0].mode2 && (cdi->tracks[0].attr != AUDIO_TRACK) && *(uint64_t *) &(buf[(i * sector_size) + 2068]))
            return 0;
    }

    memcpy((void *) buffer, buf, buf_len);
    free(buf);
    buf = NULL;

    return success;
}

/* TODO: Do CUE+BIN images with a sector size of 2448 even exist? */
int
cdi_read_sector_sub(cd_img_t *cdi, uint8_t *buffer, uint32_t sector)
{
    const int track = cdi_get_track(cdi, sector) - 1;

    if (track < 0)
        return 0;

    const track_t *trk  = &cdi->tracks[track];
    const uint64_t seek = trk->skip + (((uint64_t) sector - trk->start) * trk->sector_size);
    if (trk->sector_size != 2448)
        return 0;

    return trk->file->read(trk->file, buffer, seek, 2448);
}

int
cdi_get_sector_size(cd_img_t *cdi, uint32_t sector)
{
    const int track = cdi_get_track(cdi, sector) - 1;

    if (track < 0)
        return 0;

    const track_t *trk = &cdi->tracks[track];
    return trk->sector_size;
}

int
cdi_is_mode2(cd_img_t *cdi, uint32_t sector)
{
    const int track = cdi_get_track(cdi, sector) - 1;

    if (track < 0)
        return 0;

    const track_t *trk = &cdi->tracks[track];

    return !!(trk->mode2);
}

int
cdi_get_mode2_form(cd_img_t *cdi, uint32_t sector)
{
    const int track = cdi_get_track(cdi, sector) - 1;

    if (track < 0)
        return 0;

    const track_t *trk = &cdi->tracks[track];

    return trk->form;
}

static int
cdi_can_read_pvd(track_file_t *file, uint64_t sector_size, int mode2, int form)
{
    uint8_t  pvd[COOKED_SECTOR_SIZE];
    uint64_t seek = 16ULL * sector_size; /* First VD is located at sector 16. */

    if (sector_size == RAW_SECTOR_SIZE) {
        if (mode2 && (form > 0))
            seek += 24;
        else
            seek += 16;
    } else if (form > 0)
        seek += 8;

    file->read(file, pvd, seek, COOKED_SECTOR_SIZE);

    return ((pvd[0] == 1 && !strncmp((char *) (&pvd[1]), "CD001", 5) && pvd[6] == 1) || (pvd[8] == 1 && !strncmp((char *) (&pvd[9]), "CDROM", 5) && pvd[14] == 1));
}

/* This reallocates the array and returns the pointer to the last track. */
static void
cdi_track_push_back(cd_img_t *cdi, track_t *trk)
{
    /* This has to be done so situations in which realloc would misbehave
       can be detected and reported to the user. */
    if ((cdi->tracks != NULL) && (cdi->tracks_num == 0))
        fatal("CD-ROM Image: Non-null tracks array at 0 loaded tracks\n");
    if ((cdi->tracks == NULL) && (cdi->tracks_num != 0))
        fatal("CD-ROM Image: Null tracks array at non-zero loaded tracks\n");

    cdi->tracks = realloc(cdi->tracks, (cdi->tracks_num + 1) * sizeof(track_t));
    memcpy(&(cdi->tracks[cdi->tracks_num]), trk, sizeof(track_t));
    cdi->tracks_num++;
}

int
cdi_get_iso_track(cd_img_t *cdi, track_t *trk, const char *filename)
{
    int error = 0;
    int ret = 2;
    memset(trk, 0, sizeof(track_t));

    /* Data track (shouldn't there be a lead in track?). */
    trk->file = bin_init(filename, &error);
    if (error) {
        if ((trk->file != NULL) && (trk->file->close != NULL))
            trk->file->close(trk->file);
        ret       = 3;
        trk->file = viso_init(filename, &error);
        if (error) {
            if ((trk->file != NULL) && (trk->file->close != NULL))
                trk->file->close(trk->file);
            return 0;
        }
    }
    trk->number       = 1;
    trk->track_number = 1;
    trk->attr         = DATA_TRACK;

    /* Try to detect ISO type. */
    trk->form  = 0;
    trk->mode2 = 0;

    if (cdi_can_read_pvd(trk->file, RAW_SECTOR_SIZE, 0, 0))
        trk->sector_size = RAW_SECTOR_SIZE;
    else if (cdi_can_read_pvd(trk->file, 2336, 1, 0)) {
        trk->sector_size = 2336;
        trk->mode2       = 1;
    } else if (cdi_can_read_pvd(trk->file, 2324, 1, 2)) {
        trk->sector_size = 2324;
        trk->mode2       = 1;
        trk->form        = 2;
        trk->noskip      = 1;
    } else if (cdi_can_read_pvd(trk->file, 2328, 1, 2)) {
        trk->sector_size = 2328;
        trk->mode2       = 1;
        trk->form        = 2;
        trk->noskip      = 1;
    } else if (cdi_can_read_pvd(trk->file, 2336, 1, 1)) {
        trk->sector_size = 2336;
        trk->mode2       = 1;
        trk->form        = 1;
        trk->skip        = 8;
    } else if (cdi_can_read_pvd(trk->file, RAW_SECTOR_SIZE, 1, 0)) {
        trk->sector_size = RAW_SECTOR_SIZE;
        trk->mode2       = 1;
    } else if (cdi_can_read_pvd(trk->file, RAW_SECTOR_SIZE, 1, 1)) {
        trk->sector_size = RAW_SECTOR_SIZE;
        trk->mode2       = 1;
        trk->form        = 1;
    } else {
        /* We use 2048 mode 1 as the default. */
        trk->sector_size = COOKED_SECTOR_SIZE;
    }

    trk->length = trk->file->get_length(trk->file) / trk->sector_size;

    trk->indexes[0].in_file = 0;
    trk->indexes[0].start   = 0;
    trk->indexes[0].length  = 150;
    trk->indexes[1].in_file = 1;
    trk->indexes[1].start   = 150;
    trk->indexes[1].length  = trk->length;
    trk->indexes[2].in_file = 0;
    trk->indexes[2].start   = trk->length + 150;
    trk->indexes[2].length  = 0;

    cdrom_image_backend_log("ISO: Data track: length = %" PRIu64 ", sector_size = %i\n", trk->length, trk->sector_size);
    return ret;
}

int
cdi_load_iso(cd_img_t *cdi, const char *filename)
{
    int     ret = 2;
    track_t trk = { 0 };

    cdi->tracks     = NULL;
    cdi->tracks_num = 0;

    ret = cdi_get_iso_track(cdi, &trk, filename);

    if (ret >= 1) {
        cdi_track_push_back(cdi, &trk);

        /* Lead out track. */
        trk.number       = 2;
        trk.track_number = 0xAA;
        // trk.attr         = 0x16; /* Was originally 0x00, but I believe 0x16 is appropriate. */
        trk.start        = trk.length;
        trk.file         = NULL;

        for (int i = 0; i < 3; i++) {
            trk.indexes[i].in_file = 0;
            trk.indexes[i].start   = trk.length + 150;
            trk.indexes[i].length  = 0;
        }

        trk.length       = 0;

        cdi_track_push_back(cdi, &trk);
    }

    return ret;
}

static int
cdi_cue_get_buffer(char *str, char **line, int up)
{
    char *s     = *line;
    char *p     = str;
    int   quote = 0;
    int   done  = 0;
    int   space = 1;

    /* Copy to local buffer until we have end of string or whitespace. */
    while (!done) {
        switch (*s) {
            case '\0':
                if (quote) {
                    /* Ouch, unterminated string.. */
                    return 0;
                }
                done = 1;
                break;

            case '\"':
                quote ^= 1;
                break;

            case ' ':
            case '\t':
                if (space)
                    break;

                if (!quote) {
                    done = 1;
                    break;
                }
                fallthrough;

            default:
                if (up && islower((int) *s))
                    *p++ = toupper((int) *s);
                else
                    *p++ = *s;
                space = 0;
                break;
        }

        if (!done)
            s++;
    }
    *p = '\0';

    *line = s;

    return 1;
}

static int
cdi_cue_get_keyword(char **dest, char **line)
{
    int success;

    success = cdi_cue_get_buffer(temp_keyword, line, 1);
    if (success)
        *dest = temp_keyword;

    return success;
}

/* Get a string from the input line, handling quotes properly. */
static uint64_t
cdi_cue_get_number(char **line)
{
    char     temp[128];
    uint64_t num;

    if (!cdi_cue_get_buffer(temp, line, 0))
        return 0;

    if (sscanf(temp, "%" PRIu64, &num) != 1)
        return 0;

    return num;
}

static int
cdi_cue_get_frame(uint64_t *frames, char **line)
{
    char temp[128];
    int  min = 0;
    int  sec = 0;
    int  fr  = 0;
    int  success;

    success = cdi_cue_get_buffer(temp, line, 0);
    if (!success)
        return 0;

    success = sscanf(temp, "%d:%d:%d", &min, &sec, &fr) == 3;
    if (!success)
        return 0;

    *frames = MSF_TO_FRAMES(min, sec, fr);

    return 1;
}

static int
cdi_cue_get_flags(track_t *cur, char **line)
{
    char temp[128];
    char temp2[128];
    int  success;

    success = cdi_cue_get_buffer(temp, line, 0);
    if (!success)
        return 0;

    memset(temp2, 0x00, sizeof(temp2));
    success = sscanf(temp, "%s", temp2) == 1;
    if (!success)
        return 0;

    cur->pre = (strstr(temp2, "PRE") != NULL);

    return 1;
}

uint64_t total_pregap  = 0ULL;

static int
cdi_add_track(cd_img_t *cdi, track_t *cur, uint64_t *shift, uint64_t prestart, uint64_t cur_pregap)
{
    /* Frames between index 0 (prestart) and 1 (current track start) must be skipped. */
    track_t *prev = NULL;

    /* Skip *MUST* be calculated even if prestart is 0. */
    if (prestart > cur->start)
        return 0;
    /* If prestart is 0, there is no skip. */
    uint64_t skip = (prestart == 0) ? 0 : (cur->start - prestart);

    if ((cdi->tracks != NULL) && (cdi->tracks_num != 0))
        prev = &cdi->tracks[cdi->tracks_num - 1];
    else if ((cdi->tracks == NULL) && (cdi->tracks_num != 0)) {
        fatal("NULL cdi->tracks with non-zero cdi->tracks_num\n");
        return 0;
    }

    /* First track (track number must be 1). */
    if ((prev == NULL) || (cdi->tracks_num == 0)) {
        /* I guess this makes sure the structure is not filled with invalid data. */
        if (cur->number != 1)
            return 0;
        cur->skip = skip * cur->sector_size;
        if ((cur->sector_size != RAW_SECTOR_SIZE) && (cur->form > 0) && !cur->noskip)
            cur->skip += 8;
        cur->start += cur_pregap;
        cdi_track_push_back(cdi, cur);
        return 1;
    }

    if (prev->indexes[2].length != 0) {
        prev->indexes[2].start = cur->indexes[0].start - prev->indexes[2].length;
        prev->indexes[1].length = prev->indexes[2].start - prev->indexes[1].start;
        cdrom_image_backend_log("Index 2 (%i): %02i:%02i:%02i\n", prev->indexes[2].in_file,
                                (int) ((prev->indexes[2].start / 75) / 60), (int) ((prev->indexes[2].start / 75) % 60),
                                (int) (prev->indexes[2].start % 75));
    } else if (prev->indexes[2].in_file)
        prev->indexes[2].length = cur->indexes[0].start - prev->indexes[2].start;
    else {
        prev->indexes[1].length = cur->indexes[0].start - prev->indexes[1].start;
        prev->indexes[2].start  = prev->indexes[1].start + prev->indexes[1].length;
        prev->indexes[2].length = 0;
        cdrom_image_backend_log("Index 2 (%i): %02i:%02i:%02i\n", prev->indexes[2].in_file,
                                (int) ((prev->indexes[2].start / 75) / 60), (int) ((prev->indexes[2].start / 75) % 60),
                                (int) (prev->indexes[2].start % 75));
    }

    /* Current track consumes data from the same file as the previous. */
    if (prev->file == cur->file) {
        cur->start += *shift;
        prev->length = cur->start - prev->start - skip;
        cur->skip += prev->skip + (prev->length * prev->sector_size) + (skip * cur->sector_size);
        cur->start += cur_pregap;
    } else {
        const uint64_t temp = prev->file->get_length(prev->file) - (prev->skip);
        prev->length        = temp / ((uint64_t) prev->sector_size);
        if ((temp % prev->sector_size) != 0)
            /* Padding. */
            prev->length++;

        cur->start += prev->start + prev->length + cur_pregap;
        cur->skip = skip * cur->sector_size;
        if ((cur->sector_size != RAW_SECTOR_SIZE) && (cur->form > 0) && !cur->noskip)
            cur->skip += 8;
        *shift += prev->start + prev->length;
    }

    /* Error checks. */
    if (cur->number <= 1)
        return 0;
    if ((prev->number + 1) != cur->number)
        return 0;
    if (cur->start < (prev->start + prev->length))
        return 0;

    cdi_track_push_back(cdi, cur);

    return 1;
}

int
cdi_load_cue(cd_img_t *cdi, const char *cuefile)
{
    track_t  trk;
    char     pathname[MAX_FILENAME_LENGTH];
    uint64_t shift         = 0ULL;
    uint64_t prestart      = 0ULL;
    uint64_t cur_pregap    = 0ULL;
    uint64_t frame         = 0ULL;
    uint64_t index;
    int      iso_file_used = 0;
    int      success;
    int      error;
    int      can_add_track = 0;
    FILE    *fp;
    char     buf[MAX_LINE_LENGTH];
    char    *line;
    char    *command;
    char    *type;

    cdi->tracks     = NULL;
    cdi->tracks_num = 0;

    memset(&trk, 0, sizeof(track_t));

    /* Get a copy of the filename into pathname, we need it later. */
    memset(pathname, 0, MAX_FILENAME_LENGTH * sizeof(char));
    path_get_dirname(pathname, cuefile);

    /* Open the file. */
    fp = plat_fopen(cuefile, "r");
    if (fp == NULL)
        return 0;

    success = 0;

    for (;;) {
        line = buf;

        /* Read a line from the cuesheet file. */
        if (feof(fp) || fgets(buf, sizeof(buf), fp) == NULL || ferror(fp))
            break;

        /* Do two iterations to make sure to nuke even if it's \r\n or \n\r,
           but do checks to make sure we're not nuking other bytes. */
        for (uint8_t i = 0; i < 2; i++) {
            if (strlen(buf) > 0) {
                if (buf[strlen(buf) - 1] == '\n')
                    buf[strlen(buf) - 1] = '\0';
                /* nuke trailing newline */
                else if (buf[strlen(buf) - 1] == '\r')
                    buf[strlen(buf) - 1] = '\0';
                /* nuke trailing newline */
            }
        }
        cdrom_image_backend_log("line = %s\n", line);

        (void) cdi_cue_get_keyword(&command, &line);

        if (!strcmp(command, "TRACK")) {
            if (can_add_track)
                success = cdi_add_track(cdi, &trk, &shift, prestart, cur_pregap);
            else
                success = 1;
            if (!success)
                break;

            cur_pregap       = 0;
            prestart         = 0;
            trk.number       = cdi_cue_get_number(&line);
            trk.track_number = trk.number;
            cdrom_image_backend_log("cdi_load_cue(): Track %02X\n", trk.number);
            success          = cdi_cue_get_keyword(&type, &line);

            memset(trk.indexes, 0x00, sizeof(trk.indexes));

            if (!success)
                break;

            if (iso_file_used) {
                /*
                   We don't alter anything of the detected track type with
                   the one specified in the CUE file, except its numbers.
                 */
                can_add_track    = 1;

                iso_file_used    = 0;
            } else {
                trk.start        = 0;
                trk.skip         = 0;

                trk.form         = 0;
                trk.mode2        = 0;

                trk.pre          = 0;

                if (!strcmp(type, "AUDIO")) {
                    trk.sector_size = RAW_SECTOR_SIZE;
                    trk.attr        = AUDIO_TRACK;
                } else if (!strcmp(type, "MODE1/2048")) {
                    trk.sector_size = COOKED_SECTOR_SIZE;
                    trk.attr        = DATA_TRACK;
                } else if (!strcmp(type, "MODE1/2352")) {
                    trk.sector_size = RAW_SECTOR_SIZE;
                    trk.attr        = DATA_TRACK;
                } else if (!strcmp(type, "MODE1/2448")) {
                    trk.sector_size = 2448;
                    trk.attr        = DATA_TRACK;
                } else if (!strcmp(type, "MODE2/2048")) {
                    trk.form        = 1;
                    trk.sector_size = COOKED_SECTOR_SIZE;
                    trk.attr        = DATA_TRACK;
                    trk.mode2       = 1;
                } else if (!strcmp(type, "MODE2/2324")) {
                    trk.form        = 2;
                    trk.sector_size = 2324;
                    trk.attr        = DATA_TRACK;
                    trk.mode2       = 1;
                } else if (!strcmp(type, "MODE2/2328")) {
                    trk.form        = 2;
                    trk.sector_size = 2328;
                    trk.attr        = DATA_TRACK;
                    trk.mode2       = 1;
                } else if (!strcmp(type, "MODE2/2336")) {
                    trk.form        = 1;
                    trk.sector_size = 2336;
                    trk.attr        = DATA_TRACK;
                    trk.mode2       = 1;
                } else if (!strcmp(type, "MODE2/2352")) {
                    /* Assume this is XA Mode 2 Form 1. */
                    trk.form        = 1;
                    trk.sector_size = RAW_SECTOR_SIZE;
                    trk.attr        = DATA_TRACK;
                    trk.mode2       = 1;
                } else if (!strcmp(type, "MODE2/2448")) {
                    /* Assume this is XA Mode 2 Form 1. */
                    trk.form        = 1;
                    trk.sector_size = 2448;
                    trk.attr        = DATA_TRACK;
                    trk.mode2       = 1;
                } else if (!strcmp(type, "CDG/2448")) {
                    trk.sector_size = 2448;
                    trk.attr        = DATA_TRACK;
                    trk.mode2       = 1;
                } else if (!strcmp(type, "CDI/2336")) {
                    trk.sector_size = 2336;
                    trk.attr        = DATA_TRACK;
                    trk.mode2       = 1;
                } else if (!strcmp(type, "CDI/2352")) {
                    trk.sector_size = RAW_SECTOR_SIZE;
                    trk.attr        = DATA_TRACK;
                    trk.mode2       = 1;
                } else
                    success = 0;

                cdrom_image_backend_log("cdi_load_cue(): Format: %i bytes per sector, %02X, %i, %i\n",
                                        trk.sector_size, trk.attr, trk.mode2, trk.form);

                can_add_track = 1;
            }
        } else if (!strcmp(command, "INDEX")) {
            index   = cdi_cue_get_number(&line);
            success = cdi_cue_get_frame(&frame, &line);

            switch (index) {
                case 0:
                    prestart                   = frame;
                    trk.indexes[0].in_file     = 1;
                    trk.indexes[0].start       = prestart + total_pregap;
                    break;

                case 1:
                    if (trk.indexes[0].in_file)
                        trk.indexes[0].length  = frame - prestart;
                    else if (cur_pregap > 0) {
                        trk.indexes[0].start   = frame + total_pregap;
                        trk.indexes[0].length  = cur_pregap;
                        total_pregap += trk.indexes[0].length;
                    } else if (trk.number == 1) {
                        trk.indexes[0].start   = 0;
                        trk.indexes[0].length  = 150;
                        total_pregap += trk.indexes[0].length;
                    } else {
                        trk.indexes[0].start   = frame + total_pregap;
                        trk.indexes[0].length  = 0;
                    }
                    cdrom_image_backend_log("Index 0 (%i): %02i:%02i:%02i\n", trk.indexes[0].in_file,
                                            (int) ((trk.indexes[0].start / 75) / 60),
                          (int) ((trk.indexes[0].start / 75) % 60),
                          (int) (trk.indexes[0].start % 75));

                    if (cur_pregap > 0)
                        trk.start              = frame + cur_pregap;
                    else
                        trk.start              = frame;
                    trk.indexes[1].start   = trk.indexes[0].start + trk.indexes[0].length;
                    trk.indexes[1].in_file = 1;
                    cdrom_image_backend_log("Index 1 (%i): %02i:%02i:%02i\n", trk.indexes[1].in_file,
                                            (int) ((trk.indexes[1].start / 75) / 60),
                                            (int) ((trk.indexes[1].start / 75) % 60),
                                            (int) (trk.indexes[1].start % 75));
                    break;

                case 2:
                    trk.indexes[2].in_file = 1;
                    if (cur_pregap > 0)
                        trk.indexes[2].start  = frame + cur_pregap;
                    else
                        trk.indexes[2].start  = frame;
                    trk.indexes[1].length = trk.indexes[2].start - trk.indexes[1].start;
                    trk.indexes[2].length = 0;
                    cdrom_image_backend_log("Index 2 (%i): %02i:%02i:%02i\n", trk.indexes[2].in_file,
                                            (int) ((trk.indexes[2].start / 75) / 60),
                                            (int) ((trk.indexes[2].start / 75) % 60),
                                            (int) (trk.indexes[2].start % 75));
                    break;

                default:
                    /* Ignore other indices. */
                    break;
            }
        } else if (!strcmp(command, "FILE")) {
            char filename[MAX_FILENAME_LENGTH];
            char ansi[MAX_FILENAME_LENGTH];

            if (can_add_track)
                success = cdi_add_track(cdi, &trk, &shift, prestart, cur_pregap);
            else
                success = 1;
            if (!success)
                break;
            can_add_track = 0;

            memset(ansi, 0, MAX_FILENAME_LENGTH * sizeof(char));
            memset(filename, 0, MAX_FILENAME_LENGTH * sizeof(char));

            success = cdi_cue_get_buffer(ansi, &line, 0);
            if (!success)
                break;
            success = cdi_cue_get_keyword(&type, &line);
            if (!success)
                break;

            trk.file = NULL;
            error    = 1;

            if (!strcmp(type, "BINARY") || !strcmp(type, "MOTOROLA")) {
                int fn_len = 0;
                if (!path_abs(ansi)) {
                    path_append_filename(filename, pathname, ansi);
                } else {
                    strcpy(filename, ansi);
                }
                fn_len = strlen(filename);
                if ((tolower((int) filename[fn_len - 1]) == 'o'
                    && tolower((int) filename[fn_len - 2]) == 's'
                    && tolower((int) filename[fn_len - 3]) == 'i'
                    && filename[fn_len - 4] == '.')
                    || plat_dir_check(filename)) {
                    error = !cdi_get_iso_track(cdi, &trk, filename);
                    if (!error) {
                        iso_file_used = 1;
                    }
                } else
                    trk.file = track_file_init(filename, &error);
                
                if (trk.file) {
                    trk.file->motorola = !strcmp(type, "MOTOROLA");
                }
            } else if (!strcmp(type, "WAVE") || !strcmp(type, "AIFF") || !strcmp(type, "MP3")) {
                if (!path_abs(ansi)) {
                    path_append_filename(filename, pathname, ansi);
                } else {
                    strcpy(filename, ansi);
                }
                trk.file = audio_init(filename, &error);
            }
            if (error) {
                cdrom_image_backend_log("CUE: cannot open file '%s' in cue sheet!\n",
                                        filename);

                if (trk.file != NULL) {
                    trk.file->close(trk.file);
                    trk.file = NULL;
                }
                success = 0;
            }
        } else if (!strcmp(command, "PREGAP"))
            success = cdi_cue_get_frame(&cur_pregap, &line);
        else if (!strcmp(command, "POSTGAP")) {
            success = cdi_cue_get_frame(&trk.indexes[2].length, &line);
            trk.indexes[2].in_file = 0;
        } else if (!strcmp(command, "FLAGS"))
            success = cdi_cue_get_flags(&trk, &line);
        else if (!strcmp(command, "CATALOG") || !strcmp(command, "CDTEXTFILE") || !strcmp(command, "ISRC") || !strcmp(command, "PERFORMER") || !strcmp(command, "POSTGAP") || !strcmp(command, "REM") || !strcmp(command, "SONGWRITER") || !strcmp(command, "TITLE") || !strcmp(command, "")) {
            /* Ignored commands. */
            success = 1;
        } else {
            cdrom_image_backend_log("CUE: unsupported command '%s' in cue sheet!\n", command);

            success = 0;
        }

        if (!success)
            break;
    }

    fclose(fp);
    if (!success)
        return 0;

    /* Add last track. */
    cdrom_image_backend_log("LEAD OUT\n");
    if (!cdi_add_track(cdi, &trk, &shift, prestart, cur_pregap))
        return 0;

    /* Add lead out track. */
    cdrom_image_backend_log("END OF CUE\n");
    trk.number++;
    memset(trk.indexes, 0x00, sizeof(trk.indexes));
    trk.track_number = 0xAA;
    // trk.attr         = 0x16;
    trk.start        = 0;
    trk.length       = 0;
    trk.file         = NULL;
    if (!cdi_add_track(cdi, &trk, &shift, 0, 0))
        return 0;

    track_t *cur  = &cdi->tracks[cdi->tracks_num - 1];
    track_t *prev = &cdi->tracks[cdi->tracks_num - 2];

    for (int i = 0; i < 3; i++) {
        cur->indexes[i].in_file = 0;
        cur->indexes[i].start   = prev->indexes[1].start + prev->length - 150;
        cur->indexes[i].length  = 0;
    }

    if (prev->indexes[2].length != 0) {
        prev->indexes[2].start = cur->indexes[0].start - prev->indexes[2].length;
        prev->indexes[1].length = prev->indexes[2].start - prev->indexes[1].start;
    } else if (prev->indexes[2].in_file)
        prev->indexes[2].length = cur->indexes[0].start - prev->indexes[2].start;
    else {
        prev->indexes[1].length = cur->indexes[0].start - prev->indexes[1].start;
        prev->indexes[2].start  = prev->indexes[1].start + prev->indexes[1].length;
        prev->indexes[2].length = 0;
    }

#ifdef ENABLE_CDROM_IMAGE_BACKEND_LOG
    for (int i = 0; i < cdi->tracks_num; i++) {
        track_t *t = &(cdi->tracks[i]);
        for (int j = 0; j < 3; j++) {
             track_index_t *ti = &(t->indexes[j]);
             cdrom_image_backend_log("Track %02X.%01X (%i): %02i:%02i:%02i-%02i:%02i:%02i\n",
                                     t->track_number, j, 
                                     ti->in_file,
                                     (int) ((ti->start / 75) / 60),
                                     (int) ((ti->start / 75) % 60),
                                     (int) (ti->start % 75),
                                     (int) (((ti->start + ti->length - 1) / 75) / 60),
                                     (int) (((ti->start + ti->length - 1) / 75) % 60),
                                     (int) ((ti->start + ti->length - 1) % 75));
        }
    }
#endif

    return 1;
}

int
cdi_has_data_track(cd_img_t *cdi)
{
    if ((cdi == NULL) || (cdi->tracks == NULL))
        return 0;

    /* Data track has attribute 0x14. */
    for (int i = 0; i < cdi->tracks_num; i++) {
        if (cdi->tracks[i].attr == DATA_TRACK)
            return 1;
    }

    return 0;
}

int
cdi_has_audio_track(cd_img_t *cdi)
{
    if ((cdi == NULL) || (cdi->tracks == NULL))
        return 0;

    /* Audio track has attribute 0x10. */
    for (int i = 0; i < cdi->tracks_num; i++) {
        if (cdi->tracks[i].attr == AUDIO_TRACK)
            return 1;
    }

    return 0;
}
