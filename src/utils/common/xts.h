/*
 Copyright (c) 2008 TrueCrypt Foundation. All rights reserved.

 Governed by the TrueCrypt License 2.4 the full text of which is contained
 in the file License.txt included in TrueCrypt binary and source code
 distribution packages.
*/

#ifndef XTS_H
#define XTS_H

// Header files (optional)

#include <inttypes.h>

#include "tcdefs.h"
#include "../common/endian.h"
#include "crypto.h"


#ifdef __cplusplus
extern "C" {
#endif

// Macros

#ifndef LITTLE_ENDIAN
#	define LITTLE_ENDIAN	1
#endif

#ifndef BIG_ENDIAN
#	define BIG_ENDIAN		2
#endif

#ifndef BYTE_ORDER
#	define BYTE_ORDER		LITTLE_ENDIAN
#endif

#ifndef LE64
#	if BYTE_ORDER == LITTLE_ENDIAN
#		define LE64(x)	(x)
#	endif
#endif

// Custom data types

#ifndef TC_LARGEST_COMPILER_UINT
#	ifdef TC_NO_COMPILER_INT64
		typedef uint32_t	TC_LARGEST_COMPILER_UINT;
#	else
		typedef uint64_t	TC_LARGEST_COMPILER_UINT;
#	endif
#endif

#ifndef TCDEFS_H
typedef union
{
	struct
	{
		uint32_t LowPart;
		uint32_t HighPart;
	};
#	ifndef TC_NO_COMPILER_INT64
	uint64_t Value;
#	endif

} UINT64_STRUCT;
#endif

// Public function prototypes

void EncryptBufferXTS (uint8_t *buffer, TC_LARGEST_COMPILER_UINT length, const UINT64_STRUCT *startDataUnitNo, unsigned int startCipherBlockNo, uint8_t *ks, uint8_t *ks2, int cipher);
void DecryptBufferXTS (uint8_t *buffer, TC_LARGEST_COMPILER_UINT length, const UINT64_STRUCT *startDataUnitNo, unsigned int startCipherBlockNo, uint8_t *ks, uint8_t *ks2, int cipher);

#ifdef __cplusplus
}
#endif

#endif	// #ifndef XTS_H
