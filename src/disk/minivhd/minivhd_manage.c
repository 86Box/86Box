/**
 * \file
 * \brief VHD management functions (open, close, read write etc)
 */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "cwalk.h"
#include "libxml2_encoding.h"
#include "minivhd_internal.h"
#include "minivhd_io.h"
#include "minivhd_util.h"
#include "minivhd_struct_rw.h"
#include "minivhd.h"

int mvhd_errno = 0;
static char tmp_open_path[MVHD_MAX_PATH_BYTES] = {0};
struct MVHDPaths {
    char dir_path[MVHD_MAX_PATH_BYTES];
    char file_name[MVHD_MAX_PATH_BYTES];
    char w2ku_path[MVHD_MAX_PATH_BYTES];
    char w2ru_path[MVHD_MAX_PATH_BYTES];
    char joined_path[MVHD_MAX_PATH_BYTES];
    uint16_t tmp_src_path[MVHD_MAX_PATH_CHARS];
};

static void mvhd_read_footer(MVHDMeta* vhdm);
static void mvhd_read_sparse_header(MVHDMeta* vhdm);
static bool mvhd_footer_checksum_valid(MVHDMeta* vhdm);
static bool mvhd_sparse_checksum_valid(MVHDMeta* vhdm);
static int mvhd_read_bat(MVHDMeta *vhdm, MVHDError* err);
static void mvhd_calc_sparse_values(MVHDMeta* vhdm);
static int mvhd_init_sector_bitmap(MVHDMeta* vhdm, MVHDError* err);

/**
 * \brief Populate data stuctures with content from a VHD footer
 *
 * \param [in] vhdm MiniVHD data structure
 */
static void mvhd_read_footer(MVHDMeta* vhdm) {
    uint8_t buffer[MVHD_FOOTER_SIZE];
    mvhd_fseeko64(vhdm->f, -MVHD_FOOTER_SIZE, SEEK_END);
    (void) !fread(buffer, sizeof buffer, 1, vhdm->f);
    mvhd_buffer_to_footer(&vhdm->footer, buffer);
}

/**
 * \brief Populate data stuctures with content from a VHD sparse header
 *
 * \param [in] vhdm MiniVHD data structure
 */
static void mvhd_read_sparse_header(MVHDMeta* vhdm) {
    uint8_t buffer[MVHD_SPARSE_SIZE];
    mvhd_fseeko64(vhdm->f, vhdm->footer.data_offset, SEEK_SET);
    (void) !fread(buffer, sizeof buffer, 1, vhdm->f);
    mvhd_buffer_to_header(&vhdm->sparse, buffer);
}

/**
 * \brief Validate VHD footer checksum
 *
 * This works by generating a checksum from the footer, and comparing it against the stored checksum.
 *
 * \param [in] vhdm MiniVHD data structure
 */
static bool mvhd_footer_checksum_valid(MVHDMeta* vhdm) {
    return vhdm->footer.checksum == mvhd_gen_footer_checksum(&vhdm->footer);
}

/**
 * \brief Validate VHD sparse header checksum
 *
 * This works by generating a checksum from the sparse header, and comparing it against the stored checksum.
 *
 * \param [in] vhdm MiniVHD data structure
 */
static bool mvhd_sparse_checksum_valid(MVHDMeta* vhdm) {
    return vhdm->sparse.checksum == mvhd_gen_sparse_checksum(&vhdm->sparse);
}

/**
 * \brief Read BAT into MiniVHD data structure
 *
 * The Block Allocation Table (BAT) is the structure in a sparse and differencing VHD which stores
 * the 4-byte sector offsets for each data block. This function allocates enough memory to contain
 * the entire BAT, and then reads the contents of the BAT into the buffer.
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [out] err this is populated with MVHD_ERR_MEM if the calloc fails
 *
 * \retval -1 if an error occurrs. Check value of err in this case
 * \retval 0 if the function call succeeds
 */
static int mvhd_read_bat(MVHDMeta *vhdm, MVHDError* err) {
    vhdm->block_offset = calloc(vhdm->sparse.max_bat_ent, sizeof *vhdm->block_offset);
    if (vhdm->block_offset == NULL) {
        *err = MVHD_ERR_MEM;
        return -1;
    }
    mvhd_fseeko64(vhdm->f, vhdm->sparse.bat_offset, SEEK_SET);
    for (uint32_t i = 0; i < vhdm->sparse.max_bat_ent; i++) {
        (void) !fread(&vhdm->block_offset[i], sizeof *vhdm->block_offset, 1, vhdm->f);
        vhdm->block_offset[i] = mvhd_from_be32(vhdm->block_offset[i]);
    }
    return 0;
}

