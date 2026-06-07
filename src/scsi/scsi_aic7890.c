/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Adaptec AIC-7890 Ultra2 SCSI HBA emulation.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/dma.h>
#include <86box/pci.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_aic7890.h>
#include <86box/nmc93cxx.h>
#include <86box/plat_unused.h>

#define AIC7890_LOCAL_ONBOARD 0x00000001

#define AIC7890_PCI_IO_SIZE   0x100
#define AIC7890_PCI_MMIO_SIZE 0x1000

#define AIC7890_REG_WINDOW    0x100
#define AIC7890_SEQRAM_SIZE   (768 * 4)
#define AIC7890_SCB_COUNT     256
#define AIC7890_SCB_SIZE      64

#define AIC7890_HOST_ID       7

/*
 * Linux aic7xxx ID_AIC7890 is 0x001F9005000F9005:
 * device 001f, vendor 9005, subdevice 000f, subvendor 9005.
 */
#define AIC7890_PCI_DEVICE_ID 0x001f
#define AIC7890_PCI_SUBSYS_ID 0x000f

#define REG_SCSISEQ           0x00
#define REG_SXFRCTL0          0x01
#define REG_SXFRCTL1          0x02
#define REG_SCSISIGI          0x03
#define REG_SCSIRATE          0x04
#define REG_SCSIID            0x05
#define REG_CLRSINT0_SSTAT0   0x0b
#define REG_CLRSINT1_SSTAT1   0x0c
#define REG_SSTAT2            0x0d
#define REG_SSTAT3            0x0e
#define REG_SCSIID_ULTRA2     0x0f
#define REG_SIMODE0           0x10
#define REG_SIMODE1           0x11
#define REG_SEECTL            0x1e
#define REG_SBLKCTL           0x1f
#define REG_TARG_SCSIRATE     0x20
#define REG_ULTRA_ENB         0x30
#define REG_DISC_DSB          0x32
#define REG_SEQ_FLAGS         0x3c
#define REG_WAITING_SCBH      0x40
#define REG_HSCB_ADDR         0x44
#define REG_SHARED_DATA_ADDR  0x48
#define REG_KERNEL_QINPOS     0x4c
#define REG_QINPOS            0x4d
#define REG_QOUTPOS           0x4e
#define REG_SCSISEQ_TEMPLATE  0x54
#define REG_SEQCTL            0x60
#define REG_SEQRAM            0x61
#define REG_SEQADDR0          0x62
#define REG_SEQADDR1          0x63
#define REG_DSCOMMAND0        0x84
#define REG_DSPCISTATUS       0x86
#define REG_HCNTRL            0x87
#define REG_SCBPTR            0x90
#define REG_INTSTAT           0x91
#define REG_ERROR_CLRINT      0x92
#define REG_DFCNTRL           0x93
#define REG_DFSTATUS          0x94
#define REG_SCBCNT            0x9a
#define REG_QINFIFO           0x9b
#define REG_QINCNT            0x9c
#define REG_QOUTFIFO          0x9d
#define REG_QOUTCNT           0x9e
#define REG_SFUNCT            0x9f
#define REG_SCB_BASE          0xa0
#define REG_CCSCBRAM          0xec
#define REG_CCSCBADDR         0xed
#define REG_CCSCBCTL          0xee
#define REG_CCSCBCNT          0xef
#define REG_CCSCBPTR          0xf1
#define REG_HNSCB_QOFF        0xf4
#define REG_SNSCB_QOFF        0xf6
#define REG_SDSCB_QOFF        0xf8
#define REG_QOFF_CTLSTA       0xfa
#define REG_DFF_THRSH         0xfb

#define SEECTL_EXTARBACK      0x80
#define SEECTL_EXTARBREQ      0x40
#define SEECTL_SEEMS          0x20
#define SEECTL_SEERDY         0x10
#define SEECTL_SEECS          0x08
#define SEECTL_SEECK          0x04
#define SEECTL_SEEDO          0x02
#define SEECTL_SEEDI          0x01

#define SBLKCTL_ENAB40        0x08
#define SBLKCTL_SELWIDE       0x02

#define HCNTRL_POWRDN         0x40
#define HCNTRL_SWINT          0x10
#define HCNTRL_PAUSE          0x04
#define HCNTRL_INTEN          0x02
#define HCNTRL_CHIPRST        0x01

#define INTSTAT_BRKADRINT     0x08
#define INTSTAT_SCSIINT       0x04
#define INTSTAT_CMDCMPLT      0x02
#define INTSTAT_SEQINT        0x01
#define INTSTAT_INT_PEND      0x0f

#define CLRINT_CLRPARERR      0x10
#define CLRINT_CLRBRKADRINT   0x08
#define CLRINT_CLRSCSIINT     0x04
#define CLRINT_CLRCMDINT      0x02
#define CLRINT_CLRSEQINT      0x01

#define SSTAT0_SELINGO        0x10
#define SSTAT0_DMADONE        0x01
#define SSTAT1_SELTO          0x80

