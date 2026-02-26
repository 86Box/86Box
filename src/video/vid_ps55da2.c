/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM PS/55 Display Adapter II (and its successors) emulation.
 * 
 *   Notes: There are some known issues that should be corrected.
 *            - Incorrect foreground text color appears on an active window in OS/2 J1.3. 
 *            - Glitches some part of graphics on the Control Panel in OS/2 J2.1 beta. 
 * 
 *          The code should be tested with following cases.
 *            - Execute MODE 0, 1, 3 and 4 commands in DOS K3.3 to test various video modes.
 *            - Run SAMPLE program with the BASIC interpreter in DOS K3.3.
 *            - Run DOS J4.0 install program to test video mode 03.
 *            - Run Win 3.1 (IBM-J OEM) and OS/2 J1.3 with 16 and 256 color driver to test BilBlt operations.
 *
 * Authors: Akamaki.
 *
 *          Copyright 2024-2025 Akamaki.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/mca.h>
#include <86box/rom.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/vid_ps55da2.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include "cpu.h"

#define DA2_FONTROM_PATH_JPAN    "roms/video/da2/94X1320.BIN"
#define DA2_FONTROM_PATH_HANT    "roms/video/da2/23F2698.BIN"
#define DA2_FONTROM_SIZE         (1536 * 1024)
#define DA2_FONTROM_BASESBCS     0x98000
#define DA2_GAIJIRAM_SBCS        0x34000
#define DA2_GAIJIRAM_SBEX        0x3c000
#define DA2_VM03_BASECHR         0x18000
#define DA2_VM03_BASEEXATTR      0x10000
#define DA2_INVALIDACCESS8       0xffu
#define DA2_INVALIDACCESS16      0xffffu
#define DA2_INVALIDACCESS32      0xffffffffu
#define DA2_MASK_MMIO            0x1ffff
#define DA2_SIZE_VRAM            (1024 * 1024) /* 0x100000 */
#define DA2_SIZE_CRAM            (4 * 1024)    /* 0x1000 */
#define DA2_SIZE_GAIJIRAM        (256 * 1024)  /* 0x40000 */
#define DA2_MASK_A000            0x1ffff       /* 0x1FFFF */
#define DA2_MASK_CRAM            0xfff         /* 0xFFF */
#define DA2_MASK_GAIJIRAM        0x3ffff       /* 0x3FFFF */
#define DA2_MASK_VRAM            0xfffff       /* 0xFFFFF */
#define DA2_MASK_VRAMPLANE       0x1ffff       /* 0x1FFFF */
#define DA2_PIXELCLOCK           58000000.0    /* 58 MHz interlaced */
#define DA2_BLT_MEMSIZE          0x10
#define DA2_BLT_REGSIZE          0x40
#define DA2_DEBUG_BLTLOG_SIZE    (DA2_BLT_REGSIZE + 1)
#define DA2_DEBUG_BLTLOG_MAX     (256 * 1024)
#define DA2_DEBUG_BLT_NEVERUSED  0xfefefefe
#define DA2_DEBUG_BLT_USEDRESET  0xfefefe
#define DA2_DCONFIG_CHARSET_JPAN 0 /* for Code page 932 Japanese */
// #define DA2_DCONFIG_CHARSET_HANG 1 /* for Code page 934 Hangul */
// #define DA2_DCONFIG_CHARSET_HANS 2 /* for Code page 936 Simplified Chinese */
/* These ROMs are currently not found.
   At least, IBM-J released a Korean and PRC version of PS/55 for each market. */
#define DA2_DCONFIG_CHARSET_HANT  3 /* for Code page 938 Traditional Chinese */
#define DA2_DCONFIG_MONTYPE_COLOR 0x0A
#define DA2_DCONFIG_MONTYPE_8515  0x0B
#define DA2_DCONFIG_MONTYPE_MONO  0x09

#define DA2_BLT_CIDLE             0
#define DA2_BLT_CFILLRECT         1
#define DA2_BLT_CFILLTILE         2
#define DA2_BLT_CCOPYF            3
#define DA2_BLT_CCOPYR            4
#define DA2_BLT_CPUTCHAR          5
#define DA2_BLT_CLINE             6
#define DA2_BLT_CDONE             7
#define DA2_BLT_CLOAD             8
/* POS ID = 0xeffe : Display Adapter II, III, V  */
#define DA2_POSID_H 0xef
#define DA2_POSID_L 0xfe
/*
     [Identification]
    POS ID  SYS ID
    EFFFh   *       Display Adapter (PS/55 Model 5571-S0A) [Toledo]
    E013h   *       Layout Display Terminal (PS/55-5571 RPQ model) [LDT]
    EFFEh   *       Display Adapter II (I/O 3E0:0A = xx0x xxxx) [Atlas]
    |-      FFF2h   Display Adapter B2 (I/O 3E0:0A = xx1x xxxx) (PS/55 Model 5530Z-SX)
    |-      FDFEh   Display Adapter B2 (I/O 3E0:0A = xx1x xxxx) (PS/55 Model 5550-V2)
    |-      *       Display Adapter III,V (I/O 3E0:0A = xx1x xxxx)
    ECECh   FF4Fh   Display Adapter B1 (PS/55 Model 5531Z-SX) [Atlas-KENT]
    |-      *       Display Adapter IV
    ECCEh   *       Display Adapter IV
    901Fh   *       Display Adapter A2
    901Dh   *       Display Adapter A1 [Atlas II]
    901Eh   *       Plasma Display Adapter
    EFD8h   *       Display Adapter/J [Atlas-SP2]

     [Japanese DOS and Display Adapter compatibility]
    |            |                             | 5605JBK | 5605PAA | 5605PCA | 5605PDE | 5605PAW | 5605PXB |
    | POS ID     | Adapter Name                | K3.31   | J4.05   | J4.08   | J1.33   | 3.02    | 2.0     |
    |------------|-----------------------------|:-------:|:-------:|:-------:|:-------:|:-------:|:-------:|
    | EFFFh      | Display Adapter             |    X    |         |         |         |         |         |
    | FFEDh      | ? [Atlas EVT]               |    X    |         |         |         |         |         |
    | FFFDh      | ? [LDT EVT]                 |    X    |         |         |         |         |         |
    | EFFEh      | Display Adapter II,III,V,B2 |    X    |    X    |    X    |    X    |    X    |    X    |
    | E013h      | ? [LDT]                     |    X    |    X    |    X    |    X    |         |    X    |
    | ECCEh      | Display Adapter IV          |         |    X    |    X    |    X    |         |    X    |
    | ECECh      | Display Adapter IV,B1       |         |    X    |    X    |    X    |    X    |    X    |
    | 9000-901Fh | Display Adapter A1,A2       |         |    X    |    X    |         |    X    |    X    |
    | EFD8h      | Display Adapter /J          |         |         |    X    |    X    |    X    |    X    |

     - 5605JBK : Japanese DOS K3.3, K3.4 (for PS/55 model 55xx-S, T, V, Z)
     - 5605PAA : DOS J4.0
     - 5605PCA : DOS J4.0 (with MKK)
     - 5605PDE : OS/2 Extended Edition J1.3
     - 5605PAW : Japanese Microsoft Windows 3.0 (IBMJ OEM)
     - 5605PXB : DOS/V Extension V2.0
    */
/* IO 3E0/3E1:0Ah Hardware Configuration Value L (imported from OS/2 DDK) */
#define OldLSI 0x20 /* 1 = DA-2, 0 = DA-3 */
// #define  Mon_ID3   0x10
#define FontCard  0x08 /* ? */
#define Page_One  0x06 /*  80000h 110b */
#define Page_Two  0x05 /* 100000h 101b */
#define Page_Four 0x03 /* 200000h 011b */
/* Page Number Mask : Memory Size? = (110b and 111b): vram size is 512k (256 color mode unsupported). */

/* IO 3E0/3E1:0Bh Hardware Configuration Value H (imported from OS/2 DDK) */
#define AddPage 0x08 /* ? */
// #define  Mon_ID2   0x04
// #define  Mon_ID1   0x02
// #define  Mon_ID0   0x01

/* Monitor ID (imported from OS/2 DDK 1.2)
#define  StarbuckC     0x0A  // 1 010b IBM 8514, 9518 color 1040x768
#define  StarbuckM     0x09  // 1 001b IBM 8507, 8604 grayscale
#define  Lark_B        0x02  // 0 010b IBM 9517       color 1040x768 but 4bpp
#define  Dallas        0x0B  // 1 011b IBM 8515, 9515 color 1040x740 another palette
*/

/* DA2 Registers (imported from OS/2 DDK) */
#define AC_REG                  0x3EE
#define AC_DMAE                 0x80
#define AC_FONT_SEL             0x40
#define FONT_BANK               0x3EF
#define LS_INDEX                0x3E0
#define LS_DATA                 0x3E1
#define LS_RESET                0x00
#define LS_MODE                 0x02
#define LS_STATUS               0x03 /* added */
#define LS_MMIO                 0x08 /* added */
#define LS_CONFIG1              0x0a
#define LS_CONFIG2              0x0b /* added */
#define LF_INDEX                0x3e2
#define LF_DATA                 0x3e3
#define LF_MMIO_SEL             0x08 /* added */
#define LF_MMIO_09              0x09 /* added */
#define LF_MMIO_ADDR            0x0A /* added */
#define LF_MMIO_MODE            0x0B /* added */
#define LF_PD_RESET             0x18 /* added */
#define LC_INDEX                0x3E4
#define LC_DATA                 0x3E5
#define LC_HORIZONTAL_TOTAL     0x00
#define LC_H_DISPLAY_ENABLE_END 0x01
#define LC_START_H_BLANKING     0x02
#define LC_END_H_BLANKING       0x03
#define LC_START_HSYNC_PULSE    0x04
#define LC_END_HSYNC_PULSE      0x05
#define LC_VERTICAL_TOTALJ      0x06
#define LC_CRTC_OVERFLOW        0x07
#define LC_PRESET_ROW_SCANJ     0x08
#define LC_MAXIMUM_SCAN_LINE    0x09
#define LC_CURSOR_ROW_START     0x0A
#define LC_CURSOR_ROW_END       0x0B
#define LC_START_ADDRESS_HIGH   0x0C
#define LC_START_ADDRESS_LOW    0x0D
#define LC_CURSOR_LOC_HIGH      0x0E
#define LC_ROW_CURSOR_LOC       0x0E
#define LC_CURSOR_LOC_LOWJ      0x0F
#define LC_COLUMN_CURSOR_LOC    0x0F
#define LC_VERTICAL_SYNC_START  0x10
#define LC_LIGHT_PEN_HIGH       0x10
#define LC_VERTICAL_SYNC_END    0x11
#define LC_LIGHT_PEN_LOW        0x11
#define LC_V_DISPLAY_ENABLE_END 0x12
#define LC_OFFSET               0x13
#define LC_UNDERLINE_LOCATION   0x14
#define LC_START_VERTICAL_BLANK 0x15
#define LC_END_VERTICAL_BLANK   0x16
#define LC_LC_MODE_CONTROL      0x17
#define LC_LINE_COMPAREJ        0x18
#define LC_START_H_DISPLAY_ENAB 0x19
#define LC_START_V_DISPLAY_ENAB 0x1A
#define LC_VIEWPORT_COMMAND     0x1B
#define LC_VIEWPORT_SELECT      0x1C
#define LC_VIEWPORT_PRIORITY    0x1D
#define LC_COMMAND              0x1E
#define LC_COMPATIBILITY        0x1F
#define LC_VIEWPORT_NUMBER      0x1F
#define LV_PORT                 0x3E8
#define LV_PALETTE_0            0x00
#define LV_MODE_CONTROL         0x10
#define LV_OVERSCAN_COLOR       0x11
#define LV_COLOR_PLANE_ENAB     0x12
#define LV_PANNING              0x13
#define LV_VIEWPORT1_BG         0x14
#define LV_VIEWPORT2_BG         0x15
#define LV_VIEWPORT3_BG         0x16
#define LV_BLINK_COLOR          0x17
#define LV_BLINK_CODE           0x18
#define LV_GR_CURSOR_ROTATION   0x19
#define LV_GR_CURSOR_COLOR      0x1A
#define LV_GR_CURSOR_CONTROL    0x1B
#define LV_COMMAND              0x1C
#define LV_VP_BORDER_LINE       0x1D
#define LV_SYNC_POLARITY        0x1F
#define LV_CURSOR_CODE_0        0x20
#define LV_GRID_COLOR_0         0x34
#define LV_GRID_COLOR_1         0x35
#define LV_GRID_COLOR_2         0x36
#define LV_GRID_COLOR_3         0x37
#define LV_ATTRIBUTE_CNTL       0x38
#define LV_CURSOR_COLOR         0x3A
#define LV_CURSOR_CONTROL       0x3B
#define LV_RAS_STATUS_VIDEO     0x3C
#define LV_PAS_STATUS_CNTRL     0x3D
#define LV_IDENTIFICATION       0x3E
#define LV_OUTPUT               0x3E
#define LV_COMPATIBILITY        0x3F
#define LG_INDEX                0x3EA
#define LG_DATA                 0x3EB
#define LG_SET_RESETJ           0x00
#define LG_ENABLE_SRJ           0x01
#define LG_COLOR_COMPAREJ       0x02
#define LG_DATA_ROTATION        0x03
#define LG_READ_MAP_SELECT      0x04
#define LG_MODE                 0x05
#define LG_COMPLEMENT           0x06
#define LG_COLOR_DONT_CARE      0x07
#define LG_BIT_MASK_LOW         0x08
#define LG_BIT_MASK_HIGH        0x09
#define LG_MAP_MASKJ            0x0A
#define LG_COMMAND              0x0B
#define LG_SET_RESET_2          0x10

#ifndef RELEASE_BUILD
// #define ENABLE_DA2_LOG 1
#endif

#ifdef ENABLE_DA2_LOG
#    define ENABLE_DA2_DEBUGIO 1
#    define ENABLE_DA2_DEBUGBLT 1
// #    define ENABLE_DA2_DEBUGVRAM 1
// #    define ENABLE_DA2_DEBUGFULLSCREEN 1
// #    define ENABLE_DA2_DEBUGMONWAIT 1
int da2_do_log = ENABLE_DA2_LOG;

