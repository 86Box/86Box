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

#ifndef LIBAARUFORMAT_ENUMS_H
#define LIBAARUFORMAT_ENUMS_H

#ifdef __clang__
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#endif

/**
 * \enum CompressionType
 * \brief List of known compression types.
 */
typedef enum
{
    kCompressionNone    = 0,  ///< Not compressed.
    kCompressionLzma    = 1,  ///< LZMA compression.
    kCompressionFlac    = 2,  ///< FLAC compression.
    kCompressionLzmaCst = 3,  ///< LZMA applied to Claunia Subchannel Transform processed data.
    kCompressionZstd    = 4,  ///< Zstandard compression.
    kCompressionZstdCst = 5   ///< Zstandard applied to Claunia Subchannel Transform processed data.
} CompressionType;

/**
 * \enum DataType
 * \brief List of known data types stored within an Aaru image.
 */
typedef enum
{
    kDataTypeNone                         = 0,   ///< No data.
    kDataTypeUserData                     = 1,   ///< User (main) data.
    kDataTypeCdToc                        = 2,   ///< Compact Disc partial Table of Contents.
    kDataTypeSessionInfo                  = 3,   ///< Compact Disc session information.
    kDataTypeFullToc                      = 4,   ///< Compact Disc full Table of Contents.
    kDataTypeCdPma                        = 5,   ///< Compact Disc Power Management Area (PMA).
    kDataTypeCdAtip                       = 6,   ///< Compact Disc Absolute Time In Pregroove (ATIP).
    kDataTypeCdText                       = 7,   ///< Compact Disc lead-in CD-Text.
    kDataTypeDvdPfi                       = 8,   ///< DVD Physical Format Information.
    kDataTypeDvdCmi                       = 9,   ///< DVD lead-in Copyright Management Information (CMI).
    kDataTypeDvdDiscKey                   = 10,  ///< DVD disc key.
    kDataTypeDvdBca                       = 11,  ///< DVD Burst Cutting Area (BCA).
    kDataTypeDvdDmi                       = 12,  ///< DVD Disc Manufacturing Information (DMI).
    kDataTypeDvdMediaIdentifier           = 13,  ///< DVD media identifier.
    kDataTypeDvdMkb                       = 14,  ///< DVD Media Key Block (MKB).
    kDataTypeDvdRamDds                    = 15,  ///< DVD-RAM Disc Definition Structure (DDS).
    kDataTypeDvdRamMediumStatus           = 16,  ///< DVD-RAM medium status.
    kDataTypeDvdRamSpareArea              = 17,  ///< DVD-RAM spare area information.
    kDataTypeDvdrRmd                      = 18,  ///< DVD-R RMD (Recording Management Data).
    kDataTypeDvdrPreRecordedInfo          = 19,  ///< DVD-R pre‑recorded information.
    kDataTypeDvdrMediaIdentifier          = 20,  ///< DVD-R media identifier.
    kDataTypeDvdrPfi                      = 21,  ///< DVD-R Physical Format Information.
    kDataTypeDvdAdip                      = 22,  ///< DVD Address In Pregroove (ADIP).
    kDataTypeHddvdCpi                     = 23,  ///< HD DVD Copy Protection Information (CPI).
    kDataTypeHddvdMediumStatus            = 24,  ///< HD DVD medium status.
    kDataTypeDvddlLayerCapacity           = 25,  ///< DVD dual-layer capacity.
    kDataTypeDvddlMiddleZoneAddress       = 26,  ///< DVD dual-layer middle zone address.
    kDataTypeDvddlJumpIntervalSize        = 27,  ///< DVD dual-layer jump interval size.
    kDataTypeDvddlManualLayerJumpLba      = 28,  ///< DVD dual-layer manual layer jump LBA.
    kDataTypeBlurayDi                     = 29,  ///< Blu-ray Disc Information (DI).
    kDataTypeBlurayBca                    = 30,  ///< Blu-ray Burst Cutting Area (BCA).
    kDataTypeBlurayDds                    = 31,  ///< Blu-ray Disc Definition Structure (DDS).
    kDataTypeBlurayCartridgeStatus        = 32,  ///< Blu-ray cartridge status.
    kDataTypeBluraySpareArea              = 33,  ///< Blu-ray spare area information.
    kDataTypeAacsVolumeIdentifier         = 34,  ///< AACS volume identifier.
    kDataTypeAacsSerialNumber             = 35,  ///< AACS serial number.
    kDataTypeAacsMediaIdentifier          = 36,  ///< AACS media identifier.
    kDataTypeAacsMkb                      = 37,  ///< AACS Media Key Block (MKB).
    kDataTypeAacsDataKeys                 = 38,  ///< AACS data keys.
    kDataTypeAacsLbaExtents               = 39,  ///< AACS LBA extents.
    kDataTypeCprmMkb                      = 40,  ///< CPRM Media Key Block (MKB).
    kDataTypeHybridRecognizedLayers       = 41,  ///< Recognized layers (hybrid media).
    kDataTypeMmcWriteProtection           = 42,  ///< MMC write-protection data.
    kDataTypeMmcDiscInformation           = 43,  ///< MMC disc information.
    kDataTypeMmcTrackResourcesInformation = 44,  ///< MMC track resources information.
    kDataTypeMmcPowResourcesInformation   = 45,  ///< MMC POW (Persistent Optical Write?) resources information.
    kDataTypeScsiInquiry                  = 46,  ///< SCSI INQUIRY response.
    kDataTypeScsiModePage2A               = 47,  ///< SCSI MODE PAGE 2Ah.
    kDataTypeAtaIdentify                  = 48,  ///< ATA IDENTIFY DEVICE data.
    kDataTypeAtapiIdentify                = 49,  ///< ATAPI IDENTIFY PACKET DEVICE data.
    kDataTypePcmciaCis                    = 50,  ///< PCMCIA Card Information Structure (CIS).
    kDataTypeSdCid                        = 51,  ///< Secure Digital CID register.
    kDataTypeSdCsd                        = 52,  ///< Secure Digital CSD register.
    kDataTypeSdScr                        = 53,  ///< Secure Digital SCR register.
    kDataTypeSdOcr                        = 54,  ///< Secure Digital OCR register.
    kDataTypeMmcCid                       = 55,  ///< MultiMediaCard CID register.
    kDataTypeMmcCsd                       = 56,  ///< MultiMediaCard CSD register.
    kDataTypeMmcOcr                       = 57,  ///< MultiMediaCard OCR register.
    kDataTypeExtendedCsd                  = 58,  ///< MultiMediaCard Extended CSD register.
    kDataTypeXboxSecuritySector           = 59,  ///< Xbox Security Sector.
    kDataTypeFloppyLeadOut                = 60,  ///< Floppy lead‑out data.
    kDataTypeDiscControlBlock             = 61,  ///< DVD Disc Control Block.
    kDataTypeCdFirstTrackPregap           = 62,  ///< Compact Disc first track pre-gap.
    kDataTypeCdLeadOut                    = 63,  ///< Compact Disc lead‑out.
    kDataTypeScsiModeSense6               = 64,  ///< SCSI MODE SENSE (6) response.
    kDataTypeScsiModeSense10              = 65,  ///< SCSI MODE SENSE (10) response.
    kDataTypeUsbDescriptors               = 66,  ///< USB descriptors set.
    kDataTypeXboxDmi                      = 67,  ///< Xbox DMI.
    kDataTypeXboxPfi                      = 68,  ///< Xbox Physical Format Information (PFI).
    kDataTypeCdSectorPrefix               = 69,  ///< Compact Disc sector prefix (sync, header).
    kDataTypeCdSectorSuffix               = 70,  ///< Compact Disc sector suffix (EDC, ECC P, ECC Q).
    kDataTypeCdSubchannel                 = 71,  ///< Compact Disc subchannel data.
    kDataTypeAppleProfileTag              = 72,  ///< Apple Profile (20‑byte) tag.
    kDataTypeAppleSonyTag                 = 73,  ///< Apple Sony (12‑byte) tag.
    kDataTypePriamDataTowerTag            = 74,  ///< Priam Data Tower (24‑byte) tag.
    kDataTypeCdMcn                        = 75,  ///< Compact Disc Media Catalogue Number (lead‑in, 13 ASCII bytes).
    kDataTypeCdSectorPrefixCorrected      = 76,  ///< Compact Disc sector prefix (sync, header) corrected-only stored.
    kDataTypeCdSectorSuffixCorrected = 77,  ///< Compact Disc sector suffix (EDC, ECC P, ECC Q) corrected-only stored.
    kDataTypeCdSubHeader             = 78,  ///< Compact Disc MODE 2 subheader.
    kDataTypeCdLeadIn                = 79,  ///< Compact Disc lead‑in.
    kDataTypeDvdDiscKeyDecrypted     = 80,  ///< Decrypted DVD Disc Key
    kDataTypeDvdSectorCprMai         = 81,  ///< DVD Copyright Management Information (CPR_MAI)
    kDataTypeDvdTitleKeyDecrypted    = 82,  ///< Decrypted DVD Title Key
    kDataTypeDvdSectorId             = 83,  ///< DVD Identification Data (ID)
    kDataTypeDvdSectorIed            = 84,  ///< DVD ID Error Detection Code (IED)
    kDataTypeDvdSectorEdc            = 85,  ///< DVD Error Detection Code (EDC)
    kDataTypeDvdSectorEccPi          = 86,  ///< DVD Error Correction Code (ECC) Parity of Inner Code (PI)
    kDataTypeDvdEccBlockPo           = 87,  ///< DVD Error Correction Code (ECC) Parity of Outer Code (PO)
    kDataTypeDvdPfi2ndLayer          = 88,  ///< DVD Physical Format Information for the second layer
    kDataTypeFluxData                = 89,  ///< Flux data.
    kDataTypeBitstreamData           = 90,  ///< Bitstream data.
    kDataTypeFloppyWriteProtect      = 91,  ///< Floppy write-protect status.
    kDataTypeWiiUDiscKey             = 92,  ///< Nintendo Wii U disc key (16 bytes, from non-readable disc area)
    kDataTypePs3DiscKey              = 93,  ///< PS3 derived disc key (16 bytes)
    kDataTypePs3Data1                = 94,  ///< PS3 data1 key (16 bytes, from disc)
    kDataTypePs3Data2                = 95,  ///< PS3 data2 key (16 bytes, from disc)
    kDataTypePs3Pic                  = 96,  ///< PS3 PIC data (115 bytes, from disc lead-in)
    kDataTypePs3EncryptionMap        = 97,  ///< PS3 encryption region map (serialized from sector 0)
    kDataTypeWiiUPartitionKeyMap     = 98,  ///< Nintendo Wii U partition-to-key mapping with regions
    kDataTypeWiiPartitionKeyMap      = 99,  ///< Nintendo Wii partition-to-key mapping with regions
    kDataTypeNgcwJunkMap             = 100, ///< Nintendo GameCube/Wii junk region map with LFG seeds
    kDataTypeAacsMediaKey            = 101, ///< AACS Media Key
    kDataTypeAacsVolumeUniqueKey     = 102, ///< AACS Volume Unique Key
    kDataTypeBdSectorEdc             = 103, ///< Blu-ray Sector EDC
    kDataTypeErasureParity           = 104, ///< Erasure coding parity shard for data blocks.
    kDataTypeErasureParityDdt        = 105, ///< Erasure coding parity shard for DDT secondary blocks.
    kDataTypeErasureParityDdtPrimary = 106, ///< Erasure coding parity replica for DDT primary block.
    kDataTypeErasureParityMeta       = 107, ///< Erasure coding parity shard for metadata blocks.
    kDataTypeErasureParityIndex      = 108, ///< Erasure coding parity replica for index block.
} DataType;

