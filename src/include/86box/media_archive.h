#ifndef EMU_MEDIA_ARCHIVE_H
#define EMU_MEDIA_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int
media_archive_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)
        return 0;
    return ((ext[1] == 'z' || ext[1] == 'Z') &&
            (ext[2] == 'i' || ext[2] == 'I') &&
            (ext[3] == 'p' || ext[3] == 'P') && !ext[4]) ||
           (ext[1] == '7' && (ext[2] == 'z' || ext[2] == 'Z') && !ext[3]);
}

/* Entry 0 is the root. The first/entry functions return 0 at the end of a directory.
   Names are borrowed from the archive and remain valid until close. */
void *media_archive_open(const char *path);
size_t media_archive_first(const void *archive, size_t parent);
size_t media_archive_entry(const void *archive, size_t id, const char **name, uint64_t *size, int *is_directory);
int media_archive_read(void *archive, size_t id, uint64_t offset, uint8_t *buffer, size_t count);
void media_archive_close(void *archive);

#ifdef __cplusplus
}
#endif

#endif