static void
da2_log(const char *fmt, ...)
{
    va_list ap;

    if (da2_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define da2_log(fmt, ...)
#endif
#ifdef ENABLE_DA2_DEBUGIO
#    define da2_iolog da2_log
#else
#    define da2_iolog(fmt, ...)
#endif
#ifdef ENABLE_DA2_DEBUGBLT
#    define da2_bltlog da2_log
#    define ENABLE_DA2_DEBUGBLT_DETAIL 1
#    ifdef ENABLE_DA2_DEBUGBLT_DETAIL
#        define da2_bltreglog da2_log
#    else
#        define da2_bltreglog(fmt, ...)
#    endif
#else
#    define da2_bltlog(fmt, ...)
#    define da2_bltreglog(fmt, ...)
#endif

typedef struct da2_t {
    mem_mapping_t cmapping;

    uint8_t  ioctl[16];
    uint8_t  fctl[32];
    uint16_t crtc[32];
    uint16_t crtc_vpreg[128];
    uint8_t  crtc_vpsel;
    uint8_t  gdcreg[64];
    uint8_t  reg3ee[16];
    int      gdcaddr;
    uint8_t  attrc[0x40];
    int      attraddr, attrff;
    int      attr_palette_enable;
    int      outflipflop;
    int      inflipflop;
    int      iolatch;

    int ioctladdr;
    int fctladdr;
    int crtcaddr;

    uint32_t gdcla[8];
    uint32_t gdcinput[8];
    uint32_t gdcsrc[8];

    uint8_t dac_mask, dac_status;
    int     dac_read, dac_write, dac_pos;
    int     dac_r, dac_g;

    uint8_t cgastat;

    uint8_t plane_mask;

    int      writemode, readplane;
    uint8_t  planemask;

    uint8_t  egapal[16];
    uint32_t pallook[512];
    PALETTE  vgapal;

    int      vtotal, dispend, vsyncstart, split, vblankstart;
    int      hdisp, htotal, hdisp_time, rowoffset;
    int      lowres;
    int      rowcount;
    double   clock;
    uint32_t memaddr_latch, ca_adj;

    uint64_t   dispontime, dispofftime;
    pc_timer_t timer;
    uint64_t   da2const;

    int dispon;
    int hdisp_on;

    uint32_t memaddr, memaddr_backup, cursoraddr;
    int      vc;
    int      scanline;
    int      linepos, vslines, linecountff;
    int      cursorvisible, cursoron, blink, blinkconf;
    int      scrollcache;
    int      char_width;

    int firstline, lastline;
    int firstline_draw, lastline_draw;
    int displine;
    int oddeven;

    /* Attribute Buffer E0000-E0FFFh (4 KB) */
    uint8_t *cram;
    /* APA Buffer A0000-BFFFFh (128 KB) */
    uint8_t *vram;
    uint8_t *changedvram;
    uint32_t vram_display_mask;

    int fullchange;

    void (*render)(struct da2_t *da2);

    /*If set then another device is driving the monitor output and the SVGA
      card should not attempt to display anything */
    int override;

    struct {
        int           enable;
        mem_mapping_t mapping;
        uint8_t       ram[DA2_SIZE_GAIJIRAM];
        uint8_t      *font;
        int           charset;
    } mmio;

    struct {
        int        bitshift_destr;
        int        raster_op;
        uint8_t    payload[DA2_BLT_MEMSIZE];
        int        payload_addr;
        int        payload_opsize;
        int32_t    reg[DA2_BLT_REGSIZE]; // must be signed int
#ifdef ENABLE_DA2_DEBUGBLT
        int32_t   *debug_reg;            // for debug
        int        debug_reg_ip;         // for debug
        int        debug_exesteps;
#endif
        pc_timer_t timer;
        int64_t    timerspeed;
        int        exec;
        int        indata;
        int32_t    destaddr;
        int32_t    srcaddr;
        int16_t    size_x;
        int16_t    tile_w;
        int16_t    size_y;
        int16_t    dest_x;
        int16_t    dest_y;
        int16_t    destpitch;
        int16_t    srcpitch;
        int32_t    fcolor;
        int32_t    maskl, maskr;
        int32_t    count;
        int32_t    d;
        int8_t     destoption;
        int        octdir;
        int        x, y, wx1, wx2, wy1, wy2;
    } bitblt;

#ifdef ENABLE_DA2_DEBUGVRAM
    FILE    *mmdbg_fp;
    FILE    *mmrdbg_fp;
    uint32_t mmdbg_vidaddr;
    uint32_t mmrdbg_vidaddr;
#endif

    uint8_t    pos_regs[8];
    svga_t    *mb_vga;
    uint8_t    monitorid;
    pc_timer_t timer_vidupd;

    int old_pos2;
} da2_t;

static video_timings_t timing_da2_mca = 
{ .type = VIDEO_MCA, .write_b = 4, .write_w = 4, .write_l =  10, .read_b = 4, .read_w = 4, .read_l = 10 };

static void     da2_recalctimings(da2_t *da2);
static void     da2_mmio_gc_writeW(uint32_t addr, uint16_t val, void *p);
static void     da2_bitblt_exec(void *p);
static void     da2_updatevidselector(da2_t *da2);
static void     da2_reset_ioctl(da2_t *da2);
static void     da2_reset(void *priv);
static uint16_t da2_rightrotate(uint16_t data, uint8_t count);

typedef union {
    uint32_t d;
    uint8_t  b[4];
} vidseq32;

typedef struct {
    uint32_t p8[8];
} pixel32;

/* safety read for internal functions */
static uint32_t
da2_vram_r(uint32_t addr, da2_t *da2)
{
    if (addr & ~DA2_MASK_VRAM)
        return DA2_INVALIDACCESS32;
    return da2->vram[addr];
}
/* safety write for internal functions */
static void
da2_vram_w(uint32_t addr, uint8_t val, da2_t *da2)
{
    if (addr & ~DA2_MASK_VRAM)
        return;
    da2->vram[addr] = val;
    return;
}

/*
Param   Desc
01      Color
03      Bit Shift
04      Select plane?
05      Direction (1000h or 1100h) + Command? (40 or 48)
08      Mask Left
09      Mask Right
0A      Plane Mask?
0B      ROP
            2000 NOT Source
            200  Source / Pattern
            20   NOT Dest
            10   NOT Pattern
            8    ?
            0-3  Bit op (0 None, 1 AND, 2 OR, 3 XOR)
0D
20      Exec (1) or Exec without reset regs? (21h)
21      Dest Pitch
22      Src Pitch
23      Tile W
28      Tile H
29      Dest Addr
2A      Src Addr
2B      Tile Addr
2D      Octant for line drawing
2F      Dest Write Option
32      wx1, Dest X
33      wx2, Size W
34      wy1, Dest Y
35      wy2, Size H
*/
/* write pixel data with rop (Note: bitmask must be in big endian) */
static void
da2_WritePlaneDataWithBitmask(uint32_t destaddr, const uint16_t mask, pixel32 *srcpx, da2_t *da2)
{
    uint32_t writepx[8];
    destaddr &= 0xfffffffe; /* align to word address to work bit shift correctly */
    // da2_log("DA2_WPDWB addr %x mask %x rop %x shift %d\n", destaddr, mask, da2->bitblt.raster_op, da2->bitblt.bitshift_destr);
    da2->changedvram[(DA2_MASK_VRAMPLANE & destaddr) >> 9]       = 3;
    destaddr <<= 3;
    /* read destination data with big endian order */
    for (uint8_t i = 0; i < 8; i++)
        writepx[i] = da2_vram_r((destaddr + 24) | i, da2)
            | (da2_vram_r((destaddr + 16) | i, da2) << 8)
            | (da2_vram_r((destaddr + 8) | i, da2) << 16)
            | (da2_vram_r((destaddr + 0) | i, da2) << 24);

    vidseq32 mask32in;
    mask32in.d = (uint32_t) mask;
    vidseq32 mask32;
    mask32.b[3] = mask32in.b[0];
    mask32.b[2] = mask32in.b[1];
    mask32.d &= 0xffff0000;
    for (uint8_t i = 0; i < 8; i++) {
        if (da2->bitblt.bitshift_destr > 0)
            srcpx->p8[i] <<= 16 - da2->bitblt.bitshift_destr;
// #ifdef ENABLE_DA2_DEBUGBLT
//         if (i == 0) {
//             pclog("writeplane: src %08X mask %08X dest %08X\n", srcpx->p8[i], mask32.d, writepx[i]);
//         }
// #endif
        if (da2->bitblt.raster_op & 0x2010) /* NOT Src or NOT Pattern */
            srcpx->p8[i] = ~srcpx->p8[i] & mask32.d;
        if (da2->bitblt.raster_op & 0x20) /* Dest NOT */
            writepx[i] = (~writepx[i] & mask32.d) | (writepx[i] & ~mask32.d);
        switch (da2->bitblt.raster_op & 0x03) {
            case 0x00: /* None */
                writepx[i] &= ~mask32.d;
                writepx[i] |= srcpx->p8[i] & mask32.d;
                break;
            case 0x01: /* AND */
                writepx[i] &= srcpx->p8[i] | ~mask32.d;
                break;
            case 0x02: /* OR */
                writepx[i] |= srcpx->p8[i] & mask32.d;
                break;
            case 0x03: /* XOR */
                writepx[i] ^= srcpx->p8[i] & mask32.d;
                break;
        }
    }
    for (uint8_t i = 0; i < 8; i++) {
        da2_vram_w(destaddr | i, (writepx[i] >> 24) & 0xff, da2);
        da2_vram_w((destaddr + 8) | i, (writepx[i] >> 16) & 0xff, da2);
    }
}

static void
da2_DrawColorWithBitmask(uint32_t destaddr, uint8_t color, uint16_t mask, da2_t *da2)
{
    pixel32 srcpx;
    /* fill data with input color */
    for (uint8_t i = 0; i < 8; i++)
        srcpx.p8[i] = (color & (1 << i)) ? 0xffffffff : 0; /* read in word */

    da2_WritePlaneDataWithBitmask(destaddr, mask, &srcpx, da2);
}
static void
da2_CopyPlaneDataWithBitmask(uint32_t srcaddr, uint32_t destaddr, uint16_t mask, da2_t *da2)
{
    pixel32 srcpx;
    srcaddr &= 0xfffffffe;
    srcaddr <<= 3;
    for (uint8_t i = 0; i < 8; i++)
        srcpx.p8[i] = da2_vram_r((srcaddr + 24) | i, da2)
            | (da2_vram_r((srcaddr + 16) | i, da2) << 8)
            | (da2_vram_r((srcaddr + 8) | i, da2) << 16)
            | (da2_vram_r((srcaddr + 0) | i, da2) << 24);

    da2_WritePlaneDataWithBitmask(destaddr, mask, &srcpx, da2);
}
/* get font data for bitblt operation */
static uint32_t
getRAMFont(int32_t code, int line, int x, void *priv)
{
    da2_t   *da2   = (da2_t *) priv;
    uint32_t font  = 0;
#ifdef RESERVED_FOR_FUTURE_USE
    int      fline = line - 2; /* Start line of drawing character (line >= 1 AND line < 24 + 1 ) */
#endif
    if (code < 0x100) {        /* SBCS 13x29 */
        code *= 0x40;
        code += DA2_GAIJIRAM_SBCS + (line * 2) + x;
        font = da2->mmio.ram[code];
        font <<= 8;
        font |= da2->mmio.ram[code + 1];
        font <<= 16;
    } else if ((code >= 0xb000) && (code <= 0xb75f)) { /* DBCS 26x29  */
        /* convert code->address in gaiji memory */
        code -= 0xb000;
        code = (code * 0x80) + (line * 4) + x;
        font = da2->mmio.ram[code];
        font <<= 8;
        font |= da2->mmio.ram[code + 1];
        font <<= 8;
        font |= da2->mmio.ram[code + 2];
        font <<= 8;
        font |= da2->mmio.ram[code + 3];
    } else
        font = 0;
    return font;
}
#ifdef RESERVED_FOR_FUTURE_USE
/* Reverse the bit order of attribute code IBGR to IRGB(used in Mode 3 and Cursor Color) */
static int8_t
IBGRtoIRGB(uint8_t attr)
{
    attr = ((attr & 0x01) << 6) | ((attr & 0x02) << 4) | ((attr & 0x04) << 2) | ((attr & 0x08) << 4);
    return attr >>= 4;
}
#endif
static void
da2_PutcharWithBitmask(uint32_t codeIBMJ, int width, uint16_t attr, int line, uint32_t destaddr, uint16_t maskl, uint16_t maskr, da2_t *da2)
{
    pixel32 srcpx;
    uint8_t fg = (~attr >> 8) & 0x0f;
    uint8_t bg = (~attr >> 12) & 0x0f;
    uint32_t font = getRAMFont(codeIBMJ, line, 0, da2);
    uint32_t fontinv;
    if (width <= 2) {
        fontinv = ~font;
        for (uint8_t i = 0; i < 8; i++) {
            srcpx.p8[i] = (fg & (1 << i)) ? font >> 16 : 0;
            srcpx.p8[i] |= (bg & (1 << i)) ? fontinv >> 16 : 0;
        }
        da2_WritePlaneDataWithBitmask(destaddr, maskl, &srcpx, da2);
        for (uint8_t i = 0; i < 8; i++) {
            srcpx.p8[i] = (fg & (1 << i)) ? font : 0;
            srcpx.p8[i] |= (bg & (1 << i)) ? fontinv : 0;
        }
        da2_WritePlaneDataWithBitmask(destaddr + 2, maskr, &srcpx, da2);
    } else {
        font = (font & 0xfff80000) | ((font & 0x0000ffff) << 3);
        fontinv = ~font;
        for (uint8_t i = 0; i < 8; i++) {
            srcpx.p8[i] = (fg & (1 << i)) ? font >> 16 : 0;
            srcpx.p8[i] |= (bg & (1 << i)) ? fontinv >> 16 : 0;
        }
        da2_WritePlaneDataWithBitmask(destaddr, maskl, &srcpx, da2);
        for (uint8_t i = 0; i < 8; i++) {
            srcpx.p8[i] = (fg & (1 << i)) ? font : 0;
            srcpx.p8[i] |= (bg & (1 << i)) ? fontinv : 0;
        }
        if (da2->bitblt.destoption & 0x20) {
            da2_WritePlaneDataWithBitmask(destaddr + 2,  maskr, &srcpx, da2);
        } else {
            da2_WritePlaneDataWithBitmask(destaddr + 2, 0xffff, &srcpx, da2);
            for (uint8_t i = 0; i < 8; i++) {
                srcpx.p8[i] = (fg & (1 << i)) ? font << 16 : 0;
                srcpx.p8[i] |= (bg & (1 << i)) ? fontinv << 16 : 0;
            }
            da2_WritePlaneDataWithBitmask(destaddr + 4, maskr, &srcpx, da2);
        }
    }
}
#ifdef ENABLE_DA2_DEBUGBLT
static uint8_t
pixel1tohex(uint32_t addr, int index, da2_t *da2)
{
    uint8_t pixeldata = 0;
    for (uint8_t i = 0; i < 8; j++) {
        if (da2_vram_r(((addr << 3) | i) & (1 << (7 - index)), da2))
            pixeldata++;
    }
    return pixeldata;
}
static void
print_pixelbyte(uint32_t addr, da2_t *da2)
{
    for (uint8_t i = 0; i < 8; i++) {
        pclog("%X", pixel1tohex(addr, i, da2));
    }
}
static void
print_bytetobin(uint8_t b)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (b & 0x80)
            pclog("1");
        else
            pclog("0");
        b <<= 1;
    }
}
/* Convert internal char code to Shift JIS code */
static int
isKanji1(uint8_t chr)
{
    return (chr >= 0x81 && chr <= 0x9f) || (chr >= 0xe0 && chr <= 0xfc);
}
static int
isKanji2(uint8_t chr)
{
    return (chr >= 0x40 && chr <= 0x7e) || (chr >= 0x80 && chr <= 0xfc);
}
static uint16_t
IBMJtoSJIS(uint16_t knj)
{
    if (knj < 0x100)
        return 0xffff;
    knj -= 0x100;
    if (knj <= 0x1f7d)
        ; /* do nothing */
    else if (knj >= 0xb700 && knj <= 0xb75f)
        knj -= 0x90ec;
    else if (knj >= 0xb3f0 && knj <= 0xb67f)
        knj -= 0x906c;
    else if (knj >= 0x8000 && knj <= 0x8183)
        knj -= 0x5524;
    else
        return 0xffff;
    uint32_t knj1 = knj / 0xBC;
    uint32_t knj2 = knj - (knj1 * 0xBC);
    knj1 += 0x81;
    if (knj1 > 0x9F)
        knj1 += 0x40;
    knj2 += 0x40;
    if (knj2 > 0x7E)
        knj2++;
    // if (!isKanji1(knj1)) return 0xffff;
    // if (!isKanji2(knj2)) return 0xffff;
    knj = knj1 << 8;
    knj |= knj2;
    return knj;
}
#endif

static void
da2_bitblt_parse(da2_t *da2)
{
    uint32_t value32;
    uint64_t value64;
    switch (da2->bitblt.payload[0]) {
        case 0x88:
        case 0x89:
        case 0x95:
            value32 = da2->bitblt.payload[3];
            value32 <<= 8;
            value32 |= da2->bitblt.payload[2];
            da2_bltreglog("[%02x] %02x: %04x (%d)\n", da2->bitblt.payload[0], da2->bitblt.payload[1], value32, value32);
            da2->bitblt.reg[da2->bitblt.payload[1]] = value32;
            break;
        case 0x91:
            value32 = da2->bitblt.payload[5];
            value32 <<= 8;
            value32 |= da2->bitblt.payload[4];
            value32 <<= 8;
            value32 |= da2->bitblt.payload[3];
            value32 <<= 8;
            value32 |= da2->bitblt.payload[2];
            da2_bltreglog("[%02x] %02x: %08x (%d)\n", da2->bitblt.payload[0], da2->bitblt.payload[1], value32, value32);
            da2->bitblt.reg[da2->bitblt.payload[1]] = value32;
            break;
        case 0x99:
            value64 = da2->bitblt.payload[7];
            value64 <<= 8;
            value64 = da2->bitblt.payload[6];
            value64 <<= 8;
            value64 = da2->bitblt.payload[5];
            value64 <<= 8;
            value64 |= da2->bitblt.payload[4];
            value64 <<= 8;
            value64 |= da2->bitblt.payload[3];
            value64 <<= 8;
            value64 |= da2->bitblt.payload[2];
            da2_bltreglog("[%02x] %02x: %02x %02x %02x %02x %02x %02x\n", da2->bitblt.payload[0], da2->bitblt.payload[1], da2->bitblt.payload[2], da2->bitblt.payload[3],
                          da2->bitblt.payload[4], da2->bitblt.payload[5], da2->bitblt.payload[6], da2->bitblt.payload[7]);
            da2->bitblt.reg[da2->bitblt.payload[1]] = value64;
            break;
        case 0xa1:
            value64 = da2->bitblt.payload[9];
            value64 <<= 8;
            value64 |= da2->bitblt.payload[8];
            value64 <<= 8;
            value64 |= da2->bitblt.payload[7];
            value64 <<= 8;
            value64 |= da2->bitblt.payload[6];
            value64 <<= 8;
            value64 |= da2->bitblt.payload[5];
            value64 <<= 8;
            value64 |= da2->bitblt.payload[4];
            value64 <<= 8;
            value64 |= da2->bitblt.payload[3];
            value64 <<= 8;
            value64 |= da2->bitblt.payload[2];
            da2_bltreglog("[%02x] %02x: %02x %02x %02x %02x %02x %02x %02x %02x\n", da2->bitblt.payload[0], da2->bitblt.payload[1], da2->bitblt.payload[2], da2->bitblt.payload[3],
                          da2->bitblt.payload[4], da2->bitblt.payload[5], da2->bitblt.payload[6], da2->bitblt.payload[7], da2->bitblt.payload[8], da2->bitblt.payload[9]);
            da2->bitblt.reg[da2->bitblt.payload[1]] = value64;
            break;
        case 0x00: /* Win 3.0 Clock writes invalid zero data. */
            break;
        default:
            da2_log("bltload: Unknown PreOP!\n");
            break;
    }
    if (da2->bitblt.reg[0x20] & 0x01) { /* Execute Bitblt immediately (for OS/2 J1.3) */
        da2->bitblt.exec = DA2_BLT_CLOAD;
        da2_bitblt_exec(da2);
    }
    /* clear payload memory */
    memset(da2->bitblt.payload, 0x00, DA2_BLT_MEMSIZE);
    da2->bitblt.payload_addr = 0;
    da2->bitblt.indata       = 0;
}

