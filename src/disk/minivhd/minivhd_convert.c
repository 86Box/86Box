#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "minivhd_create.h"
#include "minivhd_internal.h"
#include "minivhd_util.h"
#include "minivhd.h"

static FILE* mvhd_open_existing_raw_img(const char* utf8_raw_path, MVHDGeom* geom, int* err);

static FILE* mvhd_open_existing_raw_img(const char* utf8_raw_path, MVHDGeom* geom, int* err) {
    FILE *raw_img = mvhd_fopen(utf8_raw_path, "rb", err);
    if (raw_img == NULL) {
        *err = MVHD_ERR_FILE;
        return NULL;
    }
    if (geom == NULL) {
        *err = MVHD_ERR_INVALID_GEOM;
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

MVHDMeta* mvhd_convert_to_vhd_fixed(const char* utf8_raw_path, const char* utf8_vhd_path, int* err) {
    MVHDGeom geom;
    FILE *raw_img = mvhd_open_existing_raw_img(utf8_raw_path, &geom, err);
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
MVHDMeta* mvhd_convert_to_vhd_sparse(const char* utf8_raw_path, const char* utf8_vhd_path, int* err) {
    MVHDGeom geom;
    MVHDMeta *vhdm = NULL;
    FILE *raw_img = mvhd_open_existing_raw_img(utf8_raw_path, &geom, err);
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
FILE* mvhd_convert_to_raw(const char* utf8_vhd_path, const char* utf8_raw_path, int *err) {
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
