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

#ifndef TC_HEADER_CRC
#define TC_HEADER_CRC

#include <inttypes.h>

#include "tcdefs.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#define UPDC32(octet, crc)\
  (uint32_t)((crc_32_tab[(((uint32_t)(crc)) ^ ((unsigned char)(octet))) & 0xff] ^ (((uint32_t)(crc)) >> 8)))

uint32_t GetCrc32 (unsigned char *data, int length);
uint32_t crc32int (uint32_t *data);
int crc32_selftests (void);

extern uint32_t crc_32_tab[];

#if defined(__cplusplus)
}
#endif

#endif // TC_HEADER_CRC