static void
da2_bitblt_load(da2_t *da2)
{
    da2->bitblt.reg[0x20] = 0; /* need to stop execution */
#ifdef ENABLE_DA2_DEBUGBLT
    for (int i = 0; i < DA2_DEBUG_BLTLOG_SIZE; i++) {
        da2->bitblt.debug_reg[DA2_DEBUG_BLTLOG_SIZE * da2->bitblt.debug_reg_ip + i] = da2->bitblt.reg[i];
    }
    da2->bitblt.debug_reg[DA2_DEBUG_BLTLOG_SIZE * (da2->bitblt.debug_reg_ip + 1) - 1] = 0;
    da2->bitblt.debug_reg_ip++;
    if ((DA2_DEBUG_BLTLOG_SIZE * da2->bitblt.debug_reg_ip) >= DA2_DEBUG_BLTLOG_MAX)
        da2->bitblt.debug_reg_ip = 0;
    da2->bitblt.debug_exesteps = 0;
#endif
    da2->bitblt.bitshift_destr = ((da2->bitblt.reg[0x03] >> 4) & 0x0f); /* set bit shift */
    da2->bitblt.raster_op      = da2->bitblt.reg[0x0b];
    da2->bitblt.destaddr       = da2->bitblt.reg[0x29];
    da2->bitblt.size_x         = da2->bitblt.reg[0x33];
    da2->bitblt.size_y         = da2->bitblt.reg[0x35];
    da2->bitblt.destpitch      = da2->bitblt.reg[0x21];
    da2->bitblt.srcpitch       = da2->bitblt.reg[0x22];
    /*
        DOS/V Extension 1040x725 some DBCS uses 0xB0 others 0x90
    */
    da2->bitblt.destoption = da2->bitblt.reg[0x2F];
    if (da2->bitblt.destoption & 0x10) { /* destaddr -= 2, length += 1; */
        da2->bitblt.destaddr -= 2;
        da2->bitblt.size_x += 1;
        da2->bitblt.destpitch -= 2;
        da2->bitblt.srcpitch -= 2;
    }
    da2->bitblt.fcolor = da2->bitblt.reg[0x0];
    da2->bitblt.maskl  = da2->bitblt.reg[0x8];
    da2->bitblt.maskr  = da2->bitblt.reg[0x9];
    da2->bitblt.x      = 0;
    da2->bitblt.y      = 0;
    da2->bitblt.exec   = DA2_BLT_CDONE;

    /* Put DBCS char used by OS/2 and DOS/V Extension */
    if (!(da2->bitblt.reg[0xb] & 0x08)) {
        da2->bitblt.exec    = DA2_BLT_CPUTCHAR;
        da2->bitblt.fcolor  = da2->bitblt.reg[0x1];
        da2->bitblt.srcaddr = da2->bitblt.reg[0x12];
        da2->bitblt.destaddr += 2;
#ifdef ENABLE_DA2_DEBUGBLT
        uint8_t sjis_h = IBMJtoSJIS(da2->bitblt.reg[0x12]) >> 8;
        uint8_t sjis_l = IBMJtoSJIS(da2->bitblt.reg[0x12]) & 0xff;
        if (da2->bitblt.reg[0x12] < 0x100) {
            sjis_h = 0x20;
            sjis_l = da2->bitblt.reg[0x12];
        } else {
            if (!(isKanji1(sjis_h)))
                sjis_h = 0x3f;
            if (!(isKanji2(sjis_l)))
                sjis_l = 0x3f;
        }
        da2_log("put char src=%x, dest=%x, x=%d, y=%d, w=%d, h=%d, c=%c%c\n",
                da2->bitblt.srcaddr, da2->bitblt.destaddr,
                da2->bitblt.reg[0x29] % (da2->rowoffset * 2), da2->bitblt.reg[0x29] / (da2->rowoffset * 2),
                da2->bitblt.size_x, da2->bitblt.size_y, sjis_h, sjis_l);
#else
        da2_log("put char src=%x, dest=%x, x=%d, y=%d, w=%d, h=%d\n",
                da2->bitblt.srcaddr, da2->bitblt.destaddr,
                da2->bitblt.reg[0x29] % (da2->rowoffset * 2), da2->bitblt.reg[0x29] / (da2->rowoffset * 2),
                da2->bitblt.size_x, da2->bitblt.size_y);
#endif

    /* Draw a line */
    } else if (da2->bitblt.reg[0x5] == 0x43) {
        da2->bitblt.exec           = DA2_BLT_CLINE;
        da2->bitblt.dest_x         = (da2->bitblt.reg[0x32] & 0xffff);
        da2->bitblt.dest_y         = (da2->bitblt.reg[0x34] & 0xffff);
        da2->bitblt.wx1            = (da2->bitblt.reg[0x32]) >> 16;
        da2->bitblt.wx2            = (da2->bitblt.reg[0x33]) >> 16;
        da2->bitblt.wy1            = (da2->bitblt.reg[0x34]) >> 16;
        da2->bitblt.wy2            = (da2->bitblt.reg[0x35]) >> 16;
        da2->bitblt.size_x         = abs((int16_t) (da2->bitblt.reg[0x33] & 0xffff) - da2->bitblt.dest_x);
        da2->bitblt.size_y         = abs((int16_t) (da2->bitblt.reg[0x35] & 0xffff) - da2->bitblt.dest_y);
        da2->bitblt.count          = 0;
        da2->bitblt.octdir         = da2->bitblt.reg[0x2D];
        da2->bitblt.bitshift_destr = 0;
        if (da2->bitblt.octdir & 0x04) /* dX > dY */
            da2->bitblt.d = 2 * da2->bitblt.size_y - da2->bitblt.size_x;
        else
            da2->bitblt.d = 2 * da2->bitblt.size_x - da2->bitblt.size_y;
        da2->bitblt.x = da2->bitblt.dest_x;
        da2->bitblt.y = da2->bitblt.dest_y;
        da2_log("drawline x=%d, y=%d, dx=%d, dy=%d, oct=%dn",
                da2->bitblt.dest_x, da2->bitblt.dest_y,
                da2->bitblt.size_x, da2->bitblt.size_y, da2->bitblt.octdir);
        da2_log("         x1=%d, x2=%d, y1=%d, y2=%d\n",
                da2->bitblt.reg[0x32] & 0x7ff, da2->bitblt.reg[0x33] & 0x7ff,
                da2->bitblt.reg[0x34] & 0x7ff, da2->bitblt.reg[0x35] & 0x7ff);
        da2_log("        ux1=%d,ux2=%d,uy1=%d,uy2=%d\n",
                (da2->bitblt.reg[0x32] >> 16) & 0x7ff, (da2->bitblt.reg[0x33] >> 16) & 0x7ff,
                (da2->bitblt.reg[0x34] >> 16) & 0x7ff, (da2->bitblt.reg[0x35] >> 16) & 0x7ff);

    /* Fill a rectangle (or draw a horizontal / vertical line) */
    } else if ((da2->bitblt.reg[0x5] & 0xfff0) == 0x40 && da2->bitblt.reg[0x3D] == 0) {
        da2_log("fillrect x=%d, y=%d, w=%d, h=%d, c=%d, 2f=%x, rowcount=%x\n",
                da2->bitblt.reg[0x29] % (da2->rowoffset * 2), da2->bitblt.reg[0x29] / (da2->rowoffset * 2),
                da2->bitblt.size_x, da2->bitblt.size_y, da2->bitblt.reg[0x0], da2->bitblt.reg[0x2F], da2->rowoffset * 2);
        da2->bitblt.exec = DA2_BLT_CFILLRECT;
        da2->bitblt.destaddr += 2;

    /* Tiling a rectangle ??(transfer tile data multiple times) os/2 only */
    } else if ((da2->bitblt.reg[0x5] & 0xfff0) == 0x0040 && da2->bitblt.reg[0x3D] == 0x40) {
        da2->bitblt.exec = DA2_BLT_CFILLTILE;
        da2->bitblt.destaddr += 2;
        da2->bitblt.srcaddr = da2->bitblt.reg[0x2B];
        da2->bitblt.tile_w  = da2->bitblt.reg[0x28];
        da2_log("copy tile src=%x, dest=%x, x1=%d, y1=%d, x2=%d, y2=%d, w=%d, h=%d\n",
                da2->bitblt.srcaddr, da2->bitblt.destaddr,
                da2->bitblt.reg[0x2B] % (da2->rowoffset * 2), da2->bitblt.reg[0x2B] / (da2->rowoffset * 2),
                da2->bitblt.reg[0x29] % (da2->rowoffset * 2), da2->bitblt.reg[0x29] / (da2->rowoffset * 2),
                da2->bitblt.size_x, da2->bitblt.size_y);

    /* Tiling a rectangle (transfer tile data multiple times) */
    } else if ((da2->bitblt.reg[0x5] & 0xfff0) == 0x1040 && da2->bitblt.reg[0x3D] == 0x40) {
        da2->bitblt.exec = DA2_BLT_CFILLTILE;
        da2->bitblt.destaddr += 2;
        da2->bitblt.srcaddr = da2->bitblt.reg[0x2B];
        da2->bitblt.tile_w  = da2->bitblt.reg[0x28];
        da2_log("copy tile src=%x, dest=%x, x1=%d, y1=%d, x2=%d, y2=%d, w=%d, h=%d\n",
                da2->bitblt.srcaddr, da2->bitblt.destaddr,
                da2->bitblt.reg[0x2B] % (da2->rowoffset * 2), da2->bitblt.reg[0x2B] / (da2->rowoffset * 2),
                da2->bitblt.reg[0x29] % (da2->rowoffset * 2), da2->bitblt.reg[0x29] / (da2->rowoffset * 2),
                da2->bitblt.size_x, da2->bitblt.size_y);

    /* Block copy */
    } else if ((da2->bitblt.reg[0x5] & 0xfff0) == 0x1040 && da2->bitblt.reg[0x3D] == 0x00) {
        da2->bitblt.exec    = DA2_BLT_CCOPYF;
        da2->bitblt.srcaddr = da2->bitblt.reg[0x2A];
        da2->bitblt.destaddr += 2;
        da2_log("copy block src=%x, dest=%x, x1=%d, y1=%d, x2=%d, y2=%d, w=%d, h=%d\n",
                da2->bitblt.srcaddr, da2->bitblt.destaddr,
                da2->bitblt.reg[0x2A] % (da2->rowoffset * 2), da2->bitblt.reg[0x2A] / (da2->rowoffset * 2),
                da2->bitblt.reg[0x29] % (da2->rowoffset * 2), da2->bitblt.reg[0x29] / (da2->rowoffset * 2),
                da2->bitblt.size_x, da2->bitblt.size_y);

    /* Block copy but reversed direction */
    } else if ((da2->bitblt.reg[0x5] & 0xfff0) == 0x1140 && da2->bitblt.reg[0x3D] == 0x00) {
        da2->bitblt.exec    = DA2_BLT_CCOPYR;
        da2->bitblt.srcaddr = da2->bitblt.reg[0x2A];
        da2->bitblt.destaddr -= 2;
        da2->bitblt.srcaddr -= 2;
        da2_log("copy blockR src=%x, dest=%x, x1=%d, y1=%d, x2=%d, y2=%d, w=%d, h=%d\n",
                da2->bitblt.srcaddr, da2->bitblt.destaddr,
                da2->bitblt.reg[0x2A] % (da2->rowoffset * 2), da2->bitblt.reg[0x2A] / (da2->rowoffset * 2),
                da2->bitblt.reg[0x29] % (da2->rowoffset * 2), da2->bitblt.reg[0x29] / (da2->rowoffset * 2),
                da2->bitblt.size_x, da2->bitblt.size_y);
    }
}
static void
da2_bitblt_exec(void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    // timer_set_delay_u64(&da2->bitblt.timer, da2->bitblt.timerspeed);
#ifdef ENABLE_DA2_DEBUGBLT_DETAIL
    if (!(da2->bitblt.debug_exesteps & 0xff))
        da2_log("bitblt_exec: %d %d\n", da2->bitblt.exec, da2->bitblt.debug_exesteps);
    da2->bitblt.debug_exesteps++;
#else
    da2_log("bltload_exec: %x, CS:PC=%4x:%4x\n", da2->bitblt.exec, CS, cpu_state.pc);
#endif
    switch (da2->bitblt.exec) {
        case DA2_BLT_CIDLE:
            // timer_disable(&da2->bitblt.timer);
            break;
        case DA2_BLT_CLOAD:
            da2_bitblt_load(da2);
            // da2->bitblt.indata = 0;
            break;
        case DA2_BLT_CLINE:
            {
                /* Draw a dot */
                da2_bltlog("point: %d %d %d %d %d\n", da2->bitblt.x, da2->bitblt.y, da2->bitblt.d, da2->bitblt.x, da2->bitblt.y);
                int destaddr  = da2->bitblt.y * (da2->rowoffset * 2) + da2->bitblt.x / 8;
                int pixelmask = da2->bitblt.x % 16;
                if (pixelmask >= 8)
                    pixelmask = (0x8000 >> (pixelmask - 8));
                else
                    pixelmask = (0x80 >> pixelmask);

                /* check the current position is inside the window */
                if (da2->bitblt.x < da2->bitblt.wx1 || da2->bitblt.x > da2->bitblt.wx2
                    || da2->bitblt.y < da2->bitblt.wy1 || da2->bitblt.y > da2->bitblt.wy2)
                    ;
                else
                    da2_DrawColorWithBitmask(destaddr, da2->bitblt.fcolor, pixelmask, da2);
                da2->bitblt.count++;

                /* calculate the next position with Bresenham's line algorithm */
                if (da2->bitblt.octdir & 0x04) { /* dX > dY */
                    if (da2->bitblt.octdir & 0x02) {
                        da2->bitblt.x++;
                    } else {
                        da2->bitblt.x--;
                    }
                    if (da2->bitblt.d >= 0) {
                        da2->bitblt.d -= (2 * da2->bitblt.size_x);
                        if (da2->bitblt.octdir & 0x01) {
                            da2->bitblt.y++;
                        } else {
                            da2->bitblt.y--;
                        }
                    }
                    da2->bitblt.d += (2 * da2->bitblt.size_y);
                    if (da2->bitblt.count >= da2->bitblt.size_x)
                        da2->bitblt.exec = DA2_BLT_CDONE;
                } else {
                    if (da2->bitblt.octdir & 0x01) {
                        da2->bitblt.y++;
                    } else {
                        da2->bitblt.y--;
                    }
                    if (da2->bitblt.d >= 0) {
                        da2->bitblt.d -= (2 * da2->bitblt.size_y);
                        if (da2->bitblt.octdir & 0x02) {
                            da2->bitblt.x++;
                        } else {
                            da2->bitblt.x--;
                        }
                    }
                    da2->bitblt.d += (2 * da2->bitblt.size_x);
                    if (da2->bitblt.count >= da2->bitblt.size_y)
                        da2->bitblt.exec = DA2_BLT_CDONE;
                }
                break;
            }
        case DA2_BLT_CFILLRECT:
            // da2_log("%x %x %x\n", da2->bitblt.destaddr, da2->bitblt.x, da2->bitblt.y);
            if (da2->bitblt.x >= da2->bitblt.size_x - 1) {
                da2_DrawColorWithBitmask(da2->bitblt.destaddr, da2->bitblt.fcolor, da2->bitblt.maskr, da2);
                if (da2->bitblt.y >= da2->bitblt.size_y - 1) {
                    da2->bitblt.exec = DA2_BLT_CDONE;
                }
                da2->bitblt.x = 0;
                da2->bitblt.y++;
                da2->bitblt.destaddr += da2->bitblt.destpitch + 2;
            } else if (da2->bitblt.x == 0) {
                da2_DrawColorWithBitmask(da2->bitblt.destaddr, da2->bitblt.fcolor, da2->bitblt.maskl, da2);
                da2->bitblt.x++;
            } else {
                da2_DrawColorWithBitmask(da2->bitblt.destaddr, da2->bitblt.fcolor, 0xffff, da2);
                da2->bitblt.x++;
            }
            da2->bitblt.destaddr += 2;
            break;
        case DA2_BLT_CFILLTILE:
            {
                int32_t tileaddr = da2->bitblt.srcaddr + (da2->bitblt.y % da2->bitblt.tile_w) * 2;
                if (da2->bitblt.x >= da2->bitblt.size_x - 1) {
                    da2_CopyPlaneDataWithBitmask(tileaddr, da2->bitblt.destaddr, da2->bitblt.maskr, da2);
                    if (da2->bitblt.y >= da2->bitblt.size_y - 1) {
                        da2->bitblt.exec = DA2_BLT_CDONE;
                    }
                    da2->bitblt.x = 0;
                    da2->bitblt.y++;
                    da2->bitblt.destaddr += da2->bitblt.destpitch + 2;
                } else if (da2->bitblt.x == 0) {
                    da2_CopyPlaneDataWithBitmask(tileaddr, da2->bitblt.destaddr, da2->bitblt.maskl, da2);
                    da2->bitblt.x++;
                } else {
                    da2_CopyPlaneDataWithBitmask(tileaddr, da2->bitblt.destaddr, 0xffff, da2);
                    da2->bitblt.x++;
                }
                da2->bitblt.destaddr += 2;
                break;
            }
        case DA2_BLT_CCOPYF:
            if (da2->bitblt.x >= da2->bitblt.size_x - 1) {
                da2_CopyPlaneDataWithBitmask(da2->bitblt.srcaddr, da2->bitblt.destaddr, da2->bitblt.maskr, da2);
                if (da2->bitblt.y >= da2->bitblt.size_y - 1) {
                    da2->bitblt.exec = DA2_BLT_CDONE;
                }
                da2->bitblt.x = 0;
                da2->bitblt.y++;
                da2->bitblt.destaddr += da2->bitblt.destpitch + 2;
                da2->bitblt.srcaddr += da2->bitblt.srcpitch + 2;
            } else if (da2->bitblt.x == 0) {
                da2_CopyPlaneDataWithBitmask(da2->bitblt.srcaddr, da2->bitblt.destaddr, da2->bitblt.maskl, da2);
                da2->bitblt.x++;
            } else {
                da2_CopyPlaneDataWithBitmask(da2->bitblt.srcaddr, da2->bitblt.destaddr, 0xffff, da2);
                da2->bitblt.x++;
            }
            da2->bitblt.destaddr += 2;
            da2->bitblt.srcaddr += 2;
            break;
        case DA2_BLT_CCOPYR:
            if (da2->bitblt.x >= da2->bitblt.size_x - 1) {
                da2_CopyPlaneDataWithBitmask(da2->bitblt.srcaddr, da2->bitblt.destaddr, da2->bitblt.maskr, da2);
                if (da2->bitblt.y >= da2->bitblt.size_y - 1) {
                    da2->bitblt.exec = DA2_BLT_CDONE;
                }
                da2->bitblt.x = 0;
                da2->bitblt.y++;
                da2->bitblt.destaddr -= da2->bitblt.destpitch;
                da2->bitblt.srcaddr -= da2->bitblt.srcpitch;
                da2->bitblt.destaddr -= 2;
                da2->bitblt.srcaddr -= 2;
            } else if (da2->bitblt.x == 0) {
                da2_CopyPlaneDataWithBitmask(da2->bitblt.srcaddr, da2->bitblt.destaddr, da2->bitblt.maskl, da2);
                da2->bitblt.x++;
            } else {
                da2_CopyPlaneDataWithBitmask(da2->bitblt.srcaddr, da2->bitblt.destaddr, 0xffff, da2);
                da2->bitblt.x++;
            }
            da2->bitblt.destaddr -= 2;
            da2->bitblt.srcaddr -= 2;
            break;
        case DA2_BLT_CPUTCHAR: /* used in OS/2 J1.3 wo ROM patch. TODO: still not work */
            // pclog("scr %x dest %x x %x y %x\n", da2->bitblt.srcaddr, da2->bitblt.destaddr, da2->bitblt.x, da2->bitblt.y);
            if (da2->bitblt.y >= da2->bitblt.size_y) {
                da2->bitblt.exec = DA2_BLT_CDONE;
            } else {
                da2_PutcharWithBitmask(da2->bitblt.srcaddr, da2->bitblt.size_x, da2->bitblt.fcolor, da2->bitblt.y, da2->bitblt.destaddr, da2->bitblt.maskl, da2->bitblt.maskr, da2);
            }
            da2->bitblt.y++;
            da2->bitblt.destaddr += da2->bitblt.size_x * 2 + da2->bitblt.destpitch + 2;
            break;
        case DA2_BLT_CDONE:
            // if (!(da2->bitblt.reg[0x20] & 0x20)) {
            //     /* initialize regs and set magic value for debug dump */
            //     for (int i = 0; i < DA2_BLT_REGSIZE; i++) {
            //         if (da2->bitblt.reg[i] != DA2_DEBUG_BLT_NEVERUSED)
            //             da2->bitblt.reg[i] = DA2_DEBUG_BLT_USEDRESET;
            //     }
            // } else /* without init regs */
            // if (da2->bitblt.indata)
            //     da2->bitblt.exec = DA2_BLT_CLOAD;
            // else
            da2->bitblt.exec = DA2_BLT_CIDLE;
            break;
    }
}
static void
da2_bitblt_dopayload(void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    timer_set_delay_u64(&da2->bitblt.timer, da2->bitblt.timerspeed);
    /* do async operation but it causes the scrolling text glitch in OS/2 J1.3 Command Prompt (TODO) */
    if (da2->bitblt.exec != DA2_BLT_CIDLE) {
        while (da2->bitblt.exec != DA2_BLT_CIDLE) /* this disables async operation */
            da2_bitblt_exec(da2);
    } else if ((da2->bitblt.reg[0x20] & 0x01) && (da2->bitblt.exec == DA2_BLT_CIDLE)) {
        da2->bitblt.exec = DA2_BLT_CLOAD;
        da2_bitblt_exec(da2);
    } else {
        // timer_disable(&da2->bitblt.timer);
    }
}
static void
da2_bitblt_addpayload(uint8_t val, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    da2->bitblt.indata = 1;
    if (da2->bitblt.payload_addr >= DA2_BLT_MEMSIZE)
        da2_log("da2_mmio_write payload overflow! addr %x, val %x\n", da2->bitblt.payload_addr, val);
    else {
        if (da2->bitblt.payload_addr == 0) {
            da2->bitblt.payload[da2->bitblt.payload_addr] = val;
            da2->bitblt.payload_addr++;
            switch (val) {
                case 0x88:
                case 0x89:
                case 0x95:
                    da2->bitblt.payload_opsize = 3;
                    break;
                case 0x91:
                    da2->bitblt.payload_opsize = 5;
                    break;
                case 0x99:
                    da2->bitblt.payload_opsize = 7;
                    break;
                case 0xa1:
                    da2->bitblt.payload_opsize = 9;
                    break;
                default:
                    da2_log("addpayload: Unknown PreOP! %x\n", val);
                    da2->bitblt.payload_addr = 0; /* ignore input */
                    break;
            }
        } else if (da2->bitblt.payload_addr < da2->bitblt.payload_opsize) {
            da2->bitblt.payload[da2->bitblt.payload_addr] = val;
            da2->bitblt.payload_addr++;
        } else if (da2->bitblt.payload_addr == da2->bitblt.payload_opsize) {
            da2->bitblt.payload[da2->bitblt.payload_addr] = val;
            da2->bitblt.payload_opsize = 0; /* reset */
            da2_bitblt_parse(da2);
        }
    }
}

