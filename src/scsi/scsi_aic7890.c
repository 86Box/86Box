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
#include <86box/cdrom.h>
#include <86box/scsi_cdrom.h>
#include <86box/scsi_aic7890.h>
#include <86box/nmc93cxx.h>
#include <86box/timer.h>
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
#define REG_SCSIDATL          0x06
#define REG_SCSIDATH          0x07
#define REG_CLRSINT0_SSTAT0   0x0b
#define REG_CLRSINT1_SSTAT1   0x0c
#define REG_SSTAT2            0x0d
#define REG_SSTAT3            0x0e
#define REG_SCSIBUSL          0x12
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
#define REG_CCSGCTL           0xeb
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
#define SSTAT0_SWRAP          0x08
#define SSTAT0_SDONE          0x04
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
#define CFSUPREM              0x0001
#define CFSUPREMB             0x0002
#define CFBIOSEN              0x0004
#define CFBIOS_BUSSCAN        0x0008
#define CFSM2DRV              0x0010
#define CFCTRL_A              0x0020
#define CFEXTEND              0x0080
#define CFBOOTCD              0x0800
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
    uint8_t       trace_last_read[AIC7890_REG_WINDOW];
    uint32_t      trace_same_reads[AIC7890_REG_WINDOW];
    bool          trace_read_valid[AIC7890_REG_WINDOW];
    bool          trace_irq_valid;
    bool          trace_irq_active;

    uint16_t      io_base;
    bool          io_enabled;
    mem_mapping_t mmio_mapping;

    pc_timer_t    countdown_timer;
    uint32_t      countdown_remaining;
    bool          countdown_active;

    pc_timer_t    scan_init_timer;
    uint32_t      scan_init_remaining;
    bool          scan_init_active;
    uint32_t      scan_init_hash;   /* seqram hash for the init interrupt */

    pc_timer_t    scan2_timer;
    uint32_t      scan2_remaining;
    bool          scan2_active;

    /* Sequencer microcode interpreter state (scan-start at SEQADDR 0x0002). */
    uint16_t      seq_pc;
    uint16_t      seq_stack[4];
    int           seq_sp;
    uint64_t      seq_steps;
    bool          seq_active;

    /* Minimal SCSI bus model for the scan-start probe. */
    uint8_t       seq_phase;        /* phase to present (SCSISIGI bits 7-5) */
    bool          seq_req_pending;  /* target has asserted REQ */
    bool          seq_cmd_done;     /* command has been handed to the device */
    int           seq_stage;        /* probe phase progression stage */
    uint8_t       seq_cmd[16];      /* command bytes collected from PIO/DMA */
    int           seq_cmd_len;
    uint8_t       seq_status;       /* device status byte */
    bool          seq_has_data;
    uint8_t      *seq_data;
    int           seq_data_len;
    int           seq_data_pos;
    bool          seq_dma_done;     /* status/message byte transferred (DMADONE) */
    uint8_t       seq_msg;          /* MESSAGE IN byte (0x00 = command complete) */
    bool          seq_busfree;      /* target released the bus */

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

static uint64_t aic7890_scan_init_us = 0;
static int      aic7890_scan_init_int = 0;
static int      aic7890_scan_init_pause_setting = -1;
static int      aic7890_scan2_init_int = 0;
static uint32_t aic7890_countdown_cap = 0;

static uint64_t
aic7890_scan_init_delay(void)
{
    if (aic7890_scan_init_us == 0) {
        const char *env = getenv("AIC7890_SCAN_US");

        aic7890_scan_init_us = (env != NULL && env[0] != '\0')
                             ? strtoul(env, NULL, 10) : 1;
    }
    return aic7890_scan_init_us;
}

static int
aic7890_scan_init_interrupt(void)
{
    if (aic7890_scan_init_int == 0) {
        const char *env = getenv("AIC7890_SCAN_INT");

        aic7890_scan_init_int = (env != NULL && env[0] != '\0')
                              ? (int) strtoul(env, NULL, 0) : INTSTAT_SEQINT;
    }
    return aic7890_scan_init_int;
}

static int
aic7890_scan_init_pause(void)
{
    if (aic7890_scan_init_pause_setting < 0) {
        const char *env = getenv("AIC7890_SCAN_PAUSE");

        aic7890_scan_init_pause_setting = (env != NULL && env[0] != '\0')
                                        ? atoi(env) : 1;
    }
    return aic7890_scan_init_pause_setting;
}

static uint32_t
aic7890_countdown_cap_us(void)
{
    if (aic7890_countdown_cap == 0) {
        const char *env = getenv("AIC7890_CDN_US");

        aic7890_countdown_cap = (env != NULL && env[0] != '\0')
                              ? (uint32_t) strtoul(env, NULL, 10) : 500;
    }
    return aic7890_countdown_cap;
}

