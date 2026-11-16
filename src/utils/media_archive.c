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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <archive.h>
#include <archive_entry.h>
#ifdef _WIN32
#    include <windows.h>
#    include <io.h>
#else
#    include <langinfo.h>
#    include <locale.h>
#    include <unistd.h>
#endif
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/nvr.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/media_archive.h>
#include <86box/fs_name.h>

#if defined(_WIN32) && defined(LIBARCHIVE_STATIC)
#    include <zlib.h>

/* libarchive's own build defines ZLIB_DLL on Windows whenever that links, so the
   static library of MSYS2 calls zlib through DLL import pointers, which the static
   zlib doesn't have. Provide them. */
#    define ZLIB_IMPORT(f) void *__imp_##f = (void *) f
ZLIB_IMPORT(crc32);
ZLIB_IMPORT(deflate);
ZLIB_IMPORT(deflateEnd);
ZLIB_IMPORT(deflateInit_);
ZLIB_IMPORT(deflateInit2_);
ZLIB_IMPORT(deflateReset);
ZLIB_IMPORT(inflate);
ZLIB_IMPORT(inflateEnd);
ZLIB_IMPORT(inflateInit_);
ZLIB_IMPORT(inflateInit2_);
ZLIB_IMPORT(inflateReset);
ZLIB_IMPORT(inflateSetDictionary);
ZLIB_IMPORT(zlibVersion);
#endif

#define BLOCK_SIZE 65536

struct media_archive_t {
    FILE                   *fp;
    media_archive_entry_t  *root;
    media_archive_entry_t  *all;
    media_archive_entry_t **by_index; /* header number -> regular file entry */
    uint32_t                headers;
    uint32_t                count;
    uint8_t                *block;
    FILE                   *data;     /* extracted data, see media_archive_extract_all() */
};

/* Extensions handled as archives. Raw disk images are never matched, as
   libarchive would also read some of them (such as ISO 9660). */
static const char *const extensions[] = {
    "7z", "cab", "cpio", "lha", "lzh", "rar", "tar", "taz", "tb2", "tbz", "tbz2", "tgz",
    "tlz", "txz", "tz2", "tzst", "xar", "zip", NULL
};
static const char *const tar_extensions[] = {
    "bz2", "gz", "lz", "lzma", "xz", "z", "zst", NULL
};

static int
match_extension(const char *extension, const char *const *list)
{
    for (; *list; list++) {
        if (!strcasecmp(extension, *list))
            return 1;
    }
    return 0;
}

int
media_archive_is_archive(const char *path)
{
    char *name = path_get_filename((char *) path);
    char *dot  = strrchr(name, '.');
    if (!dot || (dot == name))
        return 0;
    if (match_extension(dot + 1, extensions))
        return 1;

    /* .tar.gz and friends. */
    if (match_extension(dot + 1, tar_extensions)) {
        for (char *p = dot - 1; p > name; p--) {
            if (*p == '.')
                return ((dot - p) == 4) && !strncasecmp(p + 1, "tar", 3);
        }
    }
    return 0;
}

FILE *
media_temp_file(const char *prefix)
{
    char name[256];
    char path[1024];

    for (int i = 0; i < 100; i++) {
        plat_tempfile(name, (char *) prefix, NULL);
        snprintf(path, sizeof(path), "%s-%d.tmp", nvr_path(name), i);
#ifdef _WIN32
        /* The CRT deletes D files when they are closed. */
        FILE *fp = plat_fopen64(path, "w+bxD");
#else
        FILE *fp = plat_fopen64(path, "w+bx");
#endif
        if (fp) {
#ifndef _WIN32
            /* An unlinked file goes away with the emulator, even if it crashes. */
            remove(path);
#endif
            return fp;
        }
    }
    return NULL;
}

int
media_temp_file_set_size(FILE *fp, uint64_t size)
{
    if (fflush(fp))
        return 0;
#ifdef _WIN32
    /* NTFS allocates the whole size of a file that is not sparse. */
    DWORD  returned;
    HANDLE handle = (HANDLE) _get_osfhandle(_fileno(fp));
    DeviceIoControl(handle, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &returned, NULL);
    return !_chsize_s(_fileno(fp), size);
#else
    return !ftruncate(fileno(fp), size);
#endif
}

static la_ssize_t
archive_read_cb(UNUSED(struct archive *a), void *priv, const void **buffer)
{
    media_archive_t *archive = (media_archive_t *) priv;

    *buffer = archive->block;
    return (la_ssize_t) fread(archive->block, 1, BLOCK_SIZE, archive->fp);
}

