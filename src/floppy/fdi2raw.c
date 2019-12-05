/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		FDI to raw bit stream converter
 *		FDI format created by Vincent "ApH" Joguin
 *		Tiny changes - function type fixes, multiple drives,
 *		addition of get_last_head and C++ callability by Thomas
 *		Harte.
 *
 * Version:	@(#)fdi2raw.c	1.0.4	2018/10/18
 *
 * Authors:	Toni Wilen, <twilen@arabuusimiehet.com>
 *		and Vincent Joguin,
 *		Thomas Harte, <T.Harte@excite.co.uk>
 *
 *		Copyright 2001-2004 Toni Wilen.
 *		Copyright 2001-2004 Vincent Joguin.
 *		Copyright 2001 Thomas Harte.
 */
#define STATIC_INLINE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

/* IF UAE */
/*#include "sysconfig.h"
#include "sysdeps.h"
#include "zfile.h"*/
/* ELSE */
#define xmalloc malloc
#define HAVE_STDARG_H
#include "../86box.h"
#include "fdi2raw.h"


#undef DEBUG
#define VERBOSE
#undef VERBOSE


#ifdef ENABLE_FDI2RAW_LOG
int fdi2raw_do_log = ENABLE_FDI2RAW_LOG;


static void
fdi2raw_log(const char *fmt, ...)
{
   va_list ap;

   if (fdi2raw_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define fdi2raw_log(fmt, ...)
#endif


#ifdef ENABLE_FDI2RAW_LOG
#ifdef DEBUG
static char *datalog(uae_u8 *src, int len)
{
	static char buf[1000];
	static int offset;
	int i = 0, offset2;

	offset2 = offset;
	buf[offset++]='\'';
	while(len--) {
		sprintf (buf + offset, "%02.2X", src[i]);
		offset += 2;
		i++;
		if (i > 10) break;
	}
	buf[offset++]='\'';
	buf[offset++] = 0;
	if (offset >= 900) offset = 0;
	return buf + offset2;
}
#else
static char *datalog(uae_u8 *src, int len) { return ""; }
#endif

static int fdi_allocated;
#endif

#ifdef DEBUG
static void fdi_free (void *p)
{
	int size;
	if (!p)
		return;
	size = ((int*)p)[-1];
	fdi_allocated -= size;
	write_log ("%d freed (%d)\n", size, fdi_allocated);
	free ((int*)p - 1);
}
static void *fdi_malloc (int size)
{
	void *p = xmalloc (size + sizeof (int));
	((int*)p)[0] = size;
	fdi_allocated += size;
	write_log ("%d allocated (%d)\n", size, fdi_allocated);
	return (int*)p + 1;
}
#else
#define fdi_free free
#define fdi_malloc xmalloc
#endif

#define MAX_SRC_BUFFER 4194304
#define MAX_DST_BUFFER 40000
#define MAX_MFM_SYNC_BUFFER 60000
#define MAX_TIMING_BUFFER 400000
#define MAX_TRACKS 166

struct fdi_cache {
	uae_u32 *avgp, *minp, *maxp;
	uae_u8 *idxp;
	int avg_free, idx_free, min_free, max_free;
	uae_u32 totalavg, pulses, maxidx, indexoffset;
	int weakbits;
	int lowlevel;
};

struct fdi {
	uae_u8 *track_src_buffer;
	uae_u8 *track_src;
	int track_src_len;
	uae_u8 *track_dst_buffer;
	uae_u8 *track_dst;
	uae_u16 *track_dst_buffer_timing;
	uae_u8 track_len;
	uae_u8 track_type;
	int current_track;
	int last_track;
	int last_head;
	int rotation_speed;
	int bit_rate;
	int disk_type;
	int write_protect;
	int err;
	uae_u8 header[2048];
	int track_offsets[MAX_TRACKS];
	FILE *file;
	int out;
	int mfmsync_offset;
	int *mfmsync_buffer;
	/* sector described only */
	int index_offset;
	int encoding_type;
	/* bit handling */
	int nextdrop;
	struct fdi_cache cache[MAX_TRACKS];
};

#define get_u32(x) ((((x)[0])<<24)|(((x)[1])<<16)|(((x)[2])<<8)|((x)[3]))
#define get_u24(x) ((((x)[0])<<16)|(((x)[1])<<8)|((x)[2]))
STATIC_INLINE void put_u32 (uae_u8 *d, uae_u32 v)
{
	d[0] = v >> 24;
	d[1] = v >> 16;
	d[2] = v >> 8;
	d[3] = v;
}

struct node {
	uae_u16 v;
	struct node *left;
	struct node *right;
};
typedef struct node NODE;

static uae_u8 temp, temp2;

static uae_u8 *expand_tree (uae_u8 *stream, NODE *node)
{
	if (temp & temp2) {
		fdi_free (node->left);
		node->left = 0;
		fdi_free (node->right);
		node->right = 0;
		temp2 >>= 1;
		if (!temp2) {
			temp = *stream++;
			temp2 = 0x80;
		}
		return stream;
	} else {
		uae_u8 *stream_temp;
		temp2 >>= 1;
		if (!temp2) {
			temp = *stream++;
			temp2 = 0x80;
		}
		node->left = fdi_malloc (sizeof (NODE));
		memset (node->left, 0, sizeof (NODE));
		stream_temp = expand_tree (stream, node->left);
		node->right = fdi_malloc (sizeof (NODE));
		memset (node->right, 0, sizeof (NODE));
		return expand_tree (stream_temp, node->right);
	}
}

static uae_u8 *values_tree8 (uae_u8 *stream, NODE *node)
{
	if (node->left == 0) {
		node->v = *stream++;
		return stream;
	} else {
		uae_u8 *stream_temp = values_tree8 (stream, node->left);
		return values_tree8 (stream_temp, node->right);
	}
}

static uae_u8 *values_tree16 (uae_u8 *stream, NODE *node)
{
	if (node->left == 0) {
		uae_u16 high_8_bits = (*stream++) << 8;
		node->v = high_8_bits | (*stream++);
		return stream;
	} else {
		uae_u8 *stream_temp = values_tree16 (stream, node->left);
		return values_tree16 (stream_temp, node->right);
	}
}

static void free_nodes (NODE *node)
{
	if (node) {
		free_nodes (node->left);
		free_nodes (node->right);
		fdi_free (node);
	}
}

static uae_u32 sign_extend16 (uae_u32 v)
{
	if (v & 0x8000)
		v |= 0xffff0000;
	return v;
}

static uae_u32 sign_extend8 (uae_u32 v)
{
	if (v & 0x80)
		v |= 0xffffff00;
	return v;
}

static void fdi_decode (uae_u8 *stream, int size, uae_u8 *out)
{
	int i;
	uae_u8 sign_extend, sixteen_bit, sub_stream_shift;
	NODE root;
	NODE *current_node;

	memset (out, 0, size * 4);
	sub_stream_shift = 1;
	while (sub_stream_shift) {

		/* sub-stream header decode */
		sign_extend = *stream++;
		sub_stream_shift = sign_extend & 0x7f;
		sign_extend &= 0x80;
		sixteen_bit = (*stream++) & 0x80;

		/* huffman tree architecture decode */
		temp = *stream++;
		temp2 =	0x80;
		stream = expand_tree (stream, &root);
		if (temp2 == 0x80)
			stream--;

		/* huffman output values decode */
		if (sixteen_bit)
			stream = values_tree16 (stream, &root);
		else
			stream = values_tree8 (stream, &root);

		/* sub-stream data decode */
		temp2 =	0;
		for (i = 0; i < size; i++) {
			uae_u32 v;
			uae_u8 decode = 1;
			current_node = &root;
			while (decode) {
				if (current_node->left == 0) {
					decode = 0;
				} else {
					temp2 >>= 1;
					if (!temp2) {
						temp2 = 0x80;
						temp = *stream++;
					}
					if (temp & temp2)
						current_node = current_node->right;
					else
						current_node = current_node->left;
				}
			}
			v = ((uae_u32*)out)[i];
			if (sign_extend) {
				if (sixteen_bit)
					v |= sign_extend16 (current_node->v) << sub_stream_shift;
				else
					v |= sign_extend8 (current_node->v) << sub_stream_shift;
			} else {
				v |= current_node->v << sub_stream_shift;
			}
			((uae_u32*)out)[i] = v;
		}
		free_nodes (root.left);
		free_nodes (root.right);
	}
}


static int decode_raw_track (FDI *fdi)
{
	int size = get_u32(fdi->track_src);
	memcpy (fdi->track_dst, fdi->track_src, (size + 7) >> 3);
	fdi->track_src += (size + 7) >> 3;
	return size;
}

/* unknown track */
static void zxx (FDI *fdi)
{
	fdi2raw_log("track %d: unknown track type 0x%02.2X\n", fdi->current_track, fdi->track_type);
}
/* unsupported track */
#if 0
static void zyy (FDI *fdi)
{
	fdi2raw_log("track %d: unsupported track type 0x%02.2X\n", fdi->current_track, fdi->track_type);
}
#endif
/* empty track */
static void track_empty (FDI *fdi)
{
	return;
}

/* unknown sector described type */
static void dxx (FDI *fdi)
{
	fdi2raw_log("\ntrack %d: unknown sector described type 0x%02.2X\n", fdi->current_track, fdi->track_type);
	fdi->err = 1;
}
/* add position of mfm sync bit */
static void add_mfm_sync_bit (FDI *fdi)
{
	if (fdi->nextdrop) {
		fdi->nextdrop = 0;
		return;
	}
	fdi->mfmsync_buffer[fdi->mfmsync_offset++] = fdi->out;
	if (fdi->out == 0) {
		fdi2raw_log("illegal position for mfm sync bit, offset=%d\n",fdi->out);
		fdi->err = 1;
	}
	if (fdi->mfmsync_offset >= MAX_MFM_SYNC_BUFFER) {
		fdi->mfmsync_offset = 0;
		fdi2raw_log("mfmsync buffer overflow\n");
		fdi->err = 1;
	}
	fdi->out++;
}

#define BIT_BYTEOFFSET ((fdi->out) >> 3)
#define BIT_BITOFFSET (7-((fdi->out)&7))

/* add one bit */
static void bit_add (FDI *fdi, int bit)
{
	if (fdi->nextdrop) {
		fdi->nextdrop = 0;
		return;
	}
	fdi->track_dst[BIT_BYTEOFFSET] &= ~(1 << BIT_BITOFFSET);
	if (bit)
		fdi->track_dst[BIT_BYTEOFFSET] |= (1 << BIT_BITOFFSET);
	fdi->out++;
	if (fdi->out >= MAX_DST_BUFFER * 8) {
		fdi2raw_log("destination buffer overflow\n");
		fdi->err = 1;
		fdi->out = 1;
	}
}
/* add bit and mfm sync bit */
static void bit_mfm_add (FDI *fdi, int bit)
{
	add_mfm_sync_bit (fdi);
	bit_add (fdi, bit);
}
/* remove following bit */
static void bit_drop_next (FDI *fdi)
{
	if (fdi->nextdrop > 0) {
		fdi2raw_log("multiple bit_drop_next() called");
	} else if (fdi->nextdrop < 0) {
		fdi->nextdrop = 0;
		fdi2raw_log(":DNN:");
		return;
	}
	fdi2raw_log(":DN:");
	fdi->nextdrop = 1;
}

/* ignore next bit_drop_next() */
static void bit_dedrop (FDI *fdi)
{
	if (fdi->nextdrop) {
		fdi2raw_log("bit_drop_next called before bit_dedrop");
	}
	fdi->nextdrop = -1;
	fdi2raw_log(":BDD:");
}

/* add one byte */
static void byte_add (FDI *fdi, uae_u8 v)
{
	int i;
	for (i = 7; i >= 0; i--)
		bit_add (fdi, v & (1 << i));
}
/* add one word */
static void word_add (FDI *fdi, uae_u16 v)
{
	byte_add (fdi, (uae_u8)(v >> 8));
	byte_add (fdi, (uae_u8)v);
}
/* add one byte and mfm encode it */
static void byte_mfm_add (FDI *fdi, uae_u8 v)
{
	int i;
	for (i = 7; i >= 0; i--)
		bit_mfm_add (fdi, v & (1 << i));
}
/* add multiple bytes and mfm encode them */
static void bytes_mfm_add (FDI *fdi, uae_u8 v, int len)
{
	int i;
	for (i = 0; i < len; i++) byte_mfm_add (fdi, v);
}
/* add one mfm encoded word and re-mfm encode it */
static void word_post_mfm_add (FDI *fdi, uae_u16 v)
{
	int i;
	for (i = 14; i >= 0; i -= 2)
		bit_mfm_add (fdi, v & (1 << i));
}

/* bit 0 */
static void s00(FDI *fdi) { bit_add (fdi, 0); }
/* bit 1*/
static void s01(FDI *fdi) { bit_add (fdi, 1); }
/* 4489 */
static void s02(FDI *fdi) { word_add (fdi, 0x4489); }
/* 5224 */
static void s03(FDI *fdi) { word_add (fdi, 0x5224); }
/* mfm sync bit */
static void s04(FDI *fdi) { add_mfm_sync_bit (fdi); }
/* RLE MFM-encoded data */
static void s08(FDI *fdi)
{
	int bytes = *fdi->track_src++;
	uae_u8 byte = *fdi->track_src++;
	if (bytes == 0) bytes = 256;
	fdi2raw_log("s08:len=%d,data=%02.2X",bytes,byte);
	while(bytes--) byte_add (fdi, byte);
}
/* RLE MFM-decoded data */
static void s09(FDI *fdi)
{
	int bytes = *fdi->track_src++;
	uae_u8 byte = *fdi->track_src++;
	if (bytes == 0) bytes = 256;
	bit_drop_next (fdi);
	fdi2raw_log("s09:len=%d,data=%02.2X",bytes,byte);
	while(bytes--) byte_mfm_add (fdi, byte);
}
/* MFM-encoded data */
static void s0a(FDI *fdi)
{
	int i, bits = (fdi->track_src[0] << 8) | fdi->track_src[1];
	uae_u8 b;
	fdi->track_src += 2;
	fdi2raw_log("s0a:bits=%d,data=%s", bits, datalog(fdi->track_src, (bits + 7) / 8));
	while (bits >= 8) {
		byte_add (fdi, *fdi->track_src++);
		bits -= 8;
	}
	if (bits > 0) {
		i = 7;
		b = *fdi->track_src++;
		while (bits--) {
			bit_add (fdi, b & (1 << i));
			i--;
		}
	}
}
/* MFM-encoded data */
static void s0b(FDI *fdi)
{
	int i, bits = ((fdi->track_src[0] << 8) | fdi->track_src[1]) + 65536;
	uae_u8 b;
	fdi->track_src += 2;
	fdi2raw_log("s0b:bits=%d,data=%s", bits, datalog(fdi->track_src, (bits + 7) / 8));
	while (bits >= 8) {
		byte_add (fdi, *fdi->track_src++);
		bits -= 8;
	}
	if (bits > 0) {
		i = 7;
		b = *fdi->track_src++;
		while (bits--) {
			bit_add (fdi, b & (1 << i));
			i--;
		}
	}
}
/* MFM-decoded data */
static void s0c(FDI *fdi)
{
	int i, bits = (fdi->track_src[0] << 8) | fdi->track_src[1];
	uae_u8 b;
	fdi->track_src += 2;
	bit_drop_next (fdi);
	fdi2raw_log("s0c:bits=%d,data=%s", bits, datalog(fdi->track_src, (bits + 7) / 8));
	while (bits >= 8) {
		byte_mfm_add (fdi, *fdi->track_src++);
		bits -= 8;
	}
	if (bits > 0) {
		i = 7;
		b = *fdi->track_src++;
		while(bits--) {
			bit_mfm_add (fdi, b & (1 << i));
			i--;
		}
	}
}
/* MFM-decoded data */
static void s0d(FDI *fdi)
{
	int i, bits = ((fdi->track_src[0] << 8) | fdi->track_src[1]) + 65536;
	uae_u8 b;
	fdi->track_src += 2;
	bit_drop_next (fdi);
	fdi2raw_log("s0d:bits=%d,data=%s", bits, datalog(fdi->track_src, (bits + 7) / 8));
	while (bits >= 8) {
		byte_mfm_add (fdi, *fdi->track_src++);
		bits -= 8;
	}
	if (bits > 0) {
		i = 7;
		b = *fdi->track_src++;
		while(bits--) {
			bit_mfm_add (fdi, b & (1 << i));
			i--;
		}
	}
}

/* ***** */
/* AMIGA */
/* ***** */

/* just for testing integrity of Amiga sectors */

/*static void rotateonebit (uae_u8 *start, uae_u8 *end, int shift)
{
	if (shift == 0)
		return;
	while (start <= end) {
		start[0] <<= shift;
		start[0] |= start[1] >> (8 - shift);
		start++;
	}
}*/

/*static uae_u16 getmfmword (uae_u8 *mbuf)
{
	uae_u32 v;

	v = (mbuf[0] << 8) | (mbuf[1] << 0);
	if (check_offset == 0)
		return v;
	v <<= 8;
	v |= mbuf[2];
	v >>= check_offset;
	return v;
}*/

#define MFMMASK 0x55555555
/*static uae_u32 getmfmlong (uae_u8 * mbuf)
{
	return ((getmfmword (mbuf) << 16) | getmfmword (mbuf + 2)) & MFMMASK;
}*/

#if 0
static int amiga_check_track (FDI *fdi)
{
	int i, j, secwritten = 0;
	int fwlen = fdi->out / 8;
	int length = 2 * fwlen;
	int drvsec = 11;
	uae_u32 odd, even, chksum, id, dlong;
	uae_u8 *secdata;
	uae_u8 secbuf[544];
	uae_u8 bigmfmbuf[60000];
	uae_u8 *mbuf, *mbuf2, *mend;
	char sectable[22];
	uae_u8 *raw = fdi->track_dst_buffer;
	int slabel, off;
	int ok = 1;

	memset (bigmfmbuf, 0, sizeof (bigmfmbuf));
	mbuf = bigmfmbuf;
	check_offset = 0;
	for (i = 0; i < (fdi->out + 7) / 8; i++)
		*mbuf++ = raw[i];
	off = fdi->out & 7;
#if 1
	if (off > 0) {
		mbuf--;
		*mbuf &= ~((1 << (8 - off)) - 1);
	}
	j = 0;
	while (i < (fdi->out + 7) / 8 + 600) {
		*mbuf++ |= (raw[j] >> off) | ((raw[j + 1]) << (8 - off));
		j++;
		i++;
	}
#endif
	mbuf = bigmfmbuf;

	memset (sectable, 0, sizeof (sectable));
	mend = bigmfmbuf + length;
	mend -= (4 + 16 + 8 + 512);

	while (secwritten < drvsec) {
		int trackoffs;

		for (;;) {
			rotateonebit (bigmfmbuf, mend, 1);
			if (getmfmword (mbuf) == 0)
				break;
			if (secwritten == 10) {
				mbuf[0] = 0x44;
				mbuf[1] = 0x89;
			}
			if (check_offset > 7) {
				check_offset = 0;
				mbuf++;
				if (mbuf >= mend || *mbuf == 0)
					break;
			}
			if (getmfmword (mbuf) == 0x4489)
				break;
		}
		if (mbuf >= mend || *mbuf == 0)
			break;

		rotateonebit (bigmfmbuf, mend, check_offset);
		check_offset = 0;

		while (getmfmword (mbuf) == 0x4489)
			mbuf+= 1 * 2;
		mbuf2 =	mbuf + 8;

		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2 * 2);
		mbuf +=	4 * 2;
		id = (odd << 1) | even;

		trackoffs = (id & 0xff00) >> 8;
		if (trackoffs + 1 > drvsec) {
			fdi2raw_log("illegal sector offset %d\n",trackoffs);
			ok = 0;
			mbuf = mbuf2;
			continue;
		}
		if ((id >> 24) != 0xff) {
			fdi2raw_log("sector %d format type %02.2X?\n", trackoffs, id >> 24);
			ok = 0;
		}
		chksum = odd ^ even;
		slabel = 0;
		for (i = 0; i < 4; i++) {
			odd = getmfmlong (mbuf);
			even = getmfmlong (mbuf + 8 * 2);
			mbuf += 2* 2;

			dlong = (odd << 1) | even;
			if (dlong) slabel = 1;
			chksum ^= odd ^ even;
		}
		mbuf += 8 * 2;
		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2 * 2);
		mbuf += 4 * 2;
		if (((odd << 1) | even) != chksum) {
			fdi2raw_log("sector %d header crc error\n", trackoffs);
			ok = 0;
			mbuf = mbuf2;
			continue;
		}
		fdi2raw_log("sector %d header crc ok\n", trackoffs);
		if (((id & 0x00ff0000) >> 16) != (uae_u32)fdi->current_track) {
			fdi2raw_log("illegal track number %d <> %d\n",fdi->current_track,(id & 0x00ff0000) >> 16);
			ok++;
			mbuf = mbuf2;
			continue;
		}
		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2 * 2);
		mbuf += 4 * 2;
		chksum = (odd << 1) | even;
		secdata = secbuf + 32;
		for (i = 0; i < 128; i++) {
			odd = getmfmlong (mbuf);
			even = getmfmlong (mbuf + 256 * 2);
			mbuf += 2 * 2;
			dlong = (odd << 1) | even;
			*secdata++ = (uae_u8) (dlong >> 24);
			*secdata++ = (uae_u8) (dlong >> 16);
			*secdata++ = (uae_u8) (dlong >> 8);
			*secdata++ = (uae_u8) dlong;
			chksum ^= odd ^ even;
		}
		mbuf += 256 * 2;
		if (chksum) {
			fdi2raw_log("sector %d data checksum error\n",trackoffs);
			ok = 0;
		} else if (sectable[trackoffs]) {
			fdi2raw_log("sector %d already found?\n", trackoffs);
			mbuf = mbuf2;
		} else {
			fdi2raw_log("sector %d ok\n",trackoffs);
			if (slabel) fdi2raw_log("(non-empty sector header)\n");
			sectable[trackoffs] = 1;
			secwritten++;
			if (trackoffs == 9)
				mbuf += 0x228;
		}
	}
	for (i = 0; i < drvsec; i++) {
		if (!sectable[i]) {
			fdi2raw_log("sector %d missing\n", i);
			ok = 0;
		}
	}
	return ok;
}
#endif

