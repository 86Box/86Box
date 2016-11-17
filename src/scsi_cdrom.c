/* Copyright holders: SA1988
   see COPYING for more details
*/
/*SCSI CD-ROM emulation*/
#include <stdlib.h>
#include <string.h>
#include "86box.h"
#include "ibm.h"

#include "cdrom.h"
#include "scsi.h"

#include "timer.h"

#define SCSI_TIME (5 * 100 * (1 << TIMER_SHIFT))

uint8_t ScsiStatus = SCSI_STATUS_OK;

/* Table of all ATAPI commands and their flags, needed for the new disc change / not ready handler. */
uint8_t SCSICommandTable[0x100] =
{
	[GPCMD_TEST_UNIT_READY]               = CHECK_READY | NONDATA,
	[GPCMD_REQUEST_SENSE]                 = ALLOW_UA,
	[GPCMD_READ_6]                        = CHECK_READY,
	[GPCMD_INQUIRY]                       = ALLOW_UA,
	[GPCMD_MODE_SELECT_6]                 = 0,
	[GPCMD_MODE_SENSE_6]                  = 0,
	[GPCMD_START_STOP_UNIT]               = CHECK_READY,
	[GPCMD_PREVENT_REMOVAL]               = CHECK_READY,
	[GPCMD_READ_CDROM_CAPACITY]           = CHECK_READY,
	[GPCMD_READ_10]                       = CHECK_READY,
	[GPCMD_SEEK]                          = CHECK_READY | NONDATA,
	[GPCMD_READ_SUBCHANNEL]               = CHECK_READY,
	[GPCMD_READ_TOC_PMA_ATIP]             = CHECK_READY | ALLOW_UA,		/* Read TOC - can get through UNIT_ATTENTION, per VIDE-CDD.SYS
										   NOTE: The ATAPI reference says otherwise, but I think this is a question of
											 interpreting things right - the UNIT ATTENTION condition we have here
											 is a tradition from not ready to ready, by definition the drive
											 eventually becomes ready, make the condition go away. */
	[GPCMD_READ_HEADER]                   = CHECK_READY,
	[GPCMD_PLAY_AUDIO_10]                 = CHECK_READY,
	[GPCMD_GET_CONFIGURATION]             = ALLOW_UA,
	[GPCMD_PLAY_AUDIO_MSF]                = CHECK_READY,
	[GPCMD_GET_EVENT_STATUS_NOTIFICATION] = ALLOW_UA,
	[GPCMD_PAUSE_RESUME]                  = CHECK_READY,
	[GPCMD_STOP_PLAY_SCAN]                = CHECK_READY,
	[GPCMD_READ_DISC_INFORMATION]         = CHECK_READY,
	[GPCMD_MODE_SELECT_10]                = 0,
	[GPCMD_MODE_SENSE_10]                 = 0,
	[GPCMD_PLAY_AUDIO_12]                 = CHECK_READY,
	[GPCMD_READ_12]                       = CHECK_READY,
	[GPCMD_READ_DVD_STRUCTURE]            = CHECK_READY,
	[GPCMD_READ_CD_MSF]                   = CHECK_READY,
	[GPCMD_SET_SPEED]                     = 0,
	[GPCMD_PLAY_CD]                       = CHECK_READY,
	[GPCMD_MECHANISM_STATUS]              = 0,
	[GPCMD_READ_CD]                       = CHECK_READY,
	[GPCMD_SEND_DVD_STRUCTURE]			  = CHECK_READY
};

uint8_t mode_sense_pages[0x40] =
{
	[GPMODE_R_W_ERROR_PAGE]    = IMPLEMENTED,
	[GPMODE_CDROM_PAGE]        = IMPLEMENTED,
	[GPMODE_CDROM_AUDIO_PAGE]  = IMPLEMENTED,
	[GPMODE_CAPABILITIES_PAGE] = IMPLEMENTED,
	[GPMODE_ALL_PAGES]         = IMPLEMENTED
};

uint8_t page_flags[256] =
{
	[GPMODE_R_W_ERROR_PAGE]    = 0,
	[GPMODE_CDROM_PAGE]        = 0,
	[GPMODE_CDROM_AUDIO_PAGE]  = PAGE_CHANGEABLE,
	[GPMODE_CAPABILITIES_PAGE] = 0,
};

CDROM *cdrom;

int readcdmode = 0;

uint8_t mode_pages_in[256][256];
uint8_t prefix_len;
uint8_t page_current;

/*SCSI Sense Initialization*/
void SCSISenseCodeOk(void)
{	
	SCSISense.SenseKey=SENSE_NONE;
	SCSISense.Asc=0;
	SCSISense.Ascq=0;
}

void SCSISenseCodeError(uint8_t SenseKey, uint8_t Asc, uint8_t Ascq)
{
	SCSISense.SenseKey=SenseKey;
	SCSISense.Asc=Asc;
	SCSISense.Ascq=Ascq;
}

static void
ScsiPadStr8(uint8_t *buf, int buf_size, const char *src)
{
	int i;

	for (i = 0; i < buf_size; i++) {
		if (*src != '\0') {
			buf[i] = *src++;
		} else {
			buf[i] = ' ';
		}
	}
}

uint32_t SCSIGetCDChannel(int channel)
{
	return (page_flags[GPMODE_CDROM_AUDIO_PAGE] & PAGE_CHANGED) ? mode_pages_in[GPMODE_CDROM_AUDIO_PAGE][channel ? 8 : 6] : (channel + 1);
}

uint32_t SCSIGetCDVolume(int channel)
{
	// return ((page_flags[GPMODE_CDROM_AUDIO_PAGE] & PAGE_CHANGED) && (mode_pages_in[GPMODE_CDROM_AUDIO_PAGE][channel ? 8 : 6] != 0)) ? mode_pages_in[GPMODE_CDROM_AUDIO_PAGE][channel ? 9 : 7] : 0xFF;
	return (page_flags[GPMODE_CDROM_AUDIO_PAGE] & PAGE_CHANGED) ? mode_pages_in[GPMODE_CDROM_AUDIO_PAGE][channel ? 9 : 7] : 0xFF;
}