/**
 * \enum BlockType
 * \brief List of known block types contained in an Aaru image.
 */
typedef enum
{
    DataBlock                    = 0x4B4C4244,  ///< Block containing data.
    DeDuplicationTable           = 0x2A544444,  ///< Block containing a deduplication table (v1).
    DeDuplicationTable2          = 0x32544444,  ///< Block containing a deduplication table v2.
    DeDuplicationTableSAlpha     = 0x53545444,  ///< Block containing a secondary deduplication table (v2) (mistake).
    DeDuplicationTableSecondary  = 0x53544444,  ///< Block containing a secondary deduplication table (v2).
    IndexBlock                   = 0x58444E49,  ///< Block containing the index (v1).
    IndexBlock2                  = 0x32584449,  ///< Block containing the index v2.
    IndexBlock3                  = 0x33584449,  ///< Block containing the index v3.
    GeometryBlock                = 0x4D4F4547,  ///< Block containing logical geometry.
    MetadataBlock                = 0x4154454D,  ///< Block containing metadata.
    TracksBlock                  = 0x534B5254,  ///< Block containing optical disc tracks.
    CicmBlock                    = 0x4D434943,  ///< Block containing CICM XML metadata.
    ChecksumBlock                = 0x4D534B43,  ///< Block containing contents checksums.
    DataPositionMeasurementBlock = 0x2A4D5044,  ///< Block containing data position measurements (reserved / TODO).
    SnapshotBlock                = 0x50414E53,  ///< Block containing a snapshot index (reserved / TODO).
    ParentBlock                  = 0x50524E54,  ///< Block describing how to locate the parent image (reserved / TODO).
    DumpHardwareBlock            = 0x2A504D44,  ///< Block containing an array of hardware used to create the image.
    TapeFileBlock                = 0x454C4654,  ///< Block containing list of files for a tape image.
    TapePartitionBlock           = 0x54425054,  ///< Block containing list of partitions for a tape image.
    AaruMetadataJsonBlock        = 0x444D534A,  ///< Block containing JSON version of Aaru Metadata
    FluxDataBlock                = 0x58554C46,  ///< Block containing flux data metadata.
    DataStreamPayloadBlock =
        0x4C505344,  ///< Block containing compressed data stream payload (e.g., flux data, bitstreams).
    ErasureCodingMapBlock = 0x424D4345  ///< Block containing erasure coding stripe map and recovery metadata.
} BlockType;

