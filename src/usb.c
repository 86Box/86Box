/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Universal Serial Bus host controllers: a UHCI and an OpenHCI
 *          (OHCI) controller with their root hubs, written from:
 *            Intel, Universal Host Controller Interface (UHCI) Design
 *              Guide, Revision 1.1, March 1996;
 *            Compaq, Microsoft, National Semiconductor, OpenHCI: Open
 *              Host Controller Interface Specification for USB,
 *              Release 1.0a, September 14, 1999;
 *            Universal Serial Bus Specification, Revision 1.1,
 *              September 23, 1998;
 *            Intel 82371AB PCI-to-ISA/IDE Xcelerator (PIIX4) datasheet,
 *              order number 290562-001, April 1997;
 *            VIA VT82C686B datasheet, Revision 1.8, August 1, 2000;
 *            ALi M1543C Preliminary Datasheet, Version 1.10;
 *            SiS 5595 PCI System I/O Chipset datasheet, Preliminary
 *              Rev. 2.0, November 2, 1998;
 *            SMSC SLC90E66 Victory66 datasheet, Rev. 07/10/2002;
 *            STMicroelectronics STPC Atlas Programming Manual, Issue 1.0,
 *              July 2, 2002;
 *            OPTi FireLink 82C861 PCI-to-USB Bridge Data Book,
 *              912-2000-049 Revision 1.0, May 1998.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          skiretic, <skiretic@proton.me>
 *
 *          Copyright 2020 Miran Grca.
 *          Copyright 2026 skiretic.
 */
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/usb.h>
#include <86box/usb_dev.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include "cpu.h"
#include <86box/plat_unused.h>

/* UHCI I/O registers and their bits (UHCI 1.1 Table 2 and s2.1, p. 10-17 /
   PDF 16-23). */
#define UHCI_USBCMD    0x00
#define UHCI_USBSTS    0x02
#define UHCI_USBINTR   0x04
#define UHCI_FRNUM     0x06
#define UHCI_FLBASEADD 0x08
#define UHCI_SOFMOD    0x0c
#define UHCI_PORTSC1   0x10
#define UHCI_PORTSC2   0x12

#define UHCI_CMD_RS      0x0001
#define UHCI_CMD_HCRESET 0x0002
#define UHCI_CMD_GRESET  0x0004
#define UHCI_CMD_EGSM    0x0008
#define UHCI_CMD_FGR     0x0010
#define UHCI_CMD_SWDBG   0x0020
#define UHCI_CMD_CF      0x0040
#define UHCI_CMD_MAXP    0x0080

#define UHCI_STS_USBINT   0x0001
#define UHCI_STS_ERRINT   0x0002
#define UHCI_STS_RD       0x0004
#define UHCI_STS_HSE      0x0008
#define UHCI_STS_HCPE     0x0010
#define UHCI_STS_HCHALTED 0x0020

#define UHCI_INTR_TOCRC  0x0001
#define UHCI_INTR_RESUME 0x0002
#define UHCI_INTR_IOC    0x0004
#define UHCI_INTR_SP     0x0008

#define UHCI_PORT_CCS  0x0001
#define UHCI_PORT_CSC  0x0002
#define UHCI_PORT_PE   0x0004
#define UHCI_PORT_PEDC 0x0008
#define UHCI_PORT_DP   0x0010
#define UHCI_PORT_DM   0x0020
#define UHCI_PORT_RD   0x0040
#define UHCI_PORT_ONE  0x0080
#define UHCI_PORT_LSDA 0x0100
#define UHCI_PORT_PR   0x0200
#define UHCI_PORT_SUSP 0x1000

#define UHCI_PORTS 2

/* Link pointer bits of frame list entries, TDs and QHs: T = terminate,
   Q = points to a QH, Vf = depth first (UHCI 1.1 s3.1-3.3, p. 20-26 /
   PDF 26-32). */
#define UHCI_LP_T  0x00000001
#define UHCI_LP_Q  0x00000002
#define UHCI_LP_VF 0x00000004

/* TD control and status, dword 1 (UHCI 1.1 s3.2.2, p. 22-23 / PDF 28-29). */
#define UHCI_TD_ACTLEN   0x000007ff
#define UHCI_TD_BITSTUFF 0x00020000
#define UHCI_TD_CRCTO    0x00040000
#define UHCI_TD_NAK      0x00080000
#define UHCI_TD_BABBLE   0x00100000
#define UHCI_TD_DBE      0x00200000
#define UHCI_TD_STALLED  0x00400000
#define UHCI_TD_ACTIVE   0x00800000
#define UHCI_TD_IOC      0x01000000
#define UHCI_TD_IOS      0x02000000
#define UHCI_TD_LS       0x04000000
#define UHCI_TD_CERR     0x18000000
#define UHCI_TD_SPD      0x20000000
#define UHCI_TD_STATUS   0x00fe0000

typedef struct uhci_port_t {
    uint16_t       sc;  /* CCS, CSC, PE, PEDC, RD, LSDA, PR, SUSP */
    usb_dev_t     *usb; /* the device on the wire, if any */
    uint8_t        eop;  /* RD written 1 -> 0: the low-speed EOP that ends the
                            resume is on the wire, and RD reads 1 until it ends */
    uint16_t       line; /* Line Status, bits 5:4, as sampled at the last EOF2 */
    pc_timer_t     eop_timer;
    struct uhci_t *hc;
    int            num;
} uhci_port_t;

struct uhci_t {
    uint16_t    cmd;
    uint16_t    sts;
    uint16_t    intr;
    uint16_t    frnum;
    uint32_t    flbaseadd;
    uint8_t     sofmod;
    uint8_t     int_ioc; /* sources behind USBSTS.USBINT, each with its own enable */
    uint8_t     int_spd;
    uint8_t     irq_level;
    uint8_t     frame_frozen;
    uint8_t     bus_master; /* PCICMD BME from the chipset */
    /* Where the schedule walk stands in the current frame; a stop in debug
       mode resumes there. */
    uint8_t     dbg_resume;
    uint32_t    dbg_link;
    double      dbg_left;
    double      frame_left_us;
    /* VIA USB function Rx40 bit 6 (Babble Option): when set, a port that
       babbles at EOF is not disabled (VT82C686B, p. 76 / PDF 82). */
    uint8_t     babble_keep_port;
    /* Completions in the frame in progress. They reach USBSTS only at the
       end of the frame (UHCI 1.1 s4, p. 35 / PDF 41). */
    uint8_t     eof_ioc;
    uint8_t     eof_spd;
    uint8_t     eof_err;
    uhci_port_t port[UHCI_PORTS];
    pc_timer_t  frame_timer;
    pc_timer_t  detect_timer;
    uint8_t     fgr_eop; /* FGR written 1 -> 0: the low-speed EOP that ends the
                            global resume is on the wire, and FGR reads 1 until
                            it ends */
    pc_timer_t  fgr_timer;
    void      (*set_irq)(void *priv, int level);
    void       *irq_priv;
};

/* OHCI operational registers and their bits (OpenHCI 1.0a s7, p. 108-131 /
   PDF 122-145), and the legacy support registers at 100h (Appendix B). */
#define OHCI_HCREVISION         0x000
#define OHCI_HCCONTROL          0x004
#define OHCI_HCCOMMANDSTATUS    0x008
#define OHCI_HCINTERRUPTSTATUS  0x00c
#define OHCI_HCINTERRUPTENABLE  0x010
#define OHCI_HCINTERRUPTDISABLE 0x014
#define OHCI_HCHCCA             0x018
#define OHCI_HCPERIODCURRENTED  0x01c
#define OHCI_HCCONTROLHEADED    0x020
#define OHCI_HCCONTROLCURRENTED 0x024
#define OHCI_HCBULKHEADED       0x028
#define OHCI_HCBULKCURRENTED    0x02c
#define OHCI_HCDONEHEAD         0x030
#define OHCI_HCFMINTERVAL       0x034
#define OHCI_HCFMREMAINING      0x038
#define OHCI_HCFMNUMBER         0x03c
#define OHCI_HCPERIODICSTART    0x040
#define OHCI_HCLSTHRESHOLD      0x044
#define OHCI_HCRHDESCRIPTORA    0x048
#define OHCI_HCRHDESCRIPTORB    0x04c
#define OHCI_HCRHSTATUS         0x050
#define OHCI_HCRHPORTSTATUS1    0x054
#define OHCI_HCECONTROL         0x100
#define OHCI_HCEINPUT           0x104
#define OHCI_HCEOUTPUT          0x108
#define OHCI_HCESTATUS          0x10c

#define OHCI_CTL_CBSR 0x00000003
#define OHCI_CTL_PLE  0x00000004
#define OHCI_CTL_IE   0x00000008
#define OHCI_CTL_CLE  0x00000010
#define OHCI_CTL_BLE  0x00000020
#define OHCI_CTL_HCFS 0x000000c0
#define OHCI_CTL_IR   0x00000100
#define OHCI_CTL_RWC  0x00000200
#define OHCI_CTL_RWE  0x00000400

#define OHCI_HCFS_RESET       0x00000000
#define OHCI_HCFS_RESUME      0x00000040
#define OHCI_HCFS_OPERATIONAL 0x00000080
#define OHCI_HCFS_SUSPEND     0x000000c0

#define OHCI_CS_HCR 0x00000001
#define OHCI_CS_CLF 0x00000002
#define OHCI_CS_BLF 0x00000004
#define OHCI_CS_OCR 0x00000008
#define OHCI_CS_SOC 0x00030000

#define OHCI_INT_SO   0x00000001
#define OHCI_INT_WDH  0x00000002
#define OHCI_INT_SF   0x00000004
#define OHCI_INT_RD   0x00000008
#define OHCI_INT_UE   0x00000010
#define OHCI_INT_FNO  0x00000020
#define OHCI_INT_RHSC 0x00000040
#define OHCI_INT_OC   0x40000000
#define OHCI_INT_MIE  0x80000000
#define OHCI_INT_ALL  0x4000007f

/* HcRhStatus (OpenHCI 1.0a s7.4.3, p. 126-127 / PDF 140-141). Some bits
   mean one thing on a read and another on a write of 1; the comment names
   the write command. */
#define OHCI_RH_LPS  0x00000001 /* ClearGlobalPower */
#define OHCI_RH_OCI  0x00000002
#define OHCI_RH_DRWE 0x00008000 /* SetRemoteWakeupEnable */
#define OHCI_RH_LPSC 0x00010000 /* SetGlobalPower */
#define OHCI_RH_OCIC 0x00020000
#define OHCI_RH_CRWE 0x80000000

/* HcRhPortStatus (OpenHCI 1.0a s7.4.4, p. 128-131 / PDF 142-145). As in
   HcRhStatus, the comment names the command a write of 1 gives. */
#define OHCI_PS_CCS    0x00000001 /* ClearPortEnable */
#define OHCI_PS_PES    0x00000002 /* SetPortEnable */
#define OHCI_PS_PSS    0x00000004 /* SetPortSuspend */
#define OHCI_PS_POCI   0x00000008 /* ClearSuspendStatus */
#define OHCI_PS_PRS    0x00000010 /* SetPortReset */
#define OHCI_PS_PPS    0x00000100 /* SetPortPower */
#define OHCI_PS_LSDA   0x00000200 /* ClearPortPower */
#define OHCI_PS_CSC    0x00010000
#define OHCI_PS_PESC   0x00020000
#define OHCI_PS_PSSC   0x00040000
#define OHCI_PS_OCIC   0x00080000
#define OHCI_PS_PRSC   0x00100000
#define OHCI_PS_CHANGE 0x001f0000

#define OHCI_DA_PSM 0x00000100
#define OHCI_DA_NPS 0x00000200

/* PRSC is set at the end of the 10 ms port reset; PSSC at the end of the
   20 ms resume pulse, the low-speed EOP and the 3 ms resynchronization
   delay (OpenHCI 1.0a s7.4.4, p. 131 / PDF 145). The EOP's 2 us is left
   out. */
#define OHCI_PORT_RESET_US  10000.0
#define OHCI_PORT_RESUME_US 23000.0

enum {
    OHCI_PORT_TIMER_NONE = 0,
    OHCI_PORT_TIMER_RESET,
    OHCI_PORT_TIMER_RESUME
};

/* Endpoint Descriptor dwords 0 and 2 (OpenHCI 1.0a s4.2, p. 16-17 /
   PDF 30-31) and General TD dword 0 (s4.3.1, p. 20-21 / PDF 34-35). */
#define OHCI_ED_S     0x00002000
#define OHCI_ED_K     0x00004000
#define OHCI_ED_F     0x00008000
#define OHCI_ED_H     0x00000001
#define OHCI_ED_C     0x00000002
#define OHCI_TD_R     0x00040000
#define OHCI_TD_DI    0x00e00000
#define OHCI_TD_T     0x03000000
#define OHCI_TD_EC    0x0c000000
#define OHCI_TD_CC    0xf0000000

#define OHCI_CC_NOERROR             0x0
#define OHCI_CC_DATATOGGLEMISMATCH  0x3
#define OHCI_CC_STALL               0x4
#define OHCI_CC_DEVICENOTRESPONDING 0x5
#define OHCI_CC_DATAOVERRUN         0x8
#define OHCI_CC_DATAUNDERRUN        0x9

#define OHCI_LIST_CONTROL 0
#define OHCI_LIST_BULK    1

/* Bus time charged for one descriptor fetch: 3 bit times (250 ns), about
   one PCI burst read. The UHCI walk charges the same per fetch. */
#define OHCI_FETCH_BITS 3.0

/* The SOF token: SYNC, PID, 11-bit frame number and CRC5 make 32 bits,
   then a 3-bit EOP (USB 1.1 s8.2, p. 155 / PDF 171; s8.4.2, p. 159 /
   PDF 175; Table 7-1, p. 116 / PDF 132). Bit stuffing is not counted. */
#define OHCI_SOF_BITS 35.0

/* FrameInterval resets to 11,999 and holds frame length - 1, so a frame is
   12,000 bit times (OpenHCI 1.0a s6.3.1, p. 90 / PDF 104). */
#define OHCI_FI_NOMINAL 0x00002edf

/* HceControl and HceStatus (OpenHCI 1.0a Appendix B, Tables B-6 and B-7,
   p. 144-145 / PDF 158-159). */
#define OHCI_HCE_EE        0x00000001
#define OHCI_HCE_EI        0x00000002
#define OHCI_HCE_CP        0x00000004
#define OHCI_HCE_EXTIRQEN  0x00000010
#define OHCI_HCE_IRQ1ACT   0x00000040
#define OHCI_HCE_IRQ12ACT  0x00000080
#define OHCI_HCES_OUTFULL  0x01
#define OHCI_HCES_INFULL   0x02

/* Room for the ALi M5237 with its third port enabled (ALi M1543C
   datasheet, HcRhDescriptorA, p. 184 / PDF 185). */
#define OHCI_MAX_PORTS 3

typedef struct ohci_port_t {
    uint32_t   sts; /* HcRhPortStatus: status in bits 15:0, change in bits 31:16.
                       PPS holds the switched power state; with NoPowerSwitching
                       set the port reads powered whatever PPS holds. */
    uint8_t    timer_op;
    int        num;
    usb_dev_t *usb; /* the device on the wire, if any */
    ohci_t    *hc;
    pc_timer_t timer;
} ohci_port_t;

struct ohci_t {
    uint32_t    control;
    uint32_t    cmdsts;
    uint32_t    intsts;
    uint32_t    inten;
    uint32_t    hcca;
    uint32_t    period_cur;
    uint32_t    ctrl_head;
    uint32_t    ctrl_cur;
    uint32_t    bulk_head;
    uint32_t    bulk_cur;
    uint32_t    done_head;
    uint32_t    fm_interval;
    uint32_t    fm_remaining;
    uint32_t    fm_number;
    uint32_t    periodic_start;
    uint32_t    ls_threshold;
    uint32_t    rh_desc_a;
    uint32_t    rh_desc_b;
    uint32_t    rh_status;
    int         ndp;
    int         ports; /* connectors on the board; a device plugged in past
                          NDP stays unseen until the chipset raises NDP */
    ohci_port_t port[OHCI_MAX_PORTS];
    uint8_t     bus_master; /* PCI COMMAND Bus Master Enable from the chipset */
    uint8_t     bus_master_hardwired; /* the chip ties Bus Master Enable to 1,
                                         so bus_master stays 1 through resets */
    uint8_t     mem_access;           /* PCI COMMAND Memory Access, passed in with
                                         the window; 0 holds INT and SMI
                                         inactive */
    uint32_t    chip_flags;           /* OHCI_CHIP_* */
    /* The frame in progress: the value FrameRemaining counted down from, and
       FrameNumber bit 15 as of the last HCCA update. */
    uint32_t    fr_load;
    uint32_t    fn_msb;
    pc_timer_t  frame_timer;
    /* The root hub waits 5 ms after entering USBSUSPEND before it may raise
       a wakeup; a resume event in that window is held until it ends
       (OpenHCI 1.0a s6.2.3, p. 89 / PDF 103). */
    pc_timer_t  suspend_timer;
    uint8_t     suspend_settling;
    uint8_t     resume_held;
    /* List processing: bit times left in the frame being run, the next
       nonperiodic list and the control EDs served since the last bulk ED
       (carried across frames), and whether a list still waits for its first
       service since entering USBOPERATIONAL. */
    double      bits_left;
    int         np_list;
    int         ctrl_served;
    uint8_t     np_first[2];
    uint8_t     so_pending; /* the periodic list overran: SO at the next HCCA update */
    uint8_t     di_count;   /* Done Queue Interrupt Counter, 7 = no writeback due */
    void      (*set_irq)(void *priv, int level);
    void       *irq_priv;
    void      (*set_smi)(void *priv, int level);
    void       *smi_priv;
    uint8_t     int_level; /* the INT and SMI outputs as last driven */
    uint8_t     smi_level;
    /* Legacy support registers (OpenHCI 1.0a Appendix B, p. 136-145 /
       PDF 150-159). Only the registers are modeled; ports 60h and 64h are
       not decoded. */
    uint32_t    hce_control;
    uint8_t     hce_input;
    uint8_t     hce_output;
    uint8_t     hce_status;
};

static void
uhci_update_irq(uhci_t *dev)
{
    int level = 0;

    /* Host Controller Process Error and Host System Error cannot be masked
       (UHCI 1.1 s4.2.2-4.2.3, p. 37 / PDF 43). */
    if (dev->sts & (UHCI_STS_HCPE | UHCI_STS_HSE))
        level = 1;
    if ((dev->sts & UHCI_STS_RD) && (dev->intr & UHCI_INTR_RESUME))
        level = 1;
    if ((dev->sts & UHCI_STS_ERRINT) && (dev->intr & UHCI_INTR_TOCRC))
        level = 1;
    if ((dev->sts & UHCI_STS_USBINT) && ((dev->int_ioc && (dev->intr & UHCI_INTR_IOC)) || (dev->int_spd && (dev->intr & UHCI_INTR_SP))))
        level = 1;

    if (level == dev->irq_level)
        return;

    dev->irq_level = level;
    if (dev->set_irq != NULL)
        dev->set_irq(dev->irq_priv, level);
}

static void
uhci_set_halted(uhci_t *dev, int halted)
{
    if (halted)
        dev->sts |= UHCI_STS_HCHALTED;
    else
        dev->sts &= ~UHCI_STS_HCHALTED;
}

static double
uhci_frame_us(const uhci_t *dev)
{
    /* A frame is 11936 + SOFMOD[6:0] clocks of 12 MHz (UHCI 1.1 s2.1.6,
       p. 15 / PDF 21). */
    return (11936.0 + (double) (dev->sofmod & 0x7f)) / 12.0;
}

static int
uhci_running(const uhci_t *dev)
{
    return (dev->cmd & UHCI_CMD_RS) && !(dev->cmd & (UHCI_CMD_EGSM | UHCI_CMD_GRESET));
}

/* IOC, short packet and error completions of a frame reach USBSTS at the
   end of that frame (UHCI 1.1 s4.1.1-4.1.3, p. 35-36 / PDF 41-42). */
static void
uhci_latch_frame_status(uhci_t *dev)
{
    if (dev->eof_ioc) {
        dev->sts |= UHCI_STS_USBINT;
        dev->int_ioc = 1;
    }
    if (dev->eof_spd) {
        dev->sts |= UHCI_STS_USBINT;
        dev->int_spd = 1;
    }
    if (dev->eof_err)
        dev->sts |= UHCI_STS_ERRINT;

    dev->eof_ioc = dev->eof_spd = dev->eof_err = 0;

    /* IOC and short packet share USBINT but have separate enables, so a
       source latched behind a USBINT already set can raise the line while
       USBSTS itself does not change. */
    uhci_update_irq(dev);
}

static void uhci_run_frame(uhci_t *dev);
static void uhci_frame_end(void *priv);
static void uhci_latch_lines(uhci_t *dev);

