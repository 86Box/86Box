#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cwalk.h"
#include "libxml2_encoding.h"
#include "minivhd_internal.h"
#include "minivhd_util.h"
#include "minivhd_struct_rw.h"
#include "minivhd_io.h"
#include "minivhd_create.h"
#include "minivhd.h"

static void mvhd_gen_footer(MVHDFooter* footer, uint64_t size_in_bytes, MVHDGeom* geom, MVHDType type, uint64_t sparse_header_off);
static void mvhd_gen_sparse_header(MVHDSparseHeader* header, uint32_t num_blks, uint64_t bat_offset, uint32_t block_size_in_sectors);
static int mvhd_gen_par_loc(MVHDSparseHeader* header,
                            const char* child_path,
                            const char* par_path,
                            uint64_t start_offset,
                            mvhd_utf16* w2ku_path_buff,
                            mvhd_utf16* w2ru_path_buff,
                            MVHDError* err);
static MVHDMeta* mvhd_create_sparse_diff(const char* path, const char* par_path, uint64_t size_in_bytes, MVHDGeom* geom, uint32_t block_size_in_sectors, int* err);

/**
 * \brief Populate a VHD footer
 *
 * \param [in] footer to populate
 * \param [in] size_in_bytes is the total size of the virtual hard disk in bytes
 * \param [in] geom to use
 * \param [in] type of HVD that is being created
 * \param [in] sparse_header_off, an absolute file offset to the sparse header. Not used for fixed VHD images
 */
static void mvhd_gen_footer(MVHDFooter* footer, uint64_t size_in_bytes, MVHDGeom* geom, MVHDType type, uint64_t sparse_header_off) {
    memcpy(footer->cookie, "conectix", sizeof footer->cookie);
    footer->features = 0x00000002;
    footer->fi_fmt_vers = 0x00010000;
    footer->data_offset = (type == MVHD_TYPE_DIFF || type == MVHD_TYPE_DYNAMIC) ? sparse_header_off : 0xffffffffffffffff;
    footer->timestamp = vhd_calc_timestamp();
    memcpy(footer->cr_app, "mvhd", sizeof footer->cr_app);
    footer->cr_vers = 0x000e0000;
    memcpy(footer->cr_host_os, "Wi2k", sizeof footer->cr_host_os);
    footer->orig_sz = footer->curr_sz = size_in_bytes;
    footer->geom.cyl = geom->cyl;
    footer->geom.heads = geom->heads;
    footer->geom.spt = geom->spt;
    footer->disk_type = type;
    mvhd_generate_uuid(footer->uuid);
    footer->checksum = mvhd_gen_footer_checksum(footer);
}

/**
 * \brief Populate a VHD sparse header
 *
 * \param [in] header for sparse and differencing images
 * \param [in] num_blks is the number of data blocks that the image contains
 * \param [in] bat_offset is the absolute file offset for start of the Block Allocation Table
 * \param [in] block_size_in_sectors is the block size in sectors.
 */
static void mvhd_gen_sparse_header(MVHDSparseHeader* header, uint32_t num_blks, uint64_t bat_offset, uint32_t block_size_in_sectors) {
    memcpy(header->cookie, "cxsparse", sizeof header->cookie);
    header->data_offset = 0xffffffffffffffff;
    header->bat_offset = bat_offset;
    header->head_vers = 0x00010000;
    header->max_bat_ent = num_blks;
    header->block_sz = block_size_in_sectors * (uint32_t)MVHD_SECTOR_SIZE;
    header->checksum = mvhd_gen_sparse_checksum(header);
}

/**
 * \brief Generate parent locators for differencing VHD images
 *
 * \param [in] header the sparse header to populate with parent locator entries
 * \param [in] child_path is the full path to the VHD being created
 * \param [in] par_path is the full path to the parent image
 * \param [in] start_offset is the absolute file offset from where to start storing the entries themselves. Must be sector aligned.
 * \param [out] w2ku_path_buff is a buffer containing the full path to the parent, encoded as UTF16-LE
 * \param [out] w2ru_path_buff is a buffer containing the relative path to the parent, encoded as UTF16-LE
 * \param [out] err indicates what error occurred, if any
 *
 * \retval 0 if success
 * \retval < 0 if an error occurrs. Check value of *err for actual error
 */
