/* Copyright holders: SA1988, Tenshi
   see COPYING for more details
*/
/*SCSI CD-ROM emulation*/
#include <stdlib.h>
#include <string.h>
#include "86box.h"
#include "ibm.h"

#include "cdrom.h"
#include "scsi.h"

sector_buffer_t cdrom_sector_buffer;

int cdrom_sector_type, cdrom_sector_flags;
int cdrom_sector_size, cdrom_sector_ismsf;

uint8_t SCSIPhase = SCSI_PHASE_BUS_FREE;
uint8_t SCSIStatus = SCSI_STATUS_OK;

/* Table of all SCSI commands and their flags, needed for the new disc change / not ready handler. */
uint8_t SCSICommandTable[0x100] =
{
	[GPCMD_TEST_UNIT_READY]               = CHECK_READY | NONDATA,
	[GPCMD_REQUEST_SENSE]                 = ALLOW_UA,
	[GPCMD_READ_6]                        = CHECK_READY,
	[GPCMD_SEEK_6]                        = CHECK_READY | NONDATA,
	[GPCMD_INQUIRY]                       = ALLOW_UA,
	[GPCMD_MODE_SELECT_6]                 = 0,
	[GPCMD_MODE_SENSE_6]                  = 0,
	[GPCMD_START_STOP_UNIT]               = CHECK_READY,
	[GPCMD_PREVENT_REMOVAL]               = CHECK_READY,
	[GPCMD_READ_CDROM_CAPACITY]           = CHECK_READY,
	[GPCMD_READ_10]                       = CHECK_READY,
	[GPCMD_SEEK_10]                       = CHECK_READY | NONDATA,
	[GPCMD_READ_SUBCHANNEL]               = CHECK_READY,
	[GPCMD_READ_TOC_PMA_ATIP]             = CHECK_READY,			/* Read TOC - can get through UNIT_ATTENTION, per VIDE-CDD.SYS
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
	[GPCMD_READ_TRACK_INFORMATION]        = CHECK_READY,
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
	[GPCMD_SEND_DVD_STRUCTURE]	      	  = CHECK_READY
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
	SCSIPhase = SCSI_PHASE_STATUS;		/* This *HAS* to be done, SCSIPhase is the same thing that ATAPI returns in IDE sector count, so after any error,
						   status phase (3) is correct. */
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
}

int cd_status = CD_STATUS_EMPTY;
int prev_status;

static uint8_t ScsiPrev;
static int SenseCompleted;

int cdrom_add_error_and_subchannel(uint8_t *b, int real_sector_type)
{
	if ((cdrom_sector_flags & 0x06) == 0x02)
	{
		/* Add error flags. */
		memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.c2, 294);
		cdrom_sector_size += 294;
	}
	else if ((cdrom_sector_flags & 0x06) == 0x04)
	{
		/* Add error flags. */
		memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.c2, 296);
		cdrom_sector_size += 296;
	}
	else if ((cdrom_sector_flags & 0x06) == 0x06)
	{
		// pclog("Invalid error flags\n");
		return 0;
	}
	
	/* if (real_sector_type == 1)
	{ */
		if ((cdrom_sector_flags & 0x700) == 0x100)
		{
			memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.subchannel_raw, 96);
			cdrom_sector_size += 96;
		}
		else if ((cdrom_sector_flags & 0x700) == 0x200)
		{
			memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.subchannel_q, 16);
			cdrom_sector_size += 16;
		}
		else if ((cdrom_sector_flags & 0x700) == 0x400)
		{
			memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.subchannel_rw, 96);
			cdrom_sector_size += 96;
		}
		else if (((cdrom_sector_flags & 0x700) == 0x300) || ((cdrom_sector_flags & 0x700) > 0x400))
		{
			// pclog("Invalid subchannel data flags\n");
			return 0;
		}
	// }
	
	// pclog("CD-ROM sector size after processing: %i (%i, %i) [%04X]\n", cdrom_sector_size, cdrom_sector_type, real_sector_type, cdrom_sector_flags);

	return cdrom_sector_size;
}

int cdrom_LBAtoMSF_accurate()
{
	int temp_pos;
	int m, s, f;
	
	temp_pos = SectorLBA + 150;
	f = temp_pos % 75;
	temp_pos -= f;
	temp_pos /= 75;
	s = temp_pos % 60;
	temp_pos -= s;
	temp_pos /= 60;
	m = temp_pos;
	
	return ((m << 16) | (s << 8) | f);
}

