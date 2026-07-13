/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBAARUFORMAT_ERRORS_H
#define LIBAARUFORMAT_ERRORS_H

/** \file aaruformat/errors.h
 *  \brief Public error and status code definitions for libaaruformat.
 *
 *  Negative values represent fatal / non-recoverable error conditions returned by library functions.
 *  Non-negative values (>=0) are either success (0) or sector-level status annotations used when
 *  decoding per-sector metadata (e.g. a sector not dumped or with corrected/unrecoverable errors).
 *
 *  Usage guidelines:
 *   - Always test for < 0 to check generic failure without enumerating all codes.
 *   - Use exact comparisons for caller-specific handling (e.g. retry on AARUF_ERROR_CANNOT_READ_BLOCK).
 *   - Sector status codes are never returned as fatal function results; they appear in output parameters
 *     populated by read/identify routines.
 *
 *  Helper: see aaruformat_error_string() for a human-readable textual description suitable for logs.
 */

/** \name Fatal / library-level error codes (negative)
 *  @{ */
#define AARUF_ERROR_NOT_AARUFORMAT            (-1)   ///< Input file/stream failed magic or structural validation.
#define AARUF_ERROR_FILE_TOO_SMALL            (-2)   ///< File size insufficient for mandatory header / structures.
#define AARUF_ERROR_INCOMPATIBLE_VERSION      (-3)   ///< Image uses a newer incompatible on-disk version.
#define AARUF_ERROR_CANNOT_READ_INDEX         (-4)   ///< Index block unreadable / truncated / bad identifier.
#define AARUF_ERROR_SECTOR_OUT_OF_BOUNDS      (-5)   ///< Requested logical sector outside media bounds.
#define AARUF_ERROR_CANNOT_READ_HEADER        (-6)   ///< Failed to read container header.
#define AARUF_ERROR_CANNOT_READ_BLOCK         (-7)   ///< Generic block read failure (seek/read error).
#define AARUF_ERROR_UNSUPPORTED_COMPRESSION   (-8)   ///< Block marked with unsupported compression algorithm.
#define AARUF_ERROR_NOT_ENOUGH_MEMORY         (-9)   ///< Memory allocation failure (critical).
#define AARUF_ERROR_BUFFER_TOO_SMALL          (-10)  ///< Caller-supplied buffer insufficient for data.
#define AARUF_ERROR_MEDIA_TAG_NOT_PRESENT     (-11)  ///< Requested media tag absent.
#define AARUF_ERROR_INCORRECT_MEDIA_TYPE      (-12)  ///< Operation incompatible with image media type.
#define AARUF_ERROR_TRACK_NOT_FOUND           (-13)  ///< Referenced track number not present.
#define AARUF_ERROR_REACHED_UNREACHABLE_CODE  (-14)  ///< Internal logic assertion hit unexpected path.
#define AARUF_ERROR_INVALID_TRACK_FORMAT      (-15)  ///< Track metadata internally inconsistent or malformed.
#define AARUF_ERROR_SECTOR_TAG_NOT_PRESENT    (-16)  ///< Requested sector tag (e.g. subchannel/prefix) not stored.
#define AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK   (-17)  ///< Decompression routine failed or size mismatch.
#define AARUF_ERROR_INVALID_BLOCK_CRC         (-18)  ///< CRC64 mismatch indicating corruption.
#define AARUF_ERROR_CANNOT_CREATE_FILE        (-19)  ///< Output file could not be created / opened for write.
#define AARUF_ERROR_INVALID_APP_NAME_LENGTH   (-20)  ///< Application name field length invalid (sanity limit).
#define AARUF_ERROR_CANNOT_WRITE_HEADER       (-21)  ///< Failure writing container header.
#define AARUF_READ_ONLY                       (-22)  ///< Operation requires write mode but context is read-only.
#define AARUF_ERROR_CANNOT_WRITE_BLOCK_HEADER (-23)  ///< Failure writing block header.
#define AARUF_ERROR_CANNOT_WRITE_BLOCK_DATA   (-24)  ///< Failure writing block payload.
#define AARUF_ERROR_CANNOT_SET_DDT_ENTRY      (-25)  ///< Failed to encode/store a DDT entry (overflow or IO).
#define AARUF_ERROR_INCORRECT_DATA_SIZE       (-26)  ///< Data size does not match expected size.
#define AARUF_ERROR_INVALID_TAG               (-27)  ///< Invalid or unsupported media or sector tag format.
#define AARUF_ERROR_TAPE_FILE_NOT_FOUND       (-28)  ///< Requested tape file number not present in image.
#define AARUF_ERROR_TAPE_PARTITION_NOT_FOUND  (-29)  ///< Requested tape partition not present in image.
#define AARUF_ERROR_METADATA_NOT_PRESENT      (-30)  ///< Requested metadata not present in image.
#define AARUF_ERROR_INVALID_SECTOR_LENGTH     (-31)  ///< Sector length is too big.
#define AARUF_ERROR_FLUX_DATA_NOT_FOUND       (-32)  ///< Requested flux data not present in image.
#define AARUF_ERROR_INCOMPATIBLE_FEATURES     (-33)  ///< Image requires features not supported by this library.
#define AARUF_ERROR_CANNOT_ENCRYPT_SECTOR     (-34)  ///< AES sector encryption failed.
#define AARUF_ERROR_CANNOT_DECRYPT_SECTOR     (-35)  ///< AES sector decryption failed.
#define AARUF_ERROR_MISSING_ENCRYPTION_KEY    (-36)  ///< Required encryption key not present in media tags.
#define AARUF_ERROR_USER_DATA_NOT_PRESENT     (-37)  ///< Image has no user-data DDT (e.g. flux-only image); sector data is unavailable.
/** @} */

