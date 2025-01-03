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
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2024-2025 Cacodemon345.
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
track_file_init(const char *filename, int *error, int *is_viso)
{
    track_file_t *tf;

    *is_viso = 0;

    /* Current we only support .BIN files, either combined or one per
       track. In the future, more is planned. */
    tf = bin_init(filename, error);

    if (*error) {
        if ((tf != NULL) && (tf->close != NULL)) {
            tf->close(tf);
            tf = NULL;
        }

        tf  = viso_init(filename, error);

        if (!*error)
            *is_viso = 1;
    }

   return tf;
}

static void
index_file_close(track_index_t *idx)
{
    if (idx == NULL)
        return;

    if (idx->file == NULL)
        return;

    if (idx->file->close == NULL)
        return;

    idx->file->close(idx->file);
    idx->file = NULL;
}

void
cdi_get_raw_track_info(cd_img_t *cdi, int *num, uint8_t *buffer)
{
    int len = 0;

    cdrom_image_backend_log("cdi->tracks_num = %i\n", cdi->tracks_num);

    for (int i = 0; i < cdi->tracks_num; i++) {
        track_t *ct = &(cdi->tracks[i]);
#ifdef ENABLE_CDROM_IMAGE_BACKEND_LOG
        int old_len = len;
#endif
        buffer[len++] = ct->session;                  /* Session number */
        buffer[len++] = ct->attr;                     /* Track ADR and Control */
        buffer[len++] = ct->tno;                      /* TNO (always 0) */
        buffer[len++] = ct->point;                    /* Point (for track points - track number) */
        for (int j = 0; j < 4; j++)
            buffer[len++] = ct->extra[j];
        buffer[len++] = (ct->idx[1].start / 75) / 60;
        buffer[len++] = (ct->idx[1].start / 75) % 60;
        buffer[len++] = ct->idx[1].start % 75;
        cdrom_image_backend_log("%i: %02X %02X %02X %02X %02X %02X %02X\n", i,
                                buffer[old_len], buffer[old_len + 1], buffer[old_len + 2], buffer[old_len + 3],
                                buffer[old_len + 8], buffer[old_len + 9], buffer[old_len + 10]);
    }

    *num = cdi->tracks_num;
}

static int
cdi_get_track(cd_img_t *cdi, uint32_t sector)
{
    int ret = -1;

    for (int i = 0; i < cdi->tracks_num; i++) {
        track_t *ct = &(cdi->tracks[i]);
        for (int j = 0; j < 3; j++) {
            track_index_t *ci = &(ct->idx[j]);
            if (((sector + 150) >= ci->start) && ((sector + 150) <= (ci->start + ci->length - 1))) {
                ret = i;
                break;
            }
        }
    }

    return ret;
}

static void
cdi_get_track_and_index(cd_img_t *cdi, uint32_t sector, int *track, int *index)
{
    *track = -1;
    *index = -1;

    for (int i = 0; i < cdi->tracks_num; i++) {
        track_t *ct = &(cdi->tracks[i]);
        for (int j = 0; j < 3; j++) {
            track_index_t *ci = &(ct->idx[j]);
            if (((sector + 150) >= ci->start) && ((sector + 150) <= (ci->start + ci->length - 1))) {
                *track = i;
                *index = j;
                break;
            }
        }
    }
}

/* TODO: See if track start is adjusted by 150 or not. */
int
cdi_get_audio_sub(cd_img_t *cdi, uint32_t sector, uint8_t *attr, uint8_t *track, uint8_t *index, TMSF *rel_pos, TMSF *abs_pos)
{
    int cur_track = cdi_get_track(cdi, sector);

    if (cur_track < 1)
        return 0;

    *track             = (uint8_t) cur_track;
    const track_t *trk = &cdi->tracks[*track];
    *attr              = trk->attr;
    *index             = 1;

    /* Absolute position should be adjusted by 150, not the relative ones. */
    FRAMES_TO_MSF(sector + 150, &abs_pos->min, &abs_pos->sec, &abs_pos->fr);

    /* Relative position is relative Index 1 start - pre-gap values will be negative. */
    FRAMES_TO_MSF((int32_t) (sector + 150 - trk->idx[1].start), &rel_pos->min, &rel_pos->sec, &rel_pos->fr);

    return 1;
}

