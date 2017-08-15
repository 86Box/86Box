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
 * NOTE:	THIS IS CURRENTLY A MESS, but will be cleaned up as I go.
 *
 * Version:	@(#)scsi_aha154x.c	1.0.8	2017/08/15
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Original Buslogic version by SA1988 and Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "ibm.h"
#include "io.h"
#include "mca.h"
#include "mem.h"
#include "mca.h"
#include "rom.h"
#include "dma.h"
#include "pic.h"
#include "timer.h"
#include "device.h"
#include "cdrom.h"
#include "scsi.h"
#include "scsi_disk.h"
#include "scsi_aha154x.h"


#define SCSI_DELAY_TM	1			/* was 50 */


#define AHA		AHA154xCF		/* set desired card type */
#define  AHA154xB	1			/* AHA-154x Rev.B */
#define  AHA154xC	2			/* AHA-154x Rev.C */
#define  AHA154xCF	3			/* AHA-154x Rev.CF */
#define  AHA154xCP	4			/* AHA-154x Rev.CP */


#if AHA == AHA154xB
# define ROMFILE	L"roms/scsi/adaptec/aha1540b310.bin"
# define AHA_BID	'A'			/* AHA-154x B */
#endif

#if AHA == AHA154xC
# define ROMFILE	L"roms/scsi/adaptec/aha1542c101.bin"
# define AHA_BID	'D'		/* AHA-154x C */
# define ROM_FWHIGH	0x0022		/* firmware version (hi/lo) */
# define ROM_SHRAM	0x3F80		/* shadow RAM address base */
# define ROM_SHRAMSZ	128		/* size of shadow RAM */
# define ROM_IOADDR	0x3F7E		/* [2:0] idx into addr table */
# define EEP_SIZE	32		/* 32 bytes of storage */
#endif

#if AHA == AHA154xCF
# define ROMFILE	L"roms/scsi/adaptec/aha1542cf201.bin"
# define AHA_BID	'E'		/* AHA-154x CF */
# define ROM_FWHIGH	0x0022		/* firmware version (hi/lo) */
# define ROM_SHRAM	0x3F80		/* shadow RAM address base */
# define ROM_SHRAMSZ	128		/* size of shadow RAM */
# define ROM_IOADDR	0x3F7E		/* [2:0] idx into addr table */
# define EEP_SIZE	32		/* 32 bytes of storage */
#endif

#if AHA == AHA154xCP
# define ROMFILE	L"roms/scsi/adaptec/aha1542cp102.bin"
# define AHA_BID	'F'		/* AHA-154x CP */
# define ROM_FWHIGH	0x0055		/* firmware version (hi/lo) */
# define ROM_SHRAM	0x3F80		/* shadow RAM address base */
# define ROM_SHRAMSZ	128		/* size of shadow RAM */
# define ROM_IOADDR	0x3F7E		/* [2:0] idx into addr table */
# define EEP_SIZE	32		/* 32 bytes of storage */
#endif

#define ROM_SIZE	16384		/* one ROM is 16K */


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


/*
 * Host Adapter I/O ports.
 *
 * READ  Port x+0: STATUS
 * WRITE Port x+0: CONTROL
 *
 * READ  Port x+1: DATA
 * WRITE Port x+1: COMMAND
 *
 * READ  Port x+2: INTERRUPT STATUS
 * WRITE Port x+2: (undefined?)
 *
 * R/W   Port x+3: (undefined)
 */

/* WRITE CONTROL commands. */
#define CTRL_HRST	0x80		/* Hard reset */
#define CTRL_SRST	0x40		/* Soft reset */
#define CTRL_IRST	0x20		/* interrupt reset */
#define CTRL_SCRST	0x10		/* SCSI bus reset */

/* READ STATUS. */
#define STAT_STST	0x80		/* self-test in progress */
#define STAT_DFAIL	0x40		/* internal diagnostic failure */
#define STAT_INIT	0x20		/* mailbox initialization required */
#define STAT_IDLE	0x10		/* HBA is idle */
#define STAT_CDFULL	0x08		/* Command/Data output port is full */
#define STAT_DFULL	0x04		/* Data input port is full */
#define STAT_INVCMD	0x01		/* Invalid command */

/* READ/WRITE DATA. */
#define CMD_NOP		0x00		/* No operation */
#define CMD_MBINIT	0x01		/* mailbox initialization */
#define CMD_START_SCSI	0x02		/* Start SCSI command */
#define CMD_BIOS	0x03		/* Execute ROM BIOS command */
#define CMD_INQUIRY	0x04		/* Adapter inquiry */
#define CMD_EMBOI	0x05		/* enable Mailbox Out Interrupt */
#define CMD_SELTIMEOUT	0x06		/* Set SEL timeout */
#define CMD_BUSON_TIME	0x07		/* set bus-On time */
#define CMD_BUSOFF_TIME	0x08		/* set bus-off time */
#define CMD_DMASPEED	0x09		/* set ISA DMA speed */
#define CMD_RETDEVS	0x0A		/* return installed devices */
#define CMD_RETCONF	0x0B		/* return configuration data */
#define CMD_TARGET	0x0C		/* set HBA to target mode */
#define CMD_RETSETUP	0x0D		/* return setup data */
#define CMD_ECHO	0x1F		/* ECHO command data */

/* READ INTERRUPT STATUS. */
#define INTR_ANY	0x80		/* any interrupt */
#define INTR_SRCD	0x08		/* SCSI reset detected */
#define INTR_HACC	0x04		/* HA command complete */
#define INTR_MBOA	0x02		/* MBO empty */
#define INTR_MBIF	0x01		/* MBI full */


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

#define WALTJE 1

#ifdef WALTJE
int aha_do_log = 1;
# define ENABLE_AHA154X_LOG
#else
int aha_do_log = 0;
#endif


static void
aha_log(const char *format, ...)
{
#ifdef ENABLE_AHA154X_LOG
    va_list ap;

    if (aha_do_log) {
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	fflush(stdout);
    }
#endif
}
#define pclog	aha_log


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

    
enum {
    CHIP_AHA154XB,
    CHIP_AHA154XCF,
	CHIP_AHA1640
};


/* Initialize AHA-154xNN-specific stuff. */
static void
aha154x_bios(uint16_t ioaddr, uint32_t memaddr, aha_info *aha, int irq, int dma, int chip)
{
    uint32_t bios_size;
    uint32_t bios_addr;
    uint32_t bios_mask;
    wchar_t *bios_path;
    uint32_t temp;
    FILE *f;

    /* Set BIOS load address. */
    bios_addr = memaddr;
    /* bios_path = ROMFILE; */
    if (chip == CHIP_AHA154XB)
    {
	/* bios_path = L"roms/scsi/adaptec/aha1540b310.bin"; */
	bios_path = L"roms/scsi/adaptec/B_AC00.BIN";
    }
    else
    {
	bios_path = L"roms/scsi/adaptec/aha1542cf201.bin";
    }
    pclog_w(L"AHA154x: loading BIOS from '%s'\n", bios_path);

    /* Open the BIOS image file and make sure it exists. */
    if ((f = romfopen(bios_path, L"rb")) == NULL) {
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

#if 0
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
#endif

    if (chip == CHIP_AHA154XB)
    {
	/* Fake BIOS firmware version. */
	aha->fwh = '1';
	aha->fwl = '0';
    }
    else
    {
	/* Patch the ROM BIOS image to work with us. */
	aha_patch(aha_bios.rom, ioaddr);

	/* Read firmware version from the BIOS. */
	aha->fwh = aha_bios.rom[ROM_FWHIGH];
	aha->fwl = aha_bios.rom[ROM_FWHIGH+1];
    }

    /* aha->bid = AHA_BID; */
    aha->bid = (chip == CHIP_AHA154XB) ? 'A' : 'E';

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

/* #ifdef EEP_SIZE */
    /* Initialize the on-board EEPROM. */
    if (chip != CHIP_AHA154XB)
    {
	    memset(aha_eep, 0x00, EEP_SIZE);
	    aha_eep[0] = 7;			/* SCSI ID 7 */
		aha_eep[0] |= (0x10 | 0x20 | 0x40);
	    aha_eep[1] = irq-9;			/* IRQ15 */
	    aha_eep[1] |= (dma<<4);		/* DMA6 */
	    aha_eep[2] = (EE2_HABIOS	| 	/* BIOS enabled		*/
			  EE2_DYNSCAN	|	/* scan bus		*/
			  EE2_EXT1G | EE2_RMVOK);/* Immediate return on seek	*/
	    aha_eep[3] = SPEED_50;		/* speed 5.0 MB/s		*/
	    aha_eep[6] = (EE6_TERM	|	/* host term enable		*/
			  EE6_RSTBUS);		/* reset SCSI bus on boot	*/
    }
/* #endif */
}


/* Mess with the AHA-154xCF's Shadow RAM. */
static uint8_t
aha154x_shram(uint8_t cmd, int chip)
{
/* #ifdef ROM_SHRAM */
    if (chip != CHIP_AHA154XB) {
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
    }
/* #endif */

    /* Firmware expects 04 status. */
    return(0x04);
}


static uint8_t
aha154x_eeprom(uint8_t cmd,uint8_t arg,uint8_t len,uint8_t off,uint8_t *bufp, int chip)
{
    uint8_t r = 0xff;

    pclog("AHA154x: EEPROM cmd=%02x, arg=%02x len=%d, off=%02x\n",
					cmd, arg, len, off);

/* #ifdef EEP_SIZE */
    if (chip != CHIP_AHA154XB) {
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
    }
/* #endif */

    return(r);
}


static uint8_t
aha154x_memory(uint8_t cmd)
{
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


#define AHA_RESET_DURATION_NS UINT64_C(25000000)


/*
 * Auto SCSI structure which is located in host adapter RAM
 * and contains several configuration parameters.
 */
#pragma pack(push,1)
typedef struct {
    uint8_t	aInternalSignature[2];
    uint8_t	cbInformation;
    uint8_t	aHostAdaptertype[6];
    uint8_t	uReserved1;
    uint8_t	fFloppyEnabled		:1,
		fFloppySecondary	:1,
		fLevelSensitiveInterrupt:1,
		uReserved2		:2,
		uSystemRAMAreForBIOS	:3;
    uint8_t	uDMAChannel		:7,
		fDMAAutoConfiguration	:1,
		uIrqChannel		:7,
		fIrqAutoConfiguration	:1;
    uint8_t	uDMATransferRate;
    uint8_t	uSCSIId;
    uint8_t	fLowByteTerminated	:1,
		fParityCheckingEnabled	:1,
		fHighByteTerminated	:1,
		fNoisyCablingEnvironment:1,
		fFastSyncNegotiation	:1,
		fBusResetEnabled	:1,
		fReserved3		:1,
		fActiveNegotiationEna	:1;
    uint8_t	uBusOnDelay;
    uint8_t	uBusOffDelay;
    uint8_t	fHostAdapterBIOSEnabled	:1,
		fBIOSRedirectionOfInt19 :1,
		fExtendedTranslation	:1,
		fMapRemovableAsFixed	:1,
		fReserved4		:1,
		fBIOSMoreThan2Drives	:1,
		fBIOSInterruptMode	:1,
		fFlopticalSupport	:1;
    uint16_t	u16DeviceEnabledMask;
    uint16_t	u16WidePermittedMask;
    uint16_t	u16FastPermittedMask;
    uint16_t	u16SynchronousPermittedMask;
    uint16_t	u16DisconnectPermittedMask;
    uint16_t	u16SendStartUnitCommandMask;
    uint16_t	u16IgnoreInBIOSScanMask;
    unsigned char uPCIInterruptPin :                2;
    unsigned char uHostAdapterIoPortAddress :       2;
    uint8_t          fStrictRoundRobinMode :           1;
    uint8_t          fVesaBusSpeedGreaterThan33MHz :   1;
    uint8_t          fVesaBurstWrite :                 1;
    uint8_t          fVesaBurstRead :                  1;
    uint16_t      u16UltraPermittedMask;
    uint32_t      uReserved5;
    uint8_t       uReserved6;
    uint8_t       uAutoSCSIMaximumLUN;
    uint8_t          fReserved7 :                      1;
    uint8_t          fSCAMDominant :                   1;
    uint8_t          fSCAMenabled :                    1;
    uint8_t          fSCAMLevel2 :                     1;
    unsigned char uReserved8 :                      4;
    uint8_t          fInt13Extension :                 1;
    uint8_t          fReserved9 :                      1;
    uint8_t          fCDROMBoot :                      1;
    unsigned char uReserved10 :                     5;
    unsigned char uBootTargetId :                   4;
    unsigned char uBootChannel :                    4;
    uint8_t          fForceBusDeviceScanningOrder :    1;
    unsigned char uReserved11 :                     7;
    uint16_t      u16NonTaggedToAlternateLunPermittedMask;
    uint16_t      u16RenegotiateSyncAfterCheckConditionMask;
    uint8_t       aReserved12[10];
    uint8_t       aManufacturingDiagnostic[2];
    uint16_t      u16Checksum;
} AutoSCSIRam;
#pragma pack(pop)

/* The local RAM. */
#pragma pack(push,1)
typedef union {
    uint8_t u8View[256];		/* byte view */
    struct {				/* structured view */
        uint8_t     u8Bios[64];		/* offset 0 - 63 is for BIOS */
        AutoSCSIRam autoSCSIData;	/* Auto SCSI structure */
    } structured;
} HALocalRAM;
#pragma pack(pop)

/* Structure for the INQUIRE_SETUP_INFORMATION reply. */
#pragma pack(push,1)
typedef struct {
    uint8_t	uOffset		:4,
		uTransferPeriod :3,
		fSynchronous	:1;
} ReplyInquireSetupInformationSynchronousValue;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint8_t	fSynchronousInitiationEnabled	:1,
		fParityCheckingEnabled		:1,
		uReserved1			:6;
    uint8_t	uBusTransferRate;
    uint8_t	uPreemptTimeOnBus;
    uint8_t	uTimeOffBus;
    uint8_t	cMailbox;
    addr24	MailboxAddress;
    ReplyInquireSetupInformationSynchronousValue SynchronousValuesId0To7[8];
    uint8_t	uDisconnectPermittedId0To7;
    uint8_t	uSignature;
    uint8_t	uCharacterD;
    uint8_t	uHostBusType;
    uint8_t	uWideTransferPermittedId0To7;
    uint8_t	uWideTransfersActiveId0To7;
    ReplyInquireSetupInformationSynchronousValue SynchronousValuesId8To15[8];
    uint8_t	uDisconnectPermittedId8To15;
    uint8_t	uReserved2;
    uint8_t	uWideTransferPermittedId8To15;
    uint8_t	uWideTransfersActiveId8To15;
} ReplyInquireSetupInformation;
#pragma pack(pop)


#pragma pack(push,1)
typedef struct {
    uint8_t	Count;
    addr24	Address;
} MailboxInit_t;
#pragma pack(pop)


/*
 * Mailbox Definitions.
 *
 * Mailbox Out (MBO) command values.
 */
#define MBO_FREE                  0x00
#define MBO_START                 0x01
#define MBO_ABORT                 0x02

/* Mailbox In (MBI) status values. */
#define MBI_FREE                  0x00
#define MBI_SUCCESS               0x01
#define MBI_ABORT                 0x02
#define MBI_NOT_FOUND             0x03
#define MBI_ERROR                 0x04


#pragma pack(push,1)
typedef struct {
    uint8_t	CmdStatus;
    addr24	CCBPointer;
} Mailbox_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint32_t	CCBPointer;
    union {
	struct {
		uint8_t Reserved[3];
		uint8_t ActionCode;
	} out;
	struct {
		uint8_t HostStatus;
		uint8_t TargetStatus;
		uint8_t Reserved;
		uint8_t CompletionCode;
	} in;
    }		u;
} Mailbox32_t;
#pragma pack(pop)


/*
 *
 * CCB - SCSI Command Control Block
 *
 *    The CCB is a superset of the CDB (Command Descriptor Block)
 *    and specifies detailed information about a SCSI command.
 *
 */
/*    Byte 0    Command Control Block Operation Code */
#define SCSI_INITIATOR_COMMAND		0x00
#define TARGET_MODE_COMMAND		0x01
#define SCATTER_GATHER_COMMAND		0x02
#define SCSI_INITIATOR_COMMAND_RES	0x03
#define SCATTER_GATHER_COMMAND_RES	0x04
#define BUS_RESET			0x81

/*    Byte 1    Address and Direction Control */
#define CCB_TARGET_ID_SHIFT		0x06	/* CCB Op Code = 00, 02 */
#define CCB_INITIATOR_ID_SHIFT		0x06	/* CCB Op Code = 01 */
#define CCB_DATA_XFER_IN		0x01
#define CCB_DATA_XFER_OUT		0x02
#define CCB_LUN_MASK			0x07	/* Logical Unit Number */

/*    Byte 2    SCSI_Command_Length - Length of SCSI CDB
      Byte 3    Request Sense Allocation Length */
#define FOURTEEN_BYTES			0x00	/* Request Sense Buffer size */
#define NO_AUTO_REQUEST_SENSE		0x01	/* No Request Sense Buffer */

/*    Bytes 4, 5 and 6    Data Length		 - Data transfer byte count */
/*    Bytes 7, 8 and 9    Data Pointer		 - SGD List or Data Buffer */
/*    Bytes 10, 11 and 12 Link Pointer		 - Next CCB in Linked List */
/*    Byte 13   Command Link ID			 - TBD (I don't know yet) */
/*    Byte 14   Host Status			 - Host Adapter status */
#define CCB_COMPLETE			0x00	/* CCB completed without error */
#define CCB_LINKED_COMPLETE		0x0A	/* Linked command completed */
#define CCB_LINKED_COMPLETE_INT		0x0B	/* Linked complete with intr */
#define CCB_SELECTION_TIMEOUT		0x11	/* Set SCSI selection timed out */
#define CCB_DATA_OVER_UNDER_RUN		0x12
#define CCB_UNEXPECTED_BUS_FREE		0x13	/* Trg dropped SCSI BSY */
#define CCB_PHASE_SEQUENCE_FAIL		0x14	/* Trg bus phase sequence fail */
#define CCB_BAD_MBO_COMMAND		0x15	/* MBO command not 0, 1 or 2 */
#define CCB_INVALID_OP_CODE		0x16	/* CCB invalid operation code */
#define CCB_BAD_LINKED_LUN		0x17	/* Linked CCB LUN diff from 1st */
#define CCB_INVALID_DIRECTION		0x18	/* Invalid target direction */
#define CCB_DUPLICATE_CCB		0x19	/* Duplicate CCB */
#define CCB_INVALID_CCB			0x1A	/* Invalid CCB - bad parameter */

/*    Byte 15   Target Status

      See scsi.h files for these statuses.
      Bytes 16 and 17   Reserved (must be 0)
      Bytes 18 through 18+n-1, where n=size of CDB  Command Descriptor Block */

#pragma pack(push,1)
typedef struct {
    uint8_t	Opcode;
    uint8_t	Reserved1	:3,
		ControlByte	:2,
		TagQueued	:1,
		QueueTag	:2;
    uint8_t	CdbLength;
    uint8_t	RequestSenseLength;
    uint32_t	DataLength;
    uint32_t	DataPointer;
    uint8_t	Reserved2[2];
    uint8_t	HostStatus;
    uint8_t	TargetStatus;
    uint8_t	Id;
    uint8_t	Lun		:5,
		LegacyTagEnable	:1,
		LegacyQueueTag	:2;
    uint8_t	Cdb[12];
    uint8_t	Reserved3[6];
    uint32_t	SensePointer;
} CCB32;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint8_t	Opcode;
    uint8_t	Lun		:3,
		ControlByte	:2,
		Id		:3;
    uint8_t	CdbLength;
    uint8_t	RequestSenseLength;
    addr24	DataLength;
    addr24	DataPointer;
    addr24	LinkPointer;
    uint8_t	LinkId;
    uint8_t	HostStatus;
    uint8_t	TargetStatus;
    uint8_t	Reserved[2];
    uint8_t	Cdb[12];
} CCB;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint8_t	Opcode;
    uint8_t	Pad1		:3,
		ControlByte	:2,
		Pad2		:3;
    uint8_t	CdbLength;
    uint8_t	RequestSenseLength;
    uint8_t	Pad3[10];
    uint8_t	HostStatus;
    uint8_t	TargetStatus;
    uint8_t	Pad4[2];
    uint8_t	Cdb[12];
} CCBC;
#pragma pack(pop)