static int mvhd_gen_par_loc(MVHDSparseHeader* header,
                            const char* child_path,
                            const char* par_path,
                            uint64_t start_offset,
                            mvhd_utf16* w2ku_path_buff,
                            mvhd_utf16* w2ru_path_buff,
                            MVHDError* err) {
    /* Get our paths to store in the differencing VHD. We want both the absolute path to the parent,
       as well as the relative path from the child VHD */
    int rv = 0;
    char* par_filename;
    size_t par_fn_len;
    char rel_path[MVHD_MAX_PATH_BYTES] = {0};
    char child_dir[MVHD_MAX_PATH_BYTES] = {0};
    size_t child_dir_len;
    if (strlen(child_path) < sizeof child_dir) {
        strcpy(child_dir, child_path);
    } else {
        *err = MVHD_ERR_PATH_LEN;
        rv = -1;
        goto end;
    }
    cwk_path_get_basename(par_path, (const char**)&par_filename, &par_fn_len);
    cwk_path_get_dirname(child_dir, &child_dir_len);
    child_dir[child_dir_len] = '\0';
    size_t rel_len = cwk_path_get_relative(child_dir, par_path, rel_path, sizeof rel_path);
    if (rel_len > sizeof rel_path) {
        *err = MVHD_ERR_PATH_LEN;
        rv = -1;
        goto end;
    }
    /* We have our paths, now store the parent filename directly in the sparse header. */
    int outlen = sizeof header->par_utf16_name;
    int utf_ret;
    utf_ret = UTF8ToUTF16BE((unsigned char*)header->par_utf16_name, &outlen, (const unsigned char*)par_filename, (int*)&par_fn_len);
    if (utf_ret < 0) {
        mvhd_set_encoding_err(utf_ret, (int*)err);
        rv = -1;
        goto end;
    }

    /* And encode the paths to UTF16-LE */
    size_t par_path_len = strlen(par_path);
    outlen = sizeof *w2ku_path_buff * MVHD_MAX_PATH_CHARS;
    utf_ret = UTF8ToUTF16LE((unsigned char*)w2ku_path_buff, &outlen, (const unsigned char*)par_path, (int*)&par_path_len);
    if (utf_ret < 0) {
        mvhd_set_encoding_err(utf_ret, (int*)err);
        rv = -1;
        goto end;
    }
    int w2ku_len = utf_ret;
    outlen = sizeof *w2ru_path_buff * MVHD_MAX_PATH_CHARS;
    utf_ret = UTF8ToUTF16LE((unsigned char*)w2ru_path_buff, &outlen, (const unsigned char*)rel_path, (int*)&rel_len);
    if (utf_ret < 0) {
        mvhd_set_encoding_err(utf_ret, (int*)err);
        rv = -1;
        goto end;
    }
    int w2ru_len = utf_ret;
    /**
     * Finally populate the parent locaters in the sparse header.
     * This is the information needed to find the paths saved elsewhere
     * in the VHD image
     */

    /* Note about the plat_data_space field: The VHD spec says this field stores the number of sectors needed to store the locator path.
     * However, Hyper-V and VPC store the number of bytes, not the number of sectors, and will refuse to open VHDs which have the
     * number of sectors in this field.
     * See https://stackoverflow.com/questions/40760181/mistake-in-virtual-hard-disk-image-format-specification
     */
    header->par_loc_entry[0].plat_code = MVHD_DIF_LOC_W2KU;
    header->par_loc_entry[0].plat_data_len = (uint32_t)w2ku_len;
    header->par_loc_entry[0].plat_data_offset = (uint64_t)start_offset;
    header->par_loc_entry[0].plat_data_space = ((header->par_loc_entry[0].plat_data_len / MVHD_SECTOR_SIZE) + 1) * MVHD_SECTOR_SIZE;
    header->par_loc_entry[1].plat_code = MVHD_DIF_LOC_W2RU;
    header->par_loc_entry[1].plat_data_len = (uint32_t)w2ru_len;
    header->par_loc_entry[1].plat_data_offset = (uint64_t)start_offset + ((uint64_t)header->par_loc_entry[0].plat_data_space);
    header->par_loc_entry[1].plat_data_space = ((header->par_loc_entry[1].plat_data_len / MVHD_SECTOR_SIZE) + 1) * MVHD_SECTOR_SIZE;
    goto end;

end:
    return rv;
}

