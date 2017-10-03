/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the NCR 5380 series of SCSI Host Adapters
 *		made by NCR. These controllers were designed for
 *		the ISA bus.
 *
 * Version:	@(#)scsi_ncr5380.c	1.0.0	2017/10/04
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		TheCollector1995, <mariogplayer@gmail.com>
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include "../ibm.h"
#include "../io.h"
#include "../mca.h"
#include "../mem.h"
#include "../mca.h"
#include "../rom.h"
#include "../nvr.h"
#include "../dma.h"
#include "../pic.h"
#include "../timer.h"
#include "../device.h"
#include "../win/plat_thread.h"
#include "scsi.h"
#include "scsi_device.h"
#include "scsi_ncr5380.h"

#define NCR_CURDATA		0 /* current SCSI data (read only) */
#define NCR_OUTDATA 	0 /* output data (write only) */
#define NCR_INITCOMMAND 1 /* initiator command (read/write) */
#define NCR_MODE		2 /* mode (read/write) */
#define NCR_TARGETCMD	3 /* target command (read/write) */
#define NCR_SELENABLE	4 /* select enable (write only) */
#define NCR_BUSSTATUS	4 /* bus status (read only) */
#define NCR_STARTDMA	5 /* start DMA send (write only) */
#define NCR_BUSANDSTAT	5 /* bus and status (read only) */
#define NCR_DMATARGET	6 /* DMA target (write only) */
#define NCR_INPUTDATA	6 /* input data (read only) */
#define NCR_DMAINIRECV	7 /* DMA initiator receive (write only) */
#define NCR_RESETPARITY	7 /* reset parity/interrupt (read only) */

#define POLL_TIME_US 10
#define MAX_BYTES_TRANSFERRED_PER_POLL 50
/*10us poll period with 50 bytes transferred per poll = 5MB/sec*/

#pragma pack(push,1)
typedef struct ncr5380_t
{	
	uint8_t icr;
	uint8_t mode;
	uint8_t tcr;
	uint8_t ser;
	uint8_t isr;

	int target_bsy;
	int target_req;
	uint8_t target_id;
	
	uint8_t bus_status;
	
	int dma_mode;
	
	scsi_bus_t bus;
} ncr5380_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct lcs6821n_t
{
        rom_t bios_rom;
        
        mem_mapping_t mapping;
        
        uint8_t block_count;
        int block_count_loaded;
        
        uint8_t status_ctrl;
        
        uint8_t buffer[0x80];
        int buffer_pos;
        int buffer_host_pos;
        
        uint8_t int_ram[0x40];        
        uint8_t ext_ram[0x600];
        
        ncr5380_t ncr;
        
        int ncr5380_dma_enabled;
        
        int dma_callback;
        int dma_enabled;
        
        int ncr_busy;
} lcs6821n_t;
#pragma pack(pop)

#define ICR_DBP             0x01
#define ICR_ATN             0x02
#define ICR_SEL             0x04
#define ICR_BSY             0x08
#define ICR_ACK             0x10
#define ICR_ARB_LOST        0x20
#define ICR_ARB_IN_PROGRESS 0x40

#define MODE_ARBITRATE 0x01
#define MODE_DMA       0x02
#define MODE_MONITOR_BUSY  0x04
#define MODE_ENA_EOP_INT   0x08

#define STATUS_ACK 0x01
#define STATUS_BUSY_ERROR 0x04
#define STATUS_INT 0x10
#define STATUS_DRQ 0x40
#define STATUS_END_OF_DMA 0x80

#define TCR_IO  0x01
#define TCR_CD  0x02
#define TCR_MSG 0x04
#define TCR_REQ 0x08
#define TCR_LAST_BYTE_SENT 0x80

enum
{
        DMA_IDLE = 0,
        DMA_SEND,
        DMA_TARGET_RECEIVE,
        DMA_INITIATOR_RECEIVE
};

#if ENABLE_NCR5380_LOG
int ncr5380_do_log = ENABLE_NCR5380_LOG;
#endif


static void
ncr5380_log(const char *fmt, ...)
{
#if ENABLE_NCR5380_LOG
    va_list ap;

    if (ncr5380_do_log) {
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
    }
#endif
}

static void ncr53c400_dma_changed(void *p, int mode, int enable)
{
	lcs6821n_t *scsi = (lcs6821n_t *)p;
	
	scsi->ncr5380_dma_enabled = (mode && enable);
	
	scsi->dma_enabled = (scsi->ncr5380_dma_enabled && scsi->block_count_loaded);
}

static uint32_t get_bus_host(ncr5380_t *ncr)
{
	uint32_t bus_host = 0;
        
	if (ncr->icr & ICR_DBP)
		bus_host |= BUS_DBP;
	if (ncr->icr & ICR_SEL)
		bus_host |= BUS_SEL;
	if (ncr->icr & ICR_ATN)
		bus_host |= BUS_ATN;
	if (ncr->tcr & TCR_IO)
		bus_host |= BUS_IO;
	if (ncr->tcr & TCR_CD)
		bus_host |= BUS_CD;
	if (ncr->tcr & TCR_MSG)
		bus_host |= BUS_MSG;
	if (ncr->tcr & TCR_REQ)
		bus_host |= BUS_REQ;
	if (ncr->icr & ICR_BSY)
		bus_host |= BUS_BSY;
	if (ncr->icr & ICR_ACK)
		bus_host |= BUS_ACK;
	if (ncr->mode & MODE_ARBITRATE)
		bus_host |= BUS_ARB;
	return bus_host | BUS_SETDATA(SCSI_BufferLength);
}

static void 
ncr5380_write(uint16_t port, uint8_t val, void *priv)
{
	lcs6821n_t *scsi = (lcs6821n_t *)priv;
	ncr5380_t *ncr = &scsi->ncr;
	int bus_host = 0;
        
	//ncr5380_log("ncr5380_write: addr=%06x val=%02x %04x:%04x\n", port, val, CS,cpu_state.pc);
	switch (port & 7)
	{
		case 0: /*Output data register*/
			SCSI_BufferLength = val;
			break;

		case 1: /*Initiator Command Register*/
			if ((val & (ICR_BSY | ICR_SEL)) == (ICR_BSY | ICR_SEL) &&
				(ncr->icr & (ICR_BSY | ICR_SEL)) == ICR_SEL)
			{
				uint8_t temp = SCSI_BufferLength & 0x7f;
				
				ncr->target_id = -1;
				while (temp)
				{
					temp >>= 1;
					ncr->target_id++;
				}
				
				ncr5380_log("Select - target ID = %i, temp data %x\n", ncr->target_id, temp);
			}			

			ncr->icr = val;
			break;

		case 2: /*Mode register*/
			if ((val & MODE_ARBITRATE) && !(ncr->mode & MODE_ARBITRATE))
			{
				ncr->icr &= ~ICR_ARB_LOST;
				ncr->icr |=  ICR_ARB_IN_PROGRESS;
			}
				
			ncr->mode = val;
			ncr53c400_dma_changed(scsi, ncr->dma_mode, ncr->mode & MODE_DMA);
			if (!(ncr->mode & MODE_DMA))
			{
					ncr->tcr &= ~TCR_LAST_BYTE_SENT;
					ncr->isr &= ~STATUS_END_OF_DMA;
					ncr->dma_mode = DMA_IDLE;
			}
			break;

		case 3: /*Target Command Register*/
			ncr->tcr = val;
			break;
			
		case 4: /*Select Enable Register*/
			ncr->ser = val;
			break;

		case 5: /*Start DMA Send*/
			ncr->dma_mode = DMA_SEND;
			ncr53c400_dma_changed(scsi, ncr->dma_mode, ncr->mode & MODE_DMA);
			break;

		case 7: /*Start DMA Initiator Receive*/
			ncr->dma_mode = DMA_INITIATOR_RECEIVE;
			ncr53c400_dma_changed(scsi, ncr->dma_mode, ncr->mode & MODE_DMA);
			break;

		default:
		pclog("Bad NCR5380 write %06x %02x\n", port, val);
		break;
	}
	
	bus_host = get_bus_host(ncr);

	scsi_bus_update(&ncr->bus, bus_host);
}

static uint8_t 
ncr5380_read(uint16_t port, void *priv)
{
	lcs6821n_t *scsi = (lcs6821n_t *)priv;
	ncr5380_t *ncr = &scsi->ncr;
	uint32_t bus = 0;
	uint8_t temp = 0xff;

	switch (port & 7)
	{
		case 0: /*Current SCSI Data*/
			if (ncr->icr & ICR_DBP)
			{
				temp = SCSI_BufferLength;
			}
			else
			{	
				bus = scsi_bus_read(&ncr->bus);
				temp = BUS_GETDATA(bus);
			}
			break;
		case 1: /*Initiator Command Register*/
			temp = ncr->icr;
			break;
		case 2: /*Mode Register*/
			temp = ncr->mode;
			break;
		case 3: /*Target Command Register*/
			temp = ncr->tcr;
			break;

		case 4: /*Current SCSI Bus Status*/
			temp = 0;
			bus = scsi_bus_read(&ncr->bus);
			temp |= (bus & 0xff);
			break;

		case 5: /*Bus and Status Register*/
			temp = 0;
			
			bus = get_bus_host(ncr);
			if (scsi_bus_match(&ncr->bus, bus))
				temp |= 8;
			bus = scsi_bus_read(&ncr->bus);

			if (bus & BUS_ACK)
				temp |= STATUS_ACK;
			if ((bus & BUS_REQ) && (ncr->mode & MODE_DMA))
				temp |= STATUS_DRQ;
			if ((bus & BUS_REQ) && (ncr->mode & MODE_DMA))
			{
				int bus_state = 0;
				if (bus & BUS_IO)
					bus_state |= TCR_IO;
				if (bus & BUS_CD)
					bus_state |= TCR_CD;
				if (bus & BUS_MSG)
					bus_state |= TCR_MSG;
				if ((ncr->tcr & 7) != bus_state)
					ncr->isr |= STATUS_INT;
			}
			if (!(bus & BUS_BSY) && (ncr->mode & MODE_MONITOR_BUSY))
					temp |= STATUS_BUSY_ERROR;
			temp |= (ncr->isr & (STATUS_INT | STATUS_END_OF_DMA));
			break;

		case 7: /*Reset Parity/Interrupt*/
			ncr->isr &= ~STATUS_INT;
			break;

		default:
			ncr5380_log("Bad NCR5380 read %06x\n", port);
			break;
	}
	
	//ncr5380_log("ncr5380_read: addr=%06x temp=%02x pc=%04x:%04x\n", port, temp, CS,cpu_state.pc);
	return temp;
}

#define CTRL_DATA_DIR (1 << 6)

#define STATUS_BUFFER_NOT_READY (1 << 2)
#define STATUS_53C80_ACCESSIBLE (1 << 7)

static uint8_t 
lcs6821n_read(uint32_t addr, void *p)
{
	lcs6821n_t *scsi = (lcs6821n_t *)p;
	uint8_t temp = 0xff;
	
	addr &= 0x3fff;
	//ncr5380_log("lcs6821n_read %08x\n", addr);
	
	if (addr < 0x2000)
		temp = scsi->bios_rom.rom[addr & 0x1fff];
	else if (addr < 0x3800)
		temp = 0xff;
	else if (addr >= 0x3a00)
		temp = scsi->ext_ram[addr - 0x3a00];
	else switch (addr & 0x3f80)
	{
		case 0x3800:
			//ncr5380_log("Read intRAM %02x %02x\n", addr & 0x3f, scsi->int_ram[addr & 0x3f]);
			temp = scsi->int_ram[addr & 0x3f];
			break;
		
		case 0x3880:
			//ncr5380_log("Read 53c80 %04x\n", addr);
			temp = ncr5380_read(addr, scsi);
			break;
		
		case 0x3900:
			//ncr5380_log(" Read 3900 %i %02x\n", scsi->buffer_host_pos, scsi->status_ctrl);
			if (scsi->buffer_host_pos >= 128 || !(scsi->status_ctrl & CTRL_DATA_DIR))
					temp = 0xff;
			else
			{
					temp = scsi->buffer[scsi->buffer_host_pos++];
					
					if (scsi->buffer_host_pos == 128)
							scsi->status_ctrl |= STATUS_BUFFER_NOT_READY;
			}
			break;
		
		case 0x3980:
			switch (addr)
			{
					case 0x3980: /*Status*/
						temp = scsi->status_ctrl;// | 0x80;
						if (!scsi->ncr_busy)
								temp |= STATUS_53C80_ACCESSIBLE;
						break;
					case 0x3981: /*Block counter register*/
						temp = scsi->block_count;
						break;
					case 0x3982: /*Switch register read*/
						temp = 0xff;
						break;
					case 0x3983:
						temp = 0xff;
						break;
			}
			break;
}

	//if (addr >= 0x3880) ncr5380_log("lcs6821n_read: addr=%05x val=%02x\n", addr, temp);
                
	return temp;
}

static void 
lcs6821n_write(uint32_t addr, uint8_t val, void *p)
{
	lcs6821n_t *scsi = (lcs6821n_t *)p;

	addr &= 0x3fff;

	//ncr5380_log("lcs6821n_write: addr=%05x val=%02x %04x:%04x  %i %02x\n", addr, val, CS,cpu_state.pc,  scsi->buffer_host_pos, scsi->status_ctrl);
                
	if (addr >= 0x3a00)
		scsi->ext_ram[addr - 0x3a00] = val;
	else switch (addr & 0x3f80)
	{
		case 0x3800:
			//ncr5380_log("Write intram %02x %02x\n", addr & 0x3f, val);
			scsi->int_ram[addr & 0x3f] = val;
			break;
                
		case 0x3880:
			//ncr5380_log("Write 53c80 %04x %02x\n", addr, val);
			ncr5380_write(addr, val, scsi);
			break;
                
		case 0x3900:
			if (!(scsi->status_ctrl & CTRL_DATA_DIR) && scsi->buffer_host_pos < 128)
			{
					scsi->buffer[scsi->buffer_host_pos++] = val;
					if (scsi->buffer_host_pos == 128)
					{
							scsi->status_ctrl |= STATUS_BUFFER_NOT_READY;
							scsi->ncr_busy = 1;
					}
			}
			break;
                
			case 0x3980:
				switch (addr)        
				{
						case 0x3980: /*Control*/
							if ((val & CTRL_DATA_DIR) && !(scsi->status_ctrl & CTRL_DATA_DIR))
							{
									scsi->buffer_host_pos = 128;
									scsi->status_ctrl |= STATUS_BUFFER_NOT_READY;
							}
							else if (!(val & CTRL_DATA_DIR) && (scsi->status_ctrl & CTRL_DATA_DIR))
							{
									scsi->buffer_host_pos = 0;
									scsi->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
							}
							scsi->status_ctrl = (scsi->status_ctrl & 0x87) | (val & 0x78);
							break;
						case 0x3981: /*Block counter register*/
							scsi->block_count = val;
							scsi->block_count_loaded = 1;
							scsi->dma_enabled = (scsi->ncr5380_dma_enabled && scsi->block_count_loaded);
							if (scsi->status_ctrl & CTRL_DATA_DIR)
							{
									scsi->buffer_host_pos = 128;
									scsi->status_ctrl |= STATUS_BUFFER_NOT_READY;
							}
							else
							{
									scsi->buffer_host_pos = 0;
									scsi->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
							}
							break;
				}
				break;
        }
}

static uint8_t 
t130b_read(uint32_t addr, void *p)
{
        lcs6821n_t *scsi = (lcs6821n_t *)p;
        uint8_t temp = 0xff;
        addr &= 0x3fff;
        
        if (addr < 0x1800)
                temp = scsi->bios_rom.rom[addr & 0x1fff];
        else if (addr < 0x1880)
                temp = scsi->ext_ram[addr & 0x7f];

        return temp;
}
static void 
t130b_write(uint32_t addr, uint8_t val, void *p)
{
        lcs6821n_t *scsi = (lcs6821n_t *)p;

        addr &= 0x3fff;

        if (addr >= 0x1800 && addr < 0x1880)
                scsi->ext_ram[addr & 0x7f] = val;
}

static uint8_t 
t130b_in(uint16_t port, void *p)
{
	lcs6821n_t *scsi = (lcs6821n_t *)p;
	uint8_t temp = 0xff;
	
	switch (port & 0xf)
	{
			case 0x0: case 0x1: case 0x2: case 0x3:
			temp = lcs6821n_read((port & 7) | 0x3980, scsi);
			break;
									
			case 0x4: case 0x5:
			temp = lcs6821n_read(0x3900, scsi);
			break;

			case 0x8: case 0x9: case 0xa: case 0xb:
			case 0xc: case 0xd: case 0xe: case 0xf:
			temp = ncr5380_read(port, scsi);
			break;
	}
	return temp;
}
static void 
t130b_out(uint16_t port, uint8_t val, void *p)
{
	lcs6821n_t *scsi = (lcs6821n_t *)p;

	switch (port & 0xf)
	{
			case 0x0: case 0x1: case 0x2: case 0x3:
			lcs6821n_write((port & 7) | 0x3980, val, scsi);
			break;
									
			case 0x4: case 0x5:
			lcs6821n_write(0x3900, val, scsi);
			break;

			case 0x8: case 0x9: case 0xa: case 0xb:
			case 0xc: case 0xd: case 0xe: case 0xf:
			ncr5380_write(port, val, scsi);
			break;
	}
}

static void ncr53c400_dma_callback(void *p)
{
	lcs6821n_t *scsi = (lcs6821n_t *)p;
	ncr5380_t *ncr = &scsi->ncr;
	int c;
	int bytes_transferred = 0;

	scsi->dma_callback += POLL_TIME_US;
                
	switch (scsi->ncr.dma_mode)
	{
		case DMA_SEND:
			if (scsi->status_ctrl & CTRL_DATA_DIR)
			{
				ncr5380_log("DMA_SEND with DMA direction set wrong\n");
				break;
			}
		 
			if (!(scsi->status_ctrl & STATUS_BUFFER_NOT_READY))
			{
				break;
			}
					
			if (!scsi->block_count_loaded)
			{
				break;
			}
                
			while (bytes_transferred < MAX_BYTES_TRANSFERRED_PER_POLL)
			{
				int bus;
				uint8_t data;
				
				for (c = 0; c < 10; c++)
				{
					uint8_t status = scsi_bus_read(&ncr->bus);
				
					if (status & BUS_REQ)
						break;
				}
				if (c == 10)
				{
					break;
				}

				/*Data ready*/
				data = scsi->buffer[scsi->buffer_pos];
				bus = get_bus_host(ncr) & ~BUS_DATAMASK;
				bus |= BUS_SETDATA(data);
					
				scsi_bus_update(&ncr->bus, bus | BUS_ACK);
				scsi_bus_update(&ncr->bus, bus & ~BUS_ACK);

				scsi->buffer_pos++;
				bytes_transferred++;
					
				if (scsi->buffer_pos == 128)
				{
					scsi->buffer_pos = 0;
					scsi->buffer_host_pos = 0;
					scsi->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
					scsi->block_count = (scsi->block_count - 1) & 255;
					scsi->ncr_busy = 0;
					if (!scsi->block_count)
					{
							scsi->block_count_loaded = 0;
							scsi->dma_enabled = 0;

							ncr->tcr |= TCR_LAST_BYTE_SENT;
							ncr->isr |= STATUS_END_OF_DMA;                                        
							if (ncr->mode & MODE_ENA_EOP_INT)
									ncr->isr |= STATUS_INT;
					}
					break;
				}
			}
			break;

		case DMA_INITIATOR_RECEIVE:
			if (!(scsi->status_ctrl & CTRL_DATA_DIR))
			{
				ncr5380_log("DMA_INITIATOR_RECEIVE with DMA direction set wrong\n");
				break;
			}
			
			if (!(scsi->status_ctrl & STATUS_BUFFER_NOT_READY))
				break;
			
			if (!scsi->block_count_loaded)
				break;
                
			while (bytes_transferred < MAX_BYTES_TRANSFERRED_PER_POLL)
			{
				int bus;
				uint8_t temp;
					
				for (c = 0; c < 10; c++)
				{
					uint8_t status = scsi_bus_read(&ncr->bus);
			
					if (status & BUS_REQ)
						break;
				}
				if (c == 10)
					break;

				/*Data ready*/
        		bus = scsi_bus_read(&ncr->bus);
        		temp = BUS_GETDATA(bus);
				bus = get_bus_host(ncr);
	
				scsi_bus_update(&ncr->bus, bus | BUS_ACK);
				scsi_bus_update(&ncr->bus, bus & ~BUS_ACK);

				scsi->buffer[scsi->buffer_pos++] = temp;
				bytes_transferred++;
                                                
				if (scsi->buffer_pos == 128)
				{
						scsi->buffer_pos = 0;
						scsi->buffer_host_pos = 0;
						scsi->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
						scsi->block_count = (scsi->block_count - 1) & 255;
						if (!scsi->block_count)
						{
								scsi->block_count_loaded = 0;
								scsi->dma_enabled = 0;
								
								ncr->isr |= STATUS_END_OF_DMA;
								if (ncr->mode & MODE_ENA_EOP_INT)
										ncr->isr |= STATUS_INT;
						}
						break;
				}
			}
			break;
                
			default:
                pclog("DMA callback bad mode %i\n", scsi->ncr.dma_mode);
                break;
        }

        {
			int bus = scsi_bus_read(&ncr->bus);

			if (!(bus & BUS_BSY) && (ncr->mode & MODE_MONITOR_BUSY))
			{
				ncr->mode &= ~MODE_DMA;
				ncr->dma_mode = DMA_IDLE;
				ncr53c400_dma_changed(scsi, ncr->dma_mode, ncr->mode & MODE_DMA);
			}
        }
}

static void *scsi_53c400_init(wchar_t *bios_fn)
{
	lcs6821n_t *scsi = malloc(sizeof(lcs6821n_t));
	memset(scsi, 0, sizeof(lcs6821n_t));
	
	SCSI_BufferLength = 0;
	
	rom_init(&scsi->bios_rom, bios_fn, 0xdc000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
	mem_mapping_disable(&scsi->bios_rom.mapping);

	mem_mapping_add(&scsi->mapping, 0xdc000, 0x4000, 
					lcs6821n_read, NULL, NULL,
					lcs6821n_write, NULL, NULL,
                    scsi->bios_rom.rom, 0, scsi);

	scsi->status_ctrl = STATUS_BUFFER_NOT_READY;
	scsi->buffer_host_pos = 128;

	timer_add(ncr53c400_dma_callback, &scsi->dma_callback, &scsi->dma_enabled, scsi);
	
	return scsi;
}

static void *scsi_lcs6821n_init(void)
{
	return scsi_53c400_init(L"roms/scsi/ncr5380/Longshine LCS-6821N - BIOS version 1.04.bin");
}
static void *scsi_rt1000b_init(void)
{
	return scsi_53c400_init(L"roms/scsi/ncr5380/Rancho_RT1000_RTBios_version_8.10R.bin");
}

static void *scsi_t130b_init(void)
{
	lcs6821n_t *scsi = malloc(sizeof(lcs6821n_t));
	memset(scsi, 0, sizeof(lcs6821n_t));

	SCSI_BufferLength = 0;
	
	rom_init(&scsi->bios_rom, L"roms/scsi/ncr5380/trantor_t130b_bios_v2.14.bin", 0xdc000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

	mem_mapping_add(&scsi->mapping, 0xdc000, 0x4000, 
				t130b_read, NULL, NULL,
				t130b_write, NULL, NULL,
				scsi->bios_rom.rom, 0, scsi);
	io_sethandler(0x0350, 0x0010,
					t130b_in, NULL, NULL,
					t130b_out, NULL, NULL,
					scsi);
					
	scsi->status_ctrl = STATUS_BUFFER_NOT_READY;
	scsi->buffer_host_pos = 128;

	timer_add(ncr53c400_dma_callback, &scsi->dma_callback, &scsi->dma_enabled, scsi);
	
	return scsi;
}


static void 
scsi_53c400_close(void *p)
{
	lcs6821n_t *scsi = (lcs6821n_t *)p;

	free(scsi);
}

static int scsi_lcs6821n_available()
{
	return rom_present(L"roms/scsi/ncr5380/Longshine LCS-6821N - BIOS version 1.04.bin");
}

static int scsi_rt1000b_available()
{
	return rom_present(L"roms/scsi/ncr5380/Rancho_RT1000_RTBios_version_8.10R.bin");
}

static int scsi_t130b_available()
{
	return rom_present(L"roms/scsi/ncr5380/trantor_t130b_bios_v2.14.bin");
}

device_t scsi_lcs6821n_device =
{
        "Longshine LCS-6821N (SCSI)",
        0,
        scsi_lcs6821n_init,
        scsi_53c400_close,
        scsi_lcs6821n_available,
        NULL,
        NULL,
        NULL,
        NULL
};

device_t scsi_rt1000b_device =
{
        "Ranco RT1000B (SCSI)",
        0,
        scsi_rt1000b_init,
        scsi_53c400_close,
        scsi_rt1000b_available,
        NULL,
        NULL,
        NULL,
        NULL
};


device_t scsi_t130b_device =
{
        "Trantor T130B (SCSI)",
        0,
        scsi_t130b_init,
        scsi_53c400_close,
        scsi_t130b_available,
        NULL,
        NULL,
        NULL,
        NULL
};
