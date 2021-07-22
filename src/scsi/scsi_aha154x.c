/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the AHA-154x series of SCSI Host Adapters
 *		made by Adaptec, Inc. These controllers were designed for
 *		the ISA bus.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Original Buslogic version by SA1988 and Miran Grca.
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
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
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/mca.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/plat.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/isapnp.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_aha154x.h>
#include <86box/scsi_x54x.h>


enum {
    AHA_154xA,
    AHA_154xB,
    AHA_154xC,
    AHA_154xCF,
    AHA_154xCP,
    AHA_1640
};



#define CMD_WRITE_EEPROM 0x22		/* UNDOC: Write EEPROM */
#define CMD_READ_EEPROM	0x23		/* UNDOC: Read EEPROM */
#define CMD_SHADOW_RAM	0x24		/* UNDOC: BIOS shadow ram */
#define CMD_BIOS_MBINIT	0x25		/* UNDOC: BIOS mailbox initialization */
#define CMD_MEMORY_MAP_1 0x26		/* UNDOC: Memory Mapper */
#define CMD_MEMORY_MAP_2 0x27		/* UNDOC: Memory Mapper */
#define CMD_EXTBIOS     0x28		/* UNDOC: return extended BIOS info */
#define CMD_MBENABLE    0x29		/* set mailbox interface enable */
#define CMD_BIOS_SCSI	0x82		/* start ROM BIOS SCSI command */


uint16_t	aha_ports[] = {
    0x0330, 0x0334, 0x0230, 0x0234,
    0x0130, 0x0134, 0x0000, 0x0000
};

static uint8_t *aha1542cp_pnp_rom = NULL;


#pragma pack(push,1)
typedef struct {
    uint8_t	CustomerSignature[20];
    uint8_t	uAutoRetry;
    uint8_t	uBoardSwitches;
    uint8_t	uChecksum;
    uint8_t	uUnknown;
    addr24	BIOSMailboxAddress;
} aha_setup_t;
#pragma pack(pop)


#ifdef ENABLE_AHA154X_LOG
int aha_do_log = ENABLE_AHA154X_LOG;