/*SCSI Mode Sense 6/10*/
uint32_t SCSICDROMModeSense(uint8_t *buf, uint32_t pos, uint8_t type)
{
        if (type==GPMODE_ALL_PAGES || type==GPMODE_R_W_ERROR_PAGE)
        {
        	/* &01 - Read error recovery */
        	buf[pos++] = GPMODE_R_W_ERROR_PAGE;
        	buf[pos++] = 6; /* Page length */
        	buf[pos++] = 0; /* Error recovery parameters */
        	buf[pos++] = 5; /* Read retry count */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        }

        if (type==GPMODE_ALL_PAGES || type==GPMODE_CDROM_PAGE)
        {
        	/* &0D - CD-ROM Parameters */
        	buf[pos++] = GPMODE_CDROM_PAGE;
        	buf[pos++] = 6; /* Page length */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 1; /* Inactivity time multiplier *NEEDED BY RISCOS* value is a guess */
        	buf[pos++] = 0; buf[pos++] = 60; /* MSF settings */
        	buf[pos++] = 0; buf[pos++] = 75; /* MSF settings */
        }

        if (type==GPMODE_ALL_PAGES || type==GPMODE_CDROM_AUDIO_PAGE)
        {
        	/* &0e - CD-ROM Audio Control Parameters */
        	buf[pos++] = GPMODE_CDROM_AUDIO_PAGE;
			buf[pos++] = 0xE; /* Page length */
			if (page_flags[GPMODE_CDROM_AUDIO_PAGE] & PAGE_CHANGED)
			{
				int i;
				for (i = 0; i < 14; i++)
				{
					buf[pos++] = mode_pages_in[GPMODE_CDROM_AUDIO_PAGE][i];
				}
			}
			else
			{
				buf[pos++] = 4; /* Reserved */
				buf[pos++] = 0; /* Reserved */
				buf[pos++] = 0; /* Reserved */
				buf[pos++] = 0; /* Reserved */
				buf[pos++] = 0; buf[pos++] = 75; /* Logical audio block per second */
				buf[pos++] = 1;    /* CDDA Output Port 0 Channel Selection */
				buf[pos++] = 0xFF; /* CDDA Output Port 0 Volume */
				buf[pos++] = 2;    /* CDDA Output Port 1 Channel Selection */
				buf[pos++] = 0xFF; /* CDDA Output Port 1 Volume */
				buf[pos++] = 0;    /* CDDA Output Port 2 Channel Selection */
				buf[pos++] = 0;    /* CDDA Output Port 2 Volume */
				buf[pos++] = 0;    /* CDDA Output Port 3 Channel Selection */
				buf[pos++] = 0;    /* CDDA Output Port 3 Volume */
			}
        }

        if (type==GPMODE_ALL_PAGES || type==GPMODE_CAPABILITIES_PAGE)
        {
//                pclog("Capabilities page\n");
               	/* &2A - CD-ROM capabilities and mechanical status */
        	buf[pos++] = GPMODE_CAPABILITIES_PAGE;
        	buf[pos++] = 0x12; /* Page length */
        	buf[pos++] = 0; buf[pos++] = 0; /* CD-R methods */
        	buf[pos++] = 1; /* Supports audio play, not multisession */
        	buf[pos++] = 0; /* Some other stuff not supported */
        	buf[pos++] = 0; /* Some other stuff not supported (lock state + eject) */
        	buf[pos++] = 0; /* Some other stuff not supported */
        	buf[pos++] = (uint8_t) (CDROM_SPEED >> 8);
        	buf[pos++] = (uint8_t) CDROM_SPEED; /* Maximum speed */
        	buf[pos++] = 0; buf[pos++] = 2; /* Number of audio levels - on and off only */
        	buf[pos++] = 0; buf[pos++] = 0; /* Buffer size - none */
        	buf[pos++] = (uint8_t) (CDROM_SPEED >> 8);
        	buf[pos++] = (uint8_t) CDROM_SPEED; /* Current speed */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Drive digital format */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        }

	return pos;
}

/*SCSI Get Configuration*/
uint8_t SCSICDROMSetProfile(uint8_t *buf, uint8_t *index, uint16_t profile)
{
    uint8_t *buf_profile = buf + 12; /* start of profiles */

    buf_profile += ((*index) * 4); /* start of indexed profile */
    buf_profile[0] = (profile >> 8) & 0xff;
	buf_profile[1] = profile & 0xff;
    buf_profile[2] = ((buf_profile[0] == buf[6]) && (buf_profile[1] == buf[7]));

    /* each profile adds 4 bytes to the response */
    (*index)++;
    buf[11] += 4; /* Additional Length */

    return 4;
}

