#include "codec_zstd.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*-------------------------------------------------
 *  zstd_codec_init - constructor
 *-------------------------------------------------
 */

chd_error zstd_codec_init(void* codec, uint32_t hunkbytes)
{
	zstd_codec_data* zstd_codec = (zstd_codec_data*) codec;

	(void)hunkbytes;
	zstd_codec->dstream = ZSTD_createDStream();
	if (!zstd_codec->dstream) {
#if 0
		printf("NO DSTREAM CREATED!\n");
#endif
		return CHDERR_DECOMPRESSION_ERROR;
	}
	return CHDERR_NONE;
}

/*-------------------------------------------------
 *  zstd_codec_free
 *-------------------------------------------------
 */

void zstd_codec_free(void* codec)
{
	zstd_codec_data* zstd_codec = (zstd_codec_data*) codec;

	ZSTD_freeDStream(zstd_codec->dstream);
}

/*-------------------------------------------------
 *  decompress - decompress data using the ZSTD 
 *  codec
 *-------------------------------------------------
 */
chd_error zstd_codec_decompress(void* codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen)
{
	ZSTD_inBuffer input;
	ZSTD_outBuffer output;

	/* initialize */
	zstd_codec_data* zstd_codec = (zstd_codec_data*) codec;

	/* reset decompressor */
	size_t zstd_res =  ZSTD_initDStream(zstd_codec->dstream);

	if (ZSTD_isError(zstd_res)) 
	{
#if 0
		printf("INITI DSTREAM FAILED!\n");
#endif
		return CHDERR_DECOMPRESSION_ERROR;
	}

	input.src   = src;
	input.size  = complen;
	input.pos   = 0;

	output.dst  = dest;
	output.size = destlen;
	output.pos  = 0;

	while ((input.pos < input.size) && (output.pos < output.size))
	{
		zstd_res = ZSTD_decompressStream(zstd_codec->dstream, &output, &input);
		if (ZSTD_isError(zstd_res))
		{
#if 0
			printf("DECOMPRESSION ERROR IN LOOP\n");
#endif
			return CHDERR_DECOMPRESSION_ERROR;
		}
	}
	if (output.pos != output.size)
	{
#if 0
		printf("OUTPUT DOESN'T MATCH!\n");
#endif
		return CHDERR_DECOMPRESSION_ERROR;
	}
	return CHDERR_NONE;

}