static void
aha_log(const char *fmt, ...)
{
    va_list ap;

    if (aha_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define aha_log(fmt, ...)
#endif


/*
 * Write data to the BIOS space.
 *
 * AHA-1542C's and up have a feature where they map a 128-byte
 * RAM space into the ROM BIOS' address space, and then use it
 * as working memory. This function implements the writing to
 * that memory.
 *
 * We enable/disable this memory through AHA command 0x24.
 */
static void
aha_mem_write(uint32_t addr, uint8_t val, void *priv)
{
    x54x_t *dev = (x54x_t *)priv;

    addr &= 0x3fff;

    if ((addr >= dev->rom_shram) && (dev->shram_mode & 1))
	dev->shadow_ram[addr & (dev->rom_shramsz - 1)] = val;
}


static uint8_t
aha_mem_read(uint32_t addr, void *priv)
{
    x54x_t *dev = (x54x_t *)priv;
    rom_t *rom = &dev->bios;

    addr &= 0x3fff;

    if ((addr >= dev->rom_shram) && (dev->shram_mode & 2))
	return dev->shadow_ram[addr & (dev->rom_shramsz - 1)];

    return(rom->rom[addr]);
}


static uint8_t
aha154x_shram(x54x_t *dev, uint8_t cmd)
{
    /* If not supported, give up. */
    if (dev->rom_shram == 0x0000) return(0x04);

    /* Bit 0 = Shadow RAM write enable;
       Bit 1 = Shadow RAM read enable. */
    dev->shram_mode = cmd;

    /* Firmware expects 04 status. */
    return(0x04);
}


static void
aha_eeprom_save(x54x_t *dev)
{
    FILE *f;

    f = nvr_fopen(dev->nvr_path, "wb");
    if (f)
    {
	fwrite(dev->nvr, 1, NVR_SIZE, f);
	fclose(f);
	f = NULL;
    }
}


static uint8_t
aha154x_eeprom(x54x_t *dev, uint8_t cmd,uint8_t arg,uint8_t len,uint8_t off,uint8_t *bufp)
{
    uint8_t r = 0xff;
    int c;

    aha_log("%s: EEPROM cmd=%02x, arg=%02x len=%d, off=%02x\n",
				dev->name, cmd, arg, len, off);

    /* Only if we can handle it.. */
    if (dev->nvr == NULL) return(r);

    if (cmd == 0x22) {
	/* Write data to the EEPROM. */
	for (c = 0; c < len; c++)
		dev->nvr[(off + c) & 0xff] = bufp[c];
	r = 0;

	aha_eeprom_save(dev);

	if (dev->type == AHA_154xCF) {
		if (dev->fdc_address > 0) {
			fdc_remove(dev->fdc);
			fdc_set_base(dev->fdc, (dev->nvr[0] & EE0_ALTFLOP) ? 0x370 : 0x3f0);
		}
	}
    }

    if (cmd == 0x23) {
	/* Read data from the EEPROM. */
	for (c = 0; c < len; c++)
		bufp[c] = dev->nvr[(off + c) & 0xff];
	r = len;
    }

    return(r);
}


/* Map either the main or utility (Select) ROM into the memory space. */
static uint8_t
aha154x_mmap(x54x_t *dev, uint8_t cmd)
{
    aha_log("%s: MEMORY cmd=%02x\n", dev->name, cmd);

    switch(cmd) {
	case 0x26:
		/* Disable the mapper, so, set ROM1 active. */
		dev->bios.rom = dev->rom1;
		break;

	case 0x27:
		/* Enable the mapper, so, set ROM2 active. */
		dev->bios.rom = dev->rom2;
		break;
    }

    return(0);
}


static uint8_t
aha_get_host_id(void *p)
{
    x54x_t *dev = (x54x_t *)p;

    return dev->nvr[0] & 0x07;
}


static uint8_t
aha_get_irq(void *p)
{
    x54x_t *dev = (x54x_t *)p;

    return (dev->nvr[1] & 0x07) + 9;
}


static uint8_t
aha_get_dma(void *p)
{
    x54x_t *dev = (x54x_t *)p;

    return (dev->nvr[1] >> 4) & 0x07;
}


static uint8_t
aha_cmd_is_fast(void *p)
{
    x54x_t *dev = (x54x_t *)p;

    if (dev->Command == CMD_BIOS_SCSI)
	return 1;
    else
	return 0;
}


static uint8_t
aha_fast_cmds(void *p, uint8_t cmd)
{
    x54x_t *dev = (x54x_t *)p;

    if (cmd == CMD_BIOS_SCSI) {
	dev->BIOSMailboxReq++;
	return 1;
    }

    return 0;
}


static uint8_t
aha_param_len(void *p)
{
    x54x_t *dev = (x54x_t *)p;

    switch (dev->Command) {
	case CMD_BIOS_MBINIT:
		/* Same as 0x01 for AHA. */
		return sizeof(MailboxInit_t);
		break;

	case CMD_SHADOW_RAM:
		return 1;
		break;	

	case CMD_WRITE_EEPROM:
		return 35;
		break;

	case CMD_READ_EEPROM:
		return 3;

	case CMD_MBENABLE:
		return 2;

	case 0x39:
		return 3;

	case 0x40:
		return 2;

	default:
		return 0;
    }
}


static uint8_t
aha_cmds(void *p)
{
    x54x_t *dev = (x54x_t *)p;
    MailboxInit_t *mbi;

    if (! dev->CmdParamLeft) {
	aha_log("Running Operation Code 0x%02X\n", dev->Command);
	switch (dev->Command) {
		case CMD_WRITE_EEPROM:	/* write EEPROM */
			/* Sent by CF BIOS. */
			dev->DataReplyLeft =
			    aha154x_eeprom(dev,
					   dev->Command,
					   dev->CmdBuf[0],
					   dev->CmdBuf[1],
					   dev->CmdBuf[2],
					   &(dev->CmdBuf[3]));
			if (dev->DataReplyLeft == 0xff) {
				dev->DataReplyLeft = 0;
				dev->Status |= STAT_INVCMD;
			}
			break;

		case CMD_READ_EEPROM: /* read EEPROM */
			/* Sent by CF BIOS. */
			dev->DataReplyLeft =
			    aha154x_eeprom(dev,
					   dev->Command,
					   dev->CmdBuf[0],
					   dev->CmdBuf[1],
					   dev->CmdBuf[2],
					   dev->DataBuf);
			if (dev->DataReplyLeft == 0xff) {
				dev->DataReplyLeft = 0;
				dev->Status |= STAT_INVCMD;
			}
			break;

		case CMD_SHADOW_RAM: /* Shadow RAM */
			/*
			 * For AHA1542CF, this is the command
			 * to play with the Shadow RAM.  BIOS
			 * gives us one argument (00,02,03)
			 * and expects a 0x04 back in the INTR
			 * register.  --FvK
			 */
			/* dev->Interrupt = aha154x_shram(dev,val); */
			dev->Interrupt = aha154x_shram(dev, dev->CmdBuf[0]);
			break;

		case CMD_BIOS_MBINIT: /* BIOS Mailbox Initialization */
			/* Sent by CF BIOS. */
			dev->flags |= X54X_MBX_24BIT;

			mbi = (MailboxInit_t *)dev->CmdBuf;

			dev->BIOSMailboxInit = 1;
			dev->BIOSMailboxCount = mbi->Count;
			dev->BIOSMailboxOutAddr = ADDR_TO_U32(mbi->Address);

			aha_log("Initialize BIOS Mailbox: MBO=0x%08lx, %d entries at 0x%08lx\n",
				dev->BIOSMailboxOutAddr,
				mbi->Count,
				ADDR_TO_U32(mbi->Address));

			dev->Status &= ~STAT_INIT;
			dev->DataReplyLeft = 0;
			break;

		case CMD_MEMORY_MAP_1:	/* AHA memory mapper */
		case CMD_MEMORY_MAP_2:	/* AHA memory mapper */
			/* Sent by CF BIOS. */
			dev->DataReplyLeft =
			    aha154x_mmap(dev, dev->Command);
			break;

		case CMD_EXTBIOS: /* Return extended BIOS information */
			dev->DataBuf[0] = 0x08;
			dev->DataBuf[1] = dev->Lock;
			dev->DataReplyLeft = 2;
			break;
					
		case CMD_MBENABLE: /* Mailbox interface enable Command */
			dev->DataReplyLeft = 0;
			if (dev->CmdBuf[1] == dev->Lock) {
				if (dev->CmdBuf[0] & 1) {
					dev->Lock = 1;
				} else {
					dev->Lock = 0;
				}
			}
			break;

		case 0x2C:	/* Detect termination status */
			/* Bits 7,6 are termination status and must be 1,0 for the BIOS to work. */
			dev->DataBuf[0] = 0x40;
			dev->DataReplyLeft = 1;
			break;

		case 0x2D:	/* ???? - Returns two bytes according to the microcode */
			dev->DataBuf[0] = 0x00;
			dev->DataBuf[0] = 0x00;
			dev->DataReplyLeft = 2;
			break;

		case 0x33:	/* Send the SCSISelect code decompressor program */
			if (dev->cmd_33_len == 0x0000) {
				/* If we are on a controller without this command, return invalid command. */
				dev->DataReplyLeft = 0;
				dev->Status |= STAT_INVCMD;
				break;
			}

			/* We have to send (decompressor program length + 2 bytes of little endian size). */
			dev->DataReplyLeft = dev->cmd_33_len + 2;
			memset(dev->DataBuf, 0x00, dev->DataReplyLeft);
			dev->DataBuf[0] = dev->cmd_33_len & 0xff;
			dev->DataBuf[1] = (dev->cmd_33_len >> 8) & 0xff;
			memcpy(&(dev->DataBuf[2]), dev->cmd_33_buf, dev->cmd_33_len);
			break;

		case 0x39:	/* Receive 3 bytes: address high, address low, byte to write to that address. */
			/* Since we are not running the actual microcode, just log the received values
			   (if logging is enabled) and break. */
			aha_log("aha_cmds(): Command 0x39: %02X -> %02X%02X\n",
				dev->CmdBuf[2], dev->CmdBuf[0], dev->CmdBuf[1]);
			break;

		case 0x40:	/* Receive 2 bytes: address high, address low, then return one byte from that
				   address. */
			aha_log("aha_cmds(): Command 0x40: %02X%02X\n",
				dev->CmdBuf[0], dev->CmdBuf[1]);
			dev->DataReplyLeft = 1;
			dev->DataBuf[0] = 0xff;
			break;

		default:
			dev->DataReplyLeft = 0;
			dev->Status |= STAT_INVCMD;
			break;
	}
    }

    return 0;
}


static void
aha_setup_data(void *p)
{
    x54x_t *dev = (x54x_t *)p;
    ReplyInquireSetupInformation *ReplyISI;
    aha_setup_t *aha_setup;

    ReplyISI = (ReplyInquireSetupInformation *)dev->DataBuf;
    aha_setup = (aha_setup_t *)ReplyISI->VendorSpecificData;

    ReplyISI->fSynchronousInitiationEnabled = dev->sync & 1;
    ReplyISI->fParityCheckingEnabled = dev->parity & 1;

    U32_TO_ADDR(aha_setup->BIOSMailboxAddress, dev->BIOSMailboxOutAddr);
    aha_setup->uChecksum = 0xA3;
    aha_setup->uUnknown = 0xC2;
}


static void
aha_do_bios_mail(x54x_t *dev)
{
    dev->MailboxIsBIOS = 1;

    if (!dev->BIOSMailboxCount) {
	aha_log("aha_do_bios_mail(): No BIOS Mailboxes\n");
	return;
    }

    /* Search for a filled mailbox - stop if we have scanned all mailboxes. */
    for (dev->BIOSMailboxOutPosCur = 0; dev->BIOSMailboxOutPosCur < dev->BIOSMailboxCount; dev->BIOSMailboxOutPosCur++) {
	if (x54x_mbo_process(dev))
		break;
    }
}


static void
aha_callback(void *p)
{
    x54x_t *dev = (x54x_t *)p;

    if (dev->BIOSMailboxInit && dev->BIOSMailboxReq)
	aha_do_bios_mail(dev);
}


static uint8_t
aha_mca_read(int port, void *priv)
{
    x54x_t *dev = (x54x_t *)priv;

    return(dev->pos_regs[port & 7]);
}


static void
aha_mca_write(int port, uint8_t val, void *priv)
{
    x54x_t *dev = (x54x_t *)priv;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102) return;

    /* Save the MCA register value. */
    dev->pos_regs[port & 7] = val;

    /* This is always necessary so that the old handler doesn't remain. */
    x54x_io_remove(dev, dev->Base, 4);

    /* Get the new assigned I/O base address. */
    dev->Base = (dev->pos_regs[3] & 7) << 8;
    dev->Base |= ((dev->pos_regs[3] & 0xc0) ? 0x34 : 0x30);

    /* Save the new IRQ and DMA channel values. */
    dev->Irq = (dev->pos_regs[4] & 0x07) + 8;
    dev->DmaChannel = dev->pos_regs[5] & 0x0f;	

    /* Extract the BIOS ROM address info. */
    if (! (dev->pos_regs[2] & 0x80)) switch(dev->pos_regs[3] & 0x38) {
	case 0x38:		/* [1]=xx11 1xxx */
		dev->rom_addr = 0xDC000;
		break;

	case 0x30:		/* [1]=xx11 0xxx */
		dev->rom_addr = 0xD8000;
		break;

	case 0x28:		/* [1]=xx10 1xxx */
		dev->rom_addr = 0xD4000;
		break;

	case 0x20:		/* [1]=xx10 0xxx */
		dev->rom_addr = 0xD0000;
		break;

	case 0x18:		/* [1]=xx01 1xxx */
		dev->rom_addr = 0xCC000;
		break;

	case 0x10:		/* [1]=xx01 0xxx */
		dev->rom_addr = 0xC8000;
		break;
    } else {
	/* Disabled. */
	dev->rom_addr = 0x000000;
    }

    /*
     * Get misc SCSI config stuff.  For now, we are only
     * interested in the configured HA target ID:
     *
     *  pos[2]=111xxxxx = 7
     *  pos[2]=000xxxxx = 0
     */
    dev->HostID = (dev->pos_regs[4] >> 5) & 0x07;

    /*
     * SYNC mode is pos[2]=xxxx1xxx.
     *
     * SCSI Parity is pos[2]=xxx1xxxx.
     */
    dev->sync = (dev->pos_regs[4] >> 3) & 1;
    dev->parity = (dev->pos_regs[4] >> 4) & 1;

    /*
     * The PS/2 Model 80 BIOS always enables a card if it finds one,
     * even if no resources were assigned yet (because we only added
     * the card, but have not run AutoConfig yet...)
     *
     * So, remove current address, if any.
     */
    mem_mapping_disable(&dev->bios.mapping);

    /* Initialize the device if fully configured. */
    if (dev->pos_regs[2] & 0x01) {
	/* Card enabled; register (new) I/O handler. */
	x54x_io_set(dev, dev->Base, 4);

	/* Reset the device. */
	x54x_reset_ctrl(dev, CTRL_HRST);

	/* Enable or disable the BIOS ROM. */
	if (dev->rom_addr != 0x000000) {
		mem_mapping_enable(&dev->bios.mapping);
		mem_mapping_set_addr(&dev->bios.mapping, dev->rom_addr, ROM_SIZE);
	}

	/* Say hello. */
	aha_log("AHA-1640: I/O=%04x, IRQ=%d, DMA=%d, BIOS @%05X, HOST ID %i\n",
		dev->Base, dev->Irq, dev->DmaChannel, dev->rom_addr, dev->HostID);
    }
}


static uint8_t
aha_mca_feedb(void *priv)
{
    x54x_t *dev = (x54x_t *)priv;

    return (dev->pos_regs[2] & 0x01);
}


static void
aha_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    x54x_t *dev = (x54x_t *) priv;
    int i;

    switch (ld) {
	case 0:
		if (dev->Base) {
			x54x_io_remove(dev, dev->Base, 4);
			dev->Base = 0;
		}

		dev->Irq = 0;
		dev->DmaChannel = ISAPNP_DMA_DISABLED;
		dev->rom_addr = 0;

		mem_mapping_disable(&dev->bios.mapping);

		if (config->activate) {
			dev->Base = config->io[0].base;
			if (dev->Base != ISAPNP_IO_DISABLED)
				x54x_io_set(dev, dev->Base, 4);

			/*
			 * Patch the ROM BIOS image for stuff Adaptec deliberately
			 * made hard to understand. Well, maybe not, maybe it was
			 * their way of handling issues like these at the time..
			 *
			 * Patch 1: emulate the I/O ADDR SW setting by patching a
			 *	    byte in the BIOS that indicates the I/O ADDR
			 *	    switch setting on the board.
			 */
			if (dev->rom_ioaddr != 0x0000) {
				/* Look up the I/O address in the table. */
					for (i=0; i<8; i++)
					if (aha_ports[i] == dev->Base) break;
				if (i == 8) {
					aha_log("%s: invalid I/O address %04x selected!\n",
								dev->name, dev->Base);
					return;
				}
				dev->bios.rom[dev->rom_ioaddr] = (uint8_t)i;
				/* Negation of the DIP switches to satify the checksum. */
				dev->bios.rom[dev->rom_ioaddr + 1] = (uint8_t)((i ^ 0xff) + 1);
			}

			dev->Irq = config->irq[0].irq;
			dev->DmaChannel = config->dma[0].dma;

			dev->nvr[1] = (dev->Irq - 9) | (dev->DmaChannel << 4);
			aha_eeprom_save(dev);

			dev->rom_addr = config->mem[0].base;
			if (dev->rom_addr) {
				mem_mapping_enable(&dev->bios.mapping);
				aha_log("SCSI BIOS set to: %08X-%08X\n", dev->rom_addr, dev->rom_addr + config->mem[0].size - 1);
				mem_mapping_set_addr(&dev->bios.mapping, dev->rom_addr, config->mem[0].size);
			}
		}

		break;

#ifdef AHA1542CP_FDC
	case 1:
		if (dev->fdc_address) {
			fdc_remove(dev->fdc);
			dev->fdc_address = 0;
		}

		fdc_set_irq(dev->fdc, 0);
		fdc_set_dma_ch(dev->fdc, ISAPNP_DMA_DISABLED);

		if (config->activate) {
			dev->fdc_address = config->io[0].base;
			if (dev->fdc_address != ISAPNP_IO_DISABLED)
				fdc_set_base(dev->fdc, dev->fdc_address);

			fdc_set_irq(dev->fdc, config->irq[0].irq);
			fdc_set_dma_ch(dev->fdc, config->dma[0].dma);
		}

		break;
#endif
    }
}


