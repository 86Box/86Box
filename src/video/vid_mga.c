/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Matrox MGA graphics card emulation.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2008-2020 Sarah Walker.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdatomic.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

#define ROM_MILLENNIUM    "roms/video/matrox/matrox2064wr2.BIN"
#define ROM_MILLENNIUM_II "roms/video/matrox/matrox2164wpc.BIN"
#define ROM_MYSTIQUE      "roms/video/matrox/MYSTIQUE.VBI"
#define ROM_MYSTIQUE_220  "roms/video/matrox/Myst220_66-99mhz.vbi"
#define ROM_G100          "roms/video/matrox/productiva8mbsdr.BIN"

#define FIFO_SIZE        65536
#define FIFO_MASK        (FIFO_SIZE - 1)
#define FIFO_ENTRY_SIZE  (1 << 31)
#define FIFO_THRESHOLD   0xe000

#define WAKE_DELAY       (100 * TIMER_USEC) /* 100us */

#define FIFO_ENTRIES     (mystique->fifo_write_idx - mystique->fifo_read_idx)
#define FIFO_FULL        ((mystique->fifo_write_idx - mystique->fifo_read_idx) >= (FIFO_SIZE - 1))
#define FIFO_EMPTY       (mystique->fifo_read_idx == mystique->fifo_write_idx)

#define FIFO_TYPE        0xff000000
#define FIFO_ADDR        0x00ffffff

#define DMA_POLL_TIME_US 100 /*100us*/
#define DMA_MAX_WORDS    (20 * 14) /*280 quad words per 100us poll*/

/*These registers are also mirrored into 0x1dxx, with the mirrored versions starting
  the blitter*/
#define REG_DWGCTL       0x1c00
#define REG_MACCESS      0x1c04
#define REG_MCTLWTST     0x1c08
#define REG_ZORG         0x1c0c
#define REG_PAT0         0x1c10
#define REG_PAT1         0x1c14
#define REG_PLNWT        0x1c1c
#define REG_BCOL         0x1c20
#define REG_FCOL         0x1c24
#define REG_SRC0         0x1c30
#define REG_SRC1         0x1c34
#define REG_SRC2         0x1c38
#define REG_SRC3         0x1c3c
#define REG_XYSTRT       0x1c40
#define REG_XYEND        0x1c44
#define REG_SHIFT        0x1c50
#define REG_DMAPAD       0x1c54
#define REG_SGN          0x1c58
#define REG_LEN          0x1c5c
#define REG_AR0          0x1c60
#define REG_AR1          0x1c64
#define REG_AR2          0x1c68
#define REG_AR3          0x1c6c
#define REG_AR4          0x1c70
#define REG_AR5          0x1c74
#define REG_AR6          0x1c78
#define REG_CXBNDRY      0x1c80
#define REG_FXBNDRY      0x1c84
#define REG_YDSTLEN      0x1c88
#define REG_PITCH        0x1c8c
#define REG_YDST         0x1c90
#define REG_YDSTORG      0x1c94
#define REG_YTOP         0x1c98
#define REG_YBOT         0x1c9c
#define REG_CXLEFT       0x1ca0
#define REG_CXRIGHT      0x1ca4
#define REG_FXLEFT       0x1ca8
#define REG_FXRIGHT      0x1cac
#define REG_XDST         0x1cb0
#define REG_DR0          0x1cc0
#define REG_DR2          0x1cc8
#define REG_DR3          0x1ccc
#define REG_DR4          0x1cd0
#define REG_DR6          0x1cd8
#define REG_DR7          0x1cdc
#define REG_DR8          0x1ce0
#define REG_DR10         0x1ce8
#define REG_DR11         0x1cec
#define REG_DR12         0x1cf0
#define REG_DR14         0x1cf8
#define REG_DR15         0x1cfc

#define REG_DR0_Z32LSB      0x2c50
#define REG_DR0_Z32MSB      0x2c54
#define REG_DR2_Z32LSB      0x2c60
#define REG_DR2_Z32MSB      0x2c64
#define REG_DR3_Z32LSB      0x2c68
#define REG_DR3_Z32MSB      0x2c6c
#define REG_TEXFILTER       0x2c58

#define REG_FIFOSTATUS   0x1e10
#define REG_STATUS       0x1e14
#define REG_ICLEAR       0x1e18
#define REG_IEN          0x1e1c
#define REG_VCOUNT       0x1e20
#define REG_DMAMAP       0x1e30
#define REG_RST          0x1e40
#define REG_OPMODE       0x1e54
#define REG_PRIMADDRESS  0x1e58
#define REG_PRIMEND      0x1e5c
#define REG_DWG_INDIR_WT 0x1e80

#define REG_ATTR_IDX     0x1fc0
#define REG_ATTR_DATA    0x1fc1
#define REG_INSTS0       0x1fc2
#define REG_MISC         0x1fc2
#define REG_SEQ_IDX      0x1fc4
#define REG_SEQ_DATA     0x1fc5
#define REG_MISCREAD     0x1fcc
#define REG_GCTL_IDX     0x1fce
#define REG_GCTL_DATA    0x1fcf
#define REG_CRTC_IDX     0x1fd4
#define REG_CRTC_DATA    0x1fd5
#define REG_INSTS1       0x1fda
#define REG_CRTCEXT_IDX  0x1fde
#define REG_CRTCEXT_DATA 0x1fdf
#define REG_CACHEFLUSH   0x1fff

/*Mystique only*/
#define REG_TMR0       0x2c00
#define REG_TMR1       0x2c04
#define REG_TMR2       0x2c08
#define REG_TMR3       0x2c0c
#define REG_TMR4       0x2c10
#define REG_TMR5       0x2c14
#define REG_TMR6       0x2c18
#define REG_TMR7       0x2c1c
#define REG_TMR8       0x2c20
#define REG_TEXORG     0x2c24
#define REG_TEXWIDTH   0x2c28
#define REG_TEXHEIGHT  0x2c2c
#define REG_TEXCTL     0x2c30
#define REG_TEXTRANS   0x2c34
#define REG_SECADDRESS 0x2c40
#define REG_SECEND     0x2c44
#define REG_SOFTRAP    0x2c48
#define REG_ALPHASTART 0x2c70
#define REG_ALPHACTRL  0x2c7c
#define REG_ALPHAXINC  0x2c74
#define REG_ALPHAYINC  0x2c78
#define REG_FOGSTART   0x1cc4
#define REG_FOGXINC    0x1cd4
#define REG_FOGYINC    0x1ce4
#define REG_FOGCOL     0x1cf4

/*Mystique only*/
#define REG_PALWTADD                  0x3c00
#define REG_PALDATA                   0x3c01
#define REG_PIXRDMSK                  0x3c02
#define REG_PALRDADD                  0x3c03
#define REG_X_DATAREG                 0x3c0a
#define REG_CURPOSX                   0x3c0c
#define REG_CURPOSY                   0x3c0e

#define REG_STATUS_VSYNCSTS           (1 << 3)

#define CRTCX_R0_STARTADD_MASK        (0xf << 0)
#define CRTCX_R0_OFFSET_MASK          (3 << 4)

#define CRTCX_R1_HTOTAL8              (1 << 0)
#define CRTCX_R1_HBLKSTRT8            (1 << 1)
#define CRTCX_R1_HBLKEND6             (1 << 6)

#define CRTCX_R2_VTOTAL10             (1 << 0)
#define CRTCX_R2_VTOTAL11             (1 << 1)
#define CRTCX_R2_VDISPEND10           (1 << 2)
#define CRTCX_R2_VBLKSTR10            (1 << 3)
#define CRTCX_R2_VBLKSTR11            (1 << 4)
#define CRTCX_R2_VSYNCSTR10           (1 << 5)
#define CRTCX_R2_VSYNCSTR11           (1 << 6)
#define CRTCX_R2_LINECOMP10           (1 << 7)

#define CRTCX_R3_MGAMODE              (1 << 7)

#define XREG_XCURADDL                 0x04
#define XREG_XCURADDH                 0x05
#define XREG_XCURCTRL                 0x06

#define XREG_XCURCOL0R                0x08
#define XREG_XCURCOL0G                0x09
#define XREG_XCURCOL0B                0x0a

#define XREG_XCURCOL1R                0x0c
#define XREG_XCURCOL1G                0x0d
#define XREG_XCURCOL1B                0x0e

#define XREG_XCURCOL2R                0x10
#define XREG_XCURCOL2G                0x11
#define XREG_XCURCOL2B                0x12

#define XREG_XVREFCTRL                0x18
#define XREG_XMULCTRL                 0x19
#define XREG_XPIXCLKCTRL              0x1a
#define XREG_XGENCTRL                 0x1d
#define XREG_XMISCCTRL                0x1e

#define XREG_XGENIOCTRL               0x2a
#define XREG_XGENIODATA               0x2b

#define XREG_XSYSPLLM                 0x2c
#define XREG_XSYSPLLN                 0x2d
#define XREG_XSYSPLLP                 0x2e
#define XREG_XSYSPLLSTAT              0x2f

#define XREG_XZOOMCTRL                0x38

#define XREG_XSENSETEST               0x3a

#define XREG_XCRCREML                 0x3c
#define XREG_XCRCREMH                 0x3d
#define XREG_XCRCBITSEL               0x3e

#define XREG_XCOLKEYMSKL              0x40
#define XREG_XCOLKEYMSKH              0x41
#define XREG_XCOLKEYL                 0x42
#define XREG_XCOLKEYH                 0x43

#define XREG_XPIXPLLCM                0x4c
#define XREG_XPIXPLLCN                0x4d
#define XREG_XPIXPLLCP                0x4e
#define XREG_XPIXPLLSTAT              0x4f

#define XMISCCTRL_VGA8DAC             (1 << 3)
#define XMISCCTRL_RAMCS               (1 << 4)

#define XMULCTRL_DEPTH_MASK           (7 << 0)
#define XMULCTRL_DEPTH_8              (0 << 0)
#define XMULCTRL_DEPTH_15             (1 << 0)
#define XMULCTRL_DEPTH_16             (2 << 0)
#define XMULCTRL_DEPTH_24             (3 << 0)
#define XMULCTRL_DEPTH_32_OVERLAYED   (4 << 0)
#define XMULCTRL_DEPTH_2G8V16         (5 << 0)
#define XMULCTRL_DEPTH_G16V16         (6 << 0)
#define XMULCTRL_DEPTH_32             (7 << 0)

#define XSYSPLLSTAT_SYSLOCK           (1 << 6)

#define XPIXPLLSTAT_SYSLOCK           (1 << 6)

#define XCURCTRL_CURMODE_MASK         (3 << 0)
#define XCURCTRL_CURMODE_3COL         (1 << 0)
#define XCURCTRL_CURMODE_XGA          (2 << 0)
#define XCURCTRL_CURMODE_XWIN         (3 << 0)

#define DWGCTRL_OPCODE_MASK           (0xf << 0)
#define DWGCTRL_OPCODE_LINE_OPEN      (0x0 << 0)
#define DWGCTRL_OPCODE_AUTOLINE_OPEN  (0x1 << 0)
#define DWGCTRL_OPCODE_LINE_CLOSE     (0x2 << 0)
#define DWGCTRL_OPCODE_AUTOLINE_CLOSE (0x3 << 0)
#define DWGCTRL_OPCODE_TRAP           (0x4 << 0)
#define DWGCTRL_OPCODE_TEXTURE_TRAP   (0x6 << 0)
#define DWGCTRL_OPCODE_ILOAD_HIGH     (0x7 << 0)
#define DWGCTRL_OPCODE_BITBLT         (0x8 << 0)
#define DWGCTRL_OPCODE_ILOAD          (0x9 << 0)
#define DWGCTRL_OPCODE_IDUMP          (0xa << 0)
#define DWGCTRL_OPCODE_FBITBLT        (0xc << 0)
#define DWGCTRL_OPCODE_ILOAD_SCALE    (0xd << 0)
#define DWGCTRL_OPCODE_ILOAD_HIGHV    (0xe << 0)
#define DWGCTRL_OPCODE_ILOAD_FILTER   (0xf << 0) /* Not implemented. */
#define DWGCTRL_ATYPE_MASK            (7 << 4)
#define DWGCTRL_ATYPE_RPL             (0 << 4)
#define DWGCTRL_ATYPE_RSTR            (1 << 4)
#define DWGCTRL_ATYPE_ZI              (3 << 4)
#define DWGCTRL_ATYPE_BLK             (4 << 4)
#define DWGCTRL_ATYPE_I               (7 << 4)
#define DWGCTRL_LINEAR                (1 << 7)
#define DWGCTRL_ZMODE_MASK            (7 << 8)
#define DWGCTRL_ZMODE_NOZCMP          (0 << 8)
#define DWGCTRL_ZMODE_ZE              (2 << 8)
#define DWGCTRL_ZMODE_ZNE             (3 << 8)
#define DWGCTRL_ZMODE_ZLT             (4 << 8)
#define DWGCTRL_ZMODE_ZLTE            (5 << 8)
#define DWGCTRL_ZMODE_ZGT             (6 << 8)
#define DWGCTRL_ZMODE_ZGTE            (7 << 8)
#define DWGCTRL_SOLID                 (1 << 11)
#define DWGCTRL_ARZERO                (1 << 12)
#define DWGCTRL_SGNZERO               (1 << 13)
#define DWGCTRL_SHTZERO               (1 << 14)
#define DWGCTRL_BOP_MASK              (0xf << 16)
#define DWGCTRL_TRANS_SHIFT           (20)
#define DWGCTRL_TRANS_MASK            (0xf << DWGCTRL_TRANS_SHIFT)
#define DWGCTRL_BLTMOD_MASK           (0xf << 25)
#define DWGCTRL_BLTMOD_BMONOLEF       (0x0 << 25)
#define DWGCTRL_BLTMOD_BPLAN          (0x1 << 25)
#define DWGCTRL_BLTMOD_BFCOL          (0x2 << 25)
#define DWGCTRL_BLTMOD_BU32BGR        (0x3 << 25)
#define DWGCTRL_BLTMOD_BMONOWF        (0x4 << 25)
#define DWGCTRL_BLTMOD_BU32RGB        (0x7 << 25)
#define DWGCTRL_BLTMOD_BUYUV          (0xe << 25)
#define DWGCTRL_BLTMOD_BU24RGB        (0xf << 25)
#define DWGCTRL_PATTERN               (1 << 29)
#define DWGCTRL_TRANSC                (1 << 30)
#define BOP(x)                        ((x) << 16)

#define MACCESS_PWIDTH_MASK           (3 << 0)
#define MACCESS_PWIDTH_8              (0 << 0)
#define MACCESS_PWIDTH_16             (1 << 0)
#define MACCESS_PWIDTH_32             (2 << 0)
#define MACCESS_PWIDTH_24             (3 << 0)
#define MACCESS_ZWIDTH                (1 << 3)
#define MACCESS_FOGEN                 (1 << 26)
#define MACCESS_TLUTLOAD              (1 << 29)
#define MACCESS_NODITHER              (1 << 30)
#define MACCESS_DIT555                (1 << 31)

#define PITCH_MASK                    0xfe0
#define PITCH_YLIN                    (1 << 15)

#define SGN_SDYDXL                    (1 << 0)
#define SGN_SCANLEFT                  (1 << 0)
#define SGN_SDXL                      (1 << 1)
#define SGN_SDY                       (1 << 2)
#define SGN_SDXR                      (1 << 5)

#define DMA_ADDR_MASK                 0xfffffffc
#define DMA_MODE_MASK                 3

#define DMA_MODE_REG                  0
#define DMA_MODE_BLIT                 1
#define DMA_MODE_VECTOR               2

#define STATUS_SOFTRAPEN              (1 << 0)
#define STATUS_VSYNCPEN               (1 << 4)
#define STATUS_VLINEPEN               (1 << 5)
#define STATUS_DWGENGSTS              (1 << 16)
#define STATUS_ENDPRDMASTS            (1 << 17)

#define ICLEAR_SOFTRAPICLR            (1 << 0)
#define ICLEAR_VLINEICLR              (1 << 5)

#define IEN_SOFTRAPEN                 (1 << 0)

#define TEXCTL_TEXFORMAT_MASK         (7 << 0)
#define TEXCTL_TEXFORMAT_TW4          (0 << 0)
#define TEXCTL_TEXFORMAT_TW8          (1 << 0)
#define TEXCTL_TEXFORMAT_TW15         (2 << 0)
#define TEXCTL_TEXFORMAT_TW16         (3 << 0)
#define TEXCTL_TEXFORMAT_TW12         (4 << 0)
#define TEXCTL_PALSEL_MASK            (0xf << 4)
#define TEXCTL_TPITCH_SHIFT           (16)
#define TEXCTL_TPITCH_MASK            (7 << TEXCTL_TPITCH_SHIFT)
#define TEXCTL_TPITCHLIN              (1 << 8)
#define TEXCTL_TPITCHEXT_MASK         (0x7ff << 9)
#define TEXCTL_NPCEN                  (1 << 21)
#define TEXCTL_AZEROEXTEND            (1 << 23)
#define TEXCTL_DECALCKEY              (1 << 24)
#define TEXCTL_TAKEY                  (1 << 25)
#define TEXCTL_TAMASK                 (1 << 26)
#define TEXCTL_CLAMPV                 (1 << 27)
#define TEXCTL_CLAMPU                 (1 << 28)
#define TEXCTL_TMODULATE              (1 << 29)
#define TEXCTL_STRANS                 (1 << 30)
#define TEXCTL_ITRANS                 (1 << 31)

#define TEXHEIGHT_TH_MASK             (0x3f << 0)
#define TEXHEIGHT_THMASK_SHIFT        (18)
#define TEXHEIGHT_THMASK_MASK         (0x7ff << TEXHEIGHT_THMASK_SHIFT)

#define TEXWIDTH_TW_MASK              (0x3f << 0)
#define TEXWIDTH_TWMASK_SHIFT         (18)
#define TEXWIDTH_TWMASK_MASK          (0x7ff << TEXWIDTH_TWMASK_SHIFT)

#define TEXTRANS_TCKEY_MASK           (0xffff)
#define TEXTRANS_TKMASK_SHIFT         (16)
#define TEXTRANS_TKMASK_MASK          (0xffff << TEXTRANS_TKMASK_SHIFT)

#define DITHER_565                    0
#define DITHER_NONE_565               1
#define DITHER_555                    2
#define DITHER_NONE_555               3

/*PCI configuration registers*/
#define OPTION_INTERLEAVE (1 << 12)

enum {
    MGA_2064W,  /*Millennium*/
    MGA_1064SG, /*Mystique*/
    MGA_1164SG, /*Mystique 220*/
    MGA_2164W, /*Millennium II*/
    MGA_G100,  /*Productiva G100*/
};

enum {
    FIFO_INVALID          = (0x00 << 24),
    FIFO_WRITE_CTRL_BYTE  = (0x01 << 24),
    FIFO_WRITE_CTRL_LONG  = (0x02 << 24),
    FIFO_WRITE_ILOAD_LONG = (0x03 << 24)
};

enum {
    DMA_STATE_IDLE = 0,
    DMA_STATE_PRI,
    DMA_STATE_SEC
};

typedef struct {
    uint32_t addr_type;
    uint32_t val;
} fifo_entry_t;

typedef struct mystique_t {
    svga_t svga;

    rom_t bios_rom;

    int type;

    mem_mapping_t lfb_mapping, ctrl_mapping,
        iload_mapping;

    uint8_t int_line, xcurctrl,
        xsyspllm, xsysplln, xsyspllp,
        xgenioctrl, xgeniodata,
        xmulctrl, xgenctrl,
        xmiscctrl, xpixclkctrl,
        xvrefctrl, ien, dmamod,
        dmadatasiz, dirdatasiz,
        xcolkeymskl, xcolkeymskh,
        xcolkeyl, xcolkeyh,
        xcrcbitsel;

    uint8_t pci_slot, irq_state, pad, pad0;

    uint8_t pci_regs[256], crtcext_regs[6],
        xreg_regs[256], dmamap[16];

    int vram_size, crtcext_idx, xreg_idx, xzoomctrl;

    atomic_int busy, blitter_submit_refcount,
        blitter_submit_dma_refcount, blitter_complete_refcount,
        endprdmasts_pending, softrap_pending,
        fifo_read_idx, fifo_write_idx;

    uint32_t vram_mask, vram_mask_w, vram_mask_l,
        lfb_base, ctrl_base, iload_base,
        ma_latch_old, maccess, mctlwtst, maccess_running,
        softrap_pending_val;

    atomic_uint status;
    atomic_bool softrap_status_read;

    uint64_t blitter_time, status_time;

    pc_timer_t softrap_pending_timer, wake_timer;

    fifo_entry_t fifo[FIFO_SIZE];

    thread_t *fifo_thread;

    event_t *wake_fifo_thread, *fifo_not_full_event;

    struct {
        int m, n, p, s;
    } xpixpll[3];

    struct {
        uint8_t funcnt : 7, stylelen,
            dmamod;

        int16_t fxleft, fxright,
            xdst;

        uint16_t cxleft, cxright,
            length;

        int xoff, yoff, selline, ydst,
            length_cur, iload_rem_count, idump_end_of_line, words,
            ta_key, ta_mask, lastpix_r, lastpix_g,
            lastpix_b, highv_line, beta, dither, err, k1, k2;

        bool pattern[8][16];

        uint32_t dwgctrl, dwgctrl_running, bcol, fcol,
            pitch, plnwt, ybot, ydstorg,
            ytop, texorg, texwidth, texheight,
            texctl, textrans, zorg, ydst_lin,
            src_addr, z_base, iload_rem_data, highv_data,
            fogcol, fogxinc : 24, fogyinc : 24, fogstart : 24,
            alphactrl, alphaxinc : 24, alphayinc : 24, alphastart : 24,
            texfilter;

        uint32_t src[4], ar[7],
            dr[16], tmr[9];

        uint64_t extended_dr[4];

        struct {
            int sdydxl, scanleft, sdxl, sdy,
                sdxr;
        } sgn;
    } dwgreg;

    struct {
        uint8_t r, g, b;
    } lut[256];

    struct {
        uint16_t pos_x, pos_y,
            addr;
        uint32_t col[3];
    } cursor;

    struct {
        atomic_int pri_state, sec_state, iload_state, state;

        atomic_uint primaddress, primend, secaddress, secend,
            pri_header, sec_header,
            iload_header;

        atomic_uint words_expected;

        mutex_t *lock;
    } dma;

    uint8_t thread_run;

    void *i2c, *i2c_ddc, *ddc;
} mystique_t;

static const uint8_t trans_masks[16][16] = {
  // clang-format off
    {
        1, 1, 1, 1,
        1, 1, 1, 1,
        1, 1, 1, 1,
        1, 1, 1, 1
    },
    {
        1, 0, 1, 0,
        0, 1, 0, 1,
        1, 0, 1, 0,
        0, 1, 0, 1
    },
    {
        0, 1, 0, 1,
        1, 0, 1, 0,
        0, 1, 0, 1,
        1, 0, 1, 0
    },
    {
        1, 0, 1, 0,
        0, 0, 0, 0,
        1, 0, 1, 0,
        0, 0, 0, 0
    },
    {
        0, 1, 0, 1,
        0, 0, 0, 0,
        0, 1, 0, 1,
        0, 0, 0, 0
    },
    {
        0, 0, 0, 0,
        1, 0, 1, 0,
        0, 0, 0, 0,
        1, 0, 1, 0
    },
    {
        0, 0, 0, 0,
        0, 1, 0, 1,
        0, 0, 0, 0,
        0, 1, 0, 1
    },
    {
        1, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 0
    },
    {
        0, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 1
    },
    {
        0, 0, 0, 1,
        0, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 0, 0
    },
    {
        0, 0, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 0,
        1, 0, 0, 0
    },
    {
        0, 0, 0, 0,
        1, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 1, 0
    },
    {
        0, 1, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 1,
        0, 0, 0, 0
    },
    {
        0, 0, 0, 0,
        0, 0, 0, 1,
        0, 0, 0, 0,
        0, 1, 0, 0
    },
    {
        0, 0, 1, 0,
        0, 0, 0, 0,
        1, 0, 0, 0,
        0, 0, 0, 0
    },
    {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    }
  // clang-format on
};

static int8_t dither5[256][2][2];
static int8_t dither6[256][2][2];
static double bayer_mat[4][4] =
{
    { 0.0, 8. / 16., 2. / 16., 10. / 16.},
    { 12. / 16., 4. / 16., 14. / 16., 6. / 16.},
    { 3. / 16., 11. / 16., 1. / 16., 9. / 16.},
    { 15. / 16., 7. / 16., 13. / 16., 5. / 16.},
};

static video_timings_t timing_matrox_millennium = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 10, .read_w = 10, .read_l = 10 };
static video_timings_t timing_matrox_mystique   = { .type = VIDEO_PCI, .write_b = 4, .write_w = 4, .write_l = 4, .read_b = 10, .read_w = 10, .read_l = 10 };

static void mystique_start_blit(mystique_t *mystique);
static void mystique_update_irqs(mystique_t *mystique);

static void wake_fifo_thread(mystique_t *mystique);
static void wait_fifo_idle(mystique_t *mystique);
static void mystique_queue(mystique_t *mystique, uint32_t addr, uint32_t val, uint32_t type);

static uint8_t  mystique_readb_linear(uint32_t addr, void *priv);
static uint16_t mystique_readw_linear(uint32_t addr, void *priv);
static uint32_t mystique_readl_linear(uint32_t addr, void *priv);
static void     mystique_writeb_linear(uint32_t addr, uint8_t val, void *priv);
static void     mystique_writew_linear(uint32_t addr, uint16_t val, void *priv);
static void     mystique_writel_linear(uint32_t addr, uint32_t val, void *priv);

static void mystique_recalc_mapping(mystique_t *mystique);
static int  mystique_line_compare(svga_t *svga);

static uint8_t  mystique_iload_read_b(uint32_t addr, void *priv);
static uint32_t mystique_iload_read_l(uint32_t addr, void *priv);
static void     mystique_iload_write_b(uint32_t addr, uint8_t val, void *priv);
static void     mystique_iload_write_l(uint32_t addr, uint32_t val, void *priv);

static uint32_t blit_idump_read(mystique_t *mystique);
static void     blit_iload_write(mystique_t *mystique, uint32_t data, int size);

