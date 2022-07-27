/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the NCR 53C810 and 53C875 SCSI Host
 *		Adapters made by NCR and later Symbios and LSI. These
 *		controllers were designed for the PCI bus.
 *
 *
 *
 * Authors:	Paul Brook (QEMU)
 *		Artyom Tarasenko (QEMU)
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2006-2018 Paul Brook.
 *		Copyright 2009-2018 Artyom Tarasenko.
 *		Copyright 2017,2018 Miran Grca.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/i2c.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_ncr53c8xx.h>


#define NCR53C8XX_SDMS3_ROM	"roms/scsi/ncr53c8xx/NCR307.BIN"
#define SYM53C8XX_SDMS4_ROM	"roms/scsi/ncr53c8xx/8xx_64.rom"

#define HA_ID		  7

#define CHIP_810	  0x01
#define CHIP_820	  0x02
#define CHIP_825	  0x03
#define CHIP_815	  0x04
#define CHIP_810AP	  0x05
#define CHIP_860	  0x06
#define CHIP_895	  0x0c
#define CHIP_875	  0x0f
#define CHIP_895A	  0x12
#define CHIP_875A	  0x13
#define CHIP_875J	  0x8f

#define NCR_SCNTL0_TRG    0x01
#define NCR_SCNTL0_AAP    0x02
#define NCR_SCNTL0_EPC    0x08
#define NCR_SCNTL0_WATN   0x10
#define NCR_SCNTL0_START  0x20

#define NCR_SCNTL1_SST    0x01
#define NCR_SCNTL1_IARB   0x02
#define NCR_SCNTL1_AESP   0x04
#define NCR_SCNTL1_RST    0x08
#define NCR_SCNTL1_CON    0x10
#define NCR_SCNTL1_DHP    0x20
#define NCR_SCNTL1_ADB    0x40
#define NCR_SCNTL1_EXC    0x80

#define NCR_SCNTL2_WSR    0x01
#define NCR_SCNTL2_VUE0   0x02
#define NCR_SCNTL2_VUE1   0x04
#define NCR_SCNTL2_WSS    0x08
#define NCR_SCNTL2_SLPHBEN 0x10
#define NCR_SCNTL2_SLPMD  0x20
#define NCR_SCNTL2_CHM    0x40
#define NCR_SCNTL2_SDU    0x80

#define NCR_ISTAT_DIP    0x01
#define NCR_ISTAT_SIP    0x02
#define NCR_ISTAT_INTF   0x04
#define NCR_ISTAT_CON    0x08
#define NCR_ISTAT_SEM    0x10
#define NCR_ISTAT_SIGP   0x20
#define NCR_ISTAT_SRST   0x40
#define NCR_ISTAT_ABRT   0x80

#define NCR_SSTAT0_SDP0   0x01
#define NCR_SSTAT0_RST    0x02
#define NCR_SSTAT0_WOA    0x04
#define NCR_SSTAT0_LOA    0x08
#define NCR_SSTAT0_AIP    0x10
#define NCR_SSTAT0_OLF    0x20
#define NCR_SSTAT0_ORF    0x40
#define NCR_SSTAT0_ILF    0x80

#define NCR_SIST0_PAR     0x01
#define NCR_SIST0_RST     0x02
#define NCR_SIST0_UDC     0x04
#define NCR_SIST0_SGE     0x08
#define NCR_SIST0_RSL     0x10
#define NCR_SIST0_SEL     0x20
#define NCR_SIST0_CMP     0x40
#define NCR_SIST0_MA      0x80

#define NCR_SIST1_HTH     0x01
#define NCR_SIST1_GEN     0x02
#define NCR_SIST1_STO     0x04
#define NCR_SIST1_SBMC    0x10

#define NCR_SOCL_IO       0x01
#define NCR_SOCL_CD       0x02
#define NCR_SOCL_MSG      0x04
#define NCR_SOCL_ATN      0x08
#define NCR_SOCL_SEL      0x10
#define NCR_SOCL_BSY      0x20
#define NCR_SOCL_ACK      0x40
#define NCR_SOCL_REQ      0x80

#define NCR_DSTAT_IID     0x01
#define NCR_DSTAT_SIR     0x04
#define NCR_DSTAT_SSI     0x08
#define NCR_DSTAT_ABRT    0x10
#define NCR_DSTAT_BF      0x20
#define NCR_DSTAT_MDPE    0x40
#define NCR_DSTAT_DFE     0x80

#define NCR_DCNTL_COM     0x01
#define NCR_DCNTL_IRQD    0x02
#define NCR_DCNTL_STD     0x04
#define NCR_DCNTL_IRQM    0x08
#define NCR_DCNTL_SSM     0x10
#define NCR_DCNTL_PFEN    0x20
#define NCR_DCNTL_PFF     0x40
#define NCR_DCNTL_CLSE    0x80

#define NCR_DMODE_MAN     0x01
#define NCR_DMODE_BOF     0x02
#define NCR_DMODE_ERMP    0x04
#define NCR_DMODE_ERL     0x08
#define NCR_DMODE_DIOM    0x10
#define NCR_DMODE_SIOM    0x20

#define NCR_CTEST2_DACK   0x01
#define NCR_CTEST2_DREQ   0x02
#define NCR_CTEST2_TEOP   0x04
#define NCR_CTEST2_PCICIE 0x08
#define NCR_CTEST2_CM     0x10
#define NCR_CTEST2_CIO    0x20
#define NCR_CTEST2_SIGP   0x40
#define NCR_CTEST2_DDIR   0x80

#define NCR_CTEST5_BL2    0x04
#define NCR_CTEST5_DDIR   0x08
#define NCR_CTEST5_MASR   0x10
#define NCR_CTEST5_DFSN   0x20
#define NCR_CTEST5_BBCK   0x40
#define NCR_CTEST5_ADCK   0x80

/* Enable Response to Reselection */
#define NCR_SCID_RRE      0x60

#define PHASE_DO          0
#define PHASE_DI          1
#define PHASE_CMD         2
#define PHASE_ST          3
#define PHASE_MO          6
#define PHASE_MI          7
#define PHASE_MASK        7

/* Maximum length of MSG IN data.  */
#define NCR_MAX_MSGIN_LEN 8

/* Flag set if this is a tagged command.  */
#define NCR_TAG_VALID     (1 << 16)

#define NCR_NVRAM_SIZE	  2048
#define NCR_BUF_SIZE	  4096

typedef struct ncr53c8xx_request {
    uint32_t tag;
    uint32_t dma_len;
    uint8_t *dma_buf;
    uint32_t pending;
    int out;
} ncr53c8xx_request;

typedef enum
{
        SCSI_STATE_SEND_COMMAND,
        SCSI_STATE_READ_DATA,
        SCSI_STATE_WRITE_DATA,
        SCSI_STATE_READ_STATUS,
        SCSI_STATE_READ_MESSAGE,
	SCSI_STATE_WRITE_MESSAGE
} scsi_state_t;

typedef struct {
    char	*nvr_path;
    uint8_t	pci_slot;
    uint8_t	chip, wide;
    int		has_bios;
    int		BIOSBase;
    rom_t	bios;
    int		PCIBase;
    int		MMIOBase;
    mem_mapping_t mmio_mapping;
    int		RAMBase;
    mem_mapping_t ram_mapping;

    int carry; /* ??? Should this be an a visible register somewhere?  */
    int status;
    /* Action to take at the end of a MSG IN phase.
       0 = COMMAND, 1 = disconnect, 2 = DATA OUT, 3 = DATA IN.  */
    int msg_action;
    int msg_len;
    uint8_t msg[NCR_MAX_MSGIN_LEN];
    uint8_t nvram[NCR_NVRAM_SIZE]; /* 24C16 EEPROM (16 Kbit) */
    void *i2c, *eeprom;
    uint8_t ram[NCR_BUF_SIZE];	/* NCR 53C875 RAM (4 KB) */
    /* 0 if SCRIPTS are running or stopped.
     * 1 if a Wait Reselect instruction has been issued.
     * 2 if processing DMA from ncr53c8xx_execute_script.
     * 3 if a DMA operation is in progress.  */
    int waiting;

    uint8_t current_lun;

    uint8_t istat;
    uint8_t dcmd;
    uint8_t dstat;
    uint8_t dien;
    uint8_t sist0;
    uint8_t sist1;
    uint8_t sien0;
    uint8_t sien1;
    uint8_t mbox0;
    uint8_t mbox1;
    uint8_t dfifo;
    uint8_t ctest2;
    uint8_t ctest3;
    uint8_t ctest4;
    uint8_t ctest5;
    uint8_t dmode;
    uint8_t dcntl;
    uint8_t scntl0;
    uint8_t scntl1;
    uint8_t scntl2;
    uint8_t scntl3;
    uint8_t sstat0;
    uint8_t sstat1;
    uint8_t scid;
    uint8_t sxfer;
    uint8_t socl;
    uint8_t sdid;
    uint8_t ssid;
    uint8_t sfbr;
    uint8_t stest1;
    uint8_t stest2;
    uint8_t stest3;
    uint8_t sidl0;
    uint8_t sidl1;
    uint8_t stime0;
    uint8_t respid0;
    uint8_t respid1;
    uint8_t sbr;
    uint8_t chip_rev;
    uint8_t gpreg;
    uint8_t slpar;
    uint8_t swide;
    uint8_t gpcntl;
    uint8_t last_command;

    int command_complete;
    ncr53c8xx_request *current;

    int irq;

    uint32_t dsa;
    uint32_t temp;
    uint32_t dnad;
    uint32_t dbc;
    uint32_t dsp;
    uint32_t dsps;
    uint32_t scratcha, scratchb, scratchc, scratchd;
    uint32_t scratche, scratchf, scratchg, scratchh;
    uint32_t scratchi, scratchj;
    int last_level;
    void *hba_private;
    uint32_t buffer_pos;
    int32_t temp_buf_len;

    uint8_t sstop;

    uint8_t regop;
    uint32_t adder;

    uint32_t bios_mask;

    uint8_t bus;

    pc_timer_t timer;

#ifdef USE_WDTR
    uint8_t tr_set[16];
#endif
} ncr53c8xx_t;


#ifdef ENABLE_NCR53C8XX_LOG
int ncr53c8xx_do_log = ENABLE_NCR53C8XX_LOG;


