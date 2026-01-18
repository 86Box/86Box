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

#ifndef TC_ENDIAN_H
#define TC_ENDIAN_H

#include <inttypes.h>

#if defined(__cplusplus)
extern "C"
{
#endif

#ifdef _WIN32

#	ifndef LITTLE_ENDIAN
#		define LITTLE_ENDIAN 1234
#	endif
#	ifndef BYTE_ORDER
#		define BYTE_ORDER LITTLE_ENDIAN
#	endif

#elif !defined(BYTE_ORDER)

#	ifdef TC_MACOSX
#		include <machine/endian.h>
#	elif defined (TC_BSD)
#		include <sys/endian.h>
#	else
#		include <endian.h>
#	endif

#	ifndef BYTE_ORDER
#		ifndef __BYTE_ORDER
#			error Byte order cannot be determined (BYTE_ORDER undefined)
#		endif

#		define BYTE_ORDER __BYTE_ORDER
#	endif

#	ifndef LITTLE_ENDIAN
#		define LITTLE_ENDIAN __LITTLE_ENDIAN
#	endif

#	ifndef BIG_ENDIAN
#		define BIG_ENDIAN __BIG_ENDIAN
#	endif

#endif // !BYTE_ORDER

/* Macros to read and write 16, 32, and 64-bit quantities in a portable manner.
   These functions are implemented as macros rather than true functions as
   the need to adjust the memory pointers makes them somewhat painful to call
   in user code */

#define mputInt64(memPtr,data) \
	*memPtr++ = ( unsigned char ) ( ( ( data ) >> 56 ) & 0xFF ), \
	*memPtr++ = ( unsigned char ) ( ( ( data ) >> 48 ) & 0xFF ), \
	*memPtr++ = ( unsigned char ) ( ( ( data ) >> 40 ) & 0xFF ), \
	*memPtr++ = ( unsigned char ) ( ( ( data ) >> 32 ) & 0xFF ), \
	*memPtr++ = ( unsigned char ) ( ( ( data ) >> 24 ) & 0xFF ), \
	*memPtr++ = ( unsigned char ) ( ( ( data ) >> 16 ) & 0xFF ), \
	*memPtr++ = ( unsigned char ) ( ( ( data ) >> 8 ) & 0xFF ), \
	*memPtr++ = ( unsigned char ) ( ( data ) & 0xFF )

#define mputLong(memPtr,data) \
	*memPtr++ = ( unsigned char ) ( ( ( data ) >> 24 ) & 0xFF ), \
	*memPtr++ = ( unsigned char ) ( ( ( data ) >> 16 ) & 0xFF ), \
	*memPtr++ = ( unsigned char ) ( ( ( data ) >> 8 ) & 0xFF ), \
	*memPtr++ = ( unsigned char ) ( ( data ) & 0xFF )

#define mputWord(memPtr,data) \
	*memPtr++ = ( unsigned char ) ( ( ( data ) >> 8 ) & 0xFF ), \
	*memPtr++ = ( unsigned char ) ( ( data ) & 0xFF )

#define mputByte(memPtr,data)	\
	*memPtr++ = ( unsigned char ) data

#define mputBytes(memPtr,data,len)  \
	memcpy (memPtr,data,len); \
	memPtr += len;

#define mgetInt64(memPtr) 		\
	( memPtr += 8, ( ( unsigned __int64 ) memPtr[ -8 ] << 56 ) | ( ( unsigned __int64 ) memPtr[ -7 ] << 48 ) | \
	( ( unsigned __int64 ) memPtr[ -6 ] << 40 ) | ( ( unsigned __int64 ) memPtr[ -5 ] << 32 ) | \
	( ( unsigned __int64 ) memPtr[ -4 ] << 24 ) | ( ( unsigned __int64 ) memPtr[ -3 ] << 16 ) | \
	  ( ( unsigned __int64 ) memPtr[ -2 ] << 8 ) | ( unsigned __int64 ) memPtr[ -1 ] )

#define mgetLong(memPtr) 		\
	( memPtr += 4, ( ( unsigned __int32 ) memPtr[ -4 ] << 24 ) | ( ( unsigned __int32 ) memPtr[ -3 ] << 16 ) | \
	  ( ( unsigned __int32 ) memPtr[ -2 ] << 8 ) | ( unsigned __int32 ) memPtr[ -1 ] )

#define mgetWord(memPtr) 		\
	( memPtr += 2, ( unsigned short ) memPtr[ -2 ] << 8 ) | ( ( unsigned short ) memPtr[ -1 ] )

#define mgetByte(memPtr)		\
	( ( unsigned char ) *memPtr++ )

#if BYTE_ORDER == BIG_ENDIAN
#	define LE16(x) MirrorBytes16(x)
#	define LE32(x) MirrorBytes32(x)
#	define LE64(x) MirrorBytes64(x)
#else
#	define LE16(x) (x)
#	define LE32(x) (x)
#	define LE64(x) (x)
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#	define BE16(x) MirrorBytes16(x)
#	define BE32(x) MirrorBytes32(x)
#	define BE64(x) MirrorBytes64(x)
#else
#	define BE16(x) (x)
#	define BE32(x) (x)
#	define BE64(x) (x)
#endif

uint16_t MirrorBytes16 (uint16_t x);
uint32_t MirrorBytes32 (uint32_t x);
#ifndef TC_NO_COMPILER_INT64
uint64_t MirrorBytes64 (uint64_t x);
#endif
void LongReverse ( uint32_t *buffer , unsigned byteCount );

#if defined(__cplusplus)
}
#endif

#endif /* TC_ENDIAN_H */
