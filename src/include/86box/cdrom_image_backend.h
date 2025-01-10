/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CD-ROM image file handling module header.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          RichardG, <richardg867@gmail.com>
 *          Cacodemon345
 *
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2024-2025 Cacodemon345.
 */
#ifndef CDROM_IMAGE_BACKEND_H
#define CDROM_IMAGE_BACKEND_H

#define RAW_SECTOR_SIZE    2352
#define COOKED_SECTOR_SIZE 2048

#define DATA_TRACK         0x14
#define AUDIO_TRACK        0x10

#define CD_FPS             75
#define FRAMES_TO_MSF(f, M, S, F)                 \
    {                                             \
        uint64_t value = f;                       \
        *(F)           = (value % CD_FPS) & 0xff; \
        value /= CD_FPS;                          \
        *(S) = (value % 60) & 0xff;               \
        value /= 60;                              \
        *(M) = value & 0xff;                      \
    }
#define MSF_TO_FRAMES(M, S, F) ((M) *60 * CD_FPS + (S) *CD_FPS + (F))

typedef struct SMSF {
    uint16_t min;
    uint8_t  sec;
    uint8_t  fr;
} TMSF;

/* Track file struct. */
typedef struct track_file_t {
    int (*read)(void *priv, uint8_t *buffer, uint64_t seek, size_t count);
    uint64_t (*get_length)(void *priv);
    void (*close)(void *priv);

    char  fn[260];
    FILE *fp;
    void *priv;

    int motorola;
} track_file_t;

#define INDEX_SPECIAL -2 /* Track A0h onwards. */
#define INDEX_NONE    -1 /* Empty block. */
#define INDEX_ZERO     0 /* Block not in the file, return all 0x00's. */
#define INDEX_NORMAL   1 /* Block in the file. */

typedef struct track_index_t {
    /* Is the current block in the file? If not, return all 0x00's. -1 means not yet loaded. */
    int32_t       type;
    /* The amount of bytes to skip at the beginning of each sector. */
    int32_t       skip;
    /* Starting and ending sector LBA - negative in order to accomodate LBA -150 to -1
       to read the pregap of track 1. */
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
    uint8_t       pad;
    uint8_t       skip;
    uint32_t      sector_size;
    track_index_t idx[3];
} track_t;

typedef struct cd_img_t {
    int32_t       tracks_num;
    track_t      *tracks;
} cd_img_t;

/* Binary file functions. */
extern void cdi_get_raw_track_info(cd_img_t *cdi, int *num, uint8_t *buffer);
extern int  cdi_get_audio_sub(cd_img_t *cdi, uint32_t sector, uint8_t *attr, uint8_t *track,
                              uint8_t *index, TMSF *rel_pos, TMSF *abs_pos);
extern int  cdi_read_sector(cd_img_t *cdi, uint8_t *buffer, int raw, uint32_t sector);
extern int  cdi_read_sector_sub(cd_img_t *cdi, uint8_t *buffer, uint32_t sector);
extern int  cdi_get_sector_size(cd_img_t *cdi, uint32_t sector);
extern int  cdi_is_audio(cd_img_t *cdi, uint32_t sector);
extern int  cdi_is_pre(cd_img_t *cdi, uint32_t sector);
extern int  cdi_is_mode2(cd_img_t *cdi, uint32_t sector);
extern int  cdi_get_mode2_form(cd_img_t *cdi, uint32_t sector);
extern int  cdi_load_iso(cd_img_t *cdi, const char *filename);
extern int  cdi_load_cue(cd_img_t *cdi, const char *cuefile);
extern void cdi_close(cd_img_t *cdi);
extern int  cdi_set_device(cd_img_t *cdi, const char *path);

/* Virtual ISO functions. */
extern int           viso_read(void *priv, uint8_t *buffer, uint64_t seek, size_t count);
extern uint64_t      viso_get_length(void *priv);
extern void          viso_close(void *priv);
extern track_file_t *viso_init(const char *dirname, int *error);

#endif /*CDROM_IMAGE_BACKEND_H*/
