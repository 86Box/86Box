/*
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Adapted by TrueCrypt Foundation */

#ifndef  _RMD160_H
#define  _RMD160_H

#include <inttypes.h>

#if defined(__cplusplus)
extern "C"
{
#endif

#ifndef	u_int32_t
#define	u_int32_t	uint32_t
#define	u_int64_t	uint64_t
#define	u_char		uint8_t
#endif

/* RMD160 context. */
typedef struct RMD160Context {
	u_int32_t state[5];	/* state */
#ifndef TC_NO_COMPILER_INT64
	u_int64_t count;	/* number of bits, modulo 2^64 */
#else
	u_int32_t count;
#endif
	u_char buffer[64];	/* input buffer */
} RMD160_CTX;

void	 RMD160Init(RMD160_CTX *);
void	 RMD160Transform(u_int32_t [5], const u_char [64]);
void	 RMD160Update(RMD160_CTX *, const u_char *, u_int32_t);
void	 RMD160Final(u_char [20], RMD160_CTX *);

#if defined(__cplusplus)
}
#endif

#endif  /* _RMD160_H */
