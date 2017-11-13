/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the Simple PNG image file format handler.
 *
 * Version:	@(#)win_png.h	1.0.1	2017/11/11
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef WIN_PNG_H
# define WIN_PNG_H


/* PNG defintions, as per the specification. */
#define PNG_COLOR_TYPE		0x02		/* 3-sample sRGB */
#define PNG_COMPRESSION_TYPE	0x00		/* deflate compression */
#define PNG_FILTER_TYPE		0x00		/* no filtering */
#define PNG_INTERLACE_MODE	0x00		/* no interlacing */

/* DEFLATE definition, as per RFC1950/1 specification. */
#define DEFL_MAX_BLKSZ		65535		/* DEFLATE max block size */


typedef struct {
    wchar_t	*name;		/* name of datafile */
    FILE	*fp;

    uint16_t	width,		/* configured with in pixels */
		height;		/* configured with in pixels */
    uint8_t	bpp,		/* configured bits per pixel */
		ctype;		/* configured color type */

    uint16_t	col,		/* current column */
		line,		/* current scanline */
		lwidth;		/* line width in bytes */
    uint8_t	cdepth,		/* color depth in bits */
		pwidth;		/* bytes per pixel */
    uint32_t	crc;		/* idat chunk crc */
    uint32_t	dcrc;		/* deflate data crc */

    uint32_t	bufcnt;		/* #bytes in block */
} png_t;


#ifdef __cplusplus
extern "C" {
#endif

extern void	png_putlong(uint8_t *ptr, uint32_t val, int *off);
extern void	png_close(png_t *png);
extern png_t	*png_create(wchar_t *fn, int width, int height, int bpp);
extern int	png_write(png_t *png, uint8_t *bitmap, uint32_t pixels);

#ifdef __cplusplus
}
#endif


#endif	/*WIN_PNG_H*/