#pragma pack(push,1)
typedef union {
    CCB32	new;
    CCB		old;
    CCBC	common;
} CCBU;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    CCBU	CmdBlock;
    uint8_t	*RequestSenseBuffer;
    uint32_t	CCBPointer;
    int		Is24bit;
    uint8_t	TargetID;
    uint8_t	LUN;
    uint8_t	HostStatus;
    uint8_t	TargetStatus;
    uint8_t	MailboxCompletionCode;
} Req_t;
#pragma pack(pop)


#pragma pack(push,1)
typedef struct {
    rom_t	bios;
    int		UseLocalRAM;
    int		StrictRoundRobinMode;
    int		ExtendedLUNCCBFormat;
    HALocalRAM	LocalRAM;
    Req_t	Req;
    uint8_t	Status;
    uint8_t	Interrupt;
    uint8_t	Geometry;
    uint8_t	Control;
    uint8_t	Command;
    uint8_t	CmdBuf[53];
    uint8_t	CmdParam;
    uint8_t	CmdParamLeft;
    uint8_t	DataBuf[64];
    uint16_t	DataReply;
    uint16_t	DataReplyLeft;
    uint32_t	MailboxCount;
    uint32_t	MailboxOutAddr;
    uint32_t	MailboxOutPosCur;
    uint32_t	MailboxInAddr;
    uint32_t	MailboxInPosCur;
    int		Base;
    int		Irq;
    int		DmaChannel;
    int		IrqEnabled;
    int		Mbx24bit;
    int		MailboxOutInterrupts;
    int		MbiActive[256];
    int		PendingInterrupt;
    int		Lock;
    mem_mapping_t mmio_mapping;
    aha_info	aha;
    int		chip;
	uint8_t pos_regs[8];
} aha_t;
#pragma pack(pop)


