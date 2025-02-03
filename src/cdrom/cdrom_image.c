/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CD-ROM image file handling module.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          RichardG, <richardg867@gmail.com>
 *          Cacodemon345
 *
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2016-2025 RichardG.
 *          Copyright 2024-2025 Cacodemon345.
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
#include <sys/stat.h>
#ifndef _WIN32
#    include <libgen.h>
#endif
#include <86box/86box.h>
#include <86box/log.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/cdrom_image_viso.h>

#include <sndfile.h>

#define MAX_LINE_LENGTH     512
#define MAX_FILENAME_LENGTH 256
#define CROSS_LEN           512

static char temp_keyword[1024];

#define INDEX_SPECIAL -2 /* Track A0h onwards. */
#define INDEX_NONE    -1 /* Empty block. */
#define INDEX_ZERO     0 /* Block not in the file, return all 0x00's. */
#define INDEX_NORMAL   1 /* Block in the file. */

typedef struct track_index_t {
    /*
       Is the current block in the file? If not, return all 0x00's. -1 means not
       yet loaded.
    */
    int32_t       type;
    /* The amount of bytes to skip at the beginning of each sector. */
    int32_t       skip;
    /*
       Starting and ending sector LBA - negative in order to accomodate LBA -150 to -1
       to read the pregap of track 1.
     */
    uint64_t      start;
    uint64_t      length;
    uint64_t      file_start;
    uint64_t      file_length;
    track_file_t *file;
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
    uint32_t      sector_size;
    track_index_t idx[3];
} track_t;

typedef struct cd_image_t {
    cdrom_t      *dev;
    void         *log;
    int           is_dvd;
    int           has_audio;
    int32_t       tracks_num;
    uint32_t      bad_sectors_num;
    track_t      *tracks;
    uint32_t     *bad_sectors;
} cd_image_t;

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

typedef struct audio_file_t {
    SNDFILE *file;
    SF_INFO  info;
} audio_file_t;

/* Audio file functions */
static int
audio_read(void *priv, uint8_t *buffer, const uint64_t seek, const size_t count)
{
    const track_file_t *tf            = (track_file_t *) priv;
    const audio_file_t *audio         = (audio_file_t *) tf->priv;
    const uint64_t      samples_seek  = seek / 4;
    const uint64_t      samples_count = count / 4;

    if ((seek & 3) || (count & 3)) {
        image_log(tf->log, "CD Audio file: Reading on non-4-aligned boundaries.\n");
    }

    const sf_count_t res = sf_seek(audio->file, samples_seek, SEEK_SET);

    if (res == -1)
        return 0;

    return !!sf_readf_short(audio->file, (short *) buffer, samples_count);
}

static uint64_t
audio_get_length(void *priv)
{
    const track_file_t *tf    = (track_file_t *) priv;
    const audio_file_t *audio = (audio_file_t *) tf->priv;

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
audio_init(const uint8_t id, const char *filename, int *error)
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

    if (audio->file == NULL) {
        image_log(tf->log, "Audio file open error!");
        goto cleanup_error;
    }

    if (audio->info.channels != 2 || audio->info.samplerate != 44100 || !audio->info.seekable) {
        image_log(tf->log, "Audio file not seekable or in non-CD format!");
        sf_close(audio->file);
        goto cleanup_error;
    }

    *error         = 0;

    tf->priv       = audio;
    tf->fp         = NULL;
    tf->close      = audio_close;
    tf->get_length = audio_get_length;
    tf->read       = audio_read;

    char n[1024]        = { 0 };

    sprintf(n, "CD-ROM %i Audio", id + 1);
    tf->log          = log_open(n);

    return tf;
cleanup_error:
    free(tf);
    free(audio);
    *error = 1;
    return NULL;
}

/* Binary file functions. */
static int
bin_read(void *priv, uint8_t *buffer, const uint64_t seek, const size_t count)
{
    const track_file_t *tf = (track_file_t *) priv;

    if (tf->fp == NULL)
        return 0;

    image_log(tf->log, "binary_read(%08lx, pos=%" PRIu64 " count=%lu)\n",
                    tf->fp, seek, count);

    if (fseeko64(tf->fp, seek, SEEK_SET) == -1) {
        image_log(tf->log, "binary_read failed during seek!\n");

        return -1;
    }

    if (fread(buffer, count, 1, tf->fp) != 1) {
        image_log(tf->log, "binary_read failed during read!\n");

        return -1;
    }

    if (UNLIKELY(tf->motorola)) {
        for (uint64_t i = 0; i < count; i += 2) {
            const uint8_t buffer0 = buffer[i];
            const uint8_t buffer1 = buffer[i + 1];
            buffer[i] = buffer1;
            buffer[i + 1] = buffer0;
        }
    }

    return 1;
}

