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

#ifndef LIBAARUFORMAT_AARU_H
#define LIBAARUFORMAT_AARU_H

#ifdef __clang_
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#endif

#include <stdint.h>

/** \file aaru.h
 *  \brief Public high-level API types: media classifications, per-sector / per-media tag enums and image summary.
 *
 *  This header provides:
 *   - \ref MediaType : exhaustive enumeration of recognized physical / logical media formats
 *     (disks, tapes, cards, optics).
 *   - \ref ImageInfo : summary metadata extracted from an opened image (sizes, creator, geometry,
 *     drive/media identity).
 *   - \ref SectorTagType : bit-addressable per-sector auxiliary data kinds (prefix/suffix/ECC/subchannel, etc.).
 *   - \ref MediaTagType : higher-level per-media metadata structures (TOCs, format information, IDs,
 *     inquiry pages, etc.).
 *
 *  Value range conventions (MediaType): groups of contiguous numeric IDs are reserved for technology families;
 *  comments in the enum mark the ranges. This stable numeric mapping forms part of the external on-disk and
 *  API contract—do not renumber existing entries. New media types must be appended in an unused range or in an
 *  explicitly extended tail section to preserve backward compatibility.
 *
 *  Thread safety: All types here are PODs / enumerations and are safe for concurrent read-only access.
 *  Encoding: All UTF-8 textual pointers inside \ref ImageInfo refer to NUL-terminated dynamically allocated
 *  buffers owned by the context that produced them (caller copies if persistence is required beyond context lifetime).
 *
 *  See also: aaruformat/context.h for runtime context internals and metadata/dump structures.
 */

/** \addtogroup MediaTypes Media type classification
 *  Comprehensive list of supported physical / logical media. Grouped by numeric ranges in the enum.
 *  @{ */
/** \enum MediaType
 *  \brief Enumerates every recognized media / cartridge / optical / tape / card / disk format.
 *
 *  The numeric values are ABI- and file-format-stable. They are stored verbatim in serialized Aaru images
 *  and MUST NOT be changed. Only append new identifiers. When adding new values document them clearly.
 *
 *  Typical usage patterns:
 *   - Switch over a MediaType to choose decoding / geometry heuristics.
 *   - Persist the numeric value in sidecar metadata (never stringify and parse unless necessary).
 *   - Provide user-facing localization by mapping the enum to translated labels externally.
 *
 *  Ranges (do not reorder existing values):
 *   - 0–9: Generic / unknown basic categories.
 *   - 10–39: Compact Disc (CD) family (Red/Yellow/Green/Orange Books & variants).
 *   - 40–50: DVD family.
 *   - 51–59: HD-DVD family.
 *   - 60–69: Blu-ray family.
 *   - 70–79: Rare / niche optical formats.
 *   - 80–89: LaserDisc and derivatives.
 *   - 90–99: MiniDisc related.
 *   - 100–109: Plasmon UDO.
 *   - 110–169: Console / game optical & cartridge media (Sony / Sega / others).
 *   - 170–229: Additional console & early optical / cartridge variants.
 *   - 180–289: Floppy standards (Apple / IBM / NEC / ECMA / vendor-specific).
 *   - 290–309: Non-standard / extended PC floppy formats.
 *   - 310–359: Tape & removable disk (OnStream, AIT, Iomega, etc.).
 *   - 360–399: Audio/video and DEC / Exabyte families.
 *   - 400–699: Removable cards (PCMCIA, MemoryStick, SD, MMC), additional tape/storage families.
 *   - 700–739: DEC hard disks & Imation removable media.
 *   - 740–749: Niche video disc formats (VideoNow, etc.).
 *   - 750+: Extended / rare or future additions.
 *
 *  Invariants / recommendations:
 *   - Treat Unknown (0) as a safe default; do not assume geometry.
 *   - Where possible, prefer feature probing (e.g. presence of TOC) instead of relying solely on the enum.
 *   - New client code should always have a default clause in switch statements to remain forward compatible.
 */
