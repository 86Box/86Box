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

#ifndef LIBAARUFORMAT_DUMP_H
#define LIBAARUFORMAT_DUMP_H

#include <stdint.h> /* Fixed-width integer types for on‑disk packed structures */

#pragma pack(push, 1)

/** \file aaruformat/structs/dump.h
 *  \brief Packed on-disk structures describing hardware and software used during image acquisition.
 *
 *  A Dump Hardware block (identifier = BlockType::DumpHardwareBlock) records one or more dump "environments" –
 *  typically combinations of a physical device (drive, controller, adapter) and the software stack that
 *  performed the read operation. Each environment is represented by a \ref DumpHardwareEntry followed by a
 *  sequence of UTF‑8 strings and an optional array of extent ranges (\ref DumpExtent, defined in context.h) that
 *  delimit portions of the medium this environment contributed to.
 *
 *  Binary layout (little-endian, packed, all multi-byte integers LE):
 *
 *    DumpHardwareHeader (sizeof = 16 bytes)
 *      identifier  (4)  -> BlockType::DumpHardwareBlock
 *      entries     (2)  -> number of following hardware entries
 *      length      (4)  -> total bytes of payload that follow this header
 *      crc64       (8)  -> CRC64-ECMA of the payload bytes
 *
 *    Repeated for i in [0, entries):
 *      DumpHardwareEntry (36 bytes)
 *        manufacturerLength (4)
 *        modelLength        (4)
 *        revisionLength     (4)
 *        firmwareLength     (4)
 *        serialLength       (4)
 *        softwareNameLength (4)
 *        softwareVersionLength (4)
 *        softwareOperatingSystemLength (4)
 *        extents (4) -> number of DumpExtent structs after the strings
 *
 *      Variable-length UTF-8 strings (not NUL-terminated on disk) appear immediately after the entry, in the
 *      exact order of the length fields above; each string is present only if its length > 0. The reader allocates
 *      an extra byte to append '\0' for in-memory convenience.
 *
 *      Array of 'extents' DumpExtent structures (each 16 bytes: start, end) follows the strings if extents > 0.
 *      The semantic of each extent is an inclusive [start, end] logical sector (or unit) range contributed by
 *      this hardware/software combination.
 *
 *  CRC semantics:
 *   - crc64 covers exactly 'length' bytes immediately following the header.
 *   - For legacy images with header.imageMajorVersion <= AARUF_VERSION_V1 the original C# writer produced a
 *     byte-swapped CRC; the library compensates internally (see process_dumphw_block()).
 *
 *  Invariants / validation recommendations:
 *   - identifier == BlockType::DumpHardwareBlock
 *   - Accumulated size of all (entry + strings + extents arrays) == length
 *   - All length fields are trusted only after bounds checking against remaining payload bytes
 *   - Strings are raw UTF-8 data with no implicit terminator
 *   - extents * sizeof(DumpExtent) fits inside remaining payload
 *
 *  Memory management notes (runtime library):
 *   - Each string is malloc'ed with +1 byte for terminator during processing.
 *   - Extents array is malloc'ed per entry when extents > 0.
 *   - See aaruformatContext::dumpHardwareEntriesWithData for owning pointers.
 *
 *  \warning Structures are packed; never rely on natural alignment when mapping from a byte buffer.
 *  \see DumpHardwareHeader
 *  \see DumpHardwareEntry
 *  \see DumpExtent (in context.h)
 *  \see BlockType
 */

/** \struct DumpHardwareHeader
 *  \brief Header that precedes a sequence of dump hardware entries and their variable-length payload.
 */
typedef struct DumpHardwareHeader
{
    uint32_t identifier;  ///< Block identifier, must be BlockType::DumpHardwareBlock.
    uint16_t entries;     ///< Number of DumpHardwareEntry records that follow.
    uint32_t length;      ///< Total payload bytes after this header (sum of entries, strings, and extents arrays).
    uint64_t crc64;       ///< CRC64-ECMA of the payload (byte-swapped for legacy v1 images, handled automatically).
} DumpHardwareHeader;

/** \struct DumpHardwareEntry
 *  \brief Per-environment length table describing subsequent UTF-8 strings and optional extent array.
 *
 *  Immediately after this structure the variable-length UTF‑8 strings appear in the documented order, each
 *  present only if its corresponding length is non-zero. No padding is present between strings. When all
 *  strings are consumed, an array of \ref DumpExtent follows if \ref extents > 0.
 *
 *  All length fields measure bytes (not characters) and exclude any in-memory NUL terminator added by the reader.
 *
 *  Typical semantics:
 *   - manufacturer/model/revision/firmware/serial identify the hardware device.
 *   - softwareName/softwareVersion/softwareOperatingSystem identify the acquisition software environment.
 *   - extents list which logical ranges this environment actually dumped (useful for multi-device composites).
 */
typedef struct DumpHardwareEntry
{
    uint32_t manufacturerLength;             ///< Length in bytes of manufacturer UTF-8 string.
    uint32_t modelLength;                    ///< Length in bytes of model UTF-8 string.
    uint32_t revisionLength;                 ///< Length in bytes of revision / hardware revision string.
    uint32_t firmwareLength;                 ///< Length in bytes of firmware version string.
    uint32_t serialLength;                   ///< Length in bytes of device serial number string.
    uint32_t softwareNameLength;             ///< Length in bytes of dumping software name string.
    uint32_t softwareVersionLength;          ///< Length in bytes of dumping software version string.
    uint32_t softwareOperatingSystemLength;  ///< Length in bytes of host operating system string.
    uint32_t extents;                        ///< Number of DumpExtent records following the strings (0 = none).
} DumpHardwareEntry;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_DUMP_H