/*SCSI Read DVD Structure*/
int SCSICDROMReadDVDStructure(int format, const uint8_t *packet, uint8_t *buf)
{
    switch (format) {
        case 0x0: /* Physical format information */
            {
                int layer = packet[6];
                uint64_t total_sectors;
				total_sectors = (uint64_t) cdrom->size();

                if (layer != 0)
                    return -ASC_INV_FIELD_IN_CMD_PACKET;

                total_sectors >>= 2;
                if (total_sectors == 0)
                    return -ASC_MEDIUM_NOT_PRESENT;

                buf[4] = 1;   /* DVD-ROM, part version 1 */
                buf[5] = 0xf; /* 120mm disc, minimum rate unspecified */
                buf[6] = 1;   /* one layer, read-only (per MMC-2 spec) */
                buf[7] = 0;   /* default densities */

                /* FIXME: 0x30000 per spec? */
				buf[8] = buf[9] = buf[10] = buf[11] = 0; /* start sector */
				buf[12] = (total_sectors >> 24) & 0xff; /* end sector */
				buf[13] = (total_sectors >> 16) & 0xff;
				buf[14] = (total_sectors >> 8) & 0xff;
				buf[15] = total_sectors & 0xff;

				buf[16] = (total_sectors >> 24) & 0xff; /* l0 end sector */
				buf[17] = (total_sectors >> 16) & 0xff;
				buf[18] = (total_sectors >> 8) & 0xff;
				buf[19] = total_sectors & 0xff;

                /* Size of buffer, not including 2 byte size field */				
				buf[0] = ((2048+2)>>8)&0xff;
				buf[1] = (2048+2)&0xff;

                /* 2k data + 4 byte header */
                return (2048 + 4);
            }

        case 0x01: /* DVD copyright information */
            buf[4] = 0; /* no copyright data */
            buf[5] = 0; /* no region restrictions */

            /* Size of buffer, not including 2 byte size field */
			buf[0] = ((4+2)>>8)&0xff;
			buf[1] = (4+2)&0xff;			

            /* 4 byte header + 4 byte data */
            return (4 + 4);

        case 0x03: /* BCA information - invalid field for no BCA info */
            return -ASC_INV_FIELD_IN_CMD_PACKET;

        case 0x04: /* DVD disc manufacturing information */
            /* Size of buffer, not including 2 byte size field */
				buf[0] = ((2048+2)>>8)&0xff;
				buf[1] = (2048+2)&0xff;

            /* 2k data + 4 byte header */
            return (2048 + 4);

        case 0xff:
            /*
             * This lists all the command capabilities above.  Add new ones
             * in order and update the length and buffer return values.
             */

            buf[4] = 0x00; /* Physical format */
            buf[5] = 0x40; /* Not writable, is readable */
				buf[6] = ((2048+4)>>8)&0xff;
				buf[7] = (2048+4)&0xff;

            buf[8] = 0x01; /* Copyright info */
            buf[9] = 0x40; /* Not writable, is readable */
				buf[10] = ((4+4)>>8)&0xff;
				buf[11] = (4+4)&0xff;

            buf[12] = 0x03; /* BCA info */
            buf[13] = 0x40; /* Not writable, is readable */
				buf[14] = ((188+4)>>8)&0xff;
				buf[15] = (188+4)&0xff;

            buf[16] = 0x04; /* Manufacturing info */
            buf[17] = 0x40; /* Not writable, is readable */
				buf[18] = ((2048+4)>>8)&0xff;
				buf[19] = (2048+4)&0xff;

            /* Size of buffer, not including 2 byte size field */
				buf[6] = ((16+2)>>8)&0xff;
				buf[7] = (16+2)&0xff;

            /* data written + 4 byte header */
            return (16 + 4);

        default: /* TODO: formats beyond DVD-ROM requires */
            return -ASC_INV_FIELD_IN_CMD_PACKET;
    }
}

/*SCSI Get Event Status Notification*/
uint32_t SCSICDROMEventStatus(uint8_t *buffer)
{
	uint8_t event_code, media_status = 0;

	if (buffer[5])
	{
		media_status = MS_TRAY_OPEN;
		cdrom->stop();
	}
	else
	{
		media_status = MS_MEDIA_PRESENT;
	}
	
	event_code = MEC_NO_CHANGE;
	if (media_status != MS_TRAY_OPEN)
	{
		if (!buffer[4])
		{
			event_code = MEC_NEW_MEDIA;
			cdrom->load();
		}
		else if (buffer[4]==2)
		{
			event_code = MEC_EJECT_REQUESTED;
			cdrom->eject();
		}
	}
	
	buffer[4] = event_code;
	buffer[5] = media_status;
	buffer[6] = 0;
	buffer[7] = 0;
	
	return 8;
}

void SCSICDROM_Insert()
{
	SCSISense.UnitAttention=1;
#if 0
	SCSISense.SenseKey=SENSE_UNIT_ATTENTION;
	SCSISense.Asc=ASC_MEDIUM_MAY_HAVE_CHANGED;
	SCSISense.Ascq=0;
#endif
}

int cd_status = CD_STATUS_EMPTY;
int prev_status;

static uint8_t ScsiPrev;
static int SenseCompleted;

void SCSIClearSense(uint8_t Command, int IgnoreUA)
{
	if ((SCSISense.SenseKey == SENSE_UNIT_ATTENTION) || IgnoreUA)
	{
		ScsiPrev=Command;
		ScsiStatus = SCSI_STATUS_OK;
		SCSISenseCodeOk();
	}
}

