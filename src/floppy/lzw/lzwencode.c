/***************************************************************************
*                    Lempel-Ziv-Welch Encoding Functions
*
*   File    : lzwencode.c
*   Purpose : Provides a function for Lempel-Ziv-Welch encoding of file
*             streams
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
/* node in dictionary tree */
typedef struct dict_node_t
{
    unsigned int codeWord;      /* code word for this entry */
    unsigned char suffixChar;   /* last char in encoded string */
    unsigned int prefixCode;    /* code for remaining chars in string */

    /* pointer to child nodes */
    struct dict_node_t *left;   /* child with < key */
    struct dict_node_t *right;  /* child with >= key */
} dict_node_t;

/***************************************************************************
*                                CONSTANTS
***************************************************************************/

/***************************************************************************
*                                  MACROS
***************************************************************************/

/***************************************************************************
*                            GLOBAL VARIABLES
***************************************************************************/

/***************************************************************************
*                               PROTOTYPES
***************************************************************************/

/* dictionary tree node create/free */
static dict_node_t *MakeNode(const unsigned int codeWord, const unsigned int prefixCode,
                             const unsigned char suffixChar);
static void FreeTree(dict_node_t *node);

/* searches tree for matching dictionary entry */
static dict_node_t *FindDictionaryEntry(dict_node_t *root, const int unsigned prefixCode,
                                        const unsigned char c);

/* makes key from prefix code and character */
static unsigned int MakeKey(const unsigned int prefixCode, const unsigned char suffixChar);

/* write encoded data */
static int PutCodeWord(char *dest, int code);

static char    *src_base;
static uint64_t src_length = 0x0000000000000000ULL;

static uint32_t bufPos = 0x00000000;
static uint32_t bufInPos = 0x00000000;

static int
is_eob(char *src)
{
    return ((uint64_t) (uintptr_t) (src - src_base)) >= src_length;
}

static int
get_char(char **src)
{
    int ret = EOF;

    if (!is_eob(*src)) {
        ret = (uint8_t) **src;
        (*src)++;
    }

    return ret;
}

/***************************************************************************
*                                FUNCTIONS
***************************************************************************/

static int
LZWEncodeFile_Internal(char *dest, char *src)
{
    unsigned int code;                  /* code for current string */
    unsigned int nextCode;              /* next available code index */
    int c;                              /* character to add to string */

    dict_node_t *dictRoot;              /* root of dictionary tree */
    dict_node_t *node;                  /* node of dictionary tree */

    /* validate arguments */
    if (src == NULL) {
        errno = ENOENT;
        return -1;
    }

    /* initialize dictionary as empty */
    dictRoot = NULL;

    nextCode = FIRST_CODE;  /* code for next (first) string */

    bufPos = 0x00000000;
    bufInPos = 0x00000000;

    /* now start the actual encoding process */

    c = get_char(&src);

    if (c == EOF)
        return -1;      /* empty file */
    else {
        bufInPos++;
        code = c;       /* start with code string = first character */
    }

    /* create a tree root from 1st 2 character string */
    if ((c = get_char(&src)) != EOF) {
        bufInPos++;

        /* special case for NULL root */
        dictRoot = MakeNode(nextCode, code, c);

        if (dictRoot == NULL) {
            perror("Making Dictionary Root");
            return -1;
        }
        
        nextCode++;

        /* write code for 1st char */
        (void) PutCodeWord(dest, code);

        /* new code is just 2nd char */
        code = c;
    }

    /* now encode normally */
    while ((c = get_char(&src)) != EOF) {
        /* look for code + c in the dictionary */
        node = FindDictionaryEntry(dictRoot, code, c);

        if ((node->prefixCode == code) && (node->suffixChar == c))
            /* code + c is in the dictionary, make it's code the new code */
            code = node->codeWord;
        else {
            /* code + c is not in the dictionary, add it if there's room */
            if (nextCode < MAX_CODES) {
                dict_node_t *tmp = MakeNode(nextCode, code, c);

                if (tmp == NULL) {
                    perror("Making Dictionary Node");
                    FreeTree(dictRoot);
                    return -1;
                }

                nextCode++;

                if (MakeKey(code, c) < MakeKey(node->prefixCode, node->suffixChar))
                    node->left = tmp;
                else
                    node->right = tmp;
            }

            /* write out code for the string before c was added */
            if (PutCodeWord(dest, code))
                break;

            /* new code is just c */
            code = c;
        }

        bufInPos++;
    }

    /* no more input.  write out last of the code. */
    (void) PutCodeWord(dest, code);

    /* free the dictionary */
    FreeTree(dictRoot);

    return (c == EOF) ? 1 : 0;
}

/***************************************************************************
*   Function   : LZWEncodeFile
*   Description: This routine reads an input file 1 character at a time and
*                writes out an LZW encoded version of that file.
*   Parameters : fpIn - pointer to the open binary file to encode
*                fpOut - pointer to the open binary file to write encoded
*                       output
*   Effects    : fpIn is encoded using the LZW algorithm with CODE_LEN codes
*                and written to fpOut.  Neither file is closed after exit.
*   Returned   : 0 for success, -1 for failure.  errno will be set in the
*                event of a failure.
***************************************************************************/
int
LZWEncodeFile(char *dest, char *src, uint64_t *dst_len, uint64_t src_len)
{
    uint64_t pos  = 0x0000000000000000ULL;

    /* validate arguments */
    if ((dest == NULL) || (src == NULL)) {
        errno = ENOENT;
        return -1;
    }

    if (dst_len != NULL)
        *dst_len = 0x0000000000000000ULL;

    src_base = src;
    src_length = src_len;

    while (1) {
        int ret = LZWEncodeFile_Internal(dest + 2, src);
        if (ret == -1)
            break;
        *(uint16_t *) dest = bufPos;
        if (bufPos & 1)
            bufPos = (bufPos >> 1) + 1;
        else
            bufPos >>= 1;
        dest += (bufPos + 2);
        if (dst_len != NULL)
            *dst_len += (bufPos + 2);
        /* TODO: Why do we need this - 1 clunkfest? */
        src += bufInPos;
        pos += bufInPos;
        if ((ret == 1) || (pos >= src_len) || (bufPos < 0x1800))
            break;
    }

    return 0;
}