static void
ncr53c8xx_log(const char *fmt, ...)
{
    va_list ap;

    if (ncr53c8xx_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ncr53c8xx_log(fmt, ...)
#endif


static uint8_t	ncr53c8xx_reg_readb(ncr53c8xx_t *dev, uint32_t offset);
static void	ncr53c8xx_reg_writeb(ncr53c8xx_t *dev, uint32_t offset, uint8_t val);


static __inline int32_t
sextract32(uint32_t value, int start, int length)
{
    /* Note that this implementation relies on right shift of signed
     * integers being an arithmetic shift.
     */
    return ((int32_t)(value << (32 - length - start))) >> (32 - length);
}


static __inline int
ncr53c8xx_irq_on_rsl(ncr53c8xx_t *dev)
{
    return (dev->sien0 & NCR_SIST0_RSL) && (dev->scid & NCR_SCID_RRE);
}


static void
ncr53c8xx_soft_reset(ncr53c8xx_t *dev)
{
    int i;

    ncr53c8xx_log("LSI Reset\n");
    timer_stop(&dev->timer);

    dev->carry = 0;

    dev->msg_action = 0;
    dev->msg_len = 0;
    dev->waiting = 0;
    dev->dsa = 0;
    dev->dnad = 0;
    dev->dbc = 0;
    dev->temp = 0;
    dev->scratcha = 0;
    dev->scratchb = 0;
    dev->scratchc = 0;
    dev->scratchd = 0;
    dev->scratche = 0;
    dev->scratchf = 0;
    dev->scratchg = 0;
    dev->scratchh = 0;
    dev->scratchi = 0;
    dev->scratchj = 0;
    dev->istat = 0;
    dev->dcmd = 0x40;
    dev->dstat = NCR_DSTAT_DFE;
    dev->dien = 0;
    dev->sist0 = 0;
    dev->sist1 = 0;
    dev->sien0 = 0;
    dev->sien1 = 0;
    dev->mbox0 = 0;
    dev->mbox1 = 0;
    dev->dfifo = 0;
    dev->ctest2 = NCR_CTEST2_DACK;
    dev->ctest3 = 0;
    dev->ctest4 = 0;
    dev->ctest5 = 0;
    dev->dsp = 0;
    dev->dsps = 0;
    dev->dmode = 0;
    dev->dcntl = 0;
    dev->scntl0 = 0xc0;
    dev->scntl1 = 0;
    dev->scntl2 = 0;
    if (dev->wide)
	dev->scntl3 = 8;
    else
	dev->scntl3 = 0;
    dev->sstat0 = 0;
    dev->sstat1 = 0;
    dev->scid = HA_ID;
    dev->sxfer = 0;
    dev->socl = 0;
    dev->sdid = 0;
    dev->ssid = 0;
    dev->stest1 = 0;
    dev->stest2 = 0;
    dev->stest3 = 0;
    dev->sidl0 = 0;
    dev->sidl1 = 0;
    dev->stime0 = 0;
    dev->stime0 = 1;
    dev->respid0 = 0x80;
    dev->respid1 = 0x00;
    dev->sbr = 0;
    dev->last_level = 0;
    dev->gpreg = 0;
    dev->slpar = 0;
    dev->sstop = 1;
    dev->gpcntl = 0x03;

    if (dev->wide) {
	/* This *IS* a wide SCSI controller, so reset all SCSI
	   devices. */
	for (i = 0; i < 16; i++) {
#ifdef USE_WDTR
		dev->tr_set[i] = 0;
#endif
		scsi_device_reset(&scsi_devices[dev->bus][i]);
	}
    } else {
	/* This is *NOT* a wide SCSI controller, so do not touch
	   SCSI devices with ID's >= 8. */
	for (i = 0; i < 8; i++) {
#ifdef USE_WDTR
		dev->tr_set[i] = 0;
#endif
		scsi_device_reset(&scsi_devices[dev->bus][i]);
	}
    }
}


static void
ncr53c8xx_read(ncr53c8xx_t *dev, uint32_t addr, uint8_t *buf, uint32_t len)
{
	uint32_t i = 0;

	ncr53c8xx_log("ncr53c8xx_read(): %08X-%08X, length %i\n", addr, (addr + len - 1), len);

	if (dev->dmode & NCR_DMODE_SIOM) {
		ncr53c8xx_log("NCR 810: Reading from I/O address %04X\n", (uint16_t) addr);
		for (i = 0; i < len; i++)
			buf[i] = inb((uint16_t) (addr + i));
	} else {
		ncr53c8xx_log("NCR 810: Reading from memory address %08X\n", addr);
    	dma_bm_read(addr, buf, len, 4);
	}
}


static void
ncr53c8xx_write(ncr53c8xx_t *dev, uint32_t addr, uint8_t *buf, uint32_t len)
{
	uint32_t i = 0;

	ncr53c8xx_log("ncr53c8xx_write(): %08X-%08X, length %i\n", addr, (addr + len - 1), len);

	if (dev->dmode & NCR_DMODE_DIOM) {
		ncr53c8xx_log("NCR 810: Writing to I/O address %04X\n", (uint16_t) addr);
		for (i = 0; i < len; i++)
			outb((uint16_t) (addr + i), buf[i]);
	} else {
		ncr53c8xx_log("NCR 810: Writing to memory address %08X\n", addr);
    	dma_bm_write(addr, buf, len, 4);
	}
}


static __inline uint32_t
read_dword(ncr53c8xx_t *dev, uint32_t addr)
{
    uint32_t buf;
    ncr53c8xx_log("Reading the next DWORD from memory (%08X)...\n", addr);
    dma_bm_read(addr, (uint8_t *)&buf, 4, 4);
    return buf;
}


static
void do_irq(ncr53c8xx_t *dev, int level)
{
    if (level) {
	pci_set_irq(dev->pci_slot, PCI_INTA);
	ncr53c8xx_log("Raising IRQ...\n");
    } else {
	pci_clear_irq(dev->pci_slot, PCI_INTA);
	ncr53c8xx_log("Lowering IRQ...\n");
    }
}


static void
ncr53c8xx_update_irq(ncr53c8xx_t *dev)
{
    int level;

    /* It's unclear whether the DIP/SIP bits should be cleared when the
       Interrupt Status Registers are cleared or when istat is read.
       We currently do the formwer, which seems to work.  */
    level = 0;
    if (dev->dstat & 0x7f) {
        if ((dev->dstat & dev->dien) & 0x7f)
            level = 1;
        dev->istat |= NCR_ISTAT_DIP;
    } else {
        dev->istat &= ~NCR_ISTAT_DIP;
    }

    if (dev->sist0 || dev->sist1) {
        if ((dev->sist0 & dev->sien0) || (dev->sist1 & dev->sien1))
            level = 1;
        dev->istat |= NCR_ISTAT_SIP;
    } else {
        dev->istat &= ~NCR_ISTAT_SIP;
    }
    if (dev->istat & NCR_ISTAT_INTF) {
        level = 1;
    }

    if (level != dev->last_level) {
        ncr53c8xx_log("Update IRQ level %d dstat %02x sist %02x%02x\n",
                level, dev->dstat, dev->sist1, dev->sist0);
        dev->last_level = level;
	do_irq(dev, level);	/* Only do something with the IRQ if the new level differs from the previous one. */
    }
}


/* Stop SCRIPTS execution and raise a SCSI interrupt.  */
static void
ncr53c8xx_script_scsi_interrupt(ncr53c8xx_t *dev, int stat0, int stat1)
{
    uint32_t mask0;
    uint32_t mask1;

    ncr53c8xx_log("SCSI Interrupt 0x%02x%02x prev 0x%02x%02x\n",
            stat1, stat0, dev->sist1, dev->sist0);
    dev->sist0 |= stat0;
    dev->sist1 |= stat1;
    /* Stop processor on fatal or unmasked interrupt.  As a special hack
       we don't stop processing when raising STO.  Instead continue
       execution and stop at the next insn that accesses the SCSI bus.  */
    mask0 = dev->sien0 | ~(NCR_SIST0_CMP | NCR_SIST0_SEL | NCR_SIST0_RSL);
    mask1 = dev->sien1 | ~(NCR_SIST1_GEN | NCR_SIST1_HTH);
    mask1 &= ~NCR_SIST1_STO;
    if ((dev->sist0 & mask0) || (dev->sist1 & mask1)) {
	ncr53c8xx_log("NCR 810: IRQ-mandated stop\n");
	dev->sstop = 1;
	timer_stop(&dev->timer);
    }
    ncr53c8xx_update_irq(dev);
}


/* Stop SCRIPTS execution and raise a DMA interrupt.  */
static void
ncr53c8xx_script_dma_interrupt(ncr53c8xx_t *dev, int stat)
{
    ncr53c8xx_log("DMA Interrupt 0x%x prev 0x%x\n", stat, dev->dstat);
    dev->dstat |= stat;
    ncr53c8xx_update_irq(dev);
    dev->sstop = 1;
    timer_stop(&dev->timer);
}


static __inline void
ncr53c8xx_set_phase(ncr53c8xx_t *dev, int phase)
{
    dev->sstat1 = (dev->sstat1 & ~PHASE_MASK) | phase;
}


static void
ncr53c8xx_bad_phase(ncr53c8xx_t *dev, int out, int new_phase)
{
    /* Trigger a phase mismatch.  */
    ncr53c8xx_log("Phase mismatch interrupt\n");
    ncr53c8xx_script_scsi_interrupt(dev, NCR_SIST0_MA, 0);
    dev->sstop = 1;
    timer_stop(&dev->timer);
    ncr53c8xx_set_phase(dev, new_phase);
}


static void
ncr53c8xx_disconnect(ncr53c8xx_t *dev)
{
    scsi_device_t *sd;

    sd = &scsi_devices[dev->bus][dev->sdid];

    dev->scntl1 &= ~NCR_SCNTL1_CON;
    dev->sstat1 &= ~PHASE_MASK;
    if (dev->dcmd & 0x01)		/* Select with ATN */
	dev->sstat1 |= 0x07;
    scsi_device_identify(sd, SCSI_LUN_USE_CDB);
}


static void
ncr53c8xx_bad_selection(ncr53c8xx_t *dev, uint32_t id)
{
    ncr53c8xx_log("Selected absent target %d\n", id);
    ncr53c8xx_script_scsi_interrupt(dev, 0, NCR_SIST1_STO);
    ncr53c8xx_disconnect(dev);
}


/* Callback to indicate that the SCSI layer has completed a command.  */
static void
ncr53c8xx_command_complete(void *priv, uint32_t status)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)priv;
    int out;

    out = (dev->sstat1 & PHASE_MASK) == PHASE_DO;
    ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Command complete status=%d\n", dev->current->tag, dev->current_lun, dev->last_command, (int)status);
    dev->status = status;
    dev->command_complete = 2;
    if (dev->waiting && dev->dbc != 0) {
	/* Raise phase mismatch for short transfers.  */
	ncr53c8xx_bad_phase(dev, out, PHASE_ST);
    } else
	ncr53c8xx_set_phase(dev, PHASE_ST);

    dev->sstop = 0;
}


static void
ncr53c8xx_do_dma(ncr53c8xx_t *dev, int out, uint8_t id)
{
    uint32_t addr, tdbc;
    int count;

    scsi_device_t *sd = &scsi_devices[dev->bus][id];

    if ((!scsi_device_present(sd))) {
	ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Device not present when attempting to do DMA\n", id, dev->current_lun, dev->last_command);
	return;
    }

    if (!dev->current->dma_len) {
	/* Wait until data is available.  */
	ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: DMA no data available\n", id, dev->current_lun, dev->last_command);
	return;
    }

    /* Make sure count is never bigger than buffer_length. */
    count = tdbc = dev->dbc;
    if (count > dev->temp_buf_len)
	count = dev->temp_buf_len;

    addr = dev->dnad;

    ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: DMA addr=0x%08x len=%d cur_len=%d dev->dbc=%d\n", id, dev->current_lun, dev->last_command, dev->dnad, dev->temp_buf_len, count, tdbc);
    dev->dnad += count;
    dev->dbc -= count;

    if (out)
	ncr53c8xx_read(dev, addr, sd->sc->temp_buffer + dev->buffer_pos, count);
    else {
#ifdef ENABLE_NCR53C8XX_LOG
	if (!dev->buffer_pos)
		ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: SCSI Command Phase 1 on PHASE_DI\n", id, dev->current_lun, dev->last_command);
#endif
	ncr53c8xx_write(dev, addr, sd->sc->temp_buffer + dev->buffer_pos, count);
    }

    dev->temp_buf_len -= count;
    dev->buffer_pos += count;

    if (dev->temp_buf_len <= 0) {
	scsi_device_command_phase1(&scsi_devices[dev->bus][id]);
#ifdef ENABLE_NCR53C8XX_LOG
	if (out)
		ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: SCSI Command Phase 1 on PHASE_DO\n", id, dev->current_lun, dev->last_command);
#endif
	ncr53c8xx_command_complete(dev, sd->status);
    } else {
	ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Resume SCRIPTS\n", id, dev->current_lun, dev->last_command);
	dev->sstop = 0;
    }
}


/* Queue a byte for a MSG IN phase.  */
static void
ncr53c8xx_add_msg_byte(ncr53c8xx_t *dev, uint8_t data)
{
    if (dev->msg_len >= NCR_MAX_MSGIN_LEN)
	ncr53c8xx_log("MSG IN data too long\n");
    else {
	ncr53c8xx_log("MSG IN 0x%02x\n", data);
	dev->msg[dev->msg_len++] = data;
    }
}


static void
ncr53c8xx_timer_on(ncr53c8xx_t *dev, scsi_device_t *sd, double p)
{
    double period;

    /* Fast SCSI: 10000000 bytes per second */
    period = (p > 0.0) ? p : (((double) sd->buffer_length) * 0.1);

    timer_on_auto(&dev->timer, period + 40.0);
}


