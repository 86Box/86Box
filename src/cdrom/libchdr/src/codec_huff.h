#ifndef LIBCHDR_CODEC_HUFF_H
#define LIBCHDR_CODEC_HUFF_H

#include <stdint.h>

#include "../include/libchdr/chd.h"

struct huffman_decoder;

/* codec-private data for the FLAC codec */
typedef struct _huff_codec_data huff_codec_data;
struct _huff_codec_data
{
	struct huffman_decoder* decoder;
};

/* huff compression codec */
chd_error huff_codec_init(void *codec, uint32_t hunkbytes);
void huff_codec_free(void *codec);
chd_error huff_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen);

#endif /* LIBCHDR_CODEC_HUFF_H */