MVHDMeta* mvhd_create_fixed(const char* path, MVHDGeom geom, int* err, mvhd_progress_callback progress_callback) {
    uint64_t size_in_bytes = mvhd_calc_size_bytes(&geom);
    return mvhd_create_fixed_raw(path, NULL, size_in_bytes, &geom, err, progress_callback);
}

/**
 * \brief internal function that implements public mvhd_create_fixed() functionality
 *
 * Contains one more parameter than the public function, to allow using an existing
 * raw disk image as the data source for the new fixed VHD.
 *
 * \param [in] raw_image file handle to a raw disk image to populate VHD
 */
MVHDMeta* mvhd_create_fixed_raw(const char* path, FILE* raw_img, uint64_t size_in_bytes, MVHDGeom* geom, int* err, mvhd_progress_callback progress_callback) {
    uint8_t img_data[MVHD_SECTOR_SIZE] = {0};
    uint8_t footer_buff[MVHD_FOOTER_SIZE] = {0};
    MVHDMeta* vhdm = calloc(1, sizeof *vhdm);
    if (vhdm == NULL) {
        *err = MVHD_ERR_MEM;
        goto end;
    }
    if (geom == NULL || (geom->cyl == 0 || geom->heads == 0 || geom->spt == 0)) {
        *err = MVHD_ERR_INVALID_GEOM;
        goto cleanup_vhdm;
    }
    FILE* f = mvhd_fopen(path, "wb+", err);
    if (f == NULL) {
        goto cleanup_vhdm;
    }
    mvhd_fseeko64(f, 0, SEEK_SET);
    uint32_t size_sectors = (uint32_t)(size_in_bytes / MVHD_SECTOR_SIZE);
    uint32_t s;
    if (progress_callback)
        progress_callback(0, size_sectors);
    if (raw_img != NULL) {
        mvhd_fseeko64(raw_img, 0, SEEK_END);
        uint64_t raw_size = (uint64_t)mvhd_ftello64(raw_img);
        MVHDGeom raw_geom = mvhd_calculate_geometry(raw_size);
        if (mvhd_calc_size_bytes(&raw_geom) != raw_size) {
            *err = MVHD_ERR_CONV_SIZE;
            goto cleanup_vhdm;
        }
        mvhd_gen_footer(&vhdm->footer, raw_size, geom, MVHD_TYPE_FIXED, 0);
        mvhd_fseeko64(raw_img, 0, SEEK_SET);
        for (s = 0; s < size_sectors; s++) {
            fread(img_data, sizeof img_data, 1, raw_img);
            fwrite(img_data, sizeof img_data, 1, f);
            if (progress_callback)
                progress_callback(s + 1, size_sectors);
        }
    } else {
        mvhd_gen_footer(&vhdm->footer, size_in_bytes, geom, MVHD_TYPE_FIXED, 0);
        for (s = 0; s < size_sectors; s++) {
            fwrite(img_data, sizeof img_data, 1, f);
            if (progress_callback)
                progress_callback(s + 1, size_sectors);
        }
    }
    mvhd_footer_to_buffer(&vhdm->footer, footer_buff);
    fwrite(footer_buff, sizeof footer_buff, 1, f);
    fclose(f);
    f = NULL;
    free(vhdm);
    vhdm = mvhd_open(path, false, err);
    goto end;

cleanup_vhdm:
    free(vhdm);
    vhdm = NULL;
end:
    return vhdm;
}