/**
 * \enum ChecksumAlgorithm
 * \brief Supported checksum / hash algorithms.
 */
typedef enum
{
    Invalid = 0,  ///< Invalid / unspecified algorithm.
    Md5     = 1,  ///< MD5 hash.
    Sha1    = 2,  ///< SHA-1 hash.
    Sha256  = 3,  ///< SHA-256 hash.
    SpamSum = 4,  ///< SpamSum (context-triggered piecewise hash).
    Blake3  = 5,  ///< BLAKE3 hash.
} ChecksumAlgorithm;

/**
 * \enum CdFixFlags
 * \brief Flags describing Compact Disc sector fix-up status.
 */
typedef enum
{
    NotDumped       = 0x10000000,  ///< Sector(s) have not yet been dumped.
    Correct         = 0x20000000,  ///< Sector(s) contain valid MODE 1 data with regenerable suffix/prefix.
    Mode2Form1Ok    = 0x30000000,  ///< Sector suffix valid for MODE 2 Form 1; regenerable.
    Mode2Form2Ok    = 0x40000000,  ///< Sector suffix valid for MODE 2 Form 2 with correct CRC.
    Mode2Form2NoCrc = 0x50000000   ///< Sector suffix valid for MODE 2 Form 2 but CRC absent/empty.
} CdFixFlags;

