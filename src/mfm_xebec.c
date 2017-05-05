#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include "ibm.h"

#include "device.h"
#include "dma.h"
#include "io.h"
#include "mem.h"
#include "pic.h"
#include "rom.h"
#include "timer.h"

#include "mfm_xebec.h"

#define XEBEC_TIME (2000 * TIMER_USEC)

enum
{
        STATE_IDLE,
        STATE_RECEIVE_COMMAND,
        STATE_START_COMMAND,
        STATE_RECEIVE_DATA,
        STATE_RECEIVED_DATA,
        STATE_SEND_DATA,
        STATE_SENT_DATA,
        STATE_COMPLETION_BYTE,
        STATE_DUNNO
};

typedef struct mfm_drive_t
{
        int spt, hpc;
        int tracks;
        int cfg_spt;
        int cfg_hpc;
        int cfg_cyl;
        int current_cylinder;
        FILE *hdfile;
} mfm_drive_t;

typedef struct xebec_t
{
        rom_t bios_rom;
        
        int callback;
        
        int state;
        
        uint8_t status;
        
        uint8_t command[6];
        int command_pos;
        
        uint8_t data[512];
        int data_pos, data_len;

        uint8_t sector_buf[512];
        
        uint8_t irq_dma_mask;
        
        uint8_t completion_byte;
        uint8_t error;
        
        int drive_sel;
        
        mfm_drive_t drives[2];
        
        int sector, head, cylinder;
        int sector_count;
        
        uint8_t switches;
} xebec_t;

#define STAT_IRQ 0x20
#define STAT_DRQ 0x10
#define STAT_BSY 0x08
#define STAT_CD  0x04
#define STAT_IO  0x02
#define STAT_REQ 0x01

#define IRQ_ENA 0x02
#define DMA_ENA 0x01

#define CMD_TEST_DRIVE_READY      0x00
#define CMD_RECALIBRATE           0x01
#define CMD_READ_STATUS           0x03
#define CMD_VERIFY_SECTORS        0x05
#define CMD_FORMAT_TRACK          0x06
#define CMD_READ_SECTORS          0x08
#define CMD_WRITE_SECTORS         0x0a
#define CMD_SEEK                  0x0b
#define CMD_INIT_DRIVE_PARAMS     0x0c
#define CMD_WRITE_SECTOR_BUFFER   0x0f
#define CMD_BUFFER_DIAGNOSTIC     0xe0
#define CMD_CONTROLLER_DIAGNOSTIC 0xe4
#define CMD_DTC_GET_DRIVE_PARAMS  0xfb
#define CMD_DTC_SET_STEP_RATE     0xfc
#define CMD_DTC_SET_GEOMETRY      0xfe
#define CMD_DTC_GET_GEOMETRY      0xff

#define ERR_NOT_READY              0x04
#define ERR_SEEK_ERROR             0x15
#define ERR_ILLEGAL_SECTOR_ADDRESS 0x21

static uint8_t xebec_read(uint16_t port, void *p)
{
        xebec_t *xebec = (xebec_t *)p;
        uint8_t temp = 0xff;
        
        switch (port)
        {
                case 0x320: /*Read data*/
                xebec->status &= ~STAT_IRQ;
                switch (xebec->state)
                {
                        case STATE_COMPLETION_BYTE:
                        if ((xebec->status & 0xf) != (STAT_CD | STAT_IO | STAT_REQ | STAT_BSY))
                                fatal("Read data STATE_COMPLETION_BYTE, status=%02x\n", xebec->status);

                        temp = xebec->completion_byte;
                        xebec->status = 0;
                        xebec->state = STATE_IDLE;
                        break;
                        
                        case STATE_SEND_DATA:
                        if ((xebec->status & 0xf) != (STAT_IO | STAT_REQ | STAT_BSY))
                                fatal("Read data STATE_COMPLETION_BYTE, status=%02x\n", xebec->status);
                        if (xebec->data_pos >= xebec->data_len)
                                fatal("Data write with full data!\n");
                        temp = xebec->data[xebec->data_pos++];
                        if (xebec->data_pos == xebec->data_len)
                        {
                                xebec->status = STAT_BSY;
                                xebec->state = STATE_SENT_DATA;
                                xebec->callback = XEBEC_TIME;
                        }
                        break;
                                
                        default:
                        fatal("Read data register - %i, %02x\n", xebec->state, xebec->status);
                }
                break;
                
                case 0x321: /*Read status*/
                temp = xebec->status;
                break;
                
                case 0x322: /*Read option jumpers*/
                temp = xebec->switches;
                break;
        }
        
        return temp;        
}

