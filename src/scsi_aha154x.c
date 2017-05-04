/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of several low-level support functions for
 *		the AHA-154x series of ISA Host Adapters made by Adaptec.
 *		These functions implement the support needed by the ROM BIOS
 *		of these cards.
 *
 * Version:	@(#)aha154x.c	1.0.4	2017/04/21
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ibm.h"
#include "mem.h"
#include "rom.h"
#include "device.h"
#include "scsi_buslogic.h"


#define AHA		AHA154xCF		/* set desired card type */
#define  AHA154xB	1			/* AHA-154x Rev.B */
#define  AHA154xC	2			/* AHA-154x Rev.C */
#define  AHA154xCF	3			/* AHA-154x Rev.CF */
#define  AHA154xCP	4			/* AHA-154x Rev.CP */


#if AHA == AHA154xB
# define ROMFILE	"roms/adaptec/aha1540b310.bin"
# define AHA_BID	'A'			/* AHA-154x B */
#endif

#if AHA == AHA154xC
# define ROMFILE	"roms/adaptec/aha1542c101.bin"
# define AHA_BID	'D'			/* AHA-154x C */
# define ROM_FWHIGH	0x0022			/* firmware version (hi/lo) */
# define ROM_SHRAM	0x3F80			/* shadow RAM address base */
# define ROM_SHRAMSZ	128			/* size of shadow RAM */
# define ROM_IOADDR	0x3F7E			/* [2:0] idx into addr table */
# define EEP_SIZE	32			/* 32 bytes of storage */
#endif

#if AHA == AHA154xCF
# define ROMFILE	"roms/adaptec/aha1542cf201.bin"
# define AHA_BID	'E'			/* AHA-154x CF */
# define ROM_FWHIGH	0x0022			/* firmware version (hi/lo) */
# define ROM_SHRAM	0x3F80			/* shadow RAM address base */
# define ROM_SHRAMSZ	128			/* size of shadow RAM */
# define ROM_IOADDR	0x3F7E			/* [2:0] idx into addr table */
# define EEP_SIZE	32			/* 32 bytes of storage */
#endif

#if AHA == AHA154xCP
# define ROMFILE	"roms/adaptec/aha1542cp102.bin"
# define AHA_BID	'F'			/* AHA-154x CP */
# define ROM_FWHIGH	0x0055			/* firmware version (hi/lo) */
# define ROM_SHRAM	0x3F80			/* shadow RAM address base */
# define ROM_SHRAMSZ	128			/* size of shadow RAM */
# define ROM_IOADDR	0x3F7E			/* [2:0] idx into addr table */
# define EEP_SIZE	32			/* 32 bytes of storage */
#endif

#define ROM_SIZE	16384			/* one ROM is 16K */


/* EEPROM map and bit definitions. */
#define EE0_HOSTID	0x07	/* EE(0) [2:0]				*/
#define EE0_ALTFLOP	0x80	/* EE(0) [7] FDC at 370h		*/
#define EE1_IRQCH	0x07	/* EE(1) [3:0]				*/
#define EE1_DMACH	0x70	/* EE(1) [7:4]				*/
#define EE2_RMVOK	0x01	/* EE(2) [0] Support removable disks	*/
#define EE2_HABIOS	0x02	/* EE(2) [1] HA Bios Space Reserved	*/
#define EE2_INT19	0x04	/* EE(2) [2] HA Bios Controls INT19	*/
#define EE2_DYNSCAN	0x08	/* EE(2) [3] Dynamically scan bus	*/
#define EE2_TWODRV	0x10	/* EE(2) [4] Allow more than 2 drives	*/
#define EE2_SEEKRET	0x20	/* EE(2) [5] Immediate return on seek	*/
#define EE2_EXT1G	0x80	/* EE(2) [7] Extended Translation >1GB	*/
#define EE3_SPEED	0x00	/* EE(3) [7:0] DMA Speed		*/
#define  SPEED_33	0xFF
#define  SPEED_50	0x00
#define  SPEED_56	0x04
#define  SPEED_67	0x01
#define  SPEED_80	0x02
#define  SPEED_10	0x03
#define EE4_FLOPTOK	0x80	/* EE(4) [7] Support Flopticals		*/
#define EE6_PARITY	0x01	/* EE(6) [0] parity check enable	*/
#define EE6_TERM	0x02	/* EE(6) [1] host term enable		*/
#define EE6_RSTBUS	0x04	/* EE(6) [2] reset SCSI bus on boot	*/
#define EEE_SYNC	0x01	/* EE(E) [0] Enable Sync Negotiation	*/
#define EEE_DISCON	0x02	/* EE(E) [1] Enable Disconnection	*/
#define EEE_FAST	0x04	/* EE(E) [2] Enable FAST SCSI		*/
#define EEE_START	0x08	/* EE(E) [3] Enable Start Unit		*/


static rom_t	aha_bios;			/* active ROM */
static uint8_t	*aha_rom1;			/* main BIOS */
static uint8_t	*aha_rom2;			/* SCSI-Select */
#ifdef EEP_SIZE
static uint8_t	aha_eep[EEP_SIZE];		/* EEPROM storage */
#endif
static uint16_t	aha_ports[] = {
    0x0330, 0x0334, 0x0230, 0x0234,
    0x0130, 0x0134, 0x0000, 0x0000
};


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
    rom_t *rom = (rom_t *)priv;

#if 0
    pclog("AHA1542x: writing to BIOS space, %06lX, val %02x\n", addr, val);
    pclog("          called from %04X:%04X\n", CS, cpu_state.pc);        
#endif
    if ((addr & rom->mask) >= 0x3F80)
	rom->rom[addr & rom->mask] = val;
}