void
mystique_out(uint16_t addr, uint8_t val, void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;
    svga_t     *svga     = &mystique->svga;
    uint8_t     old;

    if ((((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && addr < 0x3de) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c8:
            mystique->xreg_idx = val;
            fallthrough;
        case 0x3c6:
        case 0x3c7:
        case 0x3c9:
            if (mystique->type == MGA_2064W || mystique->type == MGA_2164W) {
                tvp3026_ramdac_out(addr, 0, 0, val, svga->ramdac, svga);
                return;
            }
            break;

        case 0x3cf:
            if ((svga->gdcaddr & 15) == 6 && svga->gdcreg[6] != val) {
                svga->gdcreg[svga->gdcaddr & 15] = val;
                mystique_recalc_mapping(mystique);
                return;
            }
            break;

        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if (((svga->crtcreg & 0x3f) < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if (((svga->crtcreg & 0x3f) == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                              = svga->crtc[svga->crtcreg & 0x3f];
            svga->crtc[svga->crtcreg & 0x3f] = val;
            if (old != val) {
                if ((svga->crtcreg & 0x3f) < 0xE || (svga->crtcreg & 0x3f) > 0x10) {
                    if (((svga->crtcreg & 0x3f) == 0xc) || ((svga->crtcreg & 0x3f) == 0xd)) {
                        svga->fullchange = 3;
                        svga->ma_latch   = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                    }
                }
                if (svga->crtcreg == 0x11) {
                    if (!(val & 0x10))
                        mystique->status &= ~STATUS_VSYNCPEN;
                    mystique_update_irqs(mystique);
                }
            }
            break;

        case 0x3de:
            mystique->crtcext_idx = val;
            break;
        case 0x3df:
            if (mystique->crtcext_idx == 1)
                svga->dpms = !!(val & 0x30);
            old = mystique->crtcext_regs[mystique->crtcext_idx];
            if (mystique->crtcext_idx < 6)
                mystique->crtcext_regs[mystique->crtcext_idx] = val;

            if ((mystique->type >= MGA_1064SG) && (mystique->crtcext_idx == 0) &&
                (mystique->crtcext_regs[3] & CRTCX_R3_MGAMODE)) {
                svga->rowoffset     = svga->crtc[0x13] |
                                      ((mystique->crtcext_regs[0] & CRTCX_R0_OFFSET_MASK) << 4);

                if (!(mystique->type >= MGA_2164W))
                    svga->rowoffset <<= 1;

                svga->ma_latch      = ((mystique->crtcext_regs[0] & CRTCX_R0_STARTADD_MASK) << 16) |
                                      (svga->crtc[0xc] << 8) | svga->crtc[0xd];
                if ((mystique->pci_regs[0x41] & (OPTION_INTERLEAVE >> 8))) {
                    svga->rowoffset <<= 1;
                    svga->ma_latch <<= 1;
                }

                if (!(mystique->type >= MGA_2164W))
                    svga->ma_latch <<= 1;

                if (svga->ma_latch != mystique->ma_latch_old) {
                    if (svga->interlace && svga->oddeven)
                        svga->maback = (svga->maback - (mystique->ma_latch_old << 2)) +
                                       (svga->ma_latch << 2) + (svga->rowoffset << 1);
                    else
                        svga->maback = (svga->maback - (mystique->ma_latch_old << 2)) +
                                       (svga->ma_latch << 2);
                    mystique->ma_latch_old = svga->ma_latch;
                }
            }

            if (mystique->crtcext_idx == 4) {
                if (svga->gdcreg[6] & 0xc) {
                    /*64k banks*/
                    if (mystique->type >= MGA_2164W) {
                        svga->read_bank  = val << 16;
                        svga->write_bank = val << 16;
                    } else {
                        svga->read_bank  = (val & 0x7f) << 16;
                        svga->write_bank = (val & 0x7f) << 16;
                    }
                } else {
                    /*128k banks*/
                    if (mystique->type >= MGA_2164W) {
                        svga->read_bank  = (val & 0xfe) << 16;
                        svga->write_bank = (val & 0xfe) << 16;
                    } else {
                        svga->read_bank  = (val & 0x7e) << 16;
                        svga->write_bank = (val & 0x7e) << 16;
                    }
                }
            }
            if (!((mystique->type >= MGA_1064SG) && (mystique->crtcext_idx == 0) &&
                (mystique->crtcext_regs[3] & CRTCX_R3_MGAMODE)))
                svga_recalctimings(svga);

            break;

        default:
            break;
    }

    svga_out(addr, val, svga);
}

uint8_t
mystique_in(uint16_t addr, void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;
    svga_t     *svga     = &mystique->svga;
    uint8_t     temp     = 0xff;

    if ((((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && addr < 0x3de) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c1:
            if (svga->attraddr >= 0x15)
                temp = 0;
            else
                temp = svga->attrregs[svga->attraddr];
            break;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            if (mystique->type == MGA_2064W || mystique->type == MGA_2164W)
                temp = tvp3026_ramdac_in(addr, 0, 0, svga->ramdac, svga);
            else
                temp = svga_in(addr, svga);
            break;

        case 0x3D4:
            temp = svga->crtcreg;
            break;
        case 0x3D5:
            if ((svga->crtcreg >= 0x19 && svga->crtcreg <= 0x21) || svga->crtcreg == 0x23 || svga->crtcreg == 0x25 || svga->crtcreg >= 0x27)
                temp = 0;
            else
                temp = svga->crtc[svga->crtcreg & 0x3f];
            break;

        case 0x3de:
            temp = mystique->crtcext_idx;
            break;

        case 0x3df:
            if (mystique->crtcext_idx < 6)
                temp = mystique->crtcext_regs[mystique->crtcext_idx];
            break;

        default:
            temp = svga_in(addr, svga);
            break;
    }

    return temp;
}

static int
mystique_line_compare(svga_t *svga)
{
    mystique_t *mystique = (mystique_t *) svga->priv;

    mystique->status |= STATUS_VLINEPEN;
    mystique_update_irqs(mystique);

    return 0;
}

static void
mystique_vblank_start(svga_t *svga)
{
    mystique_t *mystique = (mystique_t *) svga->priv;

    if (mystique->crtcext_regs[3] & CRTCX_R3_MGAMODE) {
        svga->ma_latch      = ((mystique->crtcext_regs[0] & CRTCX_R0_STARTADD_MASK) << 16) | (svga->crtc[0xc] << 8) | svga->crtc[0xd];
        if (mystique->pci_regs[0x41] & (OPTION_INTERLEAVE >> 8))
            svga->ma_latch <<= 1;
    }
}

static void
mystique_vsync_callback(svga_t *svga)
{
    mystique_t *mystique = (mystique_t *) svga->priv;

    if (svga->crtc[0x11] & 0x10) {
        mystique->status |= STATUS_VSYNCPEN;
        mystique_update_irqs(mystique);
    }
}

static float
mystique_getclock(int clock, void *priv)
{
    const mystique_t *mystique = (mystique_t *) priv;

    if (clock == 0)
        return 25175000.0;
    if (clock == 1)
        return 28322000.0;

    int m  = mystique->xpixpll[2].m;
    int n  = mystique->xpixpll[2].n;
    int pl = mystique->xpixpll[2].p;

    float fvco = 14318181.0 * (n + 1) / (m + 1);
    float fo   = fvco / (pl + 1);

    return fo;
}

void
mystique_recalctimings(svga_t *svga)
{
    mystique_t *mystique = (mystique_t *) svga->priv;
    int         clk_sel  = (svga->miscout >> 2) & 3;

    svga->clock = (cpuclock * (float) (1ULL << 32)) / svga->getclock(clk_sel & 3, svga->clock_gen);

    if (mystique->crtcext_regs[1] & CRTCX_R1_HTOTAL8)
        svga->htotal |= 0x100;

    svga->hblankstart    = (((mystique->crtcext_regs[1] & 0x02) >> 2) << 8) + svga->crtc[2];

    if (mystique->crtcext_regs[2] & CRTCX_R2_VTOTAL10)
        svga->vtotal |= 0x400;
    if (mystique->crtcext_regs[2] & CRTCX_R2_VTOTAL11)
        svga->vtotal |= 0x800;
    if (mystique->crtcext_regs[2] & CRTCX_R2_VDISPEND10)
        svga->dispend |= 0x400;
    if (mystique->crtcext_regs[2] & CRTCX_R2_VBLKSTR10)
        svga->vblankstart |= 0x400;
    if (mystique->crtcext_regs[2] & CRTCX_R2_VBLKSTR11)
        svga->vblankstart |= 0x800;
    if (mystique->crtcext_regs[2] & CRTCX_R2_VSYNCSTR10)
        svga->vsyncstart |= 0x400;
    if (mystique->crtcext_regs[2] & CRTCX_R2_VSYNCSTR11)
        svga->vsyncstart |= 0x800;
    if (mystique->crtcext_regs[2] & CRTCX_R2_LINECOMP10)
        svga->split |= 0x400;

    if (mystique->type == MGA_2064W || mystique->type == MGA_2164W) {
        tvp3026_recalctimings(svga->ramdac, svga);
        svga->interlace |= !!(mystique->crtcext_regs[0] & 0x80);
    } else
        svga->interlace = !!(mystique->crtcext_regs[0] & 0x80);

    if (mystique->crtcext_regs[3] & CRTCX_R3_MGAMODE) {
        svga->lowres        = 0;
        svga->char_width    = 8;
        svga->hdisp         = (svga->crtc[1] + 1) << 3;
        svga->hdisp_time    = svga->hdisp;
        svga->rowoffset     = svga->crtc[0x13] | ((mystique->crtcext_regs[0] & CRTCX_R0_OFFSET_MASK) << 4);

        svga->dots_per_clock  = 8;
        svga->hblank_end_val  = (svga->crtc[3] & 0x1f) | (((svga->crtc[5] & 0x80) >> 7) << 5) |
                                (((mystique->crtcext_regs[1] & 0x40) >> 6) << 6);
        svga->hblank_end_mask = 0x0000007f;

        if (mystique->type != MGA_2164W && mystique->type != MGA_2064W)
            svga->lut_map = !!(mystique->xmiscctrl & XMISCCTRL_RAMCS);

        if (mystique->type >= MGA_1064SG)
            svga->ma_latch = ((mystique->crtcext_regs[0] & CRTCX_R0_STARTADD_MASK) << 16) | (svga->crtc[0xc] << 8) | svga->crtc[0xd];

        if ((mystique->pci_regs[0x41] & (OPTION_INTERLEAVE >> 8))) {
            svga->rowoffset <<= 1;
            if (mystique->type >= MGA_1064SG)
                svga->ma_latch <<= 1;
        }

        if (mystique->type >= MGA_1064SG) {
            /*Mystique and later, unlike most SVGA cards, allows display start to take
              effect mid-screen*/
            if (!(mystique->type >= MGA_2164W))
                svga->ma_latch <<= 1;
            /* Only change maback so the new display start will take effect on the next
               horizontal retrace. */
            if (svga->ma_latch != mystique->ma_latch_old) {
                if (svga->interlace && svga->oddeven)
                    svga->maback = (svga->maback - (mystique->ma_latch_old << 2)) +
                                   (svga->ma_latch << 2) + (svga->rowoffset << 1);
                else
                    svga->maback = (svga->maback - (mystique->ma_latch_old << 2)) +
                                   (svga->ma_latch << 2);
                mystique->ma_latch_old = svga->ma_latch;
            }

            if (!(mystique->type >= MGA_2164W))
                svga->rowoffset <<= 1;
            if (mystique->type != MGA_2164W) {
                switch (mystique->xmulctrl & XMULCTRL_DEPTH_MASK) {
                    case XMULCTRL_DEPTH_8:
                    case XMULCTRL_DEPTH_2G8V16:
                        svga->render = svga_render_8bpp_highres;
                        svga->bpp    = 8;
                        break;
                    case XMULCTRL_DEPTH_15:
                    case XMULCTRL_DEPTH_G16V16:
                        svga->render = svga_render_15bpp_highres;
                        svga->bpp    = 15;
                        break;
                    case XMULCTRL_DEPTH_16:
                        svga->render = svga_render_16bpp_highres;
                        svga->bpp    = 16;
                        break;
                    case XMULCTRL_DEPTH_24:
                        svga->render = svga_render_24bpp_highres;
                        svga->bpp    = 24;
                        break;
                    case XMULCTRL_DEPTH_32:
                    case XMULCTRL_DEPTH_32_OVERLAYED:
                        svga->render = svga_render_32bpp_highres;
                        svga->bpp    = 32;
                        break;

                    default:
                        break;
                }
            } else {
                switch (svga->bpp) {
                    case 8:
                        svga->render = svga_render_8bpp_highres;
                        break;
                    case 15:
                        svga->render = svga_render_15bpp_highres;
                        break;
                    case 16:
                        svga->render = svga_render_16bpp_highres;
                        break;
                    case 24:
                        svga->render = svga_render_24bpp_highres;
                        break;
                    case 32:
                        svga->render = svga_render_32bpp_highres;
                        break;
                }
            }
        } else {
            switch (svga->bpp) {
                case 8:
                    svga->render = svga_render_8bpp_highres;
                    break;
                case 15:
                    svga->render = svga_render_15bpp_highres;
                    break;
                case 16:
                    svga->render = svga_render_16bpp_highres;
                    break;
                case 24:
                    svga->render = svga_render_24bpp_highres;
                    break;
                case 32:
                    svga->render = svga_render_32bpp_highres;
                    break;
            }
        }
        svga->packed_chain4 = 1;
        svga->line_compare = mystique_line_compare;
        if (mystique->type < MGA_1064SG)
            svga->vblank_start = mystique_vblank_start;
    } else {
        svga->packed_chain4 = 0;
        svga->line_compare  = NULL;
        svga->lut_map       = 0;
        if (mystique->type >= MGA_1064SG)
            svga->bpp = 8;
        else
            svga->vblank_start  = NULL;
    }

    svga->fb_only       = svga->packed_chain4;
    svga->disable_blink = (svga->bpp > 4);
    video_force_resize_set_monitor(1, svga->monitor_index);
#if 0
    pclog("PackedChain4=%d, chain4=%x, fast=%x, bit6 attrreg10=%02x, bits 5-6 gdcreg5=%02x, extmode=%02x.\n", svga->packed_chain4, svga->chain4, svga->fast, svga->attrregs[0x10] & 0x40, svga->gdcreg[5] & 0x60, mystique->pci_regs[0x41] & 1, mystique->crtcext_regs[3] & CRTCX_R3_MGAMODE);
#endif
}

static void
mystique_recalc_mapping(mystique_t *mystique)
{
    svga_t *svga = &mystique->svga;

    io_removehandler(0x03c0, 0x0020, mystique_in, NULL, NULL, mystique_out, NULL, NULL, mystique);
    if ((mystique->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) && (mystique->pci_regs[0x41] & 1))
        io_sethandler(0x03c0, 0x0020, mystique_in, NULL, NULL, mystique_out, NULL, NULL, mystique);

    if (!(mystique->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
        mem_mapping_disable(&svga->mapping);
        mem_mapping_disable(&mystique->ctrl_mapping);
        mem_mapping_disable(&mystique->lfb_mapping);
        mem_mapping_disable(&mystique->iload_mapping);
        return;
    }

    if (mystique->ctrl_base)
        mem_mapping_set_addr(&mystique->ctrl_mapping, mystique->ctrl_base, 0x4000);
    else
        mem_mapping_disable(&mystique->ctrl_mapping);

    if (mystique->lfb_base)
        mem_mapping_set_addr(&mystique->lfb_mapping, mystique->lfb_base, (mystique->type >= MGA_2164W) ? 0x1000000 : 0x800000);
    else
        mem_mapping_disable(&mystique->lfb_mapping);

    if (mystique->iload_base)
        mem_mapping_set_addr(&mystique->iload_mapping, mystique->iload_base, 0x800000);
    else
        mem_mapping_disable(&mystique->iload_mapping);

    if (mystique->pci_regs[0x41] & 1) {
        switch (svga->gdcreg[6] & 0x0C) {
            case 0x0: /*128k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
                svga->banked_mask = 0xffff;
                break;
            case 0x4: /*64k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                svga->banked_mask = 0xffff;
                break;
            case 0x8: /*32k at B0000*/
                mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
                svga->banked_mask = 0x7fff;
                break;
            case 0xC: /*32k at B8000*/
                mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
                svga->banked_mask = 0x7fff;
                break;

            default:
                break;
        }
        if (svga->gdcreg[6] & 0xc) {
            /*64k banks*/
            svga->read_bank  = (mystique->crtcext_regs[4] & 0x7f) << 16;
            svga->write_bank = (mystique->crtcext_regs[4] & 0x7f) << 16;
        } else {
            /*128k banks*/
            svga->read_bank  = (mystique->crtcext_regs[4] & 0x7e) << 16;
            svga->write_bank = (mystique->crtcext_regs[4] & 0x7e) << 16;
        }
    } else
        mem_mapping_disable(&svga->mapping);
}

static void
mystique_update_irqs(mystique_t *mystique)
{
    const svga_t *svga = &mystique->svga;
    int           irq  = 0;

    if ((mystique->status & mystique->ien) & STATUS_SOFTRAPEN)
        irq = 1;
    if ((mystique->status & mystique->ien) & STATUS_VLINEPEN)
        irq = 1;
    if ((mystique->status & STATUS_VSYNCPEN) && (svga->crtc[0x11] & 0x30) == 0x10)
        irq = 1;

    if (irq)
        pci_set_irq(mystique->pci_slot, PCI_INTA, &mystique->irq_state);
    else
        pci_clear_irq(mystique->pci_slot, PCI_INTA, &mystique->irq_state);
}

#define READ8(addr, var)                \
    switch ((addr) &3) {                \
        case 0:                         \
            ret = (var) &0xff;          \
            break;                      \
        case 1:                         \
            ret = ((var) >> 8) & 0xff;  \
            break;                      \
        case 2:                         \
            ret = ((var) >> 16) & 0xff; \
            break;                      \
        case 3:                         \
            ret = ((var) >> 24) & 0xff; \
            break;                      \
    }

#define WRITE8(addr, var, val)                        \
    switch ((addr) &3) {                              \
        case 0:                                       \
            var = (var & 0xffffff00) | (val);         \
            break;                                    \
        case 1:                                       \
            var = (var & 0xffff00ff) | ((val) << 8);  \
            break;                                    \
        case 2:                                       \
            var = (var & 0xff00ffff) | ((val) << 16); \
            break;                                    \
        case 3:                                       \
            var = (var & 0x00ffffff) | ((val) << 24); \
            break;                                    \
    }

static uint8_t
mystique_read_xreg(mystique_t *mystique, int reg)
{
    uint8_t ret = 0xff;

    switch (reg) {
        case XREG_XCURADDL:
            ret = mystique->cursor.addr & 0xff;
            break;
        case XREG_XCURADDH:
            ret = mystique->cursor.addr >> 8;
            break;
        case XREG_XCURCTRL:
            ret = mystique->xcurctrl;
            break;

        case XREG_XCURCOL0R:
        case XREG_XCURCOL0G:
        case XREG_XCURCOL0B:
            READ8(reg, mystique->cursor.col[0]);
            break;
        case XREG_XCURCOL1R:
        case XREG_XCURCOL1G:
        case XREG_XCURCOL1B:
            READ8(reg, mystique->cursor.col[1]);
            break;
        case XREG_XCURCOL2R:
        case XREG_XCURCOL2G:
        case XREG_XCURCOL2B:
            READ8(reg, mystique->cursor.col[2]);
            break;

        case XREG_XMULCTRL:
            ret = mystique->xmulctrl;
            break;

        case XREG_XMISCCTRL:
            ret = mystique->xmiscctrl;
            break;

        case XREG_XGENCTRL:
            ret = mystique->xgenctrl;
            break;

        case XREG_XVREFCTRL:
            ret = mystique->xvrefctrl;
            break;

        case XREG_XGENIOCTRL:
            ret = mystique->xgenioctrl;
            break;
        case XREG_XGENIODATA:
            ret = mystique->xgeniodata & 0xf0;
            if (i2c_gpio_get_scl(mystique->i2c_ddc))
                ret |= 0x08;
            if (i2c_gpio_get_scl(mystique->i2c))
                ret |= 0x04;
            if (i2c_gpio_get_sda(mystique->i2c_ddc))
                ret |= 0x02;
            if (i2c_gpio_get_sda(mystique->i2c))
                ret |= 0x01;
            break;

        case XREG_XSYSPLLM:
            ret = mystique->xsyspllm;
            break;
        case XREG_XSYSPLLN:
            ret = mystique->xsysplln;
            break;
        case XREG_XSYSPLLP:
            ret = mystique->xsyspllp;
            break;

        case XREG_XZOOMCTRL:
            ret = mystique->xzoomctrl;
            break;

        case XREG_XSENSETEST:
            ret = 0;
            if (mystique->svga.vgapal[0].b < 0x80)
                ret |= 1;
            if (mystique->svga.vgapal[0].g < 0x80)
                ret |= 2;
            if (mystique->svga.vgapal[0].r < 0x80)
                ret |= 4;
            break;

        case XREG_XCRCREML: /*CRC not implemented*/
            ret = 0;
            break;
        case XREG_XCRCREMH:
            ret = 0;
            break;
        case XREG_XCRCBITSEL:
            ret = mystique->xcrcbitsel;
            break;

        case XREG_XCOLKEYMSKL:
            ret = mystique->xcolkeymskl;
            break;
        case XREG_XCOLKEYMSKH:
            ret = mystique->xcolkeymskh;
            break;
        case XREG_XCOLKEYL:
            ret = mystique->xcolkeyl;
            break;
        case XREG_XCOLKEYH:
            ret = mystique->xcolkeyh;
            break;

        case XREG_XPIXCLKCTRL:
            ret = mystique->xpixclkctrl;
            break;

        case XREG_XSYSPLLSTAT:
            ret = XSYSPLLSTAT_SYSLOCK;
            break;

        case XREG_XPIXPLLSTAT:
            ret = XPIXPLLSTAT_SYSLOCK;
            break;

        case XREG_XPIXPLLCM:
            ret = mystique->xpixpll[2].m;
            break;
        case XREG_XPIXPLLCN:
            ret = mystique->xpixpll[2].n;
            break;
        case XREG_XPIXPLLCP:
            ret = mystique->xpixpll[2].p | (mystique->xpixpll[2].s << 3);
            break;

        case 0x00:
        case 0x20:
        case 0x3f:
            ret = 0xff;
            break;

        default:
            if (reg >= 0x50)
                ret = 0xff;
            break;
    }

    return ret;
}

static void
mystique_write_xreg(mystique_t *mystique, int reg, uint8_t val)
{
    svga_t *svga = &mystique->svga;

    switch (reg) {
        case XREG_XCURADDL:
            mystique->cursor.addr = (mystique->cursor.addr & 0x1f00) | val;
            svga->hwcursor.addr   = mystique->cursor.addr << 10;
            break;
        case XREG_XCURADDH:
            mystique->cursor.addr = (mystique->cursor.addr & 0x00ff) | ((val & 0x1f) << 8);
            svga->hwcursor.addr   = mystique->cursor.addr << 10;
            break;

        case XREG_XCURCTRL:
            mystique->xcurctrl = val;
            svga->hwcursor.ena = (val & 3) ? 1 : 0;
            break;

        case XREG_XCURCOL0R:
        case XREG_XCURCOL0G:
        case XREG_XCURCOL0B:
            WRITE8(reg, mystique->cursor.col[0], val);
            break;
        case XREG_XCURCOL1R:
        case XREG_XCURCOL1G:
        case XREG_XCURCOL1B:
            WRITE8(reg, mystique->cursor.col[1], val);
            break;
        case XREG_XCURCOL2R:
        case XREG_XCURCOL2G:
        case XREG_XCURCOL2B:
            WRITE8(reg, mystique->cursor.col[2], val);
            break;

        case XREG_XMULCTRL:
            mystique->xmulctrl = val;
            break;

        case XREG_XMISCCTRL:
            mystique->xmiscctrl = val;
            svga_set_ramdac_type(svga, (val & XMISCCTRL_VGA8DAC) ? RAMDAC_8BIT : RAMDAC_6BIT);
            if (mystique->crtcext_regs[3] & CRTCX_R3_MGAMODE)
                svga->lut_map       = !!(mystique->xmiscctrl & XMISCCTRL_RAMCS);
            break;

        case XREG_XGENCTRL:
            mystique->xgenctrl = val;
            break;

        case XREG_XVREFCTRL:
            mystique->xvrefctrl = val;
            break;

        case XREG_XGENIOCTRL:
            mystique->xgenioctrl = val;
            i2c_gpio_set(mystique->i2c_ddc, !(mystique->xgenioctrl & 0x08) || (mystique->xgeniodata & 0x08), !(mystique->xgenioctrl & 0x02) || (mystique->xgeniodata & 0x02));
            i2c_gpio_set(mystique->i2c, !(mystique->xgenioctrl & 0x04) || (mystique->xgeniodata & 0x04), !(mystique->xgenioctrl & 0x01) || (mystique->xgeniodata & 0x01));
            break;
        case XREG_XGENIODATA:
            mystique->xgeniodata = val;
            break;

        case XREG_XSYSPLLM:
            mystique->xsyspllm = val;
            break;
        case XREG_XSYSPLLN:
            mystique->xsysplln = val;
            break;
        case XREG_XSYSPLLP:
            mystique->xsyspllp = val;
            break;

        case XREG_XZOOMCTRL:
            mystique->xzoomctrl = val & 3;
            break;

        case XREG_XSENSETEST:
            break;

        case XREG_XCRCREML: /*CRC not implemented*/
            break;
        case XREG_XCRCREMH:
            break;
        case XREG_XCRCBITSEL:
            mystique->xcrcbitsel = val & 0x1f;
            break;

        case XREG_XCOLKEYMSKL:
            mystique->xcolkeymskl = val;
            break;
        case XREG_XCOLKEYMSKH:
            mystique->xcolkeymskh = val;
            break;
        case XREG_XCOLKEYL:
            mystique->xcolkeyl = val;
            break;
        case XREG_XCOLKEYH:
            mystique->xcolkeyh = val;
            break;

        case XREG_XSYSPLLSTAT:
            break;

        case XREG_XPIXPLLSTAT:
            break;

        case XREG_XPIXCLKCTRL:
            mystique->xpixclkctrl = val;
            break;

        case XREG_XPIXPLLCM:
            mystique->xpixpll[2].m = val;
            break;
        case XREG_XPIXPLLCN:
            mystique->xpixpll[2].n = val;
            break;
        case XREG_XPIXPLLCP:
            mystique->xpixpll[2].p = val & 7;
            mystique->xpixpll[2].s = (val >> 3) & 3;
            break;

        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x07:
        case 0x0b:
        case 0x0f:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x1b:
        case 0x1c:
        case 0x20:
        case 0x39:
        case 0x3b:
        case 0x3f:
        case 0x47:
        case 0x4b:
            break;

        default:
            break;
    }
}

static uint8_t
mystique_ctrl_read_b(uint32_t addr, void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;
    svga_t     *svga     = &mystique->svga;
    uint8_t     ret      = 0xff;
    int         fifocount;
    uint8_t     addr_0x0f = 0;
    uint16_t    addr_0x03 = 0;
    int         rs2 = 0;
    int         rs3 = 0;

    if ((mystique->type == MGA_2064W || mystique->type == MGA_2164W) && (addr & 0x3e00) == 0x3c00) {
        /*RAMDAC*/
        addr_0x0f = addr & 0x0f;

        if ((addr_0x0f & 3) == 0)
            addr_0x03 = 0x3c8;
        else if ((addr_0x0f & 3) == 1)
            addr_0x03 = 0x3c9;
        else if ((addr_0x0f & 3) == 2)
            addr_0x03 = 0x3c6;
        else if ((addr_0x0f & 3) == 3)
            addr_0x03 = 0x3c7;

        if ((addr_0x0f >= 0x04) && (addr_0x0f <= 0x07)) {
            rs2 = 1;
            rs3 = 0;
        } else if ((addr_0x0f >= 0x08) && (addr_0x0f <= 0x0b)) {
            rs2 = 0;
            rs3 = 1;
        } else if ((addr_0x0f >= 0x0c) && (addr_0x0f <= 0x0f)) {
            rs2 = 1;
            rs3 = 1;
        }

        ret = tvp3026_ramdac_in(addr_0x03, rs2, rs3, svga->ramdac, svga);
    } else
        switch (addr & 0x3fff) {
            case REG_FIFOSTATUS:
                fifocount = FIFO_SIZE - FIFO_ENTRIES;
                if (fifocount > (mystique->type <= MGA_1064SG ? 32 : 64))
                    fifocount = (mystique->type <= MGA_1064SG ? 32 : 64);
                ret = fifocount;
                break;
            case REG_FIFOSTATUS + 1:
                if (FIFO_EMPTY)
                    ret |= 2;
                else if (FIFO_ENTRIES >= (mystique->type <= MGA_1064SG ? 32 : 64))
                    ret |= 1;
                break;
            case REG_FIFOSTATUS + 2:
            case REG_FIFOSTATUS + 3:
                ret = 0;
                break;

            case REG_STATUS:
                ret = mystique->status & 0xff;
                if (svga->cgastat & 8)
                    ret |= REG_STATUS_VSYNCSTS;
                if (ret & 1)
                    mystique->softrap_status_read = 1;
                if (mystique->softrap_status_read == 0 && !(ret & 1)) {
                    mystique->softrap_status_read = 1;
                    ret |= 1;
                }
                break;
            case REG_STATUS + 1:
                ret = (mystique->status >> 8) & 0xff;
                break;
            case REG_STATUS + 2:
                ret = (mystique->status >> 16) & 0xff;
                if (mystique->busy || ((mystique->blitter_submit_refcount + mystique->blitter_submit_dma_refcount) != mystique->blitter_complete_refcount) || !FIFO_EMPTY
                || mystique->dma.state != DMA_STATE_IDLE || mystique->softrap_pending || mystique->endprdmasts_pending)
                    ret |= (STATUS_DWGENGSTS >> 16);
                break;
            case REG_STATUS + 3:
                ret = (mystique->status >> 24) & 0xff;
                break;

            case REG_IEN:
                ret = mystique->ien & 0x65;
                break;
            case REG_IEN + 1:
            case REG_IEN + 2:
            case REG_IEN + 3:
                ret = 0;
                break;

            case REG_OPMODE:
                ret = mystique->dmamod << 2;
                break;
            case REG_OPMODE + 1:
                ret = mystique->dmadatasiz;
                break;
            case REG_OPMODE + 2:
                ret = mystique->dirdatasiz;
                break;
            case REG_OPMODE + 3:
                ret = 0;
                break;

            case REG_PRIMADDRESS:
            case REG_PRIMADDRESS + 1:
            case REG_PRIMADDRESS + 2:
            case REG_PRIMADDRESS + 3:
                READ8(addr, mystique->dma.primaddress);
                break;
            case REG_PRIMEND:
            case REG_PRIMEND + 1:
            case REG_PRIMEND + 2:
            case REG_PRIMEND + 3:
                READ8(addr, mystique->dma.primend);
                break;

            case REG_SECADDRESS:
            case REG_SECADDRESS + 1:
            case REG_SECADDRESS + 2:
            case REG_SECADDRESS + 3:
                READ8(addr, mystique->dma.secaddress);
                break;

            case REG_VCOUNT:
            case REG_VCOUNT + 1:
            case REG_VCOUNT + 2:
            case REG_VCOUNT + 3:
                READ8(addr, svga->vc);
                break;

            case REG_ATTR_IDX:
                ret = svga_in(0x3c0, svga);
                break;
            case REG_ATTR_DATA:
                ret = svga_in(0x3c1, svga);
                break;

            case REG_INSTS0:
                ret = svga_in(0x3c2, svga);
                break;

            case REG_SEQ_IDX:
                ret = svga_in(0x3c4, svga);
                break;
            case REG_SEQ_DATA:
                ret = svga_in(0x3c5, svga);
                break;

            case REG_MISCREAD:
                ret = svga_in(0x3cc, svga);
                break;

            case REG_GCTL_IDX:
                ret = mystique_in(0x3ce, mystique);
                break;
            case REG_GCTL_DATA:
                ret = mystique_in(0x3cf, mystique);
                break;

            case REG_CRTC_IDX:
                ret = mystique_in(0x3d4, mystique);
                break;
            case REG_CRTC_DATA:
                ret = mystique_in(0x3d5, mystique);
                break;

            case REG_INSTS1:
                ret = mystique_in(0x3da, mystique);
                break;

            case REG_CRTCEXT_IDX:
                ret = mystique_in(0x3de, mystique);
                break;
            case REG_CRTCEXT_DATA:
                ret = mystique_in(0x3df, mystique);
                break;

            case REG_PALWTADD:
                ret = svga_in(0x3c8, svga);
                break;
            case REG_PALDATA:
                ret = svga_in(0x3c9, svga);
                break;
            case REG_PIXRDMSK:
                ret = svga_in(0x3c6, svga);
                break;
            case REG_PALRDADD:
                ret = svga_in(0x3c7, svga);
                break;

            case REG_X_DATAREG:
                ret = mystique_read_xreg(mystique, mystique->xreg_idx);
                break;

            case 0x1c40:
            case 0x1c41:
            case 0x1c42:
            case 0x1c43:
            case 0x1d44:
            case 0x1d45:
            case 0x1d46:
            case 0x1d47:
            case 0x1e50:
            case 0x1e51:
            case 0x1e52:
            case 0x1e53:
            case REG_ICLEAR:
            case REG_ICLEAR + 1:
            case REG_ICLEAR + 2:
            case REG_ICLEAR + 3:
            case 0x2c30:
            case 0x2c31:
            case 0x2c32:
            case 0x2c33:
            case 0x3e08:
                break;

            case 0x3c08:
            case 0x3c09:
            case 0x3c0b:
                break;

            default:
                if ((addr & 0x3fff) >= 0x2c00 && (addr & 0x3fff) < 0x2c40)
                    break;
                if ((addr & 0x3fff) >= 0x3e00)
                    break;
                break;
        }

    return ret;
}

static void
mystique_accel_ctrl_write_b(uint32_t addr, uint8_t val, void *priv)
{
    mystique_t *mystique   = (mystique_t *) priv;
    int         start_blit = 0;

    if ((addr & 0x300) == 0x100) {
        addr &= ~0x100;
        start_blit = 1;
    }

    switch (addr & 0x3fff) {
        case REG_MACCESS:
        case REG_MACCESS + 1:
        case REG_MACCESS + 2:
        case REG_MACCESS + 3:
            WRITE8(addr, mystique->maccess, val);
            mystique->dwgreg.dither = mystique->maccess >> 30;
            if (mystique->type < MGA_2164W)
                mystique->maccess &= ~MACCESS_ZWIDTH;
            else
                mystique->dwgreg.z_base = mystique->dwgreg.ydstorg * ((mystique->maccess & MACCESS_ZWIDTH) ? 4 : 2) + mystique->dwgreg.zorg;
            break;

        case REG_MCTLWTST:
        case REG_MCTLWTST + 1:
        case REG_MCTLWTST + 2:
        case REG_MCTLWTST + 3:
            WRITE8(addr, mystique->mctlwtst, val);
            break;

        case REG_PAT0:
        case REG_PAT0 + 1:
        case REG_PAT0 + 2:
        case REG_PAT0 + 3:
        case REG_PAT1:
        case REG_PAT1 + 1:
        case REG_PAT1 + 2:
        case REG_PAT1 + 3:
            for (uint8_t x = 0; x < 8; x++)
                mystique->dwgreg.pattern[addr & 7][x] = mystique->dwgreg.pattern[addr & 7][x + 8] = val & (1 << (7 - x));
            break;

        case REG_XYSTRT:
        case REG_XYSTRT + 1:
            WRITE8(addr & 1, mystique->dwgreg.ar[5], val);
            if (mystique->dwgreg.ar[5] & 0x8000)
                mystique->dwgreg.ar[5] |= 0xffff8000;
            else
                mystique->dwgreg.ar[5] &= ~0xffff8000;
            WRITE8(addr & 1, mystique->dwgreg.xdst, val);
            break;
        case REG_XYSTRT + 2:
        case REG_XYSTRT + 3:
            WRITE8(addr & 1, mystique->dwgreg.ar[6], val);
            if (mystique->dwgreg.ar[6] & 0x8000)
                mystique->dwgreg.ar[6] |= 0xffff8000;
            else
                mystique->dwgreg.ar[6] &= ~0xffff8000;
            WRITE8(addr & 1, mystique->dwgreg.ydst, val);
            mystique->dwgreg.ydst_lin = ((int32_t) (int16_t) mystique->dwgreg.ydst * (mystique->dwgreg.pitch & PITCH_MASK)) + mystique->dwgreg.ydstorg;
            break;

        case REG_XYEND:
        case REG_XYEND + 1:
            WRITE8(addr & 1, mystique->dwgreg.ar[0], val);
            if (mystique->dwgreg.ar[0] & 0x8000)
                mystique->dwgreg.ar[0] |= 0xffff8000;
            else
                mystique->dwgreg.ar[0] &= ~0xffff8000;
            break;
        case REG_XYEND + 2:
        case REG_XYEND + 3:
            WRITE8(addr & 1, mystique->dwgreg.ar[2], val);
            if (mystique->dwgreg.ar[2] & 0x8000)
                mystique->dwgreg.ar[2] |= 0xffff8000;
            else
                mystique->dwgreg.ar[2] &= ~0xffff8000;
            break;

        case REG_SGN:
            mystique->dwgreg.sgn.sdydxl   = val & SGN_SDYDXL;
            mystique->dwgreg.sgn.scanleft = val & SGN_SCANLEFT;
            mystique->dwgreg.sgn.sdxl     = val & SGN_SDXL;
            mystique->dwgreg.sgn.sdy      = val & SGN_SDY;
            mystique->dwgreg.sgn.sdxr     = val & SGN_SDXR;
            break;
        case REG_SGN + 1:
        case REG_SGN + 2:
        case REG_SGN + 3:
            break;

        case REG_LEN:
        case REG_LEN + 1:
            WRITE8(addr, mystique->dwgreg.length, val);
            break;
        case REG_LEN + 2:
            break;
        case REG_LEN + 3:
            mystique->dwgreg.beta = val >> 4;
            if (!mystique->dwgreg.beta)
                mystique->dwgreg.beta = 16;
            break;

        case REG_CXBNDRY:
        case REG_CXBNDRY + 1:
            WRITE8(addr, mystique->dwgreg.cxleft, val);
            break;
        case REG_CXBNDRY + 2:
        case REG_CXBNDRY + 3:
            WRITE8(addr & 1, mystique->dwgreg.cxright, val);
            break;
        case REG_FXBNDRY:
        case REG_FXBNDRY + 1:
            WRITE8(addr, mystique->dwgreg.fxleft, val);
            break;
        case REG_FXBNDRY + 2:
        case REG_FXBNDRY + 3:
            WRITE8(addr & 1, mystique->dwgreg.fxright, val);
            break;

        case REG_YDSTLEN:
        case REG_YDSTLEN + 1:
            WRITE8(addr, mystique->dwgreg.length, val);
#if 0
            pclog("Write YDSTLEN+%i %i\n", addr&1, mystique->dwgreg.length);
#endif
            break;
        case REG_YDSTLEN + 2:
            mystique->dwgreg.ydst = (mystique->dwgreg.ydst & ~0xff) | val;
            if (mystique->dwgreg.pitch & PITCH_YLIN)
                mystique->dwgreg.ydst_lin = (mystique->dwgreg.ydst << 5) + mystique->dwgreg.ydstorg;
            else {
                mystique->dwgreg.ydst_lin = ((int32_t) (int16_t) mystique->dwgreg.ydst * (mystique->dwgreg.pitch & PITCH_MASK)) + mystique->dwgreg.ydstorg;
                mystique->dwgreg.selline  = val & 7;
            }
            break;
        case REG_YDSTLEN + 3:
            mystique->dwgreg.ydst = (mystique->dwgreg.ydst & 0xff) | (((int32_t) (int8_t) val) << 8);
            if (mystique->dwgreg.pitch & PITCH_YLIN)
                mystique->dwgreg.ydst_lin = (mystique->dwgreg.ydst << 5) + mystique->dwgreg.ydstorg;
            else
                mystique->dwgreg.ydst_lin = ((int32_t) (int16_t) mystique->dwgreg.ydst * (mystique->dwgreg.pitch & PITCH_MASK)) + mystique->dwgreg.ydstorg;
            break;

        case REG_XDST:
        case REG_XDST + 1:
            WRITE8(addr & 1, mystique->dwgreg.xdst, val);
            break;
        case REG_XDST + 2:
        case REG_XDST + 3:
            break;

        case REG_YDSTORG:
        case REG_YDSTORG + 1:
        case REG_YDSTORG + 2:
        case REG_YDSTORG + 3:
            WRITE8(addr, mystique->dwgreg.ydstorg, val);
            mystique->dwgreg.z_base = mystique->dwgreg.ydstorg * ((mystique->maccess & MACCESS_ZWIDTH) ? 4 : 2) + mystique->dwgreg.zorg;
            break;
        case REG_YTOP:
        case REG_YTOP + 1:
        case REG_YTOP + 2:
        case REG_YTOP + 3:
            WRITE8(addr, mystique->dwgreg.ytop, val);
            break;
        case REG_YBOT:
        case REG_YBOT + 1:
        case REG_YBOT + 2:
        case REG_YBOT + 3:
            WRITE8(addr, mystique->dwgreg.ybot, val);
            break;

        case REG_CXLEFT:
        case REG_CXLEFT + 1:
            WRITE8(addr, mystique->dwgreg.cxleft, val);
            break;
        case REG_CXLEFT + 2:
        case REG_CXLEFT + 3:
            break;
        case REG_CXRIGHT:
        case REG_CXRIGHT + 1:
            WRITE8(addr, mystique->dwgreg.cxright, val);
            break;
        case REG_CXRIGHT + 2:
        case REG_CXRIGHT + 3:
            break;

        case REG_FXLEFT:
        case REG_FXLEFT + 1:
            WRITE8(addr, mystique->dwgreg.fxleft, val);
            break;
        case REG_FXLEFT + 2:
        case REG_FXLEFT + 3:
            break;
        case REG_FXRIGHT:
        case REG_FXRIGHT + 1:
            WRITE8(addr, mystique->dwgreg.fxright, val);
            break;
        case REG_FXRIGHT + 2:
        case REG_FXRIGHT + 3:
            break;

        case REG_SECADDRESS:
        case REG_SECADDRESS + 1:
        case REG_SECADDRESS + 2:
        case REG_SECADDRESS + 3:
            WRITE8(addr, mystique->dma.secaddress, val);
            mystique->dma.sec_state = 0;
            break;

        case REG_TMR0:
        case REG_TMR0 + 1:
        case REG_TMR0 + 2:
        case REG_TMR0 + 3:
            WRITE8(addr, mystique->dwgreg.tmr[0], val);
            break;
        case REG_TMR1:
        case REG_TMR1 + 1:
        case REG_TMR1 + 2:
        case REG_TMR1 + 3:
            WRITE8(addr, mystique->dwgreg.tmr[1], val);
            break;
        case REG_TMR2:
        case REG_TMR2 + 1:
        case REG_TMR2 + 2:
        case REG_TMR2 + 3:
            WRITE8(addr, mystique->dwgreg.tmr[2], val);
            break;
        case REG_TMR3:
        case REG_TMR3 + 1:
        case REG_TMR3 + 2:
        case REG_TMR3 + 3:
            WRITE8(addr, mystique->dwgreg.tmr[3], val);
            break;
        case REG_TMR4:
        case REG_TMR4 + 1:
        case REG_TMR4 + 2:
        case REG_TMR4 + 3:
            WRITE8(addr, mystique->dwgreg.tmr[4], val);
            break;
        case REG_TMR5:
        case REG_TMR5 + 1:
        case REG_TMR5 + 2:
        case REG_TMR5 + 3:
            WRITE8(addr, mystique->dwgreg.tmr[5], val);
            break;
        case REG_TMR6:
        case REG_TMR6 + 1:
        case REG_TMR6 + 2:
        case REG_TMR6 + 3:
            WRITE8(addr, mystique->dwgreg.tmr[6], val);
            break;
        case REG_TMR7:
        case REG_TMR7 + 1:
        case REG_TMR7 + 2:
        case REG_TMR7 + 3:
            WRITE8(addr, mystique->dwgreg.tmr[7], val);
            break;
        case REG_TMR8:
        case REG_TMR8 + 1:
        case REG_TMR8 + 2:
        case REG_TMR8 + 3:
            WRITE8(addr, mystique->dwgreg.tmr[8], val);
            break;

        case REG_TEXORG:
        case REG_TEXORG + 1:
        case REG_TEXORG + 2:
        case REG_TEXORG + 3:
            WRITE8(addr, mystique->dwgreg.texorg, val);
            break;
        case REG_TEXWIDTH:
        case REG_TEXWIDTH + 1:
        case REG_TEXWIDTH + 2:
        case REG_TEXWIDTH + 3:
            WRITE8(addr, mystique->dwgreg.texwidth, val);
            break;
        case REG_TEXHEIGHT:
        case REG_TEXHEIGHT + 1:
        case REG_TEXHEIGHT + 2:
        case REG_TEXHEIGHT + 3:
            WRITE8(addr, mystique->dwgreg.texheight, val);
            break;
        case REG_TEXCTL:
        case REG_TEXCTL + 1:
        case REG_TEXCTL + 2:
        case REG_TEXCTL + 3:
            WRITE8(addr, mystique->dwgreg.texctl, val);
            mystique->dwgreg.ta_key  = (mystique->dwgreg.texctl & TEXCTL_TAKEY) ? 1 : 0;
            mystique->dwgreg.ta_mask = (mystique->dwgreg.texctl & TEXCTL_TAMASK) ? 1 : 0;
            break;
        case REG_TEXTRANS:
        case REG_TEXTRANS + 1:
        case REG_TEXTRANS + 2:
        case REG_TEXTRANS + 3:
            WRITE8(addr, mystique->dwgreg.textrans, val);
            break;

        case 0x1c18:
        case 0x1c19:
        case 0x1c1a:
        case 0x1c1b:
        case 0x1c28:
        case 0x1c29:
        case 0x1c2a:
        case 0x1c2b:
        case 0x1c2c:
        case 0x1c2d:
        case 0x1c2e:
        case 0x1c2f:
        case 0x1cc4:
        case 0x1cc5:
        case 0x1cc6:
        case 0x1cc7:
        case 0x1cd4:
        case 0x1cd5:
        case 0x1cd6:
        case 0x1cd7:
        case 0x1ce4:
        case 0x1ce5:
        case 0x1ce6:
        case 0x1ce7:
        case 0x1cf4:
        case 0x1cf5:
        case 0x1cf6:
        case 0x1cf7:
            break;

        case REG_OPMODE:
            mystique->dwgreg.dmamod   = (val >> 2) & 3;
            mystique->dma.iload_state = 0;
            break;

        default:
            if ((addr & 0x3fff) >= 0x2c4c && (addr & 0x3fff) <= 0x2cff)
                break;
            break;
    }

    if (start_blit)
        mystique_start_blit(mystique);
}

static void
mystique_ctrl_write_b(uint32_t addr, uint8_t val, void *priv)
{
    mystique_t *mystique  = (mystique_t *) priv;
    svga_t     *svga      = &mystique->svga;
    uint8_t     addr_0x0f = 0;
    uint16_t    addr_0x03 = 0;
    int         rs2 = 0;
    int         rs3 = 0;

    if ((mystique->type == MGA_2064W || mystique->type == MGA_2164W) && (addr & 0x3e00) == 0x3c00) {
        /*RAMDAC*/
        addr_0x0f = addr & 0x0f;

        if ((addr & 3) == 0)
            addr_0x03 = 0x3c8;
        else if ((addr & 3) == 1)
            addr_0x03 = 0x3c9;
        else if ((addr & 3) == 2)
            addr_0x03 = 0x3c6;
        else if ((addr & 3) == 3)
            addr_0x03 = 0x3c7;

        if ((addr_0x0f >= 0x04) && (addr_0x0f <= 0x07)) {
            rs2 = 1;
            rs3 = 0;
        } else if ((addr_0x0f >= 0x08) && (addr_0x0f <= 0x0b)) {
            rs2 = 0;
            rs3 = 1;
        } else if ((addr_0x0f >= 0x0c) && (addr_0x0f <= 0x0f)) {
            rs2 = 1;
            rs3 = 1;
        }

        tvp3026_ramdac_out(addr_0x03, rs2, rs3, val, svga->ramdac, svga);
        return;
    }

    if ((addr & 0x3fff) < 0x1c00) {
        mystique_iload_write_b(addr, val, priv);
        return;
    }
    if ((addr & 0x3e00) == 0x1c00 || (addr & 0x3e00) == 0x2c00) {
        if ((addr & 0x300) == 0x100)
            mystique->blitter_submit_refcount++;
        mystique_queue(mystique, addr & 0x3fff, val, FIFO_WRITE_CTRL_BYTE);
        return;
    }

    switch (addr & 0x3fff) {
        case REG_ICLEAR:
            if (val & ICLEAR_SOFTRAPICLR) {
                //pclog("softrapiclr\n");
                mystique->status &= ~STATUS_SOFTRAPEN;
                mystique_update_irqs(mystique);
            }
            if (val & ICLEAR_VLINEICLR) {
                mystique->status &= ~STATUS_VLINEPEN;
                mystique_update_irqs(mystique);
            }
            break;
        case REG_ICLEAR + 1:
        case REG_ICLEAR + 2:
        case REG_ICLEAR + 3:
            break;

        case REG_IEN:
            mystique->ien = val & 0x65;
            break;
        case REG_IEN + 1:
        case REG_IEN + 2:
        case REG_IEN + 3:
            break;

        case REG_OPMODE:
            thread_wait_mutex(mystique->dma.lock);
            mystique->dma.state = DMA_STATE_IDLE; /* Interrupt DMA. */
            thread_release_mutex(mystique->dma.lock);
            mystique->dmamod = (val >> 2) & 3;
            mystique_queue(mystique, addr & 0x3fff, val, FIFO_WRITE_CTRL_BYTE);
            break;
        case REG_OPMODE + 1:
            mystique->dmadatasiz = val & 3;
            break;
        case REG_OPMODE + 2:
            mystique->dirdatasiz = val & 3;
            break;
        case REG_OPMODE + 3:
            break;

        case REG_PRIMADDRESS:
        case REG_PRIMADDRESS + 1:
        case REG_PRIMADDRESS + 2:
        case REG_PRIMADDRESS + 3:
            thread_wait_mutex(mystique->dma.lock);
            WRITE8(addr, mystique->dma.primaddress, val);
            mystique->dma.pri_state = 0;
            if (mystique->dma.state == DMA_STATE_IDLE && !(mystique->softrap_pending || mystique->endprdmasts_pending || !mystique->softrap_status_read)) {
                mystique->dma.words_expected = 0;
            }
            mystique->dma.state = DMA_STATE_IDLE;
            thread_release_mutex(mystique->dma.lock);
            break;

        case REG_DMAMAP:
        case REG_DMAMAP + 0x1:
        case REG_DMAMAP + 0x2:
        case REG_DMAMAP + 0x3:
        case REG_DMAMAP + 0x4:
        case REG_DMAMAP + 0x5:
        case REG_DMAMAP + 0x6:
        case REG_DMAMAP + 0x7:
        case REG_DMAMAP + 0x8:
        case REG_DMAMAP + 0x9:
        case REG_DMAMAP + 0xa:
        case REG_DMAMAP + 0xb:
        case REG_DMAMAP + 0xc:
        case REG_DMAMAP + 0xd:
        case REG_DMAMAP + 0xe:
        case REG_DMAMAP + 0xf:
            mystique->dmamap[addr & 0xf] = val;
            break;

        case REG_RST:
        case REG_RST + 1:
        case REG_RST + 2:
        case REG_RST + 3:
            wait_fifo_idle(mystique);
            mystique->busy                        = 0;
            mystique->blitter_submit_refcount     = 0;
            mystique->blitter_submit_dma_refcount = 0;
            mystique->blitter_complete_refcount   = 0;
            mystique->dwgreg.iload_rem_count      = 0;
            mystique->status                      = STATUS_ENDPRDMASTS;
            thread_wait_mutex(mystique->dma.lock);
            mystique->dma.pri_state               = 0;
            mystique->dma.sec_state               = 0;
            mystique->dma.state                   = DMA_STATE_IDLE;
            mystique->dma.words_expected          = 0;
            thread_release_mutex(mystique->dma.lock);
            break;

        case REG_ATTR_IDX:
            svga_out(0x3c0, val, svga);
            break;
        case REG_ATTR_DATA:
            svga_out(0x3c1, val, svga);
            break;

        case REG_MISC:
            svga_out(0x3c2, val, svga);
            break;

        case REG_SEQ_IDX:
            svga_out(0x3c4, val, svga);
            break;
        case REG_SEQ_DATA:
            svga_out(0x3c5, val, svga);
            break;

        case REG_GCTL_IDX:
            mystique_out(0x3ce, val, mystique);
            break;
        case REG_GCTL_DATA:
            mystique_out(0x3cf, val, mystique);
            break;

        case REG_CRTC_IDX:
            mystique_out(0x3d4, val, mystique);
            break;
        case REG_CRTC_DATA:
            mystique_out(0x3d5, val, mystique);
            break;

        case REG_CRTCEXT_IDX:
            mystique_out(0x3de, val, mystique);
            break;
        case REG_CRTCEXT_DATA:
            mystique_out(0x3df, val, mystique);
            break;

        case REG_CACHEFLUSH:
            break;

        case REG_PALWTADD:
            svga_out(0x3c8, val, svga);
            mystique->xreg_idx = val;
            break;
        case REG_PALDATA:
            svga_out(0x3c9, val, svga);
            break;
        case REG_PIXRDMSK:
            svga_out(0x3c6, val, svga);
            break;
        case REG_PALRDADD:
            svga_out(0x3c7, val, svga);
            break;

        case REG_X_DATAREG:
            mystique_write_xreg(mystique, mystique->xreg_idx, val);
            break;

        case REG_CURPOSX:
        case REG_CURPOSX + 1:
            WRITE8(addr, mystique->cursor.pos_x, val);
            svga->hwcursor.x = mystique->cursor.pos_x - 64;
            break;
        case REG_CURPOSY:
        case REG_CURPOSY + 1:
            WRITE8(addr & 1, mystique->cursor.pos_y, val);
            svga->hwcursor.y = mystique->cursor.pos_y - 64;
            break;

        case 0x1e50:
        case 0x1e51:
        case 0x1e52:
        case 0x1e53:
        case 0x3c0b:
        case 0x3e02:
        case 0x3e08:
            break;

        default:
            if ((addr & 0x3fff) >= 0x2c4c && (addr & 0x3fff) <= 0x2cff)
                break;
            if ((addr & 0x3fff) >= 0x3e00)
                break;
            break;
    }
}

static uint32_t
mystique_ctrl_read_l(uint32_t addr, void *priv)
{
    uint32_t ret;

    if ((addr & 0x3fff) < 0x1c00)
        return mystique_iload_read_l(addr, priv);

    ret = mystique_ctrl_read_b(addr, priv);
    ret |= mystique_ctrl_read_b(addr + 1, priv) << 8;
    ret |= mystique_ctrl_read_b(addr + 2, priv) << 16;
    ret |= mystique_ctrl_read_b(addr + 3, priv) << 24;

    return ret;
}

static void
mystique_accel_ctrl_write_l(uint32_t addr, uint32_t val, void *priv)
{
    mystique_t *mystique   = (mystique_t *) priv;
    int         start_blit = 0;

    if ((addr & 0x300) == 0x100) {
        addr &= ~0x100;
        start_blit = 1;
    }

    switch (addr & 0x3ffc) {
        case REG_DWGCTL:
            mystique->dwgreg.dwgctrl = val;

            if (val & DWGCTRL_SOLID) {
                for (uint8_t y = 0; y < 8; y++) {
                    for (uint8_t x = 0; x < 16; x++)
                        mystique->dwgreg.pattern[y][x] = 1;
                }
                mystique->dwgreg.src[0] = 0xffffffff;
                mystique->dwgreg.src[1] = 0xffffffff;
                mystique->dwgreg.src[2] = 0xffffffff;
                mystique->dwgreg.src[3] = 0xffffffff;
            }
            if (val & DWGCTRL_ARZERO) {
                mystique->dwgreg.ar[0] = 0;
                mystique->dwgreg.ar[1] = 0;
                mystique->dwgreg.ar[2] = 0;
                mystique->dwgreg.ar[4] = 0;
                mystique->dwgreg.ar[5] = 0;
                mystique->dwgreg.ar[6] = 0;
            }
            if (val & DWGCTRL_SGNZERO) {
                mystique->dwgreg.sgn.sdydxl   = 0;
                mystique->dwgreg.sgn.scanleft = 0;
                mystique->dwgreg.sgn.sdxl     = 0;
                mystique->dwgreg.sgn.sdy      = 0;
                mystique->dwgreg.sgn.sdxr     = 0;
            }
            if (val & DWGCTRL_SHTZERO) {
                mystique->dwgreg.funcnt   = 0;
                mystique->dwgreg.stylelen = 0;
                mystique->dwgreg.xoff     = 0;
                mystique->dwgreg.yoff     = 0;
            }
            break;

        case REG_ZORG:
            mystique->dwgreg.zorg   = val;
            mystique->dwgreg.z_base = mystique->dwgreg.ydstorg * ((mystique->maccess & MACCESS_ZWIDTH) ? 4 : 2) + mystique->dwgreg.zorg;
            break;

        case REG_PLNWT:
            mystique->dwgreg.plnwt = val;
            break;

        case REG_SHIFT:
            mystique->dwgreg.funcnt   = val & 0x7f;
            mystique->dwgreg.xoff     = val & 7;
            mystique->dwgreg.yoff     = (val >> 4) & 7;
            mystique->dwgreg.stylelen = (val >> 16) & 0x7f;
            break;

        case REG_PITCH:
            mystique->dwgreg.pitch = val & 0xffff;
            if (mystique->dwgreg.pitch & PITCH_YLIN)
                mystique->dwgreg.ydst_lin = (mystique->dwgreg.ydst << 5) + mystique->dwgreg.ydstorg;
            else
                mystique->dwgreg.ydst_lin = ((int32_t) (int16_t) mystique->dwgreg.ydst * (mystique->dwgreg.pitch & PITCH_MASK)) + mystique->dwgreg.ydstorg;
            break;

        case REG_YDST:
            mystique->dwgreg.ydst = val & 0x3fffff;
            if (mystique->dwgreg.pitch & PITCH_YLIN) {
                mystique->dwgreg.ydst_lin = (mystique->dwgreg.ydst << 5) + mystique->dwgreg.ydstorg;
                mystique->dwgreg.selline  = val >> 29;
            } else {
                mystique->dwgreg.ydst_lin = ((int32_t) (int16_t) mystique->dwgreg.ydst * (mystique->dwgreg.pitch & PITCH_MASK)) + mystique->dwgreg.ydstorg;
                mystique->dwgreg.selline  = val & 7;
            }
            break;
        case REG_BCOL:
            mystique->dwgreg.bcol = val;
            break;
        case REG_FCOL:
            mystique->dwgreg.fcol = val;
            break;

        case REG_SRC0:
            {
                mystique->dwgreg.src[0] = val;
                for (uint8_t y = 0; y < 2; y++) {
                    for (uint8_t x = 0; x < 16; x++) {
                        mystique->dwgreg.pattern[y][x] = val & (1 << (x + (y * 16)));
                    }
                }
#if 0
                pclog("SRC0 = 0x%08X\n", val);
#endif
                if (mystique->busy && (mystique->dwgreg.dwgctrl_running & DWGCTRL_OPCODE_MASK) == DWGCTRL_OPCODE_ILOAD)
                    blit_iload_write(mystique, mystique->dwgreg.src[0], 32);
            }
            break;
        case REG_SRC1:
            {
                mystique->dwgreg.src[1] = val;
                for (uint8_t y = 2; y < 4; y++) {
                    for (uint8_t x = 0; x < 16; x++) {
                        mystique->dwgreg.pattern[y][x] = val & (1 << (x + ((y - 2) * 16)));
                    }
                }
#if 0
                pclog("SRC1 = 0x%08X\n", val);
#endif
                if (mystique->busy && (mystique->dwgreg.dwgctrl_running & DWGCTRL_OPCODE_MASK) == DWGCTRL_OPCODE_ILOAD)
                    blit_iload_write(mystique, mystique->dwgreg.src[1], 32);
            }
            break;
        case REG_SRC2:
            {
                mystique->dwgreg.src[2] = val;
                for (uint8_t y = 4; y < 6; y++) {
                    for (uint8_t x = 0; x < 16; x++) {
                        mystique->dwgreg.pattern[y][x] = val & (1 << (x + ((y - 4) * 16)));
                    }
                }
#if 0
                pclog("SRC2 = 0x%08X\n", val);
#endif
                if (mystique->busy && (mystique->dwgreg.dwgctrl_running & DWGCTRL_OPCODE_MASK) == DWGCTRL_OPCODE_ILOAD)
                    blit_iload_write(mystique, mystique->dwgreg.src[2], 32);
                break;
            }
        case REG_SRC3:
            {
                mystique->dwgreg.src[3] = val;
                for (uint8_t y = 6; y < 8; y++) {
                    for (uint8_t x = 0; x < 16; x++) {
                        mystique->dwgreg.pattern[y][x] = val & (1 << (x + ((y - 6) * 16)));
                    }
                }
#if 0
                pclog("SRC3 = 0x%08X\n", val);
#endif
                if (mystique->busy && (mystique->dwgreg.dwgctrl_running & DWGCTRL_OPCODE_MASK) == DWGCTRL_OPCODE_ILOAD)
                    blit_iload_write(mystique, mystique->dwgreg.src[3], 32);
                break;
            }

        case REG_DMAPAD:
            if (mystique->busy && (mystique->dwgreg.dwgctrl_running & DWGCTRL_OPCODE_MASK) == DWGCTRL_OPCODE_ILOAD)
                blit_iload_write(mystique, val, 32);
            break;

        case REG_AR0:
            mystique->dwgreg.ar[0] = val;
            break;
        case REG_AR1:
            mystique->dwgreg.ar[1] = val;
            break;
        case REG_AR2:
            mystique->dwgreg.ar[2] = val;
            break;
        case REG_AR3:
            mystique->dwgreg.ar[3] = val;
            break;
        case REG_AR4:
            mystique->dwgreg.ar[4] = val;
            break;
        case REG_AR5:
            mystique->dwgreg.ar[5] = val;
            break;
        case REG_AR6:
            mystique->dwgreg.ar[6] = val;
            break;

        case REG_DR0_Z32LSB:
            mystique->dwgreg.extended_dr[0] = (mystique->dwgreg.extended_dr[0] & ~0xFFFFFFFF) | val;
            mystique->dwgreg.dr[0] = (mystique->dwgreg.extended_dr[0] >> 16) & 0xFFFFFFFF;
            break;

        case REG_DR0_Z32MSB:
            mystique->dwgreg.extended_dr[0] = (mystique->dwgreg.extended_dr[0] & 0xFFFFFFFF) | ((val & 0xFFFFull) << 32ull);
            mystique->dwgreg.dr[0] = (mystique->dwgreg.extended_dr[0] >> 16) & 0xFFFFFFFF;
            break;

        case REG_DR2_Z32LSB:
            mystique->dwgreg.extended_dr[2] = (mystique->dwgreg.extended_dr[2] & ~0xFFFFFFFF) | val;
            mystique->dwgreg.dr[2] = (mystique->dwgreg.extended_dr[2] >> 16) & 0xFFFFFFFF;
            break;

        case REG_DR2_Z32MSB:
            mystique->dwgreg.extended_dr[2] = (mystique->dwgreg.extended_dr[2] & 0xFFFFFFFF) | ((val & 0xFFFFull) << 32ull);
            mystique->dwgreg.dr[2] = (mystique->dwgreg.extended_dr[2] >> 16) & 0xFFFFFFFF;
            break;

        case REG_DR3_Z32LSB:
            mystique->dwgreg.extended_dr[3] = (mystique->dwgreg.extended_dr[3] & ~0xFFFFFFFF) | val;
            mystique->dwgreg.dr[3] = (mystique->dwgreg.extended_dr[3] >> 16) & 0xFFFFFFFF;
            break;

        case REG_DR3_Z32MSB:
            mystique->dwgreg.extended_dr[3] = (mystique->dwgreg.extended_dr[3] & 0xFFFFFFFF) | ((val & 0xFFFFull) << 32ull);
            mystique->dwgreg.dr[3] = (mystique->dwgreg.extended_dr[3] >> 16) & 0xFFFFFFFF;
            break;

        case REG_DR0:
            mystique->dwgreg.dr[0] = val;
            mystique->dwgreg.extended_dr[0] = (mystique->dwgreg.extended_dr[0] & ~0xFFFFull) | ((uint64_t)val << 16ull);
            break;
        case REG_DR2:
            mystique->dwgreg.dr[2] = val;
            mystique->dwgreg.extended_dr[2] = (mystique->dwgreg.extended_dr[2] & ~0xFFFFull) | ((uint64_t)val << 16ull);
            break;
        case REG_DR3:
            mystique->dwgreg.dr[3] = val;
            mystique->dwgreg.extended_dr[3] = (mystique->dwgreg.extended_dr[3] & ~0xFFFFull) | ((uint64_t)val << 16ull);
            break;
        case REG_DR4:
            mystique->dwgreg.dr[4] = val;
            break;
        case REG_DR6:
            mystique->dwgreg.dr[6] = val;
            break;
        case REG_DR7:
            mystique->dwgreg.dr[7] = val;
            break;
        case REG_DR8:
            mystique->dwgreg.dr[8] = val;
            break;
        case REG_DR10:
            mystique->dwgreg.dr[10] = val;
            break;
        case REG_DR11:
            mystique->dwgreg.dr[11] = val;
            break;
        case REG_DR12:
            mystique->dwgreg.dr[12] = val;
            break;
        case REG_DR14:
            mystique->dwgreg.dr[14] = val;
            break;
        case REG_DR15:
            mystique->dwgreg.dr[15] = val;
            break;

        case REG_SECEND:
            mystique->dma.secend = val;
            if (mystique->dma.state != DMA_STATE_SEC && (mystique->dma.secaddress & DMA_ADDR_MASK) != (mystique->dma.secend & DMA_ADDR_MASK))
                mystique->dma.state = DMA_STATE_SEC;
            break;

        case REG_SOFTRAP:
            mystique->dma.state           = DMA_STATE_IDLE;
            mystique->dma.pri_state       = 0;
            mystique->dma.words_expected  = 0;
            mystique->endprdmasts_pending = 1;
            mystique->softrap_pending_val = val;
            mystique->softrap_pending     += 1;
            break;

        case REG_ALPHACTRL:
            mystique->dwgreg.alphactrl = val;
            break;

        case REG_ALPHASTART:
            mystique->dwgreg.alphastart = val;
            break;

        case REG_ALPHAXINC:
            mystique->dwgreg.alphaxinc = val;
            break;

        case REG_ALPHAYINC:
            mystique->dwgreg.alphayinc = val;
            break;

        case REG_FOGCOL:
            mystique->dwgreg.fogcol = val;
            break;

        case REG_FOGSTART:
            mystique->dwgreg.fogstart = val;
            break;

        case REG_FOGXINC:
            mystique->dwgreg.fogxinc = val;
            break;

        case REG_FOGYINC:
            mystique->dwgreg.fogyinc = val;
            break;

        case REG_TEXFILTER:
            mystique->dwgreg.texfilter = val;
            break;

        default:
            mystique_accel_ctrl_write_b(addr, val & 0xff, priv);
            mystique_accel_ctrl_write_b(addr + 1, (val >> 8) & 0xff, priv);
            mystique_accel_ctrl_write_b(addr + 2, (val >> 16) & 0xff, priv);
            mystique_accel_ctrl_write_b(addr + 3, (val >> 24) & 0xff, priv);
            break;
    }

    if (start_blit)
        mystique_start_blit(mystique);
}

static void
mystique_ctrl_write_l(uint32_t addr, uint32_t val, void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;
    uint32_t    reg_addr;

    if ((addr & 0x3fff) < 0x1c00) {
        mystique_iload_write_l(addr, val, priv);
        return;
    }

    if ((addr & 0x3e00) == 0x1c00 || (addr & 0x3e00) == 0x2c00) {
        if ((addr & 0x300) == 0x100)
            mystique->blitter_submit_refcount++;
        mystique_queue(mystique, addr & 0x3fff, val, FIFO_WRITE_CTRL_LONG);
        return;
    }

    switch (addr & 0x3ffc) {
        case REG_PRIMEND:
            thread_wait_mutex(mystique->dma.lock);
            mystique->dma.primend = val;
            //pclog("PRIMADDRESS = 0x%08X, PRIMEND = 0x%08X\n", mystique->dma.primaddress, mystique->dma.primend);
            if (mystique->dma.state == DMA_STATE_IDLE && (mystique->dma.primaddress & DMA_ADDR_MASK) != (mystique->dma.primend & DMA_ADDR_MASK)) {
                mystique->endprdmasts_pending = 0;
                mystique->status &= ~STATUS_ENDPRDMASTS;

                mystique->dma.state     = DMA_STATE_PRI;
                //mystique->dma.pri_state = 0;
                wake_fifo_thread(mystique);
            }
            /* HACK: For DirectX 9.0b Direct3D testing on Windows 98 SE.

                The 4.12.013 drivers give an out-of-bounds busmastering range when dxdiag enumerates Direct3D, with exactly 16384 bytes of difference.
                Don't attempt busmastering in such cases. This isn't ideal, but there are no more crashes faced in this case. */
            if ((mystique->dma.primend & DMA_ADDR_MASK) < (mystique->dma.primaddress & DMA_ADDR_MASK) && ((mystique->dma.primaddress & DMA_ADDR_MASK) - (mystique->dma.primend & DMA_ADDR_MASK)) == 0x4000)
            {
                mystique->dma.primaddress = mystique->dma.primend;
                mystique->endprdmasts_pending = 1;
                mystique->dma.state = DMA_STATE_IDLE;
            }
            thread_release_mutex(mystique->dma.lock);
            break;

        case REG_DWG_INDIR_WT:
        case REG_DWG_INDIR_WT + 0x04:
        case REG_DWG_INDIR_WT + 0x08:
        case REG_DWG_INDIR_WT + 0x0c:
        case REG_DWG_INDIR_WT + 0x10:
        case REG_DWG_INDIR_WT + 0x14:
        case REG_DWG_INDIR_WT + 0x18:
        case REG_DWG_INDIR_WT + 0x1c:
        case REG_DWG_INDIR_WT + 0x20:
        case REG_DWG_INDIR_WT + 0x24:
        case REG_DWG_INDIR_WT + 0x28:
        case REG_DWG_INDIR_WT + 0x2c:
        case REG_DWG_INDIR_WT + 0x30:
        case REG_DWG_INDIR_WT + 0x34:
        case REG_DWG_INDIR_WT + 0x38:
        case REG_DWG_INDIR_WT + 0x3c:
            reg_addr = (mystique->dmamap[(addr >> 2) & 0xf] & 0x7f) << 2;
            if (mystique->dmamap[(addr >> 2) & 0xf] & 0x80)
                reg_addr += 0x2c00;
            else
                reg_addr += 0x1c00;

            if ((reg_addr & 0x300) == 0x100)
                mystique->blitter_submit_refcount++;

            mystique_queue(mystique, reg_addr, val, FIFO_WRITE_CTRL_LONG);
            break;

        default:
            mystique_ctrl_write_b(addr, val & 0xff, priv);
            mystique_ctrl_write_b(addr + 1, (val >> 8) & 0xff, priv);
            mystique_ctrl_write_b(addr + 2, (val >> 16) & 0xff, priv);
            mystique_ctrl_write_b(addr + 3, (val >> 24) & 0xff, priv);
            break;
    }
}

static uint8_t
mystique_iload_read_b(UNUSED(uint32_t addr), void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    wait_fifo_idle(mystique);

    if (!mystique->busy)
        return 0xff;

    return blit_idump_read(mystique);
}

static uint32_t
mystique_iload_read_l(UNUSED(uint32_t addr), void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    wait_fifo_idle(mystique);

    if (!mystique->busy)
        return 0xffffffff;

    mystique->dwgreg.words++;
    return blit_idump_read(mystique);
}

static void
mystique_iload_write_b(UNUSED(uint32_t addr), UNUSED(uint8_t val), UNUSED(void *priv))
{
    //
}

static void
mystique_iload_write_l(UNUSED(uint32_t addr), uint32_t val, void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    mystique_queue(mystique, 0, val, FIFO_WRITE_ILOAD_LONG);
}

static void
mystique_accel_iload_write_l(UNUSED(uint32_t addr), uint32_t val, void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    switch (mystique->dwgreg.dmamod) {
        case DMA_MODE_REG:
            if (mystique->dma.iload_state == 0) {
                mystique->dma.iload_header = val;
                mystique->dma.iload_state  = 1;
            } else {
                uint32_t reg_addr = (mystique->dma.iload_header & 0x7f) << 2;
                if (mystique->dma.iload_header & 0x80)
                    reg_addr += 0x2c00;
                else
                    reg_addr += 0x1c00;

                if ((reg_addr & 0x300) == 0x100)
                    mystique->blitter_submit_dma_refcount++;
                mystique_accel_ctrl_write_l(reg_addr, val, mystique);

                mystique->dma.iload_header >>= 8;
                mystique->dma.iload_state = (mystique->dma.iload_state == 4) ? 0 : (mystique->dma.iload_state + 1);
            }
            break;

        case DMA_MODE_BLIT:
            if (mystique->busy)
                blit_iload_write(mystique, val, 32);
            break;

        default:
#if 0
            pclog("ILOAD write DMAMOD %i\n", mystique->dwgreg.dmamod); */
#endif
            break;
    }
}

static uint8_t
mystique_readb_linear(uint32_t addr, void *priv)
{
    const svga_t *svga = (svga_t *) priv;

    cycles -= svga->monitor->mon_video_timing_read_b;

    if (!svga->fast) {
        if (svga->chain2_read) {
            addr &= ~1;
            addr <<= 2;
        }
    }

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return 0xff;

    return svga->vram[addr & svga->vram_mask];
}

static uint16_t
mystique_readw_linear(uint32_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    cycles -= svga->monitor->mon_video_timing_read_w;

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return 0xffff;

    return *(uint16_t *) &svga->vram[addr & svga->vram_mask];
}

static uint32_t
mystique_readl_linear(uint32_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    cycles -= svga->monitor->mon_video_timing_read_l;

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return 0xffffffff;

    return *(uint32_t *) &svga->vram[addr & svga->vram_mask];
}

static void
mystique_writeb_linear(uint32_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    cycles -= svga->monitor->mon_video_timing_write_b;

    if (!svga->fast) {
        if (svga->chain2_write) {
            addr &= ~1;
            addr <<= 2;
        }
    }

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;
    svga->vram[addr]              = val;
}

static void
mystique_writew_linear(uint32_t addr, uint16_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    cycles -= svga->monitor->mon_video_timing_write_w;

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12]   = svga->monitor->mon_changeframecount;
    *(uint16_t *) &svga->vram[addr] = val;
}

static void
mystique_writel_linear(uint32_t addr, uint32_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    cycles -= svga->monitor->mon_video_timing_write_l;

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12]   = svga->monitor->mon_changeframecount;
    *(uint32_t *) &svga->vram[addr] = val;
}

static void
run_dma(mystique_t *mystique)
{
    int words_transferred = 0;

    thread_wait_mutex(mystique->dma.lock);

    if (mystique->softrap_pending || mystique->endprdmasts_pending || !mystique->softrap_status_read)
    {
        thread_release_mutex(mystique->dma.lock);
        return;
    }

    if (mystique->dma.state == DMA_STATE_IDLE) {
        if (!(mystique->status & STATUS_ENDPRDMASTS))
        {
            /* Force this to appear. */
            mystique->endprdmasts_pending = 1;
        }
        thread_release_mutex(mystique->dma.lock);
        return;
    }

    while (words_transferred < DMA_MAX_WORDS && mystique->dma.state != DMA_STATE_IDLE) {
        switch (atomic_load(&mystique->dma.state)) {
            case DMA_STATE_PRI:
                switch (mystique->dma.primaddress & DMA_MODE_MASK) {
                    case DMA_MODE_REG:
                        if ((mystique->dma.primaddress & DMA_ADDR_MASK) == (mystique->dma.primend & DMA_ADDR_MASK)) {
                            mystique->endprdmasts_pending = 1;
                            mystique->dma.state           = DMA_STATE_IDLE;
                            break;
                        }
                        if (mystique->dma.pri_state == 0 && !mystique->dma.words_expected) {
                            dma_bm_read(mystique->dma.primaddress & DMA_ADDR_MASK, (uint8_t *) &mystique->dma.pri_header, 4, 4);
                            //pclog("DMA header: 0x%08X\n", mystique->dma.pri_header);
                            mystique->dma.primaddress += 4;
                            mystique->dma.words_expected = 4;
                            words_transferred++;
                        }

                        if ((mystique->dma.primaddress & DMA_ADDR_MASK) == (mystique->dma.primend & DMA_ADDR_MASK)) {
                            mystique->endprdmasts_pending = 1;
                            mystique->dma.state           = DMA_STATE_IDLE;
                            break;
                        }

                        {
                            uint32_t val;
                            uint32_t reg_addr;

                            dma_bm_read(mystique->dma.primaddress & DMA_ADDR_MASK, (uint8_t *) &val, 4, 4);
                            words_transferred++;

                            reg_addr = (mystique->dma.pri_header & 0x7f) << 2;
                            if (mystique->dma.pri_header & 0x80)
                                reg_addr += 0x2c00;
                            else
                                reg_addr += 0x1c00;

                            if ((reg_addr & 0x300) == 0x100)
                                mystique->blitter_submit_dma_refcount++;

                            //pclog("DMA value: 0x%08X to reg 0x%04X\n", val, reg_addr);
                            mystique_accel_ctrl_write_l(reg_addr, val, mystique);
                            if (reg_addr == REG_SOFTRAP) {
                                mystique->dma.primaddress += 4;
                                break;
                            }
                        }

                        if (mystique->dma.words_expected)
                            mystique->dma.words_expected--;
                        mystique->dma.primaddress += 4;

                        mystique->dma.pri_header >>= 8;
                        mystique->dma.pri_state = (mystique->dma.pri_state + 1) & 3;

                        if (mystique->dma.state == DMA_STATE_SEC) {
                            mystique->dma.sec_state = 0;
                        }
                        else if ((mystique->dma.primaddress & DMA_ADDR_MASK) == (mystique->dma.primend & DMA_ADDR_MASK)) {
                            mystique->endprdmasts_pending = 1;
                            mystique->dma.state           = DMA_STATE_IDLE;
                        }
                        break;

                    default:
                        fatal("DMA_STATE_PRI: mode %i\n", mystique->dma.primaddress & DMA_MODE_MASK);
                }
                break;

            case DMA_STATE_SEC:
                switch (mystique->dma.secaddress & DMA_MODE_MASK) {
                    case DMA_MODE_REG:
                        if ((mystique->dma.secaddress & DMA_ADDR_MASK) >= (mystique->dma.secend & DMA_ADDR_MASK)) {
                            if ((mystique->dma.primaddress & DMA_ADDR_MASK) == (mystique->dma.primend & DMA_ADDR_MASK)) {
                                mystique->endprdmasts_pending = 1;
                                mystique->dma.state           = DMA_STATE_IDLE;
                                mystique->dma.pri_state       = 0;
                                mystique->dma.words_expected  = 0;
                            } else {
                                mystique->dma.state = DMA_STATE_PRI;
                                mystique->dma.words_expected = 0;
                                mystique->dma.pri_state = 0;
                            }
                        }
                        if (mystique->dma.sec_state == 0) {
                            dma_bm_read(mystique->dma.secaddress & DMA_ADDR_MASK, (uint8_t *) &mystique->dma.sec_header, 4, 4);
                            mystique->dma.secaddress += 4;
                            //pclog("DMA header (secondary): 0x%08X\n", mystique->dma.sec_header);
                            words_transferred++;
                        }

                        if ((mystique->dma.secaddress & DMA_ADDR_MASK) >= (mystique->dma.secend & DMA_ADDR_MASK)) {
                            if ((mystique->dma.primaddress & DMA_ADDR_MASK) == (mystique->dma.primend & DMA_ADDR_MASK)) {
                                mystique->endprdmasts_pending = 1;
                                mystique->dma.state           = DMA_STATE_IDLE;
                                mystique->dma.pri_state       = 0;
                                mystique->dma.words_expected  = 0;
                            } else {
                                mystique->dma.state = DMA_STATE_PRI;
                                mystique->dma.words_expected = 0;
                                mystique->dma.pri_state = 0;
                            }
                        }

                        uint32_t val;
                        uint32_t reg_addr;

                        dma_bm_read(mystique->dma.secaddress & DMA_ADDR_MASK, (uint8_t *) &val, 4, 4);
                        mystique->dma.secaddress += 4;

                        reg_addr = (mystique->dma.sec_header & 0x7f) << 2;
                        if (mystique->dma.sec_header & 0x80)
                            reg_addr += 0x2c00;
                        else
                            reg_addr += 0x1c00;

                        if ((reg_addr & 0x300) == 0x100)
                            mystique->blitter_submit_dma_refcount++;

                        mystique_accel_ctrl_write_l(reg_addr, val, mystique);
                        //pclog("DMA value (secondary): 0x%08X\n", val);
                        mystique->dma.sec_header >>= 8;
                        mystique->dma.sec_state = (mystique->dma.sec_state + 1) & 3;

                        words_transferred++;
                        if ((mystique->dma.secaddress & DMA_ADDR_MASK) >= (mystique->dma.secend & DMA_ADDR_MASK)) {
                            if ((mystique->dma.primaddress & DMA_ADDR_MASK) == (mystique->dma.primend & DMA_ADDR_MASK)) {
                                mystique->endprdmasts_pending = 1;
                                mystique->dma.state           = DMA_STATE_IDLE;
                                mystique->dma.pri_state       = 0;
                                mystique->dma.words_expected  = 0;
                            } else {
                                mystique->dma.state = DMA_STATE_PRI;
                                mystique->dma.words_expected = 0;
                                mystique->dma.pri_state = 0;
                            }
                        }
                        break;

                    case DMA_MODE_BLIT:
                        {
                            uint32_t val;
                            if ((mystique->dma.secaddress & DMA_ADDR_MASK) >= (mystique->dma.secend & DMA_ADDR_MASK)) {
                                if ((mystique->dma.primaddress & DMA_ADDR_MASK) == (mystique->dma.primend & DMA_ADDR_MASK)) {
                                    mystique->endprdmasts_pending = 1;
                                    mystique->dma.state           = DMA_STATE_IDLE;
                                    mystique->dma.words_expected = 0;
                                    mystique->dma.pri_state = 0;
                                } else {
                                    mystique->dma.state = DMA_STATE_PRI;
                                    mystique->dma.words_expected = 0;
                                    mystique->dma.pri_state = 0;
                                }
                            }

                            dma_bm_read(mystique->dma.secaddress & DMA_ADDR_MASK, (uint8_t *) &val, 4, 4);
                            mystique->dma.secaddress += 4;

                            if (mystique->busy)
                                blit_iload_write(mystique, val, 32);

                            words_transferred++;
                            if ((mystique->dma.secaddress & DMA_ADDR_MASK) >= (mystique->dma.secend & DMA_ADDR_MASK)) {
                                if ((mystique->dma.primaddress & DMA_ADDR_MASK) == (mystique->dma.primend & DMA_ADDR_MASK)) {
                                    mystique->endprdmasts_pending = 1;
                                    mystique->dma.state           = DMA_STATE_IDLE;
                                    mystique->dma.words_expected = 0;
                                    mystique->dma.pri_state = 0;
                                } else {
                                    mystique->dma.state = DMA_STATE_PRI;
                                    mystique->dma.words_expected = 0;
                                    mystique->dma.pri_state = 0;
                                }
                            }
                        }
                        break;

                    default:
                        fatal("DMA_STATE_SEC: mode %i\n", mystique->dma.secaddress & DMA_MODE_MASK);
                }
                break;

            default:
                break;
        }
    }

    thread_release_mutex(mystique->dma.lock);
}

static void
fifo_thread(void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    while (mystique->thread_run) {
        thread_set_event(mystique->fifo_not_full_event);
        thread_wait_event(mystique->wake_fifo_thread, -1);
        thread_reset_event(mystique->wake_fifo_thread);

        while (!FIFO_EMPTY || mystique->dma.state != DMA_STATE_IDLE) {
            int words_transferred = 0;

            while (!FIFO_EMPTY && words_transferred < 100) {
                fifo_entry_t *fifo = &mystique->fifo[mystique->fifo_read_idx & FIFO_MASK];

                switch (fifo->addr_type & FIFO_TYPE) {
                    case FIFO_WRITE_CTRL_BYTE:
                        mystique_accel_ctrl_write_b(fifo->addr_type & FIFO_ADDR, fifo->val, mystique);
                        break;
                    case FIFO_WRITE_CTRL_LONG:
                        mystique_accel_ctrl_write_l(fifo->addr_type & FIFO_ADDR, fifo->val, mystique);
                        break;
                    case FIFO_WRITE_ILOAD_LONG:
                        mystique_accel_iload_write_l(fifo->addr_type & FIFO_ADDR, fifo->val, mystique);
                        break;

                    default:
                        break;
                }

                fifo->addr_type = FIFO_INVALID;
                mystique->fifo_read_idx++;

                if (FIFO_ENTRIES > FIFO_THRESHOLD)
                    thread_set_event(mystique->fifo_not_full_event);

                words_transferred++;
            }

            /*Only run DMA once the FIFO is empty. Required by
              Screamer 2 / Rally which will incorrectly clip an ILOAD
              if DMA runs ahead*/
            if (!words_transferred)
                run_dma(mystique);
        }
    }
}

static void
wake_fifo_thread(mystique_t *mystique)
{
    if (!timer_is_enabled(&mystique->wake_timer)) {
        /* Don't wake FIFO thread immediately - if we do that it will probably
           process one word and go back to sleep, requiring it to be woken on
           almost every write. Instead, wait a short while so that the CPU
           emulation writes more data so we have more batched-up work. */
        timer_set_delay_u64(&mystique->wake_timer, WAKE_DELAY);
    }
}

static void
wake_fifo_thread_now(mystique_t *mystique)
{
    thread_set_event(mystique->wake_fifo_thread);
}

static void
mystique_wake_timer(void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    thread_set_event(mystique->wake_fifo_thread); /*Wake up FIFO thread if moving from idle*/
}

static void
wait_fifo_idle(mystique_t *mystique)
{
    while (!FIFO_EMPTY) {
        wake_fifo_thread_now(mystique);
        thread_wait_event(mystique->fifo_not_full_event, 1);
    }
}

/*IRQ code (PCI & PIC) is not currently thread safe. SOFTRAP IRQ requests must
  therefore be submitted from the main emulation thread, in this case via a timer
  callback. End-of-DMA status is also deferred here to prevent races between
  SOFTRAP IRQs and code reading the status register. Croc will get into an IRQ
  loop and triple fault if the ENDPRDMASTS flag is seen before the IRQ is taken*/
static void
mystique_softrap_pending_timer(void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    timer_advance_u64(&mystique->softrap_pending_timer, TIMER_USEC * 100);

    if (mystique->endprdmasts_pending) {
        mystique->endprdmasts_pending = 0;
        mystique->status |= STATUS_ENDPRDMASTS;
    }
    if (mystique->softrap_pending) {
        mystique->dma.secaddress = mystique->softrap_pending_val;
        mystique->status |= STATUS_SOFTRAPEN;
        mystique->softrap_status_read = 0;
        //pclog("softrapen\n");
        mystique_update_irqs(mystique);
        mystique->softrap_pending--;
    }

}

static void
mystique_queue(mystique_t *mystique, uint32_t addr, uint32_t val, uint32_t type)
{
    fifo_entry_t *fifo = &mystique->fifo[mystique->fifo_write_idx & FIFO_MASK];

    if (FIFO_FULL) {
        thread_reset_event(mystique->fifo_not_full_event);
        if (FIFO_FULL)
            thread_wait_event(mystique->fifo_not_full_event, -1); /* Wait for room in ringbuffer */
    }

    fifo->val       = val;
    fifo->addr_type = (addr & FIFO_ADDR) | type;

    mystique->fifo_write_idx++;

    if (FIFO_ENTRIES > FIFO_THRESHOLD || FIFO_ENTRIES < 8)
        wake_fifo_thread(mystique);
}

static uint32_t
bitop(uint32_t src, uint32_t dst, uint32_t dwgctrl)
{
    switch (dwgctrl & DWGCTRL_BOP_MASK) {
        case BOP(0x0):
            return 0;
        case BOP(0x1):
            return ~(dst | src);
        case BOP(0x2):
            return dst & ~src;
        case BOP(0x3):
            return ~src;
        case BOP(0x4):
            return ~dst & src;
        case BOP(0x5):
            return ~dst;
        case BOP(0x6):
            return dst ^ src;
        case BOP(0x7):
            return ~(dst & src);
        case BOP(0x8):
            return dst & src;
        case BOP(0x9):
            return ~(dst ^ src);
        case BOP(0xa):
            return dst;
        case BOP(0xb):
            return dst | ~src;
        case BOP(0xc):
            return src;
        case BOP(0xd):
            return ~dst | src;
        case BOP(0xe):
            return dst | src;
        case BOP(0xf):
            return ~0;

        default:
            break;
    }

    return 0;
}

static uint16_t
dither(mystique_t *mystique, int r, int g, int b, int x, int y)
{
    switch (mystique->dwgreg.dither) {
        case DITHER_NONE_555:
            return (b >> 3) | ((g >> 3) << 5) | ((r >> 3) << 10);

        case DITHER_NONE_565:
            return (b >> 3) | ((g >> 2) << 5) | ((r >> 3) << 11);

        case DITHER_555:
            return dither5[b][y][x] | (dither5[g][y][x] << 5) | (dither5[r][y][x] << 10);

        case DITHER_565:
        default:
            return dither5[b][y][x] | (dither6[g][y][x] << 5) | (dither5[r][y][x] << 11);
    }
}

static uint32_t
blit_idump_idump(mystique_t *mystique)
{
    svga_t  *svga  = &mystique->svga;
    uint64_t val64 = 0;
    uint32_t val   = 0;
    int      count = 0;

    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) {
        case DWGCTRL_ATYPE_RPL:
            switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) {
                case DWGCTRL_BLTMOD_BU32RGB:
                case DWGCTRL_BLTMOD_BFCOL:
                    switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                        case MACCESS_PWIDTH_8:
                            while (count < 32) {
                                val |= (svga->vram[mystique->dwgreg.src_addr & mystique->vram_mask] << count);

                                if (mystique->dwgreg.src_addr == mystique->dwgreg.ar[0]) {
                                    mystique->dwgreg.ar[0] += mystique->dwgreg.ar[5];
                                    mystique->dwgreg.ar[3] += mystique->dwgreg.ar[5];
                                    mystique->dwgreg.src_addr = mystique->dwgreg.ar[3];
                                } else
                                    mystique->dwgreg.src_addr++;

                                if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
                                    mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
                                    mystique->dwgreg.length_cur--;
                                    if (!mystique->dwgreg.length_cur) {
                                        mystique->busy = 0;
                                        mystique->blitter_complete_refcount++;
                                        break;
                                    }
                                    break;
                                } else
                                    mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;

                                count += 8;
                            }
                            break;

                        case MACCESS_PWIDTH_16:
                            while (count < 32) {
                                val |= (((uint16_t *) svga->vram)[mystique->dwgreg.src_addr & mystique->vram_mask_w] << count);

                                if (mystique->dwgreg.src_addr == mystique->dwgreg.ar[0]) {
                                    mystique->dwgreg.ar[0] += mystique->dwgreg.ar[5];
                                    mystique->dwgreg.ar[3] += mystique->dwgreg.ar[5];
                                    mystique->dwgreg.src_addr = mystique->dwgreg.ar[3];
                                } else
                                    mystique->dwgreg.src_addr++;

                                if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
                                    mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
                                    mystique->dwgreg.length_cur--;
                                    if (!mystique->dwgreg.length_cur) {
                                        mystique->busy = 0;
                                        mystique->blitter_complete_refcount++;
                                        break;
                                    }
                                    break;
                                } else
                                    mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;

                                count += 16;
                            }
                            break;

                        case MACCESS_PWIDTH_24:
                            if (mystique->dwgreg.idump_end_of_line) {
                                mystique->dwgreg.idump_end_of_line = 0;
                                val                                = mystique->dwgreg.iload_rem_data;
                                mystique->dwgreg.iload_rem_count   = 0;
                                mystique->dwgreg.iload_rem_data    = 0;
                                if (!mystique->dwgreg.length_cur) {
                                    mystique->busy = 0;
                                    mystique->blitter_complete_refcount++;
                                }
                                break;
                            }

                            count += mystique->dwgreg.iload_rem_count;
                            val64 = mystique->dwgreg.iload_rem_data;

                            while ((count < 32) && !mystique->dwgreg.idump_end_of_line) {
                                val64 |= (uint64_t) ((*(uint32_t *) &svga->vram[(mystique->dwgreg.src_addr * 3) & mystique->vram_mask]) & 0xffffff) << count;

                                if (mystique->dwgreg.src_addr == mystique->dwgreg.ar[0]) {
                                    mystique->dwgreg.ar[0] += mystique->dwgreg.ar[5];
                                    mystique->dwgreg.ar[3] += mystique->dwgreg.ar[5];
                                    mystique->dwgreg.src_addr = mystique->dwgreg.ar[3];
                                } else
                                    mystique->dwgreg.src_addr++;

                                if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
                                    mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
                                    mystique->dwgreg.length_cur--;
                                    if (!mystique->dwgreg.length_cur) {
                                        if (count > 8)
                                            mystique->dwgreg.idump_end_of_line = 1;
                                        else {
                                            count          = 32;
                                            mystique->busy = 0;
                                            mystique->blitter_complete_refcount++;
                                        }
                                        break;
                                    }
                                    if (!(mystique->dwgreg.dwgctrl_running & DWGCTRL_LINEAR)) {
                                        if (count > 8)
                                            mystique->dwgreg.idump_end_of_line = 1;
                                        else {
                                            count = 32;
                                            break;
                                        }
                                    }
                                } else
                                    mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;

                                count += 24;
                            }
                            if (count > 32)
                                mystique->dwgreg.iload_rem_count = count - 32;
                            else
                                mystique->dwgreg.iload_rem_count = 0;
                            mystique->dwgreg.iload_rem_data = (uint32_t) (val64 >> 32);
                            val                             = val64 & 0xffffffff;
                            break;

                        case MACCESS_PWIDTH_32:
                            val = (((uint32_t *) svga->vram)[mystique->dwgreg.src_addr & mystique->vram_mask_l] << count);

                            if (mystique->dwgreg.src_addr == mystique->dwgreg.ar[0]) {
                                mystique->dwgreg.ar[0] += mystique->dwgreg.ar[5];
                                mystique->dwgreg.ar[3] += mystique->dwgreg.ar[5];
                                mystique->dwgreg.src_addr = mystique->dwgreg.ar[3];
                            } else
                                mystique->dwgreg.src_addr++;

                            if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
                                mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
                                mystique->dwgreg.length_cur--;
                                if (!mystique->dwgreg.length_cur) {
                                    mystique->busy = 0;
                                    mystique->blitter_complete_refcount++;
                                    break;
                                }
                                break;
                            } else
                                mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;
                            break;

                        default:
                            fatal("IDUMP DWGCTRL_BLTMOD_BU32RGB %x %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK, mystique->maccess_running);
                    }
                    break;

                default:
                    fatal("IDUMP DWGCTRL_ATYPE_RPL %08x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK, mystique->dwgreg.dwgctrl_running);
                    break;
            }
            break;

        default:
            fatal("Unknown IDUMP atype %03x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK, mystique->dwgreg.dwgctrl_running);
    }

    return val;
}

static uint32_t
blit_idump_read(mystique_t *mystique)
{
    uint32_t ret = 0xffffffff;

    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_OPCODE_MASK) {
        case DWGCTRL_OPCODE_IDUMP:
            ret = blit_idump_idump(mystique);
            break;

        default:
            /* pclog("blit_idump_read: bad opcode %08x\n", mystique->dwgreg.dwgctrl_running); */
            break;
    }

    return ret;
}

static void
blit_fbitblt(mystique_t *mystique)
{
    svga_t  *svga = &mystique->svga;
    uint32_t src_addr;
    int      x_dir   = mystique->dwgreg.sgn.scanleft ? -1 : 1;
    int16_t  x_start = mystique->dwgreg.sgn.scanleft ? mystique->dwgreg.fxright : mystique->dwgreg.fxleft;
    int16_t  x_end   = mystique->dwgreg.sgn.scanleft ? mystique->dwgreg.fxleft : mystique->dwgreg.fxright;

    src_addr = mystique->dwgreg.ar[3];

    for (uint16_t y = 0; y < mystique->dwgreg.length; y++) {
        int16_t x = x_start;
        while (1) {
            if (x >= mystique->dwgreg.cxleft && x <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot) {
                uint32_t src;
                uint32_t old_dst;

                switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                    case MACCESS_PWIDTH_8:
                        src = svga->vram[src_addr & mystique->vram_mask];

                        svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask]                = src;
                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask) >> 12] = changeframecount;
                        break;

                    case MACCESS_PWIDTH_16:
                        src = ((uint16_t *) svga->vram)[src_addr & mystique->vram_mask_w];

                        ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w] = src;
                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w) >> 11] = changeframecount;
                        break;

                    case MACCESS_PWIDTH_24:
                        src     = *(uint32_t *) &svga->vram[(src_addr * 3) & mystique->vram_mask];
                        old_dst = *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask];

                        *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask] = (src & 0xffffff) | (old_dst & 0xff000000);
                        svga->changedvram[(((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask) >> 12] = changeframecount;
                        break;

                    case MACCESS_PWIDTH_32:
                        src = ((uint32_t *) svga->vram)[src_addr & mystique->vram_mask_l];

                        ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l] = src;
                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l) >> 10] = changeframecount;
                        break;

                    default:
                        fatal("BITBLT RPL BFCOL PWIDTH %x %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK, mystique->dwgreg.dwgctrl_running);
                }
            }

            if (src_addr == mystique->dwgreg.ar[0]) {
                mystique->dwgreg.ar[0] += mystique->dwgreg.ar[5];
                mystique->dwgreg.ar[3] += mystique->dwgreg.ar[5];
                src_addr = mystique->dwgreg.ar[3];
                break;
            } else
                src_addr += x_dir;

            if (x != x_end)
                x += x_dir;
            else
                break;
        }

        if (mystique->dwgreg.sgn.sdy)
            mystique->dwgreg.ydst_lin -= (mystique->dwgreg.pitch & PITCH_MASK);
        else
            mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
    }

    mystique->blitter_complete_refcount++;
}

