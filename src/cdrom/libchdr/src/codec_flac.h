#ifndef LIBCHDR_CODEC_FLAC_H
#define LIBCHDR_CODEC_FLAC_H

#include <stdint.h>

#include "../include/libchdr/chd.h"
#include "../include/libchdr/flac.h"

/* codec-private data for the FLAC codec */
typedef struct _flac_codec_data flac_codec_data;
struct _flac_codec_data {
	/* internal state */
	int		native_endian;
	flac_decoder	decoder;
};

/* flac compression codec */
chd_error flac_codec_init(void *codec, uint32_t hunkbytes);
void flac_codec_free(void *codec);
chd_error flac_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen);

#endif /* LIBCHDR_CODEC_FLAC_H */