static __inline int
bin2bcd(int x)
{
    return (x % 10) | ((x / 10) << 4);
}

int
cdi_read_sector(cd_img_t *cdi, uint8_t *buffer, int raw, uint32_t sector)
{
    const uint64_t sect        = (uint64_t) sector;
    int            m           = 0;
    int            s           = 0;
    int            f           = 0;
    int            ret         = 0;
    uint64_t       offset      = 0ULL;
    int            track;
    int            index;
    int            raw_size;
    int            cooked_size;
    uint8_t        q[16] = { 0x00 };

    cdi_get_track_and_index(cdi, sector, &track, &index);

    if (track < 0)
        return 0;

    const track_t       *trk          = &(cdi->tracks[track]);
    const track_index_t *idx          = &(trk->idx[index]);
    const int            track_is_raw = ((trk->sector_size == RAW_SECTOR_SIZE) || (trk->sector_size == 2448));
    const uint64_t       seek         = (sect + 150 - idx->start + idx->file_start) * trk->sector_size;
 
    cdrom_image_backend_log("cdrom_read_sector(%08X): track %02X, index %02X, %016" PRIX64 ", %016" PRIX64 ", %i\n",
                            sector, track, index, idx->start, trk->sector_size);

    if (track_is_raw)
        raw_size = trk->sector_size;
    else
        raw_size = 2448;

    if ((trk->mode == 2) && (trk->form != 1)) {
        if (trk->form == 2)
            cooked_size = (track_is_raw ? 2328 : trk->sector_size); /* Both 2324 + ECC and 2328 variants are valid. */
        else
            cooked_size = 2336;
    } else
        cooked_size = COOKED_SECTOR_SIZE;

    if ((trk->mode == 2) && (trk->form >= 1))
        offset = 24ULL;
    else
        offset = 16ULL;

    if (idx->type < INDEX_NORMAL) {
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
            buffer[3] = trk->mode;
            ret = 1;
        }
    } else if (raw && !track_is_raw) {
        memset(buffer, 0x00, 2448);
        /* We are doing a raw read but the track is cooked, length should be cooked size. */
        const int temp = idx->file->read(idx->file, buffer + offset, seek, cooked_size);
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
            buffer[3] = trk->mode;
            ret = 1;
        }
    } else if (!raw && track_is_raw)
        /* The track is raw but we are doing a cooked read, length should be cooked size. */
        return idx->file->read(idx->file, buffer, seek + offset, cooked_size);
    else {
        /* The track is raw and we are doing a raw read, length should be raw size. */
        ret = idx->file->read(idx->file, buffer, seek, raw_size);
        if (raw && (raw_size == 2448))
            return ret;
    }

    /* Construct Q. */
    q[0] = (trk->attr >> 4) | ((trk->attr & 0xf) << 4);
    q[1] = bin2bcd(trk->point);
    q[2] = index;
    if (index == 0) {
        /* Pre-gap sector relative frame addresses count from 00:01:74 downwards. */
        FRAMES_TO_MSF((int32_t) (149 - (sector + 150 - idx->start)), &m, &s, &f);
    } else {
        FRAMES_TO_MSF((int32_t) (sector + 150 - idx->start), &m, &s, &f);
    }
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

/* TODO: Do CUE+BIN images with a sector size of 2448 even exist? */
int
cdi_read_sector_sub(cd_img_t *cdi, uint8_t *buffer, uint32_t sector)
{
    int            track;
    int            index;

    cdi_get_track_and_index(cdi, sector, &track, &index);

    if (track < 0)
        return 0;

    const track_t       *trk = &(cdi->tracks[track]);
    const track_index_t *idx = &(trk->idx[index]);

    const uint64_t seek = (((uint64_t) sector + 150 - idx->start + idx->file_start) * trk->sector_size);

    if ((idx->type < INDEX_NORMAL) && (trk->sector_size != 2448))
        return 0;

    return idx->file->read(idx->file, buffer, seek, 2448);
}

