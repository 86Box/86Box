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
 *          Copyright 2018-2026 Miran Grca.
 *          Copyright 2022-2026 Jasmine Iwanek.
 */
#ifdef ENABLE_RDISK_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/log.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/nvr.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/hdc_ide.h>
#include <86box/hdd.h>
#include <86box/lpt.h>
#include <86box/rdisk.h>
#include <86box/version.h>

#define IDE_ATAPI_IS_EARLY             id->sc->pad0

rdisk_drive_t rdisk_drives[RDISK_NUM];

/* Drives reached through a parallel-port bridge, indexed by LPT port. */
static scsi_device_t lpt_rdisk_devices[PARALLEL_MAX];

/*
 * A Zip drive on the parallel port is a SCSI Zip drive behind the
 * parallel-to-SCSI chip that Iomega built into it (PPA3 in the Zip 100, IMM in
 * the Zip Plus and the Zip 250): the host talks SCSI to it, not ATAPI. Every
 * other drive on the parallel port (the SuperDisk behind the Shuttle EPAT) is
 * an ATAPI drive.
 */
static int
rdisk_is_scsi(const rdisk_t *dev)
{
    return (dev->drv->bus_type == RDISK_BUS_SCSI) ||
           ((dev->drv->bus_type == RDISK_BUS_LPT) &&
            ((dev->drv->type == RDISK_TYPE_ZIP_100) || (dev->drv->type == RDISK_TYPE_ZIP_100_PLUS) ||
             (dev->drv->type == RDISK_TYPE_ZIP_250)));
}

/*
 * An ATAPI drive on an IDE channel: on the IDE bus, or on the IDE board of a
 * parallel port bridge other than the EPAT (whose model drives the drive
 * itself, as the ZIP's chips do).
 */
static int
rdisk_on_ide(const rdisk_drive_t *drv)
{
    return (drv->bus_type == RDISK_BUS_ATAPI) ||
           ((drv->bus_type == RDISK_BUS_LPT) && (drv->lpt_bridge != HDD_LPT_EPAT));
}

static int
rdisk_ide_channel(const rdisk_drive_t *drv)
{
    return (drv->bus_type == RDISK_BUS_LPT) ? ide_lpt_channel(drv->res & (PARALLEL_MAX - 1)) :
                                              drv->ide_channel;
}

/* The SCSI device entry of a drive that talks SCSI. */
static scsi_device_t *
rdisk_scsi_device(const rdisk_t *dev)
{
    if (dev->drv->bus_type == RDISK_BUS_LPT)
        return &lpt_rdisk_devices[dev->drv->res & (PARALLEL_MAX - 1)];

    return &scsi_devices[(dev->drv->scsi_device_id >> 4) & 0x0f][dev->drv->scsi_device_id & 0x0f];
}

// clang-format off
/*
   Table of all SCSI commands and their flags, needed for the new disc change /
   not ready handler.

   LS-120 note (implementation specification, sections 3 and 7.2): MKELS120's function 42h media-status
   operation issues a vendor-specific 12-byte CDB with opcode 24h.  Its device-side meaning (and its
   data phase) has not been established, so it is deliberately NOT implemented here: it fails as an
   illegal opcode, and the CDB is in the log (see rdisk_command()) for anyone capturing it.  Do not
   give it a guessed standard SCSI meaning.
 */
const uint8_t rdisk_command_flags[0x100] = {
    [0x00]          = IMPLEMENTED | CHECK_READY,
    [0x01]          = IMPLEMENTED | ALLOW_UA | SCSI_ONLY,
    [0x03]          = IMPLEMENTED | ALLOW_UA,
    [0x04]          = IMPLEMENTED | CHECK_READY | ALLOW_UA | SCSI_ONLY,
    [0x06]          = IMPLEMENTED,
    [0x08]          = IMPLEMENTED | CHECK_READY,
    [0x0a ... 0x0b] = IMPLEMENTED | CHECK_READY,
    [0x0c]          = IMPLEMENTED,
    [0x0d]          = IMPLEMENTED | ATAPI_ONLY,
    [0x12]          = IMPLEMENTED | ALLOW_UA,
    [0x13]          = IMPLEMENTED | CHECK_READY,
    [0x15]          = IMPLEMENTED,
    [0x16 ... 0x17] = IMPLEMENTED | SCSI_ONLY,
    [0x1a]          = IMPLEMENTED,
    [0x1b]          = IMPLEMENTED | CHECK_READY,
    [0x1d]          = IMPLEMENTED,
    [0x1e]          = IMPLEMENTED | CHECK_READY,
    [0x23]          = IMPLEMENTED | ATAPI_ONLY,
    [0x25]          = IMPLEMENTED | CHECK_READY,
    [0x28]          = IMPLEMENTED | CHECK_READY,
    [0x2a ... 0x2b] = IMPLEMENTED | CHECK_READY,
    [0x2e ... 0x2f] = IMPLEMENTED | CHECK_READY,
    [0x41]          = IMPLEMENTED | CHECK_READY,
    [0x55]          = IMPLEMENTED,
    [0x5a]          = IMPLEMENTED,
    [0xa8]          = IMPLEMENTED | CHECK_READY,
    [0xaa]          = IMPLEMENTED | CHECK_READY,
    [0xae]          = IMPLEMENTED | CHECK_READY,
    [0xaf]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0xbd]          = IMPLEMENTED
};

/*
   The Bernoulli MultiDisk 150 (firmware T.51) has its own command set, from the dispatch table of its ROM:
   only the opcodes up to 3Fh (and a vendor E3h) exist, so MODE SENSE (10) and MODE SELECT (10) are illegal.
   CHECK_READY follows the firmware's "needs a medium" flag.  START/STOP UNIT, PREVENT MEDIUM REMOVAL and
   READ CAPACITY do not have that flag and do their own checks in rdisk_command().

   Commands of the real drive that are not emulated, and so fail as illegal opcodes: REASSIGN BLOCKS (07h),
   RECEIVE DIAGNOSTIC (1Ch), READ DEFECT DATA (37h), WRITE/READ BUFFER (3Bh, 3Ch) and READ/WRITE LONG (3Eh,
   3Fh).  Opcodes 05h, 0Dh, 0Eh, 20h, 21h, 24h, 40h and E3h are illegal on the real drive too unless bit 5 of
   byte 2 of mode page 2F is set; that unlock is not emulated.
 */
static const uint8_t bernoulli_150_command_flags[0x100] = {
    [0x00]          = IMPLEMENTED | CHECK_READY,
    [0x01]          = IMPLEMENTED | SCSI_ONLY,
    [0x03]          = IMPLEMENTED | ALLOW_UA,
    [0x04]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0x06]          = IMPLEMENTED,
    [0x08]          = IMPLEMENTED | CHECK_READY,
    [0x0a ... 0x0b] = IMPLEMENTED | CHECK_READY,
    [0x12]          = IMPLEMENTED | ALLOW_UA,
    [0x15]          = IMPLEMENTED,
    [0x16 ... 0x17] = IMPLEMENTED | SCSI_ONLY,
    [0x1a ... 0x1b] = IMPLEMENTED,
    [0x1d ... 0x1e] = IMPLEMENTED,
    [0x25]          = IMPLEMENTED,
    [0x28]          = IMPLEMENTED | CHECK_READY,
    [0x2a ... 0x2b] = IMPLEMENTED | CHECK_READY,
    [0x2e ... 0x2f] = IMPLEMENTED | CHECK_READY,
    [0x34]          = IMPLEMENTED | CHECK_READY,    /* PRE-FETCH */
    [0x35]          = IMPLEMENTED,                  /* SYNCHRONIZE CACHE */
    [0x36]          = IMPLEMENTED | CHECK_READY     /* LOCK/UNLOCK CACHE */
};

/* Which CDB bits the same firmware requires to be zero, for bytes 1 to 4 of a 6-byte CDB and bytes 1 to 8 of a
   10-byte one; a set bit fails with INVALID FIELD IN CDB.  The LUN field of byte 1 is not included. */
typedef struct rdisk_cdb_mask_t {
    uint8_t op;
    uint8_t mask[8];
} rdisk_cdb_mask_t;

static const rdisk_cdb_mask_t bernoulli_150_cdb_masks[] = {
    { 0x00, { 0xff, 0xff, 0xff, 0xff } },
    { 0x01, { 0xff, 0xff, 0xff, 0xff } },
    { 0x03, { 0xff, 0xff, 0xff, 0x00 } },
    { 0x04, { 0xe0, 0x40, 0xff, 0xfe } },
    { 0x06, { 0xff, 0x00, 0xff, 0x00 } },
    { 0x08, { 0xe0, 0x00, 0x00, 0x00 } },
    { 0x0a, { 0xe0, 0x00, 0x00, 0x00 } },
    { 0x0b, { 0xe0, 0x00, 0x00, 0xff } },
    { 0x12, { 0xff, 0xff, 0xff, 0x00 } },
    { 0x15, { 0xef, 0xff, 0xff, 0x00 } },
    { 0x16, { 0xe0, 0x00, 0x00, 0x00 } },
    { 0x17, { 0xe0, 0x00, 0xff, 0xff } },
    { 0x1a, { 0xf7, 0x00, 0xff, 0x00 } },
    { 0x1b, { 0xfe, 0xff, 0xff, 0xfc } },
    { 0x1d, { 0xe8, 0xff, 0x00, 0x00 } },
    { 0x1e, { 0xff, 0xff, 0xff, 0xfe } },
    { 0x25, { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xfe } },
    { 0x28, { 0xe7, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00 } },
    { 0x2a, { 0xe7, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00 } },
    { 0x2b, { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff } },
    { 0x2e, { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00 } },
    { 0x2f, { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00 } },
    { 0x34, { 0xfd, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00 } },
    { 0x35, { 0xfd, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00 } },
    { 0x36, { 0xfd, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00 } }
};

#define IS_ZIP_100(t) (((t) == RDISK_TYPE_ZIP_100) || ((t) == RDISK_TYPE_ZIP_100_PLUS))
#define IS_ZIP(t)     (IS_ZIP_100(t) || ((t) == RDISK_TYPE_ZIP_250) || ((t) == RDISK_TYPE_ZIP_750))
/* Each model of a family has its own drive type (and its own row in rdisk_drive_types[]). */
#define IS_SUPERDISK_120(t) (((t) == RDISK_TYPE_SUPERDISK_120) || ((t) == RDISK_TYPE_SUPERDISK_120_VER5) || \
                             ((t) == RDISK_TYPE_SUPERDISK_120_IMATION) || \
                             ((t) == RDISK_TYPE_SUPERDISK_120_COSM04_MATSHITA) || \
                             ((t) == RDISK_TYPE_SUPERDISK_120_VER5_COMPAQ) || \
                             ((t) == RDISK_TYPE_SUPERDISK_120_HIMA_COMPAQ))
#define IS_SUPERDISK_240(t) (((t) == RDISK_TYPE_SUPERDISK_240) || ((t) == RDISK_TYPE_SUPERDISK_240_IMATION))
#define IS_SUPERDISK(t)     (IS_SUPERDISK_120(t) || IS_SUPERDISK_240(t))
#define IS_REV(t)           (((t) == RDISK_TYPE_REV_35) || ((t) == RDISK_TYPE_REV_70) || ((t) == RDISK_TYPE_REV_120))
#define IS_SYJET(t)         (((t) == RDISK_TYPE_SYJET_1_5GB) || ((t) == RDISK_TYPE_SYJET_S))
/* The LS-120 COSM 05 that the ATAPI captures come from: what only that model is known to do is keyed to it.  Its
   MATSHITA COSM 04 is a clone of it. */
#define IS_LS120_COSM05(t)  (((t) == RDISK_TYPE_SUPERDISK_120) || ((t) == RDISK_TYPE_SUPERDISK_120_COSM04_MATSHITA))
/* The Bernoulli MultiDisk 150 whose ROM (firmware T.51) was dumped, and the Insider 150 IDE that is treated as one. */
#define IS_BERNOULLI_150(t) (((t) == RDISK_TYPE_BERNOULLI_150) || ((t) == RDISK_TYPE_BERNOULLI_150_IDE))
#define IS_FLOPTICAL(t)     (((t) == RDISK_TYPE_FLOPTICAL_20) || ((t) == RDISK_TYPE_FLOPTICAL_IO20S))
#define IS_SYQUEST_IDE(t)   (((t) == RDISK_TYPE_EZ135A) || ((t) == RDISK_TYPE_EZ230A) || \
                             ((t) == RDISK_TYPE_SQ3105A) || ((t) == RDISK_TYPE_SQ3270A))
/* What Iomega's Tools treat alike: IsProtectable() in SCSIDLL.DLL is true for these two families. */
#define IS_ZIP_OR_JAZ(t)    (IS_ZIP(t) || ((t) == RDISK_TYPE_JAZ_1GB) || ((t) == RDISK_TYPE_JAZ_2GB))
/* The drives with Iomega's disk protection: opcode 0Ch sets it and byte 15h of page 2 of opcode 06h reports it. */
#define HAS_PROTECTION(t)   (IS_ZIP_OR_JAZ(t) || IS_REV(t))

/* SENSE KEY: DATA PROTECT.  The LS-120 reports write-protected media with it (MKELS120 maps it, with
   ASC 27h, to its write-protect result; see rdisk_write_protected()). */
#define RDISK_SENSE_DATA_PROTECT 0x07

/* Commands that have no GPCMD_* constant. */
#define RDISK_CMD_ATAPI_FORMAT_UNIT 0x24    /* What the Iomega ATAPI driver turns FORMAT UNIT into */
#define RDISK_CMD_PRE_FETCH         0x34
#define RDISK_CMD_LOCK_UNLOCK_CACHE 0x36
#define RDISK_CMD_READ_DEFECT_DATA  0x37
#define RDISK_CMD_CALEB_VENDOR      0xf0    /* Write protection and firmware, see rdisk_command() */

/* The Bernoulli 150 returns 25 bytes of sense data. */
#define RDISK_BERNOULLI_150_SENSE_LEN   25
#define RDISK_BERNOULLI_II_SENSE_LEN    27    /* The Bernoulli 20, 44 and 90: bytes 17h to 1Ah, see below */
/* The Tools show spares left, of those two counts, as Format Life; a count that a disk with nothing used up has. */
#define RDISK_FORMAT_LIFE_COUNT         1000

/* Additional sense codes that are only used here. */
#define RDISK_ASC_DEFECT_LIST_NOT_FOUND 0x1c    /* The Bernoulli 150's READ CAPACITY without a cartridge */
#define RDISK_ASC_NOT_READY             0x04    /* With ASCQ 2, a Floptical before its unlock: see the Floptical pages */
#define RDISK_ASC_SAVING_NOT_SUPPORTED  0x39    /* The Bernoulli 150's MODE SENSE of saved values */
#define IS_BERNOULLI_II(t) (((t) == RDISK_TYPE_BERNOULLI_20) || ((t) == RDISK_TYPE_BERNOULLI_44))
/* The drives whose REQUEST SENSE has the counts of Format Life, not their Iomega sense page 2. */
#define IS_BERNOULLI_LIFE_SENSE(t) (IS_BERNOULLI_II(t) || ((t) == RDISK_TYPE_BERNOULLI_90))
#define IS_BERNOULLI(t)    (((t) == RDISK_TYPE_BERNOULLI_90) || IS_BERNOULLI_150(t) || \
                            ((t) == RDISK_TYPE_BERNOULLI_230) || IS_BERNOULLI_II(t))

/* Drive types that only belong on the SCSI bus (drive_type in rdisk.h: the Floptical and the Bernoulli
   Box II drives) are the older narrow (8-bit) SCSI drives. */
#define IS_NARROW_SCSI_ONLY(t) ((((t) < KNOWN_RDISK_DRIVE_TYPES) && \
                                 (rdisk_drive_types[(t)].drive_type == RDISK_DRIVE_TYPE_SCSI)) || \
                                ((t) == RDISK_TYPE_ORB_2_2GB) ||  /* The ORB2-SE is a narrow SCSI drive too. */ \
                                ((t) == RDISK_TYPE_ZIP_100_PLUS))

static uint64_t zip_100_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE |
                                                 GPMODEP_IOMEGA_PAGE | GPMODEP_ALL_PAGES);
static uint64_t zip_250_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_FLEXIBLE_DISK_PAGE |
                                                 GPMODEP_CACHING_PAGE | GPMODEP_IOMEGA_PAGE |
                                                 GPMODEP_ALL_PAGES);
/* SuperDisk drives are not Iomega drives, so unlike the Zip 250 they do NOT
   get the Iomega vendor page (0x2f) - Iomega's drivers use it to recognize a Zip. */
/* Jaz drives have no flexible disk page (unlike the ATAPI Zip 250): a real Jaz 1GB (Aaru report 49)
   reports pages 1, 2, 8 and the Iomega page 0x2f only. */
static uint64_t jaz_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE |
                                             GPMODEP_CACHING_PAGE | GPMODEP_IOMEGA_PAGE |
                                             GPMODEP_ALL_PAGES);

/* ESTIMATED: no real Jaz 2GB report exists, the Jaz 1GB's page set is assumed. */
static uint64_t jaz_2gb_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE |
                                                 GPMODEP_CACHING_PAGE | GPMODEP_IOMEGA_PAGE |
                                                 GPMODEP_ALL_PAGES);
/* Clik! 40: a real drive (Aaru report 50) reports only page 1 and the Iomega page 0x2f. */
static uint64_t clik_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_IOMEGA_PAGE |
                                              GPMODEP_ALL_PAGES);
/* The Zip 750 reports the same page set as the Zip 250 (Aaru report 53), so it uses
   zip_250_mode_sense_page_flags and zip_250_mode_sense_pages_changeable. */

/*
   The Iomega mode page 2F, from Iomega's own software (Tools 5.4: SCSIDLL.DLL and imgatapi.sys) and the ROM of a
   Bernoulli MultiDisk 150:

     - Byte 2: bit 5 unlocks the vendor commands.  The Tools set it with MODE SELECT, issue opcode 0Dh (SetJazSpeed(),
       GetHalfSpeenState()) and clear it again, and the MultiDisk 150 rejects those opcodes while it is clear.  The
       other bits are not known.
     - Byte 3: the sleep timer, the one byte that SetSleepTimer() changes.  0 turns it off.  It is in minutes: the
       default of the MultiDisk 150 is 1Eh, and the OS/2 drivers offer "/T30" (the default), "/T15" and "/T0" (never).
     - Byte 4: a delay in seconds that has a floor per recording format on the MultiDisk 150 (ESTIMATED: the unit
       of byte 3 is 61 of its unit).  The OS/2 drivers set it with "/D".
     - Byte 5: low 4 bits only.  The MultiDisk 150 writes it to a register of its drive electronics.

   SetSleepTimer() and the OS/2 drivers read the page with MODE SENSE (6) and take it from offset 12, so they rely
   on the block descriptor being there, and write it back with a MODE SELECT (6) of the header and the page, 10 bytes
   with PF set, which the OS/2 drivers work out as the mode data length less 7.  SCSIDLL.DLL and Windows NT find out
   that a medium is write-protected from bit 7 of the device-specific parameter in the header of this MODE SENSE.
 */

/* Mode page numbers that have no GPMODE_* constant. */
#define MS_PAGE_RIGID_DISK       0x04
#define MS_PAGE_VERIFY_ERROR     0x07
#define MS_PAGE_MEDIUM_TYPES     0x0b
#define MS_PAGE_RBAC             0x1b
#define MS_PAGE_INFO_EXCEPTIONS  0x1c
#define MS_PAGE_CONTROL          0x0a
#define MS_PAGE_SYQUEST          0x20

/* Sony HiFD (Aaru report 118): pages 1, 3, 5, 7, 8, 0B, 1B and 1C. */
static uint64_t hifd_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_FORMAT_DEVICE_PAGE |
                                              GPMODEP_FLEXIBLE_DISK_PAGE | (1ULL << MS_PAGE_VERIFY_ERROR) |
                                              GPMODEP_CACHING_PAGE | (1ULL << MS_PAGE_MEDIUM_TYPES) |
                                              (1ULL << MS_PAGE_RBAC) | (1ULL << MS_PAGE_INFO_EXCEPTIONS) |
                                              GPMODEP_ALL_PAGES);
/* Floptical (Aaru report 258): pages 1, 3 and 4 are implemented here; 5, 0B and 2E also exist on the drive
   but their contents are not decoded in the report. */
#define MS_PAGE_FLOPTICAL        0x2e
static uint64_t floptical_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_FORMAT_DEVICE_PAGE |
                                                   (1ULL << 0x04) | (1ULL << MS_PAGE_FLOPTICAL) | GPMODEP_ALL_PAGES);
/* Bernoulli Box II 90 (report 186): pages 1, 2, 3, 4 and 2F. */
static uint64_t bernoulli_90_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE |
                                                      GPMODEP_FORMAT_DEVICE_PAGE | (1ULL << MS_PAGE_RIGID_DISK) |
                                                      GPMODEP_IOMEGA_PAGE | GPMODEP_ALL_PAGES);
/* Bernoulli Box II 44: pages 1, 2 and 2F.  Its geometry is not known, so it has no pages 3 and 4. */
static uint64_t bernoulli_44_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE |
                                                      GPMODEP_IOMEGA_PAGE | GPMODEP_ALL_PAGES);
/* Bernoulli Box II 20: the same without page 2F, see the description of the Bernoulli pages. */
static uint64_t bernoulli_20_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE |
                                                      GPMODEP_ALL_PAGES);
/* Bernoulli 150 (ROM, firmware T.51): pages 1, 2, 8 and the Iomega page 2F.  Page 0 is header-only, see
   rdisk_mode_sense(). */
static uint64_t bernoulli_150_mode_sense_page_flags = (GPMODEP_UNIT_ATN_PAGE | GPMODEP_R_W_ERROR_PAGE |
                                                       GPMODEP_DISCONNECT_PAGE | GPMODEP_CACHING_PAGE |
                                                       GPMODEP_IOMEGA_PAGE | GPMODEP_ALL_PAGES);
/* Bernoulli 230 (report 311): pages 1, 2, 8 and 2F. */
static uint64_t bernoulli_230_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE |
                                                       GPMODEP_CACHING_PAGE | GPMODEP_IOMEGA_PAGE | GPMODEP_ALL_PAGES);

#define SUPERDISK_MEDIUM_TYPES_PAGE 0x0b    /* Medium Types Supported */
#define SUPERDISK_RBAC_PAGE         0x1b    /* Removable Block Access Capabilities */
/* The real LS-120 (Aaru report 66) has page 3, the real LS-240 (report 67) has page 0x1b instead. */
static uint64_t superdisk_120_mode_sense_page_flags = (GPMODEP_UNIT_ATN_PAGE | GPMODEP_R_W_ERROR_PAGE | GPMODEP_FORMAT_DEVICE_PAGE |
                                                       GPMODEP_FLEXIBLE_DISK_PAGE | GPMODEP_CACHING_PAGE |
                                                       (1ULL << SUPERDISK_MEDIUM_TYPES_PAGE) |
                                                       GPMODEP_ALL_PAGES);
static uint64_t superdisk_240_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_FLEXIBLE_DISK_PAGE |
                                                       GPMODEP_CACHING_PAGE |
                                                       (1ULL << SUPERDISK_MEDIUM_TYPES_PAGE) |
                                                       (1ULL << SUPERDISK_RBAC_PAGE) | GPMODEP_ALL_PAGES);
/* Unlike the Iomega-made Zip/Jaz drives above, the SyJet is a SyQuest drive and
   its SCSI Technical Reference documents no Iomega-style vendor page or vendor
   EJECT command, so it gets its own (non-Iomega) standard SCSI-2 page set. */
static uint64_t syjet_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_FORMAT_DEVICE_PAGE |
                                                GPMODEP_RIGID_DISK_PAGE | GPMODEP_ALL_PAGES);
/* The SparQ never shipped as a real SCSI drive at all (IDE/EIDE and parallel
   port only) - this page set exists only so 86Box's generic rdisk framework
   behaves sensibly if a SparQ is attached to a SCSI bus, and mirrors SyJet's
   page set since both are SyQuest drives of the same generation. */
static uint64_t sparq_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_FORMAT_DEVICE_PAGE |
                                                GPMODEP_RIGID_DISK_PAGE | GPMODEP_ALL_PAGES);

static const mode_sense_pages_t zip_100_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xc8, 0x16, 0x00, 0x00, 0x00, 0x00,
                 0x5a,                                0x00, 0x50, 0x20                         },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x0f, 0xff, 0x0f             } }
};

static const mode_sense_pages_t zip_250_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xc8, 0x64, 0x00, 0x00, 0x00, 0x00 },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,           0x1e, 0x80, 0x00, 0x40, 0x20, 0x02, 0x00,
                 0x00,                                0xef, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x0b, 0x7d, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0xff, 0xff, 0x00, 0x00,
                 0xff,                                0xff, 0xff, 0xff                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x0f, 0x3c, 0x0f             } }
};

/* SCSI Zip 100, from sginfo output of a real SCSI Zip 100 (firmware D.06) in Gough's "Experiments:
   ZIP Disk Defect Lists, Mode Page Changes, Protected Cartridges" (2015): read retries 22, write
   retries 49, recovery time limit 12320 ms (0x3020), fair arbitration 2 (byte 12 = 0x20), first burst
   size 8224 (0x2020), page 2F 5c 0f b4 05.  (The ATAPI table keeps the values the ATAPI drive
   there reported: write retries 90, recovery time 20512 ms.) */
static const mode_sense_pages_t zip_100_mode_sense_pages_default_scsi = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xc8, 0x16, 0x00, 0x00, 0x00, 0x00,
                 0x31,                                0x00, 0x30, 0x20                         },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x20, 0x00, 0x20, 0x20 },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x0f, 0xb4, 0x05             } }
};

static const mode_sense_pages_t zip_250_mode_sense_pages_default_scsi = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xc8, 0x64, 0x00, 0x00, 0x00, 0x00 },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,           0x1e, 0x80, 0x00, 0x40, 0x20, 0x02, 0x00,
                 0x00,                                0xef, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x0b, 0x7d, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0xff, 0xff, 0x00, 0x00,
                 0xff,                                0xff, 0xff, 0xff                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x0f, 0x3c, 0x0f             } }
};

static const mode_sense_pages_t zip_100_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff,
                 0x5a,                                0xff, 0xff, 0xff                         },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0xff, 0xff, 0xff, 0xff             } }
};

static const mode_sense_pages_t zip_250_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,           0x1e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0xff, 0xff, 0x00, 0x00,
                 0xff,                                0xff, 0xff, 0xff                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0xff, 0xff, 0xff, 0xff             } }
};

/*
   Jaz mode-sense pages, from Aaru device report 49 (https://aaru.app/report/49), a real SCSI
   iomega jaz 1GB, firmware H.72.  All three standard pages have the PS (parameters saveable) bit set.
     - Page 1:  AWRE and ARRE enabled, read retries 100, write retries 100.  Unlike the Zip
                drives the "most expedient recovery first" (EER) flag is not set.
     - Page 2:  buffer full ratio 32, buffer empty ratio 32, no data transfer disconnect control.
                The limit fields were not reported and are left at 0.
     - Page 8:  write cache enabled, no multiple selection, pre-fetch up to 65535 blocks (same
                bytes as the Zip 250's caching page apart from PS).
     - Page 2F: the report only says "unknown page"; the Zip 250's bytes are reused and are
                NOT confirmed for the Jaz.
   There is no report for an ATAPI Jaz: these tables are used for both buses.  The changeable
   masks are placeholders.  The Jaz 2GB has its own (estimated) tables below.
 */
static const mode_sense_pages_t jaz_mode_sense_pages_default = {
    { [0x01] = { 0x80 | GPMODE_R_W_ERROR_PAGE,        0x0a, 0xc0, 0x64, 0x00, 0x00, 0x00, 0x00,
                 0x64,                                0x00, 0x00, 0x00                         },
      [0x02] = { 0x80 | GPMODE_DISCONNECT_PAGE,       0x0e, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { 0x80 | GPMODE_CACHING_PAGE,          0x0a, 0x04, 0x00, 0xff, 0xff, 0x00, 0x00,
                 0xff,                                0xff, 0xff, 0xff                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x0f, 0x3c, 0x0f             } }
};

static const mode_sense_pages_t jaz_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                 0xff,                                0x00, 0x00, 0x00                         },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0xff, 0xff, 0x00, 0x00,
                 0xff,                                0xff, 0xff, 0xff                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0xff, 0xff, 0xff, 0xff             } }
};

/*
   Jaz 2GB mode-sense pages - ESTIMATED.  No real Jaz 2GB report exists.  The values are the Jaz 1GB's
   (Aaru report 49): the two drives are the same family and share the same SCSI-2 firmware lineage
   (the existing INQUIRY data is "jaz 1GB" H.72 vs. "jaz 2GB" E.17), so the same pages are assumed:
   pages 1, 2, 8 and 2F, PS bit set, no flexible disk page.  The only real difference one would expect
   is in values that were never reported (retry counts, page 2 limits, page 2F bytes), which stay at
   the 1GB's / the Zip 250's values.  Kept as a separate table so it can be corrected on its own.
 */
static const mode_sense_pages_t jaz_2gb_mode_sense_pages_default = {
    { [0x01] = { 0x80 | GPMODE_R_W_ERROR_PAGE,        0x0a, 0xc0, 0x64, 0x00, 0x00, 0x00, 0x00,
                 0x64,                                0x00, 0x00, 0x00                         },
      [0x02] = { 0x80 | GPMODE_DISCONNECT_PAGE,       0x0e, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { 0x80 | GPMODE_CACHING_PAGE,          0x0a, 0x04, 0x00, 0xff, 0xff, 0x00, 0x00,
                 0xff,                                0xff, 0xff, 0xff                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x0f, 0x3c, 0x0f             } }
};

static const mode_sense_pages_t jaz_2gb_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                 0xff,                                0x00, 0x00, 0x00                         },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0xff, 0xff, 0x00, 0x00,
                 0xff,                                0xff, 0xff, 0xff                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0xff, 0xff, 0xff, 0xff             } }
};

/*
   Clik! 40 mode-sense pages, from Aaru device report 50 (https://aaru.app/report/50), a real
   ATAPI IOMEGA Clik! 40 CZ, firmware 30cJ.  It reports only two pages:
     - Page 1:  AWRE and ARRE enabled, "most expedient recovery first" (EER), read retries 44.
                No write retry count is reported, so the 6-byte page layout of the Zip 250 is used.
     - Page 2F: reported as an unknown page; the Zip 250's bytes are reused and are NOT confirmed.
   There is no flexible disk or caching page.  The changeable masks are placeholders.
 */
static const mode_sense_pages_t clik_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xc8, 0x2c, 0x00, 0x00, 0x00, 0x00 },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x0f, 0x3c, 0x0f             } }
};

static const mode_sense_pages_t clik_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0xff, 0xff, 0xff, 0xff             } }
};