static int	ResetCB = 0;
static int	AHA_Callback = 0;
static int	AHA_InOperation = 0;
static aha_t	*ResetDev;


static void
ClearIntr(aha_t *dev)
{
    dev->Interrupt = 0;
    pclog("AHA154X: lowering IRQ %i (stat 0x%02x)\n",
				dev->Irq, dev->Interrupt);
    picintc(1 << dev->Irq);
    if (dev->PendingInterrupt) {
	dev->Interrupt = dev->PendingInterrupt;
	pclog("AHA154X: Raising Interrupt 0x%02X (Pending)\n", dev->Interrupt);
	if (dev->MailboxOutInterrupts || !(dev->Interrupt & INTR_MBOA)) {
		if (dev->IrqEnabled)  picint(1 << dev->Irq);
	}
	dev->PendingInterrupt = 0;
    }
}


static void
RaiseIntr(aha_t *dev, uint8_t Interrupt)
{
    if (dev->Interrupt & INTR_HACC) {
	pclog("Pending IRQ\n");
	dev->PendingInterrupt = Interrupt;
    } else {
	dev->Interrupt = Interrupt;
	pclog("Raising IRQ %i\n", dev->Irq);
	if (dev->IrqEnabled)
		picint(1 << dev->Irq);
    }
}


static void
LocalRAM(aha_t *dev)
{
    /*
     * These values are mostly from what I think is right
     * looking at the dmesg output from a Linux guest inside
     * a VMware server VM.
     *
     * So they don't have to be right :)
     */
    memset(dev->LocalRAM.u8View, 0, sizeof(HALocalRAM));
    dev->LocalRAM.structured.autoSCSIData.fLevelSensitiveInterrupt = 1;
    dev->LocalRAM.structured.autoSCSIData.fParityCheckingEnabled = 1;
    dev->LocalRAM.structured.autoSCSIData.fExtendedTranslation = 1; /* Same as in geometry register. */
    dev->LocalRAM.structured.autoSCSIData.u16DeviceEnabledMask = UINT16_MAX; /* All enabled. Maybe mask out non present devices? */
    dev->LocalRAM.structured.autoSCSIData.u16WidePermittedMask = UINT16_MAX;
    dev->LocalRAM.structured.autoSCSIData.u16FastPermittedMask = UINT16_MAX;
    dev->LocalRAM.structured.autoSCSIData.u16SynchronousPermittedMask = UINT16_MAX;
    dev->LocalRAM.structured.autoSCSIData.u16DisconnectPermittedMask = UINT16_MAX;
    dev->LocalRAM.structured.autoSCSIData.fStrictRoundRobinMode = dev->StrictRoundRobinMode;
    dev->LocalRAM.structured.autoSCSIData.u16UltraPermittedMask = UINT16_MAX;
    /** @todo calculate checksum? */
}


static void
aha_reset(aha_t *dev)
{
    AHA_Callback = 0;
    ResetCB = 0;
    dev->Status = STAT_IDLE | STAT_INIT;
    dev->Geometry = 0x80;
    dev->Command = 0xFF;
    dev->CmdParam = 0;
    dev->CmdParamLeft = 0;
    dev->IrqEnabled = 1;
    dev->StrictRoundRobinMode = 0;
    dev->ExtendedLUNCCBFormat = 0;
    dev->MailboxOutPosCur = 0;
    dev->MailboxInPosCur = 0;
    dev->MailboxOutInterrupts = 0;
    dev->PendingInterrupt = 0;
    dev->Lock = 0;
    AHA_InOperation = 0;

    ClearIntr(dev);

    LocalRAM(dev);
}


static void
aha_reset_ctrl(aha_t *dev, uint8_t Reset)
{
    aha_reset(dev);
    if (Reset) {
	dev->Status |= STAT_STST;
	dev->Status &= ~STAT_IDLE;
    }
    ResetCB = AHA_RESET_DURATION_NS * TIMER_USEC;
}


static void
aha_cmd_done(aha_t *dev)
{
    dev->DataReply = 0;
    dev->Status |= STAT_IDLE;
				
    if ((dev->Command != 0x02) && (dev->Command != 0x82)) {
	dev->Status &= ~STAT_DFULL;
	dev->Interrupt = (INTR_ANY | INTR_HACC);
	pclog("Raising IRQ %i\n", dev->Irq);
	if (dev->IrqEnabled)
		picint(1 << dev->Irq);
    }

    dev->Command = 0xFF;
    dev->CmdParam = 0;
}


static void
aha_mbi_setup(aha_t *dev, uint32_t CCBPointer, CCBU *CmdBlock,
	      uint8_t HostStatus, uint8_t TargetStatus, uint8_t mbcc)
{
    Req_t *req = &dev->Req;

    req->CCBPointer = CCBPointer;
    memcpy(&(req->CmdBlock), CmdBlock, sizeof(CCB32));
    req->Is24bit = dev->Mbx24bit;
    req->HostStatus = HostStatus;
    req->TargetStatus = TargetStatus;
    req->MailboxCompletionCode = mbcc;

    pclog("Mailbox in setup\n");

    AHA_InOperation = 2;
}


static void
aha_mbi(aha_t *dev)
{	
    Req_t *req = &dev->Req;
    uint32_t CCBPointer = req->CCBPointer;
    CCBU *CmdBlock = &(req->CmdBlock);
    uint8_t HostStatus = req->HostStatus;
    uint8_t TargetStatus = req->TargetStatus;
    uint8_t MailboxCompletionCode = req->MailboxCompletionCode;
    Mailbox32_t Mailbox32;
    Mailbox_t MailboxIn;
    uint32_t Incoming;

    Mailbox32.CCBPointer = CCBPointer;
    Mailbox32.u.in.HostStatus = HostStatus;
    Mailbox32.u.in.TargetStatus = TargetStatus;
    Mailbox32.u.in.CompletionCode = MailboxCompletionCode;

    Incoming = dev->MailboxInAddr + (dev->MailboxInPosCur * (dev->Mbx24bit ? sizeof(Mailbox_t) : sizeof(Mailbox32_t)));

    if (MailboxCompletionCode != MBI_NOT_FOUND) {
	CmdBlock->common.HostStatus = HostStatus;
	CmdBlock->common.TargetStatus = TargetStatus;		
		
	/* Rewrite the CCB up to the CDB. */
	pclog("CCB rewritten to the CDB (pointer %08X, length %i)\n", CCBPointer, offsetof(CCBC, Cdb));
	DMAPageWrite(CCBPointer, (char *)CmdBlock, offsetof(CCBC, Cdb));
    } else {
	pclog("Mailbox not found!\n");
    }

    pclog("Host Status 0x%02X, Target Status 0x%02X\n",HostStatus,TargetStatus);

    if (dev->Mbx24bit) {
	MailboxIn.CmdStatus = Mailbox32.u.in.CompletionCode;
	U32_TO_ADDR(MailboxIn.CCBPointer, Mailbox32.CCBPointer);
	pclog("Mailbox 24-bit: Status=0x%02X, CCB at 0x%04X\n", MailboxIn.CmdStatus, ADDR_TO_U32(MailboxIn.CCBPointer));

	DMAPageWrite(Incoming, (char *)&MailboxIn, sizeof(Mailbox_t));
	pclog("%i bytes of 24-bit mailbox written to: %08X\n", sizeof(Mailbox_t), Incoming);
    } else {
	pclog("Mailbox 32-bit: Status=0x%02X, CCB at 0x%04X\n", Mailbox32.u.in.CompletionCode, Mailbox32.CCBPointer);

	DMAPageWrite(Incoming, (char *)&Mailbox32, sizeof(Mailbox32_t));		
	pclog("%i bytes of 32-bit mailbox written to: %08X\n", sizeof(Mailbox32_t), Incoming);
    }

    dev->MailboxInPosCur++;
    if (dev->MailboxInPosCur >= dev->MailboxCount)
		dev->MailboxInPosCur = 0;

    RaiseIntr(dev, INTR_MBIF | INTR_ANY);

    AHA_InOperation = 0;
}