static void
da2_out(uint16_t addr, uint16_t val, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    int    oldval;
    /*
    3E0 3E1     Sequencer Registers (undoc)
    3E2 3E3     Font Registers (undoc)
    3E4 3E5     CRT Control Registers (undoc)
    3E8 3E9     Attribute Controller Registers (undoc)
    3EA 3EB 3EC Graphics Contoller Registers
    */
    switch (addr) {
        case 0x3c6: /* PEL Mask */
            da2->dac_mask = val;
            break;
        case 0x3C7: /* Read Address */
            da2->dac_read = val;
            da2->dac_pos  = 0;
            break;
        case 0x3C8: /* Write Address */
            da2->dac_write = val;
            da2->dac_read  = val - 1;
            da2->dac_pos   = 0;
            break;
        case 0x3C9: /* Data */
            // da2_iolog("DA2 Out addr %03X idx %d:%d val %02X %04X:%04X esdi %04X:%04X\n", addr, da2->dac_write, da2->dac_pos, val, cs >> 4, cpu_state.pc, ES, DI);
            da2->dac_status = 0;
            da2->fullchange = 3;
            switch (da2->dac_pos) {
                case 0:
                    da2->dac_r = val;
                    da2->dac_pos++;
                    break;
                case 1:
                    da2->dac_g = val;
                    da2->dac_pos++;
                    break;
                case 2:
                    da2->vgapal[da2->dac_write].r = da2->dac_r;
                    da2->vgapal[da2->dac_write].g = da2->dac_g;
                    da2->vgapal[da2->dac_write].b = val;
                    // if (da2->ramdac_type == RAMDAC_8BIT)
                    //     da2->pallook[da2->dac_write] = makecol32(da2->vgapal[da2->dac_write].r, da2->vgapal[da2->dac_write].g, da2->vgapal[da2->dac_write].b);
                    // else
                    da2->pallook[da2->dac_write] = makecol32((da2->vgapal[da2->dac_write].r & 0x3f) * 4, (da2->vgapal[da2->dac_write].g & 0x3f) * 4, (da2->vgapal[da2->dac_write].b & 0x3f) * 4);
                    da2->dac_pos                 = 0;
                    da2->dac_write               = (da2->dac_write + 1) & 255;
                    break;
            }
            break;
        case LS_INDEX:
            da2->ioctladdr = val;
            break;
        case LS_DATA:
            // da2_iolog("DA2 Out addr %03X idx %02X val %02X %04X:%04X\n", addr, da2->ioctladdr, val, cs >> 4, cpu_state.pc);
            if (da2->ioctladdr > 0xf)
                return;
            if (da2->ioctl[da2->ioctladdr & 15] != val)
                da2_iolog("ioctl changed %x: %x -> %x  %04X:%04X\n", da2->ioctladdr & 15, da2->ioctl[da2->ioctladdr & 15], val, cs >> 4, cpu_state.pc);
            oldval                     = da2->ioctl[da2->ioctladdr];
            da2->ioctl[da2->ioctladdr] = val;
            if (oldval != val) {
                if (da2->ioctladdr == LS_RESET && val & 0x01) /* Reset register */
                    da2_reset_ioctl(da2);
                else if (da2->ioctladdr == LS_MODE && ((oldval ^ val) & 0x03)) { /* Mode register */
                    da2->fullchange = 3;
                    da2_recalctimings(da2);
                    da2_updatevidselector(da2);
                }
            }
            break;
        case LF_INDEX:
            da2->fctladdr = val;
            break;
        case LF_DATA:
            // da2_iolog("DA2 Out addr %03X idx %02X val %02X %04X:%04X\n", addr, da2->fctladdr, val, cs >> 4, cpu_state.pc);
            if (da2->fctladdr > 0x1f)
                return;
            if (da2->fctl[da2->fctladdr & 0x1f] != val)
                da2_iolog("fctl changed %x: %x -> %x  %04X:%04X\n", da2->fctladdr & 0x1f, da2->fctl[da2->fctladdr & 0x1f], val, cs >> 4, cpu_state.pc);
            oldval                   = da2->fctl[da2->fctladdr];
            da2->fctl[da2->fctladdr] = val;
            if (da2->fctladdr == 0 && oldval != val) {
                da2_iolog("DA2 Out FCTL addr %03X idx %02X val %02X %04X:%04X\n", addr, da2->fctladdr, val, cs >> 4, cpu_state.pc);
            } else if ((da2->fctladdr == LF_PD_RESET) && ((val & 0x7) == 7)) {
                /* Reset Bitblt busy */
                da2->bitblt.indata = 0;
                da2->bitblt.exec = DA2_BLT_CIDLE;
            }
            break;
        case LC_INDEX:
            da2->crtcaddr = val;
            break;
        case LC_DATA:
            if (da2->crtcaddr > 0x1f)
                return;
            if (!(da2->crtcaddr == LC_CURSOR_LOC_HIGH || da2->crtcaddr == LC_CURSOR_LOC_LOWJ))
                da2_iolog("DA2 Out addr %03X idx %02X val %02X %04X:%04X\n", addr, da2->crtcaddr, val, cs >> 4, cpu_state.pc);
            if (!(da2->crtc[da2->crtcaddr] ^ val))
                return;
            switch (da2->crtcaddr) {
                case LC_CRTC_OVERFLOW:
                    // return;
                    break;
                case LC_MAXIMUM_SCAN_LINE:
                    if (!(da2->ioctl[LS_MODE] & 0x01)) /* 16 or 256 color graphics mode */
                        val = 0;
                    break;
                case LC_VERTICAL_TOTALJ:      /* Vertical Total */
                case LC_VERTICAL_SYNC_START:  /* Vertical Retrace Start Register */
                case LC_V_DISPLAY_ENABLE_END: /* Vertical Display End Register */
                case LC_START_VERTICAL_BLANK: /* Start Vertical Blank Register */
#ifdef ENABLE_DA2_DEBUGFULLSCREEN
                    val = 0x400; /* for debugging bitblt in Win 3.x */
#endif
                    break;
            }
            da2->crtc[da2->crtcaddr] = val;
            switch (da2->crtcaddr) {
                // case LC_START_ADDRESS_HIGH:
                // case LC_START_ADDRESS_LOW:
                /* The DOS J4.0 MODE 4 command and OS/2 driver write 0xFF00.
                   OS/2 DOS MODE 1 setup reads this to set the base line, but it causes the screen glitch. */
                    // outb(0x680, da2->crtc[LC_START_ADDRESS_LOW]);
                    // outb(0x680, da2->crtc[LC_START_ADDRESS_HIGH]);
                // break;
                case LC_VIEWPORT_SELECT:
                    /* backup some current crtc regs */
                    for (int i = LC_START_ADDRESS_HIGH; i <= LC_START_ADDRESS_LOW; i++) {
                        da2->crtc_vpreg[(da2->crtc_vpsel * 0x20) + i] = da2->crtc[i];
                    }
                    da2->crtc_vpsel = (val >> 6) & 3;
                    /* restore crtc regs */
                    for (int i = LC_START_ADDRESS_HIGH; i <= LC_START_ADDRESS_LOW; i++) {
                        da2->crtc[i] = da2->crtc_vpreg[(da2->crtc_vpsel * 0x20) + i];
                    }
                    break;
                default:
                    break;
            }
            switch (da2->crtcaddr) {
                case LC_H_DISPLAY_ENABLE_END:
                case LC_VERTICAL_TOTALJ:
                case LC_MAXIMUM_SCAN_LINE:
                case LC_START_ADDRESS_HIGH:
                case LC_START_ADDRESS_LOW:
                case LC_VERTICAL_SYNC_START:
                case LC_V_DISPLAY_ENABLE_END:
                case LC_START_VERTICAL_BLANK:
                case LC_LINE_COMPAREJ:
                case LC_START_H_DISPLAY_ENAB:
                case LC_START_V_DISPLAY_ENAB:
                case LC_VIEWPORT_SELECT:
                case LC_VIEWPORT_PRIORITY:
                    da2->fullchange = 3;
                    da2_recalctimings(da2);
                    break;
                default:
                    break;
            }
            break;
        case LV_PORT:
            // da2_iolog("DA2 Out addr %03X val %02X ff %d %04X:%04X\n", addr, val, da2->attrff,cs >> 4, cpu_state.pc);
            if (!da2->attrff) {
                da2->attraddr = val & 0x3f;
                if ((val & 0x20) != (da2->attr_palette_enable & 0x20)) {
                    da2->fullchange          = 3;
                    da2->attr_palette_enable = val & 0x20;
                    da2_recalctimings(da2);
                }
                // da2_iolog("set attraddr: %X\n", da2->attraddr);
            } else {
                if ((da2->attraddr == LV_PANNING) && (da2->attrc[LV_PANNING] != val))
                    da2->fullchange = 3;
                if (da2->attrc[da2->attraddr & 0x3f] != val)
                    da2_iolog("attr changed %x: %x -> %x\n", da2->attraddr & 0x3f, da2->attrc[da2->attraddr & 0x3f], val);
                da2->attrc[da2->attraddr & 0x3f] = val;
                // da2_iolog("set attrc %x: %x\n", da2->attraddr & 31, val);
                if (da2->attraddr < 16)
                    da2->fullchange = 3;
                if (da2->attraddr == LV_MODE_CONTROL || da2->attraddr < 0x10) {
                    for (uint8_t c = 0; c < 16; c++) {
                        // if (da2->attrc[LV_MODE_CONTROL] & 0x80) da2->egapal[c] = (da2->attrc[c] & 0xf) | ((da2->attrc[0x14] & 0xf) << 4);
                        // else                             da2->egapal[c] = (da2->attrc[c] & 0x3f) | ((da2->attrc[0x14] & 0xc) << 4);
                        if (da2->attrc[LV_MODE_CONTROL] & 0x80)
                            da2->egapal[c] = da2->attrc[c] & 0xf;
                        else
                            da2->egapal[c] = da2->attrc[c] & 0x3f;
                    }
                }
                switch (da2->attraddr) {
                    case LV_COLOR_PLANE_ENAB:
                        if ((val & 0xff) != da2->plane_mask)
                            da2->fullchange = 3;
                        da2->plane_mask = val & 0xff;
                        break;
                    case LV_CURSOR_CONTROL:
                        switch (val & 0x18) {
                            case 0x08: /* fast blink */
                                da2->blinkconf = 0x10;
                                break;
                            case 0x18: /* slow blink */
                                da2->blinkconf = 0x20;
                                break;
                            default: /* no blink */
                                da2->blinkconf = 0xff;
                                break;
                        }
                        break;
                    case LV_MODE_CONTROL:
                    case LV_ATTRIBUTE_CNTL:
                    case LV_COMPATIBILITY:
                        da2_recalctimings(da2);
                        break;
                    default:
                        break;
                }
            }
            da2->attrff ^= 1;
            break;
        case 0x3E9:
            /* VZ Editor's CURSOR.COM writes data via this port */
            da2->attrc[da2->attraddr & 0x3f] = val;
            break;
        case LG_INDEX:
            da2_iolog("DA2 Out addr %03X val %02X\n", addr, val);
            da2->gdcaddr = val;
            break;
        case LG_DATA:
        // if(da2->gdcaddr != 8 && da2->gdcaddr != 9) da2_iolog("DA2 GCOut idx %X val %02X %04X:%04X esdi %04X:%04X\n", da2->gdcaddr, val, cs >> 4, cpu_state.pc, ES, DI);
            da2_iolog("DA2 Out addr %03X idx %02X val %02X\n", addr, da2->gdcaddr, val);
            da2->gdcreg[da2->gdcaddr & 0x0f] = val & 0xff;
            switch (da2->gdcaddr & 0x1f) {
                case LG_READ_MAP_SELECT:
                    da2->readplane = val & 0x7;
                    break;
                case LG_MODE:
                    da2->writemode = val & 3;
                     /* Resettting masks here gliches chart drawing in IBM Multitool Chart K3.1 */
                    // da2->gdcreg[LG_BIT_MASK_LOW] = 0xff;
                    // da2->gdcreg[LG_BIT_MASK_HIGH] = 0xff;
                    // da2->planemask = 0xff;
                    // da2->gdcreg[LG_MAP_MASKJ] = 0xff;
                    break;
                case LG_MAP_MASKJ:
                    da2->planemask = val & 0xff;
                    break;
                case LG_COMMAND:
                    break;
                case LG_SET_RESET_2:
                    da2_iolog("!!!DA2 GC Out addr %03X idx 10 val %02X\n", addr, val);
                    return;
            }
            break;
        // case 0x3ed: /* used by Windows 3.1 display driver */
        //     da2->gdcreg[5] = val & 0xff;
        //     break;
        default:
            da2_iolog("DA2? Out addr %03X val %02X\n", addr, val);
            break;
    }
}

