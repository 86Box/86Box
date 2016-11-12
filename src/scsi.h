/* Copyright holders: SA1988
   see COPYING for more details
*/
#ifndef __SCSI_H__
#define __SCSI_H__

#include "scattergather.h"

/* SCSI Commands */
#define GPCMD_TEST_UNIT_READY           0x00
#define GPCMD_REQUEST_SENSE		0x03
#define GPCMD_READ_6			0x08
#define GPCMD_INQUIRY			0x12
#define GPCMD_MODE_SELECT_6		0x15
#define GPCMD_MODE_SENSE_6		0x1a
#define GPCMD_START_STOP_UNIT		0x1b
#define GPCMD_PREVENT_REMOVAL          	0x1e
#define GPCMD_READ_CDROM_CAPACITY	0x25
#define GPCMD_READ_10                   0x28
#define GPCMD_SEEK			0x2b
#define GPCMD_READ_SUBCHANNEL		0x42
#define GPCMD_READ_TOC_PMA_ATIP		0x43
#define GPCMD_READ_HEADER		0x44
#define GPCMD_PLAY_AUDIO_10		0x45
#define GPCMD_GET_CONFIGURATION		0x46
#define GPCMD_PLAY_AUDIO_MSF	        0x47
#define GPCMD_GET_EVENT_STATUS_NOTIFICATION	0x4a
#define GPCMD_PAUSE_RESUME		0x4b
#define GPCMD_STOP_PLAY_SCAN            0x4e
#define GPCMD_READ_DISC_INFORMATION	0x51
#define GPCMD_MODE_SELECT_10		0x55
#define GPCMD_MODE_SENSE_10		0x5a
#define GPCMD_PLAY_AUDIO_12		0xa5
#define GPCMD_READ_12                   0xa8
#define GPCMD_READ_DVD_STRUCTURE	0xad	/* For reading. */
#define GPCMD_SET_SPEED			0xbb
#define GPCMD_MECHANISM_STATUS		0xbd
#define GPCMD_READ_CD			0xbe
#define GPCMD_SEND_DVD_STRUCTURE	0xbf	/* This is for writing only, irrelevant to PCem. */

/* Mode page codes for mode sense/set */
#define GPMODE_R_W_ERROR_PAGE		0x01
#define GPMODE_CDROM_PAGE		0x0d
#define GPMODE_CDROM_AUDIO_PAGE		0x0e
#define GPMODE_CAPABILITIES_PAGE	0x2a
#define GPMODE_ALL_PAGES		0x3f

/* SCSI Status Codes */
#define SCSI_STATUS_OK				0
#define SCSI_STATUS_CHECK_CONDITION 2

/* SCSI Sense Keys */
#define SENSE_NONE			0
#define SENSE_NOT_READY			2
#define SENSE_ILLEGAL_REQUEST		5
#define SENSE_UNIT_ATTENTION		6

/* SCSI Additional Sense Codes */
#define ASC_AUDIO_PLAY_OPERATION	0x00
#define ASC_ILLEGAL_OPCODE		0x20
#define	ASC_INV_FIELD_IN_CMD_PACKET	0x24
#define ASC_MEDIUM_MAY_HAVE_CHANGED	0x28
#define ASC_INCOMPATIBLE_FORMAT              0x30
#define ASC_MEDIUM_NOT_PRESENT		0x3a
#define ASC_DATA_PHASE_ERROR		0x4b
#define ASC_ILLEGAL_MODE_FOR_THIS_TRACK	0x64

#define ASCQ_AUDIO_PLAY_OPERATION_IN_PROGRESS	0x11
#define ASCQ_AUDIO_PLAY_OPERATION_PAUSED		0x12
#define ASCQ_AUDIO_PLAY_OPERATION_COMPLETED		0x13

/* Tell RISC OS that we have a 4x CD-ROM drive (600kb/sec data, 706kb/sec raw).
   Not that it means anything */
#define CDROM_SPEED	706

/* Some generally useful CD-ROM information */
#define CD_MINS                       75 /* max. minutes per CD */
#define CD_SECS                       60 /* seconds per minute */
#define CD_FRAMES                     75 /* frames per second */
#define CD_FRAMESIZE                2048 /* bytes per frame, "cooked" mode */
#define CD_MAX_BYTES       (CD_MINS * CD_SECS * CD_FRAMES * CD_FRAMESIZE)
#define CD_MAX_SECTORS     (CD_MAX_BYTES / 512)	
	
/* Event notification classes for GET EVENT STATUS NOTIFICATION */
#define GESN_NO_EVENTS                0
#define GESN_OPERATIONAL_CHANGE       1
#define GESN_POWER_MANAGEMENT         2
#define GESN_EXTERNAL_REQUEST         3
#define GESN_MEDIA                    4
#define GESN_MULTIPLE_HOSTS           5
#define GESN_DEVICE_BUSY              6

