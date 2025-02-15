/*
 * MiniVHD	Minimalist VHD implementation in C.
 *
 *		This file is part of the MiniVHD Project.
 *
 *		Sector reading and writing implementations.
 *
 * Version:	@(#)io.c	1.0.3	2021/04/16
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
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "minivhd.h"
#include "internal.h"
#define HAVE_STDARG_H

#include "cpu.h"
#include <86box/86box.h>

/*
 * The following bit array macros adapted from:
 *
 * http://www.mathcs.emory.edu/~cheung/Courses/255/Syllabus/1-C-intro/bit-array.html
 */
#define VHD_SETBIT(A,k)     ( A[(k>>3)] |= (0x80 >> (k&7)) )
#define VHD_CLEARBIT(A,k)   ( A[(k>>3)] &= ~(0x80 >> (k&7)) )
#define VHD_TESTBIT(A,k)    ( A[(k>>3)] & (0x80 >> (k&7)) )

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
static inline void
check_sectors(uint32_t offset, int num_sectors, uint32_t total_sectors, int *transfer_sect, int *trunc_sect)
{
    *transfer_sect = num_sectors;
    *trunc_sect = 0;

    if ((total_sectors - offset) < ((uint32_t) *transfer_sect)) {
        *transfer_sect = total_sectors - offset;
        *trunc_sect = num_sectors - *transfer_sect;
    }
}