static uint16_t
da2_in(uint16_t addr, void *priv)
{
    da2_t   *da2  = (da2_t *) priv;
    uint16_t temp = 0xff;

    switch (addr) {
        case 0x3c3:
            temp = 0;
            break;
        case 0x3c6:
            temp = da2->dac_mask;
            break;
        case 0x3c7:
            temp = da2->dac_status;
            break;
        case 0x3c8:
            temp = da2->dac_write;
            break;
        case 0x3c9:
            da2->dac_status = 3;
            switch (da2->dac_pos) {
                case 0:
                    da2->dac_pos++;
                    temp = da2->vgapal[da2->dac_read].r & 0x3f;
                    break;
                case 1:
                    da2->dac_pos++;
                    temp = da2->vgapal[da2->dac_read].g & 0x3f;
                    break;
                case 2:
                    da2->dac_pos  = 0;
                    da2->dac_read = (da2->dac_read + 1) & 255;
                    temp          = da2->vgapal[(da2->dac_read - 1) & 255].b & 0x3f;
                    break;
            }
            break;
        case LS_INDEX:
            temp = da2->ioctladdr;
            break;
        case LS_DATA:
            // da2->ioctl[3] = 0x80; /* 3E1h:3 bit 7 color monitor, bit 3 busy(GC) bit 0 busy (IO?) */
            if (da2->ioctladdr > 0xf)
                return DA2_INVALIDACCESS16;
            temp = da2->ioctl[da2->ioctladdr];
            if (da2->ioctladdr == LS_STATUS) {             /* Status register */
                if (da2->attrc[LV_COMPATIBILITY] & 0x08) { /* for detecting monitor type and cable wiring */
                    if (da2->monitorid == DA2_DCONFIG_MONTYPE_MONO) {
                        /* grayscale monitor */
                        if ((da2->vgapal[0].r >= 10) || (da2->vgapal[0].g >= 40) || (da2->vgapal[0].b >= 10))
                            temp &= 0x7F; /* Inactive when the RGB output voltage is high (or the cable is not connected to a color monitor). */
                        else
                            temp |= 0x80; /* Active when the RGB output voltage is low and the cable is connected to a color monitor.
                                             If the cable or the monitor is wrong, it becomes inactive. */
                    } else {
                        /* color monitor */
                        if ((da2->vgapal[0].r + da2->vgapal[0].g + da2->vgapal[0].b) >= 80)
                            temp &= 0x7F;
                        else
                            temp |= 0x80;
                    }
                } else {
                    temp |= 0x80;
                }
                temp &= 0xf6;           /* clear busy bit */
                // if (da2->bitblt.indata) /* for OS/2 J1.3 command prompt scrolling  */
                //     da2_bitblt_dopayload(da2);
                if (da2->bitblt.exec != DA2_BLT_CIDLE) {
                    temp |= 0x09; /* wait (bit 3 + bit 0) ? need verify */
                }
                // if (da2->bitblt.indata) temp |= 0x08;
#ifdef ENABLE_DA2_DEBUGMONWAIT
                da2_iolog("DA2 In %04X(%02X) %04X %04X:%04X\n", addr, da2->ioctladdr, temp, cs >> 4, cpu_state.pc);
#endif
            }
            break;
        case LF_INDEX:
            temp = da2->fctladdr;
            break;
        case LF_DATA:
            if (da2->fctladdr > 0x1f)
                return DA2_INVALIDACCESS16;
            temp = da2->fctl[da2->fctladdr];
            break;
        case LC_INDEX:
            temp = da2->crtcaddr;
            break;
        case LC_DATA:
            if (da2->crtcaddr > 0x1f)
                return DA2_INVALIDACCESS16;
            temp = da2->crtc[da2->crtcaddr];
            break;
        case LV_PORT:
            temp = da2->attraddr | da2->attr_palette_enable;
            break;
        case 0x3E9:
            if (da2->attraddr == LV_RAS_STATUS_VIDEO) /* this maybe equivalent to 3ba / 3da ISR1 */
            {
                if (da2->cgastat & 0x01)
                    da2->cgastat &= ~0x30;
                else
                    da2->cgastat ^= 0x30; /* toggle */
                if (da2->cgastat & 0x08)
                    da2->cgastat &= ~0x08;
                else
                    da2->cgastat ^= 0x08; /* toggle */
                temp = da2->cgastat;
            } else
                temp = da2->attrc[da2->attraddr];
            // da2_iolog("DA2 In %04X(%02X) %04X %04X:%04X\n", addr, da2->attraddr, temp, cs >> 4, cpu_state.pc);
            da2->attrff = 0; /* reset flipflop (VGA does not reset flipflop) */
            break;
        case LG_INDEX:
            temp = da2->gdcaddr;
            break;
        case LG_DATA:
            temp = da2->gdcreg[da2->gdcaddr & 0x1f];
            // da2_iolog("DA2 In %04X(%02X) %04X %04X:%04X\n", addr, da2->gdcaddr, temp, cs >> 4, cpu_state.pc);
            break;
    }
    // da2_iolog("DA2 In %04X %04X %04X:%04X\n", addr, temp, cs >> 4, cpu_state.pc);
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
da2_outb(uint16_t addr, uint8_t val, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    da2_iolog("DA2 Outb addr %03X val %02X %04X:%04X es:di=%x:%x ds:si=%x:%x\n", addr, val, cs >> 4, cpu_state.pc, ES, DI, DS, SI);
    da2->inflipflop = 0;
    switch (addr) {
        case LS_DATA:
        case LF_DATA:
        case LC_DATA:
        case LG_DATA:
            if (da2->outflipflop) {
                /* out b(idx), out b(data), out b(data) */
                da2->iolatch |= (uint16_t) val << 8;
                da2->outflipflop = 0;
            } else { //
                da2->iolatch     = val;
                da2->outflipflop = 1;
            }
            break;
        case LS_INDEX:
        case LF_INDEX:
        case LC_INDEX:
        case LG_INDEX:
        default:
            da2->iolatch     = val;
            da2->outflipflop = 0;
            break;
    }
    da2_out(addr, da2->iolatch, da2);
}
void
da2_outw(uint16_t addr, uint16_t val, void *priv)
{
    da2_iolog("DA2 Outw addr %03X val %04X\n", addr, val);
    da2_t *da2      = (da2_t *) priv;
    da2->inflipflop = 0;
    switch (addr) {
        case LS_INDEX:
        case LF_INDEX:
        case LC_INDEX:
        case LG_INDEX:
            da2_out(addr, val & 0xff, da2);
            da2->iolatch = val >> 8;
            da2_out(addr + 1, da2->iolatch, da2);
            da2->outflipflop = 1;
            break;
        case LV_PORT:
            da2->attrff = 0;
            da2_out(addr, val & 0xff, da2);
            da2_out(addr, val >> 8, da2);
            da2->outflipflop = 0;
            break;
        case 0x3EC:
            // da2_iolog("DA2 Outw addr %03X val %04X %04X:%04X\n", addr, val, cs >> 4, cpu_state.pc);
            da2_out(LG_DATA, val >> 8, da2);
            /* reset masks for compatibility with Win 3.1 solitaire */
            if (da2->gdcaddr == LG_MODE) {
                da2->gdcreg[LG_BIT_MASK_LOW]  = 0xff;
                da2->gdcreg[LG_BIT_MASK_HIGH] = 0xff;
                da2->planemask                = 0xff;
                da2->gdcreg[LG_MAP_MASKJ]     = 0xff;
            }
            break;
        case 0x3ED:
            da2->gdcaddr = LG_MODE;
            da2_out(LG_DATA, val, da2);
            break;
        case LS_DATA:
        case LF_DATA:
        case LC_DATA:
        case LG_DATA:
        default:
            da2_out(addr, val, da2);
            da2->outflipflop = 0;
            break;
        case AC_REG:
            /* no register is revealed */
            da2_iolog("DA2 Outw addr %03X val %04X %04X:%04X\n", addr, val, cs >> 4, cpu_state.pc);
            da2->reg3ee[val & 0x0f] = val >> 8;
            break;
    }
}
static uint8_t
da2_inb(uint16_t addr, void *priv)
{
    uint8_t temp;
    da2_t  *da2      = (da2_t *) priv;
    da2->outflipflop = 0;
    switch (addr) {
        case LC_DATA:
            if (da2->inflipflop) {
                /* out b(idx), in b(low data), in b(high data) */
                temp            = da2->iolatch >> 8;
                da2->inflipflop = 0;
            } else { //
                da2->iolatch    = da2_in(addr, da2);
                temp            = da2->iolatch & 0xff;
                da2->inflipflop = 1;
            }
            break;
        case LS_INDEX:
        case LF_INDEX:
        case LC_INDEX:
        case LG_INDEX:
        case LS_DATA:
        case LF_DATA:
        case LG_DATA:
        default:
            temp            = da2_in(addr, da2) & 0xff;
            da2->inflipflop = 0;
            break;
    }
    // da2_iolog("DA2 Inb %04X %02X %04X:%04X\n", addr, temp, cs >> 4, cpu_state.pc);
    return temp;
}
static uint16_t
da2_inw(uint16_t addr, void *priv)
{
    uint16_t temp;
    da2_t *da2       = (da2_t *) priv;
    da2->inflipflop  = 0;
    da2->outflipflop = 0;
    temp = da2_in(addr, da2);
    da2_iolog("DA2 Inw addr %03X val %04X\n", addr, temp);
    return temp;
}
/* IO 03DAh : Input Status Register 2 for DOSSHELL used by DOS J4.0 */
static uint8_t
da2_in_ISR(uint16_t addr, void *priv)
{
    da2_t  *da2  = (da2_t *) priv;
    uint8_t temp = 0;
    if (addr == 0x3da) {
        if (da2->cgastat & 0x01)
            da2->cgastat &= ~0x30;
        else
            da2->cgastat ^= 0x30;
        temp = da2->cgastat;
    }
    // da2_iolog("DA2D In %04X %04X %04X:%04X\n", addr, temp, cs >> 4, cpu_state.pc);
    return temp;
}

static void
da2_out_ISR(uint16_t addr, uint8_t val, void *priv)
{
    // da2_t* da2 = (da2_t*) priv;
    da2_iolog("DA2D Out %04X %04X %04X:%04X\n", addr, val, cs >> 4, cpu_state.pc);
}

/*
The IBM 5550 character mode addresses video memory between E0000h and E0FFFh.
 [Character drawing]
 SBCS:
   1 2   ...       13
 1  |   H.Grid
    |----------------
 2  |    Space
 3 V|
   .|
   G|  Font Pattern
   r| (12x24 pixels)
   i|
   d|
26  |________________
27       Space
     ----------------
28     Underscore    ]
     ---------------- >Cursor Position
29                   ]

 DBCS:
   1 2   ...    13 1 2  ...  12 13
 1  |   H.Grid    |   H.Grid   |
   -|--------------------------|-
 2  |    Space    |    Space   |
   -|--------------------------|-
 3 V|             |            |S
   .|             |            |p
   G|        Font Pattern      |a
   r|       (24x24 pixels)     |c
   i|             |            |e
   d|             |            |
26  |_____________|____________|
27  |    Space    |    Space
   ------------------------------
28  |  Underscore |  Underscore  ]
   ------------------------------ >Cursor Position
29  |             |              ]

 [Attributes]
 Video mode 08h:
  7      6      5      4      3      2      1      0
  Blink |Under |HGrid |VGrid |Bright|Revers|FntSet|DBCS/SBCS|

 Video mode 0Eh:
  -Blue |-Green|HGrid |VGrid |-Red  |Revers|FntSet|DBCS/SBCS|

 Bit 1 switches the font bank to the Extended SBCS. DOS K3.x loads APL characters from $SYSEX24.FNT into it.
 DOS Bunsho Program transfers 1/2 and 1/4 fonts fron the font ROM to the Extended SBCS.
 This bit is not used for DBCS, but some apps set it as that column is right half of DBCS.

[Font ROM Map (DA2, Japanese)]
The Font ROM can be accessed via 128 KB memory window located at A0000-BFFFFh.

 Bank 0
 4800-  *
 Bank 1, 2, 3
   *  -  *
 Bank 4
   *  -0DB6Fh ( 4800-8DB6Fh) : JIS X 0208 DBCS (24 x 24) (IBMJ code: 100-1F7Dh)
 10000-16D1Fh (90000-96D1Fh) : IBM Extended Characters (IBMJ code: 2ADC-2C5Fh)
 18000-1BFCFh (98000-9BFCFh) : JIS X 0201 SBCS (13 x 30)
 1C000-1FFFFh (9C000-9FFFFh) : Codepage 437 characters (13 x 30)
 Bank 5
 00000-0C68Fh (A0000-AC68Fh) : Gaiji used by DOS Bunsho
 10000-13FFFh (B0000-B3FFFh) : Extended SBCS (13 x 30)
 14000-1477Fh (B4000-B477Fh) : Half-width box drawing characters used by DOS Bunsho
 16000-17FFFh (B6000-B7FFFh) : Codepage 850 characters (13 x 30)
 18000-1A3FFh (B8000-BA3FFh) : CAD control icons and box drawing characters (32 x 32)

 Some models have the signature 80h, 01h placed at Bank 0:1AFFEh. It disables Bitblt text drawing in OS/2 J1.3.

[Font ROM Map (DA3, Traditional Chinese)]
 Bank 0 - 11 : Valid Font ROM data
 Bank 12 : Alias of bank 11 (At least, DOS T5.0 uses this on purpose to obtain the SBCS font.)
 Bank 13 : Filled by 0xFFh

[Gaiji RAM Map (DA2)]
 Bank 0 00000-1FFFFh placed between A0000h-BFFFFh
 00000-1F7FFh(A0000-BF7FFh): Gaiji Non-resident (Kuten 103-114 ku,IBM: 2674-2ADBh) 1008 chs 128 bytes
 1F800-1FFFFh(BF800-BFFFFh): Gaiji Resident (SJIS: F040-F04Fh, IBM: 2384-2393h) 16 chs

 Bank 1 20000-3FFFFh placed between A0000h-BFFFFh
 20000-33FFFh(A0000-B3FFFh): Gaiji Resident (SJIS: F050-F39Ch, IBM: 2394-2613h) 640 chs
 34000-37FFFh(B4000-B7FFFh): Basic SBCS(00-FFh, ATTR bit 1 = off)
 38000-3AFFFh(B8000-BAFFFh): Gaiji Resident (SJIS: F39D-F3FCh, IBM: 2614-2673h) 96 chs
 3C000-3FFFFh(BC000-BFFFFh): Extended SBCS(00-FFh, ATTR bit 1 = on)

[IBMJ code to Gaiji address conv tbl from DOS K3.3]
 A     B     C
 2ADC, 2C5F, +5524  --> 8000
 2614, 2673, +90EC  --> B700
 2384, 2613, +906C  --> B3F0
 0682, 1F7D, +0000

 8000 - 8183h 184h(388 characters) IBM Extended Characters
 B3F0 - B75Fh 370h(880 characters) User-defined Characters
*/

/* Get character line pattern from jfont rom or gaiji volatile memory */
static uint32_t
getfont_ps55dbcs(int32_t code, int32_t line, void *priv)
{
    da2_t   *da2   = (da2_t *) priv;
    uint32_t font  = 0;
    int32_t  fline = line - 2; /* Start line of drawing character (line >= 1 AND line < 24 + 1 ) */
    if (code >= 0x8000 && code <= 0x8183)
        code -= 0x6000; /* shift for IBM extended characters (I don't know how the real card works.) */
    if (code < DA2_FONTROM_SIZE / 72 && fline >= 0 && fline < 24) {
        font = da2->mmio.font[code * 72 + fline * 3];             /* 0000 0000 0000 0000 0000 0000 1111 1111 */
        font <<= 8;                                               /* 0000 0000 0000 0000 1111 1111 0000 0000 */
        font |= da2->mmio.font[code * 72 + fline * 3 + 1] & 0xf0; /* 0000 0000 0000 0000 1111 1111 2222 0000 */
        font <<= 3;                                               /* 0000 0000 0000 0111 1111 1222 2000 0000 */
        font |= da2->mmio.font[code * 72 + fline * 3 + 1] & 0x0f; /* 0000 0000 0000 0111 1111 1222 2000 2222 */
        font <<= 8;                                               /* 0000 0111 1111 1222 2000 2222 0000 0000 */
        font |= da2->mmio.font[code * 72 + fline * 3 + 2];        /* 0000 0111 1111 1222 2000 2222 3333 3333 */
        font <<= 4;                                               /* 0111 1111 1222 2000 2222 3333 3333 0000 */
        /* font >>= 1;//put blank at column 1 (and 26) */
    } else if (code >= 0xb000 && code <= 0xb75f) {
        /* convert code->address in gaiji memory */
        code -= 0xb000;
        code *= 0x80;
        // code += 0xf800;
        font = da2->mmio.ram[code + line * 4];
        font <<= 8;
        font |= da2->mmio.ram[code + line * 4 + 1];
        font <<= 8;
        font |= da2->mmio.ram[code + line * 4 + 2];
        font <<= 8;
        font |= da2->mmio.ram[code + line * 4 + 3];
    } else if (code > DA2_FONTROM_SIZE)
        font = 0xffffffff;
    else
        font = 0;
    return font;
}

/* Reverse the bit order of attribute code IRGB to BGRI(used in Mode 3 and Cursor Color) */
static int8_t
IRGBtoBGRI(uint8_t attr)
{
    attr = ((attr & 0x01) << 7) | ((attr & 0x02) << 5) | ((attr & 0x04) << 3) | ((attr & 0x08) << 1);
    return attr >>= 4;
}
/* Get the foreground color from the attribute byte */
static uint8_t
getPS55ForeColor(uint8_t attr, da2_t *da2)
{
    uint8_t foreground = ~attr & 0x08; /* 0000 1000 */
    foreground <<= 2;                  /* 0010 0000 */
    foreground |= ~attr & 0xc0;        /*  1110 0000 */
    foreground >>= 4;                  /* 0000 1110 */
    if (da2->attrc[LV_PAS_STATUS_CNTRL] & 0x40)
        foreground |= 0x01; /* bright color palette */
    return foreground;
}

static void
da2_render_blank(da2_t *da2)
{
    int x, xx;
    int cwidth;
    if (da2->ioctl[LS_MODE] & 0x01) cwidth = 13; /* in character mode */
    else cwidth = 16;

    if (da2->firstline_draw == 2000)
        da2->firstline_draw = da2->displine;
    da2->lastline_draw = da2->displine;

    for (x = 0; x < da2->hdisp; x++) {
        for (xx = 0; xx < cwidth; xx++)
            ((uint32_t *) buffer32->line[da2->displine])[(x * cwidth) + xx + 32] = 0;
    }
}
/* Display Adapter Mode 8, E Drawing */
static void
da2_render_text(da2_t *da2)
{
    if (da2->firstline_draw == 2000)
        da2->firstline_draw = da2->displine;
    da2->lastline_draw = da2->displine;

    if (da2->fullchange) {
        int       offset = (8 - da2->scrollcache) + 24;
        uint32_t *p      = &((uint32_t *) buffer32->line[da2->displine])[offset];
        int       x;
        int       drawcursor;
        uint8_t   chr, attr;
        int       fg, bg;
        uint32_t  chr_dbcs;
        int       chr_wide = 0;
        int       colormode = ((da2->attrc[LV_PAS_STATUS_CNTRL] & 0x80) == 0x80);
        // da2_log("\nda2ma: %x, da2sc: %x\n", da2->memaddr, da2->scanline);
        for (x = 0; x < da2->hdisp; x += 13) {
            chr  = da2->cram[(da2->memaddr) & DA2_MASK_CRAM];
            attr = da2->cram[(da2->memaddr + 1) & DA2_MASK_CRAM];
            // if(chr!=0x20) da2_log("chr: %x, attr: %x    ", chr, attr);
            if (colormode) /* IO 3E8h, Index 1Dh */
            {                                           /* --Parse attribute byte in color mode-- */
                bg = 0;                                 /* bg color is always black (the only way to change background color is programming PAL) */
                fg = getPS55ForeColor(attr, da2);
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
                fg = ((da2->blink & 0x20) || (!(attr & 0x80))) ? fg : bg;
                // if(chr!=0x20) da2_log("chr: %x, %x, %x, %x, %x    ", chr, attr, fg, da2->egapal[fg], da2->pallook[da2->egapal[fg]]);
            }
            /* Draw character */
            for (uint32_t n = 0; n < 13; n++)
                p[n] = da2->pallook[da2->egapal[bg]]; /* draw blank */
            /*  SBCS or DBCS left half */
            if (chr_wide == 0) {
                if (attr & 0x01)
                    chr_wide = 1;
                // chr_wide = 0;
                /* Stay drawing If the char code is DBCS and not at last column. */
                if (chr_wide) {
                    /* Get high DBCS code from the next video address */
                    chr_dbcs = da2->cram[(da2->memaddr + 2) & DA2_MASK_CRAM];
                    chr_dbcs <<= 8;
                    chr_dbcs |= chr;
                    /* Get the font pattern */
                    uint32_t font = getfont_ps55dbcs(chr_dbcs, da2->scanline, da2);
                    /* Draw 13 dots */
                    for (uint32_t n = 0; n < 13; n++) {
                        p[n] = da2->pallook[da2->egapal[(font & 0x80000000) ? fg : bg]];
                        font <<= 1;
                    }
                } else {
                    /* the char code is SBCS (ANK) */
                    uint32_t fontbase;
                    if (attr & 0x02) /* second map of SBCS font */
                        fontbase = DA2_GAIJIRAM_SBEX;
                    else
                        fontbase = DA2_GAIJIRAM_SBCS;
                    uint16_t font = da2->mmio.ram[fontbase + chr * 0x40 + da2->scanline * 2]; /* w13xh29 font */
                    font <<= 8;
                    font |= da2->mmio.ram[fontbase + chr * 0x40 + da2->scanline * 2 + 1]; /* w13xh29 font */
                    // if(chr!=0x20) da2_log("memaddr: %x, scanline: %x, chr: %x, font: %x    ", da2->memaddr, da2->scanline, chr, font);
                    /* Draw 13 dots */
                    for (uint32_t n = 0; n < 13; n++) {
                        p[n] = da2->pallook[da2->egapal[(font & 0x8000) ? fg : bg]];
                        font <<= 1;
                    }
                }
            }
            /* right half of DBCS */
            else {
                uint32_t font = getfont_ps55dbcs(chr_dbcs, da2->scanline, da2);
                /* Draw 13 dots */
                for (uint32_t n = 0; n < 13; n++) {
                    p[n] = da2->pallook[da2->egapal[(font & 0x8000) ? fg : bg]];
                    font <<= 1;
                }
                chr_wide = 0;
            }
            /* Line 28 (Underscore) Note: Draw this first to display blink + vertical + underline correctly. */
            if (da2->scanline == da2->crtc[LC_UNDERLINE_LOCATION] && attr & 0x40 && !colormode) { /* Underscore only in monochrome mode */
                for (uint32_t n = 0; n < 13; n++)
                    p[n] = da2->pallook[da2->egapal[fg]]; /* under line (white) */
            }
            /* Column 1 (Vertical Line) */
            if (attr & 0x10) {
                p[0] = da2->pallook[da2->egapal[(colormode) ? IRGBtoBGRI(da2->attrc[LV_GRID_COLOR_0]) : 2]]; /* vertical line (white) */
            }
            if (da2->scanline == 0 && attr & 0x20 && ~da2->attrc[LV_PAS_STATUS_CNTRL]) { /* HGrid */
                for (uint32_t n = 0; n < 13; n++)
                    p[n] = da2->pallook[da2->egapal[(colormode) ? IRGBtoBGRI(da2->attrc[LV_GRID_COLOR_0]) : 2]]; /* horizontal line (white) */
            }
            /* Drawing text cursor */
            drawcursor = ((da2->memaddr == da2->cursoraddr) && da2->cursorvisible && da2->cursoron);
            if (drawcursor) {
                int cursorwidth = (da2->crtc[LC_COMPATIBILITY] & 0x20 ? 26 : 13);
                int cursorcolor = (colormode) ? IRGBtoBGRI(da2->attrc[LV_CURSOR_COLOR]) : 2; /* Choose color 2 if mode 8 */
                fg              = (colormode) ? getPS55ForeColor(attr, da2) : ((attr & 0x08) ? 3 : 2);
                bg              = 0;
                if (attr & 0x04) { /* Color 0 if reverse */
                    bg = fg;
                    fg = 0;
                }
                for (uint32_t n = 0; n < cursorwidth; n++)
                    if (p[n] == da2->pallook[da2->egapal[cursorcolor]] || da2->egapal[bg] == da2->egapal[cursorcolor])
                        p[n] = (p[n] == da2->pallook[da2->egapal[bg]]) ? da2->pallook[da2->egapal[fg]] : da2->pallook[da2->egapal[bg]];
                    else
                        p[n] = (p[n] == da2->pallook[da2->egapal[bg]]) ? da2->pallook[da2->egapal[cursorcolor]] : p[n];
            }
            da2->memaddr += 2;
            p += 13;
        }
        // da2->memaddr &= DA2_MASK_CRAM;
        // da2->writelines++;
    }
}

/* Display Adapter Mode 3 Drawing */
static void
da2_render_textm3(da2_t *da2)
{
    if (da2->firstline_draw == 2000)
        da2->firstline_draw = da2->displine;
    da2->lastline_draw = da2->displine;

    if (da2->fullchange) {
        int       offset = (8 - da2->scrollcache) + 24;
        uint32_t *p      = &((uint32_t *) buffer32->line[da2->displine])[offset];
        int       x;
        int       drawcursor;
        uint8_t   chr, attr, extattr;
        int       fg, bg;
        uint32_t  chr_dbcs;
        int       chr_wide = 0;
        // da2_log("\nda2ma: %x, da2sc: %x\n", da2->memaddr, da2->scanline);
        for (x = 0; x < da2->hdisp; x += 13) {
            chr     = da2_vram_r(DA2_VM03_BASECHR + da2->memaddr, da2);
            attr    = da2_vram_r(DA2_VM03_BASECHR + da2->memaddr + 1, da2);
            extattr = da2_vram_r(DA2_VM03_BASEEXATTR + da2->memaddr + 1, da2);
            // if(chr!=0x20) da2_log("addr: %x, chr: %x, attr: %x    ", (DA2_VM03_BASECHR + da2->memaddr << 1) & da2->vram_mask, chr, attr);
            bg = attr >> 4;
            // if (da2->blink) bg &= ~0x8;
            // fg = (da2->blink || (!(attr & 0x80))) ? (attr & 0xf) : bg;
            fg = attr & 0xf;
            fg = IRGBtoBGRI(fg);
            bg = IRGBtoBGRI(bg);
            /* Draw character */
            for (uint32_t n = 0; n < 13; n++)
                p[n] = da2->pallook[da2->egapal[bg]]; /* draw blank */
            /* BCS or DBCS left half */
            if (chr_wide == 0) {
                if (extattr & 0x01)
                    chr_wide = 1;
                /* Stay drawing if the char code is DBCS and not at last column. */
                if (chr_wide) {
                    /* Get high DBCS code from the next video address */
                    chr_dbcs = da2_vram_r(DA2_VM03_BASECHR + da2->memaddr + 2, da2);
                    chr_dbcs <<= 8;
                    chr_dbcs |= chr;
                    /* Get the font pattern */
                    uint32_t font = getfont_ps55dbcs(chr_dbcs, da2->scanline, da2);
                    /* Draw 13 dots */
                    for (uint32_t n = 0; n < 13; n++) {
                        p[n] = da2->pallook[da2->egapal[(font & 0x80000000) ? fg : bg]];
                        font <<= 1;
                    }
                } else {
                    /* the char code is SBCS (ANK) */
                    uint32_t fontbase;
                    if (extattr & 0x80) /* second map of SBCS font */
                        fontbase = DA2_GAIJIRAM_SBEX;
                    else
                        fontbase = DA2_GAIJIRAM_SBCS;
                    uint16_t font = da2->mmio.ram[fontbase+ chr * 0x40 + da2->scanline * 2]; /* w13xh29 font */
                    font <<= 8;
                    font |= da2->mmio.ram[fontbase + chr * 0x40 + da2->scanline * 2 + 1]; /* w13xh29 font */
                    // if(chr!=0x20) da2_log("memaddr: %x, scanline: %x, chr: %x, font: %x    ", da2->memaddr, da2->scanline, chr, font);
                    for (uint32_t n = 0; n < 13; n++) {
                        p[n] = da2->pallook[da2->egapal[(font & 0x8000) ? fg : bg]];
                        font <<= 1;
                    }
                }
            }
            /* right half of DBCS */
            else {
                uint32_t font = getfont_ps55dbcs(chr_dbcs, da2->scanline, da2);
                /* Draw 13 dots */
                for (uint32_t n = 0; n < 13; n++) {
                    p[n] = da2->pallook[da2->egapal[(font & 0x8000) ? fg : bg]];
                    font <<= 1;
                }
                chr_wide = 0;
            }
            drawcursor = ((da2->memaddr == da2->cursoraddr) && da2->cursorvisible && da2->cursoron);
            if (drawcursor && da2->scanline >= da2->crtc[LC_CURSOR_ROW_START] && da2->scanline <= da2->crtc[LC_CURSOR_ROW_END]) {
                // int cursorwidth = (da2->crtc[0x1f] & 0x20 ? 26 : 13);
                // int cursorcolor = (colormode) ? IRGBtoBGRI(da2->attrc[0x1a]) : 2;/* Choose color 2 if mode 8 */
                // fg = (colormode) ? getPS55ForeColor(attr, da2) : (attr & 0x08) ? 3 : 2;
                // bg = 0;
                // if (attr & 0x04) {/* Color 0 if reverse */
                //     bg = fg;
                //     fg = 0;
                // }
                for (uint32_t n = 0; n < 13; n++)
                    p[n] = da2->pallook[da2->egapal[fg]];
            }
            da2->memaddr += 2;
            p += 13;
        }
        // da2->memaddr &= DA2_MASK_CRAM;
        // da2->writelines++;
    }
}

static void
da2_render_color_4bpp(da2_t *da2)
{
    int changed_offset = da2->memaddr >> 9;
    // da2_log("memaddr %x cf %x\n", da2->memaddr, changed_offset);
    da2->plane_mask &= 0x0f; /*safety */

    if (da2->changedvram[changed_offset] || da2->changedvram[changed_offset + 1] || da2->fullchange) {
        int       x;
        int       offset = (8 - da2->scrollcache) + 24;
        uint32_t *p      = &((uint32_t *) buffer32->line[da2->displine])[offset];

        if (da2->firstline_draw == 2000)
            da2->firstline_draw = da2->displine;
        da2->lastline_draw = da2->displine;
        // da2_log("d %X\n", da2->memaddr);

        for (x = 0; x <= da2->hdisp; x += 8) /* hdisp = 1024 */
        {
            uint8_t edat[8];
            uint8_t dat;

            /* get 8 pixels from vram */
            da2->memaddr &= da2->vram_display_mask;
            *(uint32_t *) (&edat[0]) = *(uint32_t *) (&da2->vram[da2->memaddr << 3]);
            da2->memaddr += 1;

            dat  = ((edat[0] >> 7) & (1 << 0)) | ((edat[1] >> 6) & (1 << 1)) | ((edat[2] >> 5) & (1 << 2)) | ((edat[3] >> 4) & (1 << 3));
            p[0] = da2->pallook[da2->egapal[dat & da2->plane_mask]];
            dat  = ((edat[0] >> 6) & (1 << 0)) | ((edat[1] >> 5) & (1 << 1)) | ((edat[2] >> 4) & (1 << 2)) | ((edat[3] >> 3) & (1 << 3));
            p[1] = da2->pallook[da2->egapal[dat & da2->plane_mask]];
            dat  = ((edat[0] >> 5) & (1 << 0)) | ((edat[1] >> 4) & (1 << 1)) | ((edat[2] >> 3) & (1 << 2)) | ((edat[3] >> 2) & (1 << 3));
            p[2] = da2->pallook[da2->egapal[dat & da2->plane_mask]];
            dat  = ((edat[0] >> 4) & (1 << 0)) | ((edat[1] >> 3) & (1 << 1)) | ((edat[2] >> 2) & (1 << 2)) | ((edat[3] >> 1) & (1 << 3));
            p[3] = da2->pallook[da2->egapal[dat & da2->plane_mask]];
            dat  = ((edat[0] >> 3) & (1 << 0)) | ((edat[1] >> 2) & (1 << 1)) | ((edat[2] >> 1) & (1 << 2)) | ((edat[3] >> 0) & (1 << 3));
            p[4] = da2->pallook[da2->egapal[dat & da2->plane_mask]];
            dat  = ((edat[0] >> 2) & (1 << 0)) | ((edat[1] >> 1) & (1 << 1)) | ((edat[2] >> 0) & (1 << 2)) | ((edat[3] << 1) & (1 << 3));
            p[5] = da2->pallook[da2->egapal[dat & da2->plane_mask]];
            dat  = ((edat[0] >> 1) & (1 << 0)) | ((edat[1] >> 0) & (1 << 1)) | ((edat[2] << 1) & (1 << 2)) | ((edat[3] << 2) & (1 << 3));
            p[6] = da2->pallook[da2->egapal[dat & da2->plane_mask]];
            dat  = ((edat[0] >> 0) & (1 << 0)) | ((edat[1] << 1) & (1 << 1)) | ((edat[2] << 2) & (1 << 2)) | ((edat[3] << 3) & (1 << 3));
            p[7] = da2->pallook[da2->egapal[dat & da2->plane_mask]];
            p += 8;
        }
        // da2->writelines++;
    }
}

static void
da2_render_color_8bpp(da2_t *da2)
{
    int changed_offset = da2->memaddr >> 9;
    // da2_log("memaddr %x cf %x\n", da2->memaddr, changed_offset);

    if (da2->changedvram[changed_offset] || da2->changedvram[changed_offset + 1] || da2->fullchange) {
        int       x;
        int       offset = (8 - da2->scrollcache) + 24;
        uint32_t *p      = &((uint32_t *) buffer32->line[da2->displine])[offset];

        if (da2->firstline_draw == 2000)
            da2->firstline_draw = da2->displine;
        da2->lastline_draw = da2->displine;
        // da2_log("d %X\n", da2->memaddr);

        for (x = 0; x <= da2->hdisp; x += 8) /* hdisp = 1024 */
        {
            uint8_t edat[8];
            uint8_t dat;

            /* get 8 pixels from vram */
            da2->memaddr &= da2->vram_display_mask;
            *(uint32_t *) (&edat[0]) = *(uint32_t *) (&da2->vram[da2->memaddr << 3]);
            *(uint32_t *) (&edat[4]) = *(uint32_t *) (&da2->vram[(da2->memaddr << 3) + 4]);
            da2->memaddr += 1;

            dat  = ((edat[0] >> 7) & (1 << 0)) | ((edat[1] >> 6) & (1 << 1)) | ((edat[2] >> 5) & (1 << 2)) | ((edat[3] >> 4) & (1 << 3)) | ((edat[4] >> 3) & (1 << 4)) | ((edat[5] >> 2) & (1 << 5)) | ((edat[6] >> 1) & (1 << 6)) | ((edat[7] >> 0) & (1 << 7));
            p[0] = da2->pallook[dat];
            dat  = ((edat[0] >> 6) & (1 << 0)) | ((edat[1] >> 5) & (1 << 1)) | ((edat[2] >> 4) & (1 << 2)) | ((edat[3] >> 3) & (1 << 3)) | ((edat[4] >> 2) & (1 << 4)) | ((edat[5] >> 1) & (1 << 5)) | ((edat[6] >> 0) & (1 << 6)) | ((edat[7] << 1) & (1 << 7));
            p[1] = da2->pallook[dat];
            dat  = ((edat[0] >> 5) & (1 << 0)) | ((edat[1] >> 4) & (1 << 1)) | ((edat[2] >> 3) & (1 << 2)) | ((edat[3] >> 2) & (1 << 3)) | ((edat[4] >> 1) & (1 << 4)) | ((edat[5] >> 0) & (1 << 5)) | ((edat[6] << 1) & (1 << 6)) | ((edat[7] << 2) & (1 << 7));
            p[2] = da2->pallook[dat];
            dat  = ((edat[0] >> 4) & (1 << 0)) | ((edat[1] >> 3) & (1 << 1)) | ((edat[2] >> 2) & (1 << 2)) | ((edat[3] >> 1) & (1 << 3)) | ((edat[4] >> 0) & (1 << 4)) | ((edat[5] << 1) & (1 << 5)) | ((edat[6] << 2) & (1 << 6)) | ((edat[7] << 3) & (1 << 7));
            p[3] = da2->pallook[dat];
            dat  = ((edat[0] >> 3) & (1 << 0)) | ((edat[1] >> 2) & (1 << 1)) | ((edat[2] >> 1) & (1 << 2)) | ((edat[3] >> 0) & (1 << 3)) | ((edat[4] << 1) & (1 << 4)) | ((edat[5] << 2) & (1 << 5)) | ((edat[6] << 3) & (1 << 6)) | ((edat[7] << 4) & (1 << 7));
            p[4] = da2->pallook[dat];
            dat  = ((edat[0] >> 2) & (1 << 0)) | ((edat[1] >> 1) & (1 << 1)) | ((edat[2] >> 0) & (1 << 2)) | ((edat[3] << 1) & (1 << 3)) | ((edat[4] << 2) & (1 << 4)) | ((edat[5] << 3) & (1 << 5)) | ((edat[6] << 4) & (1 << 6)) | ((edat[7] << 5) & (1 << 7));
            p[5] = da2->pallook[dat];
            dat  = ((edat[0] >> 1) & (1 << 0)) | ((edat[1] >> 0) & (1 << 1)) | ((edat[2] << 1) & (1 << 2)) | ((edat[3] << 2) & (1 << 3)) | ((edat[4] << 3) & (1 << 4)) | ((edat[5] << 4) & (1 << 5)) | ((edat[6] << 5) & (1 << 6)) | ((edat[7] << 6) & (1 << 7));
            p[6] = da2->pallook[dat];
            dat  = ((edat[0] >> 0) & (1 << 0)) | ((edat[1] << 1) & (1 << 1)) | ((edat[2] << 2) & (1 << 2)) | ((edat[3] << 3) & (1 << 3)) | ((edat[4] << 4) & (1 << 4)) | ((edat[5] << 5) & (1 << 5)) | ((edat[6] << 6) & (1 << 6)) | ((edat[7] << 7) & (1 << 7));
            p[7] = da2->pallook[dat];
            p += 8;
        }
        // da2->writelines++;
    }
}

static void
da2_updatevidselector_tick(void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    if (da2->ioctl[LS_MODE] & 0x02) {
        /* VGA passthrough mode */
        da2->override = 1;
        timer_disable(&da2->timer);
        svga_set_override(da2->mb_vga, 0);
        da2_log("DA2 selector: VGA\n");
    } else {
        svga_set_override(da2->mb_vga, 1);
        timer_enable(&da2->timer);
        da2->override = 0;
        da2_log("DA2 selector: DA2\n");
    }
}

static void
da2_updatevidselector(da2_t *da2)
{
    timer_set_delay_u64(&da2->timer_vidupd, 100000ull * TIMER_USEC);
}

/*
    INT 10h video modes supported in DOS J4.0 (The DA-2 doesn't have a video BIOS on its card.)
    Mode        Type    Colors  Text    Base Address    PELs        Render
    3           A/N/K   16      80 x 25 B0000h/B8000h   1040 x 754  textm3
    8           A/N/K   2       80 x 25 E0000h          1040 x 725  text
    Ah          APA     1       78 x 25 A0000h          1024 x 768  color_4bpp
    Dh          APA     16      78 x 25 A0000h          1024 x 768  color_4bpp
    Eh          A/N/K   16      80 x 25 E0000h          1040 x 725  text
    Fh          APA     256     NA      A0000h          1024 x 768  color_8bpp
    45h(undoc)  APA     16      NA      A0000h          1040 x 768  color_4bpp
    46h(undoc)  APA     16      ?       A0000h          1040 x 768  color_4bpp
*/
static void
da2_recalctimings(da2_t *da2)
{
    double crtcconst;
    double _dispontime, _dispofftime, disptime;
    
    da2->vtotal      = da2->crtc[LC_VERTICAL_TOTALJ] & 0xfff;
    da2->dispend     = da2->crtc[LC_V_DISPLAY_ENABLE_END] & 0xfff;
    da2->vsyncstart  = da2->crtc[LC_VERTICAL_SYNC_START] & 0xfff;
    da2->split       = da2->crtc[LC_LINE_COMPAREJ] & 0xfff;
    da2->split -= 1;
    da2->vblankstart = da2->crtc[LC_START_VERTICAL_BLANK] & 0xfff;
    da2->hdisp       = da2->crtc[LC_H_DISPLAY_ENABLE_END];

    /* In the video mode 3, you'll see a blank below the screen. It's NOT a bug. */
    da2->hdisp -= da2->crtc[LC_START_H_DISPLAY_ENAB];
    da2->dispend -= da2->crtc[LC_START_V_DISPLAY_ENAB];
    da2->dispend += 1;

    da2->htotal = da2->crtc[LC_HORIZONTAL_TOTAL];
    da2->htotal += 1;

    da2->rowoffset = da2->crtc[LC_OFFSET]; /* number of bytes in a scanline */

    da2->clock = da2->da2const;

    if (da2->vtotal == 0)
        da2->vtotal = da2->vsyncstart = da2->vblankstart = 256;
    if (da2->htotal == 0)
        da2->htotal = da2->dispend = da2->hdisp = 64;
    if (da2->rowoffset == 0)
        da2->rowoffset = 64 * 2; /* To avoid causing a DBZ error */
    if (da2->split == 0) /* To avoid a glitch in MODE 1 of OS/2 J1.3 DOSBox. */
        da2->memaddr_latch = 0;
    else
        da2->memaddr_latch = ((da2->crtc[LC_START_ADDRESS_HIGH] & 0x3ff) << 8) | da2->crtc[LC_START_ADDRESS_LOW];

    da2->ca_adj = 0;
    da2->rowcount = da2->crtc[LC_MAXIMUM_SCAN_LINE];
    da2->hdisp_time = da2->hdisp;
    da2->render     = da2_render_blank;
    /* determine display mode */
    // if (da2->attr_palette_enable && (da2->attrc[0x1f] & 0x08))
    /* 16 color graphics mode */
    if (!(da2->ioctl[LS_MODE] & 0x01)) {
        da2->hdisp *= 16;
        da2->char_width = 13;
        if (da2->crtc[LC_VIEWPORT_PRIORITY] & 0x80) {
            da2_log("Set videomode to PS/55 8 bpp graphics.\n");
            da2->render            = da2_render_color_8bpp;
            da2->vram_display_mask = DA2_MASK_A000;
        } else { /* PS/55 8-color */
            da2_log("Set videomode to PS/55 4 bpp graphics.\n");
            da2->render            = da2_render_color_4bpp;
            da2->vram_display_mask = DA2_MASK_A000;
        }
    } else {
        /* text mode */
        if (da2->attrc[LV_ATTRIBUTE_CNTL] & 1) {
            da2_log("Set videomode to PS/55 Mode 03 text.\n");
            da2->render            = da2_render_textm3;
            da2->vram_display_mask = DA2_MASK_CRAM;
        } else { /* PS/55 text(color/mono) */
            da2_log("Set videomode to PS/55 Mode 8/E text.\n");
            da2->render            = da2_render_text;
            da2->vram_display_mask = DA2_MASK_CRAM;
        }
        da2->hdisp *= 13;
        da2->char_width = 13;
    }
    /* if output disabled or VGA passthrough */
    if (da2->ioctl[LS_MODE] & 0x02 || !(da2->attrc[LV_COMPATIBILITY] & 0x08)) 
        da2->render = da2_render_blank;

    if (da2->vblankstart < da2->dispend)
        da2->dispend = da2->vblankstart;

    crtcconst = da2->clock * da2->char_width;

    disptime    = da2->htotal;
    _dispontime = da2->hdisp_time;

    da2_log("Disptime %f dispontime %f hdisp %i\n", disptime, _dispontime, da2->hdisp);

    _dispofftime = disptime - _dispontime;
    _dispontime *= crtcconst;
    _dispofftime *= crtcconst;

    da2->dispontime  = (uint64_t) _dispontime;
    da2->dispofftime = (uint64_t) _dispofftime;
    if (da2->dispontime < TIMER_USEC)
        da2->dispontime = TIMER_USEC;
    if (da2->dispofftime < TIMER_USEC)
        da2->dispofftime = TIMER_USEC;
    da2_log("da2 horiz total %i display end %i vidclock %f\n", da2->crtc[0], da2->crtc[1], da2->clock);
    da2_log("da2 vert total %i display end %i max row %i vsync %i\n",da2->vtotal,da2->dispend,(da2->crtc[9]&31)+1,da2->vsyncstart);
    da2_log("da2 dispon %lu dispoff %lu on(us) %f off(us) %f\n",da2->dispontime, da2->dispofftime, (double)da2->dispontime / (double)cpuclock /  (double) (1ULL << 32) * 1000000.0, (double)da2->dispofftime / (double)cpuclock /  (double) (1ULL << 32) * 1000000.0);
    da2_log("da2 linecompare %d\n", da2->split);
}

static void
da2_mapping_update(da2_t *da2)
{
    /* Has the CardEnable bit been changed? */
    if (!((da2->pos_regs[2] ^ da2->old_pos2) & 1))
        return;
    da2->old_pos2 = da2->pos_regs[2];
    // da2_recalc_mapping(da2);
    if (da2->pos_regs[2] & 0x01) {
        da2_log("DA2 enable registers\n");
        for (uint8_t i = 0; i < 8; i++)
            da2_log("DA2 POS[%d]: %x\n", i, da2->pos_regs[i]);
        io_sethandler(0x03c0, 0x000a, da2_inb, da2_inw, NULL, da2_outb, da2_outw, NULL, da2);
        io_sethandler(0x03e0, 0x0010, da2_inb, da2_inw, NULL, da2_outb, da2_outw, NULL, da2);
        io_sethandler(0x03d0, 0x000b, da2_in_ISR, NULL, NULL, da2_out_ISR, NULL, NULL, da2);
        mem_mapping_enable(&da2->cmapping);
        mem_mapping_enable(&da2->mmio.mapping);
        timer_enable(&da2->bitblt.timer);
    } else {
        da2_log("DA2 disable registers\n");
        timer_disable(&da2->bitblt.timer);
        mem_mapping_disable(&da2->cmapping);
        mem_mapping_disable(&da2->mmio.mapping);
        io_removehandler(0x03c0, 0x000a, da2_inb, da2_inw, NULL, da2_outb, da2_outw, NULL, da2);
        io_removehandler(0x03e0, 0x0010, da2_inb, da2_inw, NULL, da2_outb, da2_outw, NULL, da2);
        io_removehandler(0x03d0, 0x000b, da2_in_ISR, NULL, NULL, da2_out_ISR, NULL, NULL, da2);
    }
}

static uint8_t
da2_mca_read(int port, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    return da2->pos_regs[port & 7];
}

static void
da2_mca_write(int port, uint8_t val, void *priv)
{
    da2_t *da2 = (da2_t *) priv;

    da2_log("da2_mca_write: port=%04x val=%02x\n", port, val);

    if (port < 0x102)
        return;
    da2->pos_regs[port & 7] = val;

    da2_mapping_update(da2);
}

static uint8_t
da2_mca_feedb(void *priv)
{
    const da2_t *da2 = (da2_t *) priv;

    return da2->pos_regs[2] & 0x01;
}

static void
da2_mca_reset(void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    da2_log("da2_mca_reset called.\n");
    da2_reset(da2);
    da2_mca_write(0x102, 0, da2);
}

/* ROP gdcinput and gdcsrc, and write the result with bitmask at addr (byte) */
static void
da2_gdcropB(uint32_t addr,uint8_t bitmask, da2_t *da2)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (da2->planemask & (1 << i)) {
            // da2_log("da2_gdcropB o%x a%x d%x p%d m%x\n", da2->gdcreg[LG_COMMAND] & 0x03, addr, da2->gdcinput[i], i, bitmask);
            switch (da2->gdcreg[LG_COMMAND] & 0x03) {
                case 0: /*Set*/
                    // da2->vram[addr | i] = (da2->gdcinput[i] & bitmask) | (da2->gdcsrc[i] & ~bitmask);
                    // da2->vram[addr | i] = (da2->gdcinput[i] & bitmask) | (da2->vram[addr | i] & ~bitmask);
                    da2_vram_w(addr | i, (da2->gdcinput[i] & bitmask) | (da2->vram[addr | i] & ~bitmask), da2);
                    break;
                case 1: /*AND*/
                    // da2->vram[addr | i] = (da2->gdcinput[i] | ~bitmask) & da2->gdcsrc[i];
                    da2_vram_w(addr | i, ((da2->gdcinput[i] & da2->gdcsrc[i]) & bitmask) | (da2->vram[addr | i] & ~bitmask), da2);
                    break;
                case 2: /*OR*/
                    // da2->vram[addr | i] = (da2->gdcinput[i] & bitmask) | da2->gdcsrc[i];
                    da2_vram_w(addr | i,  ((da2->gdcinput[i] | da2->gdcsrc[i]) & bitmask) | (da2->vram[addr | i] & ~bitmask), da2);
                    break;
                case 3: /*XOR*/
                    // da2->vram[addr | i] = (da2->gdcinput[i] & bitmask) ^ da2->gdcsrc[i];
                    da2_vram_w(addr | i,  ((da2->gdcinput[i] ^ da2->gdcsrc[i]) & bitmask) | (da2->vram[addr | i] & ~bitmask), da2);
                    break;
            }
        }
    }
}
/* ROP gdcinput and gdcsrc, and write the result with bitmask at addr (word) */
static void
da2_gdcropW(uint32_t addr, uint16_t bitmask, da2_t *da2)
{
    if((addr & 8) && !(da2->gdcreg[LG_COMMAND] & 0x08)) bitmask = da2_rightrotate(bitmask, 8);
    // if((addr & 8)) bitmask = da2_rightrotate(bitmask, 8);
    uint8_t bitmask_l = bitmask & 0xff;
    uint8_t bitmask_h = bitmask >> 8;
    for (uint8_t i = 0; i < 8; i++) {
        if (da2->planemask & (1 << i)) {
            // da2_log("da2_gdcropW m%x a%x d%x i%d ml%x mh%x\n", da2->gdcreg[LG_COMMAND] & 0x03, addr, da2->gdcinput[i], i, da2->gdcreg[LG_BIT_MASK_LOW], da2->gdcreg[LG_BIT_MASK_HIGH]);
            switch (da2->gdcreg[LG_COMMAND] & 0x03) {
                case 0: /*Set*/
                    // da2->vram[addr | i]       = (da2->gdcinput[i] & bitmask_l) | (da2->gdcsrc[i] & ~bitmask_l);
                    // da2->vram[(addr + 8) | i] = ((da2->gdcinput[i] >> 8) & bitmask_h) | ((da2->gdcsrc[i] >> 8) & ~bitmask_h);
                    da2_vram_w(addr | i, (da2->gdcinput[i] & bitmask_l) | (da2->vram[addr | i] & ~bitmask_l), da2);
                    da2_vram_w((addr + 8) | i, ((da2->gdcinput[i] >> 8) & bitmask_h)
                        | (da2->vram[(addr + 8) | i] & ~bitmask_h), da2);
                    break;
                case 1: /*AND*/
                    // da2->vram[addr | i]       = (da2->gdcinput[i] | ~bitmask_l) & da2->gdcsrc[i];
                    // da2->vram[(addr + 8) | i] = ((da2->gdcinput[i] >> 8) | ~bitmask_h) & (da2->gdcsrc[i] >> 8);
                    da2_vram_w(addr | i, ((da2->gdcinput[i] & da2->gdcsrc[i]) & bitmask_l) | (da2->vram[addr | i] & ~bitmask_l), da2);
                    da2_vram_w((addr + 8) | i, (((da2->gdcinput[i] >> 8) & (da2->gdcsrc[i] >> 8)) & bitmask_h)
                        | (da2->vram[(addr + 8) | i] & ~bitmask_h), da2);
                    break;
                case 2: /*OR*/
                    // da2->vram[addr | i]       = (da2->gdcinput[i] & bitmask_l) | da2->gdcsrc[i];
                    // da2->vram[(addr + 8) | i] = ((da2->gdcinput[i] >> 8) & bitmask_h) | (da2->gdcsrc[i] >> 8);
                    da2_vram_w(addr | i, ((da2->gdcinput[i] | da2->gdcsrc[i]) & bitmask_l) | (da2->vram[addr | i] & ~bitmask_l), da2);
                    da2_vram_w((addr + 8) | i, (((da2->gdcinput[i] >> 8) | (da2->gdcsrc[i] >> 8)) & bitmask_h)
                        | (da2->vram[(addr + 8) | i] & ~bitmask_h), da2);
                    break;
                case 3: /*XOR*/
                    // da2->vram[addr | i]       = (da2->gdcinput[i] & bitmask_l) ^ da2->gdcsrc[i];
                    // da2->vram[(addr + 8) | i] = ((da2->gdcinput[i] >> 8) & bitmask_h) ^ (da2->gdcsrc[i] >> 8);
                    da2_vram_w(addr | i, ((da2->gdcinput[i] ^ da2->gdcsrc[i]) & bitmask_l) | (da2->vram[addr | i] & ~bitmask_l), da2);
                    da2_vram_w((addr + 8) | i, (((da2->gdcinput[i] >> 8) ^ (da2->gdcsrc[i] >> 8)) & bitmask_h)
                        | (da2->vram[(addr + 8) | i] & ~bitmask_h), da2);
                    break;
            }
        }
    }
}

