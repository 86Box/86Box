/*
 * MiniVHD	Minimalist VHD implementation in C.
 *
 *		This file is part of the MiniVHD Project.
 *
 * Version:	@(#)convert.c	1.0.2	2021/04/16
 *
 * Authors:	Sherman Perry, <shermperry@gmail.com>
 *		Fred N. van Kempen, <waltje@varcem.com>
 *
 *		Copyright 2019-2021 Sherman Perry.
 *		Copyright 2021 Fred N. van Kempen.
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
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "minivhd.h"
#include "internal.h"


static FILE*
open_existing_raw_img(const char* utf8_raw_path, MVHDGeom* geom, int* err)
{
    if (geom == NULL) {
        *err = MVHD_ERR_INVALID_GEOM;
        return NULL;
    }

    FILE *raw_img = mvhd_fopen(utf8_raw_path, "rb", err);
    if (raw_img == NULL) {
        *err = MVHD_ERR_FILE;
        return NULL;
    }

    mvhd_fseeko64(raw_img, 0, SEEK_END);
    uint64_t size_bytes = (uint64_t)mvhd_ftello64(raw_img);
    MVHDGeom new_geom = mvhd_calculate_geometry(size_bytes);
    if (mvhd_calc_size_bytes(&new_geom) != size_bytes) {
        *err = MVHD_ERR_CONV_SIZE;
        return NULL;
    }
    geom->cyl = new_geom.cyl;
    geom->heads = new_geom.heads;
    geom->spt = new_geom.spt;
    mvhd_fseeko64(raw_img, 0, SEEK_SET);

    return raw_img;
}


MVHDAPI MVHDMeta*
mvhd_convert_to_vhd_fixed(const char* utf8_raw_path, const char* utf8_vhd_path, int* err)
{
    MVHDGeom geom;

    FILE *raw_img = open_existing_raw_img(utf8_raw_path, &geom, err);
    if (raw_img == NULL) {
        return NULL;
    }

    uint64_t size_in_bytes = mvhd_calc_size_bytes(&geom);
    MVHDMeta *vhdm = mvhd_create_fixed_raw(utf8_vhd_path, raw_img, size_in_bytes, &geom, err, NULL);
    if (vhdm == NULL) {
        return NULL;
    }

    return vhdm;
}


MVHDAPI MVHDMeta*
mvhd_convert_to_vhd_sparse(const char* utf8_raw_path, const char* utf8_vhd_path, int* err)
{
    MVHDGeom geom;
    MVHDMeta *vhdm = NULL;

    FILE *raw_img = open_existing_raw_img(utf8_raw_path, &geom, err);
    if (raw_img == NULL) {
        return NULL;
    }

    vhdm = mvhd_create_sparse(utf8_vhd_path, geom, err);
    if (vhdm == NULL) {
        goto end;
    }

    uint8_t buff[4096] = {0}; // 8 sectors
    uint8_t empty_buff[4096] = {0};
    int total_sectors = mvhd_calc_size_sectors(&geom);
    int copy_sect = 0;

    for (int i = 0; i < total_sectors; i += 8) {
        copy_sect = 8;
        if ((i + 8) >= total_sectors) {
            copy_sect = total_sectors - i;
            memset(buff, 0, sizeof buff);
        }
        (void) !fread(buff, MVHD_SECTOR_SIZE, copy_sect, raw_img);

        /* Only write data if there's data to write, to take advantage of the sparse VHD format */
        if (memcmp(buff, empty_buff, sizeof buff) != 0) {
            mvhd_write_sectors(vhdm, i, copy_sect, buff);
        }
    }
end:
    fclose(raw_img);

    return vhdm;
}


MVHDAPI FILE*
mvhd_convert_to_raw(const char* utf8_vhd_path, const char* utf8_raw_path, int *err)
{
    FILE *raw_img = mvhd_fopen(utf8_raw_path, "wb", err);
    if (raw_img == NULL) {
        return NULL;
    }

    MVHDMeta *vhdm = mvhd_open(utf8_vhd_path, true, err);
    if (vhdm == NULL) {
        fclose(raw_img);
        return NULL;
    }

    uint8_t buff[4096] = {0}; // 8 sectors
    int total_sectors = mvhd_calc_size_sectors((MVHDGeom*)&vhdm->footer.geom);
    int copy_sect = 0;
    for (int i = 0; i < total_sectors; i += 8) {
        copy_sect = 8;
        if ((i + 8) >= total_sectors) {
            copy_sect = total_sectors - i;
        }
        mvhd_read_sectors(vhdm, i, copy_sect, buff);
        fwrite(buff, MVHD_SECTOR_SIZE, copy_sect, raw_img);
    }
    mvhd_close(vhdm);
    mvhd_fseeko64(raw_img, 0, SEEK_SET);

    return raw_img;
}