// NOLINTBEGIN(readability-identifier-naming)
typedef enum
{
    // Generics, types 0 to 9
    UnknownMedia = 0,  ///< Unknown disk type
    UnknownMO    = 1,  ///< Unknown magneto-optical
    GENERIC_HDD  = 2,  ///< Generic hard disk
    Microdrive   = 3,  ///< Microdrive type hard disk
    Zone_HDD     = 4,  ///< Zoned hard disk
    FlashDrive   = 5,  ///< USB flash drives
    // Generics, types 0 to 9

    // Somewhat standard Compact Disc formats, types 10 to 39
    CD         = 10,  ///< Any unknown or standard violating CD
    CDDA       = 11,  ///< CD Digital Audio (Red Book)
    CDG        = 12,  ///< CD+G (Red Book)
    CDEG       = 13,  ///< CD+EG (Red Book)
    CDI        = 14,  ///< CD-i (Green Book)
    CDROM      = 15,  ///< CD-ROM (Yellow Book)
    CDROMXA    = 16,  ///< CD-ROM XA (Yellow Book)
    CDPLUS     = 17,  ///< CD+ (Blue Book)
    CDMO       = 18,  ///< CD-MO (Orange Book)
    CDR        = 19,  ///< CD-Recordable (Orange Book)
    CDRW       = 20,  ///< CD-ReWritable (Orange Book)
    CDMRW      = 21,  ///< Mount-Rainier CD-RW
    VCD        = 22,  ///< Video CD (White Book)
    SVCD       = 23,  ///< Super Video CD (White Book)
    PCD        = 24,  ///< Photo CD (Beige Book)
    SACD       = 25,  ///< Super Audio CD (Scarlet Book)
    DDCD       = 26,  ///< Double-Density CD-ROM (Purple Book)
    DDCDR      = 27,  ///< DD CD-R (Purple Book)
    DDCDRW     = 28,  ///< DD CD-RW (Purple Book)
    DTSCD      = 29,  ///< DTS audio CD (non-standard)
    CDMIDI     = 30,  ///< CD-MIDI (Red Book)
    CDV        = 31,  ///< CD-Video (ISO/IEC 61104)
    PD650      = 32,  ///< 120mm, Phase-Change, 1298496 sectors, 512 bytes/sector, PD650, ECMA-240, ISO 15485
    PD650_WORM = 33,  ///< 120mm, Write-Once, 1281856 sectors, 512 bytes/sector, PD650, ECMA-240, ISO 15485
    CDIREADY   = 34,  ///< CD-i Ready, contains a track before the first TOC track, in mode 2, and all TOC tracks are
                      ///< Audio. Subchannel marks track as audio pause.
    FMTOWNS    = 35,  ///< Fujitsu FM Towns bootable CD (mixed-mode proprietary extensions)
    // Somewhat standard Compact Disc formats, types 10 to 39

    // Standard DVD formats, types 40 to 50
    DVDROM      = 40,  ///< DVD-ROM (applies to DVD Video and DVD Audio)
    DVDR        = 41,  ///< DVD-R
    DVDRW       = 42,  ///< DVD-RW
    DVDPR       = 43,  ///< DVD+R
    DVDPRW      = 44,  ///< DVD+RW
    DVDPRWDL    = 45,  ///< DVD+RW DL
    DVDRDL      = 46,  ///< DVD-R DL
    DVDPRDL     = 47,  ///< DVD+R DL
    DVDRAM      = 48,  ///< DVD-RAM
    DVDRWDL     = 49,  ///< DVD-RW DL
    DVDDownload = 50,  ///< DVD-Download
    // Standard DVD formats, types 40 to 50

    // Standard HD-DVD formats, types 51 to 59
    HDDVDROM  = 51,  ///< HD DVD-ROM (applies to HD DVD Video)
    HDDVDRAM  = 52,  ///< HD DVD-RAM
    HDDVDR    = 53,  ///< HD DVD-R
    HDDVDRW   = 54,  ///< HD DVD-RW
    HDDVDRDL  = 55,  ///< HD DVD-R DL
    HDDVDRWDL = 56,  ///< HD DVD-RW DL
    // Standard HD-DVD formats, types 51 to 59

    // Standard Blu-ray formats, types 60 to 69
    BDROM  = 60,  ///< BD-ROM (and BD Video)
    BDR    = 61,  ///< BD-R
    BDRE   = 62,  ///< BD-RE
    BDRXL  = 63,  ///< BD-R XL
    BDREXL = 64,  ///< BD-RE XL
    UHDBD  = 65,  ///< Ultra HD Blu-ray
    // Standard Blu-ray formats, types 60 to 69

    // Rare or uncommon optical standards, types 70 to 79
    EVD   = 70,  ///< Enhanced Versatile Disc
    FVD   = 71,  ///< Forward Versatile Disc
    HVD   = 72,  ///< Holographic Versatile Disc
    CBHD  = 73,  ///< China Blue High Definition
    HDVMD = 74,  ///< High Definition Versatile Multilayer Disc
    VCDHD = 75,  ///< Versatile Compact Disc High Density
    SVOD  = 76,  ///< Stacked Volumetric Optical Disc
    FDDVD = 77,  ///< Five Dimensional disc
    // Rare or uncommon optical standards, types 70 to 79

    // LaserDisc based, types 80 to 89
    LD     = 80,  ///< Pioneer LaserDisc
    LDROM  = 81,  ///< Pioneer LaserDisc data
    LDROM2 = 82,
    LVROM  = 83,
    MegaLD = 84,
    // LaserDisc based, types 80 to 89

    // MiniDisc based, types 90 to 99
    HiMD    = 90,  ///< Sony Hi-MD
    MD      = 91,  ///< Sony MiniDisc
    MDData  = 92,  ///< MiniDisc DATA (HiFD style data-only variant)
    MDData2 = 93,  ///< High-capacity MiniDisc DATA 2
    MD60    = 94,  ///< Sony MiniDisc, 60 minutes, formatted with Hi-MD format
    MD74    = 95,  ///< Sony MiniDisc, 74 minutes, formatted with Hi-MD format
    MD80    = 96,  ///< Sony MiniDisc, 80 minutes, formatted with Hi-MD format
    // MiniDisc based, types 90 to 99

    // Plasmon UDO, types 100 to 109
    UDO = 100,  ///< 5.25", Phase-Change, 1834348 sectors, 8192 bytes/sector, Ultra Density Optical, ECMA-350, ISO 17345
    UDO2 =
        101,  ///< 5.25", Phase-Change, 3669724 sectors, 8192 bytes/sector, Ultra Density Optical 2, ECMA-380, ISO 11976
    UDO2_WORM =
        102,  ///< 5.25", Write-Once, 3668759 sectors, 8192 bytes/sector, Ultra Density Optical 2, ECMA-380, ISO 11976
    // Plasmon UDO, types 100 to 109

    // Sony game media, types 110 to 129
    PlayStationMemoryCard   = 110,  ///< Sony PlayStation (PS1) memory card (128 KiB, 8 KB blocks)
    PlayStationMemoryCard2  = 111,  ///< Sony PlayStation 2 memory card (MagicGate, 8 MiB)
    PS1CD                   = 112,  ///< Sony PlayStation game CD
    PS2CD                   = 113,  ///< Sony PlayStation 2 game CD
    PS2DVD                  = 114,  ///< Sony PlayStation 2 game DVD
    PS3DVD                  = 115,  ///< Sony PlayStation 3 game DVD
    PS3BD                   = 116,  ///< Sony PlayStation 3 game Blu-ray
    PS4BD                   = 117,  ///< Sony PlayStation 4 game Blu-ray
    UMD                     = 118,  ///< Sony PlayStation Portable Universal Media Disc (ECMA-365)
    PlayStationVitaGameCard = 119,  ///< PS Vita NV memory card (proprietary flash)
    PS5BD                   = 120,  ///< Sony PlayStation 5 game Blu-ray
    // Sony game media, types 110 to 129

    // Microsoft game media, types 130 to 149
    XGD  = 130,  ///< Microsoft X-box Game Disc
    XGD2 = 131,  ///< Microsoft X-box 360 Game Disc
    XGD3 = 132,  ///< Microsoft X-box 360 Game Disc
    XGD4 = 133,  ///< Microsoft X-box One Game Disc
    // Microsoft game media, types 130 to 149

    // Sega game media, types 150 to 169
    MEGACD   = 150,  ///< Sega MegaCD
    SATURNCD = 151,  ///< Sega Saturn disc
    GDROM    = 152,  ///< Sega/Yamaha Gigabyte Disc
    GDR      = 153,  ///< Sega/Yamaha recordable Gigabyte Disc
    SegaCard = 154,  ///< Sega My Card / Sega Card (SG-1000 / Mark III)
    MilCD    = 155,  ///< Sega Dreamcast MIL-CD enhanced multimedia disc
    // Sega game media, types 150 to 169

    // Other game media, types 170 to 179
    HuCard      = 170,  ///< PC-Engine / TurboGrafx cartridge
    SuperCDROM2 = 171,  ///< PC-Engine / TurboGrafx CD
    JaguarCD    = 172,  ///< Atari Jaguar CD
    ThreeDO     = 173,  ///< 3DO CD
    PCFX        = 174,  ///< NEC PC-FX
    NeoGeoCD    = 175,  ///< NEO-GEO CD
    CDTV        = 176,  ///< Commodore CDTV
    CD32        = 177,  ///< Amiga CD32
    Nuon        = 178,  ///< Nuon (DVD based videogame console)
    Playdia     = 179,  ///< Bandai Playdia
    // Other game media, types 170 to 179

    // Apple standard floppy format, types 180 to 189
    Apple32SS     = 180,  ///< 5.25", SS, DD, 35 tracks, 13 spt, 256 bytes/sector, GCR
    Apple32DS     = 181,  ///< 5.25", DS, DD, 35 tracks, 13 spt, 256 bytes/sector, GCR
    Apple33SS     = 182,  ///< 5.25", SS, DD, 35 tracks, 16 spt, 256 bytes/sector, GCR
    Apple33DS     = 183,  ///< 5.25", DS, DD, 35 tracks, 16 spt, 256 bytes/sector, GCR
    AppleSonySS   = 184,  ///< 3.5", SS, DD, 80 tracks, 8 to 12 spt, 512 bytes/sector, GCR
    AppleSonyDS   = 185,  ///< 3.5", DS, DD, 80 tracks, 8 to 12 spt, 512 bytes/sector, GCR
    AppleFileWare = 186,  ///< 5.25", DS, ?D, ?? tracks, ?? spt, 512 bytes/sector, GCR, opposite side heads, aka Twiggy
    // Apple standard floppy format

    // IBM/Microsoft PC floppy formats, types 190 to 209
    DOS_525_SS_DD_8 = 190,  ///< 5.25", SS, DD, 40 tracks, 8 spt, 512 bytes/sector, MFM
    DOS_525_SS_DD_9 = 191,  ///< 5.25", SS, DD, 40 tracks, 9 spt, 512 bytes/sector, MFM
    DOS_525_DS_DD_8 = 192,  ///< 5.25", DS, DD, 40 tracks, 8 spt, 512 bytes/sector, MFM
    DOS_525_DS_DD_9 = 193,  ///< 5.25", DS, DD, 40 tracks, 9 spt, 512 bytes/sector, MFM
    DOS_525_HD      = 194,  ///< 5.25", DS, HD, 80 tracks, 15 spt, 512 bytes/sector, MFM
    DOS_35_SS_DD_8  = 195,  ///< 3.5", SS, DD, 80 tracks, 8 spt, 512 bytes/sector, MFM
    DOS_35_SS_DD_9  = 196,  ///< 3.5", SS, DD, 80 tracks, 9 spt, 512 bytes/sector, MFM
    DOS_35_DS_DD_8  = 197,  ///< 3.5", DS, DD, 80 tracks, 8 spt, 512 bytes/sector, MFM
    DOS_35_DS_DD_9  = 198,  ///< 3.5", DS, DD, 80 tracks, 9 spt, 512 bytes/sector, MFM
    DOS_35_HD       = 199,  ///< 3.5", DS, HD, 80 tracks, 18 spt, 512 bytes/sector, MFM
    DOS_35_ED       = 200,  ///< 3.5", DS, ED, 80 tracks, 36 spt, 512 bytes/sector, MFM
    DMF             = 201,  ///< 3.5", DS, HD, 80 tracks, 21 spt, 512 bytes/sector, MFM
    DMF_82          = 202,  ///< 3.5", DS, HD, 82 tracks, 21 spt, 512 bytes/sector, MFM
    XDF_525 = 203,  ///< 5.25", DS, HD, 80 tracks, ? spt, ??? + ??? + ??? bytes/sector, MFM track 0 = ??15 sectors, 512
                    ///< bytes/sector, falsified to DOS as 19 spt, 512 bps
    XDF_35 = 204,  ///< 3.5", DS, HD, 80 tracks, 4 spt, 8192 + 2048 + 1024 + 512 bytes/sector, MFM track 0 = 19 sectors,
                   ///< 512 bytes/sector, falsified to DOS as 23 spt, 512 bps
    // IBM/Microsoft PC standard floppy formats, types 190 to 209

    // IBM standard floppy formats, types 210 to 219
    IBM23FD     = 210,  ///< 8", SS, SD, 32 tracks, 8 spt, 319 bytes/sector, FM
    IBM33FD_128 = 211,  ///< 8", SS, SD, 73 tracks, 26 spt, 128 bytes/sector, FM
    IBM33FD_256 = 212,  ///< 8", SS, SD, 74 tracks, 15 spt, 256 bytes/sector, FM, track 0 = 26 sectors, 128 bytes/sector
    IBM33FD_512 = 213,  ///< 8", SS, SD, 74 tracks, 8 spt, 512 bytes/sector, FM, track 0 = 26 sectors, 128 bytes/sector
    IBM43FD_128 = 214,  ///< 8", DS, SD, 74 tracks, 26 spt, 128 bytes/sector, FM, track 0 = 26 sectors, 128 bytes/sector
    IBM43FD_256 = 215,  ///< 8", DS, SD, 74 tracks, 26 spt, 256 bytes/sector, FM, track 0 = 26 sectors, 128 bytes/sector
    IBM53FD_256 = 216,  ///< 8", DS, DD, 74 tracks, 26 spt, 256 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128
                        ///< bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
    IBM53FD_512 = 217,  ///< 8", DS, DD, 74 tracks, 15 spt, 512 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128
                        ///< bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
    IBM53FD_1024 = 218,  ///< 8", DS, DD, 74 tracks, 8 spt, 1024 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128
                         ///< bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
    // IBM standard floppy formats, types 210 to 219

    // DEC standard floppy formats, types 220 to 229
    RX01 = 220,  ///< 8", SS, DD, 77 tracks, 26 spt, 128 bytes/sector, FM
    RX02 = 221,  ///< 8", SS, DD, 77 tracks, 26 spt, 256 bytes/sector, FM/MFM
    RX03 = 222,  ///< 8", DS, DD, 77 tracks, 26 spt, 256 bytes/sector, FM/MFM
    RX50 = 223,  ///< 5.25", SS, DD, 80 tracks, 10 spt, 512 bytes/sector, MFM
    // DEC standard floppy formats, types 220 to 229

    // Acorn standard floppy formats, types 230 to 239
    ACORN_525_SS_SD_40 = 230,  ///< 5,25", SS, SD, 40 tracks, 10 spt, 256 bytes/sector, FM
    ACORN_525_SS_SD_80 = 231,  ///< 5,25", SS, SD, 80 tracks, 10 spt, 256 bytes/sector, FM
    ACORN_525_SS_DD_40 = 232,  ///< 5,25", SS, DD, 40 tracks, 16 spt, 256 bytes/sector, MFM
    ACORN_525_SS_DD_80 = 233,  ///< 5,25", SS, DD, 80 tracks, 16 spt, 256 bytes/sector, MFM
    ACORN_525_DS_DD    = 234,  ///< 5,25", DS, DD, 80 tracks, 16 spt, 256 bytes/sector, MFM
    ACORN_35_DS_DD     = 235,  ///< 3,5", DS, DD, 80 tracks, 5 spt, 1024 bytes/sector, MFM
    ACORN_35_DS_HD     = 236,  ///< 3,5", DS, HD, 80 tracks, 10 spt, 1024 bytes/sector, MFM
    // Acorn standard floppy formats, types 230 to 239

    // Atari standard floppy formats, types 240 to 249
    ATARI_525_SD      = 240,  ///< 5,25", SS, SD, 40 tracks, 18 spt, 128 bytes/sector, FM
    ATARI_525_ED      = 241,  ///< 5,25", SS, ED, 40 tracks, 26 spt, 128 bytes/sector, MFM
    ATARI_525_DD      = 242,  ///< 5,25", SS, DD, 40 tracks, 18 spt, 256 bytes/sector, MFM
    ATARI_35_SS_DD    = 243,  ///< 3,5", SS, DD, 80 tracks, 10 spt, 512 bytes/sector, MFM
    ATARI_35_DS_DD    = 244,  ///< 3,5", DS, DD, 80 tracks, 10 spt, 512 bytes/sector, MFM
    ATARI_35_SS_DD_11 = 245,  ///< 3,5", SS, DD, 80 tracks, 11 spt, 512 bytes/sector, MFM
    ATARI_35_DS_DD_11 = 246,  ///< 3,5", DS, DD, 80 tracks, 11 spt, 512 bytes/sector, MFM
    // Atari standard floppy formats, types 240 to 249

    // Commodore standard floppy formats, types 250 to 259
    CBM_35_DD       = 250,  ///< 3,5", DS, DD, 80 tracks, 10 spt, 512 bytes/sector, MFM (1581)
    CBM_AMIGA_35_DD = 251,  ///< 3,5", DS, DD, 80 tracks, 11 spt, 512 bytes/sector, MFM (Amiga)
    CBM_AMIGA_35_HD = 252,  ///< 3,5", DS, HD, 80 tracks, 22 spt, 512 bytes/sector, MFM (Amiga)
    CBM_1540        = 253,  ///< 5,25", SS, DD, 35 tracks, GCR
    CBM_1540_Ext    = 254,  ///< 5,25", SS, DD, 40 tracks, GCR
    CBM_1571        = 255,  ///< 5,25", DS, DD, 35 tracks, GCR
    // Commodore standard floppy formats, types 250 to 259

    // NEC/SHARP standard floppy formats, types 260 to 269
    NEC_8_SD     = 260,          ///< 8", DS, SD, 77 tracks, 26 spt, 128 bytes/sector, FM
    NEC_8_DD     = 261,          ///< 8", DS, DD, 77 tracks, 26 spt, 256 bytes/sector, MFM
    NEC_525_SS   = 262,          ///< 5.25", SS, SD, 80 tracks, 16 spt, 256 bytes/sector, FM
    NEC_525_DS   = 263,          ///< 5.25", DS, SD, 80 tracks, 16 spt, 256 bytes/sector, MFM
    NEC_525_HD   = 264,          ///< 5,25", DS, HD, 77 tracks, 8 spt, 1024 bytes/sector, MFM
    NEC_35_HD_8  = 265,          ///< 3,5", DS, HD, 77 tracks, 8 spt, 1024 bytes/sector, MFM, aka mode 3
    NEC_35_HD_15 = 266,          ///< 3,5", DS, HD, 80 tracks, 15 spt, 512 bytes/sector, MFM
    NEC_35_TD    = 267,          ///< 3,5", DS, TD, 240 tracks, 38 spt, 512 bytes/sector, MFM
    SHARP_525    = NEC_525_HD,   ///< 5,25", DS, HD, 77 tracks, 8 spt, 1024 bytes/sector, MFM
    SHARP_525_9  = 268,          ///< 3,5", DS, HD, 80 tracks, 9 spt, 1024 bytes/sector, MFM
    SHARP_35     = NEC_35_HD_8,  ///< 3,5", DS, HD, 77 tracks, 8 spt, 1024 bytes/sector, MFM, aka mode 3
    SHARP_35_9   = 269,          ///< 3,5", DS, HD, 80 tracks, 9 spt, 1024 bytes/sector, MFM
    // NEC/SHARP standard floppy formats, types 260 to 269

    // ECMA floppy standards, types 270 to 289
    ECMA_99_8  = 270,  ///< 5,25", DS, DD, 80 tracks, 8 spt, 1024 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128
                       ///< bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
    ECMA_99_15 = 271,  ///< 5,25", DS, DD, 77 tracks, 15 spt, 512 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128
                       ///< bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
    ECMA_99_26 = 272,  ///< 5,25", DS, DD, 77 tracks, 26 spt, 256 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128
                       ///< bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
    ECMA_100   = DOS_35_DS_DD_9,  ///< 3,5", DS, DD, 80 tracks, 9 spt, 512 bytes/sector, MFM
    ECMA_125   = DOS_35_HD,       ///< 3,5", DS, HD, 80 tracks, 18 spt, 512 bytes/sector, MFM
    ECMA_147   = DOS_35_ED,       ///< 3,5", DS, ED, 80 tracks, 36 spt, 512 bytes/sector, MFM
    ECMA_54    = 273,             ///< 8", SS, SD, 77 tracks, 26 spt, 128 bytes/sector, FM
    ECMA_59    = 274,             ///< 8", DS, SD, 77 tracks, 26 spt, 128 bytes/sector, FM
    ECMA_66 =
        275,  ///< 5,25", SS, DD, 35 tracks, 9 spt, 256 bytes/sector, FM, track 0 side 0 = 16 sectors, 128 bytes/sector
    ECMA_69_8  = 276,  ///< 8", DS, DD, 77 tracks, 8 spt, 1024 bytes/sector, FM, track 0 side 0 = 26 sectors, 128
                       ///< bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
    ECMA_69_15 = 277,  ///< 8", DS, DD, 77 tracks, 15 spt, 512 bytes/sector, FM, track 0 side 0 = 26 sectors, 128
                       ///< bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
    ECMA_69_26 = 278,  ///< 8", DS, DD, 77 tracks, 26 spt, 256 bytes/sector, FM, track 0 side 0 = 26 sectors, 128
                       ///< bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
    ECMA_70    = 279,  ///< 5,25", DS, DD, 40 tracks, 16 spt, 256 bytes/sector, FM, track 0 side 0 = 16 sectors, 128
                       ///< bytes/sector, track 0 side 1 = 16 sectors, 256 bytes/sector
    ECMA_78    = 280,
    ECMA_78_2  = 281,  ///< 5,25", DS, DD, 80 tracks, 9 spt, 512 bytes/sector, FM
    // ECMA floppy standards, types 270 to 289

    // Non-standard PC formats (FDFORMAT, 2M, etc), types 290 to 308
    FDFORMAT_525_DD = 290,  ///< 5,25", DS, DD, 82 tracks, 10 spt, 512 bytes/sector, MFM
    FDFORMAT_525_HD = 291,  ///< 5,25", DS, HD, 82 tracks, 17 spt, 512 bytes/sector, MFM
    FDFORMAT_35_DD  = 292,  ///< 3,5", DS, DD, 82 tracks, 10 spt, 512 bytes/sector, MFM
    FDFORMAT_35_HD  = 293,  ///< 3,5", DS, HD, 82 tracks, 21 spt, 512 bytes/sector, MFM
    // Non-standard PC formats (FDFORMAT, 2M, etc), types 290 to 308

    // Apricot ACT standard floppy formats, type 309
    Apricot_35 = 309,  ///< 3.5", DS, DD, 70 tracks, 9 spt, 512 bytes/sector, MFM
    // Apricot ACT standard floppy formats, type 309

    // OnStream ADR, types 310 to 319
    ADR2120 = 310,
    ADR260  = 311,
    ADR30   = 312,
    ADR50   = 313,
    // OnStream ADR, types 310 to 319

    // Advanced Intelligent Tape, types 320 to 339
    AIT1      = 320,
    AIT1Turbo = 321,
    AIT2      = 322,
    AIT2Turbo = 323,
    AIT3      = 324,
    AIT3Ex    = 325,
    AIT3Turbo = 326,
    AIT4      = 327,
    AIT5      = 328,
    AITETurbo = 329,
    SAIT1     = 330,
    SAIT2     = 331,
    // Advanced Intelligent Tape, types 320 to 339

    // Iomega, types 340 to 359
    Bernoulli    = 340,
    Bernoulli2   = 341,
    Ditto        = 342,
    DittoMax     = 343,
    Jaz          = 344,
    Jaz2         = 345,
    PocketZip    = 346,
    REV120       = 347,
    REV35        = 348,
    REV70        = 349,
    ZIP100       = 350,
    ZIP250       = 351,
    ZIP750       = 352,
    Bernoulli35  = 353,  ///< 5⅓" Bernoulli Box II disk with 35Mb capacity
    Bernoulli44  = 354,  ///< 5⅓" Bernoulli Box II disk with 44Mb capacity
    Bernoulli65  = 355,  ///< 5⅓" Bernoulli Box II disk with 65Mb capacity
    Bernoulli90  = 356,  ///< 5⅓" Bernoulli Box II disk with 90Mb capacity
    Bernoulli105 = 357,  ///< 5⅓" Bernoulli Box II disk with 105Mb capacity
    Bernoulli150 = 358,  ///< 5⅓" Bernoulli Box II disk with 150Mb capacity
    Bernoulli230 = 359,  ///< 5⅓" Bernoulli Box II disk with 230Mb capacity
                         // Iomega, types 340 to 359

    // Audio or video media, types 360 to 369
    CompactCassette = 360,
    Data8           = 361,
    MiniDV          = 362,
    Dcas25          = 363,  ///< D/CAS-25: Digital data on Compact Cassette form factor, special magnetic media, 9-track
    Dcas85  = 364,  ///< D/CAS-85: Digital data on Compact Cassette form factor, special magnetic media, 17-track
    Dcas103 = 365,  ///< D/CAS-103: Digital data on Compact Cassette form factor, special magnetic media, 21-track
    // Audio media, types 360 to 369

    // CompactFlash Association, types 370 to 379
    CFast             = 370,
    CompactFlash      = 371,
    CompactFlashType2 = 372,
    // CompactFlash Association, types 370 to 379

    // Digital Audio Tape / Digital Data Storage, types 380 to 389
    DigitalAudioTape = 380,
    DAT160           = 381,
    DAT320           = 382,
    DAT72            = 383,
    DDS1             = 384,
    DDS2             = 385,
    DDS3             = 386,
    DDS4             = 387,
    // Digital Audio Tape / Digital Data Storage, types 380 to 389

    // DEC, types 390 to 399
    CompactTapeI  = 390,
    CompactTapeII = 391,
    DECtapeII     = 392,
    DLTtapeIII    = 393,
    DLTtapeIIIxt  = 394,
    DLTtapeIV     = 395,
    DLTtapeS4     = 396,
    SDLT1         = 397,
    SDLT2         = 398,
    VStapeI       = 399,
    // DEC, types 390 to 399

    // Exatape, types 400 to 419
    Exatape15m    = 400,
    Exatape22m    = 401,
    Exatape22mAME = 402,
    Exatape28m    = 403,
    Exatape40m    = 404,
    Exatape45m    = 405,
    Exatape54m    = 406,
    Exatape75m    = 407,
    Exatape76m    = 408,
    Exatape80m    = 409,
    Exatape106m   = 410,
    Exatape160mXL = 411,
    Exatape112m   = 412,
    Exatape125m   = 413,
    Exatape150m   = 414,
    Exatape170m   = 415,
    Exatape225m   = 416,
    // Exatape, types 400 to 419

    // PCMCIA / ExpressCard, types 420 to 429
    ExpressCard34 = 420,
    ExpressCard54 = 421,
    PCCardTypeI   = 422,
    PCCardTypeII  = 423,
    PCCardTypeIII = 424,
    PCCardTypeIV  = 425,
    // PCMCIA / ExpressCard, types 420 to 429

    // SyQuest, types 430 to 449
    EZ135  = 430,
    EZ230  = 431,
    Quest  = 432,
    SparQ  = 433,
    SQ100  = 434,
    SQ200  = 435,
    SQ300  = 436,
    SQ310  = 437,
    SQ327  = 438,
    SQ400  = 439,
    SQ800  = 440,
    SQ1500 = 441,
    SQ2000 = 442,
    SyJet  = 443,
    // SyQuest, types 430 to 449

    // Nintendo, types 450 to 469
    FamicomGamePak        = 450,  ///< Nintendo Famicom cartridge
    GameBoyAdvanceGamePak = 451,  ///< Nintendo Game Boy Advance cartridge
    GameBoyGamePak        = 452,  ///< Nintendo Game Boy / Color cartridge
    GOD                   = 453,  ///< Nintendo GameCube Optical Disc
    N64DD                 = 454,
    N64GamePak            = 455,  ///< Nintendo 64 cartridge
    NESGamePak            = 456,  ///< Nintendo NES cartridge
    Nintendo3DSGameCard   = 457,  ///< Nintendo 3DS ROM card
    NintendoDiskCard      = 458,  ///< Famicom Disk System disk
    NintendoDSGameCard    = 459,  ///< Nintendo DS ROM card
    NintendoDSiGameCard   = 460,  ///< Nintendo DSi enhanced ROM card
    SNESGamePak           = 461,  ///< Nintendo SNES (PAL/JPN) cartridge
    SNESGamePakUS         = 462,  ///< Nintendo SNES (US) cartridge (different shell)
    WOD                   = 463,  ///< Nintendo Wii Optical Disc
    WUOD                  = 464,  ///< Nintendo Wii U Optical Disc
    SwitchGameCard        = 465,  ///< Nintendo Switch Game Card (NV flash)
    // Nintendo, types 450 to 469

    // IBM Tapes, types 470 to 479
    IBM3470  = 470,
    IBM3480  = 471,
    IBM3490  = 472,
    IBM3490E = 473,
    IBM3592  = 474,
    // IBM Tapes, types 470 to 479

    // LTO Ultrium, types 480 to 509
    LTO      = 480,
    LTO2     = 481,
    LTO3     = 482,
    LTO3WORM = 483,
    LTO4     = 484,
    LTO4WORM = 485,
    LTO5     = 486,
    LTO5WORM = 487,
    LTO6     = 488,
    LTO6WORM = 489,
    LTO7     = 490,
    LTO7WORM = 491,
    // LTO Ultrium, types 480 to 509

    // MemoryStick, types 510 to 519
    MemoryStick       = 510,
    MemoryStickDuo    = 511,
    MemoryStickMicro  = 512,
    MemoryStickPro    = 513,
    MemoryStickProDuo = 514,
    // MemoryStick, types 510 to 519

    // SecureDigital, types 520 to 529
    microSD       = 520,  ///< microSD / microSDHC / microSDXC card
    miniSD        = 521,  ///< miniSD card
    SecureDigital = 522,  ///< Full-size SD / SDHC / SDXC card
    // SecureDigital, types 520 to 529

    // MultiMediaCard, types 530 to 539
    MMC       = 530,  ///< MultiMediaCard (legacy)
    MMCmicro  = 531,  ///< MMCmicro (RS-MMC form)
    RSMMC     = 532,  ///< Reduced Size MMC
    MMCplus   = 533,  ///< MMCplus (high speed)
    MMCmobile = 534,  ///< MMCmobile (dual voltage)
    // MultiMediaCard, types 530 to 539

    // SLR, types 540 to 569
    MLR1        = 540,
    MLR1SL      = 541,
    MLR3        = 542,
    SLR1        = 543,
    SLR2        = 544,
    SLR3        = 545,
    SLR32       = 546,
    SLR32SL     = 547,
    SLR4        = 548,
    SLR5        = 549,
    SLR5SL      = 550,
    SLR6        = 551,
    SLRtape7    = 552,
    SLRtape7SL  = 553,
    SLRtape24   = 554,
    SLRtape24SL = 555,
    SLRtape40   = 556,
    SLRtape50   = 557,
    SLRtape60   = 558,
    SLRtape75   = 559,
    SLRtape100  = 560,
    SLRtape140  = 561,
    // SLR, types 540 to 569

    // QIC, types 570 to 589
    QIC11   = 570,
    QIC120  = 571,
    QIC1350 = 572,
    QIC150  = 573,
    QIC24   = 574,
    QIC3010 = 575,
    QIC3020 = 576,
    QIC3080 = 577,
    QIC3095 = 578,
    QIC320  = 579,
    QIC40   = 580,
    QIC525  = 581,
    QIC80   = 582,
    // QIC, types 570 to 589

    // StorageTek tapes, types 590 to 609
    STK4480 = 590,
    STK4490 = 591,
    STK9490 = 592,
    T9840A  = 593,
    T9840B  = 594,
    T9840C  = 595,
    T9840D  = 596,
    T9940A  = 597,
    T9940B  = 598,
    T10000A = 599,
    T10000B = 600,
    T10000C = 601,
    T10000D = 602,
    // StorageTek tapes, types 590 to 609

    // Travan, types 610 to 619
    Travan1   = 610,
    Travan1Ex = 611,
    Travan3   = 612,
    Travan3Ex = 613,
    Travan4   = 614,
    Travan5   = 615,
    Travan7   = 616,
    // Travan, types 610 to 619

    // VXA, types 620 to 629
    VXA1 = 620,
    VXA2 = 621,
    VXA3 = 622,
    // VXA, types 620 to 629

    // Magneto-optical, types 630 to 659
    ECMA_153        = 630,  ///< 5,25", M.O., ??? sectors, 1024 bytes/sector, ECMA-153, ISO 11560
    ECMA_153_512    = 631,  ///< 5,25", M.O., ??? sectors, 512 bytes/sector, ECMA-153, ISO 11560
    ECMA_154        = 632,  ///< 3,5", M.O., 249850 sectors, 512 bytes/sector, ECMA-154, ISO 10090
    ECMA_183_512    = 633,  ///< 5,25", M.O., 904995 sectors, 512 bytes/sector, ECMA-183, ISO 13481
    ECMA_183        = 634,  ///< 5,25", M.O., 498526 sectors, 1024 bytes/sector, ECMA-183, ISO 13481
    ECMA_184_512    = 635,  ///< 5,25", M.O., 1128772 or 1163337 sectors, 512 bytes/sector, ECMA-183, ISO 13549
    ECMA_184        = 636,  ///< 5,25", M.O., 603466 or 637041 sectors, 1024 bytes/sector, ECMA-183, ISO 13549
    ECMA_189        = 637,  ///< 300mm, M.O., ??? sectors, 1024 bytes/sector, ECMA-189, ISO 13614
    ECMA_190        = 638,  ///< 300mm, M.O., ??? sectors, 1024 bytes/sector, ECMA-190, ISO 13403
    ECMA_195        = 639,  ///< 5,25", M.O., 936921 or 948770 sectors, 1024 bytes/sector, ECMA-195, ISO 13842
    ECMA_195_512    = 640,  ///< 5,25", M.O., 1644581 or 1647371 sectors, 512 bytes/sector, ECMA-195, ISO 13842
    ECMA_201        = 641,  ///< 3,5", M.O., 446325 sectors, 512 bytes/sector, ECMA-201, ISO 13963
    ECMA_201_ROM    = 642,  ///< 3,5", M.O., 429975 sectors, 512 bytes/sector, embossed, ISO 13963
    ECMA_223        = 643,  ///< 3,5", M.O., 371371 sectors, 1024 bytes/sector, ECMA-223
    ECMA_223_512    = 644,  ///< 3,5", M.O., 694929 sectors, 512 bytes/sector, ECMA-223
    ECMA_238        = 645,  ///< 5,25", M.O., 1244621 sectors, 1024 bytes/sector, ECMA-238, ISO 15486
    ECMA_239        = 646,  ///< 3,5", M.O., 318988, 320332 or 321100 sectors, 2048 bytes/sector, ECMA-239, ISO 15498
    ECMA_260        = 647,  ///< 356mm, M.O., 14476734 sectors, 1024 bytes/sector, ECMA-260, ISO 15898
    ECMA_260_Double = 648,  ///< 356mm, M.O., 24445990 sectors, 1024 bytes/sector, ECMA-260, ISO 15898
    ECMA_280        = 649,  ///< 5,25", M.O., 1128134 sectors, 2048 bytes/sector, ECMA-280, ISO 18093
    ECMA_317        = 650,  ///< 300mm, M.O., 7355716 sectors, 2048 bytes/sector, ECMA-317, ISO 20162
    ECMA_322        = 651,  ///< 5,25", M.O., 1095840 sectors, 4096 bytes/sector, ECMA-322, ISO 22092
    ECMA_322_2k     = 652,  ///< 5,25", M.O., 2043664 sectors, 2048 bytes/sector, ECMA-322, ISO 22092
    GigaMo          = 653,  ///< 3,5", M.O., 605846 sectors, 2048 bytes/sector, Cherry Book, GigaMo, ECMA-351, ISO 17346
    GigaMo2 = 654,  ///< 3,5", M.O., 1063146 sectors, 2048 bytes/sector, Cherry Book 2, GigaMo 2, ECMA-353, ISO 22533
    // Magneto-optical, types 630 to 659

    // Other floppy standards, types 660 to 689
    CompactFloppy = 660,
    DemiDiskette  = 661,
    Floptical     = 662,  ///< 3.5", 652 tracks, 2 sides, 512 bytes/sector, Floptical, ECMA-207, ISO 14169
    HiFD          = 663,
    QuickDisk     = 664,
    UHD144        = 665,
    VideoFloppy   = 666,
    Wafer         = 667,
    ZXMicrodrive  = 668,
    // Other floppy standards, types 660 to 669

    // Miscellaneous, types 670 to 689
    BeeCard     = 670,
    Borsu       = 671,
    DataStore   = 672,
    DIR         = 673,
    DST         = 674,
    DTF         = 675,
    DTF2        = 676,
    Flextra3020 = 677,
    Flextra3225 = 678,
    HiTC1       = 679,
    HiTC2       = 680,
    LT1         = 681,
    MiniCard    = 872,
    Orb         = 683,
    Orb5        = 684,
    SmartMedia  = 685,
    xD          = 686,
    XQD         = 687,
    DataPlay    = 688,
    // Miscellaneous, types 670 to 689

    // Apple specific media, types 690 to 699
    AppleProfile   = 690,
    AppleWidget    = 691,
    AppleHD20      = 692,
    PriamDataTower = 693,
    Pippin         = 694,
    // Apple specific media, types 690 to 699

    // DEC hard disks, types 700 to 729
    RA60    = 700,  ///< 2382 cylinders, 4 tracks/cylinder, 42 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 204890112 bytes
    RA80    = 701,  ///< 546 cylinders, 14 tracks/cylinder, 31 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 121325568 bytes
    RA81    = 702,  ///< 1248 cylinders, 14 tracks/cylinder, 51 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 456228864 bytes
    RC25    = 703,  ///< 302 cylinders, 4 tracks/cylinder, 42 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 25976832 bytes
    RD31    = 704,  ///< 615 cylinders, 4 tracks/cylinder, 17 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 21411840 bytes
    RD32    = 705,  ///< 820 cylinders, 6 tracks/cylinder, 17 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 42823680 bytes
    RD51    = 706,  ///< 306 cylinders, 4 tracks/cylinder, 17 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 10653696 bytes
    RD52    = 707,  ///< 480 cylinders, 7 tracks/cylinder, 18 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 30965760 bytes
    RD53    = 708,  ///< 1024 cylinders, 7 tracks/cylinder, 18 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 75497472 bytes
    RD54    = 709,  ///< 1225 cylinders, 8 tracks/cylinder, 18 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 159936000 bytes
    RK06    = 710,  ///< 411 cylinders, 3 tracks/cylinder, 22 sectors/track, 256 words/sector, 16 bits/word, 512
                    ///< bytes/sector, 13888512 bytes
    RK06_18 = 711,  ///< 411 cylinders, 3 tracks/cylinder, 20 sectors/track, 256 words/sector, 18 bits/word, 576
                    ///< bytes/sector, 14204160 bytes
    RK07    = 712,  ///< 815 cylinders, 3 tracks/cylinder, 22 sectors/track, 256 words/sector, 16 bits/word, 512
                    ///< bytes/sector, 27540480 bytes
    RK07_18 = 713,  ///< 815 cylinders, 3 tracks/cylinder, 20 sectors/track, 256 words/sector, 18 bits/word, 576
                    ///< bytes/sector, 28166400 bytes
    RM02    = 714,  ///< 823 cylinders, 5 tracks/cylinder, 32 sectors/track, 256 words/sector, 16 bits/word, 512
                    ///< bytes/sector, 67420160 bytes
    RM03    = 715,  ///< 823 cylinders, 5 tracks/cylinder, 32 sectors/track, 256 words/sector, 16 bits/word, 512
                    ///< bytes/sector, 67420160 bytes
    RM05    = 716,  ///< 823 cylinders, 19 tracks/cylinder, 32 sectors/track, 256 words/sector, 16 bits/word, 512
                    ///< bytes/sector, 256196608 bytes
    RP02    = 717,  ///< 203 cylinders, 10 tracks/cylinder, 22 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 22865920 bytes
    RP02_18 = 718,  ///< 203 cylinders, 10 tracks/cylinder, 20 sectors/track, 128 words/sector, 36 bits/word, 576
                    ///< bytes/sector, 23385600 bytes
    RP03    = 719,  ///< 400 cylinders, 10 tracks/cylinder, 22 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 45056000 bytes
    RP03_18 = 720,  ///< 400 cylinders, 10 tracks/cylinder, 20 sectors/track, 128 words/sector, 36 bits/word, 576
                    ///< bytes/sector, 46080000 bytes
    RP04    = 721,  ///< 411 cylinders, 19 tracks/cylinder, 22 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 87960576 bytes
    RP04_18 = 722,  ///< 411 cylinders, 19 tracks/cylinder, 20 sectors/track, 128 words/sector, 36 bits/word, 576
                    ///< bytes/sector, 89959680 bytes
    RP05    = 723,  ///< 411 cylinders, 19 tracks/cylinder, 22 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 87960576 bytes
    RP05_18 = 724,  ///< 411 cylinders, 19 tracks/cylinder, 20 sectors/track, 128 words/sector, 36 bits/word, 576
                    ///< bytes/sector, 89959680 bytes
    RP06    = 725,  ///< 815 cylinders, 19 tracks/cylinder, 22 sectors/track, 128 words/sector, 32 bits/word, 512
                    ///< bytes/sector, 174423040 bytes
    RP06_18 = 726,  ///< 815 cylinders, 19 tracks/cylinder, 20 sectors/track, 128 words/sector, 36 bits/word, 576
                    ///< bytes/sector, 178387200 bytes
    // DEC hard disks, types 700 to 729

    // Imation, types 730 to 739
    LS120  = 730,  ///< Imation LS-120 SuperDisk
    LS240  = 731,  ///< Imation LS-240 SuperDisk
    FD32MB = 732,  ///< MF2HD formatted as 32MiB disk in Imation LS-240 drive
    RDX    = 733,  ///< Tandberg / Imation RDX removable disk cartridge
    RDX320 = 734,  ///< Imation 320Gb RDX
    // Imation, types 730 to 739

    // VideoNow, types 740 to 749
    VideoNow      = 740,  ///< Hasbro VideoNow 85 mm proprietary video disc
    VideoNowColor = 741,  ///< Hasbro VideoNow Color disc
    VideoNowXp    = 742,  ///< Hasbro VideoNow XP higher capacity disc
    // VideoNow, types 740 to 749

    // Iomega, types 750 to 759
    Bernoulli10      = 750,  ///< 8"x11" Bernoulli Box disk with 10Mb capacity
    ///< 8"x11" Bernoulli Box disk with 20Mb capacity
    Bernoulli20      = 751,
    ///< 5⅓" Bernoulli Box II disk with 20Mb capacity
    BernoulliBox2_20 = 752,
    // Iomega, types 750 to 759

    // Kodak, types 760 to 769
    KodakVerbatim3  = 760,  ///< Kodak/Verbatim (3Mb)
    KodakVerbatim6  = 761,  ///< Kodak/Verbatim (6Mb)
    KodakVerbatim12 = 762,  ///< Kodak/Verbatim (12Mb)
    // Kodak, types 760 to 769

    // Sony and Panasonic Blu-ray derived, types 770 to 799
    ProfessionalDisc       = 770,  ///< Professional Disc for video, single layer, rewritable, 23Gb
    ProfessionalDiscDual   = 771,  ///< Professional Disc for video, dual layer, rewritable, 50Gb
    ProfessionalDiscTriple = 772,  ///< Professional Disc for video, triple layer, rewritable, 100Gb
    ProfessionalDiscQuad   = 773,  ///< Professional Disc for video, quad layer, write once, 128Gb
    PDD                    = 774,  ///< Professional Disc for DATA, single layer, rewritable, 23Gb
    PDD_WORM               = 775,  ///< Professional Disc for DATA, single layer, write once, 23Gb
    ArchivalDisc           = 776,  ///< Archival Disc, 1st gen., 300Gb
    ArchivalDisc2          = 777,  ///< Archival Disc, 2nd gen., 500Gb
    ArchivalDisc3          = 778,  ///< Archival Disc, 3rd gen., 1Tb
    ODC300R                = 779,  ///< Optical Disc archive, 1st gen., write once, 300Gb
    ODC300RE               = 780,  ///< Optical Disc archive, 1st gen., rewritable, 300Gb
    ODC600R                = 781,  ///< Optical Disc archive, 2nd gen., write once, 600Gb
    ODC600RE               = 782,  ///< Optical Disc archive, 2nd gen., rewritable, 600Gb
    ODC1200RE              = 783,  ///< Optical Disc archive, 3rd gen., rewritable, 1200Gb
    ODC1500R               = 784,  ///< Optical Disc archive, 3rd gen., write once, 1500Gb
    ODC3300R               = 785,  ///< Optical Disc archive, 4th gen., write once, 3300Gb
    ODC5500R               = 786,  ///< Optical Disc archive, 5th gen., write once, 5500Gb
    // Sony and Panasonic Blu-ray derived, types 770 to 799

    // Magneto-optical, types 800 to 819
    ECMA_322_1k   = 800,  ///< 5,25", M.O., 4383356 sectors, 1024 bytes/sector, ECMA-322, ISO 22092, 9.1Gb/cart
    ECMA_322_512  = 801,  ///< 5,25", M.O., ??????? sectors, 512 bytes/sector, ECMA-322, ISO 22092, 9.1Gb/cart
    ISO_14517     = 802,  ///< 5,25", M.O., 1273011 sectors, 1024 bytes/sector, ISO 14517, 2.6Gb/cart
    ISO_14517_512 = 803,  ///< 5,25", M.O., 2244958 sectors, 512 bytes/sector, ISO 14517, 2.3Gb/cart
    ISO_15041_512 = 804,  ///< 3,5", M.O., 1041500 sectors, 512 bytes/sector, ISO 15041, 540Mb/cart
    HSM650        = 805,  ///< 3,5", M.O., ??????? sectors, proprietary, 650Mb/cart, Sony HyperStorage
    // Magneto-optical, types 800 to 819

    // More floppy formats, types 820 to deprecated
    MetaFloppy_Mod_I = 820,  ///< 5.25", SS, DD, 35 tracks, 16 spt, 256 bytes/sector, MFM, 48 tpi, ???rpm
    HF12 = 823,  ///< HyperFlex (12Mb), 5.25", DS, 301 tracks, 78 spt, 256 bytes/sector, MFM, 333 tpi, 600rpm
    HF24 = 824,  ///< HyperFlex (24Mb), 5.25", DS, 506 tracks, 78 spt, 256 bytes/sector, MFM, 666 tpi, 720rpm
    // More floppy formats, types 820 to deprecated

    AtariLynxCard        = 821,  ///< Atari Lynx card
    AtariJaguarCartridge = 822   ///< Atari Jaguar cartridge
} MediaType;

