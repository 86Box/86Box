/*
 * MiniVHD	Minimalist VHD implementation in C.
 *
 *		This file is part of the MiniVHD Project.
 *
 * Version:	@(#)xml2_encoding.h	1.0.1	2021/03/15
 *
 * Author:	Sherman Perry, <shermperry@gmail.com>
 *
 *		Copyright 2019-2021 Sherman Perry.
 *
 *		MIT License
 *
 *		Permission is hereby granted, free of  charge, to any person
 *		obtaining a copy of this software  and associated documenta-
 *		tion files (the "Software"), to deal in the Software without
 *		restriction, including without limitation the rights to use,
 *		copy, modify, merge, publish, distribute, sublicense, and/or
 *		sell copies of  the Software, and  to permit persons to whom
 *		the Software is furnished to do so, subject to the following
 *		conditions:
 *
 *		The above  copyright notice and this permission notice shall
 *		be included in  all copies or  substantial  portions of  the
 *		Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING  BUT NOT LIMITED TO THE  WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN  NO EVENT  SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER  IN AN ACTION OF  CONTRACT, TORT OR  OTHERWISE, ARISING
 * FROM, OUT OF  O R IN  CONNECTION WITH THE  SOFTWARE OR  THE USE  OR  OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef XML2_ENCODING_H
# define XML2_ENCODING_H


typedef uint16_t mvhd_utf16;


#ifdef __cplusplus
extern "C" {
#endif

void	xmlEncodingInit(void);

int	UTF16LEToUTF8(uint8_t *out, int *outlen, const uint8_t *inb,
		      int *inlenb);
int	UTF8ToUTF16LE(uint8_t *outb, int *outlen, const uint8_t *in,
		      int *inlen);
int	UTF16BEToUTF8(uint8_t *out, int *outlen, const uint8_t *inb,
		      int *inlenb);
int	UTF8ToUTF16BE(uint8_t *outb, int *outlen, const uint8_t *in,
		      int *inlen);

#ifdef __cplusplus
}
#endif


#endif	/*XML2_ENCODING_H*/