static uint8_t
dither_24_to_8(int r, int g, int b)
{
    return ((b >> 6) & 3) | (((g >> 5) & 7) << 2) | (((r >> 5) & 7) << 5);
}

static void
blit_iload_iload(mystique_t *mystique, uint32_t data, int size)
{
    svga_t              *svga = &mystique->svga;
    uint32_t             src;
    uint32_t             dst;
    uint32_t             dst2;
    uint64_t             data64;
    int                  min_size = 8;
    uint32_t             bltckey = mystique->dwgreg.fcol;
    uint32_t             bltcmsk = mystique->dwgreg.bcol;
    const int            transc    = mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC;
    const int            trans_sel = (mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANS_MASK) >> DWGCTRL_TRANS_SHIFT;
    uint8_t const *const trans     = &trans_masks[trans_sel][(mystique->dwgreg.selline & 3) * 4];
    uint32_t             data_mask = 1;

    switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
        case MACCESS_PWIDTH_8:
            bltckey &= 0xff;
            bltcmsk &= 0xff;
            break;
        case MACCESS_PWIDTH_16:
            bltckey &= 0xffff;
            bltcmsk &= 0xffff;
            break;
    }

    mystique->dwgreg.words++;
    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) {
        case DWGCTRL_ATYPE_RPL:
            if (mystique->maccess_running & MACCESS_TLUTLOAD) {
                while ((mystique->dwgreg.length_cur > 0) && (size >= 16)) {
                    uint16_t src = data & 0xffff;

                    mystique->lut[mystique->dwgreg.ydst & 0xff].r = (src >> 11) << 3;
                    mystique->lut[mystique->dwgreg.ydst & 0xff].g = ((src >> 5) & 0x3f) << 2;
                    mystique->lut[mystique->dwgreg.ydst & 0xff].b = (src & 0x1f) << 3;
                    mystique->dwgreg.ydst++;
                    mystique->dwgreg.length_cur--;
                    data >>= 16;
                    size -= 16;
                }

                if (!mystique->dwgreg.length_cur) {
                    mystique->busy = 0;
                    mystique->blitter_complete_refcount++;
                }
                break;
            }
        case DWGCTRL_ATYPE_RSTR:
        case DWGCTRL_ATYPE_BLK:
            switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) {
                case DWGCTRL_BLTMOD_BFCOL:
                    size += mystique->dwgreg.iload_rem_count;
                    data64 = mystique->dwgreg.iload_rem_data | ((uint64_t) data << mystique->dwgreg.iload_rem_count);

                    switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                        case MACCESS_PWIDTH_8:
                            min_size = 8;
                            break;
                        case MACCESS_PWIDTH_16:
                            min_size = 16;
                            break;
                        case MACCESS_PWIDTH_24:
                            min_size = 24;
                            break;
                        case MACCESS_PWIDTH_32:
                            min_size = 32;
                            break;

                        default:
                            break;
                    }

                    while (size >= min_size) {
                        int draw = (!transc || (data & bltcmsk) != bltckey) && trans[mystique->dwgreg.xdst & 3];

                        switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                            case MACCESS_PWIDTH_8:
                                if (mystique->dwgreg.xdst >= mystique->dwgreg.cxleft && mystique->dwgreg.xdst <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && draw) {
                                    dst = svga->vram[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask];

                                    dst                                                                                                  = bitop(data & 0xff, dst, mystique->dwgreg.dwgctrl_running);
                                    svga->vram[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask]                = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask) >> 12] = changeframecount;
                                }

                                data >>= 8;
                                size -= 8;
                                break;

                            case MACCESS_PWIDTH_16:
                                if (mystique->dwgreg.xdst >= mystique->dwgreg.cxleft && mystique->dwgreg.xdst <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && draw) {
                                    dst = ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w];

                                    dst                                                                                                    = bitop(data & 0xffff, dst, mystique->dwgreg.dwgctrl_running);
                                    ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w) >> 11] = changeframecount;
                                }

                                data >>= 16;
                                size -= 16;
                                break;

                            case MACCESS_PWIDTH_24:
                                if (mystique->dwgreg.xdst >= mystique->dwgreg.cxleft && mystique->dwgreg.xdst <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot) {
                                    uint32_t old_dst = *((uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) * 3) & mystique->vram_mask]);

                                    dst                                                                                                          = bitop(data64, old_dst, mystique->dwgreg.dwgctrl_running);
                                    *((uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) * 3) & mystique->vram_mask]) = (dst & 0xffffff) | (old_dst & 0xff000000);
                                    svga->changedvram[(((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) * 3) & mystique->vram_mask) >> 12]   = changeframecount;
                                }

                                data64 >>= 24;
                                size -= 24;
                                break;

                            case MACCESS_PWIDTH_32:
                                if (mystique->dwgreg.xdst >= mystique->dwgreg.cxleft && mystique->dwgreg.xdst <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && draw) {
                                    dst = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l];

                                    dst                                                                                                    = bitop(data, dst, mystique->dwgreg.dwgctrl_running);
                                    ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l) >> 10] = changeframecount;
                                }

                                size = 0;
                                break;

                            default:
                                fatal("ILOAD RSTR/RPL BFCOL pwidth %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK);
                        }

                        if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
                            mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
                            mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
                            mystique->dwgreg.selline = (mystique->dwgreg.selline + 1) & 7;
                            mystique->dwgreg.length_cur--;
                            if (!mystique->dwgreg.length_cur) {
                                mystique->busy = 0;
                                mystique->blitter_complete_refcount++;
                                break;
                            }
                            data64 = 0;
                            size   = 0;
                            break;
                        } else
                            mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;
                    }
                    mystique->dwgreg.iload_rem_count = size;
                    mystique->dwgreg.iload_rem_data  = data64;
                    break;

                case DWGCTRL_BLTMOD_BMONOWF:
                    data      = (data >> 24) | ((data & 0x00ff0000) >> 8) | ((data & 0x0000ff00) << 8) | (data << 24);
                    data_mask = (1 << 31);
                case DWGCTRL_BLTMOD_BMONOLEF:
                    while (size) {
                        if (mystique->dwgreg.xdst >= mystique->dwgreg.cxleft && mystique->dwgreg.xdst <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && ((data & data_mask) || !(mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC)) && trans[mystique->dwgreg.xdst & 3]) {
                            uint32_t old_dst;

                            src = (data & data_mask) ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                            switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                                case MACCESS_PWIDTH_8:
                                    dst = svga->vram[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask];

                                    dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                    svga->vram[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask]                = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask) >> 12] = changeframecount;
                                    break;

                                case MACCESS_PWIDTH_16:
                                    dst = ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w];

                                    dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                    ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w) >> 11] = changeframecount;
                                    break;

                                case MACCESS_PWIDTH_24:
                                    old_dst = *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) * 3) & mystique->vram_mask];

                                    dst = bitop(src, old_dst, mystique->dwgreg.dwgctrl_running);

                                    *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) * 3) & mystique->vram_mask] = (dst & 0xffffff) | (old_dst & 0xff000000);
                                    svga->changedvram[(((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) * 3) & mystique->vram_mask) >> 12] = changeframecount;
                                    break;

                                case MACCESS_PWIDTH_32:
                                    dst = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l];

                                    dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                    ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l) >> 10] = changeframecount;
                                    break;

                                default:
                                    fatal("ILOAD RSTR/RPL BMONOWF pwidth %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK);
                            }
                        }

                        if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
                            mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
                            mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
                            mystique->dwgreg.length_cur--;
                            if (!mystique->dwgreg.length_cur) {
                                mystique->busy = 0;
                                mystique->blitter_complete_refcount++;
                                break;
                            }
                            if (!(mystique->dwgreg.dwgctrl_running & DWGCTRL_LINEAR))
                                break;
                        } else
                            mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;
                        if (data_mask == 1)
                            data >>= 1;
                        else
                            data <<= 1;
                        size--;
                    }
                    break;

                case DWGCTRL_BLTMOD_BU24RGB:
                    size += mystique->dwgreg.iload_rem_count;
                    data64 = mystique->dwgreg.iload_rem_data | ((uint64_t) data << mystique->dwgreg.iload_rem_count);

                    while (size >= 24) {
                        if (mystique->dwgreg.xdst >= mystique->dwgreg.cxleft && mystique->dwgreg.xdst <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot) {
                            switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                                case MACCESS_PWIDTH_16:
                                {
                                    dst = ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w];

                                    dst = bitop(dither(mystique, (data64 >> 16) & 0xFF, (data64 >> 8) & 0xFF, data64 & 0xFF, mystique->dwgreg.xdst & 1, mystique->dwgreg.selline & 1), dst, mystique->dwgreg.dwgctrl_running);

                                    ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w) >> 11] = changeframecount;
                                    break;
                                }
                                case MACCESS_PWIDTH_8:
                                {
                                    dst = ((uint8_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask];

                                    dst = bitop(dither_24_to_8((data64 >> 16) & 0xFF, (data64 >> 8) & 0xFF, data64 & 0xFF), dst, mystique->dwgreg.dwgctrl_running);

                                    ((uint8_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask) >> 12] = changeframecount;
                                    break;
                                }
                                case MACCESS_PWIDTH_32:
                                    dst = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l];

                                    dst = bitop(data64 & 0xffffff, dst, mystique->dwgreg.dwgctrl_running);

                                    ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l) >> 10] = changeframecount;
                                    break;

                                default:
                                    fatal("ILOAD RSTR/RPL BU24RGB pwidth %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK);
                            }
                        }

                        data64 >>= 24;
                        size -= 24;
                        if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
                            mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
                            mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
                            mystique->dwgreg.length_cur--;
                            if (!mystique->dwgreg.length_cur) {
                                mystique->busy = 0;
                                mystique->blitter_complete_refcount++;
                                break;
                            }
                            data64 = 0;
                            size   = 0;
                            break;
                        } else
                            mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;
                    }

                    mystique->dwgreg.iload_rem_count = size;
                    mystique->dwgreg.iload_rem_data  = data64;
                    break;

                case DWGCTRL_BLTMOD_BU32RGB:
                    size += mystique->dwgreg.iload_rem_count;
                    data64 = mystique->dwgreg.iload_rem_data | ((uint64_t) data << mystique->dwgreg.iload_rem_count);
                    while (size >= 32) {
                        int draw = (!transc || (data & bltcmsk) != bltckey) && trans[mystique->dwgreg.xdst & 3];

                        if (mystique->dwgreg.xdst >= mystique->dwgreg.cxleft && mystique->dwgreg.xdst <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && draw) {
                            switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                                case MACCESS_PWIDTH_16:
                                {
                                    dst = ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w];

                                    dst = bitop(dither(mystique, (data64 >> 16) & 0xFF, (data64 >> 8) & 0xFF, data64 & 0xFF, mystique->dwgreg.xdst & 1, mystique->dwgreg.selline & 1), dst, mystique->dwgreg.dwgctrl_running);

                                    ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w) >> 11] = changeframecount;
                                    break;
                                }
                                case MACCESS_PWIDTH_8:
                                {
                                    dst = ((uint8_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask];

                                    dst = bitop(dither_24_to_8((data64 >> 16) & 0xFF, (data64 >> 8) & 0xFF, data64 & 0xFF), dst, mystique->dwgreg.dwgctrl_running);

                                    ((uint8_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask) >> 12] = changeframecount;
                                    break;
                                }
                                default: {
                                    dst = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l];

                                    dst                                                                                                    = bitop(data, dst, mystique->dwgreg.dwgctrl_running);
                                    ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l) >> 10] = changeframecount;
                                    break;
                                }
                            }
                        }

                        size = 0;

                        if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
                            mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
                            mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
                            mystique->dwgreg.selline = (mystique->dwgreg.selline + 1) & 7;
                            mystique->dwgreg.length_cur--;
                            if (!mystique->dwgreg.length_cur) {
                                mystique->busy = 0;
                                mystique->blitter_complete_refcount++;
                                break;
                            }
                            data64 = 0;
                            size   = 0;
                            break;
                        } else
                            mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;
                    }
                    mystique->dwgreg.iload_rem_count = size;
                    mystique->dwgreg.iload_rem_data  = data64;
                    break;

                case DWGCTRL_BLTMOD_BU32BGR:
                    size += mystique->dwgreg.iload_rem_count;
                    while (size >= 32) {
                        if (mystique->dwgreg.xdst >= mystique->dwgreg.cxleft && mystique->dwgreg.xdst <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot) {
                            switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                                case MACCESS_PWIDTH_32:
                                    dst  = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l];
                                    dst2 = ((dst >> 16) & 0xff) | (dst & 0xff00) | ((dst & 0xff) << 16); /* BGR to RGB */

                                    dst = bitop(data, dst2, mystique->dwgreg.dwgctrl_running);

                                    ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l) >> 10] = changeframecount;
                                    break;

                                default:
                                    fatal("ILOAD RSTR/RPL BU32RGB pwidth %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK);
                            }
                        }

                        size = 0;
                        if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
                            mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
                            mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
                            mystique->dwgreg.length_cur--;
                            if (!mystique->dwgreg.length_cur) {
                                mystique->busy = 0;
                                mystique->blitter_complete_refcount++;
                                break;
                            }
                            break;
                        } else
                            mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;
                    }

                    mystique->dwgreg.iload_rem_count = size;
                    break;

                default:
                    fatal("ILOAD DWGCTRL_ATYPE_RPL\n");
                    break;
            }
            break;

        default:
            fatal("Unknown ILOAD iload atype %03x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK, mystique->dwgreg.dwgctrl_running);
    }
}