/* Initialize the board's ROM BIOS. */
static void
aha_setbios(x54x_t *dev)
{
    uint32_t size;
    uint32_t mask;
    uint32_t temp;
    FILE *f;
    int i;

    /* Only if this device has a BIOS ROM. */
    if (dev->bios_path == NULL) return;

    /* Open the BIOS image file and make sure it exists. */
    aha_log("%s: loading BIOS from '%s'\n", dev->name, dev->bios_path);
    if ((f = rom_fopen(dev->bios_path, "rb")) == NULL) {
	aha_log("%s: BIOS ROM not found!\n", dev->name);
	return;
    }

    /*
     * Manually load and process the ROM image.
     *
     * We *could* use the system "rom_init" function here, but for
     * this special case, we can't: we may need WRITE access to the
     * memory later on.
     */
    (void)fseek(f, 0L, SEEK_END);
    temp = ftell(f);
    (void)fseek(f, 0L, SEEK_SET);

    /* Load first chunk of BIOS (which is the main BIOS, aka ROM1.) */
    dev->rom1 = malloc(ROM_SIZE);
    (void)fread(dev->rom1, ROM_SIZE, 1, f);
    temp -= ROM_SIZE;
    if (temp > 0) {
	dev->rom2 = malloc(ROM_SIZE);
	(void)fread(dev->rom2, ROM_SIZE, 1, f);
	temp -= ROM_SIZE;
    } else {
	dev->rom2 = NULL;
    }
    if (temp != 0) {
	aha_log("%s: BIOS ROM size invalid!\n", dev->name);
	free(dev->rom1);
	if (dev->rom2 != NULL)
		free(dev->rom2);
	(void)fclose(f);
	return;
    }
    temp = ftell(f);
    if (temp > ROM_SIZE)
	temp = ROM_SIZE;
    (void)fclose(f);

    /* Adjust BIOS size in chunks of 2K, as per BIOS spec. */
    size = 0x10000;
    if (temp <= 0x8000)
	size = 0x8000;
    if (temp <= 0x4000)
	size = 0x4000;
    if (temp <= 0x2000)
	size = 0x2000;
    mask = (size - 1);
    aha_log("%s: BIOS at 0x%06lX, size %lu, mask %08lx\n",
			dev->name, dev->rom_addr, size, mask);

    /* Initialize the ROM entry for this BIOS. */
    memset(&dev->bios, 0x00, sizeof(rom_t));

    /* Enable ROM1 into the memory map. */
    dev->bios.rom = dev->rom1;

    /* Set up an address mask for this memory. */
    dev->bios.mask = mask;

    /* Map this system into the memory map. */
    mem_mapping_add(&dev->bios.mapping, dev->rom_addr, size,
		    aha_mem_read, NULL, NULL, /* aha_mem_readw, aha_mem_readl, */
		    aha_mem_write, NULL, NULL,
		    dev->bios.rom, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->bios.mapping);

    /*
     * Patch the ROM BIOS image for stuff Adaptec deliberately
     * made hard to understand. Well, maybe not, maybe it was
     * their way of handling issues like these at the time..
     *
     * Patch 1: emulate the I/O ADDR SW setting by patching a
     *	    byte in the BIOS that indicates the I/O ADDR
     *	    switch setting on the board.
     */
    if (dev->rom_ioaddr != 0x0000) {
	/* Look up the I/O address in the table. */
	for (i=0; i<8; i++)
		if (aha_ports[i] == dev->Base) break;
	if (i == 8) {
		aha_log("%s: invalid I/O address %04x selected!\n",
					dev->name, dev->Base);
		return;
	}
	dev->bios.rom[dev->rom_ioaddr] = (uint8_t)i;
	/* Negation of the DIP switches to satify the checksum. */
	dev->bios.rom[dev->rom_ioaddr + 1] = (uint8_t)((i ^ 0xff) + 1);
    }
}