static int
aic7890_scan2_interrupt(void)
{
    if (aic7890_scan2_init_int == 0) {
        const char *env = getenv("AIC7890_SCAN2_INT");

        if (env != NULL && env[0] != '\0')
            aic7890_scan2_init_int = (int) strtoul(env, NULL, 0);
    }
    return aic7890_scan2_init_int;
}

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
    dev->countdown_active = false;
    timer_disable(&dev->countdown_timer);
    dev->scan_init_active = false;
    dev->scan_init_hash = AIC7890_TRACE_HASH_INIT;
    timer_disable(&dev->scan_init_timer);
    dev->scan2_active = false;
    timer_disable(&dev->scan2_timer);
    dev->seq_active = false;
    dev->seq_sp = 0;
    dev->seq_steps = 0;
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

    config[16] = CFSUPREM | CFSUPREMB | CFBIOSEN | CFBIOS_BUSSCAN | CFSM2DRV
               | CFCTRL_A | CFEXTEND | CFBOOTCD;
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
    dev->countdown_active = false;
    timer_disable(&dev->countdown_timer);
    dev->scan_init_active = false;
    dev->scan_init_hash = AIC7890_TRACE_HASH_INIT;
    timer_disable(&dev->scan_init_timer);
    dev->scan2_active = false;
    timer_disable(&dev->scan2_timer);
    dev->seq_active = false;
    dev->seq_sp = 0;
    dev->seq_steps = 0;
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

/* ------------------------------------------------------------------ */
/* AIC-7890 sequencer microcode interpreter.                           */
/*                                                                     */
/* The BIOS downloads a 24-bit-encoded program into the sequencer RAM  */
/* and starts it at SEQADDR 0x0002 (scan-start).  The emulator cannot  */
/* rely on the host driver's shared-queue SCB path here, so we decode   */
/* and execute the microcode directly.  Only the scan-start path needs  */
/* to be followed far enough to complete the BIOS probe; the engine is  */
/* written generally so future phases can reuse it.                     */
/* ------------------------------------------------------------------ */

#define REG_ACCUM              0x64
#define REG_SINDEX             0x65
#define REG_DINDEX             0x66
#define REG_ALLONES            0x69
#define REG_ALLZEROS           0x6a
#define REG_FLAGS              0x6b
#define REG_SINDIR             0x6c
#define REG_DINDIR             0x6d
#define REG_STACK              0x6f

#define SEQ_FLAG_ZERO          0x02
#define SEQ_FLAG_CARRY         0x01

#define SEQ_OP_OR              0x0
#define SEQ_OP_AND             0x1
#define SEQ_OP_XOR             0x2
#define SEQ_OP_ADD             0x3
#define SEQ_OP_ADC             0x4
#define SEQ_OP_ROL             0x5
#define SEQ_OP_BMOV            0x6
#define SEQ_OP_MVI16           0x7
#define SEQ_OP_JMP             0x8
#define SEQ_OP_JC              0x9
#define SEQ_OP_JNC             0xa
#define SEQ_OP_CALL            0xb
#define SEQ_OP_JNE             0xc
#define SEQ_OP_JNZ             0xd
#define SEQ_OP_JE              0xe
#define SEQ_OP_JZ              0xf

/* 16-bit and far operations: opcode_ext byte selects the variant. */
#define SEQ_OPEXT_OR16         0x80
#define SEQ_OPEXT_AND16        0x81
#define SEQ_OPEXT_XOR16        0x82
#define SEQ_OPEXT_ADD16        0x83
#define SEQ_OPEXT_ADC16        0x84
#define SEQ_OPEXT_JNE16        0x88
#define SEQ_OPEXT_JNZ16        0x89
#define SEQ_OPEXT_JZ16         0x8b
#define SEQ_OPEXT_JE16         0x8c
#define SEQ_OPEXT_JMP16        0x90
#define SEQ_OPEXT_JC16         0x91
#define SEQ_OPEXT_JNC16        0x92
#define SEQ_OPEXT_CALL16       0x93

#define SEQ_SEQCTL_FASTMODE    0x80
#define SEQ_SEQCTL_FASTMODE2   0x40
#define SEQ_SEQCTL_RESET       0x20
#define SEQ_SEQCTL_LOADRAM     0x10
#define SEQ_SEQCTL_PAUSEDIS    0x08
#define SEQ_SEQCTL_STEP        0x04

#define SEQ_MAX_STEPS          200000