static void amiga_data_raw (FDI *fdi, uae_u8 *secbuf, uae_u8 *crc, int len)
{
	int i;
	uae_u8 crcbuf[4];

	if (!crc) {
		memset (crcbuf, 0, 4);
	} else {
		memcpy (crcbuf, crc ,4);
	}
	for (i = 0; i < 4; i++)
		byte_mfm_add (fdi, crcbuf[i]);
	for (i = 0; i < len; i++)
		byte_mfm_add (fdi, secbuf[i]);
}

static void amiga_data (FDI *fdi, uae_u8 *secbuf)
{
	uae_u16 mfmbuf[4 + 512];
	uae_u32 dodd, deven, dck;
	int i;

	for (i = 0; i < 512; i += 4) {
		deven = ((secbuf[i + 0] << 24) | (secbuf[i + 1] << 16)
		 | (secbuf[i + 2] << 8) | (secbuf[i + 3]));
		dodd = deven >> 1;
		deven &= 0x55555555;
		dodd &= 0x55555555;
		mfmbuf[(i >> 1) + 4] = (uae_u16) (dodd >> 16);
		mfmbuf[(i >> 1) + 5] = (uae_u16) dodd;
		mfmbuf[(i >> 1) + 256 + 4] = (uae_u16) (deven >> 16);
		mfmbuf[(i >> 1) + 256 + 5] = (uae_u16) deven;
	}
	dck = 0;
	for (i = 4; i < 4 + 512; i += 2)
		dck ^= (mfmbuf[i] << 16) | mfmbuf[i + 1];
	deven = dodd = dck;
	dodd >>= 1;
	deven &= 0x55555555;
	dodd &= 0x55555555;
	mfmbuf[0] = (uae_u16) (dodd >> 16);
	mfmbuf[1] = (uae_u16) dodd;
	mfmbuf[2] = (uae_u16) (deven >> 16);
	mfmbuf[3] = (uae_u16) deven;

	for (i = 0; i < 4 + 512; i ++)
		word_post_mfm_add (fdi, mfmbuf[i]);
}

