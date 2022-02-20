/**
 * \file
 * \brief Sector reading and writing implementations
 */

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include <stdlib.h>
#include <string.h>
#include "minivhd_internal.h"
#include "minivhd_util.h"

/* The following bit array macros adapted from
   http://www.mathcs.emory.edu/~cheung/Courses/255/Syllabus/1-C-intro/bit-array.html */

#define VHD_SETBIT(A,k)     ( A[(k/8)] |= (0x80 >> (k%8)) )
#define VHD_CLEARBIT(A,k)   ( A[(k/8)] &= ~(0x80 >> (k%8)) )
#define VHD_TESTBIT(A,k)    ( A[(k/8)] & (0x80 >> (k%8)) )

static inline void mvhd_check_sectors(uint32_t offset, int num_sectors, uint32_t total_sectors, int* transfer_sect, int* trunc_sect);
static void mvhd_read_sect_bitmap(MVHDMeta* vhdm, int blk);
static void mvhd_write_bat_entry(MVHDMeta* vhdm, int blk);
static void mvhd_create_block(MVHDMeta* vhdm, int blk);
static void mvhd_write_curr_sect_bitmap(MVHDMeta* vhdm);

/**
 * \brief Check that we will not be overflowing buffers
 *
 * \param [in] offset The offset from which we are beginning from
 * \param [in] num_sectors The number of sectors which we desire to read/write
 * \param [in] total_sectors The total number of sectors available
 * \param [out] transfer_sect The number of sectors to actually write.
 * This may be lower than num_sectors if offset + num_sectors >= total_sectors
 * \param [out] trunc_sectors The number of sectors truncated if transfer_sectors < num_sectors
 */
static inline void mvhd_check_sectors(uint32_t offset, int num_sectors, uint32_t total_sectors, int* transfer_sect, int* trunc_sect) {
    *transfer_sect = num_sectors;
    *trunc_sect = 0;
    if ((total_sectors - offset) < (uint32_t)*transfer_sect) {
        *transfer_sect = total_sectors - offset;
        *trunc_sect = num_sectors - *transfer_sect;
    }
}

void mvhd_write_empty_sectors(FILE* f, int sector_count) {
    uint8_t zero_bytes[MVHD_SECTOR_SIZE] = {0};
    for (int i = 0; i < sector_count; i++) {
        fwrite(zero_bytes, sizeof zero_bytes, 1, f);
    }
}

/**
 * \brief Read the sector bitmap for a block.
 *
 * If the block is sparse, the sector bitmap in memory will be
 * zeroed. Otherwise, the sector bitmap is read from the VHD file.
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] blk The block for which to read the sector bitmap from
 */
static void mvhd_read_sect_bitmap(MVHDMeta* vhdm, int blk) {
    if (vhdm->block_offset[blk] != MVHD_SPARSE_BLK) {
        mvhd_fseeko64(vhdm->f, (uint64_t)vhdm->block_offset[blk] * MVHD_SECTOR_SIZE, SEEK_SET);
        fread(vhdm->bitmap.curr_bitmap, vhdm->bitmap.sector_count * MVHD_SECTOR_SIZE, 1, vhdm->f);
    } else {
        memset(vhdm->bitmap.curr_bitmap, 0, vhdm->bitmap.sector_count * MVHD_SECTOR_SIZE);
    }
    vhdm->bitmap.curr_block = blk;
}

/**
 * \brief Write the current sector bitmap in memory to file
 *
 * \param [in] vhdm MiniVHD data structure
 */
static void mvhd_write_curr_sect_bitmap(MVHDMeta* vhdm) {
    if (vhdm->bitmap.curr_block >= 0) {
        int64_t abs_offset = (int64_t)vhdm->block_offset[vhdm->bitmap.curr_block] * MVHD_SECTOR_SIZE;
        mvhd_fseeko64(vhdm->f, abs_offset, SEEK_SET);
        fwrite(vhdm->bitmap.curr_bitmap, MVHD_SECTOR_SIZE, vhdm->bitmap.sector_count, vhdm->f);
    }
}

/**
 * \brief Write block offset from memory into file
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] blk The block for which to write the offset for
 */
static void mvhd_write_bat_entry(MVHDMeta* vhdm, int blk) {
    uint64_t table_offset = vhdm->sparse.bat_offset + ((uint64_t)blk * sizeof *vhdm->block_offset);
    uint32_t offset = mvhd_to_be32(vhdm->block_offset[blk]);
    mvhd_fseeko64(vhdm->f, table_offset, SEEK_SET);
    fwrite(&offset, sizeof offset, 1, vhdm->f);
}

