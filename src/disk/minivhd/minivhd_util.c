/*
 * MiniVHD	Minimalist VHD implementation in C.
 *
 *		This file is part of the MiniVHD Project.
 *
 *		Utility functions.
 *
 * Version:	@(#)util.c	1.0.4	2021/04/16
 *
 * Author:	Sherman Perry, <shermperry@gmail.com>
 *
 *		Copyright 2019-2021 Sherman Perry.
 *
 *		MIT License
 *
 *		Permission is hereby granted, free of  charge, to any person
 *		obtaining a copy of this software  and associated documenta-
 *		tion files (the "Software"), to deal in the Software without
 *		restriction, including without limitation the rights to use,
 *		copy, modify, merge, publish, distribute, sublicense, and/or
 *		sell copies of  the Software, and  to permit persons to whom
 *		the Software is furnished to do so, subject to the following
 *		conditions:
 *
 *		The above  copyright notice and this permission notice shall
 *		be included in  all copies or  substantial  portions of  the
 *		Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING  BUT NOT LIMITED TO THE  WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN  NO EVENT  SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER  IN AN ACTION OF  CONTRACT, TORT OR  OTHERWISE, ARISING
 * FROM, OUT OF  O R IN  CONNECTION WITH THE  SOFTWARE OR  THE USE  OR  OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef _FILE_OFFSET_BITS
# define _FILE_OFFSET_BITS 64
#endif
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "minivhd.h"
#include "internal.h"
#include "xml2_encoding.h"


uint16_t
mvhd_from_be16(uint16_t val)
{
    uint8_t *tmp = (uint8_t*)&val;
    uint16_t ret = 0;

    ret |= (uint16_t)tmp[0] << 8;
    ret |= (uint16_t)tmp[1] << 0;

    return ret;
}


uint32_t
mvhd_from_be32(uint32_t val)
{
    uint8_t *tmp = (uint8_t*)&val;
    uint32_t ret = 0;

    ret =  (uint32_t)tmp[0] << 24;
    ret |= (uint32_t)tmp[1] << 16;
    ret |= (uint32_t)tmp[2] << 8;
    ret |= (uint32_t)tmp[3] << 0;

    return ret;
}


uint64_t
mvhd_from_be64(uint64_t val)
{
    uint8_t *tmp = (uint8_t*)&val;
    uint64_t ret = 0;

    ret =  (uint64_t)tmp[0] << 56;
    ret |= (uint64_t)tmp[1] << 48;
    ret |= (uint64_t)tmp[2] << 40;
    ret |= (uint64_t)tmp[3] << 32;
    ret |= (uint64_t)tmp[4] << 24;
    ret |= (uint64_t)tmp[5] << 16;
    ret |= (uint64_t)tmp[6] << 8;
    ret |= (uint64_t)tmp[7] << 0;

    return ret;
}


uint16_t
mvhd_to_be16(uint16_t val)
{
    uint16_t ret = 0;
    uint8_t *tmp = (uint8_t*)&ret;

    tmp[0] = (val & 0xff00) >> 8;
    tmp[1] = (val & 0x00ff) >> 0;

    return ret;
}


uint32_t
mvhd_to_be32(uint32_t val)
{
    uint32_t ret = 0;
    uint8_t *tmp = (uint8_t*)&ret;

    tmp[0] = (val & 0xff000000) >> 24;
    tmp[1] = (val & 0x00ff0000) >> 16;
    tmp[2] = (val & 0x0000ff00) >> 8;
    tmp[3] = (val & 0x000000ff) >> 0;

    return ret;
}


uint64_t
mvhd_to_be64(uint64_t val)
{
    uint64_t ret = 0;
    uint8_t *tmp = (uint8_t*)&ret;

    tmp[0] = (uint8_t)((val & 0xff00000000000000) >> 56);
    tmp[1] = (uint8_t)((val & 0x00ff000000000000) >> 48);
    tmp[2] = (uint8_t)((val & 0x0000ff0000000000) >> 40);
    tmp[3] = (uint8_t)((val & 0x000000ff00000000) >> 32);
    tmp[4] = (uint8_t)((val & 0x00000000ff000000) >> 24);
    tmp[5] = (uint8_t)((val & 0x0000000000ff0000) >> 16);
    tmp[6] = (uint8_t)((val & 0x000000000000ff00) >> 8);
    tmp[7] = (uint8_t)((val & 0x00000000000000ff) >> 0);

    return ret;
}


void
mvhd_generate_uuid(uint8_t* uuid)
{
    /* We aren't doing crypto here, so using system time as seed should be good enough */
    srand((unsigned int)time(0));

    for (int n = 0; n < 16; n++) {
        uuid[n] = rand();
    }
    uuid[6] &= 0x0F;
    uuid[6] |= 0x40; /* Type 4 */
    uuid[8] &= 0x3F;
    uuid[8] |= 0x80; /* Variant 1 */
}


