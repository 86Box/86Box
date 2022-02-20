#ifndef LIBXML2_ENCODING_H
#define LIBXML2_ENCODING_H

#include <stdint.h>
typedef uint16_t mvhd_utf16;

void xmlEncodingInit(void);
int UTF16LEToUTF8(unsigned char* out, int *outlen, const unsigned char* inb, int *inlenb);
int UTF8ToUTF16LE(unsigned char* outb, int *outlen, const unsigned char* in, int *inlen);
int UTF16BEToUTF8(unsigned char* out, int *outlen, const unsigned char* inb, int *inlenb);
int UTF8ToUTF16BE(unsigned char* outb, int *outlen, const unsigned char* in, int *inlen);
#endif