/* Execute the scan-start probe command against the selected target. */
static void
aic7890_seq_probe_command(aic7890_t *dev)
{
    uint8_t *scb = dev->scb_ram[dev->regs[REG_SCBPTR]];
    uint8_t target = (dev->regs[REG_SCSIID] >> 4) & 0x0f;
    uint8_t cdb[16];
    int cdb_len = scb[0x02];
    scsi_device_t *sd;

    if (cdb_len == 0 || cdb_len > 16)
        cdb_len = 12;
    memcpy(cdb, scb, cdb_len);
    memset(cdb, 0, sizeof(cdb));   /* XXX HADDR DMA not modelled: use clean TUR */


    aic7890_log(1,
                "AIC7890: scan2 probe target=%u scbptr=%02x len=%d cdb=%02x %02x %02x %02x %02x %02x\n",
                target, dev->regs[REG_SCBPTR], cdb_len,
                cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5]);
    aic7890_log(1,
                "AIC7890: scan2 probe sindex=%02x dindex=%02x haddr=%02x%02x%02x%02x scb=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                dev->regs[REG_SINDEX], dev->regs[REG_DINDEX],
                dev->regs[0x8b], dev->regs[0x8a], dev->regs[0x89], dev->regs[0x88],
                dev->scb_ram[dev->regs[REG_SCBPTR]][0],
                dev->scb_ram[dev->regs[REG_SCBPTR]][1],
                dev->scb_ram[dev->regs[REG_SCBPTR]][2],
                dev->scb_ram[dev->regs[REG_SCBPTR]][3],
                dev->scb_ram[dev->regs[REG_SCBPTR]][4],
                dev->scb_ram[dev->regs[REG_SCBPTR]][5],
                dev->scb_ram[dev->regs[REG_SCBPTR]][6],
                dev->scb_ram[dev->regs[REG_SCBPTR]][7],
                dev->scb_ram[dev->regs[REG_SCBPTR]][8],
                dev->scb_ram[dev->regs[REG_SCBPTR]][9],
                dev->scb_ram[dev->regs[REG_SCBPTR]][10],
                dev->scb_ram[dev->regs[REG_SCBPTR]][11]);

    if (target >= SCSI_ID_MAX || dev->scsi_bus >= SCSI_BUS_MAX
        || !scsi_device_present(&scsi_devices[dev->scsi_bus][target])) {
        dev->seq_status = 0;
        dev->seq_cmd_done = true;
        return;
    }

    sd = &scsi_devices[dev->scsi_bus][target];
    sd->buffer_length = -1;
    scsi_device_identify(sd, 0);
    if (sd->type == SCSI_REMOVABLE_CDROM && sd->sc != NULL)
        ((scsi_cdrom_t *) sd->sc)->unit_attention = 0;   /* clear UA after boot reset */
    scsi_device_command_phase0(sd, cdb);
    if (sd->phase != SCSI_PHASE_STATUS && sd->buffer_length > 0)
        scsi_device_command_phase1(sd);
    scsi_device_identify(sd, SCSI_LUN_USE_CDB);

    dev->seq_status = sd->status;
    dev->seq_has_data = false;
    dev->seq_data = NULL;
    dev->seq_data_len = 0;
    dev->seq_data_pos = 0;
    if (sd->sc != NULL && sd->buffer_length > 0 && sd->buffer_length < 4096) {
        dev->seq_data = sd->sc->temp_buffer;
        dev->seq_data_len = sd->buffer_length;
        dev->seq_data_pos = 0;
        dev->seq_has_data = true;
    }
    dev->seq_cmd_done = true;
    aic7890_log(1,
                "AIC7890: scan2 probe result status=%02x data_len=%d phase=%d ua=%d cdst=%d sense[2]=%02x\n",
                dev->seq_status, dev->seq_data_len, sd->phase,
                sd->type == SCSI_REMOVABLE_CDROM && sd->sc != NULL
                    ? ((scsi_cdrom_t *) sd->sc)->unit_attention : -1,
                sd->type == SCSI_REMOVABLE_CDROM && sd->sc != NULL
                    ? ((scsi_cdrom_t *) sd->sc)->drv->cd_status : -1,
                sd->sc != NULL ? sd->sc->sense[2] : 0xff);
}

static uint32_t
aic7890_seq_word(const aic7890_t *dev, uint16_t pc)
{
    uint32_t pos = ((uint32_t) pc & 0x3ff) * 4;

    if (pos + 3 >= AIC7890_SEQRAM_SIZE)
        return 0;
    return (uint32_t) dev->seqram[pos]
         | ((uint32_t) dev->seqram[pos + 1] << 8)
         | ((uint32_t) dev->seqram[pos + 2] << 16)
         | ((uint32_t) dev->seqram[pos + 3] << 24);
}

static uint8_t
aic7890_seq_flag_carry(const aic7890_t *dev)
{
    return dev->regs[REG_FLAGS] & SEQ_FLAG_CARRY;
}

static void
aic7890_seq_set_flags(aic7890_t *dev, bool zero, bool carry)
{
    uint8_t f = dev->regs[REG_FLAGS] & ~(SEQ_FLAG_ZERO | SEQ_FLAG_CARRY);

    if (zero)
        f |= SEQ_FLAG_ZERO;
    if (carry)
        f |= SEQ_FLAG_CARRY;
    dev->regs[REG_FLAGS] = f;
}

static uint8_t
aic7890_seq_effective_addr(const aic7890_t *dev, uint16_t field, bool is_dest)
{
    uint16_t base = is_dest ? dev->regs[REG_DINDEX] : dev->regs[REG_SINDEX];

    if (field & 0x100)
        return (uint8_t) ((base + (field & 0xff)) & 0xff);
    return (uint8_t) (field & 0xff);
}

