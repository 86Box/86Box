/* license:BSD-3-Clause
 * copyright-holders:Aaron Giles
***************************************************************************

    flac.c

    FLAC compression wrappers

***************************************************************************/

#include <string.h>

#include "../include/libchdr/flac.h"
#include "../include/libchdr/macros.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "../include/dr_libs/dr_flac.h"

/***************************************************************************
 *  FLAC DECODER
 ***************************************************************************
 */

static size_t flac_decoder_read_callback(void *userdata, void *buffer, size_t bytes);
static drflac_bool32 flac_decoder_seek_callback(void *userdata, int offset, drflac_seek_origin origin);
static drflac_bool32 flac_decoder_tell_callback(void *userdata, drflac_int64 *cursor);
static void flac_decoder_metadata_callback(void *userdata, drflac_metadata *metadata);
static void flac_decoder_write_callback(void *userdata, void *buffer, size_t bytes);


/* getters (valid after reset) */
static uint32_t sample_rate(flac_decoder *decoder)  { return decoder->sample_rate; }
static uint8_t channels(flac_decoder *decoder)  { return decoder->channels; }
static uint8_t bits_per_sample(flac_decoder *decoder) { return decoder->bits_per_sample; }

/*-------------------------------------------------
 *  flac_decoder - constructor
 *-------------------------------------------------
 */

int flac_decoder_init(flac_decoder *decoder)
{
	decoder->decoder = NULL;
	decoder->sample_rate = 0;
	decoder->channels = 0;
	decoder->bits_per_sample = 0;
	decoder->compressed_offset = 0;
	decoder->compressed_start = NULL;
	decoder->compressed_length = 0;
	decoder->compressed2_start = NULL;
	decoder->compressed2_length = 0;
	decoder->uncompressed_offset = 0;
	decoder->uncompressed_length = 0;
	decoder->uncompressed_swap = 0;
	return 0;
}

/*-------------------------------------------------
 *  flac_decoder - destructor
 *-------------------------------------------------
 */

void flac_decoder_free(flac_decoder* decoder)
{
	if ((decoder != NULL) && (decoder->decoder != NULL)) {
		drflac_close((drflac*)decoder->decoder);
		decoder->decoder = NULL;
	}
}

/*-------------------------------------------------
 *  reset - reset state with the original
 *  parameters
 *-------------------------------------------------
 */

static int flac_decoder_internal_reset(flac_decoder* decoder)
{
	decoder->compressed_offset = 0;
	flac_decoder_free(decoder);
	decoder->decoder = drflac_open_with_metadata(
		flac_decoder_read_callback, flac_decoder_seek_callback,
		flac_decoder_tell_callback, flac_decoder_metadata_callback,
		decoder, NULL);
	return (decoder->decoder != NULL);
}

/*-------------------------------------------------
 *  reset - reset state with new memory parameters
 *  and a custom-generated header
 *-------------------------------------------------
 */

int flac_decoder_reset(flac_decoder* decoder, uint32_t sample_rate, uint8_t num_channels, uint32_t block_size, const void *buffer, uint32_t length)
{
	/* modify the template header with our parameters */
	static const uint8_t s_header_template[0x2a] =
	{
		0x66, 0x4C, 0x61, 0x43,                         /* +00: 'fLaC' stream header */
		0x80,                                           /* +04: metadata block type 0 (STREAMINFO), */
								/*      flagged as last block */
		0x00, 0x00, 0x22,                               /* +05: metadata block length = 0x22 */
		0x00, 0x00,                                     /* +08: minimum block size */
		0x00, 0x00,                                     /* +0A: maximum block size */
		0x00, 0x00, 0x00,                               /* +0C: minimum frame size (0 == unknown) */
		0x00, 0x00, 0x00,                               /* +0F: maximum frame size (0 == unknown) */
		0x0A, 0xC4, 0x42, 0xF0, 0x00, 0x00, 0x00, 0x00, /* +12: sample rate (0x0ac44 == 44100), */
								/*      numchannels (2), sample bits (16), */
								/*      samples in stream (0 == unknown) */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* +1A: MD5 signature (0 == none) */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /* +2A: start of stream data */
	};
	memcpy(decoder->custom_header, s_header_template, sizeof(s_header_template));
	decoder->custom_header[0x08] = decoder->custom_header[0x0a] = (block_size*num_channels) >> 8;
	decoder->custom_header[0x09] = decoder->custom_header[0x0b] = (block_size*num_channels) & 0xff;
	decoder->custom_header[0x12] = sample_rate >> 12;
	decoder->custom_header[0x13] = sample_rate >> 4;
	decoder->custom_header[0x14] = (sample_rate << 4) | ((num_channels - 1) << 1);

	/* configure the header ahead of the provided buffer */
	decoder->compressed_start = (const uint8_t *)(decoder->custom_header);
	decoder->compressed_length = sizeof(decoder->custom_header);
	decoder->compressed2_start = (const uint8_t *)(buffer);
	decoder->compressed2_length = length;
	return flac_decoder_internal_reset(decoder);
}

