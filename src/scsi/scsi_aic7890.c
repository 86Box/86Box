/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Adaptec AIC-7890 Ultra2 SCSI HBA emulation.
 */
#include <inttypes.h>
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

#define AIC7890_LOCAL_ONBOARD       0x00000001
#define AIC7890_LOCAL_LARGE_SEEPROM 0x00000002

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
#define REG_BRDCTL            0x1d
#define REG_SEECTL            0x1e
#define REG_SBLKCTL           0x1f
#define REG_TARG_SCSIRATE     0x20
#define REG_ULTRA_ENB         0x30
#define REG_DISC_DSB          0x32
#define REG_NEXT_QUEUED_SCB   0x39
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
#define SEQADDR1_MASK         0x03
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
#define REG_CCHADDR           0xe0
#define REG_CCHCNT            0xe8
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

/*
 * The Windows 2000 aic78u2 sequencer image uses a private scratch layout.
 * These offsets hold the DMA addresses it programs immediately after the
 * sequencer download, while the Linux-visible HSCB/SHARED_DATA scratch
 * locations remain zero.
 */
#define REG_WIN_HSCB_ADDR         0x20
#define REG_WIN_DONEQ_LAST        0x28
#define REG_WIN_SHARED_DATA_ADDR0 0x29
#define REG_WIN_SHARED_DATA_ADDR1 0x31

#define SCSISEQ_SCSIRSTO      0x01

#define SEQCTL_LOADRAM        0x01

#define SEECTL_EXTARBACK      0x80
#define SEECTL_EXTARBREQ      0x40
#define SEECTL_SEEMS          0x20
#define SEECTL_SEERDY         0x10
#define SEECTL_SEECS          0x08
#define SEECTL_SEECK          0x04
#define SEECTL_SEEDO          0x02
#define SEECTL_SEEDI          0x01

#define BRDCTL_BRDDAT7        0x80
#define BRDCTL_BRDDAT6        0x40
#define BRDCTL_BRDDAT5        0x20
#define BRDCTL_BRDDAT4        0x10
#define BRDCTL_BRDDAT3        0x08
#define BRDCTL_BRDDAT2        0x04
#define BRDCTL_BRDRW          0x02
#define BRDCTL_BRDSTB         0x01

#define SBLKCTL_DIAGLEDEN     0x80
#define SBLKCTL_DIAGLEDON     0x40
#define SBLKCTL_AUTOFLUSHDIS  0x20
#define SBLKCTL_ENAB40        0x08
#define SBLKCTL_ENAB20        0x04
#define SBLKCTL_SELWIDE       0x02
#define SBLKCTL_XCVR          0x01

#define HCNTRL_POWRDN         0x40
#define HCNTRL_SWINT          0x10
#define HCNTRL_HCNTRL3        0x08
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
#define SSTAT0_SELDO          0x40
#define SSTAT0_SELDI          0x20
#define SSTAT0_SPIORDY        0x02
#define SSTAT0_DMADONE        0x01
#define SSTAT1_SELTO          0x80
#define SSTAT1_ATNO           0x40
#define SSTAT1_SCSIRSTI       0x20
#define SSTAT1_BUSFREE        0x08
#define SSTAT1_SCSIPERR       0x04
#define SSTAT1_PHASECHG       0x02
#define SSTAT1_REQINIT        0x01

#define CLRSINT0_CLRSELDO     0x40
#define CLRSINT0_CLRSELDI     0x20
#define CLRSINT0_CLRSELINGO   0x10
#define CLRSINT0_CLRSPIORDY   0x02
#define CLRSINT1_CLRSELTIMEO  0x80
#define CLRSINT1_CLRATNO      0x40
#define CLRSINT1_CLRSCSIRSTI  0x20
#define CLRSINT1_CLRBUSFREE   0x08
#define CLRSINT1_CLRSCSIPERR  0x04
#define CLRSINT1_CLRPHASECHG  0x02
#define CLRSINT1_CLRREQINIT   0x01

#define SIMODE1_ENSCSIRST     0x20

#define QOFF_CTLSTA_SCB_AVAIL 0x40
#define QOFF_CTLSTA_SNSCB_ROLLOVER 0x20
#define QOFF_CTLSTA_SDSCB_ROLLOVER 0x10
#define QOFF_CTLSTA_QSIZE_256 0x06

#define SFUNCT_CFGSPACE       0x0e

#define DSCOMMAND0_CACHETHEN  0x80
#define DSCOMMAND0_DPARCKEN   0x40
#define DSCOMMAND0_MPARCKEN   0x20
#define DSCOMMAND0_EXTREQLCK  0x10
#define DSCOMMAND0_INTSCBRAMSEL 0x08
#define DSCOMMAND0_RAMPS      0x04
#define DSCOMMAND0_USCBSIZE32 0x02
#define DSCOMMAND0_CIOPARCKEN 0x01

#define CCSCBCTL_CCSCBDONE    0x80
#define CCSCBCTL_ARRDONE      0x40
#define CCSCBCTL_CCARREN      0x10
#define CCSCBCTL_CCSCBEN      0x08
#define CCSCBCTL_CCSCBDIR     0x04
#define CCSCBCTL_CCSCBRESET   0x01

#define SCBCNT_SCBAUTO        0x80
#define SCBCNT_MASK           0x3f

#define SCB_CDB_PTR           0
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

#define WIN_SCB_DATAPTR       0
#define WIN_SCB_DATACNT       4
#define WIN_SCB_HOST_TAG      11
#define WIN_SCB_TARGET        12
#define WIN_SCB_CDB_LEN       14
#define WIN_SCB_CDB           24

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
    uint8_t       ccscb_ram[AIC7890_SCB_SIZE];
    uint8_t       seqram[AIC7890_SEQRAM_SIZE];
    uint8_t       seqram_byte;

    uint8_t       hns_qoff;
    uint8_t       sns_qoff;
    uint8_t       qout_next;
    uint8_t       win_qout_valid;
    uint8_t       win_next_hscb_tag;
    uint8_t       win_last_inquiry_target;
    uint8_t       brdctl_data;
    uint8_t       qin_fifo[AIC7890_SCB_COUNT];
    uint8_t       qin_head;
    uint8_t       qin_tail;
    uint16_t      qin_count;
    uint8_t       qout_fifo[AIC7890_SCB_COUNT];
    uint8_t       qout_head;
    uint8_t       qout_tail;
    uint16_t      qout_count;
    uint32_t      seqram_writes;
    uint32_t      seqram_reads;
    uint32_t      seqram_write_hash;
    uint32_t      seqram_read_hash;
    uint32_t      seq_idle_pauses;
    uint8_t       trace_last_read[AIC7890_REG_WINDOW];
    uint32_t      trace_same_reads[AIC7890_REG_WINDOW];
    bool          trace_read_valid[AIC7890_REG_WINDOW];
    bool          trace_irq_valid;
    bool          trace_irq_active;

    uint16_t      io_base;
    bool          io_enabled;
    mem_mapping_t mmio_mapping;

    uint16_t      eeprom_default[256];
    nmc93cxx_eeprom_t *eeprom;
} aic7890_t;

static uint8_t aic7890_pci_read(int func, int addr, int len, void *priv);
static void    aic7890_pci_write(int func, int addr, int len, uint8_t val, void *priv);

#ifndef ENABLE_AIC7890_LOG
#    define ENABLE_AIC7890_LOG 0
#endif

#define AIC7890_TRACE_HASH_INIT 2166136261U

static int aic7890_do_log = -1;

static int
aic7890_log_level(void)
{
    if (aic7890_do_log < 0) {
        const char *env = getenv("AIC7890_LOG");

        aic7890_do_log = ENABLE_AIC7890_LOG;
        if (env != NULL && env[0] != '\0') {
            if (!strcmp(env, "verbose") || !strcmp(env, "trace"))
                aic7890_do_log = 2;
            else {
                aic7890_do_log = atoi(env);
                if (aic7890_do_log <= 0 && env[0] != '0')
                    aic7890_do_log = 1;
            }
        }
    }

    return aic7890_do_log;
}

static void
aic7890_log(int level, const char *fmt, ...)
{
    va_list ap;

    if (aic7890_log_level() < level)
        return;

    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}