#define CLRSINT0_CLRSELINGO   0x10
#define CLRSINT1_CLRSELTIMEO  0x80
#define CLRSINT1_CLRBUSFREE   0x08
#define CLRSINT1_CLRSCSIPERR  0x04

#define QOFF_CTLSTA_SCB_AVAIL 0x40
#define QOFF_CTLSTA_QSIZE_256 0x06

#define CCSCBCTL_CCSCBBEN     0x08
#define CCSCBCTL_CCSCBDONE    0x80
#define CCSCBCTL_CCSCBRESET   0x01

#define SCB_DATAPTR           12
#define SCB_DATACNT           16
#define SCB_SGPTR             20
#define SCB_CONTROL           24
#define SCB_SCSIID            25
#define SCB_LUN               26
#define SCB_TAG               27
#define SCB_CDB_LEN           28
#define SCB_NEXT              31
#define SCB_CDB32             32

#define SCB_LIST_NULL         0xff
#define SG_PTR_MASK           0xfffffff8U
#define SG_LIST_NULL          0x00000001U
#define SG_FULL_RESID         0x00000002U
#define SG_RESID_VALID        0x00000004U
#define AHC_DMA_LAST_SEG      0x80000000U
#define AHC_SG_LEN_MASK       0x00ffffffU

#define CFXFER                0x0007
#define CFSYNCH               0x0008
#define CFDISC                0x0010
#define CFWIDEB               0x0020
#define CFSYNCHISULTRA        0x0040
#define CFINCBIOS             0x0200
#define CFULTRAEN             0x0002
#define CFSPARITY             0x0010
#define CFRESETB              0x0040
#define CFAUTOTERM            0x0001
#define CFSEAUTOTERM          0x0400
#define CFSIGNATURE2          0x0300

#define AIC_MIN(a, b)         (((a) < (b)) ? (a) : (b))

typedef struct aic7890_t {
    uint8_t       pci_slot;
    uint8_t       irq_state;
    uint8_t       pci_cfg[256];

    uint8_t       scsi_bus;
    uint8_t       regs[AIC7890_REG_WINDOW];
    uint8_t       scb_ram[AIC7890_SCB_COUNT][AIC7890_SCB_SIZE];
    uint8_t       seqram[AIC7890_SEQRAM_SIZE];
    uint8_t       seqram_byte;

    uint8_t       hns_qoff;
    uint8_t       sns_qoff;
    uint8_t       qout_next;

    uint16_t      io_base;
    bool          io_enabled;
    mem_mapping_t mmio_mapping;

    uint16_t      eeprom_default[64];
    nmc93cxx_eeprom_t *eeprom;
} aic7890_t;

static uint32_t
aic7890_get_le32(const uint8_t *buf)
{
    return (uint32_t) buf[0] | ((uint32_t) buf[1] << 8)
         | ((uint32_t) buf[2] << 16) | ((uint32_t) buf[3] << 24);
}

static uint32_t
aic7890_pci_bar(const aic7890_t *dev, int bar)
{
    int base = PCI_REG_BAR0_BYTE0 + (bar * 4);

    return (uint32_t) dev->pci_cfg[base]
         | ((uint32_t) dev->pci_cfg[base + 1] << 8)
         | ((uint32_t) dev->pci_cfg[base + 2] << 16)
         | ((uint32_t) dev->pci_cfg[base + 3] << 24);
}

static uint32_t
aic7890_scratch_l(const aic7890_t *dev, uint8_t reg)
{
    return (uint32_t) dev->regs[reg]
         | ((uint32_t) dev->regs[reg + 1] << 8)
         | ((uint32_t) dev->regs[reg + 2] << 16)
         | ((uint32_t) dev->regs[reg + 3] << 24);
}

static void
aic7890_update_irq(aic7890_t *dev)
{
    if ((dev->regs[REG_HCNTRL] & HCNTRL_INTEN) && (dev->regs[REG_INTSTAT] & INTSTAT_INT_PEND)) {
        dev->pci_cfg[PCI_REG_STATUS_L] |= PCI_STATUS_L_INT;
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    } else {
        dev->pci_cfg[PCI_REG_STATUS_L] &= ~PCI_STATUS_L_INT;
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    }
}

static void
aic7890_reset_regs(aic7890_t *dev)
{
    memset(dev->regs, 0, sizeof(dev->regs));
    memset(dev->scb_ram, 0, sizeof(dev->scb_ram));

    dev->regs[REG_HCNTRL]       = HCNTRL_PAUSE;
    dev->regs[REG_SBLKCTL]      = SBLKCTL_ENAB40 | SBLKCTL_SELWIDE;
    dev->regs[REG_SCSIID]       = AIC7890_HOST_ID;
    dev->regs[REG_SCSIID_ULTRA2]= AIC7890_HOST_ID;
    dev->regs[REG_SXFRCTL1]     = 0x27;
    dev->regs[REG_DFSTATUS]     = 0x89;
    dev->regs[REG_QOFF_CTLSTA]  = QOFF_CTLSTA_SCB_AVAIL | QOFF_CTLSTA_QSIZE_256;
    dev->regs[REG_CCSCBCTL]     = CCSCBCTL_CCSCBDONE;
    dev->regs[REG_DFF_THRSH]    = 0x21;

    dev->hns_qoff  = 0;
    dev->sns_qoff  = 0;
    dev->qout_next = 0;

    aic7890_update_irq(dev);
}

