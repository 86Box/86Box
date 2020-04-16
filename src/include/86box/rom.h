/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the ROM image handler.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2018,2019 Fred N. van Kempen.
 */
#ifndef EMU_ROM_H
# define EMU_ROM_H


#define FLAG_INT	1
#define FLAG_INV	2
#define FLAG_AUX	4
#define FLAG_REP	8


#define bios_load_linear(a, b, c, d)			bios_load(a, NULL, b, c, d, 0)
#define bios_load_linearr(a, b, c, d)			bios_load(a, NULL, b, c, d, FLAG_REP)
#define bios_load_aux_linear(a, b, c, d)		bios_load(a, NULL, b, c, d, FLAG_AUX)
#define bios_load_linear_inverted(a, b, c, d)		bios_load(a, NULL, b, c, d, FLAG_INV)
#define bios_load_aux_linear_inverted(a, b, c, d)	bios_load(a, NULL, b, c, d, FLAG_INV | FLAG_AUX)
#define bios_load_interleaved(a, b, c, d, e)		bios_load(a, b, c, d, e, FLAG_INT)
#define bios_load_interleavedr(a, b, c, d, e)		bios_load(a, b, c, d, e, FLAG_INT | FLAG_REP)
#define bios_load_aux_interleaved(a, b, c, d, e)	bios_load(a, b, c, d, e, FLAG_INT | FLAG_AUX)


typedef struct {
    uint8_t		*rom;
    int			sz;
    uint32_t		mask;
    mem_mapping_t	mapping;
} rom_t;


extern uint8_t	rom_read(uint32_t addr, void *p);
extern uint16_t	rom_readw(uint32_t addr, void *p);
extern uint32_t	rom_readl(uint32_t addr, void *p);

extern FILE	*rom_fopen(wchar_t *fn, wchar_t *mode);
extern int	rom_getfile(wchar_t *fn, wchar_t *s, int size);
extern int	rom_present(wchar_t *fn);

extern int	rom_load_linear(wchar_t *fn, uint32_t addr, int sz,
				int off, uint8_t *ptr);
extern int	rom_load_interleaved(wchar_t *fnl, wchar_t *fnh, uint32_t addr,
				     int sz, int off, uint8_t *ptr);

extern int	bios_load(wchar_t *fn1, wchar_t *fn2, uint32_t addr, int sz,
			  int off, int flags);
extern int	bios_load_linear_combined(wchar_t *fn1, wchar_t *fn2,
					  int sz, int off);
extern int	bios_load_linear_combined2(wchar_t *fn1, wchar_t *fn2,
					   wchar_t *fn3, wchar_t *fn4, wchar_t *fn5,
					   int sz, int off);

extern int	rom_init(rom_t *rom, wchar_t *fn, uint32_t address, int size,
			 int mask, int file_offset, uint32_t flags);
extern int	rom_init_interleaved(rom_t *rom, wchar_t *fn_low,
				     wchar_t *fn_high, uint32_t address,
				     int size, int mask, int file_offset,
				     uint32_t flags);


#endif	/*EMU_ROM_H*/