/** @} */ /* end of MediaTypes group */

// NOLINTEND(readability-identifier-naming)

/** \struct ImageInfo
 *  \ingroup MediaTypes
 *  \brief High-level summary of an opened Aaru image containing metadata and media characteristics.
 *
 *  This structure aggregates essential information extracted from an Aaru format image file,
 *  providing callers with a comprehensive view of the imaged media without requiring access
 *  to internal image structures. All fields are read-only from the caller's perspective and
 *  reflect the state at the time the image was created or last modified.
 *
 *  \par Field Semantics:
 *
 *  \b HasPartitions (uint8_t):
 *    - Non-zero (typically 1) if the image contains partition table metadata (MBR, GPT, APM, etc.)
 *      or, for optical media, track information structures.
 *    - Zero if no partition/track structures were detected or if the media is unpartitioned.
 *    - Usage: Check this before attempting to enumerate partitions/tracks via dedicated APIs.
 *
 *  \b HasSessions (uint8_t):
 *    - Non-zero (typically 1) if multiple recording sessions are present (primarily optical media).
 *    - Zero for single-session media or media types that don't support sessions (e.g., floppy, HDD).
 *    - Usage: Multi-session handling may require session-specific TOC/track enumeration.
 *
 *  \b ImageSize (uint64_t):
 *    - Total size in bytes of image payload data, excluding format headers, metadata, and container overhead.
 *    - May not reflect current file size due to compression, sparse allocation, or incremental updates.
 *    - Usage: For informational/statistical purposes; not reliable for disk space calculations.
 *
 *  \b Sectors (uint64_t):
 *    - Total count of addressable logical blocks (sectors) in the image.
 *    - Range: [1, 2^64-1] for valid images; 0 indicates corruption or initialization failure.
 *    - Usage: Multiply by SectorSize to determine total addressable capacity.
 *
 *  \b SectorSize (uint32_t):
 *    - Size of each logical sector in bytes. Common values: 512, 2048, 2352, 4096.
 *    - Guaranteed to be non-zero for valid images; may vary by media type (CD: 2352, HDD: 512/4096).
 *    - Usage: Required for LBA-to-byte offset calculations and buffer allocation.
 *
 *  \b Version[32] (char array):
 *    - NUL-terminated string identifying the Aaru image format version (e.g., "6.0", "5.3").
 *    - Not necessarily the application version; reflects on-disk format compatibility level.
 *    - Empty string if version information is unavailable or unrecognized.
 *
 *  \b Application[64] (char array):
 *    - NUL-terminated string naming the application that created the image (e.g., "Aaru", "DiscImageChef").
 *    - May contain vendor/project identifiers; not guaranteed to match executable name.
 *    - Empty string if creator information was not stored or is unavailable.
 *
 *  \b ApplicationVersion[32] (char array):
 *    - NUL-terminated string specifying the version of the creating application (e.g., "6.0.0-alpha1").
 *    - Semantic versioning format recommended but not enforced.
 *    - Empty string if version metadata is absent.
 *
 *  \b CreationTime (int64_t):
 *    - Image creation timestamp as Windows FILETIME: 100-nanosecond intervals since January 1, 1601 00:00:00 UTC.
 *    - Zero (0) may represent epoch or absence of creation time; check for < 0 for explicit invalidity.
 *    - Usage: Convert to UNIX timestamp via: (CreationTime / 10000000) - 11644473600.
 *
 *  \b LastModificationTime (int64_t):
 *    - Last modification timestamp in Windows FILETIME format (same encoding as CreationTime).
 *    - Updated when image data or metadata is altered; not filesystem modification time.
 *    - May equal CreationTime for unmodified images.
 *    - Negative values indicate missing/invalid metadata.
 *
 *  \b MediaType (uint32_t):
 *    - Numeric identifier from the \ref MediaType enumeration representing the physical/logical media.
 *    - Value 0 (\ref Unknown) when automatic detection failed or media is unrecognized/exotic.
 *    - Stable across versions; safe to persist and compare.
 *    - Usage: Cast to \c MediaType enum for switch/case logic; always include default/Unknown handling.
 *
 *  \b MetadataMediaType (uint8_t):
 *    - Internal identifier used for sidecar/metadata generation (METS/CICM/ALTO compatibility).
 *    - Not directly useful for most callers; primarily for serialization/archival workflows.
 *
 *  \par Invariants and Constraints:
 *  - All pointer-like char arrays are guaranteed NUL-terminated and safe for string functions.
 *  - Sectors > 0 and SectorSize > 0 for structurally valid images.
 *  - Timestamps may be 0 or negative; consumers must validate before using.
 *  - MediaType range corresponds to \ref MediaType enum; out-of-range values are possible for future extensions.
 *
 *  \par Thread Safety:
 *  - Struct contents are stable after retrieval; safe for concurrent reads.
 *  - Do not cache ImageInfo across context operations that may invalidate it (e.g., re-opening).
 *
 *  \par ABI Stability:
 *  - Field layout is ABI-stable; new fields append to end in future versions.
 *  - Reordering or removing fields constitutes a major version break.
 *  - Linter suppressions acknowledge intentionally large field count for completeness.
 */
