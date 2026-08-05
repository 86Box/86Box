#ifndef LIBCHDR_CODEC_CDZL_H
#define LIBCHDR_CODEC_CDZL_H

#include <stdint.h>

#include "../include/libchdr/chd.h"
#include "../include/libchdr/chdconfig.h"
#include "codec_zlib.h"

/* codec-private data for the CDZL codec */
typedef struct _cdzl_codec_data cdzl_codec_data;
struct _cdzl_codec_data {
	/* internal state */
	zlib_codec_data		base_decompressor;
#if WANT_SUBCODE
	zlib_codec_data		subcode_decompressor;
#endif
	uint8_t*			buffer;
};

/* cdzl compression codec */
chd_error cdzl_codec_init(void* codec, uint32_t hunkbytes);
void cdzl_codec_free(void* codec);
chd_error cdzl_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen);

#endif /* LIBCHDR_CODEC_CDZL_H */