uint32_t
vhd_calc_timestamp(void)
{
    time_t start_time;
    time_t curr_time;
    double vhd_time;

    start_time = MVHD_START_TS; /* 1 Jan 2000 00:00 */
    curr_time = time(NULL);
    vhd_time = difftime(curr_time, start_time);

    return (uint32_t)vhd_time;
}


uint32_t
mvhd_epoch_to_vhd_ts(time_t ts)
{
    time_t start_time = MVHD_START_TS;
    double vhd_time;

    if (ts < start_time)
        return (uint32_t)start_time;

    vhd_time = difftime(ts, start_time);

    return (uint32_t)vhd_time;
}


time_t
vhd_get_created_time(MVHDMeta *vhdm)
{
    time_t vhd_time = (time_t)vhdm->footer.timestamp;
    time_t vhd_time_unix = MVHD_START_TS + vhd_time;

    return vhd_time_unix;
}


FILE*
mvhd_fopen(const char* path, const char* mode, int* err)
{
    FILE* f = NULL;
#ifdef _WIN32
    size_t path_len = strlen(path);
    size_t mode_len = strlen(mode);
    mvhd_utf16 new_path[260] = {0};
    int new_path_len = (sizeof new_path) - 2;
    mvhd_utf16 mode_str[5] = {0};
    int new_mode_len = (sizeof mode_str) - 2;
    int path_res = UTF8ToUTF16LE((unsigned char*)new_path, &new_path_len, (const unsigned char*)path, (int*)&path_len);
    int mode_res = UTF8ToUTF16LE((unsigned char*)mode_str, &new_mode_len, (const unsigned char*)mode, (int*)&mode_len);

    if (path_res > 0 && mode_res > 0) {
        f = _wfopen(new_path, mode_str);
        if (f == NULL) {
            mvhd_errno = errno;
            *err = MVHD_ERR_FILE;
        }
    } else {
        if (path_res == -1 || mode_res == -1) {
            *err = MVHD_ERR_UTF_SIZE;
        } else if (path_res == -2 || mode_res == -2) {
            *err = MVHD_ERR_UTF_TRANSCODING_FAILED;
        }
    }
#else
    f = fopen(path, mode);
    if (f == NULL) {
        mvhd_errno = errno;
        *err = MVHD_ERR_FILE;
    }
#endif

    return f;
}


void
mvhd_set_encoding_err(int encoding_retval, int* err)
{
    if (encoding_retval == -1) {
        *err = MVHD_ERR_UTF_SIZE;
    } else if (encoding_retval == -2) {
        *err = MVHD_ERR_UTF_TRANSCODING_FAILED;
    }
}


uint64_t
mvhd_calc_size_bytes(MVHDGeom *geom)
{
    uint64_t img_size = (uint64_t)geom->cyl * (uint64_t)geom->heads * (uint64_t)geom->spt * (uint64_t)MVHD_SECTOR_SIZE;

    return img_size;
}


uint32_t
mvhd_calc_size_sectors(MVHDGeom *geom)
{
    uint32_t sector_size = (uint32_t)geom->cyl * (uint32_t)geom->heads * (uint32_t)geom->spt;

    return sector_size;
}


MVHDAPI MVHDGeom
mvhd_get_geometry(MVHDMeta* vhdm)
{
    MVHDGeom geometry = {
        .cyl = vhdm->footer.geom.cyl,
        .heads = vhdm->footer.geom.heads,
        .spt = vhdm->footer.geom.spt
    };

    return geometry;
}


MVHDAPI uint64_t
mvhd_get_current_size(MVHDMeta* vhdm)
{
    return vhdm->footer.curr_sz;
}


uint32_t
mvhd_gen_footer_checksum(MVHDFooter* footer)
{
    uint32_t new_chk = 0;
    uint32_t orig_chk = footer->checksum;
    footer->checksum = 0;
    uint8_t* footer_bytes = (uint8_t*)footer;

    for (size_t i = 0; i < sizeof *footer; i++)
        new_chk += footer_bytes[i];
    footer->checksum = orig_chk;

    return ~new_chk;
}


uint32_t
mvhd_gen_sparse_checksum(MVHDSparseHeader* header)
{
    uint32_t new_chk = 0;
    uint32_t orig_chk = header->checksum;
    header->checksum = 0;
    uint8_t* sparse_bytes = (uint8_t*)header;

    for (size_t i = 0; i < sizeof *header; i++) {
        new_chk += sparse_bytes[i];
    }
    header->checksum = orig_chk;

    return ~new_chk;
}


