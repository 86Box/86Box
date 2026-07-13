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

#ifndef LIBAARUFORMAT_METADATA_H
#define LIBAARUFORMAT_METADATA_H

#pragma pack(push, 1)

/** \file aaruformat/structs/metadata.h
 *  \brief Packed on-disk metadata block headers for descriptive strings and CICM XML (if present).
 *
 *  Two metadata-related block header layouts are defined:
 *   - \ref MetadataBlockHeader (BlockType::MetadataBlock): offsets + lengths for several UTF-16LE strings.
 *   - \ref CicmMetadataBlock (BlockType::CicmBlock): length of embedded CICM XML metadata payload.
 *
 *  All multi-byte integers are little-endian. Structures are packed (1-byte alignment). All textual fields
 *  referenced by offsets are UTF-16LE, null-terminated (0x0000). Length fields include the terminating
 *  null (i.e. length >= 2 and an even number). Offsets are relative to the start of the corresponding block
 *  header (byte 0 = first byte of the header). No padding is implicitly added between strings; producers
 *  may pack them tightly or align them manually (alignment not required by the specification).
 *
 *  Metadata block layout (conceptual):
 *    MetadataBlockHeader (fixed size)
 *    <variable region holding each present UTF-16LE string in any order chosen by the writer>
 *
 *  Invariants / validation recommendations for MetadataBlockHeader:
 *   - identifier == BlockType::MetadataBlock
 *   - blockSize >= sizeof(MetadataBlockHeader)
 *   - For every (offset,length) pair where length > 0:
 *       * offset >= sizeof(MetadataBlockHeader)
 *       * offset + length <= blockSize
 *       * length % 2 == 0
 *       * The 16-bit code unit at (offset + length - 2) == 0x0000 (null terminator)
 *   - mediaSequence >= 0 and lastMediaSequence >= 0; if lastMediaSequence > 0 then 0 <= mediaSequence <
 * lastMediaSequence
 *
 *  CICM metadata block layout:
 *    CicmMetadataBlock (header)
 *    <length bytes of UTF-8 or XML text payload (implementation-defined, not null-terminated)>
 *
 *  NOTE: The library code reading these blocks must not assume strings are present; a zero length means the
 *  corresponding field is omitted. Offsets for omitted fields MAY be zero or arbitrary; readers should skip them
 *  whenever length == 0.
 */

/** \struct MetadataBlockHeader
 *  \brief Header for a metadata block containing offsets and lengths to UTF-16LE descriptive strings.
 *
 *  Descriptive fields (all optional): creator, comments, media title/manufacturer/model/serial/barcode/part number,
 *  drive manufacturer/model/serial/firmware revision. Strings can be used to describe both physical medium and
 *  acquisition hardware. Length values include the UTF-16LE null terminator (two zero bytes).
 */
typedef struct MetadataBlockHeader
{
    uint32_t identifier;         ///< Block identifier, must be BlockType::MetadataBlock.
    uint32_t blockSize;          ///< Total size in bytes of the entire metadata block (header + strings).
    int32_t  mediaSequence;      ///< Sequence number within a multi-disc / multi-volume set (0-based or 1-based as
                                 ///< producer defines).
    int32_t  lastMediaSequence;  ///< Total number of media in the set; 0 or 1 if single item.
    uint32_t creatorOffset;      ///< Offset to UTF-16LE creator string (or undefined if creatorLength==0).
    uint32_t creatorLength;      ///< Length in bytes (including null) of creator string (0 if absent).
    uint32_t commentsOffset;     ///< Offset to UTF-16LE comments string.
    uint32_t commentsLength;     ///< Length in bytes (including null) of comments string.
    uint32_t mediaTitleOffset;   ///< Offset to UTF-16LE media title string.
    uint32_t mediaTitleLength;   ///< Length in bytes (including null) of media title string.
    uint32_t mediaManufacturerOffset;      ///< Offset to UTF-16LE media manufacturer string.
    uint32_t mediaManufacturerLength;      ///< Length in bytes (including null) of media manufacturer string.
    uint32_t mediaModelOffset;             ///< Offset to UTF-16LE media model string.
    uint32_t mediaModelLength;             ///< Length in bytes (including null) of media model string.
    uint32_t mediaSerialNumberOffset;      ///< Offset to UTF-16LE media serial number string.
    uint32_t mediaSerialNumberLength;      ///< Length in bytes (including null) of media serial number string.
    uint32_t mediaBarcodeOffset;           ///< Offset to UTF-16LE media barcode string.
    uint32_t mediaBarcodeLength;           ///< Length in bytes (including null) of media barcode string.
    uint32_t mediaPartNumberOffset;        ///< Offset to UTF-16LE media part number string.
    uint32_t mediaPartNumberLength;        ///< Length in bytes (including null) of media part number string.
    uint32_t driveManufacturerOffset;      ///< Offset to UTF-16LE drive manufacturer string.
    uint32_t driveManufacturerLength;      ///< Length in bytes (including null) of drive manufacturer string.
    uint32_t driveModelOffset;             ///< Offset to UTF-16LE drive model string.
    uint32_t driveModelLength;             ///< Length in bytes (including null) of drive model string.
    uint32_t driveSerialNumberOffset;      ///< Offset to UTF-16LE drive serial number string.
    uint32_t driveSerialNumberLength;      ///< Length in bytes (including null) of drive serial number string.
    uint32_t driveFirmwareRevisionOffset;  ///< Offset to UTF-16LE drive firmware revision string.
    uint32_t driveFirmwareRevisionLength;  ///< Length in bytes (including null) of drive firmware revision string.
} MetadataBlockHeader;

/** \struct CicmMetadataBlock
 *  \brief Header for a CICM XML metadata block (identifier == BlockType::CicmBlock).
 *
 *  The following 'length' bytes immediately after the header contain the CICM XML payload. Encoding is typically
 *  UTF-8; the payload is not required to be null-terminated.
 */
typedef struct CicmMetadataBlock
{
    uint32_t identifier;  ///< Block identifier, must be BlockType::CicmBlock.
    uint32_t length;      ///< Length in bytes of the CICM metadata payload that follows.
} CicmMetadataBlock;

/** \struct AaruMetadataJsonBlockHeader
 *  \brief Header for an Aaru metadata JSON block (identifier == BlockType::AaruMetadataJsonBlock).
 *
 *  The following 'length' bytes immediately after the header contain the Aaru metadata JSON payload. Encoding is
 *  typically UTF-8; the payload is not required to be null-terminated.
 */
typedef struct AaruMetadataJsonBlockHeader
{
    uint32_t identifier;  ///< Block identifier, must be BlockType::AaruMetadataJsonBlock.
    uint32_t length;      ///< Length in bytes of the Aaru metadata JSON payload that follows.
} AaruMetadataJsonBlockHeader;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_METADATA_H
