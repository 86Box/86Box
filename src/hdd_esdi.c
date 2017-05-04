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
#include "mca.h"
#include "mem.h"
#include "pic.h"
#include "rom.h"
#include "timer.h"

#include "hdd_esdi.h"

#define ESDI_TIME (2000 * TIMER_USEC)

#define CMD_ADAPTER 0

typedef struct esdi_drive_t
{
        int spt, hpc;
        int tracks;
        int sectors;
        FILE *hdfile;
} esdi_drive_t;

typedef struct esdi_t
{
        rom_t bios_rom;
        
        uint8_t basic_ctrl;
        uint8_t status;
        uint8_t irq_status;
        
        int irq_in_progress;
        int cmd_req_in_progress;
        
        int cmd_pos;
        uint16_t cmd_data[4];
        int cmd_dev;
        
        int status_pos, status_len;
        uint16_t status_data[256];

        int data_pos;
        uint16_t data[256];

        uint16_t sector_buffer[16][256];

        int sector_pos;
        int sector_count;
                
        int command;
        int cmd_state;
        
        int in_reset;
        int callback;
        
        uint32_t rba;
        
        struct
        {
                int req_in_progress;
        } cmds[3];
        
        esdi_drive_t drives[2];
        
        uint8_t pos_regs[8];
} esdi_t;

#define STATUS_DMA_ENA         (1 << 7)
#define STATUS_IRQ_PENDING     (1 << 6)
#define STATUS_CMD_IN_PROGRESS (1 << 5)
#define STATUS_BUSY            (1 << 4)
#define STATUS_STATUS_OUT_FULL (1 << 3)
#define STATUS_CMD_IR_FULL     (1 << 2)
#define STATUS_TRANSFER_REQ    (1 << 1)
#define STATUS_IRQ             (1 << 0)

#define CTRL_RESET   (1 << 7)
#define CTRL_DMA_ENA (1 << 1)
#define CTRL_IRQ_ENA (1 << 0)

#define IRQ_HOST_ADAPTER (7 << 5)
#define IRQ_DEVICE_0     (0 << 5)
#define IRQ_CMD_COMPLETE_SUCCESS 0x1
#define IRQ_RESET_COMPLETE       0xa
#define IRQ_DATA_TRANSFER_READY  0xb
#define IRQ_CMD_COMPLETE_FAILURE 0xc

#define ATTN_DEVICE_SEL   (7 << 5)
#define ATTN_HOST_ADAPTER (7 << 5)
#define ATTN_DEVICE_0     (0 << 5)
#define ATTN_DEVICE_1     (1 << 5)
#define ATTN_REQ_MASK     0xf
#define ATTN_CMD_REQ      1
#define ATTN_EOI          2
#define ATTN_RESET        4

#define CMD_SIZE_4 (1 << 14)

#define CMD_DEVICE_SEL     (7 << 5)
#define CMD_MASK           0x1f
#define CMD_READ           0x01
#define CMD_WRITE          0x02
#define CMD_READ_VERIFY    0x03
#define CMD_WRITE_VERIFY   0x04
#define CMD_SEEK           0x05
#define CMD_GET_DEV_CONFIG 0x09
#define CMD_GET_POS_INFO   0x0a

#define STATUS_LEN(x) ((x) << 8)
#define STATUS_DEVICE_HOST_ADAPTER (7 << 5)

static __inline void esdi_set_irq(esdi_t *esdi)
{
        if (esdi->basic_ctrl & CTRL_IRQ_ENA)
                picint(1 << 14);
}
static __inline void esdi_clear_irq()
{
        picintc(1 << 14);
}

static uint8_t esdi_read(uint16_t port, void *p)
{
        esdi_t *esdi = (esdi_t *)p;
        uint8_t temp = 0xff;

        switch (port)
        {
                case 0x3512: /*Basic status register*/
                temp = esdi->status;
                break;
                case 0x3513: /*IRQ status*/
                esdi->status &= ~STATUS_IRQ;
                temp = esdi->irq_status;
                break;
                
                default:
                fatal("esdi_read port=%04x\n", port);
        }
        
        return temp;        
}

