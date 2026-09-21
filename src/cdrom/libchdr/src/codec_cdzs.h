#ifndef LIBCHDR_CODEC_CDZS_H
#define LIBCHDR_CODEC_CDZS_H

#include <stdint.h>

#include "../include/libchdr/chd.h"
#include "../include/libchdr/chdconfig.h"
#include "codec_zstd.h"

/* codec-private data for the CDZS codec */
typedef struct _cdzs_codec_data cdzs_codec_data;
struct _cdzs_codec_data
{
	zstd_codec_data base_decompressor;
#if WANT_SUBCODE
	zstd_codec_data subcode_decompressor;
#endif
	uint8_t*				buffer;
};

/* cdlz compression codec */
chd_error cdzs_codec_init(void *codec, uint32_t hunkbytes);
void cdzs_codec_free(void *codec);
chd_error cdzs_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen);

#endif /* LIBCHDR_CODEC_CDZS_H */