/*
   Zip 750 mode-sense pages, from Aaru device report 53 (https://aaru.app/report/53), a real
   USB Zip 750, firmware 72.u (only tested with a Zip 100 medium):
     - Page 1:  AWRE, ARRE, EER, read retries 100.
     - Page 5:  32768 kbit/s, 64 heads, 718 cylinders, 32 sectors/track, 512 bytes/sector, 1 step
                pulse per cylinder, 3676 rpm (718 * 64 * 32 = 1470464 sectors).
     - Page 8:  write cache enabled, pre-fetch up to 65535 blocks (as the Zip 250).
     - Page 2F: reported as an unknown page; the Zip 250's bytes are reused and are NOT confirmed.
   The raw bytes of a USB Zip 750 (ZuluIDE firmware issue 39, firmware 78.a) match this table
   exactly for pages 1, 5 and 8 (6-byte page 1; the 1 step pulse per cylinder that Aaru prints is SPC = 0).
   Page 2F depends on the bus: that Zip 750 reports 5c 01 0f 0f, the same as a USB Zip 100, while
   ATAPI drives report the Zip 250's 5c 0f 3c 0f (used here, as this emulates an ATAPI drive) and a SCSI
   Zip 100 reports 5c 0f b4 05.  With a Zip 100 or Zip 250 medium inserted, only the cylinder count is
   switched (see zip_750_media_geometry); that is ESTIMATED.
 */
static const mode_sense_pages_t zip_750_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xc8, 0x64, 0x00, 0x00, 0x00, 0x00 },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,           0x1e, 0x80, 0x00, 0x40, 0x20, 0x02, 0x00,
                 0x02,                                0xce, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x0e, 0x5c, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0xff, 0xff, 0x00, 0x00,
                 0xff,                                0xff, 0xff, 0xff                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x0f, 0x3c, 0x0f             } }
};

/*
   Sony HiFD mode-sense pages, from Aaru device report 118 (https://aaru.app/report/118), a real ATAPI
   SONY HiFD SFD200, firmware 1264:
     - Page 1:  AWRE and ARRE enabled, read retries 32, write retries 32.
     - Page 3:  11570 tracks/zone, 2049 alternate sectors/zone, 34 sectors/track, 512 bytes/sector,
                interleave 0, no skews, flags HSEC | RMB | SURF.
     - Page 5:  28000 kbit/s, 13 heads, 890 cylinders, 34 sectors/track, 512 bytes/sector, 3600 rpm
                (13 * 890 * 34 = 393380 sectors).  "1 step pulse per cylinder" is SPC = 0 (Aaru prints SPC + 1).
     - Page 7:  verify retries 32.
     - Page 8:  write cache enabled, no pre-fetch.
     - Page 0B: medium types 0x40 and 0x42 (the HiFD medium is type 0x42).
     - Page 1B: supports reporting progress of format, 1 LUN (byte layout from the SFF-8070 / Linux
                ide-floppy structure, as for the LS-240).
     - Page 1C: informational exceptions enabled, method of reporting 13.
   The changeable masks are not in the report and are placeholders.

   Sony's "Internal Dual I/F HiFD" driver (IBM option 20L0543) agrees on the geometry: hifdnt4.sys answers
   IOCTLs with 890 x 13 x 34 and 201,410,560 bytes.  The HiFD Format boot sector has no hidden sectors, media F0h,
   8 sectors per cluster, 512 root entries, 192 sectors per FAT and 393,380 sectors.  HiFD Format and the
   driver have no capacity table, and no sign of a 150 MB medium.

   That drive has two interfaces: ATAPI for HiFD disks and the floppy cable for 720 KB and 1.44 MB ones
   ("LoFD").  The drivers pair the two with vendor commands, which are not emulated: CCh (Windows 9x) and CEh
   (NT) with byte 1 = 0 release, 1 claim, 2 arm the pairing test and 3 read it, and CDh with byte 4 = 2 ejects the
   floppy.  They must not simply succeed: HiFD95.VXD repeats CCh 03h until it fails.  As illegal opcodes they
   mean "no pair", and the driver then leaves the floppy drives alone.

   So the row in rdisk.h takes no floppy media: a floppy in this drive is on the floppy cable, not on ATAPI.

   HiFD Format: Full sends FORMAT UNIT 04 17 with FOV and Immed ("Delete all data ... takes about 20 minutes").
   The other options write only the boot sector, FATs and root directory.  hifdnt4.sys sends the same command with
   FOV and DCRT.  rdisk_hifd_format() erases the image unless DCRT is set.
 */
static const mode_sense_pages_t hifd_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xc0, 0x20, 0x00, 0x00, 0x00, 0x00,
                 0x20,                                0x00, 0x00, 0x00                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x2d, 0x32, 0x08, 0x01, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x22, 0x02, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00 },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,           0x1e, 0x6d, 0x60, 0x0d, 0x22, 0x02, 0x00,
                 0x03,                                0x7a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x0e, 0x10, 0x00, 0x00 },
      [0x07] = { MS_PAGE_VERIFY_ERROR,                0x0a, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x0b] = { MS_PAGE_MEDIUM_TYPES,                0x06, 0x00, 0x00, 0x40, 0x42, 0x00, 0x00 },
      [0x1b] = { MS_PAGE_RBAC,                        0x0a, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x1c] = { MS_PAGE_INFO_EXCEPTIONS,             0x0a, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         } }
};

static const mode_sense_pages_t hifd_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                 0xff,                                0x00, 0x00, 0x00                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,           0x1e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00 },
      [0x07] = { MS_PAGE_VERIFY_ERROR,                0x0a, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x0b] = { MS_PAGE_MEDIUM_TYPES,                0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x1b] = { MS_PAGE_RBAC,                        0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x1c] = { MS_PAGE_INFO_EXCEPTIONS,             0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         } }
};

/*
   Floptical mode-sense pages, from Aaru device report 258 (https://aaru.app/report/258), a real SCSI-1
   INSITE I325VM Floptical, firmware 0387, 20 MB medium (40662 blocks):
     - Page 1:  only the "transfer back unrecovered data" (TB) flag is set; no retry counts.
     - Page 3:  all zero (no flags).
     - Page 4:  all zero (0 heads, 0 cylinders).
   The drive also reports pages 5 and 0B, which the report could not decode; they are NOT implemented.
   Iomega's Io20S has the same pages (ESTIMATED).

   Page 2E is the unlock.  A Floptical powers up write-protected so that a PC starting up cannot harm a disk, and "even
   low-level formatting cannot be done" until a utility has talked to it (a review of the INSITE I325VM).  Iomega's
   Macintosh driver 4.0.4 shows the rest: a command that ends in NOT READY, 04/02 (initializing command required)
   makes it send MODE SENSE of page 2E, 34 bytes allocated, and try the command again, and SCSIUTIL.EXE does the same
   read before it formats.  Both also read the page at every disk for the medium type in the header (the Macintosh
   driver checks that against 737,280, 1,474,560 and 20,818,944 bytes).  So writes and FORMAT fail here with 04/02 until
   the page has been read.  It is not known if other commands fail too, or if a MODE SENSE of all pages counts; here
   neither does.  The page's contents are NOT known: zeros, and the 30 bytes that the 34 allocated hold.

   Windows 95 does this by itself: DISKVSD.VXD (OSR2) marks a removable drive that is "INSITE  I325" or "IOMEGA  Io20"
   with "*F" at product bytes 14 and 15, and on a read after a disk change sends 1A 00 2E 00 2A, and ignores what it gets.
 */
static const mode_sense_pages_t floptical_io20s_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x04] = { MS_PAGE_RIGID_DISK,                  0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x2e] = { MS_PAGE_FLOPTICAL,                   0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00 } }
};

static const mode_sense_pages_t floptical_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x04] = { MS_PAGE_RIGID_DISK,                  0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x2e] = { MS_PAGE_FLOPTICAL,                   0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00 } }
};

static const mode_sense_pages_t floptical_io20s_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x04] = { MS_PAGE_RIGID_DISK,                  0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x2e] = { MS_PAGE_FLOPTICAL,                   0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00 } }
};

static const mode_sense_pages_t floptical_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x04] = { MS_PAGE_RIGID_DISK,                  0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x2e] = { MS_PAGE_FLOPTICAL,                   0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00 } }
};

/*
   Bernoulli Box II mode-sense pages (all three are SCSI-only drives).

   Bernoulli 90 - Aaru report 186 (https://aaru.app/report/186), IOMEGA BETA90, firmware 1.4:
     - Page 1:  AWRE, ARRE, EER, read retries 11.  No write retry count is reported (6-byte page).
     - Page 2:  buffer full ratio 208, buffer empty ratio 128, no data transfer disconnect control.
     - Page 3:  29 sectors/track, 512 bytes/sector, flags HSEC | RMB | SURF.
     - Page 4:  758 cylinders, 8 heads (8 * 758 * 29 = 175856 sectors).
   Bernoulli 230 - Aaru report 311 (BETA230, firmware 2.02, tested with no cartridge):
     - Page 1:  AWRE, ARRE, EER, read retries 22 (6-byte page).
     - Page 2:  all zero, no data transfer disconnect control.
     - Page 8:  write cache enabled, pre-fetch for up to 65535 blocks, minimum pre-fetch 64, maximum 65535.
   Bernoulli 150 - the ROM of firmware T.51 (tables at 39F4h and, for the changeable masks, 39CCh), which agrees
   with Aaru report 202 (firmware T.47) except for the things listed here:
     - Page 2 has 10 parameter bytes, not 14.
     - Page 8 is stored little-endian and swapped when it is sent, so the minimum pre-fetch is 0040h as above.
     - Page 2F, undecoded in the reports, is 5C 1E 0A 05 (bit 5 of its first byte unlocks vendor commands).
   The 90 and 230 report a page 2F too, which the reports could not decode, and they have the bytes of the 150
   (ESTIMATED), as has the Bernoulli Box II 44, which has no report: pages 1 and 2 of the 90 and page 2F of the 150
   are all it has here (ESTIMATED).  Iomega's OS/2 drivers show that the 44 has that page: BBII-44.DEV and
   BBII-90.DEV are the same code as BETA-MD.DEV and BETA-230.DEV in the commands that they send, spin-down option
   included.  The Bernoulli Box II 20 does not: BBII-20.DEV and BB-20.DEV never send a MODE SENSE or MODE SELECT and
   their .OPT files offer no spin-down, and the help of the Iomega tools says that Auto Sleep is not available on
   every drive.  The DOS tools confirm it: in SCSIUTIL.EXE the method that sets the sleep timer does nothing on the
   20 and only there.  So MODE SENSE of page 2F fails on the 20, and SCSIDLL.DLL takes that as not write-protected.
   SCSIUTIL.EXE reads the write protection of the 20, 44, 90 and 150 from MODE SENSE (6) of page 1 with 7 bytes
   allocated, which is all that is known of page 1 of the 20: its contents and those of its page 2 are ESTIMATED.
   That test wants byte 2 of the header to be exactly 80h.  The changeable masks of the 44 are placeholders, but for
   page 2F, which has the mask of the 150.
 */
static const mode_sense_pages_t bernoulli_90_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xc8, 0x0b, 0x00, 0x00, 0x00, 0x00 },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0xd0, 0x80, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x1d, 0x02, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00 },
      [0x04] = { MS_PAGE_RIGID_DISK,                  0x16, 0x00, 0x02, 0xf6, 0x08, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x1e, 0x0a, 0x05             } }
};

static const mode_sense_pages_t bernoulli_44_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xc8, 0x0b, 0x00, 0x00, 0x00, 0x00 },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0xd0, 0x80, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x1e, 0x0a, 0x05             } }
};

static const mode_sense_pages_t bernoulli_150_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xc8, 0x16, 0x00, 0x00, 0x00, 0x00 },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0xff, 0xff, 0x00, 0x40,
                 0xff,                                0xff, 0xff, 0xff                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x1e, 0x0a, 0x05             } }
};

static const mode_sense_pages_t bernoulli_150_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xef, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x07, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x7d, 0xff, 0xff, 0x0f             } }
};

static const mode_sense_pages_t bernoulli_230_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xc8, 0x16, 0x00, 0x00, 0x00, 0x00 },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0xff, 0xff, 0x00, 0x40,
                 0xff,                                0xff, 0xff, 0xff                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x5c, 0x1e, 0x0a, 0x05             } }
};

static const mode_sense_pages_t bernoulli_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x04] = { MS_PAGE_RIGID_DISK,                  0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x2f] = { GPMODE_IOMEGA_PAGE,                  0x04, 0x7d, 0xff, 0xff, 0x0f             } }
};

/*
   SuperDisk mode-sense pages, transcribed from real drives in the Aaru device report database:

   LS-120 - report 66 (https://aaru.app/report/66), MATSHITA LS-120 VER5 00, firmware F523:
     - Page 1:  read retries 1, write retries 2, no flags set.
     - Page 3:  32 tracks/zone, no alternates, 32 sectors/track, 512 bytes/sector,
                interleave 0, track skew 10, cylinder skew 0, flags SSEC | RMB (the report says
                addressing runs "in one cylinder", i.e. SURF is clear; byte 20 = 0xa0).
     - Page 5:  4265 kbit/s, 8 heads, 963 cylinders, 32 sectors/track, 512 bytes/sector, 720 rpm.
     - Page 8:  all zero (no flags, no prefetch).
     - Page 0B: medium types 0x10, 0x11, 0x20.

   LS-240 - report 67 (https://aaru.app/report/67), MATSHITA LS-120/240 00, firmware K047:
     - Page 1:  as the LS-120.
     - NO page 3.
     - Page 5:  23960 kbit/s, 32 heads, 262 cylinders, 56 sectors/track, 512 bytes/sector, 1500 rpm
                (469504 sectors).  Which medium was inserted is not stated; the geometry is the
                LS-240 medium's.
     - Page 8:  as the LS-120.
     - Page 0B: as the LS-120.
     - Page 1B: Removable Block Access Capabilities: system floppy device, 1 LUN.  Byte layout
                (SFLP = bit 7 of byte 2, total LUNs = low bits of byte 3) is from the SFF-8070
                style layout used by Linux's ide-floppy, not from the report itself.

   Not mapped / unconfirmed:
     - Page 5: both reports print "1 step pulse per cylinder".  The raw bytes of a real Zip 750
       (ZuluIDE issue 39) have byte 22 (SPC) = 0 where Aaru prints the same line, so Aaru prints SPC + 1
       and byte 22 is 0 here.
     - The motor-off delay that both reports print as 400 is byte 20 of page 5, 28h in the raw pages of a real
       LS-120 COSM 05, so that value is used for both drives.
     - Page 0B "medium type one" is not printed in either report; it is assumed to be 0.
     - The changeable masks are not in the reports and are placeholders (page 5 is fully
       changeable so guests can still MODE SELECT their geometry).
     - What an LS-240 reports for page 5 with LS-120 media inserted (only the geometry is
       overridden in rdisk_mode_sense_read).

   LS-120 COSM 05 - raw MODE SENSE (10) pages from three captures of a real drive (MITBISHI, firmware 0512C105):
     - Pages 1, 3, 5 and 8 with no disk are the bytes above, and so is every page but 3 and 5 with a 1.44 MB disk.
     - Page 0B lists twelve medium types, not three, in a 14-byte page.
     - Pages 3 and 5 report the geometry of an inserted floppy: see rdisk_floppy_media_geometry().

   Unlike the Zip 250 these have NO Iomega vendor page (0x2f).
 */
static const mode_sense_pages_t superdisk_120_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                 0x02,                                0x00, 0x00, 0x00                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x20, 0x02, 0x00, 0x00, 0x00,
                 0x00,                                0x0a, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00 },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,           0x1e, 0x10, 0xa9, 0x08, 0x20, 0x02, 0x00,
                 0x03,                                0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x02, 0xd0, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x0b] = { SUPERDISK_MEDIUM_TYPES_PAGE,         0x06, 0x00, 0x00, 0x00, 0x10, 0x11, 0x20 } }
};

static const mode_sense_pages_t superdisk_120_cosm05_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                 0x02,                                0x00, 0x00, 0x00                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x20, 0x02, 0x00, 0x00, 0x00,
                 0x00,                                0x0a, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00 },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,           0x1e, 0x10, 0xa9, 0x08, 0x20, 0x02, 0x00,
                 0x03,                                0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x02, 0xd0, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x0b] = { SUPERDISK_MEDIUM_TYPES_PAGE,         0x0e, 0x00, 0x00, 0x00, 0x10, 0x11, 0x20,
                 0x22,                                0x23, 0x24, 0x25, 0x26, 0x27, 0x30, 0x31 } }
};

static const mode_sense_pages_t superdisk_240_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                 0x02,                                0x00, 0x00, 0x00                         },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,           0x1e, 0x5d, 0x98, 0x20, 0x38, 0x02, 0x00,
                 0x01,                                0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x05, 0xdc, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x0b] = { SUPERDISK_MEDIUM_TYPES_PAGE,         0x06, 0x00, 0x00, 0x00, 0x10, 0x11, 0x20 },
      [0x1b] = { SUPERDISK_RBAC_PAGE,                 0x0a, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         } }
};

static const mode_sense_pages_t superdisk_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                 0xff,                                0x00, 0x00, 0x00                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,           0x1e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x0b] = { SUPERDISK_MEDIUM_TYPES_PAGE,         0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x1b] = { SUPERDISK_RBAC_PAGE,                 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         } }
};

/*
   SyJet mode-sense pages, transcribed from the SyQuest SyJet SCSI Technical
   Reference (1997, P/N 112857-001A):
     - Page 1 (R/W Error Recovery): standard SCSI-2 layout; AWRE=1 and ARRE=1
       are the documented SyJet defaults (flags byte 0xc8), remaining bytes
       follow the same convention already used for zip_100's page 1 above.
     - Page 3 (Format Device): bytes-per-physical-sector = 512 (0x0200) and
       interleave = 1 (1:1) are documented SyJet values; RMB bit set in the
       flags byte (removable medium, per Table 6-25). Sectors-per-track has
       no single real value (SyJet uses zone-bit recording, "varies based on
       the notch page selected" per the manual) so it uses the same 256
       placeholder scsi_disk.c's page 3 already uses for the same reason.
     - Page 4 (Rigid Disk Geometry): cylinders = 5258 (0x148A), heads = 4,
       and medium rotation rate = 5400 RPM (0x1518) are the real documented
       values (Table 6-31/6-36). Write-precomp/step-rate/landing-zone/RPL
       fields are explicitly "Not supported" per the manual, encoded as 0xff
       (or 0 for RPL, where 0 means "not supported" per SCSI-2) following
       the same not-applicable convention scsi_disk.c's page 4 already uses
       for its landing-zone field.
 */
static const mode_sense_pages_t syjet_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xc8, 0x16, 0x00, 0x00, 0x00, 0x00,
                 0x5a,                                0x00, 0x50, 0x20                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01,
                 0x00,                                0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x01,
                 0x00,                                0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
      [0x04] = { GPMODE_RIGID_DISK_PAGE,              0x16, 0x00, 0x14, 0x8a, 0x04, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0x00, 0x00, 0x00, 0x15, 0x18, 0x00, 0x00 } }
};

static const mode_sense_pages_t syjet_mode_sense_pages_default_scsi = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xc8, 0x16, 0x00, 0x00, 0x00, 0x00,
                 0x5a,                                0x00, 0x50, 0x20                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01,
                 0x00,                                0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x01,
                 0x00,                                0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
      [0x04] = { GPMODE_RIGID_DISK_PAGE,              0x16, 0x00, 0x14, 0x8a, 0x04, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0x00, 0x00, 0x00, 0x15, 0x18, 0x00, 0x00 } }
};

static const mode_sense_pages_t syjet_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff,
                 0x5a,                                0xff, 0xff, 0xff                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x04] = { GPMODE_RIGID_DISK_PAGE,              0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }
};

/*
   SparQ mode-sense pages. The SparQ never shipped as a real SCSI drive, so
   unlike SyJet these can't be transcribed from an OEM SCSI reference - they
   mirror SyJet's page layout/style (same vendor, same drive generation) with
   SparQ's own real values substituted in from the SyQuest SparQ Internal
   EIDE Technical Reference (1997, P/N 113277-001A):
     - Page 1 (R/W Error Recovery): identical to SyJet's - generic SCSI-2
       defaults, not SparQ-specific (no SCSI reference to source it from).
     - Page 3 (Format Device): bytes-per-physical-sector = 512 and RMB set,
       as with SyJet. Sectors-per-track = 63 (0x3F) is a *real* documented
       SparQ value here (unlike SyJet, whose zoned recording had no single
       real value) - it's the fixed divisor used in the manual's own
       LBA = ((cylinder * heads + head) * sectors_per_track) + sector - 1
       formula (section "Identify Device (ECh)"). Interleave = 1 (1:1) is
       assumed by SyQuest product-family convention (SyJet's manual confirms
       1:1 explicitly; SparQ's EIDE reference doesn't discuss interleave at
       all since ATA CHS addressing doesn't expose the concept).
     - Page 4 (Rigid Disk Geometry): cylinders = 2906 (0x0B5A) and heads = 16
       are the real documented default ATA CHS translation (same section).
       Medium rotation rate = 5400 RPM (0x1518) is assumed by SyQuest
       product-family convention (confirmed for SyJet; not stated in the
       SparQ EIDE reference, which has no SCSI rotation-rate page).
 */
static const mode_sense_pages_t sparq_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xc8, 0x16, 0x00, 0x00, 0x00, 0x00,
                 0x5a,                                0x00, 0x50, 0x20                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01,
                 0x00,                                0x01, 0x00, 0x3f, 0x02, 0x00, 0x00, 0x01,
                 0x00,                                0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
      [0x04] = { GPMODE_RIGID_DISK_PAGE,              0x16, 0x00, 0x0b, 0x5a, 0x10, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0x00, 0x00, 0x00, 0x15, 0x18, 0x00, 0x00 } }
};

static const mode_sense_pages_t sparq_mode_sense_pages_default_scsi = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xc8, 0x16, 0x00, 0x00, 0x00, 0x00,
                 0x5a,                                0x00, 0x50, 0x20                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01,
                 0x00,                                0x01, 0x00, 0x3f, 0x02, 0x00, 0x00, 0x01,
                 0x00,                                0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
      [0x04] = { GPMODE_RIGID_DISK_PAGE,              0x16, 0x00, 0x0b, 0x5a, 0x10, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0x00, 0x00, 0x00, 0x15, 0x18, 0x00, 0x00 } }
};

static const mode_sense_pages_t sparq_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff,
                 0x5a,                                0xff, 0xff, 0xff                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x04] = { GPMODE_RIGID_DISK_PAGE,              0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }
};
/*
   Castlewood ORB 2.2 GB mode-sense pages, from Aaru device report 200 (https://aaru.app/report/200),
   a real SCSI-2 CWS ORB2 -SE U ID 0, firmware D01K:
     - Page 1:  read retries 32, write retries 32, no flags.
     - Page 2:  buffer full ratio 255, no data transfer disconnect control.
     - Page 3:  1234 tracks/zone, 380 alternate sectors/zone, 30 alternate tracks/zone, 60 alternate
                tracks/LUN, 63 sectors/track, 512 bytes/sector, interleave 0, track skew 15, cylinder
                skew 15, flags HSEC | RMB (SURF clear: addressing runs "in one cylinder").
     - Page 4:  PS, 128 heads, 534 cylinders (the report prints no rotation rate).
     - Page 7:  verify retries 32.   - Page 8: all zero.   - Page 0A (control): all zero.
   Pages 09h and 0Ch exist but could not be decoded and are not implemented.  Changeable masks are
   placeholders.
 */
static const mode_sense_pages_t orb_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00,
                 0x20,                                0x00, 0x00, 0x00                         },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x04, 0xd2, 0x01, 0x7c, 0x00, 0x1e,
                 0x00,                                0x3c, 0x00, 0x3f, 0x02, 0x00, 0x00, 0x00,
                 0x00,                                0x0f, 0x00, 0x0f, 0x60, 0x00, 0x00, 0x00 },
      [0x04] = { 0x80 | MS_PAGE_RIGID_DISK,           0x16, 0x00, 0x02, 0x16, 0x80, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x07] = { MS_PAGE_VERIFY_ERROR,                0x0a, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x0a] = { MS_PAGE_CONTROL,                     0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         } }
};

/*
   SyQuest SQ5200C (5.25" SQ400 / SQ800 / SQ2000), from Aaru device report 197
   (https://aaru.app/report/197), a real SCSI-2 SyQuest SQ5200C, firmware 3CE4 (MODE SENSE(6) only):
     - Page 1:  PS, read retries 3, write retries 14, no flags.
     - Page 2:  PS, buffer full ratio 192, buffer empty ratio 128.
     - Page 3:  903 tracks/zone, 384 alternate sectors/zone, 4 alternate tracks/zone, 32 alternate
                tracks/LUN, 96 sectors/track, 512 bytes/sector, interleave 1, track skew 32, cylinder
                skew 0, flags HSEC | RMB | SURF.
     - Page 4:  2 heads, 2260 cylinders, 3220 rpm.   - Page 8: PS, all zero.
   Page 0Ch exists but could not be decoded and is not implemented.  Page 20h is the drive's name, see
   SYQUEST_PAGE_20.  The geometry is what the drive reported with its tested media and is not switched per medium.
   Changeable masks are placeholders.
 */
/*
   Page 20h of the SyQuest SCSI drives, from SQDRIVER.SYS 7.95: 24 bytes of ASCII, "SyQuest " and the product.  The
   driver reads it with MODE SENSE (6), 38 bytes allocated, default and current values, from offset 14, tells a SyQuest
   drive by the first 4 bytes and the cartridge from the product: "SQ555", "SQ5110", "SQ3105", "SQ327" and so on.  It
   treats firmware older than 7220 of the EZ230S differently; 7220 is the firmware here, so that does not matter.

   The same driver has the three settings of SyQuest's utilities, all in mode pages: the write cache is WCE in page 8;
   A/V mode is DCR set and ARRE clear in page 1, with SP set; the cartridge write protection is bit 1 of byte 2 of a
   vendor page 0 of 3 bytes, which is not implemented because the reports do not show that page.
 */
static const mode_sense_pages_t sq5200c_mode_sense_pages_default = {
    { [0x01] = { 0x80 | GPMODE_R_W_ERROR_PAGE,        0x0a, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
                 0x0e,                                0x00, 0x00, 0x00                         },
      [0x02] = { 0x80 | GPMODE_DISCONNECT_PAGE,       0x0e, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x03, 0x87, 0x01, 0x80, 0x00, 0x04,
                 0x00,                                0x20, 0x00, 0x60, 0x02, 0x00, 0x00, 0x01,
                 0x00,                                0x20, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00 },
      [0x04] = { MS_PAGE_RIGID_DISK,                  0x16, 0x00, 0x08, 0xd4, 0x02, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x0c, 0x94, 0x00, 0x00 },
      [0x08] = { 0x80 | GPMODE_CACHING_PAGE,          0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x20] = { 0x20, 0x18, 'S', 'y', 'Q', 'u', 'e', 's', 't', ' ', 'S', 'Q', '5', '2', '0', '0', 'C', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } }
};

/*
   SyQuest SQ3105S (SQ310): Aaru device report 196 (https://aaru.app/report/196) shows no standard mode
   pages at all, only a vendor page that it could not decode, taken to be page 20h (ESTIMATED).
 */
static const mode_sense_pages_t sq3105s_mode_sense_pages_default = {
    { [0x20] = { 0x20, 0x18, 'S', 'y', 'Q', 'u', 'e', 's', 't', ' ', 'S', 'Q', '3', '1', '0', '5', 'S', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } }
};

/*
   SyQuest EZFlyer 230 (EZ135 / EZFlyer media), from Aaru device report 195 (https://aaru.app/report/195),
   a real SCSI-2 SyQuest EZ230S, firmware 7220 (MODE SENSE(6) only):
     - Page 1:  PS only, no retry counts.
     - Page 2:  PS, buffer full ratio 192, buffer empty ratio 128.
     - Page 3:  887 tracks/zone, 280 alternate sectors/zone, 2 alternate tracks/zone, 8 alternate
                tracks/LUN, 140 sectors/track, 512 bytes/sector, interleave 1, track skew 24, cylinder
                skew 24, flags HSEC | RMB | SURF.
     - Page 4:  1 head, 4192 cylinders, 3600 rpm.
     - Page 8:  PS, write cache enabled, demand-read retention priority 1 ("evicted sooner").
   Page 0Ch exists but could not be decoded and is not implemented.  Page 20h is the drive's name, see the SQ5200C.
   Changeable masks are placeholders.
 */
static const mode_sense_pages_t ez230s_mode_sense_pages_default = {
    { [0x01] = { 0x80 | GPMODE_R_W_ERROR_PAGE,        0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x02] = { 0x80 | GPMODE_DISCONNECT_PAGE,       0x0e, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x03, 0x77, 0x01, 0x18, 0x00, 0x02,
                 0x00,                                0x08, 0x00, 0x8c, 0x02, 0x00, 0x00, 0x01,
                 0x00,                                0x18, 0x00, 0x18, 0x70, 0x00, 0x00, 0x00 },
      [0x04] = { MS_PAGE_RIGID_DISK,                  0x16, 0x00, 0x10, 0x60, 0x01, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x0e, 0x10, 0x00, 0x00 },
      [0x08] = { 0x80 | GPMODE_CACHING_PAGE,          0x0a, 0x04, 0x10, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x20] = { 0x20, 0x18, 'S', 'y', 'Q', 'u', 'e', 's', 't', ' ', 'E', 'Z', '2', '3', '0', 'S', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } }
};

/*
   The SyQuest drives without a report, all ESTIMATED: pages 1, 2 and 8 of the nearest drive that has one, with no
   geometry because it is not known, and page 20h with its own name.  The SQ3270S has page 20h only, like the SQ3105S.
   The IDE drives have what the EZ230S has: mode pages are SCSI, and the real IDE drives have none.
 */
