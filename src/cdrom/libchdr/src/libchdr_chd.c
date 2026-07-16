/***************************************************************************

    chd.c

    MAME Compressed Hunks of Data file format

****************************************************************************

    Copyright Aaron Giles
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

        * Redistributions of source code must retain the above copyright
          notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
          notice, this list of conditions and the following disclaimer in
          the documentation and/or other materials provided with the
          distribution.
        * Neither the name 'MAME' nor the names of its contributors may be
          used to endorse or promote products derived from this software
          without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY AARON GILES ''AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL AARON GILES BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
    IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

***************************************************************************/

/* Feature test macros: must come before any system header.
 * Ensures fseeko/ftello are declared under strict C99/C11 and
 * that 64-bit file offsets are used on 32-bit platforms (armhf,
 * i386, and other time64/LFS rebuilds). */
#if !defined(_WIN32) && !defined(__PS3__) && !defined(__SWITCH__) && !defined(__vita__)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef __ANDROID__
#    ifndef _FILE_OFFSET_BITS
#      define _FILE_OFFSET_BITS 64
#    endif
#  endif
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include "../include/libchdr/chd.h"
#include "../include/libchdr/cdrom.h"
#include "codec_cdfl.h"
#include "codec_cdlz.h"
#include "codec_cdzl.h"
#include "codec_cdzs.h"
#include "codec_flac.h"
#include "codec_huff.h"
#include "codec_lzma.h"
#include "codec_zlib.h"
#include "codec_zstd.h"
#include "../include/libchdr/huffman.h"
#include "../include/libchdr/macros.h"

#include "../deps/lzma-25.01/include/LzmaDec.h"

#undef TRUE
#undef FALSE
#define TRUE 1
#define FALSE 0

#define SHA1_DIGEST_SIZE 20

/***************************************************************************
    CONSTANTS
***************************************************************************/

#define MAP_STACK_ENTRIES			512			/* max number of entries to use on the stack */
#define MAP_ENTRY_SIZE				16			/* V3 and later */
#define OLD_MAP_ENTRY_SIZE			8			/* V1-V2 */
#define METADATA_HEADER_SIZE		16			/* metadata header size */

#define MAP_ENTRY_FLAG_TYPE_MASK	0x0f		/* what type of hunk */
#define MAP_ENTRY_FLAG_NO_CRC		0x10		/* no CRC is present */

#define CHD_V1_SECTOR_SIZE			512			/* size of a "sector" in the V1 header */

#define CHD_MAX_HUNK_SIZE				(128 * 1024 * 1024) /* hunk size probably shouldn't be more than 128MB */

/* Sanity cap on logical (decompressed) CHD size. Large enough to cover
 * any realistic disc image: BD50 PS3 ISOs (50 GB), arcade laserdisc
 * CHDs, and future-proofing for larger optical formats. 1 TB is well
 * under any integer-overflow threshold, and actual allocation safety
 * is further bounded by CHD_MAX_HUNK_SIZE and the totalhunks check in
 * header_read. Previously 10 GB, which rejected legitimate PS3 ISOs
 * (#147). */
#define CHD_MAX_FILE_SIZE				(1024ULL * 1024 * 1024 * 1024)

#define COOKIE_VALUE				0xbaadf00d

#define END_OF_LIST_COOKIE			"EndOfListCookie"

#define NO_MATCH					(~0)

/* V3-V4 entry types */
enum
{
	V34_MAP_ENTRY_TYPE_INVALID = 0,             /* invalid type */
	V34_MAP_ENTRY_TYPE_COMPRESSED = 1,          /* standard compression */
	V34_MAP_ENTRY_TYPE_UNCOMPRESSED = 2,        /* uncompressed data */
	V34_MAP_ENTRY_TYPE_MINI = 3,                /* mini: use offset as raw data */
	V34_MAP_ENTRY_TYPE_SELF_HUNK = 4,           /* same as another hunk in this file */
	V34_MAP_ENTRY_TYPE_PARENT_HUNK = 5,         /* same as a hunk in the parent file */
	V34_MAP_ENTRY_TYPE_2ND_COMPRESSED = 6       /* compressed with secondary algorithm (usually FLAC CDDA) */
};

/* V5 compression types */
enum
{
	/* codec #0
	 * these types are live when running */
	COMPRESSION_TYPE_0 = 0,
	/* codec #1 */
	COMPRESSION_TYPE_1 = 1,
	/* codec #2 */
	COMPRESSION_TYPE_2 = 2,
	/* codec #3 */
	COMPRESSION_TYPE_3 = 3,
	/* no compression; implicit length = hunkbytes */
	COMPRESSION_NONE = 4,
	/* same as another block in this chd */
	COMPRESSION_SELF = 5,
	/* same as a hunk's worth of units in the parent chd */
	COMPRESSION_PARENT = 6,

	/* start of small RLE run (4-bit length)
	 * these additional pseudo-types are used for compressed encodings: */
	COMPRESSION_RLE_SMALL,
	/* start of large RLE run (8-bit length) */
	COMPRESSION_RLE_LARGE,
	/* same as the last COMPRESSION_SELF block */
	COMPRESSION_SELF_0,
	/* same as the last COMPRESSION_SELF block + 1 */
	COMPRESSION_SELF_1,
	/* same block in the parent */
	COMPRESSION_PARENT_SELF,
	/* same as the last COMPRESSION_PARENT block */
	COMPRESSION_PARENT_0,
	/* same as the last COMPRESSION_PARENT block + 1 */
	COMPRESSION_PARENT_1
};

/***************************************************************************
    MACROS
***************************************************************************/

#define EARLY_EXIT(x)				do { (void)(x); goto cleanup; } while (0)

/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

/* interface to a codec */
typedef struct _codec_interface codec_interface;
struct _codec_interface
{
	uint32_t		compression;								/* type of compression */
	const char *compname;									/* name of the algorithm */
	uint8_t		lossy;										/* is this a lossy algorithm? */
	chd_error	(*init)(void *codec, uint32_t hunkbytes);		/* codec initialize */
	void		(*free)(void *codec);						/* codec free */
	chd_codec_interface_decompress	decompress; /* decompress data */
	chd_error	(*config)(void *codec, int param, void *config); /* configure */
};

/* a single map entry */
typedef struct _map_entry map_entry;
struct _map_entry
{
	uint64_t					offset;			/* offset within the file of the data */
	uint32_t					crc;			/* 32-bit CRC of the data */
	uint32_t					length;			/* length of the data */
	uint8_t					flags;			/* misc flags */
};

/* a single metadata entry */
typedef struct _metadata_entry metadata_entry;
struct _metadata_entry
{
	uint64_t					offset;			/* offset within the file of the header */
	uint64_t					next;			/* offset within the file of the next header */
	uint64_t					prev;			/* offset within the file of the previous header */
	uint32_t					length;			/* length of the metadata */
	uint32_t					metatag;		/* metadata tag */
	uint8_t					flags;			/* flag bits */
};

/* internal representation of an open CHD file */
struct _chd_file
{
	uint32_t					cookie;			/* cookie, should equal COOKIE_VALUE */

	core_file_callbacks_and_argp	file;			/* handle to the open core file */
	uint64_t				file_size;		/* size of the core file */
	chd_header				header;			/* header, extracted from file */

	chd_file *				parent;			/* pointer to parent file, or NULL */

	map_entry *				map;			/* array of map entries */

	uint8_t *					compressed;		/* pointer to buffer for compressed data */
	const codec_interface *	codecintf[4];	/* interface to the codec */

	struct
	{
		zlib_codec_data			zlib;		/* zlib codec data */
		lzma_codec_data			lzma;		/* lzma codec data */
		huff_codec_data			huff;		/* huff codec data */
		flac_codec_data			flac;		/* flac codec data */
		zstd_codec_data			zstd;		/* zstd codec data */
		cdzl_codec_data			cdzl;		/* cdzl codec data */
		cdlz_codec_data			cdlz;		/* cdlz codec data */
		cdfl_codec_data			cdfl;		/* cdfl codec data */
		cdzs_codec_data			cdzs;		/* cdzs codec data */
	} codec_data;

	uint8_t *					file_cache;		/* cache of underlying file */
};


/***************************************************************************
    GLOBAL VARIABLES
***************************************************************************/

static const uint8_t nullmd5[CHD_MD5_BYTES] = { 0 };
static const uint8_t nullsha1[CHD_SHA1_BYTES] = { 0 };

/***************************************************************************
    PROTOTYPES
***************************************************************************/

/* core_file_callbacks wrappers over stdio */
static void *core_stdio_fopen(char const *path);
static uint64_t core_stdio_fsize(void *file);
static size_t core_stdio_fread(void *ptr, size_t size, size_t nmemb, void *file);
static int core_stdio_fclose(void *file);
static int core_stdio_fclose_nonowner(void *file); /* alternate fclose used by chd_open_file */
static int core_stdio_fseek(void* file, int64_t offset, int whence);

/* Legacy core_file wrappers */
static uint64_t core_legacy_fsize(void *file);
static size_t core_legacy_fread(void *ptr, size_t size, size_t nmemb, void *file);
static int core_legacy_fclose(void *file);
static int core_legacy_fseek(void* file, int64_t offset, int whence);

/* internal header operations */
static chd_error header_read(chd_file *chd);

/* internal hunk read/write */
static chd_error hunk_read_into_memory(chd_file *chd, uint32_t hunknum, uint8_t *dest);

/* internal map access */
static chd_error map_read(chd_file *chd);

/* metadata management */
static chd_error metadata_find_entry(chd_file *chd, uint32_t metatag, uint32_t metaindex, metadata_entry *metaentry);


/***************************************************************************
    CODEC INTERFACES
***************************************************************************/