static int
ncr53c8xx_do_command(ncr53c8xx_t *dev, uint8_t id)
{
    scsi_device_t *sd;
    uint8_t buf[12];

    memset(buf, 0, 12);
    dma_bm_read(dev->dnad, buf, MIN(12, dev->dbc), 4);
    if (dev->dbc > 12) {
	ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: CDB length %i too big\n", id, dev->current_lun, buf[0], dev->dbc);
	dev->dbc = 12;
    }
    dev->sfbr = buf[0];
    dev->command_complete = 0;

    sd = &scsi_devices[dev->bus][id];
    if (!scsi_device_present(sd) || (dev->current_lun > 0)) {
	ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Bad Selection\n", id, dev->current_lun, buf[0]);
	ncr53c8xx_bad_selection(dev, id);
	return 0;
    }

    dev->current = (ncr53c8xx_request*)malloc(sizeof(ncr53c8xx_request));
    dev->current->tag = id;

    sd->buffer_length = -1;

    ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: DBC=%i\n", id, dev->current_lun, buf[0], dev->dbc);
    dev->last_command = buf[0];

    /* Make sure bits 5-7 of the CDB have the correct LUN. */
    if ((buf[1] & 0xe0) != (dev->current_lun << 5))
	buf[1] = (buf[1] & 0x1f) | (dev->current_lun << 5);

    scsi_device_command_phase0(&scsi_devices[dev->bus][dev->current->tag], buf);
    dev->hba_private = (void *)dev->current;

    dev->waiting = 0;
    dev->buffer_pos = 0;

    dev->temp_buf_len = sd->buffer_length;

    if (sd->buffer_length > 0) {
	/* This should be set to the underlying device's buffer by command phase 0. */
	dev->current->dma_len = sd->buffer_length;
    }

    if ((sd->phase == SCSI_PHASE_DATA_IN) && (sd->buffer_length > 0)) {
	ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: PHASE_DI\n", id, dev->current_lun, buf[0]);
	ncr53c8xx_set_phase(dev, PHASE_DI);
	ncr53c8xx_timer_on(dev, sd, scsi_device_get_callback(&scsi_devices[dev->bus][dev->current->tag]));
	return 1;
    } else if ((sd->phase == SCSI_PHASE_DATA_OUT) && (sd->buffer_length > 0)) {
	ncr53c8xx_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: PHASE_DO\n", id, buf[0]);
	ncr53c8xx_set_phase(dev, PHASE_DO);
	ncr53c8xx_timer_on(dev, sd, scsi_device_get_callback(&scsi_devices[dev->bus][dev->current->tag]));
	return 1;
    } else {
	ncr53c8xx_command_complete(dev, sd->status);
	return 0;
    }
}


static void
ncr53c8xx_do_status(ncr53c8xx_t *dev)
{
    uint8_t status;
    ncr53c8xx_log("Get status len=%d status=%d\n", dev->dbc, dev->status);
    if (dev->dbc != 1)
	ncr53c8xx_log("Bad Status move\n");
    dev->dbc = 1;
    status = dev->status;
    dev->sfbr = status;
    ncr53c8xx_write(dev, dev->dnad, &status, 1);
    ncr53c8xx_set_phase(dev, PHASE_MI);
    dev->msg_action = 1;
    ncr53c8xx_add_msg_byte(dev, 0); /* COMMAND COMPLETE */
}


#ifdef USE_WDTR
static void
ncr53c8xx_do_wdtr(ncr53c8xx_t *dev, int exponent)
{
    ncr53c8xx_log("Target-initiated WDTR (%08X)\n", dev);
    ncr53c8xx_set_phase(dev, PHASE_MI);
    dev->msg_action = 4;
    ncr53c8xx_add_msg_byte(dev, 0x01);		/* EXTENDED MESSAGE */
    ncr53c8xx_add_msg_byte(dev, 0x02);		/* EXTENDED MESSAGE LENGTH */
    ncr53c8xx_add_msg_byte(dev, 0x03);		/* WIDE DATA TRANSFER REQUEST */
    ncr53c8xx_add_msg_byte(dev, exponent);	/* TRANSFER WIDTH EXPONENT (16-bit) */
}
#endif


static void
ncr53c8xx_do_msgin(ncr53c8xx_t *dev)
{
    uint32_t len;
    ncr53c8xx_log("Message in len=%d/%d\n", dev->dbc, dev->msg_len);
    dev->sfbr = dev->msg[0];
    len = dev->msg_len;
    if (len > dev->dbc)
	len = dev->dbc;
    ncr53c8xx_write(dev, dev->dnad, dev->msg, len);
    /* Linux drivers rely on the last byte being in the SIDL.  */
    dev->sidl0 = dev->msg[len - 1];
    dev->msg_len -= len;
    if (dev->msg_len)
	memmove(dev->msg, dev->msg + len, dev->msg_len);
    else  {
	/* ??? Check if ATN (not yet implemented) is asserted and maybe
	   switch to PHASE_MO.  */
	switch (dev->msg_action) {
		case 0:
			ncr53c8xx_set_phase(dev, PHASE_CMD);
			break;
		case 1:
			ncr53c8xx_disconnect(dev);
			break;
		case 2:
			ncr53c8xx_set_phase(dev, PHASE_DO);
			break;
		case 3:
			ncr53c8xx_set_phase(dev, PHASE_DI);
			break;
		case 4:
			ncr53c8xx_set_phase(dev, PHASE_MO);
			break;
		default:
			abort();
	}
    }
}


/* Read the next byte during a MSGOUT phase.  */
static uint8_t
ncr53c8xx_get_msgbyte(ncr53c8xx_t *dev)
{
    uint8_t data;
    dma_bm_read(dev->dnad, &data, 1, 4);
    dev->dnad++;
    dev->dbc--;
    return data;
}


/* Skip the next n bytes during a MSGOUT phase. */
static void
ncr53c8xx_skip_msgbytes(ncr53c8xx_t *dev, unsigned int n)
{
    dev->dnad += n;
    dev->dbc  -= n;
}


static void
ncr53c8xx_bad_message(ncr53c8xx_t *dev, uint8_t msg)
{
    ncr53c8xx_log("Unimplemented message 0x%02x\n", msg);
    ncr53c8xx_set_phase(dev, PHASE_MI);
    ncr53c8xx_add_msg_byte(dev, 7); /* MESSAGE REJECT */
    dev->msg_action = 0;
}


static void
ncr53c8xx_do_msgout(ncr53c8xx_t *dev, uint8_t id)
{
    uint8_t msg;
    int len, arg;
#ifdef ENABLE_NCR53C8XX_LOG
    uint32_t current_tag;
#endif
    scsi_device_t *sd;

    sd = &scsi_devices[dev->bus][id];

#ifdef ENABLE_NCR53C8XX_LOG
    current_tag = id;
#endif

    ncr53c8xx_log("MSG out len=%d\n", dev->dbc);
    while (dev->dbc) {
	msg = ncr53c8xx_get_msgbyte(dev);
	dev->sfbr = msg;

	switch (msg) {
		case 0x04:
			ncr53c8xx_log("MSG: Disconnect\n");
			ncr53c8xx_disconnect(dev);
			break;
		case 0x08:
			ncr53c8xx_log("MSG: No Operation\n");
			ncr53c8xx_set_phase(dev, PHASE_CMD);
			break;
		case 0x01:
			len = ncr53c8xx_get_msgbyte(dev);
			msg = ncr53c8xx_get_msgbyte(dev);
			arg = ncr53c8xx_get_msgbyte(dev);
			(void) len; /* avoid a warning about unused variable*/
			ncr53c8xx_log("Extended message 0x%x (len %d)\n", msg, len);
			switch (msg) {
				case 1:
					ncr53c8xx_log("SDTR (ignored)\n");
					ncr53c8xx_skip_msgbytes(dev, 1);
					break;
				case 3:
					ncr53c8xx_log("WDTR (ignored)\n");
#ifdef USE_WDTR
					dev->tr_set[dev->sdid] = 1;
#endif
					if (arg > 0x01) {
						ncr53c8xx_bad_message(dev, msg);
						return;
					}
					ncr53c8xx_set_phase(dev, PHASE_CMD);
					break;
				case 5:
					ncr53c8xx_log("PPR (ignored)\n");
					ncr53c8xx_skip_msgbytes(dev, 4);
					break;
				default:
					ncr53c8xx_bad_message(dev, msg);
					return;
			}
			break;
		case 0x20: /* SIMPLE queue */
			id |= ncr53c8xx_get_msgbyte(dev) | NCR_TAG_VALID;
			ncr53c8xx_log("SIMPLE queue tag=0x%x\n", id & 0xff);
			break;
		case 0x21: /* HEAD of queue */
			ncr53c8xx_log("HEAD queue not implemented\n");
			id |= ncr53c8xx_get_msgbyte(dev) | NCR_TAG_VALID;
			break;
		case 0x22: /* ORDERED queue */
			ncr53c8xx_log("ORDERED queue not implemented\n");
			id |= ncr53c8xx_get_msgbyte(dev) | NCR_TAG_VALID;
			break;
		case 0x0d:
			/* The ABORT TAG message clears the current I/O process only. */
			ncr53c8xx_log("MSG: Abort Tag\n");
			scsi_device_command_stop(sd);
			ncr53c8xx_disconnect(dev);
			break;
		case 0x0c:
			/* BUS DEVICE RESET message, reset wide transfer request. */
#ifdef USE_WDTR
			dev->tr_set[dev->sdid] = 0;
#endif
			/* FALLTHROUGH */
		case 0x06:
		case 0x0e:
			/* clear the current I/O process */
			scsi_device_command_stop(sd);
			ncr53c8xx_disconnect(dev);
			break;
		default:
			if ((msg & 0x80) == 0) {
				ncr53c8xx_bad_message(dev, msg);
				return;
			} else {
				/* 0x80 to 0xff are IDENTIFY messages. */
				ncr53c8xx_log("MSG: Identify\n");
				dev->current_lun = msg & 7;
				scsi_device_identify(sd, msg & 7);
				ncr53c8xx_log("Select LUN %d\n", dev->current_lun);
#ifdef USE_WDTR
				if ((dev->chip == CHIP_875) && !dev->tr_set[dev->sdid])
					ncr53c8xx_do_wdtr(dev, 0x01);
				else
#endif
					ncr53c8xx_set_phase(dev, PHASE_CMD);
			}
			break;
	}
    }
}


static void
ncr53c8xx_memcpy(ncr53c8xx_t *dev, uint32_t dest, uint32_t src, int count)
{
    int n;
    uint8_t buf[NCR_BUF_SIZE];

    ncr53c8xx_log("memcpy dest 0x%08x src 0x%08x count %d\n", dest, src, count);
    while (count) {
	n = (count > NCR_BUF_SIZE) ? NCR_BUF_SIZE : count;
	ncr53c8xx_read(dev, src, buf, n);
	ncr53c8xx_write(dev, dest, buf, n);
	src += n;
	dest += n;
	count -= n;
    }
}


