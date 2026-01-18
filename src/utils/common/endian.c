/*
 Legal Notice: Some portions of the source code contained in this file were
 derived from the source code of Encryption for the Masses 2.02a, which is
 Copyright (c) 1998-2000 Paul Le Roux and which is governed by the 'License
 Agreement for Encryption for the Masses'. Modifications and additions to
 the original source code (contained in this file) and all other portions of
 this file are Copyright (c) 2003-2008 TrueCrypt Foundation and are governed
 by the TrueCrypt License 2.4 the full text of which is contained in the
 file License.txt included in TrueCrypt binary and source code distribution
 packages. */

#include "../common/endian.h"


uint16_t MirrorBytes16 (uint16_t x)
{
	return (x << 8) | (x >> 8);
}


uint32_t MirrorBytes32 (uint32_t x)
{
	uint32_t n = (uint8_t) x;
	n <<= 8; n |= (uint8_t) (x >> 8);
	n <<= 8; n |= (uint8_t) (x >> 16);
	return (n << 8) | (uint8_t) (x >> 24);
}

uint64_t MirrorBytes64 (uint64_t x)
{
	uint64_t n = (uint8_t) x;
	n <<= 8; n |= (uint8_t) (x >> 8);
	n <<= 8; n |= (uint8_t) (x >> 16);
	n <<= 8; n |= (uint8_t) (x >> 24);
	n <<= 8; n |= (uint8_t) (x >> 32);
	n <<= 8; n |= (uint8_t) (x >> 40);
	n <<= 8; n |= (uint8_t) (x >> 48);
	return (n << 8) | (uint8_t) (x >> 56);
}

void
LongReverse (uint32_t *buffer, unsigned byteCount)
{
	uint32_t value;

	byteCount /= sizeof (uint32_t);
	while (byteCount--)
	{
		value = *buffer;
		value = ((value & 0xFF00FF00L) >> 8) | \
		    ((value & 0x00FF00FFL) << 8);
		*buffer++ = (value << 16) | (value >> 16);
	}
}