/**
 * \brief Perform a one-time calculation of some sparse VHD values
 *
 * \param [in] vhdm MiniVHD data structure
 */
static void mvhd_calc_sparse_values(MVHDMeta* vhdm) {
    vhdm->sect_per_block = vhdm->sparse.block_sz / MVHD_SECTOR_SIZE;
    int bm_bytes = vhdm->sect_per_block / 8;
    vhdm->bitmap.sector_count = bm_bytes / MVHD_SECTOR_SIZE;
    if (bm_bytes % MVHD_SECTOR_SIZE > 0) {
        vhdm->bitmap.sector_count++;
    }
}

/**
 * \brief Allocate memory for a sector bitmap.
 *
 * Each data block is preceded by a sector bitmap. Each bit indicates whether the corresponding sector
 * is considered 'clean' or 'dirty' (for sparse VHD images), or whether to read from the parent or current
 * image (for differencing images).
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [out] err this is populated with MVHD_ERR_MEM if the calloc fails
 *
 * \retval -1 if an error occurrs. Check value of err in this case
 * \retval 0 if the function call succeeds
 */
static int mvhd_init_sector_bitmap(MVHDMeta* vhdm, MVHDError* err) {
    vhdm->bitmap.curr_bitmap = calloc(vhdm->bitmap.sector_count, MVHD_SECTOR_SIZE);
    if (vhdm->bitmap.curr_bitmap == NULL) {
        *err = MVHD_ERR_MEM;
        return -1;
    }
    vhdm->bitmap.curr_block = -1;
    return 0;
}

/**
 * \brief Check if the path for a given platform code exists
 *
 * From the available paths, both relative and absolute, construct a full path
 * and attempt to open a file at that path.
 *
 * Note, this function makes no attempt to verify that the path is the correct
 * VHD image, or even a VHD image at all.
 *
 * \param [in] paths a struct containing all available paths to work with
 * \param [in] the platform code to try and obtain a path for. Setting this to zero
 * will try using the directory of the child image
 *
 * \retval true if a file is found
 * \retval false if a file is not found
 */
static bool mvhd_parent_path_exists(struct MVHDPaths* paths, uint32_t plat_code) {
    memset(paths->joined_path, 0, sizeof paths->joined_path);
    FILE* f;
    int cwk_ret, ferr;
    enum cwk_path_style style = cwk_path_guess_style((const char*)paths->dir_path);
    cwk_path_set_style(style);
    cwk_ret = 1;
    if (plat_code == MVHD_DIF_LOC_W2RU && *paths->w2ru_path) {
        cwk_ret = cwk_path_join((const char*)paths->dir_path, (const char*)paths->w2ru_path, paths->joined_path, sizeof paths->joined_path);
    } else if (plat_code == MVHD_DIF_LOC_W2KU && *paths->w2ku_path) {
        memcpy(paths->joined_path, paths->w2ku_path, (sizeof paths->joined_path) - 1);
        cwk_ret = 0;
    } else if (plat_code == 0) {
        cwk_ret = cwk_path_join((const char*)paths->dir_path, (const char*)paths->file_name, paths->joined_path, sizeof paths->joined_path);
    }
    if (cwk_ret > MVHD_MAX_PATH_BYTES) {
        return false;
    }
    f = mvhd_fopen((const char*)paths->joined_path, "rb", &ferr);
    if (f != NULL) {
        /* We found a file at the requested path! */
        memcpy(tmp_open_path, paths->joined_path, (sizeof paths->joined_path) - 1);
        tmp_open_path[sizeof tmp_open_path - 1] = '\0';
        fclose(f);
        return true;
    } else {
        return false;
    }
}

/**
 * \brief attempt to obtain a file path to a file that may be a valid VHD image
 *
 * Differential VHD images store both a UTF-16BE file name (or path), and up to
 * eight "parent locator" entries. Using this information, this function tries to
 * find a parent image.
 *
 * This function does not verify if the path returned is a valid parent image.
 *
 * \param [in] vhdm current MiniVHD data structure
 * \param [out] err any errors that may occurr. Check this if NULL is returned
 *
 * \return a pointer to the global string `tmp_open_path`, or NULL if a path could
 * not be found, or some error occurred
 */