static uint8_t
aic7890_seq_reg_read(aic7890_t *dev, uint8_t addr, int depth)
{
    if (depth > 8)
        return 0;

    switch (addr) {
        case REG_ALLONES:
            return 0xff;
        case REG_ALLZEROS:
            return 0x00;
        case REG_SINDIR:
            return aic7890_seq_reg_read(dev, dev->regs[REG_SINDEX], depth + 1);
        default:
            break;
    }

    if (addr == REG_CLRSINT1_SSTAT1) {
        uint8_t ret = dev->regs[addr];

        if (dev->seq_busfree)
            ret |= SSTAT1_BUSFREE;
        if (dev->seq_req_pending) {
            ret |= SSTAT1_REQINIT;
            /*
             * The wait-for-REQ routine is entered by CALL from several
             * places; the return address on the stack tells us which
             * phase the target should be presenting now.
             */
            if (dev->seq_cmd_done && dev->seq_sp > 0) {
                uint16_t retaddr = dev->seq_stack[dev->seq_sp - 1];

                if (retaddr == 0x166 || retaddr == 0x66) {
                    if (dev->seq_has_data && dev->seq_data_pos < dev->seq_data_len)
                        dev->seq_phase = 0x40;   /* DATA IN */
                    else
                        dev->seq_phase = 0xc0;   /* STATUS */
                } else if (retaddr == 0xaf) {
                    dev->seq_phase = 0xe0;       /* MESSAGE OUT */
                }
            }
        }
        return ret;
    }
    if (addr == REG_CLRSINT0_SSTAT0) {
        uint8_t ret = dev->regs[addr];

        if (dev->seq_cmd_done)
            ret |= SSTAT0_SDONE;
        if (dev->seq_dma_done)
            ret |= SSTAT0_DMADONE;
        return ret;
    }
    if (addr == REG_SCSISIGI) {
        aic7890_log(2, "AIC7890: SCSISIGI read phase=%02x\n", dev->seq_phase);
        return dev->seq_phase | 0x01;   /* include BSY */
    }
    if (addr == REG_SCSIDATL)
        return dev->seq_status;
    if (addr == REG_CCSGCTL)
        return dev->regs[addr] | 0x80;  /* CCSG transfer done */
    if (addr == REG_SCSIBUSL) {
        if (dev->seq_has_data && dev->seq_data_pos < dev->seq_data_len)
            return dev->seq_data[dev->seq_data_pos++];
        dev->regs[REG_CLRSINT1_SSTAT1] |= SSTAT1_BUSFREE;   /* target released the bus */
        return 0x00;                     /* command-complete message */
    }

    if (addr >= REG_SCB_BASE && addr < (REG_SCB_BASE + AIC7890_SCB_SIZE))
        return dev->scb_ram[dev->regs[REG_SCBPTR]][addr - REG_SCB_BASE];

    return dev->regs[addr];
}

static void
aic7890_seq_reg_write(aic7890_t *dev, uint8_t addr, uint8_t val)
{
    if (addr == REG_ALLZEROS)      /* NONE destination */
        return;
    if (addr == REG_DINDIR) {
        aic7890_seq_reg_write(dev, dev->regs[REG_DINDEX], val);
        return;
    }

    if (addr == REG_DFCNTRL) {
        dev->regs[addr] = val;
        if (val & 0x20) {   /* SCSIEN: a SCSI DMA was started */
            if (!dev->seq_cmd_done && (dev->seq_phase & 0x80)) {
                aic7890_seq_probe_command(dev);
            } else if (dev->seq_cmd_done) {
                /* status / message byte transfer: complete it immediately */
                dev->seq_dma_done = true;
            }
        }
        return;
    }

    if (addr == REG_CCSGCTL) {
        dev->regs[addr] = val;
        /* CCSG DMA: copy the device response data into the SCB. */
        if (dev->seq_cmd_done && dev->seq_dma_done) {
            dev->seq_dma_done = false;
            if (dev->seq_phase == 0xc0) {          /* STATUS byte consumed */
                dev->seq_phase = 0xe0;             /* MESSAGE IN next */
                dev->seq_req_pending = true;
            } else if (dev->seq_phase == 0xe0) {   /* MESSAGE IN byte consumed */
                dev->seq_phase = 0;                /* BUSFREE */
                dev->seq_req_pending = false;
                dev->seq_busfree = true;
            }
        }
        if (dev->seq_has_data && dev->seq_data_len > 0) {
            uint8_t *scb = dev->scb_ram[dev->regs[REG_SCBPTR]];
            int n = AIC_MIN(8, dev->seq_data_len - dev->seq_data_pos);

            for (int i = 0; i < n; i++)
                scb[0x18 + i] = dev->seq_data[dev->seq_data_pos + i];
            dev->seq_data_pos += n;
            if (dev->seq_data_pos >= dev->seq_data_len) {
                dev->seq_has_data = false;
                dev->seq_phase = 0xc0;      /* STATUS follows */
                dev->seq_req_pending = true;
            }
        }
        return;
    }

    if (addr == REG_QOUTFIFO) {
        dev->regs[addr] = val;
        aic7890_qout_push(dev, val);
        return;
    }

    if (addr >= REG_SCB_BASE && addr < (REG_SCB_BASE + AIC7890_SCB_SIZE)) {
        dev->scb_ram[dev->regs[REG_SCBPTR]][addr - REG_SCB_BASE] = val;
        return;
    }

    dev->regs[addr] = val;
}

static uint8_t
aic7890_seq_field_read(aic7890_t *dev, uint16_t field)
{
    return aic7890_seq_reg_read(dev, aic7890_seq_effective_addr(dev, field, false), 0);
}

static void
aic7890_seq_field_write(aic7890_t *dev, uint16_t field, uint8_t val)
{
    aic7890_seq_reg_write(dev, aic7890_seq_effective_addr(dev, field, true), val);
}

static void
aic7890_seq_jump(aic7890_t *dev, uint16_t addr)
{
    dev->seq_pc = addr & 0x3ff;
}

static void
aic7890_seq_call(aic7890_t *dev, uint16_t addr)
{
    if (dev->seq_sp < 4)
        dev->seq_stack[dev->seq_sp++] = (dev->seq_pc + 1) & 0x3ff;
    else
        aic7890_log(2, "AIC7890: CALL STACK FULL at %03x sp=%d\n", dev->seq_pc, dev->seq_sp);
    aic7890_seq_jump(dev, addr);
}

static bool
aic7890_seq_ret(aic7890_t *dev)
{
    if (dev->seq_sp <= 0)
        return false;
    dev->seq_pc = dev->seq_stack[--dev->seq_sp];
    return true;
}