static const char *
aic7890_reg_name(uint8_t reg)
{
    if (reg >= REG_SCB_BASE && reg < (REG_SCB_BASE + AIC7890_SCB_SIZE))
        return "SCB";
    if (reg >= REG_HSCB_ADDR && reg < (REG_HSCB_ADDR + 4))
        return "HSCB_ADDR";
    if (reg >= REG_SHARED_DATA_ADDR && reg < (REG_SHARED_DATA_ADDR + 4))
        return "SHARED_DATA_ADDR";
    if (reg >= REG_CCHADDR && reg < (REG_CCHADDR + 4))
        return "CCHADDR";

    switch (reg) {
        case REG_SCSISEQ:          return "SCSISEQ";
        case REG_SXFRCTL0:         return "SXFRCTL0";
        case REG_SXFRCTL1:         return "SXFRCTL1";
        case REG_SCSISIGI:         return "SCSISIGI";
        case REG_SCSIRATE:         return "SCSIRATE";
        case REG_SCSIID:           return "SCSIID";
        case REG_CLRSINT0_SSTAT0:  return "SSTAT0/CLRSINT0";
        case REG_CLRSINT1_SSTAT1:  return "SSTAT1/CLRSINT1";
        case REG_SSTAT2:           return "SSTAT2";
        case REG_SSTAT3:           return "SSTAT3";
        case REG_SCSIID_ULTRA2:    return "SCSIID_ULTRA2";
        case REG_SIMODE0:          return "SIMODE0";
        case REG_SIMODE1:          return "SIMODE1";
        case REG_BRDCTL:           return "BRDCTL";
        case REG_SEECTL:           return "SEECTL";
        case REG_SBLKCTL:          return "SBLKCTL";
        case REG_TARG_SCSIRATE:    return "TARG_SCSIRATE";
        case REG_ULTRA_ENB:        return "ULTRA_ENB";
        case REG_DISC_DSB:         return "DISC_DSB";
        case REG_NEXT_QUEUED_SCB:  return "NEXT_QUEUED_SCB";
        case REG_SEQ_FLAGS:        return "SEQ_FLAGS";
        case REG_WAITING_SCBH:     return "WAITING_SCBH";
        case REG_KERNEL_QINPOS:    return "KERNEL_QINPOS";
        case REG_QINPOS:           return "QINPOS";
        case REG_QOUTPOS:          return "QOUTPOS";
        case REG_SCSISEQ_TEMPLATE: return "SCSISEQ_TEMPLATE";
        case REG_SEQCTL:           return "SEQCTL";
        case REG_SEQRAM:           return "SEQRAM";
        case REG_SEQADDR0:         return "SEQADDR0";
        case REG_SEQADDR1:         return "SEQADDR1";
        case REG_DSCOMMAND0:       return "DSCOMMAND0";
        case REG_DSPCISTATUS:      return "DSPCISTATUS";
        case REG_HCNTRL:           return "HCNTRL";
        case REG_SCBPTR:           return "SCBPTR";
        case REG_INTSTAT:          return "INTSTAT";
        case REG_ERROR_CLRINT:     return "ERROR/CLRINT";
        case REG_DFCNTRL:          return "DFCNTRL";
        case REG_DFSTATUS:         return "DFSTATUS";
        case REG_SCBCNT:           return "SCBCNT";
        case REG_QINFIFO:          return "QINFIFO";
        case REG_QINCNT:           return "QINCNT";
        case REG_QOUTFIFO:         return "QOUTFIFO";
        case REG_QOUTCNT:          return "QOUTCNT";
        case REG_SFUNCT:           return "SFUNCT";
        case REG_CCHCNT:           return "CCHCNT";
        case REG_CCSCBRAM:         return "CCSCBRAM";
        case REG_CCSCBADDR:        return "CCSCBADDR";
        case REG_CCSCBCTL:         return "CCSCBCTL";
        case REG_CCSCBCNT:         return "CCSCBCNT";
        case REG_CCSCBPTR:         return "CCSCBPTR";
        case REG_HNSCB_QOFF:       return "HNSCB_QOFF";
        case REG_SNSCB_QOFF:       return "SNSCB_QOFF";
        case REG_SDSCB_QOFF:       return "SDSCB_QOFF";
        case REG_QOFF_CTLSTA:      return "QOFF_CTLSTA";
        case REG_DFF_THRSH:        return "DFF_THRSH";
        default:                   return "REG";
    }
}

static const char *
aic7890_pci_reg_name(uint8_t reg)
{
    if (reg >= PCI_REG_BAR0_BYTE0 && reg <= PCI_REG_BAR0_BYTE3)
        return "BAR0";
    if (reg >= PCI_REG_BAR1_BYTE0 && reg <= PCI_REG_BAR1_BYTE3)
        return "BAR1";
    if (reg >= 0x40 && reg <= 0x43)
        return "DEVCONFIG";

    switch (reg) {
        case PCI_REG_VENDOR_ID_L:
        case PCI_REG_VENDOR_ID_H:     return "VENDOR_ID";
        case PCI_REG_DEVICE_ID_L:
        case PCI_REG_DEVICE_ID_H:     return "DEVICE_ID";
        case PCI_REG_COMMAND_L:       return "COMMAND_L";
        case PCI_REG_COMMAND_H:       return "COMMAND_H";
        case PCI_REG_STATUS_L:        return "STATUS_L";
        case PCI_REG_STATUS_H:        return "STATUS_H";
        case PCI_REG_REVISION:        return "REVISION";
        case PCI_REG_PROG_IF:         return "PROG_IF";
        case PCI_REG_SUBCLASS:        return "SUBCLASS";
        case PCI_REG_CLASS:           return "CLASS";
        case PCI_REG_CACHELINE_SIZE:  return "CACHELINE_SIZE";
        case PCI_REG_LATENCY_TIMER:   return "LATENCY_TIMER";
        case PCI_REG_HEADER_TYPE:     return "HEADER_TYPE";
        case PCI_REG_SUBVEN_ID_L:
        case PCI_REG_SUBVEN_ID_H:     return "SUBVEN_ID";
        case PCI_REG_SUBSYS_ID_L:
        case PCI_REG_SUBSYS_ID_H:     return "SUBSYS_ID";
        case PCI_REG_INT_LINE:        return "INT_LINE";
        case PCI_REG_INT_PIN:         return "INT_PIN";
        case PCI_REG_MIN_GRANT:       return "MIN_GRANT";
        case PCI_REG_MAX_LAT:         return "MAX_LAT";
        default:                      return "PCI_REG";
    }
}

static bool
aic7890_interesting_read(uint8_t reg)
{
    switch (reg) {
        case REG_SCSISEQ:
        case REG_CLRSINT0_SSTAT0:
        case REG_CLRSINT1_SSTAT1:
        case REG_SSTAT2:
        case REG_SSTAT3:
        case REG_BRDCTL:
        case REG_SEECTL:
        case REG_SBLKCTL:
        case REG_SEQCTL:
        case REG_DSCOMMAND0:
        case REG_DSPCISTATUS:
        case REG_HCNTRL:
        case REG_INTSTAT:
        case REG_ERROR_CLRINT:
        case REG_DFSTATUS:
        case REG_SCBCNT:
        case REG_QINCNT:
        case REG_QOUTFIFO:
        case REG_QOUTCNT:
        case REG_SFUNCT:
        case REG_CCSCBADDR:
        case REG_CCSCBCTL:
        case REG_CCSCBCNT:
        case REG_CCSCBPTR:
        case REG_HNSCB_QOFF:
        case REG_SNSCB_QOFF:
        case REG_SDSCB_QOFF:
        case REG_QOFF_CTLSTA:
            return true;
        default:
            return false;
    }
}

static bool
aic7890_suppress_byte_trace(uint8_t reg)
{
    return reg == REG_SEQRAM || reg == REG_CCSCBRAM
        || (reg >= REG_SCB_BASE && reg < (REG_SCB_BASE + AIC7890_SCB_SIZE));
}

static void
aic7890_trace_reg_read(aic7890_t *dev, uint8_t reg, uint8_t val)
{
    int level = aic7890_log_level();

    if (level <= 0)
        return;
    if ((level < 2 && !aic7890_interesting_read(reg))
        || (level < 3 && aic7890_suppress_byte_trace(reg)))
        return;

    if (level < 2) {
        if (dev->trace_read_valid[reg] && dev->trace_last_read[reg] == val) {
            dev->trace_same_reads[reg]++;
            if (dev->trace_same_reads[reg] != 1024)
                return;
            aic7890_log(1, "AIC7890: read %02x %-16s still %02x after 1024 reads\n",
                        reg, aic7890_reg_name(reg), val);
            dev->trace_same_reads[reg] = 0;
            return;
        }
        dev->trace_read_valid[reg] = true;
        dev->trace_last_read[reg] = val;
        dev->trace_same_reads[reg] = 0;
    }

    aic7890_log((level >= 2) ? 2 : 1, "AIC7890: read %02x %-16s -> %02x\n",
                reg, aic7890_reg_name(reg), val);
}

static void
aic7890_trace_reg_write(aic7890_t *dev, uint8_t reg, uint8_t val)
{
    int level = aic7890_log_level();

    if (level <= 0)
        return;
    if (level < 3 && aic7890_suppress_byte_trace(reg))
        return;

    aic7890_log((level >= 2) ? 2 : 1,
                "AIC7890: write %02x %-16s %02x -> %02x\n",
                reg, aic7890_reg_name(reg), dev->regs[reg], val);
}

static void
aic7890_trace_pci_read(uint8_t reg, uint8_t val)
{
    if (aic7890_log_level() >= 2)
        aic7890_log(2, "AIC7890: pci read %02x %-14s -> %02x\n",
                    reg, aic7890_pci_reg_name(reg), val);
}

static void
aic7890_trace_pci_write(uint8_t reg, uint8_t old, uint8_t val, uint8_t mask)
{
    if (aic7890_log_level() <= 0)
        return;

    aic7890_log(1, "AIC7890: pci write %02x %-14s old=%02x val=%02x mask=%02x\n",
                reg, aic7890_pci_reg_name(reg), old, val, mask);
}

static uint32_t
aic7890_trace_hash(uint32_t hash, uint8_t val)
{
    return (hash ^ val) * 16777619U;
}

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

static bool
aic7890_plausible_windows_addr_pair(uint32_t hscb_addr, uint32_t shared_addr)
{
    if (hscb_addr == 0 || shared_addr == 0)
        return false;
    if (hscb_addr <= shared_addr)
        return false;
    if ((hscb_addr & 0xff) != 0 || (shared_addr & 0xff) != 0)
        return false;

    return (hscb_addr - shared_addr) <= 0x01000000;
}

static uint32_t
aic7890_windows_shared_data_addr(const aic7890_t *dev)
{
    uint32_t hscb_addr = aic7890_scratch_l(dev, REG_WIN_HSCB_ADDR);
    uint32_t shared_addr = aic7890_scratch_l(dev, REG_WIN_SHARED_DATA_ADDR0);

    if (aic7890_plausible_windows_addr_pair(hscb_addr, shared_addr))
        return shared_addr;

    shared_addr = aic7890_scratch_l(dev, REG_WIN_SHARED_DATA_ADDR1);
    if (aic7890_plausible_windows_addr_pair(hscb_addr, shared_addr))
        return shared_addr;

    return 0;
}

static uint32_t
aic7890_hscb_addr(const aic7890_t *dev)
{
    uint32_t hscb_addr = aic7890_scratch_l(dev, REG_HSCB_ADDR);

    if (hscb_addr != 0)
        return hscb_addr;

    hscb_addr = aic7890_scratch_l(dev, REG_WIN_HSCB_ADDR);
    if (aic7890_windows_shared_data_addr(dev) != 0)
        return hscb_addr;

    return 0;
}

static uint32_t
aic7890_shared_data_addr(const aic7890_t *dev)
{
    uint32_t shared_addr = aic7890_scratch_l(dev, REG_SHARED_DATA_ADDR);

    if (shared_addr != 0)
        return shared_addr;

    return aic7890_windows_shared_data_addr(dev);
}