static void amiga_sector_header	(FDI *fdi, uae_u8 *header, uae_u8 *data, int sector, int untilgap)
{
	uae_u8 headerbuf[4], databuf[16];
	uae_u32 deven, dodd, hck;
	uae_u16 mfmbuf[24];
	int i;

	byte_mfm_add (fdi, 0);
	byte_mfm_add (fdi, 0);
	word_add (fdi, 0x4489);
	word_add (fdi, 0x4489);
	if (header) {
		memcpy (headerbuf, header, 4);
	} else {
		headerbuf[0] = 0xff;
		headerbuf[1] = (uae_u8)fdi->current_track;
		headerbuf[2] = (uae_u8)sector;
		headerbuf[3] = (uae_u8)untilgap;
	}
	if (data)
		memcpy (databuf, data, 16);
	else
		memset (databuf, 0, 16);

	deven =	((headerbuf[0] << 24) | (headerbuf[1] << 16)
		| (headerbuf[2] << 8) | (headerbuf[3]));
	dodd = deven >> 1;
	deven &= 0x55555555;
	dodd &= 0x55555555;
	mfmbuf[0] = (uae_u16) (dodd >> 16);
	mfmbuf[1] = (uae_u16) dodd;
	mfmbuf[2] = (uae_u16) (deven >> 16);
	mfmbuf[3] = (uae_u16) deven;
	for (i = 0; i < 16; i += 4) {
		deven = ((databuf[i] << 24) | (databuf[i + 1] << 16)
			| (databuf[i + 2] << 8) | (databuf[i + 3]));
		dodd = deven >> 1;
		deven &= 0x55555555;
		dodd &= 0x55555555;
		mfmbuf[(i >> 1) + 0 + 4] = (uae_u16) (dodd >> 16);
		mfmbuf[(i >> 1) + 0 + 5] = (uae_u16) dodd;
		mfmbuf[(i >> 1) + 8 + 4] = (uae_u16) (deven >> 16);
		mfmbuf[(i >> 1) + 8 + 5] = (uae_u16) deven;
	}
	hck = 0;
	for (i = 0; i < 4 + 16; i += 2)
		hck ^= (mfmbuf[i] << 16) | mfmbuf[i + 1];
	deven = dodd = hck;
	dodd >>= 1;
	deven &= 0x55555555;
	dodd &= 0x55555555;
	mfmbuf[20] = (uae_u16) (dodd >> 16);
	mfmbuf[21] = (uae_u16) dodd;
	mfmbuf[22] = (uae_u16) (deven >> 16);
	mfmbuf[23] = (uae_u16) deven;

	for (i = 0; i < 4 + 16 + 4; i ++)
		word_post_mfm_add (fdi, mfmbuf[i]);
}

