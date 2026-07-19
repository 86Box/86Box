#ifndef LIBCHDR_CODEC_ZLIB_H
#define LIBCHDR_CODEC_ZLIB_H

#include <stdint.h>

#if defined(__PS3__) || defined(__PSL1GHT__)
#define __MACTYPES__
#endif
#ifdef CHDR_SYSTEM_ZLIB
#include <zlib.h>
typedef uInt zlib_alloc_size;
#else
#include "../deps/miniz-3.1.1/miniz.h"
typedef size_t zlib_alloc_size;
#endif

#include "../include/libchdr/chd.h"

/* codec-private data for the ZLIB codec */
#define MAX_ZLIB_ALLOCS				64

typedef struct _zlib_allocator zlib_allocator;
struct _zlib_allocator
{
	uint32_t *				allocptr[MAX_ZLIB_ALLOCS];
	uint32_t *				allocptr2[MAX_ZLIB_ALLOCS];
};

typedef struct _zlib_codec_data zlib_codec_data;
struct _zlib_codec_data
{
	z_stream				inflater;
	zlib_allocator			allocator;
};

/* zlib compression codec */
chd_error zlib_codec_init(void *codec, uint32_t hunkbytes);
void zlib_codec_free(void *codec);
chd_error zlib_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen);

#endif /* LIBCHDR_CODEC_ZLIB_H */