/**
 * \brief Create sparse or differencing VHD image.
 *
 * \param [in] path is the absolute path to the VHD file to create
 * \param [in] par_path is the absolute path to a parent image. If NULL, a sparse image is created, otherwise create a differencing image
 * \param [in] size_in_bytes is the total size in bytes of the virtual hard disk image
 * \param [in] geom is the HDD geometry of the image to create. Determines final image size
 * \param [in] block_size_in_sectors is the block size in sectors
 * \param [out] err indicates what error occurred, if any
 *
 * \return NULL if an error occurrs. Check value of *err for actual error. Otherwise returns pointer to a MVHDMeta struct
 */
static MVHDMeta* mvhd_create_sparse_diff(const char* path, const char* par_path, uint64_t size_in_bytes, MVHDGeom* geom, uint32_t block_size_in_sectors, int* err) {
    uint8_t footer_buff[MVHD_FOOTER_SIZE] = {0};
    uint8_t sparse_buff[MVHD_SPARSE_SIZE] = {0};
    uint8_t bat_sect[MVHD_SECTOR_SIZE];
    MVHDGeom par_geom = {0};
    memset(bat_sect, 0xffffffff, sizeof bat_sect);
    MVHDMeta* vhdm = NULL;
    MVHDMeta* par_vhdm = NULL;
    mvhd_utf16* w2ku_path_buff = NULL;
    mvhd_utf16* w2ru_path_buff = NULL;
    uint32_t par_mod_timestamp = 0;
    if (par_path != NULL) {
        par_mod_timestamp = mvhd_file_mod_timestamp(par_path, err);
        if (*err != 0) {
            goto end;
        }
        par_vhdm = mvhd_open(par_path, true, err);
        if (par_vhdm == NULL) {
            goto end;
        }
    }
    vhdm = calloc(1, sizeof *vhdm);
    if (vhdm == NULL) {
        *err = MVHD_ERR_MEM;
        goto cleanup_par_vhdm;
    }
    if (par_vhdm != NULL) {
        /* We use the geometry from the parent VHD, not what was passed in */
        par_geom.cyl = par_vhdm->footer.geom.cyl;
        par_geom.heads = par_vhdm->footer.geom.heads;
        par_geom.spt = par_vhdm->footer.geom.spt;
        geom = &par_geom;
        size_in_bytes = par_vhdm->footer.curr_sz;
    } else if (geom == NULL || (geom->cyl == 0 || geom->heads == 0 || geom->spt == 0)) {
        *err = MVHD_ERR_INVALID_GEOM;
        goto cleanup_vhdm;
    }

    FILE* f = mvhd_fopen(path, "wb+", err);
    if (f == NULL) {
        goto cleanup_vhdm;
    }
    mvhd_fseeko64(f, 0, SEEK_SET);
    /* Note, the sparse header follows the footer copy at the beginning of the file */
    if (par_path == NULL) {
        mvhd_gen_footer(&vhdm->footer, size_in_bytes, geom, MVHD_TYPE_DYNAMIC, MVHD_FOOTER_SIZE);
    } else {
        mvhd_gen_footer(&vhdm->footer, size_in_bytes, geom, MVHD_TYPE_DIFF, MVHD_FOOTER_SIZE);
    }
    mvhd_footer_to_buffer(&vhdm->footer, footer_buff);
    /* As mentioned, start with a copy of the footer */
    fwrite(footer_buff, sizeof footer_buff, 1, f);
    /**
     * Calculate the number of (2MB or 512KB) data blocks required to store the entire
     * contents of the disk image, followed by the number of sectors the
     * BAT occupies in the image. Note, the BAT is sector aligned, and is padded
     * to the next sector boundary
     * */
    uint32_t size_in_sectors = (uint32_t)(size_in_bytes / MVHD_SECTOR_SIZE);
    uint32_t num_blks = size_in_sectors / block_size_in_sectors;
    if (size_in_sectors % block_size_in_sectors != 0) {
        num_blks += 1;
    }
    uint32_t num_bat_sect = num_blks / MVHD_BAT_ENT_PER_SECT;
    if (num_blks % MVHD_BAT_ENT_PER_SECT != 0) {
        num_bat_sect += 1;
    }
    /* Storing the BAT directly following the footer and header */
    uint64_t bat_offset = MVHD_FOOTER_SIZE + MVHD_SPARSE_SIZE;
    uint64_t par_loc_offset = 0;

    /**
     * If creating a differencing VHD, populate the sparse header with additional
     * data about the parent image, and where to find it, and it's last modified timestamp
     * */
    if (par_vhdm != NULL) {
        /**
         * Create output buffers to encode paths into.
         * The paths are not stored directly in the sparse header, hence the need to
         * store them in buffers to be written to the VHD image later
         */
        w2ku_path_buff = calloc(MVHD_MAX_PATH_CHARS, sizeof * w2ku_path_buff);
        if (w2ku_path_buff == NULL) {
            *err = MVHD_ERR_MEM;
            goto end;
        }
        w2ru_path_buff = calloc(MVHD_MAX_PATH_CHARS, sizeof * w2ru_path_buff);
        if (w2ru_path_buff == NULL) {
            *err = MVHD_ERR_MEM;
            goto end;
        }
        memcpy(vhdm->sparse.par_uuid, par_vhdm->footer.uuid, sizeof vhdm->sparse.par_uuid);
        par_loc_offset = bat_offset + ((uint64_t)num_bat_sect * MVHD_SECTOR_SIZE) + (5 * MVHD_SECTOR_SIZE);
        if (mvhd_gen_par_loc(&vhdm->sparse, path, par_path, par_loc_offset, w2ku_path_buff, w2ru_path_buff, (MVHDError*)err) < 0) {
            goto cleanup_vhdm;
        }
        vhdm->sparse.par_timestamp = par_mod_timestamp;
    }
    mvhd_gen_sparse_header(&vhdm->sparse, num_blks, bat_offset, block_size_in_sectors);
    mvhd_header_to_buffer(&vhdm->sparse, sparse_buff);
    fwrite(sparse_buff, sizeof sparse_buff, 1, f);
    /* The BAT sectors need to be filled with 0xffffffff */
    for (uint32_t i = 0; i < num_bat_sect; i++) {
        fwrite(bat_sect, sizeof bat_sect, 1, f);
    }
    mvhd_write_empty_sectors(f, 5);
    /**
     * If creating a differencing VHD, the paths to the parent image need to be written
     * tp the file. Both absolute and relative paths are written
     * */
    if (par_vhdm != NULL) {
        uint64_t curr_pos = (uint64_t)mvhd_ftello64(f);
        /* Double check my sums... */
        assert(curr_pos == par_loc_offset);
        /* Fill the space required for location data with zero */
        uint8_t empty_sect[MVHD_SECTOR_SIZE] = {0};
        for (int i = 0; i < 2; i++) {
            for (uint32_t j = 0; j < (vhdm->sparse.par_loc_entry[i].plat_data_space / MVHD_SECTOR_SIZE); j++) {
                fwrite(empty_sect, sizeof empty_sect, 1, f);
            }
        }
        /* Now write the location entries */
        mvhd_fseeko64(f, vhdm->sparse.par_loc_entry[0].plat_data_offset, SEEK_SET);
        fwrite(w2ku_path_buff, vhdm->sparse.par_loc_entry[0].plat_data_len, 1, f);
        mvhd_fseeko64(f, vhdm->sparse.par_loc_entry[1].plat_data_offset, SEEK_SET);
        fwrite(w2ru_path_buff, vhdm->sparse.par_loc_entry[1].plat_data_len, 1, f);
        /* and reset the file position to continue */
        mvhd_fseeko64(f, vhdm->sparse.par_loc_entry[1].plat_data_offset + vhdm->sparse.par_loc_entry[1].plat_data_space, SEEK_SET);
        mvhd_write_empty_sectors(f, 5);
    }
    /* And finish with the footer */
    fwrite(footer_buff, sizeof footer_buff, 1, f);
    fclose(f);
    f = NULL;
    free(vhdm);
    vhdm = mvhd_open(path, false, err);
    goto end;

cleanup_vhdm:
    free(vhdm);
    vhdm = NULL;
cleanup_par_vhdm:
    if (par_vhdm != NULL) {
        mvhd_close(par_vhdm);
    }
end:
    free(w2ku_path_buff);
    free(w2ru_path_buff);
    return vhdm;
}

