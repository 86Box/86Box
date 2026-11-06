/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of removable disk drives with SCSI(-like)
 *          commands, for both ATAPI and SCSI usage.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2018-2025 Miran Grca.
 *          Copyright 2022-2026 Jasmine Iwanek.
 */
#ifndef EMU_RDISK_H
#define EMU_RDISK_H

#define RDISK_NUM                  4

#define BUF_SIZE               32768

#define RDISK_TIME              10.0

/* Bernoulli cartridges, in blocks of the size in rdisk_types[] below.  These are the capacities that Iomega's software
   tells the cartridges apart by: the table of MPREP.EXE (OAD 2.31 for OS/2), which has the last LBA and the block size
   of each cartridge, and GetMediaType() in SCSIDLL.DLL (Tools 5.4).  The 90 MB one is also the one measured in Aaru device
   reports 186 and 202 (8 heads x 758 cylinders x 29 sectors).

   The 10 and 20 MB cartridges have blocks of 256 bytes.  The 10 MB one is an Alpha-10H, 306 tracks x 64 sectors of 512
   bytes, which is 39,168 blocks.  MPREP.EXE has that, and 83,712 for the 20 MB cartridge of the Alpha-20H (654 tracks x 64
   sectors of 512 bytes; not emulated), but the Bernoulli Box II 20 has the 83,664 below. */
#define BERNOULLI_10_SECTORS    (39168)
#define BERNOULLI_20_SECTORS    (83664)
#define BERNOULLI_35_SECTORS    (69654)
#define BERNOULLI_44_SECTORS    (87040)
#define BERNOULLI_65_SECTORS    (129090)
#define BERNOULLI_90_SECTORS    (175856)    /* Measured */
#define BERNOULLI_105_SECTORS   (206856)
#define BERNOULLI_150_SECTORS   (294918)
#define BERNOULLI_230_SECTORS   (450566)

/* 78882 blocks of 512 bytes (40,387,584 bytes) per Aaru device report 50, a real
   Iomega Clik! 40 CZ with a Clik! medium. */
#define CLIK_SECTORS              (78882)

/* Floppy media read by the SuperDisk, HiFD and Floptical drives, from Aaru device reports 66 and 67.
   The NEC 2HD medium has 1232 sectors of 1024 bytes (1,261,568 bytes); the rest use 512-byte sectors. */
#define FD_2HD_NEC_SECTORS      (1232)       /* 1.2 MB, 1024 bytes per sector */
#define FD_720K_SECTORS         (1440)
#define FD_1440K_SECTORS        (2880)
#define FD32MB_SECTORS          (65536)      /* Imation FD32MB, medium type 0x28: 1024 cylinders * 2 heads * 32 sectors; 512-byte sectors */

/* PC floppy formats that the LS-120 drives take besides the 720 KB, 1.44 MB and NEC 2HD ones above.  MKELS120
   recognizes them as medium types 12h (640 KB), 23h (1.2 MB, 15 sectors per track) and 26h (DMF), see the LS-120
   implementation specification, section 5.

   The 640 KB image has 1280 sectors (8 per track), but the drive presents it as a 720 KB medium of 1440 device
   blocks (9 per track) of which the last of each track does not exist: MKELS120 addresses it as device block
   S + S / 8 for logical sector S.  FD_640K_SECTORS is the size of the image, FD_640K_BLOCKS the number of blocks
   the drive reports (READ CAPACITY, block descriptor). */
#define FD_640K_SECTORS         (1280)
#define FD_640K_BLOCKS          (1440)
#define FD_1200K_SECTORS        (2400)       /* 80 cylinders * 2 heads * 15 sectors, 512-byte sectors */
#define FD_DMF_SECTORS          (3360)       /* 80 cylinders * 2 heads * 21 sectors, 512-byte sectors */

/* 40,662 blocks per Aaru device report 258, a real INSITE I325VM Floptical with a 20 MB medium. */
#define FLOPTICAL_SECTORS       (40662)

/* 393,380 blocks per Aaru device report 118, a real Sony HiFD SFD200 with a HiFD medium
   (13 heads x 890 cylinders x 34 sectors). */
#define HIFD_SECTORS            (393380)    /* Measured: 200 MB HiFD, medium type 0x42 */

/* Estimated: 150,000,000 bytes / 512.  Probably the HiFD's other medium (type 0x40 in its page 0B). */
#define HIFD_150_SECTORS        (292969)

#define JAZ_1GB_SECTORS     (2091050)

#define JAZ_2GB_SECTORS     (3915600)

/* The Peerless: IomegaWare 4.01 (ioReady.dll) tells its disks apart by the last LBA with the low byte
   cleared: 950A00h, 12BB200h and 2542900h.  The 2.5 inch hard disk counts below are the ones of that size
   which fall in those ranges and are also a whole number of 16 x 63 cylinders.  ESTIMATED: no real
   Peerless capacity has been seen. */
#define PEERLESS_5GB_SECTORS    (9767520)
#define PEERLESS_10GB_SECTORS   (19640880)
#define PEERLESS_20GB_SECTORS   (39070080)

/* REV disks have 2048-byte blocks.  35 GB and 120 GB: Aaru device reports 187 and 188, 35,002,122,240
   and 120,054,874,112 bytes.  70 GB: ESTIMATED, twice the 35 GB disk. */
#define REV_35_SECTORS          (17090880)
#define REV_70_SECTORS          (34181760)
#define REV_120_SECTORS         (58620544)

/* 4,307,184 blocks per Aaru device report 200, a real Castlewood ORB 2.2 GB medium. */
#define ORB_2_2GB_SECTORS       (4307184)

#define SUPERDISK_SECTORS      (963 * 256)

#define SUPERDISK_240_SECTORS     (469504)

/* SyQuest cartridges, from Aaru device reports 195, 196 and 197. */
#define SQ400_SECTORS           (86700)      /* 5.25", 44 MB */
#define SQ800_SECTORS           (173456)     /* 5.25", 88 MB */
#define SQ2000_SECTORS          (390696)     /* 5.25", 200 MB */
#define SQ310_SECTORS           (215440)     /* 3.5", 105 MB */
#define SQ327_SECTORS           (524288)     /* 3.5", 256 MB: CHS 1024 x 16 x 32, SyQuest's SQ3270A sheet */
/* The EZ135 medium is 262,144 blocks (128 MiB, CHS 512 x 16 x 32), per Aaru report 195 and the
   "hdd: SyQuest EZ135A ... CHS=512/16/32" line of a 1995 comp.os.linux.hardware post. */