static const mode_sense_pages_t sq5110c_mode_sense_pages_default = {
    {
      [0x01] = { 0x80 | GPMODE_R_W_ERROR_PAGE,        0x0a, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
                 0x0e,                                0x00, 0x00, 0x00                         },
      [0x02] = { 0x80 | GPMODE_DISCONNECT_PAGE,       0x0e, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { 0x80 | GPMODE_CACHING_PAGE,          0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x20] = { 0x20, 0x18, 'S', 'y', 'Q', 'u', 'e', 's', 't', ' ', 'S', 'Q', '5', '1', '1', '0', 'C', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } }
};

static const mode_sense_pages_t sq555_mode_sense_pages_default = {
    {
      [0x01] = { 0x80 | GPMODE_R_W_ERROR_PAGE,        0x0a, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
                 0x0e,                                0x00, 0x00, 0x00                         },
      [0x02] = { 0x80 | GPMODE_DISCONNECT_PAGE,       0x0e, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { 0x80 | GPMODE_CACHING_PAGE,          0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x20] = { 0x20, 0x18, 'S', 'y', 'Q', 'u', 'e', 's', 't', ' ', 'S', 'Q', '5', '5', '5', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } }
};

static const mode_sense_pages_t ez135s_mode_sense_pages_default = {
    {
      [0x01] = { 0x80 | GPMODE_R_W_ERROR_PAGE,        0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x02] = { 0x80 | GPMODE_DISCONNECT_PAGE,       0x0e, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { 0x80 | GPMODE_CACHING_PAGE,          0x0a, 0x04, 0x10, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x20] = { 0x20, 0x18, 'S', 'y', 'Q', 'u', 'e', 's', 't', ' ', 'E', 'Z', '1', '3', '5', 'S', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } }
};

static const mode_sense_pages_t sq3270s_mode_sense_pages_default = {
    { [0x20] = { 0x20, 0x18, 'S', 'y', 'Q', 'u', 'e', 's', 't', ' ', 'S', 'Q', '3', '2', '7', '0', 'S', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } }
};

static const mode_sense_pages_t ez135a_mode_sense_pages_default = {
    {
      [0x01] = { 0x80 | GPMODE_R_W_ERROR_PAGE,        0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x02] = { 0x80 | GPMODE_DISCONNECT_PAGE,       0x0e, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { 0x80 | GPMODE_CACHING_PAGE,          0x0a, 0x04, 0x10, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x20] = { 0x20, 0x18, 'S', 'y', 'Q', 'u', 'e', 's', 't', ' ', 'E', 'Z', '1', '3', '5', 'A', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } }
};

static const mode_sense_pages_t ez230a_mode_sense_pages_default = {
    {
      [0x01] = { 0x80 | GPMODE_R_W_ERROR_PAGE,        0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x02] = { 0x80 | GPMODE_DISCONNECT_PAGE,       0x0e, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { 0x80 | GPMODE_CACHING_PAGE,          0x0a, 0x04, 0x10, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x20] = { 0x20, 0x18, 'S', 'y', 'Q', 'u', 'e', 's', 't', ' ', 'E', 'Z', '2', '3', '0', 'A', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } }
};

static const mode_sense_pages_t sq3105a_mode_sense_pages_default = {
    {
      [0x01] = { 0x80 | GPMODE_R_W_ERROR_PAGE,        0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x02] = { 0x80 | GPMODE_DISCONNECT_PAGE,       0x0e, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { 0x80 | GPMODE_CACHING_PAGE,          0x0a, 0x04, 0x10, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x20] = { 0x20, 0x18, 'S', 'y', 'Q', 'u', 'e', 's', 't', ' ', 'S', 'Q', '3', '1', '0', '5', 'A', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } }
};

static const mode_sense_pages_t sq3270a_mode_sense_pages_default = {
    {
      [0x01] = { 0x80 | GPMODE_R_W_ERROR_PAGE,        0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x02] = { 0x80 | GPMODE_DISCONNECT_PAGE,       0x0e, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { 0x80 | GPMODE_CACHING_PAGE,          0x0a, 0x04, 0x10, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x20] = { 0x20, 0x18, 'S', 'y', 'Q', 'u', 'e', 's', 't', ' ', 'S', 'Q', '3', '2', '7', '0', 'A', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } }
};

/*
   Caleb UHD144 mode-sense pages - ESTIMATED.  No report of the drive exists, but it identifies itself as an
   "LS-120" and works with the Imation LS-120 drivers, so the LS-120's pages (Aaru report 66) are used, with
   page 5 changed to 1101 cylinders of 8 heads and 32 sectors per track, the geometry of the LS-120's page 5 (ESTIMATED):
   281,856 sectors are exactly that, and the 64 heads of the boot sector on real media are not a whole number of
   cylinders.  The rotation rate is the 1000 rpm of Caleb's technical manual (section 3.3).
   The transfer rate is the LS-120's.

   That manual (section 5.1, on the Wayback Machine at calebdrive.com/manual5.html) names the commands of the drive:
   ATA Get Media Status, Device Reset, Check Power Mode, Execute Diagnostics, Idle Immediate, Identify Packet Device,
   Set Features, Sleep and Standby Immediate, and ATAPI READ(10), WRITE(10), VERIFY, WRITE AND VERIFY, FORMAT UNIT,
   INQUIRY, MODE SELECT, MODE SENSE, READ FORMAT CAPACITIES, READ CAPACITY, REQUEST SENSE, SEEK, START/STOP UNIT and
   TEST UNIT READY.  Others are not refused here, as that list leaves out PREVENT ALLOW MEDIUM REMOVAL, which an ATAPI
   floppy needs, and the manual says that it was written before the drive had been fully tested.  Caleb's itDevice.dll
   sends INQUIRY (96 bytes), READ CAPACITY, READ(10), WRITE(10), START/STOP UNIT, FORMAT UNIT and vendor opcode F0h.
 */
static const mode_sense_pages_t uhd144_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                 0x02,                                0x00, 0x00, 0x00                         },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x20, 0x02, 0x00, 0x00, 0x00,
                 0x00,                                0x0a, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00 },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,           0x1e, 0x10, 0xa9, 0x08, 0x20, 0x02, 0x00,
                 0x04,                                0x4d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x03, 0xe8, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x0b] = { SUPERDISK_MEDIUM_TYPES_PAGE,         0x06, 0x00, 0x00, 0x00, 0x10, 0x11, 0x20 } }
};

/* Shared placeholder changeable masks for the ORB and the SyQuest SCSI drives above. */
static const mode_sense_pages_t scsi_removable_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                 0xff,                                0x00, 0x00, 0x00                         },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x04] = { MS_PAGE_RIGID_DISK,                  0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x07] = { MS_PAGE_VERIFY_ERROR,                0x0a, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x20] = { 0x20,                                0x18                                     },
      [0x08] = { GPMODE_CACHING_PAGE,                 0x0a, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         },
      [0x0a] = { MS_PAGE_CONTROL,                     0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         } }
};

/* Per-drive-type mode page sets for the types added from the later Aaru reports, so that adding a drive
   no longer means touching every mode-sense dispatcher. */
typedef struct rdisk_ms_set_t {
    uint32_t                  type;
    uint64_t                  flags;
    const mode_sense_pages_t *def;
    const mode_sense_pages_t *chg;
} rdisk_ms_set_t;

/*
   Rev mode pages, from Aaru device reports 148, 187 and 188, the same in all three.  The reports give decoded
   values, so the bytes are rebuilt from them:
     - Page 1:   read retries 40, write retries 32.  The "recovery parameter 0x192" that they print is taken
                 to be decimal: C0h, AWRE and ARRE.
     - Page 5:   16644 kbit/s, 8 heads, step time 15000 us, everything else 0.
     - Page 8:   write cache enabled, no pre-fetch.
     - Page 1Ah: idle timer 360 s, standby timer 1800 s.
     - Page 1Dh: reported as unknown, so only its length is here, which is ESTIMATED.
     - Page 2Ah: caddy, lock, eject, 256 volume levels, 2048 KB buffer, read and write speed 22160 KB/s (CLV).
 */
static const mode_sense_pages_t rev_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,      0x0a, 0xc0, 0x28, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00 },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,  0x1e, 0x41, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,        0x0a, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x1a] = { 0x1a,                       0x0a, 0x00, 0x03, 0x00, 0x00, 0x8c, 0xa0, 0x00, 0x00, 0x46, 0x50 },
      [0x1d] = { 0x1d,                       0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x2a] = { GPMODE_CAPABILITIES_PAGE,   0x22, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x56, 0x90, 0x01, 0x00,
                 0x08,                       0x00, 0x56, 0x90, 0x00, 0x00, 0x56, 0x90, 0x00, 0x00, 0x00, 0x00,
                 0x00,                       0x00, 0x00, 0x00, 0x56, 0x90, 0x00, 0x01, 0x00, 0x00, 0x56, 0x90 } }
};

static const mode_sense_pages_t rev_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,      0x0a },
      [0x05] = { GPMODE_FLEXIBLE_DISK_PAGE,  0x1e },
      [0x08] = { GPMODE_CACHING_PAGE,        0x0a },
      [0x1a] = { 0x1a,                       0x0a },
      [0x1d] = { 0x1d,                       0x08 },
      [0x2a] = { GPMODE_CAPABILITIES_PAGE,   0x22 } }
};

#define REV_MODE_SENSE_PAGE_FLAGS (GPMODEP_R_W_ERROR_PAGE | GPMODEP_FLEXIBLE_DISK_PAGE | GPMODEP_CACHING_PAGE | \
                                   (1ULL << 0x1a) | (1ULL << 0x1d) | (1ULL << GPMODE_CAPABILITIES_PAGE) | \
                                   GPMODEP_ALL_PAGES)

static const rdisk_ms_set_t rdisk_ms_sets[] = {
    /* ESTIMATED: nothing of the Peerless' pages is known, so these are those of the Jaz 2GB. */
    { RDISK_TYPE_PEERLESS,  (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_CACHING_PAGE |
                             GPMODEP_IOMEGA_PAGE | GPMODEP_ALL_PAGES),
      &jaz_2gb_mode_sense_pages_default, &jaz_2gb_mode_sense_pages_changeable },
    { RDISK_TYPE_REV_35,    REV_MODE_SENSE_PAGE_FLAGS, &rev_mode_sense_pages_default, &rev_mode_sense_pages_changeable },
    { RDISK_TYPE_REV_70,    REV_MODE_SENSE_PAGE_FLAGS, &rev_mode_sense_pages_default, &rev_mode_sense_pages_changeable },
    { RDISK_TYPE_REV_120,   REV_MODE_SENSE_PAGE_FLAGS, &rev_mode_sense_pages_default, &rev_mode_sense_pages_changeable },
    { RDISK_TYPE_ORB_2_2GB, (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_FORMAT_DEVICE_PAGE |
                             (1ULL << MS_PAGE_RIGID_DISK) | (1ULL << MS_PAGE_VERIFY_ERROR) |
                             GPMODEP_CACHING_PAGE | (1ULL << MS_PAGE_CONTROL) | GPMODEP_ALL_PAGES),
      &orb_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_SQ5200C,   (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_FORMAT_DEVICE_PAGE |
                             (1ULL << MS_PAGE_RIGID_DISK) | GPMODEP_CACHING_PAGE | (1ULL << MS_PAGE_SYQUEST) |
                             GPMODEP_ALL_PAGES),
      &sq5200c_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_SQ3105S,   ((1ULL << MS_PAGE_SYQUEST) | GPMODEP_ALL_PAGES),
      &sq3105s_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_EZ230S,    (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_FORMAT_DEVICE_PAGE |
                             (1ULL << MS_PAGE_RIGID_DISK) | GPMODEP_CACHING_PAGE | (1ULL << MS_PAGE_SYQUEST) |
                             GPMODEP_ALL_PAGES),
      &ez230s_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_SQ5110C, (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_CACHING_PAGE | (1ULL << MS_PAGE_SYQUEST) |
                             GPMODEP_ALL_PAGES),
      &sq5110c_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_SQ555, (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_CACHING_PAGE | (1ULL << MS_PAGE_SYQUEST) |
                             GPMODEP_ALL_PAGES),
      &sq555_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_EZ135S, (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_CACHING_PAGE | (1ULL << MS_PAGE_SYQUEST) |
                             GPMODEP_ALL_PAGES),
      &ez135s_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_SQ3270S, ((1ULL << MS_PAGE_SYQUEST) | GPMODEP_ALL_PAGES),
      &sq3270s_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_EZ135A, (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_CACHING_PAGE | (1ULL << MS_PAGE_SYQUEST) |
                             GPMODEP_ALL_PAGES),
      &ez135a_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_EZ230A, (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_CACHING_PAGE | (1ULL << MS_PAGE_SYQUEST) |
                             GPMODEP_ALL_PAGES),
      &ez230a_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_SQ3105A, (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_CACHING_PAGE | (1ULL << MS_PAGE_SYQUEST) |
                             GPMODEP_ALL_PAGES),
      &sq3105a_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_SQ3270A, (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_CACHING_PAGE | (1ULL << MS_PAGE_SYQUEST) |
                             GPMODEP_ALL_PAGES),
      &sq3270a_mode_sense_pages_default, &scsi_removable_mode_sense_pages_changeable },
    { RDISK_TYPE_UHD144,    (GPMODEP_R_W_ERROR_PAGE | GPMODEP_FORMAT_DEVICE_PAGE | GPMODEP_FLEXIBLE_DISK_PAGE |
                             GPMODEP_CACHING_PAGE | (1ULL << SUPERDISK_MEDIUM_TYPES_PAGE) | GPMODEP_ALL_PAGES),
      &uhd144_mode_sense_pages_default, &superdisk_mode_sense_pages_changeable }
};

static const rdisk_ms_set_t *
rdisk_find_ms_set(const uint32_t type)
{
    for (size_t i = 0; i < (sizeof(rdisk_ms_sets) / sizeof(rdisk_ms_sets[0])); i++) {
        if (rdisk_ms_sets[i].type == type)
            return &rdisk_ms_sets[i];
    }

    return NULL;
}
// clang-format on

static void rdisk_command_complete(rdisk_t *dev);
static void rdisk_init(rdisk_t *dev);

/* A medium's capacity in bytes, from the media table in rdisk.h. */
static uint64_t
rdisk_media_bytes(const uint32_t media)
{
    return (uint64_t) rdisk_types[media].sectors * rdisk_types[media].bytes_per_sector;
}

static uint32_t
rdisk_drive_type_of(const rdisk_t *dev)
{
    return (dev->drv->type < KNOWN_RDISK_DRIVE_TYPES) ? dev->drv->type : RDISK_TYPE_GENERIC;
}

/* Index into rdisk_types[] of the medium that is in the drive, or of the drive's default medium when it
   is empty.  medium_size is the number of sectors of that medium's own size (bytes_per_sector). */
static uint32_t
rdisk_medium_index(const rdisk_t *dev)
{
    if ((dev->drv->fp != NULL) && (dev->drv->medium_type < KNOWN_RDISK_TYPES))
        return dev->drv->medium_type;

    return rdisk_drive_types[rdisk_drive_type_of(dev)].default_media;
}

/* Bytes per sector of the medium in the drive (the drive's default medium when it is empty). */
static uint32_t
rdisk_sector_size(const rdisk_t *dev)
{
    return rdisk_types[rdisk_medium_index(dev)].bytes_per_sector;
}

/* Which of the media the drive type supports has exactly this size in bytes?  -1 if none. */
static int
rdisk_find_medium(const rdisk_t *dev, const uint64_t bytes)
{
    const uint32_t type = rdisk_drive_type_of(dev);

    for (uint32_t i = 0; i < KNOWN_RDISK_TYPES; i++) {
        if (rdisk_drive_types[type].supported_media[i] && (rdisk_media_bytes(i) == bytes))
            return (int) i;
    }

    return -1;
}

/* The largest medium this drive type takes, from supported_media[] in rdisk.h.  The generic drive
   takes every medium but reports the Zip 250's size, as it always has. */
static uint32_t
rdisk_max_medium_index(const rdisk_t *dev)
{
    const uint32_t type = rdisk_drive_type_of(dev);
    uint32_t       max  = RDISK_MEDIUM_ZIP_250;
    uint64_t       size = 0;

    if (type == RDISK_TYPE_GENERIC)
        return RDISK_MEDIUM_ZIP_250;

    for (uint32_t i = 0; i < KNOWN_RDISK_TYPES; i++) {
        if (rdisk_drive_types[type].supported_media[i] && (rdisk_media_bytes(i) > size)) {
            size = rdisk_media_bytes(i);
            max  = i;
        }
    }

    return max;
}

/* The number of sectors of that largest medium. */
static uint32_t
rdisk_max_medium_size(const rdisk_t *dev)
{
    return rdisk_types[rdisk_max_medium_index(dev)].sectors;
}

/* Drives that only ever took one medium always report that medium's size in READ FORMAT CAPACITIES. */
static int
rdisk_single_medium_drive(const rdisk_t *dev)
{
    const uint32_t type  = rdisk_drive_type_of(dev);
    int            count = 0;

    for (uint32_t i = 0; i < KNOWN_RDISK_TYPES; i++)
        count += (rdisk_drive_types[type].supported_media[i] != 0);

    return (type != RDISK_TYPE_GENERIC) && (count == 1);
}

/* The identity a drive reports in INQUIRY: the vendor, model (product) and revision of its row in
   rdisk_drive_types[].  The generic drive reports the emulator's own name, product and version instead, so
   it has none. */
static const rdisk_drive_type_t *
rdisk_inquiry_identity(const rdisk_t *dev)
{
    if ((dev->drv->type == RDISK_TYPE_GENERIC) || (dev->drv->type >= KNOWN_RDISK_DRIVE_TYPES))
        return NULL;

    return &rdisk_drive_types[dev->drv->type];
}

/* Media a drive only reads, from the documented compatibility: the Bernoulli 90 and MultiDisk 150
   read the 44 MB cartridge, and the Zip 750 reads the 100 MB Zip disk, but neither writes them.  The
   MultiDisk 150's ROM confirms it: of its three recording formats only the 44 MB one is not writable. */
static int
rdisk_medium_is_read_only(const uint32_t type, const uint32_t medium)
{
    return ((type == RDISK_TYPE_ZIP_750) && (medium == RDISK_MEDIUM_ZIP_100)) ||
           ((type == RDISK_TYPE_REV_120) && (medium == RDISK_MEDIUM_REV_70)) ||
           (((type == RDISK_TYPE_BERNOULLI_90) || IS_BERNOULLI_150(type) || (type == RDISK_TYPE_BERNOULLI_230)) &&
            (medium == RDISK_MEDIUM_BERNOULLI_44));
}

/* The number of blocks the drive reports for a medium.  It is the size of the image, except for the
   640 KB floppy: the LS-120 presents it as a 720 KB medium of 1440 blocks (see rdisk.h). */
static uint32_t
rdisk_medium_blocks(const uint32_t medium)
{
    return (medium == RDISK_MEDIUM_FD_640K) ? FD_640K_BLOCKS : rdisk_types[medium].sectors;
}

/* Is the medium in the drive a 640 KB floppy? */
static int
rdisk_is_640k(const rdisk_t *dev)
{
    return (dev->drv->fp != NULL) && (dev->drv->medium_type == RDISK_MEDIUM_FD_640K);
}

/* Where in the image is a block of the drive's block space?
   A 640 KB floppy has 8 sectors per track on a medium that is addressed with 9, so every 9th device block
   (LBA % 9 == 8) is absent and the others map to image sector LBA - LBA / 9 (the inverse of MKELS120's
   device LBA = S + S / 8, LS-120 specification, section 6.2).  Returns 0 for an absent block, which has no
   offset: it reads as zeroes and writes to it are dropped (MKELS120 never addresses one; this only keeps a
   transfer that spans one, such as a VERIFY of a whole track, from failing). */
static int
rdisk_block_offset(const rdisk_t *dev, const uint32_t lba, uint64_t *offs)
{
    uint32_t pos = lba;

    if (rdisk_is_640k(dev)) {
        if ((lba % 9) == 8)
            return 0;

        pos = lba - (lba / 9);
    }

    *offs = dev->drv->base + ((uint64_t) pos * rdisk_sector_size(dev));
    return 1;
}

/* The protection that is in effect, set by rdisk_set_protection(): 0 none, 2 write, 3 write with a password,
   5 read and write with a password.  Bit 3 of the mode only says that it lasts until the disk is ejected.
   On a Rev that bit is "temporary access": ImDrvInt.dll sets 0Dh with the password to open a protected disk
   for this session, so nothing is denied until the disk is ejected.  The Caleb UHD144 only has mode 2. */
static uint8_t
rdisk_protection(const rdisk_t *dev)
{
    if (IS_REV(dev->drv->type) && (dev->protect_mode & 0x08))
        return 0;

    return (HAS_PROTECTION(dev->drv->type) || (dev->drv->type == RDISK_TYPE_UHD144)) ? (dev->protect_mode & 0x07) : 0;
}

/* Is the medium write-protected, by being read-only or by the drive's protection? */
static int
rdisk_write_denied(const rdisk_t *dev)
{
    return dev->drv->read_only || (rdisk_protection(dev) != 0);
}

/* The drives that report a write-protected medium in the header of MODE SENSE. */
static int
rdisk_reports_write_protect(const rdisk_t *dev)
{
    return IS_SUPERDISK_120(dev->drv->type) || IS_BERNOULLI(dev->drv->type) || HAS_PROTECTION(dev->drv->type) ||
           (dev->drv->type == RDISK_TYPE_FLOPTICAL_IO20S);
}

/* The drives whose firmware checks the end of a transfer and not just its start. */
static int
rdisk_checks_transfer_end(const rdisk_t *dev)
{
    return IS_SUPERDISK_120(dev->drv->type) || IS_BERNOULLI_150(dev->drv->type);
}

/* Does the transfer the command asks for (sector_pos and sector_len) lie outside the medium?  Every drive
   checks its start; the LS-120 also checks its end, as the DOS driver's VERIFY(10) of a track range
   relies on an error for a range that runs past the end (LS-120 specification, tests ERR-03, VER-02), and
   the Bernoulli 150's firmware checks LBA + length for every command that has a range. */
static int
rdisk_transfer_out_of_range(const rdisk_t *dev)
{
    if (dev->sector_pos >= dev->drv->medium_size)
        return 1;

    return rdisk_checks_transfer_end(dev) &&
           (((uint64_t) dev->sector_pos + dev->sector_len) > dev->drv->medium_size);
}

#ifdef ENABLE_RDISK_LOG
int rdisk_do_log = ENABLE_RDISK_LOG;