/* Get the SCSISelect code decompressor program from the microcode rom for the
   AHA-1542CP. */
static void
aha_setmcode(x54x_t *dev)
{
    uint32_t temp;
    FILE *f;

    /* Only if this device has a BIOS ROM. */
    if (dev->mcode_path == NULL) return;

    /* Open the microcode image file and make sure it exists. */
    aha_log("%s: loading microcode from '%ls'\n", dev->name, dev->bios_path);
    if ((f = rom_fopen(dev->mcode_path, "rb")) == NULL) {
	aha_log("%s: microcode ROM not found!\n", dev->name);
	return;
    }

    /*
     * Manually load and process the ROM image.
     *
     * We *could* use the system "rom_init" function here, but for
     * this special case, we can't: we may need WRITE access to the
     * memory later on.
     */
    (void)fseek(f, 0L, SEEK_END);
    temp = ftell(f);
    (void)fseek(f, 0L, SEEK_SET);

    if (temp < (dev->cmd_33_offset + dev->cmd_33_len - 1)) {
	aha_log("%s: microcode ROM size invalid!\n", dev->name);
	(void)fclose(f);
	return;
    }

    /* Allocate the buffer and then read the real PnP ROM into it. */
    if (aha1542cp_pnp_rom != NULL) {
	free(aha1542cp_pnp_rom);
	aha1542cp_pnp_rom = NULL;
    }
    aha1542cp_pnp_rom = (uint8_t *) malloc(dev->pnp_len + 7);
    fseek(f, dev->pnp_offset, SEEK_SET);
    (void)fread(aha1542cp_pnp_rom, dev->pnp_len, 1, f);
    memset(&(aha1542cp_pnp_rom[4]), 0x00, 5);
    fseek(f, dev->pnp_offset + 4, SEEK_SET);
    (void)fread(&(aha1542cp_pnp_rom[9]), dev->pnp_len - 4, 1, f);
    /* Even the real AHA-1542CP microcode seem to be flipping bit
       4 to not erroneously indicate there is a range length. */
    aha1542cp_pnp_rom[0x87] |= 0x04;
    /* Insert the terminator and the checksum byte that will later
       be filled in by the isapnp code. */
    aha1542cp_pnp_rom[dev->pnp_len + 5] = 0x79;
    aha1542cp_pnp_rom[dev->pnp_len + 6] = 0x00;

    /* Load the SCSISelect decompression code. */
    fseek(f, dev->cmd_33_offset, SEEK_SET);
    (void)fread(dev->cmd_33_buf, dev->cmd_33_len, 1, f);

    (void)fclose(f);
}