/* standard super-extended Amiga sector header */
static void s20(FDI *fdi)
{
	bit_drop_next (fdi);
	fdi2raw_log("s20:header=%s,data=%s", datalog(fdi->track_src, 4), datalog(fdi->track_src + 4, 16));
	amiga_sector_header (fdi, fdi->track_src, fdi->track_src + 4, 0, 0);
	fdi->track_src += 4 + 16;
}
/* standard extended Amiga sector header */
static void s21(FDI *fdi)
{
	bit_drop_next (fdi);
	fdi2raw_log("s21:header=%s", datalog(fdi->track_src, 4));
	amiga_sector_header (fdi, fdi->track_src, 0, 0, 0);
	fdi->track_src += 4;
}
/* standard Amiga sector header */
static void s22(FDI *fdi)
{
	bit_drop_next (fdi);
	fdi2raw_log("s22:sector=%d,untilgap=%d", fdi->track_src[0], fdi->track_src[1]);
	amiga_sector_header (fdi, 0, 0, fdi->track_src[0], fdi->track_src[1]);
	fdi->track_src += 2;
}
/* standard 512-byte, CRC-correct Amiga data */
static void s23(FDI *fdi)
{
	fdi2raw_log("s23:data=%s", datalog (fdi->track_src, 512));
	amiga_data (fdi, fdi->track_src);
	fdi->track_src += 512;
}
/* not-decoded, 128*2^x-byte, CRC-correct Amiga data */
static void s24(FDI *fdi)
{
	int shift = *fdi->track_src++;
	fdi2raw_log("s24:shift=%d,data=%s", shift, datalog (fdi->track_src, 128 << shift));
	amiga_data_raw (fdi, fdi->track_src, 0, 128 << shift);
	fdi->track_src += 128 << shift;
}
/* not-decoded, 128*2^x-byte, CRC-incorrect Amiga data */
static void s25(FDI *fdi)
{
	int shift = *fdi->track_src++;
	fdi2raw_log("s25:shift=%d,crc=%s,data=%s", shift, datalog (fdi->track_src, 4), datalog (fdi->track_src + 4, 128 << shift));
	amiga_data_raw (fdi, fdi->track_src + 4, fdi->track_src, 128 << shift);
	fdi->track_src += 4 + (128 << shift);
}
/* standard extended Amiga sector */
static void s26(FDI *fdi)
{
	s21 (fdi);
	fdi2raw_log("s26:data=%s", datalog (fdi->track_src, 512));
	amiga_data (fdi, fdi->track_src);
	fdi->track_src += 512;
}
/* standard short Amiga sector */
static void s27(FDI *fdi)
{
	s22 (fdi);
	fdi2raw_log("s27:data=%s", datalog (fdi->track_src, 512));
	amiga_data (fdi, fdi->track_src);
	fdi->track_src += 512;
}

/* *** */
/* IBM */
/* *** */

static uae_u16 ibm_crc (uae_u8 byte, int reset)
{
	static uae_u16 crc;
	int i;

	if (reset) crc = 0xcdb4;
	for (i = 0; i < 8; i++) {
		if (crc & 0x8000) {
			crc <<= 1;
			if (!(byte & 0x80)) crc ^= 0x1021;
		} else {
			crc <<= 1;
			if (byte & 0x80) crc ^= 0x1021;
		}
		byte <<= 1;
	}
	return crc;
}

static void ibm_data (FDI *fdi, uae_u8 *data, uae_u8 *crc, int len)
{
	int i;
	uae_u8 crcbuf[2];
	uae_u16 crcv = 0;

	word_add (fdi, 0x4489);
	word_add (fdi, 0x4489);
	word_add (fdi, 0x4489);
	byte_mfm_add (fdi, 0xfb);
	ibm_crc (0xfb, 1);
	for (i = 0; i < len; i++) {
		byte_mfm_add (fdi, data[i]);
		crcv = ibm_crc (data[i], 0);
	}
	if (!crc) {
		crc = crcbuf;
		crc[0] = (uae_u8)(crcv >> 8);
		crc[1] = (uae_u8)crcv;
	}
	byte_mfm_add (fdi, crc[0]);
	byte_mfm_add (fdi, crc[1]);
}

static void ibm_sector_header (FDI *fdi, uae_u8 *data, uae_u8 *crc, int secnum, int pre)
{
	uae_u8 secbuf[5];
	uae_u8 crcbuf[2];
	uae_u16 crcv;
	int i;

	if (pre)
		bytes_mfm_add (fdi, 0, 12);
	word_add (fdi, 0x4489);
	word_add (fdi, 0x4489);
	word_add (fdi, 0x4489);
	secbuf[0] = 0xfe;
	if (secnum >= 0) {
		secbuf[1] = (uae_u8)(fdi->current_track/2);
		secbuf[2] = (uae_u8)(fdi->current_track%2);
		secbuf[3] = (uae_u8)secnum;
		secbuf[4] = 2;
	} else {
		memcpy (secbuf + 1, data, 4);
	}
	ibm_crc (secbuf[0], 1);
	ibm_crc (secbuf[1], 0);
	ibm_crc (secbuf[2], 0);
	ibm_crc (secbuf[3], 0);
	crcv = ibm_crc (secbuf[4], 0);
	if (crc) {
		memcpy (crcbuf, crc, 2);
	} else {
		crcbuf[0] = (uae_u8)(crcv >> 8);
		crcbuf[1] = (uae_u8)crcv;
	}
	/* data */
	for (i = 0;i < 5; i++)
		byte_mfm_add (fdi, secbuf[i]);
	/* crc */
	byte_mfm_add (fdi, crcbuf[0]);
	byte_mfm_add (fdi, crcbuf[1]);
}

/* standard IBM index address mark */
static void s10(FDI *fdi)
{
	bit_drop_next (fdi);
	bytes_mfm_add (fdi, 0, 12);
	word_add (fdi, 0x5224);
	word_add (fdi, 0x5224);
	word_add (fdi, 0x5224);
	byte_mfm_add (fdi, 0xfc);
}
/* standard IBM pre-gap */
static void s11(FDI *fdi)
{
	bit_drop_next (fdi);
	bytes_mfm_add (fdi, 0x4e, 78);
	bit_dedrop (fdi);
	s10 (fdi);
	bytes_mfm_add (fdi, 0x4e, 50);
}
/* standard ST pre-gap */
static void s12(FDI *fdi)
{
	bit_drop_next (fdi);
	bytes_mfm_add (fdi, 0x4e, 78);
}
/* standard extended IBM sector header */
static void s13(FDI *fdi)
{
	bit_drop_next (fdi);
	fdi2raw_log("s13:header=%s", datalog (fdi->track_src, 4));
	ibm_sector_header (fdi, fdi->track_src, 0, -1, 1);
	fdi->track_src += 4;
}
/* standard mini-extended IBM sector header */
static void s14(FDI *fdi)
{
	fdi2raw_log("s14:header=%s", datalog (fdi->track_src, 4));
	ibm_sector_header (fdi, fdi->track_src, 0, -1, 0);
	fdi->track_src += 4;
}
/* standard short IBM sector header */
static void s15(FDI *fdi)
{
	bit_drop_next (fdi);
	fdi2raw_log("s15:sector=%d", *fdi->track_src);
	ibm_sector_header (fdi, 0, 0, *fdi->track_src++, 1);
}
/* standard mini-short IBM sector header */
static void s16(FDI *fdi)
{
	fdi2raw_log("s16:track=%d", *fdi->track_src);
	ibm_sector_header (fdi, 0, 0, *fdi->track_src++, 0);
}
/* standard CRC-incorrect mini-extended IBM sector header */
static void s17(FDI *fdi)
{
	fdi2raw_log("s17:header=%s,crc=%s", datalog (fdi->track_src, 4), datalog (fdi->track_src + 4, 2));
	ibm_sector_header (fdi, fdi->track_src, fdi->track_src + 4, -1, 0);
	fdi->track_src += 4 + 2;
}
/* standard CRC-incorrect mini-short IBM sector header */
static void s18(FDI *fdi)
{
	fdi2raw_log("s18:sector=%d,header=%s", *fdi->track_src, datalog (fdi->track_src + 1, 4));
	ibm_sector_header (fdi, 0, fdi->track_src + 1, *fdi->track_src, 0);
	fdi->track_src += 1 + 4;
}
/* standard 512-byte CRC-correct IBM data */
static void s19(FDI *fdi)
{
	fdi2raw_log("s19:data=%s", datalog (fdi->track_src , 512));
	ibm_data (fdi, fdi->track_src, 0, 512);
	fdi->track_src += 512;
}
/* standard 128*2^x-byte-byte CRC-correct IBM data */
static void s1a(FDI *fdi)
{
	int shift = *fdi->track_src++;
	fdi2raw_log("s1a:shift=%d,data=%s", shift, datalog (fdi->track_src , 128 << shift));
	ibm_data (fdi, fdi->track_src, 0, 128 << shift);
	fdi->track_src += 128 << shift;
}
/* standard 128*2^x-byte-byte CRC-incorrect IBM data */
static void s1b(FDI *fdi)
{
	int shift = *fdi->track_src++;
	fdi2raw_log("s1b:shift=%d,crc=%s,data=%s", shift, datalog (fdi->track_src + (128 << shift), 2), datalog (fdi->track_src , 128 << shift));
	ibm_data (fdi, fdi->track_src, fdi->track_src + (128 << shift), 128 << shift);
	fdi->track_src += (128 << shift) + 2;
}
/* standard extended IBM sector */
static void s1c(FDI *fdi)
{
	int shift = fdi->track_src[3];
	s13 (fdi);
	bytes_mfm_add (fdi, 0x4e, 22);
	bytes_mfm_add (fdi, 0x00, 12);
	ibm_data (fdi, fdi->track_src, 0, 128 << shift);
	fdi->track_src += 128 << shift;
}
/* standard short IBM sector */
static void s1d(FDI *fdi)
{
	s15 (fdi);
	bytes_mfm_add (fdi, 0x4e, 22);
	bytes_mfm_add (fdi, 0x00, 12);
	s19 (fdi);
}

/* end marker */
static void sff(FDI *fdi)
{
}

typedef void (*decode_described_track_func)(FDI*);

static decode_described_track_func decode_sectors_described_track[] =
{
	s00,s01,s02,s03,s04,dxx,dxx,dxx,s08,s09,s0a,s0b,s0c,s0d,dxx,dxx, /* 00-0F */
	s10,s11,s12,s13,s14,s15,s16,s17,s18,s19,s1a,s1b,s1c,s1d,dxx,dxx, /* 10-1F */
	s20,s21,s22,s23,s24,s25,s26,s27,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* 20-2F */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* 30-3F */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* 40-4F */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* 50-5F */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* 60-6F */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* 70-7F */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* 80-8F */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* 90-9F */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* A0-AF */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* B0-BF */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* C0-CF */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* D0-DF */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx, /* E0-EF */
	dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,dxx,sff  /* F0-FF */
};

