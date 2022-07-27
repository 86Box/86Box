#ifndef MINIVHD_STRUCT_RW_H
#define MINIVHD_STRUCT_RW_H

#include "minivhd_internal.h"

/**
 * \brief Save the contents of a VHD footer from a buffer to a struct
 *
 * \param [out] footer save contents of buffer into footer
 * \param [in] buffer VHD footer in raw bytes
 */
void mvhd_buffer_to_footer(MVHDFooter* footer, uint8_t* buffer);

/**
 * \brief Save the contents of a VHD sparse header from a buffer to a struct
 *
 * \param [out] header save contents of buffer into header
 * \param [in] buffer VHD header in raw bytes
 */
void mvhd_buffer_to_header(MVHDSparseHeader* header, uint8_t* buffer);

/**
 * \brief Save the contents of a VHD footer struct to a buffer
 *
 * \param [in] footer save contents of struct into buffer
 * \param [out] buffer VHD footer in raw bytes
 */
void mvhd_footer_to_buffer(MVHDFooter* footer, uint8_t* buffer);

/**
 * \brief Save the contents of a VHD sparse header struct to a buffer
 *
 * \param [in] header save contents of struct into buffer
 * \param [out] buffer VHD sparse header in raw bytes
 */
void mvhd_header_to_buffer(MVHDSparseHeader* header, uint8_t* buffer);

#endif
