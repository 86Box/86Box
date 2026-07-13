/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
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

/**
 * @file flux.h
 * @brief Data structures for flux transition capture support in Aaru disk images.
 *
 * This header defines structures used to represent flux transition data captured
 * from magnetic media. Flux transitions are the raw analog signals read from
 * magnetic storage devices (such as floppy disks, hard drives, and tape) before
 * they are interpreted into digital data.
 *
 * **Flux Capture Overview:**
 * Certain hardware devices, such as Kryoflux, Pauline, and Applesauce, read
 * magnetic media at the flux transition level rather than at the sector level.
 * This provides a more complete representation of the analog properties of the
 * media, allowing for advanced recovery techniques and preservation of media
 * characteristics that would be lost in sector-level imaging.
 *
 * **Block Structure:**
 * Flux data is stored in two block types:
 * - FluxDataBlock: Contains metadata entries describing all flux captures in
 *   the image. Each entry includes location information (head, track, subtrack),
 *   capture index, resolution data, and file offsets to the payload blocks.
 * - DataStreamPayloadBlock: Contains the actual flux data and index buffers for a
 *   single capture. Payload blocks may be compressed using LZMA compression.
 *
 * **Data Organization:**
 * Each flux capture includes two data streams:
 * - Data buffer: Raw flux transition timing data, represented as an array of
 *   uint8_t bytes where each byte stores the tick count since the last flux
 *   transition. The value 0xFF indicates no transition within the byte range.
 * - Index buffer: Index structure mapping logical positions to offsets within
 *   the data buffer, enabling efficient access to specific regions of the flux
 *   data.
 *
 * **Capture Identification:**
 * Flux captures are uniquely identified by a tuple of (head, track, subtrack,
 * captureIndex). This allows multiple captures for the same physical location,
 * which is useful for capturing multiple revolutions of a floppy disk track or
 * different read attempts.
 *
 * **Resolution:**
 * Each capture specifies two resolution values:
 * - dataResolution: The sampling resolution in picoseconds for the data stream
 * - indexResolution: The sampling resolution in picoseconds for the index stream
 *
 * These resolutions determine the precision of the timing measurements and are
 * hardware-dependent.
 *
 * **Storage Efficiency:**
 * Payload blocks support LZMA compression to reduce storage requirements. The
 * compression is transparent to the API user - aaruf_read_flux_capture() handles
 * decompression automatically.
 *
 * **Use Cases:**
 * - Preservation of magnetic media at the lowest level possible
 * - Recovery of data from damaged or degraded media
 * - Analysis of media characteristics and timing variations
 * - Emulation and accurate reproduction of original media behavior
 * - Forensic imaging where complete bit-level accuracy is required
 *
 * @note All structures in this file use packed alignment (#pragma pack(push, 1))
 *       to ensure consistent on-disk layout across different compilers and platforms.
 *
 * @see BlockType for the identifier constants used in FluxHeader and DataStreamPayloadHeader
 * @see aaruf_get_flux_captures() to retrieve metadata for all captures
 * @see aaruf_read_flux_capture() to read actual flux data
 * @see aaruf_write_flux_capture() to add flux captures during write mode
 */

#ifndef LIBAARUFORMAT_FLUX_H
#define LIBAARUFORMAT_FLUX_H

#include <stdint.h>

#pragma pack(push, 1)

/**
 * @struct FluxHeader
 * @brief Header structure for a FluxDataBlock containing flux capture metadata.
 *
 * This structure is the header of a FluxDataBlock, which lists all flux captures
 * in the image. The block contains this header followed by an array of FluxEntry
 * structures, one for each flux capture.
 *
 * The header includes a CRC64 checksum computed over the FluxEntry array to ensure
 * data integrity. The identifier field must match BlockType::FluxDataBlock
 * (0x58554C46, "FLUX" in ASCII).
 *
 * The blockAlignmentShift field stores the block alignment shift value used to
 * decode the payloadOffset values in FluxEntry structures. This makes the block
 * self-contained, similar to DDT headers, allowing correct decoding of offsets
 * without requiring access to the main image header.
 *
 * @note Only one FluxDataBlock is allowed per image.
 * @note The entries field is limited to UINT16_MAX (65535) captures per image.
 * @note The blockAlignmentShift field is stored in the header to support reliable
 *       decoding of payloadOffset values, which are stored divided by (1 << blockAlignmentShift).
 */