static void
ncr53c8xx_process_script(ncr53c8xx_t *dev)
{
    uint32_t insn, addr, id, buf[2], dest;
    int opcode, insn_processed = 0, reg, operator, cond, jmp, n, i, c;
    int32_t offset;
    uint8_t op0, op1, data8, mask, data[7];
#ifdef ENABLE_NCR53C8XX_LOG
    uint8_t *pp;
#endif

    dev->sstop = 0;
again:
    insn_processed++;
    insn = read_dword(dev, dev->dsp);
    if (!insn) {
	/* If we receive an empty opcode increment the DSP by 4 bytes
	   instead of 8 and execute the next opcode at that location */
	dev->dsp += 4;
	if (insn_processed < 100)
		goto again;
	else {
		timer_on_auto(&dev->timer, 10.0);
		return;
	}
    }
    addr = read_dword(dev, dev->dsp + 4);
    ncr53c8xx_log("SCRIPTS dsp=%08x opcode %08x arg %08x\n", dev->dsp, insn, addr);
    dev->dsps = addr;
    dev->dcmd = insn >> 24;
    dev->dsp += 8;

    switch (insn >> 30) {
	case 0: /* Block move.  */
		ncr53c8xx_log("00: Block move\n");
		if (dev->sist1 & NCR_SIST1_STO) {
			ncr53c8xx_log("Delayed select timeout\n");
			dev->sstop = 1;
			break;
		}
		ncr53c8xx_log("Block Move DBC=%d\n", dev->dbc);
		dev->dbc = insn & 0xffffff;
		ncr53c8xx_log("Block Move DBC=%d now\n", dev->dbc);
		/* ??? Set ESA.  */
		if (insn & (1 << 29)) {
			/* Indirect addressing.  */
			/* Should this respect SIOM? */
			addr = read_dword(dev, addr);
			ncr53c8xx_log("Indirect Block Move address: %08X\n", addr);
		} else if (insn & (1 << 28)) {
			/* Table indirect addressing.  */

			/* 32-bit Table indirect */
			offset = sextract32(addr, 0, 24);
			dma_bm_read(dev->dsa + offset, (uint8_t *)buf, 8, 4);
			/* byte count is stored in bits 0:23 only */
			dev->dbc = buf[0] & 0xffffff;
			addr = buf[1];

			/* 40-bit DMA, upper addr bits [39:32] stored in first DWORD of
			 * table, bits [31:24] */
		}
		if ((dev->sstat1 & PHASE_MASK) != ((insn >> 24) & 7)) {
			ncr53c8xx_log("Wrong phase got %d expected %d\n",
			dev->sstat1 & PHASE_MASK, (insn >> 24) & 7);
			ncr53c8xx_script_scsi_interrupt(dev, NCR_SIST0_MA, 0);
			break;
		}
		dev->dnad = addr;
		switch (dev->sstat1 & 0x7) {
			case PHASE_DO:
				ncr53c8xx_log("Data Out Phase\n");
				dev->waiting = 0;
				ncr53c8xx_do_dma(dev, 1, dev->sdid);
				break;
			case PHASE_DI:
				ncr53c8xx_log("Data In Phase\n");
				dev->waiting = 0;
				ncr53c8xx_do_dma(dev, 0, dev->sdid);
				break;
			case PHASE_CMD:
				ncr53c8xx_log("Command Phase\n");
				c = ncr53c8xx_do_command(dev, dev->sdid);

				if (!c || dev->sstop || dev->waiting || ((dev->sstat1 & 0x7) == PHASE_ST))
					break;

				dev->dfifo = dev->dbc & 0xff;
				dev->ctest5 = (dev->ctest5 & 0xfc) | ((dev->dbc >> 8) & 3);

				if (dev->dcntl & NCR_DCNTL_SSM)
					ncr53c8xx_script_dma_interrupt(dev, NCR_DSTAT_SSI);
				return;
			case PHASE_ST:
				ncr53c8xx_log("Status Phase\n");
				ncr53c8xx_do_status(dev);
				break;
			case PHASE_MO:
				ncr53c8xx_log("MSG Out Phase\n");
				ncr53c8xx_do_msgout(dev, dev->sdid);
				break;
			case PHASE_MI:
				ncr53c8xx_log("MSG In Phase\n");
				ncr53c8xx_do_msgin(dev);
				break;
			default:
				ncr53c8xx_log("Unimplemented phase %d\n", dev->sstat1 & PHASE_MASK);
		}
		dev->dfifo = dev->dbc & 0xff;
		dev->ctest5 = (dev->ctest5 & 0xfc) | ((dev->dbc >> 8) & 3);
		break;

	case 1: /* IO or Read/Write instruction.  */
		ncr53c8xx_log("01: I/O or Read/Write instruction\n");
		opcode = (insn >> 27) & 7;
		if (opcode < 5) {
			if (insn & (1 << 25))
				id = read_dword(dev, dev->dsa + sextract32(insn, 0, 24));
			else
				id = insn;
			id = (id >> 16) & 0xf;
			if (insn & (1 << 26))
				addr = dev->dsp + sextract32(addr, 0, 24);
			dev->dnad = addr;
			switch (opcode) {
				case 0: /* Select */
					dev->sdid = id;
					if (dev->scntl1 & NCR_SCNTL1_CON) {
						ncr53c8xx_log("Already reselected, jumping to alternative address\n");
						dev->dsp = dev->dnad;
						break;
					}
					dev->sstat0 |= NCR_SSTAT0_WOA;
					dev->scntl1 &= ~NCR_SCNTL1_IARB;
					if (!scsi_device_present(&scsi_devices[dev->bus][id])) {
						ncr53c8xx_bad_selection(dev, id);
						break;
					}
					ncr53c8xx_log("Selected target %d%s\n",
						      id, insn & (1 << 24) ? " ATN" : "");
					dev->scntl1 |= NCR_SCNTL1_CON;
					if (insn & (1 << 24))
						dev->socl |= NCR_SOCL_ATN;
					ncr53c8xx_set_phase(dev, PHASE_MO);
					dev->waiting = 0;
					break;
				case 1: /* Disconnect */
					ncr53c8xx_log("Wait Disconnect\n");
					dev->scntl1 &= ~NCR_SCNTL1_CON;
					break;
				case 2: /* Wait Reselect */
					ncr53c8xx_log("Wait Reselect\n");
					if (dev->istat & NCR_ISTAT_SIGP)
						dev->dsp = dev->dnad;		/* If SIGP is set, this command causes an immediate jump to DNAD. */
					else {
						if (!ncr53c8xx_irq_on_rsl(dev))
							dev->waiting = 1;
					}
					break;
				case 3: /* Set */
					ncr53c8xx_log("Set%s%s%s%s\n", insn & (1 << 3) ? " ATN" : "",
								       insn & (1 << 6) ? " ACK" : "",
								       insn & (1 << 9) ? " TM" : "",
								       insn & (1 << 10) ? " CC" : "");
					if (insn & (1 << 3)) {
						dev->socl |= NCR_SOCL_ATN;
						ncr53c8xx_set_phase(dev, PHASE_MO);
					}
					if (insn & (1 << 9))
						ncr53c8xx_log("Target mode not implemented\n");
					if (insn & (1 << 10))
						dev->carry = 1;
					break;
				case 4: /* Clear */
					ncr53c8xx_log("Clear%s%s%s%s\n", insn & (1 << 3) ? " ATN" : "",
									 insn & (1 << 6) ? " ACK" : "",
									 insn & (1 << 9) ? " TM" : "",
									 insn & (1 << 10) ? " CC" : "");
					if (insn & (1 << 3))
						dev->socl &= ~NCR_SOCL_ATN;
					if (insn & (1 << 10))
						dev->carry = 0;
					break;
			}
		} else {
			reg = ((insn >> 16) & 0x7f) | (insn & 0x80);
			data8 = (insn >> 8) & 0xff;
			opcode = (insn >> 27) & 7;
			operator = (insn >> 24) & 7;
			op0 = op1 = 0;
			switch (opcode) {
				case 5: /* From SFBR */
					op0 = dev->sfbr;
					op1 = data8;
					break;
				case 6: /* To SFBR */
					if (operator)
						op0 = ncr53c8xx_reg_readb(dev, reg);
					op1 = data8;
					break;
				case 7: /* Read-modify-write */
					if (operator)
						op0 = ncr53c8xx_reg_readb(dev, reg);
					if (insn & (1 << 23))
						op1 = dev->sfbr;
					else
						op1 = data8;
					break;
			}

			switch (operator) {
				case 0: /* move */
					op0 = op1;
					break;
				case 1: /* Shift left */
					op1 = op0 >> 7;
					op0 = (op0 << 1) | dev->carry;
					dev->carry = op1;
					break;
				case 2: /* OR */
					op0 |= op1;
					break;
				case 3: /* XOR */
					op0 ^= op1;
					break;
				case 4: /* AND */
					op0 &= op1;
					break;
				case 5: /* SHR */
					op1 = op0 & 1;
					op0 = (op0 >> 1) | (dev->carry << 7);
					dev->carry = op1;
					break;
				case 6: /* ADD */
					op0 += op1;
					dev->carry = op0 < op1;
					break;
				case 7: /* ADC */
					op0 += op1 + dev->carry;
					if (dev->carry)
						dev->carry = op0 <= op1;
					else
						dev->carry = op0 < op1;
					break;
			}

			switch (opcode) {
				case 5: /* From SFBR */
				case 7: /* Read-modify-write */
					ncr53c8xx_reg_writeb(dev, reg, op0);
					break;
				case 6: /* To SFBR */
					dev->sfbr = op0;
					break;
			}
		}
		break;

	case 2: /* Transfer Control.  */
		ncr53c8xx_log("02: Transfer Control\n");
		if ((insn & 0x002e0000) == 0) {
			ncr53c8xx_log("NOP\n");
			break;
		}
		if (dev->sist1 & NCR_SIST1_STO) {
			ncr53c8xx_log("Delayed select timeout\n");
			dev->sstop = 1;
			break;
		}
		cond = jmp = (insn & (1 << 19)) != 0;
		if (cond == jmp && (insn & (1 << 21))) {
			ncr53c8xx_log("Compare carry %d\n", dev->carry == jmp);
			cond = dev->carry != 0;
		}
		if (cond == jmp && (insn & (1 << 17))) {
			ncr53c8xx_log("Compare phase %d %c= %d\n", (dev->sstat1 & PHASE_MASK),
								   jmp ? '=' : '!', ((insn >> 24) & 7));
			cond = (dev->sstat1 & PHASE_MASK) == ((insn >> 24) & 7);
		}
		if (cond == jmp && (insn & (1 << 18))) {
			mask = (~insn >> 8) & 0xff;
			ncr53c8xx_log("Compare data 0x%x & 0x%x %c= 0x%x\n", dev->sfbr, mask,
				      jmp ? '=' : '!', insn & mask);
			cond = (dev->sfbr & mask) == (insn & mask);
		}
		if (cond == jmp) {
			if (insn & (1 << 23)) {
				/* Relative address.  */
				addr = dev->dsp + sextract32(addr, 0, 24);
			}
			switch ((insn >> 27) & 7) {
				case 0: /* Jump */
					ncr53c8xx_log("Jump to 0x%08x\n", addr);
					dev->adder = addr;
					dev->dsp = addr;
					break;
				case 1: /* Call */
					ncr53c8xx_log("Call 0x%08x\n", addr);
					dev->temp = dev->dsp;
					dev->dsp = addr;
					break;
				case 2: /* Return */
					ncr53c8xx_log("Return to 0x%08x\n", dev->temp);
					dev->dsp = dev->temp;
					break;
				case 3: /* Interrupt */
					ncr53c8xx_log("Interrupt 0x%08x\n", dev->dsps);
					if ((insn & (1 << 20)) != 0) {
						dev->istat |= NCR_ISTAT_INTF;
						ncr53c8xx_update_irq(dev);
					} else
						ncr53c8xx_script_dma_interrupt(dev, NCR_DSTAT_SIR);
					break;
				default:
					ncr53c8xx_log("Illegal transfer control\n");
					ncr53c8xx_script_dma_interrupt(dev, NCR_DSTAT_IID);
					break;
			}
		} else
			ncr53c8xx_log("Control condition failed\n");
		break;

	case 3:
		ncr53c8xx_log("00: Memory move\n");
		if ((insn & (1 << 29)) == 0) {
			/* Memory move.  */
			/* ??? The docs imply the destination address is loaded into
			   the TEMP register.  However the Linux drivers rely on
			   the value being presrved.  */
			dest = read_dword(dev, dev->dsp);
			dev->dsp += 4;
			ncr53c8xx_memcpy(dev, dest, addr, insn & 0xffffff);
		} else {
#ifdef ENABLE_NCR53C8XX_LOG
			pp = data;
#endif

			if (insn & (1 << 28))
				addr = dev->dsa + sextract32(addr, 0, 24);
			n = (insn & 7);
			reg = (insn >> 16) & 0xff;
			if (insn & (1 << 24)) {
				dma_bm_read(addr, data, n, 4);
				for (i = 0; i < n; i++)
					ncr53c8xx_reg_writeb(dev, reg + i, data[i]);
			} else {
				ncr53c8xx_log("Store reg 0x%x size %d addr 0x%08x\n", reg, n, addr);
				for (i = 0; i < n; i++)
					data[i] = ncr53c8xx_reg_readb(dev, reg + i);
				dma_bm_write(addr, data, n, 4);
			}
		}
		break;

	default:
		ncr53c8xx_log("%02X: Unknown command\n", (uint8_t) (insn >> 30));
    }

    ncr53c8xx_log("instructions processed %i\n", insn_processed);
    if (insn_processed > 10000 && !dev->waiting) {
	/* Some windows drivers make the device spin waiting for a memory
	   location to change.  If we have been executed a lot of code then
	   assume this is the case and force an unexpected device disconnect.
	   This is apparently sufficient to beat the drivers into submission.
	 */
	ncr53c8xx_log("Some windows drivers make the device spin...\n");
	if (!(dev->sien0 & NCR_SIST0_UDC))
		ncr53c8xx_log("inf. loop with UDC masked\n");
	ncr53c8xx_script_scsi_interrupt(dev, NCR_SIST0_UDC, 0);
	ncr53c8xx_disconnect(dev);
    } else if (!dev->sstop && !dev->waiting) {
	if (dev->dcntl & NCR_DCNTL_SSM) {
		ncr53c8xx_log("NCR 810: SCRIPTS: Single-step mode\n");
		ncr53c8xx_script_dma_interrupt(dev, NCR_DSTAT_SSI);
	} else {
		ncr53c8xx_log("NCR 810: SCRIPTS: Normal mode\n");
		if (insn_processed < 100)
			goto again;
	}
    } else {
	if (dev->sstop)
		ncr53c8xx_log("NCR 810: SCRIPTS: Stopped\n");
	if (dev->waiting)
		ncr53c8xx_log("NCR 810: SCRIPTS: Waiting\n");
    }

    timer_on_auto(&dev->timer, 40.0);

    ncr53c8xx_log("SCRIPTS execution stopped\n");
}