static void xebec_write(uint16_t port, uint8_t val, void *p)
{
        xebec_t *xebec = (xebec_t *)p;

        switch (port)
        {
                case 0x320: /*Write data*/
                switch (xebec->state)
                {
                        case STATE_RECEIVE_COMMAND:
                        if ((xebec->status & 0xf) != (STAT_BSY | STAT_CD | STAT_REQ))
                                fatal("Bad write data state - STATE_START_COMMAND, status=%02x\n", xebec->status);
                        if (xebec->command_pos >= 6)
                                fatal("Command write with full command!\n");
                        /*Command data*/
                        xebec->command[xebec->command_pos++] = val;
                        if (xebec->command_pos == 6)
                        {
                                xebec->status = STAT_BSY;
                                xebec->state = STATE_START_COMMAND;
                                xebec->callback = XEBEC_TIME;
                        }
                        break;
                        
                        case STATE_RECEIVE_DATA:
                        if ((xebec->status & 0xf) != (STAT_BSY | STAT_REQ))
                                fatal("Bad write data state - STATE_RECEIVE_DATA, status=%02x\n", xebec->status);
                        if (xebec->data_pos >= xebec->data_len)
                                fatal("Data write with full data!\n");
                        /*Command data*/
                        xebec->data[xebec->data_pos++] = val;
                        if (xebec->data_pos == xebec->data_len)
                        {
                                xebec->status = STAT_BSY;
                                xebec->state = STATE_RECEIVED_DATA;
                                xebec->callback = XEBEC_TIME;
                        }
                        break;                                
                        
                        default:
                        fatal("Write data unknown state - %i %02x\n", xebec->state, xebec->status);
                }
                break;
                
                case 0x321: /*Controller reset*/
                xebec->status = 0;
                break;
                
                case 0x322: /*Generate controller-select-pulse*/
                xebec->status = STAT_BSY | STAT_CD | STAT_REQ;
                xebec->command_pos = 0;
                xebec->state = STATE_RECEIVE_COMMAND;
                break;
                
                case 0x323: /*DMA/IRQ mask register*/
                xebec->irq_dma_mask = val;
                break;
        }
}

static void xebec_complete(xebec_t *xebec)
{
        xebec->status = STAT_REQ | STAT_CD | STAT_IO | STAT_BSY;
        xebec->state = STATE_COMPLETION_BYTE;
        if (xebec->irq_dma_mask & IRQ_ENA)
        {
                xebec->status |= STAT_IRQ;
                picint(1 << 5);
        }
}

static void xebec_error(xebec_t *xebec, uint8_t error)
{
        xebec->completion_byte |= 0x02;
        xebec->error = error;
        pclog("xebec_error - %02x\n", xebec->error);
}

static int xebec_get_sector(xebec_t *xebec, off64_t *addr)
{
        mfm_drive_t *drive = &xebec->drives[xebec->drive_sel];
        int heads = drive->cfg_hpc;
        
        if (drive->current_cylinder != xebec->cylinder)
        {
                pclog("mfm_get_sector: wrong cylinder\n");
                xebec->error = ERR_ILLEGAL_SECTOR_ADDRESS;
                return 1;
        }
        if (xebec->head > heads)
        {
                pclog("mfm_get_sector: past end of configured heads\n");
                xebec->error = ERR_ILLEGAL_SECTOR_ADDRESS;
                return 1;
        }
        if (xebec->head > drive->hpc)
        {
                pclog("mfm_get_sector: past end of heads\n");
                xebec->error = ERR_ILLEGAL_SECTOR_ADDRESS;
                return 1;
        }
        if (xebec->sector >= 17)
        {
                pclog("mfm_get_sector: past end of sectors\n");
                xebec->error = ERR_ILLEGAL_SECTOR_ADDRESS;
                return 1;
        }

        *addr = ((((off64_t) xebec->cylinder * heads) + xebec->head) *
        	          17) + xebec->sector;
        
        return 0;
}