#define CLAMP(x)                      \
    do {                              \
        if ((x) & ~0xff)              \
            x = ((x) < 0) ? 0 : 0xff; \
    } while (0)

static void
blit_iload_iload_scale(mystique_t *mystique, uint32_t data, int size)
{
    svga_t  *svga   = &mystique->svga;
    uint64_t data64 = 0;
    int      y0;
    int      y1;
    int      u;
    int      v;
    int      dR;
    int      dG;
    int      dB;
    int      r0;
    int      g0;
    int      b0;
    int      r1;
    int      g1;
    int      b1;

    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) {
        case DWGCTRL_BLTMOD_BUYUV:
            y0 = (298 * ((int) (data & 0xff) - 16)) >> 8;
            u  = ((data >> 8) & 0xff) - 0x80;
            y1 = (298 * ((int) ((data >> 16) & 0xff) - 16)) >> 8;
            v  = ((data >> 24) & 0xff) - 0x80;

            dR = (309 * v) >> 8;
            dG = (100 * u + 208 * v) >> 8;
            dB = (516 * u) >> 8;

            r0 = y0 + dR;
            CLAMP(r0);
            g0 = y0 - dG;
            CLAMP(g0);
            b0 = y0 + dB;
            CLAMP(b0);
            r1 = y1 + dR;
            CLAMP(r1);
            g1 = y1 - dG;
            CLAMP(g1);
            b1 = y1 + dB;
            CLAMP(b1);

            switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                case MACCESS_PWIDTH_16:
                    data = (b0 >> 3) | ((g0 >> 2) << 5) | ((r0 >> 3) << 11);
                    data |= (((b1 >> 3) | ((g1 >> 2) << 5) | ((r1 >> 3) << 11)) << 16);
                    size = 32;
                    break;
                case MACCESS_PWIDTH_32:
                    data64 = b0 | (g0 << 8) | (r0 << 16);
                    data64 |= ((uint64_t) b0 << 32) | ((uint64_t) g0 << 40) | ((uint64_t) r0 << 48);
                    size = 64;
                    break;

                default:
                    fatal("blit_iload_iload_scale BUYUV pwidth %i\n", mystique->maccess_running & MACCESS_PWIDTH_MASK);
            }
            break;

        default:
            fatal("blit_iload_iload_scale bltmod %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK);
            break;
    }

    switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
        case MACCESS_PWIDTH_16:
            while (size >= 16) {
                if (mystique->dwgreg.xdst >= mystique->dwgreg.cxleft && mystique->dwgreg.xdst <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot) {
                    uint16_t dst                                                                                           = ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w];
                    dst                                                                                                    = bitop(data & 0xffff, dst, mystique->dwgreg.dwgctrl_running);
                    ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w] = dst;
                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w) >> 11] = changeframecount;
                }

                mystique->dwgreg.ar[6] += mystique->dwgreg.ar[2];
                if ((int32_t) mystique->dwgreg.ar[6] >= 0) {
                    mystique->dwgreg.ar[6] -= (mystique->dwgreg.fxright - mystique->dwgreg.fxleft);
                    data >>= 16;
                    size -= 16;
                }

                mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;
                if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
                    mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
                    mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
                    mystique->dwgreg.ar[0] += mystique->dwgreg.ar[5];
                    mystique->dwgreg.ar[3] += mystique->dwgreg.ar[5];
                    mystique->dwgreg.ar[6] = mystique->dwgreg.ar[2] - (mystique->dwgreg.fxright - mystique->dwgreg.fxleft);
                    mystique->dwgreg.length_cur--;
                    if (!mystique->dwgreg.length_cur) {
                        mystique->busy = 0;
                        mystique->blitter_complete_refcount++;
                        break;
                    }
                    break;
                }
            }
            break;

        case MACCESS_PWIDTH_32:
            while (size >= 32) {
                if (mystique->dwgreg.xdst >= mystique->dwgreg.cxleft && mystique->dwgreg.xdst <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot) {
                    uint32_t dst                                                                                           = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l];
                    dst                                                                                                    = bitop(data64, dst, mystique->dwgreg.dwgctrl_running);
                    ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l] = dst;
                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l) >> 10] = changeframecount;
                }

                mystique->dwgreg.ar[6] += mystique->dwgreg.ar[2];
                if ((int32_t) mystique->dwgreg.ar[6] >= 0) {
                    mystique->dwgreg.ar[6] -= (mystique->dwgreg.fxright - mystique->dwgreg.fxleft);
                    data64 >>= 32;
                    size -= 32;
                }

                mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;
                if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
                    mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
                    mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
                    mystique->dwgreg.ar[0] += mystique->dwgreg.ar[5];
                    mystique->dwgreg.ar[3] += mystique->dwgreg.ar[5];
                    mystique->dwgreg.ar[6] = mystique->dwgreg.ar[2] - (mystique->dwgreg.fxright - mystique->dwgreg.fxleft);
                    mystique->dwgreg.length_cur--;
                    if (!mystique->dwgreg.length_cur) {
                        mystique->busy = 0;
                        mystique->blitter_complete_refcount++;
                        break;
                    }
                    break;
                }
            }
            break;

        default:
            fatal("ILOAD_SCALE pwidth %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK);
    }
}