/**
 * \brief Create an empty block in a sparse or differencing VHD image
 *
 * VHD images store data in blocks, which are typically 4096 sectors in size
 * (~2MB). These blocks may be stored on disk in any order. Blocks are created
 * on demand when required.
 *
 * This function creates new, empty blocks, by replacing the footer at the end of the file
 * and then re-inserting the footer at the new file end. The BAT table entry for the
 * new block is updated with the new offset.
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] blk The block number to create
 */
static void mvhd_create_block(MVHDMeta* vhdm, int blk) {
    uint8_t footer[MVHD_FOOTER_SIZE];
    /* Seek to where the footer SHOULD be */
    mvhd_fseeko64(vhdm->f, -MVHD_FOOTER_SIZE, SEEK_END);
    fread(footer, sizeof footer, 1, vhdm->f);
    mvhd_fseeko64(vhdm->f, -MVHD_FOOTER_SIZE, SEEK_END);
    if (!mvhd_is_conectix_str(footer)) {
        /* Oh dear. We use the header instead, since something has gone wrong at the footer */
        mvhd_fseeko64(vhdm->f, 0, SEEK_SET);
        fread(footer, sizeof footer, 1, vhdm->f);
        mvhd_fseeko64(vhdm->f, 0, SEEK_END);
    }
    int64_t abs_offset = mvhd_ftello64(vhdm->f);
    if (abs_offset % MVHD_SECTOR_SIZE != 0) {
        /* Yikes! We're supposed to be on a sector boundary. Add some padding */
        int64_t padding_amount = (int64_t)MVHD_SECTOR_SIZE - (abs_offset % MVHD_SECTOR_SIZE);
        uint8_t zero_byte = 0;
        for (int i = 0; i < padding_amount; i++) {
            fwrite(&zero_byte, sizeof zero_byte, 1, vhdm->f);
        }
        abs_offset += padding_amount;
    }
    uint32_t sect_offset = (uint32_t)(abs_offset / MVHD_SECTOR_SIZE);
    int blk_size_sectors = vhdm->sparse.block_sz / MVHD_SECTOR_SIZE;
    mvhd_write_empty_sectors(vhdm->f, vhdm->bitmap.sector_count + blk_size_sectors);
    /* Add a bit of padding. That's what Windows appears to do, although it's not strictly necessary... */
    mvhd_write_empty_sectors(vhdm->f, 5);
    /* And we finish with the footer */
    fwrite(footer, sizeof footer, 1, vhdm->f);
    /* We no longer have a sparse block. Update that BAT! */
    vhdm->block_offset[blk] = sect_offset;
    mvhd_write_bat_entry(vhdm, blk);
}

int mvhd_fixed_read(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* out_buff) {
    int64_t addr;
    int transfer_sectors, truncated_sectors;
    uint32_t total_sectors = (uint32_t)(vhdm->footer.curr_sz / MVHD_SECTOR_SIZE);
    mvhd_check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);
    addr = (int64_t)offset * MVHD_SECTOR_SIZE;
    mvhd_fseeko64(vhdm->f, addr, SEEK_SET);
    fread(out_buff, transfer_sectors*MVHD_SECTOR_SIZE, 1, vhdm->f);
    return truncated_sectors;
}

int mvhd_sparse_read(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* out_buff) {
    int transfer_sectors, truncated_sectors;
    uint32_t total_sectors = (uint32_t)(vhdm->footer.curr_sz / MVHD_SECTOR_SIZE);
    mvhd_check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);
    uint8_t* buff = (uint8_t*)out_buff;
    int64_t addr;
    uint32_t s, ls;
    int blk, prev_blk, sib;
    ls = offset + transfer_sectors;
    prev_blk = -1;
    for (s = offset; s < ls; s++) {
        blk = s / vhdm->sect_per_block;
        sib = s % vhdm->sect_per_block;
        if (blk != prev_blk) {
            prev_blk = blk;
            if (vhdm->bitmap.curr_block != blk) {
                mvhd_read_sect_bitmap(vhdm, blk);
                mvhd_fseeko64(vhdm->f, (uint64_t)sib * MVHD_SECTOR_SIZE, SEEK_CUR);
            } else {
                addr = ((int64_t)vhdm->block_offset[blk] + vhdm->bitmap.sector_count + sib) * MVHD_SECTOR_SIZE;
                mvhd_fseeko64(vhdm->f, addr, SEEK_SET);
            }
        }
        if (VHD_TESTBIT(vhdm->bitmap.curr_bitmap, sib)) {
            fread(buff, MVHD_SECTOR_SIZE, 1, vhdm->f);
        } else {
            memset(buff, 0, MVHD_SECTOR_SIZE);
            mvhd_fseeko64(vhdm->f, MVHD_SECTOR_SIZE, SEEK_CUR);
        }
        buff += MVHD_SECTOR_SIZE;
    }
    return truncated_sectors;
}