static void track_amiga (struct fdi *fdi, int first_sector, int max_sector)
{
	int i;

	bit_add (fdi, 0);
	bit_drop_next (fdi);
	for (i = 0; i < max_sector; i++) {
		amiga_sector_header (fdi, 0, 0, first_sector, max_sector - i);
		amiga_data (fdi, fdi->track_src + first_sector * 512);
		first_sector++;
		if (first_sector >= max_sector) first_sector = 0;
	}
	bytes_mfm_add (fdi, 0, 260); /* gap */
}
static void track_atari_st (struct fdi *fdi, int max_sector)
{
	int i, gap3 = 0;
	uae_u8 *p = fdi->track_src;

	switch (max_sector)
		{
		case 9:
		gap3 = 40;
		break;
		case 10:
		gap3 = 24;
		break;
	}
	s15 (fdi);
	for (i = 0; i < max_sector; i++) {
		byte_mfm_add (fdi, 0x4e);
		byte_mfm_add (fdi, 0x4e);
		ibm_sector_header (fdi, 0, 0, fdi->current_track, 1);
		ibm_data (fdi, p + i * 512, 0, 512);
		bytes_mfm_add (fdi, 0x4e, gap3);
	}
	bytes_mfm_add (fdi, 0x4e, 660 - gap3);
	fdi->track_src += fdi->track_len * 256;
}
static void track_pc (struct fdi *fdi, int max_sector)
{
	int i, gap3;
	uae_u8 *p = fdi->track_src;

	switch (max_sector)
		{
		case 8:
		gap3 = 116;
		break;
		case 9:
		gap3 = 54;
		break;
		default:
		gap3 = 100; /* fixme */
		break;
	}
	s11 (fdi);
	for (i = 0; i < max_sector; i++) {
		byte_mfm_add (fdi, 0x4e);
		byte_mfm_add (fdi, 0x4e);
		ibm_sector_header (fdi, 0, 0, fdi->current_track, 1);
		ibm_data (fdi, p + i * 512, 0, 512);
		bytes_mfm_add (fdi, 0x4e, gap3);
	}
	bytes_mfm_add (fdi, 0x4e, 600 - gap3);
	fdi->track_src += fdi->track_len * 256;
}

/* amiga dd */
static void track_amiga_dd (struct fdi *fdi)
{
	uae_u8 *p = fdi->track_src;
	track_amiga (fdi, fdi->track_len >> 4, 11);
	fdi->track_src = p + (fdi->track_len & 15) * 512;
}
/* amiga hd */
static void track_amiga_hd (struct fdi *fdi)
{
	uae_u8 *p = fdi->track_src;
	track_amiga (fdi, 0, 22);
	fdi->track_src = p + fdi->track_len * 256;
}
/* atari st 9 sector */
static void track_atari_st_9 (struct fdi *fdi)
{
	track_atari_st (fdi, 9);
}
/* atari st 10 sector */
static void track_atari_st_10 (struct fdi *fdi)
{
	track_atari_st (fdi, 10);
}
/* pc 8 sector */
static void track_pc_8 (struct fdi *fdi)
{
	track_pc (fdi, 8);
}
/* pc 9 sector */
static void track_pc_9 (struct fdi *fdi)
{
	track_pc (fdi, 9);
}
/* pc 15 sector */
static void track_pc_15 (struct fdi *fdi)
{
	track_pc (fdi, 15);
}
/* pc 18 sector */
static void track_pc_18 (struct fdi *fdi)
{
	track_pc (fdi, 18);
}
/* pc 36 sector */
static void track_pc_36 (struct fdi *fdi)
{
	track_pc (fdi, 36);
}

typedef void (*decode_normal_track_func)(FDI*);

static decode_normal_track_func decode_normal_track[] =
{
	track_empty, /* 0 */
	track_amiga_dd, track_amiga_hd, /* 1-2 */
	track_atari_st_9, track_atari_st_10, /* 3-4 */
	track_pc_8, track_pc_9, track_pc_15, track_pc_18, track_pc_36, /* 5-9 */
	zxx,zxx,zxx,zxx,zxx /* A-F */
};

static void fix_mfm_sync (FDI *fdi)
{
	int i, pos, off1, off2, off3, mask1, mask2, mask3;

	for (i = 0; i < fdi->mfmsync_offset; i++) {
		pos = fdi->mfmsync_buffer[i];
		off1 = (pos - 1) >> 3;
		off2 = (pos + 1) >> 3;
		off3 = pos >> 3;
		mask1 = 1 << (7 - ((pos - 1) & 7));
		mask2 = 1 << (7 - ((pos + 1) & 7));
		mask3 = 1 << (7 - (pos & 7));
		if (!(fdi->track_dst[off1] & mask1) && !(fdi->track_dst[off2] & mask2))
			fdi->track_dst[off3] |= mask3;
		else
			fdi->track_dst[off3] &= ~mask3;
	}
}

static int handle_sectors_described_track (FDI *fdi)
{
#ifdef ENABLE_FDI2RAW_LOG
	int oldout;
	uae_u8 *start_src = fdi->track_src ;
#endif
	fdi->encoding_type = *fdi->track_src++;
	fdi->index_offset = get_u32(fdi->track_src);
	fdi->index_offset >>= 8;
	fdi->track_src += 3;
	fdi2raw_log("sectors_described, index offset: %d\n",fdi->index_offset);

	do {
		fdi->track_type = *fdi->track_src++;
		fdi2raw_log("%06.6X %06.6X %02.2X:",fdi->track_src - start_src + 0x200, fdi->out/8, fdi->track_type);
#ifdef ENABLE_FDI2RAW_LOG
		oldout = fdi->out;
#endif
		decode_sectors_described_track[fdi->track_type](fdi);
		fdi2raw_log(" %d\n", fdi->out - oldout);
#ifdef ENABLE_FDI2RAW_LOG
		oldout = fdi->out;
#endif
		if (fdi->out < 0 || fdi->err) {
			fdi2raw_log("\nin %d bytes, out %d bits\n", fdi->track_src - fdi->track_src_buffer, fdi->out);
			return -1;
		}
		if (fdi->track_src - fdi->track_src_buffer >= fdi->track_src_len) {
			fdi2raw_log("source buffer overrun, previous type: %02.2X\n", fdi->track_type);
			return -1;
		}
	} while (fdi->track_type != 0xff);
	fdi2raw_log("\n");
	fix_mfm_sync (fdi);
	return fdi->out;
}

static uae_u8 *fdi_decompress (int pulses, uae_u8 *sizep, uae_u8 *src, int *dofree)
{
	uae_u32 size = get_u24 (sizep);
	uae_u32 *dst2;
	int len = size & 0x3fffff;
	uae_u8 *dst;
	int mode = size >> 22, i;

	*dofree = 0;
	if (mode == 0 && pulses * 2 > len)
		mode = 1;
	if (mode == 0) {
		dst2 = (uae_u32*)src;
		dst = src;
		for (i = 0; i < pulses; i++) {
			*dst2++ = get_u32 (src);
			src += 4;
		}
	} else if (mode == 1) {
		dst = fdi_malloc (pulses *4);
		*dofree = 1;
		fdi_decode (src, pulses, dst);
	} else {
		dst = 0;
	}
	return dst;
}

static void dumpstream(int track, uae_u8 *stream, int len)
{
#if 0
    char name[100];
    FILE *f;

    sprintf (name, "track_%d.raw", track);
    f = fopen(name, "wb");
    fwrite (stream, 1, len * 4, f);
    fclose (f);
#endif
}

static int bitoffset;

STATIC_INLINE void addbit (uae_u8 *p, int bit)
{
	int off1 = bitoffset / 8;
	int off2 = bitoffset % 8;
	p[off1] |= bit << (7 - off2);
	bitoffset++;
}


struct pulse_sample {
	uint32_t size;
	int number_of_bits;
};


#define FDI_MAX_ARRAY 10 /* change this value as you want */
static int pulse_limitval = 15; /* tolerance of 15% */
static struct pulse_sample psarray[FDI_MAX_ARRAY];
static int array_index;
static unsigned	long total;
static int totaldiv;

static void init_array(uint32_t standard_MFM_2_bit_cell_size, int nb_of_bits)
{
	int i;

	for (i = 0; i < FDI_MAX_ARRAY; i++) {
		psarray[i].size = standard_MFM_2_bit_cell_size; /* That is (total track length / 50000) for Amiga double density */
		total += psarray[i].size;
		psarray[i].number_of_bits = nb_of_bits;
		totaldiv += psarray[i].number_of_bits;
	}
	array_index = 0;
}

#if 0