static void xebec_next_sector(xebec_t *xebec)
{
        mfm_drive_t *drive = &xebec->drives[xebec->drive_sel];
        
        xebec->sector++;
        if (xebec->sector >= 17)
        {
                xebec->sector = 0;
                xebec->head++;
                if (xebec->head >= drive->cfg_hpc)
                {
                        xebec->head = 0;
                        xebec->cylinder++;
                        drive->current_cylinder++;
                        if (drive->current_cylinder >= drive->cfg_cyl)
                                drive->current_cylinder = drive->cfg_cyl-1;
                }
        }
}

static void xebec_callback(void *p)
{
        xebec_t *xebec = (xebec_t *)p;
        mfm_drive_t *drive;
        
        xebec->callback = 0;
        
        xebec->drive_sel = (xebec->command[1] & 0x20) ? 1 : 0;
        xebec->completion_byte = xebec->drive_sel & 0x20;
        
        drive = &xebec->drives[xebec->drive_sel];
        
        switch (xebec->command[0])
        {
                case CMD_TEST_DRIVE_READY:
                if (!drive->hdfile)
                        xebec_error(xebec, ERR_NOT_READY);
                xebec_complete(xebec);
                break;
                
                case CMD_RECALIBRATE:
                if (!drive->hdfile)
                        xebec_error(xebec, ERR_NOT_READY);
                else
                {
                        xebec->cylinder = 0;
                        drive->current_cylinder = 0;
                }
                xebec_complete(xebec);
                break;
                
                case CMD_READ_STATUS:
                switch (xebec->state)
                {
                        case STATE_START_COMMAND:
                        xebec->state = STATE_SEND_DATA;
                        xebec->data_pos = 0;
                        xebec->data_len = 4;
                        xebec->status = STAT_BSY | STAT_IO | STAT_REQ;
                        xebec->data[0] = xebec->error;
                        xebec->data[1] = xebec->drive_sel ? 0x20 : 0;
                        xebec->data[2] = xebec->data[3] = 0;
                        xebec->error = 0;
                        break;
                        
                        case STATE_SENT_DATA:
                        xebec_complete(xebec);
                        break;
                }
                break;                

                case CMD_VERIFY_SECTORS:
                switch (xebec->state)
                {
                        case STATE_START_COMMAND:
                        xebec->cylinder = xebec->command[3] | ((xebec->command[2] & 0xc0) << 2);
                        drive->current_cylinder = (xebec->cylinder >= drive->cfg_cyl) ? drive->cfg_cyl-1 : xebec->cylinder;
                        xebec->head = xebec->command[1] & 0x1f;
                        xebec->sector = xebec->command[2] & 0x1f;
                        xebec->sector_count = xebec->command[4];
                        do
                        {
                                off64_t addr;
                                        
                                if (xebec_get_sector(xebec, &addr))
                                {
                                        pclog("xebec_get_sector failed\n");
                                        xebec_error(xebec, xebec->error);
                                        xebec_complete(xebec);
                                        return;
                                }
                                        
                                xebec_next_sector(xebec);

                                xebec->sector_count = (xebec->sector_count-1) & 0xff;
                        } while (xebec->sector_count);
                                
                        xebec_complete(xebec);

	                update_status_bar_icon(0x20, 1);
                        break;
                                                
                        default:
                        fatal("CMD_VERIFY_SECTORS: bad state %i\n", xebec->state);
                }
                break;

                case CMD_FORMAT_TRACK:
                {
                        off64_t addr;
                        int c;
                        
                        xebec->cylinder = xebec->command[3] | ((xebec->command[2] & 0xc0) << 2);
                        drive->current_cylinder = (xebec->cylinder >= drive->cfg_cyl) ? drive->cfg_cyl-1 : xebec->cylinder;
                        xebec->head = xebec->command[1] & 0x1f;

                        if (xebec_get_sector(xebec, &addr))
                        {
                                pclog("xebec_get_sector failed\n");
                                xebec_error(xebec, xebec->error);
                                xebec_complete(xebec);
                                return;
                        }
                        
                        fseeko64(drive->hdfile, addr * 512, SEEK_SET);
                        for (c = 0; c < 17; c++)
                                fwrite(xebec->sector_buf, 512, 1, drive->hdfile);
                                
                        xebec_complete(xebec);
                }
                break;                               
                
                case CMD_READ_SECTORS:                
                switch (xebec->state)
                {
                        case STATE_START_COMMAND:
                        xebec->cylinder = xebec->command[3] | ((xebec->command[2] & 0xc0) << 2);
                        drive->current_cylinder = (xebec->cylinder >= drive->cfg_cyl) ? drive->cfg_cyl-1 : xebec->cylinder;
                        xebec->head = xebec->command[1] & 0x1f;
                        xebec->sector = xebec->command[2] & 0x1f;
                        xebec->sector_count = xebec->command[4];
                        xebec->state = STATE_SEND_DATA;
                        xebec->data_pos = 0;
                        xebec->data_len = 512;
                        {
                                off64_t addr;

                                if (xebec_get_sector(xebec, &addr))
                                {
                                        xebec_error(xebec, xebec->error);
                                        xebec_complete(xebec);
                                        return;
                                }

                                fseeko64(drive->hdfile, addr * 512, SEEK_SET);
                                fread(xebec->sector_buf, 512, 1, drive->hdfile);
		                update_status_bar_icon(0x20, 1);
                        }
                        if (xebec->irq_dma_mask & DMA_ENA)
                                xebec->callback = XEBEC_TIME;
                        else
                        {
                                xebec->status = STAT_BSY | STAT_IO | STAT_REQ;
                                memcpy(xebec->data, xebec->sector_buf, 512);
                        }
                        break;
                        
                        case STATE_SEND_DATA:
                        xebec->status = STAT_BSY;
                        if (xebec->irq_dma_mask & DMA_ENA)
                        {
                                for (; xebec->data_pos < 512; xebec->data_pos++)
                                {
                                        int val = dma_channel_write(3, xebec->sector_buf[xebec->data_pos]);
                                        
                                        if (val == DMA_NODATA)
                                        {
                                                pclog("CMD_READ_SECTORS out of data!\n");
                                                xebec->status = STAT_BSY | STAT_CD | STAT_IO | STAT_REQ;
                                                xebec->callback = XEBEC_TIME;
                                                return;
                                        }
                                }
                                xebec->state = STATE_SENT_DATA;
                                xebec->callback = XEBEC_TIME;
                        }
                        else
                                fatal("Read sectors no DMA! - shouldn't get here\n");
                        break;

                        case STATE_SENT_DATA:
                        xebec_next_sector(xebec);
                                        
                        xebec->data_pos = 0;

                        xebec->sector_count = (xebec->sector_count-1) & 0xff;
                        
                        if (xebec->sector_count)
                        {
                                off64_t addr;

                                if (xebec_get_sector(xebec, &addr))
                                {
                                        xebec_error(xebec, xebec->error);
                                        xebec_complete(xebec);
                                        return;
                                }

                                fseeko64(drive->hdfile, addr * 512, SEEK_SET);
                                fread(xebec->sector_buf, 512, 1, drive->hdfile);
		                update_status_bar_icon(0x20, 1);

                                xebec->state = STATE_SEND_DATA;
                                
                                if (xebec->irq_dma_mask & DMA_ENA)
                                        xebec->callback = XEBEC_TIME;
                                else
                                {
                                        xebec->status = STAT_BSY | STAT_IO | STAT_REQ;
                                        memcpy(xebec->data, xebec->sector_buf, 512);
                                }
                        }
                        else
			{
                                xebec_complete(xebec);
		                update_status_bar_icon(0x20, 0);
			}
                        break;

                                                
                        default:
                        fatal("CMD_READ_SECTORS: bad state %i\n", xebec->state);
                }
                break;
                
                case CMD_WRITE_SECTORS:
                switch (xebec->state)
                {
                        case STATE_START_COMMAND:
                        xebec->cylinder = xebec->command[3] | ((xebec->command[2] & 0xc0) << 2);
                        drive->current_cylinder = (xebec->cylinder >= drive->cfg_cyl) ? drive->cfg_cyl-1 : xebec->cylinder;
                        xebec->head = xebec->command[1] & 0x1f;
                        xebec->sector = xebec->command[2] & 0x1f;
                        xebec->sector_count = xebec->command[4];
                        xebec->state = STATE_RECEIVE_DATA;
                        xebec->data_pos = 0;
                        xebec->data_len = 512;
                        if (xebec->irq_dma_mask & DMA_ENA)
                                xebec->callback = XEBEC_TIME;
                        else
                                xebec->status = STAT_BSY | STAT_REQ;
                        break;

                        case STATE_RECEIVE_DATA:
                        xebec->status = STAT_BSY;
                        if (xebec->irq_dma_mask & DMA_ENA)
                        {
                                for (; xebec->data_pos < 512; xebec->data_pos++)
                                {
                                        int val = dma_channel_read(3);
                                        
                                        if (val == DMA_NODATA)
                                        {
                                                pclog("CMD_WRITE_SECTORS out of data!\n");
                                                xebec->status = STAT_BSY | STAT_CD | STAT_IO | STAT_REQ;
                                                xebec->callback = XEBEC_TIME;
                                                return;
                                        }
                                                
                                        xebec->sector_buf[xebec->data_pos] = val & 0xff;
                                }

                                xebec->state = STATE_RECEIVED_DATA;
                                xebec->callback = XEBEC_TIME;
                        }
                        else
                                fatal("Write sectors no DMA! - should never get here\n");
                        break;
                        
                        case STATE_RECEIVED_DATA:
                        if (!(xebec->irq_dma_mask & DMA_ENA))
                                memcpy(xebec->sector_buf, xebec->data, 512);

                        {
                                off64_t addr;
                                                
                                if (xebec_get_sector(xebec, &addr))
                                {
                                        xebec_error(xebec, xebec->error);
                                        xebec_complete(xebec);
                                        return;
                                }

                                fseeko64(drive->hdfile, addr * 512, SEEK_SET);
                                fwrite(xebec->sector_buf, 512, 1, drive->hdfile);
                        }
                                
	                update_status_bar_icon(0x20, 1);
                        
                        xebec_next_sector(xebec);
                        xebec->data_pos = 0;
                        xebec->sector_count = (xebec->sector_count-1) & 0xff;

                        if (xebec->sector_count)
                        {
                                xebec->state = STATE_RECEIVE_DATA;
                                if (xebec->irq_dma_mask & DMA_ENA)
                                        xebec->callback = XEBEC_TIME;
                                else
                                        xebec->status = STAT_BSY | STAT_REQ;
                        }
                        else
                                xebec_complete(xebec);
                        break;
                                                
                        default:
                        fatal("CMD_WRITE_SECTORS: bad state %i\n", xebec->state);
                }
                break;

                case CMD_SEEK:
                if (!drive->hdfile)
                        xebec_error(xebec, ERR_NOT_READY);
                else
                {
                        int cylinder = xebec->command[3] | ((xebec->command[2] & 0xc0) << 2);

                        drive->current_cylinder = (cylinder >= drive->cfg_cyl) ? drive->cfg_cyl-1 : cylinder;
                        
                        if (cylinder != drive->current_cylinder)
                                xebec_error(xebec, ERR_SEEK_ERROR);
                }
                xebec_complete(xebec);
                break;
                                       
                case CMD_INIT_DRIVE_PARAMS:
                switch (xebec->state)
                {
                        case STATE_START_COMMAND:
                        xebec->state = STATE_RECEIVE_DATA;
                        xebec->data_pos = 0;
                        xebec->data_len = 8;
                        xebec->status = STAT_BSY | STAT_REQ;
                        break;
                        
                        case STATE_RECEIVED_DATA:
                        drive->cfg_cyl = xebec->data[1] | (xebec->data[0] << 8);
                        drive->cfg_hpc = xebec->data[2];
                        pclog("Drive %i: cylinders=%i, heads=%i\n", xebec->drive_sel, drive->cfg_cyl, drive->cfg_hpc);
                        xebec_complete(xebec);
                        break;
                        
                        default:
                        fatal("CMD_INIT_DRIVE_PARAMS bad state %i\n", xebec->state);
                }
                break;

                case CMD_WRITE_SECTOR_BUFFER:
                switch (xebec->state)
                {
                        case STATE_START_COMMAND:
                        xebec->state = STATE_RECEIVE_DATA;
                        xebec->data_pos = 0;
                        xebec->data_len = 512;
                        if (xebec->irq_dma_mask & DMA_ENA)
                                xebec->callback = XEBEC_TIME;
                        else
                                xebec->status = STAT_BSY | STAT_REQ;
                        break;

                        case STATE_RECEIVE_DATA:
                        if (xebec->irq_dma_mask & DMA_ENA)
                        {
                                xebec->status = STAT_BSY;

                                for (; xebec->data_pos < 512; xebec->data_pos++)
                                {
                                        int val = dma_channel_read(3);
                                        
                                        if (val == DMA_NODATA)
                                        {
                                                pclog("CMD_WRITE_SECTOR_BUFFER out of data!\n");
                                                xebec->status = STAT_BSY | STAT_CD | STAT_IO | STAT_REQ;
                                                xebec->callback = XEBEC_TIME;
                                                return;
                                        }
                                        
                                        xebec->data[xebec->data_pos] = val & 0xff;
                                }
                                
                                xebec->state = STATE_RECEIVED_DATA;
                                xebec->callback = XEBEC_TIME;
                        }
                        else
                                fatal("CMD_WRITE_SECTOR_BUFFER - should never get here!\n");
                        break;
                        case STATE_RECEIVED_DATA:
                        memcpy(xebec->sector_buf, xebec->data, 512);
                        xebec_complete(xebec);
                        break;
                        
                        default:
                        fatal("CMD_WRITE_SECTOR_BUFFER bad state %i\n", xebec->state);
                }
                break;

                case CMD_BUFFER_DIAGNOSTIC:
                case CMD_CONTROLLER_DIAGNOSTIC:
                xebec_complete(xebec);
                break;
                
                case 0xfa:
                xebec_complete(xebec);
                break;

                case CMD_DTC_SET_STEP_RATE:
                xebec_complete(xebec);
                break;

                case CMD_DTC_GET_DRIVE_PARAMS:
                switch (xebec->state)
                {
                        case STATE_START_COMMAND:
                        xebec->state = STATE_SEND_DATA;
                        xebec->data_pos = 0;
                        xebec->data_len = 4;
                        xebec->status = STAT_BSY | STAT_IO | STAT_REQ;
                        memset(xebec->data, 0, 4);
                        xebec->data[0] = drive->tracks & 0xff;
                        xebec->data[1] = 17 | ((drive->tracks >> 2) & 0xc0);
                        xebec->data[2] = drive->hpc-1;
                        pclog("Get drive params %02x %02x %02x %i\n", xebec->data[0], xebec->data[1], xebec->data[2], drive->tracks);
                        break;
                        
                        case STATE_SENT_DATA:
                        xebec_complete(xebec);
                        break;
                        
                        default:
                        fatal("CMD_INIT_DRIVE_PARAMS bad state %i\n", xebec->state);
                }
                break;

                case CMD_DTC_GET_GEOMETRY:
                switch (xebec->state)
                {
                        case STATE_START_COMMAND:
                        xebec->state = STATE_SEND_DATA;
                        xebec->data_pos = 0;
                        xebec->data_len = 16;
                        xebec->status = STAT_BSY | STAT_IO | STAT_REQ;
                        memset(xebec->data, 0, 16);
                        xebec->data[0x4] = drive->tracks & 0xff;
                        xebec->data[0x5] = (drive->tracks >> 8) & 0xff;
                        xebec->data[0xa] = drive->hpc;
                        break;
                        
                        case STATE_SENT_DATA:
                        xebec_complete(xebec);
                        break;
                }
                break;

                case CMD_DTC_SET_GEOMETRY:
                switch (xebec->state)
                {
                        case STATE_START_COMMAND:
                        xebec->state = STATE_RECEIVE_DATA;
                        xebec->data_pos = 0;
                        xebec->data_len = 16;
                        xebec->status = STAT_BSY | STAT_REQ;
                        break;
                        
                        case STATE_RECEIVED_DATA:
                        /*Bit of a cheat here - we always report the actual geometry of the drive in use*/
                        xebec_complete(xebec);
                        break;
                }
                break;
                                
                default:
                fatal("Unknown Xebec command - %02x %02x %02x %02x %02x %02x\n",
                        xebec->command[0], xebec->command[1],
                        xebec->command[2], xebec->command[3],
                        xebec->command[4], xebec->command[5]);
        }
}