static void
aha_initnvr(x54x_t *dev)
{
    /* Initialize the on-board EEPROM. */
    dev->nvr[0] = dev->HostID;			/* SCSI ID 7 */
    dev->nvr[0] |= (0x10 | 0x20 | 0x40);
    if (dev->fdc_address == 0x370)
	dev->nvr[0] |= EE0_ALTFLOP;
    dev->nvr[1] = dev->Irq-9;			/* IRQ15 */
    dev->nvr[1] |= (dev->DmaChannel<<4);	/* DMA6 */
    dev->nvr[2] = (EE2_HABIOS	| 		/* BIOS enabled		*/
		   EE2_DYNSCAN	|		/* scan bus		*/
		   EE2_EXT1G | EE2_RMVOK);	/* Imm return on seek	*/
    dev->nvr[3] = SPEED_50;			/* speed 5.0 MB/s	*/
    dev->nvr[6] = (EE6_TERM	|		/* host term enable	*/
		   EE6_RSTBUS);			/* reset SCSI bus on boot*/
}


/* Initialize the board's EEPROM (NVR.) */
static void
aha_setnvr(x54x_t *dev)
{
    FILE *f;

    /* Only if this device has an EEPROM. */
    if (dev->nvr_path == NULL) return;

    /* Allocate and initialize the EEPROM. */
    dev->nvr = (uint8_t *)malloc(NVR_SIZE);
    memset(dev->nvr, 0x00, NVR_SIZE);

    f = nvr_fopen(dev->nvr_path, "rb");
    if (f) {
	if (fread(dev->nvr, 1, NVR_SIZE, f) != NVR_SIZE)
		fatal("aha_setnvr(): Error reading data\n");
	fclose(f);
	f = NULL;
    } else
	aha_initnvr(dev);

    if (dev->type == AHA_154xCF) {
	if (dev->fdc_address > 0) {
		fdc_remove(dev->fdc);
		fdc_set_base(dev->fdc, (dev->nvr[0] & EE0_ALTFLOP) ? 0x370 : 0x3f0);
	}
    }
}


