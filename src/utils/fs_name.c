/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          UTF-8 and UTF-16 file name helpers shared by the FAT volume
 *          generator and the virtual ISO.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2026 RichardG.
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE /* dladdr() */
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#    include <CoreFoundation/CoreFoundation.h>
#endif
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#elif defined(USE_ICONV)
#    include <errno.h>
#    include <iconv.h>
#    ifdef __GLIBC__
#        include <dlfcn.h>
#        include <limits.h>
#        include <sys/stat.h>
#        include <gnu/libc-version.h>
#    endif
#endif
#include <86box/fs_name.h>

struct fs_name_slot_t {
    const uint16_t *name;
    int             len;
};

uint32_t
fs_utf8_next(const char **s)
{
    const uint8_t *p = (const uint8_t *) *s;
    uint32_t       c = *p++;
    int            n = 0;

    if ((c >= 0xf0) && (c < 0xf8)) {
        c &= 0x07;
        n = 3;
    } else if (c >= 0xe0) {
        c &= 0x0f;
        n = 2;
    } else if (c >= 0xc0) {
        c &= 0x1f;
        n = 1;
    } else if (c >= 0x80) {
        c = 0xfffd;
    }
    for (; n > 0; n--) {
        if ((*p & 0xc0) != 0x80) {
            c = 0xfffd;
            break;
        }
        c = (c << 6) | (*p++ & 0x3f);
    }
    if (c > 0x10ffff)
        c = 0xfffd;
    *s = (const char *) p;
    return c;
}

int
fs_utf8_check(const char *s)
{
    const uint8_t *p         = (const uint8_t *) s;
    int            non_ascii = 0;

    while (*p) {
        uint32_t c = *p++;
        int      n;
        uint32_t min;
        if (c < 0x80)
            continue;
        if ((c >= 0xc2) && (c <= 0xdf)) {
            n   = 1;
            min = 0x80;
        } else if ((c >= 0xe0) && (c <= 0xef)) {
            n   = 2;
            min = 0x800;
        } else if ((c >= 0xf0) && (c <= 0xf4)) {
            n   = 3;
            min = 0x10000;
        } else {
            return -1;
        }
        c &= 0x3f >> n;
        for (; n; n--) {
            if ((*p & 0xc0) != 0x80)
                return -1;
            c = (c << 6) | (*p++ & 0x3f);
        }
        if ((c < min) || (c > 0x10ffff) || ((c >= 0xd800) && (c <= 0xdfff)))
            return -1;
        non_ascii = 1;
    }
    return non_ascii;
}

size_t
fs_utf8_fit(const char *s, size_t len)
{
    while ((len > 0) && ((s[len] & 0xc0) == 0x80))
        len--;
    return len;
}

char *
fs_utf8_compose(const char *s)
{
#ifdef __APPLE__
    char       *out    = NULL;
    CFStringRef string = CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingUTF8);
    if (!string)
        return NULL;
    CFMutableStringRef composed = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, string);
    CFRelease(string);
    if (!composed)
        return NULL;
    CFStringNormalize(composed, kCFStringNormalizationFormC);
    CFIndex size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(composed), kCFStringEncodingUTF8) + 1;
    out          = (char *) malloc(size);
    if (out && (!CFStringGetCString(composed, out, size, kCFStringEncodingUTF8) || !strcmp(out, s))) {
        free(out);
        out = NULL;
    }
    CFRelease(composed);
    return out;
#else
    (void) s;
    return NULL;
#endif
}

size_t
fs_utf8_to_utf16(uint16_t *dest, size_t size, const char *src)
{
    size_t len = 0;

    while (*src) {
        uint32_t c = fs_utf8_next(&src);
        if (c >= 0x10000) {
            if ((len + 2) > size)
                break;
            c -= 0x10000;
            dest[len++] = 0xd800 | (c >> 10);
            dest[len++] = 0xdc00 | (c & 0x3ff);
        } else {
            if ((len + 1) > size)
                break;
            dest[len++] = c;
        }
    }
    if (len < size)
        dest[len] = 0;
    return len;
}

uint32_t
fs_upcase(uint32_t c)
{
    if ((c >= 'a') && (c <= 'z'))
        return c - 0x20;
    if ((c >= 0xe0) && (c <= 0xfe) && (c != 0xf7))
        return c - 0x20;
    if ((c >= 0x3b1) && (c <= 0x3c9) && (c != 0x3c2))
        return c - 0x20;
    return c;
}

/* Where the extension starts, or len if there is none; a leading dot doesn't start one. */
static int
ext_start(const uint16_t *name, int len)
{
    for (int i = len - 1; i > 0; i--) {
        if (name[i] == '.')
            return i;
    }
    return len;
}

/* Shortens a name to at most max units, keeping an extension that takes up to half of them
   if asked, and never splitting a surrogate pair. Returns the new length. */