static char* mvhd_get_diff_parent_path(MVHDMeta* vhdm, int* err) {
    int utf_outlen, utf_inlen, utf_ret;
    char* par_fp = NULL;
    /* We can't resolve relative paths if we don't have an absolute
       path to work with */
    if (!cwk_path_is_absolute((const char*)vhdm->filename)) {
        *err = MVHD_ERR_PATH_REL;
        goto end;
    }
    struct MVHDPaths* paths = calloc(1, sizeof *paths);
    if (paths == NULL) {
        *err = MVHD_ERR_MEM;
        goto end;
    }
    size_t dirlen;
    cwk_path_get_dirname((const char*)vhdm->filename, &dirlen);
    if (dirlen >= sizeof paths->dir_path) {
        *err = MVHD_ERR_PATH_LEN;
        goto paths_cleanup;
    }
    memcpy(paths->dir_path, vhdm->filename, dirlen);
    /* Get the filename field from the sparse header. */
    utf_outlen = (int)sizeof paths->file_name;
    utf_inlen = (int)sizeof vhdm->sparse.par_utf16_name;
    utf_ret = UTF16BEToUTF8((unsigned char*)paths->file_name, &utf_outlen, (const unsigned char*)vhdm->sparse.par_utf16_name, &utf_inlen);
    if (utf_ret < 0) {
        mvhd_set_encoding_err(utf_ret, err);
        goto paths_cleanup;
    }
    /* Now read the parent locator entries, both relative and absolute, if they exist */
    unsigned char* loc_path;
    for (int i = 0; i < 8; i++) {
        utf_outlen = MVHD_MAX_PATH_BYTES - 1;
        if (vhdm->sparse.par_loc_entry[i].plat_code == MVHD_DIF_LOC_W2RU) {
            loc_path = (unsigned char*)paths->w2ru_path;
        } else if (vhdm->sparse.par_loc_entry[i].plat_code == MVHD_DIF_LOC_W2KU) {
            loc_path = (unsigned char*)paths->w2ku_path;
        } else {
            continue;
        }
        utf_inlen = vhdm->sparse.par_loc_entry[i].plat_data_len;
        if (utf_inlen > MVHD_MAX_PATH_BYTES) {
            *err = MVHD_ERR_PATH_LEN;
            goto paths_cleanup;
        }
        mvhd_fseeko64(vhdm->f, vhdm->sparse.par_loc_entry[i].plat_data_offset, SEEK_SET);
        (void) !fread(paths->tmp_src_path, sizeof (uint8_t), utf_inlen, vhdm->f);
        /* Note, the W2*u parent locators are UTF-16LE, unlike the filename field previously obtained,
           which is UTF-16BE */
        utf_ret = UTF16LEToUTF8(loc_path, &utf_outlen, (const unsigned char*)paths->tmp_src_path, &utf_inlen);
        if (utf_ret < 0) {
            mvhd_set_encoding_err(utf_ret, err);
            goto paths_cleanup;
        }
    }
    /* We have paths in UTF-8. We should have enough info to try and find the parent VHD */
    /* Does the relative path exist? */
    if (mvhd_parent_path_exists(paths, MVHD_DIF_LOC_W2RU)) {
        par_fp = tmp_open_path;
        goto paths_cleanup;
    }
    /* What about trying the child directory? */
    if (mvhd_parent_path_exists(paths, 0)) {
        par_fp = tmp_open_path;
        goto paths_cleanup;
    }
    /* Well, all else fails, try the stored absolute path, if it exists */
    if (mvhd_parent_path_exists(paths, MVHD_DIF_LOC_W2KU)) {
        par_fp = tmp_open_path;
        goto paths_cleanup;
    }
    /* If we reach this point, we could not find a path with a valid file */
    par_fp = NULL;
    *err = MVHD_ERR_PAR_NOT_FOUND;

paths_cleanup:
    free(paths);
    paths = NULL;
end:
    return par_fp;
}

/**
 * \brief Attach the read/write function pointers to read/write functions
 *
 * Depending on the VHD type, different sector reading and writing functions are used.
 * The functions are called via function pointers stored in the vhdm struct.
 *
 * \param [in] vhdm MiniVHD data structure
 */
static void mvhd_assign_io_funcs(MVHDMeta* vhdm) {
    switch (vhdm->footer.disk_type) {
    case MVHD_TYPE_FIXED:
        vhdm->read_sectors = mvhd_fixed_read;
        vhdm->write_sectors = mvhd_fixed_write;
        break;
    case MVHD_TYPE_DYNAMIC:
        vhdm->read_sectors = mvhd_sparse_read;
        vhdm->write_sectors = mvhd_sparse_diff_write;
        break;
    case MVHD_TYPE_DIFF:
        vhdm->read_sectors = mvhd_diff_read;
        vhdm->write_sectors = mvhd_sparse_diff_write;
        break;
    }
    if (vhdm->readonly) {
        vhdm->write_sectors = mvhd_noop_write;
    }
}