#define EZ135_SECTORS           (262144)
#define EZFLYER_230_SECTORS     (450560)

/* Last LBA of 2,929,799 (i.e. 2,929,800 sectors) per the SyQuest SyJet SCSI
   Technical Reference (1997, P/N 112857-001A), section "Read Capacity (25H)". */
#define SYJET_SECTORS       (2929800)

/* Total addressable LBA count of 1,961,069 sectors per the SyQuest SparQ
   Internal EIDE Technical Reference (1997, P/N 113277-001A), section
   "Identify Device (ECh)", words 60-61. The SparQ only ever shipped as an
   IDE/EIDE (and external parallel-port) drive - it has no real SCSI variant. */
#define SPARQ_SECTORS       (1961069)

/* The Caleb UHD144 (1999, ATAPI, reports itself as an "LS-120").  281,856 sectors were observed on real media
   (boot sector, Linux fdisk and MBRWizard; 64 heads x 32 sectors x 137.625 cylinders) in
   https://msfn.org/board/topic/152866-caleb-uhd144-drive-under-win98-and-dos/, whereas Caleb's own
   specifications give 281,504 (web page and data sheet, 2011 or 1,844 tracks) and 281,376 (technical manual, 1960
   tracks, 8 zones, 50 to 90 sectors per track, 112 spares). */
#define UHD144_SECTORS          (281856)

#define ZIP_SECTORS      (96 * 2048)

/* The 25 MB Zip disk: a last LBA of BFFFh in GetMediaType() of Iomega's SCSIDLL.DLL. */
#define ZIP_25_SECTORS      (24 * 2048)

#define ZIP_250_SECTORS     (489532)

/* IomegaWare 4.01 (the Zip 750 CD), ioReady.dll: the Zip 750 tells its disks apart by the last LBA of a
   READ CAPACITY, with its low byte cleared, and needs 512-byte blocks: 2FF00h for a 100 MB disk, 77800h
   for 250 MB and 167000h for 750 MB, so the last LBA of a 750 MB disk is 167000h to 1670FFh.  The drive
   object also gives 2CE00000h (752,877,568 bytes, 1,470,464 sectors) as its largest raw capacity, which
   is the 718 x 64 x 32 of page 5 in Aaru device report 53.
   The exact count is ESTIMATED: it takes the 60 sectors that the Zip 250 has above its own page 5
   geometry (489,532 vs. 489,472).  1,470,464 itself would give a last LBA of 166FFFh, and the 1,468,596
   in the title of ZuluIDE firmware issue 39, with no READ CAPACITY behind it, is outside that range. */
#define ZIP_750_SECTORS     (1470524)

#define RDISK_IMAGE_HISTORY       10

enum {
    RDISK_TYPE_GENERIC = 0,
    /* Bernoulli */
    RDISK_TYPE_BERNOULLI_20,
    RDISK_TYPE_BERNOULLI_44,
    RDISK_TYPE_BERNOULLI_90,
    RDISK_TYPE_BERNOULLI_150,
    RDISK_TYPE_BERNOULLI_150_IDE,
    RDISK_TYPE_BERNOULLI_230,
    /* Caleb UHD144 */
    RDISK_TYPE_UHD144,
    /* Castlewood ORB */
    RDISK_TYPE_ORB_2_2GB,
    /* Clik! */
    RDISK_TYPE_CLIK_40,
    /* Floptical */
    RDISK_TYPE_FLOPTICAL_20,
    RDISK_TYPE_FLOPTICAL_IO20S,
    /* HiFD */
    RDISK_TYPE_HIFD_200,
    /* Jaz */
    RDISK_TYPE_JAZ_1GB,
    RDISK_TYPE_JAZ_2GB,
    /* Peerless */
    RDISK_TYPE_PEERLESS,
    /* Rev */
    RDISK_TYPE_REV_35,
    RDISK_TYPE_REV_70,
    RDISK_TYPE_REV_120,
    /* SuperDisk */
    RDISK_TYPE_SUPERDISK_120,
    RDISK_TYPE_SUPERDISK_120_COSM04_MATSHITA,
    RDISK_TYPE_SUPERDISK_120_VER5,
    RDISK_TYPE_SUPERDISK_120_VER5_COMPAQ,
    RDISK_TYPE_SUPERDISK_120_HIMA_COMPAQ,
    RDISK_TYPE_SUPERDISK_120_IMATION,
    RDISK_TYPE_SUPERDISK_240,
    RDISK_TYPE_SUPERDISK_240_IMATION,
    /* SyQuest */
    RDISK_TYPE_SQ555,
    RDISK_TYPE_SQ5110C,
    RDISK_TYPE_SQ3105S,
    RDISK_TYPE_SQ3105A,
    RDISK_TYPE_SQ3270S,
    RDISK_TYPE_SQ3270A,
    RDISK_TYPE_SQ5200C,
    RDISK_TYPE_EZ135S,
    RDISK_TYPE_EZ135A,
    RDISK_TYPE_EZ230S,
    RDISK_TYPE_EZ230A,
    RDISK_TYPE_SYJET_1_5GB,
    RDISK_TYPE_SYJET_S,
    RDISK_TYPE_SPARQ_1GB,
    /* Zip */
    RDISK_TYPE_ZIP_100,
    RDISK_TYPE_ZIP_100_PLUS,
    RDISK_TYPE_ZIP_250,
    RDISK_TYPE_ZIP_750
};

/* Index of each medium in rdisk_types[] (and in supported_media[]): by class in alphabetical order, and by year in a
   class, as far as the year is known.  These numbers are not saved anywhere. */
