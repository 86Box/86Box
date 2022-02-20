/**
 * \file
 * \brief Header and footer serialize/deserialize functions
 */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "minivhd_util.h"
#include "minivhd_internal.h"

/*  Read data from footer into the struct members, swapping endian where necessary
    Note: order matters here! We must read each field in the order the struct is in.
    Doing this may be less elegant than performing a memcpy to a packed struct, but
    it avoids potential data alignment issues, and the endian swapping allows us to
    use the fields directly. */

static void mvhd_next_buffer_to_struct(void* struct_memb, size_t memb_size, bool req_endian, uint8_t** buffer);
static void mvhd_next_struct_to_buffer(void* struct_memb, size_t memb_size, bool req_endian, uint8_t** buffer);

/**
 * \brief Get the next field from a buffer and store it in a struct member, converting endian if necessary
 *
 * \param [out] struct_memb struct member to save the field to
 * \param [in] memb_size the size of struct_memb, in bytes
 * \param [in] req_endian is the field a value that requires endian conversion (eg: uint16, uint32)
 * \param [in] buffer the buffer from which fields are read from. Will be advanced at the end of the function call
 */
static void mvhd_next_buffer_to_struct(void* struct_memb, size_t memb_size, bool req_endian, uint8_t** buffer) {
    memcpy(struct_memb, *buffer, memb_size);
    if (req_endian) {
        switch (memb_size) {
        case 2:
            *(uint16_t*)(struct_memb) = mvhd_from_be16(*(uint16_t*)(struct_memb));
            break;
        case 4:
            *(uint32_t*)(struct_memb) = mvhd_from_be32(*(uint32_t*)(struct_memb));
            break;
        case 8:
            *(uint64_t*)(struct_memb) = mvhd_from_be64(*(uint64_t*)(struct_memb));
            break;
        }
    }
    *buffer += memb_size;
}

/**
 * \brief Save a struct member into a buffer, converting endian if necessary
 *
 * \param [in] struct_memb struct member read from
 * \param [in] memb_size the size of struct_memb, in bytes
 * \param [in] req_endian is the field a value that requires endian conversion (eg: uint16, uint32)
 * \param [out] buffer the buffer from which struct member is saved to. Will be advanced at the end of the function call
 */
static void mvhd_next_struct_to_buffer(void* struct_memb, size_t memb_size, bool req_endian, uint8_t** buffer) {
    uint8_t *buf_ptr = *buffer;
    memcpy(buf_ptr, struct_memb, memb_size);
    if (req_endian) {
        switch (memb_size) {
        case 2:
            *((uint16_t*)buf_ptr) = mvhd_to_be16(*(uint16_t*)(struct_memb));
            break;
        case 4:
            *((uint32_t*)buf_ptr) = mvhd_to_be32(*(uint32_t*)(struct_memb));
            break;
        case 8:
            *((uint64_t*)buf_ptr) = mvhd_to_be64(*(uint64_t*)(struct_memb));
            break;
        }
    }
    buf_ptr += memb_size;
    *buffer = buf_ptr;
}

void mvhd_buffer_to_footer(MVHDFooter* footer, uint8_t* buffer) {
    uint8_t* buff_ptr = buffer;
    mvhd_next_buffer_to_struct(&footer->cookie, sizeof footer->cookie, false, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->features, sizeof footer->features, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->fi_fmt_vers, sizeof footer->fi_fmt_vers, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->data_offset, sizeof footer->data_offset, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->timestamp, sizeof footer->timestamp, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->cr_app, sizeof footer->cr_app, false, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->cr_vers, sizeof footer->cr_vers, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->cr_host_os, sizeof footer->cr_host_os, false, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->orig_sz, sizeof footer->orig_sz, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->curr_sz, sizeof footer->curr_sz, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->geom.cyl, sizeof footer->geom.cyl, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->geom.heads, sizeof footer->geom.heads, false, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->geom.spt, sizeof footer->geom.spt, false, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->disk_type, sizeof footer->disk_type, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->checksum, sizeof footer->checksum, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->uuid, sizeof footer->uuid, false, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->saved_st, sizeof footer->saved_st, false, &buff_ptr);
    mvhd_next_buffer_to_struct(&footer->reserved, sizeof footer->reserved, false, &buff_ptr);
}

