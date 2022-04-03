/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the IBM PS/2 SCSI controller with
 *		cache for MCA only.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2020 Sarah Walker.
 *		Copyright 2020 TheCollector1995.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_spock.h>

#define SPOCK_U68_1990_ROM		"roms/scsi/ibm/64f4376.bin"
#define SPOCK_U69_1990_ROM		"roms/scsi/ibm/64f4377.bin"

#define SPOCK_U68_1991_ROM		"roms/scsi/ibm/92F2244.U68"
#define SPOCK_U69_1991_ROM		"roms/scsi/ibm/92F2245.U69"

#define SPOCK_TIME (20)

typedef enum
{
        SCSI_STATE_IDLE,
        SCSI_STATE_SELECT,
        SCSI_STATE_SEND_COMMAND,
        SCSI_STATE_END_PHASE
} scsi_state_t;

#pragma pack(push,1)
typedef struct {
        uint16_t pos;
	uint16_t pos1;
	uint16_t pos2;
	uint16_t pos3;
        uint16_t pos4;
	uint16_t pos5;
	uint16_t pos6;
	uint16_t pos7;
	uint16_t pos8;
} get_pos_info_t;

typedef struct {
        uint16_t scb_status;
        uint16_t retry_count;
        uint32_t residual_byte_count;
        uint32_t sg_list_element_addr;
        uint16_t device_dep_status_len;
        uint16_t cmd_status;
        uint16_t error;
        uint16_t reserved;
        uint16_t cache_info_status;
	uint32_t scb_addr;
} get_complete_stat_t;

typedef struct {
        uint32_t sys_buf_addr;
	uint32_t sys_buf_byte_count;
} SGE;

typedef struct {
        uint16_t command;
        uint16_t enable;
        uint32_t lba_addr;
	SGE sge;
        uint32_t term_status_block_addr;
        uint32_t scb_chain_addr;
        uint16_t block_count;
        uint16_t block_length;
} scb_t;
#pragma pack(pop)

typedef struct {
        rom_t bios_rom;

	int bios_ver;
	int irq, irq_inactive;

        uint8_t pos_regs[8];

        uint8_t basic_ctrl;
        uint32_t command;

        uint8_t attention,
		attention_pending;
	int attention_wait;

        uint8_t cir[4],
		cir_pending[4];

        uint8_t irq_status;

        uint32_t scb_addr;

        uint8_t status;

	get_complete_stat_t get_complete_stat;
	get_pos_info_t get_pos_info;

        scb_t scb;
	int adapter_reset;
        int scb_id;
	int adapter_id;

        int cmd_status;
        int cir_status;

        uint8_t pacing;

        uint8_t buf[0x600];

        struct {
                int phys_id;
                int lun_id;
        } dev_id[SCSI_ID_MAX];

	uint8_t last_status, bus;
	uint8_t cdb[12];
	int cdb_len;
	int cdb_id;
	uint32_t data_ptr, data_len;
	uint8_t temp_cdb[12];

        int irq_requests[SCSI_ID_MAX];

        pc_timer_t callback_timer;

	int cmd_timer;

	int scb_state;
	int in_reset;
	int in_invalid;

	uint64_t temp_period;
	double media_period;

	scsi_state_t scsi_state;
} spock_t;

#define CTRL_RESET   (1 << 7)
#define CTRL_DMA_ENA (1 << 1)
#define CTRL_IRQ_ENA (1 << 0)

#define STATUS_CMD_FULL  (1 << 3)
#define STATUS_CMD_EMPTY (1 << 2)
#define STATUS_IRQ       (1 << 1)
#define STATUS_BUSY      (1 << 0)

#define ENABLE_PT (1 << 12)

#define CMD_MASK                 0xff3f
#define CMD_ASSIGN               0x040e
#define CMD_DEVICE_INQUIRY       0x1c0b
#define CMD_DMA_PACING_CONTROL   0x040d
#define CMD_FEATURE_CONTROL      0x040c
#define CMD_GET_POS_INFO         0x1c0a
#define CMD_INVALID_412          0x0412
#define CMD_GET_COMPLETE_STATUS  0x1c07
#define CMD_FORMAT_UNIT	         0x1c16
#define CMD_READ_DATA            0x1c01
#define CMD_READ_DEVICE_CAPACITY 0x1c09
#define CMD_REQUEST_SENSE        0x1c08
#define CMD_RESET                0x0400
#define CMD_SEND_OTHER_SCSI      0x241f
#define CMD_UNKNOWN_1C10         0x1c10
#define CMD_UNKNOWN_1C11         0x1c11
#define CMD_WRITE_DATA           0x1c02
#define CMD_VERIFY               0x1c03

#define IRQ_TYPE_NONE               0x0
#define IRQ_TYPE_SCB_COMPLETE       0x1
#define IRQ_TYPE_SCB_COMPLETE_RETRY 0x5
#define IRQ_TYPE_ADAPTER_HW_FAILURE 0x7
#define IRQ_TYPE_IMM_CMD_COMPLETE   0xa
#define IRQ_TYPE_COMMAND_FAIL       0xc
#define IRQ_TYPE_COMMAND_ERROR      0xe
#define IRQ_TYPE_SW_SEQ_ERROR       0xf
#define IRQ_TYPE_RESET_COMPLETE     0x10


#ifdef ENABLE_SPOCK_LOG
int spock_do_log = ENABLE_SPOCK_LOG;