void SCSICDROM_RunCommand(SCSI *Scsi, uint8_t Id, uint8_t *Cdb)
{
	uint8_t RCdMode = 0;
	int c;
	int Msf;
	unsigned char Temp;
	uint32_t Size;
	uint8_t PageCode;
	unsigned Idx = 0;
	unsigned SizeIndex;
	unsigned PreambleLen;
	int TocFormat;
    uint8_t Index = 0;
	int Media;
	int Format;
	int DVDRet;
	
	memcpy(Scsi->Cdb, Cdb, 32);

	Msf = Scsi->Cdb[1]&2;

	if (cdrom->medium_changed())
	{
		ScsiStatus = SCSI_STATUS_CHECK_CONDITION;			
		SCSICDROM_Insert();
	}

	if ((Scsi->Cdb[0]!=GPCMD_REQUEST_SENSE) && (Scsi->Cdb[0]!=GPCMD_TEST_UNIT_READY))
	{
		/* GPCMD_TEST_UNIT_READY is NOT supposed to clear sense! */
		SCSIClearSense(Scsi->Cdb[0], 1);
	}
		
	if ((SCSICommandTable[Scsi->Cdb[0]] & CHECK_READY) && !cdrom->ready())
	{
		ScsiStatus = SCSI_STATUS_CHECK_CONDITION;		
		SCSISenseCodeError(SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT, 0);
		return;
	}

	prev_status = cd_status;
	cd_status = cdrom->status();
	if (((prev_status == CD_STATUS_PLAYING) || (prev_status == CD_STATUS_PAUSED)) && ((cd_status != CD_STATUS_PLAYING) && (cd_status != CD_STATUS_PAUSED)))
	{
		SenseCompleted = 1;
	}
	else
	{
		SenseCompleted = 0;
	}

	pclog("SCSI Command 0x%02X, Status %02X, Sense Key %02X, Asc %02X, Ascq %02X\n", Scsi->Cdb[0], ScsiStatus, SCSISense.SenseKey, SCSISense.Asc, SCSISense.Ascq);
	
	switch (Scsi->Cdb[0])
	{
			case GPCMD_TEST_UNIT_READY:
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=50*SCSI_TIME;
			break;

			case GPCMD_REQUEST_SENSE:
			/*Will return 18 bytes of 0*/
			memset(Scsi->SegmentData.Address, 0, 512);

			Scsi->SegmentData.Address[0]=0x80|0x70;

			if ((SCSISense.SenseKey >= 0) || (cd_status < CD_STATUS_PLAYING))
			{
				if (SenseCompleted)
				{
					Scsi->SegmentData.Address[2]=SENSE_ILLEGAL_REQUEST;
					Scsi->SegmentData.Address[12]=ASC_AUDIO_PLAY_OPERATION;
					Scsi->SegmentData.Address[13]=ASCQ_AUDIO_PLAY_OPERATION_COMPLETED;
				}
				else
				{
					Scsi->SegmentData.Address[2]=SCSISense.SenseKey;
					Scsi->SegmentData.Address[12]=SCSISense.Asc;
					Scsi->SegmentData.Address[13]=SCSISense.Ascq;
				}
			}
			else
			{
				Scsi->SegmentData.Address[2]=SENSE_ILLEGAL_REQUEST;
				Scsi->SegmentData.Address[12]=ASC_AUDIO_PLAY_OPERATION;
				Scsi->SegmentData.Address[13]=(cd_status == CD_STATUS_PLAYING) ? ASCQ_AUDIO_PLAY_OPERATION_IN_PROGRESS : ASCQ_AUDIO_PLAY_OPERATION_PAUSED;
			}

			Scsi->SegmentData.Address[7]=10;

			SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, 18);
			SCSIReadTransfer(Scsi, Id);
			
			SCSIClearSense(Scsi->Cdb[0], 0);
			ScsiCallback[Id]=60*SCSI_TIME;
			break;
			
			case GPCMD_SET_SPEED:
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=50*SCSI_TIME;
			break;

			case GPCMD_MECHANISM_STATUS:
			Scsi->SegmentData.Length=(Scsi->Cdb[7]<<16)|(Scsi->Cdb[8]<<8)|Scsi->Cdb[9];

			Scsi->SegmentData.Address[0] = 0;
			Scsi->SegmentData.Address[1] = 0;
			Scsi->SegmentData.Address[2] = 0;
			Scsi->SegmentData.Address[3] = 0;
			Scsi->SegmentData.Address[4] = 0;
			Scsi->SegmentData.Address[5] = 1;
			Scsi->SegmentData.Address[6] = 0;
			Scsi->SegmentData.Address[7] = 0;

			SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, 8);
			SCSIReadTransfer(Scsi, Id);
			
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=60*SCSI_TIME;
			break;
			
			case GPCMD_READ_TOC_PMA_ATIP:
			TocFormat = Scsi->Cdb[2] & 0xf;
			if (TocFormat == 0)
				TocFormat = (Scsi->Cdb[9]>>6) & 3;
			switch (TocFormat)
			{
				case 0: /*Normal*/
				Scsi->SegmentData.Length = Scsi->Cdb[8]|(Scsi->Cdb[7]<<8);
				Scsi->SegmentData.Length = cdrom->readtoc(Scsi->SegmentData.Address, Scsi->Cdb[6], Msf, Scsi->SegmentData.Length, 0);
				break;
				
				case 1: /*Multi session*/
				Scsi->SegmentData.Length = Scsi->Cdb[8]|(Scsi->Cdb[7]<<8);
				Scsi->SegmentData.Length = cdrom->readtoc_session(Scsi->SegmentData.Address, Msf, Scsi->SegmentData.Length);
				Scsi->SegmentData.Address[0] = 0; 
				Scsi->SegmentData.Address[1] = 0xA;
				break;
				
				case 2: /*Raw*/
				Scsi->SegmentData.Length = Scsi->Cdb[8]|(Scsi->Cdb[7]<<8);
				Scsi->SegmentData.Length = cdrom->readtoc_raw(Scsi->SegmentData.Address, Scsi->SegmentData.Length);
				break;
						  
				default:
				ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				ScsiCallback[Id]=50*SCSI_TIME;
				return;
			}
			SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, Scsi->SegmentData.Length);
			SCSIReadTransfer(Scsi, Id);
			
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=60*SCSI_TIME;
			return;

			case GPCMD_READ_CD:
			RCdMode = Scsi->Cdb[9] & 0xF8;
			if ((RCdMode != 0x10) && (RCdMode != 0xF8))
			{
				ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE, 0x00);
				ScsiCallback[Id]=50*SCSI_TIME;
				break;
			}
			
			Scsi->SectorLen=(Scsi->Cdb[6]<<16)|(Scsi->Cdb[7]<<8)|Scsi->Cdb[8];
			Scsi->SectorLba=(Scsi->Cdb[2]<<24)|(Scsi->Cdb[3]<<16)|(Scsi->Cdb[4]<<8)|Scsi->Cdb[5];

			if (RCdMode == 0x10)
				cdrom->readsector(Scsi->SegmentData.Address,Scsi->SectorLba);
			else
				cdrom->readsector_raw(Scsi->SegmentData.Address,Scsi->SectorLba, 0);
				
			readcdmode = (RCdMode == 0xF8);
			
			Scsi->SectorLba++;
			Scsi->SectorLen--;
            
			SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, (Scsi->Cdb[9]==0x10) ? 2048 : 2352);
			SCSIReadTransfer(Scsi, Id);
				
			SCSISenseCodeOk();
			ScsiCallback[Id]=60*SCSI_TIME;			
			return;			
			
			case GPCMD_READ_6:
			case GPCMD_READ_10:
			case GPCMD_READ_12:
			{
				readcdmode = 0;
				
				if (Scsi->Cdb[0] == GPCMD_READ_6)
				{
					Scsi->SectorLen=Scsi->Cdb[4];
					Scsi->SectorLba=((((uint32_t) Scsi->Cdb[1]) & 0x1f)<<16)|(((uint32_t) Scsi->Cdb[2])<<8)|((uint32_t) Scsi->Cdb[3]);
				}
				else if (Scsi->Cdb[0] == GPCMD_READ_10)
				{
					Scsi->SectorLen=(Scsi->Cdb[7]<<8)|Scsi->Cdb[8];
					Scsi->SectorLba=(Scsi->Cdb[2]<<24)|(Scsi->Cdb[3]<<16)|(Scsi->Cdb[4]<<8)|Scsi->Cdb[5];
				}
				else
				{
					Scsi->SectorLen=(((uint32_t) Scsi->Cdb[6])<<24)|(((uint32_t) Scsi->Cdb[7])<<16)|(((uint32_t) Scsi->Cdb[8])<<8)|((uint32_t) Scsi->Cdb[9]);
					Scsi->SectorLba=(((uint32_t) Scsi->Cdb[2])<<24)|(((uint32_t) Scsi->Cdb[3])<<16)|(((uint32_t) Scsi->Cdb[4])<<8)|((uint32_t) Scsi->Cdb[5]);
				}
				
				if (!Scsi->SectorLen)
				{
					SCSISenseCodeOk();
					ScsiCallback[Id]=20*SCSI_TIME;	
					break;
				}
				
				cdrom->readsector(Scsi->SegmentData.Address, Scsi->SectorLba);
				
				Scsi->SectorLba++;
				Scsi->SectorLen--;
				
				SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, 2048);
				SCSIReadTransfer(Scsi, Id);
				
				SCSISenseCodeOk();
				ScsiCallback[Id]=60*SCSI_TIME;
			}
			return;
			
			case GPCMD_READ_HEADER:
			if (Msf)
			{
				ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE, 0x00);
				ScsiCallback[Id]=50*SCSI_TIME;
				break;
			}
			for (c=0;c<4;c++) 
				Scsi->SegmentData.Address[c+4] = Scsi->SegmentData.Address[c+2];
				
			Scsi->SegmentData.Address[0] = 1; /*2048 bytes user data*/
			Scsi->SegmentData.Address[1] = Scsi->SegmentData.Address[2] = Scsi->SegmentData.Address[3] = 0;

			Scsi->SegmentData.Length = 8;
			
			SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, Scsi->SegmentData.Length);
			SCSIReadTransfer(Scsi, Id);
			
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=60*SCSI_TIME;
			return;

			case GPCMD_MODE_SENSE_6:
			case GPCMD_MODE_SENSE_10:
			if (Scsi->Cdb[0] == GPCMD_MODE_SENSE_6)
			{
				Scsi->SegmentData.Length = Scsi->Cdb[4];
			}
			else
			{
				Scsi->SegmentData.Length = (Scsi->Cdb[8]|(Scsi->Cdb[7]<<8));
			}
			Temp=Scsi->Cdb[2] & 0x3F;

			memset(Scsi->SegmentData.Address, 0, Scsi->SegmentData.Length);
			
			if (!(mode_sense_pages[Temp] & IMPLEMENTED))
			{
				ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				ScsiCallback[Id]=60*SCSI_TIME;
				break;
			}
				
			if (Scsi->Cdb[0] == GPCMD_MODE_SENSE_6)
			{
				Scsi->SegmentData.Length = SCSICDROMModeSense(Scsi->SegmentData.Address, 4, Temp);
				Scsi->SegmentData.Address[0] = Scsi->SegmentData.Length - 1;
				Scsi->SegmentData.Address[1] = 3; /*120mm data CD-ROM*/
			}
			else
			{
				Scsi->SegmentData.Length = SCSICDROMModeSense(Scsi->SegmentData.Address, 8, Temp);
				Scsi->SegmentData.Address[0] = (Scsi->SegmentData.Length - 2)>>8;
				Scsi->SegmentData.Address[1] = (Scsi->SegmentData.Length - 2)&255;
				Scsi->SegmentData.Address[2] = 3; /*120mm data CD-ROM*/
			}				

			SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, Scsi->SegmentData.Length);
			SCSIReadTransfer(Scsi, Id);

			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=60*SCSI_TIME;
			return;
			
			case GPCMD_MODE_SELECT_6:
			case GPCMD_MODE_SELECT_10:
			if (Scsi->Cdb[0] == GPCMD_MODE_SELECT_6)
			{
				Scsi->SegmentData.Length = Scsi->Cdb[4];
				prefix_len = 6;
			}
			else
			{
				Scsi->SegmentData.Length = (Scsi->Cdb[7]<<8)|Scsi->Cdb[8];
				prefix_len = 10;
			}
			page_current = Scsi->Cdb[2];
			if (page_flags[page_current] & PAGE_CHANGEABLE)
				page_flags[GPMODE_CDROM_AUDIO_PAGE] |= PAGE_CHANGED;

			SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, Scsi->SegmentData.Length);
			SCSIWriteTransfer(Scsi, Id);
			
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=60*SCSI_TIME;
			return;
			
			case GPCMD_GET_CONFIGURATION:
			{
				/* XXX: could result in alignment problems in some architectures */
				Scsi->SegmentData.Length = (Scsi->Cdb[7]<<8)|Scsi->Cdb[8];

				Index = 0;
	 
				/* only feature 0 is supported */
				if (Scsi->Cdb[2] != 0 || Scsi->Cdb[3] != 0)
				{
					ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
					SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
					ScsiCallback[Id]=50*SCSI_TIME;
					break;
				}
	 
				/*
				 * XXX: avoid overflow for io_buffer if len is bigger than
				 *      the size of that buffer (dimensioned to max number of
				 *      sectors to transfer at once)
				 *
				 *      Only a problem if the feature/profiles grow.
				 */
				if (Scsi->SegmentData.Length > 512) /* XXX: assume 1 sector */
					Scsi->SegmentData.Length = 512;
	 
				memset(Scsi->SegmentData.Address, 0, Scsi->SegmentData.Length);
				/*
				 * the number of sectors from the media tells us which profile
				 * to use as current.  0 means there is no media
				 */
				if (Scsi->SegmentData.Length > CD_MAX_SECTORS )
				{
					Scsi->SegmentData.Address[6] = (MMC_PROFILE_DVD_ROM >> 8) & 0xff;
					Scsi->SegmentData.Address[7] = MMC_PROFILE_DVD_ROM & 0xff;
				}
				else if (Scsi->SegmentData.Length <= CD_MAX_SECTORS)
				{
					Scsi->SegmentData.Address[6] = (MMC_PROFILE_CD_ROM >> 8) & 0xff;
					Scsi->SegmentData.Address[7] = MMC_PROFILE_CD_ROM & 0xff;
				}
				Scsi->SegmentData.Address[10] = 0x02 | 0x01; /* persistent and current */
				Scsi->SegmentData.Length = 12; /* headers: 8 + 4 */
				Scsi->SegmentData.Length += SCSICDROMSetProfile(Scsi->SegmentData.Address, &Index, MMC_PROFILE_DVD_ROM);
				Scsi->SegmentData.Length += SCSICDROMSetProfile(Scsi->SegmentData.Address, &Index, MMC_PROFILE_CD_ROM);
				Scsi->SegmentData.Address[0] = ((Scsi->SegmentData.Length-4) >> 24) & 0xff;
				Scsi->SegmentData.Address[1] = ((Scsi->SegmentData.Length-4) >> 16) & 0xff;
				Scsi->SegmentData.Address[2] = ((Scsi->SegmentData.Length-4) >> 8) & 0xff;
				Scsi->SegmentData.Address[3] = (Scsi->SegmentData.Length-4) & 0xff;           
			
				SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, Scsi->SegmentData.Length);
				SCSIReadTransfer(Scsi, Id);
				
				ScsiStatus = SCSI_STATUS_OK;
				SCSISenseCodeOk();
				ScsiCallback[Id]=60*SCSI_TIME;
			}
			break;

			case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
			{
				struct
				{
					uint8_t opcode;
					uint8_t polled;
					uint8_t reserved2[2];
					uint8_t class;
					uint8_t reserved3[2];
					uint16_t len;
					uint8_t control;
				} *gesn_cdb;
				
				struct
				{
					uint16_t len;
					uint8_t notification_class;
					uint8_t supported_events;
				} *gesn_event_header;
				unsigned int used_len =	Scsi->SegmentData.Length;
				
				gesn_cdb = (void *)Scsi->Cdb;
				gesn_event_header = (void *)Scsi->SegmentData.Address;
				
				/* It is fine by the MMC spec to not support async mode operations */
				if (!(gesn_cdb->polled & 0x01))
				{   /* asynchronous mode */
					/* Only pollign is supported, asynchronous mode is not. */
					ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
					SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
					ScsiCallback[Id]=50*SCSI_TIME;
					return;
				}
				
				/* polling mode operation */
				
				/*
				* These are the supported events.
				*
				* We currently only support requests of the 'media' type.
				* Notification class requests and supported event classes are bitmasks,
				* but they are built from the same values as the "notification class"
				* field.
				*/
				gesn_event_header->supported_events = 1 << GESN_MEDIA;
				
				/*
				* We use |= below to set the class field; other bits in this byte
				* are reserved now but this is useful to do if we have to use the
				* reserved fields later.
				*/
				gesn_event_header->notification_class = 0;
				
				/*
				* Responses to requests are to be based on request priority.  The
				* notification_class_request_type enum above specifies the
				* priority: upper elements are higher prio than lower ones.
				*/
				if (gesn_cdb->class & (1 << GESN_MEDIA))
				{
					gesn_event_header->notification_class |= GESN_MEDIA;
					used_len = SCSICDROMEventStatus(Scsi->SegmentData.Address);
				}
				else
				{
					gesn_event_header->notification_class = 0x80; /* No event available */
					used_len = sizeof(*gesn_event_header);
				}
				gesn_event_header->len = used_len - sizeof(*gesn_event_header);
				
				Scsi->SegmentData.Length = used_len;
				
				SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, Scsi->SegmentData.Length);
				SCSIReadTransfer(Scsi, Id);				
				
				ScsiStatus = SCSI_STATUS_OK;
				SCSISenseCodeOk();
				ScsiCallback[Id]=60*SCSI_TIME;
			}
			break;

			case GPCMD_READ_DISC_INFORMATION:			
			Scsi->SegmentData.Address[1] = 32;
			Scsi->SegmentData.Address[2] = 0xe; /* last session complete, disc finalized */
			Scsi->SegmentData.Address[3] = 1; /* first track on disc */
			Scsi->SegmentData.Address[4] = 1; /* # of sessions */
			Scsi->SegmentData.Address[5] = 1; /* first track of last session */
			Scsi->SegmentData.Address[6] = 1; /* last track of last session */
			Scsi->SegmentData.Address[7] = 0x20; /* unrestricted use */
			Scsi->SegmentData.Address[8] = 0x00; /* CD-ROM */
			Scsi->SegmentData.Length = 34;
			
			SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, Scsi->SegmentData.Length);
			SCSIReadTransfer(Scsi, Id);	
			
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=60*SCSI_TIME;
			break;

			case GPCMD_PLAY_AUDIO_10:
			case GPCMD_PLAY_AUDIO_12:
			case GPCMD_PLAY_AUDIO_MSF:
			/*This is apparently deprecated in the SCSI spec, and apparently
					  has been since 1995 (!). Hence I'm having to guess most of it*/
			if (Scsi->Cdb[0] == GPCMD_PLAY_AUDIO_10)
			{
				Scsi->BufferPosition = (Scsi->Cdb[2]<<24)|(Scsi->Cdb[3]<<16)|(Scsi->Cdb[4]<<8)|Scsi->Cdb[5];
				Scsi->SegmentData.Length = (Scsi->Cdb[7]<<8)|Scsi->Cdb[8];
			}
			else if (Scsi->Cdb[0] == GPCMD_PLAY_AUDIO_MSF)
			{
				Scsi->BufferPosition = (Scsi->Cdb[3]<<16)|(Scsi->Cdb[4]<<8)|Scsi->Cdb[5];
				Scsi->SegmentData.Length = (Scsi->Cdb[6]<<16)|(Scsi->Cdb[7]<<8)|Scsi->Cdb[8];
			}
			else
			{
				Scsi->BufferPosition = (Scsi->Cdb[3]<<16)|(Scsi->Cdb[4]<<8)|Scsi->Cdb[5];
				Scsi->SegmentData.Length = (Scsi->Cdb[7]<<16)|(Scsi->Cdb[8]<<8)|Scsi->Cdb[9];
			}


			if ((cdrom_drive < 1) || (cdrom_drive == 200) || (cd_status <= CD_STATUS_DATA_ONLY) ||
						!cdrom->is_track_audio(Scsi->BufferPosition, (Scsi->Cdb[0] == GPCMD_PLAY_AUDIO_MSF) ? 1 : 0))
			{
				ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_MODE_FOR_THIS_TRACK, 0x00);
				ScsiCallback[Id]=50*SCSI_TIME;
				break;
			}
					
			cdrom->playaudio(Scsi->BufferPosition, Scsi->SegmentData.Length, (Scsi->Cdb[0] == GPCMD_PLAY_AUDIO_MSF) ? 1 : 0);
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=50*SCSI_TIME;
			break;

			case GPCMD_READ_SUBCHANNEL:
			Temp = Scsi->Cdb[2] & 0x40;
			if (Scsi->Cdb[3] != 1)
			{
				ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE, 0x00);
				ScsiCallback[Id]=50*SCSI_TIME;
				break;
			}
			Scsi->BufferPosition = 0;
			Scsi->SegmentData.Address[Scsi->BufferPosition++] = 0;
			Scsi->SegmentData.Address[Scsi->BufferPosition++] = 0; /*Audio status*/
			Scsi->SegmentData.Address[Scsi->BufferPosition++] = 0; Scsi->SegmentData.Address[Scsi->BufferPosition++] = 0; /*Subchannel length*/
			Scsi->SegmentData.Address[Scsi->BufferPosition++] = 1; /*Format code*/
			Scsi->SegmentData.Address[1] = cdrom->getcurrentsubchannel(&Scsi->SegmentData.Address[5], Msf);
			Scsi->SegmentData.Length = 16;
			if (!Temp) 
				Scsi->SegmentData.Length = 4;

			SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, Scsi->SegmentData.Length);
			SCSIReadTransfer(Scsi, Id);
			
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=1000*SCSI_TIME;
			break;

			case GPCMD_READ_DVD_STRUCTURE:
			Media = Scsi->Cdb[1];
			Format = Scsi->Cdb[7];
	 
			Scsi->SegmentData.Length = (Scsi->Cdb[6]<<24)|(Scsi->Cdb[7]<<16)|(Scsi->Cdb[8]<<8)|Scsi->Cdb[9];

			if (Format < 0xff) {
				if (Scsi->SegmentData.Length <= CD_MAX_SECTORS) 
				{
					ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
					SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INCOMPATIBLE_FORMAT, 0x00);
					ScsiCallback[Id]=50*SCSI_TIME;
					break;
				} 
				else 
				{
					ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
					SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
					ScsiCallback[Id]=50*SCSI_TIME;
					return;
				}
			}
	 
			memset(Scsi->SegmentData.Address, 0, Scsi->SegmentData.Length > 256 * 512 + 4 ? 256 * 512 + 4 : Scsi->SegmentData.Length);
				
			switch (Format) 
			{
				case 0x00 ... 0x7f:
				case 0xff:
				if (Media == 0) 
				{
					DVDRet = SCSICDROMReadDVDStructure(Format, Scsi->Cdb, Scsi->SegmentData.Address);
	 
					if (DVDRet < 0)
					{
						ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
						SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, -DVDRet, 0x00);
						ScsiCallback[Id]=50*SCSI_TIME;
						return;
					}
					else
					{	
						SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, Scsi->SegmentData.Length);
						SCSIReadTransfer(Scsi, Id);	
						
						ScsiStatus = SCSI_STATUS_OK;
						SCSISenseCodeOk();
						ScsiCallback[Id]=60*SCSI_TIME;
					}
					break;
				}
				/* TODO: BD support, fall through for now */
	 
				/* Generic disk structures */
				case 0x80: /* TODO: AACS volume identifier */
				case 0x81: /* TODO: AACS media serial number */
				case 0x82: /* TODO: AACS media identifier */
				case 0x83: /* TODO: AACS media key block */
				case 0x90: /* TODO: List of recognized format layers */
				case 0xc0: /* TODO: Write protection status */
				default:
				ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				ScsiCallback[Id]=50*SCSI_TIME;
				break;
			}
			break;

			case GPCMD_START_STOP_UNIT:
			if (Scsi->Cdb[4]!=2 && Scsi->Cdb[4]!=3 && Scsi->Cdb[4])
			{
				ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE, 0x00);
				ScsiCallback[Id]=50*SCSI_TIME;
				break;
			}
			if (!Scsi->Cdb[4])        cdrom->stop();
			else if (Scsi->Cdb[4]==2) cdrom->eject();
			else                	  cdrom->load();
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=50*SCSI_TIME;
			break;

			case GPCMD_INQUIRY:
			PageCode = Scsi->Cdb[2];
			Scsi->SegmentData.Length = Scsi->Cdb[4];
			
			pclog("SCSI Inquiry Page %02X\n", Scsi->Cdb[1] & 1);
			if (Scsi->Cdb[1] & 1)
			{
				PreambleLen = 4;
				SizeIndex = 3;
						
				Scsi->SegmentData.Address[Idx++] = 5;
				Scsi->SegmentData.Address[Idx++] = PageCode;
				Scsi->SegmentData.Address[Idx++] = 0;
						
				Idx++;
				
				pclog("SCSI Inquiry Page Code %02X\n", PageCode);
				switch (PageCode)
				{
					case 0x00:
					Scsi->SegmentData.Address[Idx++] = 0x00;
					Scsi->SegmentData.Address[Idx++] = 0x00;
					Scsi->SegmentData.Address[Idx++] = 2;
					Scsi->SegmentData.Address[Idx++] = 0x00;
					Scsi->SegmentData.Address[Idx++] = 0x80;
					Scsi->SegmentData.Address[Idx++] = 0x83;
					break;
					
					case 0x83:
					if (Idx + 24 > Scsi->SegmentData.Length)
					{
						ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
						SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_DATA_PHASE_ERROR, 0);
						ScsiCallback[Id]=500*SCSI_TIME;
						return;
					}
					Scsi->SegmentData.Address[Idx++] = 0x02;
					Scsi->SegmentData.Address[Idx++] = 0x00;
					Scsi->SegmentData.Address[Idx++] = 0x00;
					Scsi->SegmentData.Address[Idx++] = 20;
					ScsiPadStr8(Scsi->SegmentData.Address + Idx, 20, "3097165");	/* Serial */
					Idx += 20;

					if (Idx + 72 > Scsi->SegmentData.Length)
					{
						goto SCSIOut;
					}
					Scsi->SegmentData.Address[Idx++] = 0x02;
					Scsi->SegmentData.Address[Idx++] = 0x01;
					Scsi->SegmentData.Address[Idx++] = 0x00;
					Scsi->SegmentData.Address[Idx++] = 68;
					ScsiPadStr8(Scsi->SegmentData.Address + Idx, 8, "Sony"); /* Vendor */
					Idx += 8;
					ScsiPadStr8(Scsi->SegmentData.Address + Idx, 40, "CDU-76S 1.0"); /* Product */
					Idx += 40;
					ScsiPadStr8(Scsi->SegmentData.Address + Idx, 20, "3097165"); /* Product */
					Idx += 20;
					break;
					
					default:
					ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
					SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
					ScsiCallback[Id]=500*SCSI_TIME;
					return;
				}
			}
			else
			{
				PreambleLen = 5;
				SizeIndex = 4;

				Scsi->SegmentData.Address[0] = 5; /*CD-ROM*/
				Scsi->SegmentData.Address[1] = 0x80; /*Removable*/
				Scsi->SegmentData.Address[2] = 0;
				Scsi->SegmentData.Address[3] = 0x21;
				Scsi->SegmentData.Address[4] = 31;
				Scsi->SegmentData.Address[5] = 0;
				Scsi->SegmentData.Address[6] = 0;
				Scsi->SegmentData.Address[7] = 0;
				ScsiPadStr8(Scsi->SegmentData.Address + 8, 8, "Sony"); /* Vendor */
				ScsiPadStr8(Scsi->SegmentData.Address + 16, 16, "CDU-76S"); /* Product */
				ScsiPadStr8(Scsi->SegmentData.Address + 32, 4, "1.0"); /* Revision */
					
				Idx = 36;
			}

