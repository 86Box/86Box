#ifndef LIBCHDR_CODEC_LZMA_H
#define LIBCHDR_CODEC_LZMA_H

#include <stdint.h>

#include "../deps/lzma-25.01/include/LzmaDec.h"

#include "../include/libchdr/chd.h"

/* codec-private data for the LZMA codec */
#define MAX_LZMA_ALLOCS 64

typedef struct _lzma_allocator lzma_allocator;
struct _lzma_allocator
{
	ISzAlloc	base;	/* must be first member for (ISzAlloc*) cast compatibility */
	uint32_t*	allocptr[MAX_LZMA_ALLOCS];
	uint32_t*	allocptr2[MAX_LZMA_ALLOCS];
};

typedef struct _lzma_codec_data lzma_codec_data;
struct _lzma_codec_data
{
	CLzmaDec		decoder;
	lzma_allocator	allocator;
};

/* lzma compression codec */
chd_error lzma_codec_init(void *codec, uint32_t hunkbytes);
void lzma_codec_free(void *codec);
chd_error lzma_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen);

#endif /* LIBCHDR_CODEC_LZMA_H */