static void fdi2_decode (FDI *fdi, uint32_t totalavg, uae_u32 *avgp, uae_u32 *minp, uae_u32 *maxp, uae_u8 *idx, int maxidx, int *indexoffsetp, int pulses, int mfm)
{
	uint32_t adjust;
	uint32_t adjusted_pulse;
	uint32_t standard_MFM_2_bit_cell_size = totalavg / 50000;
	uint32_t standard_MFM_8_bit_cell_size = totalavg / 12500;
	int real_size, i, j, eodat, outstep;
	int indexoffset = *indexoffsetp;
	uae_u8 *d = fdi->track_dst_buffer;
	uae_u16 *pt = fdi->track_dst_buffer_timing;
	uae_u32 ref_pulse, pulse;

	/* detects a long-enough stable pulse coming just after another stable pulse */
	i = 1;
	while ( (i < pulses) && ( (idx[i] < maxidx)
		|| (idx[i - 1] < maxidx)
		|| (avgp[i] < (standard_MFM_2_bit_cell_size - (standard_MFM_2_bit_cell_size / 4))) ) )
			i++;
	if (i == pulses)  {
		fdi2raw_log("No stable and long-enough pulse in track.\n");
		return;
	}
	i--;
	eodat = i;
	adjust = 0;
	total = 0;
	totaldiv = 0;
	init_array(standard_MFM_2_bit_cell_size, 2);
	bitoffset = 0;
	ref_pulse = 0;
	outstep = 0;
	while (outstep < 2) {

		/* calculates the current average bitrate from previous decoded data */
		uae_u32 avg_size = (total << 3) / totaldiv; /* this is the new average size for one MFM bit */
		/* uae_u32 avg_size = (uae_u32)((((float)total)*8.0) / ((float)totaldiv)); */
		/* you can try tighter ranges than 25%, or wider ranges. I would probably go for tighter... */
		if ((avg_size < (standard_MFM_8_bit_cell_size - (pulse_limitval * standard_MFM_8_bit_cell_size / 100))) ||
			(avg_size > (standard_MFM_8_bit_cell_size + (pulse_limitval * standard_MFM_8_bit_cell_size / 100)))) {
				avg_size = standard_MFM_8_bit_cell_size;
		}
		/* this is to prevent the average value from going too far
		* from the theoretical value, otherwise it could progressively go to (2 *
		* real value), or (real value / 2), etc. */

		/* gets the next long-enough pulse (this may require more than one pulse) */
		pulse = 0;
		while (pulse < ((avg_size / 4) - (avg_size / 16))) {
			int indx;
			i++;
			if (i >= pulses)
				i = 0;
			indx = idx[i];
			if (rand() <= (indx * RAND_MAX) / maxidx) {
				pulse += avgp[i] - ref_pulse;
				if (indx >= maxidx)
					ref_pulse = 0;
				else
					ref_pulse = avgp[i];
			}
			if (i == eodat)
				outstep++;
			if (outstep == 1 && indexoffset == i)
			    *indexoffsetp = bitoffset;
		}

		/* gets the size in bits from the pulse width, considering the current average bitrate */
		adjusted_pulse = pulse;
		real_size = 0;
		while (adjusted_pulse >= avg_size) {
			real_size += 4;
			adjusted_pulse -= avg_size / 2;
		}
		adjusted_pulse <<= 3;
		while (adjusted_pulse >= ((avg_size * 4) + (avg_size / 4))) {
			real_size += 2;
			adjusted_pulse -= avg_size * 2;
		}
		if (adjusted_pulse >= ((avg_size * 3) + (avg_size / 4))) {
			if (adjusted_pulse <= ((avg_size * 4) - (avg_size / 4))) {
				if ((2 * ((adjusted_pulse >> 2) - adjust)) <= ((2 * avg_size) - (avg_size / 4)))
					real_size += 3;
				else
					real_size += 4;
			} else
				real_size += 4;
		} else {
			if (adjusted_pulse > ((avg_size * 3) - (avg_size / 4))) {
				real_size += 3;
			} else {
				if (adjusted_pulse >= ((avg_size * 2) + (avg_size / 4))) {
					if ((2 * ((adjusted_pulse >> 2) - adjust)) < (avg_size + (avg_size / 4)))
						real_size += 2;
					else
						real_size += 3;
				} else
					real_size += 2;
			}
		}

		if (outstep == 1) {
			for (j = real_size; j > 1; j--)
				addbit (d, 0);
			addbit (d, 1);
			for (j = 0; j <	real_size; j++)
				*pt++ =	(uae_u16)(pulse / real_size);
		}

		/* prepares for the next pulse */
		adjust = ((real_size * avg_size)/8) - pulse;
		total -= psarray[array_index].size;
		totaldiv -= psarray[array_index].number_of_bits;
		psarray[array_index].size = pulse;
		psarray[array_index].number_of_bits = real_size;
		total += pulse;
		totaldiv += real_size;
		array_index++;
		if (array_index	>= FDI_MAX_ARRAY)
			array_index = 0;
	}

	fdi->out = bitoffset;
}

#else

static void fdi2_decode (FDI *fdi, uint32_t totalavg, uae_u32 *avgp, uae_u32 *minp, uae_u32 *maxp, uae_u8 *idx, int maxidx, int *indexoffsetp, int pulses, int mfm)
{
	uint32_t adjust;
	uint32_t adjusted_pulse;
	uint32_t standard_MFM_2_bit_cell_size = totalavg / 50000;
	uint32_t standard_MFM_8_bit_cell_size = totalavg / 12500;
	int real_size, i, j, nexti, eodat, outstep, randval;
	int indexoffset = *indexoffsetp;
	uae_u8 *d = fdi->track_dst_buffer;
	uae_u16	*pt = fdi->track_dst_buffer_timing;
	uae_u32 ref_pulse, pulse;
	long jitter;

	/* detects a long-enough stable pulse coming just after another stable pulse */
	i = 1;
	while ( (i < pulses) && ( (idx[i] < maxidx)
		|| (idx[i - 1] < maxidx)
		|| (minp[i] < (standard_MFM_2_bit_cell_size - (standard_MFM_2_bit_cell_size / 4))) ) )
			i++;
	if (i == pulses)  {
		fdi2raw_log("FDI: No stable and long-enough pulse in track.\n");
		return;
	}
	nexti = i;
	eodat = i;
	i--;
	adjust = 0;
	total = 0;
	totaldiv = 0;
	init_array(standard_MFM_2_bit_cell_size, 1 + mfm);
	bitoffset = 0;
	ref_pulse = 0;
	jitter = 0;
	outstep = -1;
	while (outstep < 2) {

		/* calculates the current average bitrate from previous decoded data */
		uae_u32 avg_size = (total << (2 + mfm)) / totaldiv; /* this is the new average size for one MFM bit */
		/* uae_u32 avg_size = (uae_u32)((((float)total)*((float)(mfm+1))*4.0) / ((float)totaldiv)); */
		/* you can try tighter ranges than 25%, or wider ranges. I would probably go for tighter... */
		if ((avg_size < (standard_MFM_8_bit_cell_size - (pulse_limitval * standard_MFM_8_bit_cell_size / 100))) ||
			(avg_size > (standard_MFM_8_bit_cell_size + (pulse_limitval * standard_MFM_8_bit_cell_size / 100)))) {
				avg_size = standard_MFM_8_bit_cell_size;
		}
		/* this	is to prevent the average value from going too far
		* from the theoretical value, otherwise it could progressively go to (2 *
		* real value), or (real value / 2), etc. */

		/* gets the next long-enough pulse (this may require more than one pulse) */
		pulse = 0;
		while (pulse < ((avg_size / 4) - (avg_size / 16))) {
			uae_u32 avg_pulse, min_pulse, max_pulse;
			i++;
			if (i >= pulses)
				i = 0;
			if (i == nexti) {
				do {
					nexti++;
					if (nexti >= pulses)
						nexti = 0;
				} while (idx[nexti] < maxidx);
			}
			if (idx[i] >= maxidx) { /* stable pulse */
				avg_pulse = avgp[i] - jitter;
				min_pulse = minp[i];
				max_pulse = maxp[i];
				if (jitter >= 0)
					max_pulse -= jitter;
				else
					min_pulse -= jitter;
				if ((maxp[nexti] - avgp[nexti]) < (avg_pulse - min_pulse))
					min_pulse = avg_pulse - (maxp[nexti] - avgp[nexti]);
				if ((avgp[nexti] - minp[nexti]) < (max_pulse - avg_pulse))
					max_pulse = avg_pulse + (avgp[nexti] - minp[nexti]);
				if (min_pulse < ref_pulse)
					min_pulse = ref_pulse;
				randval = rand();
				if (randval < (RAND_MAX / 2)) {
					if (randval > (RAND_MAX / 4)) {
						if (randval <= (((3LL*RAND_MAX) / 8)))
							randval = (2 * randval) - (RAND_MAX /4);
						else
							randval = (4 * randval) - RAND_MAX;
					}
					jitter = 0 - (randval * (avg_pulse - min_pulse)) / RAND_MAX;
				} else {
					randval -= RAND_MAX / 2;
					if (randval > (RAND_MAX / 4)) {
						if (randval <= (((3LL*RAND_MAX) / 8)))
							randval = (2 * randval) - (RAND_MAX /4);
						else
							randval = (4 * randval) - RAND_MAX;
					}
					jitter = (randval * (max_pulse - avg_pulse)) / RAND_MAX;
				}
				avg_pulse += jitter;
				if ((avg_pulse < min_pulse) || (avg_pulse > max_pulse)) {
					fdi2raw_log("FDI: avg_pulse outside bounds! avg=%u min=%u max=%u\n", avg_pulse, min_pulse, max_pulse);
					fdi2raw_log("FDI: avgp=%u (%u) minp=%u (%u) maxp=%u (%u) jitter=%d i=%d ni=%d\n",
						avgp[i], avgp[nexti], minp[i], minp[nexti], maxp[i], maxp[nexti], jitter, i, nexti);
				}
				if (avg_pulse < ref_pulse)
					fdi2raw_log("FDI: avg_pulse < ref_pulse! (%u < %u)\n", avg_pulse, ref_pulse);
				pulse += avg_pulse - ref_pulse;
				ref_pulse = 0;
				if (i == eodat)
					outstep++;
			} else if (rand() <= ((idx[i] * RAND_MAX) / maxidx)) {
				avg_pulse = avgp[i];
				min_pulse = minp[i];
				max_pulse = maxp[i];
				randval = rand();
				if (randval < (RAND_MAX / 2)) {
					if (randval > (RAND_MAX / 4)) {
						if (randval <= (((3LL*RAND_MAX) / 8)))
							randval = (2 * randval) - (RAND_MAX /4);
						else
							randval = (4 * randval) - RAND_MAX;
					}
					avg_pulse -= (randval * (avg_pulse - min_pulse)) / RAND_MAX;
				} else {
					randval -= RAND_MAX / 2;
					if (randval > (RAND_MAX / 4)) {
						if (randval <= (((3LL*RAND_MAX) / 8)))
							randval = (2 * randval) - (RAND_MAX /4);
						else
							randval = (4 * randval) - RAND_MAX;
					}
					avg_pulse += (randval * (max_pulse - avg_pulse)) / RAND_MAX;
				}
				if ((avg_pulse > ref_pulse) && (avg_pulse < (avgp[nexti] - jitter))) {
					pulse += avg_pulse - ref_pulse;
					ref_pulse = avg_pulse;
				}
			}
			if (outstep == 1 && indexoffset == i)
			    *indexoffsetp = bitoffset;
		}

		/* gets the size in bits from the pulse width, considering the current average bitrate */
		adjusted_pulse = pulse;
		real_size = 0;
		if (mfm) {
		    while (adjusted_pulse >= avg_size) {
			    real_size += 4;
			    adjusted_pulse -= avg_size / 2;
		    }
		    adjusted_pulse <<= 3;
		    while (adjusted_pulse >= ((avg_size * 4) + (avg_size / 4))) {
			    real_size += 2;
			    adjusted_pulse -= avg_size * 2;
		    }
		    if (adjusted_pulse >= ((avg_size * 3) + (avg_size / 4))) {
			    if (adjusted_pulse <= ((avg_size * 4) - (avg_size / 4))) {
				    if ((2 * ((adjusted_pulse >> 2) - adjust)) <= ((2 * avg_size) - (avg_size / 4)))
					    real_size += 3;
				    else
					    real_size += 4;
			    } else
				    real_size += 4;
		    } else {
			    if (adjusted_pulse > ((avg_size * 3) - (avg_size / 4))) {
				    real_size += 3;
			    } else {
				    if (adjusted_pulse >= ((avg_size * 2) + (avg_size / 4))) {
					    if ((2 * ((adjusted_pulse >> 2) - adjust)) < (avg_size + (avg_size / 4)))
						    real_size += 2;
					    else
						    real_size += 3;
				    } else
					    real_size += 2;
			    }
		    }
		} else {
		    while (adjusted_pulse >= (2*avg_size))
		    {
			    real_size+=4;
			    adjusted_pulse-=avg_size;
		    }
		    adjusted_pulse<<=2;
		    while (adjusted_pulse >= ((avg_size*3)+(avg_size/4)))
		    {
			    real_size+=2;
			    adjusted_pulse-=avg_size*2;
		    }
		    if (adjusted_pulse >= ((avg_size*2)+(avg_size/4)))
		    {
			    if (adjusted_pulse <= ((avg_size*3)-(avg_size/4)))
			    {
				    if (((adjusted_pulse>>1)-adjust) < (avg_size+(avg_size/4)))
					    real_size+=2;
				    else
					    real_size+=3;
			    }
			    else
				    real_size+=3;
		    }
		    else
		    {
			    if (adjusted_pulse > ((avg_size*2)-(avg_size/4)))
				    real_size+=2;
			    else
			    {
				    if (adjusted_pulse >= (avg_size+(avg_size/4)))
				    {
					    if (((adjusted_pulse>>1)-adjust) <= (avg_size-(avg_size/4)))
						    real_size++;
					    else
						    real_size+=2;
				    }
				    else
					    real_size++;
			    }
		    }
		}

		/* after one pass to correctly initialize the average bitrate, outputs the bits */
		if (outstep == 1) {
			for (j = real_size; j > 1; j--)
				addbit (d, 0);
			addbit (d, 1);
			for (j = 0; j < real_size; j++)
				*pt++ = (uae_u16)(pulse / real_size);
		}

		/* prepares for the next pulse */
		adjust = ((real_size * avg_size) / (4 << mfm)) - pulse;
		total -= psarray[array_index].size;
		totaldiv -= psarray[array_index].number_of_bits;
		psarray[array_index].size = pulse;
		psarray[array_index].number_of_bits = real_size;
		total += pulse;
		totaldiv += real_size;
		array_index++;
		if (array_index >= FDI_MAX_ARRAY)
			array_index = 0;
	}

	fdi->out = bitoffset;
}