static void
aic7890_create_eeprom(aic7890_t *dev)
{
    uint32_t checksum = 0;

    memset(dev->eeprom_default, 0, sizeof(dev->eeprom_default));

    for (int i = 0; i < 16; i++)
        dev->eeprom_default[i] = CFXFER | CFSYNCH | CFDISC | CFWIDEB | CFSYNCHISULTRA | CFINCBIOS;

    dev->eeprom_default[16] = 0x0000;
    dev->eeprom_default[17] = CFAUTOTERM | CFULTRAEN | CFSPARITY | CFRESETB | CFSEAUTOTERM;
    dev->eeprom_default[18] = AIC7890_HOST_ID;
    dev->eeprom_default[19] = 16;
    dev->eeprom_default[30] = CFSIGNATURE2;

    for (int i = 0; i < 31; i++)
        checksum += dev->eeprom_default[i];
    dev->eeprom_default[31] = checksum & 0xffff;
}

static void
aic7890_remove_waiting_scb(aic7890_t *dev, uint8_t tag)
{
    uint8_t prev = SCB_LIST_NULL;
    uint8_t next = dev->regs[REG_WAITING_SCBH];

    if (tag == SCB_LIST_NULL)
        return;

    for (int i = 0; i < AIC7890_SCB_COUNT && next != SCB_LIST_NULL; i++) {
        uint8_t cur = next;

        next = dev->scb_ram[cur][SCB_NEXT];
        if (next == cur)
            next = SCB_LIST_NULL;

        if (cur == tag) {
            if (prev == SCB_LIST_NULL)
                dev->regs[REG_WAITING_SCBH] = next;
            else
                dev->scb_ram[prev][SCB_NEXT] = next;
            dev->scb_ram[cur][SCB_NEXT] = SCB_LIST_NULL;
            return;
        }

        prev = cur;
    }
}

static void
aic7890_add_waiting_scb(aic7890_t *dev, uint8_t tag)
{
    if (tag == SCB_LIST_NULL)
        return;

    aic7890_remove_waiting_scb(dev, tag);
    dev->scb_ram[tag][SCB_NEXT] = dev->regs[REG_WAITING_SCBH];
    dev->regs[REG_WAITING_SCBH] = tag;
    dev->regs[REG_SCBPTR]       = tag;
}

static void
aic7890_complete_scb(aic7890_t *dev, uint8_t tag, uint8_t *hscb, uint8_t scsi_status)
{
    uint32_t shared_addr = aic7890_scratch_l(dev, REG_SHARED_DATA_ADDR);
    uint32_t hscb_addr   = aic7890_scratch_l(dev, REG_HSCB_ADDR);

    hscb[0] = 0;
    hscb[1] = 0;
    hscb[2] = 0;
    hscb[3] = 0;
    hscb[4] = SG_LIST_NULL;
    hscb[5] = 0;
    hscb[6] = 0;
    hscb[7] = 0;
    hscb[8] = scsi_status;

    if (hscb_addr != 0)
        dma_bm_write(hscb_addr + ((uint32_t) tag * AIC7890_SCB_SIZE), hscb, AIC7890_SCB_SIZE, 4);

    if (shared_addr != 0) {
        dma_bm_write(shared_addr + dev->qout_next, &tag, 1, 4);
        dev->qout_next++;
        dev->regs[REG_SDSCB_QOFF] = dev->qout_next;
    }

    dev->regs[REG_INTSTAT] |= INTSTAT_CMDCMPLT;
    aic7890_update_irq(dev);
}

static void
aic7890_selection_timeout(aic7890_t *dev, uint8_t tag, const uint8_t *hscb)
{
    (void) hscb;

    dev->regs[REG_SCBPTR]       = tag;
    dev->regs[REG_SEQ_FLAGS]   &= ~0x80;
    dev->regs[REG_CLRSINT1_SSTAT1] |= SSTAT1_SELTO;
    dev->regs[REG_INTSTAT]     |= INTSTAT_SCSIINT;
    aic7890_update_irq(dev);
}

static uint32_t
aic7890_hscb_host_length(const uint8_t *hscb)
{
    uint32_t total = 0;
    uint32_t datacnt = aic7890_get_le32(&hscb[SCB_DATACNT]);
    uint32_t sgptr = aic7890_get_le32(&hscb[SCB_SGPTR]);

    if (sgptr & SG_LIST_NULL)
        return 0;

    total = datacnt & AHC_SG_LEN_MASK;
    if (datacnt & AHC_DMA_LAST_SEG)
        return total;

    sgptr &= SG_PTR_MASK;
    for (int i = 0; i < 255 && sgptr != 0; i++) {
        uint8_t sg[8];
        uint32_t len;

        dma_bm_read(sgptr, sg, sizeof(sg), 4);
        len = aic7890_get_le32(&sg[4]);
        total += len & AHC_SG_LEN_MASK;
        if (len & AHC_DMA_LAST_SEG)
            break;
        sgptr += 8;
    }

    return total;
}