static uint64_t
bin_get_length(void *priv)
{
    const track_file_t *tf = (track_file_t *) priv;

    if (tf->fp == NULL)
        return 0;

    fseeko64(tf->fp, 0, SEEK_END);
    const off64_t len = ftello64(tf->fp);
    image_log(tf->log, "binary_length(%08lx) = %" PRIu64 "\n", tf->fp, len);

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
bin_init(const uint8_t id, const char *filename, int *error)
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
    image_log(tf->log, "binary_open(%s) = %08lx\n", tf->fn, tf->fp);

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

        char n[1024]        = { 0 };

        sprintf(n, "CD-ROM %i Bin  ", id + 1);
        tf->log          = log_open(n);
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
index_file_init(const uint8_t id, const char *filename, int *error, int *is_viso)
{
    track_file_t *tf = NULL;

    *is_viso = 0;

    /* Current we only support .BIN files, either combined or one per
       track. In the future, more is planned. */
    tf = bin_init(id, filename, error);

    if (*error) {
        if ((tf != NULL) && (tf->close != NULL)) {
            tf->close(tf);
            tf = NULL;
        }

        tf  = viso_init(id, filename, error);

        if (!*error)
            *is_viso = 1;
    }

    return tf;
}

static void
index_file_close(track_index_t *idx)
{
    if ((idx == NULL) || (idx->file == NULL))
        return;

    image_log(idx->file->log, "Log closed\n");

    if (idx->file->log != NULL) {
        log_close(idx->file->log);
        idx->file->log = NULL;
    }

    if (idx->file->close != NULL)
        idx->file->close(idx->file);

    idx->file = NULL;
}

/* Internal functions. */
static int
image_get_track(const cd_image_t *img, const uint32_t sector)
{
    int ret = -1;

    for (int i = 0; i < img->tracks_num; i++) {
        track_t *ct = &(img->tracks[i]);
        for (int j = 0; j < 3; j++) {
            const track_index_t *ci = &(ct->idx[j]);
            if ((ci->type >= INDEX_ZERO) && (ci->length != 0ULL) &&
                ((sector + 150) >= ci->start) && ((sector + 150) <= (ci->start + ci->length - 1))) {
                ret = i;
                break;
            }
        }
    }

    return ret;
}

static void
image_get_track_and_index(const cd_image_t *img, const uint32_t sector,
                          int *track, int *index)
{
    *track = -1;
    *index = -1;

    for (int i = 0; i < img->tracks_num; i++) {
        track_t *ct = &(img->tracks[i]);
        for (int j = 0; j < 3; j++) {
            track_index_t *ci = &(ct->idx[j]);
            if ((ci->type >= INDEX_ZERO) && (ci->length != 0ULL) &&
                ((sector + 150) >= ci->start) && ((sector + 150) <= (ci->start + ci->length - 1))) {
                *track = i;
                *index = j;
                break;
            }
        }
    }
}

static int
image_is_sector_bad(const cd_image_t *img, const uint32_t sector)
{
    int ret = 0;

    if (img->bad_sectors_num > 0)  for (int i = 0; i < img->bad_sectors_num; i++)
        if (img->bad_sectors[i] == sector) {
            ret = 1;
            break;
        }

    return ret;
}

static int
image_is_track_audio(const cd_image_t *img, const uint32_t pos)
{
    int ret   = 0;

    if (img->has_audio) {
        const int track = image_get_track(img, pos);

        if (track >= 0) {
            const track_t *trk = &(img->tracks[track]);

            ret = (trk->mode == 0);
        }
    }

    return ret;
}

static int
image_can_read_pvd(track_file_t *file, const uint64_t start,
                   const uint64_t sector_size, const int xa)
{
    uint8_t  buf[2448] = { 0 };
    /* First VD is located at sector 16. */
    uint64_t seek = start + (16ULL * sector_size);
    uint8_t *pvd  = (uint8_t *) buf;

    if (sector_size >= RAW_SECTOR_SIZE) {
        if (xa)
            pvd = &(buf[24]);
        else
            pvd = &(buf[16]);
    } else if (sector_size >= 2332) {
        if (xa)
            pvd = &(buf[8]);
    }

    file->read(file, buf, seek, sector_size);

    int ret = (((pvd[0] == 1) &&
                !strncmp((char *) &(pvd[1]), "CD001", 5) &&
                (pvd[6] == 1)) ||
               ((pvd[8] == 1) &&
                !strncmp((char *) &(pvd[9]), "CDROM", 5) &&
                (pvd[14] == 1)));

    if (ret) {
        if (sector_size >= RAW_SECTOR_SIZE) {
            if (xa)
                /* Mode 2 XA, Form from the sub-header. */
                ret = 0x20 | (((buf[18] & 0x20) >> 5) + 1);
            else
                /* Mode from header. */
                ret = buf[15] << 4;
        } else if (sector_size >= 2332) {
            if (xa)
                /* Mode 2 XA, Form from the sub-header. */
                ret = 0x20 | (((buf[2] & 0x20) >> 5) + 1);
            else
                /* Mode 2 non-XA. */
                ret = 0x20;
        } else if (sector_size >= 2324)
            /* Mode 2 XA Form 2. */
            ret = 0x22;
        else if (!strncmp((char *) &(pvd[0x400]), "CD-XA001", 8))
            /* Mode 2 XA Form 1. */
            ret = 0x21;
        else
            /* Mode 1. */
            ret = 0x10;
    }

    return ret;
}

static int
image_cue_get_buffer(char *str, char **line, const int up)
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
image_cue_get_keyword(char **dest, char **line)
{
    const int success = image_cue_get_buffer(temp_keyword, line, 1);

    if (success)
        *dest = temp_keyword;

    return success;
}

/* Get a string from the input line, handling quotes properly. */
static uint64_t
image_cue_get_number(char **line)
{
    char     temp[128];
    uint64_t num;

    if (!image_cue_get_buffer(temp, line, 0))
        return 0;

    if (sscanf(temp, "%" PRIu64, &num) != 1)
        return 0;

    return num;
}

static int
image_cue_get_frame(uint64_t *frames, char **line)
{
    char temp[128];
    int  min = 0;
    int  sec = 0;
    int  fr  = 0;

    int  success = image_cue_get_buffer(temp, line, 0);
    if (!success)
        return 0;

    success = sscanf(temp, "%d:%d:%d", &min, &sec, &fr) == 3;
    if (!success)
        return 0;

    *frames = MSF_TO_FRAMES(min, sec, fr);

    return 1;
}

static int
image_cue_get_flags(track_t *cur, char **line)
{
    char temp[128];
    char temp2[128];

    int success = image_cue_get_buffer(temp, line, 0);
    if (!success)
        return 0;

    memset(temp2, 0x00, sizeof(temp2));
    success = sscanf(temp, "%s", temp2) == 1;
    if (!success)
        return 0;

    if (strstr(temp2, "PRE") != NULL)
        cur->attr |= 0x01;
    if (strstr(temp2, "DCP") != NULL)
        cur->attr |= 0x02;
    if (strstr(temp2, "4CH") != NULL)
        cur->attr |= 0x08;

    return 1;
}

static track_t *
image_insert_track(cd_image_t *img, const uint8_t session, const uint8_t point)
{
    track_t       *ct      = NULL;

    img->tracks_num++;
    if (img->tracks == NULL) {
        img->tracks = calloc(1, sizeof(track_t));
        ct          = &(img->tracks[0]);
    } else {
        img->tracks = realloc(img->tracks, img->tracks_num * sizeof(track_t));
        ct          = &(img->tracks[img->tracks_num - 1]);
    }

    image_log(img->log, "    [TRACK   ] Insert %02X: img->tracks[%2i]\n",
              point, img->tracks_num - 1);

    memset(ct, 0x00, sizeof(track_t));

    ct->session = session;
    ct->point   = point;

    for (int i = 0; i < 3; i++)
        ct->idx[i].type = (point > 99) ? INDEX_SPECIAL : INDEX_NONE;

     if (point >= 0xb0)
         ct->attr = 0x50;

    return ct;
}

static void
image_process(cd_image_t *img)
{
    track_t       *ct       = NULL;
    track_t       *lt       = NULL;
    track_index_t *ci       = NULL;
    track_file_t  *tf       = NULL;
    uint64_t       tf_len   = 0ULL;
    uint64_t       cur_pos  = 0ULL;
    int            pos      = 0;
    int            ls       = 0;
    int            map[256] = { 0 };
    int            lead[3]  = { 0 };
    uint64_t       spg[256] = { 0ULL };
    track_t       *lo[256]  = { 0 };

    /*
       Pass 2 - adjusting pre-gaps of the first track of every session and creating the
       map so we can map from <A0/A1/A2> <01-99> <B0/C0> to <01-99> <A0/A1/A2> <B0/C0>
       so that their times and length can be adjusted correctly in the third and fourth
       passes - especially important for multi-session Cue files.

       We have to do that because Cue sheets do not explicitly indicate those pre-gaps
       but they are required so we have the correct frames - the first track of each
       session always has a pre-gap of at least 0:02:00. We do not adjust it if it is
       already present.
     */
    image_log(img->log, "Pass 2 (adjusting pre-gaps and preparing map)...\n");

    /* Pre-gap of the first track of the first session. */
    ct = &(img->tracks[3]);
    ci = &(ct->idx[0]);

    if (ci->type == INDEX_NONE) {
        ci->type   = INDEX_ZERO;
        ci->start  = 0ULL;
        ci->length = 150ULL;
    }

    image_log(img->log, "    [PREGAP  ] Adjusted pre-gap of track %02X (first in "
                    "session %i)\n", ct->point, ct->session);

    /* The other pre-gaps and the preparation of the map. */   
    for (int i = 0; i < img->tracks_num; i++) {
        ct = &(img->tracks[i]);
        if (((ct->point >= 1) && (ct->point <= 99)) || (ct->point >= 0xb0)) {
            if (ct->point == 0xb0) {
                /* The first track of a session always has a pre-gap of at least 0:02:00. */
                track_t *ft = &(img->tracks[i + (ct->session == 1) + 4]);
                ci          = &(ft->idx[0]);

                if (ci->type == INDEX_NONE) {
                    if (ft->idx[1].type == INDEX_NORMAL) {
                        ci->type       = INDEX_NORMAL;
                        ci->file_start = ft->idx[1].file_start - 150ULL;
                    } else {
                        ci->type       = INDEX_ZERO;
                        ci->start      = 0ULL;
                        ci->length     = 150ULL;
                    }
                }

                image_log(img->log, "    [PREGAP  ] Adjusted pre-gap of track %02X "
                          "(first in session %i)\n", ft->point, ct->session);

                /* Point B0h found, add the previous three lead tracks. */
                for (int j = 0; j < 3; j++) {
                    map[pos] = lead[j];
                    image_log(img->log, "    [REMAP   ] Remap %3i to %3i (%02X)\n", pos,
                              map[pos], 0xa0 + j);
                    pos++;
                }
            }

            /* Add the current track. */
            map[pos] = i;
            image_log(img->log, "    [NORMAL  ] Remap %3i to %3i\n", pos, map[pos]);
            pos++;
        } else if ((ct->point >= 0xa0) && (ct->point <= 0xa2)) {
            /*
               Collect lead track (A0 = first track in session, A1 = last track in session,
               A2 = lead out).
             */
            lead[ct->point & 0x03] = i;

            image_log(img->log, "    [LEAD    ] Lead %i = %3i (%02X)\n", ct->point & 0x03, i,
                      ct->point);
        }
    }

    /* Add the last three lead tracks. */
    for (int i = 0; i < 3; i++) {
         map[pos] = lead[i];
         image_log(img->log, "    [REMAP   ] Remap %3i to %3i (%02X)\n", pos, map[pos],
                   0xa0 + i);
         pos++;
    }

    /*
       If these two mismatch, it is a fatal condition since it means something
       has gone wrong enough that the Cue sheet processing has been messed up.
     */
    if (pos != img->tracks_num)
        log_fatal(img->log, "Something has gone wrong and we have remappped %3i tracks "
                  "instead of the expected %3i\n", pos, img->tracks_num);

    /*
       Pass 3 - adjusting the time lengths of each index of track according to the
       files.

       We have to do that because Cue sheets do not explicitly indicate the lengths
       of track, so we have to deduce them from what the combination of the Cue sheet
       and the various files give us.
     */
    image_log(img->log, "Pass 3 (adjusting track file lengths according to the files)...\n");
    for (int i = (img->tracks_num - 1); i >= 0; i--) {
        ct = &(img->tracks[map[i]]);
        if (ct->idx[1].type != INDEX_SPECIAL) {
            for (int j = 2; j >= 0; j--) {
                ci = &(ct->idx[j]);

                /*
                   If the file is not NULL and is different from the previous file,
                   open it and read its length.
                 */
                if ((ci->file != NULL) && (ci->file != tf)) {
                    tf     = ci->file;
                    if (tf != NULL) {
                        tf_len = tf->get_length(tf) / ct->sector_size;
                        image_log(img->log, "    [FILE    ] File length: %016"
                                  PRIX64 " sectors\n", tf_len);
                    }
                }

                if ((ci->type < INDEX_SPECIAL) || (ci->type > INDEX_NORMAL)) {
                    image_log(img->log, "    [TRACK   ] %02X, INDEX %02X, ATTR %02X,\n",
                              ci->type, j,
                              ct->attr);
                    log_fatal(img->log, "               Unrecognized index type during "
                              "Pass 3: %2i\n",
                              ci->type);
                } else if (ci->type == INDEX_NORMAL) {
                    /* Index was in the cue sheet and is present in the file. */
                    ci->file_length = tf_len - ci->file_start;
                    tf_len -= ci->file_length;
                } else {
                    /* Index was not in the cue sheet or is not present in the file,
                       keep its length at zero. */
                    ci->file_start = tf_len;
                }

                image_log(img->log, "    [TRACK   ] %02X/%02X, INDEX %02X, ATTR %02X, "
                          "MODE %02X/%02X, %8s,\n",
                          ct->session,
                          ct->point, j,
                          ct->attr,
                          ct->mode, ct->form,
                          cit[ci->type + 2]);
                image_log(img->log, "               file_start   = %016"
                          PRIX64 " (%2i:%02i:%02i),\n",
                          ci->file_start,
                          (int) ((ci->file_start / 75) / 60),
                          (int) ((ci->file_start / 75) % 60),
                          (int) (ci->file_start % 75));
                image_log(img->log, "               file_length  = %016"
                          PRIX64 " (%2i:%02i:%02i),\n",
                          ci->file_length,
                          (int) ((ci->file_length / 75) / 60),
                          (int) ((ci->file_length / 75) % 60),
                          (int) (ci->file_length % 75));
                image_log(img->log, "               remaining    = %016"
                          PRIX64 " (%2i:%02i:%02i)\n",
                          tf_len,
                          (int) ((tf_len / 75) / 60),
                          (int) ((tf_len / 75) % 60),
                          (int) (tf_len % 75));
            }
        }
    }

    /*
       Pass 4 - calculating the actual track starts and lengths for the TOC.
     */
    image_log(img->log, "Pass 4 (calculating the actual track starts "
              "and lengths for the TOC)...\n");
    for (int i = 0; i < img->tracks_num; i++) {
        ct = &(img->tracks[map[i]]);
        if (ct->idx[1].type != INDEX_SPECIAL) {
            int session_changed = 0;

            /*
               If the session has changed, store the last session
               and mark that it has changed.
             */
            if (ct->session != ls) {
                ls              = ct->session;
                session_changed = 1;
            }

            for (int j = 0; j < 3; j++) {
                ci = &(ct->idx[j]);

                if ((ci->type < INDEX_SPECIAL) || (ci->type > INDEX_NORMAL)) {
                    image_log(img->log, "    [TRACK   ] %02X, INDEX %02X, ATTR %02X,\n",
                              ci->type, j,
                              ct->attr);
                    log_fatal(img->log, "               Unrecognized index type during "
                              "Pass 4: %2i\n",
                              ci->type);
                } else if (ci->type <= INDEX_NONE)
                    /* Index was not in the cue sheet, keep its length at zero. */
                    ci->start   = cur_pos;
                else if (ci->type == INDEX_ZERO) {
                    /* Index was in the cue sheet and is not present in the file. */
                    ci->start   = cur_pos;
                    cur_pos    += ci->length;
                } else if (ci->type == INDEX_NORMAL) {
                    /* Index was in the cue sheet and is present in the file. */
                    ci->start   = cur_pos;
                    ci->length  = ci->file_length;
                    cur_pos    += ci->file_length;
                }

                image_log(img->log, "    [TRACK   ] %02X/%02X, INDEX %02X, ATTR %02X, "
                          "MODE %02X/%02X, %8s,\n",
                          ct->session,
                          ct->point, j,
                          ct->attr,
                          ct->mode, ct->form,
                          cit[ci->type + 2]);
                image_log(img->log, "               start       = %016"
                         PRIX64 " (%2i:%02i:%02i),\n",
                          ci->start,
                          (int) ((ci->start / 75) / 60),
                          (int) ((ci->start / 75) % 60),
                          (int) (ci->start % 75));
                image_log(img->log, "               length      = %016"
                          PRIX64 " (%2i:%02i:%02i),\n",
                          ci->length,
                          (int) ((ci->length / 75) / 60),
                          (int) ((ci->length / 75) % 60),
                          (int) (ci->length % 75));
                image_log(img->log, "               cur_pos     = %016"
                          PRIX64 " (%2i:%02i:%02i)\n",
                          cur_pos,
                          (int) ((cur_pos / 75) / 60),
                          (int) ((cur_pos / 75) % 60),
                          (int) (cur_pos % 75));

                /* Set the pre-gap of the first track of this session. */
                if (session_changed)
                    spg[ct->session] = ct->idx[0].start;
            }
        }
    }

    /*
       Pass 5 - setting the lead out starts for all sessions.
     */
    image_log(img->log, "Pass 5 (setting the lead out starts for all sessions)...\n");
    for (int i = 0; i <= ls; i++) {
        lo[i] = NULL;
        for (int j = (img->tracks_num - 1); j >= 0; j--) {
            const track_t *jt = &(img->tracks[j]);
            if ((jt->session == i)  && (jt->point >= 1) && (jt->point <= 99)) {
                lo[i] = &(img->tracks[j]);
                image_log(img->log, "    [TRACK   ] %02X/%02X, INDEX %02X, ATTR %02X, "
                          "MODE %02X/%02X, %8s,\n",
                          ct->session,
                          ct->point, j,
                          ct->attr,
                          ct->mode, ct->form,
                          cit[ci->type + 2]);
                image_log(img->log, "               using to calculate the start of session "
                          "%02X lead out\n",
                          ct->session);
                break;
            }
        }
    }

    /*
       Pass 6 - refinining modes and forms, and finalizing all the special tracks.
     */
    image_log(img->log, "Pass 6 (refinining modes and forms, and finalizing "
              "all the special tracks)...\n");
    for (int i = 0; i < img->tracks_num; i++) {
        ct = &(img->tracks[i]);
        lt = NULL;
        switch (ct->point) {
            default:
                break;
            case 1 ... 99:
                ci = &(ct->idx[1]);

                if ((ci->type == INDEX_NORMAL) && (ct->mode >= 1)) {
                    image_log(img->log, "    [TRACK   ] %02X/01, INDEX %02X, ATTR %02X, "
                              "MODE %02X/%02X, %8s,\n",
                              ct->session,
                              ct->point,
                              ct->attr,
                              ct->mode, ct->form,
                              cit[ct->idx[1].type + 2]);

                    /* Override the loaded modes with that we determine here. */
                    int can_read_pvd = image_can_read_pvd(ci->file,
                                                          ci->file_start * ct->sector_size,
                                                          ct->sector_size, 0);
                    ct->skip         = 0;
                    if (can_read_pvd) {
                        ct->mode = can_read_pvd >> 4;
                        ct->form = can_read_pvd & 0xf;
                        if (((ct->sector_size == 2332) || (ct->sector_size == 2336)) &&
                            (ct->form >= 1))
                            ct->skip = 8;
                    } else if (ct->sector_size >= 2332) {
                        can_read_pvd = image_can_read_pvd(ci->file,
                                                          ci->file_start * ct->sector_size,
                                                          ct->sector_size, 1);
                        if (can_read_pvd) {
                            ct->mode = can_read_pvd >> 4;
                            ct->form = can_read_pvd & 0xf;
                            if (((ct->sector_size == 2332) || (ct->sector_size == 2336)) &&
                                (ct->form >= 1))
                                ct->skip = 8;
                        }
                    }

                    image_log(img->log, "             NEW MODE: %02X/%02X\n",
                              ct->mode, ct->form);
                }
                break;
            case 0xa0:
                for (int j = 0; j < img->tracks_num; j++) {
                    track_t *jt = &(img->tracks[j]);
                    if ((jt->session == ct->session)  &&
                        (jt->point >= 1) && (jt->point <= 99)) {
                        lt = jt;
                        break;
                    }
                }

                if (lt != NULL) {
                    int disc_type = 0x00;

                    ct->attr = lt->attr;

                    ct->mode = lt->mode;
                    ct->form = lt->form;

                    if (lt->mode == 2)
                        disc_type = 0x20;

                    for (int j = 0; j < 3; j++) {
                        ci = &(ct->idx[j]);
                        ci->type   = INDEX_ZERO;
                        ci->start  = (lt->point * 60 * 75) + (disc_type * 75);
                        ci->length = 0;

                        image_log(img->log, "    [TRACK   ] %02X/%02X, INDEX %02X, "
                                  "ATTR %02X, MODE %02X/%02X, %8s,\n",
                                  ct->session,
                                  ct->point, j,
                                  ct->attr,
                                  ct->mode, ct->form,
                                  cit[ci->type + 2]);
                        image_log(img->log, "               first track = %02X, "
                                  "disc type = %02X\n",
                                  lt->point, disc_type);
                    }
                }
                break;
            case 0xa1:
                for (int j = (img->tracks_num - 1); j >= 0; j--) {
                    track_t *jt = &(img->tracks[j]);
                    if ((jt->session == ct->session)  && (jt->point >= 1) && (jt->point <= 99)) {
                        lt = jt;
                        break;
                    }
                }

                if (lt != NULL) {
                    ct->attr = lt->attr;

                    ct->mode = lt->mode;
                    ct->form = lt->form;

                    for (int j = 0; j < 3; j++) {
                        ci = &(ct->idx[j]);
                        ci->type   = INDEX_ZERO;
                        ci->start  = (lt->point * 60 * 75);
                        ci->length = 0;

                        image_log(img->log, "    [TRACK   ] %02X/%02X, INDEX %02X, "
                                  "ATTR %02X, MODE %02X/%02X, %8s,\n",
                                  ct->session,
                                  ct->point, j,
                                  ct->attr,
                                  ct->mode, ct->form,
                                  cit[ci->type + 2]);
                        image_log(img->log, "               last track  = %02X\n",
                                  lt->point);
                    }
                }
                break;
            case 0xa2:
                if (lo[ct->session] != NULL) {
                    /*
                       We have a track to use for the calculation, first adjust the track's
                       attribute (ADR/Ctrl), mode, and form to match the last non-special track.
                     */
                    lt = lo[ct->session];

                    ct->attr = lt->attr;

                    ct->mode = lt->mode;
                    ct->form = lt->form;

                    if (ct->idx[1].type != INDEX_NORMAL) {
                        /*
                           Index not normal, therefore, this is not a lead out track from a
                           second or afterwards session of a multi-session Cue sheet, calculate
                           the starting time and update all the indexes accordingly.
                         */
                        const track_index_t *li = &(lt->idx[2]);

                        for (int j = 0; j < 3; j++) {
                            image_log(img->log, "    [TRACK   ] %02X/%02X, INDEX %02X, "
                                      "ATTR %02X, MODE %02X/%02X, %8s,\n",
                                      ct->session,
                                      ct->point, j,
                                      ct->attr,
                                      ct->mode, ct->form,
                                      cit[ci->type + 2]);

                            ci = &(ct->idx[j]);
                            ci->type   = INDEX_ZERO;
                            ci->start  = li->start + li->length;
                            ci->length = 0;

                            image_log(img->log, "               start       = %016" PRIX64
                                      " (%2i:%02i:%02i)\n",
                                      ci->start,
                                      (int) ((ci->start / 75) / 60),
                                      (int) ((ci->start / 75) % 60),
                                      (int) (ci->start % 75));
                        }
                    }
#ifdef ENABLE_IMAGE_LOG
                    else
                        image_log(img->log, "               no start calculation done, "
                                  "already specified\n");
#endif
                }
#ifdef ENABLE_IMAGE_LOG
                else
                    image_log(img->log, "               nothing done, no suitable last track "
                              "found\n");
#endif
                break;
            case 0xb0:
                /*
                   B0: MSF points to the beginning of the pre-gap
                   of the following session's first track.
                 */
                ct->extra[0] = (spg[ct->session + 1] / 75) / 60;
                ct->extra[1] = (spg[ct->session + 1] / 75) % 60;
                ct->extra[2] = spg[ct->session + 1] % 75;

                image_log(img->log, "    [TRACK   ] %02X/%02X, INDEX 01, "
                          "ATTR %02X, MODE %02X/%02X, %8s,\n",
                          ct->session,
                          ct->point,
                          ct->attr,
                          ct->mode, ct->form,
                          cit[ct->idx[1].type + 2]);
                image_log(img->log, "               %02X:%02X:%02X, %02X,\n",
                          ct->extra[0], ct->extra[1], ct->extra[2], ct->extra[3]);

                /*
                   B0 PMSF points to the start of the lead out track
                   of the last session.
                 */
                if (lo[ls] != NULL) {
                    lt                      = lo[ls];
                    const track_index_t *li = &(lt->idx[2]);

                    ct->idx[1].start  = li->start + li->length;

                    image_log(img->log, "               start       = %016" PRIX64
                              " (%2i:%02i:%02i)\n",
                              ct->idx[1].start,
                              (int) ((ct->idx[1].start / 75) / 60),
                              (int) ((ct->idx[1].start / 75) % 60),
                              (int) (ct->idx[1].start % 75));
                }
#ifdef ENABLE_IMAGE_LOG
                else
                    image_log(img->log, "               no start calculation done, "
                              "no suitable last track found\n");
#endif
                break;
        }
    }

#ifdef ENABLE_IMAGE_LOG
    image_log(img->log, "Final tracks list:\n");
    for (int i = 0; i < img->tracks_num; i++) {
        ct = &(img->tracks[i]);
        for (int j = 0; j < 3; j++) {
            ci = &(ct->idx[j]);
            image_log(img->log, "    [TRACK   ] %02X INDEX %02X: [%8s, %016" PRIX64 "]\n",
                      ct->point, j,
                      cit[ci->type + 2], ci->file_start * ct->sector_size);
            image_log(img->log, "               TOC data: %02X %02X %02X "
                      "%%02X %02X %02X %02X 02X %02X %02X %02X\n",
                      ct->session, ct->attr, ct->tno, ct->point,
                      ct->extra[0], ct->extra[1], ct->extra[2], ct->extra[3],
                      (uint32_t) ((ci->start / 75) / 60),
                      (uint32_t) ((ci->start / 75) % 60),
                      (uint32_t) (ci->start % 75));
        }
    }
#endif
}

static void
image_set_track_subch_type(track_t *ct)
{
    if (ct->sector_size == 2448)
        ct->subch_type = 0x08;
    else if (ct->sector_size == 2368)
        ct->subch_type = 0x10;
    else
        ct->subch_type = 0x00;
}

static int
image_load_iso(cd_image_t *img, const char *filename)
{
    track_t       *ct      = NULL;
    track_index_t *ci      = NULL;
    track_file_t  *tf      = NULL;
    int            success = 1;
    int            error   = 1;
    int            is_viso = 0;
    int            sector_sizes[8] = { 2448, 2368, RAW_SECTOR_SIZE, 2336,
                                       2332, 2328, 2324,            COOKED_SECTOR_SIZE };

    img->tracks     = NULL;
    /*
       Pass 1 - loading the ISO image.
     */
    image_log(img->log, "Pass 1 (loading the ISO image)...\n");
    img->tracks_num = 0;

    image_insert_track(img, 1, 0xa0);
    image_insert_track(img, 1, 0xa1);
    image_insert_track(img, 1, 0xa2);

    /* Data track (shouldn't there be a lead in track?). */
    tf = index_file_init(img->dev->id, filename, &error, &is_viso);

    if (error) {
        if (tf != NULL) {
            tf->close(tf);
            tf = NULL;
        }

        success = 0;
    } else if (is_viso)
        success = 3;
 
    if (success) {
        ct = image_insert_track(img, 1, 1);
        ci = &(ct->idx[1]);

        ct->form         = 0;
        ct->mode         = 0;

        for (int i = 0; i < 3; i++)
            ct->idx[i].type = INDEX_NONE;

        ct->attr         = DATA_TRACK;

        /* Try to detect ISO type. */
        ct->mode         = 1;
        ct->form         = 0;

        ci->type         = INDEX_NORMAL;
        ci->file_start   = 0ULL;

        ci->file         = tf;

        for (int i = 0; i < 8; i++) {
            ct->sector_size  = sector_sizes[i];
            int can_read_pvd = image_can_read_pvd(ci->file, 0, ct->sector_size, 0);
            if (can_read_pvd) {
                ct->mode = can_read_pvd >> 4;
                ct->form = can_read_pvd & 0xf;
                if (((ct->sector_size == 2332) || (ct->sector_size == 2336)) &&
                    (ct->form >= 1))
                    ct->skip = 8;
                break;
            } else if (ct->sector_size >= 2332) {
                can_read_pvd = image_can_read_pvd(ci->file, 0, ct->sector_size, 1);
                if (can_read_pvd) {
                    ct->mode = can_read_pvd >> 4;
                    ct->form = can_read_pvd & 0xf;
                    if (((ct->sector_size == 2332) || (ct->sector_size == 2336)) &&
                        (ct->form >= 1))
                        ct->skip = 8;
                    break;
                }
            }
        }

        image_set_track_subch_type(ct);

        image_log(img->log, "    [TRACK   ] %02X/%02X, ATTR %02X, MODE %02X/%02X,\n",
                  ct->session,
                  ct->point,
                  ct->attr,
                  ct->mode, ct->form);
        image_log(img->log, "               %02X:%02X:%02X, %02X, %i\n",
                  ct->sector_size);
    }

    if (success)  for (int i = 2; i >= 0; i--) {
        if (ct->idx[i].file == NULL)
            ct->idx[i].file = tf;
        else
            break;
    }

    tf = NULL;

    if (success)
        image_process(img);
    else {
        image_log(img->log, "    [ISO     ] Unable to open image or folder \"%s\"\n",
                  filename);
        return 0;
    }

    return success;
}

static int
image_load_cue(cd_image_t *img, const char *cuefile)
{
    track_t       *ct                            = NULL;
    track_index_t *ci                            = NULL;
    track_file_t  *tf                            = NULL;
    uint64_t       frame                         = 0ULL;
    uint64_t       last                          = 0ULL;
    uint8_t        session                       = 1;
    int            last_t                        = -1;
    int            is_viso                       = 0;
    int            lead[3]                       = { 0 };
    int            error;
    char           pathname[MAX_FILENAME_LENGTH];
    char           buf[MAX_LINE_LENGTH];
    char          *line;
    char          *command;
    char          *type;
    char           temp;

    img->tracks     = NULL;
    img->tracks_num = 0;

    /* Get a copy of the filename into pathname, we need it later. */
    memset(pathname, 0, MAX_FILENAME_LENGTH * sizeof(char));
    path_get_dirname(pathname, cuefile);

    /* Open the file. */
    FILE          *fp = plat_fopen(cuefile, "r");
    if (fp == NULL)
        return 0;

    int            success = 0;

    /*
       Pass 1 - loading the Cue sheet.
     */
    image_log(img->log, "Pass 1 (loading the Cue sheet)...\n");
    img->tracks_num = 0;

    for (int i = 0; i < 3; i++) {
        lead[i] = img->tracks_num;
        (void) image_insert_track(img, session, 0xa0 + i);
    }

    while (1) {
        line = buf;

        /* Read a line from the cuesheet file. */
        if (feof(fp) || (fgets(buf, sizeof(buf), fp) == NULL) || ferror(fp))
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
        image_log(img->log, "    [LINE    ] \"%s\"\n", line);

        (void) image_cue_get_keyword(&command, &line);

        if (!strcmp(command, "FILE")) {
            /* The file for the track. */
            char filename[MAX_FILENAME_LENGTH];
            char ansi[MAX_FILENAME_LENGTH];

            tf = NULL;

            memset(ansi, 0, MAX_FILENAME_LENGTH * sizeof(char));
            memset(filename, 0, MAX_FILENAME_LENGTH * sizeof(char));

            success = image_cue_get_buffer(ansi, &line, 0);
            if (!success)
                break;
            success = image_cue_get_keyword(&type, &line);
            if (!success)
                break;

            error    = 1;
            is_viso  = 0;

            if (!strcmp(type, "BINARY") || !strcmp(type, "MOTOROLA")) {
                if (!path_abs(ansi))
                    path_append_filename(filename, pathname, ansi);
                else
                    strcpy(filename, ansi);

                tf = index_file_init(img->dev->id, filename, &error, &is_viso);
                
                if (tf)
                    tf->motorola = !strcmp(type, "MOTOROLA");
            } else if (!strcmp(type, "WAVE") || !strcmp(type, "AIFF") ||
                       !strcmp(type, "MP3")) {
                if (!path_abs(ansi))
                    path_append_filename(filename, pathname, ansi);
                else
                    strcpy(filename, ansi);
                tf = audio_init(img->dev->id, filename, &error);
            }
            if (error) {
                if (tf != NULL) {
                    tf->close(tf);
                    tf = NULL;
                }
                success = 0;
            } else if (is_viso)
                success = 3;

#ifdef ENABLE_IMAGE_LOG
            if (!success)
                image_log(img->log, "    [FILE    ] Unable to open file \"%s\" "
                          "specified in cue sheet\n", filename);
#endif
        } else if (!strcmp(command, "TRACK")) {
            int t            = image_cue_get_number(&line);
            success          = image_cue_get_keyword(&type, &line);

            if (!success)
                break;

            if (last_t != -1) {
                /*
                   Important: This has to be done like this because pointers
                            change due to realloc.
                 */
                ct = &(img->tracks[t]);

                for (int i = 2; i >= 0; i--) {
                    if (ct->idx[i].file == NULL)
                        ct->idx[i].file = tf;
                    else
                        break;
                }
            }

            last_t           = t;
            ct               = image_insert_track(img, session, t);

            ct->form         = 0;
            ct->mode         = 0;

            if (!strcmp(type, "AUDIO")) {
                ct->sector_size = RAW_SECTOR_SIZE;
                ct->attr        = AUDIO_TRACK;
            } else if (!memcmp(type, "MODE", 4)) {
                uint32_t mode;
                ct->attr        = DATA_TRACK;
                sscanf(type, "MODE%" PRIu32 "/%" PRIu32,
                       &mode, &(ct->sector_size));
                ct->mode = mode;
                if (ct->mode == 2)  switch(ct->sector_size) {
                    default:
                        break;
                    case 2324: case 2328:
                        ct->form = 2;
                        break;
                    case 2048: case 2332: case 2336: case 2352: case 2368: case 2448:
                        ct->form = 1;
                        break;
                }
                if (((ct->sector_size == 2336) || (ct->sector_size == 2332)) && (ct->mode == 2) && (ct->form == 1))
                    ct->skip        = 8;
            } else if (!memcmp(type, "CD", 2)) {
                ct->attr        = DATA_TRACK;
                ct->mode        = 2;
                sscanf(type, "CD%c/%i", &temp, &(ct->sector_size));
            } else
                success = 0;

            if (success) {
                image_set_track_subch_type(ct);

                last = ct->sector_size;

                image_log(img->log, "    [TRACK   ] %02X/%02X, ATTR %02X, MODE %02X/%02X,\n",
                          ct->session,
                          ct->point,
                          ct->attr,
                          ct->mode, ct->form);
                image_log(img->log, "               %i\n",
                          ct->sector_size);
            }
#ifdef ENABLE_IMAGE_LOG
            else
                image_log(img->log, "    [TRACK   ] Unable to initialize track %02X "
                          "specified in Cue sheet\n", t);
#endif
        } else if (!strcmp(command, "INDEX")) {
            int t            = image_cue_get_number(&line);
            ci               = &(ct->idx[t]);

            ci->type         = INDEX_NORMAL;
            ci->file         = tf;
            success          = image_cue_get_frame(&frame, &line);
            ci->file_start   = frame;

            image_log(img->log, "    [INDEX   ] %02X (%8s): Initialization %s\n",
                      t, cit[ci->type + 2], success ? "successful" : "failed");
        } else if (!strcmp(command, "PREGAP")) {
            ci               = &(ct->idx[0]);

            ci->type         = INDEX_ZERO;
            ci->file         = tf;
            success          = image_cue_get_frame(&frame, &line);
            ci->length       = frame;

            image_log(img->log, "    [INDEX   ] 00 (%8s): Initialization %s\n",
                      cit[ci->type + 2], success ? "successful" : "failed");
        } else if (!strcmp(command, "PAUSE") || !strcmp(command, "ZERO")) {
            ci               = &(ct->idx[1]);

            ci->type         = INDEX_ZERO;
            ci->file         = tf;
            success          = image_cue_get_frame(&frame, &line);
            ci->length       = frame;

            image_log(img->log, "    [INDEX   ] 01 (%8s): Initialization %s\n",
                      cit[ci->type + 2], success ? "successful" : "failed");
        } else if (!strcmp(command, "POSTGAP")) {
            ci               = &(ct->idx[2]);

            ci->type         = INDEX_ZERO;
            ci->file         = tf;
            success          = image_cue_get_frame(&frame, &line);
            ci->length       = frame;

            image_log(img->log, "    [INDEX   ] 02 (%8s): Initialization %s\n",
                      cit[ci->type + 2], success ? "successful" : "failed");
        } else if (!strcmp(command, "FLAGS")) {
            success = image_cue_get_flags(ct, &line);

            image_log(img->log, "    [FLAGS   ] Initialization %s\n",
                      success ? "successful" : "failed");
        } else if (!strcmp(command, "REM")) {
            success = 1;
            char *space = strstr(line, " ");
            if (space != NULL) {
                space++;
                if (space < (line + strlen(line))) {
                    (void) image_cue_get_keyword(&command, &space);
                    if (!strcmp(command, "LEAD-OUT")) {
                        ct                   = &(img->tracks[lead[2]]);
                        /*
                           Mark it this way so file pointers on it are not
                           going to be adjusted.
                         */
                        last_t               = -1;
                        ct->sector_size      = last;
                        ci                   = &(ct->idx[1]);
                        ci->type             = INDEX_NORMAL;
                        ci->file             = tf;
                        success              = image_cue_get_frame(&frame, &space);
                        ci->file_start       = frame;

                        image_log(img->log, "    [LEAD-OUT] Initialization %s\n",
                                  success ? "successful" : "failed");
                    } else if (!strcmp(command, "SESSION")) {
                        session              = image_cue_get_number(&space);

                        if (session > 1) {
                            ct = image_insert_track(img, session - 1, 0xb0);
                            /*
                               Mark it this way so file pointers on it are not
                               going to be adjusted.
                             */
                            last_t               = -1;
                            ci                   = &(ct->idx[1]);
                            ci->start            = (0x40 * 60 * 75) + (0x02 * 75);

                            if (session == 2) {
                                ct->extra[3]         = 0x02;

                                /*
                                   00:00:00 on Wembley, C0:00:00 in the spec.
                                   And what's in PMSF?
                                 */
                                ct = image_insert_track(img, session - 1, 0xc0);
                                /*
                                   Mark it this way so file pointers on it are not
                                   going to be adjusted.
                                 */
                                last_t               = -1;
                                ci                   = &(ct->idx[1]);
                                /* Queen - Live at Wembley '86 CD 1. */
                                ci->start            = (0x5f * 75 * 60);
                                /* Optimum recording power. */
                                ct->extra[0]         = 0x00;
                            } else
                                ct->extra[3]         = 0x01;

                            for (int i = 0; i < 3; i++) {
                                lead[i] = img->tracks_num;
                                (void) image_insert_track(img, session, 0xa0 + i);
                            }
                        }

                        image_log(img->log, "    [SESSION ] Initialization successful\n");
                    }
                }
            }
        } else if (!strcmp(command, "CATALOG") || !strcmp(command, "CDTEXTFILE") ||
                   !strcmp(command, "ISRC") || !strcmp(command, "PERFORMER") ||
                   !strcmp(command, "SONGWRITER") || !strcmp(command, "TITLE") ||
                   !strcmp(command, "")) {
            /* Ignored commands. */
            image_log(img->log, "    [CUE   ] Ignored command \"%s\" in Cue sheet\n",
                      command);
            success = 1;
        } else {
            image_log(img->log, "    [CUE   ] Unsupported command \"%s\" in Cue sheet\n",
                      command);
            success = 0;
        }

        if (!success)
            break;
    }

    if (success && (ct != NULL))  for (int i = 2; i >= 0; i--) {
        if (ct->idx[i].file == NULL)
            ct->idx[i].file = tf;
        else
            break;
    }

    tf = NULL;

    fclose(fp);

    if (success)
        image_process(img);
    else {
        image_log(img->log, "    [CUE   ] Unable to open Cue sheet \"%s\"\n", cuefile);
        return 0;
    }

    return success;
}

/* Root functions. */
static void
image_clear_tracks(cd_image_t *img)
{
    const track_file_t *last = NULL;
    track_t            *cur  = NULL;
    track_index_t      *idx  = NULL;

    if ((img->tracks != NULL) && (img->tracks_num > 0)) {
        for (int i = 0; i < img->tracks_num; i++) {
            cur = &img->tracks[i];

            if (((cur->point >= 1) && (cur->point <= 99)) ||
                (cur->point == 0xa2))  for (int j = 0; j < 3; j++) {
                    idx = &(cur->idx[j]);
                    /* Make sure we do not attempt to close a NULL file. */
                    if ((idx->file != NULL) && (idx->type == INDEX_NORMAL)) {
                        if (idx->file != last) {
                            last = idx->file;
                            index_file_close(idx);
                        } else
                            idx->file = NULL;
                    }
                }
        }

        /* Now free the array. */
        free(img->tracks);
        img->tracks = NULL;

        /* Mark that there's no tracks. */
        img->tracks_num = 0;
    }
}

/* Shared functions. */
static int
image_get_track_info(const void *local, const uint32_t track,
                     const int end, track_info_t *ti)
{
    const cd_image_t *img = (const cd_image_t *) local;
    const track_t    *ct  = NULL;
    int               ret = 0;

    for (int i = 0; i < img->tracks_num; i++) {
         ct = &(img->tracks[i]);
         if (ct->point == track)
             break;
    }

    if (ct != NULL) {
        const uint32_t pos = end ? ct->idx[1].start :
                                   (ct->idx[1].start + ct->idx[1].length);

        ti->number = ct->point;
        ti->attr   = ct->attr;
        ti->m      = (pos / 75) / 60;
        ti->s      = (pos / 75) % 60;
        ti->f      = pos % 75;

        ret        = 1;
    }

    return ret;
}

static void
image_get_raw_track_info(const void *local, int *num, uint8_t *buffer)
{
    const cd_image_t *img = (const cd_image_t *) local;
    int               len = 0;

    image_log(img->log, "img->tracks_num = %i\n", img->tracks_num);

    for (int i = 0; i < img->tracks_num; i++) {
        const track_t *ct = &(img->tracks[i]);
#ifdef ENABLE_IMAGE_LOG
        int old_len = len;
#endif
        buffer[len++] = ct->session;                  /* Session number. */
        buffer[len++] = ct->attr;                     /* Track ADR and Control. */
        buffer[len++] = ct->tno;                      /* TNO (always 0). */
        buffer[len++] = ct->point;                    /* Point (track number). */
        for (int j = 0; j < 4; j++)
            buffer[len++] = ct->extra[j];
        buffer[len++] = (ct->idx[1].start / 75) / 60;
        buffer[len++] = (ct->idx[1].start / 75) % 60;
        buffer[len++] = ct->idx[1].start % 75;
        image_log(img->log, "%i: %02X %02X %02X %02X %02X %02X %02X\n", i,
                  buffer[old_len], buffer[old_len + 1],
                  buffer[old_len + 2], buffer[old_len + 3],
                  buffer[old_len + 8], buffer[old_len + 9],
                  buffer[old_len + 10]);
    }

    *num = img->tracks_num;
}

static int
image_is_track_pre(const void *local, const uint32_t sector)
{
    const cd_image_t *img   = (const cd_image_t *) local;
    int               ret   = 0;

    if (img->has_audio) {
        const int track = image_get_track(img, sector);

        if (track >= 0) {
            const track_t *trk = &(img->tracks[track]);

            ret = !!(trk->attr & 0x01);
        }
    }

    return ret;
}

static int
image_read_sector(const void *local, uint8_t *buffer,
                  const uint32_t sector)
{
    const cd_image_t *img    = (const cd_image_t *) local;
    int               m      = 0;
    int               s      = 0;
    int               f      = 0;
    int               ret    = 0;
    uint32_t          lba    = sector;
    int               track;
    int               index;
    uint8_t           q[16]  = { 0x00 };

    if (sector == 0xffffffff)
        lba = img->dev->seek_pos;

    const uint64_t sect   = (uint64_t) lba;

    image_get_track_and_index(img, lba, &track, &index);

    const track_t       *trk          = &(img->tracks[track]);
    const track_index_t *idx          = &(trk->idx[index]);
    const int            track_is_raw = ((trk->sector_size == RAW_SECTOR_SIZE) ||
                                         (trk->sector_size == 2448));
    const uint64_t       seek         = ((sect + 150 - idx->start + idx->file_start) *
                                         trk->sector_size) + trk->skip;

    if (track >= 0) {
        /* Signal CIRC error to the guest if sector is bad. */
        ret = image_is_sector_bad(img, lba) ? -1 : 1;

        if (ret > 0) {
            uint64_t       offset = 0ULL;

            image_log(img->log, "cdrom_read_sector(%08X): track %02X, index %02X, %016"
                      PRIX64 ", %i, %i, %i, %i\n",
                      lba, track, index, idx->start, trk->sector_size, track_is_raw,
                      trk->mode, trk->form);

            memset(buffer, 0x00, 2448);

            if ((trk->attr & 0x04) && ((idx->type < INDEX_NORMAL) || !track_is_raw)) {
                offset += 16ULL;

                /* Construct the header. */
                memset(buffer + 1, 0xff, 10);
                buffer += 12;
                FRAMES_TO_MSF(sector + 150, &m, &s, &f);
                /* These have to be BCD. */
                buffer[0] = bin2bcd(m & 0xff);
                buffer[1] = bin2bcd(s & 0xff);
                buffer[2] = bin2bcd(f & 0xff);
                /* Data, should reflect the actual sector type. */
                buffer[3] = trk->mode;
                buffer += 4;
                if (trk->form >= 1) {
                    offset += 8ULL;

                    /* Construct the CD-I/XA sub-header. */
                    buffer[2] = buffer[6] = (trk->form - 1) << 5;
                    buffer += 8;
                }
            }

            if (idx->type >= INDEX_NORMAL) {
                /* Read the data from the file. */
                ret = idx->file->read(idx->file, buffer, seek, trk->sector_size);
            } else
                /* Index is not in the file, no read to fail here. */
                ret = 1;

            if ((ret > 0) && ((idx->type < INDEX_NORMAL) || (trk->subch_type != 0x08))) {
                buffer -= offset;

                if (trk->subch_type == 0x10)
                    memcpy(q, &(buffer[2352]), 12);
                else {
                    /* Construct Q. */
                    q[0] = (trk->attr >> 4) | ((trk->attr & 0xf) << 4);
                    q[1] = bin2bcd(trk->point);
                    q[2] = index;
                    if (index == 0) {
                        /*
                           Pre-gap sector relative frame addresses count from
                           00:01:74 downwards.
                         */
                        FRAMES_TO_MSF((int32_t) (149 - (lba + 150 - idx->start)), &m, &s, &f);
                    } else {
                        FRAMES_TO_MSF((int32_t) (lba + 150 - idx->start), &m, &s, &f);
                    }
                    q[3] = bin2bcd(m & 0xff);
                    q[4] = bin2bcd(s & 0xff);
                    q[5] = bin2bcd(f & 0xff);
                    FRAMES_TO_MSF(lba + 150, &m, &s, &f);
                    q[7] = bin2bcd(m & 0xff);
                    q[8] = bin2bcd(s & 0xff);
                    q[9] = bin2bcd(f & 0xff);
                }

                /* Construct raw subchannel data from Q only. */
                for (int i = 0; i < 12; i++)
                     for (int j = 0; j < 8; j++)
                          buffer[2352 + (i << 3) + j] = ((q[i] >> (7 - j)) & 0x01) << 6;
            }
        }
    }

    return ret;
}

static uint8_t
image_get_track_type(const void *local, const uint32_t sector)
{
    const cd_image_t *img   = (cd_image_t *) local;
    const int         track = image_get_track(img, sector);
    const track_t *   trk   = &(img->tracks[track]);
    int               ret   = 0x00;

    if (image_is_track_audio(img, sector))
        ret = CD_TRACK_AUDIO;
    else if (track >= 0)  for (int i = 0; i < img->tracks_num; i++) {
        const track_t *ct = &(img->tracks[i]);
        const track_t *nt = &(img->tracks[i + 1]);

        if (ct->point == 0xa0) {
            const uint8_t first = (ct->idx[1].start / 75 / 60);
            const uint8_t last  = (nt->idx[1].start / 75 / 60);

            if ((trk->point >= first) && (trk->point <= last)) {
                ret = (ct->idx[1].start / 75) % 60;
                break;
            }
        }
    }

    return ret;
}

static uint32_t
image_get_last_block(const void *local)
{
    const cd_image_t *img = (const cd_image_t *) local;
    uint32_t          lb  = 0x00000000;

    if (img != NULL) {
        const track_t    *lo  = NULL;

        for (int i = (img->tracks_num - 1); i >= 0; i--) {
            if (img->tracks[i].point == 0xa2) {
                lo = &(img->tracks[i]);
                break;
            }
        }

        if (lo != NULL)
            lb = lo->idx[1].start - 1;
    }

    return lb;
}

static int
image_read_dvd_structure(const void *local, const uint8_t layer, const uint8_t format,
                         uint8_t *buffer, uint32_t *info)
{
    return 0;
}

static int
image_is_dvd(const void *local)
{
    const cd_image_t *img = (const cd_image_t *) local;

    return img->is_dvd;
}

static int
image_has_audio(const void *local)
{
    const cd_image_t *img = (const cd_image_t *) local;

    return img->has_audio;
}

static void
image_close(void *local)
{
    cd_image_t *img = (cd_image_t *) local;

    if (img != NULL) {
        image_clear_tracks(img);

        image_log(img->log, "Log closed\n");

        log_close(img->log);
        img->log = NULL;

        free(img);
    }
}

static const cdrom_ops_t image_ops = {
    image_get_track_info,
    image_get_raw_track_info,
    image_is_track_pre,
    image_read_sector,
    image_get_track_type,
    image_get_last_block,
    image_read_dvd_structure,
    image_is_dvd,
    image_has_audio,
    NULL,
    image_close,
    NULL
};

/* Public functions. */
void *
image_open(cdrom_t *dev, const char *path)
{
    const uintptr_t  ext = path + strlen(path) - strrchr(path, '.');
    cd_image_t      *img = (cd_image_t *) calloc(1, sizeof(cd_image_t));

    if (img != NULL) {
        int       ret;
        const int is_cue = ((ext == 4) && !stricmp(path + strlen(path) - ext + 1, "CUE"));

        img->dev = dev;

        if (is_cue) {
            ret = image_load_cue(img, path);

            if (ret >= 2)
                img->has_audio = 0;
            else if (ret)
                img->has_audio = 1;
        } else {
            ret = image_load_iso(img, path);

            if (!ret) {
                image_close(img);
                img = NULL;
            } else
                img->has_audio = 0;
        }

        if (ret) {
            char n[1024]        = { 0 };

            sprintf(n, "CD-ROM %i Image", dev->id + 1);
            img->log          = log_open(n);

            dev->ops = &image_ops;
        }
    }

    return img;
}