static void esdi_write(uint16_t port, uint8_t val, void *p)
{
        esdi_t *esdi = (esdi_t *)p;

        switch (port)
        {
                case 0x3512: /*Basic control register*/
                if ((esdi->basic_ctrl & CTRL_RESET) && !(val & CTRL_RESET))
                {
                        esdi->in_reset = 1;
                        esdi->callback = ESDI_TIME * 50;
                        esdi->status = STATUS_BUSY;
                }
                esdi->basic_ctrl = val;
                if (!(esdi->basic_ctrl & CTRL_IRQ_ENA))
                        picintc(1 << 14);
                break;
                case 0x3513: /*Attention register*/
                switch (val & ATTN_DEVICE_SEL)
                {
                        case ATTN_HOST_ADAPTER:
                        switch (val & ATTN_REQ_MASK)
                        {
                                case ATTN_CMD_REQ:
                                if (esdi->cmd_req_in_progress)
                                        fatal("Try to start command on in_progress adapter\n");
                                esdi->cmd_req_in_progress = 1;
                                esdi->cmd_dev = ATTN_HOST_ADAPTER;
                                esdi->status |= STATUS_BUSY;
                                esdi->cmd_pos = 0;
                                break;
                                
                                case ATTN_EOI:
                                esdi->irq_in_progress = 0;
                                esdi->status &= ~STATUS_IRQ;
                                esdi_clear_irq();
                                break;

                                case ATTN_RESET:
                                esdi->in_reset = 1;
                                esdi->callback = ESDI_TIME * 50;
                                esdi->status = STATUS_BUSY;
                                break;
                                
                                default:
                                fatal("Bad attention request %02x\n", val);
                        }
                        break;

                        case ATTN_DEVICE_0:
                        switch (val & ATTN_REQ_MASK)
                        {
                                case ATTN_CMD_REQ:
                                if (esdi->cmd_req_in_progress)
                                        fatal("Try to start command on in_progress device0\n");
                                esdi->cmd_req_in_progress = 1;
                                esdi->cmd_dev = ATTN_DEVICE_0;
                                esdi->status |= STATUS_BUSY;
                                esdi->cmd_pos = 0;
                                break;
                                
                                case ATTN_EOI:
                                esdi->irq_in_progress = 0;
                                esdi->status &= ~STATUS_IRQ;
                                esdi_clear_irq();
                                break;
                                
                                default:
                                fatal("Bad attention request %02x\n", val);
                        }
                        break;

                        case ATTN_DEVICE_1:
                        switch (val & ATTN_REQ_MASK)
                        {
                                case ATTN_CMD_REQ:
                                if (esdi->cmd_req_in_progress)
                                        fatal("Try to start command on in_progress device0\n");
                                esdi->cmd_req_in_progress = 1;
                                esdi->cmd_dev = ATTN_DEVICE_1;
                                esdi->status |= STATUS_BUSY;
                                esdi->cmd_pos = 0;
                                break;
                                
                                case ATTN_EOI:
                                esdi->irq_in_progress = 0;
                                esdi->status &= ~STATUS_IRQ;
                                esdi_clear_irq();
                                break;
                                
                                default:
                                fatal("Bad attention request %02x\n", val);
                        }
                        break;
                                
                        default:
                        fatal("Attention to unknown device %02x\n", val);
                }
                break;
                
                default:
                fatal("esdi_write port=%04x val=%02x\n", port, val);
        }
}

static uint16_t esdi_readw(uint16_t port, void *p)
{
        esdi_t *esdi = (esdi_t *)p;
        uint16_t temp = 0xffff;

        switch (port)
        {
                case 0x3510: /*Status Interface Register*/
                if (esdi->status_pos >= esdi->status_len)
                        return 0;
                temp = esdi->status_data[esdi->status_pos++];
                if (esdi->status_pos >= esdi->status_len)
                {
                        esdi->status &= ~STATUS_STATUS_OUT_FULL;
                        esdi->status_pos = esdi->status_len = 0;
                }
                break;
                
                default:
                fatal("esdi_readw port=%04x\n", port);
        }
        
        return temp;        
}