/* Decode and execute a single microcode word; advance seq_pc. */
static bool
aic7890_seq_step(aic7890_t *dev)
{
    uint32_t w = aic7890_seq_word(dev, dev->seq_pc);
    uint16_t cur_pc = dev->seq_pc;
    uint16_t opcode = (w >> 27) & 0xf;
    bool ret = !!(w & (1u << 26));
    uint8_t imm = w & 0xff;
    uint16_t source = (w >> 8) & 0x1ff;
    uint16_t dest = (w >> 17) & 0x1ff;
    uint16_t addr = (w >> 17) & 0x3ff;
    uint16_t next = (dev->seq_pc + 1) & 0x3ff;
    bool taken = false;
    uint8_t sv, dv, rv;

    switch (opcode) {
        case SEQ_OP_OR:
            sv = aic7890_seq_field_read(dev, source);
            rv = sv | imm;
            aic7890_seq_field_write(dev, dest, rv);
            aic7890_seq_set_flags(dev, rv == 0, aic7890_seq_flag_carry(dev));
            break;
        case SEQ_OP_AND:
            sv = aic7890_seq_field_read(dev, source);
            rv = sv & imm;
            aic7890_seq_field_write(dev, dest, rv);
            aic7890_seq_set_flags(dev, rv == 0, aic7890_seq_flag_carry(dev));
            break;
        case SEQ_OP_XOR:
            sv = aic7890_seq_field_read(dev, source);
            rv = sv ^ imm;
            aic7890_seq_field_write(dev, dest, rv);
            aic7890_seq_set_flags(dev, rv == 0, aic7890_seq_flag_carry(dev));
            break;
        case SEQ_OP_ADD: {
            int res = aic7890_seq_field_read(dev, source) + imm;
            aic7890_seq_field_write(dev, dest, res & 0xff);
            aic7890_seq_set_flags(dev, (res & 0xff) == 0, res > 0xff);
            break;
        }
        case SEQ_OP_ADC: {
            int res = aic7890_seq_field_read(dev, source) + imm
                    + (aic7890_seq_flag_carry(dev) ? 1 : 0);
            aic7890_seq_field_write(dev, dest, res & 0xff);
            aic7890_seq_set_flags(dev, (res & 0xff) == 0, res > 0xff);
            break;
        }
        case SEQ_OP_ROL: {
            uint8_t sc = imm;
            uint8_t carry = aic7890_seq_flag_carry(dev) ? 1 : 0;

            sv = aic7890_seq_field_read(dev, source);
            if (sc == 0xf0) {
                carry = sv >> 7;
                rv = 0;
            } else if (sc == 0xf8) {
                carry = sv & 1;
                rv = 0;
            } else if (sc & 0x08) {
                int n = (sc >> 4) & 0xf;
                if (n == 0) {            /* ROR */
                    n = 8 - (sc & 7);
                    if (n == 0)
                        n = 8;
                    rv = (sv >> n) | (sv << (8 - n));
                    carry = sv & 1;
                } else {                 /* SHR */
                    rv = (uint8_t) (sv >> n);
                    carry = (sv >> (n - 1)) & 1;
                }
            } else {                     /* ROL / SHL */
                int n = sc & 7;
                if (n == 0)
                    n = 8;
                rv = (uint8_t) ((sv << n) | (sv >> (8 - n)));
                carry = (sv >> (8 - n)) & 1;
            }
            aic7890_seq_field_write(dev, dest, rv);
            aic7890_seq_set_flags(dev, rv == 0, carry != 0);
            break;
        }
        case SEQ_OP_BMOV: {
            int count = imm ? imm : 1;
            uint8_t ea_src = aic7890_seq_effective_addr(dev, source, false);
            uint8_t ea_dst = aic7890_seq_effective_addr(dev, dest, true);

            if (cur_pc == 0x176 || cur_pc == 0x17a)
                aic7890_log(2, "AIC7890: BMOV %03x pc=%03x src=%03x dst=%03x cnt=%d scb3=%02x\n",
                            dev->seq_pc, cur_pc, ea_src, ea_dst, count,
                            dev->scb_ram[dev->regs[REG_SCBPTR]][3]);
            for (int i = 0; i < count; i++) {
                uint8_t v = aic7890_seq_reg_read(dev, (uint8_t) (ea_src + i), 0);
                aic7890_seq_reg_write(dev, (uint8_t) (ea_dst + i), v);
                rv = v;
            }
            aic7890_seq_set_flags(dev, rv == 0, aic7890_seq_flag_carry(dev));
            break;
        }
        case SEQ_OP_MVI16: {
            uint16_t sv16 = (uint16_t) aic7890_seq_field_read(dev, source);
            sv16 |= (uint16_t) aic7890_seq_field_read(dev, source + 1) << 8;
            aic7890_seq_field_write(dev, dest, sv16 & 0xff);
            aic7890_seq_field_write(dev, dest + 1, (sv16 >> 8) & 0xff);
            aic7890_seq_set_flags(dev, sv16 == 0, aic7890_seq_flag_carry(dev));
            break;
        }
        case SEQ_OP_JMP:
        case SEQ_OP_JC:
        case SEQ_OP_JNC:
        case SEQ_OP_CALL:
        case SEQ_OP_JNE:
        case SEQ_OP_JNZ:
        case SEQ_OP_JE:
        case SEQ_OP_JZ: {
            switch (opcode) {
                case SEQ_OP_JMP:
                    taken = true;
                    break;
                case SEQ_OP_CALL:
                    aic7890_seq_call(dev, addr);
                    taken = true;
                    break;
                case SEQ_OP_JC:
                    taken = aic7890_seq_flag_carry(dev) != 0;
                    break;
                case SEQ_OP_JNC:
                    taken = aic7890_seq_flag_carry(dev) == 0;
                    break;
                case SEQ_OP_JNE:
                    taken = aic7890_seq_field_read(dev, source) != imm;
                    break;
                case SEQ_OP_JE:
                    taken = aic7890_seq_field_read(dev, source) == imm;
                    break;
                case SEQ_OP_JNZ:
                    taken = (aic7890_seq_field_read(dev, source) & imm) != 0;
                    break;
                case SEQ_OP_JZ:
                    taken = (aic7890_seq_field_read(dev, source) & imm) == 0;
                    if (cur_pc == 0x08c)
                        aic7890_log(2, "AIC7890: JZ 08c src=%03x val=%02x imm=%02x\n",
                                    source, aic7890_seq_field_read(dev, source), imm);
                    break;
                default:
                    break;
            }
            if (taken && opcode != SEQ_OP_CALL)
                aic7890_seq_jump(dev, addr);
            break;
        }
        default:
            break;
    }

    if (opcode == SEQ_OP_CALL)
        ;                                /* pc already set by call */
    else if (opcode >= SEQ_OP_JMP && opcode <= SEQ_OP_JZ) {
        if (!taken)
            dev->seq_pc = next;
    } else {
        dev->seq_pc = next;
    }

    if (ret) {
        if (!aic7890_seq_ret(dev))
            dev->seq_pc = next;
    }

    dev->seq_steps++;
    if (aic7890_log_level() >= 2) {
        aic7890_log(2,
                    "AIC7890: seq %03x op=%x src=%03x dst=%03x imm=%02x addr=%03x take=%u sp=%d\n",
                    cur_pc, opcode, source, dest, imm, addr, taken, dev->seq_sp);
    }
    return true;
}