static bool
aic7890_windows_scratch_active(const aic7890_t *dev)
{
    return aic7890_scratch_l(dev, REG_SHARED_DATA_ADDR) == 0
        && aic7890_windows_shared_data_addr(dev) != 0;
}

static void
aic7890_set_scratch_l(aic7890_t *dev, uint8_t reg, uint32_t val)
{
    dev->regs[reg]     = val & 0xff;
    dev->regs[reg + 1] = (val >> 8) & 0xff;
    dev->regs[reg + 2] = (val >> 16) & 0xff;
    dev->regs[reg + 3] = (val >> 24) & 0xff;
}

static uint8_t
aic7890_scb_page_mask(const aic7890_t *dev)
{
    return (dev->regs[REG_DSCOMMAND0] & DSCOMMAND0_USCBSIZE32) ? 0x1f : 0x3f;
}

static uint8_t
aic7890_scb_page_size(const aic7890_t *dev)
{
    return aic7890_scb_page_mask(dev) + 1;
}

static uint16_t
aic7890_seqaddr(const aic7890_t *dev)
{
    return (uint16_t) dev->regs[REG_SEQADDR0]
         | (((uint16_t) dev->regs[REG_SEQADDR1] & SEQADDR1_MASK) << 8);
}

static void
aic7890_update_irq(aic7890_t *dev)
{
    bool irq_active = (dev->regs[REG_HCNTRL] & HCNTRL_INTEN)
                   && !(dev->regs[REG_HCNTRL] & HCNTRL_POWRDN)
                   && ((dev->regs[REG_INTSTAT] & INTSTAT_INT_PEND)
                    || (dev->regs[REG_HCNTRL] & HCNTRL_SWINT));

    if (irq_active)
        dev->pci_cfg[PCI_REG_STATUS_L] |= PCI_STATUS_L_INT;
    else
        dev->pci_cfg[PCI_REG_STATUS_L] &= ~PCI_STATUS_L_INT;

    if (irq_active && !(dev->pci_cfg[PCI_REG_COMMAND_H] & PCI_COMMAND_H_INT_DIS)) {
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    } else {
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    }

    if (!dev->trace_irq_valid || dev->trace_irq_active != irq_active) {
        aic7890_log(1,
                    "AIC7890: irq %s hcntrl=%02x intstat=%02x pci_cmd_h=%02x pci_status_l=%02x\n",
                    irq_active ? "assert" : "clear", dev->regs[REG_HCNTRL],
                    dev->regs[REG_INTSTAT], dev->pci_cfg[PCI_REG_COMMAND_H],
                    dev->pci_cfg[PCI_REG_STATUS_L]);
        dev->trace_irq_valid = true;
        dev->trace_irq_active = irq_active;
    }
}

static void
aic7890_reset_regs(aic7890_t *dev)
{
    memset(dev->regs, 0, sizeof(dev->regs));
    memset(dev->scb_ram, 0, sizeof(dev->scb_ram));
    memset(dev->ccscb_ram, 0, sizeof(dev->ccscb_ram));
    memset(dev->qin_fifo, SCB_LIST_NULL, sizeof(dev->qin_fifo));
    memset(dev->qout_fifo, SCB_LIST_NULL, sizeof(dev->qout_fifo));

    dev->regs[REG_HCNTRL]       = HCNTRL_PAUSE;
    dev->regs[REG_SBLKCTL]      = SBLKCTL_ENAB40 | SBLKCTL_SELWIDE;
    dev->regs[REG_SCSIID]       = AIC7890_HOST_ID;
    dev->regs[REG_SCSIID_ULTRA2]= AIC7890_HOST_ID;
    dev->regs[REG_SXFRCTL1]     = 0x27;
    dev->regs[REG_DFSTATUS]     = 0x89;
    dev->regs[REG_DSCOMMAND0]   = DSCOMMAND0_INTSCBRAMSEL;
    dev->regs[REG_QOFF_CTLSTA]  = QOFF_CTLSTA_SCB_AVAIL | QOFF_CTLSTA_QSIZE_256;
    dev->regs[REG_CCSCBCTL]     = 0;
    dev->regs[REG_DFF_THRSH]    = 0x21;
    dev->regs[REG_WAITING_SCBH] = SCB_LIST_NULL;

    dev->hns_qoff  = 0;
    dev->sns_qoff  = 0;
    dev->qout_next = 0;
    dev->win_qout_valid = 0;
    dev->win_next_hscb_tag = 0;
    dev->win_last_inquiry_target = SCB_LIST_NULL;
    dev->brdctl_data = BRDCTL_BRDDAT7 | BRDCTL_BRDDAT4 | BRDCTL_BRDDAT3;
    dev->qin_head   = 0;
    dev->qin_tail   = 0;
    dev->qin_count  = 0;
    dev->qout_head  = 0;
    dev->qout_tail  = 0;
    dev->qout_count = 0;
    dev->seqram_writes = 0;
    dev->seqram_reads = 0;
    dev->seqram_write_hash = AIC7890_TRACE_HASH_INIT;
    dev->seqram_read_hash = AIC7890_TRACE_HASH_INIT;
    dev->seq_idle_pauses = 0;
    memset(dev->trace_read_valid, 0, sizeof(dev->trace_read_valid));
    memset(dev->trace_same_reads, 0, sizeof(dev->trace_same_reads));
    dev->trace_irq_valid = false;

    aic7890_log(1, "AIC7890: reset registers\n");
    aic7890_update_irq(dev);
}

static void
aic7890_create_eeprom_config(uint16_t *config)
{
    uint32_t checksum = 0;

    for (int i = 0; i < 16; i++)
        config[i] = CFXFER | CFSYNCH | CFDISC | CFWIDEB | CFSYNCHISULTRA | CFINCBIOS;

    config[16] = 0x0000;
    config[17] = CFAUTOTERM | CFULTRAEN | CFSPARITY | CFRESETB | CFSEAUTOTERM;
    config[18] = AIC7890_HOST_ID;
    config[19] = 16;
    config[30] = CFSIGNATURE2;

    for (int i = 0; i < 31; i++)
        checksum += config[i];
    config[31] = checksum & 0xffff;
}

static void
aic7890_create_eeprom(aic7890_t *dev, bool large)
{
    memset(dev->eeprom_default, 0, sizeof(dev->eeprom_default));

    aic7890_create_eeprom_config(&dev->eeprom_default[0]);

    if (large)
        aic7890_create_eeprom_config(&dev->eeprom_default[32]);
}

static uint8_t
aic7890_qoff_status(const aic7890_t *dev)
{
    uint8_t ret = dev->regs[REG_QOFF_CTLSTA] & ~QOFF_CTLSTA_SCB_AVAIL;

    if (dev->hns_qoff != dev->sns_qoff)
        ret |= QOFF_CTLSTA_SCB_AVAIL;
    if (dev->sns_qoff == 0)
        ret |= QOFF_CTLSTA_SNSCB_ROLLOVER;
    if (dev->qout_next == 0)
        ret |= QOFF_CTLSTA_SDSCB_ROLLOVER;

    return ret;
}

static void
aic7890_qout_push(aic7890_t *dev, uint8_t tag)
{
    if (dev->qout_count >= AIC7890_SCB_COUNT) {
        aic7890_log(1, "AIC7890: qout overflow tag=%02x\n", tag);
        return;
    }

    dev->qout_fifo[dev->qout_tail] = tag;
    dev->qout_tail++;
    dev->qout_count++;
    aic7890_log(1, "AIC7890: qout push tag=%02x count=%u\n",
                tag, dev->qout_count);
}

static uint8_t
aic7890_qout_pop(aic7890_t *dev)
{
    uint8_t tag;

    if (dev->qout_count == 0)
        return SCB_LIST_NULL;

    tag = dev->qout_fifo[dev->qout_head];
    dev->qout_fifo[dev->qout_head] = SCB_LIST_NULL;
    dev->qout_head++;
    dev->qout_count--;
    aic7890_log(1, "AIC7890: qout pop tag=%02x count=%u\n",
                tag, dev->qout_count);
    return tag;
}

static void
aic7890_qin_push(aic7890_t *dev, uint8_t tag)
{
    if (dev->qin_count >= AIC7890_SCB_COUNT) {
        aic7890_log(1, "AIC7890: qin overflow tag=%02x\n", tag);
        return;
    }

    dev->qin_fifo[dev->qin_tail] = tag;
    dev->qin_tail++;
    dev->qin_count++;
    aic7890_log(1, "AIC7890: qin push tag=%02x count=%u\n",
                tag, dev->qin_count);
}

static uint8_t
aic7890_qin_pop(aic7890_t *dev)
{
    uint8_t tag;

    if (dev->qin_count == 0)
        return SCB_LIST_NULL;

    tag = dev->qin_fifo[dev->qin_head];
    dev->qin_fifo[dev->qin_head] = SCB_LIST_NULL;
    dev->qin_head++;
    dev->qin_count--;
    aic7890_log(1, "AIC7890: qin pop tag=%02x count=%u\n",
                tag, dev->qin_count);
    return tag;
}

static void
aic7890_ccscb_array_to_sram(aic7890_t *dev)
{
    uint8_t addr = dev->regs[REG_CCSCBADDR];
    uint8_t count = dev->regs[REG_CCSCBCNT];
    uint8_t tag = dev->regs[REG_CCSCBPTR];
    uint8_t page_size = aic7890_scb_page_size(dev);
    uint8_t start_addr = addr;

    for (int i = 0; i < count && addr < page_size; i++, addr++)
        dev->ccscb_ram[addr] = dev->scb_ram[tag][addr];

    dev->regs[REG_CCSCBADDR] = addr;
    dev->regs[REG_CCSCBCNT]  = 0;
    aic7890_log(1, "AIC7890: ccscb array->sram tag=%02x addr=%02x count=%u moved=%u\n",
                tag, start_addr, count, addr - start_addr);
}