int
cdi_get_sector_size(cd_img_t *cdi, uint32_t sector)
{
    int track = cdi_get_track(cdi, sector);

    if (track < 0)
        return 0;

    const track_t *trk = &(cdi->tracks[track]);

    return trk->sector_size;
}

int
cdi_is_audio(cd_img_t *cdi, uint32_t sector)
{
    int track = cdi_get_track(cdi, sector);

    if (track < 0)
        return 0;

    const track_t *trk = &(cdi->tracks[track]);

    return !!(trk->mode == 0);
}

int
cdi_is_pre(cd_img_t *cdi, uint32_t sector)
{
    int track = cdi_get_track(cdi, sector);

    if (track < 0)
        return 0;

    const track_t *trk = &(cdi->tracks[track]);

    return !!(trk->attr & 0x01);
}

int
cdi_is_mode2(cd_img_t *cdi, uint32_t sector)
{
    int track = cdi_get_track(cdi, sector);

    if (track < 0)
        return 0;

    const track_t *trk = &(cdi->tracks[track]);

    return !!(trk->mode == 2);
}

int
cdi_get_mode2_form(cd_img_t *cdi, uint32_t sector)
{
    int track = cdi_get_track(cdi, sector);

    if (track < 0)
        return 0;

    const track_t *trk = &(cdi->tracks[track]);

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

    if (strstr(temp2, "PRE") != NULL)
        cur->attr |= 0x01;
    if (strstr(temp2, "DCP") != NULL)
        cur->attr |= 0x02;
    if (strstr(temp2, "4CH") != NULL)
        cur->attr |= 0x08;

    return 1;
}

static track_t *
cdi_insert_track(cd_img_t *cdi, uint8_t session, uint8_t point)
{
    track_t       *ct      = NULL;

    cdi->tracks_num++;
    if (cdi->tracks == NULL) {
        cdi->tracks = calloc(1, sizeof(track_t));
        ct          = &(cdi->tracks[0]);
    } else {
        cdi->tracks = realloc(cdi->tracks, cdi->tracks_num * sizeof(track_t));
        ct          = &(cdi->tracks[cdi->tracks_num - 1]);
    }
    cdrom_image_backend_log("%02X: cdi->tracks[%2i] = %016" PRIX64 "\n", point, cdi->tracks_num - 1, (uint64_t) ct);

    memset(ct, 0x00, sizeof(track_t));

    ct->session = session;
    ct->point   = point;

    for (int i = 0; i < 3; i++)
        ct->idx[i].type = (point > 99) ? INDEX_SPECIAL : INDEX_NONE;

    return ct;
}

