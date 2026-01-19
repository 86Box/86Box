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

#include <sndfile.h>

#ifdef ENABLE_IMAGE_LOG
#define LOG_VAR(a) size_t a =
#else
#define LOG_VAR(a)
#endif

#define NO_CHIPHER_IDS_ENUM
#include "../utils/mds.h"

#define MAX_LINE_LENGTH     512
#define MAX_FILENAME_LENGTH 256
#define CROSS_LEN           512

static char temp_keyword[1024];
static char temp_file[260]     = { 0 };

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
    uint8_t       max_index;
    uint32_t      sector_size;
    track_index_t idx[100];
} track_t;

/*
   MDS for DVD has the disc structure table - 4 byte pointer to BCA,
   followed by the copyright, DMI, and layer pages.
*/
#pragma pack(push, 1)
typedef struct
{
    uint8_t  f1[4];
    uint8_t  f4[2048];
    uint8_t  f0[2048];
} layer_t;

typedef struct
{
    layer_t  layers[2];
} mds_disc_struct_t;
#pragma pack(pop)

#define dstruct_t mds_disc_struct_t

typedef struct cd_image_t {
    cdrom_t      *dev;
    void         *log;
    int           is_dvd;
    int           has_audio;
    int           has_dstruct;
    int32_t       tracks_num;
    uint32_t      bad_sectors_num;
    track_t      *tracks;
    uint32_t     *bad_sectors;
    dstruct_t     dstruct;
} cd_image_t;

typedef enum
{
    CD          = 0x00,    /* CD-ROM */
    CD_R        = 0x01,    /* CD-R */
    CD_RW       = 0x02,    /* CD-RW */
    DVD         = 0x10,    /* DVD-ROM */
    DVD_MINUS_R = 0x12     /* DVD-R */
} mds_medium_type_t;

typedef enum
{
    UNKNOWN     = 0x00,
    AUDIO       = 0xa9,    /* sector size = 2352 */
    MODE1       = 0xaa,    /* sector size = 2048 */
    MODE2       = 0xab,    /* sector size = 2336 */
    MODE2_FORM1 = 0xac,    /* sector size = 2048 */
    MODE2_FORM2 = 0xad     /* sector size = 2324 (+4) */
} mds_trk_mode_t;

#pragma pack(push, 1)
typedef struct
{
    uint8_t  file_sig[16];
    uint8_t  file_ver[2];
    uint16_t medium_type;
    uint16_t sess_num;
    uint16_t pad[2];
    uint16_t bca_data_len;
    uint32_t pad0[2];
    uint32_t bca_data_offs_offs;
    uint32_t pad1[6];
    uint32_t disc_struct_offs;
    uint32_t pad2[3];
    uint32_t sess_blocks_offs;
    uint32_t dpm_blocks_offs;
} mds_hdr_t;    /* 88 bytes */

typedef struct
{
    int32_t  sess_start;
    int32_t  sess_end;
    uint16_t sess_id;
    uint8_t  all_blocks_num;
    uint8_t  non_track_blocks_num;
    uint16_t first_trk;
    uint16_t last_trk;
    uint32_t pad;
    uint32_t trk_blocks_offs;
} mds_sess_block_t;    /* 24 bytes */

/* MDF v2.01 session block. */
typedef struct
{
    int64_t  sess_start;
    uint16_t sess_id;
    uint8_t  all_blocks_num;
    uint8_t  non_track_blocks_num;
    uint16_t first_trk;
    uint16_t last_trk;
    uint32_t pad;
    uint32_t trk_blocks_offs;
    int64_t  sess_end;
} mds_v2_sess_block_t;    /* 24 bytes */

typedef struct
{
    uint8_t  trk_mode;
    /* DiscImageCreator says this is the number of subchannels. */
    uint8_t  subch_mode;
    uint8_t  adr_ctl;
    uint8_t  track_id;
    uint8_t  point;
    uint8_t  m;
    uint8_t  s;
    uint8_t  f;
    uint8_t  zero;
    uint8_t  pm;
    uint8_t  ps;
    uint8_t  pf;
    /* DiscImageCreator calls this the index offset. */
    uint32_t ex_offs;
    uint16_t sector_len;
    /* DiscImageCreator says unknown1 followed by 17x zero. */
    uint8_t  pad0[18];
    uint32_t start_sect;
    uint64_t start_offs;
    uint32_t files_num;
    uint32_t footer_offs;
    union {
        uint8_t  pad1[24];
        struct {
            uint64_t start_sect_v2;
            uint8_t  pad2[16];
        };
    };        
} mds_trk_block_t;    /* 80 bytes */

/*
   DiscImageCreator's interpretation here makes sense and essentially
   matches libmirage's - Index 0 sectors followed by Index 1 sectors.
 */
typedef struct
{
    uint32_t pregap;
    uint32_t trk_sectors;
} mds_trk_ex_block_t;    /* 8 bytes */

typedef struct
{
    uint32_t fn_offs;
    uint32_t fn_is_wide;
    uint32_t pad;
    uint32_t pad0;
} mds_footer_t;    /* 16 bytes */

/* MDF v2.01 track footer block. */
typedef struct
{
    uint32_t fn_offs;
    uint32_t pad;     /* Always wide */
    uint32_t pad0;
    uint32_t pad1;
    uint64_t trk_sectors;
    uint64_t pad2;
} mds_v2_footer_t;    /* 16 bytes */