static void
aic7890_ccscb_sram_to_array(aic7890_t *dev)
{
    uint8_t addr = dev->regs[REG_CCSCBADDR];
    uint8_t count = dev->regs[REG_CCSCBCNT];
    uint8_t tag = dev->regs[REG_CCSCBPTR];
    uint8_t page_size = aic7890_scb_page_size(dev);
    uint8_t start_addr = addr;

    for (int i = 0; i < count && addr < page_size; i++, addr++)
        dev->scb_ram[tag][addr] = dev->ccscb_ram[addr];

    dev->regs[REG_CCSCBADDR] = addr;
    dev->regs[REG_CCSCBCNT]  = 0;
    aic7890_log(1, "AIC7890: ccscb sram->array tag=%02x addr=%02x count=%u moved=%u\n",
                tag, start_addr, count, addr - start_addr);
}

static void
aic7890_ccscb_host_to_sram(aic7890_t *dev)
{
    uint8_t addr = dev->regs[REG_CCSCBADDR];
    uint8_t count = dev->regs[REG_CCHCNT];
    uint32_t host_addr = aic7890_scratch_l(dev, REG_CCHADDR);
    uint8_t page_size = aic7890_scb_page_size(dev);
    uint8_t moved = 0;
    uint8_t start_addr = addr;

    for (int i = 0; i < count && addr < page_size; i++, addr++, moved++)
        dma_bm_read(host_addr + moved, &dev->ccscb_ram[addr], 1, 4);

    dev->regs[REG_CCSCBADDR] = addr;
    dev->regs[REG_CCHCNT]    = 0;
    aic7890_set_scratch_l(dev, REG_CCHADDR, host_addr + moved);
    aic7890_log(1, "AIC7890: ccscb host->sram host=%08x addr=%02x count=%u moved=%u\n",
                host_addr, start_addr, count, moved);
}

static void
aic7890_ccscb_sram_to_host(aic7890_t *dev)
{
    uint8_t addr = dev->regs[REG_CCSCBADDR];
    uint8_t count = dev->regs[REG_CCHCNT];
    uint32_t host_addr = aic7890_scratch_l(dev, REG_CCHADDR);
    uint8_t page_size = aic7890_scb_page_size(dev);
    uint8_t moved = 0;
    uint8_t start_addr = addr;

    for (int i = 0; i < count && addr < page_size; i++, addr++, moved++)
        dma_bm_write(host_addr + moved, &dev->ccscb_ram[addr], 1, 4);

    dev->regs[REG_CCSCBADDR] = addr;
    dev->regs[REG_CCHCNT]    = 0;
    aic7890_set_scratch_l(dev, REG_CCHADDR, host_addr + moved);
    aic7890_log(1, "AIC7890: ccscb sram->host host=%08x addr=%02x count=%u moved=%u\n",
                host_addr, start_addr, count, moved);
}

static void
aic7890_run_ccscb(aic7890_t *dev, uint8_t val)
{
    uint8_t status = val & (CCSCBCTL_CCARREN | CCSCBCTL_CCSCBEN | CCSCBCTL_CCSCBDIR);
    uint8_t start_addr = dev->regs[REG_CCSCBADDR];

    aic7890_log(1,
                "AIC7890: ccscb ctl=%02x ptr=%02x addr=%02x cchaddr=%08x cchcnt=%u ccnt=%u\n",
                val, dev->regs[REG_CCSCBPTR], dev->regs[REG_CCSCBADDR],
                aic7890_scratch_l(dev, REG_CCHADDR), dev->regs[REG_CCHCNT],
                dev->regs[REG_CCSCBCNT]);

    if (val & CCSCBCTL_CCSCBRESET) {
        start_addr = 0;
        dev->regs[REG_CCSCBADDR] = 0;
    }

    if (val & CCSCBCTL_CCSCBDIR) {
        if (val & CCSCBCTL_CCSCBEN)
            aic7890_ccscb_host_to_sram(dev);
        if ((val & (CCSCBCTL_CCSCBEN | CCSCBCTL_CCARREN)) == (CCSCBCTL_CCSCBEN | CCSCBCTL_CCARREN))
            dev->regs[REG_CCSCBADDR] = start_addr;
        if (val & CCSCBCTL_CCARREN)
            aic7890_ccscb_sram_to_array(dev);
    } else {
        if (val & CCSCBCTL_CCARREN)
            aic7890_ccscb_array_to_sram(dev);
        if ((val & (CCSCBCTL_CCSCBEN | CCSCBCTL_CCARREN)) == (CCSCBCTL_CCSCBEN | CCSCBCTL_CCARREN))
            dev->regs[REG_CCSCBADDR] = start_addr;
        if (val & CCSCBCTL_CCSCBEN)
            aic7890_ccscb_sram_to_host(dev);
    }

    if (val & CCSCBCTL_CCSCBEN)
        status |= CCSCBCTL_CCSCBDONE;
    if (val & CCSCBCTL_CCARREN)
        status |= CCSCBCTL_ARRDONE;

    if (!(val & (CCSCBCTL_CCSCBEN | CCSCBCTL_CCARREN))) {
        status = val & (CCSCBCTL_CCSCBDIR);
    }

    dev->regs[REG_CCSCBCTL] = status;
    aic7890_log(1, "AIC7890: ccscb done status=%02x addr=%02x cchaddr=%08x\n",
                status, dev->regs[REG_CCSCBADDR],
                aic7890_scratch_l(dev, REG_CCHADDR));
}

