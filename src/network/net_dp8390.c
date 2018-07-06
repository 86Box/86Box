#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <time.h>
#define HAVE_STDARG_H

/*
 * Return the 6-bit index into the multicast
 * table. Stolen unashamedly from FreeBSD's if_ed.c
 */
int
mcast_index(const void *dst)
{
#define POLYNOMIAL 0x04c11db6
    uint32_t crc = 0xffffffffL;
    int carry, i, j;
    uint8_t b;
    uint8_t *ep = (uint8_t *)dst;

    for (i=6; --i>=0;) {
	b = *ep++;
	for (j = 8; --j >= 0;) {
		carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
		crc <<= 1;
		b >>= 1;
		if (carry)
			crc = ((crc ^ POLYNOMIAL) | carry);
	}
    }
    return(crc >> 26);
#undef POLYNOMIAL
}