static void
aha_rd_sge(int Is24bit, uint32_t SGList, uint32_t Entries, SGE32 *SG)
{
    uint32_t i;
    SGE SGE24[MAX_SG_DESCRIPTORS];

    if (Is24bit) {
	DMAPageRead(SGList, (char *)&SGE24, Entries * sizeof(SGE));

	for (i=0;i<Entries;++i) {
		/* Convert the 24-bit entries into 32-bit entries. */
		SG[i].Segment = ADDR_TO_U32(SGE24[i].Segment);
		SG[i].SegmentPointer = ADDR_TO_U32(SGE24[i].SegmentPointer);
	}
    } else {
	DMAPageRead(SGList, (char *)SG, Entries * sizeof(SGE32));		
    }
}


static void
aha_buf_alloc(Req_t *req, int Is24bit)
{
    uint32_t sg_buffer_pos = 0;
    uint32_t DataPointer, DataLength;
    uint32_t SGEntryLength = (Is24bit ? sizeof(SGE) : sizeof(SGE32));
    uint32_t Address;

    if (Is24bit) {
	DataPointer = ADDR_TO_U32(req->CmdBlock.old.DataPointer);
	DataLength = ADDR_TO_U32(req->CmdBlock.old.DataLength);
    } else {
	DataPointer = req->CmdBlock.new.DataPointer;
	DataLength = req->CmdBlock.new.DataLength;		
    }
    pclog("Data Buffer write: length %d, pointer 0x%04X\n",
				DataLength, DataPointer);	

    if (SCSIDevices[req->TargetID][req->LUN].CmdBuffer != NULL)
    {
	free(SCSIDevices[req->TargetID][req->LUN].CmdBuffer);
	SCSIDevices[req->TargetID][req->LUN].CmdBuffer = NULL;
    }

    if ((req->CmdBlock.common.ControlByte != 0x03) && DataLength) {
	if (req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND ||
	    req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES) {
		uint32_t SGRead;
		uint32_t ScatterEntry;
		SGE32 SGBuffer[MAX_SG_DESCRIPTORS];
		uint32_t SGLeft = DataLength / SGEntryLength;
		uint32_t SGAddrCurrent = DataPointer;
		uint32_t DataToTransfer = 0;
			
		do {
			SGRead = (SGLeft < ELEMENTS(SGBuffer)) ? SGLeft : ELEMENTS(SGBuffer);
			SGLeft -= SGRead;

			aha_rd_sge(Is24bit, SGAddrCurrent, SGRead, SGBuffer);

			for (ScatterEntry = 0; ScatterEntry < SGRead; ScatterEntry++) {
				pclog("BusLogic S/G Write: ScatterEntry=%u\n", ScatterEntry);

				Address = SGBuffer[ScatterEntry].SegmentPointer;
				DataToTransfer += SGBuffer[ScatterEntry].Segment;

				pclog("BusLogic S/G Write: Address=%08X DatatoTransfer=%u\n", Address, DataToTransfer);
			}

			SGAddrCurrent += SGRead * SGEntryLength;
		} while (SGLeft > 0);

		pclog("Data to transfer (S/G) %d\n", DataToTransfer);

		SCSIDevices[req->TargetID][req->LUN].InitLength = DataToTransfer;

		pclog("Allocating buffer for Scatter/Gather (%i bytes)\n", DataToTransfer);
		SCSIDevices[req->TargetID][req->LUN].CmdBuffer = (uint8_t *) malloc(DataToTransfer);
		memset(SCSIDevices[req->TargetID][req->LUN].CmdBuffer, 0, DataToTransfer);

		/* If the control byte is 0x00, it means that the transfer direction is set up by the SCSI command without
		   checking its length, so do this procedure for both no read/write commands. */
		if ((req->CmdBlock.common.ControlByte == CCB_DATA_XFER_OUT) ||
		    (req->CmdBlock.common.ControlByte == 0x00)) {
			SGLeft = DataLength / SGEntryLength;
			SGAddrCurrent = DataPointer;

			do {
				SGRead = (SGLeft < ELEMENTS(SGBuffer)) ? SGLeft : ELEMENTS(SGBuffer);
				SGLeft -= SGRead;

				aha_rd_sge(Is24bit, SGAddrCurrent,
						      SGRead, SGBuffer);

				for (ScatterEntry = 0; ScatterEntry < SGRead; ScatterEntry++) {
					pclog("BusLogic S/G Write: ScatterEntry=%u\n", ScatterEntry);

					Address = SGBuffer[ScatterEntry].SegmentPointer;
					DataToTransfer = SGBuffer[ScatterEntry].Segment;

					pclog("BusLogic S/G Write: Address=%08X DatatoTransfer=%u\n", Address, DataToTransfer);

					DMAPageRead(Address, (char *)SCSIDevices[req->TargetID][req->LUN].CmdBuffer + sg_buffer_pos, DataToTransfer);
					sg_buffer_pos += DataToTransfer;
				}

				SGAddrCurrent += SGRead * (Is24bit ? sizeof(SGE) : sizeof(SGE32));
			} while (SGLeft > 0);				
		}
	} else if (req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND ||
		   req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES) {
			Address = DataPointer;

			SCSIDevices[req->TargetID][req->LUN].InitLength = DataLength;

			pclog("Allocating buffer for direct transfer (%i bytes)\n", DataLength);
			SCSIDevices[req->TargetID][req->LUN].CmdBuffer = (uint8_t *) malloc(DataLength);
			memset(SCSIDevices[req->TargetID][req->LUN].CmdBuffer, 0, DataLength);

			if (DataLength > 0) {
				DMAPageRead(Address,
					    (char *)SCSIDevices[req->TargetID][req->LUN].CmdBuffer,
					    SCSIDevices[req->TargetID][req->LUN].InitLength);
			}
	}
    }
}


static void
aha_buf_free(Req_t *req)
{
    SGE32 SGBuffer[MAX_SG_DESCRIPTORS];
    uint32_t DataPointer = 0;
    uint32_t DataLength = 0;
    uint32_t sg_buffer_pos = 0;
    uint32_t SGRead;
    uint32_t ScatterEntry;
    uint32_t SGEntrySize;
    uint32_t SGLeft;
    uint32_t SGAddrCurrent;
    uint32_t Address;
    uint32_t Residual;
    uint32_t DataToTransfer;

    if (req->Is24bit) {
	DataPointer = ADDR_TO_U32(req->CmdBlock.old.DataPointer);
	DataLength = ADDR_TO_U32(req->CmdBlock.old.DataLength);
    } else {
	DataPointer = req->CmdBlock.new.DataPointer;
	DataLength = req->CmdBlock.new.DataLength;		
    }

    if ((DataLength != 0) && (req->CmdBlock.common.Cdb[0] == GPCMD_TEST_UNIT_READY)) {
	pclog("Data length not 0 with TEST UNIT READY: %i (%i)\n",
		DataLength, SCSIDevices[req->TargetID][req->LUN].InitLength);
    }

    if (req->CmdBlock.common.Cdb[0] == GPCMD_TEST_UNIT_READY) {
	DataLength = 0;
    }

    pclog("Data Buffer read: length %d, pointer 0x%04X\n",
				DataLength, DataPointer);

    /* If the control byte is 0x00, it means that the transfer direction is set up by the SCSI command without
       checking its length, so do this procedure for both read/write commands. */
    if ((DataLength > 0) &&
        ((req->CmdBlock.common.ControlByte == CCB_DATA_XFER_IN) ||
	(req->CmdBlock.common.ControlByte == 0x00))) {	
	if ((req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND) ||
	    (req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES)) {
		SGEntrySize = (req->Is24bit ? sizeof(SGE) : sizeof(SGE32));			
		SGLeft = DataLength / SGEntrySize;
		SGAddrCurrent = DataPointer;

		do {
			SGRead = (SGLeft < ELEMENTS(SGBuffer)) ? SGLeft : ELEMENTS(SGBuffer);
			SGLeft -= SGRead;

			aha_rd_sge(req->Is24bit, SGAddrCurrent,
					      SGRead, SGBuffer);

			for (ScatterEntry = 0; ScatterEntry < SGRead; ScatterEntry++) {
				pclog("BusLogic S/G: ScatterEntry=%u\n", ScatterEntry);

				Address = SGBuffer[ScatterEntry].SegmentPointer;
				DataToTransfer = SGBuffer[ScatterEntry].Segment;

				pclog("BusLogic S/G: Writing %i bytes at %08X\n", DataToTransfer, Address);

				DMAPageWrite(Address, (char *)SCSIDevices[req->TargetID][req->LUN].CmdBuffer + sg_buffer_pos, DataToTransfer);
				sg_buffer_pos += DataToTransfer;
			}

			SGAddrCurrent += (SGRead * SGEntrySize);
		} while (SGLeft > 0);
	} else if (req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND ||
		   req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES) {
		Address = DataPointer;

		pclog("BusLogic DMA: Writing %i bytes at %08X\n", DataLength, Address);
		DMAPageWrite(Address, (char *)SCSIDevices[req->TargetID][req->LUN].CmdBuffer, DataLength);
	}
    }

    if ((req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES) ||
	(req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES)) {
	/* Should be 0 when scatter/gather? */
	if (DataLength >= SCSIDevices[req->TargetID][req->LUN].InitLength) {
		Residual = DataLength;
		Residual -= SCSIDevices[req->TargetID][req->LUN].InitLength;
	} else {
		Residual = 0;
	}

	if (req->Is24bit) {
		U32_TO_ADDR(req->CmdBlock.old.DataLength, Residual);
		pclog("24-bit Residual data length for reading: %d\n",
			ADDR_TO_U32(req->CmdBlock.old.DataLength));
	} else {
		req->CmdBlock.new.DataLength = Residual;
		pclog("32-bit Residual data length for reading: %d\n",
				req->CmdBlock.new.DataLength);
	}
    }

    if (SCSIDevices[req->TargetID][req->LUN].CmdBuffer != NULL)
    {
	free(SCSIDevices[req->TargetID][req->LUN].CmdBuffer);
	SCSIDevices[req->TargetID][req->LUN].CmdBuffer = NULL;
    }
}