enum {
    /* Bernoulli */
    RDISK_MEDIUM_BERNOULLI_10 = 0,
    RDISK_MEDIUM_BERNOULLI_20,
    RDISK_MEDIUM_BERNOULLI_44,
    RDISK_MEDIUM_BERNOULLI_90,
    RDISK_MEDIUM_BERNOULLI_35,
    RDISK_MEDIUM_BERNOULLI_65,
    RDISK_MEDIUM_BERNOULLI_105,
    RDISK_MEDIUM_BERNOULLI_150,
    RDISK_MEDIUM_BERNOULLI_230,
    /* Clik! */
    RDISK_MEDIUM_CLIK_40,
    /* Floppy disks */
    RDISK_MEDIUM_FD_1200K,
    RDISK_MEDIUM_FD_2HD_NEC,
    RDISK_MEDIUM_FD_640K,
    RDISK_MEDIUM_FD_720K,
    RDISK_MEDIUM_FD_1440K,
    RDISK_MEDIUM_FD_DMF,
    RDISK_MEDIUM_FD32MB,
    /* Floptical */
    RDISK_MEDIUM_FLOPTICAL_20,
    /* HiFD */
    RDISK_MEDIUM_HIFD_150,
    RDISK_MEDIUM_HIFD_200,
    /* Jaz */
    RDISK_MEDIUM_JAZ_1GB,
    RDISK_MEDIUM_JAZ_2GB,
    /* ORB */
    RDISK_MEDIUM_ORB_2_2GB,
    /* Peerless */
    RDISK_MEDIUM_PEERLESS_5GB,
    RDISK_MEDIUM_PEERLESS_10GB,
    RDISK_MEDIUM_PEERLESS_20GB,
    /* Rev */
    RDISK_MEDIUM_REV_35,
    RDISK_MEDIUM_REV_70,
    RDISK_MEDIUM_REV_120,
    /* SuperDisk */
    RDISK_MEDIUM_SUPERDISK_120,
    RDISK_MEDIUM_SUPERDISK_240,
    /* SyQuest */
    RDISK_MEDIUM_SQ400,
    RDISK_MEDIUM_SQ800,
    RDISK_MEDIUM_SQ310,
    RDISK_MEDIUM_SQ327,
    RDISK_MEDIUM_SQ2000,
    RDISK_MEDIUM_EZ135,
    RDISK_MEDIUM_EZFLYER_230,
    RDISK_MEDIUM_SYJET_1_5GB,
    RDISK_MEDIUM_SPARQ_1GB,
    /* UHD144 */
    RDISK_MEDIUM_UHD144,
    /* Zip */
    RDISK_MEDIUM_ZIP_100,
    RDISK_MEDIUM_ZIP_25,
    RDISK_MEDIUM_ZIP_250,
    RDISK_MEDIUM_ZIP_750,
    RDISK_MEDIUM_COUNT
};

typedef struct rdisk_type_t {
    const char *name;
    uint32_t    sectors;
    uint16_t    bytes_per_sector;
    uint8_t     medium_type_code;
} rdisk_type_t;

#define KNOWN_RDISK_TYPES RDISK_MEDIUM_COUNT
static const rdisk_type_t rdisk_types[KNOWN_RDISK_TYPES] = {
    /* Bernoulli */
    { "Bernoulli 10",  BERNOULLI_10_SECTORS,  256, 0x00 },
    { "Bernoulli 20",  BERNOULLI_20_SECTORS,  256, 0x00 },
    { "Bernoulli 44",  BERNOULLI_44_SECTORS,  512, 0x00 },
    { "Bernoulli 90",  BERNOULLI_90_SECTORS,  512, 0x00 },
    { "Bernoulli 35",  BERNOULLI_35_SECTORS,  512, 0x00 },
    { "Bernoulli 65",  BERNOULLI_65_SECTORS,  512, 0x00 },
    { "Bernoulli 105", BERNOULLI_105_SECTORS, 512, 0x00 },
    { "Bernoulli 150", BERNOULLI_150_SECTORS, 512, 0x00 },
    { "Bernoulli 230", BERNOULLI_230_SECTORS, 512, 0x00 },
    /* Clik! */
    { "Clik!",         CLIK_SECTORS,          512, 0x00 },
    /* Floppy disks */
    /* The 640 KB image is reported as a 720 KB medium (0x11): MKELS120 derives its 640 KB profile from the BPB's sectors
       per track.  The other formats are read by the LS-120 drives as MKELS120 profiles 23h and 26h. */
    { "1.2 MB (PC)",   FD_1200K_SECTORS,      512, 0x23 },
    { "2HD (1.2 MB)",  FD_2HD_NEC_SECTORS,   1024, 0x22 },
    { "640 KB",        FD_640K_SECTORS,       512, 0x11 },
    { "720 KB",        FD_720K_SECTORS,       512, 0x11 },
    { "1.44 MB",       FD_1440K_SECTORS,      512, 0x24 },
    { "DMF (1.68 MB)", FD_DMF_SECTORS,        512, 0x26 },
    { "FD32MB",        FD32MB_SECTORS,        512, 0x28 },
    /* Floptical */
    { "Floptical",     FLOPTICAL_SECTORS,     512, 0x00 },
    /* HiFD */
    { "HiFD 150",      HIFD_150_SECTORS,      512, 0x40 },
    { "HiFD",          HIFD_SECTORS,          512, 0x42 },
    /* Jaz */
    { "Jaz 1 GB",      JAZ_1GB_SECTORS,       512, 0x00 },
    { "Jaz 2 GB",      JAZ_2GB_SECTORS,       512, 0x00 },
    /* ORB */
    { "ORB 2.2 GB",    ORB_2_2GB_SECTORS,     512, 0x00 },
    /* Peerless */
    { "Peerless 5 GB", PEERLESS_5GB_SECTORS,  512, 0x00 },
    { "Peerless 10 GB", PEERLESS_10GB_SECTORS, 512, 0x00 },
    { "Peerless 20 GB", PEERLESS_20GB_SECTORS, 512, 0x00 },
    /* Rev: medium type 41h in Aaru device reports 187 and 188. */
    { "REV 35 GB",     REV_35_SECTORS,       2048, 0x41 },
    { "REV 70 GB",     REV_70_SECTORS,       2048, 0x41 },
    { "REV 120 GB",    REV_120_SECTORS,      2048, 0x41 },
    /* SuperDisk */
    { "LS-120",        SUPERDISK_SECTORS,     512, 0x31 },
    { "LS-240",        SUPERDISK_240_SECTORS, 512, 0x32 },
    /* SyQuest */
    { "SQ400",         SQ400_SECTORS,         512, 0x00 },
    { "SQ800",         SQ800_SECTORS,         512, 0x00 },
    { "SQ310",         SQ310_SECTORS,         512, 0x00 },
    { "SQ327",         SQ327_SECTORS,         512, 0x00 },
    { "SQ2000",        SQ2000_SECTORS,        512, 0x00 },
    { "EZ135",         EZ135_SECTORS,         512, 0x00 },
    { "EZFlyer 230",   EZFLYER_230_SECTORS,   512, 0x00 },
    { "Syjet",         SYJET_SECTORS,         512, 0x00 },
    { "Sparq",         SPARQ_SECTORS,         512, 0x00 },
    /* UHD144 */
    { "UHD144",        UHD144_SECTORS,        512, 0x00 },
    /* Zip */
    { "ZIP 100",       ZIP_SECTORS,           512, 0x00 },
    { "ZIP 25",        ZIP_25_SECTORS,        512, 0x00 },
    { "ZIP 250",       ZIP_250_SECTORS,       512, 0x00 },
    { "ZIP 750",       ZIP_750_SECTORS,       512, 0x00 }
};

