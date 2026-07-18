#include "codec_cdfl.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../include/libchdr/cdrom.h"

static uint32_t cdfl_codec_blocksize(uint32_t bytes)
{
	/* for CDs it seems that CD_MAX_SECTOR_DATA is the right target */
	uint32_t blocksize = bytes / 4;
	while (blocksize > CD_MAX_SECTOR_DATA)
		blocksize /= 2;
	return blocksize;
}

chd_error cdfl_codec_init(void *codec, uint32_t hunkbytes)
{
#if WANT_SUBCODE
	chd_error ret;
#endif
	cdfl_codec_data *cdfl = (cdfl_codec_data*)codec;

	/* make sure the CHD's hunk size is an even multiple of the frame size */
	if (hunkbytes % CD_FRAME_SIZE != 0)
		return CHDERR_CODEC_ERROR;

	cdfl->buffer = (uint8_t*)malloc(sizeof(uint8_t) * hunkbytes);
	if (cdfl->buffer == NULL)
		return CHDERR_OUT_OF_MEMORY;

	/* determine whether we want native or swapped samples */
	cdfl->swap_endian = flac_decoder_detect_native_endian();

#if WANT_SUBCODE
	/* init zlib inflater */
	ret = zlib_codec_init(&cdfl->subcode_decompressor, (hunkbytes / CD_FRAME_SIZE) * CD_MAX_SECTOR_DATA);
	if (ret != CHDERR_NONE)
		return ret;
#endif

	/* flac decoder init */
	if (flac_decoder_init(&cdfl->decoder))
		return CHDERR_OUT_OF_MEMORY;

	return CHDERR_NONE;
}

void cdfl_codec_free(void *codec)
{
	cdfl_codec_data *cdfl = (cdfl_codec_data*)codec;
	flac_decoder_free(&cdfl->decoder);
#if WANT_SUBCODE
	zlib_codec_free(&cdfl->subcode_decompressor);
#endif
	if (cdfl->buffer)
		free(cdfl->buffer);
}

chd_error cdfl_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen)
{
	uint32_t framenum;
	uint8_t *buffer;
#if WANT_SUBCODE
	uint32_t offset;
	chd_error ret;
#endif
	cdfl_codec_data *cdfl = (cdfl_codec_data*)codec;

	/* reset and decode */
	uint32_t frames = destlen / CD_FRAME_SIZE;

	if (!flac_decoder_reset(&cdfl->decoder, 44100, 2, cdfl_codec_blocksize(frames * CD_MAX_SECTOR_DATA), src, complen))
		return CHDERR_DECOMPRESSION_ERROR;
	buffer = &cdfl->buffer[0];
	if (!flac_decoder_decode_interleaved(&cdfl->decoder, (int16_t *)(buffer), frames * CD_MAX_SECTOR_DATA/4, cdfl->swap_endian))
		return CHDERR_DECOMPRESSION_ERROR;

#if WANT_SUBCODE
	/* inflate the subcode data */
	offset = flac_decoder_finish(&cdfl->decoder);
	ret = zlib_codec_decompress(&cdfl->subcode_decompressor, src + offset, complen - offset, &cdfl->buffer[frames * CD_MAX_SECTOR_DATA], frames * CD_MAX_SUBCODE_DATA);
	if (ret != CHDERR_NONE)
		return ret;
#else
	flac_decoder_finish(&cdfl->decoder);
#endif

	/* reassemble the data */
	for (framenum = 0; framenum < frames; framenum++)
	{
		memcpy(&dest[framenum * CD_FRAME_SIZE], &cdfl->buffer[framenum * CD_MAX_SECTOR_DATA], CD_MAX_SECTOR_DATA);
#if WANT_SUBCODE
		memcpy(&dest[framenum * CD_FRAME_SIZE + CD_MAX_SECTOR_DATA], &cdfl->buffer[frames * CD_MAX_SECTOR_DATA + framenum * CD_MAX_SUBCODE_DATA], CD_MAX_SUBCODE_DATA);
#endif
	}

	return CHDERR_NONE;
}