static uint8_t
aha_read(uint16_t port, void *priv)
{
    aha_t *dev = (aha_t *)priv;
    uint8_t ret;

    switch (port & 3) {
	case 0:
	default:
		ret = dev->Status;
		break;
		
	case 1:
		if (dev->UseLocalRAM)
			ret = dev->LocalRAM.u8View[dev->DataReply];
		else
			ret = dev->DataBuf[dev->DataReply];
		if (dev->DataReplyLeft) {
			dev->DataReply++;
			dev->DataReplyLeft--;
			if (! dev->DataReplyLeft)
				aha_cmd_done(dev);
		}
		break;
		
	case 2:
		ret = dev->Interrupt;
		break;
		
	case 3:
		ret = dev->Geometry;
		break;
    }

    if (port < 0x1000) {
	pclog("AHA154X: Read Port 0x%02X, Returned Value %02X\n", port, ret);
    }

    return(ret);
}


static uint16_t
aha_readw(uint16_t port, void *priv)
{
    return aha_read(port, priv);
}


static void
aha_write(uint16_t port, uint8_t val, void *priv)
{
    int i = 0;
    uint8_t j = 0;
    aha_t *dev = (aha_t *)priv;
    uint8_t Offset;
    MailboxInit_t *MailboxInit;
    BIOSCMD *BiosCmd;
    ReplyInquireSetupInformation *ReplyISI;
    uint16_t cyl = 0;
    uint8_t temp = 0;

    pclog("AHA154X: Write Port 0x%02X, Value %02X\n", port, val);

    switch (port & 3) {
	case 0:
		if ((val & CTRL_HRST) || (val & CTRL_SRST)) {	
			uint8_t Reset = (val & CTRL_HRST);
			pclog("Reset completed = %x\n", Reset);
			aha_reset_ctrl(dev, Reset);
			break;
		}
		
		if (val & CTRL_IRST) {
			ClearIntr(dev);
		}
		break;

	case 1:
		/* Fast path for the mailbox execution command. */
		if (((val == 0x02) || (val == 0x82)) &&
		    (dev->Command == 0xFF)) {
			/* If there are no mailboxes configured, don't even try to do anything. */
			if (dev->MailboxCount) {
				if (!AHA_Callback) {
					AHA_Callback = 1 * TIMER_USEC;
				}
			}
			return;
		}

		if (dev->Command == 0xFF) {
			dev->Command = val;
			dev->CmdParam = 0;
			dev->CmdParamLeft = 0;
			
			dev->Status &= ~(STAT_INVCMD | STAT_IDLE);
			pclog("AHA154X: Operation Code 0x%02X\n", val);
			switch (dev->Command) {
				case 0x01:
					dev->CmdParamLeft = sizeof(MailboxInit_t);
					break;
				
				case 0x03:		/* Exec BIOS Command */
					dev->CmdParamLeft = 10;
					break;

				case 0x25:
					/* Same as 0x01 for AHA. */
					dev->CmdParamLeft = sizeof(MailboxInit_t);
					break;

				case 0x05:
				case 0x07:
				case 0x08:
				case 0x09:
				case 0x0D:
				case 0x1F:
				case 0x21:
				case 0x24:
					dev->CmdParamLeft = 1;
					break;	

				case 0x06:
					dev->CmdParamLeft = 4;
					break;

				case 0x1C:
				case 0x1D:
					dev->CmdParamLeft = 3;
					break;

				case 0x22:		/* write EEPROM */
					dev->CmdParamLeft = 3+32;
					break;

				case 0x23:		/* read EEPROM */
					dev->CmdParamLeft = 3;
					break;

				case 0x29:
					dev->CmdParamLeft = 2;
					break;

				case 0x91:
					dev->CmdParamLeft = 2;
					break;
			}
		} else {
			dev->CmdBuf[dev->CmdParam] = val;
			dev->CmdParam++;
			dev->CmdParamLeft--;
		}
		
		if (! dev->CmdParamLeft) {
			pclog("Running Operation Code 0x%02X\n", dev->Command);
			switch (dev->Command) {
				case 0x00:
					dev->DataReplyLeft = 0;
					break;

				case 0x01:
aha_0x01:
				{
					dev->Mbx24bit = 1;
							
					MailboxInit = (MailboxInit_t *)dev->CmdBuf;

					dev->MailboxCount = MailboxInit->Count;
					dev->MailboxOutAddr = ADDR_TO_U32(MailboxInit->Address);
					dev->MailboxInAddr = dev->MailboxOutAddr + (dev->MailboxCount * sizeof(Mailbox_t));
						
					pclog("Initialize Mailbox Command\n");
					pclog("Mailbox Out Address=0x%08X\n", dev->MailboxOutAddr);
					pclog("Mailbox In Address=0x%08X\n", dev->MailboxInAddr);
					pclog("Initialized Mailbox, %d entries at 0x%08X\n", MailboxInit->Count, ADDR_TO_U32(MailboxInit->Address));
						
					dev->Status &= ~STAT_INIT;
					dev->DataReplyLeft = 0;
				}
				break;

				case 0x03:
					BiosCmd = (BIOSCMD *)dev->CmdBuf;

					cyl = ((BiosCmd->cylinder & 0xff) << 8) | ((BiosCmd->cylinder >> 8) & 0xff);
					BiosCmd->cylinder = cyl;						
					temp = BiosCmd->id;
					BiosCmd->id = BiosCmd->lun;
					BiosCmd->lun = temp;
					pclog("C: %04X, H: %02X, S: %02X\n", BiosCmd->cylinder, BiosCmd->head, BiosCmd->sector);
					dev->DataBuf[0] = HACommand03Handler(7, BiosCmd);
					pclog("BIOS Completion/Status Code %x\n", dev->DataBuf[0]);
					dev->DataReplyLeft = 1;
					break;

				case 0x04:
					dev->DataBuf[0] = (dev->chip != CHIP_AHA1640) ? dev->aha.bid : 0x42;
					dev->DataBuf[1] = (dev->chip != CHIP_AHA1640) ? 0x30 : 0x42;
					dev->DataBuf[2] = dev->aha.fwh;
					dev->DataBuf[3] = dev->aha.fwl;
					dev->DataReplyLeft = 4;
					break;

				case 0x05:
					if (dev->CmdBuf[0] <= 1) {
						dev->MailboxOutInterrupts = dev->CmdBuf[0];
						pclog("Mailbox out interrupts: %s\n", dev->MailboxOutInterrupts ? "ON" : "OFF");
					} else {
						dev->Status |= STAT_INVCMD;
					}
					dev->DataReplyLeft = 0;
					break;

				case 0x06:
					dev->DataReplyLeft = 0;
					break;
						
				case 0x07:
					dev->DataReplyLeft = 0;
					dev->LocalRAM.structured.autoSCSIData.uBusOnDelay = dev->CmdBuf[0];
					pclog("Bus-on time: %d\n", dev->CmdBuf[0]);
					break;
						
				case 0x08:
					dev->DataReplyLeft = 0;
					dev->LocalRAM.structured.autoSCSIData.uBusOffDelay = dev->CmdBuf[0];
					pclog("Bus-off time: %d\n", dev->CmdBuf[0]);
					break;
						
				case 0x09:
					dev->DataReplyLeft = 0;
					dev->LocalRAM.structured.autoSCSIData.uDMATransferRate = dev->CmdBuf[0];
					pclog("DMA transfer rate: %02X\n", dev->CmdBuf[0]);
					break;

				case 0x0A:
					memset(dev->DataBuf, 0, 8);
					for (i=0; i<7; i++) {
					    dev->DataBuf[i] = 0;
					    for (j=0; j<8; j++) {
						if (SCSIDevices[i][j].LunType != SCSI_NONE)
						    dev->DataBuf[i] |= (1<<j);
					    }
					}
					dev->DataBuf[7] = 0;
					dev->DataReplyLeft = 8;
					break;				

				case 0x0B:
					dev->DataBuf[0] = (1<<dev->DmaChannel);
					if (dev->Irq >= 8)
					    dev->DataBuf[1]=(1<<(dev->Irq-9));
					else
					    dev->DataBuf[1]=(1<<dev->Irq);
					dev->DataBuf[2] = 7;	/* HOST ID */
					dev->DataReplyLeft = 3;
					break;

				case 0x0D:
				{
					dev->DataReplyLeft = dev->CmdBuf[0];

					ReplyISI = (ReplyInquireSetupInformation *)dev->DataBuf;
					memset(ReplyISI, 0, sizeof(ReplyInquireSetupInformation));
					
					ReplyISI->fSynchronousInitiationEnabled = 1;
					ReplyISI->fParityCheckingEnabled = 1;
					ReplyISI->cMailbox = dev->MailboxCount;
					U32_TO_ADDR(ReplyISI->MailboxAddress, dev->MailboxOutAddr);
					pclog("Return Setup Information: %d\n", dev->CmdBuf[0]);
				}
				break;

				case 0x1C:
				{
					uint32_t FIFOBuf;
					addr24 Address;
					
					dev->DataReplyLeft = 0;
					Address.hi = dev->CmdBuf[0];
					Address.mid = dev->CmdBuf[1];
					Address.lo = dev->CmdBuf[2];
					FIFOBuf = ADDR_TO_U32(Address);
					DMAPageRead(FIFOBuf, (char *)&dev->LocalRAM.u8View[64], 64);
				}
				break;
						
				case 0x1D:
				{
					uint32_t FIFOBuf;
					addr24 Address;
					
					dev->DataReplyLeft = 0;
					Address.hi = dev->CmdBuf[0];
					Address.mid = dev->CmdBuf[1];
					Address.lo = dev->CmdBuf[2];
					FIFOBuf = ADDR_TO_U32(Address);
					pclog("FIFO: Writing 64 bytes at %08X\n", FIFOBuf);
					DMAPageWrite(FIFOBuf, (char *)&dev->LocalRAM.u8View[64], 64);
				}
				break;	
						
				case 0x1F:
					dev->DataBuf[0] = dev->CmdBuf[0];
					dev->DataReplyLeft = 1;
					break;

				case 0x21:
					if (dev->CmdParam == 1)
						dev->CmdParamLeft = dev->CmdBuf[0];
					dev->DataReplyLeft = 0;
					break;

				case 0x22:	/* write EEPROM */
					/* Sent by CF BIOS. */
					dev->DataReplyLeft =
					    aha154x_eeprom(dev->Command,
						   dev->CmdBuf[0],
						   dev->CmdBuf[1],
						   dev->CmdBuf[2],
						   dev->DataBuf,
						   dev->chip);
					if (dev->DataReplyLeft == 0xff) {
						dev->DataReplyLeft = 0;
						dev->Status |= STAT_INVCMD;
					}
					break;

				case 0x23:
					dev->DataReplyLeft =
					    aha154x_eeprom(dev->Command,
						   dev->CmdBuf[0],
						   dev->CmdBuf[1],
						   dev->CmdBuf[2],
						   dev->DataBuf,
						   dev->chip);
					if (dev->DataReplyLeft == 0xff) {
						dev->DataReplyLeft = 0;
						dev->Status |= STAT_INVCMD;
					}
					break;

				case 0x24:
					/*
					 * For AHA1542CF, this is the command
					 * to play with the Shadow RAM.  BIOS
					 * gives us one argument (00,02,03)
					 * and expects a 0x04 back in the INTR
					 * register.  --FvK
					 */
					dev->Interrupt = aha154x_shram(val, dev->chip);
					break;

				case 0x25:
					goto aha_0x01;

				case 0x26:	/* AHA memory mapper */
				case 0x27:	/* AHA memory mapper */
					dev->DataReplyLeft =
					    aha154x_memory(dev->Command);
					break;

				case 0x28:
					dev->DataBuf[0] = 0x08;
					dev->DataBuf[1] = dev->Lock;
					dev->DataReplyLeft = 2;
					break;
				case 0x29:
					dev->DataReplyLeft = 0;
					if (dev->CmdBuf[1] == dev->Lock) {
						if (dev->CmdBuf[0] & 1) {
							dev->Lock = 1;
						} else {
							dev->Lock = 0;
						}
					}
					break;

				case 0x2C:	/* AHA-1542CP sends this */
					dev->DataBuf[0] = 0x00;
					dev->DataReplyLeft = 1;
					break;

				case 0x33:	/* AHA-1542CP sends this */
					dev->DataBuf[0] = 0x00;
					dev->DataBuf[1] = 0x00;
					dev->DataBuf[2] = 0x00;
					dev->DataBuf[3] = 0x00;
					dev->DataReplyLeft = 256;
					break;

				case 0x91:
					Offset = dev->CmdBuf[0];
					dev->DataReplyLeft = dev->CmdBuf[1];
							
					dev->UseLocalRAM = 1;
					dev->DataReply = Offset;
					break;

				default:
					dev->DataReplyLeft = 0;
					dev->Status |= STAT_INVCMD;
					break;
			}
		}
		
		if (dev->DataReplyLeft)
			dev->Status |= STAT_DFULL;
		else if (!dev->CmdParamLeft)
			aha_cmd_done(dev);
		break;
		
	case 2:
		break;
		
	case 3:
		break;
    }
}