static void
ncr53c8xx_execute_script(ncr53c8xx_t *dev)
{
    dev->sstop = 0;
    timer_on_auto(&dev->timer, 40.0);
}


static void
ncr53c8xx_callback(void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *) p;

    if (!dev->sstop) {
	if (dev->waiting)
		timer_on_auto(&dev->timer, 40.0);
	else
		ncr53c8xx_process_script(dev);
    }

    if (dev->sstop)
	timer_stop(&dev->timer);
}


static void
ncr53c8xx_eeprom(ncr53c8xx_t *dev, uint8_t save)
{
    FILE *f;

    f = nvr_fopen(dev->nvr_path, save ? "wb": "rb");
    if (f) {
	if (save)
		fwrite(&dev->nvram, sizeof(dev->nvram), 1, f);
	else
		fread(&dev->nvram, sizeof(dev->nvram), 1, f);
	fclose(f);
    }
}


static void
ncr53c8xx_reg_writeb(ncr53c8xx_t *dev, uint32_t offset, uint8_t val)
{
    uint8_t tmp = 0;

#define CASE_SET_REG24(name, addr) \
    case addr    : dev->name &= 0xffffff00; dev->name |= val;       break; \
    case addr + 1: dev->name &= 0xffff00ff; dev->name |= val << 8;  break; \
    case addr + 2: dev->name &= 0xff00ffff; dev->name |= val << 16; break;

#define CASE_SET_REG32(name, addr) \
    case addr    : dev->name &= 0xffffff00; dev->name |= val;       break; \
    case addr + 1: dev->name &= 0xffff00ff; dev->name |= val << 8;  break; \
    case addr + 2: dev->name &= 0xff00ffff; dev->name |= val << 16; break; \
    case addr + 3: dev->name &= 0x00ffffff; dev->name |= val << 24; break;

#ifdef DEBUG_NCR_REG
    ncr53c8xx_log("Write reg %02x = %02x\n", offset, val);
#endif

    dev->regop = 1;

    switch (offset) {
	case 0x00: /* SCNTL0 */
		dev->scntl0 = val;
		if (val & NCR_SCNTL0_START) {
			/* Looks like this (turn on bit 4 of SSTAT0 to mark arbitration in progress)
			   is enough to make BIOS v4.x happy. */
			ncr53c8xx_log("NCR 810: Selecting SCSI ID %i\n", dev->sdid);
			dev->sstat0 |= 0x10;
		}
		break;
	case 0x01: /* SCNTL1 */
		dev->scntl1 = val & ~NCR_SCNTL1_SST;
		if (val & NCR_SCNTL1_IARB) {
			ncr53c8xx_log("Arbitration lost\n");
			dev->sstat0 |= 0x08;
			dev->waiting = 0;
		}
		if (val & NCR_SCNTL1_RST) {
			if (!(dev->sstat0 & NCR_SSTAT0_RST)) {
				dev->sstat0 |= NCR_SSTAT0_RST;
				ncr53c8xx_script_scsi_interrupt(dev, NCR_SIST0_RST, 0);
			}
		} else
			dev->sstat0 &= ~NCR_SSTAT0_RST;
		break;
	case 0x02: /* SCNTL2 */
		val &= ~(NCR_SCNTL2_WSR | NCR_SCNTL2_WSS);
		dev->scntl2 = val;
		break;
	case 0x03: /* SCNTL3 */
		dev->scntl3 = val;
		break;
	case 0x04: /* SCID */
		dev->scid = val;
		break;
	case 0x05: /* SXFER */
		dev->sxfer = val;
		break;
	case 0x06: /* SDID */
		if ((dev->ssid & 0x80) && (val & 0xf) != (dev->ssid & 0xf))
			ncr53c8xx_log("Destination ID does not match SSID\n");
		dev->sdid = val & 0xf;
		break;
	case 0x07: /* GPREG */
		ncr53c8xx_log("NCR 810: GPREG write %02X\n", val);
		dev->gpreg = val;
		i2c_gpio_set(dev->i2c, (dev->gpreg & 0x02) || ((dev->gpcntl & 0x82) == 0x02), (dev->gpreg & 0x01) || ((dev->gpcntl & 0x41) == 0x01));
		break;
	case 0x08: /* SFBR */
		/* The CPU is not allowed to write to this register.  However the
		   SCRIPTS register move instructions are.  */
		dev->sfbr = val;
		break;
	case 0x09: /* SOCL */
		ncr53c8xx_log("NCR 810: SOCL write %02X\n", val);
		dev->socl = val;
		break;
	case 0x0a: case 0x0b:
		/* Openserver writes to these readonly registers on startup */
		return;
	case 0x0c: case 0x0d: case 0x0e: case 0x0f:
		/* Linux writes to these readonly registers on startup.  */
		return;
	CASE_SET_REG32(dsa, 0x10)
	case 0x14: /* ISTAT */
		ncr53c8xx_log("ISTAT write: %02X\n", val);
		tmp = dev->istat;
		dev->istat = (dev->istat & 0x0f) | (val & 0xf0);
		if ((val & NCR_ISTAT_ABRT) && !(val & NCR_ISTAT_SRST))
			ncr53c8xx_script_dma_interrupt(dev, NCR_DSTAT_ABRT);
		if (val & NCR_ISTAT_INTF) {
			dev->istat &= ~NCR_ISTAT_INTF;
			ncr53c8xx_update_irq(dev);
		}

		if ((dev->waiting == 1) && (val & NCR_ISTAT_SIGP)) {
			ncr53c8xx_log("Woken by SIGP\n");
			dev->waiting = 0;
			dev->dsp = dev->dnad;
			/* ncr53c8xx_execute_script(dev); */
		}
		if ((val & NCR_ISTAT_SRST) && !(tmp & NCR_ISTAT_SRST)) {
			ncr53c8xx_soft_reset(dev);
			ncr53c8xx_update_irq(dev);
			dev->istat = 0;
		}
		break;
	case 0x16: /* MBOX0 */
		dev->mbox0 = val;
		break;
	case 0x17: /* MBOX1 */
		dev->mbox1 = val;
		break;
	case 0x18: /* CTEST0 */
		/* nothing to do */
		break;
	case 0x19: /* CTEST1 */
		/* nothing to do */
		break;
	case 0x1a: /* CTEST2 */
		dev->ctest2 = val & NCR_CTEST2_PCICIE;
		break;
	case 0x1b: /* CTEST3 */
		dev->ctest3 = val & 0x0f;
		break;
	CASE_SET_REG32(temp, 0x1c)
	case 0x21: /* CTEST4 */
		if (val & 7)
			ncr53c8xx_log("Unimplemented CTEST4-FBL 0x%x\n", val);
		dev->ctest4 = val;
		break;
	case 0x22: /* CTEST5 */
		if (val & (NCR_CTEST5_ADCK | NCR_CTEST5_BBCK))
			ncr53c8xx_log("CTEST5 DMA increment not implemented\n");
		dev->ctest5 = val;
		break;
	CASE_SET_REG24(dbc, 0x24)
	CASE_SET_REG32(dnad, 0x28)
	case 0x2c: /* DSP[0:7] */
		dev->dsp &= 0xffffff00;
		dev->dsp |= val;
		break;
	case 0x2d: /* DSP[8:15] */
		dev->dsp &= 0xffff00ff;
		dev->dsp |= val << 8;
		break;
	case 0x2e: /* DSP[16:23] */
		dev->dsp &= 0xff00ffff;
		dev->dsp |= val << 16;
		break;
	case 0x2f: /* DSP[24:31] */
		dev->dsp &= 0x00ffffff;
		dev->dsp |= val << 24;
		if (!(dev->dmode & NCR_DMODE_MAN) && dev->sstop)
			ncr53c8xx_execute_script(dev);
		break;
	CASE_SET_REG32(dsps, 0x30)
	CASE_SET_REG32(scratcha, 0x34)
	case 0x38: /* DMODE */
		dev->dmode = val;
		break;
	case 0x39: /* DIEN */
		ncr53c8xx_log("DIEN write: %02X\n", val);
		dev->dien = val;
		ncr53c8xx_update_irq(dev);
		break;
	case 0x3a: /* SBR */
		dev->sbr = val;
		break;
	case 0x3b: /* DCNTL */
		dev->dcntl = val & ~(NCR_DCNTL_PFF | NCR_DCNTL_STD);
		if ((val & NCR_DCNTL_STD) && dev->sstop)
			ncr53c8xx_execute_script(dev);
		break;
	case 0x40: /* SIEN0 */
		dev->sien0 = val;
		ncr53c8xx_update_irq(dev);
		break;
	case 0x41: /* SIEN1 */
		dev->sien1 = val;
		ncr53c8xx_update_irq(dev);
		break;
	case 0x47: /* GPCNTL */
		ncr53c8xx_log("GPCNTL write: %02X\n", val);
		dev->gpcntl = val;
		break;
	case 0x48: /* STIME0 */
		dev->stime0 = val;
		break;
	case 0x49: /* STIME1 */
		if (val & 0xf) {
			ncr53c8xx_log("General purpose timer not implemented\n");
			/* ??? Raising the interrupt immediately seems to be sufficient
			   to keep the FreeBSD driver happy.  */
			ncr53c8xx_script_scsi_interrupt(dev, 0, NCR_SIST1_GEN);
		}
		break;
	case 0x4a: /* RESPID0 */
		dev->respid0 = val;
		break;
	case 0x4b: /* RESPID1 */
		if (dev->wide)
			dev->respid1 = val;
		break;
	case 0x4d: /* STEST1 */
		dev->stest1 = val;
		break;
	case 0x4e: /* STEST2 */
		if (val & 1)
			ncr53c8xx_log("Low level mode not implemented\n");
		dev->stest2 = val;
		break;
	case 0x4f: /* STEST3 */
		if (val & 0x41)
			ncr53c8xx_log("SCSI FIFO test mode not implemented\n");
		dev->stest3 = val;
		break;
	case 0x54:
	case 0x55:
		break;
	CASE_SET_REG32(scratchb, 0x5c)
	CASE_SET_REG32(scratchc, 0x60)
	CASE_SET_REG32(scratchd, 0x64)
	CASE_SET_REG32(scratche, 0x68)
	CASE_SET_REG32(scratchf, 0x6c)
	CASE_SET_REG32(scratchg, 0x70)
	CASE_SET_REG32(scratchh, 0x74)
	CASE_SET_REG32(scratchi, 0x78)
	CASE_SET_REG32(scratchj, 0x7c)
	default:
		ncr53c8xx_log("Unhandled writeb 0x%x = 0x%x\n", offset, val);
    }
#undef CASE_SET_REG24
#undef CASE_SET_REG32
}