static void
cdi_last_3_passes(cd_img_t *cdi)
{
    track_t       *ct       = NULL;
    track_t       *lt       = NULL;
    track_index_t *ci       = NULL;
    track_file_t  *tf       = NULL;
    uint64_t       tf_len   = 0ULL;
    uint64_t       cur_pos  = 0ULL;
    int            map[256] = { 0 };
    int            lead[3]  = { 0 };
    int            pos      = 0;
    int            ls       = 0;
    uint64_t       spg[256] = { 0ULL };
    track_t       *lo[256]  = { 0 };

    cdrom_image_backend_log("A2 = %016" PRIX64 "\n", (uint64_t) &(cdi->tracks[2]));
   
    for (int i = 0; i < cdi->tracks_num; i++) {
        ct = &(cdi->tracks[i]);
        if (((ct->point >= 1) && (ct->point <= 99)) || (ct->point >= 0xb0)) {
            if (ct->point == 0xb0) {
                /* Point B0h found, add the previous three lead tracks. */
                for (int j = 0; j < 3; j++) {
                    map[pos] = lead[j];
                    pos++;
                }
            }

            map[pos] = i;
            pos++;
        } else if ((ct->point >= 0xa0) && (ct->point <= 0xa2))
            lead[ct->point & 0x03] = i;
    }

    /* The last lead tracks. */
    for (int i = 0; i < 3; i++) {
         map[pos] = lead[i];
         pos++;
    }
#ifdef ENABLE_CDROM_IMAGE_BACKEND_LOG
    cdrom_image_backend_log("pos = %i, cdi->tracks_num = %i\n", pos, cdi->tracks_num);
    for (int i = 0; i < pos; i++)
         cdrom_image_backend_log("map[%02i] = %02X\n", i, map[i]);
#endif

    cdrom_image_backend_log("Second pass:\n");
    for (int i = (cdi->tracks_num - 1); i >= 0; i--) {
        ct = &(cdi->tracks[map[i]]);
        if (ct->idx[1].type != INDEX_SPECIAL) {
            for (int j = 2; j >= 0; j--) {
                ci = &(ct->idx[j]);

                if ((ci->type >= INDEX_ZERO) && (ci->file != tf)) {
                    tf     = ci->file;
                    if (tf != NULL) {
                        tf_len = tf->get_length(tf) / ct->sector_size;
                        cdrom_image_backend_log("    File length: %016" PRIX64 " sectors\n", tf_len);
                    }
                }

                if (ci->type == INDEX_NONE) {
                    /* Index was not in the cue sheet, keep its length at zero. */
                    ci->file_start = tf_len;
                } else if (ci->type == INDEX_NORMAL) {
                    /* Index was in the cue sheet and is present in the file. */
                    ci->file_length = tf_len - ci->file_start;
                    tf_len -= ci->file_length;
                }

                cdrom_image_backend_log("    TRACK %2i (%2i), ATTR %02X, INDEX %i: %2i, file_start = %016"
                                        PRIX64 " (%2i:%02i:%02i), file_length = %016" PRIX64 " (%2i:%02i:%02i)\n",
                                        i, map[i],
                                        ct->attr,
                                        j, ci->type,
                                        ci->file_start,
                                        (int) ((ci->file_start / 75) / 60),
                                        (int) ((ci->file_start / 75) % 60),
                                        (int) (ci->file_start % 75),
                                        ci->file_length,
                                        (int) ((ci->file_length / 75) / 60),
                                        (int) ((ci->file_length / 75) % 60),
                                        (int) (ci->file_length % 75));
            }
        }
    }

    cdrom_image_backend_log("Third pass:\n");
    for (int i = 0; i < cdi->tracks_num; i++) {
        int session_changed = 0;

        ct = &(cdi->tracks[map[i]]);
        if (ct->idx[1].type != INDEX_SPECIAL) {
            if (ct->session != ls) {
                /* The first track of a session always has a pre-gap of at least 0:02:00. */
                ci = &(ct->idx[0]);
                if (ci->type == INDEX_NONE) {
                    ci->type   = INDEX_ZERO;
                    ci->start  = 0ULL;
                    ci->length = 150ULL;
                }

                session_changed = 1;
                ls              = ct->session;
            }

            for (int j = 0; j < 3; j++) {
                ci = &(ct->idx[j]);

                if (ci->type == INDEX_NONE)
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

                cdrom_image_backend_log("    TRACK %2i (%2i) (%2i), ATTR %02X, MODE %i, INDEX %i: %2i, "
                                        "start = %016" PRIX64 " (%2i:%02i:%02i), length = %016" PRIX64
                                        " (%2i:%02i:%02i)\n",
                                        i, map[i],
                                        ct->point, ct->attr,
                                        ct->mode,
                                        j, ci->type,
                                        ci->start,
                                        (int) ((ci->start / 75) / 60),
                                        (int) ((ci->start / 75) % 60),
                                        (int) (ci->start % 75),
                                        ci->length,
                                        (int) ((ci->length / 75) / 60),
                                        (int) ((ci->length / 75) % 60),
                                        (int) (ci->length % 75));

                /* Set the pre-gap of the first track of this session. */
                if (session_changed)
                    spg[ct->session] = ct->idx[0].start;
            }
        }
    }

    /* Set the lead out starts for all sessions. */
    for (int i = 0; i <= ls; i++) {
        lo[i] = NULL;
        for (int j = (cdi->tracks_num - 1); j >= 0; j--) {
            track_t *jt = &(cdi->tracks[j]);
            if ((jt->session == ct->session)  && (jt->point >= 1) && (jt->point <= 99)) {
                lo[i] = &(cdi->tracks[j]);
                break;
            }
        }
    }

    cdrom_image_backend_log("Fourth pass:\n");
    for (int i = 0; i < cdi->tracks_num; i++) {
        ct = &(cdi->tracks[i]);
        lt = NULL;
        switch (ct->point) {
            case 0xa0:
                for (int j = 0; j < cdi->tracks_num; j++) {
                    track_t *jt = &(cdi->tracks[j]);
                    if ((jt->session == ct->session)  && (jt->point >= 1) && (jt->point <= 99)) {
                        lt = &(cdi->tracks[j]);
                        break;
                    }
                }
                if (lt != NULL) {
                    int disc_type = 0x00;

                    ct->attr = lt->attr;

                    ct->mode = lt->mode;
                    ct->form = lt->form;

                    if (lt->mode == 2)
                        disc_type = (lt->form > 0) ? 0x20 : 0x10;
                    for (int j = 0; j < 3; j++) {
                        ci = &(ct->idx[j]);
                        ci->type   = INDEX_ZERO;
                        ci->start  = (lt->point * 60 * 75) + (disc_type * 75);
                        ci->length = 0;
                    }
                }
                break;
            case 0xa1:
                for (int j = (cdi->tracks_num - 1); j >= 0; j--) {
                    track_t *jt = &(cdi->tracks[j]);
                    if ((jt->session == ct->session)  && (jt->point >= 1) && (jt->point <= 99)) {
                        lt = &(cdi->tracks[j]);
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
                    }
                }
                break;
            case 0xa2:
                if (lo[ct->session] != NULL) {
                    lt = lo[ct->session];

                    ct->attr = lt->attr;

                    ct->mode = lt->mode;
                    ct->form = lt->form;

                    if (ct->idx[1].type != INDEX_NORMAL) {
                        track_index_t *li = &(lt->idx[2]);

                        for (int j = 0; j < 3; j++) {
                            ci = &(ct->idx[j]);
                            ci->type   = INDEX_ZERO;
                            ci->start  = li->start + li->length;
                            ci->length = 0;
                        }
                    }
                }
                break;
            case 0xb0:
                /*
                   B0 MSF (*NOT* PMSF) points to the beginning of the pre-gap
                   of the corresponding session's first track.
                 */
                ct->extra[0] = (spg[ct->session] / 75) / 60;
                ct->extra[1] = (spg[ct->session] / 75) % 60;
                ct->extra[2] = spg[ct->session] % 75;

                /*
                   B0 PMSF points to the start of the lead out track
                   of the last session.
                 */
                if (lo[ls] != NULL) {
                    lt                = lo[ls];
                    track_index_t *li = &(lt->idx[2]);

                    ct->idx[1].start  = li->start + li->length;
                }
                break;
        }

#ifdef ENABLE_CDROM_IMAGE_BACKEND_LOG
        if ((ct->point >= 0xa0) && (ct->point <= 0xa2))
            cdrom_image_backend_log("    TRACK %02X, SESSION %i: start = %016" PRIX64 " (%2i:%02i:%02i)\n",
                  ct->point, ct->session,
                  ct->idx[1].start,
                  (int) ((ct->idx[1].start / 75) / 60),
                  (int) ((ct->idx[1].start / 75) % 60),
                  (int) (ct->idx[1].start % 75));
#endif
    }
}