/* Called after every USBCMD change: starts or stops the frame clock. */
static void
uhci_update_frame_clock(uhci_t *dev, int was_running)
{
    int running = uhci_running(dev);

    if (running == was_running)
        return;

    if (running) {
        int at_eof = 0;

        /* Stopped, Line Status reads the lines live; running, it reads the
           value sampled at the last EOF2 (UHCI 1.1 s2.1.7, p. 16 / PDF 22).
           Sample now so that reads before the first EOF2 are current. */
        uhci_latch_lines(dev);

        /* A frozen frame with no time left ends at once on restart; a timer
           armed with a zero period would not fire. */
        if (dev->frame_frozen && (dev->cmd & UHCI_CMD_SWDBG) && (dev->frame_left_us > 0.0))
            timer_on_auto(&dev->frame_timer, dev->frame_left_us);
        else if (dev->frame_frozen && (dev->cmd & UHCI_CMD_SWDBG))
            at_eof = 1;
        else {
            timer_on_auto(&dev->frame_timer, uhci_frame_us(dev));
            dev->dbg_resume = 0;
        }
        dev->frame_frozen = 0;
        if (at_eof)
            uhci_frame_end(dev);
        else
            uhci_run_frame(dev);
    } else {
        /* Outside debug mode a stop resets the SOF counter. In debug mode
           the counter freezes, and the schedule walk later resumes where it
           stopped, whether software or the controller cleared Run/Stop
           (UHCI 1.1 Table 4 and s2.1.1.1, p. 12-13 / PDF 18-19). The walk
           runs all at once when the frame starts, so by the time it stops
           it has used bus time the timer has not counted yet; the frozen
           frame keeps the smaller of the two remainders. */
        dev->frame_frozen  = !!(dev->cmd & UHCI_CMD_SWDBG);
        dev->dbg_resume    = dev->frame_frozen;
        dev->frame_left_us = (double) timer_get_remaining_u64(&dev->frame_timer) / (double) TIMER_USEC;
        if (dev->frame_frozen && ((dev->dbg_left / 1000.0) < dev->frame_left_us))
            dev->frame_left_us = (dev->dbg_left > 0.0) ? (dev->dbg_left / 1000.0) : 0.0;
        timer_stop(&dev->frame_timer);
        /* Completions reach USBSTS only at the end of their frame (UHCI 1.1
           s4, p. 35 / PDF 41). A frozen frame still ends after the restart.
           Any other stopped frame is dropped along with the SOF counter
           (s2.1.1.1, p. 13 / PDF 19), and its pending completions with it. */
        if (!dev->frame_frozen)
            dev->eof_ioc = dev->eof_spd = dev->eof_err = 0;
    }
}

/* The controller clears Run/Stop itself: here on a process error or after a
   debug single step (UHCI 1.1 Table 4, p. 12 / PDF 18; Table 7, p. 30 /
   PDF 36). */
static void
uhci_self_stop(uhci_t *dev, uint16_t sts_bit)
{
    int was_running = uhci_running(dev);

    dev->cmd &= ~UHCI_CMD_RS;
    if (sts_bit != 0) {
        dev->sts |= sts_bit;
    }
    uhci_set_halted(dev, 1);
    uhci_update_frame_clock(dev, was_running);
    uhci_update_irq(dev);
}

/* Bus time in ns of one transaction whose data packet takes the given
   number of bits, from the equations in USB 1.1 s5.9.3 (p. 54 / PDF 70),
   with Host_Delay 0 and Hub_LS_Setup at its minimum of four full-speed bit
   times (333 ns). */
static double
uhci_bits_ns(int low_speed, int iso, int in, double bits)
{
    if (low_speed)
        return (in ? 64060.0 : 64107.0) + (2.0 * 333.0) + ((in ? 676.67 : 667.0) * bits);
    if (iso)
        return (in ? 7268.0 : 6265.0) + (83.54 * bits);

    return 9107.0 + (83.54 * bits);
}

/* The same with the worst-case bit stuffing of USB 1.1 s5.9.3: the most a
   packet of that size can take. Used for the preSOF check and for data not
   yet known. */
static double
uhci_bus_ns(int low_speed, int iso, int in, int bytes)
{
    return uhci_bits_ns(low_speed, iso, in, floor(3.167 + (1.1667 * 8.0 * (double) bytes)));
}

/* Bit stuffing inserts a 0 after six 1s in a row. The count starts with
   the 1 that ends the SYNC and runs through the PID (DATA0 C3h, DATA1 4Bh),
   the data and the CRC16 (USB 1.1 s7.1.9, p. 124 / PDF 140). The CRC16 is
   x^16 + x^15 + x^2 + 1, seeded with ones and sent inverted, MSb of the
   remainder first (s8.3.5, p. 158 / PDF 174). The register here is kept
   bit-reversed (A001h), so its low bit is that MSb and goes out first.
   The state after each data byte describes the packet as if it ended
   there. */
typedef struct uhci_stuff_t {
    int      run;
    int      stuffed;
    uint16_t crc;
} uhci_stuff_t;

static void
uhci_stuff_byte(uhci_stuff_t *s, uint8_t byte)
{
    for (int b = 0; b < 8; b++) {
        if (!(byte & (1 << b)))
            s->run = 0;
        else if (++s->run == 6) {
            s->stuffed++;
            s->run = 0;
        }
    }
}

static void
uhci_stuff_start(uhci_stuff_t *s, int toggle)
{
    s->run     = 1;
    s->stuffed = 0;
    s->crc     = 0xffff;
    uhci_stuff_byte(s, toggle ? 0x4b : 0xc3);
}

static void
uhci_stuff_data(uhci_stuff_t *s, uint8_t byte)
{
    s->crc ^= byte;
    for (int b = 0; b < 8; b++)
        s->crc = (s->crc & 1) ? ((s->crc >> 1) ^ 0xa001) : (s->crc >> 1);
    uhci_stuff_byte(s, byte);
}

/* Stuffed bits of a packet that ends here, counting the CRC16 that would
   follow. */
static int
uhci_stuff_end(const uhci_stuff_t *s)
{
    uhci_stuff_t e   = *s;
    uint16_t     crc = ~e.crc;

    uhci_stuff_byte(&e, crc & 0xff);
    uhci_stuff_byte(&e, crc >> 8);
    return e.stuffed;
}

/* Bus time of a transaction whose data packet is known: USB 1.1 s5.9.3
   with the packet's own stuffed bits in place of the worst case. */
static double
uhci_data_ns(int low_speed, int iso, int in, int toggle, const uint8_t *data, int n)
{
    uhci_stuff_t s;

    uhci_stuff_start(&s, toggle);
    for (int i = 0; i < n; i++)
        uhci_stuff_data(&s, data[i]);

    return uhci_bits_ns(low_speed, iso, in, (8.0 * n) + uhci_stuff_end(&s));
}

/* The most bytes of a known packet whose transaction ends within left ns:
   n if it all does, -1 if not even an empty one does. */
static int
uhci_data_fit(int low_speed, int iso, int toggle, const uint8_t *data, int n, double left)
{
    uhci_stuff_t s;
    int          fit = -1;

    uhci_stuff_start(&s, toggle);
    for (int k = 0; k <= n; k++) {
        if (uhci_bits_ns(low_speed, iso, 1, (8.0 * k) + uhci_stuff_end(&s)) > left)
            break;
        fit = k;
        if (k < n)
            uhci_stuff_data(&s, data[k]);
    }

    return fit;
}

/* EOF2 is 10 full-speed bit times before the SOF. A port still carrying
   upstream traffic there is disabled as a babbler (USB 1.1 s11.2.2 and
   Table 11-1, p. 234-235 / PDF 250-251; s11.8.1, p. 251 / PDF 267), so a
   transaction that a device ends must end by EOF2. An isochronous OUT is
   all the host's own packet and only has to end by the SOF (UHCI 1.1
   s1.1.1, p. 4 / PDF 10). */
#define UHCI_EOF2_NS (10.0 * 1000.0 / 12.0)

/* EOF1 is 32 full-speed bit times before the SOF. A hub ends any upstream
   packet still running there with an EOP, and the truncated packet is
   discarded (USB 1.1 Table 11-1 and s11.3.2, p. 235 / PDF 251), so a
   packet from a device behind a hub arrives whole only if it ends by
   EOF1. */
#define UHCI_EOF1_NS (32.0 * 1000.0 / 12.0)

/* The most data bytes, up to len, of a transaction that ends within left
   ns; -1 if not even an empty one does. */
static int
uhci_bytes_before_eof(int low_speed, int iso, int in, int len, double left)
{
    while ((len >= 0) && (uhci_bus_ns(low_speed, iso, in, len) > left))
        len--;

    return len;
}

/* Wire time in ns of an IN answered by a handshake, or by nothing (the
   host's timeout wait): a 34-bit token, 18 bits of turnaround, a 16-bit
   handshake and its 3-bit EOP (USB 1.1 s11.3.3, p. 236 / PDF 252). A
   low-speed bit is 8 full-speed bits. While any enabled root port has a
   full-speed device, a low-speed token goes out behind a preamble (UHCI
   1.1 s3.2.2 bit 26, p. 22 / PDF 28), which adds 20 full-speed bit
   times. */
static double
uhci_in_nodata_ns(const uhci_t *dev, int low_speed, int handshake)
{
    double bits = 34.0 + 18.0 + (handshake ? (16.0 + 3.0) : 0.0);

    if (low_speed) {
        bits *= 8.0;
        for (int i = 0; i < UHCI_PORTS; i++) {
            const uhci_port_t *port = &dev->port[i];

            if ((port->usb != NULL) && !port->usb->low_speed && (port->sc & UHCI_PORT_PE) &&
                !(port->sc & (UHCI_PORT_SUSP | UHCI_PORT_PR))) {
                bits += 20.0;
                break;
            }
        }
    }

    return bits * 1000.0 / 12.0;
}

/* Bus time charged for one descriptor fetch, about one PCI burst read.
   Charging it also ends a loop of empty queue heads when the frame runs
   out. */
#define UHCI_FETCH_NS 250.0

static usb_dev_t *
uhci_find_device(uhci_t *dev, int addr, int low_speed)
{
    for (int i = 0; i < UHCI_PORTS; i++) {
        const uhci_port_t *port = &dev->port[i];

        usb_dev_t         *usb;

        /* Only an enabled port that is neither suspended nor in reset
           carries traffic, and a low-speed token only reaches a low-speed
           device. On a port with a full-speed hub the token goes out behind
           a preamble (UHCI 1.1 s3.2.2 bit 26, p. 22 / PDF 28) for the hub to
           repeat to its low-speed ports. */
        if ((port->usb == NULL) || !(port->sc & UHCI_PORT_PE) || (port->sc & (UHCI_PORT_SUSP | UHCI_PORT_PR)))
            continue;
        if ((port->usb->addr == addr) && (port->usb->low_speed == low_speed))
            return port->usb;
        if ((port->usb->ops->route != NULL) && ((usb = port->usb->ops->route(port->usb, addr, low_speed)) != NULL))
            return usb;
    }

    return NULL;
}

enum {
    TD_INACTIVE = 0,
    TD_ADVANCE,   /* completed: the queue moves on */
    TD_SHORT,     /* short packet with SPD: inactive, queue not advanced */
    TD_RETRY,     /* left active: NAK or a counted error */
    TD_FAILED,    /* inactive with an error: queue not advanced */
    TD_END_FRAME, /* babble ends the frame */
    TD_NO_TIME,   /* not started: preSOF has passed */
    TD_HALT       /* process error: Run/Stop cleared */
};

/* A CRC/timeout error counts down C_ERR; a TD set up with C_ERR 0 is
   retried without limit (UHCI 1.1 s3.2.2, p. 22 / PDF 28). */
static int
uhci_td_timeout(uhci_t *dev, uint32_t *ctrl)
{
    int cerr = (*ctrl & UHCI_TD_CERR) >> 27;

    *ctrl |= UHCI_TD_CRCTO;
    if (cerr == 0)
        return TD_RETRY;

    cerr--;
    *ctrl = (*ctrl & ~UHCI_TD_CERR) | ((uint32_t) cerr << 27);
    if (cerr != 0)
        return TD_RETRY;

    *ctrl        = (*ctrl & ~UHCI_TD_ACTIVE) | UHCI_TD_STALLED;
    dev->eof_err = 1;
    return TD_FAILED;
}

/* A device still sending at EOF2 has the port it sends through disabled
   (USB 1.1 s11.2.2, p. 234 / PDF 250; s11.8.1, p. 251 / PDF 267). Behind a
   hub that is the nearest hub's port (s8.7.4, p. 174 / PDF 190); the hub's
   own EOP at EOF1 keeps the root port enabled. The VIA Babble Option bit
   applies only to the controller's root ports. */
static void
uhci_eof_babbler(uhci_t *dev, usb_dev_t *usb)
{
    if (usb->parent != NULL) {
        if (usb->parent->ops->eof_babble != NULL)
            usb->parent->ops->eof_babble(usb->parent, usb);
        return;
    }

    if (dev->babble_keep_port)
        return;

    for (int i = 0; i < UHCI_PORTS; i++) {
        uhci_port_t *port = &dev->port[i];

        if ((port->usb == usb) && (port->sc & UHCI_PORT_PE)) {
            port->sc = (port->sc & ~UHCI_PORT_PE) | UHCI_PORT_PEDC;
        }
    }
}

static int
uhci_exec_td(uhci_t *dev, uint32_t addr, int in_queue, double *left, uint32_t *link)
{
    uint32_t     td[4];
    uint32_t     ctrl;
    uint8_t      buf[1280];
    usb_packet_t p;
    usb_dev_t   *usb;
    usb_dev_t   *babbler = NULL;
    int          pid;
    int          len;
    int          ls;
    int          iso;
    int          fit;
    int          eof = 0;
    int          ret;
    int          result;
    int          pend_n   = -1;
    int          pend_tog = 0;
    const uint8_t *pend   = NULL;
    double       in_ns    = 0.0;
    double       out_ns   = 0.0;
    double       to_eof;
    double       to_end;

    dma_bm_read(addr, (uint8_t *) td, 16, 4);
    *link = td[0];
    ctrl  = td[1];

    /* IOC is reported at the end of the frame even for a TD that is already
       inactive when fetched (UHCI 1.1 s3.2.2 bit 24, p. 22 / PDF 28). */
    if (!(ctrl & UHCI_TD_ACTIVE)) {
        if (ctrl & UHCI_TD_IOC)
            dev->eof_ioc = 1;
        return TD_INACTIVE;
    }

    pid = td[2] & 0xff;
    len = (td[2] >> 21) & 0x7ff;

    /* A PID other than IN, OUT or SETUP, or a MaxLen of 500h-7FEh, fails the
       consistency check (UHCI 1.1 s3.2.3, p. 24 / PDF 30; s4.2.2, p. 37 /
       PDF 43). */
    if (((pid != USB_PID_IN) && (pid != USB_PID_OUT) && (pid != USB_PID_SETUP)) ||
        ((len >= 0x500) && (len != 0x7ff))) {
        uhci_self_stop(dev, UHCI_STS_HCPE);
        return TD_HALT;
    }

    len = (len + 1) & 0x7ff;
    ls  = !!(ctrl & UHCI_TD_LS);
    iso = !!(ctrl & UHCI_TD_IOS);

    /* Nothing starts past the preSOF point, placed here so that a
       MAXP-sized IN can still end by EOF2 (UHCI 1.1 s1.3.1 cases A and B,
       p. 7-8 / PDF 13-14). A transaction started before it runs to
       completion (case C). One that is still running at EOF is an EOF
       babble (s2.1.1 bit 7, p. 11 / PDF 17; s4.1.4, p. 36 / PDF 42). */
    if (*left < (UHCI_EOF2_NS + uhci_bus_ns(0, 0, 1, (dev->cmd & UHCI_CMD_MAXP) ? 64 : 32)))
        return TD_NO_TIME;
    usb    = uhci_find_device(dev, (td[2] >> 8) & 0x7f, ls);
    to_eof = (iso && (pid == USB_PID_OUT)) ? *left : (*left - UHCI_EOF2_NS);
    to_end = ((pid == USB_PID_IN) && (usb != NULL) && (usb->parent != NULL)) ? (*left - UHCI_EOF1_NS) : to_eof;

    /* Wire time is that of the packet itself, with its own bit stuffing
       (USB 1.1 s7.1.9, p. 124 / PDF 140): the host's data for an OUT, the
       device's pending packet for an IN. The data of an isochronous IN is
       known only once it is sent. */
    if ((pid != USB_PID_IN) && (len > 0))
        dma_bm_read(td[3], buf, len, 1);
    if (pid != USB_PID_IN) {
        out_ns = uhci_data_ns(ls, iso, 0, (td[2] >> 19) & 1, buf, len);
        fit    = (out_ns > to_end) ? -1 : len;
    } else {
        if (!iso && (usb != NULL))
            pend_n = usb_dev_in_peek(usb, (td[2] >> 15) & 0x0f, &pend, &pend_tog);
        if (pend_n >= 0) {
            fit = uhci_data_fit(ls, 0, pend_tog, pend, pend_n, to_end);
            if ((fit == pend_n) || (fit > len))
                fit = len;
        } else
            fit = uhci_bytes_before_eof(ls, iso, 1, len, to_end);
    }

    p.pid     = pid;
    p.ep      = (td[2] >> 15) & 0x0f;
    p.toggle  = (td[2] >> 19) & 1;
    p.data    = buf;
    p.len     = len;
    p.actual  = 0;
    p.bus_len = 0;
    /* The controller accepts no more than MaxLen (UHCI 1.1 s3.2.3, p. 24 /
       PDF 30). A packet it cannot accept gets no handshake (USB 1.1
       s8.4.5.2, p. 161 / PDF 177), so the device keeps it. */
    p.ack_overrun = 0;
    p.no_ack      = 0;
    p.hub_cut     = 0;

    if ((pid != USB_PID_IN) && (fit < len)) {
        /* The OUT data would run past its limit. The controller ends its
           own packet with a forced bit stuff error and an EOP (UHCI 1.1
           s4.1.4, p. 36 / PDF 42) rather than draw a handshake past EOF2
           (USB 1.1 s11.3.3, p. 236 / PDF 252), so the device takes
           nothing. */
        ret = USB_RET_BABBLE;
        eof = 1;
    } else {
        int room = (fit < 0) ? 0 : fit;

        /* IN data cut off at EOF gets no handshake, so the device keeps it;
           that includes an empty packet when not even that ends in time. An
           isochronous IN is sent once, whatever the host can take. */
        if ((fit < len) && !iso) {
            p.len     = room;
            p.no_ack  = (fit < 0);
            p.hub_cut = (usb != NULL) && (usb->parent != NULL);
        }
        ret   = (usb != NULL) ? usb_dev_packet(usb, &p) : USB_RET_NO_RESPONSE;
        p.len = len; /* a short packet is short of MaxLen */

        /* An IN is an EOF babble when the device's packet, whatever MaxLen
           says, runs past EOF2 with the device still sending. A NAK or STALL
           is timed on the wire like any packet. With no answer only the
           token was sent, and a token that passed the preSOF check always
           ends before EOF: that is a timeout. */
        if (pid == USB_PID_IN) {
            if ((ret == USB_RET_NAK) || (ret == USB_RET_STALL) || (ret == USB_RET_NO_RESPONSE))
                in_ns = uhci_in_nodata_ns(dev, ls, ret != USB_RET_NO_RESPONSE);
            else if ((pend_n >= 0) && (p.bus_len == pend_n))
                in_ns = uhci_data_ns(ls, 0, 1, pend_tog, pend, pend_n);
            else if (iso && (p.bus_len <= p.actual)) {
                /* Isochronous data is sent as DATA0 (USB 1.1 s8.5.4, p. 168 /
                   PDF 184). */
                in_ns = uhci_data_ns(ls, 1, 1, 0, buf, p.bus_len);
                fit   = uhci_data_fit(ls, 1, 0, buf, p.bus_len, to_end);
                room  = (fit < 0) ? 0 : fit;
            } else
                in_ns = uhci_bus_ns(ls, iso, 1, p.bus_len);
            if ((ret != USB_RET_NO_RESPONSE) && (in_ns > to_eof)) {
                babbler = usb;
                eof     = 1;
                /* Behind a hub, the hub's EOP at EOF1 ended the packet
                   upstream: the host sees a truncated packet with a CRC
                   error (USB 1.1 s8.7.3, p. 173 / PDF 189; s11.3.2, p. 235 /
                   PDF 251), not an EOF babble on the root port. */
                if (usb->parent != NULL)
                    ret = USB_RET_NO_RESPONSE;
                else {
                    ret = USB_RET_BABBLE;
                    if (p.actual > room)
                        p.actual = room;
                }
            } else if ((ret != USB_RET_NO_RESPONSE) && (in_ns > to_end) && ((p.bus_len <= len) || (fit < len))) {
                /* The hub's EOP at EOF1 cut the answer. A data packet with a
                   CRC error, or a handshake that fails its PID check, is
                   discarded (USB 1.1 s8.7.3, p. 173 / PDF 189; s8.3.1, p. 155 /
                   PDF 171), so the device is never ACKed and keeps its data.
                   A packet that ran past MaxLen before EOF1 is left as the
                   babble it already was. */
                ret = USB_RET_NO_RESPONSE;
            }
        }

        /* Behind a hub, a handshake still running at EOF1 is cut by the
           hub's EOP (USB 1.1 s11.3.2, p. 235 / PDF 251) and fails its PID
           check (s8.3.1, p. 155 / PDF 171): the device has the data, the
           host sees no handshake. */
        if ((pid != USB_PID_IN) && !iso && (ret != USB_RET_NO_RESPONSE) && (usb->parent != NULL) &&
            (out_ns > (*left - UHCI_EOF1_NS))) {
            ret = USB_RET_NO_RESPONSE;
        }

        /* An isochronous OUT has no handshake (USB 1.1 s8.5.4, p. 168 /
           PDF 184): once token and data are sent the transaction is over,
           whether or not a device took the data. */
        if (iso && (pid == USB_PID_OUT))
            ret = USB_RET_ACK;
    }

    /* OUT data is on the bus whatever the handshake. An IN takes the time
       of what the device sent, including all of a babbled packet. An EOF
       babble uses up the rest of the frame. */
    if (eof)
        *left = 0.0;
    else
        *left -= (pid == USB_PID_IN) ? in_ns : out_ns;

    if (babbler != NULL)
        uhci_eof_babbler(dev, babbler);

    /* Each execution rewrites the status field and ActLen (UHCI 1.1 s3.2.2
       bits 23:16 and 10:0, p. 23 / PDF 29). ActLen is n - 1 encoded, so
       7FFh means no bytes; it stays so unless data moved. */
    ctrl = (ctrl & ~((UHCI_TD_STATUS & ~UHCI_TD_ACTIVE) | UHCI_TD_ACTLEN)) | 0x7ff;
    result = TD_RETRY;

    switch (ret) {
        case USB_RET_ACK:
            if (pid == USB_PID_IN) {
                if (p.actual > 0)
                    dma_bm_write(td[3], buf, p.actual, 1);
                len = p.actual;
            }
            ctrl   = (ctrl & ~(UHCI_TD_ACTIVE | UHCI_TD_ACTLEN)) | ((len - 1) & UHCI_TD_ACTLEN);
            result = TD_ADVANCE;
            if ((pid == USB_PID_IN) && in_queue && (ctrl & UHCI_TD_SPD) && (p.actual < p.len)) {
                dev->eof_spd = 1;
                result       = TD_SHORT;
            }
            break;

        case USB_RET_TOGGLE_MISMATCH:
            /* The receiver ACKs a packet with the wrong toggle and discards it
               (USB 1.1 s8.6.4, p. 170 / PDF 186); the TD stays active for the
               right one. */
            break;

        case USB_RET_NAK:
            ctrl |= UHCI_TD_NAK;
            /* A NAK or STALL to a SETUP is also reported as a timeout: C_ERR
               counts it down, and Stalled is set only when it reaches 0
               (UHCI 1.1 s3.2.2 bits 19 and 18, p. 23 / PDF 29; note under
               Table 7, p. 30 / PDF 36). */
            if (pid == USB_PID_SETUP)
                result = uhci_td_timeout(dev, &ctrl);
            break;

        case USB_RET_STALL:
            if (pid == USB_PID_SETUP) {
                result = uhci_td_timeout(dev, &ctrl);
                break;
            }
            ctrl   = (ctrl & ~UHCI_TD_ACTIVE) | UHCI_TD_STALLED;
            result = TD_FAILED;
            dev->eof_err = 1;
            break;

        case USB_RET_BABBLE:
            if (p.actual > 0)
                dma_bm_write(td[3], buf, p.actual, 1);
            ctrl   = (ctrl & ~(UHCI_TD_ACTIVE | UHCI_TD_ACTLEN)) | UHCI_TD_BABBLE | UHCI_TD_STALLED |
                     ((p.actual - 1) & UHCI_TD_ACTLEN);
            result = TD_END_FRAME;
            dev->eof_err = 1;
            break;

        default:
            /* No handshake. */
            result = uhci_td_timeout(dev, &ctrl);
            break;
    }

    /* An isochronous TD goes inactive after one execution, whatever the
       result (UHCI 1.1 s3.2.2 bit 25, p. 22 / PDF 28). One retired with an
       error sets USB Error Interrupt (s2.1.2 bit 1, p. 14 / PDF 20). */
    if (iso && (ctrl & UHCI_TD_ACTIVE)) {
        ctrl &= ~UHCI_TD_ACTIVE;
        result = TD_FAILED;
        if (ctrl & (UHCI_TD_CRCTO | UHCI_TD_BITSTUFF | UHCI_TD_DBE))
            dev->eof_err = 1;
    }

    if (!(ctrl & UHCI_TD_ACTIVE) && (ctrl & UHCI_TD_IOC))
        dev->eof_ioc = 1;

    dma_bm_write(addr + 4, (uint8_t *) &ctrl, 4, 4);

    return result;
}

