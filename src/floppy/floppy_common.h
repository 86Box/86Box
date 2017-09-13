/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Shared code for all the floppy modules.
 *
 * Version:	@(#)floppy_common.h	1.0.1	2017/09/10
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef FLOPPY_COMMON_H
# define FLOPPY_COMMON_H


extern uint8_t	floppy_holes[6];
extern uint8_t	floppy_rates[6];
extern double	floppy_bit_rates_300[6];
extern uint8_t	floppy_max_sectors[8][6];
extern uint8_t	floppy_dmf_r[21];


extern int 	floppy_get_gap3_size(int rate, int size, int sector);
extern uint8_t	floppy_sector_size_code(int size);
extern int	floppy_sector_code_size(uint8_t code);
extern int	floppy_bps_valid(uint16_t bps);
extern int	floppy_interleave(int sector, int skew, int spt);


#endif	/*FLOPPY_COMMON_H*/
