/*
 ---------------------------------------------------------------------------
 Copyright (c) 2002, Dr Brian Gladman, Worcester, UK.   All rights reserved.

 LICENSE TERMS

 The free distribution and use of this software is allowed (with or without
 changes) provided that:

  1. source code distributions include the above copyright notice, this
     list of conditions and the following disclaimer;

  2. binary distributions include the above copyright notice, this list
     of conditions and the following disclaimer in their documentation;

  3. the name of the copyright holder is not used to endorse products
     built using this software without specific written permission.

 DISCLAIMER

 This software is provided 'as is' with no explicit or implied warranties
 in respect of its properties, including, but not limited to, correctness
 and/or fitness for purpose.
 ---------------------------------------------------------------------------
 Issue Date: 26/08/2003
*/

#ifndef _SHA1_H
#define _SHA1_H

#include <limits.h>
#include <inttypes.h>

#define SHA1_BLOCK_SIZE  64
#define SHA1_DIGEST_SIZE 20

#if defined(__cplusplus)
extern "C"
{
#endif

/* define an unsigned 32-bit type */

#if defined(_MSC_VER)
  typedef   uint32_t    sha1_32t;
#elif defined(ULONG_MAX) && ULONG_MAX == 0xfffffffful
  typedef   uint32_t    sha1_32t;
#elif defined(UINT_MAX) && UINT_MAX == 0xffffffff
  typedef   uint32_t     sha1_32t;
#else
#  error Please define sha1_32t as an unsigned 32 bit type in sha1.h
#endif

/* type to hold the SHA256 context  */

typedef struct
{   sha1_32t count[2];
    sha1_32t hash[5];
    sha1_32t wbuf[16];
} sha1_ctx;

/* Note that these prototypes are the same for both bit and */
/* byte oriented implementations. However the length fields */
/* are in bytes or bits as appropriate for the version used */
/* and bit sequences are input as arrays of bytes in which  */
/* bit sequences run from the most to the least significant */
/* end of each byte                                         */

void sha1_compile(sha1_ctx ctx[1]);

void sha1_begin(sha1_ctx ctx[1]);
void sha1_hash(const uint8_t data[], uint32_t len, sha1_ctx ctx[1]);
void sha1_end(uint8_t hval[], sha1_ctx ctx[1]);
void sha1(uint8_t hval[], const uint8_t data[], uint32_t len);

#if defined(__cplusplus)
}
#endif

#endif