static void loadhd(xebec_t *xebec, int d, const wchar_t *fn)
{
        mfm_drive_t *drive = &xebec->drives[d];
        
	if (drive->hdfile == NULL)
        {
		/* Try to open existing hard disk image */
		drive->hdfile = _wfopen(fn, L"rb+");
		if (drive->hdfile == NULL)
                {
			/* Failed to open existing hard disk image */
			if (errno == ENOENT)
                        {
				/* Failed because it does not exist,
				   so try to create new file */
				drive->hdfile = _wfopen(fn, L"wb+");
				if (drive->hdfile == NULL)
                                {
					pclog("Cannot create file '%s': %s",
					      fn, strerror(errno));
					return;
				}
			}
                        else
                        {
				/* Failed for another reason */
				pclog("Cannot open file '%s': %s",
				      fn, strerror(errno));
				return;
			}
		}
	}

        drive->spt = hdc[d].spt;
        drive->hpc = hdc[d].hpc;
        drive->tracks = hdc[d].tracks;
}

static struct
{
        int tracks, hpc;
} xebec_hd_types[4] =
{
        {306, 4}, /*Type 0*/
        {612, 4}, /*Type 16*/
        {615, 4}, /*Type 2*/
        {306, 8}  /*Type 13*/
};

static void xebec_set_switches(xebec_t *xebec)
{
        int c, d;
        
        xebec->switches = 0;
        
        for (d = 0; d < 2; d++)
        {
                mfm_drive_t *drive = &xebec->drives[d];
                
                if (!drive->hdfile)
                        continue;
                
                for (c = 0; c < 4; c++)
                {
                        if (drive->spt == 17 &&
                            drive->hpc == xebec_hd_types[c].hpc &&
                            drive->tracks == xebec_hd_types[c].tracks)
                        {
                                xebec->switches |= (c << (d ? 0 : 2));
                                break;
                        }
                }
                
                if (c == 4)
                        pclog("WARNING: Drive %c: has format not supported by Fixed Disk Adapter", d ? 'D' : 'C');
        }
}