bool
mvhd_write_empty_sectors(FILE *f, int sector_count)
{
    uint8_t zero_bytes[MVHD_SECTOR_SIZE] = {0};

    for (int i = 0; i < sector_count; i++) {
        if (!fwrite(zero_bytes, sizeof zero_bytes, 1, f))
            return 0;
    }

    fflush(f);
    return 1;
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
static void
read_sect_bitmap(MVHDMeta *vhdm, int blk)
{
    if (vhdm->block_offset[blk] != MVHD_SPARSE_BLK) {
        mvhd_fseeko64(vhdm->f, (uint64_t)vhdm->block_offset[blk] * MVHD_SECTOR_SIZE, SEEK_SET);
        if (!fread(vhdm->bitmap.curr_bitmap, vhdm->bitmap.sector_count * MVHD_SECTOR_SIZE, 1, vhdm->f))
            vhdm->error = 1;
    } else
        memset(vhdm->bitmap.curr_bitmap, 0, vhdm->bitmap.sector_count * MVHD_SECTOR_SIZE);

    vhdm->bitmap.curr_block = blk;
}

/**
 * \brief Write the current sector bitmap in memory to file
 *
 * \param [in] vhdm MiniVHD data structure
 */
static void
write_curr_sect_bitmap(MVHDMeta* vhdm)
{
    if (vhdm->bitmap.curr_block >= 0) {
        int64_t abs_offset = (int64_t)vhdm->block_offset[vhdm->bitmap.curr_block] * MVHD_SECTOR_SIZE;
        if (mvhd_fseeko64(vhdm->f, abs_offset, SEEK_SET) == -1)
            vhdm->error = 1;
        if (!fwrite(vhdm->bitmap.curr_bitmap, MVHD_SECTOR_SIZE, vhdm->bitmap.sector_count, vhdm->f))
            vhdm->error = 1;
    }
}

/**
 * \brief Write block offset from memory into file
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] blk The block for which to write the offset for
 */
static void
write_bat_entry(MVHDMeta *vhdm, int blk)
{
    uint64_t table_offset = vhdm->sparse.bat_offset + ((uint64_t)blk * sizeof *vhdm->block_offset);
    uint32_t offset = mvhd_to_be32(vhdm->block_offset[blk]);

    if (mvhd_fseeko64(vhdm->f, table_offset, SEEK_SET) == -1)
        vhdm->error = 1;
    if (!fwrite(&offset, sizeof offset, 1, vhdm->f))
        vhdm->error = 1;
    fflush(vhdm->f);
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
static void
create_block(MVHDMeta *vhdm, int blk)
{
    uint8_t footer[MVHD_FOOTER_SIZE] = { 0 };

    /* Seek to where the footer SHOULD be */
    mvhd_fseeko64(vhdm->f, -MVHD_FOOTER_SIZE, SEEK_END);
    (void) !fread(footer, sizeof footer, 1, vhdm->f);
    mvhd_fseeko64(vhdm->f, -MVHD_FOOTER_SIZE, SEEK_END);

    if (!mvhd_is_conectix_str(footer)) {
        /* Oh dear. We use the header instead, since something has gone wrong at the footer */
        mvhd_fseeko64(vhdm->f, 0, SEEK_SET);
        if (!fread(footer, sizeof footer, 1, vhdm->f))
            vhdm->error = 1;
        mvhd_fseeko64(vhdm->f, 0, SEEK_END);
    }

    int64_t abs_offset = mvhd_ftello64(vhdm->f);
    if ((abs_offset % MVHD_SECTOR_SIZE) != 0) {
        /* Yikes! We're supposed to be on a sector boundary. Add some padding */
        int64_t padding_amount = ((int64_t) MVHD_SECTOR_SIZE) - (abs_offset % MVHD_SECTOR_SIZE);
        uint8_t zero_byte = 0;
        for (int i = 0; i < padding_amount; i++) {
            if (!fwrite(&zero_byte, sizeof zero_byte, 1, vhdm->f))
                vhdm->error = 1;
        }
        abs_offset += padding_amount;
    }

    uint32_t sect_offset = (uint32_t)(abs_offset / MVHD_SECTOR_SIZE);
    int blk_size_sectors = vhdm->sparse.block_sz / MVHD_SECTOR_SIZE;
    if (!mvhd_write_empty_sectors(vhdm->f, vhdm->bitmap.sector_count + blk_size_sectors))
        vhdm->error = 1;

    /* Add a bit of padding. That's what Windows appears to do, although it's not strictly necessary... */
    if (!mvhd_write_empty_sectors(vhdm->f, 5))
        vhdm->error = 1;

    /* And we finish with the footer */
    if (!fwrite(footer, sizeof footer, 1, vhdm->f))
        vhdm->error = 1;

    /* We no longer have a sparse block. Update that BAT! */
    vhdm->block_offset[blk] = sect_offset;
    write_bat_entry(vhdm, blk);

    fflush(vhdm->f);
}

int
mvhd_fixed_read(MVHDMeta *vhdm, uint32_t offset, int num_sectors, void *out_buff) {
    int64_t addr = 0ULL;
    int transfer_sectors = 0;
    int truncated_sectors = 0;
    uint32_t total_sectors = (uint32_t)(vhdm->footer.curr_sz / MVHD_SECTOR_SIZE);

    check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);

    addr = ((int64_t) offset) * MVHD_SECTOR_SIZE;
    if (mvhd_fseeko64(vhdm->f, addr, SEEK_SET) == -1)
        vhdm->error = 1;
    if (!fread(out_buff, transfer_sectors * MVHD_SECTOR_SIZE, 1, vhdm->f) && !feof(vhdm->f))
        vhdm->error = 1;

    return truncated_sectors;
}

int
mvhd_sparse_read(MVHDMeta *vhdm, uint32_t offset, int num_sectors, void *out_buff)
{
    int transfer_sectors = 0;
    int truncated_sectors;
    uint32_t total_sectors = (uint32_t)(vhdm->footer.curr_sz / MVHD_SECTOR_SIZE);

    check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);

    uint8_t* buff = (uint8_t*)out_buff;
    int64_t addr = 0ULL;
    uint32_t s = 0;
    uint32_t ls = 0;
    int blk = 0;
    int prev_blk = -1;
    int sib = 0;
    ls = offset + transfer_sectors;

    for (s = offset; s < ls; s++) {
        blk = s / vhdm->sect_per_block;
        sib = s % vhdm->sect_per_block;
        if (blk != prev_blk) {
            prev_blk = blk;
            if (vhdm->bitmap.curr_block != blk) {
                read_sect_bitmap(vhdm, blk);
                if (mvhd_fseeko64(vhdm->f, (uint64_t)sib * MVHD_SECTOR_SIZE, SEEK_CUR) == -1)
                    vhdm->error = 1;
            } else {
                addr = (((int64_t) vhdm->block_offset[blk]) + vhdm->bitmap.sector_count + sib) *
                       MVHD_SECTOR_SIZE;
                if (mvhd_fseeko64(vhdm->f, addr, SEEK_SET) == -1)
                    vhdm->error = 1;
            }
        }

        if (VHD_TESTBIT(vhdm->bitmap.curr_bitmap, sib)) {
            if (!fread(buff, MVHD_SECTOR_SIZE, 1, vhdm->f) && !feof(vhdm->f))
                vhdm->error = 1;
        } else {
            memset(buff, 0, MVHD_SECTOR_SIZE);
            mvhd_fseeko64(vhdm->f, MVHD_SECTOR_SIZE, SEEK_CUR);
        }
        buff += MVHD_SECTOR_SIZE;
    }

    return truncated_sectors;
}