static int
shorten(uint16_t *name, int len, int max, int keep_ext)
{
    if (len <= max)
        return len;

    int ext_len = keep_ext ? (len - ext_start(name, len)) : 0;
    if (ext_len > (max / 2))
        ext_len = 0;

    int stem = max - ext_len;
    if ((name[stem] >= 0xdc00) && (name[stem] <= 0xdfff))
        stem--;
    memmove(name + stem, name + len - ext_len, ext_len * sizeof(uint16_t));
    return stem + ext_len;
}

uint16_t *
fs_long_name(const char *utf8, const fs_long_name_rules_t *rules, int *out_len)
{
    /* Every UTF-16 unit takes at least one UTF-8 byte. */
    uint16_t   *name = (uint16_t *) malloc((strlen(utf8) + 1) * sizeof(uint16_t));
    int         len  = 0;
    const char *p    = utf8;

    if (!name)
        return NULL;
    while (*p) {
        uint32_t c = fs_utf8_next(&p);
        if ((c < 0x20) || ((c < 0x80) && strchr(rules->forbidden, c)))
            c = '_';
        if (c >= 0x10000) {
            c -= 0x10000;
            name[len++] = 0xd800 | (c >> 10);
            name[len++] = 0xdc00 | (c & 0x3ff);
        } else {
            name[len++] = c;
        }
    }

    len = shorten(name, len, rules->max, rules->keep_ext);
    if (rules->trim_trailing) {
        while (len && ((name[len - 1] == '.') || (name[len - 1] == ' ')))
            len--;
    }
    if (!len)
        name[len++] = '_';

    *out_len = len;
    return name;
}

static uint32_t
hash_name(const uint16_t *name, int len)
{
    uint32_t h = 2166136261U;
    for (int i = 0; i < len; i++)
        h = (h ^ fs_upcase(name[i])) * 16777619U;
    return h;
}

static int
names_equal(const uint16_t *a, int a_len, const uint16_t *b, int b_len)
{
    if (a_len != b_len)
        return 0;
    for (int i = 0; i < a_len; i++) {
        if (fs_upcase(a[i]) != fs_upcase(b[i]))
            return 0;
    }
    return 1;
}

int
fs_name_set_init(fs_name_set_t *set, size_t count)
{
    size_t size = 16;
    while (size < (count * 2))
        size <<= 1;
    set->table = (struct fs_name_slot_t *) calloc(size, sizeof(struct fs_name_slot_t));
    set->mask  = size - 1;
    return set->table != NULL;
}

void
fs_name_set_free(fs_name_set_t *set)
{
    free(set->table);
    set->table = NULL;
}

int
fs_name_set_add(fs_name_set_t *set, const uint16_t *name, int len)
{
    size_t i = hash_name(name, len) & set->mask;
    while (set->table[i].name) {
        if (names_equal(set->table[i].name, set->table[i].len, name, len))
            return 0;
        i = (i + 1) & set->mask;
    }
    set->table[i].name = name;
    set->table[i].len  = len;
    return 1;
}

/* Makes "name (n).ext", shortening the name to make room. */
static uint16_t *
number_name(const uint16_t *orig, int orig_len, uint32_t n, const fs_long_name_rules_t *rules, int *out_len)
{
    char      suffix[16];
    int       suffix_len = snprintf(suffix, sizeof(suffix), " (%u)", n);
    uint16_t *name       = (uint16_t *) malloc((orig_len + suffix_len) * sizeof(uint16_t));

    if (!name)
        return NULL;
    memcpy(name, orig, orig_len * sizeof(uint16_t));
    int len = shorten(name, orig_len, rules->max - suffix_len, rules->keep_ext);
    int ext = rules->keep_ext ? ext_start(name, len) : len;

    memmove(name + ext + suffix_len, name + ext, (len - ext) * sizeof(uint16_t));
    for (int i = 0; i < suffix_len; i++)
        name[ext + i] = suffix[i];
    *out_len = len + suffix_len;
    return name;
}

