#include "codec_zlib.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static voidpf zlib_fast_alloc(voidpf opaque, zlib_alloc_size items, zlib_alloc_size size);
static void zlib_fast_free(voidpf opaque, voidpf address);
static void zlib_allocator_free(voidpf opaque);

/*-------------------------------------------------
    zlib_codec_init - initialize the ZLIB codec
-------------------------------------------------*/

chd_error zlib_codec_init(void *codec, uint32_t hunkbytes)
{
	int zerr;
	chd_error err;
	zlib_codec_data *data = (zlib_codec_data*)codec;

	(void)hunkbytes;

	/* clear the buffers */
	memset(data, 0, sizeof(zlib_codec_data));

	/* init the inflater first */
	data->inflater.next_in = (Bytef *)data;	/* bogus, but that's ok */
	data->inflater.avail_in = 0;
	data->inflater.zalloc = zlib_fast_alloc;
	data->inflater.zfree = zlib_fast_free;
	data->inflater.opaque = &data->allocator;
	zerr = inflateInit2(&data->inflater, -MAX_WBITS);

	/* convert errors */
	if (zerr == Z_MEM_ERROR)
		err = CHDERR_OUT_OF_MEMORY;
	else if (zerr != Z_OK)
		err = CHDERR_CODEC_ERROR;
	else
		err = CHDERR_NONE;

	return err;
}

/*-------------------------------------------------
    zlib_codec_free - free data for the ZLIB
    codec
-------------------------------------------------*/

void zlib_codec_free(void *codec)
{
	zlib_codec_data *data = (zlib_codec_data *)codec;

	/* deinit the streams */
	if (data != NULL)
	{
		inflateEnd(&data->inflater);

		/* free our fast memory */
		zlib_allocator_free(&data->allocator);
	}
}

/*-------------------------------------------------
    zlib_codec_decompress - decompress data using
    the ZLIB codec
-------------------------------------------------*/

chd_error zlib_codec_decompress(void *codec, const uint8_t *src, uint32_t complen, uint8_t *dest, uint32_t destlen)
{
	zlib_codec_data *data = (zlib_codec_data *)codec;
	int zerr;

	/* reset the decompressor */
	data->inflater.next_in = (Bytef *)src;
	data->inflater.avail_in = complen;
	data->inflater.total_in = 0;
	data->inflater.next_out = (Bytef *)dest;
	data->inflater.avail_out = destlen;
	data->inflater.total_out = 0;
	zerr = inflateReset(&data->inflater);
	if (zerr != Z_OK)
		return CHDERR_DECOMPRESSION_ERROR;

	/* do it */
	zerr = inflate(&data->inflater, Z_FINISH);
	if (data->inflater.total_out != destlen)
		return CHDERR_DECOMPRESSION_ERROR;

	return CHDERR_NONE;
}

/*-------------------------------------------------
    zlib_fast_alloc - fast malloc for ZLIB, which
    allocates and frees memory frequently
-------------------------------------------------*/

/* Huge alignment values for possible SIMD optimization by compiler (NEON, SSE, AVX) */
#define ZLIB_MIN_ALIGNMENT_BITS 512
#define ZLIB_MIN_ALIGNMENT_BYTES (ZLIB_MIN_ALIGNMENT_BITS / 8)

static voidpf zlib_fast_alloc(voidpf opaque, zlib_alloc_size items, zlib_alloc_size size)
{
	zlib_allocator *alloc = (zlib_allocator *)opaque;
	uintptr_t paddr = 0;
	uint32_t *ptr;
	int i;

	/* compute the size, rounding to the nearest 1k */
	size = (size * items + 0x3ff) & ~0x3ff;

	/* reuse a hunk if we can */
	for (i = 0; i < MAX_ZLIB_ALLOCS; i++)
	{
		ptr = alloc->allocptr[i];
		if (ptr && size == *ptr)
		{
			/* set the low bit of the size so we don't match next time */
			*ptr |= 1;

			/* return aligned block address */
			return (voidpf)(alloc->allocptr2[i]);
		}
	}

	/* alloc a new one */
    ptr = (uint32_t *)malloc(size + sizeof(uint32_t) + ZLIB_MIN_ALIGNMENT_BYTES);
	if (!ptr)
		return NULL;

	/* put it into the list */
	for (i = 0; i < MAX_ZLIB_ALLOCS; i++)
		if (!alloc->allocptr[i])
		{
			alloc->allocptr[i] = ptr;
			paddr = (((uintptr_t)ptr) + sizeof(uint32_t) + (ZLIB_MIN_ALIGNMENT_BYTES-1)) & (~(ZLIB_MIN_ALIGNMENT_BYTES-1));
			alloc->allocptr2[i] = (uint32_t*)paddr;
			break;
		}

	/* set the low bit of the size so we don't match next time */
	*ptr = size | 1;

	/* return aligned block address */
	return (voidpf)paddr;
}

/*-------------------------------------------------
    zlib_fast_free - fast free for ZLIB, which
    allocates and frees memory frequently
-------------------------------------------------*/

static void zlib_fast_free(voidpf opaque, voidpf address)
{
	zlib_allocator *alloc = (zlib_allocator *)opaque;
	uint32_t *ptr = (uint32_t *)address;
	int i;

	/* find the hunk */
	for (i = 0; i < MAX_ZLIB_ALLOCS; i++)
		if (ptr == alloc->allocptr2[i])
		{
			/* clear the low bit of the size to allow matches */
			*(alloc->allocptr[i]) &= ~1;
			return;
		}
}

/*-------------------------------------------------
    zlib_allocator_free
-------------------------------------------------*/
static void zlib_allocator_free(voidpf opaque)
{
	zlib_allocator *alloc = (zlib_allocator *)opaque;
	int i;

	for (i = 0; i < MAX_ZLIB_ALLOCS; i++)
		if (alloc->allocptr[i])
			free(alloc->allocptr[i]);
}