static void
blit_iload_iload_high(mystique_t *mystique, uint32_t data, int size)
{
    svga_t  *svga = &mystique->svga;
    uint32_t out_data;
    int      y0;
    int      y1;
    int      u;
    int      v;
    int      dR;
    int      dG;
    int      dB;
    int      r = 0;
    int      g = 0;
    int      b = 0;
    int      next_r = 0;
    int      next_g = 0;
    int      next_b = 0;

    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) {
        case DWGCTRL_BLTMOD_BUYUV:
            y0 = (298 * ((int) (data & 0xff) - 16)) >> 8;
            u  = ((data >> 8) & 0xff) - 0x80;
            y1 = (298 * ((int) ((data >> 16) & 0xff) - 16)) >> 8;
            v  = ((data >> 24) & 0xff) - 0x80;

            dR = (309 * v) >> 8;
            dG = (100 * u + 208 * v) >> 8;
            dB = (516 * u) >> 8;

            r = y0 + dR;
            CLAMP(r);
            g = y0 - dG;
            CLAMP(g);
            b = y0 + dB;
            CLAMP(b);

            next_r = y1 + dR;
            CLAMP(next_r);
            next_g = y1 - dG;
            CLAMP(next_g);
            next_b = y1 + dB;
            CLAMP(next_b);

            size = 32;
            break;

        case DWGCTRL_BLTMOD_BU32BGR:
            r = ((data >> 16) & 0xff);
            CLAMP(r);
            g = ((data >> 8) & 0xff);
            CLAMP(g);
            b = (data & 0xff);
            CLAMP(b);

            next_r = r;
            next_g = g;
            next_b = b;

            size = 32;
            break;

        default:
            fatal("blit_iload_iload_high bltmod %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK);
            break;
    }

    while (size >= 16) {
        if (mystique->dwgreg.xdst >= mystique->dwgreg.cxleft && mystique->dwgreg.xdst <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot) {
            uint32_t dst;
            int      f1    = (mystique->dwgreg.ar[6] >> 12) & 0xf;
            int      f0    = 0x10 - f1;
            int      out_r = ((mystique->dwgreg.lastpix_r * f0) + (r * f1)) >> 4;
            int      out_g = ((mystique->dwgreg.lastpix_g * f0) + (g * f1)) >> 4;
            int      out_b = ((mystique->dwgreg.lastpix_b * f0) + (b * f1)) >> 4;

            switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                case MACCESS_PWIDTH_16:
                    out_data                                                                                               = (out_b >> 3) | ((out_g >> 2) << 5) | ((out_r >> 3) << 11);
                    dst                                                                                                    = ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w];
                    dst                                                                                                    = bitop(out_data, dst, mystique->dwgreg.dwgctrl_running);
                    ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w] = dst;
                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_w) >> 11] = changeframecount;
                    break;
                case MACCESS_PWIDTH_32:
                    out_data                                                                                               = out_b | (out_g << 8) | (out_r << 16);
                    dst                                                                                                    = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l];
                    dst                                                                                                    = bitop(out_data, dst, mystique->dwgreg.dwgctrl_running);
                    ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l] = dst;
                    svga->changedvram[((mystique->dwgreg.ydst_lin + mystique->dwgreg.xdst) & mystique->vram_mask_l) >> 10] = changeframecount;
                    break;

                default:
                    fatal("ILOAD_SCALE_HIGH RSTR/RPL BUYUV pwidth %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK);
            }
        }

        mystique->dwgreg.ar[6] += mystique->dwgreg.ar[2];
        if ((int32_t) mystique->dwgreg.ar[6] >= 0) {
            mystique->dwgreg.ar[6] -= 65536;
            size -= 16;

            mystique->dwgreg.lastpix_r = r;
            mystique->dwgreg.lastpix_g = g;
            mystique->dwgreg.lastpix_b = b;
            r                          = next_r;
            g                          = next_g;
            b                          = next_b;
        }

        if (mystique->dwgreg.xdst == mystique->dwgreg.fxright) {
            mystique->dwgreg.xdst = mystique->dwgreg.fxleft;
            mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
            mystique->dwgreg.ar[6]     = mystique->dwgreg.ar[2] - (mystique->dwgreg.fxright - mystique->dwgreg.fxleft);
            mystique->dwgreg.lastpix_r = 0;
            mystique->dwgreg.lastpix_g = 0;
            mystique->dwgreg.lastpix_b = 0;

            mystique->dwgreg.length_cur--;
            if (!mystique->dwgreg.length_cur) {
                mystique->busy = 0;
                mystique->blitter_complete_refcount++;
                break;
            }
            break;
        } else
            mystique->dwgreg.xdst = (mystique->dwgreg.xdst + 1) & 0xffff;
    }
}

static void
blit_iload_iload_highv(mystique_t *mystique, uint32_t data, UNUSED(int size))
{
    const uint8_t *src0;
    uint8_t       *src1;

    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) {
        case DWGCTRL_BLTMOD_BUYUV:
            if (!mystique->dwgreg.highv_line) {
                mystique->dwgreg.highv_data = data;
                mystique->dwgreg.highv_line = 1;
                return;
            }
            mystique->dwgreg.highv_line = 0;

            src0 = (uint8_t *) &mystique->dwgreg.highv_data;
            src1 = (uint8_t *) &data;

            src1[0] = ((src0[0] * mystique->dwgreg.beta) + (src1[0] * (16 - mystique->dwgreg.beta))) >> 4;
            src1[1] = ((src0[1] * mystique->dwgreg.beta) + (src1[1] * (16 - mystique->dwgreg.beta))) >> 4;
            src1[2] = ((src0[2] * mystique->dwgreg.beta) + (src1[2] * (16 - mystique->dwgreg.beta))) >> 4;
            src1[3] = ((src0[3] * mystique->dwgreg.beta) + (src1[3] * (16 - mystique->dwgreg.beta))) >> 4;
            blit_iload_iload_high(mystique, data, 32);
            break;

        default:
            fatal("blit_iload_iload_highv bltmod %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK);
            break;
    }
}

static void
blit_iload_write(mystique_t *mystique, uint32_t data, int size)
{
    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_OPCODE_MASK) {
        case DWGCTRL_OPCODE_ILOAD:
            blit_iload_iload(mystique, data, size);
            break;

        case DWGCTRL_OPCODE_ILOAD_SCALE:
            blit_iload_iload_scale(mystique, data, size);
            break;

        case DWGCTRL_OPCODE_ILOAD_HIGH:
            blit_iload_iload_high(mystique, data, size);
            break;

        case DWGCTRL_OPCODE_ILOAD_HIGHV:
            blit_iload_iload_highv(mystique, data, size);
            break;

        default:
            fatal("blit_iload_write: bad opcode %08x\n", mystique->dwgreg.dwgctrl_running);
    }
}

static int
z_check(uint16_t z, uint16_t old_z, uint32_t z_mode) // mystique->dwgreg.dwgctrl & DWGCTRL_ZMODE_MASK)
{
    switch (z_mode) {
        case DWGCTRL_ZMODE_ZE:
            return (z == old_z);
        case DWGCTRL_ZMODE_ZNE:
            return (z != old_z);
        case DWGCTRL_ZMODE_ZLT:
            return (z < old_z);
        case DWGCTRL_ZMODE_ZLTE:
            return (z <= old_z);
        case DWGCTRL_ZMODE_ZGT:
            return (z > old_z);
        case DWGCTRL_ZMODE_ZGTE:
            return (z >= old_z);

        case DWGCTRL_ZMODE_NOZCMP:
        default:
            return 1;
    }
}

static int
z_check_32(uint32_t z, uint32_t old_z, uint32_t z_mode) // mystique->dwgreg.dwgctrl & DWGCTRL_ZMODE_MASK)
{
    switch (z_mode) {
        case DWGCTRL_ZMODE_ZE:
            return (z == old_z);
        case DWGCTRL_ZMODE_ZNE:
            return (z != old_z);
        case DWGCTRL_ZMODE_ZLT:
            return (z < old_z);
        case DWGCTRL_ZMODE_ZLTE:
            return (z <= old_z);
        case DWGCTRL_ZMODE_ZGT:
            return (z > old_z);
        case DWGCTRL_ZMODE_ZGTE:
            return (z >= old_z);

        case DWGCTRL_ZMODE_NOZCMP:
        default:
            return 1;
    }
}

static void
blit_line(mystique_t *mystique, int closed, int autoline)
{
    svga_t  *svga = &mystique->svga;
    uint32_t src = 0;
    uint32_t dst;
    uint32_t old_dst;
    int      x = mystique->dwgreg.xdst;
    int      z_write;
    int      pattern_x, pattern_y;
    bool     transc = !!(mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC);

    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) {
        case DWGCTRL_ATYPE_RSTR:
        case DWGCTRL_ATYPE_RPL:
            while (mystique->dwgreg.length >= 0) {
                if (x >= mystique->dwgreg.cxleft && x <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot) {
                    pattern_y = ((mystique->dwgreg.funcnt % (mystique->dwgreg.stylelen + 1)) >> 4) & 0x7;
                    pattern_x = (mystique->dwgreg.funcnt % (mystique->dwgreg.stylelen + 1)) & 0xf;
                    if (!transc || (transc && (mystique->dwgreg.pattern[pattern_y][pattern_x])))
                    switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                        case MACCESS_PWIDTH_8:
                            src = mystique->dwgreg.pattern[pattern_y][pattern_x] ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                            dst = svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask];

                            dst                                                                              = bitop(src, dst, mystique->dwgreg.dwgctrl_running);
                            if (closed) {
                                svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask]                = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask) >> 12] = changeframecount;
                            } else if (!closed && (mystique->dwgreg.length > 0) && ((mystique->dwgreg.err > 0) || (mystique->dwgreg.err < 0)) && !autoline) {
                                svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask]                = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask) >> 12] = changeframecount;
                            } else if (!closed && (mystique->dwgreg.length > 0) && autoline) {
                                svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask]                = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask) >> 12] = changeframecount;
                            }
                            break;

                        case MACCESS_PWIDTH_16:
                            src = mystique->dwgreg.pattern[pattern_y][pattern_x] ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                            dst = ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w];

                            dst                                                                                = bitop(src, dst, mystique->dwgreg.dwgctrl_running);
                            if (closed) {
                                ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w] = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w) >> 11] = changeframecount;
                            } else if (!closed && (mystique->dwgreg.length > 0) && ((mystique->dwgreg.err > 0) || (mystique->dwgreg.err < 0)) && !autoline) {
                                ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w] = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w) >> 11] = changeframecount;
                            } else if (!closed && (mystique->dwgreg.length > 0) && autoline) {
                                ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w] = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w) >> 11] = changeframecount;
                            }
                            break;

                        case MACCESS_PWIDTH_24:
                            src = mystique->dwgreg.pattern[pattern_y][pattern_x] ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                            old_dst = *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask];

                            dst                                                                                    = bitop(src, old_dst, mystique->dwgreg.dwgctrl_running);
                            if (closed) {
                                *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask] = (dst & 0xffffff) | (old_dst & 0xff000000);
                                svga->changedvram[(((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask) >> 12] = changeframecount;
                            } else if (!closed && (mystique->dwgreg.length > 0) && ((mystique->dwgreg.err > 0) || (mystique->dwgreg.err < 0)) && !autoline) {
                                *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask] = (dst & 0xffffff) | (old_dst & 0xff000000);
                                svga->changedvram[(((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask) >> 12] = changeframecount;
                            } else if (!closed && (mystique->dwgreg.length > 0) && autoline) {
                                *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask] = (dst & 0xffffff) | (old_dst & 0xff000000);
                                svga->changedvram[(((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask) >> 12] = changeframecount;
                            }
                            break;

                        case MACCESS_PWIDTH_32:
                            src = mystique->dwgreg.pattern[pattern_y][pattern_x] ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                            dst = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l];

                            dst                                                                                = bitop(src, dst, mystique->dwgreg.dwgctrl_running);
                            if (closed) {
                                ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l] = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l) >> 10] = changeframecount;
                            } else if (!closed && (mystique->dwgreg.length > 0) && ((mystique->dwgreg.err > 0) || (mystique->dwgreg.err < 0)) && !autoline) {
                                ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l] = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l) >> 10] = changeframecount;
                            } else if (!closed && (mystique->dwgreg.length > 0) && autoline) {
                                ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l] = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l) >> 10] = changeframecount;
                            }
                            break;

                        default:
                            fatal("LINE RSTR/RPL PWIDTH %x %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK, mystique->dwgreg.dwgctrl_running);
                    }
                }

                if (!mystique->dwgreg.length)
                    break;

                if (mystique->dwgreg.sgn.sdydxl)
                    x += (mystique->dwgreg.sgn.sdxl ? -1 : 1);
                else {
                    mystique->dwgreg.ydst += (mystique->dwgreg.sgn.sdy ? -1 : 1);
                    mystique->dwgreg.ydst &= 0x7fffff;
                    mystique->dwgreg.ydst_lin += (mystique->dwgreg.sgn.sdy ? -(mystique->dwgreg.pitch & PITCH_MASK) : (mystique->dwgreg.pitch & PITCH_MASK));
                }
                if (mystique->dwgreg.err >= 0) {
                    mystique->dwgreg.err += mystique->dwgreg.k2;
                    if (mystique->dwgreg.sgn.sdydxl) {
                        mystique->dwgreg.ydst += (mystique->dwgreg.sgn.sdy ? -1 : 1);
                        mystique->dwgreg.ydst &= 0x7fffff;
                        mystique->dwgreg.ydst_lin += (mystique->dwgreg.sgn.sdy ? -(mystique->dwgreg.pitch & PITCH_MASK) : (mystique->dwgreg.pitch & PITCH_MASK));
                    } else
                        x += (mystique->dwgreg.sgn.sdxl ? -1 : 1);
                } else
                    mystique->dwgreg.err += mystique->dwgreg.k1;

                mystique->dwgreg.length--;
                mystique->dwgreg.funcnt--;
            }
            break;

        case DWGCTRL_ATYPE_I:
        case DWGCTRL_ATYPE_ZI:
            z_write = ((mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) == DWGCTRL_ATYPE_ZI);
            while (mystique->dwgreg.length > 0) {
                if (x >= mystique->dwgreg.cxleft && x <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot) {
                    bool z_check_pass = false;
                    if (mystique->maccess_running & MACCESS_ZWIDTH) {
                        uint32_t  z     = (mystique->dwgreg.extended_dr[0] & (1ull << 47ull)) ? 0 : (mystique->dwgreg.extended_dr[0] >> 15ull);
                        uint32_t *z_p   = (uint32_t *) &svga->vram[(mystique->dwgreg.ydst_lin * 4 + mystique->dwgreg.zorg) & mystique->vram_mask];
                        uint32_t  old_z = z_p[x];
                        z_check_pass = z_check_32(z, old_z, mystique->dwgreg.dwgctrl_running & DWGCTRL_ZMODE_MASK);
                        if (z_write && z_check_pass) {
                            z_p[x] = z;
                        }
                    } else {
                        uint16_t  z     = ((int32_t) mystique->dwgreg.dr[0] < 0) ? 0 : (mystique->dwgreg.dr[0] >> 15);
                        uint16_t *z_p   = (uint16_t *) &svga->vram[(mystique->dwgreg.ydst_lin * 2 + mystique->dwgreg.zorg) & mystique->vram_mask];
                        uint16_t  old_z = z_p[x];
                        z_check_pass = z_check(z, old_z, mystique->dwgreg.dwgctrl_running & DWGCTRL_ZMODE_MASK);
                        if (z_write && z_check_pass) {
                            z_p[x] = z;
                        }
                    }

                    if (z_check_pass) {
                        int r = 0;
                        int g = 0;
                        int b = 0;

                        switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                            case MACCESS_PWIDTH_8:
                                if (!(mystique->dwgreg.dr[4] & (1 << 23)))
                                    r = (mystique->dwgreg.dr[4] >> 20) & 0x7;
                                if (!(mystique->dwgreg.dr[8] & (1 << 23)))
                                    g = (mystique->dwgreg.dr[8] >> 20) & 0x7;
                                if (!(mystique->dwgreg.dr[12] & (1 << 23)))
                                    b = (mystique->dwgreg.dr[12] >> 21) & 0x3;
                                dst = (r << 5) | (g << 2) | b;

                                ((uint8_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask] = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask) >> 12] = changeframecount;
                                break;

                            case MACCESS_PWIDTH_16:
                                if (!(mystique->dwgreg.dr[4] & (1 << 23)))
                                    r = (mystique->dwgreg.dr[4] >> 18) & 0x1f;
                                if (!(mystique->dwgreg.dr[8] & (1 << 23)))
                                    g = (mystique->dwgreg.dr[8] >> 17) & 0x3f;
                                if (!(mystique->dwgreg.dr[12] & (1 << 23)))
                                    b = (mystique->dwgreg.dr[12] >> 18) & 0x1f;
                                dst = (r << 11) | (g << 5) | b;

                                ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w] = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w) >> 11] = changeframecount;
                                break;

                            case MACCESS_PWIDTH_24:
                                old_dst = *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask];
                                if (!(mystique->dwgreg.dr[4] & (1 << 23)))
                                    r = (mystique->dwgreg.dr[4] >> 15) & 0xff;
                                if (!(mystique->dwgreg.dr[8] & (1 << 23)))
                                    g = (mystique->dwgreg.dr[8] >> 15) & 0xff;
                                if (!(mystique->dwgreg.dr[12] & (1 << 23)))
                                    b = (mystique->dwgreg.dr[12] >> 15) & 0xff;
                                dst = (r << 16) | (g << 8) | b;

                                ((uint32_t *) svga->vram)[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask] = dst | (old_dst & 0xFF000000);
                                svga->changedvram[(((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask) >> 12] = changeframecount;
                                break;

                            case MACCESS_PWIDTH_32:
                                if (!(mystique->dwgreg.dr[4] & (1 << 23)))
                                    r = (mystique->dwgreg.dr[4] >> 15) & 0xff;
                                if (!(mystique->dwgreg.dr[8] & (1 << 23)))
                                    g = (mystique->dwgreg.dr[8] >> 15) & 0xff;
                                if (!(mystique->dwgreg.dr[12] & (1 << 23)))
                                    b = (mystique->dwgreg.dr[12] >> 15) & 0xff;
                                dst = (r << 16) | (g << 8) | b;

                                ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l] = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l) >> 10] = changeframecount;
                                break;

                            default:
                                fatal("LINE I/ZI PWIDTH %x %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK, mystique->dwgreg.dwgctrl_running);
                        }
                    }
                }

                if (mystique->dwgreg.sgn.sdydxl)
                    x += (mystique->dwgreg.sgn.sdxl ? -1 : 1);
                else
                    mystique->dwgreg.ydst_lin += (mystique->dwgreg.sgn.sdy ? -(mystique->dwgreg.pitch & PITCH_MASK) : (mystique->dwgreg.pitch & PITCH_MASK));

                if (mystique->maccess_running & MACCESS_ZWIDTH) {
                    mystique->dwgreg.extended_dr[0] += mystique->dwgreg.extended_dr[2];
                    mystique->dwgreg.dr[0] = (mystique->dwgreg.extended_dr[0] >> 16) & 0xFFFFFFFF;
                } else {
                    mystique->dwgreg.dr[0] += mystique->dwgreg.dr[2];
                    mystique->dwgreg.extended_dr[0] = (mystique->dwgreg.extended_dr[0] & ~0xFFFFull) | ((uint64_t)mystique->dwgreg.dr[0] << 16ull);
                }
                mystique->dwgreg.dr[4] += mystique->dwgreg.dr[6];
                mystique->dwgreg.dr[8] += mystique->dwgreg.dr[10];
                mystique->dwgreg.dr[12] += mystique->dwgreg.dr[14];

                if (mystique->dwgreg.err >= 0) {
                    mystique->dwgreg.err += mystique->dwgreg.k2;

                    if (mystique->dwgreg.sgn.sdydxl)
                        mystique->dwgreg.ydst_lin += (mystique->dwgreg.sgn.sdy ? -(mystique->dwgreg.pitch & PITCH_MASK) : (mystique->dwgreg.pitch & PITCH_MASK));
                    else
                        x += (mystique->dwgreg.sgn.sdxl ? -1 : 1);

                    if (mystique->maccess_running & MACCESS_ZWIDTH) {
                        mystique->dwgreg.extended_dr[0] += mystique->dwgreg.extended_dr[3];
                        mystique->dwgreg.dr[0] = (mystique->dwgreg.extended_dr[0] >> 16) & 0xFFFFFFFF;
                    } else {
                        mystique->dwgreg.dr[0] += mystique->dwgreg.dr[3];
                        mystique->dwgreg.extended_dr[0] = (mystique->dwgreg.extended_dr[0] & ~0xFFFFull) | ((uint64_t)mystique->dwgreg.dr[0] << 16ull);
                    }
                    mystique->dwgreg.dr[4] += mystique->dwgreg.dr[7];
                    mystique->dwgreg.dr[8] += mystique->dwgreg.dr[11];
                    mystique->dwgreg.dr[12] += mystique->dwgreg.dr[15];
                } else
                    mystique->dwgreg.err += mystique->dwgreg.k1;

                mystique->dwgreg.length--;
            }
            break;

        default:
