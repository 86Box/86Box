/*
 * Public domain / MIT SHA-256 implementation for libaaruformat.
 *
 * This code is released into the public domain. If that is not recognized
 * in your jurisdiction, you may instead treat it under the terms of the
 * MIT license reproduced below.
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
#ifndef LIBAARUFORMAT_SHA256_H
#define LIBAARUFORMAT_SHA256_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef SHA256_DIGEST_LENGTH
#define SHA256_DIGEST_LENGTH 32
#endif

    typedef struct
    {
        uint32_t state[8];
        uint64_t bitcount; /* total bits processed */
        uint8_t  buffer[64];
    } sha256_ctx;

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_SHA256_H */