static void
aic7890_scsi_bus_reset(aic7890_t *dev, bool external)
{
    aic7890_log(1, "AIC7890: SCSI bus reset%s\n",
                external ? " detected" : "");
    memset(dev->qin_fifo, SCB_LIST_NULL, sizeof(dev->qin_fifo));
    memset(dev->qout_fifo, SCB_LIST_NULL, sizeof(dev->qout_fifo));
    dev->qin_head = dev->qin_tail = 0;
    dev->qin_count = 0;
    dev->qout_head = dev->qout_tail = 0;
    dev->qout_count = 0;
    dev->seq_idle_pauses = 0;
    dev->win_last_inquiry_target = SCB_LIST_NULL;
    dev->regs[REG_WAITING_SCBH] = SCB_LIST_NULL;
    dev->regs[REG_CLRSINT0_SSTAT0] = 0;

    if (external) {
        dev->regs[REG_CLRSINT1_SSTAT1] |= SSTAT1_SCSIRSTI;
        if (dev->regs[REG_SIMODE1] & SIMODE1_ENSCSIRST)
            dev->regs[REG_INTSTAT] |= INTSTAT_SCSIINT;
    }

    aic7890_update_irq(dev);
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

static bool
aic7890_complete_windows_scb(aic7890_t *dev, uint8_t tag, uint32_t shared_addr)
{
    uint8_t entry[8] = { 0 };
    uint8_t pos;
    uint8_t scratch28;
    uint8_t valid;

    if (!aic7890_windows_scratch_active(dev))
        return false;

    scratch28 = dev->regs[REG_WIN_DONEQ_LAST];
    pos       = dev->qout_next;
    valid     = dev->win_qout_valid;

    entry[0] = tag;
    entry[7] = valid;
    dma_bm_write(shared_addr + ((uint32_t) pos * sizeof(entry)), entry,
                 sizeof(entry), 4);

    dev->qout_next = pos + 1;
    if (dev->qout_next == 0)
        dev->win_qout_valid++;
    dev->regs[REG_SDSCB_QOFF] = dev->qout_next;
    dev->regs[REG_QOUTPOS]    = dev->qout_next;

    aic7890_log(1,
                "AIC7890: windows qout entry tag=%02x pos=%u scratch28=%u valid=%02x next=%u next_valid=%02x shared=%08x\n",
                tag, pos, scratch28, valid, dev->qout_next, dev->win_qout_valid,
                shared_addr);
    return true;
}

static void
aic7890_complete_scb(aic7890_t *dev, uint8_t tag, uint8_t *hscb, uint8_t scsi_status)
{
    uint32_t shared_addr = aic7890_shared_data_addr(dev);
    uint32_t hscb_addr   = aic7890_hscb_addr(dev);
    bool windows_hscb = aic7890_windows_scratch_active(dev);

    if (!windows_hscb) {
        hscb[0] = 0;
        hscb[1] = 0;
        hscb[2] = 0;
        hscb[3] = 0;
        hscb[4] = SG_LIST_NULL;
        hscb[5] = 0;
        hscb[6] = 0;
        hscb[7] = 0;
        hscb[8] = scsi_status;
    }
    memcpy(dev->scb_ram[tag], hscb, AIC7890_SCB_SIZE);

    if (hscb_addr != 0)
        dma_bm_write(hscb_addr + ((uint32_t) tag * AIC7890_SCB_SIZE), hscb, AIC7890_SCB_SIZE, 4);

    aic7890_qout_push(dev, tag);

    if (shared_addr != 0) {
        if (!aic7890_complete_windows_scb(dev, tag, shared_addr)) {
            dma_bm_write(shared_addr + dev->qout_next, &tag, 1, 4);
            dev->qout_next++;
            dev->regs[REG_SDSCB_QOFF] = dev->qout_next;
            dev->regs[REG_QOUTPOS]    = dev->qout_next;
        }
    }

    dev->regs[REG_INTSTAT] |= INTSTAT_CMDCMPLT;
    aic7890_log(1,
                "AIC7890: complete scb tag=%02x status=%02x hscb=%08x shared=%08x qout=%u intstat=%02x\n",
                tag, scsi_status, hscb_addr, shared_addr, dev->qout_next,
                dev->regs[REG_INTSTAT]);
    aic7890_update_irq(dev);
}

static void
aic7890_selection_timeout(aic7890_t *dev, uint8_t tag, const uint8_t *hscb,
                          bool windows_hscb)
{
    uint8_t target = windows_hscb ? (hscb[WIN_SCB_TARGET] & 0x0f)
                                  : (hscb[SCB_SCSIID] >> 4);

    dev->regs[REG_SCBPTR]       = tag;
    dev->regs[REG_SEQ_FLAGS]   &= ~0x80;
    dev->regs[REG_CLRSINT1_SSTAT1] |= SSTAT1_SELTO;
    dev->regs[REG_INTSTAT]     |= INTSTAT_SCSIINT;
    aic7890_log(1, "AIC7890: selection timeout tag=%02x target=%u intstat=%02x sstat1=%02x\n",
                tag, target, dev->regs[REG_INTSTAT],
                dev->regs[REG_CLRSINT1_SSTAT1]);
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

static bool
aic7890_windows_synthetic_no_device_inquiry(aic7890_t *dev, uint8_t tag,
                                            uint8_t *hscb, const uint8_t *cdb,
                                            const char *reason)
{
    uint8_t inquiry[36] = { 0 };
    uint32_t dataptr;
    uint32_t host_len;
    uint32_t alloc_len;
    uint32_t count;

    if (cdb[0] != GPCMD_INQUIRY)
        return false;

    dataptr   = aic7890_get_le32(&hscb[WIN_SCB_DATAPTR]);
    host_len  = aic7890_get_le32(&hscb[WIN_SCB_DATACNT]) & AHC_SG_LEN_MASK;
    alloc_len = ((uint32_t) cdb[3] << 8) | cdb[4];

    inquiry[0] = 0x7f; /* No physical device on this LUN. */
    inquiry[2] = 0x02;
    inquiry[3] = 0x02;
    inquiry[4] = sizeof(inquiry) - 5;

    count = AIC_MIN((uint32_t) sizeof(inquiry), host_len);
    count = AIC_MIN(count, alloc_len);
    if (dataptr != 0 && count != 0)
        dma_bm_write(dataptr, inquiry, count, 4);

    aic7890_log(1,
                "AIC7890: windows synthetic no-device inquiry tag=%02x reason=%s target=%u lun_bits=%u data=%08x/%08x alloc=%u count=%u\n",
                tag, reason, hscb[WIN_SCB_TARGET] & 0x0f, cdb[1] >> 5,
                dataptr, host_len, alloc_len, count);
    aic7890_complete_scb(dev, tag, hscb, SCSI_STATUS_OK);
    return true;
}

static bool
aic7890_windows_inquiry_lun_probe(aic7890_t *dev, uint8_t tag, uint8_t *hscb,
                                  const uint8_t *cdb)
{
    if (!(cdb[1] & 0xe0))
        return false;

    return aic7890_windows_synthetic_no_device_inquiry(dev, tag, hscb, cdb,
                                                       "unsupported-lun");
}

static void
aic7890_execute_scb(aic7890_t *dev, uint8_t tag, bool dma_from_host)
{
    uint8_t hscb[AIC7890_SCB_SIZE];
    uint8_t cdb[32];
    uint32_t hscb_addr = aic7890_hscb_addr(dev);
    bool windows_hscb = dma_from_host && aic7890_windows_scratch_active(dev);
    uint8_t target;
    uint8_t lun;
    uint8_t cdb_len;
    bool target_valid;
    scsi_device_t *sd;

    if (dma_from_host && hscb_addr != 0)
        dma_bm_read(hscb_addr + ((uint32_t) tag * AIC7890_SCB_SIZE), hscb, sizeof(hscb), 4);
    else
        memcpy(hscb, dev->scb_ram[tag], sizeof(hscb));
    memcpy(dev->scb_ram[tag], hscb, AIC7890_SCB_SIZE);
    aic7890_add_waiting_scb(dev, tag);

    if (windows_hscb) {
        target  = hscb[WIN_SCB_TARGET] & 0x0f;
        lun     = SCSI_LUN_USE_CDB;
        cdb_len = hscb[WIN_SCB_CDB_LEN];
    } else {
        target  = hscb[SCB_SCSIID] >> 4;
        lun     = hscb[SCB_LUN] & 0x3f;
        cdb_len = hscb[SCB_CDB_LEN];
    }

    memset(cdb, 0, sizeof(cdb));
    if (cdb_len == 0)
        cdb_len = 12;
    if (windows_hscb)
        memcpy(cdb, &hscb[WIN_SCB_CDB], AIC_MIN(cdb_len, sizeof(cdb)));
    else if (cdb_len <= 12)
        memcpy(cdb, hscb, cdb_len);
    else {
        uint32_t cdb_ptr = aic7890_get_le32(&hscb[SCB_CDB_PTR]);

        if (cdb_ptr != 0)
            dma_bm_read(cdb_ptr, cdb, AIC_MIN(cdb_len, sizeof(cdb)), 4);
        else
            memcpy(cdb, &hscb[SCB_CDB32], AIC_MIN(cdb_len, sizeof(cdb)));
    }

    aic7890_log(1,
                "AIC7890: execute scb tag=%02x source=%s layout=%s target=%u lun=%u cdb_len=%u cdb=%02x %02x %02x %02x %02x %02x data=%08x/%08x sg=%08x\n",
                tag, dma_from_host ? "host" : "sram",
                windows_hscb ? "windows" : "linux", target, lun, cdb_len,
                cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5],
                windows_hscb ? aic7890_get_le32(&hscb[WIN_SCB_DATAPTR])
                             : aic7890_get_le32(&hscb[SCB_DATAPTR]),
                windows_hscb ? aic7890_get_le32(&hscb[WIN_SCB_DATACNT])
                             : aic7890_get_le32(&hscb[SCB_DATACNT]),
                aic7890_get_le32(&hscb[SCB_SGPTR]));

    target_valid = target < SCSI_ID_MAX && dev->scsi_bus < SCSI_BUS_MAX;
    if (!target_valid || !scsi_device_present(&scsi_devices[dev->scsi_bus][target])) {
        if (target_valid && windows_hscb && cdb[0] == GPCMD_INQUIRY) {
            aic7890_remove_waiting_scb(dev, tag);
            aic7890_windows_synthetic_no_device_inquiry(dev, tag, hscb, cdb,
                                                        "absent-target");
            return;
        }
        aic7890_selection_timeout(dev, tag, hscb, windows_hscb);
        return;
    }

    aic7890_remove_waiting_scb(dev, tag);
    dev->regs[REG_SCBPTR] = tag;

    sd = &scsi_devices[dev->scsi_bus][target];
    sd->buffer_length = -1;
    scsi_device_identify(sd, lun);
    scsi_device_command_phase0(sd, cdb);

    if ((sd->phase != SCSI_PHASE_STATUS) && (sd->buffer_length > 0)) {
        uint32_t host_len = windows_hscb
                          ? (aic7890_get_le32(&hscb[WIN_SCB_DATACNT]) & AHC_SG_LEN_MASK)
                          : aic7890_hscb_host_length(hscb);
        uint32_t count = AIC_MIN((uint32_t) sd->buffer_length, host_len);

        if (count != 0) {
            if (windows_hscb) {
                uint32_t dataptr = aic7890_get_le32(&hscb[WIN_SCB_DATAPTR]);

                if (dataptr != 0) {
                    if (sd->phase == SCSI_PHASE_DATA_IN)
                        dma_bm_write(dataptr, sd->sc->temp_buffer, count, 4);
                    else if (sd->phase == SCSI_PHASE_DATA_OUT)
                        dma_bm_read(dataptr, sd->sc->temp_buffer, count, 4);
                }
            } else {
                if (sd->phase == SCSI_PHASE_DATA_IN)
                    aic7890_dma_sg(dev, hscb, sd->sc->temp_buffer, count, true);
                else if (sd->phase == SCSI_PHASE_DATA_OUT)
                    aic7890_dma_sg(dev, hscb, sd->sc->temp_buffer, count, false);
            }
        }
        scsi_device_command_phase1(sd);
    }

    scsi_device_identify(sd, SCSI_LUN_USE_CDB);
    if (windows_hscb && sd->status == SCSI_STATUS_CHECK_CONDITION
        && aic7890_windows_inquiry_lun_probe(dev, tag, hscb, cdb))
        return;

    aic7890_complete_scb(dev, tag, hscb, sd->status);
}

static void
aic7890_probe_null_queue_tag(aic7890_t *dev, uint32_t shared_addr, uint8_t queue_pos)
{
    uint32_t hscb_addr = aic7890_hscb_addr(dev);
    uint8_t q[8];

    for (int i = 0; i < 8; i++) {
        q[i] = SCB_LIST_NULL;
        dma_bm_read(shared_addr + ((uint32_t) i * 0x100) + queue_pos, &q[i], 1, 4);
    }

    aic7890_log(1,
                "AIC7890: null queue probe pos=%u next=%02x shared=%08x hscb=%08x q000=%02x q100=%02x q200=%02x q300=%02x q400=%02x q500=%02x q600=%02x q700=%02x\n",
                queue_pos, dev->regs[REG_NEXT_QUEUED_SCB], shared_addr, hscb_addr,
                q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]);

    if (hscb_addr == 0)
        return;

    for (uint8_t tag = 0; tag < 32; tag++) {
        uint8_t hscb[AIC7890_SCB_SIZE];
        bool interesting;

        dma_bm_read(hscb_addr + ((uint32_t) tag * AIC7890_SCB_SIZE),
                    hscb, sizeof(hscb), 4);

        interesting = tag < 8
                   || hscb[0] == tag
                   || hscb[SCB_TAG] == tag
                   || hscb[SCB_SCSIID] != 0
                   || hscb[SCB_LUN] != 0
                   || hscb[SCB_CDB_LEN] != 0
                   || aic7890_get_le32(&hscb[SCB_DATAPTR]) != 0
                   || aic7890_get_le32(&hscb[SCB_DATACNT]) != 0
                   || aic7890_get_le32(&hscb[SCB_SGPTR]) != 0;
        if (!interesting)
            continue;

        aic7890_log(1,
                    "AIC7890: hscb probe slot=%02x b0=%02x tag27=%02x next31=%02x scsiid25=%02x lun26=%02x cdblen28=%02x cdb=%02x %02x %02x %02x %02x %02x data=%08x/%08x sg=%08x win_cdblen14=%02x win_cdb=%02x %02x %02x %02x %02x %02x win_data=%08x/%08x raw=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x raw16=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    tag, hscb[0], hscb[SCB_TAG], hscb[SCB_NEXT],
                    hscb[SCB_SCSIID], hscb[SCB_LUN], hscb[SCB_CDB_LEN],
                    hscb[0], hscb[1], hscb[2], hscb[3], hscb[4], hscb[5],
                    aic7890_get_le32(&hscb[SCB_DATAPTR]),
                    aic7890_get_le32(&hscb[SCB_DATACNT]),
                    aic7890_get_le32(&hscb[SCB_SGPTR]),
                    hscb[WIN_SCB_CDB_LEN],
                    hscb[WIN_SCB_CDB], hscb[WIN_SCB_CDB + 1],
                    hscb[WIN_SCB_CDB + 2], hscb[WIN_SCB_CDB + 3],
                    hscb[WIN_SCB_CDB + 4], hscb[WIN_SCB_CDB + 5],
                    aic7890_get_le32(&hscb[WIN_SCB_DATAPTR]),
                    aic7890_get_le32(&hscb[WIN_SCB_DATACNT]),
                    hscb[0], hscb[1], hscb[2], hscb[3],
                    hscb[4], hscb[5], hscb[6], hscb[7],
                    hscb[8], hscb[9], hscb[10], hscb[11],
                    hscb[12], hscb[13], hscb[14], hscb[15],
                    hscb[16], hscb[17], hscb[18], hscb[19],
                    hscb[20], hscb[21], hscb[22], hscb[23],
                    hscb[24], hscb[25], hscb[26], hscb[27],
                    hscb[28], hscb[29], hscb[30], hscb[31]);
    }
}