typedef struct ImageInfo  // NOLINT
{
    uint8_t  HasPartitions;    ///< Image contains partitions (or tracks for optical media); 0=no, non-zero=yes
    uint8_t  HasSessions;      ///< Image contains multiple sessions (optical media); 0=single/none, non-zero=multi
    uint64_t ImageSize;        ///< Size of the image payload in bytes (excludes headers/metadata)
    uint64_t Sectors;          ///< Total count of addressable logical sectors/blocks
    uint32_t SectorSize;       ///< Size of each logical sector in bytes (512, 2048, 2352, 4096, etc.)
    char     Version[32];      ///< Image format version string (NUL-terminated, e.g., "6.0")
    char     Application[64];  ///< Name of application that created the image (NUL-terminated)
    char     ApplicationVersion[32];  ///< Version of the creating application (NUL-terminated)
    int64_t  CreationTime;            ///< Image creation timestamp (Windows FILETIME: 100ns since 1601-01-01 UTC)
    int64_t  LastModificationTime;    ///< Last modification timestamp (Windows FILETIME format)
    uint32_t MediaType;               ///< Media type identifier (see \ref MediaType enum; 0=Unknown)
    uint8_t  MetadataMediaType;       ///< Media type for sidecar generation (internal archival use)
} ImageInfo;