#endif

static void fdi2_celltiming (FDI *fdi, uint32_t totalavg, int bitoffset, uae_u16 *out)
{
	uae_u16 *pt2, *pt;
	double avg_bit_len;
	int i;

	avg_bit_len = (double)totalavg / (double)bitoffset;
	pt2 = fdi->track_dst_buffer_timing;
	pt = out;
	for (i = 0; i < bitoffset / 8; i++) {
		double v = (pt2[0] + pt2[1] + pt2[2] + pt2[3] + pt2[4] + pt2[5] + pt2[6] + pt2[7]) / 8.0;
		v = 1000.0 * v / avg_bit_len;
		*pt++ = (uae_u16)v;
		pt2 += 8;
	}
	*pt++ = out[0];
	*pt = out[0];
}

static int decode_lowlevel_track (FDI *fdi, int track, struct fdi_cache *cache)
{
	uae_u8 *p1;
	uae_u32 *p2;
	uae_u32 *avgp, *minp = 0, *maxp = 0;
	uae_u8 *idxp = 0;
	uae_u32 maxidx, totalavg, weakbits;
	int i, j, len, pulses, indexoffset;
	int avg_free, min_free = 0, max_free = 0, idx_free;
	int idx_off1 = 0, idx_off2 = 0, idx_off3 = 0;

	p1 = fdi->track_src;
	pulses = get_u32 (p1);
	if (!pulses)
		return -1;
	p1 += 4;
	len = 12;
	avgp = (uae_u32*)fdi_decompress (pulses, p1 + 0, p1 + len, &avg_free);
	dumpstream(track, (uae_u8*)avgp, pulses);
	len += get_u24 (p1 + 0) & 0x3fffff;
	if (!avgp)
		return -1;
	if (get_u24 (p1 + 3) && get_u24 (p1 + 6)) {
		minp = (uae_u32*)fdi_decompress (pulses, p1 + 3, p1 + len, &min_free);
		len += get_u24 (p1 + 3) & 0x3fffff;
		maxp = (uae_u32*)fdi_decompress (pulses, p1 + 6, p1 + len, &max_free);
		len += get_u24 (p1 + 6) & 0x3fffff;
		/* Computes the real min and max values */
		for (i = 0; i < pulses; i++) {
			maxp[i] = avgp[i] + minp[i] - maxp[i];
			minp[i] = avgp[i] - minp[i];
		}
	} else {
		minp = avgp;
		maxp = avgp;
	}
	if (get_u24 (p1 + 9)) {
		idx_off1 = 0;
		idx_off2 = 1;
		idx_off3 = 2;
		idxp = fdi_decompress (pulses, p1 + 9, p1 + len, &idx_free);
		if (idx_free) {
			if (idxp[0] == 0 && idxp[1] == 0) {
				idx_off1 = 2;
				idx_off2 = 3;
			} else {
				idx_off1 = 1;
				idx_off2 = 0;
			}
			idx_off3 = 4;
		}
	} else {
		idxp = fdi_malloc (pulses * 2);
		idx_free = 1;
		for (i = 0; i < pulses; i++) {
			idxp[i * 2 + 0] = 2;
			idxp[i * 2 + 1] = 0;
		}
		idxp[0] = 1;
		idxp[1] = 1;
	}

	maxidx = 0;
	indexoffset = 0;
	p1 = idxp;
	for (i = 0; i < pulses; i++) {
		if ((uint32_t)p1[idx_off1] + (uint32_t)p1[idx_off2] > maxidx)
			maxidx = p1[idx_off1] + p1[idx_off2];
		p1 += idx_off3;
	}
	p1 = idxp;
	for (i = 0; (i < pulses) && (p1[idx_off2] != 0); i++) /* falling edge, replace with idx_off1 for rising edge */
		p1 += idx_off3;
	if (i < pulses) {
		j = i;
		do {
			i++;
			p1 += idx_off3;
			if (i >= pulses) {
				i = 0;
				p1 = idxp;
			}
		} while ((i != j) && (p1[idx_off2] == 0)); /* falling edge, replace with idx_off1 for rising edge */
		if (i != j) /* index pulse detected */
		{
			while ((i != j) && (p1[idx_off1] > p1[idx_off2])) { /* falling edge, replace with "<" for rising edge */
				i++;
				p1 += idx_off3;
				if (i >= pulses) {
					i = 0;
					p1 = idxp;
				}
			}
			if (i != j)
				indexoffset = i; /* index position detected */
		}
	}
	p1 = idxp;
	p2 = avgp;
	totalavg = 0;
	weakbits = 0;
	for (i = 0; i < pulses; i++) {
		uint32_t sum = p1[idx_off1] + p1[idx_off2];
		if (sum >= maxidx) {
			totalavg += *p2;
		} else {
			weakbits++;
		}
		p2++;
		p1 += idx_off3;
		idxp[i] = sum;
	}
	len = totalavg / 100000;
	/* fdi2raw_log("totalavg=%u index=%d (%d) maxidx=%d weakbits=%d len=%d\n",
		totalavg, indexoffset, maxidx, weakbits, len); */
	cache->avgp = avgp;
	cache->idxp = idxp;
	cache->minp = minp;
	cache->maxp = maxp;
	cache->avg_free = avg_free;
	cache->idx_free = idx_free;
	cache->min_free = min_free;
	cache->max_free = max_free;
	cache->totalavg = totalavg;
	cache->pulses = pulses;
	cache->maxidx = maxidx;
	cache->indexoffset = indexoffset;
	cache->weakbits = weakbits;
	cache->lowlevel = 1;

	return 1;
}