int
mvhd_diff_read(MVHDMeta *vhdm, uint32_t offset, int num_sectors, void *out_buff)
{
    int transfer_sectors = 0;
    int truncated_sectors = 0;
    uint32_t total_sectors = (uint32_t)(vhdm->footer.curr_sz / MVHD_SECTOR_SIZE);

    check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);

    uint8_t *buff = (uint8_t*)out_buff;
    MVHDMeta *curr_vhdm = vhdm;
    uint32_t s = 0;
    uint32_t ls = 0;
    int blk = 0;
    int sib = 0;
    ls = offset + transfer_sectors;

    for (s = offset; s < ls; s++) {
        while (curr_vhdm->footer.disk_type == MVHD_TYPE_DIFF) {
            blk = s / curr_vhdm->sect_per_block;
            sib = s % curr_vhdm->sect_per_block;
            if (curr_vhdm->bitmap.curr_block != blk) {
                read_sect_bitmap(curr_vhdm, blk);
            }
            if (!VHD_TESTBIT(curr_vhdm->bitmap.curr_bitmap, sib)) {
                curr_vhdm = curr_vhdm->parent;
            } else { break; }
        }

        /* We handle actual sector reading using the fixed or sparse functions,
           as a differencing VHD is also a sparse VHD */
        if ((curr_vhdm->footer.disk_type == MVHD_TYPE_DIFF) ||
            (curr_vhdm->footer.disk_type == MVHD_TYPE_DYNAMIC))
            mvhd_sparse_read(curr_vhdm, s, 1, buff);
        else
            mvhd_fixed_read(curr_vhdm, s, 1, buff);
        if (curr_vhdm->error) {
            curr_vhdm->error = 0;
            vhdm->error = 1;
        }

        curr_vhdm = vhdm;
        buff += MVHD_SECTOR_SIZE;
    }

    return truncated_sectors;
}

int
mvhd_fixed_write(MVHDMeta *vhdm, uint32_t offset, int num_sectors, void *in_buff)
{
    int64_t addr = 0ULL;
    int transfer_sectors = 0;
    int truncated_sectors = 0;
    uint32_t total_sectors = (uint32_t)(vhdm->footer.curr_sz / MVHD_SECTOR_SIZE);

    check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);

    addr = (int64_t)offset * MVHD_SECTOR_SIZE;
    if (mvhd_fseeko64(vhdm->f, addr, SEEK_SET) == -1)
        vhdm->error = 1;
    if (!fwrite(in_buff, transfer_sectors * MVHD_SECTOR_SIZE, 1, vhdm->f))
        vhdm->error = 1;
    fflush(vhdm->f);

    return truncated_sectors;
}

int
mvhd_sparse_diff_write(MVHDMeta *vhdm, uint32_t offset, int num_sectors, void *in_buff)
{
    int transfer_sectors = 0;
    int truncated_sectors = 0;
    uint32_t total_sectors = (uint32_t)(vhdm->footer.curr_sz / MVHD_SECTOR_SIZE);

    check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);

    uint8_t* buff = (uint8_t *) in_buff;
    int64_t addr = 0ULL;
    uint32_t s = 0;
    uint32_t ls = 0;
    int blk = 0;
    int prev_blk = -1;
    int sib = 0;
    ls = offset + transfer_sectors;

    if (offset < total_sectors) {
        for (s = offset; s < ls; s++) {
            blk = s / vhdm->sect_per_block;
            sib = s % vhdm->sect_per_block;
            if (vhdm->bitmap.curr_block != blk && prev_blk >= 0) {
                /* Write the sector bitmap for the previous block, before we replace it. */
                write_curr_sect_bitmap(vhdm);
            }

            if (vhdm->block_offset[blk] == MVHD_SPARSE_BLK) {
                /* "read" the sector bitmap first, before creating a new block, as the bitmap will be
                   zero either way */
                read_sect_bitmap(vhdm, blk);
                create_block(vhdm, blk);
            }

            if (blk != prev_blk) {
                if (vhdm->bitmap.curr_block != blk) {
                    read_sect_bitmap(vhdm, blk);
                    if (mvhd_fseeko64(vhdm->f, (uint64_t)sib * MVHD_SECTOR_SIZE, SEEK_CUR) == -1)
                        vhdm->error = 1;
                } else {
                    addr = (((int64_t) vhdm->block_offset[blk]) + vhdm->bitmap.sector_count + sib) *
                           MVHD_SECTOR_SIZE;
                    if (mvhd_fseeko64(vhdm->f, addr, SEEK_SET) == -1)
                        vhdm->error = 1;
                }
                prev_blk = blk;
            }

            if (!fwrite(buff, MVHD_SECTOR_SIZE, 1, vhdm->f))
                vhdm->error = 1;
            VHD_SETBIT(vhdm->bitmap.curr_bitmap, sib);
            buff += MVHD_SECTOR_SIZE;
        }
    }

    /* And write the sector bitmap for the last block we visited to disk */
    write_curr_sect_bitmap(vhdm);

    fflush(vhdm->f);

    return truncated_sectors;
}

int
mvhd_noop_write(MVHDMeta *vhdm, uint32_t offset, int num_sectors, void *in_buff)
{
    (void) vhdm;
    (void) offset;
    (void) num_sectors;
    (void) in_buff;

    return 0;
}