static void *xebec_init()
{
	int i = 0;

        xebec_t *xebec = malloc(sizeof(xebec_t));
        memset(xebec, 0, sizeof(xebec_t));

	for (i = 0; i < HDC_NUM; i++)
	{
		if (hdc[i].bus == 1)
		{
			loadhd(xebec, hdc[i].mfm_channel, hdd_fn[i]);
		}
	}

	xebec_set_switches(xebec);

        rom_init(&xebec->bios_rom, "roms/ibm_xebec_62x0822_1985.bin", 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
                
        io_sethandler(0x0320, 0x0004, xebec_read, NULL, NULL, xebec_write, NULL, NULL, xebec);

        timer_add(xebec_callback, &xebec->callback, &xebec->callback, xebec);
        
        return xebec;
}

static void xebec_close(void *p)
{
        xebec_t *xebec = (xebec_t *)p;
        int d;

        for (d = 0; d < 2; d++)
        {
                mfm_drive_t *drive = &xebec->drives[d];
                
                if (drive->hdfile != NULL)
                        fclose(drive->hdfile);
        }
        
        free(xebec);
}

static int xebec_available()
{
        return rom_present("roms/ibm_xebec_62x0822_1985.bin");
}

device_t mfm_xebec_device =
{
        "IBM PC Fixed Disk Adapter",
        0,
        xebec_init,
        xebec_close,
        xebec_available,
        NULL,
        NULL,
        NULL,
        NULL
};

static void *dtc_5150x_init()
{
	int i = 0;

        xebec_t *xebec = malloc(sizeof(xebec_t));
        memset(xebec, 0, sizeof(xebec_t));

	for (i = 0; i < HDC_NUM; i++)
	{
		if (hdc[i].bus == 1)
		{
			loadhd(xebec, hdc[i].mfm_channel, hdd_fn[i]);
		}
	}

        xebec->switches = 0xff;

        xebec->drives[0].cfg_cyl = xebec->drives[0].tracks;
        xebec->drives[0].cfg_hpc = xebec->drives[0].hpc;
        xebec->drives[1].cfg_cyl = xebec->drives[1].tracks;
        xebec->drives[1].cfg_hpc = xebec->drives[1].hpc;

        rom_init(&xebec->bios_rom, "roms/dtc_cxd21a.bin", 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
                
        io_sethandler(0x0320, 0x0004, xebec_read, NULL, NULL, xebec_write, NULL, NULL, xebec);

        timer_add(xebec_callback, &xebec->callback, &xebec->callback, xebec);
        
        return xebec;
}
static int dtc_5150x_available()
{
        return rom_present("roms/dtc_cxd21a.bin");
}

device_t dtc_5150x_device =
{
        "DTC 5150X",
        0,
        dtc_5150x_init,
        xebec_close,
        dtc_5150x_available,
        NULL,
        NULL,
        NULL,
        NULL
};
