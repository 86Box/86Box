/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Simple PNG image file format handler.
 *
 *		Adapted for use with 86Box. Writing of (very basic) PNG
 *		image files, mostly intended to be used for creating
 *		screenshots. We only support sRGB format (3-byte pixel
 *		data) with a color depth of 8 bits per sample- so, 24bpp.
 *
 * NOTES:	This is a stripped-down version of my full library for PNG
 *		image file format support. All the 'reading' code has been
 *		removed, and some other stuff we don't need in 86Box.
 *
 * TODO:	Compression is currently not supported, until I figure out
 *		how ZLIB works so I can interface with it here.
 *
 * Version:	@(#)win_png.c	1.0.1	2017/11/11
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../86Box.h"
#include "../plat.h"
#include "win_png.h"


#ifdef CRC_DYNAMIC
static uint32_t	crc_table[256];
static int	crc_tblok = 0;


/* Pre-calculate the CRC table. */
static void
crc_create(void)
{
    uint32_t crc;
    int i, k;

    for (i=0; i<256; i++) {
	crc = (uint32_t)i;
	for (k=0; k<8; k++) {
		if (crc & 1)
			crc = 0xedb88320UL ^ (crc >> 1);
		  else
			crc >>= 1;
	}
	crc_table[i] = crc;
    }

    crc_tblok = 1;
}
   

#else
static uint32_t	crc_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
    0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
    0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
    0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
    0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
    0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
    0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
    0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
    0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
    0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
    0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
    0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
    0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
    0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};
#endif


/* Standard Network CRC32. */
static uint32_t
crc_upd(uint32_t crc, uint8_t *bufp, int buflen)
{
    int i;

#ifdef CRC_DYNAMIC
    if (! crc_tblok)
	crc_create();
#endif

    for (i=0; i<buflen; i++)
	crc = crc_table[(crc ^ *bufp++) & 0xff] ^ (crc >> 8);

    return(crc);
}


static uint32_t
crc_init(void)
{
    return(0xffffffffUL);
}


static uint32_t
crc_finish(uint32_t crc)
{
    return(crc ^ 0xffffffffUL);
}


/* Perform CRC over the compressed data. */
static uint32_t
png_dcrc(uint32_t state, const uint8_t *bufp, uint32_t buflen)
{
    uint16_t s1, s2;
    uint32_t i;

    s1 = (uint16_t)(state & 0xffff);
    s2 = (uint16_t)(state >> 16);

    for (i=0; i<buflen; i++) {
	s1 = ((uint32_t)s1 + bufp[i]) % 65521;
	s2 = ((uint32_t)s2 + s1) % 65521;
    }

    return((uint32_t)s2 << 16 | s1);
}


/* Finish the PNG file by writing the IEND record. */
static int
png_wriend(png_t *png)
{
    uint8_t buff[12];
    int i = 0;

    /* Create the IEND chunk. */
    png_putlong(&buff[i], 0, &i);
    memcpy(&buff[i], "IEND", 4); i += 4;
    png_putlong(&buff[i], 0xae426082, &i);
    if (fwrite(buff, 1, sizeof(buff), png->fp) != sizeof(buff)) {
	pclog("PNG(%ls): could not write trailer chunk!\n", png->name);
	return(-1);
    }

    /* All is good! */
    return(0);
}


/* Write an IDAT chunk header to the file. */
static uint32_t
png_wridat(png_t *png, uint32_t pixels)
{
    uint8_t buff[10];
    uint32_t cnt;
    int i, fluff;

    /*
     * We must know the size of the chunk we are about to write,
     * and that is not straightforward. If compression is enabled,
     * we won't know until we're done with the chunk, so we would
     * have to just write a dummy, and then go back afterwards and
     * fix up the chunk size.
     *
     * Without compression, we could do the same, or we can do some
     * calculations on how much data we expect to generate, and use
     * that. For now, we'll go with the latter option.
     */
    cnt = png->lwidth;				/* bytes per scanline */
    cnt += 1;					/* filter type per line */
    cnt *= (pixels/png->width);			/* number of full lines */
    cnt += (pixels%png->width);			/* number of pixels */

    /*
     * We now have the total number of pixel bytes to write.
     *
     * All that is left is adding the expected overhead (since we
     * are dealing with fixed-size blocks and their headers) and
     * that should give us the grand total..
     */
    i = (cnt / DEFL_MAX_BLKSZ);			/* number of full blocks */
    if ((cnt % DEFL_MAX_BLKSZ) != 0) i++;	/* incomplete block */
    cnt += (i * 5);				/* block header is 5 bytes */

    /*
     * We are not using compression, so we must account for
     * the extra data we insert to fake that. Below you will
     * see the two DEFLATE bytes (this makes it look like a
     * real deflate stream.)  Upon closing, we also add the
     * four compressed-data CRC bytes, so, a total of 6.
     */
    fluff = 6;

    i = 0;
    png_putlong(&buff[i], cnt+fluff, &i);	/* IDAT chunk */
    memcpy(&buff[i], "IDAT", 4); i += 4;
    buff[i++] = 0x08;				/* deflate data: zlib */
    buff[i++] = 0x1d;				/* zlib method number */
    if (fwrite(buff, 1, i, png->fp) != i) {
	pclog("PNG(%ls): unable to write IDAT header!\n", png->name);
	png_close(png);
	return(-1);
    }

    /* Initialize IDAT CRC. */
    png->crc = crc_upd(png->crc, &buff[4], i-4);

    /* Initialize for writing data. */
    png->bufcnt = 0;				/* no data in buffer */

    return(cnt);
}


