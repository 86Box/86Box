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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    set->slots = (struct fs_name_slot_t *) calloc(size, sizeof(struct fs_name_slot_t));
    set->mask  = size - 1;
    return set->slots != NULL;
}

void
fs_name_set_free(fs_name_set_t *set)
{
    free(set->slots);
    set->slots = NULL;
}

int
fs_name_set_add(fs_name_set_t *set, const uint16_t *name, int len)
{
    size_t i = hash_name(name, len) & set->mask;
    while (set->slots[i].name) {
        if (names_equal(set->slots[i].name, set->slots[i].len, name, len))
            return 0;
        i = (i + 1) & set->mask;
    }
    set->slots[i].name = name;
    set->slots[i].len  = len;
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
