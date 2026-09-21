/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Locating PC-98 C-BUS board firmware across the layouts these
 *          ROM sets actually ship in.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <86box/mem.h>
#include <86box/86box.h>
#include <86box/pc98_rom.h>
#include <86box/rom.h>

int
pc98_rom_find_quiet(const char *const *candidates, char *out, size_t out_len)
{
    if (!candidates || !out || !out_len)
        return 0;

    for (const char *const *p = candidates; *p; p++) {
        /*
         * rom_present() goes through rom_fopen(), which tries the path
         * under every configured ROM root - so this covers both "a
         * different directory" and "a different layout inside it".
         */
        if (rom_present(*p)) {
            snprintf(out, out_len, "%s", *p);
            return 1;
        }
    }

    out[0] = '\0';
    return 0;
}

int
pc98_rom_find(const char *const *candidates, char *out, size_t out_len,
              const char *what)
{
    if (pc98_rom_find_quiet(candidates, out, out_len))
        return 1;

    /* Say what was looked for and where, rather than just failing. */
    pclog("[PC98] %s not found.  Tried, under every configured ROM "
          "directory:\n", what ? what : "ROM");
    if (candidates) {
        for (const char *const *p = candidates; *p; p++)
            pclog("[PC98]     %s\n", *p);
    }

    return 0;
}