static uint8_t
da2_mmio_read(uint32_t addr, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    uint32_t index = 0;
    addr &= DA2_MASK_MMIO;
    if (da2->ioctl[LS_MMIO] & 0x10) {
        if (da2->fctl[LF_MMIO_SEL] == 0x80)
            /* linear access */
            addr |= ((uint32_t) da2->fctl[LF_MMIO_ADDR] << 17);
        else {
            /* 64k bank switch access */
            index = da2->fctl[LF_MMIO_MODE] & 0x0f;
            index <<= 8;
            index |= da2->fctl[LF_MMIO_ADDR];
        }
        // da2_log("PS55_MemHnd: Read from mem %x, bank %x, addr %x\n", da2->fctl[LF_MMIO_MODE], da2->fctl[LF_MMIO_ADDR], addr);
        switch (da2->fctl[LF_MMIO_MODE] & 0xf0) {
            case 0xb0:                     /* Gaiji RAM */
                addr += index * 0x80;
                addr &= DA2_MASK_GAIJIRAM; /* safety access */
                // da2_log("PS55_MemHnd_G: Read from mem %x, bank %x, chr %x (%x), val %x\n", da2->fctl[LF_MMIO_MODE], da2->fctl[LF_MMIO_ADDR], addr / 128, addr, da2->mmio.font[addr]);
                return da2->mmio.ram[addr];
                break;
            case 0x10: /* Font ROM */
                if (da2->mmio.charset == DA2_DCONFIG_CHARSET_HANT) {
                    if (addr >= 0x1a0000)
                        return DA2_INVALIDACCESS8;
                    if (addr >= 0x180000)
                        addr -= 0x40000; /* The bank 12 (180000h-19ffffh) is beyond the available ROM address range,
                                              but the Chinese font sub card actually has this alias, and is used by DOS T5.0. */
                }
                if (addr >= DA2_FONTROM_SIZE)
                    return DA2_INVALIDACCESS8;
                // da2_log("PS55_MemHnd: Read from mem %x, bank %x, chr %x (%x), val %x\n", da2->fctl[LF_MMIO_MODE], da2->fctl[LF_MMIO_ADDR], addr / 72, addr, da2->mmio.font[addr]);
                return da2->mmio.font[addr];
                break;
            case 0x00: /* SBCS in Gaiji RAM (used in the downward writing mode of DOS/V Extension) */
                addr += DA2_GAIJIRAM_SBCS + index * 0x40;
                addr &= DA2_MASK_GAIJIRAM;
                return da2->mmio.ram[addr];
                break;
            default:
                da2_log("PS55_MemHnd: Invalid read mem %x, bank %x, chr %x (%x), val %x\n", da2->fctl[LF_MMIO_MODE], da2->fctl[LF_MMIO_ADDR], addr / 72, addr, da2->mmio.font[addr]);
                return DA2_INVALIDACCESS8; /* invalid memory access */
                break;
        }
    } else if (!(da2->ioctl[LS_MODE] & 1)) { /* 16 or 256 color mode */
        cycles -= video_timing_read_b;
        for (uint8_t i = 0; i < 8; i++)
            da2->gdcla[i] = da2->vram[(addr << 3) | i]; /* read in byte */
#ifdef ENABLE_DA2_DEBUGVRAM
        da2_log("da2_Rb: %05x=%02x\n", addr, da2->gdcla[da2->readplane]);
#endif
        if (da2->gdcreg[LG_MODE] & 0x08) { /* compare data across planes if the read mode bit (3EB 05, bit 3) is 1 */
            uint8_t ret = 0;
            for (uint8_t i = 0; i < 8; i++) {
                if (~da2->gdcreg[LG_COLOR_DONT_CARE] & (1 << i)) /* color don't care register */
                    ret |= da2->gdcla[i] ^ ((da2->gdcreg[LG_COLOR_COMPAREJ] & (1 << i)) ? 0xff : 0);
            }
            return ~ret;
        } else
            return da2->gdcla[da2->readplane];
    } else { /* text mode 3 */
        cycles -= video_timing_read_b;
        return da2->vram[addr];
    }
}
static uint16_t
da2_mmio_readw(uint32_t addr, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    // da2_log("da2_readW: %x %x %x %x %x\n", da2->ioctl[LS_MMIO], da2->fctl[LF_MMIO_SEL], da2->fctl[LF_MMIO_MODE], da2->fctl[LF_MMIO_ADDR], addr);
    // da2_log("da2_readW: %x %x %x %x %x CS:PC=%4x:%4x\n", da2->ioctl[LS_MMIO], da2->fctl[LF_MMIO_SEL], da2->fctl[LF_MMIO_MODE], da2->fctl[LF_MMIO_ADDR], addr, CS, cpu_state.pc);

    if (da2->ioctl[LS_MMIO] & 0x10) {
        return (uint16_t) da2_mmio_read(addr, da2) | (uint16_t) (da2_mmio_read(addr + 1, da2) << 8);
    } else if (!(da2->ioctl[LS_MODE] & 1)) {/* 16 color or 256 color mode */
        cycles -= video_timing_read_w;
        addr &= DA2_MASK_MMIO;
        for (uint8_t i = 0; i < 8; i++)
            da2->gdcla[i] = (uint16_t) (da2->vram[(addr << 3) | i]) | ((uint16_t) (da2->vram[((addr << 3) + 8) | i]) << 8); /* read vram into latch */

#ifdef ENABLE_DA2_DEBUGVRAM
        ////debug
        // if (((int)addr - (int)da2->mmrdbg_vidaddr) > 2 || (((int)da2->mmrdbg_vidaddr - (int)addr) > 2) || da2->mmrdbg_vidaddr == addr)
        //{
        //     fprintf(da2->mmrdbg_fp, "\nR %x ", addr);
        //     for (uint8_t i = 0; i <= 0xb; i++)
        //         fprintf(da2->mmrdbg_fp, "%02x ", da2->gdcreg[i]);
        // }
        // for (uint8_t i = 0; i < 16; i++)
        //{
        //     int pixeldata = 0;
        //     if (da2->gdcla[da2->readplane] & (1 << (15 - i))) pixeldata = 1;
        //     fprintf(da2->mmrdbg_fp, "%X", pixeldata);
        // }
        // da2->mmrdbg_vidaddr = addr;
#endif

        if (da2->gdcreg[LG_MODE] & 0x08) { /* compare data across planes if the read mode bit (3EB 05, bit 3) is 1 */
            uint16_t ret = 0;

            for (uint8_t i = 0; i < 8; i++) {
                if (~da2->gdcreg[LG_COLOR_DONT_CARE] & (1 << i)) /* color don't care register */
                    ret |= da2->gdcla[i] ^ ((da2->gdcreg[LG_COLOR_COMPAREJ] & (1 << i)) ? 0xffff : 0);
            }
            return ~ret;
        } else {
#ifdef ENABLE_DA2_DEBUGVRAM
            da2_log("da2_Rw: %05x(%d) = %04x\n", addr, da2->readplane, da2->gdcla[da2->readplane]);
#endif
            return da2->gdcla[da2->readplane];
        }
    } else {
        return (uint16_t) da2_mmio_read(addr, da2) | (uint16_t) (da2_mmio_read(addr + 1, da2) << 8);
    }
}
static void
da2_mmio_write(uint32_t addr, uint8_t val, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    uint32_t index = 0;
    // da2_log("da2_mmio_write %x %x\n", addr, val);
    // if ((addr & ~DA2_MASK_MMIO) != 0xA0000)
    //     return;
    addr &= DA2_MASK_MMIO;

    if (da2->ioctl[LS_MMIO] == 0x1f) {/* write bitblt fifo data */
        da2_bitblt_addpayload(val, da2);
    }
    else if (da2->ioctl[LS_MMIO] & 0x10) { /* access to gaiji ram */
        // if(da2->ioctl[LS_MMIO] == 0x1f) da2_log("mw mem %x, addr %x, val %x, ESDI %x:%x DSSI %x:%x\n", da2->fctl[LF_MMIO_MODE], addr, val, ES, DI, DS, SI);
        /* Gaiji RAM */
        if (da2->fctl[LF_MMIO_SEL] == 0x80)
            addr |= ((uint32_t) da2->fctl[LF_MMIO_ADDR] << 17); /* xxxy yyyy yyyy yyyy yyyy */
        else { 
            index = da2->fctl[LF_MMIO_MODE] & 0x0f;
            index <<= 8;
            index |= da2->fctl[LF_MMIO_ADDR];
        // da2_log("da2_mmio_w io %x, sl %x, 09 %x, ad %x, mm %x, addr %x, val %x\n", da2->ioctl[LS_MMIO],da2->fctl[LF_MMIO_SEL], 
        // da2->fctl[LF_MMIO_09],da2->fctl[LF_MMIO_ADDR], da2->fctl[LF_MMIO_MODE], addr, val);
        }
        switch (da2->fctl[LF_MMIO_MODE] & 0xf0) {
            case 0xb0: /* Gaiji RAM 1011 0000 */
                addr += index * 0x80;
                da2->mmio.ram[addr & DA2_MASK_GAIJIRAM] = val;
                break;
            case 0x10: /* Font ROM  0001 0000 */
                /* Read-Only */
                break;
            case 0x00: /* SBCS in Gaiji RAM (used by DOS/V Extension DSPXDA2 driver) */
                addr += index * 0x40;
                da2->mmio.ram[(DA2_GAIJIRAM_SBCS + addr) & DA2_MASK_GAIJIRAM] = val;
                // da2_log("da2_mmio_write %x %x %04X:%04X\n", addr, val, CS, cpu_state.pc);
                break;
            default:
                da2_log("da2_mmio_write failed io %x, mm %x, addr %x, val %x\n", da2->ioctl[LS_MMIO] ,da2->fctl[LF_MMIO_MODE], addr, val);
                break;
        }
    } else if (!(da2->ioctl[LS_MODE] & 1)) { /* 16 color or 256 color mode */
        uint8_t bitmask;
        /* Align bitmask with even address */
        /* With byte align: Win 3.1 (Window) - ok, Solitaire 3.1 - ok, A-Train IV (splash): bad,  OS/2 J2.0(cmd) - ok */
        // if ((addr & 1)  && !(da2->gdcreg[LG_COMMAND] & 0x08)) bitmask = da2->gdcreg[LG_BIT_MASK_HIGH];
        /* Without byte align: Win 3.1 (Window) - bad, Solitaire 3.1 - ok, A-Train IV (splash): ok,  OS/2 J2.0(cmd) - ok */
        /* With byte align: Win 3.1 (Window) - ok, Solitaire 3.1 - ok, A-Train IV (splash): ok, OS/2 J2.0(cmd) - ok */
        if ((addr & 1)) bitmask = da2->gdcreg[LG_BIT_MASK_HIGH];
        else  
        /* No align: Win 3.1 (Window Title) - ok, Solitaire 3.1 - ok, A-Train IV (splash): bad,  OS/2 J2.0(cmd) - bad */
            bitmask = da2->gdcreg[LG_BIT_MASK_LOW];
        
#ifdef ENABLE_DA2_DEBUGVRAM
        // da2_log("da2_wB %x %02x\n", addr, val);
        da2_log("da2_wB %x %d %d %02x\n", addr,
            addr % (da2->rowoffset * 2) * 8, addr / (da2->rowoffset * 2), val);
        // if (!(da2->gdcreg[LG_COMMAND] & 0x08))
        //{
        if (((int) addr - (int) da2->mmdbg_vidaddr) > 2 || (((int) da2->mmdbg_vidaddr - (int) addr) > 2) || da2->mmdbg_vidaddr == addr) {
            fprintf(da2->mmdbg_fp, "\nB %x %02x ", addr, val);
            for (uint8_t i = 0; i <= 0xb; i++)
                fprintf(da2->mmdbg_fp, "%02x ", da2->gdcreg[i]);
        }
        for (uint8_t i = 0; i < 8; i++) {
            int pixeldata = 0;
            if (val & (1 << (7 - i)))
                pixeldata = (da2->planemask & 0xf);
            fprintf(da2->mmdbg_fp, "%X", pixeldata);
        }
        da2->mmdbg_vidaddr = addr;
        //}
#endif
        cycles -= video_timing_write_b;
        da2->changedvram[addr >> 9] = 3;/* 0x1FFFF -> 0x1F */
        addr <<= 3;

        for (uint8_t i = 0; i < 8; i++)
            da2->gdcsrc[i] = da2->gdcla[i]; /* use latch */

        // da2_log("da2_Wb m%02x r%02x %05x:%02x %x:%x\n", da2->gdcreg[0x5], da2->gdcreg[LG_COMMAND], addr >> 3, val, cs >> 4, cpu_state.pc);
        // da2_log("da2_Wb m%02x r%02x %05x:%02x=%02x%02x%02x%02x->", da2->gdcreg[0x5], da2->gdcreg[LG_COMMAND], addr >> 3, val, da2->vram[addr + 0], da2->vram[addr + 1], da2->vram[addr + 2], da2->vram[addr + 3]);

        if (!(da2->gdcreg[LG_COMMAND] & 0x08)) {
            for (uint8_t i = 0; i < 8; i++)
                if (da2->gdcreg[LG_ENABLE_SRJ] & (1 << i))
                    da2->gdcinput[i] = (da2->gdcreg[LG_SET_RESETJ] & (1 << i)) ? 0xff : 0;
                else if (da2->gdcreg[LG_SET_RESETJ] & (1 << i))
                    da2->gdcinput[i] = ~val;
                else
                    da2->gdcinput[i] = val;
            da2_gdcropB(addr, bitmask, da2);
            return;
        }

        switch (da2->writemode) {
            case 2: /* equiv to vga write mode 1 */
                for (uint8_t i = 0; i < 8; i++)
                    if (da2->planemask & (1 << i))
                        da2_vram_w(addr | i, da2->gdcsrc[i], da2);
                break;
            case 0:/* equiv to vga write mode 0 */
                if (da2->gdcreg[LG_DATA_ROTATION] & 7)
                    val = svga_rotate[da2->gdcreg[LG_DATA_ROTATION] & 7][val];
                if (bitmask == 0xff && !(da2->gdcreg[LG_COMMAND] & 0x03) && (!da2->gdcreg[LG_ENABLE_SRJ])) {
                    for (uint8_t i = 0; i < 8; i++)
                        if (da2->planemask & (1 << i))
                            da2_vram_w(addr | i, val, da2);
                } else {
                    for (uint8_t i = 0; i < 8; i++)
                        if (da2->gdcreg[LG_ENABLE_SRJ] & (1 << i))
                            da2->gdcinput[i] = (da2->gdcreg[LG_SET_RESETJ] & (1 << i)) ? 0xff : 0;
                        else
                            da2->gdcinput[i] = val;
                    da2_gdcropB(addr, bitmask, da2);
                }
                break;
            case 1:/* equiv to vga write mode 2 */
                    for (uint8_t i = 0; i < 8; i++)
                        da2->gdcinput[i] = ((val & (1 << i)) ? 0xff : 0);
                    da2_gdcropB(addr, bitmask, da2);
                break;
            case 3:/* equiv to vga write mode 3 */
                if (da2->gdcreg[LG_DATA_ROTATION] & 7)
                    val = svga_rotate[da2->gdcreg[LG_DATA_ROTATION] & 7][val];
                bitmask &= val;

                for (uint8_t i = 0; i < 8; i++)
                        da2->gdcinput[i] = (da2->gdcreg[LG_SET_RESETJ] & (1 << i)) ? 0xff : 0;
                da2_gdcropB(addr, bitmask, da2);
                break;
        }
    } else { /*  mode 3h text */
        cycles -= video_timing_write_b;
        da2_vram_w(addr, val, da2);
        da2->fullchange = 3;
    }
}
static uint16_t
da2_rightrotate(uint16_t data, uint8_t count)
{
    return (data >> count) | (data << (sizeof(data) * 8 - count));
}
static void
da2_mmio_gc_writeW(uint32_t addr, uint16_t val, void *priv)
{
    da2_t   *da2     = (da2_t *) priv;
    uint16_t bitmask;
    addr &= DA2_MASK_MMIO;
    bitmask = da2->gdcreg[LG_BIT_MASK_HIGH];
    bitmask <<= 8;
    bitmask |= (uint16_t) da2->gdcreg[LG_BIT_MASK_LOW];
#ifdef ENABLE_DA2_DEBUGVRAM
    da2_log("da2_wW %x %d %d %02x\n", addr,
    addr % (da2->rowoffset * 2) * 8, addr / (da2->rowoffset * 2), val);

    if (((int) addr - (int) da2->mmdbg_vidaddr) > 2 || (((int) da2->mmdbg_vidaddr - (int) addr) > 2) || da2->mmdbg_vidaddr == addr) {
        fprintf(da2->mmdbg_fp, "\nW %x %x ", addr, val);
        for (uint8_t i = 0; i <= 0xb; i++)
            fprintf(da2->mmdbg_fp, "%02x ", da2->gdcreg[i]);
    }
    for (uint8_t i = 0; i < 16; i++) {
        int pixeldata = 0;
        if (val & (1 << (15 - i)))
            pixeldata = (da2->planemask & 0xf);
        fprintf(da2->mmdbg_fp, "%X", pixeldata);
    }
    da2->mmdbg_vidaddr = addr;
#endif
    cycles -= video_timing_write_w;

    // da2_log("da2_gcW m%d a%x d%x\n", da2->writemode, addr, val);
    // da2_log("da2_gcW %05X %02X %04X:%04X esdi %04X:%04X dssi %04X:%04X\n", addr, val, cs >> 4, cpu_state.pc, ES, DI, DS, SI);

    da2->changedvram[addr >> 9]       = 3;
    addr <<= 3;

    for (uint8_t i = 0; i < 8; i++)
        da2->gdcsrc[i] = da2->gdcla[i]; /* use latch */

    if (!(da2->gdcreg[LG_COMMAND] & 0x08)) {
        for (uint8_t i = 0; i < 8; i++)
            if (da2->gdcreg[LG_ENABLE_SRJ] & (1 << i))
                da2->gdcinput[i] = (da2->gdcreg[LG_SET_RESETJ] & (1 << i)) ? 0xffff : 0;
            else if (da2->gdcreg[LG_SET_RESETJ] & (1 << i))
                da2->gdcinput[i] = ~val;
            else
                da2->gdcinput[i] = val;
        da2_gdcropW(addr, bitmask, da2);
        return;
    }
    // da2_log("da2_Ww m%02x r%02x %05x:%04x=%02x%02x%02x%02x,%02x%02x%02x%02x->", da2->gdcreg[0x5], da2->gdcreg[LG_COMMAND], addr >> 3, val, da2->vram[addr + 0], da2->vram[addr + 1], da2->vram[addr + 2], da2->vram[addr + 3]
    //     , da2->vram[addr + 8], da2->vram[addr + 9], da2->vram[addr + 10], da2->vram[addr + 11]);
    switch (da2->writemode) {
        case 2:
            for (uint8_t i = 0; i < 8; i++)
                if (da2->planemask & (1 << i)) {
                    da2_vram_w(addr | i, da2->gdcsrc[i] & 0xff, da2);
                    da2_vram_w((addr + 8) | i, da2->gdcsrc[i] >> 8, da2);
                }
            break;
        case 0:
            if (da2->gdcreg[LG_DATA_ROTATION] & 15)
                val = da2_rightrotate(val, da2->gdcreg[LG_DATA_ROTATION] & 15);
            if (bitmask == 0xffff && !(da2->gdcreg[LG_COMMAND] & 0x03) && (!da2->gdcreg[LG_ENABLE_SRJ])) {
                for (uint8_t i = 0; i < 8; i++)
                    if (da2->planemask & (1 << i)) {
                        da2_vram_w(addr | i, val & 0xff, da2);
                        da2_vram_w((addr + 8) | i, val >> 8, da2);
                    }
            } else {
                for (uint8_t i = 0; i < 8; i++)
                    if (da2->gdcreg[LG_ENABLE_SRJ] & (1 << i))
                        da2->gdcinput[i] = (da2->gdcreg[LG_SET_RESETJ] & (1 << i)) ? 0xffff : 0;
                    else
                        da2->gdcinput[i] = val;
                da2_gdcropW(addr, bitmask, da2);
                // da2_log("- %02X %02X %02X %02X   %08X\n",vram[addr],vram[addr|0x1],vram[addr|0x2],vram[addr|0x3],addr);
            }
            break;
        case 1:
            for (uint8_t i = 0; i < 8; i++)
                da2->gdcinput[i] = ((val & (1 << i)) ? 0xffff : 0);
            da2_gdcropW(addr, bitmask, da2);
            break;
        case 3:
            if (da2->gdcreg[LG_DATA_ROTATION] & 15)
                val = da2_rightrotate(val, da2->gdcreg[LG_DATA_ROTATION] & 15);
            bitmask &= val;

            for (uint8_t i = 0; i < 8; i++)
                    da2->gdcinput[i] = (da2->gdcreg[LG_SET_RESETJ] & (1 << i)) ? 0xffff : 0;
            da2_gdcropW(addr, bitmask, da2);
            break;
    }
    // da2_log("%02x%02x%02x%02x,%02x%02x%02x%02x\n", da2->vram[addr + 0], da2->vram[addr + 1], da2->vram[addr + 2], da2->vram[addr + 3]
    //     , da2->vram[addr + 8], da2->vram[addr + 9], da2->vram[addr + 10], da2->vram[addr + 11]);
}
static void
da2_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    // if (da2->bitblt.exec != DA2_BLT_CIDLE) /* Bitblt is in operation. */
    //     return;
    // if ((addr & ~0x1ffff) != 0xA0000) return;
    if (da2->ioctl[LS_MMIO] & 0x10) {
#ifdef ENABLE_DA2_DEBUGVRAM
        da2_log("da2_mmio_writeW %x %x %x %x %x %x\n", da2->ioctl[LS_MMIO], da2->fctl[LF_MMIO_SEL], da2->fctl[LF_MMIO_MODE], da2->fctl[LF_MMIO_ADDR], addr, val);
#endif
        da2_mmio_write(addr, val & 0xff, da2);
        da2_mmio_write(addr + 1, val >> 8, da2);
    } else if (!(da2->ioctl[LS_MODE] & 1)) { /* 16 color or 256 color mode */
#ifdef ENABLE_DA2_DEBUGVRAM
        da2_log("da2_mmio_writeGW %x %x %x %x %x %x\n", da2->ioctl[LS_MMIO], da2->fctl[LF_MMIO_SEL], da2->fctl[LF_MMIO_MODE], da2->fctl[LF_MMIO_ADDR], addr, val);
#endif
        da2_mmio_gc_writeW(addr, val, da2);
    } else { /* mode 3h text */
#ifdef ENABLE_DA2_DEBUGVRAM
        // if (addr & 0xff00 == 0) da2_log("da2_mmio_write %x %x %04X:%04X\n", addr, val, CS, cpu_state.pc);
        da2_log("da2_mmio_write3W %x %x %x %x %x %x\n", da2->ioctl[LS_MMIO], da2->fctl[LF_MMIO_SEL], da2->fctl[LF_MMIO_MODE], da2->fctl[LF_MMIO_ADDR], addr, val);
#endif
        da2_mmio_write(addr, val & 0xff, da2);
        da2_mmio_write(addr + 1, val >> 8, da2);
    }
}

