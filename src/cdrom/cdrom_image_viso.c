/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Virtual ISO CD-ROM image back-end.
 *
 * Authors: RichardG <richardg867@gmail.com>
 *
 *          Copyright 2022 RichardG.
 */
// clang-format off
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define __STDC_FORMAT_MACROS
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/bswap.h>
#include <86box/cdrom_image_backend.h>
#include <86box/plat.h>
#include <86box/plat_dir.h>
#include <86box/version.h>
#include <86box/timer.h>
#include <86box/nvr.h>
// clang-format on

#define VISO_SKIP(p, n) \
    memset(p, 0x00, n); \
    p += n;

/* ISO 9660 defines "both endian" data formats, which are
   stored as little endian followed by big endian. */
#define VISO_LBE_16(p, x)               \
    *((uint16_t *) p) = cpu_to_le16(x); \
    p += 2;                             \
    *((uint16_t *) p) = cpu_to_be16(x); \
    p += 2;
#define VISO_LBE_32(p, x)               \
    *((uint32_t *) p) = cpu_to_le32(x); \
    p += 4;                             \
    *((uint32_t *) p) = cpu_to_be32(x); \
    p += 4;

#define VISO_SECTOR_SIZE COOKED_SECTOR_SIZE
#define VISO_OPEN_FILES  32

enum {
    VISO_CHARSET_D = 0,
    VISO_CHARSET_A,
    VISO_CHARSET_FN,
    VISO_CHARSET_ANY
};

enum {
    VISO_DIR_CURRENT = 0,
    VISO_DIR_PARENT  = 1,
    VISO_DIR_REGULAR,
    VISO_DIR_JOLIET
};

typedef struct _viso_entry_ {
    char *path, name_short[13], name_rr[256];
    union { /* save some memory */
        uint64_t pt_offsets[4];
        FILE    *file;
    };
    union {
        uint64_t dr_offsets[2];
        uint64_t data_offset;
    };
    uint16_t name_joliet[111], pt_idx; /* name_joliet size limited by maximum directory record size */
    uint8_t  name_joliet_len;

    struct stat stats;

    struct _viso_entry_ *parent, *next, *next_dir, *first_child;
} viso_entry_t;

typedef struct {
    uint64_t     vol_size_offsets[2], pt_meta_offsets[2], eltorito_offset;
    uint32_t     metadata_sectors, all_sectors, entry_map_size;
    unsigned int sector_size, file_fifo_pos;
    uint8_t     *metadata;

    track_file_t tf;
    viso_entry_t root_dir, *eltorito_entry, **entry_map, *file_fifo[VISO_OPEN_FILES];
} viso_t;

#define ENABLE_CDROM_IMAGE_VISO_LOG 1
#ifdef ENABLE_CDROM_IMAGE_VISO_LOG
int cdrom_image_viso_do_log = ENABLE_CDROM_IMAGE_VISO_LOG;

