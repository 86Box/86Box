#include "codec_cdzs.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../include/libchdr/cdrom.h"

chd_error cdzs_codec_init(void* codec, uint32_t hunkbytes)
{
	chd_error ret;
	cdzs_codec_data* cdzs = (cdzs_codec_data*) codec;

	/* allocate buffer */
	cdzs->buffer = (uint8_t*)malloc(sizeof(uint8_t) * hunkbytes);
	if (cdzs->buffer == NULL)
		return CHDERR_OUT_OF_MEMORY;

	/* make sure the CHD's hunk size is an even multiple of the frame size */
	ret = zstd_codec_init(&cdzs->base_decompressor, (hunkbytes / CD_FRAME_SIZE) * CD_MAX_SECTOR_DATA);
	if (ret != CHDERR_NONE)
		return ret;

#if WANT_SUBCODE
	ret = zstd_codec_init(&cdzs->subcode_decompressor, (hunkbytes / CD_FRAME_SIZE) * CD_MAX_SUBCODE_DATA);
	if (ret != CHDERR_NONE)
		return ret;
#endif

	if (hunkbytes % CD_FRAME_SIZE != 0)
		return CHDERR_CODEC_ERROR;

	return CHDERR_NONE;
}

void cdzs_codec_free(void* codec)
{
	cdzs_codec_data* cdzs = (cdzs_codec_data*) codec;
	free(cdzs->buffer);
	zstd_codec_free(&cdzs->base_decompressor);
#if WANT_SUBCODE
	zstd_codec_free(&cdzs->subcode_decompressor);
#endif
}

chd_error cdzs_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen)
{
	cdzs_codec_data* cdzs = (cdzs_codec_data*)codec;

	return cd_codec_decompress(cdzs->buffer,
		&cdzs->base_decompressor, zstd_codec_decompress,
#if WANT_SUBCODE
		&cdzs->subcode_decompressor, zstd_codec_decompress,
#else
		NULL, NULL,
#endif
		src, complen, dest, destlen
	);
}