/** \addtogroup SectorTags Per-sector metadata tag types
 *  \brief Optional auxiliary fragments accompanying a raw sector dump.
 *
 *  Sector tags preserve on-disk / on-media structures that are not part of the main user data (sync/header/ECC, etc.).
 *  They enable exact reconstruction, verification or advanced analysis (error injection, subchannel decoding, etc.).
 *  Retrieval APIs generally expose presence queries and raw byte buffers of fixed size (unless documented as string).
 *  @{
 */
// NOLINTBEGIN(readability-identifier-naming)
typedef enum
{
    kSectorTagAppleSony            = 0,   ///< Apple's Sony sector tags, 12 bytes (address prolog + checksum)
    kSectorTagCdSync               = 1,   ///< 12-byte CD sync pattern (00 FF*10 00)
    kSectorTagCdHeader             = 2,   ///< 4-byte CD header (minute, second, frame, mode)
    kSectorTagCdSubHeader          = 3,   ///< Mode 2 Form subheader (8 bytes: copy, submode, channel)
    kSectorTagCdEdc                = 4,   ///< 32-bit CRC (EDC)
    kSectorTagCdEccP               = 5,   ///< 172 bytes Reed-Solomon ECC (P)
    kSectorTagCdEccQ               = 6,   ///< 104 bytes Reed-Solomon ECC (Q)
    kSectorTagCdEcc                = 7,   ///< Combined P+Q ECC (276 bytes)
    kSectorTagCdSubchannel         = 8,   ///< 96 raw subchannel bytes (P-W)
    kSectorTagCdTrackIsrc          = 9,   ///< Track ISRC (12 ASCII chars, no terminator)
    kSectorTagCdTrackText          = 10,  ///< Track text (CD-Text fragment, 13 bytes)
    kSectorTagCdTrackFlags         = 11,  ///< Track flags (audio/data, copy permitted, pre-emphasis)
    kSectorTagDvdCmi               = 12,  ///< DVD Copyright Management Information (CSS)
    kSectorTagFloppyAddressMark    = 13,  ///< Raw address mark & sync preamble (format dependent)
    kSectorTagDvdTitleKey          = 14,  ///< DVD sector title key, 5 bytes
    kSectorTagDvdTitleKeyDecrypted = 15,  ///< Decrypted DVD sector title key, 5 bytes
    kSectorTagDvdSectorInformation = 16,  ///< DVD sector information, 1 bytes
    kSectorTagDvdSectorNumber      = 17,  ///< DVD sector number, 3 bytes
    kSectorTagDvdSectorIed         = 18,  ///< DVD sector ID error detection, 2 bytes
    kSectorTagDvdSectorEdc         = 19,  ///< DVD sector EDC, 4 bytes
    kSectorTagAppleProfile         = 20,  ///< Apple's Profile sector tags, 20 bytes
    kSectorTagPriamDataTower       = 21,  ///< Priam DataTower sector tags, 24 bytes
    kSectorTagBdSectorEdc          = 22,  ///< Blu-ray sector EDC, 4 bytes
    MaxSectorTag                   = kSectorTagBdSectorEdc
} SectorTagType;