MVHDAPI const char*
mvhd_strerr(MVHDError err)
{
    const char *s = "unknown error";

    switch (err) {
        case MVHD_ERR_MEM:
            s = "memory allocation error";
            break;

        case MVHD_ERR_FILE:
            s = "file error";
            break;

        case MVHD_ERR_NOT_VHD:
            s = "file is not a VHD image";
            break;

        case MVHD_ERR_TYPE:
            s = "unsupported VHD image type";
            break;

        case MVHD_ERR_FOOTER_CHECKSUM:
            s = "invalid VHD footer checksum";
            break;

        case MVHD_ERR_SPARSE_CHECKSUM:
            s = "invalid VHD sparse header checksum";
            break;

        case MVHD_ERR_UTF_TRANSCODING_FAILED:
            s = "error converting path encoding";
            break;

        case MVHD_ERR_UTF_SIZE:
            s = "buffer size mismatch when converting path encoding";
            break;

        case MVHD_ERR_PATH_REL:
            s = "relative path detected where absolute path expected";
            break;

        case MVHD_ERR_PATH_LEN:
            s = "path length exceeds MVHD_MAX_PATH";
            break;

        case MVHD_ERR_PAR_NOT_FOUND:
            s = "parent VHD image not found";
            break;

        case MVHD_ERR_INVALID_PAR_UUID:
            s = "UUID mismatch between child and parent VHD";
            break;

        case MVHD_ERR_INVALID_GEOM:
            s = "invalid geometry detected";
            break;

        case MVHD_ERR_INVALID_SIZE:
            s = "invalid size";
            break;

        case MVHD_ERR_INVALID_BLOCK_SIZE:
            s = "invalid block size";
            break;

        case MVHD_ERR_INVALID_PARAMS:
            s = "invalid parameters passed to function";
            break;

        case MVHD_ERR_CONV_SIZE:
            s = "error converting image. Size mismatch detected";
            break;

        default:
            break;
    }

    return s;
}


int64_t
mvhd_ftello64(FILE* stream)
{
#ifdef _MSC_VER
    return _ftelli64(stream);
#elif defined(__MINGW32__)
    return ftello64(stream);
#else /* This should work with linux (with _FILE_OFFSET_BITS), and hopefully OS X and BSD */
    return ftello(stream);
#endif
}


int
mvhd_fseeko64(FILE* stream, int64_t offset, int origin)
{
#ifdef _MSC_VER
    return _fseeki64(stream, offset, origin);
#elif defined(__MINGW32__)
    return fseeko64(stream, offset, origin);
#else /* This should work with linux (with _FILE_OFFSET_BITS), and hopefully OS X and BSD */
    return fseeko(stream, offset, origin);
#endif
}


uint32_t
mvhd_crc32_for_byte(uint32_t r)
{
    for (int j = 0; j < 8; ++j)
        r = (r & 1 ? 0 : (uint32_t)0xEDB88320L) ^ r >> 1;

    return r ^ (uint32_t)0xFF000000L;
}


uint32_t
mvhd_crc32(const void* data, size_t n_bytes)
{
    static uint32_t table[0x100];

    if (!*table)
        for (size_t i = 0; i < 0x100; ++i)
            table[i] = mvhd_crc32_for_byte((uint32_t)i);

    uint32_t crc = 0;
    for (size_t i = 0; i < n_bytes; ++i)
        crc = table[(uint8_t)crc ^ ((uint8_t*)data)[i]] ^ crc >> 8;

    return crc;
}


uint32_t
mvhd_file_mod_timestamp(const char* path, int *err)
{
    *err = 0;
#ifdef _WIN32
    struct _stat file_stat = { 0 };
    size_t path_len = strlen(path);
    mvhd_utf16 new_path[260] = {0};
    int new_path_len = (sizeof new_path) - 2;
    int path_res = UTF8ToUTF16LE((unsigned char*)new_path, &new_path_len, (const unsigned char*)path, (int*)&path_len);

    if (path_res > 0) {
        int stat_res = _wstat(new_path, &file_stat);
        if (stat_res != 0) {
            mvhd_errno = errno;
            *err = MVHD_ERR_FILE;
            return 0;
        }
        return mvhd_epoch_to_vhd_ts(file_stat.st_mtime);
    } else {
        if (path_res == -1) {
            *err = MVHD_ERR_UTF_SIZE;
        } else if (path_res == -2) {
            *err = MVHD_ERR_UTF_TRANSCODING_FAILED;
        }
        return 0;
    }
#else
    struct stat file_stat = { 0 };
    int stat_res = stat(path, &file_stat);

    if (stat_res != 0) {
            mvhd_errno = errno;
            *err = MVHD_ERR_FILE;
            return 0;
        }
    return mvhd_epoch_to_vhd_ts(file_stat.st_mtime);
#endif
}