/* Run the interpreter from the current seq_pc for up to max_steps
 * instructions.  Returns true if the microcode reached a state where the
 * host expects it to pause (an interrupt was raised). */
static uint32_t
aic7890_seq_max_steps(void)
{
    const char *env = getenv("AIC7890_SEQ_STEPS");

    if (env != NULL && env[0] != '\0') {
        uint32_t v = (uint32_t) strtoul(env, NULL, 10);

        if (v > 0)
            return v;
    }
    return SEQ_MAX_STEPS;
}

static bool
aic7890_seq_run(aic7890_t *dev, uint32_t max_steps)
{
    uint32_t steps = 0;

    if (!dev->seq_active)
        return false;
    if (dev->seqram_writes == 0)
        return false;

    while (steps < max_steps && !(dev->regs[REG_HCNTRL] & HCNTRL_PAUSE)
           && !(dev->regs[REG_INTSTAT] & INTSTAT_INT_PEND)) {
        aic7890_seq_step(dev);
        steps++;
        if (dev->seq_pc >= 0x300)
            break;
    }

    dev->seq_steps += steps;
    if (aic7890_log_level() >= 1)
        aic7890_log(1, "AIC7890: seq run pc=%03x steps=%u intstat=%02x\n",
                    dev->seq_pc, steps, dev->regs[REG_INTSTAT]);
    return true;
}