int
cdi_load_iso(cd_img_t *cdi, const char *filename)
{
    track_t       *ct      = NULL;
    track_index_t *ci      = NULL;
    track_file_t  *tf      = NULL;
    int            success;
    int            error   = 1;
    int            is_viso = 0;

    cdi->tracks     = NULL;
    success         = 1;

    cdrom_image_backend_log("First pass:\n");
    cdi->tracks_num = 0;

    cdi_insert_track(cdi, 1, 0xa0);
    cdi_insert_track(cdi, 1, 0xa1);
    cdi_insert_track(cdi, 1, 0xa2);

    /* Data track (shouldn't there be a lead in track?). */
    tf = track_file_init(filename, &error, &is_viso);

    if (error) {
        cdrom_image_backend_log("ISO: cannot open file '%s'!\n", filename);

        if (tf != NULL) {
            tf->close(tf);
            tf = NULL;
        }
        success = 0;
    } else if (is_viso)
        success = 3;
 
    if (success) {
        ct = cdi_insert_track(cdi, 1, 1);
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

        /* For Mode 2 XA, skip the first 8 bytes in every sector when sector size = 2336. */
        if (cdi_can_read_pvd(ci->file, RAW_SECTOR_SIZE, 0, 0))
            ct->sector_size  = RAW_SECTOR_SIZE;
        else if (cdi_can_read_pvd(ci->file, 2336, 1, 0)) {
            ct->sector_size  = 2336;
            ct->mode         = 2;
        } else if (cdi_can_read_pvd(ci->file, 2324, 1, 2)) {
            ct->sector_size  = 2324;
            ct->mode         = 2;
            ct->form         = 2;
        } else if (cdi_can_read_pvd(ci->file, 2328, 1, 2)) {
            ct->sector_size  = 2328;
            ct->mode         = 2;
            ct->form         = 2;
        } else if (cdi_can_read_pvd(ci->file, 2336, 1, 1)) {
            ct->sector_size  = 2336;
            ct->mode         = 2;
            ct->form         = 1;
            ct->skip         = 8;
        } else if (cdi_can_read_pvd(ci->file, RAW_SECTOR_SIZE, 1, 0)) {
            ct->sector_size  = RAW_SECTOR_SIZE;
            ct->mode         = 2;
        } else if (cdi_can_read_pvd(ci->file, RAW_SECTOR_SIZE, 1, 1)) {
            ct->sector_size  = RAW_SECTOR_SIZE;
            ct->mode         = 2;
            ct->form         = 1;
        } else {
            /* We use 2048 mode 1 as the default. */
            ct->sector_size  = COOKED_SECTOR_SIZE;
        }

        cdrom_image_backend_log("TRACK 1: Mode = %i, Form = %i, Sector size = %08X\n",
                                ct->mode, ct->form, ct->sector_size);
    }

    tf = NULL;

    if (!success)
        return 0;

    cdi_last_3_passes(cdi);

    return success;
}