MVHDMeta* mvhd_create_sparse(const char* path, MVHDGeom geom, int* err) {
    uint64_t size_in_bytes = mvhd_calc_size_bytes(&geom);
    return mvhd_create_sparse_diff(path, NULL, size_in_bytes, &geom, MVHD_BLOCK_LARGE, err);
}

MVHDMeta* mvhd_create_diff(const char* path, const char* par_path, int* err) {
    return mvhd_create_sparse_diff(path, par_path, 0, NULL, MVHD_BLOCK_LARGE, err);
}

MVHDMeta* mvhd_create_ex(MVHDCreationOptions options, int* err) {
    uint32_t geom_sector_size;
    switch (options.type)
    {
    case MVHD_TYPE_FIXED:
    case MVHD_TYPE_DYNAMIC:
        geom_sector_size = mvhd_calc_size_sectors(&(options.geometry));
        if ((options.size_in_bytes > 0 && (options.size_in_bytes % MVHD_SECTOR_SIZE) > 0)
            || (options.size_in_bytes > MVHD_MAX_SIZE_IN_BYTES)
            || (options.size_in_bytes == 0 && geom_sector_size == 0))
        {
            *err = MVHD_ERR_INVALID_SIZE;
            return NULL;
        }

        if (options.size_in_bytes > 0 && ((uint64_t)geom_sector_size * MVHD_SECTOR_SIZE) > options.size_in_bytes)
        {
            *err = MVHD_ERR_INVALID_GEOM;
            return NULL;
        }

        if (options.size_in_bytes == 0)
            options.size_in_bytes = (uint64_t)geom_sector_size * MVHD_SECTOR_SIZE;

        if (geom_sector_size == 0)
            options.geometry = mvhd_calculate_geometry(options.size_in_bytes);
        break;
    case MVHD_TYPE_DIFF:
        if (options.parent_path == NULL)
        {
            *err = MVHD_ERR_FILE;
            return NULL;
        }
        break;
    default:
        *err = MVHD_ERR_TYPE;
        return NULL;
    }

    if (options.path == NULL)
    {
        *err = MVHD_ERR_FILE;
        return NULL;
    }

    if (options.type != MVHD_TYPE_FIXED)
    {
        if (options.block_size_in_sectors == MVHD_BLOCK_DEFAULT)
            options.block_size_in_sectors = MVHD_BLOCK_LARGE;

        if (options.block_size_in_sectors != MVHD_BLOCK_LARGE && options.block_size_in_sectors != MVHD_BLOCK_SMALL)
        {
            *err = MVHD_ERR_INVALID_BLOCK_SIZE;
            return NULL;
        }
    }

    switch (options.type)
    {
    case MVHD_TYPE_FIXED:
        return mvhd_create_fixed_raw(options.path, NULL, options.size_in_bytes, &(options.geometry), err, options.progress_callback);
    case MVHD_TYPE_DYNAMIC:
        return mvhd_create_sparse_diff(options.path, NULL, options.size_in_bytes, &(options.geometry), options.block_size_in_sectors, err);
    case MVHD_TYPE_DIFF:
        return mvhd_create_sparse_diff(options.path, options.parent_path, 0, NULL, options.block_size_in_sectors, err);
    }

    return NULL; /* Make the compiler happy */
}