int
fs_name_set_add_unique(fs_name_set_t *set, uint16_t **name, int *len, const fs_long_name_rules_t *rules)
{
    if (fs_name_set_add(set, *name, *len))
        return 1;

    for (uint32_t n = 2;; n++) {
        int       new_len;
        uint16_t *numbered = number_name(*name, *len, n, rules, &new_len);
        if (!numbered)
            return 0;
        if (fs_name_set_add(set, numbered, new_len)) {
            free(*name);
            *name = numbered;
            *len  = new_len;
            return 1;
        }
        free(numbered);
    }
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

/* DOS code pages. Code page 437 has its table; others come from the host: any OEM code page on Windows,
   and Shift-JIS (932) through iconv elsewhere. */
int
fs_codepage_encode(int cp, uint32_t c, uint8_t *out)
{
    if (c < 0x80) {
        out[0] = c;
        return 1;
    }
    if (cp == 437) {
        for (int i = 0; i < 128; i++) {
            if (cp437[i] == c) {
                out[0] = 0x80 + i;
                return 1;
            }
        }
        return 0;
    }
    if ((c > 0x10ffff) || ((c >= 0xd800) && (c <= 0xdfff)))
        return 0;

#ifdef _WIN32
    WCHAR w[2];
    int   wlen = 1;
    BOOL  lossy = FALSE;
    char  buf[8];
    if (c >= 0x10000) {
        w[0] = 0xd800 | ((c - 0x10000) >> 10);
        w[1] = 0xdc00 | ((c - 0x10000) & 0x3ff);
        wlen = 2;
    } else {
        w[0] = c;
    }
    int n = WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, w, wlen, buf, sizeof(buf), NULL, &lossy);
    if ((n <= 0) || (n > 2) || lossy)
        return 0;
    memcpy(out, buf, n);
    return n;
#elif defined(USE_ICONV)
    if (cp != 932)
        return 0;
    /* The character as UTF-8. */
    char   in[4];
    size_t in_len;
    if (c < 0x800) {
        in[0]  = 0xc0 | (c >> 6);
        in[1]  = 0x80 | (c & 0x3f);
        in_len = 2;
    } else if (c < 0x10000) {
        in[0]  = 0xe0 | (c >> 12);
        in[1]  = 0x80 | ((c >> 6) & 0x3f);
        in[2]  = 0x80 | (c & 0x3f);
        in_len = 3;
    } else {
        in[0]  = 0xf0 | (c >> 18);
        in[1]  = 0x80 | ((c >> 12) & 0x3f);
        in[2]  = 0x80 | ((c >> 6) & 0x3f);
        in[3]  = 0x80 | (c & 0x3f);
        in_len = 4;
    }
    iconv_t cd = iconv_open("CP932", "UTF-8");
    if (cd == (iconv_t) -1)
        return 0;
    char   buf[8];
    char  *inp      = in;
    char  *outp     = buf;
    size_t out_left = sizeof(buf);
    size_t ret      = iconv(cd, &inp, &in_len, &outp, &out_left);
    iconv_close(cd);
    size_t n = sizeof(buf) - out_left;
    /* Characters iconv substitutes count as not there. */
    if ((ret != 0) || in_len || (n == 0) || (n > 2))
        return 0;
    memcpy(out, buf, n);
    return (int) n;
#else
    return 0;
#endif
}

void
fs_codepage_init(void)
{
#if defined(USE_ICONV) && defined(__GLIBC__)
    /* In an AppImage on a system older than it, the C library is the bundled one, which would load the
       system's converters, built for another version of it. Put the ones bundled next to it first in
       GCONV_PATH, ahead of any the user set, for as long as it reads its list of converters, which it
       does once; then put GCONV_PATH back, so that programs run from here get the environment they had. */
    const char *appdir = getenv("APPDIR");
    const char *user   = getenv("GCONV_PATH");
    Dl_info     info;
    char        path[PATH_MAX];
    struct stat st;

    if (!appdir || !appdir[0] || !dladdr((void *) gnu_get_libc_version, &info) || !info.dli_fname ||
        strncmp(info.dli_fname, appdir, strlen(appdir)) || (info.dli_fname[strlen(appdir)] != '/'))
        return;
    snprintf(path, sizeof(path), "%s", info.dli_fname);
    char *slash = strrchr(path, '/');
    snprintf(slash, sizeof(path) - (slash - path), "/gconv");
    if (stat(path, &st) || !S_ISDIR(st.st_mode))
        return;

    /* setenv() may free the string getenv() returned. */
    char  *saved    = user ? strdup(user) : NULL;
    size_t len      = strlen(path) + (saved ? (strlen(saved) + 1) : 0) + 1;
    char  *combined = (char *) malloc(len);
    if (!combined || (user && !saved)) {
        free(combined);
        free(saved);
        return;
    }
    if (saved && saved[0])
        snprintf(combined, len, "%s:%s", path, saved);
    else
        snprintf(combined, len, "%s", path);

    setenv("GCONV_PATH", combined, 1);
    iconv_t cd = iconv_open("CP932", "UTF-8");
    if (cd != (iconv_t) -1)
        iconv_close(cd);
    if (saved)
        setenv("GCONV_PATH", saved, 1);
    else
        unsetenv("GCONV_PATH");

    free(combined);
    free(saved);
#endif
}

int
fs_codepage_encode1(int cp, uint32_t c)
{
    uint8_t out[2];
    return (fs_codepage_encode(cp, c, out) == 1) ? out[0] : 0;
}

int
fs_codepage_is_lead(int cp, uint8_t b)
{
    if ((cp == 437) || (b < 0x80))
        return 0;
#ifdef _WIN32
    return IsDBCSLeadByteEx(cp, b);
#else
    return (cp == 932) && (((b >= 0x81) && (b <= 0x9f)) || ((b >= 0xe0) && (b <= 0xfc)));
#endif
}

size_t
fs_codepage_fit(int cp, const uint8_t *s, size_t len, size_t max)
{
    size_t i = 0;

    while (i < len) {
        size_t n = (fs_codepage_is_lead(cp, s[i]) && ((i + 1) < len)) ? 2 : 1;
        if ((i + n) > max)
            break;
        i += n;
    }
    return i;
}