enum {
    RDISK_BUS_DISABLED =  0,
    RDISK_BUS_LPT      =  6,  /* coincides with HDD_BUS_LPT/CDROM_BUS_LPT for config strings */
    RDISK_BUS_IDE      =  7,  /* coincides with HDD_BUS_IDE: a native ATA (not ATAPI) drive */
    RDISK_BUS_ATAPI    =  8,  /* coincides with HDD_BUS_ATAPI */
    RDISK_BUS_SCSI     =  9,  /* coincides with HDD_BUS_SCSI */
    RDISK_BUS_USB      = 10,
};

/* Which bus(es) a drive type belongs on, modelled on tape_drive_type_t's drive_type in scsi_tape.h.
   A single bus is that bus's RDISK_BUS_* value; the negative values are combinations. */
#define RDISK_DRIVE_TYPE_LPT            RDISK_BUS_LPT
#define RDISK_DRIVE_TYPE_ATAPI          RDISK_BUS_ATAPI
#define RDISK_DRIVE_TYPE_SCSI           RDISK_BUS_SCSI
#define RDISK_DRIVE_TYPE_SCSI_ATAPI     (-2)
#define RDISK_DRIVE_TYPE_NONE           (-1)
#define RDISK_DRIVE_TYPE_ATAPI_LPT      (-3)
#define RDISK_DRIVE_TYPE_SCSI_ATAPI_LPT (-4)
#define RDISK_DRIVE_TYPE_SCSI_LPT       (-5)
#define RDISK_DRIVE_TYPE_IDE_ATAPI      (-6)

typedef struct rdisk_drive_type_t {
    const char *vendor;
    const char *model;
    const char *revision;
    uint8_t     default_media;
    int8_t      drive_type;
    int8_t      supported_media[KNOWN_RDISK_TYPES];
} rdisk_drive_type_t;

/*
   Zip 750 firmware revision.  Default: "78.a", from the full INQUIRY dump of a real Zip 750 in
   ZuluIDE firmware issue 39 (build date 08/27/02, copyright 2002).  Alternate (#if 0): "72.u", from
   Aaru device report 53, where the date and copyright string were not captured, so the same
   ones are used with it.  Both were seen on USB drives; the revision of a real ATAPI Zip 750 is unknown.
 */
#if 0
#    define ZIP_750_REV    "72.u"
#else
#    define ZIP_750_REV    "78.a"
#endif

/*
   Firmware revision of the SyQuest drives that have no report: a placeholder.
 */
#define SYQUEST_PLACEHOLDER_REV "1.00"

/*
   SyJet-S (SCSI) firmware revision.  Real SyJet-S drives were seen with 0111 (Aaru report 327), 0095
   (report 328) and 0117 (report 329); none of the reports include more than the INQUIRY data.
   Default: 0117, the highest.  To use another, change the 0 of its #if/#elif to 1.
 */
#if 0
#    define SYJET_S_REV    "0095"
#elif 0
#    define SYJET_S_REV    "0111"
#else
#    define SYJET_S_REV    "0117"
#endif

#define KNOWN_RDISK_DRIVE_TYPES 45
/* vendor, model and revision are the strings the drive reports in INQUIRY (8, 16 and 4 characters, shorter
   ones are space padded), which rdisk.c uses for everything but the generic drive: that one reports the
   emulator's own name, product and version.  A model whose INQUIRY identity differs (another firmware
   family, another bus) is its own row.  default_media says which medium the drive is built around (it
   also sets the sector size of an empty drive); supported_media[] is indexed like rdisk_types[]
   (see RDISK_MEDIUM_*) and rdisk.c takes the media a drive accepts, its largest medium and whether it is a
   one-medium drive from it.
   drive_type says which bus(es) each type belongs on (see RDISK_DRIVE_TYPE_* above): the interfaces
   the real drive shipped with where that is known (from the Aaru device reports and the notes below); the
   generic drive and the Zip 100/250, which rdisk.c emulates on SCSI, ATAPI and through the parallel-port
   bridge, take all three.  rdisk.c itself does not refuse a type on a bus: use
   rdisk_drive_type_supports_bus() below to filter.
   The rows are by class in alphabetical order, after the generic drive, and by year in a class as far as it is known;
   the Settings dialog shows them in this order.  rdisk_type_config_ids[] below keeps saved configurations valid. */