static void
da2_code_write(uint32_t addr, uint8_t val, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    // if ((addr & ~0xfff) != 0xE0000) return;
    addr &= DA2_MASK_CRAM;
    da2->cram[addr] = val;
    da2->fullchange = 3;
}
static void
da2_code_writeb(uint32_t addr, uint8_t val, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    // da2_log("DA2_code_writeb: Write to %x, val %x\n", addr, val);
    cycles -= video_timing_write_b;
    da2_code_write(addr, val, da2);
}
static void
da2_code_writew(uint32_t addr, uint16_t val, void *priv)
{
    // da2_log("DA2_code_writ   ew: Write to %x, val %x\n", addr, val);
    da2_t *da2 = (da2_t *) priv;
    cycles -= video_timing_write_w;
    da2_code_write(addr, val & 0xff, da2);
    da2_code_write(addr + 1, val >> 8, da2);
}

static uint8_t
da2_code_read(uint32_t addr, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    // if ((addr & ~DA2_MASK_CRAM) != 0xE0000)
    //     return DA2_INVALIDACCESS8;
    addr &= DA2_MASK_CRAM;
    return da2->cram[addr];
}
static uint8_t
da2_code_readb(uint32_t addr, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    cycles -= video_timing_read_b;
    return da2_code_read(addr, da2);
}
static uint16_t
da2_code_readw(uint32_t addr, void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    cycles -= video_timing_read_w;
    return da2_code_read(addr, da2) | (da2_code_read(addr + 1, da2) << 8);
}

