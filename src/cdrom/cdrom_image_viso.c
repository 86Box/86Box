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
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2022 RichardG.
 */
#ifndef _LARGEFILE_SOURCE
#    define _LARGEFILE_SOURCE
#endif
#ifndef _LARGEFILE64_SOURCE
#    define _LARGEFILE64_SOURCE
#endif
#define __STDC_FORMAT_MACROS
#include <ctype.h>
#include <inttypes.h>
#ifdef IMAGE_VISO_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/cdrom_image_viso.h>
#include <86box/log.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/bswap.h>
#include <86box/plat_dir.h>
#include <86box/version.h>
#include <86box/nvr.h>

#ifndef S_ISDIR
#    define S_ISDIR(m) (((m) &S_IFMT) == S_IFDIR)
#endif

#ifdef _WIN32
#    define stat _stat64
typedef struct __stat64 stat_t;
#else
typedef struct stat stat_t;
#endif

#define VISO_SKIP(p, n)         \
    {                           \
        memset((p), 0x00, (n)); \
        (p) += (n);             \
    }
#define VISO_TIME_VALID(t) ((t) > 0)

/* ISO 9660 defines "both endian" data formats, which
   are stored as little endian followed by big endian. */
#define VISO_LBE_16(p, x)                       \
    {                                           \
        *((uint16_t *) (p)) = cpu_to_le16((x)); \
        (p) += 2;                               \
        *((uint16_t *) (p)) = cpu_to_be16((x)); \
        (p) += 2;                               \
    }
#define VISO_LBE_32(p, x)                       \
    {                                           \
        *((uint32_t *) (p)) = cpu_to_le32((x)); \
        (p) += 4;                               \
        *((uint32_t *) (p)) = cpu_to_be32((x)); \
        (p) += 4;                               \
    }

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
    VISO_DIR_CURRENT_ROOT,
    VISO_DIR_PARENT,
    VISO_DIR_REGULAR,
    VISO_DIR_JOLIET
};

enum {
    VISO_FORMAT_ISO    = 1, /* ISO 9660 (High Sierra if not set) */
    VISO_FORMAT_JOLIET = 2, /* Joliet extensions (Microsoft) */
    VISO_FORMAT_RR     = 4  /* Rock Ridge extensions (*nix) */
};

typedef struct _viso_entry_ {
    union { /* save some memory */
        uint64_t pt_offsets[4];
        FILE    *file;
    };
    union {
        char     name_short[13];
        uint64_t dr_offsets[2];
        uint64_t data_offset;
    };
    uint16_t pt_idx;

    stat_t stats;

    struct _viso_entry_ *parent, *next, *next_dir, *first_child;

    char *basename, path[];
} viso_entry_t;

typedef struct {
    uint64_t vol_size_offsets[2];
    uint64_t pt_meta_offsets[2];
    int      format;
    uint8_t  use_version_suffix : 1;
    size_t   metadata_sectors, all_sectors, entry_map_size, sector_size, file_fifo_pos;
    uint8_t *metadata;

    track_file_t   tf;
    viso_entry_t  *root_dir;
    viso_entry_t **entry_map;
    viso_entry_t  *file_fifo[VISO_OPEN_FILES];
} viso_t;

static const char rr_eid[]   = "RRIP_1991A"; /* identifiers used in ER field for Rock Ridge */
static const char rr_edesc[] = "THE ROCK RIDGE INTERCHANGE PROTOCOL PROVIDES SUPPORT FOR POSIX FILE SYSTEM SEMANTICS.";
static int8_t     tz_offset  = 0;

#ifdef IMAGE_VISO_LOG
int image_viso_do_log = IMAGE_VISO_LOG;