static const codec_interface codec_interfaces[] =
{
	/* "none" or no compression */
	{
		CHDCOMPRESSION_NONE,
		"none",
		FALSE,
		NULL,
		NULL,
		NULL,
		NULL
	},

	/* standard zlib compression */
	{
		CHDCOMPRESSION_ZLIB,
		"zlib",
		FALSE,
		zlib_codec_init,
		zlib_codec_free,
		zlib_codec_decompress,
		NULL
	},

	/* zlib+ compression */
	{
		CHDCOMPRESSION_ZLIB_PLUS,
		"zlib+",
		FALSE,
		zlib_codec_init,
		zlib_codec_free,
		zlib_codec_decompress,
		NULL
	},

	/* V5 zlib compression */
	{
		CHD_CODEC_ZLIB,
		"zlib (Deflate)",
		FALSE,
		zlib_codec_init,
		zlib_codec_free,
		zlib_codec_decompress,
		NULL
	},

	/* V5 lzma compression */
	{
		CHD_CODEC_LZMA,
		"lzma (LZMA)",
		FALSE,
		lzma_codec_init,
		lzma_codec_free,
		lzma_codec_decompress,
		NULL
	},

	/* V5 huffman compression */
	{
		CHD_CODEC_HUFFMAN,
		"Huffman",
		FALSE,
		huff_codec_init,
		huff_codec_free,
		huff_codec_decompress,
		NULL
	},

	/* V5 flac compression */
	{
		CHD_CODEC_FLAC,
		"flac (FLAC)",
		FALSE,
		flac_codec_init,
		flac_codec_free,
		flac_codec_decompress,
		NULL
	},
	/* V5 zstd compression */
	{
		CHD_CODEC_ZSTD,
		"ZStandard",
		FALSE,
		zstd_codec_init,
		zstd_codec_free,
		zstd_codec_decompress,
		NULL
	},

	/* V5 CD zlib compression */
	{
		CHD_CODEC_CD_ZLIB,
		"cdzl (CD Deflate)",
		FALSE,
		cdzl_codec_init,
		cdzl_codec_free,
		cdzl_codec_decompress,
		NULL
	},

	/* V5 CD lzma compression */
	{
		CHD_CODEC_CD_LZMA,
		"cdlz (CD LZMA)",
		FALSE,
		cdlz_codec_init,
		cdlz_codec_free,
		cdlz_codec_decompress,
		NULL
	},

	/* V5 CD flac compression */
	{
		CHD_CODEC_CD_FLAC,
		"cdfl (CD FLAC)",
		FALSE,
		cdfl_codec_init,
		cdfl_codec_free,
		cdfl_codec_decompress,
		NULL
	},
	/* V5 CD zstd compression */
	{
		CHD_CODEC_CD_ZSTD,
		"cdzs (CD ZStandard)",
		FALSE,
		cdzs_codec_init,
		cdzs_codec_free,
		cdzs_codec_decompress,
		NULL
	}
	
};

/***************************************************************************
    INLINE FUNCTIONS
***************************************************************************/

/*-------------------------------------------------
    seek_and_read - read data from file at
	specified position
-------------------------------------------------*/

static CHDR_INLINE int seek_and_read(chd_file *chd, uint64_t position, void *buffer, size_t total_bytes)
{
	if (core_fseek(&chd->file, position, SEEK_SET) != 0)
		return FALSE;
	if (core_fread(&chd->file, buffer, total_bytes) != total_bytes)
		return FALSE;

	return TRUE;
}

/*-------------------------------------------------
    get_bigendian_uint64_t - fetch a uint64_t from
    the data stream in bigendian order
-------------------------------------------------*/

static CHDR_INLINE uint64_t get_bigendian_uint64_t(const uint8_t *base)
{
	return ((uint64_t)base[0] << 56) | ((uint64_t)base[1] << 48) | ((uint64_t)base[2] << 40) | ((uint64_t)base[3] << 32) |
			((uint64_t)base[4] << 24) | ((uint64_t)base[5] << 16) | ((uint64_t)base[6] << 8) | (uint64_t)base[7];
}

/*-------------------------------------------------
    put_bigendian_uint64_t - write a uint64_t to
    the data stream in bigendian order
-------------------------------------------------*/

static CHDR_INLINE void put_bigendian_uint64_t(uint8_t *base, uint64_t value)
{
	base[0] = value >> 56;
	base[1] = value >> 48;
	base[2] = value >> 40;
	base[3] = value >> 32;
	base[4] = value >> 24;
	base[5] = value >> 16;
	base[6] = value >> 8;
	base[7] = value;
}

/*-------------------------------------------------
    get_bigendian_uint48 - fetch a UINT48 from
    the data stream in bigendian order
-------------------------------------------------*/

static CHDR_INLINE uint64_t get_bigendian_uint48(const uint8_t *base)
{
	return  ((uint64_t)base[0] << 40) | ((uint64_t)base[1] << 32) |
			((uint64_t)base[2] << 24) | ((uint64_t)base[3] << 16) | ((uint64_t)base[4] << 8) | (uint64_t)base[5];
}

/*-------------------------------------------------
    put_bigendian_uint48 - write a UINT48 to
    the data stream in bigendian order
-------------------------------------------------*/

static CHDR_INLINE void put_bigendian_uint48(uint8_t *base, uint64_t value)
{
	value &= 0xffffffffffff;
	base[0] = value >> 40;
	base[1] = value >> 32;
	base[2] = value >> 24;
	base[3] = value >> 16;
	base[4] = value >> 8;
	base[5] = value;
}
/*-------------------------------------------------
    get_bigendian_uint32_t - fetch a uint32_t from
    the data stream in bigendian order
-------------------------------------------------*/

static CHDR_INLINE uint32_t get_bigendian_uint32_t(const uint8_t *base)
{
	return (base[0] << 24) | (base[1] << 16) | (base[2] << 8) | base[3];
}

/*-------------------------------------------------
    put_bigendian_uint32_t - write a uint32_t to
    the data stream in bigendian order
-------------------------------------------------*/

static CHDR_INLINE void put_bigendian_uint32_t(uint8_t *base, uint32_t value)
{
	base[0] = value >> 24;
	base[1] = value >> 16;
	base[2] = value >> 8;
	base[3] = value;
}

/*-------------------------------------------------
    put_bigendian_uint24 - write a UINT24 to
    the data stream in bigendian order
-------------------------------------------------*/

static CHDR_INLINE void put_bigendian_uint24(uint8_t *base, uint32_t value)
{
	value &= 0xffffff;
	base[0] = value >> 16;
	base[1] = value >> 8;
	base[2] = value;
}

/*-------------------------------------------------
    get_bigendian_uint24 - fetch a UINT24 from
    the data stream in bigendian order
-------------------------------------------------*/

static CHDR_INLINE uint32_t get_bigendian_uint24(const uint8_t *base)
{
	return (base[0] << 16) | (base[1] << 8) | base[2];
}

/*-------------------------------------------------
    get_bigendian_uint16 - fetch a uint16_t from
    the data stream in bigendian order
-------------------------------------------------*/

static CHDR_INLINE uint16_t get_bigendian_uint16(const uint8_t *base)
{
	return (base[0] << 8) | base[1];
}

/*-------------------------------------------------
    put_bigendian_uint16 - write a uint16_t to
    the data stream in bigendian order
-------------------------------------------------*/

static CHDR_INLINE void put_bigendian_uint16(uint8_t *base, uint16_t value)
{
	base[0] = value >> 8;
	base[1] = value;
}

/*-------------------------------------------------
    map_extract - extract a single map
    entry from the datastream
-------------------------------------------------*/

static CHDR_INLINE void map_extract(const uint8_t *base, map_entry *entry)
{
	entry->offset = get_bigendian_uint64_t(&base[0]);
	entry->crc = get_bigendian_uint32_t(&base[8]);
	entry->length = get_bigendian_uint16(&base[12]) | (base[14] << 16);
	entry->flags = base[15];
}

/*-------------------------------------------------
    map_size_v5 - calculate CHDv5 map size
-------------------------------------------------*/
static CHDR_INLINE int map_size_v5(chd_header* header, size_t *size)
{
	/* Avoid overflow due to corrupted data. */
	const size_t max_hunkcount = ((size_t)-1 / header->mapentrybytes);
	if (header->hunkcount > max_hunkcount)
		return FALSE;

	*size = (size_t)header->hunkcount * header->mapentrybytes;
	return TRUE;
}

/*-------------------------------------------------
    crc16 - calculate CRC16 (from hashing.cpp)
-------------------------------------------------*/
uint16_t crc16(const void *data, uint32_t length)
{
	uint16_t crc = 0xffff;

	static const uint16_t s_table[256] =
	{
		0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
		0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
		0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
		0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
		0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
		0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
		0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
		0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
		0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
		0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
		0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
		0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
		0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
		0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
		0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
		0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
		0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
		0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
		0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
		0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
		0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
		0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
		0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
		0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
		0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
		0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
		0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
		0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
		0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
		0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
		0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
		0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
	};

	const uint8_t *src = (uint8_t*)data;

	/* fetch the current value into a local and rip through the source data */
	while (length-- != 0)
		crc = (crc << 8) ^ s_table[(crc >> 8) ^ *src++];
	return crc;
}

/*-------------------------------------------------
	compressed - test if CHD file is compressed
+-------------------------------------------------*/
static CHDR_INLINE int chd_compressed(chd_header* header) {
	return header->compression[0] != CHD_CODEC_NONE;
}

/*-------------------------------------------------
	decompress_v5_map - decompress the v5 map
-------------------------------------------------*/