static uint8_t
ncr53c8xx_reg_readb(ncr53c8xx_t *dev, uint32_t offset)
{
    uint8_t tmp;
#define CASE_GET_REG24(name, addr) \
    case addr: return dev->name & 0xff; \
    case addr + 1: return (dev->name >> 8) & 0xff; \
    case addr + 2: return (dev->name >> 16) & 0xff;

#define CASE_GET_REG32(name, addr) \
    case addr: return dev->name & 0xff; \
    case addr + 1: return (dev->name >> 8) & 0xff; \
    case addr + 2: return (dev->name >> 16) & 0xff; \
    case addr + 3: return (dev->name >> 24) & 0xff;

#define CASE_GET_REG32_COND(name, addr) \
    case addr: if (dev->wide) \
			return dev->name & 0xff; \
		else \
			return 0x00; \
    case addr + 1: if (dev->wide) \
			return (dev->name >> 8) & 0xff; \
		else \
			return 0x00; \
    case addr + 2: if (dev->wide) \
			return (dev->name >> 16) & 0xff; \
		else \
			return 0x00; \
    case addr + 3: if (dev->wide) \
			return (dev->name >> 24) & 0xff; \
		else \
			return 0x00;

    dev->regop = 1;

    switch (offset) {
	case 0x00: /* SCNTL0 */
		ncr53c8xx_log("NCR 810: Read SCNTL0 %02X\n", dev->scntl0);
		return dev->scntl0;
	case 0x01: /* SCNTL1 */
		ncr53c8xx_log("NCR 810: Read SCNTL1 %02X\n", dev->scntl1);
		return dev->scntl1;
	case 0x02: /* SCNTL2 */
		ncr53c8xx_log("NCR 810: Read SCNTL2 %02X\n", dev->scntl2);
		return dev->scntl2;
	case 0x03: /* SCNTL3 */
		ncr53c8xx_log("NCR 810: Read SCNTL3 %02X\n", dev->scntl3);
		return dev->scntl3;
	case 0x04: /* SCID */
		ncr53c8xx_log("NCR 810: Read SCID %02X\n", dev->scid);
		return dev->scid & ~0x40;
	case 0x05: /* SXFER */
		ncr53c8xx_log("NCR 810: Read SXFER %02X\n", dev->sxfer);
		return dev->sxfer;
	case 0x06: /* SDID */
		ncr53c8xx_log("NCR 810: Read SDID %02X\n", dev->sdid);
		return dev->sdid;
	case 0x07: /* GPREG */
		tmp = (dev->gpreg & (dev->gpcntl ^ 0x1f)) & 0x1f;
		if ((dev->gpcntl & 0x41) == 0x01) {
			tmp &= 0xfe;
			if (i2c_gpio_get_sda(dev->i2c))
				tmp |= 0x01;
		}
		if ((dev->gpcntl & 0x82) == 0x02) {
			tmp &= 0xfd;
			if (i2c_gpio_get_scl(dev->i2c))
				tmp |= 0x02;
		}

		ncr53c8xx_log("NCR 810: Read GPREG %02X\n", tmp);
		return tmp;
	case 0x08: /* Revision ID */
		ncr53c8xx_log("NCR 810: Read REVID 00\n");
		return 0x00;
	case 0xa: /* SSID */
		ncr53c8xx_log("NCR 810: Read SSID %02X\n", dev->ssid);
		return dev->ssid;
	case 0xb: /* SBCL */
		/* Bit 7 = REQ (SREQ/ status)
		   Bit 6 = ACK (SACK/ status)
		   Bit 5 = BSY (SBSY/ status)
		   Bit 4 = SEL (SSEL/ status)
		   Bit 3 = ATN (SATN/ status)
		   Bit 2 = MSG (SMSG/ status)
		   Bit 1 = C/D (SC_D/ status)
		   Bit 0 = I/O (SI_O/ status) */
		tmp = (dev->sstat1 & 7);
		ncr53c8xx_log("NCR 810: Read SBCL %02X\n", tmp);
		return tmp;	/* For now, return the MSG, C/D, and I/O bits from SSTAT1. */
	case 0xc: /* DSTAT */
		tmp = dev->dstat | NCR_DSTAT_DFE;
		if ((dev->istat & NCR_ISTAT_INTF) == 0)
			dev->dstat = 0;
		ncr53c8xx_update_irq(dev);
		ncr53c8xx_log("NCR 810: Read DSTAT %02X\n", tmp);
		return tmp;
	case 0x0d: /* SSTAT0 */
		ncr53c8xx_log("NCR 810: Read SSTAT0 %02X\n", dev->sstat0);
		return dev->sstat0;
	case 0x0e: /* SSTAT1 */
		ncr53c8xx_log("NCR 810: Read SSTAT1 %02X\n", dev->sstat1);
		return dev->sstat1;
	case 0x0f: /* SSTAT2 */
		ncr53c8xx_log("NCR 810: Read SSTAT2 %02X\n", dev->scntl1 & NCR_SCNTL1_CON ? 0 : 2);
		return dev->scntl1 & NCR_SCNTL1_CON ? 0 : 2;
	CASE_GET_REG32(dsa, 0x10)
	case 0x14: /* ISTAT */
		ncr53c8xx_log("NCR 810: Read ISTAT %02X\n", dev->istat);
		tmp = dev->istat;
		return tmp;
	case 0x16: /* MBOX0 */
		if (dev->wide)
			return 0x00;
		ncr53c8xx_log("NCR 810: Read MBOX0 %02X\n", dev->mbox0);
		return dev->mbox0;
	case 0x17: /* MBOX1 */
		if (dev->wide)
			return 0x00;
		ncr53c8xx_log("NCR 810: Read MBOX1 %02X\n", dev->mbox1);
		return dev->mbox1;
	case 0x18: /* CTEST0 */
		ncr53c8xx_log("NCR 810: Read CTEST0 FF\n");
		return 0xff;
	case 0x19: /* CTEST1 */
		ncr53c8xx_log("NCR 810: Read CTEST1 F0\n");
		return 0xf0;	/* dma fifo empty */
	case 0x1a: /* CTEST2 */
		tmp = dev->ctest2 | NCR_CTEST2_DACK | NCR_CTEST2_CM;
		if (dev->istat & NCR_ISTAT_SIGP) {
			dev->istat &= ~NCR_ISTAT_SIGP;
			tmp |= NCR_CTEST2_SIGP;
		}
		ncr53c8xx_log("NCR 810: Read CTEST2 %02X\n", tmp);
		return tmp;
	case 0x1b: /* CTEST3 */
		ncr53c8xx_log("NCR 810: Read CTEST3 %02X\n",
			      (dev->ctest3 & (0x08 | 0x02 | 0x01)) | ((dev->chip_rev & 0x0f) << 4));
		return (dev->ctest3 & (0x08 | 0x02 | 0x01)) | ((dev->chip_rev & 0x0f) << 4);
	CASE_GET_REG32(temp, 0x1c)
	case 0x20: /* DFIFO */
		ncr53c8xx_log("NCR 810: Read DFIFO 00\n");
		return 0;
	case 0x21: /* CTEST4 */
		ncr53c8xx_log("NCR 810: Read CTEST4 %02X\n", dev->ctest4);
		return dev->ctest4;
	case 0x22: /* CTEST5 */
		ncr53c8xx_log("NCR 810: Read CTEST5 %02X\n", dev->ctest5);
		return dev->ctest5;
	case 0x23: /* CTEST6 */
		ncr53c8xx_log("NCR 810: Read CTEST6 00\n");
		return 0;
	CASE_GET_REG24(dbc, 0x24)
	case 0x27: /* DCMD */
		ncr53c8xx_log("NCR 810: Read DCMD %02X\n", dev->dcmd);
		return dev->dcmd;
	CASE_GET_REG32(dnad, 0x28)
	CASE_GET_REG32(dsp, 0x2c)
	CASE_GET_REG32(dsps, 0x30)
	CASE_GET_REG32(scratcha, 0x34)
	case 0x38: /* DMODE */
		ncr53c8xx_log("NCR 810: Read DMODE %02X\n", dev->dmode);
		return dev->dmode;
	case 0x39: /* DIEN */
		ncr53c8xx_log("NCR 810: Read DIEN %02X\n", dev->dien);
		return dev->dien;
	case 0x3a: /* SBR */
		ncr53c8xx_log("NCR 810: Read SBR %02X\n", dev->sbr);
		return dev->sbr;
	case 0x3b: /* DCNTL */
		ncr53c8xx_log("NCR 810: Read DCNTL %02X\n", dev->dcntl);
		return dev->dcntl;
	CASE_GET_REG32(adder, 0x3c)	/* ADDER Output (Debug of relative jump address) */
	case 0x40: /* SIEN0 */
		ncr53c8xx_log("NCR 810: Read SIEN0 %02X\n", dev->sien0);
		return dev->sien0;
	case 0x41: /* SIEN1 */
		ncr53c8xx_log("NCR 810: Read SIEN1 %02X\n", dev->sien1);
		return dev->sien1;
	case 0x42: /* SIST0 */
		tmp = dev->sist0;
		dev->sist0 = 0x00;
		ncr53c8xx_update_irq(dev);
		ncr53c8xx_log("NCR 810: Read SIST0 %02X\n", tmp);
		return tmp;
	case 0x43: /* SIST1 */
		tmp = dev->sist1;
		dev->sist1 = 0x00;
		ncr53c8xx_update_irq(dev);
		ncr53c8xx_log("NCR 810: Read SIST1 %02X\n", tmp);
		return tmp;
	case 0x44: /* SLPAR */
		if (!dev->wide)
			return 0x00;
		ncr53c8xx_log("NCR 810: Read SLPAR %02X\n", dev->stime0);
		return dev->slpar;
	case 0x45: /* SWIDE */
		if (!dev->wide)
			return 0x00;
		ncr53c8xx_log("NCR 810: Read SWIDE %02X\n", dev->stime0);
		return dev->swide;
	case 0x46: /* MACNTL */
		ncr53c8xx_log("NCR 810: Read MACNTL 4F\n");
		return 0x4f;
	case 0x47: /* GPCNTL */
		ncr53c8xx_log("NCR 810: Read GPCNTL %02X\n", dev->gpcntl);
		return dev->gpcntl;
	case 0x48: /* STIME0 */
		ncr53c8xx_log("NCR 810: Read STIME0 %02X\n", dev->stime0);
		return dev->stime0;
	case 0x4a: /* RESPID0 */
		if (dev->wide) {
			ncr53c8xx_log("NCR 810: Read RESPID0 %02X\n", dev->respid0);
		} else {
			ncr53c8xx_log("NCR 810: Read RESPID %02X\n", dev->respid0);
		}
		return dev->respid0;
	case 0x4b: /* RESPID1 */
		if (!dev->wide)
			return 0x00;
		ncr53c8xx_log("NCR 810: Read RESPID1 %02X\n", dev->respid1);
		return dev->respid1;
	case 0x4c: /* STEST0 */
		ncr53c8xx_log("NCR 810: Read STEST0 %02X\n", dev->stest1);
		return 0x00;
	case 0x4d: /* STEST1 */
		ncr53c8xx_log("NCR 810: Read STEST1 %02X\n", dev->stest1);
		return dev->stest1;
	case 0x4e: /* STEST2 */
		ncr53c8xx_log("NCR 810: Read STEST2 %02X\n", dev->stest2);
		return dev->stest2;
	case 0x4f: /* STEST3 */
		ncr53c8xx_log("NCR 810: Read STEST3 %02X\n", dev->stest3);
		return dev->stest3;
	case 0x50: /* SIDL0 */
		/* This is needed by the linux drivers.  We currently only update it
		   during the MSG IN phase.  */
		if (dev->wide) {
			ncr53c8xx_log("NCR 810: Read SIDL0 %02X\n", dev->sidl0);
		} else {
			ncr53c8xx_log("NCR 810: Read SIDL %02X\n", dev->sidl0);
		}
		return dev->sidl0;
	case 0x51: /* SIDL1 */
		if (!dev->wide)
			return 0x00;
		ncr53c8xx_log("NCR 810: Read SIDL1 %02X\n", dev->sidl1);
		return dev->sidl1;
	case 0x52: /* STEST4 */
		ncr53c8xx_log("NCR 810: Read STEST4 E0\n");
		return 0xe0;
	case 0x58: /* SBDL */
		/* Some drivers peek at the data bus during the MSG IN phase.  */
		if ((dev->sstat1 & PHASE_MASK) == PHASE_MI) {
			ncr53c8xx_log("NCR 810: Read SBDL %02X\n", dev->msg[0]);
			return dev->msg[0];
		}
		ncr53c8xx_log("NCR 810: Read SBDL 00\n");
		return 0;
	case 0x59: /* SBDL high */
		ncr53c8xx_log("NCR 810: Read SBDLH 00\n");
		return 0;
	CASE_GET_REG32(scratchb, 0x5c)
	CASE_GET_REG32_COND(scratchc, 0x60)
	CASE_GET_REG32_COND(scratchd, 0x64)
	CASE_GET_REG32_COND(scratche, 0x68)
	CASE_GET_REG32_COND(scratchf, 0x6c)
	CASE_GET_REG32_COND(scratchg, 0x70)
	CASE_GET_REG32_COND(scratchh, 0x74)
	CASE_GET_REG32_COND(scratchi, 0x78)
	CASE_GET_REG32_COND(scratchj, 0x7c)
    }
    ncr53c8xx_log("readb 0x%x\n", offset);
    return 0;

#undef CASE_GET_REG24
#undef CASE_GET_REG32
}


static uint8_t
ncr53c8xx_io_readb(uint16_t addr, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;
    return ncr53c8xx_reg_readb(dev, addr & 0xff);
}


static uint16_t
ncr53c8xx_io_readw(uint16_t addr, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;
    uint16_t val;

    addr &= 0xff;
    val = ncr53c8xx_reg_readb(dev, addr);
    val |= ncr53c8xx_reg_readb(dev, addr + 1) << 8;
    return val;
}