static void
aic7890_dma_sg(aic7890_t *dev, const uint8_t *hscb, uint8_t *buffer,
               uint32_t buffer_len, bool to_host)
{
    uint32_t moved = 0;
    uint32_t dataptr = aic7890_get_le32(&hscb[SCB_DATAPTR]);
    uint32_t datacnt = aic7890_get_le32(&hscb[SCB_DATACNT]);
    uint32_t sgptr = aic7890_get_le32(&hscb[SCB_SGPTR]);

    (void) dev;

    if ((sgptr & SG_LIST_NULL) || (buffer_len == 0))
        return;

    for (int i = 0; i < 256 && moved < buffer_len; i++) {
        uint32_t len = datacnt & AHC_SG_LEN_MASK;
        uint32_t count = AIC_MIN(len, buffer_len - moved);

        if (count != 0) {
            if (to_host)
                dma_bm_write(dataptr, &buffer[moved], count, 4);
            else
                dma_bm_read(dataptr, &buffer[moved], count, 4);
            moved += count;
        }

        if (datacnt & AHC_DMA_LAST_SEG)
            break;

        sgptr &= SG_PTR_MASK;
        if (sgptr == 0)
            break;

        uint8_t sg[8];
        dma_bm_read(sgptr, sg, sizeof(sg), 4);
        dataptr = aic7890_get_le32(&sg[0]);
        datacnt = aic7890_get_le32(&sg[4]);
        sgptr += 8;
    }
}

static void
aic7890_execute_scb(aic7890_t *dev, uint8_t tag)
{
    uint8_t hscb[AIC7890_SCB_SIZE];
    uint8_t cdb[32];
    uint32_t hscb_addr = aic7890_scratch_l(dev, REG_HSCB_ADDR);
    uint8_t target;
    uint8_t lun;
    uint8_t cdb_len;
    scsi_device_t *sd;

    if (hscb_addr == 0)
        return;

    dma_bm_read(hscb_addr + ((uint32_t) tag * AIC7890_SCB_SIZE), hscb, sizeof(hscb), 4);
    memcpy(dev->scb_ram[tag], hscb, AIC7890_SCB_SIZE);
    aic7890_add_waiting_scb(dev, tag);

    target  = hscb[SCB_SCSIID] >> 4;
    lun     = hscb[SCB_LUN] & 0x3f;
    cdb_len = hscb[SCB_CDB_LEN];

    if (target >= SCSI_ID_MAX || dev->scsi_bus >= SCSI_BUS_MAX
        || !scsi_device_present(&scsi_devices[dev->scsi_bus][target])) {
        aic7890_selection_timeout(dev, tag, hscb);
        return;
    }

    aic7890_remove_waiting_scb(dev, tag);
    dev->regs[REG_SCBPTR] = tag;

    memset(cdb, 0, sizeof(cdb));
    if (cdb_len == 0)
        cdb_len = 12;
    if (cdb_len <= 12)
        memcpy(cdb, hscb, cdb_len);
    else
        memcpy(cdb, &hscb[SCB_CDB32], AIC_MIN(cdb_len, sizeof(cdb)));

    sd = &scsi_devices[dev->scsi_bus][target];
    sd->buffer_length = -1;
    scsi_device_identify(sd, lun);
    scsi_device_command_phase0(sd, cdb);

    if ((sd->phase != SCSI_PHASE_STATUS) && (sd->buffer_length > 0)) {
        uint32_t host_len = aic7890_hscb_host_length(hscb);
        uint32_t count = AIC_MIN((uint32_t) sd->buffer_length, host_len);

        if (count != 0) {
            if (sd->phase == SCSI_PHASE_DATA_IN)
                aic7890_dma_sg(dev, hscb, sd->sc->temp_buffer, count, true);
            else if (sd->phase == SCSI_PHASE_DATA_OUT)
                aic7890_dma_sg(dev, hscb, sd->sc->temp_buffer, count, false);
        }
        scsi_device_command_phase1(sd);
    }

    scsi_device_identify(sd, SCSI_LUN_USE_CDB);
    aic7890_complete_scb(dev, tag, hscb, sd->status);
}

static void
aic7890_process_queue(aic7890_t *dev)
{
    uint32_t shared_addr = aic7890_scratch_l(dev, REG_SHARED_DATA_ADDR);

    if (shared_addr == 0)
        return;

    while (dev->sns_qoff != dev->hns_qoff) {
        uint8_t tag = SCB_LIST_NULL;

        if (dev->regs[REG_INTSTAT] & INTSTAT_SCSIINT)
            break;

        dma_bm_read(shared_addr + 256 + dev->sns_qoff, &tag, 1, 4);
        dev->sns_qoff++;
        dev->regs[REG_SNSCB_QOFF] = dev->sns_qoff;

        if (tag == SCB_LIST_NULL)
            continue;

        aic7890_execute_scb(dev, tag);
    }
}

