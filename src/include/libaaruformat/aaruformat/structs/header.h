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

#ifndef LIBAARUFORMAT_HEADER_H
#define LIBAARUFORMAT_HEADER_H

/** \file aaruformat/structs/header.h
 *  \brief On-disk container header structures (v1 and v2) for Aaru images.
 *
 *  These packed headers appear at the very beginning (offset 0) of every Aaru image file and
 *  advertise container format version, creator application, indexing offset and optional extended
 *  feature capability bitfields (v2+). All multi-byte integers are little-endian. Strings stored
 *  in the fixed-size application field are UTF‑16LE and zero padded (not necessarily NUL-terminated
 *  if fully filled). The GUID field (v2) allows derivative / child images to reference an origin.
 *
 *  Version progression:
 *   - v1: \ref AaruHeader (no GUID, no alignment or shift metadata, no feature bitfields).
 *   - v2: \ref AaruHeaderV2 introduces GUID, block/data/table shift hints (mirroring DDT metadata),
 *         and three 64‑bit feature bitmaps to negotiate reader/writer compatibility.
 *
 *  Compatibility handling (recommended logic for consumers):
 *   1. If any bit set in featureIncompatible is not implemented by the reader: abort (cannot safely read/write).
 *   2. Else if any bit set in featureCompatibleRo is not implemented: allow read‑only operations.
 *   3. Bits only present in featureCompatible but not implemented MAY be ignored for both read/write while
 *      still preserving round‑trip capability (writer should not clear unknown bits when re‑saving).
 *
 *  Alignment & shift semantics (duplicated here for quick reference, see DdtHeader2 for full details):
 *   - blockAlignmentShift: underlying blocks are aligned to 2^blockAlignmentShift bytes.
 *   - dataShift: data pointer / DDT entry low bits encode offsets modulo 2^dataShift sectors/items.
 *   - tableShift: primary DDT entries span 2^tableShift logical sectors (0 implies single-level tables).
 *
 *  Invariants:
 *   - identifier == AARU_MAGIC (external constant; not defined here).
 *   - For v1: sizeof(AaruHeader) exact and indexOffset > 0 (indexOffset == 0 => corrupt/unreadable image).
 *   - For v2: sizeof(AaruHeaderV2) exact; indexOffset > 0; blockAlignmentShift, dataShift, tableShift within
 *             sane bounds (e.g. < 63). Zero is permissible only for the shift fields (not for indexOffset).
 *
 *  Security / robustness considerations:
 *   - Always bounds-check indexOffset against file size before seeking.
 *   - Treat application field as untrusted UTF‑16LE; validate surrogate pairs if necessary.
 *   - Unknown feature bits MUST be preserved if a file is rewritten to avoid capability loss.
 */

#define AARU_HEADER_APP_NAME_LEN 64 /**< Size in bytes (UTF-16LE) of application name field (32 UTF-16 code units). */
#define GUID_SIZE                16 /**< Size in bytes of GUID / UUID-like binary identifier. */

#pragma pack(push, 1)

/** \struct AaruHeader
 *  \brief Version 1 container header placed at offset 0 for legacy / initial format.
 *
 *  Field summary:
 *   - identifier: magic signature (AARU_MAGIC) identifying the container.
 *   - application: UTF‑16LE creator application name (fixed 64 bytes, zero padded).
 *   - imageMajorVersion / imageMinorVersion: container format version of the file itself (not the app).
 *   - applicationMajorVersion / applicationMinorVersion: version of the creating application.
 *   - mediaType: media type enumeration (\ref MediaType).
 *   - indexOffset: byte offset to the first index block (must be > 0).
 *   - creationTime / lastWrittenTime: 64-bit Windows FILETIME timestamps (100 ns intervals since 1601-01-01 UTC).
 */
typedef struct AaruHeader
{
    uint64_t identifier;                             ///< File magic (AARU_MAGIC).
    uint8_t  application[AARU_HEADER_APP_NAME_LEN];  ///< UTF-16LE creator application name (fixed-size buffer).
    uint8_t  imageMajorVersion;        ///< Container format major version (incompatible changes when incremented).
    uint8_t  imageMinorVersion;        ///< Container format minor version (backward compatible evolutions).
    uint8_t  applicationMajorVersion;  ///< Creator application major version.
    uint8_t  applicationMinorVersion;  ///< Creator application minor / patch version.
    uint32_t mediaType;                ///< Media type enumeration (value from \ref MediaType).
    uint64_t indexOffset;      ///< Absolute byte offset to primary index block (MUST be > 0; 0 => corrupt/unreadable).
    int64_t  creationTime;     ///< Creation FILETIME (100 ns since 1601-01-01 UTC).
    int64_t  lastWrittenTime;  ///< Last modification FILETIME (100 ns since 1601-01-01 UTC).
} AaruHeader;

/** \struct AaruHeaderV2
 *  \brief Version 2 container header with GUID, alignment shifts, and feature negotiation bitmaps.
 *
 *  Additions over v1:
 *   - guid: stable 128-bit identifier enabling linkage by derivative images.
 *   - blockAlignmentShift / dataShift / tableShift: global structural hints copied into data & DDT blocks.
 *   - featureCompatible / featureCompatibleRo / featureIncompatible: capability bitmasks.
 *
 *  Feature bitmask semantics:
 *   - featureCompatible: Optional features; absence of implementation should not impact R/W correctness.
 *   - featureCompatibleRo: If unimplemented, image MAY be opened read-only.
 *   - featureIncompatible: If any bit unimplemented, image MUST NOT be opened (prevent misinterpretation).
 *
 *  Readers should AND their supported bit set with the header masks to decide access level (see file
 *  documentation). Writers must preserve unknown bits when saving an existing image.
 */
typedef struct AaruHeaderV2
{
    uint64_t identifier;                             ///< File magic (AARU_MAGIC).
    uint8_t  application[AARU_HEADER_APP_NAME_LEN];  ///< UTF-8 creator application name (fixed 64 bytes).
    uint8_t  imageMajorVersion;                      ///< Container format major version.
    uint8_t  imageMinorVersion;                      ///< Container format minor version.
    uint8_t  applicationMajorVersion;                ///< Creator application major version.
    uint8_t  applicationMinorVersion;                ///< Creator application minor / patch version.
    uint32_t mediaType;                              ///< Media type enumeration (value from \ref MediaType).
    uint64_t indexOffset;      ///< Absolute byte offset to primary index block (MUST be > 0; 0 => corrupt/unreadable).
    int64_t  creationTime;     ///< Creation FILETIME (100 ns since 1601-01-01 UTC).
    int64_t  lastWrittenTime;  ///< Last modification FILETIME (100 ns since 1601-01-01 UTC).
    uint8_t  guid[GUID_SIZE];  ///< 128-bit image GUID (binary, not text); stable across children.
    uint8_t  blockAlignmentShift;  ///< log2 block alignment (block size alignment = 2^blockAlignmentShift bytes).
    uint16_t biggestSectorSize;    ///< size of biggest sector in the image (in bytes).
    uint64_t featureCompatible;    ///< Feature bits: unimplemented bits are ignorable (still R/W safe).
    uint64_t featureCompatibleRo;  ///< Feature bits: unimplemented -> degrade to read-only access.
    uint64_t featureIncompatible;  ///< Feature bits: any unimplemented -> abort (cannot open safely).
} AaruHeaderV2;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_HEADER_H