static bool
aic7890_windows_hscb_plausible(const uint8_t *hscb)
{
    uint8_t target  = hscb[WIN_SCB_TARGET] & 0x0f;
    uint8_t cdb_len = hscb[WIN_SCB_CDB_LEN];

    if (target >= SCSI_ID_MAX)
        return false;
    if (cdb_len == 0 || cdb_len > (AIC7890_SCB_SIZE - WIN_SCB_CDB))
        return false;

    return true;
}

static bool
aic7890_windows_hscb_stale_inquiry(aic7890_t *dev, uint8_t candidate,
                                    uint8_t queue_pos, uint8_t first_tag,
                                    const uint8_t *hscb)
{
    uint8_t target = hscb[WIN_SCB_TARGET] & 0x0f;

    if (candidate == first_tag)
        return false;
    if (hscb[WIN_SCB_CDB] != GPCMD_INQUIRY)
        return false;
    if (dev->win_last_inquiry_target == SCB_LIST_NULL)
        return false;
    if (target >= dev->win_last_inquiry_target)
        return false;

    aic7890_log(1,
                "AIC7890: windows queue fallback skip stale inquiry tag=%02x pos=%u cursor=%02x target=%u last_target=%u host_tag=%02x cdb=%02x %02x %02x %02x %02x %02x\n",
                candidate, queue_pos, first_tag, target,
                dev->win_last_inquiry_target, hscb[WIN_SCB_HOST_TAG],
                hscb[WIN_SCB_CDB], hscb[WIN_SCB_CDB + 1],
                hscb[WIN_SCB_CDB + 2], hscb[WIN_SCB_CDB + 3],
                hscb[WIN_SCB_CDB + 4], hscb[WIN_SCB_CDB + 5]);
    return true;
}

static bool
aic7890_windows_queue_tag(aic7890_t *dev, uint8_t queue_pos, uint8_t *tag)
{
    uint32_t hscb_addr;
    uint32_t dataptr;
    uint32_t datacnt;
    uint8_t hscb[AIC7890_SCB_SIZE];
    uint8_t first_tag;

    if (aic7890_scratch_l(dev, REG_HSCB_ADDR) != 0)
        return false;

    hscb_addr = aic7890_hscb_addr(dev);
    if (hscb_addr == 0)
        return false;

    first_tag = dev->win_next_hscb_tag;
    for (uint16_t offset = 0; offset < AIC7890_SCB_COUNT; offset++) {
        uint8_t candidate = first_tag + offset;

        dma_bm_read(hscb_addr + ((uint32_t) candidate * AIC7890_SCB_SIZE),
                    hscb, sizeof(hscb), 4);

        if (!aic7890_windows_hscb_plausible(hscb))
            continue;
        if (aic7890_windows_hscb_stale_inquiry(dev, candidate,
                                               queue_pos, first_tag, hscb)) {
            dev->win_next_hscb_tag = (uint8_t) (candidate + 1);
            continue;
        }

        *tag = candidate;
        dev->win_next_hscb_tag = (uint8_t) (candidate + 1);
        if (hscb[WIN_SCB_CDB] == GPCMD_INQUIRY)
            dev->win_last_inquiry_target = hscb[WIN_SCB_TARGET] & 0x0f;
        dataptr = aic7890_get_le32(&hscb[WIN_SCB_DATAPTR]);
        datacnt = aic7890_get_le32(&hscb[WIN_SCB_DATACNT]);
        aic7890_log(1,
                    "AIC7890: windows queue fallback tag=%02x pos=%u cursor=%02x next=%02x hscb=%08x host_tag=%02x target=%u lun_bits=%u tag27=%02x next31=%02x cdb_len=%u cdb=%02x %02x %02x %02x %02x %02x data=%08x/%08x\n",
                    *tag, queue_pos, first_tag, dev->win_next_hscb_tag,
                    hscb_addr, hscb[WIN_SCB_HOST_TAG],
                    hscb[WIN_SCB_TARGET] & 0x0f,
                    hscb[WIN_SCB_CDB + 1] >> 5, hscb[SCB_TAG],
                    hscb[SCB_NEXT], hscb[WIN_SCB_CDB_LEN],
                    hscb[WIN_SCB_CDB], hscb[WIN_SCB_CDB + 1],
                    hscb[WIN_SCB_CDB + 2], hscb[WIN_SCB_CDB + 3],
                    hscb[WIN_SCB_CDB + 4], hscb[WIN_SCB_CDB + 5],
                    dataptr, datacnt);
        return true;
    }

    aic7890_log(1,
                "AIC7890: windows queue fallback found no live hscb pos=%u cursor=%02x hscb=%08x\n",
                queue_pos, first_tag, hscb_addr);
    return false;
}

static void
aic7890_process_queue(aic7890_t *dev)
{
    uint32_t shared_addr = aic7890_shared_data_addr(dev);
    bool windows_shared = aic7890_windows_scratch_active(dev);

    if (dev->regs[REG_HCNTRL] & HCNTRL_PAUSE)
        return;
    if (shared_addr == 0) {
        if (dev->sns_qoff != dev->hns_qoff)
            aic7890_log(1,
                        "AIC7890: queue pending without shared addr sns=%u hns=%u win_hscb=%08x win_shared0=%08x win_shared1=%08x\n",
                        dev->sns_qoff, dev->hns_qoff,
                        aic7890_scratch_l(dev, REG_WIN_HSCB_ADDR),
                        aic7890_scratch_l(dev, REG_WIN_SHARED_DATA_ADDR0),
                        aic7890_scratch_l(dev, REG_WIN_SHARED_DATA_ADDR1));
        return;
    }

    while (dev->sns_qoff != dev->hns_qoff) {
        uint8_t tag = SCB_LIST_NULL;
        uint8_t queue_pos = dev->sns_qoff;

        if (dev->regs[REG_INTSTAT] & INTSTAT_SCSIINT)
            break;

        dma_bm_read(shared_addr + 256 + queue_pos, &tag, 1, 4);
        dev->sns_qoff++;
        dev->regs[REG_SNSCB_QOFF] = dev->sns_qoff;
        dev->regs[REG_QINPOS]     = dev->sns_qoff;
        aic7890_log(1, "AIC7890: shared queue tag=%02x sns=%u hns=%u shared=%08x\n",
                    tag, dev->sns_qoff, dev->hns_qoff, shared_addr);

        if (windows_shared) {
            if (tag == SCB_LIST_NULL)
                aic7890_probe_null_queue_tag(dev, shared_addr, queue_pos);
            else
                aic7890_log(1,
                            "AIC7890: windows shared queue raw tag=%02x ignored pos=%u cursor=%02x qout=%u\n",
                            tag, queue_pos, dev->win_next_hscb_tag,
                            dev->qout_next);
            if (!aic7890_windows_queue_tag(dev, queue_pos, &tag))
                continue;
        } else if (tag == SCB_LIST_NULL) {
            aic7890_probe_null_queue_tag(dev, shared_addr, queue_pos);
            continue;
        }

        aic7890_execute_scb(dev, tag, true);
    }
}

static void
aic7890_process_fifo(aic7890_t *dev)
{
    if (dev->regs[REG_HCNTRL] & HCNTRL_PAUSE)
        return;

    while (dev->qin_count != 0) {
        uint8_t tag;

        if (dev->regs[REG_INTSTAT] & INTSTAT_SCSIINT)
            break;

        tag = aic7890_qin_pop(dev);
        if (tag == SCB_LIST_NULL)
            continue;

        aic7890_execute_scb(dev, tag, false);
    }
}

static void
aic7890_process_pending(aic7890_t *dev)
{
    aic7890_process_queue(dev);
    aic7890_process_fifo(dev);
}

static bool
aic7890_has_pending_work(const aic7890_t *dev)
{
    uint32_t shared_addr = aic7890_shared_data_addr(dev);

    return dev->qin_count != 0
        || (shared_addr != 0 && dev->sns_qoff != dev->hns_qoff);
}

static void
aic7890_emulate_sequencer_run(aic7890_t *dev)
{
    bool reset_pulse;

    if (dev->regs[REG_HCNTRL] & HCNTRL_PAUSE)
        return;

    aic7890_process_pending(dev);

    reset_pulse = !!(dev->regs[REG_SCSISEQ] & SCSISEQ_SCSIRSTO);
    if (aic7890_seqaddr(dev) == 0x0004
        && !(dev->regs[REG_HCNTRL] & HCNTRL_INTEN)
        && !(dev->regs[REG_INTSTAT] & INTSTAT_INT_PEND)
        && !aic7890_has_pending_work(dev)) {
        dev->seq_idle_pauses++;
        /*
         * The onboard BIOS asserts SCSIRSTO and waits for the sequencer
         * to report idle again.  Since we do not execute the downloaded
         * sequencer, complete that reset pulse with the synthetic SEQINT.
         */
        if (reset_pulse)
            dev->regs[REG_SCSISEQ] &= ~SCSISEQ_SCSIRSTO;
        dev->regs[REG_INTSTAT] |= INTSTAT_SEQINT;
        dev->regs[REG_HCNTRL] |= HCNTRL_PAUSE;
        aic7890_log(1,
                    "AIC7890: sequencer idle self-pause %u seqaddr=%04x reset=%u intstat=%02x\n",
                    dev->seq_idle_pauses, aic7890_seqaddr(dev),
                    reset_pulse, dev->regs[REG_INTSTAT]);
        aic7890_update_irq(dev);
    }
}