static void esdi_writew(uint16_t port, uint16_t val, void *p)
{
        esdi_t *esdi = (esdi_t *)p;

        switch (port)
        {
                case 0x3510: /*Command Interface Register*/
                if (esdi->cmd_pos >= 4)
                        fatal("CIR pos 4\n");
                esdi->cmd_data[esdi->cmd_pos++] = val;
                if ( ((esdi->cmd_data[0] & CMD_SIZE_4) && esdi->cmd_pos == 4) ||
                    (!(esdi->cmd_data[0] & CMD_SIZE_4) && esdi->cmd_pos == 2))
                {
                        
                        esdi->cmd_pos = 0;
                        esdi->cmd_req_in_progress = 0;
                        esdi->cmd_state = 0;

                        if ((esdi->cmd_data[0] & CMD_DEVICE_SEL) != esdi->cmd_dev)
                                fatal("Command device mismatch with attn\n");
                        esdi->command = esdi->cmd_data[0] & CMD_MASK;
                        esdi->callback = ESDI_TIME;
                        esdi->status = STATUS_BUSY;
                        esdi->data_pos = 0;
                }
                break;
                
                default:
                fatal("esdi_writew port=%04x val=%04x\n", port, val);
        }
}

static void cmd_unsupported(esdi_t *esdi)
{
        esdi->status_len = 9;
        esdi->status_data[0] = esdi->command | STATUS_LEN(9) | esdi->cmd_dev;
        esdi->status_data[1] = 0x0f03; /*Attention error, command not supported*/
        esdi->status_data[2] = 0x0002; /*Interface fault*/
        esdi->status_data[3] = 0;
        esdi->status_data[4] = 0;
        esdi->status_data[5] = 0;
        esdi->status_data[6] = 0;
        esdi->status_data[7] = 0;
        esdi->status_data[8] = 0;

        esdi->status = STATUS_IRQ | STATUS_STATUS_OUT_FULL;
        esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_FAILURE;
        esdi->irq_in_progress = 1;
        esdi_set_irq(esdi);
}

static void device_not_present(esdi_t *esdi)
{
        esdi->status_len = 9;
        esdi->status_data[0] = esdi->command | STATUS_LEN(9) | esdi->cmd_dev;
        esdi->status_data[1] = 0x0c11; /*Command failed, internal hardware error*/
        esdi->status_data[2] = 0x000b; /*Selection error*/
        esdi->status_data[3] = 0;
        esdi->status_data[4] = 0;
        esdi->status_data[5] = 0;
        esdi->status_data[6] = 0;                        
        esdi->status_data[7] = 0;                       
        esdi->status_data[8] = 0;

        esdi->status = STATUS_IRQ | STATUS_STATUS_OUT_FULL;
        esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_FAILURE;
        esdi->irq_in_progress = 1;
        esdi_set_irq(esdi);
}

#define ESDI_ADAPTER_ONLY() do \
        {                                                 \
                if (esdi->cmd_dev != ATTN_HOST_ADAPTER)   \
                {                                         \
                        cmd_unsupported(esdi);            \
                        return;                           \
                }                                         \
        } while (0)

#define ESDI_DRIVE_ONLY() do \
        {                                                                               \
                if (esdi->cmd_dev != ATTN_DEVICE_0 && esdi->cmd_dev != ATTN_DEVICE_1)   \
                {                                                                       \
                        cmd_unsupported(esdi);                                          \
                        return;                                                         \
                }                                                                       \
                if (esdi->cmd_dev == ATTN_DEVICE_0)                                     \
                        drive = &esdi->drives[0];                                       \
                else                                                                    \
                        drive = &esdi->drives[1];                                       \
        } while (0)
                