static la_int64_t
archive_seek_cb(UNUSED(struct archive *a), void *priv, la_int64_t offset, int whence)
{
    const media_archive_t *archive = (const media_archive_t *) priv;

    if (fseeko64(archive->fp, offset, whence))
        return ARCHIVE_FATAL;
    return ftello64(archive->fp);
}

static la_int64_t
archive_skip_cb(UNUSED(struct archive *a), void *priv, la_int64_t request)
{
    const media_archive_t *archive = (const media_archive_t *) priv;

    if (fseeko64(archive->fp, request, SEEK_CUR))
        return 0;
    return request;
}

/* Code page 437, 80h to FFh. */
static const uint16_t cp437[128] = {
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, 0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
    0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba, 0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f, 0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b, 0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
    0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4, 0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248, 0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0
};

uint8_t
media_archive_cp437_encode(uint32_t c)
{
    if (c < 0x80)
        return c;
    for (int i = 0; i < 128; i++) {
        if (cp437[i] == c)
            return 0x80 + i;
    }
    return 0;
}

/* Names not flagged as UTF-8 are read as code page 437, but archivers on Unix and
   macOS store UTF-8 names without the flag. Returns the name as UTF-8 if its code
   page 437 bytes are UTF-8, or NULL to keep it. */
static char *
oem_name_as_utf8(const char *name)
{
    uint8_t *bytes = (uint8_t *) malloc(strlen(name) + 1);
    size_t   len   = 0;

    if (!bytes)
        return NULL;
    while (*name) {
        uint32_t c = fs_utf8_next(&name);
        uint8_t  b = media_archive_cp437_encode(c);
        if (!b) {
            /* Not from code page 437, so it was flagged as UTF-8. */
            free(bytes);
            return NULL;
        }
        bytes[len++] = b;
    }
    bytes[len] = '\0';
    if (fs_utf8_check((const char *) bytes) <= 0) {
        free(bytes);
        return NULL;
    }
    return (char *) bytes;
}

/* Formats whose names are in the DOS code page unless flagged as UTF-8. */
static int
has_oem_names(struct archive *a)
{
    switch (archive_format(a) & ARCHIVE_FORMAT_BASE_MASK) {
        case ARCHIVE_FORMAT_ZIP:
        case ARCHIVE_FORMAT_LHA:
        case ARCHIVE_FORMAT_CAB:
        case ARCHIVE_FORMAT_RAR:
            return 1;
        default:
            return 0;
    }
}

static struct archive *
archive_start(media_archive_t *archive)
{
    struct archive *a = archive_read_new();
    if (!a)
        return NULL;

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    /* Names not flagged as UTF-8 are in code page 437, as the ZIP specification says and DOS
       archivers wrote them. Otherwise they would be read in the host's locale, which is the
       C locale on Windows. */
    archive_read_set_format_option(a, "zip", "hdrcharset", "CP437");
    archive_read_set_format_option(a, "lha", "hdrcharset", "CP437");
    archive_read_set_format_option(a, "cab", "hdrcharset", "CP437");
    archive_read_set_format_option(a, "rar", "hdrcharset", "CP437");
    archive_read_set_read_callback(a, archive_read_cb);
    archive_read_set_seek_callback(a, archive_seek_cb);
    archive_read_set_skip_callback(a, archive_skip_cb);
    archive_read_set_callback_data(a, archive);

    rewind(archive->fp);
    if (archive_read_open1(a) != ARCHIVE_OK) {
        archive_read_free(a);
        return NULL;
    }
    return a;
}

static media_archive_entry_t *
entry_add(media_archive_t *archive, media_archive_entry_t *parent, const char *name, size_t length)
{
    media_archive_entry_t *entry = (media_archive_entry_t *) calloc(1, sizeof(media_archive_entry_t) + length + 1);
    if (!entry)
        return NULL;

    memcpy(entry->name, name, length);
    entry->parent = parent;
    entry->is_dir = 1;
    if (parent) {
        entry->next      = parent->children;
        parent->children = entry;
    }
    entry->all_next = archive->all;
    archive->all    = entry;
    archive->count++;
    return entry;
}

static int
entry_compare(const void *a, const void *b)
{
    return strcmp((*(const media_archive_entry_t *const *) a)->name, (*(const media_archive_entry_t *const *) b)->name);
}

static int
entry_sort(media_archive_entry_t *dir)
{
    size_t count = 0;
    for (const media_archive_entry_t *entry = dir->children; entry; entry = entry->next)
        count++;
    if (count > 1) {
        media_archive_entry_t **list = (media_archive_entry_t **) malloc(count * sizeof(media_archive_entry_t *));
        if (!list)
            return 0;
        count = 0;
        for (media_archive_entry_t *entry = dir->children; entry; entry = entry->next)
            list[count++] = entry;
        qsort(list, count, sizeof(media_archive_entry_t *), entry_compare);
        for (size_t i = 0; i < count; i++)
            list[i]->next = (i + 1 < count) ? list[i + 1] : NULL;
        dir->children = list[0];
        free(list);
    }
    for (media_archive_entry_t *entry = dir->children; entry; entry = entry->next) {
        if (entry->is_dir && !entry_sort(entry))
            return 0;
    }
    return 1;
}