static uint8_t
aic7890_seqram_read(aic7890_t *dev)
{
    uint16_t seqaddr = (uint16_t) dev->regs[REG_SEQADDR0]
                     | (((uint16_t) dev->regs[REG_SEQADDR1] & SEQADDR1_MASK) << 8);
    uint32_t pos = ((uint32_t) seqaddr * 4) + dev->seqram_byte;
    uint8_t ret = (pos < AIC7890_SEQRAM_SIZE) ? dev->seqram[pos] : 0xff;

    dev->seqram_reads++;
    dev->seqram_read_hash = aic7890_trace_hash(dev->seqram_read_hash, ret);
    if (dev->seqram_reads == 1 || (dev->seqram_reads % 1024) == 0)
        aic7890_log(1, "AIC7890: sequencer RAM reads=%u last_pos=%u hash=%08x\n",
                    dev->seqram_reads, pos, dev->seqram_read_hash);

    dev->seqram_byte = (dev->seqram_byte + 1) & 3;
    if (dev->seqram_byte == 0) {
        seqaddr++;
        dev->regs[REG_SEQADDR0] = seqaddr & 0xff;
        dev->regs[REG_SEQADDR1] = (seqaddr >> 8) & SEQADDR1_MASK;
    }

    return ret;
}

static void
aic7890_seqram_write(aic7890_t *dev, uint8_t val)
{
    uint16_t seqaddr = (uint16_t) dev->regs[REG_SEQADDR0]
                     | (((uint16_t) dev->regs[REG_SEQADDR1] & SEQADDR1_MASK) << 8);
    uint32_t pos = ((uint32_t) seqaddr * 4) + dev->seqram_byte;

    if (pos < AIC7890_SEQRAM_SIZE)
        dev->seqram[pos] = val;
    dev->seqram_writes++;
    dev->seqram_write_hash = aic7890_trace_hash(dev->seqram_write_hash, val);
    if (dev->seqram_writes == 1 || (dev->seqram_writes % 1024) == 0)
        aic7890_log(1, "AIC7890: sequencer RAM writes=%u last_pos=%u hash=%08x\n",
                    dev->seqram_writes, pos, dev->seqram_write_hash);

    dev->seqram_byte = (dev->seqram_byte + 1) & 3;
    if (dev->seqram_byte == 0) {
        seqaddr++;
        dev->regs[REG_SEQADDR0] = seqaddr & 0xff;
        dev->regs[REG_SEQADDR1] = (seqaddr >> 8) & SEQADDR1_MASK;
    }
}

#define AIC7890_REG_READ_RETURN(_val)                         \
    do {                                                       \
        uint8_t _aic7890_ret = (uint8_t) (_val);               \
        aic7890_trace_reg_read(dev, reg, _aic7890_ret);        \
        return _aic7890_ret;                                  \
    } while (0)

static uint8_t
aic7890_reg_read(uint32_t addr, void *priv)
{
    aic7890_t *dev = priv;
    uint8_t reg = addr & 0xff;

    if (((dev->regs[REG_SFUNCT] & 0x0f) == SFUNCT_CFGSPACE) && reg != REG_SFUNCT)
        AIC7890_REG_READ_RETURN(aic7890_pci_read(0, reg, 1, priv));

    if (reg >= REG_SCB_BASE && reg < (REG_SCB_BASE + AIC7890_SCB_SIZE)) {
        uint8_t scb_mask = aic7890_scb_page_mask(dev);
        uint8_t scb_addr = (dev->regs[REG_SCBCNT] & SCBCNT_SCBAUTO)
                         ? (dev->regs[REG_SCBCNT] & scb_mask)
                         : ((reg - REG_SCB_BASE) & scb_mask);
        uint8_t ret = dev->scb_ram[dev->regs[REG_SCBPTR]][scb_addr];

        if (dev->regs[REG_SCBCNT] & SCBCNT_SCBAUTO)
            dev->regs[REG_SCBCNT] = SCBCNT_SCBAUTO | ((scb_addr + 1) & scb_mask);
        AIC7890_REG_READ_RETURN(ret);
    }

    switch (reg) {
        case REG_SCBCNT:
            AIC7890_REG_READ_RETURN(dev->regs[REG_SCBCNT] & (SCBCNT_SCBAUTO | aic7890_scb_page_mask(dev)));

        case REG_BRDCTL:
            if (dev->regs[REG_BRDCTL] & BRDCTL_BRDRW)
                AIC7890_REG_READ_RETURN(dev->brdctl_data | BRDCTL_BRDRW);
            AIC7890_REG_READ_RETURN(dev->regs[REG_BRDCTL]);

        case REG_SEECTL: {
            uint8_t ret = dev->regs[REG_SEECTL] | SEECTL_SEERDY;
            if (dev->regs[REG_SEECTL] & SEECTL_EXTARBREQ)
                ret |= SEECTL_EXTARBACK;
            if (nmc93cxx_eeprom_read(dev->eeprom))
                ret |= SEECTL_SEEDI;
            else
                ret &= ~SEECTL_SEEDI;
            AIC7890_REG_READ_RETURN(ret);
        }

        case REG_SEQRAM:
            AIC7890_REG_READ_RETURN(aic7890_seqram_read(dev));

        case REG_QINCNT:
            AIC7890_REG_READ_RETURN(dev->qin_count & 0xff);

        case REG_QOUTFIFO:
            AIC7890_REG_READ_RETURN(aic7890_qout_pop(dev));

        case REG_QOUTCNT:
            AIC7890_REG_READ_RETURN(dev->qout_count & 0xff);

        case REG_ERROR_CLRINT:
            AIC7890_REG_READ_RETURN(0);

        case REG_CCSCBRAM: {
            uint8_t ccscb_addr = dev->regs[REG_CCSCBADDR];
            uint8_t ret = dev->ccscb_ram[ccscb_addr & aic7890_scb_page_mask(dev)];

            dev->regs[REG_CCSCBADDR] = ccscb_addr + 1;
            AIC7890_REG_READ_RETURN(ret);
        }

        case REG_SNSCB_QOFF: {
            uint8_t ret = dev->sns_qoff;

            dev->sns_qoff++;
            dev->regs[REG_SNSCB_QOFF] = dev->sns_qoff;
            AIC7890_REG_READ_RETURN(ret);
        }

        case REG_SDSCB_QOFF: {
            uint8_t ret = dev->qout_next;

            dev->qout_next++;
            dev->regs[REG_SDSCB_QOFF] = dev->qout_next;
            AIC7890_REG_READ_RETURN(ret);
        }

        case REG_QOFF_CTLSTA:
            AIC7890_REG_READ_RETURN(aic7890_qoff_status(dev));

        default:
            AIC7890_REG_READ_RETURN(dev->regs[reg]);
    }
}

#undef AIC7890_REG_READ_RETURN

