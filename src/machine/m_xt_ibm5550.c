/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the IBM 5550 machine.
 * 
 *          The IBM 5550 was launched with three models:
 *            5551-Axx: 12" monochrome CRT with 16x16 font
 *                       (replaced by model D)
 *            5551-Bxx: 15" monochrome CRT with 24x24 font
 *                       (replaced by model G, J, M)
 *            5551-Cxx: 14" color CRT with 16x16 font
 *                       (replaced by model E, H, K, P)
 *          These first-gen models have 1-3 DSQD diskette drives.
 *          You need select "Type: 5.25" 720k" in the Settings dialog - Floppy & CD-ROM drives.
 * 
 *          Currently, this module supports model A and B configurations without hard disk.
 * 
 * Authors: Akamaki.
 *
 *          Copyright 2026 Akamaki.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/ppi.h>
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/nvr.h>
#include <86box/keyboard.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/serial.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>
#define EMU_DEVICE_H
#include <86box/pit.h>
#include <86box/mouse.h>

// #define EPOCH_FONTROM_SIZE         (1024 * 1024)
// #define EPOCH_FONTROM_MASK         0xffff
// #define EPOCH_FONTROM_BASESBCS     0x98000
#define EPOCH_VRAM_SBCS        0x38000
#define EPOCH_VRAM_SBEX        0x30000
#define EPOCH_INVALIDACCESS8       0xffu
#define EPOCH_INVALIDACCESS16      0xffffu
#define EPOCH_INVALIDACCESS32      0xffffffffu
#define EPOCH_SIZE_VRAM            (256 * 1024) /* 0x40000 */
#define EPOCH_SIZE_CRAM            (4 * 1024)    /* 0x1000 */
#define EPOCH_MASK_A000            0x1ffff       /* 0x1FFFF */
#define EPOCH_MASK_CRAM            0xfff         /* 0xFFF */
#define EPOCH_MASK_VRAM            0x3ffff       /* 0xFFFFF */
// #define EPOCH_MASK_VRAMPLANE       0x1ffff       /* 0x1FFFF */
#define EPOCH_PIXELCLOCK24         40000000.0    /* 40 MHz interlaced */
#define EPOCH_PIXELCLOCK16         20000000.0    /* 20 MHz interlaced (not confirmed) */
#define EPOCH_CONFIG_MONO16 0 /* Model 5551-Axx (Font 16, monochrome) */
#define EPOCH_CONFIG_MONO24 1 /* Model 5551-Bxx (Font 24, monochrome) */

#define LC_INDEX                0x3D0
#define LC_DATA                 0x3D1
#define LC_HORIZONTAL_TOTAL     0x00 /* -1 */
#define LC_HORIZONTAL_DISPLAYED 0x01
#define LC_H_SYNC_POSITION      0x02
#define LC_SYNC_WIDTH           0x03
#define LC_VERTICAL_TOTAL       0x04 /* -1 */
#define LC_V_TOTAL_ADJUST       0x05
#define LC_VERTICAL_DISPLAYED   0x06
#define LC_V_SYNC_POSITION      0x07
#define LC_INTERLACE_AND_SKEW   0x08
#define LC_MAXIMUM_SCAN_LINE    0x09 /* -1 */
#define LC_CURSOR_ROW_START     0x0A
#define LC_CURSOR_ROW_END       0x0B
#define LC_START_ADDRESS_HIGH   0x0C
#define LC_START_ADDRESS_LOW    0x0D
#define LC_CURSOR_LOC_HIGH      0x0E
#define LC_CURSOR_LOC_LOWJ      0x0F
#define LC_LIGHT_PEN_HIGH       0x10
#define LC_LIGHT_PEN_LOW        0x11
#define LS_ENABLE               0x3D2
#define LS_DISABLE              0x3D3
#define LS_MODE                 0x3D8
#define LS_MONSENSE             0x3DA
// #define LV_PORT                 0x3E8
// #define LV_PALETTE_0            0x00
// #define LV_MODE_CONTROL         0x10
// #define LV_OVERSCAN_COLOR       0x11
// #define LV_COLOR_PLANE_ENAB     0x12
// #define LV_PANNING              0x13
// #define LV_VIEWPORT1_BG         0x14
// #define LV_VIEWPORT2_BG         0x15
// #define LV_VIEWPORT3_BG         0x16
// #define LV_BLINK_COLOR          0x17
// #define LV_BLINK_CODE           0x18
// #define LV_GR_CURSOR_ROTATION   0x19
// #define LV_GR_CURSOR_COLOR      0x1A
// #define LV_GR_CURSOR_CONTROL    0x1B
// #define LV_COMMAND              0x1C
// #define LV_VP_BORDER_LINE       0x1D
// #define LV_SYNC_POLARITY        0x1F
// #define LV_CURSOR_CODE_0        0x20
// #define LV_GRID_COLOR_0         0x34
// #define LV_GRID_COLOR_1         0x35
// #define LV_GRID_COLOR_2         0x36
// #define LV_GRID_COLOR_3         0x37
// #define LV_ATTRIBUTE_CNTL       0x38
// #define LV_CURSOR_COLOR         0x3A
// #define LV_CURSOR_CONTROL       0x3B
// #define LV_RAS_STATUS_VIDEO     0x3C
// #define LV_PAS_STATUS_CNTRL     0x3D
// #define LV_IDENTIFICATION       0x3E
// #define LV_OUTPUT               0x3E
// #define LV_COMPATIBILITY        0x3F

#define TIMER_CTR_0 0 /* DMA */
#define TIMER_CTR_1 1 /* PIT */
#define TIMER_CTR_2 2 /* Speaker */

#define EPOCH_IRQ3_BIT (1 << 3) /* Keyboard */
#define EPOCH_IRQ6_BIT (1 << 6) /* PIT */

enum epoch_nvr_ADDR {
    epoch_nvr_SECOND1,
    epoch_nvr_SECOND10,
    epoch_nvr_MINUTE1,
    epoch_nvr_MINUTE10,
    epoch_nvr_HOUR1,
    epoch_nvr_HOUR10,
    epoch_nvr_WEEKDAY,
    epoch_nvr_DAY1,
    epoch_nvr_DAY10,
    epoch_nvr_MONTH1,
    epoch_nvr_MONTH10,
    epoch_nvr_YEAR1,
    epoch_nvr_YEAR10,
    epoch_nvr_UNKOWN_D,
    epoch_nvr_UNKOWN_E,
    epoch_nvr_UNKOWN_F,
    epoch_nvr_CONTROL /* Internal data for configuration port (I/O 360h) */
};

#ifndef RELEASE_BUILD
// #define ENABLE_EPOCH_LOG 1
#endif

#ifdef ENABLE_EPOCH_LOG
// #define ENABLE_EPOCH_DEBUGIO 1
#define ENABLE_EPOCH_DEBUGKBD 1
int epoch_do_log = ENABLE_EPOCH_LOG;