/***************************************************************************
*   Function   : MakeKey
*   Description: This routine creates a simple key from a prefix code and
*                an appended character.  The key may be used to establish
*                an order when building/searching a dictionary tree.
*   Parameters : prefixCode - code for all but the last character of a
*                             string.
*                suffixChar - the last character of a string
*   Effects    : None
*   Returned   : Key built from string represented as a prefix + char.  Key
*                format is {ms nibble of c} + prefix + {ls nibble of c}
***************************************************************************/
static unsigned int
MakeKey(const unsigned int prefixCode, const unsigned char suffixChar)
{
    unsigned int key;

    /* position ms nibble */
    key = suffixChar & 0xF0;
    key <<= MAX_CODE_LEN;

    /* include prefix code */
    key |= (prefixCode << 4);

    /* inclulde ls nibble */
    key |= (suffixChar & 0x0F);

    return key;
}

/***************************************************************************
*   Function   : MakeNode
*   Description: This routine creates and initializes a dictionary entry
*                for a string and the code word that encodes it.
*   Parameters : codeWord - code word used to encode the string prefixCode +
*                           suffixChar
*                prefixCode - code for all but the last character of a
*                             string.
*                suffixChar - the last character of a string
*   Effects    : Node is allocated for new dictionary entry
*   Returned   : Pointer to newly allocated node or NULL on error.
*                errno will be set on an error.
***************************************************************************/
static dict_node_t *
MakeNode(const unsigned int codeWord, const unsigned int prefixCode, const unsigned char suffixChar)
{
    dict_node_t *node;

    node = malloc(sizeof(dict_node_t));

    if (node != NULL) {
        node->codeWord = codeWord;
        node->prefixCode = prefixCode;
        node->suffixChar = suffixChar;

        node->left = NULL;
        node->right = NULL;
    }

    return node;
}

/***************************************************************************
*   Function   : FreeTree
*   Description: This routine will free all nodes of a tree rooted at the
*                node past as a parameter.
*   Parameters : node - root of tree to free
*   Effects    : frees allocated tree node from initial parameter down.
*   Returned   : none
***************************************************************************/
static void
FreeTree(dict_node_t *node)
{
    if (node == NULL)
        /* nothing to free */
        return;

    /* free left branch */
    if (node->left != NULL)
        FreeTree(node->left);

    /* free right branch */
    if (node->right != NULL)
        FreeTree(node->right);

    /* free root */
    free(node);
}

/***************************************************************************
*   Function   : FindDictionaryEntry
*   Description: This routine searches the dictionary tree for an entry
*                with a matching string (prefix code + suffix character).
*                If one isn't found, the parent node for that string is
*                returned.
*   Parameters : prefixCode - code for the prefix of string
*                c - last character in string
*   Effects    : None
*   Returned   : If string is in dictionary, pointer to node containing
*                string, otherwise pointer to suitable parent node.  NULL
*                is returned for an empty tree.
***************************************************************************/
static dict_node_t *
FindDictionaryEntry(dict_node_t *root, const int unsigned prefixCode, const unsigned char c)
{
    unsigned int searchKey, key;

    if (root == NULL)
        return NULL;

    searchKey = MakeKey(prefixCode, c);     /* key of string to find */

    while (1) {
        /* key of current node */
        key = MakeKey(root->prefixCode, root->suffixChar);

        if (key == searchKey)
            /* current node contains string */
            return root;
        else if (searchKey < key) {
            if (root->left != NULL)
                /* check left branch for string */
                root = root->left;
            else
                /* string isn't in tree, it can be added as a left child */
                return root;
        } else {
            if (root->right != NULL)
                /* check right branch for string */
                root = root->right;
            else
                /* string isn't in tree, it can be added as a right child */
                return root;
        }
    }
}

/***************************************************************************
*   Function   : PutCodeWord
*   Description: This function writes a code word from to an encoded file.
*                In order to deal with endian issue the code word is
*                written least significant byte followed by the remaining
*                bits.
*   Parameters : bfpOut - bit file containing the encoded data
*                code - code word to add to the encoded data
*                codeLen - length of the code word
*   Effects    : code word is written to the encoded output
*   Returned   : EOF for failure, ENOTSUP unsupported architecture,
*                otherwise the number of bits written.  If an error occurs
*                after a partial write, the partially written bits will not
*                be unwritten.
***************************************************************************/
static int
PutCodeWord(char *dest, int code)
{
    static unsigned int realPos;
    int ret = 0;

    if (bufPos >= 0x3000)
        ret = -1;
    else {
        realPos = bufPos >> 1;

        if (bufPos & 1) {
            /* Odd position. */
            dest[realPos] = (dest[realPos] & 0x0f) | ((code << 4) & 0xf0);
            dest[realPos + 1] = (code >> 4) & 0xff;
        } else {
            /* Even position. */
            dest[realPos] = code & 0xff;
            dest[realPos + 1] = ((code >> 8) & 0x0f);
        }

        bufPos += 3;

        if (bufPos >= 0x3000)
            ret = 1;
    }

    return ret;
}
