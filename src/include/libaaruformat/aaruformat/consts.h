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

#ifndef LIBAARUFORMAT_CONSTS_H
#define LIBAARUFORMAT_CONSTS_H

#ifdef __clang__
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedMacroInspection"
#endif

/** \file aaruformat/consts.h
 *  \brief Core public constants and compile‑time limits for the Aaru container format implementation.
 *
 *  This header exposes magic identifiers, format version selectors, resource limits, codec parameter bounds,
 *  and bit masks used across libaaruformat. All values are immutable interface contracts; changing them breaks
 *  backward compatibility unless a new format version is declared.
 *
 *  Summary:
 *   - Magic numbers (DIC_MAGIC, AARU_MAGIC) identify container families (legacy DiscImageChef vs AaruFormat).
 *   - Version macros distinguish format generations (V1 C# / legacy CRC endianness, V2 current C implementation).
 *   - Cache and table size limits provide protective upper bounds against runaway memory consumption.
 *   - Audio constants (SAMPLES_PER_SECTOR, MIN/MAX_FLAKE_BLOCK) align with Red Book (CD‑DA) and FLAC encoding best
 * practices.
 *   - CD_* masks assist with extracting flags / positional subfields in deduplicated Compact Disc sector tables.
 *   - CRC64 constants implement ECMA‑182 polynomial and standard seed, enabling deterministic end‑to‑end block
 * integrity.
 *
 *  Notes:
 *   - Magic values are stored little‑endian on disk when written as 64‑bit integers; when inspecting raw bytes make
 * sure to account for host endianness.
 *   - AARUF_VERSION must be incremented only when an incompatible on‑disk layout change is introduced.
 *   - MAX_DDT_ENTRY_CACHE is a soft upper bound sized to balance deduplication hit rate vs RAM; tune in future builds
 * via configuration if adaptive heuristics are introduced.
 *   - The LZMA properties length (5) derives from the standard LZMA header (lc/lp/pb + dict size) and is constant for
 *     raw LZMA streams used here.
 *   - FLAC sample block guidance: empirical evaluation shows >4608 samples per block does not yield meaningful ratio
 * gains for typical optical audio captures while increasing decode buffer size.
 *
 *  Thread safety: All macros are compile‑time constants; no synchronization required.
 *  Portability: Constants chosen to fit within 64‑bit targets; arithmetic assumes two's complement.
 */

/** Magic identifier for legacy DiscImageChef container (ASCII "DICMFRMT").
 *  Retained for backward compatibility / migration tooling. */
#define DIC_MAGIC  0x544D52464D434944ULL
/** Magic identifier for AaruFormat container (ASCII "AARUFRMT").
 *  Used in the primary header to assert correct file type. */
#define AARU_MAGIC 0x544D524655524141ULL

/** Current image format major version (incompatible changes bump this).
 *  Readers should reject headers with a higher number unless explicitly forward compatible. */
#define AARUF_VERSION    2
/** First on‑disk version (C# implementation).
 *  Quirk: CRC64 values were stored byte‑swapped relative to ECMA‑182 canonical output. */
#define AARUF_VERSION_V1 1
/** Second on‑disk version (C implementation).
 *  Introduced: extended header (GUID, feature bitmaps), hierarchical DDT v2, improved index (v2/v3),
 *  multi‑codec compression, refined metadata blocks. */
#define AARUF_VERSION_V2 2

/** Maximum read cache size (bytes). 512 MiB chosen to prevent excessive resident memory while
 *  still enabling efficient sequential and moderate random access patterns. */
#define MAX_CACHE_SIZE 536870912ULL

/** Size in bytes of the fixed LZMA properties header (lc/lp/pb + dictionary size). */
#define LZMA_PROPERTIES_LENGTH 5

/** Maximum number of cached DDT entry descriptors retained in memory for fast duplicate detection.
 *  At 16,000,000 entries with a compact structure, this caps hash_map overhead while covering large images.
 *  (Approx memory just for lookup bookkeeping: ~16 bytes * N ≈ 256 MB worst case; typical effective <50% of cap.) */
#define MAX_DDT_ENTRY_CACHE 16000000

/** Red Book (CD‑DA) PCM samples per 2352‑byte sector: 44,100 Hz / 75 sectors per second = 588 samples. */
#define SAMPLES_PER_SECTOR 588

/** FLAC maximum block size used for encoding audio sectors.
 *  Empirically >4608 samples yields diminishing compression returns and higher decode latency. */
#define MAX_FLAKE_BLOCK 4608
/** FLAC minimum block size. CUETools.Codecs.FLAKE does not accept blocks smaller than 256 samples. */
#define MIN_FLAKE_BLOCK 256

/** Mask for extracting correction / fix flags in Compact Disc suffix/prefix DDT entries.
 *  High 8 bits store status (see SectorStatus / CdFixFlags relationships). */
#define CD_XFIX_MASK 0xFF000000U

/** Bitmask of all featureIncompatible bits understood by this library version.
 *  Bit 0 = AARU_FEATURE_INCOMPAT_ZSTD (Zstandard compression). */
#define AARUF_KNOWN_INCOMPAT_FEATURES   0x1ULL
/** Bitmask of all featureCompatibleRo bits understood by this library version.
 *  Bit 0 = AARU_FEATURE_ROCOMPAT_ERASURE (erasure coding parity data). */
#define AARUF_KNOWN_ROCOMPAT_FEATURES   0x1ULL
/** Bitmask of all featureCompatible bits understood by this library version.
 *  Bit 0 = AARU_FEATURE_RW_BLAKE3 (BLAKE3 checksums). */
#define AARUF_KNOWN_COMPAT_FEATURES     0x1ULL
/** Mask for extracting positional index (lower 24 bits) in Compact Disc suffix/prefix deduplicated block entries. */
#define CD_DFIX_MASK 0x00FFFFFFU

/** Magic number at the end of the recovery footer: "AVRECMFR" in ASCII little-endian. */
#define AARU_RECOVERY_FOOTER_MAGIC 0x52464D4345525641ULL

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif  // LIBAARUFORMAT_CONSTS_H