void
aha1542cp_close(void *priv)
{
    if (aha1542cp_pnp_rom != NULL) {
	free(aha1542cp_pnp_rom);
	aha1542cp_pnp_rom = NULL;
    }

    x54x_close(priv);
}


/* General initialization routine for all boards. */
static void *
aha_init(const device_t *info)
{
    x54x_t *dev;

    /* Call common initializer. */
    dev = x54x_init(info);
    dev->bus = scsi_get_bus();

    /*
     * Set up the (initial) I/O address, IRQ and DMA info.
     *
     * Note that on MCA, configuration is handled by the BIOS,
     * and so any info we get here will be overwritten by the
     * MCA-assigned values later on!
     */
    dev->Base = device_get_config_hex16("base");
    dev->Irq = device_get_config_int("irq");
    dev->DmaChannel = device_get_config_int("dma");
    dev->rom_addr = device_get_config_hex20("bios_addr");
    if (!(dev->card_bus & DEVICE_MCA))
	dev->fdc_address = device_get_config_hex16("fdc_addr");
    else
	dev->fdc_address = 0;
    dev->HostID = 7;		/* default HA ID */
    dev->setup_info_len = sizeof(aha_setup_t);
    dev->max_id = 7;
    dev->flags = 0;

    dev->ven_callback = aha_callback;
    dev->ven_cmd_is_fast = aha_cmd_is_fast;
    dev->ven_fast_cmds = aha_fast_cmds;
    dev->get_ven_param_len = aha_param_len;
    dev->ven_cmds = aha_cmds;
    dev->get_ven_data = aha_setup_data;

    dev->mcode_path = NULL;
    dev->cmd_33_len = 0x0000;
    dev->cmd_33_offset = 0x0000;
    memset(dev->cmd_33_buf, 0x00, 4096);

    strcpy(dev->vendor, "Adaptec");

    /* Perform per-board initialization. */
    switch(dev->type) {
	case AHA_154xA:
		strcpy(dev->name, "AHA-154xA");
		dev->fw_rev = "A003";	/* The 3.07 microcode says A006. */
		dev->bios_path = "roms/scsi/adaptec/aha1540a307.bin"; /*Only for port 0x330*/
		/* This is configurable from the configuration for the 154xB, the rest of the controllers read it from the EEPROM. */
		dev->HostID = device_get_config_int("hostid");
		dev->rom_shram = 0x3F80;	/* shadow RAM address base */
		dev->rom_shramsz = 128;		/* size of shadow RAM */
		dev->ha_bps = 5000000.0;	/* normal SCSI */
		break;
		
	case AHA_154xB:
		strcpy(dev->name, "AHA-154xB");
		switch(dev->Base) {
			case 0x0330:
				dev->bios_path =
				    "roms/scsi/adaptec/aha1540b320_330.bin";
				break;

			case 0x0334:
				dev->bios_path =
				    "roms/scsi/adaptec/aha1540b320_334.bin";
				break;
		}
		dev->fw_rev = "A005";	/* The 3.2 microcode says A012. */
		/* This is configurable from the configuration for the 154xB, the rest of the controllers read it from the EEPROM. */
		dev->HostID = device_get_config_int("hostid");
		dev->rom_shram = 0x3F80;	/* shadow RAM address base */
		dev->rom_shramsz = 128;		/* size of shadow RAM */
		dev->ha_bps = 5000000.0;	/* normal SCSI */
		break;

	case AHA_154xC:
		strcpy(dev->name, "AHA-154xC");
		dev->bios_path = "roms/scsi/adaptec/aha1542c102.bin";
		dev->nvr_path = "aha1542c.nvr";
		dev->fw_rev = "D001";
		dev->rom_shram = 0x3F80;	/* shadow RAM address base */
		dev->rom_shramsz = 128;		/* size of shadow RAM */
		dev->rom_ioaddr = 0x3F7E;	/* [2:0] idx into addr table */
		dev->rom_fwhigh = 0x0022;	/* firmware version (hi/lo) */
		dev->ven_get_host_id = aha_get_host_id;	/* function to return host ID from EEPROM */
		dev->ven_get_irq = aha_get_irq;		/* function to return IRQ from EEPROM */
		dev->ven_get_dma = aha_get_dma;		/* function to return DMA channel from EEPROM */
		dev->ha_bps = 5000000.0;	/* normal SCSI */
		break;

	case AHA_154xCF:
		strcpy(dev->name, "AHA-154xCF");
		dev->bios_path = "roms/scsi/adaptec/aha1542cf211.bin";
		dev->nvr_path = "aha1542cf.nvr";
		dev->fw_rev = "E001";
		dev->rom_shram = 0x3F80;	/* shadow RAM address base */
		dev->rom_shramsz = 128;		/* size of shadow RAM */
		dev->rom_ioaddr = 0x3F7E;	/* [2:0] idx into addr table */
		dev->rom_fwhigh = 0x0022;	/* firmware version (hi/lo) */
		dev->flags |= X54X_CDROM_BOOT;
		dev->ven_get_host_id = aha_get_host_id;	/* function to return host ID from EEPROM */
		dev->ven_get_irq = aha_get_irq;		/* function to return IRQ from EEPROM */
		dev->ven_get_dma = aha_get_dma;		/* function to return DMA channel from EEPROM */
		dev->ha_bps = 10000000.0;	/* fast SCSI */
		if (dev->fdc_address > 0)
			dev->fdc = device_add(&fdc_at_device);
		break;

	case AHA_154xCP:
		strcpy(dev->name, "AHA-154xCP");
		dev->bios_path = "roms/scsi/adaptec/aha1542cp102.bin";
		dev->mcode_path = "roms/scsi/adaptec/908301-00_f_mcode_17c9.u12";
		dev->nvr_path = "aha1542cp.nvr";
		dev->fw_rev = "F001";
		dev->rom_shram = 0x3F80;	/* shadow RAM address base */
		dev->rom_shramsz = 128;		/* size of shadow RAM */
		dev->rom_ioaddr = 0x3F7E;	/* [2:0] idx into addr table */
		dev->rom_fwhigh = 0x0055;	/* firmware version (hi/lo) */
		dev->flags |= X54X_CDROM_BOOT;
		dev->flags |= X54X_ISAPNP;
		dev->ven_get_host_id = aha_get_host_id;	/* function to return host ID from EEPROM */
		dev->ven_get_irq = aha_get_irq;		/* function to return IRQ from EEPROM */
		dev->ven_get_dma = aha_get_dma;		/* function to return DMA channel from EEPROM */
		dev->ha_bps = 10000000.0;		/* fast SCSI */
		dev->pnp_len = 0x00be;			/* length of the PnP ROM */
		dev->pnp_offset = 0x533d;		/* offset of the PnP ROM in the microcode ROM */
		dev->cmd_33_len = 0x06dc;		/* length of the SCSISelect code expansion routine returned by
							   SCSI controller command 0x33 */
		dev->cmd_33_offset = 0x7000;		/* offset of the SCSISelect code expansion routine in the
							   microcode ROM */
		aha_setmcode(dev);
		if (aha1542cp_pnp_rom)
			isapnp_add_card(aha1542cp_pnp_rom, dev->pnp_len + 7, aha_pnp_config_changed, NULL, NULL, NULL, dev);
#ifdef AHA1542CP_FDC
		dev->fdc = device_add(&fdc_at_device);
#endif
		break;

	case AHA_1640:
		strcpy(dev->name, "AHA-1640");
		dev->bios_path = "roms/scsi/adaptec/aha1640.bin";
		dev->fw_rev = "BB01";

		dev->flags |= X54X_LBA_BIOS;

		/* Enable MCA. */
		dev->pos_regs[0] = 0x1F;	/* MCA board ID */
		dev->pos_regs[1] = 0x0F;	
		mca_add(aha_mca_read, aha_mca_write, aha_mca_feedb, NULL, dev);
		dev->ha_bps = 5000000.0;	/* normal SCSI */
		break;
    }	

    /* Initialize ROM BIOS if needed. */
    aha_setbios(dev);

    /* Initialize EEPROM (NVR) if needed. */
    aha_setnvr(dev);

    if (dev->Base != 0) {
	/* Initialize the device. */
	x54x_device_reset(dev);

        if (!(dev->card_bus & DEVICE_MCA) && !(dev->flags & X54X_ISAPNP)) {
		/* Register our address space. */
	        x54x_io_set(dev, dev->Base, 4);

		/* Enable the memory. */
		if (dev->rom_addr != 0x000000) {
			mem_mapping_enable(&dev->bios.mapping);
			mem_mapping_set_addr(&dev->bios.mapping, dev->rom_addr, ROM_SIZE);
		}
	}
    }

    return(dev);
}


