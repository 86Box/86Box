/*
 * Copyright (c) 2020 Bitdefender
 * SPDX-License-Identifier: Apache-2.0
 */
#include "include/bddisasm_crt.h"

//
// nd_strcat_s
//
char *
nd_strcat_s(
    char *dst,
    ND_SIZET dst_size,
    const char *src
)
{
    char *p;
    ND_SIZET available;

    p = dst;
    available = dst_size;
    while (available > 0 && *p != 0)
    {
        p++;
        available--;
    }

    if (available == 0)
    {
        nd_memzero(dst, dst_size);
        return (char *)ND_NULL;
    }

    while ((*p++ = *src++) != 0 && --available > 0);

    if (available == 0)
    {
        nd_memzero(dst, dst_size);
        return (char *)ND_NULL;
    }

    return dst;
}

#if !defined(BDDISASM_NO_FORMAT) && defined(BDDISASM_HAS_VSNPRINTF)
#include <stdio.h>

int nd_vsnprintf_s(char *buffer, ND_SIZET sizeOfBuffer, ND_SIZET count, const char *format, va_list argptr)
{
    UNREFERENCED_PARAMETER(count);
    return vsnprintf(buffer, sizeOfBuffer, format, argptr);
}
#endif // !defined(BDDISASM_NO_FORMAT) && defined(BDDISASM_HAS_VSNPRINTF)

#if defined(BDDISASM_HAS_MEMSET)
#include <string.h>

void *nd_memset(void *s, int c, ND_SIZET n)
{
    return memset(s, c, n);
}
#endif // defined(BDDISASM_HAS_MEMSET)