static uint8_t
aic7890_seqram_read(aic7890_t *dev)
{
    uint16_t seqaddr = (uint16_t) dev->regs[REG_SEQADDR0]
                     | (((uint16_t) dev->regs[REG_SEQADDR1] & 0x01) << 8);
    uint32_t pos = ((uint32_t) seqaddr * 4) + dev->seqram_byte;
    uint8_t ret = (pos < AIC7890_SEQRAM_SIZE) ? dev->seqram[pos] : 0xff;

    dev->seqram_byte = (dev->seqram_byte + 1) & 3;
    if (dev->seqram_byte == 0) {
        seqaddr++;
        dev->regs[REG_SEQADDR0] = seqaddr & 0xff;
        dev->regs[REG_SEQADDR1] = (seqaddr >> 8) & 0x01;
    }

    return ret;
}

static void
aic7890_seqram_write(aic7890_t *dev, uint8_t val)
{
    uint16_t seqaddr = (uint16_t) dev->regs[REG_SEQADDR0]
                     | (((uint16_t) dev->regs[REG_SEQADDR1] & 0x01) << 8);
    uint32_t pos = ((uint32_t) seqaddr * 4) + dev->seqram_byte;

    if (pos < AIC7890_SEQRAM_SIZE)
        dev->seqram[pos] = val;

    dev->seqram_byte = (dev->seqram_byte + 1) & 3;
    if (dev->seqram_byte == 0) {
        seqaddr++;
        dev->regs[REG_SEQADDR0] = seqaddr & 0xff;
        dev->regs[REG_SEQADDR1] = (seqaddr >> 8) & 0x01;
    }
}

static uint8_t
aic7890_reg_read(uint32_t addr, void *priv)
{
    aic7890_t *dev = priv;
    uint8_t reg = addr & 0xff;

    if (reg >= REG_SCB_BASE && reg < (REG_SCB_BASE + AIC7890_SCB_SIZE))
        return dev->scb_ram[dev->regs[REG_SCBPTR]][reg - REG_SCB_BASE];

    switch (reg) {
        case REG_SEECTL: {
            uint8_t ret = dev->regs[REG_SEECTL] | SEECTL_SEERDY;
            if (dev->regs[REG_SEECTL] & SEECTL_EXTARBREQ)
                ret |= SEECTL_EXTARBACK;
            if (nmc93cxx_eeprom_read(dev->eeprom))
                ret |= SEECTL_SEEDO;
            else
                ret &= ~SEECTL_SEEDO;
            return ret;
        }

        case REG_SEQRAM:
            return aic7890_seqram_read(dev);

        case REG_QINCNT:
            return dev->hns_qoff - dev->sns_qoff;

        case REG_QOUTCNT:
            return 0;

        case REG_ERROR_CLRINT:
            return 0;

        case REG_QOFF_CTLSTA:
            return dev->regs[reg] | QOFF_CTLSTA_SCB_AVAIL;

        default:
            return dev->regs[reg];
    }
}

