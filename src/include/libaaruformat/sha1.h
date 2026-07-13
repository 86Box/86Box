/*
 * Public domain or MIT-licensed SHA-1 implementation for libaaruformat.
 *
 * Based on a clean-room implementation referencing the FIPS PUB 180-1 specification.
 *
 * This code is released into the public domain. If that is not recognized
 * in your jurisdiction, you may treat it under the terms of the MIT license:
 *
 * MIT License
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef LIBAARUFORMAT_SHA1_H
#define LIBAARUFORMAT_SHA1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef SHA1_DIGEST_LENGTH
#define SHA1_DIGEST_LENGTH 20
#endif

    typedef struct
    {
        uint32_t state[5]; /* A,B,C,D,E */
        uint64_t count;    /* Bits processed */
        uint8_t  buffer[64];
    } sha1_ctx;

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_SHA1_H */