static unsigned char fdiid[]={"Formatted Disk Image file"};
static int bit_rate_table[16] = { 125,150,250,300,500,1000 };

void fdi2raw_header_free (FDI *fdi)
{
	int i;

	fdi_free (fdi->mfmsync_buffer);
	fdi_free (fdi->track_src_buffer);
	fdi_free (fdi->track_dst_buffer);
	fdi_free (fdi->track_dst_buffer_timing);
	for (i = 0; i <	MAX_TRACKS; i++) {
		struct fdi_cache *c = &fdi->cache[i];
		if (c->idx_free)
			fdi_free (c->idxp);
		if (c->avg_free)
			fdi_free (c->avgp);
		if (c->min_free)
			fdi_free (c->minp);
		if (c->max_free)
			fdi_free (c->maxp);
	}
	fdi_free (fdi);
	fdi2raw_log("FREE: memory allocated %d\n", fdi_allocated);
}

int fdi2raw_get_last_track (FDI *fdi)
{
	return fdi->last_track;
}

int fdi2raw_get_num_sector (FDI *fdi)
{
	if (fdi->header[152] == 0x02)
		return 22;
	return 11;
}

int fdi2raw_get_last_head (FDI *fdi)
{
	return fdi->last_head;
}

int fdi2raw_get_rotation (FDI *fdi)
{
	return fdi->rotation_speed;
}

int fdi2raw_get_bit_rate (FDI *fdi)
{
	return fdi->bit_rate;
}

int fdi2raw_get_type (FDI *fdi)
{
	return fdi->disk_type;
}

int fdi2raw_get_write_protect (FDI *fdi)
{
	return fdi->write_protect;
}

int fdi2raw_get_tpi (FDI *fdi)
{
	return fdi->header[148];
}

FDI *fdi2raw_header(FILE *f)
{
	int i, offset, oldseek;
	uae_u8 type, size;
	FDI *fdi;

	fdi2raw_log("ALLOC: memory allocated %d\n", fdi_allocated);
	fdi = fdi_malloc(sizeof(FDI));
	memset (fdi, 0, sizeof (FDI));
	fdi->file = f;
	oldseek = ftell (fdi->file);
	fseek (fdi->file, 0, SEEK_SET);
	fread (fdi->header, 2048, 1, fdi->file);
	fseek (fdi->file, oldseek, SEEK_SET);
	if (memcmp (fdiid, fdi->header, strlen ((char *)fdiid)) ) {
		fdi_free(fdi);
		return NULL;
	}
	if ((fdi->header[140] != 1 && fdi->header[140] != 2) || (fdi->header[141] != 0 && !(fdi->header[140]==2 && fdi->header[141]==1))) {
		fdi_free(fdi);
		return NULL;
	}

	fdi->mfmsync_buffer = fdi_malloc (MAX_MFM_SYNC_BUFFER * sizeof(int));
	fdi->track_src_buffer = fdi_malloc (MAX_SRC_BUFFER);
	fdi->track_dst_buffer = fdi_malloc (MAX_DST_BUFFER);
	fdi->track_dst_buffer_timing = fdi_malloc (MAX_TIMING_BUFFER);

	fdi->last_track = ((fdi->header[142] << 8) + fdi->header[143]) + 1;
	fdi->last_track *= fdi->header[144] + 1;
	if (fdi->last_track > MAX_TRACKS)
		fdi->last_track = MAX_TRACKS;
	fdi->last_head = fdi->header[144];
	fdi->disk_type = fdi->header[145];
	fdi->rotation_speed = fdi->header[146] + 128;
	fdi->write_protect = fdi->header[147] & 1;
	fdi2raw_log("FDI version %d.%d\n", fdi->header[140], fdi->header[141]);
	fdi2raw_log("last_track=%d rotation_speed=%d\n",fdi->last_track,fdi->rotation_speed);

	offset = 512;
	i = fdi->last_track;
	if (i > 180) {
		offset += 512;
		i -= 180;
		while (i > 256) {
			offset += 512;
			i -= 256;
		}
	}
	for (i = 0; i < fdi->last_track; i++) {
		fdi->track_offsets[i] = offset;
		type = fdi->header[152 + i * 2];
		size = fdi->header[152 + i * 2 + 1];
		if (type == 1)
			offset += (size & 15) * 512;
		else if ((type & 0xc0) == 0x80)
			offset += (((type & 0x3f) << 8) | size) * 256;
		else
			offset += size * 256;
	}
	fdi->track_offsets[i] = offset;

	return fdi;
}


int fdi2raw_loadrevolution_2 (FDI *fdi, uae_u16 *mfmbuf, uae_u16 *tracktiming, int track, int *tracklength, int *indexoffsetp, int *multirev, int mfm)
{
	struct fdi_cache *cache = &fdi->cache[track];
	int len, i, idx;

	memset (fdi->track_dst_buffer, 0, MAX_DST_BUFFER);
	idx = cache->indexoffset;
	fdi2_decode (fdi, cache->totalavg,
		cache->avgp, cache->minp, cache->maxp, cache->idxp,
		cache->maxidx, &idx, cache->pulses, mfm);
	/* fdi2raw_log("track %d: nbits=%d avg len=%.2f weakbits=%d idx=%d\n",
		track, bitoffset, (double)cache->totalavg / bitoffset, cache->weakbits, cache->indexoffset); */
	len = fdi->out;
	if (cache->weakbits >= 10 && multirev)
		*multirev = 1;
	*tracklength = len;

	for (i = 0; i < (len + 15) / (2 * 8); i++) {
		uae_u8 *data = fdi->track_dst_buffer + i * 2;
		*mfmbuf++ = 256 * *data + *(data + 1);
	}
	fdi2_celltiming (fdi, cache->totalavg, len, tracktiming);
	if (indexoffsetp)
		*indexoffsetp = idx;
	return 1;
}

int fdi2raw_loadrevolution (FDI *fdi, uae_u16 *mfmbuf, uae_u16 *tracktiming, int track, int *tracklength, int mfm)
{
	return fdi2raw_loadrevolution_2 (fdi, mfmbuf, tracktiming, track, tracklength, 0, 0, mfm);
}

int fdi2raw_loadtrack (FDI *fdi, uae_u16 *mfmbuf, uae_u16 *tracktiming, int track, int *tracklength, int *indexoffsetp, int *multirev, int mfm)
{
	uae_u8 *p;
	int outlen, i;
	struct fdi_cache *cache = &fdi->cache[track];

	if (cache->lowlevel)
		return fdi2raw_loadrevolution_2 (fdi, mfmbuf, tracktiming, track, tracklength, indexoffsetp, multirev, mfm);

	fdi->err = 0;
	fdi->track_src_len = fdi->track_offsets[track + 1] - fdi->track_offsets[track];
	fseek (fdi->file, fdi->track_offsets[track], SEEK_SET);
	fread (fdi->track_src_buffer, fdi->track_src_len, 1, fdi->file);
	memset (fdi->track_dst_buffer, 0, MAX_DST_BUFFER);
	fdi->track_dst_buffer_timing[0] = 0;

	fdi->current_track = track;
	fdi->track_src = fdi->track_src_buffer;
	fdi->track_dst = fdi->track_dst_buffer;
	p = fdi->header + 152 + fdi->current_track * 2;
	fdi->track_type = *p++;
	fdi->track_len = *p++;
	fdi->bit_rate = 0;
	fdi->out = 0;
	fdi->mfmsync_offset = 0;

	if ((fdi->track_type & 0xf0) == 0xf0 || (fdi->track_type & 0xf0) == 0xe0)
		fdi->bit_rate = bit_rate_table[fdi->track_type & 0x0f];
	else
		fdi->bit_rate = 250;

	/* fdi2raw_log("track %d: srclen: %d track_type: %02.2X, bitrate: %d\n",
		fdi->current_track, fdi->track_src_len, fdi->track_type, fdi->bit_rate); */

	if ((fdi->track_type & 0xc0) == 0x80) {

		outlen = decode_lowlevel_track (fdi, track, cache);

	} else if ((fdi->track_type & 0xf0) == 0xf0) {

		outlen = decode_raw_track (fdi);

	} else if ((fdi->track_type & 0xf0) == 0xe0) {

		outlen = handle_sectors_described_track (fdi);

	} else if ((fdi->track_type & 0xf0)) {

		zxx (fdi);
		outlen = -1;

	} else if (fdi->track_type < 0x10) {

		decode_normal_track[fdi->track_type](fdi);
		fix_mfm_sync (fdi);
		outlen = fdi->out;

	} else {

		zxx (fdi);
		outlen = -1;

	}

	if (fdi->err)
		return 0;

	if (outlen > 0) {
		if (cache->lowlevel)
			return fdi2raw_loadrevolution_2 (fdi, mfmbuf, tracktiming, track, tracklength, indexoffsetp, multirev, mfm);
		*tracklength = fdi->out;
		for (i = 0; i < ((*tracklength) + 15) / (2 * 8); i++) {
			uae_u8 *data = fdi->track_dst_buffer + i * 2;
			*mfmbuf++ = 256 * *data + *(data + 1);
		}
	}
	return outlen;
}