int mvhd_diff_read(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* out_buff) {
    int transfer_sectors, truncated_sectors;
    uint32_t total_sectors = (uint32_t)(vhdm->footer.curr_sz / MVHD_SECTOR_SIZE);
    mvhd_check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);
    uint8_t* buff = (uint8_t*)out_buff;
    MVHDMeta* curr_vhdm = vhdm;
    uint32_t s, ls;
    int blk, sib;
    ls = offset + transfer_sectors;
    for (s = offset; s < ls; s++) {
        while (curr_vhdm->footer.disk_type == MVHD_TYPE_DIFF) {
            blk = s / curr_vhdm->sect_per_block;
            sib = s % curr_vhdm->sect_per_block;
            if (curr_vhdm->bitmap.curr_block != blk) {
                mvhd_read_sect_bitmap(curr_vhdm, blk);
            }
            if (!VHD_TESTBIT(curr_vhdm->bitmap.curr_bitmap, sib)) {
                curr_vhdm = curr_vhdm->parent;
            } else { break; }
        }
        /* We handle actual sector reading using the fixed or sparse functions,
           as a differencing VHD is also a sparse VHD */
        if (curr_vhdm->footer.disk_type == MVHD_TYPE_DIFF || curr_vhdm->footer.disk_type == MVHD_TYPE_DYNAMIC) {
            mvhd_sparse_read(curr_vhdm, s, 1, buff);
        } else {
            mvhd_fixed_read(curr_vhdm, s, 1, buff);
        }
        curr_vhdm = vhdm;
        buff += MVHD_SECTOR_SIZE;
    }
    return truncated_sectors;
}

int mvhd_fixed_write(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* in_buff) {
    int64_t addr;
    int transfer_sectors, truncated_sectors;
    uint32_t total_sectors = (uint32_t)(vhdm->footer.curr_sz / MVHD_SECTOR_SIZE);
    mvhd_check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);
    addr = (int64_t)offset * MVHD_SECTOR_SIZE;
    mvhd_fseeko64(vhdm->f, addr, SEEK_SET);
    fwrite(in_buff, transfer_sectors*MVHD_SECTOR_SIZE, 1, vhdm->f);
    return truncated_sectors;
}

int mvhd_sparse_diff_write(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* in_buff) {
    int transfer_sectors, truncated_sectors;
    uint32_t total_sectors = (uint32_t)(vhdm->footer.curr_sz / MVHD_SECTOR_SIZE);
    mvhd_check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);
    uint8_t* buff = (uint8_t*)in_buff;
    int64_t addr;
    uint32_t s, ls;
    int blk, prev_blk, sib;
    ls = offset + transfer_sectors;
    prev_blk = -1;
    for (s = offset; s < ls; s++) {
        blk = s / vhdm->sect_per_block;
        sib = s % vhdm->sect_per_block;
        if (vhdm->bitmap.curr_block != blk && prev_blk >= 0) {
            /* Write the sector bitmap for the previous block, before we replace it. */
            mvhd_write_curr_sect_bitmap(vhdm);
        }
        if (vhdm->block_offset[blk] == MVHD_SPARSE_BLK) {
            /* "read" the sector bitmap first, before creating a new block, as the bitmap will be
               zero either way */
            mvhd_read_sect_bitmap(vhdm, blk);
            mvhd_create_block(vhdm, blk);
        }
        if (blk != prev_blk) {
            if (vhdm->bitmap.curr_block != blk) {
                mvhd_read_sect_bitmap(vhdm, blk);
                mvhd_fseeko64(vhdm->f, (uint64_t)sib * MVHD_SECTOR_SIZE, SEEK_CUR);
            } else {
                addr = ((int64_t)vhdm->block_offset[blk] + vhdm->bitmap.sector_count + sib) * MVHD_SECTOR_SIZE;
                mvhd_fseeko64(vhdm->f, addr, SEEK_SET);
            }
            prev_blk = blk;
        }
        fwrite(buff, MVHD_SECTOR_SIZE, 1, vhdm->f);
        VHD_SETBIT(vhdm->bitmap.curr_bitmap, sib);
        buff += MVHD_SECTOR_SIZE;
    }
    /* And write the sector bitmap for the last block we visited to disk */
    mvhd_write_curr_sect_bitmap(vhdm);
    return truncated_sectors;
}

int mvhd_noop_write(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* in_buff) {
    return 0;
}