#if 0
            pclog("Unknown atype %03x %08x LINE\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK, mystique->dwgreg.dwgctrl_running);
#endif
            break;
    }

    mystique->blitter_complete_refcount++;
}

static void
blit_line_start(mystique_t *mystique, int closed, int autoline)
{
    int start_x = (int32_t) mystique->dwgreg.ar[5];
    int start_y = (int32_t) mystique->dwgreg.ar[6];
    int end_x   = (int32_t) mystique->dwgreg.ar[0];
    int end_y   = (int32_t) mystique->dwgreg.ar[2];
    int dx      = end_x - start_x;
    int dy      = end_y - start_y;

    if (autoline) {
        if (ABS(dx) > ABS(dy)) {
            mystique->dwgreg.sgn.sdydxl = 1;
            mystique->dwgreg.k1         = 2 * ABS(dy);
            mystique->dwgreg.err        = 2 * ABS(dy) - ABS(dx) - ((start_y > end_y) ? 1 : 0);
            mystique->dwgreg.k2         = 2 * ABS(dy) - 2 * ABS(dx);
            mystique->dwgreg.length     = ABS(end_x - start_x);
        } else {
            mystique->dwgreg.sgn.sdydxl = 0;
            mystique->dwgreg.k1         = 2 * ABS(dx);
            mystique->dwgreg.err        = 2 * ABS(dx) - ABS(dy) - ((start_y > end_y) ? 1 : 0);
            mystique->dwgreg.k2         = 2 * ABS(dx) - 2 * ABS(dy);
            mystique->dwgreg.length     = ABS(end_y - start_y);
        }
        mystique->dwgreg.sgn.sdxl = (start_x > end_x) ? 1 : 0;
        mystique->dwgreg.sgn.sdy  = (start_y > end_y) ? 1 : 0;
    } else {
        mystique->dwgreg.k1  = (int32_t) mystique->dwgreg.ar[0];
        mystique->dwgreg.err = (int32_t) mystique->dwgreg.ar[1];
        mystique->dwgreg.k2  = (int32_t) mystique->dwgreg.ar[2];
    }

    blit_line(mystique, closed, autoline);

    if (autoline) {
        mystique->dwgreg.ar[5]    = end_x;
        mystique->dwgreg.xdst     = end_x;
        mystique->dwgreg.ar[6]    = end_y;
        mystique->dwgreg.ydst     = end_y;
        mystique->dwgreg.ydst_lin = ((int32_t) (int16_t) mystique->dwgreg.ydst * (mystique->dwgreg.pitch & PITCH_MASK)) + mystique->dwgreg.ydstorg;
    }
}

static void
blit_trap(mystique_t *mystique)
{
    svga_t   *svga = &mystique->svga;
    uint64_t  z_back_32;
    uint32_t  z_back;
    uint32_t  r_back;
    uint32_t  g_back;
    uint32_t  b_back;
    int       z_write;
    int       y;
    int       err_l = (int32_t)mystique->dwgreg.ar[1];
    int       err_r = (int32_t)mystique->dwgreg.ar[4];
    const int trans_sel = (mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANS_MASK) >> DWGCTRL_TRANS_SHIFT;

    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) {
        case DWGCTRL_ATYPE_BLK:
        case DWGCTRL_ATYPE_RPL:
            for (y = 0; y < mystique->dwgreg.length; y++) {
                uint8_t const *const trans = &trans_masks[trans_sel][(mystique->dwgreg.selline & 3) * 4];
                int16_t              x_l   = mystique->dwgreg.fxleft & 0xffff;
                int16_t              x_r   = mystique->dwgreg.fxright & 0xffff;
                int                  yoff  = (mystique->dwgreg.yoff + mystique->dwgreg.ydst) & 7;
                int                  len;

                if (x_l > x_r)
                    len = x_l - x_r;
                else
                    len = x_r - x_l;

                while (len > 0) {
                    if (x_l >= mystique->dwgreg.cxleft && x_l <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && trans[x_l & 3]) {
                        int      xoff    = (mystique->dwgreg.xoff + (x_l & 7)) & 15;
                        int      pattern = mystique->dwgreg.pattern[yoff][xoff];
                        uint32_t dst;

                        switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                            case MACCESS_PWIDTH_8:
                                svga->vram[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask]                = (pattern ? mystique->dwgreg.fcol : mystique->dwgreg.bcol) & 0xff;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask) >> 12] = changeframecount;
                                break;

                            case MACCESS_PWIDTH_16:
                                ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_w] = (pattern ? mystique->dwgreg.fcol : mystique->dwgreg.bcol) & 0xffff;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_w) >> 11] = changeframecount;
                                break;

                            case MACCESS_PWIDTH_24:
                                dst                                                                                        = *(uint32_t *) (&svga->vram[((mystique->dwgreg.ydst_lin + x_l) * 3) & mystique->vram_mask]) & 0xff000000;
                                *(uint32_t *) (&svga->vram[((mystique->dwgreg.ydst_lin + x_l) * 3) & mystique->vram_mask]) = ((pattern ? mystique->dwgreg.fcol : mystique->dwgreg.bcol) & 0xffffff) | dst;
                                svga->changedvram[(((mystique->dwgreg.ydst_lin + x_l) * 3) & mystique->vram_mask) >> 12]   = changeframecount;
                                break;

                            case MACCESS_PWIDTH_32:
                                ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_l] = pattern ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_l) >> 10] = changeframecount;
                                break;

                            default:
                                fatal("TRAP BLK/RPL PWIDTH %x %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK, mystique->dwgreg.dwgctrl_running);
                        }
                    }
                    len--;
                    x_l++;
                }

                while ((err_l < 0) && mystique->dwgreg.ar[0]) {
                    err_l += mystique->dwgreg.ar[0];
                    mystique->dwgreg.fxleft += (mystique->dwgreg.sgn.sdxl ? -1 : 1);
                }
                err_l += mystique->dwgreg.ar[2];

                while ((err_r < 0) && mystique->dwgreg.ar[6]) {
                    err_r += mystique->dwgreg.ar[6];
                    mystique->dwgreg.fxright += (mystique->dwgreg.sgn.sdxr ? -1 : 1);
                }
                err_r += mystique->dwgreg.ar[5];

                mystique->dwgreg.ydst++;
                mystique->dwgreg.ydst &= 0x7fffff;
                mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);

                mystique->dwgreg.selline = (mystique->dwgreg.selline + 1) & 7;
            }
            break;

        case DWGCTRL_ATYPE_RSTR:
            for (y = 0; y < mystique->dwgreg.length; y++) {
                uint8_t const *const trans = &trans_masks[trans_sel][(mystique->dwgreg.selline & 3) * 4];
                int16_t              x_l   = mystique->dwgreg.fxleft & 0xffff;
                int16_t              x_r   = mystique->dwgreg.fxright & 0xffff;
                int                  yoff  = (mystique->dwgreg.yoff + mystique->dwgreg.ydst) & 7;
                int                  len;

                if (x_l > x_r)
                    len = x_l - x_r;
                else
                    len = x_r - x_l;

                while (len > 0) {
                    if (x_l >= mystique->dwgreg.cxleft && x_l <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && trans[x_l & 3]) {
                        int      xoff    = (mystique->dwgreg.xoff + (x_l & 7)) & 15;
                        int      pattern = mystique->dwgreg.pattern[yoff][xoff];
                        uint32_t src     = pattern ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                        uint32_t dst;
                        uint32_t old_dst;

                        switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                            case MACCESS_PWIDTH_8:
                                dst = svga->vram[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask];

                                dst                                                                                = bitop(src, dst, mystique->dwgreg.dwgctrl_running);
                                svga->vram[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask]                = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask) >> 12] = changeframecount;
                                break;

                            case MACCESS_PWIDTH_16:
                                dst = ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_w];

                                dst                                                                                  = bitop(src, dst, mystique->dwgreg.dwgctrl_running);
                                ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_w] = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_w) >> 11] = changeframecount;
                                break;

                            case MACCESS_PWIDTH_24:
                                old_dst = *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x_l) * 3) & mystique->vram_mask];

                                dst                                                                                      = bitop(src, old_dst, mystique->dwgreg.dwgctrl_running);
                                *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x_l) * 3) & mystique->vram_mask] = (dst & 0xffffff) | (old_dst & 0xff000000);
                                svga->changedvram[(((mystique->dwgreg.ydst_lin + x_l) * 3) & mystique->vram_mask) >> 12] = changeframecount;
                                break;

                            case MACCESS_PWIDTH_32:
                                dst = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_l];

                                dst                                                                                  = bitop(src, dst, mystique->dwgreg.dwgctrl_running);
                                ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_l] = dst;
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_l) >> 10] = changeframecount;
                                break;

                            default:
                                fatal("TRAP RSTR PWIDTH %x %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK, mystique->dwgreg.dwgctrl_running);
                        }
                    }
                    x_l++;
                    len--;
                }

                while ((err_l < 0) && mystique->dwgreg.ar[0]) {
                    err_l += mystique->dwgreg.ar[0];
                    mystique->dwgreg.fxleft += (mystique->dwgreg.sgn.sdxl ? -1 : 1);
                }
                err_l += mystique->dwgreg.ar[2];

                while ((err_r < 0) && mystique->dwgreg.ar[6]) {
                    err_r += mystique->dwgreg.ar[6];
                    mystique->dwgreg.fxright += (mystique->dwgreg.sgn.sdxr ? -1 : 1);
                }
                err_r += mystique->dwgreg.ar[5];

                mystique->dwgreg.ydst++;
                mystique->dwgreg.ydst &= 0x7fffff;
                mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);

                mystique->dwgreg.selline = (mystique->dwgreg.selline + 1) & 7;
            }
            break;

        case DWGCTRL_ATYPE_I:
        case DWGCTRL_ATYPE_ZI:
            z_write = ((mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) == DWGCTRL_ATYPE_ZI);

            for (y = 0; y < mystique->dwgreg.length; y++) {
                uint8_t const *const trans   = &trans_masks[trans_sel][(mystique->dwgreg.selline & 3) * 4];
                uint16_t            *z_p     = (uint16_t *) &svga->vram[(mystique->dwgreg.ydst_lin * ((mystique->maccess_running & MACCESS_ZWIDTH) ? 4 : 2) + mystique->dwgreg.zorg) & mystique->vram_mask];
                int16_t              x_l     = mystique->dwgreg.fxleft & 0xffff;
                int16_t              x_r     = mystique->dwgreg.fxright & 0xffff;
                int16_t              old_x_l = x_l;
                int                  dx;

                z_back_32 = mystique->dwgreg.extended_dr[0];

                z_back = mystique->dwgreg.dr[0];
                r_back = mystique->dwgreg.dr[4];
                g_back = mystique->dwgreg.dr[8];
                b_back = mystique->dwgreg.dr[12];

                while (x_l != x_r) {
                    if (x_l >= mystique->dwgreg.cxleft && x_l <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && trans[x_l & 3]) {
                        bool z_check_pass = false;
                        if (mystique->maccess_running & MACCESS_ZWIDTH) {
                            uint32_t z     = (mystique->dwgreg.extended_dr[0] & (1ull << 47ull)) ? 0 : (mystique->dwgreg.extended_dr[0] >> 15ull);
                            uint32_t old_z = *(uint32_t*)&z_p[x_l * 2];
                            z_check_pass = z_check_32(z, old_z, mystique->dwgreg.dwgctrl_running & DWGCTRL_ZMODE_MASK);
                        } else {
                            uint16_t z     = ((int32_t) mystique->dwgreg.dr[0] < 0) ? 0 : (mystique->dwgreg.dr[0] >> 15);
                            uint16_t old_z = z_p[x_l];
                            z_check_pass = z_check(z, old_z, mystique->dwgreg.dwgctrl_running & DWGCTRL_ZMODE_MASK);
                        }

                        if (z_check_pass) {
                            uint32_t dst = 0;
                            uint32_t old_dst;
                            int      r = 0;
                            int      g = 0;
                            int      b = 0;

                            if (!(mystique->dwgreg.dr[4] & (1 << 23)))
                                r = (mystique->dwgreg.dr[4] >> 15) & 0xff;
                            if (!(mystique->dwgreg.dr[8] & (1 << 23)))
                                g = (mystique->dwgreg.dr[8] >> 15) & 0xff;
                            if (!(mystique->dwgreg.dr[12] & (1 << 23)))
                                b = (mystique->dwgreg.dr[12] >> 15) & 0xff;

                            if (z_write) {
                                if (mystique->maccess_running & MACCESS_ZWIDTH) {
                                    *(uint32_t*)(&z_p[x_l * 2]) = (mystique->dwgreg.extended_dr[0] & (1ull << 47ull)) ? 0 : (mystique->dwgreg.extended_dr[0] >> 15ull);
                                }
                                else
                                    z_p[x_l] = ((int32_t) mystique->dwgreg.dr[0] < 0) ? 0 : (mystique->dwgreg.dr[0] >> 15);
                            }

                            switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                                case MACCESS_PWIDTH_8:
                                    svga->vram[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask]                = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask) >> 12] = changeframecount;
                                    break;

                                case MACCESS_PWIDTH_16:
                                    dst                                                                                  = dither(mystique, r, g, b, x_l & 1, mystique->dwgreg.selline & 1);
                                    ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_w] = dst;
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_w) >> 11] = changeframecount;
                                    break;

                                case MACCESS_PWIDTH_24:
                                    old_dst                                                                                    = *(uint32_t *) (&svga->vram[((mystique->dwgreg.ydst_lin + x_l) * 3) & mystique->vram_mask]) & 0xff000000;
                                    *(uint32_t *) (&svga->vram[((mystique->dwgreg.ydst_lin + x_l) * 3) & mystique->vram_mask]) = old_dst | dst;
                                    svga->changedvram[(((mystique->dwgreg.ydst_lin + x_l) * 3) & mystique->vram_mask) >> 12]   = changeframecount;
                                    break;

                                case MACCESS_PWIDTH_32:
                                    ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_l] = b | (g << 8) | (r << 16);
                                    svga->changedvram[((mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_l) >> 10] = changeframecount;
                                    break;

                                default:
                                    fatal("TRAP BLK/RPL PWIDTH %x %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK, mystique->dwgreg.dwgctrl_running);
                            }
                        }
                    }

                    if (mystique->maccess_running & MACCESS_ZWIDTH) {
                        mystique->dwgreg.extended_dr[0] += mystique->dwgreg.extended_dr[2];
                        mystique->dwgreg.dr[0] = (mystique->dwgreg.extended_dr[0] >> 16) & 0xFFFFFFFF;
                    } else {
                        mystique->dwgreg.dr[0] += mystique->dwgreg.dr[2];
                        mystique->dwgreg.extended_dr[0] = (mystique->dwgreg.extended_dr[0] & ~0xFFFFull) | ((uint64_t)mystique->dwgreg.dr[0] << 16ull);
                    }
                    mystique->dwgreg.dr[4] += mystique->dwgreg.dr[6];
                    mystique->dwgreg.dr[8] += mystique->dwgreg.dr[10];
                    mystique->dwgreg.dr[12] += mystique->dwgreg.dr[14];

                    if (x_l > x_r)
                        x_l--;
                    else
                        x_l++;
                }

                if (mystique->maccess_running & MACCESS_ZWIDTH) {
                    mystique->dwgreg.extended_dr[0] = z_back_32 + mystique->dwgreg.extended_dr[3];
                    mystique->dwgreg.dr[0] = (mystique->dwgreg.extended_dr[0] >> 16) & 0xFFFFFFFF;
                } else {
                    mystique->dwgreg.dr[0] = z_back + mystique->dwgreg.dr[3];
                    mystique->dwgreg.extended_dr[0] = (mystique->dwgreg.extended_dr[0] & ~0xFFFFull) | ((uint64_t)mystique->dwgreg.dr[0] << 16ull);
                }
                mystique->dwgreg.dr[4]  = r_back + mystique->dwgreg.dr[7];
                mystique->dwgreg.dr[8]  = g_back + mystique->dwgreg.dr[11];
                mystique->dwgreg.dr[12] = b_back + mystique->dwgreg.dr[15];

                while ((int32_t) mystique->dwgreg.ar[1] < 0 && mystique->dwgreg.ar[0]) {
                    mystique->dwgreg.ar[1] += mystique->dwgreg.ar[0];
                    mystique->dwgreg.fxleft += (mystique->dwgreg.sgn.sdxl ? -1 : 1);
                }
                mystique->dwgreg.ar[1] += mystique->dwgreg.ar[2];

                while ((int32_t) mystique->dwgreg.ar[4] < 0 && mystique->dwgreg.ar[6]) {
                    mystique->dwgreg.ar[4] += mystique->dwgreg.ar[6];
                    mystique->dwgreg.fxright += (mystique->dwgreg.sgn.sdxr ? -1 : 1);
                }
                mystique->dwgreg.ar[4] += mystique->dwgreg.ar[5];

                dx = (int16_t) ((mystique->dwgreg.fxleft - old_x_l) & 0xffff);
                if (mystique->maccess_running & MACCESS_ZWIDTH) {
                    mystique->dwgreg.extended_dr[0] += dx * mystique->dwgreg.extended_dr[2];
                    mystique->dwgreg.dr[0] = (mystique->dwgreg.extended_dr[0] >> 16) & 0xFFFFFFFF;
                } else {
                    mystique->dwgreg.dr[0] += dx * mystique->dwgreg.dr[2];
                    mystique->dwgreg.extended_dr[0] = (mystique->dwgreg.extended_dr[0] & ~0xFFFFull) | ((uint64_t)mystique->dwgreg.dr[0] << 16ull);
                }
                mystique->dwgreg.dr[4] += dx * mystique->dwgreg.dr[6];
                mystique->dwgreg.dr[8] += dx * mystique->dwgreg.dr[10];
                mystique->dwgreg.dr[12] += dx * mystique->dwgreg.dr[14];

                mystique->dwgreg.ydst++;
                mystique->dwgreg.ydst &= 0x7fffff;
                mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);

                mystique->dwgreg.selline = (mystique->dwgreg.selline + 1) & 7;
            }
            break;

        default:
#if 0
            pclog("Unknown atype %03x %08x TRAP\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK, mystique->dwgreg.dwgctrl_running);
#endif
            break;
    }

    mystique->blitter_complete_refcount++;
}

static uint16_t texture_texel_fetch(mystique_t *mystique, int *tex_r, int *tex_g, int *tex_b, int *tex_a, int *atransp, int s, int t, int tex_pitch)
{
    const unsigned int w_mask    = (mystique->dwgreg.texwidth & TEXWIDTH_TWMASK_MASK) >> TEXWIDTH_TWMASK_SHIFT;
    const unsigned int h_mask    = (mystique->dwgreg.texheight & TEXHEIGHT_THMASK_MASK) >> TEXHEIGHT_THMASK_SHIFT;
    const unsigned int palsel    = mystique->dwgreg.texctl & TEXCTL_PALSEL_MASK;
    svga_t*            svga      = &mystique->svga;
    uint16_t           src       = 0x0;

    int atransp_dummy = 0;

    if (!atransp)
        atransp = &atransp_dummy;

    if (mystique->dwgreg.texctl & TEXCTL_CLAMPU) {
        if (s < 0)
            s = 0;
        else if (s > w_mask)
            s = w_mask;
    } else
        s &= w_mask;

    if (mystique->dwgreg.texctl & TEXCTL_CLAMPV) {
        if (t < 0)
            t = 0;
        else if (t > h_mask)
            t = h_mask;
    } else
        t &= h_mask;

    switch (mystique->dwgreg.texctl & TEXCTL_TEXFORMAT_MASK) {
        case TEXCTL_TEXFORMAT_TW4:
            src = svga->vram[(mystique->dwgreg.texorg + (((t * tex_pitch) + s) >> 1)) & mystique->vram_mask];
            if (s & 1)
                src >>= 4;
            else
                src &= 0xf;
            *tex_r   = mystique->lut[src | palsel].r;
            *tex_g   = mystique->lut[src | palsel].g;
            *tex_b   = mystique->lut[src | palsel].b;
            *atransp = 0;
            break;
        case TEXCTL_TEXFORMAT_TW8:
            src      = svga->vram[(mystique->dwgreg.texorg + (t * tex_pitch) + s) & mystique->vram_mask];
            *tex_r   = mystique->lut[src].r;
            *tex_g   = mystique->lut[src].g;
            *tex_b   = mystique->lut[src].b;
            *atransp = 0;
            break;
        case TEXCTL_TEXFORMAT_TW15:
            src    = ((uint16_t *) svga->vram)[((mystique->dwgreg.texorg >> 1) + (t * tex_pitch) + s) & mystique->vram_mask_w];
            *tex_r = ((src >> 10) & 0x1f) << 3;
            *tex_g = ((src >> 5) & 0x1f) << 3;
            *tex_b = (src & 0x1f) << 3;
            if (((src >> 15) & mystique->dwgreg.ta_mask) == mystique->dwgreg.ta_key)
                *atransp = 1;
            else
                *atransp = 0;
            break;
        case TEXCTL_TEXFORMAT_TW12:
            src    = ((uint16_t *) svga->vram)[((mystique->dwgreg.texorg >> 1) + (t * tex_pitch) + s) & mystique->vram_mask_w];
            *tex_r = ((src >> 8) & 0xf) << 4;
            *tex_g = ((src >> 4) & 0xf) << 4;
            *tex_b = (src & 0xf) << 4;
            *tex_a = ((src >> 12) & 0xf) << 4;
            if (mystique->dwgreg.texctl & TEXCTL_AZEROEXTEND) {
                *atransp = (((src >> 12) & 0xf) & mystique->dwgreg.ta_mask)  == mystique->dwgreg.ta_key;
            } else {
                uint8_t ta_mask = mystique->dwgreg.ta_mask ? 0xf : 0x0;
                uint8_t ta_key = mystique->dwgreg.ta_key ? 0xf : 0x0;
                *atransp = (((src >> 12) & 0xf) & ta_mask) == ta_key;
            }
            break;
        case TEXCTL_TEXFORMAT_TW16:
            src      = ((uint16_t *) svga->vram)[((mystique->dwgreg.texorg >> 1) + (t * tex_pitch) + s) & mystique->vram_mask_w];
            *tex_r   = (src >> 11) << 3;
            *tex_g   = ((src >> 5) & 0x3f) << 2;
            *tex_b   = (src & 0x1f) << 3;
            *atransp = 0;
            break;
        default:
            fatal("Unknown texture format %i\n", mystique->dwgreg.texctl & TEXCTL_TEXFORMAT_MASK);
            break;
    }
    return src;
}

static double lerp(double v0, double v1, double t) {
  return (1. - t) * v0 + t * v1;
}

static int
texture_read(mystique_t *mystique, int *tex_r, int *tex_g, int *tex_b, int *atransp, int *tex_a)
{
    const int          tex_shift = 3 + ((mystique->dwgreg.texctl & TEXCTL_TPITCH_MASK) >> TEXCTL_TPITCH_SHIFT);
    const uint16_t     tckey     = mystique->dwgreg.textrans & TEXTRANS_TCKEY_MASK;
    const uint16_t     tkmask    = (mystique->dwgreg.textrans & TEXTRANS_TKMASK_MASK) >> TEXTRANS_TKMASK_SHIFT;
    const unsigned int w_mask    = (mystique->dwgreg.texwidth & TEXWIDTH_TWMASK_MASK) >> TEXWIDTH_TWMASK_SHIFT;
    const unsigned int h_mask    = (mystique->dwgreg.texheight & TEXHEIGHT_THMASK_MASK) >> TEXHEIGHT_THMASK_SHIFT;
    uint16_t           src       = 0;
    int                s;
    int                t;
    int                tex_pitch = 1 << tex_shift;
    double             s_frac = 0;
    double             t_frac = 0;

    *tex_a = 255;

    if (mystique->type >= MGA_G100 && (mystique->dwgreg.texctl & TEXCTL_TPITCHLIN))
    {
        tex_pitch = (mystique->dwgreg.texctl & TEXCTL_TPITCHEXT_MASK) >> 9;
        if (tex_pitch == 0)
            tex_pitch = 2048;
    }

    if (mystique->dwgreg.texctl & TEXCTL_NPCEN) {
        const int s_shift = 20 - (mystique->dwgreg.texwidth & TEXWIDTH_TW_MASK);
        const int t_shift = 20 - (mystique->dwgreg.texheight & TEXHEIGHT_TH_MASK);

        s = (int32_t) mystique->dwgreg.tmr[6] >> s_shift;
        t = (int32_t) mystique->dwgreg.tmr[7] >> t_shift;
        s_frac = (((int32_t) mystique->dwgreg.tmr[6]) & ((1 << s_shift) - 1)) / (double)(1 << s_shift);
        t_frac = (((int32_t) mystique->dwgreg.tmr[7]) & ((1 << t_shift) - 1)) / (double)(1 << t_shift);
    } else {
        const int s_shift = (20 + 16) - (mystique->dwgreg.texwidth & TEXWIDTH_TW_MASK);
        const int t_shift = (20 + 16) - (mystique->dwgreg.texheight & TEXHEIGHT_TH_MASK);
        int64_t   q       = mystique->dwgreg.tmr[8] ? (0x100000000LL / (int64_t) (int32_t) mystique->dwgreg.tmr[8]) : 0;

        s = ((int64_t) (int32_t) mystique->dwgreg.tmr[6] * q) >> s_shift;
        t = ((int64_t) (int32_t) mystique->dwgreg.tmr[7] * q) >> t_shift;
        s_frac = (((int64_t) (int32_t) mystique->dwgreg.tmr[6] * q) & ((1 << s_shift) - 1)) / (double)(1 << s_shift);
        t_frac = (((int64_t) (int32_t) mystique->dwgreg.tmr[7] * q) & ((1 << t_shift) - 1)) / (double)(1 << t_shift);
    }

    if (mystique->dwgreg.texctl & TEXCTL_CLAMPU) {
        if (s < 0)
            s = 0;
        else if (s > w_mask)
            s = w_mask;
    } else
        s &= w_mask;

    if (mystique->dwgreg.texctl & TEXCTL_CLAMPV) {
        if (t < 0)
            t = 0;
        else if (t > h_mask)
            t = h_mask;
    } else
        t &= h_mask;

    src = texture_texel_fetch(mystique, tex_r, tex_g, tex_b, tex_a, atransp, s, t, tex_pitch);
    switch (mystique->dwgreg.texfilter & 3)
    {
        case 0:
            s_frac = t_frac = 0;
            break;
        case 1:
        case 2:
            break;
        case 3:
            s_frac = t_frac = .25;
            break;
    }
    if (s_frac && s != w_mask)
    {
        int s_tex_r = 0, s_tex_g = 0, s_tex_b = 0, s_tex_a = 255;
        texture_texel_fetch(mystique, &s_tex_r, &s_tex_g, &s_tex_b, &s_tex_a, NULL, s + 1, t, tex_pitch);
        *tex_r = (int)lerp(*tex_r, s_tex_r, s_frac);
        *tex_g = (int)lerp(*tex_g, s_tex_g, s_frac);
        *tex_b = (int)lerp(*tex_b, s_tex_b, s_frac);
        *tex_a = (int)lerp(*tex_a, s_tex_a, s_frac);
        if (*tex_r > 255) *tex_r = 255;
        if (*tex_g > 255) *tex_g = 255;
        if (*tex_b > 255) *tex_b = 255;
        if (*tex_a > 255) *tex_a = 255;
    }
    if (t_frac && t != h_mask)
    {
        int t_tex_r = 0, t_tex_g = 0, t_tex_b = 0, t_tex_a = 255;
        texture_texel_fetch(mystique, &t_tex_r, &t_tex_g, &t_tex_b, &t_tex_a, NULL, s, t + 1, tex_pitch);
        *tex_r = (int)lerp(*tex_r, t_tex_r, t_frac);
        *tex_g = (int)lerp(*tex_g, t_tex_g, t_frac);
        *tex_b = (int)lerp(*tex_b, t_tex_b, t_frac);
        *tex_a = (int)lerp(*tex_a, t_tex_a, t_frac);
        if (*tex_r > 255) *tex_r = 255;
        if (*tex_g > 255) *tex_g = 255;
        if (*tex_b > 255) *tex_b = 255;
        if (*tex_a > 255) *tex_a = 255;
    }

    return ((src & tkmask) == tckey);
}

