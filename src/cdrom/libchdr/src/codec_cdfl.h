#ifndef LIBCHDR_CODEC_CDFL_H
#define LIBCHDR_CODEC_CDFL_H

#include <stdint.h>

#include "../include/libchdr/chd.h"
#include "../include/libchdr/chdconfig.h"
#include "../include/libchdr/flac.h"
#include "codec_zlib.h"

/* codec-private data for the CDFL codec */
typedef struct _cdfl_codec_data cdfl_codec_data;
struct _cdfl_codec_data {
	/* internal state */
	int		swap_endian;
	flac_decoder	decoder;
#if WANT_SUBCODE
	zlib_codec_data		subcode_decompressor;
#endif
	uint8_t*	buffer;
};

/* cdfl compression codec */
chd_error cdfl_codec_init(void* codec, uint32_t hunkbytes);
void cdfl_codec_free(void* codec);
chd_error cdfl_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen);

#endif /* LIBCHDR_CODEC_CDFL_H */