int
cdi_load_cue(cd_img_t *cdi, const char *cuefile)
{
    track_t       *ct      = NULL;
    track_index_t *ci      = NULL;
    track_file_t  *tf      = NULL;
    uint64_t       frame   = 0ULL;
    uint64_t       last    = 0ULL;
    uint8_t        session = 1;
    int            success;
    int            error;
    int            is_viso = 0;
    int            lead[3] = { 0 };
    char           pathname[MAX_FILENAME_LENGTH];
    char           buf[MAX_LINE_LENGTH];
    FILE          *fp;
    char          *line;
    char          *command;
    char          *type;
    char           temp;

    cdi->tracks     = NULL;
    cdi->tracks_num = 0;

    /* Get a copy of the filename into pathname, we need it later. */
    memset(pathname, 0, MAX_FILENAME_LENGTH * sizeof(char));
    path_get_dirname(pathname, cuefile);

    /* Open the file. */
    fp = plat_fopen(cuefile, "r");
    if (fp == NULL)
        return 0;

    success = 0;

    cdrom_image_backend_log("First pass:\n");
    cdi->tracks_num = 0;

    for (int i = 0; i < 3; i++) {
        lead[i] = cdi->tracks_num;
        (void *) cdi_insert_track(cdi, session, 0xa0 + i);
    }
    cdrom_image_backend_log("lead[2] = %016" PRIX64 "\n", (uint64_t) &(cdi->tracks[lead[2]]));

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
        cdrom_image_backend_log("    line = %s\n", line);

        (void) cdi_cue_get_keyword(&command, &line);

        if (!strcmp(command, "FILE")) {
            /* The file for the track. */
            char filename[MAX_FILENAME_LENGTH];
            char ansi[MAX_FILENAME_LENGTH];

            tf = NULL;

            memset(ansi, 0, MAX_FILENAME_LENGTH * sizeof(char));
            memset(filename, 0, MAX_FILENAME_LENGTH * sizeof(char));

            success = cdi_cue_get_buffer(ansi, &line, 0);
            if (!success)
                break;
            success = cdi_cue_get_keyword(&type, &line);
            if (!success)
                break;

            error    = 1;
            is_viso  = 0;

            if (!strcmp(type, "BINARY") || !strcmp(type, "MOTOROLA")) {
                if (!path_abs(ansi))
                    path_append_filename(filename, pathname, ansi);
                else
                    strcpy(filename, ansi);

                tf = track_file_init(filename, &error, &is_viso);
                
                if (tf)
                    tf->motorola = !strcmp(type, "MOTOROLA");
            } else if (!strcmp(type, "WAVE") || !strcmp(type, "AIFF") || !strcmp(type, "MP3")) {
                if (!path_abs(ansi))
                    path_append_filename(filename, pathname, ansi);
                else
                    strcpy(filename, ansi);
                tf = audio_init(filename, &error);
            }
            if (error) {
                cdrom_image_backend_log("CUE: cannot open file '%s' in cue sheet!\n",
                                        filename);

                if (tf != NULL) {
                    tf->close(tf);
                    tf = NULL;
                }
                success = 0;
            } else if (is_viso)
                success = 3;
        } else if (!strcmp(command, "TRACK")) {
            int t            = cdi_cue_get_number(&line);
            success          = cdi_cue_get_keyword(&type, &line);

            if (!success)
                break;

            ct               = cdi_insert_track(cdi, session, t);

            cdrom_image_backend_log("    TRACK %i\n", t);

            ct->form         = 0;
            ct->mode         = 0;

            if (!strcmp(type, "AUDIO")) {
                ct->sector_size = RAW_SECTOR_SIZE;
                ct->attr        = AUDIO_TRACK;
            } else if (!memcmp(type, "MODE", 4)) {
                uint32_t mode;
                ct->attr        = DATA_TRACK;
                sscanf(type, "MODE%" PRIu32 "/%" PRIu32, &mode, &(ct->sector_size));
                ct->mode = mode;
                if (ct->mode == 2)  switch(ct->sector_size) {
                    case 2324: case 2328:
                        ct->form = 2;
                        break;
                    case 2048: case 2336: case 2352: case 2448:
                        ct->form = 1;
                        break;
                }
                if ((ct->sector_size == 2336) && (ct->mode == 2) && (ct->form == 1))
                    ct->skip        = 8;
            } else if (!memcmp(type, "CD", 2)) {
                ct->attr        = DATA_TRACK;
                ct->mode        = 2;
                sscanf(type, "CD%c/%i", &temp, &(ct->sector_size));
            } else
                success = 0;

            if (success)
                last = ct->sector_size;
        } else if (!strcmp(command, "INDEX")) {
            int t            = cdi_cue_get_number(&line);
            ci               = &(ct->idx[t]);

            cdrom_image_backend_log("        INDEX %i (1)\n", t);

            ci->type         = INDEX_NORMAL;
            ci->file         = tf;
            success          = cdi_cue_get_frame(&frame, &line);
            ci->file_start   = frame;
        } else if (!strcmp(command, "PREGAP")) {
            ci               = &(ct->idx[0]);
            cdrom_image_backend_log("        INDEX 0 (0)\n");

            ci->type         = INDEX_ZERO;
            ci->file         = tf;
            success          = cdi_cue_get_frame(&frame, &line);
            ci->length       = frame;
        } else if (!strcmp(command, "PAUSE")) {
            ci               = &(ct->idx[1]);
            cdrom_image_backend_log("        INDEX 1 (0)\n");

            ci->type         = INDEX_ZERO;
            ci->file         = tf;
            success          = cdi_cue_get_frame(&frame, &line);
            ci->length       = frame;
        } else if (!strcmp(command, "POSTGAP")) {
            ci               = &(ct->idx[2]);
            cdrom_image_backend_log("        INDEX 2 (0)\n");

            ci->type         = INDEX_ZERO;
            ci->file         = tf;
            success          = cdi_cue_get_frame(&frame, &line);
            ci->length       = frame;
        } else if (!strcmp(command, "ZERO")) {
            ci               = &(ct->idx[1]);
            cdrom_image_backend_log("        INDEX 1 (0)\n");

            ci->type         = INDEX_ZERO;
            ci->file         = tf;
            success          = cdi_cue_get_frame(&frame, &line);
            ci->length       = frame;
        } else if (!strcmp(command, "FLAGS"))
            success = cdi_cue_get_flags(ct, &line);
        else if (!strcmp(command, "REM")) {
            success = 1;
            char *space = strstr(line, " ");
            if (space != NULL) {
                space++;
                if (space < (line + strlen(line))) {
                    (void) cdi_cue_get_keyword(&command, &space);
                    if (!strcmp(command, "LEAD-OUT")) {
                        ct                   = &(cdi->tracks[lead[2]]);
                        cdrom_image_backend_log("lead[2] = %016" PRIX64 "\n", (uint64_t) ct);
                        ct->sector_size      = last;
                        ci                   = &(ct->idx[1]);
                        ci->type             = INDEX_NORMAL;
                        ci->file             = tf;
                        success              = cdi_cue_get_frame(&frame, &space);
                        ci->file_start       = frame;

                        cdrom_image_backend_log("        LEAD-OUT\n");
                    } else if (!strcmp(command, "SESSION")) {
                        session              = cdi_cue_get_number(&space);

                        if (session > 1) {
                            ct = cdi_insert_track(cdi, session - 1, 0xb0);
                            ci                   = &(ct->idx[1]);
                            ci->start            = (0x40 * 60 * 75) + (0x02 * 75);

                            if (session == 2) {
                                ct->extra[3]         = 0x02;

                                /* 5F:00:00 on Wembley, C0:00:00 in the spec. And what's in PMSF? */
                                ct = cdi_insert_track(cdi, session - 1, 0xc0);
                                ci                   = &(ct->idx[1]);
                                ct->extra[0]         = 0x5f;    /* Optimum recording power. */
                            } else
                                ct->extra[3]         = 0x01;

                            for (int i = 0; i < 3; i++) {
                                lead[i] = cdi->tracks_num;
                                (void *) cdi_insert_track(cdi, session, 0xa0 + i);
                            }
                            cdrom_image_backend_log("lead[2] = %016" PRIX64 "\n",
                                                    (uint64_t) &(cdi->tracks[lead[2]]));
                        }

                        cdrom_image_backend_log("        SESSION %i\n", session);
                    }
                }
            }
        } else if (!strcmp(command, "CATALOG") || !strcmp(command, "CDTEXTFILE") ||
                   !strcmp(command, "ISRC") || !strcmp(command, "PERFORMER") ||
                   !strcmp(command, "SONGWRITER") || !strcmp(command, "TITLE") ||
                   !strcmp(command, ""))
            /* Ignored commands. */
            success = 1;
        else {
            cdrom_image_backend_log("CUE: unsupported command '%s' in cue sheet!\n", command);

            success = 0;
        }

        if (!success)
            break;
    }

    tf = NULL;

    fclose(fp);

    if (!success)
        return 0;

    cdi_last_3_passes(cdi);

    return success;
}

