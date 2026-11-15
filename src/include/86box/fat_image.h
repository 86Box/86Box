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

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FAT_IMAGE_OK = 0,
    FAT_IMAGE_UNSUPPORTED,    /* the drive or medium can't hold a folder */
    FAT_IMAGE_FULL,           /* the contents don't fit, or a file is 4 GB or larger */
    FAT_IMAGE_ROOT_FULL,      /* too many files in the root directory */
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

/* Writes data at an offset of the image; the image must start out filled with zeros.
   Returns 0 on failure. */
typedef int (*fat_image_write_t)(void *opaque, uint64_t offset, const void *data, size_t size);

/* Whether the path is a folder or an archive that can be turned into a volume. */
extern int fat_image_is_source(const char *path);

/* Reads the folder or archive tree. */
extern fat_image_result_t fat_image_scan(const char *path, fat_image_t **image);
extern void               fat_image_free(fat_image_t *image);

/* Checks whether the tree fits a volume without writing anything. */
extern fat_image_result_t fat_image_check(fat_image_t *image, const fat_image_params_t *params);

/* Writes the volume, including all file data. */
extern fat_image_result_t fat_image_write(fat_image_t *image, const fat_image_params_t *params,
                                          fat_image_write_t write, void *opaque);

/* Convenience writer and generator for an image held in memory. */
extern int                fat_image_write_mem(void *opaque, uint64_t offset, const void *data, size_t size);
extern fat_image_result_t fat_image_create_mem(const char *path, const fat_image_params_t *params, uint8_t **data);

/* A FILE * writer. */
extern int fat_image_write_file(void *opaque, uint64_t offset, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* EMU_FAT_IMAGE_H */