static chd_error decompress_v5_map(chd_file* chd, chd_header* header)
{
	uint32_t hunknum;
	int repcount = 0;
	uint8_t lastcomp = 0;
	uint32_t last_self = 0;
	uint64_t last_parent = 0;
	struct bitstream* bitbuf;
	uint32_t mapbytes;
	uint64_t firstoffs;
	uint16_t mapcrc;
	uint8_t lengthbits;
	uint8_t selfbits;
	uint8_t parentbits;
	uint8_t *compressed_ptr;
	uint8_t rawbuf[16];
	struct huffman_decoder* decoder;
	enum huffman_error err;
	uint64_t curoffset;
	size_t rawmapsize;

	if (!map_size_v5(header, &rawmapsize))
		return CHDERR_INVALID_FILE;

	if (!chd_compressed(header))
	{
		if ((header->mapoffset + rawmapsize) >= chd->file_size || (header->mapoffset + rawmapsize) < header->mapoffset)
			return CHDERR_INVALID_FILE;

		header->rawmap = (uint8_t*)malloc(rawmapsize);
		if (header->rawmap == NULL)
			return CHDERR_OUT_OF_MEMORY;
		if (!seek_and_read(chd, header->mapoffset, header->rawmap, rawmapsize))
			return CHDERR_READ_ERROR;
		return CHDERR_NONE;
	}

	/* read the header */
	if (!seek_and_read(chd, header->mapoffset, rawbuf, sizeof(rawbuf)))
		return CHDERR_READ_ERROR;
	mapbytes = get_bigendian_uint32_t(&rawbuf[0]);
	firstoffs = get_bigendian_uint48(&rawbuf[4]);
	mapcrc = get_bigendian_uint16(&rawbuf[10]);
	lengthbits = rawbuf[12];
	selfbits = rawbuf[13];
	parentbits = rawbuf[14];

	/* now read the map */
	if ((header->mapoffset + mapbytes) < header->mapoffset || (header->mapoffset + mapbytes) >= chd->file_size)
		return CHDERR_INVALID_FILE;
	compressed_ptr = (uint8_t*)malloc(sizeof(uint8_t) * mapbytes);
	if (compressed_ptr == NULL)
		return CHDERR_OUT_OF_MEMORY;
	if (!seek_and_read(chd, header->mapoffset + 16, compressed_ptr, mapbytes))
	{
		free(compressed_ptr);
		return CHDERR_READ_ERROR;
	}
	bitbuf = create_bitstream(compressed_ptr, sizeof(uint8_t) * mapbytes);
	header->rawmap = (uint8_t*)malloc(rawmapsize);
	if (header->rawmap == NULL)
	{
		free(compressed_ptr);
		free(bitbuf);
		return CHDERR_OUT_OF_MEMORY;
	}

	/* first decode the compression types */
	decoder = create_huffman_decoder(16, 8);
	if (decoder == NULL)
	{
		free(compressed_ptr);
		free(bitbuf);
		return CHDERR_OUT_OF_MEMORY;
	}

	err = huffman_import_tree_rle(decoder, bitbuf);
	if (err != HUFFERR_NONE)
	{
		free(compressed_ptr);
		free(bitbuf);
		delete_huffman_decoder(decoder);
		return CHDERR_DECOMPRESSION_ERROR;
	}

	for (hunknum = 0; hunknum < header->hunkcount; hunknum++)
	{
		uint8_t *rawmap = header->rawmap + (hunknum * 12);
		if (repcount > 0)
			rawmap[0] = lastcomp, repcount--;
		else
		{
			uint8_t val;
			if (bitstream_overflow(bitbuf))
			{
				free(compressed_ptr);
				free(bitbuf);
				delete_huffman_decoder(decoder);
				return CHDERR_DECOMPRESSION_ERROR;
			}

			val = huffman_decode_one(decoder, bitbuf);
			if (val == COMPRESSION_RLE_SMALL)
				rawmap[0] = lastcomp, repcount = 2 + huffman_decode_one(decoder, bitbuf);
			else if (val == COMPRESSION_RLE_LARGE)
				rawmap[0] = lastcomp, repcount = 2 + 16 + (huffman_decode_one(decoder, bitbuf) << 4), repcount += huffman_decode_one(decoder, bitbuf);
			else
				rawmap[0] = lastcomp = val;
		}
	}

	/* then iterate through the hunks and extract the needed data */
	curoffset = firstoffs;
	for (hunknum = 0; hunknum < header->hunkcount; hunknum++)
	{
		uint8_t *rawmap = header->rawmap + (hunknum * 12);
		uint64_t offset = curoffset;
		uint32_t length = 0;
		uint16_t crc = 0;
		switch (rawmap[0])
		{
			/* base types */
			case COMPRESSION_TYPE_0:
			case COMPRESSION_TYPE_1:
			case COMPRESSION_TYPE_2:
			case COMPRESSION_TYPE_3:
				curoffset += length = bitstream_read(bitbuf, lengthbits);
				crc = bitstream_read(bitbuf, 16);
				break;

			case COMPRESSION_NONE:
				curoffset += length = header->hunkbytes;
				crc = bitstream_read(bitbuf, 16);
				break;

			case COMPRESSION_SELF:
				last_self = offset = bitstream_read(bitbuf, selfbits);
				break;

			case COMPRESSION_PARENT:
				offset = bitstream_read(bitbuf, parentbits);
				last_parent = offset;
				break;

			/* pseudo-types; convert into base types */
			case COMPRESSION_SELF_1:
				last_self++;
				/* Fallthrough */
			case COMPRESSION_SELF_0:
				rawmap[0] = COMPRESSION_SELF;
				offset = last_self;
				break;

			case COMPRESSION_PARENT_SELF:
				rawmap[0] = COMPRESSION_PARENT;
				last_parent = offset = ( ((uint64_t)hunknum) * ((uint64_t)header->hunkbytes) ) / header->unitbytes;
				break;

			case COMPRESSION_PARENT_1:
				last_parent += header->hunkbytes / header->unitbytes;
				/* Fallthrough */
			case COMPRESSION_PARENT_0:
				rawmap[0] = COMPRESSION_PARENT;
				offset = last_parent;
				break;
		}
		/* UINT24 length */
		put_bigendian_uint24(&rawmap[1], length);

		/* UINT48 offset */
		put_bigendian_uint48(&rawmap[4], offset);

		/* crc16 */
		put_bigendian_uint16(&rawmap[10], crc);
	}

	/* free memory */
	free(compressed_ptr);
	free(bitbuf);
	delete_huffman_decoder(decoder);

	/* verify the final CRC */
	if (crc16(&header->rawmap[0], header->hunkcount * 12) != mapcrc)
		return CHDERR_DECOMPRESSION_ERROR;

	return CHDERR_NONE;
}

/*-------------------------------------------------
    map_extract_old - extract a single map
    entry in old format from the datastream
-------------------------------------------------*/

static CHDR_INLINE void map_extract_old(const uint8_t *base, map_entry *entry, uint32_t hunkbytes)
{
	entry->offset = get_bigendian_uint64_t(&base[0]);
	entry->crc = 0;
	entry->length = entry->offset >> 44;
	entry->flags = MAP_ENTRY_FLAG_NO_CRC | ((entry->length == hunkbytes) ? V34_MAP_ENTRY_TYPE_UNCOMPRESSED : V34_MAP_ENTRY_TYPE_COMPRESSED);
#ifdef __MWERKS__
	entry->offset = entry->offset & 0x00000FFFFFFFFFFFLL;
#else
	entry->offset = (entry->offset << 20) >> 20;
#endif
}

/***************************************************************************
    CHD FILE MANAGEMENT
***************************************************************************/

static const core_file_callbacks core_stdio = {
	core_stdio_fsize,
	core_stdio_fread,
	core_stdio_fclose,
	core_stdio_fseek
};

static const core_file_callbacks core_stdio_nonowner = {
	core_stdio_fsize,
	core_stdio_fread,
	core_stdio_fclose_nonowner,
	core_stdio_fseek
};

static const core_file_callbacks core_legacy = {
	core_legacy_fsize,
	core_legacy_fread,
	core_legacy_fclose,
	core_legacy_fseek
};

/*-------------------------------------------------
    chd_open_file - open a CHD file for access
-------------------------------------------------*/

CHD_EXPORT chd_error chd_open_file(FILE *file, int mode, chd_file *parent, chd_file **chd) {
	return chd_open_core_file_callbacks(&core_stdio_nonowner, file, mode, parent, chd);
}

/*-------------------------------------------------
    chd_open_core_file - open a CHD file for access
-------------------------------------------------*/

CHD_EXPORT chd_error chd_open_core_file(core_file *file, int mode, chd_file *parent, chd_file **chd)
{
	if (file == NULL)
		return CHDERR_INVALID_PARAMETER;

	return chd_open_core_file_callbacks(&core_legacy, file, mode, parent, chd);
}

/*-------------------------------------------------
    chd_open_core_file_callbacks - open a CHD file for access
-------------------------------------------------*/

