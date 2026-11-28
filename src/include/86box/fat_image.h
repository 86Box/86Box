/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          FAT12/16/32 volume generator for media mounted from host
 *          folders and archives.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2026 RichardG.
 */
#ifndef EMU_FAT_IMAGE_H
#define EMU_FAT_IMAGE_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FAT_IMAGE_OK = 0,
    FAT_IMAGE_UNSUPPORTED,    /* the drive or medium can't hold a folder */
    FAT_IMAGE_FULL,           /* not even the root directory fits */
    FAT_IMAGE_ROOT_FULL,      /* a folder has more names than numeric tails can tell apart */
    FAT_IMAGE_READ_ERROR,     /* the folder or archive could not be read */
    FAT_IMAGE_WRITE_ERROR,    /* the image could not be written */
    FAT_IMAGE_NO_MEMORY
} fat_image_result_t;

typedef struct fat_image_params_t {
    uint64_t sectors;           /* whole medium, in logical sectors */
    uint32_t sector_size;       /* logical sector size: 512, 1024, 2048 or 4096 */
    uint16_t sectors_per_track; /* BPB and partition table geometry */
    uint16_t heads;
    uint32_t partition_start;   /* first sector of the partition; 0 = no partition table */
    uint8_t  partition_slot;    /* partition table entry, 0 to 3 */
    uint8_t  partition_active;
    uint8_t  drive_number;      /* BPB drive number: 0x00 or 0x80 */
    uint8_t  pc98;              /* PC-98 boot sector */

    /* A fixed FAT12 layout, as on floppies. If spc is 0, everything is picked from the size. */
    uint8_t  media_desc;
    uint8_t  spc;
    uint8_t  num_fats;
    uint16_t spfat;
    uint16_t root_entries;
} fat_image_params_t;

typedef struct fat_image_t fat_image_t;

/* Why files were left out of a volume they don't all fit on. */
#define FAT_IMAGE_LEFT_OUT_SPACE 0x01 /* out of space, or a file of 4 GB or more */
#define FAT_IMAGE_LEFT_OUT_ROOT  0x02 /* the FAT12 or FAT16 root directory is full */
#define FAT_IMAGE_LEFT_OUT_READ  0x04 /* files or folders could not be read; files that fail partway
                                         through are kept, reading as zeros past the failure */

/* Writes data at an offset of the image; the image must start out filled with zeros.
   Returns 0 on failure. */
typedef int (*fat_image_write_t)(void *opaque, uint64_t offset, const void *data, size_t size);

/* Whether the path is a folder or an archive that can be turned into a volume. */
extern int fat_image_is_source(const char *path);

/* Reads the folder or archive tree. Files and folders inside it that can't be read are left out. */
extern fat_image_result_t fat_image_scan(const char *path, fat_image_t **image);
extern void               fat_image_free(fat_image_t *image);

/* Lays the tree out on a volume without writing anything. Files and folders that don't fit are
   left out, and the rest goes on; fat_image_left_out() then says why. */
extern fat_image_result_t fat_image_check(fat_image_t *image, const fat_image_params_t *params);
/* FAT_IMAGE_LEFT_OUT_* flags for the last check or write; 0 if everything fits and could be read. */
extern int fat_image_left_out(const fat_image_t *image);

/* Writes the volume, including all file data. */
extern fat_image_result_t fat_image_write(fat_image_t *image, const fat_image_params_t *params,
                                          fat_image_write_t write, void *opaque);

/* Fills in the layout of a medium; returns 0 if the drive can't take a folder on it. */
typedef int (*fat_image_medium_t)(void *opaque, int medium, fat_image_params_t *params);

/* Picks one of count media for the tree: the default one if it holds everything, else the smallest
   that does, else the largest, with what fits. Returns the medium with its layout in params, or -1
   with the reason in result. The tree is laid out for the picked medium. */
extern int fat_image_pick(fat_image_t *image, int count, int def, fat_image_medium_t medium, void *opaque,
                          fat_image_params_t *params, fat_image_result_t *result);

/* Writes the volume into a new temporary file of the medium's size; *fp is NULL on failure. */
extern fat_image_result_t fat_image_write_temp(fat_image_t *image, const fat_image_params_t *params,
                                               const char *prefix, FILE **fp);

/* Convenience writer and generator for an image held in memory. */
extern int                fat_image_write_mem(void *opaque, uint64_t offset, const void *data, size_t size);
extern fat_image_result_t fat_image_create_mem(const char *path, const fat_image_params_t *params, uint8_t **data,
                                               int *left_out);

/* A FILE * writer. */
extern int fat_image_write_file(void *opaque, uint64_t offset, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* EMU_FAT_IMAGE_H */