static void
aha_writew(uint16_t Port, uint16_t Val, void *p)
{
    aha_write(Port, Val & 0xFF, p);
}


static uint8_t
ConvertSenseLength(uint8_t RequestSenseLength)
{
    pclog("Unconverted Request Sense length %i\n", RequestSenseLength);

    if (RequestSenseLength == 0)
	RequestSenseLength = 14;
    else if (RequestSenseLength == 1)
	RequestSenseLength = 0;

    pclog("Request Sense length %i\n", RequestSenseLength);

    return(RequestSenseLength);
}


static void
SenseBufferFree(Req_t *req, int Copy, int is_hd)
{
    uint8_t SenseLength = ConvertSenseLength(req->CmdBlock.common.RequestSenseLength);
    uint8_t cdrom_id = scsi_cdrom_drives[req->TargetID][req->LUN];
    uint8_t hdc_id = scsi_hard_disks[req->TargetID][req->LUN];
    uint32_t SenseBufferAddress;
    uint8_t temp_sense[256];

    if (SenseLength && Copy) {
	if (is_hd)
	{
		scsi_hd_request_sense_for_scsi(hdc_id, temp_sense, SenseLength);
	}
	else
	{
		cdrom_request_sense_for_scsi(cdrom_id, temp_sense, SenseLength);
	}

	/*
	 * The sense address, in 32-bit mode, is located in the
	 * Sense Pointer of the CCB, but in 24-bit mode, it is
	 * located at the end of the Command Descriptor Block.
	 */
	if (req->Is24bit) {
		SenseBufferAddress = req->CCBPointer;
		SenseBufferAddress += req->CmdBlock.common.CdbLength + offsetof(CCB, Cdb);
	} else {
		SenseBufferAddress = req->CmdBlock.new.SensePointer;
	}

	pclog("Request Sense address: %02X\n", SenseBufferAddress);

	pclog("SenseBufferFree(): Writing %i bytes at %08X\n",
					SenseLength, SenseBufferAddress);
	DMAPageWrite(SenseBufferAddress, (char *)temp_sense, SenseLength);
	pclog("Sense data written to buffer: %02X %02X %02X\n",
		temp_sense[2], temp_sense[12], temp_sense[13]);
    }
}


static void
aha_disk_cmd(aha_t *dev)
{
    Req_t *req = &dev->Req;
    uint8_t Id, Lun;
    uint8_t hdc_id;
    uint8_t hd_phase;
    uint8_t temp_cdb[12];
    uint32_t i;

    Id = req->TargetID;
    Lun = req->LUN;
    hdc_id = scsi_hard_disks[Id][Lun];

    if (hdc_id == 0xff)  fatal("SCSI hard disk on %02i:%02i has disappeared\n", Id, Lun);

    pclog("SCSI HD command being executed on: SCSI ID %i, SCSI LUN %i, HD %i\n",
							Id, Lun, hdc_id);

    pclog("SCSI Cdb[0]=0x%02X\n", req->CmdBlock.common.Cdb[0]);
    for (i = 1; i < req->CmdBlock.common.CdbLength; i++) {
	pclog("SCSI Cdb[%i]=%i\n", i, req->CmdBlock.common.Cdb[i]);
    }

    memset(temp_cdb, 0, 12);
    if (req->CmdBlock.common.CdbLength <= 12) {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb,
		req->CmdBlock.common.CdbLength);
    } else {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb, 12);
    }

    /*
     * Since that field in the HDC struct is never used when
     * the bus type is SCSI, let's use it for this scope.
     */
    shdc[hdc_id].request_length = temp_cdb[1];

    if (req->CmdBlock.common.CdbLength != 12) {
	/*
	 * Make sure the LUN field of the temporary CDB is always 0,
	 * otherwise Daemon Tools drives will misbehave when a command
	 * is passed through to them.
	 */
	temp_cdb[1] &= 0x1f;
    }

    /* Finally, execute the SCSI command immediately and get the transfer length. */
    SCSIPhase = SCSI_PHASE_COMMAND;
    scsi_hd_command(hdc_id, temp_cdb);
    SCSIStatus = scsi_hd_err_stat_to_scsi(hdc_id);
    if (SCSIStatus == SCSI_STATUS_OK) {
	hd_phase = scsi_hd_phase_to_scsi(hdc_id);
	if (hd_phase == 2) {
		/* Command completed - call the phase callback to complete the command. */
		scsi_hd_callback(hdc_id);
	} else {
		/* Command first phase complete - call the callback to execute the second phase. */
		scsi_hd_callback(hdc_id);
		SCSIStatus = scsi_hd_err_stat_to_scsi(hdc_id);
		/* Command second phase complete - call the callback to complete the command. */
		scsi_hd_callback(hdc_id);
	}
    } else {
	/* Error (Check Condition) - call the phase callback to complete the command. */
	scsi_hd_callback(hdc_id);
    }

    pclog("SCSI Status: %s, Sense: %02X, ASC: %02X, ASCQ: %02X\n", (SCSIStatus == SCSI_STATUS_OK) ? "OK" : "CHECK CONDITION", shdc[hdc_id].sense[2], shdc[hdc_id].sense[12], shdc[hdc_id].sense[13]);

    aha_buf_free(req);

    SenseBufferFree(req, (SCSIStatus != SCSI_STATUS_OK), 1);

    pclog("Request complete\n");

    if (SCSIStatus == SCSI_STATUS_OK) {
	aha_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
			       CCB_COMPLETE, SCSI_STATUS_OK, MBI_SUCCESS);
    } else if (SCSIStatus == SCSI_STATUS_CHECK_CONDITION) {
	aha_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
			CCB_COMPLETE, SCSI_STATUS_CHECK_CONDITION, MBI_ERROR);
    }
}