static void
spock_log(const char *fmt, ...)
{
    va_list ap;

    if (spock_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define spock_log(fmt, ...)
#endif

static void
spock_rethink_irqs(spock_t *scsi)
{
        int irq_pending = 0;
        int c;

        if (!scsi->irq_status) {
                for (c = 0; c < SCSI_ID_MAX; c++) {
                        if (scsi->irq_requests[c] != IRQ_TYPE_NONE) {
                                /* Found IRQ */
                                scsi->irq_status = c | (scsi->irq_requests[c] << 4);
				spock_log("Found IRQ: status = %02x\n", scsi->irq_status);
                                scsi->status |= STATUS_IRQ;
                                irq_pending = 1;
                                break;
                        }
                }
        } else
                irq_pending = 1;

        if (scsi->basic_ctrl & CTRL_IRQ_ENA) {
                if (irq_pending) {
			spock_log("IRQ issued\n");
			scsi->irq_inactive = 0;
                        picint(1 << scsi->irq);
                } else {
                        /* No IRQs pending, clear IRQ state */
			spock_log("IRQ cleared\n");
                        scsi->irq_status = 0;
			scsi->irq_inactive = 1;
                        scsi->status &= ~STATUS_IRQ;
                        picintc(1 << scsi->irq);
                }
        } else {
		spock_log("IRQ disabled\n");
                picintc(1 << scsi->irq);
        }
}

static __inline void
spock_set_irq(spock_t *scsi, int id, int type)
{
        spock_log("spock_set_irq id=%i type=%x %02x\n", id, type, scsi->irq_status);
        scsi->irq_requests[id] = type;
        if (!scsi->irq_status) /* Don't change IRQ status if one is currently being processed */
                spock_rethink_irqs(scsi);
}

static __inline void
spock_clear_irq(spock_t *scsi, int id)
{
	spock_log("spock_clear_irq id=%i\n", id);
        scsi->irq_requests[id] = IRQ_TYPE_NONE;
        spock_rethink_irqs(scsi);
}

static void
spock_add_to_period(spock_t *scsi, int TransferLength)
{
	scsi->temp_period += (uint64_t) TransferLength;
}

static void
spock_write(uint16_t port, uint8_t val, void *p)
{
        spock_t *scsi = (spock_t *)p;

        spock_log("spock_write: port=%04x val=%02x %04x:%04x\n", port, val, CS, cpu_state.pc);

        switch (port & 7) {
                case 0: case 1: case 2: case 3: /*Command Interface Register*/
			scsi->cir_pending[port & 3] = val;
			if (port & 2)
				scsi->cir_status |= 2;
			else
				scsi->cir_status |= 1;
			break;

                case 4: /*Attention Register*/
			scsi->attention_pending = val;
			scsi->attention_wait = 2;
			scsi->status |= STATUS_BUSY;
			break;

                case 5: /*Basic Control Register*/
			if ((scsi->basic_ctrl & CTRL_RESET) && !(val & CTRL_RESET)) {
				spock_log("Spock: SCSI reset and busy\n");
				scsi->in_reset = 1;
				scsi->cmd_timer = SPOCK_TIME * 2;
				scsi->status |= STATUS_BUSY;
			}
			scsi->basic_ctrl = val;
			spock_rethink_irqs(scsi);
			break;
        }
}

static void
spock_writew(uint16_t port, uint16_t val, void *p)
{
        spock_t *scsi = (spock_t *)p;

        switch (port & 7) {
                case 0: /*Command Interface Register*/
			scsi->cir_pending[0] = val & 0xff;
			scsi->cir_pending[1] = val >> 8;
			scsi->cir_status |= 1;
			break;
                case 2: /*Command Interface Register*/
			scsi->cir_pending[2] = val & 0xff;
			scsi->cir_pending[3] = val >> 8;
			scsi->cir_status |= 2;
			break;
        }

        spock_log("spock_writew: port=%04x val=%04x\n", port, val);
}


static uint8_t
spock_read(uint16_t port, void *p)
{
        spock_t *scsi = (spock_t *)p;
        uint8_t temp = 0xff;

        switch (port & 7) {
                case 0: case 1: case 2: case 3: /*Command Interface Register*/
			temp = scsi->cir_pending[port & 3];
			break;

                case 4: /*Attention Register*/
			temp = scsi->attention_pending;
			break;
                case 5: /*Basic Control Register*/
			temp = scsi->basic_ctrl;
			break;
                case 6: /*IRQ status*/
			temp = scsi->irq_status;
			break;
                case 7: /*Basic Status Register*/
			temp = scsi->status;
			spock_log("Cir Status=%d\n", scsi->cir_status);
			if (scsi->cir_status == 0) {
				spock_log("Status Cmd Empty\n");
				temp |= STATUS_CMD_EMPTY;
			}
			else if (scsi->cir_status == 3) {
				spock_log("Status Cmd Full\n");
				temp |= STATUS_CMD_FULL;
			}
			break;
        }

        spock_log("spock_read: port=%04x val=%02x %04x(%05x):%04x %02x\n", port, temp, CS, cs, cpu_state.pc, BH);
        return temp;
}

static uint16_t
spock_readw(uint16_t port, void *p)
{
        spock_t *scsi = (spock_t *)p;
        uint16_t temp = 0xffff;

        switch (port & 7) {
                case 0: /*Command Interface Register*/
			temp = scsi->cir_pending[0] | (scsi->cir_pending[1] << 8);
			break;
                case 2: /*Command Interface Register*/
			temp = scsi->cir_pending[2] | (scsi->cir_pending[3] << 8);
			break;
        }

        spock_log("spock_readw: port=%04x val=%04x\n", port, temp);
        return temp;
}

static void
spock_rd_sge(spock_t *scsi, uint32_t Address, SGE *SG)
{
	dma_bm_read(Address, (uint8_t *)SG, sizeof(SGE), 2);
	spock_add_to_period(scsi, sizeof(SGE));
}

static int
spock_get_len(spock_t *scsi, scb_t *scb)
{
	uint32_t DataToTransfer = 0, i = 0;

	spock_log("Data Buffer write: length %d, pointer 0x%04X\n",
		scsi->data_len, scsi->data_ptr);

	if (!scsi->data_len)
		return(0);

	if (scb->enable & ENABLE_PT) {
		for (i = 0; i < scsi->data_len; i += 8) {
			spock_rd_sge(scsi, scsi->data_ptr + i, &scb->sge);

			DataToTransfer += scb->sge.sys_buf_byte_count;
		}
		return(DataToTransfer);
	} else {
		return(scsi->data_len);
	}
}

static void
spock_process_imm_cmd(spock_t *scsi)
{
	int i;
	int adapter_id, phys_id, lun_id;

        switch (scsi->command & CMD_MASK) {
                case CMD_ASSIGN:
			adapter_id = (scsi->command >> 16) & 15;
			phys_id = (scsi->command >> 20) & 7;
			lun_id = (scsi->command >> 24) & 7;

			if (adapter_id == 15) {
				if (phys_id == 7) /*Device 15 always adapter*/
					spock_set_irq(scsi, scsi->attention & 0x0f, IRQ_TYPE_IMM_CMD_COMPLETE);
				else /*Can not re-assign device 15 (always adapter)*/
					spock_set_irq(scsi, scsi->attention & 0x0f, IRQ_TYPE_COMMAND_FAIL);
			} else {
				if (scsi->command & (1 << 23)) {
					spock_log("Assign: adapter id=%d\n", adapter_id);
					scsi->dev_id[adapter_id].phys_id = -1;
					spock_set_irq(scsi, scsi->attention & 0x0f, IRQ_TYPE_IMM_CMD_COMPLETE);
				} else {
					if (phys_id != scsi->adapter_id) {
						scsi->dev_id[adapter_id].phys_id = phys_id;
						scsi->dev_id[adapter_id].lun_id = lun_id;
						spock_log("Assign: adapter dev=%x scsi ID=%i LUN=%i\n", adapter_id, scsi->dev_id[adapter_id].phys_id, scsi->dev_id[adapter_id].lun_id);
						spock_set_irq(scsi, scsi->attention & 0x0f, IRQ_TYPE_IMM_CMD_COMPLETE);
					} else { /*Can not assign adapter*/
						spock_log("Assign: PUN=%d, cannot assign adapter\n", phys_id);
						spock_set_irq(scsi, scsi->attention & 0x0f, IRQ_TYPE_COMMAND_FAIL);
					}
				}
			}
			break;

		case CMD_DMA_PACING_CONTROL:
			scsi->pacing = scsi->cir[2];
			spock_log("Pacing control: %i\n", scsi->pacing);
			spock_set_irq(scsi, scsi->attention & 0x0f, IRQ_TYPE_IMM_CMD_COMPLETE);
			break;

                case CMD_FEATURE_CONTROL:
			spock_log("Feature control: timeout=%is d-rate=%i\n", (scsi->command >> 16) & 0x1fff, scsi->command >> 29);
			spock_set_irq(scsi, scsi->attention & 0x0f, IRQ_TYPE_IMM_CMD_COMPLETE);
			break;

                case CMD_INVALID_412:
			spock_log("Invalid 412\n");
			spock_set_irq(scsi, scsi->attention & 0x0f, IRQ_TYPE_IMM_CMD_COMPLETE);
			break;

                case CMD_RESET:
			spock_log("Reset Command\n");
			if ((scsi->attention & 0x0f) == 0x0f) { /*Adapter reset*/
				for (i = 0; i < 8; i++)
					scsi_device_reset(&scsi_devices[scsi->bus][i]);
				spock_log("Adapter Reset\n");

				if (!scsi->adapter_reset && scsi->bios_ver) /*The early 1990 bios must have its boot drive
															 set to ID 6 according https://www.ardent-tool.com/IBM_SCSI/SCSI-A.html */
					scsi->adapter_reset = 1;
				else
					scsi->adapter_reset = 0;

				scsi->scb_state = 0;
			}
			spock_set_irq(scsi, scsi->attention & 0x0f, IRQ_TYPE_IMM_CMD_COMPLETE);
			break;

                default:
			fatal("scsi_callback: Bad command %02x\n", scsi->command);
			break;
        }
}

static void
spock_execute_cmd(spock_t *scsi, scb_t *scb)
{
	int c;
	int old_scb_state;

	if (scsi->in_reset) {
		spock_log("Reset type = %d\n", scsi->in_reset);

		scsi->status &= ~STATUS_BUSY;
		scsi->irq_status = 0;

		for (c = 0; c < SCSI_ID_MAX; c++)
			spock_clear_irq(scsi, c);

                if (scsi->in_reset == 1) {
			scsi->basic_ctrl |= CTRL_IRQ_ENA;
			spock_set_irq(scsi, 0xf, IRQ_TYPE_RESET_COMPLETE);
		} else
                        spock_set_irq(scsi, 0xf, IRQ_TYPE_RESET_COMPLETE);

		/*Reset device mappings*/
		for (c = 0; c < 7; c++) {
			scsi->dev_id[c].phys_id = c;
			scsi->dev_id[c].lun_id = 0;
		}
		for (; c < (SCSI_ID_MAX-1); c++)
			scsi->dev_id[c].phys_id = -1;

		scsi->in_reset = 0;
		return;
	}

	if (scsi->in_invalid) {
		spock_log("Invalid command\n");
		spock_set_irq(scsi, scsi->attention & 0x0f, IRQ_TYPE_COMMAND_ERROR);
		scsi->in_invalid = 0;
		return;
	}

	spock_log("SCB State = %d\n", scsi->scb_state);

	do
	{
		old_scb_state = scsi->scb_state;

		switch (scsi->scb_state) {
			case 0: /* Idle */
				break;

			case 1: /* Select */
				if (scsi->dev_id[scsi->scb_id].phys_id == -1) {
					uint16_t term_stat_block_addr7 = (0xe << 8) | 0;
					uint16_t term_stat_block_addr8 = (0xa << 8) | 0;

					spock_log("Start failed, SCB ID = %d\n", scsi->scb_id);
					spock_set_irq(scsi, scsi->scb_id, IRQ_TYPE_COMMAND_FAIL);
					scsi->scb_state = 0;
					dma_bm_write(scb->term_status_block_addr + 0x7*2, (uint8_t *)&term_stat_block_addr7, 2, 2);
					dma_bm_write(scb->term_status_block_addr + 0x8*2, (uint8_t *)&term_stat_block_addr8, 2, 2);
					break;
				}

				dma_bm_read(scsi->scb_addr, (uint8_t *)&scb->command, 2, 2);
				dma_bm_read(scsi->scb_addr + 2, (uint8_t *)&scb->enable, 2, 2);
				dma_bm_read(scsi->scb_addr + 4, (uint8_t *)&scb->lba_addr, 4, 2);
				dma_bm_read(scsi->scb_addr + 8, (uint8_t *)&scb->sge.sys_buf_addr, 4, 2);
				dma_bm_read(scsi->scb_addr + 12, (uint8_t *)&scb->sge.sys_buf_byte_count, 4, 2);
				dma_bm_read(scsi->scb_addr + 16, (uint8_t *)&scb->term_status_block_addr, 4, 2);
				dma_bm_read(scsi->scb_addr + 20, (uint8_t *)&scb->scb_chain_addr, 4, 2);
				dma_bm_read(scsi->scb_addr + 24, (uint8_t *)&scb->block_count, 2, 2);
				dma_bm_read(scsi->scb_addr + 26, (uint8_t *)&scb->block_length, 2, 2);

				spock_log("SCB : \n"
				      "  Command = %04x\n"
				      "  Enable = %04x\n"
				      "  LBA addr = %08x\n"
				      "  System buffer addr = %08x\n"
				      "  System buffer byte count = %08x\n"
				      "  Terminate status block addr = %08x\n"
				      "  SCB chain address = %08x\n"
				      "  Block count = %04x\n"
				      "  Block length = %04x\n"
				      "  SCB id = %d\n",
					scb->command, scb->enable, scb->lba_addr,
					scb->sge.sys_buf_addr, scb->sge.sys_buf_byte_count,
					scb->term_status_block_addr, scb->scb_chain_addr,
					scb->block_count, scb->block_length, scsi->scb_id);

				switch (scb->command & CMD_MASK) {
					case CMD_GET_COMPLETE_STATUS:
					{
						spock_log("Get Complete Status\n");
						get_complete_stat_t *get_complete_stat = &scsi->get_complete_stat;

						get_complete_stat->scb_status = 0x201;
						get_complete_stat->retry_count = 0;
						get_complete_stat->residual_byte_count = 0;
						get_complete_stat->sg_list_element_addr = 0;
						get_complete_stat->device_dep_status_len = 0x0c;
						get_complete_stat->cmd_status = scsi->cmd_status << 8;
						get_complete_stat->error = 0;
						get_complete_stat->reserved = 0;
						get_complete_stat->cache_info_status = 0;
						get_complete_stat->scb_addr = scsi->scb_addr;

						dma_bm_write(scb->sge.sys_buf_addr, (uint8_t *)&get_complete_stat->scb_status, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 2, (uint8_t *)&get_complete_stat->retry_count, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 4, (uint8_t *)&get_complete_stat->residual_byte_count, 4, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 8, (uint8_t *)&get_complete_stat->sg_list_element_addr, 4, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 12, (uint8_t *)&get_complete_stat->device_dep_status_len, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 14, (uint8_t *)&get_complete_stat->cmd_status, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 16, (uint8_t *)&get_complete_stat->error, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 18, (uint8_t *)&get_complete_stat->reserved, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 20, (uint8_t *)&get_complete_stat->cache_info_status, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 22, (uint8_t *)&get_complete_stat->scb_addr, 4, 2);
						scsi->scb_state = 3;
					}
					break;

					case CMD_UNKNOWN_1C10:
						spock_log("Unknown 1C10\n");
						dma_bm_read(scb->sge.sys_buf_addr, scsi->buf, scb->sge.sys_buf_byte_count, 2);
						scsi->scb_state = 3;
						break;

					case CMD_UNKNOWN_1C11:
						spock_log("Unknown 1C11\n");
						dma_bm_write(scb->sge.sys_buf_addr, scsi->buf, scb->sge.sys_buf_byte_count, 2);
						scsi->scb_state = 3;
						break;

					case CMD_GET_POS_INFO:
					{
						spock_log("Get POS Info\n");
						get_pos_info_t *get_pos_info = &scsi->get_pos_info;

						get_pos_info->pos = 0x8eff;
						get_pos_info->pos1 = scsi->pos_regs[3] | (scsi->pos_regs[2] << 8);
						get_pos_info->pos2 = 0x0e | (scsi->pos_regs[4] << 8);
						get_pos_info->pos3 = 1 << 12;
						get_pos_info->pos4 = (7 << 8) | 8;
						get_pos_info->pos5 = (16 << 8) | scsi->pacing;
						get_pos_info->pos6 = (30 << 8) | 1;
						get_pos_info->pos7 = 0;
						get_pos_info->pos8 = 0;

						dma_bm_write(scb->sge.sys_buf_addr, (uint8_t *)&get_pos_info->pos, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 2, (uint8_t *)&get_pos_info->pos1, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 4, (uint8_t *)&get_pos_info->pos2, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 6, (uint8_t *)&get_pos_info->pos3, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 8, (uint8_t *)&get_pos_info->pos4, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 10, (uint8_t *)&get_pos_info->pos5, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 12, (uint8_t *)&get_pos_info->pos6, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 14, (uint8_t *)&get_pos_info->pos7, 2, 2);
						dma_bm_write(scb->sge.sys_buf_addr + 16, (uint8_t *)&get_pos_info->pos8, 2, 2);
						scsi->scb_state = 3;
					}
					break;

					case CMD_DEVICE_INQUIRY:
						if (scsi->adapter_reset) {
							scsi->cdb_id = scsi->scb_id;
						} else {
							scsi->cdb_id = scsi->dev_id[scsi->scb_id].phys_id;
						}
						spock_log("Device Inquiry, ID=%d\n", scsi->cdb_id);
						scsi->cdb[0] = GPCMD_INQUIRY;
						scsi->cdb[1] = scsi->dev_id[scsi->scb_id].lun_id << 5; /*LUN*/
						scsi->cdb[2] = 0; /*Page code*/
						scsi->cdb[3] = 0;
						scsi->cdb[4] = scb->sge.sys_buf_byte_count; /*Allocation length*/
						scsi->cdb[5] = 0; /*Control*/
						scsi->cdb_len = 6;
						scsi->data_ptr = scb->sge.sys_buf_addr;
						scsi->data_len = scb->sge.sys_buf_byte_count;
						scsi->scsi_state = SCSI_STATE_SELECT;
						scsi->scb_state = 2;
						return;

					case CMD_SEND_OTHER_SCSI:
						if (scsi->adapter_reset) {
							scsi->cdb_id = scsi->scb_id;
						} else {
							scsi->cdb_id = scsi->dev_id[scsi->scb_id].phys_id;
						}
						spock_log("Send Other SCSI, ID=%d\n", scsi->cdb_id);
						dma_bm_read(scsi->scb_addr + 0x18, scsi->cdb, 12, 2);
						scsi->cdb[1] = (scsi->cdb[1] & 0x1f) | (scsi->dev_id[scsi->scb_id].lun_id << 5); /*Patch correct LUN into command*/
						scsi->cdb_len = (scb->lba_addr & 0xff) ? (scb->lba_addr & 0xff) : 6;
						scsi->scsi_state = SCSI_STATE_SELECT;
						scsi->scb_state = 2;
						return;

					case CMD_READ_DEVICE_CAPACITY:
						if (scsi->adapter_reset)
							scsi->cdb_id = scsi->scb_id;
						else
							scsi->cdb_id = scsi->dev_id[scsi->scb_id].phys_id;
						spock_log("Device Capacity, ID=%d\n", scsi->cdb_id);
						scsi->cdb[0] = GPCMD_READ_CDROM_CAPACITY;
						scsi->cdb[1] = scsi->dev_id[scsi->scb_id].lun_id << 5; /*LUN*/
						scsi->cdb[2] = 0; /*LBA*/
						scsi->cdb[3] = 0;
						scsi->cdb[4] = 0;
						scsi->cdb[5] = 0;
						scsi->cdb[6] = 0; /*Reserved*/
						scsi->cdb[7] = 0;
						scsi->cdb[8] = 0;
						scsi->cdb[9] = 0; /*Control*/
						scsi->cdb_len = 10;
						scsi->scsi_state = SCSI_STATE_SELECT;
						scsi->scb_state = 2;
						return;

					case CMD_READ_DATA:
						spock_log("Device Read Data\n");
						scsi->cdb[0] = GPCMD_READ_10;
						scsi->cdb[1] = scsi->dev_id[scsi->scb_id].lun_id << 5; /*LUN*/
						scsi->cdb[2] = (scb->lba_addr >> 24) & 0xff; /*LBA*/
						scsi->cdb[3] = (scb->lba_addr >> 16) & 0xff;
						scsi->cdb[4] = (scb->lba_addr >> 8) & 0xff;
						scsi->cdb[5] = scb->lba_addr & 0xff;
						scsi->cdb[6] = 0; /*Reserved*/
						scsi->cdb[7] = (scb->block_count >> 8) & 0xff;
						scsi->cdb[8] = scb->block_count & 0xff;
						scsi->cdb[9] = 0; /*Control*/
						scsi->cdb_len = 10;
						scsi->cdb_id = scsi->dev_id[scsi->scb_id].phys_id;
						scsi->scsi_state = SCSI_STATE_SELECT;
						scsi->scb_state = 2;
						return;

					case CMD_WRITE_DATA:
						spock_log("Device Write Data\n");
						scsi->cdb[0] = GPCMD_WRITE_10;
						scsi->cdb[1] = scsi->dev_id[scsi->scb_id].lun_id << 5; /*LUN*/
						scsi->cdb[2] = (scb->lba_addr >> 24) & 0xff; /*LBA*/
						scsi->cdb[3] = (scb->lba_addr >> 16) & 0xff;
						scsi->cdb[4] = (scb->lba_addr >> 8) & 0xff;
						scsi->cdb[5] = scb->lba_addr & 0xff;
						scsi->cdb[6] = 0; /*Reserved*/
						scsi->cdb[7] = (scb->block_count >> 8) & 0xff;
						scsi->cdb[8] = scb->block_count & 0xff;
						scsi->cdb[9] = 0; /*Control*/
						scsi->cdb_len = 10;
						scsi->cdb_id = scsi->dev_id[scsi->scb_id].phys_id;
						scsi->scsi_state = SCSI_STATE_SELECT;
						scsi->scb_state = 2;
						return;

					case CMD_VERIFY:
						spock_log("Device Verify\n");
						scsi->cdb[0] = GPCMD_VERIFY_10;
						scsi->cdb[1] = scsi->dev_id[scsi->scb_id].lun_id << 5; /*LUN*/
						scsi->cdb[2] = (scb->lba_addr >> 24) & 0xff; /*LBA*/
						scsi->cdb[3] = (scb->lba_addr >> 16) & 0xff;
						scsi->cdb[4] = (scb->lba_addr >> 8) & 0xff;
						scsi->cdb[5] = scb->lba_addr & 0xff;
						scsi->cdb[6] = 0; /*Reserved*/
						scsi->cdb[7] = (scb->block_count >> 8) & 0xff;
						scsi->cdb[8] = scb->block_count & 0xff;
						scsi->cdb[9] = 0; /*Control*/
						scsi->cdb_len = 10;
						scsi->cdb_id = scsi->dev_id[scsi->scb_id].phys_id;
						scsi->data_len = 0;
						scsi->scsi_state = SCSI_STATE_SELECT;
						scsi->scb_state = 2;
						return;

					case CMD_REQUEST_SENSE:
						if (scsi->adapter_reset)
							scsi->cdb_id = scsi->scb_id;
						else
							scsi->cdb_id = scsi->dev_id[scsi->scb_id].phys_id;
						spock_log("Device Request Sense, ID=%d\n", scsi->cdb_id);
						scsi->cdb[0] = GPCMD_REQUEST_SENSE;
						scsi->cdb[1] = scsi->dev_id[scsi->scb_id].lun_id << 5; /*LUN*/
						scsi->cdb[2] = 0;
						scsi->cdb[3] = 0;
						scsi->cdb[4] = scb->sge.sys_buf_byte_count; /*Allocation length*/
						scsi->cdb[5] = 0;
						scsi->cdb_len = 6;
						scsi->scsi_state = SCSI_STATE_SELECT;
						scsi->scb_state = 2;
						return;
				}
				break;

			case 2: /* Wait */
				if (scsi->scsi_state == SCSI_STATE_IDLE && scsi_device_present(&scsi_devices[scsi->bus][scsi->cdb_id])) {
					if (scsi->last_status == SCSI_STATUS_OK) {
						scsi->scb_state = 3;
						spock_log("Status is Good on device ID %d, timer = %i\n", scsi->cdb_id, scsi->cmd_timer);
					} else if (scsi->last_status == SCSI_STATUS_CHECK_CONDITION) {
						uint16_t term_stat_block_addr7 = (0xc << 8) | 2;
						uint16_t term_stat_block_addr8 = 0x20;
						uint16_t term_stat_block_addrb = scsi->scb_addr & 0xffff;
						uint16_t term_stat_block_addrc = scsi->scb_addr >> 16;

						spock_set_irq(scsi, scsi->scb_id, IRQ_TYPE_COMMAND_FAIL);
						scsi->scb_state = 0;
						spock_log("Status Check Condition on device ID %d\n", scsi->cdb_id);
						dma_bm_write(scb->term_status_block_addr + 0x7*2, (uint8_t *)&term_stat_block_addr7, 2, 2);
						dma_bm_write(scb->term_status_block_addr + 0x8*2, (uint8_t *)&term_stat_block_addr8, 2, 2);
						dma_bm_write(scb->term_status_block_addr + 0xb*2, (uint8_t *)&term_stat_block_addrb, 2, 2);
						dma_bm_write(scb->term_status_block_addr + 0xc*2, (uint8_t *)&term_stat_block_addrc, 2, 2);
					}
				} else if (scsi->scsi_state == SCSI_STATE_IDLE && !scsi_device_present(&scsi_devices[scsi->bus][scsi->cdb_id])) {
					uint16_t term_stat_block_addr7 = (0xc << 8) | 2;
					uint16_t term_stat_block_addr8 = 0x10;
					spock_set_irq(scsi, scsi->scb_id, IRQ_TYPE_COMMAND_FAIL);
					scsi->scb_state = 0;
					dma_bm_write(scb->term_status_block_addr + 0x7*2, (uint8_t *)&term_stat_block_addr7, 2, 2);
					dma_bm_write(scb->term_status_block_addr + 0x8*2, (uint8_t *)&term_stat_block_addr8, 2, 2);
				}
				break;

			case 3: /* Complete */
				if (scb->enable & 1) {
					scsi->scb_state = 1;
					scsi->scb_addr = scb->scb_chain_addr;
					spock_log("Next SCB - %08x\n", scsi->scb_addr);
				} else {
					spock_set_irq(scsi, scsi->scb_id, IRQ_TYPE_SCB_COMPLETE);
					scsi->scb_state = 0;
					spock_log("Complete SCB\n");
				}
				break;
		}
	} while (scsi->scb_state != old_scb_state);
}

static void
spock_process_scsi(spock_t *scsi, scb_t *scb)
{
	int c;
	double p;
	scsi_device_t *sd;

	switch (scsi->scsi_state) {
		case SCSI_STATE_IDLE:
			break;

		case SCSI_STATE_SELECT:
			spock_log("Selecting ID %d\n", scsi->cdb_id);
			if ((scsi->cdb_id != (uint8_t)-1) && scsi_device_present(&scsi_devices[scsi->bus][scsi->cdb_id])) {
				scsi->scsi_state = SCSI_STATE_SEND_COMMAND;
				spock_log("Device selected at ID %i\n", scsi->cdb_id);
			} else {
				spock_log("Device selection failed at ID %i\n", scsi->cdb_id);
				scsi->scsi_state = SCSI_STATE_IDLE;
				if (!scsi->cmd_timer) {
					spock_log("Callback to reset\n");
					scsi->cmd_timer = 1;
				}
				spock_add_to_period(scsi, 1);
			}
			break;

		case SCSI_STATE_SEND_COMMAND:
			sd = &scsi_devices[scsi->bus][scsi->cdb_id];
			memset(scsi->temp_cdb, 0x00, 12);

			if (scsi->cdb_len < 12) {
				memcpy(scsi->temp_cdb, scsi->cdb,
				       scsi->cdb_len);
				spock_add_to_period(scsi, scsi->cdb_len);
			} else {
				memcpy(scsi->temp_cdb, scsi->cdb,
				       12);
				spock_add_to_period(scsi, 12);
			}

			scsi->data_ptr = scb->sge.sys_buf_addr;
			scsi->data_len = scb->sge.sys_buf_byte_count;

			if (scb->enable & 0x400)
				sd->buffer_length = -1;
			else
				sd->buffer_length = spock_get_len(scsi, scb);

			scsi_device_command_phase0(sd, scsi->temp_cdb);
			spock_log("SCSI ID %i: Current CDB[0] = %02x, LUN = %i, data len = %i, max len = %i, phase val = %02x\n", scsi->cdb_id, scsi->temp_cdb[0], scsi->temp_cdb[1] >> 5, sd->buffer_length, spock_get_len(scsi, scb), sd->phase);

			if (sd->phase != SCSI_PHASE_STATUS && sd->buffer_length > 0) {
				p = scsi_device_get_callback(sd);
				if (p <= 0.0)
					spock_add_to_period(scsi, sd->buffer_length);
				else
					scsi->media_period += p;

				if (scb->enable & ENABLE_PT) {
					int32_t buflen = sd->buffer_length;
					int sg_pos = 0;
					uint32_t DataTx = 0;
					uint32_t Address;

					if (scb->sge.sys_buf_byte_count > 0) {
						for (c = 0; c < scsi->data_len; c += 8) {
							spock_rd_sge(scsi, scsi->data_ptr + c, &scb->sge);

							Address = scb->sge.sys_buf_addr;
							DataTx = MIN((int) scb->sge.sys_buf_byte_count, buflen);

							if ((sd->phase == SCSI_PHASE_DATA_IN) && DataTx) {
								spock_log("Writing S/G segment %i: length %i, pointer %08X\n", c, DataTx, Address);
								dma_bm_write(Address, &sd->sc->temp_buffer[sg_pos], DataTx, 2);
							} else if ((sd->phase == SCSI_PHASE_DATA_OUT) && DataTx) {
								spock_log("Reading S/G segment %i: length %i, pointer %08X\n", c, DataTx, Address);
								dma_bm_read(Address, &sd->sc->temp_buffer[sg_pos], DataTx, 2);
							}

							sg_pos += scb->sge.sys_buf_byte_count;
							buflen -= scb->sge.sys_buf_byte_count;

							if (buflen < 0)
								buflen = 0;
						}
					}
				} else {
					spock_log("Normal Transfer\n");
					if (sd->phase == SCSI_PHASE_DATA_IN) {
						dma_bm_write(scsi->data_ptr, sd->sc->temp_buffer, MIN(sd->buffer_length, (int)scsi->data_len), 2);
					} else if (sd->phase == SCSI_PHASE_DATA_OUT)
						dma_bm_read(scsi->data_ptr, sd->sc->temp_buffer, MIN(sd->buffer_length, (int)scsi->data_len), 2);
				}

				scsi_device_command_phase1(sd);
			}
			scsi->last_status = sd->status;
			scsi->scsi_state = SCSI_STATE_END_PHASE;
			break;

		case SCSI_STATE_END_PHASE:
			scsi->scsi_state = SCSI_STATE_IDLE;

			spock_log("State to idle, cmd timer %d\n", scsi->cmd_timer);
			if (!scsi->cmd_timer) {
				scsi->cmd_timer = 1;
			}
			spock_add_to_period(scsi, 1);
			break;
	}
}

static void
spock_callback(void *priv)
{
	double period;
        spock_t *scsi = (spock_t *)priv;
	scb_t *scb = &scsi->scb;

	scsi->temp_period = 0;
	scsi->media_period = 0.0;

	if (scsi->cmd_timer) {
		scsi->cmd_timer--;
		if (!scsi->cmd_timer) {
			spock_execute_cmd(scsi, scb);
		}
	}

        if (scsi->attention_wait &&
                (scsi->scb_state == 0 || (scsi->attention_pending & 0xf0) == 0xe0)) {
		scsi->attention_wait--;
		if (!scsi->attention_wait) {
			scsi->attention = scsi->attention_pending;
			scsi->status &= ~STATUS_BUSY;
			scsi->cir[0] = scsi->cir_pending[0];
			scsi->cir[1] = scsi->cir_pending[1];
			scsi->cir[2] = scsi->cir_pending[2];
			scsi->cir[3] = scsi->cir_pending[3];
			scsi->cir_status = 0;

			switch (scsi->attention >> 4) {
				case 1: /*Immediate command*/
					scsi->cmd_status = 0x0a;
					scsi->command = scsi->cir[0] | (scsi->cir[1] << 8) | (scsi->cir[2] << 16) | (scsi->cir[3] << 24);
					switch (scsi->command & CMD_MASK) {
						case CMD_ASSIGN:
						case CMD_DMA_PACING_CONTROL:
						case CMD_FEATURE_CONTROL:
						case CMD_INVALID_412:
						case CMD_RESET:
							spock_process_imm_cmd(scsi);
							break;
					}
					break;

                                case 3: case 4: case 0x0f: /*Start SCB*/
					scsi->cmd_status = 1;
					scsi->scb_addr = scsi->cir[0] | (scsi->cir[1] << 8) | (scsi->cir[2] << 16) | (scsi->cir[3] << 24);
					scsi->scb_id = scsi->attention & 0x0f;
					scsi->cmd_timer = SPOCK_TIME * 2;
					spock_log("Start SCB at ID = %d\n", scsi->scb_id);
					scsi->scb_state = 1;
					break;

                                case 5: /*Invalid*/
                                case 0x0a: /*Invalid*/
					scsi->in_invalid = 1;
					scsi->cmd_timer = SPOCK_TIME * 2;
					break;

                                case 0x0e: /*EOI*/
					scsi->irq_status = 0;
					spock_clear_irq(scsi, scsi->attention & 0xf);
					break;
			}
		}
	}

	spock_process_scsi(scsi, scb);

	period = 0.2 * ((double) scsi->temp_period);
	timer_on(&scsi->callback_timer, (scsi->media_period + period + 10.0), 0);
	spock_log("Temporary period: %lf us (%" PRIi64 " periods)\n", scsi->callback_timer.period, scsi->temp_period);
}

static void
spock_mca_write(int port, uint8_t val, void *priv)
{
        spock_t *scsi = (spock_t *)priv;

        if (port < 0x102)
                return;

        io_removehandler((((scsi->pos_regs[2] >> 1) & 7) * 8) + 0x3540, 0x0008, spock_read, spock_readw, NULL, spock_write, spock_writew, NULL, scsi);
        mem_mapping_disable(&scsi->bios_rom.mapping);

        scsi->pos_regs[port & 7] = val;

	scsi->adapter_id = (scsi->pos_regs[3] & 0xe0) >> 5;

        if (scsi->pos_regs[2] & 1) {
                io_sethandler((((scsi->pos_regs[2] >> 1) & 7) * 8) + 0x3540, 0x0008, spock_read, spock_readw, NULL, spock_write, spock_writew, NULL, scsi);
		if ((scsi->pos_regs[2] >> 4) == 0x0f)
			mem_mapping_disable(&scsi->bios_rom.mapping);
		else {
			mem_mapping_set_addr(&scsi->bios_rom.mapping, ((scsi->pos_regs[2] >> 4) * 0x2000) + 0xc0000, 0x8000);
		}
        }
}

static uint8_t
spock_mca_read(int port, void *priv)
{
        spock_t *scsi = (spock_t *)priv;

        return scsi->pos_regs[port & 7];
}

static uint8_t
spock_mca_feedb(void *priv)
{
	spock_t *scsi = (spock_t *)priv;

	return (scsi->pos_regs[2] & 0x01);
}

static void
spock_mca_reset(void *priv)
{
        spock_t *scsi = (spock_t *)priv;
	int i;

	scsi->in_reset = 2;
	scsi->cmd_timer = SPOCK_TIME * 50;
	scsi->status = STATUS_BUSY;
	scsi->scsi_state = SCSI_STATE_IDLE;
	scsi->scb_state = 0;
	scsi->in_invalid = 0;
	scsi->attention_wait = 0;
	scsi->basic_ctrl = 0;

	/* Reset all devices on controller reset. */
	for (i = 0; i < 8; i++)
		scsi_device_reset(&scsi_devices[scsi->bus][i]);

	scsi->adapter_reset = 0;
}

static void *
spock_init(const device_t *info)
{
	int c;
	spock_t *scsi = malloc(sizeof(spock_t));
	memset(scsi, 0x00, sizeof(spock_t));

	scsi->bus = scsi_get_bus();

	scsi->irq = 14;

	scsi->bios_ver = device_get_config_int("bios_ver");

	switch (scsi->bios_ver) {
		case 1:
			rom_init_interleaved(&scsi->bios_rom, SPOCK_U68_1991_ROM, SPOCK_U69_1991_ROM,
				0xc8000, 0x8000, 0x7fff, 0x4000, MEM_MAPPING_EXTERNAL);
			break;
		case 0:
			rom_init_interleaved(&scsi->bios_rom, SPOCK_U68_1990_ROM, SPOCK_U69_1990_ROM,
				0xc8000, 0x8000, 0x7fff, 0x4000, MEM_MAPPING_EXTERNAL);
			break;
	}

	mem_mapping_disable(&scsi->bios_rom.mapping);

        scsi->pos_regs[0] = 0xff;
        scsi->pos_regs[1] = 0x8e;
	mca_add(spock_mca_read, spock_mca_write, spock_mca_feedb, spock_mca_reset, scsi);

	scsi->in_reset = 2;
	scsi->cmd_timer = SPOCK_TIME * 50;
	scsi->status = STATUS_BUSY;

        for (c = 0; c < (SCSI_ID_MAX-1); c++) {
                scsi->dev_id[c].phys_id = -1;
	}

	scsi->dev_id[SCSI_ID_MAX-1].phys_id = scsi->adapter_id;

	timer_add(&scsi->callback_timer, spock_callback, scsi, 1);
	scsi->callback_timer.period = 10.0;
	timer_set_delay_u64(&scsi->callback_timer, (uint64_t) (scsi->callback_timer.period * ((double) TIMER_USEC)));

	return scsi;
}

static void
spock_close(void *p)
{
        spock_t *scsi = (spock_t *)p;

	if (scsi) {
		free(scsi);
		scsi = NULL;
	}
}

static int
spock_available(void)
{
        return rom_present(SPOCK_U68_1991_ROM) && rom_present(SPOCK_U69_1991_ROM) &&
			rom_present(SPOCK_U68_1990_ROM) && rom_present(SPOCK_U69_1990_ROM);
}

static const device_config_t spock_rom_config[] = {
// clang-format off
    {
        .name = "bios_ver",
		.description = "BIOS Version",
		.type = CONFIG_SELECTION,
		.default_string = "",
		.default_int = 1,
		.file_filter = "",
		.spinner = { 0 },
        .selection = {
            { .description = "1991 BIOS (>1GB)", .value = 1 },
            { .description = "1990 BIOS",        .value = 0 },
            { .description = ""                             }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

const device_t spock_device = {
    .name = "IBM PS/2 SCSI Adapter (Spock)",
    .internal_name = "spock",
    .flags = DEVICE_MCA,
    .local = 0,
    .init = spock_init,
    .close = spock_close,
    .reset = NULL,
    { .available = spock_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = spock_rom_config
};