/* One frame of the schedule: the frame list entry, then TDs and queue
   heads by their link pointers until a terminate bit or the end of the
   frame (UHCI 1.1 s3.4, p. 26-34 / PDF 32-40). */
static void
uhci_run_frame(uhci_t *dev)
{
    uint32_t link;
    uint32_t qh[2];
    uint32_t td_link;
    uint32_t seen_qh[32];
    int      seen_work[32];
    int      n_seen = 0;
    int      work   = 0;
    int      steps  = 0;
    int      r;
    int      resume = dev->dbg_resume;
    /* The frame opens with its SOF (UHCI 1.1 s1.1.1, p. 4 / PDF 10): SYNC,
       PID, frame number and CRC5 make 32 bits, then a 3-bit EOP (USB 1.1
       s8.4.2, p. 159 / PDF 175; Table 7-1, p. 116 / PDF 132). A walk
       resumed in debug mode uses the remainder it saved instead. */
    double   left   = (uhci_frame_us(dev) * 1000.0) - (35.0 * 1000.0 / 12.0);
    double   presof = UHCI_EOF2_NS + uhci_bus_ns(0, 0, 1, (dev->cmd & UHCI_CMD_MAXP) ? 64 : 32);

    /* A frame that is not a debug resume has walked nothing yet. */
    dev->dbg_resume = 0;
    if (!resume) {
        dev->dbg_link = UHCI_LP_T;
        dev->dbg_left = left;
    }

    /* With Bus Master Enable clear the controller generates no PCI accesses
       (PIIX4 datasheet s6.1.3, p. 103 / PDF 103). The frame counter uses no
       memory, so it keeps counting. */
    if (!uhci_running(dev) || !dev->bus_master)
        return;

    /* After a stop in debug mode the walk picks up where it left off (UHCI
       1.1 Table 4, p. 12 / PDF 18). */
    if (resume) {
        link = dev->dbg_link;
        left = dev->dbg_left;
    } else {
        dma_bm_read(dev->flbaseadd + ((dev->frnum & 0x3ff) << 2), (uint8_t *) &link, 4, 4);
    }

    while (!(link & UHCI_LP_T) && (steps++ < 4096)) {
        /* Past preSOF nothing more is fetched this frame (UHCI 1.1 s1.3.1
           cases A and C, p. 7 / PDF 13). A TD that found no time (case B)
           also ends the walk, so an inactive IOC TD behind it is first seen
           in a later frame. */
        if (left < presof)
            break;
        /* Save the resume point before each element, so a stop on this
           element leaves the walk here. */
        dev->dbg_link = link;
        dev->dbg_left = left;
        left -= UHCI_FETCH_NS;

        if (link & UHCI_LP_Q) {
            uint32_t qh_addr = link & ~0x0f;
            int      i;

            /* A queue head reached again with no transaction since the last
               visit means a bandwidth reclamation loop with nothing left to
               run. */
            for (i = 0; i < n_seen; i++) {
                if (seen_qh[i] == qh_addr)
                    break;
            }
            if (i < n_seen) {
                if (seen_work[i] == work)
                    break;
                seen_work[i] = work;
            } else if (n_seen < 32) {
                seen_qh[n_seen]   = qh_addr;
                seen_work[n_seen] = work;
                n_seen++;
            }

            dma_bm_read(qh_addr, (uint8_t *) qh, 8, 4);

            if (qh[1] & UHCI_LP_T) {
                link = qh[0];
                continue;
            }
            /* An element that is a QH starts a new queue context; the
               element pointer of the one just left is not written back
               (UHCI 1.1 s3.4.2, p. 31 / PDF 37). */
            if (qh[1] & UHCI_LP_Q) {
                link = qh[1];
                continue;
            }

            r = uhci_exec_td(dev, qh[1] & ~0x0f, 1, &left, &td_link);
            if (r == TD_HALT)
                break;
            if ((r != TD_INACTIVE) && (r != TD_NO_TIME))
                work++;

            if (r == TD_ADVANCE) {
                /* The TD's link becomes the queue element (UHCI 1.1 s3.4.1.3
                   step 9, p. 29 / PDF 35). */
                dma_bm_write(qh_addr + 4, (uint8_t *) &td_link, 4, 4);
                /* Depth first: revisit this QH, whose element is now the
                   next TD. Breadth first: go on along the QH's link. */
                link = ((td_link & UHCI_LP_VF) && !(td_link & UHCI_LP_T)) ? (qh_addr | UHCI_LP_Q) : qh[0];
            } else
                link = qh[0];
        } else {
            r = uhci_exec_td(dev, link & ~0x0f, 0, &left, &td_link);
            if (r == TD_HALT)
                break;
            if ((r != TD_INACTIVE) && (r != TD_NO_TIME))
                work++;
            link = td_link;
        }

        /* Debug mode stops after each executed TD (UHCI 1.1 s2.1.1.1, p. 13 /
           PDF 19). A babble ends the frame, so after that step the walk
           resumes at nothing until the next frame. A TD not started for lack
           of time was not executed. */
        if ((dev->cmd & UHCI_CMD_SWDBG) && (r != TD_INACTIVE) && (r != TD_NO_TIME)) {
            dev->dbg_link = (r == TD_END_FRAME) ? UHCI_LP_T : link;
            dev->dbg_left = left;
            uhci_self_stop(dev, 0);
            break;
        }

        /* Babble ends the frame: no further TDs run in it (UHCI 1.1 s3.2.2
           bit 20, p. 23 / PDF 29). */
        if ((r == TD_END_FRAME) || (left <= 0.0))
            break;
    }

    /* A walk that ended with the controller still running is done with
       this frame and waits for the next SOF (UHCI 1.1 s2.1.1.1, p. 13 /
       PDF 19), so a later stop resumes at nothing. */
    if (uhci_running(dev)) {
        dev->dbg_link = UHCI_LP_T;
        dev->dbg_left = left;
    }
}

static void
uhci_frame_end(void *priv)
{
    uhci_t *dev = (uhci_t *) priv;

    uhci_latch_frame_status(dev);
    uhci_latch_lines(dev);
    dev->frnum      = (dev->frnum + 1) & 0x07ff;
    dev->dbg_resume = 0;
    timer_on_auto(&dev->frame_timer, uhci_frame_us(dev));
    uhci_run_frame(dev);
}

static int
uhci_port_in_reset(const uhci_t *dev, const uhci_port_t *port)
{
    return (port->sc & UHCI_PORT_PR) || (dev->cmd & UHCI_CMD_GRESET);
}

/* Writing FGR or PORTSC RD from 1 to 0 ends the resume with a low-speed
   EOP, about 2 bit times of SE0 and 1 of J (USB 1.1 Table 7-1, p. 116 /
   PDF 132; s7.1.13.2, p. 128 / PDF 144). The bit reads 1 until the EOP is
   done (UHCI 1.1 s2.1.1 bit 4, p. 11 / PDF 17; s2.1.7 bit 6, p. 16 /
   PDF 22). */
#define UHCI_LS_EOP_US (3.0 / 1.5)

static void
uhci_fgr_eop_done(void *priv)
{
    uhci_t *dev = (uhci_t *) priv;

    dev->fgr_eop = 0;
    dev->cmd &= ~UHCI_CMD_FGR;
}

static void
uhci_rd_eop_done(void *priv)
{
    uhci_port_t *port = (uhci_port_t *) priv;

    port->eop = 0;
    port->sc &= ~UHCI_PORT_RD;
}

/* A resume event sets FGR. One that comes while the EOP ending a global
   resume is still on the wire drops that EOP and starts resume signaling
   over. */
static void
uhci_set_fgr(uhci_t *dev)
{
    if (dev->fgr_eop) {
        timer_stop(&dev->fgr_timer);
        dev->fgr_eop = 0;
    }
    dev->cmd |= UHCI_CMD_FGR;
}

static void
uhci_port_eop_stop(uhci_port_t *port)
{
    if (port->eop) {
        timer_stop(&port->eop_timer);
        port->eop = 0;
    }
}

/* Connect detection: an attached device is seen up to 64 bit times after
   the SE0 of a reset ends (UHCI 1.1 s2.1.1 bit 1, p. 12 / PDF 18; s2.1.7,
   p. 16 / PDF 22). */
static void
uhci_detect(void *priv)
{
    uhci_t *dev = (uhci_t *) priv;

    for (int i = 0; i < UHCI_PORTS; i++) {
        uhci_port_t *port = &dev->port[i];

        if ((port->usb == NULL) || (port->sc & UHCI_PORT_CCS) || uhci_port_in_reset(dev, port))
            continue;

        port->sc |= UHCI_PORT_CCS | UHCI_PORT_CSC;
        if (port->usb->low_speed)
            port->sc |= UHCI_PORT_LSDA;

        /* A connect in global suspend is a resume event: it sets Resume
           Detect in USBSTS and FGR in USBCMD (UHCI 1.1 Table 6, p. 18 /
           PDF 24; s2.1.1 bit 4, p. 11 / PDF 17). */
        if (dev->cmd & UHCI_CMD_EGSM) {
            uhci_set_fgr(dev);
            dev->sts |= UHCI_STS_RD;
        }
    }

    uhci_update_irq(dev);
}

static void
uhci_schedule_detect(uhci_t *dev)
{
    for (int i = 0; i < UHCI_PORTS; i++) {
        if (dev->port[i].usb != NULL) {
            timer_on_auto(&dev->detect_timer, 64.0 / 12.0);
            return;
        }
    }
}

/* Power-on state. A chip reset and GRESET both land here: they differ only
   in that GRESET also signals reset on the bus (UHCI 1.1 s2.1.1 bit 2,
   p. 12 / PDF 18). */
static void
uhci_power_on_reset(uhci_t *dev)
{
    timer_stop(&dev->frame_timer);
    timer_stop(&dev->detect_timer);
    timer_stop(&dev->fgr_timer);
    dev->fgr_eop = 0;

    dev->cmd          = 0x0000;
    dev->sts          = 0x0000;
    dev->intr         = 0x0000;
    dev->frnum        = 0x0000;
    dev->flbaseadd    = 0x00000000;
    dev->sofmod       = 0x40;
    dev->int_ioc      = 0;
    dev->int_spd      = 0;
    dev->eof_ioc      = 0;
    dev->eof_spd      = 0;
    dev->eof_err      = 0;
    dev->frame_frozen = 0;
    dev->dbg_resume   = 0;

    for (int i = 0; i < UHCI_PORTS; i++) {
        uhci_port_eop_stop(&dev->port[i]);
        dev->port[i].sc = 0x0000;
    }

    uhci_update_irq(dev);
}

/* HCRESET resets timers, counters and state machines (UHCI 1.1 s2.1.1
   bit 1, p. 12 / PDF 18) and SOFMOD (s2.1.6, p. 15 / PDF 21); USBINTR and
   FLBASEADD keep their values. Resetting each port's connect state machine
   is a virtual disconnect: CCS, PE and LSDA clear, CSC and PEDC are set
   where they changed, and the device is detected again shortly after. */
static void
uhci_hc_reset(uhci_t *dev)
{
    timer_stop(&dev->frame_timer);
    timer_stop(&dev->fgr_timer);
    dev->fgr_eop = 0;

    dev->cmd          = 0x0000;
    dev->sts          = 0x0000;
    dev->frnum        = 0x0000;
    dev->sofmod       = 0x40;
    dev->int_ioc      = 0;
    dev->int_spd      = 0;
    dev->eof_ioc      = 0;
    dev->eof_spd      = 0;
    dev->eof_err      = 0;
    dev->frame_frozen = 0;
    dev->dbg_resume   = 0;

    for (int i = 0; i < UHCI_PORTS; i++) {
        uhci_port_t *port = &dev->port[i];
        uint16_t     old  = port->sc;

        uhci_port_eop_stop(port);
        port->sc &= ~(UHCI_PORT_CCS | UHCI_PORT_PE | UHCI_PORT_LSDA | UHCI_PORT_SUSP | UHCI_PORT_RD);
        if (old & UHCI_PORT_CCS)
            port->sc |= UHCI_PORT_CSC;
        if (old & UHCI_PORT_PE)
            port->sc |= UHCI_PORT_PEDC;
    }

    uhci_update_irq(dev);
    uhci_schedule_detect(dev);
}

static void
uhci_cmd_write(uhci_t *dev, uint16_t val)
{
    uint16_t old         = dev->cmd;
    int      was_running = uhci_running(dev);

    val &= 0x00ff;

    if (val & UHCI_CMD_GRESET) {
        if (!(old & UHCI_CMD_GRESET)) {
            uhci_power_on_reset(dev);
            /* Global reset signaling reaches every device on the bus. */
            for (int i = 0; i < UHCI_PORTS; i++) {
                if (dev->port[i].usb != NULL)
                    usb_dev_bus_reset(dev->port[i].usb);
            }
        }
        /* GRESET stays 1 until software clears it (UHCI 1.1 s2.1.1 bit 2,
           p. 12 / PDF 18), so the rest of the write is kept in USBCMD;
           nothing runs while GRESET is held. */
        dev->cmd = val & ~UHCI_CMD_HCRESET;
        return;
    }

    if (old & UHCI_CMD_GRESET) {
        uhci_schedule_detect(dev);
    }

    if (val & UHCI_CMD_HCRESET) {
        /* The reset completes at once, so the self-clearing bit already
           reads 0. Reset clears USBSTS (UHCI 1.1 Table 7, HC Reset row,
           p. 30 / PDF 36) and leaves Run/Stop 0, so the controller then sits
           stopped and reports HCHalted (Table 7, Run/Stop = 0 row; s2.1.2
           bit 5, p. 13 / PDF 19). Intel's AN430TX BIOS waits for HCHalted
           after HCRESET. */
        uhci_hc_reset(dev);
        uhci_set_halted(dev, 1);
        return;
    }

    /* FGR written 0 keeps reading 1 while the EOP runs; FGR written 1 in
       that time drives K again. */
    if (dev->fgr_eop) {
        if (val & UHCI_CMD_FGR) {
            timer_stop(&dev->fgr_timer);
            dev->fgr_eop = 0;
        } else
            val |= UHCI_CMD_FGR;
    } else if ((old & UHCI_CMD_FGR) && !(val & UHCI_CMD_FGR)) {
        val |= UHCI_CMD_FGR;
        dev->fgr_eop = 1;
        timer_on_auto(&dev->fgr_timer, UHCI_LS_EOP_US);
    }

    dev->cmd = val;

    /* RS written 0 sets HCHalted (UHCI 1.1 Table 7, p. 30 / PDF 36); RS
       written 1 clears it. HCHalted is not cleared by writing 1 to it: the
       spec lists USBSTS as R/WC, but Linux's uhci-hcd.c records that on real
       controllers HCHalted is read-only. */
    uhci_set_halted(dev, !(val & UHCI_CMD_RS));
    uhci_update_frame_clock(dev, was_running);
}

/* The lines as they are now: SE0 with nothing attached or in reset, else
   idle J, or K while resume is driven. J is D+ high at full speed and D-
   high at low speed (USB 1.1 Table 7-1, p. 116 / PDF 132). A global resume
   drives K on enabled ports only, not on a selectively suspended one (UHCI
   1.1 s2.1.7.1, p. 17 / PDF 23; Table 6, p. 18 / PDF 24). The EOP that ends
   a resume reads as its SE0. */
static uint16_t
uhci_port_line(const uhci_t *dev, const uhci_port_t *port)
{
    uint16_t j;
    int      fgr_k;

    if (!(port->sc & UHCI_PORT_CCS) || uhci_port_in_reset(dev, port))
        return 0;

    fgr_k = (dev->cmd & UHCI_CMD_FGR) && ((port->sc & (UHCI_PORT_PE | UHCI_PORT_SUSP)) == UHCI_PORT_PE);
    j     = port->usb->low_speed ? UHCI_PORT_DM : UHCI_PORT_DP;
    if (port->eop || (fgr_k && dev->fgr_eop))
        return 0;
    if (((port->sc & (UHCI_PORT_RD | UHCI_PORT_SUSP)) == (UHCI_PORT_RD | UHCI_PORT_SUSP)) || fgr_k)
        j ^= (UHCI_PORT_DP | UHCI_PORT_DM);

    return j;
}

/* Line Status is updated at EOF2 (UHCI 1.1 s2.1.7 bits 5:4, p. 16 /
   PDF 22): it is sampled at each frame end while frames run, and read live
   while none do. */