static void
aic7890_emulate_sequencer_run(aic7890_t *dev)
{
    bool reset_pulse;

    if (dev->regs[REG_HCNTRL] & HCNTRL_PAUSE)
        return;

    aic7890_process_pending(dev);

    reset_pulse = !!(dev->regs[REG_SCSISEQ] & SCSISEQ_SCSIRSTO);
    if (aic7890_seqaddr(dev) == 0x0000
        && !(dev->regs[REG_INTSTAT] & INTSTAT_INT_PEND)
        && !aic7890_has_pending_work(dev)) {
        uint64_t us = dev->scan_init_active ? dev->scan_init_remaining
                                            : aic7890_scan_init_delay();

        /*
         * The BIOS starts the downloaded microcode at address 0x0000
         * (reset vector) to initialize the SCSI bus after a reset.  We do
         * not execute the microcode, but the microcode's init path ends by
         * raising an interrupt shortly after it starts, so arm a short
         * timer that reproduces that interrupt.  Require a loaded program
         * so the earlier power-on unpauses (empty RAM) stay silent.
         */
        if (!dev->scan_init_active && dev->seqram_writes == 0)
            return;
        /*
         * The init interrupt is only expected once per microcode program.
         * The OS driver re-starts the sequencer at 0x0000 repeatedly while
         * scanning, so only re-fire when a (re)loaded program changes.
         */
        if (dev->scan_init_hash != AIC7890_TRACE_HASH_INIT
            && dev->scan_init_hash == dev->seqram_write_hash)
            return;
        dev->scan_init_hash = dev->seqram_write_hash;
        dev->scan_init_active = true;
        timer_set_delay_u64(&dev->scan_init_timer, us * TIMER_USEC);
        aic7890_log(1,
                    "AIC7890: sequencer scan init armed %lluus tsc=%llu ts=%llu seqaddr=%04x reset=%u\n",
                    (unsigned long long) us, (unsigned long long) tsc,
                    (unsigned long long) dev->scan_init_timer.ts_integer,
                    aic7890_seqaddr(dev), reset_pulse);
    }

    if (aic7890_seqaddr(dev) == 0x0002
        && !(dev->regs[REG_INTSTAT] & INTSTAT_INT_PEND)
        && !aic7890_has_pending_work(dev)) {
        /*
         * The BIOS pre-loads SCB tag 2 with the b0/b1 selection-timeout
         * delay counts and starts the downloaded microcode at address
         * 0x0002 (scan-start entry).  Execute the microcode directly.
         */
        if (dev->seqram_writes == 0)
            return;
        dev->seq_pc = aic7890_seqaddr(dev);
        dev->seq_active = true;
        /* Fresh scan2 attempt: re-run the probe from the COMMAND phase. */
        dev->seq_cmd_done = false;
        dev->seq_stage = 0;
        dev->seq_phase = 0x80;      /* COMMAND */
        dev->seq_req_pending = true;
        dev->seq_data_pos = 0;
        dev->seq_has_data = false;
        dev->seq_dma_done = false;
        dev->seq_busfree = false;
        dev->seq_sp = 0;            /* return stack cleared by sequencer re-arm */
        if (aic7890_log_level() >= 1 && dev->seq_steps == 0) {
            for (int i = 0; i < 0x210; i++) {
                uint32_t w = aic7890_seq_word(dev, (uint16_t) i);
                uint32_t pos = (uint32_t) i * 4;

                if (w == 0)
                    continue;
                aic7890_log(1, "AIC7890: seqram dump %03x le=%08x\n",
                            i, w);
            }
        }
        aic7890_log(1,
                    "AIC7890: sequencer scan2 interpret start pc=%03x reset=%u\n",
                    dev->seq_pc, reset_pulse);
        aic7890_seq_run(dev, aic7890_seq_max_steps());
        if (dev->regs[REG_INTSTAT] & INTSTAT_INT_PEND) {
            dev->regs[REG_HCNTRL] |= HCNTRL_PAUSE;
            aic7890_log(1,
                        "AIC7890: scan2 interpret complete pc=%03x intstat=%02x steps=%llu\n",
                        dev->seq_pc, dev->regs[REG_INTSTAT],
                        (unsigned long long) dev->seq_steps);
        }
    }

    if (aic7890_seqaddr(dev) == 0x0004
        && !(dev->regs[REG_HCNTRL] & HCNTRL_INTEN)
        && !(dev->regs[REG_INTSTAT] & INTSTAT_INT_PEND)
        && !aic7890_has_pending_work(dev)) {
        uint64_t us;

        /*
         * The downloaded microcode at the idle address runs a countdown
         * delay (decrementing SCB bytes 0x10/0x11, the BIOS's b0/b1 delay
         * counts) and then raises SEQINT, which pauses the sequencer.
         * We do not execute the microcode, so arm a timer for the same
         * countdown.  The countdown is paused whenever the host re-pauses
         * the sequencer and resumes where it left off, so the BIOS's
         * re-pause/retry loop still lets it complete.
         */
        if (dev->countdown_active) {
            us = dev->countdown_remaining;
        } else {
            uint8_t *scb = dev->scb_ram[dev->regs[REG_SCBPTR]];
            uint32_t b0 = scb[0x10];
            uint32_t b1 = scb[0x11];
            uint32_t outer = (b1 == 0xff) ? 256 : ((b1 + 1) & 0xff);
            uint32_t iters = ((b0 == 0) ? 256 : b0) + (outer - 1) * 256;

            us = ((uint64_t) iters * 100) / 1000 + 1;
            if (us > aic7890_countdown_cap_us())
                us = aic7890_countdown_cap_us();
            dev->countdown_active = true;
        }
        timer_set_delay_u64(&dev->countdown_timer, us * TIMER_USEC);
        aic7890_log(1,
                    "AIC7890: sequencer idle countdown armed %lluus seqaddr=%04x reset=%u\n",
                    (unsigned long long) us, aic7890_seqaddr(dev), reset_pulse);
    }
}

static void
aic7890_countdown_callback(void *priv)
{
    aic7890_t *dev = priv;
    uint8_t *scb;
    bool reset_pulse;

    if (dev->regs[REG_HCNTRL] & HCNTRL_PAUSE)
        return;
    if (dev->regs[REG_INTSTAT] & INTSTAT_INT_PEND)
        return;

    scb = dev->scb_ram[dev->regs[REG_SCBPTR]];
    reset_pulse = !!(dev->regs[REG_SCSISEQ] & SCSISEQ_SCSIRSTO);
    dev->countdown_active = false;

    /*
     * The countdown completed: the delay-loop's SCB bytes end at 0x00/0xff
     * (the BIOS reads them back to verify the sequencer ran), SCSIRSTO is
     * cleared by the sequencer's reset handler, and SEQINT is raised which
     * pauses the sequencer.
     */
    scb[0x10] = 0x00;
    scb[0x11] = 0xff;
    if (reset_pulse)
        dev->regs[REG_SCSISEQ] &= ~SCSISEQ_SCSIRSTO;
    dev->regs[REG_INTSTAT] |= INTSTAT_SEQINT;
    dev->regs[REG_HCNTRL] |= HCNTRL_PAUSE;
    aic7890_log(1,
                "AIC7890: sequencer idle countdown complete seqaddr=%04x reset=%u intstat=%02x\n",
                aic7890_seqaddr(dev), reset_pulse, dev->regs[REG_INTSTAT]);
    aic7890_update_irq(dev);
}

