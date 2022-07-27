#ifndef MINIVHD_IO_H
#define MINIVHD_IO_H
#include "minivhd.h"

/**
 * \brief Write zero filled sectors to file.
 *
 * Note, the caller should set the file position before calling this
 * function for correct operation.
 *
 * \param [in] f File to write sectors to
 * \param [in] sector_count The number of sectors to write
 */
void mvhd_write_empty_sectors(FILE* f, int sector_count);

/**
 * \brief Read a fixed VHD image
 *
 * Fixed VHD images are essentially raw image files with a footer tacked on
 * the end. They are therefore straightforward to write
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to read from
 * \param [in] num_sectors The desired number of sectors to read
 * \param [out] out_buff An output buffer to store read sectors. Must be
 * large enough to hold num_sectors worth of sectors.
 *
 * \retval 0 num_sectors were read from file
 * \retval >0 < num_sectors were read from file
 */
int mvhd_fixed_read(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* out_buff);

/**
 * \brief Read a sparse VHD image
 *
 * Sparse, or dynamic images are VHD images that grow as data is written to them.
 *
 * This function implements the logic to read sectors from the file, taking into
 * account the fact that blocks may be stored on disk in any order, and that the
 * read could cross block boundaries.
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to read from
 * \param [in] num_sectors The desired number of sectors to read
 * \param [out] out_buff An output buffer to store read sectors. Must be
 * large enough to hold num_sectors worth of sectors.
 *
 * \retval 0 num_sectors were read from file
 * \retval >0 < num_sectors were read from file
 */
int mvhd_sparse_read(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* out_buff);

/**
 * \brief Read a differencing VHD image
 *
 * Differencing images are a variant of a sparse image. They contain the grow-on-demand
 * properties of sparse images, but also reference a parent image. Data is read from the
 * child image only if it is newer than the data stored in the parent image.
 *
 * This function implements the logic to read sectors from the child, or a parent image.
 * Differencing images may have a differencing image as a parent, creating a chain of images.
 * There is no theoretical chain length limit, although I do not consider long chains to be
 * advisable. Verifying the parent-child relationship is not very robust.
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to read from
 * \param [in] num_sectors The desired number of sectors to read
 * \param [out] out_buff An output buffer to store read sectors. Must be
 * large enough to hold num_sectors worth of sectors.
 *
 * \retval 0 num_sectors were read from file
 * \retval >0 < num_sectors were read from file
 */
int mvhd_diff_read(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* out_buff);

/**
 * \brief Write to a fixed VHD image
 *
 * Fixed VHD images are essentially raw image files with a footer tacked on
 * the end. They are therefore straightforward to write
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to write to
 * \param [in] num_sectors The desired number of sectors to write
 * \param [in] in_buff A source buffer to write sectors from. Must be
 * large enough to hold num_sectors worth of sectors.
 *
 * \retval 0 num_sectors were written to file
 * \retval >0 < num_sectors were written to file
 */
int mvhd_fixed_write(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* in_buff);

/**
 * \brief Write to a sparse or differencing VHD image
 *
 * Sparse, or dynamic images are VHD images that grow as data is written to them.
 *
 * Differencing images are a variant of a sparse image. They contain the grow-on-demand
 * properties of sparse images, but also reference a parent image. Data is always written
 * to the child image. This makes writing to differencing images essentially identical to
 * writing to sparse images, hence they use the same function.
 *
 * This function implements the logic to write sectors to the file, taking into
 * account the fact that blocks may be stored on disk in any order, and that the
 * write operation could cross block boundaries.
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to write to
 * \param [in] num_sectors The desired number of sectors to write
 * \param [in] in_buff A source buffer to write sectors from. Must be
 * large enough to hold num_sectors worth of sectors.
 *
 * \retval 0 num_sectors were written to file
 * \retval >0 < num_sectors were written to file
 */
int mvhd_sparse_diff_write(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* in_buff);

/**
 * \brief A no-op function to "write" to read-only VHD images
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to write to
 * \param [in] num_sectors The desired number of sectors to write
 * \param [in] in_buff A source buffer to write sectors from. Must be
 * large enough to hold num_sectors worth of sectors.
 *
 * \retval 0 num_sectors were written to file
 * \retval >0 < num_sectors were written to file
 */
int mvhd_noop_write(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* in_buff);

#endif
