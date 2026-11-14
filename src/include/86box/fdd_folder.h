/*
 * 86Box    Folder-backed FAT12 floppy images.
 *
 *          This file is part of the 86Box distribution.
 */
#ifndef EMU_FLOPPY_FOLDER_H
#define EMU_FLOPPY_FOLDER_H

#include <stdint.h>
#include <86box/disksizes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FDD_FOLDER_OK,
    FDD_FOLDER_UNSUPPORTED,
    FDD_FOLDER_FULL,
    FDD_FOLDER_READ_ERROR,
    FDD_FOLDER_INVALID_NAME,
    FDD_FOLDER_CANNOT_CREATE,
    FDD_FOLDER_NO_MEMORY
} fdd_folder_result_t;

/* A NULL path creates a blank FAT12 volume. The caller frees a successful image. */
extern fdd_folder_result_t fdd_folder_create(const char *path, const disk_size_t *format, uint8_t **image);
extern fdd_folder_result_t fdd_mount_folder(void *priv, const char *path, int read_only);

#ifdef __cplusplus
}
#endif

#endif /* EMU_FLOPPY_FOLDER_H */
