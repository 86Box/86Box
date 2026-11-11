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
 *          Copyright 2016-2025 RichardG.
 *          Copyright 2024-2025 Cacodemon345.
 */
#ifndef CDROM_IMAGE_H
#define CDROM_IMAGE_H

/* Track file struct. */
typedef struct track_file_t {
    int (*read)(void *priv, uint8_t *buffer, uint64_t seek, size_t count);
    uint64_t (*get_length)(void *priv);
    void (*close)(void *priv);

    char  fn[260];
    FILE *fp;
    void *priv;
    void *log;

    int motorola;
} track_file_t;

/* A track of a disc that is being recorded. */
typedef struct aaru_write_track_t {
    uint8_t number;
    uint8_t session;
    uint8_t ctl;
    uint8_t mode;     /* 0 = audio, 1 = Mode 1, 2 = Mode 2. */
    uint8_t packet;   /* Incremental, may stay open across media changes. */
    char    isrc[12]; /* All zeros without one. */
    int32_t start;    /* First sector, including the pre-gap. */
    int32_t index1;   /* First sector of index 1. */
    int32_t end;      /* Last sector, -1 if nothing has been written yet. */
} aaru_write_track_t;

/*
   Fills a 2352-byte buffer with a sector of the disc. Returns 0 on success, 1 for a
   sector that was never recorded and is left out of the image, and -1 on failure.
 */
typedef int (*aaru_write_read_t)(void *priv, const int32_t lba, uint8_t *buffer);

extern int   aaru_image_writer_available(void);
extern int   aaru_image_write_disc(const char *path, const int rewritable, const char *state,
                                   const aaru_write_track_t *tracks, const int tracks_num,
                                   const uint8_t *full_toc, const uint32_t full_toc_len,
                                   const uint8_t *mcn, aaru_write_read_t read, void *priv);
extern int   aaru_image_get_disc_state(const char *path, int *rewritable, char *state, const int len);
/* Reads a disc back without copying it anywhere, for as long as nothing changes it. */
extern void *aaru_image_source_open(const char *path, aaru_write_track_t *tracks, int *tracks_num,
                                    uint8_t *mcn);
extern int   aaru_image_source_read(void *source, const int32_t lba, uint8_t *buffer);
extern void  aaru_image_source_close(void *source);

extern void *image_open(cdrom_t *dev, const char *path);
extern void *aaru_image_open(cdrom_t *dev, const char *path);
extern void *chd_image_open(cdrom_t *dev, const char *path);
extern void *ccd_image_open(cdrom_t *dev, const char *path);

#endif /*CDROM_IMAGE_H*/