CHD_EXPORT chd_error chd_open_core_file_callbacks(const core_file_callbacks *callbacks, const void *user_data, int mode, chd_file *parent, chd_file **chd)
{
	chd_file *newchd = NULL;
	chd_error err;

	/* verify parameters */
	if (callbacks == NULL)
		EARLY_EXIT(err = CHDERR_INVALID_PARAMETER);

	/* punt if invalid parent */
	if (parent != NULL && parent->cookie != COOKIE_VALUE)
		EARLY_EXIT(err = CHDERR_INVALID_PARAMETER);

	/* allocate memory for the final result */
	newchd = (chd_file *)malloc(sizeof(**chd));
	if (newchd == NULL)
		EARLY_EXIT(err = CHDERR_OUT_OF_MEMORY);
	memset(newchd, 0, sizeof(*newchd));
	newchd->cookie = COOKIE_VALUE;
	newchd->parent = parent;
	newchd->file.callbacks = callbacks;
	newchd->file.argp = (void*)user_data;
	newchd->file_size = core_fsize(&newchd->file);
	if ((int64_t)newchd->file_size <= 0)
		EARLY_EXIT(err = CHDERR_INVALID_FILE);

	/* now attempt to read the header */
	err = header_read(newchd);
	if (err != CHDERR_NONE)
		EARLY_EXIT(err);

	/* make sure we don't open a read-only file writeable */
	if (mode == CHD_OPEN_READWRITE && !(newchd->header.flags & CHDFLAGS_IS_WRITEABLE))
		EARLY_EXIT(err = CHDERR_FILE_NOT_WRITEABLE);

	/* also, never open an older version writeable */
	if (mode == CHD_OPEN_READWRITE && newchd->header.version < CHD_HEADER_VERSION)
		EARLY_EXIT(err = CHDERR_UNSUPPORTED_VERSION);

	/* if we need a parent, make sure we have one */
	if (parent == NULL)
	{
		/* Detect parent requirement for versions below 5 */
		if (newchd->header.version < 5 && newchd->header.flags & CHDFLAGS_HAS_PARENT)
			EARLY_EXIT(err = CHDERR_REQUIRES_PARENT);
		/* Detection for version 5 and above - if parentsha1 != 0, we have a parent */
		else if (newchd->header.version >= 5 && memcmp(nullsha1, newchd->header.parentsha1, sizeof(newchd->header.parentsha1)) != 0)
			EARLY_EXIT(err = CHDERR_REQUIRES_PARENT);
	}

	/* make sure we have a valid parent */
	if (parent != NULL)
	{
		/* check MD5 if it isn't empty */
		if (memcmp(nullmd5, newchd->header.parentmd5, sizeof(newchd->header.parentmd5)) != 0 &&
			memcmp(nullmd5, newchd->parent->header.md5, sizeof(newchd->parent->header.md5)) != 0 &&
			memcmp(newchd->parent->header.md5, newchd->header.parentmd5, sizeof(newchd->header.parentmd5)) != 0)
			EARLY_EXIT(err = CHDERR_INVALID_PARENT);

		/* check SHA1 if it isn't empty */
		if (memcmp(nullsha1, newchd->header.parentsha1, sizeof(newchd->header.parentsha1)) != 0 &&
			memcmp(nullsha1, newchd->parent->header.sha1, sizeof(newchd->parent->header.sha1)) != 0 &&
			memcmp(newchd->parent->header.sha1, newchd->header.parentsha1, sizeof(newchd->header.parentsha1)) != 0)
			EARLY_EXIT(err = CHDERR_INVALID_PARENT);
	}

	/* now read the hunk map */
	if (newchd->header.version < 5)
	{
		err = map_read(newchd);
		if (err != CHDERR_NONE)
			EARLY_EXIT(err);
	}
	else
	{
		err = decompress_v5_map(newchd, &(newchd->header));
	}
	if (err != CHDERR_NONE)
		EARLY_EXIT(err);

	/* allocate the temporary compressed buffer */
	newchd->compressed = (uint8_t *)malloc(newchd->header.hunkbytes);
	if (newchd->compressed == NULL)
		EARLY_EXIT(err = CHDERR_OUT_OF_MEMORY);

	/* find the codec interface */
	if (newchd->header.version < 5)
	{
		size_t intfnum;
		for (intfnum = 0; intfnum < ARRAY_LENGTH(codec_interfaces); intfnum++)
		{
			if (codec_interfaces[intfnum].compression == newchd->header.compression[0])
			{
				newchd->codecintf[0] = &codec_interfaces[intfnum];
				break;
			}
		}

		if (intfnum == ARRAY_LENGTH(codec_interfaces))
			EARLY_EXIT(err = CHDERR_UNSUPPORTED_FORMAT);

		/* initialize the codec */
		if (newchd->codecintf[0]->init != NULL)
		{
			err = newchd->codecintf[0]->init(&newchd->codec_data.zlib, newchd->header.hunkbytes);
			if (err != CHDERR_NONE)
				EARLY_EXIT(err);
		}
	}
	else
	{
		size_t decompnum;
		int needsinit;

		/* verify the compression types and initialize the codecs */
		for (decompnum = 0; decompnum < ARRAY_LENGTH(newchd->header.compression); decompnum++)
		{
			size_t i;
			for (i = 0 ; i < ARRAY_LENGTH(codec_interfaces) ; i++)
			{
				if (codec_interfaces[i].compression == newchd->header.compression[decompnum])
				{
					newchd->codecintf[decompnum] = &codec_interfaces[i];
					break;
				}
			}

			if (newchd->codecintf[decompnum] == NULL && newchd->header.compression[decompnum] != 0)
				EARLY_EXIT(err = CHDERR_UNSUPPORTED_FORMAT);

			/* ensure we don't try to initialize the same codec twice */
			/* this is "normal" for chds where the user overrides the codecs, it'll have none repeated */
			needsinit = (newchd->codecintf[decompnum]->init != NULL);
			for (i = 0; i < decompnum; i++)
			{
				if (newchd->codecintf[decompnum] == newchd->codecintf[i])
				{
					/* already initialized */
					needsinit = FALSE;
					break;
				}
      }

			/* initialize the codec */
			if (needsinit)
			{
				void* codec = NULL;
				switch (newchd->header.compression[decompnum])
				{
					case CHD_CODEC_ZLIB:
						codec = &newchd->codec_data.zlib;
						break;

					case CHD_CODEC_LZMA:
						codec = &newchd->codec_data.lzma;
						break;

					case CHD_CODEC_HUFFMAN:
						codec = &newchd->codec_data.huff;
						break;

					case CHD_CODEC_FLAC:
						codec = &newchd->codec_data.flac;
						break;

					case CHD_CODEC_ZSTD:
						codec = &newchd->codec_data.zstd;
						break;

					case CHD_CODEC_CD_ZLIB:
						codec = &newchd->codec_data.cdzl;
						break;

					case CHD_CODEC_CD_LZMA:
						codec = &newchd->codec_data.cdlz;
						break;

					case CHD_CODEC_CD_FLAC:
						codec = &newchd->codec_data.cdfl;
						break;

					case CHD_CODEC_CD_ZSTD:
						codec = &newchd->codec_data.cdzs;
						break;
				}

				if (codec == NULL)
					EARLY_EXIT(err = CHDERR_UNSUPPORTED_FORMAT);

				err = newchd->codecintf[decompnum]->init(codec, newchd->header.hunkbytes);
				if (err != CHDERR_NONE)
					EARLY_EXIT(err);
			}
		}
	}

	/* all done */
	*chd = newchd;
	return CHDERR_NONE;

cleanup:
	if (newchd != NULL)
		chd_close(newchd);
	return err;
}

/*-------------------------------------------------
    chd_precache - precache underlying file in
    memory
-------------------------------------------------*/

CHD_EXPORT chd_error chd_precache(chd_file *chd)
{
	if (chd->file_cache == NULL)
	{
		chd->file_cache = (uint8_t*)malloc(chd->file_size);
		if (chd->file_cache == NULL)
			return CHDERR_OUT_OF_MEMORY;
		if (!seek_and_read(chd, 0, chd->file_cache, chd->file_size))
		{
			free(chd->file_cache);
			chd->file_cache = NULL;
			return CHDERR_READ_ERROR;
		}
	}

	return CHDERR_NONE;
}

/*-------------------------------------------------
    chd_open - open a CHD file by
    filename
-------------------------------------------------*/

CHD_EXPORT chd_error chd_open(const char *filename, int mode, chd_file *parent, chd_file **chd)
{
	chd_error err;
	void *file = NULL;

	if (filename == NULL)
		EARLY_EXIT(err = CHDERR_INVALID_PARAMETER);

	/* choose the proper mode */
	switch(mode)
	{
		case CHD_OPEN_READ:
			break;

		default:
			EARLY_EXIT(err = CHDERR_INVALID_PARAMETER);
	}

	/* open the file */
	file = core_stdio_fopen(filename);
	if (file == NULL)
		EARLY_EXIT(err = CHDERR_FILE_NOT_FOUND);

	/* now open the CHD */
	return chd_open_core_file_callbacks(&core_stdio, file, mode, parent, chd);

cleanup:
	if ((err != CHDERR_NONE) && (file != NULL))
		core_stdio_fclose(file);
	return err;
}

/*-------------------------------------------------
    chd_close - close a CHD file for access
-------------------------------------------------*/