SCSIOut:
			Scsi->SegmentData.Address[SizeIndex] = Idx - PreambleLen;
			Scsi->SegmentData.Length=Idx;	
				
			SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, Scsi->SegmentData.Length);
			SCSIReadTransfer(Scsi, Id);			
			
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=60*SCSI_TIME;
			break;
			
			case GPCMD_PREVENT_REMOVAL:
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=50*SCSI_TIME;
			break;

			case GPCMD_PAUSE_RESUME:
			if (Scsi->Cdb[8]&1) 	cdrom->resume();
			else            		cdrom->pause();
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=50*SCSI_TIME;
			break;

			case GPCMD_SEEK:
			Scsi->BufferPosition = (Scsi->Cdb[3]<<16)|(Scsi->Cdb[4]<<8)|Scsi->Cdb[5];
			cdrom->seek(Scsi->BufferPosition);
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=50*SCSI_TIME;
			break;
			
			case GPCMD_READ_CDROM_CAPACITY:
			Size = cdrom->size();
			Scsi->SegmentData.Address[0] = (Size >> 24) & 0xFF;
			Scsi->SegmentData.Address[1] = (Size >> 16) & 0xFF;
			Scsi->SegmentData.Address[2] = (Size >> 8) & 0xFF;
			Scsi->SegmentData.Address[3] = Size & 0xFF;
			Scsi->SegmentData.Address[4] = (2048 >> 24) & 0xFF;
			Scsi->SegmentData.Address[5] = (2048 >> 16) & 0xFF;
			Scsi->SegmentData.Address[6] = (2048 >> 8) & 0xFF;
			Scsi->SegmentData.Address[7] = 2048 & 0xFF;
			Scsi->SegmentData.Length = 8;
			
			SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, Scsi->SegmentData.Length);

			SCSIReadTransfer(Scsi, Id);			
			
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=60*SCSI_TIME;
			break;

			case GPCMD_STOP_PLAY_SCAN:
			cdrom->stop();
			ScsiStatus = SCSI_STATUS_OK;
			SCSISenseCodeOk();
			ScsiCallback[Id]=50*SCSI_TIME;
			break;

			default:
			ScsiStatus = SCSI_STATUS_CHECK_CONDITION;
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
			ScsiCallback[Id]=50*SCSI_TIME;
			return;
	}
}

void SCSICDROM_CallRead(SCSI *Scsi, uint8_t Id)
{
	if (Scsi->SectorLen <= 0)
	{
		pclog("All done - callback set\n");
		ScsiStatus = SCSI_STATUS_OK;
		SCSISenseCodeOk();
		ScsiCallback[Id]=20*SCSI_TIME;
		return;
	}
	pclog("Continue Read command! %i blocks left\n", Scsi->SectorLen);

	if (readcdmode)
		cdrom->readsector_raw(Scsi->SegmentData.Address, Scsi->SectorLba, 0);
	else
		cdrom->readsector(Scsi->SegmentData.Address, Scsi->SectorLba);

	Scsi->SectorLba++;
    Scsi->SectorLen--;

	SegmentBufferCopyFromBuf(&Scsi->SegmentBuffer, Scsi->SegmentData.Address, readcdmode ? 2352 : 2048);
	
	ScsiStatus = SCSI_STATUS_OK;
	SCSISenseCodeOk();
	ScsiCallback[Id]=60*SCSI_TIME;	
}