/*-------------------------------------------------
 *  decode_interleaved - decode to an interleaved
 *  sound stream
 *-------------------------------------------------
 */

int flac_decoder_decode_interleaved(flac_decoder* decoder, int16_t *samples, uint32_t num_frames, int swap_endian)
{
	int16_t buffer[2352 / sizeof(int16_t)];	/* 2352 is the number of bytes per CD audio sector */
	uint32_t buf_frames = ARRAY_LENGTH(buffer) / channels(decoder);

	/* configure the uncompressed buffer */
	memset(decoder->uncompressed_start, 0, sizeof(decoder->uncompressed_start));
	decoder->uncompressed_start[0] = samples;
	decoder->uncompressed_offset = 0;
	decoder->uncompressed_length = num_frames;
	decoder->uncompressed_swap = swap_endian;

	/* loop until we get everything we want */
	while (decoder->uncompressed_offset < decoder->uncompressed_length) {
		uint32_t frames_to_do = MIN(num_frames, buf_frames);
		if (!drflac_read_pcm_frames_s16((drflac*)decoder->decoder, frames_to_do, buffer))
			return 0;
		flac_decoder_write_callback(decoder, buffer, frames_to_do*sizeof(*buffer)*channels(decoder));
		num_frames -= frames_to_do;
	}
	return 1;
}

/*-------------------------------------------------
 *  finish - finish up the decode
 *-------------------------------------------------
 */

uint32_t flac_decoder_finish(flac_decoder* decoder)
{
	/* get the final decoding position and move forward */
	drflac *flac = (drflac*)decoder->decoder;
	uint64_t position = decoder->compressed_offset;

	/* ugh... there's no function to obtain bytes used in drflac :-/ */
	position -= DRFLAC_CACHE_L2_LINES_REMAINING(&flac->bs) * sizeof(drflac_cache_t);
	position -= DRFLAC_CACHE_L1_BITS_REMAINING(&flac->bs) / 8;
	position -= flac->bs.unalignedByteCount;

	/* adjust position if we provided the header */
	if (position == 0)
		return 0;
	if (decoder->compressed_start == (const uint8_t *)(decoder->custom_header))
		position -= decoder->compressed_length;

	flac_decoder_free(decoder);
	return position;
}

/*-------------------------------------------------
 *  detect_native_endian - detect system endianness
 *-------------------------------------------------
 */

int flac_decoder_detect_native_endian(void)
{
	uint16_t native_endian = 0;
	*(uint8_t *)(&native_endian) = 1;
	return (native_endian & 1);
}

/*-------------------------------------------------
 *  read_callback - handle reads from the input
 *  stream
 *-------------------------------------------------
 */

static size_t flac_decoder_read_callback(void *userdata, void *buffer, size_t bytes)
{
	flac_decoder *decoder = (flac_decoder*)userdata;
	uint8_t *dst = (uint8_t*)buffer;

	/* copy from primary buffer first */
	uint32_t outputpos = 0;
	if (outputpos < bytes && decoder->compressed_offset < decoder->compressed_length)
	{
		uint32_t bytes_to_copy = MIN(bytes - outputpos, decoder->compressed_length - decoder->compressed_offset);
		memcpy(&dst[outputpos], decoder->compressed_start + decoder->compressed_offset, bytes_to_copy);
		outputpos += bytes_to_copy;
		decoder->compressed_offset += bytes_to_copy;
	}

	/* once we're out of that, copy from the secondary buffer */
	if (outputpos < bytes && decoder->compressed_offset < decoder->compressed_length + decoder->compressed2_length)
	{
		uint32_t bytes_to_copy = MIN(bytes - outputpos, decoder->compressed2_length - (decoder->compressed_offset - decoder->compressed_length));
		memcpy(&dst[outputpos], decoder->compressed2_start + decoder->compressed_offset - decoder->compressed_length, bytes_to_copy);
		outputpos += bytes_to_copy;
		decoder->compressed_offset += bytes_to_copy;
	}

	return outputpos;
}