static void
epoch_log(const char *fmt, ...)
{
    va_list ap;

    if (epoch_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define epoch_log(fmt, ...)
#endif
#ifdef ENABLE_EPOCH_DEBUGIO
#    define epoch_iolog epoch_log
#else
#    define epoch_iolog(fmt, ...)
#endif
#ifdef ENABLE_EPOCH_DEBUGKBD
#    define epoch_kbdlog epoch_log
#else
#    define epoch_kbdlog(fmt, ...)
#endif


typedef struct epoch_t {
    mem_mapping_t cmapping;

    uint16_t crtc[32];
    // uint16_t crtc_vpreg[128];
    // uint8_t  crtc_vpsel;
    uint8_t  crtmode;
    // uint8_t  attrc[0x40];
    // int      attraddr, attrff;
    // int      attr_palette_enable;
    int      outflipflop;
    int      inflipflop;
    int      iolatch;

    int crtcaddr;

    uint8_t cgastat;

    // int      writemode, readplane;
    // uint8_t  planemask;

    uint8_t  egapal[16];
    uint32_t pallook[64];
    PALETTE  vgapal;

    int      vtotal, dispend, vsyncstart, vblankstart;
    int      hdisp, htotal, hdisp_time, rowoffset;
    int      lowres;
    int      rowcount;
    double   clock;
    uint32_t memaddr_latch, ca_adj;

    uint64_t   dispontime, dispofftime;
    pc_timer_t timer;
    uint64_t   epochconst;

    int dispon;
    int hdisp_on;

    uint32_t memaddr, memaddr_backup, cursoraddr;
    int      vc;
    int      scanline;
    int      linepos, vslines, linecountff;
    int      cursorvisible, cursoron, blink;
    int      scrollcache;
    int      char_width;
    int      font24;
    double   pixelclock;

    int firstline, lastline;
    int firstline_draw, lastline_draw;
    int displine;
    int oddeven;

    /* Attribute Buffer E0000-E0FFFh (4 KB) */
    uint8_t *cram;
    /* SBCS Font Buffer D8000-DBFFFh (16 KB) */
    /* APA Buffer A0000-DFFFFh (256 KB) */
    uint8_t *vram;
    mem_mapping_t cmap, vmap, paritymap;
    /* Font ROM card option (?KB) */
    // struct {
    //     int           bank;
    //     mem_mapping_t map;
    //     uint8_t      *rom;
    //     int           charset;
    //     int           portdata;
    // } fontcard;
    // uint8_t *changedvram;
    uint32_t vram_display_mask;

    int parityerror;
    int parityenabled;
    uint8_t parityerroraddr;
    int lowmemorydisabled;
    int crtioenabled;

    int fullchange;

    void (*render)(struct epoch_t *epoch);

    nvr_t nvr;
    int nvrctrl;
    int nvrdata;

    int testmode;
    
} epoch_t;

static void     epoch_recalctimings(epoch_t *epoch);
static void     epoch_reset(void *priv);

/*
[IRQ]
The IBM 5550 has different IRQ assignments like the 6580 Displaywriter System.

| IRQ | 5550       | Displaywriter                      | PC/XT            |
| --- | ---------- | ---------------------------------- | ---------------- |
| 0   | ?          | Incoming data for printer sharing  | Timer            |
| 1   | Async Comm | Transfer data to commo data link   | Keyboard         |
| 2   | Fixed Disk | Printer and Mag Card data transfer | Reserved         |
| 3   | Keyboard   | Keyboard incoming data             | Async Comm (Sec) |
| 4   | Diskette   | Diskette interrupt                 | Async Comm (Pri) |
| 5   | ?          | Not used                           | Fixed Disk       |
| 6   | Timer      | Software timer                     | Diskette         |
| 7   | ?          | Error on commo data link           | Printer          |

[Memory map]
| Start Address | Function                                              | 
| ------------- | ----------------------------------------------------- | 
| 0             | 256 KB RAM on System Board                            | 
| 40000h        | 128 KB Expansion RAM Card                             | 
| 60000h        | 128 KB Expansion RAM Card                             | 
| 80000h        | 128 KB Expansion RAM Card                             | 
| A0000h        | Video RAM (Font 16: 144 KB, Font 24: 256 KB)          | 
| E0000h        | 4 KB Code/Attribute Buffer                            | 
| E8000h        | ? KB Hard Disk Control Local Memory (not implemented) | 
| F0000h        | Kanji Font Card (not implemented)                     | 
| FC000h        | ROM                                                   | 
*/

#ifdef ENABLE_EPOCH_LOG
// #include <ctype.h>
// static int dumpno = 0x61;
// static void
// epoch_dumpvram(void *priv)
// {
//             FILE *fp;
//     epoch_t *epoch = (epoch_t *) priv;
//             char str1[64] = "epoch_vramvm_";
//             char str2[3] = {0x30, 0x30, 0};
//             if (!isalnum(dumpno))
//                return;
//             str2[0] = dumpno;
//             dumpno++;
//             str2[1] = (epoch->crtmode & 0xf) + 0x30;
//             strcat(str1,str2);
//             fp = fopen(str1, "wb");
//             if (fp != NULL) {
//                 fwrite(epoch->vram, EPOCH_SIZE_VRAM, 1, fp);
//                 fclose(fp);
//             }
// }
#endif

static void
epoch_out(uint16_t addr, uint16_t val, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    epoch_iolog("%04X:%04X epoch Out addr %03X val %02X\n", cs >> 4, cpu_state.pc, addr, val);
    switch (addr) {
        case LC_INDEX:
            epoch->crtcaddr = val;
            break;
        case LC_DATA:
            if (epoch->crtcaddr > 0x1f)
                return;
            if (epoch->crtioenabled == 0)
                return;
            if (!(epoch->crtcaddr == LC_CURSOR_LOC_HIGH || epoch->crtcaddr == LC_CURSOR_LOC_LOWJ))
                epoch_iolog("%04X:%04X epoch Out addr %03X idx %02X val %02X (%d)\n", cs >> 4, cpu_state.pc, addr, epoch->crtcaddr, val, val);
            if (!(epoch->crtc[epoch->crtcaddr] ^ val))
                return;
            // switch (epoch->crtcaddr) {
            //     // case LC_CRTC_OVERFLOW:
            //     //     // return;
            //     //     break;
            //     case LC_MAXIMUM_SCAN_LINE:
            //         // if (!(epoch->ioctl[LS_MODE] & 0x01)) /* 16 or 256 color graphics mode */
            //         //     val = 0;
            //         break;
            //     // case LC_VERTICAL_TOTALJ:      /* Vertical Total */
            //     //     break;
            // }
            switch (epoch->crtcaddr) {
                case LC_START_ADDRESS_HIGH:
                case LC_CURSOR_LOC_HIGH:
                case LC_LIGHT_PEN_HIGH:
                    val &= 0x3F; /* this is required for the IPL BAT test */
                    break;
            }
            epoch->crtc[epoch->crtcaddr] = val;
            switch (epoch->crtcaddr) {
                case LC_HORIZONTAL_DISPLAYED:
                case LC_VERTICAL_DISPLAYED:
                case LC_MAXIMUM_SCAN_LINE:
                case LC_START_ADDRESS_HIGH:
                case LC_START_ADDRESS_LOW:
                    epoch->fullchange = 3;
                    epoch_recalctimings(epoch);
                    break;
                default:
                    break;
            }
            break;
        case LS_ENABLE:
            // mem_mapping_disable(&epoch->paritymap);
            epoch->crtioenabled = 1;
            mem_mapping_enable(&epoch->cmap);
            mem_mapping_enable(&epoch->vmap);
            break;
        case LS_DISABLE:
            epoch->crtioenabled = 0;
            mem_mapping_disable(&epoch->cmap);
            mem_mapping_disable(&epoch->vmap);
            // mem_mapping_enable(&epoch->paritymap);
            break;
        case LS_MODE:
            /* Bit 3: Video output enable, Bit 1: Graphic mode (switch 16 / 9 bit word in Font 16 system) */
            epoch->crtmode = val;
#ifdef ENABLE_EPOCH_LOG
            // epoch_dumpvram(epoch);
#endif
            epoch_recalctimings(epoch);
            // epoch->attrff ^= 1;
            break;
        // case LV_PORT:
        //     // epoch_iolog("epoch Out addr %03X val %02X ff %d %04X:%04X\n", addr, val, epoch->attrff,cs >> 4, cpu_state.pc);
        //     if (!epoch->attrff) {
        //         epoch->attraddr = val & 0x3f;
        //         if ((val & 0x20) != (epoch->attr_palette_enable & 0x20)) {
        //             epoch->fullchange          = 3;
        //             epoch->attr_palette_enable = val & 0x20;
        //             epoch_recalctimings(epoch);
        //         }
        //         // epoch_iolog("set attraddr: %X\n", epoch->attraddr);
        //     } else {
        //         if ((epoch->attraddr == LV_PANNING) && (epoch->attrc[LV_PANNING] != val))
        //             epoch->fullchange = changeframecount;
        //         if (epoch->attrc[epoch->attraddr & 0x3f] != val)
        //             epoch_iolog("attr changed %x: %x -> %x\n", epoch->attraddr & 0x3f, epoch->attrc[epoch->attraddr & 0x3f], val);
        //         epoch->attrc[epoch->attraddr & 0x3f] = val;
        //         // epoch_iolog("set attrc %x: %x\n", epoch->attraddr & 31, val);
        //         if (epoch->attraddr < 16)
        //             epoch->fullchange = changeframecount;
        //         if (epoch->attraddr == LV_MODE_CONTROL || epoch->attraddr < 0x10) {
        //             for (uint8_t c = 0; c < 16; c++) {
        //                 // if (epoch->attrc[LV_MODE_CONTROL] & 0x80)
        //                 //     epoch->egapal[c] = epoch->attrc[c] & 0xf;
        //                 // else
        //                 //     epoch->egapal[c] = epoch->attrc[c] & 0x3f;
        //             }
        //         }
        //         switch (epoch->attraddr) {
        //             case LV_COLOR_PLANE_ENAB:
        //                 if ((val & 0xff) != epoch->plane_mask)
        //                     epoch->fullchange = changeframecount;
        //                 epoch->plane_mask = val & 0xff;
        //                 break;
        //             case LV_CURSOR_CONTROL:
        //                 switch (val & 0x18) {
        //                     case 0x08: /* fast blink */
        //                         epoch->blinkconf = 0x10;
        //                         break;
        //                     case 0x18: /* slow blink */
        //                         epoch->blinkconf = 0x20;
        //                         break;
        //                     default: /* no blink */
        //                         epoch->blinkconf = 0xff;
        //                         break;
        //                 }
        //                 break;
        //             case LV_MODE_CONTROL:
        //             case LV_ATTRIBUTE_CNTL:
        //             case LV_COMPATIBILITY:
        //                 epoch_recalctimings(epoch);
        //                 break;
        //             default:
        //                 break;
        //         }
        //     }
        //     epoch->attrff ^= 1;
        //     break;
        // case LV_PORT+1:
        //     /* VZ Editor's CURSOR.COM writes data via this port */
        //     epoch->attrc[epoch->attraddr & 0x3f] = val;
        //     break;
        default:
            // epoch_iolog("epoch? Out addr %03X val %02X\n", addr, val);
            break;
    }
}

static uint16_t
epoch_in(uint16_t addr, void *priv)
{
    epoch_t   *epoch  = (epoch_t *) priv;
    uint16_t temp = 0xff;

    switch (addr) {
        case LC_INDEX:
            temp = epoch->crtcaddr;
            break;
        case LC_DATA:
            if (epoch->crtcaddr > 0x1f)
                return EPOCH_INVALIDACCESS16;
            if (epoch->crtioenabled == 0)
                return EPOCH_INVALIDACCESS16;
            temp = epoch->crtc[epoch->crtcaddr];
            break;
        // case LV_PORT:
        //     temp = epoch->attraddr | epoch->attr_palette_enable;
        //     break;
        // case LV_PORT + 1:
        //     switch (epoch->attraddr) {
        //         case LV_RAS_STATUS_VIDEO: /* this maybe equivalent to 3ba / 3da ISR1 */
        //             if (epoch->cgastat & 0x01)
        //                 epoch->cgastat &= ~0x30;
        //             else
        //                 epoch->cgastat ^= 0x30; /* toggle */
        //             if (epoch->cgastat & 0x08)
        //                 epoch->cgastat &= ~0x08;
        //             else
        //                 epoch->cgastat ^= 0x08; /* toggle */
        //             temp = epoch->cgastat;
        //             break;
        //         case LV_IDENTIFICATION:
        //             temp = 0x28;
        //             break;
        //         default:
        //             temp = epoch->attrc[epoch->attraddr];
        //             break;
        //     }
        //     // epoch_iolog("epoch In %04X(%02X) %04X %04X:%04X\n", addr, epoch->attraddr, temp, cs >> 4, cpu_state.pc);
        //     epoch->attrff = 0; /* reset flipflop (VGA does not reset flipflop) */
        //     break;
        case LS_MONSENSE:
            temp = 0xff;
            if (!(epoch->crtmode & 0x08)) {/* The video out is active */
                if(epoch->cgastat & 8)
                    temp &= 0x7f;
            }
            temp |= 0x01; /* monitor mono or !color */
            // temp &= 0xfe; /* color */
            break;
        }
    if (addr != LS_MONSENSE)
        epoch_iolog("%04X:%04X epoch In %04X %04X\n", cs >> 4, cpu_state.pc, addr, temp);
    return temp;
}
/*
 * Write I/O
 * out b(idx), out b(data), out b(data)
 * out b(idx), out w(data)
 * out b(idx), out w(data), out b(data)
 *             out w(idx)
 * Read I/O
 * out b(idx), in b(data)
 * out b(idx), in b, in b(data)
 * out b(idx), in w(data)
 */
static void
epoch_outb(uint16_t addr, uint8_t val, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    // epoch_iolog("%04X:%04X epoch Outb addr %03X val %02X es:di=%x:%x ds:si=%x:%x\n", cs >> 4, cpu_state.pc, addr, val, ES, DI, DS, SI);
    epoch->inflipflop = 0;
    switch (addr) {
        // case LS_DATA:
        // case LF_DATA:
        case LC_DATA:
            if (epoch->outflipflop) {
                /* out b(idx), out b(data), out b(data) */
                epoch->iolatch |= (uint16_t) val << 8;
                epoch->outflipflop = 0;
            } else { //
                epoch->iolatch     = val;
                epoch->outflipflop = 1;
            }
            break;
        // case LS_INDEX:
        // case LF_INDEX:
        case LC_INDEX:
        default:
            epoch->iolatch     = val;
            epoch->outflipflop = 0;
            break;
    }
    epoch_out(addr, epoch->iolatch, epoch);
}
static void
epoch_outw(uint16_t addr, uint16_t val, void *priv)
{
    epoch_iolog("epoch Outw addr %03X val %04X\n", addr, val);
    epoch_t *epoch      = (epoch_t *) priv;
    epoch->inflipflop = 0;
    switch (addr) {
        // case LS_INDEX:
        // case LF_INDEX:
        case LC_INDEX:
        // case LG_INDEX:
            epoch_out(addr, val & 0xff, epoch);
            epoch->iolatch = val >> 8;
            epoch_out(addr + 1, epoch->iolatch, epoch);
            epoch->outflipflop = 1;
            break;
        // case LV_PORT:
        //     // epoch->attrff = 0;
        //     epoch_out(addr, val & 0xff, epoch);
        //     epoch_out(addr, val >> 8, epoch);
        //     epoch->outflipflop = 0;
        //     break;
        // case LS_DATA:
        // case LF_DATA:
        case LC_DATA:
        // case LG_DATA:
        default:
            epoch_out(addr, val, epoch);
            epoch->outflipflop = 0;
            break;
    }
}
static uint8_t
epoch_inb(uint16_t addr, void *priv)
{
    uint8_t temp;
    epoch_t  *epoch      = (epoch_t *) priv;
    epoch->outflipflop = 0;
    switch (addr) {
        case LC_DATA:
            if (epoch->inflipflop) {
                /* out b(idx), in b(low data), in b(high data) */
                temp            = epoch->iolatch >> 8;
                epoch->inflipflop = 0;
            } else { //
                epoch->iolatch    = epoch_in(addr, epoch);
                temp            = epoch->iolatch & 0xff;
                epoch->inflipflop = 1;
            }
            break;
        // case LS_INDEX:
        // case LF_INDEX:
        case LC_INDEX:
        // case LS_DATA:
        // case LF_DATA:
        default:
            temp            = epoch_in(addr, epoch) & 0xff;
            epoch->inflipflop = 0;
            break;
    }
    // epoch_iolog("epoch Inb %04X %02X %04X:%04X\n", addr, temp, cs >> 4, cpu_state.pc);
    return temp;
}
static uint16_t
epoch_inw(uint16_t addr, void *priv)
{
    uint16_t temp;
    epoch_t *epoch       = (epoch_t *) priv;
    epoch->inflipflop  = 0;
    epoch->outflipflop = 0;
    temp = epoch_in(addr, epoch);
    epoch_iolog("epoch Inw addr %03X val %04X\n", addr, temp);
    return temp;
}

/* Return a memory address for 9-bit word access */
static uint32_t
getaddr_9bitword(int32_t addr)
{
            int32_t bit9addr = addr;
            if (bit9addr & 2)
                bit9addr--;
            if (bit9addr & 0x20000)
                bit9addr += 2;
            bit9addr &= 0x1ffff;
            return bit9addr;
}

/* Get font pattern in a line from video memory */
static uint32_t
getfont_ps55dbcs(int32_t code, int32_t line, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    uint32_t font  = 0;
    if (code < 1536) {
        code *= 0x80;
        code += line * 4;
        if (epoch->font24) { /* Font 24 (2 x 13 x 29) */
            font = epoch->vram[code];
            font <<= 8;
            code++;
            font |= epoch->vram[code];
            font <<= 8;
            code++;
            font |= epoch->vram[code];
            font <<= 8;
            code++;
            font |= epoch->vram[code];
        } else { /* Font 16 (2 x 9 x 21) */
            int32_t bit9addr = getaddr_9bitword(code);
            int bitnum = bit9addr & 7;
            bit9addr >>= 3;
            bit9addr += 0x20000; /* real: C0000h */
            font = epoch->vram[code];
            font <<= 8;
            font |= (epoch->vram[bit9addr] << (7 - bitnum)) & 0x80; /* get 9th bit */
            // font &= 0xff80;
            // font |= epoch->vram[code + line * 4 + 1];
            font <<= 8;
            code++;
            font |= epoch->vram[code];
            font <<= 8;
            bitnum = code & 0x7;
            font |= (epoch->vram[bit9addr] << (7 - bitnum)) & 0x80; /* get 9th bit */
            // font &= 0xff80ff80;
            // font |= epoch->vram[code + line * 4 + 3];
        }
    } else
        font = EPOCH_INVALIDACCESS32;
    return font;
}

/* Get the foreground color from the attribute byte */
static uint8_t
getPS55ForeColor(uint8_t attr, epoch_t *epoch)
{
    uint8_t foreground = ~attr & 0x08; /* 0000 1000 */
    foreground <<= 2;                  /* 0010 0000 */
    foreground |= ~attr & 0xc0;        /*  1110 0000 */
    foreground >>= 4;                  /* 0000 1110 */
    // if (epoch->attrc[LV_PAS_STATUS_CNTRL] & 0x40)
    //     foreground |= 0x01; /* bright color palette */
    return foreground;
}

static void
epoch_render_blank(epoch_t *epoch)
{
    int x, xx;

    if (epoch->firstline_draw == 2000)
        epoch->firstline_draw = epoch->displine;
    epoch->lastline_draw = epoch->displine;

    for (x = 0; x < epoch->hdisp + epoch->scrollcache; x++) {
        for (xx = 0; xx < epoch->char_width; xx++)
            ((uint32_t *) buffer32->line[epoch->displine])[(x * epoch->char_width) + xx] = 0;
    }
}
/* Display Adapter Mode 8, E Drawing */
static void
epoch_render_text(epoch_t *epoch)
{
    if (epoch->firstline_draw == 2000)
        epoch->firstline_draw = epoch->displine;
    epoch->lastline_draw = epoch->displine;

    if (epoch->fullchange) {
        int       offset = (8 - epoch->scrollcache) + 24;
        uint32_t *p      = &((uint32_t *) buffer32->line[epoch->displine])[offset];
        int       x;
        int       drawcursor;
        uint8_t   chr, attr;
        int       fg, bg;
        uint32_t  chr_dbcs;
        int       underscore_y = (epoch->font24) ? 28 : 20;
        int       chr_wide = 0;
        // int       colormode = ((epoch->attrc[LV_PAS_STATUS_CNTRL] & 0x80) == 0x80);
        int       colormode = 0;
        // epoch_log("displ: %x, first: %x, epochma: %x, epochsc: %x\n", 
        //     epoch->displine , epoch->firstline_draw, epoch->memaddr, epoch->scanline);
        for (x = 0; x < epoch->hdisp; x += epoch->char_width) {
            chr  = epoch->cram[(epoch->memaddr) & EPOCH_MASK_CRAM];
            attr = epoch->cram[(epoch->memaddr + 1) & EPOCH_MASK_CRAM];
            // if(chr!=0x20) epoch_log("chr: %x, attr: %x    ", chr, attr);
            if (colormode) /* IO 3E8h, Index 1Dh */
            {                                           /* --Parse attribute byte in color mode-- */
                bg = 0;                                 /* bg color is always black (the only way to change background color is programming PAL) */
                fg = getPS55ForeColor(attr, epoch);
                if (attr & 0x04) { /* reverse 0000 0100 */
                    bg = fg;
                    fg = 0;
                }
            } else { /* --Parse attribute byte in monochrome mode-- */
                if (attr & 0x08)
                    fg = 3; /* Highlight 0000 1000 */
                else
                    fg = 2;
                bg = 0;              /* Background is always color #0 (default is black) */
                if (!(~attr & 0xCC)) /* Invisible 11xx 11xx -> 00xx 00xx  */
                {
                    fg = bg;
                    attr &= 0x33; /* disable blinkking, underscore, highlight and reverse */
                }
                if (attr & 0x04) { /* reverse 0000 0100 */
                    bg = fg;
                    fg = 0;
                }
                /* Blinking 1000 0000 */
                fg = ((epoch->blink & 0x20) || (!(attr & 0x80))) ? fg : bg;
                // if(chr!=0x20) epoch_log("chr: %x, %x, %x, %x, %x    ", chr, attr, fg, epoch->egapal[fg], epoch->pallook[epoch->egapal[fg]]);
            }
            /* Draw character */
            for (uint32_t n = 0; n < epoch->char_width; n++)
                p[n] = epoch->pallook[epoch->egapal[bg]]; /* draw blank */
            /*  SBCS or DBCS left half */
            if (chr_wide == 0) {
                if (attr & 0x01)
                    chr_wide = 1;
                // chr_wide = 0;
                /* Stay drawing If the char code is DBCS and not at last column. */
                if (chr_wide) {
                    /* Get high DBCS code from the next video address */
                    chr_dbcs = epoch->cram[(epoch->memaddr + 2) & EPOCH_MASK_CRAM];
                    chr_dbcs <<= 8;
                    chr_dbcs |= chr;
                    /* Get the font pattern */
                    uint32_t font = getfont_ps55dbcs(chr_dbcs, epoch->scanline, epoch);
                    /* Draw 13 dots */
                    for (uint32_t n = 0; n < epoch->char_width; n++) {
                        p[n] = epoch->pallook[epoch->egapal[(font & 0x80000000) ? fg : bg]];
                        font <<= 1;
                    }
                } else {
                    /* the char code is SBCS (ANK) */
                    uint32_t fontbase;
                    uint16_t font;
                    if (attr & 0x02) /* second map of SBCS font */
                        fontbase = EPOCH_VRAM_SBEX;
                    else
                        fontbase = EPOCH_VRAM_SBCS;
                    if (epoch->font24) {
                        font = epoch->vram[fontbase + chr * 0x80 + epoch->scanline * 4 + 2]; /* w13xh29 font */
                        font <<= 8;
                        font |= epoch->vram[fontbase + chr * 0x80 + epoch->scanline * 4 + 3];
                    } else {
                        uint32_t bitnum, bit9addr;
                        fontbase += chr * 0x80 + epoch->scanline * 4;
                        bit9addr = getaddr_9bitword(fontbase);
                        bitnum   = bit9addr & 7;
                        bit9addr >>= 3;
                        bit9addr += 0x20000; /* real: C0000h */

                        fontbase &= 0x1ffff;
                        font = epoch->vram[fontbase + 2]; /* w9xh21 font */
                        font <<= 8;
                        font |= (epoch->vram[bit9addr] << (7 - bitnum)) & 0x80;
                        // if(chr!=0x20) epoch_log("faddr: %x, scline: %x, chr: %x, font: %x    ", fontbase + chr * 0x80 + epoch->scanline * 4, epoch->scanline, chr, font);
                    }
                    // if(chr!=0x20) epoch_log("memaddr: %x, scanline: %x, chr: %x, font: %x    ", epoch->memaddr, epoch->scanline, chr, font);
                    /* Draw 13 dots */
                    for (uint32_t n = 0; n < epoch->char_width; n++) {
                        p[n] = epoch->pallook[epoch->egapal[(font & 0x8000) ? fg : bg]];
                        font <<= 1;
                    }
                }
            }
            /* right half of DBCS */
            else {
                uint32_t font = getfont_ps55dbcs(chr_dbcs, epoch->scanline, epoch);
                /* Draw 13 dots */
                for (uint32_t n = 0; n < epoch->char_width; n++) {
                    p[n] = epoch->pallook[epoch->egapal[(font & 0x8000) ? fg : bg]];
                    font <<= 1;
                }
                chr_wide = 0;
            }
            /* Line 28 (Underscore) Note: Draw this first to display blink + vertical + underline correctly. */
            if (epoch->scanline == underscore_y && attr & 0x40 && !colormode) { /* Underscore only in monochrome mode */
                for (uint32_t n = 0; n < epoch->char_width; n++)
                    p[n] = epoch->pallook[epoch->egapal[fg]]; /* under line (white) */
            }
            /* Column 1 (Vertical Line) */
            if (attr & 0x10) {
                p[0] = epoch->pallook[epoch->egapal[2]]; /* vertical line (white) */
            }
            if (epoch->scanline == 0 && attr & 0x20) { /* HGrid */
                for (uint32_t n = 0; n < epoch->char_width; n++)
                    p[n] = epoch->pallook[epoch->egapal[2]]; /* horizontal line (white) */
            }
            /* Drawing text cursor */
            drawcursor = ((epoch->memaddr == epoch->cursoraddr) && epoch->cursorvisible && epoch->cursoron);
            if (drawcursor) {
                // int cursorwidth = (epoch->crtc[LC_COMPATIBILITY] & 0x20 ? 26 : 13);
                int cursorwidth = epoch->char_width;
                int cursorcolor = 2; /* Choose color 2 if mode 8 */
                fg              = ((attr & 0x08) ? 3 : 2);
                bg              = 0;
                if (attr & 0x04) { /* Color 0 if reverse */
                    bg = fg;
                    fg = 0;
                }
                for (uint32_t n = 0; n < cursorwidth; n++)
                    if (p[n] == epoch->pallook[epoch->egapal[cursorcolor]] || epoch->egapal[bg] == epoch->egapal[cursorcolor])
                        p[n] = (p[n] == epoch->pallook[epoch->egapal[bg]]) ? epoch->pallook[epoch->egapal[fg]] : epoch->pallook[epoch->egapal[bg]];
                    else
                        p[n] = (p[n] == epoch->pallook[epoch->egapal[bg]]) ? epoch->pallook[epoch->egapal[cursorcolor]] : p[n];
            }
            epoch->memaddr += 2;
            p += epoch->char_width;
        }
    }
}

static void
epoch_render_color_4bpp(epoch_t *epoch)
{
    // int changed_offset = epoch->memaddr >> 9;
    // epoch_log("memaddr %x cf %x\n", epoch->memaddr, changed_offset);
    // epoch->plane_mask &= 1; /*safety */

    // if (epoch->changedvram[changed_offset] || epoch->changedvram[changed_offset + 1] || epoch->fullchange) {
        int       x;
        int       offset = (8 - epoch->scrollcache) + 24;
        uint32_t *p      = &((uint32_t *) buffer32->line[epoch->displine])[offset];

        if (epoch->firstline_draw == 2000)
            epoch->firstline_draw = epoch->displine;
        epoch->lastline_draw = epoch->displine;
        // epoch_log("d %X\n", epoch->memaddr);
        int readvaddr = (epoch->memaddr * 8) + (epoch->scanline * 128);
        for (x = 0; x <= epoch->hdisp; x += 8) /* hdisp = 1024 */
        {
            uint8_t edat[8];
            uint8_t dat;

            /* get 8 pixels from vram */
            readvaddr &= epoch->vram_display_mask;
            *(uint8_t *) (&edat[0]) = *(uint8_t *) (&epoch->vram[readvaddr]);
            readvaddr += 1;

            dat  = ((edat[0] >> 7) & 1);
            p[0] = epoch->pallook[epoch->egapal[dat]];
            dat  = ((edat[0] >> 6) & 1);
            p[1] = epoch->pallook[epoch->egapal[dat]];
            dat  = ((edat[0] >> 5) & 1);
            p[2] = epoch->pallook[epoch->egapal[dat]];
            dat  = ((edat[0] >> 4) & 1);
            p[3] = epoch->pallook[epoch->egapal[dat]];
            dat  = ((edat[0] >> 3) & 1);
            p[4] = epoch->pallook[epoch->egapal[dat]];
            dat  = ((edat[0] >> 2) & 1);
            p[5] = epoch->pallook[epoch->egapal[dat]];
            dat  = ((edat[0] >> 1) & 1);
            p[6] = epoch->pallook[epoch->egapal[dat]];
            dat  = ((edat[0] >> 0) & 1);
            p[7] = epoch->pallook[epoch->egapal[dat]];
            p += 8;
        }
}

/*
    INT 10h video modes supported in DOS K3.44.
    Mode        Type    Colors  Text    Base Address    PELs        Render
    2           A/N     3       80 x 25 E0000h          1040 x 725* text
    8           A/N/K   3       80 x 25 E0000h          1040 x 725* text
    9           APA     2       80 x 25 A0000h          720 x 512   color_4bpp
    Ah          APA     2       78 x 25 A0000h          1024 x 768  color_4bpp
    Bh          APA     16      40 x 25 A0000h          360 x 512   n/a
    Ch          APA     16      80 x 25 A0000h          720 x 512   n/a
    Dh          APA     16      78 x 25 A0000h          1024 x 768  n/a
    Eh          A/N/K   16      80 x 25 E0000h          1040 x 725* n/a
                                        (* 720 x 525 in the Font 16 system.)
*/
static void
epoch_recalctimings(epoch_t *epoch)
{
    double crtcconst;
    double _dispontime, _dispofftime, disptime;
    
    epoch->vblankstart = epoch->crtc[LC_VERTICAL_TOTAL] & 0x7f;
    epoch->dispend     = epoch->crtc[LC_VERTICAL_DISPLAYED] & 0x7f;
    epoch->vsyncstart  = epoch->crtc[LC_V_SYNC_POSITION] & 0x7f;
    epoch->vblankstart += 1;
    epoch->vtotal      =  epoch->vblankstart + (epoch->crtc[LC_V_TOTAL_ADJUST] & 0x1f);
    epoch->hdisp       = epoch->crtc[LC_HORIZONTAL_DISPLAYED] & 0xff;

    // epoch->hdisp -= epoch->crtc[LC_START_H_DISPLAY_ENAB];
    // epoch->dispend -= epoch->crtc[LC_START_V_DISPLAY_ENAB];
    //epoch_log("Dispend %d\n", epoch->dispend);
    // epoch->vsyncstart += 1;
    //epoch->vtotal += 1;

    epoch->htotal = epoch->crtc[LC_HORIZONTAL_TOTAL] & 0xff;
    epoch->htotal += 1;

    // epoch->rowoffset = epoch->crtc[LC_OFFSET]; /* number of bytes in a scanline */
    epoch->rowoffset = epoch->crtc[LC_HORIZONTAL_DISPLAYED] & 0xff;

    epoch->clock = epoch->epochconst;

    if (epoch->vtotal == 0)
        epoch->vtotal = epoch->vsyncstart = epoch->vblankstart = 32;
    if (epoch->htotal == 0)
        epoch->htotal = epoch->dispend = epoch->hdisp = 64;
    if (epoch->rowoffset == 0)
        epoch->rowoffset = 64 * 2; /* To avoid causing a DBZ error */
        
    epoch->memaddr_latch = ((epoch->crtc[LC_START_ADDRESS_HIGH] & 0x3f) << 8) | epoch->crtc[LC_START_ADDRESS_LOW];

    epoch->ca_adj = 0;
    epoch->rowcount = epoch->crtc[LC_MAXIMUM_SCAN_LINE] & 0x1f;
    epoch->rowcount += 1;

    epoch->vtotal *= (epoch->rowcount + 1);
    epoch->dispend *= (epoch->rowcount + 1);
    epoch->vsyncstart *= (epoch->rowcount + 1);
    epoch->vblankstart *= (epoch->rowcount + 1);

    epoch->hdisp_time = epoch->hdisp;

    /* determine display mode */
    if (epoch->crtmode & 0x02) {
        if (epoch->font24) {
            epoch->hdisp *= 16;
            epoch->char_width = 16;
        } else {
            epoch->hdisp *= 12;
            epoch->char_width = 12;
        }
        /* PS/55 8-color */
        epoch_log("Set videomode to PS/55 4 bpp graphics.\n");
        epoch->render            = epoch_render_color_4bpp;
        epoch->vram_display_mask = EPOCH_MASK_A000;
    } else {
        /* PS/55 text(color/mono) */
        epoch_log("Set videomode to PS/55 Mode 8/E text.\n");
        epoch->render            = epoch_render_text;
        epoch->vram_display_mask = EPOCH_MASK_CRAM;
        if (epoch->font24) {
            epoch->hdisp *= 13;
            epoch->char_width = 13;
        } else {
            epoch->hdisp *= 9;
            epoch->char_width = 9;
        }
    }
    if (epoch->crtmode & 0x08)
        epoch->render = epoch_render_blank;

    if (epoch->vblankstart < epoch->vsyncstart)
        epoch->vsyncstart = epoch->vblankstart;
    if (epoch->vsyncstart < epoch->dispend)
        epoch->dispend = epoch->vsyncstart;

    crtcconst = epoch->clock * epoch->char_width;

    disptime    = epoch->htotal;
    _dispontime = epoch->hdisp_time;

    epoch_log("Disptime %f dispontime %f hdisp %i\n", disptime, _dispontime, epoch->hdisp);

    _dispofftime = disptime - _dispontime;
    _dispontime *= crtcconst;
    _dispofftime *= crtcconst;

    epoch->dispontime  = (uint64_t) _dispontime;
    epoch->dispofftime = (uint64_t) _dispofftime;
    if (epoch->dispontime < TIMER_USEC)
        epoch->dispontime = TIMER_USEC;
    if (epoch->dispofftime < TIMER_USEC)
        epoch->dispofftime = TIMER_USEC;
    epoch_log("epoch horiz display end %i vidclock %f htotal %i\n", epoch->hdisp, epoch->clock, epoch->htotal);
    epoch_log("epoch vert display end %i max row %i vsyncstart %i vtotal %i\n",epoch->dispend,epoch->rowcount,epoch->vsyncstart,epoch->vtotal);
    epoch_log("epoch dispon %lu dispoff %lu on(us) %f off(us) %f\n",epoch->dispontime, epoch->dispofftime, 
        (double)epoch->dispontime / (double)cpuclock /  (double) (1ULL << 32) * 1000000.0, 
        (double)epoch->dispofftime / (double)cpuclock /  (double) (1ULL << 32) * 1000000.0);
}

static void
epoch_doblit(int wx, int wy, epoch_t *epoch)
{
    if (wx != xsize || wy != ysize) {
        xsize = wx;
        ysize = wy;
        set_screen_size(xsize, ysize);
        if (video_force_resize_get())
            video_force_resize_set(0);
    }
    video_blit_memtoscreen(32, 0, xsize, ysize);
    frames++;

    video_res_x = wx;
    video_res_y = wy;
    video_bpp   = 8;
}

static void
epoch_poll(void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    int    x;

    if (!epoch->linepos) {
        timer_advance_u64(&epoch->timer, epoch->dispofftime);
        
        epoch->cgastat |= 1;
        epoch->linepos = 1;

        if (epoch->dispon) {
            epoch->hdisp_on = 1;

            epoch->memaddr &= epoch->vram_display_mask;
            if (epoch->firstline == 2000) {
                epoch->firstline = epoch->displine;
                video_wait_for_buffer();
            }

            if ((epoch->displine ^ !epoch->oddeven) & 1)
                epoch->render(epoch);

            if (epoch->lastline < epoch->displine)
                epoch->lastline = epoch->displine;
        }

        // epoch_log("%03i %06X %06X\n", epoch->displine, epoch->memaddr,epoch->vram_display_mask);
        epoch->displine++;
        if ((epoch->cgastat & 8) && ((epoch->displine & 0xf) == (epoch->vblankstart & 0xf)) && epoch->vslines) {
            // epoch_log("Vsync off at line %i\n",displine);
            epoch->cgastat &= ~8;
        }
        epoch->vslines++;
        if (epoch->displine > 2000)
            epoch->displine = 0;
    } else {
        // epoch_log("VC %i memaddr %05X\n", epoch->vc, epoch->memaddr);
        timer_advance_u64(&epoch->timer, epoch->dispontime);

        if (epoch->dispon)
            epoch->cgastat &= ~1;
        epoch->hdisp_on = 0;

        epoch->linepos = 0;
        if (epoch->scanline == (epoch->crtc[LC_CURSOR_ROW_END] & 0x1f))
            epoch->cursorvisible = 0;
        if (epoch->dispon) {
            if (epoch->scanline == epoch->rowcount) {
                epoch->linecountff = 0;
                epoch->scanline          = 0;

                epoch->memaddr_backup += (epoch->rowoffset << 1); /* interlace */
                epoch->memaddr_backup &= epoch->vram_display_mask;
                epoch->memaddr = epoch->memaddr_backup;
            } else {
                epoch->linecountff = 0;
                epoch->scanline++;
                epoch->scanline &= 0x1f;
                epoch->memaddr = epoch->memaddr_backup;
            }
        }

        epoch->vc++;
        epoch->vc &= 0x7ff;

        if (epoch->vc == epoch->dispend) {
            epoch->dispon = 0;
            if (!(epoch->crtmode & 0x02)) {                /* in text mode */
                switch (epoch->crtc[LC_CURSOR_ROW_START] & 0x60) {
                    case 0x20:
                        epoch->cursoron = 0;
                        break;
                    case 0x60:
                        epoch->cursoron = epoch->blink & 0x10;
                        break;
                    case 0x40:
                        epoch->cursoron = epoch->blink & 0x08;
                        break;
                    default:
                        epoch->cursoron = 1;
                        break;
                }
                if (!(epoch->blink & (0x08 - 1))) /* force redrawing for cursor and blink attribute */
                    epoch->fullchange = 3;
            }
            epoch->blink++;

            // for (x = 0; x <= (EPOCH_MASK_VRAMPLANE >> 9); x++) {
            //     if (epoch->changedvram[x])
            //         epoch->changedvram[x]--;
            // }
            // memset(changedvram,0,2048);  del
            if (epoch->fullchange) {
                epoch->fullchange--;
            }
        }
        if (epoch->vc == epoch->vsyncstart) {
            int wx, wy;
            // epoch_log("VC vsync  %i %i\n", epoch->firstline_draw, epoch->lastline_draw);
            epoch->dispon = 0;
            epoch->cgastat |= 8;
            x = epoch->hdisp;

            if (!epoch->oddeven)
                epoch->lastline++;
            if (epoch->oddeven)
                epoch->firstline--;

            wx = x;
            wy = epoch->lastline - epoch->firstline;
            
            epoch_doblit(wx, wy, epoch);

            epoch->firstline = 2000;
            epoch->lastline  = 0;

            epoch->firstline_draw = 2000;
            epoch->lastline_draw  = 0;

            epoch->oddeven ^= 1;

            epoch->vslines     = 0;

            epoch->memaddr
                = epoch->memaddr_backup = epoch->memaddr_latch << 1;
            epoch->cursoraddr = ((epoch->crtc[LC_CURSOR_LOC_HIGH] << 8) | epoch->crtc[LC_CURSOR_LOC_LOWJ]) + epoch->ca_adj;
            epoch->cursoraddr <<= 1;

            // epoch_log("Addr %08X vson %03X vsoff %01X\n",epoch->memaddr,epoch->vsyncstart,epoch->crtc[0x11]&0xF);
        }
        if (epoch->vc == epoch->vtotal) {
            // epoch_log("VC vtotal\n");
            // printf("Frame over at line %i %i  %i %i\n",displine,vc,epoch_vsyncstart,epoch_dispend);
            epoch->vc          = 0;
            epoch->scanline    = 0;
            epoch->dispon      = 1;
            epoch->displine    = 0;
            // epoch->scrollcache = epoch->attrc[LV_PANNING] & 0x07;
            epoch->scrollcache = 0;
        }
        if (epoch->scanline == (epoch->crtc[LC_CURSOR_ROW_START] & 31))
            epoch->cursorvisible = 1;
    }
}

static void
epoch_vram_write(uint32_t addr, uint8_t val, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    // epoch_log("epoch_vw: %x %x\n", addr, val);
    addr &= EPOCH_MASK_VRAM;
    epoch->vram[addr] = val;
    epoch->fullchange = 3;
}
static uint8_t
epoch_vram_read(uint32_t addr, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    addr &= EPOCH_MASK_VRAM;
    return epoch->vram[addr];
}

static void
epoch_vram_writew(uint32_t addr, uint16_t val, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    // epoch_log("%04X:%04X epoch_vww: %x, val %x DS %x SI %x ES %x DI %x %x\n", cs >> 4, cpu_state.pc, addr, val,DS,SI,ES,DI, epoch->crtc[LC_INTERLACE_AND_SKEW]);
    // epoch_log("%04X:%04X epoch_vww: %x, val %x cm %x\n", cs >> 4, cpu_state.pc, addr, val, epoch->crtmode);
    cycles -= video_timing_write_w;
    addr -= 0xA0000;
    if (!(epoch->crtmode & 0x02) && !(epoch->font24)) {
        uint32_t toaddr, bitnum;

        /* rw one word with 9 bits */
        /* virtual: 20000h (0010b, 0011b) -> real: 00001h (0000b, 0001b) */
        toaddr = getaddr_9bitword(addr);
        bitnum   = toaddr & 7;
        epoch_vram_write(toaddr, val & 0xff, epoch);
        
        /* get 9th bit */
        toaddr >>= 3;
        toaddr += 0x20000; /* real: C0000h */
        val >>= 15;
        val <<= bitnum;

        /* mask to update one bit */
        val |= (epoch_vram_read(toaddr, epoch) & (~(1 << bitnum)));
        epoch_vram_write(toaddr, val, epoch);
        // epoch_log("%x %x\n", toaddr, val);
        // epoch_log("%x %x\n", addr, val);
    } else {/* is graphic mode */
        epoch_vram_write(addr, val & 0xff, epoch);
        epoch_vram_write(addr + 1, val >> 8, epoch);
    }
    // epoch_log("%x %x\n", addr, val);
}

static uint16_t
epoch_vram_readw(uint32_t addr, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    cycles -= video_timing_read_w;
    addr -= 0xA0000;
    // epoch_log("%04X:%04X epoch_vrw: %x cm %x\n", cs >> 4, cpu_state.pc, addr, epoch->crtmode);
    if (!(epoch->crtmode & 0x02) && !(epoch->font24)) {
        uint16_t ret;
        uint32_t bitnum;
        uint32_t toaddr;
        /* rw one word with 9 bits */
        /* virtual: 20000h (0010b, 0011b) -> real: 00001h (0000b, 0001b) */
        toaddr = getaddr_9bitword(addr);
        bitnum   = toaddr & 7;
        ret = epoch_vram_read(toaddr, epoch);
        /* get 9th bit */
        toaddr >>= 3;
        toaddr += 0x20000; /* real: C0000h */
        ret |= (epoch_vram_read(toaddr, epoch) << (8 + 7 - bitnum)) & 0x8000;
        return ret;
        // return epoch_vram_read(addr, epoch) | (epoch_vram_read(addr + 1, epoch) << 8);
    } else {/* is graphic mode */
        return epoch_vram_read(addr, epoch) | (epoch_vram_read(addr + 1, epoch) << 8);
    }
}


static void
epoch_cram_write(uint32_t addr, uint8_t val, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    addr &= EPOCH_MASK_CRAM;
    epoch->cram[addr] = val;
    epoch->fullchange = 3;
    // epoch_log("cw %04X:%04X %04X %02X\n", cs >> 4, cpu_state.pc, addr, val);
}
static void
epoch_cram_writeb(uint32_t addr, uint8_t val, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    // epoch_log("epoch_cram_writeb: Write to %x, val %x\n", addr, val);
    cycles -= video_timing_write_b;
    epoch_cram_write(addr, val, epoch);
}
static void
epoch_cram_writew(uint32_t addr, uint16_t val, void *priv)
{
    // epoch_log("epoch_cram_writ   ew: Write to %x, val %x\n", addr, val);
    epoch_t *epoch = (epoch_t *) priv;
    cycles -= video_timing_write_w;
    epoch_cram_write(addr, val & 0xff, epoch);
    epoch_cram_write(addr + 1, val >> 8, epoch);
}

static uint8_t
epoch_cram_read(uint32_t addr, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    addr &= EPOCH_MASK_CRAM;
    return epoch->cram[addr];
}
static uint8_t
epoch_cram_readb(uint32_t addr, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    cycles -= video_timing_read_b;
    return epoch_cram_read(addr, epoch);
}

static uint16_t
epoch_cram_readw(uint32_t addr, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    cycles -= video_timing_read_w;
    return epoch_cram_read(addr, epoch) | (epoch_cram_read(addr + 1, epoch) << 8);
}

static uint8_t
epoch_parity_readb(uint32_t addr, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    if ((epoch->parityerror != 0) && (epoch->parityenabled != 0)) {
        epoch->parityerroraddr = (addr >> 16) & 0xFF; /* X000:0 */
        epoch_log("%04X:%04X perror at %0X\n", cs >> 4, cpu_state.pc, addr);
        if (nmi_mask)
            nmi_raise();
    }
    if (epoch->lowmemorydisabled) {
        if ((addr >= 0x40000) && (addr < 0xA0000)) {
            epoch_log("%04X:%04X mrerror at %0X\n", cs >> 4, cpu_state.pc, addr);
            return EPOCH_INVALIDACCESS8;
        }
    }
    return mem_read_ram(addr, priv);
}

static uint16_t
epoch_parity_readw(uint32_t addr, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    if ((epoch->parityerror != 0) && (epoch->parityenabled != 0)) {
        epoch->parityerroraddr = (addr >> 16) & 0xFF;
        epoch_log("%04X:%04X perror at %X\n", cs >> 4, cpu_state.pc, addr);
        if (nmi_mask)
            nmi_raise();
    }
    if (epoch->lowmemorydisabled) {
        if ((addr >= 0x40000) && (addr < 0xA0000)) {
            epoch_log("%04X:%04X mrerror at %X\n", cs >> 4, cpu_state.pc, addr);
            return EPOCH_INVALIDACCESS16;
        }
    }
    return mem_read_ramw(addr, priv);
}

static void
epoch_parity_writeb(uint32_t addr, uint8_t val, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    // epoch_log("%04X:%04X mw %0X\n", cs >> 4, cpu_state.pc, addr);
    if (epoch->parityenabled == 0)
        epoch->parityerror = 1;
    if (epoch->lowmemorydisabled) {
        if ((addr >= 0x40000) && (addr < 0xA0000)) {
            epoch_log("%04X:%04X mwerror at %X\n", cs >> 4, cpu_state.pc, addr);
            if (epoch->parityenabled)
                epoch->parityerror = 1;
            return;
        }
    }
    mem_write_ram(addr, val, priv);
}
static void
epoch_parity_writew(uint32_t addr, uint16_t val, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    if (epoch->parityenabled == 0)
        epoch->parityerror = 1;
    if (epoch->lowmemorydisabled) {
        if ((addr >= 0x40000) && (addr < 0xA0000)) {
            epoch_log("%04X:%04X mwerror at %X\n", cs >> 4, cpu_state.pc, addr);
            if (epoch->parityenabled)
                epoch->parityerror = 1;
            return;
        }
    }
    mem_write_ramw(addr, val, priv);
}

static uint8_t
epoch_misc_in(uint16_t port, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    uint8_t           ret = 0xff;

    switch (port) {
/*
I/O A0h R:
xxxx xxx1: Memory or parity error?
xxxx xx1x: Test switch (physical switch on the rear panel)
x1xx xxxx: Sense DREQ? (only used in diag test)
0xxx xxxx: Coprocessor installed?
*/
        case 0xA0:
            ret = 0;
            if (epoch->parityenabled)
                ret |= epoch->parityerror & 1;
            if (fpu_type == FPU_NONE)
                ret |= 0x80;
            for (int i = 0; i < 2; i++)
                if (dma_get_drq(i))
                    ret |= 0x40;
            if (epoch->testmode)
                ret |= 0x02;
            break;
        case 0xA1: /* High address where memory error occured */
            ret = epoch->parityerroraddr;
            break;
/*
I/O A2h R: 
xxxx 0x0x: Color 16 CRT
xxxx 1x0x: Mono 24 CRT ?
xxxx xx1x: Mono 16 CRT
xx1x xxxx: No hard drive
x1xx xxxx: No floppy drive
1xxx xxxx: No (bootable?) hard drive
*/
        case 0xA2:
            if (epoch->font24)
                ret = 0xA8; /* Mono 24 */
            else
                ret = 0xA2; /* Mono 16 */
            break;
/*
I/O A3h R: 
xxxx x111: Main RAM 256 KB
xxxx x110: Main RAM 384 KB
xxxx x100: Main RAM 512 KB
xxxx x000: Main RAM 640 KB
xxxx 1xxx: Serial port 3f8h
xxx1 xxxx: Serial port 2f8h
*/
        case 0xA3:
            ret = 0x08;
            if (mem_size < 384)
                ret |= 0x07;
            else if (mem_size < 512)
                ret |= 0x06;
            else if (mem_size < 640)
                ret |= 0x04;
            break;
        case 0xA4:
            ret = 0;
            break;
        case 0xA5:
            ret = 0x08; /* Bit 3: Keyboard connected? */
            break;
        // case 0x164:
        //     switch (epoch->fontcard.portdata) {
        //         case 0x16A:
        //             ret = 0xFD;
        //             break;
        //         case 0x168:
        //             ret = 0xFE;
        //             break;
        //     }
        //     break;
        default:
            break;
    }
    epoch_log("%04X:%04X I/O In %02X: %02X\n", cs >> 4, cpu_state.pc, port, ret);
    return ret;
}

static void
epoch_misc_out(uint16_t port, uint8_t val, void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;
    pit_intf_t *pit_intf = &pit_devs[0];

    // dev->regs[port & 0x0007] = val;
    // epoch_log("%04X:%04X I/O Out %02X: %02X\n  AX=%04X BX=%04X CX=%04X DX=%04X ES=%04X DI=%04X DS=%04X SI=%04X\n",
    //      cs >> 4, cpu_state.pc, port, val, AX, BX, CX, DX, ES, DI, DS, SI);
    if(port != 0x44)
        epoch_log("%04X:%04X I/O Out %02X: %02X\n", cs >> 4, cpu_state.pc, port, val);

    switch (port) {
        case 0x44:
            for (uint8_t i = 0; i < 3; i++) {
                pit_intf->set_gate(pit_intf->data, i, val & 1);
            }
            break;
/*
I/O A0h W:
x1xx xxxx: Enable parity update (Disable this -> Write data -> Enable this -> Read data, causes NMI)
1xxx xxxx: Enable NMI check
*/
        case 0xA0:
            nmi_mask = val & 0x80;
            epoch->parityenabled = val & 0x40; /* 1 = Enable read/write with parity */
            break;
        case 0xA1:
            /* Diagnostics LED (used by debug card module) */
            break;
        case 0xA2:
            /* Reset memory error bit */
            epoch->parityerror = 0;
            break;
        // case 0x160 ... 0x16A:
        //     mem_mapping_enable(&epoch->fontcard.map);
        //     epoch->fontcard.portdata = port;
        //     break;
        case 0x310 ... 0x312:
            epoch->lowmemorydisabled = 0;
            epoch_log("Low memory enabled\n");
            break;
        case 0x314 ... 0x316:
            epoch->lowmemorydisabled = 1;
            epoch_log("Low memory disabled\n");
            break;
    }
}
typedef struct epochkbd_t {
    int want_irq;
    int blocked;
    uint8_t irq_state;

    uint8_t pa;
    uint8_t pb;
    uint8_t clk_hold;
    uint8_t key_waiting;
    int8_t kbd_readdata_step;
    int mouse_enabled;
    int mouse_queue_num;
    int kbc_resetstep;
    uint8_t mouse_queue[4];

    pc_timer_t send_delay_timer;
} epochkbd_t;

static uint8_t key_queue[16]; /* buffer in the keyboard */
static int     key_queue_start = 0;
static int     key_queue_end   = 0;

static void
kbd_epoch_poll(void *priv)
{
    epochkbd_t *kbd = (epochkbd_t *) priv;

    timer_advance_u64(&kbd->send_delay_timer, 250 * TIMER_USEC);

    if (kbd->pb & 0x04) /* controller is sending something to keyboard */
        return;

    if (kbd->kbd_readdata_step < 16)
        kbd->kbd_readdata_step++;
    
    if (!(kbd->pb & 0x08)) /* keyboard interrupt is disabled */
        return;

    if (kbd->want_irq && kbd->kbd_readdata_step >= 16) {
        if (kbd->blocked) {
            kbd->want_irq  = 0;
            kbd->irq_state = 1;
            picint(EPOCH_IRQ3_BIT);
            epoch_kbdlog("epochkbd: kbd_poll(): keyboard_xt : take IRQ\n");
        } else {
            kbd->pa                = kbd->key_waiting;
            kbd->kbd_readdata_step = -4;
            kbd->blocked           = 1;
        }
    }

    if (!kbd->blocked) {
        if (kbd->mouse_queue_num > 0) {
            kbd->mouse_queue_num--;
            kbd->key_waiting = kbd->mouse_queue[kbd->mouse_queue_num];
            epoch_kbdlog("epochkbd: reading %02X from the mouse queue at %i\n", kbd->key_waiting, kbd->mouse_queue_num);
            kbd->want_irq = 1;
        } else if (key_queue_start != key_queue_end) {
            kbd->key_waiting = key_queue[key_queue_start];
            epoch_kbdlog("epochkbd: reading %02X from the key queue at %i\n",
                         kbd->key_waiting, key_queue_start);
            key_queue_start = (key_queue_start + 1) & 0x0f;
            kbd->want_irq   = 1;
        }
    }
}

static void
kbd_epoch_adddata_process(uint16_t val, void (*adddata)(uint16_t val))
{
    // uint8_t num_lock = 0;
    // uint8_t shift_states = 0;

    if (!adddata)
        return;

    // keyboard_get_states(NULL, &num_lock, NULL, NULL);
    // shift_states = keyboard_get_shift() & STATE_LSHIFT;

    // /* If NumLock is on, invert the left shift state so we can always check for
    //    the the same way flag being set (and with NumLock on that then means it
    //    is actually *NOT* set). */
    // if (num_lock)
    //     shift_states ^= STATE_LSHIFT;

    adddata(val);
}

static void
kbd_epoch_adddata(uint16_t val)
{
    key_queue[key_queue_end] = val;
    epoch_log("epochkbd: %02X added to key queue at %i\n",
            val, key_queue_end);
    key_queue_end = (key_queue_end + 1) & 0x0f;
}

static void
kbd_adddata_ex(uint16_t val)
{
    if (val < 0x100)
        kbd_epoch_adddata_process(val, kbd_epoch_adddata);
}

static void
kbd_epoch_softreset(void *priv)
{
    epochkbd_t *kbd = (epochkbd_t *) priv;
    key_queue_start = key_queue_end = 0;
    kbd->kbd_readdata_step          = 16;
    kbd->want_irq                   = 0;
    kbd->blocked                    = 0;
    kbd->clk_hold                   = 0;
    kbd->mouse_queue_num            = 0;
    kbd->blocked                    = 0;
    kbd->irq_state                  = 0;
    kbd->pa                         = 0;
    picintc(EPOCH_IRQ3_BIT);
}
/*
I/O 61h W:
xxxx xxx1: ? (used by kbd interrupt in DOS)
xxxx xx1x: Beep
xxxx x1xx: Hold clock line low (used to reset kbd)
xxxx 1xxx: Enable kbd?
xxx1 xxxx: Send data from system to kbd
1xxx xxxx: Clear buffer?
*/
static void
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    epochkbd_t *kbd = (epochkbd_t *) priv;
    uint8_t     new_clock;
    epoch_kbdlog("%04X:%04X epochkbd: Port %02X out: %02X BX: %04x", cs >> 4, cpu_state.pc, port, val, BX);
    epoch_kbdlog(" Clk: %x %x\n", kbd->blocked, kbd->clk_hold);

    switch (port) {
        case 0x60: /* Diagnostic Output */
            if ((kbd->pb & 0x10) && ((val & 0xf0) == 0xd0)) {
                kbd->mouse_enabled = 1;
                epoch_kbdlog("epochkbd: Mouse is enabled.\n");
            }
            break;
        case 0x61: /* Keyboard Control Register (aka Port B) */
            kbd->pb = val;

            if ((val & 0x18) == 0x08) {
                new_clock = !(val & 0x04);
                /* Trigger kbd reset after the clk line is reset to a high level */
                if (kbd->clk_hold && new_clock) {
                    kbd->mouse_enabled = 0;
                    kbd_epoch_softreset(kbd);
                    epoch_kbdlog("epochkbd: Starting keyboard reset sequence.\n");
                    /* IBM 5556 keyboards send three bytes of identification code in the reset sequence.
                       DOS K3.44 supports following IDs: 
                        A5xx1x: ? (standard layout)
                        A6xx1x: AIUEO RPQ Keyboard
                        B1xx1x, B2xx1x: Type 3, 4 Keyboard (1972 JIS layout)
                        B5xx1x: ? (standard layout)
                        AA----: XT keyboard? (The IPL supports it, but the Japanese DOS doesn't) 
                    */
                    kbd_epoch_adddata(0xA5);
                    kbd_epoch_adddata(0x00);
                    kbd_epoch_adddata(0x10);
                }
            } else if ((val & 0x18) == 0x18) {
                new_clock = !(val & 0x04);
                if (kbd->clk_hold && new_clock) {
                    kbd_epoch_softreset(kbd);
                    epoch_kbdlog("epochkbd: Starting mouse reset sequence.\n");
                    kbd_epoch_adddata(0x00);
                }
            }

            if (kbd->pb & 0x08)
                kbd->clk_hold = !!(kbd->pb & 0x04);

            timer_process();
            speaker_update();
            // if(!speaker_enable && (val & 2)) epoch_log("Buz!\n");
            speaker_gated  = val & 2;
            speaker_enable = val & 2;

            if (speaker_enable)
                was_speaker_enable = 1;
            pit_devs[0].set_gate(pit_devs[0].data, TIMER_CTR_2, val & 2);

            if (val & 0x80) {
                /* clear buffer */
                kbd->pa      = 0;
                kbd->blocked = 0;
                kbd->irq_state = 0;
                picintc(EPOCH_IRQ3_BIT);
            }

            break;
        default:
            break;
    }
}

/*
I/O 61h R
xxxx xx1x: Beep input?
xxxx x1xx: KB -CLK?
xxxx 1xxx: KB -DATA?
*/
static uint8_t
kbd_read(uint16_t port, void *priv)
{
    epochkbd_t *kbd = (epochkbd_t *) priv;
    uint8_t     ret = 0;

    switch (port) {
        case 0x60: /* Keyboard Data Register  (aka Port A) */
            ret = kbd->pa;
            break;

        case 0x61: /* Keyboard Control Register (aka Port B) */
            ret = kbd->pb & 0xf0; /* reset sense bit */
            ret |= 0x0c;          /* reset sense bit */
            if (kbd->kbd_readdata_step < 16) {
                ret &= 0xf3;
                switch (kbd->kbd_readdata_step) {
                    case -4:
                        ret |= 0x0c; /* start bit */
                        break;
                    case -3:
                        ret |= 0x0c; /* start bit */
                        break;
                    case -2:
                        ret |= 0x08; /* start bit */
                        break;
                    case -1:
                        ret |= 0x0c; /* start bit */
                        break;
                    default:
                        if (!!(kbd->kbd_readdata_step & 1))
                            ret |= 0x04;
                        if ((kbd->key_waiting >> (kbd->kbd_readdata_step >> 1)) & 1)
                            ret |= 0x08;
                        break;
                }
                epoch_kbdlog("  rdata step: %d %x %x\n", kbd->kbd_readdata_step, ret & 0x08, ret & 0x04);
            }

            /* Bit 1: Timer 2 (Speaker) out state */
            if (pit_devs[0].get_outlevel(pit_devs[0].data, TIMER_CTR_2) && speaker_enable)
                ret &= 0xfd; /* 1111 1101 */
            else
                ret |= 0x02;

            break;
        default:
            break;
    }
    // if (port != 0x61)
        epoch_kbdlog("%04X:%04X epochkbd: Port %02X in : %02X pb: %02x CX: %04x\n", cs >> 4, cpu_state.pc, port, ret, kbd->pb, CX);
    return ret;
}

static void
kbd_reset(void *priv)
{
    epochkbd_t *kbd = (epochkbd_t *) priv;

    kbd_epoch_softreset(kbd);
    kbd->pb = 0x00;

    keyboard_scan      = 1;
    kbd->mouse_enabled = 0;
}

static void *
kbd_init(const device_t *info)
{
    epochkbd_t *kbd;

    kbd = (epochkbd_t *) calloc(1, sizeof(epochkbd_t));

    io_sethandler(0x0060, 4,
                  kbd_read, NULL, NULL, kbd_write, NULL, NULL, kbd);
    keyboard_send = kbd_adddata_ex;
    kbd_reset(kbd);

    timer_add(&kbd->send_delay_timer, kbd_epoch_poll, kbd, 1);

    keyboard_set_table(scancode_set8a);
    keyboard_mode = 0x8a;

    return kbd;
}

static void
kbd_close(void *priv)
{
    epochkbd_t *kbd = (epochkbd_t *) priv;

    /* Stop the timer. */
    timer_disable(&kbd->send_delay_timer);
    mouse_close();

    /* Disable scanning. */
    keyboard_scan = 0;
    keyboard_send = NULL;

    io_removehandler(0x0060, 2,
                     kbd_read, NULL, NULL, kbd_write, NULL, NULL, kbd);

    free(kbd);
}

static const device_t kbc_epoch_device = {
    .name          = "IBM 5556 Keyboard and Mouse",
    .internal_name = "kbc_epoch",
    .flags         = 0,
    .local         = 0,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
The IBM 5550 DOS K3.44 comes with a mouse driver
for the M1 and P1 keyboards that have a built-in mouse adapter.
The mouse adapter for the expansion slot was also available,
but it may require the bundled driver. (not confirmed)

IBM 5550 DOS K3.4 Mouse Driver (Int 0Bh and 33h)
61h <- 1st kbd indata
[si+4Ch] status <- 2nd kbd indata
1xxx xxxx negative dX
x1xx xxxx bit 9 of dX
xx1x xxxx negative dY
xxx1 xxxx bit 9 of dY
xxxx x1xx cursor moved
xxxx xx1x right button
xxxx xxx1 left button
[si+4Dh] previous button status <- [si+4Ch]
[si+4Eh] dX <- 3rd kbd indata
[si+50h] dY <- 4th kbd indata
*/
static int
epoch_mouse_poll(void *priv)
{
    epochkbd_t *kbd = (epochkbd_t *) priv;
    int         dat = mouse_get_buttons_ex();
    int         dx, dy;

    if (!kbd->mouse_enabled)
        return 0;
    
    if (kbd->mouse_queue_num > 0)
        return 0;

    if (kbd->pb & 0x10)
        return 0;

    dat &= 0x03;
    if (!mouse_moved()) {
        kbd->mouse_queue[1] = 0x61;
        kbd->mouse_queue[0] = dat;
        kbd->mouse_queue_num = 2;
    } else {
        dat |= 0x04;
        mouse_subtract_x(&dx, NULL, -512, 511, 0);
        mouse_clear_x();
        if (dx < 0)
            dat |= 0x20;
        if (dx & 0x100)
            dat |= 0x10;
        mouse_subtract_y(&dy, NULL, -512, 511, 0, 0);
        mouse_clear_y();
        if (dy < 0)
            dat |= 0x80;
        if (dy & 0x100)
            dat |= 0x40;
        epoch_log("Mouse moved %x %x %x\n", dat, dx, dy);
        kbd->mouse_queue[3] = 0x61;
        kbd->mouse_queue[2] = dat;
        kbd->mouse_queue[1] = dx & 0xff;
        kbd->mouse_queue[0] = dy & 0xff;
        kbd->mouse_queue_num = 4;
    }
    return 0;
}

static void
epoch_nvr_time_set(uint8_t *regs, struct tm *tm)
{
    regs[epoch_nvr_SECOND1]  = tm->tm_sec % 10;
    regs[epoch_nvr_SECOND10] = (tm->tm_sec / 10);
    regs[epoch_nvr_MINUTE1]  = (tm->tm_min % 10);
    regs[epoch_nvr_MINUTE10] = (tm->tm_min / 10);
    regs[epoch_nvr_HOUR1]  = (tm->tm_hour % 10);
    regs[epoch_nvr_HOUR10] = (tm->tm_hour / 10);
    regs[epoch_nvr_WEEKDAY] = tm->tm_wday;
    regs[epoch_nvr_DAY1]    = (tm->tm_mday % 10);
    regs[epoch_nvr_DAY10]   = (tm->tm_mday / 10);
    regs[epoch_nvr_MONTH1]  = ((tm->tm_mon + 1) % 10);
    regs[epoch_nvr_MONTH10] = ((tm->tm_mon + 1) / 10);
    regs[epoch_nvr_YEAR1]   = (tm->tm_year % 10);
    regs[epoch_nvr_YEAR10]  = ((tm->tm_year % 100) / 10);
}

/* Get the chip time. */
#define nibbles(a) (regs[(a##1)] + 10 * regs[(a##10)])
static void
epoch_nvr_time_get(uint8_t *regs, struct tm *tm)
{
    tm->tm_sec = nibbles(epoch_nvr_SECOND);
    tm->tm_min = nibbles(epoch_nvr_MINUTE);
    tm->tm_hour = nibbles(epoch_nvr_HOUR);
    tm->tm_wday = regs[epoch_nvr_WEEKDAY];
    tm->tm_mday = nibbles(epoch_nvr_DAY);
    tm->tm_mon  = (nibbles(epoch_nvr_MONTH) - 1);
    tm->tm_year = (nibbles(epoch_nvr_YEAR));
}

/* This is called every second through the NVR/RTC hook. */
static void
epoch_nvr_tick(nvr_t *nvr)
{
    struct tm tm;
    if (!(nvr->regs[epoch_nvr_CONTROL] & 0x40)) {
        /* Get the current time from the internal clock. */
        nvr_time_get(&tm);
        /* Update registers with current time. */
        epoch_nvr_time_set(nvr->regs, &tm);
    }
}

static void
epoch_nvr_start(nvr_t *nvr)
{
    struct tm tm;
    /* Initialize the internal and chip times. */
    if (time_sync & TIME_SYNC_ENABLED) {
        /* Use the internal clock's time. */
        nvr_time_get(&tm);
        epoch_nvr_time_set(nvr->regs, &tm);
    } else {
        /* Set the internal clock from the chip time. */
        epoch_nvr_time_get(nvr->regs, &tm);
        nvr_time_set(&tm);
    }
}

/* Write to one of the chip registers. */
static void
epoch_nvr_write(uint16_t port, uint8_t val, void *priv)
{
    epoch_t *epoch      = (epoch_t *) priv;
    int addr = 0;

    switch (port) {
        case 0x360:
            epoch->nvrctrl = val;
            addr = val & 0xf;
            if (val & 0x20) { /* Write */
                if (addr >= 0x8 && (epoch->nvr.regs[addr] != val))
                    nvr_dosave = 1;
                epoch->nvr.regs[addr] = epoch->nvrdata;
            }
            break;
        case 0x361:
            if(epoch->nvrctrl & 0x40) /* Is the access enabled? */
                epoch->nvrdata = val;
            break;
    }
    // epoch_log("%04X:%04X I/O Out %02X: %02X\n", cs >> 4, cpu_state.pc, port, val);
}

/* Read from one of the chip registers. */
static uint8_t
epoch_nvr_read(uint16_t port, void *priv)
{
    const epoch_t *epoch = (epoch_t *) priv;
    
    switch (port) {
        case 0x360:
            return epoch->nvrctrl;
            break;
        case 0x361:
            if(epoch->nvrctrl & 0x40) /* Is the access enabled? */
                return (epoch->nvr.regs[(epoch->nvrctrl & 0xf)]);
            break;
    }
    return EPOCH_INVALIDACCESS8;
}

/* Reset the RTC registers to a default state. */
static void
epoch_nvr_reset(nvr_t *nvr)
{
    /* Clear the NVRAM. */
    memset(nvr->regs, 0xff, nvr->size);
    /* Reset the RTC registers. */
    memset(nvr->regs, 0x00, 0xc);
    nvr->regs[epoch_nvr_CONTROL] = 0;
}

static void
epoch_nvr_init(epoch_t *epoch)
{
    nvr_t* nvr = &epoch->nvr;
    /* This is machine specific. */
    nvr->size = 17;
    nvr->irq  = -1;
    /* Set up any local handlers here. */
    nvr->reset = epoch_nvr_reset;
    nvr->start = epoch_nvr_start;
    nvr->tick  = epoch_nvr_tick;
    /* Initialize the actual NVR. */
    nvr_init(nvr);
    io_sethandler(0x0360, 2,
                  epoch_nvr_read, NULL, NULL, epoch_nvr_write, NULL, NULL, epoch);
}

static uint8_t ibm5550_attr_mono[16] = 
{
	0,6,6,62,0,0,0,0,0,0,0,0,0,0,0,0
};

// static uint8_t ps55_attr_color[16] = /* for video mode 0eh color character */
// {
// 	0x00,0x38,0x24,0x3c,0x12,0x3a,0x36,0x3e,0x09,0x39,0x2d,0x3d,0x1b,0x3b,0x3f,0x3f
// };

/* 12-bit DAC color palette for IBMJ Display Adapter with color monitor */
static uint8_t ps55_palette_color[64][3] = {
    { 0x00, 0x00, 0x00 },    { 0x00, 0x00, 0x2A },    { 0x00, 0x2A, 0x00 },    { 0x00, 0x2A, 0x2A },
    { 0x2A, 0x00, 0x00 },    { 0x2A, 0x00, 0x2A },    { 0x2A, 0x2A, 0x00 },    { 0x2A, 0x2A, 0x2A },
    { 0x00, 0x00, 0x15 },    { 0x00, 0x00, 0x3F },    { 0x00, 0x2A, 0x15 },    { 0x00, 0x2A, 0x3F },
    { 0x2A, 0x00, 0x15 },    { 0x2A, 0x00, 0x3F },    { 0x2A, 0x2A, 0x15 },    { 0x2A, 0x2A, 0x3F },
    { 0x00, 0x15, 0x00 },    { 0x00, 0x15, 0x2A },    { 0x00, 0x3F, 0x00 },    { 0x00, 0x3F, 0x2A },
    { 0x2A, 0x15, 0x00 },    { 0x2A, 0x15, 0x2A },    { 0x2A, 0x3F, 0x00 },    { 0x2A, 0x3F, 0x2A },
    { 0x00, 0x15, 0x15 },    { 0x00, 0x15, 0x3F },    { 0x00, 0x3F, 0x15 },    { 0x00, 0x3F, 0x3F },
    { 0x2A, 0x15, 0x15 },    { 0x2A, 0x15, 0x3F },    { 0x2A, 0x3F, 0x15 },    { 0x2A, 0x3F, 0x3F },
    { 0x15, 0x00, 0x00 },    { 0x15, 0x00, 0x2A },    { 0x15, 0x2A, 0x00 },    { 0x15, 0x2A, 0x2A },
    { 0x3F, 0x00, 0x00 },    { 0x3F, 0x00, 0x2A },    { 0x3F, 0x2A, 0x00 },    { 0x3F, 0x2A, 0x2A },
    { 0x15, 0x00, 0x15 },    { 0x15, 0x00, 0x3F },    { 0x15, 0x2A, 0x15 },    { 0x15, 0x2A, 0x3F },
    { 0x3F, 0x00, 0x15 },    { 0x3F, 0x00, 0x3F },    { 0x3F, 0x2A, 0x15 },    { 0x3F, 0x2A, 0x3F },
    { 0x15, 0x15, 0x00 },    { 0x15, 0x15, 0x2A },    { 0x15, 0x3F, 0x00 },    { 0x15, 0x3F, 0x2A },
    { 0x3F, 0x15, 0x00 },    { 0x3F, 0x15, 0x2A },    { 0x3F, 0x3F, 0x00 },    { 0x3F, 0x3F, 0x2A },
    { 0x15, 0x15, 0x15 },    { 0x15, 0x15, 0x3F },    { 0x15, 0x3F, 0x15 },    { 0x15, 0x3F, 0x3F },
    { 0x3F, 0x15, 0x15 },    { 0x3F, 0x15, 0x3F },    { 0x3F, 0x3F, 0x15 },    { 0x3F, 0x3F, 0x3F }
};

static video_timings_t timing_epoch_vid = 
{ .type = VIDEO_ISA, .write_b = 8, .write_w = 8, .write_l =  16, .read_b = 8, .read_w = 8, .read_l = 16 };

static void
epoch_reset(void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;

    epoch->parityerror = 0;
    epoch->parityenabled = 1;
    epoch->lowmemorydisabled = 1;
    epoch->crtioenabled = 0;
    mem_mapping_disable(&epoch->cmap);
    mem_mapping_disable(&epoch->vmap);
    // epoch->attrc[LV_CURSOR_COLOR]    = 0x0f;                   /* cursor color */
    epoch->crtc[LC_HORIZONTAL_TOTAL] = 103;    /* Horizontal Total */
    epoch->crtc[LC_VERTICAL_TOTAL]  = 26;    /* Vertical Total (These two must be set before the timer starts.) */
    epoch->crtmode = 0;
    epoch->vram_display_mask = EPOCH_MASK_CRAM;
    // epoch->plane_mask = 1;
    epoch->oddeven = 0;
    // epoch->memaddr_latch                  = 0;
    // epoch->attrc[LV_CURSOR_CONTROL]  = 0x13; /* cursor options */
    // epoch->attr_palette_enable       = 0;    /* disable attribute generator */

    // epoch->attrc[LV_PAS_STATUS_CNTRL] = 0;
    // epoch->attrc[LV_PANNING] = 0;
    
    /* Set internal color palette registers */
    for (uint16_t i = 0; i < 16; i++) {
        epoch->egapal[i] = ibm5550_attr_mono[i];
    }
    /* Set color palette for video output */
    for (uint16_t i = 0; i < 64; i++) {
        epoch->vgapal[i].r = ps55_palette_color[i & 0x3F][0];
        epoch->vgapal[i].g = ps55_palette_color[i & 0x3F][1];
        epoch->vgapal[i].b = ps55_palette_color[i & 0x3F][2];
        epoch->pallook[i]  = makecol32((epoch->vgapal[i].r & 0x3f) * 4, (epoch->vgapal[i].g & 0x3f) * 4, (epoch->vgapal[i].b & 0x3f) * 4);
    }

    // mem_mapping_disable(&epoch->fontcard.map);

    epoch_log("epoch_reset done.\n");
}

/*
//[Font ROM Map (DA1)]
//Bank 0
// 0000-581Fh Pointers (Low) for each character font?
// 5820-7FFFh Pointers (High) for each character font?
// 8000- *  h Font Data
*/
// static void
// epoch_video_load_font(char *fname, epoch_t *epoch)
// {
//     uint8_t buf;
//     uint64_t fsize;
//     if (!fname)
//         return;
//     if (*fname == '\0')
//         return;
//     FILE *mfile = rom_fopen(fname, "rb");
//     if (!mfile) {
//         // da2_log("MSG: Can't open binary ROM font file: %s\n", fname);
//         return;
//     }
//     fseek(mfile, 0, SEEK_END);
//     fsize = ftell(mfile); /* get filesize */
//     fseek(mfile, 0, SEEK_SET);
//     if (fsize > EPOCH_FONTROM_SIZE) {
//         fsize = EPOCH_FONTROM_SIZE; /* truncate read data */
//         // da2_log("MSG: The binary ROM font is truncated: %s\n", fname);
//         // fclose(mfile);
//         // return 1;
//     }
//     uint32_t j = 0;
//     while (ftell(mfile) < fsize) {
//         (void) !fread(&buf, sizeof(uint8_t), 1, mfile);
//         epoch->fontcard.rom[j] = buf;
//         j++;
//     }
//     fclose(mfile);
//     return;
// }

// static void
// epoch_font_writeb(uint32_t addr, uint8_t val, void *priv)
// {
//     epoch_t *epoch = (epoch_t *) priv;
//     epoch->fontcard.bank = val;
//     // if ((addr & ~0xfff) != 0xE0000) return;
//     epoch_log("cw %04X %02X %04X %04X %04X %04X\n", addr, val, DS, SI, ES, DI);
// }
// static uint8_t
// epoch_font_readb(uint32_t addr, void *priv)
// {
//     epoch_t *epoch = (epoch_t *) priv;
//     uint32_t readaddr = epoch->fontcard.bank;
//     addr &= EPOCH_FONTROM_MASK;
//     readaddr *= 0xc000;/* xxx x000 0000 0000 0000 (8000h) */
//     readaddr += addr;
//     if (readaddr >= EPOCH_FONTROM_SIZE)
//         return EPOCH_INVALIDACCESS8;
//     // epoch_log("cr %X %x %04X %04X %04X %04X\n", readaddr, epoch->fontcard.rom[readaddr], DS, SI, ES, DI);
//     // if(epoch->vram[addr] == 0xcb)
//     //         epoch_log("CB %04X:%04X %04X:%04X>%04X:%04X\n", cs >> 4, cpu_state.pc, DS, SI,ES,DI);
//     return epoch->fontcard.rom[readaddr];
// }
static void *
epoch_init(UNUSED(const device_t *info))
{
    epoch_t *epoch  = calloc(1, sizeof(epoch_t));
    epoch->font24 = device_get_config_int("model");
    epoch->testmode = device_get_config_int("testmode");

    video_inform(VIDEO_FLAG_TYPE_NONE, &timing_epoch_vid);
    video_update_timing();

    epoch->dispontime        = 1000ull << 32;
    epoch->dispofftime       = 1000ull << 32;
    // epoch->changedvram       = calloc(1,  (EPOCH_MASK_VRAMPLANE + 1) >> 9); /* XX000h */

    if (epoch->font24)
        epoch->pixelclock = EPOCH_PIXELCLOCK24;
    else
        epoch->pixelclock = EPOCH_PIXELCLOCK16;

    epoch->vram              = calloc(1, 256* 1024);
    // for(int i=0;i<256*1024;i++) /* for debug */
    //     epoch->vram[i] = 0xff;
    epoch->cram              = calloc(1, 4 * 1024);
    // epoch->fontcard.rom      = calloc(1, EPOCH_FONTROM_SIZE);
    // epoch_video_load_font("roms/machines/ibm5550/GEN1FONT.BIN", epoch);

    epoch->epochconst = (uint64_t) ((cpuclock / epoch->pixelclock) * (double) (1ull << 32));

    mem_mapping_add(&epoch->cmap, 0xE0000, 0x1000, epoch_cram_readb, epoch_cram_readw, NULL,
        epoch_cram_writeb, epoch_cram_writew, NULL, NULL, MEM_MAPPING_EXTERNAL, epoch);
    mem_mapping_add(&epoch->vmap, 0xA0000, 0x40000, NULL, epoch_vram_readw, NULL,
        NULL, epoch_vram_writew, NULL, NULL, MEM_MAPPING_EXTERNAL, epoch);
    // mem_mapping_add(&epoch->fontcard.map, 0xF0000, 0xC000, epoch_font_readb, NULL, NULL,
    //     epoch_font_writeb, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, epoch);

    // mem_mapping_disable(&epoch->fontcard.map);
    mem_mapping_add(&epoch->paritymap, 0, 0xA0000, epoch_parity_readb, epoch_parity_readw, NULL,
        epoch_parity_writeb, epoch_parity_writew, NULL, NULL, MEM_MAPPING_CACHE, epoch);

    io_sethandler(0x03d0, 0x0020, epoch_inb, epoch_inw, NULL, epoch_outb, epoch_outw, NULL, epoch);

    io_sethandler(0x44, 0x0001,
                  epoch_misc_in, NULL, NULL, epoch_misc_out, NULL, NULL, epoch);
    io_sethandler(0xA0, 0x0008,
                  epoch_misc_in, NULL, NULL, epoch_misc_out, NULL, NULL, epoch);
    io_sethandler(0x310, 0x0008,
                  epoch_misc_in, NULL, NULL, epoch_misc_out, NULL, NULL, epoch);
    // io_sethandler(0x160, 0x0010,
    //               epoch_misc_in, NULL, NULL, epoch_misc_out, NULL, NULL, epoch);

    epoch_reset(epoch);
    
    timer_add(&epoch->timer, epoch_poll, epoch, 1);

    epoch_nvr_init(epoch);

    return epoch;
}

static void
epoch_close(void *priv)
{
    epoch_t *epoch = (epoch_t *) priv;

    /* dump mem for debug */
#ifdef ENABLE_EPOCH_LOG
    FILE *fp;
    fp = fopen("epoch_cram.dmp", "wb");
    if (fp != NULL) {
        fwrite(epoch->cram, EPOCH_SIZE_CRAM, 1, fp);
        fclose(fp);
    }
    fp = fopen("epoch_vram.dmp", "wb");
    if (fp != NULL) {
        fwrite(epoch->vram, EPOCH_SIZE_VRAM, 1, fp);
        fclose(fp);
    }
    // fp = fopen("epoch_attrpal.dmp", "wb");
    // if (fp != NULL) {
    //     fwrite(epoch->attrc, 32, 1, fp);
    //     fclose(fp);
    // }
    fp = fopen("epoch_daregs.txt", "w");
    if (fp != NULL) {
            fprintf(fp, "3d8(crtmode) %02X\n", epoch->crtmode);
        // for (uint8_t i = 0; i < 0x10; i++)
        //     fprintf(fp, "3e1(ioctl) %02X: %4X %d\n", i, epoch->ioctl[i], epoch->ioctl[i]);
        // for (uint8_t i = 0; i < 0x20; i++)
        //     fprintf(fp, "3e3(fctl)  %02X: %4X %d\n", i, epoch->fctl[i], epoch->fctl[i]);
        for (uint8_t i = 0; i < 0x20; i++)
            fprintf(fp, "3e5(crtc)  %02X: %4X %d\n", i, epoch->crtc[i], epoch->crtc[i]);
        // for (uint8_t i = 0; i < 0x40; i++)
        //     fprintf(fp, "3e8(attr)  %02X: %4X %d\n", i, epoch->attrc[i], epoch->attrc[i]);
        // for (uint8_t i = 0; i < 0x10; i++)
        //     fprintf(fp, "3eb(gcr)   %02X: %4X\n", i, epoch->gdcreg[i]);
        // for (uint8_t i = 0; i < 0x20; i++) {
        //     fprintf(fp, "vp         %02X: %4X %4X %4X %4X\n", i, 
        //         epoch->crtc_vpreg[0 + i], epoch->crtc_vpreg[0x20 + i], epoch->crtc_vpreg[0x40 + i], epoch->crtc_vpreg[0x60 + i]);
        // }
        fclose(fp);
    }
    fp = fopen("ram_low.dmp", "wb");
    if (fp != NULL) {
        fwrite(ram, 0x40000, 1, fp);
        fclose(fp);
    }
    epoch_log("closed %04X:%04X AX=%04X BX=%04X CX=%04X DX=%04X ES=%04X DI=%04X DS=%04X SI=%04X\n",
          cs >> 4, cpu_state.pc, AX, BX, CX, DX, ES, DI, DS, SI);
    epoch_log("PIC IRR=%02X ISR=%02X IMR=%02X ICW1=%02X ICW2=%02X ICW3=%02X ICW4=%02X OCW2=%02X OCW3=%02X\n",
          pic.irr, pic.isr, pic.imr, pic.icw1, pic.icw2, pic.icw3, pic.icw4, pic.ocw2, pic.ocw3);
#endif
    free(epoch->cram);
    free(epoch->vram);
    // free(epoch->fontcard.rom);
    // free(epoch->changedvram);
    free(epoch);
}

static void
epoch_speed_changed(void *priv)
{
    epoch_t *epoch    = (epoch_t *) priv;
    epoch->epochconst = (uint64_t) ((cpuclock / epoch->pixelclock) * (double) (1ull << 32));
    epoch_recalctimings(epoch);
}

static void
epoch_force_redraw(void *priv)
{
    epoch_t *epoch      = (epoch_t *) priv;
    epoch->fullchange = changeframecount;
}

static const device_config_t epoch_config[] = {
    // clang-format off
    {
        .name        = "model",
        .description = "Model",
        .type        = CONFIG_SELECTION,
        .default_int = EPOCH_CONFIG_MONO24,
        .selection   = {
            {
                .description = "A (Font 16)",
                .value = EPOCH_CONFIG_MONO16
            },
            {
                .description = "B (Font 24)",
                .value = EPOCH_CONFIG_MONO24
            },
            { .description = "" }
        }
    },
    {
        .name        = "testmode",
        .description = "Test mode",
        .type        = CONFIG_BINARY,
        .default_int    = 0,
        .selection      = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibm5550_device = {
    .name          = "IBM 5551 System Unit",
    .internal_name = "ibm5550",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = epoch_init,
    .close         = epoch_close,
    .reset         = epoch_reset,
    .available     = NULL,
    .speed_changed = epoch_speed_changed,
    .force_redraw  = epoch_force_redraw,
    .config        = epoch_config
};

static void
pit_irq6_timer(int new_out, int old_out, UNUSED(void *priv))
{
    // epoch_log("%04X:%04X IRQ6 Timer triggered.\n", cs >> 4, cpu_state.pc);
    if (new_out && !old_out)
        picint(EPOCH_IRQ6_BIT);

    if (!new_out)
        picintc(EPOCH_IRQ6_BIT);
}

static pit_t *
pit_ibm5550_init(void)
{
    void *pit;

    pit_intf_t *pit_intf = &pit_devs[0];

            pit       = device_add(&i8253_device);
            *pit_intf = pit_classic_intf;

    pit_intf->data = pit;

    for (uint8_t i = 0; i < 3; i++) {
        pit_intf->set_gate(pit_intf->data, i, 1);
        pit_intf->set_using_timer(pit_intf->data, i, 1);
    }

    pit_intf->set_out_func(pit_intf->data, TIMER_CTR_1, pit_irq6_timer);
    pit_intf->set_out_func(pit_intf->data, TIMER_CTR_0, pit_refresh_timer_xt);
    pit_intf->set_out_func(pit_intf->data, TIMER_CTR_2, pit_speaker_timer);
    pit_intf->set_load_func(pit_intf->data, TIMER_CTR_2, speaker_set_count);

    pit_intf->set_gate(pit_intf->data, TIMER_CTR_2, 0);

    return pit;
}

int
machine_xt_ibm5550_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ibm5550/ipl5550.rom",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&fdc_xt_5550_device);

    epochkbd_t *kbc = device_add(&kbc_epoch_device);
    
    pic_init();
    dma_init();
    pit_ibm5550_init();
    nmi_mask = 0;

    device_add(&ibm5550_device);

    device_add(&lpt_port_device);
    serial_t *uart = device_add(&ns8250_device);
    serial_setup(uart, 0x3f8, 1);/* Use IRQ 1 */

    if (mouse_type == MOUSE_TYPE_INTERNAL) {
        /* Tell mouse driver about our internal mouse. */
        mouse_reset();
        mouse_set_buttons(2);
        /* I don't know the actual polling speed, but
           a higher value may cause a conflict with the mouse driver  */
        mouse_set_sample_rate(30.0);
        mouse_set_poll(epoch_mouse_poll, kbc);
    }

    return ret;
}
