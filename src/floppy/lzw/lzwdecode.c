/***************************************************************************
*                    Lempel-Ziv-Welch Decoding Functions
*
*   File    : lzwdecode.c
*   Purpose : Provides a function for decoding Lempel-Ziv-Welch encoded
*             file streams
*   Author  : Michael Dipperstein
*   Date    : January 30, 2005
*
****************************************************************************
*
* LZW: An ANSI C Lempel-Ziv-Welch Encoding/Decoding Routines
* Copyright (C) 2005, 2007, 2014, 2017 by
* Michael Dipperstein (mdipperstein@gmail.com)
*
* This file is part of the lzw library.
*
* The lzw library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public License as
* published by the Free Software Foundation; either version 3 of the
* License, or (at your option) any later version.
*
* The lzw library is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
* General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
***************************************************************************/

/***************************************************************************
*                             INCLUDED FILES
***************************************************************************/
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "lzw.h"
#include "lzwlocal.h"

/***************************************************************************
*                            TYPE DEFINITIONS
***************************************************************************/
typedef struct
{
    uint8_t  suffixChar;    /* last char in encoded string */
    uint16_t prefixCode;    /* code for remaining chars in string */
} decode_dictionary_t;

/***************************************************************************
*                                CONSTANTS
***************************************************************************/

/***************************************************************************
*                                  MACROS
***************************************************************************/

/***************************************************************************
*                            GLOBAL VARIABLES
***************************************************************************/

/* dictionary of string the code word is the dictionary index */
static decode_dictionary_t dictionary[(MAX_CODES - FIRST_CODE)];

/***************************************************************************
*                               PROTOTYPES
***************************************************************************/
static uint8_t DecodeRecursive(unsigned int code, char **dest);

/* read encoded data */
static int GetCodeWord(char *src);

static uint16_t bufPos = 0x0000;
static uint16_t bufLen = 0x0000;

static uint32_t bufOutPos = 0x00000000;

/***************************************************************************
*                                FUNCTIONS
***************************************************************************/

/***************************************************************************
*   Function   : LZWDecodeFile
*   Description: This routine reads an input file 1 encoded string at a
*                time and decodes it using the LZW algorithm.
*   Parameters : fpIn - pointer to the open binary file to decode
*                fpOut - pointer to the open binary file to write decoded
*                       output
*   Effects    : fpIn is decoded using the LZW algorithm with CODE_LEN codes
*                and written to fpOut.  Neither file is closed after exit.
*   Returned   : 0 for success, -1 for failure.  errno will be set in the
*                event of a failure.
***************************************************************************/
int
LZWDecodeFile_Internal(char *dest, char *src)
{
    uint16_t nextCode;    /* value of next code */
    uint16_t lastCode;    /* last decoded code word */
    int      code;        /* code word to decode */
    uint8_t  c;           /* last decoded character */

    /* validate arguments */
    if (dest == NULL) {
        errno = ENOENT;
        return -1;
    }

    bufPos = 0x0000;
    bufOutPos = 0x00000000;

    /* initialize for decoding */
    nextCode = FIRST_CODE;  /* code for next (first) string */

    /* first code from file must be a character.  use it for initial values */
    lastCode = GetCodeWord(src);
    c = lastCode;
    *(dest++) = lastCode;
    bufOutPos++;

    /* decode rest of file */
    while ((int)(code = GetCodeWord(src)) != EOF) {
        if (code < nextCode) {
            /* we have a known code.  decode it */
            c = DecodeRecursive(code, &dest);
        } else {
            /***************************************************************
            * We got a code that's not in our dictionary.  This must be due
            * to the string + char + string + char + string exception.
            * Build the decoded string using the last character + the
            * string from the last code.
            ***************************************************************/
            unsigned char tmp;

            tmp = c;
            c = DecodeRecursive(lastCode, &dest);
            *(dest++) = tmp;
            bufOutPos++;
        }

        /* if room, add new code to the dictionary */
        if (nextCode < MAX_CODES) {
            dictionary[nextCode - FIRST_CODE].prefixCode = lastCode;
            dictionary[nextCode - FIRST_CODE].suffixChar = c;
            nextCode++;
        }

        /* save character and code for use in unknown code word case */
        lastCode = code;
    }

    return 0;
}

int
LZWDecodeFile(char *dest, char *src, uint64_t *dst_len, uint64_t src_len)
{
    uint16_t size = 0x0000;
    uint64_t pos  = 0x0000000000000000ULL;

    /* validate arguments */
    if ((dest == NULL) || (src == NULL)) {
        errno = ENOENT;
        return -1;
    }

    if (dst_len != NULL)
        *dst_len = 0x0000000000000000ULL;

    while (1) {
        size = *(uint16_t *) src;
        src += 2;
        bufLen = size;
        size >>= 1;
        if (bufLen & 1)
            size++;
        if (size > 0x1800)
            return -1;
        LZWDecodeFile_Internal(dest, src);
        src += size;
        dest += bufOutPos;
        if (dst_len != NULL)
            *dst_len += bufOutPos;
        pos += (size + 2);
        if ((size < 0x1800) || (pos >= src_len))
            /* We have just decoded a block smaller than 0x3000 bytes,
               this means this has been the last block, end. */
            break;
    }

    return 0;
}

/***************************************************************************
*   Function   : DecodeRecursive
*   Description: This function uses the dictionary to decode a code word
*                into the string it represents and write it to the output
*                file.  The string is actually built in reverse order and
*                recursion is used to write it out in the correct order.
*   Parameters : code - the code word to decode
*                fpOut - the file that the decoded code word is written to
*   Effects    : Decoded code word is written to a file
*   Returned   : The first character in the decoded string
***************************************************************************/
static uint8_t
DecodeRecursive(unsigned int code, char **dest)
{
    unsigned char c;
    unsigned char firstChar;

    if (code >= FIRST_CODE) {
        /* code word is string + c */
        c = dictionary[code - FIRST_CODE].suffixChar;
        code = dictionary[code - FIRST_CODE].prefixCode;

        /* evaluate new code word for remaining string */
        firstChar = DecodeRecursive(code, dest);
    } else {
        /* code word is just c */
        c = code;
        firstChar = code;
    }

    *((*dest)++) = c;
    bufOutPos++;
    return firstChar;
}

/***************************************************************************
*   Function   : GetCodeWord
*   Description: This function reads and returns a code word from an
*                encoded file.  In order to deal with endian issue the
*                code word is read least significant byte followed by the
*                remaining bits.
*   Parameters : fpIn - file containing the encoded data
*                codeLen - number of bits in code word
*   Effects    : code word is read from encoded input
*   Returned   : The next code word in the encoded file.  EOF if the end
*                of file has been reached.
*
*   NOTE: If the code word contains more than 16 bits, this routine should
*         be modified to read in all the bytes from least significant to
*         most significant followed by any left over bits.
***************************************************************************/
static int
GetCodeWord(char *src)
{
    int code = 0;
    static unsigned int realPos;

    realPos = bufPos >> 1;

    if (bufPos >= bufLen)
        /* End of buffer. */
        code = EOF;
    else if (bufPos & 1)
        /* Odd position. */
        code = (((uint8_t) src[realPos] & 0xf0) >> 4) | ((uint8_t) src[realPos + 1] << 4);
    else
        /* Even position. */
        code = ((uint8_t) src[realPos] & 0xff) | (((uint8_t) src[realPos + 1] & 0xf) << 8);

    bufPos += 3;

    return code;
}