static void
uhci_latch_lines(uhci_t *dev)
{
    for (int i = 0; i < UHCI_PORTS; i++)
        dev->port[i].line = uhci_port_line(dev, &dev->port[i]);
}

static uint16_t
uhci_portsc_read(const uhci_t *dev, int n)
{
    const uhci_port_t *port = &dev->port[n];

    return port->sc | UHCI_PORT_ONE | (uhci_running(dev) ? port->line : uhci_port_line(dev, port));
}

static void
uhci_portsc_write(uhci_t *dev, int n, uint16_t val)
{
    uhci_port_t *port = &dev->port[n];
    uint16_t     old  = port->sc;

    port->sc &= ~(val & (UHCI_PORT_CSC | UHCI_PORT_PEDC));

    if ((val & UHCI_PORT_PR) && !(old & UHCI_PORT_PR)) {
        /* The reset SE0 reads as a disconnect: an enabled port reports its
           disable in PEDC, and the end of reset is a connect change. The
           spec does not say this; Linux's uhci-hub.c expects it of real
           controllers and clears both changes after a port reset. */
        if (old & UHCI_PORT_PE)
            port->sc |= UHCI_PORT_PEDC;
        uhci_port_eop_stop(port);
        port->sc = (port->sc & ~(UHCI_PORT_PE | UHCI_PORT_SUSP | UHCI_PORT_RD)) | UHCI_PORT_PR;
        if (port->usb != NULL)
            usb_dev_bus_reset(port->usb);
    } else if (!(val & UHCI_PORT_PR) && (old & UHCI_PORT_PR)) {
        port->sc &= ~UHCI_PORT_PR;
        if (port->sc & UHCI_PORT_CCS)
            port->sc |= UHCI_PORT_CSC;
        /* Connect detection skips a port in reset, so a device not yet
           seen is detected now. */
        else if (port->usb != NULL)
            uhci_schedule_detect(dev);
    }

    if (!(port->sc & UHCI_PORT_PR)) {
        /* Only software enables a port (UHCI 1.1 s2.1.7 bit 2, p. 17 /
           PDF 23), and only a connected one: a disconnected port has no path
           to the Enabled state (USB 1.1 s11.5.1, p. 240-242 / PDF 256-258). */
        if ((val & UHCI_PORT_PE) && (port->sc & UHCI_PORT_CCS))
            port->sc |= UHCI_PORT_PE;
        else if (!(val & UHCI_PORT_PE))
            port->sc &= ~UHCI_PORT_PE;

        /* Suspend is a state of an enabled port: bits [12,2] = 11 (UHCI 1.1
           s2.1.7 bit 12, p. 16 / PDF 22). */
        if ((val & UHCI_PORT_SUSP) && (port->sc & UHCI_PORT_PE))
            port->sc |= UHCI_PORT_SUSP;
        else
            port->sc &= ~UHCI_PORT_SUSP;

        /* RD written 0 keeps reading 1 while the EOP runs; RD written 1 in
           that time drives K again. */
        if (port->eop) {
            if (val & UHCI_PORT_RD)
                uhci_port_eop_stop(port);
        } else if ((old & UHCI_PORT_RD) && !(val & UHCI_PORT_RD)) {
            port->eop = 1;
            timer_on_auto(&port->eop_timer, UHCI_LS_EOP_US);
        } else
            port->sc = (port->sc & ~UHCI_PORT_RD) | (val & UHCI_PORT_RD);
    }
}

/* Nonzero when no SOFs, traffic, reset or K reach the device on this port:
   the port is disabled or suspended, or the controller is stopped or in
   global suspend. A device on an idle bus suspends (USB 1.1 s7.1.7.4,
   p. 122 / PDF 138; s9.1.1.6, p. 179 / PDF 195). K counts as driven
   exactly when uhci_port_line shows it. */
static int
uhci_port_idle(const uhci_t *dev, const uhci_port_t *port)
{
    if (!(port->sc & UHCI_PORT_CCS) || uhci_port_in_reset(dev, port))
        return 0;
    if (!(port->sc & UHCI_PORT_PE))
        return 1;
    if (((port->sc & (UHCI_PORT_RD | UHCI_PORT_SUSP)) == (UHCI_PORT_RD | UHCI_PORT_SUSP)) ||
        ((dev->cmd & UHCI_CMD_FGR) && !(port->sc & UHCI_PORT_SUSP)))
        return 0;
    return (port->sc & UHCI_PORT_SUSP) || !uhci_running(dev);
}

/* Resume signaling from the device on a suspended bus (UHCI 1.1 Tables 5
   and 6, p. 18 / PDF 24): a suspended port reflects it in PORTSC RD; in
   global suspend it also sets USBSTS RD and FGR (s2.1.1 bit 4, p. 11 /
   PDF 17). A disabled port, or an enabled one outside global suspend,
   ignores it. */
static void
uhci_port_resume(uhci_t *dev, int n)
{
    uhci_port_t *port = &dev->port[n];

    if (!uhci_port_idle(dev, port) || !(port->sc & UHCI_PORT_PE))
        return;

    if (port->sc & UHCI_PORT_SUSP) {
        port->sc |= UHCI_PORT_RD;
    }
    if (dev->cmd & UHCI_CMD_EGSM) {
        uhci_set_fgr(dev);
        dev->sts |= UHCI_STS_RD;
    }

    uhci_update_irq(dev);
}

static uint16_t
uhci_read16(const uhci_t *dev, uint16_t off)
{
    switch (off & 0x1e) {
        case UHCI_USBCMD:
            return dev->cmd;
        case UHCI_USBSTS:
            return dev->sts;
        case UHCI_USBINTR:
            return dev->intr;
        case UHCI_FRNUM:
            return dev->frnum;
        case UHCI_FLBASEADD:
            return dev->flbaseadd & 0xffff;
        case UHCI_FLBASEADD + 2:
            return dev->flbaseadd >> 16;
        case UHCI_SOFMOD:
            return dev->sofmod;
        case UHCI_PORTSC1:
        case UHCI_PORTSC2:
            return uhci_portsc_read(dev, ((off & 0x1e) - UHCI_PORTSC1) >> 1);
        default:
            /* 0Dh-0Fh and 14h-1Fh are not implemented and read 0. PORTSC
               bit 7 always reads 1 (UHCI 1.1 s2.1.7, p. 16 / PDF 22), and
               drivers that count ports by it (Linux's uhci-hcd.c does) stop
               at bit 7 clear in 14h: two ports. */
            return 0x0000;
    }
}

/* lanes selects the bytes the guest wrote. Unwritten bytes keep their R/W
   bits and write 0 to the write-1-to-clear bits. FRNUM and PORTSC (UHCI
   1.1 Table 2, p. 10 / PDF 16), and USBCMD on the PIIX4 (s6.2.1, p. 109 /
   PDF 109), are word-writeable only, and a byte write to them has no
   defined effect; this merge is what the model does. */
static void
uhci_write16(uhci_t *dev, uint16_t off, uint16_t val, uint16_t lanes)
{
    uint16_t cur;
    int      n;

    off &= 0x1e;

    switch (off) {
        case UHCI_USBCMD:
            if (lanes & 0x00ff)
                uhci_cmd_write(dev, (dev->cmd & ~lanes) | (val & lanes));
            break;

        case UHCI_USBSTS:
            val &= lanes & (UHCI_STS_USBINT | UHCI_STS_ERRINT | UHCI_STS_RD | UHCI_STS_HSE | UHCI_STS_HCPE);
            if (val & dev->sts) {
                dev->sts &= ~val;
                if (val & UHCI_STS_USBINT)
                    dev->int_ioc = dev->int_spd = 0;
                uhci_update_irq(dev);
            }
            break;

        case UHCI_USBINTR:
            dev->intr = ((dev->intr & ~lanes) | (val & lanes)) & 0x000f;
            uhci_update_irq(dev);
            break;

        case UHCI_FRNUM:
            /* A write while Run/Stop is set is ignored (UHCI 1.1 s2.1.4,
               p. 14 / PDF 20). */
            if (!(dev->cmd & UHCI_CMD_RS))
                dev->frnum = ((dev->frnum & ~lanes) | (val & lanes)) & 0x07ff;
            break;

        case UHCI_FLBASEADD:
            dev->flbaseadd = ((dev->flbaseadd & ~((uint32_t) lanes)) | (val & lanes)) & 0xfffff000;
            break;

        case UHCI_FLBASEADD + 2:
            dev->flbaseadd = ((dev->flbaseadd & ~((uint32_t) lanes << 16)) | ((uint32_t) (val & lanes) << 16)) & 0xfffff000;
            break;

        case UHCI_SOFMOD:
            if (lanes & 0x00ff)
                dev->sofmod = val & 0x7f;
            break;

        case UHCI_PORTSC1:
        case UHCI_PORTSC2:
            n   = (off - UHCI_PORTSC1) >> 1;
            cur = dev->port[n].sc & ~(UHCI_PORT_CSC | UHCI_PORT_PEDC);
            uhci_portsc_write(dev, n, (cur & ~lanes) | (val & lanes));
            break;

        default:
            break;
    }
}

static uint8_t
uhci_inb(uint16_t addr, void *priv)
{
    const uhci_t *dev = (uhci_t *) priv;
    uint16_t      off = addr & 0x1f;
    uint8_t       ret = uhci_read16(dev, off) >> ((off & 1) << 3);

    return ret;
}

static uint16_t
uhci_inw(uint16_t addr, void *priv)
{
    const uhci_t *dev = (uhci_t *) priv;
    uint16_t      off = addr & 0x1f;
    uint16_t      ret;

    /* A word read at 1Fh takes its high byte from 20h, outside this block;
       that byte reads FFh, as in uhci_inl. */
    if (off & 1)
        ret = (uhci_read16(dev, off) >> 8) | ((off < 0x1f) ? (uhci_read16(dev, off + 1) << 8) : 0xff00);
    else
        ret = uhci_read16(dev, off);

    return ret;
}

static uint32_t
uhci_inl(uint16_t addr, void *priv)
{
    const uhci_t *dev = (uhci_t *) priv;
    uint16_t      off = addr & 0x1f;
    uint32_t      ret = 0;

    /* Byte by byte, so an unaligned dword gets the bytes at its offset.
       Bytes past 1Fh belong to no register of this block and read FFh. */
    for (int i = 3; i >= 0; i--) {
        uint16_t b = off + i;

        ret = (ret << 8) | ((b < 0x20) ? ((uhci_read16(dev, b) >> ((b & 1) << 3)) & 0xff) : 0xff);
    }

    return ret;
}

static void
uhci_outb(uint16_t addr, uint8_t val, void *priv)
{
    uhci_t  *dev = (uhci_t *) priv;
    uint16_t off = addr & 0x1f;

    if (off & 1)
        uhci_write16(dev, off, val << 8, 0xff00);
    else
        uhci_write16(dev, off, val, 0x00ff);
}

static void
uhci_outw(uint16_t addr, uint16_t val, void *priv)
{
    uhci_t  *dev = (uhci_t *) priv;
    uint16_t off = addr & 0x1f;

    /* A word write at 1Fh puts its high byte at 20h, outside this block; that
       byte is dropped, as in uhci_outl. */
    if (off & 1) {
        uhci_write16(dev, off, val << 8, 0xff00);
        if (off < 0x1f)
            uhci_write16(dev, off + 1, val >> 8, 0x00ff);
    } else
        uhci_write16(dev, off, val, 0xffff);
}

static void
uhci_outl(uint16_t addr, uint32_t val, void *priv)
{
    uhci_t  *dev   = (uhci_t *) priv;
    uint16_t off   = addr & 0x1f;
    uint64_t data  = (uint64_t) val << ((off & 1) << 3);
    uint64_t lanes = 0xffffffffULL << ((off & 1) << 3);

    /* Each register word takes the bytes that fall on it, so an unaligned
       dword keeps its offset. Bytes past 1Fh are outside this block and are
       dropped. */
    for (uint16_t w = off & 0x1e; (w < 0x20) && (lanes & 0xffff); w += 2) {
        uhci_write16(dev, w, data & 0xffff, lanes & 0xffff);
        data >>= 16;
        lanes >>= 16;
    }
}

void
uhci_update_io_mapping(usb_t *dev, uint8_t base_l, uint8_t base_h, int enable)
{
    if (dev->uhci_enable && (dev->uhci_io_base != 0x0000))
        io_removehandler(dev->uhci_io_base, 0x20, uhci_inb, uhci_inw, uhci_inl, uhci_outb, uhci_outw, uhci_outl, dev->uhci);

    dev->uhci_io_base = base_l | (base_h << 8);
    dev->uhci_enable  = enable;

    if (dev->uhci_enable && (dev->uhci_io_base != 0x0000))
        io_sethandler(dev->uhci_io_base, 0x20, uhci_inb, uhci_inw, uhci_inl, uhci_outb, uhci_outw, uhci_outl, dev->uhci);
}

void
uhci_set_irq_handler(usb_t *dev, void (*set_irq)(void *priv, int level), void *priv)
{
    dev->uhci->set_irq  = set_irq;
    dev->uhci->irq_priv = priv;
}

void
uhci_set_bus_master(usb_t *dev, int enable)
{
    dev->uhci->bus_master = !!enable;
}

void
uhci_set_babble_keep_port(usb_t *dev, int keep)
{
    dev->uhci->babble_keep_port = !!keep;
}

#define USB_MAX_HC 8

static usb_t *usb_hcs[USB_MAX_HC];

static void ohci_port_attach(ohci_port_t *p, usb_dev_t *udev);
static void ohci_port_detach(ohci_port_t *p);

static void
uhci_port_attach(uhci_t *hc, int i, usb_dev_t *udev)
{
    hc->port[i].usb = udev;
    udev->port      = &hc->port[i];
    uhci_schedule_detect(hc);
}

/* The port the device was last on, if it is still free. A port on a hub
   counts even while that hub is unplugged: the device goes back into the
   hub, and is seen once the hub is plugged in. With that port taken, the
   hub gives the device another of its ports. */
static int
usb_attach_home(usb_dev_t *udev)
{
    if (udev->home_parent != NULL)
        return udev->home_parent->ops->attach(udev->home_parent, udev);

    if (udev->home_port == NULL)
        return 0;

    for (int h = 0; h < USB_MAX_HC; h++) {
        uhci_t *hc = (usb_hcs[h] != NULL) ? usb_hcs[h]->uhci : NULL;
        ohci_t *oc = (usb_hcs[h] != NULL) ? usb_hcs[h]->ohci : NULL;

        for (int i = 0; (hc != NULL) && (hc->set_irq != NULL) && (i < UHCI_PORTS); i++) {
            if ((udev->home_port == &hc->port[i]) && (hc->port[i].usb == NULL)) {
                uhci_port_attach(hc, i, udev);
                return 1;
            }
        }

        for (int i = 0; (oc != NULL) && (oc->set_irq != NULL) && (i < oc->ports); i++) {
            if ((udev->home_port == &oc->port[i]) && (oc->port[i].usb == NULL)) {
                ohci_port_attach(&oc->port[i], udev);
                return 1;
            }
        }
    }

    return 0;
}

/* Only a controller the chipset has wired up, that is one whose interrupt
   it routes, takes devices. A device goes to the first free root port, then
   to a hub on a root port, then to an OHCI connector past NDP. A device left
   out is named to the user with the reason. */
static int
usb_attach_search(usb_dev_t *udev)
{
    char msg[512];
    int  wired = 0;

    for (int h = 0; h < USB_MAX_HC; h++) {
        uhci_t *hc = (usb_hcs[h] != NULL) ? usb_hcs[h]->uhci : NULL;
        ohci_t *oc = (usb_hcs[h] != NULL) ? usb_hcs[h]->ohci : NULL;

        if ((hc != NULL) && (hc->set_irq != NULL)) {
            wired = 1;
            for (int i = 0; i < UHCI_PORTS; i++) {
                if (hc->port[i].usb == NULL) {
                    uhci_port_attach(hc, i, udev);
                    return 1;
                }
            }
        }

        if ((oc != NULL) && (oc->set_irq != NULL)) {
            wired = 1;
            for (int i = 0; i < oc->ndp; i++) {
                if (oc->port[i].usb == NULL) {
                    ohci_port_attach(&oc->port[i], udev);
                    return 1;
                }
            }
        }
    }

    /* Root ports full: a hub on one of them takes the device. */
    for (int h = 0; h < USB_MAX_HC; h++) {
        uhci_t *hc = (usb_hcs[h] != NULL) ? usb_hcs[h]->uhci : NULL;
        ohci_t *oc = (usb_hcs[h] != NULL) ? usb_hcs[h]->ohci : NULL;

        for (int i = 0; (hc != NULL) && (hc->set_irq != NULL) && (i < UHCI_PORTS); i++) {
            usb_dev_t *hub = hc->port[i].usb;

            if ((hub != NULL) && (hub->ops->attach != NULL) && hub->ops->attach(hub, udev))
                return 1;
        }

        for (int i = 0; (oc != NULL) && (oc->set_irq != NULL) && (i < oc->ndp); i++) {
            usb_dev_t *hub = oc->port[i].usb;

            if ((hub != NULL) && (hub->ops->attach != NULL) && hub->ops->attach(hub, udev))
                return 1;
        }
    }

    /* Last, an OHCI connector past NDP: its port is not implemented, and the
       device stays unseen until the chipset raises NDP. */
    for (int h = 0; h < USB_MAX_HC; h++) {
        ohci_t *oc = (usb_hcs[h] != NULL) ? usb_hcs[h]->ohci : NULL;

        for (int i = (oc != NULL) ? oc->ndp : 0; (oc != NULL) && (oc->set_irq != NULL) && (i < oc->ports); i++) {
            usb_dev_t *on = oc->port[i].usb;

            if (on == NULL) {
                ohci_port_attach(&oc->port[i], udev);
                return 1;
            }
            if ((on->ops->attach != NULL) && on->ops->attach(on, udev))
                return 1;
        }
    }

    snprintf(msg, sizeof(msg), plat_get_string(wired ? STRING_USB_NO_PORT : STRING_USB_NO_CONTROLLER), udev->name);
    pclog("USB: %s\n", msg);
    ui_msgbox_header(MBX_WARNING, NULL, msg);

    return 0;
}

int
usb_attach(usb_dev_t *udev)
{
    if (!usb_attach_home(udev) && !usb_attach_search(udev))
        return 0;

    udev->home_port   = udev->port;
    udev->home_parent = udev->parent;
    return 1;
}

/* A disconnect clears CCS and PE and sets CSC and PEDC for whichever of
   the two was set (UHCI 1.1 s2.1.7, p. 16-17 / PDF 22-23; Tables 5 and 6,
   p. 18 / PDF 24). */
void
usb_detach(usb_dev_t *udev)
{
    if (udev->parent != NULL) {
        udev->parent->ops->detach(udev->parent, udev);
        return;
    }

    for (int h = 0; h < USB_MAX_HC; h++) {
        uhci_t *hc = (usb_hcs[h] != NULL) ? usb_hcs[h]->uhci : NULL;

        if (hc == NULL)
            continue;

        for (int i = 0; i < UHCI_PORTS; i++) {
            uhci_port_t *port = &hc->port[i];

            if (port->usb != udev)
                continue;

            port->usb = NULL;
            if (port->sc & UHCI_PORT_CCS) {
                port->sc |= UHCI_PORT_CSC;
                /* In global suspend the disconnect is a resume event (UHCI 1.1
                   Table 6, p. 18 / PDF 24). A device not yet detected, or one
                   removed under GRESET (which holds PORTSC at 0), leaves no
                   change behind. */
                if (hc->cmd & UHCI_CMD_EGSM) {
                    uhci_set_fgr(hc);
                    hc->sts |= UHCI_STS_RD;
                }
            }
            if (port->sc & UHCI_PORT_PE)
                port->sc |= UHCI_PORT_PEDC;
            uhci_port_eop_stop(port);
            port->sc &= ~(UHCI_PORT_CCS | UHCI_PORT_PE | UHCI_PORT_LSDA | UHCI_PORT_SUSP | UHCI_PORT_RD);
            uhci_update_irq(hc);
        }
    }

    for (int h = 0; h < USB_MAX_HC; h++) {
        ohci_t *oc = (usb_hcs[h] != NULL) ? usb_hcs[h]->ohci : NULL;

        for (int i = 0; (oc != NULL) && (i < OHCI_MAX_PORTS); i++) {
            if (oc->port[i].usb == udev)
                ohci_port_detach(&oc->port[i]);
        }
    }

    udev->port = NULL;
}

/* Unplugging takes the device off its port and with it the bus power the
   port gave it. A device that loses power drops back to the Powered state,
   without address or configuration (USB 1.1 Figure 9-1, p. 176 / PDF 192),
   and answers nothing until it sees a reset (s9.1.1.3, p. 178 / PDF 194):
   usb_dev_bus_reset() clears the address, the configuration and the class
   state, and clearing powered_reset makes the device ignore traffic until
   its next port reset. A hub's reset hook turns off its ports, so the
   devices plugged into an unplugged hub lose power too. */
