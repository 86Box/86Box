/*
 * encoding.c : implements the encoding conversion functions needed for XML
 *
 * Related specs:
 * rfc2044        (UTF-8 and UTF-16) F. Yergeau Alis Technologies
 * rfc2781        UTF-16, an encoding of ISO 10646, P. Hoffman, F. Yergeau
 * [ISO-10646]    UTF-8 and UTF-16 in Annexes
 * [ISO-8859-1]   ISO Latin-1 characters codes.
 * [UNICODE]      The Unicode Consortium, "The Unicode Standard --
 *                Worldwide Character Encoding -- Version 1.0", Addison-
 *                Wesley, Volume 1, 1991, Volume 2, 1992.  UTF-8 is
 *                described in Unicode Technical Report #4.
 * [US-ASCII]     Coded Character Set--7-bit American Standard Code for
 *                Information Interchange, ANSI X3.4-1986.
 *
 * See Copyright for the status of this software.
 *
 * daniel@veillard.com
 *
 * Original code for IsoLatin1 and UTF-16 by "Martin J. Duerst" <duerst@w3.org>
 *
 * Adapted and abridged for MiniVHD by Sherman Perry
 */
#include <stdlib.h>

static int xmlLittleEndian = 1;

/* Note: extracted from original 'void xmlInitCharEncodingHandlers(void)' function */
void xmlEncodingInit(void)
{
    unsigned short int tst = 0x1234;
    unsigned char *ptr = (unsigned char *) &tst;

    if (*ptr == 0x12) xmlLittleEndian = 0;
    else if (*ptr == 0x34) xmlLittleEndian = 1;
}

/**
 * UTF16LEToUTF8:
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @inb:  a pointer to an array of UTF-16LE passwd as a byte array
 * @inlenb:  the length of @in in UTF-16LE chars
 *
 * Take a block of UTF-16LE ushorts in and try to convert it to an UTF-8
 * block of chars out. This function assumes the endian property
 * is the same between the native type of this machine and the
 * inputed one.
 *
 * Returns the number of bytes written, or -1 if lack of space, or -2
 *     if the transcoding fails (if *in is not a valid utf16 string)
 *     The value of *inlen after return is the number of octets consumed
 *     if the return value is positive, else unpredictable.
 */
int UTF16LEToUTF8(unsigned char* out, int *outlen,
            const unsigned char* inb, int *inlenb)
{
    unsigned char* outstart = out;
    const unsigned char* processed = inb;
    unsigned char* outend = out + *outlen;
    unsigned short* in = (unsigned short*) inb;
    unsigned short* inend;
    unsigned int c, d, inlen;
    unsigned char *tmp;
    int bits;

    if ((*inlenb % 2) == 1)
        (*inlenb)--;
    inlen = *inlenb / 2;
    inend = in + inlen;
    while ((in < inend) && (out - outstart + 5 < *outlen)) {
        if (xmlLittleEndian) {
	    c= *in++;
	} else {
	    tmp = (unsigned char *) in;
	    c = *tmp++;
	    c = c | (((unsigned int)*tmp) << 8);
	    in++;
	}
        if ((c & 0xFC00) == 0xD800) {    /* surrogates */
	    if (in >= inend) {           /* (in > inend) shouldn't happens */
		break;
	    }
	    if (xmlLittleEndian) {
		d = *in++;
	    } else {
		tmp = (unsigned char *) in;
		d = *tmp++;
		d = d | (((unsigned int)*tmp) << 8);
		in++;
	    }
            if ((d & 0xFC00) == 0xDC00) {
                c &= 0x03FF;
                c <<= 10;
                c |= d & 0x03FF;
                c += 0x10000;
            }
            else {
		*outlen = out - outstart;
		*inlenb = processed - inb;
	        return(-2);
	    }
        }

	/* assertion: c is a single UTF-4 value */
        if (out >= outend)
	    break;
        if      (c <    0x80) {  *out++=  c;                bits= -6; }
        else if (c <   0x800) {  *out++= ((c >>  6) & 0x1F) | 0xC0;  bits=  0; }
        else if (c < 0x10000) {  *out++= ((c >> 12) & 0x0F) | 0xE0;  bits=  6; }
        else                  {  *out++= ((c >> 18) & 0x07) | 0xF0;  bits= 12; }

        for ( ; bits >= 0; bits-= 6) {
            if (out >= outend)
	        break;
            *out++= ((c >> bits) & 0x3F) | 0x80;
        }
	processed = (const unsigned char*) in;
    }
    *outlen = out - outstart;
    *inlenb = processed - inb;
    return(*outlen);
}

/**
 * UTF8ToUTF16LE:
 * @outb:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @outb
 * @in:  a pointer to an array of UTF-8 chars
 * @inlen:  the length of @in
 *
 * Take a block of UTF-8 chars in and try to convert it to an UTF-16LE
 * block of chars out.
 *
 * Returns the number of bytes written, or -1 if lack of space, or -2
 *     if the transcoding failed.
 */