static void
blit_texture_trap(mystique_t *mystique)
{
    svga_t   *svga = &mystique->svga;
    int       y;
    int       z_write;
    const int trans_sel = (mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANS_MASK) >> DWGCTRL_TRANS_SHIFT;
    const int dest32    = ((mystique->maccess_running & MACCESS_PWIDTH_MASK) == MACCESS_PWIDTH_32);

    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) {
        case DWGCTRL_ATYPE_I:
        case DWGCTRL_ATYPE_ZI:
            z_write = ((mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) == DWGCTRL_ATYPE_ZI);

            for (y = 0; y < mystique->dwgreg.length; y++) {
                uint8_t const *const trans   = &trans_masks[trans_sel][(mystique->dwgreg.selline & 3) * 4];
                uint16_t            *z_p     = (uint16_t *) &svga->vram[(mystique->dwgreg.ydst_lin * ((mystique->maccess_running & MACCESS_ZWIDTH) ? 4 : 2) + mystique->dwgreg.zorg) & mystique->vram_mask];
                int16_t              x_l     = mystique->dwgreg.fxleft & 0xffff;
                int16_t              x_r     = mystique->dwgreg.fxright & 0xffff;
                int16_t              old_x_l = x_l;
                int                  dx;

                uint64_t z_back_32 = mystique->dwgreg.extended_dr[0];

                uint32_t z_back = mystique->dwgreg.dr[0];
                uint32_t r_back = mystique->dwgreg.dr[4];
                uint32_t g_back = mystique->dwgreg.dr[8];
                uint32_t b_back = mystique->dwgreg.dr[12];
                uint32_t s_back = mystique->dwgreg.tmr[6];
                uint32_t t_back = mystique->dwgreg.tmr[7];
                uint32_t q_back = mystique->dwgreg.tmr[8];
                uint32_t a_back = mystique->dwgreg.alphastart;
                uint32_t fog_back = mystique->dwgreg.fogstart;

                while (x_l != x_r) {
                    if (x_l >= mystique->dwgreg.cxleft && x_l <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && trans[x_l & 3]) {
                        bool z_check_pass = false;
                        if (mystique->maccess_running & MACCESS_ZWIDTH) {
                            uint32_t z     = (mystique->dwgreg.extended_dr[0] & (1ull << 47ull)) ? 0 : (mystique->dwgreg.extended_dr[0] >> 15ull);
                            uint32_t old_z = *(uint32_t*)&z_p[x_l * 2];
                            z_check_pass = z_check_32(z, old_z, mystique->dwgreg.dwgctrl_running & DWGCTRL_ZMODE_MASK);
                        } else {
                            uint16_t z     = ((int32_t) mystique->dwgreg.dr[0] < 0) ? 0 : (mystique->dwgreg.dr[0] >> 15);
                            uint16_t old_z = z_p[x_l];
                            z_check_pass = z_check(z, old_z, mystique->dwgreg.dwgctrl_running & DWGCTRL_ZMODE_MASK);
                        }

                        if (z_check_pass) {
                            int tex_r = 0;
                            int tex_g = 0;
                            int tex_b = 0;
                            int tex_a = 255;
                            int ctransp;
                            int atransp = 0;
                            int i_r = 0;
                            int i_g = 0;
                            int i_b = 0;
                            int i_a = 255;
                            int i_fog = 0;
                            uint8_t final_a = 255;

                            if (!(mystique->dwgreg.dr[4] & (1 << 23)))
                                i_r = (mystique->dwgreg.dr[4] >> 15) & 0xff;
                            if (!(mystique->dwgreg.dr[8] & (1 << 23)))
                                i_g = (mystique->dwgreg.dr[8] >> 15) & 0xff;
                            if (!(mystique->dwgreg.dr[12] & (1 << 23)))
                                i_b = (mystique->dwgreg.dr[12] >> 15) & 0xff;

                            if (mystique->type >= MGA_G100)
                            {
                                if (!(mystique->dwgreg.alphastart & (1 << 23)))
                                    i_a = (mystique->dwgreg.alphastart >> 15) & 0xff;
                                else
                                    i_a = 0;

                                if (!(mystique->dwgreg.fogstart & (1 << 23)))
                                    i_fog = (mystique->dwgreg.fogstart >> 15) & 0xff;
                                else
                                    i_fog = 0;
                            }

                            ctransp = texture_read(mystique, &tex_r, &tex_g, &tex_b, &atransp, &tex_a);

                            if (mystique->type >= MGA_G100)
                            {
                                uint8_t alpha_sel = (mystique->dwgreg.alphactrl >> 24) & 3;

                                switch (alpha_sel)
                                {
                                    case 0x0: /* alpha from texture */
                                        final_a = tex_a;
                                        break;
                                    default:
                                    case 0x1: /* interpolated alpha */
                                        if ((mystique->dwgreg.alphactrl & (1 << 11)))
                                            final_a = i_a;
                                        break;
                                    case 0x2: /* modulated alpha */
                                        if (!(mystique->dwgreg.alphactrl & (1 << 11)))
                                            final_a = tex_a;
                                        else
                                            final_a = ((i_a * tex_a) >> 8) & 0xFF;
                                        break;
                                }
                            }

                            switch (mystique->dwgreg.texctl & (TEXCTL_TMODULATE | TEXCTL_STRANS | TEXCTL_ITRANS | TEXCTL_DECALCKEY)) {
                                case 0:
                                    if (ctransp)
                                        goto skip_pixel;
                                    if (atransp) {
                                        tex_r = i_r;
                                        tex_g = i_g;
                                        tex_b = i_b;
                                    }
                                    break;

                                case TEXCTL_DECALCKEY:
                                    if (ctransp) {
                                        tex_r = i_r;
                                        tex_g = i_g;
                                        tex_b = i_b;
                                    }
                                    break;

                                case (TEXCTL_STRANS | TEXCTL_DECALCKEY):
                                    if (ctransp)
                                        goto skip_pixel;
                                    break;

                                case TEXCTL_TMODULATE:
                                    if (ctransp)
                                        goto skip_pixel;
                                    if (mystique->dwgreg.texctl & TEXCTL_TMODULATE) {
                                        tex_r = (tex_r * i_r) >> 8;
                                        tex_g = (tex_g * i_g) >> 8;
                                        tex_b = (tex_b * i_b) >> 8;
                                    }
                                    break;

                                case (TEXCTL_TMODULATE | TEXCTL_STRANS):
                                    if (ctransp || atransp)
                                        goto skip_pixel;
                                    if (mystique->dwgreg.texctl & TEXCTL_TMODULATE) {
                                        tex_r = (tex_r * i_r) >> 8;
                                        tex_g = (tex_g * i_g) >> 8;
                                        tex_b = (tex_b * i_b) >> 8;
                                    }
                                    break;

                                case (TEXCTL_STRANS | TEXCTL_ITRANS | TEXCTL_DECALCKEY):
                                    if (!ctransp)
                                        goto skip_pixel;

                                    tex_r = i_r;
                                    tex_g = i_g;
                                    tex_b = i_b;
                                    break;

                                default:
                                    fatal("Bad TEXCTL %08x %08x\n", mystique->dwgreg.texctl, mystique->dwgreg.texctl & (TEXCTL_TMODULATE | TEXCTL_STRANS | TEXCTL_ITRANS | TEXCTL_DECALCKEY));
                            }

                            if (mystique->type >= MGA_G100 && (mystique->maccess_running & MACCESS_FOGEN))
                            {
                                tex_r = (tex_r * ((i_fog) / 255.)) + (mystique->dwgreg.fogcol >> 16) * ((255 - i_fog) / 255.);
                                tex_g = (tex_g * ((i_fog) / 255.)) + ((mystique->dwgreg.fogcol >> 8) & 0xFF) * ((255 - i_fog) / 255.);
                                tex_b = (tex_b * ((i_fog) / 255.)) + ((mystique->dwgreg.fogcol) & 0xFF) * ((255 - i_fog) / 255.);
                            }

                            if (final_a != 255)
                            {
                                {
                                    double threshold = bayer_mat[mystique->dwgreg.selline & 3][x_l & 3];
                                    double final_a_frac = (final_a) / 255.;
                                    if (final_a_frac >= threshold) {
                                        final_a = 255;
                                    } else {
                                        goto skip_pixel;
                                    }
                                }
                            }

                            if (dest32) {
                                ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_l] = tex_b | (tex_g << 8) | (tex_r << 16);
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_l) >> 10] = changeframecount;
                            } else {
                                ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_w] = dither(mystique, tex_r, tex_g, tex_b, x_l & 1, mystique->dwgreg.selline & 1);
                                svga->changedvram[((mystique->dwgreg.ydst_lin + x_l) & mystique->vram_mask_w) >> 11] = changeframecount;
                            }
                            if (z_write) {
                                if (mystique->maccess_running & MACCESS_ZWIDTH) {
                                    *(uint32_t*)(&z_p[x_l * 2]) = (mystique->dwgreg.extended_dr[0] & (1ull << 47ull)) ? 0 : (mystique->dwgreg.extended_dr[0] >> 15ull);
                                }
                                else
                                    z_p[x_l] = ((int32_t) mystique->dwgreg.dr[0] < 0) ? 0 : (mystique->dwgreg.dr[0] >> 15);
                            }
                        }
                    }
skip_pixel:
                    if (x_l > x_r)
                        x_l--;
                    else
                        x_l++;

                    if (mystique->maccess_running & MACCESS_ZWIDTH) {
                        mystique->dwgreg.extended_dr[0] += mystique->dwgreg.extended_dr[2];
                        mystique->dwgreg.dr[0] = (mystique->dwgreg.extended_dr[0] >> 16) & 0xFFFFFFFF;
                    } else {
                        mystique->dwgreg.dr[0] += mystique->dwgreg.dr[2];
                        mystique->dwgreg.extended_dr[0] = (mystique->dwgreg.extended_dr[0] & ~0xFFFFull) | ((uint64_t)mystique->dwgreg.dr[0] << 16ull);
                    }
                    mystique->dwgreg.dr[4] += mystique->dwgreg.dr[6];
                    mystique->dwgreg.dr[8] += mystique->dwgreg.dr[10];
                    mystique->dwgreg.dr[12] += mystique->dwgreg.dr[14];
                    mystique->dwgreg.tmr[6] += mystique->dwgreg.tmr[0];
                    mystique->dwgreg.tmr[7] += mystique->dwgreg.tmr[2];
                    mystique->dwgreg.tmr[8] += mystique->dwgreg.tmr[4];
                    mystique->dwgreg.fogstart += mystique->dwgreg.fogxinc;
                    mystique->dwgreg.alphastart += mystique->dwgreg.alphaxinc;
                    mystique->dwgreg.fogstart &= 0xFFFFFF;
                    mystique->dwgreg.alphastart &= 0xFFFFFF;
                }

                if (mystique->maccess_running & MACCESS_ZWIDTH) {
                    mystique->dwgreg.extended_dr[0] = z_back_32 + mystique->dwgreg.extended_dr[3];
                    mystique->dwgreg.dr[0] = (mystique->dwgreg.extended_dr[0] >> 16) & 0xFFFFFFFF;
                } else {
                    mystique->dwgreg.dr[0] = z_back + mystique->dwgreg.dr[3];
                    mystique->dwgreg.extended_dr[0] = (mystique->dwgreg.extended_dr[0] & ~0xFFFFull) | ((uint64_t)mystique->dwgreg.dr[0] << 16ull);
                }
                mystique->dwgreg.dr[4]      = r_back + mystique->dwgreg.dr[7];
                mystique->dwgreg.dr[8]      = g_back + mystique->dwgreg.dr[11];
                mystique->dwgreg.dr[12]     = b_back + mystique->dwgreg.dr[15];
                mystique->dwgreg.tmr[6]     = s_back + mystique->dwgreg.tmr[1];
                mystique->dwgreg.tmr[7]     = t_back + mystique->dwgreg.tmr[3];
                mystique->dwgreg.tmr[8]     = q_back + mystique->dwgreg.tmr[5];
                mystique->dwgreg.fogstart   = fog_back + mystique->dwgreg.fogyinc;
                mystique->dwgreg.alphastart = a_back + mystique->dwgreg.alphayinc;
                mystique->dwgreg.fogstart &= 0xFFFFFF;
                mystique->dwgreg.alphastart &= 0xFFFFFF;

                while ((int32_t) mystique->dwgreg.ar[1] < 0 && mystique->dwgreg.ar[0]) {
                    mystique->dwgreg.ar[1] += mystique->dwgreg.ar[0];
                    mystique->dwgreg.fxleft += (mystique->dwgreg.sgn.sdxl ? -1 : 1);
                }
                mystique->dwgreg.ar[1] += mystique->dwgreg.ar[2];

                while ((int32_t) mystique->dwgreg.ar[4] < 0 && mystique->dwgreg.ar[6]) {
                    mystique->dwgreg.ar[4] += mystique->dwgreg.ar[6];
                    mystique->dwgreg.fxright += (mystique->dwgreg.sgn.sdxr ? -1 : 1);
                }
                mystique->dwgreg.ar[4] += mystique->dwgreg.ar[5];

                dx = (int16_t) ((mystique->dwgreg.fxleft - old_x_l) & 0xffff);
                if (mystique->maccess_running & MACCESS_ZWIDTH) {
                    mystique->dwgreg.extended_dr[0] += dx * mystique->dwgreg.extended_dr[2];
                    mystique->dwgreg.dr[0] = (mystique->dwgreg.extended_dr[0] >> 16) & 0xFFFFFFFF;
                } else {
                    mystique->dwgreg.dr[0] += dx * mystique->dwgreg.dr[2];
                    mystique->dwgreg.extended_dr[0] = (mystique->dwgreg.extended_dr[0] & ~0xFFFFull) | ((uint64_t)mystique->dwgreg.dr[0] << 16ull);
                }
                mystique->dwgreg.dr[4] += dx * mystique->dwgreg.dr[6];
                mystique->dwgreg.dr[8] += dx * mystique->dwgreg.dr[10];
                mystique->dwgreg.dr[12] += dx * mystique->dwgreg.dr[14];
                mystique->dwgreg.tmr[6] += dx * mystique->dwgreg.tmr[0];
                mystique->dwgreg.tmr[7] += dx * mystique->dwgreg.tmr[2];
                mystique->dwgreg.tmr[8] += dx * mystique->dwgreg.tmr[4];
                mystique->dwgreg.fogstart += dx * mystique->dwgreg.fogxinc;
                mystique->dwgreg.alphastart += dx * mystique->dwgreg.alphaxinc;
                mystique->dwgreg.fogstart &= 0xFFFFFF;
                mystique->dwgreg.alphastart &= 0xFFFFFF;

                mystique->dwgreg.ydst++;
                mystique->dwgreg.ydst &= 0x7fffff;
                mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);

                mystique->dwgreg.selline = (mystique->dwgreg.selline + 1) & 7;
            }
            break;

        default:
            fatal("Unknown atype %03x %08x TEXTURE_TRAP\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK, mystique->dwgreg.dwgctrl_running);
    }

    mystique->blitter_complete_refcount++;
}

static void
blit_bitblt(mystique_t *mystique)
{
    svga_t   *svga = &mystique->svga;
    uint32_t  src_addr;
    int       y;
    int       x_dir     = mystique->dwgreg.sgn.scanleft ? -1 : 1;
    int16_t   x_start   = mystique->dwgreg.sgn.scanleft ? mystique->dwgreg.fxright : mystique->dwgreg.fxleft;
    int16_t   x_end     = mystique->dwgreg.sgn.scanleft ? mystique->dwgreg.fxleft : mystique->dwgreg.fxright;
    const int trans_sel = (mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANS_MASK) >> DWGCTRL_TRANS_SHIFT;
    uint32_t  bltckey   = mystique->dwgreg.fcol;
    uint32_t  bltcmsk   = mystique->dwgreg.bcol;

    switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
        case MACCESS_PWIDTH_8:
            bltckey &= 0xff;
            bltcmsk &= 0xff;
            break;
        case MACCESS_PWIDTH_16:
            bltckey &= 0xffff;
            bltcmsk &= 0xffff;
            break;
    }

    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) {
        case DWGCTRL_ATYPE_BLK:
            switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) {
                case DWGCTRL_BLTMOD_BMONOLEF:
                case DWGCTRL_BLTMOD_BMONOWF:
                    src_addr = mystique->dwgreg.ar[3];

                    for (y = 0; y < mystique->dwgreg.length; y++) {
                        int16_t x = x_start;

                        while (1) {
                            if (x >= mystique->dwgreg.cxleft && x <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot) {
                                uint32_t byte_addr  = (src_addr >> 3) & mystique->vram_mask;
                                int      bit_offset = ((mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) == DWGCTRL_BLTMOD_BMONOWF) ? (7 - (src_addr & 7)) : (src_addr & 7);
                                uint32_t old_dst;

                                switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                                    case MACCESS_PWIDTH_8:
                                        if (mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC) {
                                            if (svga->vram[byte_addr] & (1 << bit_offset))
                                                svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask] = mystique->dwgreg.fcol;
                                        } else
                                            svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask] = (svga->vram[byte_addr] & (1 << bit_offset)) ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask) >> 12] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_16:
                                        if (mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC) {
                                            if (svga->vram[byte_addr] & (1 << bit_offset))
                                                ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w] = mystique->dwgreg.fcol;
                                        } else
                                            ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w] = (svga->vram[byte_addr] & (1 << bit_offset)) ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w) >> 11] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_24:
                                        old_dst = *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask];
                                        if (mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC) {
                                            if (svga->vram[byte_addr] & (1 << bit_offset))
                                                *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask] = (old_dst & 0xff000000) | (mystique->dwgreg.fcol & 0xffffff);
                                        } else
                                            *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask] = (old_dst & 0xff000000) | (((svga->vram[byte_addr] & (1 << bit_offset)) ? mystique->dwgreg.fcol : mystique->dwgreg.bcol) & 0xffffff);
                                        svga->changedvram[(((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask) >> 12] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_32:
                                        if (mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC) {
                                            if (svga->vram[byte_addr] & (1 << bit_offset))
                                                ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l] = mystique->dwgreg.fcol;
                                        } else
                                            ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l] = (svga->vram[byte_addr] & (1 << bit_offset)) ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l) >> 11] = changeframecount;
                                        break;

                                    default:
                                        fatal("BITBLT DWGCTRL_ATYPE_BLK unknown MACCESS %i\n", mystique->maccess_running & MACCESS_PWIDTH_MASK);
                                }
                            }

                            if (src_addr == mystique->dwgreg.ar[0]) {
                                mystique->dwgreg.ar[0] += mystique->dwgreg.ar[5];
                                mystique->dwgreg.ar[3] += mystique->dwgreg.ar[5];
                                src_addr = mystique->dwgreg.ar[3];
                                break;
                            } else
                                src_addr += x_dir;

                            if (x != x_end)  {
                                if ((x > x_end) && (x_dir == 1))
                                    x--;
                                else if ((x < x_end) && (x_dir == -1))
                                    x++;
                                else
                                    x += x_dir;
                            } else
                                break;
                        }

                        if (mystique->dwgreg.sgn.sdy)
                            mystique->dwgreg.ydst_lin -= (mystique->dwgreg.pitch & PITCH_MASK);
                        else
                            mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
                    }
                    break;

                default:
                    fatal("BITBLT BLK %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK);
                    break;
            }
            break;

        case DWGCTRL_ATYPE_RPL:
            if (mystique->maccess_running & MACCESS_TLUTLOAD) {
                src_addr = mystique->dwgreg.ar[3];

                y = mystique->dwgreg.ydst;

                while (mystique->dwgreg.length) {
                    uint16_t src = ((uint16_t *) svga->vram)[src_addr & mystique->vram_mask_w];

                    mystique->lut[y & 0xff].r = (src >> 11) << 3;
                    mystique->lut[y & 0xff].g = ((src >> 5) & 0x3f) << 2;
                    mystique->lut[y & 0xff].b = (src & 0x1f) << 3;
                    src_addr++;
                    y++;
                    mystique->dwgreg.length--;
                }
                break;
            }
        case DWGCTRL_ATYPE_RSTR:
            switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) {
                /* TODO: This isn't exactly perfect. */
                case DWGCTRL_BLTMOD_BPLAN:
                    if (mystique->dwgreg.dwgctrl_running & DWGCTRL_PATTERN)
                        fatal("BITBLT RPL/RSTR BPLAN with pattern\n");

                    src_addr = mystique->dwgreg.ar[3];

                    for (y = 0; y < mystique->dwgreg.length; y++) {
                        uint8_t const *const trans = &trans_masks[trans_sel][(mystique->dwgreg.selline & 3) * 4];
                        int16_t              x     = x_start;

                        while (1) {
                            uint32_t byte_addr = src_addr & mystique->vram_mask;

                            if (x >= mystique->dwgreg.cxleft && x <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && ((svga->vram[byte_addr] & 1) || !(mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC)) && trans[x & 3]) {
                                uint32_t src = (svga->vram[byte_addr] & 1) ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                                uint32_t dst;
                                uint32_t old_dst;

                                switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                                    case MACCESS_PWIDTH_8:
                                        dst = svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask];

                                        dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                        svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask]                = dst;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask) >> 12] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_16:
                                        dst = ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w];

                                        dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                        ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w] = dst;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w) >> 11] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_24:
                                        old_dst = *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask];

                                        dst = bitop(src, old_dst, mystique->dwgreg.dwgctrl_running); // & DWGCTRL_BOP_MASK

                                        *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask] = (dst & 0xffffff) | (old_dst & 0xff000000);
                                        svga->changedvram[(((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask) >> 12] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_32:
                                        dst = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l];

                                        dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                        ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l] = dst;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l) >> 10] = changeframecount;
                                        break;

                                    default:
                                        fatal("BITBLT RPL BPLAN PWIDTH %x %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK, mystique->dwgreg.dwgctrl_running);
                                }
                            }

                            if (src_addr == mystique->dwgreg.ar[0]) {
                                mystique->dwgreg.ar[0] += mystique->dwgreg.ar[5];
                                mystique->dwgreg.ar[3] += mystique->dwgreg.ar[5];
                                src_addr = mystique->dwgreg.ar[3];
                                break;
                            } else
                                src_addr += x_dir;

                            if (x != x_end)  {
                                if ((x > x_end) && (x_dir == 1))
                                    x--;
                                else if ((x < x_end) && (x_dir == -1))
                                    x++;
                                else
                                    x += x_dir;
                            } else
                                break;
                        }

                        if (mystique->dwgreg.sgn.sdy)
                            mystique->dwgreg.ydst_lin -= (mystique->dwgreg.pitch & PITCH_MASK);
                        else
                            mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
                    }
                    break;
                case DWGCTRL_BLTMOD_BMONOLEF:
                case DWGCTRL_BLTMOD_BMONOWF:
                    if (mystique->dwgreg.dwgctrl_running & DWGCTRL_PATTERN)
                        fatal("BITBLT RPL/RSTR BMONOLEF with pattern\n");

                    src_addr = mystique->dwgreg.ar[3];

                    for (y = 0; y < mystique->dwgreg.length; y++) {
                        uint8_t const *const trans = &trans_masks[trans_sel][(mystique->dwgreg.selline & 3) * 4];
                        int16_t              x     = x_start;

                        while (1) {
                            uint32_t byte_addr  = (src_addr >> 3) & mystique->vram_mask;
                            int      bit_offset = ((mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) == DWGCTRL_BLTMOD_BMONOWF) ? (7 - (src_addr & 7)) : (src_addr & 7);

                            if (x >= mystique->dwgreg.cxleft && x <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && ((svga->vram[byte_addr] & (1 << bit_offset)) || !(mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC)) && trans[x & 3]) {
                                uint32_t src = (svga->vram[byte_addr] & (1 << bit_offset)) ? mystique->dwgreg.fcol : mystique->dwgreg.bcol;
                                uint32_t dst;
                                uint32_t old_dst;

                                switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                                    case MACCESS_PWIDTH_8:
                                        dst = svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask];

                                        dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                        svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask]                = dst;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask) >> 12] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_16:
                                        dst = ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w];

                                        dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                        ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w] = dst;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w) >> 11] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_24:
                                        old_dst = *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask];

                                        dst = bitop(src, old_dst, mystique->dwgreg.dwgctrl_running); // & DWGCTRL_BOP_MASK

                                        *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask] = (dst & 0xffffff) | (old_dst & 0xff000000);
                                        svga->changedvram[(((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask) >> 12] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_32:
                                        dst = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l];

                                        dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                        ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l] = dst;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l) >> 10] = changeframecount;
                                        break;

                                    default:
                                        fatal("BITBLT RPL BMONOLEF PWIDTH %x %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK, mystique->dwgreg.dwgctrl_running);
                                }
                            }

                            if (src_addr == mystique->dwgreg.ar[0]) {
                                mystique->dwgreg.ar[0] += mystique->dwgreg.ar[5];
                                mystique->dwgreg.ar[3] += mystique->dwgreg.ar[5];
                                src_addr = mystique->dwgreg.ar[3];
                                break;
                            } else
                                src_addr += x_dir;

                            if (x != x_end)  {
                                if ((x > x_end) && (x_dir == 1))
                                    x--;
                                else if ((x < x_end) && (x_dir == -1))
                                    x++;
                                else
                                    x += x_dir;
                            } else
                                break;
                        }

                        if (mystique->dwgreg.sgn.sdy)
                            mystique->dwgreg.ydst_lin -= (mystique->dwgreg.pitch & PITCH_MASK);
                        else
                            mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
                    }
                    break;

                case DWGCTRL_BLTMOD_BFCOL:
                case DWGCTRL_BLTMOD_BU32RGB:
                    src_addr = mystique->dwgreg.ar[3];

                    for (y = 0; y < mystique->dwgreg.length; y++) {
                        uint8_t const *const trans        = &trans_masks[trans_sel][(mystique->dwgreg.selline & 3) * 4];
                        uint32_t             old_src_addr = src_addr;
                        int16_t              x            = x_start;

                        while (1) {
                            if (x >= mystique->dwgreg.cxleft && x <= mystique->dwgreg.cxright && mystique->dwgreg.ydst_lin >= mystique->dwgreg.ytop && mystique->dwgreg.ydst_lin <= mystique->dwgreg.ybot && trans[x & 3]) {
                                uint32_t src;
                                uint32_t dst;
                                uint32_t old_dst;

                                switch (mystique->maccess_running & MACCESS_PWIDTH_MASK) {
                                    case MACCESS_PWIDTH_8:
                                        src = svga->vram[src_addr & mystique->vram_mask];
                                        dst = svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask];
                                        if (!((!(mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC) || (src & bltcmsk) != bltckey)))
                                            break;

                                        dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                        svga->vram[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask]                = dst;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask) >> 12] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_16:
                                        src = ((uint16_t *) svga->vram)[src_addr & mystique->vram_mask_w];
                                        dst = ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w];
                                        if (!((!(mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC) || (src & bltcmsk) != bltckey)))
                                            break;

                                        dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                        ((uint16_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w] = dst;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_w) >> 11] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_24:
                                        src     = *(uint32_t *) &svga->vram[(src_addr * 3) & mystique->vram_mask];
                                        old_dst = *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask];
                                        if (!((!(mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC) || (src & bltcmsk) != bltckey)))
                                            break;

                                        dst = bitop(src, old_dst, mystique->dwgreg.dwgctrl_running);

                                        *(uint32_t *) &svga->vram[((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask] = (dst & 0xffffff) | (old_dst & 0xff000000);
                                        svga->changedvram[(((mystique->dwgreg.ydst_lin + x) * 3) & mystique->vram_mask) >> 12] = changeframecount;
                                        break;

                                    case MACCESS_PWIDTH_32:
                                        src = ((uint32_t *) svga->vram)[src_addr & mystique->vram_mask_l];
                                        dst = ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l];
                                        if (!((!(mystique->dwgreg.dwgctrl_running & DWGCTRL_TRANSC) || (src & bltcmsk) != bltckey)))
                                            break;

                                        dst = bitop(src, dst, mystique->dwgreg.dwgctrl_running);

                                        ((uint32_t *) svga->vram)[(mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l] = dst;
                                        svga->changedvram[((mystique->dwgreg.ydst_lin + x) & mystique->vram_mask_l) >> 10] = changeframecount;
                                        break;

                                    default:
                                        fatal("BITBLT RPL BFCOL PWIDTH %x %08x\n", mystique->maccess_running & MACCESS_PWIDTH_MASK, mystique->dwgreg.dwgctrl_running);
                                }
                            }

                            if (mystique->dwgreg.dwgctrl_running & DWGCTRL_PATTERN)
                                src_addr = ((src_addr + x_dir) & 7) | (src_addr & ~7);
                            else if (src_addr == mystique->dwgreg.ar[0]) {
                                mystique->dwgreg.ar[0] += mystique->dwgreg.ar[5];
                                mystique->dwgreg.ar[3] += mystique->dwgreg.ar[5];
                                src_addr = mystique->dwgreg.ar[3];
                                break;
                            } else
                                src_addr += x_dir;

                            if (x != x_end)  {
                                if ((x > x_end) && (x_dir == 1))
                                    x--;
                                else if ((x < x_end) && (x_dir == -1))
                                    x++;
                                else
                                    x += x_dir;
                            } else
                                break;
                        }

                        if (mystique->dwgreg.dwgctrl_running & DWGCTRL_PATTERN) {
                            src_addr = old_src_addr;
                            if (mystique->dwgreg.sgn.sdy)
                                src_addr = ((src_addr - 32) & 0xe0) | (src_addr & ~0xe0);
                            else
                                src_addr = ((src_addr + 32) & 0xe0) | (src_addr & ~0xe0);
                        }

                        if (mystique->dwgreg.sgn.sdy)
                            mystique->dwgreg.ydst_lin -= (mystique->dwgreg.pitch & PITCH_MASK);
                        else
                            mystique->dwgreg.ydst_lin += (mystique->dwgreg.pitch & PITCH_MASK);
                    }
                    break;

                default:
                    fatal("BITBLT DWGCTRL_ATYPE_RPL unknown BLTMOD %08x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK, mystique->dwgreg.dwgctrl_running);
            }
            break;

        default:
#if 0
            pclog("Unknown BITBLT atype %03x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK, mystique->dwgreg.dwgctrl_running);
#endif
            break;
    }

    mystique->blitter_complete_refcount++;
}