bool mvhd_file_is_vhd(FILE* f) {
    if (f) {
        uint8_t con_str[8];
        mvhd_fseeko64(f, -MVHD_FOOTER_SIZE, SEEK_END);
        (void) !fread(con_str, sizeof con_str, 1, f);
        return mvhd_is_conectix_str(con_str);
    } else {
        return false;
    }
}

MVHDGeom mvhd_calculate_geometry(uint64_t size) {
    MVHDGeom chs;
    uint32_t ts = (uint32_t)(size / MVHD_SECTOR_SIZE);
    uint32_t spt, heads, cyl, cth;
    if (ts > 65535 * 16 * 255) {
        ts = 65535 * 16 * 255;
    }
    if (ts >= 65535 * 16 * 63) {
        spt = 255;
        heads = 16;
        cth = ts / spt;
    } else {
        spt = 17;
        cth = ts / spt;
        heads = (cth + 1023) / 1024;
        if (heads < 4) {
            heads = 4;
        }
        if (cth >= (heads * 1024) || heads > 16) {
            spt = 31;
            heads = 16;
            cth = ts / spt;
        }
        if (cth >= (heads * 1024)) {
            spt = 63;
            heads = 16;
            cth = ts / spt;
        }
    }
    cyl = cth / heads;
    chs.heads = heads;
    chs.spt = spt;
    chs.cyl = cyl;
    return chs;
}

MVHDMeta* mvhd_open(const char* path, bool readonly, int* err) {
    MVHDError open_err;
    MVHDMeta *vhdm = calloc(sizeof *vhdm, 1);
    if (vhdm == NULL) {
        *err = MVHD_ERR_MEM;
        goto end;
    }
    if (strlen(path) >= sizeof vhdm->filename) {
        *err = MVHD_ERR_PATH_LEN;
        goto cleanup_vhdm;
    }
    //This is safe, as we've just checked for potential overflow above
    strcpy(vhdm->filename, path);
    vhdm->f = readonly ? mvhd_fopen((const char*)vhdm->filename, "rb", err) : mvhd_fopen((const char*)vhdm->filename, "rb+", err);
    if (vhdm->f == NULL) {
        /* note, mvhd_fopen sets err for us */
        goto cleanup_vhdm;
    }
    vhdm->readonly = readonly;
    if (!mvhd_file_is_vhd(vhdm->f)) {
        *err = MVHD_ERR_NOT_VHD;
        goto cleanup_file;
    }
    mvhd_read_footer(vhdm);
    if (!mvhd_footer_checksum_valid(vhdm)) {
        *err = MVHD_ERR_FOOTER_CHECKSUM;
        goto cleanup_file;
    }
    if (vhdm->footer.disk_type == MVHD_TYPE_DIFF || vhdm->footer.disk_type == MVHD_TYPE_DYNAMIC) {
        mvhd_read_sparse_header(vhdm);
        if (!mvhd_sparse_checksum_valid(vhdm)) {
            *err = MVHD_ERR_SPARSE_CHECKSUM;
            goto cleanup_file;
        }
        if (mvhd_read_bat(vhdm, &open_err) == -1) {
            *err = open_err;
            goto cleanup_file;
        }
        mvhd_calc_sparse_values(vhdm);
        if (mvhd_init_sector_bitmap(vhdm, &open_err) == -1) {
            *err = open_err;
            goto cleanup_bat;
        }

    } else if (vhdm->footer.disk_type != MVHD_TYPE_FIXED) {
        *err = MVHD_ERR_TYPE;
        goto cleanup_bitmap;
    }
    mvhd_assign_io_funcs(vhdm);
    vhdm->format_buffer.zero_data = calloc(64, MVHD_SECTOR_SIZE);
    if (vhdm->format_buffer.zero_data == NULL) {
        *err = MVHD_ERR_MEM;
        goto cleanup_bitmap;
    }
    vhdm->format_buffer.sector_count = 64;
    if (vhdm->footer.disk_type == MVHD_TYPE_DIFF) {
        char* par_path = mvhd_get_diff_parent_path(vhdm, err);
        if (par_path == NULL) {
            goto cleanup_format_buff;
        }
        uint32_t par_mod_ts = mvhd_file_mod_timestamp(par_path, err);
        if (*err != 0) {
            goto cleanup_format_buff;
        }
        if (vhdm->sparse.par_timestamp != par_mod_ts) {
            /* The last-modified timestamp is to fragile to make this a fatal error.
               Instead, we inform the caller of the potential problem. */
            *err = MVHD_ERR_TIMESTAMP;
        }
        vhdm->parent = mvhd_open(par_path, true, err);
        if (vhdm->parent == NULL) {
            goto cleanup_format_buff;
        }
        if (memcmp(vhdm->sparse.par_uuid, vhdm->parent->footer.uuid, sizeof vhdm->sparse.par_uuid) != 0) {
            *err = MVHD_ERR_INVALID_PAR_UUID;
            goto cleanup_format_buff;
        }
    }
    /* If we've reached this point, we are good to go, so skip the cleanup steps */
    goto end;
cleanup_format_buff:
    free(vhdm->format_buffer.zero_data);
    vhdm->format_buffer.zero_data = NULL;
cleanup_bitmap:
    free(vhdm->bitmap.curr_bitmap);
    vhdm->bitmap.curr_bitmap = NULL;
cleanup_bat:
    free(vhdm->block_offset);
    vhdm->block_offset = NULL;
cleanup_file:
    fclose(vhdm->f);
    vhdm->f = NULL;
cleanup_vhdm:
    free(vhdm);
    vhdm = NULL;
end:
    return vhdm;
}