int UTF8ToUTF16LE(unsigned char* outb, int *outlen,
            const unsigned char* in, int *inlen)
{
    unsigned short* out = (unsigned short*) outb;
    const unsigned char* processed = in;
    const unsigned char *const instart = in;
    unsigned short* outstart= out;
    unsigned short* outend;
    const unsigned char* inend;
    unsigned int c, d;
    int trailing;
    unsigned char *tmp;
    unsigned short tmp1, tmp2;

    /* UTF16LE encoding has no BOM */
    if ((out == NULL) || (outlen == NULL) || (inlen == NULL)) return(-1);
    if (in == NULL) {
	*outlen = 0;
	*inlen = 0;
	return(0);
    }
    inend= in + *inlen;
    outend = out + (*outlen / 2);
    while (in < inend) {
      d= *in++;
      if      (d < 0x80)  { c= d; trailing= 0; }
      else if (d < 0xC0) {
          /* trailing byte in leading position */
	  *outlen = (out - outstart) * 2;
	  *inlen = processed - instart;
	  return(-2);
      } else if (d < 0xE0)  { c= d & 0x1F; trailing= 1; }
      else if (d < 0xF0)  { c= d & 0x0F; trailing= 2; }
      else if (d < 0xF8)  { c= d & 0x07; trailing= 3; }
      else {
	/* no chance for this in UTF-16 */
	*outlen = (out - outstart) * 2;
	*inlen = processed - instart;
	return(-2);
      }

      if (inend - in < trailing) {
          break;
      }

      for ( ; trailing; trailing--) {
          if ((in >= inend) || (((d= *in++) & 0xC0) != 0x80))
	      break;
          c <<= 6;
          c |= d & 0x3F;
      }

      /* assertion: c is a single UTF-4 value */
        if (c < 0x10000) {
            if (out >= outend)
	        break;
	    if (xmlLittleEndian) {
		*out++ = c;
	    } else {
		tmp = (unsigned char *) out;
		*tmp = c ;
		*(tmp + 1) = c >> 8 ;
		out++;
	    }
        }
        else if (c < 0x110000) {
            if (out+1 >= outend)
	        break;
            c -= 0x10000;
	    if (xmlLittleEndian) {
		*out++ = 0xD800 | (c >> 10);
		*out++ = 0xDC00 | (c & 0x03FF);
	    } else {
		tmp1 = 0xD800 | (c >> 10);
		tmp = (unsigned char *) out;
		*tmp = (unsigned char) tmp1;
		*(tmp + 1) = tmp1 >> 8;
		out++;

		tmp2 = 0xDC00 | (c & 0x03FF);
		tmp = (unsigned char *) out;
		*tmp  = (unsigned char) tmp2;
		*(tmp + 1) = tmp2 >> 8;
		out++;
	    }
        }
        else
	    break;
	processed = in;
    }
    *outlen = (out - outstart) * 2;
    *inlen = processed - instart;
    return(*outlen);
}

/**
 * UTF16BEToUTF8:
 * @out:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @out
 * @inb:  a pointer to an array of UTF-16 passed as a byte array
 * @inlenb:  the length of @in in UTF-16 chars
 *
 * Take a block of UTF-16 ushorts in and try to convert it to an UTF-8
 * block of chars out. This function assumes the endian property
 * is the same between the native type of this machine and the
 * inputed one.
 *
 * Returns the number of bytes written, or -1 if lack of space, or -2
 *     if the transcoding fails (if *in is not a valid utf16 string)
 * The value of *inlen after return is the number of octets consumed
 *     if the return value is positive, else unpredictable.
 */
