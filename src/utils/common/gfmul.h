/*
 ---------------------------------------------------------------------------
 Copyright (c) 2003, Dr Brian Gladman, Worcester, UK.   All rights reserved.

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
 Issue Date: 31/01/2004
*/

/* Adapted for TrueCrypt by the TrueCrypt Foundation */

#ifndef _GCM_H
#define _GCM_H

#include <inttypes.h>

#if defined(__cplusplus)
extern "C"
{
#endif

#define CBLK_LEN   16  /* encryption block length */
#define CBLK_LEN8  8

typedef struct
{
    uint32_t gf_t8k[CBLK_LEN * 2][16][CBLK_LEN / 4];
} GfCtx8k;

typedef struct
{
		uint32_t gf_t4k[CBLK_LEN8 * 2][16][CBLK_LEN / 4];
} GfCtx4k64;

typedef struct
{
	/* union not used to support faster mounting */
    uint32_t gf_t128[CBLK_LEN * 2 / 2][16][CBLK_LEN / 4];
    uint32_t gf_t64[CBLK_LEN8 * 2][16][CBLK_LEN8 / 4];
} GfCtx;

typedef int  ret_type;

void GfMul128 (void *a, const void* b);
void GfMul128Tab(unsigned char a[16], GfCtx8k *ctx);
int Gf128Tab64Init (uint8_t *a, GfCtx *ctx);
void Gf128MulBy64Tab (uint8_t a[8], uint8_t p[16], GfCtx *ctx);
int Gf64TabInit (uint8_t *a, GfCtx *ctx);
void Gf64MulTab (unsigned char a[8], unsigned char p[8], GfCtx *ctx);
void MirrorBits128 (uint8_t *a);
void MirrorBits64 (uint8_t *a);
int GfMulSelfTest (void);

#if defined(__cplusplus)
}
#endif

#endif