/** @} */ /* end of SectorTags group */

/*
 *     Metadata present for each media.
 */
// NOLINTBEGIN(readability-identifier-naming)
/** \addtogroup MediaTags Per-media metadata tag types
 *  \brief High-level descriptors captured for the whole medium (lead-in/out, inquiry data, identification registers).
 *
 *  Media tags are coarse-grained data blocks not tied to individual sectors. Absence indicates either unreadability
 *  (hardware limitation / error) or irrelevance for the specific media. Consumers should treat absence as unknown, not
 *  failure. Enum values are stable and serialized; append only.
 *  @{ */
typedef enum
{
    /* CD table of contents */
    kMediaTagCdToc                        = 0,  ///< Standard CD Table Of Contents (lead-in, first session)
    kMediaTagSessionInfo                  = 1,  ///< Per-session summary (start/end addresses, track count)
    kMediaTagFullToc                      = 2,  ///< Complete multi-session TOC including hidden tracks
    kMediaTagCdPma                        = 3,  ///< Program Memory Area (temporary track info before finalization)
    kMediaTagCdAtip                       = 4,  ///< Absolute Time In Pregroove (writable media timing & power metadata)
    kMediaTagCdText                       = 5,  ///< CD-Text blocks (titles, performers, etc.)
    kMediaTagCdMcn                        = 6,  ///< Media Catalogue Number (EAN/UPC style identifier)
    kMediaTagDvdPfi                       = 7,  ///< Physical Format Information (layer geometry & book type)
    kMediaTagDvdCmi                       = 8,  ///< Copyright Management Information (CSS/CPRM flags)
    kMediaTagDvdDiscKey                   = 9,  ///< Encrypted disc key block (CSS)
    kMediaTagDvdBca                       = 10,  ///< Burst Cutting Area (etched manufacturer / AACS info)
    kMediaTagDvdDmi                       = 11,  ///< Disc Manufacturer Information (lead-in descriptor)
    kMediaTagDvdMediaIdentifier           = 12,  ///< Writable media dye / manufacturer ID
    kMediaTagDvdMkb                       = 13,  ///< Media Key Block (AACS/DVD)
    kMediaTagDvdRamDds                    = 14,  ///< Defect Data Structure (DVD-RAM mapping)
    kMediaTagDvdRamMediumStatus           = 15,  ///< Medium Status (allocated spare info)
    kMediaTagDvdRamSpareArea              = 16,  ///< Spare area descriptors
    kMediaTagDvdrRmd                      = 17,  ///< Recorded Media Data (RMD) last border-out
    kMediaTagDvdrPreRecordedInfo          = 18,  ///< Pre-recorded info area (lead-in)
    kMediaTagDvdrMediaIdentifier          = 19,  ///< DVD-R/-RW writable media identifier
    kMediaTagDvdrPfi                      = 20,  ///< DVD-R physical format (layer data)
    kMediaTagDvdAdip                      = 21,  ///< Address In Pregroove (DVD+ / wobble timing)
    kMediaTagHddvdCpi                     = 22,  ///< Content Protection Info (HD DVD)
    kMediaTagHddvdMediumStatus            = 23,  ///< HD DVD Medium status (spares/defects)
    kMediaTagDvddlLayerCapacity           = 24,  ///< Dual layer capacity & break info
    kMediaTagDvddlMiddleZoneAddress       = 25,  ///< Middle zone start LBA
    kMediaTagDvddlJumpIntervalSize        = 26,  ///< Jump interval size (opposite track path)
    kMediaTagDvddlManualLayerJumpLba      = 27,  ///< Manual layer jump LBA (OTP)
    kMediaTagBlurayDi                     = 28,  ///< Disc Information (BD)
    kMediaTagBlurayBca                    = 29,  ///< Blu-ray Burst Cutting Area
    kMediaTagBlurayDds                    = 30,  ///< Disc Definition Structure (recordable)
    kMediaTagBlurayCartridgeStatus        = 31,  ///< Cartridge presence / write protect (BD-RE/BD-R in caddy)
    kMediaTagBluraySpareArea              = 32,  ///< BD spare area allocation map
    kMediaTagAacsVolumeIdentifier         = 33,  ///< AACS Volume Identifier
    kMediaTagAacsSerialNumber             = 34,  ///< Pre-recorded media serial number (AACS)
    kMediaTagAacsMediaIdentifier          = 35,  ///< AACS Media Identifier (unique per disc)
    kMediaTagAacsMkb                      = 36,  ///< AACS Media Key Block
    kMediaTagAacsDataKeys                 = 37,  ///< Extracted AACS title/volume keys (when decrypted)
    kMediaTagAacsLbaExtents               = 38,  ///< LBA extents requiring bus encryption
    kMediaTagCprmMkb                      = 39,  ///< CPRM Media Key Block
    kMediaTagHybridRecognizedLayers       = 40,  ///< Hybrid disc recognized layer combinations (e.g. CD/DVD/BD)
    kMediaTagMmcWriteProtection           = 41,  ///< Write protection status (MMC GET CONFIG)
    kMediaTagMmcDiscInformation           = 42,  ///< Disc Information (recordable status, erasable, last session)
    kMediaTagMmcTrackResourcesInformation = 43,  ///< Track Resources (allocated/open track data)
    kMediaTagMmcPowResourcesInformation   = 44,  ///< Pseudo OverWrite resources (BD-R POW)
    kMediaTagScsiInquiry                  = 45,  ///< SCSI INQUIRY standard data (SPC-*)
    kMediaTagScsiModePage2A               = 46,  ///< SCSI Mode Page 2Ah (CD/DVD capabilities)
    kMediaTagAtaIdentify                  = 47,  ///< ATA IDENTIFY DEVICE (512 bytes)
    kMediaTagAtapiIdentify                = 48,  ///< ATA PACKET IDENTIFY DEVICE
    kMediaTagPcmciaCis                    = 49,  ///< PCMCIA/CardBus CIS tuple chain
    kMediaTagSdCid                        = 50,  ///< SecureDigital Card ID register
    kMediaTagSdCsd                        = 51,  ///< SecureDigital Card Specific Data
    kMediaTagSdScr                        = 52,  ///< SecureDigital Configuration Register
    kMediaTagSdOcr                        = 53,  ///< SecureDigital Operation Conditions (voltage)
    kMediaTagMmcCid                       = 54,  ///< MMC Card ID
    kMediaTagMmcCsd                       = 55,  ///< MMC Card Specific Data
    kMediaTagMmcOcr                       = 56,  ///< MMC Operation Conditions
    kMediaTagExtendedCsd                  = 57,  ///< MMC Extended CSD (512 bytes)
    kMediaTagXboxSecuritySector           = 58,  ///< Xbox/Xbox 360 Security Sector (SS.bin)
    kMediaTagFloppyLeadOut                = 59,  ///< Manufacturer / duplication cylinder (floppy special data)
    kMediaTagDiscControlBlock             = 60,  ///< DVD Disc Control Blocks
    kMediaTagCdFirstTrackPregap           = 61,  ///< First track pregap (index 0)
    kMediaTagCdLeadOut                    = 62,  ///< Lead-out area contents
    kMediaTagScsiModeSense6               = 63,  ///< Raw MODE SENSE (6) data
    kMediaTagScsiModeSense10              = 64,  ///< Raw MODE SENSE (10) data
    kMediaTagUsbDescriptors               = 65,  ///< Concatenated USB descriptors (device/config/interface)
    kMediaTagXboxDmi                      = 66,  ///< Xbox Disc Manufacturing Info (DMI)
    kMediaTagXboxPfi                      = 67,  ///< Xbox Physical Format Information (PFI)
    kMediaTagCdLeadIn                     = 68,  ///< Raw lead-in (TOC frames)
    kMediaTagMiniDiscType                 = 69,  ///< 8 bytes response that seems to define type of MiniDisc
    kMediaTagMiniDiscD5                   = 70,  ///< 4 bytes response to vendor command D5h
    kMediaTagMiniDiscUtoc =
        71,  ///< User TOC, contains fragments, track names, and can be from 1 to 3 sectors of 2336 bytes
    kMediaTagMiniDiscDtoc        = 72,  ///< Not entirely clear kind of TOC that only appears on MD-DATA discs
    kMediaTagDvdDiscKeyDecrypted = 73,  ///< Decrypted DVD disc key,
    kMediaTagDvdPfi2ndLayer      = 74,  ///< DVD Physical Format Information for the second layer
    kMediaTagFloppyWriteProtect  = 75,  ///< Write protection status of the floppy disk
    kMediaTagWiiUDiscKey         = 76,  ///< Nintendo Wii U disc key (16 bytes, from non-readable disc area)
    kMediaTagPs3DiscKey          = 77,  ///< PS3 derived disc key (16 bytes)
    kMediaTagPs3Data1            = 78,  ///< PS3 data1 key (16 bytes, from disc)
    kMediaTagPs3Data2            = 79,  ///< PS3 data2 key (16 bytes, from disc)
    kMediaTagPs3Pic              = 80,  ///< PS3 PIC data (115 bytes, from disc lead-in)
    kMediaTagPs3EncryptionMap    = 81,  ///< PS3 encryption region map (serialized from sector 0)
    kMediaTagWiiUPartitionKeyMap = 82,  ///< Nintendo Wii U partition-to-key mapping with regions
    kMediaTagWiiPartitionKeyMap  = 83,  ///< Nintendo Wii partition-to-key mapping with regions
    kMediaTagNgcwJunkMap         = 84,  ///< Nintendo GameCube/Wii junk region map with LFG seeds
    kMediaTagAacsMediaKey        = 85,  ///< AACS Media Key
    kMediaTagAacsVolumeUniqueKey = 86,  ///< AACS Volume Unique Key
    MaxMediaTag                  = kMediaTagAacsVolumeUniqueKey
} MediaTagType;

/** @} */ /* end of MediaTags group */

// NOLINTEND(readability-identifier-naming)

#ifdef __clang_
#pragma clang diagnostic pop
#endif

#endif  // LIBAARUFORMAT_AARU_H
