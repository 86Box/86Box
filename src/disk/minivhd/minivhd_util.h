#ifndef MINIVHD_UTIL_H
#define MINIVHD_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "minivhd_internal.h"
#include "minivhd.h"
#define MVHD_START_TS 946684800

/**
 * Functions to deal with endian issues
 */
uint16_t mvhd_from_be16(uint16_t val);
uint32_t mvhd_from_be32(uint32_t val);
uint64_t mvhd_from_be64(uint64_t val);
uint16_t mvhd_to_be16(uint16_t val);
uint32_t mvhd_to_be32(uint32_t val);
uint64_t mvhd_to_be64(uint64_t val);

/**
 * \brief Check if provided buffer begins with the string "conectix"
 *
 * \param [in] buffer The buffer to compare. Must be at least 8 bytes in length
 *
 * \return true if the buffer begins with "conectix"
 * \return false if the buffer does not begin with "conectix"
 */
bool mvhd_is_conectix_str(const void* buffer);

/**
 * \brief Generate a raw 16 byte UUID
 *
 * \param [out] uuid A 16 byte buffer in which the generated UUID will be stored to
 */
void mvhd_generate_uuid(uint8_t *uuid);

/**
 * \brief Calculate a VHD formatted timestamp from the current time
 */
uint32_t vhd_calc_timestamp(void);

/**
 * \brief Convert an epoch timestamp to a VHD timestamp
 *
 * \param [in] ts epoch timestamp to convert.
 *
 * \return The adjusted timestamp, or 0 if the input timestamp is
 * earlier that 1 Janurary 2000
 */
uint32_t mvhd_epoch_to_vhd_ts(time_t ts);

/**
 * \brief Return the created time from a VHD image
 *
 * \param [in] vhdm Pointer to the MiniVHD metadata structure
 *
 * \return The created time, as a Unix timestamp
 */
time_t vhd_get_created_time(MVHDMeta *vhdm);

/**
 * \brief Cross platform, unicode filepath opening
 *
 * This function accounts for the fact that fopen() handles file paths differently compared to other
 * operating systems. Windows version of fopen() will not handle multi byte encoded text like UTF-8.
 *
 * Unicode filepath support on Windows requires using the _wfopen() function, which expects UTF-16LE
 * encoded path and modestring.
 *
 * \param [in] path The filepath to open as a UTF-8 string
 * \param [in] mode The mode string to use (eg: "rb+"")
 * \param [out] err The error value, if an error occurrs
 *
 * \return a FILE pointer if successful, NULL otherwise. If NULL, check the value of err
 */
FILE* mvhd_fopen(const char* path, const char* mode, int* err);

void mvhd_set_encoding_err(int encoding_retval, int* err);
uint64_t mvhd_calc_size_bytes(MVHDGeom *geom);
uint32_t mvhd_calc_size_sectors(MVHDGeom *geom);
MVHDGeom mvhd_get_geometry(MVHDMeta* vhdm);

/**
 * \brief Generate VHD footer checksum
 *
 * \param [in] vhdm MiniVHD data structure
 */
uint32_t mvhd_gen_footer_checksum(MVHDFooter* footer);

/**
 * \brief Generate VHD sparse header checksum
 *
 * \param [in] vhdm MiniVHD data structure
 */
uint32_t mvhd_gen_sparse_checksum(MVHDSparseHeader* header);

/**
 * \brief Get current position in file stream
 *
 * This is a portable version of the POSIX ftello64(). *
 */
int64_t mvhd_ftello64(FILE* stream);

/**
 * \brief Reposition the file stream's position
 *
 * This is a portable version of the POSIX fseeko64(). *
 */
int mvhd_fseeko64(FILE* stream, int64_t offset, int origin);

/**
 * \brief Calculate the CRC32 of a data buffer.
 *
 * This function can be used for verifying data integrity.
 *
 * \param [in] data The data buffer
 * \param [in] n_bytes The size of the data buffer in bytes
 *
 * \return The CRC32 of the data buffer
 */
uint32_t mvhd_crc32(const void* data, size_t n_bytes);

/**
 * \brief Calculate the file modification timestamp.
 *
 * This function is primarily to help protect differencing VHD's
 *
 * \param [in] path the UTF-8 file path
 * \param [out] err The error value, if an error occurrs
 *
 * \return The file modified timestamp, in VHD compatible timestamp.
 * 'err' will be set to non-zero on error
 */
uint32_t mvhd_file_mod_timestamp(const char* path, int *err);
#endif