static void
aha_cdrom_cmd(aha_t *dev)
{
    Req_t *req = &dev->Req;
    uint8_t Id, Lun;
    uint8_t cdrom_id;
    uint8_t cdrom_phase;
    uint8_t temp_cdb[12];
    uint32_t i;

    Id = req->TargetID;
    Lun = req->LUN;
    cdrom_id = scsi_cdrom_drives[Id][Lun];

    if (cdrom_id == 0xff)  fatal("SCSI CD-ROM on %02i:%02i has disappeared\n", Id, Lun);

    pclog("CD-ROM command being executed on: SCSI ID %i, SCSI LUN %i, CD-ROM %i\n",
							Id, Lun, cdrom_id);

    pclog("SCSI Cdb[0]=0x%02X\n", req->CmdBlock.common.Cdb[0]);
    for (i = 1; i < req->CmdBlock.common.CdbLength; i++) {
	pclog("SCSI Cdb[%i]=%i\n", i, req->CmdBlock.common.Cdb[i]);
    }

    memset(temp_cdb, 0, cdrom[cdrom_id].cdb_len);
    if (req->CmdBlock.common.CdbLength <= cdrom[cdrom_id].cdb_len) {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb,
		req->CmdBlock.common.CdbLength);
    } else {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb, cdrom[cdrom_id].cdb_len);
    }

    /*
     * Since that field in the CDROM struct is never used when
     * the bus type is SCSI, let's use it for this scope.
     */
    cdrom[cdrom_id].request_length = temp_cdb[1];

    if (req->CmdBlock.common.CdbLength != 12) {
	/*
	 * Make sure the LUN field of the temporary CDB is always 0,
	 * otherwise Daemon Tools drives will misbehave when a command
	 * is passed through to them.
	 */
	temp_cdb[1] &= 0x1f;
    }

    /* Finally, execute the SCSI command immediately and get the transfer length. */
    SCSIPhase = SCSI_PHASE_COMMAND;
    cdrom_command(cdrom_id, temp_cdb);
    SCSIStatus = cdrom_CDROM_PHASE_to_scsi(cdrom_id);
    if (SCSIStatus == SCSI_STATUS_OK) {
	cdrom_phase = cdrom_atapi_phase_to_scsi(cdrom_id);
	if (cdrom_phase == 2) {
		/* Command completed - call the phase callback to complete the command. */
		cdrom_phase_callback(cdrom_id);
	} else {
		/* Command first phase complete - call the callback to execute the second phase. */
		cdrom_phase_callback(cdrom_id);
		SCSIStatus = cdrom_CDROM_PHASE_to_scsi(cdrom_id);
		/* Command second phase complete - call the callback to complete the command. */
		cdrom_phase_callback(cdrom_id);
	}
    } else {
	/* Error (Check Condition) - call the phase callback to complete the command. */
	cdrom_phase_callback(cdrom_id);
    }

    pclog("SCSI Status: %s, Sense: %02X, ASC: %02X, ASCQ: %02X\n", (SCSIStatus == SCSI_STATUS_OK) ? "OK" : "CHECK CONDITION", cdrom[cdrom_id].sense[2], cdrom[cdrom_id].sense[12], cdrom[cdrom_id].sense[13]);

    aha_buf_free(req);

    SenseBufferFree(req, (SCSIStatus != SCSI_STATUS_OK), 0);

    pclog("Request complete\n");

    if (SCSIStatus == SCSI_STATUS_OK) {
	aha_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
			       CCB_COMPLETE, SCSI_STATUS_OK, MBI_SUCCESS);
    } else if (SCSIStatus == SCSI_STATUS_CHECK_CONDITION) {
	aha_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
			CCB_COMPLETE, SCSI_STATUS_CHECK_CONDITION, MBI_ERROR);
    }
}


static void
aha_req_setup(aha_t *dev, uint32_t CCBPointer, Mailbox32_t *Mailbox32)
{	
    Req_t *req = &dev->Req;
    uint8_t Id, Lun;
    uint8_t last_id = 7;

    /* Fetch data from the Command Control Block. */
    DMAPageRead(CCBPointer, (char *)&req->CmdBlock, sizeof(CCB32));

    req->Is24bit = dev->Mbx24bit;
    req->CCBPointer = CCBPointer;
    req->TargetID = dev->Mbx24bit ? req->CmdBlock.old.Id : req->CmdBlock.new.Id;
    req->LUN = dev->Mbx24bit ? req->CmdBlock.old.Lun : req->CmdBlock.new.Lun;
 
    Id = req->TargetID;
    Lun = req->LUN;
    if ((Id > last_id) || (Lun > 7)) {
	aha_mbi_setup(dev, CCBPointer, &req->CmdBlock,
		      CCB_INVALID_CCB, SCSI_STATUS_OK, MBI_ERROR);
	return;
    }
	
    pclog("Scanning SCSI Target ID %i\n", Id);		

    SCSIStatus = SCSI_STATUS_OK;
    SCSIDevices[Id][Lun].InitLength = 0;

    aha_buf_alloc(req, req->Is24bit);

    if (SCSIDevices[Id][Lun].LunType == SCSI_NONE) {
	pclog("SCSI Target ID %i and LUN %i have no device attached\n",Id,Lun);
	aha_buf_free(req);
	SenseBufferFree(req, 0, 0);
	aha_mbi_setup(dev, CCBPointer, &req->CmdBlock,
		      CCB_SELECTION_TIMEOUT,SCSI_STATUS_OK,MBI_ERROR);
    } else {
	pclog("SCSI Target ID %i and LUN %i detected and working\n", Id, Lun);

	pclog("Transfer Control %02X\n", req->CmdBlock.common.ControlByte);
	pclog("CDB Length %i\n", req->CmdBlock.common.CdbLength);	
	pclog("CCB Opcode %x\n", req->CmdBlock.common.Opcode);		
	if (req->CmdBlock.common.ControlByte > 0x03) {
		pclog("Invalid control byte: %02X\n",
			req->CmdBlock.common.ControlByte);
	}

	AHA_InOperation = (SCSIDevices[Id][Lun].LunType == SCSI_DISK) ? 0x11 : 1;
	pclog("SCSI (%i:%i) -> %i\n", Id, Lun, SCSIDevices[Id][Lun].LunType);
    }
}


static void
aha_req_abort(aha_t *dev, uint32_t CCBPointer)
{
    CCBU CmdBlock;

    /* Fetch data from the Command Control Block. */
    DMAPageRead(CCBPointer, (char *)&CmdBlock, sizeof(CCB32));

    aha_mbi_setup(dev, CCBPointer, &CmdBlock,
		  0x26, SCSI_STATUS_OK, MBI_NOT_FOUND);
}


static uint32_t
aha_mbo(aha_t *dev, Mailbox32_t *Mailbox32)
{	
    Mailbox_t MailboxOut;
    uint32_t Outgoing;

    if (dev->Mbx24bit) {
	Outgoing = dev->MailboxOutAddr + (dev->MailboxOutPosCur * sizeof(Mailbox_t));
	DMAPageRead(Outgoing, (char *)&MailboxOut, sizeof(Mailbox_t));

	Mailbox32->CCBPointer = ADDR_TO_U32(MailboxOut.CCBPointer);
	Mailbox32->u.out.ActionCode = MailboxOut.CmdStatus;
    } else {
	Outgoing = dev->MailboxOutAddr + (dev->MailboxOutPosCur * sizeof(Mailbox32_t));

	DMAPageRead(Outgoing, (char *)Mailbox32, sizeof(Mailbox32_t));	
    }
	
    return Outgoing;
}


static void
aha_mbo_adv(aha_t *dev)
{
    dev->MailboxOutPosCur = (dev->MailboxOutPosCur + 1) % dev->MailboxCount;
}