static const device_config_t aha_154xb_config[] = {
        {
		"base", "Address", CONFIG_HEX16, "", 0x334, "", { 0 },
                {
                        {
                                "None",      0
                        },
                        {
                                "0x330", 0x330
                        },
                        {
                                "0x334", 0x334
                        },
                        {
                                "0x230", 0x230
                        },
                        {
                                "0x234", 0x234
                        },
                        {
                                "0x130", 0x130
                        },
                        {
                                "0x134", 0x134
                        },
                        {
                                ""
                        }
                },
        },
        {
		"irq", "IRQ", CONFIG_SELECTION, "", 11, "", { 0 },
                {
                        {
                                "IRQ 9", 9
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                "IRQ 11", 11
                        },
                        {
                                "IRQ 12", 12
                        },
                        {
                                "IRQ 14", 14
                        },
                        {
                                "IRQ 15", 15
                        },
                        {
                                ""
                        }
                },
        },
        {
		"dma", "DMA channel", CONFIG_SELECTION, "", 6, "", { 0 },
                {
                        {
                                "DMA 5", 5
                        },
                        {
                                "DMA 6", 6
                        },
                        {
                                "DMA 7", 7
                        },
                        {
                                ""
                        }
                },
        },
        {
		"hostid", "Host ID", CONFIG_SELECTION, "", 7, "", { 0 },
                {
                        {
                                "0", 0
                        },
                        {
                                "1", 1
                        },
                        {
                                "2", 2
                        },
                        {
                                "3", 3
                        },
                        {
                                "4", 4
                        },
                        {
                                "5", 5
                        },
                        {
                                "6", 6
                        },
                        {
                                "7", 7
                        },
                        {
                                ""
                        }
                },
        },
        {
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0, "", { 0 },
                {
                        {
                                "Disabled", 0
                        },
                        {
                                "C800H", 0xc8000
                        },
                        {
                                "D000H", 0xd0000
                        },
                        {
                                "D800H", 0xd8000
                        },
                        {
                                ""
                        }
                },
        },
	{
		"", "", -1
	}
};