static void
rdisk_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (rdisk_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define rdisk_log(priv, fmt, ...)
#endif

static int
rdisk_load_abort(const rdisk_t *dev)
{
    if (dev->drv->fp)
        fclose(dev->drv->fp);
    dev->drv->fp           = NULL;
    dev->drv->medium_size = 0;
    rdisk_eject(dev->id); /* Make sure the host OS knows we've rejected (and ejected) the image. */
    return 0;
}

static int
image_is_sdi(const char *s)
{
    return !strcasecmp(path_get_extension((char *) s), "SDI");
}

static int
image_is_zdi(const char *s)
{
    return !strcasecmp(path_get_extension((char *) s), "ZDI");
}

int
rdisk_is_empty(const uint8_t id)
{
    const rdisk_t *dev = (const rdisk_t *) rdisk_drives[id].priv;
    int          ret = 0;

    if ((dev->drv == NULL) || (dev->drv->fp == NULL))
        ret = 1;

    return ret;
}

void
rdisk_load(const rdisk_t *dev, const char *fn, const int skip_insert)
{
    const int was_empty = rdisk_is_empty(dev->id);
    int       ret       = 0;
    int       offs      = 0;

    if (strstr(fn, "wp://") == fn) {
        offs                = 5;
        dev->drv->read_only = 1;
    }

    fn += offs;

    if (dev->drv == NULL)
        rdisk_eject(dev->id);
    else {
        const int is_sdi = image_is_sdi(fn);
        const int is_zdi = image_is_zdi(fn);

        dev->drv->fp     = plat_fopen(fn, dev->drv->read_only ? "rb" : "rb+");
        ret              = 1;

        if (dev->drv->fp == NULL) {
            if (!dev->drv->read_only) {
                dev->drv->fp = plat_fopen(fn, "rb");
                if (dev->drv->fp == NULL)
                    ret = rdisk_load_abort(dev);
                else
                    dev->drv->read_only = 1;
            } else
                ret = rdisk_load_abort(dev);
        }

        if (ret) {
            fseeko64(dev->drv->fp, 0, SEEK_END);
            int64_t size = ftello64(dev->drv->fp);    /* ORB and Jaz images are 2 GB or more */

            if ((is_sdi) || (is_zdi)) {
                /* This is a SDI or ZDI image. */
                size -= 0x1000;
                dev->drv->base = 0x1000;
            } else
                dev->drv->base = 0;

            const int medium = (size < 0) ? -1 : rdisk_find_medium(dev, (uint64_t) size);

            if ((size < 0) || ((size & 511) != 0) || (medium < 0)) {
                rdisk_log(dev->log, "File is incorrect size for a RDISK image\n");
                rdisk_log(dev->log, "Image size is not supported by this removable drive\n");
                ret = rdisk_load_abort(dev);
            }

            if (ret) {
                /* The sector count is in the medium's own sector size (bytes_per_sector). */
                dev->drv->medium_type = (uint32_t) medium;
                dev->drv->medium_size = rdisk_medium_blocks((uint32_t) medium);

                if (rdisk_medium_is_read_only(rdisk_drive_type_of(dev), (uint32_t) medium))
                    dev->drv->read_only = 1;
            }
        }

         if (ret) {
             if (fseek(dev->drv->fp, dev->drv->base, SEEK_SET) == -1)
                 log_fatal(dev->log, "rdisk_load(): Error seeking to the beginning of "
                           "the file\n");

             if (dev->drv->image_path != (fn - offs)) {
                 const int len = MIN(strlen(fn - offs), (strlen(dev->drv->image_path) - 1));
                 strncpy(dev->drv->image_path, fn - offs, len);
             }
             /*
                After using strncpy, dev->drv->image_path needs to be explicitly null
                terminated to make gcc happy.
                In the event strlen(dev->drv->image_path) == sizeof(dev->drv->image_path)
                (no null terminator) it is placed at the very end. Otherwise, it is placed
                right after the string.
              */
             const size_t term = strlen(dev->drv->image_path) ==
                 sizeof(dev->drv->image_path) ? sizeof(dev->drv->image_path) - 1 :
                                                strlen(dev->drv->image_path);
             dev->drv->image_path[term] = '\0';
        }
    }

    if (ret && !skip_insert) {
        /* Signal media change to the emulated machine. */
        rdisk_insert((rdisk_t *) dev);

        /* The drive was previously empty, transition directly to UNIT ATTENTION. */
        if (was_empty)
            rdisk_insert((rdisk_t *) dev);
    }

    if (ret)
        ui_sb_update_icon_wp(SB_RDISK | dev->id, dev->drv->read_only);
}

void
rdisk_disk_reload(const rdisk_t *dev)
{
    if (strlen(dev->drv->prev_image_path) != 0)
        (void) rdisk_load(dev, dev->drv->prev_image_path, 0);
}

static void
rdisk_disk_unload(const rdisk_t *dev)
{
    if ((dev->drv != NULL) && (dev->drv->fp != NULL)) {
        fclose(dev->drv->fp);
        dev->drv->fp = NULL;
    }
}

void
rdisk_disk_close(const rdisk_t *dev)
{
    if ((dev->drv != NULL) && (dev->drv->fp != NULL)) {
        rdisk_disk_unload(dev);

        memcpy(dev->drv->prev_image_path, dev->drv->image_path,
               sizeof(dev->drv->image_path));
        memset(dev->drv->image_path, 0, sizeof(dev->drv->image_path));

        dev->drv->medium_size = 0;

        rdisk_insert((rdisk_t *) dev);
    }
}

static void
rdisk_set_callback(const rdisk_t *dev)
{
    /*
     * A parallel-port drive has no IDE channel, so ide_channel is meaningless
     * here and indexing ide_drives with it would set a callback on an unrelated
     * drive. The bridge paces its own transfers.
     */
    if (rdisk_on_ide(dev->drv))
        ide_set_callback(ide_drives[rdisk_ide_channel(dev->drv)], dev->callback);
}

static void
rdisk_init(rdisk_t *dev)
{
    if (dev->id < RDISK_NUM) {
        dev->requested_blocks = 1;
        dev->sense[0]         = 0xf0;
        dev->sense[7]         = 10;
        dev->drv->bus_mode    = 0;
        if ((dev->drv->bus_type >= RDISK_BUS_ATAPI) || rdisk_is_scsi(dev))
            dev->drv->bus_mode |= 2;
        if (!rdisk_is_scsi(dev))
            dev->drv->bus_mode |= 1;
        /* The LS-120 COSM 05 is a PIO-only drive: bit 8 of word 49 of its IDENTIFY data is clear. */
        if (rdisk_on_ide(dev->drv) && IS_LS120_COSM05(dev->drv->type))
            dev->drv->bus_mode &= ~2;
        rdisk_log(dev->log, "Bus type %i, bus mode %i\n", dev->drv->bus_type, dev->drv->bus_mode);
        if (!rdisk_is_scsi(dev)) {
            dev->tf->phase          = 1;
            dev->tf->request_length = 0xEB14;
        }
        dev->tf->status    = READY_STAT | DSC_STAT;
        dev->tf->pos       = 0;
        dev->packet_status = PHASE_NONE;
        rdisk_sense_key = rdisk_asc = rdisk_ascq = dev->unit_attention = dev->transition = 0;
        rdisk_info      = 0x00000000;
        dev->prevent    = 0;
        dev->protect_mode         = 0;
        dev->protect_password_len = 0;
        dev->floptical_unlocked   = 0;
    }
}

static int
rdisk_supports_pio(const rdisk_t *dev)
{
    return (dev->drv->bus_mode & 1);
}

static int
rdisk_supports_dma(const rdisk_t *dev)
{
    return (dev->drv->bus_mode & 2);
}

/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
static int
rdisk_current_mode(const rdisk_t *dev)
{
    if (!rdisk_supports_pio(dev) && !rdisk_supports_dma(dev))
        return 0;
    if (rdisk_supports_pio(dev) && !rdisk_supports_dma(dev)) {
        rdisk_log(dev->log, "Drive does not support DMA, setting to PIO\n");
        return 1;
    }
    if (!rdisk_supports_pio(dev) && rdisk_supports_dma(dev))
        return 2;
    if (rdisk_supports_pio(dev) && rdisk_supports_dma(dev)) {
        rdisk_log(dev->log, "Drive supports both, setting to %s\n",
                (dev->tf->features & 1) ? "DMA" : "PIO");
        return (dev->tf->features & 1) ? 2 : 1;
    }

    return 0;
}

static void
rdisk_mode_sense_load(rdisk_t *dev)
{
    char  fn[512] = { 0 };

    memset(&dev->ms_pages_saved, 0, sizeof(mode_sense_pages_t));

    if (IS_ZIP_100(dev->drv->type)) {
        if (rdisk_is_scsi(dev))
            memcpy(&dev->ms_pages_saved, &zip_100_mode_sense_pages_default_scsi, sizeof(mode_sense_pages_t));
        else
            memcpy(&dev->ms_pages_saved, &zip_100_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (dev->drv->type == RDISK_TYPE_JAZ_1GB) {
        memcpy(&dev->ms_pages_saved, &jaz_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (dev->drv->type == RDISK_TYPE_JAZ_2GB) {
        memcpy(&dev->ms_pages_saved, &jaz_2gb_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (dev->drv->type == RDISK_TYPE_CLIK_40) {
        memcpy(&dev->ms_pages_saved, &clik_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (dev->drv->type == RDISK_TYPE_ZIP_750) {
        memcpy(&dev->ms_pages_saved, &zip_750_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (dev->drv->type == RDISK_TYPE_HIFD_200) {
        memcpy(&dev->ms_pages_saved, &hifd_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (dev->drv->type == RDISK_TYPE_FLOPTICAL_20) {
        memcpy(&dev->ms_pages_saved, &floptical_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (dev->drv->type == RDISK_TYPE_FLOPTICAL_IO20S) {
        memcpy(&dev->ms_pages_saved, &floptical_io20s_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (dev->drv->type == RDISK_TYPE_BERNOULLI_90) {
        memcpy(&dev->ms_pages_saved, &bernoulli_90_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (IS_BERNOULLI_150(dev->drv->type)) {
        memcpy(&dev->ms_pages_saved, &bernoulli_150_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (dev->drv->type == RDISK_TYPE_BERNOULLI_230) {
        memcpy(&dev->ms_pages_saved, &bernoulli_230_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (IS_BERNOULLI_II(dev->drv->type)) {
        memcpy(&dev->ms_pages_saved, &bernoulli_44_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (IS_LS120_COSM05(dev->drv->type)) {
        memcpy(&dev->ms_pages_saved, &superdisk_120_cosm05_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (IS_SUPERDISK_120(dev->drv->type)) {
        memcpy(&dev->ms_pages_saved, &superdisk_120_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (IS_SUPERDISK_240(dev->drv->type)) {
        memcpy(&dev->ms_pages_saved, &superdisk_240_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (rdisk_find_ms_set(dev->drv->type) != NULL) {
        memcpy(&dev->ms_pages_saved, rdisk_find_ms_set(dev->drv->type)->def, sizeof(mode_sense_pages_t));
    } else if (IS_SYJET(dev->drv->type)) {
        if (rdisk_is_scsi(dev))
            memcpy(&dev->ms_pages_saved, &syjet_mode_sense_pages_default_scsi, sizeof(mode_sense_pages_t));
        else
            memcpy(&dev->ms_pages_saved, &syjet_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else if (dev->drv->type == RDISK_TYPE_SPARQ_1GB) {
        if (rdisk_is_scsi(dev))
            memcpy(&dev->ms_pages_saved, &sparq_mode_sense_pages_default_scsi, sizeof(mode_sense_pages_t));
        else
            memcpy(&dev->ms_pages_saved, &sparq_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    } else {
        if (rdisk_is_scsi(dev))
            memcpy(&dev->ms_pages_saved, &zip_250_mode_sense_pages_default_scsi, sizeof(mode_sense_pages_t));
        else
            memcpy(&dev->ms_pages_saved, &zip_250_mode_sense_pages_default, sizeof(mode_sense_pages_t));
    }

    if (rdisk_is_scsi(dev))
        sprintf(fn, "scsi_rdisk_%02i_mode_sense_bin", dev->id);
    else
        sprintf(fn, "rdisk_%02i_mode_sense_bin", dev->id);

    FILE *fp = plat_fopen(nvr_path(fn), "rb");
    if (fp) {
        /* Nothing to read, not used by RDISK. */
        fclose(fp);
    }
}

static void
rdisk_mode_sense_save(const rdisk_t *dev)
{
    char  fn[512] = { 0 };

    if (rdisk_is_scsi(dev))
        sprintf(fn, "scsi_rdisk_%02i_mode_sense_bin", dev->id);
    else
        sprintf(fn, "rdisk_%02i_mode_sense_bin", dev->id);
    FILE *fp = plat_fopen(nvr_path(fn), "wb");
    if (fp) {
        /* Nothing to write, not used by RDISK. */
        fclose(fp);
    }
}

static uint64_t
rdisk_mode_sense_page_flags(const rdisk_t *dev)
{
    if (IS_ZIP_100(dev->drv->type))
        return zip_100_mode_sense_page_flags;
    else if (dev->drv->type == RDISK_TYPE_JAZ_1GB)
        return jaz_mode_sense_page_flags;
    else if (dev->drv->type == RDISK_TYPE_JAZ_2GB)
        return jaz_2gb_mode_sense_page_flags;
    else if (dev->drv->type == RDISK_TYPE_CLIK_40)
        return clik_mode_sense_page_flags;
    else if (dev->drv->type == RDISK_TYPE_HIFD_200)
        return hifd_mode_sense_page_flags;
    else if (IS_FLOPTICAL(dev->drv->type))
        return floptical_mode_sense_page_flags;
    else if (dev->drv->type == RDISK_TYPE_BERNOULLI_90)
        return bernoulli_90_mode_sense_page_flags;
    else if (IS_BERNOULLI_150(dev->drv->type))
        return bernoulli_150_mode_sense_page_flags;
    else if (dev->drv->type == RDISK_TYPE_BERNOULLI_230)
        return bernoulli_230_mode_sense_page_flags;
    else if (dev->drv->type == RDISK_TYPE_BERNOULLI_20)
        return bernoulli_20_mode_sense_page_flags;
    else if (dev->drv->type == RDISK_TYPE_BERNOULLI_44)
        return bernoulli_44_mode_sense_page_flags;
    else if (IS_SUPERDISK_120(dev->drv->type))
        return superdisk_120_mode_sense_page_flags;
    else if (IS_SUPERDISK_240(dev->drv->type))
        return superdisk_240_mode_sense_page_flags;
    else if (rdisk_find_ms_set(dev->drv->type) != NULL)
        return rdisk_find_ms_set(dev->drv->type)->flags;
    else if (IS_SYJET(dev->drv->type))
        return syjet_mode_sense_page_flags;
    else if (dev->drv->type == RDISK_TYPE_SPARQ_1GB)
        return sparq_mode_sense_page_flags;
    else
        return zip_250_mode_sense_page_flags;
}

static const mode_sense_pages_t *
rdisk_mode_sense_pages_changeable(const rdisk_t *dev)
{
    if (IS_ZIP_100(dev->drv->type))
        return &zip_100_mode_sense_pages_changeable;
    else if (dev->drv->type == RDISK_TYPE_JAZ_1GB)
        return &jaz_mode_sense_pages_changeable;
    else if (dev->drv->type == RDISK_TYPE_JAZ_2GB)
        return &jaz_2gb_mode_sense_pages_changeable;
    else if (dev->drv->type == RDISK_TYPE_CLIK_40)
        return &clik_mode_sense_pages_changeable;
    else if (dev->drv->type == RDISK_TYPE_HIFD_200)
        return &hifd_mode_sense_pages_changeable;
    else if (dev->drv->type == RDISK_TYPE_FLOPTICAL_20)
        return &floptical_mode_sense_pages_changeable;
    else if (dev->drv->type == RDISK_TYPE_FLOPTICAL_IO20S)
        return &floptical_io20s_mode_sense_pages_changeable;
    else if (IS_BERNOULLI_150(dev->drv->type))
        return &bernoulli_150_mode_sense_pages_changeable;
    else if (IS_BERNOULLI(dev->drv->type))
        return &bernoulli_mode_sense_pages_changeable;
    else if (IS_SUPERDISK(dev->drv->type))
        return &superdisk_mode_sense_pages_changeable;
    else if (rdisk_find_ms_set(dev->drv->type) != NULL)
        return rdisk_find_ms_set(dev->drv->type)->chg;
    else if (IS_SYJET(dev->drv->type))
        return &syjet_mode_sense_pages_changeable;
    else if (dev->drv->type == RDISK_TYPE_SPARQ_1GB)
        return &sparq_mode_sense_pages_changeable;
    else
        return &zip_250_mode_sense_pages_changeable;
}

/* Zip 750 with a Zip 100 or Zip 250 medium inserted: report that medium's cylinder count
   (ESTIMATED, mirrors what the Zip 250 does with Zip 100 media).  Returns -1 if not applicable. */
static int
zip_750_media_geometry(const rdisk_t *dev, const uint8_t page, const uint8_t pos)
{
    if ((dev->drv->type == RDISK_TYPE_ZIP_750) && (page == 5)) {
        if (dev->drv->medium_size == ZIP_SECTORS) {
            if (pos == 8)
                return 0x00;
            if (pos == 9)
                return 0x60;    /* 96 cylinders */
        } else if (dev->drv->medium_size == ZIP_250_SECTORS) {
            if (pos == 8)
                return 0x00;
            if (pos == 9)
                return 0xef;    /* 239 cylinders */
        }
    }

    return -1;
}

/* The geometry of a PC floppy format, and the transfer and rotation rates of a standard floppy drive (0 if they
   are not known).  Returns 0 if the medium that is in the drive is not one of those formats. */
static int
rdisk_floppy_format(const rdisk_t *dev, uint16_t *cyl, uint8_t *heads, uint8_t *spt, uint16_t *bps,
                    uint16_t *rate, uint16_t *rpm)
{
    *heads = 2;
    *bps   = 512;
    *rate  = 0;
    *rpm   = 0;

    if (dev->drv->fp == NULL)
        return 0;

    switch (dev->drv->medium_type) {
        case RDISK_MEDIUM_FD_1440K:    /* 1.44 MB */
            *rate = 500;
            *cyl  = 80;
            *rpm  = 300;
            *spt  = 18;
            break;
        case RDISK_MEDIUM_FD_640K:     /* 640 KB: addressed as a 720 KB medium, 9 sectors per track */
        case RDISK_MEDIUM_FD_720K:     /* 720 KB */
            *rate = 250;
            *cyl  = 80;
            *rpm  = 300;
            *spt  = 9;
            break;
        case RDISK_MEDIUM_FD_1200K:    /* 1.2 MB, 15 sectors per track (ESTIMATED: standard 5.25" HD rates) */
            *rate = 500;
            *cyl  = 80;
            *rpm  = 360;
            *spt  = 15;
            break;
        case RDISK_MEDIUM_FD_DMF:      /* DMF, 21 sectors per track (ESTIMATED) */
            *rate = 500;
            *cyl  = 80;
            *rpm  = 300;
            *spt  = 21;
            break;
        case RDISK_MEDIUM_FD_2HD_NEC:  /* 1.2 MB, 1024-byte sectors */
            *rate = 500;
            *cyl  = 77;
            *bps  = 1024;
            *rpm  = 360;
            *spt  = 8;
            break;
        case RDISK_MEDIUM_FD32MB:      /* 32 MB: 1024 cylinders * 2 heads * 32 sectors */
            *cyl = 1024;
            *spt = 32;
            break;
        default:
            return 0;
    }

    return 1;
}

/* SuperDisk, Floptical and Caleb drives also take ordinary PC floppy media.  With one inserted,
   page 5 (flexible disk) reports the geometry of that format: the standard PC geometry of the 720 KB,
   1.44 MB and NEC 2HD (1.2 MB, 1024-byte sectors) media (ESTIMATED; the reports only captured the drives'
   own media) and 1024 cylinders x 2 heads x 32 sectors for the FD32MB, whose transfer and rotation rates
   are not known and are left alone.

   A real LS-120 COSM 05 with a 1.44 MB disk has the geometry above, but its spindle stays at 720 rpm: page 5
   reports 720 rpm and 1200 kbit/s, which is the disk's 500 kbit/s at 300 rpm scaled to that speed.  The other
   formats are scaled the same way (ESTIMATED).  Its page 3 changes too: tracks per zone is the number of tracks
   of the disk, sectors per track and bytes per sector are the disk's, and the track skew becomes 0.

   Returns -1 if not applicable. */
static int
rdisk_floppy_media_geometry(const rdisk_t *dev, const uint8_t page, const uint8_t pos)
{
    const int ls120 = IS_SUPERDISK_120(dev->drv->type);
    uint16_t  rate;
    uint16_t  rpm;
    uint16_t  cyl;
    uint16_t  bps;
    uint8_t   heads;
    uint8_t   spt;

    if (!((page == 5) || (ls120 && (page == 3))) ||
        !(IS_SUPERDISK(dev->drv->type) || IS_FLOPTICAL(dev->drv->type) || (dev->drv->type == RDISK_TYPE_UHD144)) ||
        !rdisk_floppy_format(dev, &cyl, &heads, &spt, &bps, &rate, &rpm))
        return -1;

    if (page == 3) {
        const uint16_t tracks = cyl * heads;

        switch (pos) {
            case 2:
                return tracks >> 8;
            case 3:
                return tracks & 0xff;
            case 10:
                return spt >> 8;
            case 11:
                return spt;
            case 12:
                return bps >> 8;
            case 13:
                return bps & 0xff;
            case 16:
            case 17:
                return 0;
            default:
                return -1;
        }
    }

    if (ls120 && rpm) {
        rate = (uint16_t) ((rate * 720U) / rpm);
        rpm  = 720;
    }

    switch (pos) {
        case 2:
            return rate ? (rate >> 8) : -1;
        case 3:
            return rate ? (rate & 0xff) : -1;
        case 4:
            return heads;
        case 5:
            return spt;
        case 6:
            return bps >> 8;
        case 7:
            return bps & 0xff;
        case 8:
            return cyl >> 8;
        case 9:
            return cyl & 0xff;
        case 28:
            return rpm ? (rpm >> 8) : -1;
        case 29:
            return rpm ? (rpm & 0xff) : -1;
        default:
            return -1;
    }
}

/* SCSI Mode Sense 6/10. */
static uint8_t
rdisk_mode_sense_read(const rdisk_t *dev, const uint8_t pgctl,
                    const uint8_t page, const uint8_t pos)
{
    switch (pgctl) {
        case 0:
        case 3:
            if (rdisk_floppy_media_geometry(dev, page, pos) >= 0)
                return (uint8_t) rdisk_floppy_media_geometry(dev, page, pos);
            /* An LS-240 drive with LS-120 media inserted reports the LS-120 geometry
               (963 cylinders, 8 heads, 32 sectors per track). */
            if ((IS_SUPERDISK_240(dev->drv->type)) &&
                (dev->drv->medium_size == SUPERDISK_SECTORS)) {
                if (page == 5) {
                    switch (pos) {
                        case 4:
                            return 0x08;
                        case 5:
                            return 0x20;
                        case 8:
                            return 0x03;
                        case 9:
                            return 0xc3;
                        default:
                            break;
                    }
                }
            }
            if (zip_750_media_geometry(dev, page, pos) >= 0)
                return (uint8_t) zip_750_media_geometry(dev, page, pos);
            if (!IS_SUPERDISK(dev->drv->type) && !IS_ZIP_100(dev->drv->type) &&
                (page == 5) && (pos == 9) && (dev->drv->medium_size == ZIP_SECTORS))
                return 0x60;
            return dev->ms_pages_saved.pages[page][pos];
        case 1:
            return rdisk_mode_sense_pages_changeable(dev)->pages[page][pos];
        case 2:
            if (IS_ZIP_100(dev->drv->type)) {
                if (rdisk_is_scsi(dev))
                    return zip_100_mode_sense_pages_default_scsi.pages[page][pos];
                else
                    return zip_100_mode_sense_pages_default.pages[page][pos];
            } else if (dev->drv->type == RDISK_TYPE_JAZ_1GB) {
                return jaz_mode_sense_pages_default.pages[page][pos];
            } else if (dev->drv->type == RDISK_TYPE_JAZ_2GB) {
                return jaz_2gb_mode_sense_pages_default.pages[page][pos];
            } else if (dev->drv->type == RDISK_TYPE_CLIK_40) {
                return clik_mode_sense_pages_default.pages[page][pos];
            } else if (dev->drv->type == RDISK_TYPE_ZIP_750) {
                return zip_750_mode_sense_pages_default.pages[page][pos];
            } else if (dev->drv->type == RDISK_TYPE_HIFD_200) {
                return hifd_mode_sense_pages_default.pages[page][pos];
            } else if (dev->drv->type == RDISK_TYPE_FLOPTICAL_20) {
                return floptical_mode_sense_pages_default.pages[page][pos];
            } else if (dev->drv->type == RDISK_TYPE_FLOPTICAL_IO20S) {
                return floptical_io20s_mode_sense_pages_default.pages[page][pos];
            } else if (dev->drv->type == RDISK_TYPE_BERNOULLI_90) {
                return bernoulli_90_mode_sense_pages_default.pages[page][pos];
            } else if (IS_BERNOULLI_150(dev->drv->type)) {
                return bernoulli_150_mode_sense_pages_default.pages[page][pos];
            } else if (dev->drv->type == RDISK_TYPE_BERNOULLI_230) {
                return bernoulli_230_mode_sense_pages_default.pages[page][pos];
            } else if (IS_BERNOULLI_II(dev->drv->type)) {
                return bernoulli_44_mode_sense_pages_default.pages[page][pos];
            } else if (IS_LS120_COSM05(dev->drv->type)) {
                return superdisk_120_cosm05_mode_sense_pages_default.pages[page][pos];
            } else if (IS_SUPERDISK_120(dev->drv->type)) {
                return superdisk_120_mode_sense_pages_default.pages[page][pos];
            } else if (IS_SUPERDISK_240(dev->drv->type)) {
                return superdisk_240_mode_sense_pages_default.pages[page][pos];
            } else if (rdisk_find_ms_set(dev->drv->type) != NULL) {
                return rdisk_find_ms_set(dev->drv->type)->def->pages[page][pos];
            } else if (IS_SYJET(dev->drv->type)) {
                if (rdisk_is_scsi(dev))
                    return syjet_mode_sense_pages_default_scsi.pages[page][pos];
                else
                    return syjet_mode_sense_pages_default.pages[page][pos];
            } else if (dev->drv->type == RDISK_TYPE_SPARQ_1GB) {
                if (rdisk_is_scsi(dev))
                    return sparq_mode_sense_pages_default_scsi.pages[page][pos];
                else
                    return sparq_mode_sense_pages_default.pages[page][pos];
            } else {
                if ((page == 5) && (pos == 9) && (dev->drv->medium_size == ZIP_SECTORS))
                    return 0x60;
                if (rdisk_is_scsi(dev))
                    return zip_250_mode_sense_pages_default_scsi.pages[page][pos];
                else
                    return zip_250_mode_sense_pages_default.pages[page][pos];
            }

        default:
            break;
    }

    return 0;
}

/* The "medium type" byte of the MODE SENSE parameter header (byte 1 of the 6-byte form, byte 2 of the
   10-byte form).  Real SuperDisk and HiFD drives report the type of the medium that is loaded here, and
   Matsushita's SuperDisk format utility (MKELSFMT.EXE, through MKEDEVIO.DLL) refuses to see a drive unless
   this byte is non-zero ("No SuperDisk drive is connected to your computer, or no disk is in the drive").
   The codes are the "Type" values of the media in Aaru device reports 66 and 67 (LS-120, LS-240) and 118
   (HiFD).  Empty drive: 0x00 (no medium).  Drives whose real value was not reported (Zip, Jaz, Clik!,
   Floptical, UHD144...) keep reporting 0x00, as Aaru shows for the Iomega ones. */
static uint8_t
rdisk_mode_sense_medium_type(const rdisk_t *dev)
{
    /* Iomega's Io20S: 1Eh, 1Fh and 20h, with bit 7 for a write-protected disk, are what SCSIUTIL.EXE tells its three
       media apart by.  This is its only test of the medium, so the other two are in the same order. */
    if ((dev->drv->fp != NULL) && (dev->drv->type == RDISK_TYPE_FLOPTICAL_IO20S)) {
        const uint8_t wp = dev->drv->read_only ? 0x80 : 0x00;

        switch (dev->drv->medium_type) {
            case RDISK_MEDIUM_FD_720K:
                return 0x1e | wp;
            case RDISK_MEDIUM_FD_1440K:
                return 0x1f | wp;
            default:
                return 0x20 | wp;
        }
    }

    if ((dev->drv->fp == NULL) ||
        !(IS_SUPERDISK(dev->drv->type) || (dev->drv->type == RDISK_TYPE_HIFD_200)))
        return 0x00;

    switch (dev->drv->medium_type) {
        case RDISK_MEDIUM_SUPERDISK_120:
            return 0x31;    /* SuperDisk 120MB (Aaru reports 66, 67) */
        case RDISK_MEDIUM_SUPERDISK_240:
            return 0x32;    /* SuperDisk 240MB (Aaru report 67) */
        case RDISK_MEDIUM_FD_2HD_NEC:
            return 0x22;    /* NEC 2HD 1.2 MB (Aaru reports 66, 67) */
        case RDISK_MEDIUM_FD_720K:
            return 0x11;    /* MFD-2DD 720 KB (Aaru reports 66, 67) */
        case RDISK_MEDIUM_FD_640K:
            /* The drive cannot tell a 640 KB floppy from a 720 KB one: MKELS120 sees 11h and turns it into its
               640 KB profile (12h) when the BPB says 8 sectors per track (LS-120 specification, section 5). */
            return 0x11;
        case RDISK_MEDIUM_FD_1200K:
            return 0x23;    /* MKELS120 profile 23h, from the LS-120 specification (section 5) */
        case RDISK_MEDIUM_FD_DMF:
            return 0x26;    /* MKELS120 profile 26h (DMF), from the LS-120 specification (section 5) */
        case RDISK_MEDIUM_FD_1440K:
            return 0x24;    /* MFD-2HD 1.44 MB (Aaru reports 66, 67) */
        case RDISK_MEDIUM_FD32MB:
            return 0x28;    /* Imation FD32MB (Aaru report 67) */
        case RDISK_MEDIUM_HIFD_200:
            return 0x42;    /* HiFD (Aaru report 118) */
        case RDISK_MEDIUM_HIFD_150:
            return 0x40;    /* ESTIMATED: page 0B of report 118 lists 0x40 and 0x42 */
        default:
            return 0x00;
    }
}

static uint32_t
rdisk_mode_sense(const rdisk_t *dev, uint8_t *buf, uint32_t pos,
               uint8_t page, const uint8_t block_descriptor_len)
{
    uint64_t       pf;
    const uint8_t  pgctl = (page >> 6) & 3;

    pf = rdisk_mode_sense_page_flags(dev);

    page &= 0x3f;

    if (block_descriptor_len) {
        /* The Bernoulli 150's block descriptor has only the block length, the number of blocks is 0, and so does
           the one that Iomega's imgatapi.sys makes up for the Zip 100 on its native ATA interface. */
        const uint32_t blocks = (IS_BERNOULLI_150(dev->drv->type) || (IS_ZIP_100(dev->drv->type))) ?
                                0 : dev->drv->medium_size;

        buf[pos++] = ((blocks >> 24) & 0xff);
        buf[pos++] = ((blocks >> 16) & 0xff);
        buf[pos++] = ((blocks >> 8) & 0xff);
        buf[pos++] = (blocks & 0xff);
        buf[pos++] = 0; /* Reserved. */
        buf[pos++] = (rdisk_sector_size(dev) >> 16) & 0xff;    /* Block length. */
        buf[pos++] = (rdisk_sector_size(dev) >> 8) & 0xff;
        buf[pos++] = rdisk_sector_size(dev) & 0xff;
    }

    /* Only the pages the drive has: MODE SENSE of all pages returns those and nothing else (a real LS-120 COSM 05
       returns exactly its five pages).  A page without a length, like page 0, only returns the header. */
    for (uint8_t i = 0; i < GPMODE_ALL_PAGES; i++) {
        if ((page == GPMODE_ALL_PAGES) || (page == i)) {
            const uint8_t msplen = rdisk_mode_sense_read(dev, pgctl, i, 1);

            if ((pf & (1ULL << i)) && msplen) {
                buf[pos++]           = rdisk_mode_sense_read(dev, pgctl, i, 0);
                buf[pos++]           = msplen;
                rdisk_log(dev->log, "MODE SENSE: Page [%02X] length %i\n", i, msplen);
                for (uint8_t j = 0; j < msplen; j++)
                    buf[pos++] = rdisk_mode_sense_read(dev, pgctl, i, 2 + j);
            }
        }
    }

    return pos;
}

static void
rdisk_update_request_length(rdisk_t *dev, int len, int block_len)
{
    int bt;
    int min_len = 0;

    dev->max_transfer_len = dev->tf->request_length;

    /*
       For media access commands, make sure the requested DRQ length matches the
       block length.
     */
    switch (dev->current_cdb[0]) {
        case 0x08:
        case 0x0a:
        case 0x28:
        case 0x2a:
        case 0xa8:
        case 0xaa:
            /* Round it to the nearest 2048 bytes. */
            if (block_len > 0)
                dev->max_transfer_len = (dev->max_transfer_len / block_len) * block_len;

            /*
               Make sure total length is not bigger than sum of the lengths of
               all the requested blocks.
             */
            bt = (dev->requested_blocks * block_len);
            if (len > bt)
                len = bt;

            min_len = block_len;

            if (len <= block_len) {
                /* Total length is less or equal to block length. */
                if (dev->max_transfer_len < block_len) {
                    /* Transfer a minimum of (block size) bytes. */
                    dev->max_transfer_len = block_len;
                    dev->packet_len       = block_len;
                    break;
                }
            }
            fallthrough;

        default:
            dev->packet_len = len;
            break;
    }
    /*
       If the DRQ length is odd, and the total remaining length is bigger,
       make sure it's even.
     */
    if ((dev->max_transfer_len & 1) && (dev->max_transfer_len < len))
        dev->max_transfer_len &= 0xfffe;
    /*
       If the DRQ length is smaller or equal in size to the total remaining length,
       set it to that.
     */
    if (!dev->max_transfer_len)
        dev->max_transfer_len = 65534;

    if ((len <= dev->max_transfer_len) && (len >= min_len))
        dev->tf->request_length = dev->max_transfer_len = len;
    else if (len > dev->max_transfer_len)
        dev->tf->request_length = dev->max_transfer_len;

    return;
}

static double
rdisk_bus_speed(rdisk_t *dev)
{
    double ret = -1.0;

    if (dev && dev->drv && rdisk_on_ide(dev->drv))
        ret = ide_atapi_get_period(rdisk_ide_channel(dev->drv));

    if (ret == -1.0) {
        if (dev)
            dev->callback = -1.0;
        ret = 0.0;
    }

    return ret;
}

static void
rdisk_command_common(rdisk_t *dev)
{
    dev->tf->status = BUSY_STAT;
    dev->tf->phase  = 1;
    dev->tf->pos    = 0;
    if (dev->packet_status == PHASE_COMPLETE)
        dev->callback = 0.0;
    else if (rdisk_is_scsi(dev))
        dev->callback = -1.0; /* Speed depends on SCSI controller */
    else
        dev->callback = rdisk_bus_speed(dev) * (double) (dev->packet_len);

    rdisk_set_callback(dev);
}

static void
rdisk_command_complete(rdisk_t *dev)
{
    dev->packet_status = PHASE_COMPLETE;
    rdisk_command_common(dev);
}

static void
rdisk_command_read(rdisk_t *dev)
{
    dev->packet_status = PHASE_DATA_IN;
    rdisk_command_common(dev);
}

static void
rdisk_command_read_dma(rdisk_t *dev)
{
    dev->packet_status = PHASE_DATA_IN_DMA;
    rdisk_command_common(dev);
}

static void
rdisk_command_write(rdisk_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT;
    rdisk_command_common(dev);
}

static void
rdisk_command_write_dma(rdisk_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT_DMA;
    rdisk_command_common(dev);
}

/*
   dev = Pointer to current RDISK device;
   len = Total transfer length;
   block_len = Length of a single block (why does it matter?!);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host).
 */
static void
rdisk_data_command_finish(rdisk_t *dev, int len, const int block_len,
                        const int alloc_len, const int direction)
{
    rdisk_log(dev->log, "Finishing command (%02X): %i, %i, %i, %i, %i\n",
            dev->current_cdb[0], len, block_len, alloc_len,
            direction, dev->tf->request_length);
    dev->tf->pos = 0;
    if (alloc_len >= 0) {
        if (alloc_len < len)
            len = alloc_len;
    }
    if ((len == 0) || (rdisk_current_mode(dev) == 0)) {
        if (!rdisk_is_scsi(dev))
            dev->packet_len = 0;

        rdisk_command_complete(dev);
    } else {
        if (rdisk_current_mode(dev) == 2) {
            if (!rdisk_is_scsi(dev))
                dev->packet_len = alloc_len;

            if (direction == 0)
                rdisk_command_read_dma(dev);
            else
                rdisk_command_write_dma(dev);
        } else {
            rdisk_update_request_length(dev, len, block_len);
            if (!rdisk_is_scsi(dev) &&
                (dev->tf->request_length == 0))
                rdisk_command_complete(dev);
            else if (direction == 0)
                rdisk_command_read(dev);
            else
                rdisk_command_write(dev);
        }
    }

    rdisk_log(dev->log, "Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n",
            dev->packet_status, dev->tf->request_length, dev->packet_len,
            dev->tf->pos, dev->tf->phase);
}

static void
rdisk_sense_clear(rdisk_t *dev, UNUSED(int command))
{
    rdisk_sense_key = rdisk_asc = rdisk_ascq = 0;
    rdisk_info      = 0x00000000;
}

static void
rdisk_set_phase(const rdisk_t *dev, const uint8_t phase)
{
    if (rdisk_is_scsi(dev))
        rdisk_scsi_device(dev)->phase = phase;
}

static void
rdisk_cmd_error(rdisk_t *dev)
{
    rdisk_set_phase(dev, SCSI_PHASE_STATUS);
    dev->tf->error = ((rdisk_sense_key & 0xf) << 4) | ABRT_ERR;
    dev->tf->status    = READY_STAT | ERR_STAT;
    dev->tf->phase     = 3;
    dev->tf->pos       = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback      = 50.0 * RDISK_TIME;
    rdisk_set_callback(dev);
    ui_sb_update_icon(SB_RDISK | dev->id, 0);
    ui_sb_update_icon_write(SB_RDISK | dev->id, 0);
    rdisk_log(dev->log, "[%02X] ERROR: %02X/%02X/%02X\n", dev->current_cdb[0], rdisk_sense_key,
            rdisk_asc, rdisk_ascq);
}

static void
rdisk_unit_attention(rdisk_t *dev)
{
    rdisk_set_phase(dev, SCSI_PHASE_STATUS);
    dev->tf->error = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
    dev->tf->status    = READY_STAT | ERR_STAT;
    dev->tf->phase     = 3;
    dev->tf->pos       = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback      = 50.0 * RDISK_TIME;
    rdisk_set_callback(dev);
    ui_sb_update_icon(SB_RDISK | dev->id, 0);
    ui_sb_update_icon_write(SB_RDISK | dev->id, 0);
    rdisk_log(dev->log, "UNIT ATTENTION\n", dev->id);
}

static void
rdisk_buf_alloc(rdisk_t *dev, const uint32_t len)
{
    rdisk_log(dev->log, "Allocated buffer length: %i\n", len);

    if (dev->buffer == NULL) {
        dev->buffer = (uint8_t *) calloc(1, len);
        dev->buffer_sz = len;
    }

    if (len > dev->buffer_sz) {
        uint8_t *buf = (uint8_t *) realloc(dev->buffer, len);
        dev->buffer = buf;
        dev->buffer_sz = len;
    }
}

static void
rdisk_buf_free(rdisk_t *dev)
{
    if (dev->buffer != NULL) {
        rdisk_log(dev->log, "Removable Disk %i: Freeing buffer...\n");
        free(dev->buffer);
        dev->buffer = NULL;
    }
}

/* The value of the information field of the sense data, which is big-endian there. */
static uint32_t
rdisk_sense_info(const uint32_t val)
{
    return (val >> 24) | ((val >> 8) & 0x0000ff00) | ((val << 8) & 0x00ff0000) | (val << 24);
}

static void
rdisk_bus_master_error(scsi_common_t *sc)
{
    rdisk_t *dev = (rdisk_t *) sc;

    rdisk_buf_free(dev);
    rdisk_sense_key = rdisk_asc = rdisk_ascq = 0;
    rdisk_info      = rdisk_sense_info(dev->sector_pos);
    rdisk_cmd_error(dev);
}

static void
rdisk_not_ready(rdisk_t *dev)
{
    rdisk_sense_key = SENSE_NOT_READY;
    rdisk_asc       = ASC_MEDIUM_NOT_PRESENT;
    rdisk_ascq      = 0;
    rdisk_info      = 0x00000000;
    rdisk_cmd_error(dev);
}

/* A write to a Floptical that has not been unlocked, see the description of its pages. */
static int
rdisk_floptical_locked(const rdisk_t *dev)
{
    return IS_FLOPTICAL(dev->drv->type) && !dev->floptical_unlocked;
}

static void
rdisk_floptical_needs_unlock(rdisk_t *dev)
{
    rdisk_sense_key = SENSE_NOT_READY;
    rdisk_asc       = RDISK_ASC_NOT_READY;
    rdisk_ascq      = 0x02;
    rdisk_info      = 0x00000000;
    rdisk_cmd_error(dev);
}

static void
rdisk_write_protected(rdisk_t *dev)
{
    /* The LS-120 reports DATA PROTECT / ASC 27h, the only pair that MKELS120 translates to its write-protect
       result (LS-120 specification, section 8.1, test ERR-02), and so does the Bernoulli 150 (the sense table
       of its ROM).  So do the Zip and Jaz: Iomega's imgatapi.sys reports the ATA write-protect error as DATA PROTECT,
       and Windows NT only takes that key for a write-protected medium.  The other drives keep the UNIT ATTENTION
       they always reported. */
    rdisk_sense_key = rdisk_reports_write_protect(dev) ? RDISK_SENSE_DATA_PROTECT : SENSE_UNIT_ATTENTION;
    rdisk_asc       = ASC_WRITE_PROTECTED;
    rdisk_ascq      = 0;
    rdisk_info      = rdisk_sense_info(dev->sector_pos);
    rdisk_cmd_error(dev);
}

static void
rdisk_write_error(rdisk_t *dev)
{
    rdisk_sense_key = SENSE_MEDIUM_ERROR;
    rdisk_asc       = ASC_WRITE_ERROR;
    rdisk_ascq      = 0;
    rdisk_info      = rdisk_sense_info(dev->sector_pos);
    rdisk_cmd_error(dev);
}

static void
rdisk_read_error(rdisk_t *dev)
{
    rdisk_sense_key = SENSE_MEDIUM_ERROR;
    rdisk_asc       = ASC_UNRECOVERED_READ_ERROR;
    rdisk_ascq      = 0;
    rdisk_info      = rdisk_sense_info(dev->sector_pos);
    rdisk_cmd_error(dev);
}

static void
rdisk_invalid_lun(rdisk_t *dev, const uint8_t lun)
{
    rdisk_sense_key = SENSE_ILLEGAL_REQUEST;
    rdisk_asc       = ASC_INV_LUN;
    rdisk_ascq      = 0;
    rdisk_info      = (uint32_t) lun << 24;
    rdisk_cmd_error(dev);
}

static void
rdisk_illegal_opcode(rdisk_t *dev, const uint8_t opcode)
{
    rdisk_sense_key = SENSE_ILLEGAL_REQUEST;
    rdisk_asc       = ASC_ILLEGAL_OPCODE;
    rdisk_ascq      = 0;
    rdisk_info      = (uint32_t) opcode << 24;
    rdisk_cmd_error(dev);
}

static void
rdisk_lba_out_of_range(rdisk_t *dev)
{
    rdisk_sense_key = SENSE_ILLEGAL_REQUEST;
    rdisk_asc       = ASC_LBA_OUT_OF_RANGE;
    rdisk_ascq      = 0;
    rdisk_info      = rdisk_sense_info(dev->sector_pos);
    rdisk_cmd_error(dev);
}

static void
rdisk_invalid_field(rdisk_t *dev, const uint32_t field)
{
    rdisk_sense_key = SENSE_ILLEGAL_REQUEST;
    rdisk_asc       = ASC_INV_FIELD_IN_CMD_PACKET;
    rdisk_ascq      = 0;
    rdisk_info      = rdisk_sense_info(field);
    rdisk_cmd_error(dev);
    dev->tf->status = 0x53;
}

static void
rdisk_invalid_field_pl(rdisk_t *dev, const uint32_t field)
{
    rdisk_sense_key = SENSE_ILLEGAL_REQUEST;
    rdisk_asc       = ASC_INV_FIELD_IN_PARAMETER_LIST;
    rdisk_ascq      = 0;
    rdisk_info      = rdisk_sense_info(field);
    rdisk_cmd_error(dev);
    dev->tf->status = 0x53;
}

/* The Bernoulli 150 has no saved mode pages: MODE SENSE with a page control of 3 fails. */
static void
rdisk_saving_not_supported(rdisk_t *dev)
{
    rdisk_sense_key = SENSE_ILLEGAL_REQUEST;
    rdisk_asc       = RDISK_ASC_SAVING_NOT_SUPPORTED;
    rdisk_ascq      = 0;
    rdisk_info      = 0x00000000;
    rdisk_cmd_error(dev);
}

/* What the Bernoulli 150's READ CAPACITY reports without a cartridge, which has no format information to read:
   it does not go through the NOT READY check that other commands do. */
static void
rdisk_no_format_information(rdisk_t *dev)
{
    rdisk_sense_key = SENSE_MEDIUM_ERROR;
    rdisk_asc       = RDISK_ASC_DEFECT_LIST_NOT_FOUND;
    rdisk_ascq      = 0;
    rdisk_info      = 0x00000000;
    rdisk_cmd_error(dev);
}

static void
rdisk_data_phase_error(rdisk_t *dev, const uint32_t info)
{
    rdisk_sense_key = SENSE_ILLEGAL_REQUEST;
    rdisk_asc       = ASC_DATA_PHASE_ERROR;
    rdisk_ascq      = 0;
    rdisk_info      = rdisk_sense_info(info);
    rdisk_cmd_error(dev);
}

static int
rdisk_blocks(rdisk_t *dev, int32_t *len, const int out)
{
    int ret = 1;
    *len    = 0;

    if (dev->sector_len > 0) {
        rdisk_log(dev->log, "%sing %i blocks starting from %i...\n", out ? "Writ" : "Read",
                dev->requested_blocks, dev->sector_pos);

        if (dev->sector_pos >= dev->drv->medium_size) {
            rdisk_log(dev->log, "Trying to %s beyond the end of disk\n",
                    out ? "write" : "read");
            rdisk_lba_out_of_range(dev);
            ret = 0;
        } else {
            const uint32_t bps = rdisk_sector_size(dev);

            *len    = dev->requested_blocks * bps;

            for (int i = 0; i < dev->requested_blocks; i++) {
                uint64_t offs = 0;

                if (!rdisk_block_offset(dev, dev->sector_pos, &offs)) {
                    /* A block of the 640 KB medium that does not exist. */
                    if (!out)
                        memset(dev->buffer + (i * bps), 0x00, bps);
                } else if (fseeko64(dev->drv->fp, offs, SEEK_SET) == -1) {
                    if (out)
                        rdisk_write_error(dev);
                    else
                        rdisk_read_error(dev);
                    ret = -1;
                } else {
                    if (feof(dev->drv->fp))
                        break;

                    if (out) {
                        if (fwrite(dev->buffer + (i * bps), 1,
                                   bps, dev->drv->fp) != bps) {
                            rdisk_log(dev->log, "rdisk_blocks(): Error writing data\n");
                            rdisk_write_error(dev);
                            ret = -1;
                        } else
                            fflush(dev->drv->fp);
                    } else if (fread(dev->buffer + (i * bps), 1,
                                     bps, dev->drv->fp) != bps) {
                        rdisk_log(dev->log, "rdisk_blocks(): Error reading data\n");
                        rdisk_read_error(dev);
                        ret = -1;
                    }
                }

                if (ret == -1)
                    break;

                dev->sector_pos++;
            }

            if (ret == 1) {
                rdisk_log(dev->log, "%s %i bytes of blocks...\n", out ? "Written" :
                        "Read", *len);

                dev->sector_len -= dev->requested_blocks;
            }
        }
    } else {
        rdisk_command_complete(dev);
        ret = 0;
    }

    return ret;
}

void
rdisk_insert(rdisk_t *dev)
{
    if ((dev != NULL) && (dev->drv != NULL)) {
        /* An ATA drive reports a change once, as MC on its next media command. */
        dev->ata_mc     = 1;
        dev->ata_locked = 0;
        dev->soft_wp    = 0;
        if (dev->drv->fp == NULL) {
            dev->unit_attention = 0;
            dev->transition     = 0;
            dev->prevent        = 0;
            /* See rdisk_set_protection(). */
            dev->protect_mode         = 0;
            dev->protect_password_len = 0;
            rdisk_log(dev->log, "Media removal\n");
        } else if (dev->transition) {
            dev->unit_attention = 1;
            /* Turn off the medium changed status. */
            dev->transition     = 0;
            rdisk_log(dev->log, "Media insert\n");
        } else {
            dev->unit_attention = 0;
            dev->transition     = 1;
            rdisk_log(dev->log, "Media transition\n");
        }
    }
}

/* The two pairs of words of Format Life, bytes 13 to 20 of the Iomega sense page 2. */
static void
rdisk_format_life(rdisk_t *dev)
{
    dev->buffer[13] = dev->buffer[17] = (RDISK_FORMAT_LIFE_COUNT >> 8) & 0xff;
    dev->buffer[14] = dev->buffer[18] = RDISK_FORMAT_LIFE_COUNT & 0xff;
}

/*
   The sectors per track of a Bernoulli disk, READ DEFECT DATA with byte 2 = 1Eh (format 6, in the ROM of the
   MultiDisk 150): a 4-byte header with the format and the length, the bytes of one side, one byte (two on the 20) and
   the bytes of the other side.  The Bernoulli 150 and 230 keep their other disks in the Iomega sense pages.

   SCSIUTIL.EXE adds the bytes of each side up and takes away the sectors of that side that are not spares, which are
   the 41,832 / 43,647 / 88,055 below; Disk Life is what is left, as a percentage of the spares of a new disk.  It
   checks that against 2,080 spares at most, so a side here has those spares on top and the gauge reads 100%.  Only
   the number of tracks and the sum come from there; how they are spread out is not known.

   Returns the length of the data, 0 if this disk has no such list.
 */
static int
rdisk_bernoulli_track_list(const rdisk_t *dev, uint8_t *buffer, const int max_len)
{
    static const struct {
        uint8_t  medium;
        uint16_t tracks;
        uint32_t sectors;
        uint8_t  gap;
        uint8_t  tail;
    } lists[] = {
        { RDISK_MEDIUM_BERNOULLI_20, 674,  41832, 2, 1 },
        { RDISK_MEDIUM_BERNOULLI_44, 1140, 43647, 1, 0 },
        { RDISK_MEDIUM_BERNOULLI_90, 1586, 88055, 1, 0 }
    };

    for (size_t i = 0; i < (sizeof(lists) / sizeof(lists[0])); i++) {
        const int len = 4 + (2 * lists[i].tracks) + lists[i].gap + lists[i].tail;

        if ((dev->drv->medium_size != rdisk_types[lists[i].medium].sectors) || (len > max_len))
            continue;

        const uint32_t sum = lists[i].sectors + 2080;

        memset(buffer, 0x00, len);
        buffer[1] = 0x1e;
        buffer[2] = ((len - 4) >> 8) & 0xff;
        buffer[3] = (len - 4) & 0xff;
        for (int t = 0; t < lists[i].tracks; t++) {
            const uint8_t sectors = (sum / lists[i].tracks) + (t < (int) (sum % lists[i].tracks));

            buffer[4 + t]                                = sectors;
            buffer[4 + lists[i].tracks + lists[i].gap + t] = sectors;
        }

        return len;
    }

    return 0;
}

/* The flags of a command, for the command set of this drive. */
static uint8_t
rdisk_cmd_flags(const rdisk_t *dev, const uint8_t opcode)
{
    if (IS_ZIP_OR_JAZ(dev->drv->type)) {
        if (opcode == RDISK_CMD_ATAPI_FORMAT_UNIT)
            return IMPLEMENTED | CHECK_READY | ALLOW_UA | ATAPI_ONLY;

        if (opcode == RDISK_CMD_READ_DEFECT_DATA)
            return IMPLEMENTED | CHECK_READY;
    }

    /* FORMAT UNIT is 04h on the ATAPI HiFD as well: hifdnt4.sys and Sony's REFRESH.EXE both send it.  So it is on the
       SuperDisk: DISKVSD.VXD of Windows 95 OSR2 and 98 sends it to an ATAPI floppy to format a track. */
    if (((dev->drv->type == RDISK_TYPE_HIFD_200) || IS_SUPERDISK(dev->drv->type)) && (opcode == GPCMD_FORMAT_UNIT))
        return IMPLEMENTED | CHECK_READY | ALLOW_UA;

    if (IS_BERNOULLI(dev->drv->type) && !IS_BERNOULLI_150(dev->drv->type) && (opcode == RDISK_CMD_READ_DEFECT_DATA))
        return IMPLEMENTED | CHECK_READY;

    if (IS_REV(dev->drv->type) && (opcode == GPCMD_GET_CONFIGURATION))
        return IMPLEMENTED | ALLOW_UA;

    /* ImDrvFmt.dll sends FORMAT UNIT as 04 18 20 on any bus.  CDevice::ProtectionGetState() in ImDrvInt.dll fails
       if READ DISC STRUCTURE fails, whatever the disk, and SetMMCWriteProtect() sends SEND DISC STRUCTURE. */
    /* FORMAT UNIT is in the Caleb's manual and its itDevice.dll (no tool of Caleb's calls it, though). */
    if ((dev->drv->type == RDISK_TYPE_UHD144) && ((opcode == GPCMD_FORMAT_UNIT) || (opcode == RDISK_CMD_CALEB_VENDOR)))
        return IMPLEMENTED | CHECK_READY | ALLOW_UA;

    if (IS_REV(dev->drv->type) && ((opcode == GPCMD_FORMAT_UNIT) || (opcode == GPCMD_READ_DVD_STRUCTURE) ||
                                   (opcode == GPCMD_SEND_DVD_STRUCTURE)))
        return IMPLEMENTED | CHECK_READY | ALLOW_UA;

    return IS_BERNOULLI_150(dev->drv->type) ? bernoulli_150_command_flags[opcode] : rdisk_command_flags[opcode];
}

/* One entry of the Rev's status pages: the top two bits of the first byte are its size (1, 2, 3 or 5 bytes in all,
   of which the first byte is this one) and the other six its tag.  An entry of one byte has the value 1. */
static int
rdisk_rev_msg(uint8_t *buf, int pos, const uint8_t tag, const uint32_t val, const int bytes)
{
    buf[pos++] = tag | ((bytes == 4) ? 0xc0 : (bytes << 6));

    for (int i = bytes - 1; i >= 0; i--)
        buf[pos++] = (val >> (i * 8)) & 0xff;

    return pos;
}

/*
   Pages 3Eh ("AmpQueryDrive"), 3Fh ("AmpQueryDisk") and 40h (progress) of opcode 06h on a Rev drive, which all ask
   for F0h bytes.  ImDrvInt.dll and ImDrvFmt.dll walk these from their first byte as a list of the entries above,
   looking for a tag, and take a tag that is not there as DEADBEEFh, so several of them have to be present.  What the
   software needs and does with them:

   Page 3Eh, the drive:
     tag 09h  must be 41h, or the software gives up with 80000053h.
     tag 0Dh  the media state, from the "Media State" line of RevDiagnostics.exe: 0101h no disk, 0103h new disk
              inserted, 0201h disk being ejected, 0202h, 0203h, 0302h and 0303h disk present, 0404h a problem with
              the disk.  Only those four "present" values let the software use the disk, the others fail with
              80000054h.  ESTIMATED: what tells the four apart is not known, so this is always 0202h.
     tag 0Eh  the drive's serial number, left out: RevDiagnostics.exe does without it.
     tag 1Ah  left out: its presence is a capability (bit 3 of the protection state) that is not understood.
   Page 3Fh, the disk (there has to be one):
     tag 0Dh  the last LBA of the current view, "MaxLba" in the debug strings.
     tag 08h  the last LBA of the disk, as the software works out the size of a view from it.  ESTIMATED: that it is
              0-based like MaxLba.
     tag 0Fh  the offset of the first view: anything but 3C0h sets 1000h in the protection state.
     tag 14h  temporary access ("TEMP_ACCESS_MSG"): only tested for not being zero, so it has to be present.
     tag 30h  password protected ("PASSWORD_PROTECTED_MSG"): the same.
   Page 40h: tag 06h is progress, above FFh while the drive is busy, low byte in percent.  It has to be present as
   well, as the high byte of DEADBEEFh reads as busy and CHardwareFormat would wait for ever.

   The tags 02h (format flags), 05h and 48h of a disk with views are left out as only the views need them.  This is
   not how a real drive's page looks, which no capture shows: it is the minimum that the software accepts.

   Returns the length of the page.
*/
static int
rdisk_rev_status_page(const rdisk_t *dev, uint8_t *buf, const uint8_t page)
{
    const uint32_t last  = dev->drv->medium_size - 1;
    const uint8_t  state = dev->protect_mode & 0x07;
    int            pos   = 0;

    memset(buf, 0x00, 256);

    switch (page) {
        case 0x3e:
            pos = rdisk_rev_msg(buf, pos, 0x09, 0x41, 1);
            pos = rdisk_rev_msg(buf, pos, 0x0d, (dev->drv->fp != NULL) ? 0x0202 : 0x0101, 2);
            break;
        case 0x3f:
            pos = rdisk_rev_msg(buf, pos, 0x0d, last, 4);
            pos = rdisk_rev_msg(buf, pos, 0x08, last, 4);
            pos = rdisk_rev_msg(buf, pos, 0x0f, 0x03c0, 2);
            pos = rdisk_rev_msg(buf, pos, 0x14, !!(dev->protect_mode & 0x08), 1);
            pos = rdisk_rev_msg(buf, pos, 0x30, (state == 3) || (state == 5), 1);
            break;
        default:
            pos = rdisk_rev_msg(buf, pos, 0x06, 0x0000, 2);
            break;
    }

    return 0xf0;
}

/* The feature descriptors of GET CONFIGURATION on a Rev drive.  Returns the length of the response. */
static int
rdisk_rev_configuration(const rdisk_t *dev, uint8_t *buf, const uint16_t start, const uint8_t rt)
{
    const int      present = dev->drv->fp != NULL;
    const uint32_t last    = present ? (dev->drv->medium_size - 1) : 0;
    const uint8_t  cur     = present ? 0x01 : 0x00;
    int            pos     = 8;

    const struct {
        uint16_t code;
        uint8_t  flags;
        uint8_t  len;
        uint8_t  data[12];
    } f[] = {
        { 0x0000, 0x03,       4, { 0x00, 0x02, cur, 0x00 } },                       /* Profiles: Removable Disk */
        { 0x0001, 0x0b,       8, { 0x00, 0x00, 0x00, (uint8_t) (rdisk_is_scsi(dev) ? 1 : 2) } }, /* Core */
        { 0x0003, 0x03,       4, { 0x09 } },                                        /* Removable Medium: caddy, eject, lock */
        { 0x0010, 0x02 | cur, 8, { 0x00, 0x00, 0x08, 0x00, 0x00, 0x20 } },          /* Random Readable */
        { 0x0020, 0x02 | cur, 12, { (uint8_t) (last >> 24), (uint8_t) (last >> 16), (uint8_t) (last >> 8), (uint8_t) last,
                                    0x00, 0x00, 0x08, 0x00, 0x00, 0x20 } },         /* Random Writable */
        { 0x0023, 0x02 | cur, 0, { 0 } },                                           /* Formattable */
    };

    for (size_t i = 0; i < (sizeof(f) / sizeof(f[0])); i++) {
        if ((f[i].code < start) || ((rt == 2) && (f[i].code != start)) || ((rt == 1) && !(f[i].flags & 1)))
            continue;

        buf[pos++] = f[i].code >> 8;
        buf[pos++] = f[i].code & 0xff;
        buf[pos++] = f[i].flags;
        buf[pos++] = f[i].len;
        memcpy(buf + pos, f[i].data, f[i].len);
        pos += f[i].len;
    }

    buf[0] = 0;
    buf[1] = 0;
    buf[2] = ((pos - 4) >> 8) & 0xff;
    buf[3] = (pos - 4) & 0xff;
    buf[6] = 0x00;
    buf[7] = present ? 0x02 : 0x00;    /* Current profile */

    return pos;
}

/*
   Set the protection of a Zip or Jaz disk, GPCMD_IOMEGA_SET_PROTECTION_MODE: the mode is in byte 1 of the CDB and the
   length of a password, which follows in a data phase, in byte 4 (SetLockState() in SCSIDLL.DLL and IdeDiskProtect() in
   imgatapi.sys, which sends the same two values to the native ATA drive).  The modes are in rdisk_protection().

   How the drive treats a wrong password is not known (ESTIMATED: it fails as a write-protected medium).  The real
   drive keeps the protection on the disk, which an image has no place for: here it lasts until the disk is ejected.

   Returns 1 if the protection was set; if not, the command has failed.
 */
static int
rdisk_set_protection(rdisk_t *dev, const uint8_t mode, const uint8_t *password, const uint8_t len)
{
    const uint8_t state = mode & 0x07;
    const uint8_t cur   = rdisk_protection(dev);

    if ((mode & 0xf0) || ((state != 0) && (state != 2) && (state != 3) && (state != 5))) {
        rdisk_invalid_field(dev, mode);
        return 0;
    }

    if (((state == 3) || (state == 5)) && !len) {
        rdisk_invalid_field(dev, len);
        return 0;
    }

    if (len > sizeof(dev->protect_password)) {
        rdisk_invalid_field(dev, len);
        return 0;
    }

    if (((cur == 3) || (cur == 5)) &&
        ((len != dev->protect_password_len) || (len && memcmp(password, dev->protect_password, len)))) {
        rdisk_write_protected(dev);
        return 0;
    }

    dev->protect_mode         = mode;
    dev->protect_password_len = ((state == 3) || (state == 5)) ? len : 0;
    memset(dev->protect_password, 0x00, sizeof(dev->protect_password));
    if (dev->protect_password_len)
        memcpy(dev->protect_password, password, len);

    return 1;
}

/* The Bernoulli 150's firmware rejects a CDB with a reserved bit set, or a control byte other than 0, 1 or 3. */
static int
rdisk_bernoulli_150_cdb_is_valid(const uint8_t *cdb)
{
    const int len = (cdb[0] < 0x20) ? 6 : 10;

    if ((cdb[len - 1] != 0x00) && (cdb[len - 1] != 0x01) && (cdb[len - 1] != 0x03))
        return 0;

    for (size_t i = 0; i < (sizeof(bernoulli_150_cdb_masks) / sizeof(bernoulli_150_cdb_masks[0])); i++) {
        if (bernoulli_150_cdb_masks[i].op == cdb[0]) {
            for (int j = 0; j < (len - 2); j++) {
                if (cdb[j + 1] & ((j == 0) ? 0x1f : 0xff) & bernoulli_150_cdb_masks[i].mask[j])
                    return 0;
            }

            break;
        }
    }

    return 1;
}

static int
rdisk_pre_execution_check(rdisk_t *dev, const uint8_t *cdb)
{
    const uint8_t flags = rdisk_cmd_flags(dev, cdb[0]);
    int           ready;

    if ((cdb[0] != GPCMD_REQUEST_SENSE) && (dev->cur_lun == SCSI_LUN_USE_CDB) &&
        (cdb[1] & 0xe0)) {
        rdisk_log(dev->log, "Attempting to execute a unknown command targeted at SCSI LUN %i\n",
                ((dev->tf->request_length >> 5) & 7));
        rdisk_invalid_lun(dev, cdb[1] >> 5);
        return 0;
    }

    if (!(flags & IMPLEMENTED)) {
        rdisk_log(dev->log, "Attempting to execute unknown command %02X over %s\n",
                  cdb[0], rdisk_is_scsi(dev) ?
                  "SCSI" : "ATAPI");

        rdisk_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if (!rdisk_is_scsi(dev) &&
        (flags & SCSI_ONLY)) {
        rdisk_log(dev->log, "Attempting to execute SCSI-only command %02X "
                  "over ATAPI\n", cdb[0]);
        rdisk_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if (rdisk_is_scsi(dev) &&
        (flags & ATAPI_ONLY)) {
        rdisk_log(dev->log, "Attempting to execute ATAPI-only command %02X "
                  "over SCSI\n", cdb[0]);
        rdisk_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if (IS_BERNOULLI_150(dev->drv->type) && !rdisk_bernoulli_150_cdb_is_valid(cdb)) {
        rdisk_log(dev->log, "Reserved bits set in the CDB of command %02X\n", cdb[0]);
        rdisk_invalid_field(dev, cdb[1]);
        return 0;
    }

    if (dev->transition) {
        if ((cdb[0] == GPCMD_TEST_UNIT_READY) || (cdb[0] == GPCMD_REQUEST_SENSE))
            ready = 0;
        else {
            if (!(flags & ALLOW_UA)) {
                rdisk_log(dev->log, "(ext_medium_changed != 0): rdisk_insert()\n");
                rdisk_insert((void *) dev);
            }

            ready = (dev->drv->fp != NULL);
        }
    } else
        ready = (dev->drv->fp != NULL);

    /*
       If the drive is not ready, there is no reason to keep the
       UNIT ATTENTION condition present, as we only use it to mark
       disc changes.
     */
    if (!ready && (dev->unit_attention > 0))
        dev->unit_attention = 0;

    /*
       If the UNIT ATTENTION condition is set and the command does not allow
       execution under it, error out and report the condition.
     */
    if (dev->unit_attention == 1) {
        /*
           Only increment the unit attention phase if the command can
           not pass through it.
         */
        if (!(flags & ALLOW_UA)) {
            rdisk_log(dev->log, "Unit attention now 2\n");
            dev->unit_attention++;
            rdisk_log(dev->log, "UNIT ATTENTION: Command %02X not allowed to pass through\n",
                      cdb[0]);
            rdisk_unit_attention(dev);
            return 0;
        }
    } else if (dev->unit_attention == 2) {
        if (cdb[0] != GPCMD_REQUEST_SENSE) {
            rdisk_log(dev->log, "Unit attention now 0\n");
            dev->unit_attention = 0;
        }
    }

    /*
       Unless the command is REQUEST SENSE, clear the sense. This will *NOT* clear
       the UNIT ATTENTION condition if it's set.
     */
    if (cdb[0] != GPCMD_REQUEST_SENSE)
        rdisk_sense_clear(dev, cdb[0]);

    if (!ready && (flags & CHECK_READY)) {
        rdisk_log(dev->log, "Not ready (%02X)\n", cdb[0]);
        rdisk_not_ready(dev);
        return 0;
    }

    rdisk_log(dev->log, "Continuing with command %02X\n", cdb[0]);
    return 1;
}

static void
rdisk_seek(rdisk_t *dev, const uint32_t pos)
{
    dev->sector_pos = pos;
}

static void
rdisk_rezero(rdisk_t *dev)
{
    dev->sector_pos = dev->sector_len = 0;
    rdisk_seek(dev, 0);
}

void
rdisk_reset(scsi_common_t *sc)
{
    rdisk_t *dev = (rdisk_t *) sc;

    rdisk_rezero(dev);
    dev->tf->status         = 0;
    dev->callback           = 0.0;
    rdisk_set_callback(dev);
    dev->tf->phase          = 1;
    dev->tf->request_length = 0xEB14;
    dev->packet_status      = PHASE_NONE;
    dev->unit_attention     = 0;
    dev->prevent            = 0;
    dev->cur_lun            = SCSI_LUN_USE_CDB;
    rdisk_sense_key         = rdisk_asc = rdisk_ascq = dev->unit_attention = dev->transition = 0;
    rdisk_info              = 0x00000000;
}

static void
rdisk_request_sense(rdisk_t *dev, uint8_t *buffer, const uint8_t alloc_length, const int desc)
{
    /*Will return 18 bytes of 0*/
    if (alloc_length != 0) {
        memset(buffer, 0, alloc_length);
        if (!desc)
            memcpy(buffer, dev->sense, alloc_length);
        else {
            buffer[1] = rdisk_sense_key;
            buffer[2] = rdisk_asc;
            buffer[3] = rdisk_ascq;
        }
    }

    buffer[0] = desc ? 0x72 : 0xf0;
    if (!desc)
        buffer[7] = IS_BERNOULLI_150(dev->drv->type) ? RDISK_BERNOULLI_150_SENSE_LEN - 8 :
                    (IS_BERNOULLI_LIFE_SENSE(dev->drv->type) ? RDISK_BERNOULLI_II_SENSE_LEN - 8 : 10);

    /* Format Life in SCSIUTIL.EXE divides by the sum of bytes 17h and 18h, and by that of 19h and 1Ah, of a 28-byte
       REQUEST SENSE, one pair per side of the disk, which it takes as left and used: with zeros it stops with a divide
       error.  The values are ESTIMATED. */
    if (!desc && IS_BERNOULLI_LIFE_SENSE(dev->drv->type) && (alloc_length >= RDISK_BERNOULLI_II_SENSE_LEN)) {
        buffer[0x17] = 100;
        buffer[0x18] = 0;
        buffer[0x19] = 100;
        buffer[0x1a] = 0;
    }

    if (dev->unit_attention && (rdisk_sense_key == 0)) {
        buffer[desc ? 1 : 2]  = SENSE_UNIT_ATTENTION;
        buffer[desc ? 2 : 12] = ASC_MEDIUM_MAY_HAVE_CHANGED;
        buffer[desc ? 3 : 13] = 0;
    }

    /* These two only set the valid bit, and the information field with it, for an error that is tied to a block:
       a real LS-120 COSM 05 reports 70h for NO SENSE and NOT READY, and the sense table of the Bernoulli 150's ROM
       has 70h for everything but medium errors and an LBA out of range. */
    if (!desc && (IS_LS120_COSM05(dev->drv->type) || IS_BERNOULLI_150(dev->drv->type)) &&
        (buffer[2] != SENSE_MEDIUM_ERROR) &&
        !((buffer[2] == SENSE_ILLEGAL_REQUEST) && (buffer[12] == ASC_LBA_OUT_OF_RANGE))) {
        buffer[0] = 0x70;
        memset(&buffer[3], 0x00, 4);
    }

    rdisk_log(dev->log, "Reporting sense: %02X %02X %02X\n", buffer[2],
            buffer[12], buffer[13]);

    if (buffer[desc ? 1 : 2] == SENSE_UNIT_ATTENTION) {
        /* If the last remaining sense is unit attention, clear
           that condition. */
        dev->unit_attention = 0;
    }

    /* Clear the sense stuff as per the spec. */
    rdisk_sense_clear(dev, GPCMD_REQUEST_SENSE);

    if (dev->transition) {
        rdisk_log(dev->log, "Removable Disk_TRANSITION: rdisk_insert()\n");
        rdisk_insert((void *) dev);
    }
}

static void
rdisk_request_sense_for_scsi(scsi_common_t *sc, uint8_t *buffer, const uint8_t alloc_length)
{
    rdisk_t  *dev   = (rdisk_t *) sc;
    const int ready = (dev->drv->fp != NULL);

    if (!ready && dev->unit_attention) {
        /*
           If the drive is not ready, there is no reason to keep the UNIT ATTENTION
           condition present, as we only use it to mark disc changes.
         */
        dev->unit_attention = 0;
    }

    /* Do *NOT* advance the unit attention phase. */
    rdisk_request_sense(dev, buffer, alloc_length, 0);
}

static void
rdisk_set_buf_len(const rdisk_t *dev, int32_t *BufLen, int32_t *src_len)
{
    if (*BufLen == -1)
        *BufLen = *src_len;
    else {
        *BufLen  = MIN(*src_len, *BufLen);
        *src_len = *BufLen;
    }
    rdisk_log(dev->log, "Actual transfer length: %i\n", *BufLen);
}

/* Add a capacity descriptor to the list of READ FORMAT CAPACITIES. */
static int
rdisk_format_descriptor(uint8_t *buf, int pos, const uint32_t blocks, const uint8_t code, const uint32_t bps)
{
    buf[pos++] = (blocks >> 24) & 0xff;
    buf[pos++] = (blocks >> 16) & 0xff;
    buf[pos++] = (blocks >> 8) & 0xff;
    buf[pos++] = blocks & 0xff;
    buf[pos++] = code;
    buf[pos++] = (bps >> 16) & 0xff;
    buf[pos++] = (bps >> 8) & 0xff;
    buf[pos++] = bps & 0xff;

    return pos;
}

/* Is the medium in the drive a format of a 2HD floppy? */
static int
rdisk_is_2hd(const rdisk_t *dev)
{
    if (dev->drv->fp == NULL)
        return 0;

    return (dev->drv->medium_type == RDISK_MEDIUM_FD_1440K) || (dev->drv->medium_type == RDISK_MEDIUM_FD_1200K) ||
           (dev->drv->medium_type == RDISK_MEDIUM_FD_2HD_NEC) || (dev->drv->medium_type == RDISK_MEDIUM_FD_DMF);
}

static void
rdisk_command(scsi_common_t *sc, const uint8_t *cdb)
{
    rdisk_t       *dev                = (rdisk_t *) sc;
    char           device_identify[9] = { '8', '6', 'B', '_', 'R', 'D', '0', '0', 0 };
    int            pos                = 0;
    int            idx                = 0;
    int32_t        blen               = 65536;
    uint32_t       i;
    unsigned       preamble_len;
    int32_t        len;
    int32_t        max_len;
    int32_t        alloc_length;
    int            block_desc;
    int            size_idx;
    int32_t *      BufLen;

    if (rdisk_is_scsi(dev)) {
        BufLen          = &rdisk_scsi_device(dev)->buffer_length;
        dev->tf->status &= ~ERR_STAT;
    } else {
        BufLen         = &blen;
        dev->tf->error = 0;
    }

    dev->packet_len  = 0;
    dev->request_pos = 0;

    device_identify[7] = dev->id + 0x30;

    memcpy(dev->current_cdb, cdb, 12);

    if (cdb[0] != 0) {
        rdisk_log(dev->log, "Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, "
                  "Unit attention: %i\n",
                  cdb[0], rdisk_sense_key, rdisk_asc, rdisk_ascq, dev->unit_attention);
        rdisk_log(dev->log, "Request length: %04X\n", dev->tf->request_length);

        rdisk_log(dev->log, "CDB: %02X %02X %02X %02X %02X %02X %02X %02X "
                  "%02X %02X %02X %02X\n",
                  cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
                  cdb[8], cdb[9], cdb[10], cdb[11]);
    }

    dev->sector_len = 0;

    rdisk_set_phase(dev, SCSI_PHASE_STATUS);

    /*
       This handles the Not Ready/Unit Attention check if it has to be handled at
       this point.
     */
    if (rdisk_pre_execution_check(dev, cdb) == 0)
        return;

    switch (cdb[0]) {
        case GPCMD_SEND_DIAGNOSTIC:
            if (!(cdb[1] & (1 << 2))) {
                rdisk_invalid_field(dev, cdb[1]);
                return;
            }
            fallthrough;
        case GPCMD_SCSI_RESERVE:
        case GPCMD_SCSI_RELEASE:
        case GPCMD_TEST_UNIT_READY:
            rdisk_set_phase(dev, SCSI_PHASE_STATUS);
            rdisk_command_complete(dev);
            break;

        case GPCMD_FORMAT_UNIT:
        case RDISK_CMD_ATAPI_FORMAT_UNIT:
            /* Bit 5 of byte 2 is how IssueFormat() in SCSIDLL.DLL formats a disk with a password on it. */
            if (rdisk_floptical_locked(dev))
                rdisk_floptical_needs_unlock(dev);
            else if (dev->drv->read_only || ((rdisk_protection(dev) != 0) && !(cdb[2] & 0x20)) ||
                (rdisk_protection(dev) == 2))
                rdisk_write_protected(dev);
            else if (HAS_PROTECTION(dev->drv->type) && (cdb[1] & 0x10)) {
                /* FMTDATA: the same function sends a 4-byte defect list header (the only list it has) that says
                   how to format.  An image does not need any of it. */
                rdisk_set_phase(dev, SCSI_PHASE_DATA_OUT);
                rdisk_buf_alloc(dev, 256);
                len = 4;
                rdisk_set_buf_len(dev, BufLen, &len);
                rdisk_data_command_finish(dev, len, len, len, 1);
            } else if (((dev->drv->type == RDISK_TYPE_HIFD_200) || IS_SUPERDISK(dev->drv->type) ||
                        (dev->drv->type == RDISK_TYPE_UHD144)) && (cdb[1] & 0x10)) {
                /* Sony's hifdnt4.sys and REFRESH.EXE send 04 17 with no length in the CDB, and always a 12-byte
                   list: 4-byte header, then block count, type and block length.  An image needs none of it.
                   DISKVSD.VXD of Windows 95 OSR2 and 98 sends the same for each track of an ATAPI floppy: 04 17 and the
                   track in byte 2, and a header of 00, B0h or the side, 00, 08.  B0h is FOV, DCRT and single track, so
                   here it changes nothing on the image, and the file system that follows is what gets written. */
                rdisk_set_phase(dev, SCSI_PHASE_DATA_OUT);
                rdisk_buf_alloc(dev, 256);
                len = 12;
                rdisk_set_buf_len(dev, BufLen, &len);
                rdisk_data_command_finish(dev, len, len, len, 1);
            } else {
                rdisk_set_phase(dev, SCSI_PHASE_STATUS);
                rdisk_command_complete(dev);
            }
            break;

        case GPCMD_IOMEGA_SET_PROTECTION_MODE:
            /* A command of the Zip, Jaz and Rev only: to the other drives it is still an illegal opcode. */
            if (!HAS_PROTECTION(dev->drv->type))
                rdisk_illegal_opcode(dev, cdb[0]);
            else if (dev->drv->fp == NULL)
                rdisk_not_ready(dev);
            else if (IS_REV(dev->drv->type) && (cdb[2] == 0x40)) {
                /* "AmpIomFormat" and "AmpIomShredFormat" in ImDrvFmt.dll, 0C 00 40 xx 80 00 with no data: byte 3 is
                   0 or 1 by the type of format, and 4 to shred.  ESTIMATED: done at once and without touching
                   the image, as blanking gigabytes here would stop the machine and the software writes a file
                   system afterwards. */
                if (rdisk_write_denied(dev))
                    rdisk_write_protected(dev);
                else {
                    rdisk_set_phase(dev, SCSI_PHASE_STATUS);
                    rdisk_command_complete(dev);
                }
            } else if (IS_REV(dev->drv->type) && cdb[2] && (cdb[2] != 0x41)) {
                /* Not the protection: 0C 00 3D 0B 89, 0C 00 3D 0C 80, 0C 00 3D 0D A5 and 0C 00 3F 01 are sent by
                   ImDrvInt.dll and what they do is not known. */
                rdisk_invalid_field(dev, cdb[2]);
            } else if (cdb[4]) {
                /* On a Rev, byte 2 being 41h makes this "AmpSetView", with 80h bytes that describe the views
                   the disk is split into.  Those are not emulated (see rdisk_phase_data_out()), but formatting
                   fails if this fails. */
                rdisk_set_phase(dev, SCSI_PHASE_DATA_OUT);
                rdisk_buf_alloc(dev, 256);
                len = cdb[4];
                rdisk_set_buf_len(dev, BufLen, &len);
                rdisk_data_command_finish(dev, len, len, len, 1);
            } else if (rdisk_set_protection(dev, cdb[1], NULL, 0)) {
                rdisk_set_phase(dev, SCSI_PHASE_STATUS);
                rdisk_command_complete(dev);
            }
            break;

        case RDISK_CMD_READ_DEFECT_DATA:
            rdisk_set_phase(dev, SCSI_PHASE_DATA_IN);

            if (IS_BERNOULLI(dev->drv->type) && (cdb[2] == 0x1e)) {
                rdisk_buf_alloc(dev, 4096);
                max_len = rdisk_bernoulli_track_list(dev, dev->buffer, 4096);
                if (max_len != 0) {
                    len = MIN(max_len, (cdb[7] << 8) | cdb[8]);
                    rdisk_set_buf_len(dev, BufLen, &len);
                    rdisk_data_command_finish(dev, len, len, len, 0);
                    break;
                }
            }

            /* GetDefectData() in SCSIDLL.DLL.  An image has no defects, so this is just the list header. */
            rdisk_buf_alloc(dev, 4);
            memset(dev->buffer, 0x00, 4);
            dev->buffer[1] = cdb[2] & 0x1f;
            len            = MIN(4, (cdb[7] << 8) | cdb[8]);
            rdisk_set_buf_len(dev, BufLen, &len);
            rdisk_data_command_finish(dev, len, len, len, 0);
            break;

        case GPCMD_IOMEGA_SENSE:
            rdisk_set_phase(dev, SCSI_PHASE_DATA_IN);
            max_len = cdb[4];
            rdisk_buf_alloc(dev, 256);

            /* See rdisk_rev_status_page().  Pages 1 and 2 are the same as on the Zip and Jaz, with 4Ah or 64h of
               page 2 being asked for. */
            if (IS_REV(dev->drv->type) && (cdb[2] >= 0x3e) && (cdb[2] <= 0x40)) {
                if ((cdb[2] == 0x3f) && (dev->drv->fp == NULL)) {
                    rdisk_not_ready(dev);
                    rdisk_buf_free(dev);
                    return;
                }

                len = MIN(rdisk_rev_status_page(dev, dev->buffer, cdb[2]), max_len);
                rdisk_set_buf_len(dev, BufLen, &len);
                rdisk_data_command_finish(dev, len, len, len, 0);
                break;
            }

            /* The Bernoulli 150's own pages, from its ROM: page 0 lists the pages, page 1 has what the disk says about
               itself and page 2 is the drive's status.  Page 3 has not been decoded and keeps the behavior of the
               other drives.  The Tools read the Bernoulli 230 the same way.  In page 2, bytes 13 to 20 are two pairs
               of big-endian words, one per side, that Format Life divides by the sum of: with zeros SCSIUTIL.EXE
               stops with a divide error.  The first word of each pair is spares left, the second those used up
               (ESTIMATED).  In page 1, bytes 0Ah to 0Dh are one word per side for Disk Life: SCSIUTIL.EXE takes 12EDh
               or 142Ch away from them, by disk, so 4000h leaves it more spares than any disk it knows. */
            if ((IS_BERNOULLI_150(dev->drv->type) || (dev->drv->type == RDISK_TYPE_BERNOULLI_230)) && (cdb[2] <= 2)) {
                memset(dev->buffer, 0, 256);
                if (cdb[2] == 0) {
                    dev->buffer[1] = 0x04;
                    dev->buffer[3] = 0x01;
                    dev->buffer[4] = 0x02;
                    dev->buffer[5] = 0x03;
                    len            = 6;
                } else if (cdb[2] == 1) {
                    dev->buffer[0]  = 0x01;
                    dev->buffer[1]  = 0x56;
                    dev->buffer[10] = 0x40;
                    dev->buffer[12] = 0x40;
                    len             = 0x58;
                } else {
                    const uint32_t last = (dev->drv->fp != NULL) ? (dev->drv->medium_size - 1) : 0;

                    dev->buffer[0]  = 0x02;
                    dev->buffer[1]  = 0x13;
                    dev->buffer[4]  = dev->prevent;
                    dev->buffer[5]  = (last >> 24) & 0xff;
                    dev->buffer[6]  = (last >> 16) & 0xff;
                    dev->buffer[7]  = (last >> 8) & 0xff;
                    dev->buffer[8]  = last & 0xff;
                    dev->buffer[11] = 0x02;    /* Block length. */
                    rdisk_format_life(dev);
                    len             = 21;
                }

                len = MIN(len, max_len);
                rdisk_set_buf_len(dev, BufLen, &len);
                rdisk_data_command_finish(dev, len, len, len, 0);
                break;
            }

            memset(dev->buffer, 0, 256);
            if (cdb[2] == 1) {
                /*
                   This page is related to disk health status - setting
                   this page to 0 makes disk health read as "marginal".
                 */
                dev->buffer[0] = 0x01;
                dev->buffer[1] = 0x58;
                for (i = 0x00; i < 0x58; i++)
                    dev->buffer[i + 0x02] = 0xff;
            } else if (cdb[2] == 2) {
                dev->buffer[0] = 0x02;
                dev->buffer[1] = 0x3d;
                for (i = 0x00; i < 0x13; i++)
                    dev->buffer[i + 0x02] = 0x00;
                /* What GetLockState() in SCSIDLL.DLL reads: the protection mode as it was set.  IdeDiskStatus() in
                   imgatapi.sys builds this page, of 63 bytes, the same way on the native ATA drive. */
                dev->buffer[0x15] = HAS_PROTECTION(dev->drv->type) ? (dev->protect_mode & 0x0f) : 0x00;
                if (dev->drv->read_only && !(dev->buffer[0x15] & 0x07))
                    dev->buffer[0x15] |= 0x02;
                for (i = 0x00; i < 0x27; i++)
                    dev->buffer[i + 0x16] = 0x00;
                /* SCSIUTIL.EXE treats these the same on a Zip as on a Bernoulli, divide included. */
                rdisk_format_life(dev);
            } else {
                rdisk_invalid_field(dev, cdb[2]);
                rdisk_buf_free(dev);
                return;
            }
            /* Page code, length and the rest of the page, as the Bernoulli 150's ROM and imgatapi.sys have them:
               the protection mode is past the 18th byte. */
            len = MIN(dev->buffer[1] + 2, max_len);
            rdisk_set_buf_len(dev, BufLen, &len);
            rdisk_data_command_finish(dev, len, len, len, 0);
            break;

        case GPCMD_REZERO_UNIT:
            dev->sector_pos = dev->sector_len = 0;
            rdisk_seek(dev, 0);
            rdisk_set_phase(dev, SCSI_PHASE_STATUS);
            break;

        case GPCMD_REQUEST_SENSE:
            /*
               If there's a unit attention condition and there's a buffered not ready, a
               standalone REQUEST SENSE should forget about the not ready, and report unit
               attention straight away.
             */
            rdisk_set_phase(dev, SCSI_PHASE_DATA_IN);
            max_len = cdb[4];

            if (!max_len) {
                rdisk_set_phase(dev, SCSI_PHASE_STATUS);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * RDISK_TIME;
                rdisk_set_callback(dev);
                break;
            }

            rdisk_buf_alloc(dev, 256);
            rdisk_set_buf_len(dev, BufLen, &max_len);
            len = (cdb[1] & 1) ? 8 : (IS_BERNOULLI_150(dev->drv->type) ? RDISK_BERNOULLI_150_SENSE_LEN :
                                      (IS_BERNOULLI_LIFE_SENSE(dev->drv->type) ? RDISK_BERNOULLI_II_SENSE_LEN : 18));
            rdisk_request_sense(dev, dev->buffer, max_len, cdb[1] & 1);
            rdisk_data_command_finish(dev, len, len, cdb[4], 0);
            break;

        case GPCMD_MECHANISM_STATUS:
            rdisk_set_phase(dev, SCSI_PHASE_DATA_IN);
            len = (cdb[8] << 8) | cdb[9];

            rdisk_buf_alloc(dev, 8);
            rdisk_set_buf_len(dev, BufLen, &len);

            memset(dev->buffer, 0, 8);
            dev->buffer[5] = 1;

            rdisk_data_command_finish(dev, 8, 8, len, 0);
            break;

        case GPCMD_READ_6:
        case GPCMD_READ_10:
        case GPCMD_READ_12:
            rdisk_set_phase(dev, SCSI_PHASE_DATA_IN);
            alloc_length = rdisk_sector_size(dev);

            /* Protection mode 5 is against reads too. */
            if (rdisk_protection(dev) == 5) {
                rdisk_write_protected(dev);
                break;
            }

            switch (cdb[0]) {
                case GPCMD_READ_6:
                    dev->sector_len = cdb[4];
                    /*
                       For READ (6) and WRITE (6), a length of 0 indicates a
                       transfer of 256 sectors.
                     */
                    if (dev->sector_len == 0)
                        dev->sector_len = 256;
                    dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) |
                                      (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
                    rdisk_log(dev->log, "Length: %i, LBA: %i\n", dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_READ_10:
                    dev->sector_len = (cdb[7] << 8) | cdb[8];
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) |
                                      (cdb[4] << 8) | cdb[5];
                    rdisk_log(dev->log, "Length: %i, LBA: %i\n", dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_READ_12:
                    dev->sector_len = (((uint32_t) cdb[6]) << 24) |
                                      (((uint32_t) cdb[7]) << 16) |
                                      (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
                    dev->sector_pos = (((uint32_t) cdb[2]) << 24) |
                                      (((uint32_t) cdb[3]) << 16) |
                                      (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
                    break;

                default:
                    break;
            }

            if (rdisk_transfer_out_of_range(dev))
                rdisk_lba_out_of_range(dev);
            else if (dev->sector_len) {
                max_len               = dev->sector_len;
                dev->requested_blocks = max_len;

                dev->packet_len = max_len * alloc_length;
                rdisk_buf_alloc(dev, dev->packet_len);

                const int ret = rdisk_blocks(dev, &alloc_length, 0);
                alloc_length  = dev->requested_blocks * rdisk_sector_size(dev);

                if (ret > 0) {
                    dev->requested_blocks = max_len;
                    dev->packet_len       = alloc_length;

                    rdisk_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

                    rdisk_data_command_finish(dev, alloc_length, rdisk_sector_size(dev),
                                            alloc_length, 0);

                    ui_sb_update_icon(SB_RDISK | dev->id,
                                      dev->packet_status != PHASE_COMPLETE);
                } else {
                    rdisk_set_phase(dev, SCSI_PHASE_STATUS);
                    dev->packet_status = (ret < 0) ? PHASE_ERROR : PHASE_COMPLETE;
                    dev->callback      = 20.0 * RDISK_TIME;
                    rdisk_set_callback(dev);
                    rdisk_buf_free(dev);
                }
            } else {
                rdisk_set_phase(dev, SCSI_PHASE_STATUS);
                /* rdisk_log(dev->log, "All done - callback set\n"); */
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * RDISK_TIME;
                rdisk_set_callback(dev);
                break;
            }
            break;

        case GPCMD_VERIFY_6:
        case GPCMD_VERIFY_10:
        case GPCMD_VERIFY_12:
            if (rdisk_checks_transfer_end(dev) && (cdb[0] == GPCMD_VERIFY_10)) {
                /* MKELS120 verifies track ranges (category 08, function 62h) with VERIFY(10) and no data
                   transfer: Good status for a range inside the medium, ILLEGAL REQUEST / LBA OUT OF RANGE
                   for one that is not (LS-120 specification, section 6.4, tests VER-01 and VER-02).  An
                   image file is always readable, so the range is all there is to check.  The Bernoulli 150
                   has the same range check, and rejects a byte check. */
                dev->sector_len = (cdb[7] << 8) | cdb[8];
                dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

                if (rdisk_transfer_out_of_range(dev)) {
                    rdisk_lba_out_of_range(dev);
                    break;
                }
            }

            if (!(cdb[1] & 2)) {
                rdisk_set_phase(dev, SCSI_PHASE_STATUS);
                rdisk_command_complete(dev);
                break;
            }
            fallthrough;
        case GPCMD_WRITE_6:
        case GPCMD_WRITE_10:
        case GPCMD_WRITE_AND_VERIFY_10:
        case GPCMD_WRITE_12:
        case GPCMD_WRITE_AND_VERIFY_12:
            rdisk_set_phase(dev, SCSI_PHASE_DATA_OUT);
            alloc_length = rdisk_sector_size(dev);

            if (rdisk_write_denied(dev)) {
                rdisk_write_protected(dev);
                break;
            }

            if (rdisk_floptical_locked(dev) && (cdb[0] != GPCMD_VERIFY_6) && (cdb[0] != GPCMD_VERIFY_10) &&
                (cdb[0] != GPCMD_VERIFY_12)) {
                rdisk_floptical_needs_unlock(dev);
                break;
            }

            switch (cdb[0]) {
                case GPCMD_VERIFY_6:
                case GPCMD_WRITE_6:
                    dev->sector_len = cdb[4];
                    /*
                       For READ (6) and WRITE (6), a length of 0 indicates a
                       transfer of 256 sectors.
                     */
                    if (dev->sector_len == 0)
                        dev->sector_len = 256;
                    dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) |
                                      (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
                    break;
                case GPCMD_VERIFY_10:
                case GPCMD_WRITE_10:
                case GPCMD_WRITE_AND_VERIFY_10:
                    dev->sector_len = (cdb[7] << 8) | cdb[8];
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) |
                                      (cdb[4] << 8) | cdb[5];
                    rdisk_log(dev->log, "Length: %i, LBA: %i\n",
                            dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_VERIFY_12:
                case GPCMD_WRITE_12:
                case GPCMD_WRITE_AND_VERIFY_12:
                    dev->sector_len = (((uint32_t) cdb[6]) << 24) |
                                      (((uint32_t) cdb[7]) << 16) |
                                      (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
                    dev->sector_pos = (((uint32_t) cdb[2]) << 24) |
                                      (((uint32_t) cdb[3]) << 16) |
                                      (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
                    break;

                default:
                    break;
            }

            if (rdisk_transfer_out_of_range(dev)) {
                rdisk_lba_out_of_range(dev);
                /* The LS-120 and Bernoulli 150 stop here.  The other drives have always gone on to the data phase
                   after reporting the error (and failed it there): their behavior is left as it was. */
                if (rdisk_checks_transfer_end(dev))
                    break;
            }
            if (dev->sector_len) {
                max_len               = dev->sector_len;
                dev->requested_blocks = max_len;

                dev->packet_len = max_len * alloc_length;
                rdisk_buf_alloc(dev, dev->packet_len);

                dev->requested_blocks = max_len;
                dev->packet_len       = max_len * rdisk_sector_size(dev);

                rdisk_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

                rdisk_data_command_finish(dev, dev->packet_len, rdisk_sector_size(dev),
                                        dev->packet_len, 1);

                ui_sb_update_icon_write(SB_RDISK | dev->id,
                                  dev->packet_status != PHASE_COMPLETE);
            } else {
                rdisk_set_phase(dev, SCSI_PHASE_STATUS);
                /* rdisk_log(dev->log, "All done - callback set\n"); */
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * RDISK_TIME;
                rdisk_set_callback(dev);
            }
            break;

        case GPCMD_WRITE_SAME_10:
            rdisk_set_phase(dev, SCSI_PHASE_DATA_OUT);
            alloc_length = rdisk_sector_size(dev);

            if ((cdb[1] & 6) == 6)
                rdisk_invalid_field(dev, cdb[1]);
            else {
                if (rdisk_write_denied(dev))
                    rdisk_write_protected(dev);
                else if (rdisk_floptical_locked(dev))
                    rdisk_floptical_needs_unlock(dev);
                else {
                    dev->sector_len = (cdb[7] << 8) | cdb[8];
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) |
                                      (cdb[4] << 8) | cdb[5];

                    if (dev->sector_pos >= dev->drv->medium_size)
                        rdisk_lba_out_of_range(dev);
                    else if (dev->sector_len) {
                        rdisk_buf_alloc(dev, alloc_length);
                        rdisk_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

                        max_len               = 1;
                        dev->requested_blocks = 1;

                        dev->packet_len = alloc_length;

                        rdisk_set_phase(dev, SCSI_PHASE_DATA_OUT);

                        rdisk_data_command_finish(dev, rdisk_sector_size(dev), rdisk_sector_size(dev),
                                                alloc_length, 1);

                        ui_sb_update_icon_write(SB_RDISK | dev->id,
                                          dev->packet_status != PHASE_COMPLETE);
                    } else {
                        rdisk_set_phase(dev, SCSI_PHASE_STATUS);
                        /* rdisk_log(dev->log, "All done - callback set\n"); */
                        dev->packet_status = PHASE_COMPLETE;
                        dev->callback      = 20.0 * RDISK_TIME;
                        rdisk_set_callback(dev);
                    }
                }
            }
            break;

        case GPCMD_MODE_SENSE_6:
        case GPCMD_MODE_SENSE_10:
            rdisk_set_phase(dev, SCSI_PHASE_DATA_IN);

            /* Over ATAPI, the Zip, Jaz and Insider 150 only have a block descriptor in the MODE SENSE (6) of Iomega's
               Tools, which expect it, see the description of page 2F. */
            if (rdisk_is_scsi(dev) ||
                ((IS_ZIP_OR_JAZ(dev->drv->type) || (dev->drv->type == RDISK_TYPE_BERNOULLI_150_IDE)) &&
                 (cdb[0] == GPCMD_MODE_SENSE_6)))
                block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;
            else
                block_desc = 0;

            if (IS_FLOPTICAL(dev->drv->type) && ((cdb[2] & 0x3f) == MS_PAGE_FLOPTICAL))
                dev->floptical_unlocked = 1;

            /* Page 0 only exists as a CDB byte of 0, which returns just the header and block descriptor, and
               the Bernoulli 150 has no saved values. */
            if (IS_BERNOULLI_150(dev->drv->type) && cdb[2] && !(cdb[2] & 0x3f)) {
                rdisk_invalid_field(dev, cdb[2]);
                break;
            }

            if (IS_BERNOULLI_150(dev->drv->type) && ((cdb[2] & 0xc0) == 0xc0) &&
                (rdisk_mode_sense_page_flags(dev) & (1ULL << (cdb[2] & 0x3f)))) {
                rdisk_saving_not_supported(dev);
                break;
            }

            if (cdb[0] == GPCMD_MODE_SENSE_6) {
                len = cdb[4];
                rdisk_buf_alloc(dev, 256);
            } else {
                len = (cdb[8] | (cdb[7] << 8));
                rdisk_buf_alloc(dev, 65536);
            }

            if (rdisk_mode_sense_page_flags(dev) & (1ULL << (cdb[2] & 0x3f))) {
                memset(dev->buffer, 0, len);
                alloc_length = len;

                if (cdb[0] == GPCMD_MODE_SENSE_6) {
                    len            = rdisk_mode_sense(dev, dev->buffer, 4, cdb[2],
                                                      block_desc);
                    /* The Bernoulli 150 reports the length of all the data, whatever the host asked for. */
                    dev->buffer[0] = (IS_BERNOULLI_150(dev->drv->type) ? len : MIN(len, alloc_length)) - 1;
                    len            = MIN(len, alloc_length);
                    dev->buffer[1] = rdisk_mode_sense_medium_type(dev);
                    /* Device-specific parameter: WP (bit 7) for a write-protected medium. */
                    if (rdisk_reports_write_protect(dev) && (dev->drv->fp != NULL) && rdisk_write_denied(dev))
                        dev->buffer[2] |= 0x80;
                    if (block_desc)
                        dev->buffer[3] = 8;
                } else {
                    len            = rdisk_mode_sense(dev, dev->buffer, 8, cdb[2],
                                                      block_desc);
                    len            = MIN(len, alloc_length);
                    dev->buffer[0] = (len - 2) >> 8;
                    dev->buffer[1] = (len - 2) & 255;
                    dev->buffer[2] = rdisk_mode_sense_medium_type(dev);
                    if (rdisk_reports_write_protect(dev) && (dev->drv->fp != NULL) && rdisk_write_denied(dev))
                        dev->buffer[3] |= 0x80;
                    if (block_desc) {
                        dev->buffer[6] = 0;
                        dev->buffer[7] = 8;
                    }
                }

                rdisk_set_buf_len(dev, BufLen, &len);

                rdisk_log(dev->log, "Reading mode page: %02X...\n", cdb[2]);

                rdisk_data_command_finish(dev, len, len, alloc_length, 0);
            } else {
                rdisk_invalid_field(dev, cdb[2]);
                rdisk_buf_free(dev);
            }
            break;

        case GPCMD_MODE_SELECT_6:
        case GPCMD_MODE_SELECT_10:
            rdisk_set_phase(dev, SCSI_PHASE_DATA_OUT);

            if (cdb[0] == GPCMD_MODE_SELECT_6) {
                len = cdb[4];
                rdisk_buf_alloc(dev, 256);
            } else {
                len = (cdb[7] << 8) | cdb[8];
                rdisk_buf_alloc(dev, 65536);
            }

            rdisk_set_buf_len(dev, BufLen, &len);

            dev->total_length = len;
            dev->do_page_save = cdb[1] & 1;

            rdisk_data_command_finish(dev, len, len, len, 1);
            return;

        case GPCMD_START_STOP_UNIT:
            rdisk_set_phase(dev, SCSI_PHASE_STATUS);

            /* The Bernoulli 150 only looks at the Start bit: stopping also ejects the cartridge, unless its
               removal is prevented, and starting without one fails. */
            if (IS_BERNOULLI_150(dev->drv->type)) {
                if (cdb[4] & 1) {
                    if (dev->drv->fp == NULL) {
                        rdisk_not_ready(dev);
                        break;
                    }
                } else if (!dev->prevent)
                    rdisk_eject(dev->id);

                rdisk_command_complete(dev);
                break;
            }

            switch (cdb[4] & 3) {
                case 0:                 /* Stop the disc. */
                    rdisk_eject(dev->id); /* The Iomega Windows 9x drivers require this. */
                    break;
                case 1: /* Start the disc and read the TOC. */
                    break;
                case 2: /* Eject the disc if possible. */
                    /* SpinUp_Down() in SCSIDLL.DLL ejects a Jaz disk like this, and a Zip disk with case 0. */
                    if ((dev->drv->type == RDISK_TYPE_JAZ_1GB) || (dev->drv->type == RDISK_TYPE_JAZ_2GB))
                        rdisk_eject(dev->id);
                    break;
                case 3: /* Load the disc (close tray). */
                    rdisk_reload(dev->id);
                    break;

                default:
                    break;
            }

            rdisk_command_complete(dev);
            break;

        case GPCMD_INQUIRY:
            rdisk_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[3];
            max_len <<= 8;
            max_len |= cdb[4];

            rdisk_buf_alloc(dev, 65536);

            if (cdb[1] & 1) {
                preamble_len = 4;
                size_idx     = 3;

                dev->buffer[idx++] = 0;
                dev->buffer[idx++] = cdb[2];
                dev->buffer[idx++] = 0;

                idx++;

                switch (cdb[2]) {
                    case 0x00:
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 0x83;
                        break;
                    case 0x83:
                        if (idx + 24 > max_len) {
                            rdisk_data_phase_error(dev, cdb[2]);
                            rdisk_buf_free(dev);
                            return;
                        }

                        dev->buffer[idx++] = 0x02;
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 20;
                        ide_padstr8(dev->buffer + idx, 20, "53R141"); /* Serial */
                        idx += 20;

                        if (idx + 72 > cdb[4])
                            goto atapi_out;
                        dev->buffer[idx++] = 0x02;
                        dev->buffer[idx++] = 0x01;
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 68;
                        /* Vendor and product: from rdisk_drive_types[]; the generic drive reports the emulator's. */
                        if (rdisk_inquiry_identity(dev) != NULL)
                            ide_padstr8(dev->buffer + idx, 8, rdisk_inquiry_identity(dev)->vendor);
                        else
                            ide_padstr8(dev->buffer + 8, 8, EMU_NAME);          /* Vendor */
                        idx += 8;
                        /* Product */
                        if (rdisk_inquiry_identity(dev) != NULL)
                            ide_padstr8(dev->buffer + idx, 40, rdisk_inquiry_identity(dev)->model);
                        else
                            ide_padstr8(dev->buffer + 16, 40, device_identify);      /* Product */
                        idx += 40;
                        ide_padstr8(dev->buffer + idx, 20, "53R141");
                        idx += 20;
                        break;
                    default:
                        rdisk_log(dev->log, "INQUIRY: Invalid page: %02X\n", cdb[2]);
                        rdisk_invalid_field(dev, cdb[2]);
                        rdisk_buf_free(dev);
                        return;
                }
            } else {
                preamble_len = 5;
                size_idx     = 4;

                memset(dev->buffer, 0, 8);
                if ((cdb[1] & 0xe0) || ((dev->cur_lun > 0x00) && (dev->cur_lun < 0xff)))
                    dev->buffer[0] = 0x7f;    /* No physical device on this LUN */
                else
                    dev->buffer[0] = IS_REV(dev->drv->type) ? 0x05 : 0x00;    /* MMC device or hard disk */
                dev->buffer[1] = 0x80;        /* Removable */
                /* SCSI-2 compliant */
                dev->buffer[2] = rdisk_is_scsi(dev) ? 0x02 : 0x00;
                dev->buffer[3] = rdisk_is_scsi(dev) ? 0x02 : 0x21;
                /* The Floptical is a SCSI-1 device (Aaru report 258). */
                if ((dev->drv->type == RDISK_TYPE_FLOPTICAL_20) && rdisk_is_scsi(dev))
                    dev->buffer[2] = 0x01;
#if 0
                dev->buffer[4] = 31;
#endif
                dev->buffer[4] = 0;
                if (rdisk_is_scsi(dev) && !IS_NARROW_SCSI_ONLY(dev->drv->type)) {
                    dev->buffer[6] = 1;       /* 16-bit transfers supported */
                    dev->buffer[7] = 0x20;    /* Wide bus supported */
                }
                dev->buffer[7] |= 0x02;

                if (rdisk_inquiry_identity(dev) != NULL) {
                    /* Vendor, product and revision come from the drive's row in rdisk_drive_types[]. */
                    ide_padstr8(dev->buffer + 8, 8, rdisk_inquiry_identity(dev)->vendor);       /* Vendor */
                    ide_padstr8(dev->buffer + 16, 16, rdisk_inquiry_identity(dev)->model);      /* Product */
                    ide_padstr8(dev->buffer + 32, 4, rdisk_inquiry_identity(dev)->revision);    /* Revision */

                    /* The Zip 250 and Zip 750 also carry a build date at 36 and a copyright string at 96. */
                    if (dev->drv->type == RDISK_TYPE_ZIP_250) {
                        /* Date? */
                        if (max_len >= 44)
                            ide_padstr8(dev->buffer + 36, 8, "08/08/01");
                        if (max_len >= 122)
                            ide_padstr8(dev->buffer + 96, 26, "(c) Copyright IOMEGA 2000 "); /* Copyright string */
                    } else if (dev->drv->type == RDISK_TYPE_ZIP_750) {
                        /* Real USB Zip 750 (ZuluIDE issue 39) */
                        if (max_len >= 44)
                            ide_padstr8(dev->buffer + 36, 8, "08/27/02");
                        if (max_len >= 122)
                            ide_padstr8(dev->buffer + 96, 26, "(c) Copyright IOMEGA 2002 ");
                    }
                } else {
                    /* The generic removable disk. */
                    ide_padstr8(dev->buffer + 8, 8,
                                EMU_NAME);          /* Vendor */
                    ide_padstr8(dev->buffer + 16, 16,
                                device_identify);      /* Product */
                    ide_padstr8(dev->buffer + 32, 4,
                                EMU_VERSION_EX);    /* Revision */
                }
                idx = 36;

                if (max_len == 96) {
                    dev->buffer[4] = 91;
                    idx            = 96;
                } else if (max_len == 128) {
                    dev->buffer[4] = 0x75;
                    idx            = 128;
                }

                if (IS_LS120_COSM05(dev->drv->type) && rdisk_on_ide(dev->drv)) {
                    /* A real drive: always 128 bytes, of which only the first 96 were captured.  The firmware's
                       second half is at 36, followed by the 03 C3 00 of the capture: DISKVSD.VXD of Windows 95 OSR2
                       reads 44 bytes of an ATAPI floppy and, if the additional length is at least 27h, takes bytes
                       40 to 42 as its blocks, 246,528 here, with 8 heads and 32 sectors per track. */
                    dev->buffer[3] = 0x01;
                    dev->buffer[7] = 0x00;
                    memset(dev->buffer + 36, 0x00, 128 - 36);
                    memcpy(dev->buffer + 36, (dev->drv->type == RDISK_TYPE_SUPERDISK_120) ? "C105" : "M09T", 4);
                    dev->buffer[40] = 0x03;
                    dev->buffer[41] = 0xc3;
                    idx             = 128;
                } else if (IS_BERNOULLI_150(dev->drv->type)) {
                    /* From the ROM: always 128 bytes, with sync, linked commands and command queueing.  With a
                       cartridge that has the data, the real drive appends 40 bytes of it and sets byte 44 from
                       it, which is not emulated. */
                    dev->buffer[7] = 0x1a;
                    memset(dev->buffer + 36, 0x00, 128 - 36);
                    memcpy(dev->buffer + 36, "04/04/94", 8);
                    memcpy(dev->buffer + 98, "(c) Copyright IOMEGA 1992     ", 30);
                    idx            = 128;
                } else if (IS_REV(dev->drv->type)) {
                    /* Aaru reports 148, 187 and 188: no ANSI version, NormACA, HiSup, and three version
                       descriptors. */
                    dev->buffer[2] = 0x00;
                    dev->buffer[3] = 0x32;
                    dev->buffer[6] = 0x00;
                    dev->buffer[7] = 0x00;
                    memset(dev->buffer + 36, 0x00, 96 - 36);
                    dev->buffer[58] = 0xa0;
                    dev->buffer[59] = 0x03;
                    dev->buffer[60] = 0x29;
                    dev->buffer[61] = 0x17;
                    dev->buffer[62] = 0x30;
                    dev->buffer[63] = 0x17;
                    idx             = 96;
                }
            }

atapi_out:
            dev->buffer[size_idx] = idx - preamble_len;
            len                   = idx;

            len = MIN(len, max_len);
            rdisk_set_buf_len(dev, BufLen, &len);

            rdisk_data_command_finish(dev, len, len, max_len, 0);
            break;

        case GPCMD_GET_CONFIGURATION:
            /* Rev only.  Reports 187 and 188 give the features but no profile, so Removable Disk is taken
               from the eject, lock, 2048-byte blocks with a blocking of 32 and format features they list. */
            rdisk_set_phase(dev, SCSI_PHASE_DATA_IN);
            max_len = (cdb[7] << 8) | cdb[8];
            rdisk_buf_alloc(dev, 65536);
            memset(dev->buffer, 0, 128);
            len = rdisk_rev_configuration(dev, dev->buffer, (cdb[2] << 8) | cdb[3], cdb[1] & 3);
            len = MIN(len, max_len);
            rdisk_set_buf_len(dev, BufLen, &len);
            rdisk_data_command_finish(dev, len, len, max_len, 0);
            break;

        case GPCMD_PREVENT_REMOVAL:
            rdisk_set_phase(dev, SCSI_PHASE_STATUS);

            if (IS_BERNOULLI_150(dev->drv->type)) {
                /* Preventing removal needs a cartridge. */
                if ((cdb[4] & 1) && (dev->drv->fp == NULL)) {
                    rdisk_not_ready(dev);
                    break;
                }

                dev->prevent = cdb[4] & 1;
            }

            rdisk_command_complete(dev);
            break;

        case RDISK_CMD_PRE_FETCH:
        case GPCMD_SYNCHRONIZE_CACHE:
        case RDISK_CMD_LOCK_UNLOCK_CACHE:
            /* Only the Bernoulli 150 has these, see bernoulli_150_command_flags[].  An image has no cache, so
               all that is left of them is the range check. */
            rdisk_set_phase(dev, SCSI_PHASE_STATUS);
            dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
            dev->sector_len = (cdb[7] << 8) | cdb[8];

            if ((dev->drv->fp != NULL) && rdisk_transfer_out_of_range(dev))
                rdisk_lba_out_of_range(dev);
            else
                rdisk_command_complete(dev);
            break;

        case GPCMD_SEEK_6:
        case GPCMD_SEEK_10:
            rdisk_set_phase(dev, SCSI_PHASE_STATUS);

            switch (cdb[0]) {
                case GPCMD_SEEK_6:
                    pos = (cdb[2] << 8) | cdb[3];
                    break;
                case GPCMD_SEEK_10:
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    break;

                default:
                    break;
            }
            rdisk_seek(dev, pos);
            rdisk_command_complete(dev);
            break;

        case GPCMD_READ_CDROM_CAPACITY:
            rdisk_set_phase(dev, SCSI_PHASE_DATA_IN);

            if (IS_BERNOULLI_150(dev->drv->type)) {
                /* An LBA is only valid with PMI. */
                if (!(cdb[8] & 1) && (cdb[2] || cdb[3] || cdb[4] || cdb[5])) {
                    rdisk_invalid_field(dev, cdb[2]);
                    break;
                }

                if (dev->drv->fp == NULL) {
                    rdisk_no_format_information(dev);
                    break;
                }
            }

            rdisk_buf_alloc(dev, 8);

            /* IMPORTANT: What's returned is the last LBA block. */
            max_len = dev->drv->medium_size - 1;
            memset(dev->buffer, 0, 8);
            dev->buffer[0] = (max_len >> 24) & 0xff;
            dev->buffer[1] = (max_len >> 16) & 0xff;
            dev->buffer[2] = (max_len >> 8) & 0xff;
            dev->buffer[3] = max_len & 0xff;
            dev->buffer[4] = (rdisk_sector_size(dev) >> 24) & 0xff;    /* Block length. */
            dev->buffer[5] = (rdisk_sector_size(dev) >> 16) & 0xff;
            dev->buffer[6] = (rdisk_sector_size(dev) >> 8) & 0xff;
            dev->buffer[7] = rdisk_sector_size(dev) & 0xff;
            len            = 8;

            rdisk_set_buf_len(dev, BufLen, &len);

            rdisk_data_command_finish(dev, len, len, len, 0);
            break;

        case GPCMD_IOMEGA_EJECT:
            /* ImDrvFmt.dll sends 0D 00 DA to a Rev before it formats, with the disk staying in, so whatever this
               does on a Rev (it is not known) it is not an eject. */
            if (IS_REV(dev->drv->type)) {
                if (cdb[2] != 0xda) {
                    rdisk_illegal_opcode(dev, cdb[0]);
                    break;
                }
            } else
                rdisk_eject(dev->id);

            rdisk_set_phase(dev, SCSI_PHASE_STATUS);
            rdisk_command_complete(dev);
            break;

        case RDISK_CMD_CALEB_VENDOR:
            /* Device::WriteProtect() in Caleb's itDevice.dll, for itWrtCtl.exe: F0 01 00 00 00 00 0F 00 00 00 protects
               and the same with FFh in byte 7 unprotects.  As a UHD disk has no tab, the real drive keeps this on the
               disk, which an image has no place for: here it lasts until the disk is ejected, like the Zip's.
               Device::BurnFirmware() sends F0 02 with a block of firmware in a 12-byte CDB, which fails here:
               itFwUpdt.exe then says that the device rejected the update.  IsWriteProtected() just writes back
               the last sector and looks for a failure, so nothing else is needed. */
            if ((cdb[1] != 0x01) || (cdb[6] != 0x0f) || ((cdb[7] != 0x00) && (cdb[7] != 0xff)))
                rdisk_invalid_field(dev, cdb[1]);
            else {
                dev->protect_mode = cdb[7] ? 0 : 2;
                rdisk_set_phase(dev, SCSI_PHASE_STATUS);
                rdisk_command_complete(dev);
            }
            break;

        case GPCMD_READ_DVD_STRUCTURE:
            /* Only format C0h, Write Protection Status, is known to be used.  ImDrvInt.dll looks at PWP.  A
               read-only image is a cartridge with its tab set, CWP (ESTIMATED: Rev disks have no such tab). */
            if (cdb[7] != 0xc0) {
                rdisk_invalid_field(dev, cdb[7]);
                break;
            }

            rdisk_set_phase(dev, SCSI_PHASE_DATA_IN);
            rdisk_buf_alloc(dev, 8);
            memset(dev->buffer, 0x00, 8);
            dev->buffer[1] = 0x06;
            dev->buffer[4] = ((rdisk_protection(dev) == 2) ? 0x02 : 0x00) | (dev->drv->read_only ? 0x04 : 0x00);
            len            = MIN(8, (cdb[8] << 8) | cdb[9]);
            rdisk_set_buf_len(dev, BufLen, &len);
            rdisk_data_command_finish(dev, len, len, len, 0);
            break;

        case GPCMD_SEND_DVD_STRUCTURE:
            /* Format C0h again: SetMMCWriteProtect() in ImDrvInt.dll reads the 8 bytes above, changes PWP and
               sends them back.  This is how the software write-protects a disk. */
            len = (cdb[8] << 8) | cdb[9];
            if ((cdb[7] != 0xc0) || (len < 8) || (len > 256)) {
                rdisk_invalid_field(dev, (cdb[7] != 0xc0) ? cdb[7] : len);
                break;
            }

            rdisk_set_phase(dev, SCSI_PHASE_DATA_OUT);
            rdisk_buf_alloc(dev, 256);
            rdisk_set_buf_len(dev, BufLen, &len);
            rdisk_data_command_finish(dev, len, len, len, 1);
            break;

        case GPCMD_READ_FORMAT_CAPACITIES:
            len = (cdb[7] << 8) | cdb[8];

            /* Room for the whole list: the allocation length in the CDB can be smaller than it. */
            rdisk_buf_alloc(dev, 64);
            memset(dev->buffer, 0, 64);

            /* The list header, whose length is filled in below. */
            pos = 4;

            /* Current/Maximum capacity header.  A drive that takes several media (Zip 250, Jaz 2GB, the
               SuperDisk drives...) returns the inserted medium's size, if there is one, otherwise its
               largest medium; a one-medium drive always returns its medium. */
            {
                const int      current     = (dev->drv->fp != NULL) && !rdisk_single_medium_drive(dev);
                const uint32_t medium_size = current ? dev->drv->medium_size : rdisk_max_medium_size(dev);
                const uint32_t bps         = current ? rdisk_sector_size(dev) :
                                                       rdisk_types[rdisk_max_medium_index(dev)].bytes_per_sector;

                pos = rdisk_format_descriptor(dev->buffer, pos, medium_size,
                                              (dev->drv->fp != NULL) ? 2 : 3, bps);    /* Current / maximum capacity */
            }

            /* Formattable capacity descriptors.  A real LS-120 COSM 05 lists its own medium even with no disk in
               the drive, and with a 1.44 MB disk the three formats of that disk.  The other formats of the same
               2HD disk are taken to list them too. */
            if (IS_SUPERDISK_120(dev->drv->type) && (dev->drv->fp == NULL))
                pos = rdisk_format_descriptor(dev->buffer, pos, SUPERDISK_SECTORS, 0, 512);
            else if (IS_SUPERDISK_120(dev->drv->type) && rdisk_is_2hd(dev)) {
                pos = rdisk_format_descriptor(dev->buffer, pos, FD_1440K_SECTORS, 0, 512);
                pos = rdisk_format_descriptor(dev->buffer, pos, FD_1200K_SECTORS, 0, 512);
                pos = rdisk_format_descriptor(dev->buffer, pos, FD_2HD_NEC_SECTORS, 0, 1024);
            } else if (dev->drv->fp != NULL)
                pos = rdisk_format_descriptor(dev->buffer, pos, dev->drv->medium_size, 0, rdisk_sector_size(dev));

            dev->buffer[3] = pos - 4;

            /* What the list holds, not what was asked for: a real drive sends 36 bytes for a 64-byte request. */
            len = MIN(len, pos);

            rdisk_set_buf_len(dev, BufLen, &len);

            rdisk_data_command_finish(dev, len, len, len, 0);
            break;

        default:
            rdisk_illegal_opcode(dev, cdb[0]);
            break;
    }

#if 0
    rdisk_log(dev->log, "Phase: %02X, request length: %i\n",
            dev->tf->phase, dev->tf->request_length);
#endif

    if ((dev->packet_status == PHASE_COMPLETE) || (dev->packet_status == PHASE_ERROR))
        rdisk_buf_free(dev);
}

static void
rdisk_command_stop(scsi_common_t *sc)
{
    rdisk_t *dev = (rdisk_t *) sc;

    rdisk_command_complete(dev);
    rdisk_buf_free(dev);
}

/* MODE SELECT (6) parameter list of the Bernoulli 150, as its firmware checks it.  A failed check changes nothing
   and fails with INVALID FIELD IN PARAMETER LIST.  Returns 1 if it is valid. */
static int
rdisk_bernoulli_150_mode_select(rdisk_t *dev)
{
    const mode_sense_pages_t *chg   = &bernoulli_150_mode_sense_pages_changeable;
    const uint8_t            *buf   = dev->buffer;
    const int                 total = dev->total_length;
    int                       start;

    if (total == 0)
        return 1;

    /* Mode data length, medium type and device-specific parameter are zero, the block descriptors come in 8s. */
    if ((total < 4) || buf[0] || buf[1] || buf[2] || (buf[3] & 7) || ((4 + buf[3]) > total)) {
        rdisk_invalid_field_pl(dev, 0);
        return 0;
    }

    /* A block descriptor can only say what the drive already does: blocks of 512 bytes. */
    for (int i = 4; i < (4 + buf[3]); i += 8) {
        for (int j = 0; j < 8; j++) {
            if (buf[i + j] != ((j == 6) ? 0x02 : 0x00)) {
                rdisk_invalid_field_pl(dev, i + j);
                return 0;
            }
        }
    }

    start = 4 + buf[3];

    /* Check all of it before changing any of it. */
    for (int apply = 0; apply < 2; apply++) {
        int pos = start;

        while (pos < total) {
            const uint8_t page = buf[pos] & 0x3f;
            const uint8_t plen = ((pos + 1) < total) ? buf[pos + 1] : 0xff;

            if ((pos + 2 + plen) > total) {
                rdisk_invalid_field_pl(dev, pos);
                return 0;
            }

            /* The length of a page has to be the drive's, and its fixed bits have to be 0. */
            if (!chg->pages[page][1] || (plen != chg->pages[page][1])) {
                rdisk_invalid_field_pl(dev, pos);
                return 0;
            }

            for (int i = 0; i < plen; i++) {
                if (buf[pos + 2 + i] & ~chg->pages[page][i + 2]) {
                    rdisk_invalid_field_pl(dev, pos + 2 + i);
                    return 0;
                }

                if (apply)
                    dev->ms_pages_saved.pages[page][i + 2] = buf[pos + 2 + i];
            }

            /* Some combinations of the error recovery bits are not allowed. */
            if (page == 0x01) {
                const uint8_t mode = buf[pos + 2] & 0x0f;

                if ((mode == 0x02) || (mode == 0x03) || (mode == 0x09) || (mode == 0x0a) ||
                    (mode == 0x0b) || (mode == 0x0f)) {
                    rdisk_invalid_field_pl(dev, pos + 2);
                    return 0;
                }
            }

            pos += 2 + plen;
        }
    }

    return 1;
}

/* The command second phase function, needed for Mode Select. */
/* A full format of a HiFD disk deletes everything on it, see the HiFD comment at the top. */
static int
rdisk_hifd_format(rdisk_t *dev, const uint8_t flags)
{
    static const uint8_t zero[4096] = { 0 };
    uint64_t             left       = (uint64_t) dev->drv->medium_size * rdisk_sector_size(dev);

    if (flags & 0x20)    /* DCRT */
        return 1;

    if (fseeko64(dev->drv->fp, dev->drv->base, SEEK_SET) == -1) {
        rdisk_write_error(dev);
        return 0;
    }

    while (left > 0) {
        const size_t n = (size_t) MIN(left, sizeof(zero));

        if (fwrite(zero, 1, n, dev->drv->fp) != n) {
            rdisk_write_error(dev);
            return 0;
        }

        left -= n;
    }

    fflush(dev->drv->fp);

    return 1;
}

static uint8_t
rdisk_phase_data_out(scsi_common_t *sc)
{
    rdisk_t *dev              = (rdisk_t *) sc;
    int      len            = 0;
    uint8_t  error          = 0;
    uint32_t last_to_write;
    uint32_t i;
    uint16_t block_desc_len;
    uint16_t pos;
    uint16_t param_list_len;
    uint8_t  hdr_len;
    uint8_t  val;

    switch (dev->current_cdb[0]) {
        case GPCMD_FORMAT_UNIT:
            if ((dev->drv->type == RDISK_TYPE_HIFD_200) && !rdisk_hifd_format(dev, dev->buffer[1])) {
                rdisk_buf_free(dev);
                return 0;
            }
            break;
        case GPCMD_IOMEGA_SET_PROTECTION_MODE:
            /* On a Rev the views of "AmpSetView" have no effect here: a disk has the one view, of all of it. */
            if (!dev->current_cdb[2] &&
                !rdisk_set_protection(dev, dev->current_cdb[1], dev->buffer, dev->current_cdb[4])) {
                rdisk_buf_free(dev);
                return 0;
            }
            break;
        case GPCMD_SEND_DVD_STRUCTURE:
            if (!rdisk_set_protection(dev, (dev->buffer[4] & 0x02) ? 2 : 0, NULL, 0)) {
                rdisk_buf_free(dev);
                return 0;
            }
            break;
        case GPCMD_VERIFY_6:
        case GPCMD_VERIFY_10:
        case GPCMD_VERIFY_12:
            break;
        case GPCMD_WRITE_6:
        case GPCMD_WRITE_10:
        case GPCMD_WRITE_AND_VERIFY_10:
        case GPCMD_WRITE_12:
        case GPCMD_WRITE_AND_VERIFY_12:
            if (dev->requested_blocks > 0)
                rdisk_blocks(dev, &len, 1);
            break;
        case GPCMD_WRITE_SAME_10:
            if (!dev->current_cdb[7] && !dev->current_cdb[8]) {
                last_to_write = (dev->drv->medium_size - 1);
            } else
                last_to_write = dev->sector_pos + dev->sector_len - 1;

            for (i = dev->sector_pos; i <= last_to_write; i++) {
                if (dev->current_cdb[1] & 2) {
                    dev->buffer[0] = (i >> 24) & 0xff;
                    dev->buffer[1] = (i >> 16) & 0xff;
                    dev->buffer[2] = (i >> 8) & 0xff;
                    dev->buffer[3] = i & 0xff;
                } else if (dev->current_cdb[1] & 4) {
                    /* CHS are 96, 1, 2048 (RDISK 100) and 239, 1, 2048 (RDISK 250) */
                    const uint32_t s = (i % 2048);
                    const uint32_t h = ((i - s) / 2048) % 1;
                    const uint32_t c = ((i - s) / 2048) / 1;
                    dev->buffer[0]   = (c >> 16) & 0xff;
                    dev->buffer[1]   = (c >> 8) & 0xff;
                    dev->buffer[2]   = c & 0xff;
                    dev->buffer[3]   = h & 0xff;
                    dev->buffer[4]   = (s >> 24) & 0xff;
                    dev->buffer[5]   = (s >> 16) & 0xff;
                    dev->buffer[6]   = (s >> 8) & 0xff;
                    dev->buffer[7]   = s & 0xff;
                }
                uint64_t offs = 0;

                if (!rdisk_block_offset(dev, i, &offs))
                    continue;    /* A block of the 640 KB medium that does not exist. */
                if (fseeko64(dev->drv->fp, offs, SEEK_SET) == -1)
                    log_fatal(dev->log, "rdisk_phase_data_out(): Error seeking\n");
                if (fwrite(dev->buffer, 1, rdisk_sector_size(dev), dev->drv->fp) != rdisk_sector_size(dev))
                    log_fatal(dev->log, "rdisk_phase_data_out(): Error writing data\n");
            }

            fflush(dev->drv->fp);
            break;
        case GPCMD_MODE_SELECT_6:
        case GPCMD_MODE_SELECT_10:
            if (IS_BERNOULLI_150(dev->drv->type)) {
                if (!rdisk_bernoulli_150_mode_select(dev)) {
                    rdisk_buf_free(dev);
                    return 0;
                }

                break;
            }

            if (dev->current_cdb[0] == GPCMD_MODE_SELECT_10) {
                hdr_len        = 8;
                param_list_len = dev->current_cdb[7];
                param_list_len <<= 8;
                param_list_len |= dev->current_cdb[8];
            } else {
                hdr_len        = 4;
                param_list_len = dev->current_cdb[4];
            }

            if (rdisk_is_scsi(dev)) {
                if (dev->current_cdb[0] == GPCMD_MODE_SELECT_6)
                    block_desc_len = dev->buffer[3];    /* Byte 2 is the device-specific parameter. */
                else {
                    block_desc_len = dev->buffer[6];
                    block_desc_len <<= 8;
                    block_desc_len |= dev->buffer[7];
                }
            } else
                block_desc_len = 0;

            pos = hdr_len + block_desc_len;

            while (1) {
                if (pos >= param_list_len) {
                    rdisk_log(dev->log, "Buffer has only block descriptor\n");
                    break;
                }

                const uint8_t page     = dev->buffer[pos] & 0x3f;
                const uint8_t page_len = dev->buffer[pos + 1];

                pos += 2;

                if (!(rdisk_mode_sense_page_flags(dev) & (1LL << ((uint64_t) page))))
                    error |= 1;
                else for (i = 0; i < page_len; i++) {
                    const uint8_t old_val = dev->ms_pages_saved.pages[page][i + 2];
                    const uint8_t ch      = rdisk_mode_sense_pages_changeable(dev)->pages[page][i + 2];
                    val                   = dev->buffer[pos + i];
                    if (val != old_val) {
                        if (ch)
                            dev->ms_pages_saved.pages[page][i + 2] = val;
                        else {
                            error |= 1;
                            rdisk_invalid_field_pl(dev, val);
                        }
                    }
                }

                pos += page_len;

                if (rdisk_is_scsi(dev))
                    val = zip_100_mode_sense_pages_default_scsi.pages[page][0] & 0x80;
                else
                    val = zip_100_mode_sense_pages_default.pages[page][0] & 0x80;
                if (dev->do_page_save && val)
                    rdisk_mode_sense_save(dev);

                if (pos >= dev->total_length)
                    break;
            }

            if (error) {
                rdisk_buf_free(dev);
                return 0;
            }
            break;

        default:
            break;
    }

    rdisk_command_stop((scsi_common_t *) dev);
    return 1;
}

/* Peform a master init on the entire module. */
void
rdisk_global_init(void)
{
    /* Clear the global data. */
    memset(rdisk_drives, 0x00, sizeof(rdisk_drives));
}

/* Is this IDE device an LS-120 COSM 05?  It is a PIO mode 2 drive with no DMA (IDENTIFY words 49 and 51). */
static int
rdisk_ide_is_ls120_cosm05(const ide_t *ide)
{
    const rdisk_t *dev = (const rdisk_t *) ide->sc;

    return (dev != NULL) && IS_LS120_COSM05(rdisk_drives[dev->id].type);
}

static int
rdisk_get_max(const ide_t *ide, const int ide_has_dma, const int type)
{
    const int dma = ide_has_dma && !rdisk_ide_is_ls120_cosm05(ide);
    int       ret;

    switch (type) {
        case TYPE_PIO:
            ret = rdisk_ide_is_ls120_cosm05(ide) ? 2 : 3;
            break;
        case TYPE_SDMA:
        default:
            ret = -1;
            break;
        case TYPE_MDMA:
            ret = dma ? 1 : -1;
            break;
        case TYPE_UDMA:
            ret = dma ? 5 : -1;
            break;
    }

    return ret;
}

static int
rdisk_get_timings(const ide_t *ide, const int ide_has_dma, const int type)
{
    int ret;

    switch (type) {
        case TIMINGS_DMA:
            ret = (ide_has_dma && !rdisk_ide_is_ls120_cosm05(ide)) ? 0x96 : 0;
            break;
        case TIMINGS_PIO:
            ret = 0xf0;
            break;
        case TIMINGS_PIO_FC:
            ret = 0xb4;
            break;
        default:
            ret = 0;
            break;
    }

    return ret;
}

static void
rdisk_zip_100_identify(const ide_t *ide)
{
    ide_padstr((char *) (ide->buffer + 23), "E.08", 8);                  /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), "IOMEGA  ZIP 100       ATAPI", 40); /* Model, of a real IDE Zip 100 (ZuluIDE firmware) */
}

/* "SyQuest " and the product is what SQATDRVR.SYS looks for, and a real "SyQuest EZ135A" was seen by Linux. */
static void
rdisk_syquest_ide_identify(const ide_t *ide, const uint32_t type)
{
    char model[40];

    snprintf(model, sizeof(model), "SyQuest %s", rdisk_drive_types[type].model);
    ide_padstr((char *) (ide->buffer + 23), rdisk_drive_types[type].revision, 8);    /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), model, 40);                              /* Model */
}

/* ESTIMATED: the model is written like that of the native ATA Zip 100, "IOMEGA  ZIP 100", in ASPIIDE.SYS. */
static void
rdisk_bernoulli_150_ide_identify(const ide_t *ide)
{
    ide_padstr((char *) (ide->buffer + 23), "T.51", 8);                 /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), "IOMEGA  BETA150-IDE", 40); /* Model */
}

static void
rdisk_zip_250_identify(const ide_t *ide, const int ide_has_dma)
{
    /* Firmware */
    ide_padstr((char *) (ide->buffer + 23), "42.S", 8);
    /* Model */
    ide_padstr((char *) (ide->buffer + 27), "IOMEGA  ZIP 250       ATAPI", 40);

    if (ide_has_dma) {
        ide->buffer[80] = 0x70;    /* Supported ATA versions : ATA/ATAPI-4 ATA/ATAPI-6 */
        /* Maximum ATA revision supported : ATA/ATAPI-6 T13 1410D revision 3a */
        ide->buffer[81] = 0x19;
    }
}

static void
rdisk_superdisk_identify(const ide_t *ide, const int ide_has_dma, const uint32_t type)
{
    const char *model;
    const char *firmware;

    /* ATA model and firmware of each SuperDisk model.  Only the LS-120 VER5 (Aaru report 66) is a real
       IDENTIFY dump (its internal spacing is inferred from its INQUIRY); the others are derived from the
       INQUIRY strings or, for the Imation ones, from the original table.

       The spacing matters: "Floppy" has to be the last 6 of the 40 characters.  That is how ESDI_506.PDR of Windows 95
       OSR2 (4.00.1111) tells an ATAPI floppy, for which DISKVSD.VXD then uses READ FORMAT CAPACITIES and not READ
       CAPACITY.  The real COSM 05's model has this form, so the VER5 ones are spaced like it (ESTIMATED). */
    switch (type) {
        case RDISK_TYPE_SUPERDISK_120_VER5:
            model    = "LS-120 VER5   00              UHD Floppy";
            firmware = "F523M5A9";
            break;
        case RDISK_TYPE_SUPERDISK_120_VER5_COMPAQ:
            model    = "LS-120 VER5   AB              UHD Floppy";
            firmware = "F523M5A9";
            break;
        case RDISK_TYPE_SUPERDISK_120_HIMA_COMPAQ:
            model    = "LS-120 HIMA   01              UHD Floppy";
            firmware = "0100";
            break;
        case RDISK_TYPE_SUPERDISK_120_IMATION:
            model    = "SUPERDISK 120 ATAPI";
            firmware = "04";
            break;
        case RDISK_TYPE_SUPERDISK_240_IMATION:
            model    = "SUPERDISK  240       ATAPI";
            firmware = "04";
            break;
        case RDISK_TYPE_SUPERDISK_240:
            model    = "LS-120/240    00";
            firmware = "K047";
            break;
        case RDISK_TYPE_SUPERDISK_120_COSM04_MATSHITA:
            model    = "LS-120 COSM   04              UHD Floppy";
            firmware = "0270M09T";
            break;
        default:
            /* From a real drive: its ATA model really is "CSMO", unlike the "COSM" of its INQUIRY data. */
            model    = "LS-120 CSMO   05              UHD Floppy";
            firmware = "0512C105";
            break;
    }

    ide_padstr((char *) (ide->buffer + 23), firmware, 8);    /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), model, 40);      /* Model */

    if (ide_has_dma) {
        ide->buffer[80] = 0x70;    /* Supported ATA versions : ATA/ATAPI-4 ATA/ATAPI-6 */
        /* Maximum ATA revision supported : ATA/ATAPI-6 T13 1410D revision 3a */
        ide->buffer[81] = 0x19;
    }
}

/*
   The rest of the IDENTIFY PACKET DEVICE data of a real LS-120 COSM 05, on top of the strings above: the serial
   number of the drive it was captured from, and ATA version words and a word 126 of 0.  The geometry and capacity
   words are 0 with no disk.  They were captured with a 1.44 MB disk only, in which word 4 is the medium type, and the
   capacity words hold the high half first; the other media are taken to work the same way, with the LS-120
   disk at its own 963 cylinders * 8 heads * 32 sectors.

   Words 53 (bit 1), 67 and 68, which the real drive sets to 0003h, 00F0h and 00F0h, belong to ide_identify(),
   which only reports them for PIO mode 3 and up.
 */
static void
rdisk_ls120_cosm05_identify(const ide_t *ide, const rdisk_t *rdisk)
{
    uint16_t cyl;
    uint16_t bps;
    uint16_t rate;
    uint16_t rpm;
    uint8_t  heads;
    uint8_t  spt;

    ide_padstr((char *) (ide->buffer + 10), "7ZABB0063833", 20);    /* Serial Number */

    ide->buffer[49]  = 0x0a00;    /* IORDY and LBA */
    ide->buffer[53]  = 0x0003;
    ide->buffer[80]  = 0x0000;
    ide->buffer[81]  = 0x0000;
    ide->buffer[126] = 0x0000;
    ide->buffer[127] = 0x0101;

    if (rdisk->drv->fp != NULL) {
        const uint32_t blocks = rdisk->drv->medium_size;

        if (!rdisk_floppy_format(rdisk, &cyl, &heads, &spt, &bps, &rate, &rpm)) {
            cyl   = 963;
            heads = 8;
            spt   = 32;
            bps   = 512;
        }

        ide->buffer[1]  = cyl;
        ide->buffer[3]  = heads;
        ide->buffer[4]  = rdisk_mode_sense_medium_type(rdisk);
        ide->buffer[5]  = bps;
        ide->buffer[6]  = spt;
        ide->buffer[54] = cyl;
        ide->buffer[55] = heads;
        ide->buffer[56] = spt;
        ide->buffer[57] = blocks >> 16;
        ide->buffer[58] = blocks & 0xffff;
        ide->buffer[60] = blocks >> 16;
        ide->buffer[61] = blocks & 0xffff;
    }
}

static void
rdisk_generic_identify(const ide_t *ide, const int ide_has_dma, const rdisk_t *rdisk)
{
    char model[40];

    memset(model, 0, 40);
    snprintf(model, 40, "%s %s%02i", EMU_NAME, "86B_RD", rdisk->id);
    ide_padstr((char *) (ide->buffer + 23), EMU_VERSION_EX, 8);    /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), model, 40);               /* Model */

    if (ide_has_dma) {
        ide->buffer[80] = 0x70;    /* Supported ATA versions : ATA/ATAPI-4 ATA/ATAPI-6 */
        /* Maximum ATA revision supported : ATA/ATAPI-6 T13 1410D revision 3a */
        ide->buffer[81] = 0x19;
    }
}

/* The real IDE drive is a native ATA one, not an ATAPI one, so this is as close as a drive here can get to it: both
   the Macintosh driver and ORB Tools 1.64 look for this model, which is also how the Windows registry has it, under Enum, ESDI.
   The firmware is that of the SCSI drive (ESTIMATED).  The ATA commands that SBKUPNT.SYS sends to the real drive have no
   place in a packet device: C3h (write protection, and the operating modes), DAh (media status), DEh and DFh (lock),
   E3h (spin down), EDh (eject), and F0h then 9Ah for IntReassignSectors. */
static void
rdisk_orb_identify(const ide_t *ide, const int ide_has_dma)
{
    ide_padstr((char *) (ide->buffer + 23), "D01K", 8);
    ide_padstr((char *) (ide->buffer + 27), "CASTLEWOOD ORB2-E", 40);

    if (ide_has_dma) {
        ide->buffer[80] = 0x70;    /* Supported ATA versions : ATA/ATAPI-4 ATA/ATAPI-6 */
        ide->buffer[81] = 0x19;
    }
}

static void
rdisk_jaz_1gb_identify(const ide_t *ide, const int ide_has_dma)
{
    /* Firmware */
    ide_padstr((char *) (ide->buffer + 23), "42.S", 8);
    /* Model */
    ide_padstr((char *) (ide->buffer + 27), "iomega jaz 1GB       ATAPI", 40);

    if (ide_has_dma) {
        ide->buffer[80] = 0x70;    /* took from the ZIP 250 config, fix if wrong */
        ide->buffer[81] = 0x19;
    }
}

static void
rdisk_jaz_2gb_identify(const ide_t *ide, const int ide_has_dma)
{
    /* Firmware */
    ide_padstr((char *) (ide->buffer + 23), "42.S", 8);
    /* Model */
    ide_padstr((char *) (ide->buffer + 27), "iomega jaz 2GB       ATAPI", 40);

    if (ide_has_dma) {
        ide->buffer[80] = 0x70;    /* took from the ZIP 250 config, fix if wrong */
        ide->buffer[81] = 0x19;
    }
}

/*
   The real SyJet only ever shipped as a SCSI drive - there is no real ATAPI
   SyJet to match. This exists so that attaching a "SyJet" drive type to an
   IDE/ATAPI bus (which 86Box's generic rdisk framework otherwise permits)
   reports the real SyQuest/SyJet strings instead of silently falling back to
   the generic 86Box-branded identify block.
 */
static void
rdisk_syjet_identify(const ide_t *ide, const int ide_has_dma)
{
    /* Firmware - not confirmed real, see rdisk.h comment. */
    ide_padstr((char *) (ide->buffer + 23), "1.06", 8);
    /* Model */
    ide_padstr((char *) (ide->buffer + 27), "SyQuest SyJet 1.5GB", 40);

    if (ide_has_dma) {
        ide->buffer[80] = 0x70;    /* took from the ZIP 250 config, fix if wrong */
        ide->buffer[81] = 0x19;
    }
}

/*
   Unlike the other rdisk types' IDE story, the real SparQ's IDE/EIDE
   interface used genuine ATA IDENTIFY DEVICE (ECh) and CHS/LBA hard-disk
   commands, not the ATAPI packet interface this function (and 86Box's rdisk
   framework generally, which only implements ATAPI-packet and SCSI command
   sets; RDISK_BUS_IDE is wired to a native-ATA command path for the EZ135A and
   EZ230A only) actually emulates. This is the closest available
   approximation for attaching a SparQ to an IDE/ATAPI bus in 86Box today,
   not a byte-accurate reproduction of the real drive's native-ATA behavior.
 */
static void
rdisk_sparq_identify(const ide_t *ide, const int ide_has_dma)
{
    (void) ide_has_dma;    /* The real drive reports the same version words with or without DMA. */

    /* Model and firmware from Aaru report 175, a real (native ATA) SyQuest SparQ, firmware SA_0043. */
    ide_padstr((char *) (ide->buffer + 23), "SA_0043", 8);
    ide_padstr((char *) (ide->buffer + 27), "SyQuest SparQ", 40);

    /* The same report: ATA/ATAPI-4 only, T13 1153D revision 17. */
    ide->buffer[80] = 0x0010;
    ide->buffer[81] = 0x0017;
}

/* Caleb UHD144: reports itself as "Caleb LS-120", with no firmware revision shown.  That is its INQUIRY data, its ATA
   model is not known.  Caleb's manual says that Windows 95 OSR2 has built-in support and the Windows 95 before it does
   not, which fits a model that ends in "Floppy" at character 34 (see rdisk_superdisk_identify()), but without one the
   drive still works there as an ordinary removable disk, so it was not changed. */
static void
rdisk_uhd144_identify(const ide_t *ide)
{
    ide_padstr((char *) (ide->buffer + 23), "", 8);                             /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), "Caleb LS-120", 40);                /* Model */
}

/*
   Sony HiFD: from Aaru report 118 (a real ATAPI SONY HiFD SFD200, firmware 1264): ATA/ATAPI-4 only,
   minor version not specified.  The report's general configuration says "DRQ within 3 ms"; word 0,
   the PIO/MDMA words and the DMA capability are left to the IDE core.
 */
static void
rdisk_hifd_identify(const ide_t *ide)
{
    ide_padstr((char *) (ide->buffer + 23), "1264", 8);                         /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), "SONY HiFD SFD200", 40);            /* Model */

    ide->buffer[80] = 0x0010;    /* ATA/ATAPI-4 */
    ide->buffer[81] = 0x0000;    /* Minor version not specified */
}

/*
   Clik! 40: strings and capabilities from Aaru report 50 (a real ATAPI IOMEGA Clik! 40 CZ,
   firmware 30cJ): ATA-1 through ATA/ATAPI-4 (T13 1153D revision 15), PIO only.  The model string's
   internal spacing is as displayed in the report and may differ from the raw bytes.  The IORDY bits,
   PIO timing words and the absence of DMA are not set here.
 */
static void
rdisk_clik_identify(const ide_t *ide)
{
    ide_padstr((char *) (ide->buffer + 23), "30cJ", 8);                         /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), "IOMEGA Clik! 40 CZ ATAPI", 40);    /* Model */

    ide->buffer[80] = 0x001e;    /* ATA-1 to ATA/ATAPI-4 */
    ide->buffer[81] = 0x0015;    /* ATA/ATAPI-4 T13 1153D revision 15 */
}

/* Rev: no IDENTIFY data has been seen, so the model is ESTIMATED from the INQUIRY data. */
static void
rdisk_rev_identify(const ide_t *ide, const int ide_has_dma, const int type)
{
    ide_padstr((char *) (ide->buffer + 23), rdisk_drive_types[type].revision, 8);    /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), (type == RDISK_TYPE_REV_35) ? "Iomega RRD" :
               ((type == RDISK_TYPE_REV_70) ? "Iomega RRD2" : "Iomega RRD3"), 40);   /* Model */

    if (ide_has_dma) {
        ide->buffer[80] = 0x70;    /* Supported ATA versions : ATA/ATAPI-4 ATA/ATAPI-6 */
        ide->buffer[81] = 0x19;    /* Maximum ATA revision supported : ATA/ATAPI-6 T13 1410D revision 3a */
    }
}

/*
   Zip 750: the firmware is ZIP_750_REV (seen on USB drives); the ATAPI drive's own firmware and
   model string are NOT confirmed (the model follows the Zip 250's layout).
 */
static void
rdisk_zip_750_identify(const ide_t *ide, const int ide_has_dma)
{
    ide_padstr((char *) (ide->buffer + 23), ZIP_750_REV, 8);                              /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), "IOMEGA  ZIP 750       ATAPI", 40);      /* Model */

    if (ide_has_dma) {
        ide->buffer[80] = 0x70;    /* took from the ZIP 250 config, fix if wrong */
        ide->buffer[81] = 0x19;
    }
}

static void
rdisk_identify(const ide_t *ide, const int ide_has_dma)
{
    const rdisk_t *rdisk = (rdisk_t *) ide->sc;

    if (IS_REV(rdisk_drives[rdisk->id].type))
        ide->buffer[0] = 0x8000 | (5 << 8) | 0x80 | (1 << 5); /* ATAPI device, MMC device, removable */
    else if (IS_LS120_COSM05(rdisk_drives[rdisk->id].type))
        ide->buffer[0] = 0x8000 | (0 << 8) | 0x80 | (0 << 5); /* ATAPI device, Direct Access device, removable media, microprocessor DRQ */
    else if (IS_SUPERDISK(rdisk_drives[rdisk->id].type))
        ide->buffer[0] = 0x8000 | (0 << 8) | 0x80 | (2 << 5); /* ATAPI device, Direct Access device, removable media, accelerated DRQ */
    else
        ide->buffer[0] = 0x8000 | (0 << 8) | 0x80 | (1 << 5); /* ATAPI device, Direct Access device, removable media, interrupt DRQ */

    ide_padstr((char *) (ide->buffer + 10), "", 20);    /* Serial Number */
    ide->buffer[49]  = 0x200;                                  /* LBA supported */
    /* Interpret zero byte count limit as maximum length */
    ide->buffer[126] = 0xfffe;

    if (rdisk_drives[rdisk->id].type == RDISK_TYPE_ZIP_250)
        rdisk_zip_250_identify(ide, ide_has_dma);
    else if (IS_ZIP_100(rdisk_drives[rdisk->id].type))
        rdisk_zip_100_identify(ide);
    else if (rdisk_drives[rdisk->id].type == RDISK_TYPE_JAZ_1GB)
        rdisk_jaz_1gb_identify(ide, ide_has_dma);
    else if (rdisk_drives[rdisk->id].type == RDISK_TYPE_JAZ_2GB)
        rdisk_jaz_2gb_identify(ide, ide_has_dma);
    else if (IS_SYJET(rdisk_drives[rdisk->id].type))
        rdisk_syjet_identify(ide, ide_has_dma);
    else if (rdisk_drives[rdisk->id].type == RDISK_TYPE_SPARQ_1GB)
        rdisk_sparq_identify(ide, ide_has_dma);
    else if (IS_SUPERDISK(rdisk_drives[rdisk->id].type)) {
        rdisk_superdisk_identify(ide, ide_has_dma, rdisk_drives[rdisk->id].type);
        if (IS_LS120_COSM05(rdisk_drives[rdisk->id].type))
            rdisk_ls120_cosm05_identify(ide, rdisk);
    }
    else if (rdisk_drives[rdisk->id].type == RDISK_TYPE_CLIK_40)
        rdisk_clik_identify(ide);
    else if (rdisk_drives[rdisk->id].type == RDISK_TYPE_HIFD_200)
        rdisk_hifd_identify(ide);
    else if (rdisk_drives[rdisk->id].type == RDISK_TYPE_UHD144)
        rdisk_uhd144_identify(ide);
    else if (rdisk_drives[rdisk->id].type == RDISK_TYPE_ZIP_750)
        rdisk_zip_750_identify(ide, ide_has_dma);
    else if (IS_REV(rdisk_drives[rdisk->id].type))
        rdisk_rev_identify(ide, ide_has_dma, rdisk_drives[rdisk->id].type);
    else if (rdisk_drives[rdisk->id].type == RDISK_TYPE_BERNOULLI_150_IDE)
        rdisk_bernoulli_150_ide_identify(ide);
    else if (IS_SYQUEST_IDE(rdisk_drives[rdisk->id].type))
        rdisk_syquest_ide_identify(ide, rdisk_drives[rdisk->id].type);
    else if (rdisk_drives[rdisk->id].type == RDISK_TYPE_ORB_2_2GB)
        rdisk_orb_identify(ide, ide_has_dma);
    else
        rdisk_generic_identify(ide, ide_has_dma, rdisk);
}

static void
rdisk_drive_reset(const int c)
{
    const uint8_t scsi_bus = (rdisk_drives[c].scsi_device_id >> 4) & 0x0f;
    const uint8_t scsi_id  = rdisk_drives[c].scsi_device_id & 0x0f;

    if (rdisk_drives[c].priv == NULL) {
        rdisk_drives[c].priv = (rdisk_t *) calloc(1, sizeof(rdisk_t));
        rdisk_t *dev         = (rdisk_t *) rdisk_drives[c].priv;

        char n[1024]       = { 0 };

        sprintf(n, "Removable Disk %i", c + 1);
        dev->log           = log_open(n);
    }

    rdisk_t *dev = (rdisk_t *) rdisk_drives[c].priv;

    dev->id      = c;
    dev->cur_lun = SCSI_LUN_USE_CDB;

    if (rdisk_drives[c].bus_type == RDISK_BUS_SCSI) {
        if (dev->tf == NULL)
            dev->tf        = (ide_tf_t *) calloc(1, sizeof(ide_tf_t));

        /* SCSI RDISK, attach to the SCSI bus. */
        scsi_device_t *sd = &scsi_devices[scsi_bus][scsi_id];

        sd->sc             = (scsi_common_t *) dev;
        sd->command        = rdisk_command;
        sd->request_sense  = rdisk_request_sense_for_scsi;
        sd->reset          = rdisk_reset;
        sd->phase_data_out = rdisk_phase_data_out;
        sd->command_stop   = rdisk_command_stop;
        sd->type           = SCSI_REMOVABLE_DISK;
    } else if (rdisk_on_ide(&rdisk_drives[c])) {
        /* ATAPI drive, attach to the IDE bus or the bridge's IDE board. */
        ide_t         *id = ide_get_drive(rdisk_ide_channel(&rdisk_drives[c]));
        /* If the IDE channel is initialized, we attach to it,
           otherwise, we do nothing - it's going to be a drive
           that's not attached to anything. */
        if (id) {
            id->sc               = (scsi_common_t *) dev;
            dev->tf              = id->tf;
            IDE_ATAPI_IS_EARLY   = 0;
            id->get_max          = rdisk_get_max;
            id->get_timings      = rdisk_get_timings;
            id->identify         = rdisk_identify;
            id->stop             = NULL;
            id->packet_command   = rdisk_command;
            id->device_reset     = rdisk_reset;
            id->phase_data_out   = rdisk_phase_data_out;
            id->command_stop     = rdisk_command_stop;
            id->bus_master_error = rdisk_bus_master_error;
            if (IS_SUPERDISK(rdisk_drives[c].type))
                id->interrupt_drq    = 0;
            else
                id->interrupt_drq    = 1;

            ide_atapi_attach(id);
        }
    } else if (rdisk_drives[c].bus_type == RDISK_BUS_IDE) {
        /*
         * ATA (not ATAPI) cartridge drive. The IDE code runs it as a hard
         * disk on the reserved slot HDD_NUM + c, whose sector I/O comes back
         * here through rdisk_ata_*(). The CHS geometry is the drive's own
         * cartridge in 16 heads and 32 sectors (512 and 880 cylinders, SyQuest
         * Technical Bulletin 5040).
         */
        const int                 slot = HDD_NUM + c;
        hard_disk_t              *hd   = &hdd[slot];
        const rdisk_drive_type_t *dt   = &rdisk_drive_types[rdisk_drives[c].type % KNOWN_RDISK_DRIVE_TYPES];

        if (dev->tf == NULL)
            dev->tf = (ide_tf_t *) calloc(1, sizeof(ide_tf_t));

        memset(hd, 0x00, sizeof(hard_disk_t));
        hd->bus_type           = HDD_BUS_IDE;
        hd->ide_channel        = rdisk_drives[c].ide_channel;
        hd->hpc                = 16;
        hd->spt                = 32;
        hd->tracks             = rdisk_types[dt->default_media].sectors / (hd->hpc * hd->spt);
        hd->max_multiple_block = 16;
        hd->vendor             = "SyQuest";
        hd->model              = dt->model;
        hd->version            = dt->revision;

        if (!ide_ata_removable_attach(rdisk_drives[c].ide_channel, slot))
            rdisk_log(dev->log, "IDE channel %i absent or taken\n", rdisk_drives[c].ide_channel);
    } else if (rdisk_drives[c].bus_type == RDISK_BUS_LPT) {
        /*
         * Parallel-port drive, reached through a bridge such as the Shuttle
         * EPAT. There is no bus to attach to: the bridge fetches the drive
         * from lpt_rdisk_devices[] by its own port number, and drives it with
         * the same entry points a SCSI drive uses.
         */
        if (dev->tf == NULL)
            dev->tf        = (ide_tf_t *) calloc(1, sizeof(ide_tf_t));

        scsi_device_t *sd = &lpt_rdisk_devices[rdisk_drives[c].res & (PARALLEL_MAX - 1)];

        sd->sc             = (scsi_common_t *) dev;
        sd->command        = rdisk_command;
        sd->request_sense  = rdisk_request_sense_for_scsi;
        sd->reset          = rdisk_reset;
        sd->phase_data_out = rdisk_phase_data_out;
        sd->command_stop   = rdisk_command_stop;
        sd->type           = SCSI_REMOVABLE_DISK;
    }
}

void
rdisk_hard_reset(void)
{
    for (uint8_t c = 0; c < RDISK_NUM; c++) {
        if ((rdisk_drives[c].bus_type == RDISK_BUS_ATAPI) ||
            (rdisk_drives[c].bus_type == RDISK_BUS_IDE)   ||
            (rdisk_drives[c].bus_type == RDISK_BUS_SCSI)  ||
            (rdisk_drives[c].bus_type == RDISK_BUS_LPT)) {

            if (rdisk_drives[c].bus_type == RDISK_BUS_SCSI) {
                const uint8_t scsi_bus = (rdisk_drives[c].scsi_device_id >> 4) & 0x0f;
                const uint8_t scsi_id  = rdisk_drives[c].scsi_device_id & 0x0f;

                /* Make sure to ignore any SCSI RDISK drive that has an out of range SCSI bus. */
                if (scsi_bus >= SCSI_BUS_MAX)
                    continue;

                /* Make sure to ignore any SCSI RDISK drive that has an out of range ID. */
                if (scsi_id >= SCSI_ID_MAX)
                    continue;
            }

            /* Only a drive type with a native ATA interface can be on the IDE bus. */
            if ((rdisk_drives[c].bus_type == RDISK_BUS_IDE) &&
                ((rdisk_drives[c].type >= KNOWN_RDISK_DRIVE_TYPES) ||
                 !rdisk_drive_type_supports_bus(rdisk_drive_types[rdisk_drives[c].type].drive_type, RDISK_BUS_IDE)))
                continue;

            /* Make sure to ignore any ATAPI or IDE RDISK drive that has an out of range IDE channel. */
            if (((rdisk_drives[c].bus_type == RDISK_BUS_ATAPI) || (rdisk_drives[c].bus_type == RDISK_BUS_IDE)) &&
                (rdisk_drives[c].ide_channel >= IDE_DRIVES_MAX))
                continue;

            /* A bridge with an IDE board of its own: the board comes first,
               the drive is an ATAPI drive on it. */
            if ((rdisk_drives[c].bus_type == RDISK_BUS_LPT) && rdisk_on_ide(&rdisk_drives[c]))
                device_add_inst(hdd_lpt_bridge_device(rdisk_drives[c].lpt_bridge), rdisk_drives[c].res + 1);

            rdisk_drive_reset(c);

            rdisk_t *dev = (rdisk_t *) rdisk_drives[c].priv;

            rdisk_log(dev->log, "Removable Disk hard_reset drive=%d\n", c);

            dev->id  = c;
            dev->drv = &rdisk_drives[c];

            if (dev->tf == NULL)
                continue;

            rdisk_init(dev);

            if (strlen(rdisk_drives[c].image_path))
                rdisk_load(dev, rdisk_drives[c].image_path, 0);

            rdisk_mode_sense_load(dev);

            /* A cartridge in the drive at power on is no change. */
            dev->ata_mc     = 0;
            dev->ata_locked = 0;
            dev->soft_wp    = 0;

            if (rdisk_drives[c].bus_type == RDISK_BUS_SCSI)
                rdisk_log(dev->log, "SCSI RDISK drive %i attached to SCSI ID %i\n",
                        c, rdisk_drives[c].scsi_device_id);
            else if (rdisk_drives[c].bus_type == RDISK_BUS_ATAPI)
                rdisk_log(dev->log, "ATAPI RDISK drive %i attached to IDE channel %i\n",
                        c, rdisk_drives[c].ide_channel);
            else if (rdisk_drives[c].bus_type == RDISK_BUS_IDE)
                rdisk_log(dev->log, "ATA RDISK drive %i attached to IDE channel %i\n",
                        c, rdisk_drives[c].ide_channel);
            else if (rdisk_drives[c].bus_type == RDISK_BUS_LPT)
                rdisk_log(dev->log, "LPT RDISK drive %i attached to LPT port %i\n",
                        c, rdisk_drives[c].res);

            /*
             * A parallel-port drive is reached through a bridge, and the bridge is not
             * something the user picks: assigning the drive to an LPT port is the whole
             * configuration. Instantiate it here and let it claim the port with
             * lpt_attach() - first claim wins against any other LPT device.
             */
            if ((rdisk_drives[c].bus_type == RDISK_BUS_LPT) && !rdisk_on_ide(&rdisk_drives[c])) {
                const device_t *bridge = &lpt_epat_device;

                if (rdisk_drives[c].type == RDISK_TYPE_ZIP_100)
                    bridge = &lpt_zip_device;
                else if ((rdisk_drives[c].type == RDISK_TYPE_ZIP_100_PLUS) ||
                         (rdisk_drives[c].type == RDISK_TYPE_ZIP_250))
                    bridge = &lpt_zip250_device;
                device_add_inst(bridge, rdisk_drives[c].res + 1);
            }
        }
    }
}

/*
 * Sector I/O and media state of an ATA cartridge drive, for hdd_image.c and
 * the IDE code. slot is the drive number (the hard disk slot minus HDD_NUM).
 */
static rdisk_t *
rdisk_ata_dev(const int slot)
{
    rdisk_t *dev;

    if ((slot < 0) || (slot >= RDISK_NUM) || (rdisk_drives[slot].bus_type != RDISK_BUS_IDE))
        return NULL;

    dev = (rdisk_t *) rdisk_drives[slot].priv;
    return ((dev == NULL) || (dev->drv == NULL)) ? NULL : dev;
}

static int
rdisk_ata_seek(const rdisk_t *dev, const uint32_t sector, const uint32_t count)
{
    if ((dev == NULL) || (dev->drv->fp == NULL) || (count > dev->drv->medium_size) ||
        (sector > (dev->drv->medium_size - count)))
        return -1;

    return fseeko64(dev->drv->fp, ((uint64_t) sector << 9) + dev->drv->base, SEEK_SET);
}

int
rdisk_ata_read(const int slot, const uint32_t sector, const uint32_t count, uint8_t *buffer)
{
    const rdisk_t *dev = rdisk_ata_dev(slot);

    if ((rdisk_ata_seek(dev, sector, count) == -1) ||
        (fread(buffer, 512, count, dev->drv->fp) != count))
        return -1;

    return 0;
}

int
rdisk_ata_write(const int slot, const uint32_t sector, const uint32_t count, const uint8_t *buffer)
{
    const rdisk_t *dev = rdisk_ata_dev(slot);

    if ((dev == NULL) || dev->drv->read_only || dev->soft_wp ||
        (rdisk_ata_seek(dev, sector, count) == -1) || (fwrite(buffer, 512, count, dev->drv->fp) != count))
        return -1;

    fflush(dev->drv->fp);
    return 0;
}

int
rdisk_ata_zero(const int slot, const uint32_t sector, const uint32_t count)
{
    static const uint8_t zero[512] = { 0 };
    const rdisk_t       *dev       = rdisk_ata_dev(slot);

    if ((dev == NULL) || dev->drv->read_only || dev->soft_wp ||
        (rdisk_ata_seek(dev, sector, count) == -1))
        return -1;

    for (uint32_t i = 0; i < count; i++) {
        if (fwrite(zero, 512, 1, dev->drv->fp) != 1)
            return -1;
    }

    fflush(dev->drv->fp);
    return 0;
}

uint32_t
rdisk_ata_last_sector(const int slot)
{
    const rdisk_t *dev = rdisk_ata_dev(slot);

    return ((dev == NULL) || (dev->drv->fp == NULL) || !dev->drv->medium_size) ? 0 :
           (dev->drv->medium_size - 1);
}

int
rdisk_ata_media_present(const int slot)
{
    const rdisk_t *dev = rdisk_ata_dev(slot);

    return (dev != NULL) && (dev->drv->fp != NULL);
}

int
rdisk_ata_write_protected(const int slot)
{
    const rdisk_t *dev = rdisk_ata_dev(slot);

    return (dev != NULL) && (dev->drv->read_only || dev->soft_wp);
}

/*
   SyQuest Write Protect Enable/Disable (C0h): Features 'P' sets the
   cartridge's software write protection, 'E' clears it (SparQ Internal EIDE
   Technical Reference, 1997, p. 6-72). The real drive keeps the setting on
   the cartridge; here it lasts until the cartridge is changed. The EZ135
   aborts the command: it has only the write protect tab.
 */
int
rdisk_ata_soft_protect(const int slot, const uint8_t subcommand)
{
    rdisk_t *dev = rdisk_ata_dev(slot);

    if ((dev == NULL) || (dev->drv->type != RDISK_TYPE_EZ230A) ||
        ((subcommand != 'P') && (subcommand != 'E')))
        return -1;

    dev->soft_wp    = (subcommand == 'P');
    return 0;
}

int
rdisk_ata_take_media_change(const int slot)
{
    rdisk_t  *dev = rdisk_ata_dev(slot);
    const int ret = (dev != NULL) && dev->ata_mc;

    if (dev != NULL)
        dev->ata_mc = 0;

    return ret;
}

void
rdisk_ata_lock(const int slot, const int lock)
{
    rdisk_t *dev = rdisk_ata_dev(slot);

    if (dev != NULL)
        dev->ata_locked = !!lock;
}

int
rdisk_ata_eject(const int slot)
{
    const rdisk_t *dev = rdisk_ata_dev(slot);

    if ((dev == NULL) || dev->ata_locked)
        return -1;

    if (dev->drv->fp != NULL)
        rdisk_eject(dev->id);

    return 0;
}

/*
 * Hand a parallel-port bridge the drive assigned to its port, or NULL if
 * none is. The bridge owns the wire protocol; the drive owns ATAPI.
 */
scsi_device_t *
rdisk_get_lpt_device(const uint8_t port)
{
    if (port >= PARALLEL_MAX)
        return NULL;

    return (lpt_rdisk_devices[port].sc == NULL) ? NULL : &lpt_rdisk_devices[port];
}

void
rdisk_close(void)
{
    for (uint8_t c = 0; c < RDISK_NUM; c++) {
        if (rdisk_drives[c].bus_type == RDISK_BUS_SCSI) {
            const uint8_t scsi_bus = (rdisk_drives[c].scsi_device_id >> 4) & 0x0f;
            const uint8_t scsi_id  = rdisk_drives[c].scsi_device_id & 0x0f;

            memset(&scsi_devices[scsi_bus][scsi_id], 0x00, sizeof(scsi_device_t));
        }

        rdisk_t *dev = (rdisk_t *) rdisk_drives[c].priv;

        if (dev) {
            rdisk_disk_unload(dev);

            if (dev->tf)
                free(dev->tf);

            if (dev->log != NULL) {
                rdisk_log(dev->log, "Log closed\n");

                log_close(dev->log);
                dev->log = NULL;
            }

            free(dev);
            rdisk_drives[c].priv = NULL;
        }
    }
}