static void
aha_do_mail(aha_t *dev)
{
    Mailbox32_t mb32;
    uint32_t Outgoing;
    uint8_t CmdStatus = MBO_FREE;
    uint32_t CodeOffset = 0;

    CodeOffset = dev->Mbx24bit ? offsetof(Mailbox_t, CmdStatus) : offsetof(Mailbox32_t, u.out.ActionCode);

    if (! dev->StrictRoundRobinMode) {
	uint8_t MailboxCur = dev->MailboxOutPosCur;

	/* Search for a filled mailbox - stop if we have scanned all mailboxes. */
	do {
		/* Fetch mailbox from guest memory. */
		Outgoing = aha_mbo(dev, &mb32);

		/* Check the next mailbox. */
		aha_mbo_adv(dev);
	} while ((mb32.u.out.ActionCode == MBO_FREE) && (MailboxCur != dev->MailboxOutPosCur));
    } else {
	Outgoing = aha_mbo(dev, &mb32);
    }

    if (mb32.u.out.ActionCode != MBO_FREE) {
	/* We got the mailbox, mark it as free in the guest. */
	pclog("aha_do_mail(): Writing %i bytes at %08X\n", sizeof(CmdStatus), Outgoing + CodeOffset);
		DMAPageWrite(Outgoing + CodeOffset, (char *)&CmdStatus, sizeof(CmdStatus));
    }

    if (dev->MailboxOutInterrupts)
	RaiseIntr(dev, INTR_MBOA | INTR_ANY);

    /* Check if the mailbox is actually loaded. */
    if (mb32.u.out.ActionCode == MBO_FREE) {
	return;
    }

    if (mb32.u.out.ActionCode == MBO_START) {
	pclog("Start Mailbox Command\n");
	aha_req_setup(dev, mb32.CCBPointer, &mb32);
    } else if (mb32.u.out.ActionCode == MBO_ABORT) {
		pclog("Abort Mailbox Command\n");
		aha_req_abort(dev, mb32.CCBPointer);
    } else {
	pclog("Invalid action code: %02X\n", mb32.u.out.ActionCode);
    }

    /* Advance to the next mailbox. */
    if (dev->StrictRoundRobinMode)
	aha_mbo_adv(dev);
}


static void
aha_reset_poll(void *priv)
{
    aha_t *dev = (aha_t *)priv;

    dev->Status &= ~STAT_STST;
    dev->Status |= STAT_IDLE;

    ResetCB = 0;
}


static void
aha_cmd_cb(void *priv)
{
    aha_t *dev = (aha_t *)priv;

    if (AHA_InOperation == 0) {
	if (dev->MailboxCount) {
		aha_do_mail(dev);
	} else {
		AHA_Callback += 1 * TIMER_USEC;
		return;
	}
    } else if (AHA_InOperation == 1) {
	pclog("BusLogic Callback: Process CD-ROM request\n");
	aha_cdrom_cmd(dev);
	aha_mbi(dev);
	if (dev->Req.CmdBlock.common.Cdb[0] == 0x42)
	{
		/* This is needed since CD Audio inevitably means READ SUBCHANNEL spam. */
		AHA_Callback += 1000 * TIMER_USEC;
		return;
	}
    } else if (AHA_InOperation == 2) {
	pclog("BusLogic Callback: Send incoming mailbox\n");
	aha_mbi(dev);
    } else if (AHA_InOperation == 0x11) {
	pclog("BusLogic Callback: Process DISK request\n");
	aha_disk_cmd(dev);
	aha_mbi(dev);
    } else {
	fatal("Invalid BusLogic callback phase: %i\n", AHA_InOperation);
    }

    AHA_Callback += 1 * TIMER_USEC;
}

uint8_t aha_mca_read(int port, void *p)
{
    aha_t *dev = (aha_t *)p;

	return dev->pos_regs[port & 7];
}

uint16_t aha_mca_get_port(uint8_t pos_port)
{
	uint16_t addr = 0;
	switch (pos_port & 0xC7)
	{
		case 0x01:
		addr = 0x130;
		break;
		
		case 0x02:
		addr = 0x230;
		break;
		
		case 0x03:
		addr = 0x330;
		break;
		
		case 0x41:
		addr = 0x134;
		break;
		
		case 0x42:
		addr = 0x234;
		break;
		
		case 0x43:
		addr = 0x334;
		break;
	}
		
	return addr;
}

void aha_mca_write(int port, uint8_t val, void *p)
{
    aha_t *dev = (aha_t *)p;
    uint16_t addr;
	
	if (port < 0x102)
			return;

	addr = aha_mca_get_port(dev->pos_regs[3]);		
	io_removehandler(addr, 0x0004, aha_read, aha_readw, NULL, aha_write, aha_writew, NULL, dev);		
		
	dev->pos_regs[port & 7] = val;
	
	if (dev->pos_regs[2] & 1)
	{
		addr = aha_mca_get_port(dev->pos_regs[3]);
		io_sethandler(addr, 0x0004, aha_read, aha_readw, NULL, aha_write, aha_writew, NULL, dev);
	}
	
	dev->Irq = (dev->pos_regs[4] & 0x7) + 8;
	dev->DmaChannel = dev->pos_regs[5] & 0xf;	
}

void
aha_device_reset(void *p)
{
	aha_t *dev = (aha_t *) p;
	aha_reset_ctrl(dev, 1);
}


static void *
aha_init(int chip, int has_bios)
{
    aha_t *dev;
    int i = 0;
    int j = 0;
    uint32_t bios_addr = 0;
    int bios = 0;

    dev = malloc(sizeof(aha_t));
    memset(dev, 0x00, sizeof(aha_t));

    ResetDev = dev;
    dev->chip = chip;
    dev->Base = device_get_config_hex16("base");
    dev->Irq = device_get_config_int("irq");
    dev->DmaChannel = device_get_config_int("dma");
    bios = device_get_config_int("bios");
    bios_addr = device_get_config_hex20("bios_addr");

    if (dev->Base != 0) {
	io_sethandler(dev->Base, 4,
		      aha_read, aha_readw, NULL,
		      aha_write, aha_writew, NULL, dev);
    }

    pclog("Building SCSI hard disk map...\n");
    build_scsi_hd_map();
    pclog("Building SCSI CD-ROM map...\n");
    build_scsi_cdrom_map();

    for (i=0; i<16; i++) {
	for (j=0; j<8; j++) {
		if (scsi_hard_disks[i][j] != 0xff) {
			SCSIDevices[i][j].LunType = SCSI_DISK;
		}
		else if (find_cdrom_for_scsi_id(i, j) != 0xff) {
			SCSIDevices[i][j].LunType = SCSI_CDROM;
		}
		else {
			SCSIDevices[i][j].LunType = SCSI_NONE;
		}
	}
    }

    timer_add(aha_reset_poll, &ResetCB, &ResetCB, dev);
    timer_add(aha_cmd_cb, &AHA_Callback, &AHA_Callback, dev);

    pclog("Adaptec AHA-154x on port 0x%04X\n", dev->Base);
	
    aha_reset_ctrl(dev, CTRL_HRST);

    if (bios) {
	/* Perform AHA-154xNN-specific initialization. */
	if (chip == CHIP_AHA154XB)
	{
		rom_init(&dev->bios, L"roms/scsi/adaptec/B_AC00.BIN", 0xd8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
	}
	else
	{
		aha154x_bios(dev->Base, bios_addr, &dev->aha, dev->Irq, dev->DmaChannel, chip);
	}
    }

    return(dev);
}


static void *
aha_154xB_init(void)
{
    return(aha_init(CHIP_AHA154XB, 0));
}


static void *
aha_154xCF_init(void)
{
    return(aha_init(CHIP_AHA154XCF, 1));
}

static void *
aha_1640_init(void)
{
    aha_t *dev;
    int i = 0;
    int j = 0;

    dev = malloc(sizeof(aha_t));
    memset(dev, 0x00, sizeof(aha_t));

    ResetDev = dev;
	dev->chip = CHIP_AHA1640;
	
	pclog("Aha1640 initialized\n");
	mca_add(aha_mca_read, aha_mca_write, dev);
	dev->pos_regs[0] = 0x1F;
	dev->pos_regs[1] = 0x0F;	
	
    pclog("Building SCSI hard disk map...\n");
    build_scsi_hd_map();
    pclog("Building SCSI CD-ROM map...\n");
    build_scsi_cdrom_map();

    for (i=0; i<16; i++) {
	for (j=0; j<8; j++) {
		if (scsi_hard_disks[i][j] != 0xff) {
			SCSIDevices[i][j].LunType = SCSI_DISK;
		}
		else if (find_cdrom_for_scsi_id(i, j) != 0xff) {
			SCSIDevices[i][j].LunType = SCSI_CDROM;
		}
		else {
			SCSIDevices[i][j].LunType = SCSI_NONE;
		}
	}
    }

    timer_add(aha_reset_poll, &ResetCB, &ResetCB, dev);
    timer_add(aha_cmd_cb, &AHA_Callback, &AHA_Callback, dev);

    aha_reset_ctrl(dev, CTRL_HRST);

    return(dev);
}


static void
aha_close(void *priv)
{
    aha_t *dev = (aha_t *)priv;
    free(dev);
    ResetDev = NULL;
}


static device_config_t aha_154XCF_config[] = {
        {
		"base", "Address", CONFIG_HEX16, "", 0x334,
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
		"irq", "IRQ", CONFIG_SELECTION, "", 9,
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
		"dma", "DMA channel", CONFIG_SELECTION, "", 6,
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
		"bios", "Enable BIOS", CONFIG_BINARY, "", 0
	},
        {
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0xd8000,
                {
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

device_t aha1540b_device = {
    "Adaptec AHA-1540B",
    0,
    aha_154xB_init,
    aha_close,
    NULL,
    NULL,
    NULL,
    NULL,
    aha_154XCF_config
};

device_t aha1542cf_device = {
    "Adaptec AHA-1542CF",
    0,
    aha_154xCF_init,
    aha_close,
    NULL,
    NULL,
    NULL,
    NULL,
    aha_154XCF_config
};

device_t aha1640_device = {
    "Adaptec AHA-1640",
    DEVICE_MCA,
    aha_1640_init,
    aha_close,
    NULL,
    NULL,
    NULL,
    NULL
};