static void
aic7890_reg_write(uint32_t addr, uint8_t val, void *priv)
{
    aic7890_t *dev = priv;
    uint8_t reg = addr & 0xff;

    if (reg >= REG_SCB_BASE && reg < (REG_SCB_BASE + AIC7890_SCB_SIZE)) {
        dev->scb_ram[dev->regs[REG_SCBPTR]][reg - REG_SCB_BASE] = val;
        return;
    }

    switch (reg) {
        case REG_CLRSINT0_SSTAT0:
            dev->regs[reg] &= ~(val & (CLRSINT0_CLRSELINGO | SSTAT0_DMADONE));
            return;

        case REG_CLRSINT1_SSTAT1:
            if (val & CLRSINT1_CLRSELTIMEO)
                dev->regs[reg] &= ~SSTAT1_SELTO;
            if (val & CLRSINT1_CLRBUSFREE)
                dev->regs[reg] &= ~0x08;
            if (val & CLRSINT1_CLRSCSIPERR)
                dev->regs[reg] &= ~0x04;
            return;

        case REG_SEECTL:
            dev->regs[reg] = val & ~(SEECTL_SEERDY | SEECTL_SEEDO | SEECTL_EXTARBACK);
            nmc93cxx_eeprom_write(dev->eeprom, !!(val & SEECTL_SEECS),
                                  !!(val & SEECTL_SEECK), !!(val & SEECTL_SEEDI));
            return;

        case REG_SBLKCTL:
            dev->regs[reg] = val | SBLKCTL_SELWIDE;
            return;

        case REG_SEQADDR0:
        case REG_SEQADDR1:
            dev->regs[reg] = (reg == REG_SEQADDR1) ? (val & 0x01) : val;
            dev->seqram_byte = 0;
            return;

        case REG_SEQRAM:
            aic7890_seqram_write(dev, val);
            return;

        case REG_HCNTRL:
            if (val & HCNTRL_CHIPRST) {
                aic7890_reset_regs(dev);
                dev->regs[REG_HCNTRL] = (val & (HCNTRL_PAUSE | HCNTRL_INTEN)) | HCNTRL_CHIPRST;
            } else {
                dev->regs[reg] = val & (HCNTRL_POWRDN | HCNTRL_SWINT | HCNTRL_PAUSE | HCNTRL_INTEN);
            }
            aic7890_update_irq(dev);
            return;

        case REG_ERROR_CLRINT:
            if (val & CLRINT_CLRCMDINT)
                dev->regs[REG_INTSTAT] &= ~INTSTAT_CMDCMPLT;
            if (val & CLRINT_CLRSCSIINT)
                dev->regs[REG_INTSTAT] &= ~INTSTAT_SCSIINT;
            if (val & CLRINT_CLRSEQINT)
                dev->regs[REG_INTSTAT] &= ~INTSTAT_SEQINT;
            if (val & CLRINT_CLRBRKADRINT)
                dev->regs[REG_INTSTAT] &= ~INTSTAT_BRKADRINT;
            aic7890_update_irq(dev);
            aic7890_process_queue(dev);
            return;

        case REG_DFCNTRL:
            dev->regs[reg] = val & ~(0x03);
            return;

        case REG_CCSCBCTL:
            if (val & CCSCBCTL_CCSCBRESET)
                dev->regs[reg] = CCSCBCTL_CCSCBDONE;
            else if (val & CCSCBCTL_CCSCBBEN)
                dev->regs[reg] = val | CCSCBCTL_CCSCBDONE;
            else
                dev->regs[reg] = val | CCSCBCTL_CCSCBDONE;
            return;

        case REG_HNSCB_QOFF:
            dev->hns_qoff = val;
            dev->regs[reg] = val;
            aic7890_process_queue(dev);
            return;

        case REG_SNSCB_QOFF:
            dev->sns_qoff = val;
            dev->regs[reg] = val;
            return;

        case REG_QOFF_CTLSTA:
            dev->regs[reg] = val | QOFF_CTLSTA_SCB_AVAIL;
            return;

        default:
            dev->regs[reg] = val;
            return;
    }
}

static uint16_t
aic7890_reg_readw(uint32_t addr, void *priv)
{
    uint16_t ret;

    ret = aic7890_reg_read(addr, priv);
    ret |= (uint16_t) aic7890_reg_read(addr + 1, priv) << 8;
    return ret;
}

static uint32_t
aic7890_reg_readl(uint32_t addr, void *priv)
{
    uint32_t ret;

    ret = aic7890_reg_read(addr, priv);
    ret |= (uint32_t) aic7890_reg_read(addr + 1, priv) << 8;
    ret |= (uint32_t) aic7890_reg_read(addr + 2, priv) << 16;
    ret |= (uint32_t) aic7890_reg_read(addr + 3, priv) << 24;
    return ret;
}

static void
aic7890_reg_writew(uint32_t addr, uint16_t val, void *priv)
{
    aic7890_reg_write(addr, val & 0xff, priv);
    aic7890_reg_write(addr + 1, (val >> 8) & 0xff, priv);
}

static void
aic7890_reg_writel(uint32_t addr, uint32_t val, void *priv)
{
    aic7890_reg_write(addr, val & 0xff, priv);
    aic7890_reg_write(addr + 1, (val >> 8) & 0xff, priv);
    aic7890_reg_write(addr + 2, (val >> 16) & 0xff, priv);
    aic7890_reg_write(addr + 3, (val >> 24) & 0xff, priv);
}

static uint8_t
aic7890_io_readb(uint16_t port, void *priv)
{
    aic7890_t *dev = priv;
    return aic7890_reg_read(port - dev->io_base, priv);
}

static uint16_t
aic7890_io_readw(uint16_t port, void *priv)
{
    aic7890_t *dev = priv;
    return aic7890_reg_readw(port - dev->io_base, priv);
}

static uint32_t
aic7890_io_readl(uint16_t port, void *priv)
{
    aic7890_t *dev = priv;
    return aic7890_reg_readl(port - dev->io_base, priv);
}

static void
aic7890_io_writeb(uint16_t port, uint8_t val, void *priv)
{
    aic7890_t *dev = priv;
    aic7890_reg_write(port - dev->io_base, val, priv);
}

static void
aic7890_io_writew(uint16_t port, uint16_t val, void *priv)
{
    aic7890_t *dev = priv;
    aic7890_reg_writew(port - dev->io_base, val, priv);
}

static void
aic7890_io_writel(uint16_t port, uint32_t val, void *priv)
{
    aic7890_t *dev = priv;
    aic7890_reg_writel(port - dev->io_base, val, priv);
}