static void esdi_callback(void *p)
{
        esdi_t *esdi = (esdi_t *)p;
        esdi_drive_t *drive;
        
        esdi->callback = 0;
        
        if (esdi->in_reset)
        {
                esdi->in_reset = 0;
                esdi->status = STATUS_IRQ;
                esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_RESET_COMPLETE;
                
                return;
        }
        switch (esdi->command)
        {
                case CMD_READ:
                ESDI_DRIVE_ONLY();

                if (!drive->hdfile)
                {
                        device_not_present(esdi);
                        return;
                }
                
                switch (esdi->cmd_state)
                {
                        case 0:
                        esdi->rba = (esdi->cmd_data[2] | (esdi->cmd_data[3] << 16)) & 0x0fffffff;

                        esdi->sector_pos = 0;
                        esdi->sector_count = esdi->cmd_data[1];
                                
                        esdi->status = STATUS_IRQ | STATUS_CMD_IN_PROGRESS | STATUS_TRANSFER_REQ;
                        esdi->irq_status = esdi->cmd_dev | IRQ_DATA_TRANSFER_READY;
                        esdi->irq_in_progress = 1;
                        esdi_set_irq(esdi);
                        
                        esdi->cmd_state = 1;
                        esdi->callback = ESDI_TIME;
                        esdi->data_pos = 0;
                        break;
                        
                        case 1:
                        if (!(esdi->basic_ctrl & CTRL_DMA_ENA))
                        {
                                esdi->callback = ESDI_TIME;
                                return;
                        }
                        while (esdi->sector_pos < esdi->sector_count)
                        {
                                if (!esdi->data_pos)
                                {
                                        if (esdi->rba >= drive->sectors)
                                                fatal("Read past end of drive\n");
                                        fseek(drive->hdfile, esdi->rba * 512, SEEK_SET);
                                        fread(esdi->data, 512, 1, drive->hdfile);
			                update_status_bar_icon(0x20, 1);
                                }
                                while (esdi->data_pos < 256)
                                {
                                        int val = dma_channel_write(5, esdi->data[esdi->data_pos]);
                                
                                        if (val == DMA_NODATA)
                                        {
                                                esdi->callback = ESDI_TIME;
                                                return;
                                        }
                                
                                        esdi->data_pos++;
                                }
                                
                                esdi->data_pos = 0;
                                esdi->sector_pos++;
                                esdi->rba++;
                        }

                        esdi->status = STATUS_CMD_IN_PROGRESS;
                        esdi->cmd_state = 2;
                        esdi->callback = ESDI_TIME;
                        break;
                        
                        case 2:
                        esdi->status = STATUS_IRQ;
                        esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_SUCCESS;
                        esdi->irq_in_progress = 1;
                        esdi_set_irq(esdi);
                        break;
                }
                break;
                
                case CMD_WRITE:
                case CMD_WRITE_VERIFY:
                ESDI_DRIVE_ONLY();

                if (!drive->hdfile)
                {
                        device_not_present(esdi);
                        return;
                }
                
                switch (esdi->cmd_state)
                {
                        case 0:
                        esdi->rba = (esdi->cmd_data[2] | (esdi->cmd_data[3] << 16)) & 0x0fffffff;

                        esdi->sector_pos = 0;
                        esdi->sector_count = esdi->cmd_data[1];
                                
                        esdi->status = STATUS_IRQ | STATUS_CMD_IN_PROGRESS | STATUS_TRANSFER_REQ;
                        esdi->irq_status = esdi->cmd_dev | IRQ_DATA_TRANSFER_READY;
                        esdi->irq_in_progress = 1;
                        esdi_set_irq(esdi);
                        
                        esdi->cmd_state = 1;
                        esdi->callback = ESDI_TIME;
                        esdi->data_pos = 0;
                        break;
                        
                        case 1:
                        if (!(esdi->basic_ctrl & CTRL_DMA_ENA))
                        {
                                esdi->callback = ESDI_TIME;
                                return;
                        }
                        while (esdi->sector_pos < esdi->sector_count)
                        {
                                while (esdi->data_pos < 256)
                                {
                                        int val = dma_channel_read(5);
                                
                                        if (val == DMA_NODATA)
                                        {
                                                esdi->callback = ESDI_TIME;
                                                return;
                                        }
                                
                                        esdi->data[esdi->data_pos++] = val & 0xffff;
                                }

                                if (esdi->rba >= drive->sectors)
                                        fatal("Write past end of drive\n");
                                fseek(drive->hdfile, esdi->rba * 512, SEEK_SET);
                                fwrite(esdi->data, 512, 1, drive->hdfile);
                                esdi->rba++;
                                esdi->sector_pos++;
		                update_status_bar_icon(0x20, 1);
                                        
                                esdi->data_pos = 0;
                        }
	                update_status_bar_icon(0x20, 0);

                        esdi->status = STATUS_CMD_IN_PROGRESS;
                        esdi->cmd_state = 2;
                        esdi->callback = ESDI_TIME;
                        break;
                        
                        case 2:
                        esdi->status = STATUS_IRQ;
                        esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_SUCCESS;
                        esdi->irq_in_progress = 1;
                        esdi_set_irq(esdi);
                        break;
                }
                break;
                
                case CMD_READ_VERIFY:
                ESDI_DRIVE_ONLY();

                if (!drive->hdfile)
                {
                        device_not_present(esdi);
                        return;
                }

                esdi->status = STATUS_IRQ;
                esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_SUCCESS;
                esdi->irq_in_progress = 1;
                esdi_set_irq(esdi);
                break;

                case CMD_SEEK:
                ESDI_DRIVE_ONLY();

                if (!drive->hdfile)
                {
                        device_not_present(esdi);
                        return;
                }

                esdi->status = STATUS_IRQ;
                esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_SUCCESS;
                esdi->irq_in_progress = 1;
                esdi_set_irq(esdi);
                break;

                case CMD_GET_DEV_CONFIG:
                ESDI_DRIVE_ONLY();

                if (!drive->hdfile)
                {
                        device_not_present(esdi);
                        return;
                }

                if (esdi->status_pos)
                        fatal("Status send in progress\n");
                if ((esdi->status & STATUS_IRQ) || esdi->irq_in_progress)
                        fatal("IRQ in progress %02x %i\n", esdi->status, esdi->irq_in_progress);

                esdi->status_len = 6;
                esdi->status_data[0] = CMD_GET_POS_INFO | STATUS_LEN(6) | STATUS_DEVICE_HOST_ADAPTER;
                esdi->status_data[1] = 0x10; /*Zero defect*/
                esdi->status_data[2] = drive->sectors & 0xffff;
                esdi->status_data[3] = drive->sectors >> 16;
                esdi->status_data[4] = drive->tracks;
                esdi->status_data[5] = drive->hpc | (drive->spt << 16);
                
/*                pclog("CMD_GET_DEV_CONFIG %i  %04x %04x %04x %04x %04x %04x\n", drive->sectors,
                                esdi->status_data[0], esdi->status_data[1],
                                esdi->status_data[2], esdi->status_data[3],
                                esdi->status_data[4], esdi->status_data[5]);*/

                esdi->status = STATUS_IRQ | STATUS_STATUS_OUT_FULL;
                esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_SUCCESS;
                esdi->irq_in_progress = 1;
                esdi_set_irq(esdi);
                break;
                        
                case CMD_GET_POS_INFO:
                ESDI_ADAPTER_ONLY();
                if (esdi->status_pos)
                        fatal("Status send in progress\n");
                if ((esdi->status & STATUS_IRQ) || esdi->irq_in_progress)
                        fatal("IRQ in progress %02x %i\n", esdi->status, esdi->irq_in_progress);
                
                esdi->status_len = 5;
                esdi->status_data[0] = CMD_GET_POS_INFO | STATUS_LEN(5) | STATUS_DEVICE_HOST_ADAPTER;
                esdi->status_data[1] = 0xffdd; /*MCA ID*/
                esdi->status_data[2] = esdi->pos_regs[3] | (esdi->pos_regs[2] << 8);
                esdi->status_data[3] = 0xff;
                esdi->status_data[4] = 0xff;

                esdi->status = STATUS_IRQ | STATUS_STATUS_OUT_FULL;
                esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_CMD_COMPLETE_SUCCESS;
                esdi->irq_in_progress = 1;
                esdi_set_irq(esdi);
                break;
                
                case 0x11:
                ESDI_ADAPTER_ONLY();
                switch (esdi->cmd_state)
                {
                        case 0:
                        esdi->sector_pos = 0;
                        esdi->sector_count = esdi->cmd_data[1];
                        if (esdi->sector_count > 16)
                                fatal("Read sector buffer count %04x\n", esdi->cmd_data[1]);
                                
                        esdi->status = STATUS_IRQ | STATUS_CMD_IN_PROGRESS | STATUS_TRANSFER_REQ;
                        esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_DATA_TRANSFER_READY;
                        esdi->irq_in_progress = 1;
                        esdi_set_irq(esdi);
                        
                        esdi->cmd_state = 1;
                        esdi->callback = ESDI_TIME;
                        esdi->data_pos = 0;
                        break;
                        
                        case 1:
                        if (!(esdi->basic_ctrl & CTRL_DMA_ENA))
                        {
                                esdi->callback = ESDI_TIME;
                                return;
                        }
                        while (esdi->sector_pos < esdi->sector_count)
                        {
                                if (!esdi->data_pos)
                                        memcpy(esdi->data, esdi->sector_buffer[esdi->sector_pos++], 512);
                                while (esdi->data_pos < 256)
                                {
                                        int val = dma_channel_write(5, esdi->data[esdi->data_pos]);
                                
                                        if (val == DMA_NODATA)
                                        {
                                                esdi->callback = ESDI_TIME;
                                                return;
                                        }
                                
                                        esdi->data_pos++;
                                }
                                
                                esdi->data_pos = 0;
                        }

                        esdi->status = STATUS_CMD_IN_PROGRESS;
                        esdi->cmd_state = 2;
                        esdi->callback = ESDI_TIME;
                        break;
                        
                        case 2:
                        esdi->status = STATUS_IRQ;
                        esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_CMD_COMPLETE_SUCCESS;
                        esdi->irq_in_progress = 1;
                        esdi_set_irq(esdi);
                        break;
                }
                break;

                case 0x10:
                ESDI_ADAPTER_ONLY();
                switch (esdi->cmd_state)
                {
                        case 0:
                        esdi->sector_pos = 0;
                        esdi->sector_count = esdi->cmd_data[1];
                        if (esdi->sector_count > 16)
                                fatal("Write sector buffer count %04x\n", esdi->cmd_data[1]);
                                
                        esdi->status = STATUS_IRQ | STATUS_CMD_IN_PROGRESS | STATUS_TRANSFER_REQ;
                        esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_DATA_TRANSFER_READY;
                        esdi->irq_in_progress = 1;
                        esdi_set_irq(esdi);
                        
                        esdi->cmd_state = 1;
                        esdi->callback = ESDI_TIME;
                        esdi->data_pos = 0;
                        break;
                        
                        case 1:
                        if (!(esdi->basic_ctrl & CTRL_DMA_ENA))
                        {
                                esdi->callback = ESDI_TIME;
                                return;
                        }
                        while (esdi->sector_pos < esdi->sector_count)
                        {
                                while (esdi->data_pos < 256)
                                {
                                        int val = dma_channel_read(5);
                                
                                        if (val == DMA_NODATA)
                                        {
                                                esdi->callback = ESDI_TIME;
                                                return;
                                        }
                                
                                        esdi->data[esdi->data_pos++] = val & 0xffff;;
                                }

                                memcpy(esdi->sector_buffer[esdi->sector_pos++], esdi->data, 512);
                                esdi->data_pos = 0;
                        }

                        esdi->status = STATUS_CMD_IN_PROGRESS;
                        esdi->cmd_state = 2;
                        esdi->callback = ESDI_TIME;
                        break;
                        
                        case 2:
                        esdi->status = STATUS_IRQ;
                        esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_CMD_COMPLETE_SUCCESS;
                        esdi->irq_in_progress = 1;
                        esdi_set_irq(esdi);
                        break;
                }
                break;
                        
                case 0x12:
                ESDI_ADAPTER_ONLY();
                if (esdi->status_pos)
                        fatal("Status send in progress\n");
                if ((esdi->status & STATUS_IRQ) || esdi->irq_in_progress)
                        fatal("IRQ in progress %02x %i\n", esdi->status, esdi->irq_in_progress);
                
                esdi->status_len = 2;
                esdi->status_data[0] = 0x12 | STATUS_LEN(5) | STATUS_DEVICE_HOST_ADAPTER;
                esdi->status_data[1] = 0;

                esdi->status = STATUS_IRQ | STATUS_STATUS_OUT_FULL;
                esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_CMD_COMPLETE_SUCCESS;
                esdi->irq_in_progress = 1;
                esdi_set_irq(esdi);
                break;

                default:
                fatal("Bad command %02x %i\n", esdi->command, esdi->cmd_dev);

        }
}