void mvhd_close(MVHDMeta* vhdm) {
    if (vhdm != NULL) {
        if (vhdm->parent != NULL) {
            mvhd_close(vhdm->parent);
        }
        fclose(vhdm->f);
        if (vhdm->block_offset != NULL) {
            free(vhdm->block_offset);
            vhdm->block_offset = NULL;
        }
        if (vhdm->bitmap.curr_bitmap != NULL) {
            free(vhdm->bitmap.curr_bitmap);
            vhdm->bitmap.curr_bitmap = NULL;
        }
        if (vhdm->format_buffer.zero_data != NULL) {
            free(vhdm->format_buffer.zero_data);
            vhdm->format_buffer.zero_data = NULL;
        }
        free(vhdm);
        vhdm = NULL;
    }
}

int mvhd_diff_update_par_timestamp(MVHDMeta* vhdm, int* err) {
    uint8_t sparse_buff[1024];
    if (vhdm == NULL || err == NULL) {
        *err = MVHD_ERR_INVALID_PARAMS;
        return -1;
    }
    if (vhdm->footer.disk_type != MVHD_TYPE_DIFF) {
        *err = MVHD_ERR_TYPE;
        return -1;
    }
    char* par_path = mvhd_get_diff_parent_path(vhdm, err);
    if (par_path == NULL) {
        return -1;
    }
    uint32_t par_mod_ts = mvhd_file_mod_timestamp(par_path, err);
    if (*err != 0) {
        return -1;
    }
    /* Update the timestamp and sparse header checksum */
    vhdm->sparse.par_timestamp = par_mod_ts;
    vhdm->sparse.checksum = mvhd_gen_sparse_checksum(&vhdm->sparse);
    /* Generate and write the updated sparse header */
    mvhd_header_to_buffer(&vhdm->sparse, sparse_buff);
    mvhd_fseeko64(vhdm->f, (int64_t)vhdm->footer.data_offset, SEEK_SET);
    fwrite(sparse_buff, sizeof sparse_buff, 1, vhdm->f);
    return 0;
}

int mvhd_read_sectors(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* out_buff) {
    return vhdm->read_sectors(vhdm, offset, num_sectors, out_buff);
}

int mvhd_write_sectors(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* in_buff) {
    return vhdm->write_sectors(vhdm, offset, num_sectors, in_buff);
}

int mvhd_format_sectors(MVHDMeta* vhdm, uint32_t offset, int num_sectors) {
    int num_full = num_sectors / vhdm->format_buffer.sector_count;
    int remain = num_sectors % vhdm->format_buffer.sector_count;
    for (int i = 0; i < num_full; i++) {
        vhdm->write_sectors(vhdm, offset, vhdm->format_buffer.sector_count, vhdm->format_buffer.zero_data);
        offset += vhdm->format_buffer.sector_count;
    }
    vhdm->write_sectors(vhdm, offset, remain, vhdm->format_buffer.zero_data);
    return 0;
}
