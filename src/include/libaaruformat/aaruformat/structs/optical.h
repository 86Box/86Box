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
 * */

#ifndef LIBAARUFORMAT_OPTICAL_H
#define LIBAARUFORMAT_OPTICAL_H

#pragma pack(push, 1)

/** \file aaruformat/structs/optical.h
 *  \brief On-disk structures describing optical disc tracks (Track list block).
 *
 *  An optical tracks block (identifier == BlockType::TracksBlock) stores a list of \ref TrackEntry
 *  records describing the logical layout of tracks and sessions for CD/DVD/BD and similar media.
 *
 *  Layout:
 *    TracksHeader (fixed)
 *    TrackEntry[ entries ] (array, packed)
 *
 *  CRC semantics:
 *   - TracksHeader::crc64 is a CRC64-ECMA over the contiguous TrackEntry array ONLY (header excluded).
 *   - For legacy images (imageMajorVersion <= AARUF_VERSION_V1) a byte swap is applied when verifying.
 *
 *  Field semantics (TrackEntry):
 *   - sequence: Logical track number (1..99 typical for CD). Values outside that range may encode extras.
 *   - type: Value from \ref TrackType (Audio, Data, Mode variants, etc.).
 *   - start / end: Inclusive Logical Block Address (LBA) bounds for the track. end >= start.
 *   - pregap: Number of sectors of pre-gap *preceding* the track's first user-accessible sector (can be 0 or negative
 *             if representing lead-in semantics; negative interpretation is implementation-defined).
 *   - session: Session number starting at 1 for multi-session discs (1 for single session).
 *   - isrc: 13-byte ISRC (raw code, no terminating null). If fewer significant characters, remaining bytes are 0.
 *   - flags: Bitmask of track/control flags. Unless otherwise specified, recommended mapping (mirrors CD subchannel Q
 *            control bits) is: bit0 Pre-emphasis, bit1 Copy permitted, bit2 Data track, bit3 Four-channel audio,
 *            bits4-7 reserved. Actual semantics may be extended by the format specification.
 *
 *  Invariants / validation recommendations:
 *   - identifier == BlockType::TracksBlock
 *   - entries * sizeof(TrackEntry) bytes are present after the header in the block image.
 *   - 1 <= sequence <= 99 for standard CD tracks (non-conforming values allowed but should be documented).
 *   - start <= end; pregap >= 0 (if negative pregaps unsupported in implementation).
 *   - ISRC bytes either all zero (no ISRC) or printable ASCII (A-Z 0-9 -) per ISO 3901 (without hyphen formatting).
 */

/** \struct TracksHeader
 *  \brief Header for an optical tracks block listing track entries.
 */
typedef struct TracksHeader
{
    uint32_t identifier;  ///< Block identifier (must be BlockType::TracksBlock).
    uint16_t entries;     ///< Number of TrackEntry records following this header.
    uint64_t crc64;  ///< CRC64-ECMA of the TrackEntry array (header excluded, legacy byte-swap for early versions).
} TracksHeader;

/** \struct TrackEntry
 *  \brief Single optical disc track descriptor (sequence, type, LBAs, session, ISRC, flags).
 */
typedef struct TrackEntry
{
    uint8_t sequence;  ///< Track number (1..99 typical for CD audio/data). 0 is a valid hidden track
                       ///< preceding track 1, as used by CD-i Ready and similar discs.
    uint8_t type;      ///< Track type (value from \ref TrackType).
    int64_t start;     ///< Inclusive starting LBA of the track.
    int64_t end;       ///< Inclusive ending LBA of the track.
    int64_t pregap;    ///< Pre-gap length in sectors preceding track start (0 if none).
    uint8_t session;   ///< Session number (1-based). 1 for single-session discs.
    uint8_t isrc[13];  ///< ISRC raw 13-byte code (no null terminator). All zeros if not present.
    uint8_t flags;     ///< Control / attribute bitfield (see file documentation for suggested bit mapping).
} TrackEntry;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_OPTICAL_H
