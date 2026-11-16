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
#ifndef EMU_FS_NAME_H
#define EMU_FS_NAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes one UTF-8 character and advances; invalid sequences decode as U+FFFD. */
extern uint32_t fs_utf8_next(const char **s);
/* Returns -1 if the string isn't well-formed UTF-8, 0 if it's ASCII, 1 if it has other characters. */
extern int fs_utf8_check(const char *s);
/* The largest length up to len that doesn't split a UTF-8 sequence; s must have at least len bytes. */
extern size_t fs_utf8_fit(const char *s, size_t len);
/* The string in composed form (NFC), which the caller frees, or NULL if it is already. Only macOS
   gives names in decomposed form: as HFS+ stores them, and through libarchive. */
extern char *fs_utf8_compose(const char *s);
/* Converts to UTF-16, never splitting a surrogate pair, and terminates the result if there is room.
   Returns the number of units written, not counting the terminator. */
extern size_t fs_utf8_to_utf16(uint16_t *dest, size_t size, const char *src);

/* Sets up the code page conversions; called before anything else. */
extern void fs_codepage_init(void);
/* DOS code pages: 437, and on Windows any OEM code page, elsewhere 932 too. Encodes a character in 1 or 2
   bytes, or returns 0 if the code page has no exact equivalent. */
extern int fs_codepage_encode(int cp, uint32_t c, uint8_t *out);
/* The same for a character that takes one byte; 0 otherwise. */
extern int fs_codepage_encode1(int cp, uint32_t c);
/* Whether a byte starts a double-byte character. */
extern int fs_codepage_is_lead(int cp, uint8_t b);
/* The largest length up to max that doesn't split a double-byte character. */
extern size_t fs_codepage_fit(int cp, const uint8_t *s, size_t len, size_t max);

/* Upper case for case-insensitive names: ASCII, Latin-1 and Greek. */
extern uint32_t fs_upcase(uint32_t c);

/* How a host name becomes a long name. */
typedef struct fs_long_name_rules_t {
    const char *forbidden;     /* ASCII characters replaced with underscores, besides control characters */
    int         max;           /* in UTF-16 units */
    int         keep_ext;      /* keep the extension when shortening or numbering */
    int         trim_trailing; /* drop trailing dots and spaces, as Windows does */
} fs_long_name_rules_t;

/* Converts a UTF-8 name to a long name, which the caller frees. */
extern uint16_t *fs_long_name(const char *utf8, const fs_long_name_rules_t *rules, int *len);

/* Long names in one directory, compared without case. The set points to the names, which must
   outlive it. */
typedef struct fs_name_set_t {
    struct fs_name_slot_t *table;
    size_t                 mask;
} fs_name_set_t;

extern int  fs_name_set_init(fs_name_set_t *set, size_t count);
extern void fs_name_set_free(fs_name_set_t *set);
/* Returns 1 if the name was added, 0 if a name differing only by case is already there. */
extern int fs_name_set_add(fs_name_set_t *set, const uint16_t *name, int len);
/* Adds a name, numbering it " (2)", " (3)"... before the extension while it clashes. May replace
   *name with a new allocation. Returns 0 if out of memory. */
extern int fs_name_set_add_unique(fs_name_set_t *set, uint16_t **name, int *len, const fs_long_name_rules_t *rules);

#ifdef __cplusplus
}
#endif

#endif /* EMU_FS_NAME_H */