/* Adds the entry named by a path to the tree. Returns NULL if the path is unusable. */
static media_archive_entry_t *
entry_from_path(media_archive_t *archive, const char *path, int is_dir, int *error)
{
    media_archive_entry_t *parent = archive->root;
    media_archive_entry_t *entry  = NULL;

    while (*path) {
        /* DOS and Windows tools may write backslashes. */
        size_t length = strcspn(path, "/\\");
        if (!length || ((length == 1) && (path[0] == '.'))) {
            path += length + !!path[length];
            continue;
        }
        if ((length == 2) && (path[0] == '.') && (path[1] == '.'))
            return NULL;

        int       last = !path[length] || !path[length + strspn(path + length, "/\\")];
        for (entry = parent->children; entry; entry = entry->next) {
            if (!strncmp(entry->name, path, length) && !entry->name[length])
                break;
        }
        if (entry) {
            /* A file and a directory can't share a name; the later entry wins. */
            if ((!last || is_dir) && !entry->is_dir)
                return NULL;
            if (last && !is_dir && entry->is_dir)
                return NULL;
        } else {
            entry = entry_add(archive, parent, path, length);
            if (!entry) {
                *error = 1;
                return NULL;
            }
            entry->is_dir = !last || is_dir;
        }

        parent = entry;
        path += length;
        path += strspn(path, "/\\");
    }
    return entry;
}

/* libarchive converts names through the current locale, and GUI processes (such as
   on macOS) may run in the C locale, which can't represent non-ASCII names. Switch
   this thread to a UTF-8 locale while listing. */
#ifndef _WIN32
static locale_t
utf8_locale_enter(locale_t *old)
{
    locale_t utf8 = (locale_t) 0;

    *old = (locale_t) 0;
    if (strcmp(nl_langinfo(CODESET), "UTF-8")) {
        utf8 = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t) 0);
        if (!utf8)
            utf8 = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", (locale_t) 0);
        if (utf8)
            *old = uselocale(utf8);
    }
    return utf8;
}

static void
utf8_locale_leave(locale_t utf8, locale_t old)
{
    if (utf8) {
        uselocale(old);
        freelocale(utf8);
    }
}
#endif

static media_archive_t *media_archive_list(const char *path);

media_archive_t *
media_archive_open(const char *path)
{
#ifndef _WIN32
    locale_t         old;
    locale_t         utf8    = utf8_locale_enter(&old);
    media_archive_t *archive = media_archive_list(path);
    utf8_locale_leave(utf8, old);
    return archive;
#else
    return media_archive_list(path);
#endif
}

static media_archive_t *
media_archive_list(const char *path)
{
    media_archive_t *archive = (media_archive_t *) calloc(1, sizeof(media_archive_t));
    struct archive  *a       = NULL;
    int              error   = 1;

    if (!archive)
        return NULL;
    archive->block = (uint8_t *) malloc(BLOCK_SIZE);
    archive->fp    = plat_fopen64(path, "rb");
    if (!archive->block || !archive->fp)
        goto end;
    archive->root = entry_add(archive, NULL, "", 0);
    if (!archive->root)
        goto end;

    a = archive_start(archive);
    if (!a)
        goto end;

    size_t                 index_size = 0;
    struct archive_entry  *header;
    int                    ret;
    while ((ret = archive_read_next_header(a, &header)) == ARCHIVE_OK || (ret == ARCHIVE_WARN)) {
        uint32_t       index = archive->headers++;
        if (index >= index_size) {
            index_size                     = index_size ? (index_size * 2) : 256;
            media_archive_entry_t **by_index = (media_archive_entry_t **) realloc(archive->by_index, index_size * sizeof(media_archive_entry_t *));
            if (!by_index)
                goto end;
            archive->by_index = by_index;
        }
        archive->by_index[index] = NULL;

        unsigned int       type = archive_entry_filetype(header);
        if (((type != AE_IFREG) && (type != AE_IFDIR)) || archive_entry_hardlink(header))
            continue;

        const char *name = archive_entry_pathname_utf8(header);
        if (!name)
            name = archive_entry_pathname(header);
        if (!name)
            continue;
        char *recoded = has_oem_names(a) ? oem_name_as_utf8(name) : NULL;

        int                    alloc_error = 0;
        media_archive_entry_t *entry       = entry_from_path(archive, recoded ? recoded : name, type == AE_IFDIR, &alloc_error);
        free(recoded);
        if (alloc_error)
            goto end;
        if (!entry || (entry == archive->root))
            continue;

        /* A later header for the same path replaces the earlier one. */
        if (entry->has_header && !entry->is_dir)
            archive->by_index[entry->index] = NULL;
        entry->has_header = 1;
        entry->mtime      = archive_entry_mtime_is_set(header) ? archive_entry_mtime(header) : 0;
        entry->dos_attr   = (archive_entry_perm(header) & 0200) ? 0x00 : 0x01;
        if (type == AE_IFDIR)
            continue;

        entry->index = index;
        if (archive_entry_size_is_set(header)) {
            entry->size = (uint64_t) archive_entry_size(header);
        } else {
            /* Streamed entries may not record their size; measure it. */
            la_ssize_t read;
            entry->size = 0;
            while ((read = archive_read_data(a, archive->block, BLOCK_SIZE)) > 0)
                entry->size += read;
            if (read < 0)
                goto end;
        }
        archive->by_index[index] = entry;
    }
    if (ret != ARCHIVE_EOF)
        goto end;

    error = !entry_sort(archive->root);

end:
    if (a)
        archive_read_free(a);
    if (error) {
        media_archive_close(archive);
        return NULL;
    }
    return archive;
}