typedef struct FluxHeader
{
    uint32_t identifier;          ///< Block identifier, must be BlockType::FluxDataBlock (0x58554C46, "FLUX").
    uint16_t entries;             ///< Number of FluxEntry records following this header. Maximum value: 65535.
    uint8_t  blockAlignmentShift; ///< Block alignment shift: 2^blockAlignmentShift = block alignment boundary in bytes.
    uint64_t crc64;               ///< CRC64-ECMA checksum of the FluxEntry array (header excluded).
} FluxHeader;

/**
 * @struct FluxEntry
 * @brief Metadata entry describing a single flux capture in the FluxDataBlock.
 *
 * This structure describes one flux capture, including its location on the media,
 * capture index, sampling resolutions, and file offsets to the payload data. Each
 * FluxEntry corresponds to one DataStreamPayloadBlock containing the actual flux data.
 *
 * **Location Identification:**
 * The head, track, and subtrack fields identify the physical location on the media
 * where the capture was taken. The captureIndex allows multiple captures for the
 * same location (e.g., multiple revolutions of a floppy disk track).
 *
 * **Payload Access:**
 * The payloadOffset field points to the file offset where the corresponding
 * DataStreamPayloadBlock is stored. The offset is stored divided by the block alignment
 * (blockAlignmentShift from FluxHeader), consistent with DDT table offset storage.
 * To convert to an absolute file offset, multiply by (1 << blockAlignmentShift).
 * The indexOffset field indicates where the index buffer starts within the payload
 * (the payload is stored as [data_buffer][index_buffer] concatenated).
 *
 * **Resolution:**
 * Both indexResolution and dataResolution are specified in picoseconds, indicating
 * the precision of the timing measurements. These values are hardware-dependent and
 * determine how accurately the flux transitions are represented.
 *
 * @note The combination of (head, track, subtrack, captureIndex) uniquely identifies
 *       a flux capture within an image.
 * @note The indexOffset equals the data_length, as the index buffer immediately
 *       follows the data buffer in the payload.
 */
typedef struct FluxEntry
{
    uint32_t head;            ///< Head number the flux capture corresponds to. Typically 0 or 1 for double-sided media.
    uint16_t track;           ///< Track number the flux capture corresponds to. Track numbering is format-dependent.
    uint8_t  subtrack;        ///< Subtrack number, allowing sub-stepping within a track. Used for fine positioning.
    uint32_t captureIndex;    ///< Capture index, allowing multiple captures for the same location (e.g., multiple revolutions).
    uint64_t indexResolution; ///< Resolution in picoseconds at which the index stream was sampled.
    uint64_t dataResolution;  ///< Resolution in picoseconds at which the data stream was sampled.
    uint64_t indexOffset;     ///< Byte offset within the payload where the index buffer starts (equals data_length).
    uint64_t payloadOffset;   ///< Block-aligned file offset where the DataStreamPayloadBlock containing this capture's data is stored, divided by (1 << blockAlignmentShift). To get the absolute offset, multiply by (1 << blockAlignmentShift) from FluxHeader.
} FluxEntry;

/**
 * @struct FluxCaptureMeta
 * @brief Metadata structure returned by aaruf_get_flux_captures().
 *
 * This structure contains the public metadata for a flux capture, excluding
 * internal file offsets. It is used when retrieving metadata for all captures
 * in an image without needing to access the actual flux data.
 *
 * The structure contains the same location and resolution information as FluxEntry,
 * but omits the indexOffset and payloadOffset fields which are implementation
 * details not needed by API users.
 *
 * @see aaruf_get_flux_captures() to retrieve metadata for all captures
 * @see FluxEntry for the complete structure including file offsets
 */
typedef struct FluxCaptureMeta
{
    uint32_t head;           ///< Head number the flux capture corresponds to.
    uint16_t track;          ///< Track number the flux capture corresponds to.
    uint8_t  subtrack;       ///< Subtrack number the flux capture corresponds to.
    uint32_t captureIndex;   ///< Capture index, allowing multiple captures for the same location.
    uint64_t indexResolution; ///< Resolution in picoseconds at which the index stream was sampled.
    uint64_t dataResolution;  ///< Resolution in picoseconds at which the data stream was sampled.
} FluxCaptureMeta;

/**
 * @struct FluxCaptureMapEntry
 * @brief Internal hash table entry for flux capture lookup.
 *
 * This structure is used internally by the library to provide O(1) lookup of
 * flux captures by their identifier tuple. It maps a FluxCaptureKey to an index
 * in the flux_entries array.
 *
 * @note This structure is opaque to API users and is only used internally.
 * @internal
 */
typedef struct FluxCaptureMapEntry FluxCaptureMapEntry;