static void
blit_iload(mystique_t *mystique)
{
    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) {
        case DWGCTRL_ATYPE_RPL:
        case DWGCTRL_ATYPE_RSTR:
        case DWGCTRL_ATYPE_BLK:
#if 0
            pclog("ILOAD BLTMOD DWGCTRL = %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK);
#endif
            switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) {
                case DWGCTRL_BLTMOD_BFCOL:
                case DWGCTRL_BLTMOD_BMONOLEF:
                case DWGCTRL_BLTMOD_BMONOWF:
                case DWGCTRL_BLTMOD_BU24RGB:
                case DWGCTRL_BLTMOD_BU32RGB:
                    mystique->dwgreg.length_cur      = mystique->dwgreg.length;
                    mystique->dwgreg.xdst            = mystique->dwgreg.fxleft;
                    mystique->dwgreg.iload_rem_data  = 0;
                    mystique->dwgreg.iload_rem_count = 0;
                    mystique->busy                   = 1;
#if 0
                    pclog("ILOAD busy\n");
#endif
                    mystique->dwgreg.words = 0;
                    break;

                default:
                    fatal("ILOAD DWGCTRL_ATYPE_RPL %08x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK, mystique->dwgreg.dwgctrl_running);
                    break;
            }
            break;

        default:
            fatal("Unknown ILOAD atype %03x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK, mystique->dwgreg.dwgctrl_running);
    }
}

static void
blit_idump(mystique_t *mystique)
{
    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) {
        case DWGCTRL_ATYPE_RPL:
            mystique->dwgreg.length_cur        = mystique->dwgreg.length;
            mystique->dwgreg.xdst              = mystique->dwgreg.fxleft;
            mystique->dwgreg.src_addr          = mystique->dwgreg.ar[3];
            mystique->dwgreg.words             = 0;
            mystique->dwgreg.iload_rem_count   = 0;
            mystique->dwgreg.iload_rem_data    = 0;
            mystique->dwgreg.idump_end_of_line = 0;
            mystique->busy                     = 1;
#if 0
            pclog("IDUMP ATYPE RPL busy\n");
#endif
            break;

        default:
            fatal("Unknown IDUMP atype %03x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK, mystique->dwgreg.dwgctrl_running);
    }
}

static void
blit_iload_scale(mystique_t *mystique)
{
    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) {
        case DWGCTRL_ATYPE_RPL:
            switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) {
                case DWGCTRL_BLTMOD_BUYUV:
                    mystique->dwgreg.length_cur      = mystique->dwgreg.length;
                    mystique->dwgreg.xdst            = mystique->dwgreg.fxleft;
                    mystique->dwgreg.iload_rem_data  = 0;
                    mystique->dwgreg.iload_rem_count = 0;
                    mystique->busy                   = 1;
                    mystique->dwgreg.words           = 0;
                    /* pclog("ILOAD SCALE ATYPE RPL BLTMOD BUYUV busy\n"); */
                    break;

                default:
                    fatal("ILOAD_SCALE DWGCTRL_ATYPE_RPL %08x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK, mystique->dwgreg.dwgctrl_running);
                    break;
            }
            break;

        default:
            fatal("Unknown ILOAD_SCALE atype %03x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK, mystique->dwgreg.dwgctrl_running);
    }
}

static void
blit_iload_high(mystique_t *mystique)
{
    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) {
        case DWGCTRL_ATYPE_RPL:
            switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) {
                case DWGCTRL_BLTMOD_BUYUV:
                case DWGCTRL_BLTMOD_BU32BGR:
                    mystique->dwgreg.length_cur      = mystique->dwgreg.length;
                    mystique->dwgreg.xdst            = mystique->dwgreg.fxleft;
                    mystique->dwgreg.iload_rem_data  = 0;
                    mystique->dwgreg.iload_rem_count = 0;
                    mystique->busy                   = 1;
                    mystique->dwgreg.words           = 0;
                    /* pclog("ILOAD HIGH ATYPE RPL BLTMOD BUYUV busy\n"); */
                    break;

                default:
                    fatal("ILOAD_HIGH DWGCTRL_ATYPE_RPL %08x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK, mystique->dwgreg.dwgctrl_running);
                    break;
            }
            break;

        default:
            fatal("Unknown ILOAD_HIGH atype %03x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK, mystique->dwgreg.dwgctrl_running);
    }
}

static void
blit_iload_highv(mystique_t *mystique)
{
    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK) {
        case DWGCTRL_ATYPE_RPL:
            switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK) {
                case DWGCTRL_BLTMOD_BUYUV:
                    mystique->dwgreg.length_cur      = mystique->dwgreg.length;
                    mystique->dwgreg.xdst            = mystique->dwgreg.fxleft;
                    mystique->dwgreg.iload_rem_data  = 0;
                    mystique->dwgreg.iload_rem_count = 0;
                    mystique->busy                   = 1;
                    mystique->dwgreg.words           = 0;
                    mystique->dwgreg.highv_line      = 0;
                    mystique->dwgreg.lastpix_r       = 0;
                    mystique->dwgreg.lastpix_g       = 0;
                    mystique->dwgreg.lastpix_b       = 0;
                    /* pclog("ILOAD HIGHV ATYPE RPL BLTMOD BUYUV busy\n"); */
                    break;

                default:
                    fatal("ILOAD_HIGHV DWGCTRL_ATYPE_RPL %08x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_BLTMOD_MASK, mystique->dwgreg.dwgctrl_running);
                    break;
            }
            break;

        default:
            fatal("Unknown ILOAD_HIGHV atype %03x %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_ATYPE_MASK, mystique->dwgreg.dwgctrl_running);
    }
}

static void
mystique_start_blit(mystique_t *mystique)
{
    svga_t *svga = &mystique->svga;
    uint64_t start_time = plat_timer_read();
    uint64_t end_time;

    /*Make sure we don't get any artifacts.*/
    svga->chain2_write = 0;
    svga->chain2_read = 0;

    mystique->dwgreg.dwgctrl_running = mystique->dwgreg.dwgctrl;
    mystique->maccess_running        = mystique->maccess;

    switch (mystique->dwgreg.dwgctrl_running & DWGCTRL_OPCODE_MASK) {
        case DWGCTRL_OPCODE_LINE_OPEN:
            blit_line_start(mystique, 0, 0);
            break;

        case DWGCTRL_OPCODE_AUTOLINE_OPEN:
            blit_line_start(mystique, 0, 1);
            break;

        case DWGCTRL_OPCODE_LINE_CLOSE:
            blit_line_start(mystique, 1, 0);
            break;

        case DWGCTRL_OPCODE_AUTOLINE_CLOSE:
            blit_line_start(mystique, 1, 1);
            break;

        case DWGCTRL_OPCODE_TRAP:
            blit_trap(mystique);
            break;

        case DWGCTRL_OPCODE_TEXTURE_TRAP:
            blit_texture_trap(mystique);
            break;

        case DWGCTRL_OPCODE_ILOAD_HIGH:
            blit_iload_high(mystique);
            break;

        case DWGCTRL_OPCODE_BITBLT:
            blit_bitblt(mystique);
            break;

        case DWGCTRL_OPCODE_FBITBLT:
            blit_fbitblt(mystique);
            break;

        case DWGCTRL_OPCODE_ILOAD:
            blit_iload(mystique);
            break;

        case DWGCTRL_OPCODE_IDUMP:
            blit_idump(mystique);
            break;

        case DWGCTRL_OPCODE_ILOAD_SCALE:
            blit_iload_scale(mystique);
            break;

        case DWGCTRL_OPCODE_ILOAD_HIGHV:
            blit_iload_highv(mystique);
            break;

        case DWGCTRL_OPCODE_ILOAD_FILTER:
            /* TODO: Actually implement this. */
            break;

        default:
            fatal("mystique_start_blit: unknown blit %08x\n", mystique->dwgreg.dwgctrl_running & DWGCTRL_OPCODE_MASK);
            break;
    }

    end_time = plat_timer_read();
    mystique->blitter_time += end_time - start_time;
}

static void
mystique_hwcursor_draw(svga_t *svga, int displine)
{
    const mystique_t *mystique = (mystique_t *) svga->priv;
    uint64_t          dat[2];
    int               offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;

    if (svga->interlace && svga->hwcursor_oddeven)
        svga->hwcursor_latch.addr += 16;

    dat[0] = *(uint64_t *) (&svga->vram[svga->hwcursor_latch.addr]);
    dat[1] = *(uint64_t *) (&svga->vram[svga->hwcursor_latch.addr + 8]);
    svga->hwcursor_latch.addr += 16;
    switch (mystique->xcurctrl & XCURCTRL_CURMODE_MASK) {
        case XCURCTRL_CURMODE_XGA:
            for (uint8_t x = 0; x < 64; x++) {
                if (!(dat[1] & (1ULL << 63)))
                    svga->monitor->target_buffer->line[displine][(offset + svga->x_add) & 2047] = (dat[0] & (1ULL << 63)) ? svga_lookup_lut_ram(svga, mystique->cursor.col[1]) : svga_lookup_lut_ram(svga, mystique->cursor.col[0]);
                else if (dat[0] & (1ULL << 63))
                    svga->monitor->target_buffer->line[displine][(offset + svga->x_add) & 2047] ^= 0xffffff;

                offset++;
                dat[0] <<= 1;
                dat[1] <<= 1;
            }
            break;

        case XCURCTRL_CURMODE_XWIN:
            for (uint8_t x = 0; x < 64; x++) {
                if ((dat[1] & (1ULL << 63)))
                    svga->monitor->target_buffer->line[displine][(offset + svga->x_add) & 2047] = (dat[0] & (1ULL << 63)) ? (mystique->cursor.col[1]) : (mystique->cursor.col[0]);

                offset++;
                dat[0] <<= 1;
                dat[1] <<= 1;
            }
            break;

        default:
            break;
    }

    if (svga->interlace && !svga->hwcursor_oddeven)
        svga->hwcursor_latch.addr += 16;
}

static uint8_t
mystique_tvp3026_gpio_read(UNUSED(uint8_t cntl), void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    uint8_t ret = 0xff;
    if (!i2c_gpio_get_scl(mystique->i2c_ddc))
        ret &= ~0x10;
    if (!i2c_gpio_get_sda(mystique->i2c_ddc))
        ret &= ~0x04;
    return ret;
}

static void
mystique_tvp3026_gpio_write(uint8_t cntl, uint8_t data, void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    i2c_gpio_set(mystique->i2c_ddc, !(cntl & 0x10) || (data & 0x10), !(cntl & 0x04) || (data & 0x04));
}

static uint8_t
mystique_pci_read(UNUSED(int func), int addr, void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;
    uint8_t     ret      = 0x00;

    if ((addr >= 0x30) && (addr <= 0x33) && !(mystique->pci_regs[0x43] & 0x40))
        ret = 0x00;
    else
        switch (addr) {
            case 0x00:
                ret = 0x2b;
                break; /*Matrox*/
            case 0x01:
                ret = 0x10;
                break;

            case 0x02:
                if (mystique->type == MGA_G100)
                    ret = 0x01;
                else
                    ret = (mystique->type == MGA_2164W) ? 0x1b : ((mystique->type == MGA_2064W) ? 0x19 : 0x1a);
                break; /*MGA*/
            case 0x03:
                if (mystique->type == MGA_G100)
                    ret = 0x10;
                else
                    ret = 0x05;
                break;

            case PCI_REG_COMMAND:
                ret = mystique->pci_regs[PCI_REG_COMMAND] | 0x80;
                break; /*Respond to IO and memory accesses*/
            case 0x05:
                ret = 0x00;
                break;

            case 0x06:
                ret = 0x80;
                break;
            case 0x07:
                ret = mystique->pci_regs[0x07];
                break; /*Fast DEVSEL timing*/

            case 0x08:
                ret = (mystique->type == MGA_1164SG) ? 3 : 0;
                break; /*Revision ID*/
            case 0x09:
                ret = 0;
                break; /*Programming interface*/

            case 0x0a:
                ret = 0x00;
                break; /*Supports VGA interface*/
            case 0x0b:
                ret = 0x03;
                break;

            case 0x10:
                ret = 0x00;
                break; /*Control aperture for Millennium and Mystique, LFB for Mystique 220 and later*/
            case 0x11:
                if (mystique->type >= MGA_1164SG)
                    ret = 0x00;
                else
                    ret = (mystique->ctrl_base >> 8) & 0xc0;
                break;
            case 0x12:
                if (mystique->type >= MGA_1164SG)
                    ret = (mystique->type >= MGA_2164W) ? 0x00 : ((mystique->lfb_base >> 16) & 0x80);
                else
                    ret = mystique->ctrl_base >> 16;
                break;
            case 0x13:
                if (mystique->type >= MGA_1164SG)
                    ret = mystique->lfb_base >> 24;
                else
                    ret = mystique->ctrl_base >> 24;
                break;

            case 0x14:
                ret = 0x00;
                break; /*LFB for Millennium and Mystique, Control aperture for Mystique 220 and later*/
            case 0x15:
                if (mystique->type >= MGA_1164SG)
                    ret = (mystique->ctrl_base >> 8) & 0xc0;
                else
                    ret = 0x00;
                break;
            case 0x16:
                if (mystique->type >= MGA_1164SG)
                    ret = mystique->ctrl_base >> 16;
                else
                    ret = (mystique->lfb_base >> 16) & 0x80;
                break;
            case 0x17:
                if (mystique->type >= MGA_1164SG)
                    ret = mystique->ctrl_base >> 24;
                else
                    ret = mystique->lfb_base >> 24;
                break;

            case 0x18:
                ret = 0x00;
                break; /*Pseudo-DMA (ILOAD)*/
            case 0x1a:
                ret = (mystique->iload_base >> 16) & 0x80;
                break;
            case 0x1b:
                ret = mystique->iload_base >> 24;
                break;

            case 0x2c:
                ret = mystique->pci_regs[0x2c];
                break;
            case 0x2d:
                ret = mystique->pci_regs[0x2d];
                break;
            case 0x2e:
                ret = mystique->pci_regs[0x2e];
                break;
            case 0x2f:
                ret = mystique->pci_regs[0x2f];
                break;

            case 0x30:
                ret = mystique->pci_regs[0x30] & 0x01;
                break; /*BIOS ROM address*/
            case 0x31:
                ret = 0x00;
                break;
            case 0x32:
                ret = mystique->pci_regs[0x32];
                break;
            case 0x33:
                ret = mystique->pci_regs[0x33];
                break;

            case 0x34:
                ret = (mystique->type == MGA_G100) ? 0xdc : 0x00;
                break;

            case 0x3c:
                ret = mystique->int_line;
                break;
            case 0x3d:
                ret = PCI_INTA;
                break;

            case 0x40:
                ret = mystique->pci_regs[0x40];
                break;
            case 0x41:
                ret = mystique->pci_regs[0x41];
                break;
            case 0x42:
                ret = mystique->pci_regs[0x42];
                break;
            case 0x43:
                ret = mystique->pci_regs[0x43];
                break;

            case 0x44:
                ret = mystique->pci_regs[0x44];
                break;
            case 0x45:
                ret = mystique->pci_regs[0x45];
                break;

            case 0x48:
            case 0x49:
            case 0x4a:
            case 0x4b:
                addr = (mystique->pci_regs[0x44] & 0xfc) | ((mystique->pci_regs[0x45] & 0x3f) << 8) | (addr & 3);
                ret  = mystique_ctrl_read_b(addr, mystique);
                break;

            case 0xdc:
                ret = 0x01;
                break;

            case 0xdd:
                ret = 0xf0;
                break;

            case 0xde:
                ret = 0x21;
                break;

            /* No support for turning off the video adapter yet. */
            case 0xe0:
                ret = 0x0;
                break;

            case 0xf0:
                ret = 0x02;
                break;

            case 0xf1:
                ret = 0x00;
                break;

            case 0xf2:
                ret = 0x10;
                break;

            case 0xf4:
                ret = 0x1;
                break;

            case 0xf5:
                ret = 0x2;
                break;

            case 0xf7:
                ret = 0x1;
                break;

            case 0xf8:
                ret = mystique->pci_regs[0xf8] & 0x7;
                break;

            case 0xf9:
                ret = mystique->pci_regs[0xf9] & 0x3;
                break;

            case 0xfb:
                ret = mystique->pci_regs[0xfb];
                break;

            default:
                break;
        }

    return ret;
}

static void
mystique_pci_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    switch (addr) {
        case PCI_REG_COMMAND:
            mystique->pci_regs[PCI_REG_COMMAND] = (val & 0x27) | 0x80;
            mystique_recalc_mapping(mystique);
            break;

        case 0x07:
            mystique->pci_regs[0x07] &= ~(val & 0x38);
            break;

        case 0x0d:
            mystique->pci_regs[0x0d] = val;
            break;

        case 0x11:
            if (mystique->type >= MGA_1164SG)
                break;
            else {
                mystique->ctrl_base = (mystique->ctrl_base & 0xffff0000) | ((val & 0xc0) << 8);
                mystique_recalc_mapping(mystique);
            }
            break;
        case 0x12:
            if (mystique->type >= MGA_1164SG) {
                if (mystique->type >= MGA_2164W)
                    break;
                mystique->lfb_base = (mystique->lfb_base & 0xff000000) | ((val & 0x80) << 16);
                mystique_recalc_mapping(mystique);
            } else {
                mystique->ctrl_base = (mystique->ctrl_base & 0xff00c000) | (val << 16);
                mystique_recalc_mapping(mystique);
            }
            break;
        case 0x13:
            if (mystique->type >= MGA_1164SG) {
                if (mystique->type >= MGA_2164W)
                    mystique->lfb_base = val << 24;
                else
                    mystique->lfb_base = (mystique->lfb_base & 0x00800000) | (val << 24);

                mystique_recalc_mapping(mystique);
            } else {
                mystique->ctrl_base = (mystique->ctrl_base & 0x00ffc000) | (val << 24);
                mystique_recalc_mapping(mystique);
            }
            break;

        case 0x15:
            if (mystique->type >= MGA_1164SG) {
                mystique->ctrl_base = (mystique->ctrl_base & 0xffff0000) | ((val & 0xc0) << 8);
                mystique_recalc_mapping(mystique);
            }
            break;
        case 0x16:
            if (mystique->type >= MGA_1164SG) {
                mystique->ctrl_base = (mystique->ctrl_base & 0xff00c000) | (val << 16);
                mystique_recalc_mapping(mystique);
            } else {
                mystique->lfb_base = (mystique->lfb_base & 0xff000000) | ((val & 0x80) << 16);
                mystique_recalc_mapping(mystique);
            }
            break;
        case 0x17:
            if (mystique->type >= MGA_1164SG) {
                mystique->ctrl_base = (mystique->ctrl_base & 0x00ffc000) | (val << 24);
                mystique_recalc_mapping(mystique);
            } else {
                mystique->lfb_base = (mystique->lfb_base & 0x00800000) | (val << 24);
                mystique_recalc_mapping(mystique);
            }
            break;

        case 0x1a:
            mystique->iload_base = (mystique->iload_base & 0xff000000) | ((val & 0x80) << 16);
            mystique_recalc_mapping(mystique);
            break;
        case 0x1b:
            mystique->iload_base = (mystique->iload_base & 0x00800000) | (val << 24);
            mystique_recalc_mapping(mystique);
            break;

        case 0x30:
        case 0x32:
        case 0x33:
            if (!(mystique->pci_regs[0x43] & 0x40))
                return;
            mystique->pci_regs[addr] = val;
            if (addr == 0x30)
                mystique->pci_regs[addr] &= 1;

            if (mystique->pci_regs[0x30] & 0x01) {
                uint32_t biosaddr = (mystique->pci_regs[0x32] << 16) | (mystique->pci_regs[0x33] << 24);
                mem_mapping_set_addr(&mystique->bios_rom.mapping, biosaddr, (mystique->type == MGA_G100) ? 0x10000 : 0x8000);
            } else
                mem_mapping_disable(&mystique->bios_rom.mapping);
            return;

        case 0x3c:
            mystique->int_line = val;
            return;

        case 0x40:
            mystique->pci_regs[0x40] = val & 0x3f;
            break;
        case 0x41:
            mystique->pci_regs[0x41] = val;
            break;
        case 0x42:
            mystique->pci_regs[0x42] = val & 0x1f;
            break;
        case 0x43:
            mystique->pci_regs[0x43] = val;
            if (val & 0x40) {
                if (mystique->pci_regs[0x30] & 0x01) {
                    uint32_t biosaddr = (mystique->pci_regs[0x32] << 16) | (mystique->pci_regs[0x33] << 24);
                    mem_mapping_set_addr(&mystique->bios_rom.mapping, biosaddr, (mystique->type == MGA_G100) ? 0x10000 : 0x8000);
                } else
                    mem_mapping_disable(&mystique->bios_rom.mapping);
            } else
                mem_mapping_set_addr(&mystique->bios_rom.mapping, 0x000c0000, (mystique->type == MGA_G100) ? 0x10000 : 0x8000);
            break;

        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
            mystique->pci_regs[addr - 0x20] = val;
            break;

        case 0x44:
            mystique->pci_regs[addr] = val & 0xfc;
            break;
        case 0x45:
            mystique->pci_regs[addr] = val & 0x3f;
            break;

        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
            addr = (mystique->pci_regs[0x44] & 0xfc) | ((mystique->pci_regs[0x45] & 0x3f) << 8) | (addr & 3);
#if 0
            pclog("mystique_ctrl_write_b(%04X, %02X)\n", addr, val);
#endif
            mystique_ctrl_write_b(addr, val, mystique);
            break;

        case 0xf8:
            mystique->pci_regs[0xf8] = val & 0x7;
            break;

        case 0xf9:
            mystique->pci_regs[0xf9] = val & 0x3;
            break;

        case 0xfb:
            mystique->pci_regs[0xfb] = val;
            break;

        default:
            break;
    }
}

static uint32_t
mystique_conv_16to32(svga_t* svga, uint16_t color, uint8_t bpp)
{
    mystique_t *mystique = (mystique_t*)svga->priv;
    uint32_t ret = 0x00000000;

    if (svga->lut_map) {
        if (bpp == 15) {
            if (mystique->xgenctrl & (1 << 2))
                color &= 0x7FFF;
#if 0
            uint8_t b = getcolr(svga->pallook[(color & 0x1F) | (!!(color & 0x8000) >> 8)]);
            uint8_t g = getcolg(svga->pallook[((color & 0x3E0) >> 5) | (!!(color & 0x8000) >> 8)]);
            uint8_t r = getcolb(svga->pallook[((color & 0x7C00) >> 10) | (!!(color & 0x8000) >> 8)]);
#else
            uint8_t b = getcolr(svga->pallook[color & 0x1f]);
            uint8_t g = getcolg(svga->pallook[(color & 0x3e0) >> 5]);
            uint8_t r = getcolb(svga->pallook[(color & 0x7c00) >> 10]);
#endif
            ret = (video_15to32[color] & 0xFF000000) | makecol(r, g, b);
        } else {
            uint8_t b = getcolr(svga->pallook[color & 0x1f]);
            uint8_t g = getcolg(svga->pallook[(color & 0x7e0) >> 5]);
            uint8_t r = getcolb(svga->pallook[(color & 0xf800) >> 11]);
            ret = (video_16to32[color] & 0xFF000000) | makecol(r, g, b);
        }
    } else
        ret = (bpp == 15) ? video_15to32[color] : video_16to32[color];

    return ret;
}

static void *
mystique_init(const device_t *info)
{
    mystique_t *mystique = malloc(sizeof(mystique_t));
    const char *romfn = NULL;

    memset(mystique, 0, sizeof(mystique_t));

    mystique->type = info->local;

    if (mystique->type == MGA_2064W)
        romfn = ROM_MILLENNIUM;
    else if (mystique->type == MGA_2164W)
        romfn = ROM_MILLENNIUM_II;
    else if (mystique->type == MGA_1064SG)
        romfn = ROM_MYSTIQUE;
    else if (mystique->type == MGA_G100)
        romfn = ROM_G100;
    else
        romfn = ROM_MYSTIQUE_220;

    if (mystique->type == MGA_G100)
        rom_init(&mystique->bios_rom, romfn, 0xc0000, 0x10000, 0xffff, 0, MEM_MAPPING_EXTERNAL);
    else
        rom_init(&mystique->bios_rom, romfn, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_disable(&mystique->bios_rom.mapping);

    mystique->vram_size   = device_get_config_int("memory");
    mystique->vram_mask   = (mystique->vram_size << 20) - 1;
    mystique->vram_mask_w = mystique->vram_mask >> 1;
    mystique->vram_mask_l = mystique->vram_mask >> 2;

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_matrox_mystique);

    if (mystique->type == MGA_2064W || mystique->type == MGA_2164W) {
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, (mystique->type == MGA_2164W) ? &timing_matrox_mystique : &timing_matrox_millennium);
        svga_init(info, &mystique->svga, mystique, mystique->vram_size << 20,
                  mystique_recalctimings,
                  mystique_in, mystique_out,
                  NULL,
                  NULL);
        mystique->svga.dac_hwcursor_draw = tvp3026_hwcursor_draw;
        mystique->svga.ramdac            = device_add(&tvp3026_ramdac_device);
        mystique->svga.clock_gen         = mystique->svga.ramdac;
        mystique->svga.getclock          = tvp3026_getclock;
        mystique->svga.conv_16to32       = tvp3026_conv_16to32;
        if (mystique->type == MGA_2164W)
            mystique->svga.decode_mask = 0xffffff;

        tvp3026_gpio(mystique_tvp3026_gpio_read, mystique_tvp3026_gpio_write, mystique, mystique->svga.ramdac);
    } else {
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_matrox_mystique);
        svga_init(info, &mystique->svga, mystique, mystique->vram_size << 20,
                  mystique_recalctimings,
                  mystique_in, mystique_out,
                  mystique_hwcursor_draw,
                  NULL);
        mystique->svga.clock_gen = mystique;
        mystique->svga.getclock  = mystique_getclock;
        if (mystique->type == MGA_G100)
            mystique->svga.decode_mask = 0xffffff;
    }

    io_sethandler(0x03c0, 0x0020, mystique_in, NULL, NULL, mystique_out, NULL, NULL, mystique);
    mem_mapping_add(&mystique->ctrl_mapping, 0, 0,
                    mystique_ctrl_read_b, NULL, mystique_ctrl_read_l,
                    mystique_ctrl_write_b, NULL, mystique_ctrl_write_l,
                    NULL, 0, mystique);
    mem_mapping_disable(&mystique->ctrl_mapping);

    mem_mapping_add(&mystique->lfb_mapping, 0, 0,
                    mystique_readb_linear, mystique_readw_linear, mystique_readl_linear,
                    mystique_writeb_linear, mystique_writew_linear, mystique_writel_linear,
                    NULL, 0, &mystique->svga);
    mem_mapping_disable(&mystique->lfb_mapping);

    mem_mapping_add(&mystique->iload_mapping, 0, 0,
                    mystique_iload_read_b, NULL, mystique_iload_read_l,
                    mystique_iload_write_b, NULL, mystique_iload_write_l,
                    NULL, 0, mystique);
    mem_mapping_disable(&mystique->iload_mapping);

    if (romfn == NULL)
        pci_add_card(PCI_ADD_VIDEO, mystique_pci_read, mystique_pci_write, mystique, &mystique->pci_slot);
    else
        pci_add_card((info->flags & DEVICE_AGP) ? PCI_ADD_AGP : PCI_ADD_NORMAL, mystique_pci_read, mystique_pci_write, mystique, &mystique->pci_slot);
    mystique->pci_regs[0x06] = 0x80;
    mystique->pci_regs[0x07] = 0 << 1;
    mystique->pci_regs[0x2c] = mystique->bios_rom.rom[0x7ff8];
    mystique->pci_regs[0x2d] = mystique->bios_rom.rom[0x7ff9];
    mystique->pci_regs[0x2e] = mystique->bios_rom.rom[0x7ffa];
    mystique->pci_regs[0x2f] = mystique->bios_rom.rom[0x7ffb];

    mystique->svga.miscout   = 1;
    mystique->pci_regs[0x41] = 0x01; /* vgaboot = 1 */
    mystique->pci_regs[0x43] = 0x40; /* biosen = 1 */

    for (uint16_t c = 0; c < 256; c++) {
        dither5[c][0][0] = c >> 3;
        dither5[c][1][1] = (c + 2) >> 3;
        dither5[c][1][0] = (c + 4) >> 3;
        dither5[c][0][1] = (c + 6) >> 3;

        if (dither5[c][1][1] > 31)
            dither5[c][1][1] = 31;
        if (dither5[c][1][0] > 31)
            dither5[c][1][0] = 31;
        if (dither5[c][0][1] > 31)
            dither5[c][0][1] = 31;

        dither6[c][0][0] = c >> 2;
        dither6[c][1][1] = (c + 1) >> 2;
        dither6[c][1][0] = (c + 2) >> 2;
        dither6[c][0][1] = (c + 3) >> 2;

        if (dither6[c][1][1] > 63)
            dither6[c][1][1] = 63;
        if (dither6[c][1][0] > 63)
            dither6[c][1][0] = 63;
        if (dither6[c][0][1] > 63)
            dither6[c][0][1] = 63;
    }

    mystique->wake_fifo_thread    = thread_create_event();
    mystique->fifo_not_full_event = thread_create_event();
    mystique->thread_run          = 1;
    mystique->fifo_thread         = thread_create(fifo_thread, mystique);
    mystique->dma.lock            = thread_create_mutex();

    timer_add(&mystique->wake_timer, mystique_wake_timer, (void *) mystique, 0);
    timer_add(&mystique->softrap_pending_timer, mystique_softrap_pending_timer, (void *) mystique, 1);

    mystique->status = STATUS_ENDPRDMASTS;

    mystique->softrap_status_read = 1;

    mystique->svga.vsync_callback = mystique_vsync_callback;

    if (mystique->type != MGA_2064W && mystique->type != MGA_2164W)
        mystique->svga.conv_16to32    = mystique_conv_16to32;

    mystique->i2c     = i2c_gpio_init("i2c_mga");
    mystique->i2c_ddc = i2c_gpio_init("ddc_mga");
    mystique->ddc     = ddc_init(i2c_gpio_get_bus(mystique->i2c_ddc));

    return mystique;
}

static void
mystique_close(void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    mystique->thread_run = 0;
    thread_set_event(mystique->wake_fifo_thread);
    thread_wait(mystique->fifo_thread);
    thread_destroy_event(mystique->wake_fifo_thread);
    thread_destroy_event(mystique->fifo_not_full_event);
    thread_close_mutex(mystique->dma.lock);

    svga_close(&mystique->svga);

    ddc_close(mystique->ddc);
    i2c_gpio_close(mystique->i2c_ddc);
    i2c_gpio_close(mystique->i2c);

    free(mystique);
}

static int
millennium_available(void)
{
    return rom_present(ROM_MILLENNIUM);
}

static int
mystique_available(void)
{
    return rom_present(ROM_MYSTIQUE);
}

static int
mystique_220_available(void)
{
    return rom_present(ROM_MYSTIQUE_220);
}

static int
millennium_ii_available(void)
{
    return rom_present(ROM_MILLENNIUM_II);
}

#ifdef USE_G100
static int
matrox_g100_available(void)
{
    return rom_present(ROM_G100);
}
#endif

static void
mystique_speed_changed(void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    svga_recalctimings(&mystique->svga);
}

static void
mystique_force_redraw(void *priv)
{
    mystique_t *mystique = (mystique_t *) priv;

    mystique->svga.fullchange = changeframecount;
}

static const device_config_t mystique_config[] = {
  // clang-format off
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 8,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "2 MB", .value = 2 },
            { .description = "4 MB", .value = 4 },
            { .description = "8 MB", .value = 8 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t millennium_ii_config[] = {
  // clang-format off
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 8,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description =  "4 MB", .value =  4 },
            { .description =  "8 MB", .value =  8 },
            { .description = "16 MB", .value = 16 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t millennium_device = {
    .name          = "Matrox Millennium",
    .internal_name = "millennium",
    .flags         = DEVICE_PCI,
    .local         = MGA_2064W,
    .init          = mystique_init,
    .close         = mystique_close,
    .reset         = NULL,
    .available     = millennium_available,
    .speed_changed = mystique_speed_changed,
    .force_redraw  = mystique_force_redraw,
    .config        = mystique_config
};

const device_t mystique_device = {
    .name          = "Matrox Mystique",
    .internal_name = "mystique",
    .flags         = DEVICE_PCI,
    .local         = MGA_1064SG,
    .init          = mystique_init,
    .close         = mystique_close,
    .reset         = NULL,
    .available     = mystique_available,
    .speed_changed = mystique_speed_changed,
    .force_redraw  = mystique_force_redraw,
    .config        = mystique_config
};

const device_t mystique_220_device = {
    .name          = "Matrox Mystique 220",
    .internal_name = "mystique_220",
    .flags         = DEVICE_PCI,
    .local         = MGA_1164SG,
    .init          = mystique_init,
    .close         = mystique_close,
    .reset         = NULL,
    .available     = mystique_220_available,
    .speed_changed = mystique_speed_changed,
    .force_redraw  = mystique_force_redraw,
    .config        = mystique_config
};

const device_t millennium_ii_device = {
    .name          = "Matrox Millennium II",
    .internal_name = "millennium_ii",
    .flags         = DEVICE_PCI,
    .local         = MGA_2164W,
    .init          = mystique_init,
    .close         = mystique_close,
    .reset         = NULL,
    .available     = millennium_ii_available,
    .speed_changed = mystique_speed_changed,
    .force_redraw  = mystique_force_redraw,
    .config        = millennium_ii_config
};

#ifdef USE_G100
const device_t productiva_g100_device = {
    .name          = "Matrox Productiva G100",
    .internal_name = "productiva_g100",
    .flags         = DEVICE_AGP,
    .local         = MGA_G100,
    .init          = mystique_init,
    .close         = mystique_close,
    .reset         = NULL,
    .available     = matrox_g100_available,
    .speed_changed = mystique_speed_changed,
    .force_redraw  = mystique_force_redraw,
    .config        = millennium_ii_config
};
#endif
