/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Archive reader for media mounted from archives, using libarchive.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2026 RichardG.
 */
#ifndef EMU_MEDIA_ARCHIVE_H
#define EMU_MEDIA_ARCHIVE_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct media_archive_entry_t {
    struct media_archive_entry_t *parent;
    struct media_archive_entry_t *children; /* first child, in name order */
    struct media_archive_entry_t *next;     /* next sibling, in name order */
    struct media_archive_entry_t *all_next; /* every entry, for freeing */

    uint64_t size;
    time_t   mtime;
    uint64_t offset; /* within the extracted data, see media_archive_extract_all() */
    uint32_t index;  /* header number in the archive, regular files only */
    uint8_t  is_dir;
    uint8_t  dos_attr;   /* DOS attributes stored in the archive, if any */
    uint8_t  has_header; /* the archive has a header for this entry */
    void    *priv;       /* for the consumer */

    char name[]; /* one path component, UTF-8 */
} media_archive_entry_t;

typedef struct media_archive_t media_archive_t;

/* Data callback for media_archive_extract(): returns 0 to stop. */
typedef int (*media_archive_data_t)(void *opaque, media_archive_entry_t *entry, uint64_t offset,
                                    const void *data, size_t size);

/* Whether the path names a file that should be treated as an archive. */
extern int media_archive_is_archive(const char *path);

/* Lists the archive into a directory tree. */
extern media_archive_t       *media_archive_open(const char *path);
extern void                   media_archive_close(media_archive_t *archive);
extern media_archive_entry_t *media_archive_root(media_archive_t *archive);
extern uint32_t               media_archive_count(media_archive_t *archive);

/* Streams the data of every regular file, in archive order. Files that can't be read are passed
   over, and media_archive_unreadable() then says so. Returns 0 if the archive can't be read
   again or the callback stops it. */
extern int media_archive_extract(media_archive_t *archive, media_archive_data_t callback, void *opaque);
/* Whether files were left out of the listing, or were only partly extracted, as they could not be
   read. */
extern int media_archive_unreadable(const media_archive_t *archive);

/* Extracts every regular file into an anonymous temporary file for random access. */
extern int media_archive_extract_all(media_archive_t *archive);
extern int media_archive_read(media_archive_t *archive, const media_archive_entry_t *entry,
                              uint64_t offset, void *buffer, size_t count);

/* The code page of the names written for DOS, and read from archives that don't flag them as UTF-8:
   932 (Shift-JIS) when 86Box is in Japanese or the machine is Japanese, else the host's OEM code page on
   Windows and 437 elsewhere. */
extern int media_codepage(void);

/* Opens an anonymous temporary file in the VM directory, deleted when closed. */
extern FILE *media_temp_file(const char *prefix);
/* Sets the size of a temporary file, leaving the space unallocated where possible. */
extern int media_temp_file_set_size(FILE *fp, uint64_t size);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MEDIA_ARCHIVE_H */
