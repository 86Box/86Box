#ifndef LIBCHDR_CODEC_ZSTD_H
#define LIBCHDR_CODEC_ZSTD_H

#include <stdint.h>

#ifdef CHDR_SYSTEM_ZSTD
#include <zstd.h>
#else
#include "../deps/zstd-1.5.7/zstd.h"
#endif

#include "../include/libchdr/chd.h"

/* codec-private data for the ZSTD codec */

typedef struct _zstd_codec_data zstd_codec_data;
struct _zstd_codec_data
{
	ZSTD_DStream *dstream;
};

/* zstd compression codec */
chd_error zstd_codec_init(void *codec, uint32_t hunkbytes);
void zstd_codec_free(void *codec);
chd_error zstd_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen);

#endif /* LIBCHDR_CODEC_ZSTD_H */