CHD_EXPORT void chd_close(chd_file *chd)
{
	/* punt if NULL or invalid */
	if (chd == NULL || chd->cookie != COOKIE_VALUE)
		return;

	/* deinit the codec */
	if (chd->header.version < 5)
	{
		if (chd->codecintf[0] != NULL && chd->codecintf[0]->free != NULL)
			chd->codecintf[0]->free(&chd->codec_data.zlib);
	}
	else
	{
		size_t i;
		/* Free the codecs */
		for (i = 0 ; i < ARRAY_LENGTH(chd->codecintf); i++)
		{
			void* codec = NULL;
			size_t j;
			int needsfree;

			if (chd->codecintf[i] == NULL)
				continue;

			/* only free each codec at max once */
			needsfree = 1;
			for (j = 0; j < i; j++)
			{
				if (chd->codecintf[i] == chd->codecintf[j])
				{
					needsfree = FALSE;
					break;
				}
			}
			if (!needsfree)
				continue;

			switch (chd->codecintf[i]->compression)
			{
				case CHD_CODEC_ZLIB:
					codec = &chd->codec_data.zlib;
					break;

				case CHD_CODEC_LZMA:
					codec = &chd->codec_data.lzma;
					break;

				case CHD_CODEC_HUFFMAN:
					codec = &chd->codec_data.huff;
					break;

				case CHD_CODEC_FLAC:
					codec = &chd->codec_data.flac;
					break;

				case CHD_CODEC_ZSTD:
					codec = &chd->codec_data.zstd;
					break;

				case CHD_CODEC_CD_ZLIB:
					codec = &chd->codec_data.cdzl;
					break;

				case CHD_CODEC_CD_LZMA:
					codec = &chd->codec_data.cdlz;
					break;

				case CHD_CODEC_CD_FLAC:
					codec = &chd->codec_data.cdfl;
					break;

				case CHD_CODEC_CD_ZSTD:
					codec = &chd->codec_data.cdzs;
					break;
			}

			if (codec)
			{
				chd->codecintf[i]->free(codec);
			}
		}

		/* Free the raw map */
		if (chd->header.rawmap != NULL)
			free(chd->header.rawmap);
	}

	/* free the compressed data buffer */
	if (chd->compressed != NULL)
		free(chd->compressed);

	/* free the hunk map */
	if (chd->map != NULL)
		free(chd->map);

	/* close the file */
	if (chd->file.callbacks != NULL)
		core_fclose(&chd->file);

	if (chd->file_cache)
		free(chd->file_cache);

	if (chd->parent)
		chd_close(chd->parent);

	/* free our memory */
	free(chd);
}

/*-------------------------------------------------
    chd_core_file - return the associated
    core_file
-------------------------------------------------*/

CHD_EXPORT core_file *chd_core_file(chd_file *chd)
{
	if (chd->file.callbacks != &core_legacy)
		return NULL;

	return (core_file*)chd->file.argp;
}

/*-------------------------------------------------
    chd_error_string - return an error string for
    the given CHD error
-------------------------------------------------*/

CHD_EXPORT const char *chd_error_string(chd_error err)
{
	switch (err)
	{
		case CHDERR_NONE:						return "no error";
		case CHDERR_NO_INTERFACE:				return "no drive interface";
		case CHDERR_OUT_OF_MEMORY:				return "out of memory";
		case CHDERR_INVALID_FILE:				return "invalid file";
		case CHDERR_INVALID_PARAMETER:			return "invalid parameter";
		case CHDERR_INVALID_DATA:				return "invalid data";
		case CHDERR_FILE_NOT_FOUND:				return "file not found";
		case CHDERR_REQUIRES_PARENT:			return "requires parent";
		case CHDERR_FILE_NOT_WRITEABLE:			return "file not writeable";
		case CHDERR_READ_ERROR:					return "read error";
		case CHDERR_WRITE_ERROR:				return "write error";
		case CHDERR_CODEC_ERROR:				return "codec error";
		case CHDERR_INVALID_PARENT:				return "invalid parent";
		case CHDERR_HUNK_OUT_OF_RANGE:			return "hunk out of range";
		case CHDERR_DECOMPRESSION_ERROR:		return "decompression error";
		case CHDERR_COMPRESSION_ERROR:			return "compression error";
		case CHDERR_CANT_CREATE_FILE:			return "can't create file";
		case CHDERR_CANT_VERIFY:				return "can't verify file";
		case CHDERR_NOT_SUPPORTED:				return "operation not supported";
		case CHDERR_METADATA_NOT_FOUND:			return "can't find metadata";
		case CHDERR_INVALID_METADATA_SIZE:		return "invalid metadata size";
		case CHDERR_UNSUPPORTED_VERSION:		return "unsupported CHD version";
		case CHDERR_VERIFY_INCOMPLETE:			return "incomplete verify";
		case CHDERR_INVALID_METADATA:			return "invalid metadata";
		case CHDERR_INVALID_STATE:				return "invalid state";
		case CHDERR_OPERATION_PENDING:			return "operation pending";
		case CHDERR_NO_ASYNC_OPERATION:			return "no async operation in progress";
		case CHDERR_UNSUPPORTED_FORMAT:			return "unsupported format";
		default:								return "undocumented error";
	}
}

/***************************************************************************
    CHD HEADER MANAGEMENT
***************************************************************************/

/*-------------------------------------------------
    chd_get_header - return a pointer to the
    extracted header data
-------------------------------------------------*/

CHD_EXPORT const chd_header *chd_get_header(chd_file *chd)
{
	/* punt if NULL or invalid */
	if (chd == NULL || chd->cookie != COOKIE_VALUE)
		return NULL;

	return &chd->header;
}

/*-------------------------------------------------
    chd_read_header_core_file_callbacks - read CHD header data
	from file into the pointed struct
-------------------------------------------------*/

CHD_EXPORT chd_error chd_read_header_core_file_callbacks(const core_file_callbacks *callbacks, const void *user_data, chd_header *header)
{
	chd_error err;
	chd_file chd;

	/* verify parameters */
	if (callbacks == NULL || header == NULL)
		return CHDERR_INVALID_PARAMETER;

	chd.cookie = COOKIE_VALUE;
	chd.file.callbacks = callbacks;
	chd.file.argp = (void*)user_data;
	chd.file_size = core_fsize(&chd.file);
	if ((int64_t)chd.file_size <= 0)
		return CHDERR_INVALID_FILE;

	/* attempt to read the header */
	err = header_read(&chd);
	if (err != CHDERR_NONE)
		return err;
	*header = chd.header;
	return CHDERR_NONE;
}

/*-------------------------------------------------
    chd_read_header_core_file - read CHD header data
	from file into the pointed struct
-------------------------------------------------*/

CHD_EXPORT chd_error chd_read_header_core_file(core_file *file, chd_header *header)
{
	if (file == NULL)
		return CHDERR_INVALID_PARAMETER;

	return chd_read_header_core_file_callbacks(&core_legacy, file, header);
}

/*-------------------------------------------------
    chd_read_header - read CHD header data
	from file into the pointed struct
-------------------------------------------------*/

CHD_EXPORT chd_error chd_read_header_file(FILE *file, chd_header *header)
{
	return chd_read_header_core_file_callbacks(&core_stdio_nonowner, file, header);
}

/*-------------------------------------------------
    chd_read_header - read CHD header data
	from file into the pointed struct
-------------------------------------------------*/

CHD_EXPORT chd_error chd_read_header(const char *filename, chd_header *header)
{
	chd_error err;
	void *file = NULL;

	if (filename == NULL)
		EARLY_EXIT(err = CHDERR_INVALID_PARAMETER);

	/* open the file */
	file = core_stdio_fopen(filename);
	if (file == NULL)
		EARLY_EXIT(err = CHDERR_FILE_NOT_FOUND);

	err = chd_read_header_core_file_callbacks(&core_stdio, file, header);

	cleanup:
	if (file != NULL)
		core_stdio_fclose(file);
	return err;
}

/***************************************************************************
    CORE DATA READ/WRITE
***************************************************************************/

/*-------------------------------------------------
    chd_read - read a single hunk from the CHD
    file
-------------------------------------------------*/

CHD_EXPORT chd_error chd_read(chd_file *chd, uint32_t hunknum, void *buffer)
{
	/* punt if NULL or invalid */
	if (chd == NULL || chd->cookie != COOKIE_VALUE)
		return CHDERR_INVALID_PARAMETER;

	/* if we're past the end, fail */
	if (hunknum >= chd->header.totalhunks)
		return CHDERR_HUNK_OUT_OF_RANGE;

	/* perform the read */
	return hunk_read_into_memory(chd, hunknum, (uint8_t *)buffer);
}

/***************************************************************************
    METADATA MANAGEMENT
***************************************************************************/

/*-------------------------------------------------
    chd_get_metadata - get the indexed metadata
    of the given type
-------------------------------------------------*/

CHD_EXPORT chd_error chd_get_metadata(chd_file *chd, uint32_t searchtag, uint32_t searchindex, void *output, uint32_t outputlen, uint32_t *resultlen, uint32_t *resulttag, uint8_t *resultflags)
{
	metadata_entry metaentry;
	chd_error err;

	/* if we didn't find it, just return */
	err = metadata_find_entry(chd, searchtag, searchindex, &metaentry);
	if (err != CHDERR_NONE)
	{
		/* unless we're an old version and they are requesting hard disk metadata */
		if (chd->header.version < 3 && (searchtag == HARD_DISK_METADATA_TAG || searchtag == CHDMETATAG_WILDCARD) && searchindex == 0)
		{
			char faux_metadata[256];
			uint32_t faux_length;

			/* fill in the faux metadata */
			snprintf(faux_metadata, sizeof(faux_metadata), HARD_DISK_METADATA_FORMAT, chd->header.obsolete_cylinders, chd->header.obsolete_heads, chd->header.obsolete_sectors, (chd->header.obsolete_hunksize != 0) ? (chd->header.hunkbytes / chd->header.obsolete_hunksize) : 0);
			faux_length = (uint32_t)strlen(faux_metadata) + 1;

			/* copy the metadata itself */
			memcpy(output, faux_metadata, MIN(outputlen, faux_length));

			/* return the length of the data and the tag */
			if (resultlen != NULL)
				*resultlen = faux_length;
			if (resulttag != NULL)
				*resulttag = HARD_DISK_METADATA_TAG;
			return CHDERR_NONE;
		}
		return err;
	}

	/* read the metadata */
	outputlen = MIN(outputlen, metaentry.length);
	if (!seek_and_read(chd, metaentry.offset + METADATA_HEADER_SIZE, output, outputlen))
		return CHDERR_READ_ERROR;

	/* return the length of the data and the tag */
	if (resultlen != NULL)
		*resultlen = metaentry.length;
	if (resulttag != NULL)
		*resulttag = metaentry.metatag;
	if (resultflags != NULL)
		*resultflags = metaentry.flags;
	return CHDERR_NONE;
}

/***************************************************************************
    INTERNAL HEADER OPERATIONS
***************************************************************************/