static void
usb_unplug(usb_dev_t *udev)
{
    usb_detach(udev);
    usb_dev_bus_reset(udev);
    udev->powered_reset = 0;
}

typedef struct usb_slot_t {
    usb_dev_t                *dev; /* emulation thread only */
    _Atomic(const device_t *) info;
    atomic_int                connected; /* on a port */
    atomic_int                online;    /* on a port, and so is every hub above it */
} usb_slot_t;

static const char *const usb_slot_names[USB_SLOTS] = {
    "usb_hub_4port", "usb_mouse", "usb_tablet", "usb_keyboard", "usb_speaker", "usb_msd"
};

static usb_slot_t usb_slots[USB_SLOTS];
/* One bit per slot: the user unplugged that device. */
static atomic_int usb_slot_unplugged;
static atomic_int usb_hotplug_pending;

const char *
usb_slot_internal_name(int slot)
{
    return ((slot >= 0) && (slot < USB_SLOTS)) ? usb_slot_names[slot] : NULL;
}

const device_t *
usb_slot_device(int slot)
{
    return ((slot >= 0) && (slot < USB_SLOTS)) ? atomic_load(&usb_slots[slot].info) : NULL;
}

int
usb_slot_connected(int slot)
{
    return ((slot >= 0) && (slot < USB_SLOTS)) ? atomic_load(&usb_slots[slot].connected) : 0;
}

int
usb_slot_online(int slot)
{
    return ((slot >= 0) && (slot < USB_SLOTS)) ? atomic_load(&usb_slots[slot].online) : 0;
}

/* A device reaches the host only if it and every hub above it are plugged
   in. */
static int
usb_dev_on_bus(const usb_dev_t *dev)
{
    for (; dev != NULL; dev = dev->parent) {
        if (dev->port == NULL)
            return 0;
    }
    return 1;
}

/* Republishes every slot's state for the GUI: one plug or unplug can change
   the devices on a hub too. */
static void
usb_slots_update(void)
{
    for (int i = 0; i < USB_SLOTS; i++) {
        const usb_dev_t *dev = usb_slots[i].dev;

        atomic_store(&usb_slots[i].connected, (dev != NULL) && (dev->port != NULL));
        atomic_store(&usb_slots[i].online, (dev != NULL) && usb_dev_on_bus(dev));
    }
}

int
usb_slot_wanted(int slot)
{
    return ((slot >= 0) && (slot < USB_SLOTS)) ? !(atomic_load(&usb_slot_unplugged) & (1 << slot)) : 0;
}

void
usb_slot_want(int slot, int connected)
{
    if ((slot < 0) || (slot >= USB_SLOTS))
        return;

    if (connected)
        atomic_fetch_and(&usb_slot_unplugged, ~(1 << slot));
    else
        atomic_fetch_or(&usb_slot_unplugged, 1 << slot);
    atomic_store(&usb_hotplug_pending, 1);
}

/* While a request waits for the emulation thread (or the machine is
   paused), the request itself, so that a second click takes it back;
   otherwise whether the device is attached, which shows a plug that found
   no port as unplugged. */
int
usb_slot_requested(int slot)
{
    if (atomic_load(&usb_hotplug_pending))
        return usb_slot_wanted(slot);
    return usb_slot_connected(slot);
}

/* Each request is acted on once: a device that finds no port when plugged
   in is named to the user once, and stays out until the user tries again. */
void
usb_hotplug_process(void)
{
    if (!atomic_exchange(&usb_hotplug_pending, 0))
        return;

    for (int i = 0; i < USB_SLOTS; i++) {
        usb_dev_t *dev = usb_slots[i].dev;

        if (dev == NULL)
            continue;

        if (usb_slot_wanted(i) && (dev->port == NULL)) {
            usb_attach(dev);
        } else if (!usb_slot_wanted(i) && (dev->port != NULL)) {
            usb_unplug(dev);
        }
    }

    usb_slots_update();
}

void
usb_dev_add(usb_dev_t *dev, const device_t *info)
{
    int slot = -1;

    for (int i = 0; i < USB_SLOTS; i++) {
        if ((info != NULL) && (usb_slots[i].dev == NULL) && !strcmp(info->internal_name, usb_slot_names[i])) {
            slot = i;
            break;
        }
    }

    if ((slot < 0) || usb_slot_wanted(slot))
        usb_attach(dev);

    if (slot >= 0) {
        usb_slots[slot].dev = dev;
        usb_slots_update();
        atomic_store(&usb_slots[slot].info, info);
    }
}

/* A hub that leaves the machine is no longer anyone's last port. */
void
usb_dev_remove(usb_dev_t *dev)
{
    if (dev->port != NULL)
        usb_detach(dev);

    for (int i = 0; i < USB_SLOTS; i++) {
        usb_dev_t *other = usb_slots[i].dev;

        if (other == dev) {
            atomic_store(&usb_slots[i].info, NULL);
            usb_slots[i].dev = NULL;
        } else if ((other != NULL) && (other->home_parent == dev)) {
            other->home_parent = NULL;
            other->home_port   = NULL;
        }
    }

    usb_slots_update();
}

/* The INT and SMI outputs: an enabled status bit with MasterInterruptEnable
   set requests an interrupt, on INT with InterruptRouting clear and on SMI
   with it set; OwnershipChange always goes to SMI (OpenHCI 1.0a s6.5,
   p. 105 / PDF 119; s6.5.8, p. 107 / PDF 121). With PCI Memory Access off,
   interrupt generation halts (Appendix A, COMMAND register, p. 134 /
   PDF 148): both outputs are held inactive, and a condition still pending
   is signaled again once Memory Access is back on. */
static void
ohci_update_irq(ohci_t *dev)
{
    uint32_t pending = (dev->mem_access && (dev->inten & OHCI_INT_MIE)) ? (dev->intsts & dev->inten & OHCI_INT_ALL) : 0;
    uint8_t  req     = !!(pending & ~OHCI_INT_OC);
    uint8_t  ir      = !!(dev->control & OHCI_CTL_IR);
    uint8_t  int_lvl = req && !ir;
    uint8_t  smi_lvl = (req && ir) || !!(pending & OHCI_INT_OC);

    if (int_lvl != dev->int_level) {
        dev->int_level = int_lvl;
        if (dev->set_irq != NULL)
            dev->set_irq(dev->irq_priv, int_lvl);
    }
    if (smi_lvl != dev->smi_level) {
        dev->smi_level = smi_lvl;
        if (dev->set_smi != NULL)
            dev->set_smi(dev->smi_priv, smi_lvl);
    }
}

static void
ohci_port_timer_stop(ohci_port_t *p)
{
    timer_stop(&p->timer);
    p->timer_op = OHCI_PORT_TIMER_NONE;
}

/* A change bit set by the root hub raises RootHubStatusChange (OpenHCI 1.0a
   s6.5.7, p. 107 / PDF 121); the HCD's own writes to the status bits do
   not. Outside USBOPERATIONAL the only interrupts possible are
   OwnershipChange and ResumeDetected (s5.1.2, p. 43-44 / PDF 57-58): the
   port's change bit latches, but RootHubStatusChange is not set, then or
   later. */
static void
ohci_port_change(ohci_port_t *p, uint32_t bits)
{
    p->sts |= bits;
    if ((p->hc->control & OHCI_CTL_HCFS) == OHCI_HCFS_OPERATIONAL)
        p->hc->intsts |= OHCI_INT_RHSC;
}

static int
ohci_port_fixed(const ohci_port_t *p)
{
    return !!(p->hc->rh_desc_b & (1 << p->num));
}

static int
ohci_port_powered(const ohci_port_t *p)
{
    return (p->hc->rh_desc_a & OHCI_DA_NPS) || (p->sts & OHCI_PS_PPS);
}

static int
ohci_rh_in_reset(const ohci_t *dev)
{
    return (dev->control & OHCI_CTL_HCFS) == OHCI_HCFS_RESET;
}

static uint32_t
ohci_port_read(const ohci_port_t *p)
{
    uint32_t ret = p->sts;

    if (p->hc->rh_desc_a & OHCI_DA_NPS)
        ret |= OHCI_PS_PPS;
    if (ohci_port_fixed(p))
        ret |= OHCI_PS_CCS;

    return ret;
}

/* Entering USBRESUME, by the HCD or the HC, clears every port's
   PortSuspendStatus (OpenHCI 1.0a s7.4.4, p. 129 / PDF 143). */
static void
ohci_rh_global_resume(ohci_t *dev)
{
    for (int i = 0; i < dev->ndp; i++) {
        ohci_port_t *p = &dev->port[i];

        if (p->timer_op == OHCI_PORT_TIMER_RESUME)
            ohci_port_timer_stop(p);
        p->sts &= ~OHCI_PS_PSS;
    }
}

/* A resume event in USBSUSPEND: the HC moves itself to USBRESUME and sets
   ResumeDetected (OpenHCI 1.0a s6.5.4, p. 106 / PDF 120). The root hub may
   not do so in the first 5 ms of USBSUSPEND, so an event then waits for the
   5 ms to pass (s6.2.3, p. 89 / PDF 103). */
static void
ohci_resume_detected(ohci_t *dev)
{
    if ((dev->control & OHCI_CTL_HCFS) != OHCI_HCFS_SUSPEND)
        return;
    if (dev->suspend_settling) {
        dev->resume_held = 1;
        return;
    }

    dev->control = (dev->control & ~OHCI_CTL_HCFS) | OHCI_HCFS_RESUME;
    dev->intsts |= OHCI_INT_RD;
    ohci_rh_global_resume(dev);
}

static void
ohci_suspend_settled(void *priv)
{
    ohci_t *dev = (ohci_t *) priv;

    dev->suspend_settling = 0;
    if (dev->resume_held) {
        dev->resume_held = 0;
        ohci_resume_detected(dev);
        ohci_update_irq(dev);
    }
}

/* Entering USBSUSPEND starts the 5 ms. Leaving USBSUSPEND drops a held
   event, since only USBSUSPEND turns one into USBRESUME. */
static void
ohci_suspend_settle(ohci_t *dev, int entered)
{
    dev->resume_held      = 0;
    dev->suspend_settling = entered;
    if (entered)
        timer_on_auto(&dev->suspend_timer, 5000.0);
    else
        timer_stop(&dev->suspend_timer);
}

/* With DeviceRemovable set, CSC is set only after a root hub reset (OpenHCI
   1.0a s7.4.4, p. 131 / PDF 145). With DeviceRemoteWakeupEnable set, a
   connect change is a resume event (s7.4.3, p. 127 / PDF 141). */
static void
ohci_port_connect_change(ohci_port_t *p)
{
    if (!ohci_port_fixed(p))
        ohci_port_change(p, OHCI_PS_CSC);
    if (p->hc->rh_status & OHCI_RH_DRWE)
        ohci_resume_detected(p->hc);
}

/* Connect detection: a device is seen on a powered port within NDP, outside
   USBRESET. */
static void
ohci_port_detect(ohci_port_t *p)
{
    if ((p->usb == NULL) || (p->num > p->hc->ndp) || (p->sts & OHCI_PS_CCS) || !ohci_port_powered(p) || ohci_rh_in_reset(p->hc))
        return;

    p->sts |= OHCI_PS_CCS | (p->usb->low_speed ? OHCI_PS_LSDA : 0);
    ohci_port_connect_change(p);
}

/* Power off resets CCS, PES, PSS and PRS (OpenHCI 1.0a s7.4.4, PPS, p. 130 /
   PDF 144). Losing PES to switched-off power sets PESC (PES, p. 129 /
   PDF 143). The device loses its address and configuration. */
static void
ohci_port_power_off(ohci_port_t *p)
{
    ohci_port_timer_stop(p);
    if (p->sts & OHCI_PS_PES)
        ohci_port_change(p, OHCI_PS_PESC);
    p->sts &= ~(OHCI_PS_CCS | OHCI_PS_PES | OHCI_PS_PSS | OHCI_PS_PRS | OHCI_PS_LSDA);
    if (p->usb != NULL) {
        usb_dev_bus_reset(p->usb);
        p->usb->powered_reset = 0;
    }
}

static void
ohci_port_set_power(ohci_port_t *p, int on)
{
    int was = ohci_port_powered(p);

    if (on)
        p->sts |= OHCI_PS_PPS;
    else
        p->sts &= ~OHCI_PS_PPS;

    if (was && !ohci_port_powered(p))
        ohci_port_power_off(p);
    else
        ohci_port_detect(p);
}

/* Set/ClearGlobalPower reach every port in global mode, and in per-port
   mode the ports whose PortPowerControlMask bit is clear (OpenHCI 1.0a
   s7.4.2-7.4.3, p. 126-127 / PDF 140-141). */
static void
ohci_rh_global_power(ohci_t *dev, int on)
{
    for (int i = 0; i < dev->ndp; i++) {
        if (!(dev->rh_desc_a & OHCI_DA_PSM) || !(dev->rh_desc_b & (0x10000 << (i + 1))))
            ohci_port_set_power(&dev->port[i], on);
    }
}

static void
ohci_port_timer(void *priv)
{
    ohci_port_t *p  = (ohci_port_t *) priv;
    int          op = p->timer_op;

    p->timer_op = OHCI_PORT_TIMER_NONE;

    switch (op) {
        case OHCI_PORT_TIMER_RESET:
            /* The end of reset sets PES and clears PSS and PSSC (OpenHCI 1.0a
               s7.4.4, p. 129-131 / PDF 143-145). */
            p->sts = (p->sts & ~(OHCI_PS_PRS | OHCI_PS_PSS | OHCI_PS_PSSC)) | OHCI_PS_PES;
            ohci_port_change(p, OHCI_PS_PRSC);
            break;

        case OHCI_PORT_TIMER_RESUME:
            p->sts = (p->sts & ~OHCI_PS_PSS) | OHCI_PS_PES;
            ohci_port_change(p, OHCI_PS_PSSC);
            break;

        default:
            break;
    }
    ohci_update_irq(p->hc);
}

/* PSS stays set through the resume and clears when PSSC is set at its end
   (OpenHCI 1.0a s7.4.4, p. 129 / PDF 143; p. 131 / PDF 145). */
static void
ohci_port_resume_start(ohci_port_t *p)
{
    p->timer_op = OHCI_PORT_TIMER_RESUME;
    timer_on_auto(&p->timer, OHCI_PORT_RESUME_US);
}

static void
ohci_port_write(ohci_port_t *p, uint32_t val)
{
    const ohci_t *dev       = p->hc;
    int           connected = !!(ohci_port_read(p) & OHCI_PS_CCS);

    if (ohci_rh_in_reset(dev))
        return;

    /* Change bits are cleared first, so a CSC that a command below sets
       survives the write. */
    p->sts &= ~(val & OHCI_PS_CHANGE);

    if (val & OHCI_PS_CCS)
        p->sts &= ~OHCI_PS_PES;

    /* SetPortEnable, SetPortSuspend and SetPortReset on a disconnected port
       set CSC instead (OpenHCI 1.0a s7.4.4, p. 129-131 / PDF 143-145). */
    if (val & (OHCI_PS_PES | OHCI_PS_PSS | OHCI_PS_PRS)) {
        if (!connected)
            ohci_port_change(p, OHCI_PS_CSC);
        else {
            if (val & OHCI_PS_PES)
                p->sts |= OHCI_PS_PES;
            if (val & OHCI_PS_PSS)
                p->sts |= OHCI_PS_PSS;
            if ((val & OHCI_PS_PRS) && !(p->sts & OHCI_PS_PRS)) {
                p->sts |= OHCI_PS_PRS;
                p->timer_op = OHCI_PORT_TIMER_RESET;
                timer_on_auto(&p->timer, OHCI_PORT_RESET_US);
                if (p->usb != NULL)
                    usb_dev_bus_reset(p->usb);
            }
        }
    }

    /* ClearSuspendStatus starts a resume only on a suspended port (OpenHCI
       1.0a s7.4.4, POCI, p. 129 / PDF 143). */
    if ((val & OHCI_PS_POCI) && (p->sts & OHCI_PS_PSS) && (p->timer_op == OHCI_PORT_TIMER_NONE))
        ohci_port_resume_start(p);

    /* Per-port power commands reach a port only in per-port mode with its
       PortPowerControlMask bit set (OpenHCI 1.0a s7.4.4, PPS, p. 130 /
       PDF 144). */
    if (!(dev->rh_desc_a & OHCI_DA_NPS) && (dev->rh_desc_a & OHCI_DA_PSM) && (dev->rh_desc_b & (0x10000 << p->num))) {
        if (val & OHCI_PS_PPS)
            ohci_port_set_power(p, 1);
        if (val & OHCI_PS_LSDA)
            ohci_port_set_power(p, 0);
    }
}

static void
ohci_rh_status_write(ohci_t *dev, uint32_t val)
{
    if (ohci_rh_in_reset(dev))
        return;

    dev->rh_status &= ~(val & OHCI_RH_OCIC);
    if (val & OHCI_RH_LPS)
        ohci_rh_global_power(dev, 0);
    if (val & OHCI_RH_LPSC)
        ohci_rh_global_power(dev, 1);
    if (val & OHCI_RH_DRWE)
        dev->rh_status |= OHCI_RH_DRWE;
    if (val & OHCI_RH_CRWE)
        dev->rh_status &= ~OHCI_RH_DRWE;
}

static void
ohci_rh_detect_all(ohci_t *dev)
{
    for (int i = 0; i < dev->ndp; i++)
        ohci_port_detect(&dev->port[i]);
}

/* The root hub's reset, on entering USBRESET and on a hardware reset
   (OpenHCI 1.0a s5.1.2.1, p. 43 / PDF 57; ALi M1543C datasheet, HcRhStatus
   and HcRhPortStatus, p. 186-187 / PDF 187-188). Reset signaling reaches
   every device, and without NoPowerSwitching the ports also lose power.
   HcRhDescriptorA and B are reset by a hardware reset only. */
static void
ohci_root_hub_reset(ohci_t *dev)
{
    dev->rh_status = 0x00000000;
    for (int i = 0; i < OHCI_MAX_PORTS; i++) {
        ohci_port_t *p = &dev->port[i];

        ohci_port_timer_stop(p);
        p->sts = 0x00000000;
        if (p->usb != NULL) {
            usb_dev_bus_reset(p->usb);
            if (!(dev->rh_desc_a & OHCI_DA_NPS))
                p->usb->powered_reset = 0;
        }
        /* CSC resets to 1 on a non-removable port (ALi M1543C datasheet,
           p. 187 / PDF 188). In USBRESET that raises no RootHubStatusChange
           (OpenHCI 1.0a s5.1.2.1, p. 43 / PDF 57). */
        if ((i < dev->ndp) && ohci_port_fixed(p))
            ohci_port_change(p, OHCI_PS_CSC);
    }
}

/* Entering USBSUSPEND while a port is resuming, or has finished resuming
   with PSSC set, clears that port resume and is a resume event for the HC
   (OpenHCI 1.0a s6.5.4, p. 106 / PDF 120). */
static void
ohci_rh_suspend(ohci_t *dev)
{
    int event = 0;

    for (int i = 0; i < dev->ndp; i++) {
        ohci_port_t *p = &dev->port[i];

        if ((p->timer_op == OHCI_PORT_TIMER_RESUME) || (p->sts & OHCI_PS_PSSC)) {
            if (p->timer_op == OHCI_PORT_TIMER_RESUME)
                ohci_port_timer_stop(p);
            p->sts &= ~OHCI_PS_PSS;
            event = 1;
        }
    }

    if (event)
        ohci_resume_detected(dev);
}

static void
ohci_port_attach(ohci_port_t *p, usb_dev_t *udev)
{
    p->usb     = udev;
    udev->port = p;
    ohci_port_detect(p);
    ohci_update_irq(p->hc);
}

/* A disconnect clears CCS and the port state. Losing PES to a disconnect
   sets PESC (OpenHCI 1.0a s7.4.4, p. 129 / PDF 143). */
static void
ohci_port_detach(ohci_port_t *p)
{
    ohci_port_timer_stop(p);
    if (p->sts & OHCI_PS_CCS) {
        if (p->sts & OHCI_PS_PES)
            ohci_port_change(p, OHCI_PS_PESC);
        p->sts &= ~(OHCI_PS_CCS | OHCI_PS_PES | OHCI_PS_PSS | OHCI_PS_PRS | OHCI_PS_LSDA);
        ohci_port_connect_change(p);
    }
    p->usb = NULL;
    ohci_update_irq(p->hc);
}

/* Nonzero when no SOFs, traffic, reset or resume reach the device on this
   port: the port is disabled or suspended, or the HC is in USBSUSPEND
   (OpenHCI 1.0a s5.1.2, p. 43-44 / PDF 57-58; s6.2.3, p. 89 / PDF 103). */
static int
ohci_port_idle(const ohci_port_t *p)
{
    uint32_t hcfs = p->hc->control & OHCI_CTL_HCFS;

    if (!(p->sts & OHCI_PS_CCS) || (p->sts & OHCI_PS_PRS) || (hcfs == OHCI_HCFS_RESET))
        return 0;
    if (!(p->sts & OHCI_PS_PES))
        return 1;
    if ((hcfs == OHCI_HCFS_RESUME) || (p->timer_op == OHCI_PORT_TIMER_RESUME))
        return 0;
    return (hcfs == OHCI_HCFS_SUSPEND) || (p->sts & OHCI_PS_PSS);
}