void
media_archive_close(media_archive_t *archive)
{
    if (!archive)
        return;

    while (archive->all) {
        media_archive_entry_t *entry = archive->all;
        archive->all                 = entry->all_next;
        free(entry);
    }
    if (archive->data)
        fclose(archive->data);
    if (archive->fp)
        fclose(archive->fp);
    free(archive->by_index);
    free(archive->block);
    free(archive);
}

media_archive_entry_t *
media_archive_root(media_archive_t *archive)
{
    return archive->root;
}

uint32_t
media_archive_count(media_archive_t *archive)
{
    return archive->count;
}

int
media_archive_extract(media_archive_t *archive, media_archive_data_t callback, void *opaque)
{
    struct archive *a = archive_start(archive);
    if (!a)
        return 0;

    struct archive_entry *header;
    uint32_t              index = 0;
    int                   ret;
    int                   success = 0;
    while ((ret = archive_read_next_header(a, &header)) == ARCHIVE_OK || (ret == ARCHIVE_WARN)) {
        if (index >= archive->headers)
            goto end;
        media_archive_entry_t *entry = archive->by_index[index++];
        if (!entry)
            continue;

        const void *data;
        size_t      size;
        la_int64_t  offset;
        while ((ret = archive_read_data_block(a, &data, &size, &offset)) == ARCHIVE_OK) {
            /* Ignore data past the size that the listing found. */
            if ((uint64_t) offset >= entry->size)
                continue;
            if (size > (entry->size - offset))
                size = entry->size - offset;
            if (!callback(opaque, entry, offset, data, size))
                goto end;
        }
        if (ret != ARCHIVE_EOF)
            goto end;
    }
    success = (ret == ARCHIVE_EOF) && (index == archive->headers);

end:
    archive_read_free(a);
    return success;
}

static int
extract_all_cb(void *opaque, media_archive_entry_t *entry, uint64_t offset, const void *data, size_t size)
{
    FILE *fp = (FILE *) opaque;

    return !fseeko64(fp, entry->offset + offset, SEEK_SET) && (fwrite(data, 1, size, fp) == size);
}

int
media_archive_extract_all(media_archive_t *archive)
{
    if (archive->data)
        return 1;

    /* Lay the files out in archive order. */
    uint64_t offset = 0;
    for (uint32_t i = 0; i < archive->headers; i++) {
        media_archive_entry_t *entry = archive->by_index[i];
        if (entry) {
            entry->offset = offset;
            offset += entry->size;
        }
    }

    archive->data = media_temp_file("archive");
    if (!archive->data)
        return 0;
    if (!media_archive_extract(archive, extract_all_cb, archive->data) || fflush(archive->data)) {
        fclose(archive->data);
        archive->data = NULL;
        return 0;
    }
    return 1;
}

int
media_archive_read(media_archive_t *archive, const media_archive_entry_t *entry,
                   uint64_t offset, void *buffer, size_t count)
{
    if (!archive->data || (offset > entry->size) || (count > (entry->size - offset)))
        return 0;
    if (fseeko64(archive->data, entry->offset + offset, SEEK_SET))
        return 0;

    /* Data that an archive did not hold reads as zeros. */
    size_t       read = fread(buffer, 1, count, archive->data);
    memset((uint8_t *) buffer + read, 0x00, count - read);
    return !ferror(archive->data);
}
