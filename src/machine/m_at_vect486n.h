// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef M_AT_VECT486N_H
#define M_AT_VECT486N_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Skip the mandatory MPU cache diagnostic when the functional CPU cache is
   disabled. Both revisions jump to F000:048C; replace that near jump with a
   short jump to POST 07h. The skipped byte preserves the POST 02h checksum.
   Patch only the emulator's loaded copy of a recognized original ROM. */
static inline int
vect486n_patch_cache_test(uint8_t *bios, size_t size)
{
    static const struct {
        const char *version;
        const char *date;
        size_t      offset;
        uint8_t     entry[11];
    } revisions[] = {
        { "T.04.02", "08/27/92", 0x84fb,
          { 0xb0, 0x06, 0xe6, 0x80, 0xe9, 0x8a, 0x7f, 0xb0, 0x07, 0xe6, 0x80 } },
        { "T.04.05", "10/11/94", 0x8599,
          { 0xb0, 0x06, 0xe6, 0x80, 0xe9, 0xec, 0x7e, 0xb0, 0x07, 0xe6, 0x80 } }
    };

    if (!bios || size != 0x10000)
        return 0;

    for (size_t i = 0; i < sizeof(revisions) / sizeof(revisions[0]); i++) {
        if (memcmp(bios + 0x93, revisions[i].version, 7) ||
            memcmp(bios + 0xfff5, revisions[i].date, 8) ||
            memcmp(bios + revisions[i].offset, revisions[i].entry, sizeof(revisions[i].entry)))
            continue;

        uint8_t *jump = bios + revisions[i].offset + 4;
        uint8_t  sum  = jump[0] + jump[1] + jump[2];
        jump[0]       = 0xeb;
        jump[1]       = 0x01;
        jump[2]       = sum - 0xeb - 0x01;
        return 1;
    }

    return 0;
}

#endif