static uint8_t
aic7890_mmio_readb(uint32_t addr, void *priv)
{
    return aic7890_reg_read(addr, priv);
}

static uint16_t
aic7890_mmio_readw(uint32_t addr, void *priv)
{
    return aic7890_reg_readw(addr, priv);
}

static uint32_t
aic7890_mmio_readl(uint32_t addr, void *priv)
{
    return aic7890_reg_readl(addr, priv);
}

static void
aic7890_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    aic7890_reg_write(addr, val, priv);
}

static void
aic7890_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    aic7890_reg_writew(addr, val, priv);
}

static void
aic7890_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    aic7890_reg_writel(addr, val, priv);
}

static void
aic7890_io_disable(aic7890_t *dev)
{
    if (!dev->io_enabled)
        return;

    io_removehandler(dev->io_base, AIC7890_PCI_IO_SIZE,
                     aic7890_io_readb, aic7890_io_readw, aic7890_io_readl,
                     aic7890_io_writeb, aic7890_io_writew, aic7890_io_writel, dev);
    dev->io_enabled = false;
}

static void
aic7890_io_enable(aic7890_t *dev)
{
    uint32_t base = aic7890_pci_bar(dev, 0) & ~0x03U;

    if (base == 0)
        return;

    dev->io_base = base & 0xffff;
    io_sethandler(dev->io_base, AIC7890_PCI_IO_SIZE,
                  aic7890_io_readb, aic7890_io_readw, aic7890_io_readl,
                  aic7890_io_writeb, aic7890_io_writew, aic7890_io_writel, dev);
    dev->io_enabled = true;
}

static void
aic7890_mmio_remap(aic7890_t *dev)
{
    uint32_t base = aic7890_pci_bar(dev, 1) & ~0x0fU;

    if ((dev->pci_cfg[PCI_REG_COMMAND_L] & PCI_COMMAND_MEM) && base != 0)
        mem_mapping_set_addr(&dev->mmio_mapping, base, AIC7890_PCI_MMIO_SIZE);
    else
        mem_mapping_disable(&dev->mmio_mapping);
}

static uint8_t
aic7890_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    aic7890_t *dev = priv;

    return dev->pci_cfg[addr & 0xff];
}

static void
aic7890_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    aic7890_t *dev = priv;
    uint8_t reg = addr & 0xff;
    uint8_t mask = 0x00;

    switch (reg) {
        case PCI_REG_COMMAND_L:
            mask = PCI_COMMAND_L_IO | PCI_COMMAND_L_MEM | PCI_COMMAND_L_BM
                 | PCI_COMMAND_L_PARITY;
            break;
        case PCI_REG_COMMAND_H:
            mask = PCI_COMMAND_H_SERR | PCI_COMMAND_H_INT_DIS;
            break;
        case PCI_REG_STATUS_L:
            mask = PCI_STATUS_L_INT;
            break;
        case PCI_REG_STATUS_H:
            dev->pci_cfg[reg] &= ~(val & 0xf8);
            dev->pci_cfg[reg] |= PCI_DEVSEL_MEDIUM;
            return;
        case PCI_REG_CACHELINE_SIZE:
            mask = 0xff;
            break;
        case PCI_REG_LATENCY_TIMER:
            mask = 0xff;
            break;
        case PCI_REG_BAR0_BYTE0:
            mask = 0x00;
            break;
        case PCI_REG_BAR0_BYTE1:
        case PCI_REG_BAR0_BYTE2:
        case PCI_REG_BAR0_BYTE3:
            mask = 0xff;
            break;
        case PCI_REG_BAR1_BYTE0:
            mask = 0x00;
            break;
        case PCI_REG_BAR1_BYTE1:
            mask = 0xf0;
            break;
        case PCI_REG_BAR1_BYTE2:
        case PCI_REG_BAR1_BYTE3:
            mask = 0xff;
            break;
        case PCI_REG_INT_LINE:
            mask = 0xff;
            break;
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
            mask = 0xff;
            break;
        default:
            mask = 0x00;
            break;
    }

    if (reg == PCI_REG_COMMAND_L || (reg >= PCI_REG_BAR0_BYTE0 && reg <= PCI_REG_BAR0_BYTE3))
        aic7890_io_disable(dev);
    if (reg == PCI_REG_COMMAND_L || (reg >= PCI_REG_BAR1_BYTE0 && reg <= PCI_REG_BAR1_BYTE3))
        mem_mapping_disable(&dev->mmio_mapping);

    dev->pci_cfg[reg] = (dev->pci_cfg[reg] & ~mask) | (val & mask);

    if (reg == PCI_REG_COMMAND_L || (reg >= PCI_REG_BAR0_BYTE0 && reg <= PCI_REG_BAR0_BYTE3)) {
        dev->pci_cfg[PCI_REG_BAR0_BYTE0] = 0x01;
        if (dev->pci_cfg[PCI_REG_COMMAND_L] & PCI_COMMAND_IO)
            aic7890_io_enable(dev);
    }
    if (reg == PCI_REG_COMMAND_L || (reg >= PCI_REG_BAR1_BYTE0 && reg <= PCI_REG_BAR1_BYTE3)) {
        dev->pci_cfg[PCI_REG_BAR1_BYTE0] = 0x00;
        aic7890_mmio_remap(dev);
    }

    aic7890_update_irq(dev);
}