/* Resume signaling from the device. In USBSUSPEND it is a resume event for
   the HC (OpenHCI 1.0a s6.5.4, p. 106 / PDF 120; HCFS, p. 111 / PDF 125),
   which neither RemoteWakeupEnable nor DeviceRemoteWakeupEnable gates
   (p. 111 / PDF 125; p. 127 / PDF 141). On a suspended port in
   USBOPERATIONAL the port resumes (USB 1.1 s11.5.1.9, p. 242-243 /
   PDF 258-259). A disabled port ignores it. */
static void
ohci_port_resume(ohci_port_t *p)
{
    if (!ohci_port_idle(p) || !(p->sts & OHCI_PS_PES))
        return;

    if ((p->hc->control & OHCI_CTL_HCFS) == OHCI_HCFS_SUSPEND)
        ohci_resume_detected(p->hc);
    else
        ohci_port_resume_start(p);
    ohci_update_irq(p->hc);
}

int
usb_dev_suspended(const usb_dev_t *udev)
{
    if (udev->parent != NULL)
        return (udev->parent->ops->port_idle != NULL) && udev->parent->ops->port_idle(udev->parent, udev);

    for (int h = 0; h < USB_MAX_HC; h++) {
        uhci_t *hc = (usb_hcs[h] != NULL) ? usb_hcs[h]->uhci : NULL;
        ohci_t *oc = (usb_hcs[h] != NULL) ? usb_hcs[h]->ohci : NULL;

        for (int i = 0; (hc != NULL) && (i < UHCI_PORTS); i++) {
            if (hc->port[i].usb == udev)
                return uhci_port_idle(hc, &hc->port[i]);
        }
        for (int i = 0; (oc != NULL) && (i < OHCI_MAX_PORTS); i++) {
            if (oc->port[i].usb == udev)
                return ohci_port_idle(&oc->port[i]);
        }
    }

    return 0;
}

void
usb_resume_upstream(usb_dev_t *udev)
{
    if (udev->parent != NULL) {
        if (udev->parent->ops->resume != NULL)
            udev->parent->ops->resume(udev->parent, udev);
        return;
    }

    for (int h = 0; h < USB_MAX_HC; h++) {
        uhci_t *hc = (usb_hcs[h] != NULL) ? usb_hcs[h]->uhci : NULL;
        ohci_t *oc = (usb_hcs[h] != NULL) ? usb_hcs[h]->ohci : NULL;

        for (int i = 0; (hc != NULL) && (i < UHCI_PORTS); i++) {
            if (hc->port[i].usb == udev)
                uhci_port_resume(hc, i);
        }
        for (int i = 0; (oc != NULL) && (i < OHCI_MAX_PORTS); i++) {
            if (oc->port[i].usb == udev)
                ohci_port_resume(&oc->port[i]);
        }
    }
}

enum {
    OHCI_ED_NONE = 0, /* nothing sent: skipped, halted or empty ED */
    OHCI_ED_SENT,     /* a transaction was attempted */
    OHCI_ED_NO_TIME,  /* a TD waits, but its packet does not fit the frame */
    OHCI_ED_END       /* the list is disabled or has nothing left to serve */
};

static void
ohci_read_dwords(uint32_t addr, uint32_t *d, int n)
{
    uint8_t b[32];

    dma_bm_read(addr, b, n * 4, 4);
    for (int i = 0; i < n; i++)
        d[i] = b[i * 4] | (b[i * 4 + 1] << 8) | (b[i * 4 + 2] << 16) | ((uint32_t) b[i * 4 + 3] << 24);
}

static void
ohci_write_dword(uint32_t addr, uint32_t val)
{
    uint8_t b[4] = { val & 0xff, (val >> 8) & 0xff, (val >> 16) & 0xff, val >> 24 };

    dma_bm_write(addr, b, 4, 4);
}

/* Tokens go out on root ports that are enabled and neither suspended nor in
   reset. A low-speed token only reaches a low-speed device, or a hub's
   low-speed port behind a preamble. */
static usb_dev_t *
ohci_find_device(const ohci_t *dev, int addr, int low_speed)
{
    for (int i = 0; i < dev->ndp; i++) {
        const ohci_port_t *p = &dev->port[i];
        usb_dev_t         *usb;

        if ((p->usb == NULL) || ((p->sts & (OHCI_PS_CCS | OHCI_PS_PES | OHCI_PS_PSS | OHCI_PS_PRS)) != (OHCI_PS_CCS | OHCI_PS_PES)))
            continue;
        if ((p->usb->addr == addr) && (p->usb->low_speed == low_speed))
            return p->usb;
        if ((p->usb->ops->route != NULL) && ((usb = p->usb->ops->route(p->usb, addr, low_speed)) != NULL))
            return usb;
    }

    return NULL;
}

/* Bytes left in a General TD's buffer: A - B + 1 with the 13-bit terms A
   (from BE, bit 12 set when BE is on another page than CBP) and B (from
   CBP) of OpenHCI 1.0a s6.4.4.2 (p. 101 / PDF 115). A buffer holds 0 to
   8192 bytes (s4.3, p. 19 / PDF 33), so the result needs 14 bits. CBP 0
   means no bytes left (s4.3.1.2, p. 21 / PDF 35). */
static int
ohci_td_remaining(uint32_t cbp, uint32_t be)
{
    uint32_t a = be & 0xfff;

    if (cbp == 0)
        return 0;
    if ((cbp ^ be) & 0xfffff000)
        a |= 0x1000;

    return (a - (cbp & 0xfff) + 1) & 0x3fff;
}

/* n bytes at CBP. Past the end of CBP's 4 KB page they continue at the
   start of BE's page, also within one packet (OpenHCI 1.0a s4.3.1.3.1,
   p. 21 / PDF 35). */
static void
ohci_td_data(uint32_t cbp, uint32_t be, uint8_t *buf, int n, int write)
{
    int first = 0x1000 - (cbp & 0xfff);

    if (n <= 0)
        return;
    if (first > n)
        first = n;

    if (write) {
        dma_bm_write(cbp, buf, first, 1);
        if (n > first)
            dma_bm_write(be & 0xfffff000, buf + first, n - first, 1);
    } else {
        dma_bm_read(cbp, buf, first, 1);
        if (n > first)
            dma_bm_read(be & 0xfffff000, buf + first, n - first, 1);
    }
}

/* The Done Queue Interrupt Counter at a retirement: DelayInterrupt 111b
   leaves it alone, a DI smaller than the counter loads it, and a retirement
   with an error clears it to 0 (OpenHCI 1.0a s6.4.4.6, p. 104 / PDF 118). */
static void
ohci_done_delay(ohci_t *dev, uint32_t dw0)
{
    uint8_t di = (dw0 & OHCI_TD_DI) >> 21;

    if ((dw0 & OHCI_TD_CC) != ((uint32_t) OHCI_CC_NOERROR << 28))
        di = 0;
    if ((di != 7) && (di < dev->di_count))
        dev->di_count = di;
}

/* Retirement: HeadP takes the TD's NextTD, the TD joins the done queue,
   toggleCarry takes the TD's last toggle, and an error halts the ED
   (OpenHCI 1.0a s6.4.4.6, p. 103 / PDF 117; s4.6, p. 37 / PDF 51; s4.3.3.1,
   p. 33 / PDF 47). A TD whose toggle MSb is still 0 never moved data and
   leaves toggleCarry as it was. */
static void
ohci_td_retire(ohci_t *dev, uint32_t ed_addr, uint32_t *ed, uint32_t td_addr, const uint32_t *td)
{
    int carry = (td[0] & 0x02000000) ? !!(td[0] & 0x01000000) : !!(ed[2] & OHCI_ED_C);

    ed[2] = (td[2] & 0xfffffff0) | (carry ? OHCI_ED_C : 0);
    if ((td[0] & OHCI_TD_CC) != ((uint32_t) OHCI_CC_NOERROR << 28))
        ed[2] |= OHCI_ED_H;
    ohci_write_dword(td_addr + 8, dev->done_head);
    dev->done_head = td_addr;
    ohci_write_dword(ed_addr + 8, ed[2]);
    ohci_done_delay(dev, td[0]);
}

/* One transaction attempt on the ED's head General TD (OpenHCI 1.0a s6.4.4,
   p. 99-103 / PDF 113-117). */
static int
ohci_service_gtd(ohci_t *dev, uint32_t ed_addr, uint32_t *ed)
{
    uint32_t     td_addr = ed[2] & 0xfffffff0;
    uint32_t     td[4];
    uint8_t      buf[2048];
    usb_packet_t p;
    usb_dev_t   *usb;
    double       ldpc;
    int          dir;
    int          mps;
    int          ls;
    int          remaining;
    int          size;
    int          toggle;
    int          ret;
    int          ec;
    int          cc;
    int          bytes    = 0;
    int          complete = 0;

    ohci_read_dwords(td_addr, td, 4);
    dev->bits_left -= OHCI_FETCH_BITS;

    /* ED.D, or TD.DP when D is 00b or 11b: 0 SETUP, 1 OUT, 2 IN; DP 11b is
       reserved (OpenHCI 1.0a Tables 4-1 and 4-2, p. 17 / PDF 31; p. 20 /
       PDF 34). */
    dir = (ed[0] >> 11) & 3;
    if ((dir == 0) || (dir == 3))
        dir = (td[0] >> 19) & 3;
    if (dir == 3)
        return OHCI_ED_NONE;

    mps       = (ed[0] >> 16) & 0x7ff;
    ls        = !!(ed[0] & OHCI_ED_S);
    remaining = ohci_td_remaining(td[1], td[3]);
    size      = (remaining < mps) ? remaining : mps;

    /* Nothing starts that could overrun the frame, and a TD not started gets
       no writeback. Low speed is checked against LSThreshold; full speed
       against the Largest Data Packet Counter, which starts each frame at
       FSLargestDataPacket and loses 6 for every 7 bit times (OpenHCI 1.0a
       s6.4.4.3-6.4.4.4, p. 101-102 / PDF 115-116). An IN is checked at
       MaximumPacketSize. */
    if (dev->bits_left < 1.0)
        return OHCI_ED_NO_TIME;
    if (ls) {
        if ((dev->bits_left - 1.0) < (double) dev->ls_threshold)
            return OHCI_ED_NO_TIME;
    } else {
        ldpc = (double) ((dev->fm_interval >> 16) & 0x7fff) - (((double) dev->fr_load + 1.0 - dev->bits_left) * 6.0 / 7.0);
        if ((double) (((dir == 2) ? mps : size) * 8) > ldpc)
            return OHCI_ED_NO_TIME;
    }

    /* The toggle comes from the TD when its MSb is set, else from the ED's
       toggleCarry (OpenHCI 1.0a s4.3.1.2, p. 21 / PDF 35). */
    toggle = (td[0] & 0x02000000) ? ((td[0] >> 24) & 1) : !!(ed[2] & OHCI_ED_C);

    p.pid     = (dir == 0) ? USB_PID_SETUP : ((dir == 1) ? USB_PID_OUT : USB_PID_IN);
    p.ep      = (ed[0] >> 7) & 0x0f;
    p.toggle  = toggle;
    p.data    = buf;
    p.len     = size;
    p.actual  = 0;
    p.bus_len = 0;
    /* An overrun is ACKed, so the toggle advances (OpenHCI 1.0a s4.3.1.3.4,
       p. 23 / PDF 37; s4.3.1.3.6.2, p. 24 / PDF 38). */
    p.ack_overrun = 1;
    p.no_ack      = 0;
    p.hub_cut     = 0;

    if (dir != 2)
        ohci_td_data(td[1], td[3], buf, size, 0);

    usb = ohci_find_device(dev, ed[0] & 0x7f, ls);
    ret = (usb != NULL) ? usb_dev_packet(usb, &p) : USB_RET_NO_RESPONSE;

    /* Bus time from the same USB 1.1 s5.9.3 equations the UHCI side uses,
       with worst-case bit stuffing, in bit times. An IN is charged for the
       packet the device sent, all of it on an overrun (OpenHCI 1.0a
       s4.3.1.3.6.2, p. 24 / PDF 38), not for what the buffer kept. */
    dev->bits_left -= uhci_bus_ns(ls, 0, dir == 2, (dir == 2) ? p.bus_len : size) * 12.0 / 1000.0;

    ec = (td[0] & OHCI_TD_EC) >> 26;
    cc = OHCI_CC_NOERROR;

    switch (ret) {
        case USB_RET_NAK:
            /* NAK leaves every TD field as it was (OpenHCI 1.0a s4.3.1.3.7.1,
               p. 25 / PDF 39). */
            break;

        case USB_RET_STALL:
            /* Retired, and the ED halted, with CBP, EC and T as they were
               (OpenHCI 1.0a s4.3.1.3.7.2, p. 25 / PDF 39). */
            cc       = OHCI_CC_STALL;
            complete = 1;
            break;

        case USB_RET_ACK:
        case USB_RET_OVERRUN:
            /* The toggle advances and EC goes to 0 (OpenHCI 1.0a s4.3.1.2,
               p. 21 / PDF 35; s6.4.4.5.1, p. 102 / PDF 116). IN data past
               the buffer is a data overrun: the data is kept up to the end of
               the buffer and CBP is left at the packet's start
               (s4.3.1.3.6.2, p. 24 / PDF 38). */
            bytes = size;
            if (dir == 2) {
                bytes = p.actual;
                ohci_td_data(td[1], td[3], buf, bytes, 1);
            }
            ec    = 0;
            td[0] = (td[0] & ~OHCI_TD_T) | 0x02000000 | ((toggle ^ 1) << 24);
            if (ret == USB_RET_OVERRUN) {
                cc       = OHCI_CC_DATAOVERRUN;
                complete = 1;
            } else if (bytes == remaining) {
                td[1]    = 0x00000000;
                complete = 1;
            } else {
                if (((td[1] & 0xfff) + bytes) > 0xfff)
                    td[1] = (td[3] & 0xfffff000) | ((td[1] + bytes) & 0xfff);
                else
                    td[1] += bytes;
                /* A short IN packet ends the TD; without bufferRounding it
                   is a data underrun (OpenHCI 1.0a s4.3.1.3.5, p. 23 / PDF 37;
                   s4.3.1.3.6.2, p. 24 / PDF 38). */
                if (bytes < size) {
                    complete = 1;
                    if (!(td[0] & OHCI_TD_R))
                        cc = OHCI_CC_DATAUNDERRUN;
                }
            }
            break;

        default:
            /* Transmission errors: no handshake, or IN data with the wrong
               toggle, which is discarded. The third in a row retires the TD
               (OpenHCI 1.0a s4.3.1.3.6.1, p. 24 / PDF 38). */
            cc       = (ret == USB_RET_TOGGLE_MISMATCH) ? OHCI_CC_DATATOGGLEMISMATCH : OHCI_CC_DEVICENOTRESPONDING;
            complete = (++ec == 3);
            break;
    }

    if (ret != USB_RET_NAK) {
        td[0] = (td[0] & ~(OHCI_TD_EC | OHCI_TD_CC)) | ((uint32_t) ec << 26) | ((uint32_t) cc << 28);
        ohci_write_dword(td_addr, td[0]);
        ohci_write_dword(td_addr + 4, td[1]);
        if (complete)
            ohci_td_retire(dev, ed_addr, ed, td_addr, td);
    }

    return OHCI_ED_SENT;
}

/* One visit of a nonperiodic list (OpenHCI 1.0a s6.4.2-6.4.3, Figures 6-5
   and 6-6, p. 96-99 / PDF 110-113). */
static int
ohci_service_list(ohci_t *dev, int list)
{
    uint32_t *cur    = (list == OHCI_LIST_BULK) ? &dev->bulk_cur : &dev->ctrl_cur;
    uint32_t  head   = (list == OHCI_LIST_BULK) ? dev->bulk_head : dev->ctrl_head;
    uint32_t  filled = (list == OHCI_LIST_BULK) ? OHCI_CS_BLF : OHCI_CS_CLF;
    uint32_t  enable = (list == OHCI_LIST_BULK) ? OHCI_CTL_BLE : OHCI_CTL_CLE;
    uint32_t  ed_addr;
    uint32_t  ed[4];
    int       r;

    if (!(dev->control & enable))
        return OHCI_ED_END;

    /* The first service after entering USBOPERATIONAL starts at the head
       (OpenHCI 1.0a s6.4.2.2, p. 97 / PDF 111). After that, CurrentED 0 is
       the end of the list, and the Filled bit says whether to go round
       again (s6.4.2.2; s7.2.4 and s7.2.6, p. 118-119 / PDF 132-133). */
    if (dev->np_first[list]) {
        dev->np_first[list] = 0;
        *cur                = head;
    } else if (*cur == 0) {
        if (!(dev->cmdsts & filled))
            return OHCI_ED_END;
        *cur = head;
        dev->cmdsts &= ~filled;
    }
    if (*cur == 0)
        return OHCI_ED_END;

    ed_addr = *cur;
    ohci_read_dwords(ed_addr, ed, 4);
    dev->bits_left -= OHCI_FETCH_BITS;

    /* sKip, Halted or no TD: nothing is sent (OpenHCI 1.0a s6.4.3, p. 99 /
       PDF 113). An ED with the isochronous format bit belongs on the
       periodic list only and is passed over here. */
    if ((ed[0] & (OHCI_ED_K | OHCI_ED_F)) || (ed[2] & OHCI_ED_H) || ((ed[1] & 0xfffffff0) == (ed[2] & 0xfffffff0))) {
        *cur = ed[3] & 0xfffffff0;
        return OHCI_ED_NONE;
    }

    /* A TD found marks the list filled (OpenHCI 1.0a s4.5, p. 36 / PDF 50;
       Figure 6-6, p. 98 / PDF 112). A packet that does not fit ends only
       this ED's service, and the next ED may still fit (Figures 6-5 and
       6-7, p. 96 / PDF 110 and p. 100 / PDF 114). */
    dev->cmdsts |= filled;
    r    = ohci_service_gtd(dev, ed_addr, ed);
    *cur = ed[3] & 0xfffffff0;

    return r;
}

/* The nonperiodic lists: CBSR + 1 control EDs served, then one bulk ED
   visit. The count carries across frames, and an empty list yields to the
   other (OpenHCI 1.0a s6.4.1.1.2, p. 93-94 / PDF 107-108). A control ED
   counts when a transaction was attempted or its packet did not fit. The
   count is checked against the current CBSR before each service (Figure
   6-5, p. 96 / PDF 110), so a CBSR change applies at once. Runs until the
   time or the work is gone. */
static void
ohci_run_nonperiodic(ohci_t *dev, double stop)
{
    int idle = 0;

    while ((dev->bits_left > stop) && (idle < 2)) {
        int list;
        int r;

        if ((dev->np_list == OHCI_LIST_CONTROL) && (dev->ctrl_served > (int) (dev->control & OHCI_CTL_CBSR)))
            dev->np_list = OHCI_LIST_BULK;
        list = dev->np_list;
        r    = ohci_service_list(dev, list);

        idle = (r == OHCI_ED_END) ? (idle + 1) : 0;

        if (list == OHCI_LIST_CONTROL) {
            if ((r == OHCI_ED_SENT) || (r == OHCI_ED_NO_TIME))
                dev->ctrl_served++;
            else if (r == OHCI_ED_END)
                dev->np_list = OHCI_LIST_BULK;
        } else {
            dev->np_list     = OHCI_LIST_CONTROL;
            dev->ctrl_served = 0;
        }
    }
}

/* An isochronous TD retires without touching toggleCarry (OpenHCI 1.0a
   Table 4-1, p. 17 / PDF 31) and never halts the ED (s4.3.3.1, p. 33 /
   PDF 47). */
static void
ohci_itd_retire(ohci_t *dev, uint32_t ed_addr, uint32_t *ed, uint32_t td_addr, const uint32_t *td)
{
    ed[2] = (td[2] & 0xfffffff0) | (ed[2] & OHCI_ED_C);
    ohci_write_dword(td_addr + 8, dev->done_head);
    dev->done_head = td_addr;
    ohci_write_dword(ed_addr + 8, ed[2]);
    ohci_done_delay(dev, td[0]);
}

/* The Isochronous TD at the ED's head (OpenHCI 1.0a s4.3.2, p. 25-31 /
   PDF 39-45). Of its FrameCount + 1 packets, packet R = FrameNumber -
   StartingFrame is sent once, in its own frame. */