int UTF16BEToUTF8(unsigned char* out, int *outlen,
            const unsigned char* inb, int *inlenb)
{
    unsigned char* outstart = out;
    const unsigned char* processed = inb;
    unsigned char* outend = out + *outlen;
    unsigned short* in = (unsigned short*) inb;
    unsigned short* inend;
    unsigned int c, d, inlen;
    unsigned char *tmp;
    int bits;

    if ((*inlenb % 2) == 1)
        (*inlenb)--;
    inlen = *inlenb / 2;
    inend= in + inlen;
    while (in < inend) {
	if (xmlLittleEndian) {
	    tmp = (unsigned char *) in;
	    c = *tmp++;
	    c = c << 8;
	    c = c | (unsigned int) *tmp;
	    in++;
	} else {
	    c= *in++;
	}
        if ((c & 0xFC00) == 0xD800) {    /* surrogates */
	    if (in >= inend) {           /* (in > inend) shouldn't happens */
		*outlen = out - outstart;
		*inlenb = processed - inb;
	        return(-2);
	    }
	    if (xmlLittleEndian) {
		tmp = (unsigned char *) in;
		d = *tmp++;
		d = d << 8;
		d = d | (unsigned int) *tmp;
		in++;
	    } else {
		d= *in++;
	    }
            if ((d & 0xFC00) == 0xDC00) {
                c &= 0x03FF;
                c <<= 10;
                c |= d & 0x03FF;
                c += 0x10000;
            }
            else {
		*outlen = out - outstart;
		*inlenb = processed - inb;
	        return(-2);
	    }
        }

	/* assertion: c is a single UTF-4 value */
        if (out >= outend)
	    break;
        if      (c <    0x80) {  *out++=  c;                bits= -6; }
        else if (c <   0x800) {  *out++= ((c >>  6) & 0x1F) | 0xC0;  bits=  0; }
        else if (c < 0x10000) {  *out++= ((c >> 12) & 0x0F) | 0xE0;  bits=  6; }
        else                  {  *out++= ((c >> 18) & 0x07) | 0xF0;  bits= 12; }

        for ( ; bits >= 0; bits-= 6) {
            if (out >= outend)
	        break;
            *out++= ((c >> bits) & 0x3F) | 0x80;
        }
	processed = (const unsigned char*) in;
    }
    *outlen = out - outstart;
    *inlenb = processed - inb;
    return(*outlen);
}

/**
 * UTF8ToUTF16BE:
 * @outb:  a pointer to an array of bytes to store the result
 * @outlen:  the length of @outb
 * @in:  a pointer to an array of UTF-8 chars
 * @inlen:  the length of @in
 *
 * Take a block of UTF-8 chars in and try to convert it to an UTF-16BE
 * block of chars out.
 *
 * Returns the number of byte written, or -1 by lack of space, or -2
 *     if the transcoding failed.
 */
int UTF8ToUTF16BE(unsigned char* outb, int *outlen,
            const unsigned char* in, int *inlen)
{
    unsigned short* out = (unsigned short*) outb;
    const unsigned char* processed = in;
    const unsigned char *const instart = in;
    unsigned short* outstart= out;
    unsigned short* outend;
    const unsigned char* inend;
    unsigned int c, d;
    int trailing;
    unsigned char *tmp;
    unsigned short tmp1, tmp2;

    /* UTF-16BE has no BOM */
    if ((outb == NULL) || (outlen == NULL) || (inlen == NULL)) return(-1);
    if (in == NULL) {
	*outlen = 0;
	*inlen = 0;
	return(0);
    }
    inend= in + *inlen;
    outend = out + (*outlen / 2);
    while (in < inend) {
      d= *in++;
      if      (d < 0x80)  { c= d; trailing= 0; }
      else if (d < 0xC0)  {
          /* trailing byte in leading position */
	  *outlen = out - outstart;
	  *inlen = processed - instart;
	  return(-2);
      } else if (d < 0xE0)  { c= d & 0x1F; trailing= 1; }
      else if (d < 0xF0)  { c= d & 0x0F; trailing= 2; }
      else if (d < 0xF8)  { c= d & 0x07; trailing= 3; }
      else {
          /* no chance for this in UTF-16 */
	  *outlen = out - outstart;
	  *inlen = processed - instart;
	  return(-2);
      }

      if (inend - in < trailing) {
          break;
      }

      for ( ; trailing; trailing--) {
          if ((in >= inend) || (((d= *in++) & 0xC0) != 0x80))  break;
          c <<= 6;
          c |= d & 0x3F;
      }

      /* assertion: c is a single UTF-4 value */
        if (c < 0x10000) {
            if (out >= outend)  break;
	    if (xmlLittleEndian) {
		tmp = (unsigned char *) out;
		*tmp = c >> 8;
		*(tmp + 1) = c;
		out++;
	    } else {
		*out++ = c;
	    }
        }
        else if (c < 0x110000) {
            if (out+1 >= outend)  break;
            c -= 0x10000;
	    if (xmlLittleEndian) {
		tmp1 = 0xD800 | (c >> 10);
		tmp = (unsigned char *) out;
		*tmp = tmp1 >> 8;
		*(tmp + 1) = (unsigned char) tmp1;
		out++;

		tmp2 = 0xDC00 | (c & 0x03FF);
		tmp = (unsigned char *) out;
		*tmp = tmp2 >> 8;
		*(tmp + 1) = (unsigned char) tmp2;
		out++;
	    } else {
		*out++ = 0xD800 | (c >> 10);
		*out++ = 0xDC00 | (c & 0x03FF);
	    }
        }
        else
	    break;
	processed = in;
    }
    *outlen = (out - outstart) * 2;
    *inlen = processed - instart;
    return(*outlen);
}

/* This file is licenced under the MIT licence as follows:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is fur-
nished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE. */
