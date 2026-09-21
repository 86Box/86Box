#ifndef LIBCHDR_CODEC_CDLZ_H
#define LIBCHDR_CODEC_CDLZ_H

#include <stdint.h>

#include "../include/libchdr/chd.h"
#include "../include/libchdr/chdconfig.h"
#include "codec_lzma.h"
#include "codec_zlib.h"

/* codec-private data for the CDLZ codec */
typedef struct _cdlz_codec_data cdlz_codec_data;
struct _cdlz_codec_data {
	/* internal state */
	lzma_codec_data		base_decompressor;
#if WANT_SUBCODE
	zlib_codec_data		subcode_decompressor;
#endif
	uint8_t*			buffer;
};

/* cdlz compression codec */
chd_error cdlz_codec_init(void* codec, uint32_t hunkbytes);
void cdlz_codec_free(void* codec);
chd_error cdlz_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen);

#endif /* LIBCHDR_CODEC_CDLZ_H */