static int
ohci_service_itd(ohci_t *dev, uint32_t ed_addr, uint32_t *ed)
{
    uint32_t     td_addr;
    uint32_t     td[8];
    uint32_t     start;
    uint32_t     end;
    uint32_t     s13;
    uint32_t     e13;
    uint32_t     off;
    uint8_t      buf[2048];
    uint8_t      psw[2];
    usb_packet_t p;
    usb_dev_t   *usb;
    double       ldpc;
    int          dir = (ed[0] >> 11) & 3;
    int          mps = (ed[0] >> 16) & 0x7ff;
    int          r;
    int          fc;
    int          size;
    int          ret;
    int          cc;
    int          len = 0;

    /* An isochronous TD has no direction field, so the ED's D must give one
       (OpenHCI 1.0a Table 4-1, p. 17 / PDF 31; Figure 4-3, p. 25 /
       PDF 39). */
    if ((dir != 1) && (dir != 2))
        return OHCI_ED_NONE;

    for (;;) {
        td_addr = ed[2] & 0xfffffff0;
        if (td_addr == (ed[1] & 0xfffffff0))
            return OHCI_ED_NONE;

        ohci_read_dwords(td_addr, td, 8);
        dev->bits_left -= OHCI_FETCH_BITS;

        r  = (int16_t) (dev->fm_number - (td[0] & 0xffff));
        fc = (td[0] >> 24) & 7;
        if (r < 0)
            return OHCI_ED_NONE;
        if (r <= fc)
            break;

        /* Expired: retired with DATAOVERRUN, the ED not halted, and the next
           TD of the ED is processed (OpenHCI 1.0a s4.3.2.3.5.3, p. 29 /
           PDF 43). */
        td[0] = (td[0] & ~OHCI_TD_CC) | ((uint32_t) OHCI_CC_DATAOVERRUN << 28);
        ohci_write_dword(td_addr, td[0]);
        ohci_itd_retire(dev, ed_addr, ed, td_addr, td);
    }

    /* The packet's buffer runs from Offset[R] to Offset[R+1] - 1, or to BE
       for the last packet. Bit 12 of an offset picks BP0's page (0) or BE's
       (1) (OpenHCI 1.0a s4.3.2.3.1, p. 27 / PDF 41). */
    off   = (td[4 + (r >> 1)] >> ((r & 1) << 4)) & 0x1fff;
    s13   = off;
    start = ((off & 0x1000) ? (td[3] & 0xfffff000) : (td[1] & 0xfffff000)) | (off & 0xfff);
    if (r < fc) {
        e13 = (((td[4 + ((r + 1) >> 1)] >> (((r + 1) & 1) << 4)) & 0x1fff) - 1) & 0x1fff;
        end = ((e13 & 0x1000) ? (td[3] & 0xfffff000) : (td[1] & 0xfffff000)) | (e13 & 0xfff);
    } else {
        e13 = (((td[3] ^ td[1]) & 0xfffff000) ? 0x1000 : 0) | (td[3] & 0xfff);
        end = td[3];
    }
    size = (e13 + 1 - s13) & 0x1fff;
    if (size > (int) sizeof(buf))
        size = sizeof(buf);

    if (dev->bits_left < 1.0)
        return OHCI_ED_NO_TIME;
    ldpc = (double) ((dev->fm_interval >> 16) & 0x7fff) - (((double) dev->fr_load + 1.0 - dev->bits_left) * 6.0 / 7.0);
    if ((double) (((dir == 2) ? mps : size) * 8) > ldpc)
        return OHCI_ED_NO_TIME;

    p.pid    = (dir == 1) ? USB_PID_OUT : USB_PID_IN;
    p.ep     = (ed[0] >> 7) & 0x0f;
    p.toggle = 0;
    p.data   = buf;
    p.len    = (dir == 1) ? size : ((mps > size) ? mps : size);
    p.actual = 0;
    p.ack_overrun = 1;
    p.no_ack      = 0;
    p.hub_cut     = 0;
    if (p.len > (int) sizeof(buf))
        p.len = sizeof(buf);

    if (dir == 1)
        ohci_td_data(start, end, buf, size, 0);

    usb = ohci_find_device(dev, ed[0] & 0x7f, 0);
    ret = (usb != NULL) ? usb_dev_packet(usb, &p) : USB_RET_NO_RESPONSE;
    dev->bits_left -= uhci_bus_ns(0, 1, dir == 2, (dir == 2) ? p.actual : size) * 12.0 / 1000.0;

    /* The PacketStatusWord (OpenHCI 1.0a s4.3.2.3.3-4.3.2.4, p. 28-31 /
       PDF 42-45): an OUT has no handshake, so it ends without error; an IN
       reports what arrived, up to the size of the buffer. NAK or STALL on
       an IN reads as STALL with size 0. */
    if (dir == 1)
        cc = OHCI_CC_NOERROR;
    else if ((ret == USB_RET_ACK) || (ret == USB_RET_OVERRUN)) {
        len = p.actual;
        if (len > size) {
            len = size;
            cc  = OHCI_CC_DATAOVERRUN;
        } else
            cc = (len < size) ? OHCI_CC_DATAUNDERRUN : OHCI_CC_NOERROR;
        ohci_td_data(start, end, buf, len, 1);
    } else if ((ret == USB_RET_NAK) || (ret == USB_RET_STALL))
        cc = OHCI_CC_STALL;
    else
        cc = OHCI_CC_DEVICENOTRESPONDING;

    psw[0] = len & 0xff;
    psw[1] = (cc << 4) | ((len >> 8) & 0x07);
    dma_bm_write(td_addr + 16 + (r << 1), psw, 2, 2);

    /* The last packet completes the TD with NOERROR (OpenHCI 1.0a
       s4.3.2.3.4, p. 28 / PDF 42). */
    if (r == fc) {
        td[0] = (td[0] & ~OHCI_TD_CC) | ((uint32_t) OHCI_CC_NOERROR << 28);
        ohci_write_dword(td_addr, td[0]);
        ohci_itd_retire(dev, ed_addr, ed, td_addr, td);
    }

    return OHCI_ED_SENT;
}

/* The periodic list: the interrupt list of this frame from the HCCA, with
   the isochronous EDs at its tail, followed to its end in one pass
   (OpenHCI 1.0a Figure 6-5 and s6.4.2.2, p. 96-97 / PDF 110-111). If it
   cannot finish in the frame, that is a scheduling overrun (s6.5.1,
   p. 105 / PDF 119). */
static void
ohci_run_periodic(ohci_t *dev)
{
    uint32_t ed_addr;
    uint32_t ed[4];
    uint32_t so_ed = 0;
    int      r;

    if (!(dev->control & OHCI_CTL_PLE))
        return;

    ohci_read_dwords(dev->hcca + ((dev->fm_number & 0x1f) << 2), &ed_addr, 1);
    dev->bits_left -= OHCI_FETCH_BITS;
    ed_addr &= 0xfffffff0;
    dev->period_cur = ed_addr;

    while (ed_addr != 0) {
        if (dev->bits_left < 1.0) {
            dev->so_pending = 1;
            if (so_ed == 0)
                so_ed = ed_addr;
            break;
        }

        ohci_read_dwords(ed_addr, ed, 4);
        dev->bits_left -= OHCI_FETCH_BITS;

        /* With IsochronousEnable clear the list ends at its first
           isochronous ED (OpenHCI 1.0a s6.4.2.1, p. 95 / PDF 109). */
        if ((ed[0] & OHCI_ED_F) && !(dev->control & OHCI_CTL_IE))
            break;

        if (!(ed[0] & OHCI_ED_K) && !(ed[2] & OHCI_ED_H) && ((ed[1] & 0xfffffff0) != (ed[2] & 0xfffffff0))) {
            /* A packet that does not fit is an overrun, but only this ED's
               service ends, and later EDs may still fit (OpenHCI 1.0a
               Figures 6-5 and 6-7, p. 96 / PDF 110 and p. 100 / PDF 114). */
            r = (ed[0] & OHCI_ED_F) ? ohci_service_itd(dev, ed_addr, ed) : ohci_service_gtd(dev, ed_addr, ed);
            if ((r == OHCI_ED_NO_TIME) && (so_ed == 0)) {
                dev->so_pending = 1;
                so_ed           = ed_addr;
            }
        }

        ed_addr         = ed[3] & 0xfffffff0;
        dev->period_cur = ed_addr;
    }
}

/* The frame's schedule, run at its boundary against its bit time: the
   nonperiodic lists while FrameRemaining > PeriodicStart, the periodic list
   to completion, then the nonperiodic lists again (OpenHCI 1.0a s6.4.1,
   p. 92-93 / PDF 106-107). With Bus Master Enable clear no descriptor is
   read (Appendix A, COMMAND register, p. 134 / PDF 148). */
static void
ohci_run_frame(ohci_t *dev)
{
    if (!dev->bus_master)
        return;

    /* The frame opens with its SOF, and FrameRemaining counts from the
       first bit of its SYNC (OpenHCI 1.0a s6.3.1-6.3.2, p. 90-91 /
       PDF 104-105). The partial frame begun on entering USBOPERATIONAL has
       no SOF (s6.2.1, p. 88 / PDF 102) and runs no lists. */
    dev->bits_left = (double) dev->fr_load + 1.0 - OHCI_SOF_BITS;
    ohci_run_nonperiodic(dev, (double) dev->periodic_start + 1.0);
    /* Idle nonperiodic lists end early, but the periodic list still waits
       for FrameRemaining to reach PeriodicStart (OpenHCI 1.0a s7.3.4,
       p. 122 / PDF 136). */
    if (dev->bits_left > ((double) dev->periodic_start + 1.0))
        dev->bits_left = (double) dev->periodic_start + 1.0;
    ohci_run_periodic(dev);
    ohci_run_nonperiodic(dev, 0.0);
}

/* A frame is FrameInterval + 1 bit times of 12 MHz (OpenHCI 1.0a s6.3.1,
   p. 90 / PDF 104). */
static double
ohci_frame_us(uint32_t fi)
{
    return ((double) (fi & 0x3fff) + 1.0) / 12.0;
}

/* FrameRemaining from the frame timer: FrameInterval in the first bit time
   of the frame, 0 in the last (OpenHCI 1.0a s6.3.1, p. 90 / PDF 104). */
static uint32_t
ohci_fr_live(ohci_t *dev)
{
    double   bits = ((double) timer_get_remaining_u64(&dev->frame_timer) / (double) TIMER_USEC) * 12.0;
    uint32_t fr   = (bits >= 1.0) ? (uint32_t) ceil(bits) - 1 : 0;

    return (fr > dev->fr_load) ? dev->fr_load : fr;
}

/* FrameRemaining counts down only in USBOPERATIONAL and holds otherwise
   (ALi M1543C datasheet, HcFrameRemaining, p. 182 / PDF 183). */
static uint32_t
ohci_fm_remaining(ohci_t *dev)
{
    if ((dev->control & OHCI_CTL_HCFS) != OHCI_HCFS_OPERATIONAL)
        return dev->fm_remaining;

    return (dev->fm_remaining & 0x80000000) | ohci_fr_live(dev);
}

/* Entering USBOPERATIONAL loads FrameRemaining and increments FrameNumber,
   with no SOF; the first SOF is at the next frame boundary (OpenHCI 1.0a
   s6.2.1, p. 88 / PDF 102). */
static void
ohci_frame_start(ohci_t *dev)
{
    dev->fr_load      = dev->fm_interval & 0x3fff;
    dev->fm_remaining = (dev->fm_remaining & 0x80000000) | dev->fr_load;
    dev->fm_number    = (dev->fm_number + 1) & 0xffff;
    /* Nonperiodic service starts with the full control count (OpenHCI 1.0a
       s6.4.1.1.2, p. 94 / PDF 108), each list from its head (s6.4.2.2,
       p. 97 / PDF 111). The Done Queue Interrupt Counter starts at 111b
       (s6.4.5.1, p. 104 / PDF 118). */
    dev->np_list     = OHCI_LIST_CONTROL;
    dev->ctrl_served = 0;
    dev->np_first[0] = dev->np_first[1] = 1;
    dev->di_count    = 7;
    timer_on_auto(&dev->frame_timer, ohci_frame_us(dev->fr_load));
}

static void
ohci_frame_stop(ohci_t *dev)
{
    if (!(dev->frame_timer.flags & TIMER_ENABLED))
        return;

    dev->fm_remaining = (dev->fm_remaining & 0x80000000) | ohci_fr_live(dev);
    timer_stop(&dev->frame_timer);
}

/* The done queue at a frame boundary, judged by the counter as it stood in
   the frame's last bit time (OpenHCI 1.0a s6.4.5-6.4.5.1, p. 104-105 /
   PDF 118-119). At 0 with WritebackDoneHead clear, HccaDoneHead takes
   HcDoneHead, with its LSb set when another enabled status bit is set
   (s4.4.2.3, p. 35 / PDF 49). Otherwise the counter counts down; at 111b
   or 0 it holds. */
static void
ohci_done_writeback(ohci_t *dev)
{
    uint32_t head;
    uint8_t  b[4];

    if (dev->di_count != 0) {
        if (dev->di_count != 7)
            dev->di_count--;
        return;
    }
    if ((dev->intsts & OHCI_INT_WDH) || !dev->bus_master)
        return;

    head = dev->done_head;
    if (dev->intsts & dev->inten & OHCI_INT_ALL & ~OHCI_INT_WDH)
        head |= 1;
    b[0] = head & 0xff;
    b[1] = (head >> 8) & 0xff;
    b[2] = (head >> 16) & 0xff;
    b[3] = head >> 24;
    dma_bm_write(dev->hcca + 0x84, b, 4, 4);
    dev->done_head = 0x00000000;
    dev->intsts |= OHCI_INT_WDH;
    dev->di_count = 7;
}

/* The frame boundary, where FrameRemaining goes from 0 to FrameInterval
   (OpenHCI 1.0a s6.3.1, p. 90 / PDF 104). */
static void
ohci_frame_boundary(void *priv)
{
    ohci_t *dev = (ohci_t *) priv;
    uint8_t hcca_fn[4];

    /* FrameRemaining reloads with the FrameInterval in force now, and FRT
       takes FIT (OpenHCI 1.0a s7.3.2, p. 121 / PDF 135). */
    dev->fr_load      = dev->fm_interval & 0x3fff;
    dev->fm_remaining = (dev->fm_interval & 0x80000000) | dev->fr_load;
    dev->fm_number    = (dev->fm_number + 1) & 0xffff;
    timer_on_auto(&dev->frame_timer, ohci_frame_us(dev->fr_load));

    /* SOF, then HccaFrameNumber with HccaPad1 = 0 before the first ED of
       the frame, then StartOfFrame (OpenHCI 1.0a s4.4, p. 34-35 /
       PDF 48-49; s6.3.1, p. 90 / PDF 104). Without Bus Master Enable there
       is no HCCA write (Appendix A, p. 134 / PDF 148). */
    if (dev->bus_master) {
        hcca_fn[0] = dev->fm_number & 0xff;
        hcca_fn[1] = dev->fm_number >> 8;
        hcca_fn[2] = hcca_fn[3] = 0x00;
        dma_bm_write(dev->hcca + 0x80, hcca_fn, 4, 4);
    }
    /* FrameNumberOverflow: bit 15 changed since the last update (OpenHCI
       1.0a s6.5.6, p. 106 / PDF 120). */
    if ((dev->fm_number & 0x8000) != dev->fn_msb) {
        dev->fn_msb = dev->fm_number & 0x8000;
        dev->intsts |= OHCI_INT_FNO;
    }
    /* SchedulingOverrun of the last frame, set after the HccaFrameNumber
       update, and its 2-bit count (OpenHCI 1.0a s6.5.1, p. 105 / PDF 119;
       s7.1.3, p. 113 / PDF 127). */
    if (dev->so_pending) {
        dev->so_pending = 0;
        dev->intsts |= OHCI_INT_SO;
        dev->cmdsts = (dev->cmdsts & ~OHCI_CS_SOC) | ((dev->cmdsts + 0x00010000) & OHCI_CS_SOC);
    }
    dev->intsts |= OHCI_INT_SF;
    ohci_done_writeback(dev);

    ohci_run_frame(dev);
    ohci_update_irq(dev);
}

/* HostControllerReset: the HC goes to USBSUSPEND and the operational
   registers to their reset values (OpenHCI 1.0a s7.1.3, p. 112 / PDF 126),
   FrameInterval to nominal (s7.3.1, p. 121 / PDF 135), except
   InterruptRouting and RemoteWakeupConnected (s7.1.2, p. 111 / PDF 125).
   The root hub keeps its contents (Appendix A, p. 132 / PDF 146), and so
   do the legacy support registers. */
static void
ohci_soft_reset(ohci_t *dev)
{
    timer_stop(&dev->frame_timer);
    dev->so_pending     = 0;
    dev->di_count       = 7;
    dev->fn_msb         = 0x0000;
    dev->fr_load        = 0x0000;
    dev->control        = (dev->control & (OHCI_CTL_IR | OHCI_CTL_RWC)) | OHCI_HCFS_SUSPEND;
    ohci_suspend_settle(dev, 1);
    dev->cmdsts         = 0x00000000;
    dev->intsts         = 0x00000000;
    dev->inten          = 0x00000000;
    dev->hcca           = 0x00000000;
    dev->period_cur     = 0x00000000;
    dev->ctrl_head      = 0x00000000;
    dev->ctrl_cur       = 0x00000000;
    dev->bulk_head      = 0x00000000;
    dev->bulk_cur       = 0x00000000;
    dev->done_head      = 0x00000000;
    dev->fm_interval    = OHCI_FI_NOMINAL;
    dev->fm_remaining   = 0x00000000;
    dev->fm_number      = 0x00000000;
    dev->periodic_start = 0x00000000;
    /* 0628h in the spec (OpenHCI 1.0a s7.3.5, p. 123 / PDF 137); ALi and
       SiS reset it to 0. */
    dev->ls_threshold = (dev->chip_flags & OHCI_CHIP_LST_0628H) ? 0x00000628 : 0x00000000;
}

static void
ohci_hard_reset(ohci_t *dev)
{
    ohci_soft_reset(dev);
    dev->control   = OHCI_HCFS_RESET;
    ohci_suspend_settle(dev, 0);
    dev->rh_desc_a = 0x01000000 | dev->ndp;
    dev->rh_desc_b = 0x00000000;
    ohci_root_hub_reset(dev);
    dev->hce_control = 0x00000000;
    dev->hce_input   = 0x00;
    dev->hce_output  = 0x00;
    dev->hce_status  = 0x00;
}

static void
ohci_control_write(ohci_t *dev, uint32_t val)
{
    uint32_t old  = dev->control;
    uint32_t mask = OHCI_CTL_CBSR | OHCI_CTL_PLE | OHCI_CTL_IE | OHCI_CTL_CLE | OHCI_CTL_BLE | OHCI_CTL_HCFS | OHCI_CTL_IR | OHCI_CTL_RWE;

    /* RemoteWakeupConnected is hard 0 on ALi, SiS and SMSC (ALi M1543C
       datasheet, p. 178 / PDF 179; SiS 5595 datasheet V2.0, p. 175 /
       PDF 181; SMSC SLC90E66 datasheet, p. 105 / PDF 105).
       RemoteWakeupEnable is read-only on SiS (SiS 5595, p. 174 / PDF 180). */
    if (dev->chip_flags & OHCI_CHIP_RWC)
        mask |= OHCI_CTL_RWC;
    if (dev->chip_flags & OHCI_CHIP_RWE_RO)
        mask &= ~OHCI_CTL_RWE;
    dev->control = val & mask;

    /* OwnershipChangeRequest clears after the changeover (OpenHCI 1.0a
       s7.1.3, p. 113 / PDF 127), which is the InterruptRouting change that
       hands the HC over (s5.1.1.3.3, p. 41-42 / PDF 55-56; s7.1.2, p. 111 /
       PDF 125). */
    if (((dev->control ^ old) & OHCI_CTL_IR) && (dev->cmdsts & OHCI_CS_OCR)) {
        dev->cmdsts &= ~OHCI_CS_OCR;
    }

    if ((dev->control ^ old) & OHCI_CTL_HCFS) {
        if ((old & OHCI_CTL_HCFS) == OHCI_HCFS_OPERATIONAL)
            ohci_frame_stop(dev);
        else if ((dev->control & OHCI_CTL_HCFS) == OHCI_HCFS_OPERATIONAL)
            ohci_frame_start(dev);
        ohci_suspend_settle(dev, (dev->control & OHCI_CTL_HCFS) == OHCI_HCFS_SUSPEND);
        switch (dev->control & OHCI_CTL_HCFS) {
            case OHCI_HCFS_RESET:
                /* The root hub is held in reset while in USBRESET (OpenHCI
                   1.0a s5.1.2.1, p. 43 / PDF 57). */
                ohci_root_hub_reset(dev);
                break;
            case OHCI_HCFS_RESUME:
                ohci_rh_global_resume(dev);
                break;
            case OHCI_HCFS_SUSPEND:
                ohci_rh_suspend(dev);
                break;
            default:
                break;
        }
        if ((old & OHCI_CTL_HCFS) == OHCI_HCFS_RESET)
            ohci_rh_detect_all(dev);
    }
}

/* EmulationInterrupt, a static decode of the synchronous and asynchronous
   emulation interrupt conditions (OpenHCI 1.0a Appendix B, Implementation
   Notes, p. 146 / PDF 160). */
