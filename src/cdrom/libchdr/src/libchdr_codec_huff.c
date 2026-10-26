#include "codec_huff.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../include/libchdr/huffman.h"

chd_error huff_codec_init(void* codec, uint32_t hunkbytes)
{
	huff_codec_data* huff_codec = (huff_codec_data*) codec;
	(void)hunkbytes;
	huff_codec->decoder = create_huffman_decoder(256, 16);
	return CHDERR_NONE;
}

void huff_codec_free(void *codec)
{
	huff_codec_data* huff_codec = (huff_codec_data*) codec;
	delete_huffman_decoder(huff_codec->decoder);
}

chd_error huff_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen)
{
	huff_codec_data* huff_codec = (huff_codec_data*) codec;
	struct bitstream* bitbuf = create_bitstream(src, complen);
	uint32_t cur;
	chd_error result;

	/* first import the tree */
	enum huffman_error err = huffman_import_tree_huffman(huff_codec->decoder, bitbuf);
	if (err != HUFFERR_NONE)
	{
		free(bitbuf);
		return CHDERR_DECOMPRESSION_ERROR;
	}

	/* then decode the data */
	for (cur = 0; cur < destlen; cur++)
		dest[cur] = huffman_decode_one(huff_codec->decoder, bitbuf);
	bitstream_flush(bitbuf);
	result = bitstream_overflow(bitbuf) ? CHDERR_DECOMPRESSION_ERROR : CHDERR_NONE;

	free(bitbuf);
	return result;
}