void mvhd_footer_to_buffer(MVHDFooter* footer, uint8_t* buffer) {
    uint8_t* buff_ptr = buffer;
    mvhd_next_struct_to_buffer(&footer->cookie, sizeof footer->cookie, false, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->features, sizeof footer->features, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->fi_fmt_vers, sizeof footer->fi_fmt_vers, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->data_offset, sizeof footer->data_offset, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->timestamp, sizeof footer->timestamp, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->cr_app, sizeof footer->cr_app, false, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->cr_vers, sizeof footer->cr_vers, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->cr_host_os, sizeof footer->cr_host_os, false, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->orig_sz, sizeof footer->orig_sz, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->curr_sz, sizeof footer->curr_sz, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->geom.cyl, sizeof footer->geom.cyl, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->geom.heads, sizeof footer->geom.heads, false, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->geom.spt, sizeof footer->geom.spt, false, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->disk_type, sizeof footer->disk_type, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->checksum, sizeof footer->checksum, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->uuid, sizeof footer->uuid, false, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->saved_st, sizeof footer->saved_st, false, &buff_ptr);
    mvhd_next_struct_to_buffer(&footer->reserved, sizeof footer->reserved, false, &buff_ptr);
}

void mvhd_buffer_to_header(MVHDSparseHeader* header, uint8_t* buffer) {
    uint8_t* buff_ptr = buffer;
    mvhd_next_buffer_to_struct(&header->cookie, sizeof header->cookie, false, &buff_ptr);
    mvhd_next_buffer_to_struct(&header->data_offset, sizeof header->data_offset, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&header->bat_offset, sizeof header->bat_offset, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&header->head_vers, sizeof header->head_vers, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&header->max_bat_ent, sizeof header->max_bat_ent, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&header->block_sz, sizeof header->block_sz, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&header->checksum, sizeof header->checksum, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&header->par_uuid, sizeof header->par_uuid, false, &buff_ptr);
    mvhd_next_buffer_to_struct(&header->par_timestamp, sizeof header->par_timestamp, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&header->reserved_1, sizeof header->reserved_1, true, &buff_ptr);
    mvhd_next_buffer_to_struct(&header->par_utf16_name, sizeof header->par_utf16_name, false, &buff_ptr);
    for (int i = 0; i < 8; i++) {
        mvhd_next_buffer_to_struct(&header->par_loc_entry[i].plat_code, sizeof header->par_loc_entry[i].plat_code, true, &buff_ptr);
        mvhd_next_buffer_to_struct(&header->par_loc_entry[i].plat_data_space, sizeof header->par_loc_entry[i].plat_data_space, true, &buff_ptr);
        mvhd_next_buffer_to_struct(&header->par_loc_entry[i].plat_data_len, sizeof header->par_loc_entry[i].plat_data_len, true, &buff_ptr);
        mvhd_next_buffer_to_struct(&header->par_loc_entry[i].reserved, sizeof header->par_loc_entry[i].reserved, true, &buff_ptr);
        mvhd_next_buffer_to_struct(&header->par_loc_entry[i].plat_data_offset, sizeof header->par_loc_entry[i].plat_data_offset, true, &buff_ptr);
    }
    mvhd_next_buffer_to_struct(&header->reserved_2, sizeof header->reserved_2, false, &buff_ptr);
}

void mvhd_header_to_buffer(MVHDSparseHeader* header, uint8_t* buffer) {
    uint8_t* buff_ptr = buffer;
    mvhd_next_struct_to_buffer(&header->cookie, sizeof header->cookie, false, &buff_ptr);
    mvhd_next_struct_to_buffer(&header->data_offset, sizeof header->data_offset, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&header->bat_offset, sizeof header->bat_offset, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&header->head_vers, sizeof header->head_vers, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&header->max_bat_ent, sizeof header->max_bat_ent, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&header->block_sz, sizeof header->block_sz, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&header->checksum, sizeof header->checksum, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&header->par_uuid, sizeof header->par_uuid, false, &buff_ptr);
    mvhd_next_struct_to_buffer(&header->par_timestamp, sizeof header->par_timestamp, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&header->reserved_1, sizeof header->reserved_1, true, &buff_ptr);
    mvhd_next_struct_to_buffer(&header->par_utf16_name, sizeof header->par_utf16_name, false, &buff_ptr);
    for (int i = 0; i < 8; i++) {
        mvhd_next_struct_to_buffer(&header->par_loc_entry[i].plat_code, sizeof header->par_loc_entry[i].plat_code, true, &buff_ptr);
        mvhd_next_struct_to_buffer(&header->par_loc_entry[i].plat_data_space, sizeof header->par_loc_entry[i].plat_data_space, true, &buff_ptr);
        mvhd_next_struct_to_buffer(&header->par_loc_entry[i].plat_data_len, sizeof header->par_loc_entry[i].plat_data_len, true, &buff_ptr);
        mvhd_next_struct_to_buffer(&header->par_loc_entry[i].reserved, sizeof header->par_loc_entry[i].reserved, true, &buff_ptr);
        mvhd_next_struct_to_buffer(&header->par_loc_entry[i].plat_data_offset, sizeof header->par_loc_entry[i].plat_data_offset, true, &buff_ptr);
    }
    mvhd_next_struct_to_buffer(&header->reserved_2, sizeof header->reserved_2, false, &buff_ptr);
}