/*-------------------------------------------------
 *  metadata_callback - handle STREAMINFO metadata
 *-------------------------------------------------
 */

static void flac_decoder_metadata_callback(void *userdata, drflac_metadata *metadata)
{
	flac_decoder *decoder = (flac_decoder*)userdata;

	/* ignore all but STREAMINFO metadata */
	if (metadata->type != DRFLAC_METADATA_BLOCK_TYPE_STREAMINFO)
		return;

	/* parse out the data we care about */
	decoder->sample_rate = metadata->data.streaminfo.sampleRate;
	decoder->bits_per_sample = metadata->data.streaminfo.bitsPerSample;
	decoder->channels = metadata->data.streaminfo.channels;
}

/*-------------------------------------------------
 *  write_callback - handle writes to the output
 *  stream
 *-------------------------------------------------
 */

static void flac_decoder_write_callback(void *userdata, void *buffer, size_t bytes)
{
	int sampnum, chan;
	int shift, blocksize;
	flac_decoder * decoder = (flac_decoder *)userdata;
	int16_t *sampbuf = (int16_t *)buffer;
	int sampch = channels(decoder);
	uint32_t offset = decoder->uncompressed_offset;
	uint16_t usample;

	/* interleaved case */
	shift = decoder->uncompressed_swap ? 8 : 0;
	blocksize = bytes / (sampch * sizeof(sampbuf[0]));
	if (decoder->uncompressed_start[1] == NULL)
	{
		int16_t *dest = decoder->uncompressed_start[0] + offset * sampch;
		for (sampnum = 0; sampnum < blocksize && offset < decoder->uncompressed_length; sampnum++, offset++)
			for (chan = 0; chan < sampch; chan++) {
				usample = (uint16_t)*sampbuf++;
				*dest++ = (int16_t)((usample << shift) | (usample >> shift));
			}
	}

	/* non-interleaved case */
	else
	{
		for (sampnum = 0; sampnum < blocksize && offset < decoder->uncompressed_length; sampnum++, offset++)
			for (chan = 0; chan < sampch; chan++) {
				usample = (uint16_t)*sampbuf++;
				if (decoder->uncompressed_start[chan] != NULL)
					decoder->uncompressed_start[chan][offset] = (int16_t) ((usample << shift) | (usample >> shift));
			}
	}
	decoder->uncompressed_offset = offset;
}


/*-------------------------------------------------
 *  seek_callback - handle seeks on the output
 *  stream
 *-------------------------------------------------
 */

static drflac_bool32 flac_decoder_seek_callback(void *userdata, int offset, drflac_seek_origin origin)
{
	flac_decoder * decoder = (flac_decoder *)userdata;
	uint32_t length = decoder->compressed_length + decoder->compressed2_length;

	if (origin == DRFLAC_SEEK_SET) {
		uint32_t pos = offset;
		if (pos <= length) {
			decoder->compressed_offset = pos;
			return DRFLAC_TRUE;
		}
	} else if (origin == DRFLAC_SEEK_CUR) {
		uint32_t pos = decoder->compressed_offset + offset;
		if (pos <= length) {
			decoder->compressed_offset = pos;
			return DRFLAC_TRUE;
		}
	}
	return DRFLAC_FALSE;
}


/*-------------------------------------------------
 *  tell_callback - handle seeks on the output
 *  stream
 *-------------------------------------------------
 */

static drflac_bool32 flac_decoder_tell_callback(void *userdata, drflac_int64 *cursor)
{
	flac_decoder * decoder = (flac_decoder *)userdata;
	*cursor = decoder->compressed_offset;
	return 1;
}