static uint32_t
ncr53c8xx_io_readl(uint16_t addr, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;
    uint32_t val;

    addr &= 0xff;
    val = ncr53c8xx_reg_readb(dev, addr);
    val |= ncr53c8xx_reg_readb(dev, addr + 1) << 8;
    val |= ncr53c8xx_reg_readb(dev, addr + 2) << 16;
    val |= ncr53c8xx_reg_readb(dev, addr + 3) << 24;
    return val;
}


static void
ncr53c8xx_io_writeb(uint16_t addr, uint8_t val, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;
	ncr53c8xx_reg_writeb(dev, addr & 0xff, val);
}


static void
ncr53c8xx_io_writew(uint16_t addr, uint16_t val, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;
	addr &= 0xff;
	ncr53c8xx_reg_writeb(dev, addr, val & 0xff);
	ncr53c8xx_reg_writeb(dev, addr + 1, (val >> 8) & 0xff);
}


static void
ncr53c8xx_io_writel(uint16_t addr, uint32_t val, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;
    addr &= 0xff;
    ncr53c8xx_reg_writeb(dev, addr, val & 0xff);
    ncr53c8xx_reg_writeb(dev, addr + 1, (val >> 8) & 0xff);
    ncr53c8xx_reg_writeb(dev, addr + 2, (val >> 16) & 0xff);
    ncr53c8xx_reg_writeb(dev, addr + 3, (val >> 24) & 0xff);
}


static void
ncr53c8xx_mmio_writeb(uint32_t addr, uint8_t val, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;

    ncr53c8xx_reg_writeb(dev, addr & 0xff, val);
}


static void
ncr53c8xx_mmio_writew(uint32_t addr, uint16_t val, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;

    addr &= 0xff;
    ncr53c8xx_reg_writeb(dev, addr, val & 0xff);
    ncr53c8xx_reg_writeb(dev, addr + 1, (val >> 8) & 0xff);
}


static void
ncr53c8xx_mmio_writel(uint32_t addr, uint32_t val, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;

    addr &= 0xff;
    ncr53c8xx_reg_writeb(dev, addr, val & 0xff);
    ncr53c8xx_reg_writeb(dev, addr + 1, (val >> 8) & 0xff);
    ncr53c8xx_reg_writeb(dev, addr + 2, (val >> 16) & 0xff);
    ncr53c8xx_reg_writeb(dev, addr + 3, (val >> 24) & 0xff);
}


static uint8_t
ncr53c8xx_mmio_readb(uint32_t addr, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;

    return ncr53c8xx_reg_readb(dev, addr & 0xff);
}


static uint16_t
ncr53c8xx_mmio_readw(uint32_t addr, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;
    uint16_t val;

    addr &= 0xff;
    val = ncr53c8xx_reg_readb(dev, addr);
    val |= ncr53c8xx_reg_readb(dev, addr + 1) << 8;
    return val;
}


static uint32_t
ncr53c8xx_mmio_readl(uint32_t addr, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;
    uint32_t val;

    addr &= 0xff;
    val = ncr53c8xx_reg_readb(dev, addr);
    val |= ncr53c8xx_reg_readb(dev, addr + 1) << 8;
    val |= ncr53c8xx_reg_readb(dev, addr + 2) << 16;
    val |= ncr53c8xx_reg_readb(dev, addr + 3) << 24;

    return val;
}


static void
ncr53c8xx_ram_writeb(uint32_t addr, uint8_t val, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;

    dev->ram[addr & 0x0fff] = val;
}


static void
ncr53c8xx_ram_writew(uint32_t addr, uint16_t val, void *p)
{
    ncr53c8xx_ram_writeb(addr, val & 0xff, p);
    ncr53c8xx_ram_writeb(addr + 1, (val >> 8) & 0xff, p);
}


static void
ncr53c8xx_ram_writel(uint32_t addr, uint32_t val, void *p)
{
    ncr53c8xx_ram_writeb(addr, val & 0xff, p);
    ncr53c8xx_ram_writeb(addr + 1, (val >> 8) & 0xff, p);
    ncr53c8xx_ram_writeb(addr + 2, (val >> 16) & 0xff, p);
    ncr53c8xx_ram_writeb(addr + 3, (val >> 24) & 0xff, p);
}


static uint8_t
ncr53c8xx_ram_readb(uint32_t addr, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;

    return dev->ram[addr & 0x0fff];
}


static uint16_t
ncr53c8xx_ram_readw(uint32_t addr, void *p)
{
    uint16_t val;

    val = ncr53c8xx_ram_readb(addr, p);
    val |= ncr53c8xx_ram_readb(addr + 1, p) << 8;

    return val;
}


static uint32_t
ncr53c8xx_ram_readl(uint32_t addr, void *p)
{
    uint32_t val;

    val = ncr53c8xx_ram_readb(addr, p);
    val |= ncr53c8xx_ram_readb(addr + 1, p) << 8;
    val |= ncr53c8xx_ram_readb(addr + 2, p) << 16;
    val |= ncr53c8xx_ram_readb(addr + 3, p) << 24;

    return val;
}


static void
ncr53c8xx_io_set(ncr53c8xx_t *dev, uint32_t base, uint16_t len)
{
    ncr53c8xx_log("NCR53c8xx: [PCI] Setting I/O handler at %04X\n", base);
    io_sethandler(base, len,
		  ncr53c8xx_io_readb, ncr53c8xx_io_readw, ncr53c8xx_io_readl,
		  ncr53c8xx_io_writeb, ncr53c8xx_io_writew, ncr53c8xx_io_writel, dev);
}


static void
ncr53c8xx_io_remove(ncr53c8xx_t *dev, uint32_t base, uint16_t len)
{
    ncr53c8xx_log("NCR53c8xx: Removing I/O handler at %04X\n", base);
    io_removehandler(base, len,
		     ncr53c8xx_io_readb, ncr53c8xx_io_readw, ncr53c8xx_io_readl,
                     ncr53c8xx_io_writeb, ncr53c8xx_io_writew, ncr53c8xx_io_writel, dev);
}


static void
ncr53c8xx_mem_init(ncr53c8xx_t *dev, uint32_t addr)
{
    mem_mapping_add(&dev->mmio_mapping, addr, 0x100,
		    ncr53c8xx_mmio_readb, ncr53c8xx_mmio_readw, ncr53c8xx_mmio_readl,
		    ncr53c8xx_mmio_writeb, ncr53c8xx_mmio_writew, ncr53c8xx_mmio_writel,
		    NULL, MEM_MAPPING_EXTERNAL, dev);
}


static void
ncr53c8xx_ram_init(ncr53c8xx_t *dev, uint32_t addr)
{
    mem_mapping_add(&dev->ram_mapping, addr, 0x1000,
		    ncr53c8xx_ram_readb, ncr53c8xx_ram_readw, ncr53c8xx_ram_readl,
		    ncr53c8xx_ram_writeb, ncr53c8xx_ram_writew, ncr53c8xx_ram_writel,
		    NULL, MEM_MAPPING_EXTERNAL, dev);
}


static void
ncr53c8xx_mem_set_addr(ncr53c8xx_t *dev, uint32_t base)
{
    mem_mapping_set_addr(&dev->mmio_mapping, base, 0x100);
}


static void
ncr53c8xx_ram_set_addr(ncr53c8xx_t *dev, uint32_t base)
{
    mem_mapping_set_addr(&dev->ram_mapping, base, 0x1000);
}


static void
ncr53c8xx_bios_set_addr(ncr53c8xx_t *dev, uint32_t base)
{
    if (dev->has_bios == 2)
	mem_mapping_set_addr(&dev->bios.mapping, base, 0x10000);
    else if (dev->has_bios == 1)
	mem_mapping_set_addr(&dev->bios.mapping, base, 0x04000);
}


static void
ncr53c8xx_mem_disable(ncr53c8xx_t *dev)
{
    mem_mapping_disable(&dev->mmio_mapping);
}


static void
ncr53c8xx_ram_disable(ncr53c8xx_t *dev)
{
    mem_mapping_disable(&dev->ram_mapping);
}


static void
ncr53c8xx_bios_disable(ncr53c8xx_t *dev)
{
    mem_mapping_disable(&dev->bios.mapping);
}


uint8_t	ncr53c8xx_pci_regs[256];
bar_t	ncr53c8xx_pci_bar[4];


static uint8_t
ncr53c8xx_pci_read(int func, int addr, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;

    ncr53c8xx_log("NCR53c8xx: Reading register %02X\n", addr & 0xff);

    if ((addr >= 0x80) && (addr <= 0xFF))
	return ncr53c8xx_reg_readb(dev, addr & 0x7F);

    switch (addr) {
	case 0x00:
		return 0x00;
	case 0x01:
		return 0x10;
	case 0x02:
		return dev->chip;
	case 0x03:
		return 0x00;
	case 0x04:
		return ncr53c8xx_pci_regs[0x04] & 0x57;	/*Respond to IO and memory accesses*/
	case 0x05:
		return ncr53c8xx_pci_regs[0x05] & 0x01;
	case 0x07:
		return 2;
	case 0x08:
		return dev->chip_rev;		/*Revision ID*/
	case 0x09:
		return 0;			/*Programming interface*/
	case 0x0A:
		return 0;			/*devubclass*/
	case 0x0B:
		return 1;			/*Class code*/
	case 0x0C:
	case 0x0D:
		return ncr53c8xx_pci_regs[addr];
	case 0x0E:
		return 0;			/*Header type */
	case 0x10:
		return 1;			/*I/O space*/
	case 0x11:
		return ncr53c8xx_pci_bar[0].addr_regs[1];
	case 0x12:
		return ncr53c8xx_pci_bar[0].addr_regs[2];
	case 0x13:
		return ncr53c8xx_pci_bar[0].addr_regs[3];
	case 0x14:
		return 0;			/*Memory space*/
	case 0x15:
		return ncr53c8xx_pci_bar[1].addr_regs[1];
	case 0x16:
		return ncr53c8xx_pci_bar[1].addr_regs[2];
	case 0x17:
		return ncr53c8xx_pci_bar[1].addr_regs[3];
	case 0x18:
		return 0;			/*Memory space*/
	case 0x19:
		if (!dev->wide)
			return 0;
		return ncr53c8xx_pci_bar[2].addr_regs[1];
	case 0x1A:
		if (!dev->wide)
			return 0;
		return ncr53c8xx_pci_bar[2].addr_regs[2];
	case 0x1B:
		if (!dev->wide)
			return 0;
		return ncr53c8xx_pci_bar[2].addr_regs[3];
	case 0x2C:
		return 0x00;
	case 0x2D:
		if (dev->wide)
			return 0;
		return 0x10;
	case 0x2E:
		if (dev->wide)
			return 0;
		return 0x01;
	case 0x2F:
		return 0x00;
	case 0x30:
		return ncr53c8xx_pci_bar[3].addr_regs[0] & 0x01;
	case 0x31:
		return ncr53c8xx_pci_bar[3].addr_regs[1];
	case 0x32:
		return ncr53c8xx_pci_bar[3].addr_regs[2];
	case 0x33:
		return ncr53c8xx_pci_bar[3].addr_regs[3];
	case 0x3C:
		return dev->irq;
	case 0x3D:
		return PCI_INTA;
	case 0x3E:
		return 0x11;
	case 0x3F:
		return 0x40;
    }

    return(0);
}