static const device_config_t aha_154x_config[] = {
        {
		"base", "Address", CONFIG_HEX16, "", 0x334, "", { 0 },
                {
                        {
                                "None",      0
                        },
                        {
                                "0x330", 0x330
                        },
                        {
                                "0x334", 0x334
                        },
                        {
                                "0x230", 0x230
                        },
                        {
                                "0x234", 0x234
                        },
                        {
                                "0x130", 0x130
                        },
                        {
                                "0x134", 0x134
                        },
                        {
                                ""
                        }
                },
        },
        {
		"irq", "IRQ", CONFIG_SELECTION, "", 11, "", { 0 },
                {
                        {
                                "IRQ 9", 9
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                "IRQ 11", 11
                        },
                        {
                                "IRQ 12", 12
                        },
                        {
                                "IRQ 14", 14
                        },
                        {
                                "IRQ 15", 15
                        },
                        {
                                ""
                        }
                },
        },
        {
		"dma", "DMA channel", CONFIG_SELECTION, "", 6, "", { 0 },
                {
                        {
                                "DMA 5", 5
                        },
                        {
                                "DMA 6", 6
                        },
                        {
                                "DMA 7", 7
                        },
                        {
                                ""
                        }
                },
        },
        {
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0, "", { 0 },
                {
                        {
                                "Disabled", 0
                        },
                        {
                                "C800H", 0xc8000
                        },
                        {
                                "D000H", 0xd0000
                        },
                        {
                                "D800H", 0xd8000
                        },
                        {
                                ""
                        }
                },
        },
	{
		"", "", -1
	}
};


static const device_config_t aha_154xcf_config[] = {
        {
		"base", "Address", CONFIG_HEX16, "", 0x334, "", { 0 },
                {
                        {
                                "None",      0
                        },
                        {
                                "0x330", 0x330
                        },
                        {
                                "0x334", 0x334
                        },
                        {
                                "0x230", 0x230
                        },
                        {
                                "0x234", 0x234
                        },
                        {
                                "0x130", 0x130
                        },
                        {
                                "0x134", 0x134
                        },
                        {
                                ""
                        }
                },
        },
        {
		"irq", "IRQ", CONFIG_SELECTION, "", 11, "", { 0 },
                {
                        {
                                "IRQ 9", 9
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                "IRQ 11", 11
                        },
                        {
                                "IRQ 12", 12
                        },
                        {
                                "IRQ 14", 14
                        },
                        {
                                "IRQ 15", 15
                        },
                        {
                                ""
                        }
                },
        },
        {
		"dma", "DMA channel", CONFIG_SELECTION, "", 6, "", { 0 },
                {
                        {
                                "DMA 5", 5
                        },
                        {
                                "DMA 6", 6
                        },
                        {
                                "DMA 7", 7
                        },
                        {
                                ""
                        }
                },
        },
        {
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0, "", { 0 },
                {
                        {
                                "Disabled", 0
                        },
                        {
                                "C800H", 0xc8000
                        },
                        {
                                "D000H", 0xd0000
                        },
                        {
                                "D800H", 0xd8000
                        },
                        {
                                ""
                        }
                },
        },
        {
		"fdc_addr", "FDC address", CONFIG_HEX16, "", 0, "", { 0 },
                {
                        {
                                "None",      0
                        },
                        {
                                "0x3f0", 0x3f0
                        },
                        {
                                "0x370", 0x370
                        },
                        {
                                ""
                        }
                },
        },
	{
		"", "", -1
	}
};


const device_t aha154xa_device = {
    "Adaptec AHA-154xA",
    DEVICE_ISA | DEVICE_AT,
    AHA_154xA,
    aha_init, x54x_close, NULL,
    { NULL }, NULL, NULL,
    aha_154xb_config
};

const device_t aha154xb_device = {
    "Adaptec AHA-154xB",
    DEVICE_ISA | DEVICE_AT,
    AHA_154xB,
    aha_init, x54x_close, NULL,
    { NULL }, NULL, NULL,
    aha_154xb_config
};

const device_t aha154xc_device = {
    "Adaptec AHA-154xC",
    DEVICE_ISA | DEVICE_AT,
    AHA_154xC,
    aha_init, x54x_close, NULL,
    { NULL }, NULL, NULL,
    aha_154x_config
};

const device_t aha154xcf_device = {
    "Adaptec AHA-154xCF",
    DEVICE_ISA | DEVICE_AT,
    AHA_154xCF,
    aha_init, x54x_close, NULL,
    { NULL }, NULL, NULL,
    aha_154xcf_config
};

const device_t aha154xcp_device = {
    "Adaptec AHA-154xCP",
    DEVICE_ISA | DEVICE_AT,
    AHA_154xCP,
    aha_init, aha1542cp_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};

const device_t aha1640_device = {
    "Adaptec AHA-1640",
    DEVICE_MCA,
    AHA_1640,
    aha_init, x54x_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