int cdrom_read_data(uint8_t *buffer)
{
	int real_sector_type;
	uint8_t *b;
	uint8_t *temp_b;
	int is_audio;
	int real_pos;
	
	b = temp_b = buffer;
	
	if  (cdrom_sector_ismsf)
	{
		real_pos = cdrom_LBAtoMSF_accurate();
	}
	else
	{
		real_pos = SectorLBA;
	}

	memset(cdrom_sector_buffer.buffer, 0, 2856);

	// is_audio = cdrom->is_track_audio(real_pos, cdrom_sector_ismsf);
	real_sector_type = cdrom->sector_data_type(real_pos, cdrom_sector_ismsf);
	// pclog("Sector type: %i\n", real_sector_type);
	
	if ((cdrom_sector_type > 0) && (cdrom_sector_type < 6))
	{
		if (real_sector_type != cdrom_sector_type)
		{
			return 0;
		}
	}
	else if (cdrom_sector_type == 6)
	{
		/* READ (6), READ (10), READ (12) */
		if ((real_sector_type != 2) && (real_sector_type != 4))
		{
			return 0;
		}
	}

	if (!(cdrom_sector_flags & 0xf0))		/* 0x00 and 0x08 are illegal modes */
	{
		return 0;
	}

	cdrom->readsector_raw(cdrom_sector_buffer.buffer, real_pos, cdrom_sector_ismsf);
	
	if (real_sector_type == 1)
	{
		memcpy(b, cdrom_sector_buffer.buffer, 2352);
		cdrom_sector_size = 2352;
	}
	else
	{
		if ((cdrom_sector_flags & 0x18) == 0x08)		/* EDC/ECC without user data is an illegal mode */
		{
			return 0;
		}

		cdrom_sector_size = 0;

		if (cdrom_sector_flags & 0x80)		/* Sync */
		{
			memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.sync, 12);
			cdrom_sector_size += 12;
			temp_b += 12;
		}
		if (cdrom_sector_flags & 0x20)		/* Header */
		{
			memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.header, 4);
			cdrom_sector_size += 4;
			temp_b += 4;
		}
		
		if (real_sector_type == 2)
		{
			/* Mode 1 sector, expected type is 1 type. */
			if (cdrom_sector_flags & 0x40)		/* Sub-header */
			{
				if (!(cdrom_sector_flags & 0x10))		/* No user data */
				{
					memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m1_data.user_data, 8);
					cdrom_sector_size += 8;
					temp_b += 8;
				}
			}
			if (cdrom_sector_flags & 0x10)		/* User data */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m1_data.user_data, 2048);
				cdrom_sector_size += 2048;
				temp_b += 2048;
			}
			if (cdrom_sector_flags & 0x08)		/* EDC/ECC */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m1_data.ecc, 288);
				cdrom_sector_size += 288;
				temp_b += 288;
			}
		}
		else if (real_sector_type == 3)
		{
			/* Mode 2 sector, non-XA mode. */
			if (cdrom_sector_flags & 0x40)		/* Sub-header */
			{
				if (!(cdrom_sector_flags & 0x10))		/* No user data */
				{
					memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.sub_header, 8);
					cdrom_sector_size += 8;
					temp_b += 8;
				}
			}
			if (cdrom_sector_flags & 0x10)		/* User data */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.sub_header, 2328);
				cdrom_sector_size += 8;
				temp_b += 8;
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.user_data, 2328);
				cdrom_sector_size += 2328;
				temp_b += 2328;
			}
		}
		else if (real_sector_type == 4)
		{
			/* Mode 2 sector, XA Form 1 mode */
			if ((cdrom_sector_flags & 0xf0) == 0x30)
			{
				return 0;
			}
			if (((cdrom_sector_flags & 0xf8) >= 0xa8) && ((cdrom_sector_flags & 0xf8) <= 0xd8))
			{
				return 0;
			}
			if (cdrom_sector_flags & 0x40)		/* Sub-header */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.sub_header, 8);
				cdrom_sector_size += 8;
				temp_b += 8;
			}
			if (cdrom_sector_flags & 0x10)		/* User data */
			{
				if ((cdrom_sector_flags & 0xf0) == 0x10)
				{
					/* The data is alone, include sub-header. */
					memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.sub_header, 8);
					cdrom_sector_size += 8;
					temp_b += 8;
				}
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.user_data, 2040);
				cdrom_sector_size += 2040;
				temp_b += 2040;
			}
			if (cdrom_sector_flags & 0x08)		/* EDC/ECC */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.user_data + 2040, 288);
				cdrom_sector_size += 288;
				temp_b += 288;
			}
		}
		else if (real_sector_type == 5)
		{
			/* Mode 2 sector, XA Form 2 mode */
			if ((cdrom_sector_flags & 0xf0) == 0x30)
			{
				return 0;
			}
			if (((cdrom_sector_flags & 0xf8) >= 0xa8) || ((cdrom_sector_flags & 0xf8) <= 0xd8))
			{
				return 0;
			}
			/* Mode 2 sector, XA Form 1 mode */
			if (cdrom_sector_flags & 0x40)		/* Sub-header */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.sub_header, 8);
				cdrom_sector_size += 8;
				temp_b += 8;
			}
			if (cdrom_sector_flags & 0x10)		/* User data */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.user_data, 2328);
				cdrom_sector_size += 2328;
				temp_b += 2328;
			}
		}
		else
		{
			return 0;
		}
	}

	pclog("CD-ROM sector size: %i (%i, %i) [%04X]\n", cdrom_sector_size, cdrom_sector_type, real_sector_type, cdrom_sector_flags);
	return cdrom_add_error_and_subchannel(b, real_sector_type);
}

