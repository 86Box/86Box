/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Locating PC-98 C-BUS board firmware.
 *
 *          86Box already searches every configured ROM root for a given
 *          relative path, so a user's ROM directory does not have to be the
 *          one next to the executable.  What it does not do is try more than
 *          one relative path, and PC-98 ROM sets are laid out several
 *          different ways in the wild - MAME-style per-board directories
 *          (pc9801_26/, pc9801_27/, pc9801_86/), everything dropped into one
 *          pc9801/ directory, or the Yamaha sample ROM under sound/.
 *
 *          pc98_rom_find() walks a candidate list against every configured
 *          root and reports which one it used, or logs all of them when the
 *          file is nowhere to be found, so a missing ROM says where it was
 *          looked for instead of the board silently not appearing.
 */
#ifndef EMU_PC98_ROM_H
#define EMU_PC98_ROM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC98_ROM_PATH_MAX 512

typedef enum pc98_model_t {
    PC98_MODEL_VX  = 0,
    PC98_MODEL_BX4 = 1
} pc98_model_t;

#define PC98_BX4_BANK_SIZE  0x8000u
#define PC98_BX4_BANKS_SIZE (8u * PC98_BX4_BANK_SIZE)
int      pc98_ipl_available_for_model(pc98_model_t model);
uint8_t *pc98_ipl_load_for_model(pc98_model_t model);
uint32_t pc98_ipl_window_offset_for_model(uint32_t addr, pc98_model_t model);
/* Exact, complete getitf98 dump; never falls back to VX firmware. */
uint8_t *pc98_bx4_banks_load(void);

/* Load VX firmware. Returned IPL is heap owned.
 * Availability and loading use the same exact sizes; partial sets fail closed. */
int      pc98_rom_read_exact(const char *path, uint8_t *dst, size_t bytes);
int      pc98_rom_has_size(const char *path, size_t bytes);
int      pc98_ipl_available(void);
uint8_t *pc98_ipl_load(void);
uint32_t pc98_ipl_window_offset(uint32_t addr);

/*
 * candidates is a NULL-terminated list of relative paths, most preferred
 * first.  On success `out` receives the one that exists and 1 is returned.
 * `what` names the ROM for the log line.
 */
int pc98_rom_find(const char *const *candidates, char *out, size_t out_len,
                  const char *what);

/*
 * The same, but silent when nothing is found - for ROMs a board can do
 * without, where the caller wants to word its own message.
 */
int pc98_rom_find_quiet(const char *const *candidates, char *out,
                        size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /*EMU_PC98_ROM_H*/
