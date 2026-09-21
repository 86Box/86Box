/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the ROM image handler.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2018-2019 Fred N. van Kempen.
 */
#ifndef EMU_ROM_H
#define EMU_ROM_H

#define FLAG_INT                                  1
#define FLAG_INV                                  2
#define FLAG_AUX                                  4
#define FLAG_REP                                  8

#define bios_load_linear(a, b, c, d)              bios_load(a, NULL, b, c, d, 0)
#define bios_load_linearr(a, b, c, d)             bios_load(a, NULL, b, c, d, FLAG_REP)
#define bios_load_aux_linear(a, b, c, d)          bios_load(a, NULL, b, c, d, FLAG_AUX)
#define bios_load_linear_inverted(a, b, c, d)     bios_load(a, NULL, b, c, d, FLAG_INV)
#define bios_load_aux_linear_inverted(a, b, c, d) bios_load(a, NULL, b, c, d, FLAG_INV | FLAG_AUX)
#define bios_load_interleaved(a, b, c, d, e)      bios_load(a, b, c, d, e, FLAG_INT)
#define bios_load_interleavedr(a, b, c, d, e)     bios_load(a, b, c, d, e, FLAG_INT | FLAG_REP)
#define bios_load_aux_interleaved(a, b, c, d, e)  bios_load(a, b, c, d, e, FLAG_INT | FLAG_AUX)

typedef struct rom_t {
    uint8_t      *rom;
    int           sz;
    uint32_t      mask;
    mem_mapping_t mapping;
} rom_t;

typedef struct rom_path_t {
    char               path[1024];
    struct rom_path_t *next;
} rom_path_t;

extern rom_path_t rom_paths;
extern rom_path_t asset_paths;

extern void asset_add_path(const char *path);

extern void rom_add_path(const char *path);

extern uint8_t  rom_read(uint32_t addr, void *priv);
extern uint16_t rom_readw(uint32_t addr, void *priv);
extern uint32_t rom_readl(uint32_t addr, void *priv);

extern void rom_write(uint32_t addr, uint8_t val, void *priv);
extern void rom_writew(uint32_t addr, uint16_t val, void *priv);
extern void rom_writel(uint32_t addr, uint32_t val, void *priv);

extern void  asset_get_full_path(char *dest, const char *fn);

extern void  rom_get_full_path(char *dest, const char *fn);

extern FILE *asset_fopen(const char *fn, char *mode);
extern int   asset_getfile(const char *fn, char *s, int size);
extern int   asset_present(const char *fn);

extern FILE *rom_fopen(const char *fn, char *mode);
extern int   rom_getfile(const char *fn, char *s, int size);
extern int   rom_present(const char *fn);

extern int rom_load_linear_oddeven(const char *fn, uint32_t addr, int sz,
                                   int off, uint8_t *ptr);
extern int rom_load_linear(const char *fn, uint32_t addr, int sz,
                           int off, uint8_t *ptr);
extern int rom_load_interleaved(const char *fnl, const char *fnh, uint32_t addr,
                                int sz, int off, uint8_t *ptr);

extern uint8_t  bios_read(uint32_t addr, void *priv);
extern uint16_t bios_readw(uint32_t addr, void *priv);
extern uint32_t bios_readl(uint32_t addr, void *priv);

extern int bios_load(const char *fn1, const char *fn2, uint32_t addr, int sz,
                     int off, int flags);
extern int bios_load_linear_combined(const char *fn1, const char *fn2,
                                     int sz, int off);
extern int bios_load_linear_combined2(const char *fn1, const char *fn2,
                                      const char *fn3, const char *fn4, const char *fn5,
                                      int sz, int off);
extern int bios_load_linear_combined2_ex(const char *fn1, const char *fn2,
                                         const char *fn3, const char *fn4, const char *fn5,
                                         int sz, int off);

extern int rom_init(rom_t *rom, const char *fn, uint32_t address, int size,
                    int mask, int file_offset, uint32_t flags);
extern int rom_init_oddeven(rom_t *rom, const char *fn, uint32_t address, int size,
                            int mask, int file_offset, uint32_t flags);
extern int rom_init_interleaved(rom_t *rom, const char *fn_low,
                                const char *fn_high, uint32_t address,
                                int size, int mask, int file_offset,
                                uint32_t flags);

#endif /*EMU_ROM_H*/