void
cdrom_image_viso_log(const char *fmt, ...)
{
    va_list ap;

    if (cdrom_image_viso_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cdrom_image_viso_log(fmt, ...)
#endif

static size_t
viso_pread(void *ptr, uint64_t offset, size_t size, size_t count, FILE *stream)
{
    uint64_t cur_pos = ftello64(stream);
    size_t   ret     = 0;
    if (fseeko64(stream, offset, SEEK_SET) != -1)
        ret = fread(ptr, size, count, stream);
    fseeko64(stream, cur_pos, SEEK_SET);
    return ret;
}

static size_t
viso_pwrite(const void *ptr, uint64_t offset, size_t size, size_t count, FILE *stream)
{
    uint64_t cur_pos = ftello64(stream);
    size_t   ret     = 0;
    if (fseeko64(stream, offset, SEEK_SET) != -1)
        ret = fwrite(ptr, size, count, stream);
    fseeko64(stream, cur_pos, SEEK_SET);
    return ret;
}

#define VISO_WRITE_STR_FUNC(n, t, st, cnv)                                           \
    static void                                                                      \
    n(t *dest, const st *src, int buf_size, int charset)                             \
    {                                                                                \
        while (*src && (buf_size-- > 0)) {                                           \
            switch (*src) {                                                          \
                case 'A' ... 'Z':                                                    \
                case '0' ... '9':                                                    \
                case '_':                                                            \
                    /* Valid on all sets. */                                         \
                    *dest = *src;                                                    \
                    break;                                                           \
                                                                                     \
                case 'a' ... 'z':                                                    \
                    /* Convert to uppercase on A and D. */                           \
                    if (charset > VISO_CHARSET_A)                                    \
                        *dest = *src;                                                \
                    else                                                             \
                        *dest = *src - 32;                                           \
                    break;                                                           \
                                                                                     \
                case ' ':                                                            \
                case '!':                                                            \
                case '"':                                                            \
                case '%':                                                            \
                case '&':                                                            \
                case '(':                                                            \
                case ')':                                                            \
                case '+':                                                            \
                case ',':                                                            \
                case '-':                                                            \
                case '.':                                                            \
                case '<':                                                            \
                case '=':                                                            \
                case '>':                                                            \
                    /* Valid for A and filenames but not for D. */                   \
                    if (charset >= VISO_CHARSET_A)                                   \
                        *dest = *src;                                                \
                    else                                                             \
                        *dest = '_';                                                 \
                    break;                                                           \
                                                                                     \
                case '*':                                                            \
                case '/':                                                            \
                case ':':                                                            \
                case ';':                                                            \
                case '?':                                                            \
                case '\'':                                                           \
                    /* Valid for A but not for filenames or D. */                    \
                    if ((charset >= VISO_CHARSET_A) && (charset != VISO_CHARSET_FN)) \
                        *dest = *src;                                                \
                    else                                                             \
                        *dest = '_';                                                 \
                    break;                                                           \
                                                                                     \
                case 0x00 ... 0x1f:                                                  \
                    /* Not valid for A, D or filenames. */                           \
                    if (charset > VISO_CHARSET_FN)                                   \
                        *dest = *src;                                                \
                    else                                                             \
                        *dest = '_';                                                 \
                                                                                     \
                default:                                                             \
                    /* Not valid for A or D, but valid for filenames. */             \
                    if ((charset >= VISO_CHARSET_FN) && (*src <= 0xffff))            \
                        *dest = *src;                                                \
                    else                                                             \
                        *dest = '_';                                                 \
            }                                                                        \
                                                                                     \
            *dest = cnv(*dest);                                                      \
                                                                                     \
            dest++;                                                                  \
            src++;                                                                   \
        }                                                                            \
                                                                                     \
        /* Apply space padding. */                                                   \
        while (buf_size-- > 0)                                                       \
            *dest++ = cnv(' ');                                                      \
    }
VISO_WRITE_STR_FUNC(viso_write_string, uint8_t, char, )
VISO_WRITE_STR_FUNC(viso_write_wstring, uint16_t, wchar_t, cpu_to_be16)

static int
viso_get_short_filename(viso_entry_t *dir, char *dest, const char *src)
{
    /* Get name and extension length. */
    const char *ext_pos = strrchr(src, '.');
    int         name_len, ext_len;
    if (ext_pos) {
        name_len = ext_pos - src;
        ext_len  = strlen(ext_pos);
    } else {
        name_len = strlen(src);
        ext_len  = 0;
    }

    /* Copy name. */
    int name_copy_len = MIN(8, name_len);
    viso_write_string((uint8_t *) dest, src, name_copy_len, VISO_CHARSET_D);
    dest[name_copy_len] = 0;

    /* Copy extension to temporary buffer. */
    char ext[5]     = { 0 };
    int  force_tail = (name_len > 8) || (ext_len == 1);
    if (ext_len > 1) {
        ext[0] = '.';
        if (ext_len > 4)
            force_tail = 1;
        viso_write_string((uint8_t *) &ext[1], &ext_pos[1], MIN(ext_len, 4) - 1, VISO_CHARSET_D);
    }

    /* Check if this filename is unique, and add a tail if required, while also adding the extension. */
    char tail[8];
    for (int i = force_tail; i <= 999999; i++) {
        /* Add tail to the filename if this is not the first run. */
        int tail_len = -1;
        if (i) {
            tail_len = sprintf(tail, "~%d", i);
            strcpy(&dest[MIN(name_copy_len, 8 - tail_len)], tail);
        }

        /* Add extension to the filename if present. */
        if (ext[0])
            strcat(dest, ext);

        /* Go through files in this directory to make sure this filename is unique. */
        viso_entry_t *entry = dir->first_child;
        while (entry) {
            /* Flag and stop if this filename was seen. */
            if ((entry->name_short != dest) && !strcmp(dest, entry->name_short)) {
                tail_len = 0;
                break;
            }

            /* Move on to the next entry, and stop if the end of this directory was reached. */
            entry = entry->next;
            if (entry && (entry->parent != dir))
                break;
        }

        /* Stop if this is an unique name. */
        if (tail_len)
            return 0;
    }
    return 1;
}

static void
viso_fill_dir_record(viso_entry_t *entry, uint8_t *data, int type)
{
    uint8_t *p = data, *q;

    *p++ = 0;                             /* size (filled in later) */
    *p++ = 0;                             /* extended attribute length */
    VISO_SKIP(p, 8);                      /* sector offset */
    VISO_LBE_32(p, entry->stats.st_size); /* size (filled in later if this is a directory) */

    time_t     secs   = entry->stats.st_mtime;
    struct tm *time_s = gmtime(&secs);      /* time, use UTC as timezones are not portable */
    *p++              = time_s->tm_year;    /* year since 1900 */
    *p++              = 1 + time_s->tm_mon; /* month */
    *p++              = time_s->tm_mday;    /* day */
    *p++              = time_s->tm_hour;    /* hour */
    *p++              = time_s->tm_min;     /* minute */
    *p++              = time_s->tm_sec;     /* second */
    *p++              = 0;                  /* timezone */

    *p++ = S_ISDIR(entry->stats.st_mode) ? 0x02 : 0x00; /* flags */

    VISO_SKIP(p, 2);   /* interleave unit/gap size */
    VISO_LBE_16(p, 1); /* volume sequence number */

    switch (type) {
        case VISO_DIR_CURRENT:
        case VISO_DIR_PARENT:
            *p++ = 1;                                  /* file ID length */
            *p++ = (type == VISO_DIR_CURRENT) ? 0 : 1; /* magic value corresponding to . or .. */
            break;

        case VISO_DIR_REGULAR:
            q = p++; /* save location of the file ID length for later */

            *q = strlen(entry->name_short);
            memcpy(p, entry->name_short, *q); /* file ID */
            p += *q;
            if (!S_ISDIR(entry->stats.st_mode)) {
                memcpy(p, ";1", 2); /* version suffix for files */
                p += 2;
                *q += 2;
            }

            if (!((*q) & 1)) /* padding for even file ID lengths */
                *p++ = 0;

            *p++ = 'R'; /* RR = present Rock Ridge entries (only documented by RRIP revision 1.09!) */
            *p++ = 'R';
            *p++ = 5; /* length */
            *p++ = 1; /* version */

            q = p++; /* save location of Rock Ridge flags for later */

            if (strcmp(entry->name_short, entry->name_rr)) {
                *q |= 0x08; /* NM = alternate name */
                *p++ = 'N';
                *p++ = 'M';
                *p++ = 5 + MIN(128, strlen(entry->name_rr)); /* length */
                *p++ = 1;                                    /* version */

                *p++ = 0;                                /* flags */
                memcpy(p, entry->name_rr, *(p - 3) - 5); /* name */
                p += *(p - 3) - 5;
            }

            *q |= 0x01; /* PX = POSIX attributes */
            *p++ = 'P';
            *p++ = 'X';
            *p++ = 36; /* length */
            *p++ = 1;  /* version */

            VISO_LBE_32(p, entry->stats.st_mode);  /* mode */
            VISO_LBE_32(p, entry->stats.st_nlink); /* number of links */
            VISO_LBE_32(p, entry->stats.st_uid);   /* owner UID */
            VISO_LBE_32(p, entry->stats.st_gid);   /* owner GID */

#if defined(S_ISCHR) || defined(S_ISBLK)
#    if defined(S_ISCHR) && defined(S_ISBLK)
            if (S_ISCHR(entry->stats.st_mode) || S_ISBLK(entry->stats.st_mode))
#    elif defined(S_ISCHR)
            if (S_ISCHR(entry->stats.st_mode))
#    else
            if (S_ISBLK(entry->stats.st_mode))
#    endif
            {
                *q |= 0x02; /* PN = POSIX device */
                *p++ = 'P';
                *p++ = 'N';
                *p++ = 20; /* length */
                *p++ = 1;  /* version */

                VISO_LBE_32(p, 0);                    /* device high 32 bits */
                VISO_LBE_32(p, entry->stats.st_rdev); /* device low 32 bits */
            }
#endif
#ifdef S_ISLNK
            if (S_ISLNK(entry->stats.st_mode)) { /* TODO: rather complex path splitting system */
                *q |= 0x04;                      /* SL = symlink */
                *p++ = 'S';
                *p++ = 'L';
                *p++ = 5; /* length */
                *p++ = 1; /* version */

                *p++ = 0; /* flags */
            }
#endif
            if (entry->stats.st_atime || entry->stats.st_mtime || entry->stats.st_ctime) {
                *q |= 0x80; /* TF = timestamps */
                *p++ = 'T';
                *p++ = 'F';
                *p++ = 29; /* length */
                *p++ = 1;  /* version */

                *p++ = 0x0e;                           /* flags: modified | access | attributes */
                VISO_LBE_32(p, entry->stats.st_mtime); /* modified */
                VISO_LBE_32(p, entry->stats.st_atime); /* access */
                VISO_LBE_32(p, entry->stats.st_ctime); /* attributes */
            }

            if ((p - data) & 1) /* padding for odd Rock Ridge section lengths */
                *p++ = 0;
            break;

        case VISO_DIR_JOLIET:
            q = p++; /* save location of the file ID length for later */

            *q = entry->name_joliet_len * sizeof(entry->name_joliet[0]);
            memcpy(p, entry->name_joliet, *q); /* file ID */
            p += *q;

            if (!((*q) & 1)) /* padding for even file ID lengths */
                *p++ = 0;
            break;
    }

    data[0] = p - data; /* length */
}

int
viso_read(void *p, uint8_t *buffer, uint64_t seek, size_t count)
{
    track_file_t *tf   = (track_file_t *) p;
    viso_t       *viso = (viso_t *) tf->priv;

    /* Handle reads in a sector by sector basis. */
    while (count > 0) {
        /* Determine the current sector, offset and remainder. */
        uint32_t sector        = seek / viso->sector_size,
                 sector_offset = seek % viso->sector_size,
                 sector_remain = MIN(count, viso->sector_size - sector_offset);

        /* Handle sector. */
        if (sector < viso->metadata_sectors) {
            /* Copy metadata. */
            memcpy(buffer, viso->metadata + seek, sector_remain);
        } else {
            size_t read = 0;

            /* Get the file entry corresponding to this sector. */
            viso_entry_t *entry = viso->entry_map[sector - viso->metadata_sectors];
            if (entry) {
                /* Open file if it's not already open. */
                if (!entry->file) {
                    /* Close any existing FIFO entry's file. */
                    viso_entry_t *other_entry = viso->file_fifo[viso->file_fifo_pos];
                    if (other_entry && other_entry->file) {
                        cdrom_image_viso_log("VISO: Closing [%s]", other_entry->path);
                        fclose(other_entry->file);
                        other_entry->file = NULL;
                        cdrom_image_viso_log("\n");
                    }

                    /* Open file. */
                    cdrom_image_viso_log("VISO: Opening [%s]", entry->path);
                    if ((entry->file = fopen(entry->path, "rb"))) {
                        cdrom_image_viso_log("\n");

                        /* Add this entry to the FIFO. */
                        viso->file_fifo[viso->file_fifo_pos++] = entry;
                        viso->file_fifo_pos &= (sizeof(viso->file_fifo) / sizeof(viso->file_fifo[0])) - 1;
                    } else {
                        cdrom_image_viso_log(" => failed\n");

                        /* Clear any existing FIFO entry. */
                        viso->file_fifo[viso->file_fifo_pos] = NULL;
                    }
                }

                /* Read data. */
                if (entry->file && (fseeko64(entry->file, seek - entry->data_offset, SEEK_SET) != -1))
                    read = fread(buffer, 1, sector_remain, entry->file);
            }

            /* Fill remainder with 00 bytes if needed. */
            if (read < sector_remain)
                memset(buffer + read, 0x00, sector_remain - read);
        }

        /* Move on to the next sector. */
        buffer += sector_remain;
        seek += sector_remain;
        count -= sector_remain;
    }

    return 1;
}

uint64_t
viso_get_length(void *p)
{
    track_file_t *tf   = (track_file_t *) p;
    viso_t       *viso = (viso_t *) tf->priv;
    return ((uint64_t) viso->all_sectors) * viso->sector_size;
}

void
viso_close(void *p)
{
    track_file_t *tf   = (track_file_t *) p;
    viso_t       *viso = (viso_t *) tf->priv;

    if (viso == NULL)
        return;

    cdrom_image_viso_log("VISO: close()\n");

    /* De-allocate everything. */
    if (tf->file)
        fclose(tf->file);

    viso_entry_t *entry = &viso->root_dir, *next_entry;
    while (entry) {
        if (entry->path)
            free(entry->path);
        if (entry->file)
            fclose(entry->file);
        next_entry = entry->next;
        if (entry != &viso->root_dir)
            free(entry);
        entry = next_entry;
    }

    if (viso->metadata)
        free(viso->metadata);
    if (viso->entry_map)
        free(viso->entry_map);

    free(viso);
}

track_file_t *
viso_init(const char *dirname, int *error)
{
    cdrom_image_viso_log("VISO: init()\n");

    /* Initialize our data structure. */
    viso_t  *viso  = (viso_t *) calloc(1, sizeof(viso_t));
    uint8_t *data  = NULL, *p;
    wchar_t *wtemp = NULL;
    *error         = 1;
    if (viso == NULL)
        goto end;
    viso->sector_size = VISO_SECTOR_SIZE;

    /* Prepare temporary data buffers. */
    data          = calloc(2, viso->sector_size);
    int wtemp_len = MIN(64, sizeof(viso->root_dir.name_joliet) / sizeof(viso->root_dir.name_joliet[0])) + 1;
    wtemp         = malloc(wtemp_len * sizeof(wchar_t));
    if (!data || !wtemp)
        goto end;

        /* Open temporary file. */
#ifdef ENABLE_CDROM_IMAGE_VISO_LOG
    strcpy(viso->tf.fn, "viso-debug.iso");
#else
    plat_tempfile(viso->tf.fn, "viso", ".tmp");
#endif
    viso->tf.file = plat_fopen64(nvr_path(viso->tf.fn), "w+b");
    if (!viso->tf.file)
        goto end;

    /* Set up directory traversal. */
    cdrom_image_viso_log("VISO: Traversing directories:\n");
    viso_entry_t  *dir = &viso->root_dir, *last_dir = dir, *last_entry = dir;
    struct dirent *readdir_entry;
    int            max_len, len, name_len;
    char          *path;

    /* Fill root directory entry. */
    dir->path = (char *) malloc(strlen(dirname) + 1);
    if (!dir->path)
        goto end;
    strcpy(dir->path, dirname);
    stat(dirname, &dir->stats);
    if (!S_ISDIR(dir->stats.st_mode))
        goto end;
    dir->parent = dir; /* for path table filling */
    cdrom_image_viso_log("[%08X] %s => [root]\n", dir, dir->path);

    /* Traverse directories, starting with the root. */
    while (dir) {
        /* Open directory for listing. */
        DIR *dirp = opendir(dir->path);
        if (!dirp)
            goto next_dir;

        /* Add . and .. pseudo-directories. */
        for (int i = 0; i < 2; i++) {
            last_entry->next = (viso_entry_t *) calloc(1, sizeof(viso_entry_t));
            if (!last_entry->next)
                goto end;
            last_entry         = last_entry->next;
            last_entry->parent = dir;
            if (!i)
                dir->first_child = last_entry;

            /* Stat the current directory or parent directory. */
            stat(i ? dir->parent->path : dir->path, &last_entry->stats);

            /* Set short and long filenames. */
            strcpy(last_entry->name_short, i ? ".." : ".");
            strcpy(last_entry->name_rr, i ? ".." : ".");
            wcscpy(last_entry->name_joliet, i ? L".." : L".");

            cdrom_image_viso_log("[%08X] %s => %s\n", last_entry, dir->path, last_entry->name_short);
        }

        /* Iterate through this directory's children. */
        size_t dir_path_len = strlen(dir->path);
        while ((readdir_entry = readdir(dirp))) {
            /* Ignore . and .. pseudo-directories. */
            if (readdir_entry->d_name[0] == '.' && (readdir_entry->d_name[1] == '\0' || (readdir_entry->d_name[1] == '.' && readdir_entry->d_name[2] == '\0')))
                continue;

            /* Save full file path. */
            name_len = strlen(readdir_entry->d_name);
            path     = (char *) malloc(dir_path_len + name_len + 2);
            if (!path)
                goto end;
            strcpy(path, dir->path);
            plat_path_slash(path);
            strcat(path, readdir_entry->d_name);

            /* Add and fill entry. */
            last_entry->next = (viso_entry_t *) calloc(1, sizeof(viso_entry_t));
            if (!last_entry->next) {
                free(path);
                goto end;
            }
            last_entry         = last_entry->next;
            last_entry->path   = path;
            last_entry->parent = dir;

            /* Stat this child. */
            if (stat(path, &last_entry->stats) != 0) {
                /* Use a blank structure if stat failed. */
                memset(&last_entry->stats, 0x00, sizeof(struct stat));
            }

            /* Handle file size. */
            if (!S_ISDIR(last_entry->stats.st_mode)) {
                /* Limit to 4 GB - 1 byte. */
                if (last_entry->stats.st_size > ((uint32_t) -1))
                    last_entry->stats.st_size = (uint32_t) -1;

                /* Increase entry map size. */
                viso->entry_map_size += last_entry->stats.st_size / viso->sector_size;
                if (last_entry->stats.st_size % viso->sector_size)
                    viso->entry_map_size++; /* round up to the next sector */
            }

            /* Detect El Torito boot code file and set it accordingly. */
            if (!strnicmp(readdir_entry->d_name, "eltorito.", 9) && (!stricmp(readdir_entry->d_name + 9, "com") || !stricmp(readdir_entry->d_name + 9, "img")))
                viso->eltorito_entry = last_entry;

            /* Set short filename. */
            if (viso_get_short_filename(dir, last_entry->name_short, readdir_entry->d_name))
                goto end;

            /* Set Rock Ridge long filename. */
            len = MIN(name_len, sizeof(last_entry->name_rr) - 1);
            viso_write_string((uint8_t *) last_entry->name_rr, readdir_entry->d_name, len, VISO_CHARSET_FN);
            last_entry->name_rr[len] = '\0';

            /* Set Joliet long filename. */
            if (wtemp_len < (name_len + 1)) { /* grow wchar buffer if needed */
                wtemp_len = name_len + 1;
                wtemp     = realloc(wtemp, wtemp_len * sizeof(wchar_t));
            }
            max_len = (sizeof(last_entry->name_joliet) / sizeof(last_entry->name_joliet[0])) - 1;
            len     = mbstowcs(wtemp, readdir_entry->d_name, wtemp_len - 1);
            if (len > max_len) {
                /* Relocate extension if this is a file whose name exceeds the maximum length. */
                if (!S_ISDIR(last_entry->stats.st_mode)) {
                    wchar_t *wext = wcsrchr(wtemp, L'.');
                    if (wext) {
                        len = wcslen(wext);
                        memmove(wtemp + (max_len - len), wext, len * sizeof(wchar_t));
                    }
                }
                len = max_len;
            }
            viso_write_wstring(last_entry->name_joliet, wtemp, len, VISO_CHARSET_FN);
            last_entry->name_joliet[len] = '\0';
            last_entry->name_joliet_len  = len;

            cdrom_image_viso_log("[%08X] %s => [%-12s] %s\n", last_entry, dir->path, last_entry->name_short, last_entry->name_rr);

            /* If this is a directory, add it to the traversal list. */
            if (S_ISDIR(last_entry->stats.st_mode)) {
                last_dir->next_dir    = last_entry;
                last_dir              = last_entry;
                last_dir->first_child = NULL;
            }
        }

next_dir:
        /* Move on to the next directory. */
        dir = dir->next_dir;
    }

    /* Write 16 blank sectors. */
    for (int i = 0; i < 16; i++)
        fwrite(data, viso->sector_size, 1, viso->tf.file);

    /* Get current time for the volume descriptors. */
    time_t     secs   = time(NULL);
    struct tm *time_s = gmtime(&secs);

    /* Get root directory basename for the volume ID. */
    char *basename = plat_get_basename(dirname);
    if (!basename || (basename[0] == '\0'))
        basename = EMU_NAME;

    /* Write volume descriptors. */
    for (int i = 0; i < 2; i++) {
        /* Fill volume descriptor. */
        p    = data;
        *p++ = 1 + i;          /* type */
        memcpy(p, "CD001", 5); /* standard ID */
        p += 5;
        *p++ = 1; /* version */
        *p++ = 0; /* unused */

        if (i) {
            viso_write_wstring((uint16_t *) p, EMU_NAME_W, 16, VISO_CHARSET_A); /* system ID */
            p += 32;
            mbstowcs(wtemp, basename, 16);
            viso_write_wstring((uint16_t *) p, wtemp, 16, VISO_CHARSET_D); /* volume ID */
            p += 32;
        } else {
            viso_write_string(p, EMU_NAME, 32, VISO_CHARSET_A); /* system ID */
            p += 32;
            viso_write_string(p, basename, 32, VISO_CHARSET_D); /* volume ID */
            p += 32;
        }

        VISO_SKIP(p, 8); /* unused */

        viso->vol_size_offsets[i] = ftello64(viso->tf.file) + (p - data);
        VISO_LBE_32(p, 0); /* volume space size (filled in later) */

        if (i) {
            *p++ = 0x25; /* escape sequence (indicates our Joliet names are UCS-2 Level 3) */
            *p++ = 0x2f;
            *p++ = 0x45;
            VISO_SKIP(p, 32 - 3); /* unused */
        } else {
            VISO_SKIP(p, 32); /* unused */
        }

        VISO_LBE_16(p, 1);                 /* volume set size */
        VISO_LBE_16(p, 1);                 /* volume sequence number */
        VISO_LBE_16(p, viso->sector_size); /* logical block size */

        /* Path table metadata is filled in later. */
        viso->pt_meta_offsets[i] = ftello64(viso->tf.file) + (p - data);
        VISO_LBE_32(p, 0); /* path table size */
        VISO_LBE_32(p, 0); /* little endian path table and optional path table sector (VISO_LBE_32 is a shortcut to set both) */
        VISO_LBE_32(p, 0); /* big endian path table and optional path table sector (VISO_LBE_32 is a shortcut to set both) */

        viso->root_dir.dr_offsets[i] = ftello64(viso->tf.file) + (p - data);
        viso_fill_dir_record(&viso->root_dir, p, VISO_DIR_CURRENT); /* root directory */
        p += p[0];

        if (i) {
            viso_write_wstring((uint16_t *) p, L"", 64, VISO_CHARSET_D); /* volume set ID */
            p += 128;
            viso_write_wstring((uint16_t *) p, L"", 64, VISO_CHARSET_A); /* publisher ID */
            p += 128;
            viso_write_wstring((uint16_t *) p, L"", 64, VISO_CHARSET_A); /* data preparer ID */
            p += 128;
            swprintf(wtemp, 64, L"%ls %ls VIRTUAL ISO", EMU_NAME_W, EMU_VERSION_W);
            viso_write_wstring((uint16_t *) p, wtemp, 64, VISO_CHARSET_A); /* application ID */
            p += 128;
            viso_write_wstring((uint16_t *) p, L"", 18, VISO_CHARSET_D); /* copyright file ID */
            p += 37;
            viso_write_wstring((uint16_t *) p, L"", 18, VISO_CHARSET_D); /* abstract file ID */
            p += 37;
            viso_write_wstring((uint16_t *) p, L"", 18, VISO_CHARSET_D); /* bibliography file ID */
            p += 37;
        } else {
            viso_write_string(p, "", 128, VISO_CHARSET_D); /* volume set ID */
            p += 128;
            viso_write_string(p, "", 128, VISO_CHARSET_A); /* publisher ID */
            p += 128;
            viso_write_string(p, "", 128, VISO_CHARSET_A); /* data preparer ID */
            p += 128;
            snprintf((char *) p, 128, "%s %s VIRTUAL ISO", EMU_NAME, EMU_VERSION);
            viso_write_string(p, (char *) p, 128, VISO_CHARSET_A); /* application ID */
            p += 128;
            viso_write_string(p, "", 37, VISO_CHARSET_D); /* copyright file ID */
            p += 37;
            viso_write_string(p, "", 37, VISO_CHARSET_D); /* abstract file ID */
            p += 37;
            viso_write_string(p, "", 37, VISO_CHARSET_D); /* bibliography file ID */
            p += 37;
        }

        /* For the created/modified time, the string's NUL
           terminator will act as our timezone offset of 0. */
        sprintf((char *) p, "%04d%02d%02d%02d%02d%02d%02d", /* volume created */
                1900 + time_s->tm_year, 1 + time_s->tm_mon, time_s->tm_mday,
                time_s->tm_hour, time_s->tm_min, time_s->tm_sec, 0);
        strcpy((char *) (p + 17), (char *) p); /* volume modified */
        p += 34;
        VISO_SKIP(p, 34); /* volume expires/effective */

        *p++ = 1; /* file structure version */
        *p++ = 0; /* unused */

        /* Blank the rest of the working sector. */
        memset(p, 0x00, viso->sector_size - (p - data));

        /* Write volume descriptor. */
        fwrite(data, viso->sector_size, 1, viso->tf.file);

        /* Write El Torito boot descriptor. This is an awkward spot for
           that, but the spec requires it to be the second descriptor. */
        if (!i && viso->eltorito_entry) {
            p    = data;
            *p++ = 0;              /* type */
            memcpy(p, "CD001", 5); /* standard ID */
            p += 5;
            *p++ = 1; /* version */

            memcpy(p, "EL TORITO SPECIFICATION", 24); /* identifier */
            p += 24;
            VISO_SKIP(p, 40);

            /* Save the boot catalog pointer's offset for later. */
            viso->eltorito_offset = ftello64(viso->tf.file) + (p - data);

            /* Blank the rest of the working sector. */
            memset(p, 0x00, viso->sector_size - (p - data));

            /* Write boot descriptor. */
            fwrite(data, viso->sector_size, 1, viso->tf.file);
        }
    }

    /* Fill terminator. */
    p    = data;
    *p++ = 0xff;
    memcpy(p, "CD001", 5);
    p += 5;
    *p++ = 0x01;

    /* Blank the rest of the working sector. */
    memset(p, 0x00, viso->sector_size - (p - data));

    /* Write terminator. */
    fwrite(data, viso->sector_size, 1, viso->tf.file);

    /* We start seeing a pattern of padding to even sectors here.
       mkisofs does this, presumably for a very good reason... */
    int write = ftello64(viso->tf.file) % (viso->sector_size * 2);
    if (write) {
        write = (viso->sector_size * 2) - write;
        memset(data, 0x00, write);
        fwrite(data, write, 1, viso->tf.file);
    }

    /* Handle El Torito boot catalog. */
    if (viso->eltorito_entry) {
        /* Write a pointer to this boot catalog to the boot descriptor. */
        *((uint32_t *) data) = ftello64(viso->tf.file) / viso->sector_size;
        viso_pwrite(data, viso->eltorito_offset, 4, 1, viso->tf.file);

        /* Fill boot catalog validation entry. */
        p    = data;
        *p++ = 0x01; /* header ID */
        *p++ = 0x00; /* platform */
        *p++ = 0x00; /* reserved */
        *p++ = 0x00;
        VISO_SKIP(p, 24);
        strncpy((char *) (p - 24), EMU_NAME, 24); /* ID string */
        *p++ = 0x00; /* checksum */
        *p++ = 0x00;
        *p++ = 0x55; /* key bytes */
        *p++ = 0xaa;

        /* Calculate checksum. */
        uint16_t eltorito_checksum = 0;
        for (int i = 0; i < (p - data); i += 2)
            eltorito_checksum -= *((uint16_t *) &data[i]);
        *((uint16_t *) &data[28]) = eltorito_checksum;

        /* Now fill the default boot entry. */
        *p++ = 0x88; /* bootable flag */

        if (viso->eltorito_entry->name_short[9] == 'C') { /* boot media type */
            *p++ = 0x00;
        } else {
            /* This could use with a decoupling of fdd_img's algorithms
               for loading non-raw images and detecting raw image sizes. */
            switch (viso->eltorito_entry->stats.st_size) {
                case 0 ... 1228800: /* 1.2 MB */
                    *p++ = 0x01;
                    break;

                case 1228801 ... 1474560: /* 1.44 MB */
                    *p++ = 0x02;
                    break;

                case 1474561 ... 2949120: /* 2.88 MB */
                    *p++ = 0x03;
                    break;

                default: /* hard drive */
                    *p++ = 0x04;
                    break;
            }
        }

        *p++ = 0x00; /* load segment */
        *p++ = 0x00;
        *p++ = 0x00; /* system type (is this even relevant?) */
        *p++ = 0x00; /* reserved */

        /* Save offsets to the boot catalog entry's offset and size fields for later. */
        viso->eltorito_offset = ftello64(viso->tf.file) + (p - data);

        /* Blank the rest of the working sector. This includes the sector count,
           ISO sector offset and 20-byte selection criteria fields at the end. */
        memset(p, 0x00, viso->sector_size - (p - data));

        /* Write boot catalog. */
        fwrite(data, viso->sector_size, 1, viso->tf.file);

        /* Pad to the next even sector. */
        write = ftello64(viso->tf.file) % (viso->sector_size * 2);
        if (write) {
            write = (viso->sector_size * 2) - write;
            memset(data, 0x00, write);
            fwrite(data, write, 1, viso->tf.file);
        }
    }

    /* Write each path table. */
    for (int i = 0; i < 4; i++) {
        cdrom_image_viso_log("VISO: Generating path table #%d:\n", i);

        /* Save this path table's start offset. */
        uint64_t pt_start = ftello64(viso->tf.file);

        /* Write this table's sector offset to the corresponding volume descriptor. */
        uint32_t pt_temp = pt_start / viso->sector_size;
        if (i & 1)
            *((uint32_t *) data) = cpu_to_be32(pt_temp);
        else
            *((uint32_t *) data) = cpu_to_le32(pt_temp);
        viso_pwrite(data, viso->pt_meta_offsets[i >> 1] + 8 + (8 * (i & 1)), 4, 1, viso->tf.file);

        /* Go through directories. */
        dir             = &viso->root_dir;
        uint16_t pt_idx = 1;
        while (dir) {
            /* Ignore . and .. pseudo-directories. */
            if (dir->name_short[0] == '.' && (dir->name_short[1] == '\0' || (dir->name_short[1] == '.' && dir->name_short[2] == '\0'))) {
                dir = dir->next_dir;
                continue;
            }

            cdrom_image_viso_log("[%08X] %s => %s\n", dir, dir->path, (i & 2) ? dir->name_rr : dir->name_short);

            /* Save this directory's path table index and offset. */
            dir->pt_idx        = pt_idx;
            dir->pt_offsets[i] = ftello64(viso->tf.file);

            /* Fill path table entry. */
            if (dir == &viso->root_dir) /* directory ID length */
                data[0] = 1;
            else if (i & 2)
                data[0] = dir->name_joliet_len * sizeof(dir->name_joliet[0]);
            else
                data[0] = strlen(dir->name_short);

            data[1]                  = 0; /* extended attribute length */
            *((uint32_t *) &data[2]) = 0; /* extent location (filled in later) */
            if (i & 1)                    /* parent directory number */
                *((uint16_t *) &data[6]) = cpu_to_be16(dir->parent->pt_idx);
            else
                *((uint16_t *) &data[6]) = cpu_to_le16(dir->parent->pt_idx);

            if (dir == &viso->root_dir) /* directory ID */
                data[8] = 0;
            else if (i & 2)
                memcpy(&data[8], dir->name_joliet, data[0]);
            else
                memcpy(&data[8], dir->name_short, data[0]);
            data[8 + data[0]] = 0; /* padding for odd directory ID lengths */

            /* Write path table entry. */
            fwrite(data, 8 + data[0] + (data[0] & 1), 1, viso->tf.file);

            /* Increment path table index and stop if it overflows. */
            if (++pt_idx == 0)
                break;

            /* Move on to the next directory. */
            dir = dir->next_dir;
        }

        /* Write this table's size to the corresponding volume descriptor. */
        pt_temp = ftello64(viso->tf.file) - pt_start;
        p       = data;
        VISO_LBE_32(p, pt_temp);
        viso_pwrite(data, viso->pt_meta_offsets[i >> 1], 8, 1, viso->tf.file);

        /* Pad to the next even sector. */
        write = ftello64(viso->tf.file) % (viso->sector_size * 2);
        if (write) {
            write = (viso->sector_size * 2) - write;
            memset(data, 0x00, write);
            fwrite(data, write, 1, viso->tf.file);
        }
    }

    /* Write directory records for each type. */
    for (int i = 0; i < 2; i++) {
        cdrom_image_viso_log("VISO: Generating directory record set #%d:\n", i);

        /* Go through directories. */
        dir = &viso->root_dir;
        while (dir) {
            /* Pad to the next sector if required. */
            write = ftello64(viso->tf.file) % viso->sector_size;
            if (write) {
                write = viso->sector_size - write;
                memset(data, 0x00, write);
                fwrite(data, write, 1, viso->tf.file);
            }

            /* Save this directory's child record array's start offset. */
            uint64_t dir_start = ftello64(viso->tf.file);

            /* Write this directory's child record array's sector offset to its record... */
            uint32_t dir_temp = dir_start / viso->sector_size;
            p                 = data;
            VISO_LBE_32(p, dir_temp);
            viso_pwrite(data, dir->dr_offsets[i] + 2, 8, 1, viso->tf.file);

            /* ...and to its path table entries. */
            viso_pwrite(data, dir->pt_offsets[i << 1] + 2, 4, 1, viso->tf.file);           /* little endian */
            viso_pwrite(data + 4, dir->pt_offsets[(i << 1) | 1] + 2, 4, 1, viso->tf.file); /* big endian */

            if (i) /* clear union if we no longer need path table offsets */
                dir->file = NULL;

            /* Go through entries in this directory. */
            viso_entry_t *entry    = dir->first_child;
            int           dir_type = VISO_DIR_CURRENT;
            while (entry) {
                /* Skip the El Torito boot code entry if present. */
                if (entry == viso->eltorito_entry)
                    goto next_entry;

                cdrom_image_viso_log("[%08X] %s => %s\n", entry,
                                     entry->path ? entry->path : ((dir_type == VISO_DIR_PARENT) ? dir->parent->path : dir->path),
                                     i ? entry->name_rr : entry->name_short);

                /* Fill directory record. */
                viso_fill_dir_record(entry, data, dir_type);

                /* Entries cannot cross sector boundaries, so pad to the next sector if needed. */
                write = viso->sector_size - (ftello64(viso->tf.file) % viso->sector_size);
                if (write < data[0]) {
                    p = data + (viso->sector_size * 2) - write;
                    memset(p, 0x00, write);
                    fwrite(p, write, 1, viso->tf.file);
                }

                /* Write this entry's record's offset. */
                entry->dr_offsets[i] = ftello64(viso->tf.file);

                /* Write data related to the . and .. pseudo-subdirectories,
                   while advancing the current directory type. */
                if (dir_type == VISO_DIR_CURRENT) {
                    /* Write a self-referential pointer to this entry. */
                    p = data + 2;
                    VISO_LBE_32(p, dir_temp);

                    dir_type = VISO_DIR_PARENT;
                } else if (dir_type == VISO_DIR_PARENT) {
                    /* Copy the parent directory's offset and size. The root directory's
                       parent size is a special, self-referential case handled later. */
                    viso_pread(data + 2, dir->parent->dr_offsets[i] + 2, 16, 1, viso->tf.file);

                    dir_type = i ? VISO_DIR_JOLIET : VISO_DIR_REGULAR;
                }

                /* Write entry. */
                fwrite(data, data[0], 1, viso->tf.file);
next_entry:
                /* Move on to the next entry, and stop if the end of this directory was reached. */
                entry = entry->next;
                if (entry && (entry->parent != dir))
                    break;
            }

            /* Write this directory's child record array's size to its parent and . records. */
            dir_temp = ftello64(viso->tf.file) - dir_start;
            p        = data;
            VISO_LBE_32(p, dir_temp);
            viso_pwrite(data, dir->dr_offsets[i] + 10, 8, 1, viso->tf.file);
            viso_pwrite(data, dir->first_child->dr_offsets[i] + 10, 8, 1, viso->tf.file);
            if (dir->parent == dir) /* write size to .. on root directory as well */
                viso_pwrite(data, dir->first_child->next->dr_offsets[i] + 10, 8, 1, viso->tf.file);

            /* Move on to the next directory. */
            dir = dir->next_dir;
        }

        /* Pad to the next even sector. */
        write = ftello64(viso->tf.file) % (viso->sector_size * 2);
        if (write) {
            write = (viso->sector_size * 2) - write;
            memset(data, 0x00, write);
            fwrite(data, write, 1, viso->tf.file);
        }
    }

    /* Allocate entry map for sector->file lookups. */
    viso->entry_map        = (viso_entry_t **) calloc(viso->entry_map_size, sizeof(viso_entry_t *));
    viso->metadata_sectors = ftello64(viso->tf.file) / viso->sector_size;
    viso->all_sectors      = viso->metadata_sectors;

    /* Go through files, allocating them to sectors. */
    cdrom_image_viso_log("VISO: Allocating sectors for files (entry map size %d):\n", viso->entry_map_size);
    viso_entry_t *prev_entry   = &viso->root_dir,
                 *entry        = prev_entry->next,
                 **entry_map_p = viso->entry_map;
    while (entry) {
        /* Skip this entry if it corresponds to a directory. */
        if (S_ISDIR(entry->stats.st_mode)) {
            /* Deallocate directory entries to save some memory. */
            prev_entry->next = entry->next;
            free(entry);
            entry = prev_entry->next;
            continue;
        }

        /* Write this file's starting sector offset to its directory
           entries, unless this is the El Torito boot code entry,
           in which case, write offset and size to the boot entry. */
        if (entry == viso->eltorito_entry) {
            /* Load the entire file if not emulating, or just the first virtual
               sector (which usually contains all the boot code) if emulating. */
            if (entry->name_short[9] == 'C') {
                uint32_t boot_size = entry->stats.st_size;
                if (boot_size % 512) /* round up */
                    boot_size += 512 - (boot_size % 512);
                *((uint16_t *) &data[0]) = boot_size / 512;
            } else {
                *((uint16_t *) &data[0]) = 1;
            }
            *((uint32_t *) &data[2]) = viso->all_sectors;
            viso_pwrite(data, viso->eltorito_offset, 6, 1, viso->tf.file);
        } else {
            p = data;
            VISO_LBE_32(p, viso->all_sectors);
            for (int i = 0; i < (sizeof(entry->dr_offsets) / sizeof(entry->dr_offsets[0])); i++)
                viso_pwrite(data, entry->dr_offsets[i] + 2, 8, 1, viso->tf.file);
        }

        /* Save this file's starting offset. This overwrites dr_offsets in the union. */
        entry->data_offset = ((uint64_t) viso->all_sectors) * viso->sector_size;

        /* Determine how many sectors this file will take. */
        uint32_t size = entry->stats.st_size / viso->sector_size;
        if (entry->stats.st_size % viso->sector_size)
            size++; /* round up to the next sector */
        cdrom_image_viso_log("[%08X] %s => %" PRIu32 " + %" PRIu32 " sectors\n", entry, entry->path, viso->all_sectors, size);

        /* Allocate sectors to this file. */
        viso->all_sectors += size;
        while (size-- > 0)
            *entry_map_p++ = entry;

        /* Move on to the next entry. */
        entry = entry->next;
    }

    /* Write final volume size to all volume descriptors. */
    p = data;
    VISO_LBE_32(p, viso->all_sectors);
    for (int i = 0; i < (sizeof(viso->vol_size_offsets) / sizeof(viso->vol_size_offsets[0])); i++)
        viso_pwrite(data, viso->vol_size_offsets[i], 8, 1, viso->tf.file);

    /* Metadata processing is finished, read it back to memory. */
    cdrom_image_viso_log("VISO: Reading back %d sectors of metadata\n", viso->metadata_sectors);
    viso->metadata = (uint8_t *) calloc(viso->metadata_sectors, viso->sector_size);
    if (!viso->metadata)
        goto end;
    fseeko64(viso->tf.file, 0, SEEK_SET);
    uint64_t metadata_size = viso->metadata_sectors * viso->sector_size, metadata_remain = metadata_size;
    while (metadata_remain > 0)
        metadata_remain -= fread(viso->metadata + (metadata_size - metadata_remain), 1, MIN(metadata_remain, 2048), viso->tf.file);

    /* We no longer need the temporary file; close and delete it. */
    fclose(viso->tf.file);
    viso->tf.file = NULL;
#ifndef ENABLE_CDROM_IMAGE_VISO_LOG
    remove(nvr_path(viso->tf.fn));
#endif

    /* All good. */
    *error = 0;

end:
    /* Set the function pointers. */
    viso->tf.priv = viso;
    if (!*error) {
        cdrom_image_viso_log("VISO: Initialized\n");
        viso->tf.read       = viso_read;
        viso->tf.get_length = viso_get_length;
        viso->tf.close      = viso_close;
        return &viso->tf;
    } else {
        cdrom_image_viso_log("VISO: Initialization failed\n");
        if (data)
            free(data);
        if (wtemp)
            free(wtemp);
        viso_close(&viso->tf);
        return NULL;
    }
}