/*
 * API: Write (more) data to the PNG file.
 *
 * To keep things semi-simple, we generate a single IDAT
 * chunk for each write. So, if the application calls us
 * with the entire image, we have one IDAT. If it calls
 * us multiple times (for example, once per scanline), we
 * get one IDAT per scanline.
 */
int
png_write(png_t *png, uint8_t *bitmap, uint32_t pixels)
{
    uint8_t buff[10];
    uint32_t cnt, n;
    uint16_t s;
    int i;

    /* Do they want to close up? */
    if (bitmap == NULL && pixels == 0) {
	i = png_wriend(png);
	return(i);
    }

    if (png->line >= png->height) {
	pclog("PNG(%ls): cannot write %u pixels!\n", png->name, pixels);
	png_close(png);
	return(-1);
    }

    /* Start a new IDAT chunk. */
    cnt = png_wridat(png, pixels);

    /* Loop, writing all pixels to the file. */
    while (cnt > 0) {
	if (png->bufcnt == 0) {
		/*
		 * Initialize a new block header.
		 *
		 * Bit0:	LastBlock
		 * Bits[2:1]:	compression type (00=stored)
		 * Len:		block size (MSB)
		 * Nlen:	block size, negated (MSB)
		 */
		s = (cnt < DEFL_MAX_BLKSZ) ? cnt : DEFL_MAX_BLKSZ;
		i = 0;
		buff[i++] = (cnt <= DEFL_MAX_BLKSZ) ? 1:0;
		buff[i++] = ((s >> 8) & 0xff);
		buff[i++] = (s & 0xff);
		buff[i++] = ((s >> 8) ^ 0xff);
		buff[i++] = (s ^ 0xff);
		if (fwrite(buff, 1, i, png->fp) != i) {
			pclog("PNG(%ls): block header write failed!\n",
							png->name);
			png_close(png);
			return(-1);
		}
		png->crc = crc_upd(png->crc, buff, i);
		cnt -= i;
	}

	if (png->col == 0) {
		/* Beginning of line, write filter method. */
		buff[0] = 0x00;
		if (fwrite(buff, 1, 1, png->fp) != 1) {
			pclog("PNG(%ls): cannot write filter?!\n", png->name);
			png_close(png);
			return(-1);
		}
		png->crc = crc_upd(png->crc, buff, 1);
		png->dcrc = png_dcrc(png->dcrc, buff, 1);
		png->bufcnt++;
		cnt--;
	}

	/* See how many pixels we can write for this scanline. */
	n = cnt;
	if ((png->lwidth - png->col) < n)
		n = (png->lwidth - png->col);
	if ((DEFL_MAX_BLKSZ - png->bufcnt) < n)
		n = (DEFL_MAX_BLKSZ - png->bufcnt);

	/* Write the pixel data for this block. */
	if (fwrite(bitmap, 1, n, png->fp) != n) {
		pclog("PNG(%ls): cannot write pixeldata?!\n", png->name);
		png_close(png);
		return(-1);
	}

	/* Update the CRCs for these pixels. */
	png->crc = crc_upd(png->crc, bitmap, n);
	png->dcrc = png_dcrc(png->dcrc, bitmap, n);

	/* Update stats. */
	bitmap += n;
	png->bufcnt += n;
	if (png->bufcnt == DEFL_MAX_BLKSZ)
		png->bufcnt = 0;
	cnt -= n;

	png->col += n;
	if (png->col == png->lwidth) {
		png->col = 0;
		if (++png->line == png->height) {
			if (cnt > 0) {
				pclog("PNG(%ls): done, more data?!\n",
							png->name);
				png_close(png);
				return(-1);
			}
		}
	}
    }

    /* Write the CRCs. */
    i = 0;
    png_putlong(buff, png->dcrc, &i);
    png->crc = crc_finish(crc_upd(png->crc, buff, i));
    png_putlong(&buff[i], png->crc, &i);
    if (fwrite(buff, 1, i, png->fp) != i) {
	pclog("PNG(%ls): cannot write IDAT trailer?!\n", png->name);
	png_close(png);
	return(-1);
    }

    return(0);
}