/**
 * \enum TrackType
 * \brief Track (partitioning element) types for optical media.
 */
typedef enum
{
    kTrackTypeAudio           = 0,  ///< Audio track.
    kTrackTypeData            = 1,  ///< Generic data track (not further specified).
    kTrackTypeCdMode1         = 2,  ///< Compact Disc Mode 1 data track.
    kTrackTypeCdMode2Formless = 3,  ///< Compact Disc Mode 2 (formless) data track.
    kTrackTypeCdMode2Form1    = 4,  ///< Compact Disc Mode 2 Form 1 data track.
    kTrackTypeCdMode2Form2    = 5   ///< Compact Disc Mode 2 Form 2 data track.
} TrackType;

/**
 * \enum AaruformatStatus
 * \brief Status / error codes specific to libaaruformat.
 */
typedef enum
{
    AARUF_STATUS_INVALID_CONTEXT = -1  ///< Provided context/handle is invalid.
} AaruformatStatus;

/**
 * \enum XmlMediaType
 * \brief Enumeration of media types defined in CICM metadata.
 */
typedef enum
{
    OpticalDisc = 0,  ///< Purely optical discs.
    BlockMedia  = 1,  ///< Media that is physically block-based or abstracted like that.
    LinearMedia = 2,  ///< Media that can be accessed by-byte or by-bit, like chips.
    AudioMedia  = 3   ///< Media that can only store data when modulated to audio.
} XmlMediaType;

/**
 * \enum SectorStatus
 * \brief Acquisition / content status for one or more sectors.
 */