static void
aic7890_reg_write(uint32_t addr, uint8_t val, void *priv)
{
    aic7890_t *dev = priv;
    uint8_t reg = addr & 0xff;

    if (((dev->regs[REG_SFUNCT] & 0x0f) == SFUNCT_CFGSPACE) && reg != REG_SFUNCT) {
        aic7890_pci_write(0, reg, 1, val, priv);
        return;
    }

    aic7890_trace_reg_write(dev, reg, val);

    if (reg >= REG_SCB_BASE && reg < (REG_SCB_BASE + AIC7890_SCB_SIZE)) {
        uint8_t scb_mask = aic7890_scb_page_mask(dev);
        uint8_t scb_addr = (dev->regs[REG_SCBCNT] & SCBCNT_SCBAUTO)
                         ? (dev->regs[REG_SCBCNT] & scb_mask)
                         : ((reg - REG_SCB_BASE) & scb_mask);

        dev->scb_ram[dev->regs[REG_SCBPTR]][scb_addr] = val;
        if (dev->regs[REG_SCBCNT] & SCBCNT_SCBAUTO)
            dev->regs[REG_SCBCNT] = SCBCNT_SCBAUTO | ((scb_addr + 1) & scb_mask);
        return;
    }

    switch (reg) {
        case REG_SCSISEQ:
            if ((val & SCSISEQ_SCSIRSTO) && !(dev->regs[reg] & SCSISEQ_SCSIRSTO))
                aic7890_scsi_bus_reset(dev, false);
            dev->regs[reg] = val;
            return;

        case REG_CLRSINT0_SSTAT0:
            dev->regs[reg] &= ~(val & (CLRSINT0_CLRSELDO | CLRSINT0_CLRSELDI
                                      | CLRSINT0_CLRSELINGO | CLRSINT0_CLRSPIORDY
                                      | SSTAT0_DMADONE));
            return;

        case REG_CLRSINT1_SSTAT1:
            if (val & CLRSINT1_CLRSELTIMEO)
                dev->regs[reg] &= ~SSTAT1_SELTO;
            if (val & CLRSINT1_CLRATNO)
                dev->regs[reg] &= ~SSTAT1_ATNO;
            if (val & CLRSINT1_CLRSCSIRSTI)
                dev->regs[reg] &= ~SSTAT1_SCSIRSTI;
            if (val & CLRSINT1_CLRBUSFREE)
                dev->regs[reg] &= ~SSTAT1_BUSFREE;
            if (val & CLRSINT1_CLRSCSIPERR)
                dev->regs[reg] &= ~SSTAT1_SCSIPERR;
            if (val & CLRSINT1_CLRPHASECHG)
                dev->regs[reg] &= ~SSTAT1_PHASECHG;
            if (val & CLRSINT1_CLRREQINIT)
                dev->regs[reg] &= ~SSTAT1_REQINIT;
            return;

        case REG_SEECTL:
            dev->regs[reg] = val & ~(SEECTL_SEERDY | SEECTL_SEEDI | SEECTL_EXTARBACK);
            nmc93cxx_eeprom_write(dev->eeprom, !!(val & SEECTL_SEECS),
                                  !!(val & SEECTL_SEECK), !!(val & SEECTL_SEEDO));
            return;

        case REG_BRDCTL:
            dev->regs[reg] = val;
            if ((val & BRDCTL_BRDSTB) && !(val & BRDCTL_BRDRW))
                dev->brdctl_data = BRDCTL_BRDDAT7
                                 | (val & (BRDCTL_BRDDAT6 | BRDCTL_BRDDAT5
                                         | BRDCTL_BRDDAT4 | BRDCTL_BRDDAT3
                                         | BRDCTL_BRDDAT2));
            return;

        case REG_SBLKCTL:
            dev->regs[reg] = (val & (SBLKCTL_DIAGLEDEN | SBLKCTL_DIAGLEDON
                                   | SBLKCTL_AUTOFLUSHDIS | SBLKCTL_SELWIDE
                                   | SBLKCTL_XCVR))
                           | SBLKCTL_ENAB40 | SBLKCTL_SELWIDE;
            return;

        case REG_SEQADDR0:
        case REG_SEQADDR1:
            dev->regs[reg] = (reg == REG_SEQADDR1) ? (val & SEQADDR1_MASK) : val;
            dev->seqram_byte = 0;
            return;

        case REG_SEQCTL:
        {
            uint8_t old_seqctl = dev->regs[reg];

            dev->regs[reg] = val;
            if ((old_seqctl & SEQCTL_LOADRAM) && !(val & SEQCTL_LOADRAM))
                aic7890_log(1,
                            "AIC7890: sequencer LOADRAM clear writes=%u/%08x reads=%u/%08x seqaddr=%02x%02x\n",
                            dev->seqram_writes, dev->seqram_write_hash,
                            dev->seqram_reads, dev->seqram_read_hash,
                            dev->regs[REG_SEQADDR1], dev->regs[REG_SEQADDR0]);
            else if (!(old_seqctl & SEQCTL_LOADRAM) && (val & SEQCTL_LOADRAM))
                aic7890_log(1,
                            "AIC7890: sequencer LOADRAM set writes=%u/%08x reads=%u/%08x seqaddr=%02x%02x\n",
                            dev->seqram_writes, dev->seqram_write_hash,
                            dev->seqram_reads, dev->seqram_read_hash,
                            dev->regs[REG_SEQADDR1], dev->regs[REG_SEQADDR0]);
            return;
        }

        case REG_SEQRAM:
            aic7890_seqram_write(dev, val);
            return;

        case REG_HCNTRL:
        {
            uint8_t old_hcntrl = dev->regs[reg];

            if (val & HCNTRL_CHIPRST) {
                aic7890_log(1, "AIC7890: chip reset request hcntrl=%02x\n", val);
                aic7890_reset_regs(dev);
                dev->regs[REG_HCNTRL] = (val & (HCNTRL_HCNTRL3 | HCNTRL_PAUSE | HCNTRL_INTEN))
                                      | HCNTRL_CHIPRST;
            } else {
                dev->regs[reg] = val & (HCNTRL_POWRDN | HCNTRL_SWINT | HCNTRL_HCNTRL3
                                      | HCNTRL_PAUSE | HCNTRL_INTEN);
            }
            if ((old_hcntrl & HCNTRL_PAUSE) && !(dev->regs[REG_HCNTRL] & HCNTRL_PAUSE))
                aic7890_log(1,
                            "AIC7890: sequencer unpaused writes=%u/%08x reads=%u/%08x seqaddr=%02x%02x\n",
                            dev->seqram_writes, dev->seqram_write_hash,
                            dev->seqram_reads, dev->seqram_read_hash,
                            dev->regs[REG_SEQADDR1], dev->regs[REG_SEQADDR0]);
            else if (!(old_hcntrl & HCNTRL_PAUSE) && (dev->regs[REG_HCNTRL] & HCNTRL_PAUSE))
                aic7890_log(1, "AIC7890: sequencer paused hcntrl=%02x\n",
                            dev->regs[REG_HCNTRL]);
            aic7890_update_irq(dev);
            if (!(dev->regs[REG_HCNTRL] & HCNTRL_PAUSE))
                aic7890_emulate_sequencer_run(dev);
            return;
        }

        case REG_ERROR_CLRINT:
            if (val & CLRINT_CLRCMDINT)
                dev->regs[REG_INTSTAT] &= ~INTSTAT_CMDCMPLT;
            if (val & CLRINT_CLRSCSIINT)
                dev->regs[REG_INTSTAT] &= ~INTSTAT_SCSIINT;
            if (val & CLRINT_CLRSEQINT)
                dev->regs[REG_INTSTAT] &= ~INTSTAT_SEQINT;
            if (val & CLRINT_CLRBRKADRINT)
                dev->regs[REG_INTSTAT] &= ~INTSTAT_BRKADRINT;
            aic7890_log(1, "AIC7890: clear interrupt val=%02x intstat=%02x\n",
                        val, dev->regs[REG_INTSTAT]);
            aic7890_update_irq(dev);
            aic7890_process_pending(dev);
            return;

        case REG_DFCNTRL:
            dev->regs[reg] = val & ~(0x03);
            return;

        case REG_DSCOMMAND0:
            dev->regs[reg] = val & ~DSCOMMAND0_RAMPS;
            dev->regs[REG_SCBCNT] &= SCBCNT_SCBAUTO | aic7890_scb_page_mask(dev);
            return;

        case REG_QINFIFO:
            aic7890_qin_push(dev, val);
            aic7890_process_fifo(dev);
            return;

        case REG_CCHCNT:
            dev->regs[reg] = val;
            if (!(dev->regs[REG_CCSCBCTL] & CCSCBCTL_CCARREN))
                dev->regs[REG_CCSCBCNT] = val;
            return;

        case REG_CCSCBRAM:
            dev->ccscb_ram[dev->regs[REG_CCSCBADDR] & aic7890_scb_page_mask(dev)] = val;
            dev->regs[REG_CCSCBADDR]++;
            return;

        case REG_SCBCNT:
            dev->regs[reg] = val & (SCBCNT_SCBAUTO | aic7890_scb_page_mask(dev));
            return;

        case REG_CCSCBCTL:
            aic7890_run_ccscb(dev, val);
            return;

        case REG_KERNEL_QINPOS:
            dev->hns_qoff = val;
            dev->regs[reg] = val;
            aic7890_process_queue(dev);
            return;

        case REG_QINPOS:
            dev->sns_qoff = val;
            dev->regs[reg] = val;
            return;

        case REG_QOUTPOS:
            dev->qout_next = val;
            if (val == 0)
                dev->win_qout_valid = 0;
            dev->regs[reg] = val;
            return;

        case REG_HNSCB_QOFF:
            if (val < dev->hns_qoff)
                dev->win_last_inquiry_target = SCB_LIST_NULL;
            dev->hns_qoff = val;
            dev->regs[reg] = val;
            aic7890_process_queue(dev);
            return;

        case REG_SNSCB_QOFF:
            dev->sns_qoff = val;
            dev->regs[reg] = val;
            return;

        case REG_SDSCB_QOFF:
            dev->qout_next = val;
            if (val == 0) {
                dev->win_qout_valid = 0;
                aic7890_log(1, "AIC7890: windows qout valid reset\n");
            }
            dev->regs[reg] = val;
            return;

        case REG_QOFF_CTLSTA:
            dev->regs[reg] = val & 0x07;
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
    aic7890_log(1, "AIC7890: IO disabled base=%04x\n", dev->io_base);
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
    aic7890_log(1, "AIC7890: IO enabled base=%04x\n", dev->io_base);
}

static void
aic7890_mmio_remap(aic7890_t *dev)
{
    uint32_t base = aic7890_pci_bar(dev, 1) & ~0x0fU;

    if ((dev->pci_cfg[PCI_REG_COMMAND_L] & PCI_COMMAND_MEM) && base != 0) {
        mem_mapping_set_addr(&dev->mmio_mapping, base, AIC7890_PCI_MMIO_SIZE);
        aic7890_log(1, "AIC7890: MMIO enabled base=%08x\n", base);
    } else {
        mem_mapping_disable(&dev->mmio_mapping);
        aic7890_log(1, "AIC7890: MMIO disabled base=%08x cmd=%02x\n",
                    base, dev->pci_cfg[PCI_REG_COMMAND_L]);
    }
}

static uint8_t
aic7890_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    aic7890_t *dev = priv;
    uint8_t reg = addr & 0xff;
    uint8_t ret = dev->pci_cfg[reg];

    aic7890_trace_pci_read(reg, ret);
    return ret;
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

    aic7890_trace_pci_write(reg, dev->pci_cfg[reg], val, mask);

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
    dev->pci_cfg[0x40]                   = 0x40;
    dev->pci_cfg[PCI_REG_INT_LINE]       = 0x00;
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
    bool large_seeprom = !!(info->local & AIC7890_LOCAL_LARGE_SEEPROM);

    dev->scsi_bus = scsi_get_bus();
    if (dev->scsi_bus < SCSI_BUS_MAX)
        scsi_bus_set_speed(dev->scsi_bus, 80000000.0);

    aic7890_create_eeprom(dev, large_seeprom);
    snprintf(eeprom_name, sizeof(eeprom_name), "nmc93cxx_eeprom_%s_%d.nvr",
             info->internal_name, inst);
    eeprom_params.type = large_seeprom ? NMC_93C66_x16_256 : NMC_93C46_x16_64;
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

    aic7890_log(1, "AIC7890: init bus=%u pci_slot=%u local=%08" PRIxPTR "\n",
                dev->scsi_bus, dev->pci_slot, info->local);

    return dev;
}

static void
aic7890_close(void *priv)
{
    aic7890_t *dev = priv;

    aic7890_io_disable(dev);
    mem_mapping_disable(&dev->mmio_mapping);
    aic7890_log(1, "AIC7890: close\n");
    free(dev);
}

const device_t aic7890_pci_device = {
    .name          = "Adaptec AIC-7890",
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
    .name          = "Adaptec AIC-7890 (On-Board)",
    .internal_name = "aic7890_onboard",
    .flags         = DEVICE_PCI | DEVICE_ONBOARD,
    .local         = AIC7890_LOCAL_ONBOARD | AIC7890_LOCAL_LARGE_SEEPROM,
    .init          = aic7890_init,
    .close         = aic7890_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