/* Write an unsigned long 32bit value in MSB. */
void
png_putlong(uint8_t *ptr, uint32_t val, int *off)
{
    *ptr++ = (val >> 24) & 0xff;
    *ptr++ = (val >> 16) & 0xff;
    *ptr++ = (val >> 8) & 0xff;
    *ptr = val & 0xff;

    if (off != NULL)
	*off += sizeof(uint32_t);
}


/* API: Close the current PNG file. */
void
png_close(png_t *png)
{
    if (png->fp != NULL) {
	fflush(png->fp);
	(void)fclose(png->fp);

	free(png);
    }
}


/* API: Create a new PNG file. */
png_t *
png_create(wchar_t *fn, int width, int height, int bpp)
{
    uint8_t buff[33];
    uint32_t crc;
    png_t *png;
    int i;

    /* Make sure we can do this. */
    if ((bpp != 24) ||
	(width<=0 || width >= 65536) ||
	(height<=0 || height >= 65536)) {
	pclog("PNG(%ls): invalid image parameters!\n", fn);
	return(NULL);
    }

    /* Allocate the control block. */
    png = (png_t *)malloc(sizeof(png_t));
    if (png == NULL) {
	pclog("PNG(%ls): out of memory!\n", fn);
	return(NULL);
    }
    memset(png, 0x00, sizeof(png_t));
    png->name = fn;
    png->width = width;			/* width, in pixels */
    png->height = height;		/* height, in pixels */
    png->bpp = bpp;			/* total bits per pixel */
    png->ctype = PNG_COLOR_TYPE;	/* 02 - sRBG */
    png->cdepth = (bpp/3);		/* bits per color sample */
    png->pwidth = (bpp/8);		/* bytes per pixel */
    png->lwidth = (png->pwidth*width);	/* line width in bytes */
    png->crc = crc_init();		/* initialize data CRC */
    png->dcrc = 1;			/* compressed-data CRC */

    /* Create the data file. */
    if ((png->fp = plat_fopen(fn, L"wb")) == NULL) {
	pclog("PNG(%ls): unable to create file!\n", fn);
	png_close(png);
	return(NULL);
    }

    /* Write out the basic header chunks. */
    i = 0;
    buff[i++] = 0x89;				/* standard PNG header */
    memcpy(&buff[i], "PNG", 3); i += 3;
    buff[i++] = 0x0d; buff[i++] = 0x0a;
    buff[i++] = 0x1a; buff[i++] = 0x0a;

    png_putlong(&buff[i], 13, &i);		/* IHDR chunk */
    memcpy(&buff[i], "IHDR", 4); i += 4;
    png_putlong(&buff[i], png->width, &i);	/* width */
    png_putlong(&buff[i], png->height, &i);	/* height */
    buff[i++] = png->cdepth;			/* color depth (per color) */
    buff[i++] = png->ctype;			/* color type (2=RGB) */
    buff[i++] = PNG_COMPRESSION_TYPE;		/* compression (0=deflate) */
    buff[i++] = PNG_FILTER_TYPE;		/* filter (0=adaptive) */
    buff[i++] = PNG_INTERLACE_MODE;		/* interlace (0=none) */
    crc = crc_finish(crc_upd(crc_init(), &buff[12], 17));
    png_putlong(&buff[i], crc, &i);
    if (fwrite(buff, 1, i, png->fp) != i) {
	pclog("PNG(%ls): unable to write PNG header!\n", fn);
	png_close(png);
	return(NULL);
    }

    i = 4;
    memcpy(&buff[i], "tEXt", 4); i += 4;
    memcpy(&buff[i], "Software", 8); i+= 8;
    buff[i++] = 0x00;
    memcpy(&buff[i], "86Box PNGlib", 12); i+= 12;
    png_putlong(buff, i-8, NULL);
    crc = crc_finish(crc_upd(crc_init(), &buff[4], i-4));
    png_putlong(&buff[i], crc, &i);
    if (fwrite(buff, 1, i, png->fp) != i) {
	pclog("PNG(%ls): unable to write chunk header!\n", fn);
	png_close(png);
	return(NULL);
    }

    return(png);
}
