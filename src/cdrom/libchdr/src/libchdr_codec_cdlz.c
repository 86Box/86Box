#include "codec_cdlz.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../include/libchdr/cdrom.h"

chd_error cdlz_codec_init(void* codec, uint32_t hunkbytes)
{
	chd_error ret;
	cdlz_codec_data* cdlz = (cdlz_codec_data*) codec;

	/* allocate buffer */
	cdlz->buffer = (uint8_t*)malloc(sizeof(uint8_t) * hunkbytes);
	if (cdlz->buffer == NULL)
		return CHDERR_OUT_OF_MEMORY;

	/* make sure the CHD's hunk size is an even multiple of the frame size */
	ret = lzma_codec_init(&cdlz->base_decompressor, (hunkbytes / CD_FRAME_SIZE) * CD_MAX_SECTOR_DATA);
	if (ret != CHDERR_NONE)
		return ret;

#if WANT_SUBCODE
	ret = zlib_codec_init(&cdlz->subcode_decompressor, (hunkbytes / CD_FRAME_SIZE) * CD_MAX_SUBCODE_DATA);
	if (ret != CHDERR_NONE)
		return ret;
#endif

	if (hunkbytes % CD_FRAME_SIZE != 0)
		return CHDERR_CODEC_ERROR;

	return CHDERR_NONE;
}

void cdlz_codec_free(void* codec)
{
	cdlz_codec_data* cdlz = (cdlz_codec_data*) codec;
	free(cdlz->buffer);
	lzma_codec_free(&cdlz->base_decompressor);
#if WANT_SUBCODE
	zlib_codec_free(&cdlz->subcode_decompressor);
#endif
}

chd_error cdlz_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen)
{
	cdlz_codec_data* cdlz = (cdlz_codec_data*)codec;

	return cd_codec_decompress(cdlz->buffer,
		&cdlz->base_decompressor, lzma_codec_decompress,
#if WANT_SUBCODE
		&cdlz->subcode_decompressor, zlib_codec_decompress,
#else
		NULL, NULL,
#endif
		src, complen, dest, destlen
	);
}
