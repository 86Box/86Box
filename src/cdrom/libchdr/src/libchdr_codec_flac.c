#include "codec_flac.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------
 *  flac_codec_blocksize - return the optimal block size
 *------------------------------------------------------
 */

static uint32_t flac_codec_blocksize(uint32_t bytes)
{
	/* determine FLAC block size, which must be 16-65535
	 * clamp to 2k since that's supposed to be the sweet spot */
	uint32_t blocksize = bytes / 4;
	while (blocksize > 2048)
		blocksize /= 2;
	return blocksize;
}

chd_error flac_codec_init(void *codec, uint32_t hunkbytes)
{
	flac_codec_data *flac = (flac_codec_data*)codec;

	/* make sure the CHD's hunk size is an even multiple of the sample size */
	if (hunkbytes % 4 != 0)
		return CHDERR_CODEC_ERROR;

	/* determine whether we want native or swapped samples */
	flac->native_endian = flac_decoder_detect_native_endian();

	/* flac decoder init */
	if (flac_decoder_init(&flac->decoder))
		return CHDERR_OUT_OF_MEMORY;

	return CHDERR_NONE;
}

void flac_codec_free(void *codec)
{
	flac_codec_data *flac = (flac_codec_data*)codec;
	flac_decoder_free(&flac->decoder);
}

chd_error flac_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen)
{
	flac_codec_data *flac = (flac_codec_data*)codec;
	int swap_endian;

	if (src[0] == 'L')
		swap_endian = !flac->native_endian;
	else if (src[0] == 'B')
		swap_endian = flac->native_endian;
	else
		return CHDERR_DECOMPRESSION_ERROR;

	if (!flac_decoder_reset(&flac->decoder, 44100, 2, flac_codec_blocksize(destlen), src + 1, complen - 1))
		return CHDERR_DECOMPRESSION_ERROR;
	if (!flac_decoder_decode_interleaved(&flac->decoder, (int16_t *)(dest), destlen/4, swap_endian))
		return CHDERR_DECOMPRESSION_ERROR;
	flac_decoder_finish(&flac->decoder);

	return CHDERR_NONE;
}