void
image_viso_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (image_viso_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define image_viso_log(priv, fmt, ...)
#endif

static size_t
viso_pread(void *ptr, const uint64_t offset, const size_t size,
           const size_t count, FILE *fp)
{
    const uint64_t cur_pos = ftello64(fp);
    size_t         ret     = 0;
    if (fseeko64(fp, offset, SEEK_SET) != -1)
        ret = fread(ptr, size, count, fp);
    fseeko64(fp, cur_pos, SEEK_SET);
    return ret;
}

static size_t
viso_pwrite(const void *ptr, const uint64_t offset, const size_t size,
            const size_t count, FILE *fp)
{
    const uint64_t cur_pos = ftello64(fp);
    size_t         ret     = 0;
    if (fseeko64(fp, offset, SEEK_SET) != -1)
        ret = fwrite(ptr, size, count, fp);
    fseeko64(fp, cur_pos, SEEK_SET);
    return ret;
}

static size_t
viso_convert_utf8(wchar_t *dest, const char *src, ssize_t buf_size)
{
    uint32_t c;
    wchar_t *p = dest;
    size_t   next;

    while (buf_size-- > 0) {
        /* Interpret source codepoint. */
        c = *src;
        if (!c) {
            /* Terminator. */
            *p = 0;
            break;
        } else if (c & 0x80) {
            /* Convert UTF-8 sequence into a codepoint. */
            next = 0;
            while (c & 0x40) {
                next++;
                c <<= 1;
            }
            c = *src++ & (0x3f >> next);
            while ((next-- > 0) && ((*src & 0xc0) == 0x80))
                c = (c << 6) | (*src++ & 0x3f);

            /* Convert codepoints >= U+10000 to UTF-16 surrogate pairs.
               This has to be done here because wchar_t on some platforms
               (Windows) is not wide enough to store such high codepoints. */
            if (c >= 0x10000) {
                if ((c <= 0x10ffff) && (buf_size-- > 0)) {
                    /* Encode surrogate pair. */
                    c -= 0x10000;
                    *p++ = 0xd800 | (c >> 10);
                    c    = 0xdc00 | (c & 0x3ff);
                } else {
                    /* Codepoint overflow or no room for a pair. */
                    c = '?';
                }
            }
        } else {
            /* Pass through sub-UTF-8 codepoints. */
            src++;
        }

        /* Write destination codepoint. */
        *p++ = c;
    }

    return p - dest;
}

#define VISO_WRITE_STR_FUNC(func, dst_type, src_type, converter, bounds_chk)        \
    static void                                                                     \
    func(dst_type *dest, const src_type *src, ssize_t buf_size, int charset)        \
    {                                                                               \
        src_type c;                                                                 \
        while (buf_size-- > 0) {                                                    \
            /* Interpret source codepoint. */                                       \
            c = *src++;                                                             \
            switch (c) {                                                            \
                case 0x00:                                                          \
                    /* Terminator, apply space padding. */                          \
                    while (buf_size-- >= 0)                                         \
                        *dest++ = converter(' ');                                   \
                    return;                                                         \
                                                                                    \
                case 'A' ... 'Z':                                                   \
                case '0' ... '9':                                                   \
                case '_':                                                           \
                    /* Valid on all sets. */                                        \
                    break;                                                          \
                                                                                    \
                case 'a' ... 'z':                                                   \
                    /* Convert to uppercase on D and A. */                          \
                    if (charset <= VISO_CHARSET_A)                                  \
                        c -= 'a' - 'A';                                             \
                    break;                                                          \
                                                                                    \
                case '!':                                                           \
                case '#':                                                           \
                case '$':                                                           \
                case '%':                                                           \
                case '&':                                                           \
                case '\'':                                                          \
                case '(':                                                           \
                case ')':                                                           \
                case '-':                                                           \
                case '@':                                                           \
                case '^':                                                           \
                case '`':                                                           \
                case '{':                                                           \
                case '}':                                                           \
                case '~':                                                           \
                    /* Valid on all sets (non-complying DOS characters). */         \
                    break;                                                          \
                                                                                    \
                case ' ':                                                           \
                case '"':                                                           \
                case '+':                                                           \
                case ',':                                                           \
                case '.':                                                           \
                case '<':                                                           \
                case '=':                                                           \
                case '>':                                                           \
                    /* Valid for A and filenames but not for D. */                  \
                    if (charset < VISO_CHARSET_A)                                   \
                        c = '_';                                                    \
                    break;                                                          \
                                                                                    \
                case '*':                                                           \
                case '/':                                                           \
                case ':':                                                           \
                case ';':                                                           \
                case '?':                                                           \
                    /* Valid for A but not for filenames or D. */                   \
                    if ((charset < VISO_CHARSET_A) || (charset == VISO_CHARSET_FN)) \
                        c = '_';                                                    \
                    break;                                                          \
                                                                                    \
                case 0x01 ... 0x1f:                                                 \
                case '\\':                                                          \
                    /* Not valid for D, A or filenames. */                          \
                    if (charset <= VISO_CHARSET_FN)                                 \
                        c = '_';                                                    \
                    break;                                                          \
                                                                                    \
                default:                                                            \
                    /* Not valid for D or A, but valid for filenames. */            \
                    if ((charset < VISO_CHARSET_FN) || (bounds_chk))                \
                        c = '_';                                                    \
                    break;                                                          \
            }                                                                       \
                                                                                    \
            /* Write destination codepoint with conversion function applied. */     \
            *dest++ = converter(c);                                                 \
        }                                                                           \
    }
VISO_WRITE_STR_FUNC(viso_write_string, uint8_t, char, , 0)
VISO_WRITE_STR_FUNC(viso_write_wstring, uint16_t, wchar_t, cpu_to_be16, c > 0xffff)

static int
viso_fill_fn_short(char *data, const viso_entry_t *entry, viso_entry_t **entries)
{
    /* Get name and extension length. */
    const char *ext_pos = strrchr(entry->basename, '.');
    int         name_len;
    int         ext_len;
    if (ext_pos) {
        name_len = ext_pos - entry->basename;
        ext_len  = strlen(ext_pos);
    } else {
        name_len = strlen(entry->basename);
        ext_len  = 0;
    }

    /* Copy name. */
    int name_copy_len = MIN(8, name_len);
    viso_write_string((uint8_t *) data, entry->basename, name_copy_len, VISO_CHARSET_D);
    data[name_copy_len] = '\0';

    /* Copy extension to temporary buffer. */
    char ext[5]     = { 0 };
    int  force_tail = (name_len > 8) || (ext_len == 1);
    if (ext_len > 1) {
        ext[0] = '.';
        if (ext_len > 4) {
            ext_len    = 4;
            force_tail = 1;
        }
        viso_write_string((uint8_t *) &ext[1], &ext_pos[1], ext_len - 1, VISO_CHARSET_D);
    }

    /* Check if this filename is unique, and add a tail if required, while also adding the extension. */
    char tail[16];
    for (int i = force_tail; i <= 999999; i++) {
        /* Add tail to the filename if this is not the first run. */
        int tail_len = -1;
        if (i) {
            tail_len = sprintf(tail, "~%d", i);
            strcpy(&data[MIN(name_copy_len, 8 - tail_len)], tail);
        }

        /* Add extension to the filename if present. */
        if (ext[0])
            strcat(data, ext);

        /* Go through files in this directory to make sure this filename is unique. */
        for (size_t j = 0; entries[j] != entry; j++) {
            /* Flag and stop if this filename was seen. */
            if (!strcmp(data, entries[j]->name_short)) {
                tail_len = 0;
                break;
            }
        }

        /* Stop if this is an unique name. */
        if (tail_len)
            return 0;
    }
    return 1;
}

static size_t
viso_fill_fn_rr(uint8_t *data, const viso_entry_t *entry, size_t max_len)
{
    /* Trim filename to max_len if needed. */
    size_t len = strlen(entry->basename);
    if (len > max_len) {
        viso_write_string(data, entry->basename, max_len, VISO_CHARSET_FN);

        /* Relocate extension if the original name exceeds the maximum length. */
        if (!S_ISDIR(entry->stats.st_mode)) { /* do this on files only */
            const char *ext = strrchr(entry->basename, '.');
            if (ext > entry->basename) {
                len = strlen(ext);
                if (len >= max_len)
                    len = max_len - 1; /* don't create a dotfile where there isn't one */
                viso_write_string(data + (max_len - len), ext, len, VISO_CHARSET_FN);
            }
        }

        return max_len;
    } else {
        viso_write_string(data, entry->basename, len, VISO_CHARSET_FN);
        return len;
    }
}

static size_t
viso_fill_fn_joliet(uint8_t *data, const viso_entry_t *entry, size_t max_len) /* note: receives and returns byte sizes */
{
    /* Decode filename as UTF-8. */
    size_t  len = strlen(entry->basename);
    wchar_t utf8dec[len + 1];
    len = viso_convert_utf8(utf8dec, entry->basename, len + 1);

    /* Trim decoded filename to max_len if needed. */
    max_len /= 2;
    if (len > max_len) {
        viso_write_wstring((uint16_t *) data, utf8dec, max_len, VISO_CHARSET_FN);

        /* Relocate extension if the original name exceeds the maximum length. */
        if (!S_ISDIR(entry->stats.st_mode)) { /* do this on files only */
            const wchar_t *ext = wcsrchr(utf8dec, L'.');
            if (ext > utf8dec) {
                len = wcslen(ext);
                if (len > max_len)
                    len = max_len;
                else if ((len < max_len) && ((((uint16_t *) data)[max_len - len] & be16_to_cpu(0xfc00)) == be16_to_cpu(0xdc00)))
                    max_len--; /* don't break an UTF-16 pair */
                viso_write_wstring(((uint16_t *) data) + (max_len - len), ext, len, VISO_CHARSET_FN);
            }
        }

        return max_len * 2;
    } else {
        viso_write_wstring((uint16_t *) data, utf8dec, len, VISO_CHARSET_FN);
        return len * 2;
    }
}

static int
viso_fill_time(uint8_t *data, time_t time, int format, int longform)
{
    uint8_t   *p      = data;
    struct tm *time_s = localtime(&time);
    if (!time_s) {
        /* localtime will return NULL if the time_t is negative (Windows)
           or way too far into 64-bit space (Linux). Fall back to epoch. */
        time_t epoch = 0;
        time_s       = localtime(&epoch);
        if (UNLIKELY(!time_s))
            fatal("VISO: localtime(0) = NULL\n");

        /* Force year clamping if the timestamp is known to be outside the supported ranges. */
        if (time < (longform ? -62135596800LL : -2208988800LL)) /* 0001-01-01 00:00:00 : 1900-01-01 00:00:00 */
            time_s->tm_year = -1901;
        else if (time > (longform ? 253402300799LL : 5869583999LL)) /* 9999-12-31 23:59:59 : 2155-12-31 23:59:59 */
            time_s->tm_year = 8100;
    }

    /* Clamp year to the supported ranges, and assume the
       OS returns valid numbers in the other struct fields. */
    if (time_s->tm_year < (longform ? -1900 : 0)) {
        time_s->tm_year = longform ? -1900 : 0;
        time_s->tm_mon = time_s->tm_hour = time_s->tm_min = time_s->tm_sec = 0;
        time_s->tm_mday                                                    = 1;
    } else if (time_s->tm_year > (longform ? 8099 : 255)) {
        time_s->tm_year = longform ? 8099 : 255;
        time_s->tm_mon  = 11;
        time_s->tm_mday = 31;
        time_s->tm_hour = 23;
        time_s->tm_min = time_s->tm_sec = 59;
    }

    /* Convert timestamp. */
    if (longform) {
        p += sprintf((char *) p, "%04u%02u%02u%02u%02u%02u00",
                     1900 + time_s->tm_year, 1 + time_s->tm_mon, time_s->tm_mday,
                     time_s->tm_hour, time_s->tm_min, time_s->tm_sec);
    } else {
        *p++ = time_s->tm_year;    /* year since 1900 */
        *p++ = 1 + time_s->tm_mon; /* month */
        *p++ = time_s->tm_mday;    /* day */
        *p++ = time_s->tm_hour;    /* hour */
        *p++ = time_s->tm_min;     /* minute */
        *p++ = time_s->tm_sec;     /* second */
    }
    if (format & VISO_FORMAT_ISO)
        *p++ = tz_offset; /* timezone (ISO only) */

    return p - data;
}

static int
viso_fill_dir_record(uint8_t *data, viso_entry_t *entry, viso_t *viso, int type)
{
    uint8_t *p = data;
    uint8_t *q;
    uint8_t *r;

    *p++ = 0;                             /* size (filled in later) */
    *p++ = 0;                             /* extended attribute length */
    VISO_SKIP(p, 8);                      /* sector offset */
    VISO_LBE_32(p, entry->stats.st_size); /* size (filled in later if this is a directory) */
    p += viso_fill_time(p, entry->stats.st_mtime, viso->format, 0); /* time */
    *p++ = S_ISDIR(entry->stats.st_mode) ? 0x02 : 0x00;             /* flags */

    VISO_SKIP(p, 2 + !(viso->format & VISO_FORMAT_ISO)); /* file unit size (reserved on HSF), interleave gap size (HSF/ISO) and skip factor (HSF only) */
    VISO_LBE_16(p, 1);                                   /* volume sequence number */

    switch (type) {
        case VISO_DIR_CURRENT:
        case VISO_DIR_CURRENT_ROOT:
        case VISO_DIR_PARENT:
            *p++ = 1;                                 /* file ID length */
            *p++ = (type == VISO_DIR_PARENT) ? 1 : 0; /* magic value corresponding to . or .. */

            /* Fill Rock Ridge Extension Record for the root directory's . entry. */
            if ((type == VISO_DIR_CURRENT_ROOT) && (viso->format & VISO_FORMAT_RR)) {
                *p++ = 'E';
                *p++ = 'R';
                *p++ = 8 + (sizeof(rr_eid) - 1) + (sizeof(rr_edesc) - 1); /* length */
                *p++ = 1;                                                 /* version */

                *p++ = sizeof(rr_eid) - 1;   /* ID length */
                *p++ = sizeof(rr_edesc) - 1; /* description length */
                *p++ = 0;                    /* source length (source is recommended but won't fit here) */
                *p++ = 1;                    /* extension version */

                memcpy(p, rr_eid, sizeof(rr_eid) - 1); /* ID */
                p += sizeof(rr_eid) - 1;
                memcpy(p, rr_edesc, sizeof(rr_edesc) - 1); /* description */
                p += sizeof(rr_edesc) - 1;

                goto pad_susp;
            }
            break;

        case VISO_DIR_REGULAR:
            q = p++; /* save file ID length location for later */

            *q = strlen(entry->name_short);
            memcpy(p, entry->name_short, *q); /* file ID */
            p += *q;
            if (viso->use_version_suffix && !S_ISDIR(entry->stats.st_mode)) {
                *p++ = ';'; /* version suffix for files (ISO only, except for Windows NT SETUPLDR.BIN El Torito hack) */
                *p++ = '1';
                *q += 2;
            }

            if (!(*q & 1)) /* padding for even file ID lengths */
                *p++ = 0;

            /* Fill Rock Ridge data. */
            if (viso->format & VISO_FORMAT_RR) {
                *p++ = 'R'; /* RR = present Rock Ridge entries (only documented by RRIP revision 1.09!) */
                *p++ = 'R';
                *p++ = 5; /* length */
                *p++ = 1; /* version */

                q = p++; /* save Rock Ridge flags location for later */

#ifndef _WIN32              /* attributes reported by MinGW don't really make sense because it's Windows */
                *q |= 0x01; /* PX = POSIX attributes */
                *p++ = 'P';
                *p++ = 'X';
                *p++ = 36; /* length */
                *p++ = 1;  /* version */

                VISO_LBE_32(p, entry->stats.st_mode);  /* mode */
                VISO_LBE_32(p, entry->stats.st_nlink); /* number of links */
                VISO_LBE_32(p, entry->stats.st_uid);   /* owner UID */
                VISO_LBE_32(p, entry->stats.st_gid);   /* owner GID */

#    ifndef S_ISCHR
#        define S_ISCHR(x) 0
#    endif
#    ifndef S_ISBLK
#        define S_ISBLK(x) 0
#    endif
                if (S_ISCHR(entry->stats.st_mode) || S_ISBLK(entry->stats.st_mode)) {
                    *q |= 0x02; /* PN = POSIX device */
                    *p++ = 'P';
                    *p++ = 'N';
                    *p++ = 20; /* length */
                    *p++ = 1;  /* version */

                    uint64_t dev = entry->stats.st_rdev; /* avoid warning if <= 32 bits */
                    VISO_LBE_32(p, dev >> 32);           /* device number (high 32 bits) */
                    VISO_LBE_32(p, dev);                 /* device number (low 32 bits) */
                }
#endif
                int times =
#ifdef st_birthtime
                    (VISO_TIME_VALID(entry->stats.st_birthtime) << 0) | /* creation (hack: assume the platform remaps st_birthtime at header level) */
#endif
                    (VISO_TIME_VALID(entry->stats.st_mtime) << 1) | /* modify */
                    (VISO_TIME_VALID(entry->stats.st_atime) << 2) | /* access */
                    (VISO_TIME_VALID(entry->stats.st_ctime) << 3);  /* attributes */
                if (times) {
                    *q |= 0x80; /* TF = timestamps */
                    *p++ = 'T';
                    *p++ = 'F';
                    r    = p; /* save length location for later */
                    *p++ = 2; /* length (added to later) */
                    *p++ = 1; /* version */

                    *p++ = times; /* flags */
#ifdef st_birthtime
                    if (times & (1 << 0))
                        p += viso_fill_time(p, entry->stats.st_birthtime, viso->format, 0); /* creation */
#endif
                    if (times & (1 << 1))
                        p += viso_fill_time(p, entry->stats.st_mtime, viso->format, 0); /* modify */
                    if (times & (1 << 2))
                        p += viso_fill_time(p, entry->stats.st_atime, viso->format, 0); /* access */
                    if (times & (1 << 3))
                        p += viso_fill_time(p, entry->stats.st_ctime, viso->format, 0); /* attributes */

                    *r += p - r; /* add to length */
                }

                *q |= 0x08; /* NM = alternate name */
                *p++ = 'N';
                *p++ = 'M';
                r    = p; /* save length location for later */
                *p++ = 2; /* length (added to later) */
                *p++ = 1; /* version */

                *p++ = 0;                                         /* flags */
                p += viso_fill_fn_rr(p, entry, 254 - (p - data)); /* name */

                *r += p - r; /* add to length */
pad_susp:
                if ((p - data) & 1) /* padding for odd SUSP section lengths */
                    *p++ = 0;
            }
            break;

        case VISO_DIR_JOLIET:
            q = p++; /* save file ID length location for later */

            *q = viso_fill_fn_joliet(p, entry, 254 - (p - data));
            p += *q;

            if (!(*q & 1)) /* padding for even file ID lengths */
                *p++ = 0;
            break;

        default:
            break;
    }

    if (UNLIKELY((p - data) > 255))
        fatal("VISO: Directory record overflow (%" PRIuPTR ") on entry %08" PRIXPTR "\n", (uintptr_t) (p - data), (uintptr_t) entry);

    data[0] = p - data; /* length */
    return data[0];
}

static int
viso_compare_entries(const void *a, const void *b)
{
    return strcmp((*((viso_entry_t **) a))->name_short, (*((viso_entry_t **) b))->name_short);
}

int
viso_read(void *priv, uint8_t *buffer, uint64_t seek, size_t count)
{
    track_file_t *tf   = (track_file_t *) priv;
    viso_t       *viso = (viso_t *) tf->priv;

    /* Handle reads in a sector by sector basis. */
    while (count > 0) {
        /* Determine the current sector, offset and remainder. */
        size_t sector        = seek / viso->sector_size;
        size_t sector_offset = seek % viso->sector_size;
        size_t sector_remain = MIN(count, viso->sector_size - sector_offset);

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
                        image_viso_log(viso->tf.log, "Closing [%s]...\n", other_entry->path);
                        fclose(other_entry->file);
                        other_entry->file = NULL;
                        image_viso_log(viso->tf.log, "Done\n");
                    }

                    /* Open file. */
                    image_viso_log(viso->tf.log, "Opening [%s]...\n", entry->path);
                    if ((entry->file = fopen(entry->path, "rb"))) {
                        image_viso_log(viso->tf.log, "Done\n");

                        /* Add this entry to the FIFO. */
                        viso->file_fifo[viso->file_fifo_pos++] = entry;
                        viso->file_fifo_pos &= (sizeof(viso->file_fifo) / sizeof(viso->file_fifo[0])) - 1;
                    } else {
                        image_viso_log(viso->tf.log, "Failed\n");

                        /* Clear any existing FIFO entry. */
                        viso->file_fifo[viso->file_fifo_pos] = NULL;
                    }
                }

                /* Read data. */
                if (!entry->file || (fseeko64(entry->file, seek - entry->data_offset, SEEK_SET) == -1))
                    return -1;
                read = fread(buffer, 1, sector_remain, entry->file);
                if (sector_remain && !read)
                    return -1;
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
viso_get_length(void *priv)
{
    track_file_t       *tf   = (track_file_t *) priv;
    const viso_t       *viso = (viso_t *) tf->priv;

    return ((uint64_t) viso->all_sectors) * viso->sector_size;
}

void
viso_close(void *priv)
{
    track_file_t *tf   = (track_file_t *) priv;
    viso_t       *viso = (viso_t *) tf->priv;

    if (viso == NULL)
        return;

    image_viso_log(viso->tf.log, "close()\n");

    /* De-allocate everything. */
    if (tf->fp)
        fclose(tf->fp);
#ifndef ENABLE_IMAGE_VISO_LOG
    remove(nvr_path(viso->tf.fn));
#endif

    viso_entry_t *entry = viso->root_dir;
    viso_entry_t *next_entry;
    while (entry) {
        if (entry->file)
            fclose(entry->file);
        next_entry = entry->next;
        free(entry);
        entry = next_entry;
    }

    if (viso->metadata)
        free(viso->metadata);
    if (viso->entry_map)
        free(viso->entry_map);

    if (tf->log != NULL) {

    }

    free(viso);
}

track_file_t *
viso_init(const uint8_t id, const char *dirname, int *error)
{
    /* Initialize our data structure. */
    viso_t  *viso = (viso_t *) calloc(1, sizeof(viso_t));
    uint8_t *data = NULL;
    uint8_t *p;
    *error        = 1;

    if (viso == NULL)
        goto end;

    char n[1024]        = { 0 };

    sprintf(n, "CD-ROM %i VISO ", id + 1);
    viso->tf.log        = log_open(n);

    image_viso_log(viso->tf.log, "init()\n");

    viso->sector_size        = VISO_SECTOR_SIZE;
    viso->format             = VISO_FORMAT_ISO | VISO_FORMAT_JOLIET | VISO_FORMAT_RR;
    viso->use_version_suffix = (viso->format & VISO_FORMAT_ISO); /* cleared later if required */

    /* Prepare temporary data buffers. */
    data = calloc(2, viso->sector_size);
    if (!data)
        goto end;

        /* Open temporary file. */
#ifdef ENABLE_IMAGE_VISO_LOG
    strcpy(viso->tf.fn, "viso-debug.iso");
#else
    plat_tempfile(viso->tf.fn, "viso", ".tmp");
#endif
    viso->tf.fp = plat_fopen64(nvr_path(viso->tf.fn), "w+b");
    if (!viso->tf.fp)
        goto end;

    /* Set up directory traversal. */
    image_viso_log(viso->tf.log, "Traversing directories:\n");
    viso_entry_t        *entry;
    viso_entry_t        *last_entry;
    viso_entry_t        *dir;
    viso_entry_t        *last_dir;
    const viso_entry_t  *eltorito_dir = NULL;
    const viso_entry_t  *eltorito_entry = NULL;
    struct dirent       *readdir_entry;
    int                  len;
    int                  eltorito_others_present = 0;
    size_t               dir_path_len;
    uint64_t             eltorito_offset = 0;
    uint8_t              eltorito_type   = 0;

    /* Fill root directory entry. */
    dir_path_len = strlen(dirname);
    last_entry = dir = last_dir = viso->root_dir = (viso_entry_t *) calloc(1, sizeof(viso_entry_t) + dir_path_len + 1);
    if (!dir)
        goto end;
    strcpy(dir->path, dirname);
    if (stat(dirname, &dir->stats) != 0) {
        /* Use a blank structure if stat failed. */
        memset(&dir->stats, 0x00, sizeof(stat_t));
    }
    if (!S_ISDIR(dir->stats.st_mode)) /* root is not a directory */
        goto end;
    dir->parent = dir; /* for the root's path table and .. entries */
    image_viso_log(viso->tf.log, "[%08X] %s => [root]\n", dir, dir->path);

    /* Traverse directories, starting with the root. */
    viso_entry_t **dir_entries     = NULL;
    size_t         dir_entries_len = 0;
    while (dir) {
        /* Open directory for listing. */
        DIR *dirp = opendir(dir->path);

        /* Iterate through this directory's children to determine the entry array size. */
        size_t children_count = 3; /* include terminator, . and .. */
        if (dirp) {                /* create empty directory if opendir failed */
            while ((readdir_entry = readdir(dirp))) {
                /* Ignore . and .. pseudo-directories. */
                if ((readdir_entry->d_name[0] == '.') && ((readdir_entry->d_name[1] == '\0') || (*((uint16_t *) &readdir_entry->d_name[1]) == '.')))
                    continue;
                children_count++;
            }
        }

        /* Grow array if needed. */
        if (children_count > dir_entries_len) {
            viso_entry_t **new_dir_entries = (viso_entry_t **) realloc(dir_entries, children_count * sizeof(viso_entry_t *));
            if (new_dir_entries) {
                dir_entries     = new_dir_entries;
                dir_entries_len = children_count;
            } else {
                goto next_dir;
            }
        }

        /* Add . and .. pseudo-directories. */
        dir_path_len = strlen(dir->path);
        for (children_count = 0; children_count < 2; children_count++) {
            entry = dir_entries[children_count] = (viso_entry_t *) calloc(1, sizeof(viso_entry_t) + 1);
            if (!entry)
                goto next_dir;
            entry->parent = dir;
            if (!children_count)
                dir->first_child = entry;

            /* Stat the current directory or parent directory. */
            if (stat(children_count ? dir->parent->path : dir->path, &entry->stats) != 0) {
                /* Use a blank structure if stat failed. */
                memset(&entry->stats, 0x00, sizeof(stat_t));
            }

            /* Set basename. */
            strcpy(entry->name_short, children_count ? ".." : ".");

            image_viso_log(viso->tf.log, "[%08X] %s => %s\n", entry,
                           dir->path, entry->name_short);
        }

        /* Iterate through this directory's children again, making the entries. */
        if (dirp) {
            rewinddir(dirp);
            while ((readdir_entry = readdir(dirp))) {
                /* Ignore . and .. pseudo-directories. */
                if ((readdir_entry->d_name[0] == '.') &&
                    ((readdir_entry->d_name[1] == '\0') ||
                    (*((uint16_t *) &readdir_entry->d_name[1]) == '.')))
                    continue;

                /* Add and fill entry. */
                entry = dir_entries[children_count++] =
                    (viso_entry_t *) calloc(1, sizeof(viso_entry_t) +
                        dir_path_len + strlen(readdir_entry->d_name) + 2);
                if (entry == NULL)
                    break;
                entry->parent = dir;
                strcpy(entry->path, dir->path);
                path_slash(&entry->path[dir_path_len]);
                entry->basename = &entry->path[dir_path_len + 1];
                strcpy(entry->basename, readdir_entry->d_name);

                /* Stat this child. */
                if (stat(entry->path, &entry->stats) != 0) {
                    /* Use a blank structure if stat failed. */
                    memset(&entry->stats, 0x00, sizeof(stat_t));
                }

                /* Handle file size and El Torito boot code. */
                if (!S_ISDIR(entry->stats.st_mode)) {
                    /* Clamp file size to 4 GB - 1 byte. */
                    if (entry->stats.st_size > ((uint32_t) -1))
                        entry->stats.st_size = (uint32_t) -1;

                    /* Increase entry map size. */
                    viso->entry_map_size += entry->stats.st_size / viso->sector_size;
                    if (entry->stats.st_size % viso->sector_size)
                        viso->entry_map_size++; /* round up to the next sector */

                    /* Detect El Torito boot code file and set it accordingly. */
                    if (dir == eltorito_dir) {
                        if (!stricmp(readdir_entry->d_name, "Boot-NoEmul.img")) {
                            eltorito_type = 0x00;
have_eltorito_entry:
                            if (eltorito_entry)
                                eltorito_others_present = 1; /* flag that the boot code directory contains other files */
                            eltorito_entry = entry;
                        } else if (!stricmp(readdir_entry->d_name, "Boot-1.2M.img")) {
                            eltorito_type = 0x01;
                            goto have_eltorito_entry;
                        } else if (!stricmp(readdir_entry->d_name, "Boot-1.44M.img")) {
                            eltorito_type = 0x02;
                            goto have_eltorito_entry;
                        } else if (!stricmp(readdir_entry->d_name, "Boot-2.88M.img")) {
                            eltorito_type = 0x03;
                            goto have_eltorito_entry;
                        } else if (!stricmp(readdir_entry->d_name, "Boot-HardDisk.img")) {
                            eltorito_type = 0x04;
                            goto have_eltorito_entry;
                        } else {
                            eltorito_others_present = 1; /* flag that the boot code directory contains other files */
                        }
                    } else {
                        /* Disable version suffixes if this structure appears to contain the Windows NT
                           El Torito boot code, which is known not to tolerate suffixed file names. */
                        if (eltorito_dir &&                                  /* El Torito directory present? */
                            (eltorito_type == 0x00) &&                       /* El Torito directory not checked yet, or confirmed to contain non-emulation boot code? */
                            (dir->parent == viso->root_dir) &&               /* one subdirectory deep? (I386 for instance) */
                            !stricmp(readdir_entry->d_name, "SETUPLDR.BIN")) /* SETUPLDR.BIN present? */
                            viso->use_version_suffix = 0;
                    }
                } else if ((dir == viso->root_dir) && !stricmp(readdir_entry->d_name, "[BOOT]")) {
                    /* Set this as the directory containing El Torito boot code. */
                    eltorito_dir            = entry;
                    eltorito_others_present = 0;
                }

                /* Set short filename. */
                if (viso_fill_fn_short(entry->name_short, entry, dir_entries)) {
                    free(entry);
                    children_count--;
                    continue;
                }

                image_viso_log(viso->tf.log, "[%08X] %s => [%-12s] %s\n", entry,
                               dir->path, entry->name_short, entry->basename);
            }
        } else {
            image_viso_log(viso->tf.log, "Failed to enumerate [%s], will be empty\n",
                           dir->path);
        }

        /* Add terminator. */
        dir_entries[children_count] = NULL;

        /* Sort directory entries and create the linked list. */
        qsort(&dir_entries[2], children_count - 2, sizeof(viso_entry_t *), viso_compare_entries);
        for (size_t i = 0; dir_entries[i]; i++) {
            /* Add link. */
            last_entry->next = dir_entries[i];
            last_entry       = dir_entries[i];

            /* If this is a directory, add it to the traversal list. */
            if ((i >= 2) && S_ISDIR(dir_entries[i]->stats.st_mode)) {
                last_dir->next_dir = dir_entries[i];
                last_dir           = dir_entries[i];
            }
        }

next_dir:
        /* Move on to the next directory. */
        if (dirp)
            closedir(dirp);
        dir = dir->next_dir;
    }
    if (dir_entries)
        free(dir_entries);

    /* Write 16 blank sectors. */
    for (int i = 0; i < 16; i++)
        fwrite(data, viso->sector_size, 1, viso->tf.fp);

    /* Get current time for the volume descriptors, and calculate
       the timezone offset for descriptors and file times to use. */
    tzset();
    time_t now = time(NULL);
    if (viso->format & VISO_FORMAT_ISO) /* timezones are ISO only */
        tz_offset = (now - mktime(gmtime(&now))) / (3600 / 4);

    /* Get root directory basename for the volume ID. */
    const char *basename = path_get_filename(viso->root_dir->path);
    if (!basename || (basename[0] == '\0'))
        basename = EMU_NAME;

    /* Determine whether or not we're working with 2 volume descriptors
       (as well as 2 directory trees and 4 path tables) for Joliet. */
    int max_vd = (viso->format & VISO_FORMAT_JOLIET) ? 1 : 0;

    /* Write volume descriptors. */
    for (int i = 0; i <= max_vd; i++) {
        /* Fill volume descriptor. */
        p = data;
        if (!(viso->format & VISO_FORMAT_ISO))
            VISO_LBE_32(p, ftello64(viso->tf.fp) / viso->sector_size);    /* sector offset (HSF only) */
        *p++ = 1 + i;                                                       /* type */
        memcpy(p, (viso->format & VISO_FORMAT_ISO) ? "CD001" : "CDROM", 5); /* standard ID */
        p += 5;
        *p++ = 1; /* version */
        *p++ = 0; /* unused */

        if (i) {
            viso_write_wstring((uint16_t *) p, EMU_NAME_W, 16, VISO_CHARSET_A); /* system ID */
            p += 32;
            wchar_t wtemp[16];
            viso_convert_utf8(wtemp, basename, 16);
            viso_write_wstring((uint16_t *) p, wtemp, 16, VISO_CHARSET_D); /* volume ID */
            p += 32;
        } else {
            viso_write_string(p, EMU_NAME, 32, VISO_CHARSET_A); /* system ID */
            p += 32;
            viso_write_string(p, basename, 32, VISO_CHARSET_D); /* volume ID */
            p += 32;
        }

        VISO_SKIP(p, 8); /* unused */

        viso->vol_size_offsets[i] = ftello64(viso->tf.fp) + (p - data);
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
        viso->pt_meta_offsets[i] = ftello64(viso->tf.fp) + (p - data);
        VISO_SKIP(p, 24 + (16 * !(viso->format & VISO_FORMAT_ISO))); /* PT size, LE PT offset, optional LE PT offset (three on HSF), BE PT offset, optional BE PT offset (three on HSF) */

        viso->root_dir->dr_offsets[i] = ftello64(viso->tf.fp) + (p - data);
        p += viso_fill_dir_record(p, viso->root_dir, viso, VISO_DIR_CURRENT); /* root directory */

        int copyright_abstract_len = (viso->format & VISO_FORMAT_ISO) ? 37 : 32;
        if (i) {
            viso_write_wstring((uint16_t *) p, L"", 64, VISO_CHARSET_D); /* volume set ID */
            p += 128;
            viso_write_wstring((uint16_t *) p, L"", 64, VISO_CHARSET_A); /* publisher ID */
            p += 128;
            viso_write_wstring((uint16_t *) p, L"", 64, VISO_CHARSET_A); /* data preparer ID */
            p += 128;
            viso_write_wstring((uint16_t *) p, EMU_NAME_W L" " EMU_VERSION_W L" VIRTUAL ISO", 64, VISO_CHARSET_A); /* application ID */
            p += 128;
            viso_write_wstring((uint16_t *) p, L"", copyright_abstract_len >> 1, VISO_CHARSET_D); /* copyright file ID */
            p += copyright_abstract_len;
            viso_write_wstring((uint16_t *) p, L"", copyright_abstract_len >> 1, VISO_CHARSET_D); /* abstract file ID */
            p += copyright_abstract_len;
            if (viso->format & VISO_FORMAT_ISO) {
                viso_write_wstring((uint16_t *) p, L"", 18, VISO_CHARSET_D); /* bibliography file ID (ISO only) */
                p += 37;
            }
        } else {
            viso_write_string(p, "", 128, VISO_CHARSET_D); /* volume set ID */
            p += 128;
            viso_write_string(p, "", 128, VISO_CHARSET_A); /* publisher ID */
            p += 128;
            viso_write_string(p, "", 128, VISO_CHARSET_A); /* data preparer ID */
            p += 128;
            viso_write_string(p, EMU_NAME " " EMU_VERSION " VIRTUAL ISO", 128, VISO_CHARSET_A); /* application ID */
            p += 128;
            viso_write_string(p, "", copyright_abstract_len, VISO_CHARSET_D); /* copyright file ID */
            p += copyright_abstract_len;
            viso_write_string(p, "", copyright_abstract_len, VISO_CHARSET_D); /* abstract file ID */
            p += copyright_abstract_len;
            if (viso->format & VISO_FORMAT_ISO) {
                viso_write_string(p, "", 37, VISO_CHARSET_D); /* bibliography file ID (ISO only) */
                p += 37;
            }
        }

        len = viso_fill_time(p, now, viso->format, 1); /* volume created */
        memcpy(p + len, p, len);                       /* volume modified */
        p += len * 2;
        VISO_SKIP(p, len * 2); /* volume expires/effective */

        *p++ = 1; /* file structure version */
        *p++ = 0; /* unused */

        /* Blank the rest of the working sector. */
        memset(p, 0x00, viso->sector_size - (p - data));

        /* Write volume descriptor. */
        fwrite(data, viso->sector_size, 1, viso->tf.fp);

        /* Write El Torito boot descriptor. This is an awkward spot for
           that, but the spec requires it to be the second descriptor. */
        if (!i && eltorito_entry) {
            image_viso_log(viso->tf.log, "Writing El Torito boot descriptor for "
                           "entry [%08X]\n", eltorito_entry);

            p = data;
            if (!(viso->format & VISO_FORMAT_ISO))
                /* Sector offset (HSF only). */
                VISO_LBE_32(p, ftello64(viso->tf.fp) / viso->sector_size);
            /* Type. */
            *p++ = 0;
            /* Standard ID. */
            memcpy(p, (viso->format & VISO_FORMAT_ISO) ? "CD001" : "CDROM", 5);
            p += 5;
            *p++ = 1; /* version */

            memcpy(p, "EL TORITO SPECIFICATION", 24); /* identifier */
            p += 24;
            VISO_SKIP(p, 40);

            /* Save the boot catalog pointer's offset for later. */
            eltorito_offset = ftello64(viso->tf.fp) + (p - data);

            /* Blank the rest of the working sector. */
            memset(p, 0x00, viso->sector_size - (p - data));

            /* Write boot descriptor. */
            fwrite(data, viso->sector_size, 1, viso->tf.fp);
        }
    }

    /* Fill terminator. */
    p = data;
    if (!(viso->format & VISO_FORMAT_ISO))
        VISO_LBE_32(p, ftello64(viso->tf.fp) / viso->sector_size);    /* sector offset (HSF only) */
    *p++ = 0xff;                                                        /* type */
    memcpy(p, (viso->format & VISO_FORMAT_ISO) ? "CD001" : "CDROM", 5); /* standard ID */
    p += 5;
    *p++ = 1; /* version */

    /* Blank the rest of the working sector. */
    memset(p, 0x00, viso->sector_size - (p - data));

    /* Write terminator. */
    fwrite(data, viso->sector_size, 1, viso->tf.fp);

    /* We start seeing a pattern of padding to even sectors here.
       mkisofs does this, presumably for a very good reason... */
    int write = ftello64(viso->tf.fp) % (viso->sector_size * 2);
    if (write) {
        write = (viso->sector_size * 2) - write;
        memset(data, 0x00, write);
        fwrite(data, write, 1, viso->tf.fp);
    }

    /* Handle El Torito boot catalog. */
    if (eltorito_entry) {
        /* Write a pointer to this boot catalog to the boot descriptor. */
        *((uint32_t *) data) = cpu_to_le32(ftello64(viso->tf.fp) / viso->sector_size);
        viso_pwrite(data, eltorito_offset, 4, 1, viso->tf.fp);

        /* Fill boot catalog validation entry. */
        p    = data;
        *p++ = 0x01; /* header ID */
        *p++ = 0x00; /* platform */
        *p++ = 0x00; /* reserved */
        *p++ = 0x00;
        VISO_SKIP(p, 24);
        strncpy((char *) (p - 24), EMU_NAME, 24); /* ID string */
        *p++ = 0x00;                              /* checksum */
        *p++ = 0x00;
        *p++ = 0x55; /* key bytes */
        *p++ = 0xaa;

        /* Calculate checksum. */
        uint16_t eltorito_checksum = 0;
        for (int i = 0; i < (p - data); i += 2)
            eltorito_checksum -= le16_to_cpu(*((uint16_t *) &data[i]));
        *((uint16_t *) &data[28]) = cpu_to_le16(eltorito_checksum);

        /* Now fill the default boot entry. */
        *p++ = 0x88;          /* bootable flag */
        *p++ = eltorito_type; /* boot media type */
        *p++ = 0x00;          /* load segment */
        *p++ = 0x00;
        *p++ = 0x00; /* system type (is this even relevant?) */
        *p++ = 0x00; /* reserved */

        /* Save offsets to the boot catalog entry's offset and size fields for later. */
        eltorito_offset = ftello64(viso->tf.fp) + (p - data);

        /* Blank the rest of the working sector. This includes the sector count,
           ISO sector offset and 20-byte selection criteria fields at the end. */
        memset(p, 0x00, viso->sector_size - (p - data));

        /* Write boot catalog. */
        fwrite(data, viso->sector_size, 1, viso->tf.fp);

        /* Pad to the next even sector. */
        write = ftello64(viso->tf.fp) % (viso->sector_size * 2);
        if (write) {
            write = (viso->sector_size * 2) - write;
            memset(data, 0x00, write);
            fwrite(data, write, 1, viso->tf.fp);
        }

        /* Flag that we shouldn't hide the boot code directory if it contains other files. */
        if (eltorito_others_present)
            eltorito_dir = NULL;
    }

    /* Write each path table. */
    for (int i = 0; i <= ((max_vd << 1) | 1); i++) {
        image_viso_log(viso->tf.log, "Generating path table #%d:\n", i);

        /* Save this path table's start offset. */
        uint64_t pt_start = ftello64(viso->tf.fp);

        /* Write this table's sector offset to the corresponding volume descriptor. */
        uint32_t pt_temp     = pt_start / viso->sector_size;
        *((uint32_t *) data) = (i & 1) ? cpu_to_be32(pt_temp) : cpu_to_le32(pt_temp);
        viso_pwrite(data, viso->pt_meta_offsets[i >> 1] + 8 + (8 * (i & 1)), 4, 1, viso->tf.fp);

        /* Go through directories. */
        dir             = viso->root_dir;
        uint16_t pt_idx = 1;
        while (dir) {
            /* Ignore . and .. pseudo-directories, and hide the El Torito
               boot code directory if no other files are present in it. */
            if ((dir->name_short[0] == '.' && (dir->name_short[1] == '\0' || (dir->name_short[1] == '.' && dir->name_short[2] == '\0'))) || (dir == eltorito_dir)) {
                dir = dir->next_dir;
                continue;
            }

            image_viso_log(viso->tf.log, "[%08X] %s => %s\n", dir,
                           dir->path, ((i & 2) || (dir == viso->root_dir)) ? dir->basename :
                           dir->name_short);

            /* Save this directory's path table index and offset. */
            dir->pt_idx        = pt_idx;
            dir->pt_offsets[i] = ftello64(viso->tf.fp);

            /* Fill path table entry. */
            p = data;
            if (!(viso->format & VISO_FORMAT_ISO)) {
                *((uint32_t *) p) = 0; /* extent location (filled in later) */
                p += 4;
                *p++ = 0; /* extended attribute length */
                p++;      /* skip ID length for now */
            } else {
                p++;      /* skip ID length for now */
                *p++ = 0; /* extended attribute length */
                dir->pt_offsets[i] += p - data;
                *((uint32_t *) p) = 0; /* extent location (filled in later) */
                p += 4;
            }

            *((uint16_t *) p) = (i & 1) ? cpu_to_be16(dir->parent->pt_idx) : cpu_to_le16(dir->parent->pt_idx); /* parent directory number */
            p += 2;

            pt_temp = 5 * !(viso->format & VISO_FORMAT_ISO); /* directory ID length at offset 0 for ISO, 5 for HSF */
            if (dir == viso->root_dir) {                     /* directory ID length then ID for root... */
                data[pt_temp] = 1;
                *p            = 0x00;
            } else if (i & 2) { /* ...or Joliet... */
                data[pt_temp] = viso_fill_fn_joliet(p, dir, 255);
            } else { /* ...or short name */
                data[pt_temp] = strlen(dir->name_short);
                memcpy(p, dir->name_short, data[pt_temp]);
            }
            p += data[pt_temp];

            if ((p - data) & 1) /* padding for odd directory ID lengths */
                *p++ = 0x00;

            /* Write path table entry. */
            fwrite(data, p - data, 1, viso->tf.fp);

            /* Increment path table index and stop if it overflows. */
            if (++pt_idx == 0)
                break;

            /* Move on to the next directory. */
            dir = dir->next_dir;
        }

        /* Write this table's size to the corresponding volume descriptor. */
        pt_temp = ftello64(viso->tf.fp) - pt_start;
        p       = data;
        VISO_LBE_32(p, pt_temp);
        viso_pwrite(data, viso->pt_meta_offsets[i >> 1], 8, 1, viso->tf.fp);

        /* Pad to the next even sector. */
        write = ftello64(viso->tf.fp) % (viso->sector_size * 2);
        if (write) {
            write = (viso->sector_size * 2) - write;
            memset(data, 0x00, write);
            fwrite(data, write, 1, viso->tf.fp);
        }
    }

    /* Write directory records for each type. */
    int dir_type = VISO_DIR_CURRENT_ROOT;
    for (int i = 0; i <= max_vd; i++) {
        image_viso_log(viso->tf.log, "Generating directory record set #%d:\n", i);

        /* Go through directories. */
        dir = viso->root_dir;
        while (dir) {
            /* Hide the El Torito boot code directory if no other files are present in it. */
            if (dir == eltorito_dir) {
                dir = dir->next_dir;
                continue;
            }

            /* Pad to the next sector if required. */
            write = ftello64(viso->tf.fp) % viso->sector_size;
            if (write) {
                write = viso->sector_size - write;
                memset(data, 0x00, write);
                fwrite(data, write, 1, viso->tf.fp);
            }

            /* Save this directory's child record array's start offset. */
            uint64_t dir_start = ftello64(viso->tf.fp);

            /* Write this directory's child record array's sector offset to its record... */
            uint32_t dir_temp = dir_start / viso->sector_size;
            p                 = data;
            VISO_LBE_32(p, dir_temp);
            viso_pwrite(data, dir->dr_offsets[i] + 2, 8, 1, viso->tf.fp);

            /* ...and to its path table entries. */
            viso_pwrite(data, dir->pt_offsets[i << 1], 4, 1, viso->tf.fp);           /* little endian */
            viso_pwrite(data + 4, dir->pt_offsets[(i << 1) | 1], 4, 1, viso->tf.fp); /* big endian */

            if (i == max_vd) /* overwrite pt_offsets in the union if we no longer need them */
                dir->file = NULL;

            /* Go through entries in this directory. */
            entry = dir->first_child;
            while (entry) {
                /* Skip the El Torito boot code entry if present, or hide the
                   boot code directory if no other files are present in it. */
                if ((entry == eltorito_entry) || (entry == eltorito_dir))
                    goto next_entry;

                image_viso_log(viso->tf.log, "[%08X] %s => %s\n", entry, dir->path,
                               ((dir_type == VISO_DIR_PARENT) ? ".." :
                               ((dir_type < VISO_DIR_PARENT) ? "." :
                                (i ? entry->basename : entry->name_short))));

                /* Fill directory record. */
                viso_fill_dir_record(data, entry, viso, dir_type);

                /* Entries cannot cross sector boundaries, so pad to the next sector if needed. */
                write = viso->sector_size - (ftello64(viso->tf.fp) % viso->sector_size);
                if (write < data[0]) {
                    p = data + (viso->sector_size * 2) - write;
                    memset(p, 0x00, write);
                    fwrite(p, write, 1, viso->tf.fp);
                }

                /* Save this entry's record's offset. This overwrites name_short in the union. */
                entry->dr_offsets[i] = ftello64(viso->tf.fp);

                /* Write data related to the . and .. pseudo-subdirectories,
                   while advancing the current directory type. */
                if (dir_type < VISO_DIR_PARENT) {
                    /* Write a self-referential pointer to this entry. */
                    p = data + 2;
                    VISO_LBE_32(p, dir_temp);

                    dir_type = VISO_DIR_PARENT;
                } else if (dir_type == VISO_DIR_PARENT) {
                    /* Copy the parent directory's offset and size. The root directory's
                       parent size is a special, self-referential case handled later. */
                    viso_pread(data + 2, dir->parent->dr_offsets[i] + 2, 16, 1, viso->tf.fp);

                    dir_type = i ? VISO_DIR_JOLIET : VISO_DIR_REGULAR;
                }

                /* Write entry. */
                fwrite(data, data[0], 1, viso->tf.fp);
next_entry:
                /* Move on to the next entry, and stop if the end of this directory was reached. */
                entry = entry->next;
                if (entry && (entry->parent != dir))
                    break;
            }

            /* Write this directory's child record array's size to its parent and . records. */
            dir_temp = ftello64(viso->tf.fp) - dir_start;
            p        = data;
            VISO_LBE_32(p, dir_temp);
            viso_pwrite(data, dir->dr_offsets[i] + 10, 8, 1, viso->tf.fp);
            viso_pwrite(data, dir->first_child->dr_offsets[i] + 10, 8, 1, viso->tf.fp);
            if (dir->parent == dir) /* write size to .. on root directory as well */
                viso_pwrite(data, dir->first_child->next->dr_offsets[i] + 10, 8, 1, viso->tf.fp);

            /* Move on to the next directory. */
            dir_type = VISO_DIR_CURRENT;
            dir      = dir->next_dir;
        }

        /* Pad to the next even sector. */
        write = ftello64(viso->tf.fp) % (viso->sector_size * 2);
        if (write) {
            write = (viso->sector_size * 2) - write;
            memset(data, 0x00, write);
            fwrite(data, write, 1, viso->tf.fp);
        }
    }

    /* Allocate entry map for sector->file lookups. */
    size_t orig_sector_size = viso->sector_size;
    while (1) {
        image_viso_log(viso->tf.log, "Allocating entry map for %zu %zu-byte sectors\n",
                       viso->entry_map_size, viso->sector_size);
        viso->entry_map = (viso_entry_t **) calloc(viso->entry_map_size, sizeof(viso_entry_t *));
        if (viso->entry_map) {
            /* Successfully allocated. */
            break;
        } else {
            /* Blank data buffer for padding if this is the first run. */
            if (orig_sector_size == viso->sector_size)
                memset(data, 0x00, orig_sector_size);

            /* If we don't have enough memory, double the sector size. */
            viso->sector_size *= 2;
            if ((viso->sector_size < VISO_SECTOR_SIZE) || (viso->sector_size > (1 << 30))) /* give up if sectors become too large */
                goto end;

            /* Go through files, recalculating the entry map size. */
            size_t orig_entry_map_size = viso->entry_map_size;
            viso->entry_map_size       = 0;
            entry                      = viso->root_dir;
            while (entry) {
                if (!S_ISDIR(entry->stats.st_mode)) {
                    viso->entry_map_size += entry->stats.st_size / viso->sector_size;
                    if (entry->stats.st_size % viso->sector_size)
                        viso->entry_map_size++; /* round up to the next sector */
                }
                entry = entry->next;
            }
            if (viso->entry_map_size == orig_entry_map_size) /* give up if there was no change in map size */
                goto end;

            /* Pad metadata to the new size's next sector. */
            while (ftello64(viso->tf.fp) % viso->sector_size)
                fwrite(data, orig_sector_size, 1, viso->tf.fp);
        }
    }

    /* Start sector counts. */
    viso->metadata_sectors = ftello64(viso->tf.fp) / viso->sector_size;
    viso->all_sectors      = viso->metadata_sectors;

    /* Go through files, assigning sectors to them. */
    image_viso_log(viso->tf.log, "Assigning sectors to files:\n");
    size_t        base_factor  = viso->sector_size / orig_sector_size;
    viso_entry_t *prev_entry   = viso->root_dir;
    viso_entry_t **entry_map_p = viso->entry_map;
    entry                      = prev_entry->next;
    while (entry) {
        /* Skip this entry if it corresponds to a directory. */
        if (S_ISDIR(entry->stats.st_mode)) {
            /* Deallocate directory entries to save some memory. */
            prev_entry->next = entry->next;
            free(entry);
            entry = prev_entry->next;
            continue;
        }

        /* Write this file's base sector offset to its directory
           entries, unless this is the El Torito boot code entry,
           in which case, write offset and size to the boot entry. */
        if (entry == eltorito_entry) {
            /* Load the entire file if not emulating, or just the first virtual
               sector (which usually contains all the boot code) if emulating. */
            if (eltorito_type == 0x00) { /* non-emulation */
                uint32_t boot_size = entry->stats.st_size;
                if (boot_size % 512) /* round up */
                    boot_size += 512 - (boot_size % 512);
                *((uint16_t *) &data[0]) = cpu_to_le16(boot_size / 512);
            } else { /* emulation */
                *((uint16_t *) &data[0]) = cpu_to_le16(1);
            }
            *((uint32_t *) &data[2]) = cpu_to_le32(viso->all_sectors * base_factor);
            viso_pwrite(data, eltorito_offset, 6, 1, viso->tf.fp);
        } else {
            p = data;
            VISO_LBE_32(p, viso->all_sectors * base_factor);
            for (int i = 0; i <= max_vd; i++)
                viso_pwrite(data, entry->dr_offsets[i] + 2, 8, 1, viso->tf.fp);
        }

        /* Save this file's base offset. This overwrites dr_offsets in the union. */
        entry->data_offset = ((uint64_t) viso->all_sectors) * viso->sector_size;

        /* Determine how many sectors this file will take. */
        size_t size = entry->stats.st_size / viso->sector_size;
        if (entry->stats.st_size % viso->sector_size)
            size++; /* round up to the next sector */
        image_viso_log(viso->tf.log, "[%08X] %s => %zu + %zu sectors\n", entry,
                       entry->path, viso->all_sectors, size);

        /* Allocate sectors to this file. */
        viso->all_sectors += size;
        while (size-- > 0)
            *entry_map_p++ = entry;

        /* Move on to the next entry. */
        prev_entry = entry;
        entry      = entry->next;
    }

    /* Write final volume size to all volume descriptors. */
    p = data;
    VISO_LBE_32(p, viso->all_sectors);
    for (int i = 0; i < (sizeof(viso->vol_size_offsets) / sizeof(viso->vol_size_offsets[0])); i++)
        viso_pwrite(data, viso->vol_size_offsets[i], 8, 1, viso->tf.fp);

    /* Metadata processing is finished, read it back to memory. */
    image_viso_log(viso->tf.log, "Reading back %zu %zu-byte sectors of metadata\n",
                   viso->metadata_sectors, viso->sector_size);
    viso->metadata = (uint8_t *) calloc(viso->metadata_sectors, viso->sector_size);
    if (viso->metadata == NULL)
        goto end;
    fseeko64(viso->tf.fp, 0, SEEK_SET);
    size_t metadata_size = viso->metadata_sectors * viso->sector_size;
    size_t metadata_remain = metadata_size;
    while (metadata_remain > 0)
        metadata_remain -= fread(viso->metadata + (metadata_size - metadata_remain), 1, MIN(metadata_remain, viso->sector_size), viso->tf.fp);

    /* We no longer need the temporary file; close and delete it. */
    fclose(viso->tf.fp);
    viso->tf.fp = NULL;
#ifndef ENABLE_IMAGE_VISO_LOG
    remove(nvr_path(viso->tf.fn));
#endif

    /* All good. */
    *error = 0;

end:
    /* Set the function pointers. */
    viso->tf.priv = viso;
    if (!*error) {
        image_viso_log(viso->tf.log, "Initialized\n");

        viso->tf.read       = viso_read;
        viso->tf.get_length = viso_get_length;
        viso->tf.close      = viso_close;

        return &viso->tf;
    } else {
        image_viso_log(viso->tf.log, "Initialization failed\n");
        if (data)
            free(data);
        viso_close(&viso->tf);
        return NULL;
    }
}