static void
ncr53c8xx_pci_write(int func, int addr, uint8_t val, void *p)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)p;
    uint8_t valxor;

    ncr53c8xx_log("NCR53c8xx: Write value %02X to register %02X\n", val, addr & 0xff);

    if ((addr >= 0x80) && (addr <= 0xFF)) {
	ncr53c8xx_reg_writeb(dev, addr & 0x7F, val);
	return;
    }

    switch (addr)
	{
	case 0x04:
		valxor = (val & 0x57) ^ ncr53c8xx_pci_regs[addr];
		if (valxor & PCI_COMMAND_IO) {
			ncr53c8xx_io_remove(dev, dev->PCIBase, 0x0100);
			if ((dev->PCIBase != 0) && (val & PCI_COMMAND_IO))
				ncr53c8xx_io_set(dev, dev->PCIBase, 0x0100);
		}
		if (valxor & PCI_COMMAND_MEM) {
			ncr53c8xx_mem_disable(dev);
			if ((dev->MMIOBase != 0) && (val & PCI_COMMAND_MEM))
				ncr53c8xx_mem_set_addr(dev, dev->MMIOBase);
			if (dev->wide) {
				ncr53c8xx_ram_disable(dev);
				if ((dev->RAMBase != 0) && (val & PCI_COMMAND_MEM))
					ncr53c8xx_ram_set_addr(dev, dev->RAMBase);
			}
		}
		ncr53c8xx_pci_regs[addr] = val & 0x57;
		break;

	case 0x05:
		ncr53c8xx_pci_regs[addr] = val & 0x01;
		break;

	case 0x0C:
	case 0x0D:
		ncr53c8xx_pci_regs[addr] = val;
		break;

	case 0x10: case 0x11: case 0x12: case 0x13:
		/* I/O Base set. */
		/* First, remove the old I/O. */
		ncr53c8xx_io_remove(dev, dev->PCIBase, 0x0100);
		/* Then let's set the PCI regs. */
		ncr53c8xx_pci_bar[0].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		ncr53c8xx_pci_bar[0].addr &= 0xff00;
		dev->PCIBase = ncr53c8xx_pci_bar[0].addr;
		/* Log the new base. */
		ncr53c8xx_log("NCR53c8xx: New I/O base is %04X\n" , dev->PCIBase);
		/* We're done, so get out of the here. */
		if (ncr53c8xx_pci_regs[4] & PCI_COMMAND_IO) {
			if (dev->PCIBase != 0) {
				ncr53c8xx_io_set(dev, dev->PCIBase, 0x0100);
			}
		}
		return;

	case 0x15: case 0x16: case 0x17:
		/* MMIO Base set. */
		/* First, remove the old I/O. */
		ncr53c8xx_mem_disable(dev);
		/* Then let's set the PCI regs. */
		ncr53c8xx_pci_bar[1].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		ncr53c8xx_pci_bar[1].addr &= 0xffffc000;
		dev->MMIOBase = ncr53c8xx_pci_bar[1].addr & 0xffffc000;
		/* Log the new base. */
		ncr53c8xx_log("NCR53c8xx: New MMIO base is %08X\n" , dev->MMIOBase);
		/* We're done, so get out of the here. */
		if (ncr53c8xx_pci_regs[4] & PCI_COMMAND_MEM) {
			if (dev->MMIOBase != 0)
				ncr53c8xx_mem_set_addr(dev, dev->MMIOBase);
		}
		return;

	case 0x19: case 0x1A: case 0x1B:
		if (!dev->wide)
			return;
		/* RAM Base set. */
		/* First, remove the old I/O. */
		ncr53c8xx_ram_disable(dev);
		/* Then let's set the PCI regs. */
		ncr53c8xx_pci_bar[2].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		ncr53c8xx_pci_bar[2].addr &= 0xfffff000;
		dev->RAMBase = ncr53c8xx_pci_bar[2].addr & 0xfffff000;
		/* Log the new base. */
		ncr53c8xx_log("NCR53c8xx: New RAM base is %08X\n" , dev->RAMBase);
		/* We're done, so get out of the here. */
		if (ncr53c8xx_pci_regs[4] & PCI_COMMAND_MEM) {
			if (dev->RAMBase != 0)
				ncr53c8xx_ram_set_addr(dev, dev->RAMBase);
		}
		return;

	case 0x30: case 0x31: case 0x32: case 0x33:
		if (dev->has_bios == 0)
			return;
		/* BIOS Base set. */
		/* First, remove the old I/O. */
		ncr53c8xx_bios_disable(dev);
		/* Then let's set the PCI regs. */
		ncr53c8xx_pci_bar[3].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		ncr53c8xx_pci_bar[3].addr &= (dev->bios_mask | 0x00000001);
		dev->BIOSBase = ncr53c8xx_pci_bar[3].addr & dev->bios_mask;
		ncr53c8xx_log("BIOS BAR: %08X\n", dev->BIOSBase | ncr53c8xx_pci_bar[3].addr_regs[0]);
		/* Log the new base. */
		ncr53c8xx_log("NCR53c8xx: New BIOS base is %08X\n" , dev->BIOSBase);
		/* We're done, so get out of the here. */
		if (ncr53c8xx_pci_bar[3].addr_regs[0] & 0x01)
			ncr53c8xx_bios_set_addr(dev, dev->BIOSBase);
		return;

	case 0x3C:
		ncr53c8xx_pci_regs[addr] = val;
		dev->irq = val;
		return;
    }
}


static void *
ncr53c8xx_init(const device_t *info)
{
    ncr53c8xx_t *dev;

    dev = malloc(sizeof(ncr53c8xx_t));
    memset(dev, 0x00, sizeof(ncr53c8xx_t));

    dev->bus = scsi_get_bus();

    dev->chip_rev = 0;
    dev->chip = info->local & 0xff;

    if ((dev->chip != CHIP_810) && (dev->chip != CHIP_820) && !(info->local & 0x8000)) {
	dev->has_bios = device_get_config_int("bios");

	/* We have to auto-patch the BIOS to have the correct PCI Device ID, because for some reason, they all ship with
	   the PCI Device ID set to that of the NCR 53c825, but for a machine BIOS to load the SCSI BIOS correctly, the
	   PCI Device ID in the BIOS' PCIR block must match the one returned in the PCI registers. */
	if (dev->has_bios == 2) {
	    rom_init(&dev->bios, SYM53C8XX_SDMS4_ROM, 0xd0000, 0x10000, 0xffff, 0, MEM_MAPPING_EXTERNAL);
	    ncr53c8xx_log("BIOS v4.19: Old BIOS CHIP ID: %02X, old BIOS checksum: %02X\n", dev->bios.rom[0x0022], dev->bios.rom[0xffff]);
	    dev->bios.rom[0xffff] += (dev->bios.rom[0x0022] - dev->chip);
	    dev->bios.rom[0x0022] = dev->chip;
	    ncr53c8xx_log("BIOS v4.19: New BIOS CHIP ID: %02X, old BIOS checksum: %02X\n", dev->bios.rom[0x0022], dev->bios.rom[0xffff]);
	} else if (dev->has_bios == 1) {
	    rom_init(&dev->bios, NCR53C8XX_SDMS3_ROM, 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
	    ncr53c8xx_log("BIOS v3.07: Old BIOS CHIP ID: %02X, old BIOS checksum: %02X\n", dev->bios.rom[0x3fcb], dev->bios.rom[0x3fff]);
	    dev->bios.rom[0x3fff] += (dev->bios.rom[0x3fcb] - dev->chip);
	    dev->bios.rom[0x3fcb] = dev->chip;
	    ncr53c8xx_log("BIOS v3.07: New BIOS CHIP ID: %02X, old BIOS checksum: %02X\n", dev->bios.rom[0x3fcb], dev->bios.rom[0x3fff]);
	}
    } else
	dev->has_bios = 0;

    if (info->local & 0x8000)
	dev->pci_slot = pci_add_card(PCI_ADD_SCSI, ncr53c8xx_pci_read, ncr53c8xx_pci_write, dev);
    else
	dev->pci_slot = pci_add_card(PCI_ADD_NORMAL, ncr53c8xx_pci_read, ncr53c8xx_pci_write, dev);

    if (dev->chip == CHIP_875) {
	dev->chip_rev = 0x04;
	dev->nvr_path = "ncr53c875.nvr";
	dev->wide = 1;
    } else if (dev->chip == CHIP_860) {
	dev->chip_rev = 0x04;
	dev->nvr_path = "ncr53c860.nvr";
	dev->wide = 1;
    } else if (dev->chip == CHIP_820) {
	dev->nvr_path = "ncr53c820.nvr";
	dev->wide = 1;
    } else if (dev->chip == CHIP_825) {
	dev->chip_rev = 0x26;
	dev->nvr_path = "ncr53c825a.nvr";
	dev->wide = 1;
    } else if (dev->chip == CHIP_810) {
	dev->nvr_path = "ncr53c810.nvr";
	dev->wide = 0;
    } else if (dev->chip == CHIP_815) {
	dev->chip_rev = 0x04;
	dev->nvr_path = "ncr53c815.nvr";
	dev->wide = 0;
    }

    ncr53c8xx_pci_bar[0].addr_regs[0] = 1;
    ncr53c8xx_pci_bar[1].addr_regs[0] = 0;
    ncr53c8xx_pci_regs[0x04] = 3;

    if (dev->has_bios == 2) {
	ncr53c8xx_pci_bar[3].addr = 0xffff0000;
	dev->bios_mask = 0xffff0000;
    } else if (dev->has_bios == 1) {
	ncr53c8xx_pci_bar[3].addr = 0xffffc000;
	dev->bios_mask = 0xffffc000;
    } else {
	ncr53c8xx_pci_bar[3].addr = 0x00000000;
	dev->bios_mask = 0x00000000;
    }

    ncr53c8xx_mem_init(dev, 0x0fffff00);
    ncr53c8xx_mem_disable(dev);

    ncr53c8xx_pci_bar[2].addr_regs[0] = 0;

    if (dev->wide) {
	ncr53c8xx_ram_init(dev, 0x0ffff000);
	ncr53c8xx_ram_disable(dev);
    }

    if (dev->has_bios)
	ncr53c8xx_bios_disable(dev);

    dev->i2c = i2c_gpio_init("nvr_ncr53c8xx");
    dev->eeprom = i2c_eeprom_init(i2c_gpio_get_bus(dev->i2c), 0x50, dev->nvram, sizeof(dev->nvram), 1);

    /* Load the serial EEPROM. */
    ncr53c8xx_eeprom(dev, 0);

    ncr53c8xx_soft_reset(dev);

    timer_add(&dev->timer, ncr53c8xx_callback, dev, 0);

    return(dev);
}


static void
ncr53c8xx_close(void *priv)
{
    ncr53c8xx_t *dev = (ncr53c8xx_t *)priv;

    if (dev) {
	if (dev->eeprom)
		i2c_eeprom_close(dev->eeprom);

	if (dev->i2c)
		i2c_gpio_close(dev->i2c);

	/* Save the serial EEPROM. */
	ncr53c8xx_eeprom(dev, 1);

	free(dev);
	dev = NULL;
    }
}

static const device_config_t ncr53c8xx_pci_config[] = {
// clang-format off
    {
        .name = "bios",
        .description = "BIOS",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 1,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "SDMS 4.x BIOS", .value = 2 },
            { .description = "SDMS 3.x BIOS", .value = 1 },
            { .description = "Disable BIOS",  .value = 0 },
            { .description = ""                          }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

const device_t ncr53c810_pci_device = {
    .name = "NCR 53c810",
    .internal_name = "ncr53c810",
    .flags = DEVICE_PCI,
    .local = CHIP_810,
    .init = ncr53c8xx_init,
    .close = ncr53c8xx_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ncr53c810_onboard_pci_device = {
    .name = "NCR 53c810 On-Board",
    .internal_name = "ncr53c810_onboard",
    .flags = DEVICE_PCI,
    .local = 0x8001,
    .init = ncr53c8xx_init,
    .close = ncr53c8xx_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ncr53c815_pci_device = {
    .name = "NCR 53c815",
    .internal_name = "ncr53c815",
    .flags = DEVICE_PCI,
    .local = CHIP_815,
    .init = ncr53c8xx_init,
    .close = ncr53c8xx_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    ncr53c8xx_pci_config
};

const device_t ncr53c820_pci_device = {
    .name = "NCR 53c820",
    .internal_name = "ncr53c820",
    .flags = DEVICE_PCI,
    .local = CHIP_820,
    .init = ncr53c8xx_init,
    .close = ncr53c8xx_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ncr53c825a_pci_device = {
    .name = "NCR 53c825A",
    .internal_name = "ncr53c825a",
    .flags = DEVICE_PCI,
    .local = CHIP_825,
    .init = ncr53c8xx_init,
    .close = ncr53c8xx_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ncr53c8xx_pci_config
};

const device_t ncr53c860_pci_device = {
    .name = "NCR 53c860",
    .internal_name = "ncr53c860",
    .flags = DEVICE_PCI,
    .local = CHIP_860,
    .init = ncr53c8xx_init,
    .close = ncr53c8xx_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ncr53c8xx_pci_config
};

const device_t ncr53c875_pci_device = {
    .name = "NCR 53c875",
    .internal_name = "ncr53c875",
    .flags = DEVICE_PCI,
    .local = CHIP_875,
    .init = ncr53c8xx_init,
    .close = ncr53c8xx_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ncr53c8xx_pci_config
};