static void
da2_doblit(int wx, int wy, da2_t *da2)
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
da2_poll(void *priv)
{
    da2_t *da2 = (da2_t *) priv;
    int    x;

    if (!da2->linepos) {
        timer_advance_u64(&da2->timer, da2->dispofftime);
        // if (output) printf("Display off %f\n",vidtime);
        da2->cgastat |= 1;
        da2->linepos = 1;

        if (da2->dispon) {
            da2->hdisp_on = 1;

            da2->memaddr &= da2->vram_display_mask;
            if (da2->firstline == 2000) {
                da2->firstline = da2->displine;
                video_wait_for_buffer();
            }

            if (!da2->override && ((da2->displine ^ !da2->oddeven) & 1))
                da2->render(da2);

            if (da2->lastline < da2->displine)
                da2->lastline = da2->displine;
        }
        // if(da2->fullchange)
        //     pclog("%03i %05X %d %d\n", da2->displine, da2->memaddr, ((da2->displine ^ !da2->oddeven) & 1), da2->fullchange);
        da2->displine++;
        if ((da2->cgastat & 8) && ((da2->displine & 0xf) == (da2->crtc[LC_VERTICAL_SYNC_END] & 0xf)) && da2->vslines) {
            // da2_log("Vsync off at line %i\n",displine);
            da2->cgastat &= ~8;
        }
        da2->vslines++;
        if (da2->displine > 1200)
            da2->displine = 0;
        // da2_log("Col is %08X %08X %08X   %i %i  %08X\n",((uint32_t *)buffer32->line[displine])[320],((uint32_t *)buffer32->line[displine])[321],((uint32_t *)buffer32->line[displine])[322],
        // displine, vc, memaddr);
    } else {
        // da2_log("VC %i memaddr %05X\n", da2->vc, da2->memaddr);
        timer_advance_u64(&da2->timer, da2->dispontime);

        if (da2->dispon)
            da2->cgastat &= ~1;
        da2->hdisp_on = 0;

        da2->linepos = 0;
        if (da2->scanline == (da2->crtc[LC_CURSOR_ROW_END] & 31))
            da2->cursorvisible = 0;
        if (da2->dispon) {
            if (da2->scanline == da2->rowcount) {
                da2->linecountff = 0;
                da2->scanline          = 0;

                da2->memaddr_backup += (da2->rowoffset << 1); /*   color = 0x50(80), mono = 0x40(64) */
                da2->memaddr_backup &= da2->vram_display_mask;
                da2->memaddr = da2->memaddr_backup;
            } else {
                da2->scanline++;
                da2->scanline &= 31;
                da2->memaddr = da2->memaddr_backup;
            }
        }

        da2->vc++;
        da2->vc &= 2047;

        if (da2->vc == da2->split) {
            // da2->memaddr = da2->memaddr_backup = da2->hblank_sub;
            da2->memaddr = da2->memaddr_backup = 0;
            da2->scanline = 0;
            // da2->displine    = 0;
        }

        if (da2->vc == da2->dispend) {
            da2->dispon = 0;
            // if (da2->crtc[10] & 0x20) da2->cursoron = 0;
            // else da2->cursoron = da2->blink & 16;
            if (da2->ioctl[LS_MODE] & 1) {                /* in text mode */
                if (da2->attrc[LV_CURSOR_CONTROL] & 0x01) {/* cursor blinking */
                    da2->cursoron = (da2->blink | 1) & da2->blinkconf;
                } else {
                    da2->cursoron = 0;
                }
                if (!(da2->blink & (0x08 - 1))) /* force redrawing for cursor and blink attribute */
                    da2->fullchange = 3;
            }
            da2->blink++;

            for (x = 0; x <= (DA2_MASK_VRAMPLANE >> 9); x++) {
                if (da2->changedvram[x])
                    da2->changedvram[x]--;
            }
            // memset(changedvram,0,2048);  del
            if (da2->fullchange) {
                da2->fullchange--;
                // pclog("fc %d %d\n",da2->fullchange,da2->oddeven);
            }
        }
        if (da2->vc == da2->vsyncstart) {
            int wx, wy;
            // da2_log("VC vsync  %i %i\n", da2->firstline_draw, da2->lastline_draw);
            da2->dispon = 0;
            da2->cgastat |= 8;
            x = da2->hdisp;

            if (!da2->oddeven)
                da2->lastline++;
            if (da2->oddeven)
                da2->firstline--;

            wx = x;
            wy = da2->lastline - da2->firstline;

            da2_doblit(wx, wy, da2);

            da2->firstline = 2000;
            da2->lastline  = 0;

            da2->firstline_draw = 2000;
            da2->lastline_draw  = 0;

            da2->oddeven ^= 1;

            da2->vslines     = 0;

            da2->memaddr
                = da2->memaddr_backup = da2->memaddr_latch << 1;
            da2->cursoraddr           = ((da2->crtc[LC_CURSOR_LOC_HIGH] << 8) | da2->crtc[LC_CURSOR_LOC_LOWJ]) + da2->ca_adj;
            da2->cursoraddr <<= 1;

            // da2_log("Addr %08X vson %03X vsoff %01X\n",da2->memaddr,da2->vsyncstart,da2->crtc[0x11]&0xF);
        }
        if (da2->vc == da2->vtotal) {
            // da2_log("VC vtotal\n");
            // printf("Frame over at line %i %i  %i %i\n",displine,vc,da2_vsyncstart,da2_dispend);
            da2->vc          = 0;
            da2->scanline    = da2->crtc[LC_PRESET_ROW_SCANJ] & 0x1f;
            da2->dispon      = 1;
            da2->displine    = 0;
            da2->scrollcache = da2->attrc[LV_PANNING] & 7;
        }
        if (da2->scanline == (da2->crtc[LC_CURSOR_ROW_START] & 31))
            da2->cursorvisible = 1;
    }
}

static void
da2_video_load_font(char *fname, void *priv)
{
    da2_t  *da2 = (da2_t *) priv;
    uint8_t buf;
    uint64_t fsize;
    if (!fname)
        return;
    if (*fname == '\0')
        return;
    FILE *mfile = rom_fopen(fname, "rb");
    if (!mfile) {
        // da2_log("MSG: Can't open binary ROM font file: %s\n", fname);
        return;
    }
    fseek(mfile, 0, SEEK_END);
    fsize = ftell(mfile); /* get filesize */
    fseek(mfile, 0, SEEK_SET);
    if (fsize > DA2_FONTROM_SIZE) {
        fsize = DA2_FONTROM_SIZE; /* truncate read data */
        // da2_log("MSG: The binary ROM font is truncated: %s\n", fname);
        // fclose(mfile);
        // return 1;
    }
    uint32_t j = 0;
    while (ftell(mfile) < fsize) {
        (void) !fread(&buf, sizeof(uint8_t), 1, mfile);
        da2->mmio.font[j] = buf;
        j++;
    }
    fclose(mfile);
    return;
}

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

static void
da2_reset_ioctl(da2_t *da2)
{
    da2->ioctl[LS_RESET] = 0x00;     /* Bit 0: Reset sequencer */
    da2_outw(LS_INDEX, 0x0302, da2); /* Index 02, Bit 1: VGA passthrough, Bit 0: Character Mode */
    da2_outw(LS_INDEX, 0x0008, da2); /* Index 08, Bit 0: Enable MMIO */
}

static void
da2_reset(void *priv)
{
    da2_t *da2 = (da2_t *) priv;

    /* Initialize drawing */
    da2->bitblt.exec = DA2_BLT_CIDLE;
    da2_reset_ioctl(da2);

    da2->pos_regs[0]       = DA2_POSID_L;                    /* Adapter Identification Byte (Low byte) */
    da2->pos_regs[1]       = DA2_POSID_H;                    /* Adapter Identification Byte (High byte) */
    da2->pos_regs[2]       = 0x40;                           /* Bit 7-5: 010=Mono, 100=Color, Bit 0 : Card Enable (set by reference diskette) */
    da2->ioctl[LS_CONFIG1] = OldLSI | Page_Two;              /* Configuration 1 : DA-II, 1024 KB */
    da2->ioctl[LS_CONFIG1] |= ((da2->monitorid & 0x8) << 1); /* Configuration 1 : Monitor ID 3 */
    da2->ioctl[LS_CONFIG2]         = (da2->monitorid & 0x7); /* Configuration 2: Monitor ID 0-2 */
    da2->fctl[0]                   = 0x2b;                   /* 3E3h:0 */
    da2->fctl[LF_MMIO_MODE]        = 0xb0;                   /* 3E3h:0bh */
    da2->attrc[LV_CURSOR_COLOR]    = 0x0f;                   /* cursor color */
    da2->crtc[LC_HORIZONTAL_TOTAL] = 63;                     /* Horizontal Total */
    da2->crtc[LC_VERTICAL_TOTALJ]  = 255;                    /* Vertical Total (These two must be set before the timer starts.) */
    da2->memaddr_latch             = 0;
    da2->oddeven                   = 0;
    da2->attrc[LV_CURSOR_CONTROL]  = 0x13; /* cursor options */
    da2->attr_palette_enable       = 0;    /* disable attribute generator */

    /* Set default color palette (Windows 3.1 display driver won't reset palette) */
    for (uint16_t i = 0; i < 256; i++) {
        da2->vgapal[i].r = ps55_palette_color[i & 0x3F][0];
        da2->vgapal[i].g = ps55_palette_color[i & 0x3F][1];
        da2->vgapal[i].b = ps55_palette_color[i & 0x3F][2];
        da2->pallook[i]  = makecol32((da2->vgapal[i].r & 0x3f) * 4, (da2->vgapal[i].g & 0x3f) * 4, (da2->vgapal[i].b & 0x3f) * 4);
    }
    da2_log("da2_reset done.\n");
}

static void *
da2_init(UNUSED(const device_t *info))
{
    if (svga_get_pri() == NULL)
        return NULL;
    svga_t *mb_vga          = svga_get_pri();
    mb_vga->cable_connected = 0;

    da2_t *da2  = calloc(1, sizeof(da2_t));
    da2->mb_vga = mb_vga;

    da2->dispontime        = 1000ull << 32;
    da2->dispofftime       = 1000ull << 32;
    da2->vram              = calloc(1, DA2_SIZE_VRAM);
    da2->cram              = calloc(1, DA2_SIZE_CRAM);
    da2->vram_display_mask = DA2_MASK_CRAM;
    da2->monitorid         = device_get_config_int("montype");       
    da2->changedvram       = calloc(1,  (DA2_MASK_VRAMPLANE + 1) >> 9); /* XX000h */

    da2->mmio.charset = device_get_config_int("charset");
    da2->mmio.font    = malloc(DA2_FONTROM_SIZE);
    switch (da2->mmio.charset) {
        case DA2_DCONFIG_CHARSET_HANT:
            da2_video_load_font(DA2_FONTROM_PATH_HANT, da2);
            break;
        case DA2_DCONFIG_CHARSET_JPAN:
            da2_video_load_font(DA2_FONTROM_PATH_JPAN, da2);
            /* Add magic code for OS/2 J1.3. This disables BitBlt's text drawing function. */
            da2->mmio.font[0x1AFFE] = 0x80;
            da2->mmio.font[0x1AFFF] = 0x01;
            break;
    }

    mca_add(da2_mca_read, da2_mca_write, da2_mca_feedb, da2_mca_reset, da2);
    da2->da2const = (uint64_t) ((cpuclock / DA2_PIXELCLOCK) * (double) (1ull << 32));
    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_da2_mca);
    memset(da2->bitblt.payload, 0x00, DA2_BLT_MEMSIZE);
    memset(da2->bitblt.reg, 0xfe, DA2_BLT_REGSIZE * sizeof(uint32_t)); /* clear memory */
#ifdef ENABLE_DA2_DEBUGBLT
    da2->bitblt.debug_reg    = malloc(DA2_DEBUG_BLTLOG_MAX * DA2_DEBUG_BLTLOG_SIZE);
    da2->bitblt.debug_reg_ip = 0;
#endif
#ifdef ENABLE_DA2_DEBUGVRAM
    da2->mmdbg_fp            = fopen("da2_mmiowdat.txt", "w");
    da2->mmrdbg_fp           = fopen("da2_mmiordat.txt", "w");
#endif
    da2->bitblt.payload_addr = 0;

    timer_add(&da2->timer_vidupd, da2_updatevidselector_tick, da2, 0);/* Init timer before executing reset */
    da2_reset(da2);

    mem_mapping_add(&da2->mmio.mapping, 0xA0000, 0x20000, da2_mmio_read, da2_mmio_readw, NULL, da2_mmio_write, da2_mmio_writew, NULL, NULL, MEM_MAPPING_EXTERNAL, da2);
    // da2_log("DA2mmio new mapping: %X, base: %x, size: %x\n", &da2->mmio.mapping, da2->mmio.mapping.base, da2->mmio.mapping.size);

    mem_mapping_disable(&da2->mmio.mapping);

    mem_mapping_add(&da2->cmapping, 0xE0000, 0x1000, da2_code_readb, da2_code_readw, NULL, da2_code_writeb, da2_code_writew, NULL, NULL, MEM_MAPPING_EXTERNAL, da2);

    mem_mapping_disable(&da2->cmapping);

    timer_add(&da2->timer, da2_poll, da2, 0);
    da2->bitblt.timerspeed = 5ull * TIMER_USEC; /* Bitblt execution speed */
    timer_add(&da2->bitblt.timer, da2_bitblt_dopayload, da2, 0);

    return da2;
}
static int
da2_available(void)
{
    return (rom_present(DA2_FONTROM_PATH_HANT) || rom_present(DA2_FONTROM_PATH_JPAN));
}

static void
da2_close(void *priv)
{
    da2_t *da2 = (da2_t *) priv;

    /* dump mem for debug */
#ifdef ENABLE_DA2_LOG
    FILE *fp;
    fp = fopen("da2_cram.dmp", "wb");
    if (fp != NULL) {
        fwrite(da2->cram, DA2_SIZE_CRAM, 1, fp);
        fclose(fp);
    }
    fp = fopen("da2_vram.dmp", "wb");
    if (fp != NULL) {
        fwrite(da2->vram, DA2_SIZE_VRAM, 1, fp);
        fclose(fp);
    }
    fp = fopen("da2_gram.dmp", "wb");
    if (fp != NULL) {
        fwrite(da2->mmio.ram, DA2_SIZE_GAIJIRAM, 1, fp);
        fclose(fp);
    }
    fp = fopen("da2_attrpal.dmp", "wb");
    if (fp != NULL) {
        fwrite(da2->attrc, 32, 1, fp);
        fclose(fp);
    }
    fp = fopen("da2_dacrgb.dmp", "wb");
    if (fp != NULL) {
        fwrite(da2->vgapal, 3 * 256, 1, fp);
        fclose(fp);
    }
    fp = fopen("da2_daregs.txt", "w");
    if (fp != NULL) {
        for (uint8_t i = 0; i < 0x10; i++)
            fprintf(fp, "3e1(ioctl) %02X: %4X\n", i, da2->ioctl[i]);
        for (uint8_t i = 0; i < 0x20; i++)
            fprintf(fp, "3e3(fctl)  %02X: %4X\n", i, da2->fctl[i]);
        for (uint8_t i = 0; i < 0x20; i++)
            fprintf(fp, "3e5(crtc)  %02X: %4X\n", i, da2->crtc[i]);
        for (uint8_t i = 0; i < 0x40; i++)
            fprintf(fp, "3e8(attr)  %02X: %4X\n", i, da2->attrc[i]);
        for (uint8_t i = 0; i < 0x10; i++)
            fprintf(fp, "3eb(gcr)   %02X: %4X\n", i, da2->gdcreg[i]);
        for (uint8_t i = 0; i < 0x10; i++)
            fprintf(fp, "3ee(?)     %02X: %4X\n", i, da2->reg3ee[i]);
        for (uint8_t i = 0; i < 0x20; i++) {
            fprintf(fp, "vp         %02X: %4X %4X %4X %4X\n", i, 
                da2->crtc_vpreg[0 + i], da2->crtc_vpreg[0x20 + i], da2->crtc_vpreg[0x40 + i], da2->crtc_vpreg[0x60 + i]);
        }
        fclose(fp);
    }
    fp = fopen("ram_low.dmp", "wb");
    if (fp != NULL) {
        fwrite(&ram[0x0], 0x100000, 1, fp);
        fclose(fp);
    }
    pclog("closed %04X:%04X DS %04X\n", cs >> 4, cpu_state.pc, DS);
#endif
#ifdef ENABLE_DA2_DEBUGBLT
    fp = fopen("da2_bltdump.csv", "w");
    if (fp != NULL && da2->bitblt.debug_reg_ip > 0) {
        /* print header */
        for (int y = 0; y < DA2_DEBUG_BLTLOG_SIZE; y++) {
            if (da2->bitblt.debug_reg[(da2->bitblt.debug_reg_ip - 1) * DA2_DEBUG_BLTLOG_SIZE + y] != DA2_DEBUG_BLT_NEVERUSED)
                fprintf(fp, "\"%02X\"\t", y);
        }
        fprintf(fp, "\n");
        /* print data */
        for (int x = 0; x < da2->bitblt.debug_reg_ip; x++) {
            for (int y = 0; y < DA2_DEBUG_BLTLOG_SIZE; y++) {
                if (da2->bitblt.debug_reg[x * DA2_DEBUG_BLTLOG_SIZE + y] == DA2_DEBUG_BLT_NEVERUSED)
                    ;
                else if (da2->bitblt.debug_reg[x * DA2_DEBUG_BLTLOG_SIZE + y] == DA2_DEBUG_BLT_USEDRESET)
                    fprintf(fp, "\"\"\t");
                else {
                    fprintf(fp, "\"%X\"\t", da2->bitblt.debug_reg[x * DA2_DEBUG_BLTLOG_SIZE + y]);
                    if (y == 0x12) {
                        int chr = da2->bitblt.debug_reg[x * DA2_DEBUG_BLTLOG_SIZE + 0x12];
                        if ((chr >= 0x20) && (chr < 0x7f))
                            fprintf(fp, "\"%c\"\t", chr);
                        else
                            fprintf(fp, "\"\"\t");
                    }
                }
            }
            fprintf(fp, "\n");
        }
        fclose(fp);
    }
    free(da2->bitblt.debug_reg);
#endif
#ifdef ENABLE_DA2_DEBUGVRAM
    if (da2->mmdbg_fp != NULL)
        fclose(da2->mmdbg_fp);
    if (da2->mmrdbg_fp != NULL)
        fclose(da2->mmrdbg_fp);
#endif
    free(da2->cram);
    free(da2->vram);
    free(da2->changedvram);
    free(da2->mmio.font);
    free(da2);
}

static void
da2_speed_changed(void *priv)
{
    da2_t *da2    = (da2_t *) priv;
    da2->da2const = (uint64_t) ((cpuclock / DA2_PIXELCLOCK) * (double) (1ull << 32));
    da2_recalctimings(da2);
}

static void
da2_force_redraw(void *priv)
{
    da2_t *da2      = (da2_t *) priv;
    da2->fullchange = 3;
}

static const device_config_t da2_configuration[] = {
    // clang-format off
    {
        .name        = "charset",
        .description = "Character set",
        .type        = CONFIG_SELECTION,
        .default_int = DA2_DCONFIG_CHARSET_JPAN,
        .selection   = {
            {
                .description = "932 (Japanese)",
                .value = DA2_DCONFIG_CHARSET_JPAN
            },
            {
                .description = "938 (Traditional Chinese)",
                .value = DA2_DCONFIG_CHARSET_HANT
            },
            { .description = "" }
        }
    },
    {
        .name        = "montype",
        .description = "Monitor type",
        .type        = CONFIG_SELECTION,
        .default_int = DA2_DCONFIG_MONTYPE_COLOR,
        .selection   = {
            {
                .description = "Color",
                .value = DA2_DCONFIG_MONTYPE_COLOR
            },
            {
                .description = "IBM 8515",
                .value = DA2_DCONFIG_MONTYPE_8515
            },
            {
                .description = "Grayscale",
                .value = DA2_DCONFIG_MONTYPE_MONO
            },
            { .description = "" }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ps55da2_device = {
    .name          = "IBM Display Adapter II (MCA)",
    .internal_name = "ps55da2",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = da2_init,
    .close         = da2_close,
    .reset         = da2_reset,
    .available     = da2_available,
    .speed_changed = da2_speed_changed,
    .force_redraw  = da2_force_redraw,
    .config        = da2_configuration
};

void
da2_device_add(void)
{
    if (!da2_standalone_enabled)
        return;

    if (machine_has_bus(machine, MACHINE_BUS_MCA))
        device_add(&ps55da2_device);
    else
        return;
}
