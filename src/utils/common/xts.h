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
typedef union {
    struct {
        uint32_t LowPart;
        uint32_t HighPart;
    };
    uint64_t Value;
} UINT64_STRUCT;

// Public function prototypes

void EncryptBufferXTS (uint8_t *buffer, uint64_t length, const UINT64_STRUCT *startDataUnitNo, unsigned int startCipherBlockNo, uint8_t *ks, uint8_t *ks2, int cipher);
void DecryptBufferXTS (uint8_t *buffer, uint64_t length, const UINT64_STRUCT *startDataUnitNo, unsigned int startCipherBlockNo, uint8_t *ks, uint8_t *ks2, int cipher);

#ifdef __cplusplus
}
#endif

#endif	// #ifndef XTS_H