static void
aic7890_scan_init_callback(void *priv)
{
    aic7890_t *dev = priv;

    if (dev->regs[REG_HCNTRL] & HCNTRL_PAUSE)
        return;
    if (dev->regs[REG_INTSTAT] & INTSTAT_INT_PEND)
        return;

    dev->scan_init_active = false;

    /*
     * The scan-init microcode at address 0x0000 finishes its setup and
     * raises an interrupt, which pauses the sequencer.  Reproduce that
     * interrupt here.
     */
    dev->regs[REG_INTSTAT] |= aic7890_scan_init_interrupt();
    if (aic7890_scan_init_pause())
        dev->regs[REG_HCNTRL] |= HCNTRL_PAUSE;
    aic7890_log(1,
                "AIC7890: sequencer scan init complete seqaddr=%04x intstat=%02x tsc=%llu\n",
                aic7890_seqaddr(dev), dev->regs[REG_INTSTAT],
                (unsigned long long) tsc);
    aic7890_update_irq(dev);
}

static void
aic7890_scan2_callback(void *priv)
{
    aic7890_t *dev = priv;
    uint8_t target;
    bool present;

    if (dev->regs[REG_HCNTRL] & HCNTRL_PAUSE)
        return;
    if (dev->regs[REG_INTSTAT] & INTSTAT_INT_PEND)
        return;

    dev->scan2_active = false;

    /*
     * The scan-start countdown finished: mark the SCB tag 2 delay bytes
     * as completed (b0 -> 0x00, b1 -> 0xff) and report the selection
     * result.  The BIOS enabled SCSI interrupts (SIMODE) and waits for
     * the countdown wrap (target responded) or SELTO (target absent).
     */
    dev->scb_ram[2][0x10] = 0x00;
    dev->scb_ram[2][0x11] = 0xff;

    target = (dev->regs[REG_SCSIID] >> 4) & 0x0f;
    present = dev->scsi_bus < SCSI_BUS_MAX && target < SCSI_ID_MAX
           && scsi_device_present(&scsi_devices[dev->scsi_bus][target]);
    if (aic7890_scan2_interrupt() != 0) {
        dev->regs[REG_INTSTAT] |= (uint8_t) aic7890_scan2_interrupt();
    } else if (present) {
        dev->regs[REG_CLRSINT0_SSTAT0] |= SSTAT0_SWRAP | SSTAT0_SELDO;
        dev->regs[REG_INTSTAT] |= INTSTAT_SCSIINT;
    } else {
        dev->regs[REG_CLRSINT1_SSTAT1] |= SSTAT1_SELTO;
        dev->regs[REG_INTSTAT] |= INTSTAT_SCSIINT;
    }
    dev->regs[REG_HCNTRL] |= HCNTRL_PAUSE;
    aic7890_log(1,
                "AIC7890: scan2 selection complete target=%u present=%u intstat=%02x sstat0=%02x sstat1=%02x\n",
                target, present, dev->regs[REG_INTSTAT],
                dev->regs[REG_CLRSINT0_SSTAT0],
                dev->regs[REG_CLRSINT1_SSTAT1]);
    aic7890_update_irq(dev);
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

            if (dev->regs[REG_SEECTL] & SEECTL_SEEMS) {
                dev->regs[REG_SEECTL] &= ~(SEECTL_SEEMS | SEECTL_EXTARBREQ);
                if (!(ret & SEECTL_SEEMS))
                    ret |= SEECTL_SEEMS;
            }
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
                                      | SSTAT0_SWRAP | SSTAT0_DMADONE));
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
        {
            uint8_t new_val = val & ~(SEECTL_SEERDY | SEECTL_SEEDI | SEECTL_EXTARBACK);

            if (!(dev->regs[reg] & SEECTL_SEEMS) && (new_val & SEECTL_SEEMS))
                new_val |= SEECTL_EXTARBREQ;

            dev->regs[reg] = new_val;
            nmc93cxx_eeprom_write(dev->eeprom, !!(val & SEECTL_SEECS),
                                  !!(val & SEECTL_SEECK), !!(val & SEECTL_SEEDO));
            return;
        }

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
            /*
              * Rewriting the sequencer PC restarts the downloaded microcode,
              * so any in-progress idle countdown or scan init starts over on
              * the next unpause.
              */
             dev->countdown_active = false;
             timer_disable(&dev->countdown_timer);
             dev->scan_init_active = false;
             timer_disable(&dev->scan_init_timer);
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
            else if (!(old_hcntrl & HCNTRL_PAUSE) && (dev->regs[REG_HCNTRL] & HCNTRL_PAUSE)) {
                aic7890_log(1, "AIC7890: sequencer paused hcntrl=%02x\n",
                            dev->regs[REG_HCNTRL]);
                if (timer_is_enabled(&dev->countdown_timer))
                    dev->countdown_remaining = timer_get_remaining_us(&dev->countdown_timer);
                timer_disable(&dev->countdown_timer);
                if (timer_is_enabled(&dev->scan_init_timer))
                    dev->scan_init_remaining = timer_get_remaining_us(&dev->scan_init_timer);
                timer_disable(&dev->scan_init_timer);
                if (timer_is_enabled(&dev->scan2_timer))
                    dev->scan2_remaining = timer_get_remaining_us(&dev->scan2_timer);
                timer_disable(&dev->scan2_timer);
            }
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
                dev->regs[REG_INTSTAT] &= ~(INTSTAT_SEQINT | 0xf0);
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

    timer_add(&dev->countdown_timer, aic7890_countdown_callback, dev, 0);
    timer_add(&dev->scan_init_timer, aic7890_scan_init_callback, dev, 0);
    timer_add(&dev->scan2_timer, aic7890_scan2_callback, dev, 0);

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