static void
aic7890_init_pci(aic7890_t *dev)
{
    memset(dev->pci_cfg, 0, sizeof(dev->pci_cfg));

    dev->pci_cfg[PCI_REG_VENDOR_ID_L]    = 0x05;
    dev->pci_cfg[PCI_REG_VENDOR_ID_H]    = 0x90;
    dev->pci_cfg[PCI_REG_DEVICE_ID_L]    = AIC7890_PCI_DEVICE_ID & 0xff;
    dev->pci_cfg[PCI_REG_DEVICE_ID_H]    = AIC7890_PCI_DEVICE_ID >> 8;
    dev->pci_cfg[PCI_REG_STATUS_H]       = PCI_DEVSEL_MEDIUM;
    dev->pci_cfg[PCI_REG_REVISION]       = 0x01;
    dev->pci_cfg[PCI_REG_PROG_IF]        = 0x00;
    dev->pci_cfg[PCI_REG_SUBCLASS]       = 0x00;
    dev->pci_cfg[PCI_REG_CLASS]          = 0x01;
    dev->pci_cfg[PCI_REG_LATENCY_TIMER]  = 0x20;
    dev->pci_cfg[PCI_REG_HEADER_TYPE]    = 0x00;
    dev->pci_cfg[PCI_REG_BAR0_BYTE0]     = 0x01;
    dev->pci_cfg[PCI_REG_BAR1_BYTE0]     = 0x00;
    dev->pci_cfg[PCI_REG_SUBVEN_ID_L]    = 0x05;
    dev->pci_cfg[PCI_REG_SUBVEN_ID_H]    = 0x90;
    dev->pci_cfg[PCI_REG_SUBSYS_ID_L]    = AIC7890_PCI_SUBSYS_ID & 0xff;
    dev->pci_cfg[PCI_REG_SUBSYS_ID_H]    = AIC7890_PCI_SUBSYS_ID >> 8;
    dev->pci_cfg[PCI_REG_INT_LINE]       = 0xff;
    dev->pci_cfg[PCI_REG_INT_PIN]        = PCI_INTA;
    dev->pci_cfg[PCI_REG_MIN_GRANT]      = 0x08;
    dev->pci_cfg[PCI_REG_MAX_LAT]        = 0x08;
}

static void *
aic7890_init(const device_t *info)
{
    aic7890_t *dev = calloc(1, sizeof(aic7890_t));
    nmc93cxx_eeprom_params_t eeprom_params;
    char eeprom_name[64];
    int inst = device_get_instance();

    dev->scsi_bus = scsi_get_bus();
    if (dev->scsi_bus < SCSI_BUS_MAX)
        scsi_bus_set_speed(dev->scsi_bus, 80000000.0);

    aic7890_create_eeprom(dev);
    snprintf(eeprom_name, sizeof(eeprom_name), "nmc93cxx_eeprom_%s_%d.nvr",
             info->internal_name, inst);
    eeprom_params.type = NMC_93C46_x16_64;
    eeprom_params.filename = eeprom_name;
    eeprom_params.default_content = dev->eeprom_default;
    dev->eeprom = device_add_inst_params(&nmc93cxx_device, inst, &eeprom_params);

    aic7890_init_pci(dev);
    aic7890_reset_regs(dev);

    mem_mapping_add(&dev->mmio_mapping, 0, 0,
                    aic7890_mmio_readb, aic7890_mmio_readw, aic7890_mmio_readl,
                    aic7890_mmio_writeb, aic7890_mmio_writew, aic7890_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->mmio_mapping);

    pci_add_card((info->local & AIC7890_LOCAL_ONBOARD) ? (PCI_ADD_SCSI | PCI_ADD_STRICT) : PCI_ADD_NORMAL,
                 aic7890_pci_read, aic7890_pci_write, dev, &dev->pci_slot);

    return dev;
}

static void
aic7890_close(void *priv)
{
    aic7890_t *dev = priv;

    aic7890_io_disable(dev);
    mem_mapping_disable(&dev->mmio_mapping);
    free(dev);
}

const device_t aic7890_pci_device = {
    .name          = "Adaptec AIC-7890 Ultra2 SCSI",
    .internal_name = "aic7890",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = aic7890_init,
    .close         = aic7890_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t aic7890_onboard_pci_device = {
    .name          = "Adaptec AIC-7890AB Ultra2 SCSI",
    .internal_name = "aic7890_onboard",
    .flags         = DEVICE_PCI | DEVICE_ONBOARD,
    .local         = AIC7890_LOCAL_ONBOARD,
    .init          = aic7890_init,
    .close         = aic7890_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