/* Event codes for MEDIA event status notification */
#define MEC_NO_CHANGE                 0
#define MEC_EJECT_REQUESTED           1
#define MEC_NEW_MEDIA                 2
#define MEC_MEDIA_REMOVAL             3 /* only for media changers */
#define MEC_MEDIA_CHANGED             4 /* only for media changers */
#define MEC_BG_FORMAT_COMPLETED       5 /* MRW or DVD+RW b/g format completed */
#define MEC_BG_FORMAT_RESTARTED       6 /* MRW or DVD+RW b/g format restarted */
#define MS_TRAY_OPEN                  1
#define MS_MEDIA_PRESENT              2

/*
 * The MMC values are not IDE specific and might need to be moved
 * to a common header if they are also needed for the SCSI emulation
 */

/* Profile list from MMC-6 revision 1 table 91 */
#define MMC_PROFILE_NONE                0x0000
#define MMC_PROFILE_CD_ROM              0x0008
#define MMC_PROFILE_CD_R                0x0009
#define MMC_PROFILE_CD_RW               0x000A
#define MMC_PROFILE_DVD_ROM             0x0010
#define MMC_PROFILE_DVD_R_SR            0x0011
#define MMC_PROFILE_DVD_RAM             0x0012
#define MMC_PROFILE_DVD_RW_RO           0x0013
#define MMC_PROFILE_DVD_RW_SR           0x0014
#define MMC_PROFILE_DVD_R_DL_SR         0x0015
#define MMC_PROFILE_DVD_R_DL_JR         0x0016
#define MMC_PROFILE_DVD_RW_DL           0x0017
#define MMC_PROFILE_DVD_DDR             0x0018
#define MMC_PROFILE_DVD_PLUS_RW         0x001A
#define MMC_PROFILE_DVD_PLUS_R          0x001B
#define MMC_PROFILE_DVD_PLUS_RW_DL      0x002A
#define MMC_PROFILE_DVD_PLUS_R_DL       0x002B
#define MMC_PROFILE_BD_ROM              0x0040
#define MMC_PROFILE_BD_R_SRM            0x0041
#define MMC_PROFILE_BD_R_RRM            0x0042
#define MMC_PROFILE_BD_RE               0x0043
#define MMC_PROFILE_HDDVD_ROM           0x0050
#define MMC_PROFILE_HDDVD_R             0x0051
#define MMC_PROFILE_HDDVD_RAM           0x0052
#define MMC_PROFILE_HDDVD_RW            0x0053
#define MMC_PROFILE_HDDVD_R_DL          0x0058
#define MMC_PROFILE_HDDVD_RW_DL         0x005A
#define MMC_PROFILE_INVALID             0xFFFF

#define NONDATA			4
#define CHECK_READY		2
#define ALLOW_UA		1

extern uint8_t SCSICommandTable[0x100];

#define IMPLEMENTED		1

extern uint8_t mode_sense_pages[0x40];

extern int readcdmode;

/* Mode sense/select stuff. */
extern uint8_t mode_pages_in[256][256];
#define PAGE_CHANGEABLE		1
#define PAGE_CHANGED		2

extern uint8_t page_flags[256];
extern uint8_t prefix_len;
extern uint8_t page_current; 

uint32_t DataLength;
uint32_t DataPointer;

extern uint8_t ScsiStatus;
extern int ScsiCallback[7];
extern uint8_t scsi_cdrom_id;

struct
{
	uint8_t SenseKey;
	uint8_t Asc;
	uint8_t Ascq;		
} SCSISense;

extern int cd_status;
extern int prev_status;

#define SCSI_NONE 0
#define SCSI_HDD 1 /*not present yet*/
#define SCSI_CDROM 2

typedef struct SCSI
{
	uint8_t Cdb[32];
	uint8_t CdbLength;
	SGBUF SegmentBuffer;
	int SectorLen;
	int SectorLba;
	int BufferPosition;
	SGSEG SegmentData;
	int LunType;
	void *p;
} SCSI;

SCSI ScsiDrives[7];

void SCSIQueryResidual(SCSI *Scsi, uint32_t *Residual);

void SCSISendCommand(SCSI *Scsi, uint8_t Id, uint8_t *Cdb, uint8_t CdbLength, 
									uint32_t BufferLength, uint8_t *SenseBufferPointer, 
									uint8_t SenseBufferLength);

uint32_t (*pfnIoRequestCopyFromBuffer)(uint32_t OffDst, SGBUF *SegmentBuffer,
										uint32_t Copy);
uint32_t (*pfnIoRequestCopyToBuffer)(uint32_t OffSrc, SGBUF *SegmentBuffer,
										uint32_t Copy);
										
void SCSIReadTransfer(SCSI *Scsi, uint8_t Id);
void SCSIWriteTransfer(SCSI *Scsi, uint8_t Id);									
										
extern void SCSIReset(SCSI *Scsi, uint8_t Id);

extern uint32_t SCSICDROMModeSense(uint8_t *buf, uint32_t pos, uint8_t type);
extern uint8_t SCSICDROMSetProfile(uint8_t *buf, uint8_t *index, uint16_t profile);
extern int SCSICDROMReadDVDStructure(int format, const uint8_t *packet, uint8_t *buf);
extern uint32_t SCSICDROMEventStatus(uint8_t *buffer);

extern void SCSICDROM_Insert();

#endif