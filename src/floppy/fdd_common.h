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
 * Version:	@(#)fdd_common.h	1.0.2	2018/03/16
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef FDD_COMMON_H
# define FDD_COMMON_H


extern const uint8_t	fdd_holes[6];
extern const uint8_t	fdd_rates[6];
extern const double	fdd_bit_rates_300[6];
extern const uint8_t	fdd_max_sectors[8][6];
extern const uint8_t	fdd_dmf_r[21];


extern int 	fdd_get_gap3_size(int rate, int size, int sector);
extern uint8_t	fdd_sector_size_code(int size);
extern int	fdd_sector_code_size(uint8_t code);
extern int	fdd_bps_valid(uint16_t bps);
extern int	fdd_interleave(int sector, int skew, int spt);


#endif	/*FDD_COMMON_H*/