/* Root functions. */
static void
cdi_clear_tracks(cd_img_t *cdi)
{
    track_file_t       *last = NULL;
    track_t            *cur  = NULL;
    track_index_t      *idx  = NULL;

    if ((cdi->tracks == NULL) || (cdi->tracks_num == 0))
        return;

    for (int i = 0; i < cdi->tracks_num; i++) {
        cur = &cdi->tracks[i];

        if ((cur->point >= 1) && (cur->point <= 99))  for (int j = 0; j < 3; j++) {
            idx = &(cur->idx[j]);

            /* Make sure we do not attempt to close a NULL file. */
            if (idx->file != NULL) {
                if (idx->file != last) {
                    last = idx->file;
                    index_file_close(idx);
                } else
                    idx->file = NULL;
            }
        }
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
    uintptr_t ext = path + strlen(path) - strrchr(path, '.');
    int       ret;

    cdrom_image_backend_log("cdi_set_device(): %" PRIu64 ", %lli, %s\n",
                            ext, strlen(path), path + strlen(path) - ext + 1);

    if ((ext == 4) && !stricmp(path + strlen(path) - ext + 1, "CUE")) {
        if ((ret = cdi_load_cue(cdi, path)))
            return ret;

        cdi_clear_tracks(cdi);
    }

    if ((ret = cdi_load_iso(cdi, path)))
        return ret;

    cdi_close(cdi);

    return 0;
}