static int
ohci_emulation_interrupt(const ohci_t *dev)
{
    uint32_t ctl = dev->hce_control;

    if ((ctl & OHCI_HCE_EE) && (ctl & OHCI_HCE_CP) && !(dev->hce_status & OHCI_HCES_OUTFULL))
        return 1;
    if ((ctl & OHCI_HCE_EE) && (dev->hce_status & OHCI_HCES_INFULL))
        return 1;
    return (ctl & OHCI_HCE_EXTIRQEN) && (ctl & (OHCI_HCE_IRQ1ACT | OHCI_HCE_IRQ12ACT));
}

static uint32_t
ohci_reg_read(ohci_t *dev, uint32_t off)
{
    uint32_t n;

    switch (off) {
        case OHCI_HCREVISION:
            /* Revision 1.0, legacy support registers present (OpenHCI 1.0a
               s7.1.1, p. 109 / PDF 123). */
            return 0x00000110;
        case OHCI_HCCONTROL:
            return dev->control;
        case OHCI_HCCOMMANDSTATUS:
            return dev->cmdsts;
        case OHCI_HCINTERRUPTSTATUS:
            return dev->intsts;
        case OHCI_HCINTERRUPTENABLE:
        case OHCI_HCINTERRUPTDISABLE:
            return dev->inten;
        case OHCI_HCHCCA:
            return dev->hcca;
        case OHCI_HCPERIODCURRENTED:
            return dev->period_cur;
        case OHCI_HCCONTROLHEADED:
            return dev->ctrl_head;
        case OHCI_HCCONTROLCURRENTED:
            return dev->ctrl_cur;
        case OHCI_HCBULKHEADED:
            return dev->bulk_head;
        case OHCI_HCBULKCURRENTED:
            return dev->bulk_cur;
        case OHCI_HCDONEHEAD:
            return dev->done_head;
        case OHCI_HCFMINTERVAL:
            return dev->fm_interval;
        case OHCI_HCFMREMAINING:
            return ohci_fm_remaining(dev);
        case OHCI_HCFMNUMBER:
            return dev->fm_number;
        case OHCI_HCPERIODICSTART:
            return dev->periodic_start;
        case OHCI_HCLSTHRESHOLD:
            return dev->ls_threshold;
        case OHCI_HCRHDESCRIPTORA:
            return dev->rh_desc_a;
        case OHCI_HCRHDESCRIPTORB:
            return dev->rh_desc_b;
        case OHCI_HCRHSTATUS:
            return dev->rh_status;
        case OHCI_HCECONTROL:
            return dev->hce_control | (ohci_emulation_interrupt(dev) ? OHCI_HCE_EI : 0);
        case OHCI_HCEINPUT:
            return dev->hce_input;
        case OHCI_HCEOUTPUT:
            return dev->hce_output;
        case OHCI_HCESTATUS:
            return dev->hce_status;
        default:
            n = (off - OHCI_HCRHPORTSTATUS1) >> 2;
            if ((off >= OHCI_HCRHPORTSTATUS1) && (n < (uint32_t) dev->ndp))
                return ohci_port_read(&dev->port[n]);
            /* The rest of the 4 KB window need not respond (OpenHCI 1.0a
               Appendix A, BAR_OHCI, p. 135 / PDF 149). */
            return 0x00000000;
    }
}

static uint32_t
ohci_merge(uint32_t cur, uint32_t val, uint32_t lanes)
{
    return (cur & ~lanes) | (val & lanes);
}

static uint32_t
ohci_inten_mask(const ohci_t *dev)
{
    uint32_t mask = OHCI_INT_ALL | OHCI_INT_MIE;

    return (dev->chip_flags & OHCI_CHIP_UE_RW) ? mask : (mask & ~OHCI_INT_UE);
}

/* lanes selects the bytes the guest wrote (the PCI byte enables): R/W bits
   in them are merged, and unwritten bytes write 0 to the write-1 command
   bits. */
static void
ohci_reg_write(ohci_t *dev, uint32_t off, uint32_t val, uint32_t lanes)
{
    uint32_t cmd = val & lanes;
    uint32_t ports;
    uint32_t n;
    int      was_reset;

    switch (off) {
        case OHCI_HCCONTROL:
            ohci_control_write(dev, ohci_merge(dev->control, val, lanes));
            break;

        case OHCI_HCCOMMANDSTATUS:
            if (cmd & OHCI_CS_HCR) {
                /* The reset completes at once, so the self-clearing bit
                   already reads 0. */
                was_reset = ohci_rh_in_reset(dev);
                ohci_soft_reset(dev);
                /* Out of USBRESET, the root hub is no longer held in reset. */
                if (was_reset)
                    ohci_rh_detect_all(dev);
                break;
            }
            dev->cmdsts |= cmd & (OHCI_CS_CLF | OHCI_CS_BLF | OHCI_CS_OCR);
            /* OwnershipChange is tied to 0 without an SMI pin (OpenHCI 1.0a
               s7.1.4, p. 114 / PDF 128). */
            if ((cmd & OHCI_CS_OCR) && !(dev->chip_flags & OHCI_CHIP_NO_SMI)) {
                dev->intsts |= OHCI_INT_OC;
            }
            break;

        case OHCI_HCINTERRUPTSTATUS:
            cmd &= OHCI_INT_ALL;
            if (cmd & dev->intsts) {
                dev->intsts &= ~cmd;
            }
            break;

        /* UnrecoverableError is not implemented. Its enable bit ignores
           writes on ALi and SMSC (ALi M1543C datasheet, p. 179 / PDF 180;
           SMSC SLC90E66 datasheet, p. 107 / PDF 107) but is R/W on SiS and
           STPC (SiS 5595 datasheet V2.0, p. 180 / PDF 186; STPC Atlas
           programming manual, p. 575 / PDF 575). */
        case OHCI_HCINTERRUPTENABLE:
            dev->inten |= cmd & ohci_inten_mask(dev);
            break;
        case OHCI_HCINTERRUPTDISABLE:
            dev->inten &= ~(cmd & ohci_inten_mask(dev));
            break;

        case OHCI_HCHCCA:
            dev->hcca = ohci_merge(dev->hcca, val, lanes) & 0xffffff00;
            break;
        /* The current and done pointers are R/W on ALi, SiS, SMSC and OPTi
           (OPTi 82C861 data book, p. 25-26 / PDF 33-34). The
           spec makes HcPeriodCurrentED and HcDoneHead read-only to the HCD
           (OpenHCI 1.0a s7.2.2, p. 117 / PDF 131; s7.2.7, p. 120 / PDF 134),
           and that holds on STPC, whose programming manual defers to the
           spec (p. 575 / PDF 575). */
        case OHCI_HCPERIODCURRENTED:
            if (!(dev->chip_flags & OHCI_CHIP_PTRS_RO))
                dev->period_cur = ohci_merge(dev->period_cur, val, lanes) & 0xfffffff0;
            break;
        case OHCI_HCCONTROLHEADED:
            dev->ctrl_head = ohci_merge(dev->ctrl_head, val, lanes) & 0xfffffff0;
            break;
        case OHCI_HCCONTROLCURRENTED:
            dev->ctrl_cur = ohci_merge(dev->ctrl_cur, val, lanes) & 0xfffffff0;
            break;
        case OHCI_HCBULKHEADED:
            dev->bulk_head = ohci_merge(dev->bulk_head, val, lanes) & 0xfffffff0;
            break;
        case OHCI_HCBULKCURRENTED:
            dev->bulk_cur = ohci_merge(dev->bulk_cur, val, lanes) & 0xfffffff0;
            break;
        case OHCI_HCDONEHEAD:
            if (!(dev->chip_flags & OHCI_CHIP_PTRS_RO))
                dev->done_head = ohci_merge(dev->done_head, val, lanes) & 0xfffffff0;
            break;

        case OHCI_HCFMINTERVAL:
            dev->fm_interval = ohci_merge(dev->fm_interval, val, lanes) & 0xffff3fff;
            break;
        case OHCI_HCPERIODICSTART:
            dev->periodic_start = ohci_merge(dev->periodic_start, val, lanes) & 0x00003fff;
            break;
        case OHCI_HCLSTHRESHOLD:
            dev->ls_threshold = ohci_merge(dev->ls_threshold, val, lanes) & 0x00000fff;
            break;

        case OHCI_HCRHDESCRIPTORA:
            /* Writable: PSM, NPS, OCPM and NOCP, and of POTPGT either bits
               25:24 only (ALi M1543C datasheet, p. 184 / PDF 185; SMSC
               SLC90E66 datasheet, p. 111 / PDF 111) or the whole byte (OPTi
               82C861 data book, p. 27 / PDF 35; SiS 5595 datasheet V2.0,
               p. 187 / PDF 193; STPC, whose programming
               manual follows the spec, OpenHCI 1.0a s7.4.1, p. 125 /
               PDF 139). A NoPowerSwitching change powers ports on or off. */
            ports = 0;
            for (int i = 0; i < dev->ndp; i++)
                ports |= ohci_port_powered(&dev->port[i]) << i;
            dev->rh_desc_a = (ohci_merge(dev->rh_desc_a, val, lanes) &
                              ((dev->chip_flags & OHCI_CHIP_POTPGT_RW) ? 0xff001b00 : 0x03001b00)) | dev->ndp;
            for (int i = 0; i < dev->ndp; i++) {
                if ((ports & (1 << i)) && !ohci_port_powered(&dev->port[i]))
                    ohci_port_power_off(&dev->port[i]);
                else
                    ohci_port_detect(&dev->port[i]);
            }
            break;
        case OHCI_HCRHDESCRIPTORB:
            /* Bit 0 of each field is reserved, and so are the bits of ports
               past NDP (OpenHCI 1.0a s7.4.2, p. 126 / PDF 140). */
            ports          = ((1u << dev->ndp) - 1) << 1;
            dev->rh_desc_b = ohci_merge(dev->rh_desc_b, val, lanes) & (ports | (ports << 16));
            break;

        case OHCI_HCRHSTATUS:
            ohci_rh_status_write(dev, cmd);
            break;

        case OHCI_HCECONTROL:
            /* IRQ1Active and IRQ12Active are write-1-to-clear; EmulationInterrupt
               is read-only (OpenHCI 1.0a Table B-7, p. 145 / PDF 159). */
            dev->hce_control = (ohci_merge(dev->hce_control, val, lanes) & 0x0000013d) |
                               (dev->hce_control & ~cmd & (OHCI_HCE_IRQ1ACT | OHCI_HCE_IRQ12ACT));
            break;
        case OHCI_HCEINPUT:
            if (lanes & 0xff)
                dev->hce_input = val;
            break;
        case OHCI_HCEOUTPUT:
            if (lanes & 0xff)
                dev->hce_output = val;
            break;
        case OHCI_HCESTATUS:
            if (lanes & 0xff)
                dev->hce_status = val;
            break;

        default:
            n = (off - OHCI_HCRHPORTSTATUS1) >> 2;
            if ((off >= OHCI_HCRHPORTSTATUS1) && (n < (uint32_t) dev->ndp))
                ohci_port_write(&dev->port[n], cmd);
            /* Read-only registers and the rest of the window take no write. */
            break;
    }

    ohci_update_irq(dev);
}

static uint8_t
ohci_mmio_readb(uint32_t addr, void *priv)
{
    ohci_t  *dev = (ohci_t *) priv;
    uint32_t off = addr & 0xfff;
    uint8_t  ret = ohci_reg_read(dev, off & 0xffc) >> ((off & 3) << 3);

    return ret;
}

static uint16_t
ohci_mmio_readw(uint32_t addr, void *priv)
{
    ohci_t  *dev = (ohci_t *) priv;
    uint32_t off = addr & 0xfff;
    uint16_t ret;

    if ((off & 3) == 3)
        ret = (ohci_reg_read(dev, off & 0xffc) >> 24) | (ohci_reg_read(dev, (off + 1) & 0xffc) << 8);
    else
        ret = ohci_reg_read(dev, off & 0xffc) >> ((off & 3) << 3);

    return ret;
}

static uint32_t
ohci_mmio_readl(uint32_t addr, void *priv)
{
    ohci_t  *dev = (ohci_t *) priv;
    uint32_t off = addr & 0xffc;
    uint32_t ret = ohci_reg_read(dev, off);

    return ret;
}

static void
ohci_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    ohci_t  *dev = (ohci_t *) priv;
    uint32_t off = addr & 0xfff;
    int      sh  = (off & 3) << 3;

    ohci_reg_write(dev, off & 0xffc, (uint32_t) val << sh, 0xffu << sh);
}

static void
ohci_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    ohci_t  *dev = (ohci_t *) priv;
    uint32_t off = addr & 0xfff;
    int      sh  = (off & 3) << 3;

    if ((off & 3) == 3) {
        ohci_reg_write(dev, off & 0xffc, (uint32_t) val << 24, 0xff000000);
        ohci_reg_write(dev, (off + 1) & 0xffc, val >> 8, 0x000000ff);
    } else
        ohci_reg_write(dev, off & 0xffc, (uint32_t) val << sh, 0xffffu << sh);
}

static void
ohci_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    ohci_t  *dev = (ohci_t *) priv;
    uint32_t off = addr & 0xffc;

    ohci_reg_write(dev, off, val, 0xffffffff);
}

void
ohci_update_mem_mapping(usb_t *dev, uint8_t base1, uint8_t base2, uint8_t base3, int enable)
{
    if (dev->ohci_enable && (dev->ohci_mem_base != 0x00000000))
        mem_mapping_disable(&dev->ohci_mmio_mapping);

    dev->ohci_mem_base = ((base1 << 8) | (base2 << 16) | ((uint32_t) base3 << 24)) & 0xfffff000;
    dev->ohci_enable   = enable;

    if (dev->ohci_enable && (dev->ohci_mem_base != 0x00000000))
        mem_mapping_set_addr(&dev->ohci_mmio_mapping, dev->ohci_mem_base, 0x1000);

    dev->ohci->mem_access = !!enable;
    ohci_update_irq(dev->ohci);
}

void
ohci_set_irq_handler(usb_t *dev, void (*set_irq)(void *priv, int level), void *priv)
{
    dev->ohci->set_irq  = set_irq;
    dev->ohci->irq_priv = priv;
}

void
ohci_set_smi_handler(usb_t *dev, void (*set_smi)(void *priv, int level), void *priv)
{
    dev->ohci->set_smi  = set_smi;
    dev->ohci->smi_priv = priv;
}

void
ohci_set_bus_master(usb_t *dev, int enable)
{
    dev->ohci->bus_master = !!enable;
}

void
ohci_hardwire_bus_master(usb_t *dev)
{
    dev->ohci->bus_master_hardwired = 1;
    dev->ohci->bus_master           = 1;
}

void
ohci_set_chip_flags(usb_t *dev, uint32_t flags)
{
    dev->ohci->chip_flags = flags;
    /* Called after usb_init's reset, so LSThreshold takes its chip reset
       value here. RemoteWakeupConnected and OwnershipChange reset to 0 on
       every chip, so they need nothing. */
    dev->ohci->ls_threshold = (flags & OHCI_CHIP_LST_0628H) ? 0x00000628 : 0x00000000;
}

/* Only NDP HcRhPortStatus registers are implemented (OpenHCI 1.0a s7.4.4,
   p. 128 / PDF 142). A port dropped past NDP goes to its reset value with
   no change reported, its device unpowered but still plugged in. A port
   brought in starts from reset with its PortPowerControlMask bit clear, so
   it follows the global power switch (s7.4.2, p. 126 / PDF 140). */
void
ohci_set_ndp(usb_t *dev, int ndp, int ports)
{
    ohci_t  *oc  = dev->ohci;
    int      old = oc->ndp;
    uint32_t pps = 0;
    uint32_t mask;

    oc->ports = ports;
    if (ndp == old)
        return;

    for (int i = 0; i < old; i++) {
        if (!(oc->rh_desc_a & OHCI_DA_PSM) || !(oc->rh_desc_b & (0x10000 << (i + 1)))) {
            pps = oc->port[i].sts & OHCI_PS_PPS;
            break;
        }
    }
    for (int i = ndp; i < old; i++) {
        ohci_port_timer_stop(&oc->port[i]);
        oc->port[i].sts = 0x00000000;
        if (oc->port[i].usb != NULL) {
            usb_dev_bus_reset(oc->port[i].usb);
            oc->port[i].usb->powered_reset = 0;
        }
    }

    oc->ndp       = ndp;
    mask          = ((1u << ndp) - 1) << 1;
    oc->rh_desc_a = (oc->rh_desc_a & ~0xffu) | ndp;
    oc->rh_desc_b &= mask | (mask << 16);
    for (int i = old; i < ndp; i++) {
        oc->port[i].sts = pps;
        ohci_port_detect(&oc->port[i]);
    }
    ohci_update_irq(oc);
}

static void
usb_reset(void *priv)
{
    usb_t *dev = (usb_t *) priv;

    uhci_power_on_reset(dev->uhci);
    /* PCICMD resets to 0; GRESET leaves PCI configuration alone. */
    dev->uhci->bus_master = 0;
    /* Devices stay on the wire through a chip reset and are detected again
       (UHCI 1.1 s2.1.7, p. 16 / PDF 22). */
    uhci_schedule_detect(dev->uhci);

    ohci_hard_reset(dev->ohci);
    dev->ohci->bus_master = dev->ohci->bus_master_hardwired;
    dev->ohci->mem_access = 0;
    ohci_update_irq(dev->ohci);

    if (dev->uhci_enable && (dev->uhci_io_base != 0x0000))
        io_removehandler(dev->uhci_io_base, 0x20, uhci_inb, uhci_inw, uhci_inl, uhci_outb, uhci_outw, uhci_outl, dev->uhci);
    dev->uhci_enable = 0;

    mem_mapping_disable(&dev->ohci_mmio_mapping);
    dev->ohci_enable = 0;
}

static void
usb_close(void *priv)
{
    usb_t *dev = (usb_t *) priv;

    for (int h = 0; h < USB_MAX_HC; h++) {
        if (usb_hcs[h] == dev)
            usb_hcs[h] = NULL;
    }
    for (int i = 0; i < UHCI_PORTS; i++) {
        if (dev->uhci->port[i].usb != NULL)
            dev->uhci->port[i].usb->port = NULL;
    }
    for (int i = 0; i < OHCI_MAX_PORTS; i++) {
        if (dev->ohci->port[i].usb != NULL)
            dev->ohci->port[i].usb->port = NULL;
        timer_disable(&dev->ohci->port[i].timer);
    }
    timer_disable(&dev->ohci->frame_timer);
    timer_disable(&dev->ohci->suspend_timer);

    timer_disable(&dev->uhci->frame_timer);
    timer_disable(&dev->uhci->detect_timer);
    timer_disable(&dev->uhci->fgr_timer);
    for (int i = 0; i < UHCI_PORTS; i++)
        timer_disable(&dev->uhci->port[i].eop_timer);
    free(dev->uhci);
    free(dev->ohci);
    free(dev);
}

static void *
usb_init(UNUSED(const device_t *info))
{
    usb_t *dev;

    dev = (usb_t *) calloc(1, sizeof(usb_t));
    if (dev == NULL)
        return (NULL);
    memset(dev, 0x00, sizeof(usb_t));

    dev->uhci = (uhci_t *) calloc(1, sizeof(uhci_t));
    timer_add(&dev->uhci->frame_timer, uhci_frame_end, dev->uhci, 0);
    timer_add(&dev->uhci->detect_timer, uhci_detect, dev->uhci, 0);
    timer_add(&dev->uhci->fgr_timer, uhci_fgr_eop_done, dev->uhci, 0);
    for (int i = 0; i < UHCI_PORTS; i++) {
        dev->uhci->port[i].hc  = dev->uhci;
        dev->uhci->port[i].num = i;
        timer_add(&dev->uhci->port[i].eop_timer, uhci_rd_eop_done, &dev->uhci->port[i], 0);
    }

    dev->ohci        = (ohci_t *) calloc(1, sizeof(ohci_t));
    dev->ohci->ndp   = 2;
    dev->ohci->ports = 2;
    timer_add(&dev->ohci->frame_timer, ohci_frame_boundary, dev->ohci, 0);
    timer_add(&dev->ohci->suspend_timer, ohci_suspend_settled, dev->ohci, 0);
    for (int i = 0; i < OHCI_MAX_PORTS; i++) {
        dev->ohci->port[i].num = i + 1;
        dev->ohci->port[i].hc  = dev->ohci;
        timer_add(&dev->ohci->port[i].timer, ohci_port_timer, &dev->ohci->port[i], 0);
    }

    /* A board uses the UHCI or the OHCI of its usb_t, so they share a bus number. */
    for (int h = 0; h < USB_MAX_HC; h++) {
        if (usb_hcs[h] == NULL) {
            usb_hcs[h] = dev;
            break;
        }
    }

    mem_mapping_add(&dev->ohci_mmio_mapping, 0, 0,
                    ohci_mmio_readb, ohci_mmio_readw, ohci_mmio_readl,
                    ohci_mmio_writeb, ohci_mmio_writew, ohci_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev->ohci);
    usb_reset(dev);

    return dev;
}

const device_t usb_device = {
    .name          = "Universal Serial Bus",
    .internal_name = "usb",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = usb_init,
    .close         = usb_close,
    .reset         = usb_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