void SCSIClearSense(uint8_t Command, uint8_t IgnoreUA)
{
	if ((SCSISense.SenseKey == SENSE_UNIT_ATTENTION) || IgnoreUA)
	{
		pclog("Sense cleared\n");	
		ScsiPrev=Command;
		SCSISenseCodeOk();
	}
}

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

static int SCSICDROM_TOC(uint8_t id, uint8_t *cdb)
{
	int TocFormat;
	
	TocFormat = cdb[2] & 0xf;
	if (TocFormat == 0)
		TocFormat = (cdb[9]>>6) & 3;

	return TocFormat;
}

void SCSICDROM_Command(uint8_t id, uint8_t *cmdbuffer, uint8_t *cdb)
{	
	int len;
	int pos=0;
	int DVDRet;
	uint8_t Index = 0;
	int real_pos;	
	int msf;
	uint32_t Size;
	unsigned Idx = 0;
	unsigned SizeIndex;
	unsigned PreambleLen;
	unsigned char Temp;
	int max_length = 0;
	int track = 0;
	int ret = 0;

	msf = cdb[1] & 2;
	
	//The not ready/unit attention stuff below is only for the Adaptec!
	//The Buslogic one is located in buslogic.c.	
	
	if (!scsi_model)
	{
		if (cdrom->medium_changed())
		{
			pclog("Media changed\n");
			SCSICDROM_Insert();
		}
		
		if (!cdrom->ready() && SCSISense.UnitAttention)
		{
			/* If the drive is not ready, there is no reason to keep the
			   UNIT ATTENTION condition present, as we only use it to mark
			   disc changes. */
			SCSISense.UnitAttention = 0;
		}

		/* If the UNIT ATTENTION condition is set and the command does not allow
		   execution under it, error out and report the condition. */
		if (SCSISense.UnitAttention == 1)
		{
			SCSISense.UnitAttention = 2;
			if (!(SCSICommandTable[cdb[0]] & ALLOW_UA))
			{
				SCSISenseCodeError(SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0);
				SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
				SCSICallback[id]=50*SCSI_TIME;
				return;
			}
		}
		else if (SCSISense.UnitAttention == 2)
		{
			if (cdb[0]!=GPCMD_REQUEST_SENSE)
			{
				SCSISense.UnitAttention = 0;
			}
		}

		/* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
		   clear the UNIT ATTENTION condition if it's set. */
		if (cdb[0]!=GPCMD_REQUEST_SENSE)
		{
			SCSIClearSense(cdb[0], 0);
		}

		/* Next it's time for NOT READY. */
		if ((SCSICommandTable[cdb[0]] & CHECK_READY) && !cdrom->ready())
		{
			pclog("Not ready\n");
			SCSISenseCodeError(SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT, 0);
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
			SCSICallback[id]=50*SCSI_TIME;
			return;
		}
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
	
	switch (cdb[0])
	{
		case GPCMD_TEST_UNIT_READY:
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		break;

		case GPCMD_REQUEST_SENSE:
		len = cdb[4];
		
		/*Will return 18 bytes of 0*/
		memset(cmdbuffer,0,512);

		cmdbuffer[0]=0x80|0x70;

		if ((SCSISense.SenseKey > 0) || (cd_status < CD_STATUS_PLAYING))
		{
			if (SenseCompleted)
			{
				cmdbuffer[2]=SENSE_ILLEGAL_REQUEST;
				cmdbuffer[12]=ASC_AUDIO_PLAY_OPERATION;
				cmdbuffer[13]=ASCQ_AUDIO_PLAY_OPERATION_COMPLETED;
			}
			else
			{
				cmdbuffer[2]=SCSISense.SenseKey;
				cmdbuffer[12]=SCSISense.Asc;
				cmdbuffer[13]=SCSISense.Ascq;
			}
		}
		else if ((SCSISense.SenseKey == 0) && (cd_status >= CD_STATUS_PLAYING) && (cd_status != CD_STATUS_STOPPED))
		{
			cmdbuffer[2]=SENSE_ILLEGAL_REQUEST;
			cmdbuffer[12]=ASC_AUDIO_PLAY_OPERATION;
			cmdbuffer[13]=(cd_status == CD_STATUS_PLAYING) ? ASCQ_AUDIO_PLAY_OPERATION_IN_PROGRESS : ASCQ_AUDIO_PLAY_OPERATION_PAUSED;
		}
		else
		{
			if (SCSISense.UnitAttention)
			{
				cmdbuffer[2]=SENSE_UNIT_ATTENTION;
				cmdbuffer[12]=ASC_MEDIUM_MAY_HAVE_CHANGED;
				cmdbuffer[13]=0;
			}
		}

		cmdbuffer[7]=10;
		
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = len;
		
		SCSIDMAResetPosition(id);		
		
		if (cmdbuffer[2] == SENSE_UNIT_ATTENTION)
		{
			/* If the last remaining sense is unit attention, clear
				that condition. */
			SCSISense.UnitAttention = 0;
		}

		/* Clear the sense stuff as per the spec. */
		ScsiPrev=cdb[0];
		if (cdrom->ready())
			SCSISenseCodeOk();
		return;
		
		case GPCMD_READ_6:
		case GPCMD_READ_10:
		case GPCMD_READ_12:
		cdrom_sector_ismsf = 0;
		
		if (cdb[0] == GPCMD_READ_6)
		{
			SectorLen=cdb[4];
			SectorLBA=((((uint32_t) cdb[1]) & 0x1f)<<16)|(((uint32_t) cdb[2])<<8)|((uint32_t) cdb[3]);
		}
		else if (cdb[0] == GPCMD_READ_10)
		{
			SectorLen=(cdb[7]<<8)|cdb[8];
			SectorLBA=(cdb[2]<<24)|(cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
		}
		else
		{
			SectorLen=(((uint32_t) cdb[6])<<24)|(((uint32_t) cdb[7])<<16)|(((uint32_t) cdb[8])<<8)|((uint32_t) cdb[9]);
			SectorLBA=(((uint32_t) cdb[2])<<24)|(((uint32_t) cdb[3])<<16)|(((uint32_t) cdb[4])<<8)|((uint32_t) cdb[5]);
		}
		
		if (SectorLBA > (cdrom->size() - 1))
		{
			pclog("Trying to read beyond the end of disc\n");
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0);
			SCSICallback[id]=50*SCSI_TIME;
			break;
		}
		
		if (!SectorLen)
		{
			SCSIPhase = SCSI_PHASE_STATUS;
			SCSIStatus = SCSI_STATUS_OK;
			SCSICallback[id]=20*SCSI_TIME;
			break;
		}

		cdrom_sector_type = 6;
		cdrom_sector_flags = 0x10;

		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = SectorLen * 2048;
		
		SCSIDMAResetPosition(id);
		return;
	
		case GPCMD_SEEK_6:
		case GPCMD_SEEK_10:
		if (cdb[0] == GPCMD_SEEK_6)
		{
			pos = ((((uint32_t) cdb[1]) & 0x1f)<<16)|(((uint32_t) cdb[2])<<8)|((uint32_t) cdb[3]);
		}
		else
		{
			pos = (cdb[2]<<24)|(cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
		}
		cdrom->seek(pos);
		
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		break;
	
		case GPCMD_INQUIRY:
		max_length = cdb[4];
		len = max_length;
		
		if (cdb[1] & 1)
		{
			PreambleLen = 4;
			SizeIndex = 3;
						
			cmdbuffer[Idx++] = 05;
			cmdbuffer[Idx++] = cdb[2];
			cmdbuffer[Idx++] = 0;
						
			Idx++;

			switch (cdb[2])
			{
				case 0x00:
				cmdbuffer[Idx++] = 0x00;
				cmdbuffer[Idx++] = 0x83;
				break;

				case 0x83:
				if (Idx + 24 > max_length)
				{
					SCSIStatus = SCSI_STATUS_CHECK_CONDITION;					
					SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_DATA_PHASE_ERROR, 0x00);
					SCSICallback[id]=50*SCSI_TIME;
					return;					
				}
				cmdbuffer[Idx++] = 0x02;
				cmdbuffer[Idx++] = 0x00;
				cmdbuffer[Idx++] = 0x00;
				cmdbuffer[Idx++] = 20;
				ScsiPadStr8(cmdbuffer + Idx, 20, "53R141");	/* Serial */
				Idx += 20;

				if (Idx + 72 > max_length)
				{
					goto SCSIOut;
				}
				cmdbuffer[Idx++] = 0x02;
				cmdbuffer[Idx++] = 0x01;
				cmdbuffer[Idx++] = 0x00;
				cmdbuffer[Idx++] = 68;
				ScsiPadStr8(cmdbuffer + Idx, 8, "86Box"); /* Vendor */
				Idx += 8;
				ScsiPadStr8(cmdbuffer + Idx, 40, "86BoxCD 1.00"); /* Product */
				Idx += 40;
				ScsiPadStr8(cmdbuffer + Idx, 20, "53R141"); /* Product */
				Idx += 20;
				break;
				
				default:
				SCSIStatus = SCSI_STATUS_CHECK_CONDITION;				
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				SCSICallback[id]=50*SCSI_TIME;
				return;
			}
		}
		else
		{
			PreambleLen = 5;
			SizeIndex = 4;

			cmdbuffer[0] = 0x05; /*CD-ROM*/
			cmdbuffer[1] = 0x80; /*Removable*/
			cmdbuffer[2] = 0;
			cmdbuffer[3] = 0x21;
			cmdbuffer[4] = 31;
			cmdbuffer[5] = 0;
			cmdbuffer[6] = 0;
			cmdbuffer[7] = 0;
			ScsiPadStr8(cmdbuffer + 8, 8, "86Box"); /* Vendor */
			ScsiPadStr8(cmdbuffer + 16, 16, "86BoxCD"); /* Product */
			ScsiPadStr8(cmdbuffer + 32, 4, emulator_version); /* Revision */
			
			Idx = 36;
		}
		
SCSIOut:
		cmdbuffer[SizeIndex] = Idx - PreambleLen;
		len=Idx;
		
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = len;
		
		SCSIDMAResetPosition(id);
		break;
		
		case GPCMD_MODE_SELECT_6:
		case GPCMD_MODE_SELECT_10:
		if (cdb[0] == GPCMD_MODE_SELECT_6)
		{
			len = cdb[4];
			prefix_len = 6;
		}
		else
		{
			len = (cdb[7]<<8)|cdb[8];
			prefix_len = 10;
		}
		
		page_current = cdb[2];
		if (page_flags[page_current] & PAGE_CHANGEABLE)
				page_flags[GPMODE_CDROM_AUDIO_PAGE] |= PAGE_CHANGED;
			
		SCSIPhase = SCSI_PHASE_DATAOUT;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = len;

		SCSIDMAResetPosition(id);
		return;
		
		case GPCMD_MODE_SENSE_6:
		case GPCMD_MODE_SENSE_10:
		if (cdb[0] == GPCMD_MODE_SENSE_6)
			len = cdb[4];
		else
			len = (cdb[7]<<8)|cdb[8];

		Temp = cdb[2] & 0x3F;
		
		memset(cmdbuffer, 0, len);
		
		if (!(mode_sense_pages[cdb[2] & 0x3f] & IMPLEMENTED))
		{
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;	
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
			SCSICallback[id]=50*SCSI_TIME;
			return;
		}

		if (cdb[0] == GPCMD_MODE_SENSE_6)
		{
			len = SCSICDROMModeSense(cmdbuffer, 4, Temp);
			cmdbuffer[0] = len - 1;
			cmdbuffer[1] = 3; /*120mm data CD-ROM*/
		}
		else
		{
			len = SCSICDROMModeSense(cmdbuffer, 8, Temp);
			cmdbuffer[0] = (len - 2)>>8;
			cmdbuffer[1] = (len - 2)&255;
			cmdbuffer[2] = 3; /*120mm data CD-ROM*/
		}		
		
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = len;

		SCSIDMAResetPosition(id);
		return;
		
		case GPCMD_START_STOP_UNIT:
		switch(cdb[4] & 3)
		{
			case 0:		/* Stop the disc. */
				cdrom->stop();
				break;
			case 1:		/* Start the disc and read the TOC. */
				cdrom->medium_changed();	/* This causes a TOC reload. */
				break;
			case 2:		/* Eject the disc if possible. */
				cdrom->stop();
#ifndef __unix
				win_cdrom_eject();
#endif
				break;
			case 3:		/* Load the disc (close tray). */
#ifndef __unix
				win_cdrom_reload();
#else
				cdrom->load();
#endif
				break;
		}
		
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		break;
		
		case GPCMD_PREVENT_REMOVAL:
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		break;

		case GPCMD_READ_CDROM_CAPACITY:
		if (cdrom->read_capacity)
		{
			cdrom->read_capacity(cmdbuffer);
		}
		else
		{
			Size = cdrom->size() - 1;		/* IMPORTANT: What's returned is the last LBA block. */
			memset(cmdbuffer, 0, 8);
			cmdbuffer[0] = (Size >> 24) & 0xff;
			cmdbuffer[1] = (Size >> 16) & 0xff;
			cmdbuffer[2] = (Size >> 8) & 0xff;
			cmdbuffer[3] = Size & 0xff;
			cmdbuffer[6] = 8;				/* 2048 = 0x0800 */
		}		
		
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 8;
		
		SCSIDMAResetPosition(id);
		break;

		case GPCMD_READ_SUBCHANNEL:
		if (cdb[3] > 3)
		{
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
			SCSICallback[id]=50*SCSI_TIME;
			break;
		}

		switch(cdb[3])
		{
			case 0:
				Temp = 4;
				break;
			case 1:
				Temp = 16;
				break;
			default:
				Temp = 24;
				break;
		}

		if (cdrom->read_subchannel)
		{
			cdrom->read_subchannel(cdb, cmdbuffer);
			len = Temp;
		}
		else
		{
			memset(cmdbuffer, 24, 0);
			pos = 0;
			cmdbuffer[pos++]=0;
			cmdbuffer[pos++]=0; /*Audio status*/
			cmdbuffer[pos++]=0; cmdbuffer[pos++]=0; /*Subchannel length*/
			cmdbuffer[pos++]=cdb[3]; /*Format code*/
			if (!(cdb[2] & 0x40) || (cdb[3] == 0))
			{
				len = 4;
			}
			else
			{
				len = Temp;
			}
			if (cdb[3] == 1)
			{
				cmdbuffer[1]=cdrom->getcurrentsubchannel(&cmdbuffer[5],msf);
			}
		}

		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=1000*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = len;

		SCSIDMAResetPosition(id);
		break;

		case GPCMD_READ_TOC_PMA_ATIP:
		switch (SCSICDROM_TOC(id, cdb))
		{
			case 0: /*Normal*/
			len = cdb[8]|(cdb[7]<<8);
			len = cdrom->readtoc(cmdbuffer, cdb[6], msf, len, 0);
			break;
				
			case 1: /*Multi session*/
			len = cdb[8]|(cdb[7]<<8);
			len = cdrom->readtoc_session(cmdbuffer, msf, len);
			cmdbuffer[0] = 0; 
			cmdbuffer[1] = 0xA;
			break;
				
			case 2: /*Raw*/
			len = cdb[8]|(cdb[7]<<8);
			len = cdrom->readtoc_raw(cmdbuffer, msf, len);
			break;
						  
			default:
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
			SCSICallback[id]=50*SCSI_TIME;
			return;
		}
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = len;
		
		SCSIDMAResetPosition(id);
		return;
	
		case GPCMD_READ_HEADER:
		if (cdrom->read_header)
		{
			cdrom->read_header(cdb, cmdbuffer);
		}
		else
		{
			SectorLen=(cdb[7]<<8)|cdb[8];
			SectorLBA=(cdb[2]<<24)|(cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
			if (msf)
			{
				real_pos = cdrom_LBAtoMSF_accurate();
			}
			else
			{
				real_pos = SectorLBA;
			}
			cmdbuffer[4] = (real_pos >> 24);
			cmdbuffer[5] = ((real_pos >> 16) & 0xff);
			cmdbuffer[6] = ((real_pos >> 8) & 0xff);
			cmdbuffer[7] = real_pos & 0xff;
			cmdbuffer[0]=1; /*2048 bytes user data*/
			cmdbuffer[1]=cmdbuffer[2]=cmdbuffer[3]=0;
		}		
		
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 8;
		
		SCSIDMAResetPosition(id);
		break;
	
		case GPCMD_PLAY_AUDIO_10:
		case GPCMD_PLAY_AUDIO_MSF:
		case GPCMD_PLAY_AUDIO_12:
		if (cdb[0] == GPCMD_PLAY_AUDIO_10)
		{
			pos = (cdb[2]<<24)|(cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
			len = (cdb[7]<<8)|cdb[8];
		}
		else if (cdb[0] == GPCMD_PLAY_AUDIO_MSF)
		{
			pos = (cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
			len = (cdb[6]<<16)|(cdb[7]<<8)|cdb[8];
		}
		else
		{
			pos = (cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
			len = (cdb[7]<<16)|(cdb[8]<<8)|cdb[9];
		}

		if ((cdrom_drive < 1) || (cdrom_drive == 200) || (cd_status <= CD_STATUS_DATA_ONLY) ||
			!cdrom->is_track_audio(pos, (cdb[0] == GPCMD_PLAY_AUDIO_MSF) ? 1 : 0))
		{
			pclog("Invalid mode for this track\n");
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_MODE_FOR_THIS_TRACK, 0x00);
			SCSICallback[id]=50*SCSI_TIME;
			break;
		}
			
		cdrom->playaudio(pos, len, (cdb[0] == GPCMD_PLAY_AUDIO_MSF) ? 1 : 0);
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		break;
		
		case GPCMD_GET_CONFIGURATION:
		len = (cdb[7]<<8)|cdb[8];
		
		Index = 0;
 
		/* only feature 0 is supported */
		if (cdb[2] != 0 || cdb[3] != 0)
		{
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
			SCSICallback[id]=50*SCSI_TIME;
			return;
		}
 
		/*
		* XXX: avoid overflow for io_buffer if length is bigger than
		*      the size of that buffer (dimensioned to max number of
		*      sectors to transfer at once)
		*
		*      Only a problem if the feature/profiles grow.
		*/
		if (len > 512) /* XXX: assume 1 sector */
			len = 512;

		memset(cmdbuffer, 0, len);
		/*
		* the number of sectors from the media tells us which profile
		* to use as current.  0 means there is no media
		*/
		if (len > CD_MAX_SECTORS )
		{
			cmdbuffer[6] = (MMC_PROFILE_DVD_ROM >> 8) & 0xff;
			cmdbuffer[7] = MMC_PROFILE_DVD_ROM & 0xff;
		}
		else if (len <= CD_MAX_SECTORS)
		{
			cmdbuffer[6] = (MMC_PROFILE_CD_ROM >> 8) & 0xff;
			cmdbuffer[7] = MMC_PROFILE_CD_ROM & 0xff;
		}
		cmdbuffer[10] = 0x02 | 0x01; /* persistent and current */
		len = 12; /* headers: 8 + 4 */
		len += SCSICDROMSetProfile(cmdbuffer, &Index, MMC_PROFILE_DVD_ROM);
		len += SCSICDROMSetProfile(cmdbuffer, &Index, MMC_PROFILE_CD_ROM);
		cmdbuffer[0] = ((len-4) >> 24) & 0xff;
		cmdbuffer[1] = ((len-4) >> 16) & 0xff;
		cmdbuffer[2] = ((len-4) >> 8) & 0xff;
		cmdbuffer[3] = (len-4) & 0xff;		
		
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = len;
		
		SCSIDMAResetPosition(id);
		break;
		
		case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
		gesn_cdb = (void *)cdb;
		gesn_event_header = (void *)cmdbuffer;
			
		/* It is fine by the MMC spec to not support async mode operations */
		if (!(gesn_cdb->polled & 0x01))
		{   /* asynchronous mode */
			/* Only pollign is supported, asynchronous mode is not. */
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;				
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
			SCSICallback[id]=50*SCSI_TIME;
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
			len = SCSICDROMEventStatus(cmdbuffer);
		}
		else
		{
			gesn_event_header->notification_class = 0x80; /* No event available */
			SCSIDevices[id].CmdBufferLength = sizeof(*gesn_event_header);
		}
		gesn_event_header->len = len - sizeof(*gesn_event_header);		
		
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = len;
		
		SCSIDMAResetPosition(id);
		break;
		
		case GPCMD_PAUSE_RESUME:
		if (cdb[8]&1) 	cdrom->resume();
		else           	cdrom->pause();
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		break;
		
		case GPCMD_STOP_PLAY_SCAN:
		cdrom->stop();
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		break;
		
		case GPCMD_READ_DISC_INFORMATION:
		if (cdrom->read_disc_information)
		{
			cdrom->read_disc_information(cmdbuffer);
		}
		else
		{
			cmdbuffer[1] = 32;
			cmdbuffer[2] = 0xe; /* last session complete, disc finalized */
			cmdbuffer[3] = 1; /* first track on disc */
			cmdbuffer[4] = 1; /* # of sessions */
			cmdbuffer[5] = 1; /* first track of last session */
			cmdbuffer[6] = 1; /* last track of last session */
			cmdbuffer[7] = 0x20; /* unrestricted use */
			cmdbuffer[8] = 0x00; /* CD-ROM */
		}		
		
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 34;
		
		SCSIDMAResetPosition(id);
		break;		
	
		case GPCMD_READ_TRACK_INFORMATION:
		max_length = cdb[7];
		max_length <<= 8;
		max_length |= cdb[8];

		track = ((uint32_t) cdb[2]) << 24;
		track |= ((uint32_t) cdb[3]) << 16;
		track |= ((uint32_t) cdb[4]) << 8;
		track |= (uint32_t) cdb[5];

		if (cdrom->read_track_information)
		{
			ret = cdrom->read_track_information(cdb, cmdbuffer);

			if (!ret)
			{
				SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				SCSICallback[id]=50*SCSI_TIME;
				return;		
			}

			len = cmdbuffer[0];
			len <<= 8;
			len |= cmdbuffer[1];
			len += 2;
		}
		else
		{
			if (((cdb[1] & 0x03) != 1) || (track != 1))
			{
				SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				SCSICallback[id]=50*SCSI_TIME;
				return;		
			}

			len = 36;
			cmdbuffer[1] = 34;
			cmdbuffer[2] = 1; /* track number (LSB) */
			cmdbuffer[3] = 1; /* session number (LSB) */
			cmdbuffer[5] = (0 << 5) | (0 << 4) | (4 << 0); /* not damaged, primary copy, data track */
			cmdbuffer[6] = (0 << 7) | (0 << 6) | (0 << 5) | (0 << 6) | (1 << 0); /* not reserved track, not blank, not packet writing, not fixed packet, data mode 1 */
			cmdbuffer[7] = (0 << 1) | (0 << 0); /* last recorded address not valid, next recordable address not valid */
			cmdbuffer[8] = 0; /* track start address is 0 */
			cmdbuffer[24] = (cdrom->size() >> 24) & 0xff; /* track size */
			cmdbuffer[25] = (cdrom->size() >> 16) & 0xff; /* track size */
			cmdbuffer[26] = (cdrom->size() >> 8) & 0xff; /* track size */
			cmdbuffer[27] = cdrom->size() & 0xff; /* track size */
			cmdbuffer[32] = 0; /* track number (MSB) */
			cmdbuffer[33] = 0; /* session number (MSB) */
		} 

		if (len > max_length)
		{
			len = max_length;
			cmdbuffer[0] = ((max_length - 2) >> 8) & 0xff;
			cmdbuffer[1] = (max_length - 2) & 0xff;
		}		
		
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = len;
		
		SCSIDMAResetPosition(id);
		break;
	
		case GPCMD_READ_DVD_STRUCTURE:
		len = (cdb[6]<<24)|(cdb[7]<<16)|(cdb[8]<<8)|cdb[9];
			
		if (cdb[7] < 0xff) 
		{
			if (len <= CD_MAX_SECTORS) 
			{
				SCSIStatus = SCSI_STATUS_CHECK_CONDITION;				
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INCOMPATIBLE_FORMAT, 0x00);
				SCSICallback[id]=50*SCSI_TIME;
				break;
			} 
			else
			{
				SCSIStatus = SCSI_STATUS_CHECK_CONDITION;				
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				SCSICallback[id]=50*SCSI_TIME;
				return;
			}
		}			
		
		memset(cmdbuffer, 0, len > 256 * 512 + 4 ? 256 * 512 + 4 : len);
				
		switch (cdb[7]) 
		{
			case 0x00 ... 0x7f:
			case 0xff:
			if (cdb[1] == 0) 
			{
				DVDRet = SCSICDROMReadDVDStructure(cdb[7], cdb, cmdbuffer);
	 
				if (DVDRet < 0)
				{
					SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
					SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, -DVDRet, 0x00);
					SCSICallback[id]=50*SCSI_TIME;
					return;
				}
				else
				{
					SCSIPhase = SCSI_PHASE_DATAIN;
					SCSIStatus = SCSI_STATUS_OK;
					SCSICallback[id]=60*SCSI_TIME;
					SCSIDevices[id].CmdBufferLength = len;
					
					SCSIDMAResetPosition(id);
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
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
			SCSICallback[id]=50*SCSI_TIME;
			return;
		}
		break;
		
		case GPCMD_READ_CD_MSF:
		case GPCMD_READ_CD:
		if (cdb[0] == GPCMD_READ_CD_MSF)
		{
			SectorLBA=MSFtoLBA(cdb[3],cdb[4],cdb[5]);
			SectorLen=MSFtoLBA(cdb[6],cdb[7],cdb[8]);

			SectorLen -= SectorLBA;
			SectorLen++;
				
			cdrom_sector_ismsf = 1;
		}
		else
		{
			SectorLen=(cdb[6]<<16)|(cdb[7]<<8)|cdb[8];
			SectorLBA=(cdb[2]<<24)|(cdb[3]<<16)|(cdb[4]<<8)|cdb[5];

			cdrom_sector_ismsf = 0;
		}
		
		if (SectorLBA > (cdrom->size() - 1))
		{
			//pclog("Trying to read beyond the end of disc\n");
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0);
			SCSICallback[id]=50*SCSI_TIME;
			break;
		}		
		
		cdrom_sector_type = (cdb[1] >> 2) & 7;
		cdrom_sector_flags = cdb[9] || ((cdb[10]) << 8);		

		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = SectorLen * cdrom_sector_size;
		
		SCSIDMAResetPosition(id);
		return;
		
		case GPCMD_SET_SPEED:
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		break;
		
		case GPCMD_MECHANISM_STATUS:
		cmdbuffer[0] = 0;
		cmdbuffer[1] = 0;
		cmdbuffer[2] = 0;
		cmdbuffer[3] = 0;
		cmdbuffer[4] = 0;
		cmdbuffer[5] = 1;
		cmdbuffer[6] = 0;
		cmdbuffer[7] = 0;		
		
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 8;
		
		SCSIDMAResetPosition(id);
		break;
	}
	
	
}

void SCSICDROM_ReadData(uint8_t id, uint8_t *cdb, uint8_t *data, int datalen)
{
	int read_length = 0;	
	
	switch (cdb[0])
	{
		case GPCMD_READ_6:
		case GPCMD_READ_10:
		case GPCMD_READ_12:
        case GPCMD_READ_CD_MSF:
        case GPCMD_READ_CD:
		pclog("Total data length requested: %d\n", datalen);
		while (datalen > 0)
		{
			read_length = cdrom_read_data(data); //Fill the buffer the data it needs
			if (!read_length)
			{
				pclog("Invalid read\n");
				SCSIStatus = SCSI_STATUS_CHECK_CONDITION;				
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				SCSICallback[id]=50*SCSI_TIME;
				break;
			}
			else
			{
				//Continue reading data until the sector length is 0.
				data += read_length;
				datalen -= read_length;
			}
		   
			pclog("True LBA: %d, buffer half: %d\n", SectorLBA, SectorLen * cdrom_sector_size);
				
			SectorLBA++;
			SectorLen--;

			if (SectorLen == 0)
				break;
		}
		break;
	}
}