typedef enum
{
    SectorStatusNotDumped       = 0x0,  ///< Sector(s) not yet acquired during image dumping.
    SectorStatusDumped          = 0x1,  ///< Sector(s) successfully dumped without error.
    SectorStatusErrored         = 0x2,  ///< Error during dumping; data may be incomplete or corrupt.
    SectorStatusMode1Correct    = 0x3,  ///< Valid MODE 1 data with regenerable suffix/prefix.
    SectorStatusMode2Form1Ok    = 0x4,  ///< Suffix verified/regenerable for MODE 2 Form 1.
    SectorStatusMode2Form2Ok    = 0x5,  ///< Suffix matches MODE 2 Form 2 with valid CRC.
    SectorStatusMode2Form2NoCrc = 0x6,  ///< Suffix matches MODE 2 Form 2 but CRC empty/missing.
    SectorStatusTwin            = 0x7,  ///< Pointer references a twin sector table.
    SectorStatusUnrecorded      = 0x8,  ///< Sector physically unrecorded; repeated reads non-deterministic.
    SectorStatusEncrypted       = 0x9,  ///< Content encrypted and stored encrypted in image.
    SectorStatusUnencrypted     = 0xA,  ///< Content originally encrypted but stored decrypted in image.
    SectorStatusGenerable       = 0xB   ///< Content can be generated using a known algorithm.
} SectorStatus;

/**
 * \enum FeaturesCompatible
 * \brief Bit-mask of optional, backward-compatible features stored in an image.
 *
 * These flags advertise additional data structures or capabilities embedded in the
 * image that older readers MAY safely ignore. An unknown bit MUST be treated as
 * "feature unsupported" without failing to open the image. Writers set the bits for
 * features they included; readers test them to enable extended behaviors.
 *
 * Usage example:
 * \code{.c}
 * uint64_t features = header->featuresCompatible; // value read from on-disk header
 * if(features & AARU_FEATURE_RW_BLAKE3)
 * {
 *     // Image contains BLAKE3 checksums; enable BLAKE3 verification path.
 * }
 * \endcode
 *
 * Future compatible features SHALL use the next available bit (1ULL << n).
 */
typedef enum
{
    AARU_FEATURE_RW_BLAKE3 = 0x1,  ///< BLAKE3 checksum is present (read/write support for BLAKE3 hashes).
} FeaturesCompatible;

/**
 * @brief Incompatible feature flags for AaruHeader V2.
 *
 * If any bit in featureIncompatible is not understood by a reader, the image
 * MUST NOT be opened (the reader cannot safely interpret the data).
 *
 * Future incompatible features SHALL use the next available bit (1ULL << n).
 */
typedef enum
{
    AARU_FEATURE_INCOMPAT_ZSTD = 0x1,  ///< Image contains Zstandard-compressed blocks.
} FeaturesIncompatible;

/**
 * @brief Read-only compatible feature flags for AaruHeader V2.
 *
 * If any bit in featureCompatibleRo is not understood by a reader, the image
 * SHOULD be opened read-only (the reader cannot safely modify the data without
 * understanding these features).
 */
typedef enum
{
    AARU_FEATURE_ROCOMPAT_ERASURE = 0x1,  ///< Image contains erasure coding parity blocks and recovery metadata.
} FeaturesCompatibleRo;

/**
 * \enum ErasureCodingAlgorithm
 * \brief Erasure coding algorithms supported by the ECMB.
 */
typedef enum
{
    kErasureCodingXor          = 0,  ///< Simple XOR parity (M must be 1).
    kErasureCodingRsVandermonde = 1   ///< Reed-Solomon with Vandermonde generator matrix over GF(2^8).
} ErasureCodingAlgorithm;

/**
 * \enum ErasureCodingGroupType
 * \brief Identifies which protection group a stripe belongs to.
 */
typedef enum
{
    kECGroupData       = 0,  ///< User data blocks (DBLK).
    kECGroupDdtSecondary = 1, ///< Secondary DDT subtables.
    kECGroupDdtPrimary  = 2,  ///< Primary DDT (K=1, M replicas).
    kECGroupMetadata    = 3,  ///< Metadata/media tag blocks.
    kECGroupIndex       = 4   ///< Index block (K=1, M replicas).
} ErasureCodingGroupType;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif  // LIBAARUFORMAT_ENUMS_H