/**
 * @struct DataStreamPayloadHeader
 * @brief Header structure for a DataStreamPayloadBlock containing data stream payload.
 *
 * This structure is the header of a DataStreamPayloadBlock, which contains a generic
 * compressed data stream payload. The payload data immediately follows this header and
 * may be compressed using LZMA compression. Currently used for flux capture data, but
 * can be used for other data streams such as bitstreams or PNG data.
 *
 * **Data Type:**
 * The dataType field identifies the type of data stored in the payload. Common values include:
 * - FluxData: Flux capture data (data and index buffers)
 * - BitstreamData: Bitstream data
 * This field enables the block to be self-describing and allows validation of the payload content.
 *
 * **Compression:**
 * The compression field indicates whether the payload is compressed:
 * - 0 (None): Payload is stored uncompressed
 * - 1 (Lzma): Payload is compressed using LZMA, with LZMA properties stored
 *   in the first 5 bytes of the compressed data
 *
 * **Checksums:**
 * Two CRC64 checksums are stored:
 * - cmpCrc64: Checksum of the compressed payload (or same as crc64 if uncompressed)
 * - crc64: Checksum of the uncompressed payload
 *
 * Both checksums are validated when reading the payload to ensure data integrity.
 *
 * **Payload Layout:**
 * The uncompressed payload is an arbitrary stream of binary data. For flux captures,
 * this consists of concatenated data and index buffers: [data_buffer][index_buffer],
 * with the indexOffset from the corresponding FluxEntry indicating where the index
 * buffer starts. For other data types, the layout is specific to the data type.
 *
 * @note The identifier field must match BlockType::DataStreamPayloadBlock (0x4C505344, "DSPL").
 * @note If compression is Lzma, cmpLength includes the 5-byte LZMA properties header.
 */
typedef struct DataStreamPayloadHeader
{
    uint32_t identifier;   ///< Block identifier, must be BlockType::DataStreamPayloadBlock (0x4C505344, "DSPL").
    uint16_t dataType;     ///< Data type classification (value from \ref DataType), e.g., FluxData or BitstreamData.
    uint16_t compression;  ///< Compression type: 0 = None, 1 = Lzma.
    uint32_t cmpLength;    ///< Compressed length in bytes (includes LZMA properties if compression = Lzma).
    uint32_t length;       ///< Uncompressed length in bytes.
    uint64_t cmpCrc64;     ///< CRC64-ECMA checksum of the compressed payload (or same as crc64 if uncompressed).
    uint64_t crc64;        ///< CRC64-ECMA checksum of the uncompressed payload.
} DataStreamPayloadHeader;

/**
 * @struct FluxCaptureRecord
 * @brief Internal structure for storing flux capture data during write mode.
 *
 * This structure is used internally by the library to store flux capture data
 * in memory before it is written to the image. It combines the FluxEntry metadata
 * with pointers to the actual data and index buffers.
 *
 * The structure is stored in a utarray (ctx->flux_captures) during write mode,
 * and the buffers are freed automatically when the record is removed from the
 * array or when the array is freed.
 *
 * @note This structure is used internally and is not part of the public API.
 * @note The data_buffer and index_buffer are owned by the utarray and are freed
 *       automatically via the flux_capture_record_dtor() destructor.
 * @internal
 */
typedef struct FluxCaptureRecord
{
    FluxEntry entry;        ///< Flux entry metadata describing this capture.
    uint8_t  *data_buffer;  ///< Pointer to the flux data buffer. Owned by the utarray, freed automatically.
    uint32_t  data_length;  ///< Length of the data buffer in bytes.
    uint8_t  *index_buffer; ///< Pointer to the flux index buffer. Owned by the utarray, freed automatically.
    uint32_t  index_length; ///< Length of the index buffer in bytes.
} FluxCaptureRecord;

/**
 * @struct FluxCaptureKey
 * @brief Key structure for flux capture lookup map.
 *
 * This structure uniquely identifies a flux capture within an image. It is used
 * as the key in the internal hash table (flux_map) that provides O(1) lookup
 * of flux captures by their identifier tuple.
 *
 * The structure matches the first four fields of FluxEntry (head, track, subtrack,
 * captureIndex).
 *
 * @note This structure is used internally for efficient lookup and is not part of
 *       the public API.
 * @note The combination of (head, track, subtrack, captureIndex) must be unique
 *       within an image.
 * @internal
 */
typedef struct FluxCaptureKey
{
    uint32_t head;         ///< Head number identifying the capture location.
    uint16_t track;        ///< Track number identifying the capture location.
    uint8_t  subtrack;     ///< Subtrack number identifying the capture location.
    uint32_t captureIndex; ///< Capture index, allowing multiple captures for the same location.
} FluxCaptureKey;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_FLUX_H