/*-------------------------------------------------
    header_guess_unitbytes - for older CHD formats,
    guess at the bytes/unit based on metadata
-------------------------------------------------*/

static uint32_t header_guess_unitbytes(chd_file *chd)
{
	/* look for hard disk metadata; if found, then the unit size == sector size */
	char metadata[512];
	int i0, i1, i2, i3;
	if (chd_get_metadata(chd, HARD_DISK_METADATA_TAG, 0, metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE &&
		sscanf(metadata, HARD_DISK_METADATA_FORMAT, &i0, &i1, &i2, &i3) == 4)
		return i3;

	/* look for CD-ROM metadata; if found, then the unit size == CD frame size */
	if (chd_get_metadata(chd, CDROM_OLD_METADATA_TAG, 0, metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE ||
		chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, 0, metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE ||
		chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, 0, metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE ||
		chd_get_metadata(chd, GDROM_OLD_METADATA_TAG, 0, metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE ||
		chd_get_metadata(chd, GDROM_TRACK_METADATA_TAG, 0, metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE)
		return CD_FRAME_SIZE;

	/* otherwise, just map 1:1 with the hunk size */
	return chd->header.hunkbytes;
}

/*-------------------------------------------------
    header_read - read a CHD header into the
    internal data structure and perform validation
-------------------------------------------------*/

static chd_error header_read(chd_file *chd)
{
	static const uint32_t header_sizes[CHD_HEADER_VERSION] = {
		CHD_V1_HEADER_SIZE,
		CHD_V2_HEADER_SIZE,
		CHD_V3_HEADER_SIZE,
		CHD_V4_HEADER_SIZE,
		CHD_V5_HEADER_SIZE,
	};

	uint8_t rawheader[CHD_MAX_HEADER_SIZE];

	/* punt if NULL */
	if (chd == NULL)
		return CHDERR_INVALID_PARAMETER;

	/* punt if invalid file */
	if (chd->file.callbacks == NULL)
		return CHDERR_INVALID_FILE;

	/* read the start of the header */
	if (!seek_and_read(chd, 0, rawheader, 8 + 4 + 4))
		return CHDERR_READ_ERROR;

	/* verify the tag */
	if (memcmp(rawheader, "MComprHD", 8) != 0)
		return CHDERR_INVALID_DATA;

	/* extract the direct data */
	chd_header *header = &chd->header;
	memset(header, 0, sizeof(*header));
	header->length  = get_bigendian_uint32_t(&rawheader[8]);
	header->version = get_bigendian_uint32_t(&rawheader[12]);

	/* Unknown version */
	if (header->version == 0 || header->version > ARRAY_LENGTH(header_sizes))
		return CHDERR_UNSUPPORTED_VERSION;

	/* make sure the length is expected */
	if (header->length != header_sizes[header->version - 1])
		return CHDERR_INVALID_DATA;

	/* read the full header, now that we know its size */
	if (!seek_and_read(chd, 0, rawheader, header->length))
		return CHDERR_READ_ERROR;

	switch (header->version)
	{
		default:
			/* Unknown version */
			return CHDERR_UNSUPPORTED_VERSION;

		case 1:
		case 2:
			header->flags              = get_bigendian_uint32_t(&rawheader[16]);
			header->compression[0]     = get_bigendian_uint32_t(&rawheader[20]);
			header->obsolete_hunksize  = get_bigendian_uint32_t(&rawheader[24]);
			header->totalhunks         = get_bigendian_uint32_t(&rawheader[28]);
			header->obsolete_cylinders = get_bigendian_uint32_t(&rawheader[32]);
			header->obsolete_heads     = get_bigendian_uint32_t(&rawheader[36]);
			header->obsolete_sectors   = get_bigendian_uint32_t(&rawheader[40]);
			memcpy(header->md5, &rawheader[44], CHD_MD5_BYTES);
			memcpy(header->parentmd5, &rawheader[60], CHD_MD5_BYTES);
			{
				uint32_t seclen = (header->version == 1) ? CHD_V1_SECTOR_SIZE : get_bigendian_uint32_t(&rawheader[76]);
				uint64_t hunkbytes64 = (uint64_t)seclen * (uint64_t)header->obsolete_hunksize;
				header->logicalbytes = (uint64_t)header->obsolete_cylinders * (uint64_t)header->obsolete_heads * (uint64_t)header->obsolete_sectors * (uint64_t)seclen;
				/* reject malformed headers where hunkbytes would overflow uint32_t or is zero */
				if (hunkbytes64 == 0 || hunkbytes64 > UINT32_MAX)
					return CHDERR_INVALID_DATA;
				header->hunkbytes = (uint32_t)hunkbytes64;
			}
			header->unitbytes          = header_guess_unitbytes(chd);
			if (header->unitbytes == 0)
				return CHDERR_INVALID_DATA;
			header->unitcount          = (header->logicalbytes + header->unitbytes - 1) / header->unitbytes;
			header->metaoffset = 0;

			break;

		case 3:
			header->flags          = get_bigendian_uint32_t(&rawheader[16]);
			header->compression[0] = get_bigendian_uint32_t(&rawheader[20]);
			header->totalhunks     = get_bigendian_uint32_t(&rawheader[24]);
			header->logicalbytes   = get_bigendian_uint64_t(&rawheader[28]);
			header->metaoffset     = get_bigendian_uint64_t(&rawheader[36]);
			memcpy(header->md5, &rawheader[44], CHD_MD5_BYTES);
			memcpy(header->parentmd5, &rawheader[60], CHD_MD5_BYTES);
			header->hunkbytes      = get_bigendian_uint32_t(&rawheader[76]);
			header->unitbytes      = header_guess_unitbytes(chd);
			if (header->unitbytes == 0)
				return CHDERR_INVALID_DATA;
			header->unitcount      = (header->logicalbytes + header->unitbytes - 1) / header->unitbytes;
			memcpy(header->sha1, &rawheader[80], CHD_SHA1_BYTES);
			memcpy(header->parentsha1, &rawheader[100], CHD_SHA1_BYTES);

			break;

		case 4:
			header->flags          = get_bigendian_uint32_t(&rawheader[16]);
			header->compression[0] = get_bigendian_uint32_t(&rawheader[20]);
			header->totalhunks     = get_bigendian_uint32_t(&rawheader[24]);
			header->logicalbytes   = get_bigendian_uint64_t(&rawheader[28]);
			header->metaoffset     = get_bigendian_uint64_t(&rawheader[36]);
			header->hunkbytes      = get_bigendian_uint32_t(&rawheader[44]);
			header->unitbytes      = header_guess_unitbytes(chd);
			if (header->unitbytes == 0)
				return CHDERR_INVALID_DATA;
			header->unitcount      = (header->logicalbytes + header->unitbytes - 1) / header->unitbytes;
			memcpy(header->sha1, &rawheader[48], CHD_SHA1_BYTES);
			memcpy(header->parentsha1, &rawheader[68], CHD_SHA1_BYTES);
			memcpy(header->rawsha1, &rawheader[88], CHD_SHA1_BYTES);

			break;

		case 5:
			header->compression[0] = get_bigendian_uint32_t(&rawheader[16]);
			header->compression[1] = get_bigendian_uint32_t(&rawheader[20]);
			header->compression[2] = get_bigendian_uint32_t(&rawheader[24]);
			header->compression[3] = get_bigendian_uint32_t(&rawheader[28]);
			header->logicalbytes   = get_bigendian_uint64_t(&rawheader[32]);
			header->mapoffset      = get_bigendian_uint64_t(&rawheader[40]);
			header->metaoffset     = get_bigendian_uint64_t(&rawheader[48]);
			header->hunkbytes      = get_bigendian_uint32_t(&rawheader[56]);
			if (header->hunkbytes == 0)
				return CHDERR_INVALID_DATA;
			header->hunkcount      = (header->logicalbytes + header->hunkbytes - 1) / header->hunkbytes;
			header->unitbytes      = get_bigendian_uint32_t(&rawheader[60]);
			if (header->unitbytes == 0)
				return CHDERR_INVALID_DATA;
			header->unitcount      = (header->logicalbytes + header->unitbytes - 1) / header->unitbytes;
			memcpy(header->sha1, &rawheader[84], CHD_SHA1_BYTES);
			memcpy(header->parentsha1, &rawheader[104], CHD_SHA1_BYTES);
			memcpy(header->rawsha1, &rawheader[64], CHD_SHA1_BYTES);

			/* determine properties of map entries */
			header->mapentrybytes  = chd_compressed(header) ? 12 : 4;

			/* hack */
			header->totalhunks     = header->hunkcount;

			break;
	}

	/* Do not validate v5 header */
	if (header->version <= 4)
	{
		size_t intfnum;

		/* require valid flags */
		if (header->flags & CHDFLAGS_UNDEFINED)
			return CHDERR_INVALID_DATA;

		/* require a supported compression mechanism */
		for (intfnum = 0; intfnum < ARRAY_LENGTH(codec_interfaces); intfnum++)
			if (codec_interfaces[intfnum].compression == header->compression[0])
				break;

		if (intfnum == ARRAY_LENGTH(codec_interfaces))
			return CHDERR_INVALID_DATA;

		/* require a valid hunksize */
		if (header->hunkbytes == 0 || header->hunkbytes >= 65536 * 256)
			return CHDERR_INVALID_DATA;

		/* require a valid hunk count */
		if (header->totalhunks == 0)
			return CHDERR_INVALID_DATA;

		/* require a valid MD5 and/or SHA1 if we're using a parent */
		if ((header->flags & CHDFLAGS_HAS_PARENT) && memcmp(header->parentmd5, nullmd5, sizeof(nullmd5)) == 0 && memcmp(header->parentsha1, nullsha1, sizeof(nullsha1)) == 0)
			return CHDERR_INVALID_DATA;

		/* if we're V3 or later, the obsolete fields must be 0 */
		if (header->version >= 3 &&
			(header->obsolete_cylinders != 0 || header->obsolete_sectors != 0 ||
			 header->obsolete_heads != 0 || header->obsolete_hunksize != 0))
			return CHDERR_INVALID_DATA;

		/* if we're pre-V3, the obsolete fields must NOT be 0 */
		if (header->version < 3 &&
			(header->obsolete_cylinders == 0 || header->obsolete_sectors == 0 ||
			 header->obsolete_heads == 0 || header->obsolete_hunksize == 0))
			return CHDERR_INVALID_DATA;
	}

	/* some basic size checks to prevent huge mallocs */
	if (header->hunkbytes >= CHD_MAX_HUNK_SIZE || ((uint64_t)header->hunkbytes * (uint64_t)header->totalhunks) >= CHD_MAX_FILE_SIZE)
		return CHDERR_INVALID_DATA;

	/* totalhunks is used to size the map allocation; a malformed header
	 * can otherwise request multi-GB allocations for map[] even when the
	 * file itself is tiny. Every hunk map entry consumes at least one bit
	 * in the compressed on-disk map, so totalhunks cannot legitimately
	 * exceed file_size * 8. */
	if ((uint64_t)header->totalhunks > chd->file_size * 8)
		return CHDERR_INVALID_DATA;

	/* guess it worked */
	return CHDERR_NONE;
}

/***************************************************************************
    INTERNAL HUNK READ/WRITE
***************************************************************************/

/*-------------------------------------------------
    hunk_read_compressed - read a compressed
    hunk
-------------------------------------------------*/

static uint8_t* hunk_read_compressed(chd_file *chd, uint64_t offset, size_t size)
{
	if (chd->file_cache != NULL)
	{
		if ((offset + size) > chd->file_size || (offset + size) < offset)
			return NULL;
		else
			return chd->file_cache + offset;
	}
	else
	{
		/* make sure it isn't larger than the compressed buffer */
		if (size > chd->header.hunkbytes)
			return NULL;

		if (!seek_and_read(chd, offset, chd->compressed, size))
			return NULL;
		return chd->compressed;
	}
}

/*-------------------------------------------------
    hunk_read_uncompressed - read an uncompressed
    hunk
-------------------------------------------------*/

static chd_error hunk_read_uncompressed(chd_file *chd, uint64_t offset, size_t size, uint8_t *dest)
{
	if (chd->file_cache != NULL)
	{
		if ((offset + size) > chd->file_size || (offset + size) < offset)
			return CHDERR_READ_ERROR;

		memcpy(dest, chd->file_cache + offset, size);
	}
	else
	{
		if (!seek_and_read(chd, offset, dest, size))
			return CHDERR_READ_ERROR;
	}
	return CHDERR_NONE;
}

/*-------------------------------------------------
    hunk_read_into_memory - read a hunk into
    memory at the given location
-------------------------------------------------*/

static chd_error hunk_read_into_memory(chd_file *chd, uint32_t hunknum, uint8_t *dest)
{
	chd_error err;

	/* punt if no file */
	if (chd->file.callbacks == NULL)
		return CHDERR_INVALID_FILE;

	/* return an error if out of range */
	if (hunknum >= chd->header.totalhunks)
		return CHDERR_HUNK_OUT_OF_RANGE;

	if (dest == NULL)
		return CHDERR_INVALID_PARAMETER;

	if (chd->header.version < 5)
	{
		map_entry *entry = &chd->map[hunknum];
		uint32_t bytes;
		uint8_t* compressed_bytes;

		/* switch off the entry type */
		switch (entry->flags & MAP_ENTRY_FLAG_TYPE_MASK)
		{
			/* compressed data */
			case V34_MAP_ENTRY_TYPE_COMPRESSED:
			{
				void *codec = NULL;

				/* read it into the decompression buffer */
				compressed_bytes = hunk_read_compressed(chd, entry->offset, entry->length);
				if (compressed_bytes == NULL)
					return CHDERR_READ_ERROR;

				/* now decompress using the codec */
				err = CHDERR_NONE;
				codec = &chd->codec_data.zlib;
				if (chd->codecintf[0]->decompress != NULL)
					err = chd->codecintf[0]->decompress(codec, compressed_bytes, entry->length, dest, chd->header.hunkbytes);
				if (err != CHDERR_NONE)
					return err;
				break;
			}

			/* uncompressed data */
			case V34_MAP_ENTRY_TYPE_UNCOMPRESSED:
				err = hunk_read_uncompressed(chd, entry->offset, chd->header.hunkbytes, dest);
				if (err != CHDERR_NONE)
					return err;
				break;

			/* mini-compressed data */
			case V34_MAP_ENTRY_TYPE_MINI:
				put_bigendian_uint64_t(&dest[0], entry->offset);
				for (bytes = 8; bytes < chd->header.hunkbytes; bytes++)
					dest[bytes] = dest[bytes - 8];
				break;

			/* self-referenced data */
			case V34_MAP_ENTRY_TYPE_SELF_HUNK:
				return hunk_read_into_memory(chd, entry->offset, dest);

			/* parent-referenced data */
			case V34_MAP_ENTRY_TYPE_PARENT_HUNK:
				err = hunk_read_into_memory(chd->parent, entry->offset, dest);
				if (err != CHDERR_NONE)
					return err;
				break;
		}
		return CHDERR_NONE;
	}
	else
	{
		void* codec = NULL;
		/* get a pointer to the map entry */
		uint64_t blockoffs;
		uint32_t blocklen;
#if VERIFY_BLOCK_CRC
		uint16_t blockcrc;
#endif
		uint8_t *rawmap = &chd->header.rawmap[chd->header.mapentrybytes * hunknum];
		uint8_t* compressed_bytes;

		/* uncompressed case */
		if (!chd_compressed(&chd->header))
		{
			blockoffs = (uint64_t)get_bigendian_uint32_t(rawmap) * (uint64_t)chd->header.hunkbytes;
			if (blockoffs != 0) {
				if (!seek_and_read(chd, blockoffs, dest, chd->header.hunkbytes))
					return CHDERR_READ_ERROR;
			/* TODO
			else if (m_parent_missing)
				throw CHDERR_REQUIRES_PARENT; */
			} else if (chd->parent) {
				err = hunk_read_into_memory(chd->parent, hunknum, dest);
				if (err != CHDERR_NONE)
					return err;
			} else {
				memset(dest, 0, chd->header.hunkbytes);
			}

			return CHDERR_NONE;
		}

		/* compressed case */
		blocklen = get_bigendian_uint24(&rawmap[1]);
		blockoffs = get_bigendian_uint48(&rawmap[4]);
#if VERIFY_BLOCK_CRC
		blockcrc = get_bigendian_uint16(&rawmap[10]);
#endif
		codec = NULL;
		switch (rawmap[0])
		{
			case COMPRESSION_TYPE_0:
			case COMPRESSION_TYPE_1:
			case COMPRESSION_TYPE_2:
			case COMPRESSION_TYPE_3:
				compressed_bytes = hunk_read_compressed(chd, blockoffs, blocklen);
				if (compressed_bytes == NULL)
					return CHDERR_READ_ERROR;
				switch (chd->codecintf[rawmap[0]]->compression)
				{
					case CHD_CODEC_ZLIB:
						codec = &chd->codec_data.zlib;
						break;

					case CHD_CODEC_LZMA:
						codec = &chd->codec_data.lzma;
						break;

					case CHD_CODEC_HUFFMAN:
						codec = &chd->codec_data.huff;
						break;

					case CHD_CODEC_FLAC:
						codec = &chd->codec_data.flac;
						break;

					case CHD_CODEC_ZSTD:
						codec = &chd->codec_data.zstd;
						break;

					case CHD_CODEC_CD_ZLIB:
						codec = &chd->codec_data.cdzl;
						break;

					case CHD_CODEC_CD_LZMA:
						codec = &chd->codec_data.cdlz;
						break;

					case CHD_CODEC_CD_FLAC:
						codec = &chd->codec_data.cdfl;
						break;

					case CHD_CODEC_CD_ZSTD:
						codec = &chd->codec_data.cdzs;
						break;
				}
				if (codec==NULL)
					return CHDERR_CODEC_ERROR;
				err = chd->codecintf[rawmap[0]]->decompress(codec, compressed_bytes, blocklen, dest, chd->header.hunkbytes);
				if (err != CHDERR_NONE)
					return err;
#if VERIFY_BLOCK_CRC
				if (crc16(dest, chd->header.hunkbytes) != blockcrc)
					return CHDERR_DECOMPRESSION_ERROR;
#endif
				return CHDERR_NONE;

			case COMPRESSION_NONE:
				err = hunk_read_uncompressed(chd, blockoffs, blocklen, dest);
				if (err != CHDERR_NONE)
					return err;
#if VERIFY_BLOCK_CRC
				if (crc16(dest, chd->header.hunkbytes) != blockcrc)
					return CHDERR_DECOMPRESSION_ERROR;
#endif
				return CHDERR_NONE;

			case COMPRESSION_SELF:
				return hunk_read_into_memory(chd, blockoffs, dest);

			case COMPRESSION_PARENT:
			{
				uint8_t units_in_hunk;

				if (chd->parent == NULL)
					return CHDERR_REQUIRES_PARENT;
				units_in_hunk = chd->header.hunkbytes / chd->header.unitbytes;

				/* blockoffs is aligned to units_in_hunk */
				if (blockoffs % units_in_hunk == 0) {
					return hunk_read_into_memory(chd->parent, blockoffs / units_in_hunk, dest);
				/* blockoffs is not aligned to units_in_hunk */
				} else {
					uint32_t unit_in_hunk = blockoffs % units_in_hunk;
					uint8_t *buf = (uint8_t*)malloc(chd->header.hunkbytes);
					/* Read first half of hunk which contains blockoffs */
					err = hunk_read_into_memory(chd->parent, blockoffs / units_in_hunk, buf);
					if (err != CHDERR_NONE) {
						free(buf);
						return err;
					}
					memcpy(dest, buf + unit_in_hunk * chd->header.unitbytes, (units_in_hunk - unit_in_hunk) * chd->header.unitbytes);
					/* Read second half of hunk which contains blockoffs */
					err = hunk_read_into_memory(chd->parent, (blockoffs / units_in_hunk) + 1, buf);
					if (err != CHDERR_NONE) {
						free(buf);
						return err;
					}
					memcpy(dest + (units_in_hunk - unit_in_hunk) * chd->header.unitbytes, buf, unit_in_hunk * chd->header.unitbytes);
					free(buf);
				}
				break;
			}
		}
		return CHDERR_NONE;
	}

	/* We should not reach this code */
	return CHDERR_DECOMPRESSION_ERROR;
}

/***************************************************************************
    INTERNAL MAP ACCESS
***************************************************************************/

/*-------------------------------------------------
    map_read - read the initial sector map
-------------------------------------------------*/

static chd_error map_read(chd_file *chd)
{
	uint32_t entrysize = (chd->header.version < 3) ? OLD_MAP_ENTRY_SIZE : MAP_ENTRY_SIZE;
	uint8_t raw_map_entries[MAP_STACK_ENTRIES * MAP_ENTRY_SIZE];
	uint64_t fileoffset, maxoffset = 0;
	uint8_t cookie[MAP_ENTRY_SIZE];
	chd_error err;
	uint32_t i;

	/* first allocate memory */
	chd->map = (map_entry *)malloc(sizeof(chd->map[0]) * chd->header.totalhunks);
	if (!chd->map)
		return CHDERR_OUT_OF_MEMORY;

	/* read the map entries in in chunks and extract to the map list */
	fileoffset = chd->header.length;
	for (i = 0; i < chd->header.totalhunks; i += MAP_STACK_ENTRIES)
	{
		/* compute how many entries this time */
		int entries = chd->header.totalhunks - i, j;
		if (entries > MAP_STACK_ENTRIES)
			entries = MAP_STACK_ENTRIES;

		/* read that many */
		if (!seek_and_read(chd, fileoffset, raw_map_entries, entries * entrysize))
			EARLY_EXIT(err = CHDERR_READ_ERROR);
		fileoffset += entries * entrysize;

		/* process that many */
		if (entrysize == MAP_ENTRY_SIZE)
		{
			for (j = 0; j < entries; j++)
				map_extract(&raw_map_entries[j * MAP_ENTRY_SIZE], &chd->map[i + j]);
		}
		else
		{
			for (j = 0; j < entries; j++)
				map_extract_old(&raw_map_entries[j * OLD_MAP_ENTRY_SIZE], &chd->map[i + j], chd->header.hunkbytes);
		}

		/* track the maximum offset */
		for (j = 0; j < entries; j++)
			if ((chd->map[i + j].flags & MAP_ENTRY_FLAG_TYPE_MASK) == V34_MAP_ENTRY_TYPE_COMPRESSED ||
				(chd->map[i + j].flags & MAP_ENTRY_FLAG_TYPE_MASK) == V34_MAP_ENTRY_TYPE_UNCOMPRESSED)
				maxoffset = MAX(maxoffset, chd->map[i + j].offset + chd->map[i + j].length);
	}

	/* verify the cookie */
	if (!seek_and_read(chd, fileoffset, &cookie, entrysize) || memcmp(&cookie, END_OF_LIST_COOKIE, entrysize))
		EARLY_EXIT(err = CHDERR_INVALID_FILE);

	/* verify the length */
	if (maxoffset > chd->file_size)
		EARLY_EXIT(err = CHDERR_INVALID_FILE);
	return CHDERR_NONE;

cleanup:
	if (chd->map)
		free(chd->map);
	chd->map = NULL;
	return err;
}

/***************************************************************************
    INTERNAL METADATA ACCESS
***************************************************************************/

/*-------------------------------------------------
    metadata_find_entry - find a metadata entry
-------------------------------------------------*/

static chd_error metadata_find_entry(chd_file *chd, uint32_t metatag, uint32_t metaindex, metadata_entry *metaentry)
{
	/* cap traversal to guard against malformed CHDs with cyclic metadata chains */
	uint32_t iter = 0;
	#define CHD_MAX_METADATA_ENTRIES 65536

	/* start at the beginning */
	metaentry->offset = chd->header.metaoffset;
	metaentry->prev = 0;

	/* loop until we run out of options */
	while (metaentry->offset != 0)
	{
		uint8_t	raw_meta_header[METADATA_HEADER_SIZE];

		if (++iter > CHD_MAX_METADATA_ENTRIES)
			return CHDERR_INVALID_DATA;

		/* read the raw header */
		if (!seek_and_read(chd, metaentry->offset, raw_meta_header, sizeof(raw_meta_header)))
			break;

		/* extract the data */
		metaentry->metatag = get_bigendian_uint32_t(&raw_meta_header[0]);
		metaentry->length = get_bigendian_uint32_t(&raw_meta_header[4]);
		metaentry->next = get_bigendian_uint64_t(&raw_meta_header[8]);

		/* flags are encoded in the high byte of length */
		metaentry->flags = metaentry->length >> 24;
		metaentry->length &= 0x00ffffff;

		/* if we got a match, proceed */
		if (metatag == CHDMETATAG_WILDCARD || metaentry->metatag == metatag)
			if (metaindex-- == 0)
				return CHDERR_NONE;

		/* no match, fetch the next link */
		metaentry->prev = metaentry->offset;
		metaentry->offset = metaentry->next;
	}

	/* if we get here, we didn't find it */
	return CHDERR_METADATA_NOT_FOUND;
}

/***************************************************************************
    CORE FILE
***************************************************************************/

/*-------------------------------------------------
	core_stdio_fopen - core_file wrapper over fopen
-------------------------------------------------*/
static void *core_stdio_fopen(char const *path) {
	return fopen(path, "rb");
}

/*-------------------------------------------------
	core_stdio_fsize - core_file function for
	getting file size with stdio
-------------------------------------------------*/
static uint64_t core_stdio_fsize(void *file) {
#if defined USE_LIBRETRO_VFS
	#define core_stdio_fseek_impl fseek
	#define core_stdio_ftell_impl ftell
#elif defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__WIN64__)
	#define core_stdio_fseek_impl _fseeki64
	#define core_stdio_ftell_impl _ftelli64
#elif defined(__PS3__) && !defined(__PSL1GHT__) || defined(__SWITCH__) || defined(__vita__)
	#define core_stdio_fseek_impl(x,y,z) fseek(x,(off_t)y,z)
	#define core_stdio_ftell_impl(x) (off_t)ftell(x)
#else
	/* With _FILE_OFFSET_BITS=64 defined at the top of this TU, glibc
	 * aliases fseeko/ftello to the 64-bit variants, so no *64 suffix
	 * is needed. This is also the time64-safe spelling on armhf. */
	#define core_stdio_fseek_impl fseeko
	#define core_stdio_ftell_impl ftello
#endif
	FILE *fp;
	uint64_t p, rv;
	fp = (FILE*)file;

	p = core_stdio_ftell_impl(fp);
	core_stdio_fseek_impl(fp, 0, SEEK_END);
	rv = core_stdio_ftell_impl(fp);
	core_stdio_fseek_impl(fp, p, SEEK_SET);
	return rv;
}

/*-------------------------------------------------
	core_stdio_fread - core_file wrapper over fread
-------------------------------------------------*/
static size_t core_stdio_fread(void *ptr, size_t size, size_t nmemb, void *file) {
	return fread(ptr, size, nmemb, (FILE*)file);
}

/*-------------------------------------------------
	core_stdio_fclose - core_file wrapper over fclose
-------------------------------------------------*/
static int core_stdio_fclose(void *file) {
	return fclose((FILE*)file);
}

/*-------------------------------------------------
	core_stdio_fclose_nonowner - don't call fclose because
		we don't own the underlying file.
-------------------------------------------------*/
static int core_stdio_fclose_nonowner(void *file) {
	(void)file;
	return 0;
}

/*-------------------------------------------------
	core_stdio_fseek - core_file wrapper over fclose
-------------------------------------------------*/
static int core_stdio_fseek(void* file, int64_t offset, int whence) {
	return core_stdio_fseek_impl((FILE*)file, offset, whence);
}

/*-------------------------------------------------
	core_legacy_fsize - legacy core_file wrapper
-------------------------------------------------*/
static uint64_t core_legacy_fsize(void *file) {
	core_file* const core = (core_file*)file;
	return core->fsize(core);
}

/*-------------------------------------------------
	core_legacy_fread - legacy core_file wrapper
-------------------------------------------------*/
static size_t core_legacy_fread(void *ptr, size_t size, size_t nmemb, void *file) {
	core_file* const core = (core_file*)file;
	return core->fread(ptr, size, nmemb, core);
}

/*-------------------------------------------------
	core_legacy_fclose - legacy core_file wrapper
-------------------------------------------------*/
static int core_legacy_fclose(void *file) {
	core_file* const core = (core_file*)file;
	return core->fclose(core);
}

/*-------------------------------------------------
	core_legacy_fseek - legacy core_file wrapper
-------------------------------------------------*/
static int core_legacy_fseek(void* file, int64_t offset, int whence) {
	core_file* const core = (core_file*)file;
	return core->fseek(core, offset, whence);
}