static const rdisk_drive_type_t rdisk_drive_types[KNOWN_RDISK_DRIVE_TYPES] = {
    { "86BOX", "REMOVABLE DISK", "5.00", RDISK_MEDIUM_ZIP_100, RDISK_DRIVE_TYPE_SCSI_ATAPI_LPT, { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 } },
    /* ---- Bernoulli ---- */
    /* The Bernoulli Box II 20 and 44, SCSI drives that no Aaru report has.  Their INQUIRY product is what the OAD driver set
       for OS/2 (the .OPT file of each driver) and GetDevType() in SCSIDLL.DLL look for; their firmware revision is
       not known.  The 44 reads and writes 44 MB cartridges only (Iomega Bernoulli SCSI Drive Settings and Specs); which
       other cartridges the 20 takes is not known. */
    { "IOMEGA", "BETA20", "", RDISK_MEDIUM_BERNOULLI_20, RDISK_DRIVE_TYPE_SCSI, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "IOMEGA", "BETA44", "", RDISK_MEDIUM_BERNOULLI_44, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* From Aaru device reports 186 (BETA90), 202 (BETA150, firmware T.47) and 311 (BETA230), all SCSI-only
       drives.  The BETA150 row is firmware T.51, dated 04/04/94, from a dump of its ROM.
       Cartridge support follows the documented table (Iomega Bernoulli SCSI Drive Settings and Specs):
       Bernoulli 90: 90 MB read/write, 44 MB read-only; MultiDisk 150: 35, 65, 90, 105 and 150 MB read/write,
       44 MB read-only; Bernoulli 230: 90, 150 and 230 MB read/write.  rdisk.c makes the 44 MB cartridge
       read-only in the 90 and 150 (the T.51 firmware has a writable flag per recording format, clear for the
       44 MB one).  The 10 MB cartridge belongs to an earlier drive that is not emulated.
       The Bernoulli 230 differs from that table: it also takes 35, 44, 65 and 105 MB cartridges here, which is the
       list of cartridges in SCSIUTIL.EXE (Iomega SCSI tools 5.0) for this drive.  The tool does not say what a drive
       can do with each, so the 35, 65 and 105 MB ones are read/write as in the 150 and the 44 MB one is read-only. */
    { "IOMEGA", "BETA90", "1.4", RDISK_MEDIUM_BERNOULLI_90, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "IOMEGA", "BETA150", "T.51", RDISK_MEDIUM_BERNOULLI_150, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* The Bernoulli Insider 150 IDE.  SCSIUTIL.EXE knows it by the product "BETA150-IDE", calls it "INSIDER 150 IDE"
       and treats it as a BETA150 in all but a line of text on a screen, so it is the MultiDisk 150 of the row above on
       ATAPI here, with the same cartridges.  No driver or report has more: the firmware revision is the 150's, and
       the ATA identity and whether the real drive is ATAPI at all are ESTIMATED.  Iomega's IDE ASPI manager of the
       time, ASPIIDE.SYS, only knows the native ATA Zip 100. */
    { "IOMEGA", "BETA150-IDE", "T.51", RDISK_MEDIUM_BERNOULLI_150, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "IOMEGA", "BETA230", "2.02", RDISK_MEDIUM_BERNOULLI_230, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* ---- Caleb UHD144 ---- */
    /* The Caleb UHD144: an ATAPI drive that identifies itself as "Caleb LS-120" (no firmware revision shown).
       It reads 144 MB UHD144 media and 1.44 MB floppies (Caleb's manual, which only hints at DD disks, so the
       720 KB is assumed).  A UHD disk has no write-protect tab: Caleb's utility sets that with vendor opcode F0h,
       and the same manual says that the drive senses a floppy's tab.  Caleb's tools accept any drive with the
       vendor "Caleb" (the list "Caleb,*;" in itDevice.dll). */
    { "Caleb", "LS-120", "", RDISK_MEDIUM_UHD144, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 } },
    /* ---- Castlewood ORB ---- */
    /* Aaru device report 200, a real SCSI Castlewood ORB 2.2 GB.  The IDE ORB2-E is a native ATA drive, which
       is as near as ATAPI gets here (see rdisk_orb_identify()), hence SCSI_ATAPI.  There is no row for the ORB 5.7 GB:
       ORB Tools 1.64 only compares a product with "ORB5" and has Release5GB=FALSE, which gives no capacity. */
    { "CWS ORB2", "-SE U ID 0", "D01K", RDISK_MEDIUM_ORB_2_2GB, RDISK_DRIVE_TYPE_SCSI_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* ---- Clik! ---- */
    /* From Aaru device report 50, a real ATAPI Clik! 40 CZ. */
    { "IOMEGA", "Clik! 40 CZ", "30cJ", RDISK_MEDIUM_CLIK_40, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* ---- Floptical ---- */
    /* From Aaru device report 258, a real SCSI-1 INSITE Floptical (the INQUIRY product field really is
       "I325VM" + 8 spaces + "*F"); the report says it also reads 720 KB and 1.44 MB floppies. */
    { "INSITE", "I325VM        *F", "0387", RDISK_MEDIUM_FLOPTICAL_20, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* Iomega's own Floptical.  Vendor, product (like the INSITE, 5 characters, spaces and "*F"), firmware and SCSI-2
       are from a TattleTech report of a real drive (RaSCSI wiki, Iomega Floptical).  The cartridges and how to tell
       them apart are from SCSIUTIL.EXE, which knows it as "Io20S"; see rdisk_mode_sense_medium_type(). */
    { "IOMEGA", "Io20S         *F", "PP33", RDISK_MEDIUM_FLOPTICAL_20, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* ---- HiFD ---- */
    /* From Aaru device report 118, a real ATAPI Sony HiFD SFD200.  Its page 0B lists medium types 0x40 and 0x42; 0x42
       is the 200 MB disk, and 0x40 is taken to be a 150 MB one.  Nothing in Sony's driver or tools (IBM option 20L0543)
       knows a 150 MB disk or tests either type, so that medium remains an estimate.
       No floppy media: the internal HiFD is a "Dual I/F" drive, with ATAPI for HiFD disks and a floppy cable for
       720 KB and 1.44 MB disks, which Sony calls LoFD.  Over ATAPI it does not read floppies, as report 118 found
       with MFD-2DD and MFD-2HD disks.  A floppy in that drive belongs to the floppy controller, see rdisk.c. */
    { "SONY", "HiFD", "1264", RDISK_MEDIUM_HIFD_200, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* ---- Jaz ---- */
    /* The Jaz was a SCSI drive; rdisk.c also has ATAPI identify data for it, so both are allowed.  Its INQUIRY
       vendor and product really are lower case (Aaru report 49). */
    { "iomega", "jaz 1GB", "H.72", RDISK_MEDIUM_JAZ_1GB, RDISK_DRIVE_TYPE_SCSI_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "iomega", "jaz 2GB", "E.17", RDISK_MEDIUM_JAZ_2GB, RDISK_DRIVE_TYPE_SCSI_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* ---- Peerless ---- */
    /* INQUIRY strings from ioReady.dll of IomegaWare 4.01.  The real drive came with a FireWire or USB module
       and a SCSI module was announced, so only the SCSI bus here; the firmware revision is MADE UP. */
    { "Iomega", "Peerless", "1.00", RDISK_MEDIUM_PEERLESS_20GB, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* ---- Rev ---- */
    /* Aaru device reports 148, 187 and 188, all on USB.  RRD3 reads 70 GB disks and does not write them; RRD2
       reads and writes 35 GB disks (report 187 was made with one).  The RRD is taken to be the 35 GB drive,
       although usb.ids calls its 059B:0042 "Rev 70 GB".  Rev drives are not hard disks but MMC devices, type 5.
       ATAPI: the 35 and 70 GB drives were sold with it.  The 120 GB drive was USB and SATA, emulated here
       as ATAPI.  SCSI: Iomega sold external SCSI Rev drives; the model is assumed. */
    { "Iomega", "RRD", "23.B", RDISK_MEDIUM_REV_35, RDISK_DRIVE_TYPE_SCSI_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "Iomega", "RRD2", "P099", RDISK_MEDIUM_REV_70, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "Iomega", "RRD3", "M074", RDISK_MEDIUM_REV_120, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* ---- SuperDisk ---- */
    /* The SuperDisk drives also read 720 KB, 1.2 MB (NEC 2HD) and 1.44 MB floppies, and the LS-240
       also the 32 MB (FD32MB) format (Aaru reports 66 and 67).  This LS-120 is a Mitsubishi-made LS-120 COSM 05,
       firmware 0512C105, from three ATAPI captures of a real drive (no disk and two with a 1.44 MB floppy); the
       vendor really is spelt "MITBISHI". */
    { "MITBISHI", "LS-120 COSM   05", "0512", RDISK_MEDIUM_SUPERDISK_120, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* The MATSHITA variant of the MITBISHI LS-120 COSM 05: its ATA strings are the ones that rdisk.c already had for an
       LS-120 COSM 04, firmware 0270M09T.  It is a clone of the COSM 05 in all else, and none of that is confirmed for
       this drive: the captures are of the Mitsubishi one.  This identity matters: MKEDEVIO.DLL (the format utility of
       every Panasonic and Imation driver kit) only takes an INQUIRY that has "MATSHITALS-" in it, unless the registry
       value C35 says otherwise, and no kit sets it; mkefpd.sys (NT 4) and mkesflop.sys (Windows 2000) look for the vendor
       MATSHITA and "LS-" in the product.  The kits have no sign of the MITBISHI and IMATION identities. */
    { "MATSHITA", "LS-120 COSM   04", "0270", RDISK_MEDIUM_SUPERDISK_120, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* A Matsushita LS-120 VER5 00 (firmware F523), from Aaru device report 66 (behind a Shuttle E-USB bridge). */
    { "MATSHITA", "LS-120 VER5   00", "F523", RDISK_MEDIUM_SUPERDISK_120, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* COMPAQ LS-120: in the sfloppy list of flpydisk.inf of Windows 2000 and XP, as hardware IDs that have
       vendor and product but no revision, so the firmware revisions here are MADE UP.  Otherwise as the
       MATSHITA LS-120 VER5. */
    { "COMPAQ", "LS-120 VER5   AB", "F523", RDISK_MEDIUM_SUPERDISK_120, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "COMPAQ", "LS-120 HIMA   01", "0100", RDISK_MEDIUM_SUPERDISK_120, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* The Imation-branded SuperDisk 120 and 240 from the original table.  These strings are not confirmed
       to be what a real drive's INQUIRY returns, and Imation's own driver kits would not accept them, see the
       MATSHITA LS-120 COSM 04. */
    { "IMATION", "SUPERDISK 120", "04", RDISK_MEDIUM_SUPERDISK_120, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* Aaru report 67 (behind a USB bridge; its ATA IDENTIFY strings are derived from these). */
    { "MATSHITA", "LS-120/240    00", "K047", RDISK_MEDIUM_SUPERDISK_240, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "IMATION", "SUPERDISK 240", "04", RDISK_MEDIUM_SUPERDISK_240, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* ---- SyQuest ---- */
    /* SyQuest drives that no report has, here and below.  The products of the IDE drives are in SQATDRVR.SYS 1.89 (byte-swapped,
       after "SyQuest ") and those of the SCSI drives in SQDRIVER.SYS 7.95 and SyQuest's driver lists; the firmware revisions
       are placeholders.  The real IDE drives are ATA disks with removable media commands.  The EZ135A and EZ230A are
       that on the IDE bus (RDISK_BUS_IDE, see rdisk_drive_reset()), with ATAPI kept as an approximation, which is all the
       SQ3105A and SQ3270A are.  Their manuals do say that SyQuest EIDE drives support the ATAPI standard.  The cartridges are
       those of each drive's name, plus the 105 MB in the SQ3270 and the 44 MB in the SQ5110C, from what is generally
       known of these drives rather than from the drivers.  EZFlyer EIDE: 880 / 16 / 32, which is EZFLYER_230_SECTORS. */
    { "SyQuest", "SQ555", SYQUEST_PLACEHOLDER_REV, RDISK_MEDIUM_SQ400, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* SCSIPORT.PDR of Windows 95 has a real SQ5110 in its table of drives with faults, by exact vendor, product and
       revision: "SyQuest ", "SQ5110", "CHC".  The fault is not emulated. */
    { "SyQuest", "SQ5110", "CHC", RDISK_MEDIUM_SQ800, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* Aaru device report 196, a real SCSI SyQuest SQ3105S (SQ310 cartridge). */
    { "SyQuest", "SQ3105S", "1_21", RDISK_MEDIUM_SQ310, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "SyQuest", "SQ3105A", SYQUEST_PLACEHOLDER_REV, RDISK_MEDIUM_SQ310, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "SyQuest", "SQ3270S", SYQUEST_PLACEHOLDER_REV, RDISK_MEDIUM_SQ327, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "SyQuest", "SQ3270A", SYQUEST_PLACEHOLDER_REV, RDISK_MEDIUM_SQ327, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* Aaru device report 197, a real SCSI SyQuest SQ5200C, which reads the SQ400, SQ800 and SQ2000. */
    { "SyQuest", "SQ5200C", "3CE4", RDISK_MEDIUM_SQ2000, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "SyQuest", "EZ135S", SYQUEST_PLACEHOLDER_REV, RDISK_MEDIUM_EZ135, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "SyQuest", "EZ135A", SYQUEST_PLACEHOLDER_REV, RDISK_MEDIUM_EZ135, RDISK_DRIVE_TYPE_IDE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 } },
    /* Aaru device report 195, a real SCSI SyQuest EZ230S, which reads EZ135 and EZFlyer 230 media. */
    { "SyQuest", "EZ230S", "7220", RDISK_MEDIUM_EZFLYER_230, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0 } },
    { "SyQuest", "EZ230A", SYQUEST_PLACEHOLDER_REV, RDISK_MEDIUM_EZFLYER_230, RDISK_DRIVE_TYPE_IDE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0 } },
    /* The ATAPI SyJet: its identity is not confirmed real (the revision is a placeholder). */
    { "SyQuest", "SyJet", "1.06", RDISK_MEDIUM_SYJET_1_5GB, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 } },
    /* The SCSI SyJet-S, seen with firmware 0111, 0095 and 0117 (Aaru reports 327, 328, 329); see SYJET_S_REV
       above. */
    { "SyQuest", "SyJet-S", SYJET_S_REV, RDISK_MEDIUM_SYJET_1_5GB, RDISK_DRIVE_TYPE_SCSI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 } },
    /* The SparQ only ever shipped as an IDE/EIDE (and external parallel-port) drive, hence ATAPI_LPT; rdisk.c
       still has SCSI mode pages for it.  Its INQUIRY revision is a placeholder: the real drive (Aaru report
       175, native ATA, firmware "SA_0043") has no INQUIRY, and rdisk.c reports that firmware in IDENTIFY. */
    { "SyQuest", "SparQ", "1.03", RDISK_MEDIUM_SPARQ_1GB, RDISK_DRIVE_TYPE_ATAPI_LPT, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 } },
    /* ---- Zip ---- */
    { "IOMEGA", "ZIP 100", "E.08", RDISK_MEDIUM_ZIP_100, RDISK_DRIVE_TYPE_SCSI_ATAPI_LPT, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 } },
    /* The Zip 100 Plus: one external drive with a SCSI and a parallel port, so it has no ATAPI.  Tools 5.51
       (GetDevTypeSpecific() in SCSIDLL.DLL) tell it from the Zip 100 by its product, "ZIP 100 PLUS".  Everything else
       is the Zip 100's.  Firmware J.66, SCSI-2, 196,608 sectors, from a real drive on the parallel port in the Linux
       ZIP-Drive HOWTO; there it needs the imm driver, not the ppa driver of the parallel Zip 100. */
    { "IOMEGA", "ZIP 100 PLUS", "J.66", RDISK_MEDIUM_ZIP_100, RDISK_DRIVE_TYPE_SCSI_LPT, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 } },
    { "IOMEGA", "ZIP 250", "42.S", RDISK_MEDIUM_ZIP_250, RDISK_DRIVE_TYPE_SCSI_ATAPI_LPT, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0 } },
    /* Compatibility: reads and writes 750 and 250 MB media, reads (read-only) 100 MB media; rdisk.c makes
       the Zip 100 medium read-only in it.  The product is "ZIP 750" in ioReady.dll of IomegaWare 4.01, which
       also offers the short format on 750 and 250 MB disks and the long format only on 750 MB ones. */
    { "IOMEGA", "ZIP 750", ZIP_750_REV, RDISK_MEDIUM_ZIP_750, RDISK_DRIVE_TYPE_ATAPI, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1 } }
};

/* The number that a drive type has in a configuration file.  RDISK_TYPE_* are in the order of the table above, so these
   keep the numbers of existing files the same: a new type gets the next number, whichever its place in the table. */
static const uint8_t rdisk_type_config_ids[KNOWN_RDISK_DRIVE_TYPES] = {
     0, /* GENERIC */
    25, /* BERNOULLI_20 */
    26, /* BERNOULLI_44 */
    13, /* BERNOULLI_90 */
    14, /* BERNOULLI_150 */
    29, /* BERNOULLI_150_IDE */
    15, /* BERNOULLI_230 */
    20, /* UHD144 */
    16, /* ORB_2_2GB */
     9, /* CLIK_40 */
    12, /* FLOPTICAL_20 */
    30, /* FLOPTICAL_IO20S */
    11, /* HIFD_200 */
     3, /* JAZ_1GB */
     4, /* JAZ_2GB */
    39, /* PEERLESS */
    40, /* REV_35 */
    41, /* REV_70 */
    42, /* REV_120 */
     7, /* SUPERDISK_120 */
    28, /* SUPERDISK_120_COSM04_MATSHITA */
    22, /* SUPERDISK_120_VER5 */
    43, /* SUPERDISK_120_VER5_COMPAQ */
    44, /* SUPERDISK_120_HIMA_COMPAQ */
    23, /* SUPERDISK_120_IMATION */
     8, /* SUPERDISK_240 */
    24, /* SUPERDISK_240_IMATION */
    38, /* SQ555 */
    37, /* SQ5110C */
    18, /* SQ3105S */
    33, /* SQ3105A */
    36, /* SQ3270S */
    34, /* SQ3270A */
    17, /* SQ5200C */
    35, /* EZ135S */
    31, /* EZ135A */
    19, /* EZ230S */
    32, /* EZ230A */
     5, /* SYJET_1_5GB */
    21, /* SYJET_S */
     6, /* SPARQ_1GB */
     1, /* ZIP_100 */
    27, /* ZIP_100_PLUS */
     2, /* ZIP_250 */
    10, /* ZIP_750 */
};

static inline uint32_t
rdisk_type_to_config(const uint32_t type)
{
    return (type < KNOWN_RDISK_DRIVE_TYPES) ? rdisk_type_config_ids[type] : 0;
}

static inline uint32_t
rdisk_type_from_config(const uint32_t id)
{
    for (uint32_t i = 0; i < KNOWN_RDISK_DRIVE_TYPES; i++) {
        if (rdisk_type_config_ids[i] == id)
            return i;
    }

    return RDISK_TYPE_GENERIC;
}

/* Does this drive type belong on this RDISK_BUS_* bus, according to its drive_type? */
static inline int
rdisk_drive_type_supports_bus(const int drive_type, const uint8_t bus_type)
{
    switch (drive_type) {
        case RDISK_DRIVE_TYPE_NONE:
            return 0;
        case RDISK_DRIVE_TYPE_SCSI_ATAPI:
            return (bus_type == RDISK_BUS_SCSI) || (bus_type == RDISK_BUS_ATAPI);
        case RDISK_DRIVE_TYPE_ATAPI_LPT:
            return (bus_type == RDISK_BUS_ATAPI) || (bus_type == RDISK_BUS_LPT);
        case RDISK_DRIVE_TYPE_SCSI_LPT:
            return (bus_type == RDISK_BUS_SCSI) || (bus_type == RDISK_BUS_LPT);
        case RDISK_DRIVE_TYPE_IDE_ATAPI:
            return (bus_type == RDISK_BUS_IDE) || (bus_type == RDISK_BUS_ATAPI);
        case RDISK_DRIVE_TYPE_SCSI_ATAPI_LPT:
            return (bus_type == RDISK_BUS_SCSI) || (bus_type == RDISK_BUS_ATAPI) ||
                   (bus_type == RDISK_BUS_LPT);
        default:
            return drive_type == (int) bus_type;
    }
}

typedef struct rdisk_drive_t {
    uint8_t            id;

    union {
        uint8_t            res;
        /* Reserved for other ID's. */
        uint8_t            res0;
        uint8_t            res1;
        uint8_t            ide_channel;
        uint8_t            scsi_device_id;
    };

    uint8_t            bus_type;  /* 0 = ATAPI, 1 = SCSI */
    uint8_t            bus_mode;  /* Bit 0 = PIO suported;
                                     Bit 1 = DMA supportd. */
    uint8_t            read_only; /* Struct variable reserved for
                                     media status. */
    uint8_t            lpt_bridge; /* bus LPT: the bridge, HDD_LPT_*; the
                                      EPAT, or the ZIP's own chip */
    uint8_t            pad0;

    FILE              *fp;
    void              *priv;

    char               image_path[MAX_IMAGE_PATH_LEN];
    char               prev_image_path[MAX_IMAGE_PATH_LEN + 256];

    char              *image_history[RDISK_IMAGE_HISTORY];

    uint32_t           type;
    uint32_t           medium_size;
    uint32_t           base;

    uint32_t           medium_type;
} rdisk_drive_t;

typedef struct rdisk_t {
    mode_sense_pages_t ms_pages_saved;

    rdisk_drive_t       *drv;
#ifdef EMU_IDE_H
    ide_tf_t          *tf;
#else
    void              *tf;
#endif

    void              *log;

    uint8_t           *buffer;
    size_t             buffer_sz;
    uint8_t            atapi_cdb[16];
    uint8_t            current_cdb[16];
    uint8_t            sense[256];

    uint8_t            id;
    uint8_t            cur_lun;
    uint8_t            pad0;
    uint8_t            pad1;

    uint16_t           max_transfer_len;
    uint16_t           pad2;

    int                requested_blocks;
    int                packet_status;
    int                total_length;
    int                do_page_save;
    int                unit_attention;
    int                request_pos;
    int                old_len;
    int                transition;

    uint32_t           sector_pos;
    uint32_t           sector_len;
    uint32_t           packet_len;
    uint32_t           block_len;

    double             callback;

    uint8_t            (*ven_cmd)(void *sc, uint8_t *cdb, int32_t *BufLen);

    /* Everything above mirrors scsi_common_t, which the IDE and parallel port code see the drive as: new fields go
       below. */
    uint8_t            prevent;    /* PREVENT MEDIUM REMOVAL is in effect. */
    uint8_t            floptical_unlocked; /* A Floptical has had its MODE SENSE of page 2E since power-up. */
    uint8_t            protect_mode;    /* Zip and Jaz protection mode, see rdisk_set_protection(). */
    uint8_t            protect_password_len;
    uint8_t            protect_password[32];

    /* A drive on RDISK_BUS_IDE. */
    uint8_t            ata_mc;        /* a cartridge change to report as MC */
    uint8_t            ata_locked;    /* door locked (MEDIA LOCK) */
    uint8_t            soft_wp;       /* the cartridge's software write protection (C0h) */
} rdisk_t;

extern rdisk_t      *rdisk[RDISK_NUM];
extern rdisk_drive_t rdisk_drives[RDISK_NUM];
extern uint8_t     atapi_rdisk_drives[8];
extern uint8_t     scsi_rdisk_drives[16];

#define rdisk_sense_error dev->sense[0]
#define rdisk_sense_key   dev->sense[2]
#define rdisk_info        *(uint32_t *) &(dev->sense[3])
#define rdisk_asc         dev->sense[12]
#define rdisk_ascq        dev->sense[13]

#ifdef __cplusplus
extern "C" {
#endif

extern void rdisk_disk_close(const rdisk_t *dev);
extern void rdisk_disk_reload(const rdisk_t *dev);
extern void rdisk_insert(rdisk_t *dev);

extern void rdisk_global_init(void);
extern void rdisk_hard_reset(void);

extern void rdisk_reset(scsi_common_t *sc);
extern int  rdisk_is_empty(const uint8_t id);
extern void rdisk_load(const rdisk_t *dev, const char *fn, const int skip_insert);
extern void rdisk_close(void);
#ifdef SCSI_DEVICE_H
extern scsi_device_t *rdisk_get_lpt_device(const uint8_t port);
#endif

#ifdef __cplusplus
}
#endif

#endif /*EMU_RDISK_H*/