/** \name Non-fatal sector status codes (non-negative)
 *  Returned through output parameters to describe individual sector state.
 *  @{ */
#define AARUF_STATUS_OK                 0  ///< Sector present and read without uncorrectable errors.
#define AARUF_STATUS_SECTOR_NOT_DUMPED  1  ///< Sector not captured (gap / missing / intentionally skipped).
#define AARUF_STATUS_SECTOR_WITH_ERRORS 2  ///< Sector present but with unrecoverable or flagged errors.
#define AARUF_STATUS_SECTOR_DELETED     3  ///< Sector logically marked deleted (e.g. filesystem deleted area).

/** @} */

/** \brief Convert an AaruFormat error or status code to a static human-readable string.
 *
 *  Designed for diagnostics / logging; returns a constant string literal. Unknown codes yield
 *  "Unknown error/status". This helper is inline to avoid adding a separate translation unit.
 *
 *  \param code Error (<0) or status (>=0) numeric code.
 *  \return Constant C string describing the code.
 */
static inline const char *aaruformat_error_string(int code)
{
    switch(code)
    {
        /* Errors */
        case AARUF_ERROR_NOT_AARUFORMAT:
            return "Not an AaruFormat image";
        case AARUF_ERROR_FILE_TOO_SMALL:
            return "File too small";
        case AARUF_ERROR_INCOMPATIBLE_VERSION:
            return "Incompatible image version";
        case AARUF_ERROR_CANNOT_READ_INDEX:
            return "Cannot read index";
        case AARUF_ERROR_SECTOR_OUT_OF_BOUNDS:
            return "Sector out of bounds";
        case AARUF_ERROR_CANNOT_READ_HEADER:
            return "Cannot read header";
        case AARUF_ERROR_CANNOT_READ_BLOCK:
            return "Cannot read block";
        case AARUF_ERROR_UNSUPPORTED_COMPRESSION:
            return "Unsupported compression";
        case AARUF_ERROR_NOT_ENOUGH_MEMORY:
            return "Not enough memory";
        case AARUF_ERROR_BUFFER_TOO_SMALL:
            return "Buffer too small";
        case AARUF_ERROR_MEDIA_TAG_NOT_PRESENT:
            return "Media tag not present";
        case AARUF_ERROR_INCORRECT_MEDIA_TYPE:
            return "Incorrect media type";
        case AARUF_ERROR_TRACK_NOT_FOUND:
            return "Track not found";
        case AARUF_ERROR_REACHED_UNREACHABLE_CODE:
            return "Internal unreachable code reached";
        case AARUF_ERROR_INVALID_TRACK_FORMAT:
            return "Invalid track format";
        case AARUF_ERROR_SECTOR_TAG_NOT_PRESENT:
            return "Sector tag not present";
        case AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK:
            return "Cannot decompress block";
        case AARUF_ERROR_INVALID_BLOCK_CRC:
            return "Invalid block CRC";
        case AARUF_ERROR_CANNOT_CREATE_FILE:
            return "Cannot create file";
        case AARUF_ERROR_INVALID_APP_NAME_LENGTH:
            return "Invalid application name length";
        case AARUF_ERROR_CANNOT_WRITE_HEADER:
            return "Cannot write header";
        case AARUF_READ_ONLY:
            return "Read-only context";
        case AARUF_ERROR_CANNOT_WRITE_BLOCK_HEADER:
            return "Cannot write block header";
        case AARUF_ERROR_CANNOT_WRITE_BLOCK_DATA:
            return "Cannot write block data";
        case AARUF_ERROR_CANNOT_SET_DDT_ENTRY:
            return "Cannot set DDT entry";
        case AARUF_ERROR_INCOMPATIBLE_FEATURES:
            return "Image requires unsupported features";
        case AARUF_ERROR_CANNOT_ENCRYPT_SECTOR:
            return "Cannot encrypt sector";
        case AARUF_ERROR_CANNOT_DECRYPT_SECTOR:
            return "Cannot decrypt sector";
        case AARUF_ERROR_MISSING_ENCRYPTION_KEY:
            return "Missing encryption key";
        case AARUF_ERROR_USER_DATA_NOT_PRESENT:
            return "User data not present (flux-only image)";

        /* Status */
        case AARUF_STATUS_OK:
            return "OK";
        case AARUF_STATUS_SECTOR_NOT_DUMPED:
            return "Sector not dumped";
        case AARUF_STATUS_SECTOR_WITH_ERRORS:
            return "Sector with errors";
        case AARUF_STATUS_SECTOR_DELETED:
            return "Sector deleted";
    }
    return "Unknown error/status";
}

#endif  // LIBAARUFORMAT_ERRORS_H