static uint8_t
aha_mem_read(uint32_t addr, void *priv)
{
    rom_t *rom = (rom_t *)priv;

    return(rom->rom[addr & rom->mask]);
}


static uint16_t
aha_mem_readw(uint32_t addr, void *priv)
{
    rom_t *rom = (rom_t *)priv;

    return(*(uint16_t *)&rom->rom[addr & rom->mask]);
}


static uint32_t
aha_mem_readl(uint32_t addr, void *priv)
{
    rom_t *rom = (rom_t *)priv;

    return(*(uint32_t *)&rom->rom[addr & rom->mask]);
}


#ifdef ROM_IOADDR
/*
 * Patch the ROM BIOS image for stuff Adaptec deliberately
 * made hard to understand. Well, maybe not, maybe it was
 * their way of handling issues like these at the time..
 *
 * Patch 1: emulate the I/O ADDR SW setting by patching a
 *	    byte in the BIOS that indicates the I/O ADDR
 *	    switch setting on the board.
 */
static void
aha_patch(uint8_t *romptr, uint16_t ioaddr)
{
    int i;

    /* Look up the I/O address in the table. */
    for (i=0; i<8; i++)
	if (aha_ports[i] == ioaddr) break;
    if (i == 8) {
	pclog("AHA154x: bad news, invalid I/O address %04x selected!\n",
								ioaddr);
	return;
    }
    romptr[ROM_IOADDR] = (unsigned char)i;
}
#endif

    
/* Initialize AHA-154xNN-specific stuff. */
void
aha154x_init(uint16_t ioaddr, uint32_t memaddr, aha_info *aha)
{
    uint32_t bios_size;
    uint32_t bios_addr;
    uint32_t bios_mask;
    char *bios_path;
    uint32_t temp;
    FILE *f;

    /* Set BIOS load address. */
    bios_addr = memaddr;
    bios_path = ROMFILE;
    pclog("AHA154x: loading BIOS from '%s'\n", bios_path);

    /* Open the BIOS image file and make sure it exists. */
    if ((f = fopen(bios_path, "rb")) == NULL) {
	pclog("AHA154x: BIOS ROM not found!\n");
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
    aha_rom1 = malloc(ROM_SIZE);
    (void)fread(aha_rom1, ROM_SIZE, 1, f);
    temp -= ROM_SIZE;
    if (temp > 0) {
	aha_rom2 = malloc(ROM_SIZE);
	(void)fread(aha_rom2, ROM_SIZE, 1, f);
	temp -= ROM_SIZE;
    } else {
	aha_rom2 = NULL;
    }
    if (temp != 0) {
	pclog("AHA154x: BIOS ROM size invalid!\n");
	free(aha_rom1);
	if (aha_rom2 != NULL)
		free(aha_rom2);
	(void)fclose(f);
	return;
    }
    temp = ftell(f);
    if (temp > ROM_SIZE)
	temp = ROM_SIZE;
    (void)fclose(f);

    /* Adjust BIOS size in chunks of 2K, as per BIOS spec. */
    bios_size = 0x10000;
    if (temp <= 0x8000)
	bios_size = 0x8000;
    if (temp <= 0x4000)
	bios_size = 0x4000;
    if (temp <= 0x2000)
	bios_size = 0x2000;
    bios_mask = (bios_size - 1);
    pclog("AHA154x: BIOS at 0x%06lX, size %lu, mask %08lx\n",
				bios_addr, bios_size, bios_mask);

    /* Initialize the ROM entry for this BIOS. */
    memset(&aha_bios, 0x00, sizeof(rom_t));

    /* Enable ROM1 into the memory map. */
    aha_bios.rom = aha_rom1;

    /* Set up an address mask for this memory. */
    aha_bios.mask = bios_mask;

    /* Map this system into the memory map. */
    mem_mapping_add(&aha_bios.mapping, bios_addr, bios_size,
		    aha_mem_read, aha_mem_readw, aha_mem_readl,
		    aha_mem_write, NULL, NULL,
		    aha_bios.rom, MEM_MAPPING_EXTERNAL, &aha_bios);

#ifdef ROM_IOADDR
    /* Patch the ROM BIOS image to work with us. */
    aha_patch(aha_bios.rom, ioaddr);
#endif

#if ROM_FWHIGH
    /* Read firmware version from the BIOS. */
    aha->fwh = aha_bios.rom[ROM_FWHIGH];
    aha->fwl = aha_bios.rom[ROM_FWHIGH+1];
#else
    /* Fake BIOS firmware version. */
    aha->fwh = '1';
    aha->fwl = '0';
#endif
    aha->bid = AHA_BID;

    /*
     * Do a checksum on the ROM.
     * The BIOS ROMs on the 154xC(F) boards will always fail
     * the checksum, because they are incomplete: on the real
     * boards, a shadow RAM and some other (config) registers
     * are mapped into its space.  It is assumed that boards
     * have logic that automatically generate a "fixup" byte
     * at the end of the data to 'make up' for this.
     *
     * We emulated some of those in the patch routine, so now
     * it is time to "fix up" the BIOS image so that the main
     * (system) BIOS considers it valid.
     */
again:
    bios_mask = 0;
    for (temp=0; temp<16384; temp++)
	bios_mask += aha_bios.rom[temp];
    bios_mask &= 0xff;
    if (bios_mask != 0x00) {
	pclog("AHA154x: fixing BIOS checksum (%02x) ..\n", bios_mask);
	aha_bios.rom[temp-1] += (256 - bios_mask);
	goto again;
    }

    /* Enable the memory. */
    mem_mapping_enable(&aha_bios.mapping);
    mem_mapping_set_addr(&aha_bios.mapping, bios_addr, bios_size);

#ifdef EEP_SIZE
    /* Initialize the on-board EEPROM. */
    memset(aha_eep, 0x00, EEP_SIZE);
    aha_eep[0] = 7;			/* SCSI ID 7 */
    aha_eep[1] = 15-9;			/* IRQ15 */
    aha_eep[1] |= (6<<4);		/* DMA6 */
    aha_eep[2] = (EE2_HABIOS	|	/* BIOS Space Reserved		*/
		  EE2_SEEKRET);		/* Immediate return on seek	*/
    aha_eep[3] = SPEED_50;		/* speed 5.0 MB/s		*/
    aha_eep[6] = (EE6_TERM	|	/* host term enable		*/
		  EE6_RSTBUS);		/* reset SCSI bus on boot	*/
#endif
}


/* Mess with the AHA-154xCF's Shadow RAM. */
uint8_t
aha154x_shram(uint8_t cmd)
{
#ifdef ROM_SHRAM
    switch(cmd) {
	case 0x00:	/* disable, make it look like ROM */
		memset(&aha_bios.rom[ROM_SHRAM], 0xFF, ROM_SHRAMSZ);
		break;

	case 0x02:	/* clear it */
		memset(&aha_bios.rom[ROM_SHRAM], 0x00, ROM_SHRAMSZ);
		break;

	case 0x03:	/* enable, clear for use */
		memset(&aha_bios.rom[ROM_SHRAM], 0x00, ROM_SHRAMSZ);
		break;
    }
#endif

    /* Firmware expects 04 status. */
    return(0x04);
}


uint8_t
aha154x_eeprom(uint8_t cmd,uint8_t arg,uint8_t len,uint8_t off,uint8_t *bufp)
{
    uint8_t r = 0xff;

    pclog("AHA154x: EEPROM cmd=%02x, arg=%02x len=%d, off=%02x\n",
					cmd, arg, len, off);

#ifdef EEP_SIZE
    if ((off+len) > EEP_SIZE) return(r);	/* no can do.. */

    if (cmd == 0x22) {
	/* Write data to the EEPROM. */
	memcpy(&aha_eep[off], bufp, len);
	r = 0;
    }

    if (cmd == 0x23) {
	/* Read data from the EEPROM. */
	memcpy(bufp, &aha_eep[off], len);
	r = len;
    }
#endif

    return(r);
}


uint8_t
aha154x_memory(uint8_t cmd)
{
    uint8_t r = 0xff;

    pclog("AHA154x: MEMORY cmd=%02x\n", cmd);

    if (cmd == 0x27) {
	/* Enable the mapper, so, set ROM2 active. */
	aha_bios.rom = aha_rom2;
    }
    if (cmd == 0x26) {
	/* Disable the mapper, so, set ROM1 active. */
	aha_bios.rom = aha_rom1;
    }

    return(0);
}