static uint8_t esdi_mca_read(int port, void *p)
{
        esdi_t *esdi = (esdi_t *)p;

        return esdi->pos_regs[port & 7];
}

static void esdi_mca_write(int port, uint8_t val, void *p)
{
        esdi_t *esdi = (esdi_t *)p;

        if (port < 0x102)
                return;

        esdi->pos_regs[port & 7] = val;

        io_removehandler(0x3510, 0x0008, esdi_read, esdi_readw, NULL, esdi_write, esdi_writew, NULL, esdi);
        mem_mapping_disable(&esdi->bios_rom.mapping);
        if (esdi->pos_regs[2] & 1)
        {
                io_sethandler(0x3510, 0x0008, esdi_read, esdi_readw, NULL, esdi_write, esdi_writew, NULL, esdi);
                if (!(esdi->pos_regs[3] & 8))
                {
                        mem_mapping_enable(&esdi->bios_rom.mapping);
                        mem_mapping_set_addr(&esdi->bios_rom.mapping, ((esdi->pos_regs[3] & 7) * 0x4000) + 0xc0000, 0x4000);
                }
        }
}

static void loadhd(esdi_t *esdi, int d, const char *fn)
{
        esdi_drive_t *drive = &esdi->drives[d];
        
	if (drive->hdfile == NULL)
        {
		/* Try to open existing hard disk image */
		drive->hdfile = fopen64(fn, "rb+");
		if (drive->hdfile == NULL)
                {
			/* Failed to open existing hard disk image */
			if (errno == ENOENT)
                        {
				/* Failed because it does not exist,
				   so try to create new file */
				drive->hdfile = fopen64(fn, "wb+");
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
        drive->sectors = hdc[d].spt * hdc[d].hpc * hdc[d].tracks;
}

static void *esdi_init()
{
	int i = 0;

        esdi_t *esdi = malloc(sizeof(esdi_t));
        memset(esdi, 0, sizeof(esdi_t));

        rom_init_interleaved(&esdi->bios_rom, "roms/90x8970.bin", "roms/90x8969.bin", 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
        mem_mapping_disable(&esdi->bios_rom.mapping);

	for (i = 0; i < HDC_NUM; i++)
	{
		loadhd(esdi, hdc[i].mfm_channel, hdd_fn[i]);
	}
                
        timer_add(esdi_callback, &esdi->callback, &esdi->callback, esdi);
        
        mca_add(esdi_mca_read, esdi_mca_write, esdi);
        
        esdi->pos_regs[0] = 0xff;
        esdi->pos_regs[1] = 0xdd;

        esdi->in_reset = 1;
        esdi->callback = ESDI_TIME * 50;
        esdi->status = STATUS_BUSY;
        
        return esdi;
}

static void esdi_close(void *p)
{
        esdi_t *esdi = (esdi_t *)p;
        int d;

        for (d = 0; d < 2; d++)
        {
                esdi_drive_t *drive = &esdi->drives[d];
                
                if (drive->hdfile != NULL)
                        fclose(drive->hdfile);
        }
        
        free(esdi);
}

static int esdi_available()
{
        return rom_present("roms/90x8969.bin") && rom_present("roms/90x8970.bin");
}

device_t hdd_esdi_device =
{
        "IBM ESDI Fixed Disk Adapter (MCA)",
        DEVICE_MCA,
        esdi_init,
        esdi_close,
        esdi_available,
        NULL,
        NULL,
        NULL,
        NULL
};