typedef struct
{
    uint32_t type;
    uint32_t pad[2];
    uint32_t entries;
} mds_dpm_block_t;
#pragma pack(pop)

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

    log_close(tf->log);
    tf->log = NULL;

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

    char n[1024]        = { 0 };

    sprintf(n, "CD-ROM %i Bin  ", id + 1);
    tf->log          = log_open(n);

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
    } else {
        /* From the check above, error may still be non-zero if opening a directory.
         * The error is set for viso to try and open the directory following this function.
         * However, we need to make sure the descriptor is closed. */
        if ((tf->fp != NULL) && ((stats.st_mode & S_IFMT) == S_IFDIR)) {
            /* tf is freed by bin_close */
            bin_close(tf);
        } else {
            log_close(tf->log);
            tf->log = NULL;

            free(tf);
        }
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
        for (int j = 0; j <= ct->max_index; j++) {
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
        if ((ct->point >= 1) && (ct->point <= 99))  for (int j = 0; j <= ct->max_index; j++) {
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

    ct->max_index = 2;

    ct->session   = session;
    ct->point     = point;

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
            for (int j = ct->max_index; j >= 0; j--) {
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

                if ((ci->type == INDEX_NORMAL) && (((int64_t) ci->file_start) < 0LL)) {
                    ci->type        = INDEX_ZERO;
                    ci->length      = 150;
                    ci->file_start  = 0ULL;
                    ci->file_length = 0ULL;
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

            for (int j = 0; j <= ct->max_index; j++) {
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

                    for (int j = 0; j <= ct->max_index; j++) {
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

                    for (int j = 0; j <= ct->max_index; j++) {
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
                        const track_index_t *li = &(lt->idx[lt->max_index]);

                        for (int j = 0; j <= ct->max_index; j++) {
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
        for (int j = 0; j <= ct->max_index; j++) {
            ci = &(ct->idx[j]);
            image_log(img->log, "    [TRACK   ] %02X INDEX %02X: [%8s, %016" PRIX64 "]\n",
                      ct->point, j,
                      cit[ci->type + 2], ci->file_start * ct->sector_size);
            image_log(img->log, "               TOC data: %02X %02X %02X "
                      "%02X %02X %02X %02X %02X %02X %02X %02X\n",
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
    track_t       *ct              = NULL;
    track_index_t *ci              = NULL;
    track_file_t  *tf              = NULL;
    int            success         = 1;
    int            error           = 1;
    int            is_viso         = 0;
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
#ifdef ENABLE_IMAGE_LOG
        log_warning(img->log, "Unable to open image or folder \"%s\"\n",
                    filename);
#else
        warning("Unable to open image or folder \"%s\"\n", filename);
#endif
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
    int            lo_cmd                        = 0;
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
                ct = &(img->tracks[img->tracks_num - 1]);

                for (int i = ct->max_index; i >= 0; i--) {
                    if (ct->idx[i].file == NULL)
                        ct->idx[i].file = tf;
                    else
                        break;
                }
            } else if ((t == 0) && (line[strlen(line) - 2] == ' ') &&
                       (line[strlen(line) - 1] == '0'))
                t = 1;

            last_t           = t;
            ct               = image_insert_track(img, session, t);

            for (int i = 2; i >= 0; i--)
                ct->idx[i].type = INDEX_NONE;

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

            if (t > ct->max_index)
                ct->max_index    = t;

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
                        lo_cmd               = 1;
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
                            if (!lo_cmd) {
                                ct                   = &(img->tracks[lead[2]]);
                                /*
                                   Mark it this way so file pointers on it are not
                                   going to be adjusted.
                                 */
                                last_t               = -1;
                                ct->sector_size      = last;
                                ci                   = &(ct->idx[1]);
                                ci->type             = INDEX_ZERO;
                                ci->file             = tf;
                                ci->file_start       = 0;
                                ci->file_length      = 0;
                                ci->length           = (2 * 60 * 75) + (30 * 75);

                                image_log(img->log, "    [LEAD-OUT] Initialization successful\n");
                            }

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

                        lo_cmd               = 0;

                        image_log(img->log, "    [SESSION ] Initialization successful\n");
                    } else if (!strcmp(command, "TAOGAP")) {
                        ci               = &(ct->idx[2]);

                        ci->type         = INDEX_ZERO;
                        ci->file         = tf;
                        success          = image_cue_get_frame(&frame, &line);
                        ci->length        = frame;

                        image_log(img->log, "    [INDEX   ] 02 (%8s): Initialization %s\n",
                                  cit[ci->type + 2], success ? "successful" : "failed");
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

    if (success && (ct != NULL))  for (int i = ct->max_index; i >= 0; i--) {
        if (ct->idx[i].file == NULL)
            ct->idx[i].file = tf;
        else
            break;
    }

    tf = NULL;

    fclose(fp);

    if (success)
        image_process(img);
    else
#ifdef ENABLE_IMAGE_LOG
        log_warning(img->log, "    [CUE   ] Unable to open Cue sheet \"%s\"\n", cuefile);
#else
        warning("Unable to open Cue sheet \"%s\"\n", cuefile);
#endif

    return success;
}

// Converts UTF-16 string into UTF-8 string.
// If destination string is NULL returns total number of symbols that would've
// been written (without null terminator). However, when actually writing into
// destination string, it does include it. So, be sure to allocate extra byte
// for destination string.
// Params:
// u16_str      - source UTF-16 string
// u16_str_len  - length of source UTF-16 string
// u8_str       - destination UTF-8 string
// u8_str_size  - size of destination UTF-8 string in bytes
// Return value:
// 0 on success, -1 if encountered invalid surrogate pair, -2 if
// encountered buffer overflow or length of destination UTF-8 string in bytes
// (without including the null terminator).
long int utf16_to_utf8(const uint16_t *u16_str, size_t u16_str_len,
                       uint8_t *u8_str, size_t u8_str_size)
{
    size_t i = 0, j = 0;

    if (!u8_str) {
        u8_str_size = u16_str_len * 4;
    }

    while (i < u16_str_len) {
        uint32_t codepoint = u16_str[i++];

        // check for surrogate pair
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            uint16_t high_surr = codepoint;
            uint16_t low_surr  = u16_str[i++];

            if (low_surr < 0xDC00 || low_surr > 0xDFFF)
                return -1;

            codepoint = ((high_surr - 0xD800) << 10) +
                        (low_surr - 0xDC00) + 0x10000;
        }

        if (codepoint < 0x80) {
            if (j + 1 > u8_str_size) return -2;

            if (u8_str) u8_str[j] = (char)codepoint;

            j++;
        } else if (codepoint < 0x800) {
            if (j + 2 > u8_str_size) return -2;

            if (u8_str) {
                u8_str[j + 0] = 0xC0 | (codepoint >> 6);
                u8_str[j + 1] = 0x80 | (codepoint & 0x3F);
            }

            j += 2;
        } else if (codepoint < 0x10000) {
            if (j + 3 > u8_str_size) return -2;

            if (u8_str) {
                u8_str[j + 0] = 0xE0 | (codepoint >> 12);
                u8_str[j + 1] = 0x80 | ((codepoint >> 6) & 0x3F);
                u8_str[j + 2] = 0x80 | (codepoint & 0x3F);
            }

            j += 3;
        } else {
            if (j + 4 > u8_str_size) return -2;

            if (u8_str) {
                u8_str[j + 0] = 0xF0 | (codepoint >> 18);
                u8_str[j + 1] = 0x80 | ((codepoint >> 12) & 0x3F);
                u8_str[j + 2] = 0x80 | ((codepoint >> 6) & 0x3F);
                u8_str[j + 3] = 0x80 | (codepoint & 0x3F);
            }

            j += 4;
        }
    }

    if (u8_str) {
        if (j >= u8_str_size) return -2;
        u8_str[j] = '\0';
    }

    return (long int)j;
}

int
mds_decrypt_track_data(cd_image_t *img, const char *mdsfile, FILE **fp)
{
    int      is_mdx     = 0;

    uint64_t mdx_offset = 0ULL;
    uint64_t mdx_size_1 = 0ULL;

    if (*fp == NULL) {
#ifdef ENABLE_IMAGE_LOG
        log_warning(img->log, "    [MDS   ] \"%s\" is not open\n",
                    mdsfile);
#else
        warning("\"%s\" is not open\n", mdsfile);
#endif
        return 0;
    }

    image_log(img->log, "mds_decrypt_track_data(): Decrypting MDS...\n");
    /*
       If we are here, them we have already determined in
       image_load_mds() that the version is 2.x.
     */
    fseek(*fp, 0x2c, SEEK_SET);

    uint64_t offset = 0ULL;
    fread(&offset, 1, 4, *fp);
    image_log(img->log, "mds_decrypt_track_data(): Offset is %016" PRIX64 "\n", offset);

    if (offset == 0xffffffff) {
        image_log(img->log, "mds_decrypt_track_data(): File is MDX\n");
        is_mdx = 1;

        fread(&mdx_offset, 1, 8, *fp);
        fread(&mdx_size_1, 1, 8, *fp);
        image_log(img->log, "mds_decrypt_track_data(): MDX footer is %" PRIi64 " bytes at offset %016" PRIX64 "\n", mdx_size_1, mdx_offset);

        offset = mdx_offset + (mdx_size_1 - 0x40);
        image_log(img->log, "mds_decrypt_track_data(): MDX offset is %016" PRIX64 "\n", offset);
    }

    fseek(*fp, offset, SEEK_SET);

    uint8_t data1[0x200];

    fread(data1, 0x200, 1, *fp);
    image_log(img->log, "mds_decrypt_track_data(): Read the first data buffer\n");

    PCRYPTO_INFO ci;
    decode1(data1, NULL, &ci);
    image_log(img->log, "data1: %02X %02X %02X %02X\n", data1[0], data1[1], data1[2], data1[3]);
    FILE *d1f = fopen("data1.tmp", "wb");
    fwrite(data1, 1, 0x200, d1f);
    fclose(d1f);
    image_log(img->log, "mds_decrypt_track_data(): Decoded the first data buffer\n");

    /* Compressed size at 0x150? */
    uint32_t decSize = getU32(data1 + 0x154);    /* Decompressed size? */
    image_log(img->log, "mds_decrypt_track_data(): Decompressed size is %i bytes\n", decSize);

    uint64_t data2Offset = 0x30;                         /* For MDS v2. */
    uint64_t data2Size = offset - 0x30;                  /* For MDS v2. */

    if (is_mdx) {
        data2Offset = mdx_offset;
        data2Size = mdx_size_1 - 0x40;
    }
    image_log(img->log, "mds_decrypt_track_data(): Second data buffer is %" PRIi64 " bytes at offset %016" PRIX64 "\n", data2Size, data2Offset);

    fseek(*fp, data2Offset, SEEK_SET);

    u8 *data2 = (u8 *)malloc(data2Size);
    fread(data2, 1, data2Size, *fp);
    image_log(img->log, "mds_decrypt_track_data(): Read the second data buffer\n");

    DecryptBlock(data2, data2Size, 0, 0, 4, ci);
    image_log(img->log, "mds_decrypt_track_data(): Decoded the second data buffer\n");

    u8 *mdxHeader = (u8 *)malloc(decSize + 0x12);

    z_stream infstream;
    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;
    infstream.avail_in = data2Size;
    infstream.next_in = data2;
    infstream.avail_out = decSize;
    infstream.next_out = mdxHeader + 0x12;

    inflateInit(&infstream);

    inflate(&infstream, Z_NO_FLUSH);
    inflateEnd(&infstream);

    fseek(*fp, 0, SEEK_SET);
    fread(mdxHeader, 1, 0x12, *fp);

    u8 medium_type = getU8(mdxHeader + offsetof(MDX_Header, medium_type));
    int isDVD = 1;

    if (medium_type < 3) // 0, 1, 2
        isDVD = 0;

    Decoder encryptInfo;
    encryptInfo.mode = -1;
    encryptInfo.ctr = 1;

    u32 keyBlockOff = getU32(mdxHeader + offsetof(MDX_Header, encryption_block_offset));

    if (keyBlockOff) {
        image_log(img->log, "Encryption detected\n");

        const char *password = NULL;

        image_log(img->log, "Trying without password\n");

        PCRYPTO_INFO ci2;
#ifdef ENABLE_IMAGE_LOG
        if (decode1(mdxHeader + keyBlockOff, password, &ci2) == 0) {
            if (password)
                image_log(img->log, "Password \"%s\": OK\n", password);
            else
                image_log(img->log, "It's encrypted with NULL password. OK!\n");
        } else {
            if (password)
                image_log(img->log, "Password \"%s\": WRONG\n", password);
            else
                image_log(img->log, "Please specify password. Seems it's necessery.\n");

            image_log(img->log, "But we save header_not_decrypted.out with encrypted key block\n");
#else
        if (decode1(mdxHeader + keyBlockOff, password, &ci2) != 0) {
#endif

#if 0
            FILE *b = fopen("header_not_decrypted.out", "wb");
            fwrite(mdxHeader, 1, decSize + 0x12, b);
            fclose(b);
#else
#ifdef ENABLE_IMAGE_LOG
            log_warning(img->log, "    [MDS   ] \"%s\" is an unsupported password-protected file\n",
                        mdsfile);
#else
            warning("\"%s\" is an unsupported password-protected file\n", mdsfile);
#endif
            fclose(*fp);
            *fp = NULL;
#endif

            return 0;
        }

        /*
           Seems it's always use one mode AES 256 with GF. */
        encryptInfo.bsize = 32;
        encryptInfo.mode = 2;

        u8 *keyblock = mdxHeader + keyBlockOff;
        memcpy(encryptInfo.dg, keyblock + 0x50, 0x20);
        Gf128Tab64Init(keyblock + 0x50, &encryptInfo.gf_ctx);
        aes_encrypt_key(keyblock + 0x70, encryptInfo.bsize, &encryptInfo.encr);
        aes_decrypt_key(keyblock + 0x70, encryptInfo.bsize, &encryptInfo.decr);
    } else
        image_log(img->log, "No encryption detected\n");

    fclose(*fp);
    *fp = NULL;

    /* Dump mdxHeader */
    plat_tempfile(temp_file, "mds_v2", ".tmp");
    image_log(img->log, "\nDumping header into %s... ", nvr_path(temp_file));

    *fp = plat_fopen64(nvr_path(temp_file), "wb");
    fwrite(mdxHeader, 1, decSize + 0x12, *fp);
    fclose(*fp);

    fclose(*fp);
    *fp = NULL;

    *fp = plat_fopen64(nvr_path(temp_file), "rb");

    image_log(img->log, "Done\n");
    return isDVD + 1;
}

static int
image_load_mds(cd_image_t *img, const char *mdsfile)
{
    track_t       *ct                            = NULL;
    track_index_t *ci                            = NULL;
    track_file_t  *tf                            = NULL;
    int            is_viso                       = 0;
    int            version                       = 1;
    int            last_t                        = -1;
    int            error;
    char           pathname[MAX_FILENAME_LENGTH];
    char           ofn[2048]                     = { 0 };

    mds_hdr_t             mds_hdr             = { 0 };
    mds_sess_block_t      mds_sess_block      = { 0 };
    mds_v2_sess_block_t   mds_v2_sess_block   = { 0 };
    mds_trk_block_t       mds_trk_block       = { 0 };
    mds_trk_ex_block_t    mds_trk_ex_block    = { 0 };
    mds_footer_t          mds_footer          = { 0 };
    mds_v2_footer_t       mds_v2_footer       = { 0 };
    mds_dpm_block_t       mds_dpm_block       = { 0 };
    uint32_t              mds_dpm_blocks_num  = 0x00000000;
    uint32_t              mds_dpm_block_offs  = 0x00000000;

    img->tracks     = NULL;
    img->tracks_num = 0;

    /* Get a copy of the filename into pathname, we need it later. */
    memset(pathname, 0, MAX_FILENAME_LENGTH * sizeof(char));
    path_get_dirname(pathname, mdsfile);

    /* Open the file. */
    FILE          *fp = plat_fopen(mdsfile, "rb");
    if (fp == NULL)
        return 0;

    int            success = 0;

    /*
       Pass 1 - loading the MDS sheet.
     */
    image_log(img->log, "Pass 1 (loading the Media Descriptor Sheet)...\n");
    img->tracks_num = 0;
    success = 2;

    fseek(fp, 0, SEEK_SET);
    if (fread(&mds_hdr, 1, sizeof(mds_hdr_t), fp) != sizeof(mds_hdr_t))
        return 0;

    if (memcmp(mds_hdr.file_sig, "MEDIA DESCRIPTOR", 16)) {
#ifdef ENABLE_IMAGE_LOG
        log_warning(img->log, "    [MDS   ] \"%s\" is not an actual MDF file\n",
                    mdsfile);
#else
        warning("\"%s\" is not an actual MDF file\n", mdsfile);
#endif
        fclose(fp);
        return 0;
    }

    if (mds_hdr.file_ver[0] == 0x02) {
        int mdsx = mdsx_init();
        if (!mdsx) {
#ifdef ENABLE_IMAGE_LOG
            log_warning(img->log, "    [MDS   ] Error initializing dynamic library %s\n", mdsfile);
#else
            warning("Error initializing dynamic library %s\n", mdsfile);
#endif
            if (fp != NULL)
                fclose(fp);
            return 0;
        }

        image_log(img->log, "Pass 1.5 (decrypting the Media Descriptor Sheet)...\n");

        fseek(fp, 0, SEEK_SET);
        int ret = mds_decrypt_track_data(img, mdsfile, &fp);

        mdsx_close();

        if (ret == 0) {
#ifdef ENABLE_IMAGE_LOG
            log_warning(img->log, "    [MDS   ] Error decrypting \"%s\"\n",
                        mdsfile);
#else
            warning("Error decrypting \"%s\"\n", mdsfile);
#endif
            if (fp != NULL)
                fclose(fp);
            return 0;
        } else {
            img->is_dvd = ret - 1;
            version     = 2;

            fseek(fp, 0, SEEK_SET);
            if (fread(&mds_hdr, 1, sizeof(mds_hdr_t), fp) != sizeof(mds_hdr_t))
                return 0;
        }
        image_log(img->log, "ret = %i\n", ret);
    } else
        img->is_dvd = (mds_hdr.medium_type >= 0x10);

    if (img->is_dvd) {
        if (mds_hdr.disc_struct_offs != 0x00) {
            fseek(fp, mds_hdr.disc_struct_offs, SEEK_SET);
            if (fread(&(img->dstruct.layers[0]), 1, sizeof(layer_t), fp) != sizeof(layer_t))
                return 0;
            img->has_dstruct = 1;

            if (((img->dstruct.layers[0].f0[2] & 0x60) >> 4) == 0x01) {
                fseek(fp, mds_hdr.disc_struct_offs, SEEK_SET);
                if (fread(&(img->dstruct.layers[1]), 1, sizeof(layer_t), fp) != sizeof(layer_t))
                    return 0;
                img->has_dstruct++;
            }
        }

        for (int t = 0; t < 3; t++) {
            ct = image_insert_track(img, 1, 0xa0 + t);

            ct->attr        = DATA_TRACK;
            ct->mode        = 0;
            ct->form        = 0;
            ct->tno         = 0;
            ct->subch_type  = 0;
            memset(ct->extra, 0x00, 4);

            for (int i = 0; i < 3; i++) {
                ci = &(ct->idx[i]);
                ci->type = INDEX_NONE;
                ci->start = 0;
                ci->length = 0;
                ci->file_start = 0;
                ci->file_length = 0;
                ci->file = NULL;
            }

            ci = &(ct->idx[1]);

            if (t < 2)
                ci->start = (0x01 * 60 * 75) + (0 * 75) + 0;
        }
    }

    if (mds_hdr.dpm_blocks_offs != 0x00) {
        fseek(fp, mds_hdr.dpm_blocks_offs, SEEK_SET);
        if (LOG_VAR(dbnret) fread(&mds_dpm_blocks_num, 1, sizeof(uint32_t), fp) != sizeof(uint32_t)) {
            image_log(img->log, "dbnret = %i (expected: %i)\n", (int) dbnret, (int) sizeof(uint32_t));
            return 0;
        }

        if (mds_dpm_blocks_num > 0)  for (int b = 0; b < mds_dpm_blocks_num; b++) {
            fseek(fp, mds_hdr.dpm_blocks_offs + 4 + (b * 4), SEEK_SET);
            if (LOG_VAR(dboret) fread(&mds_dpm_block_offs, 1, sizeof(uint32_t), fp) != sizeof(uint32_t)) {
                image_log(img->log, "dboret = %i (expected: %i)\n", (int) dboret, (int) sizeof(uint32_t));
                return 0;
            }

            fseek(fp, mds_dpm_block_offs, SEEK_SET);
            if (LOG_VAR(dbret) fread(&mds_dpm_block, 1, sizeof(mds_dpm_block_t), fp) != sizeof(mds_dpm_block_t)) {
                image_log(img->log, "dbret = %i (expected: %i)\n", (int) dbret, (int) sizeof(mds_dpm_block_t));
                return 0;
            }

            /* We currently only support the bad sectors block and not (yet) actual DPM. */
            if (mds_dpm_block.type == 0x00000002) {
                /* Bad sectors. */
                img->bad_sectors_num = mds_dpm_block.entries;
                img->bad_sectors     = (uint32_t *) malloc(img->bad_sectors_num * sizeof(uint32_t));
                fseek(fp, mds_dpm_block_offs + sizeof(mds_dpm_block_t), SEEK_SET);
                int read_size = img->bad_sectors_num * sizeof(uint32_t);
                if (LOG_VAR(dbtret) fread(img->bad_sectors, 1, read_size, fp) != read_size) {
                    image_log(img->log, "dbtret = %i (expected: %i)\n", (int) dbtret, (int) read_size);
                    return 0;
                }
                break;
            }
        }
    }

    for (int s = 0; s < mds_hdr.sess_num; s++) {
        if (version == 2) {
            fseek(fp, mds_hdr.sess_blocks_offs + (s * sizeof(mds_v2_sess_block_t)), SEEK_SET);
            if (LOG_VAR(hret) fread(&mds_v2_sess_block, 1, sizeof(mds_v2_sess_block_t), fp) != sizeof(mds_v2_sess_block_t)) {
                image_log(img->log, "hret = %i (expected: %i)\n", (int) hret, (int) sizeof(mds_v2_sess_block_t));
                return 0;
            }
            memcpy(&mds_sess_block, &mds_v2_sess_block, sizeof(mds_sess_block_t));
            mds_sess_block.sess_start = (int32_t) mds_v2_sess_block.sess_start;
            mds_sess_block.sess_end   = (int32_t) mds_v2_sess_block.sess_end;
        } else {
            fseek(fp, mds_hdr.sess_blocks_offs + (s * sizeof(mds_sess_block_t)), SEEK_SET);
            if (LOG_VAR(hret2) fread(&mds_sess_block, 1, sizeof(mds_sess_block_t), fp) != sizeof(mds_sess_block_t)) {
                image_log(img->log, "hret2 = %i (expected: %i)\n", (int) hret2, (int) sizeof(mds_sess_block_t));
                return 0;
            }
        }

        for (int t = 0; t < mds_sess_block.all_blocks_num; t++) {
            fseek(fp, mds_sess_block.trk_blocks_offs + (t * sizeof(mds_trk_block_t)), SEEK_SET);
            if (LOG_VAR(tbret) fread(&mds_trk_block, 1, sizeof(mds_trk_block_t), fp) != sizeof(mds_trk_block_t)) {
                image_log(img->log, "tbret = %i (expected: %i)\n", (int) tbret, (int) sizeof(mds_trk_block));
                return 0;
            }

            if (version == 2) {
                image_log(img->log, "Start sector V2: %016" PRIX64 "\n", mds_trk_block.start_sect_v2);
                mds_trk_block.start_sect = (uint32_t) mds_trk_block.start_sect_v2;
            }

            if (last_t != -1) {
                /*
                   Important: This has to be done like this because pointers
                              change due to realloc.
                 */
                ct = &(img->tracks[img->tracks_num - 1]);

                for (int i = 2; i >= 0; i--) {
                    if (ct->idx[i].file == NULL)
                        ct->idx[i].file = tf;
                    else
                        break;
                }
            }

            last_t           = mds_trk_block.point;
            ct               = image_insert_track(img, mds_sess_block.sess_id, mds_trk_block.point);

            if (img->is_dvd) {
                /* DVD images have no extra block - the extra block offset is the track length. */
                memset(&mds_trk_ex_block, 0x00, sizeof(mds_trk_ex_block_t));
                mds_trk_ex_block.pregap = 0x00000000;
                mds_trk_ex_block.trk_sectors = mds_trk_block.ex_offs;
            } else if (mds_trk_block.ex_offs != 0ULL) {
                fseek(fp, mds_trk_block.ex_offs, SEEK_SET);
                if (LOG_VAR(tret) fread(&mds_trk_ex_block, 1, sizeof(mds_trk_ex_block), fp) != sizeof(mds_trk_ex_block)) {
                    image_log(img->log, "tret = %i (expected: %i)\n", (int) tret, (int) sizeof(mds_trk_ex_block));
                    return 0;
                }
            }

            uint32_t astart = mds_trk_block.start_sect - mds_trk_ex_block.pregap;
            uint32_t aend = astart + mds_trk_ex_block.pregap;
            uint32_t aend2 = aend + mds_trk_ex_block.trk_sectors;
            uint32_t astart2 = mds_trk_block.start_sect + mds_trk_ex_block.trk_sectors;

            ct->skip = 0;

            if (mds_trk_block.footer_offs != 0ULL)  for (uint32_t ff = 0; ff < mds_trk_block.files_num; ff++) {
                if (version == 2) {
                    fseek(fp, mds_trk_block.footer_offs + (ff * sizeof(mds_v2_footer_t)), SEEK_SET);
                    if (LOG_VAR(fret) fread(&mds_v2_footer, 1, sizeof(mds_v2_footer_t), fp) != sizeof(mds_v2_footer_t)) {
                        image_log(img->log, "fret = %i (expected: %i)\n", (int) fret, (int) sizeof(mds_v2_footer_t));
                        return 0;
                    } 
                    memcpy(&mds_footer, &mds_v2_footer, sizeof(mds_footer));
                    mds_footer.fn_is_wide = 1;
                } else {
                    fseek(fp, mds_trk_block.footer_offs + (ff * sizeof(mds_footer_t)), SEEK_SET);
                    if (LOG_VAR(fret2) fread(&mds_footer, 1, sizeof(mds_footer_t), fp) != sizeof(mds_footer_t)) {
                        image_log(img->log, "fret2 = %i (expected: %i)\n", (int) fret2, (int) sizeof(mds_footer_t));
                        return 0;
                    }
                }

                uint16_t wfn[2048] = { 0 };
                char     fn[2048] = { 0 };

                if (mds_footer.fn_offs == 0x00000000) {
                    /* This is in MDX files - the file name string is empty. */
                    strcpy(fn, mdsfile);
                    ct->skip = 0x40;
                } else {
                    fseek(fp, mds_footer.fn_offs, SEEK_SET);
                    if (mds_footer.fn_is_wide) {
                        for (int i = 0; i < 256; i++) {
                            if (LOG_VAR(fnret) fread(&(wfn[i]), 1, 2, fp) != 2) {
                                image_log(img->log, "fnret = %i (expected: %i)\n", (int) fnret, (int) 2);
                                return 0;
                            }
                            if (wfn[i] == 0x0000)
                                break;
                        }
                        (void) utf16_to_utf8(wfn, 2048, (uint8_t *) fn, 2048);
                    } else  for (int i = 0; i < 512; i++) {
                        if (LOG_VAR(fnret2) fread(&fn[i], 1, 1, fp) != 1) {
                            image_log(img->log, "fnret2 = %i (expected: %i)\n", (int) fnret2, (int) 1);
                            return 0;
                        }
                        if (fn[i] == 0x00)
                            break;
                    }

                    if (!stricmp(fn, "*.mdf")) {
                        strcpy(fn, mdsfile);
                        fn[strlen(mdsfile) - 3] = 'm';
                        fn[strlen(mdsfile) - 2] = 'd';
                        fn[strlen(mdsfile) - 1] = 'f';
                    }
                }
                image_log(img->log, "fn = \"%s\"\n", fn);

                char    filename[2048] = { 0 };
                if (!path_abs(fn))
                    path_append_filename(filename, pathname, fn);
                else
                    strcpy(filename, fn);

                if (strcmp(ofn, filename) != 0) {
                    tf = index_file_init(img->dev->id, filename, &error, &is_viso);
                    strcpy(ofn, filename);
                }
            }

            ct->sector_size = mds_trk_block.sector_len;
            ct->form        = 0;
            ct->tno         = mds_trk_block.track_id;
            ct->subch_type  = mds_trk_block.subch_mode;
            ct->extra[0]    = mds_trk_block.m;
            ct->extra[1]    = mds_trk_block.s;
            ct->extra[2]    = mds_trk_block.f;
            ct->extra[3]    = mds_trk_block.zero;
            /*
                Note from DiscImageCreator:

                I hexedited the track mode field with various values and fed it to Alchohol;
                it seemed that high part of byte had no effect at all; only the lower one
                affected the mode, in the following manner:
                00: Mode 2, 01: Audio, 02: Mode 1, 03: Mode 2, 04: Mode 2 Form 1,
                05: Mode 2 Form 2, 06: UKNONOWN, 07: Mode 2
                08: Mode 2, 09: Audio, 0A: Mode 1, 0B: Mode 2, 0C: Mode 2 Form 1,
                0D: Mode 2 Form 2, 0E: UKNONOWN, 0F: Mode 2
             */
            ct->attr        = ((mds_trk_block.trk_mode & 0x07) == 0x01) ?
                                  AUDIO_TRACK : DATA_TRACK;
            ct->mode        = 0;
            ct->form        = 0;
            if (((mds_trk_block.trk_mode & 0x07) != 0x01) && 
                ((mds_trk_block.trk_mode & 0x07) != 0x06))
                ct->mode        = ((mds_trk_block.trk_mode & 0x07) != 0x02) + 1;
            if ((mds_trk_block.trk_mode & 0x06) == 0x04)
                ct->form        = (mds_trk_block.trk_mode & 0x07) - 0x03;
            if (ct->attr == AUDIO_TRACK)
                success         = 1;

            if (((ct->sector_size == 2336) || (ct->sector_size == 2332)) && (ct->mode == 2) && (ct->form == 1))
                ct->skip       += 8;

            ci = &(ct->idx[0]);
            if (ct->point < 0xa0) {
                ci->start = astart + 150;
                ci->length = mds_trk_ex_block.pregap;
            }
            ci->type = (ci->length > 0) ? INDEX_ZERO : INDEX_NONE;
            ci->file_start = 0;
            ci->file_length = 0;
            ci->file = NULL;

            ci = &(ct->idx[1]);
            if ((mds_trk_block.point >= 1) && (mds_trk_block.point <= 99)) {
                ci->start = aend + 150;
                ci->length = mds_trk_ex_block.trk_sectors;
                ci->type = INDEX_NORMAL;
                ci->file_start = (mds_trk_block.start_offs - (ct->skip & 0x40)) / ct->sector_size;
                ci->file_length = ci->length;
                ci->file = tf;
            } else {
                ci->start = (mds_trk_block.pm * 60 * 75) + (mds_trk_block.ps * 75) + mds_trk_block.pf;
                ci->type = INDEX_NONE;
                ci->file_start = 0;
                ci->file_length = 0;
                ci->file = NULL;
            }

            ci = &(ct->idx[2]);
            if (ct->point < 0xa0) {
                ci->start = aend2 + 150;
                ci->length = astart2 - aend2;
            }
            ci->type = (ci->length > 0) ? INDEX_ZERO : INDEX_NONE;
            ci->file_start = 0;
            ci->file_length = 0;
            ci->file = NULL;

            if (img->is_dvd) {
                ci = &(ct->idx[1]);
                uint32_t total = ci->start + ci->length;

                ci = &(img->tracks[2].idx[1]);
                ci->start = total;
            }
        }

        for (int i = 2; i >= 0; i--) {
            if (ct->point >= 0xa0)
                ci->type = INDEX_SPECIAL;

            if (ct->idx[i].file == NULL)
                ct->idx[i].file = tf;
            else
                break;
        }
    }

    tf = NULL;

    fclose(fp);

    if (success) {
#ifdef ENABLE_IMAGE_LOG
        image_log(img->log, "Final tracks list:\n");
        for (int i = 0; i < img->tracks_num; i++) {
            ct = &(img->tracks[i]);
            for (int j = 0; j <= ct->max_index; j++) {
                ci = &(ct->idx[j]);
                    image_log(img->log, "    [TRACK   ] %02X INDEX %02X: [%8s, %016" PRIX64 "]\n",
                          ct->point, j,
                          cit[ci->type + 2], ci->file_start * ct->sector_size);
                image_log(img->log, "               TOC data: %02X %02X %02X "
                          "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                          ct->session, ct->attr, ct->tno, ct->point,
                          ct->extra[0], ct->extra[1], ct->extra[2], ct->extra[3],
                          (uint32_t) ((ci->start / 75) / 60),
                          (uint32_t) ((ci->start / 75) % 60),
                          (uint32_t) (ci->start % 75));
            }
        }
#endif
    } else
#ifdef ENABLE_IMAGE_LOG
        log_warning(img->log, "    [MDS   ] Unable to open MDS sheet \"%s\"\n", mdsfile);
#else
        warning("Unable to open MDS sheet \"%s\"\n", mdsfile);
#endif

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
                (cur->point == 0xa2))  for (int j = 0; j <= cur->max_index; j++) {
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
        const uint32_t pos = end ? (ct->idx[1].start + ct->idx[1].length) :
                                   ct->idx[1].start;

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
    cdrom_t          *dev    = (cdrom_t *) img->dev;
    int               m      = 0;
    int               s      = 0;
    int               f      = 0;
    int               ret    = 0;
    uint32_t          lba    = sector;
    int               track;
    int               index;
    uint8_t           q[16]  = { 0x00 };
    uint8_t          *buf    = buffer;

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

            if (idx->type >= INDEX_NORMAL)
                /* Read the data from the file. */
                ret = idx->file->read(idx->file, buffer, seek, trk->sector_size);
            else
                /* Index is not in the file, no read to fail here. */
                ret = 1;

            if ((ret > 0) && (trk->attr & 0x04) && ((idx->type < INDEX_NORMAL) || !track_is_raw)) {
                uint32_t crc;

                if ((trk->mode == 2) && (trk->form == 1)) {
                    crc = cdrom_crc32(0xffffffff, &(buf[16]), 2056) ^ 0xffffffff;
                    memcpy(&(buf[2072]), &crc, 4);
                } else {
                    crc = cdrom_crc32(0xffffffff, buf, 2064) ^ 0xffffffff;
                    memcpy(&(buf[2064]), &crc, 4);
                }

                int m2f1 = (trk->mode == 2) && (trk->form == 1);

                /* Compute ECC P code. */
                cdrom_compute_ecc_block(dev, &(buf[2076]), &(buf[12]), 86, 24, 2, 86, m2f1);

                /* Compute ECC Q code. */
                cdrom_compute_ecc_block(dev, &(buf[2248]), &(buf[12]), 52, 43, 86, 88, m2f1);
            }

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
    const cd_image_t *img = (const cd_image_t *) local;
    int               ret = 0;

    if ((img->has_dstruct > 0) && ((layer + 1) > img->has_dstruct)) {
        switch (format) {
            case 0x00:
                memcpy(buffer + 4, img->dstruct.layers[layer].f0, 2048);
                ret = 2048 + 2;
                break;
            case 0x01:
                memcpy(buffer + 4, img->dstruct.layers[layer].f1, 4);
                ret = 4 + 2;
                break;
            case 0x04:
                memcpy(buffer + 4, img->dstruct.layers[layer].f4, 2048);
                ret = 2048 + 2;
                break;
        }
    }

    return ret;
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

        if (img->bad_sectors != NULL)
            free(img->bad_sectors);

        free(img);

        if (temp_file[0] != 0x00) {
            remove(temp_file);
            temp_file[0] = 0x00;
        }
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
        const int is_cue  = ((ext == 4) && !stricmp(path + strlen(path) - ext + 1, "CUE"));
        const int is_mds  = ((ext == 4) && (!stricmp(path + strlen(path) - ext + 1, "MDS") ||
                                            !stricmp(path + strlen(path) - ext + 1, "MDX")));
        char      n[1024] = { 0 };

        sprintf(n, "CD-ROM %i Image", dev->id + 1);
        img->log          = log_open(n);

        img->dev          = dev;

        if (is_mds) {
            ret = image_load_mds(img, path);

            if (ret >= 2)
                img->has_audio = 0;
            else if (ret)
                img->has_audio = 1;
        } else if (is_cue) {
            ret = image_load_cue(img, path);

            if (ret >= 2)
                img->has_audio = 0;
            else if (ret)
                img->has_audio = 1;

            if (ret >= 1)
                img->is_dvd = 2;
        } else {
            ret = image_load_iso(img, path);

            if (ret) {
                img->has_audio = 0;
                img->is_dvd = 2;
            }
        }

        if (ret > 0) {
            if (img->is_dvd == 2) {
                uint32_t lb = image_get_last_block(img); /* Should be safer than previous way of doing it? */
                img->is_dvd = (lb >= 524287);    /* Minimum 1 GB total capacity as threshold for DVD. */
            }

            dev->ops = &image_ops;
        } else {
            log_warning(img->log, "Unable to load CD-ROM image: %s\n", path);

            image_close(img);
            img = NULL;
        }
    }

    return img;
}
